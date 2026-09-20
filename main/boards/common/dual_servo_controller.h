#ifndef __DUAL_SERVO_CONTROLLER_H__
#define __DUAL_SERVO_CONTROLLER_H__

#include "mcp_server.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

class DualServoController {
private:
    struct ServoStep {
        int pan = 90;
        int tilt = 90;
        int duration_ms = 300;
    };

    static constexpr const char* TAG = "DualServo";

    gpio_num_t pan_gpio_ = GPIO_NUM_NC;
    gpio_num_t tilt_gpio_ = GPIO_NUM_NC;
    bool ready_ = false;

    ledc_mode_t speed_mode_ = LEDC_LOW_SPEED_MODE;
    ledc_timer_t timer_ = LEDC_TIMER_1;
    ledc_timer_bit_t duty_resolution_ = LEDC_TIMER_14_BIT;
    ledc_channel_t pan_channel_ = LEDC_CHANNEL_2;
    ledc_channel_t tilt_channel_ = LEDC_CHANNEL_3;

    int min_angle_ = 0;
    int max_angle_ = 180;
    int center_pan_ = 90;
    int center_tilt_ = 90;
    int default_step_ms_ = 280;
    int emotion_cooldown_ms_ = 1400;

    std::string config_path_;
    std::string role_name_;
    std::string tool_prefix_;
    bool accept_emotion_events_ = false;

    std::mutex mutex_;
    std::unordered_map<std::string, std::vector<ServoStep>> actions_;
    std::unordered_map<std::string, std::string> emotion_map_;

    std::atomic<uint32_t> generation_{0};
    std::atomic<int64_t> last_emotion_us_{0};

    static DualServoController* emotion_target_instance_;

    std::string BuildToolName(const char* suffix) const {
        if (suffix == nullptr || *suffix == '\0') {
            return tool_prefix_;
        }
        return tool_prefix_ + "." + suffix;
    }

    static std::string ToLowerCopy(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return s;
    }

    static int GetIntOrDefault(const cJSON* obj, const char* key, int default_val) {
        if (obj == nullptr || key == nullptr) {
            return default_val;
        }
        auto* item = cJSON_GetObjectItem(obj, key);
        if (cJSON_IsNumber(item)) {
            return item->valueint;
        }
        return default_val;
    }

    static int GetRandomInt(int min_value, int max_value) {
        if (max_value <= min_value) {
            return min_value;
        }
        uint32_t value = esp_random();
        return min_value + static_cast<int>(value % static_cast<uint32_t>(max_value - min_value + 1));
    }

    void LoadBuiltInDefaultsLocked() {
        actions_.clear();
        emotion_map_.clear();

        // Các động tác mặc định (không cần JSON vẫn chạy được)
        actions_["center"] = {
            {center_pan_, center_tilt_, 220}
        };
        actions_["nodyes"] = {
            {center_pan_, center_tilt_ + 14, 170},
            {center_pan_, center_tilt_ - 14, 170},
            {center_pan_, center_tilt_ + 12, 170},
            {center_pan_, center_tilt_ - 12, 170},
            {center_pan_, center_tilt_, 160}
        };
        actions_["shakeno"] = {
            {center_pan_ + 24, center_tilt_, 150},
            {center_pan_ - 24, center_tilt_, 150},
            {center_pan_ + 24, center_tilt_, 150},
            {center_pan_ - 24, center_tilt_, 150},
            {center_pan_, center_tilt_, 150}
        };
        actions_["curiousheadtilt"] = {
            {center_pan_ + 10, center_tilt_ + 10, 250},
            {center_pan_ + 14, center_tilt_ + 14, 280},
            {center_pan_, center_tilt_, 220}
        };
        actions_["step_back"] = {
            {center_pan_ + 32, center_tilt_ - 18, 180},
            {center_pan_ + 42, center_tilt_ - 26, 180},
            {center_pan_, center_tilt_, 180}
        };

        // Backward-compatible aliases
        actions_["nod"] = actions_["nodyes"];
        actions_["shake"] = actions_["shakeno"];
        actions_["curious_head_tilt"] = actions_["curiousheadtilt"];

        emotion_map_["surprised"] = "step_back";
        emotion_map_["happy"] = "nodyes";
        emotion_map_["angry"] = "shakeno";
        emotion_map_["curious"] = "curiousheadtilt";
        emotion_map_["neutral"] = "center";
    }

    std::vector<ServoStep> BuildTalkingMotionLocked() const {
        std::vector<ServoStep> steps;
        int total_steps = GetRandomInt(5, 8);
        steps.reserve(static_cast<size_t>(total_steps + 1));

        for (int i = 0; i < total_steps; ++i) {
            int pan = center_pan_ + GetRandomInt(-10, 10);
            int tilt = center_tilt_ + GetRandomInt(-8, 8);
            int duration = GetRandomInt(120, 240);
            steps.push_back({pan, tilt, duration});
        }
        steps.push_back({center_pan_, center_tilt_, 160});
        return steps;
    }

    bool ReadFileToString(const std::string& path, std::string& out) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) {
            return false;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size <= 0) {
            fclose(f);
            return false;
        }
        out.resize(static_cast<size_t>(size));
        size_t n = fread(out.data(), 1, out.size(), f);
        fclose(f);
        if (n != out.size()) {
            out.clear();
            return false;
        }
        return true;
    }

    void ParseAndMergeJsonLocked(const cJSON* root) {
        if (!cJSON_IsObject(root)) {
            return;
        }

        auto* defaults = cJSON_GetObjectItem(root, "defaults");
        if (cJSON_IsObject(defaults)) {
            min_angle_ = GetIntOrDefault(defaults, "min_angle", min_angle_);
            max_angle_ = GetIntOrDefault(defaults, "max_angle", max_angle_);
            center_pan_ = GetIntOrDefault(defaults, "center_pan", GetIntOrDefault(defaults, "center_left", center_pan_));
            center_tilt_ = GetIntOrDefault(defaults, "center_tilt", GetIntOrDefault(defaults, "center_right", center_tilt_));
            default_step_ms_ = GetIntOrDefault(defaults, "default_step_ms", default_step_ms_);
            emotion_cooldown_ms_ = GetIntOrDefault(defaults, "emotion_cooldown_ms", emotion_cooldown_ms_);
            if (min_angle_ > max_angle_) {
                std::swap(min_angle_, max_angle_);
            }
        }

        auto* emotions = cJSON_GetObjectItem(root, "emotions");
        if (cJSON_IsObject(emotions)) {
            for (auto* child = emotions->child; child != nullptr; child = child->next) {
                if (child->string != nullptr && cJSON_IsString(child) && child->valuestring != nullptr) {
                    emotion_map_[ToLowerCopy(child->string)] = ToLowerCopy(child->valuestring);
                }
            }
        }

        auto* actions = cJSON_GetObjectItem(root, "actions");
        if (cJSON_IsObject(actions)) {
            for (auto* action = actions->child; action != nullptr; action = action->next) {
                if (action->string == nullptr || !cJSON_IsArray(action)) {
                    continue;
                }
                std::vector<ServoStep> steps;
                int count = cJSON_GetArraySize(action);
                for (int i = 0; i < count; ++i) {
                    auto* step = cJSON_GetArrayItem(action, i);
                    if (!cJSON_IsObject(step)) {
                        continue;
                    }
                    ServoStep item;
                    item.pan = GetIntOrDefault(step, "pan", GetIntOrDefault(step, "left", center_pan_));
                    item.tilt = GetIntOrDefault(step, "tilt", GetIntOrDefault(step, "right", center_tilt_));
                    item.duration_ms = GetIntOrDefault(step, "duration_ms", default_step_ms_);
                    if (item.duration_ms < 10) {
                        item.duration_ms = 10;
                    }
                    steps.push_back(item);
                }
                if (!steps.empty()) {
                    actions_[ToLowerCopy(action->string)] = std::move(steps);
                }
            }
        }
    }

    uint32_t AngleToDuty(int angle) const {
        int clamped = std::clamp(angle, min_angle_, max_angle_);
        constexpr int kPulseMinUs = 500;
        constexpr int kPulseMaxUs = 2500;
        constexpr int kPeriodUs = 20000; // 50Hz
        int pulse_us = kPulseMinUs + (kPulseMaxUs - kPulseMinUs) * (clamped - min_angle_) / std::max(1, (max_angle_ - min_angle_));
        uint32_t duty_max = (1u << duty_resolution_) - 1u;
        return static_cast<uint32_t>((static_cast<uint64_t>(pulse_us) * duty_max) / kPeriodUs);
    }

    void WriteAnglesInternal(int pan, int tilt) {
        if (!ready_) {
            return;
        }
        uint32_t pan_duty = AngleToDuty(pan);
        uint32_t tilt_duty = AngleToDuty(tilt);

        ledc_set_duty(speed_mode_, pan_channel_, pan_duty);
        ledc_update_duty(speed_mode_, pan_channel_);
        ledc_set_duty(speed_mode_, tilt_channel_, tilt_duty);
        ledc_update_duty(speed_mode_, tilt_channel_);
    }

    struct ActionTaskArg {
        DualServoController* self = nullptr;
        std::string action_name;
        std::vector<ServoStep> steps;
        uint32_t generation = 0;
    };

    static void ActionTaskEntry(void* arg) {
        std::unique_ptr<ActionTaskArg> holder(static_cast<ActionTaskArg*>(arg));
        if (!holder || holder->self == nullptr) {
            vTaskDelete(nullptr);
            return;
        }

        auto* self = holder->self;
        ESP_LOGI(TAG, "Run servo action: %s (%d steps)", holder->action_name.c_str(), static_cast<int>(holder->steps.size()));

        for (const auto& step : holder->steps) {
            if (holder->generation != self->generation_.load()) {
                ESP_LOGI(TAG, "Servo action aborted: %s", holder->action_name.c_str());
                vTaskDelete(nullptr);
                return;
            }
            self->WriteAnglesInternal(step.pan, step.tilt);
            vTaskDelay(pdMS_TO_TICKS(step.duration_ms));
        }

        vTaskDelete(nullptr);
    }

    bool LaunchActionLocked(const std::string& action_name, const std::vector<ServoStep>& steps) {
        if (steps.empty()) {
            return false;
        }

        auto* task_arg = new (std::nothrow) ActionTaskArg();
        if (!task_arg) {
            return false;
        }

        task_arg->self = this;
        task_arg->action_name = action_name;
        task_arg->steps = steps;
        task_arg->generation = ++generation_;

        BaseType_t ok = xTaskCreate(&DualServoController::ActionTaskEntry, "servo_action", 4096, task_arg, 4, nullptr);
        if (ok != pdPASS) {
            delete task_arg;
            return false;
        }
        return true;
    }

    bool ExecuteActionLocked(const std::string& action_name) {
        auto normalized = ToLowerCopy(action_name);
        if (normalized == "talkingmotion" || normalized == "talking_motion" || normalized == "talking") {
            return LaunchActionLocked("talkingMotion", BuildTalkingMotionLocked());
        }

        auto it = actions_.find(normalized);
        if (it == actions_.end() || it->second.empty()) {
            return false;
        }
        return LaunchActionLocked(action_name, it->second);
    }

    void RegisterMcpTools() {
        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool(BuildToolName("get_state"),
            "Get current dual-servo capability and runtime action list.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                cJSON* json = cJSON_CreateObject();
                cJSON_AddBoolToObject(json, "ready", ready_);
                cJSON_AddNumberToObject(json, "pan_gpio", static_cast<int>(pan_gpio_));
                cJSON_AddNumberToObject(json, "tilt_gpio", static_cast<int>(tilt_gpio_));
                cJSON_AddStringToObject(json, "config_path", config_path_.c_str());
            cJSON_AddStringToObject(json, "role", role_name_.c_str());
            cJSON_AddStringToObject(json, "tool_prefix", tool_prefix_.c_str());
            cJSON_AddBoolToObject(json, "accept_emotion_events", accept_emotion_events_);

                cJSON* actions = cJSON_CreateArray();
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    for (const auto& kv : actions_) {
                        cJSON_AddItemToArray(actions, cJSON_CreateString(kv.first.c_str()));
                    }
                }
                cJSON_AddItemToObject(json, "actions", actions);
                return json;
            });

        mcp_server.AddTool(BuildToolName("nod_yes"),
            "nodYes(): Gật đầu nhẹ nhàng 2 lần thể hiện đồng ý/lắng nghe.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                if (!ready_) {
                    return std::string("Dual servo is not ready. Check pins and power.");
                }
                return nodYes() ? std::string("OK") : std::string("Failed");
            });

        mcp_server.AddTool(BuildToolName("shake_no"),
            "shakeNo(): Lắc đầu qua lại 2 lần thể hiện từ chối/không đồng ý.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                if (!ready_) {
                    return std::string("Dual servo is not ready. Check pins and power.");
                }
                return shakeNo() ? std::string("OK") : std::string("Failed");
            });

        mcp_server.AddTool(BuildToolName("talking_motion"),
            "talkingMotion(): Chuỗi gật/lắc nhẹ ngẫu nhiên khi robot đang nói.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                if (!ready_) {
                    return std::string("Dual servo is not ready. Check pins and power.");
                }
                return talkingMotion() ? std::string("OK") : std::string("Failed");
            });

        mcp_server.AddTool(BuildToolName("curious_head_tilt"),
            "curiousHeadTilt(): Nghiêng nhẹ cổ thể hiện tò mò/thắc mắc.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                if (!ready_) {
                    return std::string("Dual servo is not ready. Check pins and power.");
                }
                return curiousHeadTilt() ? std::string("OK") : std::string("Failed");
            });

        mcp_server.AddTool(BuildToolName("set_angles"),
            "Set servo angles immediately. Useful for precise control. Range: 0..180.",
            PropertyList({
                Property("pan", kPropertyTypeInteger, 0, 180),
                Property("tilt", kPropertyTypeInteger, 0, 180)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                if (!ready_) {
                    return std::string("Dual servo is not ready. Check pins and power.");
                }
                int pan = properties["pan"].value<int>();
                int tilt = properties["tilt"].value<int>();
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    ++generation_;
                    WriteAnglesInternal(pan, tilt);
                }
                return std::string("OK");
            });

        mcp_server.AddTool(BuildToolName("run_action"),
            "Run predefined pan-tilt actions by name: nodYes, shakeNo, talkingMotion, curiousHeadTilt, step_back, center.",
            PropertyList({
                Property("name", kPropertyTypeString)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                if (!ready_) {
                    return std::string("Dual servo is not ready. Check pins and power.");
                }
                auto name = properties["name"].value<std::string>();
                std::lock_guard<std::mutex> lock(mutex_);
                if (!ExecuteActionLocked(name)) {
                    return std::string("Action not found. Call self.robot.dual_servo.get_state to list available actions.");
                }
                return std::string("Running action: ") + name;
            });

        mcp_server.AddTool(BuildToolName("stop"),
            "Stop current dual-servo action and keep current angle.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                ++generation_;
                return std::string("Stopped");
            });

        mcp_server.AddTool(BuildToolName("reload_config"),
            "Reload dual-servo actions from SD JSON file. Optional `path`.",
            PropertyList({
                Property("path", kPropertyTypeString, std::string(""))
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                auto path = properties["path"].value<std::string>();
                bool ok = ReloadConfig(path.empty() ? config_path_ : path);
                if (!ok) {
                    return std::string("Reload failed. Keep defaults. Ensure SD and JSON path are correct.");
                }
                return std::string("Reloaded servo config successfully.");
            });
    }

public:
    DualServoController(const std::string& role_name,
                        const std::string& tool_prefix,
                        gpio_num_t pan_gpio,
                        gpio_num_t tilt_gpio,
                        const std::string& config_path = "/sdcard/robot/servo_actions.json",
                        bool accept_emotion_events = false)
        : pan_gpio_(pan_gpio),
          tilt_gpio_(tilt_gpio),
          config_path_(config_path),
          role_name_(role_name),
          tool_prefix_(tool_prefix),
          accept_emotion_events_(accept_emotion_events) {
        if (tool_prefix_.empty()) {
            tool_prefix_ = "self.robot.dual_servo";
        }

        if (accept_emotion_events_) {
            emotion_target_instance_ = this;
        }

        if (pan_gpio_ == GPIO_NUM_NC || tilt_gpio_ == GPIO_NUM_NC) {
            ESP_LOGW(TAG, "Dual servo disabled: invalid GPIO");
            RegisterMcpTools();
            return;
        }

        ledc_timer_config_t timer_cfg = {};
        timer_cfg.speed_mode = speed_mode_;
        timer_cfg.duty_resolution = duty_resolution_;
        timer_cfg.timer_num = timer_;
        timer_cfg.freq_hz = 50;
        timer_cfg.clk_cfg = LEDC_AUTO_CLK;

        if (ledc_timer_config(&timer_cfg) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init LEDC timer for servos");
            RegisterMcpTools();
            return;
        }

        ledc_channel_config_t pan_cfg = {};
        pan_cfg.gpio_num = pan_gpio_;
        pan_cfg.speed_mode = speed_mode_;
        pan_cfg.channel = pan_channel_;
        pan_cfg.timer_sel = timer_;
        pan_cfg.duty = 0;
        pan_cfg.hpoint = 0;

        ledc_channel_config_t tilt_cfg = pan_cfg;
        tilt_cfg.gpio_num = tilt_gpio_;
        tilt_cfg.channel = tilt_channel_;

        if (ledc_channel_config(&pan_cfg) != ESP_OK || ledc_channel_config(&tilt_cfg) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init LEDC channels for servos");
            RegisterMcpTools();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            LoadBuiltInDefaultsLocked();
            WriteAnglesInternal(center_pan_, center_tilt_);
        }

        ready_ = true;
        ESP_LOGI(TAG, "Dual servo ready: pan=%d tilt=%d", static_cast<int>(pan_gpio_), static_cast<int>(tilt_gpio_));

        // Load external JSON if available; fallback defaults otherwise.
        ReloadConfig(config_path_);
        RegisterMcpTools();
    }

    bool ReloadConfig(const std::string& path) {
        std::string json_text;
        if (!ReadFileToString(path, json_text)) {
            ESP_LOGW(TAG, "Servo JSON not found: %s (using built-in defaults)", path.c_str());
            return false;
        }

        cJSON* root = cJSON_Parse(json_text.c_str());
        if (!root) {
            ESP_LOGE(TAG, "Invalid servo JSON at: %s", path.c_str());
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            LoadBuiltInDefaultsLocked();
            ParseAndMergeJsonLocked(root);
        }

        cJSON_Delete(root);
        ESP_LOGI(TAG, "Servo JSON loaded: %s", path.c_str());
        return true;
    }

    bool RunAction(const std::string& name) {
        if (!ready_) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        return ExecuteActionLocked(name);
    }

    bool nodYes() {
        return RunAction("nodyes");
    }

    bool shakeNo() {
        return RunAction("shakeno");
    }

    bool talkingMotion() {
        return RunAction("talkingmotion");
    }

    bool curiousHeadTilt() {
        return RunAction("curiousheadtilt");
    }

    void NotifyEmotionInternal(const std::string& emotion) {
        if (!ready_) {
            return;
        }

        std::string mapped_action;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = emotion_map_.find(ToLowerCopy(emotion));
            if (it == emotion_map_.end()) {
                return;
            }
            mapped_action = it->second;
        }

        int64_t now_us = esp_timer_get_time();
        int64_t prev = last_emotion_us_.load();
        if ((now_us - prev) < static_cast<int64_t>(emotion_cooldown_ms_) * 1000LL) {
            return;
        }
        last_emotion_us_.store(now_us);

        RunAction(mapped_action);
    }

    static void NotifyEmotion(const std::string& emotion) {
        if (emotion_target_instance_ == nullptr) {
            return;
        }
        emotion_target_instance_->NotifyEmotionInternal(emotion);
    }
};

inline DualServoController* DualServoController::emotion_target_instance_ = nullptr;

#endif // __DUAL_SERVO_CONTROLLER_H__
