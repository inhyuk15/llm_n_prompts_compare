from dataclasses import dataclass


@dataclass
class StageResult:
    step: int
    prompt_name: str
    system_prompt: str
    code: str
    
@dataclass
class StageEvalResult:
    step: int
    prompt_name: str
    evaluation: str
    