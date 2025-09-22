/*	$OpenBSD: stringlist.c,v 1.4 2023/01/04 13:00:11 jsg Exp $	*/

/*
 * Copyright (c) 1994 Christos Zoulas
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <stdio.h>
#include <netgroup.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "stringlist.h"

static const char _ngstar[] = "*";

static int		getstring(char **, int, char **);
static struct netgroup	*getnetgroup(char **);

/*
 * _ng_sl_init(): Initialize a string list
 */
struct stringlist *
_ng_sl_init(void)
{
	struct stringlist *sl = malloc(sizeof(struct stringlist));
	if (sl == NULL)
		return NULL;

	sl->sl_cur = 0;
	sl->sl_max = 20;
	sl->sl_str = calloc(sl->sl_max, sizeof(char *));
	if (sl->sl_str == NULL) {
		free(sl);
		return NULL;
	}
	return sl;
}


/*
 * _ng_sl_add(): Add an item to the string list
 */
int
_ng_sl_add(struct stringlist *sl, char *name)
{
	if (sl->sl_cur == sl->sl_max - 1) {
		char **slstr;

		sl->sl_max += 20;
		slstr = reallocarray(sl->sl_str, sl->sl_max, sizeof(char *));
		if (slstr == NULL) {
			free(sl->sl_str);
			sl->sl_str = NULL;
			return -1;
		}
		sl->sl_str = slstr;
	}
	sl->sl_str[sl->sl_cur++] = name;
	return 0;
}


/*
 * _ng_sl_free(): Free a stringlist
 */
void
_ng_sl_free(struct stringlist *sl, int all)
{
	size_t	i;

	if (all)
		for (i = 0; i < sl->sl_cur; i++)
			free(sl->sl_str[i]);
	free(sl->sl_str);
	free(sl);
}


/*
 * sl_find(): Find a name in the string list
 */
char *
_ng_sl_find(struct stringlist *sl, char *name)
{
	size_t	i;

	for (i = 0; i < sl->sl_cur; i++)
		if (strcmp(sl->sl_str[i], name) == 0)
			return sl->sl_str[i];

	return NULL;
}

/*
 * _ng_parse(): Parse a line and return: _NG_ERROR: Syntax Error _NG_NONE:
 * line was empty or a comment _NG_GROUP: line had a netgroup definition,
 * returned in ng _NG_NAME:  line had a netgroup name, returned in name
 *
 * Public since used by netgroup_mkdb
 */
int
_ng_parse(char **p, char **name, struct netgroup **ng)
{
	while (**p) {
		if (**p == '#')
			/* comment */
			return _NG_NONE;

		while (**p && _NG_ISSPACE(**p))
			/* skipblank */
			(*p)++;

		if (**p == '(') {
			if ((*ng = getnetgroup(p)) == NULL)
				return _NG_ERROR;
			return _NG_GROUP;
		} else {
			char	*np;
			int	i;

			for (np = *p; **p && !_NG_ISSPACE(**p); (*p)++)
				continue;
			if (np != *p) {
				i = (*p - np) + 1;
				*name = malloc(i);
				if (*name == NULL)
					return _NG_ERROR;
				memcpy(*name, np, i);
				(*name)[i - 1] = '\0';
				return _NG_NAME;
			}
		}
	}
	return _NG_NONE;
}

/*
 * _ng_makekey(): Make a key from the two names given. The key is of the form
 * <name1>.<name2> Names strings are replaced with * if they are empty;
 */
char *
_ng_makekey(const char *s1, const char *s2, size_t len)
{
	char *buf = malloc(len);
	int ret;

	if (buf == NULL)
		return NULL;
	ret = snprintf(buf, len, "%s.%s", _NG_STAR(s1), _NG_STAR(s2));
	if (ret < 0 || ret >= len) {
		free(buf);
		return NULL;
	}

	return buf;
}

void
_ng_print(char *buf, size_t len, const struct netgroup *ng)
{
	(void) snprintf(buf, len, "(%s,%s,%s)", _NG_EMPTY(ng->ng_host),
	    _NG_EMPTY(ng->ng_user), _NG_EMPTY(ng->ng_domain));
}

/*
 * getnetgroup(): Parse a netgroup, and advance the pointer
 */
static struct netgroup *
getnetgroup(char **pp)
{
	struct netgroup *ng = malloc(sizeof(struct netgroup));

	if (ng == NULL)
		return NULL;

	(*pp)++;	/* skip '(' */
	if (!getstring(pp, ',', &ng->ng_host))
		goto badhost;

	if (!getstring(pp, ',', &ng->ng_user))
		goto baduser;

	if (!getstring(pp, ')', &ng->ng_domain))
		goto baddomain;

#ifdef DEBUG_NG
	{
		char buf[1024];
		_ng_print(buf, sizeof(buf), ng);
		fprintf(stderr, "netgroup %s\n", buf);
	}
#endif
	return ng;

baddomain:
	free(ng->ng_user);
baduser:
	free(ng->ng_host);
badhost:
	free(ng);
	return NULL;
}

/*
 * getstring(): Get a string delimited by the character, skipping leading and
 * trailing blanks and advancing the pointer
 */
static int
getstring(char **pp, int del, char **str)
{
	char *sp, *ep, *dp;

	/* skip leading blanks */
	for (sp = *pp; *sp && _NG_ISSPACE(*sp); sp++)
		continue;

	/* accumulate till delimiter or space */
	for (ep = sp; *ep && *ep != del && !_NG_ISSPACE(*ep); ep++)
		continue;

	/* hunt for the delimiter */
	for (dp = ep; *dp && *dp != del && _NG_ISSPACE(*dp); dp++)
		continue;

	if (*dp != del) {
		*str = NULL;
		return 0;
	}

	*pp = ++dp;

	del = (ep - sp) + 1;
	if (del > 1) {
		dp = malloc(del);
		if (dp == NULL)
			return 0;
		memcpy(dp, sp, del);
		dp[del - 1] = '\0';
	} else
		dp = NULL;

	*str = dp;
	return 1;
}
