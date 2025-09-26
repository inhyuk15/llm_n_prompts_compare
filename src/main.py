
import os
from pathlib import Path
from agent import Agent
from evaluation import Evaluator
from pipe_agent import PipeAgent
from pipe_evaluation import PipeEvaluator

BASE_DIR = Path(__file__).resolve().parent
ROOT_DIR = BASE_DIR.parent

code_file = 'output.c'
eval_file = 'eval.md'
gen_pipe_dir = 'gen_pipe'
eval_pipe_dir = 'eval_pipe'

user_query_dir = 'user_queries'

def main():
    # user_msg = """
    # 4x4 키패드 입력을 받아 사칙연산을 수행하는 계산기를 구현해주세요.
    # 부동소수점 연산은 사용할 수 없고, 32비트 고정소수점(Q16.16 형식)으로 
    # 구현해야 합니다. LCD 16x2에 결과를 표시합니다.
    # Free RTOS를 사용하세요.
    # """
    user_query_path = ROOT_DIR / user_query_dir
    for f in user_query_path.iterdir():
        user_msg = f.read_text(encoding='utf-8')
        
        apply_prompts = ['p0', 'p1', 'p2', 'p3', 'p4', 'p5', 'p6', 'p7', 'p8']
        print('code generation...')
        agent = PipeAgent()

        gen_out_dir = Path(gen_pipe_dir) / f.stem
        gen_out_dir.mkdir(parents=True, exist_ok=True)
        
        stages = list(agent.invoke_yield(apply_prompts, user_msg))
        for s in stages:
            gen_output_name = f'out_step{s.step}_{f.stem}_{s.prompt_name}.c'
            (gen_out_dir / gen_output_name).write_text(s.code, encoding='utf-8')
            print(f'[CODEGEN] {gen_output_name} created')
        
        
        print('evaluation...')
        
        evaluator = PipeEvaluator()
        
        eval_out_dir = Path(eval_pipe_dir) / f.stem
        eval_out_dir.mkdir(parents=True, exist_ok=True)
        
        eval_stages = list(evaluator.invoke_yield(stages[1:]))
        for es in eval_stages:
            eval_output_name = f'out_step{es.step}_{f.stem}_{es.prompt_name}.md'
            (eval_out_dir / eval_output_name).write_text(es.evaluation, encoding='utf-8')
            print(f'[EVAL] {eval_output_name} created')
    
    

if __name__ == '__main__':
    main()