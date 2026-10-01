from __future__ import annotations

import io

import httpx

from app.config import settings


class SttService:
    async def transcribe(self, audio_bytes: bytes, file_name: str = 'audio.wav', language: str = 'vi') -> str:
        if settings.groq_api_key:
            return await self._groq_whisper(audio_bytes, file_name=file_name, language=language)
        if settings.openai_api_key:
            return await self._openai_whisper(audio_bytes, file_name=file_name, language=language)
        raise RuntimeError('Missing STT credentials: set GROQ_API_KEY or OPENAI_API_KEY')

    async def _groq_whisper(self, audio_bytes: bytes, file_name: str, language: str) -> str:
        url = 'https://api.groq.com/openai/v1/audio/transcriptions'
        headers = {'Authorization': f'Bearer {settings.groq_api_key}'}
        files = {
            'file': (file_name, io.BytesIO(audio_bytes), 'application/octet-stream'),
            'model': (None, settings.groq_stt_model),
            'language': (None, language),
            'response_format': (None, 'json'),
        }

        async with httpx.AsyncClient(timeout=60) as client:
            resp = await client.post(url, headers=headers, files=files)
            resp.raise_for_status()
            data = resp.json()

        text = (data.get('text') or '').strip()
        return text

    async def _openai_whisper(self, audio_bytes: bytes, file_name: str, language: str) -> str:
        url = 'https://api.openai.com/v1/audio/transcriptions'
        headers = {'Authorization': f'Bearer {settings.openai_api_key}'}
        files = {
            'file': (file_name, io.BytesIO(audio_bytes), 'application/octet-stream'),
            'model': (None, settings.openai_stt_model),
            'language': (None, language),
            'response_format': (None, 'json'),
        }

        async with httpx.AsyncClient(timeout=60) as client:
            resp = await client.post(url, headers=headers, files=files)
            resp.raise_for_status()
            data = resp.json()

        return (data.get('text') or '').strip()
