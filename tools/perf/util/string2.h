/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_STRING_H
#define PERF_STRING_H

#include <linux/types.h>
#include <stddef.h>
#include <string.h>

s64 perf_atoll(const char *str);
char **argv_split(const char *str, int *argcp);
void argv_free(char **argv);
bool strglobmatch(const char *str, const char *pat);
bool strglobmatch_nocase(const char *str, const char *pat);
bool strlazymatch(const char *str, const char *pat);
static inline bool strisglob(const char *str)
{
	return strpbrk(str, "*?[") != NULL;
}
int strtailcmp(const char *s1, const char *s2);
char *strxfrchar(char *s, char from, char to);

char *ltrim(char *s);
char *rtrim(char *s);

static inline char *trim(char *s)
{
	return ltrim(rtrim(s));
}

char *asprintf_expr_inout_ints(const char *var, bool in, size_t nints, int *ints);

static inline char *asprintf_expr_in_ints(const char *var, size_t nints, int *ints)
{
	return asprintf_expr_inout_ints(var, true, nints, ints);
}

static inline char *asprintf_expr_not_in_ints(const char *var, size_t nints, int *ints)
{
	return asprintf_expr_inout_ints(var, false, nints, ints);
}

char *strpbrk_esc(char *str, const char *stopset);
char *strdup_esc(const char *str);

#endif /* PERF_STRING_H */
