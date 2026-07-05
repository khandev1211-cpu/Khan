#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"
#include "ast.h"
#include "chunk.h"
#include "value.h"
#include "lexer.h"
#include "parser.h"

/* ══════════════════════════════════════════════════════════════
   Compiler state
   ══════════════════════════════════════════════════════════════ */

#define MAX_LOCALS  256
#define MAX_BREAKS  64
#define MAX_IMPORTED 128

typedef struct {
    char name[256];
    int  depth;
} Local;

/* Per-loop break/continue patch list */
typedef struct {
    int break_patches[MAX_BREAKS];
    int break_count;
    int continue_patches[MAX_BREAKS];
    int continue_count;
    int loop_start;           /* for continue: where to jump back to */
} LoopCtx;

typedef struct CompilerState {
    KhanFunction       *fn;           /* function being compiled */
    Local               locals[MAX_LOCALS];
    int                 local_count;
    int                 scope_depth;  /* 0 = global */
    struct CompilerState *enclosing;

    LoopCtx             loops[16];
    int                 loop_depth;

    int                 had_error;

    /* Import tracking */
    char               *base_path;
    char               *current_import_dir;
    char               *imported_paths[MAX_IMPORTED];
    int                 imported_count;
} CompilerState;

static CompilerState *current = NULL;

/* ── helpers ── */

static void compiler_error(const char *msg, int line) {
    fprintf(stderr, "[line %d] Compile error: %s\n", line, msg);
    if (current) current->had_error = 1;
}

static Chunk *cur_chunk(void) { return &current->fn->chunk; }

static void emit(uint8_t byte, int line) {
    chunk_write(cur_chunk(), byte, line);
}

static void emit2(uint8_t op, uint8_t operand, int line) {
    emit(op, line);
    emit(operand, line);
}

/* emit a 2-byte big-endian jump placeholder; return offset of 1st placeholder */
static int emit_jump(uint8_t op, int line) {
    emit(op, line);
    emit(0xFF, line);
    emit(0xFF, line);
    return cur_chunk()->count - 2;
}

static void patch_jump(int offset) {
    int jump = cur_chunk()->count - offset - 2;
    if (jump > 0xFFFF) {
        compiler_error("Jump too large", 0);
        return;
    }
    cur_chunk()->code[offset]     = (jump >> 8) & 0xFF;
    cur_chunk()->code[offset + 1] = jump & 0xFF;
}

static void emit_loop(int loop_start, int line) {
    emit(OP_LOOP, line);
    int offset = cur_chunk()->count - loop_start + 2;
    emit((offset >> 8) & 0xFF, line);
    emit(offset & 0xFF, line);
}

/* Add a value to the constant pool and emit OP_CONST */
static void emit_const(Value v, int line) {
    int idx = chunk_add_const(cur_chunk(), v);
    emit2(OP_CONST, (uint8_t)idx, line);
}

/* ── Scope helpers ── */

static void begin_scope(void) { current->scope_depth++; }

static void end_scope(int line) {
    current->scope_depth--;
    while (current->local_count > 0 &&
           current->locals[current->local_count - 1].depth > current->scope_depth) {
        emit(OP_POP, line);
        current->local_count--;
    }
}

static int resolve_local(CompilerState *c, const char *name) {
    for (int i = c->local_count - 1; i >= 0; i--)
        if (strcmp(c->locals[i].name, name) == 0)
            return i;
    return -1;
}

static void add_local(const char *name) {
    if (current->local_count >= MAX_LOCALS) {
        compiler_error("Too many local variables", 0);
        return;
    }
    Local *l = &current->locals[current->local_count++];
    strncpy(l->name, name, 255);
    l->name[255] = '\0';
    l->depth = current->scope_depth;
}

/* Emit name string to constants and GET/SET global */
static void emit_global_get(const char *name, int line) {
    int idx = chunk_add_const(cur_chunk(), vm_val_string(name));
    emit2(OP_GET_GLOBAL, (uint8_t)idx, line);
}

static void emit_global_set(const char *name, int line) {
    int idx = chunk_add_const(cur_chunk(), vm_val_string(name));
    emit2(OP_SET_GLOBAL, (uint8_t)idx, line);
}

static void emit_global_def(const char *name, int line) {
    int idx = chunk_add_const(cur_chunk(), vm_val_string(name));
    emit2(OP_DEF_GLOBAL, (uint8_t)idx, line);
}

/* ── CompilerState init/end ── */

static void compiler_state_init(CompilerState *c, KhanFunction *fn,
                                CompilerState *enclosing) {
    c->fn           = fn;
    c->local_count  = 0;
    c->scope_depth  = 0;
    c->enclosing    = enclosing;
    c->loop_depth   = 0;
    c->had_error    = 0;

    if (enclosing) {
        c->base_path = enclosing->base_path;
        c->current_import_dir = enclosing->current_import_dir ? strdup(enclosing->current_import_dir) : NULL;
        c->imported_count = enclosing->imported_count;
        // Share imported paths with parent
    } else {
        c->base_path = NULL;
        c->current_import_dir = NULL;
        c->imported_count = 0;
    }

    if (c->current_import_dir) {
        fn->source_dir = strdup(c->current_import_dir);
    }
}

/* ══════════════════════════════════════════════════════════════
   Forward declarations
   ══════════════════════════════════════════════════════════════ */
static void compile_expr(AstNode *node);
static void compile_stmt(AstNode *node);
static void compile_block(AstNodeList *stmts, int line);

static char *compiler_read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0L, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *buffer = malloc(size + 1);
    size_t n = fread(buffer, 1, size, file);
    buffer[n] = '\0';
    fclose(file);
    return buffer;
}

static void compile_import(const char *path, int line) {
    // Built-in native-only imports (no-op)
    if (strcmp(path, "json") == 0) return;
    if (strcmp(path, "math") == 0) return;

    // Prevent double imports
    for (int i = 0; i < current->imported_count; i++) {
        if (strcmp(current->imported_paths[i], path) == 0) return;
    }
    if (current->imported_count >= MAX_IMPORTED) {
        compiler_error("Too many imports", line);
        return;
    }

    char full_path[1024];
    FILE *file = NULL;

    // Resolve path (logic from interpreter.c)
    if (current->current_import_dir) {
        snprintf(full_path, sizeof(full_path), "%s/%s", current->current_import_dir, path);
        file = fopen(full_path, "rb");
    }
    if (!file && current->base_path) {
        snprintf(full_path, sizeof(full_path), "%s/%s", current->base_path, path);
        file = fopen(full_path, "rb");
    }
    if (!file) {
        file = fopen(path, "rb");
        if (file) strncpy(full_path, path, sizeof(full_path));
    }
    if (!file) {
        // Try package
        int is_pkg = 1;
        for (const char *p = path; *p; p++) if (*p == '/' || *p == '\\' || *p == '.') { is_pkg = 0; break; }
        if (is_pkg) {
            // 1. Try local packages folder
            snprintf(full_path, sizeof(full_path), "packages/%s/%s.kh", path, path);
            file = fopen(full_path, "rb");

            if (!file) {
                // 2. Try home directory cache
                char *home = getenv("USERPROFILE");
                if (!home) home = getenv("HOME");
                if (home) {
#ifdef _WIN32
                    snprintf(full_path, sizeof(full_path), "%s\\.khan\\packages\\%s\\%s.kh", home, path, path);
#else
                    snprintf(full_path, sizeof(full_path), "%s/.khan/packages/%s/%s.kh", home, path, path);
#endif
                    file = fopen(full_path, "rb");
                }
            }
        }
    }

    if (!file) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Could not open import '%s'", path);
        compiler_error(msg, line);
        return;
    }
    fclose(file);

    char *source = compiler_read_file(full_path);
    if (!source) {
        compiler_error("Could not read import file", line);
        return;
    }

    Lexer lexer;
    lexer_init(&lexer, source);
    Parser parser;
    parser_init(&parser, &lexer);
    AstNode *program = parser_parse(&parser);

    if (parser.had_error) {
        compiler_error("Syntax error in import", line);
        free(source);
        return;
    }

    // Mark as imported
    current->imported_paths[current->imported_count++] = strdup(path);

    // Save old import dir
    char *old_dir = current->current_import_dir ? strdup(current->current_import_dir) : NULL;

    // Set new import dir
    char *last_slash = strrchr(full_path, '/');
    char *last_backslash = strrchr(full_path, '\\');
    char *sep = (last_slash > last_backslash) ? last_slash : last_backslash;
    if (sep) {
        int dlen = (int)(sep - full_path);
        if (current->current_import_dir) free(current->current_import_dir);
        current->current_import_dir = malloc(dlen + 1);
        memcpy(current->current_import_dir, full_path, dlen);
        current->current_import_dir[dlen] = '\0';
    }

    // Compile the imported program into the current chunk
    if (program->type == AST_PROGRAM) {
        for (AstNodeList *s = program->data.program_stmts; s; s = s->next)
            compile_stmt(s->node);
    } else {
        compile_stmt(program);
    }

    // Restore old import dir
    if (current->current_import_dir) free(current->current_import_dir);
    current->current_import_dir = old_dir;

    ast_free(program);
    free(source);
}

static void compile_from_import(const char *path, char **names, int name_count, int line) {
    // Built-in special case
    if (strcmp(path, "webi") == 0) {
        for (int i = 0; i < name_count; i++) {
            if (strcmp(names[i], "webi") == 0) {
                compile_import("webi", line);
                return;
            }
        }
    }

    // Selective import logic is complex for the VM because it populates
    // the global hash table directly. For now, we perform a full import
    // to ensure all necessary functions are available to the VM.
    compile_import(path, line);
}

/* ══════════════════════════════════════════════════════════════
   Count items in an AstNodeList
   ══════════════════════════════════════════════════════════════ */
static int list_count(AstNodeList *l) {
    int n = 0;
    while (l) { n++; l = l->next; }
    return n;
}

/* ══════════════════════════════════════════════════════════════
   Expression compilation
   ══════════════════════════════════════════════════════════════ */

static void compile_expr(AstNode *node) {
    if (!node) { emit(OP_NIL, 0); return; }
    int line = node->line;

    switch (node->type) {

    /* ── Literals ── */
    case AST_NUMBER:
        emit_const(vm_val_number(node->data.number_value), line);
        break;

    case AST_STRING:
        emit_const(vm_val_string(node->data.string_value), line);
        break;

    case AST_BOOL:
        emit(node->data.bool_value ? OP_TRUE : OP_FALSE, line);
        break;

    case AST_NIL:
        emit(OP_NIL, line);
        break;

    /* ── Identifier (variable read) ── */
    case AST_IDENTIFIER: {
        const char *name = node->data.name;
        int slot = resolve_local(current, name);
        if (slot >= 0)
            emit2(OP_GET_LOCAL, (uint8_t)slot, line);
        else
            emit_global_get(name, line);
        break;
    }

    /* ── Grouping ── */
    case AST_GROUPING:
        compile_expr(node->data.grouping);
        break;

    /* ── Binary operations ── */
    case AST_BINARY: {
        AstOp op    = node->data.binary.op;
        AstNode *L  = node->data.binary.left;
        AstNode *R  = node->data.binary.right;

        /* Short-circuit AND */
        if (op == OP_AND) {
            compile_expr(L);
            int skip = emit_jump(OP_JUMP_IF_FALSE, line);
            emit(OP_POP, line);
            compile_expr(R);
            patch_jump(skip);
            break;
        }

        /* Short-circuit OR */
        if (op == OP_OR) {
            compile_expr(L);
            int else_j = emit_jump(OP_JUMP_IF_FALSE, line);
            int end_j  = emit_jump(OP_JUMP, line);
            patch_jump(else_j);
            emit(OP_POP, line);
            compile_expr(R);
            patch_jump(end_j);
            break;
        }

        compile_expr(L);
        compile_expr(R);

        switch (op) {
            case OP_PLUS:          emit(OP_ADD, line); break;
            case OP_MINUS:         emit(OP_SUB, line); break;
            case OP_STAR:          emit(OP_MUL, line); break;
            case OP_SLASH:         emit(OP_DIV, line); break;
            case OP_PERCENT:       emit(OP_MOD, line); break;
            case OP_EQUAL_EQUAL:   emit(OP_EQ,  line); break;
            case OP_BANG_EQUAL:    emit(OP_NEQ, line); break;
            case OP_LESS:          emit(OP_LT,  line); break;
            case OP_LESS_EQUAL:    emit(OP_LE,  line); break;
            case OP_GREATER:       emit(OP_GT,  line); break;
            case OP_GREATER_EQUAL: emit(OP_GE,  line); break;
            default:
                compiler_error("Unknown binary operator", line);
        }
        break;
    }

    /* ── Unary operations ── */
    case AST_UNARY:
        compile_expr(node->data.unary.right);
        switch (node->data.unary.op) {
            case OP_NEGATE: emit(OP_NEGATE_NUM, line); break;
            case OP_NOT:    emit(OP_NOT_BOOL,   line); break;
            default:        compiler_error("Unknown unary operator", line);
        }
        break;

    /* ── Array literal ── */
    case AST_ARRAY: {
        int count = 0;
        for (AstNodeList *el = node->data.array_elements; el; el = el->next) {
            compile_expr(el->node);
            count++;
        }
        emit2(OP_MAKE_ARRAY, (uint8_t)count, line);
        break;
    }

    /* ── Map literal ── */
    case AST_MAP: {
        int pairs = 0;
        for (AstNodeList *el = node->data.map_entries; el; el = el->next) {
            AstNode *entry = el->node;   /* AST_MAP_ENTRY */
            compile_expr(entry->data.map_entry.key);
            compile_expr(entry->data.map_entry.value);
            pairs++;
        }
        emit2(OP_MAKE_MAP, (uint8_t)pairs, line);
        break;
    }

    /* ── Index read  arr[i] ── */
    case AST_INDEX:
        compile_expr(node->data.index_expr.object);
        compile_expr(node->data.index_expr.index);
        emit(OP_GET_INDEX, line);
        break;

    /* ── Function call ── */
    case AST_CALL: {
        /* Push callee (looked up by name) */
        emit_global_get(node->data.call.callee, line);

        int argc = 0;
        for (AstNodeList *arg = node->data.call.arguments; arg; arg = arg->next) {
            compile_expr(arg->node);
            argc++;
        }
        emit2(OP_CALL, (uint8_t)argc, line);
        break;
    }

    /* ── Assignment used as expression  (x = value) ── */
    case AST_ASSIGNMENT: {
        compile_expr(node->data.assignment.value);
        const char *name = node->data.assignment.var_name;
        int slot = resolve_local(current, name);
        if (slot >= 0)
            emit2(OP_SET_LOCAL, (uint8_t)slot, line);
        else
            emit_global_set(name, line);
        /* leave value on stack so it can be used as an expression */
        break;
    }

    /* ── Index assignment used as expression  arr[i] = v ── */
    case AST_INDEX_ASSIGN:
        compile_expr(node->data.index_assign.object);
        compile_expr(node->data.index_assign.index);
        compile_expr(node->data.index_assign.value);
        emit(OP_SET_INDEX, line);
        break;

    default:
        compiler_error("Unhandled expression node", line);
        emit(OP_NIL, line);
        break;
    }
}

/* ══════════════════════════════════════════════════════════════
   Statement compilation
   ══════════════════════════════════════════════════════════════ */

static void compile_block(AstNodeList *stmts, int line) {
    begin_scope();
    for (AstNodeList *s = stmts; s; s = s->next)
        compile_stmt(s->node);
    end_scope(line);
}

static void compile_stmt(AstNode *node) {
    if (!node) return;
    int line = node->line;

    switch (node->type) {

    /* ── let x = expr ── */
    case AST_LET_STMT: {
        const char *name = node->data.let_decl.let_name;
        if (node->data.let_decl.let_initializer)
            compile_expr(node->data.let_decl.let_initializer);
        else
            emit(OP_NIL, line);

        if (current->scope_depth > 0) {
            add_local(name);
            /* value on stack IS the local — no extra emit needed */
        } else {
            emit_global_def(name, line);
        }
        break;
    }

    /* ── x = expr (standalone assignment statement) ── */
    case AST_ASSIGNMENT: {
        compile_expr(node->data.assignment.value);
        const char *name = node->data.assignment.var_name;
        int slot = resolve_local(current, name);
        if (slot >= 0)
            emit2(OP_SET_LOCAL, (uint8_t)slot, line);
        else
            emit_global_set(name, line);
        emit(OP_POP, line);
        break;
    }

    /* ── arr[i] = expr (standalone) ── */
    case AST_INDEX_ASSIGN:
        compile_expr(node->data.index_assign.object);
        compile_expr(node->data.index_assign.index);
        compile_expr(node->data.index_assign.value);
        emit(OP_SET_INDEX, line);
        emit(OP_POP, line);
        break;

    /* ── print expr ── */
    case AST_PRINT_STMT:
        compile_expr(node->data.expr);
        emit(OP_PRINT, line);
        break;

    /* ── expr; ── */
    case AST_EXPR_STMT:
        compile_expr(node->data.expr);
        emit(OP_POP, line);
        break;

    /* ── if / elif / else ── */
    case AST_IF_STMT: {
        compile_expr(node->data.if_stmt.condition);
        int then_jump = emit_jump(OP_JUMP_IF_FALSE, line);
        emit(OP_POP, line);

        compile_stmt(node->data.if_stmt.then_branch);

        int else_jump = emit_jump(OP_JUMP, line);
        patch_jump(then_jump);
        emit(OP_POP, line);

        if (node->data.if_stmt.else_branch)
            compile_stmt(node->data.if_stmt.else_branch);

        patch_jump(else_jump);
        break;
    }

    /* ── while condition: body ── */
    case AST_WHILE_STMT: {
        /* Register loop context for break/continue */
        LoopCtx *ctx = &current->loops[current->loop_depth++];
        ctx->break_count    = 0;
        ctx->continue_count = 0;

        int loop_start = cur_chunk()->count;
        ctx->loop_start = loop_start;

        compile_expr(node->data.while_stmt.while_condition);
        int exit_jump = emit_jump(OP_JUMP_IF_FALSE, line);
        emit(OP_POP, line);

        compile_stmt(node->data.while_stmt.while_body);

        /* Patch continue jumps → top of loop */
        for (int i = 0; i < ctx->continue_count; i++) {
            /* emit_loop equivalent for an already-emitted placeholder */
            int off = cur_chunk()->count - ctx->continue_patches[i] + 2;
            /* We emitted OP_JUMP placeholders for continues; patch them */
            cur_chunk()->code[ctx->continue_patches[i]]     = (off >> 8) & 0xFF;
            cur_chunk()->code[ctx->continue_patches[i] + 1] = off & 0xFF;
        }

        emit_loop(loop_start, line);
        patch_jump(exit_jump);
        emit(OP_POP, line);

        /* Patch break jumps → after loop */
        for (int i = 0; i < ctx->break_count; i++)
            patch_jump(ctx->break_patches[i]);

        current->loop_depth--;
        break;
    }

    /* ── for var in iterable: body ── */
    case AST_FOR_STMT: {
        /*
         * Desugar to:
         *   let __arr = iterable
         *   let __i   = 0
         *   while __i < len(__arr):
         *       let var = __arr[__i]
         *       body
         *       __i = __i + 1
         */
        begin_scope();

        /* __arr = iterable */
        compile_expr(node->data.for_stmt.for_iterable);
        add_local("__for_arr");

        /* __i = 0 */
        emit_const(vm_val_number(0), line);
        add_local("__for_i");

        /* Register loop context */
        LoopCtx *ctx = &current->loops[current->loop_depth++];
        ctx->break_count    = 0;
        ctx->continue_count = 0;

        int loop_start = cur_chunk()->count;
        ctx->loop_start = loop_start;

        /* condition: __i < len(__arr) */
        int i_slot   = resolve_local(current, "__for_i");
        int arr_slot = resolve_local(current, "__for_arr");

        emit2(OP_GET_LOCAL, (uint8_t)i_slot,   line);
        emit_global_get("len", line);
        emit2(OP_GET_LOCAL, (uint8_t)arr_slot, line);
        emit2(OP_CALL, 1, line);
        emit(OP_LT, line);

        int exit_jump = emit_jump(OP_JUMP_IF_FALSE, line);
        emit(OP_POP, line);

        /* var = __arr[__i] — in an inner scope */
        begin_scope();
        emit2(OP_GET_LOCAL, (uint8_t)arr_slot, line);
        emit2(OP_GET_LOCAL, (uint8_t)i_slot,   line);
        emit(OP_GET_INDEX, line);
        add_local(node->data.for_stmt.for_var);

        /* compile body (it's a block node) */
        compile_stmt(node->data.for_stmt.for_body);

        end_scope(line);   /* pops loop var */

        /* patch continues */
        for (int i = 0; i < ctx->continue_count; i++) {
            int off = cur_chunk()->count - ctx->continue_patches[i] + 2;
            cur_chunk()->code[ctx->continue_patches[i]]     = (off >> 8) & 0xFF;
            cur_chunk()->code[ctx->continue_patches[i] + 1] = off & 0xFF;
        }

        /* __i = __i + 1 */
        emit2(OP_GET_LOCAL, (uint8_t)i_slot, line);
        emit_const(vm_val_number(1), line);
        emit(OP_ADD, line);
        emit2(OP_SET_LOCAL, (uint8_t)i_slot, line);
        emit(OP_POP, line);

        emit_loop(loop_start, line);
        patch_jump(exit_jump);
        emit(OP_POP, line);

        /* patch breaks */
        for (int i = 0; i < ctx->break_count; i++)
            patch_jump(ctx->break_patches[i]);

        current->loop_depth--;
        end_scope(line);   /* pops __arr and __i */
        break;
    }

    /* ── break ── */
    case AST_BREAK_STMT: {
        if (current->loop_depth == 0) {
            compiler_error("'break' outside loop", line);
            break;
        }
        LoopCtx *ctx = &current->loops[current->loop_depth - 1];
        /* Pop locals that belong to the loop's inner scopes */
        int pops = 0;
        for (int i = current->local_count - 1; i >= 0; i--) {
            if (current->locals[i].depth > ctx->loop_start) pops++;
            else break;
        }
        for (int i = 0; i < pops; i++) emit(OP_POP, line);
        int jp = emit_jump(OP_JUMP, line);
        ctx->break_patches[ctx->break_count++] = jp;
        break;
    }

    /* ── continue ── */
    case AST_CONTINUE_STMT: {
        if (current->loop_depth == 0) {
            compiler_error("'continue' outside loop", line);
            break;
        }
        LoopCtx *ctx = &current->loops[current->loop_depth - 1];
        int pops = 0;
        for (int i = current->local_count - 1; i >= 0; i--) {
            if (current->locals[i].depth > ctx->loop_start) pops++;
            else break;
        }
        for (int i = 0; i < pops; i++) emit(OP_POP, line);
        /* Jump to a placeholder; we patch it after the increment step */
        int jp = emit_jump(OP_JUMP, line);
        ctx->continue_patches[ctx->continue_count++] = jp;
        break;
    }

    /* ── Block ── */
    case AST_BLOCK:
        compile_block(node->data.statements, line);
        break;

    /* ── fn name(params): body ── */
    case AST_FN_DECL: {
        const char *fname = node->data.fn_decl.fn_name;
        int arity = list_count(node->data.fn_decl.params);

        KhanFunction *fn = khanfn_new(fname, arity);

        /* compile function body into a nested compiler state */
        CompilerState fn_state;
        compiler_state_init(&fn_state, fn, current);
        current = &fn_state;

        /* scope_depth = 1 so params are locals */
        current->scope_depth = 1;

        for (AstNodeList *p = node->data.fn_decl.params; p; p = p->next)
            add_local(p->node->data.name);

        /* body is an AST_BLOCK */
        AstNode *body = node->data.fn_decl.fn_body;
        if (body->type == AST_BLOCK) {
            for (AstNodeList *s = body->data.statements; s; s = s->next)
                compile_stmt(s->node);
        } else {
            compile_stmt(body);
        }

        /* implicit nil return */
        emit(OP_NIL, line);
        emit(OP_RETURN, line);

        int fn_had_error = current->had_error;
        current = fn_state.enclosing;
        if (fn_had_error) current->had_error = 1;

        /* Register compiled function; emit its index so VM finds it */
        khanfn_register(fn);
        int fn_idx = khanfn_registry_index();

        emit_const(value_number((double)fn_idx), line);
        int name_idx = chunk_add_const(cur_chunk(), value_string(fname));
        emit2(OP_DEF_GLOBAL, (uint8_t)name_idx, line);
        break;
    }

    /* ── return ── */
    case AST_RETURN_STMT:
        if (node->data.expr)
            compile_expr(node->data.expr);
        else
            emit(OP_NIL, line);
        emit(OP_RETURN, line);
        break;

    /* ── import — handled at parse time; skip in VM mode ── */
    case AST_IMPORT_STMT:
        compile_import(node->data.import_path, line);
        break;

    case AST_FROM_IMPORT_STMT:
        compile_from_import(node->data.from_import.path,
                            node->data.from_import.names,
                            node->data.from_import.name_count, line);
        break;

    /* ── program root ── */
    case AST_PROGRAM:
        for (AstNodeList *s = node->data.program_stmts; s; s = s->next)
            compile_stmt(s->node);
        break;

    default:
        compiler_error("Unhandled statement node", line);
        break;
    }
}

/* ══════════════════════════════════════════════════════════════
   Public entry point
   ══════════════════════════════════════════════════════════════ */

KhanFunction *compile(AstNode *program, const char *base_path) {
    KhanFunction *script = khanfn_new("<script>", 0);

    CompilerState state;
    compiler_state_init(&state, script, NULL);

    // Set base path dir
    if (base_path) {
        char *last_slash = strrchr(base_path, '/');
        char *last_backslash = strrchr(base_path, '\\');
        char *sep = (last_slash > last_backslash) ? last_slash : last_backslash;
        if (sep) {
            int len = (int)(sep - base_path);
            state.base_path = malloc(len + 1);
            memcpy(state.base_path, base_path, len);
            state.base_path[len] = '\0';

            // Also set as source_dir for the top-level script
            if (script->source_dir) free(script->source_dir);
            script->source_dir = strdup(state.base_path);
        }
    }

    current = &state;

    /* compile all top-level statements */
    if (program->type == AST_PROGRAM) {
        for (AstNodeList *s = program->data.program_stmts; s; s = s->next)
            compile_stmt(s->node);
    } else {
        compile_stmt(program);
    }

    /* implicit nil return from script */
    emit(OP_NIL, 0);
    emit(OP_RETURN, 0);

    current = NULL;

    if (state.had_error) {
        khanfn_free(script);
        return NULL;
    }
    return script;
}