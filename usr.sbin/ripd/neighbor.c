/*	$OpenBSD: neighbor.c,v 1.12 2023/03/08 04:43:14 guenther Exp $ */

/*
 * Copyright (c) 2006 Michele Marchetto <mydecay@openbeer.it>
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

#include "ripd.h"
#include "rip.h"
#include "ripe.h"
#include "log.h"
#include "rde.h"

void	nbr_set_timer(struct nbr *);
void	nbr_stop_timer(struct nbr *);

void	nbr_failed_new(struct nbr *);
void	nbr_failed_timeout(int, short, void *);

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
    {NBR_STA_DOWN,	NBR_EVT_REQUEST_RCVD,	NBR_ACT_NOTHING,	NBR_STA_REQ_RCVD},
    {NBR_STA_DOWN,	NBR_EVT_RESPONSE_RCVD,	NBR_ACT_STRT_TIMER,	NBR_STA_ACTIVE},
    {NBR_STA_ACTIVE,	NBR_EVT_RESPONSE_RCVD,	NBR_ACT_RST_TIMER,	NBR_STA_ACTIVE},
    {NBR_STA_ACTIVE,	NBR_EVT_REQUEST_RCVD,	NBR_ACT_NOTHING,	NBR_STA_ACTIVE},
    {NBR_STA_ACTIVE,	NBR_EVT_TIMEOUT,	NBR_ACT_DEL,		NBR_STA_DOWN},
    {NBR_STA_REQ_RCVD,	NBR_EVT_RESPONSE_SENT,	NBR_ACT_DEL,		NBR_STA_DOWN},
    {NBR_STA_ACTIVE,	NBR_EVT_RESPONSE_SENT,	NBR_ACT_NOTHING,	NBR_STA_ACTIVE},
    {NBR_STA_ANY,	NBR_EVT_KILL_NBR,	NBR_ACT_DEL,		NBR_STA_DOWN},
    {-1,		NBR_EVT_NOTHING,	NBR_ACT_NOTHING,	0},
};

const char * const nbr_event_names[] = {
	"RESPONSE RCVD",
	"REQUEST RCVD",
	"RESPONSE SENT",
	"NBR TIMEOUT",
	"NBR KILL",
	"NOTHING"
};

const char * const nbr_action_names[] = {
	"START TIMER",
	"RESET TIMER",
	"DELETE NBR",
	"NOTHING"
};

int
nbr_fsm(struct nbr *nbr, enum nbr_event event)
{
	struct timeval	 now;
	int		 old_state;
	int		 new_state = 0;
	int		 i;

	old_state = nbr->state;
	for (i = 0; nbr_fsm_tbl[i].state != -1; i++)
		if ((nbr_fsm_tbl[i].state & old_state) &&
		    (nbr_fsm_tbl[i].event == event)) {
			new_state = nbr_fsm_tbl[i].new_state;
			break;
		}

	if (nbr_fsm_tbl[i].state == -1) {
		/* event outside of the defined fsm, ignore it. */
		log_warnx("nbr_fsm: neighbor ID %s, "
		    "event '%s' not expected in state '%s'",
		    inet_ntoa(nbr->id), nbr_event_name(event),
		    nbr_state_name(old_state));
		return (0);
	}

	switch (nbr_fsm_tbl[i].action) {
	case NBR_ACT_RST_TIMER:
		nbr_set_timer(nbr);
		break;
	case NBR_ACT_STRT_TIMER:
		nbr_set_timer(nbr);
		break;
	case NBR_ACT_DEL:
		nbr_act_del(nbr);
		break;
	case NBR_ACT_NOTHING:
		/* do nothing */
		break;
	}

	if (new_state != 0)
		nbr->state = new_state;

	if (old_state != nbr->state) {
		/* neighbor changed from/to ACTIVE */
		gettimeofday(&now, NULL);
		nbr->uptime = now.tv_sec;

		log_debug("nbr_fsm: event '%s' resulted in action '%s' and "
		    "changing state for neighbor ID %s from '%s' to '%s'",
		    nbr_event_name(event),
		    nbr_action_name(nbr_fsm_tbl[i].action),
		    inet_ntoa(nbr->id), nbr_state_name(old_state),
		    nbr_state_name(nbr->state));
	}

	return (0);
}

void
nbr_init(u_int32_t hashsize)
{
	u_int32_t	 hs, i;

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
nbr_new(u_int32_t nbr_id, struct iface *iface)
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

	TAILQ_INIT(&nbr->rp_list);
	TAILQ_INIT(&nbr->rq_list);

	/* set event structures */
	evtimer_set(&nbr->timeout_timer, nbr_timeout_timer, nbr);

	log_debug("nbr_new: neighbor ID %s, peerid %u",
	    inet_ntoa(nbr->id), nbr->peerid);

	return (nbr);
}

void
nbr_del(struct nbr *nbr)
{
	log_debug("nbr_del: neighbor ID %s, peerid %u", inet_ntoa(nbr->id),
	    nbr->peerid);

	/* stop timer */
	nbr_stop_timer(nbr);

	LIST_REMOVE(nbr, entry);
	LIST_REMOVE(nbr, hash);

	free(nbr);
}

void
nbr_act_del(struct nbr *nbr)
{
	/* If there is no authentication or it is just a route request
	 * there is no need to keep track of the failed neighbors */
	if (nbr->iface->auth_type == AUTH_CRYPT &&
	    nbr->state != NBR_STA_REQ_RCVD)
		nbr_failed_new(nbr);

	log_debug("nbr_act_del: neighbor ID %s, peerid %u", inet_ntoa(nbr->id),
	    nbr->peerid);

	/* schedule kill timer */
	nbr_set_timer(nbr);

	/* clear lists */
	clear_list(&nbr->rq_list);
	clear_list(&nbr->rp_list);
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

/* failed nbr handling */
void
nbr_failed_new(struct nbr *nbr)
{
	struct timeval		 tv;
	struct iface		*iface;
	struct nbr_failed	*nbr_failed;

	if ((nbr_failed = calloc(1, sizeof(*nbr_failed))) == NULL)
		fatal("nbr_failed_new");

	nbr_failed->addr = nbr->addr;
	nbr_failed->auth_seq_num = nbr->auth_seq_num;
	iface = nbr->iface;

	timerclear(&tv);
	tv.tv_sec = FAILED_NBR_TIMEOUT;

	evtimer_set(&nbr_failed->timeout_timer, nbr_failed_timeout,
	    nbr_failed);

	if (evtimer_add(&nbr_failed->timeout_timer, &tv) == -1)
		fatal("nbr_failed_new");

	LIST_INSERT_HEAD(&iface->failed_nbr_list, nbr_failed, entry);
}

struct nbr_failed *
nbr_failed_find(struct iface *iface, u_int32_t src_ip)
{
	struct nbr_failed	*nbr_failed = NULL;

	LIST_FOREACH(nbr_failed, &iface->failed_nbr_list, entry) {
		if (nbr_failed->addr.s_addr == src_ip) {
			return (nbr_failed);
		}
	}

	return (NULL);
}

void
nbr_failed_delete(struct nbr_failed *nbr_failed)
{
	if (evtimer_pending(&nbr_failed->timeout_timer, NULL))
		if (evtimer_del(&nbr_failed->timeout_timer) == -1)
			fatal("nbr_failed_delete");

	LIST_REMOVE(nbr_failed, entry);
	free(nbr_failed);
}

/* timers */
void
nbr_timeout_timer(int fd, short event, void *arg)
{
	struct nbr *nbr = arg;

	if (nbr->state == NBR_STA_DOWN)
		nbr_del(nbr);
	else
		nbr_fsm(nbr, NBR_EVT_TIMEOUT);
}

void
nbr_failed_timeout(int fd, short event, void *arg)
{
	struct nbr_failed	*nbr_failed = arg;

	log_debug("nbr_failed_timeout: failed neighbor ID %s deleted",
	    inet_ntoa(nbr_failed->addr));

	nbr_failed_delete(nbr_failed);
}

/* actions */
void
nbr_set_timer(struct nbr *nbr)
{
	struct timeval	tv;

	timerclear(&tv);
	tv.tv_sec = NBR_TIMEOUT;

	if (evtimer_add(&nbr->timeout_timer, &tv) == -1)
		fatal("nbr_set_timer");
}

void
nbr_stop_timer(struct nbr *nbr)
{
	if (evtimer_del(&nbr->timeout_timer) == -1)
		fatal("nbr_stop_timer");
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

	nctl.nbr_state = nbr->state;
	nctl.iface_state = nbr->iface->state;

	gettimeofday(&now, NULL);
	if (evtimer_pending(&nbr->timeout_timer, &tv)) {
		timersub(&tv, &now, &res);
		if (nbr->state & NBR_STA_DOWN)
			nctl.dead_timer = NBR_TIMEOUT - res.tv_sec;
		else
			nctl.dead_timer = res.tv_sec;
	} else
		nctl.dead_timer = 0;

	if (nbr->state == NBR_STA_ACTIVE) {
		nctl.uptime = now.tv_sec - nbr->uptime;
	} else
		nctl.uptime = 0;

	return (&nctl);
}
