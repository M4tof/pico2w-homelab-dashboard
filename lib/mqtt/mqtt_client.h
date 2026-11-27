#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

// Type definition for message received callback
typedef void (*mqtt_msg_callback_t)(const char *topic, const char *payload, uint16_t len);

// Initialize and start the background MQTT connection task (with callback support)
void start_mqtt_client(const char *broker_ip, uint16_t port, mqtt_msg_callback_t cb);

// Helper function to publish and subscribe
bool mqtt_publish_message(const char *topic, const char *payload);
bool mqtt_subscribe_topic(const char *topic);

// Check if currently connected to the broker
bool is_mqtt_connected(void);

#endif