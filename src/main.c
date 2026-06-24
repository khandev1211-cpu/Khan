#include <stdio.h>
#include <stdlib.h>
#include "lexer.h"
#include "parser.h"
#include "ast.h"

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
        ast_print(program, 0);
        ast_free(program);
        free(source);
        return 65;
    }

    // Print the AST so we can see the structure
    ast_print(program, 0);

    ast_free(program);
    free(source);
    return 0;
}
