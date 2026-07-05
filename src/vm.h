#ifndef KHAN_VM_H
#define KHAN_VM_H

#define _POSIX_C_SOURCE 200809L
#include <string.h>   /* strdup */
#include <stdlib.h>   /* malloc, free */
#include "chunk.h"
#include "value.h"

#define VM_STACK_MAX  16384
#define VM_FRAMES_MAX  1024

/* One activation record on the call stack */
typedef struct {
    KhanFunction *fn;
    uint8_t      *ip;       /* instruction pointer into fn->chunk.code */
    Value        *slots;    /* base of this frame's locals on vm.stack  */
} CallFrame;

/* ── Global variable store (Hash Table) ── */
typedef struct {
    char  *key;
    Value  val;
} TableEntry;

typedef struct {
    int         count;
    int         capacity;
    TableEntry *entries;
} Table;

/* ── The VM ── */
typedef struct {
    /* Fields shared with Interpreter for native library compatibility */
    int       had_runtime_error;
    const char *base_path;
    int       is_returning;   /* unused in VM loop directly */
    Value     return_value;   /* unused in VM loop directly */
    int       is_breaking;    /* unused in VM loop directly */
    int       is_continuing;  /* unused in VM loop directly */
    void     *base_env;       /* NULL in VM mode */
    char      current_import_dir[1024];

    /* VM specific fields */
    CallFrame frames[VM_FRAMES_MAX];
    int       frame_count;

    Value     stack[VM_STACK_MAX];
    Value    *stack_top;

    Table     globals;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

void            vm_init(VM *vm);
void            vm_free(VM *vm);

/* Register Khan's stdlib native functions into the VM's global table */
void            vm_register_builtins(VM *vm);
void            vm_global_set_native(VM *vm, const char *name, NativeFn fn);
void            vm_global_set(VM *vm, const char *name, Value val);

void json_register_all_vm(VM *vm);
void datetime_register_all_vm(VM *vm);
void requests_register_all_vm(VM *vm);
void webi_register_all_vm(VM *vm);

/* Run a compiled script function */
InterpretResult vm_run(VM *vm, KhanFunction *script);

/* Call a Khan function by name via the VM */
Value           vm_call_fn(VM *vm, const char *name, int argc, Value *args);

/* Used by compiler to register KhanFunction objects */
void khanfn_register(KhanFunction *fn);
int  khanfn_registry_index(void);

#endif /* KHAN_VM_H */