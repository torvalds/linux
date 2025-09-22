/* $OpenBSD: monitor.c,v 1.83 2023/02/08 08:03:11 tb Exp $	 */

/*
 * Copyright (c) 2003 Håkan Olsson.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include <regex.h>
#include <keynote.h>

#include "conf.h"
#include "log.h"
#include "monitor.h"
#include "policy.h"
#include "ui.h"
#include "util.h"
#include "pf_key_v2.h"

struct monitor_state {
	pid_t           pid;
	int             s;
	char            root[PATH_MAX];
} m_state;

extern char *pid_file;

extern void	set_slave_signals(void);

/* Private functions.  */
static void	must_read(void *, size_t);
static void	must_write(const void *, size_t);

static void	m_priv_getfd(void);
static void	m_priv_setsockopt(void);
static void	m_priv_req_readdir(void);
static void	m_priv_bind(void);
static void	m_priv_pfkey_open(void);
static int	m_priv_local_sanitize_path(const char *, size_t, int);
static int	m_priv_check_sockopt(int, int);
static int	m_priv_check_bind(const struct sockaddr *, socklen_t);

static void	set_monitor_signals(void);
static void	sig_pass_to_chld(int);

/*
 * Public functions, unprivileged.
 */

/* Setup monitor context, fork, drop child privs.  */
pid_t
monitor_init(int debug)
{
	struct passwd  *pw;
	int             p[2];

	bzero(&m_state, sizeof m_state);

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, p) != 0)
		log_fatal("monitor_init: socketpair() failed");

	pw = getpwnam(ISAKMPD_PRIVSEP_USER);
	if (pw == NULL)
		log_fatalx("monitor_init: getpwnam(\"%s\") failed",
		    ISAKMPD_PRIVSEP_USER);
	strlcpy(m_state.root, pw->pw_dir, sizeof m_state.root);

	set_monitor_signals();
	m_state.pid = fork();

	if (m_state.pid == -1)
		log_fatal("monitor_init: fork of unprivileged child failed");
	if (m_state.pid == 0) {
		/* The child process drops privileges. */
		set_slave_signals();

		if (chroot(pw->pw_dir) != 0 || chdir("/") != 0)
			log_fatal("monitor_init: chroot failed");

		if (setgroups(1, &pw->pw_gid) == -1 ||
		    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
		    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
			log_fatal("monitor_init: can't drop privileges");

		m_state.s = p[0];
		close(p[1]);

		LOG_DBG((LOG_MISC, 10,
		    "monitor_init: privileges dropped for child process"));
	} else {
		/* Privileged monitor. */
		setproctitle("monitor [priv]");

		m_state.s = p[1];
		close(p[0]);
	}

	/* With "-dd", stop and wait here. For gdb "attach" etc.  */
	if (debug > 1) {
		log_print("monitor_init: stopped %s PID %d fd %d%s",
		    m_state.pid ? "priv" : "child", getpid(), m_state.s,
		    m_state.pid ? ", waiting for SIGCONT" : "");
		kill(getpid(), SIGSTOP);	/* Wait here for SIGCONT.  */
		if (m_state.pid)
			kill(m_state.pid, SIGCONT); /* Continue child.  */
	}

	return m_state.pid;
}

void
monitor_exit(int code)
{
	int status = 0, gotstatus = 0;
	pid_t pid;

	if (m_state.pid != 0) {
		/* When called from the monitor, kill slave and wait for it  */
		kill(m_state.pid, SIGTERM);

		do {
			pid = waitpid(m_state.pid, &status, 0);
		} while (pid == -1 && errno == EINTR);
		if (pid != -1)
			gotstatus = 1;

		/* Remove FIFO and pid files.  */
		unlink(ui_fifo);
		unlink(pid_file);
	}

	close(m_state.s);
	if (code == 0 && gotstatus)
		exit(WIFEXITED(status)? WEXITSTATUS(status) : 1);
	else
		exit(code);
}

int
monitor_pf_key_v2_open(void)
{
	int	err, cmd;

	cmd = MONITOR_PFKEY_OPEN;
	must_write(&cmd, sizeof cmd);

	must_read(&err, sizeof err);
	if (err < 0) {
		log_error("monitor_pf_key_v2_open: parent could not create "
		    "PF_KEY socket");
		return -1;
	}
	pf_key_v2_socket = mm_receive_fd(m_state.s);
	if (pf_key_v2_socket < 0) {
		log_error("monitor_pf_key_v2_open: mm_receive_fd() failed");
		return -1;
	}

	return pf_key_v2_socket;
}

int
monitor_open(const char *path, int flags, mode_t mode)
{
	size_t	len;
	int	fd, err, cmd;
	char	pathreal[PATH_MAX];

	if (path[0] == '/')
		strlcpy(pathreal, path, sizeof pathreal);
	else
		snprintf(pathreal, sizeof pathreal, "%s/%s", m_state.root,
		    path);

	cmd = MONITOR_GET_FD;
	must_write(&cmd, sizeof cmd);

	len = strlen(pathreal);
	must_write(&len, sizeof len);
	must_write(&pathreal, len);

	must_write(&flags, sizeof flags);
	must_write(&mode, sizeof mode);

	must_read(&err, sizeof err);
	if (err != 0) {
		errno = err;
		return -1;
	}

	fd = mm_receive_fd(m_state.s);
	if (fd < 0) {
		log_error("monitor_open: mm_receive_fd () failed");
		return -1;
	}

	return fd;
}

FILE *
monitor_fopen(const char *path, const char *mode)
{
	FILE	*fp;
	int	 fd, flags = 0, saved_errno;
	mode_t	 mask, cur_umask;

	/* Only the child process is supposed to run this.  */
	if (m_state.pid)
		log_fatal("[priv] bad call to monitor_fopen");

	switch (mode[0]) {
	case 'r':
		flags = (mode[1] == '+' ? O_RDWR : O_RDONLY);
		break;
	case 'w':
		flags = (mode[1] == '+' ? O_RDWR : O_WRONLY) | O_CREAT |
		    O_TRUNC;
		break;
	case 'a':
		flags = (mode[1] == '+' ? O_RDWR : O_WRONLY) | O_CREAT |
		    O_APPEND;
		break;
	default:
		log_fatal("monitor_fopen: bad call");
	}

	cur_umask = umask(0);
	(void)umask(cur_umask);
	mask = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	mask &= ~cur_umask;

	fd = monitor_open(path, flags, mask);
	if (fd < 0)
		return NULL;

	/* Got the fd, attach a FILE * to it.  */
	fp = fdopen(fd, mode);
	if (!fp) {
		log_error("monitor_fopen: fdopen() failed");
		saved_errno = errno;
		close(fd);
		errno = saved_errno;
		return NULL;
	}
	return fp;
}

int
monitor_stat(const char *path, struct stat *sb)
{
	int	fd, r, saved_errno;

	/* O_NONBLOCK is needed for stat'ing fifos. */
	fd = monitor_open(path, O_RDONLY | O_NONBLOCK, 0);
	if (fd < 0)
		return -1;

	r = fstat(fd, sb);
	saved_errno = errno;
	close(fd);
	errno = saved_errno;
	return r;
}

int
monitor_setsockopt(int s, int level, int optname, const void *optval,
    socklen_t optlen)
{
	int	ret, err, cmd;

	cmd = MONITOR_SETSOCKOPT;
	must_write(&cmd, sizeof cmd);
	if (mm_send_fd(m_state.s, s)) {
		log_print("monitor_setsockopt: read/write error");
		return -1;
	}

	must_write(&level, sizeof level);
	must_write(&optname, sizeof optname);
	must_write(&optlen, sizeof optlen);
	must_write(optval, optlen);

	must_read(&err, sizeof err);
	must_read(&ret, sizeof ret);
	if (err != 0)
		errno = err;
	return ret;
}

int
monitor_bind(int s, const struct sockaddr *name, socklen_t namelen)
{
	int	ret, err, cmd;

	cmd = MONITOR_BIND;
	must_write(&cmd, sizeof cmd);
	if (mm_send_fd(m_state.s, s)) {
		log_print("monitor_bind: read/write error");
		return -1;
	}

	must_write(&namelen, sizeof namelen);
	must_write(name, namelen);

	must_read(&err, sizeof err);
	must_read(&ret, sizeof ret);
	if (err != 0)
		errno = err;
	return ret;
}

int
monitor_req_readdir(const char *filename)
{
	int cmd, err;
	size_t len;

	cmd = MONITOR_REQ_READDIR;
	must_write(&cmd, sizeof cmd);

	len = strlen(filename);
	must_write(&len, sizeof len);
	must_write(filename, len);

	must_read(&err, sizeof err);
	if (err == -1)
		must_read(&errno, sizeof errno);

	return err;
}

int
monitor_readdir(char *file, size_t size)
{
	int fd;
	size_t len;

	must_read(&len, sizeof len);
	if (len == 0)
		return -1;
	if (len >= size)
		log_fatal("monitor_readdir: received bad length from monitor");
	must_read(file, len);
	file[len] = '\0';
	fd = mm_receive_fd(m_state.s);
	return fd;
}

void
monitor_init_done(void)
{
	int	cmd;

	cmd = MONITOR_INIT_DONE;
	must_write(&cmd, sizeof cmd);
}

/*
 * Start of code running with privileges (the monitor process).
 */

static void
set_monitor_signals(void)
{
	int n;

	for (n = 1; n < _NSIG; n++)
		signal(n, SIG_DFL);

	/* Forward some signals to the child. */
	signal(SIGTERM, sig_pass_to_chld);
	signal(SIGHUP, sig_pass_to_chld);
	signal(SIGUSR1, sig_pass_to_chld);
}

static void
sig_pass_to_chld(int sig)
{
	int	oerrno = errno;

	if (m_state.pid > 0)
		kill(m_state.pid, sig);
	errno = oerrno;
}

/* This function is where the privileged process waits(loops) indefinitely.  */
void
monitor_loop(int debug)
{
	int	 msgcode;

	if (!debug)
		log_to(0);

	for (;;) {
		must_read(&msgcode, sizeof msgcode);

		switch (msgcode) {
		case MONITOR_GET_FD:
			m_priv_getfd();
			break;

		case MONITOR_PFKEY_OPEN:
			LOG_DBG((LOG_MISC, 80,
			    "monitor_loop: MONITOR_PFKEY_OPEN"));
			m_priv_pfkey_open();
			break;

		case MONITOR_SETSOCKOPT:
			LOG_DBG((LOG_MISC, 80,
			    "monitor_loop: MONITOR_SETSOCKOPT"));
			m_priv_setsockopt();
			break;

		case MONITOR_BIND:
			LOG_DBG((LOG_MISC, 80,
			    "monitor_loop: MONITOR_BIND"));
			m_priv_bind();
			break;

		case MONITOR_REQ_READDIR:
			LOG_DBG((LOG_MISC, 80,
			    "monitor_loop: MONITOR_REQ_READDIR"));
			m_priv_req_readdir();
			break;

		case MONITOR_INIT_DONE:
			LOG_DBG((LOG_MISC, 80,
			    "monitor_loop: MONITOR_INIT_DONE"));
			break;

		case MONITOR_SHUTDOWN:
			LOG_DBG((LOG_MISC, 80,
			    "monitor_loop: MONITOR_SHUTDOWN"));
			break;

		default:
			log_print("monitor_loop: got unknown code %d",
			    msgcode);
		}
	}

	exit(0);
}


/* Privileged: called by monitor_loop.  */
static void
m_priv_pfkey_open(void)
{
	int	fd, err = 0;

	fd = pf_key_v2_open();
	if (fd < 0)
		err = -1;

	must_write(&err, sizeof err);

	if (fd > 0 && mm_send_fd(m_state.s, fd)) {
		log_error("m_priv_pfkey_open: read/write operation failed");
		close(fd);
		return;
	}
	close(fd);
}

/* Privileged: called by monitor_loop.  */
static void
m_priv_getfd(void)
{
	char	path[PATH_MAX];
	size_t	len;
	int	v, flags, ret;
	int	err = 0;
	mode_t	mode;

	must_read(&len, sizeof len);
	if (len == 0 || len >= sizeof path)
		log_fatal("m_priv_getfd: invalid pathname length");

	must_read(path, len);
	path[len] = '\0';
	if (strlen(path) != len)
		log_fatal("m_priv_getfd: invalid pathname");

	must_read(&flags, sizeof flags);
	must_read(&mode, sizeof mode);

	if ((ret = m_priv_local_sanitize_path(path, sizeof path, flags))
	    != 0) {
		if (errno != ENOENT)
			log_print("m_priv_getfd: illegal path \"%s\"", path);
		err = errno;
		v = -1;
	} else {
		if ((v = open(path, flags, mode)) == -1)
			err = errno;
	}

	must_write(&err, sizeof err);

	if (v != -1) {
		if (mm_send_fd(m_state.s, v) == -1)
			log_error("m_priv_getfd: sending fd failed");
		close(v);
	}
}

/* Privileged: called by monitor_loop.  */
static void
m_priv_setsockopt(void)
{
	int		 sock, level, optname, v;
	int		 err = 0;
	char		*optval = 0;
	socklen_t	 optlen;

	sock = mm_receive_fd(m_state.s);
	if (sock < 0) {
		log_print("m_priv_setsockopt: read/write error");
		return;
	}

	must_read(&level, sizeof level);
	must_read(&optname, sizeof optname);
	must_read(&optlen, sizeof optlen);

	optval = malloc(optlen);
	if (!optval) {
		log_print("m_priv_setsockopt: malloc failed");
		close(sock);
		return;
	}

	must_read(optval, optlen);

	if (m_priv_check_sockopt(level, optname) != 0) {
		err = EACCES;
		v = -1;
	} else {
		v = setsockopt(sock, level, optname, optval, optlen);
		if (v < 0)
			err = errno;
	}

	close(sock);
	sock = -1;

	must_write(&err, sizeof err);
	must_write(&v, sizeof v);

	free(optval);
	return;
}

/* Privileged: called by monitor_loop.  */
static void
m_priv_bind(void)
{
	int		 sock, v, err = 0;
	struct sockaddr *name = 0;
	socklen_t        namelen;

	sock = mm_receive_fd(m_state.s);
	if (sock < 0) {
		log_print("m_priv_bind: read/write error");
		return;
	}

	must_read(&namelen, sizeof namelen);
	name = malloc(namelen);
	if (!name) {
		log_print("m_priv_bind: malloc failed");
		close(sock);
		return;
	}
	must_read((char *)name, namelen);

	if (m_priv_check_bind(name, namelen) != 0) {
		err = EACCES;
		v = -1;
	} else {
		v = bind(sock, name, namelen);
		if (v == -1) {
			log_error("m_priv_bind: bind(%d,%p,%d) returned %d",
			    sock, name, namelen, v);
			err = errno;
		}
	}

	close(sock);
	sock = -1;

	must_write(&err, sizeof err);
	must_write(&v, sizeof v);

	free(name);
	return;
}

/*
 * Help functions, used by both privileged and unprivileged code
 */

/*
 * Read data with the assertion that it all must come through, or else abort
 * the process.  Based on atomicio() from openssh.
 */
static void
must_read(void *buf, size_t n)
{
        char *s = buf;
	size_t pos = 0;
        ssize_t res;

        while (n > pos) {
                res = read(m_state.s, s + pos, n - pos);
                switch (res) {
                case -1:
                        if (errno == EINTR || errno == EAGAIN)
                                continue;
                case 0:
			monitor_exit(0);
                default:
                        pos += res;
                }
        }
}

/*
 * Write data with the assertion that it all has to be written, or else abort
 * the process.  Based on atomicio() from openssh.
 */
static void
must_write(const void *buf, size_t n)
{
        const char *s = buf;
	size_t pos = 0;
	ssize_t res;

        while (n > pos) {
                res = write(m_state.s, s + pos, n - pos);
                switch (res) {
                case -1:
                        if (errno == EINTR || errno == EAGAIN)
                                continue;
                case 0:
			monitor_exit(0);
                default:
                        pos += res;
                }
        }
}

/* Check that path/mode is permitted.  */
static int
m_priv_local_sanitize_path(const char *path, size_t pmax, int flags)
{
	char new_path[PATH_MAX], var_run[PATH_MAX], *enddir;

	/*
	 * We only permit paths starting with
	 *  /etc/isakmpd/	(read only)
	 *  /var/run/		(rw)
	 */

	if (realpath(path, new_path) == NULL) {
		if (errno != ENOENT)
			return 1;
		/*
		 * It is ok if the directory exists,
		 * but the file should be created.
		 */
		if (strlcpy(new_path, path, sizeof(new_path)) >=
		    sizeof(new_path))
			return 1;
		enddir = strrchr(new_path, '/');
		if (enddir == NULL || enddir[1] == '\0')
			return 1;
		enddir[1] = '\0';
		if (realpath(new_path, new_path) == NULL) {
			errno = ENOENT;
			return 1;
		}
		enddir = strrchr(path, '/');
		strlcat(new_path, enddir, sizeof(new_path));
	}

	if (realpath("/var/run/", var_run) == NULL)
		return 1;
	strlcat(var_run, "/", sizeof(var_run));

	if (strncmp(var_run, new_path, strlen(var_run)) == 0)
		return 0;

	if (strncmp(ISAKMPD_ROOT, new_path, strlen(ISAKMPD_ROOT)) == 0 &&
	    (flags & O_ACCMODE) == O_RDONLY)
		return 0;

	errno = EACCES;
	return 1;
}

/* Check setsockopt */
static int
m_priv_check_sockopt(int level, int name)
{
	switch (level) {
		/* These are allowed */
		case SOL_SOCKET:
		case IPPROTO_IP:
		case IPPROTO_IPV6:
		break;

	default:
		log_print("m_priv_check_sockopt: Illegal level %d", level);
		return 1;
	}

	switch (name) {
		/* These are allowed */
	case SO_REUSEPORT:
	case SO_REUSEADDR:
	case IP_AUTH_LEVEL:
	case IP_ESP_TRANS_LEVEL:
	case IP_ESP_NETWORK_LEVEL:
	case IP_IPCOMP_LEVEL:
	case IPV6_AUTH_LEVEL:
	case IPV6_ESP_TRANS_LEVEL:
	case IPV6_ESP_NETWORK_LEVEL:
	case IPV6_IPCOMP_LEVEL:
		break;

	default:
		log_print("m_priv_check_sockopt: Illegal option name %d",
		    name);
		return 1;
	}

	return 0;
}

/* Check bind */
static int
m_priv_check_bind(const struct sockaddr *sa, socklen_t salen)
{
	in_port_t       port;

	if (sa == NULL) {
		log_print("NULL address");
		return 1;
	}
	if (SA_LEN(sa) != salen) {
		log_print("Length mismatch: %lu %lu", (unsigned long)sa->sa_len,
		    (unsigned long)salen);
		return 1;
	}
	switch (sa->sa_family) {
	case AF_INET:
		if (salen != sizeof(struct sockaddr_in)) {
			log_print("Invalid inet address length");
			return 1;
		}
		port = ((const struct sockaddr_in *)sa)->sin_port;
		break;
	case AF_INET6:
		if (salen != sizeof(struct sockaddr_in6)) {
			log_print("Invalid inet6 address length");
			return 1;
		}
		port = ((const struct sockaddr_in6 *)sa)->sin6_port;
		break;
	default:
		log_print("Unknown address family");
		return 1;
	}

	port = ntohs(port);

	if (port != ISAKMP_PORT_DEFAULT && port < 1024) {
		log_print("Disallowed port %u", port);
		return 1;
	}
	return 0;
}

static void
m_priv_req_readdir(void)
{
	size_t len;
	char path[PATH_MAX];
	DIR *dp;
	struct dirent *file;
	struct stat sb;
	int off, size, fd, ret, serrno;

	must_read(&len, sizeof len);
	if (len == 0 || len >= sizeof path)
		log_fatal("m_priv_req_readdir: invalid pathname length");
	must_read(path, len);
	path[len] = '\0';
	if (strlen(path) != len)
		log_fatal("m_priv_req_readdir: invalid pathname");

	off = strlen(path);
	size = sizeof path - off;

	if ((dp = opendir(path)) == NULL) {
		serrno = errno;
		ret = -1;
		must_write(&ret, sizeof ret);
		must_write(&serrno, sizeof serrno);
		return;
	}

	/* report opendir() success */
	ret = 0;
	must_write(&ret, sizeof ret);

	while ((file = readdir(dp)) != NULL) {
		strlcpy(path + off, file->d_name, size);

		if (m_priv_local_sanitize_path(path, sizeof path, O_RDONLY)
		    != 0)
			continue;
		fd = open(path, O_RDONLY);
		if (fd == -1) {
			log_error("m_priv_req_readdir: open "
			    "(\"%s\", O_RDONLY, 0) failed", path);
			continue;
		}
		if ((fstat(fd, &sb) == -1) ||
		    !(S_ISREG(sb.st_mode) || S_ISLNK(sb.st_mode))) {
			close(fd);
			continue;
		}

		len = strlen(path);
		must_write(&len, sizeof len);
		must_write(path, len);

		mm_send_fd(m_state.s, fd);
		close(fd);
	}
	closedir(dp);

	len = 0;
	must_write(&len, sizeof len);
}
