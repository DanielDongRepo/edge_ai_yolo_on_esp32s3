#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"

#include "app_camera.h"

// #include "lvgl.h"
#include "bsp/esp-bsp.h"

#include "app_ai_detect.h"
#include "app_mode_manager.h"

#include "sensor.h"

static const char *TAG = "app_camera";

static bool camera_running = false;
static bool camera_finished = false;

static esp_lcd_panel_handle_t st_lcd_panel;

esp_err_t app_camera_init(esp_lcd_panel_handle_t lcd_panel)
{
    /*
    gpio_config_t conf;
    conf.mode = GPIO_MODE_INPUT;
    conf.pull_up_en = GPIO_PULLUP_ENABLE;
    conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    conf.intr_type = GPIO_INTR_DISABLE;
    conf.pin_bit_mask = 1LL << 13;
    gpio_config(&conf);
    conf.pin_bit_mask = 1LL << 14;
    gpio_config(&conf);
    */
    ESP_LOGI(TAG, "Camera init start");
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
    config.xclk_freq_hz = XCLK_FREQ_HZ;
    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = FRAMESIZE_240X240;
    config.jpeg_quality = 12;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.sccb_i2c_port = BSP_I2C_NUM;

    // save lcd panel
    st_lcd_panel = lcd_panel;

    ESP_LOGI(TAG, "Config: xclk_pin=%d, xclk_freq=%d", config.pin_xclk, config.xclk_freq_hz);
    ESP_LOGI(TAG, "Config: pwdn=%d, reset=%d", config.pin_pwdn, config.pin_reset);

    // camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%x (%s)", err, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Camera init success");
    sensor_t *s = esp_camera_sensor_get();

    // s->set_vflip(s, 1); // flip it back
    // initial sensors are flipped vertically and colors are a bit saturated
    if (s->id.PID == OV3660_PID) {
        s->set_brightness(s, 1);  // up the blightness just a bit
        s->set_saturation(s, -2); // lower the saturation
    }
    s->set_sharpness(s, 2);
    s->set_awb_gain(s, 2);

    st_lcd_panel = lcd_panel;

    return ESP_OK;
}

static void app_camera_task(void *arg)
{
    ESP_LOGD(TAG, "Start");
    while (true) {
        if (camera_running) {
            camera_finished = false;
            // Check if we're in camera display mode
            if (app_mode_manager_get_mode() == MODE_CAMERA_DISPLAY) {
                camera_fb_t *frame = esp_camera_fb_get();
                if (frame == NULL) {
                    ESP_LOGE(TAG, "Camera capture failed");
                } else {
                    // Only display raw camera feed in camera mode (no AI detection)

                    /*uint16_t color = 0xF800; // 纯红色
                    esp_lcd_panel_draw_bitmap(st_lcd_panel, 0, 0, 239, 239, &color);
                    ESP_LOGI(TAG, "Draw red color");*/
                    esp_lcd_panel_draw_bitmap(st_lcd_panel, 0, 0, BSP_LCD_H_RES, BSP_LCD_V_RES, frame->buf);
                    ESP_LOGI(TAG, "Draw camera bitmap");

                    esp_camera_fb_return(frame);
                }
            } else {
                // In AI detection mode, let mode manager handle the task
                app_mode_manager_task();
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }
            camera_finished = true;
        } else {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
    ESP_LOGD(TAG, "Stop");
    vTaskDelete(NULL);
}

/*
static void app_camera_task(void *arg)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(50); // 强制 20Hz 刷新率 (50ms)

    while (true)
    {
        if (camera_running) {
            // 检查模式
            if (app_mode_manager_get_mode() == MODE_CAMERA_DISPLAY) {
                // 【关键】使用带超时的获取，防止永久阻塞
                // 注意：标准 esp_camera_fb_get 没有超时参数，
                // 但我们可以通过外部延时来控制调用频率

                camera_fb_t *frame = esp_camera_fb_get();

                if(frame != NULL) {
                    esp_lcd_panel_draw_bitmap(st_lcd_panel, 0, 0, BSP_LCD_H_RES, BSP_LCD_V_RES, frame->buf);
                    esp_camera_fb_return(frame); // 必须立即归还
                } else {
                    ESP_LOGW(TAG, "Frame capture failed or timeout");
                }
            } else {
                // AI 模式下不要在这里做任何事，交给 mode_manager
                // 但为了防止空转，稍微延时
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
        }

        // 【核心修复】无论是否成功，都强制等待到下一个周期
        // 这将把刷新率锁定在 20FPS 左右，彻底消除 OVF
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}
*/

esp_err_t app_camera_begin(void)
{
    xTaskCreatePinnedToCore(app_camera_task, "app_camera_task", 4096, NULL, 5, NULL, 1);
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
