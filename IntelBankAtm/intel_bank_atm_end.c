#include "intel_bank_atm_header.h"

pid_t pid;

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

void db_disconnect(MYSQL *conn) {
  if (conn) {
    mysql_close(conn);
  }
}

int main() {
  int fd;
  int size;
  char *map;
  struct fb_var_screeninfo vinfo;
  struct fb_fix_screeninfo finfo;

  // 프레임버퍼 열기
  fd = open(FBDEV, O_RDWR);
  if (fd == -1) {
    printf("[%d] error: %s (%d)\n", pid, strerror(errno), __LINE__);
    return EXIT_FAILURE;
  }

  // 프레임버퍼 정보 가져오기
  ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
  ioctl(fd, FBIOGET_FSCREENINFO, &finfo);

  // mmap 매핑
  size = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
  map = (char *)mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (map == (char *)-1) {
    printf("[%d] error: %s (%d)\n", pid, strerror(errno), __LINE__);
    return EXIT_FAILURE;
  }

  // 배경 색상 설정
  draw_rounded_rect(0, 0, vinfo.xres, vinfo.yres, 20, COLOR_WHITE, &vinfo,
                    &finfo, map);

  // 상단 환영 메시지 박스
  draw_rounded_rect(20, 30, 440, 100, 20, COLOR_MAIN, &vinfo, &finfo, map);
  draw_text("WELLCOME TO OHSY BANK", 70, 65, 4, COLOR_WHITE, &vinfo, &finfo,
            map);

  // 이용 감사 메시지 박스
  draw_rounded_rect(20, 160, 760, 230, 20, COLOR_BUTTON, &vinfo, &finfo, map);
  draw_text("THANK YOU FOR USING", 170, 205, 6, COLOR_WHITE, &vinfo, &finfo,
            map);
  draw_text("OHSY FINANCIAL", 230, 285, 6, COLOR_WHITE, &vinfo, &finfo, map);

  // 보이스 피싱 안내
  draw_rounded_rect(20, 400, 440, 50, 15, COLOR_RED, &vinfo, &finfo, map);
  draw_text("CALL 112 IF YOU SUSPECT VOICE PHISHING", 30, 420, 2, COLOR_WHITE,
            &vinfo, &finfo, map);

  // intel bank 버튼
  draw_rounded_rect(500, 400, 280, 50, 15, COLOR_MAIN, &vinfo, &finfo, map);
  draw_text("OHSY FINANCIAL", 530, 415, 3, COLOR_WHITE, &vinfo, &finfo, map);

  // 종료
  munmap(map, size);
  close(fd);

  if (sleep(10) == 0) {
    execl("./intel_bank_atm_login", "intel_bank_atm_login", NULL);
  }

  return 0;
}
