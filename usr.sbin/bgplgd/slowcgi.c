/*	$OpenBSD: slowcgi.c,v 1.7 2024/01/26 18:11:49 job Exp $ */
/*
 * Copyright (c) 2020 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2019 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "slowcgi.h"
#include "bgplgd.h"
#include "http.h"
#include "version.h"

#define TIMEOUT_DEFAULT		 30
#define WWW_USER		 "www"
#define BGPLGD_USER		 "_bgplgd"

#define FCGI_CONTENT_SIZE	 65535
#define FCGI_PADDING_SIZE	 255
#define FCGI_RECORD_SIZE	 \
    (sizeof(struct fcgi_record_header) + FCGI_CONTENT_SIZE + FCGI_PADDING_SIZE)

#define FCGI_ALIGNMENT		 8
#define FCGI_ALIGN(n)		 \
    (((n) + (FCGI_ALIGNMENT - 1)) & ~(FCGI_ALIGNMENT - 1))

#define STDOUT_DONE		0x1
#define STDERR_DONE		0x2
#define SCRIPT_DONE		0x4

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
	char			*key;
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

struct request {
	LIST_ENTRY(request)		entry;
	struct event			ev;
	struct event			resp_ev;
	struct event			tmo;
	struct event			script_ev;
	struct event			script_err_ev;
	struct fcgi_response_head	response_head;
	struct env_head			env;
	uint8_t				buf[FCGI_RECORD_SIZE];
	size_t				buf_pos;
	size_t				buf_len;
	int				fd;
	int				env_count;
	int				inflight_fds_accounted;
	pid_t				command_pid;
	int				command_status;
	int				script_flags;
	uint16_t			id;
	uint8_t				request_started;
	uint8_t				request_done;
	uint8_t				timeout_fired;
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
int		accept_reserve(int, struct sockaddr *, socklen_t *, int,
		    volatile int *);
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
char		*env_get(struct request *, const char *);
void		error_response(struct request *, int);
void		exec_cgi(struct request *);
void		script_std_in(int, short, void *);
void		script_err_in(int, short, void *);
void		create_data_record(struct request *, uint8_t, const void *,
		    size_t);
void		create_end_record(struct request *);
void		cleanup_request(struct request *);
void		dump_fcgi_record(const char *,
		    struct fcgi_record_header *);
void		dump_fcgi_record_header(const char *,
		    struct fcgi_record_header *);
void		dump_fcgi_begin_request_body(const char *,
		    struct fcgi_begin_request_body *);
void		dump_fcgi_end_request_body(const char *,
		    struct fcgi_end_request_body *);

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

__dead void
usage(void)
{
	extern char *__progname;
	fprintf(stderr,
	    "usage: %s [-d] [-p path] [-S socket] [-s socket] [-U user]\n",
	    __progname);
	exit(1);
}

struct timeval		timeout = { TIMEOUT_DEFAULT, 0 };
struct timeval		kill_timeout = { 5, 0 };
struct slowcgi_proc	slowcgi_proc;
int			debug = 0;
int			on = 1;
char			*fcgi_socket = "/var/www/run/bgplgd.sock";
char			*bgpctlpath = "bgpctl";
char			*bgpctlsock = "/var/run/bgpd.rsock";


/*
 * Unveil the command we want to run.
 * If this has a pathname component in it, interpret as a file
 * and unveil the file directly.
 * Otherwise, look up the command in our PATH.
 */
static void
unveil_command(const char *prog)
{
	const char *pp;
	char *save, *cmd, *path;
	struct stat st;

	if (strchr(prog, '/') != NULL) {
		if (unveil(prog, "x") == -1)
			err(1, "%s: unveil", prog);
		return;
	}

	if (getenv("PATH") == NULL)
		lerrx(1, "PATH is unset");
	if ((path = strdup(getenv("PATH"))) == NULL)
		lerr(1, NULL);
	save = path;
	while ((pp = strsep(&path, ":")) != NULL) {
		if (*pp == '\0')
			continue;
		if (asprintf(&cmd, "%s/%s", pp, prog) == -1)
			lerr(1, NULL);
		if (lstat(cmd, &st) == -1) {
			free(cmd);
			continue;
		}
		if (unveil(cmd, "x") == -1)
			lerr(1, "%s: unveil", cmd);
		free(cmd);
		break;
	}
	free(save);
}

int
main(int argc, char *argv[])
{
	extern char *__progname;
	struct listener	*l = NULL;
	struct passwd	*pw;
	struct stat	 sb;
	int		 c, fd;
	const char	*sock_user = WWW_USER;
	const char	*cgi_user = BGPLGD_USER;

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

	while ((c = getopt(argc, argv, "dp:S:s:U:u:V")) != -1) {
		switch (c) {
		case 'd':
			debug++;
			break;
		case 'p':
			bgpctlpath = optarg;
			break;
		case 'S':
			bgpctlsock = optarg;
			break;
		case 's':
			fcgi_socket = optarg;
			break;
		case 'U':
			sock_user = optarg;
			break;
		case 'V':
			fprintf(stderr, "OpenBGPD %s\n", BGPD_VERSION);
			return 0;
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

	ldebug("cgi_user: %s", cgi_user);
	pw = getpwnam(cgi_user);
	if (pw == NULL)
		lerrx(1, "no %s user", cgi_user);

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		lerr(1, "unable to revoke privs");

	unveil_command(bgpctlpath);

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
	signal_add(&slowcgi_proc.ev_sigchld, NULL);

	signal(SIGPIPE, SIG_IGN);

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

	memset(&sun, 0, sizeof(sun));
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
    int reserve, volatile int *counter)
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
	c->inflight_fds_accounted = 0;
	TAILQ_INIT(&c->response_head);

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
	struct request	*c = arg;
	int sig = SIGTERM;

	if (c->script_flags & SCRIPT_DONE)
		return;

	if (c->command_pid == 0) {
		c->command_status = SIGALRM;
		error_response(c, 408);
		return;
	}

	ldebug("timeout fired for pid %d", c->command_pid);

	if (c->timeout_fired)
		sig = SIGKILL;

	if (kill(c->command_pid, sig) == -1) {
		lwarn("kill child %d after timeout", c->command_pid);
		if (event_initialized(&c->script_ev)) {
			close(EVENT_FD(&c->script_ev));
			event_del(&c->script_ev);
		}
		if (event_initialized(&c->script_err_ev)) {
			close(EVENT_FD(&c->script_err_ev));
			event_del(&c->script_err_ev);
		}

		c->command_status = SIGALRM;
		c->script_flags = STDOUT_DONE | STDERR_DONE | SCRIPT_DONE;
		create_end_record(c);
	}

	if (c->timeout_fired++ == 0)
		evtimer_add(&c->tmo, &kill_timeout);
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
				if (c->command_pid == pid)
					break;
			if (c == NULL) {
				lwarnx("caught exit of unknown child %d", pid);
				continue;
			}

			if (WIFSIGNALED(status))
				c->command_status = WTERMSIG(status);
			else
				c->command_status = WEXITSTATUS(status);

			ldebug("pid %d exit %s%d", pid,
			    WIFSIGNALED(status) ? "signal " : "",
			    c->command_status);

			c->script_flags |= SCRIPT_DONE;

			if (c->script_flags == (STDOUT_DONE | STDERR_DONE |
			    SCRIPT_DONE))
				create_end_record(c);
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
		if (c->request_done)
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
		memmove(c->buf, c->buf + c->buf_pos, c->buf_len);
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

		if ((env_entry->key = malloc(name_len + 1)) == NULL) {
			lwarnx("cannot allocate env_entry->key");
			free(env_entry);
			return;
		}
		if ((env_entry->val = malloc(val_len + 1)) == NULL) {
			lwarnx("cannot allocate env_entry->val");
			free(env_entry->key);
			free(env_entry);
			return;
		}

		memcpy(env_entry->key, buf, name_len);
		buf += name_len;
		n -= name_len;
		env_entry->key[name_len] = '\0';
		memcpy(env_entry->val, buf, val_len);
		buf += val_len;
		n -= val_len;
		env_entry->val[val_len] = '\0';

		SLIST_INSERT_HEAD(&c->env, env_entry, entry);
		ldebug("env[%d], %s=%s", c->env_count, env_entry->key,
		    env_entry->val);
		c->env_count++;
	}
}

void
parse_stdin(uint8_t *buf, uint16_t n, struct request *c, uint16_t id)
{
	if (c->id != id) {
		lwarnx("unexpected id, ignoring");
		return;
	}

	if (n != 0)
		lwarnx("unexpected stdin input, ignoring");
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

char *
env_get(struct request *c, const char *key)
{
	struct env_val	*env;

	SLIST_FOREACH(env, &c->env, entry) {
		if (strcmp(env->key, key) == 0)
			return (env->val);
	}
	return (NULL);
}

static const char *
http_error(int *res)
{
	const struct http_error errors[] = HTTP_ERRORS;
	size_t i;

	for (i = 0; errors[i].error_code != 0; i++)
		if (errors[i].error_code == *res)
			return errors[i].error_name;

	/* unknown error - change to 500 */
	lwarnx("unknown http error %d", *res);
	*res = 500;
	return "Internal Server Error";
}

void
error_response(struct request *c, int res)
{
	const char *type = "text/html";
	const char *errstr = http_error(&res);
	char *buf;
	int len;

	lwarnx("HTTP status %d: %s", res, errstr);

	len = asprintf(&buf,
	    "Content-Type: %s\n"
	    "Status: %d\n"
	    "Cache-Control: no-cache\n"
	    "\n"
	    "<!DOCTYPE html>\n"
	    "<html>\n"
	    " <head>\n"
	    "  <meta http-equiv=\"Content-Type\" "
	    "content=\"%s; charset=utf-8\"/>\n"
	    "  <title>%d %s</title>\n"
	    " </head>\n"
	    " <body>\n"
	    "  <h1>%d %s</h1>\n"
	    "  <hr>\n"
	    "  <address>OpenBSD bgplgd</address>\n"
	    " </body>\n"
	    "</html>\n",
	    type, res, type, res, errstr, res, errstr);

	if (len == -1)
		lerr(1, NULL);

	create_data_record(c, FCGI_STDOUT, buf, len);
	free(buf);
	c->script_flags = (STDOUT_DONE | STDERR_DONE | SCRIPT_DONE);
	create_end_record(c);
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
	struct lg_ctx	 ctx = { 0 };
	int		 s_in[2], s_out[2], s_err[2], res;
	pid_t		 pid;

	res = prep_request(&ctx, env_get(c, "REQUEST_METHOD"),
	    env_get(c, "PATH_INFO"), env_get(c, "QUERY_STRING"));
	if (res != 0) {
		error_response(c, res);
		return;
	}

	if (pipe(s_in) == -1)
		lerr(1, "pipe");
	if (pipe(s_out) == -1)
		lerr(1, "pipe");
	if (pipe(s_err) == -1)
		lerr(1, "pipe");
	cgi_inflight--;
	c->inflight_fds_accounted = 1;

	switch (pid = fork()) {
	case -1:
		c->command_status = errno;

		lwarn("fork");

		close(s_in[0]);
		close(s_out[0]);
		close(s_err[0]);

		close(s_in[1]);
		close(s_out[1]);
		close(s_err[1]);

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

		bgpctl_call(&ctx);

		/* should not be reached */
		_exit(1);

	}

	ldebug("fork %d", pid);

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

	close(s_in[0]);	/* close stdin, bgpctl does not expect anything */

	c->command_pid = pid;
	event_set(&c->script_ev, s_out[0], EV_READ | EV_PERSIST,
	    script_std_in, c);
	event_add(&c->script_ev, NULL);
	event_set(&c->script_err_ev, s_err[0], EV_READ | EV_PERSIST,
	    script_err_in, c);
	event_add(&c->script_err_ev, NULL);
}

static void
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
		    SCRIPT_DONE))
			create_end_record(c);
		event_del(ev);
		close(fd);
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
create_data_record(struct request *c, uint8_t type, const void *buf, size_t len)
{
	struct fcgi_response		*resp;
	struct fcgi_record_header	*header;
	const char			*d = buf;
	size_t				 n;

	do {
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

		n = len > FCGI_CONTENT_SIZE ? FCGI_CONTENT_SIZE : len;
		memcpy(resp->data + sizeof(struct fcgi_record_header), d, n);

		header->content_len = htons(n);
		resp->data_pos = 0;
		resp->data_len = n + sizeof(struct fcgi_record_header);
		slowcgi_add_response(c, resp);

		len -= n;
		d += n;
	} while (len > 0);
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
	end_request->app_status = htonl(c->command_status);
	end_request->protocol_status = FCGI_REQUEST_COMPLETE;
	end_request->reserved[0] = 0;
	end_request->reserved[1] = 0;
	end_request->reserved[2] = 0;
	resp->data_pos = 0;
	resp->data_len = sizeof(struct fcgi_end_request_body) +
	    sizeof(struct fcgi_record_header);
	slowcgi_add_response(c, resp);
	c->request_done = 1;
}

void
cleanup_request(struct request *c)
{
	struct fcgi_response	*resp;
	struct env_val		*env_entry;

	evtimer_del(&c->tmo);
	if (event_initialized(&c->ev))
		event_del(&c->ev);
	if (event_initialized(&c->resp_ev))
		event_del(&c->resp_ev);
	if (event_initialized(&c->script_ev)) {
		close(EVENT_FD(&c->script_ev));
		event_del(&c->script_ev);
	}
	if (event_initialized(&c->script_err_ev)) {
		close(EVENT_FD(&c->script_err_ev));
		event_del(&c->script_err_ev);
	}

	close(c->fd);
	while (!SLIST_EMPTY(&c->env)) {
		env_entry = SLIST_FIRST(&c->env);
		SLIST_REMOVE_HEAD(&c->env, entry);
		free(env_entry->key);
		free(env_entry->val);
		free(env_entry);
	}

	while ((resp = TAILQ_FIRST(&c->response_head))) {
		TAILQ_REMOVE(&c->response_head, resp, entry);
		free(resp);
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
	va_list ap;

	va_start(ap, fmt);
	vsyslog(LOG_DEBUG, fmt, ap);
	va_end(ap);
}
