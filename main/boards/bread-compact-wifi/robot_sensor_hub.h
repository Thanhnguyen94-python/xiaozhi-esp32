#ifndef BREAD_COMPACT_WIFI_ROBOT_SENSOR_HUB_H
#define BREAD_COMPACT_WIFI_ROBOT_SENSOR_HUB_H

// Cam bien an toan cho banh xe:
// - 2 cam bien va cham/vat can digital (LM393, cong tac): truoc + sau, chan chon tu WebUI.
// - 1 cam bien ToF VL6180X (I2C 0x29, chung bus OLED chinh): chan vat can phia truoc
//   hoac phat hien mep ban (gan chuc xuong).
// Chan an toan: cai bo loc vao DualDcMotorController nen MOI nguon lenh (WebUI, giong noi,
// macro, Robot Life, RC) deu khong the chay vao huong dang bi chan.
// Thong so luu NVS (namespace "robot_sensor"), chinh tu WebUI, doi chan khong can reboot.
// File header-only, dat trong boards/bread-compact-wifi/ (khong can sua CMakeLists).

#include "board.h"
#include "display.h"
#include "dual_dc_motor_controller.h"
#include "rc_sbus_controller.h"
#include "settings.h"
#include "vl6180x.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>

#include <cJSON.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

struct SensorConfig {
    int front_en         = 0;   // cam bien va cham truoc
    int front_gpio       = 3;
    int front_active_low = 1;   // LM393: keo xuong 0 khi phat hien
    int back_en          = 0;   // cam bien va cham sau
    int back_gpio        = 18;
    int back_active_low  = 1;
    int tof_en           = 0;   // VL6180X
    int tof_mode         = 0;   // 0 = vat can phia truoc, 1 = mep ban (gan chuc xuong)
    int tof_mm           = 60;  // nguong (mm): vat can < nguong / mep ban > nguong
    int action           = 1;   // 0 = chi dung, 1 = dung + lui nhe
    int backoff_ms       = 250;
    int backoff_speed    = 60;
    int rc_bypass        = 0;   // 1 = tay cam RC duoc phep vuot qua cam bien
    int react_face       = 1;   // 1 = doi mat "giat minh" khi va cham
};

class RobotSensorHub {
public:
    // Chan cho phep chon (da kiem tra trong tren board nay, xem config.h).
    static constexpr int kAllowedGpios[] = {3, 18, 20, 39, 40};

    RobotSensorHub(DualDcMotorController* wheels, i2c_master_bus_handle_t i2c_bus)
        : wheels_(wheels), i2c_bus_(i2c_bus) {
        LoadConfig();
        if (wheels_ != nullptr) {
            wheels_->SetSpeedFilter([this](int& left, int& right) { FilterSpeed(left, right); });
        }
    }

    ~RobotSensorHub() {
        if (task_ != nullptr) {
            vTaskDelete(task_);
            task_ = nullptr;
        }
        if (wheels_ != nullptr) {
            wheels_->SetSpeedFilter(nullptr);
        }
    }

    void Start() {
        if (task_ != nullptr) {
            return;
        }
        if (xTaskCreate(&TaskEntry, "robot_sensor", 4096, this, 4, &task_) != pdPASS) {
            task_ = nullptr;
            ESP_LOGE(TAG, "Khong tao duoc task robot_sensor");
        }
    }

    bool FrontBlocked() const { return front_blocked_.load(); }
    bool BackBlocked() const { return back_blocked_.load(); }

    SensorConfig GetConfig() {
        std::lock_guard<std::mutex> lock(config_mutex_);
        return config_;
    }

    void SetConfig(SensorConfig c) {
        Sanitize(c);
        {
            std::lock_guard<std::mutex> lock(config_mutex_);
            config_ = c;
        }
        config_dirty_.store(true);
        SaveConfig(c);
    }

    // ---- Cau noi voi WebUI ----
    void AppendConfigToJson(cJSON* root) {
        SensorConfig c = GetConfig();
        cJSON_AddNumberToObject(root, "sns_front_en", c.front_en);
        cJSON_AddNumberToObject(root, "sns_front_gpio", c.front_gpio);
        cJSON_AddNumberToObject(root, "sns_front_low", c.front_active_low);
        cJSON_AddNumberToObject(root, "sns_back_en", c.back_en);
        cJSON_AddNumberToObject(root, "sns_back_gpio", c.back_gpio);
        cJSON_AddNumberToObject(root, "sns_back_low", c.back_active_low);
        cJSON_AddNumberToObject(root, "sns_tof_en", c.tof_en);
        cJSON_AddNumberToObject(root, "sns_tof_mode", c.tof_mode);
        cJSON_AddNumberToObject(root, "sns_tof_mm", c.tof_mm);
        cJSON_AddNumberToObject(root, "sns_action", c.action);
        cJSON_AddNumberToObject(root, "sns_backoff_ms", c.backoff_ms);
        cJSON_AddNumberToObject(root, "sns_backoff_speed", c.backoff_speed);
        cJSON_AddNumberToObject(root, "sns_rc_bypass", c.rc_bypass);
        cJSON_AddNumberToObject(root, "sns_react_face", c.react_face);
        cJSON* pins = cJSON_AddArrayToObject(root, "sns_gpio_choices");
        for (int g : kAllowedGpios) {
            cJSON_AddItemToArray(pins, cJSON_CreateNumber(g));
        }
    }

    void ApplyConfigFromJson(const cJSON* root) {
        if (root == nullptr) {
            return;
        }
        SensorConfig c = GetConfig();
        bool changed = false;
        auto get = [&](const char* key, int& dst) {
            const cJSON* it = cJSON_GetObjectItem(root, key);
            if (cJSON_IsNumber(it)) {
                dst = it->valueint;
                changed = true;
            }
        };
        get("sns_front_en", c.front_en);
        get("sns_front_gpio", c.front_gpio);
        get("sns_front_low", c.front_active_low);
        get("sns_back_en", c.back_en);
        get("sns_back_gpio", c.back_gpio);
        get("sns_back_low", c.back_active_low);
        get("sns_tof_en", c.tof_en);
        get("sns_tof_mode", c.tof_mode);
        get("sns_tof_mm", c.tof_mm);
        get("sns_action", c.action);
        get("sns_backoff_ms", c.backoff_ms);
        get("sns_backoff_speed", c.backoff_speed);
        get("sns_rc_bypass", c.rc_bypass);
        get("sns_react_face", c.react_face);
        if (changed) {
            SetConfig(c);
        }
    }

    // Trang thai song cho WebUI (gui qua WebSocket "status").
    void AppendStatusToJson(cJSON* root) {
        cJSON_AddNumberToObject(root, "sns_front", front_blocked_.load() ? 1 : 0);
        cJSON_AddNumberToObject(root, "sns_back", back_blocked_.load() ? 1 : 0);
        cJSON_AddNumberToObject(root, "sns_front_hit", front_hit_.load() ? 1 : 0);
        cJSON_AddNumberToObject(root, "sns_back_hit", back_hit_.load() ? 1 : 0);
        cJSON_AddNumberToObject(root, "sns_tof_ok", tof_ok_.load() ? 1 : 0);
        cJSON_AddNumberToObject(root, "sns_tof_mm_now", tof_mm_.load());
        cJSON_AddNumberToObject(root, "sns_events", events_.load());
    }

    // Thay doi khi trang thai doi -> WebUI chi broadcast khi can.
    uint32_t StatusStamp() const { return stamp_.load(); }

private:
    static constexpr const char* TAG = "RobotSensor";
    static constexpr uint32_t kLoopMs = 20;
    static constexpr int kDebounceSamples = 3;   // 60 ms
    static constexpr int kTofDebounce = 2;
    static constexpr uint32_t kFaceGapMs = 3000;

    DualDcMotorController* wheels_ = nullptr;
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    TaskHandle_t task_ = nullptr;

    std::mutex config_mutex_;
    SensorConfig config_;
    std::atomic<bool> config_dirty_{true};

    // Ket qua tong hop (doc tu filter trong ngu canh task khac -> atomic)
    std::atomic<bool> front_blocked_{false};
    std::atomic<bool> back_blocked_{false};
    std::atomic<bool> rc_bypass_{false};
    std::atomic<bool> rc_override_{false};  // cache tu task, filter khong phai khoa mutex RC
    // Chi tiet cho WebUI
    std::atomic<bool> front_hit_{false};
    std::atomic<bool> back_hit_{false};
    std::atomic<bool> tof_ok_{false};
    std::atomic<int> tof_mm_{Vl6180x::kNoTarget};
    std::atomic<uint32_t> events_{0};
    std::atomic<uint32_t> stamp_{0};

    // Trang thai cua task
    Vl6180x tof_;
    int front_pin_ = -1;
    int back_pin_ = -1;
    int front_cnt_ = 0;
    int back_cnt_ = 0;
    int tof_bad_cnt_ = 0;
    int tof_good_cnt_ = 0;
    bool tof_blocked_ = false;
    uint32_t next_tof_try_ms_ = 0;
    uint32_t last_face_ms_ = 0;
    int last_reported_mm_ = -2;

    static bool IsAllowedGpio(int g) {
        for (int a : kAllowedGpios) {
            if (a == g) return true;
        }
        return false;
    }

    static void Sanitize(SensorConfig& c) {
        c.front_en = c.front_en ? 1 : 0;
        c.back_en = c.back_en ? 1 : 0;
        c.front_active_low = c.front_active_low ? 1 : 0;
        c.back_active_low = c.back_active_low ? 1 : 0;
        if (!IsAllowedGpio(c.front_gpio)) c.front_gpio = 3;
        if (!IsAllowedGpio(c.back_gpio)) c.back_gpio = 18;
        if (c.front_en && c.back_en && c.front_gpio == c.back_gpio) {
            c.back_en = 0;  // trung chan: chi giu cam bien truoc
        }
        c.tof_en = c.tof_en ? 1 : 0;
        c.tof_mode = c.tof_mode ? 1 : 0;
        c.tof_mm = std::clamp(c.tof_mm, 10, 200);
        c.action = c.action ? 1 : 0;
        c.backoff_ms = std::clamp(c.backoff_ms, 100, 1000);
        c.backoff_speed = std::clamp(c.backoff_speed, 30, 100);
        c.rc_bypass = c.rc_bypass ? 1 : 0;
        c.react_face = c.react_face ? 1 : 0;
    }

    void LoadConfig() {
        SensorConfig c;
        {
            Settings s("robot_sensor", false);
            c.front_en         = s.GetInt("f_en", c.front_en);
            c.front_gpio       = s.GetInt("f_gpio", c.front_gpio);
            c.front_active_low = s.GetInt("f_low", c.front_active_low);
            c.back_en          = s.GetInt("b_en", c.back_en);
            c.back_gpio        = s.GetInt("b_gpio", c.back_gpio);
            c.back_active_low  = s.GetInt("b_low", c.back_active_low);
            c.tof_en           = s.GetInt("tof_en", c.tof_en);
            c.tof_mode         = s.GetInt("tof_mode", c.tof_mode);
            c.tof_mm           = s.GetInt("tof_mm", c.tof_mm);
            c.action           = s.GetInt("action", c.action);
            c.backoff_ms       = s.GetInt("bo_ms", c.backoff_ms);
            c.backoff_speed    = s.GetInt("bo_speed", c.backoff_speed);
            c.rc_bypass        = s.GetInt("rc_bypass", c.rc_bypass);
            c.react_face       = s.GetInt("face", c.react_face);
        }
        Sanitize(c);
        std::lock_guard<std::mutex> lock(config_mutex_);
        config_ = c;
    }

    void SaveConfig(const SensorConfig& c) {
        Settings s("robot_sensor", true);
        s.SetInt("f_en", c.front_en);
        s.SetInt("f_gpio", c.front_gpio);
        s.SetInt("f_low", c.front_active_low);
        s.SetInt("b_en", c.back_en);
        s.SetInt("b_gpio", c.back_gpio);
        s.SetInt("b_low", c.back_active_low);
        s.SetInt("tof_en", c.tof_en);
        s.SetInt("tof_mode", c.tof_mode);
        s.SetInt("tof_mm", c.tof_mm);
        s.SetInt("action", c.action);
        s.SetInt("bo_ms", c.backoff_ms);
        s.SetInt("bo_speed", c.backoff_speed);
        s.SetInt("rc_bypass", c.rc_bypass);
        s.SetInt("face", c.react_face);
    }

    static uint32_t NowMs() {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    }

    // Goi trong SetSpeed (dang giu mutex cua motor): chi doc atomic, khong block.
    void FilterSpeed(int& left, int& right) {
        if (rc_bypass_.load() && rc_override_.load()) {
            return;
        }
        int forward = left + right;  // >0 dang tien, <0 dang lui, =0 quay tai cho
        if ((forward > 0 && front_blocked_.load()) || (forward < 0 && back_blocked_.load())) {
            left = 0;
            right = 0;
        }
    }

    void ReleasePin(int& pin) {
        if (pin >= 0) {
            gpio_reset_pin(static_cast<gpio_num_t>(pin));
            pin = -1;
        }
    }

    void SetupPin(int& pin, bool enable, int gpio) {
        if (!enable) {
            ReleasePin(pin);
            return;
        }
        if (pin == gpio) {
            return;
        }
        ReleasePin(pin);
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << gpio;
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;  // module LM393 da co keo len, them noi bo cho cong tac tran
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io.intr_type = GPIO_INTR_DISABLE;
        if (gpio_config(&io) == ESP_OK) {
            pin = gpio;
            ESP_LOGI(TAG, "Cam bien digital tren GPIO%d", gpio);
        } else {
            ESP_LOGE(TAG, "Khong cau hinh duoc GPIO%d", gpio);
        }
    }

    void ApplyPins(const SensorConfig& c) {
        // Nha chan cu truoc de doi cheo truoc/sau khong bi xung dot.
        if (!c.front_en || front_pin_ != c.front_gpio) ReleasePin(front_pin_);
        if (!c.back_en || back_pin_ != c.back_gpio) ReleasePin(back_pin_);
        SetupPin(front_pin_, c.front_en, c.front_gpio);
        SetupPin(back_pin_, c.back_en, c.back_gpio);
        front_cnt_ = back_cnt_ = 0;
        front_hit_.store(false);
        back_hit_.store(false);
        if (!c.tof_en) {
            tof_.Close();
            tof_ok_.store(false);
            tof_mm_.store(Vl6180x::kNoTarget);
        }
        tof_blocked_ = false;
        tof_bad_cnt_ = tof_good_cnt_ = 0;
        next_tof_try_ms_ = 0;
        rc_bypass_.store(c.rc_bypass != 0);
    }

    bool ReadPinActive(int pin, int active_low) {
        if (pin < 0) return false;
        int level = gpio_get_level(static_cast<gpio_num_t>(pin));
        return active_low ? (level == 0) : (level == 1);
    }

    // Chong doi: can kDebounceSamples mau lien tiep moi doi trang thai.
    static bool Debounce(bool raw, bool current, int& cnt) {
        if (raw == current) {
            cnt = 0;
            return current;
        }
        if (++cnt >= kDebounceSamples) {
            cnt = 0;
            return raw;
        }
        return current;
    }

    void UpdateTof(const SensorConfig& c, uint32_t now) {
        if (!c.tof_en) {
            tof_blocked_ = false;
            return;
        }
        if (!tof_.IsOpen()) {
            if (now >= next_tof_try_ms_) {
                next_tof_try_ms_ = now + 2000;
                if (tof_.Open(i2c_bus_)) {
                    ESP_LOGI(TAG, "Da ket noi VL6180X");
                } else {
                    ESP_LOGW(TAG, "Khong thay VL6180X tren bus I2C (0x29)");
                }
            }
            if (!tof_.IsOpen()) {
                tof_ok_.store(false);
                tof_blocked_ = true;  // bat cam bien ma khong doc duoc -> an toan: chan
                return;
            }
        }
        int mm = Vl6180x::kNoTarget;
        if (!tof_.ReadRangeMm(mm)) {
            tof_.Close();  // thu mo lai o vong sau
            tof_ok_.store(false);
            tof_blocked_ = true;
            return;
        }
        tof_ok_.store(true);
        tof_mm_.store(mm);
        bool bad;
        if (c.tof_mode == 0) {
            bad = (mm != Vl6180x::kNoTarget) && (mm < c.tof_mm);  // vat can gan
        } else {
            bad = (mm == Vl6180x::kNoTarget) || (mm > c.tof_mm);  // mat mat ban
        }
        if (bad) {
            tof_good_cnt_ = 0;
            if (++tof_bad_cnt_ >= kTofDebounce) tof_blocked_ = true;
        } else {
            tof_bad_cnt_ = 0;
            if (++tof_good_cnt_ >= kTofDebounce) tof_blocked_ = false;
        }
    }

    // Lui nhe ra khoi vat can; dung som neu phia sau cung bi chan.
    void BackOff(bool forward_hit, const SensorConfig& c) {
        int speed = forward_hit ? -c.backoff_speed : c.backoff_speed;
        wheels_->SetSpeed(speed, speed);
        for (uint32_t t = 0; t < static_cast<uint32_t>(c.backoff_ms); t += kLoopMs) {
            vTaskDelay(pdMS_TO_TICKS(kLoopMs));
            bool other_side = forward_hit ? ReadPinActive(back_pin_, c.back_active_low)
                                          : ReadPinActive(front_pin_, c.front_active_low);
            if (other_side) break;
        }
        int l = 0, r = 0;
        wheels_->GetSpeed(l, r);
        if (l == speed && r == speed) {
            wheels_->Stop();  // chi dung neu khong co lenh nao khac chen vao
        }
    }

    void OnBlocked(bool front, const SensorConfig& c) {
        if (wheels_ == nullptr || !wheels_->IsReady()) {
            return;
        }
        if (c.rc_bypass && rc_sbus::IsOverrideActive()) {
            return;
        }
        int l = 0, r = 0;
        wheels_->GetSpeed(l, r);
        bool moving_into = front ? (l + r > 0) : (l + r < 0);
        if (!moving_into) {
            return;  // dang dung yen hoac di ra xa: chi chan lenh moi, khong lam gi them
        }
        events_.fetch_add(1);
        ESP_LOGW(TAG, "Phat hien vat can/mep ban phia %s -> dung banh", front ? "TRUOC" : "SAU");
        wheels_->Stop();
        bool other_blocked = front ? back_blocked_.load() : front_blocked_.load();
        if (c.action == 1 && !other_blocked) {
            BackOff(front, c);
        }
        uint32_t now = NowMs();
        if (c.react_face && (now - last_face_ms_) >= kFaceGapMs) {
            last_face_ms_ = now;
            auto* display = Board::GetInstance().GetDisplay();
            if (display != nullptr) {
                display->SetEmotion("surprised");
            }
        }
    }

    static void TaskEntry(void* arg) {
        static_cast<RobotSensorHub*>(arg)->Loop();
    }

    void Loop() {
        SensorConfig c = GetConfig();
        bool tof_turn = false;
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(kLoopMs));
            if (config_dirty_.exchange(false)) {
                c = GetConfig();
                ApplyPins(c);
            }
            uint32_t now = NowMs();
            rc_override_.store(rc_sbus::IsOverrideActive());

            bool fhit = Debounce(ReadPinActive(front_pin_, c.front_active_low), front_hit_.load(), front_cnt_);
            bool bhit = Debounce(ReadPinActive(back_pin_, c.back_active_low), back_hit_.load(), back_cnt_);
            front_hit_.store(fhit);
            back_hit_.store(bhit);

            tof_turn = !tof_turn;  // ToF doc moi 40 ms
            if (tof_turn) {
                UpdateTof(c, now);
            }

            bool front = fhit || tof_blocked_;
            bool back = bhit;
            bool was_front = front_blocked_.exchange(front);
            bool was_back = back_blocked_.exchange(back);

            int mm = tof_mm_.load();
            if (front != was_front || back != was_back || mm != last_reported_mm_) {
                last_reported_mm_ = mm;
                stamp_.fetch_add(1);
            }
            if (front && !was_front) OnBlocked(true, c);
            if (back && !was_back) OnBlocked(false, c);
        }
    }
};

#endif  // BREAD_COMPACT_WIFI_ROBOT_SENSOR_HUB_H
