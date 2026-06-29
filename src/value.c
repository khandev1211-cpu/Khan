#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "value.h"

Value vm_val_string(const char *s) {
    Value v;
    v.type = VAL_STRING;
    v.as.string = strdup(s);
    return v;
}

int vm_is_truthy(Value v) {
    switch (v.type) {
        case VAL_NIL:    return 0;
        case VAL_BOOL:   return v.as.boolean;
        case VAL_NUMBER: return v.as.number != 0.0;
        case VAL_STRING: return v.as.string && v.as.string[0] != '\0';
        case VAL_ARRAY:  return 1;
        case VAL_MAP:    return 1;
        default:         return 1;
    }
}

int vm_values_equal(Value a, Value b) {
    if (a.type != b.type) return 0;
    switch (a.type) {
        case VAL_NIL:    return 1;
        case VAL_BOOL:   return a.as.boolean == b.as.boolean;
        case VAL_NUMBER: return a.as.number  == b.as.number;
        case VAL_STRING: return strcmp(a.as.string, b.as.string) == 0;
        default:         return 0;
    }
}

void vm_print_value(Value v) {
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
            for (int i = 0; i < v.as.array.count; i++) {
                if (i > 0) printf(", ");
                vm_print_value(v.as.array.items[i]);
            }
            printf("]");
            break;
        }
        case VAL_MAP: {
            printf("{");
            for (int i = 0; i < v.as.map.count; i++) {
                if (i > 0) printf(", ");
                printf("\"%s\": ", v.as.map.entries[i].key);
                vm_print_value(v.as.map.entries[i].value);
            }
            printf("}");
            break;
        }
        case VAL_FUNCTION: printf("<fn %s>", v.as.function.name ? v.as.function.name : "?"); break;
        case VAL_NATIVE:   printf("<native %s>", v.as.native.name ? v.as.native.name : "?"); break;
        default:           printf("<value>"); break;
    }
}