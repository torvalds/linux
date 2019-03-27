/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1985, 1988, 1993
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
 * -
 * Portions Copyright (c) 1993 by Digital Equipment Corporation.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies, and that
 * the name of Digital Equipment Corporation not be used in advertising or
 * publicity pertaining to distribution of the document or software without
 * specific, written prior permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND DIGITAL EQUIPMENT CORP. DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS.   IN NO EVENT SHALL DIGITAL EQUIPMENT
 * CORPORATION BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 * -
 * --Copyright--
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)gethostnamadr.c	8.1 (Berkeley) 6/4/93";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <nsswitch.h>
#include <arpa/nameser.h>	/* XXX */
#include <resolv.h>		/* XXX */
#include "netdb_private.h"

void
_sethosthtent(int f, struct hostent_data *hed)
{
	if (!hed->hostf)
		hed->hostf = fopen(_PATH_HOSTS, "re");
	else
		rewind(hed->hostf);
	hed->stayopen = f;
}

void
_endhosthtent(struct hostent_data *hed)
{
	if (hed->hostf && !hed->stayopen) {
		(void) fclose(hed->hostf);
		hed->hostf = NULL;
	}
}

static int
gethostent_p(struct hostent *he, struct hostent_data *hed, int mapped,
    res_state statp)
{
	char *p, *bp, *ep;
	char *cp, **q;
	int af, len;
	char hostbuf[BUFSIZ + 1];

	if (!hed->hostf && !(hed->hostf = fopen(_PATH_HOSTS, "re"))) {
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		return (-1);
	}
 again:
	if (!(p = fgets(hostbuf, sizeof hostbuf, hed->hostf))) {
		RES_SET_H_ERRNO(statp, HOST_NOT_FOUND);
		return (-1);
	}
	if (*p == '#')
		goto again;
	cp = strpbrk(p, "#\n");
	if (cp != NULL)
		*cp = '\0';
	if (!(cp = strpbrk(p, " \t")))
		goto again;
	*cp++ = '\0';
	if (inet_pton(AF_INET6, p, hed->host_addr) > 0) {
		af = AF_INET6;
		len = IN6ADDRSZ;
	} else if (inet_pton(AF_INET, p, hed->host_addr) > 0) {
		if (mapped) {
			_map_v4v6_address((char *)hed->host_addr,
			    (char *)hed->host_addr);
			af = AF_INET6;
			len = IN6ADDRSZ;
		} else {
			af = AF_INET;
			len = INADDRSZ;
		}
	} else {
		goto again;
	}
	hed->h_addr_ptrs[0] = (char *)hed->host_addr;
	hed->h_addr_ptrs[1] = NULL;
	he->h_addr_list = hed->h_addr_ptrs;
	he->h_length = len;
	he->h_addrtype = af;
	while (*cp == ' ' || *cp == '\t')
		cp++;
	bp = hed->hostbuf;
	ep = hed->hostbuf + sizeof hed->hostbuf;
	he->h_name = bp;
	q = he->h_aliases = hed->host_aliases;
	if ((p = strpbrk(cp, " \t")) != NULL)
		*p++ = '\0';
	len = strlen(cp) + 1;
	if (ep - bp < len) {
		RES_SET_H_ERRNO(statp, NO_RECOVERY);
		return (-1);
	}
	strlcpy(bp, cp, ep - bp);
	bp += len;
	cp = p;
	while (cp && *cp) {
		if (*cp == ' ' || *cp == '\t') {
			cp++;
			continue;
		}
		if (q >= &hed->host_aliases[_MAXALIASES - 1])
			break;
		if ((p = strpbrk(cp, " \t")) != NULL)
			*p++ = '\0';
		len = strlen(cp) + 1;
		if (ep - bp < len)
			break;
		strlcpy(bp, cp, ep - bp);
		*q++ = bp;
		bp += len;
		cp = p;
	}
	*q = NULL;
	RES_SET_H_ERRNO(statp, NETDB_SUCCESS);
	return (0);
}

int
gethostent_r(struct hostent *hptr, char *buffer, size_t buflen,
    struct hostent **result, int *h_errnop)
{
	struct hostent_data *hed;
	struct hostent he;
	res_state statp;

	statp = __res_state();
	if ((statp->options & RES_INIT) == 0 && res_ninit(statp) == -1) {
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return (-1);
	}
	if ((hed = __hostent_data_init()) == NULL) {
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return (-1);
	}
	if (gethostent_p(&he, hed, statp->options & RES_USE_INET6, statp) != 0)
		return (-1);
	if (__copy_hostent(&he, hptr, buffer, buflen) != 0) {
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return ((errno != 0) ? errno : -1);
	}
	*result = hptr;
	return (0);
}

struct hostent *
gethostent(void)
{
	struct hostdata *hd;
	struct hostent *rval;
	int ret_h_errno;

	if ((hd = __hostdata_init()) == NULL)
		return (NULL);
	if (gethostent_r(&hd->host, hd->data, sizeof(hd->data), &rval,
	    &ret_h_errno) != 0)
		return (NULL);
	return (rval);
}

int
_ht_gethostbyname(void *rval, void *cb_data, va_list ap)
{
	const char *name;
	int af;
	char *buffer;
	size_t buflen;
	int *errnop, *h_errnop;
	struct hostent *hptr, he;
	struct hostent_data *hed;
	char **cp;
	res_state statp;
	int error;

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

	_sethosthtent(0, hed);
	while ((error = gethostent_p(&he, hed, 0, statp)) == 0) {
		if (he.h_addrtype != af)
			continue;
		if (he.h_addrtype == AF_INET &&
		    statp->options & RES_USE_INET6) {
			_map_v4v6_address(he.h_addr, he.h_addr);
			he.h_length = IN6ADDRSZ;
			he.h_addrtype = AF_INET6;
		}
		if (strcasecmp(he.h_name, name) == 0)
			break;
		for (cp = he.h_aliases; *cp != 0; cp++)
			if (strcasecmp(*cp, name) == 0)
				goto found;
	}
found:
	_endhosthtent(hed);

	if (error != 0) {
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
}

int
_ht_gethostbyaddr(void *rval, void *cb_data, va_list ap)
{
	const void *addr;
	socklen_t len;
	int af;
	char *buffer;
	size_t buflen;
	int *errnop, *h_errnop;
	struct hostent *hptr, he;
	struct hostent_data *hed;
	res_state statp;
	int error;

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

	_sethosthtent(0, hed);
	while ((error = gethostent_p(&he, hed, 0, statp)) == 0)
		if (he.h_addrtype == af && !bcmp(he.h_addr, addr, len)) {
			if (he.h_addrtype == AF_INET &&
			    statp->options & RES_USE_INET6) {
				_map_v4v6_address(he.h_addr, he.h_addr);
				he.h_length = IN6ADDRSZ;
				he.h_addrtype = AF_INET6;
			}
			break;
		}
	_endhosthtent(hed);

	if (error != 0)
		return (NS_NOTFOUND);
	if (__copy_hostent(&he, hptr, buffer, buflen) != 0) {
		*errnop = errno;
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return (NS_RETURN);
	}
	*((struct hostent **)rval) = hptr;
	return (NS_SUCCESS);
}
