#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "FreeRTOS.h"
#include "task.h"
#include "secrets.h"
#include "cJSON.h"
#include "lcd.h"
#include "pinout.h"
#include "mqtt_client.h" // <--- Include our MQTT module

void start_api_server(void);

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
                
                char lcd_buf[17];
                snprintf(lcd_buf, sizeof(lcd_buf), "IP:%s", ip_str);
                lcd_set_cursor(1, 0);
                lcd_string(lcd_buf);
            }
            
            // Start Services
            start_api_server();
            start_mqtt_client(MQTT_BROKER_IP, 1883); // <--- START MQTT CLIENT HERE
            break; 
        } else {
            printf("[NetTask] Connection failed. Retrying in 5s...\n");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    vTaskDelete(NULL);
}

// Example task showing how to publish JSON over MQTT continuously
void vHeartbeatTask(void *pvParameters) {
    while (true) {
        if (is_mqtt_connected()) {
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "online");
            char *json_str = cJSON_Print(root);
            
            if (json_str) {
                mqtt_publish_message("pico/status", json_str);
                free(json_str);
            }
            cJSON_Delete(root);
        }
        vTaskDelay(pdMS_TO_TICKS(10000)); // Every 10 seconds
    }
}

int main() {
    stdio_init_all();
    sleep_ms(2000); 
    
    lcd_init(i2c0, 0x27, 2, 16);
    lcd_set_cursor(0, 0);
    lcd_string("Dashboard Booting");

    xTaskCreate(vNetworkTask, "NetTask", 1024, NULL, 2, NULL);
    xTaskCreate(vHeartbeatTask, "HeartbeatTask", 1024, NULL, 1, NULL);

    vTaskStartScheduler();
    while (true);
}