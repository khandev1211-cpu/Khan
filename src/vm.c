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

/* Safe strdup — avoids _POSIX_C_SOURCE dependency issues on MinGW/GCC16 */
static char *vm_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}


/* ══════════════════════════════════════════════════════════════
   KhanFunction registry
   (compiler registers functions here; VM looks them up by index)
   ══════════════════════════════════════════════════════════════ */

#define KHANFN_REGISTRY_MAX 1024
static KhanFunction *fn_registry[KHANFN_REGISTRY_MAX];
static int           fn_registry_count = 0;

void khanfn_register(KhanFunction *fn) {
    if (fn_registry_count < KHANFN_REGISTRY_MAX)
        fn_registry[fn_registry_count++] = fn;
}

/* returns the index of the last registered function (0-based) */
int khanfn_registry_index(void) {
    return fn_registry_count - 1;
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
   Global variable table
   ══════════════════════════════════════════════════════════════ */

static void global_set(VM *vm, const char *key, Value val) {
    for (int i = 0; i < vm->global_count; i++) {
        if (strcmp(vm->globals[i].key, key) == 0) {
            vm->globals[i].val = val;
            return;
        }
    }
    if (vm->global_count >= VM_GLOBALS_MAX) {
        fprintf(stderr, "Too many globals\n");
        return;
    }
    vm->globals[vm->global_count].key = vm_strdup(key);
    vm->globals[vm->global_count].val = val;
    vm->global_count++;
}

static int global_get(VM *vm, const char *key, Value *out) {
    for (int i = 0; i < vm->global_count; i++) {
        if (strcmp(vm->globals[i].key, key) == 0) {
            *out = vm->globals[i].val;
            return 1;
        }
    }
    return 0;
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

/* ══════════════════════════════════════════════════════════════
   Native (C) function type
   ══════════════════════════════════════════════════════════════ */

/* VM natives use the same NativeFn signature as interpreter natives */

/* We store native functions in the globals table as VAL_NATIVE,
   using native.function as the C pointer cast to void*, and
   native.name for the function name. */

/* ── Native implementations ── */

#define RET(x) do { *result = (x); return; } while(0)

static void native_len(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 1) RET(vm_val_number(0));
    Value v = args[0];
    if (v.type == VAL_STRING) RET(vm_val_number((double)strlen(v.as.string)));
    if (v.type == VAL_ARRAY)  RET(vm_val_number((double)v.as.array.count));
    if (v.type == VAL_MAP)    RET(vm_val_number((double)v.as.map.count));
    RET(vm_val_number(0));
}
static void native_str(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    char buf[128];
    if (argc < 1) RET(vm_val_string("nil"));
    Value v = args[0];
    switch (v.type) {
        case VAL_NUMBER:
            if (v.as.number == (long long)v.as.number)
                snprintf(buf, sizeof(buf), "%lld", (long long)v.as.number);
            else snprintf(buf, sizeof(buf), "%g", v.as.number);
            RET(vm_val_string(buf));
        case VAL_BOOL:   RET(vm_val_string(v.as.boolean ? "true" : "false"));
        case VAL_NIL:    RET(vm_val_string("nil"));
        case VAL_STRING: RET(vm_val_string(v.as.string));
        default:         RET(vm_val_string("<value>"));
    }
}
static void native_num(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 1) RET(vm_val_number(0));
    Value v = args[0];
    if (v.type == VAL_NUMBER) RET(v);
    if (v.type == VAL_STRING) RET(vm_val_number(atof(v.as.string)));
    RET(vm_val_number(0));
}
static void native_type(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 1) RET(vm_val_string("nil"));
    switch (args[0].type) {
        case VAL_NUMBER:   RET(vm_val_string("number"));
        case VAL_STRING:   RET(vm_val_string("string"));
        case VAL_BOOL:     RET(vm_val_string("bool"));
        case VAL_NIL:      RET(vm_val_string("nil"));
        case VAL_ARRAY:    RET(vm_val_string("array"));
        case VAL_MAP:      RET(vm_val_string("map"));
        case VAL_FUNCTION:
        case VAL_NATIVE:   RET(vm_val_string("function"));
        default:           RET(vm_val_string("unknown"));
    }
}
static void native_push(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 2 || args[0].type != VAL_ARRAY) RET(vm_val_nil());
    int old_count = args[0].as.array.count;
    Value new_arr;
    new_arr.type = VAL_ARRAY;
    new_arr.as.array.count    = old_count + 1;
    new_arr.as.array.capacity = old_count + 1;
    new_arr.as.array.items    = malloc((old_count + 1) * sizeof(Value));
    memcpy(new_arr.as.array.items, args[0].as.array.items, old_count * sizeof(Value));
    new_arr.as.array.items[old_count] = args[1];
    RET(new_arr);
}
static void native_range(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    int start = 0, end = 0;
    if (argc == 1)      { end   = (int)args[0].as.number; }
    else if (argc >= 2) { start = (int)args[0].as.number; end = (int)args[1].as.number; }
    int count = end - start; if (count < 0) count = 0;
    Value arr; arr.type = VAL_ARRAY;
    arr.as.array.count    = count;
    arr.as.array.capacity = count;
    arr.as.array.items    = count > 0 ? malloc(count * sizeof(Value)) : NULL;
    for (int i = 0; i < count; i++)
        arr.as.array.items[i] = vm_val_number((double)(start + i));
    RET(arr);
}
static void native_keys(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_MAP) RET(vm_val_nil());
    int count = args[0].as.map.count;
    Value arr; arr.type = VAL_ARRAY;
    arr.as.array.count    = count;
    arr.as.array.capacity = count;
    arr.as.array.items    = count > 0 ? malloc(count * sizeof(Value)) : NULL;
    for (int i = 0; i < count; i++)
        arr.as.array.items[i] = vm_val_string(args[0].as.map.entries[i].key);
    RET(arr);
}
static void native_has(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 2 || args[0].type != VAL_MAP || args[1].type != VAL_STRING) RET(vm_val_bool(0));
    for (int i = 0; i < args[0].as.map.count; i++)
        if (strcmp(args[0].as.map.entries[i].key, args[1].as.string) == 0) RET(vm_val_bool(1));
    RET(vm_val_bool(0));
}
static void native_abs(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 1) RET(vm_val_number(0));
    RET(vm_val_number(fabs(args[0].as.number)));
}
static void native_sqrt(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 1) RET(vm_val_number(0));
    RET(vm_val_number(sqrt(args[0].as.number)));
}
static void native_floor(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 1) RET(vm_val_number(0));
    RET(vm_val_number(floor(args[0].as.number)));
}
static void native_ceil(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 1) RET(vm_val_number(0));
    RET(vm_val_number(ceil(args[0].as.number)));
}
static void native_round(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 1) RET(vm_val_number(0));
    RET(vm_val_number(round(args[0].as.number)));
}
static void native_pow(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 2) RET(vm_val_number(0));
    RET(vm_val_number(pow(args[0].as.number, args[1].as.number)));
}
static void native_min(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 2) RET(vm_val_number(0));
    RET(vm_val_number(args[0].as.number < args[1].as.number ? args[0].as.number : args[1].as.number));
}
static void native_max(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 2) RET(vm_val_number(0));
    RET(vm_val_number(args[0].as.number > args[1].as.number ? args[0].as.number : args[1].as.number));
}
static void native_random(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp; (void)argc; (void)args;
#ifndef RAND_MAX
#  define RAND_MAX 32767
#endif
    RET(vm_val_number((double)rand() / ((double)RAND_MAX + 1.0)));
}
static void native_clock(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp; (void)argc; (void)args;
    RET(vm_val_number((double)clock() / CLOCKS_PER_SEC));
}
static void native_upper(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_STRING) RET(vm_val_string(""));
    char *s = vm_strdup(args[0].as.string);
    for (int i = 0; s[i]; i++) if (s[i] >= 'a' && s[i] <= 'z') s[i] -= 32;
    Value v; v.type = VAL_STRING; v.as.string = s;
    RET(v);
}
static void native_lower(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_STRING) RET(vm_val_string(""));
    char *s = vm_strdup(args[0].as.string);
    for (int i = 0; s[i]; i++) if (s[i] >= 'A' && s[i] <= 'Z') s[i] += 32;
    Value v; v.type = VAL_STRING; v.as.string = s;
    RET(v);
}
static void native_trim(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_STRING) RET(vm_val_string(""));
    const char *s = args[0].as.string;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    const char *e = s + strlen(s);
    while (e > s && (e[-1]==' '||e[-1]=='\t'||e[-1]=='\n'||e[-1]=='\r')) e--;
    int len = (int)(e - s);
    char *out = malloc(len + 1);
    memcpy(out, s, len); out[len] = '\0';
    Value v; v.type = VAL_STRING; v.as.string = out;
    RET(v);
}
static void native_contains(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 2 || args[0].type != VAL_STRING || args[1].type != VAL_STRING) RET(vm_val_bool(0));
    RET(vm_val_bool(strstr(args[0].as.string, args[1].as.string) != NULL));
}
static void native_substring(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 3 || args[0].type != VAL_STRING) RET(vm_val_string(""));
    int start = (int)args[1].as.number;
    int len   = (int)args[2].as.number;
    int slen  = (int)strlen(args[0].as.string);
    if (start < 0) start = 0;
    if (start >= slen) RET(vm_val_string(""));
    if (start + len > slen) len = slen - start;
    char *out = malloc(len + 1);
    memcpy(out, args[0].as.string + start, len); out[len] = '\0';
    Value v; v.type = VAL_STRING; v.as.string = out;
    RET(v);
}
static void native_split(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 2 || args[0].type != VAL_STRING || args[1].type != VAL_STRING) RET(vm_val_nil());
    const char *s   = args[0].as.string;
    const char *del = args[1].as.string;
    int dlen = (int)strlen(del);
    Value arr; arr.type = VAL_ARRAY;
    arr.as.array.count = 0; arr.as.array.capacity = 8;
    arr.as.array.items = malloc(8 * sizeof(Value));
    const char *p = s;
    while (1) {
        const char *found = dlen > 0 ? strstr(p, del) : NULL;
        int plen = found ? (int)(found - p) : (int)strlen(p);
        char *piece = malloc(plen + 1);
        memcpy(piece, p, plen); piece[plen] = '\0';
        Value sv; sv.type = VAL_STRING; sv.as.string = piece;
        if (arr.as.array.count >= arr.as.array.capacity) {
            arr.as.array.capacity *= 2;
            arr.as.array.items = realloc(arr.as.array.items, arr.as.array.capacity * sizeof(Value));
        }
        arr.as.array.items[arr.as.array.count++] = sv;
        if (!found) break;
        p = found + dlen;
    }
    RET(arr);
}
static void native_input(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc >= 1 && args[0].type == VAL_STRING) printf("%s", args[0].as.string);
    fflush(stdout);
    char buf[1024];
    if (!fgets(buf, sizeof(buf), stdin)) RET(vm_val_string(""));
    int l = (int)strlen(buf);
    if (l > 0 && buf[l-1] == '\n') buf[--l] = '\0';
    RET(vm_val_string(buf));
}
static void native_read_file(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_STRING) RET(vm_val_nil());
    FILE *f = fopen(args[0].as.string, "rb");
    if (!f) RET(vm_val_nil());
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    char *buf = malloc(sz + 1);
    size_t n = fread(buf, 1, sz, f); buf[n] = '\0'; fclose(f);
    Value v; v.type = VAL_STRING; v.as.string = buf;
    RET(v);
}
static void native_write_file(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 2 || args[0].type != VAL_STRING || args[1].type != VAL_STRING) RET(vm_val_bool(0));
    FILE *f = fopen(args[0].as.string, "wb");
    if (!f) RET(vm_val_bool(0));
    fputs(args[1].as.string, f); fclose(f);
    RET(vm_val_bool(1));
}
static void native_file_exists(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_STRING) RET(vm_val_bool(0));
    FILE *f = fopen(args[0].as.string, "r");
    if (!f) RET(vm_val_bool(0));
    fclose(f); RET(vm_val_bool(1));
}
static void native_exit(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp; (void)result;
    exit((argc >= 1) ? (int)args[0].as.number : 0);
}
static void native_sleep(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 1) RET(vm_val_nil());
#ifdef _WIN32
    Sleep((DWORD)args[0].as.number);
#else
    struct timespec ts;
    long ms = (long)args[0].as.number;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
    RET(vm_val_nil());
}

/* ── Builtin table ── */
typedef struct { const char *name; NativeFn fn; } BuiltinEntry;

static BuiltinEntry builtins[] = {
    {"len",        native_len},
    {"str",        native_str},
    {"num",        native_num},
    {"type",       native_type},
    {"push",       native_push},
    {"range",      native_range},
    {"keys",       native_keys},
    {"has",        native_has},
    {"abs",        native_abs},
    {"sqrt",       native_sqrt},
    {"floor",      native_floor},
    {"ceil",       native_ceil},
    {"round",      native_round},
    {"pow",        native_pow},
    {"min",        native_min},
    {"max",        native_max},
    {"random",     native_random},
    {"clock",      native_clock},
    {"upper",      native_upper},
    {"lower",      native_lower},
    {"trim",       native_trim},
    {"contains",   native_contains},
    {"substring",  native_substring},
    {"split",      native_split},
    {"input",      native_input},
    {"read_file",  native_read_file},
    {"write_file", native_write_file},
    {"file_exists",native_file_exists},
    {"exit",       native_exit},
    {"sleep",      native_sleep},
    {NULL, NULL}
};




void vm_register_builtins(VM *vm) {
    srand((unsigned)time(NULL));
    for (int i = 0; builtins[i].name; i++) {
        Value v;
        v.type = VAL_NATIVE;
        v.as.native.name     = builtins[i].name;
        /* Store the VMNativeFn pointer tagged — we detect it in OP_CALL */
        v.as.native.function = (void*)builtins[i].fn;
        global_set(vm, builtins[i].name, v);
    }
}

/* ══════════════════════════════════════════════════════════════
   VM init / free
   ══════════════════════════════════════════════════════════════ */

void vm_init(VM *vm) {
    vm->stack_top   = vm->stack;
    vm->frame_count = 0;
    vm->global_count = 0;
    memset(vm->globals, 0, sizeof(vm->globals));
}

void vm_free(VM *vm) {
    for (int i = 0; i < vm->global_count; i++)
        free(vm->globals[i].key);
}

/* ══════════════════════════════════════════════════════════════
   Main execution loop
   ══════════════════════════════════════════════════════════════ */

#define READ_BYTE()   (*frame->ip++)
#define READ_SHORT()  (frame->ip += 2, \
                       (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONST()  (frame->fn->chunk.constants[READ_BYTE()])
#define PEEK(d)       peek(vm, d)

InterpretResult vm_run(VM *vm, KhanFunction *script) {
    /* Push a dummy value so frame->slots[-1] is the script itself */
    push(vm, vm_val_nil());

    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->fn    = script;
    frame->ip    = script->chunk.code;
    frame->slots = vm->stack_top;

    for (;;) {
        uint8_t op = READ_BYTE();

        switch (op) {

        /* ── Literals ── */
        case OP_CONST:  push(vm, READ_CONST());    break;
        case OP_NIL:    push(vm, vm_val_nil());    break;
        case OP_TRUE:   push(vm, vm_val_bool(1));  break;
        case OP_FALSE:  push(vm, vm_val_bool(0));  break;
        case OP_POP:    pop(vm);                   break;

        /* ── Unary ── */
        case OP_NEGATE_NUM: {
            Value v = pop(vm);
            if (v.type != VAL_NUMBER) return runtime_error(vm, "Operand must be a number");
            push(vm, vm_val_number(-v.as.number));
            break;
        }
        case OP_NOT_BOOL:
            push(vm, vm_val_bool(!vm_is_truthy(pop(vm))));
            break;

        /* ── Arithmetic / comparison ── */
        case OP_ADD: {
            Value b = pop(vm), a = pop(vm);
            if (a.type == VAL_STRING && b.type == VAL_STRING) {
                int la = (int)strlen(a.as.string);
                int lb = (int)strlen(b.as.string);
                char *s = malloc(la + lb + 1);
                memcpy(s, a.as.string, la);
                memcpy(s + la, b.as.string, lb + 1);
                Value r; r.type = VAL_STRING; r.as.string = s;
                push(vm, r);
            } else if (a.type == VAL_NUMBER && b.type == VAL_NUMBER) {
                push(vm, vm_val_number(a.as.number + b.as.number));
            } else {
                return runtime_error(vm, "Operands must be two numbers or two strings");
            }
            break;
        }

#define NUMERIC_OP(result_fn, op_sym)                                    \
    do {                                                                  \
        Value b = pop(vm), a = pop(vm);                                  \
        if (a.type != VAL_NUMBER || b.type != VAL_NUMBER)                \
            return runtime_error(vm, "Operands must be numbers");        \
        push(vm, result_fn(a.as.number op_sym b.as.number));             \
    } while (0)

        case OP_SUB: NUMERIC_OP(vm_val_number, -); break;
        case OP_MUL: NUMERIC_OP(vm_val_number, *); break;
        case OP_DIV: {
            Value b = pop(vm), a = pop(vm);
            if (b.type == VAL_NUMBER && b.as.number == 0.0)
                return runtime_error(vm, "Division by zero");
            push(vm, vm_val_number(a.as.number / b.as.number));
            break;
        }
        case OP_MOD: {
            Value b = pop(vm), a = pop(vm);
            push(vm, vm_val_number(fmod(a.as.number, b.as.number)));
            break;
        }
        case OP_EQ:  { Value b=pop(vm),a=pop(vm); push(vm,vm_val_bool( vm_values_equal(a,b))); break; }
        case OP_NEQ: { Value b=pop(vm),a=pop(vm); push(vm,vm_val_bool(!vm_values_equal(a,b))); break; }
        case OP_LT:  NUMERIC_OP(vm_val_bool, <);  break;
        case OP_LE:  NUMERIC_OP(vm_val_bool, <=); break;
        case OP_GT:  NUMERIC_OP(vm_val_bool, >);  break;
        case OP_GE:  NUMERIC_OP(vm_val_bool, >=); break;

        /* ── Print ── */
        case OP_PRINT:
            vm_print_value(pop(vm));
            printf("\n");
            break;

        /* ── Globals ── */
        case OP_DEF_GLOBAL: {
            Value name_v = READ_CONST();
            /*
             * Special case: if the value on the stack is a number that
             * corresponds to a fn_registry index, the compiler has stored
             * a KhanFunction there.  Detect by checking if the number is
             * in range AND the stack value is VAL_NUMBER (not a real
             * user number being defined as a global).
             *
             * We use a sentinel: the compiler always emits
             *   OP_CONST <fn_registry_index>
             *   OP_DEF_GLOBAL <name>
             * immediately after compiling a function.  We detect by
             * peeking whether the global name matches a registered fn.
             */
            Value val = pop(vm);
            if (val.type == VAL_NUMBER) {
                int idx = (int)val.as.number;
                if (idx >= 0 && idx < fn_registry_count) {
                    /* Check if the fn in registry has this name */
                    const char *gname = name_v.as.string;
                    if (fn_registry[idx] &&
                        fn_registry[idx]->name &&
                        strcmp(fn_registry[idx]->name, gname) == 0) {
                        /* Wrap KhanFunction as a VAL_FUNCTION value */
                        Value fv;
                        fv.type = VAL_FUNCTION;
                        fv.as.function.name    = fn_registry[idx]->name;
                        fv.as.function.closure = NULL;
                        /* stash fn ptr in body field (void* cast) */
                        fv.as.function.body    = (AstNode*)fn_registry[idx];
                        fv.as.function.params  = NULL;
                        global_set(vm, gname, fv);
                        break;
                    }
                }
            }
            global_set(vm, name_v.as.string, val);
            break;
        }

        case OP_GET_GLOBAL: {
            Value name_v = READ_CONST();
            const char *name = name_v.as.string;
            Value val;
            if (!global_get(vm, name, &val)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Undefined variable '%s'", name);
                return runtime_error(vm, msg);
            }
            push(vm, val);
            break;
        }

        case OP_SET_GLOBAL: {
            Value name_v = READ_CONST();
            global_set(vm, name_v.as.string, peek(vm, 0));
            break;
        }

        /* ── Locals ── */
        case OP_GET_LOCAL: {
            uint8_t slot = READ_BYTE();
            push(vm, frame->slots[slot]);
            break;
        }
        case OP_SET_LOCAL: {
            uint8_t slot = READ_BYTE();
            frame->slots[slot] = peek(vm, 0);
            break;
        }

        /* ── Jumps ── */
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

        /* ── Function call ── */
        case OP_CALL: {
            int arg_count = READ_BYTE();
            Value callee  = peek(vm, arg_count);

            /* Native function (VAL_NATIVE) */
            if (callee.type == VAL_NATIVE) {
                /* Native functions use the interpreter-style signature:
                 * void fn(Value *result, Interpreter *interp, int argc, Value *args)
                 * The VM-registered builtins ignore the interp pointer, so NULL is safe. */
                NativeFn nfn = callee.as.native.function;
                Value *args  = vm->stack_top - arg_count;
                Value result = vm_val_nil();
                nfn(&result, NULL, arg_count, args);
                vm->stack_top -= arg_count + 1;
                push(vm, result);
                break;
            }

            /* Khan bytecode function (VAL_FUNCTION) */
            if (callee.type == VAL_FUNCTION) {
                /* Retrieve KhanFunction from body field */
                KhanFunction *fn = (KhanFunction*)callee.as.function.body;
                if (!fn) return runtime_error(vm, "Invalid function");

                if (arg_count != fn->arity) {
                    char msg[128];
                    snprintf(msg, sizeof(msg),
                             "Expected %d arguments but got %d", fn->arity, arg_count);
                    return runtime_error(vm, msg);
                }
                if (vm->frame_count >= VM_FRAMES_MAX)
                    return runtime_error(vm, "Stack overflow");

                CallFrame *new_frame = &vm->frames[vm->frame_count++];
                new_frame->fn    = fn;
                new_frame->ip    = fn->chunk.code;
                new_frame->slots = vm->stack_top - arg_count;
                frame = new_frame;
                break;
            }

            return runtime_error(vm, "Can only call functions");
        }

        /* ── Return ── */
        case OP_RETURN: {
            Value result = pop(vm);
            vm->frame_count--;
            if (vm->frame_count == 0) {
                pop(vm);   /* pop the script's dummy slot */
                return INTERPRET_OK;
            }
            /* Unwind stack to caller's top */
            vm->stack_top = frame->slots - 1;
            push(vm, result);
            frame = &vm->frames[vm->frame_count - 1];
            break;
        }

        /* ── Collections ── */
        case OP_MAKE_ARRAY: {
            int count = READ_BYTE();
            Value arr;
            arr.type = VAL_ARRAY;
            arr.as.array.count    = count;
            arr.as.array.capacity = count > 0 ? count : 1;
            arr.as.array.items    = malloc(arr.as.array.capacity * sizeof(Value));
            for (int i = count - 1; i >= 0; i--)
                arr.as.array.items[i] = pop(vm);
            push(vm, arr);
            break;
        }

        case OP_MAKE_MAP: {
            int pairs = READ_BYTE();
            Value map;
            map.type = VAL_MAP;
            map.as.map.count    = pairs;
            map.as.map.capacity = pairs > 0 ? pairs : 1;
            map.as.map.entries  = malloc(map.as.map.capacity * sizeof(MapEntry));
            for (int i = pairs - 1; i >= 0; i--) {
                Value val = pop(vm);
                Value key = pop(vm);
                map.as.map.entries[i].key   = vm_strdup(key.as.string);
                map.as.map.entries[i].value = val;
            }
            push(vm, map);
            break;
        }

        case OP_GET_INDEX: {
            Value idx = pop(vm);
            Value obj = pop(vm);

            if (obj.type == VAL_ARRAY) {
                if (idx.type != VAL_NUMBER)
                    return runtime_error(vm, "Array index must be a number");
                int i = (int)idx.as.number;
                if (i < 0 || i >= obj.as.array.count)
                    return runtime_error(vm, "Array index out of bounds");
                push(vm, obj.as.array.items[i]);
            } else if (obj.type == VAL_MAP) {
                if (idx.type != VAL_STRING)
                    return runtime_error(vm, "Map key must be a string");
                Value found = vm_val_nil();
                for (int i = 0; i < obj.as.map.count; i++) {
                    if (strcmp(obj.as.map.entries[i].key, idx.as.string) == 0) {
                        found = obj.as.map.entries[i].value;
                        break;
                    }
                }
                push(vm, found);
            } else {
                return runtime_error(vm, "Can only index arrays or maps");
            }
            break;
        }

        case OP_SET_INDEX: {
            Value val = pop(vm);
            Value idx = pop(vm);
            Value obj = pop(vm);

            if (obj.type == VAL_ARRAY) {
                int i = (int)idx.as.number;
                if (i < 0 || i >= obj.as.array.count)
                    return runtime_error(vm, "Array index out of bounds");
                obj.as.array.items[i] = val;
            } else if (obj.type == VAL_MAP) {
                const char *key = idx.as.string;
                for (int i = 0; i < obj.as.map.count; i++) {
                    if (strcmp(obj.as.map.entries[i].key, key) == 0) {
                        obj.as.map.entries[i].value = val;
                        goto set_index_done;
                    }
                }
                /* New key */
                if (obj.as.map.count >= obj.as.map.capacity) {
                    obj.as.map.capacity = obj.as.map.capacity * 2;
                    obj.as.map.entries  = realloc(obj.as.map.entries,
                                                  obj.as.map.capacity * sizeof(MapEntry));
                }
                obj.as.map.entries[obj.as.map.count].key   = vm_strdup(key);
                obj.as.map.entries[obj.as.map.count].value = val;
                obj.as.map.count++;
                set_index_done:;
            } else {
                return runtime_error(vm, "Can only index-assign arrays or maps");
            }
            break;
        }

        default:
            fprintf(stderr, "Unknown opcode %d\n", op);
            return INTERPRET_RUNTIME_ERROR;
        }
    }
}