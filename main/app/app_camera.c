/*
 * SPDX-FileCopyrightText: 2026 DanielDongRepo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_jpeg_enc.h"
#include "esp_jpeg_common.h"
#include "esp_heap_caps.h"

#include "app_camera.h"

#include "app_ai_detect.h"
#include "app_mode_manager.h"
#include "app_http_stream.h"

#include "bsp/esp-bsp.h"
#include "sensor.h"

static const char *TAG = "app_camera";

static bool camera_running = false;
static bool camera_finished = false;
static bool udp_sending_paused = false;
static bool camera_reinitializing = false;

static QueueHandle_t frame_queue = NULL;
static TaskHandle_t udp_send_task_handle = NULL;
#define FRAME_QUEUE_SIZE 4

typedef struct {
    uint8_t *buf;
    size_t len;
} frame_data_t;

static camera_mode_t current_camera_mode = CAMERA_MODE_JPEG;

static esp_err_t app_camera_init_internal(camera_mode_t mode)
{
    ESP_LOGI(TAG, "Camera init start, mode=%d", mode);
    
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = CAMERA_PIN_D0;
    config.pin_d1 = CAMERA_PIN_D1;
    config.pin_d2 = CAMERA_PIN_D2;
    config.pin_d3 = CAMERA_PIN_D3;
    config.pin_d4 = CAMERA_PIN_D4;
    config.pin_d5 = CAMERA_PIN_D5;
    config.pin_d6 = CAMERA_PIN_D6;
    config.pin_d7 = CAMERA_PIN_D7;
    config.pin_xclk = CAMERA_PIN_XCLK;
    config.pin_pclk = CAMERA_PIN_PCLK;
    config.pin_vsync = CAMERA_PIN_VSYNC;
    config.pin_href = CAMERA_PIN_HREF;
    config.pin_sscb_sda = CAMERA_PIN_SIOD;
    config.pin_sscb_scl = CAMERA_PIN_SIOC;
    config.pin_pwdn = CAMERA_PIN_PWDN;
    config.pin_reset = CAMERA_PIN_RESET;
    config.xclk_freq_hz = 15000000;
    
    if (mode == CAMERA_MODE_JPEG) {
        config.pixel_format = PIXFORMAT_JPEG;
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 12;
        ESP_LOGI(TAG, "Camera mode: JPEG, QVGA");
    } else {
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 12;
        ESP_LOGI(TAG, "Camera mode: RGB565, QVGA");
    }
    
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.sccb_i2c_port = BSP_I2C_NUM;
    
    ESP_LOGI(TAG, "Config: xclk_pin=%d, xclk_freq=%d", config.pin_xclk, config.xclk_freq_hz); 
    ESP_LOGI(TAG, "Config: pwdn=%d, reset=%d", config.pin_pwdn, config.pin_reset); 
    

    esp_err_t err = esp_camera_init(&config); 
    if (err != ESP_OK) 
    { ESP_LOGE(TAG, "Camera init failed: 0x%x (%s)", err, esp_err_to_name(err)); return err; } 
    ESP_LOGI(TAG, "Camera init success"); 
    sensor_t *s = esp_camera_sensor_get();


    // s->set_vflip(s, 1); // flip it back
    // initial sensors are flipped vertically and colors are a bit saturated
    if (s->id.PID == OV3660_PID)
    {
        s->set_brightness(s, 1);  // up the blightness just a bit
        s->set_saturation(s, -2); // lower the saturation
    }
    s->set_sharpness(s, 2);
    s->set_awb_gain(s, 2);

    current_camera_mode = mode;
    return ESP_OK;
}

esp_err_t app_camera_init(void)
{
    return app_camera_init_internal(CAMERA_MODE_JPEG);
}

esp_err_t app_camera_reinit(camera_mode_t mode)
{
    if (current_camera_mode == mode) {
        ESP_LOGI(TAG, "Camera already in mode %d, skipping reinit", mode);
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Reinitializing camera from mode %d to %d", current_camera_mode, mode);
    
    camera_running = false;
    vTaskDelay(pdMS_TO_TICKS(50));
    
    esp_err_t err = esp_camera_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera deinit failed: 0x%x", err);
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    err = app_camera_init_internal(mode);
    
    camera_running = true;
    
    return err;
}

camera_mode_t app_camera_get_mode(void)
{
    return current_camera_mode;
}

static void app_camera_task(void *arg)
{
    int frame_count = 0;
    int null_count = 0;
    TickType_t last_stats_time = xTaskGetTickCount();

    ESP_LOGI(TAG, "Camera task started on core %d", xPortGetCoreID());

    while (true)
    {
        if (camera_running && app_mode_manager_get_mode() == MODE_CAMERA_DISPLAY) {
            camera_fb_t *frame = esp_camera_fb_get();
            
            if(frame != NULL) {
                frame_data_t fd = {
                    .buf = heap_caps_malloc(frame->len, MALLOC_CAP_SPIRAM),
                    .len = frame->len
                };
                
                if (fd.buf != NULL) {
                    memcpy(fd.buf, frame->buf, frame->len);
                    
                    if (xQueueSend(frame_queue, &fd, 0) != pdTRUE) {
                        heap_caps_free(fd.buf);
                    }
                }
                
                esp_camera_fb_return(frame);
                frame_count++;
                null_count = 0;
                
                if (frame_count % 10 == 0) {
                    ESP_LOGI(TAG, "Frame #%d: size=%d bytes", frame_count, frame->len);
                }
            } else {
                null_count++;
            }
        }

        TickType_t now = xTaskGetTickCount();
        if (now - last_stats_time >= pdMS_TO_TICKS(2000)) {
            uint32_t elapsed_ms = (now - last_stats_time) * portTICK_PERIOD_MS;
            float fps = (elapsed_ms > 0) ? (float)frame_count * 1000.0f / elapsed_ms : 0;
            
            ESP_LOGI(TAG, "===== Camera Stats (2s) =====");
            ESP_LOGI(TAG, "  Frames captured: %d (%.1f FPS)", frame_count, fps);
            ESP_LOGI(TAG, "  Consecutive nulls: %d", null_count);
            ESP_LOGI(TAG, "  Queue depth: %d", uxQueueMessagesWaiting(frame_queue));
            ESP_LOGI(TAG, "  Free PSRAM: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            ESP_LOGI(TAG, "=============================");
            
            frame_count = 0;
            last_stats_time = now;
        }
        
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    ESP_LOGD(TAG, "Stop");
    vTaskDelete(NULL);
}

static void app_udp_send_task(void *arg)
{
    int send_count = 0;
    int fail_count = 0;
    TickType_t last_stats_time = xTaskGetTickCount();

    ESP_LOGI(TAG, "UDP send task started on core %d", xPortGetCoreID());

    while (true) {
        if (udp_sending_paused) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        
        frame_data_t fd;
        if (xQueueReceive(frame_queue, &fd, pdMS_TO_TICKS(100)) == pdTRUE) {
            bool sent = app_http_stream_send_direct(fd.buf, fd.len);
            heap_caps_free(fd.buf);
            
            if (sent) {
                send_count++;
            } else {
                fail_count++;
            }
            
            if (send_count % 10 == 0) {
                ESP_LOGI(TAG, "UDP sent #%d: size=%d bytes", send_count, (int)fd.len);
            }
        }

        TickType_t now = xTaskGetTickCount();
        if (now - last_stats_time >= pdMS_TO_TICKS(2000)) {
            ESP_LOGI(TAG, "===== UDP Send Stats (2s) =====");
            ESP_LOGI(TAG, "  Frames sent: %d", send_count);
            ESP_LOGI(TAG, "  Send failures: %d", fail_count);
            ESP_LOGI(TAG, "  Paused: %d", udp_sending_paused);
            ESP_LOGI(TAG, "=============================");
            
            send_count = 0;
            fail_count = 0;
            last_stats_time = now;
        }
    }
    vTaskDelete(NULL);
}

esp_err_t app_camera_begin(void)
{
    frame_queue = xQueueCreate(FRAME_QUEUE_SIZE, sizeof(frame_data_t));
    if (frame_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create frame queue");
        return ESP_FAIL;
    }
    
    xTaskCreatePinnedToCore(app_camera_task, "app_camera_task", 16384, NULL, 15, NULL, 1);
    xTaskCreatePinnedToCore(app_udp_send_task, "udp_send_task", 8192, NULL, 14, &udp_send_task_handle, 0);
    return ESP_OK;
}

esp_err_t app_camera_start(void)
{
    camera_running = true;
    return ESP_OK;
}

esp_err_t app_camera_stop(void)
{
    camera_running = false;
    return ESP_OK;
}

bool app_camera_is_finished(void)
{
    return camera_finished;
}

void app_camera_pause_udp_sending(void)
{
    ESP_LOGI(TAG, "Pausing UDP sending for AI detection");
    udp_sending_paused = true;
    
    frame_data_t fd;
    while (xQueueReceive(frame_queue, &fd, 0) == pdTRUE) {
        heap_caps_free(fd.buf);
    }
    ESP_LOGI(TAG, "Frame queue cleared");
}

void app_camera_resume_udp_sending(void)
{
    ESP_LOGI(TAG, "Resuming UDP sending");
    
    esp_err_t err = app_camera_reinit(CAMERA_MODE_JPEG);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reinit camera to JPEG mode");
    }
    
    udp_sending_paused = false;
}