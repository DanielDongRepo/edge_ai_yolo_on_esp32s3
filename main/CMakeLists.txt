set(APP_DIR app)
file(GLOB_RECURSE APP_SOURCES ${APP_DIR}/*.c)
file(GLOB_RECURSE APP_CPP_SOURCES ${APP_DIR}/*.cpp)

idf_component_register(
    SRCS "main.c" ${APP_SOURCES} ${APP_CPP_SOURCES}
    INCLUDE_DIRS "." ${APP_DIR}
    REQUIRES coco_detect esp_painter esp_orderist_bsp esp_wifi esp_http_server nvs_flash)
