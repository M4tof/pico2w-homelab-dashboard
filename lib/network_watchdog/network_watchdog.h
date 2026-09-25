#ifndef NETWORK_WATCHDOG_H
#define NETWORK_WATCHDOG_H

#include <stdbool.h>

// Health states
typedef enum {
    NET_HEALTH_OK = 0,
    NET_HEALTH_NO_MQTT,         // No MQTT for >30s, server ping OK
    NET_HEALTH_SERVER_DOWN,     // MQTT missing + Server down (192.168.1.242), router OK
    NET_HEALTH_ROUTER_DOWN      // MQTT missing + Server down + Router down (192.168.1.1)
} NetHealthState_t;

extern NetHealthState_t currentHealthState;

void start_network_watchdog(void);
void notify_mqtt_received(void); // Call this every time a valid MQTT message drops in
void send_ntfy_alert(const char *reason); // Send alert to ntfy.sh (or similar) when critical issues arise

#endif