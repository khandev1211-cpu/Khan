#ifndef KHAN_PARSER_H
#define KHAN_PARSER_H

#include "token.h"
#include "ast.h"
#include "lexer.h"

typedef struct {
    Lexer *lexer;
    Token current;
    Token previous;
    int had_error;
} Parser;

// Parse the entire source into a program AST.
AstNode *parser_parse(Parser *parser);

// Initialise the parser with a lexer.
void parser_init(Parser *parser, Lexer *lexer);

#endif