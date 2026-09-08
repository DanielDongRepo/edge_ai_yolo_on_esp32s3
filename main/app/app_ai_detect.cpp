/*
 * SPDX-FileCopyrightText: 2026 DanielDongRepo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <vector>
#include <string.h>
#include <stdio.h>

#include "bsp/esp-bsp.h"
#include "esp_log.h"

// #include "app_pedestrian_detect.h"
// #include "app_humanface_detect.h"
#include "app_coco_detect.h"

#include "app_drawing_utils.h"

#include "app_ai_detect.h"

#include "esp_painter.h"

static const char *TAG = "app_ai_detect";

static esp_painter_handle_t painter = NULL;

// static PedestrianDetect *ped_detect = NULL;
// static HumanFaceDetect *hum_detect = NULL;
static COCODetect *coco_od_detect = NULL;

static std::list<dl::detect::result_t> detect_results;

esp_err_t app_ai_detect_init(void)
{
    ESP_LOGI(TAG, "Initialize the AI detect");
    // ped_detect = get_pedestrian_detect();
    // assert(ped_detect != NULL);
    
    // hum_detect = get_humanface_detect();
    // assert(hum_detect != NULL);

    coco_od_detect = get_coco_detect();
    assert(coco_od_detect != NULL);

    // Initialize esp_painter
    esp_painter_config_t painter_config = {
        .canvas = {
            .width = BSP_LCD_H_RES,
            .height = BSP_LCD_V_RES
        },
        .color_format = ESP_PAINTER_COLOR_FORMAT_RGB565,
        .default_font = &esp_painter_basic_font_20,
        .swap_rgb565 = true
        
    };
    ESP_ERROR_CHECK(esp_painter_init(&painter_config, &painter));

    return ESP_OK;
}

esp_err_t app_coco_od_detect(uint16_t *data, int width, int height)
{
    esp_painter_set_canvas_size(painter, width, height);
    
    detect_results = app_coco_detect(data, width, height);
    
    if (detect_results.size() > 0) {
        for (const auto& res : detect_results) {
            const auto& box = res.box;
            if (box.size() >= 4) {
                draw_rectangle_rgb(data, width, height,
                                box[0], box[1], box[2], box[3],
                                0, 0, 255, 0, 0, 5, false);

                int category = res.category;
                float score = res.score;
                
                const char* class_name = get_coco_class_name(category);
                char label[64];
                snprintf(label, sizeof(label), "%s", class_name);
                
                int text_x = box[0];
                int text_y = (box[1] > 25) ? (box[1] - 20) : (box[1] + 20);
                
                if (text_y < 0) text_y = 5;
                if (text_y >= height) text_y = height - 25;
                
                esp_painter_draw_string(painter, (uint8_t*)data, 
                                        width * height * 2,
                                        text_x, text_y, NULL, 
                                        ESP_PAINTER_COLOR_BLACK, 
                                        label);

                ESP_LOGI(TAG, " 识别到: %-10s | 置信度: %.2f%% | 位置: (%d, %d, %d, %d)",  class_name, score * 100, box[0], box[1], box[2], box[3]);
            }
        }
    }
    return ESP_OK;
}
