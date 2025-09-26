from pathlib import Path
from langchain_core.prompts import ChatPromptTemplate


def load_system_prompt(name: str) -> str:
    return Path(f'prompts/{name}.md').read_text(encoding='utf-8')

RAW_START = "{% raw %}"
RAW_END = "{% endraw %}"
    
def build_generation_prompt(system_text: str, user_msg: str) -> ChatPromptTemplate:
    """
    초기 코드 '생성'용 프롬프트 (첫 에이전트)
    """
    sys = (
        f"{RAW_START}{system_text.rstrip()}\n"
        "Return only C code. Do not include explanations outside code blocks."
        f'{RAW_END}'
    )
    usr = f'{RAW_START}{user_msg}{RAW_END}'

    return ChatPromptTemplate.from_messages(
        [("system", sys), ("user", usr)],
        template_format="jinja2"
    )


def build_refine_prompt(system_text: str, prev_code: str) -> ChatPromptTemplate:
    """
    이전 단계 코드 '수정/강화'용 프롬프트 (후속 에이전트)
    """
    sys = (
        f"{RAW_START}{system_text.rstrip()}\n"
        "Return only the final C code after applying these rules."
        f'{RAW_END}'
    )
    user = (
        "Refine the following C code to fully satisfy the system rules. "
        "Preserve functionality, keep it compilable, and avoid adding external dependencies.\n\n"
        f"{RAW_START}Here is the current code:\n```c\n{prev_code}\n```{RAW_END}"
    )
    return ChatPromptTemplate.from_messages(
        [("system", sys),("user", user)],
        template_format="jinja2"
    )

