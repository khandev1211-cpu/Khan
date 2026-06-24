#ifndef KHAN_INTERPRETER_H
#define KHAN_INTERPRETER_H

#include "ast.h"

// ---------------------------------------------------------------------------
// Value types that the interpreter works with at runtime
// ---------------------------------------------------------------------------
typedef enum {
    VAL_NUMBER,
    VAL_STRING,
    VAL_BOOL,
    VAL_NIL,
    VAL_FUNCTION,
} ValueType;

typedef struct Environment Environment;

typedef struct {
    ValueType type;
    union {
        double number;
        const char *string;
        int boolean;
        struct {
            const char *name;        // function name (for error messages)
            Environment *closure;    // captured environment
            AstNode *body;           // AST_BLOCK
            AstNodeList *params;     // list of AST_IDENTIFIER nodes
        } function;
    } as;
} Value;

// ---------------------------------------------------------------------------
// Environment (scope chain)
// ---------------------------------------------------------------------------
struct Environment {
    struct EnvEntry {
        const char *name;
        Value value;
    } *entries;
    int count;
    int capacity;
    struct Environment *parent;
};

Environment *env_new(Environment *parent);
void env_free(Environment *env);
void env_define(Environment *env, const char *name, Value value);
Value *env_get(Environment *env, const char *name);
void env_assign(Environment *env, const char *name, Value value);

// ---------------------------------------------------------------------------
// Value helpers
// ---------------------------------------------------------------------------
Value value_number(double n);
Value value_string(const char *s);
Value value_bool(int b);
Value value_nil(void);
Value value_function(const char *name, Environment *closure,
                     AstNode *body, AstNodeList *params);
void value_free(Value v);
void value_print(Value v);

// ---------------------------------------------------------------------------
// Interpreter
// ---------------------------------------------------------------------------
typedef struct {
    int had_runtime_error;
} Interpreter;

void interpreter_init(Interpreter *interp);
Value interpreter_execute(Interpreter *interp, AstNode *node, Environment *env);

#endif