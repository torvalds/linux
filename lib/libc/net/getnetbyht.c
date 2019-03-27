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

/* Portions Copyright (c) 1993 Carlos Leandro and Rui Salgueiro
 *	Dep. Matematica Universidade de Coimbra, Portugal, Europe
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * from getnetent.c	1.1 (Coimbra) 93/06/02
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)getnetent.c	8.1 (Berkeley) 6/4/93";
static char orig_rcsid[] = "From: Id: getnetent.c,v 8.4 1997/06/01 20:34:37 vixie Exp";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <errno.h>
#include <netdb.h>
#include <resolv.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <nsswitch.h>
#include "netdb_private.h"

void
_setnethtent(int f, struct netent_data *ned)
{

	if (ned->netf == NULL)
		ned->netf = fopen(_PATH_NETWORKS, "re");
	else
		rewind(ned->netf);
	ned->stayopen |= f;
}

void
_endnethtent(struct netent_data *ned)
{

	if (ned->netf) {
		fclose(ned->netf);
		ned->netf = NULL;
	}
	ned->stayopen = 0;
}

static int
getnetent_p(struct netent *ne, struct netent_data *ned)
{
	char *p, *bp, *ep;
	char *cp, **q;
	int len;
	char line[BUFSIZ + 1];

	if (ned->netf == NULL &&
	    (ned->netf = fopen(_PATH_NETWORKS, "re")) == NULL)
		return (-1);
again:
	p = fgets(line, sizeof line, ned->netf);
	if (p == NULL)
		return (-1);
	if (*p == '#')
		goto again;
	cp = strpbrk(p, "#\n");
	if (cp != NULL)
		*cp = '\0';
	bp = ned->netbuf;
	ep = ned->netbuf + sizeof ned->netbuf;
	ne->n_name = bp;
	cp = strpbrk(p, " \t");
	if (cp == NULL)
		goto again;
	*cp++ = '\0';
	len = strlen(p) + 1;
	if (ep - bp < len) {
		RES_SET_H_ERRNO(__res_state(), NO_RECOVERY);
		return (-1);
	}
	strlcpy(bp, p, ep - bp);
	bp += len;
	while (*cp == ' ' || *cp == '\t')
		cp++;
	p = strpbrk(cp, " \t");
	if (p != NULL)
		*p++ = '\0';
	ne->n_net = inet_network(cp);
	ne->n_addrtype = AF_INET;
	q = ne->n_aliases = ned->net_aliases;
	if (p != NULL) {
		cp = p;
		while (cp && *cp) {
			if (*cp == ' ' || *cp == '\t') {
				cp++;
				continue;
			}
			if (q >= &ned->net_aliases[_MAXALIASES - 1])
				break;
			p = strpbrk(cp, " \t");
			if (p != NULL)
				*p++ = '\0';
			len = strlen(cp) + 1;
			if (ep - bp < len)
				break;
			strlcpy(bp, cp, ep - bp);
			*q++ = bp;
			bp += len;
			cp = p;
		}
	}
	*q = NULL;
	return (0);
}

int
getnetent_r(struct netent *nptr, char *buffer, size_t buflen,
    struct netent **result, int *h_errnop)
{
	struct netent_data *ned;
	struct netent ne;
	res_state statp;

	statp = __res_state();
	if ((ned = __netent_data_init()) == NULL) {
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return (-1);
	}
	if (getnetent_p(&ne, ned) != 0)
		return (-1);
	if (__copy_netent(&ne, nptr, buffer, buflen) != 0) {
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return ((errno != 0) ? errno : -1);
	}
	*result = nptr;
	return (0);
}

struct netent *
getnetent(void)
{
	struct netdata *nd;
	struct netent *rval;
	int ret_h_errno;

	if ((nd = __netdata_init()) == NULL)
		return (NULL);
	if (getnetent_r(&nd->net, nd->data, sizeof(nd->data), &rval,
	    &ret_h_errno) != 0)
		return (NULL);
	return (rval);
}

int
_ht_getnetbyname(void *rval, void *cb_data, va_list ap)
{
	const char *name;
	char *buffer;
	size_t buflen;
	int *errnop, *h_errnop;
	struct netent *nptr, ne;
	struct netent_data *ned;
	char **cp;
	res_state statp;
	int error;

	name = va_arg(ap, const char *);
	nptr = va_arg(ap, struct netent *);
	buffer = va_arg(ap, char *);
	buflen = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);
	h_errnop = va_arg(ap, int *);

	statp = __res_state();
	if ((ned = __netent_data_init()) == NULL) {
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return (NS_UNAVAIL);
	}

	_setnethtent(ned->stayopen, ned);
	while ((error = getnetent_p(&ne, ned)) == 0) {
		if (strcasecmp(ne.n_name, name) == 0)
			break;
		for (cp = ne.n_aliases; *cp != 0; cp++)
			if (strcasecmp(*cp, name) == 0)
				goto found;
	}
found:
	if (!ned->stayopen)
		_endnethtent(ned);
	if (error != 0) {
		*h_errnop = statp->res_h_errno;
		return (NS_NOTFOUND);
	}
	if (__copy_netent(&ne, nptr, buffer, buflen) != 0) {
		*errnop = errno;
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return (NS_RETURN);
	}
	*((struct netent **)rval) = nptr;
	return (NS_SUCCESS);
}

int
_ht_getnetbyaddr(void *rval, void *cb_data, va_list ap)
{
	uint32_t net;
	int type;
	char *buffer;
	size_t buflen;
	int *errnop, *h_errnop;
	struct netent *nptr, ne;
	struct netent_data *ned;
	res_state statp;
	int error;

	net = va_arg(ap, uint32_t);
	type = va_arg(ap, int);
	nptr = va_arg(ap, struct netent *);
	buffer = va_arg(ap, char *);
	buflen = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);
	h_errnop = va_arg(ap, int *);

	statp = __res_state();
	if ((ned = __netent_data_init()) == NULL) {
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return (NS_UNAVAIL);
	}

	_setnethtent(ned->stayopen, ned);
	while ((error = getnetent_p(&ne, ned)) == 0)
		if (ne.n_addrtype == type && ne.n_net == net)
			break;
	if (!ned->stayopen)
		_endnethtent(ned);
	if (error != 0) {
		*h_errnop = statp->res_h_errno;
		return (NS_NOTFOUND);
	}
	if (__copy_netent(&ne, nptr, buffer, buflen) != 0) {
		*errnop = errno;
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return (NS_RETURN);
	}
	*((struct netent **)rval) = nptr;
	return (NS_SUCCESS);
}
