# ─────────────────────────────────────────────────────────────
#  Khan Language — Makefile (VM edition)
# ─────────────────────────────────────────────────────────────

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2 -Isrc \
          -D_POSIX_C_SOURCE=200809L \
          -Wno-implicit-function-declaration \
          -Wno-builtin-declaration-mismatch \
          -Wno-cast-function-type

ifeq ($(OS),Windows_NT)
    LDFLAGS  = -lm -lwinhttp -lshell32
    EXT      = .exe
else
    LDFLAGS  = -lm
    EXT      =
endif

# Shared (lexer/parser/AST unchanged)
COMMON_SRCS = \
    src/lexer.c         \
    src/parser.c        \
    src/ast.c

# Old interpreter + stdlib needed by native libs (json, datetime, requests)
INTERP_SRCS = \
    src/interpreter.c   \
    src/stdlib.c

# New stack VM
VM_SRCS = \
    src/value.c         \
    src/chunk.c         \
    src/compiler.c      \
    src/vm.c

# Native C libraries (use interpreter.h Value / Environment API)
NATIVE_SRCS = \
    src/json_lib.c      \
    src/datetime_lib.c  \
    src/requests_lib.c

MAIN_VM = src/main.c
KH_SRCS = src/kh.c

.PHONY: all khan kh clean

all: khan$(EXT) kh$(EXT)

khan$(EXT): $(COMMON_SRCS) $(INTERP_SRCS) $(VM_SRCS) $(NATIVE_SRCS) $(MAIN_VM)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "  Built $@"

kh$(EXT): $(KH_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "  Built $@"

clean:
ifeq ($(OS),Windows_NT)
	-del /Q /F src\*.o khan.exe kh.exe 2>nul
else
	rm -f src/*.o khan kh
endif