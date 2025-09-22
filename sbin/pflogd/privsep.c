/*	$OpenBSD: privsep.c,v 1.35 2021/07/12 15:09:19 beck Exp $	*/

/*
 * Copyright (c) 2003 Can Erkin Acar
 * Copyright (c) 2003 Anil Madhavapeddy <anil@recoil.org>
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
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <net/if.h>
#include <net/bpf.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pcap.h>
#include <pcap-int.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <netdb.h>
#include <resolv.h>
#include "pflogd.h"

enum cmd_types {
	PRIV_INIT_PCAP,		/* init pcap fdpass bpf */
	PRIV_SET_SNAPLEN,	/* set the snaplength */
	PRIV_OPEN_LOG,		/* open logfile for appending */
};

static int priv_fd = -1;
static volatile pid_t child_pid = -1;

static void sig_pass_to_chld(int);
static int  may_read(int, void *, size_t);
static void must_read(int, void *, size_t);
static void must_write(int, void *, size_t);
static int  set_snaplen(int snap);

extern char *filename;
extern char *interface;
extern char errbuf[PCAP_ERRBUF_SIZE];
extern pcap_t *hpcap;

/* based on syslogd privsep */
void
priv_init(int Pflag, int argc, char *argv[])
{
	int i, fd = -1, bpfd = -1, nargc, socks[2], cmd;
	int snaplen, ret, olderrno;
	struct passwd *pw;
	char **nargv;
	unsigned int buflen;

	pw = getpwnam("_pflogd");
	if (pw == NULL)
		errx(1, "unknown user _pflogd");
	endpwent();

	if (Pflag) {
		gid_t gidset[1];

		/* Child - drop privileges and return */
		if (chroot(pw->pw_dir) != 0)
			err(1, "unable to chroot");
		if (chdir("/") != 0)
			err(1, "unable to chdir");

		gidset[0] = pw->pw_gid;
		if (setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) == -1)
			err(1, "setresgid() failed");
		if (setgroups(1, gidset) == -1)
			err(1, "setgroups() failed");
		if (setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) == -1)
			err(1, "setresuid() failed");
		priv_fd = 3;
		return;
	}

	/* Create sockets */
	if (socketpair(AF_LOCAL, SOCK_STREAM, PF_UNSPEC, socks) == -1)
		err(1, "socketpair() failed");

	child_pid = fork();
	if (child_pid == -1)
		err(1, "fork() failed");

	if (!child_pid) {
		close(socks[0]);
		if (dup2(socks[1], 3) == -1)
			err(1, "dup2 unpriv sock failed");
		close(socks[1]);
		if ((nargv = reallocarray(NULL, argc + 2,
		    sizeof(char *))) == NULL)
			err(1, "alloc unpriv argv failed");
		nargc = 0;
		nargv[nargc++] = argv[0];
		nargv[nargc++] = "-P";
		for (i = 1; i < argc; i++)
			nargv[nargc++] = argv[i];
		nargv[nargc] = NULL;
		execvp(nargv[0], nargv);
		err(1, "exec unpriv '%s' failed", nargv[0]);
	}
	close(socks[1]);

	/* Father */
	/* Pass ALRM/TERM/HUP/INT/QUIT through to child */
	signal(SIGALRM, sig_pass_to_chld);
	signal(SIGTERM, sig_pass_to_chld);
	signal(SIGHUP,  sig_pass_to_chld);
	signal(SIGINT,  sig_pass_to_chld);
	signal(SIGQUIT, sig_pass_to_chld);

	setproctitle("[priv]");

	if (unveil(_PATH_RESCONF, "r") == -1)
		err(1, "unveil %s", _PATH_RESCONF);
	if (unveil(_PATH_HOSTS, "r") == -1)
		err(1, "unveil %s", _PATH_HOSTS);
	if (unveil(_PATH_SERVICES, "r") == -1)
		err(1, "unveil %s", _PATH_SERVICES);
	if (unveil("/dev/bpf", "r") == -1)
		err(1, "unveil /dev/bpf");
	if (unveil(filename, "rwc") == -1)
		err(1, "unveil %s", filename);
	if (unveil(NULL, NULL) == -1)
		err(1, "unveil");

#if 0
	/* This needs to do bpf ioctl */
BROKEN	if (pledge("stdio rpath wpath cpath sendfd proc bpf", NULL) == -1)
		err(1, "pledge");
#endif

	while (1) {
		if (may_read(socks[0], &cmd, sizeof(int)))
			break;
		switch (cmd) {
		case PRIV_INIT_PCAP:
			logmsg(LOG_DEBUG,
			    "[priv]: msg PRIV_INIT_PCAP received");
			/* initialize pcap */
			if (hpcap != NULL || init_pcap()) {
				logmsg(LOG_ERR, "[priv]: Exiting, init failed");
				_exit(1);
			}
			buflen = hpcap->bufsize; /* BIOCGBLEN for unpriv proc */
			must_write(socks[0], &buflen, sizeof(unsigned int));
			fd = pcap_fileno(hpcap);
			send_fd(socks[0], fd);
			if (fd < 0) {
				logmsg(LOG_ERR, "[priv]: Exiting, init failed");
				_exit(1);
			}
			break;

		case PRIV_SET_SNAPLEN:
			logmsg(LOG_DEBUG,
			    "[priv]: msg PRIV_SET_SNAPLENGTH received");
			must_read(socks[0], &snaplen, sizeof(int));

			ret = set_snaplen(snaplen);
			if (ret) {
				logmsg(LOG_NOTICE,
				   "[priv]: set_snaplen failed for snaplen %d",
				   snaplen);
			}

			must_write(socks[0], &ret, sizeof(int));
			break;

		case PRIV_OPEN_LOG:
			logmsg(LOG_DEBUG,
			    "[priv]: msg PRIV_OPEN_LOG received");
			/* create or append logs but do not follow symlinks */
			if (bpfd != -1) {
				close(bpfd);
				bpfd = -1;
			}
			bpfd = open(filename,
			    O_RDWR|O_CREAT|O_APPEND|O_NONBLOCK|O_NOFOLLOW,
			    0600);
			olderrno = errno;
			send_fd(socks[0], bpfd);
			if (bpfd == -1)
				logmsg(LOG_NOTICE,
				    "[priv]: failed to open %s: %s",
				    filename, strerror(olderrno));
			break;

		default:
			logmsg(LOG_ERR, "[priv]: unknown command %d", cmd);
			_exit(1);
			/* NOTREACHED */
		}
	}

	exit(1);
}

/* this is called from parent */
static int
set_snaplen(int snap)
{
	if (hpcap == NULL)
		return (1);

	hpcap->snapshot = snap;
	set_pcap_filter();

	return 0;
}

/* receive bpf fd from privileged process using fdpass and init pcap */
int
priv_init_pcap(int snaplen)
{
	int cmd, fd;
	unsigned int buflen;

	if (priv_fd < 0)
		errx(1, "%s: called from privileged portion", __func__);

	cmd = PRIV_INIT_PCAP;

	must_write(priv_fd, &cmd, sizeof(int));
	must_read(priv_fd, &buflen, sizeof(unsigned int));
	fd = receive_fd(priv_fd);
	if (fd < 0)
		return (-1);

	/* XXX temporary until pcap_open_live_fd API */
	hpcap = pcap_create(interface, errbuf);
	if (hpcap == NULL)
		return (-1);

	/* XXX copies from pcap_open_live/pcap_activate */
	hpcap->fd = fd;
	pcap_set_snaplen(hpcap, snaplen);
	pcap_set_promisc(hpcap, 1);
	pcap_set_timeout(hpcap, PCAP_TO_MS);
	hpcap->oldstyle = 1;
	hpcap->linktype = DLT_PFLOG;
	hpcap->bufsize = buflen; /* XXX bpf BIOCGBLEN */
	hpcap->buffer = malloc(hpcap->bufsize);
	if (hpcap->buffer == NULL)
		return (-1);
	hpcap->activated = 1;

	return (0);
}

/*
 * send the snaplength to privileged process
 */
int
priv_set_snaplen(int snaplen)
{
	int cmd, ret;

	if (priv_fd < 0)
		errx(1, "%s: called from privileged portion", __func__);

	cmd = PRIV_SET_SNAPLEN;

	must_write(priv_fd, &cmd, sizeof(int));
	must_write(priv_fd, &snaplen, sizeof(int));

	must_read(priv_fd, &ret, sizeof(int));

	/* also set hpcap->snapshot in child */
	if (ret == 0)
		hpcap->snapshot = snaplen;

	return (ret);
}

/* Open log-file */
int
priv_open_log(void)
{
	int cmd, fd;

	if (priv_fd < 0)
		errx(1, "%s: called from privileged portion", __func__);

	cmd = PRIV_OPEN_LOG;
	must_write(priv_fd, &cmd, sizeof(int));
	fd = receive_fd(priv_fd);

	return (fd);
}

/* If priv parent gets a TERM or HUP, pass it through to child instead */
static void
sig_pass_to_chld(int sig)
{
	int oerrno = errno;

	if (child_pid != -1)
		kill(child_pid, sig);
	errno = oerrno;
}

/* Read all data or return 1 for error.  */
static int
may_read(int fd, void *buf, size_t n)
{
	char *s = buf;
	ssize_t res, pos = 0;

	while (n > pos) {
		res = read(fd, s + pos, n - pos);
		switch (res) {
		case -1:
			if (errno == EINTR || errno == EAGAIN)
				continue;
		case 0:
			return (1);
		default:
			pos += res;
		}
	}
	return (0);
}

/* Read data with the assertion that it all must come through, or
 * else abort the process.  Based on atomicio() from openssh. */
static void
must_read(int fd, void *buf, size_t n)
{
	char *s = buf;
	ssize_t res, pos = 0;

	while (n > pos) {
		res = read(fd, s + pos, n - pos);
		switch (res) {
		case -1:
			if (errno == EINTR || errno == EAGAIN)
				continue;
		case 0:
			_exit(0);
		default:
			pos += res;
		}
	}
}

/* Write data with the assertion that it all has to be written, or
 * else abort the process.  Based on atomicio() from openssh. */
static void
must_write(int fd, void *buf, size_t n)
{
	char *s = buf;
	ssize_t res, pos = 0;

	while (n > pos) {
		res = write(fd, s + pos, n - pos);
		switch (res) {
		case -1:
			if (errno == EINTR || errno == EAGAIN)
				continue;
		case 0:
			_exit(0);
		default:
			pos += res;
		}
	}
}
