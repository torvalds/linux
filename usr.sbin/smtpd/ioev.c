/*	$OpenBSD: ioev.c,v 1.49 2023/02/08 08:20:54 tb Exp $	*/
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

#include <sys/socket.h>

#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef IO_TLS
#include <tls.h>
#endif
#include <unistd.h>

#include "ioev.h"
#include "iobuf.h"
#include "log.h"

enum {
	IO_STATE_NONE,
	IO_STATE_CONNECT,
	IO_STATE_CONNECT_TLS,
	IO_STATE_ACCEPT_TLS,
	IO_STATE_UP,

	IO_STATE_MAX,
};

#define IO_PAUSE_IN 		IO_IN
#define IO_PAUSE_OUT		IO_OUT
#define IO_READ			0x04
#define IO_WRITE		0x08
#define IO_RW			(IO_READ | IO_WRITE)
#define IO_RESET		0x10  /* internal */
#define IO_HELD			0x20  /* internal */

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
	struct tls	*tls;

	const char	*error; /* only valid immediately on callback */
};

const char* io_strflags(int);
const char* io_evstr(short);

void	_io_init(void);
void	io_hold(struct io *);
void	io_release(struct io *);
void	io_callback(struct io*, int);
void	io_dispatch(int, short, void *);
void	io_dispatch_connect(int, short, void *);
size_t	io_pending(struct io *);
size_t	io_queued(struct io*);
void	io_reset(struct io *, short, void (*)(int, short, void*));
void	io_frame_enter(const char *, struct io *, int);
void	io_frame_leave(struct io *);

#ifdef IO_TLS
void	io_dispatch_handshake_tls(int, short, void *);
void	io_dispatch_accept_tls(int, short, void *);
void	io_dispatch_connect_tls(int, short, void *);
void	io_dispatch_read_tls(int, short, void *);
void	io_dispatch_write_tls(int, short, void *);
void	io_reload_tls(struct io *io);
#endif

static struct io	*current = NULL;
static uint64_t		 frame = 0;
static int		_io_debug = 0;

#define io_debug(args...) do { if (_io_debug) printf(args); } while(0)


const char*
io_strio(struct io *io)
{
	static char	buf[128];
	char		ssl[128];

	ssl[0] = '\0';
#ifdef IO_TLS
	if (io->tls) {
		(void)snprintf(ssl, sizeof ssl, " tls=%s:%s",
		    tls_conn_version(io->tls),
		    tls_conn_cipher(io->tls));
	}
#endif

	(void)snprintf(buf, sizeof buf,
	    "<io:%p fd=%d to=%d fl=%s%s ib=%zu ob=%zu>",
	    io, io->sock, io->timeout, io_strflags(io->flags), ssl,
	    io_pending(io), io_queued(io));

	return (buf);
}

#define CASE(x) case x : return #x

const char*
io_strevent(int evt)
{
	static char buf[32];

	switch (evt) {
	CASE(IO_CONNECTED);
	CASE(IO_TLSREADY);
	CASE(IO_DATAIN);
	CASE(IO_LOWAT);
	CASE(IO_DISCONNECTED);
	CASE(IO_TIMEOUT);
	CASE(IO_ERROR);
	default:
		(void)snprintf(buf, sizeof(buf), "IO_? %d", evt);
		return buf;
	}
}

void
io_set_nonblocking(int fd)
{
	int	flags;

	if ((flags = fcntl(fd, F_GETFL)) == -1)
		fatal("io_set_blocking:fcntl(F_GETFL)");

	flags |= O_NONBLOCK;

	if (fcntl(fd, F_SETFL, flags) == -1)
		fatal("io_set_blocking:fcntl(F_SETFL)");
}

void
io_set_nolinger(int fd)
{
	struct linger    l;

	memset(&l, 0, sizeof(l));
	if (setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l)) == -1)
		fatal("io_set_linger:setsockopt");
}

/*
 * Event framing must not rely on an io pointer to refer to the "same" io
 * throughout the frame, because this is not always the case:
 *
 * 1) enter(addr0) -> free(addr0) -> leave(addr0) = SEGV
 * 2) enter(addr0) -> free(addr0) -> malloc == addr0 -> leave(addr0) = BAD!
 *
 * In both case, the problem is that the io is freed in the callback, so
 * the pointer becomes invalid. If that happens, the user is required to
 * call io_clear, so we can adapt the frame state there.
 */
void
io_frame_enter(const char *where, struct io *io, int ev)
{
	io_debug("\n=== %" PRIu64 " ===\n"
	    "io_frame_enter(%s, %s, %s)\n",
	    frame, where, io_evstr(ev), io_strio(io));

	if (current)
		fatalx("io_frame_enter: interleaved frames");

	current = io;

	io_hold(io);
}

void
io_frame_leave(struct io *io)
{
	io_debug("io_frame_leave(%" PRIu64 ")\n", frame);

	if (current && current != io)
		fatalx("io_frame_leave: io mismatch");

	/* io has been cleared */
	if (current == NULL)
		goto done;

	/* TODO: There is a possible optimization there:
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
	io_debug("=== /%" PRIu64 "\n", frame);

	frame += 1;
}

void
_io_init(void)
{
	static int init = 0;

	if (init)
		return;

	init = 1;
	_io_debug = getenv("IO_DEBUG") != NULL;
}

struct io *
io_new(void)
{
	struct io *io;

	_io_init();

	if ((io = calloc(1, sizeof(*io))) == NULL)
		return NULL;

	io->sock = -1;
	io->timeout = -1;

	if (iobuf_init(&io->iobuf, 0, 0) == -1) {
		free(io);
		return NULL;
	}

	return io;
}

void
io_free(struct io *io)
{
	io_debug("io_clear(%p)\n", io);

	/* the current io is virtually dead */
	if (io == current)
		current = NULL;

#ifdef IO_TLS
	tls_free(io->tls);
	io->tls = NULL;
#endif

	if (event_initialized(&io->ev))
		event_del(&io->ev);
	if (io->sock != -1) {
		close(io->sock);
		io->sock = -1;
	}

	iobuf_clear(&io->iobuf);
	free(io);
}

void
io_hold(struct io *io)
{
	io_debug("io_enter(%p)\n", io);

	if (io->flags & IO_HELD)
		fatalx("io_hold: io is already held");

	io->flags &= ~IO_RESET;
	io->flags |= IO_HELD;
}

void
io_release(struct io *io)
{
	if (!(io->flags & IO_HELD))
		fatalx("io_release: io is not held");

	io->flags &= ~IO_HELD;
	if (!(io->flags & IO_RESET))
		io_reload(io);
}

void
io_set_fd(struct io *io, int fd)
{
	io->sock = fd;
	if (fd != -1)
		io_reload(io);
}

void
io_set_callback(struct io *io, void(*cb)(struct io *, int, void *), void *arg)
{
	io->cb = cb;
	io->arg = arg;
}

void
io_set_timeout(struct io *io, int msec)
{
	io_debug("io_set_timeout(%p, %d)\n", io, msec);

	io->timeout = msec;
}

void
io_set_lowat(struct io *io, size_t lowat)
{
	io_debug("io_set_lowat(%p, %zu)\n", io, lowat);

	io->lowat = lowat;
}

void
io_pause(struct io *io, int dir)
{
	io_debug("io_pause(%p, %x)\n", io, dir);

	io->flags |= dir & (IO_PAUSE_IN | IO_PAUSE_OUT);
	io_reload(io);
}

void
io_resume(struct io *io, int dir)
{
	io_debug("io_resume(%p, %x)\n", io, dir);

	io->flags &= ~(dir & (IO_PAUSE_IN | IO_PAUSE_OUT));
	io_reload(io);
}

void
io_set_read(struct io *io)
{
	int	mode;

	io_debug("io_set_read(%p)\n", io);

	mode = io->flags & IO_RW;
	if (!(mode == 0 || mode == IO_WRITE))
		fatalx("io_set_read: full-duplex or reading");

	io->flags &= ~IO_RW;
	io->flags |= IO_READ;
	io_reload(io);
}

void
io_set_write(struct io *io)
{
	int	mode;

	io_debug("io_set_write(%p)\n", io);

	mode = io->flags & IO_RW;
	if (!(mode == 0 || mode == IO_READ))
		fatalx("io_set_write: full-duplex or writing");

	io->flags &= ~IO_RW;
	io->flags |= IO_WRITE;
	io_reload(io);
}

const char *
io_error(struct io *io)
{
	return io->error;
}

struct tls *
io_tls(struct io *io)
{
	return io->tls;
}

int
io_fileno(struct io *io)
{
	return io->sock;
}

int
io_paused(struct io *io, int what)
{
	return (io->flags & (IO_PAUSE_IN | IO_PAUSE_OUT)) == what;
}

/*
 * Buffered output functions
 */

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

/*
 * Buffered input functions
 */

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


#define IO_READING(io) (((io)->flags & IO_RW) != IO_WRITE)
#define IO_WRITING(io) (((io)->flags & IO_RW) != IO_READ)

/*
 * Setup the necessary events as required by the current io state,
 * honouring duplex mode and i/o pauses.
 */
void
io_reload(struct io *io)
{
	short	events;

	/* io will be reloaded at release time */
	if (io->flags & IO_HELD)
		return;

	iobuf_normalize(&io->iobuf);

#ifdef IO_TLS
	if (io->tls) {
		io_reload_tls(io);
		return;
	}
#endif

	io_debug("io_reload(%p)\n", io);

	events = 0;
	if (IO_READING(io) && !(io->flags & IO_PAUSE_IN))
		events = EV_READ;
	if (IO_WRITING(io) && !(io->flags & IO_PAUSE_OUT) && io_queued(io))
		events |= EV_WRITE;

	io_reset(io, events, io_dispatch);
}

/* Set the requested event. */
void
io_reset(struct io *io, short events, void (*dispatch)(int, short, void*))
{
	struct timeval	tv, *ptv;

	io_debug("io_reset(%p, %s, %p) -> %s\n",
	    io, io_evstr(events), dispatch, io_strio(io));

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

size_t
io_pending(struct io *io)
{
	return iobuf_len(&io->iobuf);
}

const char*
io_strflags(int flags)
{
	static char	buf[64];

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
io_evstr(short ev)
{
	static char	buf[64];
	char		buf2[16];
	int		n;

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

void
io_dispatch(int fd, short ev, void *humppa)
{
	struct io	*io = humppa;
	size_t		 w;
	ssize_t		 n;
	int		 saved_errno;

	io_frame_enter("io_dispatch", io, ev);

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

void
io_callback(struct io *io, int evt)
{
	io->cb(io, evt, io->arg);
}

int
io_connect(struct io *io, const struct sockaddr *sa, const struct sockaddr *bsa)
{
	int	sock, errno_save;

	if ((sock = socket(sa->sa_family, SOCK_STREAM, 0)) == -1)
		goto fail;

	io_set_nonblocking(sock);
	io_set_nolinger(sock);

	if (bsa && bind(sock, bsa, bsa->sa_len) == -1)
		goto fail;

	if (connect(sock, sa, sa->sa_len) == -1)
		if (errno != EINPROGRESS)
			goto fail;

	io->sock = sock;
	io_reset(io, EV_WRITE, io_dispatch_connect);

	return (sock);

    fail:
	if (sock != -1) {
		errno_save = errno;
		close(sock);
		errno = errno_save;
		io->error = strerror(errno);
	}
	return (-1);
}

void
io_dispatch_connect(int fd, short ev, void *humppa)
{
	struct io	*io = humppa;
	int		 r, e;
	socklen_t	 sl;

	io_frame_enter("io_dispatch_connect", io, ev);

	if (ev == EV_TIMEOUT) {
		close(fd);
		io->sock = -1;
		io_callback(io, IO_TIMEOUT);
	} else {
		sl = sizeof(e);
		r = getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &sl);
		if (r == -1)  {
			log_warn("io_dispatch_connect: getsockopt");
			e = errno;
		}
		if (e) {
			close(fd);
			io->sock = -1;
			io->error = strerror(e);
			io_callback(io, e == ETIMEDOUT ? IO_TIMEOUT : IO_ERROR);
		}
		else {
			io->state = IO_STATE_UP;
			io_callback(io, IO_CONNECTED);
		}
	}

	io_frame_leave(io);
}

#ifdef IO_TLS
int
io_connect_tls(struct io *io, struct tls *tls, const char *hostname)
{
	int	mode;

	mode = io->flags & IO_RW;
	if (mode != IO_WRITE)
		fatalx("io_connect_tls: expect IO_WRITE mode");

	if (io->tls)
		fatalx("io_connect_tls: TLS already started");

	if (tls_connect_socket(tls, io->sock, hostname) == -1) {
		io->error = tls_error(tls);
		return (-1);
	}

	io->tls = tls;
	io->state = IO_STATE_CONNECT_TLS;
	io_reset(io, EV_READ|EV_WRITE, io_dispatch_handshake_tls);

	return (0);
}

int
io_accept_tls(struct io *io, struct tls *tls)
{
	int	mode;

	mode = io->flags & IO_RW;
	if (mode != IO_READ)
		fatalx("io_accept_tls: expect IO_READ mode");

	if (io->tls)
		fatalx("io_accept_tls: TLS already started");

	if (tls_accept_socket(tls, &io->tls, io->sock) == -1) {
		io->error = tls_error(tls);
		return (-1);
	}

	io->state = IO_STATE_ACCEPT_TLS;
	io_reset(io, EV_READ|EV_WRITE, io_dispatch_handshake_tls);

	return (0);
}

void
io_dispatch_handshake_tls(int fd, short event, void *humppa)
{
	struct io	*io = humppa;
	int		ret;

	io_frame_enter("io_dispatch_handshake_tls", io, event);

	if (event == EV_TIMEOUT) {
		io_callback(io, IO_TIMEOUT);
		goto leave;
	}

	if ((ret = tls_handshake(io->tls)) == 0) {
		io->state = IO_STATE_UP;
		io_callback(io, IO_TLSREADY);
		goto leave;
	}
	if (ret == TLS_WANT_POLLIN)
		io_reset(io, EV_READ, io_dispatch_handshake_tls);
	else if (ret == TLS_WANT_POLLOUT)
		io_reset(io, EV_WRITE, io_dispatch_handshake_tls);
	else {
		io->error = tls_error(io->tls);
		io_callback(io, IO_ERROR);
	}

 leave:
	io_frame_leave(io);
	return;
}

void
io_dispatch_read_tls(int fd, short event, void *humppa)
{
	struct io	*io = humppa;
	int		 n;

	io_frame_enter("io_dispatch_read_tls", io, event);

	if (event == EV_TIMEOUT) {
		io_callback(io, IO_TIMEOUT);
		goto leave;
	}

again:
	iobuf_normalize(&io->iobuf);
	switch ((n = iobuf_read_tls(&io->iobuf, io->tls))) {
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
		io->error = tls_error(io->tls);
		io_callback(io, IO_ERROR);
		break;
	default:
		io_debug("io_dispatch_read_tls(...) -> r=%d\n", n);
		io_callback(io, IO_DATAIN);
		if (current == io && IO_READING(io))
			goto again;
	}

    leave:
	io_frame_leave(io);
}

void
io_dispatch_write_tls(int fd, short event, void *humppa)
{
	struct io	*io = humppa;
	int		 n;
	size_t		 w2, w;

	io_frame_enter("io_dispatch_write_tls", io, event);

	if (event == EV_TIMEOUT) {
		io_callback(io, IO_TIMEOUT);
		goto leave;
	}

	w = io_queued(io);
	switch ((n = iobuf_write_tls(&io->iobuf, io->tls))) {
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
		io->error = tls_error(io->tls);
		io_callback(io, IO_ERROR);
		break;
	default:
		io_debug("io_dispatch_write_tls(...) -> w=%d\n", n);
		w2 = io_queued(io);
		if (w > io->lowat && w2 <= io->lowat)
			io_callback(io, IO_LOWAT);
		break;
	}

    leave:
	io_frame_leave(io);
}

void
io_reload_tls(struct io *io)
{
	if (io->state != IO_STATE_UP)
		fatalx("io_reload_tls: bad state");

	if (IO_READING(io) && !(io->flags & IO_PAUSE_IN)) {
		io_reset(io, EV_READ, io_dispatch_read_tls);
		return;
	}

	if (IO_WRITING(io) && !(io->flags & IO_PAUSE_OUT) && io_queued(io)) {
		io_reset(io, EV_WRITE, io_dispatch_write_tls);
		return;
	}

	/* paused */
}

#endif /* IO_TLS */
