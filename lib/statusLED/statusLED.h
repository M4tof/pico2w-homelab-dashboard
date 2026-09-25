#ifndef STATUS_LED_H
#define STATUS_LED_H

#include "FreeRTOS.h"
#include "semphr.h"
#include "hardware/pwm.h"
#include "pinout.h"
#include "misc.h"

#define COLOR_PRESET_NUM 8

extern const int ColorPresets[COLOR_PRESET_NUM][3];
extern int currentLEDState;
extern SemaphoreHandle_t ALARM_LED_MUTEX;

void setup_led_pins(void);
void applyColor(int r, int g, int b);
void setRGB(int colorPreset);

#endif