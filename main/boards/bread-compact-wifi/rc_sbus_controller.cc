#include "rc_sbus_controller.h"

#include "config.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace rc_sbus {
namespace {

constexpr const char* TAG = "RcSbus";
constexpr uint8_t kSbusHeader = 0x0F;
constexpr int kSbusFrameLen = 25;
constexpr int kSbusMin = 172;
constexpr int kSbusMid = 992;
constexpr int kSbusMax = 1811;

class RcSbusController {
public:
    void Init(DualDcMotorController* wheel_motor) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) {
            return;
        }
        wheel_motor_ = wheel_motor;
        if (wheel_motor_ == nullptr) {
            ESP_LOGW(TAG, "Wheel motor controller is null, RC SBUS disabled");
            return;
        }

        uart_config_t cfg = {};
        cfg.baud_rate = RC_SBUS_BAUDRATE;
        cfg.data_bits = UART_DATA_8_BITS;
        cfg.parity = UART_PARITY_EVEN;
        cfg.stop_bits = UART_STOP_BITS_2;
        cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        cfg.source_clk = UART_SCLK_DEFAULT;

        ESP_ERROR_CHECK(uart_param_config(RC_SBUS_UART_PORT, &cfg));

        int tx_pin = (RC_SBUS_TX_PIN == GPIO_NUM_NC) ? UART_PIN_NO_CHANGE : static_cast<int>(RC_SBUS_TX_PIN);
        int rx_pin = static_cast<int>(RC_SBUS_RX_PIN);
        ESP_ERROR_CHECK(uart_set_pin(RC_SBUS_UART_PORT, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        ESP_ERROR_CHECK(uart_set_line_inverse(RC_SBUS_UART_PORT, UART_SIGNAL_RXD_INV));
        ESP_ERROR_CHECK(uart_driver_install(RC_SBUS_UART_PORT, 1024, 0, 0, nullptr, 0));

        state_.initialized = true;
        state_.last_frame_ms = NowMs();

        BaseType_t ok = xTaskCreatePinnedToCore(
            &RcSbusController::TaskEntry,
            "rc_sbus_task",
            4096,
            this,
            8,
            nullptr,
            tskNO_AFFINITY);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "Failed to create rc_sbus_task");
            return;
        }

        initialized_ = true;
        ESP_LOGI(TAG,
                 "SBUS ready: uart=%d rx=%d baud=%d deadzone=%d%% timeout=%dms",
                 static_cast<int>(RC_SBUS_UART_PORT),
                 static_cast<int>(RC_SBUS_RX_PIN),
                 RC_SBUS_BAUDRATE,
                 RC_SBUS_DEADZONE_PERCENT,
                 RC_SBUS_FRAME_TIMEOUT_MS);
    }

    bool IsOverrideActive() {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_.override_active;
    }

    bool GetState(State& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        out = state_;
        return state_.initialized;
    }

private:
    static void TaskEntry(void* arg) {
        auto* self = static_cast<RcSbusController*>(arg);
        self->TaskLoop();
    }

    static uint32_t NowMs() {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    }

    static int ClampInt(int v, int lo, int hi) {
        return std::max(lo, std::min(hi, v));
    }

    static int16_t MapSbusTo255(int raw) {
        raw = ClampInt(raw, kSbusMin, kSbusMax);

        float n = 0.0f;
        if (raw >= kSbusMid) {
            n = static_cast<float>(raw - kSbusMid) / static_cast<float>(kSbusMax - kSbusMid);
        } else {
            n = static_cast<float>(raw - kSbusMid) / static_cast<float>(kSbusMid - kSbusMin);
        }

        const float dz = static_cast<float>(RC_SBUS_DEADZONE_PERCENT) / 100.0f;
        if (std::fabs(n) < dz) {
            return 0;
        }

        float sign = (n >= 0.0f) ? 1.0f : -1.0f;
        float a = (std::fabs(n) - dz) / (1.0f - dz);
        a = std::max(0.0f, std::min(1.0f, a));
        int out = static_cast<int>(std::lround(sign * a * 255.0f));
        return static_cast<int16_t>(ClampInt(out, -255, 255));
    }

    static int Speed255To100(int v255) {
        v255 = ClampInt(v255, -255, 255);
        int scaled = static_cast<int>(std::lround((static_cast<float>(v255) / 255.0f) * 100.0f));
        return ClampInt(scaled, -100, 100);
    }

    static bool DecodeFrame(const uint8_t frame[kSbusFrameLen], int16_t ch[16], bool& frame_lost, bool& failsafe) {
        if (frame[0] != kSbusHeader) {
            return false;
        }

        ch[0]  = ((frame[1]     | frame[2] << 8) & 0x07FF);
        ch[1]  = ((frame[2] >> 3 | frame[3] << 5) & 0x07FF);
        ch[2]  = ((frame[3] >> 6 | frame[4] << 2 | frame[5] << 10) & 0x07FF);
        ch[3]  = ((frame[5] >> 1 | frame[6] << 7) & 0x07FF);
        ch[4]  = ((frame[6] >> 4 | frame[7] << 4) & 0x07FF);
        ch[5]  = ((frame[7] >> 7 | frame[8] << 1 | frame[9] << 9) & 0x07FF);
        ch[6]  = ((frame[9] >> 2 | frame[10] << 6) & 0x07FF);
        ch[7]  = ((frame[10] >> 5 | frame[11] << 3) & 0x07FF);
        ch[8]  = ((frame[12]    | frame[13] << 8) & 0x07FF);
        ch[9]  = ((frame[13] >> 3 | frame[14] << 5) & 0x07FF);
        ch[10] = ((frame[14] >> 6 | frame[15] << 2 | frame[16] << 10) & 0x07FF);
        ch[11] = ((frame[16] >> 1 | frame[17] << 7) & 0x07FF);
        ch[12] = ((frame[17] >> 4 | frame[18] << 4) & 0x07FF);
        ch[13] = ((frame[18] >> 7 | frame[19] << 1 | frame[20] << 9) & 0x07FF);
        ch[14] = ((frame[20] >> 2 | frame[21] << 6) & 0x07FF);
        ch[15] = ((frame[21] >> 5 | frame[22] << 3) & 0x07FF);

        uint8_t flags = frame[23];
        frame_lost = (flags & (1U << 2)) != 0;
        failsafe = (flags & (1U << 3)) != 0;
        return true;
    }

    void StopMotors() {
        if (wheel_motor_ != nullptr) {
            wheel_motor_->Stop();
        }
    }

    void TaskLoop() {
        uint8_t bytes[64];
        uint8_t frame[kSbusFrameLen];
        int frame_index = 0;
        bool collecting = false;
        bool last_override = false;

        while (true) {
            int n = uart_read_bytes(RC_SBUS_UART_PORT, bytes, sizeof(bytes), pdMS_TO_TICKS(20));
            uint32_t t_now = NowMs();

            for (int i = 0; i < n; ++i) {
                uint8_t b = bytes[i];

                if (!collecting) {
                    if (b == kSbusHeader) {
                        frame[0] = b;
                        frame_index = 1;
                        collecting = true;
                    }
                    continue;
                }

                frame[frame_index++] = b;
                if (frame_index < kSbusFrameLen) {
                    continue;
                }

                collecting = false;
                frame_index = 0;

                int16_t channels[16] = {0};
                bool frame_lost = false;
                bool failsafe = false;
                if (!DecodeFrame(frame, channels, frame_lost, failsafe)) {
                    continue;
                }

                int16_t steering = MapSbusTo255(channels[0]);
                int16_t throttle = MapSbusTo255(channels[1]);
                int16_t left = static_cast<int16_t>(ClampInt(static_cast<int>(throttle) + static_cast<int>(steering), -255, 255));
                int16_t right = static_cast<int16_t>(ClampInt(static_cast<int>(throttle) - static_cast<int>(steering), -255, 255));

                bool valid = !frame_lost && !failsafe;
                bool override_active = valid && (std::abs(throttle) > 0 || std::abs(steering) > 0);

                if (!valid) {
                    throttle = 0;
                    steering = 0;
                    left = 0;
                    right = 0;
                    StopMotors();
                } else if (override_active) {
                    int left100 = Speed255To100(left);
                    int right100 = Speed255To100(right);
                    if (wheel_motor_ != nullptr) {
                        wheel_motor_->SetSpeed(left100, right100);
                    }
                } else if (last_override) {
                    // Stick quay ve trung tam: dung banh xe roi nha uu tien cho kenh web/voice.
                    StopMotors();
                }

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    std::memcpy(state_.channels_raw, channels, sizeof(channels));
                    state_.link_ok = valid;
                    state_.frame_lost = frame_lost;
                    state_.failsafe = failsafe;
                    state_.override_active = override_active;
                    state_.throttle_255 = throttle;
                    state_.steering_255 = steering;
                    state_.left_255 = left;
                    state_.right_255 = right;
                    state_.last_frame_ms = t_now;
                }

                last_override = override_active;
            }

            bool timeout = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                uint32_t dt = t_now - state_.last_frame_ms;
                timeout = (dt > static_cast<uint32_t>(RC_SBUS_FRAME_TIMEOUT_MS));
                if (timeout) {
                    state_.link_ok = false;
                    state_.frame_lost = true;
                    state_.failsafe = true;
                    state_.override_active = false;
                    state_.throttle_255 = 0;
                    state_.steering_255 = 0;
                    state_.left_255 = 0;
                    state_.right_255 = 0;
                }
            }

            if (timeout) {
                StopMotors();
                last_override = false;
            }
        }
    }

    std::mutex mutex_;
    State state_{};
    DualDcMotorController* wheel_motor_ = nullptr;
    bool initialized_ = false;
};

RcSbusController g_controller;

}  // namespace

void Init(DualDcMotorController* wheel_motor) {
    g_controller.Init(wheel_motor);
}

bool IsOverrideActive() {
    return g_controller.IsOverrideActive();
}

bool GetState(State& out) {
    return g_controller.GetState(out);
}

}  // namespace rc_sbus
