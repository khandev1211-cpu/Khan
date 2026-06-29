#ifndef KHAN_VM_VALUE_H
#define KHAN_VM_VALUE_H

/*
 * value.h — VM value type for Khan stack-based VM
 *
 * We reuse the existing Value / ValueType from interpreter.h so
 * the stdlib, JSON, datetime, and requests libraries all keep working
 * without any changes.
 *
 * This header just re-exports those types under the names the VM
 * files use, and adds a few VM-specific helpers.
 */

#include "interpreter.h"   /* Value, ValueType, VAL_* */

/* ── Constructors ── */
static inline Value vm_val_nil(void) {
    Value v; v.type = VAL_NIL; return v;
}
static inline Value vm_val_bool(int b) {
    Value v; v.type = VAL_BOOL; v.as.boolean = b; return v;
}
static inline Value vm_val_number(double n) {
    Value v; v.type = VAL_NUMBER; v.as.number = n; return v;
}

/* heap-allocates a copy of s */
Value vm_val_string(const char *s);

/* ── Truthiness ── */
int vm_is_truthy(Value v);

/* ── Equality ── */
int vm_values_equal(Value a, Value b);

/* ── Print (without trailing newline) ── */
void vm_print_value(Value v);

#endif /* KHAN_VM_VALUE_H */