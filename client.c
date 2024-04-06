#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

/*  Global constant: string buffer size */
#define BUFFER_SIZE 1024

/* Global constant: file types */
#define DIRECTORY 0  // Directory
#define TEXT 1       // Text (non-binary) file
#define BINARY 2     // Binary file
#define ERROR 3      // Error message

/* Linked list struct containing information of an indexed directory/file */
typedef struct item {
    char path[BUFFER_SIZE];  // Pathname of the file
    int item_type;           // Type of the file (directory, text, binary or error)
    struct item *next;       // Linked list: pointer to the next item
} item;

/* Helper functions */
ssize_t gopher_connect(ssize_t (*func)(char *), char *path);
ssize_t indexing(char *request);
ssize_t evaluate_file_size(char *request);
ssize_t print_response(char *request);
void add_item(item *new_item);
void evaluate(void);
void cleanup(void);

/* Global variables: values used across all functions */
int fd;                          // Socket file descriptor
char *address;                  // IP address of the Gopher server
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
        fprintf(stderr, "Usage: %s <server address> <port>\n", argv[0]);
        exit(0);
    }
    address = argv[1];
    port = atoi(argv[2]);
    
    // Begin the indexing process, starting with the root directory
    gopher_connect(indexing, "");

    // Iterate through the linked list of indexed items to index subdirectories
    item *c = list;
    while (c != NULL) {
        if (c->item_type == DIRECTORY)
            gopher_connect(indexing, c->path);
        c = c->next;
    }

    // Analyse the information of the indexed items and print info
    evaluate();

    // Clean up before returning the function
    cleanup();

    return 0;
}

/**
 * Establish a connection with the Gopher server.
 * 
 * @param address IP address of the Gopher server
 * @param port port of the Gopher server
 * @param func function handling response from the Gopher server
 * @param request request to be send to the Gopher server
 * @return the output from the function handling response
 */
ssize_t gopher_connect(ssize_t (*func)(char *), char *path) {
    // Timestamping
    struct timeval tv;

    // Create the socket
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "Error: Socket creation failed\n");
        exit(1);
    }
    
    // Specify the IP address and the port for connection
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, address, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Error: Invalid address %s\n", address);
        exit(1);
    }

    // Initiate the connection
    int connect_status = connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (connect_status == -1) {
        fprintf(stderr, "Error: Connection failed\n");
        exit(1);
    }

    // Append "\r\n" to the end of the path to form a request line
    size_t path_length = strlen(path);
    char request[path_length + 3];
    strncpy(request, path, path_length + 2);
    request[path_length] = '\r';
    request[path_length + 1] = '\n';
    request[path_length + 2] = '\0';

    // Send a request for the directory index to the server
    send(fd, request, path_length + 2, 0);
    gettimeofday(&tv, NULL);
    struct tm *timeinfo = localtime(&tv.tv_sec);
    char timeString[BUFFER_SIZE];
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", timeinfo);
    fprintf(stdout, "Request sent at %s: %s", timeString, request);

    int output = (*func)(request);

    // Terminate the connection and release the socket
    close(fd);

    return output;
}

/**
 * Upon sending a request for a directory index, this function reads the
 * response from the Gopher server and adds the indexed directories and files
 * to the linked list.
 * 
 * Each line in the directory index has four columns of information, separated
 * by tabs. The first column indicates the type of the directory/file; the
 * second column indicates the pathname.
 */
ssize_t indexing(char *request) {
    // Buffer for strings read from the server
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received = recv(fd, buffer, BUFFER_SIZE, 0);

    // Handle the lack of response from the server
    if (bytes_received == 0) {
        fprintf(stdout, "No response from the server\n");
        return 0;
    }

    // Read the directory index from the server
    do {
        buffer[bytes_received] = '\0';
        char *line = strtok(buffer, "\t\r\n");
        // Each line in the response contains four columns, separated by tab
        int column = 0;
        int item_type = ERROR;
        while (line != NULL) {
            if (column == 0) {
                // Determine the type of the indexed item
                if (line[0] == '1') item_type = DIRECTORY;
                else if (line[0] == '0') item_type = TEXT;
                else if (line[0] == '3') {
                    // Add the error to the linked list, recording the request
                    item_type = ERROR;
                    item *new_item = (item *)malloc(sizeof(item));
                    strncpy(new_item->path, request, BUFFER_SIZE - 1);
                    new_item->path[strlen(request)] = '\0';
                    new_item->item_type = item_type;
                    new_item->next = NULL;
                    add_item(new_item);
                }
                else if (line[0] != 'i') item_type = BINARY;
            }
            else if (column == 1 && item_type != ERROR) {
                // Index the directory/file
                fprintf(stdout, "Indexed: %s\n", line);
                item *new_item = (item *)malloc(sizeof(item));
                strncpy(new_item->path, line, BUFFER_SIZE - 1);
                new_item->path[strlen(line)] = '\0';
                new_item->item_type = item_type;
                new_item->next = NULL;
                add_item(new_item);
            }
            // Read the next column in the line or the next line
            line = strtok(NULL, "\t\r\n");
            column = (column + 1) % 4;
        }
    } while ((bytes_received = recv(fd, buffer, BUFFER_SIZE, 0)) > 0);
    
    return 0;
}

/**
 * Upon send a request to the Gopher server for a file, evaluate the file size.
 * 
 * @param request not used
 * @return size of the requested file
 */
ssize_t evaluate_file_size(char *request) {
    // Unused parameter
    (void)request;

    // Buffer for strings read from the server
    char buffer[BUFFER_SIZE];
    ssize_t size = 0;
    ssize_t bytes_received = recv(fd, buffer, BUFFER_SIZE, 0);

    if (bytes_received == 0) {
        fprintf(stdout, "No response from the server\n");
        return size;
    }

    // Read the directory index from the server
    do {
        size += bytes_received;
    } while ((bytes_received = recv(fd, buffer, BUFFER_SIZE, 0)) > 0);
    
    return size;
}

/**
 * Upon sending a request to the Gopher server for the smallest text file,
 * print the entire content to the terminal.
 */
ssize_t print_response(char *request) {
    // Unused parameter
    (void)request;

    // Buffer for strings read from the server
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received = recv(fd, buffer, BUFFER_SIZE, 0);

    if (bytes_received == 0) {
        fprintf(stdout, "No response from the server\n");
        return 0;
    }

    fprintf(stdout, "Content of the smallest text file:\n");
    // Read the directory index from the server
    do {
        buffer[bytes_received] = '\0';
        fprintf(stdout, "%s", buffer);
    } while ((bytes_received = recv(fd, buffer, BUFFER_SIZE, 0)) > 0);
    
    return 0;
}

/**
 * Free the heap memory occupied by the linked list of indexed items before
 * the main() function returns.
 */
void cleanup(void) {
    item *c = list;
    while (c != NULL) {
        item *next = c->next;
        free(c);
        c = next;
    }
}

/**
 * Add a new item to the linked list of indexed items.
 * Requires O(1) time for adding directories, text files and binary files.
 * Requires O(n) time for adding an invalid request (to ensure uniqueness).
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

    // If the invalid reference is already requested previously, do not add
    if (new_item->item_type == ERROR) {
        item *c = list;
        while (c != NULL) {
            if (c->item_type == ERROR && strcmp(c->path, new_item->path) == 0) {
                free(new_item);
                return;
            }
            c = c->next;
        }
    }

    // Otherwise, append the new item to the end of the linked list
    last_node->next = new_item;
    last_node = new_item;
}

/**
 * Evaluate and print out:
 *     1. Number of directories, text files, binaries and invalid references
 *     2. Sizes of the smallest/largest text/binary files
 *     3. Content of the smallest text file
 */
void evaluate(void) {
    int num_of_directories = 0;
    int num_of_text_files = 0;
    int num_of_binary_files = 0;
    int num_of_invalid_references = 0;
    char *smallest_text_file = NULL;
    int size_of_smallest_text_file = -1;
    int size_of_largest_text_file = -1;
    int size_of_smallest_binary_file = -1;
    int size_of_largest_binary_file = -1;

    item *c = list;
    ssize_t file_size;
    while (c != NULL) {
        switch (c->item_type) {
            case DIRECTORY:
                num_of_directories++;
                break;
            case TEXT:
                num_of_text_files++;

                // Evaluate the size of the file
                file_size = gopher_connect(evaluate_file_size, c->path);
                if (size_of_smallest_text_file == -1 || file_size < size_of_smallest_text_file) {
                    size_of_smallest_text_file = file_size;
                    smallest_text_file = c->path;
                }
                if (size_of_largest_text_file == -1 || file_size > size_of_largest_binary_file) {
                    size_of_largest_text_file = file_size;
                }

                break;
            case BINARY:
                num_of_binary_files++;

                // Evaluate the size of the file
                file_size = gopher_connect(evaluate_file_size, c->path);
                if (size_of_smallest_binary_file == -1 || file_size < size_of_smallest_binary_file) {
                    size_of_smallest_binary_file = file_size;
                }
                if (size_of_largest_binary_file == -1 || file_size > size_of_largest_binary_file) {
                    size_of_largest_binary_file = file_size;
                }

                break;
            case ERROR:
                num_of_invalid_references++;
                break;
        }
        c = c->next;
    }

    fprintf(stdout, "Number of directories: %d\n", num_of_directories);
    fprintf(stdout, "Number of text files: %d\n", num_of_text_files);
    fprintf(stdout, "Number of binary files: %d\n", num_of_binary_files);
    fprintf(stdout, "Number of invalid references: %d\n", num_of_invalid_references);
    gopher_connect(print_response, smallest_text_file);
    fprintf(stdout, "Size of the smallest text file: %d\n", size_of_smallest_text_file);
    fprintf(stdout, "Size of the largest text file: %d\n", size_of_largest_text_file);
    fprintf(stdout, "Size of the smallest binary file: %d\n", size_of_smallest_binary_file);
    fprintf(stdout, "Size of the largest binary file: %d\n", size_of_largest_binary_file);
}
