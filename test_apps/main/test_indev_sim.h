#pragma once

#include "lvgl.h"

lv_indev_t *test_indev_sim_create(void);
void test_indev_sim_press(int16_t x, int16_t y);
void test_indev_sim_release(void);
