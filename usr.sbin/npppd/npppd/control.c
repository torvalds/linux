/*	$OpenBSD: control.c,v 1.16 2025/03/25 03:07:58 yasuoka Exp $	*/

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

#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>

#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <imsg.h>

#include "log.h"

#define	CONTROL_BACKLOG	5

#include "npppd_local.h"
#include "npppd_ctl.h"

struct ctl_conn_list ctl_conns;

struct ctl_conn	*control_connbyfd(int);
void		 control_close(int, struct control_sock *);
void             control_accept (int, short, void *);
void             control_close (int, struct control_sock *);
void             control_dispatch_imsg (int, short, void *);
void             control_imsg_forward (struct imsg *);

int
control_init(struct control_sock *cs)
{
	struct sockaddr_un	 sun;
	int			 fd;
	mode_t			 old_umask, mode;

	if (cs->cs_name == NULL)
		return (0);

	if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0)) == -1) {
		log_warn("control_init: socket");
		return (-1);
	}

	sun.sun_family = AF_UNIX;
	if (strlcpy(sun.sun_path, cs->cs_name,
	    sizeof(sun.sun_path)) >= sizeof(sun.sun_path)) {
		log_warn("control_init: %s name too long", cs->cs_name);
		close(fd);
		return (-1);
	}

	if (unlink(cs->cs_name) == -1)
		if (errno != ENOENT) {
			log_warn("control_init: unlink %s", cs->cs_name);
			close(fd);
			return (-1);
		}

	if (cs->cs_restricted) {
		old_umask = umask(S_IXUSR|S_IXGRP|S_IXOTH);
		mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH;
	} else {
		old_umask = umask(S_IXUSR|S_IXGRP|S_IWOTH|S_IROTH|S_IXOTH);
		mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP;
	}

	if (bind(fd, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
		log_warn("control_init: bind: %s", cs->cs_name);
		close(fd);
		(void)umask(old_umask);
		return (-1);
	}
	(void)umask(old_umask);

	if (chmod(cs->cs_name, mode) == -1) {
		log_warn("control_init: chmod");
		close(fd);
		(void)unlink(cs->cs_name);
		return (-1);
	}
	TAILQ_INIT(&ctl_conns);

	cs->cs_fd = fd;

	return (0);
}

int
control_listen(struct control_sock *cs)
{
	if (cs->cs_name == NULL)
		return (0);

	if (listen(cs->cs_fd, CONTROL_BACKLOG) == -1) {
		log_warn("control_listen: listen");
		return (-1);
	}

	event_set(&cs->cs_ev, cs->cs_fd, EV_READ,
	    control_accept, cs);
	event_add(&cs->cs_ev, NULL);
	evtimer_set(&cs->cs_evt, control_accept, cs);

	return (0);
}

void
control_cleanup(struct control_sock *cs)
{
	struct ctl_conn *c, *nc;

	if (cs->cs_name == NULL)
		return;

	TAILQ_FOREACH_SAFE(c, &ctl_conns, entry, nc)
		control_close(c->iev.ibuf.fd, cs);

	event_del(&cs->cs_ev);
	event_del(&cs->cs_evt);
	(void)unlink(cs->cs_name);
}

#include <stdio.h>
void
control_accept(int listenfd, short event, void *arg)
{
	struct control_sock	*cs = (struct control_sock *)arg;
	int			 connfd;
	socklen_t		 len;
	struct sockaddr_un	 sun;
	struct ctl_conn		*c;

	event_add(&cs->cs_ev, NULL);
	if ((event & EV_TIMEOUT))
		return;

	len = sizeof(sun);
	if ((connfd = accept4(listenfd,
	    (struct sockaddr *)&sun, &len, SOCK_NONBLOCK)) == -1) {
		/*
		 * Pause accept if we are out of file descriptors, or
		 * libevent will haunt us here too.
		 */
		if (errno == ENFILE || errno == EMFILE) {
			struct timeval evtpause = { 1, 0 };

			event_del(&cs->cs_ev);
			evtimer_add(&cs->cs_evt, &evtpause);
		} else if (errno != EWOULDBLOCK && errno != EINTR)
			log_warn("control_accept: accept");
		return;
	}

	if ((c = calloc(1, sizeof(struct ctl_conn))) == NULL) {
		log_warn("control_accept");
		close(connfd);
		return;
	}
	if ((c->ctx = npppd_ctl_create(cs->cs_ctx)) == NULL) {
		log_warn("control_accept");
		close(connfd);
		free(c);
		return;
	}

	if (imsgbuf_init(&c->iev.ibuf, connfd) == -1) {
		log_warn("control_accept");
		close(connfd);
		free(c->ctx);
		free(c);
		return;
	}

	c->iev.handler = control_dispatch_imsg;
	c->iev.events = EV_READ;
	c->iev.data = cs;
	event_set(&c->iev.ev, c->iev.ibuf.fd, c->iev.events,
	    c->iev.handler, cs);
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

void
control_close(int fd, struct control_sock *cs)
{
	struct ctl_conn	*c;

	if ((c = control_connbyfd(fd)) == NULL) {
		log_warn("control_close: fd %d: not found", fd);
		return;
	}

	imsgbuf_clear(&c->iev.ibuf);
	TAILQ_REMOVE(&ctl_conns, c, entry);

	event_del(&c->iev.ev);
	close(c->iev.ibuf.fd);

	/* Some file descriptors are available again. */
	if (evtimer_pending(&cs->cs_evt, NULL)) {
		evtimer_del(&cs->cs_evt);
		event_add(&cs->cs_ev, NULL);
	}
	npppd_ctl_destroy(c->ctx);

	free(c);
}

void
control_dispatch_imsg(int fd, short event, void *arg)
{
	struct control_sock	*cs = (struct control_sock *)arg;
	struct ctl_conn		*c;
	struct imsg		 imsg;
	int			 n, retval;

	if ((c = control_connbyfd(fd)) == NULL) {
		log_warn("control_dispatch_imsg: fd %d: not found", fd);
		return;
	}


	if (event & EV_WRITE) {
		if (imsgbuf_write(&c->iev.ibuf) == -1) {
			control_close(fd, cs);
			return;
		}
		if (imsgbuf_queuelen(&c->iev.ibuf) == 0)
			npppd_ctl_imsg_compose(c->ctx, &c->iev.ibuf);
		imsg_event_add(&c->iev);
		if (!(event & EV_READ))
			return;
	}
	if (event & EV_READ) {
		if (imsgbuf_read(&c->iev.ibuf) != 1) {
			control_close(fd, cs);
			return;
		}
	}

	for (;;) {
		if ((n = imsg_get(&c->iev.ibuf, &imsg)) == -1) {
			control_close(fd, cs);
			return;
		}

		if (n == 0)
			break;

		if (cs->cs_restricted || (c->flags & CTL_CONN_LOCKED)) {
			switch (imsg.hdr.type) {
			default:
				log_debug("control_dispatch_imsg: "
				    "client requested restricted command");
				imsg_free(&imsg);
				control_close(fd, cs);
				return;
			}
		}

		switch (imsg.hdr.type) {
		case IMSG_CTL_NOP:
			imsg_compose(&c->iev.ibuf, IMSG_CTL_OK, 0, 0, -1,
			    NULL, 0);
			break;

		case IMSG_CTL_WHO:
		case IMSG_CTL_MONITOR:
		case IMSG_CTL_WHO_AND_MONITOR:
			if (imsg.hdr.type == IMSG_CTL_WHO)
				retval = npppd_ctl_who(c->ctx);
			else if (imsg.hdr.type == IMSG_CTL_MONITOR)
				retval = npppd_ctl_monitor(c->ctx);
			else
				retval = npppd_ctl_who_and_monitor(c->ctx);
			imsg_compose(&c->iev.ibuf,
			    (retval == 0)? IMSG_CTL_OK : IMSG_CTL_FAIL, 0, 0,
			    -1, NULL, 0);
			break;

		case IMSG_CTL_DISCONNECT:
		    {
			struct npppd_disconnect_request  *req;
			struct npppd_disconnect_response  res;

			req = (struct npppd_disconnect_request *)imsg.data;
			retval = npppd_ctl_disconnect(c->ctx,
			    req->ppp_id, req->count);
			res.count = retval;
			imsg_compose(&c->iev.ibuf, IMSG_CTL_OK, 0, 0,
			    -1, &res, sizeof(res));
			break;
		    }
		default:
			imsg_compose(&c->iev.ibuf, IMSG_CTL_FAIL, 0, 0, -1,
			    NULL, 0);
			break;
		}
		imsg_free(&imsg);
	}
	if (imsgbuf_queuelen(&c->iev.ibuf) == 0)
		npppd_ctl_imsg_compose(c->ctx, &c->iev.ibuf);
	imsg_event_add(&c->iev);
}

void
control_imsg_forward(struct imsg *imsg)
{
	struct ctl_conn *c;

	TAILQ_FOREACH(c, &ctl_conns, entry)
		if (c->flags & CTL_CONN_NOTIFY)
			imsg_compose(&c->iev.ibuf, imsg->hdr.type, 0,
			    imsg->hdr.pid, -1, imsg->data,
			    imsg->hdr.len - IMSG_HEADER_SIZE);
}
