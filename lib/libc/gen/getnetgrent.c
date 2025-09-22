/*	$OpenBSD: getnetgrent.c,v 1.32 2024/01/22 17:21:52 deraadt Exp $	*/

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
#include <fcntl.h>
#include <err.h>
#include <ctype.h>
#include <stdlib.h>
#include <db.h>
#ifdef YP
#include <rpcsvc/ypclnt.h>
#endif

#define _NG_STAR(s)	(((s) == NULL || *(s) == '\0') ? _ngstar : s)
#define _NG_EMPTY(s)	((s) == NULL ? "" : s)
#define _NG_ISSPACE(p)	(isspace((unsigned char) (p)) || (p) == '\n')

static const char _ngstar[] = "*";
static struct netgroup *_nghead = (struct netgroup *)NULL;
static struct netgroup *_nglist = (struct netgroup *)NULL;
static DB *_ng_db;

/*
 * Simple string list
 */
struct stringlist {
	char		**sl_str;
	size_t		  sl_max;
	size_t		  sl_cur;
};

static struct stringlist *_ng_sl_init(void);
static int	_ng_sl_add(struct stringlist *, char *);
static void	_ng_sl_free(struct stringlist *, int);
static char    *_ng_sl_find(struct stringlist *, char *);
static char    *_ng_makekey(const char *, const char *, size_t);
static int	_ng_parse(char **, char **, struct netgroup **);
static void	_ng_print(char *, size_t, const struct netgroup *);

static int		getstring(char **, int, char **);
static struct netgroup	*getnetgroup(char **);
static int		 lookup(const char *, char *, char **, int);
static void		 addgroup(char *, struct stringlist *, char *);
static int		 in_check(const char *, const char *,
			    const char *, struct netgroup *);
static int		 in_find(char *, struct stringlist *,
			    char *, const char *, const char *, const char *);
static char		*in_lookup1(const char *, const char *,
			    const char *, int);
static int		 in_lookup(const char *, const char *,
			    const char *, const char *, int);

/*
 * _ng_sl_init(): Initialize a string list
 */
static struct stringlist *
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
static int
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
static void
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
static char *
_ng_sl_find(struct stringlist *sl, char *name)
{
	size_t	i;

	for (i = 0; i < sl->sl_cur; i++)
		if (strcmp(sl->sl_str[i], name) == 0)
			return sl->sl_str[i];

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
 * lookup(): Find the given key in the database or yp, and return its value
 * in *line; returns 1 if key was found, 0 otherwise
 */
static int
lookup(const char *ypdom, char *name, char **line, int bywhat)
{
	int	ret;
#ifdef YP
	int	i;
	char	*map = NULL;
#endif

	if (_ng_db) {
		DBT	 key, data;
		size_t	 len = strlen(name) + 2;
		char	*ks = malloc(len);

		if (ks == NULL)
			return 0;
		ks[0] = bywhat;
		memcpy(&ks[1], name, len - 1);

		key.data = (u_char *) ks;
		key.size = len;

		ret = (_ng_db->get)(_ng_db, &key, &data, 0);
		free(ks);
		switch (ret) {
		case 0:
			*line = strdup(data.data);
			if (*line == NULL)
				return 0;
			return 1;

		case 1:
			break;

		case -1:
			return 0;
		}
	}
#ifdef YP
	if (ypdom) {
		switch (bywhat) {
		case _NG_KEYBYNAME:
			map = "netgroup";
			break;

		case _NG_KEYBYUSER:
			map = "netgroup.byuser";
			break;

		case _NG_KEYBYHOST:
			map = "netgroup.byhost";
			break;
		}


		if (yp_match(ypdom, map, name, strlen(name), line, &i) == 0)
			return 1;
	}
#endif

	return 0;
}


/*
 * _ng_parse(): Parse a line and return: _NG_ERROR: Syntax Error _NG_NONE:
 * line was empty or a comment _NG_GROUP: line had a netgroup definition,
 * returned in ng _NG_NAME:  line had a netgroup name, returned in name
 *
 * Public since used by netgroup_mkdb
 */
static int
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
 * addgroup(): Recursively add all the members of the netgroup to this group
 */
static void
addgroup(char *ypdom, struct stringlist *sl, char *grp)
{
	char		*line, *p;
	struct netgroup	*ng;
	char		*name;

#ifdef DEBUG_NG
	(void) fprintf(stderr, "addgroup(%s)\n", grp);
#endif
	/* check for cycles */
	if (_ng_sl_find(sl, grp) != NULL) {
		warnx("netgroup: Cycle in group `%s'", grp);
		free(grp);
		return;
	}
	if (_ng_sl_add(sl, grp) == -1) {
		free(grp);
		return;
	}

	/* Lookup this netgroup */
	if (!lookup(ypdom, grp, &line, _NG_KEYBYNAME))
		return;

	p = line;

	for (;;) {
		switch (_ng_parse(&p, &name, &ng)) {
		case _NG_NONE:
			/* Done with the line */
			free(line);
			return;

		case _NG_GROUP:
			/* new netgroup */
			/* add to the list */
			ng->ng_next = _nglist;
			_nglist = ng;
			break;

		case _NG_NAME:
			/* netgroup name */
			addgroup(ypdom, sl, name);
			break;

		case _NG_ERROR:
			return;
		}
	}
}


/*
 * in_check(): Compare the spec with the netgroup
 */
static int
in_check(const char *host, const char *user, const char *domain,
    struct netgroup *ng)
{
	if ((host != NULL) && (ng->ng_host != NULL) &&
	    strcmp(ng->ng_host, host) != 0)
		return 0;

	if ((user != NULL) && (ng->ng_user != NULL) &&
	    strcmp(ng->ng_user, user) != 0)
		return 0;

	if ((domain != NULL) && (ng->ng_domain != NULL) &&
	    strcmp(ng->ng_domain, domain) != 0)
		return 0;

	return 1;
}


/*
 * in_find(): Find a match for the host, user, domain spec
 */
static int
in_find(char *ypdom, struct stringlist *sl, char *grp, const char *host,
    const char *user, const char *domain)
{
	char		*line, *p;
	int		 i;
	struct netgroup	*ng;
	char		*name;

#ifdef DEBUG_NG
	(void) fprintf(stderr, "in_find(%s)\n", grp);
#endif
	/* check for cycles */
	if (_ng_sl_find(sl, grp) != NULL) {
		warnx("netgroup: Cycle in group `%s'", grp);
		free(grp);
		return 0;
	}
	if (_ng_sl_add(sl, grp) == -1) {
		free(grp);
		return 0;
	}

	/* Lookup this netgroup */
	if (!lookup(ypdom, grp, &line, _NG_KEYBYNAME))
		return 0;

	p = line;

	for (;;) {
		switch (_ng_parse(&p, &name, &ng)) {
		case _NG_NONE:
			/* Done with the line */
			free(line);
			return 0;

		case _NG_GROUP:
			/* new netgroup */
			i = in_check(host, user, domain, ng);
			free(ng->ng_host);
			free(ng->ng_user);
			free(ng->ng_domain);
			free(ng);
			if (i) {
				free(line);
				return 1;
			}
			break;

		case _NG_NAME:
			/* netgroup name */
			if (in_find(ypdom, sl, name, host, user, domain)) {
				free(line);
				return 1;
			}
			break;

		case _NG_ERROR:
			free(line);
			return 0;
		}
	}
}


/*
 * _ng_makekey(): Make a key from the two names given. The key is of the form
 * <name1>.<name2> Names strings are replaced with * if they are empty;
 */
static char *
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

#ifdef DEBUG_NG
static void
_ng_print(char *buf, size_t len, const struct netgroup *ng)
{
	(void) snprintf(buf, len, "(%s,%s,%s)", _NG_EMPTY(ng->ng_host),
	    _NG_EMPTY(ng->ng_user), _NG_EMPTY(ng->ng_domain));
}
#endif


/*
 * in_lookup1(): Fast lookup for a key in the appropriate map
 */
static char *
in_lookup1(const char *ypdom, const char *key, const char *domain, int map)
{
	char	*line;
	size_t	 len;
	char	*ptr;
	int	 res;

	len = (key ? strlen(key) : 1) + (domain ? strlen(domain) : 1) + 2;
	ptr = _ng_makekey(key, domain, len);
	if (ptr == NULL)
		return NULL;
	res = lookup(ypdom, ptr, &line, map);
	free(ptr);
	return res ? line : NULL;
}


/*
 * in_lookup(): Fast lookup for a key in the appropriate map
 */
static int
in_lookup(const char *ypdom, const char *group, const char *key,
    const char *domain, int map)
{
	size_t	 len;
	char	*ptr, *line;

	if (domain != NULL) {
		/* Domain specified; look in "group.domain" and "*.domain" */
		if ((line = in_lookup1(ypdom, key, domain, map)) == NULL)
			line = in_lookup1(ypdom, NULL, domain, map);
	} else
		line = NULL;

	if (line == NULL) {
		/*
		 * domain not specified or domain lookup failed; look in
		 * "group.*" and "*.*"
		 */
	    if (((line = in_lookup1(ypdom, key, NULL, map)) == NULL) &&
		((line = in_lookup1(ypdom, NULL, NULL, map)) == NULL))
		return 0;
	}

	len = strlen(group);

	for (ptr = line; (ptr = strstr(ptr, group)) != NULL;)
		/* Make sure we did not find a substring */
		if ((ptr != line && ptr[-1] != ',') ||
		    (ptr[len] != '\0' && strchr("\n\t ,", ptr[len]) == NULL))
			ptr++;
		else {
			free(line);
			return 1;
		}

	free(line);
	return 0;
}


void
endnetgrent(void)
{
	for (_nglist = _nghead; _nglist != NULL; _nglist = _nghead) {
		_nghead = _nglist->ng_next;
		free(_nglist->ng_host);
		free(_nglist->ng_user);
		free(_nglist->ng_domain);
		free(_nglist);
	}

	if (_ng_db) {
		(void) (_ng_db->close) (_ng_db);
		_ng_db = NULL;
	}
}
DEF_WEAK(endnetgrent);


void
setnetgrent(const char *ng)
{
	struct stringlist	*sl;
#ifdef YP
	static char		*__ypdomain;
	char			*line = NULL;
#endif
	char			*ng_copy, *ypdom = NULL;

	/* Cleanup any previous storage */
	if (_nghead != NULL)
		endnetgrent();

	sl = _ng_sl_init();
	if (sl == NULL)
		return;

	if (_ng_db == NULL)
		_ng_db = __hash_open(_PATH_NETGROUP_DB, O_RDONLY, 0, NULL, 0);

#ifdef YP
	/*
	 * We use yp if there is a "+" in the netgroup file, or if there is
	 * no netgroup file at all
	 */
	if (_ng_db == NULL || lookup(NULL, "+", &line, _NG_KEYBYNAME) == 0) {
		if (!__ypdomain)
			yp_get_default_domain(&__ypdomain);
		ypdom = __ypdomain;
	}
	free(line);
#endif
	ng_copy = strdup(ng);
	if (ng_copy != NULL)
		addgroup(ypdom, sl, ng_copy);
	_nghead = _nglist;
	_ng_sl_free(sl, 1);
}
DEF_WEAK(setnetgrent);


int
getnetgrent(const char **host, const char **user, const char **domain)
{
	if (_nglist == NULL)
		return 0;

	*host   = _nglist->ng_host;
	*user   = _nglist->ng_user;
	*domain = _nglist->ng_domain;

	_nglist = _nglist->ng_next;

	return 1;
}
DEF_WEAK(getnetgrent);


int
innetgr(const char *grp, const char *host, const char *user, const char *domain)
{
	char		*ypdom = NULL, *grpdup;
#ifdef YP
	static char	*__ypdomain;
	char		*line = NULL;
#endif
	int	 found;
	struct stringlist *sl;

	if (_ng_db == NULL)
		_ng_db = __hash_open(_PATH_NETGROUP_DB, O_RDONLY, 0, NULL, 0);

#ifdef YP
	/*
	 * We use yp if there is a "+" in the netgroup file, or if there is
	 * no netgroup file at all
	 */
	if (_ng_db == NULL || lookup(NULL, "+", &line, _NG_KEYBYNAME) == 0) {
		if (!__ypdomain)
			yp_get_default_domain(&__ypdomain);
		ypdom = __ypdomain;
	}

	free(line);
#endif

	/* Try the fast lookup first */
	if (host != NULL && user == NULL) {
		if (in_lookup(ypdom, grp, host, domain, _NG_KEYBYHOST))
			return 1;
	} else if (host == NULL && user != NULL) {
		if (in_lookup(ypdom, grp, user, domain, _NG_KEYBYUSER))
			return 1;
	}

	/* Too bad need the slow recursive way */
	sl = _ng_sl_init();
	if (sl == NULL)
		return 0;

	grpdup = strdup(grp);
	if (grpdup == NULL) {
		_ng_sl_free(sl, 1);
		return 0;
	}

	found = in_find(ypdom, sl, grpdup, host, user, domain);
	_ng_sl_free(sl, 1);

	return found;
}
DEF_WEAK(innetgr);
