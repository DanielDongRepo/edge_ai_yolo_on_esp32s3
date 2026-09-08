/*
 * SPDX-FileCopyrightText: 2026 DanielDongRepo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_mode_manager.h"
#include "app_camera.h"
#include "app_ai_detect.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#include "esp_jpeg_dec.h"
#include "esp_jpeg_enc.h"
#include "esp_jpeg_common.h"
#include "img_converters.h"

#include "app_http_stream.h"

static const char *TAG = "app_mode_manager";

// Static variables
static app_display_mode_t current_mode = MODE_CAMERA_DISPLAY;
static app_detection_state_t detection_state = DETECTION_IDLE;

// Task handle for detection
static TaskHandle_t detection_task_handle = NULL;

// Forward declarations
static void detection_task(void *arg);


esp_err_t app_mode_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing mode manager");
    
    current_mode = MODE_CAMERA_DISPLAY;
    detection_state = DETECTION_IDLE;
    
    ESP_LOGI(TAG, "Mode manager initialized successfully");
    return ESP_OK;
}

app_display_mode_t app_mode_manager_get_mode(void)
{
    return current_mode;
}

app_detection_state_t app_mode_manager_get_detection_state(void)
{
    return detection_state;
}

esp_err_t app_mode_manager_switch_to_camera(void)
{
    ESP_LOGI(TAG, "Request to switch to camera mode");
    
    if (!app_mode_manager_can_switch_mode()) {
        ESP_LOGW(TAG, "Cannot switch to camera mode - detection in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (current_mode == MODE_AI_DETECTION) {
        detection_state = DETECTION_IDLE;
        vTaskDelay(pdMS_TO_TICKS(50));
        ESP_LOGI(TAG, "AI detection stopped, switching to camera mode");
    }
    
    current_mode = MODE_CAMERA_DISPLAY;
    app_camera_resume_udp_sending();
    ESP_LOGI(TAG, "Switched to camera display mode");
    
    return ESP_OK;
}

esp_err_t app_mode_manager_trigger_detection(void)
{
    ESP_LOGI(TAG, "Triggering AI detection mode");
    
    app_camera_pause_udp_sending();
    
    current_mode = MODE_AI_DETECTION;
    detection_state = DETECTION_PROCESSING;
    
    if (detection_task_handle != NULL) {
        ESP_LOGW(TAG, "Previous detection task still exists, waiting for cleanup");
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    xTaskCreatePinnedToCore(detection_task, "detection_task", 16384, NULL, 5, &detection_task_handle, 0);
    ESP_LOGI(TAG, "AI detection mode triggered, new task created");
    
    return ESP_OK;
}

bool app_mode_manager_can_switch_mode(void)
{
    if (detection_state == DETECTION_PROCESSING) {
        ESP_LOGD(TAG, "Cannot switch mode - detection in progress");
        return false;
    }
    
    return true;
}
esp_err_t app_mode_manager_force_camera_mode(void) {
    ESP_LOGW(TAG, "Force switching to camera mode - bypassing normal checks");
    ESP_LOGW(TAG, "Current state before force reset - Mode: %d, Detection: %d, Task: %p", current_mode, detection_state, detection_task_handle);

    detection_state = DETECTION_IDLE;
    current_mode = MODE_CAMERA_DISPLAY;

    if (detection_task_handle != NULL) {
        ESP_LOGW(TAG, "Detection task still running, signaling termination and waiting...");
        
        for (int i = 0; i < 10; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
            if (detection_task_handle == NULL) {
                ESP_LOGI(TAG, "Detection task exited gracefully");
                break;
            }
        }

        if (detection_task_handle != NULL) {
            ESP_LOGW(TAG, "Detection task failed to exit gracefully. Forcing deletion...");
            vTaskDelete(detection_task_handle);
            detection_task_handle = NULL;
            ESP_LOGW(TAG, "Detection task has been force deleted.");
        }
    }

    app_camera_resume_udp_sending();
    
    ESP_LOGI(TAG, "Forced switch to camera display mode completed");
    return ESP_OK;
}
/*
esp_err_t app_mode_manager_force_camera_mode(void)
{
    ESP_LOGW(TAG, "Force switching to camera mode - bypassing normal checks");
    
    // Log current state for debugging
    ESP_LOGW(TAG, "Current state before force reset - Mode: %d, Detection: %d, Task: %p", 
             current_mode, detection_state, detection_task_handle);
    
    // Force reset detection state regardless of current state
    detection_state = DETECTION_IDLE;
    current_mode = MODE_CAMERA_DISPLAY;
    
    // Force cleanup detection task if it still exists
    if (detection_task_handle != NULL) {
        ESP_LOGW(TAG, "Detection task still running during force reset, signaling termination");
        // Task should see the DETECTION_IDLE state and exit
        vTaskDelay(pdMS_TO_TICKS(200));
        
        // Check if task has exited
        if (detection_task_handle != NULL) {
            ESP_LOGE(TAG, "Detection task failed to exit gracefully, task may be stuck");
            // Note: We don't forcefully delete the task as it may cause memory issues
            // The task should eventually exit when it sees the DETECTION_IDLE state
        }
    }
    
    ESP_LOGI(TAG, "Forced switch to camera display mode completed");
    return ESP_OK;
}
*/
void app_mode_manager_task(void)
{
    if (current_mode == MODE_CAMERA_DISPLAY) {
        return;
    } else if (current_mode == MODE_AI_DETECTION) {
        if (detection_state == DETECTION_PROCESSING) {
            
        }
    }
}

static void detection_task(void *arg)
{
    ESP_LOGI(TAG, "Detection task started");
    uint32_t loop_count = 0;
    
    while (true) {
        loop_count++;
        
        // Check for exit conditions first
        if (detection_state == DETECTION_IDLE) {
            ESP_LOGI(TAG, "Detection state is IDLE, exiting detection task");
            break;
        }
        
        // Add safety exit after too many loops (prevent infinite loops)
        if (loop_count > 1000) {
            ESP_LOGE(TAG, "Detection task ran too many loops, forcing exit");
            detection_state = DETECTION_IDLE;
            break;
        }
        
        if (current_mode == MODE_AI_DETECTION && detection_state == DETECTION_PROCESSING) {
            camera_fb_t *frame = esp_camera_fb_get();
            if (frame == NULL) {
                ESP_LOGE(TAG, "Camera capture failed during detection, retrying...");
                vTaskDelay(100 / portTICK_PERIOD_MS);
                continue;
            }
            
            ESP_LOGI(TAG, "Performing AI detection (loop: %d), frame size=%d bytes, format=%d", 
                     loop_count, frame->len, frame->format);
            
            ESP_LOGI(TAG, "Switching camera to RGB565 mode for AI detection");
            esp_err_t reinit_err = app_camera_reinit(CAMERA_MODE_RGB565);
            if (reinit_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to reinit camera to RGB565 mode");
                detection_state = DETECTION_IDLE;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            
            camera_fb_t *rgb_frame = esp_camera_fb_get();
            if (rgb_frame == NULL) {
                ESP_LOGE(TAG, "Failed to get RGB565 frame");
                detection_state = DETECTION_IDLE;
                app_camera_reinit(CAMERA_MODE_JPEG);
                break;
            }
            
            ESP_LOGI(TAG, "Got RGB565 frame: %dx%d, size=%d bytes", 
                     rgb_frame->width, rgb_frame->height, rgb_frame->len);
            
            size_t frame_size = rgb_frame->width * rgb_frame->height * 2;
            uint16_t *detection_buffer = (uint16_t *)heap_caps_malloc(frame_size, MALLOC_CAP_SPIRAM);
            if (detection_buffer == NULL) {
                ESP_LOGE(TAG, "Failed to allocate detection buffer, releasing frame");
                esp_camera_fb_return(rgb_frame);
                detection_state = DETECTION_IDLE;
                app_camera_reinit(CAMERA_MODE_JPEG);
                break;
            }
            
            memcpy(detection_buffer, rgb_frame->buf, frame_size);
            int frame_width = rgb_frame->width;
            int frame_height = rgb_frame->height;
            
            esp_camera_fb_return(rgb_frame);
            ESP_LOGI(TAG, "Frame data copied to detection buffer, camera buffer released");
            
            uint32_t detect_start = esp_timer_get_time();
            esp_err_t ret = app_coco_od_detect(detection_buffer, frame_width, frame_height);
            uint32_t detect_elapsed = (esp_timer_get_time() - detect_start) / 1000;
            ESP_LOGI(TAG, "Detection function returned after %lu ms, result: %s", 
                     detect_elapsed, esp_err_to_name(ret));

            if (detection_state == DETECTION_IDLE) {
                ESP_LOGW(TAG, "Detection cancelled during AI inference, exiting...");
                heap_caps_free(detection_buffer);
                app_camera_reinit(CAMERA_MODE_JPEG);
                break;
            }
            
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "AI detection successful, encoding RGB565 to JPEG...");
                
                uint8_t *jpeg_buf = NULL;
                size_t jpeg_len = 0;
                
                ESP_LOGI(TAG, "fmt2jpg: src=%p, size=%d, w=%d, h=%d", 
                         detection_buffer, (int)frame_size, frame_width, frame_height);
                
                bool jpeg_ok = fmt2jpg((uint8_t *)detection_buffer, frame_size, 
                                        frame_width, frame_height, PIXFORMAT_RGB565, 
                                        90, &jpeg_buf, &jpeg_len);
                
                if (jpeg_ok && jpeg_buf != NULL) {
                    ESP_LOGI(TAG, "JPEG encoded: %d bytes", (int)jpeg_len);
                    
                    ESP_LOGI(TAG, "Sending detection result for 5 seconds...");
                    uint32_t start_time = esp_timer_get_time();
                    uint32_t elapsed_ms = 0;
                    
                    while (elapsed_ms < 5000) {
                        if (detection_state == DETECTION_IDLE) {
                            ESP_LOGW(TAG, "Detection cancelled during result display, exiting...");
                            break;
                        }
                        app_http_stream_send_direct(jpeg_buf, jpeg_len);
                        vTaskDelay(pdMS_TO_TICKS(33));
                        elapsed_ms = (esp_timer_get_time() - start_time) / 1000;
                    }
                    
                    ESP_LOGI(TAG, "Detection result display completed");
                    free(jpeg_buf);
                } else {
                    ESP_LOGE(TAG, "Failed to encode RGB565 to JPEG, ret=%d, buf=%p, len=%d", 
                             jpeg_ok, jpeg_buf, (int)jpeg_len);
                }
                
                detection_state = DETECTION_COMPLETED;
            } else {
                ESP_LOGE(TAG, "AI detection failed (error: 0x%x), resetting to idle state", ret);
                detection_state = DETECTION_IDLE;
            }
            
            heap_caps_free(detection_buffer);
            ESP_LOGI(TAG, "Detection buffer freed");
            
            ESP_LOGI(TAG, "Switching camera back to JPEG mode");
            app_camera_reinit(CAMERA_MODE_JPEG);
            app_camera_resume_udp_sending();
            
            if (detection_state == DETECTION_COMPLETED) {
                ESP_LOGI(TAG, "Detection completed, switching back to camera display mode");
                current_mode = MODE_CAMERA_DISPLAY;
                detection_state = DETECTION_IDLE;
            }
            
        } else {
            // Not in detection mode or not processing, wait
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        
        // Small delay to prevent task starvation
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
    detection_task_handle = NULL;
    ESP_LOGI(TAG, "Detection task ended after %d loops", loop_count);
    vTaskDelete(NULL);
}