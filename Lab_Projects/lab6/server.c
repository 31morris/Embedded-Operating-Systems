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

int server_socket;
int balance = 0;
int sem_id;


void sigint_handler(int signum) {
    signal(SIGINT, SIG_DFL);
    close(server_socket);
    semctl(sem_id, 0, IPC_RMID,0);
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
		
void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    char buffer[256];
    // Receive the request from the client
    int received_bytes = recv(client_socket, buffer, sizeof(buffer), 0);
    buffer[received_bytes] = '\0';

    int amount,times;
    char action[10];
    sscanf(buffer, "%s %d %d", action, &amount, &times);

    for(int i = 0; i < times; i++) {
        P(sem_id);
        if(strcmp(action, "deposit") == 0) {
            balance += amount;
        } else if (strcmp(action, "withdraw") == 0) {
            balance -= amount;
        }
        V(sem_id);
        printf("After %s: %d\n", action, balance);
        usleep(10);
    }
    close(client_socket);
    free(arg);
    
}

int start_server(int port){

    // Create a socket
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    int yes = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("Error setting socket option");
        exit(EXIT_FAILURE);
    }
    
	memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Error binding socket");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, SOMAXCONN) == -1) {
        perror("Error listening");
        exit(EXIT_FAILURE);
    }

    //semaphore
    int key = 1234;
    if((sem_id = semget(key, 1, IPC_CREAT | 0666)) == -1) {
        perror("Error creating semaphore");
        exit(EXIT_FAILURE);
    }else {
        if (semctl(sem_id, 0, SETVAL, 1) == -1) {
            perror("Error initializing semaphore");
            exit(EXIT_FAILURE);
        }
    }
    

    while (1) {
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_socket == -1) {
            perror("Error accepting connection");
            continue;
        }

        pthread_t client_thread;
        int *client_socket_ptr = malloc(sizeof(int));
        *client_socket_ptr = client_socket;
        if (pthread_create(&client_thread, NULL, handle_client, (void*)client_socket_ptr)!= 0) {
            perror("Error creating thread");
            close(client_socket);
            free(client_socket_ptr);
            continue;
    }

    pthread_detach(client_thread);
    }
    return 0;   
    
}

    
int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, sigint_handler);
    int port = atoi(argv[1]);
    start_server(port);
    return 0;
}



