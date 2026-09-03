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
#ifndef _WIN32
#include <limits.h>
#include <stdlib.h> /* realpath */
#endif

#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "compiler.h"
#include "vm.h"
#include "vision_lib.h"
#include "vision_cv.h"
#include "vision_cascade.h"
#include "khan_version.h"
#ifdef LLM_SUPPORT
#include "llm_lib.h"
#endif

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

static void print_usage(FILE *out) {
    fprintf(out,
        "Khan " KHAN_VERSION "\n"
        "Usage: khan <script.kh> [args...]\n"
        "       khan --version | -v\n"
        "       khan --help    | -h\n"
        "\n"
        "Once run, extra arguments after the script path are available\n"
        "inside the script as the global array `argv` (argv[0] is the\n"
        "first extra argument, matching argv[2] on the C command line —\n"
        "the script path itself, argv[1] in C terms, is not included).\n");
}

int main(int argc, char *argv[]) {
    enable_ansi();

    if (argc < 2) {
        print_usage(stderr);
        return 64;
    }

    /* --version/-v and --help/-h are recognized wherever they'd
       otherwise be read as the script path (argv[1]) — matching how
       most CLI tools special-case these two regardless of position
       among the first argument. Anything after a real script path is
       left alone and passed through to the script as-is (a Khan
       script named "--version.kh" would still need to be run some
       other way; this is a deliberate, minor trade-off in favor of
       the common case working without ceremony). */
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("Khan " KHAN_VERSION "\n");
        return 0;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage(stdout);
        return 0;
    }

    char *source = read_file(argv[1]);

    /* ── Lex ── */
    Lexer lexer;
    lexer_init(&lexer, source);

    /* ── Parse ── */
    Parser parser;
    parser_init(&parser, &lexer, argv[1]);
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

    /* ── Resolve base path for imports ── */
#ifdef _WIN32
    char path_buf[2048];
    GetFullPathNameA(argv[1], (DWORD)sizeof(path_buf), path_buf, NULL);
#else
    /* realpath() requires a destination buffer of at least PATH_MAX
       bytes (glibc's fortified _chk variant enforces this exactly). */
    char path_buf[PATH_MAX];
    if (!realpath(argv[1], path_buf)) {
        /* Fall back to the raw argument if realpath fails (e.g. file
           does not exist yet or path is already relative-safe). */
        strncpy(path_buf, argv[1], sizeof(path_buf) - 1);
        path_buf[sizeof(path_buf) - 1] = '\0';
    }
#endif

    char *last_slash     = strrchr(path_buf, '/');
    char *last_backslash = strrchr(path_buf, '\\');
    char *sep = (last_slash > last_backslash) ? last_slash : last_backslash;
    if (sep) {
        *sep = '\0'; // path_buf now contains the absolute dir
        vm.base_path = strdup(path_buf);
        strncpy(vm.current_import_dir, path_buf, sizeof(vm.current_import_dir) - 1);
        vm.current_import_dir[sizeof(vm.current_import_dir) - 1] = '\0';
    } else {
        vm.base_path = strdup(".");
        strcpy(vm.current_import_dir, ".");
    }

    // printf("[DEBUG] base_path: %s, current_import_dir: %s\n", vm.base_path, vm.current_import_dir);

    /* ── Register built-ins and libraries ── */
    vm_register_builtins(&vm);
    json_register_all_vm(&vm);
    datetime_register_all_vm(&vm);
    requests_register_all_vm(&vm);
    webi_register_all_vm(&vm);
    sqlite_register_all_vm(&vm);
    vision_register_all_vm(&vm);
    vision_cv_register_all_vm(&vm);
    vision_cascade_register_all_vm(&vm);
#ifdef LLM_SUPPORT
    llm_register_all_vm(&vm);
#endif

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
