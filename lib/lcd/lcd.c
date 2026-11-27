#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lcd.h"


// Commands
#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME 0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_FUNCTIONSET 0x20
#define LCD_SETDDRAMADDR 0x80

// Flags for display entry mode
#define LCD_ENTRYLEFT 0x02
#define LCD_ENTRYSHIFTINCREMENT 0x01

// Flags for display and cursor control
#define LCD_DISPLAYON 0x04
#define LCD_CURSOROFF 0x00
#define LCD_BLINKOFF 0x00

// Flag for backlight control
#define LCD_BACKLIGHT 0x08
#define LCD_ENABLE_BIT 0x04

#define LCD_COMMAND 0
#define LCD_CHARACTER 1

static i2c_inst_t *lcd_i2c;
static uint8_t _addr;
static int _rows;
static int _cols;
static uint8_t backlight_val = LCD_BACKLIGHT;

SemaphoreHandle_t lcdMutex = NULL;
SemaphoreHandle_t lcdLinesMutex = NULL;

static void i2c_write_byte(uint8_t val) {
    i2c_write_blocking(lcd_i2c, _addr, &val, 1, false);
}

static void lcd_toggle_enable(uint8_t val) {

    taskENTER_CRITICAL();

    i2c_write_byte(val | LCD_ENABLE_BIT | backlight_val);
    sleep_us(1);
    i2c_write_byte((val & ~LCD_ENABLE_BIT) | backlight_val);

    taskEXIT_CRITICAL();

    sleep_us(50);
}

static void lcd_send_byte(uint8_t val, int mode) {
    uint8_t high = mode | (val & 0xF0) | backlight_val;
    uint8_t low = mode | ((val << 4) & 0xF0) | backlight_val;

    taskENTER_CRITICAL();
    i2c_write_byte(high);
    taskEXIT_CRITICAL();

    lcd_toggle_enable(high);
    
    taskENTER_CRITICAL();
    i2c_write_byte(low);
    taskEXIT_CRITICAL();
    
    lcd_toggle_enable(low);
}

void lcd_clear(void) {
    lcd_send_byte(LCD_CLEARDISPLAY, LCD_COMMAND);
    sleep_ms(2);
}

void lcd_set_cursor(int line, int position) {
    int val = (line == 0) ? 0x80 + position : 0xC0 + position;
    lcd_send_byte(val, LCD_COMMAND);
}

void lcd_string(const char *s) {
    while (*s) {
        lcd_send_byte(*(s++), LCD_CHARACTER);
    }
}

void lcd_backlight(bool activate) {
    backlight_val = activate ? LCD_BACKLIGHT : 0x00;

    taskENTER_CRITICAL();
    i2c_write_byte(backlight_val);
    taskEXIT_CRITICAL();
}

void lcd_soft_reset(void) {
    if (xSemaphoreTake(lcdMutex, portMAX_DELAY) == pdTRUE) {
        // Send a sequence of function set commands to force the 4-bit / 8-bit state machine to reset
        lcd_send_byte(0x03, LCD_COMMAND);
        sleep_ms(5);
        lcd_send_byte(0x02, LCD_COMMAND); // Return to 4-bit mode
        lcd_send_byte(0x28, LCD_COMMAND); // Function set: 2 line, 5x8 dots
        lcd_send_byte(0x0C, LCD_COMMAND); // Display ON, Cursor OFF
        lcd_clear();
        xSemaphoreGive(lcdMutex);
    }
}

void lcd_init(i2c_inst_t *i2c_inst, uint8_t i2c_addr, int rows, int cols) {
    lcd_i2c = i2c_inst;
    _addr = i2c_addr;
    _rows = rows;
    _cols = cols;

    // 1. Initialise the I2C block (e.g., i2c0 or i2c1)
    i2c_init(lcd_i2c, 100 * 1000);
    
    // 2. CHANGE THESE NUMBERS TO MATCH YOUR PHYSICAL PINS:
    uint sda_pin = 4; // Change to your GPIO pin for SDA
    uint scl_pin = 5; // Change to your GPIO pin for SCL

    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);

    // Initialisation sequence for HD44780 over PCF8574
    sleep_ms(50);
    lcd_send_byte(0x03, LCD_COMMAND);
    sleep_ms(5);
    lcd_send_byte(0x03, LCD_COMMAND);
    sleep_ms(1);
    lcd_send_byte(0x03, LCD_COMMAND);
    lcd_send_byte(0x02, LCD_COMMAND); // 4-bit mode

    lcd_send_byte(LCD_FUNCTIONSET | 0x08, LCD_COMMAND); // 2 lines, 5x8 dots
    lcd_send_byte(LCD_DISPLAYCONTROL | LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF, LCD_COMMAND);
    lcd_clear();
    lcd_send_byte(LCD_ENTRYMODESET | LCD_ENTRYLEFT, LCD_COMMAND);
    lcd_backlight(true);
}
