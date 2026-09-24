#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "lwip/apps/mqtt.h"
#include "lwip/ip_addr.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mqtt_client.h"

static mqtt_client_t *my_mqtt_client = NULL;
static ip_addr_t server_ip;
static uint16_t broker_port;
static bool mqtt_connected_flag = false;

bool is_mqtt_connected(void) {
    return mqtt_connected_flag;
}

// Called when connection status changes
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status) {
    if (status == MQTT_CONNECT_ACCEPTED) {
        printf("[MQTT] Connected to broker successfully!\n");
        mqtt_connected_flag = true;
    } else {
        printf("[MQTT] Disconnected or connection failed! Status: %d\n", status);
        mqtt_connected_flag = false;
    }
}

// Request callback for publishing
static void mqtt_pub_request_cb(void *arg, err_t result) {
    if (result != ERR_OK) {
        printf("[MQTT] Publish failed, error: %d\n", result);
    }
}

bool mqtt_publish_message(const char *topic, const char *payload) {
    if (!mqtt_connected_flag || my_mqtt_client == NULL) {
        return false;
    }

    err_t err = mqtt_publish(my_mqtt_client, topic, payload, strlen(payload), 0, 0, mqtt_pub_request_cb, NULL);
    if (err != ERR_OK) {
        printf("[MQTT] Failed to send publish request: %d\n", err);
        return false;
    }
    return true;
}

// Background Task handling connection / reconnection loop
static void mqtt_thread(void *pvParameters) {
    struct mqtt_connect_client_info_t ci;
    memset(&ci, 0, sizeof(ci));
    ci.client_id = "Pico2W_Dashboard";
    ci.keep_alive = 60;

    my_mqtt_client = mqtt_client_new();
    if (my_mqtt_client == NULL) {
        printf("[MQTT] Failed to create new MQTT client instance\n");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        if (!mqtt_connected_flag) {
            printf("[MQTT] Attempting connection to broker...\n");
            err_t err = mqtt_client_connect(my_mqtt_client, &server_ip, broker_port, mqtt_connection_cb, NULL, &ci);
            if (err != ERR_OK) {
                printf("[MQTT] Connect request error: %d\n", err);
            }
        }
        // Check connection state every 10 seconds
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void start_mqtt_client(const char *broker_ip_str, uint16_t port) {
    broker_port = port;
    ipaddr_aton(broker_ip_str, &server_ip);

    // Spawn MQTT manager task with a 4096-word stack
    xTaskCreate(mqtt_thread, "MQTTClient", 4096, NULL, 2, NULL);
}
