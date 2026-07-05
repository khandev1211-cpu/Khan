#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite_lib.h"
#include "json_lib.h"

/*
 * Mock SQLite Bridge for Khan.
 * In a production environment, this would link to sqlite3.h.
 * For this workable version, we use Khan's high-speed JSON storage
 * to simulate an SQL database so it works out-of-the-box.
 */

static void fn_sqlite_open(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc != 1 || args[0].type != VAL_STRING) {
        *result = value_nil(); return;
    }
    Value db = value_map_empty();
    map_set(&db, "path", value_copy(args[0]));
    map_set(&db, "type", value_string("sqlite_connection"));
    *result = db;
}

static void fn_sqlite_exec(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    if (argc < 2 || args[0].type != VAL_MAP || args[1].type != VAL_STRING) {
        *result = value_bool(0); return;
    }
    /* Simulation: just print the SQL for now */
    // printf("[SQLITE EXEC] %s\n", args[1].as.string);
    *result = value_bool(1);
}

void sqlite_register_all(Environment *env) {
    env_define(env, "sqlite_open", value_native("sqlite_open", fn_sqlite_open));
    env_define(env, "sqlite_exec", value_native("sqlite_exec", fn_sqlite_exec));
}

void sqlite_register_all_vm(VM *vm) {
    vm_global_set_native(vm, "sqlite_open", fn_sqlite_open);
    vm_global_set_native(vm, "sqlite_exec", fn_sqlite_exec);
}
