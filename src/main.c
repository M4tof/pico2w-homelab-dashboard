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
#include "statusLED.h"
#include "misc.h"
 
int LIGHT_THRESHOLD = 150; 
SemaphoreHandle_t adcMutex;

// Handle incoming messages from MQTT
void handle_incoming_mqtt(const char *topic, const char *payload, uint16_t len) {
    printf("[MQTT] Received message on topic '%s': %s\n", topic, payload);
    // example {"7s": "1258", "a": 2, "c": "T:45.2C HDD:26%|Ram:45% SSD:60%"}


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

    cJSON *led_item = cJSON_GetObjectItemCaseSensitive(doc, "a");
    if (cJSON_IsNumber(led_item)) {
        int newLEDState = led_item->valueint;
        if (newLEDState >= 0 && newLEDState < COLOR_PRESET_NUM) {
            currentLEDState = newLEDState;
            printf("[LED] Updated LED state via MQTT: %d\n", currentLEDState);
        } else {
            printf("[LED] Invalid LED state received: %d\n", newLEDState);
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
            printf("[NetTask] Wi-Fi connection failed/lost, retrying...\n");
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    vTaskDelete(NULL);
}

void vLightReadingTask(void *pvParameters) {
    int Readings = 0;
    bool backlightState = true; // true = ON, false = OFF
    bool previousBacklightState = backlightState;

    while (true) {
        checkLocalLight();
        if (xSemaphoreTake(lightMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            if (currentLightLevel < LIGHT_THRESHOLD) {
                lcd_backlight(false);
                backlightState = false;
            }
            else {
                lcd_backlight(true);
                backlightState = true;
            }
            xSemaphoreGive(lightMutex);
        } else {
            printf("[Light] Failed to acquire lightMutex for reading\n");
        }

        if (backlightState != previousBacklightState) {
            printf("[Light] Backlight state changed: %s\n", backlightState ? "ON" : "OFF");
            previousBacklightState = backlightState;
        }

        Readings++;
        if (Readings % 100 == 0) {
            printf("[Light] Current light level: %d\n", currentLightLevel);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}

void vStatusLEDTask(void *pvParameters) {
    while (true) {
        setRGB(currentLEDState);
        vTaskDelay(pdMS_TO_TICKS(50)); 
    }
}

void vLCDDisplayTask(void *pvParameters) {
    char currentLcdL1[17] = "Dis Bash's Bootin";
    char currentLcdL2[17] = "Please Wait...";

    while (true) {
        bool localDiagnostic = true; // TODO: Expand later for custom MQTT messages

        if (localDiagnostic) {

            // 1. Get Light Level safely via Mutex
            int LightCpy = -1;
            if (xSemaphoreTake(lightMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                LightCpy = currentLightLevel;
                xSemaphoreGive(lightMutex);
            } else {
                printf("[LCD] Failed to acquire lightMutex for reading\n");
            }
            snprintf(currentLcdL1, sizeof(currentLcdL1), "Light: %-5d", LightCpy);

            // 2. Get Internal Temperature
            float temp = readInternalTemperature();

            // 3. Get Free RAM percentage (RP2350 has 520KB SRAM)
            uint32_t totalRAM = 520 * 1024;
            uint32_t freeRAM = getFreeHeap();
            int usedPercent = ((int)(totalRAM - freeRAM) * 100) / (int)totalRAM;
            
            snprintf(currentLcdL2, sizeof(currentLcdL2), "T:%.1fC RAM:%d%%", temp, usedPercent);
        } 
        else {
            // MQTT custom message slot for later
        }
        
        // Clear and print to LCD using our C driver functions
        lcd_clear();
        lcd_set_cursor(0, 0); 
        lcd_string(currentLcdL1);
        lcd_set_cursor(1, 0); 
        lcd_string(currentLcdL2);

        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}

int main() {
    stdio_init_all();
    sleep_ms(2000);

    printf("[Main] Initializing hardware pins...\n");
    displayMutex = xSemaphoreCreateMutex();
    lightMutex = xSemaphoreCreateMutex();
    ALARM_LED_MUTEX = xSemaphoreCreateMutex();
    adcMutex = xSemaphoreCreateMutex();

    setup_display_pins();
    setup_led_pins();

    adc_init(); 
    adc_gpio_init(PHOTO_RES_PIN);

    lcd_init(i2c0, 0x27, 2, 16);
    lcd_set_cursor(0, 0);
    lcd_string("Dis Bash is Bootin'");

    printf("[Main] Initializing software components...\n");

    TaskHandle_t displayTaskHandle = NULL;
    TaskHandle_t ledTaskHandle = NULL;
    TaskHandle_t networkTaskHandle = NULL;
    TaskHandle_t lcdTaskHandle = NULL;

 // --- CORE 0 TASKS: Networking, Subscriptions & Background Sensors ---
    // Priority 3: Network management gets a solid middle-high priority to maintain Wi-Fi/MQTT sync
    xTaskCreate(vNetworkTask, "NetTask", 1024, NULL, 3, &networkTaskHandle);
    
    // Priority 2: MQTT message loop
    xTaskCreate(vUpdateFromMQTTTask, "MQTTSubTask", 1024, NULL, 2, &networkTaskHandle);
    
    // Priority 1: Light reading can run quietly in the background
    xTaskCreate(vLightReadingTask, "LightReadTask", 512, NULL, 1, NULL);


    // --- CORE 1 TASKS: Real-Time Visual Feedback (Display & LEDs) ---
    // Priority 5 (Highest): 7-segment display multiplexer needs absolute smoothness
    xTaskCreate(vTimeDisplayTask, "TimeDisplayTask", 512, NULL, 5, &displayTaskHandle);
    
    // Priority 4: Status LED updates based on light and state
    xTaskCreate(vStatusLEDTask, "StatusLEDTask", 512, NULL, 4, &ledTaskHandle);

    // Priority 3: LCD display updates every second with diagnostic info
    xTaskCreate(vLCDDisplayTask, "LCDTask", 1024, NULL, 3, &lcdTaskHandle);

#if configUSE_CORE_AFFINITY && configNUMBER_OF_CORES > 1
    // Pin Visual/Real-Time tasks strictly to Core 1 (Mask value '2' represents binary 10 -> Core 1)
    vTaskCoreAffinitySet(displayTaskHandle, 2); 
    vTaskCoreAffinitySet(ledTaskHandle, 2);     
    vTaskCoreAffinitySet(lcdTaskHandle, 2);
    
    // Pin Network tasks strictly to Core 0 (Mask value '1' represents binary 01 -> Core 0)
    vTaskCoreAffinitySet(networkTaskHandle, 1);
#endif

    printf("[Main] Starting FreeRTOS Scheduler...\n");
    vTaskStartScheduler();
    
    while (true);
}