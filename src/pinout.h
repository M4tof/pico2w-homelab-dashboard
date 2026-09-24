#ifndef PINOUT_H
#define PINOUT_H

#include <stdint.h>

#define ALARM_PIN_R 7  // pin for red color of the alarm LED
#define ALARM_PIN_G 10 // pin for green color of the alarm LED
#define ALARM_PIN_B 2  // pin for blue color of the alarm LED

#define PHOTO_RES_PIN 26 // pin for photoresistor readings

// Proper C constant arrays
static const uint8_t DIGITS[4] = {16, 19, 20, 11}; 
static const uint8_t SEGS[7] = {17, 21, 13, 14, 15, 18, 12}; 
#define SEG_DP 9 // pin for the decimal point

#endif