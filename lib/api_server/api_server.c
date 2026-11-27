#include "api_server.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "pico/stdlib.h"
#include "lwip/opt.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "cJSON.h"
#include "light_values.h"

// Mbed TLS includes (Notice: mbedtls/net_sockets.h is NOT needed)
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509.h"
#include "mbedtls/error.h"
#include "mbedtls/constant_time.h"

#define HTTPS_PORT 443
#define MAX_HTTP_REQ_LEN 2048

static const char *EXPECTED_API_TOKEN = "Bearer Pico2W-Secure-SuperSecretToken123!";

// Symbols linked at compile time from certs.S
extern const char srv_cert_pem[];
extern const char srv_key_pem[];

// Shared resources used by main.c
int LIGHT_THRESHOLD = LIGHT_THRESHOLD_VAL;
char currentCustomMsgForDisplay[64] = "";
static SemaphoreHandle_t s_data_mutex = NULL;

// External telemetry variables from other modules
extern int local_hh;
extern int local_mm;
extern int currentLightLevel;
extern int currentLEDState;
extern bool is_mqtt_connected(void);
extern uint32_t getFreeHeap(void);
extern float currentRoomTempShared;

// --- lwIP Sockets <-> Mbed TLS BIO Bridge Callbacks ---
static int lwip_ssl_send(void *ctx, const unsigned char *buf, size_t len) {
    int fd = (int)(intptr_t)ctx;
    int ret = send(fd, buf, len, 0);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        return -1;
    }
    return ret;
}

static int lwip_ssl_recv(void *ctx, unsigned char *buf, size_t len) {
    int fd = (int)(intptr_t)ctx;
    int ret = recv(fd, buf, len, 0);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
        return -1;
    }
    return ret; // 0 indicates connection closed (EOF)
}

// --- Thread-Safe Accessors ---
void set_light_threshold(int val) {
    if (s_data_mutex && xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        LIGHT_THRESHOLD = val;
        xSemaphoreGive(s_data_mutex);
    } else {
        LIGHT_THRESHOLD = val;
    }
}

int get_light_threshold(void) {
    int val = LIGHT_THRESHOLD;
    if (s_data_mutex && xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        val = LIGHT_THRESHOLD;
        xSemaphoreGive(s_data_mutex);
    }
    return val;
}

void set_custom_display_msg(const char *msg) {
    if (!msg) return;
    if (s_data_mutex && xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        strncpy(currentCustomMsgForDisplay, msg, sizeof(currentCustomMsgForDisplay) - 1);
        currentCustomMsgForDisplay[sizeof(currentCustomMsgForDisplay) - 1] = '\0';
        xSemaphoreGive(s_data_mutex);
    }
}

void get_custom_display_msg(char *out_buf, size_t max_len) {
    if (!out_buf || max_len == 0) return;
    if (s_data_mutex && xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        strncpy(out_buf, currentCustomMsgForDisplay, max_len - 1);
        out_buf[max_len - 1] = '\0';
        xSemaphoreGive(s_data_mutex);
    }
}

static void send_tls_http_response(mbedtls_ssl_context *ssl, int status_code, 
                                   const char *status_text, const char *content_type, 
                                   const char *body) {
    int body_len = body ? strlen(body) : 0;
    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Cache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Connection: close\r\n\r\n",
        status_code, status_text, content_type, body_len
    );

    mbedtls_ssl_write(ssl, (const unsigned char *)header, header_len);
    if (body_len > 0) {
        mbedtls_ssl_write(ssl, (const unsigned char *)body, body_len);
    }
}

static bool authenticate_request(const char *req_buf) {
    const char *auth_hdr = strstr(req_buf, "Authorization: ");
    if (!auth_hdr) return false;

    auth_hdr += 15;
    size_t expected_len = strlen(EXPECTED_API_TOKEN);

    if (mbedtls_ct_memcmp(auth_hdr, EXPECTED_API_TOKEN, expected_len) == 0) {
        if (auth_hdr[expected_len] == '\r' || auth_hdr[expected_len] == '\n' || auth_hdr[expected_len] == '\0') {
            return true;
        }
    }
    return false;
}

static char* handle_get_api(void) {
    cJSON *doc = cJSON_CreateObject();
    if (!doc) return NULL;

    char msg_copy[64];
    get_custom_display_msg(msg_copy, sizeof(msg_copy));

    cJSON_AddStringToObject(doc, "device", "Pico2W_Dashboard");
    cJSON_AddStringToObject(doc, "firmware", FIRMWARE_VERSION);
    cJSON_AddNumberToObject(doc, "uptime_s", to_ms_since_boot(get_absolute_time()) / 1000);

    char timeBuf[6];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", local_hh, local_mm);
    cJSON_AddStringToObject(doc, "local_time", timeBuf);

    cJSON_AddNumberToObject(doc, "light_level", currentLightLevel);
    cJSON_AddNumberToObject(doc, "room_temp_c", (double)currentRoomTempShared);
    cJSON_AddNumberToObject(doc, "free_ram_bytes", (double)getFreeHeap());
    cJSON_AddBoolToObject(doc, "mqtt_connected", is_mqtt_connected());
    cJSON_AddNumberToObject(doc, "led_status", currentLEDState);
    cJSON_AddNumberToObject(doc, "light_threshold", get_light_threshold());
    cJSON_AddStringToObject(doc, "custom_msg", msg_copy);

    char *json_string = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    return json_string;
}

static bool handle_post_api(const char *body) {
    if (!body) return false;

    cJSON *doc = cJSON_Parse(body);
    if (!doc) return false;

    bool state_modified = false;

    cJSON *thresh_item = cJSON_GetObjectItemCaseSensitive(doc, "set_light_threshold");
    if (cJSON_IsNumber(thresh_item)) {
        if (thresh_item->valueint >= 0 && thresh_item->valueint <= 4095) {
            set_light_threshold(thresh_item->valueint);
            state_modified = true;
        }
    }

    cJSON *msg_item = cJSON_GetObjectItemCaseSensitive(doc, "set_custom_msg");
    if (cJSON_IsString(msg_item) && (msg_item->valuestring != NULL)) {
        set_custom_display_msg(msg_item->valuestring);
        state_modified = true;
    }

    cJSON_Delete(doc);
    return state_modified;
}

static void https_server_task(void *pvParameters) {
    s_data_mutex = xSemaphoreCreateMutex();

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt srvcert;
    mbedtls_pk_context pkey;

    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&srvcert);
    mbedtls_pk_init(&pkey);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    const char *pers = "pico2w_https_server";
    if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                              (const unsigned char *)pers, strlen(pers)) != 0) {
        printf("[HTTPS] Failed to seed DRBG\n");
        vTaskDelete(NULL);
    }

    // Parse Certificate & Key
    size_t cert_len = strlen(srv_cert_pem) + 1;
    size_t key_len = strlen(srv_key_pem) + 1;

    if (mbedtls_x509_crt_parse(&srvcert, (const unsigned char *)srv_cert_pem, cert_len) != 0 ||
        mbedtls_pk_parse_key(&pkey, (const unsigned char *)srv_key_pem, key_len, NULL, 0, mbedtls_ctr_drbg_random, &ctr_drbg) != 0) {
        printf("[HTTPS] Failed to parse cert/key from flash\n");
        vTaskDelete(NULL);
    }

    if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_SERVER,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        printf("[HTTPS] Config defaults failed\n");
        vTaskDelete(NULL);
    }

    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
    mbedtls_ssl_conf_ca_chain(&conf, srvcert.next, NULL);
    mbedtls_ssl_conf_own_cert(&conf, &srvcert, &pkey);

    if (mbedtls_ssl_setup(&ssl, &conf) != 0) {
        printf("[HTTPS] SSL setup failed\n");
        vTaskDelete(NULL);
    }

    // --- Create lwIP BSD Server Socket ---
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        printf("[HTTPS] Failed to create socket\n");
        vTaskDelete(NULL);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(HTTPS_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        printf("[HTTPS] Bind failed on port %d\n", HTTPS_PORT);
        close(server_fd);
        vTaskDelete(NULL);
    }

    if (listen(server_fd, 5) < 0) {
        printf("[HTTPS] Listen failed\n");
        close(server_fd);
        vTaskDelete(NULL);
    }

    printf("[HTTPS] Hardened Server listening on port %d (TLS 1.2)\n", HTTPS_PORT);

    char rx_buffer[MAX_HTTP_REQ_LEN];

    while (true) {
        struct sockaddr_in client_address;
        int client_len = sizeof(client_address);

        int client_sock = accept(server_fd, (struct sockaddr *)&client_address, (socklen_t *)&client_len);
        if (client_sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Set 3-second socket timeout on incoming connections
        struct timeval timeout;
        timeout.tv_sec = 3;
        timeout.tv_usec = 0;
        setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));

        mbedtls_ssl_session_reset(&ssl);

        // Bind lwIP socket descriptor to Mbed TLS BIO callbacks
        mbedtls_ssl_set_bio(&ssl, (void *)(intptr_t)client_sock, lwip_ssl_send, lwip_ssl_recv, NULL);

        // Perform TLS Handshake
        int ret;
        while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                break;
            }
        }

        if (ret != 0) {
            close(client_sock);
            continue;
        }

        int total_read = 0;
        bool headers_complete = false;

        while (total_read < (MAX_HTTP_REQ_LEN - 1)) {
            ret = mbedtls_ssl_read(&ssl, (unsigned char *)rx_buffer + total_read, (MAX_HTTP_REQ_LEN - 1) - total_read);
            if (ret <= 0) break;
            total_read += ret;
            rx_buffer[total_read] = '\0';

            if (strstr(rx_buffer, "\r\n\r\n")) {
                headers_complete = true;
                break;
            }
        }

        if (!headers_complete) {
            mbedtls_ssl_close_notify(&ssl);
            close(client_sock);
            continue;
        }

        if (!authenticate_request(rx_buffer)) {
            send_tls_http_response(&ssl, 401, "Unauthorized", "application/json", "{\"error\":\"Unauthorized\"}");
            mbedtls_ssl_close_notify(&ssl);
            close(client_sock);
            continue;
        }

        if (strncmp(rx_buffer, "GET /api", 8) == 0) {
            char *json_response = handle_get_api();
            if (json_response) {
                send_tls_http_response(&ssl, 200, "OK", "application/json", json_response);
                cJSON_free(json_response);
            } else {
                send_tls_http_response(&ssl, 500, "Internal Server Error", "application/json", "{\"error\":\"cJSON failed\"}");
            }
        } 
        else if (strncmp(rx_buffer, "POST /api", 9) == 0) {
            char *body = strstr(rx_buffer, "\r\n\r\n");
            bool success = false;
            if (body) {
                body += 4;
                success = handle_post_api(body);
            }
            if (success) {
                send_tls_http_response(&ssl, 200, "OK", "application/json", "{\"status\":\"updated\"}");
            } else {
                send_tls_http_response(&ssl, 400, "Bad Request", "application/json", "{\"error\":\"Invalid payload\"}");
            }
        } 
        else {
            send_tls_http_response(&ssl, 404, "Not Found", "application/json", "{\"error\":\"Endpoint not found\"}");
        }

        mbedtls_ssl_close_notify(&ssl);
        close(client_sock);
    }
}

void start_api_server(void) {
    xTaskCreate(https_server_task, "HTTPS_Server", 8192, NULL, 3, NULL);
}