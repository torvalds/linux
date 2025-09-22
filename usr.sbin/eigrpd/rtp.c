/*	$OpenBSD: rtp.c,v 1.8 2023/03/08 04:43:13 guenther Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
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

#include <stdlib.h>

#include "eigrpd.h"
#include "eigrpe.h"
#include "log.h"

static struct pbuf	*rtp_buf_new(struct ibuf *);
static struct pbuf	*rtp_buf_hold(struct pbuf *);
static void		 rtp_buf_release(struct pbuf *);
static struct packet	*rtp_packet_new(struct nbr *, uint32_t, struct pbuf *);
static void		 rtp_send_packet(struct packet *);
static void		 rtp_enqueue_packet(struct packet *);
static void		 rtp_send_mcast(struct eigrp_iface *, struct ibuf *);
static void		 rtp_retrans_timer(int, short, void *);
static void		 rtp_retrans_start_timer(struct packet *);
static void		 rtp_retrans_stop_timer(struct packet *);

static struct pbuf *
rtp_buf_new(struct ibuf *buf)
{
	struct pbuf	*pbuf;

	if ((pbuf = calloc(1, sizeof(*pbuf))) == NULL)
		fatal("rtp_buf_new");
	pbuf->buf = buf;

	return (pbuf);
}

static struct pbuf *
rtp_buf_hold(struct pbuf *pbuf)
{
	pbuf->refcnt++;
	return (pbuf);
}

static void
rtp_buf_release(struct pbuf *pbuf)
{
	if (--pbuf->refcnt == 0) {
		ibuf_free(pbuf->buf);
		free(pbuf);
	}
}

static struct packet *
rtp_packet_new(struct nbr *nbr, uint32_t seq_num, struct pbuf *pbuf)
{
	struct packet		*pkt;

	if ((pkt = calloc(1, sizeof(struct packet))) == NULL)
		fatal("rtp_packet_new");

	pkt->nbr = nbr;
	pkt->seq_num = seq_num;
	pkt->pbuf = rtp_buf_hold(pbuf);
	pkt->attempts = 1;
	evtimer_set(&pkt->ev_timeout, rtp_retrans_timer, pkt);

	return (pkt);
}

void
rtp_packet_del(struct packet *pkt)
{
	TAILQ_REMOVE(&pkt->nbr->retrans_list, pkt, entry);
	rtp_retrans_stop_timer(pkt);
	rtp_buf_release(pkt->pbuf);
	free(pkt);
}

void
rtp_process_ack(struct nbr *nbr, uint32_t ack_num)
{
	struct eigrp	*eigrp = nbr->ei->eigrp;
	struct packet	*pkt;

	/* window size is one */
	pkt = TAILQ_FIRST(&nbr->retrans_list);
	if (pkt && pkt->seq_num == ack_num) {
		log_debug("%s: nbr %s ack %u", __func__,
		    log_addr(eigrp->af, &nbr->addr), ack_num);

		/* dequeue packet from retransmission queue */
		rtp_packet_del(pkt);

		/* enqueue next packet from retransmission queue */
		pkt = TAILQ_FIRST(&nbr->retrans_list);
		if (pkt)
			rtp_send_packet(pkt);
	}
}

static void
rtp_send_packet(struct packet *pkt)
{
	rtp_retrans_start_timer(pkt);
	send_packet(pkt->nbr->ei, pkt->nbr, 0, pkt->pbuf->buf);
}

static void
rtp_enqueue_packet(struct packet *pkt)
{
	/* only send packet if transmission queue is empty */
	if (TAILQ_EMPTY(&pkt->nbr->retrans_list))
		rtp_send_packet(pkt);

	TAILQ_INSERT_TAIL(&pkt->nbr->retrans_list, pkt, entry);
}

static void
rtp_seq_inc(struct eigrp *eigrp)
{
	/* automatic wraparound with unsigned arithmetic */
	eigrp->seq_num++;

	/* sequence number 0 is reserved for unreliably transmission */
	if (eigrp->seq_num == 0)
		eigrp->seq_num = 1;
}

void
rtp_send_ucast(struct nbr *nbr, struct ibuf *buf)
{
	struct eigrp		*eigrp = nbr->ei->eigrp;
	struct packet		*pkt;
	struct pbuf		*pbuf;

	pbuf = rtp_buf_new(buf);
	pkt = rtp_packet_new(nbr, eigrp->seq_num, pbuf);
	rtp_enqueue_packet(pkt);
	rtp_seq_inc(eigrp);
}

static void
rtp_send_mcast(struct eigrp_iface *ei, struct ibuf *buf)
{
	struct eigrp		*eigrp = ei->eigrp;
	struct nbr		*nbr;
	int			 total = 0, pending = 0;
	struct packet		*pkt;
	struct pbuf		*pbuf;
	uint32_t		 flags = 0;
	struct seq_addr_entry	*sa;
	struct seq_addr_head	 seq_addr_list;

	TAILQ_FOREACH(nbr, &ei->nbr_list, entry) {
		if (nbr->flags & F_EIGRP_NBR_SELF)
			continue;
		if (!TAILQ_EMPTY(&nbr->retrans_list))
			pending++;
		total++;
	}
	if (total == 0)
		return;

	/*
	 * send a multicast if there's at least one neighbor with an empty
	 * queue on the interface.
	 */
	if (pending < total) {
		/*
		 * build a hello packet with a seq tlv indicating all the
		 * neighbors that have full queues.
		 */
		if (pending > 0) {
			flags |= EIGRP_HDR_FLAG_CR;
			TAILQ_INIT(&seq_addr_list);

			TAILQ_FOREACH(nbr, &ei->nbr_list, entry) {
				if (TAILQ_EMPTY(&nbr->retrans_list))
					continue;
				if ((sa = calloc(1, sizeof(*sa))) == NULL)
					fatal("rtp_send_mcast");
				sa->af = eigrp->af;
				sa->addr = nbr->addr;
				TAILQ_INSERT_TAIL(&seq_addr_list, sa, entry);
			}

			send_hello(ei, &seq_addr_list, eigrp->seq_num);
			seq_addr_list_clr(&seq_addr_list);
		}
		send_packet(ei, NULL, flags, buf);
	}

	/* schedule an unicast retransmission for each neighbor */
	pbuf = rtp_buf_new(buf);
	TAILQ_FOREACH(nbr, &ei->nbr_list, entry) {
		pkt = rtp_packet_new(nbr, eigrp->seq_num, pbuf);
		TAILQ_INSERT_TAIL(&nbr->retrans_list, pkt, entry);
	}

	rtp_seq_inc(eigrp);
}

void
rtp_send(struct eigrp_iface *ei, struct nbr *nbr, struct ibuf *buf)
{
	if (nbr)
		rtp_send_ucast(nbr, buf);
	else
		rtp_send_mcast(ei, buf);
}

void
rtp_send_ack(struct nbr *nbr)
{
	struct eigrp		*eigrp = nbr->ei->eigrp;
	struct ibuf		*buf;

	if ((buf = ibuf_dynamic(PKG_DEF_SIZE,
	    IP_MAXPACKET - sizeof(struct ip))) == NULL)
		fatal("rtp_send_ack");

	/* EIGRP header */
	if (gen_eigrp_hdr(buf, EIGRP_OPC_HELLO, 0, 0, eigrp->as)) {
		log_warnx("%s: failed to send message", __func__);
		ibuf_free(buf);
		return;
	}

	/* send unreliably */
	send_packet(nbr->ei, nbr, 0, buf);
	ibuf_free(buf);
}

/* timers */

static void
rtp_retrans_timer(int fd, short event, void *arg)
{
	struct packet		*pkt = arg;
	struct eigrp		*eigrp = pkt->nbr->ei->eigrp;

	pkt->attempts++;

	if (pkt->attempts > RTP_RTRNS_MAX_ATTEMPTS) {
		log_warnx("%s: retry limit exceeded, nbr %s", __func__,
		    log_addr(eigrp->af, &pkt->nbr->addr));
		nbr_del(pkt->nbr);
		return;
	}

	rtp_send_packet(pkt);
}

static void
rtp_retrans_start_timer(struct packet *pkt)
{
	struct timeval		 tv;

	timerclear(&tv);
	tv.tv_sec = RTP_RTRNS_INTERVAL;
	if (evtimer_add(&pkt->ev_timeout, &tv) == -1)
		fatal("rtp_retrans_start_timer");
}

static void
rtp_retrans_stop_timer(struct packet *pkt)
{
	if (evtimer_pending(&pkt->ev_timeout, NULL) &&
	    evtimer_del(&pkt->ev_timeout) == -1)
		fatal("rtp_retrans_stop_timer");
}

void
rtp_ack_timer(int fd, short event, void *arg)
{
	struct nbr		*nbr = arg;

	rtp_send_ack(nbr);
}

void
rtp_ack_start_timer(struct nbr *nbr)
{
	struct timeval		 tv;

	timerclear(&tv);
	tv.tv_usec = RTP_ACK_TIMEOUT;
	if (evtimer_add(&nbr->ev_ack, &tv) == -1)
		fatal("rtp_ack_start_timer");
}

void
rtp_ack_stop_timer(struct nbr *nbr)
{
	if (evtimer_pending(&nbr->ev_ack, NULL) &&
	    evtimer_del(&nbr->ev_ack) == -1)
		fatal("rtp_ack_stop_timer");
}
