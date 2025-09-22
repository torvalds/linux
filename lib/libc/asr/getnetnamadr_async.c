/*	$OpenBSD: getnetnamadr_async.c,v 1.26 2018/04/28 15:16:49 schwarze Exp $	*/
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

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <netdb.h>
#include <asr.h>

#include "asr_private.h"

struct asr_query *
getnetbyname_async(const char *name, void *asr)
{
	struct asr_query *as;

	if ((as = gethostbyname_async(name, asr)) != NULL)
		as->as_flags |= ASYNC_GETNET;
	return (as);
}
DEF_WEAK(getnetbyname_async);

struct asr_query *
getnetbyaddr_async(in_addr_t net, int family, void *asr)
{
	struct in_addr	  in;
	struct asr_query *as;

	in.s_addr = htonl(net);
	as = gethostbyaddr_async(&in, sizeof(in), family, asr);
	if (as != NULL)
		as->as_flags |= ASYNC_GETNET;
	return (as);
}
DEF_WEAK(getnetbyaddr_async);
