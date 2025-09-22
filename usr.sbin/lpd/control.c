/*	$OpenBSD: control.c,v 1.3 2024/03/22 19:14:28 bluhm Exp $	*/

/*
 * Copyright (c) 2017 Eric Faurot <eric@openbsd.org>
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
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <event.h>
#include <imsg.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "lpd.h"

#include "log.h"
#include "proc.h"

#define	CONTROL_BACKLOG	5

static void control_init(const char *);
static void control_listen(void);
static void control_pause(void);
static void control_resume(void);
static void control_accept(int, short, void *);
static void control_close(struct imsgproc *);
static void control_dispatch_priv(struct imsgproc *, struct imsg *, void *);
static void control_dispatch_client(struct imsgproc *, struct imsg *, void *);

static struct {
	struct event	evt;
	int		fd;
	int		pause;
} ctl;

void
control(int debug, int verbose)
{
	struct passwd *pw;

	/* Early initialisation. */
	log_init(debug, LOG_DAEMON);
	log_setverbose(verbose);
	log_procinit("control");
	setproctitle("control");

	control_init(LPD_SOCKET);

	/* Drop privileges. */
	if ((pw = getpwnam(LPD_USER)) == NULL)
		fatalx("unknown user " LPD_USER);

	if (chroot(_PATH_VAREMPTY) == -1)
		fatal("%s: chroot", __func__);
	if (chdir("/") == -1)
		fatal("%s: chdir", __func__);

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("cannot drop privileges");

	if (pledge("stdio unix recvfd sendfd", NULL) == -1)
		fatal("%s: pledge", __func__);

	event_init();

	signal(SIGPIPE, SIG_IGN);

	/* Setup imsg socket with parent. */
	p_priv = proc_attach(PROC_PRIV, 3);
	if (p_priv == NULL)
		fatal("%s: proc_attach", __func__);
	proc_setcallback(p_priv, control_dispatch_priv, NULL);
	proc_enable(p_priv);

	event_dispatch();

	exit(0);
}

static void
control_init(const char *path)
{
	struct sockaddr_un sun;
	mode_t old_umask;
	int fd;

	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd == -1)
		fatal("%s: socket", __func__);

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, LPD_SOCKET, sizeof(sun.sun_path));

	if ((unlink(path) == -1) && (errno != ENOENT))
		fatal("%s: unlink: %s", __func__, path);

	old_umask = umask(S_IXUSR|S_IXGRP|S_IWOTH|S_IROTH|S_IXOTH);
	if (bind(fd, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		fatal("%s: bind: %s", __func__, path);
	umask(old_umask);

	if (chmod(path, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) == -1)
		fatal("%s: chmod: %s", __func__, path);

	ctl.fd = fd;
}

static void
control_listen(void)
{
	if (listen(ctl.fd, CONTROL_BACKLOG) == -1)
		fatal("%s: listen", __func__);

	ctl.pause = 0;
	control_resume();
}

static void
control_pause(void)
{
	struct timeval tv;

	event_del(&ctl.evt);

	tv.tv_sec = 1;
	tv.tv_usec = 0;

	evtimer_set(&ctl.evt, control_accept, NULL);
	evtimer_add(&ctl.evt, &tv);
	ctl.pause = 1;
}

static void
control_resume(void)
{
	if (ctl.pause) {
		evtimer_del(&ctl.evt);
		ctl.pause = 0;
	}
	event_set(&ctl.evt, ctl.fd, EV_READ | EV_PERSIST, control_accept, NULL);
	event_add(&ctl.evt, NULL);
}

static void
control_accept(int fd, short event, void *arg)
{
	struct imsgproc *proc;
	int sock;

	if (ctl.pause) {
		ctl.pause = 0;
		control_resume();
		return;
	}

	sock = accept4(ctl.fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
	if (sock == -1) {
		if (errno == ENFILE || errno == EMFILE)
			control_pause();
		else if (errno != EWOULDBLOCK && errno != EINTR &&
		    errno != ECONNABORTED)
			log_warn("%s: accept4", __func__);
		return;
	}

	proc = proc_attach(PROC_CLIENT, sock);
	if (proc == NULL) {
		log_warn("%s: proc_attach", __func__);
		close(sock);
		return;
	}
	proc_setcallback(proc, control_dispatch_client, NULL);
	proc_enable(proc);
}

static void
control_close(struct imsgproc *proc)
{
	proc_free(proc);

	if (ctl.pause)
		control_resume();
}

static void
control_dispatch_priv(struct imsgproc *proc, struct imsg *imsg, void *arg)
{
	if (imsg == NULL) {
		log_debug("%s: imsg connection lost", __func__);
		event_loopexit(NULL);
		return;
	}

	if (log_getverbose() > LOGLEVEL_IMSG)
		log_imsg(proc, imsg);

	switch (imsg->hdr.type) {
	case IMSG_CONF_START:
		m_end(proc);
		break;

	case IMSG_CONF_END:
		m_end(proc);
		control_listen();
		break;

	default:
		fatalx("%s: unexpected imsg %s", __func__,
		    log_fmt_imsgtype(imsg->hdr.type));
	}
}

static void
control_dispatch_client(struct imsgproc *proc, struct imsg *imsg, void *arg)
{
	if (imsg == NULL) {
		control_close(proc);
		return;
	}

	if (log_getverbose() > LOGLEVEL_IMSG)
		log_imsg(proc, imsg);

	switch (imsg->hdr.type) {
	default:
		log_debug("%s: error handling imsg %d", __func__,
		    imsg->hdr.type);
	}
}
