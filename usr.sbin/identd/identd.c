/*	$OpenBSD: identd.c,v 1.40 2019/07/03 03:24:03 deraadt Exp $ */

/*
 * Copyright (c) 2013 David Gwynne <dlg@openbsd.org>
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
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/uio.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>

#include <netdb.h>

#include <err.h>
#include <ctype.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>

#define IDENTD_USER "_identd"

#define DOTNOIDENT ".noident"

#define TIMEOUT_MIN 4
#define TIMEOUT_MAX 240
#define TIMEOUT_DEFAULT 120
#define INPUT_MAX 256

enum ident_client_state {
	S_BEGINNING = 0,
	S_SERVER_PORT,
	S_PRE_COMMA,
	S_POST_COMMA,
	S_CLIENT_PORT,
	S_PRE_EOL,
	S_EOL,

	S_DEAD,
	S_QUEUED
};

#define E_NONE		0
#define E_NOUSER	1
#define E_UNKNOWN	2
#define E_HIDDEN	3

struct ident_client {
	struct {
		/* from the socket */
		struct sockaddr_storage ss;
		socklen_t len;

		/* from the request */
		u_int port;
	} client, server;
	SIMPLEQ_ENTRY(ident_client) entry;
	enum ident_client_state state;
	struct event ev;
	struct event tmo;
	size_t rxbytes;

	char *buf;
	size_t buflen;
	size_t bufoff;
	uid_t uid;
};

struct ident_resolver {
	SIMPLEQ_ENTRY(ident_resolver) entry;
	char *buf;
	size_t buflen;
	u_int error;
};

struct identd_listener {
	struct event ev, pause;
};

void	parent_rd(int, short, void *);
void	parent_wr(int, short, void *);
int	parent_username(struct ident_resolver *, struct passwd *);
int	parent_uid(struct ident_resolver *, struct passwd *);
int	parent_token(struct ident_resolver *, struct passwd *);
void	parent_noident(struct ident_resolver *, struct passwd *);

void	child_rd(int, short, void *);
void	child_wr(int, short, void *);

void	identd_listen(const char *, const char *, int);
void	identd_paused(int, short, void *);
void	identd_accept(int, short, void *);
int	identd_error(struct ident_client *, const char *);
void	identd_close(struct ident_client *);
void	identd_timeout(int, short, void *);
void	identd_request(int, short, void *);
enum ident_client_state
	identd_parse(struct ident_client *, int);
void	identd_resolving(int, short, void *);
void	identd_response(int, short, void *);
int	fetchuid(struct ident_client *);

const char *gethost(struct sockaddr_storage *);
const char *gentoken(void);

struct loggers {
	__dead void (*err)(int, const char *, ...)
	    __attribute__((__format__ (printf, 2, 3)));
	__dead void (*errx)(int, const char *, ...)
	    __attribute__((__format__ (printf, 2, 3)));
	void (*warn)(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
	void (*warnx)(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
	void (*notice)(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
	void (*debug)(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
};

const struct loggers conslogger = {
	err,
	errx,
	warn,
	warnx,
	warnx, /* notice */
	warnx /* debug */
};

__dead void	syslog_err(int, const char *, ...)
		    __attribute__((__format__ (printf, 2, 3)));
__dead void	syslog_errx(int, const char *, ...)
		    __attribute__((__format__ (printf, 2, 3)));
void		syslog_warn(const char *, ...)
		    __attribute__((__format__ (printf, 1, 2)));
void		syslog_warnx(const char *, ...)
		    __attribute__((__format__ (printf, 1, 2)));
void		syslog_notice(const char *, ...)
		    __attribute__((__format__ (printf, 1, 2)));
void		syslog_debug(const char *, ...)
		    __attribute__((__format__ (printf, 1, 2)));
void		syslog_vstrerror(int, int, const char *, va_list)
		    __attribute__((__format__ (printf, 3, 0)));

const struct loggers syslogger = {
	syslog_err,
	syslog_errx,
	syslog_warn,
	syslog_warnx,
	syslog_notice,
	syslog_debug
};

const struct loggers *logger = &conslogger;

#define lerr(_e, _f...) logger->err((_e), _f)
#define lerrx(_e, _f...) logger->errx((_e), _f)
#define lwarn(_f...) logger->warn(_f)
#define lwarnx(_f...) logger->warnx(_f)
#define lnotice(_f...) logger->notice(_f)
#define ldebug(_f...) logger->debug(_f)

#define sa(_ss) ((struct sockaddr *)(_ss))

static __dead void
usage(void)
{
	extern char *__progname;
	fprintf(stderr, "usage: %s [-46deHhNn] [-l address] [-t timeout]\n",
	    __progname);
	exit(1);
}

struct timeval timeout = { TIMEOUT_DEFAULT, 0 };
int debug = 0;
int noident = 0;
int unknown_err = 0;
int hideall = 0;

int (*parent_uprintf)(struct ident_resolver *, struct passwd *) =
    parent_username;

struct event proc_rd, proc_wr;
union {
	struct {
		SIMPLEQ_HEAD(, ident_resolver) replies;
	} parent;
	struct {
		SIMPLEQ_HEAD(, ident_client) pushing, popping;
	} child;
} sc;

int
main(int argc, char *argv[])
{
	extern char *__progname;
	const char *errstr = NULL;

	int		 c;
	struct passwd	*pw;

	char *addr = NULL;
	int family = AF_UNSPEC;

	int pair[2];
	pid_t parent;
	int sibling;

	while ((c = getopt(argc, argv, "46deHhl:Nnt:")) != -1) {
		switch (c) {
		case '4':
			family = AF_INET;
			break;
		case '6':
			family = AF_INET6;
			break;
		case 'd':
			debug = 1;
			break;
		case 'e':
			unknown_err = 1;
			break;
		case 'H':
			hideall = 1;
			/* FALLTHROUGH */
		case 'h':
			parent_uprintf = parent_token;
			break;
		case 'l':
			addr = optarg;
			break;
		case 'N':
			noident = 1;
			break;
		case 'n':
			parent_uprintf = parent_uid;
			break;
		case 't':
			timeout.tv_sec = strtonum(optarg,
			    TIMEOUT_MIN, TIMEOUT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "timeout %s is %s", optarg, errstr);
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	if (geteuid() != 0)
		errx(1, "need root privileges");

	if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK,
	    PF_UNSPEC, pair) == -1)
		err(1, "socketpair");

	pw = getpwnam(IDENTD_USER);
	if (pw == NULL)
		errx(1, "no %s user", IDENTD_USER);

	if (!debug && daemon(1, 0) == -1)
		err(1, "daemon");

	parent = fork();
	switch (parent) {
	case -1:
		lerr(1, "fork");

	case 0:
		/* child */
		setproctitle("listener");
		close(pair[1]);
		sibling = pair[0];
		break;

	default:
		/* parent */
		setproctitle("resolver");
		close(pair[0]);
		sibling = pair[1];
		break;
	}

	if (!debug) {
		openlog(__progname, LOG_PID|LOG_NDELAY, LOG_DAEMON);
		tzset();
		logger = &syslogger;
	}

	event_init();

	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		lerr(1, "signal(SIGPIPE)");

	if (parent) {
		if (pledge("stdio getpw rpath id", NULL) == -1)
			err(1, "pledge");

		SIMPLEQ_INIT(&sc.parent.replies);

		event_set(&proc_rd, sibling, EV_READ | EV_PERSIST,
		    parent_rd, NULL);
		event_set(&proc_wr, sibling, EV_WRITE,
		    parent_wr, NULL);
	} else {
		SIMPLEQ_INIT(&sc.child.pushing);
		SIMPLEQ_INIT(&sc.child.popping);

		identd_listen(addr, "auth", family);

		if (chroot(pw->pw_dir) == -1)
			lerr(1, "chroot(%s)", pw->pw_dir);

		if (chdir("/") == -1)
			lerr(1, "chdir(%s)", pw->pw_dir);

		event_set(&proc_rd, sibling, EV_READ | EV_PERSIST,
		    child_rd, NULL);
		event_set(&proc_wr, sibling, EV_WRITE,
		    child_wr, NULL);
	}

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		lerr(1, "unable to revoke privs");

	if (parent) {
		if (noident) {
			if (pledge("stdio getpw rpath", NULL) == -1)
				err(1, "pledge");
		} else {
			if (pledge("stdio getpw", NULL) == -1)
				err(1, "pledge");
		}
	}

	event_add(&proc_rd, NULL);
	event_dispatch();
	return (0);
}

void
parent_rd(int fd, short events, void *arg)
{
	struct ident_resolver *r;
	struct passwd *pw;
	ssize_t n;
	uid_t uid;

	n = read(fd, &uid, sizeof(uid));
	switch (n) {
	case -1:
		switch (errno) {
		case EAGAIN:
		case EINTR:
			return;
		default:
			lerr(1, "parent read");
		}
		break;
	case 0:
		lerrx(1, "child has gone");
	case sizeof(uid):
		break;
	default:
		lerrx(1, "unexpected %zd data from child", n);
	}

	r = calloc(1, sizeof(*r));
	if (r == NULL)
		lerr(1, "resolver alloc");

	pw = getpwuid(uid);
	if (pw == NULL && !hideall) {
		r->error = E_NOUSER;
		goto done;
	}

	if (noident && !hideall) {
		parent_noident(r, pw);
		if (r->error != E_NONE)
			goto done;
	}

	n = (*parent_uprintf)(r, pw);
	if (n == -1) {
		r->error = E_UNKNOWN;
		goto done;
	}

	r->buflen = n + 1;

done:
	SIMPLEQ_INSERT_TAIL(&sc.parent.replies, r, entry);
	event_add(&proc_wr, NULL);
}

int
parent_username(struct ident_resolver *r, struct passwd *pw)
{
	return (asprintf(&r->buf, "%s", pw->pw_name));
}

int
parent_uid(struct ident_resolver *r, struct passwd *pw)
{
	return (asprintf(&r->buf, "%u", (u_int)pw->pw_uid));
}

int
parent_token(struct ident_resolver *r, struct passwd *pw)
{
	const char *token;
	int rv;

	token = gentoken();
	rv = asprintf(&r->buf, "%s", token);
	if (rv != -1) {
		if (pw)
			lnotice("token %s == uid %u (%s)", token,
			    (u_int)pw->pw_uid, pw->pw_name);
		else
			lnotice("token %s == NO USER", token);
	}

	return (rv);
}

void
parent_noident(struct ident_resolver *r, struct passwd *pw)
{
	char path[PATH_MAX];
	struct stat st;
	int rv;

	rv = snprintf(path, sizeof(path), "%s/%s", pw->pw_dir, DOTNOIDENT);
	if (rv < 0 || rv >= sizeof(path)) {
		r->error = E_UNKNOWN;
		return;
	}

	if (stat(path, &st) == -1)
		return;

	r->error = E_HIDDEN;
}

void
parent_wr(int fd, short events, void *arg)
{
	struct ident_resolver *r = SIMPLEQ_FIRST(&sc.parent.replies);
	struct iovec iov[2];
	int iovcnt = 0;
	ssize_t n;

	iov[iovcnt].iov_base = &r->error;
	iov[iovcnt].iov_len = sizeof(r->error);
	iovcnt++;

	if (r->buflen > 0) {
		iov[iovcnt].iov_base = r->buf;
		iov[iovcnt].iov_len = r->buflen;
		iovcnt++;
	}

	n = writev(fd, iov, iovcnt);
	if (n == -1) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
			event_add(&proc_wr, NULL);
			return;
		default:
			lerr(1, "parent write");
		}
	}

	if (n != sizeof(r->error) + r->buflen)
		lerrx(1, "unexpected parent write length %zd", n);

	SIMPLEQ_REMOVE_HEAD(&sc.parent.replies, entry);

	if (r->buflen > 0)
		free(r->buf);

	free(r);

	if (!SIMPLEQ_EMPTY(&sc.parent.replies))
		event_add(&proc_wr, NULL);
}

void
child_rd(int fd, short events, void *arg)
{
	struct ident_client *c;
	struct {
		u_int error;
		char buf[512];
	} reply;
	ssize_t n;

	n = read(fd, &reply, sizeof(reply));
	switch (n) {
	case -1:
		switch (errno) {
		case EAGAIN:
		case EINTR:
			return;
		default:
			lerr(1, "child read");
		}
		break;
	case 0:
		lerrx(1, "parent has gone");
	default:
		break;
	}

	c = SIMPLEQ_FIRST(&sc.child.popping);
	if (c == NULL)
		lerrx(1, "unsolicited data from parent");

	SIMPLEQ_REMOVE_HEAD(&sc.child.popping, entry);

	if (n < sizeof(reply.error))
		lerrx(1, "short data from parent");

	/* check if something went wrong while the parent was working */
	if (c->state == S_DEAD) {
		free(c);
		return;
	}
	c->state = S_DEAD;

	switch (reply.error) {
	case E_NONE:
		n = asprintf(&c->buf, "%u , %u : USERID : UNIX : %s\r\n",
		    c->server.port, c->client.port, reply.buf);
		break;
	case E_NOUSER:
		n = asprintf(&c->buf, "%u , %u : ERROR : %s\r\n",
		    c->server.port, c->client.port,
		    unknown_err ? "UNKNOWN-ERROR" : "NO-USER");
		break;
	case E_UNKNOWN:
		n = asprintf(&c->buf, "%u , %u : ERROR : UNKNOWN-ERROR\r\n",
		    c->server.port, c->client.port);
		break;
	case E_HIDDEN:
		n = asprintf(&c->buf, "%u , %u : ERROR : HIDDEN-USER\r\n",
		    c->server.port, c->client.port);
		break;
	default:
		lerrx(1, "unexpected error from parent %u", reply.error);
	}
	if (n == -1)
		goto fail;

	c->buflen = n;

	fd = EVENT_FD(&c->ev);
	event_del(&c->ev);
	event_set(&c->ev, fd, EV_READ | EV_WRITE | EV_PERSIST,
	    identd_response, c);
	event_add(&c->ev, NULL);
	return;

fail:
	identd_close(c);
}

void
child_wr(int fd, short events, void *arg)
{
	struct ident_client *c = SIMPLEQ_FIRST(&sc.child.pushing);
	const char *errstr = NULL;
	ssize_t n;

	n = write(fd, &c->uid, sizeof(c->uid));
	switch (n) {
	case -1:
		switch (errno) {
		case EINTR:
		case EAGAIN:
			event_add(&proc_wr, NULL);
			return;
		case ENOBUFS: /* parent has a backlog of requests */
			errstr = "UNKNOWN-ERROR";
			break;
		default:
			lerr(1, "child write");
		}
		break;
	case sizeof(c->uid):
		break;
	default:
		lerrx(1, "unexpected child write length %zd", n);
	}

	SIMPLEQ_REMOVE_HEAD(&sc.child.pushing, entry);
	if (errstr == NULL)
		SIMPLEQ_INSERT_TAIL(&sc.child.popping, c, entry);
	else if (identd_error(c, errstr) == -1)
		identd_close(c);

	if (!SIMPLEQ_EMPTY(&sc.child.pushing))
		event_add(&proc_wr, NULL);
}

void
identd_listen(const char *addr, const char *port, int family)
{
	struct identd_listener *l = NULL;

	struct addrinfo hints, *res, *res0;
	int error, s;
	const char *cause = NULL;
	int on = 1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	error = getaddrinfo(addr, port, &hints, &res0);
	if (error)
		lerrx(1, "%s/%s: %s", addr, port, gai_strerror(error));

	for (res = res0; res != NULL; res = res->ai_next) {
		s = socket(res->ai_family, res->ai_socktype | SOCK_NONBLOCK,
		    res->ai_protocol);
		if (s == -1) {
			cause = "socket";
			continue;
		}

		if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
		    &on, sizeof(on)) == -1)
			err(1, "listener setsockopt(SO_REUSEADDR)");

		if (bind(s, res->ai_addr, res->ai_addrlen) == -1) {
			int serrno = errno;

			cause = "bind";
			close(s);
			errno = serrno;
			continue;
		}

		if (listen(s, 5) == -1)
			err(1, "listen");

		l = calloc(1, sizeof(*l));
		if (l == NULL)
			err(1, "listener ev alloc");

		event_set(&l->ev, s, EV_READ | EV_PERSIST, identd_accept, l);
		event_add(&l->ev, NULL);
		evtimer_set(&l->pause, identd_paused, l);
	}
	if (l == NULL)
		err(1, "%s", cause);

	freeaddrinfo(res0);
}

void
identd_paused(int fd, short events, void *arg)
{
	struct identd_listener *l = arg;
	event_add(&l->ev, NULL);
}

void
identd_accept(int fd, short events, void *arg)
{
	struct identd_listener *l = arg;
	struct sockaddr_storage ss;
	struct timeval pause = { 1, 0 };
	struct ident_client *c = NULL;
	socklen_t len;
	int s;

	len = sizeof(ss);
	s = accept4(fd, sa(&ss), &len, SOCK_NONBLOCK);
	if (s == -1) {
		switch (errno) {
		case EINTR:
		case EWOULDBLOCK:
		case ECONNABORTED:
			return;
		case EMFILE:
		case ENFILE:
			event_del(&l->ev);
			evtimer_add(&l->pause, &pause);
			return;
		default:
			lerr(1, "accept");
		}
	}

	c = calloc(1, sizeof(*c));
	if (c == NULL) {
		lwarn("client alloc");
		close(fd);
		return;
	}

	memcpy(&c->client.ss, &ss, len);
	c->client.len = len;
	ldebug("client: %s", gethost(&ss));

	/* lookup the local ip it connected to */
	c->server.len = sizeof(c->server.ss);
	if (getsockname(s, sa(&c->server.ss), &c->server.len) == -1)
		lerr(1, "getsockname");

	event_set(&c->ev, s, EV_READ | EV_PERSIST, identd_request, c);
	event_add(&c->ev, NULL);

	evtimer_set(&c->tmo, identd_timeout, c);
	evtimer_add(&c->tmo, &timeout);
}

void
identd_timeout(int fd, short events, void *arg)
{
	struct ident_client *c = arg;

	event_del(&c->ev);
	close(fd);
	free(c->buf);

	if (c->state == S_QUEUED) /* it is queued for resolving */
		c->state = S_DEAD;
	else
		free(c);
}

void
identd_request(int fd, short events, void *arg)
{
	struct ident_client *c = arg;
	unsigned char buf[64];
	ssize_t n, i;
	char *errstr = unknown_err ? "UNKNOWN-ERROR" : "INVALID-PORT";

	n = read(fd, buf, sizeof(buf));
	switch (n) {
	case -1:
		switch (errno) {
		case EINTR:
		case EAGAIN:
			return;
		default:
			lwarn("%s read", gethost(&c->client.ss));
			goto fail;
		}
		break;

	case 0:
		ldebug("%s closed connection", gethost(&c->client.ss));
		goto fail;
	default:
		break;
	}

	c->rxbytes += n;
	if (c->rxbytes >= INPUT_MAX)
		goto fail;

	for (i = 0; c->state < S_EOL && i < n; i++)
		c->state = identd_parse(c, buf[i]);

	if (c->state == S_DEAD)
		goto error;
	if (c->state != S_EOL)
		return;

	if (c->server.port < 1 || c->client.port < 1)
		goto error;

	if (fetchuid(c) == -1) {
		errstr = unknown_err ? "UNKNOWN-ERROR" : "NO-USER";
		goto error;
	}

	SIMPLEQ_INSERT_TAIL(&sc.child.pushing, c, entry);
	c->state = S_QUEUED;

	event_del(&c->ev);
	event_set(&c->ev, fd, EV_READ | EV_PERSIST, identd_resolving, c);
	event_add(&c->ev, NULL);

	event_add(&proc_wr, NULL);
	return;

error:
	if (identd_error(c, errstr) == -1)
		goto fail;

	return;

fail:
	identd_close(c);
}

int
identd_error(struct ident_client *c, const char *errstr)
{
	int fd = EVENT_FD(&c->ev);
	ssize_t n;

	n = asprintf(&c->buf, "%u , %u : ERROR : %s\r\n",
	    c->server.port, c->client.port, errstr);
	if (n == -1)
		return (-1);

	c->buflen = n;

	event_del(&c->ev);
	event_set(&c->ev, fd, EV_READ | EV_WRITE | EV_PERSIST,
	    identd_response, c);
	event_add(&c->ev, NULL);

	return (0);
}

void
identd_close(struct ident_client *c)
{
	int fd = EVENT_FD(&c->ev);

	evtimer_del(&c->tmo);
	event_del(&c->ev);
	close(fd);
	free(c->buf);
	free(c);
}

void
identd_resolving(int fd, short events, void *arg)
{
	struct ident_client *c = arg;
	char buf[64];
	ssize_t n;

	/*
	 * something happened while we're waiting for the parent to lookup
	 * the user.
	 */

	n = read(fd, buf, sizeof(buf));
	switch (n) {
	case -1:
		switch (errno) {
		case EINTR:
		case EAGAIN:
			return;
		default:
			lwarn("resolving read");
			break;
		}
		break;
	case 0:
		ldebug("%s closed connection during resolving",
		    gethost(&c->client.ss));
		break;
	default:
		c->rxbytes += n;
		if (c->rxbytes >= INPUT_MAX)
			break;

		/* ignore extra input */
		return;
	}

	evtimer_del(&c->tmo);
	event_del(&c->ev);
	close(fd);
	c->state = S_DEAD; /* on the resolving queue */
}

enum ident_client_state
identd_parse(struct ident_client *c, int ch)
{
	enum ident_client_state s = c->state;

	switch (s) {
	case S_BEGINNING:
		/* ignore leading space */
		if (ch == '\t' || ch == ' ')
			return (s);

		if (ch == '0' || !isdigit(ch))
			return (S_DEAD);

		c->server.port = ch - '0';
		return (S_SERVER_PORT);

	case S_SERVER_PORT:
		if (ch == '\t' || ch == ' ')
			return (S_PRE_COMMA);
		if (ch == ',')
			return (S_POST_COMMA);

		if (!isdigit(ch))
			return (S_DEAD);

		c->server.port *= 10;
		c->server.port += ch - '0';
		if (c->server.port > 65535)
			return (S_DEAD);

		return (s);

	case S_PRE_COMMA:
		if (ch == '\t' || ch == ' ')
			return (s);
		if (ch == ',')
			return (S_POST_COMMA);

		return (S_DEAD);

	case S_POST_COMMA:
		if (ch == '\t' || ch == ' ')
			return (s);

		if (ch == '0' || !isdigit(ch))
			return (S_DEAD);

		c->client.port = ch - '0';
		return (S_CLIENT_PORT);

	case S_CLIENT_PORT:
		if (ch == '\t' || ch == ' ')
			return (S_PRE_EOL);
		if (ch == '\r' || ch == '\n')
			return (S_EOL);

		if (!isdigit(ch))
			return (S_DEAD);

		c->client.port *= 10;
		c->client.port += ch - '0';
		if (c->client.port > 65535)
			return (S_DEAD);

		return (s);

	case S_PRE_EOL:
		if (ch == '\t' || ch == ' ')
			return (s);
		if (ch == '\r' || ch == '\n')
			return (S_EOL);

		return (S_DEAD);

	case S_EOL:
		/* ignore trailing garbage */
		return (s);

	default:
		return (S_DEAD);
	}
}

void
identd_response(int fd, short events, void *arg)
{
	struct ident_client *c = arg;
	char buf[64];
	ssize_t n;

	if (events & EV_READ) {
		n = read(fd, buf, sizeof(buf));
		switch (n) {
		case -1:
			switch (errno) {
			case EINTR:
			case EAGAIN:
				/* meh, try a write */
				break;
			default:
				lwarn("response read");
				goto done;
			}
			break;
		case 0:
			ldebug("%s closed connection during response",
			    gethost(&c->client.ss));
			goto done;
		default:
			c->rxbytes += n;
			if (c->rxbytes >= INPUT_MAX)
				goto done;

			/* ignore extra input */
			break;
		}
	}

	if (!(events & EV_WRITE))
		return; /* try again later */

	n = write(fd, c->buf + c->bufoff, c->buflen - c->bufoff);
	if (n == -1) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
			return; /* try again later */
		case EPIPE:
			goto done;
		default:
			lwarn("response write");
			goto done;
		}
	}

	c->bufoff += n;
	if (c->bufoff != c->buflen)
		return; /* try again later */

done:
	identd_close(c);
}

void
syslog_vstrerror(int e, int priority, const char *fmt, va_list ap)
{
	char *s;

	if (vasprintf(&s, fmt, ap) == -1) {
		syslog(LOG_EMERG, "unable to alloc in syslog_vstrerror");
		exit(1);
	}
	syslog(priority, "%s: %s", s, strerror(e));
	free(s);
}

void
syslog_err(int ecode, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	syslog_vstrerror(errno, LOG_CRIT, fmt, ap);
	va_end(ap);
	exit(ecode);
}

void
syslog_errx(int ecode, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(LOG_CRIT, fmt, ap);
	va_end(ap);
	exit(ecode);
}

void
syslog_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	syslog_vstrerror(errno, LOG_ERR, fmt, ap);
	va_end(ap);
}

void
syslog_warnx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(LOG_ERR, fmt, ap);
	va_end(ap);
}

void
syslog_notice(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(LOG_NOTICE, fmt, ap);
	va_end(ap);
}

void
syslog_debug(const char *fmt, ...)
{
	va_list ap;

	if (!debug)
		return;

	va_start(ap, fmt);
	vsyslog(LOG_DEBUG, fmt, ap);
	va_end(ap);
}

const char *
gethost(struct sockaddr_storage *ss)
{
	struct sockaddr *sa = (struct sockaddr *)ss;
	static char buf[NI_MAXHOST];

	if (getnameinfo(sa, sa->sa_len, buf, sizeof(buf),
	    NULL, 0, NI_NUMERICHOST) != 0)
		return ("(unknown)");

	return (buf);
}

const char *
gentoken(void)
{
	static char buf[21];
	u_int32_t r;
	int i;

	buf[0] = 'a' + arc4random_uniform(26);
	for (i = 1; i < sizeof(buf) - 1; i++) {
		r = arc4random_uniform(36);
		buf[i] = (r < 26 ? 'a' : '0' - 26) + r;
	}
	buf[i] = '\0';

	return (buf);
}

int
fetchuid(struct ident_client *c)
{
	struct tcp_ident_mapping tir;
	int mib[] = { CTL_NET, PF_INET, IPPROTO_TCP, TCPCTL_IDENT };
	struct sockaddr_in *s4;
	struct sockaddr_in6 *s6;
	int err = 0;
	size_t len;

	memset(&tir, 0, sizeof(tir));
	memcpy(&tir.faddr, &c->client.ss, sizeof(tir.faddr));
	memcpy(&tir.laddr, &c->server.ss, sizeof(tir.laddr));

	switch (c->server.ss.ss_family) {
	case AF_INET:
		s4 = (struct sockaddr_in *)&tir.faddr;
		s4->sin_port = htons(c->client.port);

		s4 = (struct sockaddr_in *)&tir.laddr;
		s4->sin_port = htons(c->server.port);
		break;
	case AF_INET6:
		s6 = (struct sockaddr_in6 *)&tir.faddr;
		s6->sin6_port = htons(c->client.port);

		s6 = (struct sockaddr_in6 *)&tir.laddr;
		s6->sin6_port = htons(c->server.port);
		break;
	default:
		lerrx(1, "unexpected family %d", c->server.ss.ss_family);
	}

	len = sizeof(tir);
	err = sysctl(mib, sizeof(mib) / sizeof(mib[0]), &tir, &len, NULL, 0);
	if (err == -1)
		lerr(1, "sysctl");

	if (tir.ruid == -1)
		return (-1);

	c->uid = tir.ruid;
	return (0);
}
