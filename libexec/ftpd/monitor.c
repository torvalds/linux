/*	$OpenBSD: monitor.c,v 1.32 2025/05/08 15:22:49 deraadt Exp $	*/

/*
 * Copyright (c) 2004 Moritz Jodeit <moritz@openbsd.org>
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
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "monitor.h"
#include "extern.h"

enum monitor_command {
	CMD_USER,
	CMD_PASS,
	CMD_SOCKET,
	CMD_BIND
};

enum monitor_state {
	PREAUTH,
	POSTAUTH
};

extern char	remotehost[];
extern char	ttyline[20];
extern int	debug;

extern void	set_slave_signals(void);

int	fd_monitor = -1;
int	fd_slave = -1;
int	nullfd;
pid_t	slave_pid = -1;
enum monitor_state	state = PREAUTH;

void	send_data(int, void *, size_t);
void	recv_data(int, void *, size_t);
void	handle_cmds(void);
void	set_monitor_signals(void);
void	sig_pass_to_slave(int);
void	sig_chld(int);
void	fatalx(char *, ...);
void	debugmsg(char *, ...);

/*
 * Send data over a socket and exit if something fails.
 */
void
send_data(int sock, void *buf, size_t len)
{
	ssize_t n;
	size_t pos = 0;
	char *ptr = buf;

	while (len > pos) {
		switch (n = write(sock, ptr + pos, len - pos)) {
		case 0:
			kill_slave("write failure");
			_exit(0);
			/* NOTREACHED */
		case -1:
			if (errno != EINTR && errno != EAGAIN)
				fatalx("send_data: %m");
			break;
		default:
			pos += n;
		}
	}
}

/*
 * Receive data from socket and exit if something fails.
 */
void
recv_data(int sock, void *buf, size_t len)
{
	ssize_t n;
	size_t pos = 0;
	char *ptr = buf;

	while (len > pos) {
		switch (n = read(sock, ptr + pos, len - pos)) {
		case 0:
			kill_slave(NULL);
			_exit(0);
			/* NOTREACHED */
		case -1:
			if (errno != EINTR && errno != EAGAIN)
				fatalx("recv_data: %m");
			break;
		default:
			pos += n;
		}
	}
}

void
set_monitor_signals(void)
{
	struct sigaction act;
	int i;

	sigfillset(&act.sa_mask);
	act.sa_flags = SA_RESTART;

	act.sa_handler = SIG_DFL;
	for (i = 1; i < _NSIG; i++)
		sigaction(i, &act, NULL);

	act.sa_handler = sig_chld;
	sigaction(SIGCHLD, &act, NULL);

	act.sa_handler = sig_pass_to_slave;
	sigaction(SIGHUP, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGQUIT, &act, NULL);
	sigaction(SIGTERM, &act, NULL);
}

/*
 * Creates the privileged monitor process. It returns twice.
 * It returns 1 for the unprivileged slave process and 0 for the
 * user-privileged slave process after successful authentication.
 */
int
monitor_init(void)
{
	struct passwd *pw;
	int pair[2];

	if (socketpair(AF_LOCAL, SOCK_STREAM, PF_UNSPEC, pair) == -1)
		fatalx("socketpair failed");

	fd_monitor = pair[0];
	fd_slave = pair[1];

	set_monitor_signals();

	slave_pid = fork();
	if (slave_pid == -1)
		fatalx("fork of unprivileged slave failed");
	if (slave_pid == 0) {
		/* Unprivileged slave */
		set_slave_signals();

		if ((pw = getpwnam(FTPD_PRIVSEP_USER)) == NULL)
			fatalx("privilege separation user %s not found",
			    FTPD_PRIVSEP_USER);

		if (chroot(pw->pw_dir) == -1)
			fatalx("chroot %s: %m", pw->pw_dir);
		if (chdir("/") == -1)
			fatalx("chdir /: %m");

		if (setgroups(1, &pw->pw_gid) == -1)
			fatalx("setgroups: %m");
		if (setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) == -1)
			fatalx("setresgid failed");
		if (setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) == -1)
			fatalx("setresuid failed");

		close(fd_slave);
		return (1);
	}

	setproctitle("%s: [priv pre-auth]", remotehost);

	handle_cmds();

	/* User-privileged slave */
	return (0);
}

/*
 * Creates the user-privileged slave process. It is called
 * from the privileged monitor process and returns twice. It returns 0
 * for the user-privileged slave process and 1 for the monitor process.
 */
int
monitor_post_auth(void)
{
	slave_pid = fork();
	if (slave_pid == -1)
		fatalx("fork of user-privileged slave failed");

	snprintf(ttyline, sizeof(ttyline), "ftp%ld",
	    slave_pid == 0 ? (long)getpid() : (long)slave_pid);

	if (slave_pid == 0) {
		/* User privileged slave */
		close(fd_slave);
		set_slave_signals();
		return (0);
	}

	/* We have to keep stdout open, because reply() needs it. */
	if ((nullfd = open(_PATH_DEVNULL, O_RDWR)) == -1)
		fatalx("cannot open %s: %m", _PATH_DEVNULL);
	dup2(nullfd, STDIN_FILENO);
	dup2(nullfd, STDERR_FILENO);
	close(nullfd);
	close(fd_monitor);

	return (1);
}

/*
 * Handles commands received from the slave process. It will not return
 * except in one situation: After successful authentication it will
 * return as the user-privileged slave process.
 */
void
handle_cmds(void)
{
	enum monitor_command cmd;
	enum auth_ret auth;
	int err, s, slavequit, serrno, domain;
	pid_t preauth_slave_pid;
	size_t len;
	union sockunion sa;
	socklen_t salen;
	char *name, *pw;

	for (;;) {
		recv_data(fd_slave, &cmd, sizeof(cmd));

		switch (cmd) {
		case CMD_USER:
			debugmsg("CMD_USER received");

			recv_data(fd_slave, &len, sizeof(len));
			if (len == SIZE_MAX)
				fatalx("monitor received invalid user length");
			if ((name = malloc(len + 1)) == NULL)
				fatalx("malloc: %m");
			if (len > 0)
				recv_data(fd_slave, name, len);
			name[len] = '\0';

			user(name);
			free(name);
			break;
		case CMD_PASS:
			debugmsg("CMD_PASS received");

			recv_data(fd_slave, &len, sizeof(len));
			if (len == SIZE_MAX)
				fatalx("monitor received invalid pass length");
			if ((pw = malloc(len + 1)) == NULL)
				fatalx("malloc: %m");
			if (len > 0)
				recv_data(fd_slave, pw, len);
			pw[len] = '\0';

			preauth_slave_pid = slave_pid;

			auth = pass(pw);
			freezero(pw, len);

			switch (auth) {
			case AUTH_FAILED:
				/* Authentication failure */
				debugmsg("authentication failed");
				slavequit = 0;
				send_data(fd_slave, &slavequit,
				    sizeof(slavequit));
				break;
			case AUTH_SLAVE:
				if (pledge("stdio rpath wpath cpath inet recvfd"
				    " sendfd proc tty getpw", NULL) == -1)
					fatalx("pledge");
				/* User-privileged slave */
				debugmsg("user-privileged slave started");
				return;
				/* NOTREACHED */
			case AUTH_MONITOR:
				if (pledge("stdio inet sendfd recvfd proc",
				    NULL) == -1)
					fatalx("pledge");
				/* Post-auth monitor */
				debugmsg("monitor went into post-auth phase");
				state = POSTAUTH;
				setproctitle("%s: [priv post-auth]",
				    remotehost);
				slavequit = 1;

				send_data(fd_slave, &slavequit,
				    sizeof(slavequit));

				while (waitpid(preauth_slave_pid, NULL, 0) == -1 &&
				    errno == EINTR)
					;
				break;
			default:
				fatalx("bad return value from pass()");
				/* NOTREACHED */
			}
			break;
		case CMD_SOCKET:
			debugmsg("CMD_SOCKET received");

			if (state != POSTAUTH)
				fatalx("CMD_SOCKET received in invalid state");

			recv_data(fd_slave, &domain, sizeof(domain));
			if (domain != AF_INET && domain != AF_INET6)
				fatalx("monitor received invalid addr family");

			s = socket(domain, SOCK_STREAM, 0);
			serrno = errno;

			send_fd(fd_slave, s);
			if (s == -1)
				send_data(fd_slave, &serrno, sizeof(serrno));
			else
				close(s);
			break;
		case CMD_BIND:
			debugmsg("CMD_BIND received");

			if (state != POSTAUTH)
				fatalx("CMD_BIND received in invalid state");

			s = recv_fd(fd_slave);

			recv_data(fd_slave, &salen, sizeof(salen));
			if (salen == 0 || salen > sizeof(sa))
				fatalx("monitor received invalid sockaddr len");

			bzero(&sa, sizeof(sa));
			recv_data(fd_slave, &sa, salen);

			if (sa.su_si.si_len != salen)
				fatalx("monitor received invalid sockaddr len");

			if (sa.su_si.si_family != AF_INET &&
			    sa.su_si.si_family != AF_INET6)
				fatalx("monitor received invalid addr family");

			err = bind(s, (struct sockaddr *)&sa, salen);
			serrno = errno;

			if (s >= 0)
				close(s);

			send_data(fd_slave, &err, sizeof(err));
			if (err == -1)
				send_data(fd_slave, &serrno, sizeof(serrno));
			break;
		default:
			fatalx("monitor received unknown command %d", cmd);
			/* NOTREACHED */
		}
	}
}

void
sig_pass_to_slave(int signo)
{
	int olderrno = errno;

	if (slave_pid > 0)
		kill(slave_pid, signo);

	errno = olderrno;
}

void
sig_chld(int signo)
{
	pid_t pid;
	int stat, olderrno = errno;

	do {
		pid = waitpid(slave_pid, &stat, WNOHANG);
		if (pid > 0)
			_exit(0);
	} while (pid == -1 && errno == EINTR);

	errno = olderrno;
}

void
kill_slave(char *reason)
{
	if (slave_pid > 0) {
		if (reason)
			syslog(LOG_NOTICE, "kill slave %d: %s",
			    slave_pid, reason);
		kill(slave_pid, SIGQUIT);
	}
}

void
fatalx(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(LOG_ERR, fmt, ap);
	va_end(ap);

	kill_slave("fatal error");

	_exit(0);
}

void
debugmsg(char *fmt, ...)
{
	va_list ap;

	if (debug) {
		va_start(ap, fmt);
		vsyslog(LOG_DEBUG, fmt, ap);
		va_end(ap);
	}
}

void
monitor_user(char *name)
{
	enum monitor_command cmd;
	size_t len;

	cmd = CMD_USER;
	send_data(fd_monitor, &cmd, sizeof(cmd));

	len = strlen(name);
	send_data(fd_monitor, &len, sizeof(len));
	if (len > 0)
		send_data(fd_monitor, name, len);
}

int
monitor_pass(char *pass)
{
	enum monitor_command cmd;
	int quitnow;
	size_t len;

	cmd = CMD_PASS;
	send_data(fd_monitor, &cmd, sizeof(cmd));

	len = strlen(pass);
	send_data(fd_monitor, &len, sizeof(len));
	if (len > 0)
		send_data(fd_monitor, pass, len);

	recv_data(fd_monitor, &quitnow, sizeof(quitnow));

	return (quitnow);
}

int
monitor_socket(int domain)
{
	enum monitor_command cmd;
	int s, serrno;

	cmd = CMD_SOCKET;
	send_data(fd_monitor, &cmd, sizeof(cmd));
	send_data(fd_monitor, &domain, sizeof(domain));

	s = recv_fd(fd_monitor);
	if (s == -1) {
		recv_data(fd_monitor, &serrno, sizeof(serrno));
		errno = serrno;
	}

	return (s);
}

int
monitor_bind(int s, struct sockaddr *name, socklen_t namelen)
{
	enum monitor_command cmd;
	int ret, serrno;

	cmd = CMD_BIND;
	send_data(fd_monitor, &cmd, sizeof(cmd));

	send_fd(fd_monitor, s);
	send_data(fd_monitor, &namelen, sizeof(namelen));
	send_data(fd_monitor, name, namelen);

	recv_data(fd_monitor, &ret, sizeof(ret));
	if (ret == -1) {
		recv_data(fd_monitor, &serrno, sizeof(serrno));
		errno = serrno;
	}

	return (ret);
}
