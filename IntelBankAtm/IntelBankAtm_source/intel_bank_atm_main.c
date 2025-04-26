#include "intel_bank_atm_header.h"

pid_t pid;

int scale_value(int value, int min, int max, int new_min,
                int new_max) {  // 터치스크린 각 번호의 값을 화면에 맞게 변환
  return ((value - min) * (new_max - new_min) / (max - min)) + new_min;
}

int touch_State;
int last_x = -1, last_y = -1;
bool touch_active = false;

void handle_touch_event(int screen_x, int screen_y, bool touch_down,
                        char *user_ID) {
  if (!touch_down) {
    last_x = -1;  // Reset when touch is lifted
    last_y = -1;
    touch_active = false;
    return;
  }

  if (screen_x == last_x && screen_y == last_y && touch_active)
    return;  // Prevent duplicate actions

  last_x = screen_x;
  last_y = screen_y;
  touch_active = true;

  if (screen_x >= 20 && screen_x <= 260 && screen_y >= 180 && screen_y <= 360) {
    execl("./intel_bank_atm_deposit", "./intel_bank_atm_deposit", user_ID,
          NULL);

  } else if (screen_x >= 280 && screen_x <= 520 && screen_y >= 180 &&
             screen_y <= 360) {
    execl("./intel_bank_atm_withdrawal", "./intel_bank_atm_withdrawal", user_ID,
          NULL);

  } else if (screen_x >= 540 && screen_x <= 800 && screen_y >= 180 &&
             screen_y <= 360) {
    execl("./intel_bank_atm_transfer", "./intel_bank_atm_transfer", user_ID,
          NULL);
  } else if (screen_x >= 500 && screen_x <= 780 && screen_y >= 400 &&
             screen_y <= 450) {
    execl("./intel_bank_atm_check_balance", "./intel_bank_atm_check_balance",
          user_ID, NULL);
  }
  //home
  else if (screen_x >= 485 && screen_x <= 520 && screen_y >= 40 &&
             screen_y <= 70) {
    execl("./intel_bank_atm_login", "./intel_bank_atm_login",
           NULL);
  }
}

// 원을 그리는 함수 (모서리를 둥글게 하기 위해 사용)
void draw_circle(int x0, int y0, int radius, unsigned int color,
                 struct fb_var_screeninfo *vip, struct fb_fix_screeninfo *fip,
                 char *map) {
  int x = radius, y = 0;
  int err = 0;

  while (x >= y) {
    int locations[] = {
        (x0 + x) + (y0 + y) * fip->line_length / (vip->bits_per_pixel / 8),
        (x0 + y) + (y0 + x) * fip->line_length / (vip->bits_per_pixel / 8),
        (x0 - y) + (y0 + x) * fip->line_length / (vip->bits_per_pixel / 8),
        (x0 - x) + (y0 + y) * fip->line_length / (vip->bits_per_pixel / 8),
        (x0 - x) + (y0 - y) * fip->line_length / (vip->bits_per_pixel / 8),
        (x0 - y) + (y0 - x) * fip->line_length / (vip->bits_per_pixel / 8),
        (x0 + y) + (y0 - x) * fip->line_length / (vip->bits_per_pixel / 8),
        (x0 + x) + (y0 - y) * fip->line_length / (vip->bits_per_pixel / 8),
    };

    for (int i = 0; i < 8; i++) {
      if (vip->bits_per_pixel == 32) {
        *(unsigned int *)(map + locations[i] * 4) = color;
      } else {
        int r = color >> 16;
        int g = (color >> 8) & 0xff;
        int b = color & 0xff;
        *(unsigned short *)(map + locations[i] * 2) =
            (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
      }
    }

    y++;
    if (err <= 0) {
      err += 2 * y + 1;
    } else {
      x--;
      err += 2 * (y - x) + 1;
    }
  }
}

// 둥근 사각형을 그리는 함수
void draw_rounded_rect(int x, int y, int w, int h, int radius,
                       unsigned int color, struct fb_var_screeninfo *vip,
                       struct fb_fix_screeninfo *fip, char *map) {
  // 모서리에 원 그리기
  draw_circle(x + radius, y + radius, radius, color, vip, fip, map);
  draw_circle(x + w - radius - 1, y + radius, radius, color, vip, fip, map);
  draw_circle(x + radius, y + h - radius - 1, radius, color, vip, fip, map);
  draw_circle(x + w - radius - 1, y + h - radius - 1, radius, color, vip, fip,
              map);

  // 상하 직사각형
  for (int i = x + radius; i < x + w - radius; i++) {
    draw_circle(i, y + radius, radius, color, vip, fip, map);
    draw_circle(i, y + h - radius - 1, radius, color, vip, fip, map);
  }

  // 좌우 직사각형
  for (int i = y + radius; i < y + h - radius; i++) {
    draw_circle(x + radius, i, radius, color, vip, fip, map);
    draw_circle(x + w - radius - 1, i, radius, color, vip, fip, map);
  }

  // 중앙 직사각형
  for (int yy = y + radius; yy < y + h - radius; yy++) {
    for (int xx = x + radius; xx < x + w - radius; xx++) {
      int location = xx * (vip->bits_per_pixel / 8) + yy * fip->line_length;
      if (vip->bits_per_pixel == 32) {
        *(unsigned int *)(map + location) = color;
      } else {
        int r = color >> 16;
        int g = (color >> 8) & 0xff;
        int b = color & 0xff;
        *(unsigned short *)(map + location) =
            (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
      }
    }
  }
}

void draw_rect(int x, int y, int w, int h, unsigned int color,
               struct fb_var_screeninfo *vip, struct fb_fix_screeninfo *fip,
               char *map) {
  int xx, yy;
  int location = 0;

  for (yy = y; yy < (y + h); yy++) {
    for (xx = x; xx < (x + w); xx++) {
      location = xx * (vip->bits_per_pixel / 8) + yy * fip->line_length;
      if (vip->bits_per_pixel == 32) {  // 32 bpp
        *(unsigned int *)(map + location) = color;
      } else {  // 16 bpp
        int r = color >> 16;
        int g = (color >> 8) & 0xff;
        int b = color & 0xff;
        *(unsigned short *)(map + location) =
            (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
      }
    }
  }
}

void draw_number(int num, int x, int y, int scale, unsigned int color,
                 struct fb_var_screeninfo *vip, struct fb_fix_screeninfo *fip,
                 char *map) {
  if (num < 0 || num > 9) return;  // 숫자가 0~9 범위를 벗어나면 무시

  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) {
      if ((FONT_3x5[num][row] >> (2 - col)) & 1) {
        // 비트가 1이면 색칠
        draw_rect(x + col * scale, y + row * scale, scale, scale, color, vip,
                  fip, map);
      }
    }
  }
}

void draw_letter(char letter, int x, int y, int scale, unsigned int color,
                 struct fb_var_screeninfo *vip, struct fb_fix_screeninfo *fip,
                 char *map) {
  if (letter < 'A' || letter > 'Z') return;  // A~Z가 아닐 경우 무시
  int index = letter - 'A';

  for (int row = 0; row < 7; row++) {
    for (int col = 0; col < 3; col++) {
      if ((FONT_5x7[index][row] >> (2 - col)) & 1) {
        // 비트가 1이면 픽셀 그리기
        draw_rect(x + col * scale, y + row * scale, scale, scale, color, vip,
                  fip, map);
      }
    }
  }
}

void draw_text(const char *text, int x, int y, int scale, unsigned int color,
               struct fb_var_screeninfo *vip, struct fb_fix_screeninfo *fip,
               char *map) {
  int spacing = scale * 4;  // 글자 간 간격 설정

  while (*text) {
    if (*text == ' ') {
      x += spacing;  // 공백일 경우 x 좌표만 이동
    } else if (*text >= 'A' && *text <= 'Z') {
      draw_letter(*text, x, y, scale, color, vip, fip, map);
      x += spacing;
    } else if (*text >= '0' && *text <= '9') {
      draw_number(*text - '0', x, y, scale * 1.5, color, vip, fip, map);
      x += spacing * 1.5;
    }
    text++;
  }
}

// void db_disconnect(MYSQL *conn) {
//   if (conn) {
//     mysql_close(conn);
//   }
// }

int main(int argc, char *argv[]) {
  int fd;
  int size;
  char *map;
  struct fb_var_screeninfo vinfo;
  struct fb_fix_screeninfo finfo;

  int ret;
  char *dev_name;
  struct input_event ev;
  int raw_x = 0, raw_y = 0;
  int screen_x = 0, screen_y = 0;

  char *user_ID = argv[1];

  printf("ID:%s\n", user_ID);

  // 만약 선택을 하지 않고 10초가 지나면 db_disconnect 하면서
  // intel_bank_atm_login으로 이동

  // 프레임버퍼 열기
  fd = open(FBDEV, O_RDWR);
  if (fd == -1) {
    return EXIT_FAILURE;
  }

  // 프레임버퍼 정보 가져오기
  ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
  ioctl(fd, FBIOGET_FSCREENINFO, &finfo);

  // mmap 매핑
  size = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
  map = (char *)mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (map == (char *)-1) {
    return EXIT_FAILURE;
  }

  // 배경 색상 설정
  draw_rounded_rect(0, 0, vinfo.xres, vinfo.yres, 20, COLOR_WHITE, &vinfo,
                    &finfo, map);

  // 상단 환영 메시지 박스
  draw_rounded_rect(20, 30, 440, 100, 20, COLOR_MAIN, &vinfo, &finfo, map);
  draw_text("WELLCOME TO OHSY BANK", 70, 65, 4, COLOR_WHITE, &vinfo, &finfo,
            map);

  // 입금 / 출금 / 송금 버튼
  draw_rounded_rect(20, 180, 240, 180, 20, COLOR_BUTTON, &vinfo, &finfo, map);
  draw_text("DEPOSIT", 80, 255, 4, COLOR_WHITE, &vinfo, &finfo, map);

  draw_rounded_rect(280, 180, 240, 180, 20, COLOR_BUTTON, &vinfo, &finfo, map);
  draw_text("WITHDRAW", 335, 255, 4, COLOR_WHITE, &vinfo, &finfo, map);

  draw_rounded_rect(540, 180, 240, 180, 20, COLOR_BUTTON, &vinfo, &finfo, map);
  draw_text("REMITTANCE", 585, 255, 4, COLOR_WHITE, &vinfo, &finfo, map);

  // 보이스 피싱 안내
  draw_rounded_rect(20, 400, 440, 50, 15, COLOR_RED, &vinfo, &finfo, map);
  draw_text("CALL 112 IF YOU SUSPECT VOICE PHISHING", 30, 420, 2, COLOR_WHITE,
            &vinfo, &finfo, map);

  // intel bank 버튼
  draw_rounded_rect(500, 400, 280, 50, 15, COLOR_MAIN, &vinfo, &finfo, map);
  draw_text("CHECK BALANCE", 530, 415, 3, COLOR_WHITE, &vinfo, &finfo, map);

  // 뒤로가기 버튼
  draw_rounded_rect(480, 50, 50, 50, 5, COLOR_MAIN, &vinfo, &finfo, map);
  draw_text("H", 497, 58, 5, COLOR_WHITE, &vinfo, &finfo, map);

  // 이벤트 발생 시 deposit 실행
  // if (sleep(10) == 0) {
  //   execl("./intel_bank_atm_deposit", "./intel_bank_atm_deposit", NULL);
  // }

  // 이벤트 발생 시 WITHDRAW 실행

  // 이벤트 발생 시 REMITTANCE 실행
  dev_name = "/dev/input/event6";

  fd = open(dev_name, O_RDONLY);
  if (fd == -1) {
    return EXIT_FAILURE;
  }
  sleep(1);  // 메시지 혼합 방지

  // 종료
  for (;;) {
    touch_State = 0;
    ret = read(fd, &ev, sizeof(struct input_event));

    if (ret == -1) {
      close(fd);
      return EXIT_FAILURE;
    }

    if (ev.type == EV_ABS) {
      if (ev.code == ABS_X) {
        raw_x = ev.value;
        screen_x = scale_value(raw_x, TS_MIN_X, TS_MAX_X, 0, LCD_WIDTH);
      } else if (ev.code == ABS_Y) {
        raw_y = ev.value;
        screen_y = scale_value(raw_y, TS_MIN_Y, TS_MAX_Y, 0, LCD_HEIGHT);
      }
    } else if (ev.type == EV_KEY) {                 // 눌렸을떄떄
      if (ev.code == BTN_TOUCH && ev.value == 0) {  // 터치가 떼어졌을 때
        // 터치가 떼어졌을 때 1번 동작을 실행
        // 예를 들어, 사용자 인증 진행 등을 호출할 수 있습니다

        handle_touch_event(screen_x, screen_y, ev.code == BTN_TOUCH, user_ID);
      }
    }
  }
  close(fd);

  munmap(map, size);
  close(fd);

  return 0;
}
