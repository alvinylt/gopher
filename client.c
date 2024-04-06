#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

/*  Global constant: string buffer size */
#define BUFFER_SIZE 1024

/* Global constant: file types */
#define DIRECTORY 0  // Directory
#define TEXT 1       // Text (non-binary) file
#define BINARY 2     // Binary file

/* Struct containing information of an indexed directory/file */
typedef struct item {
    char path[BUFFER_SIZE];
    int item_type;
    int item_size;
    struct item *next;
} item;

/* Helper functions */
void gopher_connect(char *hostname, int port, char *request);
void index(char *path);
void add_item(item *new_item);

/* Global variables: values used across all functions */
int fd;                          // Socket file descriptor
struct sockaddr_in server_addr;  // Address and port information
struct item *list = NULL;        // Linked list of indexed directories and files
struct item *last_node = NULL;   // Last item in the linked list

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
    
    // Begin the indexing process, starting with the root directory
    gopher_connect(hostname, port, "\r\n");

    // Go through the linked list of indexed items and index subdirectories
    item *c = list;
    while (c != NULL) {
        if (c->item_type == DIRECTORY) {
            int path_len = strlen(c->path);
            char *request = malloc(path_len + 3);
            strcpy(request, c->path);
            request[path_len] = '\r';
            request[path_len + 1] = '\n';
            request[path_len + 2] = '\0';
            gopher_connect(hostname, port, request);
        }
        c = c->next;
    }

    // Analyse the information of the indexed items and print info

    return 0;
}

void gopher_connect(char *hostname, int port, char *request) {
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

    // Initiate the connection
    int connect_status = connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (connect_status == -1) {
        fprintf(stderr, "Error: Connection failed\n");
        exit(1);
    }

    index(request);

    // Terminate the connection and release the socket
    close(fd);
}

void index(char *request) {
    // Buffer for strings read from the server
    char *buffer = malloc(BUFFER_SIZE);
    int bytes_received;

    // Send a request for the directory index to the server
    int bytes = send(fd, request, strlen(request), 0);
    fprintf(stdout, "Sending request: %s", bytes == 2 ? "Root directory\n" : request);

    // Read the directory index from the server
    while ((bytes_received = recv(fd, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_received] = '\0';
        char *line = strtok(buffer, "\t\r\n");
        // Each line in the response contains four columns, separated by tab
        int column = 0;
        int item_type;
        while (line != NULL) {
            if (column == 0) {
                // Determine the type of the indexed item
                if (line[0] == '1') item_type = DIRECTORY;
                else if (line[0] == '0') item_type = TEXT;
                else item_type = BINARY;
            }
            if (column == 1) {
                fprintf(stdout, "Indexed: %s\n", line);
                item *new_item = (item *)malloc(sizeof(item));
                strcpy(new_item->path, line);
                new_item->item_type = item_type;
                new_item->next = NULL;
                add_item(new_item);
            }
            // Read the next column in the line or the next line
            line = strtok(NULL, "\t\r\n");
            column = (column + 1) % 4;
        }
    }
    
    free(buffer);
}

/**
 * Add a new item to the linked list of indexed items.
 * Requires O(1) time.
 * 
 * @param new_item pointer to the new indexed item
 */
void add_item(item *new_item) {
    // If the linked list is empty, let the new item be the initial item
    if (list == NULL) {
        list = new_item;
        last_node = new_item;
        return;
    }

    // Otherwise, append the new item to the end of the linked list
    last_node->next = new_item;
    last_node = new_item;
}
