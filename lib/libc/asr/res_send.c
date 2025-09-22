/*	$OpenBSD: res_send.c,v 1.8 2014/03/26 18:13:15 eric Exp $	*/
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
#include <string.h>
#include <stdlib.h>

int
res_send(const u_char *buf, int buflen, u_char *ans, int anslen)
{
	struct asr_query *as;
	struct asr_result ar;
	size_t len;

	res_init();

	if (ans == NULL || anslen <= 0) {
		errno = EINVAL;
		return (-1);
	}

	as = res_send_async(buf, buflen, NULL);
	if (as == NULL)
		return (-1); /* errno set */

	asr_run_sync(as, &ar);

	if (ar.ar_errno) {
		errno = ar.ar_errno;
		return (-1);
	}

	len = anslen;
	if (ar.ar_datalen < len)
		len = ar.ar_datalen;
	memmove(ans, ar.ar_data, len);
	free(ar.ar_data);

	return (ar.ar_datalen);
}
