#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
void send_request(const char* ip, int port, const char* action, int amount, int times) {


    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;                
    server_addr.sin_port = htons(port);              
    server_addr.sin_addr.s_addr = inet_addr(ip); 


    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Error connecting to server");
        close(client_socket);
        exit(EXIT_FAILURE);
    }
    
    // Send the request to the server
    char request[50];
    snprintf(request, sizeof(request), "%s %d %d", action, amount, times);
    send(client_socket, request, strlen(request), 0);
    close(client_socket);
}

int main(int argc, char* argv[]) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <ip> <port> <deposit/withdraw> <amount> <times>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    const char* ip = argv[1];
    int port = atoi(argv[2]);
    const char* action = argv[3];
    int amount = atoi(argv[4]);
    int times = atoi(argv[5]);
    send_request(ip, port, action, amount, times);
  
  
    return 0;
}

