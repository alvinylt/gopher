#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

/* Global constant: buffer size for receiving content from the server */
#define BUFFER_SIZE 4096

/* Global constants: file types */
#define DIRECTORY 0  // Directory
#define TEXT 1       // Text (non-binary) file
#define BINARY 2     // Binary file
#define ERROR 3      // Error message
#define EXTERNAL 4   // External server

/* Linked list entry containing information of an indexed item */
typedef struct entry {
    char *path;          // Pathname, error message or external server
    int item_type;       // Type of the item (directory, file, error, etc.)
    struct entry *next;  // Linked list pointer to the next item
} entry;

/* Helper functions */
ssize_t gopher_connect(ssize_t (*func)(char *), char *path);
ssize_t indexing(char *request);
void index_line(char *line, char *request);
entry *create_new_entry(int item_type, char *path);
bool is_binary_file(char type);
char *find_next_line(char *ptr);
char *extract_pathname(char *line);
ssize_t evaluate_file_size(char *request);
ssize_t print_response(char *request);
void add_item(entry *new_item);
void evaluate(void);
void cleanup(void);

/* Global variables: values used across all functions */
int fd;                          // Socket file descriptor
struct hostent *server;          // Hostname or IP address of the Gopher server
int port;                        // Port of the Gopher server
struct sockaddr_in server_addr;  // Address and port information
struct entry *list = NULL;       // Linked list of indexed directories and files
struct entry *last_node = NULL;  // Last item in the linked list

/**
 * The Internet Gopher client indexing files.
 */
int main(int argc, char* argv[]) {
    // Parse the command input
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <hostname> <port>\n", argv[0]);
        exit(EXIT_SUCCESS);
    }
    
    server = gethostbyname(argv[1]);
    if (server == NULL) {
        fprintf(stderr, "Error: unable to connect to host %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }
    port = atoi(argv[2]);
    
    // Begin the indexing process, starting with the root directory
    gopher_connect(indexing, "");

    // Iterate through the linked list of indexed items to index subdirectories
    entry *c = list;
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
 * @param address Hostname or IP address of the Gopher server
 * @param port port of the Gopher server
 * @param func function handling response from the Gopher server
 * @param request request to be send to the Gopher server
 * @return the output from the function handling response
 */
ssize_t gopher_connect(ssize_t (*func)(char *), char *path) {
    // Timestamping for logging sent requests
    struct timeval tv;

    // Create the socket
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "Error: Socket creation failed\n");
        exit(EXIT_FAILURE);
    }

    // Configure timeout limit
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        fprintf(stderr, "Error: Timeout configuration failed\n");
        exit(EXIT_FAILURE);
    }
    
    // Specify the IP address and the port for connection
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
    
    // Initiate the connection
    int connect_status = connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (connect_status == -1) {
        fprintf(stderr, "Error: Connection failed\n");
        exit(EXIT_FAILURE);
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
    char time[32];
    strftime(time, sizeof(time), "%Y-%m-%d %H:%M:%S", timeinfo);
    fprintf(stdout, "Request sent at %s: %s", time, request);

    // Execute the function that receives and handles server's response
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
 * 
 * @param request the request line sent to the server
 */
ssize_t indexing(char *request) {
    // Buffer for strings read from the server
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received = recv(fd, buffer, BUFFER_SIZE, 0);

    if (bytes_received == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            fprintf(stderr, "Error: Server response timeout\n");
        else
            fprintf(stderr, "Error: Unable to receive server response\n");
    }

    // Handle an empty string response from the server
    if (bytes_received == 0) {
        fprintf(stdout, "Empty response from the server\n");
        return 0;
    }

    // Read the directory index from the server line by line
    do {
        buffer[bytes_received] = '\0';
        char *line = buffer;
        do {
            char *next_line = find_next_line(line);
            index_line(line, request);
            line = next_line;
        } while (line != NULL);
    } while ((bytes_received = recv(fd, buffer, BUFFER_SIZE, 0)) > 0);
    
    return 0;
}

/**
 * A function resembling strtok() using "\r\n" combined (rather than "\r" or
 * "\n") as the delimiter.
 * 
 * @param ptr pointer to a string supposed to end with "\r\n"
 * @return pointer to the string after "\r\n", NULL if not found or out-of-bounds
 */
char *find_next_line(char *ptr) {
    for (char *i = ptr; *i != '\0'; i++) {
        if (*i == '\r' && *(i + 1) == '\n') {
            *i = '\0';
            return *(i + 2) == '\0' ? NULL : i + 2;
        }
    }

    return NULL;
}

/**
 * Given a line of a directory index as a string, index the item by adding it
 * to the linked list.
 * 
 * @param line pointer to the string of the directory index entry
 * @param request pointer to the string of the request
 */
void index_line(char *line, char *request) {
    // Determine the type of that line with reference to the first character
    int item_type = ERROR;
    if (line[0] == '3') {
        // Add the invalid reference to the linked list
        entry *new_item = create_new_entry(item_type, request);
        add_item(new_item);
        return;
    }
    else if (line[0] == '1') item_type = DIRECTORY;
    // Canonical type 1 refers to a directory or an external server    
    else if (line[0] == '0') item_type = TEXT;
    // Canonical type 0 refers to a (non-binary) text file
    else if (is_binary_file(line[0])) item_type = BINARY;
    // Disregard informational messages (type i) and end of response (.)
    else return;

    // Add the text/binary file to the linked list
    if (item_type != ERROR) {
        char *pathname = extract_pathname(line);
        // Index the directory/file
        if (pathname[0] == '/') {
            entry *new_item = create_new_entry(item_type, pathname);
            add_item(new_item);
        }
        else if (item_type == DIRECTORY && pathname[0] == '\0') {
            item_type = EXTERNAL;
            entry *new_item = create_new_entry(item_type, pathname + 1);
            add_item(new_item);
        }
    }
}

/**
 * Create a new entry to be added to the linked list.
 */
entry *create_new_entry(int item_type, char *path) {
    entry *new_item = (entry *)malloc(sizeof(entry));
    size_t len = strlen(path);
    new_item->path = (char *)malloc(len + 1);
    strncpy(new_item->path, path, len + 1);
    new_item->path[len] = '\0';
    new_item->item_type = item_type;
    new_item->next = NULL;
    return new_item;
}

/**
 * RFC 1436 specifies that the canonical type '9' refers to binary files.
 * Some servers make the types more specific, using 'I' for images, 'P' for
 * PDFs, and so on.
 * In this client implementation, files that are not in plain text are all
 * considered binary files.
 * Special features such as nameservers ('2'), Telnet ('8'/'T') and Gopher
 * full-text search ('7') are excluded.
 * 
 * @param type character representing the type
 * @return whether the type refers to a file that is not in plain text format
 */
bool is_binary_file(char type) {
    return type == '9' || type == '4' || type == '5' || type == '6'
            || type == 'g' || type == 'I' || type == ':' || type == ';'
            || type == '<' || type == 'd' || type == 'h' || type == 'p'
            || type == 'r' || type == 's' || type == 'P' || type == 'X';
}

/**
 * A line in a directory index uses tab characters to separate different
 * pieces of information. This function extracts the pathname corresponding
 * to a line, replacing the tab character that follows with a null terminator.
 * 
 * @param line pointer to the string of the directory index line
 * @return pointer to the pathname, NULL if it does not exist
 */
char *extract_pathname(char *line) {
    char *start = NULL;

    // First tab character in the line found: pathname follows
    for (int i = 0; line[i] != '\0'; i++) {
        if (line[i] == '\t') {
            start = &line[i + 1];
            break;
        }
    }

    // Find the second tab character or the end of line
    for (char *i = start; *i != '\0'; i++) {
        if (*i == '\t' || *i == '\r' || *i == '\n') {
            *i = '\0';
            break;
        }
    }

    return start;
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

    if (bytes_received == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            fprintf(stderr, "Error: Server response timeout\n");
        else
            fprintf(stderr, "Error: Unable to receive server response\n");
    }

    if (bytes_received == 0) {
        fprintf(stdout, "No response from the server\n");
        return size;
    }

    // Read the directory index from the server
    do size += bytes_received;
    while ((bytes_received = recv(fd, buffer, BUFFER_SIZE, 0)) > 0);
    
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

    if (bytes_received == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            fprintf(stderr, "Error: Server response timeout\n");
        else
            fprintf(stderr, "Error: Unable to receive server response\n");
    }

    if (bytes_received == 0) {
        fprintf(stdout, "Empty response from the server\n");
        return 0;
    }

    fprintf(stdout, "Content of the smallest text file:\n");
    // Read the directory index from the server
    do {
        buffer[bytes_received] = '\0';
        char *eof = strstr(buffer, ".\r\n\0");
        if (eof != NULL) *eof = '\0';
        fprintf(stdout, "%s", buffer);
    } while ((bytes_received = recv(fd, buffer, BUFFER_SIZE, 0)) > 0);
    
    return 0;
}

/**
 * Free the heap memory occupied by the linked list of indexed items before
 * the main() function returns.
 */
void cleanup(void) {
    entry *c = list;
    while (c != NULL) {
        entry *next = c->next;
        free(c->path);
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
void add_item(entry *new_item) {
    // If the linked list is empty, let the new item be the initial item
    if (list == NULL) {
        list = new_item;
        last_node = new_item;
        return;
    }

    // If the invalid reference is already requested previously, do not add
    entry *c = list;
    while (c != NULL) {
        if (c->item_type == new_item->item_type && strcmp(c->path, new_item->path) == 0) {
            free(new_item->path);
            free(new_item);
            return;
        }
        c = c->next;
    }

    // For logging the type of item indexed
    char *item_type = "item";
    if (new_item->item_type == DIRECTORY) item_type = "directory";
    else if (new_item->item_type == TEXT) item_type = "text file";
    else if (new_item->item_type == BINARY) item_type = "binary file";
    else if (new_item->item_type == ERROR) item_type = "invalid request";
    else if (new_item->item_type == EXTERNAL) item_type = "external server";

    // Otherwise, append the new item to the end of the linked list
    if (new_item->item_type == ERROR)
        // For optimising visualisation
        fprintf(stdout, "Indexed %s: %s", item_type, new_item->path);
    else
        fprintf(stdout, "Indexed %s: %s\n", item_type, new_item->path);
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
    fprintf(stdout, "\nIndexation complete. Now analysing the files.\n");
    int num_of_directories = 0;
    int num_of_text_files = 0;
    int num_of_binary_files = 0;
    int num_of_invalid_references = 0;
    char *smallest_text_file = NULL;
    int size_of_smallest_text_file = -1;
    int size_of_largest_text_file = -1;
    int size_of_smallest_binary_file = -1;
    int size_of_largest_binary_file = -1;

    entry *c = list;
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

    fprintf(stdout, "\nNumber of directories: %d\n", num_of_directories);
    fprintf(stdout, "Number of text files: %d\n", num_of_text_files);
    fprintf(stdout, "Number of binary files: %d\n", num_of_binary_files);
    fprintf(stdout, "Number of invalid references: %d\n\n", num_of_invalid_references);
    gopher_connect(print_response, smallest_text_file);
    fprintf(stdout, "\nSize of the smallest text file: %d\n", size_of_smallest_text_file);
    fprintf(stdout, "Size of the largest text file: %d\n", size_of_largest_text_file);
    fprintf(stdout, "Size of the smallest binary file: %d\n", size_of_smallest_binary_file);
    fprintf(stdout, "Size of the largest binary file: %d\n", size_of_largest_binary_file);
}
