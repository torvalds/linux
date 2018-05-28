// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2018 Masahiro Yamada <yamada.masahiro@socionext.com>

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"

#define ARRAY_SIZE(arr)		(sizeof(arr) / sizeof((arr)[0]))

static char *expand_string_with_args(const char *in, int argc, char *argv[]);

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

/*
 * Built-in functions
 */
struct function {
	const char *name;
	unsigned int min_args;
	unsigned int max_args;
	char *(*func)(int argc, char *argv[]);
};

static char *do_shell(int argc, char *argv[])
{
	FILE *p;
	char buf[256];
	char *cmd;
	size_t nread;
	int i;

	cmd = argv[0];

	p = popen(cmd, "r");
	if (!p) {
		perror(cmd);
		exit(1);
	}

	nread = fread(buf, 1, sizeof(buf), p);
	if (nread == sizeof(buf))
		nread--;

	/* remove trailing new lines */
	while (buf[nread - 1] == '\n')
		nread--;

	buf[nread] = 0;

	/* replace a new line with a space */
	for (i = 0; i < nread; i++) {
		if (buf[i] == '\n')
			buf[i] = ' ';
	}

	if (pclose(p) == -1) {
		perror(cmd);
		exit(1);
	}

	return xstrdup(buf);
}

static const struct function function_table[] = {
	/* Name		MIN	MAX	Function */
	{ "shell",	1,	1,	do_shell },
};

#define FUNCTION_MAX_ARGS		16

static char *function_expand(const char *name, int argc, char *argv[])
{
	const struct function *f;
	int i;

	for (i = 0; i < ARRAY_SIZE(function_table); i++) {
		f = &function_table[i];
		if (strcmp(f->name, name))
			continue;

		if (argc < f->min_args)
			pperror("too few function arguments passed to '%s'",
				name);

		if (argc > f->max_args)
			pperror("too many function arguments passed to '%s'",
				name);

		return f->func(argc, argv);
	}

	return NULL;
}

/*
 * Evaluate a clause with arguments.  argc/argv are arguments from the upper
 * function call.
 *
 * Returned string must be freed when done
 */
static char *eval_clause(const char *str, size_t len, int argc, char *argv[])
{
	char *tmp, *name, *res, *prev, *p;
	int new_argc = 0;
	char *new_argv[FUNCTION_MAX_ARGS];
	int nest = 0;
	int i;

	tmp = xstrndup(str, len);

	prev = p = tmp;

	/*
	 * Split into tokens
	 * The function name and arguments are separated by a comma.
	 * For example, if the function call is like this:
	 *   $(foo,$(x),$(y))
	 *
	 * The input string for this helper should be:
	 *   foo,$(x),$(y)
	 *
	 * and split into:
	 *   new_argv[0] = 'foo'
	 *   new_argv[1] = '$(x)'
	 *   new_argv[2] = '$(y)'
	 */
	while (*p) {
		if (nest == 0 && *p == ',') {
			*p = 0;
			if (new_argc >= FUNCTION_MAX_ARGS)
				pperror("too many function arguments");
			new_argv[new_argc++] = prev;
			prev = p + 1;
		} else if (*p == '(') {
			nest++;
		} else if (*p == ')') {
			nest--;
		}

		p++;
	}
	new_argv[new_argc++] = prev;

	/*
	 * Shift arguments
	 * new_argv[0] represents a function name or a variable name.  Put it
	 * into 'name', then shift the rest of the arguments.  This simplifies
	 * 'const' handling.
	 */
	name = expand_string_with_args(new_argv[0], argc, argv);
	new_argc--;
	for (i = 0; i < new_argc; i++)
		new_argv[i] = expand_string_with_args(new_argv[i + 1],
						      argc, argv);

	/* Look for built-in functions */
	res = function_expand(name, new_argc, new_argv);
	if (res)
		goto free;

	/* Last, try environment variable */
	if (new_argc == 0) {
		res = env_expand(name);
		if (res)
			goto free;
	}

	res = xstrdup("");
free:
	for (i = 0; i < new_argc; i++)
		free(new_argv[i]);
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
static char *expand_dollar_with_args(const char **str, int argc, char *argv[])
{
	const char *p = *str;
	const char *q;
	int nest = 0;

	/*
	 * In Kconfig, variable/function references always start with "$(".
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

	return eval_clause(p, q - p, argc, argv);
}

char *expand_dollar(const char **str)
{
	return expand_dollar_with_args(str, 0, NULL);
}

static char *__expand_string(const char **str, bool (*is_end)(char c),
			     int argc, char *argv[])
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
			expansion = expand_dollar_with_args(&p, argc, argv);
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
 * Expand variables and functions in the given string.  Undefined variables
 * expand to an empty string.
 * The returned string must be freed when done.
 */
static char *expand_string_with_args(const char *in, int argc, char *argv[])
{
	return __expand_string(&in, is_end_of_str, argc, argv);
}

char *expand_string(const char *in)
{
	return expand_string_with_args(in, 0, NULL);
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
	return __expand_string(str, is_end_of_token, 0, NULL);
}
