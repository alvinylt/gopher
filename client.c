#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>

/* Global constant: buffer size and limit for receiving server content */
#define BUFFER_SIZE 4096  // String buffer size
#define FILE_LIMIT 65536  // Size limit for downloading files

/* Global constants: file types and error types */
#define DIRECTORY 0  // Directory
#define TEXT 1       // Text (non-binary) file
#define BINARY 2     // Binary file
#define ERROR 3      // Error message
#define EXTERNAL 4   // Reference to external server
#define TIMEOUT 5    // Access timeout
#define TOO_LARGE 6  // File size is too large

/* Linked list entry containing information of an indexed item */
typedef struct entry {
    char *record;        // Pathname, error message or external server
    int item_type;       // Type of the item (directory, file, error, etc.)
    struct entry *next;  // Linked list pointer to the next item
} entry;

/* Helper functions */
static ssize_t gopher_connect(ssize_t (*func)(char *), char *request);
static ssize_t indexing(char *request);
static void index_line(char *line, char *request);
static entry *create_new_entry(int item_type, char *path);
static bool is_binary_file(char type);
static char *find_next_line(char *ptr);
static char *extract_pathname(char *line);
static ssize_t evaluate_file_size(char *request);
static ssize_t print_response(char *request);
static void add_item(entry *new_item);
static void evaluate(void);
static void cleanup(void);
static void test_external_servers(entry *item);

/* Global variables: values used across all functions */
static int fd;                          // Socket file descriptor
static struct hostent *server;          // Hostname/IP address of the server
static int port;                        // Port of the Gopher server
static struct sockaddr_in server_addr;  // Address and port information
static struct entry *list = NULL;       // First item of the linked list
static struct entry *last_node = NULL;  // Last item of the linked list

/**
 * The Internet Gopher client indexing files.
 */
int main(int argc, char* argv[]) {
    // Parse the command input
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <hostname> <port>\n", argv[0]);
        exit(EXIT_SUCCESS);
    }
    
    // Convert hostname into IP address
    server = gethostbyname(argv[1]);
    if (server == NULL) {
        fprintf(stderr, "Error: unable to connect to host %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }
    // Convert the second argument (port number) into integer format
    port = atoi(argv[2]);
    
    // Begin the indexing process, starting with the root directory
    gopher_connect(indexing, "");

    // Iterate through the linked list of indexed items to index subdirectories
    entry *c = list;
    while (c != NULL) {
        if (c->item_type == DIRECTORY)
            gopher_connect(indexing, c->record);
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
 * @param func function handling response from the Gopher server
 * @param request request to be send to the Gopher server
 * @return the output from the function handling response
 */
static ssize_t gopher_connect(ssize_t (*func)(char *), char *request) {
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
    int connect_status =
        connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (connect_status == -1) {
        fprintf(stderr, "Error: Connection failed\n");
        exit(EXIT_FAILURE);
    }

    // Append "\r\n" to the end of the path to form a request line
    size_t path_length = strlen(request);
    char new_request[path_length + 3];
    strncpy(new_request, request, path_length + 2);
    new_request[path_length] = '\r';
    new_request[path_length + 1] = '\n';
    new_request[path_length + 2] = '\0';

    // Send a request for the directory index to the server
    send(fd, new_request, path_length + 2, 0);
    struct timeval tv;  // Timestamping for logging sent requests
    gettimeofday(&tv, NULL);
    struct tm *timeinfo = localtime(&tv.tv_sec);
    char time[32];
    strftime(time, sizeof(time), "%Y-%m-%d %H:%M:%S", timeinfo);
    fprintf(stdout, "Request sent at %s: %s", time, new_request);

    // Execute the function that receives and handles server's response
    int output = (*func)(new_request);

    // Terminate the connection and release the socket
    close(fd);

    return output;
}

/**
 * Upon sending a request for a directory index, this function reads the
 * response from the Gopher server and adds the indexed directories and files
 * to the linked list.
 * 
 * Each line in the directory index is handled by the helper function
 * index_line().
 * 
 * @param request the request line sent to the server
 */
static ssize_t indexing(char *request) {
    // Buffer for strings read from the server
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received = recv(fd, buffer, BUFFER_SIZE, 0);

    // Handle failure in receiving server's response
    if (bytes_received == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            fprintf(stderr, "Error: Server response timeout\n");
            entry *new_item = create_new_entry(TIMEOUT, request);
            add_item(new_item);
        }
        else fprintf(stderr, "Error: Unable to receive server response\n");
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
 * @return pointer to the string after "\r\n", NULL if out-of-bounds
 */
static char *find_next_line(char *ptr) {
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
static void index_line(char *line, char *request) {
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
    // Disregard informational messages, end of response and irrelevant entries
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
 * 
 * @param item_type type of the record
 * @param path pointer to the record string
 * @return a linked list entry for the record
 */
static entry *create_new_entry(int item_type, char *path) {
    entry *new_item = (entry *)malloc(sizeof(entry));
    size_t len = strlen(path);
    new_item->record = (char *)malloc(len + 1);
    strncpy(new_item->record, path, len + 1);
    new_item->record[len] = '\0';
    new_item->item_type = item_type;
    new_item->next = NULL;
    return new_item;
}

/**
 * RFC 1436 specifies that the canonical type '9' refers to binary files.
 * Some servers make the types more specific, using 'I' for images, 'P' for
 * PDFs, etc.
 * 
 * In this client implementation, files that are not in plain text are all
 * considered binary files.
 * 
 * Special features such as nameservers ('2'), Telnet ('8'/'T') and Gopher
 * full-text search ('7') are excluded.
 * 
 * @param type character representing the type
 * @return whether the type refers to a file that is not in plain text format
 */
static bool is_binary_file(char type) {
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
static char *extract_pathname(char *line) {
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
 * @return size of the requested file, -1 if the size exceeds limit
 */
static ssize_t evaluate_file_size(char *request) {
    // Unused parameter
    (void)request;

    // Buffer for strings read from the server
    char buffer[BUFFER_SIZE];
    ssize_t size = 0;
    ssize_t bytes_received = recv(fd, buffer, BUFFER_SIZE, 0);

    // Handle failure in receiving server's response
    if (bytes_received == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            fprintf(stderr, "Error: Server response timeout\n");
            entry *new_item = create_new_entry(TIMEOUT, request);
            add_item(new_item);
        }
        else
            fprintf(stderr, "Error: Unable to receive server response\n");
        return -2;
    }

    if (bytes_received == 0) {
        fprintf(stdout, "No response from the server\n");
        return size;
    }

    // Started response but timeout for taking too much time to finish
    struct timeval response_timeout;
    response_timeout.tv_sec = 5;
    response_timeout.tv_usec = 0;
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);
    
    // Read the directory index from the server
    do {
        int ready = select(fd + 1, &read_fds, NULL, NULL, &response_timeout);
        if (ready < 0) fprintf(stderr, "Error: Timeout configuration\n");
        else if (ready == 0) {
            fprintf(stderr, "Error: Server response timeout\n");
            entry *new_item = create_new_entry(TIMEOUT, request);
            add_item(new_item);
            return -2;
        }
        size += bytes_received;
        // Stop evaluation if file is too large
        if (size >= FILE_LIMIT) return -1;
    }
    while ((bytes_received = recv(fd, buffer, BUFFER_SIZE, 0)) > 0);
    
    return size;
}

/**
 * Upon sending a request to the Gopher server for the smallest text file,
 * print the entire content to the terminal.
 */
static ssize_t print_response(char *request) {
    // Unused parameter
    (void)request;

    // Buffer for strings read from the server
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received = recv(fd, buffer, BUFFER_SIZE, 0);

    // Handle failure in receiving server's response
    if (bytes_received == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            fprintf(stderr, "Error: Server response timeout\n");
            entry *new_item = create_new_entry(TIMEOUT, request);
            add_item(new_item);
        }
        else
            fprintf(stderr, "Error: Unable to receive server response\n");
    }

    if (bytes_received == 0) {
        fprintf(stdout, "Empty response from the server\n");
        return 0;
    }
    
    // Started response but timeout for taking too much time to finish
    struct timeval response_timeout;
    response_timeout.tv_sec = 5;
    response_timeout.tv_usec = 0;
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);

    fprintf(stdout, "Content of the smallest text file:\n");
    // Read the directory index from the server
    do {
        int ready = select(fd + 1, &read_fds, NULL, NULL, &response_timeout);
        if (ready < 0) fprintf(stderr, "Error: Timeout configuration\n");
        else if (ready == 0) {
            fprintf(stderr, "Error: Server response timeout\n");
            entry *new_item = create_new_entry(TIMEOUT, request);
            add_item(new_item);
            return -1;
        }

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
static void cleanup(void) {
    entry *c = list;
    while (c != NULL) {
        entry *next = c->next;
        free(c->record);
        free(c);
        c = next;
    }
}

/**
 * Add a new item to the linked list of indexed items.
 * 
 * @param new_item pointer to the new indexed item
 */
static void add_item(entry *new_item) {
    // If the linked list is empty, let the new item be the initial item
    if (list == NULL) {
        list = new_item;
        last_node = new_item;
        return;
    }

    // If the invalid reference is already requested previously, do not add
    entry *c = list;
    while (c != NULL) {
        if (c->item_type == new_item->item_type && strcmp(c->record, new_item->record) == 0) {
            free(new_item->record);
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
    else if (new_item->item_type == TIMEOUT) item_type = "timeout";
    else if (new_item->item_type == TOO_LARGE) item_type = "too large";

    // Otherwise, append the new item to the end of the linked list
    if (new_item->item_type == ERROR)
        // For optimising visualisation
        fprintf(stdout, "Indexed %s: %s", item_type, new_item->record);
    else if (new_item->item_type == TIMEOUT)
        fprintf(stderr, "Transmission %s: %s", item_type, new_item->record);
    else if (new_item->item_type == TOO_LARGE)
        fprintf(stderr, "File %s: %s\n", item_type, new_item->record);
    else
        fprintf(stdout, "Indexed %s: %s\n", item_type, new_item->record);

    last_node->next = new_item;
    last_node = new_item;
}

/**
 * Evaluate and print to the terminal:
 *     1. Number of directories, text files, binaries and invalid references
 *     2. Sizes of the smallest/largest text/binary files
 *     3. Content of the smallest text file
 */
static void evaluate(void) {
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

    /* Find the number and sizes of directories, files and references */

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
                file_size = gopher_connect(evaluate_file_size, c->record);
                if (file_size == -1) {
                    fprintf(stderr, "The file %s is too large\n", c->record);
                    entry *new_item = create_new_entry(TOO_LARGE, c->record);
                    add_item(new_item);
                    break;
                }
                else if (file_size == -2) {
                    break;
                }

                if (size_of_smallest_text_file == -1
                        || file_size < size_of_smallest_text_file) {
                    size_of_smallest_text_file = file_size;
                    smallest_text_file = c->record;
                }
                if (size_of_largest_text_file == -1
                        || file_size > size_of_largest_binary_file) {
                    size_of_largest_text_file = file_size;
                }

                break;
            case BINARY:
                num_of_binary_files++;

                // Evaluate the size of the file
                file_size = gopher_connect(evaluate_file_size, c->record);
                if (file_size == -1) {
                    fprintf(stderr, "The file %s is too large\n", c->record);
                    entry *new_item = create_new_entry(TOO_LARGE, c->record);
                    add_item(new_item);
                    break;
                }
                else if (file_size == -2) {
                    break;
                }

                if (size_of_smallest_binary_file == -1
                        || file_size < size_of_smallest_binary_file) {
                    size_of_smallest_binary_file = file_size;
                }
                if (size_of_largest_binary_file == -1
                        || file_size > size_of_largest_binary_file) {
                    size_of_largest_binary_file = file_size;
                }

                break;
            case ERROR:
                num_of_invalid_references++;
                break;
        }
        c = c->next;
    }

    // Print the number of directories, text files, binary files and errors
    fprintf(stdout, "\nNumber of directories: %d\n"
                    "Number of text files: %d\n"
                    "Number of binary files: %d\n"
                    "Number of invalid references: %d\n\n",
                    num_of_directories, num_of_text_files,
                    num_of_binary_files, num_of_invalid_references);

    // Print the content of the smallest text file
    gopher_connect(print_response, smallest_text_file);

    // Print the sizes of the smallest/largest text/binary files
    fprintf(stdout, "\nSize of the smallest text file: %d\n"
                    "Size of the largest text file: %d\n"
                    "Size of the smallest binary file: %d\n"
                    "Size of the largest binary file: %d\n",
                    size_of_smallest_text_file, size_of_largest_text_file,
                    size_of_smallest_binary_file, size_of_largest_binary_file);

    // Test and print the connectivity to external servers
    fprintf(stdout, "\nConnectivity to external servers:\n");
    c = list;
    bool external_server_exists = false;
    while (c != NULL) {
        if (c->item_type == EXTERNAL) {
            external_server_exists = true;
            test_external_servers(c);
        }
        c = c->next;
    }
    if (!external_server_exists)
        fprintf(stdout, "No reference to any external server indexed\n");
    
    // List all references with issues/errors
    fprintf(stdout, "\nReferences with issues/errors:\n");
    c = list;
    bool issues_exists = false;
    while (c != NULL) {
        int type = c->item_type;
        if (type == ERROR || type == TIMEOUT || type == TOO_LARGE) {
            issues_exists = true;
            char *issue_type = "Invalid reference";
            if (type == TIMEOUT) issue_type = "Timeout";
            else if (type == TOO_LARGE) issue_type = "File too large";
            fprintf(stdout, "(%s) %s%s", issue_type, c->record,
                    type != TOO_LARGE ? "" : "\n");
        }
        c = c->next;
    }
    if (!issues_exists)
        fprintf(stdout, "No reference with issue/error found\n");
}

/**
 * Consider the external servers indexed and recorded in the linked list.
 * Test whether those external servers are up and print the status.
 * 
 * @param item entry of type EXTERNAL
 */
static void test_external_servers(entry *item) {
    // Only handle entries referring to an external server
    if (item->item_type != EXTERNAL) {
        fprintf(stderr, "Error: Entry not referring to an external server\n");
        return;
    }

    // Extract the server's hostname and port from the entry
    char server_info[BUFFER_SIZE + 1];
    strncpy(server_info, item->record, BUFFER_SIZE);
    char *ext_hostname = strtok(server_info, "\t");
    char *ext_port = strtok(NULL, "\r\n");
    bool connectivity = false;

    // Create the socket
    int ext_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ext_fd == -1) {
        fprintf(stderr, "Error: Socket creation failed\n");
        exit(EXIT_FAILURE);
    }

    // Configure timeout limit and disable blocking by the socket
    fcntl(ext_fd, F_SETFL, O_NONBLOCK);
    struct timeval connection_timeout;
    fd_set fdset;
    connection_timeout.tv_sec = 5;
    connection_timeout.tv_usec = 0;
    FD_ZERO(&fdset);
    FD_SET(ext_fd, &fdset);
    if (setsockopt(ext_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&connection_timeout, sizeof(connection_timeout)) < 0) {
        fprintf(stderr, "Error: Timeout configuration failed\n");
        exit(EXIT_FAILURE);
    }

    struct hostent *ext_server = gethostbyname(ext_hostname);
    struct sockaddr_in ext_server_addr;
    if (server != NULL) {
        // Specify the IP address and the port for connection
        ext_server_addr.sin_family = AF_INET;
        ext_server_addr.sin_port = htons(atoi(ext_port));
        memcpy(&ext_server_addr.sin_addr.s_addr,
                ext_server->h_addr_list[0], ext_server->h_length);
        
        // No need to test if the current server is referenced
        if (ext_server_addr.sin_addr.s_addr == server_addr.sin_addr.s_addr
                && atoi(ext_port) == port)
        return;
        
        // Attempt the connection
        connect(ext_fd, (struct sockaddr *)&ext_server_addr, sizeof(ext_server_addr));

        if (select(ext_fd + 1, NULL, &fdset, NULL, &connection_timeout) == 1) {
            int so_error;
            socklen_t len = sizeof so_error;
            getsockopt(ext_fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
            if (so_error == 0) {
                connectivity = true;
            }
        }
    }
    
    fprintf(stdout, "Server %s at port %s is %s\n",
            ext_hostname, ext_port, connectivity ? "up" : "down");
}
