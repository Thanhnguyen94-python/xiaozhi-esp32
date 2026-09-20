# MCP YouTube Server (Standalone)

Server MCP độc lập để tìm bài hát YouTube bằng tool `search_youtube_music(song_name: str)`.

## Mục tiêu
- Chạy **song song** với firmware/chatbot hiện tại, không sửa code cũ.
- Hỗ trợ **WSS Bridge** cho xiaozhi.me qua endpoint: `/mcp/?token=...`.
- Có thể giữ SSE riêng cho client khác qua `/sse?token=...`.
- Trả về JSON text gồm `title`, `url`, `duration_seconds`.

## Cài đặt
1. Tạo môi trường Python riêng.
2. Cài dependencies:
   - `pip install -r requirements.txt`
3. Copy `.env.example` thành `.env` và sửa token.
4. Điền `UPSTREAM_MCP_WSS` bằng MCP URL gốc của xiaozhi.me.

## Chạy server
- `uvicorn app:app --host 0.0.0.0 --port 8765`

## Endpoint
- Health check: `GET /healthz`
- MCP WSS bridge (khuyến nghị cho xiaozhi.me): `wss://<your-domain>/mcp/?token=<MCP_AUTH_TOKEN>`
- MCP SSE: `GET /sse?token=<MCP_AUTH_TOKEN>`

## Cách tích hợp với chatbot/backend
### Phương án A (đang dùng)
1. Đặt server này online bằng HTTPS/WSS public.
2. Trên xiaozhi.me, đổi MCP endpoint sang:
  - `wss://<your-domain>/mcp/?token=<MCP_AUTH_TOKEN>`
3. Server bridge sẽ:
  - xử lý local tool `search_youtube_music` bằng `yt-dlp`
  - forward các tool/request còn lại về `UPSTREAM_MCP_WSS`

=> Nhờ vậy không mất chức năng MCP cũ, chỉ bổ sung tìm nhạc YouTube.

### Lưu ý bảo mật
- Nếu token cũ đã lộ, hãy tạo token mới.
- Chỉ publish endpoint qua HTTPS/WSS.

## Ví dụ output tool
```json
{
  "query": "em cua ngay hom qua",
  "total": 3,
  "results": [
    {
      "title": "Sơn Tùng M-TP | Em Của Ngày Hôm Qua",
      "url": "https://www.youtube.com/watch?v=knW7-x7Y7RE",
      "duration_seconds": 230
    }
  ]
}
```
