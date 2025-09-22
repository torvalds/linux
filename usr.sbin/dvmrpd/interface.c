/*	$OpenBSD: interface.c,v 1.13 2024/08/21 09:18:47 florian Exp $ */

/*
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2005, 2006 Esben Norby <norby@openbsd.org>
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
#include <netinet/ip_mroute.h>
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

#include "igmp.h"
#include "dvmrpd.h"
#include "dvmrp.h"
#include "log.h"
#include "dvmrpe.h"

extern struct dvmrpd_conf	*conf;

void	 if_probe_timer(int, short, void *);
int	 if_start_probe_timer(struct iface *);
int	 if_stop_probe_timer(struct iface *);
void	 if_query_timer(int, short, void *);
int	 if_start_query_timer(struct iface *);
int	 if_stop_query_timer(struct iface *);
void	 if_querier_present_timer(int, short, void *);
int	 if_start_querier_present_timer(struct iface *);
int	 if_stop_querier_present_timer(struct iface *);
int	 if_reset_querier_present_timer(struct iface *);
int	 if_act_start(struct iface *);
int	 if_act_query_seen(struct iface *);
int	 if_act_reset(struct iface *);

struct {
	int			state;
	enum iface_event	event;
	enum iface_action	action;
	int			new_state;
} iface_fsm[] = {
    /* current state	event that happened	action to take	resulting state */
    {IF_STA_DOWN,	IF_EVT_UP,		IF_ACT_STRT,	0},
    {IF_STA_ACTIVE,	IF_EVT_QRECVD,		IF_ACT_QPRSNT,	0},
    {IF_STA_NONQUERIER, IF_EVT_QPRSNTTMOUT,	IF_ACT_STRT,	0},
    {IF_STA_ANY,	IF_EVT_DOWN,		IF_ACT_RST,	IF_STA_DOWN},
    {-1,		IF_EVT_NOTHING,		IF_ACT_NOTHING,	0},
};

const char * const if_action_names[] = {
	"NOTHING",
	"START",
	"QPRSNT",
	"RESET"
};

static const char * const if_event_names[] = {
	"NOTHING",
	"UP",
	"QTMOUT",
	"QRECVD",
	"QPRSNTTMOUT",
	"DOWN"
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
		/* XXX event outside of the defined fsm, ignore it. */
		log_debug("fsm_if: interface %s, "
		    "event '%s' not expected in state '%s'", iface->name,
		    if_event_name(event), if_state_name(old_state));
		return (0);
	}

	switch (iface_fsm[i].action) {
	case IF_ACT_STRT:
		ret = if_act_start(iface);
		break;
	case IF_ACT_QPRSNT:
		ret = if_act_query_seen(iface);
		break;
	case IF_ACT_RST:
		ret = if_act_reset(iface);
		break;
	case IF_ACT_NOTHING:
		/* do nothing */
		break;
	}

	if (ret) {
		log_debug("fsm_if: error changing state for interface %s, "
		    "event '%s', state '%s'", iface->name, if_event_name(event),
		    if_state_name(old_state));
		return (-1);
	}

	if (new_state != 0)
		iface->state = new_state;

	log_debug("fsm_if: event '%s' resulted in action '%s' and changing "
	    "state for interface %s from '%s' to '%s'",
	    if_event_name(event), if_action_name(iface_fsm[i].action),
	    iface->name, if_state_name(old_state), if_state_name(iface->state));

	return (ret);
}

struct iface *
if_find_index(u_short ifindex)
{
	struct iface	*iface;

	LIST_FOREACH(iface, &conf->iface_list, entry) {
		if (iface->ifindex == ifindex)
			return (iface);
	}

	return (NULL);
}

struct iface *
if_new(struct kif *kif)
{
	struct sockaddr_in	*sain;
	struct iface		*iface;
	struct ifreq		*ifr;
	int			 s;

	if ((iface = calloc(1, sizeof(*iface))) == NULL)
		err(1, "if_new: calloc");

	iface->state = IF_STA_DOWN;
	iface->passive = 1;

	LIST_INIT(&iface->nbr_list);
	TAILQ_INIT(&iface->group_list);
	TAILQ_INIT(&iface->rde_group_list);
	strlcpy(iface->name, kif->ifname, sizeof(iface->name));

	if ((ifr = calloc(1, sizeof(*ifr))) == NULL)
		err(1, "if_new: calloc");

	/* set up ifreq */
	strlcpy(ifr->ifr_name, kif->ifname, sizeof(ifr->ifr_name));
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		err(1, "if_new: socket");

	/* get type */
	if ((kif->flags & IFF_POINTOPOINT))
		iface->type = IF_TYPE_POINTOPOINT;
	if ((kif->flags & IFF_BROADCAST) &&
	    (kif->flags & IFF_MULTICAST))
		iface->type = IF_TYPE_BROADCAST;

	/* get mtu, index and flags */
	iface->mtu = kif->mtu;
	iface->ifindex = kif->ifindex;
	iface->flags = kif->flags;
	iface->linkstate = kif->link_state;
	iface->if_type = kif->if_type;
	iface->baudrate = kif->baudrate;

	/* get address */
	if (ioctl(s, SIOCGIFADDR, (caddr_t)ifr) == -1)
		err(1, "if_new: cannot get address");
	sain = (struct sockaddr_in *) &ifr->ifr_addr;
	iface->addr = sain->sin_addr;

	/* get mask */
	if (ioctl(s, SIOCGIFNETMASK, (caddr_t)ifr) == -1)
		err(1, "if_new: cannot get mask");
	sain = (struct sockaddr_in *) &ifr->ifr_addr;
	iface->mask = sain->sin_addr;

	/* get p2p dst address */
	if (iface->type == IF_TYPE_POINTOPOINT) {
		if (ioctl(s, SIOCGIFDSTADDR, (caddr_t)ifr) == -1)
			err(1, "if_new: cannot get dst addr");
		sain = (struct sockaddr_in *) &ifr->ifr_addr;
		iface->dst = sain->sin_addr;
	}

	free(ifr);
	close(s);

	return (iface);
}

void
if_init(struct dvmrpd_conf *xconf, struct iface *iface)
{
	/* set event handlers for interface */
	evtimer_set(&iface->probe_timer, if_probe_timer, iface);
	evtimer_set(&iface->query_timer, if_query_timer, iface);
	evtimer_set(&iface->querier_present_timer, if_querier_present_timer,
	    iface);

	TAILQ_INIT(&iface->rr_list);

	iface->fd = xconf->dvmrp_socket;
	iface->gen_id = xconf->gen_id;
}

int
if_del(struct iface *iface)
{
	struct nbr	*nbr = NULL;

	/* clear lists etc */
	while ((nbr = LIST_FIRST(&iface->nbr_list)) != NULL) {
		LIST_REMOVE(nbr, entry);
		nbr_del(nbr);
	}
	group_list_clr(iface);

	return (-1);
}

int
if_nbr_list_empty(struct iface *iface)
{
	return (LIST_EMPTY(&iface->nbr_list));
}

/* timers */
void
if_probe_timer(int fd, short event, void *arg)
{
	struct iface *iface = arg;
	struct timeval tv;

	send_probe(iface);

	/* reschedule probe_timer */
	if (!iface->passive) {
		timerclear(&tv);
		tv.tv_sec = iface->probe_interval;
		evtimer_add(&iface->probe_timer, &tv);
	}
}

int
if_start_probe_timer(struct iface *iface)
{
	struct timeval tv;

	timerclear(&tv);
	return (evtimer_add(&iface->probe_timer, &tv));
}

int
if_stop_probe_timer(struct iface *iface)
{
	return (evtimer_del(&iface->probe_timer));
}

void
if_query_timer(int fd, short event, void *arg)
{
	struct iface *iface = arg;
	struct timeval tv;

	/* send a general query */
	send_igmp_query(iface, NULL);

	/* reschedule query_timer */
	if (!iface->passive) {
		timerclear(&tv);
		if (iface->startup_query_counter != 0) {
			tv.tv_sec = iface->startup_query_interval;
			iface->startup_query_counter--;
		} else
			tv.tv_sec = iface->query_interval;

		evtimer_add(&iface->query_timer, &tv);
	}
}

int
if_start_query_timer(struct iface *iface)
{
	struct timeval tv;

	timerclear(&tv);
	return (evtimer_add(&iface->query_timer, &tv));
}

int
if_stop_query_timer(struct iface *iface)
{
	return (evtimer_del(&iface->query_timer));
}

void
if_querier_present_timer(int fd, short event, void *arg)
{
	struct iface *iface = arg;

	if_fsm(iface, IF_EVT_QPRSNTTMOUT);
}

int
if_start_querier_present_timer(struct iface *iface)
{
	struct timeval tv;

	/* Other Querier Present Interval */
	timerclear(&tv);
	tv.tv_sec = iface->robustness * iface->query_interval +
	    (iface->query_resp_interval / 2);

	return (evtimer_add(&iface->querier_present_timer, &tv));
}

int
if_stop_querier_present_timer(struct iface *iface)
{
	return (evtimer_del(&iface->querier_present_timer));
}

int
if_reset_querier_present_timer(struct iface *iface)
{
	struct timeval	tv;

	/* Other Querier Present Interval */
	timerclear(&tv);
	tv.tv_sec = iface->robustness * iface->query_interval +
	    (iface->query_resp_interval / 2);

	return (evtimer_add(&iface->querier_present_timer, &tv));
}

/* actions */
int
if_act_start(struct iface *iface)
{
	struct in_addr	 addr;
	struct timeval	 now;

	if (iface->passive) {
		log_debug("if_act_start: cannot start passive interface %s",
		    iface->name);
		return (-1);
	}

	if (!((iface->flags & IFF_UP) && LINK_STATE_IS_UP(iface->linkstate))) {
		log_debug("if_act_start: interface %s link down",
		    iface->name);
		return (0);
	}

	gettimeofday(&now, NULL);
	iface->uptime = now.tv_sec;

	switch (iface->type) {
	case IF_TYPE_POINTOPOINT:
	case IF_TYPE_BROADCAST:
		inet_pton(AF_INET, AllSystems, &addr);
		if (if_join_group(iface, &addr)) {
			log_warnx("if_act_start: error joining group %s, "
			    "interface %s", inet_ntoa(addr), iface->name);
			return (-1);
		}
		inet_pton(AF_INET, AllRouters, &addr);
		if (if_join_group(iface, &addr)) {
			log_warnx("if_act_start: error joining group %s, "
			    "interface %s", inet_ntoa(addr), iface->name);
			return (-1);
		}
		inet_pton(AF_INET, AllDVMRPRouters, &addr);
		if (if_join_group(iface, &addr)) {
			log_warnx("if_act_start: error joining group %s, "
			    "interface %s", inet_ntoa(addr), iface->name);
			return (-1);
		}

		iface->state = IF_STA_QUERIER;
		if_start_query_timer(iface);
		if_start_probe_timer(iface);
		iface->startup_query_counter = iface->startup_query_cnt;
		break;
	default:
		fatalx("if_act_start: unknown type");
	}

	return (0);
}

int
if_act_query_seen(struct iface *iface)
{
	log_debug("if_act_query_seen: interface %s", iface->name);

	switch (iface->type) {
	case IF_TYPE_POINTOPOINT:
	case IF_TYPE_BROADCAST:
		iface->state = IF_STA_NONQUERIER;
		if_stop_query_timer(iface);
		if_reset_querier_present_timer(iface);
		break;
	default:
		fatalx("if_act_querier_seen: unknown type");
	}

	return (0);
}

int
if_act_reset(struct iface *iface)
{
	struct in_addr	 addr;
	struct nbr	*nbr;

	switch (iface->type) {
	case IF_TYPE_POINTOPOINT:
	case IF_TYPE_BROADCAST:
		inet_pton(AF_INET, AllSystems, &addr);
		if (if_leave_group(iface, &addr)) {
			log_warnx("if_act_reset: error leaving group %s, "
			    "interface %s", inet_ntoa(addr), iface->name);
			return (-1);
		}
		inet_pton(AF_INET, AllRouters, &addr);
		if (if_leave_group(iface, &addr)) {
			log_warnx("if_act_reset: error leaving group %s, "
			    "interface %s", inet_ntoa(addr), iface->name);
			return (-1);
		}
		inet_pton(AF_INET, AllDVMRPRouters, &addr);
		if (if_leave_group(iface, &addr)) {
			log_warnx("if_act_reset: error leaving group %s, "
			    "interface %s", inet_ntoa(addr), iface->name);
			return (-1);
		}

		iface->state = IF_STA_DOWN;
		iface->gen_id++;
		if_stop_query_timer(iface);
		if_stop_querier_present_timer(iface);
		/* XXX clear nbr list? */
		break;
	default:
		fatalx("if_act_reset: unknown type");
	}

	LIST_FOREACH(nbr, &iface->nbr_list, entry) {
		if (nbr_fsm(nbr, NBR_EVT_KILL_NBR)) {
			log_debug("if_act_reset: error killing neighbor %s",
			    inet_ntoa(nbr->id));
		}
	}

	group_list_clr(iface);	/* XXX clear group list? */

	return (0);
}

const char *
if_event_name(int event)
{
	return (if_event_names[event]);
}

const char *
if_action_name(int action)
{
	return (if_action_names[action]);
}

/* misc */
int
if_set_mcast_ttl(int fd, u_int8_t ttl)
{
	if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL,
	    (char *)&ttl, sizeof(ttl)) == -1) {
		log_warn("if_set_mcast_ttl: error setting "
		    "IP_MULTICAST_TTL to %d", ttl);
		return (-1);
	}

	return (0);
}

int
if_set_tos(int fd, int tos)
{
	if (setsockopt(fd, IPPROTO_IP, IP_TOS,
	    (int *)&tos, sizeof(tos)) == -1) {
		log_warn("if_set_tos: error setting IP_TOS to 0x%x", tos);
		return (-1);
	}

	return (0);
}

void
if_set_recvbuf(int fd)
{
	int	bsize;

	bsize = 65535;
	while (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bsize,
	    sizeof(bsize)) == -1)
		bsize /= 2;
}

int
if_join_group(struct iface *iface, struct in_addr *addr)
{
	struct ip_mreq	 mreq;

	switch (iface->type) {
	case IF_TYPE_POINTOPOINT:
	case IF_TYPE_BROADCAST:
		mreq.imr_multiaddr.s_addr = addr->s_addr;
		mreq.imr_interface.s_addr = iface->addr.s_addr;

		if (setsockopt(iface->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		    (void *)&mreq, sizeof(mreq)) == -1) {
			log_debug("if_join_group: error IP_ADD_MEMBERSHIP, "
			    "interface %s", iface->name);
			return (-1);
		}
		break;
	default:
		fatalx("if_join_group: unknown interface type");
	}

	return (0);
}

int
if_leave_group(struct iface *iface, struct in_addr *addr)
{
	struct ip_mreq	 mreq;

	switch (iface->type) {
	case IF_TYPE_POINTOPOINT:
	case IF_TYPE_BROADCAST:
		mreq.imr_multiaddr.s_addr = addr->s_addr;
		mreq.imr_interface.s_addr = iface->addr.s_addr;

		if (setsockopt(iface->fd, IPPROTO_IP, IP_DROP_MEMBERSHIP,
		    (void *)&mreq, sizeof(mreq)) == -1) {
			log_debug("if_leave_group: error IP_DROP_MEMBERSHIP, "
			    "interface %s", iface->name);
			return (-1);
		}
		break;
	default:
		fatalx("if_leave_group: unknown interface type");
	}

	return (0);
}

int
if_set_mcast(struct iface *iface)
{
	switch (iface->type) {
	case IF_TYPE_POINTOPOINT:
	case IF_TYPE_BROADCAST:
		if (setsockopt(iface->fd, IPPROTO_IP, IP_MULTICAST_IF,
		    (char *)&iface->addr.s_addr,
		    sizeof(iface->addr.s_addr)) == -1) {
			log_debug("if_set_mcast: error setting "
			    "IP_MULTICAST_IF, interface %s", iface->name);
			return (-1);
		}
		break;
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

struct ctl_iface *
if_to_ctl(struct iface *iface)
{
	static struct ctl_iface	 ictl;
	struct timeval		 tv, now, res;

	memcpy(ictl.name, iface->name, sizeof(ictl.name));
	memcpy(&ictl.addr, &iface->addr, sizeof(ictl.addr));
	memcpy(&ictl.mask, &iface->mask, sizeof(ictl.mask));
	memcpy(&ictl.querier, &iface->querier, sizeof(ictl.querier));

	ictl.ifindex = iface->ifindex;
	ictl.state = iface->state;
	ictl.mtu = iface->mtu;
	ictl.nbr_cnt = iface->nbr_cnt;
	ictl.adj_cnt = iface->adj_cnt;

	ictl.gen_id = iface->gen_id;
	ictl.group_cnt = iface->group_cnt;
	ictl.probe_interval = iface->probe_interval;
	ictl.query_interval = iface->query_interval;
	ictl.query_resp_interval = iface->query_resp_interval;
	ictl.recv_query_resp_interval = iface->recv_query_resp_interval;
	ictl.group_member_interval = iface->group_member_interval;
	ictl.querier_present_interval = iface->querier_present_interval;
	ictl.startup_query_interval = iface->startup_query_interval;
	ictl.startup_query_cnt = iface->startup_query_cnt;
	ictl.last_member_query_interval = iface->last_member_query_interval;
	ictl.last_member_query_cnt = iface->last_member_query_cnt;
	ictl.last_member_query_time = iface->last_member_query_time;
	ictl.v1_querier_present_tmout = iface->v1_querier_present_tmout;
	ictl.v1_host_present_interval = iface->v1_host_present_interval;
	ictl.dead_interval = iface->dead_interval;

	ictl.baudrate = iface->baudrate;
	ictl.flags = iface->flags;
	ictl.metric = iface->metric;
	ictl.type = iface->type;
	ictl.robustness = iface->robustness;
	ictl.linkstate = iface->linkstate;
	ictl.passive = iface->passive;
	ictl.igmp_version = iface->igmp_version;
	ictl.if_type = iface->if_type;

	gettimeofday(&now, NULL);
	if (evtimer_pending(&iface->probe_timer, &tv)) {
		timersub(&tv, &now, &res);
		ictl.probe_timer = res.tv_sec;
	} else
		ictl.probe_timer = -1;

	if (evtimer_pending(&iface->query_timer, &tv)) {
		timersub(&tv, &now, &res);
		ictl.query_timer = res.tv_sec;
	} else
		ictl.query_timer = -1;

	if (evtimer_pending(&iface->querier_present_timer, &tv)) {
		timersub(&tv, &now, &res);
		ictl.querier_present_timer = res.tv_sec;
	} else
		ictl.querier_present_timer = -1;

	if (iface->state != IF_STA_DOWN) {
		ictl.uptime = now.tv_sec - iface->uptime;
	} else
		ictl.uptime = 0;

	return (&ictl);
}
