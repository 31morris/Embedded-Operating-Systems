
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int main(int argc, char *argv[]) {
    
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* vipPlates[] = {"A123", "B234", "C345", "E456", "E567"};
    int count = sizeof(vipPlates) / sizeof(vipPlates[0]);

    printf("以下車牌是VIP用戶：\n");
    for (int i = 0; i < count; i++) {
        printf("- %s\n", vipPlates[i]);
    }
    const char *server_ip = argv[1];
    int port = atoi(argv[2]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return EXIT_FAILURE;
    }

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sock);
        return EXIT_FAILURE;
    }

    printf("Connected to %s:%d\n", server_ip, port);

    char buffer[1024];
    char plate[32];

    while (1) {
        printf("請輸入車牌號碼 (輸入 'quit' 結束): ");
        if (!fgets(plate, sizeof(plate), stdin)) {
            // End of input (e.g., Ctrl+D)
            break;
        }
        plate[strcspn(plate, "\r\n")] = '\0';

        if (strcmp(plate, "quit") == 0) {
            printf("Client shutdown.\n");
            break;
        }

        // 只送車牌號碼
        snprintf(buffer, sizeof(buffer), "%s", plate);

        if (send(sock, buffer, strlen(buffer), 0) < 0) {
            perror("send");
            break;
        }
    }

    close(sock);
    return EXIT_SUCCESS;
}

// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <arpa/inet.h>

// int main(int argc, char *argv[]) {
//     if (argc != 3) {
//         fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
//         return EXIT_FAILURE;
//     }
//     const char *server_ip = argv[1];
//     int port = atoi(argv[2]);

//     int sock = socket(AF_INET, SOCK_STREAM, 0);
//     if (sock < 0) { perror("socket"); return EXIT_FAILURE; }

//     struct sockaddr_in serv_addr;
//     memset(&serv_addr, 0, sizeof(serv_addr));
//     serv_addr.sin_family = AF_INET;
//     serv_addr.sin_port = htons(port);
//     if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
//         perror("inet_pton"); close(sock); return EXIT_FAILURE;
//     }
//     if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
//         perror("connect"); close(sock); return EXIT_FAILURE;
//     }
//     printf("Connected to %s:%d\n", server_ip, port);

//     char buffer[1024];
//     char command[32];
//     char plate[32];

//     while (1) {
//         printf("請輸入指令 (enter / exit / quit): ");
//         if (!fgets(command, sizeof(command), stdin)) break;
//         command[strcspn(command, "\r\n")] = 0;

//         if (strcmp(command, "enter") == 0) {
//             // 進場：只傳車牌
//             printf("請輸入車牌號碼: ");
//             if (!fgets(plate, sizeof(plate), stdin)) break;
//             plate[strcspn(plate, "\r\n")] = 0;

//             // <<< 這裡只送車牌，不加任何前綴
//             snprintf(buffer, sizeof(buffer), "%s", plate);
//         }
//         else if (strcmp(command, "exit") == 0) {
//             // 離場：保留 "exit 車牌"
//             printf("請輸入車牌號碼: ");
//             if (!fgets(plate, sizeof(plate), stdin)) break;
//             plate[strcspn(plate, "\r\n")] = 0;

//             snprintf(buffer, sizeof(buffer), "exit %s", plate);
//         }
//         else if (strcmp(command, "quit") == 0) {
//             printf("Client shutdown.\n");
//             break;
//         }
//         else {
//             printf("無效指令，請輸入 enter、exit 或 quit\n");
//             continue;
//         }

//         if (send(sock, buffer, strlen(buffer), 0) < 0) {
//             perror("send");
//             break;
//         }
//     }

//     close(sock);
//     return EXIT_SUCCESS;
// }