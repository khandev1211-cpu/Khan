#ifndef KHAN_LEXER_H
#define KHAN_LEXER_H

#include "token.h"

typedef struct {
    const char *start;
    const char *current;
    int line;

    // Indentation tracking
    int indent_stack[64];
    int indent_top;
    int at_line_start;
    int pending_dedents;
} Lexer;

void lexer_init(Lexer *lexer, const char *source);
Token lexer_next_token(Lexer *lexer);
const char *token_type_name(TokenKind type);

#endif