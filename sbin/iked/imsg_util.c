/*	$OpenBSD: imsg_util.c,v 1.22 2023/12/12 15:52:58 claudio Exp $	*/

/*
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
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

#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <event.h>

#include "iked.h"

/*
 * Extending the imsg buffer API for internal use
 */

struct ibuf *
ibuf_new(const void *data, size_t len)
{
	struct ibuf	*buf;

	if ((buf = ibuf_dynamic(len,
	    IKED_MSGBUF_MAX)) == NULL)
		return (NULL);

	if (len == 0)
		return (buf);

	if (data == NULL) {
		if (ibuf_add_zero(buf, len) != 0) {
			ibuf_free(buf);
			return (NULL);
		}
	} else {
		if (ibuf_add(buf, data, len) != 0) {
			ibuf_free(buf);
			return (NULL);
		}
	}

	return (buf);
}

struct ibuf *
ibuf_static(void)
{
	return ibuf_open(IKED_MSGBUF_MAX);
}

size_t
ibuf_length(struct ibuf *buf)
{
	if (buf == NULL)
		return (0);
	return (ibuf_size(buf));
}

struct ibuf *
ibuf_getdata(struct ibuf *buf, size_t len)
{
	struct ibuf tmp;

	if (ibuf_get_ibuf(buf, len, &tmp) == -1)
		return (NULL);

	return (ibuf_new(ibuf_data(&tmp), ibuf_size(&tmp)));
}

struct ibuf *
ibuf_dup(struct ibuf *buf)
{
	if (buf == NULL)
		return (NULL);
	return (ibuf_new(ibuf_data(buf), ibuf_size(buf)));
}

struct ibuf *
ibuf_random(size_t len)
{
	struct ibuf	*buf;
	void		*ptr;

	if ((buf = ibuf_open(len)) == NULL)
		return (NULL);
	if ((ptr = ibuf_reserve(buf, len)) == NULL) {
		ibuf_free(buf);
		return (NULL);
	}
	arc4random_buf(ptr, len);
	return (buf);
}

int
ibuf_setsize(struct ibuf *buf, size_t len)
{
	if (len > buf->size)
		return (-1);
	buf->wpos = len;
	return (0);
}
