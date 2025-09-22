/*	$OpenBSD: control.c,v 1.18 2023/03/08 04:43:13 guenther Exp $	*/

/*
 * Copyright (c) 2010 Martin Hedenfalk <martin@bzero.se>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/tree.h>

#include <net/if.h>

#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ldapd.h"
#include "log.h"

#define	CONTROL_BACKLOG	5

struct ctl_connlist ctl_conns = TAILQ_HEAD_INITIALIZER(ctl_conns);

struct ctl_conn	*control_connbyfd(int);
void		 control_close(int, struct control_sock *);
static void	 control_imsgev(struct imsgev *iev, int code, struct imsg *imsg);
static void	 control_needfd(struct imsgev *iev);

void
control_init(struct control_sock *cs)
{
	struct sockaddr_un	 sun;
	int			 fd;
	mode_t			 old_umask, mode;

	if (cs->cs_name == NULL)
		return;

	if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0)) == -1)
		fatal("control_init: socket");

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	if (strlcpy(sun.sun_path, cs->cs_name,
	    sizeof(sun.sun_path)) >= sizeof(sun.sun_path))
		fatalx("control_init: name too long");

	if (connect(fd, (struct sockaddr *)&sun, sizeof(sun)) == 0)
		fatalx("control socket already listening");

	if (unlink(cs->cs_name) == -1 && errno != ENOENT)
		fatal("control_init: unlink");

	if (cs->cs_restricted) {
		old_umask = umask(S_IXUSR|S_IXGRP|S_IXOTH);
		mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH;
	} else {
		old_umask = umask(S_IXUSR|S_IXGRP|S_IWOTH|S_IROTH|S_IXOTH);
		mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP;
	}

	if (bind(fd, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
		(void)umask(old_umask);
		fatal("control_init: bind");
	}
	(void)umask(old_umask);

	if (chmod(cs->cs_name, mode) == -1) {
		(void)unlink(cs->cs_name);
		fatal("control_init: chmod");
	}

	cs->cs_fd = fd;
}

void
control_listen(struct control_sock *cs)
{
	if (cs->cs_name == NULL)
		return;

	if (listen(cs->cs_fd, CONTROL_BACKLOG) == -1)
		fatal("control_listen: listen");

	event_set(&cs->cs_ev, cs->cs_fd, EV_READ,
	    control_accept, cs);
	event_add(&cs->cs_ev, NULL);
	evtimer_set(&cs->cs_evt, control_accept, cs);
}

void
control_cleanup(struct control_sock *cs)
{
	if (cs->cs_name == NULL)
		return;
	event_del(&cs->cs_ev);
	event_del(&cs->cs_evt);
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
	if ((connfd = accept_reserve(listenfd,
	    (struct sockaddr *)&sun, &len, FD_RESERVE)) == -1) {
		/*
		 * Pause accept if we are out of file descriptors, or
		 * libevent will haunt us here too.
		 */
		if (errno == ENFILE || errno == EMFILE) {
			struct timeval evtpause = { 1, 0 };

			event_del(&cs->cs_ev);
			evtimer_add(&cs->cs_evt, &evtpause);
		} else if (errno != EWOULDBLOCK && errno != EINTR)
			log_warn("control_accept");
		return;
	}

	if ((c = calloc(1, sizeof(*c))) == NULL) {
		log_warn("control_accept");
		close(connfd);
		return;
	}

	log_debug("accepted control fd %d", connfd);
	TAILQ_INSERT_TAIL(&ctl_conns, c, entry);
	imsgev_init(&c->iev, connfd, cs, control_imsgev, control_needfd);
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
		log_warnx("control_close: fd %d: not found", fd);
		return;
	}

	log_debug("close control fd %d", c->iev.ibuf.fd);
	TAILQ_REMOVE(&ctl_conns, c, entry);
	imsgev_clear(&c->iev);

	/* Some file descriptors are available again. */
	if (evtimer_pending(&cs->cs_evt, NULL)) {
		evtimer_del(&cs->cs_evt);
		event_add(&cs->cs_ev, NULL);
	}

	free(c);
}

static int
send_stats(struct imsgev *iev)
{
	struct namespace	*ns;
	const struct btree_stat	*st;
	struct ns_stat		 nss;

	imsgev_compose(iev, IMSG_CTL_STATS, 0, iev->ibuf.pid, -1,
	    &stats, sizeof(stats));

	TAILQ_FOREACH(ns, &conf->namespaces, next) {
		if (namespace_has_referrals(ns))
			continue;
		memset(&nss, 0, sizeof(nss));
		strlcpy(nss.suffix, ns->suffix, sizeof(nss.suffix));
		if ((st = btree_stat(ns->data_db)) != NULL)
			bcopy(st, &nss.data_stat, sizeof(nss.data_stat));

		if ((st = btree_stat(ns->indx_db)) != NULL)
			bcopy(st, &nss.indx_stat, sizeof(nss.indx_stat));

		imsgev_compose(iev, IMSG_CTL_NSSTATS, 0, iev->ibuf.pid, -1,
		    &nss, sizeof(nss));
	}

	imsgev_compose(iev, IMSG_CTL_END, 0, iev->ibuf.pid, -1, NULL, 0);

	return 0;
}

static void
control_imsgev(struct imsgev *iev, int code, struct imsg *imsg)
{
	struct control_sock	*cs;
	struct ctl_conn		*c;
	int			 fd, verbose;

	cs = iev->data;
	fd = iev->ibuf.fd;

	if ((c = control_connbyfd(fd)) == NULL) {
		log_warnx("%s: fd %d: not found", __func__, fd);
		return;
	}

	if (code != IMSGEV_IMSG) {
		control_close(fd, cs);
		return;
	}

	log_debug("%s: got imsg %d on fd %d", __func__, imsg->hdr.type, fd);
	switch (imsg->hdr.type) {
	case IMSG_CTL_STATS:
		if (send_stats(iev) == -1) {
			log_debug("%s: failed to send statistics", __func__);
			control_close(fd, cs);
		}
		break;
	case IMSG_CTL_LOG_VERBOSE:
		if (imsg->hdr.len != IMSG_HEADER_SIZE + sizeof(verbose))
			break;

		bcopy(imsg->data, &verbose, sizeof(verbose));
		imsgev_compose(iev_ldapd, IMSG_CTL_LOG_VERBOSE, 0, 0, -1,
		    &verbose, sizeof(verbose));

		log_setverbose(verbose);
		break;
	default:
		log_warnx("%s: unexpected imsg %d", __func__, imsg->hdr.type);
		break;
	}
}

void
control_needfd(struct imsgev *iev)
{
	fatal("should never need an fd for control messages");
}

int
control_close_any(struct control_sock *cs)
{
	struct ctl_conn		*c;

	c = TAILQ_FIRST(&ctl_conns);
	if (c != NULL) {
		log_warn("closing oldest control connection");
		control_close(c->iev.ibuf.fd, cs);
		return (0);
	}
	log_warn("no control connections to close");
	return (-1);
}
