// ===========================================================================
// main.c — Khan interpreter entry point
//
// Uses the tree-walk interpreter (not the VM) so that native C libraries
// (json, datetime, requests, webi) work correctly via the Environment API.
//
// Supports:
//   khan script.kh
//
// Import syntax in .kh files:
//   import "webi"          → loads packages/webi/webi.kh
//   import "requests"      → loads packages/requests/requests.kh
//   import "json"          → built-in (json_encode / json_decode always available)
//   import "strings"       → loads packages/strings/strings.kh
//   import "math"          → loads packages/math/math.kh
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
#include "interpreter.h"
#include "khan_stdlib.h"
#include "json_lib.h"
#include "datetime_lib.h"
#include "requests_lib.h"
#include "webi_lib.h"

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
        fprintf(stderr, "Parse completed with errors.\n");
        ast_free(program);
        free(source);
        return 65;
    }

    /* ── Resolve base path for imports ── */
    const char *base_path = NULL;
    char path_buf[1024];
    char *last_slash     = strrchr(argv[1], '/');
    char *last_backslash = strrchr(argv[1], '\\');
    char *sep = (last_slash > last_backslash) ? last_slash : last_backslash;
    if (sep) {
        int len = (int)(sep - argv[1]);
        memcpy(path_buf, argv[1], len);
        path_buf[len] = '\0';
        base_path = path_buf;
    }

    /* ── Set up interpreter + global environment ── */
    Interpreter interp;
    interpreter_init(&interp, base_path);

    Environment *global = env_new(NULL);

    /* Register all native libraries into the global env.
       Order matters: stdlib first (basic types), then the rest. */
    stdlib_register_all(global);       /* len, str, push, range, split, …  */
    json_register_all(global);         /* json_encode, json_decode           */
    datetime_register_all(global);     /* now(), date_format(), …            */
    requests_register_all(global);     /* http_get/post/put/delete, …        */
    webi_register_all(global);         /* http_serve() + extended http_*_h   */

    /* ── Set base_env so webi_lib can call back into Khan ── */
    interp.base_env = global;

    /* ── Register argv ── */
    Value argv_val = value_array(NULL, 0);
    for (int i = 2; i < argc; i++) {
        argv_val.as.array.items = realloc(argv_val.as.array.items, sizeof(Value) * (argv_val.as.array.count + 1));
        argv_val.as.array.items[argv_val.as.array.count++] = value_string(argv[i]);
    }
    env_define(global, "argv", argv_val);

    /* ── Execute ── */
    interpreter_execute(&interp, program, global);

    int exit_code = interp.had_runtime_error ? 70 : 0;

    env_free(global);
    /* NOTE: program and source are intentionally NOT freed here.
       Imported-function AST nodes hold raw pointers into these buffers. */

    return exit_code;
}