#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "lwip/opt.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cJSON.h"

// External variables declared in your other modules
extern int local_hh;
extern int local_mm;
extern int currentLightLevel;
extern int currentLEDState;
extern bool is_mqtt_connected(void);
extern float readInternalTemperature(void);
extern uint32_t getFreeHeap(void);

// A sample "writeable" variable to demonstrate handling POST requests from Zabbix/servers
static int userControlVariable = 0;

#define HTTP_PORT 80

// Helper to send standard HTTP responses
static void send_http_response(int client_sock, int status_code, const char *status_text, const char *content_type, const char *body) {
    int body_len = body ? strlen(body) : 0;
    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        status_code, status_text, content_type, body_len
    );

    write(client_sock, header, header_len);
    if (body_len > 0) {
        write(client_sock, body, body_len);
    }
}

// Handle GET requests (Read telemetry for Zabbix)
static char* handle_get_api(void) {
    cJSON *doc = cJSON_CreateObject();
    if (!doc) return NULL;

    // Populate JSON with live system metrics
    cJSON_AddStringToObject(doc, "device", "Pico2W_Dashboard");
    cJSON_AddStringToObject(doc, "firmware", FIRMWARE_VERSION);
    cJSON_AddNumberToObject(doc, "uptime_s", to_ms_since_boot(get_absolute_time()) / 1000);

    char timeBuf[6];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", local_hh, local_mm);
    cJSON_AddStringToObject(doc, "local_time", timeBuf);
    cJSON_AddNumberToObject(doc, "hour", local_hh);
    cJSON_AddNumberToObject(doc, "minute", local_mm);

    cJSON_AddNumberToObject(doc, "light_level", currentLightLevel);
    cJSON_AddNumberToObject(doc, "temp_c", (double)readInternalTemperature());
    cJSON_AddNumberToObject(doc, "free_ram_bytes", (double)getFreeHeap());
    cJSON_AddBoolToObject(doc, "mqtt_connected", is_mqtt_connected());
    cJSON_AddNumberToObject(doc, "led_status", currentLEDState);
    cJSON_AddNumberToObject(doc, "user_control_var", userControlVariable);

    char *json_string = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    return json_string;
}

// Handle POST requests (Write data to Pico)
static void handle_post_api(const char *body) {
    cJSON *doc = cJSON_Parse(body);
    if (!doc) return;

    // Example: Check if the incoming POST has a "set_var" key
    cJSON *var_item = cJSON_GetObjectItemCaseSensitive(doc, "set_var");
    if (cJSON_IsNumber(var_item)) {
        userControlVariable = var_item->valueint;
        printf("[API Server] Updated userControlVariable via POST to: %d\n", userControlVariable);
    }

    cJSON_Delete(doc);
}

// Main HTTP Server Task
static void http_server_task(void *pvParameters) {
    struct sockaddr_in address, client_address;
    int server_fd, client_sock;
    int client_len = sizeof(client_address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        printf("[API Server] Failed to create socket\n");
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(HTTP_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        printf("[API Server] Bind failed on port %d\n", HTTP_PORT);
        close(server_fd);
        vTaskDelete(NULL);
        return;
    }

    if (listen(server_fd, 5) < 0) {
        printf("[API Server] Listen failed\n");
        close(server_fd);
        vTaskDelete(NULL);
    }

    printf("[API Server] Running and listening on port %d...\n", HTTP_PORT);

    char rx_buffer[1024];

    while (true) {
        client_sock = accept(server_fd, (struct sockaddr *)&client_address, (socklen_t *)&client_len);
        if (client_sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue; 
        }

        int len = read(client_sock, rx_buffer, sizeof(rx_buffer) - 1);
        if (len > 0) {
            rx_buffer[len] = '\0';

            // Check if request matches /api
            if (strstr(rx_buffer, "GET /api") != NULL) {
                printf("[API Server] GET /api request received\n");
                char *json_response = handle_get_api();
                if (json_response) {
                    send_http_response(client_sock, 200, "OK", "application/json", json_response);
                    free(json_response);
                } else {
                    send_http_response(client_sock, 500, "Internal Server Error", "text/plain", "JSON Gen Failed");
                }
            } 
            else if (strstr(rx_buffer, "POST /api") != NULL) {
                // Isolate the HTTP body after double newline (\r\n\r\n)
                char *body = strstr(rx_buffer, "\r\n\r\n");
                if (body) {
                    body += 4; // Skip past the \r\n\r\n
                    handle_post_api(body);
                }
                send_http_response(client_sock, 200, "OK", "application/json", "{\"status\":\"success\"}");
            } 
            else {
                send_http_response(client_sock, 404, "Not Found", "text/plain", "Endpoint Not Found");
            }
        }

        close(client_sock);
    }
}

void start_api_server(void) {
    xTaskCreate(http_server_task, "APIServer", 4096, NULL, 3, NULL);
}