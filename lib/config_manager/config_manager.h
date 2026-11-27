#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#define CONFIG_MAGIC 0x50494332 // "PIC2"

typedef struct {
    uint32_t magic;
    char wifi_ssid[32];
    char wifi_pass[64];
    char mqtt_ip[32];
    uint16_t mqtt_port;
} device_config_t;

extern device_config_t g_active_config;
extern volatile bool g_config_mode_active; 

void load_device_config(void);
bool save_device_config(const char *ssid, const char *pass, const char *mqtt_ip);
void run_serial_config_console(void);

#endif