/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_STRING_H
#define PERF_STRING_H

#include <linux/string.h>
#include <linux/types.h>
#include <sys/types.h> // pid_t
#include <stddef.h>
#include <string.h>

extern const char *graph_dotted_line;
extern const char *dots;

s64 perf_atoll(const char *str);
bool strglobmatch(const char *str, const char *pat);
bool strglobmatch_nocase(const char *str, const char *pat);
bool strlazymatch(const char *str, const char *pat);
static inline bool strisglob(const char *str)
{
	return strpbrk(str, "*?[") != NULL;
}
int strtailcmp(const char *s1, const char *s2);

char *asprintf_expr_inout_ints(const char *var, bool in, size_t nints, int *ints);

static inline char *asprintf_expr_in_ints(const char *var, size_t nints, int *ints)
{
	return asprintf_expr_inout_ints(var, true, nints, ints);
}

static inline char *asprintf_expr_not_in_ints(const char *var, size_t nints, int *ints)
{
	return asprintf_expr_inout_ints(var, false, nints, ints);
}

char *asprintf__tp_filter_pids(size_t npids, pid_t *pids);

char *strpbrk_esc(char *str, const char *stopset);
char *strdup_esc(const char *str);
char *strpbrk_esq(char *str, const char *stopset);
char *strdup_esq(const char *str);

unsigned int hex(char c);
char *strreplace_chars(char needle, const char *haystack, const char *replace);

#endif /* PERF_STRING_H */
