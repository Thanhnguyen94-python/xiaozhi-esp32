#include "lvgl_port_manager.h"

#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>

#define TAG "LvglPortMgr"

namespace {
bool s_initialized = false;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
}  // namespace

bool LvglPortManager::EnsureInitialized() {
    // Fast path: đã init rồi thì trả về ngay (không lock để giảm overhead)
    portENTER_CRITICAL(&s_mux);
    if (s_initialized) {
        portEXIT_CRITICAL(&s_mux);
        return true;
    }
    portEXIT_CRITICAL(&s_mux);

    // Cấu hình LVGL port — giữ NGUYÊN các giá trị mà OledDisplay cũ đang dùng
    // để không làm thay đổi hành vi hiện tại của board 1 màn.
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.task_stack = 6144;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif

    ESP_LOGI(TAG, "Initializing LVGL port (first time)");
    esp_err_t err = lvgl_port_init(&port_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init failed: %s", esp_err_to_name(err));
        return false;
    }

    portENTER_CRITICAL(&s_mux);
    s_initialized = true;
    portEXIT_CRITICAL(&s_mux);

    ESP_LOGI(TAG, "LVGL port initialized");
    return true;
}

bool LvglPortManager::IsInitialized() {
    portENTER_CRITICAL(&s_mux);
    bool v = s_initialized;
    portEXIT_CRITICAL(&s_mux);
    return v;
}