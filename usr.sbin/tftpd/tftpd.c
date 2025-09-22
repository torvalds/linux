/*	$OpenBSD: tftpd.c,v 1.52 2024/09/20 02:00:46 jsg Exp $	*/

/*
 * Copyright (c) 2012 David Gwynne <dlg@uq.edu.au>
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
 * Copyright (c) 1983 Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Trivial file transfer protocol server.
 *
 * This version is based on src/libexec/tftpd which includes many
 * modifications by Jim Guyton <guyton@rand-unix>.
 *
 * It was restructured to be a persistent event driven daemon
 * supporting concurrent connections by dlg for use at the University
 * of Queensland in the Faculty of Engineering Architecture and
 * Information Technology.
 */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/un.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/tftp.h>
#include <netdb.h>

#include <err.h>
#include <ctype.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <paths.h>
#include <poll.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <syslog.h>
#include <unistd.h>
#include <limits.h>
#include <vis.h>

#define TIMEOUT		5		/* packet rexmt timeout */
#define TIMEOUT_MIN	1		/* minimal packet rexmt timeout */
#define TIMEOUT_MAX	255		/* maximal packet rexmt timeout */

#define RETRIES		5

#define SEEDPATH	"/etc/random.seed"

struct formats;

enum opt_enum {
	OPT_TSIZE = 0,
	OPT_TIMEOUT,
	OPT_BLKSIZE,
	NOPT
};

static char *opt_names[] = {
	"tsize",
	"timeout",
	"blksize"
};

struct opt_client {
	char *o_request;
	long long o_reply;
};


struct tftp_server {
	struct event ev;
	TAILQ_ENTRY(tftp_server) entry;
	int s;
};

TAILQ_HEAD(, tftp_server) tftp_servers;

struct tftp_client {
	char buf[SEGSIZE_MAX + 4];
	struct event sev;
	struct sockaddr_storage ss;

	struct timeval tv;

	TAILQ_ENTRY(tftp_client) entry;

	struct opt_client *options;

	size_t segment_size;
	size_t packet_size;
	size_t buflen;

	FILE *file;
	int (*fgetc)(struct tftp_client *);
	int (*fputc)(struct tftp_client *, int);

	u_int retries;
	u_int16_t block;

	int opcode;
	int newline;

	int sock;
};

__dead void	usage(void);
const char	*getip(void *);
int		rdaemon(int);

void		rewrite_connect(const char *);
void		rewrite_events(void);
void		rewrite_map(struct tftp_client *, const char *);
void		rewrite_req(int, short, void *);
void		rewrite_res(int, short, void *);

int		tftpd_listen(const char *, const char *, int);
void		tftpd_events(void);
void		tftpd_recv(int, short, void *);
int		retry(struct tftp_client *);
int		tftp_flush(struct tftp_client *);

void		tftp(struct tftp_client *, struct tftphdr *, size_t);
void		tftp_open(struct tftp_client *, const char *);
void		nak(struct tftp_client *, int);
int		oack(struct tftp_client *);
void		oack_done(int, short, void *);

void		sendfile(struct tftp_client *);
void		recvfile(struct tftp_client *);
int		fget_octet(struct tftp_client *);
int		fput_octet(struct tftp_client *, int);
int		fget_netascii(struct tftp_client *);
int		fput_netascii(struct tftp_client *, int);
void		file_read(struct tftp_client *);
void		tftp_send(struct tftp_client *);
int		tftp_wrq_ack_packet(struct tftp_client *);
void		tftp_rrq_ack(int, short, void *);
void		tftp_wrq_ack(struct tftp_client *client);
void		tftp_wrq(int, short, void *);
void		tftp_wrq_end(int, short, void *);

int		parse_options(struct tftp_client *, char *, size_t,
		    struct opt_client *);
int		validate_access(struct tftp_client *, const char *);

struct tftp_client *
		client_alloc(void);
void		client_free(struct tftp_client *client);

struct formats {
	const char	*f_mode;
	int (*f_getc)(struct tftp_client *);
	int (*f_putc)(struct tftp_client *, int);
} formats[] = {
	{ "octet",	fget_octet,	fput_octet },
	{ "netascii",	fget_netascii,	fput_netascii },
	{ NULL,		NULL }
};

struct errmsg {
	int		 e_code;
	const char	*e_msg;
} errmsgs[] = {
	{ EUNDEF,	"Undefined error code" },
	{ ENOTFOUND,	"File not found" },
	{ EACCESS,	"Access violation" },
	{ ENOSPACE,	"Disk full or allocation exceeded" },
	{ EBADOP,	"Illegal TFTP operation" },
	{ EBADID,	"Unknown transfer ID" },
	{ EEXISTS,	"File already exists" },
	{ ENOUSER,	"No such user" },
	{ EOPTNEG,	"Option negotiation failed" },
	{ -1,		NULL }
};

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
	fprintf(stderr, "usage: %s [-46cdivw] [-l address] [-p port] [-r socket]"
	    " directory\n", __progname);
	exit(1);
}

int		  cancreate = 0;
int		  canwrite = 0;
int		  verbose = 0;
int		  debug = 0;
int		  iflag = 0;

int
main(int argc, char *argv[])
{
	extern char *__progname;

	int		 c;
	struct passwd	*pw;

	char *dir = NULL;
	char *rewrite = NULL;

	char *addr = NULL;
	char *port = "tftp";
	int family = AF_UNSPEC;
	int devnull = -1;

	while ((c = getopt(argc, argv, "46cdil:p:r:vw")) != -1) {
		switch (c) {
		case '4':
			family = AF_INET;
			break;
		case '6':
			family = AF_INET6;
			break;
		case 'c':
			canwrite = cancreate = 1;
			break;
		case 'd':
			verbose = debug = 1;
			break;
		case 'i':
			if (rewrite != NULL)
				errx(1, "options -i and -r are incompatible");
			iflag = 1;
			break;
		case 'l':
			addr = optarg;
			break;
		case 'p':
			port = optarg;
			break;
		case 'r':
			if (iflag == 1)
				errx(1, "options -i and -r are incompatible");
			rewrite = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'w':
			canwrite = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	dir = argv[0];

	if (geteuid() != 0)
		errx(1, "need root privileges");

	pw = getpwnam("_tftpd");
	if (pw == NULL)
		errx(1, "no _tftpd user");

	if (!debug) {
		openlog(__progname, LOG_PID|LOG_NDELAY, LOG_DAEMON);
		tzset();
		logger = &syslogger;
		devnull = open(_PATH_DEVNULL, O_RDWR);
		if (devnull == -1)
			err(1, "open %s", _PATH_DEVNULL);
	}

	if (rewrite != NULL)
		rewrite_connect(rewrite);

	tftpd_listen(addr, port, family);

	if (chroot(dir))
		err(1, "chroot %s", dir);
	if (chdir("/"))
		err(1, "chdir %s", dir);

	/* drop privs */
	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		errx(1, "can't drop privileges");

	if (!debug && rdaemon(devnull) == -1)
		err(1, "unable to daemonize");

	if (cancreate) {
		if (pledge("stdio rpath wpath cpath fattr dns inet", NULL) == -1)
			lerr(1, "pledge");
	} else if (canwrite) {
		if (pledge("stdio rpath wpath dns inet", NULL) == -1)
			lerr(1, "pledge");
	} else {
		if (pledge("stdio rpath dns inet", NULL) == -1)
			lerr(1, "pledge");
	}

	event_init();

	if (rewrite != NULL)
		rewrite_events();

	tftpd_events();

	event_dispatch();

	exit(0);
}

struct rewritemap {
	struct event wrev;
	struct event rdev;
	struct evbuffer *wrbuf;
	struct evbuffer *rdbuf;

	TAILQ_HEAD(, tftp_client) clients;

	int s;
};

struct rewritemap *rwmap = NULL;

void
rewrite_connect(const char *path)
{
	int s;
	struct sockaddr_un remote;
	size_t len;

	rwmap = malloc(sizeof(*rwmap));
	if (rwmap == NULL)
		err(1, "rewrite event malloc");

	rwmap->wrbuf = evbuffer_new();
	if (rwmap->wrbuf == NULL)
		err(1, "rewrite wrbuf");

	rwmap->rdbuf = evbuffer_new();
	if (rwmap->rdbuf == NULL)
		err(1, "rewrite rdbuf");

	TAILQ_INIT(&rwmap->clients);

	s = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (s == -1)
		err(1, "rewrite socket");

	remote.sun_family = AF_UNIX;
	len = strlcpy(remote.sun_path, path, sizeof(remote.sun_path));
	if (len >= sizeof(remote.sun_path))
		errx(1, "rewrite socket path is too long");

	len += sizeof(remote.sun_family) + 1;
	if (connect(s, (struct sockaddr *)&remote, len) == -1)
		err(1, "%s", path);

	rwmap->s = s;
}

void
rewrite_events(void)
{
	event_set(&rwmap->wrev, rwmap->s, EV_WRITE, rewrite_req, NULL);
	event_set(&rwmap->rdev, rwmap->s, EV_READ | EV_PERSIST, rewrite_res, NULL);
	event_add(&rwmap->rdev, NULL);
}

void
rewrite_map(struct tftp_client *client, const char *filename)
{
	char *nicebuf;

	if (stravis(&nicebuf, filename, VIS_SAFE|VIS_OCTAL) == -1)
		lerr(1, "rwmap stravis");

	if (evbuffer_add_printf(rwmap->wrbuf, "%s %s %s\n", getip(&client->ss),
	    client->opcode == WRQ ? "write" : "read", nicebuf) == -1)
		lerr(1, "rwmap printf");

	free(nicebuf);

	TAILQ_INSERT_TAIL(&rwmap->clients, client, entry);

	event_add(&rwmap->wrev, NULL);
}

void
rewrite_req(int fd, short events, void *arg)
{
	if (evbuffer_write(rwmap->wrbuf, fd) == -1) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
			event_add(&rwmap->wrev, NULL);
			return;
		}

		lerr(1, "rewrite socket write");
	}

	if (EVBUFFER_LENGTH(rwmap->wrbuf))
		event_add(&rwmap->wrev, NULL);
}

void
rewrite_res(int fd, short events, void *arg)
{
	struct tftp_client *client;
	char *filename;
	size_t len;

	switch (evbuffer_read(rwmap->rdbuf, fd, PATH_MAX)) {
	case -1:
		switch (errno) {
		case EINTR:
		case EAGAIN:
			return;
		}
		lerr(1, "rewrite socket read");
	case 0:
		lerrx(1, "rewrite socket closed");
	default:
		break;
	}

	while ((filename = evbuffer_readln(rwmap->rdbuf, &len,
	    EVBUFFER_EOL_LF)) != NULL) {
		client = TAILQ_FIRST(&rwmap->clients);
		if (client == NULL)
			lerrx(1, "unexpected rwmap reply");

		TAILQ_REMOVE(&rwmap->clients, client, entry);

		tftp_open(client, filename);

		free(filename);
	}
}

int
tftpd_listen(const char *addr, const char *port, int family)
{
	struct tftp_server *server;

	struct addrinfo hints, *res, *res0;
	int error;
	int s;

	int cerrno = EADDRNOTAVAIL;
	const char *cause = "getaddrinfo";

	int on = 1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	TAILQ_INIT(&tftp_servers);

	error = getaddrinfo(addr, port, &hints, &res0);
	if (error) {
		errx(1, "%s:%s: %s", addr ? addr : "*", port,
		    gai_strerror(error));
	}

	for (res = res0; res != NULL; res = res->ai_next) {
		s = socket(res->ai_family, res->ai_socktype | SOCK_NONBLOCK,
		    res->ai_protocol);
		if (s == -1) {
			cause = "socket";
			cerrno = errno;
			continue;
		}

		if (bind(s, res->ai_addr, res->ai_addrlen) == -1) {
			cause = "bind";
			cerrno = errno;
			close(s);
			continue;
		}

		switch (res->ai_family) {
		case AF_INET:
			if (setsockopt(s, IPPROTO_IP, IP_RECVDSTADDR,
			    &on, sizeof(on)) == -1)
				err(1, "setsockopt(IP_RECVDSTADDR)");
			break;
		case AF_INET6:
			if (setsockopt(s, IPPROTO_IPV6, IPV6_RECVPKTINFO,
			    &on, sizeof(on)) == -1)
				err(1, "setsockopt(IPV6_RECVPKTINFO)");
			break;
		}

		server = malloc(sizeof(*server));
		if (server == NULL)
			err(1, "malloc");

		server->s = s;
		TAILQ_INSERT_TAIL(&tftp_servers, server, entry);
	}

	if (TAILQ_EMPTY(&tftp_servers))
		errc(1, cerrno, "%s", cause);

	freeaddrinfo(res0);
	return (0);
}

void
tftpd_events(void)
{
	struct tftp_server *server;
	TAILQ_FOREACH(server, &tftp_servers, entry) {
		event_set(&server->ev, server->s, EV_READ | EV_PERSIST,
		    tftpd_recv, server);
		event_add(&server->ev, NULL);
	}
}

struct tftp_client *
client_alloc(void)
{
	struct tftp_client *client;

	client = calloc(1, sizeof(*client));
	if (client == NULL)
		return (NULL);

	client->segment_size = SEGSIZE;
	client->packet_size = SEGSIZE + 4;

	client->tv.tv_sec = TIMEOUT;
	client->tv.tv_usec = 0;

	client->sock = -1;
	client->file = NULL;
	client->newline = 0;

	return (client);
}

void
client_free(struct tftp_client *client)
{
	free(client->options);

	if (client->file != NULL)
		fclose(client->file);

	close(client->sock);

	free(client);
}

void
tftpd_recv(int fd, short events, void *arg)
{
	union {
		struct cmsghdr hdr;
		char	buf[CMSG_SPACE(sizeof(struct sockaddr_storage))];
	} cmsgbuf;
	struct cmsghdr *cmsg;
	struct msghdr msg;
	struct iovec iov;

	ssize_t n;
	struct sockaddr_storage s_in;
	int dobind = 1;
	int on = 1;

	struct tftphdr *tp;

	struct tftp_client *client;

	client = client_alloc();
	if (client == NULL) {
		char buf[SEGSIZE_MAX + 4];
		/* no memory! flush this request... */
		recv(fd, buf, SEGSIZE_MAX + 4, 0);
		/* dont care if it fails */
		return;
	}

	bzero(&msg, sizeof(msg));
	iov.iov_base = client->buf;
	iov.iov_len = client->packet_size;
	msg.msg_name = &client->ss;
	msg.msg_namelen = sizeof(client->ss);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	n = recvmsg(fd, &msg, 0);
	if (n == -1) {
		lwarn("recvmsg");
		goto err;
	}
	if (n < 4)
		goto err;

	client->sock = socket(client->ss.ss_family,
	    SOCK_DGRAM | SOCK_NONBLOCK, 0);
	if (client->sock == -1) {
		lwarn("socket");
		goto err;
	}
	memset(&s_in, 0, sizeof(s_in));
	s_in.ss_family = client->ss.ss_family;
	s_in.ss_len = client->ss.ss_len;

	/* get local address if possible */
	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
	    cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level == IPPROTO_IP &&
		    cmsg->cmsg_type == IP_RECVDSTADDR) {
			memcpy(&((struct sockaddr_in *)&s_in)->sin_addr,
			    CMSG_DATA(cmsg), sizeof(struct in_addr));
			if (((struct sockaddr_in *)&s_in)->sin_addr.s_addr ==
			    INADDR_BROADCAST)
				dobind = 0;
			break;
		}
		if (cmsg->cmsg_level == IPPROTO_IPV6 &&
		    cmsg->cmsg_type == IPV6_PKTINFO) {
			struct in6_pktinfo *ipi;

			ipi = (struct in6_pktinfo *)CMSG_DATA(cmsg);
			memcpy(&((struct sockaddr_in6 *)&s_in)->sin6_addr,
			    &ipi->ipi6_addr, sizeof(struct in6_addr));
			if (IN6_IS_ADDR_LINKLOCAL(&ipi->ipi6_addr))
				((struct sockaddr_in6 *)&s_in)->sin6_scope_id =
				    ipi->ipi6_ifindex;
			break;
		}
	}

	if (dobind) {
		setsockopt(client->sock, SOL_SOCKET, SO_REUSEADDR,
		    &on, sizeof(on));
		setsockopt(client->sock, SOL_SOCKET, SO_REUSEPORT,
		    &on, sizeof(on));

		if (bind(client->sock, (struct sockaddr *)&s_in,
		    s_in.ss_len) == -1) {
			lwarn("bind to %s", getip(&s_in));
			goto err;
		}
	}
	if (connect(client->sock, (struct sockaddr *)&client->ss,
	    client->ss.ss_len) == -1) {
		lwarn("connect to %s", getip(&client->ss));
		goto err;
	}

	tp = (struct tftphdr *)client->buf;
	client->opcode = ntohs(tp->th_opcode);
	if (client->opcode != RRQ && client->opcode != WRQ) {
		/* bad request */
		goto err;
	}

	tftp(client, tp, n);

	return;

err:
	client_free(client);
}

int
parse_options(struct tftp_client *client, char *cp, size_t size,
    struct opt_client *options)
{
	char *option;
	char *ccp;
	int has_options = 0;
	int i;

	while (++cp < client->buf + size) {
		for (i = 2, ccp = cp; i > 0; ccp++) {
			if (ccp >= client->buf + size) {
				/*
				 * Don't reject the request, just stop trying
				 * to parse the option and get on with it.
				 * Some Apple OpenFirmware versions have
				 * trailing garbage on the end of otherwise
				 * valid requests.
				 */
				return (has_options);
			} else if (*ccp == '\0')
				i--;
		}

		for (option = cp; *cp; cp++)
			*cp = tolower((unsigned char)*cp);

		for (i = 0; i < NOPT; i++) {
			if (strcmp(option, opt_names[i]) == 0) {
				options[i].o_request = ++cp;
				has_options = 1;
			}
		}
		cp = ccp - 1;
	}

	return (has_options);
}

/*
 * Handle initial connection protocol.
 */
void
tftp(struct tftp_client *client, struct tftphdr *tp, size_t size)
{
	struct opt_client *options;

	char		*cp;
	int		 i, first = 1, ecode, to;
	struct formats	*pf;
	char		*mode = NULL;
	char		 filename[PATH_MAX];
	const char	*errstr;

	if (size < 5) {
		ecode = EBADOP;
		goto error;
	}

	cp = tp->th_stuff;
again:
	while (cp < client->buf + size) {
		if (*cp == '\0')
			break;
		cp++;
	}
	if (*cp != '\0') {
		ecode = EBADOP;
		goto error;
	}
	i = cp - tp->th_stuff;
	if (i >= sizeof(filename)) {
		ecode = EBADOP;
		goto error;
	}
	memcpy(filename, tp->th_stuff, i);
	filename[i] = '\0';
	if (first) {
		mode = ++cp;
		first = 0;
		goto again;
	}
	for (cp = mode; *cp; cp++)
		*cp = tolower((unsigned char)*cp);

	for (pf = formats; pf->f_mode; pf++) {
		if (strcmp(pf->f_mode, mode) == 0)
			break;
	}
	if (pf->f_mode == 0) {
		ecode = EBADOP;
		goto error;
	}
	client->fgetc = pf->f_getc;
	client->fputc = pf->f_putc;

	client->options = options = calloc(NOPT, sizeof(*client->options));
	if (options == NULL) {
		ecode = 100 + ENOMEM;
		goto error;
	}

	if (parse_options(client, cp, size, options)) {
		if (options[OPT_TIMEOUT].o_request != NULL) {
			to = strtonum(options[OPT_TIMEOUT].o_request,
			    TIMEOUT_MIN, TIMEOUT_MAX, &errstr);
			if (errstr) {
				ecode = EBADOP;
				goto error;
			}
			options[OPT_TIMEOUT].o_reply = client->tv.tv_sec = to;
		}

		if (options[OPT_BLKSIZE].o_request) {
			client->segment_size = strtonum(
			    options[OPT_BLKSIZE].o_request,
			    SEGSIZE_MIN, SEGSIZE_MAX, &errstr);
			if (errstr) {
				ecode = EBADOP;
				goto error;
			}
			client->packet_size = client->segment_size + 4;
			options[OPT_BLKSIZE].o_reply = client->segment_size;
		}
	} else {
		free(options);
		client->options = NULL;
	}

	if (verbose) {
		char nicebuf[PATH_MAX];

		(void)strnvis(nicebuf, filename, PATH_MAX,
		    VIS_SAFE|VIS_OCTAL);

		linfo("%s: %s request for '%s'", getip(&client->ss),
		    client->opcode == WRQ ? "write" : "read", nicebuf);
	}

	if (rwmap != NULL)
		rewrite_map(client, filename);
	else
		tftp_open(client, filename);

	return;

error:
	nak(client, ecode);
}

void
tftp_open(struct tftp_client *client, const char *filename)
{
	int ecode;

	ecode = validate_access(client, filename);
	if (ecode)
		goto error;

	if (client->options) {
		if (oack(client) == -1)
			goto error;

		free(client->options);
		client->options = NULL;
	} else if (client->opcode == WRQ) {
		recvfile(client);
	} else
		sendfile(client);

	return;
error:
	nak(client, ecode);
}

/*
 * Validate file access.  Since we
 * have no uid or gid, for now require
 * file to exist and be publicly
 * readable/writable.
 * If we were invoked with arguments
 * from inetd then the file must also be
 * in one of the given directory prefixes.
 * Note also, full path name must be
 * given as we have no login directory.
 */
int
validate_access(struct tftp_client *client, const char *requested)
{
	int		 mode = client->opcode;
	struct opt_client *options = client->options;
	struct stat	 stbuf;
	int		 fd, wmode;
	const char	*errstr, *filename;
	char		 rewritten[PATH_MAX];

	if (!canwrite && mode != RRQ)
		return (EACCESS);

	if (strcmp(requested, SEEDPATH) == 0) {
		char *buf;
		if (mode != RRQ)
			return (EACCESS);

		buf = client->buf + sizeof(client->buf) - 512;
		arc4random_buf(buf, 512);
		if (options != NULL && options[OPT_TSIZE].o_request)
			options[OPT_TSIZE].o_reply = 512;
		client->file = fmemopen(buf, 512, "r");
		if (client->file == NULL)
			return (errno + 100);

		return (0);
	}

	if (iflag) {
		int ret;

		/*
		 * In -i mode, look in the directory named after the
		 * client address.
		 */
		ret = snprintf(rewritten, sizeof(rewritten), "%s/%s",
		    getip(&client->ss), requested);
		if (ret < 0 || ret >= sizeof(rewritten))
			return (ENAMETOOLONG + 100);
		filename = rewritten;
	} else {
retryread:
		filename = requested;
	}

	/*
	 * We use a different permissions scheme if `cancreate' is
	 * set.
	 */
	wmode = O_TRUNC;
	if (stat(filename, &stbuf) == -1) {
		if (!cancreate) {
			/*
			 * In -i mode, retry failed read requests from
			 * the root directory.
			 */
			if (mode == RRQ && errno == ENOENT &&
			    filename == rewritten)
				goto retryread;
			return (errno == ENOENT ? ENOTFOUND : EACCESS);
		} else {
			if ((errno == ENOENT) && (mode != RRQ))
				wmode |= O_CREAT;
			else
				return (EACCESS);
		}
	} else {
		if (mode == RRQ) {
			if ((stbuf.st_mode & (S_IRUSR >> 6)) == 0)
				return (EACCESS);
		} else {
			if ((stbuf.st_mode & (S_IWUSR >> 6)) == 0)
				return (EACCESS);
		}
	}

	if (options != NULL && options[OPT_TSIZE].o_request) {
		if (mode == RRQ)
			options[OPT_TSIZE].o_reply = stbuf.st_size;
		else {
			/* allows writes of 65535 blocks * SEGSIZE_MAX bytes */
			options[OPT_TSIZE].o_reply =
			    strtonum(options[OPT_TSIZE].o_request,
			    1, 65535LL * SEGSIZE_MAX, &errstr);
			if (errstr)
				return (EOPTNEG);
		}
	}
	fd = open(filename, mode == RRQ ? O_RDONLY : (O_WRONLY|wmode), 0666);
	if (fd == -1)
		return (errno + 100);
	/*
	 * If the file was created, set default permissions.
	 */
	if ((wmode & O_CREAT) && fchmod(fd, 0666) == -1) {
		int serrno = errno;

		close(fd);
		unlink(filename);

		return (serrno + 100);
	}
	client->file = fdopen(fd, mode == RRQ ? "r" : "w");
	if (client->file == NULL) {
		close(fd);
		return (errno + 100);
	}

	return (0);
}

int
fget_octet(struct tftp_client *client)
{
	return (getc(client->file));
}

int
fput_octet(struct tftp_client *client, int c)
{
	return (putc(c, client->file));
}

int
fget_netascii(struct tftp_client *client)
{
	int c = -1;

	switch (client->newline) {
	case 0:
		c = getc(client->file);
		if (c == EOF)
			break;

		if (c == '\n' || c == '\r') {
			client->newline = c;
			c = '\r';
		}
		break;
	case '\n':
		client->newline = 0;
		c = '\n';
		break;
	case '\r':
		client->newline = 0;
		c = '\0';
		break;
	}

	return (c);
}

int
fput_netascii(struct tftp_client *client, int c)
{
	if (client->newline == '\r') {
		client->newline = 0;

		if (c == '\0')
			c = '\r';

	} else if (c == '\r') {
		client->newline = c;
		return (c);
	}

	return (putc(c, client->file));
}

void
sendfile(struct tftp_client *client)
{
	event_set(&client->sev, client->sock, EV_READ, tftp_rrq_ack, client);
	client->block = 1;

	file_read(client);
}

void
file_read(struct tftp_client *client)
{
	u_int8_t *buf;
	struct tftphdr *dp;
	int i;
	int c;

	dp = (struct tftphdr *)client->buf;
	dp->th_opcode = htons((u_short)DATA);
	dp->th_block = htons(client->block);
	buf = (u_int8_t *)dp->th_data;

	for (i = 0; i < client->segment_size; i++) {
		c = client->fgetc(client);
		if (c == EOF) {
			if (ferror(client->file)) {
				nak(client, 100 + EIO);
				return;
			}

			break;
		}
		buf[i] = c;
	}

	client->buflen = i + 4;
	client->retries = RETRIES;

	tftp_send(client);
}

void
tftp_send(struct tftp_client *client)
{
	if (send(client->sock, client->buf, client->buflen, 0) == -1) {
		lwarn("send(block)");
		client_free(client);
		return;
	}

	event_add(&client->sev, &client->tv);
}

void
tftp_rrq_ack(int fd, short events, void *arg)
{
	struct tftp_client *client = arg;
	struct tftphdr *ap; /* ack packet */
	char rbuf[SEGSIZE_MIN];
	ssize_t n;

	if (events & EV_TIMEOUT) {
		if (retry(client) == -1) {
			lwarn("%s: retry", getip(&client->ss));
			goto done;
		}

		return;
	}

	n = recv(fd, rbuf, sizeof(rbuf), 0);
	if (n == -1) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
			event_add(&client->sev, &client->tv);
			return;

		default:
			lwarn("%s: recv", getip(&client->ss));
			goto done;
		}
	}

	ap = (struct tftphdr *)rbuf;
	ap->th_opcode = ntohs((u_short)ap->th_opcode);
	ap->th_block = ntohs((u_short)ap->th_block);

	switch (ap->th_opcode) {
	case ACK:
		break;
	case ERROR:
	default: /* assume the worst */
		goto done;
	}

	if (ap->th_block != client->block) {
		if (tftp_flush(client) == -1) {
			lwarnx("%s: flush", getip(&client->ss));
			goto done;
		}

		if (ap->th_block != (client->block - 1))
			goto done;

		tftp_send(client);
		return;
	}

	if (client->buflen != client->packet_size) {
		/* this was the last packet in the stream */
		goto done;
	}

	client->block++;
	file_read(client);
	return;

done:
	client_free(client);
}

int
tftp_flush(struct tftp_client *client)
{
	char rbuf[SEGSIZE_MIN];
	ssize_t n;

	for (;;) {
		n = recv(client->sock, rbuf, sizeof(rbuf), 0);
		if (n == -1) {
			switch (errno) {
			case EAGAIN:
				return (0);

			case EINTR:
				break;

			default:
				return (-1);
			}
		}
	}
}

void
recvfile(struct tftp_client *client)
{
	event_set(&client->sev, client->sock, EV_READ, tftp_wrq, client);
	tftp_wrq_ack(client);
}

int
tftp_wrq_ack_packet(struct tftp_client *client)
{
	struct tftphdr *ap; /* ack packet */

	ap = (struct tftphdr *)client->buf;
	ap->th_opcode = htons((u_short)ACK);
	ap->th_block = htons(client->block);

	client->buflen = 4;
	client->retries = RETRIES;

	return (send(client->sock, client->buf, client->buflen, 0) != 4);
}

void
tftp_wrq_ack(struct tftp_client *client)
{
	if (tftp_wrq_ack_packet(client) != 0) {
		lwarn("tftp wrq ack");
		client_free(client);
		return;
	}

	client->block++;
	event_add(&client->sev, &client->tv);
}

void
tftp_wrq(int fd, short events, void *arg)
{
	char wbuf[SEGSIZE_MAX + 4];
	struct tftp_client *client = arg;
	struct tftphdr *dp;
	ssize_t n;
	int i;

	if (events & EV_TIMEOUT) {
		if (retry(client) == -1) {
			lwarn("%s", getip(&client->ss));
			goto done;
		}

		return;
	}

	n = recv(fd, wbuf, client->packet_size, 0);
	if (n == -1) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
			goto retry;

		default:
			lwarn("tftp_wrq recv");
			goto done;
		}
	}

	if (n < 4)
		goto done;

	dp = (struct tftphdr *)wbuf;
	dp->th_opcode = ntohs((u_short)dp->th_opcode);
	dp->th_block = ntohs((u_short)dp->th_block);

	switch (dp->th_opcode) {
	case ERROR:
		goto done;
	case DATA:
		break;
	default:
		goto retry;
	}

	if (dp->th_block != client->block) {
		if (tftp_flush(client) == -1) {
			lwarnx("%s: flush", getip(&client->ss));
			goto done;
		}

		if (dp->th_block != (client->block - 1))
			goto done;

		goto retry;
	}

	for (i = 4; i < n; i++) {
		if (client->fputc(client, wbuf[i]) == EOF) {
			lwarn("tftp wrq");
			goto done;
		}
	}

	if (n < client->packet_size) {
		tftp_wrq_ack_packet(client);
		fclose(client->file);
		client->file = NULL;
		event_set(&client->sev, client->sock, EV_READ,
		    tftp_wrq_end, client);
		event_add(&client->sev, &client->tv);
		return;
	}

	tftp_wrq_ack(client);
	return;

retry:
	event_add(&client->sev, &client->tv);
	return;
done:
	client_free(client);
}

void
tftp_wrq_end(int fd, short events, void *arg)
{
	char wbuf[SEGSIZE_MAX + 4];
	struct tftp_client *client = arg;
	struct tftphdr *dp;
	ssize_t n;

	if (events & EV_TIMEOUT) {
		/* this was the last packet, we can clean up */
		goto done;
	}

	n = recv(fd, wbuf, client->packet_size, 0);
	if (n == -1) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
			goto retry;

		default:
			lwarn("tftp_wrq_end recv");
			goto done;
		}
	}

	if (n < 4)
		goto done;

	dp = (struct tftphdr *)wbuf;
	dp->th_opcode = ntohs((u_short)dp->th_opcode);
	dp->th_block = ntohs((u_short)dp->th_block);

	switch (dp->th_opcode) {
	case ERROR:
		goto done;
	case DATA:
		break;
	default:
		goto retry;
	}

	if (dp->th_block != client->block)
		goto done;

retry:
	if (retry(client) == -1) {
		lwarn("%s", getip(&client->ss));
		goto done;
	}
	return;
done:
	client_free(client);
	return;
}


/*
 * Send a nak packet (error message).
 * Error code passed in is one of the
 * standard TFTP codes, or a UNIX errno
 * offset by 100.
 */
void
nak(struct tftp_client *client, int error)
{
	struct tftphdr	*tp;
	struct errmsg	*pe;
	size_t		 length;
	ssize_t		 rslt;

	tp = (struct tftphdr *)client->buf;
	tp->th_opcode = htons((u_short)ERROR);
	tp->th_code = htons((u_short)error);

	for (pe = errmsgs; pe->e_code >= 0; pe++) {
		if (pe->e_code == error)
			break;
	}
	if (pe->e_code < 0) {
		pe->e_msg = strerror(error - 100);
		tp->th_code = htons(EUNDEF);   /* set 'undef' errorcode */
	}

	length = strlcpy(tp->th_msg, pe->e_msg, client->packet_size - 5) + 5;
	if (length > client->packet_size)
		length = client->packet_size;

	linfo("%s: nak: %s", getip(&client->ss), tp->th_msg);

	rslt = send(client->sock, client->buf, length, 0);
	if (rslt == -1)
		lwarn("%s: nak", getip(&client->ss));
	else if ((size_t)rslt != length)
		lwarnx("%s: nak: sent %zd of %zu bytes", getip(&client->ss),
		    rslt, length);

	client_free(client);
}

/*
 * Send an oack packet (option acknowledgement).
 */
int
oack(struct tftp_client *client)
{
	struct opt_client *options = client->options;
	struct tftphdr *tp;
	char *bp;
	int i, n, size;

	tp = (struct tftphdr *)client->buf;
	bp = (char *)tp->th_stuff;
	size = sizeof(client->buf) - 2;

	tp->th_opcode = htons((u_short)OACK);
	for (i = 0; i < NOPT; i++) {
		if (options[i].o_request == NULL)
			continue;

		n = snprintf(bp, size, "%s%c%lld", opt_names[i], '\0',
		    options[i].o_reply);
		if (n < 0 || n >= size) {
			lwarnx("oack: no buffer space");
			goto error;
		}

		bp += n + 1;
		size -= n + 1;
		if (size < 0) {
			lwarnx("oack: no buffer space");
			goto error;
		}
	}

	client->buflen = bp - client->buf;
	client->retries = RETRIES;

	if (send(client->sock, client->buf, client->buflen, 0) == -1) {
		lwarn("oack");
		goto error;
	}

	/* no client ACK for write requests with options */
	if (client->opcode == WRQ) {
		client->block = 1;
		event_set(&client->sev, client->sock, EV_READ,
		    tftp_wrq, client);
	} else
		event_set(&client->sev, client->sock, EV_READ,
		    oack_done, client);

	event_add(&client->sev, &client->tv);
	return (0);

error:
	return (-1);
}

int
retry(struct tftp_client *client)
{
	if (--client->retries == 0) {
		errno = ETIMEDOUT;
		return (-1);
	}

	tftp_send(client);

	return (0);
}

void
oack_done(int fd, short events, void *arg)
{
	struct tftp_client *client = arg;
	struct tftphdr *ap;
	ssize_t n;

	if (events & EV_TIMEOUT) {
		if (retry(client) == -1) {
			lwarn("%s", getip(&client->ss));
			goto done;
		}

		return;
	}

	n = recv(client->sock, client->buf, client->packet_size, 0);
	if (n == -1) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
			event_add(&client->sev, &client->tv);
			return;

		default:
			lwarn("%s: recv", getip(&client->ss));
			goto done;
		}
	}

	if (n < 4)
		goto done;

	ap = (struct tftphdr *)client->buf;
	ap->th_opcode = ntohs((u_short)ap->th_opcode);
	ap->th_block = ntohs((u_short)ap->th_block);

	if (ap->th_opcode != ACK || ap->th_block != 0)
		goto done;

	sendfile(client);
	return;

done:
	client_free(client);
}

const char *
getip(void *s)
{
	struct sockaddr *sa = s;
	static char hbuf[NI_MAXHOST];

	if (getnameinfo(sa, sa->sa_len, hbuf, sizeof(hbuf),
	    NULL, 0, NI_NUMERICHOST))
		strlcpy(hbuf, "0.0.0.0", sizeof(hbuf));

	return(hbuf);
}

/* daemon(3) clone, intended to be used in a "r"estricted environment */
int
rdaemon(int devnull)
{
	if (devnull == -1) {
		errno = EBADF;
		return (-1);
	}
	if (fcntl(devnull, F_GETFL) == -1)
		return (-1);

	switch (fork()) {
	case -1:
		return (-1);
	case 0:
		break;
	default:
		_exit(0);
	}

	if (setsid() == -1)
		return (-1);

	(void)dup2(devnull, STDIN_FILENO);
	(void)dup2(devnull, STDOUT_FILENO);
	(void)dup2(devnull, STDERR_FILENO);
	if (devnull > 2)
		(void)close(devnull);

	return (0);
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
