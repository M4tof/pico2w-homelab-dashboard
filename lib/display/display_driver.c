#include "display_driver.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "pinout.h"
#include "task.h"

// Define shared variables here (once)
int displayNumbers[4] = {8, 8, 8, 8}; 
SemaphoreHandle_t displayMutex = NULL;

int local_hh = 0;
int local_mm = 0;

// Mapping of numbers 0-9 and blank to 7-segment display segments
static const bool NUM_TIME_DISPLAY_MAP[11][7] = { 
  {1, 1, 1, 1, 1, 1, 0}, {0, 1, 1, 0, 0, 0, 0}, {1, 1, 0, 1, 1, 0, 1},
  {1, 1, 1, 1, 0, 0, 1}, {0, 1, 1, 0, 0, 1, 1}, {1, 0, 1, 1, 0, 1, 1},
  {1, 0, 1, 1, 1, 1, 1}, {1, 1, 1, 0, 0, 0, 0}, {1, 1, 1, 1, 1, 1, 1},
  {1, 1, 1, 1, 0, 1, 1}, {0, 0, 0, 0, 0, 0, 0}
}; 

void updateDisplayNumbers(void) {
    displayNumbers[0] = local_hh / 10;
    displayNumbers[1] = local_hh % 10;
    displayNumbers[2] = local_mm / 10;
    displayNumbers[3] = local_mm % 10;
}

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

static void showDigit(int num, int pos) {
    for (int i = 0; i < 4; i++) {
        gpio_put(DIGITS[i], 0);
    }
    for (int s = 0; s < 7; s++) {
        bool segment_state = NUM_TIME_DISPLAY_MAP[num][s];
        gpio_put(SEGS[s], segment_state ? 0 : 1); 
    }
    gpio_put(SEG_DP, (pos == 1) ? 0 : 1); 
    gpio_put(DIGITS[pos], 1); 
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