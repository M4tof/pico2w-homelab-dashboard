#ifndef LIGHT_READ_H
#define LIGHT_READ_H

#include "FreeRTOS.h"
#include "semphr.h"
#include "pinout.h"

extern int currentLightLevel;
extern SemaphoreHandle_t lightMutex;

void checkLocalLight();

#endif