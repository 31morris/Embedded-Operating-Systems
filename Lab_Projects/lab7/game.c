#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/shm.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

typedef struct {
    int guess;
    char result[8];
}data;

data*shm;
int shmid,answer;

void clean_shm(int signum){
    shmdt(shm);
    shmctl(shmid, IPC_RMID, NULL);
    exit(0);
}

void handler(int signum){
    if(shm->guess == answer){
        strcpy(shm->result, "bingo");
        printf("Guess %d, Bingo\n", shm->guess);
        // clean_shm(0);

    } else if(shm->guess < answer){
        strcpy(shm->result, "bigger");
        printf("Guess %d, Bigger\n", shm->guess);
    } else {
        strcpy(shm->result, "smaller");
        printf("Guess %d, Smaller\n", shm->guess);
    }
}

int main(int argc, char *argv[]){
    signal(SIGINT,clean_shm);
    if(argc!= 3){
        printf("Usage: %s <key> <guess>\n", argv[0]);
        return 1;
    }
    key_t key = atoi(argv[1]);
    answer = atoi(argv[2]);
    shmid = shmget(key, sizeof(data), 0666 | IPC_CREAT);
    if(shmid < 0){
        perror("shmget");
        return 1;
    }
    shm = shmat(shmid, NULL, 0);
    if(shm == (void *)-1){
        perror("shmat");
        return 1;
    }
    printf("GAME PID: %d\n", getpid());
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigaction(SIGUSR1, &sa, NULL);

    while(1){
        pause(); // Wait for signal
    }
    return 0;

}
