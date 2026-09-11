# MCP YouTube Server (Standalone)

Server MCP độc lập để tìm bài hát YouTube bằng tool `search_youtube_music(song_name: str)`.

## Mục tiêu
- Chạy **song song** với firmware/chatbot hiện tại, không sửa code cũ.
- Expose endpoint SSE có auth token qua query: `/sse?token=...`.
- Trả về JSON text gồm `title`, `url`, `duration_seconds`.

## Cài đặt
1. Tạo môi trường Python riêng.
2. Cài dependencies:
   - `pip install -r requirements.txt`
3. Copy `.env.example` thành `.env` và sửa token.

## Chạy server
- `uvicorn app:app --host 0.0.0.0 --port 8765`

## Endpoint
- Health check: `GET /healthz`
- MCP SSE: `GET /sse?token=<MCP_AUTH_TOKEN>`

## Cách tích hợp với chatbot/backend
- Backend MCP client kết nối vào SSE endpoint ở trên.
- Khi user yêu cầu tìm nhạc YouTube, backend gọi tool `search_youtube_music`.
- Firmware ESP32 giữ nguyên logic hiện tại.

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
