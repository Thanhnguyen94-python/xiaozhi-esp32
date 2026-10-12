#include "dual_oled_display.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define TAG "DualOledDisplay"

DualOledDisplay::DualOledDisplay(OledDisplay* face, OledDisplay* text)
    : face_(face), text_(text) {
    // Kế thừa kích thước từ màn face để các logic cũ (GetDisplay()->width())
    // vẫn nhận đúng giá trị.
    if (face_ != nullptr) {
        width_ = face_->width();
        height_ = face_->height();
    } else if (text_ != nullptr) {
        width_ = text_->width();
        height_ = text_->height();
    }
    mutex_ = xSemaphoreCreateRecursiveMutex();
}

DualOledDisplay::~DualOledDisplay() {
    // SetupUI() không cần gọi destroy trước — OledDisplay destructor tự dọn.
    delete face_;
    face_ = nullptr;
    delete text_;
    text_ = nullptr;

    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool DualOledDisplay::Lock(int timeout_ms) {
    if (mutex_ == nullptr) return true;
    TickType_t ticks = (timeout_ms <= 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(mutex_, ticks) == pdTRUE;
}

void DualOledDisplay::Unlock() {
    if (mutex_ != nullptr) {
        xSemaphoreGiveRecursive(mutex_);
    }
}

void DualOledDisplay::SetupUI() {
    Display::SetupUI();
    if (face_) face_->SetupUI();
    if (text_) text_->SetupUI();
}

void DualOledDisplay::SetStatus(const char* status) {
    if (face_) face_->SetStatus(status);
    if (text_) text_->SetStatus(status);
}

void DualOledDisplay::ShowNotification(const char* notification, int duration_ms) {
    if (text_) text_->ShowNotification(notification, duration_ms);
}

void DualOledDisplay::ShowNotification(const std::string& notification, int duration_ms) {
    if (text_) text_->ShowNotification(notification.c_str(), duration_ms);
}

void DualOledDisplay::SetEmotion(const char* emotion) {
    if (face_) face_->SetEmotion(emotion);
}

void DualOledDisplay::SetChatMessage(const char* role, const char* content) {
    if (text_) text_->SetChatMessage(role, content);
}

void DualOledDisplay::ClearChatMessages() {
    if (face_) face_->ClearChatMessages();
    if (text_) text_->ClearChatMessages();
}

void DualOledDisplay::SetTheme(Theme* theme) {
    if (face_) face_->SetTheme(theme);
    if (text_) text_->SetTheme(theme);
}

Theme* DualOledDisplay::GetTheme() {
    return face_ ? face_->GetTheme() : nullptr;
}

void DualOledDisplay::UpdateStatusBar(bool update_all) {
    if (face_) face_->UpdateStatusBar(update_all);
    if (text_) text_->UpdateStatusBar(update_all);
}

void DualOledDisplay::SetPowerSaveMode(bool on) {
    if (face_) face_->SetPowerSaveMode(on);
    if (text_) text_->SetPowerSaveMode(on);
}