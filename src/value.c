#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "value.h"
#include "chunk.h" /* for KhanClosure — VM closure capture, see value_copy/value_free */

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
    v.as.obj->as.map.hash_index = NULL;
    v.as.obj->as.map.hash_capacity = 0;
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
        if (copy.as.function.closure) {
            khanclosure_retain((KhanClosure *)copy.as.function.closure);
        }
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
                free(v.as.obj->as.map.hash_index);
            }
            free(v.as.obj);
        }
    } else if (v.type == VAL_STRING) {
        free((void*)v.as.string);
    } else if (v.type == VAL_FUNCTION) {
        free((void*)v.as.function.name);
        if (v.as.function.closure) {
            khanclosure_release((KhanClosure *)v.as.function.closure);
        }
    } else if (v.type == VAL_NATIVE) {
        free((void*)v.as.native.name);
    }
}

/* ---------------------------------------------------------------------
 * Map hash index — open-addressing (linear probing) side table that maps
 * key -> slot in the dense `entries[]` array. `entries[]` itself stays in
 * insertion order (unchanged), so every existing caller that walks
 * AS_MAP_ENTRIES()/AS_MAP_COUNT() in a for-loop (JSON encoding, keys(),
 * header iteration, etc.) keeps working byte-for-byte the same as before.
 * This only replaces the O(n) linear scan that map_set/map_get used to do.
 *
 * No deletion exists anywhere in the codebase for maps (checked), so no
 * tombstones are needed — every index slot is either -1 (empty) or a
 * valid entries[] index.
 * ------------------------------------------------------------------- */

static unsigned long map_hash_key(const char *key) {
    /* FNV-1a */
    unsigned long h = 2166136261UL;
    for (const unsigned char *p = (const unsigned char *)key; *p; p++) {
        h ^= *p;
        h *= 16777619UL;
    }
    return h;
}

static void map_hash_grow(Obj *o) {
    int old_cap = o->as.map.hash_capacity;
    int new_cap = old_cap ? old_cap * 2 : 8;
    int *new_index = malloc(sizeof(int) * new_cap);
    for (int i = 0; i < new_cap; i++) new_index[i] = -1;

    /* Rehash every existing entry (in insertion order) into the new table. */
    unsigned long mask = (unsigned long)(new_cap - 1);
    for (int i = 0; i < o->as.map.count; i++) {
        unsigned long h = map_hash_key(o->as.map.entries[i].key) & mask;
        while (new_index[h] != -1) {
            h = (h + 1) & mask;
        }
        new_index[h] = i;
    }

    free(o->as.map.hash_index);
    o->as.map.hash_index = new_index;
    o->as.map.hash_capacity = new_cap;
}

/* Returns the entries[] slot index for `key`, or -1 if absent.
 * If `out_probe` is non-NULL, also writes the empty slot where `key`
 * should be inserted (only meaningful when the return value is -1). */
static int map_hash_find(Obj *o, const char *key, unsigned long *out_probe) {
    if (o->as.map.hash_capacity == 0) {
        if (out_probe) *out_probe = 0;
        return -1;
    }
    unsigned long mask = (unsigned long)(o->as.map.hash_capacity - 1);
    unsigned long h = map_hash_key(key) & mask;
    while (o->as.map.hash_index[h] != -1) {
        int slot = o->as.map.hash_index[h];
        if (strcmp(o->as.map.entries[slot].key, key) == 0) {
            return slot;
        }
        h = (h + 1) & mask;
    }
    if (out_probe) *out_probe = h;
    return -1;
}

void map_set(Value *map, const char *key, Value value) {
    if (map->type != VAL_MAP || !map->as.obj) return;
    Obj *o = map->as.obj;

    /* Grow the hash table before probing if we're at/over ~70% load,
     * so map_hash_find always has room to find an empty slot. */
    if (o->as.map.hash_capacity == 0 ||
        (o->as.map.count + 1) * 10 >= o->as.map.hash_capacity * 7) {
        map_hash_grow(o);
    }

    unsigned long probe;
    int existing = map_hash_find(o, key, &probe);
    if (existing != -1) {
        value_free(*o->as.map.entries[existing].value);
        *o->as.map.entries[existing].value = value;
        return;
    }

    if (o->as.map.count >= o->as.map.capacity) {
        o->as.map.capacity = o->as.map.capacity ? o->as.map.capacity * 2 : 4;
        o->as.map.entries = realloc(o->as.map.entries, sizeof(MapEntry) * o->as.map.capacity);
    }
    int new_slot = o->as.map.count;
    o->as.map.entries[new_slot].key = strdup(key);
    o->as.map.entries[new_slot].value = malloc(sizeof(Value));
    *o->as.map.entries[new_slot].value = value;
    o->as.map.count++;

    /* `probe` was computed just above by map_hash_find on the current
     * (just-possibly-grown) table, so it's still a valid empty slot. */
    o->as.map.hash_index[probe] = new_slot;
}

Value *map_get(Value *map, const char *key) {
    if (map->type != VAL_MAP || !map->as.obj) return NULL;
    Obj *o = map->as.obj;
    int slot = map_hash_find(o, key, NULL);
    if (slot == -1) return NULL;
    return o->as.map.entries[slot].value;
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
