#ifndef ESP_CAMERA_H
#define ESP_CAMERA_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_camera.h"

#include "esp_lcd_panel_ops.h"

#if 1
#define CAMERA_PIN_PWDN -1//1
#define CAMERA_PIN_RESET -1//3

#define CAMERA_PIN_VSYNC 6
#define CAMERA_PIN_HREF 7
#define CAMERA_PIN_PCLK 13
#define CAMERA_PIN_XCLK -1//15

#define CAMERA_PIN_SIOD -1//4
#define CAMERA_PIN_SIOC -1//5

#define CAMERA_PIN_D0 11
#define CAMERA_PIN_D1 9
#define CAMERA_PIN_D2 8
#define CAMERA_PIN_D3 10
#define CAMERA_PIN_D4 12
#define CAMERA_PIN_D5 18
#define CAMERA_PIN_D6 17
#define CAMERA_PIN_D7 16
#endif

#define XCLK_FREQ_HZ 15000000

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_camera_init(esp_lcd_panel_handle_t lcd_panel);
esp_err_t app_camera_begin(void);
esp_err_t app_camera_start(void);
esp_err_t app_camera_stop(void);
bool app_camera_is_finished(void);

#ifdef __cplusplus
}
#endif
#endif