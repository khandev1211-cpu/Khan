# ─────────────────────────────────────────────────────────────
#  Khan Language — Makefile
#  Builds: khan.exe (The High-Performance Bytecode VM)
# ─────────────────────────────────────────────────────────────

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2 -Isrc \
          -D_POSIX_C_SOURCE=200809L \
          -Wno-implicit-function-declaration \
          -Wno-builtin-declaration-mismatch \
          -Wno-cast-function-type

# Installation paths
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

ifeq ($(OS),Windows_NT)
    LDFLAGS  = -lm -lwinhttp -lshell32 -lws2_32 -ladvapi32
    EXT      = .exe
else
    LDFLAGS  = -lm
    EXT      =
endif

# All Source Files for the unified High-Performance Khan
SRCS = \
    src/lexer.c         \
    src/parser.c        \
    src/ast.c           \
    src/chunk.c         \
    src/value.c         \
    src/compiler.c      \
    src/vm.c            \
    src/vm_libs.c       \
    src/interpreter.c   \
    src/khan_stdlib.c   \
    src/json_lib.c      \
    src/datetime_lib.c  \
    src/requests_lib.c  \
    src/webi_lib.c      \
    src/sqlite_lib.c    \
    src/vision_lib.c    \
    src/vision_cv.c     \
    src/vision_cascade.c \
    src/main.c

KH_SRCS = src/kh.c

.PHONY: all khan kh clean

all: khan$(EXT) kh$(EXT)

khan$(EXT): $(SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "  Built khan$(EXT) (High-Performance VM Edition)"

kh$(EXT): $(KH_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "  Built kh$(EXT) (Package Manager)"

install: all
ifeq ($(OS),Windows_NT)
	@echo "Please add the current directory to your PATH manually on Windows."
else
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 khan$(EXT) $(DESTDIR)$(BINDIR)/khan
	install -m 755 kh$(EXT) $(DESTDIR)$(BINDIR)/kh
	@echo "Khan and Kh installed to $(DESTDIR)$(BINDIR)"
endif

uninstall:
ifeq ($(OS),Windows_NT)
	@echo "Please remove the files manually on Windows."
else
	rm -f $(DESTDIR)$(BINDIR)/khan
	rm -f $(DESTDIR)$(BINDIR)/kh
	@echo "Khan and Kh removed from $(DESTDIR)$(BINDIR)"
endif

clean:
ifeq ($(OS),Windows_NT)
	-del /Q /F src\*.o khan.exe kh.exe 2>nul
else
	rm -f src/*.o khan kh
endif
