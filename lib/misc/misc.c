#include "misc.h"
#include <stdio.h>
#include <malloc.h>
#include "hardware/adc.h"
#include "pinout.h"

int map(int x, int in_min, int in_max, int out_min, int out_max){
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

uint32_t getTotalHeap(void) {
    extern char __StackLimit, __bss_end__;
    return &__StackLimit - &__bss_end__;
}

uint32_t getFreeHeap(void) {
    struct mallinfo m = mallinfo();
    return getTotalHeap() - m.uordblks;
}

float readInternalTemperature(void) {
    // 1. Ensure temperature sensor is enabled (on RP2350 / RP2040)
    adc_set_temp_sensor_enabled(true);
    uint16_t raw = 0;
    
    // 2. Select the internal temperature sensor channel (Channel 4 on RP2040, or dedicated internal channel on RP2350)
    if (xSemaphoreTake(adcMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        
        // 3. Read raw ADC value
        adc_select_input(4);
        raw = adc_read();
        
        xSemaphoreGive(adcMutex);
    } 
    else {
        printf("[~temperature reading~] Failed to acquire adcMutex, temporary failure\n");
        return -1.0f; // Indicate error
    }
    
    // 4. Convert raw reading to voltage (assuming 3.3V reference and 12-bit ADC -> 4095)
    float voltage = raw * (3.3f / (1 << 12));
    
    // 5. Convert voltage to temperature (Formula from datasheet: 2700mV at 27°C, -1.721mV per °C)
    float temperature = 27.0f - (voltage - 0.706f) / 0.001721f;
    
    return temperature;
}