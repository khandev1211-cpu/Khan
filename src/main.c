#include <stdio.h>
#include <stdlib.h>
#include "lexer.h"

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

    for (;;) {
        Token token = lexer_next_token(&lexer);
        printf("%-4d %-15s '%.*s'\n", token.line, token_type_name(token.type),
               token.length, token.start);
        if (token.type == TOKEN_EOF) break;
    }

    free(source);
    return 0;
}