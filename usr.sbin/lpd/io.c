/*	$OpenBSD: io.c,v 1.1.1.1 2018/04/27 16:14:36 eric Exp $	*/

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
#include <sys/queue.h>
#include <sys/socket.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "io.h"
#include "iobuf.h"
#include "log.h"

#ifdef IO_SSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

enum {
	IO_STATE_DOWN,
	IO_STATE_UP,
	IO_STATE_CONNECT,
	IO_STATE_CONNECT_TLS,
	IO_STATE_ACCEPT_TLS
};

#define IO_PAUSE_IN		IO_IN
#define IO_PAUSE_OUT		IO_OUT

#define IO_READ			0x0100
#define IO_WRITE		0x0200
#define IO_RW			(IO_READ | IO_WRITE)
#define IO_RESET		0x1000
#define IO_HELD			0x2000

struct io {
	int		 sock;
	void		*arg;
	void		(*cb)(struct io*, int, void *);
	struct iobuf	 iobuf;
	size_t		 lowat;
	int		 timeout;
	int		 flags;
	int		 state;
	struct event	 ev;
	void		*tls;
	const char	*error; /* only valid immediately on callback */
	struct sockaddr *bind;
	struct addrinfo	*ai;	/* for connecting */
};

static const char* io_strflags(int);
static const char* io_strevents(short);

static void io_reload(struct io *);
static void io_reset(struct io *, short, void (*)(int, short, void*));
static void io_frame_enter(const char *, struct io *, int);
static void io_frame_leave(struct io *);
static void io_hold(struct io *);
static void io_release(struct io *);
static void io_callback(struct io*, int);
static void io_dispatch(int, short, void *);
static void io_dispatch_connect(int, short, void *);
static int io_connect_next(struct io *);

#ifdef IO_SSL
void ssl_error(const char *); /* XXX external */
static const char* io_ssl_error(void);
static void io_dispatch_accept_tls(int, short, void *);
static void io_dispatch_connect_tls(int, short, void *);
static void io_dispatch_read_tls(int, short, void *);
static void io_dispatch_write_tls(int, short, void *);
static void io_reload_tls(struct io *io);
#endif

static struct io *current = NULL;
static long long unsigned frame = 0;
static int _io_trace = 0;

static const char *states[] = {
    "DOWN",
    "UP",
    "CONNECT",
    "CONNECT_TLS",
    "ACCEPT_TLS"
};

#define io_debug(args...) do { if (_io_trace) log_debug(args); } while(0)
#define IO_READING(io) (((io)->flags & IO_RW) != IO_WRITE)
#define IO_WRITING(io) (((io)->flags & IO_RW) != IO_READ)

void
io_trace(int on)
{
	_io_trace = on;
}

const char*
io_strio(struct io *io)
{
	static char buf[128];
	char ssl[128];

	ssl[0] = '\0';
#ifdef IO_SSL
	if (io->tls) {
		(void)snprintf(ssl, sizeof ssl, " ssl=%s:%s:%d",
		    SSL_get_version(io->tls),
		    SSL_get_cipher_name(io->tls),
		    SSL_get_cipher_bits(io->tls, NULL));
	}
#endif
	(void)snprintf(buf, sizeof buf,
	    "<io:%p st=%s, fd=%d to=%d fl=%s%s ib=%zu ob=%zu>",
	    io, states[io->state], io->sock, io->timeout,
	    io_strflags(io->flags), ssl, io_datalen(io), io_queued(io));

	return buf;
}

const char*
io_strevent(int evt)
{
	static char buf[32];

	switch (evt) {
	case IO_CONNECTED:
		return "IO_CONNECTED";
	case IO_TLSREADY:
		return "IO_TLSREADY";
	case IO_DATAIN:
		return "IO_DATAIN";
	case IO_LOWAT:
		return "IO_LOWAT";
	case IO_CLOSED:
		return "IO_CLOSED";
	case IO_DISCONNECTED:
		return "IO_DISCONNECTED";
	case IO_TIMEOUT:
		return "IO_TIMEOUT";
	case IO_ERROR:
		return "IO_ERROR";
	case IO_TLSERROR:
		return "IO_TLSERROR";
	default:
		(void)snprintf(buf, sizeof(buf), "IO_? %d", evt);
		return buf;
	}
}

struct io *
io_new(void)
{
	struct io *io;

	io = calloc(1, sizeof(*io));
	if (io == NULL)
		return NULL;

	iobuf_init(&io->iobuf, 0, 0);
	io->sock = -1;
	io->timeout = -1;

	return io;
}

void
io_free(struct io *io)
{
	io_debug("%s(%p)", __func__, io);

	/* the current io is virtually dead */
	if (io == current)
		current = NULL;

#ifdef IO_SSL
	if (io->tls) {
		SSL_free(io->tls);
		io->tls = NULL;
	}
#endif

	if (io->ai)
		freeaddrinfo(io->ai);
	if (event_initialized(&io->ev))
		event_del(&io->ev);
	if (io->sock != -1) {
		(void)close(io->sock);
		io->sock = -1;
	}

	iobuf_clear(&io->iobuf);
	free(io->bind);
	free(io);
}

int
io_set_callback(struct io *io, void(*cb)(struct io *, int, void *), void *arg)
{
	io->cb = cb;
	io->arg = arg;

	return 0;
}

int
io_set_bindaddr(struct io *io, const struct sockaddr *sa)
{
	struct sockaddr *t;

	if (io->state != IO_STATE_DOWN) {
		errno = EISCONN;
		return -1;
	}

	t = malloc(sa->sa_len);
	if (t == NULL)
		return -1;
	memmove(t, sa, sa->sa_len);

	free(io->bind);
	io->bind = t;

	return 0;
}

int
io_set_bufsize(struct io *io, size_t sz)
{
	errno = ENOSYS;
	return -1;
}

void
io_set_timeout(struct io *io, int msec)
{
	io_debug("%s(%p, %d)", __func__, io, msec);

	io->timeout = msec;
}

void
io_set_lowat(struct io *io, size_t lowat)
{
	io_debug("%s(%p, %zu)", __func__, io, lowat);

	io->lowat = lowat;
}

const char *
io_error(struct io *io)
{
	const char *e;

	e = io->error;
	io->error = NULL;
	return e;
}

int
io_fileno(struct io *io)
{
	return io->sock;
}

int
io_attach(struct io *io, int sock)
{
	if (io->state != IO_STATE_DOWN) {
		errno = EISCONN;
		return -1;
	}

	io->state = IO_STATE_UP;
	io->sock = sock;
	io_reload(io);
	return 0;
}

int
io_detach(struct io *io)
{
	errno = ENOSYS;
	return -1;
}

int
io_close(struct io *io)
{
	errno = ENOSYS;
	return -1;
}

int
io_connect(struct io *io, struct addrinfo *ai)
{
	if (ai == NULL) {
		errno = EINVAL;
		fatal("%s", __func__);
		return -1;
	}

	if (io->state != IO_STATE_DOWN) {
		freeaddrinfo(ai);
		errno = EISCONN;
		fatal("%s", __func__);
		return -1;
	}

	io->ai = ai;
	return io_connect_next(io);
}

int
io_disconnect(struct io *io)
{
	errno = ENOSYS;
	fatal("%s", __func__);
	return -1;
}

int
io_starttls(struct io *io, void *ssl)
{
#ifdef IO_SSL
	int mode;

	mode = io->flags & IO_RW;
	if (mode == 0 || mode == IO_RW)
		fatalx("%s: full-duplex or unset", __func__);

	if (io->tls)
		fatalx("%s: SSL already started", __func__);
	io->tls = ssl;

	if (SSL_set_fd(io->tls, io->sock) == 0) {
		ssl_error("io_start_tls:SSL_set_fd");
		return -1;
	}

	if (mode == IO_WRITE) {
		io->state = IO_STATE_CONNECT_TLS;
		SSL_set_connect_state(io->tls);
		io_reset(io, EV_WRITE, io_dispatch_connect_tls);
	} else {
		io->state = IO_STATE_ACCEPT_TLS;
		SSL_set_accept_state(io->tls);
		io_reset(io, EV_READ, io_dispatch_accept_tls);
	}

	return 0;
#else
	errno = ENOSYS;
	return -1;
#endif
}

void
io_pause(struct io *io, int dir)
{
	io_debug("%s(%p, %x)", __func__, io, dir);

	io->flags |= dir & (IO_IN | IO_OUT);
	io_reload(io);
}

void
io_resume(struct io *io, int dir)
{
	io_debug("%s(%p, %x)", __func__, io, dir);

	io->flags &= ~(dir & (IO_IN | IO_OUT));
	io_reload(io);
}

void
io_set_read(struct io *io)
{
	int mode;

	io_debug("%s(%p)", __func__, io);

	mode = io->flags & IO_RW;
	if (!(mode == 0 || mode == IO_WRITE))
		fatalx("%s: full-duplex or reading", __func__);

	io->flags &= ~IO_RW;
	io->flags |= IO_READ;
	io_reload(io);
}

void
io_set_write(struct io *io)
{
	int mode;

	io_debug("%s(%p)", __func__, io);

	mode = io->flags & IO_RW;
	if (!(mode == 0 || mode == IO_READ))
		fatalx("%s: full-duplex or writing", __func__);

	io->flags &= ~IO_RW;
	io->flags |= IO_WRITE;
	io_reload(io);
}

int
io_write(struct io *io, const void *buf, size_t len)
{
	int r;

	r = iobuf_queue(&io->iobuf, buf, len);

	io_reload(io);

	return r;
}

int
io_writev(struct io *io, const struct iovec *iov, int iovcount)
{
	int r;

	r = iobuf_queuev(&io->iobuf, iov, iovcount);

	io_reload(io);

	return r;
}

int
io_print(struct io *io, const char *s)
{
	return io_write(io, s, strlen(s));
}

int
io_printf(struct io *io, const char *fmt, ...)
{
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = io_vprintf(io, fmt, ap);
	va_end(ap);

	return r;
}

int
io_vprintf(struct io *io, const char *fmt, va_list ap)
{

	char *buf;
	int len;

	len = vasprintf(&buf, fmt, ap);
	if (len == -1)
		return -1;

	len = io_write(io, buf, len);
	free(buf);

	return len;
}

size_t
io_queued(struct io *io)
{
	return iobuf_queued(&io->iobuf);
}

void *
io_data(struct io *io)
{
	return iobuf_data(&io->iobuf);
}

size_t
io_datalen(struct io *io)
{
	return iobuf_len(&io->iobuf);
}

char *
io_getline(struct io *io, size_t *sz)
{
	return iobuf_getline(&io->iobuf, sz);
}

void
io_drop(struct io *io, size_t sz)
{
	return iobuf_drop(&io->iobuf, sz);
}

const char*
io_strflags(int flags)
{
	static char buf[64];

	buf[0] = '\0';

	switch (flags & IO_RW) {
	case 0:
		(void)strlcat(buf, "rw", sizeof buf);
		break;
	case IO_READ:
		(void)strlcat(buf, "R", sizeof buf);
		break;
	case IO_WRITE:
		(void)strlcat(buf, "W", sizeof buf);
		break;
	case IO_RW:
		(void)strlcat(buf, "RW", sizeof buf);
		break;
	}

	if (flags & IO_PAUSE_IN)
		(void)strlcat(buf, ",F_PI", sizeof buf);
	if (flags & IO_PAUSE_OUT)
		(void)strlcat(buf, ",F_PO", sizeof buf);

	return buf;
}

const char*
io_strevents(short ev)
{
	static char buf[64];
	char buf2[16];
	int n;

	n = 0;
	buf[0] = '\0';

	if (ev == 0) {
		(void)strlcat(buf, "<NONE>", sizeof(buf));
		return buf;
	}

	if (ev & EV_TIMEOUT) {
		(void)strlcat(buf, "EV_TIMEOUT", sizeof(buf));
		ev &= ~EV_TIMEOUT;
		n++;
	}

	if (ev & EV_READ) {
		if (n)
			(void)strlcat(buf, "|", sizeof(buf));
		(void)strlcat(buf, "EV_READ", sizeof(buf));
		ev &= ~EV_READ;
		n++;
	}

	if (ev & EV_WRITE) {
		if (n)
			(void)strlcat(buf, "|", sizeof(buf));
		(void)strlcat(buf, "EV_WRITE", sizeof(buf));
		ev &= ~EV_WRITE;
		n++;
	}

	if (ev & EV_SIGNAL) {
		if (n)
			(void)strlcat(buf, "|", sizeof(buf));
		(void)strlcat(buf, "EV_SIGNAL", sizeof(buf));
		ev &= ~EV_SIGNAL;
		n++;
	}

	if (ev) {
		if (n)
			(void)strlcat(buf, "|", sizeof(buf));
		(void)strlcat(buf, "EV_?=0x", sizeof(buf));
		(void)snprintf(buf2, sizeof(buf2), "%hx", ev);
		(void)strlcat(buf, buf2, sizeof(buf));
	}

	return buf;
}

/*
 * Setup the necessary events as required by the current io state,
 * honouring duplex mode and i/o pause.
 */
static void
io_reload(struct io *io)
{
	short events;

	/* The io will be reloaded at release time. */
	if (io->flags & IO_HELD)
		return;

	/* Do nothing if no socket. */
	if (io->sock == -1)
		return;

#ifdef IO_SSL
	if (io->tls) {
		io_reload_tls(io);
		return;
	}
#endif

	io_debug("%s(%p)", __func__, io);

	events = 0;
	if (IO_READING(io) && !(io->flags & IO_PAUSE_IN))
		events = EV_READ;
	if (IO_WRITING(io) && !(io->flags & IO_PAUSE_OUT) && io_queued(io))
		events |= EV_WRITE;

	io_reset(io, events, io_dispatch);
}

static void
io_reset(struct io *io, short events, void (*dispatch)(int, short, void*))
{
	struct timeval tv, *ptv;

	io_debug("%s(%p, %s, %p) -> %s", __func__, io,
	    io_strevents(events), dispatch, io_strio(io));

	/*
	 * Indicate that the event has already been reset so that reload
	 * is not called on frame_leave.
	 */
	io->flags |= IO_RESET;

	if (event_initialized(&io->ev))
		event_del(&io->ev);

	/*
	 * The io is paused by the user, so we don't want the timeout to be
	 * effective.
	 */
	if (events == 0)
		return;

	event_set(&io->ev, io->sock, events, dispatch, io);
	if (io->timeout >= 0) {
		tv.tv_sec = io->timeout / 1000;
		tv.tv_usec = (io->timeout % 1000) * 1000;
		ptv = &tv;
	} else
		ptv = NULL;

	event_add(&io->ev, ptv);
}

static void
io_frame_enter(const char *where, struct io *io, int ev)
{
	io_debug("io: BEGIN %llu", frame);
	io_debug("%s(%s, %s, %s)", __func__, where, io_strevents(ev),
	    io_strio(io));

	if (current)
		fatalx("%s: interleaved frames", __func__);

	current = io;

	io_hold(io);
}

static void
io_frame_leave(struct io *io)
{
	io_debug("%s(%llu)", __func__, frame);

	if (current && current != io)
		fatalx("%s: io mismatch", __func__);

	/* The io has been cleared. */
	if (current == NULL)
		goto done;

	/*
	 * TODO: There is a possible optimization there:
	 * In a typical half-duplex request/response scenario,
	 * the io is waiting to read a request, and when done, it queues
	 * the response in the output buffer and goes to write mode.
	 * There, the write event is set and will be triggered in the next
	 * event frame.  In most case, the write call could be done
	 * immediately as part of the last read frame, thus avoiding to go
	 * through the event loop machinery. So, as an optimisation, we
	 * could detect that case here and force an event dispatching.
	 */

	/* Reload the io if it has not been reset already. */
	io_release(io);
	current = NULL;
    done:
	io_debug("io: END %llu", frame);

	frame += 1;
}

static void
io_hold(struct io *io)
{
	io_debug("%s(%p)", __func__, io);

	if (io->flags & IO_HELD)
		fatalx("%s: already held", __func__);

	io->flags &= ~IO_RESET;
	io->flags |= IO_HELD;
}

static void
io_release(struct io *io)
{
	io_debug("%s(%p)", __func__, io);

	if (!(io->flags & IO_HELD))
		fatalx("%s: not held", __func__);

	io->flags &= ~IO_HELD;
	if (!(io->flags & IO_RESET))
		io_reload(io);
}

static void
io_callback(struct io *io, int evt)
{
	io_debug("%s(%s, %s)", __func__, io_strio(io), io_strevent(evt));

	io->cb(io, evt, io->arg);
}

static void
io_dispatch(int fd, short ev, void *arg)
{
	struct io *io = arg;
	size_t w;
	ssize_t n;
	int saved_errno;

	io_frame_enter(__func__, io, ev);

	if (ev == EV_TIMEOUT) {
		io_callback(io, IO_TIMEOUT);
		goto leave;
	}

	if (ev & EV_WRITE && (w = io_queued(io))) {
		if ((n = iobuf_write(&io->iobuf, io->sock)) < 0) {
			if (n == IOBUF_WANT_WRITE) /* kqueue bug? */
				goto read;
			if (n == IOBUF_CLOSED)
				io_callback(io, IO_DISCONNECTED);
			else {
				log_warn("%s: iobuf_write", __func__);
				saved_errno = errno;
				io->error = strerror(errno);
				errno = saved_errno;
				io_callback(io, IO_ERROR);
			}
			goto leave;
		}
		if (w > io->lowat && w - n <= io->lowat)
			io_callback(io, IO_LOWAT);
	}
    read:

	if (ev & EV_READ) {
		iobuf_normalize(&io->iobuf);
		if ((n = iobuf_read(&io->iobuf, io->sock)) < 0) {
			if (n == IOBUF_CLOSED)
				io_callback(io, IO_DISCONNECTED);
			else {
				log_warn("%s: iobuf_read", __func__);
				saved_errno = errno;
				io->error = strerror(errno);
				errno = saved_errno;
				io_callback(io, IO_ERROR);
			}
			goto leave;
		}
		if (n)
			io_callback(io, IO_DATAIN);
	}

leave:
	io_frame_leave(io);
}

static void
io_dispatch_connect(int fd, short ev, void *arg)
{
	struct io *io = arg;
	socklen_t sl;
	int r, e;

	io_frame_enter(__func__, io, ev);

	if (ev == EV_TIMEOUT)
		e = ETIMEDOUT;
	else {
		sl = sizeof(e);
		r = getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &sl);
		if (r == -1)  {
			log_warn("%s: getsockopt", __func__);
			e = errno;
		}
		else if (e) {
			errno = e;
			log_warn("%s: (connect)", __func__);
		}
	}

	if (e == 0) {
		io->state = IO_STATE_UP;
		io_callback(io, IO_CONNECTED);
		goto done;
	}

	while (io->ai) {
		r = io_connect_next(io);
		if (r == 0)
			goto done;
		e = errno;
	}

	(void)close(fd);
	io->sock = -1;
	io->error = strerror(e);
	io->state = IO_STATE_DOWN;
	io_callback(io, e == ETIMEDOUT ? IO_TIMEOUT : IO_ERROR);
    done:
	io_frame_leave(io);
}

static int
io_connect_next(struct io *io)
{
	struct addrinfo *ai;
	struct linger l;
	int saved_errno;

	while ((ai = io->ai)) {
		io->ai = ai->ai_next;
		ai->ai_next = NULL;
		if (ai->ai_socktype == SOCK_STREAM)
			break;
		freeaddrinfo(ai);
	}

	if (ai == NULL) {
		errno = ESOCKTNOSUPPORT;
		log_warn("%s", __func__);
		return -1;
	}

	if ((io->sock = socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK,
	    0)) == -1) {
		log_warn("%s: socket", __func__);
		goto fail;
	}

	memset(&l, 0, sizeof(l));
	if (setsockopt(io->sock, SOL_SOCKET, SO_LINGER, &l, sizeof(l)) == -1) {
		log_warn("%s: setsockopt", __func__);
		goto fail;
	}

	if (io->bind && bind(io->sock, io->bind, io->bind->sa_len) == -1) {
		log_warn("%s: bind", __func__);
		goto fail;
	}

	if (connect(io->sock, ai->ai_addr, ai->ai_addr->sa_len) == -1)
		if (errno != EINPROGRESS) {
			log_warn("%s: connect", __func__);
			goto fail;
		}

	freeaddrinfo(ai);
	io->state = IO_STATE_CONNECT;
	io_reset(io, EV_WRITE, io_dispatch_connect);
	return 0;

    fail:
	if (io->sock != -1) {
		saved_errno = errno;
		close(io->sock);
		errno = saved_errno;
		io->error = strerror(errno);
		io->sock = -1;
	}
	freeaddrinfo(ai);
	if (io->ai) {
		freeaddrinfo(io->ai);
		io->ai = NULL;
	}
	return -1;
}

#ifdef IO_SSL

static const char*
io_ssl_error(void)
{
	static char buf[128];
	unsigned long e;

	e = ERR_peek_last_error();
	if (e) {
		ERR_error_string(e, buf);
		return buf;
	}

	return "No SSL error";
}

static void
io_dispatch_accept_tls(int fd, short event, void *arg)
{
	struct io *io = arg;
	int e, ret;

	io_frame_enter(__func__, io, event);

	if (event == EV_TIMEOUT) {
		io_callback(io, IO_TIMEOUT);
		goto leave;
	}

	if ((ret = SSL_accept(io->tls)) > 0) {
		io->state = IO_STATE_UP;
		io_callback(io, IO_TLSREADY);
		goto leave;
	}

	switch ((e = SSL_get_error(io->tls, ret))) {
	case SSL_ERROR_WANT_READ:
		io_reset(io, EV_READ, io_dispatch_accept_tls);
		break;
	case SSL_ERROR_WANT_WRITE:
		io_reset(io, EV_WRITE, io_dispatch_accept_tls);
		break;
	default:
		io->error = io_ssl_error();
		ssl_error("io_dispatch_accept_tls:SSL_accept");
		io_callback(io, IO_TLSERROR);
		break;
	}

    leave:
	io_frame_leave(io);
}

static void
io_dispatch_connect_tls(int fd, short event, void *arg)
{
	struct io *io = arg;
	int e, ret;

	io_frame_enter(__func__, io, event);

	if (event == EV_TIMEOUT) {
		io_callback(io, IO_TIMEOUT);
		goto leave;
	}

	if ((ret = SSL_connect(io->tls)) > 0) {
		io->state = IO_STATE_UP;
		io_callback(io, IO_TLSREADY);
		goto leave;
	}

	switch ((e = SSL_get_error(io->tls, ret))) {
	case SSL_ERROR_WANT_READ:
		io_reset(io, EV_READ, io_dispatch_connect_tls);
		break;
	case SSL_ERROR_WANT_WRITE:
		io_reset(io, EV_WRITE, io_dispatch_connect_tls);
		break;
	default:
		io->error = io_ssl_error();
		ssl_error("io_dispatch_connect_tls:SSL_connect");
		io_callback(io, IO_TLSERROR);
		break;
	}

    leave:
	io_frame_leave(io);
}

static void
io_dispatch_read_tls(int fd, short event, void *arg)
{
	struct io *io = arg;
	int n, saved_errno;

	io_frame_enter(__func__, io, event);

	if (event == EV_TIMEOUT) {
		io_callback(io, IO_TIMEOUT);
		goto leave;
	}

again:
	iobuf_normalize(&io->iobuf);
	switch ((n = iobuf_read_ssl(&io->iobuf, (SSL*)io->tls))) {
	case IOBUF_WANT_READ:
		io_reset(io, EV_READ, io_dispatch_read_tls);
		break;
	case IOBUF_WANT_WRITE:
		io_reset(io, EV_WRITE, io_dispatch_read_tls);
		break;
	case IOBUF_CLOSED:
		io_callback(io, IO_DISCONNECTED);
		break;
	case IOBUF_ERROR:
		saved_errno = errno;
		io->error = strerror(errno);
		errno = saved_errno;
		log_warn("%s: iobuf_read_ssl", __func__);
		io_callback(io, IO_ERROR);
		break;
	case IOBUF_SSLERROR:
		io->error = io_ssl_error();
		ssl_error("io_dispatch_read_tls:SSL_read");
		io_callback(io, IO_TLSERROR);
		break;
	default:
		io_debug("%s(...) -> r=%d", __func__, n);
		io_callback(io, IO_DATAIN);
		if (current == io && IO_READING(io) && SSL_pending(io->tls))
			goto again;
	}

    leave:
	io_frame_leave(io);
}

static void
io_dispatch_write_tls(int fd, short event, void *arg)
{
	struct io *io = arg;
	size_t w2, w;
	int n, saved_errno;

	io_frame_enter(__func__, io, event);

	if (event == EV_TIMEOUT) {
		io_callback(io, IO_TIMEOUT);
		goto leave;
	}

	w = io_queued(io);
	switch ((n = iobuf_write_ssl(&io->iobuf, (SSL*)io->tls))) {
	case IOBUF_WANT_READ:
		io_reset(io, EV_READ, io_dispatch_write_tls);
		break;
	case IOBUF_WANT_WRITE:
		io_reset(io, EV_WRITE, io_dispatch_write_tls);
		break;
	case IOBUF_CLOSED:
		io_callback(io, IO_DISCONNECTED);
		break;
	case IOBUF_ERROR:
		saved_errno = errno;
		io->error = strerror(errno);
		errno = saved_errno;
		log_warn("%s: iobuf_write_ssl", __func__);
		io_callback(io, IO_ERROR);
		break;
	case IOBUF_SSLERROR:
		io->error = io_ssl_error();
		ssl_error("io_dispatch_write_tls:SSL_write");
		io_callback(io, IO_TLSERROR);
		break;
	default:
		io_debug("%s(...) -> w=%d", __func__, n);
		w2 = io_queued(io);
		if (w > io->lowat && w2 <= io->lowat)
			io_callback(io, IO_LOWAT);
		break;
	}

    leave:
	io_frame_leave(io);
}

static void
io_reload_tls(struct io *io)
{
	short ev = 0;
	void (*dispatch)(int, short, void*) = NULL;

	switch (io->state) {
	case IO_STATE_CONNECT_TLS:
		ev = EV_WRITE;
		dispatch = io_dispatch_connect_tls;
		break;
	case IO_STATE_ACCEPT_TLS:
		ev = EV_READ;
		dispatch = io_dispatch_accept_tls;
		break;
	case IO_STATE_UP:
		ev = 0;
		if (IO_READING(io) && !(io->flags & IO_PAUSE_IN)) {
			ev = EV_READ;
			dispatch = io_dispatch_read_tls;
		}
		else if (IO_WRITING(io) && !(io->flags & IO_PAUSE_OUT) &&
		    io_queued(io)) {
			ev = EV_WRITE;
			dispatch = io_dispatch_write_tls;
		}
		if (!ev)
			return; /* paused */
		break;
	default:
		fatalx("%s: unexpected state %d", __func__, io->state);
	}

	io_reset(io, ev, dispatch);
}

#endif /* IO_SSL */
