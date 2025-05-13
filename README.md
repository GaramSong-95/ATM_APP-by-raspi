# Project-ATM
라즈베리파이4를 활용한 ATM 구현 프로젝트

![image](https://github.com/user-attachments/assets/5bc80aac-ca50-491a-b5cc-c4d1ffabcdf7)

![image](https://github.com/user-attachments/assets/8951edf5-9ff8-4eb8-b117-c1c6cafbaeea)


## 🛠 Tech Stack

<!-- 하드웨어 및 시스템 -->
![Raspberry Pi](https://img.shields.io/badge/Raspberry%20Pi-C51A4A?style=for-the-badge&logo=raspberrypi&logoColor=white)
![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)

<!-- 언어 및 프로그래밍 -->
![C](https://img.shields.io/badge/C-00599C?style=for-the-badge&logo=c&logoColor=white)
![Shell Script](https://img.shields.io/badge/Shell_Script-4EAA25?style=for-the-badge&logo=gnu-bash&logoColor=white)

<!-- 데이터베이스 -->
![MariaDB](https://img.shields.io/badge/MariaDB-003545?style=for-the-badge&logo=mariadb&logoColor=white)

<!-- 영상처리 및 포맷 -->
![FFmpeg](https://img.shields.io/badge/FFmpeg-007808?style=for-the-badge&logo=ffmpeg&logoColor=white)

<!-- 기타 도구 -->
![Framebuffer UI](https://img.shields.io/badge/Framebuffer-1E90FF?style=for-the-badge)
![Multithreading](https://img.shields.io/badge/Multithreading-FF8C00?style=for-the-badge)

## 1. 프로젝트 개요

**본 프로젝트는 Raspberry Pi 4와 LCD 디스플레이만을 사용하여 구현한 장난감 ATM 시스템입니다.
하드웨어 자원을 최소화하고, 효율적인 UI와 사용자 인터랙션 구현을 목표로 개발하였습니다.
사용자는 로그인 후 송금, 입금, 잔액 조회 기능을 수행할 수 있습니다.**

## 2. 주요 개발 내용 및 기술

### ● 프레임버퍼 기반 UI 구현
추가 라이브러리 없이 프레임버퍼와 비트맵 방식으로 UI 직접 구현

화면에 원, 사각형, 숫자 등 기본 도형을 그리는 방식으로 UI 요소 구성

디스플레이 하드웨어 메모리에 직접 접근하여 낮은 지연 시간과 빠른 응답속도 확보

리소스를 최소화해 저성능 보드에서도 구동 가능

### ● 터치 기능 구현 (담당 역할)
터치 구현

터치된 좌표를 통해 숫자 키패드 입력 구현

ID, 비밀번호, 계좌번호, 금액 입력 시 입력 위치에 따라 숫자 출력

입력 커서 구현: 현재 입력 위치에 커서 깜빡임 처리 및 입력 시 커서 이동

2개의 스레드를 활용하여 커서와 숫자 출력의 연동 구현 → 자연스러운 사용자 입력 경험 제공

### ● 프로세스 전환 구조 (화면 전환 처리)
Linux 기반에서 execl() 함수 사용

화면 전환 시 기존 프로세스 종료 및 다음 화면 프로세스 실행

### ● DB 및 서버 연동
MariaDB를 사용하여 사용자 정보(ID, PW 등) 저장 및 검증

로그인 성공 시 메뉴 진입 → 송금 / 입금 / 잔액 확인 기능 제공

송금, 입금 후 자동 로그아웃 / 잔액 확인 후 메뉴 화면 복귀

### ● 영상 처리 기능 추가
CCTV 모듈을 통해 60초마다 영상 촬영 및 서버 저장

YUV to RGB 변환 후, MP4 포맷으로 인코딩하여 저장

## 3. 트러블슈팅 경험

### ● 터치 좌표값 문제 해결
디바이스 드라이버를 통해 터치 좌표를 읽을 수 있었지만,
실제 터치 위치와 일치하지 않는 값이 출력되어 좌표 변환 이슈 발생

디바이스 드라이버 코드를 직접 분석하여 좌표 처리 로직을 파악

팀원과 협업을 통해 좌표 변환 수식 도출 → 정확한 입력 좌표 매핑 성공

### ● 커서 깜빡임과 글자 출력 동기화 문제
커서가 글자 입력 위치를 실시간으로 따라가는 기능 구현이 어려웠음

숫자 입력 시 상태값을 별도로 관리하여 커서 스레드와 연동

커서가 자연스럽게 글자 뒤로 이동하며 깜빡이는 형태로 개선

제약 조건을 명확히 정의하여 글자 출력과 커서 움직임 간 충돌 방지

## 4. 프로젝트를 통해 얻은 것
리눅스 기반의 저수준 디바이스 접근 및 프로세스 관리 경험

사용자 인터페이스(UI) 직접 구현 및 멀티스레드 연동 기술 습득

터치 디바이스 드라이버 분석과 트러블슈팅을 통해
문제를 끝까지 파고드는 끈기와 협업 능력을 키움
