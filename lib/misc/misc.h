#ifndef MISC_H
#define MISC_H

#include <stdio.h>
#include <malloc.h>
#include "hardware/adc.h"
#include "pinout.h"

int map(int x, int in_min, int in_max, int out_min, int out_max);
uint32_t getTotalHeap(void);
uint32_t getFreeHeap(void);
float readInternalTemperature(void);

#endif