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
static mqtt_msg_callback_t user_msg_cb = NULL;

// Buffer to accumulate incoming chunked payloads
static char inpub_payload_buf[512];
static u32_t inpub_tot_len = 0;
static u32_t inpub_cur_len = 0;
static char current_topic[128];

// 1. Triggered when the publish header arrives
static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len) {
    // Save topic name safely
    strncpy(current_topic, topic, sizeof(current_topic) - 1);
    current_topic[sizeof(current_topic) - 1] = '\0';
    
    inpub_tot_len = tot_len;
    inpub_cur_len = 0;
}

// 2. Triggered when payload data fragments arrive
static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags) {
    if (inpub_cur_len + len < sizeof(inpub_payload_buf)) {
        memcpy(inpub_payload_buf + inpub_cur_len, data, len);
        inpub_cur_len += len;
        inpub_payload_buf[inpub_cur_len] = '\0'; // Null-terminate string
    }

    // Check if this is the last data chunk packet
    if (flags & MQTT_DATA_FLAG_LAST) {
        if (user_msg_cb != NULL) {
            // Forward complete topic and payload to main.c callback
            user_msg_cb(current_topic, inpub_payload_buf, inpub_cur_len);
        }
    }
}

// 3. Hook callbacks upon successful connection
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status) {
    if (status == MQTT_CONNECT_ACCEPTED) {
        printf("[MQTT] Connected to broker successfully!\n");
        mqtt_connected_flag = true;
        
        // Register the incoming publish parsing callbacks
        mqtt_set_inpub_callback(client, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, arg);
    } else {
        printf("[MQTT] Disconnected or connection failed! Status: %d\n", status);
        mqtt_connected_flag = false;
    }
}

bool is_mqtt_connected(void) {
    return mqtt_connected_flag;
}

// Request callback for subscriptions/publishes
static void mqtt_request_cb(void *arg, err_t result) {
    if (result != ERR_OK) {
        printf("[MQTT] Operation request failed, error: %d\n", result);
    }
}

bool mqtt_subscribe_topic(const char *topic) {
    if (!mqtt_connected_flag || my_mqtt_client == NULL) return false;
    err_t err = mqtt_subscribe(my_mqtt_client, topic, 0, mqtt_request_cb, NULL);
    return (err == ERR_OK);
}

bool mqtt_publish_message(const char *topic, const char *payload) {
    if (!mqtt_connected_flag || my_mqtt_client == NULL) return false;
    err_t err = mqtt_publish(my_mqtt_client, topic, payload, strlen(payload), 0, 0, mqtt_request_cb, NULL);
    return (err == ERR_OK);
}

// Background thread manager
static void mqtt_thread(void *pvParameters) {
    struct mqtt_connect_client_info_t ci;
    memset(&ci, 0, sizeof(ci));
    ci.client_id = "Pico2W_Dashboard";
    ci.keep_alive = 60;

    my_mqtt_client = mqtt_client_new();
    if (my_mqtt_client == NULL) {
        vTaskDelete(NULL);
        return;
    }

    uint32_t retry_delay_ms = 5000; // Start with 5 seconds

    while (true) {
        if (!mqtt_connected_flag) {
            printf("[MQTT] Attempting connection to broker...\n");
            err_t err = mqtt_client_connect(my_mqtt_client, &server_ip, broker_port, mqtt_connection_cb, NULL, &ci);
            if (err != ERR_OK) {
                printf("[MQTT] Connect API error: %d\n", err);
            }
        } else {
            // Reset delay back to baseline once successfully connected
            retry_delay_ms = 5000;
        }

        // Wait using the backoff delay (caps out at 30 seconds so it doesn't wait forever)
        vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));

        if (!mqtt_connected_flag) {
            retry_delay_ms *= 2; // Double the wait time next time (5s -> 10s -> 20s -> 30s)
            if (retry_delay_ms > 30000) {
                retry_delay_ms = 30000;
            }
        }
    }
}

void start_mqtt_client(const char *broker_ip_str, uint16_t port, mqtt_msg_callback_t cb) {
    broker_port = port;
    user_msg_cb = cb;
    ipaddr_aton(broker_ip_str, &server_ip);
    xTaskCreate(mqtt_thread, "MQTTClient", 4096, NULL, 2, NULL);
}