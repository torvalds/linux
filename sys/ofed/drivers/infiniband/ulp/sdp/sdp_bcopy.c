/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2006 Mellanox Technologies Ltd.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id$
 */
#include "sdp.h"

static void sdp_nagle_timeout(void *data);

#ifdef CONFIG_INFINIBAND_SDP_DEBUG_DATA
void _dump_packet(const char *func, int line, struct socket *sk, char *str,
		struct mbuf *mb, const struct sdp_bsdh *h)
{
	struct sdp_hh *hh;
	struct sdp_hah *hah;
	struct sdp_chrecvbuf *req_size;
	struct sdp_rrch *rrch;
	struct sdp_srcah *srcah;
	int len = 0;
	char buf[256];
	len += snprintf(buf, 255-len, "%s mb: %p mid: %2x:%-20s flags: 0x%x "
			"bufs: 0x%x len: 0x%x mseq: 0x%x mseq_ack: 0x%x | ",
			str, mb, h->mid, mid2str(h->mid), h->flags,
			ntohs(h->bufs), ntohl(h->len), ntohl(h->mseq),
			ntohl(h->mseq_ack));

	switch (h->mid) {
	case SDP_MID_HELLO:
		hh = (struct sdp_hh *)h;
		len += snprintf(buf + len, 255-len,
				"max_adverts: %d  majv_minv: 0x%x "
				"localrcvsz: 0x%x desremrcvsz: 0x%x |",
				hh->max_adverts, hh->majv_minv,
				ntohl(hh->localrcvsz),
				ntohl(hh->desremrcvsz));
		break;
	case SDP_MID_HELLO_ACK:
		hah = (struct sdp_hah *)h;
		len += snprintf(buf + len, 255-len, "actrcvz: 0x%x |",
				ntohl(hah->actrcvsz));
		break;
	case SDP_MID_CHRCVBUF:
	case SDP_MID_CHRCVBUF_ACK:
		req_size = (struct sdp_chrecvbuf *)(h+1);
		len += snprintf(buf + len, 255-len, "req_size: 0x%x |",
				ntohl(req_size->size));
		break;
	case SDP_MID_DATA:
		len += snprintf(buf + len, 255-len, "data_len: 0x%lx |",
			ntohl(h->len) - sizeof(struct sdp_bsdh));
		break;
	case SDP_MID_RDMARDCOMPL:
		rrch = (struct sdp_rrch *)(h+1);

		len += snprintf(buf + len, 255-len, " | len: 0x%x |",
				ntohl(rrch->len));
		break;
	case SDP_MID_SRCAVAIL:
		srcah = (struct sdp_srcah *)(h+1);

		len += snprintf(buf + len, 255-len, " | payload: 0x%lx, "
				"len: 0x%x, rkey: 0x%x, vaddr: 0x%jx |",
				ntohl(h->len) - sizeof(struct sdp_bsdh) - 
				sizeof(struct sdp_srcah),
				ntohl(srcah->len), ntohl(srcah->rkey),
				be64_to_cpu(srcah->vaddr));
		break;
	default:
		break;
	}
	buf[len] = 0;
	_sdp_printk(func, line, KERN_WARNING, sk, "%s: %s\n", str, buf);
}
#endif

static inline int
sdp_nagle_off(struct sdp_sock *ssk, struct mbuf *mb)
{

	struct sdp_bsdh *h;

	h = mtod(mb, struct sdp_bsdh *);
	int send_now =
#ifdef SDP_ZCOPY
		BZCOPY_STATE(mb) ||
#endif
		unlikely(h->mid != SDP_MID_DATA) ||
		(ssk->flags & SDP_NODELAY) ||
		!ssk->nagle_last_unacked ||
		mb->m_pkthdr.len >= ssk->xmit_size_goal / 4 ||
		(mb->m_flags & M_PUSH);

	if (send_now) {
		unsigned long mseq = ring_head(ssk->tx_ring);
		ssk->nagle_last_unacked = mseq;
	} else {
		if (!callout_pending(&ssk->nagle_timer)) {
			callout_reset(&ssk->nagle_timer, SDP_NAGLE_TIMEOUT,
			    sdp_nagle_timeout, ssk);
			sdp_dbg_data(ssk->socket, "Starting nagle timer\n");
		}
	}
	sdp_dbg_data(ssk->socket, "send_now = %d last_unacked = %ld\n",
		send_now, ssk->nagle_last_unacked);

	return send_now;
}

static void
sdp_nagle_timeout(void *data)
{
	struct sdp_sock *ssk = (struct sdp_sock *)data;
	struct socket *sk = ssk->socket;

	sdp_dbg_data(sk, "last_unacked = %ld\n", ssk->nagle_last_unacked);

	if (!callout_active(&ssk->nagle_timer))
		return;
	callout_deactivate(&ssk->nagle_timer);

	if (!ssk->nagle_last_unacked)
		goto out;
	if (ssk->state == TCPS_CLOSED)
		return;
	ssk->nagle_last_unacked = 0;
	sdp_post_sends(ssk, M_NOWAIT);

	sowwakeup(ssk->socket);
out:
	if (sk->so_snd.sb_sndptr)
		callout_reset(&ssk->nagle_timer, SDP_NAGLE_TIMEOUT,
		    sdp_nagle_timeout, ssk);
}

void
sdp_post_sends(struct sdp_sock *ssk, int wait)
{
	struct mbuf *mb;
	int post_count = 0;
	struct socket *sk;
	int low;

	sk = ssk->socket;
	if (unlikely(!ssk->id)) {
		if (sk->so_snd.sb_sndptr) {
			sdp_dbg(ssk->socket,
				"Send on socket without cmid ECONNRESET.\n");
			sdp_notify(ssk, ECONNRESET);
		}
		return;
	}
again:
	if (sdp_tx_ring_slots_left(ssk) < SDP_TX_SIZE / 2)
		sdp_xmit_poll(ssk,  1);

	if (ssk->recv_request &&
	    ring_tail(ssk->rx_ring) >= ssk->recv_request_head &&
	    tx_credits(ssk) >= SDP_MIN_TX_CREDITS &&
	    sdp_tx_ring_slots_left(ssk)) {
		mb = sdp_alloc_mb_chrcvbuf_ack(sk,
		    ssk->recv_bytes - SDP_HEAD_SIZE, wait);
		if (mb == NULL)
			goto allocfail;
		ssk->recv_request = 0;
		sdp_post_send(ssk, mb);
		post_count++;
	}

	if (tx_credits(ssk) <= SDP_MIN_TX_CREDITS &&
	    sdp_tx_ring_slots_left(ssk) && sk->so_snd.sb_sndptr &&
	    sdp_nagle_off(ssk, sk->so_snd.sb_sndptr)) {
		SDPSTATS_COUNTER_INC(send_miss_no_credits);
	}

	while (tx_credits(ssk) > SDP_MIN_TX_CREDITS &&
	    sdp_tx_ring_slots_left(ssk) && (mb = sk->so_snd.sb_sndptr) &&
	    sdp_nagle_off(ssk, mb)) {
		struct mbuf *n;

		SOCKBUF_LOCK(&sk->so_snd);
		sk->so_snd.sb_sndptr = mb->m_nextpkt;
		sk->so_snd.sb_mb = mb->m_nextpkt;
		mb->m_nextpkt = NULL;
		SB_EMPTY_FIXUP(&sk->so_snd);
		for (n = mb; n != NULL; n = n->m_next)
			sbfree(&sk->so_snd, n);
		SOCKBUF_UNLOCK(&sk->so_snd);
		sdp_post_send(ssk, mb);
		post_count++;
	}

	if (credit_update_needed(ssk) && ssk->state >= TCPS_ESTABLISHED &&
	    ssk->state < TCPS_FIN_WAIT_2) {
		mb = sdp_alloc_mb_data(ssk->socket, wait);
		if (mb == NULL)
			goto allocfail;
		sdp_post_send(ssk, mb);

		SDPSTATS_COUNTER_INC(post_send_credits);
		post_count++;
	}

	/* send DisConn if needed
	 * Do not send DisConn if there is only 1 credit. Compliance with CA4-82
	 * If one credit is available, an implementation shall only send SDP
	 * messages that provide additional credits and also do not contain ULP
	 * payload. */
	if ((ssk->flags & SDP_NEEDFIN) && !sk->so_snd.sb_sndptr &&
	    tx_credits(ssk) > 1) {
		mb = sdp_alloc_mb_disconnect(sk, wait);
		if (mb == NULL)
			goto allocfail;
		ssk->flags &= ~SDP_NEEDFIN;
		sdp_post_send(ssk, mb);
		post_count++;
	}
	low = (sdp_tx_ring_slots_left(ssk) <= SDP_MIN_TX_CREDITS);
	if (post_count || low) {
		if (low)
			sdp_arm_tx_cq(ssk);
		if (sdp_xmit_poll(ssk, low))
			goto again;
	}
	return;

allocfail:
	ssk->nagle_last_unacked = -1;
	callout_reset(&ssk->nagle_timer, 1, sdp_nagle_timeout, ssk);
	return;
}
