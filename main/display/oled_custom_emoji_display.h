#ifndef OLED_CUSTOM_EMOJI_DISPLAY_H
#define OLED_CUSTOM_EMOJI_DISPLAY_H

#include "oled_display.h"

class OledCustomEmojiDisplay : public OledDisplay {
public:
    OledCustomEmojiDisplay(esp_lcd_panel_io_handle_t panel_io,
        esp_lcd_panel_handle_t panel,
        int width,
        int height,
        bool mirror_x,
        bool mirror_y)
        : OledDisplay(panel_io, panel, width, height, mirror_x, mirror_y) {
    }

    ~OledCustomEmojiDisplay() override;

    void SetupUI() override;
    void SetStatus(const char* status) override;
    void SetEmotion(const char* emotion) override;

private:
    lv_obj_t* face_root_ = nullptr;
    lv_obj_t* left_eye_ = nullptr;
    lv_obj_t* right_eye_ = nullptr;
    lv_obj_t* mouth_label_ = nullptr;
    lv_timer_t* face_timer_ = nullptr;

    const char* current_face_emotion_ = "neutral";
    bool blink_closed_ = false;
    uint32_t frame_count_ = 0;
    uint32_t next_blink_frame_ = 24;
    uint8_t blink_phase_ = 0;
    bool speaking_ = false;
    uint8_t speaking_frame_ = 0;

    bool IsFaceMode() const;
    void SetupFaceUI_128x64();
    void RenderFace();
    void AnimateFaceTick();

    static void FaceTimerCallback(lv_timer_t* timer);
};

#endif // OLED_CUSTOM_EMOJI_DISPLAY_H
