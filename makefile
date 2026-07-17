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

ifeq ($(OS),Windows_NT)
    LDFLAGS     = -lm -lwinhttp -lshell32 -lws2_32 -ladvapi32
    EXT         = .exe
    # No pkg-config assumed on a bare MinGW toolchain: install tesseract via
    # MSYS2 (pacman -S mingw-w64-x86_64-tesseract-ocr), whose prefix is
    # already on the default include/lib search path. See docs/ocr.md.
    TESS_CFLAGS =
    TESS_LIBS   = -ltesseract
else
    LDFLAGS     = -lm
    EXT         =
    # pkg-config finds tesseract wherever apt/Homebrew put it (handles the
    # Homebrew-on-Apple-Silicon /opt/homebrew case that a bare -ltesseract
    # would miss). Falls back to a bare -ltesseract if pkg-config itself,
    # or its tesseract.pc, isn't found.
    TESS_CFLAGS := $(shell pkg-config --cflags tesseract 2>/dev/null)
    TESS_LIBS   := $(shell pkg-config --libs tesseract 2>/dev/null || echo -ltesseract)
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
    src/ocr_lib.c        \
    src/main.c

KH_SRCS = src/kh.c

.PHONY: all khan kh clean

all: khan$(EXT) kh$(EXT)

khan$(EXT): $(SRCS)
	$(CC) $(CFLAGS) $(TESS_CFLAGS) $^ -o $@ $(LDFLAGS) $(TESS_LIBS)
	@echo "  Built khan$(EXT) (High-Performance VM Edition)"

kh$(EXT): $(KH_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "  Built kh$(EXT) (Package Manager)"

clean:
ifeq ($(OS),Windows_NT)
	-del /Q /F src\*.o khan.exe kh.exe 2>nul
else
	rm -f src/*.o khan kh
endif
