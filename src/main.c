/*
 * main_vm.c — Khan entry point using the new stack-based VM
 *
 * Drop this file into src/ and rename it main.c (replacing the old one),
 * or compile with -DUSE_VM and let the Makefile choose.
 *
 * The lexer, parser, and AST are completely unchanged.
 */

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
static void enable_ansi(void) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleOutputCP(CP_UTF8);
}
#else
static void enable_ansi(void) {}
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "chunk.h"
#include "compiler.h"
#include "vm.h"

/* ── Read entire file into a heap-allocated string ── */
static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }
    fseek(file, 0L, SEEK_END);
    long size = ftell(file);
    rewind(file);

    char *buffer = malloc(size + 1);
    if (!buffer) { fprintf(stderr, "Out of memory.\n"); exit(74); }
    size_t n = fread(buffer, 1, size, file);
    buffer[n] = '\0';
    fclose(file);
    return buffer;
}

/* ── Main ── */
int main(int argc, char *argv[]) {
    enable_ansi();

    if (argc != 2) {
        fprintf(stderr, "Usage: khan <script.kh>\n");
        return 64;
    }

    /* 1. Read source */
    char *source = read_file(argv[1]);

    /* 2. Lex */
    Lexer lexer;
    lexer_init(&lexer, source);

    /* 3. Parse → AST (unchanged) */
    Parser parser;
    parser_init(&parser, &lexer);
    AstNode *program = parser_parse(&parser);

    if (parser.had_error) {
        fprintf(stderr, "Parse completed with errors.\n");
        ast_free(program);
        free(source);
        return 65;
    }

    /* 4. Compile AST → bytecode */
    KhanFunction *script = compile(program);
    ast_free(program);   /* AST no longer needed after compilation */

    if (!script) {
        fprintf(stderr, "Compilation failed.\n");
        free(source);
        return 65;
    }

    /* 5. Execute on the VM */
    VM *vm = malloc(sizeof(VM));
    if (!vm) { fprintf(stderr, "Out of memory\n"); return 70; }
    vm_init(vm);
    vm_register_builtins(vm);

    InterpretResult result = vm_run(vm, script);

    /* 6. Clean up */
    vm_free(vm);
    free(vm);
    khanfn_free(script);
    free(source);

    switch (result) {
        case INTERPRET_OK:            return 0;
        case INTERPRET_COMPILE_ERROR: return 65;
        case INTERPRET_RUNTIME_ERROR: return 70;
        default:                      return 70;
    }
}