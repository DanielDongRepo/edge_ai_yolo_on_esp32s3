/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "bsp/esp-bsp.h"

#include "app_camera.h"
#include "app_ai_detect.h"
#include "app_mode_manager.h"

#include "iot_button.h"
#include "esp_heap_caps.h"
static char *TAG = "app_main";

#define LOG_MEM_INFO    (1)

// Auto mode switch timing configuration - REMOVED, now manual only
// #define CAMERA_DISPLAY_TIME_MS  10000    // Show camera for 5 seconds
// #define DETECTION_WAIT_TIME_MS  10000   // Maximum wait time for detection completion

// Manual mode control variables - REMOVED, now always manual
// static bool manual_mode_enabled = false;   // Flag to enable/disable manual mode
// static bool auto_mode_task_running = true; // Flag to control auto mode task

#define LOG_MEMORY_SYSTEM_INFO         (0)
#define LOG_TASK_SYSTEM_INFO           (0)
#define LOG_TIME_INTERVAL_MS           (2000)
#define SYS_TASKS_ELAPSED_TIME_MS      (2000)   // Period of stats measurement

esp_lcd_panel_handle_t lcd_panel;
esp_lcd_panel_io_handle_t lcd_io;

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
        /* Initialize display and LVGL */
    ESP_LOGI(TAG, "Initializing display and LVGL...");
    const bsp_display_config_t bsp_disp_cfg = {
        .max_transfer_sz = BSP_LCD_H_RES * 10 * sizeof(uint16_t),
    };
    vTaskDelay(pdMS_TO_TICKS(200)); 
    // 1. 初始化屏幕面板并捕获返回值
    esp_err_t ret = bsp_display_new(&bsp_disp_cfg, &lcd_panel, &lcd_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display panel: %s", esp_err_to_name(ret));
        return; // 如果屏幕初始化失败，直接停止运行
    }
    ESP_LOGI(TAG, "Display panel initialized successfully.");

    // 2. 读取设备ID
    uint32_t lcd_id = 0;
    uint8_t lcd_id_data[4] = {0};

    ESP_LOGI(TAG, "Reading LCD ID...");

    // 分别读取 ST7789 的三个 ID 寄存器 (0xDA, 0xDB, 0xDC)
    uint8_t id1 = 0, id2 = 0, id3 = 0;
    esp_lcd_panel_io_rx_param(lcd_io, 0xDA, &id1, 1);
    esp_lcd_panel_io_rx_param(lcd_io, 0xDB, &id2, 1);
    esp_lcd_panel_io_rx_param(lcd_io, 0xDC, &id3, 1);

    // 将三个字节拼接成一个完整的 ID (例如: 0x858552)
    lcd_id = (id1 << 16) | (id2 << 8) | id3;

    // 打印最终结果
    ESP_LOGI(TAG, "LCD Panel ID: 0x%06"PRIx32" (Bytes: 0x%02X, 0x%02X, 0x%02X)", 
             lcd_id, id1, id2, id3);

    // 2. 开启屏幕显示并捕获返回值
    ret = esp_lcd_panel_disp_on_off(lcd_panel, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to turn on display panel: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Display panel turned on successfully.");
    }

    // 3. 开启背光
    bsp_display_backlight_on();
    ESP_LOGI(TAG, "Display backlight turned on.");
    
 /* 

    gpio_num_t pins[] = {
        GPIO_NUM_21,  // SCL
        GPIO_NUM_1,  // SDA 47旧 1新
        GPIO_NUM_45,  // CS
        GPIO_NUM_2,  // DC
        GPIO_NUM_46,  // BLK
    };

    for (int i = 0; i < sizeof(pins)/sizeof(pins[0]); i++) {
        gpio_reset_pin(pins[i]);
        gpio_set_direction(pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(pins[i], 0);
    }

    while (1) {
        gpio_set_level(GPIO_NUM_21, 1);
        gpio_set_level(GPIO_NUM_1, 1);
        gpio_set_level(GPIO_NUM_45, 1);
        //gpio_set_level(GPIO_NUM_48, 1); 1.8v
        gpio_set_level(GPIO_NUM_2, 1);
        gpio_set_level(GPIO_NUM_46, 1);

        vTaskDelay(pdMS_TO_TICKS(2000));
        gpio_set_level(GPIO_NUM_21, 0);
        gpio_set_level(GPIO_NUM_1, 0);
        gpio_set_level(GPIO_NUM_45, 0);
        //gpio_set_level(GPIO_NUM_48, 0); 1.8v
        gpio_set_level(GPIO_NUM_2, 0);
        gpio_set_level(GPIO_NUM_46, 0);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
 
    ESP_LOGI(TAG, "LCD color test start");

uint16_t *line_buf = heap_caps_malloc(BSP_LCD_H_RES * sizeof(uint16_t), MALLOC_CAP_DMA);
if (line_buf == NULL) {
    ESP_LOGE(TAG, "Failed to allocate LCD line buffer");
    return;
}

while (1) {
    for (int i = 0; i < BSP_LCD_H_RES; i++) {
        line_buf[i] = 0xF800; // red
    }
    for (int y = 0; y < BSP_LCD_V_RES; y++) {
        esp_lcd_panel_draw_bitmap(lcd_panel, 0, y, BSP_LCD_H_RES, y + 1, line_buf);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    for (int i = 0; i < BSP_LCD_H_RES; i++) {
        line_buf[i] = 0x07E0; // green
    }
    for (int y = 0; y < BSP_LCD_V_RES; y++) {
        esp_lcd_panel_draw_bitmap(lcd_panel, 0, y, BSP_LCD_H_RES, y + 1, line_buf);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    for (int i = 0; i < BSP_LCD_H_RES; i++) {
        line_buf[i] = 0x001F; // blue
    }
    for (int y = 0; y < BSP_LCD_V_RES; y++) {
        esp_lcd_panel_draw_bitmap(lcd_panel, 0, y, BSP_LCD_H_RES, y + 1, line_buf);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
}
*/
    // Initialize AI detection
    app_ai_detect_init();
    
    // Initialize mode manager
    app_mode_manager_init(lcd_panel);
    
    
    
    // Register LCD transfer callback for mutual exclusion
    esp_err_t callback_ret = app_mode_manager_register_lcd_callback(lcd_io);
    if (callback_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register LCD callback: %s", esp_err_to_name(callback_ret));
    } else {
        ESP_LOGI(TAG, "LCD transfer synchronization enabled");
    }
    


    /*
    // Initialize i2c
    bsp_i2c_init();
    // Initialize and start camera
    app_camera_init(lcd_panel);
    app_camera_begin();
    app_camera_start();
    */
    ret = bsp_i2c_init(); 
    if (ret != ESP_OK) 
    { ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(ret)); return; } 
    ESP_LOGI(TAG, "I2C init success"); 
    ret = app_camera_init(lcd_panel); 
    if (ret != ESP_OK) 
    { ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(ret)); return; } 
    ESP_LOGI(TAG, "Camera init success"); 
    app_camera_begin(); 
    app_camera_start();

    /* Create touch button */
    //bsp_touch_button_create(button_handler);

    /* Create physical buttons - only BOOT button now, ADC buttons removed to free GPIO1 for LCD MOSI */
    /* 原4个ADC按键已从bsp_button_config移除（见esp_orderist_v2_bsp.c），BOOT现在是数组第0个 */
    button_handle_t btn_array[BSP_BUTTON_INSTALLED_NUM];
    int btn_cnt = 0;
    ret = bsp_iot_button_create(btn_array, &btn_cnt, BSP_BUTTON_INSTALLED_NUM);
    // BOOT 按键现在是数组第 0 个（原来 ADC 键占 0-3，BOOT 是 4）
    if (ret == ESP_OK && btn_cnt > 0) {
        iot_button_register_cb(btn_array[0], BUTTON_SINGLE_CLICK, boot_button_single_click_cb, NULL);
        iot_button_register_cb(btn_array[0], BUTTON_LONG_PRESS_START, boot_button_long_press_cb, NULL);
        ESP_LOGI(TAG, "BOOT button callbacks registered successfully!");
    } else {
        ESP_LOGE(TAG, "Failed to register BOOT button callback!");
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create physical buttons");
    } else {
        ESP_LOGI(TAG, "Created %d physical buttons", btn_cnt);
    }
    /* 原始注释保留（DISABLED版本），便于回滚
    // button_handle_t btn_array[BSP_BUTTON_NUM];
    // int btn_cnt = 0;
    // ret = bsp_iot_button_create(btn_array, &btn_cnt, BSP_BUTTON_NUM);
    // if (ret == ESP_OK && btn_cnt > 4) {
    //     iot_button_register_cb(btn_array[4], BUTTON_SINGLE_CLICK, boot_button_single_click_cb, NULL);
    //     iot_button_register_cb(btn_array[4], BUTTON_LONG_PRESS_START, boot_button_long_press_cb, NULL);
    // }
    */


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
