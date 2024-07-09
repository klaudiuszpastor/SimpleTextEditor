TARGET = main
CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99
SRCS = main.c
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)