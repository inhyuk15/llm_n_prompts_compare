
from pathlib import Path
from agent import Agent
from evaluation import Evaluator
from pipe_agent import PipeAgent

code_file = 'output.c'
eval_file = 'eval.md'
pipe_dir = 'pipe'

def main():
    user_msg = """
    4x4 키패드 입력을 받아 사칙연산을 수행하는 계산기를 구현해주세요.
    부동소수점 연산은 사용할 수 없고, 32비트 고정소수점(Q16.16 형식)으로 
    구현해야 합니다. LCD 16x2에 결과를 표시합니다.
    Free RTOS를 사용하세요.
    """


    apply_prompts = ['p1', 'p2']
    print('code generation...')
    # agent = Agent()
    agent = PipeAgent()
    # code = agent.invoke(apply_prompts, user_msg)
    # with open(code_file, 'w', encoding='utf-8') as f:
    #     f.write(code)
    out_dir  = Path(pipe_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for step, pname, code in agent.invoke_yield(apply_prompts, user_msg):
        file_path = out_dir / (f"out_step{step}_{pname}.c")
        file_path.write_text(code, encoding="utf-8")
        print(f'[saved] {file_path}')
    
    print('evaluation...')
    evaluator = Evaluator()
    result = evaluator.invoke(apply_prompts, code, user_msg)
    with open(eval_file, 'w', encoding='utf-8') as f:
        f.write(result)
    
    

if __name__ == '__main__':
    main()