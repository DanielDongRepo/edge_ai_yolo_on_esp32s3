/*
 * SPDX-FileCopyrightText: 2026 DanielDongRepo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_http_stream_init(void);
bool app_http_stream_send_direct(uint8_t *jpeg_data, size_t jpeg_size);
bool app_http_stream_is_connected(void);

#ifdef __cplusplus
}
#endif