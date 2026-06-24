#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

// strndup is not available on all platforms (e.g. MinGW).
// Provide a simple implementation if missing.
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
static char *local_strndup(const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *copy = malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len);
        copy[len] = '\0';
    }
    return copy;
}
#define strndup local_strndup
#endif

// ---------------------------------------------------------------------------
// Error reporting
// ---------------------------------------------------------------------------
static void error_at_current(Parser *parser, const char *message) {
    fprintf(stderr, "[line %d] Error at '%s': %s\n",
            parser->current.line,
            parser->current.length > 0
                ? (char[]){0} // placeholder — print lexeme inline
                : "end",
            message);
    // Print the token lexeme manually
    fprintf(stderr, "    token: '%.*s'\n",
            parser->current.length, parser->current.start);
    parser->had_error = 1;
}

static void error(Parser *parser, const char *message) {
    fprintf(stderr, "[line %d] Error: %s\n", parser->previous.line, message);
    parser->had_error = 1;
}

// ---------------------------------------------------------------------------
// Token consumption helpers
// ---------------------------------------------------------------------------
static void advance(Parser *parser) {
    parser->previous = parser->current;
    for (;;) {
        parser->current = lexer_next_token(parser->lexer);
        if (parser->current.type != TOKEN_ERROR) break;
        // Report lexical errors but keep going
        fprintf(stderr, "[line %d] Lexical error: %.*s\n",
                parser->current.line,
                parser->current.length, parser->current.start);
        parser->had_error = 1;
    }
}

static int check(Parser *parser, TokenType type) {
    return parser->current.type == type;
}

static int match(Parser *parser, TokenType type) {
    if (!check(parser, type)) return 0;
    advance(parser);
    return 1;
}

static void consume(Parser *parser, TokenType type, const char *message) {
    if (check(parser, type)) {
        advance(parser);
        return;
    }
    error_at_current(parser, message);
}

// ---------------------------------------------------------------------------
// Forward declarations for recursive descent
// ---------------------------------------------------------------------------
static AstNode *statement(Parser *parser);
static AstNode *declaration(Parser *parser);
static AstNode *expression(Parser *parser);
static AstNode *assignment(Parser *parser);
static AstNode *logic_or(Parser *parser);
static AstNode *logic_and(Parser *parser);
static AstNode *equality(Parser *parser);
static AstNode *comparison(Parser *parser);
static AstNode *term(Parser *parser);
static AstNode *factor(Parser *parser);
static AstNode *unary(Parser *parser);
static AstNode *call(Parser *parser);
static AstNode *primary(Parser *parser);

// ---------------------------------------------------------------------------
// Parse a block: expects INDENT, statements, DEDENT
// ---------------------------------------------------------------------------
static AstNode *block(Parser *parser) {
    int line = parser->current.line;
    // The lexer emits a NEWLINE before INDENT, so skip it
    if (check(parser, TOKEN_NEWLINE)) advance(parser);
    consume(parser, TOKEN_INDENT, "Expected indented block after ':'.");

    AstNodeList *stmts = NULL;
    while (!check(parser, TOKEN_DEDENT) && !check(parser, TOKEN_EOF)) {
        stmts = ast_list_append(stmts, declaration(parser));
        // Expect a NEWLINE after each statement (except the last before DEDENT)
        if (!check(parser, TOKEN_DEDENT) && !check(parser, TOKEN_EOF)) {
            consume(parser, TOKEN_NEWLINE, "Expected newline after statement.");
        }
    }
    consume(parser, TOKEN_DEDENT, "Expected dedent to close block.");
    return ast_new_block(stmts, line);
}

// ---------------------------------------------------------------------------
// Statement parsing
// ---------------------------------------------------------------------------
static AstNode *print_statement(Parser *parser) {
    int line = parser->previous.line;
    AstNode *expr = expression(parser);
    return ast_new_print_stmt(expr, line);
}

static AstNode *let_statement(Parser *parser) {
    int line = parser->previous.line;
    consume(parser, TOKEN_IDENTIFIER, "Expected variable name after 'let'.");
    const char *name = strndup(parser->previous.start, parser->previous.length);
    AstNode *initializer = NULL;
    if (match(parser, TOKEN_EQUAL)) {
        initializer = expression(parser);
    }
    return ast_new_let_stmt(name, initializer, line);
}

static AstNode *if_statement(Parser *parser) {
    int line = parser->previous.line;
    AstNode *cond = expression(parser);
    consume(parser, TOKEN_COLON, "Expected ':' after if condition.");
    AstNode *then_branch = block(parser);

    AstNode *else_branch = NULL;
    if (match(parser, TOKEN_ELSE)) {
        consume(parser, TOKEN_COLON, "Expected ':' after else.");
        else_branch = block(parser);
    }
    return ast_new_if_stmt(cond, then_branch, else_branch, line);
}

static AstNode *while_statement(Parser *parser) {
    int line = parser->previous.line;
    AstNode *cond = expression(parser);
    consume(parser, TOKEN_COLON, "Expected ':' after while condition.");
    AstNode *body = block(parser);
    return ast_new_while_stmt(cond, body, line);
}

static AstNode *fn_declaration(Parser *parser) {
    int line = parser->previous.line;
    consume(parser, TOKEN_IDENTIFIER, "Expected function name after 'fn'.");
    const char *name = strndup(parser->previous.start, parser->previous.length);

    AstNodeList *params = NULL;
    if (match(parser, TOKEN_LPAREN)) {
        // Parameter list
        if (!check(parser, TOKEN_RPAREN)) {
            do {
                consume(parser, TOKEN_IDENTIFIER, "Expected parameter name.");
                params = ast_list_append(params,
                    ast_new_identifier(
                        strndup(parser->previous.start, parser->previous.length),
                        parser->previous.line));
            } while (match(parser, TOKEN_COMMA));
        }
        consume(parser, TOKEN_RPAREN, "Expected ')' after parameters.");
    }

    consume(parser, TOKEN_COLON, "Expected ':' after function declaration.");
    AstNode *body = block(parser);
    return ast_new_fn_decl(name, params, body, line);
}

static AstNode *return_statement(Parser *parser) {
    int line = parser->previous.line;
    AstNode *value = expression(parser);
    return ast_new_return_stmt(value, line);
}

// ---------------------------------------------------------------------------
// Top-level declaration dispatcher
// ---------------------------------------------------------------------------
static AstNode *declaration(Parser *parser) {
    if (match(parser, TOKEN_LET))    return let_statement(parser);
    if (match(parser, TOKEN_FN))     return fn_declaration(parser);

    // Otherwise it's a statement
    return statement(parser);
}

static AstNode *statement(Parser *parser) {
    if (match(parser, TOKEN_PRINT))  return print_statement(parser);
    if (match(parser, TOKEN_IF))     return if_statement(parser);
    if (match(parser, TOKEN_WHILE))  return while_statement(parser);
    if (match(parser, TOKEN_RETURN)) return return_statement(parser);

    // Expression statement
    int line = parser->current.line;
    AstNode *expr = expression(parser);
    return ast_new_expr_stmt(expr, line);
}

// ---------------------------------------------------------------------------
// Expression parsing (Pratt / precedence climbing)
// ---------------------------------------------------------------------------
static AstNode *expression(Parser *parser) {
    return assignment(parser);
}

static AstNode *assignment(Parser *parser) {
    AstNode *left = logic_or(parser);

    if (match(parser, TOKEN_EQUAL)) {
        if (left->type != AST_IDENTIFIER) {
            error(parser, "Left-hand side of assignment must be a variable.");
            // Discard the right-hand side
            expression(parser);
            return left;
        }
        AstNode *value = assignment(parser);
        // Re-purpose as assignment node
        AstNode *assign = ast_new_assignment(left->data.name, value, left->line);
        ast_free(left);  // free the old identifier node — but keep the name strdup'd
        return assign;
    }
    return left;
}

static AstNode *logic_or(Parser *parser) {
    AstNode *left = logic_and(parser);

    while (match(parser, TOKEN_OR)) {
        AstOp op = OP_OR;
        AstNode *right = logic_and(parser);
        left = ast_new_binary(left, op, right, parser->previous.line);
    }
    return left;
}

static AstNode *logic_and(Parser *parser) {
    AstNode *left = equality(parser);

    while (match(parser, TOKEN_AND)) {
        AstOp op = OP_AND;
        AstNode *right = equality(parser);
        left = ast_new_binary(left, op, right, parser->previous.line);
    }
    return left;
}

static AstNode *equality(Parser *parser) {
    AstNode *left = comparison(parser);

    while (match(parser, TOKEN_EQUAL_EQUAL) || match(parser, TOKEN_BANG_EQUAL)) {
        AstOp op = parser->previous.type == TOKEN_EQUAL_EQUAL ? OP_EQUAL_EQUAL : OP_BANG_EQUAL;
        AstNode *right = comparison(parser);
        left = ast_new_binary(left, op, right, parser->previous.line);
    }
    return left;
}

static AstNode *comparison(Parser *parser) {
    AstNode *left = term(parser);

    for (;;) {
        AstOp op;
        if (match(parser, TOKEN_LESS))           op = OP_LESS;
        else if (match(parser, TOKEN_LESS_EQUAL)) op = OP_LESS_EQUAL;
        else if (match(parser, TOKEN_GREATER))    op = OP_GREATER;
        else if (match(parser, TOKEN_GREATER_EQUAL)) op = OP_GREATER_EQUAL;
        else break;

        AstNode *right = term(parser);
        left = ast_new_binary(left, op, right, parser->previous.line);
    }
    return left;
}

static AstNode *term(Parser *parser) {
    AstNode *left = factor(parser);

    while (match(parser, TOKEN_PLUS) || match(parser, TOKEN_MINUS)) {
        AstOp op = parser->previous.type == TOKEN_PLUS ? OP_PLUS : OP_MINUS;
        AstNode *right = factor(parser);
        left = ast_new_binary(left, op, right, parser->previous.line);
    }
    return left;
}

static AstNode *factor(Parser *parser) {
    AstNode *left = unary(parser);

    while (match(parser, TOKEN_STAR) || match(parser, TOKEN_SLASH) || match(parser, TOKEN_PERCENT)) {
        AstOp op;
        if (parser->previous.type == TOKEN_STAR)    op = OP_STAR;
        else if (parser->previous.type == TOKEN_SLASH) op = OP_SLASH;
        else op = OP_PERCENT;
        AstNode *right = unary(parser);
        left = ast_new_binary(left, op, right, parser->previous.line);
    }
    return left;
}

static AstNode *unary(Parser *parser) {
    if (match(parser, TOKEN_MINUS)) {
        return ast_new_unary(OP_NEGATE, unary(parser), parser->previous.line);
    }
    if (match(parser, TOKEN_NOT)) {
        return ast_new_unary(OP_NOT, unary(parser), parser->previous.line);
    }
    return call(parser);
}

// ---------------------------------------------------------------------------
// Call and primary
// ---------------------------------------------------------------------------
static AstNode *finish_call(Parser *parser, AstNode *callee) {
    // callee must be an identifier
    if (callee->type != AST_IDENTIFIER) {
        error(parser, "Can only call functions.");
        // Skip to matching ')'
        while (!check(parser, TOKEN_RPAREN) && !check(parser, TOKEN_EOF)) advance(parser);
        if (check(parser, TOKEN_RPAREN)) advance(parser);
        return callee;
    }

    const char *callee_name = callee->data.name;
    AstNodeList *args = NULL;

    if (!check(parser, TOKEN_RPAREN)) {
        do {
            args = ast_list_append(args, expression(parser));
        } while (match(parser, TOKEN_COMMA));
    }
    consume(parser, TOKEN_RPAREN, "Expected ')' after arguments.");
    ast_free(callee);  // Don't need the identifier node anymore
    return ast_new_call(callee_name, args, parser->previous.line);
}

static AstNode *call(Parser *parser) {
    AstNode *left = primary(parser);

    for (;;) {
        if (match(parser, TOKEN_LPAREN)) {
            left = finish_call(parser, left);
        } else {
            break;
        }
    }
    return left;
}

static AstNode *primary(Parser *parser) {
    if (match(parser, TOKEN_NUMBER)) {
        char *end;
        double value = strtod(parser->previous.start, &end);
        return ast_new_number(value, parser->previous.line);
    }

    if (match(parser, TOKEN_STRING)) {
        // Strip surrounding quotes
        const char *raw = parser->previous.start;
        int len = parser->previous.length;
        char *str = malloc(len - 1);  // -2 for quotes +1 for null = len-1
        strncpy(str, raw + 1, len - 2);
        str[len - 2] = '\0';
        AstNode *node = ast_new_string(str, parser->previous.line);
        free(str);
        return node;
    }

    if (match(parser, TOKEN_IDENTIFIER)) {
        const char *name = strndup(parser->previous.start, parser->previous.length);
        return ast_new_identifier(name, parser->previous.line);
    }

    if (match(parser, TOKEN_TRUE)) {
        return ast_new_number(1.0, parser->previous.line);
    }

    if (match(parser, TOKEN_FALSE)) {
        return ast_new_number(0.0, parser->previous.line);
    }

    if (match(parser, TOKEN_LPAREN)) {
        AstNode *expr = expression(parser);
        consume(parser, TOKEN_RPAREN, "Expected ')' after expression.");
        return ast_new_grouping(expr, parser->previous.line);
    }

    // Error: unexpected token
    error_at_current(parser, "Expected expression.");
    // Advance past it to avoid infinite loops
    advance(parser);
    return ast_new_number(0.0, parser->previous.line); // dummy
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void parser_init(Parser *parser, Lexer *lexer) {
    parser->lexer = lexer;
    parser->had_error = 0;
    parser->current = (Token){0};
    parser->previous = (Token){0};
    // Prime the token stream
    advance(parser);
}

AstNode *parser_parse(Parser *parser) {
    AstNodeList *stmts = NULL;

    // Skip leading NEWLINE tokens (blank lines at top level)
    while (match(parser, TOKEN_NEWLINE)) {}

    while (!check(parser, TOKEN_EOF)) {
        stmts = ast_list_append(stmts, declaration(parser));

        // Skip newlines between top-level statements (blank lines are
        // transparent to the lexer, so NEWLINE may or may not be present)
        while (match(parser, TOKEN_NEWLINE)) {}
    }

    int line = 1;
    return ast_new_program(stmts, line);
}