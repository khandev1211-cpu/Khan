#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "lexer.h"

void lexer_init(Lexer *lexer, const char *source) {
    lexer->start = source;
    lexer->current = source;
    lexer->line = 1;
    lexer->indent_stack[0] = 0;
    lexer->indent_top = 0;
    lexer->at_line_start = 1;
    lexer->pending_dedents = 0;
}

static int is_at_end(Lexer *lexer) {
    return *lexer->current == '\0';
}

static char advance(Lexer *lexer) {
    lexer->current++;
    return lexer->current[-1];
}

static char peek(Lexer *lexer) {
    return *lexer->current;
}

static char peek_next(Lexer *lexer) {
    if (is_at_end(lexer)) return '\0';
    return lexer->current[1];
}

static int match(Lexer *lexer, char expected) {
    if (is_at_end(lexer)) return 0;
    if (*lexer->current != expected) return 0;
    lexer->current++;
    return 1;
}

static Token make_token(Lexer *lexer, TokenType type) {
    Token token;
    token.type = type;
    token.start = lexer->start;
    token.length = (int)(lexer->current - lexer->start);
    token.line = lexer->line;
    return token;
}

static Token error_token(Lexer *lexer, const char *message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = lexer->line;
    return token;
}

// Skips spaces/tabs within a line (not newlines) and comments.
static void skip_inline_whitespace(Lexer *lexer) {
    for (;;) {
        char c = peek(lexer);
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(lexer);
        } else if (c == '#') {
            while (peek(lexer) != '\n' && !is_at_end(lexer)) advance(lexer);
        } else {
            break;
        }
    }
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static TokenType keyword_type(const char *start, int length) {
    struct { const char *word; TokenType type; } keywords[] = {
        {"let", TOKEN_LET}, {"fn", TOKEN_FN}, {"print", TOKEN_PRINT},
        {"import", TOKEN_IMPORT}, {"if", TOKEN_IF}, {"else", TOKEN_ELSE},
        {"while", TOKEN_WHILE}, {"return", TOKEN_RETURN},
        {"true", TOKEN_TRUE}, {"false", TOKEN_FALSE},
        {"and", TOKEN_AND}, {"or", TOKEN_OR}, {"not", TOKEN_NOT},
    };
    int n = sizeof(keywords) / sizeof(keywords[0]);
    for (int i = 0; i < n; i++) {
        size_t klen = strlen(keywords[i].word);
        if ((int)klen == length && memcmp(keywords[i].word, start, length) == 0) {
            return keywords[i].type;
        }
    }
    return TOKEN_IDENTIFIER;
}

static Token identifier(Lexer *lexer) {
    while (is_alpha(peek(lexer)) || is_digit(peek(lexer))) advance(lexer);
    int length = (int)(lexer->current - lexer->start);
    TokenType type = keyword_type(lexer->start, length);
    return make_token(lexer, type);
}

static Token number(Lexer *lexer) {
    while (is_digit(peek(lexer))) advance(lexer);
    if (peek(lexer) == '.' && is_digit(peek_next(lexer))) {
        advance(lexer);
        while (is_digit(peek(lexer))) advance(lexer);
    }
    return make_token(lexer, TOKEN_NUMBER);
}

static Token string_literal(Lexer *lexer) {
    while (peek(lexer) != '"' && !is_at_end(lexer)) {
        if (peek(lexer) == '\n') lexer->line++;
        advance(lexer);
    }
    if (is_at_end(lexer)) return error_token(lexer, "Unterminated string.");
    advance(lexer); // closing quote
    return make_token(lexer, TOKEN_STRING);
}

// Handles start-of-line indentation: measures spaces, compares to indent
// stack, and queues INDENT/DEDENT tokens as needed. Returns 1 if a
// structural token was produced (caller should return it immediately).
static int handle_indentation(Lexer *lexer, Token *out) {
    // Skip fully blank or comment-only lines without affecting indentation.
    for (;;) {
        const char *line_start = lexer->current;
        int spaces = 0;
        while (peek(lexer) == ' ') { spaces++; advance(lexer); }
        if (peek(lexer) == '\n' || peek(lexer) == '\0' || peek(lexer) == '#') {
            if (peek(lexer) == '#') {
                while (peek(lexer) != '\n' && !is_at_end(lexer)) advance(lexer);
            }
            if (peek(lexer) == '\n') {
                advance(lexer);
                lexer->line++;
                continue; // blank/comment line, keep scanning
            }
            if (is_at_end(lexer)) {
                // EOF: emit remaining dedents
                if (lexer->indent_top > 0) {
                    lexer->indent_top--;
                    *out = make_token(lexer, TOKEN_DEDENT);
                    return 1;
                }
                return 0;
            }
        }
        (void)line_start;
        // Real code line found at this indentation level.
        if (spaces > lexer->indent_stack[lexer->indent_top]) {
            lexer->indent_top++;
            lexer->indent_stack[lexer->indent_top] = spaces;
            *out = make_token(lexer, TOKEN_INDENT);
            return 1;
        }
        if (spaces < lexer->indent_stack[lexer->indent_top]) {
            lexer->indent_top--;
            *out = make_token(lexer, TOKEN_DEDENT);
            return 1;
        }
        return 0; // same indentation, nothing structural to emit
    }
}

Token lexer_next_token(Lexer *lexer) {
    if (lexer->at_line_start) {
        Token structural;
        if (handle_indentation(lexer, &structural)) {
            lexer->start = lexer->current;
            return structural;
        }
        lexer->at_line_start = 0;
    }

    skip_inline_whitespace(lexer);
    lexer->start = lexer->current;

    if (is_at_end(lexer)) {
        if (lexer->indent_top > 0) {
            lexer->indent_top--;
            return make_token(lexer, TOKEN_DEDENT);
        }
        return make_token(lexer, TOKEN_EOF);
    }

    char c = advance(lexer);

    if (c == '\n') {
        lexer->line++;
        lexer->at_line_start = 1;
        return make_token(lexer, TOKEN_NEWLINE);
    }

    if (is_alpha(c)) return identifier(lexer);
    if (is_digit(c)) return number(lexer);

    switch (c) {
        case '"': return string_literal(lexer);
        case '(': return make_token(lexer, TOKEN_LPAREN);
        case ')': return make_token(lexer, TOKEN_RPAREN);
        case ':': return make_token(lexer, TOKEN_COLON);
        case ',': return make_token(lexer, TOKEN_COMMA);
        case '.': return make_token(lexer, TOKEN_DOT);
        case '+': return make_token(lexer, TOKEN_PLUS);
        case '-': return make_token(lexer, TOKEN_MINUS);
        case '*': return make_token(lexer, TOKEN_STAR);
        case '/': return make_token(lexer, TOKEN_SLASH);
        case '%': return make_token(lexer, TOKEN_PERCENT);
        case '=':
            return make_token(lexer, match(lexer, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '!':
            if (match(lexer, '=')) return make_token(lexer, TOKEN_BANG_EQUAL);
            return error_token(lexer, "Unexpected '!'.");
        case '<':
            return make_token(lexer, match(lexer, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            return make_token(lexer, match(lexer, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
    }

    return error_token(lexer, "Unexpected character.");
}

const char *token_type_name(TokenType type) {
    switch (type) {
        case TOKEN_IDENTIFIER: return "IDENTIFIER";
        case TOKEN_NUMBER: return "NUMBER";
        case TOKEN_STRING: return "STRING";
        case TOKEN_LET: return "LET";
        case TOKEN_FN: return "FN";
        case TOKEN_PRINT: return "PRINT";
        case TOKEN_IMPORT: return "IMPORT";
        case TOKEN_IF: return "IF";
        case TOKEN_ELSE: return "ELSE";
        case TOKEN_WHILE: return "WHILE";
        case TOKEN_RETURN: return "RETURN";
        case TOKEN_TRUE: return "TRUE";
        case TOKEN_FALSE: return "FALSE";
        case TOKEN_AND: return "AND";
        case TOKEN_OR: return "OR";
        case TOKEN_NOT: return "NOT";
        case TOKEN_PLUS: return "PLUS";
        case TOKEN_MINUS: return "MINUS";
        case TOKEN_STAR: return "STAR";
        case TOKEN_SLASH: return "SLASH";
        case TOKEN_PERCENT: return "PERCENT";
        case TOKEN_EQUAL: return "EQUAL";
        case TOKEN_EQUAL_EQUAL: return "EQUAL_EQUAL";
        case TOKEN_BANG_EQUAL: return "BANG_EQUAL";
        case TOKEN_LESS: return "LESS";
        case TOKEN_LESS_EQUAL: return "LESS_EQUAL";
        case TOKEN_GREATER: return "GREATER";
        case TOKEN_GREATER_EQUAL: return "GREATER_EQUAL";
        case TOKEN_LPAREN: return "LPAREN";
        case TOKEN_RPAREN: return "RPAREN";
        case TOKEN_COLON: return "COLON";
        case TOKEN_COMMA: return "COMMA";
        case TOKEN_DOT: return "DOT";
        case TOKEN_NEWLINE: return "NEWLINE";
        case TOKEN_INDENT: return "INDENT";
        case TOKEN_DEDENT: return "DEDENT";
        case TOKEN_EOF: return "EOF";
        case TOKEN_ERROR: return "ERROR";
    }
    return "UNKNOWN";
}