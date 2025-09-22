/*	$OpenBSD: control.c,v 1.17 2024/11/21 13:35:20 claudio Exp $	*/

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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
#ifndef SMALL
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <errno.h>
#include <event.h>
#include <imsg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "slaacd.h"
#include "control.h"
#include "frontend.h"

#define	CONTROL_BACKLOG	5

struct {
	struct event	ev;
	struct event	evt;
	int		fd;
} control_state = {.fd = -1};

struct ctl_conn {
	TAILQ_ENTRY(ctl_conn)	entry;
	struct imsgev		iev;
};

struct ctl_conn	*control_connbyfd(int);
struct ctl_conn	*control_connbypid(pid_t);
void		 control_close(int);

TAILQ_HEAD(ctl_conns, ctl_conn) ctl_conns = TAILQ_HEAD_INITIALIZER(ctl_conns);

int
control_init(char *path)
{
	struct sockaddr_un	 sun;
	int			 fd;
	mode_t			 old_umask;

	if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    0)) == -1) {
		log_warn("%s: socket", __func__);
		return (-1);
	}

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, path, sizeof(sun.sun_path));

	if (unlink(path) == -1)
		if (errno != ENOENT) {
			log_warn("%s: unlink %s", __func__, path);
			close(fd);
			return (-1);
		}

	old_umask = umask(S_IXUSR|S_IXGRP|S_IWOTH|S_IROTH|S_IXOTH);
	if (bind(fd, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
		log_warn("%s: bind: %s", __func__, path);
		close(fd);
		umask(old_umask);
		return (-1);
	}
	umask(old_umask);

	if (chmod(path, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) == -1) {
		log_warn("%s: chmod", __func__);
		close(fd);
		(void)unlink(path);
		return (-1);
	}

	return (fd);
}

int
control_listen(int fd)
{
	if (control_state.fd != -1)
		fatalx("%s: received unexpected controlsock", __func__);

	control_state.fd = fd;
	if (listen(control_state.fd, CONTROL_BACKLOG) == -1) {
		log_warn("%s: listen", __func__);
		return (-1);
	}

	event_set(&control_state.ev, control_state.fd, EV_READ,
	    control_accept, NULL);
	event_add(&control_state.ev, NULL);
	evtimer_set(&control_state.evt, control_accept, NULL);

	return (0);
}

void
control_accept(int listenfd, short event, void *bula)
{
	int			 connfd;
	socklen_t		 len;
	struct sockaddr_un	 sun;
	struct ctl_conn		*c;

	event_add(&control_state.ev, NULL);
	if ((event & EV_TIMEOUT))
		return;

	len = sizeof(sun);
	if ((connfd = accept4(listenfd, (struct sockaddr *)&sun, &len,
	    SOCK_CLOEXEC | SOCK_NONBLOCK)) == -1) {
		/*
		 * Pause accept if we are out of file descriptors, or
		 * libevent will haunt us here too.
		 */
		if (errno == ENFILE || errno == EMFILE) {
			struct timeval evtpause = { 1, 0 };

			event_del(&control_state.ev);
			evtimer_add(&control_state.evt, &evtpause);
		} else if (errno != EWOULDBLOCK && errno != EINTR &&
		    errno != ECONNABORTED)
			log_warn("%s: accept4", __func__);
		return;
	}

	if ((c = calloc(1, sizeof(struct ctl_conn))) == NULL) {
		log_warn("%s: calloc", __func__);
		close(connfd);
		return;
	}

	if (imsgbuf_init(&c->iev.ibuf, connfd) == -1) {
		log_warn("%s: imsgbuf_init", __func__);
		close(connfd);
		free(c);
		return;
	}
	c->iev.handler = control_dispatch_imsg;
	c->iev.events = EV_READ;
	event_set(&c->iev.ev, c->iev.ibuf.fd, c->iev.events,
	    c->iev.handler, &c->iev);
	event_add(&c->iev.ev, NULL);

	TAILQ_INSERT_TAIL(&ctl_conns, c, entry);
}

struct ctl_conn *
control_connbyfd(int fd)
{
	struct ctl_conn	*c;

	TAILQ_FOREACH(c, &ctl_conns, entry) {
		if (c->iev.ibuf.fd == fd)
			break;
	}

	return (c);
}

struct ctl_conn *
control_connbypid(pid_t pid)
{
	struct ctl_conn	*c;

	TAILQ_FOREACH(c, &ctl_conns, entry) {
		if (c->iev.ibuf.pid == pid)
			break;
	}

	return (c);
}

void
control_close(int fd)
{
	struct ctl_conn	*c;

	if ((c = control_connbyfd(fd)) == NULL) {
		log_warnx("%s: fd %d: not found", __func__, fd);
		return;
	}

	imsgbuf_clear(&c->iev.ibuf);
	TAILQ_REMOVE(&ctl_conns, c, entry);

	event_del(&c->iev.ev);
	close(c->iev.ibuf.fd);

	/* Some file descriptors are available again. */
	if (evtimer_pending(&control_state.evt, NULL)) {
		evtimer_del(&control_state.evt);
		event_add(&control_state.ev, NULL);
	}

	free(c);
}

void
control_dispatch_imsg(int fd, short event, void *bula)
{
	struct ctl_conn	*c;
	struct imsg	 imsg;
	ssize_t		 n;
	int		 verbose;
	uint32_t	 if_index, type;
	pid_t		 pid;

	if ((c = control_connbyfd(fd)) == NULL) {
		log_warnx("%s: fd %d: not found", __func__, fd);
		return;
	}

	if (event & EV_READ) {
		if (imsgbuf_read(&c->iev.ibuf) != 1) {
			control_close(fd);
			return;
		}
	}
	if (event & EV_WRITE) {
		if (imsgbuf_write(&c->iev.ibuf) == -1) {
			control_close(fd);
			return;
		}
	}

	for (;;) {
		if ((n = imsg_get(&c->iev.ibuf, &imsg)) == -1) {
			control_close(fd);
			return;
		}
		if (n == 0)
			break;

		type = imsg_get_type(&imsg);
		pid = imsg_get_pid(&imsg);
		switch (type) {
		case IMSG_CTL_LOG_VERBOSE:
			if (imsg_get_data(&imsg, &verbose,
			    sizeof(verbose)) == -1)
				break;

			/* Forward to all other processes. */
			frontend_imsg_compose_main(type, pid, &verbose,
			    sizeof(verbose));
			frontend_imsg_compose_engine(type, 0, pid, &verbose,
			    sizeof(verbose));

			log_setverbose(verbose);
			break;
		case IMSG_CTL_SHOW_INTERFACE_INFO:
		case IMSG_CTL_SEND_SOLICITATION:
			if (imsg_get_data(&imsg, &if_index,
			    sizeof(if_index)) == -1)
				break;

			c->iev.ibuf.pid = pid;
			frontend_imsg_compose_engine(type, 0, pid, &if_index,
			    sizeof(if_index));
			break;
		default:
			log_debug("%s: error handling imsg %d", __func__, type);
			break;
		}
		imsg_free(&imsg);
	}

	imsg_event_add(&c->iev);
}

int
control_imsg_relay(struct imsg *imsg)
{
	struct ctl_conn	*c;

	if ((c = control_connbypid(imsg_get_pid(imsg))) == NULL)
		return (0);

	return (imsg_forward_event(&c->iev, imsg));
}
#endif	/* SMALL */
