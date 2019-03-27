/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1994, Garrett Wollman
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
 * THIS SOFTWARE IS PROVIDED BY THE CONTRIBUTORS ``AS IS'' AND
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
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include "reentrant.h"
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <nsswitch.h>
#include "un-namespace.h"
#include "netdb_private.h"
#ifdef NS_CACHING
#include "nscache.h"
#endif

/* Network lookup order if nsswitch.conf is broken or nonexistent */
static const ns_src default_src[] = {
	{ NSSRC_FILES, NS_SUCCESS },
	{ NSSRC_DNS, NS_SUCCESS },
	{ 0 }
};

NETDB_THREAD_ALLOC(netent_data)
NETDB_THREAD_ALLOC(netdata)

#ifdef NS_CACHING
static int
net_id_func(char *buffer, size_t *buffer_size, va_list ap, void *cache_mdata)
{
	char *name;
	uint32_t net;
	int type;

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
		net = va_arg(ap, uint32_t);
		type = va_arg(ap, int);

		desired_size = sizeof(enum nss_lookup_type) +
		    sizeof(uint32_t) + sizeof(int);
		if (desired_size > *buffer_size) {
			res = NS_RETURN;
			goto fin;
		}

		memcpy(buffer, &lookup_type, sizeof(enum nss_lookup_type));
		memcpy(buffer + sizeof(enum nss_lookup_type), &net,
		    sizeof(uint32_t));
		memcpy(buffer + sizeof(enum nss_lookup_type) + sizeof(uint32_t),
		    &type, sizeof(int));

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
net_marshal_func(char *buffer, size_t *buffer_size, void *retval, va_list ap,
    void *cache_mdata)
{
	char *name;
	uint32_t net;
	int type;
	struct netent *ne;
	char *orig_buf;
	size_t orig_buf_size;

	struct netent new_ne;
	size_t desired_size, size, aliases_size;
	char *p;
	char **alias;

	switch ((enum nss_lookup_type)cache_mdata) {
	case nss_lt_name:
		name = va_arg(ap, char *);
		break;
	case nss_lt_id:
		net = va_arg(ap, uint32_t);
		type = va_arg(ap, int);
	break;
	case nss_lt_all:
		break;
	default:
		/* should be unreachable */
		return (NS_UNAVAIL);
	}

	ne = va_arg(ap, struct netent *);
	orig_buf = va_arg(ap, char *);
	orig_buf_size = va_arg(ap, size_t);

	desired_size = _ALIGNBYTES + sizeof(struct netent) + sizeof(char *);
	if (ne->n_name != NULL)
		desired_size += strlen(ne->n_name) + 1;

	if (ne->n_aliases != NULL) {
		aliases_size = 0;
		for (alias = ne->n_aliases; *alias; ++alias) {
			desired_size += strlen(*alias) + 1;
			++aliases_size;
		}

		desired_size += _ALIGNBYTES +
		    (aliases_size + 1) * sizeof(char *);
	}

	if (*buffer_size < desired_size) {
		/* this assignment is here for future use */
		*buffer_size = desired_size;
		return (NS_RETURN);
	}

	memcpy(&new_ne, ne, sizeof(struct netent));

	*buffer_size = desired_size;
	memset(buffer, 0, desired_size);
	p = buffer + sizeof(struct netent) + sizeof(char *);
	memcpy(buffer + sizeof(struct netent), &p, sizeof(char *));
	p = (char *)_ALIGN(p);

	if (new_ne.n_name != NULL) {
		size = strlen(new_ne.n_name);
		memcpy(p, new_ne.n_name, size);
		new_ne.n_name = p;
		p += size + 1;
	}

	if (new_ne.n_aliases != NULL) {
		p = (char *)_ALIGN(p);
		memcpy(p, new_ne.n_aliases, sizeof(char *) * aliases_size);
		new_ne.n_aliases = (char **)p;
		p += sizeof(char *) * (aliases_size + 1);

		for (alias = new_ne.n_aliases; *alias; ++alias) {
			size = strlen(*alias);
			memcpy(p, *alias, size);
			*alias = p;
			p += size + 1;
		}
	}

	memcpy(buffer, &new_ne, sizeof(struct netent));
	return (NS_SUCCESS);
}

static int
net_unmarshal_func(char *buffer, size_t buffer_size, void *retval, va_list ap,
    void *cache_mdata)
{
	char *name;
	uint32_t net;
	int type;
	struct netent *ne;
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
		net = va_arg(ap, uint32_t);
		type = va_arg(ap, int);
		break;
	case nss_lt_all:
		break;
	default:
		/* should be unreachable */
		return (NS_UNAVAIL);
	}

	ne = va_arg(ap, struct netent *);
	orig_buf = va_arg(ap, char *);
	orig_buf_size = va_arg(ap, size_t);
	ret_errno = va_arg(ap, int *);

	if (orig_buf_size <
	    buffer_size - sizeof(struct netent) - sizeof(char *)) {
		*ret_errno = ERANGE;
		return (NS_RETURN);
	}

	memcpy(ne, buffer, sizeof(struct netent));
	memcpy(&p, buffer + sizeof(struct netent), sizeof(char *));

	orig_buf = (char *)_ALIGN(orig_buf);
	memcpy(orig_buf, buffer + sizeof(struct netent) + sizeof(char *) +
	    _ALIGN(p) - (size_t)p,
	    buffer_size - sizeof(struct netent) - sizeof(char *) -
	    _ALIGN(p) + (size_t)p);
	p = (char *)_ALIGN(p);

	NS_APPLY_OFFSET(ne->n_name, orig_buf, p, char *);
	if (ne->n_aliases != NULL) {
		NS_APPLY_OFFSET(ne->n_aliases, orig_buf, p, char **);

		for (alias = ne->n_aliases; *alias; ++alias)
			NS_APPLY_OFFSET(*alias, orig_buf, p, char *);
	}

	if (retval != NULL)
		*((struct netent **)retval) = ne;

	return (NS_SUCCESS);
}
#endif /* NS_CACHING */

static void
netent_data_free(void *ptr)
{
	struct netent_data *ned = ptr;

	if (ned == NULL)
		return;
	ned->stayopen = 0;
	_endnethtent(ned);
	free(ned);
}

static void
netdata_free(void *ptr)
{
	free(ptr);
}

int
__copy_netent(struct netent *ne, struct netent *nptr, char *buf, size_t buflen)
{
	char *cp;
	int i, n;
	int numptr, len;

	/* Find out the amount of space required to store the answer. */
	numptr = 1; /* NULL ptr */
	len = (char *)ALIGN(buf) - buf;
	for (i = 0; ne->n_aliases[i]; i++, numptr++) {
		len += strlen(ne->n_aliases[i]) + 1;
	}
	len += strlen(ne->n_name) + 1;
	len += numptr * sizeof(char*);

	if (len > (int)buflen) {
		errno = ERANGE;
		return (-1);
	}

	/* copy net value and type */
	nptr->n_addrtype = ne->n_addrtype;
	nptr->n_net = ne->n_net;

	cp = (char *)ALIGN(buf) + numptr * sizeof(char *);

	/* copy official name */
	n = strlen(ne->n_name) + 1;
	strcpy(cp, ne->n_name);
	nptr->n_name = cp;
	cp += n;

	/* copy aliases */
	nptr->n_aliases = (char **)ALIGN(buf);
	for (i = 0 ; ne->n_aliases[i]; i++) {
		n = strlen(ne->n_aliases[i]) + 1;
		strcpy(cp, ne->n_aliases[i]);
		nptr->n_aliases[i] = cp;
		cp += n;
	}
	nptr->n_aliases[i] = NULL;

	return (0);
}

int
getnetbyname_r(const char *name, struct netent *ne, char *buffer,
    size_t buflen, struct netent **result, int *h_errorp)
{
#ifdef NS_CACHING
	static const nss_cache_info cache_info =
    		NS_COMMON_CACHE_INFO_INITIALIZER(
		networks, (void *)nss_lt_name,
		net_id_func, net_marshal_func, net_unmarshal_func);
#endif
	static const ns_dtab dtab[] = {
		NS_FILES_CB(_ht_getnetbyname, NULL)
		{ NSSRC_DNS, _dns_getnetbyname, NULL },
		NS_NIS_CB(_nis_getnetbyname, NULL) /* force -DHESIOD */
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ 0 }
	};
	int rval, ret_errno = 0;

	rval = _nsdispatch((void *)result, dtab, NSDB_NETWORKS,
	    "getnetbyname_r", default_src, name, ne, buffer, buflen,
	    &ret_errno, h_errorp);

	if (rval != NS_SUCCESS) {
		errno = ret_errno;
		return ((ret_errno != 0) ? ret_errno : -1);
	}
	return (0);
}

int
getnetbyaddr_r(uint32_t addr, int af, struct netent *ne, char *buffer,
    size_t buflen, struct netent **result, int *h_errorp)
{
#ifdef NS_CACHING
	static const nss_cache_info cache_info =
    		NS_COMMON_CACHE_INFO_INITIALIZER(
		networks, (void *)nss_lt_id,
		net_id_func, net_marshal_func, net_unmarshal_func);
#endif
	static const ns_dtab dtab[] = {
		NS_FILES_CB(_ht_getnetbyaddr, NULL)
		{ NSSRC_DNS, _dns_getnetbyaddr, NULL },
		NS_NIS_CB(_nis_getnetbyaddr, NULL) /* force -DHESIOD */
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ 0 }
	};
	int rval, ret_errno = 0;

	rval = _nsdispatch((void *)result, dtab, NSDB_NETWORKS,
	    "getnetbyaddr_r", default_src, addr, af, ne, buffer, buflen,
	    &ret_errno, h_errorp);

	if (rval != NS_SUCCESS) {
		errno = ret_errno;
		return ((ret_errno != 0) ? ret_errno : -1);
	}
	return (0);
}

struct netent *
getnetbyname(const char *name)
{
	struct netdata *nd;
	struct netent *rval;
	int ret_h_errno;

	if ((nd = __netdata_init()) == NULL)
		return (NULL);
	if (getnetbyname_r(name, &nd->net, nd->data, sizeof(nd->data), &rval,
	    &ret_h_errno) != 0)
		return (NULL);
	return (rval);
}

struct netent *
getnetbyaddr(uint32_t addr, int af)
{
	struct netdata *nd;
	struct netent *rval;
	int ret_h_errno;

	if ((nd = __netdata_init()) == NULL)
		return (NULL);
	if (getnetbyaddr_r(addr, af, &nd->net, nd->data, sizeof(nd->data),
	    &rval, &ret_h_errno) != 0)
		return (NULL);
	return (rval);
}

void
setnetent(int stayopen)
{
	struct netent_data *ned;

	if ((ned = __netent_data_init()) == NULL)
		return;
	_setnethtent(stayopen, ned);
	_setnetdnsent(stayopen);
}

void
endnetent(void)
{
	struct netent_data *ned;

	if ((ned = __netent_data_init()) == NULL)
		return;
	_endnethtent(ned);
	_endnetdnsent();
}
