
from agent import Agent


def main():
    user_msg = """
    4x4 키패드 입력을 받아 사칙연산을 수행하는 계산기를 구현해주세요.
    부동소수점 연산은 사용할 수 없고, 32비트 고정소수점(Q16.16 형식)으로 
    구현해야 합니다. LCD 16x2에 결과를 표시합니다.
    Free RTOS를 사용하세요.
    """

    agent = Agent()
    result = agent.invoke(['p1', 'p2'], user_msg)
    print(result)

if __name__ == '__main__':
    main()