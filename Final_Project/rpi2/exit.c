
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUF_SIZE 256

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    char plate[32];
    char buf[BUF_SIZE];

    printf("輸入車牌後按 Enter 送出離場指令；輸入 \"quit\" 或 Ctrl-D 結束。\n");
    while (1) {
        printf("Plate> ");
        if (fgets(plate, sizeof(plate), stdin) == NULL) break;  // Ctrl-D
        plate[strcspn(plate, "\r\n")] = '\0';
        if (plate[0] == '\0' || strcmp(plate, "quit") == 0) break;

        // 建立連線
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) { perror("socket"); continue; }
        struct sockaddr_in serv_addr = {0};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
            fprintf(stderr, "Invalid address: %s\n", server_ip);
            close(sock);
            continue;
        }
        if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            perror("connect"); close(sock); continue;
        }

        // 發送 exit 指令 (帶換行)
        snprintf(buf, sizeof(buf), "exit %s\n", plate);
        send(sock, buf, strlen(buf), 0);

        // 接收伺服器回覆
        int n = recv(sock, buf, sizeof(buf)-1, 0);
        if (n <= 0) {
            printf("No reply or connection closed by server.\n");
            close(sock);
            continue;
        }
        buf[n] = '\0';
        printf("Server replied: %s", buf);

        // 只要回覆裡含有 "PAY " 就认为需要手动輸入缴费指令
        if (strstr(buf, "PAY ")) {
            char pay_cmd[64];
            printf("PAY> ");
            if (fgets(pay_cmd, sizeof(pay_cmd), stdin) == NULL) {
                close(sock);
                break;
            }
            pay_cmd[strcspn(pay_cmd, "\r\n")] = '\0';

            // 在 pay_cmd 後面加上換行再發送
            snprintf(buf, sizeof(buf), "%s\n", pay_cmd);
            send(sock, buf, strlen(buf), 0);

            // 接收離場結果
            n = recv(sock, buf, sizeof(buf)-1, 0);
            if (n > 0) {
                buf[n] = '\0';
                printf("Server replied: %s", buf);
            }
        }

        close(sock);
    }

    printf("Exit client terminated.\n");
    return 0;
}

// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <arpa/inet.h>

// #define BUF_SIZE 256

// int main(int argc, char *argv[]) {
//     if (argc != 3) {
//         fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
//         exit(EXIT_FAILURE);
//     }

//     const char *server_ip = argv[1];
//     int port = atoi(argv[2]);
//     char plate[32];
//     char buf[BUF_SIZE];

//     printf("輸入車牌後按 Enter 送出離場指令；輸入 \"quit\" 或 Ctrl-D 結束。\n");
//     while (1) {
//         printf("Plate> ");
//         if (fgets(plate, sizeof(plate), stdin) == NULL) break;  // Ctrl-D
//         plate[strcspn(plate, "\r\n")] = '\0';
//         if (plate[0] == '\0' || strcmp(plate, "quit") == 0) break;

//         // 建立連線
//         int sock = socket(AF_INET, SOCK_STREAM, 0);
//         if (sock < 0) { perror("socket"); continue; }
//         struct sockaddr_in serv_addr = {0};
//         serv_addr.sin_family = AF_INET;
//         serv_addr.sin_port = htons(port);
//         if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
//             fprintf(stderr, "Invalid address: %s\n", server_ip);
//             close(sock);
//             continue;
//         }
//         if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
//             perror("connect"); close(sock); continue;
//         }

//         // 發送 exit 指令
//         snprintf(buf, sizeof(buf), "exit %s\n", plate);
//         send(sock, buf, strlen(buf), 0);

//         // 接收伺服器回覆
//         int n = recv(sock, buf, sizeof(buf)-1, 0);
//         if (n <= 0) {
//             printf("No reply or connection closed by server.\n");
//             close(sock);
//             continue;
//         }
//         buf[n] = '\0';
//         printf("Server replied: %s", buf);

//         // 如果回覆包含 "請輸入 \"PAY <plate>\""，提示用戶輸入繳費指令
//         if (strstr(buf, "請輸入 \"PAY")) {
//             // 讀取 PAY 指令
//             char pay_cmd[64];
//             printf("PAY> ");
//             if (fgets(pay_cmd, sizeof(pay_cmd), stdin) == NULL) {
//                 close(sock);
//                 break;
//             }
//             pay_cmd[strcspn(pay_cmd, "\r\n")] = '\0';
//             // 重新發送 PAY 指令
//             send(sock, pay_cmd, strlen(pay_cmd), 0);
//             // 接收離場結果
//             n = recv(sock, buf, sizeof(buf)-1, 0);
//             if (n > 0) {
//                 buf[n] = '\0';
//                 printf("Server replied: %s", buf);
//             }
//         }

//         close(sock);
//     }

//     printf("Exit client terminated.\n");
//     return 0;
// }
