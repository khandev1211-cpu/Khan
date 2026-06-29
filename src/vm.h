#ifndef KHAN_VM_H
#define KHAN_VM_H

#define _POSIX_C_SOURCE 200809L
#include <string.h>   /* strdup */
#include <stdlib.h>   /* malloc, free */
#include "chunk.h"
#include "value.h"

#define VM_STACK_MAX  2048
#define VM_FRAMES_MAX  256

/* One activation record on the call stack */
typedef struct {
    KhanFunction *fn;
    uint8_t      *ip;       /* instruction pointer into fn->chunk.code */
    Value        *slots;    /* base of this frame's locals on vm.stack  */
} CallFrame;

/* ── Global variable store ── */
#define VM_GLOBALS_MAX 512
typedef struct {
    char  *key;
    Value  val;
} GlobalEntry;

/* ── The VM ── */
typedef struct {
    CallFrame frames[VM_FRAMES_MAX];
    int       frame_count;

    Value     stack[VM_STACK_MAX];
    Value    *stack_top;

    GlobalEntry globals[VM_GLOBALS_MAX];
    int         global_count;
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

/* Run a compiled script function */
InterpretResult vm_run(VM *vm, KhanFunction *script);

/* Used by compiler to register KhanFunction objects */
void khanfn_register(KhanFunction *fn);
int  khanfn_registry_index(void);

#endif /* KHAN_VM_H */