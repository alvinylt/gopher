#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// Global variables
#define HOSTNAME "127.0.0.1"
#define PORT 70

int main(int argc, char* argv[]) {
    int client_socket;
    struct sockaddr_in server_addr;
    
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("Socket creation failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, HOSTNAME, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        exit(1);
    }

    int connect_status = connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    
    if (connect_status == -1) {
        perror("Connection failed");
        exit(1);
    }

    printf("Connected to server!\n");

    close(client_socket);
    return 0;
}
