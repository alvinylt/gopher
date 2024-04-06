#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// Global constant: string buffer size
#define BUFFER_SIZE 1024

// Helper functions
void gopher_connect(char *request);
void index(char *path);

// Global variables: values used across all functions
int fd;                          // Socket file descriptor
struct sockaddr_in server_addr;  // Address and port information

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
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "Error: Socket creation failed\n");
        exit(1);
    }
    
    // Specify the IP address and the port for connection
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, hostname, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Error: Invalid address %s\n", hostname);
        exit(1);
    }

    // Interact with the server
    gopher_connect("\r\n");

    return 0;
}

void gopher_connect(char *request) {
    // Initiate the connection
    int connect_status = connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (connect_status == -1) {
        fprintf(stderr, "Error: Connection failed\n");
        exit(1);
    }
    printf("Connected to server!\n");

    index(request);

    // Terminate the connection and release the socket
    close(fd);
}

void index(char *request) {
    // Buffer for strings read from the server
    char *buffer = malloc(BUFFER_SIZE);
    int bytes_received;

    // Send a request for the directory index to the server
    send(fd, request, strlen(request), 0);
    fprintf(stdout, "Sending request: %s", "\r\n");

    // Read the directory index from the server
    while ((bytes_received = recv(fd, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_received] = '\0';
        char *line = strtok(buffer, "\t\r\n");
        int item = 0;
        while (line != NULL) {
            if (item == 0) {
                // Index subdirectory recursively
                if (line[0] == '1') {
                    fprintf(stdout, "Directory found: ");
                }
            }
            if (item == 1)
                fprintf(stdout, "%s\n", line);
            line = strtok(NULL, "\t\r\n");
            item = (item + 1) % 4;
        }
    }
    
    free(buffer);
}
