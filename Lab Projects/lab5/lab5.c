#include <errno.h>      
#include <stdio.h>      
#include <stdlib.h>     
#include <sys/types.h>  
#include <sys/wait.h>   
#include <unistd.h>     
#include <fcntl.h>      
#include <arpa/inet.h>
#include <signal.h>
#include <string.h>

int server_socket, client_socket;

// Handle SIGCHLD signal (child process terminated) to prevent zombie processes
void stop_child(int signum) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

// Handle SIGINT signal (Ctrl+C) to shut down the server
void stop_parent(int signum) {
    signal(SIGINT, SIG_DFL);  
    close(server_socket);     
    printf("Server closed\n");
    exit(signum);            
}

void handle_client(int client_socket) {

    pid_t pid = fork();
    
    if (pid < 0) {
        perror("Error in fork");
        exit(EXIT_FAILURE);

    }else if (pid == 0) {
        // Child process
        pid_t child_pid = getpid();
        printf("Train ID: %d\n", child_pid);

        if (dup2(client_socket, STDOUT_FILENO) == -1) {
            perror("Error in dup2");
            exit(EXIT_FAILURE);
        }
        close(client_socket);
        execlp("sl", "sl", "-l", NULL);
        perror("Error executing sl");
        exit(EXIT_FAILURE);
    } else {
        // Parent process
        close(client_socket);
    }
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    int port = atoi(argv[1]); 

    // Set signal handlers
    signal(SIGCHLD, stop_child);
    signal(SIGINT, stop_parent);

    // Create TCP socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Allow address reuse to avoid "address already in use" error
    int yes = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("Error setting socket option");
        exit(EXIT_FAILURE);
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);  
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind socket to the specified IP/Port
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Error binding socket");
        exit(EXIT_FAILURE);
    }

    // Start listening for incoming connections
    if (listen(server_socket, SOMAXCONN) == -1) {
        perror("Error listening");
        exit(EXIT_FAILURE);
    }
    
    while (1) {
        if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len)) == -1) {
            perror("Error accepting connection");
            continue;
        }
        handle_client(client_socket);
    }

    return 0;
}
