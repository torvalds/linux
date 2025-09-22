/*	$OpenBSD: slowcgi.c,v 1.64 2022/08/07 07:43:53 op Exp $ */
/*
 * Copyright (c) 2013 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2013 Florian Obser <florian@openbsd.org>
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
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <err.h>
#include <fcntl.h>
#include <errno.h>
#include <event.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#define TIMEOUT_DEFAULT		 120
#define TIMEOUT_MAX		 (86400 * 365)
#define SLOWCGI_USER		 "www"

#define FCGI_CONTENT_SIZE	 65535
#define FCGI_PADDING_SIZE	 255
#define FCGI_RECORD_SIZE	 \
    (sizeof(struct fcgi_record_header) + FCGI_CONTENT_SIZE + FCGI_PADDING_SIZE)

#define FCGI_ALIGNMENT		 8
#define FCGI_ALIGN(n)		 \
    (((n) + (FCGI_ALIGNMENT - 1)) & ~(FCGI_ALIGNMENT - 1))

#define STDOUT_DONE		 1
#define STDERR_DONE		 2
#define SCRIPT_DONE		 4

#define FCGI_BEGIN_REQUEST	 1
#define FCGI_ABORT_REQUEST	 2
#define FCGI_END_REQUEST	 3
#define FCGI_PARAMS		 4
#define FCGI_STDIN		 5
#define FCGI_STDOUT		 6
#define FCGI_STDERR		 7
#define FCGI_DATA		 8
#define FCGI_GET_VALUES		 9
#define FCGI_GET_VALUES_RESULT	10
#define FCGI_UNKNOWN_TYPE	11
#define FCGI_MAXTYPE		(FCGI_UNKNOWN_TYPE)

#define FCGI_REQUEST_COMPLETE	0
#define FCGI_CANT_MPX_CONN	1
#define FCGI_OVERLOADED		2
#define FCGI_UNKNOWN_ROLE	3

#define FD_RESERVE		5
#define FD_NEEDED		6
int cgi_inflight = 0;

struct listener {
	struct event	ev, pause;
};

struct env_val {
	SLIST_ENTRY(env_val)	 entry;
	char			*val;
};
SLIST_HEAD(env_head, env_val);

struct fcgi_record_header {
	uint8_t		version;
	uint8_t		type;
	uint16_t	id;
	uint16_t	content_len;
	uint8_t		padding_len;
	uint8_t		reserved;
}__packed;

struct fcgi_response {
	TAILQ_ENTRY(fcgi_response)	entry;
	uint8_t				data[FCGI_RECORD_SIZE];
	size_t				data_pos;
	size_t				data_len;
};
TAILQ_HEAD(fcgi_response_head, fcgi_response);

struct fcgi_stdin {
	TAILQ_ENTRY(fcgi_stdin)	entry;
	uint8_t			data[FCGI_RECORD_SIZE];
	size_t			data_pos;
	size_t			data_len;
};
TAILQ_HEAD(fcgi_stdin_head, fcgi_stdin);

struct request {
	LIST_ENTRY(request)		entry;
	struct event			ev;
	struct event			resp_ev;
	struct event			tmo;
	int				fd;
	uint8_t				buf[FCGI_RECORD_SIZE];
	size_t				buf_pos;
	size_t				buf_len;
	struct fcgi_response_head	response_head;
	struct fcgi_stdin_head		stdin_head;
	uint16_t			id;
	char				script_name[PATH_MAX];
	struct env_head			env;
	int				env_count;
	pid_t				script_pid;
	int				script_status;
	struct event			script_ev;
	struct event			script_err_ev;
	struct event			script_stdin_ev;
	int				stdin_fd_closed;
	int				stdout_fd_closed;
	int				stderr_fd_closed;
	uint8_t				script_flags;
	uint8_t				request_started;
	int				inflight_fds_accounted;
};

LIST_HEAD(requests_head, request);

struct slowcgi_proc {
	struct requests_head	requests;
	struct event		ev_sigchld;
};

struct fcgi_begin_request_body {
	uint16_t	role;
	uint8_t		flags;
	uint8_t		reserved[5];
}__packed;

struct fcgi_end_request_body {
	uint32_t	app_status;
	uint8_t		protocol_status;
	uint8_t		reserved[3];
}__packed;

__dead void	usage(void);
int		slowcgi_listen(char *, struct passwd *);
void		slowcgi_paused(int, short, void *);
int		accept_reserve(int, struct sockaddr *, socklen_t *, int, int *);
void		slowcgi_accept(int, short, void *);
void		slowcgi_request(int, short, void *);
void		slowcgi_response(int, short, void *);
void		slowcgi_add_response(struct request *, struct fcgi_response *);
void		slowcgi_timeout(int, short, void *);
void		slowcgi_sig_handler(int, short, void *);
size_t		parse_record(uint8_t * , size_t, struct request *);
void		parse_begin_request(uint8_t *, uint16_t, struct request *,
		    uint16_t);
void		parse_params(uint8_t *, uint16_t, struct request *, uint16_t);
void		parse_stdin(uint8_t *, uint16_t, struct request *, uint16_t);
void		exec_cgi(struct request *);
void		script_in(int, struct event *, struct request *, uint8_t);
void		script_std_in(int, short, void *);
void		script_err_in(int, short, void *);
void		script_out(int, short, void *);
void		create_end_record(struct request *);
void		dump_fcgi_record(const char *,
		    struct fcgi_record_header *);
void		dump_fcgi_record_header(const char *,
		    struct fcgi_record_header *);
void		dump_fcgi_begin_request_body(const char *,
		    struct fcgi_begin_request_body *);
void		dump_fcgi_end_request_body(const char *,
		    struct fcgi_end_request_body *);
void		cleanup_request(struct request *);

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
	fprintf(stderr,
	    "usage: %s [-dv] [-p path] [-s socket] [-t timeout] [-U user] "
	    "[-u user]\n", __progname);
	exit(1);
}

struct timeval		timeout = { TIMEOUT_DEFAULT, 0 };
struct slowcgi_proc	slowcgi_proc;
int			debug = 0;
int			verbose = 0;
int			on = 1;
char			*fcgi_socket = "/var/www/run/slowcgi.sock";

int
main(int argc, char *argv[])
{
	extern char *__progname;
	struct listener	*l = NULL;
	struct passwd	*pw;
	struct stat	 sb;
	int		 c, fd;
	const char	*chrootpath = NULL;
	const char	*sock_user = SLOWCGI_USER;
	const char	*slowcgi_user = SLOWCGI_USER;
	const char	*errstr;

	/*
	 * Ensure we have fds 0-2 open so that we have no fd overlaps
	 * in exec_cgi() later. Just exit on error, we don't have enough
	 * fds open to output an error message anywhere.
	 */
	for (c=0; c < 3; c++) {
		if (fstat(c, &sb) == -1) {
			if ((fd = open("/dev/null", O_RDWR)) != -1) {
				if (dup2(fd, c) == -1)
					exit(1);
				if (fd > c)
					close(fd);
			} else
				exit(1);
		}
	}

	while ((c = getopt(argc, argv, "dp:s:t:U:u:v")) != -1) {
		switch (c) {
		case 'd':
			debug++;
			break;
		case 'p':
			chrootpath = optarg;
			break;
		case 's':
			fcgi_socket = optarg;
			break;
		case 't':
			timeout.tv_sec = strtonum(optarg, 1, TIMEOUT_MAX, 
			    &errstr);
			if (errstr != NULL)
				errx(1, "timeout is %s: %s", errstr, optarg);
			break;
		case 'U':
			sock_user = optarg;
			break;
		case 'u':
			slowcgi_user = optarg;
			break;
		case 'v':
			verbose++;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	if (geteuid() != 0)
		errx(1, "need root privileges");

	if (!debug && daemon(0, 0) == -1)
		err(1, "daemon");

	if (!debug) {
		openlog(__progname, LOG_PID|LOG_NDELAY, LOG_DAEMON);
		logger = &syslogger;
	}

	ldebug("sock_user: %s", sock_user);
	pw = getpwnam(sock_user);
	if (pw == NULL)
		lerrx(1, "no %s user", sock_user);

	fd = slowcgi_listen(fcgi_socket, pw);

	ldebug("slowcgi_user: %s", slowcgi_user);
	pw = getpwnam(slowcgi_user);
	if (pw == NULL)
		lerrx(1, "no %s user", slowcgi_user);

	if (chrootpath == NULL)
		chrootpath = pw->pw_dir;

	if (chroot(chrootpath) == -1)
		lerr(1, "chroot(%s)", chrootpath);

	ldebug("chroot: %s", chrootpath);

	if (chdir("/") == -1)
		lerr(1, "chdir(/)");

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		lerr(1, "unable to revoke privs");

	if (pledge("stdio rpath unix proc exec", NULL) == -1)
		lerr(1, "pledge");

	LIST_INIT(&slowcgi_proc.requests);
	event_init();

	l = calloc(1, sizeof(*l));
	if (l == NULL)
		lerr(1, "listener ev alloc");

	event_set(&l->ev, fd, EV_READ | EV_PERSIST, slowcgi_accept, l);
	event_add(&l->ev, NULL);
	evtimer_set(&l->pause, slowcgi_paused, l);

	signal_set(&slowcgi_proc.ev_sigchld, SIGCHLD, slowcgi_sig_handler,
	    &slowcgi_proc);
	signal(SIGPIPE, SIG_IGN);

	signal_add(&slowcgi_proc.ev_sigchld, NULL);

	event_dispatch();
	return (0);
}

int
slowcgi_listen(char *path, struct passwd *pw)
{
	struct sockaddr_un	 sun;
	mode_t			 old_umask;
	int			 fd;

	if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
	    0)) == -1)
		lerr(1, "slowcgi_listen: socket");

	bzero(&sun, sizeof(sun));
	sun.sun_family = AF_UNIX;
	if (strlcpy(sun.sun_path, path, sizeof(sun.sun_path)) >=
	    sizeof(sun.sun_path))
		lerrx(1, "socket path too long");

	if (unlink(path) == -1)
		if (errno != ENOENT)
			lerr(1, "slowcgi_listen: unlink %s", path);

	old_umask = umask(S_IXUSR|S_IXGRP|S_IWOTH|S_IROTH|S_IXOTH);

	if (bind(fd, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		lerr(1,"slowcgi_listen: bind: %s", path);

	umask(old_umask);

	if (chown(path, pw->pw_uid, pw->pw_gid) == -1)
		lerr(1, "slowcgi_listen: chown: %s", path);

	if (listen(fd, 5) == -1)
		lerr(1, "listen");

	ldebug("socket: %s", path);
	return fd;
}

void
slowcgi_paused(int fd, short events, void *arg)
{
	struct listener	*l = arg;
	event_add(&l->ev, NULL);
}

int
accept_reserve(int sockfd, struct sockaddr *addr, socklen_t *addrlen,
    int reserve, int *counter)
{
	int ret;
	if (getdtablecount() + reserve +
	    ((*counter + 1) * FD_NEEDED) >= getdtablesize()) {
		ldebug("inflight fds exceeded");
		errno = EMFILE;
		return -1;
	}

	if ((ret = accept4(sockfd, addr, addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC))
	    > -1) {
		(*counter)++;
		ldebug("inflight incremented, now %d", *counter);
	}
	return ret;
}

void
slowcgi_accept(int fd, short events, void *arg)
{
	struct listener		*l;
	struct sockaddr_storage	 ss;
	struct timeval		 backoff;
	struct request		*c;
	socklen_t		 len;
	int			 s;

	l = arg;
	backoff.tv_sec = 1;
	backoff.tv_usec = 0;
	c = NULL;

	len = sizeof(ss);
	if ((s = accept_reserve(fd, (struct sockaddr *)&ss,
	    &len, FD_RESERVE, &cgi_inflight)) == -1) {
		switch (errno) {
		case EINTR:
		case EWOULDBLOCK:
		case ECONNABORTED:
			return;
		case EMFILE:
		case ENFILE:
			event_del(&l->ev);
			evtimer_add(&l->pause, &backoff);
			return;
		default:
			lerr(1, "accept");
		}
	}

	c = calloc(1, sizeof(*c));
	if (c == NULL) {
		lwarn("cannot calloc request");
		close(s);
		cgi_inflight--;
		return;
	}
	c->fd = s;
	c->buf_pos = 0;
	c->buf_len = 0;
	c->request_started = 0;
	c->stdin_fd_closed = c->stdout_fd_closed = c->stderr_fd_closed = 0;
	c->inflight_fds_accounted = 0;
	TAILQ_INIT(&c->response_head);
	TAILQ_INIT(&c->stdin_head);

	event_set(&c->ev, s, EV_READ | EV_PERSIST, slowcgi_request, c);
	event_add(&c->ev, NULL);
	event_set(&c->resp_ev, s, EV_WRITE | EV_PERSIST, slowcgi_response, c);
	evtimer_set(&c->tmo, slowcgi_timeout, c);
	evtimer_add(&c->tmo, &timeout);
	LIST_INSERT_HEAD(&slowcgi_proc.requests, c, entry);
}

void
slowcgi_timeout(int fd, short events, void *arg)
{
	cleanup_request((struct request*) arg);
}

void
slowcgi_sig_handler(int sig, short event, void *arg)
{
	struct request		*c;
	struct slowcgi_proc	*p;
	pid_t			 pid;
	int			 status;

	p = arg;

	switch (sig) {
	case SIGCHLD:
		while ((pid = waitpid(WAIT_ANY, &status, WNOHANG)) > 0) {
			LIST_FOREACH(c, &p->requests, entry)
				if (c->script_pid == pid)
					break;
			if (c == NULL) {
				lwarnx("caught exit of unknown child %i", pid);
				continue;
			}

			if (WIFSIGNALED(status))
				c->script_status = WTERMSIG(status);
			else
				c->script_status = WEXITSTATUS(status);

			if (c->script_flags == (STDOUT_DONE | STDERR_DONE))
				create_end_record(c);
			c->script_flags |= SCRIPT_DONE;

			ldebug("wait: %s", c->script_name);
		}
		if (pid == -1 && errno != ECHILD)
			lwarn("waitpid");
		break;
	default:
		lerr(1, "unexpected signal: %d", sig);
		break;
	}
}

void
slowcgi_add_response(struct request *c, struct fcgi_response *resp)
{
	struct fcgi_record_header	*header;
	size_t				 padded_len;

	header = (struct fcgi_record_header*)resp->data;

	/* The FastCGI spec suggests to align the output buffer */
	padded_len = FCGI_ALIGN(resp->data_len);
	if (padded_len > resp->data_len) {
		/* There should always be FCGI_PADDING_SIZE bytes left */
		if (padded_len > FCGI_RECORD_SIZE)
			lerr(1, "response too long");
		header->padding_len = padded_len - resp->data_len;
		resp->data_len = padded_len;
	}

	TAILQ_INSERT_TAIL(&c->response_head, resp, entry);
	event_add(&c->resp_ev, NULL);
}

void
slowcgi_response(int fd, short events, void *arg)
{
	struct request			*c;
	struct fcgi_record_header	*header;
	struct fcgi_response		*resp;
	ssize_t				 n;

	c = arg;

	while ((resp = TAILQ_FIRST(&c->response_head))) {
		header = (struct fcgi_record_header*) resp->data;
		if (debug > 1)
			dump_fcgi_record("resp ", header);

		n = write(fd, resp->data + resp->data_pos, resp->data_len);
		if (n == -1) {
			if (errno == EAGAIN || errno == EINTR)
				return;
			cleanup_request(c);
			return;
		}
		resp->data_pos += n;
		resp->data_len -= n;
		if (resp->data_len == 0) {
			TAILQ_REMOVE(&c->response_head, resp, entry);
			free(resp);
		}
	}

	if (TAILQ_EMPTY(&c->response_head)) {
		if (c->script_flags == (STDOUT_DONE | STDERR_DONE |
		    SCRIPT_DONE))
			cleanup_request(c);
		else
			event_del(&c->resp_ev);
	}
}

void
slowcgi_request(int fd, short events, void *arg)
{
	struct request	*c;
	ssize_t		 n;
	size_t		 parsed;

	c = arg;

	n = read(fd, c->buf + c->buf_pos + c->buf_len,
	    FCGI_RECORD_SIZE - c->buf_pos-c->buf_len);

	switch (n) {
	case -1:
		switch (errno) {
		case EINTR:
		case EAGAIN:
			return;
		default:
			goto fail;
		}
		break;

	case 0:
		ldebug("closed connection");
		goto fail;
	default:
		break;
	}

	c->buf_len += n;

	/*
	 * Parse the records as they are received. Per the FastCGI
	 * specification, the server need only receive the FastCGI
	 * parameter records in full; it is free to begin execution
	 * at that point, which is what happens here.
	 */
	do {
		parsed = parse_record(c->buf + c->buf_pos, c->buf_len, c);
		c->buf_pos += parsed;
		c->buf_len -= parsed;
	} while (parsed > 0 && c->buf_len > 0);

	/* Make space for further reads */
	if (c->buf_len > 0) {
		bcopy(c->buf + c->buf_pos, c->buf, c->buf_len);
		c->buf_pos = 0;
	}
	return;
fail:
	cleanup_request(c);
}

void
parse_begin_request(uint8_t *buf, uint16_t n, struct request *c, uint16_t id)
{
	/* XXX -- FCGI_CANT_MPX_CONN */
	if (c->request_started) {
		lwarnx("unexpected FCGI_BEGIN_REQUEST, ignoring");
		return;
	}

	if (n != sizeof(struct fcgi_begin_request_body)) {
		lwarnx("wrong size %d != %lu", n,
		    sizeof(struct fcgi_begin_request_body));
		return;
	}

	c->request_started = 1;

	c->id = id;
	SLIST_INIT(&c->env);
	c->env_count = 0;
}

void
parse_params(uint8_t *buf, uint16_t n, struct request *c, uint16_t id)
{
	struct env_val			*env_entry;
	uint32_t			 name_len, val_len;

	if (!c->request_started) {
		lwarnx("FCGI_PARAMS without FCGI_BEGIN_REQUEST, ignoring");
		return;
	}

	if (c->id != id) {
		lwarnx("unexpected id, ignoring");
		return;
	}

	/*
	 * If this is the last FastCGI parameter record,
	 * begin execution of the CGI script.
	 */
	if (n == 0) {
		exec_cgi(c);
		return;
	}

	while (n > 0) {
		if (buf[0] >> 7 == 0) {
			name_len = buf[0];
			n--;
			buf++;
		} else {
			if (n > 3) {
				name_len = ((buf[0] & 0x7f) << 24) +
				    (buf[1] << 16) + (buf[2] << 8) + buf[3];
				n -= 4;
				buf += 4;
			} else
				return;
		}

		if (n > 0) {
			if (buf[0] >> 7 == 0) {
				val_len = buf[0];
				n--;
				buf++;
			} else {
				if (n > 3) {
					val_len = ((buf[0] & 0x7f) << 24) +
					    (buf[1] << 16) + (buf[2] << 8) +
					     buf[3];
					n -= 4;
					buf += 4;
				} else
					return;
			}
		} else
			return;

		if (n < name_len + val_len)
			return;

		if ((env_entry = malloc(sizeof(struct env_val))) == NULL) {
			lwarnx("cannot allocate env_entry");
			return;
		}

		if ((env_entry->val = calloc(sizeof(char), name_len + val_len +
		    2)) == NULL) {
			lwarnx("cannot allocate env_entry->val");
			free(env_entry);
			return;
		}

		bcopy(buf, env_entry->val, name_len);
		buf += name_len;
		n -= name_len;

		env_entry->val[name_len] = '\0';
		if (val_len < PATH_MAX && strcmp(env_entry->val,
		    "SCRIPT_NAME") == 0 && c->script_name[0] == '\0') {
			bcopy(buf, c->script_name, val_len);
			c->script_name[val_len] = '\0';
		} else if (val_len < PATH_MAX && strcmp(env_entry->val,
		    "SCRIPT_FILENAME") == 0) {
			bcopy(buf, c->script_name, val_len);
			c->script_name[val_len] = '\0';
		}
		env_entry->val[name_len] = '=';

		bcopy(buf, (env_entry->val) + name_len + 1, val_len);
		buf += val_len;
		n -= val_len;

		SLIST_INSERT_HEAD(&c->env, env_entry, entry);
		ldebug("env[%d], %s", c->env_count, env_entry->val);
		c->env_count++;
	}
}

void
parse_stdin(uint8_t *buf, uint16_t n, struct request *c, uint16_t id)
{
	struct fcgi_stdin	*node;

	if (c->id != id) {
		lwarnx("unexpected id, ignoring");
		return;
	}

	if ((node = calloc(1, sizeof(struct fcgi_stdin))) == NULL) {
		lwarnx("cannot calloc stdin node");
		return;
	}

	bcopy(buf, node->data, n);
	node->data_pos = 0;
	node->data_len = n;

	TAILQ_INSERT_TAIL(&c->stdin_head, node, entry);

	if (event_initialized(&c->script_stdin_ev))
		event_add(&c->script_stdin_ev, NULL);
}

size_t
parse_record(uint8_t *buf, size_t n, struct request *c)
{
	struct fcgi_record_header	*h;

	if (n < sizeof(struct fcgi_record_header))
		return (0);

	h = (struct fcgi_record_header*) buf;

	if (debug > 1)
		dump_fcgi_record("", h);

	if (n < sizeof(struct fcgi_record_header) + ntohs(h->content_len)
	    + h->padding_len)
		return (0);

	if (h->version != 1)
		lerrx(1, "wrong version");

	switch (h->type) {
	case FCGI_BEGIN_REQUEST:
		parse_begin_request(buf + sizeof(struct fcgi_record_header),
		    ntohs(h->content_len), c, ntohs(h->id));
		break;
	case FCGI_PARAMS:
		parse_params(buf + sizeof(struct fcgi_record_header),
		    ntohs(h->content_len), c, ntohs(h->id));
		break;
	case FCGI_STDIN:
		parse_stdin(buf + sizeof(struct fcgi_record_header),
		    ntohs(h->content_len), c, ntohs(h->id));
		break;
	default:
		lwarnx("unimplemented type %d", h->type);
		break;
	}

	return (sizeof(struct fcgi_record_header) + ntohs(h->content_len)
	    + h->padding_len);
}

/*
 * Fork a new CGI process to handle the request, translating
 * between FastCGI parameter records and CGI's environment variables,
 * as well as between the CGI process' stdin/stdout and the
 * corresponding FastCGI records.
 */
void
exec_cgi(struct request *c)
{
	struct env_val	*env_entry;
	int		 s_in[2], s_out[2], s_err[2], i;
	pid_t		 pid;
	char		*argv[2];
	char		**env;
	char		*path;

	i = 0;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, s_in) == -1)
		lerr(1, "socketpair");
	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, s_out) == -1)
		lerr(1, "socketpair");
	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, s_err) == -1)
		lerr(1, "socketpair");
	cgi_inflight--;
	c->inflight_fds_accounted = 1;
	ldebug("fork: %s", c->script_name);

	switch (pid = fork()) {
	case -1:
		c->script_status = errno;

		lwarn("fork");

		close(s_in[0]);
		close(s_out[0]);
		close(s_err[0]);

		close(s_in[1]);
		close(s_out[1]);
		close(s_err[1]);

		c->stdin_fd_closed = c->stdout_fd_closed =
		    c->stderr_fd_closed = 1;
		c->script_flags = (STDOUT_DONE | STDERR_DONE | SCRIPT_DONE);

		create_end_record(c);
		return;
	case 0:
		/* Child process */
		if (pledge("stdio rpath exec", NULL) == -1)
			lerr(1, "pledge");
		close(s_in[0]);
		close(s_out[0]);
		close(s_err[0]);

		if (dup2(s_in[1], STDIN_FILENO) == -1)
			_exit(1);
		if (dup2(s_out[1], STDOUT_FILENO) == -1)
			_exit(1);
		if (dup2(s_err[1], STDERR_FILENO) == -1)
			_exit(1);

		close(s_in[1]);
		close(s_out[1]);
		close(s_err[1]);

		signal(SIGPIPE, SIG_DFL);

		path = strrchr(c->script_name, '/');
		if (path != NULL) {
			if (path != c->script_name) {
				*path = '\0';
				if (chdir(c->script_name) == -1)
					lwarn("cannot chdir to %s",
					    c->script_name);
				*path = '/';
			} else
				if (chdir("/") == -1)
					lwarn("cannot chdir to /");
		}

		argv[0] = c->script_name;
		argv[1] = NULL;
		if ((env = calloc(c->env_count + 1, sizeof(char*))) == NULL)
			_exit(1);
		SLIST_FOREACH(env_entry, &c->env, entry)
			env[i++] = env_entry->val;
		env[i++] = NULL;
		execve(c->script_name, argv, env);
		lwarn("execve %s", c->script_name);
		_exit(1);

	}

	/* Parent process*/
	close(s_in[1]);
	close(s_out[1]);
	close(s_err[1]);

	fcntl(s_in[0], F_SETFD, FD_CLOEXEC);
	fcntl(s_out[0], F_SETFD, FD_CLOEXEC);
	fcntl(s_err[0], F_SETFD, FD_CLOEXEC);

	if (ioctl(s_in[0], FIONBIO, &on) == -1)
		lerr(1, "script ioctl(FIONBIO)");
	if (ioctl(s_out[0], FIONBIO, &on) == -1)
		lerr(1, "script ioctl(FIONBIO)");
	if (ioctl(s_err[0], FIONBIO, &on) == -1)
		lerr(1, "script ioctl(FIONBIO)");

	c->script_pid = pid;
	event_set(&c->script_stdin_ev, s_in[0], EV_WRITE | EV_PERSIST,
	    script_out, c);
	event_add(&c->script_stdin_ev, NULL);
	event_set(&c->script_ev, s_out[0], EV_READ | EV_PERSIST,
	    script_std_in, c);
	event_add(&c->script_ev, NULL);
	event_set(&c->script_err_ev, s_err[0], EV_READ | EV_PERSIST,
	    script_err_in, c);
	event_add(&c->script_err_ev, NULL);
}

void
create_end_record(struct request *c)
{
	struct fcgi_response		*resp;
	struct fcgi_record_header	*header;
	struct fcgi_end_request_body	*end_request;

	if ((resp = calloc(1, sizeof(struct fcgi_response))) == NULL) {
		lwarnx("cannot malloc fcgi_response");
		return;
	}
	header = (struct fcgi_record_header*) resp->data;
	header->version = 1;
	header->type = FCGI_END_REQUEST;
	header->id = htons(c->id);
	header->content_len = htons(sizeof(struct
	    fcgi_end_request_body));
	header->padding_len = 0;
	header->reserved = 0;
	end_request = (struct fcgi_end_request_body *) (resp->data +
	    sizeof(struct fcgi_record_header));
	end_request->app_status = htonl(c->script_status);
	end_request->protocol_status = FCGI_REQUEST_COMPLETE;
	end_request->reserved[0] = 0;
	end_request->reserved[1] = 0;
	end_request->reserved[2] = 0;
	resp->data_pos = 0;
	resp->data_len = sizeof(struct fcgi_end_request_body) +
	    sizeof(struct fcgi_record_header);
	slowcgi_add_response(c, resp);
}

void
script_in(int fd, struct event *ev, struct request *c, uint8_t type)
{
	struct fcgi_response		*resp;
	struct fcgi_record_header	*header;
	ssize_t				 n;

	if ((resp = calloc(1, sizeof(struct fcgi_response))) == NULL) {
		lwarnx("cannot malloc fcgi_response");
		return;
	}
	header = (struct fcgi_record_header*) resp->data;
	header->version = 1;
	header->type = type;
	header->id = htons(c->id);
	header->padding_len = 0;
	header->reserved = 0;

	n = read(fd, resp->data + sizeof(struct fcgi_record_header),
	    FCGI_CONTENT_SIZE);

	if (n == -1) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
			free(resp);
			return;
		default:
			n = 0; /* fake empty FCGI_STD{OUT,ERR} response */
		}
	}
	header->content_len = htons(n);
	resp->data_pos = 0;
	resp->data_len = n + sizeof(struct fcgi_record_header);
	slowcgi_add_response(c, resp);

	if (n == 0) {
		if (type == FCGI_STDOUT)
			c->script_flags |= STDOUT_DONE;
		else
			c->script_flags |= STDERR_DONE;

		if (c->script_flags == (STDOUT_DONE | STDERR_DONE |
		    SCRIPT_DONE)) {
			create_end_record(c);
		}
		event_del(ev);
		close(fd);
		if (type == FCGI_STDOUT)
			c->stdout_fd_closed = 1;
		else
			c->stderr_fd_closed = 1;
	}
}

void
script_std_in(int fd, short events, void *arg)
{
	struct request *c = arg;
	script_in(fd, &c->script_ev, c, FCGI_STDOUT);
}

void
script_err_in(int fd, short events, void *arg)
{
	struct request *c = arg;
	script_in(fd, &c->script_err_ev, c, FCGI_STDERR);
}

void
script_out(int fd, short events, void *arg)
{
	struct request		*c;
	struct fcgi_stdin	*node;
	ssize_t			 n;

	c = arg;

	while ((node = TAILQ_FIRST(&c->stdin_head))) {
		if (node->data_len == 0) { /* end of stdin marker */
			close(fd);
			c->stdin_fd_closed = 1;
			break;
		}
		n = write(fd, node->data + node->data_pos, node->data_len);
		if (n == -1) {
			if (errno == EAGAIN || errno == EINTR)
				return;
			event_del(&c->script_stdin_ev);
			return;
		}
		node->data_pos += n;
		node->data_len -= n;
		if (node->data_len == 0) {
			TAILQ_REMOVE(&c->stdin_head, node, entry);
			free(node);
		}
	}
	event_del(&c->script_stdin_ev);
}

void
cleanup_request(struct request *c)
{
	struct fcgi_response	*resp;
	struct fcgi_stdin	*stdin_node;
	struct env_val		*env_entry;

	evtimer_del(&c->tmo);
	if (event_initialized(&c->ev))
		event_del(&c->ev);
	if (event_initialized(&c->resp_ev))
		event_del(&c->resp_ev);
	if (event_initialized(&c->script_ev)) {
		if (!c->stdout_fd_closed)
			close(EVENT_FD(&c->script_ev));
		event_del(&c->script_ev);
	}
	if (event_initialized(&c->script_err_ev)) {
		if (!c->stderr_fd_closed)
			close(EVENT_FD(&c->script_err_ev));
		event_del(&c->script_err_ev);
	}
	if (event_initialized(&c->script_stdin_ev)) {
		if (!c->stdin_fd_closed)
			close(EVENT_FD(&c->script_stdin_ev));
		event_del(&c->script_stdin_ev);
	}
	close(c->fd);
	while (!SLIST_EMPTY(&c->env)) {
		env_entry = SLIST_FIRST(&c->env);
		SLIST_REMOVE_HEAD(&c->env, entry);
		free(env_entry->val);
		free(env_entry);
	}

	while ((resp = TAILQ_FIRST(&c->response_head))) {
		TAILQ_REMOVE(&c->response_head, resp, entry);
		free(resp);
	}
	while ((stdin_node = TAILQ_FIRST(&c->stdin_head))) {
		TAILQ_REMOVE(&c->stdin_head, stdin_node, entry);
		free(stdin_node);
	}
	LIST_REMOVE(c, entry);
	if (! c->inflight_fds_accounted)
		cgi_inflight--;
	free(c);
}

void
dump_fcgi_record(const char *p, struct fcgi_record_header *h)
{
	dump_fcgi_record_header(p, h);

	if (h->type == FCGI_BEGIN_REQUEST)
		dump_fcgi_begin_request_body(p,
		    (struct fcgi_begin_request_body *)(h + 1));
	else if (h->type == FCGI_END_REQUEST)
		dump_fcgi_end_request_body(p,
		    (struct fcgi_end_request_body *)(h + 1));
}

void
dump_fcgi_record_header(const char* p, struct fcgi_record_header *h)
{
	ldebug("%sversion:         %d", p, h->version);
	ldebug("%stype:            %d", p, h->type);
	ldebug("%srequestId:       %d", p, ntohs(h->id));
	ldebug("%scontentLength:   %d", p, ntohs(h->content_len));
	ldebug("%spaddingLength:   %d", p, h->padding_len);
	ldebug("%sreserved:        %d", p, h->reserved);
}

void
dump_fcgi_begin_request_body(const char *p, struct fcgi_begin_request_body *b)
{
	ldebug("%srole             %d", p, ntohs(b->role));
	ldebug("%sflags            %d", p, b->flags);
}

void
dump_fcgi_end_request_body(const char *p, struct fcgi_end_request_body *b)
{
	ldebug("%sappStatus:       %d", p, ntohl(b->app_status));
	ldebug("%sprotocolStatus:  %d", p, b->protocol_status);
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

__dead void
syslog_err(int ecode, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	syslog_vstrerror(errno, LOG_CRIT, fmt, ap);
	va_end(ap);
	exit(ecode);
}

__dead void
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
	if (verbose > 0) {
		va_list ap;
		va_start(ap, fmt);
		vsyslog(LOG_DEBUG, fmt, ap);
		va_end(ap);
	}
}
