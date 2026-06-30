#ifndef KHAN_KHANSTDLIB_H
#define KHAN_KHANSTDLIB_H

#include "interpreter.h"

// Register all standard library functions into the global environment.
void stdlib_register_all(Environment *env);

#endif