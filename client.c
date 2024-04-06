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
void gopher_connect(char *request);
void index(char *path);
void add_item(item *new_item);

/* Global variables: values used across all functions */
int fd;                          // Socket file descriptor
struct sockaddr_in server_addr;  // Address and port information
struct item *list = NULL;        // Linked list of indexed directories and files

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

    // Begin the indexing process, starting with the root directory
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

    item *c = list;
    while (c != NULL) {
        fprintf(stdout, "Visit: %s of type %d\n", c->path, c->item_type);
        c = c->next;
    }
}

void index(char *request) {
    // Buffer for strings read from the server
    char *buffer = malloc(BUFFER_SIZE);
    int bytes_received;

    // Send a request for the directory index to the server
    send(fd, request, strlen(request), 0);
    fprintf(stdout, "Sending request: %s", request);

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
 * 
 * @param new_item pointer to the new indexed item
 */
void add_item(item *new_item) {
    if (list == NULL) {
        list = new_item;
        return;
    }
    
    item *current_head = list;
    new_item->next = current_head;
    list = new_item;
}
