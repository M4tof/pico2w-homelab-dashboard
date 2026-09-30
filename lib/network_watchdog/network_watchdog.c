#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "lwip/opt.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "secrets.h"
#include "network_watchdog.h"

NetHealthState_t currentHealthState = NET_HEALTH_OK;
static TickType_t lastMQTTTimestamp = 0;
static SemaphoreHandle_t watchdogMutex = NULL;
static bool alertDispatched = false;

static bool watchdog_enabled = false;

// External references from other parts of your app
extern int currentLEDState; // Used to override LED states or patterns
extern void mqtt_publish_message(const char *topic, const char *payload); // Or use http client for ntfy

void notify_mqtt_received(void) {
    if (watchdogMutex && xSemaphoreTake(watchdogMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        lastMQTTTimestamp = xTaskGetTickCount();
        watchdog_enabled = true;
        
        if (currentHealthState != NET_HEALTH_OK) {
            printf("[Watchdog] MQTT restored! Health normal.\n");
            alertDispatched = false; 
        }
        currentHealthState = NET_HEALTH_OK;
        xSemaphoreGive(watchdogMutex);
    }
}

// Simple raw TCP socket ping helper to test if an IP address is reachable on port 80 or 443 (or standard port check)
static bool check_ip_reachable(const char *ip_str, uint16_t port) {
    struct sockaddr_in dest;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    // Set a short 2-second receive/connect timeout via socket options
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));

    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    inet_pton(AF_INET, ip_str, &dest.sin_addr);

    // Attempt connection (acts as a TCP ping)
    int res = connect(sock, (struct sockaddr *)&dest, sizeof(dest));
    close(sock);
    return (res == 0);
}

// Send alert to ntfy.sh via a quick raw HTTP socket request
void send_ntfy_alert(const char *reason) {
    printf("[Watchdog] Sending NTFY Alert: %s\n", reason);
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("[Watchdog] Failed to create socket for ntfy\n");
        return;
    }

    // Set a strict 3-second timeout for connect and send operations 
    // so it never locks up the thread if the network is dropping out.
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(80); // HTTP port for ntfy.sh

    // Use ntfy.sh public IPv4 directly to completely avoid DNS lookup hanging (getaddrinfo)
    // ntfy.sh resolves to 159.203.148.75
    if (inet_pton(AF_INET, "159.203.148.75", &dest.sin_addr) <= 0) {
        printf("[Watchdog] Invalid ntfy IP configuration\n");
        close(sock);
        return;
    }

    // Attempt connection
    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        printf("[Watchdog] Connection to ntfy server failed (network offline?)\n");
        close(sock);
        return;
    }

    // Format the payload and HTTP POST request headers
    char msg[128];
    snprintf(msg, sizeof(msg), "CRITICAL: %s", reason);

    char req[512];
    int req_len = snprintf(req, sizeof(req),
        "POST /%s HTTP/1.1\r\n"
        "Host: ntfy.sh\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: text/plain\r\n"
        "Connection: close\r\n\r\n"
        "%s",
        NTFY_TOPIC, (int)strlen(msg), msg
    );

    // Send the request
    int written = write(sock, req, req_len);
    if (written > 0) {
        printf("[Watchdog] >>> [ntfy] Alert Sent Successfully!\n");
    } else {
        printf("[Watchdog] Failed to write ntfy payload\n");
    }

    // Always clean up the socket descriptor to prevent resource leaks
    close(sock);
}

// Background Watchdog Task Loop
static void network_watchdog_task(void *pvParameters) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000)); // Check every 10 seconds

        if (watchdog_enabled) {
            continue; 
        }

        TickType_t now = xTaskGetTickCount();
        TickType_t elapsed = 0;

        if (xSemaphoreTake(watchdogMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            elapsed = now - lastMQTTTimestamp;
            xSemaphoreGive(watchdogMutex);
        }

        // If no MQTT received for more than 30 seconds
        if (elapsed > pdMS_TO_TICKS(45000)) {
            printf("[Watchdog] WARNING: No MQTT received for > 30s. Probing network...\n");

            // Step 1: Try pinging primary server (192.168.1.242) on standard port 80/3000
            bool server_up = check_ip_reachable("192.168.1.242", 3000); 

            if (server_up) {
                currentHealthState = NET_HEALTH_NO_MQTT;
                printf("[Watchdog] Status: Server (192.168.1.242) is UP, but MQTT stream stalled.\n");
                if (!alertDispatched) {
                    // send_ntfy_alert("MQTT stream lost, but local server is reachable.");
                    alertDispatched = true;
                }
            } else {
                printf("[Watchdog] Primary server 192.168.1.242 is DOWN. Checking router...\n");
                
                // Step 2: Try pinging router (192.168.1.1) on port 80/53
                bool router_up = check_ip_reachable("192.168.1.1", 80);

                if (router_up) {
                    currentHealthState = NET_HEALTH_SERVER_DOWN;
                    printf("[Watchdog] Status: Router is UP, but target server is DOWN.\n");
                    if (!alertDispatched) {
                        // send_ntfy_alert("Local server 192.168.1.242 is down!");
                        alertDispatched = true;
                    }
                } else {
                    currentHealthState = NET_HEALTH_ROUTER_DOWN;
                    printf("[Watchdog] Status: CRITICAL - Router (192.168.1.1) is UNREACHABLE!\n");
                    if (!alertDispatched) {
                        // Can't send ntfy if router is dead, but log it heavily
                        alertDispatched = true;
                    }
                }
            }
        }
    }
}

void start_network_watchdog(void) {
    watchdogMutex = xSemaphoreCreateMutex();
    xTaskCreate(network_watchdog_task, "NetWatchdog", 2048, NULL, 1, NULL);
}