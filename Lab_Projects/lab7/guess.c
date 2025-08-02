#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/shm.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

typedef struct {
    int guess;
    char result[8];
} data;

int upper_bound;
int pid;
int shmid;
int lower_bound = 0;
data *shm;

void handler(int signum){
    if(lower_bound > upper_bound){
        printf("Error in guessing range.\n");
        exit(0);
    }
    shm->guess = (lower_bound + upper_bound) / 2;
    printf("Guess: %d\n", shm->guess);
    if(kill(pid, SIGUSR1) < 0){
        perror("kill");
        exit(1);
    }
    usleep(1000); // Wait for response
    if(strcmp(shm->result, "bigger") == 0){
        lower_bound = shm->guess + 1;
    }else if(strcmp(shm->result, "smaller") == 0){
        upper_bound = shm->guess - 1;
    }else if(strcmp(shm->result, "bingo") == 0){       
        exit(0);

    }
}

int main(int argc, char *argv[]){
    if(argc != 4){
        printf("Usage: %s <key> <upper_bound> <pid>\n", argv[0]);
        return 1;
    }


    key_t key  = atoi(argv[1]);
    upper_bound = atoi(argv[2]);
    pid = atoi(argv[3]);

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

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigaction(SIGALRM, &sa, NULL);

    struct itimerval timer;
    timer.it_value.tv_sec = 1;
    timer.it_value.tv_usec = 0; 
    timer.it_interval.tv_sec = 1;
    timer.it_interval.tv_usec = 0; 

    setitimer(ITIMER_REAL, &timer, NULL);

    while(1){
        pause();
    }
    return 0;
}