#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "interpreter.h"
#include "stdlib.h"
#include "json_lib.h"
#include "datetime_lib.h"
#include "requests_lib.h"

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
    if (!buffer) {
        fprintf(stderr, "Out of memory.\n");
        exit(74);
    }
    fread(buffer, 1, size, file);
    buffer[size] = '\0';

    fclose(file);
    return buffer;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: khan <script.kh>\n");
        return 64;
    }

    char *source = read_file(argv[1]);

    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    AstNode *program = parser_parse(&parser);

    if (parser.had_error) {
        fprintf(stderr, "Parse completed with errors.\n");
        ast_free(program);
        free(source);
        return 65;
    }

    // Extract the directory path for import resolution
    const char *base_path = NULL;
    char path_buf[1024];
    char *last_slash = strrchr(argv[1], '/');
    char *last_backslash = strrchr(argv[1], '\\');
    char *sep = last_slash > last_backslash ? last_slash : last_backslash;
    if (sep) {
        int len = (int)(sep - argv[1]);
        memcpy(path_buf, argv[1], len);
        path_buf[len] = '\0';
        base_path = path_buf;
    }

    // Set up global environment with standard library
    Interpreter interp;
    interpreter_init(&interp, base_path);

    Environment *global = env_new(NULL);
    stdlib_register_all(global);
    json_register_all(global);
    datetime_register_all(global);
    requests_register_all(global);

    // Execute the program
    interpreter_execute(&interp, program, global);

    int exit_code = interp.had_runtime_error ? 70 : 0;

    env_free(global);
    ast_free(program);
    free(source);
    return exit_code;
}
