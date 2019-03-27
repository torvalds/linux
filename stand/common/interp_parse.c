/*-
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * Jordan K. Hubbard
 * 29 August 1998
 *
 * The meat of the simple parser.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>
#include <string.h>
#include "bootstrap.h"

static void	 clean(void);
static int	 insert(int *argcp, char *buf);
static char	*variable_lookup(char *name);

#define PARSE_BUFSIZE	1024	/* maximum size of one element */
#define MAXARGS		20	/* maximum number of elements */
static char		*args[MAXARGS];

/*
 * parse: accept a string of input and "parse" it for backslash
 * substitutions and environment variable expansions (${var}),
 * returning an argc/argv style vector of whitespace separated
 * arguments.  Returns 0 on success, 1 on failure (ok, ok, so I
 * wimped-out on the error codes! :).
 *
 * Note that the argv array returned must be freed by the caller, but
 * we own the space allocated for arguments and will free that on next
 * invocation.  This allows argv consumers to modify the array if
 * required.
 *
 * NB: environment variables that expand to more than one whitespace
 * separated token will be returned as a single argv[] element, not
 * split in turn.  Expanded text is also immune to further backslash
 * elimination or expansion since this is a one-pass, non-recursive
 * parser.  You didn't specify more than this so if you want more, ask
 * me. - jkh
 */

#define PARSE_FAIL(expr) \
	if (expr) { \
		printf("fail at line %d\n", __LINE__); \
		clean(); \
		free(copy); \
		free(buf); \
		return 1; \
	}

/* Accept the usual delimiters for a variable, returning counterpart */
static char
isdelim(int ch)
{

	if (ch == '{')
		return '}';
	else if (ch == '(')
		return ')';
	return '\0';
}

static int
isquote(int ch)
{

	return (ch == '\'');
}

static int
isdquote(int ch)
{

	return (ch == '"');
}

int
parse(int *argc, char ***argv, const char *str)
{
	int ac;
	char *val, *p, *q, *copy = NULL;
	size_t i = 0;
	char token, tmp, quote, dquote, *buf;
	enum { STR, VAR, WHITE } state;

	ac = *argc = 0;
	dquote = quote = 0;
	if (!str || (p = copy = backslash(str)) == NULL)
		return 1;

	/* Initialize vector and state */
	clean();
	state = STR;
	buf = (char *)malloc(PARSE_BUFSIZE);
	token = 0;

	/* And awaaaaaaaaay we go! */
	while (*p) {
		switch (state) {
		case STR:
			if ((*p == '\\') && p[1]) {
				p++;
				PARSE_FAIL(i == (PARSE_BUFSIZE - 1));
				buf[i++] = *p++;
			} else if (isquote(*p)) {
				quote = quote ? 0 : *p;
				if (dquote) { /* keep quote */
					PARSE_FAIL(i == (PARSE_BUFSIZE - 1));
					buf[i++] = *p++;
				} else
					++p;
			} else if (isdquote(*p)) {
				dquote = dquote ? 0 : *p;
				if (quote) { /* keep dquote */
					PARSE_FAIL(i == (PARSE_BUFSIZE - 1));
					buf[i++] = *p++;
				} else
					++p;
			} else if (isspace(*p) && !quote && !dquote) {
				state = WHITE;
				if (i) {
					buf[i] = '\0';
					PARSE_FAIL(insert(&ac, buf));
					i = 0;
				}
				++p;
			} else if (*p == '$' && !quote) {
				token = isdelim(*(p + 1));
				if (token)
					p += 2;
				else
					++p;
				state = VAR;
			} else {
				PARSE_FAIL(i == (PARSE_BUFSIZE - 1));
				buf[i++] = *p++;
			}
			break;

		case WHITE:
			if (isspace(*p))
				++p;
			else
				state = STR;
			break;

		case VAR:
			if (token) {
				PARSE_FAIL((q = strchr(p, token)) == NULL);
			} else {
				q = p;
				while (*q && !isspace(*q))
					++q;
			}
			tmp = *q;
			*q = '\0';
			if ((val = variable_lookup(p)) != NULL) {
				size_t len = strlen(val);

				strncpy(buf + i, val, PARSE_BUFSIZE - (i + 1));
				i += min(len, PARSE_BUFSIZE - 1);
			}
			*q = tmp;	/* restore value */
			p = q + (token ? 1 : 0);
			state = STR;
			break;
		}
	}
	/* missing terminating ' or " */
	PARSE_FAIL(quote || dquote);
	/* If at end of token, add it */
	if (i && state == STR) {
		buf[i] = '\0';
		PARSE_FAIL(insert(&ac, buf));
	}
	args[ac] = NULL;
	*argc = ac;
	*argv = (char **)malloc((sizeof(char *) * ac + 1));
	bcopy(args, *argv, sizeof(char *) * ac + 1);
	free(buf);
	free(copy);
	return 0;
}

#define MAXARGS	20

/* Clean vector space */
static void
clean(void)
{
	int		i;

	for (i = 0; i < MAXARGS; i++) {
		if (args[i] != NULL) {
			free(args[i]);
			args[i] = NULL;
		}
	}
}

static int
insert(int *argcp, char *buf)
{

	if (*argcp >= MAXARGS)
		return 1;
	args[(*argcp)++] = strdup(buf);
	return 0;
}

static char *
variable_lookup(char *name)
{

	/* XXX search "special variable" space first? */
	return (char *)getenv(name);
}
