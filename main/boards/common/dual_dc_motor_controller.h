#ifndef __DUAL_DC_MOTOR_CONTROLLER_H__
#define __DUAL_DC_MOTOR_CONTROLLER_H__

#include "mcp_server.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_log.h>

class DualDcMotorController {
private:
    static constexpr const char* TAG = "DualDcMotor";

    gpio_num_t left_in1_gpio_ = GPIO_NUM_NC;
    gpio_num_t left_in2_gpio_ = GPIO_NUM_NC;
    gpio_num_t right_in1_gpio_ = GPIO_NUM_NC;
    gpio_num_t right_in2_gpio_ = GPIO_NUM_NC;

    bool left_invert_ = false;
    bool right_invert_ = false;
    bool ready_ = false;

    int left_speed_ = 0;   // -100..100
    int right_speed_ = 0;  // -100..100

    ledc_mode_t speed_mode_ = LEDC_LOW_SPEED_MODE;
    ledc_timer_t timer_ = LEDC_TIMER_2;
    ledc_timer_bit_t duty_resolution_ = LEDC_TIMER_10_BIT;
    ledc_channel_t left_in1_channel_ = LEDC_CHANNEL_4;
    ledc_channel_t left_in2_channel_ = LEDC_CHANNEL_5;
    ledc_channel_t right_in1_channel_ = LEDC_CHANNEL_6;
    ledc_channel_t right_in2_channel_ = LEDC_CHANNEL_7;

    std::vector<std::string> tool_prefixes_;
    std::mutex mutex_;

    static int ClampSpeed(int speed) {
        return std::clamp(speed, -100, 100);
    }

    uint32_t SpeedToDuty(int speed) const {
        int abs_speed = std::abs(ClampSpeed(speed));
        uint32_t duty_max = (1u << duty_resolution_) - 1u;
        return static_cast<uint32_t>((static_cast<uint64_t>(abs_speed) * duty_max) / 100u);
    }

    std::string BuildToolName(const std::string& prefix, const char* suffix) const {
        if (suffix == nullptr || *suffix == '\0') {
            return prefix;
        }
        return prefix + "." + suffix;
    }

    void ApplyMotorStateLocked() {
        if (!ready_) {
            return;
        }

        int left = ClampSpeed(left_speed_);
        int right = ClampSpeed(right_speed_);

        if (left_invert_) {
            left = -left;
        }
        if (right_invert_) {
            right = -right;
        }

        // Left motor: IN1/IN2
        uint32_t left_in1_duty = 0;
        uint32_t left_in2_duty = 0;
        if (left > 0) {
            left_in1_duty = SpeedToDuty(left);
            left_in2_duty = 0;
        } else if (left < 0) {
            left_in1_duty = 0;
            left_in2_duty = SpeedToDuty(left);
        }

        // Right motor: IN1/IN2 (mapped to IN3/IN4 on common modules)
        uint32_t right_in1_duty = 0;
        uint32_t right_in2_duty = 0;
        if (right > 0) {
            right_in1_duty = SpeedToDuty(right);
            right_in2_duty = 0;
        } else if (right < 0) {
            right_in1_duty = 0;
            right_in2_duty = SpeedToDuty(right);
        }

        ledc_set_duty(speed_mode_, left_in1_channel_, left_in1_duty);
        ledc_update_duty(speed_mode_, left_in1_channel_);
        ledc_set_duty(speed_mode_, left_in2_channel_, left_in2_duty);
        ledc_update_duty(speed_mode_, left_in2_channel_);

        ledc_set_duty(speed_mode_, right_in1_channel_, right_in1_duty);
        ledc_update_duty(speed_mode_, right_in1_channel_);
        ledc_set_duty(speed_mode_, right_in2_channel_, right_in2_duty);
        ledc_update_duty(speed_mode_, right_in2_channel_);
    }

    void RegisterToolsForPrefix(const std::string& prefix) {
        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool(BuildToolName(prefix, "get_state"),
            "Get dual DC wheel motor state.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                cJSON* json = cJSON_CreateObject();
                cJSON_AddBoolToObject(json, "ready", ready_);
                cJSON_AddNumberToObject(json, "left_in1_gpio", static_cast<int>(left_in1_gpio_));
                cJSON_AddNumberToObject(json, "left_in2_gpio", static_cast<int>(left_in2_gpio_));
                cJSON_AddNumberToObject(json, "right_in1_gpio", static_cast<int>(right_in1_gpio_));
                cJSON_AddNumberToObject(json, "right_in2_gpio", static_cast<int>(right_in2_gpio_));

                std::lock_guard<std::mutex> lock(mutex_);
                cJSON_AddNumberToObject(json, "left_speed", left_speed_);
                cJSON_AddNumberToObject(json, "right_speed", right_speed_);
                return json;
            });

        mcp_server.AddTool(BuildToolName(prefix, "set_speed"),
            "Set wheel speeds directly. Range: -100..100. Positive = forward, negative = backward.",
            PropertyList({
                Property("left", kPropertyTypeInteger, -100, 100),
                Property("right", kPropertyTypeInteger, -100, 100)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                if (!ready_) {
                    return std::string("Dual DC motors are not ready. Check GPIO config and driver wiring.");
                }
                std::lock_guard<std::mutex> lock(mutex_);
                left_speed_ = ClampSpeed(properties["left"].value<int>());
                right_speed_ = ClampSpeed(properties["right"].value<int>());
                ApplyMotorStateLocked();
                return std::string("OK");
            });

        mcp_server.AddTool(BuildToolName(prefix, "forward"),
            "Move robot forward with both wheel motors.",
            PropertyList({
                Property("speed", kPropertyTypeInteger, 0, 100)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                if (!ready_) {
                    return std::string("Dual DC motors are not ready. Check GPIO config and driver wiring.");
                }
                int speed = std::clamp(properties["speed"].value<int>(), 0, 100);
                std::lock_guard<std::mutex> lock(mutex_);
                left_speed_ = speed;
                right_speed_ = speed;
                ApplyMotorStateLocked();
                return std::string("OK");
            });

        mcp_server.AddTool(BuildToolName(prefix, "backward"),
            "Move robot backward with both wheel motors.",
            PropertyList({
                Property("speed", kPropertyTypeInteger, 0, 100)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                if (!ready_) {
                    return std::string("Dual DC motors are not ready. Check GPIO config and driver wiring.");
                }
                int speed = std::clamp(properties["speed"].value<int>(), 0, 100);
                std::lock_guard<std::mutex> lock(mutex_);
                left_speed_ = -speed;
                right_speed_ = -speed;
                ApplyMotorStateLocked();
                return std::string("OK");
            });

        mcp_server.AddTool(BuildToolName(prefix, "turn_left"),
            "Turn robot left in place.",
            PropertyList({
                Property("speed", kPropertyTypeInteger, 0, 100)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                if (!ready_) {
                    return std::string("Dual DC motors are not ready. Check GPIO config and driver wiring.");
                }
                int speed = std::clamp(properties["speed"].value<int>(), 0, 100);
                std::lock_guard<std::mutex> lock(mutex_);
                left_speed_ = -speed;
                right_speed_ = speed;
                ApplyMotorStateLocked();
                return std::string("OK");
            });

        mcp_server.AddTool(BuildToolName(prefix, "turn_right"),
            "Turn robot right in place.",
            PropertyList({
                Property("speed", kPropertyTypeInteger, 0, 100)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                if (!ready_) {
                    return std::string("Dual DC motors are not ready. Check GPIO config and driver wiring.");
                }
                int speed = std::clamp(properties["speed"].value<int>(), 0, 100);
                std::lock_guard<std::mutex> lock(mutex_);
                left_speed_ = speed;
                right_speed_ = -speed;
                ApplyMotorStateLocked();
                return std::string("OK");
            });

        mcp_server.AddTool(BuildToolName(prefix, "stop"),
            "Stop both wheel motors immediately.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                left_speed_ = 0;
                right_speed_ = 0;
                ApplyMotorStateLocked();
                return std::string("Stopped");
            });
    }

public:
        DualDcMotorController(gpio_num_t left_in1_gpio,
                                                    gpio_num_t left_in2_gpio,
                                                    gpio_num_t right_in1_gpio,
                                                    gpio_num_t right_in2_gpio,
                          std::vector<std::string> tool_prefixes,
                          bool left_invert = false,
                          bool right_invert = false)
                : left_in1_gpio_(left_in1_gpio),
                    left_in2_gpio_(left_in2_gpio),
                    right_in1_gpio_(right_in1_gpio),
                    right_in2_gpio_(right_in2_gpio),
          left_invert_(left_invert),
          right_invert_(right_invert),
          tool_prefixes_(std::move(tool_prefixes)) {
        if (tool_prefixes_.empty()) {
            tool_prefixes_.push_back("self.robot.wheels");
        }

        if (left_in1_gpio_ == GPIO_NUM_NC ||
            left_in2_gpio_ == GPIO_NUM_NC ||
            right_in1_gpio_ == GPIO_NUM_NC ||
            right_in2_gpio_ == GPIO_NUM_NC) {
            ESP_LOGW(TAG, "Dual DC motor disabled: invalid GPIO");
            for (const auto& prefix : tool_prefixes_) {
                RegisterToolsForPrefix(prefix);
            }
            return;
        }

        ledc_timer_config_t timer_cfg = {};
        timer_cfg.speed_mode = speed_mode_;
        timer_cfg.duty_resolution = duty_resolution_;
        timer_cfg.timer_num = timer_;
        timer_cfg.freq_hz = 20000;
        timer_cfg.clk_cfg = LEDC_AUTO_CLK;

        if (ledc_timer_config(&timer_cfg) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init LEDC timer for dual DC motor");
            for (const auto& prefix : tool_prefixes_) {
                RegisterToolsForPrefix(prefix);
            }
            return;
        }

        ledc_channel_config_t left_in1_cfg = {};
        left_in1_cfg.gpio_num = left_in1_gpio_;
        left_in1_cfg.speed_mode = speed_mode_;
        left_in1_cfg.channel = left_in1_channel_;
        left_in1_cfg.timer_sel = timer_;
        left_in1_cfg.duty = 0;
        left_in1_cfg.hpoint = 0;

        ledc_channel_config_t left_in2_cfg = left_in1_cfg;
        left_in2_cfg.gpio_num = left_in2_gpio_;
        left_in2_cfg.channel = left_in2_channel_;

        ledc_channel_config_t right_in1_cfg = left_in1_cfg;
        right_in1_cfg.gpio_num = right_in1_gpio_;
        right_in1_cfg.channel = right_in1_channel_;

        ledc_channel_config_t right_in2_cfg = left_in1_cfg;
        right_in2_cfg.gpio_num = right_in2_gpio_;
        right_in2_cfg.channel = right_in2_channel_;

        if (ledc_channel_config(&left_in1_cfg) != ESP_OK ||
            ledc_channel_config(&left_in2_cfg) != ESP_OK ||
            ledc_channel_config(&right_in1_cfg) != ESP_OK ||
            ledc_channel_config(&right_in2_cfg) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init LEDC channels for dual DC motor");
            for (const auto& prefix : tool_prefixes_) {
                RegisterToolsForPrefix(prefix);
            }
            return;
        }

        ready_ = true;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ApplyMotorStateLocked();
        }

        ESP_LOGI(TAG,
                 "Dual DC motor ready: L_IN1=%d L_IN2=%d R_IN1=%d R_IN2=%d",
                 static_cast<int>(left_in1_gpio_),
                 static_cast<int>(left_in2_gpio_),
                 static_cast<int>(right_in1_gpio_),
                 static_cast<int>(right_in2_gpio_));

        for (const auto& prefix : tool_prefixes_) {
            RegisterToolsForPrefix(prefix);
        }
    }
};

#endif // __DUAL_DC_MOTOR_CONTROLLER_H__
