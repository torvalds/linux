/* $OpenBSD: tftp-proxy.c,v 1.22 2021/01/17 13:38:52 claudio Exp $
 *
 * Copyright (c) 2005 DLS Internet Services
 * Copyright (c) 2004, 2005 Camiel Dobbelaar, <cd@sentia.nl>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/tftp.h>
#include <net/if.h>
#include <net/pfvar.h>
#include <netdb.h>

#include <unistd.h>
#include <errno.h>
#include <err.h>
#include <pwd.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <event.h>

#include "filter.h"

#define CHROOT_DIR	"/var/empty"
#define NOPRIV_USER	"_tftp_proxy"

#define DEFTRANSWAIT	2
#define NTOP_BUFS	4
#define PKTSIZE		SEGSIZE+4

const char *opcode(int);
const char *sock_ntop(struct sockaddr *);
static void usage(void);

struct proxy_listener {
	struct event ev;
	TAILQ_ENTRY(proxy_listener) entry;
	int (*cmsg2dst)(struct cmsghdr *, struct sockaddr_storage *);
	int s;
};

void	proxy_listen(const char *, const char *, int);
void	proxy_listener_events(void);
int	proxy_dst4(struct cmsghdr *, struct sockaddr_storage *);
int	proxy_dst6(struct cmsghdr *, struct sockaddr_storage *);
void	proxy_recv(int, short, void *);

struct fd_reply {
	TAILQ_ENTRY(fd_reply) entry;
	int fd;
};

struct privproc {
	struct event pop_ev;
	struct event push_ev;
	TAILQ_HEAD(, fd_reply) replies;
	struct evbuffer *buf;
};

void	proxy_privproc(int, struct passwd *);
void	privproc_push(int, short, void *);
void	privproc_pop(int, short, void *);

void	unprivproc_push(int, short, void *);
void	unprivproc_pop(int, short, void *);
void	unprivproc_timeout(int, short, void *);

char	ntop_buf[NTOP_BUFS][INET6_ADDRSTRLEN];

struct loggers {
	__dead void (*err)(int, const char *, ...)
	    __attribute__((__format__ (printf, 2, 3)));
	__dead void (*errx)(int, const char *, ...)
	    __attribute__((__format__ (printf, 2, 3)));
	void (*warn)(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
	void (*warnx)(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
	void (*info)(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
	void (*debug)(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
};

const struct loggers conslogger = {
	err,
	errx,
	warn,
	warnx,
	warnx, /* info */
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
void		syslog_info(const char *, ...)
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
	syslog_info,
	syslog_debug
};

const struct loggers *logger = &conslogger;

#define lerr(_e, _f...) logger->err((_e), _f)
#define lerrx(_e, _f...) logger->errx((_e), _f)
#define lwarn(_f...) logger->warn(_f)
#define lwarnx(_f...) logger->warnx(_f)
#define linfo(_f...) logger->info(_f)
#define ldebug(_f...) logger->debug(_f)

__dead void
usage(void)
{
	extern char *__progname;
	fprintf(stderr, "usage: %s [-46dv] [-a address] [-l address] [-p port]"
	    " [-w transwait]\n", __progname);
	exit(1);
}

int	debug = 0;
int	verbose = 0;
struct timeval transwait = { DEFTRANSWAIT, 0 };

int on = 1;

struct addr_pair {
	struct sockaddr_storage src;
	struct sockaddr_storage dst;
};

struct proxy_request {
	char buf[SEGSIZE_MAX + 4];
	size_t buflen;

	struct addr_pair addrs;

	struct event ev;
	TAILQ_ENTRY(proxy_request) entry;
	u_int32_t id;
};

struct proxy_child {
	TAILQ_HEAD(, proxy_request) fdrequests;
	TAILQ_HEAD(, proxy_request) tmrequests;
	struct event push_ev;
	struct event pop_ev;
	struct evbuffer *buf;
};

struct proxy_child *child = NULL;
TAILQ_HEAD(, proxy_listener) proxy_listeners;

struct src_addr {
	TAILQ_ENTRY(src_addr)	entry;
	struct sockaddr_storage	addr;
	socklen_t		addrlen;
};
TAILQ_HEAD(, src_addr) src_addrs;

void	source_addresses(const char*, int);

int
main(int argc, char *argv[])
{
	extern char *__progname;

	int c;
	const char *errstr;

	struct src_addr *saddr, *saddr2;
	struct passwd *pw;

	char *addr = "localhost";
	char *port = "6969";
	int family = AF_UNSPEC;

	int pair[2];

	TAILQ_INIT(&src_addrs);

	while ((c = getopt(argc, argv, "46a:dvl:p:w:")) != -1) {
		switch (c) {
		case '4':
			family = AF_INET;
			break;
		case '6':
			family = AF_INET6;
			break;
		case 'a':
			source_addresses(optarg, family);
			break;
		case 'd':
			verbose = debug = 1;
			break;
		case 'l':
			addr = optarg;
			break;
		case 'p':
			port = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'w':
			transwait.tv_sec = strtonum(optarg, 1, 30, &errstr);
			if (errstr)
				errx(1, "wait is %s", errstr);
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	if (geteuid() != 0)
		lerrx(1, "need root privileges");

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, PF_UNSPEC, pair)
	    == -1)
		lerr(1, "socketpair");

	pw = getpwnam(NOPRIV_USER);
	if (pw == NULL)
		lerrx(1, "no %s user", NOPRIV_USER);

	/* Family option may have been specified late. */
	if (family != AF_UNSPEC)
		TAILQ_FOREACH_SAFE(saddr, &src_addrs, entry, saddr2)
			if (saddr->addr.ss_family != family) {
				TAILQ_REMOVE(&src_addrs, saddr, entry);
				free(saddr);
			}

	if (!debug) {
		if (daemon(1, 0) == -1)
			lerr(1, "daemon");

		openlog(__progname, LOG_PID|LOG_NDELAY, LOG_DAEMON);
		tzset();
		logger = &syslogger;
	}

	switch (fork()) {
	case -1:
		lerr(1, "fork");

	case 0:
		setproctitle("privproc");
		close(pair[1]);
		proxy_privproc(pair[0], pw);
		/* this never returns */

	default:
		setproctitle("unprivproc");
		close(pair[0]);
		break;
	}

	child = calloc(1, sizeof(*child));
	if (child == NULL)
		lerr(1, "alloc(child)");

	child->buf = evbuffer_new();
	if (child->buf == NULL)
		lerr(1, "child evbuffer");

	TAILQ_INIT(&child->fdrequests);
	TAILQ_INIT(&child->tmrequests);

	proxy_listen(addr, port, family);

	/* open /dev/pf */
	init_filter(NULL, verbose);

	/* revoke privs */
	if (chroot(CHROOT_DIR) == -1)
		lerr(1, "chroot %s", CHROOT_DIR);

	if (chdir("/") == -1)
		lerr(1, "chdir %s", CHROOT_DIR);

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		err(1, "unable to revoke privs");

	event_init();

	proxy_listener_events();

	event_set(&child->pop_ev, pair[1], EV_READ | EV_PERSIST,
	    unprivproc_pop, NULL);
	event_set(&child->push_ev, pair[1], EV_WRITE,
	    unprivproc_push, NULL);

	event_add(&child->pop_ev, NULL);

	event_dispatch();

	return(0);
}

void
source_addresses(const char* name, int family)
{
	struct addrinfo hints, *res, *res0;
	struct src_addr *saddr;
	int error;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;
	error = getaddrinfo(name, NULL, &hints, &res0);
	if (error)
		lerrx(1, "%s: %s", name, gai_strerror(error));
	for (res = res0; res != NULL; res = res->ai_next) {
		if ((saddr = calloc(1, sizeof(struct src_addr))) == NULL)
			lerrx(1, "calloc");
		memcpy(&(saddr->addr), res->ai_addr, res->ai_addrlen);
		saddr->addrlen = res->ai_addrlen;
		TAILQ_INSERT_TAIL(&src_addrs, saddr, entry);
	}
	freeaddrinfo(res0);
}

void
proxy_privproc(int s, struct passwd *pw)
{
	struct privproc p;

	if (chroot(CHROOT_DIR) == -1)
		lerr(1, "chroot to %s", CHROOT_DIR);

	if (chdir("/") == -1)
		lerr(1, "chdir to %s", CHROOT_DIR);

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid))
		lerr(1, "unable to set group ids");

	if (pledge("stdio inet sendfd", NULL) == -1)
		err(1, "pledge");

	TAILQ_INIT(&p.replies);

	p.buf = evbuffer_new();
	if (p.buf == NULL)
		err(1, "pop evbuffer_new");

	event_init();

	event_set(&p.pop_ev, s, EV_READ | EV_PERSIST, privproc_pop, &p);
	event_set(&p.push_ev, s, EV_WRITE, privproc_push, &p);

	event_add(&p.pop_ev, NULL);

	event_dispatch();
}

void
privproc_pop(int fd, short events, void *arg)
{
	struct addr_pair req;
	struct privproc *p = arg;
	struct fd_reply *rep;
	struct src_addr *saddr;
	int add = 0;

	switch (evbuffer_read(p->buf, fd, sizeof(req))) {
	case 0:
		lerrx(1, "unprivproc has gone");
	case -1:
		switch (errno) {
		case EAGAIN:
		case EINTR:
			return;
		default:
			lerr(1, "privproc_pop read");
		}
	default:
		break;
	}

	while (EVBUFFER_LENGTH(p->buf) >= sizeof(req)) {
		evbuffer_remove(p->buf, &req, sizeof(req));

		/* do i really need to check this? */
		if (req.src.ss_family != req.dst.ss_family)
			lerrx(1, "family mismatch");

		rep = calloc(1, sizeof(*rep));
		if (rep == NULL)
			lerr(1, "reply calloc");

		rep->fd = socket(req.src.ss_family, SOCK_DGRAM | SOCK_NONBLOCK,
		    IPPROTO_UDP);
		if (rep->fd == -1)
			lerr(1, "privproc socket");

		if (setsockopt(rep->fd, SOL_SOCKET, SO_BINDANY,
		    &on, sizeof(on)) == -1)
			lerr(1, "privproc setsockopt(BINDANY)");

		if (setsockopt(rep->fd, SOL_SOCKET, SO_REUSEADDR,
		    &on, sizeof(on)) == -1)
			lerr(1, "privproc setsockopt(REUSEADDR)");

		if (setsockopt(rep->fd, SOL_SOCKET, SO_REUSEPORT,
		    &on, sizeof(on)) == -1)
			lerr(1, "privproc setsockopt(REUSEPORT)");

		TAILQ_FOREACH(saddr, &src_addrs, entry)
			if (saddr->addr.ss_family == req.src.ss_family)
				break;
		if (saddr == NULL) {
			if (bind(rep->fd, (struct sockaddr *)&req.src,
			    req.src.ss_len) == -1)
				lerr(1, "privproc bind");
		} else {
			if (bind(rep->fd, (struct sockaddr*)&saddr->addr,
			    saddr->addrlen) == -1)
				lerr(1, "privproc bind");
		}

		if (TAILQ_EMPTY(&p->replies))
			add = 1;

		TAILQ_INSERT_TAIL(&p->replies, rep, entry);
	}

	if (add)
		event_add(&p->push_ev, NULL);
}

void
privproc_push(int fd, short events, void *arg)
{
	struct privproc *p = arg;
	struct fd_reply *rep;

	struct msghdr msg;
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;
	struct cmsghdr *cmsg;
	struct iovec iov;
	int result = 0;

	while ((rep = TAILQ_FIRST(&p->replies)) != NULL) {
		memset(&msg, 0, sizeof(msg));

		msg.msg_control = (caddr_t)&cmsgbuf.buf;
		msg.msg_controllen = sizeof(cmsgbuf.buf);
		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_len = CMSG_LEN(sizeof(int));
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		*(int *)CMSG_DATA(cmsg) = rep->fd;

		iov.iov_base = &result;
		iov.iov_len = sizeof(int);
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;

		switch (sendmsg(fd, &msg, 0)) {
		case sizeof(int):
			break;

		case -1:
			if (errno == EAGAIN)
				goto again;

			lerr(1, "privproc sendmsg");
			/* NOTREACHED */

		default:
			lerrx(1, "privproc sendmsg weird len");
		}

		TAILQ_REMOVE(&p->replies, rep, entry);
		close(rep->fd);
		free(rep);
	}

	if (TAILQ_EMPTY(&p->replies))
		return;

again:
	event_add(&p->push_ev, NULL);
}

void
proxy_listen(const char *addr, const char *port, int family)
{
	struct proxy_listener *l;

	struct addrinfo hints, *res, *res0;
	int error;
	int s, on = 1;
	int serrno;
	const char *cause = NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	TAILQ_INIT(&proxy_listeners);

	error = getaddrinfo(addr, port, &hints, &res0);
	if (error)
		errx(1, "%s:%s: %s", addr, port, gai_strerror(error));

	for (res = res0; res != NULL; res = res->ai_next) {
		s = socket(res->ai_family, res->ai_socktype | SOCK_NONBLOCK,
		    res->ai_protocol);
		if (s == -1) {
			cause = "socket";
			continue;
		}

		if (bind(s, res->ai_addr, res->ai_addrlen) == -1) {
			cause = "bind";
			serrno = errno;
			close(s);
			errno = serrno;
			continue;
		}

		l = calloc(1, sizeof(*l));
		if (l == NULL)
			err(1, "listener alloc");

		switch (res->ai_family) {
		case AF_INET:
			l->cmsg2dst = proxy_dst4;

			if (setsockopt(s, IPPROTO_IP, IP_RECVDSTADDR,
			    &on, sizeof(on)) == -1)
				errx(1, "setsockopt(IP_RECVDSTADDR)");
			if (setsockopt(s, IPPROTO_IP, IP_RECVDSTPORT,
			    &on, sizeof(on)) == -1)
				errx(1, "setsockopt(IP_RECVDSTPORT)");
			break;
		case AF_INET6:
			l->cmsg2dst = proxy_dst6;

			if (setsockopt(s, IPPROTO_IPV6, IPV6_RECVPKTINFO,
			    &on, sizeof(on)) == -1)
				errx(1, "setsockopt(IPV6_RECVPKTINFO)");
			if (setsockopt(s, IPPROTO_IPV6, IPV6_RECVDSTPORT,
			    &on, sizeof(on)) == -1)
				errx(1, "setsockopt(IPV6_RECVDSTPORT)");
			break;
		}
		l->s = s;

		TAILQ_INSERT_TAIL(&proxy_listeners, l, entry);
	}
	freeaddrinfo(res0);

	if (TAILQ_EMPTY(&proxy_listeners))
		err(1, "%s", cause);
}

void
proxy_listener_events(void)
{
	struct proxy_listener *l;

	TAILQ_FOREACH(l, &proxy_listeners, entry) {
		event_set(&l->ev, l->s, EV_READ | EV_PERSIST, proxy_recv, l);
		event_add(&l->ev, NULL);
	}
}

char safety[SEGSIZE_MAX + 4];

int
proxy_dst4(struct cmsghdr *cmsg, struct sockaddr_storage *ss)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)ss;

	if (cmsg->cmsg_level != IPPROTO_IP)
		return (0);

	switch (cmsg->cmsg_type) {
	case IP_RECVDSTADDR:
		memcpy(&sin->sin_addr, CMSG_DATA(cmsg), sizeof(sin->sin_addr));
		if (sin->sin_addr.s_addr == INADDR_BROADCAST)
			return (-1);
		break;

	case IP_RECVDSTPORT:
		memcpy(&sin->sin_port, CMSG_DATA(cmsg), sizeof(sin->sin_port));
		break;
	}

	return (0);
}

int
proxy_dst6(struct cmsghdr *cmsg, struct sockaddr_storage *ss)
{
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ss;
	struct in6_pktinfo *ipi = (struct in6_pktinfo *)CMSG_DATA(cmsg);

	if (cmsg->cmsg_level != IPPROTO_IPV6)
		return (0);

	switch (cmsg->cmsg_type) {
	case IPV6_PKTINFO:
		memcpy(&sin6->sin6_addr, &ipi->ipi6_addr,
		    sizeof(sin6->sin6_addr));
		if (IN6_IS_ADDR_LINKLOCAL(&ipi->ipi6_addr))
		    sin6->sin6_scope_id = ipi->ipi6_ifindex;
		break;
	case IPV6_RECVDSTPORT:
		memcpy(&sin6->sin6_port, CMSG_DATA(cmsg),
		    sizeof(sin6->sin6_port));
		break;
	}

	return (0);
}

void
proxy_recv(int fd, short events, void *arg)
{
	struct proxy_listener *l = arg;

	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(struct sockaddr_storage)) +
		    CMSG_SPACE(sizeof(in_port_t))];
	} cmsgbuf;
	struct cmsghdr *cmsg;
	struct msghdr msg;
	struct iovec iov;
	ssize_t n;

	struct proxy_request *r;
	struct tftphdr *tp;

	r = calloc(1, sizeof(*r));
	if (r == NULL) {
		recv(fd, safety, sizeof(safety), 0);
		return;
	}
	r->id = arc4random(); /* XXX unique? */

	bzero(&msg, sizeof(msg));
	iov.iov_base = r->buf;
	iov.iov_len = sizeof(r->buf);
	msg.msg_name = &r->addrs.src;
	msg.msg_namelen = sizeof(r->addrs.src);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	n = recvmsg(fd, &msg, 0);
	if (n == -1) {
		switch (errno) {
		case EAGAIN:
		case EINTR:
			goto err;
		default:
			lerr(1, "recvmsg");
			/* NOTREACHED */
		}
	}
	r->buflen = n;

	/* check the packet */
	if (n < 5) {
		/* not enough to be a real packet */
		goto err;
	}
	tp = (struct tftphdr *)r->buf;
	switch (ntohs(tp->th_opcode)) {
	case RRQ:
	case WRQ:
		break;
	default:
		goto err;
	}

	r->addrs.dst.ss_family = r->addrs.src.ss_family;
	r->addrs.dst.ss_len = r->addrs.src.ss_len;

	/* get local address if possible */
	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
	    cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (l->cmsg2dst(cmsg, &r->addrs.dst) == -1)
			goto err;
	}

	if (verbose) {
		linfo("%s:%d -> %s:%d \"%s %s\"",
		    sock_ntop((struct sockaddr *)&r->addrs.src),
		    ntohs(((struct sockaddr_in *)&r->addrs.src)->sin_port),
		    sock_ntop((struct sockaddr *)&r->addrs.dst),
		    ntohs(((struct sockaddr_in *)&r->addrs.dst)->sin_port),
		    opcode(ntohs(tp->th_opcode)), tp->th_stuff);
		/* XXX tp->th_stuff could be garbage */
	}

	TAILQ_INSERT_TAIL(&child->fdrequests, r, entry);
	evbuffer_add(child->buf, &r->addrs, sizeof(r->addrs));
	event_add(&child->push_ev, NULL);

	return;

err:
	free(r);
}

void
unprivproc_push(int fd, short events, void *arg)
{
	if (evbuffer_write(child->buf, fd) == -1)
		lerr(1, "child evbuffer_write");

	if (EVBUFFER_LENGTH(child->buf))
		event_add(&child->push_ev, NULL);
}

void
unprivproc_pop(int fd, short events, void *arg)
{
	struct proxy_request *r;

	struct msghdr msg;
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;
	struct cmsghdr *cmsg;
	struct iovec iov;
	struct src_addr *src_addr;
	struct sockaddr_storage saddr;
	socklen_t len;
	int result;
	int s;

	len = sizeof(saddr);

	do {
		memset(&msg, 0, sizeof(msg));
		iov.iov_base = &result;
		iov.iov_len = sizeof(int);
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = &cmsgbuf.buf;
		msg.msg_controllen = sizeof(cmsgbuf.buf);

		switch (recvmsg(fd, &msg, 0)) {
		case sizeof(int):
			break;

		case -1:
			switch (errno) {
			case EAGAIN:
			case EINTR:
				return;
			default:
				lerr(1, "child recvmsg");
			}
			/* NOTREACHED */

		case 0:
			lerrx(1, "privproc closed connection");

		default:
			lerrx(1, "child recvmsg was weird");
			/* NOTREACHED */
		}

		if (result != 0) {
			errno = result;
			lerr(1, "child fdpass fail");
		}

		cmsg = CMSG_FIRSTHDR(&msg);
		if (cmsg == NULL)
			lerrx(1, "%s: no message header", __func__);

		if (cmsg->cmsg_type != SCM_RIGHTS) {
			lerrx(1, "%s: expected type %d got %d", __func__,
			    SCM_RIGHTS, cmsg->cmsg_type);
		}

		s = (*(int *)CMSG_DATA(cmsg));

		r = TAILQ_FIRST(&child->fdrequests);
		if (r == NULL)
			lerrx(1, "got fd without a pending request");

		TAILQ_REMOVE(&child->fdrequests, r, entry);

		/* get ready to add rules */
		if (prepare_commit(r->id) == -1)
			lerr(1, "%s: prepare_commit", __func__);

		TAILQ_FOREACH(src_addr, &src_addrs, entry)
			if (src_addr->addr.ss_family == r->addrs.dst.ss_family)
				break;
		if (src_addr == NULL) {
			if (add_filter(r->id, PF_IN, (struct sockaddr *)
			    &r->addrs.dst, (struct sockaddr *)&r->addrs.src,
			    ntohs(((struct sockaddr_in *)&r->addrs.src)
			    ->sin_port), IPPROTO_UDP) == -1)
				lerr(1, "%s: couldn't add pass in", __func__);
		} else {
			if (getsockname(s, (struct sockaddr*)&saddr, &len) == -1)
				lerr(1, "%s: getsockname", __func__);
			if (add_rdr(r->id, (struct sockaddr *)&r->addrs.dst,
			    (struct sockaddr*)&saddr,
			    ntohs(((struct sockaddr_in *)&saddr)->sin_port),
			    (struct sockaddr *)&r->addrs.src,
			    ntohs(((struct sockaddr_in *)&r->addrs.src)->
			    sin_port), IPPROTO_UDP ) == -1)
				lerr(1, "%s: couldn't add rdr rule", __func__);
		}

		if (add_filter(r->id, PF_OUT, (struct sockaddr *)&r->addrs.dst,
		    (struct sockaddr *)&r->addrs.src,
		    ntohs(((struct sockaddr_in *)&r->addrs.src)->sin_port),
		    IPPROTO_UDP) == -1)
			lerr(1, "%s: couldn't add pass out", __func__);

		if (do_commit() == -1)
			lerr(1, "%s: couldn't commit rules", __func__);

		/* forward the initial tftp request and start the insanity */
		if (sendto(s, r->buf, r->buflen, 0,
		    (struct sockaddr *)&r->addrs.dst,
		    r->addrs.dst.ss_len) == -1)
			lerr(1, "%s: unable to send", __func__);

		close(s);

		evtimer_set(&r->ev, unprivproc_timeout, r);
		evtimer_add(&r->ev, &transwait);

		TAILQ_INSERT_TAIL(&child->tmrequests, r, entry);
	} while (!TAILQ_EMPTY(&child->fdrequests));
}

void
unprivproc_timeout(int fd, short events, void *arg)
{
	struct proxy_request *r = arg;

	TAILQ_REMOVE(&child->tmrequests, r, entry);

	/* delete our rdr rule and clean up */
	prepare_commit(r->id);
	do_commit();

	free(r);
}


const char *
opcode(int code)
{
	static char str[6];

	switch (code) {
	case 1:
		(void)snprintf(str, sizeof(str), "RRQ");
		break;
	case 2:
		(void)snprintf(str, sizeof(str), "WRQ");
		break;
	default:
		(void)snprintf(str, sizeof(str), "(%d)", code);
		break;
	}

	return (str);
}

const char *
sock_ntop(struct sockaddr *sa)
{
	static int n = 0;

	/* Cycle to next buffer. */
	n = (n + 1) % NTOP_BUFS;
	ntop_buf[n][0] = '\0';

	if (sa->sa_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)sa;

		return (inet_ntop(AF_INET, &sin->sin_addr, ntop_buf[n],
		    sizeof ntop_buf[0]));
	}

	if (sa->sa_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;

		return (inet_ntop(AF_INET6, &sin6->sin6_addr, ntop_buf[n],
		    sizeof ntop_buf[0]));
	}

	return (NULL);
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
syslog_info(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(LOG_INFO, fmt, ap);
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
