# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

set(COMPONENTS main) # "Trim" the build. Include the minimal set of components; main and anything it depends on.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options(-Wno-attributes -Wno-format) # For LVGL code
project(orderist)

if(EXISTS "$ENV{IDF_PATH}/components/esp_lcd/rgb/esp_lcd_panel_rgb.c")
    set_source_files_properties("$ENV{IDF_PATH}/components/esp_lcd/rgb/esp_lcd_panel_rgb.c"
        PROPERTIES COMPILE_FLAGS "-O1")
endif()
