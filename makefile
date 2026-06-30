# ─────────────────────────────────────────────────────────────
#  Khan Language — Makefile
#  Builds: khan.exe (interpreter + webi) and kh.exe (package manager)
# ─────────────────────────────────────────────────────────────

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2 -Isrc \
          -D_POSIX_C_SOURCE=200809L \
          -Wno-implicit-function-declaration \
          -Wno-builtin-declaration-mismatch \
          -Wno-cast-function-type

ifeq ($(OS),Windows_NT)
    LDFLAGS  = -lm -lwinhttp -lshell32 -lws2_32
    EXT      = .exe
else
    LDFLAGS  = -lm
    EXT      =
endif

# Core language files (unchanged)
COMMON_SRCS = \
    src/lexer.c         \
    src/parser.c        \
    src/ast.c

# Tree-walk interpreter
INTERP_SRCS = \
    src/interpreter.c   \
    src/khan_stdlib.c

# Native C libraries — all use interpreter.h Value / Environment API
NATIVE_SRCS = \
    src/json_lib.c      \
    src/datetime_lib.c  \
    src/requests_lib.c  \
    src/webi_lib.c

MAIN_SRC = src/main.c
KH_SRCS  = src/kh.c

.PHONY: all khan kh clean

all: khan$(EXT) kh$(EXT)

khan$(EXT): $(COMMON_SRCS) $(INTERP_SRCS) $(NATIVE_SRCS) $(MAIN_SRC)
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