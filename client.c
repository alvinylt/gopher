#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// Global variables
#define HOSTNAME "127.0.0.1"
#define PORT 70
#define BUFFER_SIZE 1024

void interact(int fd);

int main(int argc, char* argv[]) {
    int fd;
    struct sockaddr_in server_addr;
    
    // Create the socket
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "Error: Socket creation failed\n");
        exit(1);
    }

    // Specify the IP address and the port for connection
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, HOSTNAME, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Error: Invalid address %s\n", HOSTNAME);
        exit(1);
    }

    // Initiate the connection
    int connect_status = connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (connect_status == -1) {
        fprintf(stderr, "Error: Connection to port %d failed\n", PORT);
        exit(1);
    }

    // Interact with the server
    interact(fd);

    // Terminate the connection and release the socket
    close(fd);
    return 0;
}

void interact(int fd) {
    printf("Connected to server!\n");

    // Buffer for strings read from the server
    char *buffer = malloc(BUFFER_SIZE);
    int bytes_received;

    // Send a request for the directory index to the server
    send(fd, "\r\n", 2, 0);

    // Read the directory index from the server
    while ((bytes_received = recv(fd, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_received] = '\0';
        char *line = strtok(buffer, "\r\n");
        while (line != NULL) {
            printf("%s\n", line);
            line = strtok(NULL, "\r\n");
        }
    }

    free(buffer);
}
