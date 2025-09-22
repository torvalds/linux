/*	$OpenBSD: interface.c,v 1.88 2024/08/21 15:18:00 florian Exp $ */

/*
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
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
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/if_types.h>
#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <event.h>

#include "ospfd.h"
#include "ospf.h"
#include "log.h"
#include "ospfe.h"

void		 if_hello_timer(int, short, void *);
void		 if_start_hello_timer(struct iface *);
void		 if_stop_hello_timer(struct iface *);
void		 if_stop_wait_timer(struct iface *);
void		 if_wait_timer(int, short, void *);
void		 if_start_wait_timer(struct iface *);
void		 if_stop_wait_timer(struct iface *);
struct nbr	*if_elect(struct nbr *, struct nbr *);

struct {
	int			state;
	enum iface_event	event;
	enum iface_action	action;
	int			new_state;
} iface_fsm[] = {
    /* current state	event that happened	action to take	resulting state */
    {IF_STA_DOWN,	IF_EVT_UP,		IF_ACT_STRT,	0},
    {IF_STA_WAITING,	IF_EVT_BACKUP_SEEN,	IF_ACT_ELECT,	0},
    {IF_STA_WAITING,	IF_EVT_WTIMER,		IF_ACT_ELECT,	0},
    {IF_STA_ANY,	IF_EVT_WTIMER,		IF_ACT_NOTHING,	0},
    {IF_STA_WAITING,	IF_EVT_NBR_CHNG,	IF_ACT_NOTHING,	0},
    {IF_STA_MULTI,	IF_EVT_NBR_CHNG,	IF_ACT_ELECT,	0},
    {IF_STA_ANY,	IF_EVT_NBR_CHNG,	IF_ACT_NOTHING,	0},
    {IF_STA_ANY,	IF_EVT_DOWN,		IF_ACT_RST,	IF_STA_DOWN},
    {IF_STA_ANY,	IF_EVT_LOOP,		IF_ACT_RST,	IF_STA_LOOPBACK},
    {IF_STA_LOOPBACK,	IF_EVT_UNLOOP,		IF_ACT_NOTHING,	IF_STA_DOWN},
    {-1,		IF_EVT_NOTHING,		IF_ACT_NOTHING,	0},
};

static int vlink_cnt = 0;

const char * const if_event_names[] = {
	"NOTHING",
	"UP",
	"WAITTIMER",
	"BACKUPSEEN",
	"NEIGHBORCHANGE",
	"LOOP",
	"UNLOOP",
	"DOWN"
};

const char * const if_action_names[] = {
	"NOTHING",
	"START",
	"ELECT",
	"RESET"
};

int
if_fsm(struct iface *iface, enum iface_event event)
{
	int	old_state;
	int	new_state = 0;
	int	i, ret = 0;

	old_state = iface->state;

	for (i = 0; iface_fsm[i].state != -1; i++)
		if ((iface_fsm[i].state & old_state) &&
		    (iface_fsm[i].event == event)) {
			new_state = iface_fsm[i].new_state;
			break;
		}

	if (iface_fsm[i].state == -1) {
		/* event outside of the defined fsm, ignore it. */
		log_debug("if_fsm: interface %s, "
		    "event %s not expected in state %s", iface->name,
		    if_event_names[event], if_state_name(old_state));
		return (0);
	}

	switch (iface_fsm[i].action) {
	case IF_ACT_STRT:
		ret = if_act_start(iface);
		break;
	case IF_ACT_ELECT:
		ret = if_act_elect(iface);
		break;
	case IF_ACT_RST:
		ret = if_act_reset(iface);
		break;
	case IF_ACT_NOTHING:
		/* do nothing */
		break;
	}

	if (ret) {
		log_debug("if_fsm: error changing state for interface %s, "
		    "event %s, state %s", iface->name, if_event_names[event],
		    if_state_name(old_state));
		return (-1);
	}

	if (new_state != 0)
		iface->state = new_state;

	if (iface->state != old_state) {
		area_track(iface->area);
		orig_rtr_lsa(iface->area);
	}

	if (old_state & (IF_STA_MULTI | IF_STA_POINTTOPOINT) &&
	    (iface->state & (IF_STA_MULTI | IF_STA_POINTTOPOINT)) == 0)
		ospfe_demote_iface(iface, 0);
	if ((old_state & (IF_STA_MULTI | IF_STA_POINTTOPOINT)) == 0 &&
	    iface->state & (IF_STA_MULTI | IF_STA_POINTTOPOINT))
		ospfe_demote_iface(iface, 1);

	log_debug("if_fsm: event %s resulted in action %s and changing "
	    "state for interface %s from %s to %s",
	    if_event_names[event], if_action_names[iface_fsm[i].action],
	    iface->name, if_state_name(old_state), if_state_name(iface->state));

	return (ret);
}

struct iface *
if_new(struct kif *kif, struct kif_addr *ka)
{
	struct iface		*iface;

	if ((iface = calloc(1, sizeof(*iface))) == NULL)
		err(1, "if_new: calloc");

	iface->state = IF_STA_DOWN;

	LIST_INIT(&iface->nbr_list);
	TAILQ_INIT(&iface->ls_ack_list);
	TAILQ_INIT(&iface->auth_md_list);
	RB_INIT(&iface->lsa_tree);

	iface->crypt_seq_num = arc4random() & 0x0fffffff;

	if (kif == NULL) {
		iface->type = IF_TYPE_VIRTUALLINK;
		snprintf(iface->name, sizeof(iface->name), "vlink%d",
		    vlink_cnt++);
		iface->flags |= IFF_UP;
		iface->mtu = IP_MSS;
		return (iface);
	}

	strlcpy(iface->name, kif->ifname, sizeof(iface->name));

	/* get type */
	if (kif->flags & IFF_POINTOPOINT)
		iface->type = IF_TYPE_POINTOPOINT;
	if (kif->flags & IFF_BROADCAST &&
	    kif->flags & IFF_MULTICAST)
		iface->type = IF_TYPE_BROADCAST;
	if (kif->flags & IFF_LOOPBACK) {
		iface->type = IF_TYPE_POINTOPOINT;
		iface->passive = 1;
	}

	/* get mtu, index and flags */
	iface->mtu = kif->mtu;
	iface->ifindex = kif->ifindex;
	iface->rdomain = kif->rdomain;
	iface->flags = kif->flags;
	iface->linkstate = kif->link_state;
	iface->if_type = kif->if_type;
	iface->baudrate = kif->baudrate;

	/* set address, mask and p2p addr */
	iface->addr = ka->addr;
	iface->mask = ka->mask;
	if (kif->flags & IFF_POINTOPOINT) {
		iface->dst = ka->dstbrd;
	}

	return (iface);
}

void
if_del(struct iface *iface)
{
	struct nbr	*nbr = NULL;

	/* revert the demotion when the interface is deleted */
	if ((iface->state & (IF_STA_MULTI | IF_STA_POINTTOPOINT)) == 0)
		ospfe_demote_iface(iface, 1);

	/* clear lists etc */
	while ((nbr = LIST_FIRST(&iface->nbr_list)) != NULL)
		nbr_del(nbr);

	if (evtimer_pending(&iface->hello_timer, NULL))
		evtimer_del(&iface->hello_timer);
	if (evtimer_pending(&iface->wait_timer, NULL))
		evtimer_del(&iface->wait_timer);
	if (evtimer_pending(&iface->lsack_tx_timer, NULL))
		evtimer_del(&iface->lsack_tx_timer);

	ls_ack_list_clr(iface);
	md_list_clr(&iface->auth_md_list);
	free(iface);
}

void
if_init(struct ospfd_conf *xconf, struct iface *iface)
{
	/* init the dummy local neighbor */
	iface->self = nbr_new(ospfe_router_id(), iface, 1);

	/* set event handlers for interface */
	evtimer_set(&iface->lsack_tx_timer, ls_ack_tx_timer, iface);
	evtimer_set(&iface->hello_timer, if_hello_timer, iface);
	evtimer_set(&iface->wait_timer, if_wait_timer, iface);

	iface->fd = xconf->ospf_socket;

	ospfe_demote_iface(iface, 0);
}

/* timers */
void
if_hello_timer(int fd, short event, void *arg)
{
	struct iface *iface = arg;
	struct timeval tv;

	send_hello(iface);

	/* reschedule hello_timer */
	timerclear(&tv);
	if (iface->dead_interval == FAST_RTR_DEAD_TIME)
		tv.tv_usec = iface->fast_hello_interval * 1000;
	else
		tv.tv_sec = iface->hello_interval;
	if (evtimer_add(&iface->hello_timer, &tv) == -1)
		fatal("if_hello_timer");
}

void
if_start_hello_timer(struct iface *iface)
{
	struct timeval tv;

	timerclear(&tv);
	if (evtimer_add(&iface->hello_timer, &tv) == -1)
		fatal("if_start_hello_timer");
}

void
if_stop_hello_timer(struct iface *iface)
{
	if (evtimer_del(&iface->hello_timer) == -1)
		fatal("if_stop_hello_timer");
}

void
if_wait_timer(int fd, short event, void *arg)
{
	struct iface *iface = arg;

	if_fsm(iface, IF_EVT_WTIMER);
}

void
if_start_wait_timer(struct iface *iface)
{
	struct timeval	tv;

	timerclear(&tv);
	tv.tv_sec = iface->dead_interval;
	if (evtimer_add(&iface->wait_timer, &tv) == -1)
		fatal("if_start_wait_timer");
}

void
if_stop_wait_timer(struct iface *iface)
{
	if (evtimer_del(&iface->wait_timer) == -1)
		fatal("if_stop_wait_timer");
}

/* actions */
int
if_act_start(struct iface *iface)
{
	struct in_addr		 addr;
	struct timeval		 now;

	if (!(iface->flags & IFF_UP) ||
	    (!LINK_STATE_IS_UP(iface->linkstate) &&
	    !(iface->if_type == IFT_CARP &&
	    iface->linkstate == LINK_STATE_DOWN)))
		return (0);

	if (iface->if_type == IFT_CARP && iface->passive == 0) {
		/* force passive mode on carp interfaces */
		log_warnx("if_act_start: forcing interface %s to passive",
		    iface->name);
		iface->passive = 1;
	}

	gettimeofday(&now, NULL);
	iface->uptime = now.tv_sec;

	/* loopback interfaces have a special state and are passive */
	if (iface->flags & IFF_LOOPBACK)
		iface->state = IF_STA_LOOPBACK;

	if (iface->passive) {
		/* for an update of stub network entries */
		orig_rtr_lsa(iface->area);
		return (0);
	}

	switch (iface->type) {
	case IF_TYPE_POINTOPOINT:
		inet_pton(AF_INET, AllSPFRouters, &addr);
		if (if_join_group(iface, &addr))
			return (-1);
		iface->state = IF_STA_POINTTOPOINT;
		break;
	case IF_TYPE_VIRTUALLINK:
		iface->state = IF_STA_POINTTOPOINT;
		break;
	case IF_TYPE_POINTOMULTIPOINT:
	case IF_TYPE_NBMA:
		log_debug("if_act_start: type %s not supported, interface %s",
		    if_type_name(iface->type), iface->name);
		return (-1);
	case IF_TYPE_BROADCAST:
		inet_pton(AF_INET, AllSPFRouters, &addr);
		if (if_join_group(iface, &addr))
			return (-1);
		if (iface->priority == 0) {
			iface->state = IF_STA_DROTHER;
		} else {
			iface->state = IF_STA_WAITING;
			if_start_wait_timer(iface);
		}
		break;
	default:
		fatalx("if_act_start: unknown interface type");
	}

	/* hello timer needs to be started in any case */
	if_start_hello_timer(iface);
	return (0);
}

struct nbr *
if_elect(struct nbr *a, struct nbr *b)
{
	if (a->priority > b->priority)
		return (a);
	if (a->priority < b->priority)
		return (b);
	if (ntohl(a->id.s_addr) > ntohl(b->id.s_addr))
		return (a);
	return (b);
}

int
if_act_elect(struct iface *iface)
{
	struct in_addr	 addr;
	struct nbr	*nbr, *bdr = NULL, *dr = NULL;
	int		 round = 0;
	int		 changed = 0;
	int		 old_state;
	char		 b1[16], b2[16], b3[16], b4[16];

start:
	/* elect backup designated router */
	LIST_FOREACH(nbr, &iface->nbr_list, entry) {
		if (nbr->priority == 0 || nbr == dr ||	/* not electable */
		    nbr->state & NBR_STA_PRELIM ||	/* not available */
		    nbr->dr.s_addr == nbr->addr.s_addr)	/* don't elect DR */
			continue;
		if (bdr != NULL) {
			/*
			 * routers announcing themselves as BDR have higher
			 * precedence over those routers announcing a
			 * different BDR.
			 */
			if (nbr->bdr.s_addr == nbr->addr.s_addr) {
				if (bdr->bdr.s_addr == bdr->addr.s_addr)
					bdr = if_elect(bdr, nbr);
				else
					bdr = nbr;
			} else if (bdr->bdr.s_addr != bdr->addr.s_addr)
					bdr = if_elect(bdr, nbr);
		} else
			bdr = nbr;
	}

	/* elect designated router */
	LIST_FOREACH(nbr, &iface->nbr_list, entry) {
		if (nbr->priority == 0 || nbr->state & NBR_STA_PRELIM ||
		    (nbr != dr && nbr->dr.s_addr != nbr->addr.s_addr))
			/* only DR may be elected check priority too */
			continue;
		if (dr == NULL)
			dr = nbr;
		else
			dr = if_elect(dr, nbr);
	}

	if (dr == NULL) {
		/* no designated router found use backup DR */
		dr = bdr;
		bdr = NULL;
	}

	/*
	 * if we are involved in the election (e.g. new DR or no
	 * longer BDR) redo the election
	 */
	if (round == 0 &&
	    ((iface->self == dr && iface->self != iface->dr) ||
	    (iface->self != dr && iface->self == iface->dr) ||
	    (iface->self == bdr && iface->self != iface->bdr) ||
	    (iface->self != bdr && iface->self == iface->bdr))) {
		/*
		 * Reset announced DR/BDR to calculated one, so
		 * that we may get elected in the second round.
		 * This is needed to drop from a DR to a BDR.
		 */
		iface->self->dr.s_addr = dr->addr.s_addr;
		if (bdr)
			iface->self->bdr.s_addr = bdr->addr.s_addr;
		round = 1;
		goto start;
	}

	log_debug("if_act_elect: interface %s old dr %s new dr %s, "
	    "old bdr %s new bdr %s", iface->name,
	    iface->dr ? inet_ntop(AF_INET, &iface->dr->addr, b1, sizeof(b1)) :
	    "none", dr ? inet_ntop(AF_INET, &dr->addr, b2, sizeof(b2)) : "none",
	    iface->bdr ? inet_ntop(AF_INET, &iface->bdr->addr, b3, sizeof(b3)) :
	    "none", bdr ? inet_ntop(AF_INET, &bdr->addr, b4, sizeof(b4)) :
	    "none");

	/*
	 * After the second round still DR or BDR change state to DR or BDR,
	 * etc.
	 */
	old_state = iface->state;
	if (dr == iface->self)
		iface->state = IF_STA_DR;
	else if (bdr == iface->self)
		iface->state = IF_STA_BACKUP;
	else
		iface->state = IF_STA_DROTHER;

	/* TODO if iface is NBMA send all non eligible neighbors event Start */

	/*
	 * if DR or BDR changed issue a AdjOK? event for all neighbors > 2-Way
	 */
	if (iface->dr != dr || iface->bdr != bdr)
		changed = 1;

	iface->dr = dr;
	iface->bdr = bdr;

	if (changed) {
		inet_pton(AF_INET, AllDRouters, &addr);
		if (old_state & IF_STA_DRORBDR &&
		    (iface->state & IF_STA_DRORBDR) == 0) {
			if (if_leave_group(iface, &addr))
				return (-1);
		} else if ((old_state & IF_STA_DRORBDR) == 0 &&
		    iface->state & IF_STA_DRORBDR) {
			if (if_join_group(iface, &addr))
				return (-1);
		}

		LIST_FOREACH(nbr, &iface->nbr_list, entry) {
			if (nbr->state & NBR_STA_BIDIR)
				nbr_fsm(nbr, NBR_EVT_ADJ_OK);
		}

		orig_rtr_lsa(iface->area);
		if (iface->state & IF_STA_DR || old_state & IF_STA_DR)
			orig_net_lsa(iface);
	}

	if_start_hello_timer(iface);
	return (0);
}

int
if_act_reset(struct iface *iface)
{
	struct nbr		*nbr = NULL;
	struct in_addr		 addr;

	if (iface->passive) {
		/* for an update of stub network entries */
		orig_rtr_lsa(iface->area);
		return (0);
	}

	switch (iface->type) {
	case IF_TYPE_POINTOPOINT:
	case IF_TYPE_BROADCAST:
		/* try to cleanup */
		inet_pton(AF_INET, AllSPFRouters, &addr);
		if_leave_group(iface, &addr);
		if (iface->state & IF_STA_DRORBDR) {
			inet_pton(AF_INET, AllDRouters, &addr);
			if_leave_group(iface, &addr);
		}
		break;
	case IF_TYPE_VIRTUALLINK:
		/* nothing */
		break;
	case IF_TYPE_NBMA:
	case IF_TYPE_POINTOMULTIPOINT:
		log_debug("if_act_reset: type %s not supported, interface %s",
		    if_type_name(iface->type), iface->name);
		return (-1);
	default:
		fatalx("if_act_reset: unknown interface type");
	}

	LIST_FOREACH(nbr, &iface->nbr_list, entry) {
		if (nbr_fsm(nbr, NBR_EVT_KILL_NBR)) {
			log_debug("if_act_reset: error killing neighbor %s",
			    inet_ntoa(nbr->id));
		}
	}

	iface->dr = NULL;
	iface->bdr = NULL;

	ls_ack_list_clr(iface);
	stop_ls_ack_tx_timer(iface);
	if_stop_hello_timer(iface);
	if_stop_wait_timer(iface);

	/* send empty hello to tell everybody that we are going down */
	send_hello(iface);

	return (0);
}

struct ctl_iface *
if_to_ctl(struct iface *iface)
{
	static struct ctl_iface	 ictl;
	struct timeval		 tv, now, res;
	struct nbr		*nbr;

	memcpy(ictl.name, iface->name, sizeof(ictl.name));
	memcpy(&ictl.addr, &iface->addr, sizeof(ictl.addr));
	memcpy(&ictl.mask, &iface->mask, sizeof(ictl.mask));
	ictl.rtr_id.s_addr = ospfe_router_id();
	memcpy(&ictl.area, &iface->area->id, sizeof(ictl.area));
	if (iface->dr) {
		memcpy(&ictl.dr_id, &iface->dr->id, sizeof(ictl.dr_id));
		memcpy(&ictl.dr_addr, &iface->dr->addr, sizeof(ictl.dr_addr));
	} else {
		bzero(&ictl.dr_id, sizeof(ictl.dr_id));
		bzero(&ictl.dr_addr, sizeof(ictl.dr_addr));
	}
	if (iface->bdr) {
		memcpy(&ictl.bdr_id, &iface->bdr->id, sizeof(ictl.bdr_id));
		memcpy(&ictl.bdr_addr, &iface->bdr->addr,
		    sizeof(ictl.bdr_addr));
	} else {
		bzero(&ictl.bdr_id, sizeof(ictl.bdr_id));
		bzero(&ictl.bdr_addr, sizeof(ictl.bdr_addr));
	}
	ictl.ifindex = iface->ifindex;
	ictl.state = iface->state;
	ictl.mtu = iface->mtu;
	ictl.nbr_cnt = 0;
	ictl.adj_cnt = 0;
	ictl.baudrate = iface->baudrate;
	ictl.dead_interval = iface->dead_interval;
	ictl.fast_hello_interval = iface->fast_hello_interval;
	ictl.transmit_delay = iface->transmit_delay;
	ictl.hello_interval = iface->hello_interval;
	ictl.flags = iface->flags;
	ictl.metric = iface->metric;
	ictl.rxmt_interval = iface->rxmt_interval;
	ictl.type = iface->type;
	ictl.linkstate = iface->linkstate;
	ictl.if_type = iface->if_type;
	ictl.priority = iface->priority;
	ictl.passive = iface->passive;
	ictl.auth_type = iface->auth_type;
	ictl.auth_keyid = iface->auth_keyid;

	memcpy(ictl.dependon, iface->dependon, sizeof(ictl.dependon));
	ictl.depend_ok = iface->depend_ok;

	gettimeofday(&now, NULL);
	if (evtimer_pending(&iface->hello_timer, &tv)) {
		timersub(&tv, &now, &res);
		ictl.hello_timer = res;
	} else {
		ictl.hello_timer.tv_sec = -1;
	}

	if (iface->state != IF_STA_DOWN &&
	    iface->uptime != 0) {
		ictl.uptime = now.tv_sec - iface->uptime;
	} else
		ictl.uptime = 0;

	LIST_FOREACH(nbr, &iface->nbr_list, entry) {
		if (nbr == iface->self)
			continue;
		ictl.nbr_cnt++;
		if (nbr->state & NBR_STA_ADJFORM)
			ictl.adj_cnt++;
	}

	return (&ictl);
}

/* misc */
int
if_set_recvif(int fd, int enable)
{
	if (setsockopt(fd, IPPROTO_IP, IP_RECVIF, &enable,
	    sizeof(enable)) == -1) {
		log_warn("if_set_recvif: error setting IP_RECVIF");
		return (-1);
	}
	return (0);
}

void
if_set_sockbuf(int fd)
{
	int	bsize;

	bsize = 256 * 1024;
	while (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bsize,
	    sizeof(bsize)) == -1)
		bsize /= 2;

	if (bsize != 256 * 1024)
		log_warnx("if_set_sockbuf: recvbuf size only %d", bsize);

	bsize = 64 * 1024;
	while (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bsize,
	    sizeof(bsize)) == -1)
		bsize /= 2;

	if (bsize != 64 * 1024)
		log_warnx("if_set_sockbuf: sendbuf size only %d", bsize);
}

/*
 * only one JOIN or DROP per interface and address is allowed so we need
 * to keep track of what is added and removed.
 */
struct if_group_count {
	LIST_ENTRY(if_group_count)	entry;
	struct in_addr			addr;
	unsigned int			ifindex;
	int				count;
};

LIST_HEAD(,if_group_count) ifglist = LIST_HEAD_INITIALIZER(ifglist);

int
if_join_group(struct iface *iface, struct in_addr *addr)
{
	struct ip_mreqn		 mreq;
	struct if_group_count	*ifg;

	switch (iface->type) {
	case IF_TYPE_POINTOPOINT:
	case IF_TYPE_BROADCAST:
		LIST_FOREACH(ifg, &ifglist, entry)
			if (iface->ifindex == ifg->ifindex &&
			    addr->s_addr == ifg->addr.s_addr)
				break;
		if (ifg == NULL) {
			if ((ifg = calloc(1, sizeof(*ifg))) == NULL)
				fatal("if_join_group");
			ifg->addr.s_addr = addr->s_addr;
			ifg->ifindex = iface->ifindex;
			LIST_INSERT_HEAD(&ifglist, ifg, entry);
		}

		if (ifg->count++ != 0)
			/* already joined */
			return (0);

		memset(&mreq, 0, sizeof(mreq));
		mreq.imr_multiaddr.s_addr = addr->s_addr;
		mreq.imr_ifindex = iface->ifindex;

		if (setsockopt(iface->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		    (void *)&mreq, sizeof(mreq)) == -1) {
			log_warn("if_join_group: error IP_ADD_MEMBERSHIP, "
			    "interface %s address %s", iface->name,
			    inet_ntoa(*addr));
			return (-1);
		}
		break;
	case IF_TYPE_POINTOMULTIPOINT:
	case IF_TYPE_VIRTUALLINK:
	case IF_TYPE_NBMA:
		log_debug("if_join_group: type %s not supported, interface %s",
		    if_type_name(iface->type), iface->name);
		return (-1);
	default:
		fatalx("if_join_group: unknown interface type");
	}

	return (0);
}

int
if_leave_group(struct iface *iface, struct in_addr *addr)
{
	struct ip_mreqn		 mreq;
	struct if_group_count	*ifg;

	switch (iface->type) {
	case IF_TYPE_POINTOPOINT:
	case IF_TYPE_BROADCAST:
		LIST_FOREACH(ifg, &ifglist, entry)
			if (iface->ifindex == ifg->ifindex &&
			    addr->s_addr == ifg->addr.s_addr)
				break;

		/* if interface is not found just try to drop membership */
		if (ifg) {
			if (--ifg->count != 0)
				/* others still joined */
				return (0);

			LIST_REMOVE(ifg, entry);
			free(ifg);
		}

		memset(&mreq, 0, sizeof(mreq));
		mreq.imr_multiaddr.s_addr = addr->s_addr;
		mreq.imr_ifindex = iface->ifindex;

		if (setsockopt(iface->fd, IPPROTO_IP, IP_DROP_MEMBERSHIP,
		    (void *)&mreq, sizeof(mreq)) == -1) {
			log_warn("if_leave_group: error IP_DROP_MEMBERSHIP, "
			    "interface %s address %s", iface->name,
			    inet_ntoa(*addr));
			return (-1);
		}
		break;
	case IF_TYPE_POINTOMULTIPOINT:
	case IF_TYPE_VIRTUALLINK:
	case IF_TYPE_NBMA:
		log_debug("if_leave_group: type %s not supported, interface %s",
		    if_type_name(iface->type), iface->name);
		return (-1);
	default:
		fatalx("if_leave_group: unknown interface type");
	}

	return (0);
}

int
if_set_mcast(struct iface *iface)
{
	struct ip_mreqn		 mreq;

	switch (iface->type) {
	case IF_TYPE_POINTOPOINT:
	case IF_TYPE_BROADCAST:
		memset(&mreq, 0, sizeof(mreq));
		mreq.imr_ifindex = iface->ifindex;
		if (setsockopt(iface->fd, IPPROTO_IP, IP_MULTICAST_IF,
		    &mreq, sizeof(mreq)) == -1) {
			log_warn("if_set_mcast: error setting "
			    "IP_MULTICAST_IF, interface %s", iface->name);
			return (-1);
		}
		break;
	case IF_TYPE_POINTOMULTIPOINT:
	case IF_TYPE_VIRTUALLINK:
	case IF_TYPE_NBMA:
		log_debug("if_set_mcast: type %s not supported, interface %s",
		    if_type_name(iface->type), iface->name);
		return (-1);
	default:
		fatalx("if_set_mcast: unknown interface type");
	}

	return (0);
}

int
if_set_mcast_loop(int fd)
{
	u_int8_t	loop = 0;

	if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP,
	    (char *)&loop, sizeof(loop)) == -1) {
		log_warn("if_set_mcast_loop: error setting IP_MULTICAST_LOOP");
		return (-1);
	}

	return (0);
}

int
if_set_ip_hdrincl(int fd)
{
	int	hincl = 1;

	if (setsockopt(fd, IPPROTO_IP, IP_HDRINCL, &hincl, sizeof(hincl)) == -1) {
		log_warn("if_set_ip_hdrincl: error setting IP_HDRINCL");
		return (-1);
	}

	return (0);
}
