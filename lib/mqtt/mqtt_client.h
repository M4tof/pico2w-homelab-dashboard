#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdbool.h>

// Initialize and start the background MQTT connection task
void start_mqtt_client(const char *broker_ip, uint16_t port);

// Helper function to publish a topic message
bool mqtt_publish_message(const char *topic, const char *payload);

// Check if currently connected to the broker
bool is_mqtt_connected(void);

#endif
