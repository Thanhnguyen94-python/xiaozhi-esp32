#ifndef BREAD_COMPACT_WIFI_WEB_AUTH_H
#define BREAD_COMPACT_WIFI_WEB_AUTH_H

// Dang nhap cho WebUI bang HTTP Basic Auth.
// - Ten dang nhap co dinh: admin. Mat khau luu trong NVS (namespace "web_auth") dang bam SHA-256 co salt.
// - Chua doi mat khau thi dung mat khau mac dinh kDefaultPassword (WebUI se canh bao doi ngay).
// - Sai 5 lan lien tiep thi khoa 60 giay.
// - POST bat buoc Content-Type: application/json (chan form/CSRF tu trang web la).
// - WebSocket kiem tra them header Origin (chan gian doan WebSocket tu trang web la).
// LUU Y: day la HTTP thuong (khong ma hoa), thong tin dang nhap co the bi nghe len trong cung mang Wi-Fi.
// File header-only, dat trong boards/bread-compact-wifi/.

#include "settings.h"

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

namespace webauth {

// ===================== THAM SO =====================
constexpr const char* kUser = "admin";
constexpr const char* kDefaultPassword = "admin";   // doi ngay sau khi dang nhap lan dau
// Quen mat khau: dat true, nap lai firmware 1 lan de dua mat khau ve mac dinh, roi dat lai false va nap lai.
constexpr bool kResetPasswordOnBoot = false;
constexpr int kMaxFailures = 5;
constexpr int64_t kLockoutUs = 60LL * 1000000LL;
constexpr size_t kMinPasswordLen = 6;
constexpr size_t kMaxPasswordLen = 32;
// ===================================================

namespace detail {

constexpr const char* kTag = "WebAuth";
inline std::atomic<int> g_fail_count{0};
inline std::atomic<int64_t> g_locked_until_us{0};

// ---- SHA-256 tu viet (khong phu thuoc mbedtls) ----
inline void Sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto rotr = [](uint32_t x, int n) -> uint32_t { return (x >> n) | (x << (32 - n)); };

    std::string msg(reinterpret_cast<const char*>(data), len);
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) {
        msg.push_back('\0');
    }
    uint64_t bitlen = static_cast<uint64_t>(len) * 8ULL;
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<char>((bitlen >> (i * 8)) & 0xff));
    }

    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            const uint8_t* b = reinterpret_cast<const uint8_t*>(msg.data()) + off + i * 4;
            w[i] = (static_cast<uint32_t>(b[0]) << 24) | (static_cast<uint32_t>(b[1]) << 16) |
                   (static_cast<uint32_t>(b[2]) << 8) | static_cast<uint32_t>(b[3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    for (int i = 0; i < 8; ++i) {
        out[i * 4 + 0] = static_cast<uint8_t>(h[i] >> 24);
        out[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
        out[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
        out[i * 4 + 3] = static_cast<uint8_t>(h[i]);
    }
}

// ---- Base64 decode tu viet. false neu co ky tu khong hop le. ----
inline bool Base64Decode(const std::string& in, std::string& out) {
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char ch : in) {
        int v;
        if (ch >= 'A' && ch <= 'Z') v = ch - 'A';
        else if (ch >= 'a' && ch <= 'z') v = ch - 'a' + 26;
        else if (ch >= '0' && ch <= '9') v = ch - '0' + 52;
        else if (ch == '+') v = 62;
        else if (ch == '/') v = 63;
        else if (ch == '=') break;
        else return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xff));
        }
    }
    return true;
}

inline bool ConstTimeEq(const std::string& a, const std::string& b) {
    uint32_t diff = static_cast<uint32_t>(a.size() ^ b.size());
    size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
        diff |= static_cast<uint32_t>(static_cast<uint8_t>(a[i]) ^ static_cast<uint8_t>(b[i]));
    }
    return diff == 0;
}

inline void Hash(uint32_t salt, const std::string& password, uint8_t out[32]) {
    std::string buf(reinterpret_cast<const char*>(&salt), sizeof(salt));
    buf += "robot-webui:";
    buf += password;
    Sha256(reinterpret_cast<const uint8_t*>(buf.data()), buf.size(), out);
}

inline bool HasStoredPassword() {
    Settings s("web_auth", false);
    return s.GetInt("has", 0) == 1;
}

inline bool GetHeader(httpd_req_t* req, const char* name, std::string& out, size_t max_len = 256) {
    size_t len = httpd_req_get_hdr_value_len(req, name);
    if (len == 0 || len > max_len) {
        return false;
    }
    std::string buf(len + 1, '\0');
    if (httpd_req_get_hdr_value_str(req, name, buf.data(), buf.size()) != ESP_OK) {
        return false;
    }
    out.assign(buf.c_str());
    return true;
}

// Tach "Authorization: Basic base64(user:pass)". false neu khong co / sai dinh dang.
inline bool ParseBasic(httpd_req_t* req, std::string& user, std::string& pass) {
    std::string hdr;
    if (!GetHeader(req, "Authorization", hdr, 160)) {
        return false;
    }
    if (hdr.compare(0, 6, "Basic ") != 0) {
        return false;
    }
    std::string up;
    if (!Base64Decode(hdr.substr(6), up) || up.size() > 120) {
        return false;
    }
    size_t pos = up.find(':');
    if (pos == std::string::npos) {
        return false;
    }
    user = up.substr(0, pos);
    pass = up.substr(pos + 1);
    return true;
}

inline void SendChallenge(httpd_req_t* req) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Robot WebUI\"");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Can dang nhap", HTTPD_RESP_USE_STRLEN);
}

inline void SendLocked(httpd_req_t* req) {
    httpd_resp_set_status(req, "429 Too Many Requests");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Sai mat khau qua nhieu lan. Thu lai sau 60 giay.", HTTPD_RESP_USE_STRLEN);
}

}  // namespace detail

// Goi 1 lan khi WebUI khoi dong.
inline void Init() {
    if (kResetPasswordOnBoot) {
        Settings s("web_auth", true);
        s.SetInt("has", 0);
        ESP_LOGW(detail::kTag, "kResetPasswordOnBoot=true: da dua mat khau ve mac dinh (nhớ dat lai false)");
    }
    if (!detail::HasStoredPassword()) {
        ESP_LOGW(detail::kTag, "Dang dung mat khau WebUI MAC DINH. Hay doi mat khau trong the Bao mat.");
    }
}

// true neu chua tung doi mat khau (dang dung mat khau mac dinh).
inline bool IsDefaultPassword() {
    return !detail::HasStoredPassword();
}

inline bool Verify(const std::string& password) {
    if (!detail::HasStoredPassword()) {
        return detail::ConstTimeEq(password, kDefaultPassword);
    }
    Settings s("web_auth", false);
    uint32_t salt = static_cast<uint32_t>(s.GetInt("salt", 0));
    uint8_t h[32];
    detail::Hash(salt, password, h);
    uint32_t diff = 0;
    for (int i = 0; i < 8; ++i) {
        char key[3] = {'h', static_cast<char>('0' + i), '\0'};
        uint32_t stored = static_cast<uint32_t>(s.GetInt(key, 0));
        uint32_t cur;
        std::memcpy(&cur, h + i * 4, 4);
        diff |= (stored ^ cur);
    }
    return diff == 0;
}

// Doi mat khau. false neu do dai khong hop le.
inline bool SetPassword(const std::string& password) {
    if (password.size() < kMinPasswordLen || password.size() > kMaxPasswordLen) {
        return false;
    }
    uint32_t salt = esp_random();
    uint8_t h[32];
    detail::Hash(salt, password, h);
    Settings s("web_auth", true);
    s.SetInt("salt", static_cast<int>(salt));
    for (int i = 0; i < 8; ++i) {
        char key[3] = {'h', static_cast<char>('0' + i), '\0'};
        uint32_t w;
        std::memcpy(&w, h + i * 4, 4);
        s.SetInt(key, static_cast<int>(w));
    }
    s.SetInt("has", 1);
    ESP_LOGI(detail::kTag, "Da doi mat khau WebUI");
    return true;
}

// true neu request mang thong tin dang nhap dung. KHONG gui phan hoi.
// Dem so lan sai; qua kMaxFailures thi khoa kLockoutUs.
inline bool IsAuthorized(httpd_req_t* req) {
    int64_t now = esp_timer_get_time();
    if (now < detail::g_locked_until_us.load()) {
        return false;
    }
    std::string user, pass;
    if (!detail::ParseBasic(req, user, pass)) {
        return false;  // chua gui thong tin (trinh duyet se hien hop dang nhap), khong tinh la sai
    }
    bool user_ok = detail::ConstTimeEq(user, kUser);
    bool pass_ok = Verify(pass);
    if (user_ok && pass_ok) {
        detail::g_fail_count.store(0);
        return true;
    }
    vTaskDelay(pdMS_TO_TICKS(300));  // lam cham do mat khau
    if (detail::g_fail_count.fetch_add(1) + 1 >= kMaxFailures) {
        detail::g_fail_count.store(0);
        detail::g_locked_until_us.store(esp_timer_get_time() + kLockoutUs);
        ESP_LOGW(detail::kTag, "Sai mat khau qua nhieu lan, khoa %d giay", static_cast<int>(kLockoutUs / 1000000));
    }
    return false;
}

// Kiem tra cho cac API HTTP thuong: gui 401/429/403 neu khong hop le. true = cho phep xu ly tiep.
inline bool Check(httpd_req_t* req) {
    if (esp_timer_get_time() < detail::g_locked_until_us.load()) {
        detail::SendLocked(req);
        return false;
    }
    if (!IsAuthorized(req)) {
        if (esp_timer_get_time() < detail::g_locked_until_us.load()) {
            detail::SendLocked(req);
        } else {
            detail::SendChallenge(req);
        }
        return false;
    }
    if (req->method == HTTP_POST) {
        std::string ct;
        if (!detail::GetHeader(req, "Content-Type", ct, 128) || ct.compare(0, 16, "application/json") != 0) {
            httpd_resp_set_status(req, "403 Forbidden");
            httpd_resp_set_type(req, "text/plain");
            httpd_resp_send(req, "Yeu cau khong hop le", HTTPD_RESP_USE_STRLEN);
            return false;
        }
    }
    return true;
}

// WebSocket: Origin (neu co) phai trung Host cua robot.
inline bool OriginOk(httpd_req_t* req) {
    std::string origin;
    if (!detail::GetHeader(req, "Origin", origin, 128)) {
        return true;  // khong co Origin (khong phai trinh duyet)
    }
    std::string host;
    if (!detail::GetHeader(req, "Host", host, 128)) {
        return false;
    }
    size_t scheme_end = origin.find("://");
    std::string origin_host = (scheme_end == std::string::npos) ? origin : origin.substr(scheme_end + 3);
    return detail::ConstTimeEq(origin_host, host);
}

}  // namespace webauth

#endif  // BREAD_COMPACT_WIFI_WEB_AUTH_H