/*	$OpenBSD: control.c,v 1.45 2024/11/21 13:35:20 claudio Exp $	*/

/*
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
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
#include <sys/tree.h>

#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "iked.h"

#define	CONTROL_BACKLOG	5

struct ctl_connlist ctl_conns = TAILQ_HEAD_INITIALIZER(ctl_conns);
uint32_t ctl_peerid;

void
	 control_accept(int, short, void *);
struct ctl_conn
	*control_connbyfd(int);
void	 control_close(int, struct control_sock *);
void	 control_dispatch_imsg(int, short, void *);
void	 control_imsg_forward(struct imsg *);
void	 control_imsg_forward_peerid(struct imsg *);
void	 control_run(struct privsep *, struct privsep_proc *, void *);
int	 control_dispatch_ikev2(int, struct privsep_proc *, struct imsg *);
int	 control_dispatch_ca(int, struct privsep_proc *, struct imsg *);

static struct privsep_proc procs[] = {
	{ "parent",	PROC_PARENT, NULL },
	{ "ikev2",	PROC_IKEV2, control_dispatch_ikev2 },
	{ "ca",		PROC_CERT, control_dispatch_ca },
};

void
control(struct privsep *ps, struct privsep_proc *p)
{
	proc_run(ps, p, procs, nitems(procs), control_run, NULL);
}

void
control_run(struct privsep *ps, struct privsep_proc *p, void *arg)
{
	/*
	 * pledge in the control process:
	 * stdio - for malloc and basic I/O including events.
	 * unix - for the control socket.
	 */
	if (pledge("stdio unix recvfd", NULL) == -1)
		fatal("pledge");
}

int
control_init(struct privsep *ps, struct control_sock *cs)
{
	struct iked		*env = iked_env;
	struct sockaddr_un	 s_un;
	int			 fd;
	mode_t			 old_umask, mode;

	if (cs->cs_name == NULL)
		return (0);

	if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0)) == -1) {
		log_warn("%s: socket", __func__);
		return (-1);
	}

	s_un.sun_family = AF_UNIX;
	if (strlcpy(s_un.sun_path, cs->cs_name,
	    sizeof(s_un.sun_path)) >= sizeof(s_un.sun_path)) {
		log_warn("%s: %s name too long", __func__, cs->cs_name);
		close(fd);
		return (-1);
	}

	if (unlink(cs->cs_name) == -1)
		if (errno != ENOENT) {
			log_warn("%s: unlink %s", __func__, cs->cs_name);
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

	if (bind(fd, (struct sockaddr *)&s_un, sizeof(s_un)) == -1) {
		log_warn("%s: bind: %s", __func__, cs->cs_name);
		close(fd);
		(void)umask(old_umask);
		return (-1);
	}
	(void)umask(old_umask);

	if (chmod(cs->cs_name, mode) == -1) {
		log_warn("%s: chmod", __func__);
		close(fd);
		(void)unlink(cs->cs_name);
		return (-1);
	}

	cs->cs_fd = fd;
	cs->cs_env = env;

	return (0);
}

int
control_listen(struct control_sock *cs)
{
	if (cs->cs_name == NULL)
		return (0);

	if (listen(cs->cs_fd, CONTROL_BACKLOG) == -1) {
		log_warn("%s: listen", __func__);
		return (-1);
	}

	event_set(&cs->cs_ev, cs->cs_fd, EV_READ,
	    control_accept, cs);
	event_add(&cs->cs_ev, NULL);
	evtimer_set(&cs->cs_evt, control_accept, cs);

	return (0);
}

void
control_accept(int listenfd, short event, void *arg)
{
	struct control_sock	*cs = arg;
	int			 connfd;
	socklen_t		 len;
	struct sockaddr_un	 s_un;
	struct ctl_conn		*c;
	struct ctl_conn		*other;

	event_add(&cs->cs_ev, NULL);
	if ((event & EV_TIMEOUT))
		return;

	len = sizeof(s_un);
	if ((connfd = accept4(listenfd,
	    (struct sockaddr *)&s_un, &len, SOCK_NONBLOCK)) == -1) {
		/*
		 * Pause accept if we are out of file descriptors, or
		 * libevent will haunt us here too.
		 */
		if (errno == ENFILE || errno == EMFILE) {
			struct timeval evtpause = { 1, 0 };

			event_del(&cs->cs_ev);
			evtimer_add(&cs->cs_evt, &evtpause);
		} else if (errno != EWOULDBLOCK && errno != EINTR &&
		    errno != ECONNABORTED)
			log_warn("%s: accept", __func__);
		return;
	}

	if ((c = calloc(1, sizeof(struct ctl_conn))) == NULL) {
		log_warn("%s", __func__);
		close(connfd);
		return;
	}

	if (imsgbuf_init(&c->iev.ibuf, connfd) == -1) {
		log_warn("%s", __func__);
		close(connfd);
		free(c);
		return;
	}
	c->iev.handler = control_dispatch_imsg;
	c->iev.events = EV_READ;
	c->iev.data = cs;
	event_set(&c->iev.ev, c->iev.ibuf.fd, c->iev.events,
	    c->iev.handler, c->iev.data);
	event_add(&c->iev.ev, NULL);

	/* O(n^2), but n is small */
	c->peerid = ctl_peerid++;
	TAILQ_FOREACH(other, &ctl_conns, entry)
		if (c->peerid == other->peerid)
			c->peerid = ctl_peerid++;

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
		log_warn("%s: fd %d: not found", __func__, fd);
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

	free(c);
}

void
control_dispatch_imsg(int fd, short event, void *arg)
{
	struct control_sock	*cs = arg;
	struct iked		*env = cs->cs_env;
	struct ctl_conn		*c;
	struct imsg		 imsg;
	int			 n, v;

	if ((c = control_connbyfd(fd)) == NULL) {
		log_warn("%s: fd %d: not found", __func__, fd);
		return;
	}

	if (event & EV_READ) {
		if (imsgbuf_read(&c->iev.ibuf) != 1) {
			control_close(fd, cs);
			return;
		}
	}
	if (event & EV_WRITE) {
		if (imsgbuf_write(&c->iev.ibuf) == -1) {
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

		control_imsg_forward(&imsg);

		/* record peerid of connection for reply */
		imsg.hdr.peerid = c->peerid;

		switch (imsg.hdr.type) {
		case IMSG_CTL_NOTIFY:
			if (c->flags & CTL_CONN_NOTIFY) {
				log_debug("%s: "
				    "client requested notify more than once",
				    __func__);
				imsg_compose_event(&c->iev, IMSG_CTL_FAIL,
				    0, 0, -1, NULL, 0);
				break;
			}
			c->flags |= CTL_CONN_NOTIFY;
			break;
		case IMSG_CTL_VERBOSE:
			IMSG_SIZE_CHECK(&imsg, &v);

			memcpy(&v, imsg.data, sizeof(v));
			log_setverbose(v);

			proc_forward_imsg(&env->sc_ps, &imsg, PROC_PARENT, -1);
			break;
		case IMSG_CTL_RELOAD:
		case IMSG_CTL_RESET:
		case IMSG_CTL_COUPLE:
		case IMSG_CTL_DECOUPLE:
		case IMSG_CTL_ACTIVE:
		case IMSG_CTL_PASSIVE:
			proc_forward_imsg(&env->sc_ps, &imsg, PROC_PARENT, -1);
			break;
		case IMSG_CTL_RESET_ID:
			proc_forward_imsg(&env->sc_ps, &imsg, PROC_IKEV2, -1);
			break;
		case IMSG_CTL_SHOW_SA:
		case IMSG_CTL_SHOW_STATS:
			proc_forward_imsg(&env->sc_ps, &imsg, PROC_IKEV2, -1);
			break;
		case IMSG_CTL_SHOW_CERTSTORE:
			proc_forward_imsg(&env->sc_ps, &imsg, PROC_CERT, -1);
			break;
		default:
			log_debug("%s: error handling imsg %d",
			    __func__, imsg.hdr.type);
			break;
		}
		imsg_free(&imsg);
	}

	imsg_event_add(&c->iev);
}

void
control_imsg_forward(struct imsg *imsg)
{
	struct ctl_conn *c;

	TAILQ_FOREACH(c, &ctl_conns, entry)
		if (c->flags & CTL_CONN_NOTIFY)
			imsg_compose_event(&c->iev, imsg->hdr.type,
			    0, imsg->hdr.pid, -1, imsg->data,
			    imsg->hdr.len - IMSG_HEADER_SIZE);
}

void
control_imsg_forward_peerid(struct imsg *imsg)
{
	struct ctl_conn *c;

	TAILQ_FOREACH(c, &ctl_conns, entry)
		if (c->peerid == imsg->hdr.peerid)
			imsg_compose_event(&c->iev, imsg->hdr.type,
			    0, imsg->hdr.pid, -1, imsg->data,
			    imsg->hdr.len - IMSG_HEADER_SIZE);
}

int
control_dispatch_ikev2(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_SA:
	case IMSG_CTL_SHOW_STATS:
		control_imsg_forward_peerid(imsg);
		return (0);
	default:
		break;
	}

	return (-1);
}

int
control_dispatch_ca(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_CERTSTORE:
		control_imsg_forward_peerid(imsg);
		return (0);
	default:
		break;
	}

	return (-1);
}
