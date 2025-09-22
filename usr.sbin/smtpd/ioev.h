/*	$OpenBSD: ioev.h,v 1.20 2021/07/28 19:39:50 benno Exp $	*/
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

enum {
	IO_CONNECTED = 0, 	/* connection successful	*/
	IO_TLSREADY,		/* TLS started successfully	*/
	IO_DATAIN,		/* new data in input buffer	*/
	IO_LOWAT,		/* output queue running low	*/
	IO_DISCONNECTED,	/* error?			*/
	IO_TIMEOUT,		/* error?			*/
	IO_ERROR,		/* details?			*/
};

#define IO_IN		0x01
#define IO_OUT		0x02

struct io;
struct tls;

void io_set_nonblocking(int);
void io_set_nolinger(int);

struct io *io_new(void);
void io_free(struct io *);
void io_set_read(struct io *);
void io_set_write(struct io *);
void io_set_fd(struct io *, int);
void io_set_callback(struct io *io, void(*)(struct io *, int, void *), void *);
void io_set_timeout(struct io *, int);
void io_set_lowat(struct io *, size_t);
void io_pause(struct io *, int);
void io_resume(struct io *, int);
void io_reload(struct io *);
int io_connect(struct io *, const struct sockaddr *, const struct sockaddr *);
int io_connect_tls(struct io *, struct tls *, const char *);
int io_accept_tls(struct io *, struct tls *);
const char* io_strio(struct io *);
const char* io_strevent(int);
const char* io_error(struct io *);
struct tls* io_tls(struct io *);
int io_fileno(struct io *);
int io_paused(struct io *, int);

/* Buffered output functions */
int io_write(struct io *, const void *, size_t);
int io_writev(struct io *, const struct iovec *, int);
int io_print(struct io *, const char *);
int io_printf(struct io *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
int io_vprintf(struct io *, const char *, va_list)
    __attribute__((__format__ (printf, 2, 0)));
size_t io_queued(struct io *);

/* Buffered input functions */
void* io_data(struct io *);
size_t io_datalen(struct io *);
char* io_getline(struct io *, size_t *);
void io_drop(struct io *, size_t);
