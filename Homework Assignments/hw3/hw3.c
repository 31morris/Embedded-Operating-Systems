

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <limits.h>
#include <signal.h>
#include <errno.h>

#define NUM_DELIVERY_PERSONS 2
#define MAX_CLIENTS 1000
#define MAX_QUEUE   1000
#define BUFFER_SIZE 512

// remain_time[i] = 第 i 位外送員還要送完隊列裡所有訂單剩餘秒數
int remain_time[NUM_DELIVERY_PERSONS];
pthread_mutex_t remain_time_mutex = PTHREAD_MUTEX_INITIALIZER;

// ---------------------------
//    商店與訂單相關結構
// ---------------------------
typedef enum { ORDER_PENDING, ORDER_IN_QUEUE, ORDER_DELIVERING, ORDER_DELIVERED, ORDER_CANCELED } OrderStatus;

typedef struct {
    char name[50];
    int price;
    int total_quantity;
} Item;

typedef struct {
    char name[50];
    int distance;  // km → 送餐秒數
    Item items[10];
    int numItems;
} Shop;

//Order 結構：每張訂單完整資訊
typedef struct Order {
    int client_socket;
    int shopIndex;
    int delivery_secs;
    int price;
    OrderStatus status;
    int assignedPerson;
    int remaining_secs;
    int cancelled;
    pthread_mutex_t lock;
} Order;

//    外送員 (DeliveryPerson)
typedef struct {
    int id;   // 0 或 1
    Order *queue[MAX_QUEUE];
    int front, rear;
    pthread_mutex_t queue_mutex;
    pthread_cond_t  queue_cond;
} DeliveryPerson;

DeliveryPerson deliveryPersons[NUM_DELIVERY_PERSONS];

int server_socket;

//   初始化商店清單
void initShopList(Shop *shops) {
    strcpy(shops[0].name, "Dessert shop");
    shops[0].distance = 3;
    strcpy(shops[0].items[0].name, "cookie");
    shops[0].items[0].price = 60;
    shops[0].items[0].total_quantity = 0;
    strcpy(shops[0].items[1].name, "cake");
    shops[0].items[1].price = 80;
    shops[0].items[1].total_quantity = 0;
    shops[0].numItems = 2;

    strcpy(shops[1].name, "Beverage shop");
    shops[1].distance = 5;
    strcpy(shops[1].items[0].name, "tea");
    shops[1].items[0].price = 40;
    shops[1].items[0].total_quantity = 0;
    strcpy(shops[1].items[1].name, "boba");
    shops[1].items[1].price = 70;
    shops[1].items[1].total_quantity = 0;
    shops[1].numItems = 2;

    strcpy(shops[2].name, "Diner");
    shops[2].distance = 8;
    strcpy(shops[2].items[0].name, "fried-rice");
    shops[2].items[0].price = 120;
    shops[2].items[0].total_quantity = 0;
    strcpy(shops[2].items[1].name, "Egg-drop-soup");
    shops[2].items[1].price = 50;
    shops[2].items[1].total_quantity = 0;
    shops[2].numItems = 2;
}

//  回傳「shop list」給 client
void getShopList(Shop *shops, char *response) {
    response[0] = '\0';
    for (int i = 0; i < 3; i++) {
        sprintf(response + strlen(response), "%s:%dkm\n", shops[i].name, shops[i].distance);
        strcat(response + strlen(response), "- ");
        for (int j = 0; j < shops[i].numItems; j++) {
            sprintf(response + strlen(response), "%s:$%d", shops[i].items[j].name, shops[i].items[j].price);
            if (j < shops[i].numItems - 1) {
                strcat(response + strlen(response), "|");
            }
        }
        if (i < 2) strcat(response + strlen(response), "\n");
    }
}

//   找 itemName 屬於哪家店 (0~2)，找不到回 -1
int findShopIndexByItem(Shop *shops, const char *itemName) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < shops[i].numItems; j++) {
            if (strcmp(shops[i].items[j].name, itemName) == 0) {
                return i;
            }
        }
    }
    return -1;
}

//  找最不忙的外送員 id (呼叫前要 lock remain_time_mutex)
int findLeastBusyPerson() {
    int best = 0;
    int shortest = INT_MAX;
    for (int i = 0; i < NUM_DELIVERY_PERSONS; i++) {
        if (remain_time[i] < shortest) {
            shortest = remain_time[i];
            best = i;
        }
    }
    return best;
}

//  enqueue：把 Order* 放到第 personId 位外送員的 queue 末尾
void enqueue_order(int personId, Order *o) {
    DeliveryPerson *dp = &deliveryPersons[personId];
    pthread_mutex_lock(&dp->queue_mutex);
    if (dp->rear == -1) {
        dp->front = dp->rear = 0;
    } else {
        dp->rear = (dp->rear + 1) % MAX_QUEUE;
    }
    dp->queue[dp->rear] = o;
    o->status = ORDER_IN_QUEUE;
    pthread_cond_signal(&dp->queue_cond);
    pthread_mutex_unlock(&dp->queue_mutex);
}

//  dequeue：從第 personId 位外送員的 queue 拿最前面的一筆 Order*
//    呼叫前要先確定 queue 不空 (front != -1)
Order* dequeue_order(int personId) {
    DeliveryPerson *dp = &deliveryPersons[personId];
    if (dp->front == -1) return NULL;
    Order *o = dp->queue[dp->front];
    if (dp->front == dp->rear) {
        dp->front = dp->rear = -1;
    } else {
        dp->front = (dp->front + 1) % MAX_QUEUE;
    }
    return o;
}

//   外送員 thread：不斷等 queue 有訂單 → dequeue → 送餐 / 中途 Cancel
void* delivery_person_thread(void *arg) {
    int id = *(int*)arg;
    free(arg);

    while (1) {
        pthread_mutex_lock(&deliveryPersons[id].queue_mutex);
        while (deliveryPersons[id].front == -1) {
            pthread_cond_wait(&deliveryPersons[id].queue_cond, &deliveryPersons[id].queue_mutex);
        }
        Order *o = dequeue_order(id);
        pthread_mutex_unlock(&deliveryPersons[id].queue_mutex);
        if (!o) continue;

        pthread_mutex_lock(&o->lock);
        if (o->cancelled) {
            pthread_mutex_unlock(&o->lock);
            pthread_mutex_lock(&remain_time_mutex);
            remain_time[id] -= o->delivery_secs;
            pthread_mutex_unlock(&remain_time_mutex);
            free(o);
            continue;
        }
        o->status = ORDER_DELIVERING;
        pthread_mutex_unlock(&o->lock);

        int secs = o->delivery_secs;
        int i;
        for (i = 0; i < secs; i++) {
            sleep(1);
            pthread_mutex_lock(&o->lock);
            if (o->cancelled) {
                int remaining = secs - i;
                pthread_mutex_lock(&remain_time_mutex);
                remain_time[id] -= remaining;
                pthread_mutex_unlock(&remain_time_mutex);
                pthread_mutex_unlock(&o->lock);
                free(o);
                break;
            }
            pthread_mutex_unlock(&o->lock);
        }

        if (i == secs) {
            pthread_mutex_lock(&remain_time_mutex);
            remain_time[id] -= secs;
            pthread_mutex_unlock(&remain_time_mutex);

            char msg[BUFFER_SIZE];
            sprintf(msg, "Delivery has arrived and you need to pay %d$\n", o->price);
            send(o->client_socket, msg, strlen(msg), 0);
            close(o->client_socket);

            free(o);
        }
    }
    return NULL;
}

//   Client handler thread：解析 shop list / order / confirm / cancel
void* handleClient(void *arg) {
   int client_socket = *(int*)arg;
   free(arg);

   Shop shops[3];
   initShopList(shops);

   Order *currOrder = NULL;
   char buffer[BUFFER_SIZE];
   ssize_t nbytes;

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        nbytes = recv(client_socket, buffer, sizeof(buffer)-1, 0);
        if (nbytes <= 0) {
            if (currOrder) {
                pthread_mutex_lock(&currOrder->lock);
                currOrder->cancelled = 1;
                pthread_mutex_unlock(&currOrder->lock);
            }
            close(client_socket);
            return NULL;
        }
        buffer[nbytes] = '\0';

        // Debug：印出原始收到的字串（包含換行）
        printf("[DEBUG] Received raw from fd %d: \"%s\"\n", client_socket, buffer);
        fflush(stdout);

        // 去掉尾端換行
        if (buffer[strlen(buffer)-1] == '\n') {
            buffer[strlen(buffer)-1] = '\0';
        }

        // Debug：印出去掉換行後的字串
        printf("[DEBUG] Parsed buffer (newline removed): \"%s\"\n", buffer);
        fflush(stdout);

        // 如果去掉換行之後是空字串，表示剛收到的就是 "\n"
        if (strlen(buffer) == 0) {
            strcpy(buffer, "confirm");
            printf("[DEBUG] Interpreting blank line as \"confirm\" for fd %d\n", client_socket);
            fflush(stdout);
        }

        // ---------- shop list -----------
        if (strcmp(buffer, "shop list") == 0) {
            char resp[BUFFER_SIZE];
            getShopList(shops, resp);
            send(client_socket, resp, strlen(resp), 0);
            continue;
        }

        // ---------- order <itemName> <qty> -----------
        if (strncmp(buffer, "order", 5) == 0) {
            if (currOrder && currOrder->status != ORDER_CANCELED && currOrder->status != ORDER_DELIVERED) {
                if (currOrder->status != ORDER_PENDING) {
                    char *err = "Cannot place a new order. There is an existing order in progress.\n";
                    send(client_socket, err, strlen(err), 0);
                    continue;
                }
            }

            char itemName[50];
            int qty;
            int ret = sscanf(buffer, "order %s %d", itemName, &qty);
            if (ret != 2) {
                char *err = "Invalid order command. Usage: order <itemName> <quantity>\n";
                send(client_socket, err, strlen(err), 0);
                continue;
            }
            int sidx = findShopIndexByItem(shops, itemName);
            if (sidx < 0) {
                char *err = "Item not found in any shop.\n";
                send(client_socket, err, strlen(err), 0);
                continue;
            }

            if (!currOrder) {
                currOrder = malloc(sizeof(Order));
                memset(currOrder, 0, sizeof(Order));
                currOrder->client_socket = client_socket;
                currOrder->shopIndex = sidx;
                currOrder->delivery_secs = shops[sidx].distance;
                int item_price = 0;
                for (int i = 0; i < shops[sidx].numItems; i++) {
                    if (strcmp(shops[sidx].items[i].name, itemName) == 0) {
                        item_price = shops[sidx].items[i].price;
                        shops[sidx].items[i].total_quantity += qty;
                        break;
                    }
                }
                currOrder->price = item_price * qty;
                currOrder->status = ORDER_PENDING;
                currOrder->assignedPerson = -1;
                currOrder->remaining_secs = currOrder->delivery_secs;
                currOrder->cancelled = 0;
                pthread_mutex_init(&currOrder->lock, NULL);
            } else {
                int item_price = 0;
                for (int i = 0; i < shops[currOrder->shopIndex].numItems; i++) {
                    if (strcmp(shops[currOrder->shopIndex].items[i].name, itemName) == 0) {
                        item_price = shops[currOrder->shopIndex].items[i].price;
                        shops[currOrder->shopIndex].items[i].total_quantity += qty;
                        break;
                    }
                }
                currOrder->price += item_price * qty;
            }

            char resp[BUFFER_SIZE] = {0};
            int count = 0;
            for (int i = 0; i < shops[currOrder->shopIndex].numItems; i++) {
                if (shops[currOrder->shopIndex].items[i].total_quantity > 0) {
                    if (count > 0) strcat(resp, "|");
                    char qtyStr[20];
                    sprintf(qtyStr, "%d", shops[currOrder->shopIndex].items[i].total_quantity);
                    strcat(resp, shops[currOrder->shopIndex].items[i].name);
                    strcat(resp, " ");
                    strcat(resp, qtyStr);
                    count++;
                }
            }
            strcat(resp, "\n");
            send(client_socket, resp, strlen(resp), 0);
            continue;
        }

        // ---------- confirm -----------
        if (strcmp(buffer, "confirm") == 0) {
            if (!currOrder || currOrder->status != ORDER_PENDING) {
                char *err = "Please order some meals\n";
                send(client_socket, err, strlen(err), 0);
                continue;
            }
            pthread_mutex_lock(&remain_time_mutex);
            int best = findLeastBusyPerson();
            int predict = remain_time[best] + currOrder->delivery_secs;
            pthread_mutex_unlock(&remain_time_mutex);

            if (predict >= 30) {
                char *longMsg = "Your delivery will take a long time, do you want to wait?\n";
                send(client_socket, longMsg, strlen(longMsg), 0);

                memset(buffer, 0, sizeof(buffer));
                nbytes = recv(client_socket, buffer, sizeof(buffer)-1, 0);
                if (nbytes <= 0) {
                    pthread_mutex_lock(&currOrder->lock);
                    currOrder->cancelled = 1;
                    pthread_mutex_unlock(&currOrder->lock);
                    close(client_socket);
                    return NULL;
                }
                buffer[nbytes] = '\0';
                if (buffer[strlen(buffer)-1] == '\n')
                    buffer[strlen(buffer)-1] = '\0';

                if (strcmp(buffer, "No") == 0) {
                    pthread_mutex_lock(&currOrder->lock);
                    currOrder->cancelled = 1;
                    pthread_mutex_unlock(&currOrder->lock);

                    char *bye = "Order canceled. Connection will be closed.\n";
                    send(client_socket, bye, strlen(bye), 0);
                    close(client_socket);
                    return NULL;
                }
            }

            currOrder->assignedPerson = findLeastBusyPerson();
            int pid = currOrder->assignedPerson;

            pthread_mutex_lock(&remain_time_mutex);
            remain_time[pid] += currOrder->delivery_secs;
            pthread_mutex_unlock(&remain_time_mutex);

            enqueue_order(pid, currOrder);
            currOrder->status = ORDER_IN_QUEUE;

            char *waitMsg = "Please wait a few minutes...\n";
            send(client_socket, waitMsg, strlen(waitMsg), 0);
            continue;
        }

        // ---------- cancel -----------
        if (strcmp(buffer, "cancel") == 0) {
            if (!currOrder) {
                char *err = "No active order to cancel.\n";
                send(client_socket, err, strlen(err), 0);
                continue;
            }
            pthread_mutex_lock(&currOrder->lock);
            if (currOrder->status == ORDER_DELIVERED || currOrder->status == ORDER_CANCELED) {
                pthread_mutex_unlock(&currOrder->lock);
                char *ignore = "Cannot cancel; order already completed or canceled.\n";
                send(client_socket, ignore, strlen(ignore), 0);
                continue;
            }
            currOrder->cancelled = 1;
            pthread_mutex_unlock(&currOrder->lock);

            char *resp = "Order canceled. Connection will be closed.\n";
            send(client_socket, resp, strlen(resp), 0);
            close(client_socket);
            return NULL;
        }

        // ---------- Unknown command -----------
        // 只要不是 shop list/order/confirm/cancel，就 silent 跳過
        continue;
    }

    return NULL;
    }

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < NUM_DELIVERY_PERSONS; i++) {
        remain_time[i] = 0;
    }

    for (int i = 0; i < NUM_DELIVERY_PERSONS; i++) {
        deliveryPersons[i].id = i;
        deliveryPersons[i].front = deliveryPersons[i].rear = -1;
        pthread_mutex_init(&deliveryPersons[i].queue_mutex, NULL);
        pthread_cond_init(&deliveryPersons[i].queue_cond, NULL);
    }

    // 啟動 2 個外送員 thread
    for (int i = 0; i < NUM_DELIVERY_PERSONS; i++) {
        pthread_t tid;
        int *arg = malloc(sizeof(int));
        *arg = i;
        pthread_create(&tid, NULL, delivery_person_thread, arg);
        pthread_detach(tid);
    }

    // 建立 server socket
    int port = atoi(argv[1]);
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }
    // 可以使用相同的port
    int yes = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("Error setting socket option");
        exit(EXIT_FAILURE);
}

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    if (listen(server_socket, MAX_CLIENTS) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    printf("Server is listening on port %d...\n", port);

    // 不斷 accept 新 client
    while (1) {
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);
        int client_socket = accept(server_socket, (struct sockaddr*)&cliaddr, &clilen);
        if (client_socket < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        pthread_t client_thread;
        int *pfd = malloc(sizeof(int));
        *pfd = client_socket;
        pthread_create(&client_thread, NULL, handleClient, pfd);
        pthread_detach(client_thread);
    }

    close(server_socket);
    return 0;
}
 
 
 