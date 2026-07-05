#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif
#include "vm.h"
#include "value.h"
#include "chunk.h"
#include "interpreter.h"

/* ══════════════════════════════════════════════════════════════
   KhanFunction registry
   ══════════════════════════════════════════════════════════════ */

#define KHANFN_REGISTRY_MAX 1024
static KhanFunction *fn_registry[KHANFN_REGISTRY_MAX];
static int           fn_registry_count = 0;

void khanfn_register(KhanFunction *fn) {
    if (fn_registry_count < KHANFN_REGISTRY_MAX)
        fn_registry[fn_registry_count++] = fn;
}

int khanfn_registry_index(void) {
    return fn_registry_count - 1;
}

/* ══════════════════════════════════════════════════════════════
   Hash Table implementation for Globals
   ══════════════════════════════════════════════════════════════ */

static uint32_t hash_string(const char *key) {
    uint32_t hash = 2166136261u;
    for (int i = 0; key[i] != '\0'; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

static void table_init(Table *table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

static void table_free(Table *table) {
    for (int i = 0; i < table->capacity; i++) {
        if (table->entries[i].key) free(table->entries[i].key);
    }
    free(table->entries);
    table_init(table);
}

static TableEntry *find_entry(TableEntry *entries, int capacity, const char *key) {
    uint32_t hash = hash_string(key);
    uint32_t index = hash % capacity;
    for (;;) {
        TableEntry *entry = &entries[index];
        if (entry->key == NULL || strcmp(entry->key, key) == 0) {
            return entry;
        }
        index = (index + 1) % capacity;
    }
}

static void table_adjust_cap(Table *table, int capacity) {
    TableEntry *entries = malloc(sizeof(TableEntry) * capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].val = value_nil();
    }

    for (int i = 0; i < table->capacity; i++) {
        TableEntry *src = &table->entries[i];
        if (src->key == NULL) continue;
        TableEntry *dst = find_entry(entries, capacity, src->key);
        dst->key = src->key;
        dst->val = src->val;
    }

    free(table->entries);
    table->entries = entries;
    table->capacity = capacity;
}

static int table_set(Table *table, const char *key, Value val) {
    if (table->count + 1 > table->capacity * 0.75) {
        int capacity = table->capacity < 8 ? 8 : table->capacity * 2;
        table_adjust_cap(table, capacity);
    }

    TableEntry *entry = find_entry(table->entries, table->capacity, key);
    int is_new = (entry->key == NULL);
    if (is_new) table->count++;

    if (is_new) entry->key = strdup(key);
    entry->val = val;
    return is_new;
}

static int table_get(Table *table, const char *key, Value *val) {
    if (table->count == 0) return 0;
    TableEntry *entry = find_entry(table->entries, table->capacity, key);
    if (entry->key == NULL) return 0;
    *val = entry->val;
    return 1;
}

static void global_set(VM *vm, const char *key, Value val) {
    table_set(&vm->globals, key, val);
}

static int global_get(VM *vm, const char *key, Value *out) {
    return table_get(&vm->globals, key, out);
}

/* ══════════════════════════════════════════════════════════════
   Stack helpers
   ══════════════════════════════════════════════════════════════ */

static inline void push(VM *vm, Value v) {
    *vm->stack_top++ = v;
}

static inline Value pop(VM *vm) {
    return *--vm->stack_top;
}

static inline Value peek(VM *vm, int distance) {
    return vm->stack_top[-1 - distance];
}

/* ══════════════════════════════════════════════════════════════
   Runtime error
   ══════════════════════════════════════════════════════════════ */

static InterpretResult runtime_error(VM *vm, const char *msg) {
    CallFrame *f = &vm->frames[vm->frame_count - 1];
    int offset   = (int)(f->ip - f->fn->chunk.code) - 1;
    int line     = (offset >= 0 && offset < f->fn->chunk.count)
                   ? f->fn->chunk.lines[offset] : 0;
    fprintf(stderr, "[line %d] Runtime error: %s\n", line, msg);
    return INTERPRET_RUNTIME_ERROR;
}

void vm_global_set_native(VM *vm, const char *name, NativeFn fn) {
    Value v;
    v.type = VAL_NATIVE;
    v.as.native.name     = strdup(name);
    v.as.native.function = fn;
    global_set(vm, name, v);
}

void vm_init(VM *vm) {
    vm->stack_top   = vm->stack;
    vm->frame_count = 0;
    table_init(&vm->globals);
}

void vm_free(VM *vm) {
    table_free(&vm->globals);
}

/* ══════════════════════════════════════════════════════════════
   Main execution loop
   ══════════════════════════════════════════════════════════════ */

#define READ_BYTE()   (*frame->ip++)
#define READ_SHORT()  (frame->ip += 2, \
                       (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONST()  (frame->fn->chunk.constants[READ_BYTE()])

InterpretResult vm_run(VM *vm, KhanFunction *script) {
    push(vm, value_nil());

    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->fn    = script;
    frame->ip    = script->chunk.code;
    frame->slots = vm->stack_top;

    for (;;) {
        uint8_t op = READ_BYTE();

        switch (op) {
        case OP_CONST:  push(vm, value_copy(READ_CONST())); break;
        case OP_NIL:    push(vm, value_nil());    break;
        case OP_TRUE:   push(vm, value_bool(1));  break;
        case OP_FALSE:  push(vm, value_bool(0));  break;
        case OP_POP:    value_free(pop(vm));      break;

        case OP_NEGATE_NUM: {
            Value v = pop(vm);
            push(vm, value_number(-v.as.number));
            break;
        }
        case OP_NOT_BOOL:
            push(vm, value_bool(!vm_is_truthy(pop(vm))));
            break;

        case OP_ADD: {
            Value b = pop(vm), a = pop(vm);
            if (a.type == VAL_STRING && b.type == VAL_STRING) {
                int la = (int)strlen(a.as.string);
                int lb = (int)strlen(b.as.string);
                char *s = malloc(la + lb + 1);
                memcpy(s, a.as.string, la);
                memcpy(s + la, b.as.string, lb + 1);
                Value r = value_string(s);
                free(s);
                push(vm, r);
            } else if (a.type == VAL_NUMBER && b.type == VAL_NUMBER) {
                push(vm, value_number(a.as.number + b.as.number));
            } else {
                return runtime_error(vm, "Operands must be two numbers or two strings");
            }
            value_free(a); value_free(b);
            break;
        }

#define NUMERIC_OP(result_fn, op_sym)                                    \
    do {                                                                  \
        Value b = pop(vm), a = pop(vm);                                  \
        push(vm, result_fn(a.as.number op_sym b.as.number));             \
        value_free(a); value_free(b);                                    \
    } while (0)

        case OP_SUB: NUMERIC_OP(value_number, -); break;
        case OP_MUL: NUMERIC_OP(value_number, *); break;
        case OP_DIV: {
            Value b = pop(vm), a = pop(vm);
            if (b.as.number == 0.0) return runtime_error(vm, "Division by zero");
            push(vm, value_number(a.as.number / b.as.number));
            value_free(a); value_free(b);
            break;
        }
        case OP_MOD: {
            Value b = pop(vm), a = pop(vm);
            push(vm, value_number(fmod(a.as.number, b.as.number)));
            value_free(a); value_free(b);
            break;
        }
        case OP_EQ:  { Value b=pop(vm),a=pop(vm); push(vm,value_bool( vm_values_equal(a,b))); value_free(a); value_free(b); break; }
        case OP_NEQ: { Value b=pop(vm),a=pop(vm); push(vm,value_bool(!vm_values_equal(a,b))); value_free(a); value_free(b); break; }
        case OP_LT:  NUMERIC_OP(value_bool, <);  break;
        case OP_LE:  NUMERIC_OP(value_bool, <=); break;
        case OP_GT:  NUMERIC_OP(value_bool, >);  break;
        case OP_GE:  NUMERIC_OP(value_bool, >=); break;

        case OP_PRINT:
            value_print(pop(vm));
            printf("\n");
            break;

        case OP_DEF_GLOBAL: {
            Value name_v = READ_CONST();
            Value val = pop(vm);
            if (val.type == VAL_NUMBER) {
                int idx = (int)val.as.number;
                if (idx >= 0 && idx < fn_registry_count) {
                    const char *gname = name_v.as.string;
                    if (fn_registry[idx] && fn_registry[idx]->name && strcmp(fn_registry[idx]->name, gname) == 0) {
                        Value fv;
                        fv.type = VAL_FUNCTION;
                        fv.as.function.name    = strdup(fn_registry[idx]->name);
                        fv.as.function.closure = NULL;
                        fv.as.function.body    = (struct AstNode*)fn_registry[idx];
                        fv.as.function.params  = NULL;
                        global_set(vm, gname, fv);
                        value_free(val);
                        break;
                    }
                }
            }
            global_set(vm, name_v.as.string, val);
            break;
        }

        case OP_GET_GLOBAL: {
            Value name_v = READ_CONST();
            Value val;
            if (!global_get(vm, name_v.as.string, &val)) {
                char msg[256]; snprintf(msg, sizeof(msg), "Undefined variable '%s'", name_v.as.string);
                return runtime_error(vm, msg);
            }
            push(vm, value_copy(val));
            break;
        }

        case OP_SET_GLOBAL: {
            Value name_v = READ_CONST();
            Value existing;
            if (global_get(vm, name_v.as.string, &existing)) {
                value_free(existing);
            }
            global_set(vm, name_v.as.string, value_copy(peek(vm, 0)));
            break;
        }

        case OP_GET_LOCAL: {
            uint8_t slot = READ_BYTE();
            push(vm, value_copy(frame->slots[slot]));
            break;
        }
        case OP_SET_LOCAL: {
            uint8_t slot = READ_BYTE();
            value_free(frame->slots[slot]);
            frame->slots[slot] = value_copy(peek(vm, 0));
            break;
        }

        case OP_JUMP: {
            uint16_t off = READ_SHORT();
            frame->ip += off;
            break;
        }
        case OP_JUMP_IF_FALSE: {
            uint16_t off = READ_SHORT();
            if (!vm_is_truthy(peek(vm, 0))) frame->ip += off;
            break;
        }
        case OP_LOOP: {
            uint16_t off = READ_SHORT();
            frame->ip -= off;
            break;
        }

        case OP_CALL: {
            int arg_count = READ_BYTE();
            Value callee  = peek(vm, arg_count);

            if (callee.type == VAL_NATIVE) {
                NativeFn nfn = callee.as.native.function;
                Value *args  = vm->stack_top - arg_count;
                Value result = value_nil();
                nfn(&result, NULL, arg_count, args);
                for (int i = 0; i < arg_count; i++) value_free(pop(vm));
                value_free(pop(vm));
                push(vm, result);
                break;
            }

            if (callee.type == VAL_FUNCTION) {
                KhanFunction *fn = (KhanFunction*)callee.as.function.body;
                if (!fn) return runtime_error(vm, "Invalid function");
                if (arg_count != fn->arity) return runtime_error(vm, "Arg count mismatch");
                if (vm->frame_count >= VM_FRAMES_MAX) return runtime_error(vm, "Stack overflow");

                CallFrame *new_frame = &vm->frames[vm->frame_count++];
                new_frame->fn    = fn;
                new_frame->ip    = fn->chunk.code;
                new_frame->slots = vm->stack_top - arg_count;
                frame = new_frame;
                break;
            }
            return runtime_error(vm, "Can only call functions");
        }

        case OP_RETURN: {
            Value result = pop(vm);
            vm->frame_count--;
            if (vm->frame_count == 0) return INTERPRET_OK;
            vm->stack_top = frame->slots - 1;
            push(vm, result);
            frame = &vm->frames[vm->frame_count - 1];
            break;
        }

        case OP_MAKE_ARRAY: {
            int count = READ_BYTE();
            Value *items = count > 0 ? malloc(count * sizeof(Value)) : NULL;
            for (int i = count - 1; i >= 0; i--) items[i] = pop(vm);
            push(vm, value_array(items, count));
            break;
        }

        case OP_MAKE_MAP: {
            int pairs = READ_BYTE();
            Value map = value_map_empty();
            for (int i = 0; i < pairs; i++) {
                Value val = pop(vm);
                Value key = pop(vm);
                map_set(&map, key.as.string, val);
                value_free(key);
            }
            push(vm, map);
            break;
        }

        case OP_GET_INDEX: {
            Value idx = pop(vm);
            Value obj = pop(vm);
            if (obj.type == VAL_ARRAY) {
                int i = (int)idx.as.number;
                push(vm, value_copy(obj.as.obj->as.array.items[i]));
            } else if (obj.type == VAL_MAP) {
                Value *found = map_get(&obj, idx.as.string);
                if (found) push(vm, value_copy(*found));
                else push(vm, value_nil());
            }
            value_free(obj); value_free(idx);
            break;
        }

        case OP_SET_INDEX: {
            Value val = pop(vm);
            Value idx = pop(vm);
            Value obj = pop(vm);
            if (obj.type == VAL_ARRAY) {
                int i = (int)idx.as.number;
                value_free(obj.as.obj->as.array.items[i]);
                obj.as.obj->as.array.items[i] = value_copy(val);
            } else if (obj.type == VAL_MAP) {
                map_set(&obj, idx.as.string, value_copy(val));
            }
            push(vm, val);
            value_free(obj); value_free(idx);
            break;
        }

        default:
            fprintf(stderr, "Unknown opcode %d\n", op);
            return INTERPRET_RUNTIME_ERROR;
        }
    }
}
