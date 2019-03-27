/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__SCCSID("@(#)getnetgrent.c	8.2 (Berkeley) 4/27/95");
__FBSDID("$FreeBSD$");

#include "namespace.h"

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <nsswitch.h>
#include <pthread.h>
#include <pthread_np.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nss_tls.h"

#ifdef YP
/*
 * Notes:
 * We want to be able to use NIS netgroups properly while retaining
 * the ability to use a local /etc/netgroup file. Unfortunately, you
 * can't really do both at the same time - at least, not efficiently.
 * NetBSD deals with this problem by creating a netgroup database
 * using Berkeley DB (just like the password database) that allows
 * for lookups using netgroup, netgroup.byuser or netgroup.byhost
 * searches. This is a neat idea, but I don't have time to implement
 * something like that now. (I think ultimately it would be nice
 * if we DB-fied the group and netgroup stuff all in one shot, but
 * for now I'm satisfied just to have something that works well
 * without requiring massive code changes.)
 * 
 * Therefore, to still permit the use of the local file and maintain
 * optimum NIS performance, we allow for the following conditions:
 *
 * - If /etc/netgroup does not exist and NIS is turned on, we use
 *   NIS netgroups only.
 *
 * - If /etc/netgroup exists but is empty, we use NIS netgroups
 *   only.
 *
 * - If /etc/netgroup exists and contains _only_ a '+', we use
 *   NIS netgroups only.
 *
 * - If /etc/netgroup exists, contains locally defined netgroups
 *   and a '+', we use a mixture of NIS and the local entries.
 *   This method should return the same NIS data as just using
 *   NIS alone, but it will be slower if the NIS netgroup database
 *   is large (innetgr() in particular will suffer since extra
 *   processing has to be done in order to determine memberships
 *   using just the raw netgroup data).
 *
 * - If /etc/netgroup exists and contains only locally defined
 *   netgroup entries, we use just those local entries and ignore
 *   NIS (this is the original, pre-NIS behavior).
 */

#include <rpc/rpc.h>
#include <rpcsvc/yp_prot.h>
#include <rpcsvc/ypclnt.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/errno.h>
static char *_netgr_yp_domain;
int _use_only_yp;
static int _netgr_yp_enabled;
static int _yp_innetgr;
#endif

#ifndef _PATH_NETGROUP
#define _PATH_NETGROUP "/etc/netgroup"
#endif

enum constants {
	NGRP_STORAGE_INITIAL	= 1 << 10, /* 1 KByte */
	NGRP_STORAGE_MAX	= 1 << 20, /* 1 MByte */
};

static const ns_src defaultsrc[] = {
	{ NSSRC_COMPAT, NS_SUCCESS },
	{ NULL, 0 },
};

/*
 * Static Variables and functions used by setnetgrent(), getnetgrent() and
 * endnetgrent().
 * There are two linked lists:
 * - linelist is just used by setnetgrent() to parse the net group file via.
 *   parse_netgrp()
 * - netgrp is the list of entries for the current netgroup
 */
struct linelist {
	struct linelist	*l_next;	/* Chain ptr. */
	int		l_parsed;	/* Flag for cycles */
	char		*l_groupname;	/* Name of netgroup */
	char		*l_line;	/* Netgroup entrie(s) to be parsed */
};

struct netgrp {
	struct netgrp	*ng_next;	/* Chain ptr */
	char		*ng_str[3];	/* Field pointers, see below */
};

struct netgr_state {
	FILE		*st_netf;
	struct linelist	*st_linehead;
	struct netgrp	*st_nextgrp;
	struct netgrp	*st_gr;
	char		*st_grname;
};

#define NG_HOST		0	/* Host name */
#define NG_USER		1	/* User name */
#define NG_DOM		2	/* and Domain name */

static void	netgr_endstate(void *);
NSS_TLS_HANDLING(netgr);

static int	files_endnetgrent(void *, void *, va_list);
static int	files_getnetgrent_r(void *, void *, va_list);
static int	files_setnetgrent(void *, void *, va_list);

static int	compat_endnetgrent(void *, void *, va_list);
static int	compat_innetgr(void *, void *, va_list);
static int	compat_getnetgrent_r(void *, void *, va_list);
static int	compat_setnetgrent(void *, void *, va_list);

static void	_compat_clearstate(void);
static int	_getnetgrent_r(char **, char **, char **, char *, size_t, int *,
		    struct netgr_state *);
static int	_innetgr_fallback(void *, void *, const char *, const char *,
		    const char *, const char *);
static int	innetgr_fallback(void *, void *, va_list);
static int	parse_netgrp(const char *, struct netgr_state *, int);
static struct linelist *read_for_group(const char *, struct netgr_state *, int);

#define	LINSIZ	1024	/* Length of netgroup file line */

static const ns_dtab getnetgrent_dtab[] = {
	NS_FILES_CB(files_getnetgrent_r, NULL)
	NS_COMPAT_CB(compat_getnetgrent_r, NULL)
	{ NULL, NULL, NULL },
};

static const ns_dtab setnetgrent_dtab[] = {
	NS_FILES_CB(files_setnetgrent, NULL)
	NS_COMPAT_CB(compat_setnetgrent, NULL)
	{ NULL, NULL, NULL },
};

static const ns_dtab endnetgrent_dtab[] = {
	NS_FILES_CB(files_endnetgrent, NULL)
	NS_COMPAT_CB(compat_endnetgrent, NULL)
	{ NULL, NULL, NULL },
};

static struct netgr_state compat_state;

static void
netgr_endstate(void *arg)
{
	struct linelist *lp, *olp;
	struct netgrp *gp, *ogp;
	struct netgr_state *st;

	st = (struct netgr_state *)arg;
	lp = st->st_linehead;
	while (lp != NULL) {
		olp = lp;
		lp = lp->l_next;
		free(olp->l_groupname);
		free(olp->l_line);
		free(olp);
	}
	st->st_linehead = NULL;
	if (st->st_grname != NULL) {
		free(st->st_grname);
		st->st_grname = NULL;
	}
	gp = st->st_gr;
	while (gp != NULL) {
		ogp = gp;
		gp = gp->ng_next;
		free(ogp->ng_str[NG_HOST]);
		free(ogp->ng_str[NG_USER]);
		free(ogp->ng_str[NG_DOM]);
		free(ogp);
	}
	st->st_gr = NULL;
	st->st_nextgrp = NULL;
}

static int
files_getnetgrent_r(void *retval, void *mdata, va_list ap)
{
	struct netgr_state *st;
	char **hostp, **userp, **domp, *buf;
	size_t bufsize;
	int *errnop;

	hostp = va_arg(ap, char **);
	userp = va_arg(ap, char **);
	domp = va_arg(ap, char **);
	buf = va_arg(ap, char *);
	bufsize = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);

	if (netgr_getstate(&st) != 0)
		return (NS_UNAVAIL);

	return (_getnetgrent_r(hostp, userp, domp, buf, bufsize, errnop, st));
}

static int
files_setnetgrent(void *retval, void *mdata, va_list ap)
{
	const ns_src src[] = {
		{ NSSRC_FILES, NS_SUCCESS },
		{ NULL, 0 },
	};
	struct netgr_state *st;
	const char *group;
	int rv;

	group = va_arg(ap, const char *);

	if (group == NULL || group[0] == '\0')
		return (NS_RETURN);

	rv = netgr_getstate(&st);
	if (rv != 0)
		return (NS_UNAVAIL);

	if (st->st_gr == NULL || strcmp(group, st->st_grname) != 0) {
		(void)_nsdispatch(NULL, endnetgrent_dtab, NSDB_NETGROUP,
		    "endnetgrent", src);
		if ((st->st_netf = fopen(_PATH_NETGROUP, "re")) != NULL) {
			if (parse_netgrp(group, st, 0) != 0)
				(void)_nsdispatch(NULL, endnetgrent_dtab,
				    NSDB_NETGROUP, "endnetgrent", src);
			else
				st->st_grname = strdup(group);
			(void)fclose(st->st_netf);
			st->st_netf = NULL;
		}
	}
	st->st_nextgrp = st->st_gr;
	return (st->st_grname != NULL ? NS_SUCCESS : NS_NOTFOUND);
}

static int
files_endnetgrent(void *retval, void *mdata, va_list ap)
{
	struct netgr_state *st;

	if (netgr_getstate(&st) != 0)
		return (NS_UNAVAIL);
	netgr_endstate(st);
	return (NS_SUCCESS);
}

static int
compat_getnetgrent_r(void *retval, void *mdata, va_list ap)
{
	char **hostp, **userp, **domp, *buf;
	size_t bufsize;
	int *errnop;
#ifdef YP
	_yp_innetgr = 0;
#endif

	hostp = va_arg(ap, char **);
	userp = va_arg(ap, char **);
	domp = va_arg(ap, char **);
	buf = va_arg(ap, char *);
	bufsize = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);

	return (_getnetgrent_r(hostp, userp, domp, buf, bufsize, errnop,
	    &compat_state));
}

/*
 * compat_setnetgrent()
 * Parse the netgroup file looking for the netgroup and build the list
 * of netgrp structures. Let parse_netgrp() and read_for_group() do
 * most of the work.
 */
static int
compat_setnetgrent(void *retval, void *mdata, va_list ap)
{
	FILE *netf;
	const char *group;
#ifdef YP
	struct stat _yp_statp;
	char _yp_plus;
#endif

	group = va_arg(ap, const char *);

	/* Sanity check */
	if (group == NULL || !strlen(group))
		return (NS_RETURN);

	if (compat_state.st_gr == NULL ||
	    strcmp(group, compat_state.st_grname) != 0) {
		_compat_clearstate();

#ifdef YP
		/* Presumed guilty until proven innocent. */
		_use_only_yp = 0;
		/*
		 * If /etc/netgroup doesn't exist or is empty,
		 * use NIS exclusively.
		 */
		if (((stat(_PATH_NETGROUP, &_yp_statp) < 0) &&
		    errno == ENOENT) || _yp_statp.st_size == 0)
			_use_only_yp = _netgr_yp_enabled = 1;
		if ((netf = fopen(_PATH_NETGROUP,"re")) != NULL ||_use_only_yp){
			compat_state.st_netf = netf;
		/*
		 * Icky: grab the first character of the netgroup file
		 * and turn on NIS if it's a '+'. rewind the stream
		 * afterwards so we don't goof up read_for_group() later.
		 */
			if (netf) {
				fscanf(netf, "%c", &_yp_plus);
				rewind(netf);
				if (_yp_plus == '+')
					_use_only_yp = _netgr_yp_enabled = 1;
			}
		/*
		 * If we were called specifically for an innetgr()
		 * lookup and we're in NIS-only mode, short-circuit
		 * parse_netgroup() and cut directly to the chase.
		 */
			if (_use_only_yp && _yp_innetgr) {
				/* dohw! */
				if (netf != NULL)
					fclose(netf);
				return (NS_RETURN);
			}
#else
		if ((netf = fopen(_PATH_NETGROUP, "re"))) {
			compat_state.st_netf = netf;
#endif
			if (parse_netgrp(group, &compat_state, 1)) {
				_compat_clearstate();
			} else {
				compat_state.st_grname = strdup(group);
			}
			if (netf)
				fclose(netf);
		}
	}
	compat_state.st_nextgrp = compat_state.st_gr;
	return (NS_SUCCESS);
}

static void
_compat_clearstate(void)
{

#ifdef YP
	_netgr_yp_enabled = 0;
#endif
	netgr_endstate(&compat_state);
}

/*
 * compat_endnetgrent() - cleanup
 */
static int
compat_endnetgrent(void *retval, void *mdata, va_list ap)
{

	_compat_clearstate();
	return (NS_SUCCESS);
}

int
_getnetgrent_r(char **hostp, char **userp, char **domp, char *buf,
    size_t bufsize, int *errnop, struct netgr_state *st)
{
	char *p, *src;
	size_t len;
	int rv;

#define	COPY_NG_ELEM(dstp, i) do {					\
	src = st->st_nextgrp->ng_str[(i)];				\
	if (src == NULL)						\
		src = "";						\
	len = strlcpy(p, src, bufsize);					\
	if (len >= bufsize) {						\
		*errnop = ERANGE;					\
		return (NS_RETURN);					\
	}								\
	*(dstp) = p;							\
	p += len + 1;							\
	bufsize -= len + 1;						\
} while (0)

	p = buf;
	if (st->st_nextgrp != NULL) {
		COPY_NG_ELEM(hostp, NG_HOST);
		COPY_NG_ELEM(userp, NG_USER);
		COPY_NG_ELEM(domp, NG_DOM);
		st->st_nextgrp = st->st_nextgrp->ng_next;
		rv = NS_SUCCESS;
	} else {
		rv = NS_NOTFOUND;
	}
#undef COPY_NG_ELEM

	return (rv);
}

#ifdef YP
static int
_listmatch(const char *list, const char *group, int len)
{
	const char *ptr = list;
	const char *cptr;
	int glen = strlen(group);

	/* skip possible leading whitespace */
	while (isspace((unsigned char)*ptr))
		ptr++;

	while (ptr < list + len) {
		cptr = ptr;
		while(*ptr != ','  && *ptr != '\0' && !isspace((unsigned char)*ptr))
			ptr++;
		if (strncmp(cptr, group, glen) == 0 && glen == (ptr - cptr))
			return (1);
		while (*ptr == ','  || isspace((unsigned char)*ptr))
			ptr++;
	}

	return (0);
}

static int
_revnetgr_lookup(char* lookupdom, char* map, const char* str,
		 const char* dom, const char* group)
{
	int y, rv, rot;
	char key[MAXHOSTNAMELEN];
	char *result;
	int resultlen;

	for (rot = 0; ; rot++) {
		switch (rot) {
		case 0:
			snprintf(key, MAXHOSTNAMELEN, "%s.%s", str,
			    dom ? dom : lookupdom);
			break;
		case 1:
			snprintf(key, MAXHOSTNAMELEN, "%s.*", str);
			break;
		case 2:
			snprintf(key, MAXHOSTNAMELEN, "*.%s",
			    dom ? dom : lookupdom);
			break;
		case 3:
			snprintf(key, MAXHOSTNAMELEN, "*.*");
			break;
		default:
			return (0);
		}
		y = yp_match(lookupdom, map, key, strlen(key), &result,
		    &resultlen);
		if (y == 0) {
			rv = _listmatch(result, group, resultlen);
			free(result);
			if (rv)
				return (1);
		} else if (y != YPERR_KEY) {
			/*
			 * If we get an error other than 'no
			 * such key in map' then something is
			 * wrong and we should stop the search.
			 */
			return (-1);
		}
	}
}
#endif

/*
 * Search for a match in a netgroup.
 */
static int
compat_innetgr(void *retval, void *mdata, va_list ap)
{
#ifdef YP
	const ns_src src[] = {
		{ mdata, NS_SUCCESS },
		{ NULL, 0 },
	};
#endif
	const char *group, *host, *user, *dom;

	group = va_arg(ap, const char *);
	host = va_arg(ap, const char *);
	user = va_arg(ap, const char *);
	dom = va_arg(ap, const char *);

	if (group == NULL || !strlen(group))
		return (NS_RETURN);

#ifdef YP
	_yp_innetgr = 1;
	(void)_nsdispatch(NULL, setnetgrent_dtab, NSDB_NETGROUP, "setnetgrent",
	    src, group);
	_yp_innetgr = 0;
	/*
	 * If we're in NIS-only mode, do the search using
	 * NIS 'reverse netgroup' lookups.
	 * 
	 * What happens with 'reverse netgroup' lookups:
	 * 
	 * 1) try 'reverse netgroup' lookup
	 *    1.a) if host is specified and user is null:
	 *         look in netgroup.byhost
	 *         (try host.domain, host.*, *.domain or *.*)
	 *         if found, return yes
	 *    1.b) if user is specified and host is null:
	 *         look in netgroup.byuser
	 *         (try host.domain, host.*, *.domain or *.*)
	 *         if found, return yes
	 *    1.c) if both host and user are specified,
	 *         don't do 'reverse netgroup' lookup.  It won't work.
	 *    1.d) if neither host ane user are specified (why?!?)
	 *         don't do 'reverse netgroup' lookup either.
	 * 2) if domain is specified and 'reverse lookup' is done:
	 *    'reverse lookup' was authoritative.  bye bye.
	 * 3) otherwise, too bad, try it the slow way.
	 */
	if (_use_only_yp && (host == NULL) != (user == NULL)) {
		int ret;
		if(yp_get_default_domain(&_netgr_yp_domain))
			return (NS_NOTFOUND);
		ret = _revnetgr_lookup(_netgr_yp_domain,
				      host?"netgroup.byhost":"netgroup.byuser",
				      host?host:user, dom, group);
		if (ret == 1) {
			*(int *)retval = 1;
			return (NS_SUCCESS);
		} else if (ret == 0 && dom != NULL) {
			*(int *)retval = 0;
			return (NS_SUCCESS);
		}
	}
#endif /* YP */

	return (_innetgr_fallback(retval, mdata, group, host, user, dom));
}

static int
_innetgr_fallback(void *retval, void *mdata, const char *group, const char *host,
    const char *user, const char *dom)
{
	const ns_src src[] = {
		{ mdata, NS_SUCCESS },
		{ NULL, 0 },
	};
	char *h, *u, *d;
	char *buf;
	size_t bufsize;
	int rv, ret_errno;

	if (group == NULL || group[0] == '\0')
		return (NS_RETURN);

	bufsize = NGRP_STORAGE_INITIAL;
	buf = malloc(bufsize);
	if (buf == NULL)
		return (NS_UNAVAIL);

	*(int *)retval = 0;

	(void)_nsdispatch(NULL, setnetgrent_dtab, NSDB_NETGROUP, "setnetgrent",
	    src, group);

	for (;;) {
		do {
			ret_errno = 0;
			rv = _nsdispatch(NULL, getnetgrent_dtab, NSDB_NETGROUP,
			    "getnetgrent_r", src, &h, &u, &d, buf, bufsize,
			    &ret_errno);
			if (rv != NS_SUCCESS && ret_errno == ERANGE) {
				bufsize *= 2;
				if (bufsize > NGRP_STORAGE_MAX ||
				    (buf = reallocf(buf, bufsize)) == NULL)
					goto out;
			}
		} while (rv != NS_SUCCESS && ret_errno == ERANGE);

		if (rv != NS_SUCCESS) {
			if (rv == NS_NOTFOUND && ret_errno == 0)
				rv = NS_SUCCESS;
			break;
		}

		if ((host == NULL || h == NULL || strcmp(host, h) == 0) &&
		    (user == NULL || u == NULL || strcmp(user, u) == 0) &&
		    (dom == NULL || d == NULL || strcmp(dom, d) == 0)) {
			*(int *)retval = 1;
			break;
		}
	}

out:
	free(buf);
	(void)_nsdispatch(NULL, endnetgrent_dtab, NSDB_NETGROUP, "endnetgrent",
	    src);
	return (rv);
}

static int
innetgr_fallback(void *retval, void *mdata, va_list ap)
{
	const char *group, *host, *user, *dom;

	group = va_arg(ap, const char *);
	host = va_arg(ap, const char *);
	user = va_arg(ap, const char *);
	dom = va_arg(ap, const char *);

	return (_innetgr_fallback(retval, mdata, group, host, user, dom));
}

/*
 * Parse the netgroup file setting up the linked lists.
 */
static int
parse_netgrp(const char *group, struct netgr_state *st, int niscompat)
{
	struct netgrp *grp;
	struct linelist *lp = st->st_linehead;
	char **ng;
	char *epos, *gpos, *pos, *spos;
	int freepos, len, strpos;
#ifdef DEBUG
	int fields;
#endif

	/*
	 * First, see if the line has already been read in.
	 */
	while (lp) {
		if (!strcmp(group, lp->l_groupname))
			break;
		lp = lp->l_next;
	}
	if (lp == NULL && (lp = read_for_group(group, st, niscompat)) == NULL)
		return (1);
	if (lp->l_parsed) {
#ifdef DEBUG
		/*
		 * This error message is largely superflous since the
		 * code handles the error condition sucessfully, and
		 * spewing it out from inside libc can actually hose
		 * certain programs.
		 */
		fprintf(stderr, "Cycle in netgroup %s\n", lp->l_groupname);
#endif
		return (1);
	} else
		lp->l_parsed = 1;
	pos = lp->l_line;
	/* Watch for null pointer dereferences, dammit! */
	while (pos != NULL && *pos != '\0') {
		if (*pos == '(') {
			grp = malloc(sizeof(*grp));
			if (grp == NULL)
				return (1);
			ng = grp->ng_str;
			bzero(grp, sizeof(*grp));
			pos++;
			gpos = strsep(&pos, ")");
#ifdef DEBUG
			fields = 0;
#endif
			for (strpos = 0; strpos < 3; strpos++) {
				if ((spos = strsep(&gpos, ",")) == NULL) {
					/*
					 * All other systems I've tested
					 * return NULL for empty netgroup
					 * fields. It's up to user programs
					 * to handle the NULLs appropriately.
					 */
					ng[strpos] = NULL;
					continue;
				}
#ifdef DEBUG
				fields++;
#endif
				while (*spos == ' ' || *spos == '\t')
					spos++;
				if ((epos = strpbrk(spos, " \t"))) {
					*epos = '\0';
					len = epos - spos;
				} else
					len = strlen(spos);
				if (len <= 0)
					continue;
				ng[strpos] = malloc(len + 1);
				if (ng[strpos] == NULL) {
					for (freepos = 0; freepos < strpos;
					    freepos++)
						free(ng[freepos]);
					free(grp);
					return (1);
				}
				bcopy(spos, ng[strpos], len + 1);
			}
			grp->ng_next = st->st_gr;
			st->st_gr = grp;
#ifdef DEBUG
			/*
			 * Note: on other platforms, malformed netgroup
			 * entries are not normally flagged. While we
			 * can catch bad entries and report them, we should
			 * stay silent by default for compatibility's sake.
			 */
			if (fields < 3) {
				fprintf(stderr,
				"Bad entry (%s%s%s%s%s) in netgroup \"%s\"\n",
				    ng[NG_HOST] == NULL ? "" : ng[NG_HOST],
				    ng[NG_USER] == NULL ? "" : ",",
				    ng[NG_USER] == NULL ? "" : ng[NG_USER],
				    ng[NG_DOM] == NULL ? "" : ",",
				    ng[NG_DOM] == NULL ? "" : ng[NG_DOM],
				    lp->l_groupname);
			}
#endif
		} else {
			spos = strsep(&pos, ", \t");
			if (parse_netgrp(spos, st, niscompat))
				continue;
		}
		if (pos == NULL)
			break;
		while (*pos == ' ' || *pos == ',' || *pos == '\t')
			pos++;
	}
	return (0);
}

/*
 * Read the netgroup file and save lines until the line for the netgroup
 * is found. Return 1 if eof is encountered.
 */
static struct linelist *
read_for_group(const char *group, struct netgr_state *st, int niscompat)
{
	char *linep, *olinep, *pos, *spos;
	int len, olen;
	int cont;
	struct linelist *lp;
	char line[LINSIZ + 2];
	FILE *netf;
#ifdef YP
	char *result;
	int resultlen;
	linep = NULL;

	netf = st->st_netf;
	while ((_netgr_yp_enabled && niscompat) ||
	    fgets(line, LINSIZ, netf) != NULL) {
		if (_netgr_yp_enabled) {
			if(!_netgr_yp_domain)
				if(yp_get_default_domain(&_netgr_yp_domain))
					continue;
			if (yp_match(_netgr_yp_domain, "netgroup", group,
			    strlen(group), &result, &resultlen)) {
				free(result);
				if (_use_only_yp)
					return ((struct linelist *)0);
				else {
					_netgr_yp_enabled = 0;
					continue;
				}
			}
			if (strlen(result) == 0) {
				free(result);
				return (NULL);
			}
			snprintf(line, LINSIZ, "%s %s", group, result);
			free(result);
		}
#else
	linep = NULL;
	while (fgets(line, LINSIZ, netf) != NULL) {
#endif
		pos = (char *)&line;
#ifdef YP
		if (niscompat && *pos == '+') {
			_netgr_yp_enabled = 1;
			continue;
		}
#endif
		if (*pos == '#')
			continue;
		while (*pos == ' ' || *pos == '\t')
			pos++;
		spos = pos;
		while (*pos != ' ' && *pos != '\t' && *pos != '\n' &&
			*pos != '\0')
			pos++;
		len = pos - spos;
		while (*pos == ' ' || *pos == '\t')
			pos++;
		if (*pos != '\n' && *pos != '\0') {
			lp = malloc(sizeof (*lp));
			if (lp == NULL)
				return (NULL);
			lp->l_parsed = 0;
			lp->l_groupname = malloc(len + 1);
			if (lp->l_groupname == NULL) {
				free(lp);
				return (NULL);
			}
			bcopy(spos, lp->l_groupname, len);
			*(lp->l_groupname + len) = '\0';
			len = strlen(pos);
			olen = 0;

			/*
			 * Loop around handling line continuations.
			 */
			do {
				if (*(pos + len - 1) == '\n')
					len--;
				if (*(pos + len - 1) == '\\') {
					len--;
					cont = 1;
				} else
					cont = 0;
				if (len > 0) {
					linep = malloc(olen + len + 1);
					if (linep == NULL) {
						free(lp->l_groupname);
						free(lp);
						if (olen > 0)
							free(olinep);
						return (NULL);
					}
					if (olen > 0) {
						bcopy(olinep, linep, olen);
						free(olinep);
					}
					bcopy(pos, linep + olen, len);
					olen += len;
					*(linep + olen) = '\0';
					olinep = linep;
				}
				if (cont) {
					if (fgets(line, LINSIZ, netf)) {
						pos = line;
						len = strlen(pos);
					} else
						cont = 0;
				}
			} while (cont);
			lp->l_line = linep;
			lp->l_next = st->st_linehead;
			st->st_linehead = lp;

			/*
			 * If this is the one we wanted, we are done.
			 */
			if (!strcmp(lp->l_groupname, group))
				return (lp);
		}
	}
#ifdef YP
	/*
	 * Yucky. The recursive nature of this whole mess might require
	 * us to make more than one pass through the netgroup file.
	 * This might be best left outside the #ifdef YP, but YP is
	 * defined by default anyway, so I'll leave it like this
	 * until I know better.
	 */
	rewind(netf);
#endif
	return (NULL);
}

int
getnetgrent_r(char **hostp, char **userp, char **domp, char *buf, size_t bufsize)
{
	int rv, ret_errno;

	ret_errno = 0;
	rv = _nsdispatch(NULL, getnetgrent_dtab, NSDB_NETGROUP, "getnetgrent_r",
	    defaultsrc, hostp, userp, domp, buf, bufsize, &ret_errno);
	if (rv == NS_SUCCESS) {
		return (1);
	} else {
		errno = ret_errno;
		return (0);
	}
}

int
getnetgrent(char **hostp, char **userp, char **domp)
{
	static char *ngrp_storage;
	static size_t ngrp_storage_size;
	int ret_errno, rv;

	if (ngrp_storage == NULL) {
		ngrp_storage_size = NGRP_STORAGE_INITIAL;
		ngrp_storage = malloc(ngrp_storage_size);
		if (ngrp_storage == NULL)
			return (0);
	}

	do {
		ret_errno = 0;
		rv = _nsdispatch(NULL, getnetgrent_dtab, NSDB_NETGROUP,
		    "getnetgrent_r", defaultsrc, hostp, userp, domp,
		    ngrp_storage, ngrp_storage_size, &ret_errno);
		if (rv != NS_SUCCESS && ret_errno == ERANGE) {
			ngrp_storage_size *= 2;
			if (ngrp_storage_size > NGRP_STORAGE_MAX) {
				free(ngrp_storage);
				ngrp_storage = NULL;
				errno = ERANGE;
				return (0);
			}
			ngrp_storage = reallocf(ngrp_storage,
			    ngrp_storage_size);
			if (ngrp_storage == NULL)
				return (0);
		}
	} while (rv != NS_SUCCESS && ret_errno == ERANGE);

	if (rv == NS_SUCCESS) {
		return (1);
	} else {
		errno = ret_errno;
		return (0);
	}
}

void
setnetgrent(const char *netgroup)
{

	(void)_nsdispatch(NULL, setnetgrent_dtab, NSDB_NETGROUP, "setnetgrent",
	    defaultsrc, netgroup);
}

void
endnetgrent(void)
{

	(void)_nsdispatch(NULL, endnetgrent_dtab, NSDB_NETGROUP, "endnetgrent",
	    defaultsrc);
}

int
innetgr(const char *netgroup, const char *host, const char *user,
    const char *domain)
{
	static const ns_dtab dtab[] = {
		NS_COMPAT_CB(compat_innetgr, NULL)
		NS_FALLBACK_CB(innetgr_fallback)
		{ NULL, NULL, NULL },
	};
	int result, rv;

	rv = _nsdispatch(&result, dtab, NSDB_NETGROUP, "innetgr", defaultsrc,
	    netgroup, host, user, domain);
	return (rv == NS_SUCCESS ? result : 0);
}
