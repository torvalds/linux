/*	$OpenBSD: rde.c,v 1.39 2024/11/21 13:38:14 claudio Exp $ */

/*
 * Copyright (c) 2004, 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2005, 2006 Esben Norby <norby@openbsd.org>
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
#include <sys/socket.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>
#include <event.h>

#include "igmp.h"
#include "dvmrp.h"
#include "dvmrpd.h"
#include "dvmrpe.h"
#include "log.h"
#include "rde.h"

void		 rde_sig_handler(int sig, short, void *);
__dead void	 rde_shutdown(void);
void		 rde_dispatch_imsg(int, short, void *);

int		 rde_select_ds_ifs(struct mfc *, struct iface *);

volatile sig_atomic_t	 rde_quit = 0;
struct dvmrpd_conf	*rdeconf = NULL;
struct rde_nbr		*nbrself;
static struct imsgev	*iev_dvmrpe;
static struct imsgev	*iev_main;

void
rde_sig_handler(int sig, short event, void *arg)
{
	/*
	 * signal handler rules don't apply, libevent decouples for us
	 */

	switch (sig) {
	case SIGINT:
	case SIGTERM:
		rde_shutdown();
		/* NOTREACHED */
	default:
		fatalx("unexpected signal");
	}
}

/* route decision engine */
pid_t
rde(struct dvmrpd_conf *xconf, int pipe_parent2rde[2], int pipe_dvmrpe2rde[2],
    int pipe_parent2dvmrpe[2])
{
	struct passwd		*pw;
	struct event		 ev_sigint, ev_sigterm;
	pid_t			 pid;

	switch (pid = fork()) {
	case -1:
		fatal("cannot fork");
	case 0:
		break;
	default:
		return (pid);
	}

	rdeconf = xconf;

	if ((pw = getpwnam(DVMRPD_USER)) == NULL)
		fatal("getpwnam");

	if (chroot(pw->pw_dir) == -1)
		fatal("chroot");
	if (chdir("/") == -1)
		fatal("chdir(\"/\")");

	setproctitle("route decision engine");
	log_procname = "rde";

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	event_init();

	/* setup signal handler */
	signal_set(&ev_sigint, SIGINT, rde_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, rde_sig_handler, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal(SIGPIPE, SIG_IGN);

	/* setup pipes */
	close(pipe_dvmrpe2rde[0]);
	close(pipe_parent2rde[0]);
	close(pipe_parent2dvmrpe[0]);
	close(pipe_parent2dvmrpe[1]);

	if ((iev_dvmrpe = malloc(sizeof(struct imsgev))) == NULL ||
	    (iev_main = malloc(sizeof(struct imsgev))) == NULL)
		fatal(NULL);

	if (imsgbuf_init(&iev_dvmrpe->ibuf, pipe_dvmrpe2rde[1]) == -1)
		fatal(NULL);
	iev_dvmrpe->handler = rde_dispatch_imsg;

	if (imsgbuf_init(&iev_main->ibuf, pipe_parent2rde[1]) == -1)
		fatal(NULL);
	iev_main->handler = rde_dispatch_imsg;

	/* setup event handler */
	iev_dvmrpe->events = EV_READ;
	event_set(&iev_dvmrpe->ev, iev_dvmrpe->ibuf.fd, iev_dvmrpe->events,
	    iev_dvmrpe->handler, iev_dvmrpe);
	event_add(&iev_dvmrpe->ev, NULL);

	iev_main->events = EV_READ;
	event_set(&iev_main->ev, iev_main->ibuf.fd, iev_main->events,
	    iev_main->handler, iev_main);
	event_add(&iev_main->ev, NULL);

	rt_init();
	mfc_init();

	event_dispatch();

	rde_shutdown();
	/* NOTREACHED */
	return (0);
}

__dead void
rde_shutdown(void)
{
	struct iface	*iface;

	/* close pipes */
	imsgbuf_clear(&iev_dvmrpe->ibuf);
	close(iev_dvmrpe->ibuf.fd);
	imsgbuf_clear(&iev_main->ibuf);
	close(iev_main->ibuf.fd);

	rt_clear();
	mfc_clear();

	LIST_FOREACH(iface, &rdeconf->iface_list, entry) {
		if_del(iface);
	}

	free(iev_dvmrpe);
	free(iev_main);
	free(rdeconf);

	log_info("route decision engine exiting");
	_exit(0);
}

/* imesg */
int
rde_imsg_compose_parent(int type, pid_t pid, void *data, u_int16_t datalen)
{
	return (imsg_compose_event(iev_main, type, 0, pid, -1, data, datalen));
}

int
rde_imsg_compose_dvmrpe(int type, u_int32_t peerid, pid_t pid, void *data,
    u_int16_t datalen)
{
	return (imsg_compose_event(iev_dvmrpe, type, peerid, pid, -1,
	     data, datalen));
}

void
rde_dispatch_imsg(int fd, short event, void *bula)
{
	struct mfc		 mfc;
	struct prune		 p;
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf = &iev->ibuf;
	struct imsg		 imsg;
	struct route_report	 rr;
	struct nbr_msg		 nm;
	int			 i, connected = 0, shut = 0, verbose;
	ssize_t			 n;
	struct iface		*iface;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("imsgbuf_read error");
		if (n == 0)	/* connection closed */
			shut = 1;
	}
	if (event & EV_WRITE) {
		if (imsgbuf_write(ibuf) == -1) {
			if (errno == EPIPE)	/* connection closed */
				shut = 1;
			else
				fatal("imsgbuf_write");
		}
	}

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			fatal("rde_dispatch_imsg: imsg_get error");
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_CTL_SHOW_RIB:
			rt_dump(imsg.hdr.pid);
			imsg_compose_event(iev_dvmrpe, IMSG_CTL_END, 0,
			    imsg.hdr.pid, -1, NULL, 0);
			break;
		case IMSG_CTL_SHOW_MFC:
			mfc_dump(imsg.hdr.pid);
			imsg_compose_event(iev_dvmrpe, IMSG_CTL_END, 0,
			    imsg.hdr.pid, -1, NULL, 0);
			break;
		case IMSG_ROUTE_REPORT:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(rr))
				fatalx("invalid size of OE request");
			memcpy(&rr, imsg.data, sizeof(rr));

			/* directly connected networks from parent */
			if (imsg.hdr.peerid == 0)
				connected = 1;

			if (srt_check_route(&rr, connected) == -1)
				log_debug("rde_dispatch_imsg: "
				    "packet malformed");
			break;
		case IMSG_FULL_ROUTE_REPORT:
			rt_snap(imsg.hdr.peerid);
			rde_imsg_compose_dvmrpe(IMSG_FULL_ROUTE_REPORT_END,
			    imsg.hdr.peerid, 0, NULL, 0);
			break;
		case IMSG_MFC_ADD:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(mfc))
				fatalx("invalid size of OE request");
			memcpy(&mfc, imsg.data, sizeof(mfc));
#if 1
			for (i = 0; i < MAXVIFS; i++)
				mfc.ttls[i] = 0;

			LIST_FOREACH(iface, &rdeconf->iface_list, entry) {
				if (rde_select_ds_ifs(&mfc, iface))
					mfc.ttls[iface->ifindex] = 1;
			}

			mfc_update(&mfc);
#endif
			break;
		case IMSG_MFC_DEL:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(mfc))
				fatalx("invalid size of OE request");
			memcpy(&mfc, imsg.data, sizeof(mfc));
#if 1
			mfc_delete(&mfc);
#endif
			break;
		case IMSG_GROUP_ADD:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(mfc))
				fatalx("invalid size of OE request");
			memcpy(&mfc, imsg.data, sizeof(mfc));

			iface = if_find_index(mfc.ifindex);
			if (iface == NULL) {
				fatalx("rde_dispatch_imsg: "
				    "cannot find matching interface");
			}

			rde_group_list_add(iface, mfc.group);
			break;
		case IMSG_GROUP_DEL:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(mfc))
				fatalx("invalid size of OE request");
			memcpy(&mfc, imsg.data, sizeof(mfc));

			iface = if_find_index(mfc.ifindex);
			if (iface == NULL) {
				fatalx("rde_dispatch_imsg: "
				    "cannot find matching interface");
			}

			rde_group_list_remove(iface, mfc.group);
			break;
		case IMSG_NBR_DEL:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(nm))
				fatalx("invalid size of OE request");

			memcpy(&nm, imsg.data, sizeof(nm));

			srt_expire_nbr(nm.address, nm.ifindex);
			break;
		case IMSG_RECV_PRUNE:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(p))
				fatalx("invalid size of OE request");
			memcpy(&p, imsg.data, sizeof(p));

			mfc_recv_prune(&p);
			break;
		case IMSG_CTL_LOG_VERBOSE:
			/* already checked by dvmrpe */
			memcpy(&verbose, imsg.data, sizeof(verbose));
			log_verbose(verbose);
			break;
		default:
			log_debug("rde_dispatch_msg: unexpected imsg %d",
			    imsg.hdr.type);
			break;
		}
		imsg_free(&imsg);
	}
	if (!shut)
		imsg_event_add(iev);
	else {
		/* this pipe is dead, so remove the event handler */
		event_del(&iev->ev);
		event_loopexit(NULL);
	}
}

int
rde_select_ds_ifs(struct mfc *mfc, struct iface *iface)
{
	struct rt_node	*rn;

	if (mfc->ifindex == iface->ifindex)
		return (0);

	if (rde_group_list_find(iface, mfc->group))
		return (1);

	rn = rt_match_origin(mfc->origin.s_addr);
	if (rn == NULL) {
		log_debug("rde_selected_ds_iface: no information about "
		    "the origin %s", inet_ntoa(mfc->origin));
		return (0);
	}

	if (rn->ds_cnt[iface->ifindex] != 0)
		return (1);

	return (0);
}

/* rde group functions */
void
rde_group_list_add(struct iface *iface, struct in_addr group)
{
	struct rde_group	*rdegrp;

	/* validate group id */
	if (!IN_MULTICAST(htonl(group.s_addr))) {
		log_debug("rde_group_list_add: interface %s, %s is not a "
		    "multicast address", iface->name,
		    inet_ntoa(group));
		return;
	}

	if (rde_group_list_find(iface, group))
		return;

	rdegrp = calloc(1, sizeof(*rdegrp));
	if (rdegrp == NULL)
		fatal("rde_group_list_add");

	rdegrp->rde_group.s_addr = group.s_addr;

	TAILQ_INSERT_TAIL(&iface->rde_group_list, rdegrp, entry);

	log_debug("rde_group_list_add: interface %s, group %s", iface->name,
	    inet_ntoa(rdegrp->rde_group));

	return;
}

int
rde_group_list_find(struct iface *iface, struct in_addr group)
{
	struct rde_group	*rdegrp = NULL;

	/* validate group id */
	if (!IN_MULTICAST(htonl(group.s_addr))) {
		log_debug("rde_group_list_find: interface %s, %s is not a "
		    "multicast address", iface->name,
		    inet_ntoa(group));
		return (0);
	}

	TAILQ_FOREACH(rdegrp, &iface->rde_group_list, entry) {
		if (rdegrp->rde_group.s_addr == group.s_addr)
			return (1);
	}

	return (0);
}

void
rde_group_list_remove(struct iface *iface, struct in_addr group)
{
	struct rde_group	*rg, *nrg;
	struct rt_node		*rn;

	if (TAILQ_EMPTY(&iface->rde_group_list))
		fatalx("rde_group_list_remove: group does not exist");

	TAILQ_FOREACH_SAFE(rg, &iface->rde_group_list, entry, nrg) {
		if (rg->rde_group.s_addr == group.s_addr) {
			log_debug("group_list_remove: interface %s, group %s",
			    iface->name, inet_ntoa(rg->rde_group));
			TAILQ_REMOVE(&iface->rde_group_list, rg, entry);
			free(rg);
		}
	}

	rn = mfc_find_origin(group);
	if (rn == NULL)
		return;

	srt_check_downstream_ifaces(rn, iface);
}
