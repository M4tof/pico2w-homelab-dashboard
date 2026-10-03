#ifndef LCD_H
#define LCD_H

#include <stdint.h>
#include "hardware/i2c.h"
#include "FreeRTOS.h"
#include "semphr.h"

extern SemaphoreHandle_t lcdMutex;
extern SemaphoreHandle_t lcdLinesMutex;

// Initialize the LCD (i2c instance, i2c address, rows, cols)
void lcd_init(i2c_inst_t *i2c_inst, uint8_t i2c_addr, int rows, int cols);

// Clear the display
void lcd_clear(void);

// Move cursor to a specific column (0-15) and row (0-1)
void lcd_set_cursor(int line, int position);

// Print a string at the current cursor position
void lcd_string(const char *s);

// Turn backlight on or off
void lcd_backlight(bool activate);

// Perform a soft reset of the LCD, reinitializing it without a full power cycle
void lcd_soft_reset(void);

#endif
