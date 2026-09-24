#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "hardware/gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "secrets.h"
#include "cJSON.h"
#include "pinout.h"
#include "mqtt_client.h"

// -------------- Global variables for 7-segment display --------------
int displayNumbers[4] = {0, 0, 0, 0}; 
static SemaphoreHandle_t displayMutex = NULL;

const bool NUM_TIME_DISPLAY_MAP[11][7] = { 
  {1, 1, 1, 1, 1, 1, 0}, {0, 1, 1, 0, 0, 0, 0}, {1, 1, 0, 1, 1, 0, 1},
  {1, 1, 1, 1, 0, 0, 1}, {0, 1, 1, 0, 0, 1, 1}, {1, 0, 1, 1, 0, 1, 1},
  {1, 0, 1, 1, 1, 1, 1}, {1, 1, 1, 0, 0, 0, 0}, {1, 1, 1, 1, 1, 1, 1},
  {1, 1, 1, 1, 0, 1, 1}, {0, 0, 0, 0, 0, 0, 0}
}; 

int local_hh = 0;
int local_mm = 0;

void updateDisplayNumbers(void) {
    displayNumbers[0] = local_hh / 10;
    displayNumbers[1] = local_hh % 10;
    displayNumbers[2] = local_mm / 10;
    displayNumbers[3] = local_mm % 10;
}

// Hardware initialization for 7-segment display pins
void setup_display_pins(void) {
    for (int i = 0; i < 7; i++) {
        gpio_init(SEGS[i]);
        gpio_set_dir(SEGS[i], GPIO_OUT);
    }
    for (int i = 0; i < 4; i++) {
        gpio_init(DIGITS[i]);
        gpio_set_dir(DIGITS[i], GPIO_OUT);
        gpio_put(DIGITS[i], 0);
    }
    gpio_init(SEG_DP);
    gpio_set_dir(SEG_DP, GPIO_OUT);
    gpio_put(SEG_DP, 1);
}

// Multiplex a single digit
void showDigit(int num, int pos) {
    // Turn off all digits first (prevent ghosting)
    for (int i = 0; i < 4; i++) {
        gpio_put(DIGITS[i], 0);
    }
    
    // Set segments for the specific number
    for (int s = 0; s < 7; s++) {
        bool segment_state = NUM_TIME_DISPLAY_MAP[num][s];
        gpio_put(SEGS[s], segment_state ? 0 : 1); 
    }
    
    // Handle Decimal Point (between HH and MM at pos 1)
    gpio_put(SEG_DP, (pos == 1) ? 0 : 1); 
    
    // Turn on the specific digit
    gpio_put(DIGITS[pos], 1); 
}

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

// High-speed multiplexing task pinned to Core 1
void vTimeDisplayTask(void *pvParameters) {
    int local_copy[4];
    
    while (true) {
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            for(int i = 0; i < 4; i++) {
                local_copy[i] = displayNumbers[i];
            }
            xSemaphoreGive(displayMutex);
        }

        for (int i = 0; i < 4; i++) {
            showDigit(local_copy[i], i);
            vTaskDelay(pdMS_TO_TICKS(2)); 
        }
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
            // Fixed argument count to match start_mqtt_client signature (broker_ip, port, callback)
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

int main() {
    stdio_init_all();
    sleep_ms(2000);

    printf("[Main] Initializing hardware pins...\n");
    displayMutex = xSemaphoreCreateMutex();
    setup_display_pins();

    printf("[Main] Initializing software components...\n");
    TaskHandle_t displayTaskHandle = NULL;
    
    xTaskCreate(vNetworkTask, "NetTask", 1024, NULL, 2, NULL);
    xTaskCreate(vUpdateFromMQTTTask, "MQTTSubTask", 1024, NULL, 1, NULL);
    xTaskCreate(vTimeDisplayTask, "TimeDisplayTask", 512, NULL, 3, &displayTaskHandle);

#if configUSE_CORE_AFFINITY && configNUMBER_OF_CORES > 1
    vTaskCoreAffinitySet(displayTaskHandle, 2); // Core 1 binary mask
#endif

    printf("[Main] Starting FreeRTOS Scheduler...\n");
    vTaskStartScheduler();
    
    while (true);
}