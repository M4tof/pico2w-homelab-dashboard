#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "FreeRTOS.h"
#include "task.h"
#include "hardware/adc.h"
#include "semphr.h"
#include "secrets.h"
#include "cJSON.h"
#include "lcd.h"
#include "mqtt_client.h"
#include "display_driver.h" 
#include "light_read.h"

// Handle incoming messages from MQTT
void handle_incoming_mqtt(const char *topic, const char *payload, uint16_t len) {
    printf("[MQTT] Received message on topic '%s': %s\n", topic, payload);

    cJSON *doc = cJSON_ParseWithLength(payload, len);
    if (doc == NULL) {
        printf("[JSON] Parse failed\n");
        return;
    }

    cJSON *time_item = cJSON_GetObjectItemCaseSensitive(doc, "7s");
    if (cJSON_IsString(time_item) && (time_item->valuestring != NULL)) {
        const char *timeData = time_item->valuestring;
        
        if (strlen(timeData) == 4) {
            local_hh = (timeData[0] - '0') * 10 + (timeData[1] - '0');
            local_mm = (timeData[2] - '0') * 10 + (timeData[3] - '0');
            
            if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                updateDisplayNumbers();
                xSemaphoreGive(displayMutex);
            }
            
            printf("[Time] Updated time via MQTT: %02d:%02d -> Digits: [%d][%d]:[%d][%d]\n", 
                   local_hh, local_mm, displayNumbers[0], displayNumbers[1], displayNumbers[2], displayNumbers[3]);
        }
    }

    cJSON_Delete(doc);
}

void vUpdateFromMQTTTask(void *pvParameters) {
    while (true) {
        if (is_mqtt_connected()) {
            mqtt_subscribe_topic(MQTT_TOPIC); 
        }
        vTaskDelay(pdMS_TO_TICKS(10000)); 
    }
}

void vNetworkTask(void *pvParameters) {
    if (cyw43_arch_init()) {
        vTaskDelete(NULL);
    }
    cyw43_arch_enable_sta_mode();

    while (true) {
        if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 15000) == 0) {
            printf("[NetTask] Connected to Wi-Fi successfully!\n");
            start_mqtt_client(MQTT_BROKER_IP, 1883, handle_incoming_mqtt);
            break;
        }
        else {
            printf("[NetTask] Wi-Fi connection failed, retrying...\n");
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    vTaskDelete(NULL);
}

void vLightReadingTask(void *pvParameters) {
    while (true) {
        checkLocalLight();
        printf("[Light] Current light level: %d\n", currentLightLevel);
        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}

int main() {
    stdio_init_all();
    sleep_ms(2000);

    printf("[Main] Initializing hardware pins...\n");
    displayMutex = xSemaphoreCreateMutex();
    lightMutex = xSemaphoreCreateMutex();

    setup_display_pins();
    
    adc_init(); 
    adc_gpio_init(PHOTO_RES_PIN);
    adc_select_input(PHOTO_RES_PIN - 26); // Adjust for ADC input mapping

    lcd_init(i2c0, 0x27, 2, 16);
    lcd_set_cursor(0, 0);
    lcd_string("Dashboard Booting");

    printf("[Main] Initializing software components...\n");
    TaskHandle_t displayTaskHandle = NULL;
    
    xTaskCreate(vNetworkTask, "NetTask", 1024, NULL, 9, NULL);
    xTaskCreate(vUpdateFromMQTTTask, "MQTTSubTask", 1024, NULL, 1, NULL);
    
    xTaskCreate(vTimeDisplayTask, "TimeDisplayTask", 512, NULL, 10, &displayTaskHandle);
    xTaskCreate(vLightReadingTask, "LightReadTask", 512, NULL, 1, NULL);

#if configUSE_CORE_AFFINITY && configNUMBER_OF_CORES > 1
    vTaskCoreAffinitySet(displayTaskHandle, 2); // Core 1 binary mask
#endif

    printf("[Main] Starting FreeRTOS Scheduler...\n");
    vTaskStartScheduler();
    
    while (true);
}