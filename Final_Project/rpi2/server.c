#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>
#include <sys/sem.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <fcntl.h>


static void display_digit_on_7seg(int digit) {
    const char *path = "/dev/seg_device";
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
        return;
    }
    if (digit < 0) digit = 0;
    if (digit > 9) digit = 9;
    char c = '0' + digit;
    if (write(fd, &c, 1) < 0) {
        fprintf(stderr, "Failed write to %s: %s\n", path, strerror(errno));
    }
    close(fd);
}

// 用来记录每辆进场车辆的信息
typedef struct {
    char *plate;         // 车牌
    time_t entry_time;   // 进场时间
    int paid;            // 是否已付费：0=否，1=是
    char lot;            // 所属车场：'A' 或 'B'
    int  sem_used;       // <— 新增：当初扣掉的 semaphore ID
    char ui_slot_letter;
} CarRecord;

// 全局记录表和锁
CarRecord records[100];  // 最多 100 辆车
int record_count = 0;
pthread_mutex_t lockRec = PTHREAD_MUTEX_INITIALIZER;

int server_socket;

// 六个 semaphore 
int semA_gen;   // A 区：一般车位
int semA_hand;  // A 区：残障车位
int semA_ele;   // A 区：电动车位

int semB_gen;   // B 区：一般车位
int semB_hand;  // B 区：残障车位
int semB_ele;   // B 区：电动车位

// 停车场数据
char* lotA_plates[50];  // 停车场 A 的车牌
int   lotA_count = 0;   // A 区现在有多少辆车

char* lotB_plates[50];
int   lotB_count = 0;

// 加锁保护共享数据
pthread_mutex_t lockA = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lockB = PTHREAD_MUTEX_INITIALIZER;

int g_ui_lotA_slot_status[10] = {0}; // 對應 Lot A 的 UI 槽位 A-J
int g_ui_lotB_slot_status[10] = {0}; // 對應 Lot B 的 UI 槽位 A-J
pthread_mutex_t g_ui_slot_status_lock = PTHREAD_MUTEX_INITIALIZER; // 保護上面的狀態數組

int g_ui_client_socket = -1;
pthread_mutex_t g_ui_socket_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int port;
    int is_ui_port; // 1 if UI port, 0 if command port
} PortAcceptArgs;

// 函數原型聲明 (如果它們定義在 main 或 start_server 之後)
void *handle_command_client(void *arg);
void *handle_ui_connection(void *arg);
void *accept_loop(void *args);

// VIP 车牌
#define SPECIAL_COUNT 25
const char* special_plates[SPECIAL_COUNT] = {
    "A123","A234","A345","A456","A567",
    "B123","B234","B345","B456","B567",
    "C123","C234","C345","C456","C567",
    "D123","D234","D345","D456","D567",
    "C345","E123","E234","E456","E567"
};

typedef struct {
    char *plate;        // 车牌
    timer_t timerid;    // POSIX 定时器 ID
    int  notify_count;  // 已通知次数
} CarTimer;

// 充电状态回调（原样保留）
void car_timer_callback(union sigval sv) {
    CarTimer *ct = (CarTimer*)sv.sival_ptr;
    ct->notify_count++;

    switch (ct->notify_count) {
        case 1:
            printf("車牌 %s，充电 50%%\n", ct->plate);
            break;
        case 2:
            printf("車牌 %s，充电 70%%\n", ct->plate);
            break;
        case 3:
            printf("車牌 %s，充電完成！\n", ct->plate);
            timer_delete(ct->timerid);
            free(ct->plate);
            free(ct);
            return;
    }
}

// 打印停车场现状（原样保留）
void print_parking_lot() {
    printf("=== 停車場 A 車牌 ===\n");
    pthread_mutex_lock(&lockA);
    for (int i = 0; i < lotA_count; ++i)
        printf("A: %s\n", lotA_plates[i]);
    pthread_mutex_unlock(&lockA);

    printf("=== 停車场 B 車牌 ===\n");
    pthread_mutex_lock(&lockB);
    for (int i = 0; i < lotB_count; ++i)
        printf("B: %s\n", lotB_plates[i]);
    pthread_mutex_unlock(&lockB);
}

// SIGINT 处理函数：打印信息并删除所有 semaphore
void sigint_handler(int signum) {
    printf("\n目前停車場車牌信息...\n");
    print_parking_lot();  // 列出結果
    signal(SIGINT, SIG_DFL);
    close(server_socket);
    // 删除六个 semaphore
    semctl(semA_gen,  0, IPC_RMID, 0);
    semctl(semA_hand, 0, IPC_RMID, 0);
    semctl(semA_ele,  0, IPC_RMID, 0);
    semctl(semB_gen,  0, IPC_RMID, 0);
    semctl(semB_hand, 0, IPC_RMID, 0);
    semctl(semB_ele,  0, IPC_RMID, 0);
    exit(0);
}

/* P () - returns 0 if OK; -1 if there was a problem */
int P(int s) {
    struct sembuf sop;
    sop.sem_num = 0;
    sop.sem_op = -1;
    sop.sem_flg = 0;
    if (semop(s, &sop, 1) < 0) {
        perror("P(): semop failed");
        exit(EXIT_FAILURE);
    } else {
        return 0;
    }
}
/* V() - returns 0 if OK; -1 if there was a problem */
int V(int s) {
    struct sembuf sop;
    sop.sem_num = 0;
    sop.sem_op = 1;
    sop.sem_flg = 0;
    if (semop(s, &sop, 1) < 0) {
        perror("V(): semop failed");
        exit(EXIT_FAILURE);
    } else {
        return 0;
    }
}
/* 新增：Console 线程，用于监听 stdin，当输入 'p' 并回车时打印停车场状态 */
void *console_thread_func(void *arg) {
    char cmd_buf[16];
    while (1) {
        if (fgets(cmd_buf, sizeof(cmd_buf), stdin) == NULL) {
            break;
        }
        // 去掉末尾换行
        cmd_buf[strcspn(cmd_buf, "\r\n")] = '\0';
        // 如果第一个字符是 'p'（可以根据需要改成其他按键），就打印状态
        if (cmd_buf[0] == 'p') {
            print_parking_lot();
        }
    }
    return NULL;
}
// ====== 新增：辅助函数 try_park_in_lot ======
/**
 * 尝试在某个停车场 (lot='A' 或 'B') 停车，优先用「专属」再用「一般」。
 * category = 'N' (一般)、'B' (残障)、'E' (电动)。
 * 返回 >=0: 成功时做过 P(sem_id)，并返回那个 sem_id
 * 返回 -1: 该停车场专属+一般都满
 */
int try_park_in_lot(char lot, char category) {
    int sem_spec = -1;  // 车种专属 semaphore ID
    int sem_gen  = -1;  // 一般车 semaphore ID

    if (lot == 'A') {
        sem_gen = semA_gen;
        if (category == 'B')      sem_spec = semA_hand;
        else if (category == 'E') sem_spec = semA_ele;
        else                      sem_spec = -1;  // 一般车直接走 semA_gen
    }
    else { // lot == 'B'
        sem_gen = semB_gen;
        if (category == 'B')      sem_spec = semB_hand;
        else if (category == 'E') sem_spec = semB_ele;
        else                      sem_spec = -1;
    }

    // (1) 先尝试专属
    if (sem_spec != -1) {
        int val_spec = semctl(sem_spec, 0, GETVAL);
        if (val_spec > 0) {
            P(sem_spec);
            return sem_spec;
        }
        // 否则专属满，继续尝试一般
    }
    // (2) 尝试一般
    int val_gen = semctl(sem_gen, 0, GETVAL);
    if (val_gen > 0) {
        P(sem_gen);
        // 如果是 A 区的一般车位，显示到七段
        if (sem_gen == semA_gen){
            int remainA = semctl(semA_gen, 0, GETVAL);
            display_digit_on_7seg(remainA);
        }        
        return sem_gen;
    }
    // (3) 走到这里说明专属+一般都没位子
    return -1;
}

// 函數：專門處理已註冊 UI 的連線，監聽其是否斷線
void *handle_ui_connection(void *arg) {
    int ui_socket = *(int *)arg; // 從參數獲取 UI 的 socket descriptor
    // free(arg); // 這個 arg 應該在線程結束時，也就是這個函數返回前釋放

    printf("[UI Handler] UI Connection Handler thread started for accepted socket %d.\n", ui_socket);

    // 1. 註冊這個 socket 為全局 UI socket
    pthread_mutex_lock(&g_ui_socket_lock);
    if (g_ui_client_socket != -1 && g_ui_client_socket != ui_socket) {
        // 如果之前已經有一個 UI socket 被註冊了，關閉舊的，使用新的
        printf("[UI Handler] Replacing old UI socket %d with new UI socket %d.\n", g_ui_client_socket, ui_socket);
        close(g_ui_client_socket); // 關閉舊的 socket
    }
    g_ui_client_socket = ui_socket; // 註冊新的 UI socket
    printf("[UI Handler] Socket %d registered as the active UI client.\n", g_ui_client_socket);
    pthread_mutex_unlock(&g_ui_socket_lock);

    char buffer[64];
    int n;

    // 2. 循環監聽 UI 是否斷線，或是否有其他來自 UI 的訊息
    while ((n = recv(ui_socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[n] = '\0';
        // 移除可能存在的換行符，以便更清晰地打印
        buffer[strcspn(buffer, "\r\n")] = 0;
        printf("[UI Handler] Received from UI (socket %d): \"%s\"\n", ui_socket, buffer);
    }

    // 3. recv 返回 <= 0，表示 UI 斷線或出錯
    printf("[UI Handler] UI (socket %d) recv returned %d. ", ui_socket, n);
    if (n == 0) {
        printf("Client disconnected gracefully.\n");
    } else { // n < 0
        perror("Recv error from UI");
    }

    pthread_mutex_lock(&g_ui_socket_lock);
    if (g_ui_client_socket == ui_socket) { // 再次確認是自己註冊的 socket
        printf("[UI Handler] Registered UI Client (socket %d) is disconnecting. Unregistering.\n", ui_socket);
        g_ui_client_socket = -1; // 取消註冊
    } else {
        printf("[UI Handler] UI (socket %d) was disconnecting, but it was no longer the registered UI (current g_ui_client_socket: %d).\n", ui_socket, g_ui_client_socket);
    }
    pthread_mutex_unlock(&g_ui_socket_lock);

    close(ui_socket); // 關閉這個 UI client 的 socket
    free(arg);      // 釋放為這個 UI 連線分配的 client_socket_ptr 的內存
    printf("[UI Handler] UI Connection Handler ended for socket %d.\n", ui_socket);
    return NULL;
}

// 輔助函數：根據停車場和車輛類型，找到一個可用的 UI 槽位字母並標記為佔用
// lot_char: 'A' 或 'B' (車輛實際停入的物理停車場)
// category: 'N', 'E', 'B' (車輛類型)
// 返回分配的字母 (A-J)，如果沒有可用則返回 0 (或 '?')
char assign_ui_slot_letter(char lot_char, char category) {
    pthread_mutex_lock(&g_ui_slot_status_lock);
    char assigned_letter = 0; // 0 表示未分配
    int *status_array = (lot_char == 'A') ? g_ui_lotA_slot_status : g_ui_lotB_slot_status;
    
    // 簡化邏輯：我們需要定義 UI 上 A-J 哪些槽位是給哪種類型的車
    // 假設 UI 設計：
    // 一般車 ('N'): 槽位 A, B, C, D, E (索引 0-4)
    // 電動車 ('E'): 槽位 F, G, H (索引 5-7)
    // 殘障車 ('B'): 槽位 I, J (索引 8-9)
    int start_index = 0, end_index = 0;

    if (category == 'N') { start_index = 0; end_index = 4; }      // A-E
    else if (category == 'E') { start_index = 5; end_index = 7; } // F-H
    else if (category == 'B') { start_index = 8; end_index = 9; } // I-J
    else {
        pthread_mutex_unlock(&g_ui_slot_status_lock);
        printf("[UI Slot Assign Error] Unknown category %c for UI slot assignment.\n", category);
        return 0; // 無法分配
    }

    for (int i = start_index; i <= end_index; ++i) {
        if (status_array[i] == 0) { // 找到第一個狀態為 0 (空閒) 的槽位
            status_array[i] = 1; // 標記為已佔用
            assigned_letter = 'A' + i;
            break;
        }
    }

    if (assigned_letter == 0) {
        printf("[UI Slot Assign Warn] No available UI slot for lot %c, category %c in range %c-%c.\n",
               lot_char, category, 'A' + start_index, 'A' + end_index);
    } else {
        printf("[UI Slot Assign Info] Assigned UI slot %c for lot %c, category %c.\n",
               assigned_letter, lot_char, category);
    }

    pthread_mutex_unlock(&g_ui_slot_status_lock);
    return assigned_letter;
}

// 輔助函數：釋放一個 UI 槽位字母
void release_ui_slot_letter(char lot_char, char ui_slot_to_release) {
    if (ui_slot_to_release < 'A' || ui_slot_to_release > 'J') {
        printf("[UI Slot Release Warn] Attempted to release invalid UI slot: %c\n", ui_slot_to_release);
        return; 
    }

    pthread_mutex_lock(&g_ui_slot_status_lock);
    int *status_array = (lot_char == 'A') ? g_ui_lotA_slot_status : g_ui_lotB_slot_status;
    int index = ui_slot_to_release - 'A';

    if (index >= 0 && index < 10) { // 再次確認索引有效
        if (status_array[index] == 1) {
            status_array[index] = 0; // 標記為空閒
            printf("[UI Slot Release Info] Released UI slot %c for lot %c.\n", ui_slot_to_release, lot_char);
        } else {
            printf("[UI Slot Release Warn] UI slot %c in lot %c was already free.\n", ui_slot_to_release, lot_char);
        }
    }
    pthread_mutex_unlock(&g_ui_slot_status_lock);
}

void *handle_command_client(void *arg) {
    int client_socket = *(int *)arg; // 這個 client_socket 是來自 client.c 或 exit.c
    // free(arg); // 改到線程結束前釋放

    printf("[Cmd Handler] Command Client Handler started for socket %d.\n", client_socket);

    char buffer[64]; // 你原來的 buffer 大小
    int n;
    int use_sem_id = -1;
    char lot_name = '?'; // 最终停车场 'A' 或 'B'
    
    while ((n = recv(client_socket, buffer, sizeof(buffer)-1, 0)) > 0) {
        buffer[n] = '\0';
        buffer[strcspn(buffer, "\r\n")] = 0; // 去掉末尾的換行符
       
        // 1) 处理 exit -> 计算费用并回传 (你的原始邏輯)
        if (strncmp(buffer, "exit ", 5) == 0) {
            char plate[32]; // 局部變數
            sscanf(buffer + 5, "%31s", plate);
            pthread_mutex_lock(&lockRec);
            int idx = -1;
            for (int i_rec = 0; i_rec < record_count; ++i_rec) { // 使用不同迴圈變數名
                if (strcmp(records[i_rec].plate, plate) == 0 && !records[i_rec].paid) {
                    idx = i_rec;
                    break;
                }
            }
           
            if (idx < 0) {
                pthread_mutex_unlock(&lockRec);
                send(client_socket, "找不到此车牌或已离场\n", strlen("找不到此车牌或已离场\n"), 0);
                continue; 
            }
            time_t now_exit = time(NULL); // 局部變數
            double duration_exit = difftime(now_exit, records[idx].entry_time);
            int fee_exit = (int)(duration_exit * 50);
            char msg_fee[128]; // 局部變數
            snprintf(msg_fee, sizeof(msg_fee),"車牌 %s 停車 %.0f 秒，應繳 %d 元，請輸入 \"PAY %s\"\n",plate, duration_exit, fee_exit, plate);
            send(client_socket, msg_fee, strlen(msg_fee), 0); // 回應給當前的 client_socket
            pthread_mutex_unlock(&lockRec);
            continue;
        }

        // 2) 处理 PAY -> 真正离场 (你的原始邏輯)
        if (strncmp(buffer, "PAY ", 4) == 0) {
            char plate[32]; // 局部變數，來自 PAY 指令
            sscanf(buffer + 4, "%31s", plate);

            pthread_mutex_lock(&lockRec);           
            int idx = -1;
            for (int i_rec = 0; i_rec < record_count; ++i_rec) { // 使用不同迴圈變數名
                if (strcmp(records[i_rec].plate, plate) == 0 && !records[i_rec].paid) {
                    idx = i_rec;
                    break;
                }
            }
            if (idx < 0) {
                send(client_socket, "找不到此车牌或未缴费\n", strlen("找不到此车牌或未缴费\n"), 0); // 回應給當前的 client_socket
                pthread_mutex_unlock(&lockRec);
                continue;
            }
            records[idx].paid = 1;
            int sem = records[idx].sem_used;
            char lot = records[idx].lot; // 獲取車輛所在的停車場
            char ui_slot_to_release = records[idx].ui_slot_letter;
            const char *slotType = NULL; // 你的原始 slotType 判斷邏輯
            if (sem == semA_gen || sem == semB_gen) slotType = "一般";
            else if (sem == semA_hand || sem == semB_hand) slotType = "殘障";
            else if (sem == semA_ele || sem == semB_ele) slotType = "電動";
            else slotType = "未知";
            
            // 从 A/B 区移除车牌 (你的原始邏輯)
            if (lot == 'A') {
                pthread_mutex_lock(&lockA);
                for (int i_lot = 0; i_lot < lotA_count; ++i_lot) { // 使用不同的迴圈變數名
                    if (strcmp(lotA_plates[i_lot], plate) == 0) {
                        free(lotA_plates[i_lot]);
                        memmove(&lotA_plates[i_lot], &lotA_plates[i_lot+1], (lotA_count - 1 - i_lot) * sizeof(char*));
                        lotA_count--;
                        break;
                    }
                }
                pthread_mutex_unlock(&lockA);
            } else { // lot == 'B'
                pthread_mutex_lock(&lockB);
                for (int i_lot = 0; i_lot < lotB_count; ++i_lot) { // 使用不同的迴圈變數名
                    if (strcmp(lotB_plates[i_lot], plate) == 0) {
                        free(lotB_plates[i_lot]);
                        memmove(&lotB_plates[i_lot], &lotB_plates[i_lot+1], (lotB_count - 1 - i_lot) * sizeof(char*));
                        lotB_count--;
                        break;
                    }
                }
                pthread_mutex_unlock(&lockB);
            }

            V(sem); // 釋放 semaphore (你的原始邏輯)
            if (sem == semA_gen) display_digit_on_7seg(semctl(semA_gen, 0, GETVAL)); // 七段顯示 (你的原始邏輯)

            // ★★★ 新增：發送 EXIT 指令給已註冊的 UI ★★★
            if (ui_slot_to_release != 0 && ui_slot_to_release != '?') { 
                char msg_to_ui_exit_slot[128];
                char slot_representation_for_ui[4]; // 最多 "+I\0" 或 "I\0"

                if (lot == 'B') { // 如果車在 B 停車場 (UI 的第二層)
                    snprintf(slot_representation_for_ui, sizeof(slot_representation_for_ui), "+%c", ui_slot_to_release);
                } else { // 車在 A 停車場 (UI 的第一層)
                    snprintf(slot_representation_for_ui, sizeof(slot_representation_for_ui), "%c", ui_slot_to_release);
                }
                
                snprintf(msg_to_ui_exit_slot, sizeof(msg_to_ui_exit_slot), "EXIT_SLOT:%s\n", slot_representation_for_ui);
                
                pthread_mutex_lock(&g_ui_socket_lock);
                if (g_ui_client_socket != -1) {
                    if (send(g_ui_client_socket, msg_to_ui_exit_slot, strlen(msg_to_ui_exit_slot), 0) < 0) {
                        perror("[Cmd Handler] Send EXIT_SLOT notification to UI failed");
                        printf("[Cmd Handler Warn] UI client (socket: %d) seems disconnected during EXIT_SLOT. Unregistering.\n", g_ui_client_socket);
                        close(g_ui_client_socket); 
                        g_ui_client_socket = -1;   
                    } else {
                        printf("[Cmd Handler Info] Successfully sent to UI (socket %d): %s", g_ui_client_socket, msg_to_ui_exit_slot);
                    }
                }
                pthread_mutex_unlock(&g_ui_socket_lock);
                release_ui_slot_letter(lot, ui_slot_to_release); 
            } else {
                // 如果沒有有效的 ui_slot_letter，還是發送舊的 EXIT:PLATE:LOT 作為回退
                printf("[Cmd Handler Warn] No valid ui_slot_letter for plate %s (Lot %c) to send EXIT_SLOT. Sending fallback EXIT:PLATE:LOT.\n", plate, lot);
                char msg_to_ui_fallback_exit[128];
                snprintf(msg_to_ui_fallback_exit, sizeof(msg_to_ui_fallback_exit), "EXIT:%s:%c\n", plate, lot);
                pthread_mutex_lock(&g_ui_socket_lock);
                if (g_ui_client_socket != -1) {
                     if (send(g_ui_client_socket, msg_to_ui_fallback_exit, strlen(msg_to_ui_fallback_exit), 0) < 0) {
                        perror("[Cmd Handler] Send fallback EXIT notification to UI failed");
                        close(g_ui_client_socket); g_ui_client_socket = -1;
                    } else {
                        printf("[Cmd Handler Info] Sent fallback EXIT to UI (socket %d): %s", g_ui_client_socket, msg_to_ui_fallback_exit);
                    }
                }
                pthread_mutex_unlock(&g_ui_socket_lock);
            }
            // ★★★ 新增結束 ★★★

            // 从 records 中移除 (你的原始邏輯)
            // 確保在 pthread_mutex_unlock(&lockRec); 之前完成
            char* plate_to_free = records[idx].plate; // 先取得指標
            memmove(&records[idx], &records[idx+1], (record_count - 1 - idx) * sizeof(CarRecord));
            record_count--;
            free(plate_to_free); // 最後釋放
            pthread_mutex_unlock(&lockRec);

            // 回传离场成功消息給當前的 client_socket (你的原始邏輯)
            int remain = semctl(sem, 0, GETVAL);
            // 你的原始 printf 日誌
            printf("[Info] 繳費完成，%c 停車場 %s 已離場，剩餘%s車位：%d\n",lot, plate, slotType, remain);
            char reply[128]; // 局部變數
            snprintf(reply, sizeof(reply),"繳費完成，%c 停車場，車牌 %s 已離場，剩餘%s車位：%d\n",lot, plate, slotType, remain);
            send(client_socket, reply, strlen(reply), 0); // 回應給當前的 client_socket

            continue;
        }

        // 進場 (假設 buffer 中是純車牌)
        int gate_choice = 1; // 默認為 Gate 1
        char plate[32];      // 用於儲存不帶前綴的純車牌號
        memset(plate, 0, sizeof(plate)); // 清空 plate 緩衝區

        if (buffer[0] == '!') {
            gate_choice = 2;
            sscanf(buffer + 1, "%31s", plate); // 從第二個字元開始解析
            printf("[Cmd Handler] Received Gate 2 entry request for plate: %s\n", plate);
        } else if (buffer[0] == '?') {
            gate_choice = 1;
            sscanf(buffer + 1, "%31s", plate); // 從第二個字元開始解析
            printf("[Cmd Handler] Received Gate 1 entry request for plate: %s\n", plate);
        } else {
            gate_choice = 1; // 無前綴，默認為 Gate 1
            sscanf(buffer, "%31s", plate);
        }

        if (strlen(plate) == 0) { // 如果 sscanf 後 plate 是空的 (例如 buffer 只有換行符)
            continue; 
        }

        // 你的原始 printf 日誌，會根據 plate[0] 打印車輛類型
        // printf("[Cmd Handler] Processing entry for plate: %s by socket %d\n", plate, client_socket); // 這行會在你下面的 category 判斷後打印

        // 4) 分类车辆 (你的原始邏輯)
        char category; // 局部變數
        if (plate[0] == 'E') {
            category = 'E';
            printf("[Info] %s 是電動車\n", plate); // 你的原始日誌
        } else if (plate[0] == 'B') {
            category = 'B';
            printf("[Info] %s 是残障車\n", plate); // 你的原始日誌
        } else {
            category = 'N';
            printf("[Info] %s 是一般車\n", plate); // 你的原始日誌
        }

        // 5) 决定主停车场 (你的原始邏輯)
        int is_special = 0;
        for (int i_spec = 0; i_spec < SPECIAL_COUNT; ++i_spec) { // 使用不同的迴圈變數名
            if (strcmp(plate, special_plates[i_spec]) == 0) {
                is_special = 1;
                break;
            }
        }
        char primary_lot  = is_special ? 'B' : 'A';
        char fallback_lot = is_special ? 'A' : '?';  
        
        lot_name = '?'; // 重置 lot_name (你原有的)
        use_sem_id = -1; // 重置 use_sem_id (你原有的)

        // 6) 先在 primary_lot 试着 P(…) (你的原始邏輯)
        use_sem_id = try_park_in_lot(primary_lot, category);
        if (use_sem_id >= 0) {
            lot_name = primary_lot;
        } else {
            if (is_special) {
                use_sem_id = try_park_in_lot(fallback_lot, category);
                if (use_sem_id >= 0) {
                    lot_name = fallback_lot;
                    printf("[Info] %c 停車場已满，%s 改停 %c 停車場\n", primary_lot, plate, lot_name); // 你的原始日誌
                }
            }
        }

        if (use_sem_id >= 0) { // 停車成功
            // 7) 更新停車場數據等 (你的原始邏輯)
            char assigned_ui_slot = assign_ui_slot_letter(lot_name, category);
            if (assigned_ui_slot == 0 || assigned_ui_slot == '?') {
                printf("[Cmd Handler Warn] Failed to assign a UI slot letter for plate %s in lot %c, category %c. UI slot features might be limited for this car.\n", plate, lot_name, category);
                // 即使分配失敗，物理停車仍然進行，ui_slot_letter 記錄為 0 或 '?'
            }
            if (lot_name == 'A') {
                pthread_mutex_lock(&lockA);
                lotA_plates[lotA_count++] = strdup(plate);
                pthread_mutex_unlock(&lockA);
                // 根據 category 再細分列印 (你的原始日誌)
                if (use_sem_id == semA_hand) printf("進場，A停車場，剩餘殘障車位：%d\n", semctl(semA_hand, 0, GETVAL));
                else if (use_sem_id == semA_ele) printf("進場，A停車場，剩餘電動車位：%d\n", semctl(semA_ele, 0, GETVAL));
                else printf("進場，A停車場，剩餘一般車位：%d\n", semctl(semA_gen, 0, GETVAL));
            } else { // lot_name == 'B'
                pthread_mutex_lock(&lockB);
                lotB_plates[lotB_count++] = strdup(plate);
                pthread_mutex_unlock(&lockB);
                if (use_sem_id == semB_hand) printf("進場，B停車場，剩餘殘障車位：%d\n", semctl(semB_hand, 0, GETVAL));
                else if (use_sem_id == semB_ele) printf("進場，B停車場，剩餘電動車位：%d\n", semctl(semB_ele, 0, GETVAL));
                else printf("進場，B停車場，剩餘一般車位：%d\n", semctl(semB_gen, 0, GETVAL));
            }

            // ★★★ 新增：發送 ENTER 指令給已註冊的 UI ★★★
            char type_str_ui[20]; // 用於發送給 UI 的類型字串
            if (category == 'E') strcpy(type_str_ui, "EV");
            else if (category == 'B') strcpy(type_str_ui, "HANDICAP");
            else strcpy(type_str_ui, "NORMAL");
            
            char msg_to_ui_enter[128];
            char plate_for_ui[33]; // 用於構建發送給 UI 的車牌字串

            if (gate_choice == 2) {
                // 如果是 Gate 2 請求，則在發送給 UI 的車牌前加上 '!'
                snprintf(plate_for_ui, sizeof(plate_for_ui), "!%s", plate);
            } else {
                // 如果是 Gate 1 請求，則發送不帶前綴的純車牌
                // (按照你的要求「就像之前一樣傳給 UI 就好」)
                strncpy(plate_for_ui, plate, sizeof(plate_for_ui) - 1);
                plate_for_ui[sizeof(plate_for_ui) - 1] = '\0';
            }
            // char msg_to_ui_enter[128];
            snprintf(msg_to_ui_enter, sizeof(msg_to_ui_enter), "ENTER:%s:%s:%c\n",
                     plate_for_ui, type_str_ui, lot_name);
            pthread_mutex_lock(&g_ui_socket_lock);
            if (g_ui_client_socket != -1) {
                if (send(g_ui_client_socket, msg_to_ui_enter, strlen(msg_to_ui_enter), 0) < 0) {
                    perror("[Cmd Handler] Send ENTER notification to UI failed");
                    printf("[Cmd Handler Warn] UI client (socket: %d) seems disconnected during ENTER. Unregistering.\n", g_ui_client_socket);
                    close(g_ui_client_socket);
                    g_ui_client_socket = -1;
                } else {
                    // 你原有的 printf("[Info] Sent to UI: %s", msg_to_ui_enter); 已被下面的特定日誌取代
                    printf("[Cmd Handler Info] Successfully sent to UI (socket %d): %s", g_ui_client_socket, msg_to_ui_enter);
                }
            }
            pthread_mutex_unlock(&g_ui_socket_lock);
            // ★★★ 新增結束 ★★★

            // 8) 充電計時器 (你的原始邏輯)
            if (category == 'E' && (use_sem_id == semA_ele || use_sem_id == semB_ele)) {
                CarTimer *ct = malloc(sizeof(*ct));
                ct->plate = strdup(plate);
                ct->notify_count = 0;
                struct sigevent sev;
                memset(&sev, 0, sizeof(sev));
                sev.sigev_notify = SIGEV_THREAD;
                sev.sigev_notify_function = car_timer_callback;
                sev.sigev_notify_attributes = NULL;
                sev.sigev_value.sival_ptr = ct;
    
                if (timer_create(CLOCK_REALTIME, &sev, &ct->timerid) == -1) {
                    perror("timer_create");
                    free(ct->plate);
                    free(ct);
                } else {
                    struct itimerspec its;
                    memset(&its, 0, sizeof(its));
                    its.it_value.tv_sec    = 1;  // 第一次在 5 秒
                    its.it_interval.tv_sec = 1;  // 然后每 5 秒一次
    
                    if (timer_settime(ct->timerid, 0, &its, NULL) == -1)
                        perror("timer_settime");
                }
            }
    

            // 9) 記錄進場信息 (你的原始邏輯)
            time_t now_enter = time(NULL); // 局部變數
            pthread_mutex_lock(&lockRec);
            records[record_count].plate      = strdup(plate); // 這裡的 plate 是進場車牌
            records[record_count].entry_time = now_enter;
            records[record_count].paid       = 0;
            records[record_count].lot        = lot_name;
            records[record_count].sem_used   = use_sem_id;
            records[record_count].ui_slot_letter = assigned_ui_slot;
            record_count++;
            pthread_mutex_unlock(&lockRec);

            // 回應給當前 client_socket (例如 client.c)
            // 你原始的程式碼在進場成功時沒有明確的 send 給 client.c，主要是打印日誌。
            // 如果 client.c 需要一個回應，可以在這裡加。例如：
            char simple_reply_parked[64];
            snprintf(simple_reply_parked, sizeof(simple_reply_parked), "OK: %s parked in Lot %c.\n", plate, lot_name);
            send(client_socket, simple_reply_parked, strlen(simple_reply_parked),0);

        } else { // 停車失敗
            // ★★★ 新增：發送 PARK_FAIL 指令給已註冊的 UI ★★★
            char reason_str[64]; // 局部變數
            if (is_special) { // 如果是特殊車輛，且兩邊都滿
                 snprintf(reason_str, sizeof(reason_str), "All lots full for VIP");
                 printf("[Warn] %s：%c、%c 停車場都滿，進場失敗\n", plate, primary_lot, fallback_lot); // 你的原始日誌
            } else { // 普通車輛，或特殊車輛但 fallback_lot 也沒被嘗試
                 snprintf(reason_str, sizeof(reason_str), "Lot %c full", primary_lot);
                 printf("[Warn] %s：%c 停車場滿，進場失敗\n", plate, primary_lot); // 你的原始日誌
            }
            
            char msg_to_ui_park_fail[128];
            snprintf(msg_to_ui_park_fail, sizeof(msg_to_ui_park_fail), "PARK_FAIL:%s:%s\n", plate, reason_str);
            pthread_mutex_lock(&g_ui_socket_lock);
            if (g_ui_client_socket != -1) {
                if (send(g_ui_client_socket, msg_to_ui_park_fail, strlen(msg_to_ui_park_fail), 0) < 0) {
                    perror("[Cmd Handler] Send PARK_FAIL notification to UI failed");
                    printf("[Cmd Handler Warn] UI client (socket: %d) seems disconnected during PARK_FAIL. Unregistering.\n", g_ui_client_socket);
                    close(g_ui_client_socket);
                    g_ui_client_socket = -1;
                } else {
                    printf("[Cmd Handler Info] Successfully sent to UI (socket %d): %s", g_ui_client_socket, msg_to_ui_park_fail);
                }
            }
            pthread_mutex_unlock(&g_ui_socket_lock);
            // ★★★ 新增結束 ★★★

            // 回應給當前 client_socket (例如 client.c)
            // 你原來的 send(client_socket, "所有停車場已滿\n", ...) 已經做了這個
            // 可以考慮發送更詳細的失敗原因
            char client_fail_reply[128];
            snprintf(client_fail_reply, sizeof(client_fail_reply), "Parking Failed: %s\n", reason_str);
            send(client_socket, client_fail_reply, strlen(client_fail_reply), 0);
        }
        continue; // 確保在處理完一個完整指令後 continue

    } // end while ((n = recv(...)))

    // 客戶端斷開 (recv 返回 <=0)
    printf("[Cmd Handler] Command Client (socket %d) disconnected or command loop ended.\n", client_socket);
    close(client_socket);
    free(arg); // 釋放為這個命令客戶端分配的 client_socket_ptr
    return NULL;
}
void *accept_loop(void *args_ptr) {
    PortAcceptArgs *args = (PortAcceptArgs *)args_ptr;
    int port_to_listen = args->port;
    int is_ui_port_flag = args->is_ui_port;

    int listening_socket;
    struct sockaddr_in local_server_addr;
    struct sockaddr_in accepted_client_addr;
    socklen_t accepted_client_len = sizeof(accepted_client_addr);

    if ((listening_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("[Accept Loop] Error creating listening socket");
        free(args_ptr);
        return NULL;
    }

    int yes = 1;
    if (setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("[Accept Loop] Error setting socket option SO_REUSEADDR");
        close(listening_socket);
        free(args_ptr);
        return NULL;
    }
    
    memset(&local_server_addr, 0, sizeof(local_server_addr));
    local_server_addr.sin_family = AF_INET;
    local_server_addr.sin_port = htons(port_to_listen);
    local_server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listening_socket, (struct sockaddr *)&local_server_addr, sizeof(local_server_addr)) == -1) {
        perror("[Accept Loop] Error binding listening socket");
        fprintf(stderr, "[Accept Loop] Failed to bind on port %d\n", port_to_listen);
        close(listening_socket);
        free(args_ptr);
        return NULL;
    }

    if (listen(listening_socket, SOMAXCONN) == -1) {
        perror("[Accept Loop] Error listening on socket");
        close(listening_socket);
        free(args_ptr);
        return NULL;
    }

    printf("[Accept Loop] Server listening on port %d for %s clients.\n", port_to_listen, is_ui_port_flag ? "UI" : "Command");

    while (1) {
        int accepted_socket = accept(listening_socket, (struct sockaddr*)&accepted_client_addr, &accepted_client_len);
        if (accepted_socket == -1) {
            if (errno == EINTR) continue; // 被信號中斷，重試
            perror("[Accept Loop] Error accepting connection");
            // 考慮是否應該退出循環或伺服器
            break; 
        }

        printf("[Accept Loop - Port %d] Accepted new connection, assigned socket: %d\n", port_to_listen, accepted_socket);

        int *socket_ptr = malloc(sizeof(int));
        if (socket_ptr == NULL) {
            perror("[Accept Loop] Failed to malloc for socket_ptr");
            close(accepted_socket);
            continue;
        }
        *socket_ptr = accepted_socket;

        pthread_t new_thread_id;
        void *(*thread_function)(void *) = is_ui_port_flag ? handle_ui_connection : handle_command_client;
        const char *thread_type_str = is_ui_port_flag ? "UI handler" : "Command client handler";

        if (pthread_create(&new_thread_id, NULL, thread_function, (void*)socket_ptr) != 0) {
            perror("[Accept Loop] Error creating thread");
            fprintf(stderr, "[Accept Loop] Failed to create %s thread for socket %d\n", thread_type_str, accepted_socket);
            close(accepted_socket);
            free(socket_ptr);
        } else {
            printf("[Accept Loop] Successfully created %s thread for socket %d.\n", thread_type_str, accepted_socket);
            pthread_detach(new_thread_id);
        }
    }

    printf("[Accept Loop] Exiting for port %d.\n", port_to_listen);
    close(listening_socket);
    free(args_ptr);
    return NULL;
}

int initialize_semaphores() { 
    int keyA_gen  = 1234; int keyA_hand = 1235; int keyA_ele  = 1236;
    int keyB_gen  = 5678; int keyB_hand = 5679; int keyB_ele  = 5680;

    semA_gen  = semget(keyA_gen,  1, IPC_CREAT | 0666);
    semA_hand = semget(keyA_hand, 1, IPC_CREAT | 0666);
    semA_ele  = semget(keyA_ele,  1, IPC_CREAT | 0666);
    semB_gen  = semget(keyB_gen,  1, IPC_CREAT | 0666);
    semB_hand = semget(keyB_hand, 1, IPC_CREAT | 0666);
    semB_ele  = semget(keyB_ele,  1, IPC_CREAT | 0666);

    if (semA_gen < 0 || semA_hand < 0 || semA_ele < 0 ||
        semB_gen < 0 || semB_hand < 0 || semB_ele < 0) {
        perror("Error creating semaphores");
        return -1;
    }
    // 初始化六个 semaphore 的值
    if (semctl(semA_gen,  0, SETVAL, 5) == -1 ||
        semctl(semA_hand, 0, SETVAL, 2) == -1 ||
        semctl(semA_ele,  0, SETVAL, 3) == -1 ||
        semctl(semB_gen,  0, SETVAL, 5) == -1 ||
        semctl(semB_hand, 0, SETVAL, 2) == -1 ||
        semctl(semB_ele,  0, SETVAL, 3) == -1) {
        perror("Error initializing semaphores");
        return -1;
    }
    printf("Semaphores initialized successfully.\n");
    return 0;
}

// start_server 現在接收兩個 port 號
int start_server(int command_port, int ui_port) {
    if (initialize_semaphores() != 0) {
        return -1; // 信號量初始化失敗
    }

    pthread_t cmd_accept_thread_id, ui_accept_thread_id;

    PortAcceptArgs *cmd_args = malloc(sizeof(PortAcceptArgs));
    if (!cmd_args) { perror("malloc for cmd_args failed"); return -1; }
    cmd_args->port = command_port;
    cmd_args->is_ui_port = 0; // 0 表示命令 port

    PortAcceptArgs *ui_args = malloc(sizeof(PortAcceptArgs));
    if (!ui_args) { perror("malloc for ui_args failed"); free(cmd_args); return -1; }
    ui_args->port = ui_port;
    ui_args->is_ui_port = 1; // 1 表示 UI port

    printf("Attempting to start command client accept loop on port %d...\n", command_port);
    if (pthread_create(&cmd_accept_thread_id, NULL, accept_loop, (void*)cmd_args) != 0) {
        perror("Error creating command accept thread");
        free(cmd_args);
        free(ui_args);
        return -1;
    }
    pthread_detach(cmd_accept_thread_id); // 分離線程，讓它們在後台運行

    printf("Attempting to start UI client accept loop on port %d...\n", ui_port);
    if (pthread_create(&ui_accept_thread_id, NULL, accept_loop, (void*)ui_args) != 0) {
        perror("Error creating UI accept thread");
        free(ui_args); // ui_args 肯定還沒被使用
        return -1;
    }
    pthread_detach(ui_accept_thread_id);

    printf("Server accept loops started for command port %d and UI port %d.\n", command_port, ui_port);
    // 主調用線程（例如 main 中的線程）現在可以繼續執行其他任務或等待。
    // 因為 accept_loop 是無限循環，start_server 會在這裡返回，而 accept 線程在後台運行。
    return 0; // 表示成功啟動了 accept 線程
}

extern int server_socket; // 如果 sigint_handler 仍要用

int main(int argc, char* argv[]) {
    if (argc != 3) { // 需要兩個 port 參數
        fprintf(stderr, "Usage: %s <command_port> <ui_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    signal(SIGINT, sigint_handler); 

    // 啟動 Console 線程 (你的原始邏輯)
    pthread_t console_thread_id;
    if (pthread_create(&console_thread_id, NULL, console_thread_func, NULL) != 0) {
        perror("Error creating console thread");
        exit(EXIT_FAILURE);
    }
    pthread_detach(console_thread_id);
    
    int command_port = atoi(argv[1]);
    int ui_port = atoi(argv[2]);

    if (start_server(command_port, ui_port) != 0) {
        fprintf(stderr, "Failed to start server accept loops.\n");
        exit(EXIT_FAILURE);
    }
    
    // 主線程保持運行，因為 accept_loop 線程已經 detach 獨立運行了
    printf("Main thread is now active. Server is running in detached threads.\n");
    printf("Press Ctrl+C to exit.\n");
    while(1) {
        sleep(60); // 例如，主線程可以定期做一些維護工作或 просто sleep
        // 保持主線程活躍，否則如果 main 退出，整個程序可能結束
    }
    
    return 0; // 正常情況下不會執行到這裡，因為上面是無限循環
}

