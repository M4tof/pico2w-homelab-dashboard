#include "light_read.h"
#include <stdio.h>
#include "hardware/adc.h"

int currentLightLevel = 0;
SemaphoreHandle_t lightMutex = NULL;

void checkLocalLight() {
    long value = 0;
    for(int i = 0; i < 10; i++){
        if (xSemaphoreTake(adcMutex, pdMS_TO_TICKS(5)) == pdTRUE) {

            adc_select_input(PHOTO_RES_PIN - 26); // Adjust for ADC input mapping
            value += adc_read();
            xSemaphoreGive(adcMutex);
        
        } 
        else {
            printf("[~light reading~] Failed to acquire adcMutex, temporary failure\n");
        }
    }

    if (xSemaphoreTake(lightMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        currentLightLevel = (int)(value / 10);
        xSemaphoreGive(lightMutex);
    } else {
        printf("[~light reading~] Failed to acquire mutex, temporary failure\n");
    }
}