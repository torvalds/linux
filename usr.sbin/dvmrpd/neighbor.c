/*	$OpenBSD: neighbor.c,v 1.7 2009/04/16 20:11:12 michele Exp $ */

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
#include <arpa/inet.h>
#include <net/if.h>

#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <event.h>

#include "igmp.h"
#include "dvmrpd.h"
#include "dvmrp.h"
#include "dvmrpe.h"
#include "log.h"
#include "rde.h"

LIST_HEAD(nbr_head, nbr);

struct nbr_table {
	struct nbr_head		*hashtbl;
	u_int32_t		 hashmask;
} nbrtable;

#define NBR_HASH(x)		\
	&nbrtable.hashtbl[(x) & nbrtable.hashmask]

u_int32_t	peercnt = NBR_CNTSTART;

struct {
	int		state;
	enum nbr_event	event;
	enum nbr_action	action;
	int		new_state;
} nbr_fsm_tbl[] = {
    /* current state	event that happened	action to take		resulting state */
    {NBR_STA_DOWN,	NBR_EVT_PROBE_RCVD,	NBR_ACT_STRT_ITIMER,	NBR_STA_1_WAY},
    {NBR_STA_ACTIVE,	NBR_EVT_PROBE_RCVD,	NBR_ACT_RST_ITIMER,	0},
    {NBR_STA_1_WAY,	NBR_EVT_2_WAY_RCVD,	NBR_ACT_STRT_ITIMER,	NBR_STA_2_WAY},
    {NBR_STA_ACTIVE,	NBR_EVT_1_WAY_RCVD,	NBR_ACT_RESET,		NBR_STA_1_WAY},
    {NBR_STA_ANY,	NBR_EVT_KILL_NBR,	NBR_ACT_DEL,		NBR_STA_DOWN},
    {NBR_STA_ANY,	NBR_EVT_ITIMER,		NBR_ACT_DEL,		NBR_STA_DOWN},
    {-1,		NBR_EVT_NOTHING,	NBR_ACT_NOTHING,	0},
};

const char * const nbr_event_names[] = {
	"NOTHING",
	"PROBE RCVD",
	"1-WAY RCVD",
	"2-WAY RCVD",
	"KILL NBR",
	"ITIMER",
	"LL DOWN"
};

const char * const nbr_action_names[] = {
	"NOTHING",
	"RESET ITIMER",
	"START ITIMER",
	"RESET",
	"DELETE",
	"CLEAR LISTS"
};

int
nbr_fsm(struct nbr *nbr, enum nbr_event event)
{
	struct timeval	now;
	int		old_state;
	int		new_state = 0;
	int		i, ret = 0;

	old_state = nbr->state;
	for (i = 0; nbr_fsm_tbl[i].state != -1; i++)
		if ((nbr_fsm_tbl[i].state & old_state) &&
		    (nbr_fsm_tbl[i].event == event)) {
			new_state = nbr_fsm_tbl[i].new_state;
			break;
		}

	if (nbr_fsm_tbl[i].state == -1) {
		/* XXX event outside of the defined fsm, ignore it. */
		log_warnx("nbr_fsm: neighbor ID %s, "
		    "event '%s' not expected in state '%s'",
		    inet_ntoa(nbr->id), nbr_event_name(event),
		    nbr_state_name(old_state));
		return (0);
	}

	switch (nbr_fsm_tbl[i].action) {
	case NBR_ACT_RST_ITIMER:
		ret = nbr_act_reset_itimer(nbr);
		break;
	case NBR_ACT_STRT_ITIMER:
		ret = nbr_act_start_itimer(nbr);
		break;
	case NBR_ACT_RESET:
		/* XXX nbr action reset */
		break;
	case NBR_ACT_DEL:
		ret = nbr_act_delete(nbr);
		break;
	case NBR_ACT_CLR_LST:
		ret = nbr_act_clear_lists(nbr);
		break;
	case NBR_ACT_NOTHING:
		/* do nothing */
		break;
	}

	if (ret) {
		log_warnx("nbr_fsm: error changing state for neighbor ID %s, "
		    "event '%s', state '%s'", inet_ntoa(nbr->id),
		    nbr_event_name(event), nbr_state_name(old_state));
		return (-1);
	}

	if (new_state != 0)
		nbr->state = new_state;

	if (old_state != nbr->state) {
		if (old_state & NBR_STA_2_WAY || nbr->state & NBR_STA_2_WAY) {
			/* neighbor changed from/to 2_WAY */

			gettimeofday(&now, NULL);
			nbr->uptime = now.tv_sec;

			if (nbr->state & NBR_STA_2_WAY)
				nbr->iface->adj_cnt++;
			else
				nbr->iface->adj_cnt--;
		}

		log_debug("nbr_fsm: event '%s' resulted in action '%s' and "
		    "changing state for neighbor ID %s from '%s' to '%s'",
		    nbr_event_name(event),
		    nbr_action_name(nbr_fsm_tbl[i].action),
		    inet_ntoa(nbr->id), nbr_state_name(old_state),
		    nbr_state_name(nbr->state));
	}

	return (ret);
}

void
nbr_init(u_int32_t hashsize)
{
	u_int32_t        hs, i;

	for (hs = 1; hs < hashsize; hs <<= 1)
		;
	nbrtable.hashtbl = calloc(hs, sizeof(struct nbr_head));
	if (nbrtable.hashtbl == NULL)
		fatal("nbr_init");

	for (i = 0; i < hs; i++)
		LIST_INIT(&nbrtable.hashtbl[i]);

	nbrtable.hashmask = hs - 1;
}

struct nbr *
nbr_new(u_int32_t nbr_id, struct iface *iface, int self)
{
	struct nbr_head	*head;
	struct nbr	*nbr = NULL;

	if ((nbr = calloc(1, sizeof(*nbr))) == NULL)
		fatal("nbr_new");

	nbr->state = NBR_STA_DOWN;
	nbr->id.s_addr = nbr_id;

	/* get next unused peerid */
	while (nbr_find_peerid(++peercnt))
		;
	nbr->peerid = peercnt;
	head = NBR_HASH(nbr->peerid);
	LIST_INSERT_HEAD(head, nbr, hash);

	/* add to peer list */
	nbr->iface = iface;
	LIST_INSERT_HEAD(&iface->nbr_list, nbr, entry);

	TAILQ_INIT(&nbr->rr_list);

	/* set event structures */
	evtimer_set(&nbr->inactivity_timer, nbr_itimer, nbr);

	log_debug("nbr_new: neighbor ID %s, peerid %lu",
	    inet_ntoa(nbr->id), nbr->peerid);

	return (nbr);
}

int
nbr_del(struct nbr *nbr)
{
	/* clear lists */
	rr_list_clr(&nbr->rr_list);

	LIST_REMOVE(nbr, entry);
	LIST_REMOVE(nbr, hash);

	free(nbr);

	return (0);
}

struct nbr *
nbr_find_peerid(u_int32_t peerid)
{
	struct nbr_head	*head;
	struct nbr	*nbr;

	head = NBR_HASH(peerid);

	LIST_FOREACH(nbr, head, hash) {
		if (nbr->peerid == peerid)
			return (nbr);
	}

	return (NULL);
}

struct nbr *
nbr_find_ip(struct iface *iface, u_int32_t src_ip)
{
	struct nbr	*nbr = NULL;

	LIST_FOREACH(nbr, &iface->nbr_list, entry) {
		if (nbr->id.s_addr == src_ip) {
			return (nbr);
		}
	}

	return (NULL);
}

/* timers */
void
nbr_itimer(int fd, short event, void *arg)
{
	struct nbr *nbr = arg;

	log_debug("nbr_itimer: %s", inet_ntoa(nbr->id));

	nbr_fsm(nbr, NBR_EVT_ITIMER);
}

int
nbr_start_itimer(struct nbr *nbr)
{
	struct timeval	tv;

	log_debug("nbr_start_itimer: %s", inet_ntoa(nbr->id));

	timerclear(&tv);
	tv.tv_sec = nbr->iface->dead_interval;

	return (evtimer_add(&nbr->inactivity_timer, &tv));
}

int
nbr_stop_itimer(struct nbr *nbr)
{
	return (evtimer_del(&nbr->inactivity_timer));
}

int
nbr_reset_itimer(struct nbr *nbr)
{
	struct timeval	tv;

	timerclear(&tv);
	tv.tv_sec = nbr->iface->dead_interval;

	return (evtimer_add(&nbr->inactivity_timer, &tv));
}

/* actions */
int
nbr_act_start(struct nbr *nbr)
{
	log_debug("nbr_act_start: neighbor ID %s", inet_ntoa(nbr->id));

	return (-1);
}

int
nbr_act_reset_itimer(struct nbr *nbr)
{
	if (nbr_reset_itimer(nbr)) {
		log_warnx("nbr_act_reset_itimer: cannot schedule inactivity "
		    "timer, neighbor ID %s", inet_ntoa(nbr->id));
		return (-1);
	}

	return (0);
}

int
nbr_act_start_itimer(struct nbr *nbr)
{
	if (nbr_start_itimer(nbr)) {
		log_warnx("nbr_act_start_itimer: cannot schedule inactivity "
		    "timer, neighbor ID %s",
		    inet_ntoa(nbr->id));
		return (-1);
	}

	if (nbr->state == NBR_STA_1_WAY) {
		/* new nbr, send entire route table, unicast */
		log_debug("nbr_act_start_itimer: nbr %s, send route table",
		    inet_ntoa(nbr->id));

		dvmrpe_imsg_compose_rde(IMSG_FULL_ROUTE_REPORT, nbr->peerid, 0,
		    NULL, 0);
	}

	return (0);
}

int
nbr_act_delete(struct nbr *nbr)
{
	struct nbr_msg	nm;

	log_debug("nbr_act_delete: neighbor ID %s", inet_ntoa(nbr->id));

	/* stop timers */
	if (nbr_stop_itimer(nbr)) {
		log_warnx("nbr_act_delete: error removing inactivity timer, "
		    "neighbor ID %s", inet_ntoa(nbr->id));
		return (-1);
	}

	nm.address.s_addr = nbr->addr.s_addr;
	nm.ifindex = nbr->iface->ifindex;

	dvmrpe_imsg_compose_rde(IMSG_NBR_DEL, 0, 0, &nm, sizeof(nm));

	return (nbr_del(nbr));
}

int
nbr_act_clear_lists(struct nbr *nbr)
{
	log_debug("nbr_act_clear_lists: neighbor ID %s", inet_ntoa(nbr->id));
	rr_list_clr(&nbr->rr_list);

	return (0);
}

/* names */
const char *
nbr_event_name(int event)
{
	return (nbr_event_names[event]);
}

const char *
nbr_action_name(int action)
{
	return (nbr_action_names[action]);
}

struct ctl_nbr *
nbr_to_ctl(struct nbr *nbr)
{
	static struct ctl_nbr	 nctl;
	struct timeval		 tv, now, res;

	memcpy(nctl.name, nbr->iface->name, sizeof(nctl.name));
	memcpy(&nctl.id, &nbr->id, sizeof(nctl.id));
	memcpy(&nctl.addr, &nbr->addr, sizeof(nctl.addr));

	nctl.state = nbr->state;

	gettimeofday(&now, NULL);
	if (evtimer_pending(&nbr->inactivity_timer, &tv)) {
		timersub(&tv, &now, &res);
		nctl.dead_timer = res.tv_sec;
	} else
		nctl.dead_timer = 0;

	if (nbr->state == NBR_STA_2_WAY) {
		nctl.uptime = now.tv_sec - nbr->uptime;
	} else
		nctl.uptime = 0;

	return (&nctl);
}
