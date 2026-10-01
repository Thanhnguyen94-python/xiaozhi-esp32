from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file='.env', env_file_encoding='utf-8', extra='ignore')

    host: str = '0.0.0.0'
    port: int = 8787
    log_level: str = 'info'

    llm_provider: str = 'gemini'

    gemini_api_key: str = ''
    gemini_model: str = 'gemini-1.5-flash'

    groq_api_key: str = ''
    groq_llm_model: str = 'llama-3.1-8b-instant'
    groq_stt_model: str = 'whisper-large-v3'

    openai_api_key: str = ''
    openai_stt_model: str = 'whisper-1'

    tts_voice: str = 'vi-VN-HoaiMyNeural'
    tts_rate: str = '+0%'
    tts_volume: str = '+0%'

    ws_protocol_version: int = 3
    ws_audio_sample_rate: int = 24000
    ws_audio_frame_duration: int = 60


settings = Settings()
