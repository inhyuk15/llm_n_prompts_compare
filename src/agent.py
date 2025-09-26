from dataclasses import dataclass
from pathlib import Path
from typing import Union
from models import qwen_model, gpt_model
from typing_extensions import TypedDict
from langchain_core.prompts import ChatPromptTemplate
from langgraph.graph import StateGraph, START, END

#------------- prompt
@dataclass
class PromptEntry:
    prompt_name: str
    topic: str

prompts: list[PromptEntry]  = [
    PromptEntry('p1', 'memory safety and optimization'),
    PromptEntry('p2', 'coding standard compilance'),
    PromptEntry('p3', 'code metrics checker'),
    PromptEntry('p4', 'defensive programming'),
    PromptEntry('p5', 'safty rule for interrupts and sync,hronization'),
    PromptEntry('p6', 'structured error handling'),
    PromptEntry('p7', 'code documentation and annotation'),
]

PROMPT_INDEX:dict[str, PromptEntry] = {p.prompt_name: p for p in prompts}


def load_system_prompt(name: str) -> str:
    return Path(f'prompts/{name}.md').read_text(encoding='utf-8')
    
def make_prompt(sys_prompts: list[PromptEntry], user_msg: str) -> ChatPromptTemplate:
    merged_sys_prompt = '\n'.join(
        load_system_prompt(prompt.prompt_name) for prompt in sys_prompts
    )
    merged_sys_prompt += '\nyou must return only code.'
    return ChatPromptTemplate.from_messages([
        ('system', merged_sys_prompt),
        ('user', user_msg),
    ])


#------------- agent
class State(TypedDict):
    prompt_names: list[str]
    user_msg: str
    response: str
    


class Agent():
    def __init__(self):
        # self.llm = qwen_model
        self.llm = gpt_model
        self.graph_builder = StateGraph(State)
        
        self.graph_builder.add_node('agent', self._run_llm)
        self.graph_builder.add_edge(START, 'agent')
        self.graph_builder.add_edge('agent', END)
        
        self.chain = self.graph_builder.compile()
        
        
    def _run_llm(self, state: State):
        entries = [PROMPT_INDEX[name] for name in state['prompt_names']]
        prompt = make_prompt(entries, state['user_msg'])
        
        msg = (prompt | self.llm).invoke({})
        return {'response': msg.content}
    
    
    def invoke(self, prompt_names: Union[str, list[str]], user_msg: str = '') -> str:
        prompt_names = [prompt_names] if isinstance(prompt_names, str) else list(prompt_names)
        out = self.chain.invoke({'prompt_names': prompt_names, 'user_msg': user_msg})
        return out['response']
    