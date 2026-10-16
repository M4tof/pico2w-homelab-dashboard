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
#include "hardware/watchdog.h"
#include "semphr.h"
#include "secrets.h"
#include "cJSON.h"
#include "lcd.h"
#include "light_read.h"
#include "temp_sensor.h"
#include "mqtt_client.h"
#include "display_driver.h" 
#include "light_read.h"
#include "statusLED.h"
#include "api_server.h"
#include "network_watchdog.h"
#include "misc.h"

SemaphoreHandle_t adcMutex;

char currentCustomMsg[64] = "";
TickType_t lastMQTTMessageTime = 0;
SemaphoreHandle_t MQTT_MSG_MUTEX = NULL;
bool hasValidCustomMsg = false; // Tracks if a "c" key was ever successfully received

bool connectedOnce = false;

bool isButtonOverridingLED = false;

SemaphoreHandle_t tempMutex = NULL;
float currentRoomTempShared = 0.0f;

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
            strncpy(currentCustomMsgForDisplay, customMessage, sizeof(currentCustomMsgForDisplay) - 1);
            currentCustomMsg[sizeof(currentCustomMsg) - 1] = '\0';
            currentCustomMsgForDisplay[sizeof(currentCustomMsgForDisplay) - 1] = '\0';

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
    bool backlightState = true; 
    bool previousBacklightState = backlightState;

    while (true) {
        checkLocalLight();
        
        if (currentLightLevel < LIGHT_THRESHOLD - 20) { 
            backlightState = false;
        }
        else if (currentLightLevel > LIGHT_THRESHOLD + 40){ 
            backlightState = true;
        }

        // Atomically control the LCD backlight safely via mutex
        if (xSemaphoreTake(lcdMutex, portMAX_DELAY) == pdTRUE) {
            lcd_backlight(backlightState);
            xSemaphoreGive(lcdMutex);
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
    int flash_toggle = 0;
    while (true) {
        if (!isButtonOverridingLED) {

            if (currentHealthState == NET_HEALTH_OK) { // Normal operation
                setRGB(currentLEDState); // what the mqtt says
            } 
            else if (connectedOnce && currentHealthState == NET_HEALTH_NO_MQTT) { // MQTT connection lost but server reachable
                currentLEDState = 3; //warning blue
                setRGB(3); 
            } 
            else if (connectedOnce && currentHealthState == NET_HEALTH_SERVER_DOWN) { // Server unreachable but router reachable
                currentLEDState = 1; // critical red
                setRGB(1); 
            } 
            else if (currentHealthState == NET_HEALTH_ROUTER_DOWN) { // Router unreachable
                currentLEDState = 6;
                if (flash_toggle) setRGB(1);  // flashing red teal to show worst posible scenario
                else setRGB(6);             
                flash_toggle = !flash_toggle;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}

void vLCDDisplayTask(void *pvParameters) {
    char currentLcdL1[17] = "Dis Bash's Bootin";
    char currentLcdL2[17] = "Please Wait...";
    const char MsgDelimiter = '|'; 
    
    const int restartEveryWriteNr = 10;
    int writeNum = 0;

    while (true) {
        bool localDiagnostic = true;
        char customMsgCopy[64] = "";

        if (xSemaphoreTake(MQTT_MSG_MUTEX, pdMS_TO_TICKS(5)) == pdTRUE) {
            TickType_t currentTime = xTaskGetTickCount();
            
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
            int LightCpy = -1;
            int ledCpy = -1;

            if (xSemaphoreTake(lightMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                LightCpy = currentLightLevel;
                xSemaphoreGive(lightMutex);
            }
            if (xSemaphoreTake(ALARM_LED_MUTEX, pdMS_TO_TICKS(5)) == pdTRUE) {
                ledCpy = currentLEDState;
                xSemaphoreGive(ALARM_LED_MUTEX);
            }

            snprintf(currentLcdL1, sizeof(currentLcdL1), "Light:%-4d LED:%d", LightCpy, ledCpy);

            float temp = readInternalTemperature();
            uint32_t totalRAM = 520 * 1024;
            uint32_t freeRAM = getFreeHeap();
            int usedPercent = ((int)(totalRAM - freeRAM) * 100) / (int)totalRAM;

            snprintf(currentLcdL2, sizeof(currentLcdL2), "T:%.1fC RAM:%d%%", temp, usedPercent);
        } 
        else {
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
        
        // --- ABSOLUTELY ATOMIC LCD WRITE & RESET SECTION ---
        if (xSemaphoreTake(lcdMutex, portMAX_DELAY) == pdTRUE) {
            writeNum++;

            lcd_clear();
            lcd_set_cursor(0, 0); 
            lcd_string(currentLcdL1);
            lcd_set_cursor(1, 0); 
            lcd_string(currentLcdL2);

            xSemaphoreGive(lcdMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}

void vButtonTask(void *pvParameters) {
    printf("[Button] task started on GPIO %d\n", BUTTON_PIN);

    while (true) {
        if (gpio_get(BUTTON_PIN) == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (gpio_get(BUTTON_PIN) != 0) continue; 

            printf("[Button] Pressed down...\n");
            
            TickType_t pressStartTime = xTaskGetTickCount();
            uint32_t lastFlashedSecond = 0; // Tracks which second we last flashed on

            // Loop while holding the button
            while (gpio_get(BUTTON_PIN) == 0) {
                TickType_t holdDuration = xTaskGetTickCount() - pressStartTime;
                uint32_t secondsHeld = pdTICKS_TO_MS(holdDuration) / 1000;

                // Check if we crossed into a new second and haven't flashed for it yet
                if (secondsHeld > 0 && secondsHeld > lastFlashedSecond) {
                    lastFlashedSecond = secondsHeld;
                    printf("[Button] Held for %u second(s). Flashing LED...\n", secondsHeld);

                    // Tell vStatusLEDTask to back off while we flash
                    isButtonOverridingLED = true;

                    // Flash feedback: Turn off (preset 0) briefly, then restore
                    setRGB(0); // Turn off LED

                    vTaskDelay(pdMS_TO_TICKS(150)); // Short 150ms blink
                    
                    setRGB(currentLEDState); // Restore current state

                    // Hand control back to status task
                    isButtonOverridingLED = false;
                }

                vTaskDelay(pdMS_TO_TICKS(50)); 
            }

            // --- RELEASED ROUTER ---
            TickType_t totalHoldTime = xTaskGetTickCount() - pressStartTime;
            uint32_t finalSeconds = pdTICKS_TO_MS(totalHoldTime) / 1000;

            printf("[Button] Released after %u seconds held.\n", finalSeconds);

            if (finalSeconds == 0) {
                printf("[Button] Action: Quick Tap (< 1 second)\n");
            } 
            else {
                switch (finalSeconds) {
                    case 1:
                        printf("[Button] Action: 1-Second Hold\n");
                        break;

                    case 2:
                        printf("[Button] Action: 2-Second Hold. Soft Reseting LCD...\n");

                        setRGB(7); // flash magenta to indicate reset
                        lcd_soft_reset();

                        break;

                    case 3:
                        printf("[Button] Action: 3-Second Hold. Resetting...\n");
                        setRGB(7); // flash white(boot) to indicate reset
                        vTaskDelay(pdMS_TO_TICKS(100));
                        watchdog_reboot(0, 0, 0);
                        break;

                    default:
                        printf("[Button] Action: %u-Second Hold, No Special Action", finalSeconds);
                        break;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void vTempReadingTask(void *pvParameters) {
    temp_sensor_init(TEMP_SENSOR_PIN);
    printf("[Temp] Sensor task started on GPIO %u\n", TEMP_SENSOR_PIN);

    while (true) {
        // 1. Tell DS18B20 to convert temperature
        temp_sensor_start_conversion();

        // 2. Safely yield via FreeRTOS for 750ms conversion time
        vTaskDelay(pdMS_TO_TICKS(750));

        // 3. Read back scratchpad data
        float temp = 0.0f;
        if (temp_sensor_read_scratchpad(&temp)) {
            if (xSemaphoreTake(tempMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                currentRoomTempShared = temp;
                xSemaphoreGive(tempMutex);
            }
            printf("[Temp] Room Temperature: %.2f °C\n", temp);
        } else {
            printf("[Temp] Error reading DS18B20 sensor!\n");
        }

        // Wait another 4.25 seconds (Total 5-second polling loop)
        vTaskDelay(pdMS_TO_TICKS(4250));
    }
}

void vCPUWatchdog(void *pvParameters) {
    while (true) {
        watchdog_update(); // Feed the hardware dog every 3 seconds
        // printf("[CPUWatchdog] Fed hardware dog\n"); // Keep commented unless debugging
        vTaskDelay(pdMS_TO_TICKS(3000)); 
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    printf("[CRITICAL] Stack overflow detected in task: %s\n", pcTaskName);
    
    // Flash all LEDs red rapidly or force an immediate hardware reset
    // (Hardware watchdog will catch it if we force a spin, or we can reboot manually)
    watchdog_reboot(0, 0, 0); 
    while(1);
}

void vApplicationMallocFailedHook(void) {
    printf("[CRITICAL] FreeRTOS Malloc Failed! Out of RAM!\n");
    watchdog_reboot(0, 0, 0);
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
    lcdMutex = xSemaphoreCreateMutex();
    lcdLinesMutex = xSemaphoreCreateMutex();
    tempMutex = xSemaphoreCreateMutex();

    setup_display_pins();
    setup_led_pins();

    adc_init(); 
    adc_gpio_init(PHOTO_RES_PIN);

    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    lcd_init(i2c0, 0x27, 2, 16);
    lcd_set_cursor(0, 0);
    lcd_string("BOOTING...");

    printf("[Main] Initializing software components...\n");

    TaskHandle_t displayTaskHandle = NULL;
    TaskHandle_t ledTaskHandle = NULL;
    TaskHandle_t networkTaskHandle = NULL;
    TaskHandle_t lcdTaskHandle = NULL;
    TaskHandle_t mqttSubTaskHandle = NULL;
    TaskHandle_t lightSubTaskHandle = NULL;
    TaskHandle_t watchdogSubTaskHandle = NULL;
    TaskHandle_t buttonSubTaskHandle = NULL;
    TaskHandle_t tempSubTaskHandle = NULL;

    // ==========================================
    // CORE 1: REAL-TIME HARDWARE UI (Strictly Pinned)
    // ==========================================
    // These run continuously with high priority to ensure zero screen flicker.
    xTaskCreate(vTimeDisplayTask, "TimeDisplayTask", 512, NULL, 5, &displayTaskHandle);
    xTaskCreate(vStatusLEDTask, "StatusLEDTask", 256, NULL, 4, &ledTaskHandle);
    xTaskCreate(vLCDDisplayTask, "LCDTask", 2048, NULL, 3, &lcdTaskHandle);
    xTaskCreate(vButtonTask, "ButtonTask", 256, NULL, 4, &buttonSubTaskHandle);

    // ==========================================
    // CORE 0: NETWORK, MQTT & BACKGROUND SERVICES
    // ==========================================
    xTaskCreate(vNetworkTask, "NetTask", 1024, NULL, 4, &networkTaskHandle);
    xTaskCreate(vUpdateFromMQTTTask, "MQTTSubTask", 1024, NULL, 3, &mqttSubTaskHandle);
    xTaskCreate(vLightReadingTask, "LightReadTask", 256, NULL, 2, &lightSubTaskHandle);
    xTaskCreate(vTempReadingTask, "TempReadTask", 512, NULL, 2, &tempSubTaskHandle);
    
    // Proper background task for feeding the hardware watchdog
    xTaskCreate(vCPUWatchdog, "CPUWatchdog", 256, NULL, 1, &watchdogSubTaskHandle);

    // Start your network health monitor task
    start_network_watchdog();

#if configUSE_CORE_AFFINITY && configNUMBER_OF_CORES > 1
    // Pin Core 1 tasks (Display, LED, LCD, button)
    vTaskCoreAffinitySet(displayTaskHandle, 2); 
    vTaskCoreAffinitySet(ledTaskHandle, 2);     
    vTaskCoreAffinitySet(lcdTaskHandle, 2);
    vTaskCoreAffinitySet(buttonSubTaskHandle, 2);

    // Pin Core 0 tasks (MQTT loop, Light polling, Watchdog)
    vTaskCoreAffinitySet(networkTaskHandle,1);
    vTaskCoreAffinitySet(mqttSubTaskHandle, 1);
    vTaskCoreAffinitySet(lightSubTaskHandle, 1);
    vTaskCoreAffinitySet(watchdogSubTaskHandle, 1);
    vTaskCoreAffinitySet(tempSubTaskHandle, 1);
#endif

    watchdog_enable(8000, true);
    vTaskStartScheduler();
    printf("[Main] Started FreeRTOS Scheduler:\n");
    
    while (true);
}