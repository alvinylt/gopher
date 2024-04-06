CC = gcc
CFLAGS = -Wall -Wextra -Wno-incompatible-pointer-types -std=c99
TARGET = client
SRCS = client.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)
