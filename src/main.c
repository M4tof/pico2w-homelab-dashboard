#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "FreeRTOS.h"
#include "task.h"
#include "secrets.h"

// Forward declaration of our API server starter
void start_api_server(void);

// Network Connection Task
void vNetworkTask(void *pvParameters) {
    printf("[NetTask] Initializing Wi-Fi hardware...\n");
    
    if (cyw43_arch_init()) {
        printf("[NetTask] Wi-Fi hardware init failed!\n");
        vTaskDelete(NULL);
    }

    cyw43_arch_enable_sta_mode();
    printf("[NetTask] Attempting connection to SSID: %s\n", WIFI_SSID);

    while (true) {
        int err = cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 15000);
        
        if (err == 0) {
            printf("[NetTask] Connected to Wi-Fi successfully!\n");
            if (netif_list != NULL) {
                char *ip_str = ip4addr_ntoa(netif_ip4_addr(netif_list));
                printf("[NetTask] Assigned IP Address: %s\n", ip_str ? ip_str : "0.0.0.0");
            }
            
            // START THE API SERVER ONCE CONNECTED
            start_api_server();
            break; 
        } else {
            printf("[NetTask] Connection failed. Retrying in 5s...\n");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    vTaskDelete(NULL);
}

int main() {
    stdio_init_all();
    sleep_ms(2000); 
    
    printf("Starting Firmware Version: %s\n", FIRMWARE_VERSION);

    // Launch Network Task
    xTaskCreate(vNetworkTask, "NetTask", 1024, NULL, 2, NULL);

    // Start FreeRTOS Scheduler
    vTaskStartScheduler();

    while (true) {
        tight_loop_contents();
    }
}