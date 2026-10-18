#pragma once

#include "display.h"
#include "oled_display.h"
#include <freertos/FreeRTOS.h>       // ⬅️ THÊM
#include <freertos/semphr.h>         // ⬅️ THÊM
#include <freertos/task.h> 

/**
 * Wrapper route các method của Display tới 2 màn OLED:
 *   - face_  (0.96"): SetEmotion → vẽ mặt
 *   - text_  (0.91"): SetChatMessage / SetStatus / ShowNotification → vẽ chữ
 *
 * Vòng đời: DualOledDisplay sở hữu và delete cả 2 OledDisplay.
 */
class DualOledDisplay : public Display {
public:
    DualOledDisplay(OledDisplay* face, OledDisplay* text);
    virtual ~DualOledDisplay();

    // --- Route theo vai trò ---
    virtual void SetStatus(const char* status) override;                        // → text_
    virtual void ShowNotification(const char* notification, int duration_ms = 3000) override;  // → text_
    virtual void ShowNotification(const std::string& notification, int duration_ms = 3000) override;  // → text_
    virtual void SetEmotion(const char* emotion) override;                      // → face_
    virtual void SetChatMessage(const char* role, const char* content) override;  // → text_
    virtual void PinChatMessage(const char* content, int duration_ms = 20000) override;
    virtual void ShowQRCode(const char* text, int duration_ms = 20000) override;   // → face_
    virtual void HideQRCode() override;                                         // → face_
    virtual void ClearChatMessages() override;                                  // → cả 2
    virtual void SetTheme(Theme* theme) override;                               // → cả 2
    virtual Theme* GetTheme() override;                                         // → face_
    virtual void UpdateStatusBar(bool update_all = false) override;             // → cả 2
    virtual void SetPowerSaveMode(bool on) override;                            // → cả 2
    virtual void SetupUI() override;                                            // → cả 2

protected:
    virtual bool Lock(int timeout_ms = 0) override;   // dùng mutex riêng
    virtual void Unlock() override;

private:
    OledDisplay* face_ = nullptr;
    OledDisplay* text_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
};