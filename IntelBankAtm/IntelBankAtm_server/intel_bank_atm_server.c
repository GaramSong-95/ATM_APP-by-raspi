#include <arpa/inet.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PORT 26000
#define BUFFER_SIZE 1024
#define CLEANUP_INTERVAL 60
#define FILE_RETENTION 120
#define MAX_PATH 2048
#define MAX_FILENAME 256

void *cleanup_thread(void *arg) {
  while (1) {
    DIR *dir;
    struct dirent *entry;
    time_t now = time(NULL);
    char filepath[MAX_PATH];

    struct stat st = {0};
    if (stat("./cctv", &st) == -1) {
      mkdir("./cctv", 0755);
    }

    dir = opendir("./cctv");
    if (dir == NULL) {
      perror("Could not open cctv directory");
      sleep(CLEANUP_INTERVAL);
      continue;
    }

    while ((entry = readdir(dir)) != NULL) {
      if (entry->d_type != DT_REG) continue;

      if (strlen(entry->d_name) >= MAX_FILENAME) {
        printf("Warning: Filename too long: %s\n", entry->d_name);
        continue;
      }

      if (snprintf(filepath, MAX_PATH, "./cctv/%s", entry->d_name) >=
          MAX_PATH) {
        printf("Warning: Path too long for %s\n", entry->d_name);
        continue;
      }

      struct stat file_stat;
      if (stat(filepath, &file_stat) == 0) {
        double age = difftime(now, file_stat.st_mtime);

        if (age > FILE_RETENTION) {
          printf("[+] Removing old file: %s (age: %.0f seconds)\n",
                 entry->d_name, age);
          if (remove(filepath) != 0) {
            perror("Error removing file");
          }
        }
      }
    }

    closedir(dir);
    sleep(CLEANUP_INTERVAL);
  }
  return NULL;
}

void *handle_client(void *arg) {
  int client_sock = *(int *)arg;
  free(arg);
  char buffer[BUFFER_SIZE];
  FILE *file;
  int bytes_received;
  char filepath[MAX_PATH];
  char sanitized_filename[MAX_FILENAME];
  int file_received_successfully = 0;

  // 파일 이름 수신
  bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
  if (bytes_received <= 0) {
    perror("File name receive failed");
    close(client_sock);
    return NULL;
  }
  buffer[bytes_received] = '\0';

  // 파일명에서 개행 문자 제거
  char *newline_pos = strchr(buffer, '\n');
  if (newline_pos) {
    *newline_pos = '\0';
  }

  // 파일명 길이 제한 및 유효성 검사
  if (strlen(buffer) >= MAX_FILENAME) {
    printf("Error: Filename too long\n");
    close(client_sock);
    return NULL;
  }

  // 파일명 복사 및 경로 생성
  strncpy(sanitized_filename, buffer, MAX_FILENAME - 1);
  sanitized_filename[MAX_FILENAME - 1] = '\0';

  if (snprintf(filepath, MAX_PATH, "./cctv/%s", sanitized_filename) >=
      MAX_PATH) {
    printf("Error: Path too long\n");
    close(client_sock);
    return NULL;
  }

  printf("[+] Receiving file: %s\n", filepath);

  // cctv 디렉토리 확인 및 생성
  struct stat st = {0};
  if (stat("./cctv", &st) == -1) {
    mkdir("./cctv", 0755);
  }

  file = fopen(filepath, "wb");
  if (file == NULL) {
    perror("File opening failed");
    close(client_sock);
    return NULL;
  }

  // 파일 이름 수신 확인 메시지 전송
  send(client_sock, "OK", 2, 0);

  // 파일 데이터 수신
  while ((bytes_received = recv(client_sock, buffer, sizeof(buffer), 0)) > 0) {
    if (fwrite(buffer, sizeof(char), bytes_received, file) != bytes_received) {
      perror("Write to file failed");
      file_received_successfully = 0;
      break;
    }
    file_received_successfully = 1;
  }

  fclose(file);

  // 파일 수신 완료 확인 메시지 전송
  if (file_received_successfully) {
    printf("[+] File received successfully: %s\n", filepath);
    send(client_sock, "OK", 2, 0);
  } else {
    printf("[-] File reception failed: %s\n", filepath);
    send(client_sock, "ERROR", 5, 0);
  }

  close(client_sock);
  return NULL;
}

int main() {
  int server_sock, *client_sock;
  struct sockaddr_in server_addr, client_addr;
  socklen_t client_len = sizeof(client_addr);
  pthread_t thread_id, cleanup_thread_id;

  if (pthread_create(&cleanup_thread_id, NULL, cleanup_thread, NULL) != 0) {
    perror("Failed to create cleanup thread");
    exit(1);
  }
  pthread_detach(cleanup_thread_id);

  server_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (server_sock < 0) {
    perror("Socket creation failed");
    exit(1);
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(PORT);

  if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("Bind failed");
    close(server_sock);
    exit(1);
  }

  if (listen(server_sock, 10) < 0) {
    perror("Listen failed");
    close(server_sock);
    exit(1);
  }

  printf("[+] Server listening on port %d...\n", PORT);
  printf(
      "[+] Files will be stored in ./cctv/ and cleaned up after 20 minutes\n");

  while (1) {
    client_sock = malloc(sizeof(int));
    *client_sock =
        accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
    if (*client_sock < 0) {
      perror("Accept failed");
      free(client_sock);
      continue;
    }
    printf("[+] New client connected from %s:%d\n",
           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    if (pthread_create(&thread_id, NULL, handle_client, client_sock) != 0) {
      perror("Thread creation failed");
      free(client_sock);
    }
    pthread_detach(thread_id);
  }

  close(server_sock);
  return 0;
}