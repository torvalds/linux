/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/include/linux/parser.h
 *
 * Header for lib/parser.c
 * Intended use of these functions is parsing filesystem argument lists,
 * but could potentially be used anywhere else that simple option=arg
 * parsing is required.
 */
#ifndef _LINUX_PARSER_H
#define _LINUX_PARSER_H

/* associates an integer enumerator with a pattern string. */
struct match_token {
	int token;
	const char *pattern;
};

typedef struct match_token match_table_t[];

/* Maximum number of arguments that match_token will find in a pattern */
enum {MAX_OPT_ARGS = 3};

/* Describe the location within a string of a substring */
typedef struct {
	char *from;
	char *to;
} substring_t;

int match_token(char *, const match_table_t table, substring_t args[]);
int match_int(substring_t *, int *result);
int match_u64(substring_t *, u64 *result);
int match_octal(substring_t *, int *result);
int match_hex(substring_t *, int *result);
bool match_wildcard(const char *pattern, const char *str);
size_t match_strlcpy(char *, const substring_t *, size_t);
char *match_strdup(const substring_t *);

#endif /* _LINUX_PARSER_H */
