/*	$OpenBSD: neighbor.c,v 1.19 2023/03/08 04:43:14 guenther Exp $ */

/*
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2005, 2007 Esben Norby <norby@openbsd.org>
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

#include "ospf6d.h"
#include "ospf6.h"
#include "ospfe.h"
#include "log.h"
#include "rde.h"

int	 nbr_adj_ok(struct nbr *);

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
	int		new_state; /* 0 means action decides or unchanged */
} nbr_fsm_tbl[] = {
    /* current state	event that happened	action to take		resulting state */
    {NBR_STA_ACTIVE,	NBR_EVT_HELLO_RCVD,	NBR_ACT_RST_ITIMER,	0},
    {NBR_STA_BIDIR,	NBR_EVT_2_WAY_RCVD,	NBR_ACT_NOTHING,	0},
    {NBR_STA_INIT,	NBR_EVT_1_WAY_RCVD,	NBR_ACT_NOTHING,	0},
    {NBR_STA_DOWN,	NBR_EVT_HELLO_RCVD,	NBR_ACT_STRT_ITIMER,	NBR_STA_INIT},
    {NBR_STA_ATTEMPT,	NBR_EVT_HELLO_RCVD,	NBR_ACT_RST_ITIMER,	NBR_STA_INIT},
    {NBR_STA_INIT,	NBR_EVT_2_WAY_RCVD,	NBR_ACT_EVAL,		0},
    {NBR_STA_XSTRT,	NBR_EVT_NEG_DONE,	NBR_ACT_SNAP,		0},
    {NBR_STA_SNAP,	NBR_EVT_SNAP_DONE,	NBR_ACT_SNAP_DONE,	NBR_STA_XCHNG},
    {NBR_STA_XCHNG,	NBR_EVT_XCHNG_DONE,	NBR_ACT_XCHNG_DONE,	0},
    {NBR_STA_LOAD,	NBR_EVT_LOAD_DONE,	NBR_ACT_NOTHING,	NBR_STA_FULL},
    {NBR_STA_2_WAY,	NBR_EVT_ADJ_OK,		NBR_ACT_EVAL,		0},
    {NBR_STA_ADJFORM,	NBR_EVT_ADJ_OK,		NBR_ACT_ADJ_OK,		0},
    {NBR_STA_PRELIM,	NBR_EVT_ADJ_OK,		NBR_ACT_HELLO_CHK,	0},
    {NBR_STA_ADJFORM,	NBR_EVT_ADJTMOUT,	NBR_ACT_RESTRT_DD,	0},
    {NBR_STA_FLOOD,	NBR_EVT_SEQ_NUM_MIS,	NBR_ACT_RESTRT_DD,	0},
    {NBR_STA_FLOOD,	NBR_EVT_BAD_LS_REQ,	NBR_ACT_RESTRT_DD,	0},
    {NBR_STA_ANY,	NBR_EVT_KILL_NBR,	NBR_ACT_DEL,		NBR_STA_DOWN},
    {NBR_STA_ANY,	NBR_EVT_LL_DOWN,	NBR_ACT_DEL,		NBR_STA_DOWN},
    {NBR_STA_ANY,	NBR_EVT_ITIMER,		NBR_ACT_DEL,		NBR_STA_DOWN},
    {NBR_STA_BIDIR,	NBR_EVT_1_WAY_RCVD,	NBR_ACT_CLR_LST,	NBR_STA_INIT},
    {-1,		NBR_EVT_NOTHING,	NBR_ACT_NOTHING,	0},
};

const char * const nbr_event_names[] = {
	"NOTHING",
	"HELLO_RECEIVED",
	"2_WAY_RECEIVED",
	"NEGOTIATION_DONE",
	"SNAPSHOT_DONE",
	"EXCHANGE_DONE",
	"BAD_LS_REQ",
	"LOADING_DONE",
	"ADJ_OK",
	"SEQ_NUM_MISMATCH",
	"1_WAY_RECEIVED",
	"KILL_NBR",
	"INACTIVITY_TIMER",
	"LL_DOWN",
	"ADJ_TIMEOUT"
};

const char * const nbr_action_names[] = {
	"NOTHING",
	"RESET_INACTIVITY_TIMER",
	"START_INACTIVITY_TIMER",
	"EVAL",
	"SNAPSHOT",
	"SNAPSHOT_DONE",
	"EXCHANGE_DONE",
	"ADJ_OK",
	"RESET_DD",
	"DELETE",
	"CLEAR_LISTS",
	"HELLO_CHK"
};

int
nbr_fsm(struct nbr *nbr, enum nbr_event event)
{
	struct timeval	now;
	int		old_state;
	int		new_state = 0;
	int		i, ret = 0;

	if (nbr == nbr->iface->self)
		return (0);

	old_state = nbr->state;
	for (i = 0; nbr_fsm_tbl[i].state != -1; i++)
		if ((nbr_fsm_tbl[i].state & old_state) &&
		    (nbr_fsm_tbl[i].event == event)) {
			new_state = nbr_fsm_tbl[i].new_state;
			break;
		}

	if (nbr_fsm_tbl[i].state == -1) {
		/* event outside of the defined fsm, ignore it. */
		log_warnx("nbr_fsm: neighbor ID %s (%s), "
		    "event %s not expected in state %s",
		    inet_ntoa(nbr->id), nbr->iface->name,
		    nbr_event_names[event],
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
	case NBR_ACT_EVAL:
		ret = nbr_act_eval(nbr);
		break;
	case NBR_ACT_SNAP:
		ret = nbr_act_snapshot(nbr);
		break;
	case NBR_ACT_SNAP_DONE:
		/* start db exchange */
		start_db_tx_timer(nbr);
		break;
	case NBR_ACT_XCHNG_DONE:
		ret = nbr_act_exchange_done(nbr);
		break;
	case NBR_ACT_ADJ_OK:
		ret = nbr_act_adj_ok(nbr);
		break;
	case NBR_ACT_RESTRT_DD:
		ret = nbr_act_restart_dd(nbr);
		break;
	case NBR_ACT_DEL:
		ret = nbr_act_delete(nbr);
		break;
	case NBR_ACT_CLR_LST:
		ret = nbr_act_clear_lists(nbr);
		break;
	case NBR_ACT_HELLO_CHK:
		ret = nbr_act_hello_check(nbr);
		break;
	case NBR_ACT_NOTHING:
		/* do nothing */
		break;
	}

	if (ret) {
		log_warnx("nbr_fsm: error changing state for neighbor "
		    "ID %s (%s), event %s, state %s",
		    inet_ntoa(nbr->id), nbr->iface->name,
		    nbr_event_names[event], nbr_state_name(old_state));
		return (-1);
	}

	if (new_state != 0)
		nbr->state = new_state;

	if (old_state != nbr->state) {
		nbr->stats.sta_chng++;

		if (old_state & NBR_STA_FULL || nbr->state & NBR_STA_FULL) {
			/*
			 * neighbor changed from/to FULL
			 * originate new rtr and net LSA
			 */
			orig_rtr_lsa(nbr->iface->area);
			if (nbr->iface->state & IF_STA_DR)
				orig_net_lsa(nbr->iface);

			gettimeofday(&now, NULL);
			nbr->uptime = now.tv_sec;
		}

		/* state change inform RDE */
		ospfe_imsg_compose_rde(IMSG_NEIGHBOR_CHANGE,
		    nbr->peerid, 0, &nbr->state, sizeof(nbr->state));

		/* bidirectional communication lost */
		if (old_state & ~NBR_STA_PRELIM && nbr->state & NBR_STA_PRELIM)
			if_fsm(nbr->iface, IF_EVT_NBR_CHNG);

		log_debug("nbr_fsm: event %s resulted in action %s and "
		    "changing state for neighbor ID %s (%s) from %s to %s",
		    nbr_event_names[event],
		    nbr_action_names[nbr_fsm_tbl[i].action],
		    inet_ntoa(nbr->id), nbr->iface->name,
		    nbr_state_name(old_state),
		    nbr_state_name(nbr->state));

		if (nbr->iface->type == IF_TYPE_VIRTUALLINK) {
			orig_rtr_lsa(nbr->iface->area);
		}
	}

	return (ret);
}

void
nbr_init(u_int32_t hashsize)
{
	struct nbr_head	*head;
	struct nbr	*nbr;
	u_int32_t        hs, i;

	for (hs = 1; hs < hashsize; hs <<= 1)
		;
	nbrtable.hashtbl = calloc(hs, sizeof(struct nbr_head));
	if (nbrtable.hashtbl == NULL)
		fatal("nbr_init");

	for (i = 0; i < hs; i++)
		LIST_INIT(&nbrtable.hashtbl[i]);

	nbrtable.hashmask = hs - 1;

	/* allocate a dummy neighbor used for self originated AS ext routes */
	if ((nbr = calloc(1, sizeof(*nbr))) == NULL)
		fatal("nbr_init");

	nbr->id.s_addr = ospfe_router_id();
	nbr->state = NBR_STA_DOWN;
	nbr->peerid = NBR_IDSELF;
	head = NBR_HASH(nbr->peerid);
	LIST_INSERT_HEAD(head, nbr, hash);

	TAILQ_INIT(&nbr->ls_retrans_list);
	TAILQ_INIT(&nbr->db_sum_list);
	TAILQ_INIT(&nbr->ls_req_list);
}

struct nbr *
nbr_new(u_int32_t nbr_id, struct iface *iface, u_int32_t iface_id, int self,
	struct in6_addr *addr)
{
	struct nbr_head	*head;
	struct nbr	*nbr;
	struct rde_nbr	 rn;

	if ((nbr = calloc(1, sizeof(*nbr))) == NULL)
		fatal("nbr_new");

	nbr->state = NBR_STA_DOWN;
	nbr->dd_master = 1;
	nbr->dd_seq_num = arc4random();	/* RFC: some unique value */
	nbr->id.s_addr = nbr_id;

	/* get next unused peerid */
	while (nbr_find_peerid(++peercnt))
		;
	nbr->peerid = peercnt;
	head = NBR_HASH(nbr->peerid);
	LIST_INSERT_HEAD(head, nbr, hash);

	/* add to peer list */
	nbr->iface = iface;
	nbr->iface_id = iface_id;
	LIST_INSERT_HEAD(&iface->nbr_list, nbr, entry);

	TAILQ_INIT(&nbr->ls_retrans_list);
	TAILQ_INIT(&nbr->db_sum_list);
	TAILQ_INIT(&nbr->ls_req_list);

	nbr->ls_req = NULL;

	if (self) {
		nbr->state = NBR_STA_FULL;
		nbr->addr = iface->addr;
		nbr->priority = iface->priority;
	}

	/* set event structures */
	evtimer_set(&nbr->inactivity_timer, nbr_itimer, nbr);
	evtimer_set(&nbr->db_tx_timer, db_tx_timer, nbr);
	evtimer_set(&nbr->lsreq_tx_timer, ls_req_tx_timer, nbr);
	evtimer_set(&nbr->ls_retrans_timer, ls_retrans_timer, nbr);
	evtimer_set(&nbr->adj_timer, nbr_adj_timer, nbr);

	bzero(&rn, sizeof(rn));
	if (addr)
		rn.addr = *addr;
	rn.id.s_addr = nbr->id.s_addr;
	rn.area_id.s_addr = nbr->iface->area->id.s_addr;
	rn.ifindex = nbr->iface->ifindex;
	rn.iface_id = nbr->iface_id;
	rn.state = nbr->state;
	rn.self = self;
	ospfe_imsg_compose_rde(IMSG_NEIGHBOR_UP, nbr->peerid, 0, &rn,
	    sizeof(rn));

	return (nbr);
}

void
nbr_del(struct nbr *nbr)
{
	ospfe_imsg_compose_rde(IMSG_NEIGHBOR_DOWN, nbr->peerid, 0, NULL, 0);

	if (evtimer_pending(&nbr->inactivity_timer, NULL))
		evtimer_del(&nbr->inactivity_timer);
	if (evtimer_pending(&nbr->db_tx_timer, NULL))
		evtimer_del(&nbr->db_tx_timer);
	if (evtimer_pending(&nbr->lsreq_tx_timer, NULL))
		evtimer_del(&nbr->lsreq_tx_timer);
	if (evtimer_pending(&nbr->ls_retrans_timer, NULL))
		evtimer_del(&nbr->ls_retrans_timer);
	if (evtimer_pending(&nbr->adj_timer, NULL))
		evtimer_del(&nbr->adj_timer);

	/* clear lists */
	ls_retrans_list_clr(nbr);
	db_sum_list_clr(nbr);
	ls_req_list_clr(nbr);

	LIST_REMOVE(nbr, entry);
	LIST_REMOVE(nbr, hash);

	free(nbr);
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
nbr_find_id(struct iface *iface, u_int32_t rtr_id)
{
	struct nbr	*nbr = NULL;

	LIST_FOREACH(nbr, &iface->nbr_list, entry) {
		if (nbr->id.s_addr == rtr_id)
			return (nbr);
	}

	return (NULL);
}

/* timers */
void
nbr_itimer(int fd, short event, void *arg)
{
	struct nbr *nbr = arg;

	if (nbr->state == NBR_STA_DOWN)
		nbr_del(nbr);
	else
		nbr_fsm(nbr, NBR_EVT_ITIMER);
}

void
nbr_start_itimer(struct nbr *nbr)
{
	struct timeval	tv;

	timerclear(&tv);
	tv.tv_sec = nbr->iface->dead_interval;

	if (evtimer_add(&nbr->inactivity_timer, &tv) == -1)
		fatal("nbr_start_itimer");
}

void
nbr_stop_itimer(struct nbr *nbr)
{
	if (evtimer_del(&nbr->inactivity_timer) == -1)
		fatal("nbr_stop_itimer");
}

void
nbr_reset_itimer(struct nbr *nbr)
{
	struct timeval	tv;

	timerclear(&tv);
	tv.tv_sec = nbr->iface->dead_interval;

	if (evtimer_add(&nbr->inactivity_timer, &tv) == -1)
		fatal("nbr_reset_itimer");
}

void
nbr_adj_timer(int fd, short event, void *arg)
{
	struct nbr *nbr = arg;

	if (!(nbr->state & NBR_STA_ADJFORM))
		return;

	if (nbr->state & NBR_STA_ACTIVE && nbr->state != NBR_STA_FULL) {
		log_warnx("nbr_adj_timer: failed to form adjacency with "
		    "neighbor ID %s on interface %s",
		    inet_ntoa(nbr->id), nbr->iface->name);
		nbr_fsm(nbr, NBR_EVT_ADJTMOUT);
	}
}

void
nbr_start_adj_timer(struct nbr *nbr)
{
	struct timeval	tv;

	timerclear(&tv);
	tv.tv_sec = DEFAULT_ADJ_TMOUT;

	if (evtimer_add(&nbr->adj_timer, &tv) == -1)
		fatal("nbr_start_adj_timer");
}

/* actions */
int
nbr_act_reset_itimer(struct nbr *nbr)
{
	nbr_reset_itimer(nbr);

	return (0);
}

int
nbr_act_start_itimer(struct nbr *nbr)
{
	nbr_start_itimer(nbr);

	return (0);
}

int
nbr_adj_ok(struct nbr *nbr)
{
	struct iface	*iface = nbr->iface;

	switch (iface->type) {
	case IF_TYPE_POINTOPOINT:
	case IF_TYPE_VIRTUALLINK:
	case IF_TYPE_POINTOMULTIPOINT:
		/* always ok */
		break;
	case IF_TYPE_BROADCAST:
	case IF_TYPE_NBMA:
		/*
		 * if neighbor is dr, bdr or router self is dr or bdr
		 * start forming adjacency
		 */
		if (iface->dr == nbr || iface->bdr == nbr ||
		    iface->state & IF_STA_DRORBDR)
			break;
		return (0);
	default:
		fatalx("nbr_adj_ok: unknown interface type");
	}
	return (1);
}

int
nbr_act_eval(struct nbr *nbr)
{
	if (!nbr_adj_ok(nbr)) {
		nbr->state = NBR_STA_2_WAY;
		return (0);
	}

	nbr->state = NBR_STA_XSTRT;
	nbr->dd_master = 1;
	nbr->dd_seq_num++;	/* as per RFC */
	nbr->dd_pending = 0;
	/* initial db negotiation */
	start_db_tx_timer(nbr);

	nbr_start_adj_timer(nbr);

	return (0);
}

int
nbr_act_snapshot(struct nbr *nbr)
{
	stop_db_tx_timer(nbr);

	/* we need to wait for the old snapshot to finish */
	if (nbr->dd_snapshot) {
		log_debug("nbr_act_snapshot: giving up, old snapshot running "
		    "for neighbor ID %s (%s)", inet_ntoa(nbr->id),
		    nbr->iface->name);
		return (nbr_act_restart_dd(nbr));
	}
	ospfe_imsg_compose_rde(IMSG_DB_SNAPSHOT, nbr->peerid, 0, NULL, 0);

	nbr->dd_snapshot = 1;	/* wait for IMSG_DB_END */
	nbr->state = NBR_STA_SNAP;

	return (0);
}

int
nbr_act_exchange_done(struct nbr *nbr)
{
	if (nbr->dd_master)
		stop_db_tx_timer(nbr);

	if (ls_req_list_empty(nbr) && nbr->state == NBR_STA_XCHNG &&
	    nbr->dd_pending == 0) {
		nbr->state = NBR_STA_FULL;
		return (0);
	}

	nbr->state = NBR_STA_LOAD;

	if (!ls_req_list_empty(nbr))
		start_ls_req_tx_timer(nbr);

	return (0);
}

int
nbr_act_adj_ok(struct nbr *nbr)
{
	if (nbr_adj_ok(nbr)) {
		if (nbr->state == NBR_STA_2_WAY)
			return (nbr_act_eval(nbr));
	} else {
		nbr->state = NBR_STA_2_WAY;
		return (nbr_act_clear_lists(nbr));
	}

	return (0);
}

int
nbr_act_restart_dd(struct nbr *nbr)
{
	nbr_act_clear_lists(nbr);

	if (!nbr_adj_ok(nbr)) {
		nbr->state = NBR_STA_2_WAY;
		return (0);
	}

	nbr->state = NBR_STA_XSTRT;
	nbr->dd_master = 1;
	nbr->dd_seq_num += arc4random() & 0xffff;
	nbr->dd_pending = 0;

	/* initial db negotiation */
	start_db_tx_timer(nbr);

	nbr_start_adj_timer(nbr);

	return (0);
}

int
nbr_act_delete(struct nbr *nbr)
{
	struct timeval	tv;

	/* clear dr and bdr */
	nbr->dr.s_addr = 0;
	nbr->bdr.s_addr = 0;

	if (nbr == nbr->iface->self)
		return (0);

	/* stop timers */
	nbr_stop_itimer(nbr);

	/* schedule kill timer */
	timerclear(&tv);
	tv.tv_sec = DEFAULT_NBR_TMOUT;

	if (evtimer_add(&nbr->inactivity_timer, &tv)) {
		log_warnx("nbr_act_delete: error scheduling "
		    "neighbor ID %s (%s) for removal",
		    inet_ntoa(nbr->id), nbr->iface->name);
	}

	return (nbr_act_clear_lists(nbr));
}

int
nbr_act_clear_lists(struct nbr *nbr)
{
	/* stop timers */
	stop_db_tx_timer(nbr);
	stop_ls_req_tx_timer(nbr);

	/* clear lists */
	ls_retrans_list_clr(nbr);
	db_sum_list_clr(nbr);
	ls_req_list_clr(nbr);

	return (0);
}

int
nbr_act_hello_check(struct nbr *nbr)
{
	log_debug("nbr_act_hello_check: neighbor ID %s (%s)",
	    inet_ntoa(nbr->id), nbr->iface->name);

	return (-1);
}

struct ctl_nbr *
nbr_to_ctl(struct nbr *nbr)
{
	static struct ctl_nbr	 nctl;
	struct timeval		 tv, now, res;
	struct lsa_entry	*le;

	memcpy(nctl.name, nbr->iface->name, sizeof(nctl.name));
	memcpy(&nctl.id, &nbr->id, sizeof(nctl.id));
	memcpy(&nctl.addr, &nbr->addr, sizeof(nctl.addr));
	memcpy(&nctl.dr, &nbr->dr, sizeof(nctl.dr));
	memcpy(&nctl.bdr, &nbr->bdr, sizeof(nctl.bdr));
	memcpy(&nctl.area, &nbr->iface->area->id, sizeof(nctl.area));

	/* this list is 99% of the time empty so that's OK for now */
	nctl.db_sum_lst_cnt = 0;
	TAILQ_FOREACH(le, &nbr->db_sum_list, entry)
		nctl.db_sum_lst_cnt++;

	nctl.ls_req_lst_cnt = nbr->ls_req_cnt;
	nctl.ls_retrans_lst_cnt = nbr->ls_ret_cnt;

	nctl.nbr_state = nbr->state;

	/*
	 * We need to trick a bit to show the remote iface state.
	 * The idea is to print DR, BDR or DROther dependent on
	 * the type of the neighbor.
	 */
	if (nbr->iface->dr == nbr)
		nctl.iface_state = IF_STA_DR;
	else if (nbr->iface->bdr == nbr)
		nctl.iface_state = IF_STA_BACKUP;
	else if (nbr->iface->state & IF_STA_MULTI)
		nctl.iface_state = IF_STA_DROTHER;
	else
		nctl.iface_state = nbr->iface->state;

	nctl.state_chng_cnt = nbr->stats.sta_chng;

	nctl.priority = nbr->priority;
	nctl.options = nbr->options;

	gettimeofday(&now, NULL);
	if (evtimer_pending(&nbr->inactivity_timer, &tv)) {
		timersub(&tv, &now, &res);
		if (nbr->state & NBR_STA_DOWN)
			nctl.dead_timer = DEFAULT_NBR_TMOUT - res.tv_sec;
		else
			nctl.dead_timer = res.tv_sec;
	} else
		nctl.dead_timer = 0;

	if (nbr->state == NBR_STA_FULL) {
		nctl.uptime = now.tv_sec - nbr->uptime;
	} else
		nctl.uptime = 0;

	return (&nctl);
}

struct lsa_hdr *
lsa_hdr_new(void)
{
	struct lsa_hdr	*lsa_hdr = NULL;

	if ((lsa_hdr = calloc(1, sizeof(*lsa_hdr))) == NULL)
		fatal("lsa_hdr_new");

	return (lsa_hdr);
}
