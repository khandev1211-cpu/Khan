#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tensor_lib.h"

#define TENSOR_MAX_NDIM 16

/* ── nested-Khan-array <-> flat tensor conversion ──────────────────── */

/* Depth of a valid, perfectly rectangular nested array of numbers (a
   lone number is depth 0). -1 if `v` isn't shaped like that at all —
   ragged arrays are caught separately in nested_validate_shape, once we
   actually know what shape to check against. */
static int nested_depth(Value v) {
    if (v.type == VAL_NUMBER) return 0;
    if (v.type != VAL_ARRAY) return -1;
    int n = AS_ARRAY_COUNT(v);
    if (n == 0) return 1;
    int d = nested_depth(AS_ARRAY_ITEMS(v)[0]);
    if (d < 0) return -1;
    return d + 1;
}

static void nested_fill_shape(Value v, int *shape, int depth) {
    if (depth == 0) return;
    int n = AS_ARRAY_COUNT(v);
    shape[0] = n;
    if (n == 0) { for (int i = 1; i < depth; i++) shape[i] = 0; return; }
    nested_fill_shape(AS_ARRAY_ITEMS(v)[0], shape + 1, depth - 1);
}

static int nested_validate_shape(Value v, const int *shape, int depth) {
    if (depth == 0) return v.type == VAL_NUMBER;
    if (v.type != VAL_ARRAY || AS_ARRAY_COUNT(v) != shape[0]) return 0;
    Value *items = AS_ARRAY_ITEMS(v);
    for (int i = 0; i < shape[0]; i++)
        if (!nested_validate_shape(items[i], shape + 1, depth - 1)) return 0;
    return 1;
}

static void nested_flatten(Value v, int depth, double *out, int *idx) {
    if (depth == 0) { out[(*idx)++] = v.as.number; return; }
    int n = AS_ARRAY_COUNT(v);
    Value *items = AS_ARRAY_ITEMS(v);
    for (int i = 0; i < n; i++) nested_flatten(items[i], depth - 1, out, idx);
}

static Value nested_unflatten(const double *data, const int *shape, int depth, int *idx) {
    if (depth == 0) return value_number(data[(*idx)++]);
    int n = shape[0];
    Value *items = n > 0 ? malloc(sizeof(Value) * (size_t)n) : NULL;
    for (int i = 0; i < n; i++) items[i] = nested_unflatten(data, shape + 1, depth - 1, idx);
    return value_array(items, n);   /* value_array takes ownership of items */
}

void fn_tensor_from_nested(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    *result = value_nil();
    if (argc < 1) {
        fprintf(stderr, "Runtime error: _tensor_from_nested() expects 1 argument\n");
        return;
    }
    if (args[0].type == VAL_TENSOR) { *result = value_copy(args[0]); return; }

    int depth = nested_depth(args[0]);
    if (depth < 0 || depth > TENSOR_MAX_NDIM) {
        fprintf(stderr, "Runtime error: _tensor_from_nested() needs a number or a rectangular "
                        "nested array of numbers (max %d dimensions)\n", TENSOR_MAX_NDIM);
        return;
    }
    int shape[TENSOR_MAX_NDIM];
    nested_fill_shape(args[0], shape, depth);
    if (!nested_validate_shape(args[0], shape, depth)) {
        fprintf(stderr, "Runtime error: _tensor_from_nested() array is not rectangular "
                        "(sub-arrays have different lengths)\n");
        return;
    }
    int size = 1;
    for (int i = 0; i < depth; i++) size *= shape[i];
    double *flat = malloc(sizeof(double) * (size_t)(size > 0 ? size : 1));
    int idx = 0;
    nested_flatten(args[0], depth, flat, &idx);
    *result = value_tensor(flat, shape, depth);
    free(flat);
}

void fn_tensor_to_nested(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    *result = value_nil();
    if (argc < 1 || args[0].type != VAL_TENSOR) {
        fprintf(stderr, "Runtime error: _tensor_to_nested() expects a tensor\n");
        return;
    }
    Obj *t = args[0].as.obj;
    int idx = 0;
    *result = nested_unflatten(t->as.tensor.data, t->as.tensor.shape, t->as.tensor.ndim, &idx);
}

void fn_tensor_shape(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    *result = value_nil();
    if (argc < 1 || args[0].type != VAL_TENSOR) {
        fprintf(stderr, "Runtime error: _tensor_shape() expects a tensor\n");
        return;
    }
    int ndim = AS_TENSOR_NDIM(args[0]);
    Value *items = ndim > 0 ? malloc(sizeof(Value) * (size_t)ndim) : NULL;
    for (int i = 0; i < ndim; i++) items[i] = value_number(AS_TENSOR_SHAPE(args[0])[i]);
    *result = value_array(items, ndim);
}

void fn_tensor_ndim(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    *result = value_nil();
    if (argc < 1 || args[0].type != VAL_TENSOR) {
        fprintf(stderr, "Runtime error: _tensor_ndim() expects a tensor\n");
        return;
    }
    *result = value_number(AS_TENSOR_NDIM(args[0]));
}

void fn_tensor_numel(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    *result = value_nil();
    if (argc < 1 || args[0].type != VAL_TENSOR) {
        fprintf(stderr, "Runtime error: _tensor_numel() expects a tensor\n");
        return;
    }
    *result = value_number(AS_TENSOR_SIZE(args[0]));
}

/* ── elementwise add/sub/mul, with scalar broadcasting ──────────────── */

typedef enum { TOP_ADD, TOP_SUB, TOP_MUL } TensorOp;

static double tensor_apply_op(TensorOp op, double x, double y) {
    switch (op) {
        case TOP_ADD: return x + y;
        case TOP_SUB: return x - y;
        case TOP_MUL: return x * y;
    }
    return 0.0;
}

/* A "scalar" for broadcasting purposes: a plain number, or any tensor
   holding exactly one element (any ndim — a 0-D tensor from sum(), or a
   shape-[1] tensor, are both fine). */
static int tensor_as_scalar(Value v, double *out) {
    if (v.type == VAL_NUMBER) { *out = v.as.number; return 1; }
    if (v.type == VAL_TENSOR && AS_TENSOR_SIZE(v) == 1) { *out = AS_TENSOR_DATA(v)[0]; return 1; }
    return 0;
}

static void tensor_binop(Value *result, const char *name, Value a, Value b, TensorOp op) {
    *result = value_nil();
    int a_is_t = a.type == VAL_TENSOR, b_is_t = b.type == VAL_TENSOR;
    int a_is_n = a.type == VAL_NUMBER, b_is_n = b.type == VAL_NUMBER;
    if (!(a_is_t || a_is_n) || !(b_is_t || b_is_n)) {
        fprintf(stderr, "Runtime error: %s() arguments must be tensors or numbers\n", name);
        return;
    }

    /* same shape -> ordinary elementwise */
    if (a_is_t && b_is_t && AS_TENSOR_NDIM(a) == AS_TENSOR_NDIM(b) &&
        AS_TENSOR_SIZE(a) == AS_TENSOR_SIZE(b) &&
        (AS_TENSOR_NDIM(a) == 0 ||
         memcmp(AS_TENSOR_SHAPE(a), AS_TENSOR_SHAPE(b), sizeof(int) * (size_t)AS_TENSOR_NDIM(a)) == 0)) {
        int n = AS_TENSOR_SIZE(a);
        double *out = malloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
        for (int i = 0; i < n; i++) out[i] = tensor_apply_op(op, AS_TENSOR_DATA(a)[i], AS_TENSOR_DATA(b)[i]);
        *result = value_tensor(out, AS_TENSOR_SHAPE(a), AS_TENSOR_NDIM(a));
        free(out);
        return;
    }

    double scal;
    /* b is a bigger tensor, a broadcasts onto it */
    if (b_is_t && AS_TENSOR_SIZE(b) != 1 && tensor_as_scalar(a, &scal)) {
        int n = AS_TENSOR_SIZE(b);
        double *out = malloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
        for (int i = 0; i < n; i++) out[i] = tensor_apply_op(op, scal, AS_TENSOR_DATA(b)[i]);
        *result = value_tensor(out, AS_TENSOR_SHAPE(b), AS_TENSOR_NDIM(b));
        free(out);
        return;
    }
    /* a is a bigger tensor, b broadcasts onto it */
    if (a_is_t && AS_TENSOR_SIZE(a) != 1 && tensor_as_scalar(b, &scal)) {
        int n = AS_TENSOR_SIZE(a);
        double *out = malloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
        for (int i = 0; i < n; i++) out[i] = tensor_apply_op(op, AS_TENSOR_DATA(a)[i], scal);
        *result = value_tensor(out, AS_TENSOR_SHAPE(a), AS_TENSOR_NDIM(a));
        free(out);
        return;
    }
    /* both single values (including two size-1 tensors with different
       ndim, or two plain numbers) -> a 0-D tensor */
    double sa, sb;
    if (tensor_as_scalar(a, &sa) && tensor_as_scalar(b, &sb)) {
        double out = tensor_apply_op(op, sa, sb);
        *result = value_tensor(&out, NULL, 0);
        return;
    }

    fprintf(stderr, "Runtime error: %s() shape mismatch (%s vs %s)\n", name,
            a_is_t ? "tensor" : "number", b_is_t ? "tensor" : "number");
}

void fn_tensor_add(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 2) { *result = value_nil(); fprintf(stderr, "Runtime error: _tensor_add() expects 2 arguments\n"); return; }
    tensor_binop(result, "_tensor_add", args[0], args[1], TOP_ADD);
}
void fn_tensor_sub(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 2) { *result = value_nil(); fprintf(stderr, "Runtime error: _tensor_sub() expects 2 arguments\n"); return; }
    tensor_binop(result, "_tensor_sub", args[0], args[1], TOP_SUB);
}
void fn_tensor_mul(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 2) { *result = value_nil(); fprintf(stderr, "Runtime error: _tensor_mul() expects 2 arguments\n"); return; }
    tensor_binop(result, "_tensor_mul", args[0], args[1], TOP_MUL);
}

/* ── matmul / transpose (2-D only in this version) ──────────────────── */

void fn_tensor_matmul(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    *result = value_nil();
    if (argc < 2 || args[0].type != VAL_TENSOR || args[1].type != VAL_TENSOR) {
        fprintf(stderr, "Runtime error: _tensor_matmul() expects two tensors\n");
        return;
    }
    if (AS_TENSOR_NDIM(args[0]) != 2 || AS_TENSOR_NDIM(args[1]) != 2) {
        fprintf(stderr, "Runtime error: _tensor_matmul() only supports 2-D tensors in this version\n");
        return;
    }
    int m = AS_TENSOR_SHAPE(args[0])[0], k = AS_TENSOR_SHAPE(args[0])[1];
    int k2 = AS_TENSOR_SHAPE(args[1])[0], n = AS_TENSOR_SHAPE(args[1])[1];
    if (k != k2) {
        fprintf(stderr, "Runtime error: _tensor_matmul() shape mismatch: (%d,%d) x (%d,%d)\n", m, k, k2, n);
        return;
    }
    double *out = malloc(sizeof(double) * (size_t)m * (size_t)n);
    double *ad = AS_TENSOR_DATA(args[0]), *bd = AS_TENSOR_DATA(args[1]);
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            double sum = 0.0;
            for (int t = 0; t < k; t++) sum += ad[i * k + t] * bd[t * n + j];
            out[i * n + j] = sum;
        }
    }
    int shape[2] = { m, n };
    *result = value_tensor(out, shape, 2);
    free(out);
}

void fn_tensor_transpose(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    *result = value_nil();
    if (argc < 1 || args[0].type != VAL_TENSOR || AS_TENSOR_NDIM(args[0]) != 2) {
        fprintf(stderr, "Runtime error: _tensor_transpose() only supports 2-D tensors in this version\n");
        return;
    }
    int m = AS_TENSOR_SHAPE(args[0])[0], n = AS_TENSOR_SHAPE(args[0])[1];
    double *src = AS_TENSOR_DATA(args[0]);
    double *out = malloc(sizeof(double) * (size_t)m * (size_t)n);
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++)
            out[j * m + i] = src[i * n + j];
    int shape[2] = { n, m };
    *result = value_tensor(out, shape, 2);
    free(out);
}

void fn_tensor_reshape(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    *result = value_nil();
    if (argc < 2 || args[0].type != VAL_TENSOR || args[1].type != VAL_ARRAY) {
        fprintf(stderr, "Runtime error: _tensor_reshape() expects (tensor, shape_array)\n");
        return;
    }
    int ndim = AS_ARRAY_COUNT(args[1]);
    if (ndim > TENSOR_MAX_NDIM) {
        fprintf(stderr, "Runtime error: _tensor_reshape() too many dimensions (max %d)\n", TENSOR_MAX_NDIM);
        return;
    }
    int shape[TENSOR_MAX_NDIM];
    int size = 1;
    Value *items = AS_ARRAY_ITEMS(args[1]);
    for (int i = 0; i < ndim; i++) {
        if (items[i].type != VAL_NUMBER) {
            fprintf(stderr, "Runtime error: _tensor_reshape() shape must be an array of numbers\n");
            return;
        }
        shape[i] = (int)items[i].as.number;
        size *= shape[i];
    }
    if (size != AS_TENSOR_SIZE(args[0])) {
        fprintf(stderr, "Runtime error: _tensor_reshape() total size mismatch (%d vs %d)\n",
                size, AS_TENSOR_SIZE(args[0]));
        return;
    }
    *result = value_tensor(AS_TENSOR_DATA(args[0]), shape, ndim);
}

void fn_tensor_sum(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    *result = value_nil();
    if (argc < 1 || args[0].type != VAL_TENSOR) {
        fprintf(stderr, "Runtime error: _tensor_sum() expects a tensor\n");
        return;
    }
    double total = 0.0;
    int n = AS_TENSOR_SIZE(args[0]);
    double *d = AS_TENSOR_DATA(args[0]);
    for (int i = 0; i < n; i++) total += d[i];
    *result = value_tensor(&total, NULL, 0);
}

void fn_tensor_ones_like(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    *result = value_nil();
    if (argc < 1 || args[0].type != VAL_TENSOR) {
        fprintf(stderr, "Runtime error: _tensor_ones_like() expects a tensor\n");
        return;
    }
    int n = AS_TENSOR_SIZE(args[0]);
    double *ones = malloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
    for (int i = 0; i < n; i++) ones[i] = 1.0;
    *result = value_tensor(ones, AS_TENSOR_SHAPE(args[0]), AS_TENSOR_NDIM(args[0]));
    free(ones);
}

void fn_tensor_zeros_like(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    *result = value_nil();
    if (argc < 1 || args[0].type != VAL_TENSOR) {
        fprintf(stderr, "Runtime error: _tensor_zeros_like() expects a tensor\n");
        return;
    }
    /* value_tensor() zero-fills when data == NULL */
    *result = value_tensor(NULL, AS_TENSOR_SHAPE(args[0]), AS_TENSOR_NDIM(args[0]));
}

void tensor_register_all_vm(VM *vm) {
    vm_global_set_native(vm, "_tensor_from_nested", fn_tensor_from_nested);
    vm_global_set_native(vm, "_tensor_to_nested",   fn_tensor_to_nested);
    vm_global_set_native(vm, "_tensor_shape",       fn_tensor_shape);
    vm_global_set_native(vm, "_tensor_ndim",        fn_tensor_ndim);
    vm_global_set_native(vm, "_tensor_numel",       fn_tensor_numel);
    vm_global_set_native(vm, "_tensor_add",         fn_tensor_add);
    vm_global_set_native(vm, "_tensor_sub",         fn_tensor_sub);
    vm_global_set_native(vm, "_tensor_mul",         fn_tensor_mul);
    vm_global_set_native(vm, "_tensor_matmul",      fn_tensor_matmul);
    vm_global_set_native(vm, "_tensor_transpose",   fn_tensor_transpose);
    vm_global_set_native(vm, "_tensor_reshape",     fn_tensor_reshape);
    vm_global_set_native(vm, "_tensor_sum",         fn_tensor_sum);
    vm_global_set_native(vm, "_tensor_ones_like",   fn_tensor_ones_like);
    vm_global_set_native(vm, "_tensor_zeros_like",  fn_tensor_zeros_like);
}
