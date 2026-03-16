#include "test_indev_sim.h"

static int16_t s_x;
static int16_t s_y;
static bool s_pressed;

static void sim_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    data->point.x = s_x;
    data->point.y = s_y;
    data->state = s_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

lv_indev_t *test_indev_sim_create(void)
{
    lv_indev_t *indev = lv_indev_create();
    if (!indev) return NULL;

    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, sim_read_cb);
    return indev;
}

void test_indev_sim_press(int16_t x, int16_t y)
{
    s_x = x;
    s_y = y;
    s_pressed = true;
}

void test_indev_sim_release(void)
{
    s_pressed = false;
}
