/*	$OpenBSD: getnetnamadr.c,v 1.9 2015/01/16 16:48:51 deraadt Exp $	*/
/*
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
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
#include <stdlib.h>
#include <string.h>

static void _fillnetent(const struct netent *, struct netent *, char *buf,
    size_t);

static struct netent	 _netent;
static char		 _entbuf[4096];

static char *_empty[] = { NULL, };

static void
_fillnetent(const struct netent *e, struct netent *r, char *buf, size_t len)
{
	char	**ptr, *end, *pos;
	size_t	n, i;
	int	naliases;

	bzero(buf, len);
	bzero(r, sizeof(*r));
	r->n_aliases = _empty;

	end = buf + len;
	ptr = (char **)ALIGN(buf);

	if ((char *)ptr >= end)
		return;

	for (naliases = 0; e->n_aliases[naliases]; naliases++)
		;

	r->n_name = NULL;
	r->n_addrtype = e->n_addrtype;
	r->n_net = e->n_net;
	r->n_aliases = ptr;

	pos = (char *)(ptr + (naliases + 1));
	if (pos > end)
		r->n_aliases = _empty;

	n = strlcpy(pos, e->n_name, end - pos);
	if (n >= end - pos)
		return;
	r->n_name = pos;
	pos += n + 1;

	for (i = 0; i < naliases; i++) {
		n = strlcpy(pos, e->n_aliases[i], end - pos);
		if (n >= end - pos)
			return;
		r->n_aliases[i] = pos;
		pos += n + 1;
	}
}

struct netent *
getnetbyname(const char *name)
{
	struct asr_query *as;
	struct asr_result ar;

	res_init();

	as = getnetbyname_async(name, NULL);
	if (as == NULL) {
		h_errno = NETDB_INTERNAL;
		return (NULL);
	}

	asr_run_sync(as, &ar);

	errno = ar.ar_errno;
	h_errno = ar.ar_h_errno;
	if (ar.ar_netent == NULL)
		return (NULL);

	_fillnetent(ar.ar_netent, &_netent, _entbuf, sizeof(_entbuf));
	free(ar.ar_netent);

	return (&_netent);
}

struct netent *
getnetbyaddr(in_addr_t net, int type)
{
	struct asr_query *as;
	struct asr_result ar;

	res_init();

	as = getnetbyaddr_async(net, type, NULL);
	if (as == NULL) {
		h_errno = NETDB_INTERNAL;
		return (NULL);
	}

	asr_run_sync(as, &ar);

	errno = ar.ar_errno;
	h_errno = ar.ar_h_errno;
	if (ar.ar_netent == NULL)
		return (NULL);

	_fillnetent(ar.ar_netent, &_netent, _entbuf, sizeof(_entbuf));
	free(ar.ar_netent);

	return (&_netent);
}
