#ifndef __PERF_STRING_H_
#define __PERF_STRING_H_

#include "types.h"

int hex2u64(const char *ptr, u64 *val);
char *strxfrchar(char *s, char from, char to);
s64 perf_atoll(const char *str);
char **argv_split(const char *str, int *argcp);
void argv_free(char **argv);

#define _STR(x) #x
#define STR(x) _STR(x)

#endif /* __PERF_STRING_H */
