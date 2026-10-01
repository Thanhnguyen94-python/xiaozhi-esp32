# bread-compact-wifi

Tai lieu nhanh cho du an con board bread-compact-wifi.

## Muc tieu

- Chay song song dieu khien local WebUI va dieu khien server bang giong noi.
- Tach ro logic board wiring va logic dieu khien robot de de bao tri.
- Giu tuong thich voi he thong tong the cua du an xiaozhi-esp32.

## Cau truc file de xuat (hien tai)

- compact_wifi_board.cc
  - Wiring phan cung (display, nut nhan, sdcard, network)
  - Tao cac module dieu khien
  - Khoi dong WebUI
- robot_control_hub.h
- robot_control_hub.cc
  - Tap trung MCP tool cho robot va webui
  - Xu ly lenh robot.move / robot.control
  - Xu ly lay link WebUI de hien OLED
  - Timer stop khong block cho lenh di chuyen
- robot_web_ui_server.h
- robot_web_ui_server.cc
  - HTTP server local
  - API status/settings/control/music
  - Dieu khien truc tiep bang giao dien web
- config.h
  - Cau hinh chan, thong so board

## Nguyen tac tach file

- File nao phu thuoc rieng board bread-compact-wifi thi dat trong folder nay.
- File nao dung chung nhieu board thi de o boards/common hoac module tong.
- Khong dua logic app tong vao board neu khong can thiet.

## Huong dan test nhanh

1. Flash firmware moi nhat.
2. Kiem tra voice: lenh di chuyen + lenh mo nhac SD.
3. Kiem tra WebUI: forward/left/right + stop.
4. Kiem tra chuyen doi qua lai voice <-> webui van hoat dong.
5. Kiem tra hoi dia chi webui: robot doc va OLED hien link.

## Ghi chu

- Cac ghi chu moi trong module nay uu tien tieng Viet khong dau.
- Neu can bo sung thiet bi ngoai vi (den, quat, relay), nen them service rieng trong folder nay truoc.
