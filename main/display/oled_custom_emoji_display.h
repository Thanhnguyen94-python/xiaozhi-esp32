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
        : OledDisplay(panel_io, panel, width, height, mirror_x, mirror_y,
                      OledRole::kFaceOnly) {   // ⬅️ QUAN TRỌNG: ép role = kFaceOnly
    }

    ~OledCustomEmojiDisplay() override;

    void SetupUI() override;
    void SetStatus(const char* status) override;
    void SetEmotion(const char* emotion) override;
    void SetChatMessage(const char* role, const char* content) override;

protected:
    // QR do OledDisplay vẽ; ở đây chỉ ẩn/hiện mặt robot
    void OnQrVisibilityChanged(bool visible) override;

private:
    lv_obj_t* face_root_ = nullptr;
    bool in_standby_ = false;   // Đang ở trạng thái chờ (STANDBY/IDLE)
    uint32_t standby_enter_tick_ = 0;

    // Cung tròn cho mặt happy (mắt ^^ và miệng ◡)
    lv_obj_t* happy_left_eye_arc_  = nullptr;
    lv_obj_t* happy_right_eye_arc_ = nullptr;
    lv_obj_t* happy_mouth_arc_     = nullptr;
    // Mắt + miệng buồn (sad)
    // Sad face: mắt = hình tròn đen + khối trắng xoay đè lên tạo viên phân
    lv_obj_t* sad_left_eye_      = nullptr;
    lv_obj_t* sad_right_eye_     = nullptr;
    lv_obj_t* sad_left_eye_cut_  = nullptr;
    lv_obj_t* sad_right_eye_cut_ = nullptr;
    lv_obj_t* sad_mouth_arc_     = nullptr;
    lv_obj_t* sad_left_tear_     = nullptr;
    lv_obj_t* sad_right_tear_    = nullptr;

    uint32_t sad_anim_tick_ = 0;

    // Neutral (listening) animation
    uint32_t neutral_anim_tick_   = 0;
    uint8_t  eye_fall_side_       = 0;   // 0=none, 1=trái, 2=phải
    uint8_t  eye_fall_state_      = 0;   // 0=idle, 1=falling, 2=bouncing
    uint32_t eye_fall_tick_       = 0;
    uint32_t eye_fall_next_tick_  = 0;

    // Sleepy face
    lv_obj_t* sleepy_left_eye_   = nullptr;
    lv_obj_t* sleepy_right_eye_  = nullptr;
    lv_obj_t* sleepy_mouth_      = nullptr;
    lv_obj_t* sleepy_drool_      = nullptr;
    lv_obj_t* sleepy_z_[3]       = {nullptr, nullptr, nullptr};
    lv_point_precise_t sleepy_z_pts_[3][4];

    uint32_t sleepy_anim_tick_ = 0;

    // Thinking face - bóng đèn
    lv_obj_t* thinking_bulb_       = nullptr;
    lv_obj_t* thinking_bulb_inner_ = nullptr;
    lv_obj_t* thinking_zap_        = nullptr;
    lv_obj_t* thinking_base_       = nullptr;
    lv_obj_t* thinking_rays_[3]    = {nullptr, nullptr, nullptr};
    lv_point_precise_t thinking_zap_pts_[4];
    lv_point_precise_t thinking_ray_pts_[3][2];
    uint32_t thinking_anim_tick_   = 0;

    // Bob animation
    lv_anim_t bob_anim_;

    // Angry face: mắt giống sad nhưng nghiêng ngược + xa nhau hơn
    lv_obj_t* angry_left_eye_      = nullptr;
    lv_obj_t* angry_right_eye_     = nullptr;
    lv_obj_t* angry_left_eye_cut_  = nullptr;
    lv_obj_t* angry_right_eye_cut_ = nullptr;

    // Dấu giận: 4 góc vuông quanh 1 tâm, đặt trong container để dễ rung
    lv_obj_t* angry_mark_root_ = nullptr;
    lv_obj_t* angry_mark_lines_[4] = {nullptr, nullptr, nullptr, nullptr};
    lv_point_precise_t angry_mark_pts_[4][3];

    uint32_t angry_anim_tick_ = 0;

    // Mắt (hình khối)
    lv_obj_t* left_eye_ = nullptr;
    lv_obj_t* right_eye_ = nullptr;
    lv_obj_t* left_pupil_ = nullptr;
    lv_obj_t* right_pupil_ = nullptr;

    // Mắt xiên (tức giận) — dùng lv_line
    lv_obj_t* left_eye_line_ = nullptr;
    lv_obj_t* right_eye_line_ = nullptr;

    // Surprised face: miệng chữ O
    lv_obj_t* surprised_mouth_ = nullptr;
    uint32_t surprised_anim_tick_ = 0;

    // Miệng — dùng lv_line
    lv_obj_t* mouth_line_ = nullptr;

    // Miệng động (dùng Object oval cho lip-sync khi nói)
    lv_obj_t* mouth_obj_ = nullptr;
    lv_obj_t* mouth_inner_ = nullptr; /// tạo hốc miệng

    // Bộ đệm điểm cho lv_line (PHẢI là member, không được là local)
    lv_point_precise_t left_eye_pts_[2];
    lv_point_precise_t right_eye_pts_[2];
    lv_point_precise_t mouth_pts_[3];

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