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
        if (table->entries[i].key) {
            free(table->entries[i].key);
            value_free(table->entries[i].val);
        }
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
   ══════════════════════════════════════════════════════════════
   push()/pop() previously had no bounds checking at all — writing
   past the end of the fixed-size `stack[VM_STACK_MAX]` array (or
   reading before its start) is undefined behavior, not a clean
   error. Verified reproducible as a raw SIGSEGV with no diagnostic
   whatsoever via deep recursion through a function with many locals
   per frame (frame_count's own VM_FRAMES_MAX check doesn't help here,
   since frame_count * locals-per-frame can exceed VM_STACK_MAX well
   before frame_count itself reaches its limit).

   These now fail safely: a clear message to stderr and a clean exit,
   rather than corrupting adjacent memory. This can't be threaded back
   as a normal catchable Khan-level runtime error without changing the
   signature (and every call site) of push()/pop() throughout run_loop
   — a much larger, riskier change. A hard, clearly-diagnosed abort is
   a large, deliberate improvement over undefined behavior even without
   that; it's the same tradeoff many embedded VMs make for genuine
   resource exhaustion (as opposed to an ordinary, recoverable runtime
   error like "undefined variable"). */
static inline void push(VM *vm, Value v) {
    if (vm->stack_top >= vm->stack + VM_STACK_MAX) {
        fprintf(stderr, "Fatal: stack overflow (exceeded %d values) — likely runaway or "
                         "too-deep recursion\n", VM_STACK_MAX);
        exit(70);
    }
    *vm->stack_top++ = v;
}

static inline Value pop(VM *vm) {
    if (vm->stack_top <= vm->stack) {
        fprintf(stderr, "Fatal: stack underflow — this indicates a compiler bug "
                         "(bytecode popped more values than were pushed)\n");
        exit(70);
    }
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

    /* Stack trace: walk every active call frame, innermost first. Each
       frame's ip already points just past the instruction that's either
       executing now (innermost) or that called into the next frame in
       (every other frame) — either way, offset-1 is the right line. */
    if (vm->frame_count > 1) {
        fprintf(stderr, "Stack trace (most recent call first):\n");
        for (int i = vm->frame_count - 1; i >= 0; i--) {
            CallFrame *cf = &vm->frames[i];
            int coff = (int)(cf->ip - cf->fn->chunk.code) - 1;
            int cline = (coff >= 0 && coff < cf->fn->chunk.count)
                        ? cf->fn->chunk.lines[coff] : 0;
            const char *fname = (cf->fn->name && cf->fn->name[0]) ? cf->fn->name : "<script>";
            fprintf(stderr, "  at %s (line %d)\n", fname, cline);
        }
    }
    return INTERPRET_RUNTIME_ERROR;
}

void vm_global_set_native(VM *vm, const char *name, NativeFn fn) {
    Value v;
    v.type = VAL_NATIVE;
    v.as.native.name     = strdup(name);
    v.as.native.function = fn;
    global_set(vm, name, v);
}

void vm_global_set(VM *vm, const char *name, Value val) {
    global_set(vm, name, val);
}

void vm_init(VM *vm) {
    vm->had_runtime_error = 0;
    vm->base_path = NULL;
    vm->current_import_dir[0] = '\0';
    vm->base_env = NULL;
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

static InterpretResult run_loop(VM *vm, int initial_frame_count) {
    CallFrame *frame = &vm->frames[vm->frame_count - 1];

    for (;;) {
        uint8_t op = READ_BYTE();

        switch (op) {
        case OP_CONST:      push(vm, value_copy(frame->fn->chunk.constants[READ_BYTE()])); break;
        case OP_CONST_WIDE: push(vm, value_copy(frame->fn->chunk.constants[READ_SHORT()])); break;
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
            if (b.as.number == 0.0) { value_free(a); value_free(b); return runtime_error(vm, "Division by zero"); }
            push(vm, value_number(a.as.number / b.as.number));
            value_free(a); value_free(b);
            break;
        }
        case OP_MOD: {
            Value b = pop(vm), a = pop(vm);
            if (b.as.number == 0.0) { value_free(a); value_free(b); return runtime_error(vm, "Modulo by zero"); }
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

        case OP_DEF_GLOBAL:
        case OP_DEF_GLOBAL_WIDE: {
            int name_idx = (op == OP_DEF_GLOBAL) ? READ_BYTE() : READ_SHORT();
            Value name_v = frame->fn->chunk.constants[name_idx];
            Value val = pop(vm);
            const char *gname_for_free = name_v.as.string;

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

                        /* If this function captures free variables from
                           the function currently executing (the one
                           whose body this OP_DEF_GLOBAL lives in), snapshot
                           those values now — this is what makes nested
                           `fn` declarations that reference an enclosing
                           function's parameters/locals actually work. */
                        if (fn_registry[idx]->upvalue_count > 0) {
                            KhanClosure *cl = khanclosure_new(fn_registry[idx]->upvalue_count);
                            for (int u = 0; u < fn_registry[idx]->upvalue_count; u++) {
                                UpvalueDesc *d = &fn_registry[idx]->upvalues[u];
                                if (d->is_local) {
                                    cl->values[u] = value_copy(frame->slots[d->index]);
                                } else {
                                    cl->values[u] = frame->upvalues
                                        ? value_copy(frame->upvalues[d->index])
                                        : value_nil();
                                }
                            }
                            fv.as.function.closure = (Environment*)cl;
                        }

                        // A nested `fn` compiles to OP_DEF_GLOBAL and gets
                        // re-executed every time the enclosing function is
                        // called, redefining the same global name each
                        // time. Without freeing the previous value first,
                        // every call after the first leaked the old
                        // closure (and its captured values) — this is
                        // what showed up as a per-call leak under
                        // valgrind. OP_SET_GLOBAL already did this
                        // correctly; OP_DEF_GLOBAL didn't.
                        Value old;
                        if (global_get(vm, gname_for_free, &old)) {
                            value_free(old);
                        }
                        global_set(vm, gname, fv);
                        value_free(val);
                        break;
                    }
                }
            }
            {
                Value old;
                if (global_get(vm, gname_for_free, &old)) {
                    value_free(old);
                }
            }
            global_set(vm, name_v.as.string, val);
            break;
        }

        case OP_GET_GLOBAL:
        case OP_GET_GLOBAL_WIDE: {
            int name_idx = (op == OP_GET_GLOBAL) ? READ_BYTE() : READ_SHORT();
            Value name_v = frame->fn->chunk.constants[name_idx];
            Value val;
            if (!global_get(vm, name_v.as.string, &val)) {
                char msg[256]; snprintf(msg, sizeof(msg), "Undefined variable '%s'", name_v.as.string);
                return runtime_error(vm, msg);
            }
            push(vm, value_copy(val));
            break;
        }

        case OP_SET_GLOBAL:
        case OP_SET_GLOBAL_WIDE: {
            int name_idx = (op == OP_SET_GLOBAL) ? READ_BYTE() : READ_SHORT();
            Value name_v = frame->fn->chunk.constants[name_idx];
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

        case OP_GET_UPVALUE: {
            uint8_t slot = READ_BYTE();
            Value v = frame->upvalues ? frame->upvalues[slot] : value_nil();
            push(vm, value_copy(v));
            break;
        }
        case OP_SET_UPVALUE: {
            uint8_t slot = READ_BYTE();
            if (frame->upvalues) {
                value_free(frame->upvalues[slot]);
                frame->upvalues[slot] = value_copy(peek(vm, 0));
            }
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
                nfn(&result, (Interpreter*)vm, arg_count, args);
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
                new_frame->upvalues = callee.as.function.closure
                    ? ((KhanClosure*)callee.as.function.closure)->values
                    : NULL;
                frame = new_frame;
                break;
            }
            char msg[128];
            snprintf(msg, sizeof(msg), "Can only call functions (got type %d)", callee.type);
            return runtime_error(vm, msg);
        }

        case OP_RETURN: {
            Value result = pop(vm);

            // Free any locals still sitting in this frame's stack region
            // before truncating it. `result` is a value_copy'd (properly
            // retained) independent reference, so freeing everything in
            // this range is always safe — it can't double-free `result`
            // because `result` was already popped off before this loop
            // runs and isn't in this range.
            //
            // This also frees the callee slot itself (frame->slots - 1),
            // which OP_CALL populated with a retained value_copy of the
            // function/closure being invoked — previously abandoned
            // without freeing on every single call, leaking the callee's
            // name string at minimum and its closure captures for any
            // higher-order call. The top-level script frame has no such
            // slot (nothing is pushed below it), so it's excluded.
            //
            // Without any of this, any local still "in scope" at the
            // point of return (i.e. not already popped by a natural
            // end_scope()) leaked its underlying allocation forever —
            // most visible with closures/arrays/maps, since plain
            // numbers/bools don't own any heap memory to leak.
            Value *free_from = (vm->frame_count > 1) ? frame->slots - 1 : frame->slots;
            for (Value *slot = free_from; slot < vm->stack_top; slot++) {
                value_free(*slot);
            }

            // Clean up the stack: pop function and arguments
            vm->stack_top = frame->slots - 1;

            vm->frame_count--;
            if (vm->frame_count < initial_frame_count) {
                push(vm, result);
                return INTERPRET_OK;
            }

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
                int count = AS_ARRAY_COUNT(obj);
                if (i < 0 || i >= count) {
                    value_free(obj); value_free(idx);
                    return runtime_error(vm, "Array index out of bounds");
                }
                push(vm, value_copy(AS_ARRAY_ITEMS(obj)[i]));
            } else if (obj.type == VAL_MAP) {
                if (idx.type != VAL_STRING) {
                    value_free(obj); value_free(idx);
                    return runtime_error(vm, "Map index must be a string");
                }
                Value *found = map_get(&obj, idx.as.string);
                if (found) push(vm, value_copy(*found));
                else push(vm, value_nil());
            } else {
                value_free(obj); value_free(idx);
                return runtime_error(vm, "Can only index arrays and maps");
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
                int count = AS_ARRAY_COUNT(obj);
                if (i < 0 || i >= count) {
                    value_free(obj); value_free(idx); value_free(val);
                    return runtime_error(vm, "Array index out of bounds");
                }
                value_free(AS_ARRAY_ITEMS(obj)[i]);
                AS_ARRAY_ITEMS(obj)[i] = value_copy(val);
            } else if (obj.type == VAL_MAP) {
                if (idx.type != VAL_STRING) {
                    value_free(obj); value_free(idx); value_free(val);
                    return runtime_error(vm, "Map index must be a string");
                }
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

InterpretResult vm_run(VM *vm, KhanFunction *script) {
    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->fn    = script;
    frame->ip    = script->chunk.code;
    frame->slots = vm->stack_top;
    frame->upvalues = NULL;
    return run_loop(vm, 1);
}

Value vm_call_fn(VM *vm, const char *name, int argc, Value *args) {
    Value fn_val;
    if (!global_get(vm, name, &fn_val)) {
        fprintf(stderr, "[VM] vm_call_fn: function '%s' not found\n", name);
        return value_nil();
    }
    if (fn_val.type != VAL_FUNCTION) {
        fprintf(stderr, "[VM] vm_call_fn: '%s' is not a function (type %d)\n", name, fn_val.type);
        return value_nil();
    }

    /* Stack order MUST be: [fn][arg1][arg2]... */
    push(vm, value_copy(fn_val));
    for (int i = 0; i < argc; i++) push(vm, value_copy(args[i]));

    KhanFunction *fn = (KhanFunction*)fn_val.as.function.body;
    if (fn->arity != argc) {
        fprintf(stderr, "[VM] vm_call_fn: '%s' expects %d args, got %d\n", name, fn->arity, argc);
        // Clean up stack
        for (int i = 0; i <= argc; i++) value_free(pop(vm));
        return value_nil();
    }

    CallFrame *new_frame = &vm->frames[vm->frame_count++];
    new_frame->fn    = fn;
    new_frame->ip    = fn->chunk.code;
    new_frame->slots = vm->stack_top - argc;
    new_frame->upvalues = fn_val.as.function.closure
        ? ((KhanClosure*)fn_val.as.function.closure)->values
        : NULL;

    int initial_frame_count = vm->frame_count;
    InterpretResult res = run_loop(vm, initial_frame_count);

    if (res != INTERPRET_OK) {
        // Error occurred. run_loop exited early.
        // We must manually pop the function and args to prevent stack leaks.
        while (vm->frame_count >= initial_frame_count) {
             CallFrame *f = &vm->frames[--vm->frame_count];
             for (Value *slot = f->slots; slot < vm->stack_top; slot++) {
                 value_free(*slot);
             }
             vm->stack_top = f->slots - 1;
        }
        return value_nil();
    }

    return pop(vm);
}
