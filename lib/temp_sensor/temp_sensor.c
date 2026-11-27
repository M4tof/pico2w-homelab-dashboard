#include "temp_sensor.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

float currentRoomTemperature = -127.0f;
static uint _gpio_pin = TEMP_SENSOR_PIN;

static void ow_reset(void) {
    gpio_set_dir(_gpio_pin, GPIO_OUT);
    gpio_put(_gpio_pin, 0);
    sleep_us(480);
    gpio_set_dir(_gpio_pin, GPIO_IN);
    sleep_us(480);
}

static void ow_write_bit(int bit) {
    gpio_set_dir(_gpio_pin, GPIO_OUT);
    gpio_put(_gpio_pin, 0);
    if (bit) {
        sleep_us(6);
        gpio_set_dir(_gpio_pin, GPIO_IN);
        sleep_us(64);
    } else {
        sleep_us(60);
        gpio_set_dir(_gpio_pin, GPIO_IN);
        sleep_us(10);
    }
}

static int ow_read_bit(void) {
    int bit = 0;
    gpio_set_dir(_gpio_pin, GPIO_OUT);
    gpio_put(_gpio_pin, 0);
    sleep_us(2);
    gpio_set_dir(_gpio_pin, GPIO_IN);
    sleep_us(9);
    bit = gpio_get(_gpio_pin) ? 1 : 0;
    sleep_us(55);
    return bit;
}

static void ow_write_byte(uint8_t data) {
    for (int i = 0; i < 8; i++) {
        ow_write_bit(data & 1);
        data >>= 1;
    }
}

static uint8_t ow_read_byte(void) {
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        data |= (ow_read_bit() << i);
    }
    return data;
}

void temp_sensor_init(uint8_t gpio_pin) {
    _gpio_pin = gpio_pin;
    gpio_init(_gpio_pin);
    gpio_set_dir(_gpio_pin, GPIO_IN);
}

void temp_sensor_start_conversion(void) {
    ow_reset();
    ow_write_byte(0xCC); // Skip ROM
    ow_write_byte(0x44); // Convert T
}

bool temp_sensor_read_scratchpad(float *temperature_out) {
    ow_reset();
    ow_write_byte(0xCC); // Skip ROM
    ow_write_byte(0xBE); // Read Scratchpad

    uint8_t LSB = ow_read_byte();
    uint8_t MSB = ow_read_byte();

    int16_t raw_temp = (MSB << 8) | LSB;
    if (raw_temp == 0xFF50 || raw_temp == 0) { 
        return false;
    }

    *temperature_out = (float)raw_temp * 0.0625f;
    currentRoomTemperature = *temperature_out;
    return true;
}