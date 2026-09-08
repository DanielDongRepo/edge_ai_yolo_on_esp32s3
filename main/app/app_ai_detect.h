#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_ai_detect_init(void);

esp_err_t app_coco_od_detect(uint16_t *data, int width, int height);

#ifdef __cplusplus
}
#endif
