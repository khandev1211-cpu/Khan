CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -g

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)

khan: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OBJ) khan.exe khan