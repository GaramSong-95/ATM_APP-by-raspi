#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <arpa/inet.h>

#define SERVER_IP "10.10.141.211"
#define SERVER_PORT 26000
#define BUFFER_SIZE 1024
#define MONITOR_INTERVAL 60

// 파일 전송 함수
int send_file(const char* filepath) {
    int sock;
    struct sockaddr_in server_addr;
    FILE *file;
    char buffer[BUFFER_SIZE];
    int bytes_read;
    int success = 0;

    // 파일 열기
    file = fopen(filepath, "rb");
    if (file == NULL) {
        perror("File open failed");
        return -1;
    }

    // 서버 소켓 생성
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        fclose(file);
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        fclose(file);
        close(sock);
        return -1;
    }

    const char* filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;

    // 파일 이름 전송
    char filename_with_newline[BUFFER_SIZE];
    snprintf(filename_with_newline, sizeof(filename_with_newline), "%s\n", filename);
    if (send(sock, filename_with_newline, strlen(filename_with_newline), 0) < 0) {
        perror("File name send failed");
        fclose(file);
        close(sock);
        return -1;
    }

    // 서버로부터 파일 이름 수신 확인
    char response[BUFFER_SIZE];
    if (recv(sock, response, sizeof(response), 0) <= 0) {
        perror("Failed to receive filename confirmation");
        fclose(file);
        close(sock);
        return -1;
    }

    // 파일 데이터 전송
    while ((bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE, file)) > 0) {
        if (send(sock, buffer, bytes_read, 0) < 0) {
            perror("Send failed");
            fclose(file);
            close(sock);
            return -1;
        }
    }

    // 전송 완료를 알리는 EOF 신호 전송
    shutdown(sock, SHUT_WR);

    // 서버로부터 최종 완료 확인 대기
    memset(response, 0, sizeof(response));
    if (recv(sock, response, sizeof(response), 0) <= 0) {
        perror("Failed to receive completion confirmation");
        fclose(file);
        close(sock);
        return -1;
    }

    if (strcmp(response, "OK") == 0) {
        printf("File sent and received successfully: %s\n", filepath);
        success = 1;
    } else {
        printf("Server did not confirm successful reception\n");
        success = 0;
    }

    fclose(file);
    close(sock);

    if (success) {
        if (remove(filepath) == 0) {
            printf("File deleted successfully: %s\n", filepath);
        } else {
            perror("File deletion failed");
            return -1;
        }
    }

    return success ? 0 : -1;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <directory_path>\n", argv[0]);
        exit(1);
    }

    char *dir_path = argv[1];
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    char filepath[512];

    printf("Monitoring directory: %s\n", dir_path);

    while (1) {
        dir = opendir(dir_path);
        if (dir == NULL) {
            perror("Cannot open directory");
            exit(1);
        }

        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, entry->d_name);
            
            if (stat(filepath, &file_stat) == -1) {
                perror("stat failed");
                continue;
            }

            if (!S_ISREG(file_stat.st_mode))
                continue;

            printf("New file detected: %s\n", entry->d_name);
            send_file(filepath);
        }
        closedir(dir);

        sleep(MONITOR_INTERVAL);
    }

    return 0;
}