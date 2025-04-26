#!/bin/bash

# 작업 디렉토리로 이동
cd /home/willtek/work/practice_device_bd/IntelBankAtm

CCTV_DIR="./cctv"

# CCTV 디렉토리가 없으면 생성
if [ ! -d "$CCTV_DIR" ]; then
    mkdir -p "$CCTV_DIR"
    echo "Created CCTV directory: $CCTV_DIR"
fi

./intel_bank_atm_client ./cctv &

./intel*bank*atm_login