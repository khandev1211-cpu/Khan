#ifndef KHAN_INTERPRETER_H
#define KHAN_INTERPRETER_H

#include "ast.h"

typedef struct Environment Environment;
typedef struct Interpreter Interpreter;

typedef enum {
    VAL_NUMBER,
    VAL_STRING,
    VAL_BOOL,
    VAL_NIL,
    VAL_FUNCTION,
    VAL_NATIVE,
    VAL_ARRAY,
    VAL_MAP,
} ValueType;

struct Value;

typedef struct {
    const char *key;
    struct Value *value; // Pointers to Value to break circularity
} MapEntry;

typedef void (*NativeFn)(struct Value *result, Interpreter *interp, int argc, struct Value *args);

typedef struct Obj {
    ValueType type;
    int ref_count;
    union {
        struct {
            struct Value *items;
            int count;
            int capacity;
        } array;
        struct {
            MapEntry *entries;
            int count;
            int capacity;
        } map;
    } as;
} Obj;

typedef struct Value {
    ValueType type;
    union {
        double number;
        int boolean;
        const char *string;
        Obj *obj;
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
} Value;

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

Value value_number(double n);
Value value_string(const char *s);
Value value_bool(int b);
Value value_nil(void);
Value value_function(const char *name, Environment *closure,
                     AstNode *body, AstNodeList *params);
Value value_native(const char *name, NativeFn fn);
Value value_array(Value *items, int count);
Value value_map_empty(void);

void map_set(Value *map, const char *key, Value value);
Value *map_get(Value *map, const char *key);

Value value_copy(Value v);
void value_free(Value v);
void value_print(Value v);

// ---------------------------------------------------------------------------
// Accessors for shared objects
// ---------------------------------------------------------------------------
#define AS_ARRAY_COUNT(v) ((v).as.obj->as.array.count)
#define AS_ARRAY_ITEMS(v) ((v).as.obj->as.array.items)
#define AS_MAP_COUNT(v)   ((v).as.obj->as.map.count)
#define AS_MAP_ENTRIES(v) ((v).as.obj->as.map.entries)

struct Interpreter {
    int had_runtime_error;
    const char *base_path;
    int is_returning;
    Value return_value;
    int is_breaking;
    int is_continuing;
    Environment *base_env;
    char current_import_dir[1024];
};

void interpreter_init(Interpreter *interp, const char *base_path);
Value interpreter_execute(Interpreter *interp, AstNode *node, Environment *env);

Value khan_call_fn(Interpreter *interp, Environment *env,
                   const char *fn_name, int argc, Value *argv);

#endif
