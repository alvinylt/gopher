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
void gopher_connect(void (*func)(void), char *request);
void index(void);
void add_item(item *new_item);
void evaluate(void);

/* Global variables: values used across all functions */
int fd;                          // Socket file descriptor
char *hostname;                  // IP address of the Gopher server
int port;                        // Port of the Gopher server
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
    hostname = argv[1];
    port = atoi(argv[2]);
    
    // Begin the indexing process, starting with the root directory
    gopher_connect(index, "\r\n");

    // Go through the linked list of indexed items and index subdirectories
    item *c = list;
    while (c != NULL) {
        if (c->item_type == DIRECTORY) {
            int path_len = strlen(c->path);
            char *request = malloc(path_len + 3);
            strcpy(request, c->path);
            strcpy(request + path_len, "\r\n");
            gopher_connect(index, request);
        }
        c = c->next;
    }

    // Analyse the information of the indexed items and print info
    evaluate();

    return 0;
}

/**
 * Establish a connection with the Gopher server.
 * 
 * @param hostname IP address of the Gopher server
 * @param port port of the Gopher server
 * @param func function handling response from the Gopher server
 * @param request request to be send to the Gopher server
 */
void gopher_connect(void (*func)(void), char *request) {
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

    // Send a request for the directory index to the server
    int bytes = send(fd, request, strlen(request), 0);
    fprintf(stdout, "Sending request: %s", bytes == 2 ? "Root directory\n" : request);

    (*func)();

    // Terminate the connection and release the socket
    close(fd);
}

void index(void) {
    // Buffer for strings read from the server
    char *buffer = malloc(BUFFER_SIZE);
    int bytes_received;

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

/**
 * Evaluate and print out the number of directories, text files and binaries.
 * The contents of the smallest text file.
 * The size of the largest text file.
 * The size of the smallest and the largest binary files.
 * The size of the smallest and the largest binary files.
 * The number of unique invalid references (those with an “error” type)
 */
void evaluate(void) {
    int num_of_directories = 0;
    int num_of_text_files = 0;
    int num_of_binary_files = 0;
    char *smallest_text_file = NULL;
    int size_of_largest_text_file = -1;
    int size_of_smallest_binary_file = -1;
    int size_of_largest_binary_file = -1;

    item *c = list;
    while (c != NULL) {
        switch (c->item_type) {
        case DIRECTORY:
            num_of_directories++;
            break;
        case TEXT:
            num_of_text_files++;
            break;
        default:
            num_of_binary_files++;
            break;
        }

        c = c->next;
    }

    fprintf(stdout, "Number of directories: %d\n", num_of_directories);
    fprintf(stdout, "Number of text files: %d\n", num_of_text_files);
    fprintf(stdout, "Number of binary files: %d\n", num_of_binary_files);
}
