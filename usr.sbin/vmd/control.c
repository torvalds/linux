/*	$OpenBSD: control.c,v 1.52 2025/08/13 10:26:31 dv Exp $	*/

/*
 * Copyright (c) 2010-2015 Reyk Floeter <reyk@openbsd.org>
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
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <event.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "proc.h"
#include "vmd.h"

#define	CONTROL_BACKLOG	5

struct ctl_connlist ctl_conns = TAILQ_HEAD_INITIALIZER(ctl_conns);

struct ctl_notify {
	int			ctl_fd;
	uint32_t		ctl_vmid;
	TAILQ_ENTRY(ctl_notify)	entry;
};
TAILQ_HEAD(ctl_notify_q, ctl_notify) ctl_notify_q =
	TAILQ_HEAD_INITIALIZER(ctl_notify_q);
void
	 control_accept(int, short, void *);
struct ctl_conn
	*control_connbyfd(int);
void	 control_close(int, struct control_sock *);
void	 control_dispatch_imsg(int, short, void *);
int	 control_dispatch_vmd(int, struct privsep_proc *, struct imsg *);
void	 control_run(struct privsep *, struct privsep_proc *, void *);

static struct privsep_proc procs[] = {
	{ "parent",	PROC_PARENT,	control_dispatch_vmd }
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
	 * recvfd - for the proc fd exchange.
	 * sendfd - for send and receive.
	 */
	if (pledge("stdio unix recvfd sendfd", NULL) == -1)
		fatal("pledge");

	/* Signal to the parent that we're done initializing. */
	proc_compose(ps, PROC_PARENT, IMSG_VMDOP_DONE, NULL, 0);
}

int
control_dispatch_vmd(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	struct ctl_conn		*c;
	struct ctl_notify	*notify = NULL, *notify_next;
	struct privsep		*ps = p->p_ps;
	struct vmop_result	 vmr;
	uint32_t		 peer_id, type;
	int			 waiting = 0;
	unsigned int		 mode;

	peer_id = imsg_get_id(imsg);
	type = imsg_get_type(imsg);

	switch (type) {
	case IMSG_VMDOP_START_VM_RESPONSE:
	case IMSG_VMDOP_PAUSE_VM_RESPONSE:
	case IMSG_VMDOP_UNPAUSE_VM_RESPONSE:
	case IMSG_VMDOP_GET_INFO_VM_DATA:
	case IMSG_VMDOP_GET_INFO_VM_END_DATA:
	case IMSG_CTL_FAIL:
	case IMSG_CTL_OK:
		/* Provide basic response back to a specific control client */
		if ((c = control_connbyfd(peer_id)) == NULL) {
			log_warnx("%s: lost control connection: fd %d",
			    __func__, peer_id);
			return (0);
		}
		imsg_forward_event(&c->iev, imsg);
		break;
	case IMSG_VMDOP_TERMINATE_VM_RESPONSE:
		vmop_result_read(imsg, &vmr);
		if ((c = control_connbyfd(peer_id)) == NULL) {
			log_warnx("%s: lost control connection: fd %d",
			    __func__, peer_id);
			return (0);
		}

		TAILQ_FOREACH(notify, &ctl_notify_q, entry) {
			if (notify->ctl_fd == (int) peer_id) {
				/*
				 * Update if waiting by vm name. This is only
				 * supported when stopping a single vm. If
				 * stopping all vms, vmctl(8) sends the request
				 * using the vmid.
				 */
				if (notify->ctl_vmid < 1)
					notify->ctl_vmid = vmr.vmr_id;
				waiting = 1;
				break;
			}
		}

		/* An error needs to be relayed to the client immediately */
		if (!waiting || vmr.vmr_result) {
			imsg_compose_event(&c->iev, type, 0, 0, -1, &vmr,
			    sizeof(vmr));

			if (notify) {
				TAILQ_REMOVE(&ctl_notify_q, notify, entry);
				free(notify);
			}
		}
		break;
	case IMSG_VMDOP_TERMINATE_VM_EVENT:
		/* Notify any waiting clients that a VM terminated */
		vmop_result_read(imsg, &vmr);

		TAILQ_FOREACH_SAFE(notify, &ctl_notify_q, entry, notify_next) {
			if (notify->ctl_vmid != vmr.vmr_id)
				continue;
			if ((c = control_connbyfd(notify->ctl_fd)) != NULL) {
				/* Forward to the vmctl(8) client */
				imsg_compose_event(&c->iev, type, 0, 0, -1,
				    &vmr, sizeof(vmr));
				TAILQ_REMOVE(&ctl_notify_q, notify, entry);
				free(notify);
			}
		}
		break;
	case IMSG_VMDOP_CONFIG:
		config_getconfig(ps->ps_env, imsg);
		proc_compose(ps, PROC_PARENT, IMSG_VMDOP_DONE, NULL, 0);
		break;
	case IMSG_CTL_RESET:
		mode = imsg_uint_read(imsg);
		config_purge(ps->ps_env, mode);
		break;
	default:
		return (-1);
	}

	return (0);
}

int
control_init(struct privsep *ps, struct control_sock *cs)
{
	struct sockaddr_un	 sun;
	int			 fd;
	mode_t			 old_umask, mode;

	if (cs->cs_name == NULL)
		return (0);

	if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0)) == -1) {
		log_warn("%s: socket", __func__);
		return (-1);
	}

	sun.sun_family = AF_UNIX;
	if (strlcpy(sun.sun_path, cs->cs_name,
	    sizeof(sun.sun_path)) >= sizeof(sun.sun_path)) {
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

	if (bind(fd, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
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
	cs->cs_env = ps;

	return (0);
}

int
control_reset(struct control_sock *cs)
{
	/* Updating owner of the control socket */
	if (chown(cs->cs_name, cs->cs_uid, cs->cs_gid) == -1)
		return (-1);

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

	event_set(&cs->cs_ev, cs->cs_fd, EV_READ, control_accept, cs);
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

	if (getsockopt(connfd, SOL_SOCKET, SO_PEERCRED,
	    &c->peercred, &len) != 0) {
		log_warn("%s: failed to get peer credentials", __func__);
		close(connfd);
		free(c);
		return;
	}

	if (imsgbuf_init(&c->iev.ibuf, connfd) == -1) {
		log_warn("%s: failed to init imsgbuf", __func__);
		close(connfd);
		free(c);
		return;
	}
	imsgbuf_allow_fdpass(&c->iev.ibuf);
	c->iev.handler = control_dispatch_imsg;
	c->iev.events = EV_READ;
	c->iev.data = cs;
	event_set(&c->iev.ev, c->iev.ibuf.fd, c->iev.events,
	    c->iev.handler, c->iev.data);
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
	struct ctl_conn		*c;
	struct ctl_notify	*notify, *notify_next;

	if ((c = control_connbyfd(fd)) == NULL) {
		log_warn("%s: fd %d: not found", __func__, fd);
		return;
	}

	imsgbuf_clear(&c->iev.ibuf);
	TAILQ_REMOVE(&ctl_conns, c, entry);

	TAILQ_FOREACH_SAFE(notify, &ctl_notify_q, entry, notify_next) {
		if (notify->ctl_fd == fd) {
			TAILQ_REMOVE(&ctl_notify_q, notify, entry);
			free(notify);
			break;
		}
	}

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
	struct control_sock		*cs = arg;
	struct privsep			*ps = cs->cs_env;
	struct ctl_conn			*c;
	struct imsg			 imsg;
	struct vmop_create_params	 vmc;
	struct vmop_id			 vid;
	struct ctl_notify		*notify;
	int				 n, v, wait = 0, ret = 0;
	uint32_t			 peer_id = fd, type;

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

		type = imsg_get_type(&imsg);

		switch (type) {
		case IMSG_VMDOP_GET_INFO_VM_REQUEST:
		case IMSG_VMDOP_WAIT_VM_REQUEST:
		case IMSG_VMDOP_TERMINATE_VM_REQUEST:
		case IMSG_VMDOP_START_VM_REQUEST:
		case IMSG_VMDOP_PAUSE_VM:
		case IMSG_VMDOP_UNPAUSE_VM:
			break;
		default:
			if (c->peercred.uid != 0) {
				log_warnx("denied request %d from uid %d",
				    type, c->peercred.uid);
				ret = EPERM;
				goto fail;
			}
			break;
		}

		switch (type) {
		case IMSG_CTL_VERBOSE:
			v = imsg_int_read(&imsg);
			log_setverbose(v);
			if (proc_compose_imsg(ps, PROC_PARENT, type,
			    peer_id, -1, &v, sizeof(v)))
				goto fail;
			break;
		case IMSG_CTL_RESET:
		case IMSG_VMDOP_LOAD:
		case IMSG_VMDOP_RELOAD:
			if (proc_forward_imsg(ps, &imsg, PROC_PARENT, peer_id))
				goto fail;
			break;
		case IMSG_VMDOP_START_VM_REQUEST:
			vmop_create_params_read(&imsg, &vmc);
			vmc.vmc_owner.uid = c->peercred.uid;
			vmc.vmc_owner.gid = -1;

			/* imsg passed fd may contain kernel image fd. */
			if (proc_compose_imsg(ps, PROC_PARENT, type,
			    peer_id, imsg_get_fd(&imsg), &vmc,
			    sizeof(vmc)) == -1) {
				control_close(fd, cs);
				return;
			}
			break;
		case IMSG_VMDOP_WAIT_VM_REQUEST:
			wait = 1;
			/* FALLTHROUGH */
		case IMSG_VMDOP_TERMINATE_VM_REQUEST:
			vmop_id_read(&imsg, &vid);
			vid.vid_uid = c->peercred.uid;

			if (wait || vid.vid_flags & VMOP_WAIT) {
				vid.vid_flags |= VMOP_WAIT;
				notify = calloc(1, sizeof(struct ctl_notify));
				if (notify == NULL)
					fatal("%s: calloc", __func__);
				notify->ctl_vmid = vid.vid_id;
				notify->ctl_fd = fd;
				TAILQ_INSERT_TAIL(&ctl_notify_q, notify, entry);
				log_debug("%s: registered wait for peer %d",
				    __func__, fd);
			}

			if (proc_compose_imsg(ps, PROC_PARENT, type,
			    peer_id, -1, &vid, sizeof(vid))) {
				log_debug("%s: proc_compose_imsg failed",
				    __func__);
				control_close(fd, cs);
				return;
			}
			break;
		case IMSG_VMDOP_GET_INFO_VM_REQUEST:
			if (proc_compose_imsg(ps, PROC_PARENT, type,
			    peer_id, -1, NULL, 0)) {
				control_close(fd, cs);
				return;
			}
			break;
		case IMSG_VMDOP_PAUSE_VM:
		case IMSG_VMDOP_UNPAUSE_VM:
			vmop_id_read(&imsg, &vid);
			vid.vid_uid = c->peercred.uid;
			log_debug("%s id: %d, name: %s, uid: %d", __func__,
			    vid.vid_id, vid.vid_name, vid.vid_uid);

			if (proc_compose_imsg(ps, PROC_PARENT, type,
			    peer_id, imsg_get_fd(&imsg), &vid, sizeof(vid)))
				goto fail;
			break;
		default:
			log_debug("%s: error handling imsg %d", __func__, type);
			control_close(fd, cs);
			break;
		}
		imsg_free(&imsg);
	}

	imsg_event_add(&c->iev);
	return;

 fail:
	if (ret == 0)
		ret = EINVAL;
	imsg_compose_event(&c->iev, IMSG_CTL_FAIL, 0, 0, -1, &ret, sizeof(ret));
	imsgbuf_flush(&c->iev.ibuf);
	control_close(fd, cs);
}
