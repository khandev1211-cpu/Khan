#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "lexer.h"
#include "parser.h"
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
    // If `name` already exists in THIS exact scope, overwrite it instead of
    // appending a duplicate entry. This matters a lot in practice: Khan
    // doesn't create a new scope per loop iteration, so `let x = ...`
    // re-declared inside a while/for body would otherwise pile up duplicate
    // entries every iteration — and env_get() scans from index 0 upward, so
    // it would always return the OLDEST entry, leaving `x` permanently
    // stuck at its first-ever value no matter how many times the loop runs.
    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->entries[i].name, name) == 0) {
            value_free(env->entries[i].value);
            env->entries[i].value = value;
            return;
        }
    }
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

Value value_native(const char *name, NativeFn fn) {
    Value v;
    v.type = VAL_NATIVE;
    v.as.native.name = strdup(name);
    v.as.native.function = fn;
    return v;
}

// Takes ownership of the `items` buffer directly (no extra copy) — caller
// must have heap-allocated it and must not free it afterwards.
Value value_array(Value *items, int count) {
    Value v;
    v.type = VAL_ARRAY;
    v.as.array.items = items;
    v.as.array.count = count;
    v.as.array.capacity = count;
    return v;
}

Value value_map_empty(void) {
    Value v;
    v.type = VAL_MAP;
    v.as.map.entries = NULL;
    v.as.map.count = 0;
    v.as.map.capacity = 0;
    return v;
}

// Sets key -> value, overwriting if the key already exists. Takes ownership
// of `value` (caller should pass value_copy(...) if it still needs its own
// copy afterwards) — same ownership convention as everywhere else here.
void map_set(Value *map, const char *key, Value value) {
    for (int i = 0; i < map->as.map.count; i++) {
        if (strcmp(map->as.map.entries[i].key, key) == 0) {
            value_free(map->as.map.entries[i].value);
            map->as.map.entries[i].value = value;
            return;
        }
    }
    if (map->as.map.count == map->as.map.capacity) {
        int new_cap = map->as.map.capacity == 0 ? 4 : map->as.map.capacity * 2;
        map->as.map.entries = realloc(map->as.map.entries, sizeof(MapEntry) * new_cap);
        map->as.map.capacity = new_cap;
    }
    map->as.map.entries[map->as.map.count].key = strdup(key);
    map->as.map.entries[map->as.map.count].value = value;
    map->as.map.count++;
}

// Returns a pointer to the stored value for `key`, or NULL if absent.
// Simple linear search — fine for the small maps this language deals with.
Value *map_get(Value *map, const char *key) {
    for (int i = 0; i < map->as.map.count; i++) {
        if (strcmp(map->as.map.entries[i].key, key) == 0) {
            return &map->as.map.entries[i].value;
        }
    }
    return NULL;
}

// Deep-copies a value so the result owns fully independent memory from the
// original. MUST be used any time a value is read out of a variable and
// might end up stored somewhere else (another variable, a function arg,
// inside an array, etc.) — otherwise two owners can end up pointing at the
// same heap memory, and freeing one corrupts/frees the other (double-free).
Value value_copy(Value v) {
    Value copy = v;
    switch (v.type) {
        case VAL_STRING:
            copy.as.string = strdup(v.as.string);
            break;
        case VAL_FUNCTION:
            copy.as.function.name = strdup(v.as.function.name);
            break;
        case VAL_NATIVE:
            copy.as.native.name = strdup(v.as.native.name);
            break;
        case VAL_ARRAY: {
            int count = v.as.array.count;
            Value *items = count > 0 ? malloc(sizeof(Value) * count) : NULL;
            for (int i = 0; i < count; i++) {
                items[i] = value_copy(v.as.array.items[i]);
            }
            copy.as.array.items = items;
            copy.as.array.capacity = count;
            break;
        }
        case VAL_MAP: {
            int count = v.as.map.count;
            MapEntry *entries = count > 0 ? malloc(sizeof(MapEntry) * count) : NULL;
            for (int i = 0; i < count; i++) {
                entries[i].key = strdup(v.as.map.entries[i].key);
                entries[i].value = value_copy(v.as.map.entries[i].value);
            }
            copy.as.map.entries = entries;
            copy.as.map.capacity = count;
            break;
        }
        default:
            break; // numbers, bools, nil have no owned memory
    }
    return copy;
}

void value_free(Value v) {
    if (v.type == VAL_STRING) {
        free((void *)v.as.string);
    } else if (v.type == VAL_FUNCTION) {
        free((void *)v.as.function.name);
        // Don't free closure/body/params — they're owned by the AST
    } else if (v.type == VAL_NATIVE) {
        free((void *)v.as.native.name);
    } else if (v.type == VAL_ARRAY) {
        for (int i = 0; i < v.as.array.count; i++) {
            value_free(v.as.array.items[i]);
        }
        free(v.as.array.items);
    } else if (v.type == VAL_MAP) {
        for (int i = 0; i < v.as.map.count; i++) {
            free((void *)v.as.map.entries[i].key);
            value_free(v.as.map.entries[i].value);
        }
        free(v.as.map.entries);
    }
}

// Prints a value the way it should look when nested inside an array
// (e.g. strings get quotes, like Python's repr inside a list).
static void value_print_nested(Value v) {
    if (v.type == VAL_STRING) {
        printf("\"%s\"", v.as.string);
    } else {
        value_print(v);
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
        case VAL_NATIVE:
            printf("<native %s>", v.as.native.name);
            break;
        case VAL_ARRAY:
            printf("[");
            for (int i = 0; i < v.as.array.count; i++) {
                if (i > 0) printf(", ");
                value_print_nested(v.as.array.items[i]);
            }
            printf("]");
            break;
        case VAL_MAP:
            printf("{");
            for (int i = 0; i < v.as.map.count; i++) {
                if (i > 0) printf(", ");
                printf("\"%s\": ", v.as.map.entries[i].key);
                value_print_nested(v.as.map.entries[i].value);
            }
            printf("}");
            break;
    }
}

// Forward-declare the import executor so we can forward-reference it
// without having main.c's read_file helper here. We'll read the file inline.
static Value execute_import(Interpreter *interp, const char *path, Environment *env);

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

        case AST_BOOL:
            return value_bool(node->data.bool_value);

        case AST_NIL:
            return value_nil();

        case AST_STRING:
            return value_string(node->data.string_value);

        case AST_IDENTIFIER: {
            Value *v = env_get(env, node->data.name);
            if (!v) {
                runtime_error(interp, node, "Undefined variable.");
                return value_nil();
            }
            return value_copy(*v);
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

        case AST_IMPORT_STMT:
            return execute_import(interp, node->data.import_path, env);

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

            // Handle native function calls
            if (callee->type == VAL_NATIVE) {
                Value result;
                callee->as.native.function(&result, interp, argc, argv);
                free(argv);
                return result;
            }

            if (callee->type != VAL_FUNCTION) {
                runtime_error(interp, node, "Can only call functions.");
                free(argv);
                return value_nil();
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

        case AST_ARRAY: {
            int count = 0;
            for (AstNodeList *e = node->data.array_elements; e; e = e->next) count++;

            Value *items = count > 0 ? malloc(sizeof(Value) * count) : NULL;
            int i = 0;
            for (AstNodeList *e = node->data.array_elements; e; e = e->next) {
                items[i] = evaluate(interp, e->node, env);
                if (interp->had_runtime_error) {
                    // Free what we've built so far, then bail out
                    for (int j = 0; j < i; j++) value_free(items[j]);
                    free(items);
                    return value_nil();
                }
                i++;
            }
            return value_array(items, count);
        }

        case AST_MAP: {
            Value map = value_map_empty();
            for (AstNodeList *e = node->data.map_entries; e; e = e->next) {
                AstNode *entry = e->node; // AST_MAP_ENTRY

                Value key_val = evaluate(interp, entry->data.map_entry.key, env);
                if (interp->had_runtime_error) { value_free(map); return value_nil(); }
                if (key_val.type != VAL_STRING) {
                    runtime_error(interp, entry, "Map keys must be strings.");
                    value_free(key_val);
                    value_free(map);
                    return value_nil();
                }

                Value val = evaluate(interp, entry->data.map_entry.value, env);
                if (interp->had_runtime_error) {
                    value_free(key_val);
                    value_free(map);
                    return value_nil();
                }

                map_set(&map, key_val.as.string, val); // map_set takes ownership of val
                value_free(key_val); // map_set strdup'd the key internally
            }
            return map;
        }

        case AST_INDEX: {
            Value obj = evaluate(interp, node->data.index_expr.object, env);
            if (interp->had_runtime_error) return value_nil();

            if (obj.type == VAL_MAP) {
                Value key_val = evaluate(interp, node->data.index_expr.index, env);
                if (interp->had_runtime_error) { value_free(obj); return value_nil(); }
                if (key_val.type != VAL_STRING) {
                    runtime_error(interp, node, "Map key must be a string.");
                    value_free(obj);
                    value_free(key_val);
                    return value_nil();
                }
                Value *found = map_get(&obj, key_val.as.string);
                if (!found) {
                    runtime_error(interp, node, "Key not found in map.");
                    value_free(obj);
                    value_free(key_val);
                    return value_nil();
                }
                Value result = value_copy(*found);
                value_free(key_val);
                value_free(obj);
                return result;
            }

            if (obj.type != VAL_ARRAY) {
                runtime_error(interp, node, "Can only index into an array or map.");
                value_free(obj);
                return value_nil();
            }

            Value idx_val = evaluate(interp, node->data.index_expr.index, env);
            if (interp->had_runtime_error) {
                value_free(obj);
                return value_nil();
            }
            if (idx_val.type != VAL_NUMBER) {
                runtime_error(interp, node, "Array index must be a number.");
                value_free(obj);
                value_free(idx_val);
                return value_nil();
            }

            int idx = (int)idx_val.as.number;
            value_free(idx_val);

            if (idx < 0 || idx >= obj.as.array.count) {
                runtime_error(interp, node, "Array index out of bounds.");
                value_free(obj);
                return value_nil();
            }

            Value result = value_copy(obj.as.array.items[idx]);
            value_free(obj);
            return result;
        }

        case AST_INDEX_ASSIGN: {
            if (node->data.index_assign.object->type != AST_IDENTIFIER) {
                runtime_error(interp, node,
                    "Can only assign into a simple array/map variable (e.g. x[i] = v).");
                return value_nil();
            }

            const char *name = node->data.index_assign.object->data.name;
            Value *target = env_get(env, name);
            if (!target) {
                runtime_error(interp, node, "Undefined variable.");
                return value_nil();
            }

            if (target->type == VAL_MAP) {
                Value key_val = evaluate(interp, node->data.index_assign.index, env);
                if (interp->had_runtime_error) return value_nil();
                if (key_val.type != VAL_STRING) {
                    runtime_error(interp, node, "Map key must be a string.");
                    value_free(key_val);
                    return value_nil();
                }

                Value new_val = evaluate(interp, node->data.index_assign.value, env);
                if (interp->had_runtime_error) { value_free(key_val); return value_nil(); }

                map_set(target, key_val.as.string, value_copy(new_val));
                value_free(key_val);
                return new_val;
            }

            if (target->type != VAL_ARRAY) {
                runtime_error(interp, node, "Can only index-assign into an array or map.");
                return value_nil();
            }
            Value *arr_ptr = target;

            Value idx_val = evaluate(interp, node->data.index_assign.index, env);
            if (interp->had_runtime_error) return value_nil();
            if (idx_val.type != VAL_NUMBER) {
                runtime_error(interp, node, "Array index must be a number.");
                value_free(idx_val);
                return value_nil();
            }
            int idx = (int)idx_val.as.number;
            value_free(idx_val);

            if (idx < 0 || idx >= arr_ptr->as.array.count) {
                runtime_error(interp, node, "Array index out of bounds.");
                return value_nil();
            }

            Value new_val = evaluate(interp, node->data.index_assign.value, env);
            if (interp->had_runtime_error) return value_nil();

            value_free(arr_ptr->as.array.items[idx]);
            arr_ptr->as.array.items[idx] = value_copy(new_val);
            return new_val;
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

        case AST_FOR_STMT: {
            Value iterable = evaluate(interp, node->data.for_stmt.for_iterable, env);
            if (interp->had_runtime_error) return value_nil();

            if (iterable.type != VAL_ARRAY) {
                runtime_error(interp, node, "Can only iterate ('for ... in ...') over an array.");
                value_free(iterable);
                return value_nil();
            }

            Value result = value_nil();
            for (int i = 0; i < iterable.as.array.count; i++) {
                env_assign(env, node->data.for_stmt.for_var,
                           value_copy(iterable.as.array.items[i]));

                result = interpreter_execute(interp, node->data.for_stmt.for_body, env);
                if (interp->had_runtime_error) {
                    value_free(iterable);
                    return value_nil();
                }
            }
            value_free(iterable);
            return result;
        }

        case AST_FN_DECL: {
            // Capture current environment as closure
            Environment *closure = env_new(env->parent);
            // Copy current scope entries into closure
            for (int i = 0; i < env->count; i++) {
                env_define(closure, env->entries[i].name, value_copy(env->entries[i].value));
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
// Import executor — reads a file, parses it, executes it in the given env
// ---------------------------------------------------------------------------
static Value execute_import(Interpreter *interp, const char *path, Environment *env) {
    // Resolve the import path relative to the base path
    char full_path[1024];
    if (interp->base_path) {
        snprintf(full_path, sizeof(full_path), "%s/%s", interp->base_path, path);
    } else {
        snprintf(full_path, sizeof(full_path), "%s", path);
    }

    FILE *file = fopen(full_path, "rb");
    if (!file) {
        // Try the path as-is as a fallback
        file = fopen(path, "rb");
    }
    if (!file) {
        fprintf(stderr, "[line 0] Runtime error: Could not open import '%s'.\n", full_path);
        interp->had_runtime_error = 1;
        return value_nil();
    }

    fseek(file, 0L, SEEK_END);
    long size = ftell(file);
    rewind(file);

    char *source = malloc(size + 1);
    if (!source) {
        fprintf(stderr, "[line 0] Runtime error: Out of memory reading import '%s'.\n", full_path);
        fclose(file);
        interp->had_runtime_error = 1;
        return value_nil();
    }
    fread(source, 1, size, file);
    source[size] = '\0';
    fclose(file);

    // Lex, parse, and execute the imported file
    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    AstNode *program = parser_parse(&parser);

    if (parser.had_error) {
        fprintf(stderr, "[line 0] Runtime error: Syntax error(s) in import '%s'.\n", full_path);
        free(source);
        ast_free(program);
        interp->had_runtime_error = 1;
        return value_nil();
    }

    Value result = interpreter_execute(interp, program, env);

    // NOTE: We deliberately do NOT free `program` or `source` here.
    // Functions declared in the imported file hold raw pointers into this
    // AST (body/params aren't copied — see value_function/value_free).
    // Freeing it now would leave any imported function with a dangling
    // body pointer the moment it's called later in the importing script.
    // This matches how the main script's own AST is kept alive for the
    // whole process (see main.c) — a small intentional leak, not a crash.
    return result;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void interpreter_init(Interpreter *interp, const char *base_path) {
    interp->had_runtime_error = 0;
    interp->base_path = base_path;
}
