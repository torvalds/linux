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

#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <nsswitch.h>
#include <resolv.h>		/* XXX */
#ifdef YP
#include <rpc/rpc.h>
#include <rpcsvc/yp_prot.h>
#include <rpcsvc/ypclnt.h>
#endif
#include "netdb_private.h"

#ifdef YP
static int
_gethostbynis(const char *name, char *map, int af, struct hostent *he,
    struct hostent_data *hed)
{
	char *p, *bp, *ep;
	char *cp, **q;
	char *result;
	int resultlen, size, addrok = 0;
	char ypbuf[YPMAXRECORD + 2];
	res_state statp;

	statp = __res_state();
	switch(af) {
	case AF_INET:
		size = NS_INADDRSZ;
		break;
	case AF_INET6:
		size = NS_IN6ADDRSZ;
		break;
	default:
		errno = EAFNOSUPPORT;
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		return (-1);
	}

	if (hed->yp_domain == (char *)NULL)
		if (yp_get_default_domain (&hed->yp_domain)) {
			RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
			return (-1);
		}

	if (yp_match(hed->yp_domain, map, name, strlen(name), &result,
	    &resultlen)) {
		RES_SET_H_ERRNO(statp, HOST_NOT_FOUND);
		return (-1);
	}

	/* avoid potential memory leak */
	bcopy((char *)result, (char *)&ypbuf, resultlen);
	ypbuf[resultlen] = '\0';
	free(result);
	result = (char *)&ypbuf;

	if ((cp = strchr(result, '\n')))
		*cp = '\0';

	cp = strpbrk(result, " \t");
	*cp++ = '\0';
	he->h_addr_list = hed->h_addr_ptrs;
	he->h_addr = (char *)hed->host_addr;
	switch (af) {
	case AF_INET:
		addrok = inet_aton(result, (struct in_addr *)hed->host_addr);
		if (addrok != 1)
			break;
		if (statp->options & RES_USE_INET6) {
			_map_v4v6_address((char *)hed->host_addr,
			    (char *)hed->host_addr);
			af = AF_INET6;
			size = NS_IN6ADDRSZ;
		}
		break;
	case AF_INET6:
		addrok = inet_pton(af, result, hed->host_addr);
		break;
	}
	if (addrok != 1) {
		RES_SET_H_ERRNO(statp, HOST_NOT_FOUND);
		return (-1);
	}
	he->h_addr_list[1] = NULL;
	he->h_length = size;
	he->h_addrtype = af;
	while (*cp == ' ' || *cp == '\t')
		cp++;
	bp = hed->hostbuf;
	ep = hed->hostbuf + sizeof hed->hostbuf;
	he->h_name = bp;
	q = he->h_aliases = hed->host_aliases;
	p = strpbrk(cp, " \t");
	if (p != NULL)
		*p++ = '\0';
	size = strlen(cp) + 1;
	if (ep - bp < size) {
		RES_SET_H_ERRNO(statp, NO_RECOVERY);
		return (-1);
	}
	strlcpy(bp, cp, ep - bp);
	bp += size;
	cp = p;
	while (cp && *cp) {
		if (*cp == ' ' || *cp == '\t') {
			cp++;
			continue;
		}
		if (q >= &hed->host_aliases[_MAXALIASES - 1])
			break;
		p = strpbrk(cp, " \t");
		if (p != NULL)
			*p++ = '\0';
		size = strlen(cp) + 1;
		if (ep - bp < size)
			break;
		strlcpy(bp, cp, ep - bp);
		*q++ = bp;
		bp += size;
		cp = p;
	}
	*q = NULL;
	return (0);
}

static int
_gethostbynisname_r(const char *name, int af, struct hostent *he,
    struct hostent_data *hed)
{
	char *map;

	switch (af) {
	case AF_INET:
		map = "hosts.byname";
		break;
	default:
		map = "ipnodes.byname";
		break;
	}
	return (_gethostbynis(name, map, af, he, hed));
}

static int
_gethostbynisaddr_r(const void *addr, socklen_t len, int af,
    struct hostent *he, struct hostent_data *hed)
{
	char *map;
	char numaddr[46];

	switch (af) {
	case AF_INET:
		map = "hosts.byaddr";
		break;
	default:
		map = "ipnodes.byaddr";
		break;
	}
	if (inet_ntop(af, addr, numaddr, sizeof(numaddr)) == NULL)
		return (-1);
	return (_gethostbynis(numaddr, map, af, he, hed));
}
#endif /* YP */

int
_nis_gethostbyname(void *rval, void *cb_data, va_list ap)
{
#ifdef YP
	const char *name;
	int af;
	char *buffer;
	size_t buflen;
	int *errnop, *h_errnop;
	struct hostent *hptr, he;
	struct hostent_data *hed;
	res_state statp;

	name = va_arg(ap, const char *);
	af = va_arg(ap, int);
	hptr = va_arg(ap, struct hostent *);
	buffer = va_arg(ap, char *);
	buflen = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);
	h_errnop = va_arg(ap, int *);

	*((struct hostent **)rval) = NULL;

	statp = __res_state();
	if ((hed = __hostent_data_init()) == NULL) {
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return (NS_NOTFOUND);
	}

	if (_gethostbynisname_r(name, af, &he, hed) != 0) {
		*h_errnop = statp->res_h_errno;
		return (NS_NOTFOUND);
	}
	if (__copy_hostent(&he, hptr, buffer, buflen) != 0) {
		*errnop = errno;
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return (NS_RETURN);
	}
	*((struct hostent **)rval) = hptr;
	return (NS_SUCCESS);
#else
	*((struct hostent **)rval) = NULL;
	return (NS_UNAVAIL);
#endif
}

int
_nis_gethostbyaddr(void *rval, void *cb_data, va_list ap)
{
#ifdef YP
	const void *addr;
	socklen_t len;
	int af;
	char *buffer;
	size_t buflen;
	int *errnop, *h_errnop;
	struct hostent *hptr, he;
	struct hostent_data *hed;
	res_state statp;

	addr = va_arg(ap, const void *);
	len = va_arg(ap, socklen_t);
	af = va_arg(ap, int);
	hptr = va_arg(ap, struct hostent *);
	buffer = va_arg(ap, char *);
	buflen = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);
	h_errnop = va_arg(ap, int *);

	*((struct hostent **)rval) = NULL;

	statp = __res_state();
	if ((hed = __hostent_data_init()) == NULL) {
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return (NS_NOTFOUND);
	}

	if (_gethostbynisaddr_r(addr, len, af, &he, hed) != 0) {
		*h_errnop = statp->res_h_errno;
		return (NS_NOTFOUND);
	}
	if (__copy_hostent(&he, hptr, buffer, buflen) != 0) {
		*errnop = errno;
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return (NS_RETURN);
	}
	*((struct hostent **)rval) = hptr;
	return (NS_SUCCESS);
#else
	*((struct hostent **)rval) = NULL;
	return (NS_UNAVAIL);
#endif
}
