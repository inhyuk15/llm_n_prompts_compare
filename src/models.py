from langchain_openai import ChatOpenAI
from settings import settings


qwen_model = ChatOpenAI(
    model="Qwen/Qwen3-32B",
    openai_api_base=settings.openai_base_url,
    openai_api_key=settings.openai_api_key,
)

gpt_model = ChatOpenAI(
    model='gpt-4.1',
    openai_api_base="https://api.openai.com/v1",
    openai_api_key=settings.openai_api_key,
)