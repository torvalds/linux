/*	$OpenBSD: neighbor.c,v 1.81 2019/06/28 13:32:48 deraadt Exp $ */

/*
 * Copyright (c) 2013, 2016 Renato Westphal <renato@openbsd.org>
 * Copyright (c) 2009 Michele Marchetto <michele@openbsd.org>
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2005, 2008 Esben Norby <norby@openbsd.org>
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
#include <sys/time.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ldpd.h"
#include "ldpe.h"
#include "lde.h"
#include "log.h"

static __inline int	 nbr_id_compare(struct nbr *, struct nbr *);
static __inline int	 nbr_addr_compare(struct nbr *, struct nbr *);
static __inline int	 nbr_pid_compare(struct nbr *, struct nbr *);
static void		 nbr_update_peerid(struct nbr *);
static void		 nbr_ktimer(int, short, void *);
static void		 nbr_start_ktimer(struct nbr *);
static void		 nbr_ktimeout(int, short, void *);
static void		 nbr_start_ktimeout(struct nbr *);
static void		 nbr_itimeout(int, short, void *);
static void		 nbr_start_itimeout(struct nbr *);
static void		 nbr_idtimer(int, short, void *);
static int		 nbr_act_session_operational(struct nbr *);
static void		 nbr_send_labelmappings(struct nbr *);

RB_GENERATE(nbr_id_head, nbr, id_tree, nbr_id_compare)
RB_GENERATE(nbr_addr_head, nbr, addr_tree, nbr_addr_compare)
RB_GENERATE(nbr_pid_head, nbr, pid_tree, nbr_pid_compare)

struct {
	int		state;
	enum nbr_event	event;
	enum nbr_action	action;
	int		new_state;
} nbr_fsm_tbl[] = {
    /* current state	event that happened	action to take		resulting state */
/* Passive Role */
    {NBR_STA_PRESENT,	NBR_EVT_MATCH_ADJ,	NBR_ACT_NOTHING,	NBR_STA_INITIAL},
    {NBR_STA_INITIAL,	NBR_EVT_INIT_RCVD,	NBR_ACT_PASSIVE_INIT,	NBR_STA_OPENREC},
    {NBR_STA_OPENREC,	NBR_EVT_KEEPALIVE_RCVD,	NBR_ACT_SESSION_EST,	NBR_STA_OPER},
/* Active Role */
    {NBR_STA_PRESENT,	NBR_EVT_CONNECT_UP,	NBR_ACT_CONNECT_SETUP,	NBR_STA_INITIAL},
    {NBR_STA_INITIAL,	NBR_EVT_INIT_SENT,	NBR_ACT_NOTHING,	NBR_STA_OPENSENT},
    {NBR_STA_OPENSENT,	NBR_EVT_INIT_RCVD,	NBR_ACT_KEEPALIVE_SEND,	NBR_STA_OPENREC},
/* Session Maintenance */
    {NBR_STA_OPER,	NBR_EVT_PDU_RCVD,	NBR_ACT_RST_KTIMEOUT,	0},
    {NBR_STA_SESSION,	NBR_EVT_PDU_RCVD,	NBR_ACT_NOTHING,	0},
    {NBR_STA_OPER,	NBR_EVT_PDU_SENT,	NBR_ACT_RST_KTIMER,	0},
    {NBR_STA_SESSION,	NBR_EVT_PDU_SENT,	NBR_ACT_NOTHING,	0},
/* Session Close */
    {NBR_STA_PRESENT,	NBR_EVT_CLOSE_SESSION,	NBR_ACT_NOTHING,	0},
    {NBR_STA_SESSION,	NBR_EVT_CLOSE_SESSION,	NBR_ACT_CLOSE_SESSION,	NBR_STA_PRESENT},
    {-1,		NBR_EVT_NOTHING,	NBR_ACT_NOTHING,	0},
};

const char * const nbr_event_names[] = {
	"NOTHING",
	"ADJACENCY MATCHED",
	"CONNECTION UP",
	"SESSION CLOSE",
	"INIT RECEIVED",
	"KEEPALIVE RECEIVED",
	"PDU RECEIVED",
	"PDU SENT",
	"INIT SENT"
};

const char * const nbr_action_names[] = {
	"NOTHING",
	"RESET KEEPALIVE TIMEOUT",
	"START NEIGHBOR SESSION",
	"RESET KEEPALIVE TIMER",
	"SETUP NEIGHBOR CONNECTION",
	"SEND INIT AND KEEPALIVE",
	"SEND KEEPALIVE",
	"CLOSE SESSION"
};

struct nbr_id_head nbrs_by_id = RB_INITIALIZER(&nbrs_by_id);
struct nbr_addr_head nbrs_by_addr = RB_INITIALIZER(&nbrs_by_addr);
struct nbr_pid_head nbrs_by_pid = RB_INITIALIZER(&nbrs_by_pid);

static __inline int
nbr_id_compare(struct nbr *a, struct nbr *b)
{
	return (ntohl(a->id.s_addr) - ntohl(b->id.s_addr));
}

static __inline int
nbr_addr_compare(struct nbr *a, struct nbr *b)
{
	if (a->af < b->af)
		return (-1);
	if (a->af > b->af)
		return (1);

	return (ldp_addrcmp(a->af, &a->raddr, &b->raddr));
}

static __inline int
nbr_pid_compare(struct nbr *a, struct nbr *b)
{
	return (a->peerid - b->peerid);
}

int
nbr_fsm(struct nbr *nbr, enum nbr_event event)
{
	struct timeval	now;
	int		old_state;
	int		new_state = 0;
	int		i;

	old_state = nbr->state;
	for (i = 0; nbr_fsm_tbl[i].state != -1; i++)
		if ((nbr_fsm_tbl[i].state & old_state) &&
		    (nbr_fsm_tbl[i].event == event)) {
			new_state = nbr_fsm_tbl[i].new_state;
			break;
		}

	if (nbr_fsm_tbl[i].state == -1) {
		/* event outside of the defined fsm, ignore it. */
		log_warnx("%s: lsr-id %s, event %s not expected in "
		    "state %s", __func__, inet_ntoa(nbr->id),
		    nbr_event_names[event], nbr_state_name(old_state));
		return (0);
	}

	if (new_state != 0)
		nbr->state = new_state;

	if (old_state != nbr->state) {
		log_debug("%s: event %s resulted in action %s and "
		    "changing state for lsr-id %s from %s to %s",
		    __func__, nbr_event_names[event],
		    nbr_action_names[nbr_fsm_tbl[i].action],
		    inet_ntoa(nbr->id), nbr_state_name(old_state),
		    nbr_state_name(nbr->state));

		if (nbr->state == NBR_STA_OPER) {
			gettimeofday(&now, NULL);
			nbr->uptime = now.tv_sec;
		}
	}

	if (nbr->state == NBR_STA_OPER || nbr->state == NBR_STA_PRESENT)
		nbr_stop_itimeout(nbr);
	else
		nbr_start_itimeout(nbr);

	switch (nbr_fsm_tbl[i].action) {
	case NBR_ACT_RST_KTIMEOUT:
		nbr_start_ktimeout(nbr);
		break;
	case NBR_ACT_RST_KTIMER:
		nbr_start_ktimer(nbr);
		break;
	case NBR_ACT_SESSION_EST:
		nbr_act_session_operational(nbr);
		nbr_start_ktimer(nbr);
		nbr_start_ktimeout(nbr);
		if (nbr->v4_enabled)
			send_address_all(nbr, AF_INET);
		if (nbr->v6_enabled)
			send_address_all(nbr, AF_INET6);
		nbr_send_labelmappings(nbr);
		break;
	case NBR_ACT_CONNECT_SETUP:
		nbr->tcp = tcp_new(nbr->fd, nbr);

		/* trigger next state */
		send_init(nbr);
		nbr_fsm(nbr, NBR_EVT_INIT_SENT);
		break;
	case NBR_ACT_PASSIVE_INIT:
		send_init(nbr);
		send_keepalive(nbr);
		break;
	case NBR_ACT_KEEPALIVE_SEND:
		nbr_start_ktimeout(nbr);
		send_keepalive(nbr);
		break;
	case NBR_ACT_CLOSE_SESSION:
		ldpe_imsg_compose_lde(IMSG_NEIGHBOR_DOWN, nbr->peerid, 0,
		    NULL, 0);
		session_close(nbr);
		break;
	case NBR_ACT_NOTHING:
		/* do nothing */
		break;
	}

	return (0);
}

struct nbr *
nbr_new(struct in_addr id, int af, int ds_tlv, union ldpd_addr *addr,
    uint32_t scope_id)
{
	struct nbr		*nbr;
	struct adj		*adj;
	struct pending_conn	*pconn;

	log_debug("%s: lsr-id %s transport-address %s", __func__,
	    inet_ntoa(id), log_addr(af, addr));

	if ((nbr = calloc(1, sizeof(*nbr))) == NULL)
		fatal(__func__);

	LIST_INIT(&nbr->adj_list);
	nbr->state = NBR_STA_PRESENT;
	nbr->peerid = 0;
	nbr->af = af;
	nbr->ds_tlv = ds_tlv;
	if (af == AF_INET || ds_tlv)
		nbr->v4_enabled = 1;
	if (af == AF_INET6 || ds_tlv)
		nbr->v6_enabled = 1;
	nbr->id = id;
	nbr->laddr = (ldp_af_conf_get(leconf, af))->trans_addr;
	nbr->raddr = *addr;
	nbr->raddr_scope = scope_id;
	nbr->conf_seqnum = 0;

	LIST_FOREACH(adj, &global.adj_list, global_entry) {
		if (adj->lsr_id.s_addr == nbr->id.s_addr) {
			adj->nbr = nbr;
			LIST_INSERT_HEAD(&nbr->adj_list, adj, nbr_entry);
		}
	}

	if (RB_INSERT(nbr_id_head, &nbrs_by_id, nbr) != NULL)
		fatalx("nbr_new: RB_INSERT(nbrs_by_id) failed");
	if (RB_INSERT(nbr_addr_head, &nbrs_by_addr, nbr) != NULL)
		fatalx("nbr_new: RB_INSERT(nbrs_by_addr) failed");

	TAILQ_INIT(&nbr->mapping_list);
	TAILQ_INIT(&nbr->withdraw_list);
	TAILQ_INIT(&nbr->request_list);
	TAILQ_INIT(&nbr->release_list);
	TAILQ_INIT(&nbr->abortreq_list);

	/* set event structures */
	evtimer_set(&nbr->keepalive_timeout, nbr_ktimeout, nbr);
	evtimer_set(&nbr->keepalive_timer, nbr_ktimer, nbr);
	evtimer_set(&nbr->init_timeout, nbr_itimeout, nbr);
	evtimer_set(&nbr->initdelay_timer, nbr_idtimer, nbr);

	if (pfkey_establish(leconf, nbr) == -1)
		fatalx("pfkey setup failed");

	pconn = pending_conn_find(nbr->af, &nbr->raddr);
	if (pconn) {
		session_accept_nbr(nbr, pconn->fd);
		pending_conn_del(pconn);
	}

	return (nbr);
}

void
nbr_del(struct nbr *nbr)
{
	log_debug("%s: lsr-id %s", __func__, inet_ntoa(nbr->id));

	nbr_fsm(nbr, NBR_EVT_CLOSE_SESSION);
	pfkey_remove(nbr);

	if (nbr_pending_connect(nbr))
		event_del(&nbr->ev_connect);
	nbr_stop_ktimer(nbr);
	nbr_stop_ktimeout(nbr);
	nbr_stop_itimeout(nbr);
	nbr_stop_idtimer(nbr);

	mapping_list_clr(&nbr->mapping_list);
	mapping_list_clr(&nbr->withdraw_list);
	mapping_list_clr(&nbr->request_list);
	mapping_list_clr(&nbr->release_list);
	mapping_list_clr(&nbr->abortreq_list);

	if (nbr->peerid)
		RB_REMOVE(nbr_pid_head, &nbrs_by_pid, nbr);
	RB_REMOVE(nbr_id_head, &nbrs_by_id, nbr);
	RB_REMOVE(nbr_addr_head, &nbrs_by_addr, nbr);

	free(nbr);
}

static void
nbr_update_peerid(struct nbr *nbr)
{
	static uint32_t	 peercnt = 1;

	if (nbr->peerid)
		RB_REMOVE(nbr_pid_head, &nbrs_by_pid, nbr);

	/* get next unused peerid */
	while (nbr_find_peerid(++peercnt))
		;
	nbr->peerid = peercnt;

	if (RB_INSERT(nbr_pid_head, &nbrs_by_pid, nbr) != NULL)
		fatalx("nbr_update_peerid: RB_INSERT(nbrs_by_pid) failed");
}

struct nbr *
nbr_find_ldpid(uint32_t lsr_id)
{
	struct nbr	n;
	n.id.s_addr = lsr_id;
	return (RB_FIND(nbr_id_head, &nbrs_by_id, &n));
}

struct nbr *
nbr_find_addr(int af, union ldpd_addr *addr)
{
	struct nbr	n;
	n.af = af;
	n.raddr = *addr;
	return (RB_FIND(nbr_addr_head, &nbrs_by_addr, &n));
}

struct nbr *
nbr_find_peerid(uint32_t peerid)
{
	struct nbr	n;
	n.peerid = peerid;
	return (RB_FIND(nbr_pid_head, &nbrs_by_pid, &n));
}

int
nbr_adj_count(struct nbr *nbr, int af)
{
	struct adj	*adj;
	int		 total = 0;

	LIST_FOREACH(adj, &nbr->adj_list, nbr_entry)
		if (adj_get_af(adj) == af)
			total++;

	return (total);
}

int
nbr_session_active_role(struct nbr *nbr)
{
	if (ldp_addrcmp(nbr->af, &nbr->laddr, &nbr->raddr) > 0)
		return (1);

	return (0);
}

/* timers */

/* Keepalive timer: timer to send keepalive message to neighbors */

static void
nbr_ktimer(int fd, short event, void *arg)
{
	struct nbr	*nbr = arg;

	send_keepalive(nbr);
	nbr_start_ktimer(nbr);
}

static void
nbr_start_ktimer(struct nbr *nbr)
{
	struct timeval	 tv;

	/* send three keepalives per period */
	timerclear(&tv);
	tv.tv_sec = (time_t)(nbr->keepalive / KEEPALIVE_PER_PERIOD);
	if (evtimer_add(&nbr->keepalive_timer, &tv) == -1)
		fatal(__func__);
}

void
nbr_stop_ktimer(struct nbr *nbr)
{
	if (evtimer_pending(&nbr->keepalive_timer, NULL) &&
	    evtimer_del(&nbr->keepalive_timer) == -1)
		fatal(__func__);
}

/* Keepalive timeout: if the nbr hasn't sent keepalive */

static void
nbr_ktimeout(int fd, short event, void *arg)
{
	struct nbr *nbr = arg;

	log_debug("%s: lsr-id %s", __func__, inet_ntoa(nbr->id));

	session_shutdown(nbr, S_KEEPALIVE_TMR, 0, 0);
}

static void
nbr_start_ktimeout(struct nbr *nbr)
{
	struct timeval	tv;

	timerclear(&tv);
	tv.tv_sec = nbr->keepalive;

	if (evtimer_add(&nbr->keepalive_timeout, &tv) == -1)
		fatal(__func__);
}

void
nbr_stop_ktimeout(struct nbr *nbr)
{
	if (evtimer_pending(&nbr->keepalive_timeout, NULL) &&
	    evtimer_del(&nbr->keepalive_timeout) == -1)
		fatal(__func__);
}

/* Session initialization timeout: if nbr got stuck in the initialization FSM */

static void
nbr_itimeout(int fd, short event, void *arg)
{
	struct nbr *nbr = arg;

	log_debug("%s: lsr-id %s", __func__, inet_ntoa(nbr->id));

	nbr_fsm(nbr, NBR_EVT_CLOSE_SESSION);
}

static void
nbr_start_itimeout(struct nbr *nbr)
{
	struct timeval	 tv;

	timerclear(&tv);
	tv.tv_sec = INIT_FSM_TIMEOUT;
	if (evtimer_add(&nbr->init_timeout, &tv) == -1)
		fatal(__func__);
}

void
nbr_stop_itimeout(struct nbr *nbr)
{
	if (evtimer_pending(&nbr->init_timeout, NULL) &&
	    evtimer_del(&nbr->init_timeout) == -1)
		fatal(__func__);
}

/* Init delay timer: timer to retry to iniziatize session */

static void
nbr_idtimer(int fd, short event, void *arg)
{
	struct nbr *nbr = arg;

	log_debug("%s: lsr-id %s", __func__, inet_ntoa(nbr->id));

	nbr_establish_connection(nbr);
}

void
nbr_start_idtimer(struct nbr *nbr)
{
	struct timeval	tv;

	timerclear(&tv);

	tv.tv_sec = INIT_DELAY_TMR;
	switch(nbr->idtimer_cnt) {
	default:
		/* do not further increase the counter */
		tv.tv_sec = MAX_DELAY_TMR;
		break;
	case 2:
		tv.tv_sec *= 2;
		/* FALLTHROUGH */
	case 1:
		tv.tv_sec *= 2;
		/* FALLTHROUGH */
	case 0:
		nbr->idtimer_cnt++;
		break;
	}

	if (evtimer_add(&nbr->initdelay_timer, &tv) == -1)
		fatal(__func__);
}

void
nbr_stop_idtimer(struct nbr *nbr)
{
	if (evtimer_pending(&nbr->initdelay_timer, NULL) &&
	    evtimer_del(&nbr->initdelay_timer) == -1)
		fatal(__func__);
}

int
nbr_pending_idtimer(struct nbr *nbr)
{
	if (evtimer_pending(&nbr->initdelay_timer, NULL))
		return (1);

	return (0);
}

int
nbr_pending_connect(struct nbr *nbr)
{
	if (event_initialized(&nbr->ev_connect) &&
	    event_pending(&nbr->ev_connect, EV_WRITE, NULL))
		return (1);

	return (0);
}

static void
nbr_connect_cb(int fd, short event, void *arg)
{
	struct nbr	*nbr = arg;
	int		 error;
	socklen_t	 len;

	len = sizeof(error);
	if (getsockopt(nbr->fd, SOL_SOCKET, SO_ERROR, &error, &len) == -1) {
		log_warn("%s: getsockopt SOL_SOCKET SO_ERROR", __func__);
		return;
	}

	if (error) {
		close(nbr->fd);
		errno = error;
		log_debug("%s: error while connecting to %s: %s", __func__,
		    log_addr(nbr->af, &nbr->raddr), strerror(errno));
		return;
	}

	nbr_fsm(nbr, NBR_EVT_CONNECT_UP);
}

int
nbr_establish_connection(struct nbr *nbr)
{
	struct sockaddr_storage	 local_sa;
	struct sockaddr_storage	 remote_sa;
	struct adj		*adj;
	struct nbr_params	*nbrp;
	int			 opt = 1;

	nbr->fd = socket(nbr->af,
	    SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (nbr->fd == -1) {
		log_warn("%s: error while creating socket", __func__);
		return (-1);
	}

	if (nbr->auth_established) {
		if (sysdep.no_pfkey || sysdep.no_md5sig) {
			log_warnx("md5sig configured but not available");
			close(nbr->fd);
			return (-1);
		}
		if (setsockopt(nbr->fd, IPPROTO_TCP, TCP_MD5SIG,
		    &opt, sizeof(opt)) == -1) {
			log_warn("setsockopt md5sig");
			close(nbr->fd);
			return (-1);
		}
	}

	memcpy(&local_sa, addr2sa(nbr->af, &nbr->laddr, 0), sizeof(local_sa));
	memcpy(&remote_sa, addr2sa(nbr->af, &nbr->raddr, LDP_PORT),
	    sizeof(local_sa));
	if (nbr->af == AF_INET6 && nbr->raddr_scope)
		addscope((struct sockaddr_in6 *)&remote_sa, nbr->raddr_scope);

	if (bind(nbr->fd, (struct sockaddr *)&local_sa,
	    local_sa.ss_len) == -1) {
		log_warn("%s: error while binding socket to %s", __func__,
		    log_sockaddr((struct sockaddr *)&local_sa));
		close(nbr->fd);
		return (-1);
	}

	nbrp = nbr_params_find(leconf, nbr->id);
	if (nbr_gtsm_check(nbr->fd, nbr, nbrp)) {
		close(nbr->fd);
		return (-1);
	}

	/*
	 * Send an extra hello to guarantee that the remote peer has formed
	 * an adjacency as well.
	 */
	LIST_FOREACH(adj, &nbr->adj_list, nbr_entry)
		send_hello(adj->source.type, adj->source.link.ia,
		    adj->source.target);

	if (connect(nbr->fd, (struct sockaddr *)&remote_sa,
	    remote_sa.ss_len) == -1) {
		if (errno == EINPROGRESS) {
			event_set(&nbr->ev_connect, nbr->fd, EV_WRITE,
			    nbr_connect_cb, nbr);
			event_add(&nbr->ev_connect, NULL);
			return (0);
		}
		log_warn("%s: error while connecting to %s", __func__,
		    log_sockaddr((struct sockaddr *)&remote_sa));
		close(nbr->fd);
		return (-1);
	}

	/* connection completed immediately */
	nbr_fsm(nbr, NBR_EVT_CONNECT_UP);

	return (0);
}

int
nbr_gtsm_enabled(struct nbr *nbr, struct nbr_params *nbrp)
{
	/*
	 * RFC 6720 - Section 3:
	 * "This document allows for the implementation to provide an option to
	 * statically (e.g., via configuration) and/or dynamically override the
	 * default behavior and enable/disable GTSM on a per-peer basis".
	 */
	if (nbrp && (nbrp->flags & F_NBRP_GTSM))
		return (nbrp->gtsm_enabled);

	if ((ldp_af_conf_get(leconf, nbr->af))->flags & F_LDPD_AF_NO_GTSM)
		return (0);

	/* By default, GTSM support has to be negotiated for LDPv4 */
	if (nbr->af == AF_INET && !(nbr->flags & F_NBR_GTSM_NEGOTIATED))
		return (0);

	return (1);
}

int
nbr_gtsm_setup(int fd, int af, struct nbr_params *nbrp)
{
	int	 ttl = 255;

	if (nbrp && (nbrp->flags & F_NBRP_GTSM_HOPS))
		ttl = 256 - nbrp->gtsm_hops;

	switch (af) {
	case AF_INET:
		if (sock_set_ipv4_minttl(fd, ttl) == -1)
			return (-1);
		ttl = 255;
		if (sock_set_ipv4_ucast_ttl(fd, ttl) == -1)
			return (-1);
		break;
	case AF_INET6:
		if (sock_set_ipv6_minhopcount(fd, ttl) == -1)
			return (-1);
		ttl = 255;
		if (sock_set_ipv6_ucast_hops(fd, ttl) == -1)
			return (-1);
		break;
	default:
		fatalx("nbr_gtsm_setup: unknown af");
	}

	return (0);
}

int
nbr_gtsm_check(int fd, struct nbr *nbr, struct nbr_params *nbrp)
{
	if (!nbr_gtsm_enabled(nbr, nbrp)) {
		switch (nbr->af) {
		case AF_INET:
			sock_set_ipv4_ucast_ttl(fd, -1);
			break;
		case AF_INET6:
			/*
			 * Send packets with a Hop Limit of 255 even when GSTM
			 * is disabled to guarantee interoperability.
			 */
			sock_set_ipv6_ucast_hops(fd, 255);
			break;
		default:
			fatalx("nbr_gtsm_check: unknown af");
			break;
		}
		return (0);
	}

	if (nbr_gtsm_setup(fd, nbr->af, nbrp) == -1) {
		log_warnx("%s: error enabling GTSM for lsr-id %s", __func__,
		    inet_ntoa(nbr->id));
		return (-1);
	}

	return (0);
}

static int
nbr_act_session_operational(struct nbr *nbr)
{
	struct lde_nbr	 lde_nbr;

	nbr->idtimer_cnt = 0;

	/* this is necessary to avoid ipc synchronization issues */
	nbr_update_peerid(nbr);

	memset(&lde_nbr, 0, sizeof(lde_nbr));
	lde_nbr.id = nbr->id;
	lde_nbr.v4_enabled = nbr->v4_enabled;
	lde_nbr.v6_enabled = nbr->v6_enabled;
	lde_nbr.flags = nbr->flags;
	return (ldpe_imsg_compose_lde(IMSG_NEIGHBOR_UP, nbr->peerid, 0,
	    &lde_nbr, sizeof(lde_nbr)));
}

static void
nbr_send_labelmappings(struct nbr *nbr)
{
	ldpe_imsg_compose_lde(IMSG_LABEL_MAPPING_FULL, nbr->peerid, 0,
	    NULL, 0);
}

struct nbr_params *
nbr_params_new(struct in_addr lsr_id)
{
	struct nbr_params	*nbrp;

	if ((nbrp = calloc(1, sizeof(*nbrp))) == NULL)
		fatal(__func__);

	nbrp->lsr_id = lsr_id;

	return (nbrp);
}

struct nbr_params *
nbr_params_find(struct ldpd_conf *xconf, struct in_addr lsr_id)
{
	struct nbr_params *nbrp;

	LIST_FOREACH(nbrp, &xconf->nbrp_list, entry)
		if (nbrp->lsr_id.s_addr == lsr_id.s_addr)
			return (nbrp);

	return (NULL);
}

uint16_t
nbr_get_keepalive(int af, struct in_addr lsr_id)
{
	struct nbr_params	*nbrp;

	nbrp = nbr_params_find(leconf, lsr_id);
	if (nbrp && (nbrp->flags & F_NBRP_KEEPALIVE))
		return (nbrp->keepalive);

	return ((ldp_af_conf_get(leconf, af))->keepalive);
}

struct ctl_nbr *
nbr_to_ctl(struct nbr *nbr)
{
	static struct ctl_nbr	 nctl;
	struct timeval		 now;

	nctl.af = nbr->af;
	nctl.id = nbr->id;
	nctl.laddr = nbr->laddr;
	nctl.raddr = nbr->raddr;
	nctl.nbr_state = nbr->state;

	gettimeofday(&now, NULL);
	if (nbr->state == NBR_STA_OPER) {
		nctl.uptime = now.tv_sec - nbr->uptime;
	} else
		nctl.uptime = 0;

	return (&nctl);
}

void
nbr_clear_ctl(struct ctl_nbr *nctl)
{
	struct nbr		*nbr;

	RB_FOREACH(nbr, nbr_addr_head, &nbrs_by_addr) {
		if (ldp_addrisset(nctl->af, &nctl->raddr) &&
		    ldp_addrcmp(nctl->af, &nctl->raddr, &nbr->raddr))
			continue;

		log_debug("%s: neighbor %s manually cleared", __func__,
		    log_addr(nbr->af, &nbr->raddr));
		session_shutdown(nbr, S_SHUTDOWN, 0, 0);
	}
}
