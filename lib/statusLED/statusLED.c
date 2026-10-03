#include "light_read.h"
#include <stdio.h>
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "statusLED.h"
#include "misc.h"

int currentLEDState = 5; // Default to White [FirstBoot]
SemaphoreHandle_t ALARM_LED_MUTEX = NULL;

const int ColorPresets[COLOR_PRESET_NUM][3] = {
    {0, 0, 0},        // 0: Clear [OFF]
    {255, 0, 0},      // 1: Red [CRITICAL]
    {0, 255, 0},      // 2: Green [OK]
    {0, 0, 255},      // 3: Blue [Warning]
    {255, 125, 0},     // 4: Yellow [Unknown||Hour]
    {255, 125, 80},    // 5: White [FirstBoot]
    {0, 255, 125},     // 6: Teal [Used as one half of the Router Down state, w/Red]
    {255, 0, 125},       // 7: Magenta Button Press registered
};

// Function to apply color to the RGB LED Physical pins using hardware PWM
void applyColor(int r, int g, int b){
    // Ensure the values are within the 0-255 range
    r = (r < 0) ? 0 : (r > 255) ? 255 : r;
    g = (g < 0) ? 0 : (g > 255) ? 255 : g;
    b = (b < 0) ? 0 : (b > 255) ? 255 : b;
    
    bool success = false;
    while(!success){
        if (xSemaphoreTake(ALARM_LED_MUTEX, pdMS_TO_TICKS(5)) == pdTRUE) {
            pwm_set_gpio_level(ALARM_PIN_R, r);
            pwm_set_gpio_level(ALARM_PIN_G, g);
            pwm_set_gpio_level(ALARM_PIN_B, b);

            xSemaphoreGive(ALARM_LED_MUTEX);
            success = true; 
        }
        else{
            printf("[StatusLED] Failed to acquire mutex for LED control\n");
        }
    }
}

// Function to set the RGB LED based on a color preset and adjust brightness based on ambient light
void setRGB(int colorPreset) {
    if (colorPreset < 0 || colorPreset >= COLOR_PRESET_NUM) {
        printf("[StatusLED] Invalid color preset: %d, defaulting to Yellow\n", colorPreset);
        colorPreset = 4; // Set to Yellow as default
    }

    int r = ColorPresets[colorPreset][0];
    int g = ColorPresets[colorPreset][1];
    int b = ColorPresets[colorPreset][2];
    
    int LightLevelCpy = 100;
    int brightnessPercent;

    if (xSemaphoreTake(lightMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        LightLevelCpy = currentLightLevel;
        xSemaphoreGive(lightMutex);
    }
    else {
        printf("[StatusLED] Failed to acquire lightMutex, using last known light level\n");
    }

    if(colorPreset > 3){
        // if non rgb preset then don't dim [mixes badly, and these are rare anyways, at night it's either a ok or warn.]
        applyColor(r, g, b);
        return;
    }

    int maxlevel = 100;
    int minlevel = 20;
    
    int maxLight = 500;
    int minLight = 50;

    if (LightLevelCpy >= maxLight) {
        brightnessPercent = maxlevel;
    } 
    else if (LightLevelCpy <= minLight) {
        brightnessPercent = minlevel;
    } 
    else {
        brightnessPercent = map(LightLevelCpy, minLight, maxLight, minlevel, maxlevel);
    }

    r = (r * brightnessPercent) / 100;
    g = (g * brightnessPercent) / 100;
    b = (b * brightnessPercent) / 100;

    applyColor(r, g, b);
}

void setup_led_pins(void) {
    uint pins[] = {ALARM_PIN_R, ALARM_PIN_G, ALARM_PIN_B};

    for (int i = 0; i < 3; i++) {
        // 1. Tell the GPIO pin that PWM is in charge of its output state
        gpio_set_function(pins[i], GPIO_FUNC_PWM);
        
        // 2. Find which PWM slice belongs to this pin
        uint slice_num = pwm_gpio_to_slice_num(pins[i]);
        
        // 3. Set the period wrap to 255 (mimics Arduino's 0-255 analogWrite scale)
        pwm_set_wrap(slice_num, 255);
        
        // 4. Enable the PWM slice
        pwm_set_enabled(slice_num, true);
    }

    // Initialize to default white color [FirstBoot]
    applyColor(ColorPresets[5][0], ColorPresets[5][1], ColorPresets[5][2]);

    printf("[StatusLED] RGB LED pins initialized with Hardware PWM\n");
}