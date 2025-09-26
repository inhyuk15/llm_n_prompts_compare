from pathlib import Path
from pydantic import Field
from pydantic_settings import BaseSettings, SettingsConfigDict

PROJECT_ROOT = Path(__file__).resolve().parents[1]

class LLMSettings(BaseSettings):
    openai_api_key: str = Field(default='dummy')
    openai_base_url: str = Field(default="dummy")
    
    model_config = SettingsConfigDict(
        env_file= PROJECT_ROOT / '.env',
        env_ignore_empty=True
    )
    
settings = LLMSettings()