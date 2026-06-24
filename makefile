CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -g

SRC = src/main.c src/lexer.c src/ast.c src/parser.c
OBJ = $(SRC:.c=.o)

khan: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OBJ) khan.exe khan
