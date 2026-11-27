#include "config_manager.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "secrets.h"

#define FLASH_CONFIG_OFFSET (4 * 1024 * 1024 - FLASH_SECTOR_SIZE)
#define FLASH_CONFIG_ADDR   ((const device_config_t *)(XIP_BASE + FLASH_CONFIG_OFFSET))

device_config_t g_active_config;
volatile bool g_config_mode_active = false;

void load_device_config(void) {
    if (FLASH_CONFIG_ADDR->magic == CONFIG_MAGIC) {
        printf("[Config] Loaded persistent config from Flash!\n");
        memcpy(&g_active_config, FLASH_CONFIG_ADDR, sizeof(device_config_t));
    } else {
        printf("[Config] No valid flash config found. Defaulting to secrets.h\n");
        g_active_config.magic = CONFIG_MAGIC;
        strncpy(g_active_config.wifi_ssid, WIFI_SSID, sizeof(g_active_config.wifi_ssid) - 1);
        strncpy(g_active_config.wifi_pass, WIFI_PASSWORD, sizeof(g_active_config.wifi_pass) - 1);
        strncpy(g_active_config.mqtt_ip, MQTT_BROKER_IP, sizeof(g_active_config.mqtt_ip) - 1);
        g_active_config.mqtt_port = 1883;
    }
}

bool save_device_config(const char *ssid, const char *pass, const char *mqtt_ip) {
    device_config_t temp_cfg;
    memset(&temp_cfg, 0, sizeof(temp_cfg));

    temp_cfg.magic = CONFIG_MAGIC;
    strncpy(temp_cfg.wifi_ssid, ssid, sizeof(temp_cfg.wifi_ssid) - 1);
    strncpy(temp_cfg.wifi_pass, pass, sizeof(temp_cfg.wifi_pass) - 1);
    strncpy(temp_cfg.mqtt_ip, mqtt_ip, sizeof(temp_cfg.mqtt_ip) - 1);
    temp_cfg.mqtt_port = 1883;

    uint8_t page_buf[FLASH_PAGE_SIZE];
    memset(page_buf, 0xFF, sizeof(page_buf));
    memcpy(page_buf, &temp_cfg, sizeof(temp_cfg));

    printf("\n[Config] Writing new parameters to Flash...\n");

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_CONFIG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_CONFIG_OFFSET, page_buf, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    printf("[Config] Flash save complete!\n");
    return true;
}

static void read_line(char *buf, size_t max_len) {
    size_t idx = 0;
    while (idx < max_len - 1) {
        // Continuously feed watchdog so you have unlimited time to type
        watchdog_update();

        // Non-blocking read with 50ms timeout
        int c = getchar_timeout_us(50000);
        if (c == PICO_ERROR_TIMEOUT || c == EOF) {
            continue;
        }

        if (c == '\r' || c == '\n') {
            putchar('\n');
            break;
        }
        if (c == 0x08 || c == 0x7F) { // Backspace
            if (idx > 0) {
                idx--;
                printf("\b \b");
            }
        } else if (c >= 32 && c <= 126) {
            buf[idx++] = (char)c;
            putchar(c);
        }
    }
    buf[idx] = '\0';
}

void run_serial_config_console(void) {
    g_config_mode_active = true;

    // Small delay to let any currently queued print statements flush out
    sleep_ms(200);

    // ANSI escape code: Clear terminal screen and move cursor to (0,0)
    printf("\033[2J\033[H");

    printf("========================================\n");
    printf("     PICO 2 W - PROVISIONING CONSOLE    \n");
    printf("========================================\n\n");

    char new_ssid[32] = "";
    char new_pass[64] = "";
    char new_ip[32]   = "";

    printf("Current SSID: %s\n", g_active_config.wifi_ssid);
    printf("Enter new SSID (or press ENTER to keep): ");
    read_line(new_ssid, sizeof(new_ssid));
    if (strlen(new_ssid) == 0) strncpy(new_ssid, g_active_config.wifi_ssid, sizeof(new_ssid) - 1);

    printf("Enter new Wi-Fi Password (or press ENTER to keep): ");
    read_line(new_pass, sizeof(new_pass));
    if (strlen(new_pass) == 0) strncpy(new_pass, g_active_config.wifi_pass, sizeof(new_pass) - 1);

    printf("Current MQTT Broker IP: %s\n", g_active_config.mqtt_ip);
    printf("Enter new MQTT Broker IP (or press ENTER to keep): ");
    read_line(new_ip, sizeof(new_ip));
    if (strlen(new_ip) == 0) strncpy(new_ip, g_active_config.mqtt_ip, sizeof(new_ip) - 1);

    printf("\nSaving configuration:\n");
    printf(" - SSID: %s\n", new_ssid);
    printf(" - MQTT: %s\n", new_ip);

    save_device_config(new_ssid, new_pass, new_ip);

    printf("\nRebooting device in 2 seconds...\n");
    sleep_ms(2000);
    watchdog_reboot(0, 0, 0);
}