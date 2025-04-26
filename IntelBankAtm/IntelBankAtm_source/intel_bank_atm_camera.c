#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/fb.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define VIDEO_DURATION 60
#define _POSIX_C_SOURCE 200809L
#define CLEAR(x) memset(&(x), 0, sizeof(x))

#define COLOR_RED 0xff0000
#define COLOR_GREEN 0x00ff00
#define COLOR_BLUE 0x0000ff
#define COLOR_BLACK 0x000000
#define COLOR_WHITE 0xffffff

struct buffer {
  void *start;
  size_t length;
};

static char *dev_name = "/dev/video0";
static int fd = -1;
static int fd_fb = -1;
char *map_fb;
int size_fb;
struct buffer *buffers;
static unsigned int n_buffers;
static char *fb_name = "/dev/fb0";
static int force_format;
static int frame_count = 100000;
static struct v4l2_format fmt;
static int xpos, ypos;
static int clear_screen = 0;

struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;

#define MAX_FILE_BUF (640 * 2 * 480)

static void errno_exit(const char *s) {
  fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
  exit(EXIT_FAILURE);
}

void draw_rect(int x, int y, int w, int h, unsigned int color,
               struct fb_var_screeninfo *vip, struct fb_fix_screeninfo *fip,
               char *map) {
  int xx, yy;
  int location = 0;

  for (yy = y; yy < (y + h); yy++) {
    for (xx = x; xx < (x + w); xx++) {
      location = xx * (vip->bits_per_pixel / 8) + yy * fip->line_length;
      if (vip->bits_per_pixel == 32) { /* 32 bpp */
        *(unsigned int *)(map + location) = color;
      } else { /* 16 bpp */
        int r = color >> 16;
        int g = (color >> 8) & 0xff;
        int b = color & 0xff;
        *(unsigned short *)(map + location) =
            (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
      }
    }
  }
}

static int fb_init(void) {
  fd_fb = open(fb_name, O_RDWR);
  if (fd_fb == -1) {
    errno_exit("fbdev open");
  }

  if (ioctl(fd_fb, FBIOGET_VSCREENINFO, &vinfo) == -1) {
    errno_exit("FBIOGET_VSCREENINFO");
  }

  if (ioctl(fd_fb, FBIOGET_FSCREENINFO, &finfo) == -1) {
    errno_exit("FBIOGET_FSCREENINFO");
  }
  printf("%s: %dx%d, %dbpp\n", fb_name, vinfo.xres, vinfo.yres,
         vinfo.bits_per_pixel);

  size_fb = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
  map_fb =
      (char *)mmap(0, size_fb, PROT_READ | PROT_WRITE, MAP_SHARED, fd_fb, 0);
  if (map_fb == (char *)-1) {
    errno_exit("fbdev mmap");
  }

  if (clear_screen) {
    draw_rect(vinfo.xoffset, vinfo.yoffset, vinfo.xres, vinfo.yres, COLOR_BLACK,
              &vinfo, &finfo, map_fb);
  }

  return 0;
}

static int fb_close(void) {
  if (-1 == close(fd_fb)) {
    errno_exit("fbdev close");
  }
  fd_fb = -1;
  if (-1 == munmap(map_fb, size_fb)) {
    errno_exit("fbdev munmap");
  }

  return 0;
}

void generate_filename(char *filename, const char *ext) {
  struct stat st = {0};
  if (stat("./cctv", &st) == -1) {
    mkdir("./cctv", 0755);
  }

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  // 호스트네임으로 기기 구분
  char *device_id = "ATM1";

  snprintf(filename, 128, "./cctv/cam_%s_%04d%02d%02d_%02d%02d%02d.%s",
           device_id, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour,
           t->tm_min, t->tm_sec, ext);
}

static int capture_to_raw_yuv(const void *p, int size) {
  // Check and create directory if it doesn't exist
  struct stat st = {0};
  if (stat("./cctv_image_file", &st) == -1) {
    if (mkdir("./cctv_image_file", 0755) == -1) {
      perror("Error creating directory cctv_image_file");
      return -1;
    }
    printf("Created directory: cctv_image_file\n");
  }

  FILE *file = fopen("./cctv_image_file/captured_frame.yuv", "ab");
  if (file == NULL) {
    perror("Error opening file for writing YUV data");
    return -1;
  }

  size_t written = fwrite(p, 1, size, file);
  if (written != size) {
    perror("Error writing YUV data to file");
    fclose(file);
    return -1;
  }

  fclose(file);
  return 0;
}

static int capture_and_encode(const char *yuv_filename) {
  int pipefd[2];
  char mp4_filename[64];

  generate_filename(mp4_filename, "mp4");

  FILE *yuv_file = fopen(yuv_filename, "rb");
  if (yuv_file == NULL) {
    perror("fopen() failed");
    return -1;
  }

  fseek(yuv_file, 0, SEEK_END);
  long size = ftell(yuv_file);
  fseek(yuv_file, 0, SEEK_SET);

  void *yuv_data = malloc(size);
  if (yuv_data == NULL) {
    perror("malloc() failed");
    fclose(yuv_file);
    return -1;
  }
  fread(yuv_data, 1, size, yuv_file);
  fclose(yuv_file);

  if (pipe(pipefd) == -1) {
    perror("pipe() failed");
    free(yuv_data);
    return -1;
  }

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork() failed");
    free(yuv_data);
    return -1;
  }

  if (pid == 0) {
    close(pipefd[1]);

    dup2(pipefd[0], STDIN_FILENO);
    close(pipefd[0]);

    execlp("ffmpeg", "ffmpeg", "-f", "rawvideo", "-pix_fmt", "yuyv422", "-s",
           "176x144", "-r", "30", "-i", "pipe:0", "-c:v", "libx264", "-preset",
           "slow", "-crf", "23", mp4_filename, "-y", (char *)NULL);

    perror("execlp() failed");
    exit(1);
  } else {
    close(pipefd[0]);

    ssize_t written = write(pipefd[1], yuv_data, size);
    if (written != size) {
      perror("Error writing YUV data to pipe");
    }
    close(pipefd[1]);

    waitpid(pid, NULL, 0);
    printf("MP4 saved: %s\n", mp4_filename);
  }

  free(yuv_data);

  if (remove(yuv_filename) == 0) {
    printf("Deleted YUV file: %s\n", yuv_filename);
  } else {
    perror("Error deleting YUV file");
  }

  return 0;
}

static int capture_to_fb(const void *p, int size, int x, int y) {
  int i, j;
  unsigned int yuyv;
  int R, G, B;
  unsigned char Y, U, V;
  char *pp = (char *)p;
  int width = fmt.fmt.pix.width;
  int height = fmt.fmt.pix.height;

  capture_to_raw_yuv(p, size);

  for (i = 0; i < height; i++) {
    for (j = 0; j < width / 2; j++) {
      yuyv = *(unsigned int *)(pp + i * width * 2 + j * 4);

      U = (yuyv >> 8) & 0xff;
      V = (yuyv >> 24) & 0xff;

      Y = yuyv & 0xff;

      R = Y + 1.4075 * (V - 128);
      if (R > 255) R = 255;
      if (R < 0) R = 0;
      G = Y - 3455 * (U - 128) / 10000 - (7169 * (V - 128) / 10000);
      if (G > 255) G = 255;
      if (G < 0) G = 0;
      B = Y + 17790 * (U - 128) / 10000;
      if (B > 255) B = 255;
      if (B < 0) B = 0;
      if (R < 0 || R > 255 || G < 0 || G > 255 || B < 0 || B > 255)
        printf("RGB=%d,%d,%d, YUV=%d,%d,%d\n", R, B, B, Y, U, V);

      if (vinfo.bits_per_pixel == 32) {
        unsigned int rgb888 = R << 16 | G << 8 | B;
        *(unsigned int *)(map_fb + ((i + y) * vinfo.xres + j * 2 + x) *
                                       vinfo.bits_per_pixel / 8) = rgb888;
      } else {
        unsigned short rgb565 = ((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3);
        *(unsigned short *)(map_fb + ((i + y) * vinfo.xres + j * 2 + x) *
                                         vinfo.bits_per_pixel / 8) = rgb565;
      }

      Y = (yuyv >> 16) & 0xff;
      R = Y + 1.4075 * (V - 128);
      if (R > 255) R = 255;
      if (R < 0) R = 0;
      G = Y - 3455 * (U - 128) / 10000 - (7169 * (V - 128) / 10000);
      if (G > 255) G = 255;
      if (G < 0) G = 0;
      B = Y + 17790 * (U - 128) / 10000;
      if (B > 255) B = 255;
      if (B < 0) B = 0;
      if (R < 0 || R > 255 || G < 0 || G > 255 || B < 0 || B > 255)
        printf("RGB=%d,%d,%d, YUV=%d,%d,%d\n", R, B, B, Y, U, V);

      if (vinfo.bits_per_pixel == 32) {
        unsigned int rgb888 = R << 16 | G << 8 | B;
        *(unsigned int *)(map_fb + ((i + y) * vinfo.xres + j * 2 + 1 + x) *
                                       vinfo.bits_per_pixel / 8) = rgb888;
      } else {
        unsigned short rgb565 = ((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3);
        *(unsigned short *)(map_fb + ((i + y) * vinfo.xres + j * 2 + 1 + x) *
                                         vinfo.bits_per_pixel / 8) = rgb565;
      }
    }
  }
  return 0;
}

static int xioctl(int fh, int request, void *arg) {
  int r;

  do {
    r = ioctl(fh, request, arg);
  } while (-1 == r && EINTR == errno);

  return r;
}

static void process_image(const void *p, int size) {
  capture_to_fb(p, size, xpos, ypos);
}

static int read_frame(void) {
  struct v4l2_buffer buf;

  CLEAR(buf);

  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;

  if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
    switch (errno) {
      case EAGAIN:
        return 0;

      default:
        errno_exit("VIDIOC_DQBUF");
    }
  }

  assert(buf.index < n_buffers);

  process_image(buffers[buf.index].start, buf.bytesused);

  if (-1 == xioctl(fd, VIDIOC_QBUF, &buf)) errno_exit("VIDIOC_QBUF");

  return 1;
}

static void mainloop(void) {
  struct timespec start_time, current_time, frame_time;
  // unsigned int count;
  int frames_captured = 0;
  const int target_fps = 10;
  const int total_frames = VIDEO_DURATION * target_fps;
  const long frame_duration_ns = 1000000000L / target_fps;
  double elapsed_time;

  clock_gettime(CLOCK_MONOTONIC, &start_time);
  frame_time = start_time;

  while (1) {
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    elapsed_time = (current_time.tv_sec - start_time.tv_sec) +
                   (current_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;

    if (elapsed_time >= VIDEO_DURATION && frames_captured >= total_frames) {
      const char *yuv_filename = "./cctv_image_file/captured_frame.yuv";

      printf("\nRecording complete: %.3f seconds, %d frames captured\n",
             elapsed_time, frames_captured);

      if (access(yuv_filename, F_OK) != -1) {
        capture_and_encode(yuv_filename);
      } else {
        printf("YUV file not found: %s\n", yuv_filename);
      }

      clock_gettime(CLOCK_MONOTONIC, &start_time);
      frame_time = start_time;
      frames_captured = 0;
      continue;
    }

    clock_gettime(CLOCK_MONOTONIC, &current_time);

    while (1) {
      clock_gettime(CLOCK_MONOTONIC, &current_time);
      if (current_time.tv_sec > frame_time.tv_sec ||
          (current_time.tv_sec == frame_time.tv_sec &&
           current_time.tv_nsec >= frame_time.tv_nsec)) {
        break;
      }

      usleep(500);
    }

    frame_time.tv_nsec += frame_duration_ns;
    if (frame_time.tv_nsec >= 1000000000) {
      frame_time.tv_sec += 1;
      frame_time.tv_nsec -= 1000000000;
    }

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    int r = select(fd + 1, &fds, NULL, NULL, &tv);

    if (-1 == r) {
      if (EINTR == errno) continue;
      errno_exit("select");
    }

    if (r > 0 && read_frame()) {
      frames_captured++;
      // 초과 녹화 시간 표시
      double remaining_time = VIDEO_DURATION - elapsed_time;
      printf("\rCapturing frame %d/%d (%.1f%%) - Time: %.2fs (%.2fs remaining)",
             frames_captured, total_frames,
             (frames_captured * 100.0) / total_frames, elapsed_time,
             remaining_time > 0 ? remaining_time : 0.0);
      fflush(stdout);
    }
  }
}

static void stop_capturing(void) {
  enum v4l2_buf_type type;

  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type)) errno_exit("VIDIOC_STREAMOFF");
}

static void start_capturing(void) {
  unsigned int i;
  enum v4l2_buf_type type;

  for (i = 0; i < n_buffers; ++i) {
    struct v4l2_buffer buf;

    CLEAR(buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;

    if (-1 == xioctl(fd, VIDIOC_QBUF, &buf)) errno_exit("VIDIOC_QBUF");
  }
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(fd, VIDIOC_STREAMON, &type)) errno_exit("VIDIOC_STREAMON");
}

static void uninit_device(void) {
  unsigned int i;

  for (i = 0; i < n_buffers; ++i)
    if (-1 == munmap(buffers[i].start, buffers[i].length)) errno_exit("munmap");

  free(buffers);
}

static void init_mmap(void) {
  struct v4l2_requestbuffers req;

  CLEAR(req);

  req.count = 4;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
    if (EINVAL == errno) {
      fprintf(stderr,
              "%s does not support "
              "memory mapping\n",
              dev_name);
      exit(EXIT_FAILURE);
    } else {
      errno_exit("VIDIOC_REQBUFS");
    }
  }

  if (req.count < 2) {
    fprintf(stderr, "Insufficient buffer memory on %s\n", dev_name);
    exit(EXIT_FAILURE);
  }

  buffers = calloc(req.count, sizeof(*buffers));

  if (!buffers) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }

  for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
    struct v4l2_buffer buf;

    CLEAR(buf);

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = n_buffers;

    if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf)) errno_exit("VIDIOC_QUERYBUF");

    buffers[n_buffers].length = buf.length;
    buffers[n_buffers].start =
        mmap(NULL /* start anywhere */, buf.length,
             PROT_READ | PROT_WRITE /* required */,
             MAP_SHARED /* recommended */, fd, buf.m.offset);

    if (MAP_FAILED == buffers[n_buffers].start) errno_exit("mmap");
  }
}

void resize_frame(unsigned char *src, int src_w, int src_h, unsigned char *dst,
                  int dst_w, int dst_h) {
  int x_ratio = (src_w << 16) / dst_w + 1;
  int y_ratio = (src_h << 16) / dst_h + 1;

  for (int j = 0; j < dst_h; j++) {
    for (int i = 0; i < dst_w; i++) {
      int src_x = (i * x_ratio) >> 16;
      int src_y = (j * y_ratio) >> 16;

      int src_index = (src_y * src_w + src_x) * 2;
      int dst_index = (j * dst_w + i) * 2;

      dst[dst_index] = src[src_index];
      dst[dst_index + 1] = src[src_index + 1];
    }
  }
}

static void init_device(void) {
  struct v4l2_capability cap;
  struct v4l2_cropcap cropcap;
  struct v4l2_crop crop;

  if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
    if (EINVAL == errno) {
      fprintf(stderr, "%s is no V4L2 device\n", dev_name);
      exit(EXIT_FAILURE);
    } else {
      errno_exit("VIDIOC_QUERYCAP");
    }
  }

  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    fprintf(stderr, "%s is no video capture device\n", dev_name);
    exit(EXIT_FAILURE);
  }

  if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
    fprintf(stderr, "%s does not support streaming i/o\n", dev_name);
    exit(EXIT_FAILURE);
  }

  CLEAR(cropcap);

  cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    crop.c = cropcap.defrect;

    if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop)) {
      switch (errno) {
        case EINVAL:
          break;
        default:
          break;
      }
    }
  } else {
  }

  CLEAR(fmt);

  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = 176;
  fmt.fmt.pix.height = 144;
  // fmt.fmt.pix.width = 640;
  // fmt.fmt.pix.height = 480;
  // fmt.fmt.pix.width = 320;
  // fmt.fmt.pix.height = 240;
  fmt.fmt.pix.sizeimage = fmt.fmt.pix.width * fmt.fmt.pix.height * 2;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;
  if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
    errno_exit("VIDIOC_S_FMT");
  }

  printf(
      "v4l2test: witdh=%d, height=%d, bytesperline=%d, sizeimage=%d, "
      "pixelformat %c%c%c%c\n",
      fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.bytesperline,
      fmt.fmt.pix.sizeimage, fmt.fmt.pix.pixelformat & 0xff,
      (fmt.fmt.pix.pixelformat >> 8) & 0xff,
      (fmt.fmt.pix.pixelformat >> 16) & 0xff,
      (fmt.fmt.pix.pixelformat >> 24) & 0xff);

  init_mmap();
}

static void close_device(void) {
  if (-1 == close(fd)) errno_exit("close");

  fd = -1;

  fb_close();
}

static void open_device(void) {
  struct stat st;

  if (-1 == stat(dev_name, &st)) {
    fprintf(stderr, "Cannot identify '%s': %d, %s\n", dev_name, errno,
            strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (!S_ISCHR(st.st_mode)) {
    fprintf(stderr, "%s is no device\n", dev_name);
    exit(EXIT_FAILURE);
  }

  fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);

  if (-1 == fd) {
    fprintf(stderr, "Cannot open '%s': %d, %s\n", dev_name, errno,
            strerror(errno));
    exit(EXIT_FAILURE);
  }

  fb_init();
}

static void usage(FILE *fp, int argc, char **argv) {
  fprintf(fp,
          "Usage: %s [options]\n"
          "Options:\n"
          "-d | --device name   Video device name [%s]\n"
          "-h | --help          Print this message\n"
          "-b | --framebuffer   Outputs stream to framebuffer\n"
          "-f | --format        Force format to specific type\n"
          "-c | --count         Number of frames to grab [%i]\n"
          "-x | --xpos position Starting X position to draw [%i]\n"
          "-y | --ypos position Starting Y position to draw [%i]\n"
          "-s | --clear         Clear screen\n"
          "",
          argv[0], dev_name, frame_count, xpos, ypos);
}

static const char short_options[] = "d:hmruob:fc:x:y:a:n:s";

static const struct option long_options[] = {
    {"device", required_argument, NULL, 'd'},
    {"help", no_argument, NULL, 'h'},
    {"framebuffer", required_argument, NULL, 'b'},
    {"format", no_argument, NULL, 'f'},
    {"count", required_argument, NULL, 'c'},
    {"xpos", required_argument, NULL, 'x'},
    {"ypos", required_argument, NULL, 'y'},
    {"clear", no_argument, NULL, 's'},
    {0, 0, 0, 0}};

int main(int argc, char **argv) {
  xpos = 550;  // 원하는 x 위치
  ypos = 0;    // 원하는 y 위치

  for (;;) {
    int idx;
    int c;

    c = getopt_long(argc, argv, short_options, long_options, &idx);

    if (-1 == c) break;

    switch (c) {
      case 0:
        break;

      case 'd':
        dev_name = optarg;
        break;

      case 'h':
        usage(stdout, argc, argv);
        exit(EXIT_SUCCESS);

      case 'b':
        fb_name = optarg;
        break;

      case 'f':
        force_format++;
        break;

      case 'c':
        errno = 0;
        frame_count = strtol(optarg, NULL, 0);
        if (errno) errno_exit(optarg);
        break;

      case 'x':
        errno = 0;
        xpos = strtol(optarg, NULL, 0);
        if (errno) errno_exit(optarg);
        break;

      case 'y':
        errno = 0;
        ypos = strtol(optarg, NULL, 0);
        if (errno) errno_exit(optarg);
        break;

      case 's':
        clear_screen = 1;
        break;

      default:
        usage(stderr, argc, argv);
        exit(EXIT_FAILURE);
    }
  }

  open_device();

  init_device();
  start_capturing();
  mainloop();
  stop_capturing();
  uninit_device();
  close_device();
  return 0;
}
