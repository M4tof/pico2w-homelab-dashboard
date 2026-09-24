#include "light_read.h"
#include <stdio.h>
#include "hardware/adc.h"

int currentLightLevel = 0;
SemaphoreHandle_t lightMutex = NULL;

void checkLocalLight() {
    long value = 0;
    for(int i = 0; i < 10; i++){
        value += adc_read();
    }

    if (xSemaphoreTake(lightMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        currentLightLevel = (int)(value / 10);
        xSemaphoreGive(lightMutex);
    } else {
        printf("[~light reading~] Failed to acquire mutex, temporary failure\n");
    }
}