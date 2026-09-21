#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_custom_emoji_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "dual_servo_controller.h"
#include "dual_dc_motor_controller.h"
#include "led/single_led.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdspi_host.h>

#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#define TAG "CompactWifiBoard"

class CompactWifiBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    Button touch_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    bool sdcard_mounted_ = false;

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        // SSD1306 config
        esp_lcd_panel_io_i2c_config_t io_config = {};
        io_config.dev_addr = 0x3C;
        io_config.on_color_trans_done = nullptr;
        io_config.user_ctx = nullptr;
        io_config.control_phase_bytes = 1;
        io_config.dc_bit_offset = 6;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        io_config.flags.dc_low_on_data = 0;
        io_config.flags.disable_control_phase = 0;
        io_config.scl_speed_hz = 400 * 1000;

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
#endif
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // Reset the display
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledCustomEmojiDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
        touch_button_.OnPressDown([this]() {
            Application::GetInstance().StartListening();
        });
        touch_button_.OnPressUp([this]() {
            Application::GetInstance().StopListening();
        });

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void InitializeSdCard() {
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.max_freq_khz = SDCARD_SPI_MAX_FREQ_KHZ;

        spi_bus_config_t bus_cfg = {
            .mosi_io_num = SDCARD_SPI_MOSI,
            .miso_io_num = SDCARD_SPI_MISO,
            .sclk_io_num = SDCARD_SPI_SCLK,
            .quadwp_io_num = GPIO_NUM_NC,
            .quadhd_io_num = GPIO_NUM_NC,
            .max_transfer_sz = 4000,
        };

        esp_err_t ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Failed to initialize SD SPI bus: %s", esp_err_to_name(ret));
            return;
        }

        // SDSPI often needs pull-up on lines (especially MISO and CS).
        gpio_set_pull_mode(SDCARD_SPI_MISO, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode(SDCARD_SPI_MOSI, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode(SDCARD_SPI_SCLK, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode(SDCARD_SPI_CS, GPIO_PULLUP_ONLY);

        esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 0,
            .disk_status_check_enable = true,
        };

        // Try a few combinations to avoid common timeout issues caused by CS pin mismatch
        // or unstable wiring at high SPI frequency.
        const int cs_candidates[] = { static_cast<int>(SDCARD_SPI_CS), 21 };
        const int freq_candidates[] = { SDCARD_SPI_MAX_FREQ_KHZ, 1000, 400 };
        sdmmc_card_t* card = nullptr;

        for (int cs : cs_candidates) {
            for (int freq_khz : freq_candidates) {
                host.max_freq_khz = freq_khz;

                sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
                slot_cfg.host_id = (spi_host_device_t)host.slot;
                slot_cfg.gpio_cs = static_cast<gpio_num_t>(cs);

                gpio_set_pull_mode(static_cast<gpio_num_t>(cs), GPIO_PULLUP_ONLY);
                ESP_LOGI(TAG, "Try mount SD: CS=%d, freq=%dkHz", cs, freq_khz);

                ret = esp_vfs_fat_sdspi_mount(SDCARD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &card);
                if (ret == ESP_OK) {
                    sdcard_mounted_ = true;
                    sdmmc_card_print_info(stdout, card);
                    ESP_LOGI(TAG, "SD card mounted at %s (CS=%d, freq=%dkHz)", SDCARD_MOUNT_POINT, cs, freq_khz);
                    return;
                }

                ESP_LOGW(TAG, "Mount failed (CS=%d, freq=%dkHz): %s", cs, freq_khz, esp_err_to_name(ret));
                vTaskDelay(pdMS_TO_TICKS(30));
            }
        }

        ESP_LOGW(TAG, "Failed to mount SD card at %s after retries", SDCARD_MOUNT_POINT);
        ESP_LOGW(TAG, "Check wiring: MISO=%d MOSI=%d SCLK=%d CS=%d, 3.3V power, FAT32 format",
                 SDCARD_SPI_MISO, SDCARD_SPI_MOSI, SDCARD_SPI_SCLK, SDCARD_SPI_CS);
    }

    // 物联网初始化，逐步迁移到 MCP 协议
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
        // Wheel movement using 2 DC motors.
        // Exposes both new namespace `self.robot.wheels.*`
        // and compatibility namespace `self.robot.dual_servo.*`.
        static DualDcMotorController wheel_motor(
            WHEEL_MOTOR_LEFT_IN1_GPIO,
            WHEEL_MOTOR_LEFT_IN2_GPIO,
            WHEEL_MOTOR_RIGHT_IN1_GPIO,
            WHEEL_MOTOR_RIGHT_IN2_GPIO,
            {"self.robot.wheels", "self.robot.dual_servo"});

        // Dedicated head pan-tilt tools and emotion binding.
        static DualServoController head_servo(
            "head",
            "self.robot.head_servo",
            HEAD_SERVO_PAN_GPIO,
            HEAD_SERVO_TILT_GPIO,
            "/sdcard/robot/head_servo_actions.json",
            true);

        (void)wheel_motor;
        (void)head_servo;
    }

public:
    CompactWifiBoard() :
        boot_button_(BOOT_BUTTON_GPIO),
        touch_button_(TOUCH_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeSdCard();
        InitializeButtons();
        InitializeTools();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(CompactWifiBoard);
