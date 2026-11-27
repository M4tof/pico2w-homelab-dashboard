#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include "FreeRTOS.h"
#include "semphr.h"

// Expose shared state so other modules (like MQTT) can update time variables
extern int displayNumbers[4];
extern int local_hh;
extern int local_mm;
extern SemaphoreHandle_t displayMutex;

// Public functions
void setup_display_pins(void);
void updateDisplayNumbers(void);
void vTimeDisplayTask(void *pvParameters);

#endif