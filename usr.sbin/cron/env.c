/*	$OpenBSD: env.c,v 1.34 2022/05/21 01:21:29 deraadt Exp $	*/

/* Copyright 1988,1990,1993,1994 by Paul Vixie
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1997,2000 by Internet Software Consortium, Inc.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <bitstring.h>		/* for structs.h */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>		/* for structs.h */

#include "macros.h"
#include "structs.h"
#include "funcs.h"
#include "globals.h"

char **
env_init(void)
{
	char **p = malloc(sizeof(char *));

	if (p != NULL)
		p[0] = NULL;
	return (p);
}

void
env_free(char **envp)
{
	char **p;

	for (p = envp; *p != NULL; p++)
		free(*p);
	free(envp);
}

char **
env_copy(char **envp)
{
	int count, i, save_errno;
	char **p;

	for (count = 0; envp[count] != NULL; count++)
		continue;
	p = reallocarray(NULL, count+1, sizeof(char *));  /* 1 for the NULL */
	if (p != NULL) {
		for (i = 0; i < count; i++)
			if ((p[i] = strdup(envp[i])) == NULL) {
				save_errno = errno;
				while (--i >= 0)
					free(p[i]);
				free(p);
				errno = save_errno;
				return (NULL);
			}
		p[count] = NULL;
	}
	return (p);
}

static char *
env_find(char *name, char **envp, size_t *count)
{
	char **ep, *p, *q;
	size_t len;

	/*
	 * Find name in envp and return its value along with the
	 * index it was found at or the length of envp if not found.
	 * We treat a '=' in name as end of string for env_set().
	 */
	for (p = name; *p && *p != '='; p++)
		continue;
	len = (size_t)(p - name);
	for (ep = envp; (p = *ep) != NULL; ep++) {
		if ((q = strchr(p, '=')) == NULL)
			continue;
		if ((size_t)(q - p) == len && strncmp(p, name, len) == 0) {
			p = q + 1;
			break;
		}
	}
	*count = (size_t)(ep - envp);
	return (p);
}

char *
env_get(char *name, char **envp)
{
	size_t count;

	return (env_find(name, envp, &count));
}

char **
env_set(char **envp, char *envstr)
{
	size_t count;
	char **p, *envcopy;

	if ((envcopy = strdup(envstr)) == NULL)
		return (NULL);

	/* Replace existing name if found. */
	if (env_find(envstr, envp, &count) != NULL) {
		free(envp[count]);
		envp[count] = envcopy;
		return (envp);
	}

	/* Realloc envp and append new variable. */
	p = reallocarray(envp, count + 2, sizeof(char **));
	if (p == NULL) {
		free(envcopy);
		return (NULL);
	}
	p[count++] = envcopy;
	p[count] = NULL;
	return (p);
}

/* The following states are used by load_env(), traversed in order: */
enum env_state {
	NAMEI,		/* First char of NAME, may be quote */
	NAME,		/* Subsequent chars of NAME */
	EQ1,		/* After end of name, looking for '=' sign */
	EQ2,		/* After '=', skipping whitespace */
	VALUEI,		/* First char of VALUE, may be quote */
	VALUE,		/* Subsequent chars of VALUE */
	FINI,		/* All done, skipping trailing whitespace */
	ERROR		/* Error */
};

/* return	-1 = end of file
 *		FALSE = not an env setting (file was repositioned)
 *		TRUE = was an env setting
 */
int
load_env(char *envstr, FILE *f)
{
	long filepos;
	int fileline;
	enum env_state state;
	char name[MAX_ENVSTR], val[MAX_ENVSTR];
	char quotechar, *c, *str;

	filepos = ftell(f);
	fileline = LineNumber;
	skip_comments(f);
	if (get_string(envstr, MAX_ENVSTR, f, "\n") == EOF)
		return (-1);

	bzero(name, sizeof name);
	bzero(val, sizeof val);
	str = name;
	state = NAMEI;
	quotechar = '\0';
	c = envstr;
	while (state != ERROR && *c) {
		switch (state) {
		case NAMEI:
		case VALUEI:
			if (*c == '\'' || *c == '"')
				quotechar = *c++;
			state++;
			/* FALLTHROUGH */
		case NAME:
		case VALUE:
			if (quotechar) {
				if (*c == quotechar) {
					state++;
					c++;
					break;
				}
				if (state == NAME && *c == '=') {
					state = ERROR;
					break;
				}
			} else {
				if (state == NAME) {
					if (isspace((unsigned char)*c)) {
						c++;
						state++;
						break;
					}
					if (*c == '=') {
						state++;
						break;
					}
				}
			}
			*str++ = *c++;
			break;
		case EQ1:
			if (*c == '=') {
				state++;
				str = val;
				quotechar = '\0';
			} else {
				if (!isspace((unsigned char)*c))
					state = ERROR;
			}
			c++;
			break;
		case EQ2:
		case FINI:
			if (isspace((unsigned char)*c))
				c++;
			else
				state++;
			break;
		case ERROR:
			/* handled below */
			break;
		}
	}
	if (state != FINI && !(state == VALUE && !quotechar))
		goto not_env;
	if (state == VALUE) {
		/* End of unquoted value: trim trailing whitespace */
		c = val + strlen(val);
		while (c > val && isspace((unsigned char)c[-1]))
			*(--c) = '\0';
	}

	/* 2 fields from parser; looks like an env setting */

	/*
	 * This can't overflow because get_string() limited the size of the
	 * name and val fields.  Still, it doesn't hurt to be careful...
	 */
	if (snprintf(envstr, MAX_ENVSTR, "%s=%s", name, val) >= MAX_ENVSTR)
		goto not_env;
	return (TRUE);
 not_env:
	fseek(f, filepos, SEEK_SET);
	Set_LineNum(fileline);
	return (FALSE);
}
