/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "iostream"
#include "coco_detect.hpp"
#include "dl_tool.hpp"
#include "dl_image_define.hpp"
#include "app_coco_detect.h"

static const char *TAG = "app_coco_detect";
static COCODetect *detect = NULL;

// COCO dataset class names
/*
static const char* COCO_CLASSES[] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
    "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
    "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", 
    "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple", 
    "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
    "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
    "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
    "hair drier", "toothbrush"
};

static const char* COCO_CLASSES[] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
    "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
    "U", "V", "W", "X", "Y", "Z", "额外的", "酒精", "过敏", "培根",
    "袋子", "烧烤", "账单", "饼干", "苦的", "面包", "汉堡包", "再见", "蛋糕", "现金",
    "奶酪", "鸡肉", "可乐", "冷的", "费用", "优惠券", "信用卡", "杯子", "甜点", "饮料",
    "开车", "吃", "鸡蛋", "享受", "叉子", "炸薯条", "新鲜的", "你好", "热的", "冰淇淋",
    "成分", "多汁的", "番茄酱", "乳糖", "生菜", "盖子", "经理", "菜单", "牛奶", "芥末",
    "餐巾纸", "不", "订单", "胡椒", "泡菜", "披萨", "请", "准备好的", "收据", "续杯",
    "重复", "安全的", "盐", "三明治", "酱料", "小的", "苏打水", "对不起", "辣的", "勺子",
    "吸管", "糖", "甜的", "谢谢", "纸巾", "番茄", "总计", "紧急", "蔬菜", "等待",
    "温暖的", "水", "什么", "将要", "酸奶", "你的"
};
*/
static const char* COCO_CLASSES[] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
    "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
    "U", "V", "W", "X", "Y", "Z", "Extra", "Alcohol", "Allergy", "Bacon",
    "Bag", "Barbecue", "Bill", "Cookie", "Bitter", "Bread", "Hamburger", "Goodbye", "Cake", "Cash",
    "Cheese", "Chicken", "Cola", "Cold", "Fee", "Coupon", "Credit Card", "Cup", "Dessert", "Drink",
    "Drive", "Eat", "Egg", "Enjoy", "Fork", "French Fries", "Fresh", "Hello", "Hot", "Ice Cream",
    "Ingredient", "Juicy", "Ketchup", "Lactose", "Lettuce", "Lid", "Manager", "Menu", "Milk", "Mustard",
    "Napkin", "No", "Order", "Pepper", "Pickle", "Pizza", "Please", "Ready", "Receipt", "Refill",
    "Repeat", "Safe", "Salt", "Sandwich", "Sauce", "Small", "Soda", "Sorry", "Spicy", "Spoon",
    "Straw", "Sugar", "Sweet", "Thank You", "Tissue", "Tomato", "Total", "Emergency", "Vegetable", "Wait",
    "Warm", "Water", "What", "Will", "Yogurt", "Your"
};
#define COCO_CLASS_COUNT (sizeof(COCO_CLASSES) / sizeof(COCO_CLASSES[0]))

const char* get_coco_class_name(int category_index)
{
    if (category_index >= 0 && category_index < COCO_CLASS_COUNT) {
        return COCO_CLASSES[category_index];
    }
    return "unknown";
}

std::list<dl::detect::result_t> app_coco_detect(uint16_t *frame, int width, int height)
{
    dl::image::img_t img;
    img.data = frame;
    img.width = width;
    img.height = height;
    img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565;

    auto &detect_results = detect->run(img);

    return detect_results;
}

COCODetect *get_coco_detect()
{
    if (detect == NULL) {
        detect = new COCODetect();
    }

    return detect;
}

void delete_coco_detect()
{
    if (detect) {
        delete detect;
        detect = NULL;
    }
} 