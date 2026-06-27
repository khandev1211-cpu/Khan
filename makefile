CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -g

# Detect OS
ifeq ($(OS),Windows_NT)
    PLATFORM = windows
    LIBS     = -lm -lwinhttp
    EXE      = khan.exe
else
    PLATFORM = posix
    LIBS     = -lm
    EXE      = khan
endif

SRC = src/main.c src/lexer.c src/ast.c src/parser.c src/interpreter.c \
      src/stdlib.c src/json_lib.c src/datetime_lib.c src/requests_lib.c

OBJ = $(SRC:.c=.o)

$(EXE): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f $(OBJ) khan khan.exe
