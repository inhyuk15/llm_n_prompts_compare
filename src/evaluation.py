from pathlib import Path
from typing import NotRequired
from models import gpt_model
from langgraph.graph import StateGraph, START, END
from typing_extensions import TypedDict
from langchain_core.messages import SystemMessage, HumanMessage

#------------- agent
class State(TypedDict):
    prompt_names: list[str]
    user_msg: str
    code: str
    response: NotRequired[str]

def load_evaluation_prompt() -> str:
    return Path(f'prompts/evaluation.md').read_text(encoding='utf-8')
    
def load_prompt(name: str) -> str:
    """prompts/{name}.md"""
    return (Path("prompts") / f"{name}.md").read_text(encoding='utf-8')

def build_human_prompt(state: State) -> str:
    rules_text = '\n\n'.join(load_prompt(n) for n in state['prompt_names'])
    user_msg = state.get('user_msg', '').strip()
    code = state['code']

    return (
        "[RULES]\n"
        f"{rules_text}\n\n"
        "[USER]\n"
        f"{user_msg}\n\n"
        "[CODE]\n"
        "```c\n"
        f"{code}\n"
        "```\n\n"
        "위 입력만 사용하여 규정 준수 여부를 판단하고, 시스템 프롬프트의 출력 형식을 STRICT하게 따르세요."
    )

class Evaluator():
    def __init__(self):
        self.llm = gpt_model
        
        self.graph_builder = StateGraph(State)
        self.graph_builder.add_node('agent', self._run_llm)
        self.graph_builder.add_edge(START, 'agent')
        self.graph_builder.add_edge('agent', END)
        
        self.chain = self.graph_builder.compile()
            
            
    def _run_llm(self, state: State):
        system_prompt = load_evaluation_prompt()
        human_prompt = build_human_prompt(state)
        
        msgs = [
            SystemMessage(content=system_prompt),
            HumanMessage(content=human_prompt),
        ]
        ai_msg = self.llm.invoke(msgs)
        return {"response": ai_msg.content}
    
    def invoke(self, prompt_names: str | list[str], code: str, user_msg: str = '') -> str:
        if isinstance(prompt_names, str):
            prompt_names = [prompt_names]
            
        out = self.chain.invoke({'prompt_names': list(prompt_names), 'user_msg': user_msg, 'code': code})
        return out['response']
        
        