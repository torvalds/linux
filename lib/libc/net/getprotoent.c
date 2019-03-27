/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
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

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)getprotoent.c	8.1 (Berkeley) 6/4/93";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <nsswitch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "namespace.h"
#include "reentrant.h"
#include "un-namespace.h"
#include "netdb_private.h"
#ifdef NS_CACHING
#include "nscache.h"
#endif
#include "nss_tls.h"

static const ns_src defaultsrc[] = {
	{ NSSRC_FILES, NS_SUCCESS },
	{ NULL, 0 }
};

NETDB_THREAD_ALLOC(protoent_data)
NETDB_THREAD_ALLOC(protodata)

static void
protoent_data_clear(struct protoent_data *ped)
{
	if (ped->fp) {
		fclose(ped->fp);
		ped->fp = NULL;
	}
}

static void
protoent_data_free(void *ptr)
{
	struct protoent_data *ped = ptr;

	protoent_data_clear(ped);
	free(ped);
}

static void
protodata_free(void *ptr)
{
	free(ptr);
}

#ifdef NS_CACHING
int
__proto_id_func(char *buffer, size_t *buffer_size, va_list ap,
    void *cache_mdata)
{
	char *name;
	int proto;

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
		proto = va_arg(ap, int);

		desired_size = sizeof(enum nss_lookup_type) + sizeof(int);
		if (desired_size > *buffer_size) {
			res = NS_RETURN;
			goto fin;
		}

		memcpy(buffer, &lookup_type, sizeof(enum nss_lookup_type));
		memcpy(buffer + sizeof(enum nss_lookup_type), &proto,
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


int
__proto_marshal_func(char *buffer, size_t *buffer_size, void *retval,
    va_list ap, void *cache_mdata)
{
	char *name;
	int num;
	struct protoent *proto;
	char *orig_buf;
	size_t orig_buf_size;

	struct protoent new_proto;
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

	proto = va_arg(ap, struct protoent *);
	orig_buf = va_arg(ap, char *);
	orig_buf_size = va_arg(ap, size_t);

	desired_size = _ALIGNBYTES + sizeof(struct protoent) + sizeof(char *);
	if (proto->p_name != NULL)
		desired_size += strlen(proto->p_name) + 1;

	if (proto->p_aliases != NULL) {
		aliases_size = 0;
		for (alias = proto->p_aliases; *alias; ++alias) {
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

	memcpy(&new_proto, proto, sizeof(struct protoent));

	*buffer_size = desired_size;
	memset(buffer, 0, desired_size);
	p = buffer + sizeof(struct protoent) + sizeof(char *);
	memcpy(buffer + sizeof(struct protoent), &p, sizeof(char *));
	p = (char *)_ALIGN(p);

	if (new_proto.p_name != NULL) {
		size = strlen(new_proto.p_name);
		memcpy(p, new_proto.p_name, size);
		new_proto.p_name = p;
		p += size + 1;
	}

	if (new_proto.p_aliases != NULL) {
		p = (char *)_ALIGN(p);
		memcpy(p, new_proto.p_aliases, sizeof(char *) * aliases_size);
		new_proto.p_aliases = (char **)p;
		p += sizeof(char *) * (aliases_size + 1);

		for (alias = new_proto.p_aliases; *alias; ++alias) {
			size = strlen(*alias);
			memcpy(p, *alias, size);
			*alias = p;
			p += size + 1;
		}
	}

	memcpy(buffer, &new_proto, sizeof(struct protoent));
	return (NS_SUCCESS);
}

int
__proto_unmarshal_func(char *buffer, size_t buffer_size, void *retval,
    va_list ap, void *cache_mdata)
{
	char *name;
	int num;
	struct protoent *proto;
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

	proto = va_arg(ap, struct protoent *);
	orig_buf = va_arg(ap, char *);
	orig_buf_size = va_arg(ap, size_t);
	ret_errno = va_arg(ap, int *);

	if (orig_buf_size <
	    buffer_size - sizeof(struct protoent) - sizeof(char *)) {
		*ret_errno = ERANGE;
		return (NS_RETURN);
	}

	memcpy(proto, buffer, sizeof(struct protoent));
	memcpy(&p, buffer + sizeof(struct protoent), sizeof(char *));

	orig_buf = (char *)_ALIGN(orig_buf);
	memcpy(orig_buf, buffer + sizeof(struct protoent) + sizeof(char *) +
	    _ALIGN(p) - (size_t)p,
	    buffer_size - sizeof(struct protoent) - sizeof(char *) -
	    _ALIGN(p) + (size_t)p);
	p = (char *)_ALIGN(p);

	NS_APPLY_OFFSET(proto->p_name, orig_buf, p, char *);
	if (proto->p_aliases != NULL) {
		NS_APPLY_OFFSET(proto->p_aliases, orig_buf, p, char **);

		for (alias = proto->p_aliases; *alias; ++alias)
			NS_APPLY_OFFSET(*alias, orig_buf, p, char *);
	}

	if (retval != NULL)
		*((struct protoent **)retval) = proto;

	return (NS_SUCCESS);
}

NSS_MP_CACHE_HANDLING(protocols);
#endif /* NS_CACHING */

int
__copy_protoent(struct protoent *pe, struct protoent *pptr, char *buf,
    size_t buflen)
{
	char *cp;
	int i, n;
	int numptr, len;

	/* Find out the amount of space required to store the answer. */
	numptr = 1; /* NULL ptr */
	len = (char *)ALIGN(buf) - buf;
	for (i = 0; pe->p_aliases[i]; i++, numptr++) {
		len += strlen(pe->p_aliases[i]) + 1;
	}
	len += strlen(pe->p_name) + 1;
	len += numptr * sizeof(char*);

	if (len > (int)buflen) {
		errno = ERANGE;
		return (-1);
	}

	/* copy protocol value*/
	pptr->p_proto = pe->p_proto;

	cp = (char *)ALIGN(buf) + numptr * sizeof(char *);

	/* copy official name */
	n = strlen(pe->p_name) + 1;
	strcpy(cp, pe->p_name);
	pptr->p_name = cp;
	cp += n;

	/* copy aliases */
	pptr->p_aliases = (char **)ALIGN(buf);
	for (i = 0 ; pe->p_aliases[i]; i++) {
		n = strlen(pe->p_aliases[i]) + 1;
		strcpy(cp, pe->p_aliases[i]);
		pptr->p_aliases[i] = cp;
		cp += n;
	}
	pptr->p_aliases[i] = NULL;

	return (0);
}

void
__setprotoent_p(int f, struct protoent_data *ped)
{
	if (ped->fp == NULL)
		ped->fp = fopen(_PATH_PROTOCOLS, "re");
	else
		rewind(ped->fp);
	ped->stayopen |= f;
}

void
__endprotoent_p(struct protoent_data *ped)
{
	if (ped->fp) {
		fclose(ped->fp);
		ped->fp = NULL;
	}
	ped->stayopen = 0;
}

int
__getprotoent_p(struct protoent *pe, struct protoent_data *ped)
{
	char *p;
	char *cp, **q, *endp;
	long l;

	if (ped->fp == NULL && (ped->fp = fopen(_PATH_PROTOCOLS, "re")) == NULL)
		return (-1);
again:
	if ((p = fgets(ped->line, sizeof ped->line, ped->fp)) == NULL)
		return (-1);
	if (*p == '#')
		goto again;
	cp = strpbrk(p, "#\n");
	if (cp != NULL)
		*cp = '\0';
	pe->p_name = p;
	cp = strpbrk(p, " \t");
	if (cp == NULL)
		goto again;
	*cp++ = '\0';
	while (*cp == ' ' || *cp == '\t')
		cp++;
	p = strpbrk(cp, " \t");
	if (p != NULL)
		*p++ = '\0';
	l = strtol(cp, &endp, 10);
	if (endp == cp || *endp != '\0' || l < 0 || l > USHRT_MAX)
		goto again;
	pe->p_proto = l;
	q = pe->p_aliases = ped->aliases;
	if (p != NULL) {
		cp = p;
		while (cp && *cp) {
			if (*cp == ' ' || *cp == '\t') {
				cp++;
				continue;
			}
			if (q < &ped->aliases[_MAXALIASES - 1])
				*q++ = cp;
			cp = strpbrk(cp, " \t");
			if (cp != NULL)
				*cp++ = '\0';
		}
	}
	*q = NULL;
	return (0);
}

static int
files_getprotoent_r(void *retval, void *mdata, va_list ap)
{
	struct protoent pe;
	struct protoent_data *ped;

	struct protoent	*pptr;
	char *buffer;
	size_t buflen;
	int *errnop;

	pptr = va_arg(ap, struct protoent *);
	buffer = va_arg(ap, char *);
	buflen = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);

	if ((ped = __protoent_data_init()) == NULL) {
		*errnop = errno;
		return (NS_NOTFOUND);
	}

	if (__getprotoent_p(&pe, ped) != 0) {
		*errnop = errno;
		return (NS_NOTFOUND);
	}

	if (__copy_protoent(&pe, pptr, buffer, buflen) != 0) {
		*errnop = errno;
		return (NS_RETURN);
	}

	*((struct protoent **)retval) = pptr;
	return (NS_SUCCESS);
}

static int
files_setprotoent(void *retval, void *mdata, va_list ap)
{
	struct protoent_data *ped;
	int f;

	f = va_arg(ap, int);
	if ((ped = __protoent_data_init()) == NULL)
		return (NS_UNAVAIL);

	__setprotoent_p(f, ped);
	return (NS_UNAVAIL);
}

static int
files_endprotoent(void *retval, void *mdata, va_list ap)
{
	struct protoent_data *ped;

	if ((ped = __protoent_data_init()) == NULL)
		return (NS_UNAVAIL);

	__endprotoent_p(ped);
	return (NS_UNAVAIL);
}

int
getprotoent_r(struct protoent *pptr, char *buffer, size_t buflen,
    struct protoent **result)
{
#ifdef NS_CACHING
	static const nss_cache_info cache_info = NS_MP_CACHE_INFO_INITIALIZER(
		protocols, (void *)nss_lt_all,
		__proto_marshal_func, __proto_unmarshal_func);
#endif
	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_getprotoent_r, (void *)nss_lt_all },
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};
	int rv, ret_errno;

	ret_errno = 0;
	*result = NULL;
	rv = nsdispatch(result, dtab, NSDB_PROTOCOLS, "getprotoent_r",
	    defaultsrc, pptr, buffer, buflen, &ret_errno);

	if (rv != NS_SUCCESS) {
		errno = ret_errno;
		return (ret_errno);
	}
	return (0);
}

void
setprotoent(int stayopen)
{
#ifdef NS_CACHING
	static const nss_cache_info cache_info = NS_MP_CACHE_INFO_INITIALIZER(
		protocols, (void *)nss_lt_all,
		NULL, NULL);
#endif

	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_setprotoent, NULL },
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};

	(void)nsdispatch(NULL, dtab, NSDB_PROTOCOLS, "setprotoent", defaultsrc,
		stayopen);
}

void
endprotoent(void)
{
#ifdef NS_CACHING
	static const nss_cache_info cache_info = NS_MP_CACHE_INFO_INITIALIZER(
		protocols, (void *)nss_lt_all,
		NULL, NULL);
#endif

	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_endprotoent, NULL },
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};

	(void)nsdispatch(NULL, dtab, NSDB_PROTOCOLS, "endprotoent", defaultsrc);
}

struct protoent *
getprotoent(void)
{
	struct protodata *pd;
	struct protoent *rval;

	if ((pd = __protodata_init()) == NULL)
		return (NULL);
	if (getprotoent_r(&pd->proto, pd->data, sizeof(pd->data), &rval) != 0)
		return (NULL);
	return (rval);
}
