#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// Global variables
#define BUFFER_SIZE 1024

void interact(int fd, struct sockaddr_in server_addr, int port);
void index(char *path);

/**
 * The Internet Gopher client indexing files.
 */
int main(int argc, char* argv[]) {
    // Parse the command input
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <hostname> <port>\n", argv[0]);
        exit(0);
    }
    char *hostname = argv[1];
    int port = atoi(argv[2]);
    
    // Create the socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "Error: Socket creation failed\n");
        exit(1);
    }
    
    // Specify the IP address and the port for connection
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, hostname, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Error: Invalid address %s\n", hostname);
        exit(1);
    }

    // Interact with the server
    interact(fd, server_addr, port);

    return 0;
}

void interact(int fd, struct sockaddr_in server_addr, int port) {
    // Initiate the connection
    int connect_status = connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (connect_status == -1) {
        fprintf(stderr, "Error: Connection to port %d failed\n", port);
        exit(1);
    }
    printf("Connected to server!\n");

    // Buffer for strings read from the server
    char *buffer = malloc(BUFFER_SIZE);
    int bytes_received;

    // Send a request for the directory index to the server
    send(fd, "\r\n", 2, 0);
    fprintf(stdout, "Sending request: %s", "\r\n");

    // Read the directory index from the server
    while ((bytes_received = recv(fd, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_received] = '\0';
        char *line = strtok(buffer, "\t\r\n");
        int index = 0;
        while (line != NULL) {
            if (index == 0) {
                // Index subdirectory recursively
                if (line[0] == '1') {
                    fprintf(stdout, "Directory found: ");
                }
            }
            if (index == 1)
                fprintf(stdout, "%s\n", line);
            line = strtok(NULL, "\t\r\n");
            index = (index + 1) % 4;
        }
    }

    free(buffer);
    // Terminate the connection and release the socket
    close(fd);
}

void index(char *path) {

}
