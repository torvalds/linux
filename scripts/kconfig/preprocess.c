// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2018 Masahiro Yamada <yamada.masahiro@socionext.com>

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"

static void __attribute__((noreturn)) pperror(const char *format, ...)
{
	va_list ap;

	fprintf(stderr, "%s:%d: ", current_file->name, yylineno);
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	fprintf(stderr, "\n");

	exit(1);
}

/*
 * Environment variables
 */
static LIST_HEAD(env_list);

struct env {
	char *name;
	char *value;
	struct list_head node;
};

static void env_add(const char *name, const char *value)
{
	struct env *e;

	e = xmalloc(sizeof(*e));
	e->name = xstrdup(name);
	e->value = xstrdup(value);

	list_add_tail(&e->node, &env_list);
}

static void env_del(struct env *e)
{
	list_del(&e->node);
	free(e->name);
	free(e->value);
	free(e);
}

/* The returned pointer must be freed when done */
static char *env_expand(const char *name)
{
	struct env *e;
	const char *value;

	if (!*name)
		return NULL;

	list_for_each_entry(e, &env_list, node) {
		if (!strcmp(name, e->name))
			return xstrdup(e->value);
	}

	value = getenv(name);
	if (!value)
		return NULL;

	/*
	 * We need to remember all referenced environment variables.
	 * They will be written out to include/config/auto.conf.cmd
	 */
	env_add(name, value);

	return xstrdup(value);
}

void env_write_dep(FILE *f, const char *autoconfig_name)
{
	struct env *e, *tmp;

	list_for_each_entry_safe(e, tmp, &env_list, node) {
		fprintf(f, "ifneq \"$(%s)\" \"%s\"\n", e->name, e->value);
		fprintf(f, "%s: FORCE\n", autoconfig_name);
		fprintf(f, "endif\n");
		env_del(e);
	}
}

static char *eval_clause(const char *str, size_t len)
{
	char *tmp, *name, *res;

	tmp = xstrndup(str, len);

	name = expand_string(tmp);

	res = env_expand(name);
	if (res)
		goto free;

	res = xstrdup("");
free:
	free(name);
	free(tmp);

	return res;
}

/*
 * Expand a string that follows '$'
 *
 * For example, if the input string is
 *     ($(FOO)$($(BAR)))$(BAZ)
 * this helper evaluates
 *     $($(FOO)$($(BAR)))
 * and returns a new string containing the expansion (note that the string is
 * recursively expanded), also advancing 'str' to point to the next character
 * after the corresponding closing parenthesis, in this case, *str will be
 *     $(BAR)
 */
char *expand_dollar(const char **str)
{
	const char *p = *str;
	const char *q;
	int nest = 0;

	/*
	 * In Kconfig, variable references always start with "$(".
	 * Neither single-letter variables as in $A nor curly braces as in ${CC}
	 * are supported.  '$' not followed by '(' loses its special meaning.
	 */
	if (*p != '(') {
		*str = p;
		return xstrdup("$");
	}

	p++;
	q = p;
	while (*q) {
		if (*q == '(') {
			nest++;
		} else if (*q == ')') {
			if (nest-- == 0)
				break;
		}
		q++;
	}

	if (!*q)
		pperror("unterminated reference to '%s': missing ')'", p);

	/* Advance 'str' to after the expanded initial portion of the string */
	*str = q + 1;

	return eval_clause(p, q - p);
}

static char *__expand_string(const char **str, bool (*is_end)(char c))
{
	const char *in, *p;
	char *expansion, *out;
	size_t in_len, out_len;

	out = xmalloc(1);
	*out = 0;
	out_len = 1;

	p = in = *str;

	while (1) {
		if (*p == '$') {
			in_len = p - in;
			p++;
			expansion = expand_dollar(&p);
			out_len += in_len + strlen(expansion);
			out = xrealloc(out, out_len);
			strncat(out, in, in_len);
			strcat(out, expansion);
			free(expansion);
			in = p;
			continue;
		}

		if (is_end(*p))
			break;

		p++;
	}

	in_len = p - in;
	out_len += in_len;
	out = xrealloc(out, out_len);
	strncat(out, in, in_len);

	/* Advance 'str' to the end character */
	*str = p;

	return out;
}

static bool is_end_of_str(char c)
{
	return !c;
}

/*
 * Expand variables in the given string.  Undefined variables
 * expand to an empty string.
 * The returned string must be freed when done.
 */
char *expand_string(const char *in)
{
	return __expand_string(&in, is_end_of_str);
}

static bool is_end_of_token(char c)
{
	/* Why are '.' and '/' valid characters for symbols? */
	return !(isalnum(c) || c == '_' || c == '-' || c == '.' || c == '/');
}

/*
 * Expand variables in a token.  The parsing stops when a token separater
 * (in most cases, it is a whitespace) is encountered.  'str' is updated to
 * point to the next character.
 *
 * The returned string must be freed when done.
 */
char *expand_one_token(const char **str)
{
	return __expand_string(str, is_end_of_token);
}
