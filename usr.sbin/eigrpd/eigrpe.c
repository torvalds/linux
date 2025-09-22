/*	$OpenBSD: eigrpe.c,v 1.47 2024/11/21 13:38:14 claudio Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
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
#include <netinet/in.h>
#include <netinet/ip.h>

#include <arpa/inet.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "eigrpd.h"
#include "eigrpe.h"
#include "rde.h"
#include "log.h"
#include "control.h"

static void		 eigrpe_sig_handler(int, short, void *);
static __dead void	 eigrpe_shutdown(void);
static void		 eigrpe_dispatch_main(int, short, void *);
static void		 eigrpe_dispatch_rde(int, short, void *);

struct eigrpd_conf	*econf;

static struct event	 ev4;
static struct event	 ev6;
static struct imsgev	*iev_main;
static struct imsgev	*iev_rde;

static void
eigrpe_sig_handler(int sig, short event, void *bula)
{
	switch (sig) {
	case SIGINT:
	case SIGTERM:
		eigrpe_shutdown();
		/* NOTREACHED */
	default:
		fatalx("unexpected signal");
	}
}

/* eigrp engine */
void
eigrpe(int debug, int verbose, char *sockname)
{
	struct passwd		*pw;
	struct event		 ev_sigint, ev_sigterm;

	econf = config_new_empty();

	log_init(debug);
	log_verbose(verbose);

	/* create eigrpd control socket outside chroot */
	if (control_init(sockname) == -1)
		fatalx("control socket setup failed");

	if (inet_pton(AF_INET, AllEIGRPRouters_v4, &global.mcast_addr_v4) != 1)
		fatal("inet_pton");
	if (inet_pton(AF_INET6, AllEIGRPRouters_v6, &global.mcast_addr_v6) != 1)
		fatal("inet_pton");

	/* create the raw ipv4 socket */
	if ((global.eigrp_socket_v4 = socket(AF_INET,
	    SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, IPPROTO_EIGRP)) == -1)
		fatal("error creating raw ipv4 socket");

	/* set some defaults */
	if (if_set_ipv4_mcast_ttl(global.eigrp_socket_v4, EIGRP_IP_TTL) == -1)
		fatal("if_set_ipv4_mcast_ttl");
	if (if_set_ipv4_mcast_loop(global.eigrp_socket_v4) == -1)
		fatal("if_set_ipv4_mcast_loop");
	if (if_set_ipv4_recvif(global.eigrp_socket_v4, 1) == -1)
		fatal("if_set_ipv4_recvif");
	if (if_set_ipv4_hdrincl(global.eigrp_socket_v4) == -1)
		fatal("if_set_ipv4_hdrincl");
	if_set_sockbuf(global.eigrp_socket_v4);

	/* create the raw ipv6 socket */
	if ((global.eigrp_socket_v6 = socket(AF_INET6,
	    SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, IPPROTO_EIGRP)) == -1)
		fatal("error creating raw ipv6 socket");

	/* set some defaults */
	if (if_set_ipv6_mcast_loop(global.eigrp_socket_v6) == -1)
		fatal("if_set_ipv6_mcast_loop");
	if (if_set_ipv6_pktinfo(global.eigrp_socket_v6, 1) == -1)
		fatal("if_set_ipv6_pktinfo");
	if (if_set_ipv6_dscp(global.eigrp_socket_v6,
	    IPTOS_PREC_NETCONTROL) == -1)
		fatal("if_set_ipv6_dscp");
	if_set_sockbuf(global.eigrp_socket_v6);

	if ((pw = getpwnam(EIGRPD_USER)) == NULL)
		fatal("getpwnam");

	if (chroot(pw->pw_dir) == -1)
		fatal("chroot");
	if (chdir("/") == -1)
		fatal("chdir(\"/\")");

	setproctitle("eigrp engine");
	log_procname = "eigrpe";

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	if (pledge("stdio inet mcast recvfd", NULL) == -1)
		fatal("pledge");

	event_init();

	/* setup signal handler */
	signal_set(&ev_sigint, SIGINT, eigrpe_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, eigrpe_sig_handler, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	/* setup pipe and event handler to the parent process */
	if ((iev_main = malloc(sizeof(struct imsgev))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(&iev_main->ibuf, 3) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(&iev_main->ibuf);
	iev_main->handler = eigrpe_dispatch_main;
	iev_main->events = EV_READ;
	event_set(&iev_main->ev, iev_main->ibuf.fd, iev_main->events,
	    iev_main->handler, iev_main);
	event_add(&iev_main->ev, NULL);

	event_set(&ev4, global.eigrp_socket_v4, EV_READ|EV_PERSIST,
	    recv_packet, econf);
	event_add(&ev4, NULL);

	event_set(&ev6, global.eigrp_socket_v6, EV_READ|EV_PERSIST,
	    recv_packet, econf);
	event_add(&ev6, NULL);

	/* listen on eigrpd control socket */
	control_listen();

	event_dispatch();

	eigrpe_shutdown();
}

static __dead void
eigrpe_shutdown(void)
{
	/* close pipes */
	imsgbuf_write(&iev_rde->ibuf);
	imsgbuf_clear(&iev_rde->ibuf);
	close(iev_rde->ibuf.fd);
	imsgbuf_write(&iev_main->ibuf);
	imsgbuf_clear(&iev_main->ibuf);
	close(iev_main->ibuf.fd);

	config_clear(econf, PROC_EIGRP_ENGINE);

	event_del(&ev4);
	event_del(&ev6);
	close(global.eigrp_socket_v4);
	close(global.eigrp_socket_v6);

	/* clean up */
	free(iev_rde);
	free(iev_main);

	log_info("eigrp engine exiting");
	exit(0);
}

/* imesg */
int
eigrpe_imsg_compose_parent(int type, pid_t pid, void *data, uint16_t datalen)
{
	return (imsg_compose_event(iev_main, type, 0, pid, -1, data, datalen));
}

int
eigrpe_imsg_compose_rde(int type, uint32_t peerid, pid_t pid,
    void *data, uint16_t datalen)
{
	return (imsg_compose_event(iev_rde, type, peerid, pid, -1,
	    data, datalen));
}

static void
eigrpe_dispatch_main(int fd, short event, void *bula)
{
	static struct eigrpd_conf *nconf;
	static struct iface	*niface;
	static struct eigrp	*neigrp;
	struct eigrp_iface	*nei;
	struct imsg		 imsg;
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf = &iev->ibuf;
	struct iface		*iface = NULL;
	struct kif		*kif;
	struct kaddr		*ka;
	int			 n, shut = 0;

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
			fatal("eigrpe_dispatch_main: imsg_get error");
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_IFINFO:
			if (imsg.hdr.len != IMSG_HEADER_SIZE +
			    sizeof(struct kif))
				fatalx("IFSTATUS imsg with wrong len");
			kif = imsg.data;

			iface = if_lookup(econf, kif->ifindex);
			if (!iface)
				break;

			iface->flags = kif->flags;
			iface->linkstate = kif->link_state;
			if_update(iface, AF_UNSPEC);
			break;
		case IMSG_NEWADDR:
			if (imsg.hdr.len != IMSG_HEADER_SIZE +
			    sizeof(struct kaddr))
				fatalx("NEWADDR imsg with wrong len");
			ka = imsg.data;

			iface = if_lookup(econf, ka->ifindex);
			if (iface == NULL)
				break;

			if_addr_new(iface, ka);
			break;
		case IMSG_DELADDR:
			if (imsg.hdr.len != IMSG_HEADER_SIZE +
			    sizeof(struct kaddr))
				fatalx("DELADDR imsg with wrong len");
			ka = imsg.data;

			iface = if_lookup(econf, ka->ifindex);
			if (iface == NULL)
				break;

			if_addr_del(iface, ka);
			break;
		case IMSG_SOCKET_IPC:
			if (iev_rde) {
				log_warnx("%s: received unexpected imsg fd "
				    "to rde", __func__);
				break;
			}
			if ((fd = imsg_get_fd(&imsg)) == -1) {
				log_warnx("%s: expected to receive imsg fd to "
				    "rde but didn't receive any", __func__);
				break;
			}

			iev_rde = malloc(sizeof(struct imsgev));
			if (iev_rde == NULL)
				fatal(NULL);
			if (imsgbuf_init(&iev_rde->ibuf, fd) == -1)
				fatal(NULL);
			iev_rde->handler = eigrpe_dispatch_rde;
			iev_rde->events = EV_READ;
			event_set(&iev_rde->ev, iev_rde->ibuf.fd,
			    iev_rde->events, iev_rde->handler, iev_rde);
			event_add(&iev_rde->ev, NULL);
			break;
		case IMSG_RECONF_CONF:
			if ((nconf = malloc(sizeof(struct eigrpd_conf))) ==
			    NULL)
				fatal(NULL);
			memcpy(nconf, imsg.data, sizeof(struct eigrpd_conf));

			TAILQ_INIT(&nconf->iface_list);
			TAILQ_INIT(&nconf->instances);
			break;
		case IMSG_RECONF_INSTANCE:
			if ((neigrp = malloc(sizeof(struct eigrp))) == NULL)
				fatal(NULL);
			memcpy(neigrp, imsg.data, sizeof(struct eigrp));

			SIMPLEQ_INIT(&neigrp->redist_list);
			TAILQ_INIT(&neigrp->ei_list);
			RB_INIT(&neigrp->nbrs);
			RB_INIT(&neigrp->topology);
			TAILQ_INSERT_TAIL(&nconf->instances, neigrp, entry);
			break;
		case IMSG_RECONF_IFACE:
			niface = imsg.data;
			niface = if_lookup(nconf, niface->ifindex);
			if (niface)
				break;

			if ((niface = malloc(sizeof(struct iface))) == NULL)
				fatal(NULL);
			memcpy(niface, imsg.data, sizeof(struct iface));

			TAILQ_INIT(&niface->ei_list);
			TAILQ_INIT(&niface->addr_list);
			TAILQ_INSERT_TAIL(&nconf->iface_list, niface, entry);
			break;
		case IMSG_RECONF_EIGRP_IFACE:
			if (niface == NULL)
				break;
			if ((nei = malloc(sizeof(struct eigrp_iface))) == NULL)
				fatal(NULL);
			memcpy(nei, imsg.data, sizeof(struct eigrp_iface));

			nei->iface = niface;
			nei->eigrp = neigrp;
			TAILQ_INIT(&nei->nbr_list);
			TAILQ_INIT(&nei->update_list);
			TAILQ_INIT(&nei->query_list);
			TAILQ_INIT(&nei->summary_list);
			TAILQ_INSERT_TAIL(&niface->ei_list, nei, i_entry);
			TAILQ_INSERT_TAIL(&neigrp->ei_list, nei, e_entry);
			if (RB_INSERT(iface_id_head, &ifaces_by_id, nei) !=
			    NULL)
				fatalx("eigrpe_dispatch_main: "
				    "RB_INSERT(ifaces_by_id) failed");
			break;
		case IMSG_RECONF_END:
			merge_config(econf, nconf, PROC_EIGRP_ENGINE);
			nconf = NULL;
			break;
		case IMSG_CTL_KROUTE:
		case IMSG_CTL_IFINFO:
		case IMSG_CTL_END:
			control_imsg_relay(&imsg);
			break;
		default:
			log_debug("%s: error handling imsg %d", __func__,
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

static void
eigrpe_dispatch_rde(int fd, short event, void *bula)
{
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf = &iev->ibuf;
	struct imsg		 imsg;
	struct nbr		*nbr;
	struct eigrp_iface	*ei;
	struct rinfo		 rinfo;
	int			 n, shut = 0;

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
			fatal("eigrpe_dispatch_rde: imsg_get error");
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_SEND_UPDATE:
		case IMSG_SEND_QUERY:
		case IMSG_SEND_REPLY:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(rinfo))
				fatalx("invalid size of rinfo");
			memcpy(&rinfo, imsg.data, sizeof(rinfo));

			nbr = nbr_find_peerid(imsg.hdr.peerid);
			if (nbr == NULL) {
				log_debug("%s: cannot find rde neighbor",
				    __func__);
				break;
			}

			switch (imsg.hdr.type) {
			case IMSG_SEND_UPDATE:
				message_add(&nbr->update_list, &rinfo);
				break;
			case IMSG_SEND_QUERY:
				message_add(&nbr->query_list, &rinfo);
				break;
			case IMSG_SEND_REPLY:
				message_add(&nbr->reply_list, &rinfo);
				break;
			}
			break;
		case IMSG_SEND_MUPDATE:
		case IMSG_SEND_MQUERY:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(rinfo))
				fatalx("invalid size of rinfo");
			memcpy(&rinfo, imsg.data, sizeof(rinfo));

			ei = eigrp_if_lookup_id(imsg.hdr.peerid);
			if (ei == NULL) {
				log_debug("%s: cannot find interface",
				    __func__);
				break;
			}

			switch (imsg.hdr.type) {
			case IMSG_SEND_MUPDATE:
				message_add(&ei->update_list, &rinfo);
				break;
			case IMSG_SEND_MQUERY:
				message_add(&ei->query_list, &rinfo);
				break;
			}
			break;
		case IMSG_SEND_UPDATE_END:
		case IMSG_SEND_REPLY_END:
		case IMSG_SEND_SIAQUERY_END:
		case IMSG_SEND_SIAREPLY_END:
			nbr = nbr_find_peerid(imsg.hdr.peerid);
			if (nbr == NULL) {
				log_debug("%s: cannot find rde neighbor",
				    __func__);
				break;
			}

			switch (imsg.hdr.type) {
			case IMSG_SEND_UPDATE_END:
				send_update(nbr->ei, nbr, 0, &nbr->update_list);
				message_list_clr(&nbr->update_list);
				break;
			case IMSG_SEND_REPLY_END:
				send_reply(nbr,  &nbr->reply_list, 0);
				message_list_clr(&nbr->reply_list);
				break;
			case IMSG_SEND_SIAQUERY_END:
				send_query(nbr->ei, nbr, &nbr->query_list, 1);
				message_list_clr(&nbr->query_list);
				break;
			case IMSG_SEND_SIAREPLY_END:
				send_reply(nbr, &nbr->reply_list, 1);
				message_list_clr(&nbr->reply_list);
				break;
			}
			break;
		case IMSG_SEND_MUPDATE_END:
		case IMSG_SEND_MQUERY_END:
			ei = eigrp_if_lookup_id(imsg.hdr.peerid);
			if (ei == NULL) {
				log_debug("%s: cannot find interface",
				    __func__);
				break;
			}

			switch (imsg.hdr.type) {
			case IMSG_SEND_MUPDATE_END:
				send_update(ei, NULL, 0, &ei->update_list);
				message_list_clr(&ei->update_list);
				break;
			case IMSG_SEND_MQUERY_END:
				send_query(ei, NULL, &ei->query_list, 0);
				message_list_clr(&ei->query_list);
				break;
			}
			break;
		case IMSG_NEIGHBOR_DOWN:
			nbr = nbr_find_peerid(imsg.hdr.peerid);
			if (nbr == NULL) {
				log_debug("%s: cannot find rde neighbor",
				    __func__);
				break;
			}
			/* announce that this neighborship is dead */
			send_peerterm(nbr);
			nbr_del(nbr);
			break;
		case IMSG_CTL_SHOW_TOPOLOGY:
		case IMSG_CTL_END:
			control_imsg_relay(&imsg);
			break;
		default:
			log_debug("%s: error handling imsg %d", __func__,
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
eigrpe_instance_init(struct eigrp *eigrp)
{
}

void
eigrpe_instance_del(struct eigrp *eigrp)
{
	struct eigrp_iface	*ei;

	while ((ei = TAILQ_FIRST(&eigrp->ei_list)) != NULL)
		eigrp_if_del(ei);

	free(eigrp);
}

void
message_add(struct rinfo_head *rinfo_list, struct rinfo *rinfo)
{
	struct rinfo_entry	*re;

	re = calloc(1, sizeof(*re));
	if (re == NULL)
		fatal("message_add");
	re->rinfo = *rinfo;

	TAILQ_INSERT_TAIL(rinfo_list, re, entry);
}

void
message_list_clr(struct rinfo_head *rinfo_list)
{
	struct rinfo_entry	*re;

	while ((re = TAILQ_FIRST(rinfo_list)) != NULL) {
		TAILQ_REMOVE(rinfo_list, re, entry);
		free(re);
	}
}

void
seq_addr_list_clr(struct seq_addr_head *seq_addr_list)
{
	struct seq_addr_entry	*sa;

	while ((sa = TAILQ_FIRST(seq_addr_list)) != NULL) {
		TAILQ_REMOVE(seq_addr_list, sa, entry);
		free(sa);
	}
}

void
eigrpe_orig_local_route(struct eigrp_iface *ei, struct if_addr *if_addr,
    int withdraw)
{
	struct rinfo	 rinfo;

	memset(&rinfo, 0, sizeof(rinfo));
	rinfo.af = if_addr->af;
	rinfo.type = EIGRP_ROUTE_INTERNAL;
	rinfo.prefix = if_addr->addr;
	rinfo.prefixlen = if_addr->prefixlen;

	eigrp_applymask(rinfo.af, &rinfo.prefix, &rinfo.prefix,
	    rinfo.prefixlen);

	if (withdraw)
		rinfo.metric.delay = EIGRP_INFINITE_METRIC;
	else
		rinfo.metric.delay = eigrp_composite_delay(ei->delay);
	rinfo.metric.bandwidth = eigrp_composite_bandwidth(ei->bandwidth);
	metric_encode_mtu(rinfo.metric.mtu, ei->iface->mtu);
	rinfo.metric.hop_count = 0;
	rinfo.metric.reliability = DEFAULT_RELIABILITY;
	rinfo.metric.load = DEFAULT_LOAD;
	rinfo.metric.tag = 0;
	rinfo.metric.flags = 0;

	eigrpe_imsg_compose_rde(IMSG_RECV_UPDATE, ei->self->peerid, 0,
	    &rinfo, sizeof(rinfo));
}

void
eigrpe_iface_ctl(struct ctl_conn *c, unsigned int idx)
{
	struct eigrp		*eigrp;
	struct eigrp_iface	*ei;
	struct ctl_iface	*ictl;

	TAILQ_FOREACH(eigrp, &econf->instances, entry) {
		TAILQ_FOREACH(ei, &eigrp->ei_list, e_entry) {
			if (idx == 0 || idx == ei->iface->ifindex) {
				ictl = if_to_ctl(ei);
				imsg_compose_event(&c->iev,
				    IMSG_CTL_SHOW_INTERFACE, 0, 0, -1,
				    ictl, sizeof(struct ctl_iface));
			}
		}
	}
}

void
eigrpe_nbr_ctl(struct ctl_conn *c)
{
	struct eigrp	*eigrp;
	struct nbr	*nbr;
	struct ctl_nbr	*nctl;

	TAILQ_FOREACH(eigrp, &econf->instances, entry) {
		RB_FOREACH(nbr, nbr_addr_head, &eigrp->nbrs) {
			if (nbr->flags & (F_EIGRP_NBR_PENDING|F_EIGRP_NBR_SELF))
				continue;

			nctl = nbr_to_ctl(nbr);
			imsg_compose_event(&c->iev, IMSG_CTL_SHOW_NBR, 0,
			    0, -1, nctl, sizeof(struct ctl_nbr));
		}
	}

	imsg_compose_event(&c->iev, IMSG_CTL_END, 0, 0, -1, NULL, 0);
}

void
eigrpe_stats_ctl(struct ctl_conn *c)
{
	struct eigrp		*eigrp;
	struct ctl_stats	 sctl;

	TAILQ_FOREACH(eigrp, &econf->instances, entry) {
		sctl.af = eigrp->af;
		sctl.as = eigrp->as;
		sctl.stats = eigrp->stats;
		imsg_compose_event(&c->iev, IMSG_CTL_SHOW_STATS, 0,
		    0, -1, &sctl, sizeof(struct ctl_stats));
	}

	imsg_compose_event(&c->iev, IMSG_CTL_END, 0, 0, -1, NULL, 0);
}
