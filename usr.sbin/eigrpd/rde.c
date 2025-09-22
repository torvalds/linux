/*	$OpenBSD: rde.c,v 1.32 2024/11/21 13:38:14 claudio Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
 * Copyright (c) 2004, 2005 Claudio Jeker <claudio@openbsd.org>
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
#include <net/route.h>

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

static void		 rde_sig_handler(int sig, short, void *);
static __dead void	 rde_shutdown(void);
static void		 rde_dispatch_imsg(int, short, void *);
static void		 rde_dispatch_parent(int, short, void *);
static struct redistribute *eigrp_redistribute(struct eigrp *, struct kroute *);
static void		 rt_redist_set(struct kroute *, int);
static void		 rt_snap(struct rde_nbr *);
static struct ctl_rt	*rt_to_ctl(struct rt_node *, struct eigrp_route *);
static void		 rt_dump(struct ctl_show_topology_req *, pid_t);

struct eigrpd_conf	*rdeconf;

static struct imsgev	*iev_eigrpe;
static struct imsgev	*iev_main;

static void
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
void
rde(int debug, int verbose)
{
	struct event		 ev_sigint, ev_sigterm;
	struct timeval		 now;
	struct passwd		*pw;

	rdeconf = config_new_empty();

	log_init(debug);
	log_verbose(verbose);

	if ((pw = getpwnam(EIGRPD_USER)) == NULL)
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

	if (pledge("stdio recvfd", NULL) == -1)
		fatal("pledge");

	event_init();

	/* setup signal handler */
	signal_set(&ev_sigint, SIGINT, rde_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, rde_sig_handler, NULL);
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
	iev_main->handler = rde_dispatch_parent;
	iev_main->events = EV_READ;
	event_set(&iev_main->ev, iev_main->ibuf.fd, iev_main->events,
	    iev_main->handler, iev_main);
	event_add(&iev_main->ev, NULL);

	gettimeofday(&now, NULL);
	global.uptime = now.tv_sec;

	event_dispatch();

	rde_shutdown();
}

static __dead void
rde_shutdown(void)
{
	/* close pipes */
	imsgbuf_clear(&iev_eigrpe->ibuf);
	close(iev_eigrpe->ibuf.fd);
	imsgbuf_clear(&iev_main->ibuf);
	close(iev_main->ibuf.fd);

	config_clear(rdeconf, PROC_RDE_ENGINE);

	free(iev_eigrpe);
	free(iev_main);

	log_info("route decision engine exiting");
	exit(0);
}

int
rde_imsg_compose_parent(int type, pid_t pid, void *data, uint16_t datalen)
{
	return (imsg_compose_event(iev_main, type, 0, pid, -1,
	    data, datalen));
}

int
rde_imsg_compose_eigrpe(int type, uint32_t peerid, pid_t pid, void *data,
    uint16_t datalen)
{
	return (imsg_compose_event(iev_eigrpe, type, peerid, pid, -1,
	    data, datalen));
}

static void
rde_dispatch_imsg(int fd, short event, void *bula)
{
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf;
	struct imsg		 imsg;
	struct rde_nbr		*nbr;
	struct rde_nbr		 new;
	struct rinfo		 rinfo;
	ssize_t			 n;
	int			 shut = 0, verbose;

	ibuf = &iev->ibuf;

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
		case IMSG_NEIGHBOR_UP:
			if (imsg.hdr.len - IMSG_HEADER_SIZE !=
			    sizeof(struct rde_nbr))
				fatalx("invalid size of neighbor request");
			memcpy(&new, imsg.data, sizeof(new));

			if (rde_nbr_find(imsg.hdr.peerid))
				fatalx("rde_dispatch_imsg: "
				    "neighbor already exists");
			rde_nbr_new(imsg.hdr.peerid, &new);
			break;
		case IMSG_NEIGHBOR_DOWN:
			nbr = rde_nbr_find(imsg.hdr.peerid);
			if (nbr == NULL) {
				log_debug("%s: cannot find rde neighbor",
				    __func__);
				break;
			}

			rde_check_link_down_nbr(nbr);
			rde_flush_queries();
			rde_nbr_del(rde_nbr_find(imsg.hdr.peerid), 0);
			break;
		case IMSG_RECV_UPDATE_INIT:
			nbr = rde_nbr_find(imsg.hdr.peerid);
			if (nbr == NULL) {
				log_debug("%s: cannot find rde neighbor",
				    __func__);
				break;
			}

			rt_snap(nbr);
			break;
		case IMSG_RECV_UPDATE:
		case IMSG_RECV_QUERY:
		case IMSG_RECV_REPLY:
		case IMSG_RECV_SIAQUERY:
		case IMSG_RECV_SIAREPLY:
			nbr = rde_nbr_find(imsg.hdr.peerid);
			if (nbr == NULL) {
				log_debug("%s: cannot find rde neighbor",
				    __func__);
				break;
			}

			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(rinfo))
				fatalx("invalid size of rinfo");
			memcpy(&rinfo, imsg.data, sizeof(rinfo));

			switch (imsg.hdr.type) {
			case IMSG_RECV_UPDATE:
				rde_check_update(nbr, &rinfo);
				break;
			case IMSG_RECV_QUERY:
				rde_check_query(nbr, &rinfo, 0);
				break;
			case IMSG_RECV_REPLY:
				rde_check_reply(nbr, &rinfo, 0);
				break;
			case IMSG_RECV_SIAQUERY:
				rde_check_query(nbr, &rinfo, 1);
				break;
			case IMSG_RECV_SIAREPLY:
				rde_check_reply(nbr, &rinfo, 1);
				break;
			}
			break;
		case IMSG_CTL_SHOW_TOPOLOGY:
			if (imsg.hdr.len != IMSG_HEADER_SIZE +
			    sizeof(struct ctl_show_topology_req)) {
				log_warnx("%s: wrong imsg len", __func__);
				break;
			}

			rt_dump(imsg.data, imsg.hdr.pid);
			rde_imsg_compose_eigrpe(IMSG_CTL_END, 0, imsg.hdr.pid,
			    NULL, 0);
			break;
		case IMSG_CTL_LOG_VERBOSE:
			/* already checked by eigrpe */
			memcpy(&verbose, imsg.data, sizeof(verbose));
			log_verbose(verbose);
			break;
		default:
			log_debug("rde_dispatch_imsg: unexpected imsg %d",
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
rde_dispatch_parent(int fd, short event, void *bula)
{
	static struct eigrpd_conf *nconf;
	static struct iface	*niface;
	static struct eigrp	*neigrp;
	struct eigrp_iface	*nei;
	struct imsg		 imsg;
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf;
	struct kif		*kif;
	ssize_t			 n;
	int			 shut = 0;

	ibuf = &iev->ibuf;

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
			fatal("rde_dispatch_parent: imsg_get error");
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_IFDOWN:
			if (imsg.hdr.len != IMSG_HEADER_SIZE +
			    sizeof(struct kif))
				fatalx("IFDOWN imsg with wrong len");
			kif = imsg.data;
			rde_check_link_down(kif->ifindex);
			break;
		case IMSG_NETWORK_ADD:
			if (imsg.hdr.len != IMSG_HEADER_SIZE +
			    sizeof(struct kroute))
				fatalx("IMSG_NETWORK_ADD imsg with wrong len");
			rt_redist_set(imsg.data, 0);
			break;
		case IMSG_NETWORK_DEL:
			if (imsg.hdr.len != IMSG_HEADER_SIZE +
			    sizeof(struct kroute))
				fatalx("IMSG_NETWORK_DEL imsg with wrong len");
			rt_redist_set(imsg.data, 1);
			break;
		case IMSG_SOCKET_IPC:
			if (iev_eigrpe) {
				log_warnx("%s: received unexpected imsg fd "
				    "to eigrpe", __func__);
				break;
			}
			if ((fd = imsg_get_fd(&imsg)) == -1) {
				log_warnx("%s: expected to receive imsg fd to "
				    "eigrpe but didn't receive any", __func__);
				break;
			}

			iev_eigrpe = malloc(sizeof(struct imsgev));
			if (iev_eigrpe == NULL)
				fatal(NULL);
			if (imsgbuf_init(&iev_eigrpe->ibuf, fd) == -1)
				fatal(NULL);
			iev_eigrpe->handler = rde_dispatch_imsg;
			iev_eigrpe->events = EV_READ;
			event_set(&iev_eigrpe->ev, iev_eigrpe->ibuf.fd,
			    iev_eigrpe->events, iev_eigrpe->handler,
			    iev_eigrpe);
			event_add(&iev_eigrpe->ev, NULL);
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
				fatalx("rde_dispatch_parent: "
				    "RB_INSERT(ifaces_by_id) failed");
			break;
		case IMSG_RECONF_END:
			merge_config(rdeconf, nconf, PROC_RDE_ENGINE);
			nconf = NULL;
			break;
		default:
			log_debug("%s: unexpected imsg %d", __func__,
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
rde_instance_init(struct eigrp *eigrp)
{
	struct rde_nbr		 nbr;

	memset(&nbr, 0, sizeof(nbr));
	nbr.flags = F_RDE_NBR_SELF | F_RDE_NBR_REDIST;
	eigrp->rnbr_redist = rde_nbr_new(NBR_IDSELF, &nbr);
	eigrp->rnbr_redist->eigrp = eigrp;
	nbr.flags = F_RDE_NBR_SELF | F_RDE_NBR_SUMMARY;
	eigrp->rnbr_summary = rde_nbr_new(NBR_IDSELF, &nbr);
	eigrp->rnbr_summary->eigrp = eigrp;
}

void
rde_instance_del(struct eigrp *eigrp)
{
	struct rde_nbr		*nbr, *safe;
	struct rt_node		*rn;

	/* clear topology */
	while((rn = RB_MIN(rt_tree, &eigrp->topology)) != NULL)
		rt_del(rn);

	/* clear nbrs */
	RB_FOREACH_SAFE(nbr, rde_nbr_head, &rde_nbrs, safe)
		if (nbr->eigrp == eigrp)
			rde_nbr_del(nbr, 0);
	rde_nbr_del(eigrp->rnbr_redist, 0);
	rde_nbr_del(eigrp->rnbr_summary, 0);

	free(eigrp);
}

void
rde_send_change_kroute(struct rt_node *rn, struct eigrp_route *route)
{
	struct eigrp	*eigrp = route->nbr->eigrp;
	struct kroute	 kr;
	struct in6_addr	 lo6 = IN6ADDR_LOOPBACK_INIT;

	log_debug("%s: %s nbr %s", __func__, log_prefix(rn),
	    log_addr(eigrp->af, &route->nbr->addr));

	memset(&kr, 0, sizeof(kr));
	kr.af = eigrp->af;
	kr.prefix = rn->prefix;
	kr.prefixlen = rn->prefixlen;
	if (route->nbr->ei) {
		kr.nexthop = route->nexthop;
		kr.ifindex = route->nbr->ei->iface->ifindex;
	} else {
		switch (eigrp->af) {
		case AF_INET:
			kr.nexthop.v4.s_addr = htonl(INADDR_LOOPBACK);
			break;
		case AF_INET6:
			kr.nexthop.v6 = lo6;
			break;
		default:
			fatalx("rde_send_delete_kroute: unknown af");
			break;
		}
		kr.flags = F_BLACKHOLE;
	}
	if (route->type == EIGRP_ROUTE_EXTERNAL)
		kr.priority = rdeconf->fib_priority_external;
	else {
		if (route->nbr->flags & F_RDE_NBR_SUMMARY)
			kr.priority = rdeconf->fib_priority_summary;
		else
			kr.priority = rdeconf->fib_priority_internal;
	}

	rde_imsg_compose_parent(IMSG_KROUTE_CHANGE, 0, &kr, sizeof(kr));

	route->flags |= F_EIGRP_ROUTE_INSTALLED;
}

void
rde_send_delete_kroute(struct rt_node *rn, struct eigrp_route *route)
{
	struct eigrp	*eigrp = route->nbr->eigrp;
	struct kroute	 kr;
	struct in6_addr	 lo6 = IN6ADDR_LOOPBACK_INIT;

	log_debug("%s: %s nbr %s", __func__, log_prefix(rn),
	    log_addr(eigrp->af, &route->nbr->addr));

	memset(&kr, 0, sizeof(kr));
	kr.af = eigrp->af;
	kr.prefix = rn->prefix;
	kr.prefixlen = rn->prefixlen;
	if (route->nbr->ei) {
		kr.nexthop = route->nexthop;
		kr.ifindex = route->nbr->ei->iface->ifindex;
	} else {
		switch (eigrp->af) {
		case AF_INET:
			kr.nexthop.v4.s_addr = htonl(INADDR_LOOPBACK);
			break;
		case AF_INET6:
			kr.nexthop.v6 = lo6;
			break;
		default:
			fatalx("rde_send_delete_kroute: unknown af");
			break;
		}
		kr.flags = F_BLACKHOLE;
	}
	if (route->type == EIGRP_ROUTE_EXTERNAL)
		kr.priority = rdeconf->fib_priority_external;
	else {
		if (route->nbr->flags & F_RDE_NBR_SUMMARY)
			kr.priority = rdeconf->fib_priority_summary;
		else
			kr.priority = rdeconf->fib_priority_internal;
	}

	rde_imsg_compose_parent(IMSG_KROUTE_DELETE, 0, &kr, sizeof(kr));

	route->flags &= ~F_EIGRP_ROUTE_INSTALLED;
}

static struct redistribute *
eigrp_redistribute(struct eigrp *eigrp, struct kroute *kr)
{
	struct redistribute	*r;
	uint8_t			 is_default = 0;
	union eigrpd_addr	 addr;

	/* only allow the default route via REDIST_DEFAULT */
	if (!eigrp_addrisset(kr->af, &kr->prefix) && kr->prefixlen == 0)
		is_default = 1;

	SIMPLEQ_FOREACH(r, &eigrp->redist_list, entry) {
		switch (r->type & ~REDIST_NO) {
		case REDIST_STATIC:
			if (is_default)
				continue;
			if (kr->flags & F_STATIC)
				return (r->type & REDIST_NO ? NULL : r);
			break;
		case REDIST_RIP:
			if (is_default)
				continue;
			if (kr->priority == RTP_RIP)
				return (r->type & REDIST_NO ? NULL : r);
			break;
		case REDIST_OSPF:
			if (is_default)
				continue;
			if (kr->priority == RTP_OSPF)
				return (r->type & REDIST_NO ? NULL : r);
			break;
		case REDIST_CONNECTED:
			if (is_default)
				continue;
			if (kr->flags & F_CONNECTED)
				return (r->type & REDIST_NO ? NULL : r);
			break;
		case REDIST_ADDR:
			if (eigrp_addrisset(r->af, &r->addr) &&
			    r->prefixlen == 0) {
				if (is_default)
					return (r->type & REDIST_NO ? NULL : r);
				else
					return (0);
			}

			eigrp_applymask(kr->af, &addr, &kr->prefix,
			    r->prefixlen);
			if (eigrp_addrcmp(kr->af, &addr, &r->addr) == 0 &&
			    kr->prefixlen >= r->prefixlen)
				return (r->type & REDIST_NO ? NULL : r);
			break;
		case REDIST_DEFAULT:
			if (is_default)
				return (r->type & REDIST_NO ? NULL : r);
			break;
		}
	}

	return (NULL);
}

static void
rt_redist_set(struct kroute *kr, int withdraw)
{
	struct eigrp		*eigrp;
	struct redistribute	*r;
	struct redist_metric	*rmetric;
	struct rinfo		 ri;

	TAILQ_FOREACH(eigrp, &rdeconf->instances, entry) {
		if (eigrp->af != kr->af)
			continue;

		r = eigrp_redistribute(eigrp, kr);
		if (r == NULL)
			continue;

		if (r->metric)
			rmetric = r->metric;
		else if (eigrp->dflt_metric)
			rmetric = eigrp->dflt_metric;
		else
			continue;

		memset(&ri, 0, sizeof(ri));
		ri.af = kr->af;
		ri.type = EIGRP_ROUTE_EXTERNAL;
		ri.prefix = kr->prefix;
		ri.prefixlen = kr->prefixlen;

		/* metric */
		if (withdraw)
			ri.metric.delay = EIGRP_INFINITE_METRIC;
		else
			ri.metric.delay = eigrp_composite_delay(rmetric->delay);
		ri.metric.bandwidth =
		    eigrp_composite_bandwidth(rmetric->bandwidth);
		metric_encode_mtu(ri.metric.mtu, rmetric->mtu);
		ri.metric.hop_count = 0;
		ri.metric.reliability = rmetric->reliability;
		ri.metric.load = rmetric->load;
		ri.metric.tag = 0;
		ri.metric.flags = 0;

		/* external metric */
		ri.emetric.routerid = htonl(rdeconf->rtr_id.s_addr);
		ri.emetric.as = r->emetric.as;
		ri.emetric.tag = r->emetric.tag;
		ri.emetric.metric = r->emetric.metric;
		if (kr->priority == rdeconf->fib_priority_internal)
			ri.emetric.protocol = EIGRP_EXT_PROTO_EIGRP;
		else if (kr->priority == RTP_STATIC)
			ri.emetric.protocol = EIGRP_EXT_PROTO_STATIC;
		else if (kr->priority == RTP_RIP)
			ri.emetric.protocol = EIGRP_EXT_PROTO_RIP;
		else if (kr->priority == RTP_OSPF)
			ri.emetric.protocol = EIGRP_EXT_PROTO_OSPF;
		else
			ri.emetric.protocol = EIGRP_EXT_PROTO_CONN;
		ri.emetric.flags = 0;

		rde_check_update(eigrp->rnbr_redist, &ri);
	}
}

void
rt_summary_set(struct eigrp *eigrp, struct summary_addr *summary,
    struct classic_metric *metric)
{
	struct rinfo		 ri;

	memset(&ri, 0, sizeof(ri));
	ri.af = eigrp->af;
	ri.type = EIGRP_ROUTE_INTERNAL;
	ri.prefix = summary->prefix;
	ri.prefixlen = summary->prefixlen;
	ri.metric = *metric;

	rde_check_update(eigrp->rnbr_summary, &ri);
}

/* send all known routing information to new neighbor */
static void
rt_snap(struct rde_nbr *nbr)
{
	struct eigrp		*eigrp = nbr->eigrp;
	struct rt_node		*rn;
	struct rinfo		 ri;

	RB_FOREACH(rn, rt_tree, &eigrp->topology)
		if (rn->state == DUAL_STA_PASSIVE &&
		    !rde_summary_check(nbr->ei, &rn->prefix, rn->prefixlen)) {
			rinfo_fill_successor(rn, &ri);
			rde_imsg_compose_eigrpe(IMSG_SEND_UPDATE,
			    nbr->peerid, 0, &ri, sizeof(ri));
		}

	rde_imsg_compose_eigrpe(IMSG_SEND_UPDATE_END, nbr->peerid, 0,
	    NULL, 0);
}

static struct ctl_rt *
rt_to_ctl(struct rt_node *rn, struct eigrp_route *route)
{
	static struct ctl_rt	 rtctl;

	memset(&rtctl, 0, sizeof(rtctl));
	rtctl.af = route->nbr->eigrp->af;
	rtctl.as = route->nbr->eigrp->as;
	rtctl.prefix = rn->prefix;
	rtctl.prefixlen = rn->prefixlen;
	rtctl.type = route->type;
	rtctl.nexthop = route->nexthop;
	if (route->nbr->flags & F_RDE_NBR_REDIST)
		strlcpy(rtctl.ifname, "redistribute", sizeof(rtctl.ifname));
	else if (route->nbr->flags & F_RDE_NBR_SUMMARY)
		strlcpy(rtctl.ifname, "summary", sizeof(rtctl.ifname));
	else
		memcpy(rtctl.ifname, route->nbr->ei->iface->name,
		    sizeof(rtctl.ifname));
	rtctl.distance = route->distance;
	rtctl.rdistance = route->rdistance;
	rtctl.fdistance = rn->successor.fdistance;
	rtctl.state = rn->state;
	/* metric */
	rtctl.metric.delay = eigrp_real_delay(route->metric.delay);
	/* translate to microseconds */
	rtctl.metric.delay *= 10;
	rtctl.metric.bandwidth = eigrp_real_bandwidth(route->metric.bandwidth);
	rtctl.metric.mtu = metric_decode_mtu(route->metric.mtu);
	rtctl.metric.hop_count = route->metric.hop_count;
	rtctl.metric.reliability = route->metric.reliability;
	rtctl.metric.load = route->metric.load;
	/* external metric */
	rtctl.emetric = route->emetric;

	if (route->nbr == rn->successor.nbr)
		rtctl.flags |= F_CTL_RT_SUCCESSOR;
	else if (route->rdistance < rn->successor.fdistance)
		rtctl.flags |= F_CTL_RT_FSUCCESSOR;

	return (&rtctl);
}

static void
rt_dump(struct ctl_show_topology_req *treq, pid_t pid)
{
	struct eigrp		*eigrp;
	struct rt_node		*rn;
	struct eigrp_route	*route;
	struct ctl_rt		*rtctl;
	int			 first = 1;

	TAILQ_FOREACH(eigrp, &rdeconf->instances, entry) {
		RB_FOREACH(rn, rt_tree, &eigrp->topology) {
			if (eigrp_addrisset(treq->af, &treq->prefix) &&
			    eigrp_addrcmp(treq->af, &treq->prefix,
			    &rn->prefix))
				continue;

			if (treq->prefixlen &&
			    (treq->prefixlen != rn->prefixlen))
				continue;

			first = 1;
			TAILQ_FOREACH(route, &rn->routes, entry) {
				if (treq->flags & F_CTL_ACTIVE &&
				    !(rn->state & DUAL_STA_ACTIVE_ALL))
					continue;
				if (!(treq->flags & F_CTL_ALLLINKS) &&
				    route->rdistance >= rn->successor.fdistance)
					continue;

				rtctl = rt_to_ctl(rn, route);
				if (first) {
					rtctl->flags |= F_CTL_RT_FIRST;
					first = 0;
				}
				rde_imsg_compose_eigrpe(IMSG_CTL_SHOW_TOPOLOGY,
				    0, pid, rtctl, sizeof(*rtctl));
			}
		}
	}
}
