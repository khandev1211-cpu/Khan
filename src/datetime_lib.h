#ifndef KHAN_DATETIME_LIB_H
#define KHAN_DATETIME_LIB_H

#include "interpreter.h"

// Registers datetime functions:
//   now()               -> map with year/month/day/hour/minute/second/weekday/timestamp
//   date_format(map, fmt) -> string (strftime-style)
//   date_parse(str, fmt)  -> map (parses a date string)
//   date_diff(map1, map2) -> number (difference in seconds)
//   timestamp()          -> number (Unix timestamp as float)
void datetime_register_all(Environment *env);

#endif
