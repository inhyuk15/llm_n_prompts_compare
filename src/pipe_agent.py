"""
처음 프롬프트는 generation,
두번째 프롬프트부터는 edit 적용.
각 단계의 결과물을 yield로 반환.
"""

from typing import Iterator
from typing_extensions import TypedDict
from langgraph.graph import StateGraph, START, END
from models import gpt_model
from util.pipe_types import StageResult
from util.prompt_util import load_system_prompt, build_generation_prompt, build_refine_prompt

class PipeState(TypedDict):
    prompt_names: list[str]
    user_msg: str
    code: str
    step: int


def _extract_code_only(text: str) -> str:
    """
    모델이 코드펜스(```c ... ```)로 감싸거나 앞뒤 설명을 붙여도
    C 코드만 깔끔히 뽑아내기 위한 유틸.
    """
    import re
    if "```" in text:
        # ```c ... ``` 또는 ``` ... ``` 중 가장 큰 블록 추출
        blocks = re.findall(r"```[a-zA-Z]*\n(.*?)```", text, flags=re.DOTALL)
        if blocks:
            return blocks[0].strip()
    # 펜스가 없으면 전체를 코드로 간주하되 앞뒤 공백 제거
    return text.strip()
    

class PipeAgent():
    def __init__(self):
        self.llm = gpt_model
        
    def _make_graph(self, prompt_names: str | list[str]):
        graph = StateGraph(PipeState)
        
        node_ids = []
        for idx, pname in enumerate(prompt_names):
            node_id = f'agent_{idx}_{pname}'
            node_ids.append(node_id)
            
            def make_node(pname: str, idx: int):
                def _node(state: PipeState):
                    system_text = load_system_prompt(pname)
                    if state['code'].strip() == '':
                        # generate code - [1]
                        prompt = build_generation_prompt(system_text, state['user_msg'])
                    else:
                        # edit code - [2, last_idx]
                        prompt = build_refine_prompt(system_text, state['code'])
                    
                    msg = (prompt | self.llm).invoke({})
                    new_code = _extract_code_only(msg.content)
                    return {
                        'code': new_code,
                        'step': idx,
                    }
                return _node
            graph.add_node(node_id, make_node(pname, idx))
        
        if node_ids:
            graph.add_edge(START, node_ids[0])
            for i in range(len(node_ids) -1 ):
                graph.add_edge(node_ids[i], node_ids[i+1])
            graph.add_edge(node_ids[-1], END)
        else:
            assert False, "no prompt"
        
        return graph.compile()
    
    def invoke(self, prompt_names: str | list[str], user_msg: str = '') -> str:
        names = [prompt_names] if isinstance(prompt_names, str) else list(prompt_names)
        chain = self._make_graph(names)
        out = chain.invoke({
            'prompt_names': names,
            'user_msg': user_msg,
            'code': '',
            'step': -1,
        })

        return out['code']

# ------- graph 생성 없이 매번 llm 호출.
    def invoke_yield(self, prompt_names: str | list[str], user_msg: str = '') -> Iterator[StageResult]:
        names = [prompt_names] if isinstance(prompt_names, str) else list(prompt_names)
        code = ""
        
        # code_gen: bool = False
        for step, pname in enumerate(names, start=0):
            system_text = load_system_prompt(pname)
            if code.strip() == "":
                prompt = build_generation_prompt(system_text, user_msg)
            else:
                prompt = build_refine_prompt(system_text, code)
            
            msg = (prompt | self.llm).invoke({})
            code = _extract_code_only(msg.content)
            # # 처음 만든 코드를 계속 이용.
            # if not code_gen:
            #     code = _extract_code_only(msg.content)
            #     code_gen = True
            
            # yield step, pname, code
            yield StageResult(step=step, prompt_name=pname, system_prompt=system_text, code=code)