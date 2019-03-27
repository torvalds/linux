/*	$NetBSD: getrpcent.c,v 1.17 2000/01/22 22:19:17 mycroft Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2009, Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution.
 * - Neither the name of Sun Microsystems, Inc. nor the names of its 
 *   contributors may be used to endorse or promote products derived 
 *   from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char *sccsid = "@(#)getrpcent.c 1.14 91/03/11 Copyr 1984 Sun Micro";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Copyright (c) 1984 by Sun Microsystems, Inc.
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <nsswitch.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <rpc/rpc.h>
#ifdef YP
#include <rpcsvc/yp_prot.h>
#include <rpcsvc/ypclnt.h>
#endif
#include <unistd.h>
#include "namespace.h"
#include "reentrant.h"
#include "un-namespace.h"
#include "libc_private.h"
#include "nss_tls.h"
#ifdef NS_CACHING
#include "nscache.h"
#endif

#define	RPCDB	"/etc/rpc"

/* nsswitch declarations */
enum constants
{
	SETRPCENT = 1,
	ENDRPCENT = 2,
	RPCENT_STORAGE_INITIAL	= 1 << 10, /* 1 KByte */
	RPCENT_STORAGE_MAX	= 1 << 20, /* 1 MByte */
};

static const ns_src defaultsrc[] = {
	{ NSSRC_FILES, NS_SUCCESS },
#ifdef YP
	{ NSSRC_NIS, NS_SUCCESS },
#endif
	{ NULL, 0 }
};

/* files backend declarations */
struct files_state {
	FILE	*fp;
	int	stayopen;
};

static	int	files_rpcent(void *, void *, va_list);
static	int	files_setrpcent(void *, void *, va_list);

static	void	files_endstate(void *);
NSS_TLS_HANDLING(files);

/* nis backend declarations */
#ifdef YP
struct nis_state {
	char	domain[MAXHOSTNAMELEN];
	char	*current;
	int	currentlen;
	int	stepping;
	int	no_name_map;
};

static	int	nis_rpcent(void *, void *, va_list);
static	int	nis_setrpcent(void *, void *, va_list);

static	void	nis_endstate(void *);
NSS_TLS_HANDLING(nis);
#endif

/* get** wrappers for get**_r functions declarations */
struct rpcent_state {
	struct rpcent	rpc;
	char		*buffer;
	size_t	bufsize;
};
static	void	rpcent_endstate(void *);
NSS_TLS_HANDLING(rpcent);

union key {
	const char	*name;
	int		number;
};

static int wrap_getrpcbyname_r(union key, struct rpcent *, char *,
			size_t, struct rpcent **);
static int wrap_getrpcbynumber_r(union key, struct rpcent *, char *,
			size_t, struct rpcent **);
static int wrap_getrpcent_r(union key, struct rpcent *, char *,
			size_t, struct rpcent **);
static struct rpcent *getrpc(int (*fn)(union key, struct rpcent *, char *,
			size_t, struct rpcent **), union key);

#ifdef NS_CACHING
static int rpc_id_func(char *, size_t *, va_list, void *);
static int rpc_marshal_func(char *, size_t *, void *, va_list, void *);
static int rpc_unmarshal_func(char *, size_t, void *, va_list, void *);
#endif

static int
rpcent_unpack(char *p, struct rpcent *rpc, char **r_aliases,
	size_t aliases_size, int *errnop)
{
	char *cp, **q;

	assert(p != NULL);

	if (*p == '#')
		return (-1);
	cp = strpbrk(p, "#\n");
	if (cp == NULL)
		return (-1);
	*cp = '\0';
	cp = strpbrk(p, " \t");
	if (cp == NULL)
		return (-1);
	*cp++ = '\0';
	/* THIS STUFF IS INTERNET SPECIFIC */
	rpc->r_name = p;
	while (*cp == ' ' || *cp == '\t')
		cp++;
	rpc->r_number = atoi(cp);
	q = rpc->r_aliases = r_aliases;
	cp = strpbrk(cp, " \t");
	if (cp != NULL)
		*cp++ = '\0';
	while (cp && *cp) {
		if (*cp == ' ' || *cp == '\t') {
			cp++;
			continue;
		}
		if (q < &(r_aliases[aliases_size - 1]))
			*q++ = cp;
		else {
			*errnop = ERANGE;
			return -1;
		}

		cp = strpbrk(cp, " \t");
		if (cp != NULL)
			*cp++ = '\0';
	}
	*q = NULL;
	return 0;
}

/* files backend implementation */
static	void
files_endstate(void *p)
{
	FILE * f;

	if (p == NULL)
		return;

	f = ((struct files_state *)p)->fp;
	if (f != NULL)
		fclose(f);

	free(p);
}

static int
files_rpcent(void *retval, void *mdata, va_list ap)
{
	char *name;
	int number;
	struct rpcent *rpc;
	char *buffer;
	size_t bufsize;
	int *errnop;

	char *line;
	size_t linesize;
	char **aliases;
	int aliases_size;
	char **rp;

	struct files_state	*st;
	int rv;
	int stayopen;
	enum nss_lookup_type how;

	how = (enum nss_lookup_type)mdata;
	switch (how)
	{
	case nss_lt_name:
		name = va_arg(ap, char *);
		break;
	case nss_lt_id:
		number = va_arg(ap, int);
		break;
	case nss_lt_all:
		break;
	default:
		return (NS_NOTFOUND);
	}

	rpc = va_arg(ap, struct rpcent *);
	buffer = va_arg(ap, char *);
	bufsize = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);

	*errnop = files_getstate(&st);
	if (*errnop != 0)
		return (NS_UNAVAIL);

	if (st->fp == NULL && (st->fp = fopen(RPCDB, "r")) == NULL) {
		*errnop = errno;
		return (NS_UNAVAIL);
	}

	if (how == nss_lt_all)
		stayopen = 1;
	else {
		rewind(st->fp);
		stayopen = st->stayopen;
	}

	do {
		if ((line = fgetln(st->fp, &linesize)) == NULL) {
			*errnop = errno;
			rv = NS_RETURN;
			break;
		}

		if (bufsize <= linesize + _ALIGNBYTES + sizeof(char *)) {
			*errnop = ERANGE;
			rv = NS_RETURN;
			break;
		}

		aliases = (char **)_ALIGN(&buffer[linesize+1]);
		aliases_size = (buffer + bufsize -
			(char *)aliases)/sizeof(char *);
		if (aliases_size < 1) {
			*errnop = ERANGE;
			rv = NS_RETURN;
			break;
		}

		memcpy(buffer, line, linesize);
		buffer[linesize] = '\0';

		rv = rpcent_unpack(buffer, rpc, aliases, aliases_size, errnop);
		if (rv != 0) {
			if (*errnop == 0) {
				rv = NS_NOTFOUND;
				continue;
			}
			else {
				rv = NS_RETURN;
				break;
			}
		}

		switch (how)
		{
		case nss_lt_name:
			if (strcmp(rpc->r_name, name) == 0)
				goto done;
			for (rp = rpc->r_aliases; *rp != NULL; rp++) {
				if (strcmp(*rp, name) == 0)
					goto done;
			}
			rv = NS_NOTFOUND;
			continue;
done:
			rv = NS_SUCCESS;
			break;
		case nss_lt_id:
			rv = (rpc->r_number == number) ? NS_SUCCESS :
				NS_NOTFOUND;
			break;
		case nss_lt_all:
			rv = NS_SUCCESS;
			break;
		}

	} while (!(rv & NS_TERMINATE));

	if (!stayopen && st->fp!=NULL) {
		fclose(st->fp);
		st->fp = NULL;
	}

	if ((rv == NS_SUCCESS) && (retval != NULL))
		*((struct rpcent **)retval) = rpc;

	return (rv);
}

static int
files_setrpcent(void *retval, void *mdata, va_list ap)
{
	struct files_state	*st;
	int	rv;
	int	f;

	rv = files_getstate(&st);
	if (rv != 0)
		return (NS_UNAVAIL);

	switch ((enum constants)mdata)
	{
	case SETRPCENT:
		f = va_arg(ap,int);
		if (st->fp == NULL)
			st->fp = fopen(RPCDB, "r");
		else
			rewind(st->fp);
		st->stayopen |= f;
		break;
	case ENDRPCENT:
		if (st->fp != NULL) {
			fclose(st->fp);
			st->fp = NULL;
		}
		st->stayopen = 0;
		break;
	default:
		break;
	}

	return (NS_UNAVAIL);
}

/* nis backend implementation */
#ifdef YP
static 	void
nis_endstate(void *p)
{
	if (p == NULL)
		return;

	free(((struct nis_state *)p)->current);
	free(p);
}

static int
nis_rpcent(void *retval, void *mdata, va_list ap)
{
	char		*name;
	int		number;
	struct rpcent	*rpc;
	char		*buffer;
	size_t	bufsize;
	int		*errnop;

	char		**rp;
	char		**aliases;
	int		aliases_size;

	char	*lastkey;
	char	*resultbuf;
	int	resultbuflen;
	char	buf[YPMAXRECORD + 2];

	struct nis_state	*st;
	int		rv;
	enum nss_lookup_type	how;
	int	no_name_active;

	how = (enum nss_lookup_type)mdata;
	switch (how)
	{
	case nss_lt_name:
		name = va_arg(ap, char *);
		break;
	case nss_lt_id:
		number = va_arg(ap, int);
		break;
	case nss_lt_all:
		break;
	default:
		return (NS_NOTFOUND);
	}

	rpc = va_arg(ap, struct rpcent *);
	buffer = va_arg(ap, char *);
	bufsize = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);

	*errnop = nis_getstate(&st);
	if (*errnop != 0)
		return (NS_UNAVAIL);

	if (st->domain[0] == '\0') {
		if (getdomainname(st->domain, sizeof(st->domain)) != 0) {
			*errnop = errno;
			return (NS_UNAVAIL);
		}
	}

	no_name_active = 0;
	do {
		switch (how)
		{
		case nss_lt_name:
			if (!st->no_name_map)
			{
				snprintf(buf, sizeof buf, "%s", name);
				rv = yp_match(st->domain, "rpc.byname", buf,
			    		strlen(buf), &resultbuf, &resultbuflen);

				switch (rv) {
				case 0:
					break;
				case YPERR_MAP:
					st->stepping = 0;
					no_name_active = 1;
					how = nss_lt_all;

					rv = NS_NOTFOUND;
					continue;
				default:
					rv = NS_NOTFOUND;
					goto fin;
				}
			} else {
				st->stepping = 0;
				no_name_active = 1;
				how = nss_lt_all;

				rv = NS_NOTFOUND;
				continue;
			}
		break;
		case nss_lt_id:
			snprintf(buf, sizeof buf, "%d", number);
			if (yp_match(st->domain, "rpc.bynumber", buf,
			    	strlen(buf), &resultbuf, &resultbuflen)) {
				rv = NS_NOTFOUND;
				goto fin;
			}
			break;
		case nss_lt_all:
				if (!st->stepping) {
					rv = yp_first(st->domain, "rpc.bynumber",
				    		&st->current,
						&st->currentlen, &resultbuf,
				    		&resultbuflen);
					if (rv) {
						rv = NS_NOTFOUND;
						goto fin;
					}
					st->stepping = 1;
				} else {
					lastkey = st->current;
					rv = yp_next(st->domain, "rpc.bynumber",
				    		st->current,
						st->currentlen, &st->current,
				    		&st->currentlen,
						&resultbuf,	&resultbuflen);
					free(lastkey);
					if (rv) {
						st->stepping = 0;
						rv = NS_NOTFOUND;
						goto fin;
					}
				}
			break;
		}

		/* we need a room for additional \n symbol */
		if (bufsize <= resultbuflen + 1 + _ALIGNBYTES +
		    sizeof(char *)) {
			*errnop = ERANGE;
			rv = NS_RETURN;
			free(resultbuf);
			break;
		}

		aliases=(char **)_ALIGN(&buffer[resultbuflen+2]);
		aliases_size = (buffer + bufsize - (char *)aliases) /
			sizeof(char *);
		if (aliases_size < 1) {
			*errnop = ERANGE;
			rv = NS_RETURN;
			free(resultbuf);
			break;
		}

		/*
		 * rpcent_unpack expects lines terminated with \n -- make it happy
		 */
		memcpy(buffer, resultbuf, resultbuflen);
		buffer[resultbuflen] = '\n';
		buffer[resultbuflen+1] = '\0';
		free(resultbuf);

		if (rpcent_unpack(buffer, rpc, aliases, aliases_size,
		    errnop) != 0) {
			if (*errnop == 0)
				rv = NS_NOTFOUND;
			else
				rv = NS_RETURN;
		} else {
			if ((how == nss_lt_all) && (no_name_active != 0)) {
				if (strcmp(rpc->r_name, name) == 0)
					goto done;
				for (rp = rpc->r_aliases; *rp != NULL; rp++) {
					if (strcmp(*rp, name) == 0)
						goto done;
				}
				rv = NS_NOTFOUND;
				continue;
done:
				rv = NS_SUCCESS;
			} else
				rv = NS_SUCCESS;
		}

	} while (!(rv & NS_TERMINATE) && (how == nss_lt_all));

fin:
	if ((rv == NS_SUCCESS) && (retval != NULL))
		*((struct rpcent **)retval) = rpc;

	return (rv);
}

static int
nis_setrpcent(void *retval, void *mdata, va_list ap)
{
	struct nis_state	*st;
	int	rv;

	rv = nis_getstate(&st);
	if (rv != 0)
		return (NS_UNAVAIL);

	switch ((enum constants)mdata)
	{
	case SETRPCENT:
	case ENDRPCENT:
		free(st->current);
		st->current = NULL;
		st->stepping = 0;
		break;
	default:
		break;
	}

	return (NS_UNAVAIL);
}
#endif

#ifdef NS_CACHING
static int
rpc_id_func(char *buffer, size_t *buffer_size, va_list ap, void *cache_mdata)
{
	char *name;
	int rpc;

	size_t desired_size, size;
	enum nss_lookup_type lookup_type;
	int res = NS_UNAVAIL;

	lookup_type = (enum nss_lookup_type)cache_mdata;
	switch (lookup_type) {
	case nss_lt_name:
		name = va_arg(ap, char *);

		size = strlen(name);
		desired_size = sizeof(enum nss_lookup_type) + size + 1;
		if (desired_size > *buffer_size) {
			res = NS_RETURN;
			goto fin;
		}

		memcpy(buffer, &lookup_type, sizeof(enum nss_lookup_type));
		memcpy(buffer + sizeof(enum nss_lookup_type), name, size + 1);

		res = NS_SUCCESS;
		break;
	case nss_lt_id:
		rpc = va_arg(ap, int);

		desired_size = sizeof(enum nss_lookup_type) + sizeof(int);
		if (desired_size > *buffer_size) {
			res = NS_RETURN;
			goto fin;
		}

		memcpy(buffer, &lookup_type, sizeof(enum nss_lookup_type));
		memcpy(buffer + sizeof(enum nss_lookup_type), &rpc,
		    sizeof(int));

		res = NS_SUCCESS;
		break;
	default:
		/* should be unreachable */
		return (NS_UNAVAIL);
	}

fin:
	*buffer_size = desired_size;
	return (res);
}

static int
rpc_marshal_func(char *buffer, size_t *buffer_size, void *retval, va_list ap,
    void *cache_mdata)
{
	char *name;
	int num;
	struct rpcent *rpc;
	char *orig_buf;
	size_t orig_buf_size;

	struct rpcent new_rpc;
	size_t desired_size, size, aliases_size;
	char *p;
	char **alias;

	switch ((enum nss_lookup_type)cache_mdata) {
	case nss_lt_name:
		name = va_arg(ap, char *);
		break;
	case nss_lt_id:
		num = va_arg(ap, int);
		break;
	case nss_lt_all:
		break;
	default:
		/* should be unreachable */
		return (NS_UNAVAIL);
	}

	rpc = va_arg(ap, struct rpcent *);
	orig_buf = va_arg(ap, char *);
	orig_buf_size = va_arg(ap, size_t);

	desired_size = _ALIGNBYTES + sizeof(struct rpcent) + sizeof(char *);
	if (rpc->r_name != NULL)
		desired_size += strlen(rpc->r_name) + 1;

	if (rpc->r_aliases != NULL) {
		aliases_size = 0;
		for (alias = rpc->r_aliases; *alias; ++alias) {
			desired_size += strlen(*alias) + 1;
			++aliases_size;
		}

		desired_size += _ALIGNBYTES + (aliases_size + 1) *
		    sizeof(char *);
	}

	if (*buffer_size < desired_size) {
		/* this assignment is here for future use */
		*buffer_size = desired_size;
		return (NS_RETURN);
	}

	new_rpc = *rpc;

	*buffer_size = desired_size;
	memset(buffer, 0, desired_size);
	p = buffer + sizeof(struct rpcent) + sizeof(char *);
	memcpy(buffer + sizeof(struct rpcent), &p, sizeof(char *));
	p = (char *)_ALIGN(p);

	if (new_rpc.r_name != NULL) {
		size = strlen(new_rpc.r_name);
		memcpy(p, new_rpc.r_name, size);
		new_rpc.r_name = p;
		p += size + 1;
	}

	if (new_rpc.r_aliases != NULL) {
		p = (char *)_ALIGN(p);
		memcpy(p, new_rpc.r_aliases, sizeof(char *) * aliases_size);
		new_rpc.r_aliases = (char **)p;
		p += sizeof(char *) * (aliases_size + 1);

		for (alias = new_rpc.r_aliases; *alias; ++alias) {
			size = strlen(*alias);
			memcpy(p, *alias, size);
			*alias = p;
			p += size + 1;
		}
	}

	memcpy(buffer, &new_rpc, sizeof(struct rpcent));
	return (NS_SUCCESS);
}

static int
rpc_unmarshal_func(char *buffer, size_t buffer_size, void *retval, va_list ap,
    void *cache_mdata)
{
	char *name;
	int num;
	struct rpcent *rpc;
	char *orig_buf;
	size_t orig_buf_size;
	int *ret_errno;

	char *p;
	char **alias;

	switch ((enum nss_lookup_type)cache_mdata) {
	case nss_lt_name:
		name = va_arg(ap, char *);
		break;
	case nss_lt_id:
		num = va_arg(ap, int);
		break;
	case nss_lt_all:
		break;
	default:
		/* should be unreachable */
		return (NS_UNAVAIL);
	}

	rpc = va_arg(ap, struct rpcent *);
	orig_buf = va_arg(ap, char *);
	orig_buf_size = va_arg(ap, size_t);
	ret_errno = va_arg(ap, int *);

	if (orig_buf_size <
	    buffer_size - sizeof(struct rpcent) - sizeof(char *)) {
		*ret_errno = ERANGE;
		return (NS_RETURN);
	}

	memcpy(rpc, buffer, sizeof(struct rpcent));
	memcpy(&p, buffer + sizeof(struct rpcent), sizeof(char *));

	orig_buf = (char *)_ALIGN(orig_buf);
	memcpy(orig_buf, buffer + sizeof(struct rpcent) + sizeof(char *) +
	    _ALIGN(p) - (size_t)p,
	    buffer_size - sizeof(struct rpcent) - sizeof(char *) -
	    _ALIGN(p) + (size_t)p);
	p = (char *)_ALIGN(p);

	NS_APPLY_OFFSET(rpc->r_name, orig_buf, p, char *);
	if (rpc->r_aliases != NULL) {
		NS_APPLY_OFFSET(rpc->r_aliases, orig_buf, p, char **);

		for (alias = rpc->r_aliases	; *alias; ++alias)
			NS_APPLY_OFFSET(*alias, orig_buf, p, char *);
	}

	if (retval != NULL)
		*((struct rpcent **)retval) = rpc;

	return (NS_SUCCESS);
}

NSS_MP_CACHE_HANDLING(rpc);
#endif /* NS_CACHING */


/* get**_r functions implementation */
static int
getrpcbyname_r(const char *name, struct rpcent *rpc, char *buffer,
	size_t bufsize, struct rpcent **result)
{
#ifdef NS_CACHING
	static const nss_cache_info cache_info =
    		NS_COMMON_CACHE_INFO_INITIALIZER(
		rpc, (void *)nss_lt_name,
		rpc_id_func, rpc_marshal_func, rpc_unmarshal_func);
#endif
	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_rpcent, (void *)nss_lt_name },
#ifdef YP
		{ NSSRC_NIS, nis_rpcent, (void *)nss_lt_name },
#endif
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};
	int rv, ret_errno;

	ret_errno = 0;
	*result = NULL;
	rv = nsdispatch(result, dtab, NSDB_RPC, "getrpcbyname_r", defaultsrc,
	    name, rpc, buffer, bufsize, &ret_errno);

	if (rv == NS_SUCCESS)
		return (0);
	else
		return (ret_errno);
}

static int
getrpcbynumber_r(int number, struct rpcent *rpc, char *buffer,
	size_t bufsize, struct rpcent **result)
{
#ifdef NS_CACHING
	static const nss_cache_info cache_info =
    		NS_COMMON_CACHE_INFO_INITIALIZER(
		rpc, (void *)nss_lt_id,
		rpc_id_func, rpc_marshal_func, rpc_unmarshal_func);
#endif
	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_rpcent, (void *)nss_lt_id },
#ifdef YP
		{ NSSRC_NIS, nis_rpcent, (void *)nss_lt_id },
#endif
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};
	int rv, ret_errno;

	ret_errno = 0;
	*result = NULL;
	rv = nsdispatch(result, dtab, NSDB_RPC, "getrpcbynumber_r", defaultsrc,
	    number, rpc, buffer, bufsize, &ret_errno);

	if (rv == NS_SUCCESS)
		return (0);
	else
		return (ret_errno);
}

static int
getrpcent_r(struct rpcent *rpc, char *buffer, size_t bufsize,
	struct rpcent **result)
{
#ifdef NS_CACHING
	static const nss_cache_info cache_info = NS_MP_CACHE_INFO_INITIALIZER(
		rpc, (void *)nss_lt_all,
		rpc_marshal_func, rpc_unmarshal_func);
#endif
	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_rpcent, (void *)nss_lt_all },
#ifdef YP
		{ NSSRC_NIS, nis_rpcent, (void *)nss_lt_all },
#endif
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};
	int rv, ret_errno;

	ret_errno = 0;
	*result = NULL;
	rv = nsdispatch(result, dtab, NSDB_RPC, "getrpcent_r", defaultsrc,
	    rpc, buffer, bufsize, &ret_errno);

	if (rv == NS_SUCCESS)
		return (0);
	else
		return (ret_errno);
}

/* get** wrappers for get**_r functions implementation */
static 	void
rpcent_endstate(void *p)
{
	if (p == NULL)
		return;

	free(((struct rpcent_state *)p)->buffer);
	free(p);
}

static	int
wrap_getrpcbyname_r(union key key, struct rpcent *rpc, char *buffer,
    size_t bufsize, struct rpcent **res)
{
	return (getrpcbyname_r(key.name, rpc, buffer, bufsize, res));
}

static	int
wrap_getrpcbynumber_r(union key key, struct rpcent *rpc, char *buffer,
    size_t bufsize, struct rpcent **res)
{
	return (getrpcbynumber_r(key.number, rpc, buffer, bufsize, res));
}

static	int
wrap_getrpcent_r(union key key __unused, struct rpcent *rpc, char *buffer,
    size_t bufsize, struct rpcent **res)
{
	return (getrpcent_r(rpc, buffer, bufsize, res));
}

static struct rpcent *
getrpc(int (*fn)(union key, struct rpcent *, char *, size_t, struct rpcent **),
    union key key)
{
	int		 rv;
	struct rpcent	*res;
	struct rpcent_state * st;

	rv=rpcent_getstate(&st);
	if (rv != 0) {
		errno = rv;
		return NULL;
	}

	if (st->buffer == NULL) {
		st->buffer = malloc(RPCENT_STORAGE_INITIAL);
		if (st->buffer == NULL)
			return (NULL);
		st->bufsize = RPCENT_STORAGE_INITIAL;
	}
	do {
		rv = fn(key, &st->rpc, st->buffer, st->bufsize, &res);
		if (res == NULL && rv == ERANGE) {
			free(st->buffer);
			if ((st->bufsize << 1) > RPCENT_STORAGE_MAX) {
				st->buffer = NULL;
				errno = ERANGE;
				return (NULL);
			}
			st->bufsize <<= 1;
			st->buffer = malloc(st->bufsize);
			if (st->buffer == NULL)
				return (NULL);
		}
	} while (res == NULL && rv == ERANGE);
	if (rv != 0)
		errno = rv;

	return (res);
}

struct rpcent *
getrpcbyname(const char *name)
{
	union key key;

	key.name = name;

	return (getrpc(wrap_getrpcbyname_r, key));
}

struct rpcent *
getrpcbynumber(int number)
{
	union key key;

	key.number = number;

	return (getrpc(wrap_getrpcbynumber_r, key));
}

struct rpcent *
getrpcent(void)
{
	union key key;

	key.number = 0;	/* not used */

	return (getrpc(wrap_getrpcent_r, key));
}

void
setrpcent(int stayopen)
{
#ifdef NS_CACHING
	static const nss_cache_info cache_info = NS_MP_CACHE_INFO_INITIALIZER(
		rpc, (void *)nss_lt_all,
		NULL, NULL);
#endif

	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_setrpcent, (void *)SETRPCENT },
#ifdef YP
		{ NSSRC_NIS, nis_setrpcent, (void *)SETRPCENT },
#endif
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};

	(void)nsdispatch(NULL, dtab, NSDB_RPC, "setrpcent", defaultsrc,
		stayopen);
}

void
endrpcent(void)
{
#ifdef NS_CACHING
	static const nss_cache_info cache_info = NS_MP_CACHE_INFO_INITIALIZER(
		rpc, (void *)nss_lt_all,
		NULL, NULL);
#endif

	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_setrpcent, (void *)ENDRPCENT },
#ifdef YP
		{ NSSRC_NIS, nis_setrpcent, (void *)ENDRPCENT },
#endif
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};

	(void)nsdispatch(NULL, dtab, NSDB_RPC, "endrpcent", defaultsrc);
}
