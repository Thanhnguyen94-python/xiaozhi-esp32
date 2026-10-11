#pragma once

/**
 * Singleton quản lý vòng đời của esp_lvgl_port.
 *
 * Vì `lvgl_port_init()` chỉ được gọi DUY NHẤT 1 LẦN cho toàn bộ app,
 * và `lvgl_port_deinit()` chỉ gọi khi TẤT CẢ display đã bị hủy,
 * nên cần một điểm truy cập trung tâm thay vì gọi trực tiếp trong
 * từng constructor của Display.
 *
 * Cách dùng:
 *   if (!LvglPortManager::EnsureInitialized()) {
 *       ESP_LOGE(TAG, "LVGL port init failed");
 *       return;
 *   }
 *   display_ = lvgl_port_add_disp(&display_cfg);
 */
class LvglPortManager {
public:
    /**
     * Khởi tạo esp_lvgl_port nếu chưa làm.
     * Trả về true nếu port đã sẵn sàng (dù là lần init đầu hay đã init trước đó).
     * An toàn khi gọi từ nhiều task (dùng portMUX).
     */
    static bool EnsureInitialized();

    /**
     * Kiểm tra đã init chưa (không tự init).
     */
    static bool IsInitialized();

    // Không expose Deinitialize: LVGL port sống suốt vòng đời app.
    // Nếu cần deinit cho mục đích test, thêm sau.

private:
    LvglPortManager() = delete;
    ~LvglPortManager() = delete;
    LvglPortManager(const LvglPortManager&) = delete;
    LvglPortManager& operator=(const LvglPortManager&) = delete;
};