/*	$OpenBSD: monitor.c,v 1.24 2024/11/21 13:42:49 claudio Exp $	*/

/*
 * Copyright (c) 2005 Håkan Olsson.  All rights reserved.
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
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/queue.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <net/pfkeyv2.h>

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <imsg.h>

#include "types.h"	/* iked imsg types */

#include "monitor.h"
#include "sasyncd.h"

struct m_state {
	pid_t	pid;
	int	s;
} m_state;

volatile sig_atomic_t		sigchld = 0;

static void	got_sigchld(int);
static void	sig_to_child(int);
static void	m_priv_pfkey_snap(int);
static int	m_priv_control_activate(void);
static int	m_priv_control_passivate(void);
static ssize_t	m_write(int, void *, size_t);
static ssize_t	m_read(int, void *, size_t);

pid_t
monitor_init(void)
{
	struct passwd	*pw = getpwnam(SASYNCD_USER);
	extern char	*__progname;
	char		root[PATH_MAX];
	int		p[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, p) != 0) {
		log_err("%s: socketpair failed - %s", __progname,
		    strerror(errno));
		exit(1);
	}

	if (!pw) {
		log_err("%s: getpwnam(\"%s\") failed", __progname,
		    SASYNCD_USER);
		exit(1);
	}
	strlcpy(root, pw->pw_dir, sizeof root);
	endpwent();

	signal(SIGCHLD, got_sigchld);
	signal(SIGTERM, sig_to_child);
	signal(SIGHUP, sig_to_child);
	signal(SIGINT, sig_to_child);

	m_state.pid = fork();

	if (m_state.pid == -1) {
		log_err("%s: fork failed - %s", __progname, strerror(errno));
		exit(1);
	} else if (m_state.pid == 0) {
		/* Child */
		m_state.s = p[0];
		close(p[1]);

		if (chroot(pw->pw_dir) != 0 || chdir("/") != 0) {
			log_err("%s: chroot failed", __progname);
			exit(1);
		}

		if (setgroups(1, &pw->pw_gid) ||
		    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
		    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid)) {
			log_err("%s: failed to drop privileges", __progname);
			exit(1);
		}
	} else {
		/* Parent */
		setproctitle("[priv]");
		m_state.s = p[1];
		close(p[0]);
	}
	return m_state.pid;
}

static void
got_sigchld(int s)
{
	sigchld = 1;
}

static void
sig_to_child(int s)
{
	if (m_state.pid != -1)
		kill(m_state.pid, s);
}

static void
monitor_drain_input(void)
{
	int		one = 1;
	u_int8_t	tmp;

	ioctl(m_state.s, FIONBIO, &one);
	while (m_read(m_state.s, &tmp, 1) > 0)
		;
	ioctl(m_state.s, FIONBIO, 0);
}

/* We only use privsep to get in-kernel SADB and SPD snapshots via sysctl */
void
monitor_loop(void)
{
	u_int32_t	 v, vn;
	ssize_t		 r;
	fd_set		 rfds;
	int		 ret;
	struct timeval	*tvp, tv;

	FD_ZERO(&rfds);
	tvp = NULL;
	vn = 0;

	for (;;) {
		ret = 0;
		v = 0;

		if (sigchld) {
			pid_t	pid;
			int	status;
			do {
				pid = waitpid(m_state.pid, &status, WNOHANG);
			} while (pid == -1 && errno == EINTR);

			if (pid == m_state.pid &&
			    (WIFEXITED(status) || WIFSIGNALED(status)))
				break;
		}

		FD_SET(m_state.s, &rfds);
		if (select(m_state.s + 1, &rfds, NULL, NULL, tvp) == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			log_err("monitor_loop: select()");
			break;
		}

		/* Wait for next task */
		if (FD_ISSET(m_state.s, &rfds)) {
			if ((r = m_read(m_state.s, &v, sizeof v)) < 1) {
				if (r == -1)
					log_err("monitor_loop: read()");
				break;
			}
		}

		/* Retry after timeout */
		if (v == 0 && tvp != NULL) {
			v = vn;
			tvp = NULL;
			vn = 0;
		}

		switch (v) {
		case MONITOR_GETSNAP:
			/* Get the data. */
			m_priv_pfkey_snap(m_state.s);
			break;
		case MONITOR_CARPINC:
			carp_demote(CARP_INC, 1);
			break;
		case MONITOR_CARPDEC:
			carp_demote(CARP_DEC, 1);
			break;
		case MONITOR_CONTROL_ACTIVATE:
			ret = m_priv_control_activate();
			break;
		case MONITOR_CONTROL_PASSIVATE:
			ret = m_priv_control_passivate();
			break;
		}

		if (ret == -1) {
			/* Trigger retry after timeout */
			tv.tv_sec = MONITOR_RETRY_TIMEOUT;
			tv.tv_usec = 0;
			tvp = &tv;
			vn = v;
		}
	}

	monitor_carpundemote(NULL);

	if (!sigchld)
		log_msg(0, "monitor_loop: priv process exiting abnormally");
	exit(0);
}

void
monitor_carpundemote(void *v)
{
	u_int32_t mtype = MONITOR_CARPDEC;
	if (!carp_demoted)
		return;
	if (m_write(m_state.s, &mtype, sizeof mtype) < 1)
		log_msg(1, "monitor_carpundemote: unable to write to monitor");
	else
		carp_demoted = 0;
}

void
monitor_carpdemote(void *v)
{
	u_int32_t mtype = MONITOR_CARPINC;
	if (carp_demoted)
		return;
	if (m_write(m_state.s, &mtype, sizeof mtype) < 1)
		log_msg(1, "monitor_carpdemote: unable to write to monitor");
	else
		carp_demoted = 1;
}

int
monitor_get_pfkey_snap(u_int8_t **sadb, u_int32_t *sadbsize, u_int8_t **spd,
    u_int32_t *spdsize)
{
	u_int32_t	v;
	ssize_t		rbytes;

	v = MONITOR_GETSNAP;
	if (m_write(m_state.s, &v, sizeof v) < 1)
		return -1;

	/* Read SADB data. */
	*sadb = *spd = NULL;
	*spdsize = 0;
	if (m_read(m_state.s, sadbsize, sizeof *sadbsize) < 1)
		return -1;
	if (*sadbsize) {
		*sadb = malloc(*sadbsize);
		if (!*sadb) {
			log_err("monitor_get_pfkey_snap: malloc()");
			monitor_drain_input();
			return -1;
		}
		rbytes = m_read(m_state.s, *sadb, *sadbsize);
		if (rbytes < 1) {
			freezero(*sadb, *sadbsize);
			return -1;
		}
	}

	/* Read SPD data */
	if (m_read(m_state.s, spdsize, sizeof *spdsize) < 1) {
		freezero(*sadb, *sadbsize);
		return -1;
	}
	if (*spdsize) {
		*spd = malloc(*spdsize);
		if (!*spd) {
			log_err("monitor_get_pfkey_snap: malloc()");
			monitor_drain_input();
			freezero(*sadb, *sadbsize);
			return -1;
		}
		rbytes = m_read(m_state.s, *spd, *spdsize);
		if (rbytes < 1) {
			freezero(*spd, *spdsize);
			freezero(*sadb, *sadbsize);
			return -1;
		}
	}

	log_msg(2, "monitor_get_pfkey_snap: got %u bytes SADB, %u bytes SPD",
	    *sadbsize, *spdsize);
	return 0;
}

int
monitor_control_active(int active)
{
	u_int32_t	cmd =
	    active ? MONITOR_CONTROL_ACTIVATE : MONITOR_CONTROL_PASSIVATE;
	if (write(m_state.s, &cmd, sizeof cmd) < 1)
		return -1;
	return 0;
}

/* Privileged */
static void
m_priv_pfkey_snap(int s)
{
	u_int8_t	*sadb_buf = NULL, *spd_buf = NULL;
	size_t		 sadb_buflen = 0, spd_buflen = 0, sz;
	int		 mib[5];
	u_int32_t	 v;

	mib[0] = CTL_NET;
	mib[1] = PF_KEY;
	mib[2] = PF_KEY_V2;
	mib[3] = NET_KEY_SADB_DUMP;
	mib[4] = 0; /* Unspec SA type */

	/* First, fetch SADB data */
	for (;;) {
		if (sysctl(mib, sizeof mib / sizeof mib[0], NULL, &sz, NULL, 0)
		    == -1)
			break;

		if (!sz)
			break;

		/* Try to catch newly added data */
		sz *= 2;

		if ((sadb_buf = malloc(sz)) == NULL)
			break;

		if (sysctl(mib, sizeof mib / sizeof mib[0], sadb_buf, &sz, NULL, 0)
		    == -1) {
			free(sadb_buf);
			sadb_buf = NULL;
			/*
			 * If new SAs were added meanwhile and the given buffer is
			 * too small, retry.
			 */
			if (errno == ENOMEM)
				continue;
			break;
		}

		sadb_buflen = sz;
		break;
	}

	/* Next, fetch SPD data */
	mib[3] = NET_KEY_SPD_DUMP;

	for (;;) {
		if (sysctl(mib, sizeof mib / sizeof mib[0], NULL, &sz, NULL, 0)
		    == -1)
			break;

		if (!sz)
			break;

		/* Try to catch newly added data */
		sz *= 2;

		if ((spd_buf = malloc(sz)) == NULL)
			break;

		if (sysctl(mib, sizeof mib / sizeof mib[0], spd_buf, &sz, NULL, 0)
		    == -1) {
			free(spd_buf);
			spd_buf = NULL;
			/*
			 * If new SPDs were added meanwhile and the given buffer is
			 * too small, retry.
			 */
			if (errno == ENOMEM)
				continue;
			break;
		}

		spd_buflen = sz;
		break;
	}

	/* Return SADB data */
	v = (u_int32_t)sadb_buflen;
	if (m_write(s, &v, sizeof v) == -1) {
		log_err("m_priv_pfkey_snap: write");
		goto cleanup;
	}
	if (m_write(s, sadb_buf, sadb_buflen) == -1) {
		log_err("m_priv_pfkey_snap: write");
		goto cleanup;
	}

	/* Return SPD data */
	v = (u_int32_t)spd_buflen;
	if (m_write(s, &v, sizeof v) == -1) {
		log_err("m_priv_pfkey_snap: write");
		goto cleanup;
	}
	if (m_write(s, spd_buf, spd_buflen) == -1) {
		log_err("m_priv_pfkey_snap: write");
		goto cleanup;
	}

cleanup:
	freezero(sadb_buf, sadb_buflen);
	freezero(spd_buf, spd_buflen);
}

static int
m_priv_isakmpd_fifocmd(const char *cmd)
{
	struct stat	sb;
	int		fd = -1, ret = -1;

	if ((fd = open(ISAKMPD_FIFO, O_WRONLY)) == -1) {
		log_err("m_priv_isakmpd_fifocmd: open(%s)", ISAKMPD_FIFO);
		goto out;
	}
	if (fstat(fd, &sb) == -1) {
		log_err("m_priv_isakmpd_fifocmd: fstat(%s)", ISAKMPD_FIFO);
		goto out;
	}
	if (!S_ISFIFO(sb.st_mode)) {
		log_err("m_priv_isakmpd_fifocmd: %s not a fifo", ISAKMPD_FIFO);
		goto out;
	}

	if (write(fd, cmd, strlen(cmd)) == -1) {
		log_err("m_priv_isakmpd_fifocmd write");
		goto out;
	}

	ret = 0;
 out:
	if (fd != -1)
		close(fd);

	return (ret);
}

static int
m_priv_iked_imsg(u_int cmd)
{
	struct sockaddr_un	 sun;
	int			 fd = -1, ret = -1;
	struct imsgbuf		 ibuf;

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		log_err("m_priv_iked_imsg: socket");
		goto out;
	}

	bzero(&sun, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, IKED_SOCKET, sizeof(sun.sun_path));

	if (connect(fd, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
		log_err("m_priv_iked_imsg: connect");
		goto out;
	}

	if (imsgbuf_init(&ibuf, fd) == -1) {
		log_err("m_priv_iked_imsg: imsgbuf_init");
		goto out;
	}
	if (imsg_compose(&ibuf, cmd, 0, 0, -1, NULL, 0) == -1) {
		log_err("m_priv_iked_imsg: compose");
		goto err;
	}
	if (imsgbuf_flush(&ibuf) == -1) {
		log_err("m_priv_iked_imsg: flush");
		goto err;
	}

	ret = 0;
 err:
	imsgbuf_clear(&ibuf);
 out:
	if (fd != -1)
		close(fd);

	return (ret);
}

static int
m_priv_control_activate(void)
{
	if (cfgstate.flags & CTL_ISAKMPD)
		if (m_priv_isakmpd_fifocmd("M active\n") == -1)
			return (-1);
	if (cfgstate.flags & CTL_IKED)
		if (m_priv_iked_imsg(IMSG_CTL_ACTIVE) == -1)
			return (-1);
	return (0);
}

static int
m_priv_control_passivate(void)
{
	if (cfgstate.flags & CTL_ISAKMPD)
		if (m_priv_isakmpd_fifocmd("M passive\n") == -1)
			return (-1);
	if (cfgstate.flags & CTL_IKED)
		if (m_priv_iked_imsg(IMSG_CTL_PASSIVE) == -1)
			return (-1);
	return (0);
}

ssize_t
m_write(int sock, void *buf, size_t len)
{
	ssize_t n;
	size_t pos = 0;
	char *ptr = buf;

	while (len > pos) {
		switch (n = write(sock, ptr + pos, len - pos)) {
		case -1:
			if (errno == EINTR || errno == EAGAIN)
				continue;
			/* FALLTHROUGH */
		case 0:
			return n;
			/* NOTREACHED */
		default:
			pos += n;
		}
	}
	return pos;
}

ssize_t
m_read(int sock, void *buf, size_t len)
{
	ssize_t n;
	size_t pos = 0;
	char *ptr = buf;

	while (len > pos) {
		switch (n = read(sock, ptr + pos, len - pos)) {
		case -1:
			if (errno == EINTR || errno == EAGAIN)
				continue;
			/* FALLTHROUGH */
		case 0:
			return n;
			/* NOTREACHED */
		default:
			pos += n;
		}
	}
	return pos;
}
