/*	$OpenBSD: iobuf.h,v 1.1.1.1 2018/04/27 16:14:36 eric Exp $	*/

/*
 * Copyright (c) 2017 Eric Faurot <eric@openbsd.org>
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

struct ioqbuf {
	struct ioqbuf	*next;
	char		*buf;
	size_t		 size;
	size_t		 wpos;
	size_t		 rpos;
};

struct iobuf {
	char		*buf;
	size_t		 max;
	size_t		 size;
	size_t		 wpos;
	size_t		 rpos;

	size_t		 queued;
	struct ioqbuf	*outq;
	struct ioqbuf	*outqlast;
};

#define IOBUF_WANT_READ		-1
#define IOBUF_WANT_WRITE	-2
#define IOBUF_CLOSED		-3
#define IOBUF_ERROR		-4
#define IOBUF_SSLERROR		-5

int	iobuf_init(struct iobuf *, size_t, size_t);
void	iobuf_clear(struct iobuf *);

int	iobuf_extend(struct iobuf *, size_t);
void	iobuf_normalize(struct iobuf *);
void	iobuf_drop(struct iobuf *, size_t);
size_t	iobuf_space(struct iobuf *);
size_t	iobuf_len(struct iobuf *);
size_t	iobuf_left(struct iobuf *);
char   *iobuf_data(struct iobuf *);
char   *iobuf_getline(struct iobuf *, size_t *);
ssize_t	iobuf_read(struct iobuf *, int);
ssize_t	iobuf_read_ssl(struct iobuf *, void *);

size_t  iobuf_queued(struct iobuf *);
void*   iobuf_reserve(struct iobuf *, size_t);
int	iobuf_queue(struct iobuf *, const void*, size_t);
int	iobuf_queuev(struct iobuf *, const struct iovec *, int);
int	iobuf_fqueue(struct iobuf *, const char *, ...);
int	iobuf_vfqueue(struct iobuf *, const char *, va_list);
int	iobuf_flush(struct iobuf *, int);
int	iobuf_flush_ssl(struct iobuf *, void *);
ssize_t	iobuf_write(struct iobuf *, int);
ssize_t	iobuf_write_ssl(struct iobuf *, void *);
