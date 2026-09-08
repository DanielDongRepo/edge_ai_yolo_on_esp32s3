#include "app_mode_manager.h"
#include "app_camera.h"
#include "app_ai_detect.h"
#include "esp_log.h"
#include "esp_painter.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "bsp/esp-bsp.h"
#include <string.h>

static const char *TAG = "app_mode_manager";

// Static variables
static app_display_mode_t current_mode = MODE_CAMERA_DISPLAY;
static app_detection_state_t detection_state = DETECTION_IDLE;
static esp_lcd_panel_handle_t mode_lcd_panel = NULL;
static esp_lcd_panel_io_handle_t mode_lcd_io = NULL;
static esp_painter_handle_t mode_painter = NULL;

// Task handle for detection
static TaskHandle_t detection_task_handle = NULL;

// LCD transfer synchronization - ensure mutual exclusion during SPI operations
static volatile bool lcd_transfer_in_progress = false;
static SemaphoreHandle_t lcd_transfer_semaphore = NULL;

// Forward declarations
static void detection_task(void *arg);
static esp_err_t show_detection_status(void);
static bool on_lcd_color_trans_done(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx);
static esp_err_t safe_lcd_draw_bitmap(esp_lcd_panel_handle_t panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);


esp_err_t app_mode_manager_init(esp_lcd_panel_handle_t lcd_panel)
{
    ESP_LOGI(TAG, "Initializing mode manager");

    mode_lcd_panel = lcd_panel;
    current_mode = MODE_CAMERA_DISPLAY;
    detection_state = DETECTION_IDLE;

    // Initialize LCD transfer synchronization
    lcd_transfer_semaphore = xSemaphoreCreateBinary();
    if (lcd_transfer_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create LCD transfer semaphore");
        return ESP_ERR_NO_MEM;
    }

    // Give semaphore initially (no transfer in progress)
    xSemaphoreGive(lcd_transfer_semaphore);
    lcd_transfer_in_progress = false;

    // Initialize esp_painter for status display
    esp_painter_config_t painter_config = {
        .canvas = {
            .width = BSP_LCD_H_RES,
            .height = BSP_LCD_V_RES
        },
        .color_format = ESP_PAINTER_COLOR_FORMAT_RGB565,
        .default_font = &esp_painter_basic_font_20,
        .swap_rgb565 = true
    };
    ESP_ERROR_CHECK(esp_painter_init(&painter_config, &mode_painter));

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

    // Check if switching is allowed
    if (!app_mode_manager_can_switch_mode()) {
        ESP_LOGW(TAG, "Cannot switch to camera mode - detection in progress");
        return ESP_ERR_INVALID_STATE;
    }

    // Stop AI detection and cleanup
    if (current_mode == MODE_AI_DETECTION) {
        detection_state = DETECTION_IDLE;

        // Give some time for detection task to see the state change
        vTaskDelay(pdMS_TO_TICKS(50));
        ESP_LOGI(TAG, "AI detection stopped, switching to camera mode");
    }

    current_mode = MODE_CAMERA_DISPLAY;
    ESP_LOGI(TAG, "Switched to camera display mode");

    return ESP_OK;
}

esp_err_t app_mode_manager_trigger_detection(void)
{
    ESP_LOGI(TAG, "Triggering AI detection mode");

    current_mode = MODE_AI_DETECTION;
    detection_state = DETECTION_PROCESSING;

    // Always create a new detection task (previous one should have exited)
    if (detection_task_handle != NULL) {
        ESP_LOGW(TAG, "Previous detection task still exists, waiting for cleanup");
        // Wait a bit for the previous task to exit
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // Create new detection task
    xTaskCreatePinnedToCore(detection_task, "detection_task", 8192, NULL, 5, &detection_task_handle, 1);
    ESP_LOGI(TAG, "AI detection mode triggered, new task created");

    return ESP_OK;
}

bool app_mode_manager_can_switch_mode(void)
{
    // Cannot switch when detection is in progress OR when LCD transfer is in progress
    if (detection_state == DETECTION_PROCESSING) {
        ESP_LOGD(TAG, "Cannot switch mode - detection in progress");
        return false;
    }

    if (lcd_transfer_in_progress) {
        ESP_LOGD(TAG, "Cannot switch mode - LCD transfer in progress");
        return false;
    }

    return true;
}
esp_err_t app_mode_manager_force_camera_mode(void) {
    ESP_LOGW(TAG, "Force switching to camera mode - bypassing normal checks");
    ESP_LOGW(TAG, "Current state before force reset - Mode: %d, Detection: %d, Task: %p", current_mode, detection_state, detection_task_handle);

    // 1. 首先，尝试优雅地通知任务退出
    detection_state = DETECTION_IDLE;
    current_mode = MODE_CAMERA_DISPLAY;

    if (detection_task_handle != NULL) {
        ESP_LOGW(TAG, "Detection task still running, signaling termination and waiting...");

        // 等待一小段时间，给任务一个优雅退出的机会
        vTaskDelay(pdMS_TO_TICKS(200));

        // 2. 检查任务是否已经退出
        if (detection_task_handle != NULL) {
            // 优雅退出失败，现在采取强制措施！
            ESP_LOGW(TAG, "Detection task failed to exit gracefully. Forcing deletion...");

            // 强制删除任务
            vTaskDelete(detection_task_handle);

            // 必须立即将句柄置空，防止后续操作使用无效句柄
            detection_task_handle = NULL;

            ESP_LOGW(TAG, "Detection task has been force deleted.");
        }
    }

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
    // This function manages the display based on current mode and state
    if (current_mode == MODE_CAMERA_DISPLAY) {
        // Camera display mode - handled by camera task
        return;
    } else if (current_mode == MODE_AI_DETECTION) {
        if (detection_state == DETECTION_PROCESSING) {
            // Show "Detecting..." status
            show_detection_status();
        }
        // If detection is completed, the detection task will handle the display
    }
}

static esp_err_t show_detection_status(void)
{
    // Create a black background
    uint16_t *black_buffer = (uint16_t *)heap_caps_malloc(BSP_LCD_H_RES * BSP_LCD_V_RES * 2, MALLOC_CAP_SPIRAM);
    if (black_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate buffer for status display");
        return ESP_ERR_NO_MEM;
    }

    // Fill with black color
    for (int i = 0; i < BSP_LCD_H_RES * BSP_LCD_V_RES; i++) {
        black_buffer[i] = 0x0000; // Black in RGB565
    }

    // Draw "Detecting..." text in the center
    const char *status_text = "Detecting...";
    int text_x = (BSP_LCD_H_RES - strlen(status_text) * 12) / 2; // Approximate text width calculation
    int text_y = BSP_LCD_V_RES / 2;

    esp_painter_draw_string(mode_painter, (uint8_t*)black_buffer,
                            BSP_LCD_H_RES * BSP_LCD_V_RES * 2,
                            text_x, text_y, NULL,
                            ESP_PAINTER_COLOR_WHITE,
                            status_text);

    // Display the buffer using safe LCD draw with transfer synchronization
    esp_err_t ret = safe_lcd_draw_bitmap(mode_lcd_panel, 0, 0, BSP_LCD_H_RES, BSP_LCD_V_RES, black_buffer);

    free(black_buffer);
    return ret;
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
            // Capture a frame for detection
            camera_fb_t *frame = esp_camera_fb_get();
            if (frame == NULL) {
                ESP_LOGE(TAG, "Camera capture failed during detection, retrying...");
                vTaskDelay(100 / portTICK_PERIOD_MS);
                continue;
            }

            ESP_LOGI(TAG, "Performing AI detection (loop: %d)", loop_count);

            // Allocate detection buffer for frame data copy
            size_t frame_size = frame->width * frame->height * 2; // RGB565 format (2 bytes per pixel)
            uint16_t *detection_buffer = (uint16_t *)heap_caps_malloc(frame_size, MALLOC_CAP_SPIRAM);
            if (detection_buffer == NULL) {
                ESP_LOGE(TAG, "Failed to allocate detection buffer, releasing frame");
                esp_camera_fb_return(frame);
                detection_state = DETECTION_IDLE;
                break;
            }

            // Copy frame data to detection buffer
            memcpy(detection_buffer, frame->buf, frame_size);
            int frame_width = frame->width;
            int frame_height = frame->height;

            // Release camera frame buffer immediately after copying
            esp_camera_fb_return(frame);
            ESP_LOGI(TAG, "Frame data copied to detection buffer, camera buffer released");

            // Perform AI detection using the copied buffer
            esp_err_t ret = app_coco_od_detect(detection_buffer, frame_width, frame_height);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "AI detection successful, displaying result...");

                // Display the detection result using safe LCD draw with transfer synchronization
                esp_err_t display_ret = safe_lcd_draw_bitmap(mode_lcd_panel, 0, 0, frame_width, frame_height, detection_buffer);
                if (display_ret == ESP_OK) {
                    detection_state = DETECTION_COMPLETED;
                    ESP_LOGI(TAG, "AI detection completed and result displayed successfully");
                } else {
                    ESP_LOGE(TAG, "Failed to display detection result (error: 0x%x), but marking as completed", display_ret);
                    detection_state = DETECTION_COMPLETED; // Still mark as completed even if display fails
                }
            } else {
                ESP_LOGE(TAG, "AI detection failed (error: 0x%x), resetting to idle state", ret);
                detection_state = DETECTION_IDLE;
            }

            // Free the detection buffer
            free(detection_buffer);
            ESP_LOGI(TAG, "Detection buffer freed");

            // Exit after processing one detection
            if (detection_state == DETECTION_COMPLETED) {
                ESP_LOGI(TAG, "Detection completed, task will continue monitoring");
            }

        } else {
            // Not in detection mode or not processing, wait
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        // Small delay to prevent task starvation
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    // Clean up task handle when task ends
    detection_task_handle = NULL;
    ESP_LOGI(TAG, "Detection task ended after %d loops", loop_count);
    vTaskDelete(NULL);
}

// LCD transfer completion callback - called when SPI transfer is done
static bool on_lcd_color_trans_done(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    (void)user_ctx;

    // Mark transfer as completed
    lcd_transfer_in_progress = false;

    // Give semaphore to signal transfer completion
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (lcd_transfer_semaphore != NULL) {
        xSemaphoreGiveFromISR(lcd_transfer_semaphore, &xHigherPriorityTaskWoken);
    }

    return (xHigherPriorityTaskWoken == pdTRUE);
}

// Function to register LCD IO callback for transfer synchronization
esp_err_t app_mode_manager_register_lcd_callback(esp_lcd_panel_io_handle_t lcd_io)
{
    if (lcd_io == NULL) {
        ESP_LOGE(TAG, "LCD IO handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    mode_lcd_io = lcd_io;

    // Register SPI transfer completion callback for mutual exclusion
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = on_lcd_color_trans_done,
    };

    esp_err_t ret = esp_lcd_panel_io_register_event_callbacks(lcd_io, &cbs, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register LCD IO callback: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "LCD transfer callback registered successfully");
    return ESP_OK;
}

// Safe LCD draw function with transfer synchronization
static esp_err_t safe_lcd_draw_bitmap(esp_lcd_panel_handle_t panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    if (panel == NULL || color_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Wait for any previous transfer to complete
    if (lcd_transfer_semaphore != NULL) {
        if (xSemaphoreTake(lcd_transfer_semaphore, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGW(TAG, "Timeout waiting for LCD transfer semaphore");
            return ESP_ERR_TIMEOUT;
        }
    }

    // Mark transfer as starting
    lcd_transfer_in_progress = true;

    // Perform the actual draw operation
    esp_err_t ret = esp_lcd_panel_draw_bitmap(panel, x_start, y_start, x_end, y_end, color_data);

    if (ret != ESP_OK) {
        // If draw failed, immediately mark as completed and give semaphore back
        lcd_transfer_in_progress = false;
        if (lcd_transfer_semaphore != NULL) {
            xSemaphoreGive(lcd_transfer_semaphore);
        }
        ESP_LOGE(TAG, "LCD draw bitmap failed: %s", esp_err_to_name(ret));
    }
    // Note: If draw succeeds, the callback will handle semaphore release

    return ret;
}
