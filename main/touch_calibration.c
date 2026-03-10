#include "touch_calibration.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "touch_cal";
static const char *NVS_NAMESPACE = "touch_cal";

esp_err_t touch_cal_compute(const uint16_t raw_x[3], const uint16_t raw_y[3],
                            const uint16_t scr_x[3], const uint16_t scr_y[3],
                            touch_cal_t *cal)
{
    // Solve affine transform from 3 point pairs using Cramer's rule
    float x0 = raw_x[0], y0 = raw_y[0];
    float x1 = raw_x[1], y1 = raw_y[1];
    float x2 = raw_x[2], y2 = raw_y[2];

    float det = x0 * (y1 - y2) - y0 * (x1 - x2) + (x1 * y2 - x2 * y1);
    if (det == 0.0f || (det > -0.001f && det < 0.001f)) {
        ESP_LOGE(TAG, "Degenerate calibration points (det=%.4f)", det);
        return ESP_ERR_INVALID_ARG;
    }

    float inv_det = 1.0f / det;

    float sx0 = scr_x[0], sx1 = scr_x[1], sx2 = scr_x[2];
    cal->a = (sx0 * (y1 - y2) - y0 * (sx1 - sx2) + (sx1 * y2 - sx2 * y1)) * inv_det;
    cal->b = (x0 * (sx1 - sx2) - sx0 * (x1 - x2) + (x1 * sx2 - x2 * sx1)) * inv_det;
    cal->c = (x0 * (y1 * sx2 - y2 * sx1) - y0 * (x1 * sx2 - x2 * sx1) + sx0 * (x1 * y2 - x2 * y1)) * inv_det;

    float sy0 = scr_y[0], sy1 = scr_y[1], sy2 = scr_y[2];
    cal->d = (sy0 * (y1 - y2) - y0 * (sy1 - sy2) + (sy1 * y2 - sy2 * y1)) * inv_det;
    cal->e = (x0 * (sy1 - sy2) - sy0 * (x1 - x2) + (x1 * sy2 - x2 * sy1)) * inv_det;
    cal->f = (x0 * (y1 * sy2 - y2 * sy1) - y0 * (x1 * sy2 - x2 * sy1) + sy0 * (x1 * y2 - x2 * y1)) * inv_det;

    cal->valid = true;
    ESP_LOGI(TAG, "Calibration computed: a=%.4f b=%.4f c=%.1f d=%.4f e=%.4f f=%.1f",
             cal->a, cal->b, cal->c, cal->d, cal->e, cal->f);
    return ESP_OK;
}

void touch_cal_apply(const touch_cal_t *cal, uint16_t raw_x, uint16_t raw_y,
                     uint16_t *scr_x, uint16_t *scr_y, uint16_t x_max, uint16_t y_max)
{
    float sx = cal->a * raw_x + cal->b * raw_y + cal->c;
    float sy = cal->d * raw_x + cal->e * raw_y + cal->f;

    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx >= x_max) sx = x_max - 1;
    if (sy >= y_max) sy = y_max - 1;

    *scr_x = (uint16_t)sx;
    *scr_y = (uint16_t)sy;
}

esp_err_t touch_cal_save(const touch_cal_t *cal)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    float coeffs[6] = { cal->a, cal->b, cal->c, cal->d, cal->e, cal->f };
    err = nvs_set_blob(nvs, "coeffs", coeffs, sizeof(coeffs));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Calibration saved to NVS");
    }
    return err;
}

esp_err_t touch_cal_load(touch_cal_t *cal)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        cal->valid = false;
        return (err == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : err;
    }

    float coeffs[6];
    size_t len = sizeof(coeffs);
    err = nvs_get_blob(nvs, "coeffs", coeffs, &len);
    nvs_close(nvs);

    if (err == ESP_OK && len == sizeof(coeffs)) {
        cal->a = coeffs[0]; cal->b = coeffs[1]; cal->c = coeffs[2];
        cal->d = coeffs[3]; cal->e = coeffs[4]; cal->f = coeffs[5];
        cal->valid = true;
        ESP_LOGI(TAG, "Calibration loaded from NVS");
    } else {
        cal->valid = false;
    }
    return ESP_OK;
}

esp_err_t touch_cal_clear(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_erase_key(nvs, "coeffs");
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_commit(nvs);
        err = ESP_OK;
    }
    nvs_close(nvs);
    ESP_LOGI(TAG, "Calibration cleared");
    return err;
}

esp_err_t touch_z_threshold_save(uint16_t threshold)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_set_u16(nvs, "z_thresh", threshold);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

uint16_t touch_z_threshold_load(uint16_t default_val)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) return default_val;

    uint16_t val = default_val;
    nvs_get_u16(nvs, "z_thresh", &val);
    nvs_close(nvs);
    return val;
}
