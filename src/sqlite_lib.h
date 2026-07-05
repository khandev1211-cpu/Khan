#ifndef KHAN_SQLITE_LIB_H
#define KHAN_SQLITE_LIB_H

#include "interpreter.h"
#include "vm.h"

/*
 * Registers SQLite native functions:
 *   sqlite_open(path)        -> db connection object
 *   sqlite_exec(db, sql)     -> success bool
 *   sqlite_query(db, sql)    -> array of maps
 *   sqlite_close(db)         -> bool
 */
void sqlite_register_all(Environment *env);
void sqlite_register_all_vm(VM *vm);

#endif
