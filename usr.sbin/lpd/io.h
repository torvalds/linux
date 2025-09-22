/*	$OpenBSD: io.h,v 1.2 2022/12/28 21:30:17 jmc Exp $	*/

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

#include <event.h>

enum {
	IO_CONNECTED = 0, 	/* connection successful	*/
	IO_TLSREADY,		/* TLS started successfully	*/
	IO_DATAIN,		/* new data in input buffer	*/
	IO_LOWAT,		/* output queue running low	*/
	IO_CLOSED,		/* normally terminated		*/
	IO_DISCONNECTED,	/* error?			*/
	IO_TIMEOUT,		/* error?			*/
	IO_ERROR,		/* details?			*/
	IO_TLSERROR,		/* XXX - needs more work	*/
};

#define IO_IN	0x1
#define IO_OUT	0x2

struct io;

void io_trace(int);
const char* io_strio(struct io *);
const char* io_strevent(int);

/* IO management */
struct io *io_new(void);
void	io_free(struct io *);

/* IO setup */
int	io_set_callback(struct io *, void(*)(struct io *, int, void *), void *);
int	io_set_bindaddr(struct io *, const struct sockaddr *);
int	io_set_bufsize(struct io *, size_t);
void	io_set_timeout(struct io *, int);
void	io_set_lowat(struct io *, size_t);

/* State retrieval */
const char *io_error(struct io *);
int	io_fileno(struct io *);

/* Connection management */
int	io_attach(struct io *io, int);
int	io_detach(struct io *io);
int	io_close(struct io *io);
int	io_connect(struct io *, struct addrinfo *);
int	io_disconnect(struct io *io);
int	io_starttls(struct io *, void *);

/* Flow control  */
void	io_pause(struct io *, int);
void	io_resume(struct io *, int);

/* IO direction */
void	io_set_read(struct io *);
void	io_set_write(struct io *);

/* Output buffering */
int	io_write(struct io *, const void *, size_t);
int	io_writev(struct io *, const struct iovec *, int);
int	io_print(struct io *, const char *);
int	io_printf(struct io *, const char *, ...);
int	io_vprintf(struct io *, const char *, va_list);
size_t	io_queued(struct io *);

/* Buffered input */
void *	io_data(struct io *);
size_t	io_datalen(struct io *);
char *	io_getline(struct io *, size_t *);
void	io_drop(struct io *, size_t);
