
## 프로젝트 목적
이 프로젝트는 **Section 1~8**에 대해 다음 항목의 performance 비교할 수 있게 함.

1. **개별 매칭 (Single Section Test)**  
   - 시스템 프롬프트 S1 ~ S8 (각 Section 규칙)  
   - 사용자 질문 U1 ~ U8 (각 Section 대응 질문)  
   - 수행: `(S1 + U1), (S2 + U2), ... (S8 + U8)`  

2. **혼합 매칭 (Combined Section Test)**  
   - 통합 시스템 프롬프트: `S_all = S1 + S2 + ... + S8`  
   - 사용자 질문: U1 ~ U8 전체  
   - 수행: `(S_all + U1), (S_all + U2), ... (S_all + U8)`  

3. **무지시 (Zero Instruction Test)**  
   - 시스템 프롬프트 없이  
   - 사용자 질문: U1 ~ U8 전체  
   - 수행: `(U1), (U2), ... (U8)`

<br><br>


# Agent가 수행해야 하는 Section 1~8

## Section 1
제공된 코드를 메모리 안전하게 개선하세요
- 모든 할당은 정적으로 할당되어야함
- 스택 사용량은 계산되어야하며 함수에 주석으로 명시해야함
- 버퍼 오버플로우 방지를 위한 경계 검사
- 전역 변수 최소화 및 static으로 스코프 제한

## Section 2
코드 복잡도를 측정 가능한 수준으로 낮춰주세요.
- Cyclomatic Complexity 10 이하로 함수 분할
- 중첩 깊이 최대 4레벨 제한
- 함수당 최대 50줄 제한
- 함수 매개변수 5개 이하

