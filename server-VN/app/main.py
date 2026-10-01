from __future__ import annotations

import asyncio
import base64
import json
import uuid
from dataclasses import dataclass, field
from typing import Any

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse

from app.config import settings
from app.services.llm_service import LlmService
from app.services.stt_service import SttService
from app.services.tts_service import TtsService
from app.tools_registry import ToolRegistry


app = FastAPI(title='Server VN for bread-compact-wifi', version='0.1.0')

llm_service = LlmService()
stt_service = SttService()
tts_service = TtsService()
tool_registry = ToolRegistry()


@dataclass
class SessionState:
    session_id: str = field(default_factory=lambda: str(uuid.uuid4()))
    audio_buffer: bytearray = field(default_factory=bytearray)
    listening_active: bool = False
    listen_seq: int = 0
    processing: bool = False


@app.get('/health')
async def health() -> JSONResponse:
    return JSONResponse({'ok': True, 'service': 'server-VN', 'board': 'bread-compact-wifi'})


@app.websocket('/ws')
async def websocket_endpoint(ws: WebSocket) -> None:
    await ws.accept()
    session = SessionState()

    try:
        while True:
            message = await ws.receive()

            if 'bytes' in message and message['bytes'] is not None:
                # Binary audio frame (for custom pipeline). We buffer and wait for audio.commit.
                session.audio_buffer.extend(message['bytes'])
                continue

            if 'text' not in message or message['text'] is None:
                continue

            raw_text = message['text']
            payload = _safe_json_load(raw_text)
            if payload is None:
                await ws.send_json({'type': 'error', 'message': 'Invalid JSON'})
                continue

            msg_type = str(payload.get('type', '')).strip()

            if msg_type == 'hello':
                await ws.send_json({
                    'type': 'hello',
                    'transport': 'websocket',
                    'session_id': session.session_id,
                    'audio_params': {
                        'format': 'opus',
                        'sample_rate': settings.ws_audio_sample_rate,
                        'channels': 1,
                        'frame_duration': settings.ws_audio_frame_duration,
                    },
                })
                continue

            if msg_type == 'ping':
                await ws.send_json({'type': 'pong'})
                continue

            if msg_type == 'listen':
                state = str(payload.get('state', '')).strip()
                if state in {'start', 'detect'}:
                    session.listening_active = True
                    session.listen_seq += 1
                    current_seq = session.listen_seq
                    asyncio.create_task(_auto_commit_after_timeout(ws, session, current_seq, 4.0))
                    continue

                if state == 'stop':
                    session.listening_active = False
                    await _process_listen_buffer(ws, session)
                    continue

                # Unknown listen state: ignore silently for firmware compatibility
                continue

            if msg_type == 'abort':
                session.audio_buffer.clear()
                session.listening_active = False
                continue

            if msg_type in {'tools.list', 'tools/list'}:
                cursor = int(payload.get('cursor') or 0)
                limit = int(payload.get('limit') or 16)
                page = tool_registry.list_tools(cursor=cursor, limit=limit)
                await ws.send_json({
                    'type': 'tools.list.result',
                    'items': page['items'],
                    'next_cursor': page['next_cursor'],
                    'total': page['total'],
                })
                continue

            if msg_type in {'tools.call', 'tools/call'}:
                name = str(payload.get('name', ''))
                arguments = payload.get('arguments') or {}
                result = tool_registry.call_tool(name=name, arguments=arguments)
                await ws.send_json({'type': 'tools.call.result', **result})
                continue

            if msg_type in {'user.text', 'chat.text'}:
                user_text = str(payload.get('text', '')).strip()
                if not user_text:
                    await ws.send_json({'type': 'error', 'message': 'text is empty'})
                    continue
                await _handle_text_pipeline(ws, user_text)
                continue

            if msg_type == 'stt.transcribe':
                audio_b64 = str(payload.get('audio_base64', ''))
                language = str(payload.get('language', 'vi'))
                if not audio_b64:
                    await ws.send_json({'type': 'error', 'message': 'audio_base64 is required'})
                    continue
                audio_bytes = base64.b64decode(audio_b64)
                text = await stt_service.transcribe(audio_bytes, file_name='audio.wav', language=language)
                await ws.send_json({'type': 'stt.result', 'text': text})
                if text:
                    await _handle_text_pipeline(ws, text)
                continue

            if msg_type == 'audio.commit':
                if not session.audio_buffer:
                    await ws.send_json({'type': 'error', 'message': 'audio buffer is empty'})
                    continue
                language = str(payload.get('language', 'vi'))
                audio_bytes = bytes(session.audio_buffer)
                session.audio_buffer.clear()

                text = await stt_service.transcribe(audio_bytes, file_name='audio.bin', language=language)
                await ws.send_json({'type': 'stt.result', 'text': text})
                if text:
                    await _handle_text_pipeline(ws, text)
                continue

            # Ignore unknown messages to stay firmware-compatible.
            continue

    except WebSocketDisconnect:
        return
    except Exception as ex:
        await ws.send_json({'type': 'error', 'message': str(ex)})


async def _handle_text_pipeline(ws: WebSocket, text: str) -> None:
    answer = await llm_service.generate(text)
    await ws.send_json({'type': 'assistant.text', 'text': answer})

    audio_mp3 = await tts_service.synthesize_mp3(answer)
    audio_b64 = base64.b64encode(audio_mp3).decode('utf-8')
    await ws.send_json({
        'type': 'assistant.audio',
        'format': 'mp3',
        'audio_base64': audio_b64,
    })


async def _handle_device_voice_pipeline(ws: WebSocket, text: str) -> None:
    answer = await llm_service.generate(text)

    # Firmware-compatible JSON events
    await ws.send_json({'type': 'llm', 'emotion': 'neutral'})
    await ws.send_json({'type': 'tts', 'state': 'start'})
    await ws.send_json({'type': 'tts', 'state': 'sentence_start', 'text': answer})
    await ws.send_json({'type': 'tts', 'state': 'stop'})


async def _process_listen_buffer(ws: WebSocket, session: SessionState) -> None:
    if session.processing:
        return
    session.processing = True
    try:
        if not session.audio_buffer:
            await ws.send_json({'type': 'stt', 'text': ''})
            await _handle_device_voice_pipeline(ws, 'Xin lỗi, mình chưa nghe rõ. Bạn nói lại giúp mình nhé.')
            return

        audio_bytes = bytes(session.audio_buffer)
        session.audio_buffer.clear()

        try:
            text = await stt_service.transcribe(audio_bytes, file_name='audio.ogg', language='vi')
        except Exception:
            text = ''

        await ws.send_json({'type': 'stt', 'text': text})
        if text.strip():
            await _handle_device_voice_pipeline(ws, text.strip())
        else:
            await _handle_device_voice_pipeline(ws, 'Xin lỗi, mình chưa nghe rõ. Bạn nói lại giúp mình nhé.')
    finally:
        session.processing = False


async def _auto_commit_after_timeout(
    ws: WebSocket,
    session: SessionState,
    listen_seq: int,
    timeout_seconds: float,
) -> None:
    await asyncio.sleep(timeout_seconds)
    if session.listen_seq != listen_seq:
        return
    if not session.listening_active:
        return
    # Some firmware modes may not send listen.stop. Force process current buffer.
    session.listening_active = False
    await _process_listen_buffer(ws, session)


def _safe_json_load(raw: str) -> dict[str, Any] | None:
    try:
        data = json.loads(raw)
        if isinstance(data, dict):
            return data
        return None
    except Exception:
        return None
