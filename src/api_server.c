#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "lwip/opt.h"
#include "lwip/sockets.h"     // <--- Crucial for socket structs & functions
#include "lwip/inet.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cJSON.h"

#define HTTP_PORT 80

static void http_server_task(void *pvParameters) {
    struct sockaddr_in address, client_address;
    int server_fd, client_sock;
    int client_len = sizeof(client_address);

    // 1. Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        printf("[API Server] Failed to create socket (Err: %d)\n", server_fd);
        vTaskDelete(NULL);
        return;
    }

    // Allow socket address reuse so quick reboots don't throw bind errors
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 2. Bind socket to port 80
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(HTTP_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        printf("[API Server] Bind failed on port %d\n", HTTP_PORT);
        close(server_fd);
        vTaskDelete(NULL);
        return;
    }

    // 3. Listen for connections
    if (listen(server_fd, 5) < 0) {
        printf("[API Server] Listen failed\n");
        close(server_fd);
        vTaskDelete(NULL);
        return;
    }

    printf("[API Server] Running and listening on port %d...\n", HTTP_PORT);

    char rx_buffer[1024];

    // 4. Server main loop
    while (true) {
        client_sock = accept(server_fd, (struct sockaddr *)&client_address, (socklen_t *)&client_len);
        if (client_sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue; 
        }

        // Read the incoming HTTP request
        int len = read(client_sock, rx_buffer, sizeof(rx_buffer) - 1);
        if (len > 0) {
            rx_buffer[len] = '\0';

            // Check if request is for "/Api"
            if (strstr(rx_buffer, "GET /Api") != NULL || strstr(rx_buffer, "POST /Api") != NULL) {
                
                // Build JSON response using cJSON
                cJSON *root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "msg", "Hello world");
                char *json_string = cJSON_Print(root);
                
                int json_len = strlen(json_string);

                // Construct HTTP/1.1 200 OK header
                char header[256];
                int header_len = snprintf(header, sizeof(header),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: %d\r\n"
                    "Connection: close\r\n\r\n",
                    json_len
                );

                // Send data
                write(client_sock, header, header_len);
                write(client_sock, json_string, json_len);

                // Cleanup
                cJSON_Delete(root);
                free(json_string);
                
                printf("[API Server] Handled request for /Api (200 OK sent)\n");
            } else {
                // Return 404 for anything else
                const char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                write(client_sock, not_found, strlen(not_found));
            }
        }

        close(client_sock);
    }
}

void start_api_server(void) {
    xTaskCreate(http_server_task, "APIServer", 4096, NULL, 2, NULL);
}