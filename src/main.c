// ===========================================================================
// main.c — Khan High-Performance Entry Point
//
// Uses the Bytecode VM for execution (36% faster than Python).
// ===========================================================================

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

int main(int argc, char *argv[]) {
    enable_ansi();

    if (argc < 2) {
        fprintf(stderr, "Usage: khan <script.kh> [args...]\n");
        return 64;
    }

    char *source = read_file(argv[1]);

    /* ── Lex ── */
    Lexer lexer;
    lexer_init(&lexer, source);

    /* ── Parse ── */
    Parser parser;
    parser_init(&parser, &lexer);
    AstNode *program = parser_parse(&parser);

    if (parser.had_error) {
        ast_free(program);
        free(source);
        return 65;
    }

    /* ── Compile to VM bytecode ── */
    KhanFunction *script = compile(program, argv[1]);
    if (!script) {
        ast_free(program);
        free(source);
        return 65;
    }

    /* ── Initialize VM ── */
    VM vm;
    vm_init(&vm);

    /* ── Register built-ins and libraries ── */
    vm_register_builtins(&vm);
    json_register_all_vm(&vm);
    datetime_register_all_vm(&vm);
    requests_register_all_vm(&vm);
    webi_register_all_vm(&vm);
    sqlite_register_all_vm(&vm);

    /* ── Pass command line arguments as global 'argv' ── */
    Value argv_val = value_array(NULL, 0);
    for (int i = 2; i < argc; i++) {
        Obj *o = argv_val.as.obj;
        o->as.array.items = realloc(o->as.array.items, sizeof(Value) * (o->as.array.count + 1));
        o->as.array.items[o->as.array.count++] = value_string(argv[i]);
        o->as.array.capacity = o->as.array.count;
    }
    vm_global_set(&vm, "argv", argv_val);

    /* ── Execute ── */
    InterpretResult result = vm_run(&vm, script);

    vm_free(&vm);

    return result == INTERPRET_OK ? 0 : 70;
}
