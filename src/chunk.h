#ifndef KHAN_CHUNK_H
#define KHAN_CHUNK_H

#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "value.h"

/* ─────────────────────────────────────────────
   Opcode table
   ───────────────────────────────────────────── */
typedef enum {
    /* Literals */
    OP_CONST,           /* push constants[READ_BYTE()] */
    OP_NIL,
    OP_TRUE,
    OP_FALSE,

    /* Arithmetic */
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_NEGATE_NUM,

    /* Comparison */
    OP_EQ,
    OP_NEQ,
    OP_LT,
    OP_LE,
    OP_GT,
    OP_GE,

    /* Logical */
    OP_NOT_BOOL,

    /* Variables */
    OP_GET_LOCAL,       /* operand = stack-slot index (1 byte) */
    OP_SET_LOCAL,
    OP_GET_GLOBAL,      /* operand = constants index of name string (1 byte) */
    OP_SET_GLOBAL,
    OP_DEF_GLOBAL,

    /* Control flow  — 2-byte big-endian offset operands */
    OP_JUMP,            /* unconditional forward jump  */
    OP_JUMP_IF_FALSE,   /* pop + jump forward if false */
    OP_LOOP,            /* unconditional backward jump */

    /* Break / continue helpers */
    OP_BREAK,
    OP_CONTINUE,

    /* Functions */
    OP_CALL,            /* operand = arg count */
    OP_RETURN,

    /* Collections */
    OP_MAKE_ARRAY,      /* operand = element count */
    OP_MAKE_MAP,        /* operand = pair count    */
    OP_GET_INDEX,
    OP_SET_INDEX,

    /* I/O */
    OP_PRINT,

    OP_POP,
} OpCode;

/* ─────────────────────────────────────────────
   A Chunk holds all bytecode for one function
   ───────────────────────────────────────────── */
typedef struct Chunk {
    uint8_t  *code;
    int       count;
    int       capacity;

    int      *lines;       /* parallel to code — source line numbers */

    Value    *constants;
    int       const_count;
    int       const_cap;
} Chunk;

void chunk_init   (Chunk *c);
void chunk_free   (Chunk *c);
void chunk_write  (Chunk *c, uint8_t byte, int line);
int  chunk_add_const(Chunk *c, Value v);  /* returns constant index */

/* ─────────────────────────────────────────────
   KhanFunction wraps a Chunk with metadata.
   Stored as a Value (VAL_FUNCTION).
   ───────────────────────────────────────────── */
typedef struct KhanFunction {
    char  *name;
    char  *source_dir;     /* Path directory of the .kh file this fn was defined in */
    int    arity;
    Chunk  chunk;
    int    is_native;      /* 0 = Khan bytecode, 1 = C native */
} KhanFunction;

KhanFunction *khanfn_new(const char *name, int arity);
void          khanfn_free(KhanFunction *fn);

#endif /* KHAN_CHUNK_H */