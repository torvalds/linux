/*	$OpenBSD: buffer.c,v 1.33 2022/12/27 23:05:55 jmc Exp $	*/

/*
 * Copyright (c) 2002, 2003 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "event.h"
#include "log.h"

struct evbuffer *
evbuffer_new(void)
{
	struct evbuffer *buffer;

	buffer = calloc(1, sizeof(struct evbuffer));

	return (buffer);
}

void
evbuffer_free(struct evbuffer *buffer)
{
	free(buffer->orig_buffer);
	free(buffer);
}

/*
 * This is a destructive add.  The data from one buffer moves into
 * the other buffer.
 */

#define SWAP(x,y) do { \
	(x)->buffer = (y)->buffer; \
	(x)->orig_buffer = (y)->orig_buffer; \
	(x)->misalign = (y)->misalign; \
	(x)->totallen = (y)->totallen; \
	(x)->off = (y)->off; \
} while (0)

int
evbuffer_add_buffer(struct evbuffer *outbuf, struct evbuffer *inbuf)
{
	int res;

	/* Short cut for better performance */
	if (outbuf->off == 0) {
		struct evbuffer tmp;
		size_t oldoff = inbuf->off;

		/* Swap them directly */
		SWAP(&tmp, outbuf);
		SWAP(outbuf, inbuf);
		SWAP(inbuf, &tmp);

		/*
		 * Optimization comes with a price; we need to notify the
		 * buffer if necessary of the changes. oldoff is the amount
		 * of data that we transferred from inbuf to outbuf
		 */
		if (inbuf->off != oldoff && inbuf->cb != NULL)
			(*inbuf->cb)(inbuf, oldoff, inbuf->off, inbuf->cbarg);
		if (oldoff && outbuf->cb != NULL)
			(*outbuf->cb)(outbuf, 0, oldoff, outbuf->cbarg);

		return (0);
	}

	res = evbuffer_add(outbuf, inbuf->buffer, inbuf->off);
	if (res == 0) {
		/* We drain the input buffer on success */
		evbuffer_drain(inbuf, inbuf->off);
	}

	return (res);
}

int
evbuffer_add_vprintf(struct evbuffer *buf, const char *fmt, va_list ap)
{
	char *buffer;
	size_t space;
	size_t oldoff = buf->off;
	int sz;
	va_list aq;

	/* make sure that at least some space is available */
	if (evbuffer_expand(buf, 64) < 0)
		return (-1);
	for (;;) {
		size_t used = buf->misalign + buf->off;
		buffer = (char *)buf->buffer + buf->off;
		assert(buf->totallen >= used);
		space = buf->totallen - used;

		va_copy(aq, ap);

		sz = vsnprintf(buffer, space, fmt, aq);

		va_end(aq);

		if (sz < 0)
			return (-1);
		if ((size_t)sz < space) {
			buf->off += sz;
			if (buf->cb != NULL)
				(*buf->cb)(buf, oldoff, buf->off, buf->cbarg);
			return (sz);
		}
		if (evbuffer_expand(buf, sz + 1) == -1)
			return (-1);

	}
	/* NOTREACHED */
}

int
evbuffer_add_printf(struct evbuffer *buf, const char *fmt, ...)
{
	int res = -1;
	va_list ap;

	va_start(ap, fmt);
	res = evbuffer_add_vprintf(buf, fmt, ap);
	va_end(ap);

	return (res);
}

/* Reads data from an event buffer and drains the bytes read */

int
evbuffer_remove(struct evbuffer *buf, void *data, size_t datlen)
{
	size_t nread = datlen;
	if (nread >= buf->off)
		nread = buf->off;

	memcpy(data, buf->buffer, nread);
	evbuffer_drain(buf, nread);

	return (nread);
}

/*
 * Reads a line terminated by either '\r\n', '\n\r' or '\r' or '\n'.
 * The returned buffer needs to be freed by the called.
 */

char *
evbuffer_readline(struct evbuffer *buffer)
{
	u_char *data = EVBUFFER_DATA(buffer);
	size_t len = EVBUFFER_LENGTH(buffer);
	char *line;
	size_t i;

	for (i = 0; i < len; i++) {
		if (data[i] == '\r' || data[i] == '\n')
			break;
	}

	if (i == len)
		return (NULL);

	if ((line = malloc(i + 1)) == NULL) {
		event_warn("%s: out of memory", __func__);
		return (NULL);
	}

	memcpy(line, data, i);
	line[i] = '\0';

	/*
	 * Some protocols terminate a line with '\r\n', so check for
	 * that, too.
	 */
	if ( i < len - 1 ) {
		char fch = data[i], sch = data[i+1];

		/* Drain one more character if needed */
		if ( (sch == '\r' || sch == '\n') && sch != fch )
			i += 1;
	}

	evbuffer_drain(buffer, i + 1);

	return (line);
}


char *
evbuffer_readln(struct evbuffer *buffer, size_t *n_read_out,
		enum evbuffer_eol_style eol_style)
{
	u_char *data = EVBUFFER_DATA(buffer);
	u_char *start_of_eol, *end_of_eol;
	size_t len = EVBUFFER_LENGTH(buffer);
	char *line;
	size_t i, n_to_copy, n_to_drain;

	if (n_read_out)
		*n_read_out = 0;

	/* depending on eol_style, set start_of_eol to the first character
	 * in the newline, and end_of_eol to one after the last character. */
	switch (eol_style) {
	case EVBUFFER_EOL_ANY:
		for (i = 0; i < len; i++) {
			if (data[i] == '\r' || data[i] == '\n')
				break;
		}
		if (i == len)
			return (NULL);
		start_of_eol = data+i;
		++i;
		for ( ; i < len; i++) {
			if (data[i] != '\r' && data[i] != '\n')
				break;
		}
		end_of_eol = data+i;
		break;
	case EVBUFFER_EOL_CRLF:
		end_of_eol = memchr(data, '\n', len);
		if (!end_of_eol)
			return (NULL);
		if (end_of_eol > data && *(end_of_eol-1) == '\r')
			start_of_eol = end_of_eol - 1;
		else
			start_of_eol = end_of_eol;
		end_of_eol++; /*point to one after the LF. */
		break;
	case EVBUFFER_EOL_CRLF_STRICT: {
		u_char *cp = data;
		while ((cp = memchr(cp, '\r', len-(cp-data)))) {
			if (cp < data+len-1 && *(cp+1) == '\n')
				break;
			if (++cp >= data+len) {
				cp = NULL;
				break;
			}
		}
		if (!cp)
			return (NULL);
		start_of_eol = cp;
		end_of_eol = cp+2;
		break;
	}
	case EVBUFFER_EOL_LF:
		start_of_eol = memchr(data, '\n', len);
		if (!start_of_eol)
			return (NULL);
		end_of_eol = start_of_eol + 1;
		break;
	default:
		return (NULL);
	}

	n_to_copy = start_of_eol - data;
	n_to_drain = end_of_eol - data;

	if ((line = malloc(n_to_copy+1)) == NULL) {
		event_warn("%s: out of memory", __func__);
		return (NULL);
	}

	memcpy(line, data, n_to_copy);
	line[n_to_copy] = '\0';

	evbuffer_drain(buffer, n_to_drain);
	if (n_read_out)
		*n_read_out = (size_t)n_to_copy;

	return (line);
}

/* Adds data to an event buffer */

static void
evbuffer_align(struct evbuffer *buf)
{
	memmove(buf->orig_buffer, buf->buffer, buf->off);
	buf->buffer = buf->orig_buffer;
	buf->misalign = 0;
}

/* Expands the available space in the event buffer to at least datlen */

int
evbuffer_expand(struct evbuffer *buf, size_t datlen)
{
	size_t used = buf->misalign + buf->off;

	assert(buf->totallen >= used);

	/* If we can fit all the data, then we don't have to do anything */
	if (buf->totallen - used >= datlen)
		return (0);
	/* If we would need to overflow to fit this much data, we can't
	 * do anything. */
	if (datlen > SIZE_MAX - buf->off)
		return (-1);

	/*
	 * If the misalignment fulfills our data needs, we just force an
	 * alignment to happen.  Afterwards, we have enough space.
	 */
	if (buf->totallen - buf->off >= datlen) {
		evbuffer_align(buf);
	} else {
		void *newbuf;
		size_t length = buf->totallen;
		size_t need = buf->off + datlen;

		if (length < 256)
			length = 256;
		if (need < SIZE_MAX / 2) {
			while (length < need) {
				length <<= 1;
			}
		} else {
			length = need;
		}

		if (buf->orig_buffer != buf->buffer)
			evbuffer_align(buf);
		if ((newbuf = recallocarray(buf->buffer, buf->totallen,
		    length, 1)) == NULL)
			return (-1);

		buf->orig_buffer = buf->buffer = newbuf;
		buf->totallen = length;
	}

	return (0);
}

int
evbuffer_add(struct evbuffer *buf, const void *data, size_t datlen)
{
	size_t used = buf->misalign + buf->off;
	size_t oldoff = buf->off;

	if (buf->totallen - used < datlen) {
		if (evbuffer_expand(buf, datlen) == -1)
			return (-1);
	}

	memcpy(buf->buffer + buf->off, data, datlen);
	buf->off += datlen;

	if (datlen && buf->cb != NULL)
		(*buf->cb)(buf, oldoff, buf->off, buf->cbarg);

	return (0);
}

void
evbuffer_drain(struct evbuffer *buf, size_t len)
{
	size_t oldoff = buf->off;

	if (len >= buf->off) {
		buf->off = 0;
		buf->buffer = buf->orig_buffer;
		buf->misalign = 0;
		goto done;
	}

	buf->buffer += len;
	buf->misalign += len;

	buf->off -= len;

 done:
	/* Tell someone about changes in this buffer */
	if (buf->off != oldoff && buf->cb != NULL)
		(*buf->cb)(buf, oldoff, buf->off, buf->cbarg);

}

/*
 * Reads data from a file descriptor into a buffer.
 */

#define EVBUFFER_MAX_READ	4096

int
evbuffer_read(struct evbuffer *buf, int fd, int howmuch)
{
	u_char *p;
	size_t oldoff = buf->off;
	int n = EVBUFFER_MAX_READ;

	if (ioctl(fd, FIONREAD, &n) == -1 || n <= 0) {
		n = EVBUFFER_MAX_READ;
	} else if (n > EVBUFFER_MAX_READ && n > howmuch) {
		/*
		 * It's possible that a lot of data is available for
		 * reading.  We do not want to exhaust resources
		 * before the reader has a chance to do something
		 * about it.  If the reader does not tell us how much
		 * data we should read, we artificially limit it.
		 */
		if ((size_t)n > buf->totallen << 2)
			n = buf->totallen << 2;
		if (n < EVBUFFER_MAX_READ)
			n = EVBUFFER_MAX_READ;
	}
	if (howmuch < 0 || howmuch > n)
		howmuch = n;

	/* If we don't have FIONREAD, we might waste some space here */
	if (evbuffer_expand(buf, howmuch) == -1)
		return (-1);

	/* We can append new data at this point */
	p = buf->buffer + buf->off;

	n = read(fd, p, howmuch);
	if (n == -1)
		return (-1);
	if (n == 0)
		return (0);

	buf->off += n;

	/* Tell someone about changes in this buffer */
	if (buf->off != oldoff && buf->cb != NULL)
		(*buf->cb)(buf, oldoff, buf->off, buf->cbarg);

	return (n);
}

int
evbuffer_write(struct evbuffer *buffer, int fd)
{
	int n;

	n = write(fd, buffer->buffer, buffer->off);
	if (n == -1)
		return (-1);
	if (n == 0)
		return (0);
	evbuffer_drain(buffer, n);

	return (n);
}

u_char *
evbuffer_find(struct evbuffer *buffer, const u_char *what, size_t len)
{
	u_char *search = buffer->buffer, *end = search + buffer->off;
	u_char *p;

	while (search < end &&
	    (p = memchr(search, *what, end - search)) != NULL) {
		if (p + len > end)
			break;
		if (memcmp(p, what, len) == 0)
			return (p);
		search = p + 1;
	}

	return (NULL);
}

void evbuffer_setcb(struct evbuffer *buffer,
    void (*cb)(struct evbuffer *, size_t, size_t, void *),
    void *cbarg)
{
	buffer->cb = cb;
	buffer->cbarg = cbarg;
}
