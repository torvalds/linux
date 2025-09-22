/*	$OpenBSD: gethostnamadr.c,v 1.13 2015/09/14 07:38:37 guenther Exp $	*/
/*
 * Copyright (c) 2012,2013 Eric Faurot <eric@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>	/* ALIGN */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <asr.h>
#include <errno.h>
#include <resolv.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int _gethostbyname(const char *, int, struct hostent *, char *, size_t,
    int *);
static int _fillhostent(const struct hostent *, struct hostent *, char *,
    size_t);

static struct hostent	 _hostent;
static char		 _entbuf[4096];

static char *_empty[] = { NULL, };

static int
_fillhostent(const struct hostent *h, struct hostent *r, char *buf, size_t len)
{
	char	**ptr, *end, *pos;
	size_t	n, i;
	int	naliases, naddrs;

	bzero(buf, len);
	bzero(r, sizeof(*r));
	r->h_aliases = _empty;
	r->h_addr_list = _empty;

	end = buf + len;
	ptr = (char **)ALIGN(buf);

	if ((char *)ptr >= end)
		return (ERANGE);

	for (naliases = 0; h->h_aliases[naliases]; naliases++)
		;
	for (naddrs = 0; h->h_addr_list[naddrs]; naddrs++)
		;

	pos = (char *)(ptr + (naliases + 1) + (naddrs + 1));
	if (pos >= end)
		return (ERANGE);

	r->h_name = NULL;
	r->h_addrtype = h->h_addrtype;
	r->h_length = h->h_length;
	r->h_aliases = ptr;
	r->h_addr_list = ptr + naliases + 1;

	n = strlcpy(pos, h->h_name, end - pos);
	if (n >= end - pos)
		return (ERANGE);
	r->h_name = pos;
	pos += n + 1;

	for (i = 0; i < naliases; i++) {
		n = strlcpy(pos, h->h_aliases[i], end - pos);
		if (n >= end - pos)
			return (ERANGE);
		r->h_aliases[i] = pos;
		pos += n + 1;
	}

	pos = (char *)ALIGN(pos);
	if (pos >= end)
		return (ERANGE);

	for (i = 0; i < naddrs; i++) {
		if (r->h_length > end - pos)
			return (ERANGE);
		memmove(pos, h->h_addr_list[i], r->h_length);
		r->h_addr_list[i] = pos;
		pos += r->h_length;
	}

	return (0);
}

static int
_gethostbyname(const char *name, int af, struct hostent *ret, char *buf,
    size_t buflen, int *h_errnop)
{
	struct asr_query *as;
	struct asr_result ar;
	int r;

	if (af == -1)
		as = gethostbyname_async(name, NULL);
	else
		as = gethostbyname2_async(name, af, NULL);

	if (as == NULL)
		return (errno);

	asr_run_sync(as, &ar);

	errno = ar.ar_errno;
	*h_errnop = ar.ar_h_errno;
	if (ar.ar_hostent == NULL)
		return (0);

	r = _fillhostent(ar.ar_hostent, ret, buf, buflen);
	free(ar.ar_hostent);

	return (r);
}

struct hostent *
gethostbyname(const char *name)
{
	struct hostent	*h;

	res_init();

	if (_res.options & RES_USE_INET6 &&
	    (h = gethostbyname2(name, AF_INET6)))
		return (h);

	return gethostbyname2(name, AF_INET);
}
DEF_WEAK(gethostbyname);

struct hostent *
gethostbyname2(const char *name, int af)
{
	int	r;

	res_init();

	r = _gethostbyname(name, af, &_hostent, _entbuf, sizeof(_entbuf),
	    &h_errno);
	if (r) {
		h_errno = NETDB_INTERNAL;
		errno = r;
	}

	if (h_errno)
		return (NULL);

	return (&_hostent);
}
DEF_WEAK(gethostbyname2);

struct hostent *
gethostbyaddr(const void *addr, socklen_t len, int af)
{
	struct asr_query *as;
	struct asr_result ar;
	int r;

	res_init();

	as = gethostbyaddr_async(addr, len, af, NULL);
	if (as == NULL) {
		h_errno = NETDB_INTERNAL;
		return (NULL);
	}

	asr_run_sync(as, &ar);

	errno = ar.ar_errno;
	h_errno = ar.ar_h_errno;
	if (ar.ar_hostent == NULL)
		return (NULL);

	r = _fillhostent(ar.ar_hostent, &_hostent, _entbuf, sizeof(_entbuf));
	free(ar.ar_hostent);

	if (r) {
		h_errno = NETDB_INTERNAL;
		errno = r;
		return (NULL);
	}

	return (&_hostent);
}
