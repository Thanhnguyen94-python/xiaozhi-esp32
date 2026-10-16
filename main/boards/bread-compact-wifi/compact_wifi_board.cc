#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "display/dual_oled_display.h"
#include "display/oled_custom_emoji_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "dual_servo_controller.h"
#include "dual_dc_motor_controller.h"
#include "rc_sbus_controller.h"
#include "robot_web_ui_server.h"
#include "robot_control_hub.h"
#include "robot_life_behavior.h"
#include "hotplug_oled_panel.h"
#include "robot_sensor_hub.h"
#include "robot_hardware.h"
#include "led/single_led.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdspi_host.h>
#include <dirent.h>

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
    HotplugOledPanel* main_hotplug_ = nullptr;  // OLED chinh cam/rut nong khong treo LVGL

#ifdef CONFIG_BREAD_COMPACT_WIFI_DUAL_OLED
    i2c_master_bus_handle_t display2_i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t panel2_io_ = nullptr;
    esp_lcd_panel_handle_t panel2_ = nullptr;
    OledCustomEmojiDisplay* face_oled_ = nullptr;
    OledDisplay* text_oled_ = nullptr;
    HotplugOledPanel* text_hotplug_ = nullptr;
#endif
    Button boot_button_;
    Button touch_button_;
    // Nut VOL+/VOL- (GPIO40/39) da bo: am luong chinh tren WebUI. 2 chan nay de trong.
    bool sdcard_mounted_ = false;
    bool sd_bus_ready_ = false;
    sdmmc_card_t* sd_card_ = nullptr;
    int sd_cs_ = SDCARD_SPI_CS;
    int sd_freq_khz_ = SDCARD_SPI_MAX_FREQ_KHZ;
    TaskHandle_t sd_monitor_task_ = nullptr;
    DualDcMotorController* wheel_motor_ = nullptr;
    DualServoController* head_servo_ = nullptr;
    RobotWebUiServer* web_ui_server_ = nullptr;
    RobotControlHub* control_hub_ = nullptr;
    RobotLifeBehavior* life_behavior_ = nullptr;
    RobotSensorHub* sensor_hub_ = nullptr;
    RobotHardware* hardware_ = nullptr;

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
        // Do dia chi truoc: thieu man chinh thi chay NoDisplay, khong abort/reboot lien tuc.
        if (i2c_master_probe(display_i2c_bus_, 0x3C, 100) != ESP_OK) {
            ESP_LOGE(TAG, "Khong thay OLED chinh o 0x3C (SDA=%d SCL=%d) -> NoDisplay",
                     DISPLAY_SDA_PIN, DISPLAY_SCL_PIN);
            display_ = new NoDisplay();
            return;
        }

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

        main_hotplug_ = new HotplugOledPanel("OLED chinh", display_i2c_bus_, 0x3C, panel_);
#ifdef CONFIG_BREAD_COMPACT_WIFI_DUAL_OLED
        // Màn chính giờ chỉ hiển thị face, sẽ ghép với màn text trong DualOledDisplay
        face_oled_ = new OledCustomEmojiDisplay(panel_io_, main_hotplug_->handle(), DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                        DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        display_ = face_oled_;  // tạm, sẽ ghi đè bằng DualOledDisplay
        main_hotplug_->Start(face_oled_->GetLvDisplay());
#else
        auto* oled = new OledDisplay(panel_io_, main_hotplug_->handle(), DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, OledRole::kFull);
        display_ = oled;
        main_hotplug_->Start(oled->GetLvDisplay());
#endif
    }

#ifdef CONFIG_BREAD_COMPACT_WIFI_DUAL_OLED
    // ==================================================================
    // Khởi tạo màn 0.91" trên bus I2C riêng (SDA=21, SCL=38)
    // ==================================================================
    bool InitializeDisplay2I2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = DISPLAY2_I2C_PORT,
            .sda_io_num = DISPLAY2_SDA_PIN,
            .scl_io_num = DISPLAY2_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        esp_err_t err = i2c_new_master_bus(&bus_config, &display2_i2c_bus_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Khong tao duoc bus I2C man phu: %s", esp_err_to_name(err));
            display2_i2c_bus_ = nullptr;
            return false;
        }
        return true;
    }

    // Thu hoi tai nguyen man phu khi khoi tao that bai giua chung.
    void ReleaseSecondDisplay() {
        if (panel2_ != nullptr) { esp_lcd_panel_del(panel2_); panel2_ = nullptr; }
        if (panel2_io_ != nullptr) { esp_lcd_panel_io_del(panel2_io_); panel2_io_ = nullptr; }
        if (display2_i2c_bus_ != nullptr) { i2c_del_master_bus(display2_i2c_bus_); display2_i2c_bus_ = nullptr; }
    }

    // Man phu la tuy chon: neu khong co/loi thi giu man chinh (face) va chay binh thuong.
    void InitializeSecondSsd1306Display() {
        if (face_oled_ == nullptr) {
            ESP_LOGW(TAG, "Man chinh khong san sang, bo qua man phu");
            return;
        }
        if (!InitializeDisplay2I2c()) {
            return;
        }
        if (i2c_master_probe(display2_i2c_bus_, DISPLAY2_ADDR, 100) != ESP_OK) {
            ESP_LOGW(TAG, "Khong thay OLED phu o 0x%02X (SDA=%d SCL=%d) -> chi dung man chinh",
                     DISPLAY2_ADDR, DISPLAY2_SDA_PIN, DISPLAY2_SCL_PIN);
            ReleaseSecondDisplay();
            return;
        }

        esp_lcd_panel_io_i2c_config_t io_config = {};
        io_config.dev_addr = DISPLAY2_ADDR;
        io_config.on_color_trans_done = nullptr;
        io_config.user_ctx = nullptr;
        io_config.control_phase_bytes = 1;
        io_config.dc_bit_offset = 6;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        io_config.flags.dc_low_on_data = 0;
        io_config.flags.disable_control_phase = 0;
        io_config.scl_speed_hz = 400 * 1000;

        if (esp_lcd_new_panel_io_i2c(display2_i2c_bus_, &io_config, &panel2_io_) != ESP_OK) {
            ESP_LOGE(TAG, "Tao panel IO man phu that bai");
            ReleaseSecondDisplay();
            return;
        }

        ESP_LOGI(TAG, "Install second SSD1306 driver (0.91\")");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY2_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

        // Man phu 0.91" luon la SSD1306 (khong theo SH1106 cua man chinh).
        if (esp_lcd_new_panel_ssd1306(panel2_io_, &panel_config, &panel2_) != ESP_OK ||
            esp_lcd_panel_reset(panel2_) != ESP_OK ||
            esp_lcd_panel_init(panel2_) != ESP_OK ||
            esp_lcd_panel_invert_color(panel2_, false) != ESP_OK ||
            esp_lcd_panel_disp_on_off(panel2_, true) != ESP_OK) {
            ESP_LOGE(TAG, "Khoi tao OLED phu that bai -> chi dung man chinh");
            ReleaseSecondDisplay();
            return;
        }

        text_hotplug_ = new HotplugOledPanel("OLED phu", display2_i2c_bus_, DISPLAY2_ADDR, panel2_);
        text_oled_ = new OledDisplay(panel2_io_, text_hotplug_->handle(), DISPLAY2_WIDTH, DISPLAY2_HEIGHT,
                                     DISPLAY2_MIRROR_X, DISPLAY2_MIRROR_Y, OledRole::kTextOnly);
        text_hotplug_->Start(text_oled_->GetLvDisplay());

        ESP_LOGI(TAG, "Wrapping face + text into DualOledDisplay");
        display_ = new DualOledDisplay(face_oled_, text_oled_);
    }
#endif  // CONFIG_BREAD_COMPACT_WIFI_DUAL_OLED
    

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                // ⬇️ Bọc trong Schedule để chạy trên main task, không phải esp_timer task
                app.Schedule([this]() { EnterWifiConfigMode(); });
                return;
            }
            app.ToggleChatState();
        });
        touch_button_.OnPressDown([this]() {
            static int64_t s_last_edge_us = 0;    // lan cham gan nhat (ke ca lan bi loc)
            static int64_t s_first_touch_us = 0;  // lan cham dau tien cua mot cap
            constexpr int64_t kMinGapUs = 250000;   // 2 lan cham phai cach nhau >= 250 ms (loc nhieu/doi)
            constexpr int64_t kWindowUs = 1500000;  // lan cham thu 2 phai nam trong 1.5 giay

            int64_t now_us = esp_timer_get_time();
            if ((now_us - s_last_edge_us) < kMinGapUs) {
                return;
            }
            s_last_edge_us = now_us;

            if (s_first_touch_us == 0 || (now_us - s_first_touch_us) > kWindowUs) {
                s_first_touch_us = now_us;  // day la lan cham thu 1, cho lan thu 2
                return;
            }

            s_first_touch_us = 0;  // da du 2 lan cham hop le
            Application::GetInstance().ToggleChatState();
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

        sd_bus_ready_ = true;

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
        // GPIO21 chi duoc thu lam CS du phong khi KHONG dung man phu (21 = SDA cua OLED phu).
#ifdef CONFIG_BREAD_COMPACT_WIFI_DUAL_OLED
        const int cs_candidates[] = { static_cast<int>(SDCARD_SPI_CS) };
#else
        const int cs_candidates[] = { static_cast<int>(SDCARD_SPI_CS), 21 };
#endif
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
                    sd_card_ = card;
                    sd_cs_ = cs;
                    sd_freq_khz_ = freq_khz;
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

    // Mount lai the SD voi CS/tan so da biet, tat log loi de khong spam khi chua cam the.
    bool RemountSdQuiet() {
        static const char* kTags[] = {"vfs_fat_sdmmc", "sdmmc_common", "sdmmc_init", "sdmmc_sd",
                                      "sdspi_host", "sdspi_transaction", "diskio_sdmmc"};
        constexpr size_t kTagCount = sizeof(kTags) / sizeof(kTags[0]);
        esp_log_level_t saved[kTagCount];
        for (size_t i = 0; i < kTagCount; ++i) {
            saved[i] = esp_log_level_get(kTags[i]);
            esp_log_level_set(kTags[i], ESP_LOG_NONE);
        }

        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.max_freq_khz = sd_freq_khz_;
        sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot_cfg.host_id = (spi_host_device_t)host.slot;
        slot_cfg.gpio_cs = static_cast<gpio_num_t>(sd_cs_);
        esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 0,
            .disk_status_check_enable = true,
        };
        sdmmc_card_t* card = nullptr;
        esp_err_t ret = esp_vfs_fat_sdspi_mount(SDCARD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &card);

        for (size_t i = 0; i < kTagCount; ++i) {
            esp_log_level_set(kTags[i], saved[i]);
        }
        if (ret != ESP_OK) {
            return false;
        }
        sd_card_ = card;
        return true;
    }

    // Theo doi the SD: rut ra -> unmount + bao WebUI; cam lai -> tu mount lai.
    static void SdMonitorTaskEntry(void* arg) {
        static_cast<CompactWifiBoard*>(arg)->SdMonitorLoop();
    }

    void SdMonitorLoop() {
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            if (sd_card_ != nullptr) {
                // opendir di qua FatFs -> kiem tra trang thai the (disk_status_check_enable).
                DIR* dir = opendir(SDCARD_MOUNT_POINT);
                if (dir != nullptr) {
                    closedir(dir);
                    continue;
                }
                ESP_LOGW(TAG, "The SD da bi rut ra -> unmount");
                esp_vfs_fat_sdcard_unmount(SDCARD_MOUNT_POINT, sd_card_);
                sd_card_ = nullptr;
                sdcard_mounted_ = false;
                hardware_->SetSdcardPresent(false);
            } else if (sd_bus_ready_ && RemountSdQuiet()) {
                ESP_LOGI(TAG, "Da nhan lai the SD (CS=%d, %dkHz)", sd_cs_, sd_freq_khz_);
                sdcard_mounted_ = true;
                hardware_->SetSdcardPresent(true);
            }
        }
    }

    // 物联网初始化，逐步迁移到 MCP 协议
    void InitializeTools() {
        if (hardware_ == nullptr) {
            hardware_ = new RobotHardware();
#ifdef CONFIG_BREAD_COMPACT_WIFI_DUAL_OLED
            hardware_->SetDetected(sdcard_mounted_, text_hotplug_ != nullptr);
#else
            hardware_->SetDetected(sdcard_mounted_, false);
#endif
        }
        if (sd_monitor_task_ == nullptr) {
            xTaskCreate(&SdMonitorTaskEntry, "sd_monitor", 4096, this, 1, &sd_monitor_task_);
        }

        // Wheel movement using 2 DC motors.
        // Use dedicated namespace only to avoid affecting existing servo control flows.
        if (wheel_motor_ == nullptr) {
            wheel_motor_ = new DualDcMotorController(
                WHEEL_MOTOR_LEFT_IN1_GPIO,
                WHEEL_MOTOR_LEFT_IN2_GPIO,
                WHEEL_MOTOR_RIGHT_IN1_GPIO,
                WHEEL_MOTOR_RIGHT_IN2_GPIO,
                {"self.robot.wheels"});

            // RC override via MC7RE SBUS (MC6C transmitter). Tat tren WebUI -> khong khoi dong.
            if (hardware_->RcEnabled()) {
                rc_sbus::Init(wheel_motor_);
            } else {
                ESP_LOGI(TAG, "Tay cam RC dang tat (WebUI > Cai dat > Phan cung)");
            }
        }

        // Dedicated head pan-tilt tools and emotion binding.
        if (head_servo_ == nullptr) {
            head_servo_ = new DualServoController(
                "head",
                "self.robot.head_servo",
                HEAD_SERVO_PAN_GPIO,
                HEAD_SERVO_TILT_GPIO,
                "/sdcard/robot/head_servo_actions.json",
                true);
        }

        // Cam bien an toan: VL6180X dung chung bus I2C voi OLED chinh (41/42).
        if (sensor_hub_ == nullptr) {
            sensor_hub_ = new RobotSensorHub(wheel_motor_, display_i2c_bus_);
            sensor_hub_->Start();
        }

        if (web_ui_server_ == nullptr) {
            web_ui_server_ = new RobotWebUiServer(wheel_motor_, head_servo_);
            web_ui_server_->SetSensorHub(sensor_hub_);
            web_ui_server_->SetHardware(hardware_);
        }

        if (control_hub_ == nullptr) {
            control_hub_ = new RobotControlHub(wheel_motor_, head_servo_, GetDisplay());
            control_hub_->RegisterTools();
        }

        if (life_behavior_ == nullptr) {
            life_behavior_ = new RobotLifeBehavior(wheel_motor_, head_servo_);
            life_behavior_->SetSensorHub(sensor_hub_);
            life_behavior_->SetHardware(hardware_);
            if (web_ui_server_ != nullptr) {
                web_ui_server_->SetLifeBehavior(life_behavior_);
            }
        }
    }

    void InitializeRobotWebUi() {
        if (web_ui_server_ == nullptr) {
            return;
        }
        if (!web_ui_server_->Start(80)) {
            ESP_LOGE(TAG, "Failed to start local Robot Web UI");
            return;
        }

        if (control_hub_ != nullptr) {
            control_hub_->ShowWebUiAccessOnDisplay();
        }
    }

public:
        CompactWifiBoard() :
        boot_button_(BOOT_BUTTON_GPIO),
        touch_button_(TOUCH_BUTTON_GPIO) {
        InitializeDisplayI2c();
        InitializeSsd1306Display();
#ifdef CONFIG_BREAD_COMPACT_WIFI_DUAL_OLED
        // Tu do man phu: khong co man phu thi van chay voi man chinh, khong reboot.
        InitializeSecondSsd1306Display();
#endif

        InitializeSdCard();
        InitializeButtons();
        InitializeTools();
    }

        virtual ~CompactWifiBoard() {
        delete life_behavior_;
        life_behavior_ = nullptr;

        delete sensor_hub_;
        sensor_hub_ = nullptr;

        delete control_hub_;
        control_hub_ = nullptr;

        delete web_ui_server_;
        web_ui_server_ = nullptr;

        delete head_servo_;
        head_servo_ = nullptr;

        delete wheel_motor_;
        wheel_motor_ = nullptr;

#ifdef CONFIG_BREAD_COMPACT_WIFI_DUAL_OLED
        // DualOledDisplay sẽ tự delete face_oled_ và text_oled_
        delete display_;
        display_ = nullptr;
        face_oled_ = nullptr;
        text_oled_ = nullptr;

        if (panel2_ != nullptr) { esp_lcd_panel_del(panel2_); panel2_ = nullptr; }
        if (panel2_io_ != nullptr) { esp_lcd_panel_io_del(panel2_io_); panel2_io_ = nullptr; }
        if (display2_i2c_bus_ != nullptr) { i2c_del_master_bus(display2_i2c_bus_); display2_i2c_bus_ = nullptr; }
#endif
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

    void StartNetwork() override {
        WifiBoard::StartNetwork();
        vTaskDelay(pdMS_TO_TICKS(1000));
        InitializeRobotWebUi();
        if (life_behavior_ != nullptr) {
            life_behavior_->Start();
        }
    }
};

DECLARE_BOARD(CompactWifiBoard);
