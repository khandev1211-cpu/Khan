#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "interpreter.h"

// ---------------------------------------------------------------------------
// Runtime error reporting
// ---------------------------------------------------------------------------
static void runtime_error(Interpreter *interp, AstNode *node, const char *msg) {
    fprintf(stderr, "[line %d] Runtime error: %s\n", node->line, msg);
    interp->had_runtime_error = 1;
}

// ---------------------------------------------------------------------------
// Environment implementation
// ---------------------------------------------------------------------------
static void env_grow(Environment *env) {
    if (env->count < env->capacity) return;
    env->capacity = env->capacity ? env->capacity * 2 : 8;
    env->entries = realloc(env->entries, sizeof(env->entries[0]) * env->capacity);
}

Environment *env_new(Environment *parent) {
    Environment *env = malloc(sizeof(Environment));
    env->entries = NULL;
    env->count = 0;
    env->capacity = 0;
    env->parent = parent;
    return env;
}

void env_free(Environment *env) {
    if (!env) return;
    for (int i = 0; i < env->count; i++) {
        free((void *)env->entries[i].name);
        value_free(env->entries[i].value);
    }
    free(env->entries);
    free(env);
}

void env_define(Environment *env, const char *name, Value value) {
    env_grow(env);
    env->entries[env->count].name = strdup(name);
    env->entries[env->count].value = value;
    env->count++;
}

Value *env_get(Environment *env, const char *name) {
    for (Environment *e = env; e; e = e->parent) {
        for (int i = 0; i < e->count; i++) {
            if (strcmp(e->entries[i].name, name) == 0) {
                return &e->entries[i].value;
            }
        }
    }
    return NULL;
}

void env_assign(Environment *env, const char *name, Value value) {
    for (Environment *e = env; e; e = e->parent) {
        for (int i = 0; i < e->count; i++) {
            if (strcmp(e->entries[i].name, name) == 0) {
                value_free(e->entries[i].value);
                e->entries[i].value = value;
                return;
            }
        }
    }
    // If not found, define in current scope
    env_define(env, name, value);
}

// ---------------------------------------------------------------------------
// Value construction / destruction
// ---------------------------------------------------------------------------
Value value_number(double n) {
    Value v;
    v.type = VAL_NUMBER;
    v.as.number = n;
    return v;
}

Value value_string(const char *s) {
    Value v;
    v.type = VAL_STRING;
    v.as.string = strdup(s);
    return v;
}

Value value_bool(int b) {
    Value v;
    v.type = VAL_BOOL;
    v.as.boolean = b;
    return v;
}

Value value_nil(void) {
    Value v;
    v.type = VAL_NIL;
    v.as.number = 0;
    return v;
}

Value value_function(const char *name, Environment *closure,
                     AstNode *body, AstNodeList *params) {
    Value v;
    v.type = VAL_FUNCTION;
    v.as.function.name = strdup(name);
    v.as.function.closure = closure;
    v.as.function.body = body;
    v.as.function.params = params;
    return v;
}

void value_free(Value v) {
    if (v.type == VAL_STRING) {
        free((void *)v.as.string);
    } else if (v.type == VAL_FUNCTION) {
        free((void *)v.as.function.name);
        // Don't free closure/body/params — they're owned by the AST
    }
}

void value_print(Value v) {
    switch (v.type) {
        case VAL_NUMBER:
            // Print integer-like numbers without decimal
            if (v.as.number == (long long)v.as.number) {
                printf("%lld", (long long)v.as.number);
            } else {
                printf("%g", v.as.number);
            }
            break;
        case VAL_STRING:
            printf("%s", v.as.string);
            break;
        case VAL_BOOL:
            printf(v.as.boolean ? "true" : "false");
            break;
        case VAL_NIL:
            printf("nil");
            break;
        case VAL_FUNCTION:
            printf("<fn %s>", v.as.function.name);
            break;
    }
}

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static Value evaluate(Interpreter *interp, AstNode *node, Environment *env);
static Value execute_block(Interpreter *interp, AstNodeList *stmts,
                           Environment *env);

// ---------------------------------------------------------------------------
// Expression evaluation
// ---------------------------------------------------------------------------
static Value evaluate(Interpreter *interp, AstNode *node, Environment *env) {
    if (interp->had_runtime_error) return value_nil();

    switch (node->type) {
        case AST_NUMBER:
            return value_number(node->data.number_value);

        case AST_STRING:
            return value_string(node->data.string_value);

        case AST_IDENTIFIER: {
            Value *v = env_get(env, node->data.name);
            if (!v) {
                runtime_error(interp, node, "Undefined variable.");
                return value_nil();
            }
            return *v;
        }

        case AST_BINARY: {
            Value left = evaluate(interp, node->data.binary.left, env);
            if (interp->had_runtime_error) return value_nil();
            Value right = evaluate(interp, node->data.binary.right, env);
            if (interp->had_runtime_error) return value_nil();

            // Number-only arithmetic operators
            if (left.type == VAL_NUMBER && right.type == VAL_NUMBER) {
                double a = left.as.number;
                double b = right.as.number;
                switch (node->data.binary.op) {
                    case OP_PLUS:           return value_number(a + b);
                    case OP_MINUS:          return value_number(a - b);
                    case OP_STAR:           return value_number(a * b);
                    case OP_SLASH:
                        if (b == 0) {
                            runtime_error(interp, node, "Division by zero.");
                            return value_nil();
                        }
                        return value_number(a / b);
                    case OP_PERCENT:
                        if (b == 0) {
                            runtime_error(interp, node, "Division by zero.");
                            return value_nil();
                        }
                        return value_number(fmod(a, b));
                    case OP_LESS:           return value_bool(a < b);
                    case OP_LESS_EQUAL:     return value_bool(a <= b);
                    case OP_GREATER:        return value_bool(a > b);
                    case OP_GREATER_EQUAL:  return value_bool(a >= b);
                    case OP_EQUAL_EQUAL:    return value_bool(a == b);
                    case OP_BANG_EQUAL:     return value_bool(a != b);
                    default: break;
                }
            }

            // String concatenation with '+'
            if (left.type == VAL_STRING && right.type == VAL_STRING &&
                node->data.binary.op == OP_PLUS) {
                char *result = malloc(strlen(left.as.string) +
                                      strlen(right.as.string) + 1);
                strcpy(result, left.as.string);
                strcat(result, right.as.string);
                Value v = value_string(result);
                free(result);
                return v;
            }

            // Equality can compare any two values of the same type
            if (node->data.binary.op == OP_EQUAL_EQUAL ||
                node->data.binary.op == OP_BANG_EQUAL) {
                int eq = 0;
                if (left.type == right.type) {
                    switch (left.type) {
                        case VAL_NUMBER:
                            eq = left.as.number == right.as.number;
                            break;
                        case VAL_STRING:
                            eq = strcmp(left.as.string, right.as.string) == 0;
                            break;
                        case VAL_BOOL:
                            eq = left.as.boolean == right.as.boolean;
                            break;
                        case VAL_NIL:
                            eq = 1;
                            break;
                        default:
                            eq = 0;
                            break;
                    }
                }
                return value_bool(node->data.binary.op == OP_EQUAL_EQUAL ? eq : !eq);
            }

            // Logical operators — work on booleans
            if (node->data.binary.op == OP_AND || node->data.binary.op == OP_OR) {
                int lb = (left.type == VAL_BOOL) ? left.as.boolean :
                         (left.type == VAL_NUMBER && left.as.number != 0);
                int rb = (right.type == VAL_BOOL) ? right.as.boolean :
                         (right.type == VAL_NUMBER && right.as.number != 0);

                if (node->data.binary.op == OP_AND) return value_bool(lb && rb);
                else return value_bool(lb || rb);
            }

            runtime_error(interp, node, "Type mismatch in binary expression.");
            return value_nil();
        }

        case AST_UNARY: {
            Value right = evaluate(interp, node->data.unary.right, env);
            if (interp->had_runtime_error) return value_nil();

            if (node->data.unary.op == OP_NEGATE) {
                if (right.type != VAL_NUMBER) {
                    runtime_error(interp, node, "Negation requires a number.");
                    return value_nil();
                }
                return value_number(-right.as.number);
            }

            if (node->data.unary.op == OP_NOT) {
                int truthy = (right.type == VAL_NUMBER && right.as.number != 0) ||
                             (right.type == VAL_BOOL && right.as.boolean) ||
                             (right.type == VAL_STRING && strlen(right.as.string) > 0);
                return value_bool(!truthy);
            }

            runtime_error(interp, node, "Unknown unary operator.");
            return value_nil();
        }

        case AST_GROUPING:
            return evaluate(interp, node->data.grouping, env);

        case AST_ASSIGNMENT: {
            Value v = evaluate(interp, node->data.assignment.value, env);
            if (!interp->had_runtime_error) {
                env_assign(env, node->data.assignment.var_name, v);
            }
            return v;
        }

        case AST_CALL: {
            // Look up the function
            Value *callee = env_get(env, node->data.call.callee);
            if (!callee) {
                runtime_error(interp, node, "Undefined function.");
                return value_nil();
            }
            if (callee->type != VAL_FUNCTION) {
                runtime_error(interp, node, "Can only call functions.");
                return value_nil();
            }

            // Evaluate arguments
            int argc = 0;
            for (AstNodeList *a = node->data.call.arguments; a; a = a->next)
                argc++;

            Value *argv = malloc(sizeof(Value) * argc);
            int i = 0;
            for (AstNodeList *a = node->data.call.arguments; a; a = a->next) {
                argv[i] = evaluate(interp, a->node, env);
                if (interp->had_runtime_error) {
                    free(argv);
                    return value_nil();
                }
                i++;
            }

            // Check arity
            int param_count = 0;
            for (AstNodeList *p = callee->as.function.params; p; p = p->next)
                param_count++;

            if (argc != param_count) {
                runtime_error(interp, node, "Argument count mismatch.");
                free(argv);
                return value_nil();
            }

            // Create function call environment
            Environment *call_env = env_new(callee->as.function.closure);

            // Bind parameters
            AstNodeList *p = callee->as.function.params;
            for (int j = 0; j < argc; j++, p = p->next) {
                env_define(call_env, p->node->data.name, argv[j]);
            }
            free(argv);

            // Execute function body
            Value result = execute_block(interp,
                callee->as.function.body->data.statements, call_env);

            env_free(call_env);
            return result;
        }

        default:
            runtime_error(interp, node, "Unexpected node type in expression.");
            return value_nil();
    }
}

// ---------------------------------------------------------------------------
// Statement execution
// ---------------------------------------------------------------------------
static Value execute_block(Interpreter *interp, AstNodeList *stmts,
                           Environment *env) {
    Value result = value_nil();
    for (AstNodeList *cur = stmts; cur && !interp->had_runtime_error;
         cur = cur->next) {
        result = interpreter_execute(interp, cur->node, env);
    }
    return result;
}

Value interpreter_execute(Interpreter *interp, AstNode *node, Environment *env) {
    if (interp->had_runtime_error) return value_nil();

    switch (node->type) {
        case AST_PROGRAM:
            return execute_block(interp, node->data.program_stmts, env);

        case AST_BLOCK:
            return execute_block(interp, node->data.statements, env);

        case AST_EXPR_STMT:
            return evaluate(interp, node->data.expr, env);

        case AST_PRINT_STMT: {
            Value v = evaluate(interp, node->data.expr, env);
            if (!interp->had_runtime_error) {
                value_print(v);
                printf("\n");
            }
            return value_nil();
        }

        case AST_LET_STMT: {
            const char *name = node->data.let_decl.let_name;
            Value initializer = value_nil();
            if (node->data.let_decl.let_initializer) {
                initializer = evaluate(interp, node->data.let_decl.let_initializer, env);
                if (interp->had_runtime_error) return value_nil();
            }
            env_define(env, name, initializer);
            return value_nil();
        }

        case AST_IF_STMT: {
            Value cond = evaluate(interp, node->data.if_stmt.condition, env);
            if (interp->had_runtime_error) return value_nil();

            int truthy = (cond.type == VAL_NUMBER && cond.as.number != 0) ||
                         (cond.type == VAL_BOOL && cond.as.boolean) ||
                         (cond.type == VAL_STRING && strlen(cond.as.string) > 0);

            if (truthy) {
                return interpreter_execute(interp, node->data.if_stmt.then_branch, env);
            } else if (node->data.if_stmt.else_branch) {
                return interpreter_execute(interp, node->data.if_stmt.else_branch, env);
            }
            return value_nil();
        }

        case AST_WHILE_STMT: {
            Value result = value_nil();
            for (;;) {
                Value cond = evaluate(interp, node->data.while_stmt.while_condition, env);
                if (interp->had_runtime_error) return value_nil();

                int truthy = (cond.type == VAL_NUMBER && cond.as.number != 0) ||
                             (cond.type == VAL_BOOL && cond.as.boolean) ||
                             (cond.type == VAL_STRING && strlen(cond.as.string) > 0);
                if (!truthy) break;

                result = interpreter_execute(interp, node->data.while_stmt.while_body, env);
                if (interp->had_runtime_error) return value_nil();
            }
            return result;
        }

        case AST_FN_DECL: {
            // Capture current environment as closure
            Environment *closure = env_new(env->parent);
            // Copy current scope entries into closure
            for (int i = 0; i < env->count; i++) {
                env_define(closure, env->entries[i].name, env->entries[i].value);
            }

            Value fn = value_function(node->data.fn_decl.fn_name,
                                      closure,
                                      node->data.fn_decl.fn_body,
                                      node->data.fn_decl.params);
            env_define(env, node->data.fn_decl.fn_name, fn);
            return value_nil();
        }

        case AST_RETURN_STMT: {
            if (node->data.expr) {
                return evaluate(interp, node->data.expr, env);
            }
            return value_nil();
        }

        case AST_ASSIGNMENT: {
            Value v = evaluate(interp, node->data.assignment.value, env);
            if (!interp->had_runtime_error) {
                env_assign(env, node->data.assignment.var_name, v);
            }
            return v;
        }

        default: {
            // Try evaluating as expression
            return evaluate(interp, node, env);
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void interpreter_init(Interpreter *interp) {
    interp->had_runtime_error = 0;
}