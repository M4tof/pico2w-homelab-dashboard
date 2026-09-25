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
#include "api_server.h"
#include "network_watchdog.h"
#include "misc.h"
 
int LIGHT_THRESHOLD = 100; // Default threshold for ambient light level
SemaphoreHandle_t adcMutex;

char currentCustomMsg[64] = "";
TickType_t lastMQTTMessageTime = 0;
SemaphoreHandle_t MQTT_MSG_MUTEX = NULL;
bool hasValidCustomMsg = false; // Tracks if a "c" key was ever successfully received

bool connectedOnce = false;

// Handle incoming messages from MQTT
void handle_incoming_mqtt(const char *topic, const char *payload, uint16_t len) {
    printf("[MQTT] Received message on topic '%s': %s\n", topic, payload);
    notify_mqtt_received();

    cJSON *doc = cJSON_ParseWithLength(payload, len);
    if (doc == NULL) {
        printf("[JSON] Parse failed\n");
        return;
    }

    // Process time ("7s")
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
        }
    }

    // Process LED state ("a")
    cJSON *led_item = cJSON_GetObjectItemCaseSensitive(doc, "a");
    if (cJSON_IsNumber(led_item)) {
        int newLEDState = led_item->valueint;
        if (newLEDState >= 0 && newLEDState < COLOR_PRESET_NUM) {
            currentLEDState = newLEDState;
            if (newLEDState == 1) {
                printf("[LED] CRITICAL STATE\n");
                
            } else {
                printf("[LED] Setting LED to preset %d\n", newLEDState);
            }
        }
    }

    // Process Custom Message ("c")
    if (xSemaphoreTake(MQTT_MSG_MUTEX, pdMS_TO_TICKS(100)) == pdTRUE) {
        cJSON *custom_item = cJSON_GetObjectItemCaseSensitive(doc, "c");
        if (cJSON_IsString(custom_item) && (custom_item->valuestring != NULL)) {
            // A valid "c" key was found!
            const char *customMessage = custom_item->valuestring;
            strncpy(currentCustomMsg, customMessage, sizeof(currentCustomMsg) - 1);
            currentCustomMsg[sizeof(currentCustomMsg) - 1] = '\0';
            
            lastMQTTMessageTime = xTaskGetTickCount();
            hasValidCustomMsg = true; // Show custom message on LCD
        } else {
            // No "c" key provided in this packet -> fallback to local diagnostics
            hasValidCustomMsg = false; 
        }
        xSemaphoreGive(MQTT_MSG_MUTEX);
    } else {
        printf("[MQTT] Failed to acquire MQTT_MSG_MUTEX for updating custom message\n");
        hasValidCustomMsg = false; 
    }

    cJSON_Delete(doc);
}

void vUpdateFromMQTTTask(void *pvParameters) {
    while (true) {
        if (is_mqtt_connected()) {
            mqtt_subscribe_topic(MQTT_TOPIC); 
        }
        vTaskDelay(pdMS_TO_TICKS(9000)); 
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
            if (!connectedOnce){
                connectedOnce = true;
            }
            start_mqtt_client(MQTT_BROKER_IP, 1883, handle_incoming_mqtt);

            char *ip_str = ip4addr_ntoa(netif_ip4_addr(netif_list));
            printf("[NetTask] Assigned IP Address: %s\n", ip_str ? ip_str : "0.0.0.0");
            start_api_server();
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
        
        vTaskDelay(pdMS_TO_TICKS(500)); 
    }
}

void vStatusLEDTask(void *pvParameters) {
    int flash_toggle = 0;
    while (true) {
        if (currentHealthState == NET_HEALTH_OK) {
            setRGB(currentLEDState); // Normal operation
        } 
        else if (connectedOnce && currentHealthState == NET_HEALTH_NO_MQTT) {
            currentLEDState = 3;
            setRGB(3); //Blue for warning
        } 
        else if (connectedOnce && currentHealthState == NET_HEALTH_SERVER_DOWN) {
            currentLEDState = 2;
            setRGB(2); // Red for critical
        } 
        else if (currentHealthState == NET_HEALTH_ROUTER_DOWN) {
            // Alternate Red and Teal
            currentLEDState = 6;
            if (flash_toggle) setRGB(2); // Red
            else setRGB(6);             // Teal
            flash_toggle = !flash_toggle;
        }

        vTaskDelay(pdMS_TO_TICKS(500)); 
    }
}

void vLCDDisplayTask(void *pvParameters) {
    char currentLcdL1[17] = "Dis Bash's Bootin";
    char currentLcdL2[17] = "Please Wait...";
    const char MsgDelimiter = '|'; 

    while (true) {
        bool localDiagnostic = true;
        char customMsgCopy[64] = "";

        // Check if we should show the custom MQTT message or fallback to diagnostics
        if (xSemaphoreTake(MQTT_MSG_MUTEX, pdMS_TO_TICKS(5)) == pdTRUE) {
            TickType_t currentTime = xTaskGetTickCount();
            
            // Show custom message if we have a valid one AND it's been less than 10 seconds
            if (hasValidCustomMsg && (currentTime - lastMQTTMessageTime) < pdMS_TO_TICKS(10 * 1000)) {
                localDiagnostic = false;
                strncpy(customMsgCopy, currentCustomMsg, sizeof(customMsgCopy) - 1);
                customMsgCopy[sizeof(customMsgCopy) - 1] = '\0';
            } else {
                localDiagnostic = true;
            }

            xSemaphoreGive(MQTT_MSG_MUTEX);
        } else {
            printf("[LCD] Failed to acquire MQTT_MSG_MUTEX for reading\n");
        }

        if (localDiagnostic) {
            // 1. Get Light Level safely via Mutex
            int LightCpy = -1;
            int ledCpy = -1;

            if (xSemaphoreTake(lightMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                LightCpy = currentLightLevel;
                xSemaphoreGive(lightMutex);
            } else {
                printf("[LCD] Failed to acquire lightMutex for reading\n");
            }
            if (xSemaphoreTake(ALARM_LED_MUTEX, pdMS_TO_TICKS(5)) == pdTRUE) {
                ledCpy = currentLEDState;
                xSemaphoreGive(ALARM_LED_MUTEX);
            } else {
                printf("[LCD] Failed to acquire LED Mutex for reading\n");
            }

            snprintf(currentLcdL1, sizeof(currentLcdL1), "Light:%-4d LED:%d", LightCpy,ledCpy);

            // 2. Get Internal Temperature
            float temp = readInternalTemperature();

            // 3. Get Free RAM percentage (RP2350 has 520KB SRAM)
            uint32_t totalRAM = 520 * 1024;
            uint32_t freeRAM = getFreeHeap();
            int usedPercent = ((int)(totalRAM - freeRAM) * 100) / (int)totalRAM;
            
            snprintf(currentLcdL2, sizeof(currentLcdL2), "T:%.1fC RAM:%d%%", temp, usedPercent);
        } 
        else {
            // Split custom message into two lines if it contains the delimiter '|'
            char *delimiterPos = strchr(customMsgCopy, MsgDelimiter);
            if (delimiterPos != NULL) {
                size_t line1Length = delimiterPos - customMsgCopy;

                strncpy(currentLcdL1, customMsgCopy, line1Length);
                currentLcdL1[line1Length] = '\0'; 

                strncpy(currentLcdL2, delimiterPos + 1, sizeof(currentLcdL2) - 1);
                currentLcdL2[sizeof(currentLcdL2) - 1] = '\0'; 
            } else {
                strncpy(currentLcdL1, customMsgCopy, sizeof(currentLcdL1) - 1);
                currentLcdL1[sizeof(currentLcdL1) - 1] = '\0'; 
                snprintf(currentLcdL2, sizeof(currentLcdL2), "                "); 
            }
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
    MQTT_MSG_MUTEX = xSemaphoreCreateMutex();

    setup_display_pins();
    setup_led_pins();

    adc_init(); 
    adc_gpio_init(PHOTO_RES_PIN);

    lcd_init(i2c0, 0x27, 2, 16);
    lcd_set_cursor(0, 0);
    lcd_string("BOOTING...");

    printf("[Main] Initializing software components...\n");

    TaskHandle_t displayTaskHandle = NULL;
    TaskHandle_t ledTaskHandle = NULL;
    TaskHandle_t networkTaskHandle = NULL;
    TaskHandle_t lcdTaskHandle = NULL;
    TaskHandle_t mqttSubTaskHandle = NULL;

    // ==========================================
    // CORE 1: REAL-TIME HARDWARE UI (Strictly Pinned)
    // ==========================================
    // These run continuously with high priority to ensure zero screen flicker.
    xTaskCreate(vTimeDisplayTask, "TimeDisplayTask", 512, NULL, 5, &displayTaskHandle);
    xTaskCreate(vStatusLEDTask, "StatusLEDTask", 512, NULL, 4, &ledTaskHandle);
    xTaskCreate(vLCDDisplayTask, "LCDTask", 1024, NULL, 3, &lcdTaskHandle);

    // ==========================================
    // CORE 0: NETWORK, MQTT & BACKGROUND SERVICES
    // ==========================================
    // Network task handles connection setup, then exits (vTaskDelete).
    xTaskCreate(vNetworkTask, "NetTask", 1024, NULL, 4, &networkTaskHandle);
    
    // MQTT Sub loop runs quietly in the background, waking up every 10s to ensure subscription.
    xTaskCreate(vUpdateFromMQTTTask, "MQTTSubTask", 1024, NULL, 3, &mqttSubTaskHandle);
    
    // Sensor poll runs low priority so it never impacts UI or network throughput.
    xTaskCreate(vLightReadingTask, "LightReadTask", 512, NULL, 2, NULL);

    start_network_watchdog();

#if configUSE_CORE_AFFINITY && configNUMBER_OF_CORES > 1
    // Pin Core 1 tasks (Display, LED, LCD)
    vTaskCoreAffinitySet(displayTaskHandle, 2); 
    vTaskCoreAffinitySet(ledTaskHandle, 2);     
    vTaskCoreAffinitySet(lcdTaskHandle, 2);
    
    // Pin Core 0 tasks (MQTT loop, Light polling)
    vTaskCoreAffinitySet(mqttSubTaskHandle, 1);
#endif

    printf("[Main] Starting FreeRTOS Scheduler...\n");
    vTaskStartScheduler();
    
    while (true);
}