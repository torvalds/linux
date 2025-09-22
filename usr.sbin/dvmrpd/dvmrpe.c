/*	$OpenBSD: dvmrpe.c,v 1.30 2024/11/21 13:38:14 claudio Exp $ */

/*
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
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
#include <net/if_types.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <pwd.h>
#include <unistd.h>
#include <event.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>

#include "igmp.h"
#include "dvmrp.h"
#include "dvmrpd.h"
#include "dvmrpe.h"
#include "control.h"
#include "log.h"

void		 dvmrpe_sig_handler(int, short, void *);
__dead void	 dvmrpe_shutdown(void);

volatile sig_atomic_t	 dvmrpe_quit = 0;
struct dvmrpd_conf	*deconf = NULL;
static struct imsgev	*iev_main;
static struct imsgev	*iev_rde;

void
dvmrpe_sig_handler(int sig, short event, void *bula)
{
	switch (sig) {
	case SIGINT:
	case SIGTERM:
		dvmrpe_shutdown();
		/* NOTREACHED */
	default:
		fatalx("unexpected signal");
	}
}

/* dvmrp engine */
pid_t
dvmrpe(struct dvmrpd_conf *xconf, int pipe_parent2dvmrpe[2],
    int pipe_dvmrpe2rde[2], int pipe_parent2rde[2])
{
	struct iface	*iface = NULL;
	struct passwd	*pw;
	struct event	 ev_sigint, ev_sigterm;
	pid_t		 pid;

	switch (pid = fork()) {
	case -1:
		fatal("cannot fork");
	case 0:
		break;
	default:

		return (pid);
	}

	/* create the raw ip socket */
	if ((xconf->dvmrp_socket = socket(AF_INET,
	    SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    IPPROTO_IGMP)) == -1)
		fatal("error creating raw socket");

	/* create dvmrpd control socket outside chroot */
	if (control_init() == -1)
		fatalx("control socket setup failed");

	/* set some defaults */
	if (if_set_mcast_ttl(xconf->dvmrp_socket,
	    IP_DEFAULT_MULTICAST_TTL) == -1)
		fatal("if_set_mcast_ttl");

	if (if_set_mcast_loop(xconf->dvmrp_socket) == -1)
		fatal("if_set_mcast_loop");

	if (if_set_tos(xconf->dvmrp_socket, IPTOS_PREC_INTERNETCONTROL) == -1)
		fatal("if_set_tos");

	if_set_recvbuf(xconf->dvmrp_socket);

	deconf = xconf;

	if ((pw = getpwnam(DVMRPD_USER)) == NULL)
		fatal("getpwnam");

	if (chroot(pw->pw_dir) == -1)
		fatal("chroot");
	if (chdir("/") == -1)
		fatal("chdir(\"/\")");

	setproctitle("dvmrp engine");
	log_procname = "dvmrpe";

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	event_init();
	nbr_init(NBR_HASHSIZE);

	/* setup signal handler */
	signal_set(&ev_sigint, SIGINT, dvmrpe_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, dvmrpe_sig_handler, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal(SIGPIPE, SIG_IGN);

	/* setup pipes */
	close(pipe_parent2dvmrpe[0]);
	close(pipe_dvmrpe2rde[1]);
	close(pipe_parent2rde[0]);
	close(pipe_parent2rde[1]);

	if ((iev_rde = malloc(sizeof(struct imsgev))) == NULL ||
	    (iev_main = malloc(sizeof(struct imsgev))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(&iev_rde->ibuf, pipe_dvmrpe2rde[0]) == -1)
		fatal(NULL);
	iev_rde->handler = dvmrpe_dispatch_rde;

	if (imsgbuf_init(&iev_main->ibuf, pipe_parent2dvmrpe[1]) == -1)
		fatal(NULL);
	iev_main->handler = dvmrpe_dispatch_main;

	/* setup event handler */
	iev_rde->events = EV_READ;
	event_set(&iev_rde->ev, iev_rde->ibuf.fd, iev_rde->events,
	    iev_rde->handler, iev_rde);
	event_add(&iev_rde->ev, NULL);

	iev_main->events = EV_READ;
	event_set(&iev_main->ev, iev_main->ibuf.fd, iev_main->events,
	    iev_main->handler, iev_main);
	event_add(&iev_main->ev, NULL);

	event_set(&deconf->ev, deconf->dvmrp_socket, EV_READ|EV_PERSIST,
	    recv_packet, deconf);
	event_add(&deconf->ev, NULL);

	/* listen on dvmrpd control socket */
	control_listen();

	/* start interfaces */
	LIST_FOREACH(iface, &deconf->iface_list, entry) {
		if_init(xconf, iface);
		if (if_fsm(iface, IF_EVT_UP)) {
			log_debug("error starting interface %s", iface->name);
		}
	}

	evtimer_set(&deconf->report_timer, report_timer, deconf);
	start_report_timer();

	event_dispatch();

	dvmrpe_shutdown();
	/* NOTREACHED */
	return (0);
}

__dead void
dvmrpe_shutdown(void)
{
	struct iface	*iface;

	/* close pipes */
	imsgbuf_write(&iev_rde->ibuf);
	imsgbuf_clear(&iev_rde->ibuf);
	close(iev_rde->ibuf.fd);
	imsgbuf_write(&iev_main->ibuf);
	imsgbuf_clear(&iev_main->ibuf);
	close(iev_main->ibuf.fd);

	/* stop all interfaces and delete them */
	LIST_FOREACH(iface, &deconf->iface_list, entry) {
		if (if_fsm(iface, IF_EVT_DOWN)) {
			log_debug("error stopping interface %s",
			    iface->name);
		}
		if_del(iface);
	}

	/* clean up */
	free(iev_rde);
	free(iev_main);

	log_info("dvmrp engine exiting");
	_exit(0);
}

int
dvmrpe_imsg_compose_parent(int type, pid_t pid, void *data, u_int16_t datalen)
{
	return (imsg_compose_event(iev_main, type, 0, pid, -1, data, datalen));
}

int
dvmrpe_imsg_compose_rde(int type, u_int32_t peerid, pid_t pid,
    void *data, u_int16_t datalen)
{
	return (imsg_compose_event(iev_rde, type, peerid, pid,
	     -1, data, datalen));
}

void
dvmrpe_dispatch_main(int fd, short event, void *bula)
{
	struct imsg	 imsg;
	struct imsgev	*iev = bula;
	struct imsgbuf  *ibuf = &iev->ibuf;
	struct kif	*kif;
	struct iface	*iface;
	ssize_t		 n;
	int		 shut = 0, link_ok;

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
			fatal("dvmrpe_dispatch_main: imsg_get error");
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_IFINFO:
			if (imsg.hdr.len - IMSG_HEADER_SIZE !=
			    sizeof(struct kif))
				fatalx("IFINFO imsg with wrong len");
			kif = imsg.data;
			link_ok = (kif->flags & IFF_UP) &&
			    LINK_STATE_IS_UP(kif->link_state);

			LIST_FOREACH(iface, &deconf->iface_list, entry) {
				if (kif->ifindex == iface->ifindex) {
					iface->flags = kif->flags;
					iface->linkstate = kif->link_state;

					if (link_ok) {
						if_fsm(iface, IF_EVT_UP);
						log_warnx("interface %s up",
						    iface->name);
					} else {
						if_fsm(iface, IF_EVT_DOWN);
						log_warnx("interface %s down",
						    iface->name);
					}
				}
			}
			break;
		default:
			log_debug("dvmrpe_dispatch_main: error handling "
			    "imsg %d", imsg.hdr.type);
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

void
dvmrpe_dispatch_rde(int fd, short event, void *bula)
{
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf = &iev->ibuf;
	struct imsg		 imsg;
	struct nbr		*nbr;
	struct prune		 p;
	struct iface		*iface;
	struct route_report	*rr;
	ssize_t			 n;
	int			 shut = 0;

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
			fatal("dvmrpe_dispatch_rde: imsgbuf_get error");
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_CTL_SHOW_RIB:
		case IMSG_CTL_SHOW_SUM:
		case IMSG_CTL_SHOW_MFC:
		case IMSG_CTL_END:
			control_imsg_relay(&imsg);
			break;
		case IMSG_FULL_ROUTE_REPORT:
			/* add route reports to list */
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(*rr))
				fatalx("invalid size of RDE request");

			if ((rr = calloc(1, sizeof(*rr))) == NULL)
				fatal("dvmrpe_dispatch_rde");

			memcpy(rr, imsg.data, sizeof(*rr));

			/* general update, per interface */
			if (imsg.hdr.peerid == 0) {
				/* add to interface list */
				LIST_FOREACH(iface, &deconf->iface_list,
				    entry) {
					if (!if_nbr_list_empty(iface))
						rr_list_add(&iface->rr_list,
						    rr);
				}
				break;
			}

			/* add to neighbor list */
			nbr = nbr_find_peerid(imsg.hdr.peerid);
			rr_list_add(&nbr->rr_list, rr);
			break;
		case IMSG_FULL_ROUTE_REPORT_END:
			/* transmit route report */
			if (imsg.hdr.peerid == 0) {
				/*
				 * send general route report on all
				 * interfaces with neighbors.
				 */
				LIST_FOREACH(iface, &deconf->iface_list,
				    entry) {
					rr_list_send(&iface->rr_list,
					    iface, NULL);
				}
				break;
			}

			nbr = nbr_find_peerid(imsg.hdr.peerid);
			rr_list_send(&nbr->rr_list, NULL, nbr);
			break;
		case IMSG_SEND_PRUNE:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(p))
				fatalx("invalid size of RDE request");

			memcpy(&p, imsg.data, sizeof(p));

			LIST_FOREACH(iface, &deconf->iface_list, entry)
				if (p.ifindex == iface->ifindex)
					break;

			if (iface == NULL)
				fatalx("invalid interface in mfc");

			nbr = nbr_find_ip(iface, p.nexthop.s_addr);
			if (nbr == NULL)
				fatalx("unknown neighbor to send prune");

			send_prune(nbr, &p);

			break;
		case IMSG_FLASH_UPDATE:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(*rr))
				fatalx("invalid size of RDE request");

			if ((rr = calloc(1, sizeof(*rr))) == NULL)
				fatal("dvmrpe_dispatch_rde");

			memcpy(rr, imsg.data, sizeof(*rr));

			LIST_FOREACH(iface, &deconf->iface_list, entry) {
				if (!if_nbr_list_empty(iface)) {
					rr_list_add(&iface->rr_list, rr);
					rr_list_send(&iface->rr_list, iface,
					    NULL);
				}
			}
			break;
		case IMSG_FLASH_UPDATE_DS:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(*rr))
				fatalx("invalid size of RDE request");

			if ((rr = calloc(1, sizeof(*rr))) == NULL)
				fatal("dvmrpe_dispatch_rde");

			memcpy(rr, imsg.data, sizeof(*rr));

			LIST_FOREACH(iface, &deconf->iface_list, entry) {
				if (iface->ifindex == rr->ifindex)
					continue;
				if (!if_nbr_list_empty(iface)) {
					rr_list_add(&iface->rr_list, rr);
					rr_list_send(&iface->rr_list, iface,
					    NULL);
				}
			}
			break;
		default:
			log_debug("dvmrpe_dispatch_rde: error handling imsg %d",
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

void
dvmrpe_iface_ctl(struct ctl_conn *c, unsigned int idx)
{
	struct iface		*iface;
	struct ctl_iface	*ictl;

	LIST_FOREACH(iface, &deconf->iface_list, entry)
		if (idx == 0 || idx == iface->ifindex) {
			ictl = if_to_ctl(iface);
			imsg_compose_event(&c->iev, IMSG_CTL_SHOW_IFACE,
			    0, 0, -1, ictl, sizeof(struct ctl_iface));
		}
}

void
dvmrpe_iface_igmp_ctl(struct ctl_conn *c, unsigned int idx)
{
	struct iface		*iface;
	struct ctl_iface	*ictl;

	LIST_FOREACH(iface, &deconf->iface_list, entry)
		if (idx == 0 || idx == iface->ifindex) {
			ictl = if_to_ctl(iface);
			imsg_compose_event(&c->iev, IMSG_CTL_SHOW_IFACE,
			    0, 0, -1, ictl, sizeof(struct ctl_iface));
			group_list_dump(iface, c);

		}
}

void
dvmrpe_nbr_ctl(struct ctl_conn *c)
{
	struct iface	*iface;
	struct nbr	*nbr;
	struct ctl_nbr	*nctl;

	LIST_FOREACH(iface, &deconf->iface_list, entry)
		LIST_FOREACH(nbr, &iface->nbr_list, entry) {
			nctl = nbr_to_ctl(nbr);
			imsg_compose_event(&c->iev, IMSG_CTL_SHOW_NBR,
			    0, 0, -1, nctl, sizeof(struct ctl_nbr));
		}

	imsg_compose_event(&c->iev, IMSG_CTL_END, 0, 0, -1, NULL, 0);
}
