/*	$OpenBSD: control.c,v 1.7 2024/11/21 13:43:10 claudio Exp $ */

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

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>

#include <errno.h>
#include <event.h>
#include <imsg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "radiusd.h"
#include "radiusd_local.h"
#include "log.h"
#include "control.h"

static TAILQ_HEAD(, ctl_conn) ctl_conns = TAILQ_HEAD_INITIALIZER(ctl_conns);

#define	CONTROL_BACKLOG	5
static int	 idseq = 0;

struct ctl_conn	*control_connbyfd(int);
struct ctl_conn	*control_connbyid(uint32_t);
void		 control_close(int);
void		 control_connfree(struct ctl_conn *);
void		 control_event_add(struct ctl_conn *);

struct {
	struct event	ev;
	struct event	evt;
	int		fd;
} control_state;

int
control_init(const char *path)
{
	struct sockaddr_un	 sun;
	int			 fd;
	mode_t			 old_umask;

	if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    0)) == -1) {
		log_warn("control_init: socket");
		return (-1);
	}

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, path, sizeof(sun.sun_path));

	if (unlink(path) == -1)
		if (errno != ENOENT) {
			log_warn("control_init: unlink %s", path);
			close(fd);
			return (-1);
		}

	old_umask = umask(S_IXUSR|S_IXGRP|S_IWOTH|S_IROTH|S_IXOTH);
	if (bind(fd, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
		log_warn("control_init: bind: %s", path);
		close(fd);
		umask(old_umask);
		return (-1);
	}
	umask(old_umask);

	if (chmod(path, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) == -1) {
		log_warn("control_init: chmod");
		close(fd);
		(void)unlink(path);
		return (-1);
	}

	control_state.fd = fd;

	return (0);
}

int
control_listen(void)
{

	if (listen(control_state.fd, CONTROL_BACKLOG) == -1) {
		log_warn("control_listen: listen");
		return (-1);
	}

	event_set(&control_state.ev, control_state.fd, EV_READ,
	    control_accept, NULL);
	event_add(&control_state.ev, NULL);
	evtimer_set(&control_state.evt, control_accept, NULL);

	return (0);
}

void
control_cleanup(void)
{
	struct ctl_conn	*c, *t;

	TAILQ_FOREACH_SAFE(c, &ctl_conns, entry, t) {
		TAILQ_REMOVE(&ctl_conns, c, entry);
		control_connfree(c);
	}
	event_del(&control_state.ev);
	event_del(&control_state.evt);
}

/* ARGSUSED */
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
			log_warn("control_accept: accept");
		return;
	}

	if ((c = calloc(1, sizeof(struct ctl_conn))) == NULL) {
		log_warn("control_accept");
		close(connfd);
		return;
	}

	if (imsgbuf_init(&c->iev.ibuf, connfd) == -1) {
		log_warn("control_accept");
		close(connfd);
		free(c);
		return;
	}
	if (idseq == 0)	/* don't use zero.  See radiusd_module_imsg */
		++idseq;
	c->id = idseq++;
	c->iev.handler = control_dispatch_imsg;
	c->iev.events = EV_READ;
	event_set(&c->iev.ev, c->iev.ibuf.fd, c->iev.events, c->iev.handler, c);
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
control_connbyid(uint32_t id)
{
	struct ctl_conn	*c;

	TAILQ_FOREACH(c, &ctl_conns, entry) {
		if (c->id == id)
			break;
	}

	return (c);
}

void
control_close(int fd)
{
	struct ctl_conn	*c;

	if ((c = control_connbyfd(fd)) == NULL) {
		log_warn("control_close: fd %d: not found", fd);
		return;
	}
	if (c->modulename[0] != '\0')
		radiusd_imsg_compose_module(radiusd_s, c->modulename,
		    IMSG_RADIUSD_MODULE_CTRL_UNBIND, c->id, -1, -1, NULL, 0);

	control_connfree(c);
}

void
control_connfree(struct ctl_conn *c)
{
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

/* ARGSUSED */
void
control_dispatch_imsg(int fd, short event, void *bula)
{
	struct ctl_conn	*c;
	struct imsg	 imsg;
	ssize_t		 n, datalen;
	char		 modulename[RADIUSD_MODULE_NAME_LEN + 1], msg[128];

	if ((c = control_connbyfd(fd)) == NULL) {
		log_warn("control_dispatch_imsg: fd %d: not found", fd);
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

		datalen = imsg.hdr.len - IMSG_HEADER_SIZE;
		switch (imsg.hdr.type) {
		default:
			if (imsg.hdr.type >= IMSG_RADIUSD_MODULE_MIN) {
				if (datalen < RADIUSD_MODULE_NAME_LEN) {
					log_warnx( "%s: received an invalid "
					    "imsg %d: too small", __func__,
					    imsg.hdr.type);
					break;
				}
				memset(modulename, 0, sizeof(modulename));
				memcpy(modulename, imsg.data,
				    RADIUSD_MODULE_NAME_LEN);
				if (radiusd_imsg_compose_module(radiusd_s,
				    modulename, imsg.hdr.type, c->id, -1, -1,
				    (caddr_t)imsg.data +
				    RADIUSD_MODULE_NAME_LEN, datalen -
				    RADIUSD_MODULE_NAME_LEN) != 0) {
					snprintf(msg, sizeof(msg),
					    "module `%s' is not loaded or not "
					    "capable for control command",
					    modulename);
					imsg_compose_event(&c->iev,
					    IMSG_NG, c->id, -1, -1, msg,
					    strlen(msg) + 1);
				}
			} else
				log_debug("control_dispatch_imsg: "
				    "error handling imsg %d", imsg.hdr.type);
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

	if ((c = control_connbyid(imsg->hdr.peerid)) == NULL)
		return (0);

	return (imsg_compose_event(&c->iev, imsg->hdr.type, 0, imsg->hdr.pid,
	    -1, imsg->data, imsg->hdr.len - IMSG_HEADER_SIZE));
}

void
control_conn_bind(uint32_t peerid, const char *modulename)
{
	struct ctl_conn	*c;

	if ((c = control_connbyid(peerid)) == NULL)
		return;

	if (c->modulename[0] != '\0')
		radiusd_imsg_compose_module(radiusd_s, c->modulename,
		    IMSG_RADIUSD_MODULE_CTRL_UNBIND, c->id, -1, -1, NULL, 0);
	strlcpy(c->modulename, modulename, sizeof(c->modulename));
}
