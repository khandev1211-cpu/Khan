#ifndef KHAN_REQUESTS_LIB_H
#define KHAN_REQUESTS_LIB_H

#include "interpreter.h"

// Registers HTTP request functions:
//   http_get(url)                    -> response map
//   http_post(url, body)             -> response map
//   http_post_json(url, map)         -> response map  (auto-encodes map to JSON)
//   http_put(url, body)              -> response map
//   http_delete(url)                 -> response map
//
// Response map keys: "status" (number), "body" (string), "success" (bool)
void requests_register_all(Environment *env);

#endif
