/*
 * SPDX-FileCopyrightText: 2026 DanielDongRepo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_http_stream.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "sdkconfig.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "app_wifi_stream";

static int udp_socket = -1;
static struct sockaddr_in dest_addr;
static bool wifi_connected = false;

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
        
        udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_socket >= 0) {
            memset(&dest_addr, 0, sizeof(dest_addr));
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(CONFIG_SERVER_PORT);
            inet_pton(AF_INET, CONFIG_SERVER_IP, &dest_addr.sin_addr);
            ESP_LOGI(TAG, "UDP socket created, connecting to %s:%d", CONFIG_SERVER_IP, CONFIG_SERVER_PORT);
        } else {
            ESP_LOGE(TAG, "Failed to create UDP socket");
        }
    }
}

static esp_err_t wifi_init_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, CONFIG_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, CONFIG_WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGI(TAG, "Wi-Fi STA initialization done. Connecting to %s...", CONFIG_WIFI_SSID);

    return ESP_OK;
}

static bool send_udp_data(uint8_t* data, size_t size) {
    if (!wifi_connected || udp_socket < 0) {
        return false;
    }

    size_t sent_bytes = 0;
    const size_t chunk_size = 4096;

    while (sent_bytes < size) {
        size_t send_size = chunk_size;
        if (sent_bytes + send_size > size) {
            send_size = size - sent_bytes;
        }

        int sent = sendto(udp_socket, data + sent_bytes, send_size, 0,
                          (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        
        if (sent < 0) {
            return false;
        }
        sent_bytes += sent;
    }

    return true;
}

bool app_http_stream_send_direct(uint8_t *jpeg_data, size_t jpeg_size) {
    if (!wifi_connected || udp_socket < 0) {
        ESP_LOGW(TAG, "Send skipped: wifi_connected=%d, socket=%d", wifi_connected, udp_socket);
        return false;
    }
    
    bool result = send_udp_data(jpeg_data, jpeg_size);
    if (!result) {
        ESP_LOGE(TAG, "UDP send FAILED! size=%d bytes", (int)jpeg_size);
    }
    return result;
}

bool app_http_stream_is_connected(void) {
    return wifi_connected && udp_socket >= 0;
}

esp_err_t app_http_stream_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_sta();

    return ESP_OK;
}