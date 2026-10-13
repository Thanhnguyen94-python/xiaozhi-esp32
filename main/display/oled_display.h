#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "lvgl_display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

/**
 * Vai trò của màn hình OLED.
 * Cho phép 1 instance OledDisplay chỉ render 1 phần layout, để
 * DualOledDisplay có thể chia việc giữa 2 màn.
 */
enum class OledRole {
    kFull,      // Layout đầy đủ: status + emotion + chat (mặc định, giữ tương thích)
    kFaceOnly,  // Chỉ emotion + status icons (màn 0.96")
    kTextOnly,  // Chỉ status text + chat message (màn 0.91")
};

class OledDisplay : public LvglDisplay {
protected:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    OledRole role_ = OledRole::kFull;

    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* content_left_ = nullptr;
    lv_obj_t* content_right_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t* emotion_label_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;

    // Animation scroll — phải là member, KHÔNG dùng static local,
    // vì 2 instance sẽ ghi đè lẫn nhau.
    lv_anim_t scroll_anim_;

    bool chat_pinned_ = false;
    int64_t chat_pin_until_us_ = 0;

    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

    // Layout cho role kFull (giữ nguyên code cũ)
    void SetupUI_128x64();
    void SetupUI_128x32();

    // Layout cho role mới
    void SetupUI_FaceOnly();
    void SetupUI_TextOnly();

public:
    OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                int width, int height, bool mirror_x, bool mirror_y,
                OledRole role = OledRole::kFull);
    ~OledDisplay();

    virtual void SetupUI() override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void PinChatMessage(const char* content, int duration_ms = 20000) override;
    virtual void ShowQRCode(const char* text, int duration_ms = 20000) override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetTheme(Theme* theme) override;
    virtual void SetStatus(const char* status) override;
    virtual void ShowNotification(const char* notification, int duration_ms = 3000) override;

    inline OledRole role() const { return role_; }
};

#endif // OLED_DISPLAY_H