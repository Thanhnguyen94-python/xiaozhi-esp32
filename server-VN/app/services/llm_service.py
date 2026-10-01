from __future__ import annotations

from typing import Any

import httpx

from app.config import settings


class LlmService:
    async def generate(self, user_text: str, system_prompt: str = 'Ban la tro ly AI cho robot bread-compact-wifi.') -> str:
        provider = settings.llm_provider.lower().strip()
        if provider == 'groq':
            return await self._groq_chat(user_text=user_text, system_prompt=system_prompt)
        return await self._gemini_chat(user_text=user_text, system_prompt=system_prompt)

    async def _gemini_chat(self, user_text: str, system_prompt: str) -> str:
        if not settings.gemini_api_key:
            raise RuntimeError('Missing GEMINI_API_KEY')

        url = f"https://generativelanguage.googleapis.com/v1beta/models/{settings.gemini_model}:generateContent"
        params = {'key': settings.gemini_api_key}
        payload: dict[str, Any] = {
            'contents': [{'role': 'user', 'parts': [{'text': user_text}]}],
            'systemInstruction': {'parts': [{'text': system_prompt}]},
        }

        async with httpx.AsyncClient(timeout=30) as client:
            resp = await client.post(url, params=params, json=payload)
            resp.raise_for_status()
            data = resp.json()

        candidates = data.get('candidates') or []
        if not candidates:
            return 'Xin loi, toi chua co cau tra loi.'

        parts = candidates[0].get('content', {}).get('parts', [])
        text = ''.join(p.get('text', '') for p in parts if isinstance(p, dict)).strip()
        return text or 'Xin loi, toi chua co cau tra loi.'

    async def _groq_chat(self, user_text: str, system_prompt: str) -> str:
        if not settings.groq_api_key:
            raise RuntimeError('Missing GROQ_API_KEY')

        url = 'https://api.groq.com/openai/v1/chat/completions'
        headers = {'Authorization': f'Bearer {settings.groq_api_key}'}
        payload = {
            'model': settings.groq_llm_model,
            'messages': [
                {'role': 'system', 'content': system_prompt},
                {'role': 'user', 'content': user_text},
            ],
            'temperature': 0.4,
        }

        async with httpx.AsyncClient(timeout=30) as client:
            resp = await client.post(url, headers=headers, json=payload)
            resp.raise_for_status()
            data = resp.json()

        choices = data.get('choices') or []
        if not choices:
            return 'Xin loi, toi chua co cau tra loi.'

        return choices[0].get('message', {}).get('content', '').strip() or 'Xin loi, toi chua co cau tra loi.'
