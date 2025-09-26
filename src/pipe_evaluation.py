
from typing import Iterator
from util.pipe_types import StageEvalResult, StageResult
from evaluation import Evaluator

class PipeEvaluator:
    def __init__(self):
        self.evaluator = Evaluator()
    
    def invoke_yield(self, stages: list[StageResult]) -> Iterator[StageEvalResult]:
        if not stages:
            assert False, "no stage in evaluation"
        
        for s in stages:
            applied = [t.prompt_name for t in stages[: s.step]]
            md = self.evaluator.invoke(applied, s.code, user_msg='')
            yield StageEvalResult(step=s.step, prompt_name=s.prompt_name, evaluation=md)
            
            