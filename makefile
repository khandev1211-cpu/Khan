CC     = gcc
CFLAGS = -std=c11 -Wall -Wextra -g

ifeq ($(OS),Windows_NT)
    LIBS_KHAN = -lm -lwinhttp
    LIBS_KH   = -lm -lshell32
    KHAN_EXE  = khan.exe
    KH_EXE    = kh.exe
    RM        = del /Q /F
    FIXPATH   = $(subst /,\,$1)
else
    LIBS_KHAN = -lm
    LIBS_KH   = -lm
    KHAN_EXE  = khan
    KH_EXE    = kh
    RM        = rm -f
    FIXPATH   = $1
endif

KHAN_SRC = src/main.c src/lexer.c src/ast.c src/parser.c src/interpreter.c \
           src/stdlib.c src/json_lib.c src/datetime_lib.c src/requests_lib.c

KH_SRC   = src/kh.c

KHAN_OBJ = $(KHAN_SRC:.c=.o)
KH_OBJ   = $(KH_SRC:.c=.o)

all: $(KHAN_EXE) $(KH_EXE)

$(KHAN_EXE): $(KHAN_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_KHAN)

$(KH_EXE): $(KH_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_KH)

clean:
ifeq ($(OS),Windows_NT)
	-del /Q /F $(call FIXPATH,$(KHAN_OBJ)) $(call FIXPATH,$(KH_OBJ)) khan.exe kh.exe 2>nul
else
	rm -f $(KHAN_OBJ) $(KH_OBJ) khan kh
endif