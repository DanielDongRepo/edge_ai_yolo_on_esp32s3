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
    ESP_LOGI(TAG, "Detecting sign language");
    detect_results = app_coco_detect(data, width, height);
    if (detect_results.size() > 0) {
        uint16_t *rgb_buf = data;
        for (const auto& res : detect_results) {
            const auto& box = res.box;
            // Check if bounding box is valid
            if (box.size() >= 4 && std::any_of(box.begin(), box.end(), [](int v) { return v != 0; })) {
                //detect_bound.push_back(box);
                draw_rectangle_rgb(rgb_buf, width, height,
                                box[0], box[1], box[2], box[3],
                                0, 0, 255, 0, 0, 5, true);

                // Display COCO detection class name
                int category = res.category;
                float score = res.score;
                
                const char* class_name = get_coco_class_name(category);
                char label[64];
                snprintf(label, sizeof(label), "%s", class_name);
                
                // Ensure text is displayed inside the bounding box at the top-left corner
                int text_x = box[0] + 5;  // 5 pixels offset from left edge
                int text_y = box[1] + 15; // 15 pixels offset from top edge
                
                // Use esp_painter to draw text
                esp_painter_draw_string(painter, (uint8_t*)rgb_buf, 
                                        width * height * 2,
                                        text_x, text_y, NULL, 
                                        ESP_PAINTER_COLOR_YELLOW, 
                                        label);

                ESP_LOGI(TAG, " 识别到: %-10s | 置信度: %.2f%% | 位置: (%d, %d, %d, %d)",  class_name, score * 100, box[0], box[1], box[2], box[3]);
                //ESP_LOGI(TAG, "x: %d, y: %d, w: %d, h: %d, score: %f, class: %d", box[0], box[1], box[2], box[3], score, category);
            }
        }
    }
    return ESP_OK;
}
