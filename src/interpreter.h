#ifndef KHAN_INTERPRETER_H
#define KHAN_INTERPRETER_H

#include "ast.h"

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
typedef struct Environment Environment;
typedef struct Interpreter Interpreter;

// ---------------------------------------------------------------------------
// A native C function: takes interpreter, arg count, arg array, returns Value
// We use a struct wrapper to break the circular dependency between
// NativeFn's return type (Value) and Value's member (NativeFn pointer).
// ---------------------------------------------------------------------------
// Value types that the interpreter works with at runtime
typedef enum {
    VAL_NUMBER,
    VAL_STRING,
    VAL_BOOL,
    VAL_NIL,
    VAL_FUNCTION,
    VAL_NATIVE,
} ValueType;

// Forward-declare Value struct for the native function pointer
struct Value;

// Native function signature
typedef void (*NativeFn)(struct Value *result, Interpreter *interp, int argc, struct Value *args);

// Now define Value
struct Value {
    ValueType type;
    union {
        double number;
        const char *string;
        int boolean;
        struct {
            const char *name;
            Environment *closure;
            AstNode *body;
            AstNodeList *params;
        } function;
        struct {
            const char *name;
            NativeFn function;
        } native;
    } as;
};

// Typedef for convenience
typedef struct Value Value;

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
Value value_native(const char *name, NativeFn fn);
void value_free(Value v);
void value_print(Value v);

// ---------------------------------------------------------------------------
// Interpreter
// ---------------------------------------------------------------------------
struct Interpreter {
    int had_runtime_error;
};

void interpreter_init(Interpreter *interp);
Value interpreter_execute(Interpreter *interp, AstNode *node, Environment *env);

#endif