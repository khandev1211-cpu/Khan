#ifndef KHAN_JSON_LIB_H
#define KHAN_JSON_LIB_H

#include "interpreter.h"

// Registers json_encode() and json_decode() as native builtins.
void json_register_all(Environment *env);

#endif
