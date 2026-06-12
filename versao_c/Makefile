CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -pthread
TARGET = jogo_pthreads_c
SRC = jogo_pthreads.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)