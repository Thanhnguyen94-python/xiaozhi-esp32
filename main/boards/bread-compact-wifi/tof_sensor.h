#ifndef BREAD_COMPACT_WIFI_TOF_SENSOR_H
#define BREAD_COMPACT_WIFI_TOF_SENSOR_H

// Driver toi gian cho cam bien ToF dia chi I2C 0x29, tu nhan dien loai chip:
// - VL6180X (TOF050C): dia chi thanh ghi 16-bit, ID 0x000 = 0xB4, do 0..~200 mm, single-shot.
// - VL53L0X (TOF200C, GY-530, VL53L0X V2): thanh ghi 8-bit, ID 0xC0 = 0xEE, do ~30..1200 mm,
//   chay lien tuc (back-to-back) nen doc khong phai cho.
// - VL53L1X: chi nhan dien de bao loi tren WebUI (chua ho tro).
// Trinh tu init VL6180X theo ST AN4545, VL53L0X theo thu vien Pololu (rut gon: bo buoc tinh lai
// timing budget, du dung cho chong va cham / mep ban).
// Dung chung bus I2C voi OLED chinh.

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>

class TofSensor {
public:
    static constexpr uint16_t kAddr = 0x29;
    static constexpr int kNoTarget = -1;  // khong thay vat (xa hon tam do)

    // Gui len WebUI dang so (sns_tof_chip).
    enum Chip : int { kChipNone = 0, kChipVl6180x = 1, kChipVl53l0x = 2, kChipVl53l1x = 3 };
    // Ly do Open/Read that bai (sns_tof_err).
    enum Error : int {
        kErrNone = 0,
        kErrNoAck = 1,        // khong co thiet bi nao tra loi o 0x29 (day, nguon)
        kErrUnknownId = 2,    // co thiet bi o 0x29 nhung khong nhan ra loai chip
        kErrUnsupported = 3,  // nhan ra chip nhung chua ho tro (VL53L1X)
        kErrInit = 4,         // nhan ra chip nhung khoi tao that bai
        kErrRead = 5,         // dang chay thi mat ket noi / het thoi gian doc
    };

    ~TofSensor() { Close(); }

    bool Open(i2c_master_bus_handle_t bus) {
        Close();
        chip_ = kChipNone;
        last_id_ = 0;
        if (bus == nullptr || i2c_master_probe(bus, kAddr, 50) != ESP_OK) {
            error_ = kErrNoAck;
            return false;
        }
        i2c_device_config_t cfg = {};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address = kAddr;
        cfg.scl_speed_hz = 400 * 1000;
        if (i2c_master_bus_add_device(bus, &cfg, &dev_) != ESP_OK) {
            dev_ = nullptr;
            error_ = kErrNoAck;
            return false;
        }

        // Thu VL6180X truoc (ghi 0x00 vao thanh ghi 0x00 cua VL53L0X = SYSRANGE_START, vo hai).
        uint8_t id = 0;
        if (Read8Reg16(0x000, id) && id == 0xB4) {
            chip_ = kChipVl6180x;
        } else {
            last_id_ = id;
            uint8_t id8 = 0;
            if (Read8Reg8(0xC0, id8) && id8 == 0xEE) {
                chip_ = kChipVl53l0x;
            } else {
                uint8_t a = 0, b = 0;
                if (Read8Reg16(0x010F, a) && Read8Reg16(0x0110, b) && a == 0xEA && b == 0xCC) {
                    chip_ = kChipVl53l1x;
                }
            }
        }

        bool ok = false;
        switch (chip_) {
            case kChipVl6180x: ok = InitVl6180x(); break;
            case kChipVl53l0x: ok = InitVl53l0x(); break;
            case kChipVl53l1x:
                ESP_LOGW(TAG, "Phat hien VL53L1X o 0x29: chua ho tro chip nay");
                error_ = kErrUnsupported;
                Close();
                return false;
            default:
                ESP_LOGW(TAG, "Thiet bi 0x29 khong phai VL6180X/VL53L0X (ID=0x%02X)", last_id_);
                error_ = kErrUnknownId;
                Close();
                return false;
        }
        if (!ok) {
            ESP_LOGW(TAG, "Khoi tao %s that bai", ChipName(chip_));
            error_ = kErrInit;
            Close();
            return false;
        }
        error_ = kErrNone;
        last_sample_ms_ = NowMs();
        ESP_LOGI(TAG, "Da ket noi %s (0x29)", ChipName(chip_));
        return true;
    }

    void Close() {
        if (dev_ != nullptr) {
            if (chip_ == kChipVl53l0x) {
                Write8Reg8(0x00, 0x01);  // SYSRANGE_START: dung che do lien tuc
            }
            i2c_master_bus_rm_device(dev_);
            dev_ = nullptr;
        }
    }

    bool IsOpen() const { return dev_ != nullptr; }
    int chip() const { return chip_; }
    int error() const { return error_; }
    uint8_t last_id() const { return last_id_; }

    // Tam do toi da thuc te (mm) cua chip dang dung, 0 neu chua biet.
    int MaxRangeMm() const {
        switch (chip_) {
            case kChipVl6180x: return 200;
            case kChipVl53l0x: return 1200;
            default: return 0;
        }
    }

    static const char* ChipName(int chip) {
        switch (chip) {
            case kChipVl6180x: return "VL6180X";
            case kChipVl53l0x: return "VL53L0X";
            case kChipVl53l1x: return "VL53L1X";
            default: return "?";
        }
    }

    // Tra false neu loi giao tiep (goi Close() roi mo lai sau).
    // Tra true: fresh = co mau moi; mm = khoang cach hoac kNoTarget.
    bool ReadRangeMm(int& mm, bool& fresh) {
        fresh = false;
        if (dev_ == nullptr) {
            return false;
        }
        bool ok = (chip_ == kChipVl53l0x) ? ReadVl53l0x(mm, fresh) : ReadVl6180x(mm, fresh);
        if (!ok) {
            error_ = kErrRead;
        }
        return ok;
    }

private:
    static constexpr const char* TAG = "ToF";
    i2c_master_dev_handle_t dev_ = nullptr;
    int chip_ = kChipNone;
    int error_ = kErrNone;
    uint8_t last_id_ = 0;
    uint8_t stop_variable_ = 0;  // VL53L0X
    uint32_t last_sample_ms_ = 0;

    static uint32_t NowMs() {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    }

    // ---------- Truy cap thanh ghi ----------
    bool Write8Reg16(uint16_t reg, uint8_t val) {
        uint8_t buf[3] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF), val};
        return i2c_master_transmit(dev_, buf, sizeof(buf), 50) == ESP_OK;
    }

    bool Read8Reg16(uint16_t reg, uint8_t& val) {
        uint8_t addr[2] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF)};
        return i2c_master_transmit_receive(dev_, addr, sizeof(addr), &val, 1, 50) == ESP_OK;
    }

    bool Write8Reg8(uint8_t reg, uint8_t val) {
        uint8_t buf[2] = {reg, val};
        return i2c_master_transmit(dev_, buf, sizeof(buf), 50) == ESP_OK;
    }

    bool ReadReg8(uint8_t reg, uint8_t* val, size_t len) {
        return i2c_master_transmit_receive(dev_, &reg, 1, val, len, 50) == ESP_OK;
    }

    bool Read8Reg8(uint8_t reg, uint8_t& val) { return ReadReg8(reg, &val, 1); }

    bool WriteMultiReg8(uint8_t reg, const uint8_t* data, size_t len) {
        uint8_t buf[8];
        if (len + 1 > sizeof(buf)) {
            return false;
        }
        buf[0] = reg;
        for (size_t i = 0; i < len; ++i) {
            buf[i + 1] = data[i];
        }
        return i2c_master_transmit(dev_, buf, len + 1, 50) == ESP_OK;
    }

    // Ghi day cap {reg, val} 8-bit, dung som neu loi.
    bool WriteList8(const uint8_t (*list)[2], size_t n) {
        for (size_t i = 0; i < n; ++i) {
            if (!Write8Reg8(list[i][0], list[i][1])) {
                return false;
            }
        }
        return true;
    }

    // ---------- VL6180X ----------
    bool InitVl6180x() {
        uint8_t fresh = 0;
        if (!Read8Reg16(0x016, fresh)) {  // SYSTEM__FRESH_OUT_OF_RESET
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
                if (!Write8Reg16(r[0], static_cast<uint8_t>(r[1]))) {
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
            if (!Write8Reg16(r[0], static_cast<uint8_t>(r[1]))) {
                return false;
            }
        }
        return true;
    }

    // Do 1 lan (single-shot, cho toi da ~50 ms).
    bool ReadVl6180x(int& mm, bool& fresh) {
        if (!Write8Reg16(0x018, 0x01)) {  // SYSRANGE__START single-shot
            return false;
        }
        uint8_t status = 0;
        for (int i = 0; i < 25; ++i) {
            if (!Read8Reg16(0x04F, status)) {  // RESULT__INTERRUPT_STATUS_GPIO
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
        bool ok = Read8Reg16(0x062, range) && Read8Reg16(0x04D, range_status);  // RANGE_VAL / STATUS
        Write8Reg16(0x015, 0x07);  // SYSTEM__INTERRUPT_CLEAR
        if (!ok) {
            return false;
        }
        // Ma loi khac 0 (vd 11 = khong co muc tieu / tran tam do) -> coi nhu khong thay vat.
        mm = ((range_status >> 4) == 0) ? range : kNoTarget;
        fresh = true;
        return true;
    }

    // ---------- VL53L0X ----------
    // Cho den khi (reg & mask) != 0 (hoac == 0 neu want_zero), toi da timeout_ms.
    bool WaitReg8(uint8_t reg, uint8_t mask, bool want_zero, uint32_t timeout_ms) {
        uint32_t start = NowMs();
        while (true) {
            uint8_t v = 0;
            if (!Read8Reg8(reg, v)) {
                return false;
            }
            if (((v & mask) == 0) == want_zero) {
                return true;
            }
            if (NowMs() - start > timeout_ms) {
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

    bool GetSpadInfo(uint8_t& count, bool& is_aperture) {
        uint8_t v = 0;
        static const uint8_t kA[][2] = {{0x80, 0x01}, {0xFF, 0x01}, {0x00, 0x00}, {0xFF, 0x06}};
        if (!WriteList8(kA, 4) || !Read8Reg8(0x83, v) || !Write8Reg8(0x83, v | 0x04)) return false;
        static const uint8_t kB[][2] = {{0xFF, 0x07}, {0x81, 0x01}, {0x80, 0x01}, {0x94, 0x6B}, {0x83, 0x00}};
        if (!WriteList8(kB, 5)) return false;
        if (!WaitReg8(0x83, 0xFF, false, 100)) return false;  // cho 0x83 != 0
        if (!Write8Reg8(0x83, 0x01) || !Read8Reg8(0x92, v)) return false;
        count = v & 0x7F;
        is_aperture = (v >> 7) & 0x01;
        uint8_t r = 0;
        if (!Write8Reg8(0x81, 0x00) || !Write8Reg8(0xFF, 0x06) || !Read8Reg8(0x83, r) ||
            !Write8Reg8(0x83, r & ~0x04)) {
            return false;
        }
        static const uint8_t kC[][2] = {{0xFF, 0x01}, {0x00, 0x01}, {0xFF, 0x00}, {0x80, 0x00}};
        return WriteList8(kC, 4);
    }

    bool SingleRefCalibration(uint8_t vhv_init_byte) {
        if (!Write8Reg8(0x00, 0x01 | vhv_init_byte)) return false;  // SYSRANGE_START
        if (!WaitReg8(0x13, 0x07, false, 200)) return false;        // RESULT_INTERRUPT_STATUS
        return Write8Reg8(0x0B, 0x01) && Write8Reg8(0x00, 0x00);
    }

    bool InitVl53l0x() {
        uint8_t v = 0;
        // I/O 2V8 (module co LDO 2.8V)
        if (!Read8Reg8(0x89, v) || !Write8Reg8(0x89, v | 0x01)) return false;
        static const uint8_t kStd[][2] = {{0x88, 0x00}, {0x80, 0x01}, {0xFF, 0x01}, {0x00, 0x00}};
        if (!WriteList8(kStd, 4) || !Read8Reg8(0x91, stop_variable_)) return false;
        static const uint8_t kStd2[][2] = {{0x00, 0x01}, {0xFF, 0x00}, {0x80, 0x00}};
        if (!WriteList8(kStd2, 3)) return false;
        // Tat kiem tra SIGNAL_RATE_MSRC va SIGNAL_RATE_PRE_RANGE
        if (!Read8Reg8(0x60, v) || !Write8Reg8(0x60, v | 0x12)) return false;
        // Gioi han signal rate final range = 0.25 MCPS (dinh dang 9.7 -> 32)
        static const uint8_t kRate[2] = {0x00, 0x20};
        if (!WriteMultiReg8(0x44, kRate, 2)) return false;
        if (!Write8Reg8(0x01, 0xFF)) return false;  // SYSTEM_SEQUENCE_CONFIG

        // Reference SPAD
        uint8_t spad_count = 0;
        bool spad_aperture = false;
        if (!GetSpadInfo(spad_count, spad_aperture)) return false;
        uint8_t map[6] = {};
        if (!ReadReg8(0xB0, map, 6)) return false;  // GLOBAL_CONFIG_SPAD_ENABLES_REF_0
        static const uint8_t kSpad[][2] = {{0xFF, 0x01}, {0x4F, 0x00}, {0x4E, 0x2C}, {0xFF, 0x00}, {0xB6, 0xB4}};
        if (!WriteList8(kSpad, 5)) return false;
        uint8_t first = spad_aperture ? 12 : 0;
        uint8_t enabled = 0;
        for (uint8_t i = 0; i < 48; ++i) {
            if (i < first || enabled == spad_count) {
                map[i / 8] &= ~(1 << (i % 8));
            } else if ((map[i / 8] >> (i % 8)) & 0x01) {
                ++enabled;
            }
        }
        if (!WriteMultiReg8(0xB0, map, 6)) return false;

        // Default tuning settings (ST API)
        static const uint8_t kTuning[][2] = {
            {0xFF, 0x01}, {0x00, 0x00}, {0xFF, 0x00}, {0x09, 0x00}, {0x10, 0x00}, {0x11, 0x00},
            {0x24, 0x01}, {0x25, 0xFF}, {0x75, 0x00}, {0xFF, 0x01}, {0x4E, 0x2C}, {0x48, 0x00},
            {0x30, 0x20}, {0xFF, 0x00}, {0x30, 0x09}, {0x54, 0x00}, {0x31, 0x04}, {0x32, 0x03},
            {0x40, 0x83}, {0x46, 0x25}, {0x60, 0x00}, {0x27, 0x00}, {0x50, 0x06}, {0x51, 0x00},
            {0x52, 0x96}, {0x56, 0x08}, {0x57, 0x30}, {0x61, 0x00}, {0x62, 0x00}, {0x64, 0x00},
            {0x65, 0x00}, {0x66, 0xA0}, {0xFF, 0x01}, {0x22, 0x32}, {0x47, 0x14}, {0x49, 0xFF},
            {0x4A, 0x00}, {0xFF, 0x00}, {0x7A, 0x0A}, {0x7B, 0x00}, {0x78, 0x21}, {0xFF, 0x01},
            {0x23, 0x34}, {0x42, 0x00}, {0x44, 0xFF}, {0x45, 0x26}, {0x46, 0x05}, {0x40, 0x40},
            {0x0E, 0x06}, {0x20, 0x1A}, {0x43, 0x40}, {0xFF, 0x00}, {0x34, 0x03}, {0x35, 0x44},
            {0xFF, 0x01}, {0x31, 0x04}, {0x4B, 0x09}, {0x4C, 0x05}, {0x4D, 0x04}, {0xFF, 0x00},
            {0x44, 0x00}, {0x45, 0x20}, {0x47, 0x08}, {0x48, 0x28}, {0x67, 0x00}, {0x70, 0x04},
            {0x71, 0x01}, {0x72, 0xFE}, {0x76, 0x00}, {0x77, 0x00}, {0xFF, 0x01}, {0x0D, 0x01},
            {0xFF, 0x00}, {0x80, 0x01}, {0x01, 0xF8}, {0xFF, 0x01}, {0x8E, 0x01}, {0x00, 0x01},
            {0xFF, 0x00}, {0x80, 0x00},
        };
        if (!WriteList8(kTuning, sizeof(kTuning) / sizeof(kTuning[0]))) return false;

        // Ngat: new sample ready, muc thap
        if (!Write8Reg8(0x0A, 0x04)) return false;  // SYSTEM_INTERRUPT_CONFIG_GPIO
        if (!Read8Reg8(0x84, v) || !Write8Reg8(0x84, v & ~0x10)) return false;  // GPIO_HV_MUX_ACTIVE_HIGH
        if (!Write8Reg8(0x0B, 0x01)) return false;  // SYSTEM_INTERRUPT_CLEAR
        if (!Write8Reg8(0x01, 0xE8)) return false;  // tat MSRC + TCC

        // Hieu chuan tham chieu (VHV + phase)
        if (!Write8Reg8(0x01, 0x01) || !SingleRefCalibration(0x40)) return false;
        if (!Write8Reg8(0x01, 0x02) || !SingleRefCalibration(0x00)) return false;
        if (!Write8Reg8(0x01, 0xE8)) return false;

        // Bat do lien tuc back-to-back
        static const uint8_t kStart[][2] = {{0x80, 0x01}, {0xFF, 0x01}, {0x00, 0x00}};
        if (!WriteList8(kStart, 3) || !Write8Reg8(0x91, stop_variable_)) return false;
        static const uint8_t kStart2[][2] = {{0x00, 0x01}, {0xFF, 0x00}, {0x80, 0x00}, {0x00, 0x02}};
        return WriteList8(kStart2, 4);
    }

    // Khong cho: neu chua co mau moi thi fresh=false; qua 500 ms khong co mau -> loi.
    bool ReadVl53l0x(int& mm, bool& fresh) {
        uint8_t status = 0;
        if (!Read8Reg8(0x13, status)) {  // RESULT_INTERRUPT_STATUS
            return false;
        }
        uint32_t now = NowMs();
        if ((status & 0x07) == 0) {
            return (now - last_sample_ms_) < 500;
        }
        uint8_t buf[2] = {};
        uint8_t range_status = 0;
        bool ok = Read8Reg8(0x14, range_status) && ReadReg8(0x1E, buf, 2);  // RESULT_RANGE_STATUS (+10 = mm)
        Write8Reg8(0x0B, 0x01);  // SYSTEM_INTERRUPT_CLEAR
        if (!ok) {
            return false;
        }
        last_sample_ms_ = now;
        int range = (buf[0] << 8) | buf[1];
        int dev_err = (range_status >> 3) & 0x0F;
        // Theo ST API: 1/2/3 = loi phan cung, 4 = khong du tin hieu, 6/9 = loi pha -> khong co muc tieu.
        // 8/10 = vat qua gan (duoi tam do toi thieu): van giu so do nho de "nhin thang" chan duoc.
        // 8190/8191 = tran tam do.
        bool no_target = dev_err == 1 || dev_err == 2 || dev_err == 3 || dev_err == 4 ||
                         dev_err == 6 || dev_err == 9 || range >= 2000;
        mm = no_target ? kNoTarget : range;
        fresh = true;
        return true;
    }
};

#endif  // BREAD_COMPACT_WIFI_TOF_SENSOR_H
