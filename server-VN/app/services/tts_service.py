from __future__ import annotations

import edge_tts

from app.config import settings


class TtsService:
    async def synthesize_mp3(self, text: str) -> bytes:
        communicator = edge_tts.Communicate(
            text=text,
            voice=settings.tts_voice,
            rate=settings.tts_rate,
            volume=settings.tts_volume,
        )

        chunks: list[bytes] = []
        async for event in communicator.stream():
            if event.get('type') == 'audio':
                chunks.append(event.get('data', b''))

        return b''.join(chunks)
