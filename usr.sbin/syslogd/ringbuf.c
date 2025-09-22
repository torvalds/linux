/* $OpenBSD: ringbuf.c,v 1.10 2016/10/16 22:12:50 bluhm Exp $ */

/*
 * Copyright (c) 2004 Damien Miller
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

/*
 * Simple ringbuffer for lines of text.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "syslogd.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

/* Initialise a ring buffer */
struct ringbuf *
ringbuf_init(size_t len)
{
	struct ringbuf *ret;

	if (len == 0 || (ret = malloc(sizeof(*ret))) == NULL)
		return (NULL);

	if ((ret->buf = malloc(len)) == NULL) {
		free(ret);
		return (NULL);
	}

	ret->len = len;
	ret->start = ret->end = 0;

	return (ret);
}

/* Free a ring buffer */
void
ringbuf_free(struct ringbuf *rb)
{
	free(rb->buf);
	free(rb);
}

/* Clear a ring buffer */
void
ringbuf_clear(struct ringbuf *rb)
{
	rb->start = rb->end = 0;
}

/* Return the number of bytes used in a ringbuffer */
size_t
ringbuf_used(struct ringbuf *rb)
{
	return ((rb->len + rb->end - rb->start) % rb->len);
}

/*
 * Append a line to a ring buffer, will delete lines from start
 * of buffer as necessary
 */
int
ringbuf_append_line(struct ringbuf *rb, char *line)
{
	size_t llen, used, copy_len;
	int overflow = 0;

	if (rb == NULL || line == NULL)
		return (-1);

	llen = strlen(line);
	if (llen == 0)
		return (-1);

	if (line[llen - 1] != '\n')
		llen++; /* one extra for appended '\n' */

	if (llen >= rb->len)
		return (-1);

	/*
	 * If necessary, advance start pointer to make room for appended
	 * string. Ensure that start pointer is at the beginning of a line
	 * once we are done (i.e move to after '\n').
	 */
	used = ringbuf_used(rb);
	if (used + llen >= rb->len) {
		rb->start = (rb->start + used + llen - rb->len) % rb->len;

		/* Find next '\n' */
		while (rb->buf[rb->start] != '\n')
			rb->start = (rb->start + 1) % rb->len;
		/* Skip it */
		rb->start = (rb->start + 1) % rb->len;

		overflow = 1;
	}

	/*
	 * Now append string, starting from last pointer and wrapping if
	 * necessary
	 */
	if (rb->end + llen > rb->len) {
		copy_len = rb->len - rb->end;
		memcpy(rb->buf + rb->end, line, copy_len);
		memcpy(rb->buf, line + copy_len, llen - copy_len - 1);
		rb->buf[llen - copy_len - 1] = '\n';
	} else {
		memcpy(rb->buf + rb->end, line, llen - 1);
		rb->buf[rb->end + llen - 1] = '\n';
	}

	rb->end = (rb->end + llen) % rb->len;

	return (overflow);
}

/*
 * Copy and nul-terminate a ringbuffer to a string.
 */
ssize_t
ringbuf_to_string(char *buf, size_t len, struct ringbuf *rb)
{
	size_t copy_len, n;

	if (buf == NULL || rb == NULL || len == 0)
		return (-1);

	copy_len = MINIMUM(len - 1, ringbuf_used(rb));

	if (copy_len == 0)
		return (copy_len);

	if (rb->start < rb->end)
		memcpy(buf, rb->buf + rb->start, copy_len);
	else {
		/* If the buffer is wrapped, copy each hunk separately */
		n = rb->len - rb->start;
		memcpy(buf, rb->buf + rb->start, MINIMUM(n, copy_len));
		if (copy_len > n)
			memcpy(buf + n, rb->buf,
			    MINIMUM(rb->end, copy_len - n));
	}
	buf[copy_len] = '\0';

	return (ringbuf_used(rb));
}
