CC = gcc
CFLAGS = -Wall -Wextra -O3
TARGET = client
SRCS = client.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm $(TARGET)
