/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 DanielDongRepo
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "app_camera.h"
#include "app_ai_detect.h"
#include "app_mode_manager.h"
#include "app_http_stream.h"

#include "iot_button.h"
#include "esp_heap_caps.h"
#include "bsp/esp-bsp.h"

static char *TAG = "app_main";

#define LOG_MEMORY_SYSTEM_INFO         (0)
#define LOG_TASK_SYSTEM_INFO           (0)
#define LOG_TIME_INTERVAL_MS           (2000)
#define SYS_TASKS_ELAPSED_TIME_MS      (2000)

esp_err_t print_real_time_mem_stats(void);

// Function to handle automatic mode switching - REMOVED
// static void auto_mode_switch_task(void *arg)
/*
static void button_handler(touch_button_handle_t out_handle, touch_button_message_t *out_message, void *arg)
{
    
    (void) out_handle; // Unused
   
    int button = (int) arg;
    ESP_LOGI(TAG, ">>> Button Handler Triggered! Button Index: %d <<<", button); 
    // Only enable button 0 (channel 0 = TOUCH_PAD_NUM1, BOOT button) for mode switching
    if (button != 4) {
        ESP_LOGW(TAG, "Button[%d] is disabled - only (BOOT) is enabled for mode control", button);
        return;
    }
    
    if (out_message->event == TOUCH_BUTTON_EVT_ON_PRESS) {
        ESP_LOGI(TAG, "Button[%d] Press", button);
        
        // Button press: Toggle between camera and detection modes
        app_display_mode_t current_mode = app_mode_manager_get_mode();
        app_detection_state_t detection_state = app_mode_manager_get_detection_state();
        
        if (current_mode == MODE_CAMERA_DISPLAY) {
            // Switch from camera to detection mode
            if (app_mode_manager_can_switch_mode()) {
                ESP_LOGI(TAG, "Manual switch: Camera -> Detection");
                app_mode_manager_trigger_detection();
            } else {
                ESP_LOGW(TAG, "Cannot switch to detection - system busy");
            }
        } else if (current_mode == MODE_AI_DETECTION && detection_state != DETECTION_PROCESSING) {
            // Switch from detection back to camera mode (only if detection is not processing)
            ESP_LOGI(TAG, "Manual switch: Detection -> Camera");
            app_mode_manager_switch_to_camera();
        } else {
            ESP_LOGW(TAG, "Detection in progress - please wait for completion");
        }
        
    } else if (out_message->event == TOUCH_BUTTON_EVT_ON_RELEASE) {
        ESP_LOGI(TAG, "Button[%d] Release", button);
        
    } else if (out_message->event == TOUCH_BUTTON_EVT_ON_LONGPRESS) {
        ESP_LOGI(TAG, "Button[%d] LongPress", button);
        
        // Long press: Force return to camera mode (emergency reset)
        ESP_LOGI(TAG, "Long press detected - forcing return to camera mode");
        if (!app_mode_manager_can_switch_mode()) {
            ESP_LOGI(TAG, "Force switching to camera mode");
            app_mode_manager_force_camera_mode();
        } else {
            app_mode_manager_switch_to_camera();
        }
    }
}
*/

// 处理 BOOT 按键的核心逻辑
// is_long_press 为 true 表示长按，false 表示单击
static void handle_boot_button_action(bool is_long_press)
{
    if (is_long_press) {
        // --- 长按逻辑 ---
        ESP_LOGI(TAG, "Long press detected - forcing return to camera mode");
        if (!app_mode_manager_can_switch_mode()) {
            ESP_LOGI(TAG, "Force switching to camera mode");
            app_mode_manager_force_camera_mode();
        } else {
            app_mode_manager_switch_to_camera();
        }
    } else {
        // --- 单击逻辑 ---
        app_display_mode_t current_mode = app_mode_manager_get_mode();
        app_detection_state_t detection_state = app_mode_manager_get_detection_state();
        if (current_mode == MODE_CAMERA_DISPLAY) {
            if (app_mode_manager_can_switch_mode()) {
                ESP_LOGI(TAG, "Manual switch: Camera -> Detection");
                app_mode_manager_trigger_detection();
            } else {
                ESP_LOGW(TAG, "Cannot switch to detection - system busy");
            }
        } else if (current_mode == MODE_AI_DETECTION && detection_state != DETECTION_PROCESSING) {
            ESP_LOGI(TAG, "Manual switch: Detection -> Camera");
            app_mode_manager_switch_to_camera();
        } else {
            ESP_LOGW(TAG, "Detection in progress - please wait for completion");
        }
    }
}

// 单击回调
static void boot_button_single_click_cb(void *btn_handle, void *usr_data)
{
    (void) btn_handle;
    (void) usr_data;
    ESP_LOGI(TAG, ">>> BOOT Button Single Click <<<");
    handle_boot_button_action(false); // false 代表单击
}

// 长按回调
static void boot_button_long_press_cb(void *btn_handle, void *usr_data)
{
    (void) btn_handle;
    (void) usr_data;
    ESP_LOGI(TAG, ">>> BOOT Button Long Press <<<");
    handle_boot_button_action(true); // true 代表长按
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing I2C...");
    esp_err_t ret = bsp_i2c_init(); 
    if (ret != ESP_OK) 
    { ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(ret)); return; } 
    ESP_LOGI(TAG, "I2C init success"); 

    ESP_LOGI(TAG, "Initializing Camera first...");
    ret = app_camera_init(); 
    if (ret != ESP_OK) 
    { ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(ret)); return; } 
    ESP_LOGI(TAG, "Camera init success"); 

    ESP_LOGI(TAG, "Testing camera capture before WiFi...");
    camera_fb_t *test_frame = esp_camera_fb_get();
    if (test_frame != NULL) {
        ESP_LOGI(TAG, "Camera test passed! Got frame: %dx%d, size=%d", 
                 test_frame->width, test_frame->height, test_frame->len);
        esp_camera_fb_return(test_frame);
    } else {
        ESP_LOGE(TAG, "Camera test FAILED! Cannot capture frame even without WiFi");
        return;
    }
    ESP_LOGI(TAG, "Turning on LCD backlight");
    bsp_display_backlight_on();
    //ESP_LOGI(TAG, "Turning off LCD to free DMA resources...");
    //bsp_display_backlight_off();
    //ESP_LOGI(TAG, "LCD backlight turned off");
    /*
    ESP_LOGI(TAG, "Initializing HTTP stream (WiFi)...");
    ret = app_http_stream_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize HTTP stream: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "HTTP stream initialized successfully.");

    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    for (int i = 0; i < 30; i++) {
        if (app_http_stream_is_connected()) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
*/
    ESP_LOGI(TAG, "Re-initializing camera after WiFi connection...");
    esp_camera_deinit();
    ret = app_camera_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera re-init failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Camera re-init success");

    ESP_LOGI(TAG, "Initializing AI detection...");
    app_ai_detect_init();
    
    ESP_LOGI(TAG, "Initializing mode manager...");
    app_mode_manager_init();

    app_camera_begin(); 
    app_camera_start();

    /* Create touch button */
    //bsp_touch_button_create(button_handler);

    /* Create physical buttons (including BOOT button) */
    button_handle_t btn_array[BSP_BUTTON_NUM];
    int btn_cnt = 0;
    ret = bsp_iot_button_create(btn_array, &btn_cnt, BSP_BUTTON_NUM);
    // 检查是否创建成功，并且确认 BOOT 按键的索引是 4
    if (ret == ESP_OK && btn_cnt > 4) {
        // 为 BOOT 按键（索引为 4）注册单击和长按事件
        iot_button_register_cb(btn_array[4], BUTTON_SINGLE_CLICK, boot_button_single_click_cb, NULL);
        iot_button_register_cb(btn_array[4], BUTTON_LONG_PRESS_START, boot_button_long_press_cb, NULL);
        ESP_LOGI(TAG, "BOOT button callbacks registered successfully!");
    } else {
        ESP_LOGE(TAG, "Failed to register BOOT button callback!");
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create physical buttons");
    } else {
        ESP_LOGI(TAG, "Created %d physical buttons", btn_cnt);
    }


    ESP_LOGI(TAG, "System initialized. Manual button controls available:");
    ESP_LOGI(TAG, " - BOOT Button Short press: Toggle between camera and AI detection");
    ESP_LOGI(TAG, " - BOOT Button Long press: Force return to camera mode (emergency reset)");
    ESP_LOGI(TAG, " - Other buttons: Disabled for mode control");

    while (1) {
        // 主任务保持存活，可以做一些低优先级的后台工作，或者只是简单地延时
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }


#if LOG_MEMORY_SYSTEM_INFO
    static char buffer[2048];
    while (1) {
        sprintf(buffer, "\t  Biggest /     Free /    Total\n"
                " SRAM : [%8d / %8d / %8d]\n"
                "PSRAM : [%8d / %8d / %8d]\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
        printf("------------ Memory ------------\n");
        printf("%s\n", buffer);

        ESP_ERROR_CHECK(print_real_time_mem_stats());
        printf("\n");

        vTaskDelay(pdMS_TO_TICKS(LOG_TIME_INTERVAL_MS));
    }
#endif
}

#if LOG_TASK_SYSTEM_INFO
#define ARRAY_SIZE_OFFSET                   8   // Increase this if audio_sys_get_real_time_stats returns ESP_ERR_INVALID_SIZE

#define audio_malloc    malloc
#define audio_calloc    calloc
#define audio_free      free
#define AUDIO_MEM_CHECK(tag, x, action) if (x == NULL) { \
        ESP_LOGE(tag, "Memory exhausted (%s:%d)", __FILE__, __LINE__); \
        action; \
    }

const char *task_state[] = {
    "Running",
    "Ready",
    "Blocked",
    "Suspended",
    "Deleted"
};

/** @brief
 * "Extr": Allocated task stack from psram, "Intr": Allocated task stack from internel
 */
const char *task_stack[] = {"Extr", "Intr"};

esp_err_t print_real_time_mem_stats(void)
{
#if (CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID && CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
    TaskStatus_t *start_array = NULL, *end_array = NULL;
    UBaseType_t start_array_size, end_array_size;
    uint32_t start_run_time, end_run_time;
    uint32_t total_elapsed_time;
    uint32_t task_elapsed_time, percentage_time;
    esp_err_t ret;

    // Allocate array to store current task states
    start_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    start_array = (TaskStatus_t *)audio_malloc(sizeof(TaskStatus_t) * start_array_size);
    AUDIO_MEM_CHECK(TAG, start_array, {
        ret = ESP_FAIL;
        goto exit;
    });
    // Get current task states
    start_array_size = uxTaskGetSystemState(start_array, start_array_size, &start_run_time);
    if (start_array_size == 0) {
        ESP_LOGE(TAG, "Insufficient array size for uxTaskGetSystemState. Trying increasing ARRAY_SIZE_OFFSET");
        ret = ESP_FAIL;
        goto exit;
    }

    vTaskDelay(pdMS_TO_TICKS(SYS_TASKS_ELAPSED_TIME_MS));

    // Allocate array to store tasks states post delay
    end_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    end_array = (TaskStatus_t *)audio_malloc(sizeof(TaskStatus_t) * end_array_size);
    AUDIO_MEM_CHECK(TAG, start_array, {
        ret = ESP_FAIL;
        goto exit;
    });

    // Get post delay task states
    end_array_size = uxTaskGetSystemState(end_array, end_array_size, &end_run_time);
    if (end_array_size == 0) {
        ESP_LOGE(TAG, "Insufficient array size for uxTaskGetSystemState. Trying increasing ARRAY_SIZE_OFFSET");
        ret = ESP_FAIL;
        goto exit;
    }

    // Calculate total_elapsed_time in units of run time stats clock period.
    total_elapsed_time = (end_run_time - start_run_time);
    if (total_elapsed_time == 0) {
        ESP_LOGE(TAG, "Delay duration too short. Trying increasing SYS_TASKS_ELAPSED_TIME_MS");
        ret = ESP_FAIL;
        goto exit;
    }

    ESP_LOGI(TAG, "| Task              | Run Time    | Per | Prio | HWM       | State   | CoreId   | Stack ");

    // Match each task in start_array to those in the end_array
    for (int i = 0; i < start_array_size; i++) {
        for (int j = 0; j < end_array_size; j++) {
            if (start_array[i].xHandle == end_array[j].xHandle) {

                task_elapsed_time = end_array[j].ulRunTimeCounter - start_array[i].ulRunTimeCounter;
                percentage_time = (task_elapsed_time * 100UL) / (total_elapsed_time * portNUM_PROCESSORS);
                ESP_LOGI(TAG, "| %-17s | %-11d |%2d%%  | %-4u | %-9u | %-7s | %-8x | %s",
                                start_array[i].pcTaskName, (int)task_elapsed_time, (int)percentage_time, start_array[i].uxCurrentPriority,
                                (int)start_array[i].usStackHighWaterMark, task_state[(start_array[i].eCurrentState)],
                                start_array[i].xCoreID, task_stack[esp_ptr_internal(pxTaskGetStackStart(start_array[i].xHandle))]);

                // Mark that task have been matched by overwriting their handles
                start_array[i].xHandle = NULL;
                end_array[j].xHandle = NULL;
                break;
            }
        }
    }

    // Print unmatched tasks
    for (int i = 0; i < start_array_size; i++) {
        if (start_array[i].xHandle != NULL) {
            ESP_LOGI(TAG, "| %s | Deleted", start_array[i].pcTaskName);
        }
    }
    for (int i = 0; i < end_array_size; i++) {
        if (end_array[i].xHandle != NULL) {
            ESP_LOGI(TAG, "| %s | Created", end_array[i].pcTaskName);
        }
    }
    printf("\n");
    ret = ESP_OK;

exit:    // Common return path
    if (start_array) {
        audio_free(start_array);
        start_array = NULL;
    }
    if (end_array) {
        audio_free(end_array);
        end_array = NULL;
    }
    return ret;
#else
    ESP_LOGW(TAG, "Please enbale `CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID` and `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS` in menuconfig");
    return ESP_FAIL;
#endif
}
#endif
