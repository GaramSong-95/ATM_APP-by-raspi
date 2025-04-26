#include "intel_bank_atm_header.h"

#define ACCNUM 1
#define SUM 2
#define CHECK 1
#define OK 10
#define DEL 11
#define NONE -1

//------------------------------------------------------------
// Global Variables
//------------------------------------------------------------
// 별도의 파일 디스크립터를 사용: fb_fd: 프레임버퍼, ts_fd: 터치스크린/입력
// 디바이스

struct cursor {
  int x;
  int y;
};
int AS_State;

struct cursor cursor_ACCNUM;
struct cursor cursor_SUM;
int fd;
int size;
char *map;
int exit_State;
int back_State;
pid_t pid;

struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;

int last_x = -1, last_y = -1;
bool touch_active = false;

//------------------------------------------------------------
// Function Prototypes
//------------------------------------------------------------

void push_Button(struct cursor Cursor);
void handle_touch_event(int screen_x, int screen_y, bool touch_down,
                        int *touch_State, char *touch_Val, int *IP_State,
                        struct cursor *Cursor);
void *thread_func1(void *arg);
void *thread_func2(void *arg);

int scale_value(int value, int min, int max, int new_min,
                int new_max) {  // 터치스크린 각 번호의 값을 화면에 맞게 변환
  return ((value - min) * (new_max - new_min) / (max - min)) + new_min;
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

void draw_alert_box(const char *message, int x, int y, int w, int h,
                    unsigned int bg_color, unsigned int text_color,
                    struct fb_var_screeninfo *vip,
                    struct fb_fix_screeninfo *fip, char *map) {
  int bpp = vip->bits_per_pixel / 8;  // 바이트 단위로 변환
  int line_length = fip->line_length;

  // 1. 기존 화면 저장
  char *backup = (char *)malloc(w * h * bpp);
  if (!backup) return;

  for (int i = 0; i < h; i++) {
    memcpy(backup + i * w * bpp, map + ((y + i) * line_length + x * bpp),
           w * bpp);
  }

  // 2. 경고 박스 표시
  draw_rounded_rect(x, y, w, h, 10, bg_color, vip, fip, map);
  draw_text(message, x + w / 7, y + h / 3, 4, text_color, vip, fip, map);

  // 3. 2초 대기
  sleep(2);

  // 4. 저장한 화면 복원
  for (int i = 0; i < h; i++) {
    memcpy(map + ((y + i) * line_length + x * bpp), backup + i * w * bpp,
           w * bpp);
  }

  free(backup);
}

MYSQL *db_connect() {
  MYSQL *conn = mysql_init(NULL);
  if (!conn) {
    fprintf(stderr, "mysql_init() failed\n");
    return NULL;
  }

  if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL,
                          0)) {
    fprintf(stderr, "mysql_real_connect() failed: %s\n", mysql_error(conn));
    mysql_close(conn);
    return NULL;
  }
  return conn;
}

void db_disconnect(MYSQL *conn) {
  if (conn) {
    mysql_close(conn);
  }
}

int deposit_user_account(int money, const char *userID,
                         const char *bank_account,
                         struct fb_var_screeninfo *vip,
                         struct fb_fix_screeninfo *fip, char *map) {
  if (money <= 0) {
    fprintf(stderr, "Invalid deposit amount: must be positive.\n");
    return 0;
  }

  MYSQL *conn = db_connect();
  if (!conn) return 0;

  // 1. 입금 처리: 금액을 더하는 UPDATE 쿼리 실행
  char query[256];
  snprintf(query, sizeof(query),
           "UPDATE tbl_name a JOIN tbl_bank_account b ON a.ID = b.ID "
           "SET b.money = b.money + %d "
           "WHERE a.ID = '%s' and b.bank_account_ID = '%s';",
           money, userID, bank_account);

  if (mysql_query(conn, query)) {
    fprintf(stderr, "Deposit Query Failed: %s\n", mysql_error(conn));
    db_disconnect(conn);
    return 0;
  }

  int affected_rows = mysql_affected_rows(conn);
  if (affected_rows > 0) {
    // 2. 입금 성공시, 현재 잔액을 조회하는 SELECT 쿼리 실행
    char select_query[256];
    snprintf(
        select_query, sizeof(select_query),
        "SELECT b.money FROM tbl_name a JOIN tbl_bank_account b ON a.ID = b.ID "
        "WHERE a.ID = '%s' and b.bank_account_ID = '%s';",
        userID, bank_account);

    if (mysql_query(conn, select_query)) {
      fprintf(stderr, "Select Query Failed: %s\n", mysql_error(conn));
      db_disconnect(conn);
      return 0;
    }

    MYSQL_RES *result = mysql_store_result(conn);
    if (!result) {
      fprintf(stderr, "Store Result Failed: %s\n", mysql_error(conn));
      db_disconnect(conn);
      return 0;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row) {
      fprintf(stderr, "No data found for the account.\n");
      mysql_free_result(result);
      db_disconnect(conn);
      return 0;
    }

    int current_balance = atoi(row[0]);
    mysql_free_result(result);
    db_disconnect(conn);

    // 3. 현재 잔액 메시지 생성 (예: "CURRENT: 12345")
    char message[64];
    snprintf(message, sizeof(message), "%d", current_balance);

    // 4. 메시지를 알림 박스로 화면에 표시 (입금 성공시 녹색 배경)
    draw_alert_box(message, 250, 200, 300, 100, COLOR_GREEN, COLOR_WHITE, vip,
                   fip, map);
    return 1;
  } else {
    // 입금에 실패한 경우
    db_disconnect(conn);
    draw_alert_box("DEPOSIT FAILED", 250, 200, 300, 100, COLOR_RED, COLOR_WHITE,
                   vip, fip, map);
    return 0;
  }
}

void handle_touch_event(int screen_x, int screen_y, bool touch_down,
                        int *touch_State, char *touch_Val, int *AS_State,
                        struct cursor *Cursor) {
  if (!(touch_down)) {
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

  char num[] = "0123456789";

  // printf("touch num %d\n",*touch_State);

  if (*touch_State < 13) {
    if (screen_x >= 500 && screen_x <= 586 && screen_y >= 160 &&
        screen_y <= 210) {
      printf("Button 7 pressed\n");

      Cursor->x += 30;
      strcat(touch_Val, (char[]){num[7], '\0'});
      *touch_State += 1;
    } else if (screen_x >= 596 && screen_x <= 682 && screen_y >= 160 &&
               screen_y <= 210) {
      printf("Button 8 pressed\n");

      Cursor->x += 30;
      strcat(touch_Val, (char[]){num[8], '\0'});
      *touch_State += 1;
    } else if (screen_x >= 692 && screen_x <= 778 && screen_y >= 160 &&
               screen_y <= 210) {
      printf("Button 9 pressed\n");

      Cursor->x += 30;
      strcat(touch_Val, (char[]){num[9], '\0'});
      *touch_State += 1;
    } else if (screen_x >= 500 && screen_x <= 586 && screen_y >= 220 &&
               screen_y <= 270) {
      printf("Button 4 pressed\n");

      Cursor->x += 30;
      strcat(touch_Val, (char[]){num[4], '\0'});
      *touch_State += 1;

    } else if (screen_x >= 596 && screen_x <= 682 && screen_y >= 220 &&
               screen_y <= 270) {
      printf("Button 5 pressed\n");

      Cursor->x += 30;
      strcat(touch_Val, (char[]){num[5], '\0'});
      *touch_State += 1;
    } else if (screen_x >= 692 && screen_x <= 778 && screen_y >= 220 &&
               screen_y <= 270) {
      printf("Button 6 pressed\n");

      Cursor->x += 30;
      strcat(touch_Val, (char[]){num[6], '\0'});
      *touch_State += 1;
    } else if (screen_x >= 500 && screen_x <= 586 && screen_y >= 280 &&
               screen_y <= 330) {
      printf("Button 1 pressed\n");

      Cursor->x += 30;
      strcat(touch_Val, (char[]){num[1], '\0'});
      *touch_State += 1;
    } else if (screen_x >= 596 && screen_x <= 682 && screen_y >= 280 &&
               screen_y <= 330) {
      printf("Button 2 pressed\n");

      Cursor->x += 30;
      strcat(touch_Val, (char[]){num[2], '\0'});
      *touch_State += 1;
    } else if (screen_x >= 692 && screen_x <= 778 && screen_y >= 280 &&
               screen_y <= 330) {
      printf("Button 3 pressed\n");

      Cursor->x += 30;
      strcat(touch_Val, (char[]){num[3], '\0'});
      *touch_State += 1;
    } else if (screen_x >= 596 && screen_x <= 682 && screen_y >= 340 &&
               screen_y <= 390) {
      printf("Button 0 pressed\n");

      Cursor->x += 30;
      strcat(touch_Val, (char[]){num[0], '\0'});
      *touch_State += 1;
    }
  }

  if (screen_x >= 500 && screen_x <= 586 && screen_y >= 380 &&
      screen_y <= 415) {
    printf("Button OK pressed\n");
    exit_State = CHECK;
  } else if (screen_x >= 692 && screen_x <= 778 && screen_y >= 340 &&
             screen_y <= 390) {
    printf("Button DEL pressed\n");
    touch_Val[*touch_State - 1] = '\0';
    if (*touch_State > 0) {
      *touch_State -= 1;
      Cursor->x -= 30;
    }
  } else if (screen_x >= 485 && screen_x <= 520 && screen_y >= 40 &&
             screen_y <= 70) {
    printf("Button HOME pressed\n");
    exit_State = OK;
    back_State = OK;
  }

  if (*AS_State == ACCNUM) {
    draw_rounded_rect(20, 200, 440, 50, 15, COLOR_MAIN, &vinfo, &finfo, map);
    draw_text(touch_Val, 27, 208, 5, COLOR_WHITE, &vinfo, &finfo, map);
  } else if (*AS_State == SUM) {
    draw_rounded_rect(20, 310, 440, 50, 15, COLOR_MAIN, &vinfo, &finfo, map);
    draw_text(touch_Val, 27, 318, 5, COLOR_WHITE, &vinfo, &finfo, map);
  }

  if (screen_x >= 25 && screen_x <= 400 && screen_y >= 200 && screen_y <= 270) {
    printf("Button ACCNUM pressed\n");
    *AS_State = ACCNUM;
  } else if (screen_x >= 25 && screen_x <= 400 && screen_y >= 320 &&
             screen_y <= 390) {
    printf("Button SUM pressed\n");
    *AS_State = SUM;
  }

  // printf("touch num %d\n",*touch_State);
}

void *thread_func1(void *arg) {
  for (;;) {
    if (AS_State == ACCNUM) {
      draw_letter('I', cursor_ACCNUM.x, cursor_ACCNUM.y, 5, COLOR_WHITE, &vinfo,
                  &finfo, map);
      sleep(1);
      draw_letter('I', cursor_ACCNUM.x, cursor_ACCNUM.y, 5, COLOR_MAIN, &vinfo,
                  &finfo, map);
      sleep(1);
    } else if (AS_State == SUM) {
      draw_letter('I', cursor_SUM.x, cursor_SUM.y, 5, COLOR_WHITE, &vinfo,
                  &finfo, map);
      sleep(1);
      draw_letter('I', cursor_SUM.x, cursor_SUM.y, 5, COLOR_MAIN, &vinfo,
                  &finfo, map);
      sleep(1);
    }

    if (exit_State == OK) break;
  }

  pthread_exit("Goodbye Thread");
}

void *thread_func2(void *arg) {
  int ret;

  struct input_event ev;
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  int raw_x = 0, raw_y = 0;
  int screen_x = 0, screen_y = 0;
  int touch_StateACCNUM = 0;
  int touch_StateSUM = 0;
  char *touch_ValID = (char *)arg;
  char touch_ValACCNUM[15] = "";
  char touch_ValSUM[15] = "";

  for (;;) {
    ret = read(fd, &ev, sizeof(struct input_event));

    // 읽을 데이터가 없거나 오류가 발생한 경우 처리
    if (ret == -1) {
      if (errno == EAGAIN) {
        // 읽을 데이터가 없을 때는 잠시 대기하고 계속 시도
        usleep(1000);  // 1ms 대기
        continue;
      } else {
        // 다른 오류가 발생한 경우
        printf("Error: %s (%d)\n", strerror(errno), __LINE__);
        close(fd);
        break;
      }
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
        printf("Touch released at (X=%d, Y=%d)\n", screen_x, screen_y);

        // 터치가 떼어졌을 때 1번 동작을 실행
        // 예를 들어, 사용자 인증 진행 등을 호출할 수 있습니다

        if (AS_State == ACCNUM) {
          handle_touch_event(screen_x, screen_y, ev.code == BTN_TOUCH,
                             &touch_StateACCNUM, touch_ValACCNUM, &AS_State,
                             &cursor_ACCNUM);
        } else if (AS_State == SUM) {
          handle_touch_event(screen_x, screen_y, ev.code == BTN_TOUCH,
                             &touch_StateSUM, touch_ValSUM, &AS_State,
                             &cursor_SUM);
        }

        if (exit_State == CHECK) {
          // 여기서 db의 계좌번호와 잔금내역이이 일치하는지 체크 한다.
          int val_sum = atoi(touch_ValSUM);
          if (deposit_user_account(val_sum, touch_ValID, touch_ValACCNUM,
                                   &vinfo, &finfo, map)) {
            exit_State = OK;

          } else {
            printf("Authentication failed\n");
          }
        }
      }
    }

    // 계좌번호와 잔금내역이 일치하면 스레드 종료를 위해 exit_State=OK로 설정
    if (exit_State == OK) break;
    exit_State = 0;
  }

  pthread_exit("Goodbye Thread");
}

int main(int argc, char *argv[]) {
  pthread_t thread_id1, thread_id2;
  void *thread_ret;
  int ret;
  cursor_ACCNUM.x = 25;
  cursor_ACCNUM.y = 208;
  cursor_SUM.x = 25;
  cursor_SUM.y = 318;
  exit_State = 0;
  back_State = 0;
  AS_State = ACCNUM;
  char *user_ID = argv[1];
  printf("ID:%s\n", user_ID);

  char *dev_name;

  MYSQL *db_connect();

  printf("\e[?25l");  // 커서 숨기기
  fflush(stdout);

  printf("[%d] running ATM UI\n", pid = getpid());

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

  // UI 요소들
  // 제목
  draw_rounded_rect(20, 30, 440, 100, 20, COLOR_MAIN, &vinfo, &finfo, map);
  draw_text("WELLCOME TO OHSY BANK", 70, 65, 4, COLOR_WHITE, &vinfo, &finfo,
            map);

  // account number
  draw_rounded_rect(20, 200, 440, 50, 15, COLOR_MAIN, &vinfo, &finfo, map);
  draw_text("ACCOUNT NUMBER", 30, 160, 4, COLOR_BLACK, &vinfo, &finfo, map);

  // deposit amount
  draw_rounded_rect(20, 310, 440, 50, 15, COLOR_MAIN, &vinfo, &finfo, map);
  draw_text("DEPOSIT AMOUNT", 30, 270, 4, COLOR_BLACK, &vinfo, &finfo, map);

  // Call 112 if you suspect voice phishing
  draw_rounded_rect(20, 400, 440, 50, 15, COLOR_RED, &vinfo, &finfo, map);
  draw_text("CALL 112 IF YOU SUSPECT VOICE PHISHING", 30, 420, 2, COLOR_WHITE,
            &vinfo, &finfo, map);

  // INTEL BANK
  draw_rounded_rect(500, 400, 280, 50, 15, COLOR_MAIN, &vinfo, &finfo, map);
  draw_text("OHSY FINANCIAL", 530, 415, 3, COLOR_WHITE, &vinfo, &finfo, map);

  // 뒤로가기 버튼
  draw_rounded_rect(480, 50, 50, 50, 5, COLOR_MAIN, &vinfo, &finfo, map);
  draw_text("H", 497, 58, 5, COLOR_WHITE, &vinfo, &finfo, map);

  // 키패드 버튼 (둥글게)
  int start_x = 500, start_y = 160;
  int btn_w = 86, btn_h = 50, padding = 10;

  for (int i = 0; i < 9; i++) {
    int x = start_x + (i % 3) * (btn_w + padding);
    int y = start_y + (i / 3) * (btn_h + padding);
    draw_rounded_rect(x, y, btn_w, btn_h, 10, COLOR_BUTTON, &vinfo, &finfo,
                      map);

    // 숫자를 7~9, 4~6, 1~3 형태로 계산
    int num = 7 + (i % 3) - (i / 3) * 3;  // 7 8 9, 4 5 6, 1 2 3 형태로 변환
    draw_number(num, x + 35, y + 10, 5, COLOR_WHITE, &vinfo, &finfo, map);
  }

  // 키패드 ok, 0 , del 버튼
  draw_rounded_rect(500, 340, 86, 50, 10, COLOR_GREEN, &vinfo, &finfo, map);
  draw_text("OK", 520, 350, 4, COLOR_WHITE, &vinfo, &finfo, map);
  draw_rounded_rect(596, 340, 86, 50, 10, COLOR_BUTTON, &vinfo, &finfo, map);
  draw_number(0, 596 + 35, 340 + 10, 5, COLOR_WHITE, &vinfo, &finfo, map);
  draw_rounded_rect(692, 340, 86, 50, 10, COLOR_RED, &vinfo, &finfo, map);
  draw_text("DEL", 712, 350, 4, COLOR_WHITE, &vinfo, &finfo, map);

  printf("\e[?25h");  // 커서 다시 표시
  fflush(stdout);

  dev_name = "/dev/input/event6";

  fd = open(dev_name, O_RDONLY);
  if (fd == -1) {
    printf("Error: %s (%d)\n", strerror(errno), __LINE__);
    return EXIT_FAILURE;
  }
  printf("Device %s opened\n", dev_name);
  sleep(1);  // 메시지 혼합 방지

  // 스레드 생성 (터치 이벤트 처리와 커서 깜박임)
  printf("[%d] creating thread\n", pid);
  ret = pthread_create(&thread_id1, NULL, thread_func1, NULL);
  if (ret != 0) {
    printf("[%d] error: %d (%d)\n", pid, ret, __LINE__);
    return EXIT_FAILURE;
  }

  printf("[%d] waiting to join with a terminated thread\n", pid);

  printf("[%d] creating thread\n", pid);
  ret = pthread_create(&thread_id2, NULL, thread_func2, user_ID);
  if (ret != 0) {
    printf("[%d] error: %d (%d)\n", pid, ret, __LINE__);
    return EXIT_FAILURE;
  }

  printf("[%d] waiting to join with a terminated thread\n", pid);

  ret = pthread_join(thread_id1, &thread_ret);
  if (ret != 0) {
    printf("[%d] error: %d (%d)\n", pid, ret, __LINE__);
    return EXIT_FAILURE;
  }
  printf("[%d] thread returned \"%s\"\n", pid, (char *)thread_ret);

  ret = pthread_join(thread_id2, &thread_ret);
  if (ret != 0) {
    printf("[%d] error: %d (%d)\n", pid, ret, __LINE__);
    return EXIT_FAILURE;
  }
  printf("[%d] thread returned \"%s\"\n", pid, (char *)thread_ret);
  printf("[%d] terminted\n", pid);

  munmap(map, size);
  close(fd);

  if(back_State==OK) {
  // 이전 화면
    execl("./intel_bank_atm_main", "./intel_bank_atm_main",user_ID,NULL);
  }
  //다음 화면
  execl("./intel_bank_atm_end", "./intel_bank_atm_end", NULL);

  return 0;
}
