#!/bin/bash

# 프로세스 종료 함수 정의
kill_process() {
    if pgrep -f "$1" > /dev/null; then
        pkill -f "$1"
        echo "$1 프로세스를 종료했습니다."
    fi
}

# intel bank atm 관련 프로세스들 종료
kill_process "intel.*bank.*atm_login"
kill_process "intel.*bank.*atm_main"
kill_process "intel.*bank.*atm_deposit"
kill_process "intel.*bank.*atm_withdraw"
kill_process "intel.*bank.*atm_transfer"
kill_process "intel.*bank.*atm_check_balance"
kill_process "intel.*bank.*atm_end"
kill_process "intel.*bank.*atm_camera"
kill_process "intel.*bank.*atm_client"

if [ -z "$remaining_processes" ]; then
    echo "모든 프로세스가 정상적으로 종료되었습니다."
else
    echo "아직 실행 중인 프로세스:"
    echo "$remaining_processes"
fi