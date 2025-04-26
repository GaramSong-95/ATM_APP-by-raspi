#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <mariadb/mysql.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define ID 1
#define PW 2
#define EXIT 3
#define OK 10
#define DEL 11
#define NONE -1

#define FBDEV "/dev/fb0"
#define DB_HOST "10.10.141.211"
#define DB_USER "root2"
#define DB_PASS "OhsyBank1234!@"
#define DB_NAME "db_atm"

// 색상 정의
#define COLOR_RED 0xff0000
#define COLOR_GREEN 0x13cd00
#define COLOR_BLUE 0x1E2A38
#define COLOR_BLACK 0x000000
#define COLOR_WHITE 0xFFFFFF
#define COLOR_BUTTON 0x007BFF
#define COLOR_TEXT 0x000000  // 검은색 텍스트
#define COLOR_MAIN 0x015198

#define LCD_WIDTH 800  // LCD 해상도
#define LCD_HEIGHT 480

#define TS_MIN_X 260   // 터치스크린 최소 X
#define TS_MAX_X 3840  // 터치스크린 최대 X
#define TS_MIN_Y 410   // 터치스크린 최소 Y
#define TS_MAX_Y 3680  // 터치스크린 최대 Y

// 숫자 정의 배열
const int FONT_3x5[10][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111},  // 0
    {0b010, 0b110, 0b010, 0b010, 0b111},  // 1
    {0b111, 0b001, 0b111, 0b100, 0b111},  // 2
    {0b111, 0b001, 0b111, 0b001, 0b111},  // 3
    {0b101, 0b101, 0b111, 0b001, 0b001},  // 4
    {0b111, 0b100, 0b111, 0b001, 0b111},  // 5
    {0b111, 0b100, 0b111, 0b101, 0b111},  // 6
    {0b111, 0b001, 0b001, 0b010, 0b010},  // 7
    {0b111, 0b101, 0b111, 0b101, 0b111},  // 8
    {0b111, 0b101, 0b111, 0b001, 0b111}   // 9
};

// 영어 정의 배열열
const int FONT_5x7[26][7] = {
    {0b010, 0b101, 0b111, 0b101, 0b101, 0b101, 0b101},  // A
    {0b110, 0b101, 0b110, 0b101, 0b101, 0b101, 0b110},  // B
    {0b011, 0b100, 0b100, 0b100, 0b100, 0b100, 0b011},  // C
    {0b110, 0b101, 0b101, 0b101, 0b101, 0b101, 0b110},  // D
    {0b111, 0b100, 0b100, 0b110, 0b100, 0b100, 0b111},  // E
    {0b111, 0b100, 0b100, 0b110, 0b100, 0b100, 0b100},  // F
    {0b011, 0b100, 0b100, 0b101, 0b101, 0b101, 0b011},  // G
    {0b101, 0b101, 0b101, 0b111, 0b101, 0b101, 0b101},  // H
    {0b111, 0b010, 0b010, 0b010, 0b010, 0b010, 0b111},  // I
    {0b111, 0b010, 0b010, 0b010, 0b010, 0b110, 0b100},  // J
    {0b101, 0b101, 0b110, 0b100, 0b110, 0b101, 0b101},  // K
    {0b100, 0b100, 0b100, 0b100, 0b100, 0b100, 0b111},  // L
    {0b101, 0b111, 0b101, 0b101, 0b101, 0b101, 0b101},  // M
    {0b101, 0b111, 0b111, 0b111, 0b111, 0b111, 0b101},  // N
    {0b010, 0b101, 0b101, 0b101, 0b101, 0b101, 0b010},  // O
    {0b110, 0b101, 0b101, 0b110, 0b100, 0b100, 0b100},  // P
    {0b011, 0b100, 0b100, 0b101, 0b101, 0b101, 0b011},  // Q
    {0b110, 0b101, 0b101, 0b110, 0b101, 0b101, 0b101},  // R
    {0b011, 0b100, 0b100, 0b010, 0b001, 0b001, 0b110},  // S
    {0b111, 0b010, 0b010, 0b010, 0b010, 0b010, 0b010},  // T
    {0b101, 0b101, 0b101, 0b101, 0b101, 0b101, 0b011},  // U
    {0b101, 0b101, 0b101, 0b101, 0b101, 0b010, 0b010},  // V
    {0b101, 0b101, 0b101, 0b101, 0b111, 0b111, 0b101},  // W
    {0b101, 0b101, 0b010, 0b010, 0b010, 0b101, 0b101},  // X
    {0b101, 0b101, 0b101, 0b101, 0b010, 0b010, 0b010},  // Y
    {0b111, 0b001, 0b010, 0b010, 0b100, 0b100, 0b111},  // Z
};