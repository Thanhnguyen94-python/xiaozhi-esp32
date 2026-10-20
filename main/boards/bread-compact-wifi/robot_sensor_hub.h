#ifndef BREAD_COMPACT_WIFI_ROBOT_SENSOR_HUB_H
#define BREAD_COMPACT_WIFI_ROBOT_SENSOR_HUB_H

// Cam bien an toan cho banh xe:
// - 2 cam bien va cham/vat can digital (LM393, cong tac): truoc + sau, chan chon tu WebUI.
// - 2 cam bien ToF VL6180X / VL53L0X (dia chi co dinh 0x29 nen moi bus 1 con):
//     ToF 1: bus OLED chinh (SDA41/SCL42), ToF 2: bus OLED phu (SDA21/SCL38).
//   Moi con chon rieng: gan truoc/sau, nhin thang (vat can) / chuc xuong (mep ban), nguong.
// Chan an toan: cai bo loc vao DualDcMotorController nen MOI nguon lenh (WebUI, giong noi,
// macro, Robot Life, RC) deu khong the chay vao huong dang bi chan.
// Thong so luu NVS (namespace "robot_sensor"), chinh tu WebUI, doi chan khong can reboot.
// File header-only, dat trong boards/bread-compact-wifi/ (khong can sua CMakeLists).

#include "board.h"
#include "display.h"
#include "dual_dc_motor_controller.h"
#include "rc_sbus_controller.h"
#include "settings.h"
#include "tof_sensor.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include <cJSON.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/idf_additions.h>

struct TofConfig {
    int en   = 0;
    int pos  = 0;   // 0 = gan phia truoc, 1 = gan phia sau
    int mode = 0;   // 0 = vat can (nhin thang), 1 = mep ban (gan chuc xuong)
    int mm   = 60;  // nguong (mm): vat can < nguong / mep ban > nguong
};

struct SensorConfig {
    static constexpr int kTofCount = 2;

    int front_en         = 0;   // cam bien va cham truoc
    int front_gpio       = 3;
    int front_active_low = 1;   // LM393: keo xuong 0 khi phat hien
    int back_en          = 0;   // cam bien va cham sau
    int back_gpio        = 18;
    int back_active_low  = 1;
    TofConfig tof[kTofCount] = {{0, 0, 0, 60}, {0, 1, 0, 60}};
    int action           = 1;   // 0 = chi dung, 1 = dung + chay nguoc ra xa (truoc -> lui, sau -> tien)
    int backoff_ms       = 250;
    int backoff_speed    = 60;
    int rc_bypass        = 0;   // 1 = tay cam RC duoc phep vuot qua cam bien
    int react_face       = 1;   // 1 = doi mat "giat minh" khi va cham
};

class RobotSensorHub {
public:
    static constexpr int kTofCount = SensorConfig::kTofCount;
    // Chan cho phep chon (da kiem tra trong tren board nay, xem config.h).
    static constexpr int kAllowedGpios[] = {3, 18, 20, 39, 40};

    RobotSensorHub(DualDcMotorController* wheels, i2c_master_bus_handle_t tof1_bus,
                   i2c_master_bus_handle_t tof2_bus)
        : wheels_(wheels) {
        tof_[0].bus = tof1_bus;
        tof_[1].bus = tof2_bus;
        LoadConfig();
        if (wheels_ != nullptr) {
            wheels_->SetSpeedFilter([this](int& left, int& right) { FilterSpeed(left, right); });
        }
    }

    ~RobotSensorHub() {
        if (task_ != nullptr) {
            vTaskDeleteWithCaps(task_);
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
        // Stack o PSRAM de tiet kiem RAM noi (task khong ghi flash).
        if (xTaskCreateWithCaps(&TaskEntry, "robot_sensor", 4096, this, 4, &task_, MALLOC_CAP_SPIRAM) != pdPASS) {
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
    // Khoa JSON cua ToF: slot 0 = "sns_tof_*" (giu tuong thich ban cu), slot 1 = "sns_tof2_*".
    static std::string TofKey(int i, const char* field) {
        return std::string(i == 0 ? "sns_tof_" : "sns_tof2_") + field;
    }

    void AppendConfigToJson(cJSON* root) {
        SensorConfig c = GetConfig();
        cJSON_AddNumberToObject(root, "sns_front_en", c.front_en);
        cJSON_AddNumberToObject(root, "sns_front_gpio", c.front_gpio);
        cJSON_AddNumberToObject(root, "sns_front_low", c.front_active_low);
        cJSON_AddNumberToObject(root, "sns_back_en", c.back_en);
        cJSON_AddNumberToObject(root, "sns_back_gpio", c.back_gpio);
        cJSON_AddNumberToObject(root, "sns_back_low", c.back_active_low);
        for (int i = 0; i < kTofCount; ++i) {
            cJSON_AddNumberToObject(root, TofKey(i, "en").c_str(), c.tof[i].en);
            cJSON_AddNumberToObject(root, TofKey(i, "pos").c_str(), c.tof[i].pos);
            cJSON_AddNumberToObject(root, TofKey(i, "mode").c_str(), c.tof[i].mode);
            cJSON_AddNumberToObject(root, TofKey(i, "mm").c_str(), c.tof[i].mm);
            cJSON_AddNumberToObject(root, TofKey(i, "bus").c_str(), tof_[i].bus != nullptr ? 1 : 0);
        }
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
        for (int i = 0; i < kTofCount; ++i) {
            get(TofKey(i, "en").c_str(), c.tof[i].en);
            get(TofKey(i, "pos").c_str(), c.tof[i].pos);
            get(TofKey(i, "mode").c_str(), c.tof[i].mode);
            get(TofKey(i, "mm").c_str(), c.tof[i].mm);
        }
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
        // Phuc vu hieu chinh: muc logic tho tren chan (-1 = tat), loai chip / ma loi ToF.
        cJSON_AddNumberToObject(root, "sns_front_raw", front_raw_.load());
        cJSON_AddNumberToObject(root, "sns_back_raw", back_raw_.load());
        for (int i = 0; i < kTofCount; ++i) {
            const TofSlot& t = tof_[i];
            cJSON_AddNumberToObject(root, TofKey(i, "ok").c_str(), t.ok.load() ? 1 : 0);
            cJSON_AddNumberToObject(root, TofKey(i, "mm_now").c_str(), t.mm_now.load());
            cJSON_AddNumberToObject(root, TofKey(i, "blk").c_str(), t.blocked_now.load() ? 1 : 0);
            cJSON_AddNumberToObject(root, TofKey(i, "chip").c_str(), t.chip.load());
            cJSON_AddNumberToObject(root, TofKey(i, "err").c_str(), t.err.load());
            cJSON_AddNumberToObject(root, TofKey(i, "id").c_str(), t.id.load());
            cJSON_AddNumberToObject(root, TofKey(i, "max").c_str(), t.max.load());
        }
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

    // Moi ToF: thiet bi + trang thai cua task + ket qua (atomic) cho WebUI.
    struct TofSlot {
        i2c_master_bus_handle_t bus = nullptr;
        TofSensor dev;
        // Chi task robot_sensor dung
        int bad_cnt = 0;
        int good_cnt = 0;
        bool blocked = false;
        uint32_t next_try_ms = 0;
        int last_logged_err = -1;
        uint32_t last_err_log_ms = 0;
        int last_sig = -1;
        int last_reported_mm = -2;
        // Doc tu task khac
        std::atomic<bool> ok{false};
        std::atomic<int> mm_now{TofSensor::kNoTarget};
        std::atomic<bool> blocked_now{false};
        std::atomic<int> chip{TofSensor::kChipNone};
        std::atomic<int> err{TofSensor::kErrNone};
        std::atomic<int> id{0};
        std::atomic<int> max{0};

        void Reset() {
            bad_cnt = good_cnt = 0;
            blocked = false;
            next_try_ms = 0;
        }
    };

    DualDcMotorController* wheels_ = nullptr;
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
    std::atomic<int> front_raw_{-1};
    std::atomic<int> back_raw_{-1};
    std::atomic<uint32_t> events_{0};
    std::atomic<uint32_t> stamp_{0};

    // Trang thai cua task
    TofSlot tof_[kTofCount];
    int front_pin_ = -1;
    int back_pin_ = -1;
    int front_cnt_ = 0;
    int back_cnt_ = 0;
    uint32_t last_face_ms_ = 0;

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
        for (auto& t : c.tof) {
            t.en = t.en ? 1 : 0;
            t.pos = t.pos ? 1 : 0;
            t.mode = t.mode ? 1 : 0;
            t.mm = std::clamp(t.mm, 10, 1000);  // VL6180X ~200 mm, VL53L0X ~1200 mm
        }
        c.action = c.action ? 1 : 0;
        c.backoff_ms = std::clamp(c.backoff_ms, 100, 1000);
        c.backoff_speed = std::clamp(c.backoff_speed, 30, 100);
        c.rc_bypass = c.rc_bypass ? 1 : 0;
        c.react_face = c.react_face ? 1 : 0;
    }

    // Khoa NVS: ToF 1 giu khoa cu ("tof_en"...), ToF 2 = "t2_en"...
    static std::string NvsKey(int i, const char* field) {
        return std::string(i == 0 ? "tof_" : "t2_") + field;
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
            for (int i = 0; i < kTofCount; ++i) {
                TofConfig& t = c.tof[i];
                t.en   = s.GetInt(NvsKey(i, "en"), t.en);
                t.pos  = s.GetInt(NvsKey(i, "pos"), t.pos);
                t.mode = s.GetInt(NvsKey(i, "mode"), t.mode);
                t.mm   = s.GetInt(NvsKey(i, "mm"), t.mm);
            }
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
        for (int i = 0; i < kTofCount; ++i) {
            const TofConfig& t = c.tof[i];
            s.SetInt(NvsKey(i, "en"), t.en);
            s.SetInt(NvsKey(i, "pos"), t.pos);
            s.SetInt(NvsKey(i, "mode"), t.mode);
            s.SetInt(NvsKey(i, "mm"), t.mm);
        }
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
        for (int i = 0; i < kTofCount; ++i) {
            TofSlot& t = tof_[i];
            if (!c.tof[i].en) {
                t.dev.Close();
                t.ok.store(false);
                t.mm_now.store(TofSensor::kNoTarget);
                t.chip.store(TofSensor::kChipNone);
                t.err.store(TofSensor::kErrNone);
                t.max.store(0);
                t.last_logged_err = -1;
            }
            t.Reset();
        }
        rc_bypass_.store(c.rc_bypass != 0);
    }

    static int ReadPinLevel(int pin) {
        return pin < 0 ? -1 : gpio_get_level(static_cast<gpio_num_t>(pin));
    }

    static bool LevelActive(int level, int active_low) {
        if (level < 0) return false;
        return active_low ? (level == 0) : (level == 1);
    }

    bool ReadPinActive(int pin, int active_low) {
        return LevelActive(ReadPinLevel(pin), active_low);
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

    // Log loi ToF khi ma loi doi, hoac nhac lai moi 60 s (tranh spam log moi 2 s).
    void LogTofError(int i, uint32_t now) {
        TofSlot& t = tof_[i];
        int err = t.dev.error();
        if (err == t.last_logged_err && (now - t.last_err_log_ms) < 60000) {
            return;
        }
        t.last_logged_err = err;
        t.last_err_log_ms = now;
        const char* where = i == 0 ? "ToF1 (SDA41/SCL42)" : "ToF2 (SDA21/SCL38)";
        switch (err) {
            case TofSensor::kErrNoAck:
                ESP_LOGW(TAG, "%s: khong co thiet bi tra loi o 0x29 (kiem tra day, nguon 3V3)", where);
                break;
            case TofSensor::kErrUnknownId:
                ESP_LOGW(TAG, "%s: 0x29 tra loi nhung khong phai VL6180X/VL53L0X (ID=0x%02X)", where,
                         t.dev.last_id());
                break;
            case TofSensor::kErrUnsupported:
                ESP_LOGW(TAG, "%s: chip %s chua duoc ho tro", where, TofSensor::ChipName(t.dev.chip()));
                break;
            case TofSensor::kErrInit:
                ESP_LOGW(TAG, "%s: khoi tao %s that bai", where, TofSensor::ChipName(t.dev.chip()));
                break;
            case TofSensor::kErrRead:
                ESP_LOGW(TAG, "%s: mat ket noi %s khi dang doc", where, TofSensor::ChipName(t.dev.chip()));
                break;
            default:
                break;
        }
    }

    void UpdateTof(int i, const SensorConfig& c, uint32_t now) {
        TofSlot& t = tof_[i];
        const TofConfig& tc = c.tof[i];
        if (!tc.en) {
            t.blocked = false;
            return;
        }
        if (t.bus == nullptr) {
            t.ok.store(false);
            t.err.store(TofSensor::kErrNoAck);
            t.blocked = true;  // bat ma khong co bus -> an toan: chan
            return;
        }
        if (!t.dev.IsOpen()) {
            if (now >= t.next_try_ms) {
                t.next_try_ms = now + 2000;
                if (t.dev.Open(t.bus)) {
                    t.last_logged_err = -1;
                    t.max.store(t.dev.MaxRangeMm());
                } else {
                    LogTofError(i, now);
                }
                t.chip.store(t.dev.chip());
                t.err.store(t.dev.error());
                t.id.store(t.dev.last_id());
            }
            if (!t.dev.IsOpen()) {
                t.ok.store(false);
                t.blocked = true;  // bat cam bien ma khong doc duoc -> an toan: chan
                return;
            }
        }
        int mm = TofSensor::kNoTarget;
        bool fresh = false;
        if (!t.dev.ReadRangeMm(mm, fresh)) {
            LogTofError(i, now);
            t.err.store(t.dev.error());
            t.dev.Close();  // thu mo lai o vong sau
            t.ok.store(false);
            t.blocked = true;
            return;
        }
        t.ok.store(true);
        if (!fresh) {
            return;  // VL53L0X chua co mau moi: giu nguyen ket qua cu
        }
        t.mm_now.store(mm);
        bool bad;
        if (tc.mode == 0) {
            bad = (mm != TofSensor::kNoTarget) && (mm < tc.mm);  // vat can gan
        } else {
            bad = (mm == TofSensor::kNoTarget) || (mm > tc.mm);  // mat mat ban
        }
        if (bad) {
            t.good_cnt = 0;
            if (++t.bad_cnt >= kTofDebounce) t.blocked = true;
        } else {
            t.bad_cnt = 0;
            if (++t.good_cnt >= kTofDebounce) t.blocked = false;
        }
    }

    // ToF i dang chan phia truoc (front=true) hoac phia sau.
    bool TofBlocksSide(int i, const SensorConfig& c, bool front) const {
        return c.tof[i].en && tof_[i].blocked && ((c.tof[i].pos == 0) == front);
    }

    // Chay nguoc ra khoi vat can (truoc bi chan -> lui, sau bi chan -> tien);
    // dung som neu phia dang chay toi cung bi chan (cam bien digital hoac ToF gan phia do).
    void BackOff(bool forward_hit, const SensorConfig& c) {
        int speed = forward_hit ? -c.backoff_speed : c.backoff_speed;
        bool ahead_front = !forward_hit;  // phia robot sap chay toi
        for (int i = 0; i < kTofCount; ++i) {
            if (TofBlocksSide(i, c, ahead_front)) {
                return;
            }
        }
        wheels_->SetSpeed(speed, speed);
        uint32_t tick = 0;
        for (uint32_t t = 0; t < static_cast<uint32_t>(c.backoff_ms); t += kLoopMs, ++tick) {
            vTaskDelay(pdMS_TO_TICKS(kLoopMs));
            bool other_side = forward_hit ? ReadPinActive(back_pin_, c.back_active_low)
                                          : ReadPinActive(front_pin_, c.front_active_low);
            int i = tick % kTofCount;  // moi ToF doc xen ke 40 ms
            if (c.tof[i].en && (c.tof[i].pos == 0) == ahead_front) {
                UpdateTof(i, c, NowMs());
                other_side = other_side || tof_[i].blocked;
            }
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
        uint32_t tick = 0;
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(kLoopMs));
            ++tick;
            if (config_dirty_.exchange(false)) {
                c = GetConfig();
                ApplyPins(c);
            }
            uint32_t now = NowMs();
            rc_override_.store(rc_sbus::IsOverrideActive());

            int flevel = ReadPinLevel(front_pin_);
            int blevel = ReadPinLevel(back_pin_);
            bool fhit = Debounce(LevelActive(flevel, c.front_active_low), front_hit_.load(), front_cnt_);
            bool bhit = Debounce(LevelActive(blevel, c.back_active_low), back_hit_.load(), back_cnt_);
            front_hit_.store(fhit);
            back_hit_.store(bhit);
            bool changed = (front_raw_.exchange(flevel) != flevel) | (back_raw_.exchange(blevel) != blevel);

            // Moi vong doc 1 ToF, xen ke -> moi ToF 40 ms/lan.
            UpdateTof(tick % kTofCount, c, now);

            bool front = fhit;
            bool back = bhit;
            for (int i = 0; i < kTofCount; ++i) {
                TofSlot& t = tof_[i];
                front = front || TofBlocksSide(i, c, true);
                back = back || TofBlocksSide(i, c, false);
                bool blk = c.tof[i].en && t.blocked;
                changed |= (t.blocked_now.exchange(blk) != blk);
                int sig = t.err.load() * 64 + t.chip.load() * 2 + (t.ok.load() ? 1 : 0);
                int mm = t.mm_now.load();
                if (sig != t.last_sig || mm != t.last_reported_mm) {
                    t.last_sig = sig;
                    t.last_reported_mm = mm;
                    changed = true;
                }
            }
            bool was_front = front_blocked_.exchange(front);
            bool was_back = back_blocked_.exchange(back);

            if (front != was_front || back != was_back || changed) {
                stamp_.fetch_add(1);
            }
            if (front && !was_front) OnBlocked(true, c);
            if (back && !was_back) OnBlocked(false, c);
        }
    }
};

#endif  // BREAD_COMPACT_WIFI_ROBOT_SENSOR_HUB_H
