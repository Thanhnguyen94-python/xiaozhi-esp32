#ifndef BREAD_COMPACT_WIFI_HOTPLUG_OLED_PANEL_H
#define BREAD_COMPACT_WIFI_HOTPLUG_OLED_PANEL_H

// Lop boc panel OLED I2C cho phep rut/cam nong ma khong treo LVGL.
//
// Van de: khi OLED bi rut, esp_lcd_panel_draw_bitmap() loi I2C nen esp_lvgl_port
// khong bao gio goi lv_display_flush_ready() -> LVGL ket trong wait_for_flushing,
// giu khoa LVGL -> ca 2 man dung, task main bi chan, task watchdog.
//
// Cach xu ly: panel proxy dung truoc panel that.
// - Ve loi -> tu goi flush_ready, danh dau offline, bo qua moi lenh ve sau do.
// - Task nen do I2C moi 1 giay, thay man thi init lai + ve lai toan bo man hinh.
// File header-only, dat trong boards/bread-compact-wifi/ (khong can sua CMakeLists).

#include <driver/i2c_master.h>
#include <esp_lcd_panel_interface.h>
#include <esp_lcd_panel_ops.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

#include <atomic>
#include <cstddef>

class HotplugOledPanel {
public:
    HotplugOledPanel(const char* name, i2c_master_bus_handle_t bus, uint16_t addr,
                     esp_lcd_panel_handle_t real_panel)
        : name_(name), bus_(bus), addr_(addr), real_(real_panel) {
        proxy_.owner = this;
        esp_lcd_panel_t& p = proxy_.base;
        p.reset = &Reset;
        p.init = &Init;
        p.del = &Del;
        p.draw_bitmap = &DrawBitmap;
        // Chi khai bao cac ham panel that co ho tro, de esp_lcd tra NOT_SUPPORTED giong nhu cu.
        p.mirror = real_->mirror ? &Mirror : nullptr;
        p.swap_xy = real_->swap_xy ? &SwapXY : nullptr;
        p.set_gap = real_->set_gap ? &SetGap : nullptr;
        p.invert_color = real_->invert_color ? &InvertColor : nullptr;
        p.disp_on_off = real_->disp_on_off ? &DispOnOff : nullptr;
        p.disp_sleep = real_->disp_sleep ? &DispSleep : nullptr;
    }

    // Handle dua cho OledDisplay / esp_lvgl_port thay cho panel that.
    esp_lcd_panel_handle_t handle() { return &proxy_.base; }

    // Goi sau khi OledDisplay tao xong lv_display, roi bat task theo doi.
    void Start(lv_display_t* disp) {
        disp_ = disp;
        if (task_ == nullptr) {
            xTaskCreate(&TaskEntry, "oled_hotplug", 3072, this, 1, &task_);
        }
    }

    bool online() const { return online_.load(); }

private:
    struct Proxy {
        esp_lcd_panel_t base;  // phai dung dau de cast nguoc tu esp_lcd_panel_t*
        HotplugOledPanel* owner;
    };

    static constexpr const char* TAG = "OledHotplug";

    const char* name_;
    i2c_master_bus_handle_t bus_;
    uint16_t addr_;
    esp_lcd_panel_handle_t real_;
    Proxy proxy_ = {};
    lv_display_t* disp_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::atomic<bool> online_{true};

    // Trang thai cau hinh, ap lai sau khi init lai (SSD1306 mat het khi mat dien).
    bool mirror_x_ = false;
    bool mirror_y_ = false;
    bool swap_xy_ = false;
    int gap_x_ = 0;
    int gap_y_ = 0;
    bool invert_ = false;
    bool on_ = true;

    static HotplugOledPanel* Self(esp_lcd_panel_t* p) {
        return reinterpret_cast<Proxy*>(p)->owner;
    }

    void MarkOffline(esp_err_t err) {
        if (online_.exchange(false)) {
            ESP_LOGW(TAG, "%s: mat ket noi (%s), tam dung ve, cho cam lai", name_, esp_err_to_name(err));
        }
    }

    static esp_err_t DrawBitmap(esp_lcd_panel_t* p, int x0, int y0, int x1, int y1, const void* data) {
        HotplugOledPanel* self = Self(p);
        if (self->online_.load()) {
            esp_err_t err = esp_lcd_panel_draw_bitmap(self->real_, x0, y0, x1, y1, data);
            if (err == ESP_OK) {
                return ESP_OK;  // callback on_color_trans_done se bao flush_ready
            }
            self->MarkOffline(err);
        }
        // Loi hoac dang offline: callback I2C khong chay, tu bao xong de LVGL khong ket.
        if (self->disp_ != nullptr) {
            lv_display_flush_ready(self->disp_);
        }
        return ESP_OK;
    }

    // Cac lenh khac: luu lai trang thai; chi gui xuong panel khi dang online.
    template <typename Fn>
    static esp_err_t Forward(esp_lcd_panel_t* p, Fn fn) {
        HotplugOledPanel* self = Self(p);
        if (!self->online_.load()) {
            return ESP_OK;
        }
        esp_err_t err = fn(self->real_);
        if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
            self->MarkOffline(err);
        }
        return err;
    }

    static esp_err_t Reset(esp_lcd_panel_t* p) {
        return Forward(p, [](esp_lcd_panel_handle_t r) { return esp_lcd_panel_reset(r); });
    }
    static esp_err_t Init(esp_lcd_panel_t* p) {
        return Forward(p, [](esp_lcd_panel_handle_t r) { return esp_lcd_panel_init(r); });
    }
    static esp_err_t Del(esp_lcd_panel_t* p) {
        HotplugOledPanel* self = Self(p);
        if (self->task_ != nullptr) {
            vTaskDelete(self->task_);
            self->task_ = nullptr;
        }
        return esp_lcd_panel_del(self->real_);
    }
    static esp_err_t Mirror(esp_lcd_panel_t* p, bool x, bool y) {
        Self(p)->mirror_x_ = x;
        Self(p)->mirror_y_ = y;
        return Forward(p, [x, y](esp_lcd_panel_handle_t r) { return esp_lcd_panel_mirror(r, x, y); });
    }
    static esp_err_t SwapXY(esp_lcd_panel_t* p, bool swap) {
        Self(p)->swap_xy_ = swap;
        return Forward(p, [swap](esp_lcd_panel_handle_t r) { return esp_lcd_panel_swap_xy(r, swap); });
    }
    static esp_err_t SetGap(esp_lcd_panel_t* p, int x, int y) {
        Self(p)->gap_x_ = x;
        Self(p)->gap_y_ = y;
        return Forward(p, [x, y](esp_lcd_panel_handle_t r) { return esp_lcd_panel_set_gap(r, x, y); });
    }
    static esp_err_t InvertColor(esp_lcd_panel_t* p, bool invert) {
        Self(p)->invert_ = invert;
        return Forward(p, [invert](esp_lcd_panel_handle_t r) { return esp_lcd_panel_invert_color(r, invert); });
    }
    static esp_err_t DispOnOff(esp_lcd_panel_t* p, bool on) {
        Self(p)->on_ = on;
        return Forward(p, [on](esp_lcd_panel_handle_t r) { return esp_lcd_panel_disp_on_off(r, on); });
    }
    static esp_err_t DispSleep(esp_lcd_panel_t* p, bool sleep) {
        return Forward(p, [sleep](esp_lcd_panel_handle_t r) { return esp_lcd_panel_disp_sleep(r, sleep); });
    }

    // Khoi tao lai panel that sau khi cam lai, ap lai cau hinh da luu.
    bool Reinitialize() {
        if (esp_lcd_panel_reset(real_) != ESP_OK || esp_lcd_panel_init(real_) != ESP_OK) {
            return false;
        }
        if (real_->mirror) esp_lcd_panel_mirror(real_, mirror_x_, mirror_y_);
        if (real_->swap_xy) esp_lcd_panel_swap_xy(real_, swap_xy_);
        if (real_->set_gap) esp_lcd_panel_set_gap(real_, gap_x_, gap_y_);
        return esp_lcd_panel_invert_color(real_, invert_) == ESP_OK &&
               esp_lcd_panel_disp_on_off(real_, on_) == ESP_OK;
    }

    void Redraw() {
        if (disp_ == nullptr || !lvgl_port_lock(500)) {
            return;
        }
        lv_obj_invalidate(lv_display_get_screen_active(disp_));
        lv_obj_invalidate(lv_display_get_layer_top(disp_));
        lvgl_port_unlock();
    }

    static void TaskEntry(void* arg) {
        static_cast<HotplugOledPanel*>(arg)->Loop();
    }

    void Loop() {
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            if (online_.load()) {
                continue;
            }
            // Dang offline: LVGL khong dung bus nay, an toan de reset/do.
            i2c_master_bus_reset(bus_);
            if (i2c_master_probe(bus_, addr_, 50) != ESP_OK) {
                continue;
            }
            vTaskDelay(pdMS_TO_TICKS(100));  // cho OLED on dinh nguon sau khi cam
            if (!Reinitialize()) {
                ESP_LOGW(TAG, "%s: thay thiet bi nhung init lai that bai, thu lai sau", name_);
                continue;
            }
            online_.store(true);
            Redraw();
            ESP_LOGI(TAG, "%s: da ket noi lai, ve lai man hinh", name_);
        }
    }
};

#endif  // BREAD_COMPACT_WIFI_HOTPLUG_OLED_PANEL_H
