#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <stdint.h>
#include <stdbool.h>

#define TEMP_SENSOR_PIN 27

void temp_sensor_init(uint8_t gpio_pin);
void temp_sensor_start_conversion(void);
bool temp_sensor_read_scratchpad(float *temperature_out);

extern float currentRoomTemperature;

#endif