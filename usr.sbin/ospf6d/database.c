/*	$OpenBSD: database.c,v 1.23 2023/06/21 07:45:47 claudio Exp $ */

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
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ospf6d.h"
#include "ospf6.h"
#include "log.h"
#include "ospfe.h"

void	db_sum_list_next(struct nbr *);

/* database description packet handling */
int
send_db_description(struct nbr *nbr)
{
	struct in6_addr		 dst;
	struct db_dscrp_hdr	 dd_hdr;
	struct lsa_entry	*le, *nle;
	struct ibuf		*buf;
	u_int8_t		 bits = 0;

	if ((buf = ibuf_open(nbr->iface->mtu - sizeof(struct ip6_hdr))) == NULL)
		fatal("send_db_description");

	/* OSPF header */
	if (gen_ospf_hdr(buf, nbr->iface, PACKET_TYPE_DD))
		goto fail;

	/* reserve space for database description header */
	if (ibuf_add_zero(buf, sizeof(dd_hdr)) == -1)
		goto fail;

	switch (nbr->state) {
	case NBR_STA_DOWN:
	case NBR_STA_ATTEMPT:
	case NBR_STA_INIT:
	case NBR_STA_2_WAY:
	case NBR_STA_SNAP:
		log_debug("send_db_description: neighbor ID %s (%s): "
		    "cannot send packet in state %s", inet_ntoa(nbr->id),
		    nbr->iface->name, nbr_state_name(nbr->state));
		goto fail;
	case NBR_STA_XSTRT:
		bits |= OSPF_DBD_MS | OSPF_DBD_M | OSPF_DBD_I;
		nbr->dd_more = 1;
		break;
	case NBR_STA_XCHNG:
		if (nbr->dd_master)
			bits |= OSPF_DBD_MS;
		else
			bits &= ~OSPF_DBD_MS;

		if (TAILQ_EMPTY(&nbr->db_sum_list)) {
			bits &= ~OSPF_DBD_M;
			nbr->dd_more = 0;
		} else {
			bits |= OSPF_DBD_M;
			nbr->dd_more = 1;
		}

		bits &= ~OSPF_DBD_I;

		/* build LSA list */
		for (le = TAILQ_FIRST(&nbr->db_sum_list); le != NULL &&
		    ibuf_left(buf) >=  sizeof(struct lsa_hdr); le = nle) {
			nbr->dd_end = nle = TAILQ_NEXT(le, entry);
			if (ibuf_add(buf, le->le_lsa, sizeof(struct lsa_hdr)))
				goto fail;
		}
		break;
	case NBR_STA_LOAD:
	case NBR_STA_FULL:
		if (nbr->dd_master)
			bits |= OSPF_DBD_MS;
		else
			bits &= ~OSPF_DBD_MS;
		bits &= ~OSPF_DBD_M;
		bits &= ~OSPF_DBD_I;

		nbr->dd_more = 0;
		break;
	default:
		fatalx("send_db_description: unknown neighbor state");
	}

	bzero(&dd_hdr, sizeof(dd_hdr));

	switch (nbr->iface->type) {
	case IF_TYPE_POINTOPOINT:
		inet_pton(AF_INET6, AllSPFRouters, &dst);
		dd_hdr.iface_mtu = htons(nbr->iface->mtu);
		break;
	case IF_TYPE_BROADCAST:
		dst = nbr->addr;
		dd_hdr.iface_mtu = htons(nbr->iface->mtu);
		break;
	case IF_TYPE_NBMA:
	case IF_TYPE_POINTOMULTIPOINT:
		/* XXX not supported */
		break;
	case IF_TYPE_VIRTUALLINK:
		dst = nbr->iface->dst;
		dd_hdr.iface_mtu = 0;
		break;
	default:
		fatalx("send_db_description: unknown interface type");
	}

	dd_hdr.opts = htonl(area_ospf_options(nbr->iface->area));
	dd_hdr.bits = bits;
	dd_hdr.dd_seq_num = htonl(nbr->dd_seq_num);

	if (ibuf_set(buf, sizeof(struct ospf_hdr), &dd_hdr,
	    sizeof(dd_hdr)) == -1)
		goto fail;

	/* calculate checksum */
	if (upd_ospf_hdr(buf, nbr->iface))
		goto fail;

	/* transmit packet */
	if (send_packet(nbr->iface, buf, &dst) == -1)
		goto fail;

	ibuf_free(buf);
	return (0);
fail:
	log_warn("send_db_description");
	ibuf_free(buf);
	return (-1);
}

void
recv_db_description(struct nbr *nbr, char *buf, u_int16_t len)
{
	struct db_dscrp_hdr	 dd_hdr;
	int			 dupe = 0;

	if (len < sizeof(dd_hdr)) {
		log_warnx("recv_db_description: neighbor ID %s (%s): "
		    "bad packet size", inet_ntoa(nbr->id), nbr->iface->name);
		return;
	}
	memcpy(&dd_hdr, buf, sizeof(dd_hdr));
	buf += sizeof(dd_hdr);
	len -= sizeof(dd_hdr);

	/* db description packet sanity checks */
	if (ntohs(dd_hdr.iface_mtu) > nbr->iface->mtu) {
		log_warnx("recv_db_description: neighbor ID %s (%s): "
		    "invalid MTU %d expected %d", inet_ntoa(nbr->id),
		    nbr->iface->name, ntohs(dd_hdr.iface_mtu),
		    nbr->iface->mtu);
		return;
	}

	if (nbr->last_rx_options == dd_hdr.opts &&
	    nbr->last_rx_bits == dd_hdr.bits &&
	    ntohl(dd_hdr.dd_seq_num) == nbr->dd_seq_num - nbr->dd_master ?
	    1 : 0) {
		log_debug("recv_db_description: dupe from "
		    "neighbor ID %s (%s)", inet_ntoa(nbr->id),
		    nbr->iface->name);
		dupe = 1;
	}

	switch (nbr->state) {
	case NBR_STA_DOWN:
	case NBR_STA_ATTEMPT:
	case NBR_STA_2_WAY:
	case NBR_STA_SNAP:
		log_debug("recv_db_description: neighbor ID %s (%s): "
		    "packet ignored in state %s", inet_ntoa(nbr->id),
		    nbr->iface->name, nbr_state_name(nbr->state));
		return;
	case NBR_STA_INIT:
		/* evaluate dr and bdr after issuing a 2-Way event */
		nbr_fsm(nbr, NBR_EVT_2_WAY_RCVD);
		if_fsm(nbr->iface, IF_EVT_NBR_CHNG);
		if (nbr->state != NBR_STA_XSTRT)
			return;
		/* FALLTHROUGH */
	case NBR_STA_XSTRT:
		if (dupe)
			return;
		/*
		 * check bits: either I,M,MS or only M
		 */
		if (dd_hdr.bits == (OSPF_DBD_I | OSPF_DBD_M | OSPF_DBD_MS)) {
			/* if nbr Router ID is larger than own -> slave */
			if ((ntohl(nbr->id.s_addr)) >
			    ntohl(ospfe_router_id())) {
				/* slave */
				nbr->dd_master = 0;
				nbr->dd_seq_num = ntohl(dd_hdr.dd_seq_num);

				/* event negotiation done */
				nbr_fsm(nbr, NBR_EVT_NEG_DONE);
			}
		} else if (!(dd_hdr.bits & (OSPF_DBD_I | OSPF_DBD_MS))) {
			/* M only case: we are master */
			if (ntohl(dd_hdr.dd_seq_num) != nbr->dd_seq_num) {
				log_warnx("recv_db_description: "
				    "neighbor ID %s (%s): "
				    "invalid seq num, mine %x his %x",
				    inet_ntoa(nbr->id), nbr->iface->name,
				    nbr->dd_seq_num, ntohl(dd_hdr.dd_seq_num));
				return;
			}
			nbr->dd_seq_num++;

			/* event negotiation done */
			nbr_fsm(nbr, NBR_EVT_NEG_DONE);

			/* this packet may already have data so pass it on */
			if (len > 0) {
				nbr->dd_pending++;
				ospfe_imsg_compose_rde(IMSG_DD, nbr->peerid,
				    0, buf, len);
			}
		} else {
			/* ignore packet */
			log_debug("recv_db_description: neighbor ID %s (%s): "
			    "packet ignored in state %s (bad flags)",
			    inet_ntoa(nbr->id), nbr->iface->name,
			    nbr_state_name(nbr->state));
		}
		break;
	case NBR_STA_XCHNG:
	case NBR_STA_LOAD:
	case NBR_STA_FULL:
		if (dd_hdr.bits & OSPF_DBD_I ||
		    !(dd_hdr.bits & OSPF_DBD_MS) == !nbr->dd_master) {
			log_warnx("recv_db_description: neighbor ID %s (%s): "
			    "seq num mismatch, bad flags", inet_ntoa(nbr->id),
			    nbr->iface->name);
			nbr_fsm(nbr, NBR_EVT_SEQ_NUM_MIS);
			return;
		}

		if (nbr->last_rx_options != dd_hdr.opts) {
			log_warnx("recv_db_description: neighbor ID %s (%s): "
			    "seq num mismatch, bad options",
			    inet_ntoa(nbr->id), nbr->iface->name);
			nbr_fsm(nbr, NBR_EVT_SEQ_NUM_MIS);
			return;
		}

		if (dupe) {
			if (!nbr->dd_master)
				/* retransmit */
				start_db_tx_timer(nbr);
			return;
		}

		if (nbr->state != NBR_STA_XCHNG) {
			log_warnx("recv_db_description: neighbor ID %s (%s): "
			    "invalid seq num, mine %x his %x",
			    inet_ntoa(nbr->id), nbr->iface->name,
			    nbr->dd_seq_num, ntohl(dd_hdr.dd_seq_num));
			nbr_fsm(nbr, NBR_EVT_SEQ_NUM_MIS);
			return;
		}

		/* sanity check dd seq number */
		if (nbr->dd_master) {
			/* master */
			if (ntohl(dd_hdr.dd_seq_num) != nbr->dd_seq_num) {
				log_warnx("recv_db_description: "
				    "neighbor ID %s (%s): "
				    "invalid seq num, mine %x his %x, master",
				    inet_ntoa(nbr->id), nbr->iface->name,
				    nbr->dd_seq_num, ntohl(dd_hdr.dd_seq_num));
				nbr_fsm(nbr, NBR_EVT_SEQ_NUM_MIS);
				return;
			}
			nbr->dd_seq_num++;
		} else {
			/* slave */
			if (ntohl(dd_hdr.dd_seq_num) != nbr->dd_seq_num + 1) {
				log_warnx("recv_db_description: "
				    "neighbor ID %s (%s): "
				    "invalid seq num, mine %x his %x, slave",
				    inet_ntoa(nbr->id), nbr->iface->name,
				    nbr->dd_seq_num, ntohl(dd_hdr.dd_seq_num));
				nbr_fsm(nbr, NBR_EVT_SEQ_NUM_MIS);
				return;
			}
			nbr->dd_seq_num = ntohl(dd_hdr.dd_seq_num);
		}

		/* forward to RDE and let it decide which LSAs to request */
		if (len > 0) {
			nbr->dd_pending++;
			ospfe_imsg_compose_rde(IMSG_DD, nbr->peerid, 0,
			    buf, len);
		}

		/* next packet */
		db_sum_list_next(nbr);
		start_db_tx_timer(nbr);

		if (!(dd_hdr.bits & OSPF_DBD_M) &&
		    TAILQ_EMPTY(&nbr->db_sum_list))
			if (!nbr->dd_master || !nbr->dd_more)
				nbr_fsm(nbr, NBR_EVT_XCHNG_DONE);
		break;
	default:
		fatalx("recv_db_description: unknown neighbor state");
	}

	nbr->last_rx_options = dd_hdr.opts;
	nbr->last_rx_bits = dd_hdr.bits;
}

void
db_sum_list_add(struct nbr *nbr, struct lsa_hdr *lsa)
{
	struct lsa_entry	*le;

	if ((le = calloc(1, sizeof(*le))) == NULL)
		fatal("db_sum_list_add");

	TAILQ_INSERT_TAIL(&nbr->db_sum_list, le, entry);
	le->le_lsa = lsa;
}

void
db_sum_list_next(struct nbr *nbr)
{
	struct lsa_entry	*le;

	while ((le = TAILQ_FIRST(&nbr->db_sum_list)) != nbr->dd_end) {
		TAILQ_REMOVE(&nbr->db_sum_list, le, entry);
		free(le->le_lsa);
		free(le);
	}
}

void
db_sum_list_clr(struct nbr *nbr)
{
	nbr->dd_end = NULL;
	db_sum_list_next(nbr);
}

/* timers */
void
db_tx_timer(int fd, short event, void *arg)
{
	struct nbr *nbr = arg;
	struct timeval tv;

	switch (nbr->state) {
	case NBR_STA_DOWN:
	case NBR_STA_ATTEMPT:
	case NBR_STA_INIT:
	case NBR_STA_2_WAY:
	case NBR_STA_SNAP:
		return ;
	case NBR_STA_XSTRT:
	case NBR_STA_XCHNG:
	case NBR_STA_LOAD:
	case NBR_STA_FULL:
		send_db_description(nbr);
		break;
	default:
		log_debug("db_tx_timer: neighbor ID %s (%s): "
		    "unknown neighbor state",
		    inet_ntoa(nbr->id), nbr->iface->name);
		break;
	}

	/* reschedule db_tx_timer but only in master mode */
	if (nbr->dd_master) {
		timerclear(&tv);
		tv.tv_sec = nbr->iface->rxmt_interval;
		if (evtimer_add(&nbr->db_tx_timer, &tv) == -1)
			fatal("db_tx_timer");
	}
}

void
start_db_tx_timer(struct nbr *nbr)
{
	struct timeval	tv;

	if (nbr == nbr->iface->self)
		return;

	timerclear(&tv);
	if (evtimer_add(&nbr->db_tx_timer, &tv) == -1)
		fatal("start_db_tx_timer");
}

void
stop_db_tx_timer(struct nbr *nbr)
{
	if (nbr == nbr->iface->self)
		return;

	if (evtimer_del(&nbr->db_tx_timer) == -1)
		fatal("stop_db_tx_timer");
}
