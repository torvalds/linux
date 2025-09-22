/*	$OpenBSD: getaddrinfo.c,v 1.9 2015/10/08 14:08:44 eric Exp $	*/
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
#include <netinet/in.h>
#include <netdb.h>

#include <asr.h>
#include <errno.h>
#include <resolv.h>

int
getaddrinfo(const char *hostname, const char *servname,
    const struct addrinfo *hints, struct addrinfo **res)
{
	struct asr_query *as;
	struct asr_result ar;
	int		 saved_errno = errno;

	if (hints == NULL || (hints->ai_flags & AI_NUMERICHOST) == 0)
		res_init();

	as = getaddrinfo_async(hostname, servname, hints, NULL);
	if (as == NULL) {
		if (errno == ENOMEM) {
			errno = saved_errno;
			return (EAI_MEMORY);
		}
		return (EAI_SYSTEM);
	}

	asr_run_sync(as, &ar);

	*res = ar.ar_addrinfo;
	if (ar.ar_gai_errno == EAI_SYSTEM)
		errno = ar.ar_errno;

	return (ar.ar_gai_errno);
}
DEF_WEAK(getaddrinfo);
