# Server VN (independent) for bread-compact-wifi

Server nay duoc tao doc lap trong `server-VN/`, khong sua bat ky file ESP32 hoac server cu.

## 1) Cau truc thu muc

```text
server-VN/
├─ app/
│  ├─ __init__.py
│  ├─ config.py
│  ├─ main.py
│  ├─ tools_registry.py
│  └─ services/
│     ├─ __init__.py
│     ├─ llm_service.py
│     ├─ stt_service.py
│     └─ tts_service.py
├─ .env.example
├─ requirements.txt
└─ README.md
```

## 2) Chuc nang chinh

- WebSocket endpoint: `ws://<server-ip>:8787/ws`
- Health check: `GET /health`
- Handshake `hello` tuong thich flow websocket cua firmware
- Tool registry phan trang (`tools.list`) de tranh gioi han 32 tool
- Pipeline AI:
  - STT: Groq Whisper (hoac OpenAI Whisper)
  - LLM: Gemini hoac Groq
  - TTS: Edge-TTS (giong tieng Viet)

## 3) Cai dat & chay

### 3.1 Tao virtual env

```bash
cd server-VN
python -m venv .venv
```

Windows PowerShell:

```powershell
.\.venv\Scripts\Activate.ps1
```

### 3.2 Cai dependency

```bash
pip install -r requirements.txt
```

### 3.3 Tao file env

```bash
cp .env.example .env
```

Windows PowerShell:

```powershell
Copy-Item .env.example .env
```

Cap nhat cac key trong `.env`:

- `GEMINI_API_KEY` (neu dung Gemini)
- `GROQ_API_KEY` (neu dung Groq LLM/STT)
- `OPENAI_API_KEY` (optional fallback Whisper)

### 3.4 Chay server

```bash
uvicorn app.main:app --host 0.0.0.0 --port 8787 --reload
```

## 4) Message mau qua WebSocket

### Handshake
Client gui:

```json
{"type":"hello","version":3,"transport":"websocket","audio_params":{"format":"opus","sample_rate":16000,"channels":1,"frame_duration":60}}
```

Server tra:

```json
{"type":"hello","transport":"websocket","session_id":"...","audio_params":{"format":"opus","sample_rate":24000,"channels":1,"frame_duration":60}}
```

### tools.list (phan trang)
Request:

```json
{"type":"tools.list","cursor":0,"limit":16}
```

Response:

```json
{"type":"tools.list.result","items":[...],"next_cursor":16,"total":42}
```

### STT -> LLM -> TTS
Request text:

```json
{"type":"user.text","text":"Xin chao robot"}
```

Response:

1) `assistant.text`
2) `assistant.audio` (mp3 base64)

Request STT (audio base64):

```json
{"type":"stt.transcribe","audio_base64":"<base64>","language":"vi"}
```

Server se tra `stt.result` roi tiep tuc LLM -> TTS.

## 5) Cach doi qua lai server cu/server moi (khong sua code firmware)

### Cach A (uu tien): cap nhat setting websocket tren thiet bi
Du an dang doc cau hinh tu `Settings("websocket")` trong firmware (`url`, `token`, `version`).
Ban doi `websocket.url` sang:

- Server moi: `ws://<IP-may-chay-server-VN>:8787/ws`
- Server cu: URL cu dang dung truoc do

### Cach B: OTA config (neu ban dang day websocket config qua OTA)
Firmware co parse muc `websocket` trong OTA config. Co the chuyen nhanh URL o lop OTA de rollback de dang.

## 6) Ghi chu tuong thich

- Server nay khong dong vao code ESP32 hien tai.
- Neu can dung binary Opus stream day du theo giao thuc cu, ban co the mo rong xu ly `audio.commit` + decoder Opus/FFmpeg.
- Muc tieu ban dau cua ban (vuot gioi han 32 tools) da duoc xu ly bang phan trang `tools.list`.
