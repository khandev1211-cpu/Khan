#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "value.h"

static Obj *obj_new(ValueType type) {
    Obj *obj = malloc(sizeof(Obj));
    obj->type = type;
    obj->ref_count = 1;
    return obj;
}

Value value_number(double n) {
    Value v; v.type = VAL_NUMBER; v.as.number = n; return v;
}

Value value_bool(int b) {
    Value v; v.type = VAL_BOOL; v.as.boolean = b; return v;
}

Value value_nil(void) {
    Value v; v.type = VAL_NIL; v.as.number = 0; return v;
}

Value value_string(const char *s) {
    Value v;
    v.type = VAL_STRING;
    v.as.string = strdup(s);
    return v;
}

Value value_array(Value *items, int count) {
    Value v;
    v.type = VAL_ARRAY;
    v.as.obj = obj_new(VAL_ARRAY);
    v.as.obj->as.array.items = items;
    v.as.obj->as.array.count = count;
    v.as.obj->as.array.capacity = count;
    return v;
}

Value value_map_empty(void) {
    Value v;
    v.type = VAL_MAP;
    v.as.obj = obj_new(VAL_MAP);
    v.as.obj->as.map.entries = NULL;
    v.as.obj->as.map.count = 0;
    v.as.obj->as.map.capacity = 0;
    return v;
}

Value value_native(const char *name, NativeFn fn) {
    Value v;
    v.type = VAL_NATIVE;
    v.as.native.name = strdup(name);
    v.as.native.function = fn;
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

Value value_copy(Value v) {
    if (v.type == VAL_ARRAY || v.type == VAL_MAP) {
        if (v.as.obj) v.as.obj->ref_count++;
        return v;
    }
    if (v.type == VAL_STRING) {
        Value copy = v;
        copy.as.string = strdup(v.as.string);
        return copy;
    }
    if (v.type == VAL_FUNCTION) {
        Value copy = v;
        copy.as.function.name = strdup(v.as.function.name);
        return copy;
    }
    if (v.type == VAL_NATIVE) {
        Value copy = v;
        copy.as.native.name = strdup(v.as.native.name);
        return copy;
    }
    return v;
}

void value_free(Value v) {
    if (v.type == VAL_ARRAY || v.type == VAL_MAP) {
        if (!v.as.obj) return;
        v.as.obj->ref_count--;
        if (v.as.obj->ref_count <= 0) {
            if (v.type == VAL_ARRAY) {
                for (int i = 0; i < v.as.obj->as.array.count; i++) {
                    value_free(v.as.obj->as.array.items[i]);
                }
                free(v.as.obj->as.array.items);
            } else {
                for (int i = 0; i < v.as.obj->as.map.count; i++) {
                    free((void*)v.as.obj->as.map.entries[i].key);
                    value_free(*v.as.obj->as.map.entries[i].value);
                    free(v.as.obj->as.map.entries[i].value);
                }
                free(v.as.obj->as.map.entries);
            }
            free(v.as.obj);
        }
    } else if (v.type == VAL_STRING) {
        free((void*)v.as.string);
    } else if (v.type == VAL_FUNCTION) {
        free((void*)v.as.function.name);
    } else if (v.type == VAL_NATIVE) {
        free((void*)v.as.native.name);
    }
}

void map_set(Value *map, const char *key, Value value) {
    if (map->type != VAL_MAP || !map->as.obj) return;
    Obj *o = map->as.obj;
    for (int i = 0; i < o->as.map.count; i++) {
        if (strcmp(o->as.map.entries[i].key, key) == 0) {
            value_free(*o->as.map.entries[i].value);
            *o->as.map.entries[i].value = value;
            return;
        }
    }
    if (o->as.map.count >= o->as.map.capacity) {
        o->as.map.capacity = o->as.map.capacity ? o->as.map.capacity * 2 : 4;
        o->as.map.entries = realloc(o->as.map.entries, sizeof(MapEntry) * o->as.map.capacity);
    }
    o->as.map.entries[o->as.map.count].key = strdup(key);
    o->as.map.entries[o->as.map.count].value = malloc(sizeof(Value));
    *o->as.map.entries[o->as.map.count].value = value;
    o->as.map.count++;
}

Value *map_get(Value *map, const char *key) {
    if (map->type != VAL_MAP || !map->as.obj) return NULL;
    Obj *o = map->as.obj;
    for (int i = 0; i < o->as.map.count; i++) {
        if (strcmp(o->as.map.entries[i].key, key) == 0) {
            return o->as.map.entries[i].value;
        }
    }
    return NULL;
}

void value_print(Value v) {
    switch (v.type) {
        case VAL_NUMBER: {
            double n = v.as.number;
            if (n == (long long)n) printf("%lld", (long long)n);
            else                   printf("%g", n);
            break;
        }
        case VAL_STRING:  printf("%s", v.as.string); break;
        case VAL_BOOL:    printf("%s", v.as.boolean ? "true" : "false"); break;
        case VAL_NIL:     printf("nil"); break;
        case VAL_ARRAY: {
            printf("[");
            if (v.as.obj) {
                for (int i = 0; i < v.as.obj->as.array.count; i++) {
                    if (i > 0) printf(", ");
                    value_print(v.as.obj->as.array.items[i]);
                }
            }
            printf("]");
            break;
        }
        case VAL_MAP: {
            printf("{");
            if (v.as.obj) {
                for (int i = 0; i < v.as.obj->as.map.count; i++) {
                    if (i > 0) printf(", ");
                    printf("\"%s\": ", v.as.obj->as.map.entries[i].key);
                    value_print(*v.as.obj->as.map.entries[i].value);
                }
            }
            printf("}");
            break;
        }
        case VAL_FUNCTION: printf("<fn %s>", v.as.function.name); break;
        case VAL_NATIVE:   printf("<native %s>", v.as.native.name); break;
    }
}

int vm_is_truthy(Value v) {
    if (v.type == VAL_NIL) return 0;
    if (v.type == VAL_BOOL) return v.as.boolean;
    if (v.type == VAL_NUMBER) return v.as.number != 0;
    if (v.type == VAL_STRING) return v.as.string && v.as.string[0] != '\0';
    return 1;
}

int vm_values_equal(Value a, Value b) {
    if (a.type != b.type) return 0;
    switch (a.type) {
        case VAL_BOOL:   return a.as.boolean == b.as.boolean;
        case VAL_NUMBER: return a.as.number == b.as.number;
        case VAL_STRING: return strcmp(a.as.string, b.as.string) == 0;
        case VAL_NIL:    return 1;
        case VAL_ARRAY:  return a.as.obj == b.as.obj;
        case VAL_MAP:    return a.as.obj == b.as.obj;
        default: return 0;
    }
}

void vm_print_value(Value v) {
    value_print(v);
}
