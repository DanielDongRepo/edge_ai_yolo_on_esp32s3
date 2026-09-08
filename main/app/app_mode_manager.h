#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

// Define display modes
typedef enum {
    MODE_CAMERA_DISPLAY = 0,    // Default camera display mode
    MODE_AI_DETECTION = 1       // AI detection mode
} app_display_mode_t;

// Define detection states
typedef enum {
    DETECTION_IDLE = 0,         // Not detecting
    DETECTION_PROCESSING = 1,   // Detection in progress
    DETECTION_COMPLETED = 2     // Detection completed, showing results
} app_detection_state_t;

/**
 * @brief Initialize the mode manager
 * @param lcd_panel LCD panel handle for display operations
 * @return ESP_OK on success
 */
esp_err_t app_mode_manager_init(esp_lcd_panel_handle_t lcd_panel);

/**
 * @brief Get current display mode
 * @return Current display mode
 */
app_display_mode_t app_mode_manager_get_mode(void);

/**
 * @brief Get current detection state
 * @return Current detection state
 */
app_detection_state_t app_mode_manager_get_detection_state(void);

/**
 * @brief Register LCD IO callback for transfer synchronization
 * This function must be called after bsp_display_new() to enable SPI transfer mutual exclusion
 * @param lcd_io LCD panel IO handle for callback registration
 * @return ESP_OK on success
 */
esp_err_t app_mode_manager_register_lcd_callback(esp_lcd_panel_io_handle_t lcd_io);

/**
 * @brief Switch to camera display mode
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if detection is in progress
 */
esp_err_t app_mode_manager_switch_to_camera(void);

/**
 * @brief Trigger AI detection mode
 * @return ESP_OK on success
 */
esp_err_t app_mode_manager_trigger_detection(void);

/**
 * @brief Check if mode switching is allowed
 * @return true if switching is allowed, false otherwise
 */
bool app_mode_manager_can_switch_mode(void);

/**
 * @brief Force switch to camera mode (bypass normal checks)
 * Used for timeout recovery and emergency state reset
 * @return ESP_OK on success
 */
esp_err_t app_mode_manager_force_camera_mode(void);

/**
 * @brief Main task function for mode manager
 * This should be called from the main application loop
 */
void app_mode_manager_task(void);

#ifdef __cplusplus
}
#endif
