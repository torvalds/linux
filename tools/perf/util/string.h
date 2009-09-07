#ifndef _PERF_STRING_H_
#define _PERF_STRING_H_

#include "types.h"

int hex2u64(const char *ptr, u64 *val);

#define _STR(x) #x
#define STR(x) _STR(x)

#endif
