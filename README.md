# Gopher Indexing Client

This Internet Gopher client written in the C programming language indexes
directories and files. It is implemented in accordance with
[RFC 1436](https://www.rfc-editor.org/rfc/rfc1436) for indexing directories
and files.

## Initialisation

The program is intended for Linux machines with GCC installed. The standard
C library is sufficient. Simply execute the command
`./client <server address> <port>` to run the program. For example,
if the Gopher server is hosted locally at port 70, `./client 127.0.0.1 70` can
be executed. Use the command `make` to compile the program with the
[source code](client.c).

For local testing, a local Gopher server can be started using
[Motsognir](https://github.com/unisx/motsognir) with the command
`sudo motsognir`. The listening port and the process can be listed using the
command `sudo lsof -i :70`.

## Program Design

The overall design relies on performing a breadth-first search (BFS) of the
filesystem hosted on the Gopher server. The indexed files and directories are
recorded in a linked list implemented using the `item` struct.

Since the protocol is stateless and the connection is terminated once the server
has responded, the function `gopher_connect()` establishes and closes a
connection every time a new request is made. The function `gopher_connect()`
is responsible for appending `\r\n` to the request (passed as the parameter
`path`) and sending the request line to the server. The function `func()`,
passed as another argument to `gopher_connect()`, is responsible for receiving
the response from the server.

Instantiations of `func()` include `indexing()`, `evaluate_file_size()` and
`print_response()`. With reference to the protocol guidelines, the client
listens for a response until the server terminates the connection. Therefore,
these three functions use `recv()` provided by the Socket API in a while loop.

### Recording Indexed Files and Directories

As long as the server adheres to the protocol's format, its response to a
pathname is always the directory index with the first character of a row
indicating the nature of the file. For instance, `0` indicates that the file
is a (non-binary) text file whereas `1` refers to a subdirectory. The function
`indexing()` uses this information to determine the type of the file/directory.
A new entry is added to the linked list, including the item's pathname and type.

If the request is invalid, the corresponding response begins with `3`. In this
case, a new entry is added to the linked list if and only if it does not already
exist. This enables us to count the number of invalid references at a later
stage.

Any row in the response starting with the character `i` is a human-readable
informational message, thus ignored by the indexation process.

### Recursively Index Subdirectories

Following the indexation of the root directory using the request `\r\n`,
`main()` goes through the linked list and calls `gopher_connect()` for every
subdirectory. This effectively models a breadth-first search of the filesystem
hosted on the Gopher server.

### Evaluation and Loading of File Content

Upon indexation of all directories and files in the filesystem, `evaluate()` is
called to find the following information:
1. Number of directories, text files and binary files
2. Content of the smallest text file (the earliest one if multiple exist)
3. Sizes of the smallest and the largest text files
4. Sizes of the smallest and the largest binary files
5. Number of invalid references (each unique invalid request is counted once)

The list of external servers and references with “issues/errors” are yet to be
implemented.

### Terminal Output

The terminal outputs are in the following formats.
- `Request sent at <timestamp>: <request>`
- `Indexed: <pathname of newly indexed item>`
- `Number of <directories/text files/binary files/invalid references>: <number>`
- `Size of the <smallest/largest text/binary file>: <number>`

### Minimising Errors and Maximising Security

Whilst this client program assumes the server responds in a format in line with
the Gopher protocol standard, measures are taken to reduce the impact of
malformed responses. For instance, the response is written to a string variable
and is always explicitly terminated with the null terminator (`\0`). This
avoids problems such as out-of-bound reading/writing.

Safer built-in functions in C (such as `strncpy()` rather than `strcpy()`) are
used to avoid unexpected behaviour that might possibly be the result of long
requests or responses. Indexed records in the linked list, with memory allocated
using `malloc()`, are cleaned up by `cleanup()` before `main()` terminates.

A well-implemented server should respond with some content under all
circumstances. However, to optimise the client's handling of edge cases,
"No response from the server" is printed to the terminal if the server
terminates the connection without any response.

Servers may be busy or unresponsive at times. Timeout is implemented to prevent
the program from getting stuck indefinitely using `setsockopt()`, a built-in
feature in the Socket API, and the `timeval` struct.

A well-implemented server's directory index response should include four pieces
of information, separated by tab characters. In case a line therein is
malformed, the program handles it gracefully to avoid indexing invalid things.
It is also designed to handle long responses from the server.

Unicode was not published at the time the Gopher protocol was publicised.
We assume that pathnames contain only ASCII characters, or else converted into
ASCII representations on the server side (as it is in Motsognir). For example,
a file named "ɡoʊfər" is detected as "%C9%A1o%CA%8Af%C9%99r".

At the beginning of `gopher_connect()`, the program is terminated if the
connection cannot be established. An error message is printed to `stderr`
specifying the issue.

## References

1. Anklesaria et al. RFC 1436: The Internet Gopher Protocol. *The RFC Series*.
   https://www.rfc-editor.org/rfc/rfc1436
2. Arkaitz Jimenez. Time stamp in the C programming language. *Stack Overflow*.
   https://stackoverflow.com/questions/1444428/time-stamp-in-the-c-programming-language#1444440
3. dgookin. The `gettimeofday()` Function. *C For Dummies Blog*.
   https://c-for-dummies.com/blog/?p=4236
4. IEEE and The Open Group. `setsockopt`. *Linux man page*.
   https://linux.die.net/man/3/setsockopt
5. Milan. How to convert string to IP address and vice versa. *Stack Overflow*.
   https://stackoverflow.com/questions/5328070/how-to-convert-string-to-ip-address-and-vice-versa#5328184
6. Wikipedia contributors. Gopher (protocol). *Wikipedia*.
   https://en.wikipedia.org/wiki/Gopher_(protocol)


Last update: 2024-04-07
