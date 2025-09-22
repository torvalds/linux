/*	$OpenBSD: ospfe.c,v 1.79 2025/07/22 18:39:19 jan Exp $ */

/*
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004 Esben Norby <norby@openbsd.org>
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

#include "ospf6.h"
#include "ospf6d.h"
#include "ospfe.h"
#include "rde.h"
#include "control.h"
#include "log.h"

void		 ospfe_sig_handler(int, short, void *);
__dead void	 ospfe_shutdown(void);
void		 orig_rtr_lsa_all(struct area *);
struct iface	*find_vlink(struct abr_rtr *);

struct ospfd_conf	*oeconf = NULL, *noeconf;
static struct imsgev	*iev_main;
static struct imsgev	*iev_rde;
int			 oe_nofib;

void
ospfe_sig_handler(int sig, short event, void *bula)
{
	switch (sig) {
	case SIGINT:
	case SIGTERM:
		ospfe_shutdown();
		/* NOTREACHED */
	default:
		fatalx("unexpected signal");
	}
}

/* ospf engine */
pid_t
ospfe(struct ospfd_conf *xconf, int pipe_parent2ospfe[2], int pipe_ospfe2rde[2],
    int pipe_parent2rde[2])
{
	struct area	*area;
	struct iface	*iface;
	struct passwd	*pw;
	struct event	 ev_sigint, ev_sigterm;
	pid_t		 pid;
	int		 pre = IPTOS_PREC_INTERNETCONTROL;

	switch (pid = fork()) {
	case -1:
		fatal("cannot fork");
	case 0:
		break;
	default:
		return (pid);
	}

	/* create the raw ip socket */
	if ((xconf->ospf_socket = socket(AF_INET6,
	    SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, IPPROTO_OSPF)) == -1)
		fatal("error creating raw socket");

	if (setsockopt(xconf->ospf_socket, IPPROTO_IPV6, IPV6_TCLASS, &pre,
	    sizeof(pre)) == -1)
		fatal("setsockopt IPV6_TCLASS");

	/* set some defaults */
	if (if_set_mcast_loop(xconf->ospf_socket) == -1)
		fatal("if_set_mcast_loop");
	if (if_set_ipv6_checksum(xconf->ospf_socket) == -1)
		fatal("if_set_ipv6_checksum");
	if (if_set_ipv6_pktinfo(xconf->ospf_socket, 1) == -1)
		fatal("if_set_ipv6_pktinfo");
	if_set_sockbuf(xconf->ospf_socket);

	oeconf = xconf;
	if (oeconf->flags & OSPFD_FLAG_NO_FIB_UPDATE)
		oe_nofib = 1;

	if ((pw = getpwnam(OSPF6D_USER)) == NULL)
		fatal("getpwnam");

	if (chroot(pw->pw_dir) == -1)
		fatal("chroot");
	if (chdir("/") == -1)
		fatal("chdir(\"/\")");

	setproctitle("ospf engine");
	/*
	 * XXX needed with fork+exec
	 * log_init(debug, LOG_DAEMON);
	 * log_setverbose(verbose);
	 */

	ospfd_process = PROC_OSPF_ENGINE;
	log_procinit(log_procnames[ospfd_process]);

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	if (pledge("stdio inet mcast recvfd", NULL) == -1)
		fatal("pledge");

	event_init();
	nbr_init(NBR_HASHSIZE);
	lsa_cache_init(LSA_HASHSIZE);

	/* setup signal handler */
	signal_set(&ev_sigint, SIGINT, ospfe_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, ospfe_sig_handler, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	/* setup pipes */
	close(pipe_parent2ospfe[0]);
	close(pipe_ospfe2rde[1]);
	close(pipe_parent2rde[0]);
	close(pipe_parent2rde[1]);

	if ((iev_rde = malloc(sizeof(struct imsgev))) == NULL ||
	    (iev_main = malloc(sizeof(struct imsgev))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(&iev_rde->ibuf, pipe_ospfe2rde[0]) == -1)
		fatal(NULL);
	iev_rde->handler = ospfe_dispatch_rde;
	if (imsgbuf_init(&iev_main->ibuf, pipe_parent2ospfe[1]) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(&iev_main->ibuf);
	iev_main->handler = ospfe_dispatch_main;

	/* setup event handler */
	iev_rde->events = EV_READ;
	event_set(&iev_rde->ev, iev_rde->ibuf.fd, iev_rde->events,
	    iev_rde->handler, iev_rde);
	event_add(&iev_rde->ev, NULL);

	iev_main->events = EV_READ;
	event_set(&iev_main->ev, iev_main->ibuf.fd, iev_main->events,
	    iev_main->handler, iev_main);
	event_add(&iev_main->ev, NULL);

	event_set(&oeconf->ev, oeconf->ospf_socket, EV_READ|EV_PERSIST,
	    recv_packet, oeconf);
	event_add(&oeconf->ev, NULL);

	/* remove unneeded config stuff */
	conf_clear_redist_list(&oeconf->redist_list);

	/* start interfaces */
	LIST_FOREACH(area, &oeconf->area_list, entry) {
		ospfe_demote_area(area, 0);
		LIST_FOREACH(iface, &area->iface_list, entry)
			if_start(xconf, iface);
	}

	event_dispatch();

	ospfe_shutdown();
	/* NOTREACHED */
	return (0);
}

__dead void
ospfe_shutdown(void)
{
	struct area	*area;
	struct iface	*iface;

	/* stop all interfaces and remove all areas */
	while ((area = LIST_FIRST(&oeconf->area_list)) != NULL) {
		LIST_FOREACH(iface, &area->iface_list, entry) {
			if (if_fsm(iface, IF_EVT_DOWN)) {
				log_debug("error stopping interface %s",
				    iface->name);
			}
		}
		LIST_REMOVE(area, entry);
		area_del(area);
	}

	close(oeconf->ospf_socket);

	/* close pipes */
	imsgbuf_write(&iev_rde->ibuf);
	imsgbuf_clear(&iev_rde->ibuf);
	close(iev_rde->ibuf.fd);
	imsgbuf_write(&iev_main->ibuf);
	imsgbuf_clear(&iev_main->ibuf);
	close(iev_main->ibuf.fd);

	/* clean up */
	free(iev_rde);
	free(iev_main);
	free(oeconf);

	log_info("ospf engine exiting");
	_exit(0);
}

/* imesg */
int
ospfe_imsg_compose_parent(int type, pid_t pid, void *data, u_int16_t datalen)
{
	return (imsg_compose_event(iev_main, type, 0, pid, -1, data, datalen));
}

int
ospfe_imsg_compose_rde(int type, u_int32_t peerid, pid_t pid,
    void *data, u_int16_t datalen)
{
	return (imsg_compose_event(iev_rde, type, peerid, pid, -1,
	    data, datalen));
}

void
ospfe_dispatch_main(int fd, short event, void *bula)
{
	static struct area	*narea;
	struct area		*area;
	struct iface		*iface, *ifp, *i;
	struct ifaddrchange	*ifc;
	struct iface_addr	*ia, *nia;
	struct imsg		 imsg;
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf = &iev->ibuf;
	int			 n, stub_changed, shut = 0, isvalid, wasvalid;

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
			fatal("ospfe_dispatch_main: imsg_get error");
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_IFINFO:
			if (imsg.hdr.len != IMSG_HEADER_SIZE +
			    sizeof(struct iface))
				fatalx("IFINFO imsg with wrong len");
			ifp = imsg.data;

			LIST_FOREACH(area, &oeconf->area_list, entry) {
				LIST_FOREACH(i, &area->iface_list, entry) {
					if (strcmp(i->dependon,
					    ifp->name) == 0) {
						log_warnx("interface %s"
						    " changed state, %s"
						    " depends on it",
						    ifp->name, i->name);
						i->depend_ok =
						    ifstate_is_up(ifp);
						if (ifstate_is_up(i))
							orig_rtr_lsa(i->area);
					}
				}
			}

			if (!(ifp->cflags & F_IFACE_CONFIGURED))
				break;
			iface = if_find(ifp->ifindex);
			if (iface == NULL)
				fatalx("interface lost in ospfe");

			wasvalid = (iface->flags & IFF_UP) &&
			    LINK_STATE_IS_UP(iface->linkstate);

			if_update(iface, ifp->mtu, ifp->flags, ifp->if_type,
			    ifp->linkstate, ifp->baudrate, ifp->rdomain);

			isvalid = (iface->flags & IFF_UP) &&
			    LINK_STATE_IS_UP(iface->linkstate);

			if (wasvalid == isvalid)
				break;

			if (isvalid) {
				if_fsm(iface, IF_EVT_UP);
				log_warnx("interface %s up", iface->name);
			} else {
				if_fsm(iface, IF_EVT_DOWN);
				log_warnx("interface %s down", iface->name);
			}
			break;
		case IMSG_IFADDRNEW:
			if (imsg.hdr.len != IMSG_HEADER_SIZE +
			    sizeof(struct ifaddrchange))
				fatalx("IFADDRNEW imsg with wrong len");
			ifc = imsg.data;

			iface = if_find(ifc->ifindex);
			if (iface == NULL)
				fatalx("IFADDRNEW interface lost in ospfe");

			if ((ia = calloc(1, sizeof(struct iface_addr))) ==
			    NULL)
				fatal("ospfe_dispatch_main IFADDRNEW");
			ia->addr = ifc->addr;
			ia->dstbrd = ifc->dstbrd;
			ia->prefixlen = ifc->prefixlen;

			TAILQ_INSERT_TAIL(&iface->ifa_list, ia, entry);
			orig_link_lsa(iface);
			break;
		case IMSG_IFADDRDEL:
			if (imsg.hdr.len != IMSG_HEADER_SIZE +
			    sizeof(struct ifaddrchange))
				fatalx("IFADDRDEL imsg with wrong len");
			ifc = imsg.data;

			iface = if_find(ifc->ifindex);
			if (iface == NULL)
				fatalx("IFADDRDEL interface lost in ospfe");

			for (ia = TAILQ_FIRST(&iface->ifa_list); ia != NULL;
			    ia = nia) {
				nia = TAILQ_NEXT(ia, entry);

				if (IN6_ARE_ADDR_EQUAL(&ia->addr,
				    &ifc->addr)) {
					TAILQ_REMOVE(&iface->ifa_list, ia,
					    entry);
					free(ia);
					break;
				}
			}
			orig_link_lsa(iface);
			break;
		case IMSG_RECONF_CONF:
			if ((noeconf = malloc(sizeof(struct ospfd_conf))) ==
			    NULL)
				fatal(NULL);
			memcpy(noeconf, imsg.data, sizeof(struct ospfd_conf));

			LIST_INIT(&noeconf->area_list);
			LIST_INIT(&noeconf->cand_list);
			break;
		case IMSG_RECONF_AREA:
			if ((narea = area_new()) == NULL)
				fatal(NULL);
			memcpy(narea, imsg.data, sizeof(struct area));

			LIST_INIT(&narea->iface_list);
			LIST_INIT(&narea->nbr_list);
			RB_INIT(&narea->lsa_tree);

			LIST_INSERT_HEAD(&noeconf->area_list, narea, entry);
			break;
		case IMSG_RECONF_END:
			if ((oeconf->flags & OSPFD_FLAG_STUB_ROUTER) !=
			    (noeconf->flags & OSPFD_FLAG_STUB_ROUTER))
				stub_changed = 1;
			else
				stub_changed = 0;
			merge_config(oeconf, noeconf);
			noeconf = NULL;
			if (stub_changed)
				orig_rtr_lsa_all(NULL);
			break;
		case IMSG_CTL_KROUTE:
		case IMSG_CTL_KROUTE_ADDR:
		case IMSG_CTL_END:
			control_imsg_relay(&imsg);
			break;
		case IMSG_CONTROLFD:
			if ((fd = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg control"
				    "fd but didn't receive any", __func__);
			/* Listen on control socket. */
			control_listen(fd);
			if (pledge("stdio inet mcast", NULL) == -1)
				fatal("pledge");
			break;
		default:
			log_debug("ospfe_dispatch_main: error handling imsg %d",
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
ospfe_dispatch_rde(int fd, short event, void *bula)
{
	struct lsa_hdr		 lsa_hdr;
	struct lsa_link		 lsa_link;
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf = &iev->ibuf;
	struct nbr		*nbr;
	struct lsa_hdr		*lhp;
	struct lsa_ref		*ref;
	struct area		*area;
	struct iface		*iface;
	struct lsa_entry	*le;
	struct imsg		 imsg;
	struct abr_rtr		 ar;
	int			 n, noack = 0, shut = 0;
	u_int16_t		 l, age;

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
			fatal("ospfe_dispatch_rde: imsg_get error");
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_DD:
			nbr = nbr_find_peerid(imsg.hdr.peerid);
			if (nbr == NULL)
				break;

			/* put these on my ls_req_list for retrieval */
			lhp = lsa_hdr_new();
			memcpy(lhp, imsg.data, sizeof(*lhp));
			ls_req_list_add(nbr, lhp);
			break;
		case IMSG_DD_END:
			nbr = nbr_find_peerid(imsg.hdr.peerid);
			if (nbr == NULL)
				break;

			nbr->dd_pending--;
			if (nbr->dd_pending == 0 && nbr->state & NBR_STA_LOAD) {
				if (ls_req_list_empty(nbr))
					nbr_fsm(nbr, NBR_EVT_LOAD_DONE);
				else
					start_ls_req_tx_timer(nbr);
			}
			break;
		case IMSG_DB_SNAPSHOT:
			nbr = nbr_find_peerid(imsg.hdr.peerid);
			if (nbr == NULL)
				break;
			if (nbr->state != NBR_STA_SNAP)	/* discard */
				break;

			/* add LSA header to the neighbor db_sum_list */
			lhp = lsa_hdr_new();
			memcpy(lhp, imsg.data, sizeof(*lhp));
			db_sum_list_add(nbr, lhp);
			break;
		case IMSG_DB_END:
			nbr = nbr_find_peerid(imsg.hdr.peerid);
			if (nbr == NULL)
				break;

			nbr->dd_snapshot = 0;
			if (nbr->state != NBR_STA_SNAP)
				break;

			/* snapshot done, start tx of dd packets */
			nbr_fsm(nbr, NBR_EVT_SNAP_DONE);
			break;
		case IMSG_LS_FLOOD:
			nbr = nbr_find_peerid(imsg.hdr.peerid);
			if (nbr == NULL)
				break;

			l = imsg.hdr.len - IMSG_HEADER_SIZE;
			if (l < sizeof(lsa_hdr))
				fatalx("ospfe_dispatch_rde: "
				    "bad imsg size");
			memcpy(&lsa_hdr, imsg.data, sizeof(lsa_hdr));

			ref = lsa_cache_add(imsg.data, l);

			if (lsa_hdr.type == htons(LSA_TYPE_EXTERNAL)) {
				/*
				 * flood on all areas but stub areas and
				 * virtual links
				 */
				LIST_FOREACH(area, &oeconf->area_list, entry) {
					if (area->stub)
						continue;
					LIST_FOREACH(iface, &area->iface_list,
					    entry) {
						noack += lsa_flood(iface, nbr,
						    &lsa_hdr, imsg.data);
					}
				}
			} else if (lsa_hdr.type == htons(LSA_TYPE_LINK)) {
				/*
				 * Save link-LSA options of neighbor.
				 * This is needed to originate network-LSA.
				 */
				if (l - sizeof(lsa_hdr) < sizeof(lsa_link))
					fatalx("ospfe_dispatch_rde: "
					    "bad imsg link size");
				memcpy(&lsa_link, (char *)imsg.data +
				    sizeof(lsa_hdr), sizeof(lsa_link));
				nbr->link_options = lsa_link.opts &
				    htonl(LSA_24_MASK);

				/*
				 * flood on interface only
				 */
				noack += lsa_flood(nbr->iface, nbr,
				    &lsa_hdr, imsg.data);
			} else {
				/*
				 * flood on all area interfaces on
				 * area 0.0.0.0 include also virtual links.
				 */
				LIST_FOREACH(iface,
				    &nbr->iface->area->iface_list, entry) {
					noack += lsa_flood(iface, nbr,
					    &lsa_hdr, imsg.data);
				}
				/* XXX virtual links */
			}

			/* remove from ls_req_list */
			le = ls_req_list_get(nbr, &lsa_hdr);
			if (!(nbr->state & NBR_STA_FULL) && le != NULL) {
				ls_req_list_free(nbr, le);
				/*
				 * XXX no need to ack requested lsa
				 * the problem is that the RFC is very
				 * unclear about this.
				 */
				noack = 1;
			}

			if (!noack && nbr->iface != NULL &&
			    nbr->iface->self != nbr) {
				if (!(nbr->iface->state & IF_STA_BACKUP) ||
				    nbr->iface->dr == nbr) {
					/* delayed ack */
					lhp = lsa_hdr_new();
					memcpy(lhp, &lsa_hdr, sizeof(*lhp));
					ls_ack_list_add(nbr->iface, lhp);
				}
			}

			lsa_cache_put(ref, nbr);
			break;
		case IMSG_LS_UPD:
		case IMSG_LS_SNAP:
			/*
			 * IMSG_LS_UPD is used in two cases:
			 * 1. as response to ls requests
			 * 2. as response to ls updates where the DB
			 *    is newer then the sent LSA
			 * IMSG_LS_SNAP is used in one case:
			 *    in EXSTART when the LSA has age MaxAge
			 */
			l = imsg.hdr.len - IMSG_HEADER_SIZE;
			if (l < sizeof(lsa_hdr))
				fatalx("ospfe_dispatch_rde: "
				    "bad imsg size");

			nbr = nbr_find_peerid(imsg.hdr.peerid);
			if (nbr == NULL)
				break;

			if (nbr->iface->self == nbr)
				break;

			if (imsg.hdr.type == IMSG_LS_SNAP &&
			    nbr->state != NBR_STA_SNAP)
				break;

			memcpy(&age, imsg.data, sizeof(age));
			ref = lsa_cache_add(imsg.data, l);
			if (ntohs(age) >= MAX_AGE)
				/* add to retransmit list */
				ls_retrans_list_add(nbr, imsg.data, 0, 0);
			else
				ls_retrans_list_add(nbr, imsg.data, 0, 1);

			lsa_cache_put(ref, nbr);
			break;
		case IMSG_LS_ACK:
			/*
			 * IMSG_LS_ACK is used in two cases:
			 * 1. LSA was a duplicate
			 * 2. LS age is MaxAge and there is no current
			 *    instance in the DB plus no neighbor in state
			 *    Exchange or Loading
			 */
			nbr = nbr_find_peerid(imsg.hdr.peerid);
			if (nbr == NULL)
				break;

			if (nbr->iface->self == nbr)
				break;

			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(lsa_hdr))
				fatalx("ospfe_dispatch_rde: bad imsg size");
			memcpy(&lsa_hdr, imsg.data, sizeof(lsa_hdr));

			/* for case one check for implied acks */
			if (nbr->iface->state & IF_STA_DROTHER)
				if (ls_retrans_list_del(nbr->iface->self,
				    &lsa_hdr) == 0)
					break;
			if (ls_retrans_list_del(nbr, &lsa_hdr) == 0)
				break;

			/* send a direct acknowledgement */
			send_direct_ack(nbr->iface, nbr->addr, imsg.data,
			    imsg.hdr.len - IMSG_HEADER_SIZE);

			break;
		case IMSG_LS_BADREQ:
			nbr = nbr_find_peerid(imsg.hdr.peerid);
			if (nbr == NULL)
				break;

			if (nbr->iface->self == nbr)
				fatalx("ospfe_dispatch_rde: "
				    "dummy neighbor got BADREQ");

			nbr_fsm(nbr, NBR_EVT_BAD_LS_REQ);
			break;
		case IMSG_ABR_UP:
			memcpy(&ar, imsg.data, sizeof(ar));

			if ((iface = find_vlink(&ar)) != NULL &&
			    iface->state == IF_STA_DOWN)
				if (if_fsm(iface, IF_EVT_UP)) {
					log_debug("error starting interface %s",
					    iface->name);
				}
			break;
		case IMSG_ABR_DOWN:
			memcpy(&ar, imsg.data, sizeof(ar));

			if ((iface = find_vlink(&ar)) != NULL &&
			    iface->state == IF_STA_POINTTOPOINT)
				if (if_fsm(iface, IF_EVT_DOWN)) {
					log_debug("error stopping interface %s",
					    iface->name);
				}
			break;
		case IMSG_CTL_AREA:
		case IMSG_CTL_IFACE:
		case IMSG_CTL_END:
		case IMSG_CTL_SHOW_DATABASE:
		case IMSG_CTL_SHOW_DB_EXT:
		case IMSG_CTL_SHOW_DB_LINK:
		case IMSG_CTL_SHOW_DB_NET:
		case IMSG_CTL_SHOW_DB_RTR:
		case IMSG_CTL_SHOW_DB_INTRA:
		case IMSG_CTL_SHOW_DB_SELF:
		case IMSG_CTL_SHOW_DB_SUM:
		case IMSG_CTL_SHOW_DB_ASBR:
		case IMSG_CTL_SHOW_RIB:
		case IMSG_CTL_SHOW_SUM:
		case IMSG_CTL_SHOW_SUM_AREA:
			control_imsg_relay(&imsg);
			break;
		default:
			log_debug("ospfe_dispatch_rde: error handling imsg %d",
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

struct iface *
find_vlink(struct abr_rtr *ar)
{
	struct area	*area;
	struct iface	*iface = NULL;

	LIST_FOREACH(area, &oeconf->area_list, entry)
		LIST_FOREACH(iface, &area->iface_list, entry)
			if (iface->abr_id.s_addr == ar->abr_id.s_addr &&
			    iface->type == IF_TYPE_VIRTUALLINK &&
			    iface->area->id.s_addr == ar->area.s_addr) {
				iface->dst = ar->dst_ip;
				iface->addr = ar->addr;
				iface->metric = ar->metric;

				return (iface);
			}

	return (iface);
}

void
orig_rtr_lsa_all(struct area *area)
{
	struct area	*a;

	/*
	 * update all router LSA in all areas except area itself,
	 * as this update is already running.
	 */
	LIST_FOREACH(a, &oeconf->area_list, entry)
		if (a != area)
			orig_rtr_lsa(a);
}

void
orig_rtr_lsa(struct area *area)
{
	struct lsa_hdr		 lsa_hdr;
	struct lsa_rtr		 lsa_rtr;
	struct lsa_rtr_link	 rtr_link;
	struct iface		*iface;
	struct ibuf		*buf;
	struct nbr		*nbr, *self = NULL;
	u_int32_t		 flags;
	u_int16_t		 chksum;
	u_int8_t		 border, virtual = 0;

	log_debug("orig_rtr_lsa: area %s", inet_ntoa(area->id));

	/* XXX IBUF_READ_SIZE */
	if ((buf = ibuf_dynamic(sizeof(lsa_hdr), IBUF_READ_SIZE)) == NULL)
		fatal("orig_rtr_lsa");

	/* reserve space for LSA header and LSA Router header */
	if (ibuf_add_zero(buf, sizeof(lsa_hdr)) == -1)
		fatal("orig_rtr_lsa: ibuf_add_zero failed");

	if (ibuf_add_zero(buf, sizeof(lsa_rtr)) == -1)
		fatal("orig_rtr_lsa: ibuf_add_zero failed");

	/* links */
	LIST_FOREACH(iface, &area->iface_list, entry) {
		if (self == NULL && iface->self != NULL)
			self = iface->self;

		bzero(&rtr_link, sizeof(rtr_link));

		switch (iface->type) {
		case IF_TYPE_POINTOPOINT:
			LIST_FOREACH(nbr, &iface->nbr_list, entry)
				if (nbr != iface->self &&
				    nbr->state & NBR_STA_FULL)
					break;
			if (nbr && iface->state & IF_STA_POINTTOPOINT) {
				log_debug("orig_rtr_lsa: point-to-point, "
				    "interface %s", iface->name);
				rtr_link.type = LINK_TYPE_POINTTOPOINT;
				if (iface->dependon[0] != '\0' &&
				    iface->depend_ok == 0)
					rtr_link.metric = MAX_METRIC;
				else
					rtr_link.metric = htons(iface->metric);
				rtr_link.iface_id = htonl(iface->ifindex);
				rtr_link.nbr_iface_id = htonl(nbr->iface_id);
				rtr_link.nbr_rtr_id = nbr->id.s_addr;
				if (ibuf_add(buf, &rtr_link, sizeof(rtr_link)))
					fatalx("orig_rtr_lsa: ibuf_add failed");
			}
			continue;
		case IF_TYPE_BROADCAST:
		case IF_TYPE_NBMA:
			if ((iface->state & IF_STA_MULTI)) {
				if (iface->dr == iface->self) {
					LIST_FOREACH(nbr, &iface->nbr_list,
					    entry)
						if (nbr != iface->self &&
						    nbr->state & NBR_STA_FULL)
							break;
				} else
					nbr = iface->dr;

				if (nbr && nbr->state & NBR_STA_FULL) {
					log_debug("orig_rtr_lsa: transit net, "
					    "interface %s", iface->name);

					rtr_link.type = LINK_TYPE_TRANSIT_NET;
					if (iface->dependon[0] != '\0' &&
					    iface->depend_ok == 0)
						rtr_link.metric = MAX_METRIC;
					else
						rtr_link.metric =
						    htons(iface->metric);
					rtr_link.iface_id = htonl(iface->ifindex);
					rtr_link.nbr_iface_id = htonl(iface->dr->iface_id);
					rtr_link.nbr_rtr_id = iface->dr->id.s_addr;
					if (ibuf_add(buf, &rtr_link,
					    sizeof(rtr_link)))
						fatalx("orig_rtr_lsa: "
						    "ibuf_add failed");
					break;
				}
			}
			break;
#if 0 /* TODO virtualllink/pointtomulti */
		case IF_TYPE_VIRTUALLINK:
			LIST_FOREACH(nbr, &iface->nbr_list, entry) {
				if (nbr != iface->self &&
				    nbr->state & NBR_STA_FULL)
					break;
			}
			if (nbr) {
				rtr_link.id = nbr->id.s_addr;
//XXX				rtr_link.data = iface->addr.s_addr;
				rtr_link.type = LINK_TYPE_VIRTUAL;
				/* RFC 3137: stub router support */
				if (oeconf->flags & OSPFD_FLAG_STUB_ROUTER ||
				    oe_nofib)
					rtr_link.metric = 0xffff;
				else
					rtr_link.metric = htons(iface->metric);
				virtual = 1;
				if (ibuf_add(buf, &rtr_link, sizeof(rtr_link)))
					fatalx("orig_rtr_lsa: ibuf_add failed");

				log_debug("orig_rtr_lsa: virtual link, "
				    "interface %s", iface->name);
			}
			continue;
		case IF_TYPE_POINTOMULTIPOINT:
			log_debug("orig_rtr_lsa: stub net, "
			    "interface %s", iface->name);
//XXX			rtr_link.id = iface->addr.s_addr;
			rtr_link.data = 0xffffffff;
			rtr_link.type = LINK_TYPE_STUB_NET;
			rtr_link.metric = htons(iface->metric);
			if (ibuf_add(buf, &rtr_link, sizeof(rtr_link)))
				fatalx("orig_rtr_lsa: ibuf_add failed");

			LIST_FOREACH(nbr, &iface->nbr_list, entry) {
				if (nbr != iface->self &&
				    nbr->state & NBR_STA_FULL) {
					bzero(&rtr_link, sizeof(rtr_link));
					log_debug("orig_rtr_lsa: "
					    "point-to-multipoint, interface %s",
					    iface->name);
//XXX					rtr_link.id = nbr->addr.s_addr;
//XXX					rtr_link.data = iface->addr.s_addr;
					rtr_link.type = LINK_TYPE_POINTTOPOINT;
					/* RFC 3137: stub router support */
					if (oe_nofib || oeconf->flags &
					    OSPFD_FLAG_STUB_ROUTER)
						rtr_link.metric = MAX_METRIC;
					else if (iface->dependon[0] != '\0' &&
						 iface->dependon_ok == 0)
						rtr_link.metric = MAX_METRIC;
					else
						rtr_link.metric =
						    htons(iface->metric);
					if (ibuf_add(buf, &rtr_link,
					    sizeof(rtr_link)))
						fatalx("orig_rtr_lsa: "
						    "ibuf_add failed");
				}
			}
			continue;
#endif /* TODO virtualllink/pointtomulti */
		default:
			fatalx("orig_rtr_lsa: unknown interface type");
		}
	}

	/* LSA router header */
	lsa_rtr.opts = 0;
	flags = 0;

	/*
	 * Set the E bit as soon as an as-ext lsa may be redistributed, only
	 * setting it in case we redistribute something is not worth the fuss.
	 */
	if (oeconf->redistribute && !area->stub)
		flags |= OSPF_RTR_E;

	border = (area_border_router(oeconf) != 0);
	if (border != oeconf->border) {
		oeconf->border = border;
		orig_rtr_lsa_all(area);
	}

	if (oeconf->border)
		flags |= OSPF_RTR_B;
	/* TODO set V flag if a active virtual link ends here and the
	 * area is the transit area for this link. */
	if (virtual)
		flags |= OSPF_RTR_V;

	LSA_24_SETLO(lsa_rtr.opts, area_ospf_options(area));
	LSA_24_SETHI(lsa_rtr.opts, flags);
	lsa_rtr.opts = htonl(lsa_rtr.opts);
	if (ibuf_set(buf, sizeof(lsa_hdr), &lsa_rtr, sizeof(lsa_rtr)) == -1)
		fatal("orig_rtr_lsa: ibuf_set failed");

	/* LSA header */
	lsa_hdr.age = htons(DEFAULT_AGE);
	lsa_hdr.type = htons(LSA_TYPE_ROUTER);
	/* XXX needs to be fixed if multiple router-lsa need to be announced */
	lsa_hdr.ls_id = 0;
	lsa_hdr.adv_rtr = oeconf->rtr_id.s_addr;
	lsa_hdr.seq_num = htonl(INIT_SEQ_NUM);
	lsa_hdr.len = htons(ibuf_size(buf));
	lsa_hdr.ls_chksum = 0;		/* updated later */
	if (ibuf_set(buf, 0, &lsa_hdr, sizeof(lsa_hdr)) == -1)
		fatal("orig_rtr_lsa: ibuf_set failed");

	chksum = iso_cksum(ibuf_data(buf), ibuf_size(buf), LS_CKSUM_OFFSET);
	if (ibuf_set_n16(buf, LS_CKSUM_OFFSET, chksum) == -1)
		fatal("orig_rtr_lsa: ibuf_set_n16 failed");

	if (self)
		imsg_compose_event(iev_rde, IMSG_LS_UPD, self->peerid, 0,
		    -1, ibuf_data(buf), ibuf_size(buf));
	else
		log_warnx("orig_rtr_lsa: empty area %s",
		    inet_ntoa(area->id));

	ibuf_free(buf);
}

void
orig_net_lsa(struct iface *iface)
{
	struct lsa_hdr		 lsa_hdr;
	struct nbr		*nbr;
	struct ibuf		*buf;
	struct lsa_net		 lsa_net;
	int			 num_rtr = 0;
	u_int16_t		 chksum;

	/* XXX IBUF_READ_SIZE */
	if ((buf = ibuf_dynamic(sizeof(lsa_hdr), IBUF_READ_SIZE)) == NULL)
		fatal("orig_net_lsa");

	/* reserve space for LSA header and options field */
	if (ibuf_add_zero(buf, sizeof(lsa_hdr) + sizeof(lsa_net)) == -1)
		fatal("orig_net_lsa: ibuf_add_zero failed");

	lsa_net.opts = 0;
	/* fully adjacent neighbors + self */
	LIST_FOREACH(nbr, &iface->nbr_list, entry)
		if (nbr->state & NBR_STA_FULL) {
			if (ibuf_add(buf, &nbr->id, sizeof(nbr->id)))
				fatal("orig_net_lsa: ibuf_add failed");
			lsa_net.opts |= nbr->link_options;
			num_rtr++;
		}

	if (num_rtr == 1) {
		/* non transit net therefore no need to generate a net lsa */
		ibuf_free(buf);
		return;
	}

	/* LSA header */
	if (iface->state & IF_STA_DR)
		lsa_hdr.age = htons(DEFAULT_AGE);
	else
		lsa_hdr.age = htons(MAX_AGE);

	lsa_hdr.type = htons(LSA_TYPE_NETWORK);
	/* for network LSAs, the link state ID equals the interface ID */
	lsa_hdr.ls_id = htonl(iface->ifindex);
	lsa_hdr.adv_rtr = oeconf->rtr_id.s_addr;
	lsa_hdr.seq_num = htonl(INIT_SEQ_NUM);
	lsa_hdr.len = htons(ibuf_size(buf));
	lsa_hdr.ls_chksum = 0;		/* updated later */
	if (ibuf_set(buf, 0, &lsa_hdr, sizeof(lsa_hdr)) == -1)
		fatal("orig_net_lsa: ibuf_set failed");

	lsa_net.opts &= lsa_net.opts & htonl(LSA_24_MASK);
	if (ibuf_set(buf, sizeof(lsa_hdr), &lsa_net, sizeof(lsa_net)) == -1)
		fatal("orig_net_lsa: ibuf_set failed");

	chksum = iso_cksum(ibuf_data(buf), ibuf_size(buf), LS_CKSUM_OFFSET);
	if (ibuf_set_n16(buf, LS_CKSUM_OFFSET, chksum) == -1)
		fatal("orig_net_lsa: ibuf_set_n16 failed");

	imsg_compose_event(iev_rde, IMSG_LS_UPD, iface->self->peerid, 0,
	    -1, ibuf_data(buf), ibuf_size(buf));

	ibuf_free(buf);
}

void
orig_link_lsa(struct iface *iface)
{
	struct lsa_hdr		 lsa_hdr;
	struct lsa_link		 lsa_link;
	struct lsa_prefix	 lsa_prefix;
	struct ibuf		*buf;
	struct iface_addr	*ia;
	struct in6_addr		 prefix;
	unsigned int		 num_prefix = 0;
	u_int16_t		 chksum;
	u_int32_t		 options;

	log_debug("orig_link_lsa: interface %s", iface->name);

	switch (iface->type) {
	case IF_TYPE_VIRTUALLINK:	/* forbidden by rfc5340 */
		return;
	case IF_TYPE_BROADCAST:
	case IF_TYPE_NBMA:
		if ((iface->state & IF_STA_MULTI) == 0)
			return;
		break;
	case IF_TYPE_POINTOPOINT:
	case IF_TYPE_POINTOMULTIPOINT:
		if ((iface->state & IF_STA_POINTTOPOINT) == 0)
			return;
		break;
	default:
		fatalx("orig_link_lsa: unknown interface type");
	}

	/* XXX IBUF_READ_SIZE */
	if ((buf = ibuf_dynamic(sizeof(lsa_hdr) + sizeof(lsa_link),
	    IBUF_READ_SIZE)) == NULL)
		fatal("orig_link_lsa");

	/* reserve space for LSA header and LSA link header */
	if (ibuf_add_zero(buf, sizeof(lsa_hdr) + sizeof(lsa_link)) == -1)
		fatal("orig_link_lsa: ibuf_add_zero failed");

	/* link-local address, and all prefixes configured on interface */
	TAILQ_FOREACH(ia, &iface->ifa_list, entry) {
		if (IN6_IS_ADDR_LINKLOCAL(&ia->addr)) {
			log_debug("orig_link_lsa: link local address %s",
			    log_in6addr(&ia->addr));
			lsa_link.lladdr = ia->addr;
			continue;
		}

		lsa_prefix.prefixlen = ia->prefixlen;
		lsa_prefix.options = 0;
		lsa_prefix.metric = 0;
		inet6applymask(&prefix, &ia->addr, ia->prefixlen);
		log_debug("orig_link_lsa: prefix %s", log_in6addr(&prefix));
		if (ibuf_add(buf, &lsa_prefix, sizeof(lsa_prefix)))
			fatal("orig_link_lsa: ibuf_add failed");
		if (ibuf_add(buf, &prefix.s6_addr[0],
		    LSA_PREFIXSIZE(ia->prefixlen)))
			fatal("orig_link_lsa: ibuf_add failed");
		num_prefix++;
	}

	/* LSA link header (lladdr has already been filled in above) */
	LSA_24_SETHI(lsa_link.opts, iface->priority);
	options = area_ospf_options(iface->area);
	LSA_24_SETLO(lsa_link.opts, options);
	lsa_link.opts = htonl(lsa_link.opts);
	lsa_link.numprefix = htonl(num_prefix);
	if (ibuf_set(buf, sizeof(lsa_hdr), &lsa_link, sizeof(lsa_link)) == -1)
		fatal("orig_link_lsa: ibuf_set failed");

	/* LSA header */
	lsa_hdr.age = htons(DEFAULT_AGE);
	lsa_hdr.type = htons(LSA_TYPE_LINK);
	/* for link LSAs, the link state ID equals the interface ID */
	lsa_hdr.ls_id = htonl(iface->ifindex);
	lsa_hdr.adv_rtr = oeconf->rtr_id.s_addr;
	lsa_hdr.seq_num = htonl(INIT_SEQ_NUM);
	lsa_hdr.len = htons(ibuf_size(buf));
	lsa_hdr.ls_chksum = 0;		/* updated later */
	if (ibuf_set(buf, 0, &lsa_hdr, sizeof(lsa_hdr)) == -1)
		fatal("orig_link_lsa: ibuf_set failed");

	chksum = iso_cksum(ibuf_data(buf), ibuf_size(buf), LS_CKSUM_OFFSET);
	if (ibuf_set_n16(buf, LS_CKSUM_OFFSET, chksum) == -1)
		fatal("orig_link_lsa: ibuf_set_n16 failed");

	imsg_compose_event(iev_rde, IMSG_LS_UPD, iface->self->peerid, 0,
	    -1, ibuf_data(buf), ibuf_size(buf));

	ibuf_free(buf);
}

u_int32_t
ospfe_router_id(void)
{
	return (oeconf->rtr_id.s_addr);
}

void
ospfe_fib_update(int type)
{
	int	old = oe_nofib;

	if (type == IMSG_CTL_FIB_COUPLE)
		oe_nofib = 0;
	if (type == IMSG_CTL_FIB_DECOUPLE)
		oe_nofib = 1;
	if (old != oe_nofib)
		orig_rtr_lsa_all(NULL);
}

void
ospfe_iface_ctl(struct ctl_conn *c, unsigned int idx)
{
	struct area		*area;
	struct iface		*iface;
	struct ctl_iface	*ictl;

	LIST_FOREACH(area, &oeconf->area_list, entry)
		LIST_FOREACH(iface, &area->iface_list, entry)
			if (idx == 0 || idx == iface->ifindex) {
				ictl = if_to_ctl(iface);
				imsg_compose_event(&c->iev,
				    IMSG_CTL_SHOW_INTERFACE, 0, 0, -1,
				    ictl, sizeof(struct ctl_iface));
			}
}

void
ospfe_nbr_ctl(struct ctl_conn *c)
{
	struct area	*area;
	struct iface	*iface;
	struct nbr	*nbr;
	struct ctl_nbr	*nctl;

	LIST_FOREACH(area, &oeconf->area_list, entry)
		LIST_FOREACH(iface, &area->iface_list, entry)
			LIST_FOREACH(nbr, &iface->nbr_list, entry) {
				if (iface->self != nbr) {
					nctl = nbr_to_ctl(nbr);
					imsg_compose_event(&c->iev,
					    IMSG_CTL_SHOW_NBR, 0, 0, -1, nctl,
					    sizeof(struct ctl_nbr));
				}
			}

	imsg_compose_event(&c->iev, IMSG_CTL_END, 0, 0, -1, NULL, 0);
}

void
ospfe_demote_area(struct area *area, int active)
{
	struct demote_msg	dmsg;

	if (ospfd_process != PROC_OSPF_ENGINE ||
	    area->demote_group[0] == '\0')
		return;

	bzero(&dmsg, sizeof(dmsg));
	strlcpy(dmsg.demote_group, area->demote_group,
	    sizeof(dmsg.demote_group));
	dmsg.level = area->demote_level;
	if (active)
		dmsg.level = -dmsg.level;

	ospfe_imsg_compose_parent(IMSG_DEMOTE, 0, &dmsg, sizeof(dmsg));
}

void
ospfe_demote_iface(struct iface *iface, int active)
{
	struct demote_msg	dmsg;

	if (ospfd_process != PROC_OSPF_ENGINE ||
	    iface->demote_group[0] == '\0')
		return;

	bzero(&dmsg, sizeof(dmsg));
	strlcpy(dmsg.demote_group, iface->demote_group,
	sizeof(dmsg.demote_group));
	if (active)
		dmsg.level = -1;
	else
		dmsg.level = 1;

	log_warnx("ospfe_demote_iface: group %s level %d", dmsg.demote_group,
	    dmsg.level);

	ospfe_imsg_compose_parent(IMSG_DEMOTE, 0, &dmsg, sizeof(dmsg));
}
