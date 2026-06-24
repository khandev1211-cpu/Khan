#include <stdio.h>
#include <stdlib.h>
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "interpreter.h"

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

    // Execute the program
    Interpreter interp;
    interpreter_init(&interp);

    Environment *global = env_new(NULL);
    interpreter_execute(&interp, program, global);

    int exit_code = interp.had_runtime_error ? 70 : 0;

    env_free(global);
    ast_free(program);
    free(source);
    return exit_code;
}
