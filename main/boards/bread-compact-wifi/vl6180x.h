#ifndef BREAD_COMPACT_WIFI_VL6180X_H
#define BREAD_COMPACT_WIFI_VL6180X_H

// Driver toi gian cho cam bien ToF VL6180X (module TOF050C), dia chi I2C 0x29.
// Do khoang cach single-shot 0..~200 mm. Dung chung bus I2C voi OLED chinh.
// Trinh tu init theo ST AN4545 (giong thu vien Adafruit/Pololu).

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>

class Vl6180x {
public:
    static constexpr uint16_t kAddr = 0x29;
    static constexpr int kNoTarget = -1;  // khong thay vat (xa hon tam do)

    ~Vl6180x() { Close(); }

    // Gan vao bus va khoi tao. Tra false neu khong thay cam bien.
    bool Open(i2c_master_bus_handle_t bus) {
        Close();
        if (bus == nullptr || i2c_master_probe(bus, kAddr, 50) != ESP_OK) {
            return false;
        }
        i2c_device_config_t cfg = {};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address = kAddr;
        cfg.scl_speed_hz = 400 * 1000;
        if (i2c_master_bus_add_device(bus, &cfg, &dev_) != ESP_OK) {
            dev_ = nullptr;
            return false;
        }
        uint8_t model = 0;
        if (!Read8(0x000, model) || model != 0xB4) {
            ESP_LOGW(TAG, "Thiet bi 0x29 khong phai VL6180X (model=0x%02X)", model);
            Close();
            return false;
        }
        if (!LoadSettings()) {
            ESP_LOGW(TAG, "Init VL6180X that bai");
            Close();
            return false;
        }
        return true;
    }

    void Close() {
        if (dev_ != nullptr) {
            i2c_master_bus_rm_device(dev_);
            dev_ = nullptr;
        }
    }

    bool IsOpen() const { return dev_ != nullptr; }

    // Do 1 lan. Tra true neu giao tiep OK; mm = khoang cach hoac kNoTarget.
    bool ReadRangeMm(int& mm) {
        if (dev_ == nullptr || !Write8(0x018, 0x01)) {  // SYSRANGE__START single-shot
            return false;
        }
        uint8_t status = 0;
        for (int i = 0; i < 25; ++i) {  // toi da ~50 ms
            if (!Read8(0x04F, status)) {  // RESULT__INTERRUPT_STATUS_GPIO
                return false;
            }
            if ((status & 0x07) == 0x04) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        if ((status & 0x07) != 0x04) {
            return false;
        }
        uint8_t range = 0;
        uint8_t range_status = 0;
        bool ok = Read8(0x062, range) && Read8(0x04D, range_status);  // RESULT__RANGE_VAL / STATUS
        Write8(0x015, 0x07);  // SYSTEM__INTERRUPT_CLEAR
        if (!ok) {
            return false;
        }
        // Ma loi khac 0 (vd 11 = khong co muc tieu / tran tam do) -> coi nhu khong thay vat.
        mm = ((range_status >> 4) == 0) ? range : kNoTarget;
        return true;
    }

private:
    static constexpr const char* TAG = "VL6180X";
    i2c_master_dev_handle_t dev_ = nullptr;

    bool Write8(uint16_t reg, uint8_t val) {
        uint8_t buf[3] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF), val};
        return i2c_master_transmit(dev_, buf, sizeof(buf), 50) == ESP_OK;
    }

    bool Read8(uint16_t reg, uint8_t& val) {
        uint8_t addr[2] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF)};
        return i2c_master_transmit_receive(dev_, addr, sizeof(addr), &val, 1, 50) == ESP_OK;
    }

    bool LoadSettings() {
        uint8_t fresh = 0;
        if (!Read8(0x016, fresh)) {  // SYSTEM__FRESH_OUT_OF_RESET
            return false;
        }
        if (fresh == 1) {
            // Thanh ghi private bat buoc (ST AN4545)
            static const uint16_t kPrivate[][2] = {
                {0x0207, 0x01}, {0x0208, 0x01}, {0x0096, 0x00}, {0x0097, 0xFD},
                {0x00E3, 0x00}, {0x00E4, 0x04}, {0x00E5, 0x02}, {0x00E6, 0x01},
                {0x00E7, 0x03}, {0x00F5, 0x02}, {0x00D9, 0x05}, {0x00DB, 0xCE},
                {0x00DC, 0x03}, {0x00DD, 0xF8}, {0x009F, 0x00}, {0x00A3, 0x3C},
                {0x00B7, 0x00}, {0x00BB, 0x3C}, {0x00B2, 0x09}, {0x00CA, 0x09},
                {0x0198, 0x01}, {0x01B0, 0x17}, {0x01AD, 0x00}, {0x00FF, 0x05},
                {0x0100, 0x05}, {0x0199, 0x05}, {0x01A6, 0x1B}, {0x01AC, 0x3E},
                {0x01A7, 0x1F}, {0x0030, 0x00},
            };
            for (const auto& r : kPrivate) {
                if (!Write8(r[0], static_cast<uint8_t>(r[1]))) {
                    return false;
                }
            }
        }
        // Thanh ghi public khuyen nghi
        static const uint16_t kPublic[][2] = {
            {0x0011, 0x10},  // GPIO1: ngat muc thap
            {0x010A, 0x30},  // averaging period
            {0x003F, 0x46},  // ALS analogue gain
            {0x0031, 0xFF},  // auto calib period
            {0x0040, 0x63},  // ALS integration 100 ms
            {0x002E, 0x01},  // calib nhiet do 1 lan
            {0x001B, 0x09},  // range inter-measurement 100 ms
            {0x003E, 0x31},  // ALS inter-measurement 500 ms
            {0x0014, 0x24},  // ngat: range new sample ready
            {0x016, 0x00},   // xoa co fresh-out-of-reset
        };
        for (const auto& r : kPublic) {
            if (!Write8(r[0], static_cast<uint8_t>(r[1]))) {
                return false;
            }
        }
        return true;
    }
};

#endif  // BREAD_COMPACT_WIFI_VL6180X_H
