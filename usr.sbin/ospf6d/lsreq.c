/*	$OpenBSD: lsreq.c,v 1.15 2023/07/03 09:51:38 claudio Exp $ */

/*
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <arpa/inet.h>
#include <stdlib.h>

#include "ospf6d.h"
#include "ospf6.h"
#include "log.h"
#include "ospfe.h"

/* link state request packet handling */
int
send_ls_req(struct nbr *nbr)
{
	struct in6_addr		 dst;
	struct ls_req_hdr	 ls_req_hdr;
	struct lsa_entry	*le, *nle;
	struct ibuf		*buf;

	if ((buf = ibuf_open(nbr->iface->mtu - sizeof(struct ip6_hdr))) == NULL)
		fatal("send_ls_req");

	switch (nbr->iface->type) {
	case IF_TYPE_POINTOPOINT:
		inet_pton(AF_INET6, AllSPFRouters, &dst);
		break;
	case IF_TYPE_BROADCAST:
	case IF_TYPE_NBMA:
	case IF_TYPE_POINTOMULTIPOINT:
	case IF_TYPE_VIRTUALLINK:
		dst = nbr->addr;
		break;
	default:
		fatalx("send_ls_req: unknown interface type");
	}

	/* OSPF header */
	if (gen_ospf_hdr(buf, nbr->iface, PACKET_TYPE_LS_REQUEST))
		goto fail;

	/* LSA header(s) */
	for (le = TAILQ_FIRST(&nbr->ls_req_list);
	    le != NULL && sizeof(ls_req_hdr) < ibuf_left(buf);
	    le = nle) {
		nbr->ls_req = nle = TAILQ_NEXT(le, entry);
		ls_req_hdr.zero = 0;
		ls_req_hdr.type = le->le_lsa->type;
		ls_req_hdr.ls_id = le->le_lsa->ls_id;
		ls_req_hdr.adv_rtr = le->le_lsa->adv_rtr;
		if (ibuf_add(buf, &ls_req_hdr, sizeof(ls_req_hdr)))
			goto fail;
	}

	/* calculate checksum */
	if (upd_ospf_hdr(buf, nbr->iface))
		goto fail;

	if (send_packet(nbr->iface, buf, &dst) == -1)
		goto fail;

	ibuf_free(buf);
	return (0);
fail:
	log_warn("send_ls_req");
	ibuf_free(buf);
	return (-1);
}

void
recv_ls_req(struct nbr *nbr, char *buf, u_int16_t len)
{
	switch (nbr->state) {
	case NBR_STA_DOWN:
	case NBR_STA_ATTEMPT:
	case NBR_STA_INIT:
	case NBR_STA_2_WAY:
	case NBR_STA_XSTRT:
	case NBR_STA_SNAP:
		log_debug("recv_ls_req: packet ignored in state %s, "
		    "neighbor ID %s (%s)", nbr_state_name(nbr->state),
		    inet_ntoa(nbr->id), nbr->iface->name);
		break;
	case NBR_STA_XCHNG:
	case NBR_STA_LOAD:
	case NBR_STA_FULL:
		ospfe_imsg_compose_rde(IMSG_LS_REQ, nbr->peerid, 0, buf, len);
		break;
	default:
		fatalx("recv_ls_req: unknown neighbor state");
	}
}

/* link state request list */
void
ls_req_list_add(struct nbr *nbr, struct lsa_hdr *lsa)
{
	struct lsa_entry	*le;

	if (lsa == NULL)
		fatalx("ls_req_list_add: no LSA header");

	if ((le = calloc(1, sizeof(*le))) == NULL)
		fatal("ls_req_list_add");

	TAILQ_INSERT_TAIL(&nbr->ls_req_list, le, entry);
	le->le_lsa = lsa;
	nbr->ls_req_cnt++;
}

struct lsa_entry *
ls_req_list_get(struct nbr *nbr, struct lsa_hdr *lsa_hdr)
{
	struct lsa_entry	*le;

	TAILQ_FOREACH(le, &nbr->ls_req_list, entry) {
		if ((lsa_hdr->type == le->le_lsa->type) &&
		    (lsa_hdr->ls_id == le->le_lsa->ls_id) &&
		    (lsa_hdr->adv_rtr == le->le_lsa->adv_rtr))
			return (le);
	}
	return (NULL);
}

void
ls_req_list_free(struct nbr *nbr, struct lsa_entry *le)
{
	if (nbr->ls_req == le) {
		nbr->ls_req = TAILQ_NEXT(le, entry);
	}

	TAILQ_REMOVE(&nbr->ls_req_list, le, entry);
	free(le->le_lsa);
	free(le);
	nbr->ls_req_cnt--;

	/* received all requested LSA(s), send a new LS req */
	if (nbr->ls_req != NULL &&
	    nbr->ls_req == TAILQ_FIRST(&nbr->ls_req_list)) {
		start_ls_req_tx_timer(nbr);
	}

	/* we might not have received all DDs and are still in XCHNG */
	if (ls_req_list_empty(nbr) && nbr->dd_pending == 0 &&
	    nbr->state != NBR_STA_XCHNG)
		nbr_fsm(nbr, NBR_EVT_LOAD_DONE);
}

void
ls_req_list_clr(struct nbr *nbr)
{
	struct lsa_entry	*le;

	while ((le = TAILQ_FIRST(&nbr->ls_req_list)) != NULL) {
		TAILQ_REMOVE(&nbr->ls_req_list, le, entry);
		free(le->le_lsa);
		free(le);
	}

	nbr->ls_req_cnt = 0;
	nbr->ls_req = NULL;
}

int
ls_req_list_empty(struct nbr *nbr)
{
	return (TAILQ_EMPTY(&nbr->ls_req_list));
}

/* timers */
void
ls_req_tx_timer(int fd, short event, void *arg)
{
	struct nbr	*nbr = arg;
	struct timeval	 tv;

	switch (nbr->state) {
	case NBR_STA_DOWN:
	case NBR_STA_ATTEMPT:
	case NBR_STA_INIT:
	case NBR_STA_2_WAY:
	case NBR_STA_SNAP:
	case NBR_STA_XSTRT:
	case NBR_STA_XCHNG:
		return;
	case NBR_STA_LOAD:
		send_ls_req(nbr);
		break;
	case NBR_STA_FULL:
		return;
	default:
		log_debug("ls_req_tx_timer: unknown neighbor state, "
		    "neighbor ID %s (%s)", inet_ntoa(nbr->id),
		    nbr->iface->name);
		break;
	}

	/* reschedule lsreq_tx_timer */
	if (nbr->state == NBR_STA_LOAD) {
		timerclear(&tv);
		tv.tv_sec = nbr->iface->rxmt_interval;
		if (evtimer_add(&nbr->lsreq_tx_timer, &tv) == -1)
			fatal("ls_req_tx_timer");
	}
}

void
start_ls_req_tx_timer(struct nbr *nbr)
{
	struct timeval tv;

	if (nbr == nbr->iface->self)
		return;

	timerclear(&tv);
	if (evtimer_add(&nbr->lsreq_tx_timer, &tv) == -1)
		fatal("start_ls_req_tx_timer");
}

void
stop_ls_req_tx_timer(struct nbr *nbr)
{
	if (nbr == nbr->iface->self)
		return;

	if (evtimer_del(&nbr->lsreq_tx_timer) == -1)
		fatal("stop_ls_req_tx_timer");
}
