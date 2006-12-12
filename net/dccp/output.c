/*
 *  net/dccp/output.c
 *
 *  An implementation of the DCCP protocol
 *  Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/dccp.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>

#include <net/inet_sock.h>
#include <net/sock.h>

#include "ackvec.h"
#include "ccid.h"
#include "dccp.h"

static inline void dccp_event_ack_sent(struct sock *sk)
{
	inet_csk_clear_xmit_timer(sk, ICSK_TIME_DACK);
}

static void dccp_skb_entail(struct sock *sk, struct sk_buff *skb)
{
	skb_set_owner_w(skb, sk);
	WARN_ON(sk->sk_send_head);
	sk->sk_send_head = skb;
}

/*
 * All SKB's seen here are completely headerless. It is our
 * job to build the DCCP header, and pass the packet down to
 * IP so it can do the same plus pass the packet off to the
 * device.
 */
static int dccp_transmit_skb(struct sock *sk, struct sk_buff *skb)
{
	if (likely(skb != NULL)) {
		const struct inet_sock *inet = inet_sk(sk);
		const struct inet_connection_sock *icsk = inet_csk(sk);
		struct dccp_sock *dp = dccp_sk(sk);
		struct dccp_skb_cb *dcb = DCCP_SKB_CB(skb);
		struct dccp_hdr *dh;
		/* XXX For now we're using only 48 bits sequence numbers */
		const u32 dccp_header_size = sizeof(*dh) +
					     sizeof(struct dccp_hdr_ext) +
					  dccp_packet_hdr_len(dcb->dccpd_type);
		int err, set_ack = 1;
		u64 ackno = dp->dccps_gsr;

		dccp_inc_seqno(&dp->dccps_gss);

		switch (dcb->dccpd_type) {
		case DCCP_PKT_DATA:
			set_ack = 0;
			/* fall through */
		case DCCP_PKT_DATAACK:
			break;

		case DCCP_PKT_REQUEST:
			set_ack = 0;
			/* fall through */

		case DCCP_PKT_SYNC:
		case DCCP_PKT_SYNCACK:
			ackno = dcb->dccpd_seq;
			/* fall through */
		default:
			/*
			 * Only data packets should come through with skb->sk
			 * set.
			 */
			WARN_ON(skb->sk);
			skb_set_owner_w(skb, sk);
			break;
		}

		dcb->dccpd_seq = dp->dccps_gss;

		if (dccp_insert_options(sk, skb)) {
			kfree_skb(skb);
			return -EPROTO;
		}
		

		/* Build DCCP header and checksum it. */
		dh = dccp_zeroed_hdr(skb, dccp_header_size);
		dh->dccph_type	= dcb->dccpd_type;
		dh->dccph_sport	= inet->sport;
		dh->dccph_dport	= inet->dport;
		dh->dccph_doff	= (dccp_header_size + dcb->dccpd_opt_len) / 4;
		dh->dccph_ccval	= dcb->dccpd_ccval;
		dh->dccph_cscov = dp->dccps_pcslen;
		/* XXX For now we're using only 48 bits sequence numbers */
		dh->dccph_x	= 1;

		dp->dccps_awh = dp->dccps_gss;
		dccp_hdr_set_seq(dh, dp->dccps_gss);
		if (set_ack)
			dccp_hdr_set_ack(dccp_hdr_ack_bits(skb), ackno);

		switch (dcb->dccpd_type) {
		case DCCP_PKT_REQUEST:
			dccp_hdr_request(skb)->dccph_req_service =
							dp->dccps_service;
			break;
		case DCCP_PKT_RESET:
			dccp_hdr_reset(skb)->dccph_reset_code =
							dcb->dccpd_reset_code;
			break;
		}

		icsk->icsk_af_ops->send_check(sk, 0, skb);

		if (set_ack)
			dccp_event_ack_sent(sk);

		DCCP_INC_STATS(DCCP_MIB_OUTSEGS);

		memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));
		err = icsk->icsk_af_ops->queue_xmit(skb, sk, 0);
		return net_xmit_eval(err);
	}
	return -ENOBUFS;
}

unsigned int dccp_sync_mss(struct sock *sk, u32 pmtu)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct dccp_sock *dp = dccp_sk(sk);
	int mss_now = (pmtu - icsk->icsk_af_ops->net_header_len -
		       sizeof(struct dccp_hdr) - sizeof(struct dccp_hdr_ext));

	/* Now subtract optional transport overhead */
	mss_now -= icsk->icsk_ext_hdr_len;

	/*
	 * FIXME: this should come from the CCID infrastructure, where, say,
	 * TFRC will say it wants TIMESTAMPS, ELAPSED time, etc, for now lets
	 * put a rough estimate for NDP + TIMESTAMP + TIMESTAMP_ECHO + ELAPSED
	 * TIME + TFRC_OPT_LOSS_EVENT_RATE + TFRC_OPT_RECEIVE_RATE + padding to
	 * make it a multiple of 4
	 */

	mss_now -= ((5 + 6 + 10 + 6 + 6 + 6 + 3) / 4) * 4;

	/* And store cached results */
	icsk->icsk_pmtu_cookie = pmtu;
	dp->dccps_mss_cache = mss_now;

	return mss_now;
}

EXPORT_SYMBOL_GPL(dccp_sync_mss);

void dccp_write_space(struct sock *sk)
{
	read_lock(&sk->sk_callback_lock);

	if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
		wake_up_interruptible(sk->sk_sleep);
	/* Should agree with poll, otherwise some programs break */
	if (sock_writeable(sk))
		sk_wake_async(sk, 2, POLL_OUT);

	read_unlock(&sk->sk_callback_lock);
}

/**
 * dccp_wait_for_ccid - Wait for ccid to tell us we can send a packet
 * @sk: socket to wait for
 */
static int dccp_wait_for_ccid(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);
	DEFINE_WAIT(wait);
	unsigned long delay;
	int rc;

	while (1) {
		prepare_to_wait(sk->sk_sleep, &wait, TASK_INTERRUPTIBLE);

		if (sk->sk_err)
			goto do_error;
		if (signal_pending(current))
			goto do_interrupted;

		rc = ccid_hc_tx_send_packet(dp->dccps_hc_tx_ccid, sk, skb);
		if (rc <= 0)
			break;
		delay = msecs_to_jiffies(rc);
		sk->sk_write_pending++;
		release_sock(sk);
		schedule_timeout(delay);
		lock_sock(sk);
		sk->sk_write_pending--;
	}
out:
	finish_wait(sk->sk_sleep, &wait);
	return rc;

do_error:
	rc = -EPIPE;
	goto out;
do_interrupted:
	rc = -EINTR;
	goto out;
}

static void dccp_write_xmit_timer(unsigned long data) {
	struct sock *sk = (struct sock *)data;
	struct dccp_sock *dp = dccp_sk(sk);

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk))
		sk_reset_timer(sk, &dp->dccps_xmit_timer, jiffies+1);
	else
		dccp_write_xmit(sk, 0);
	bh_unlock_sock(sk);
	sock_put(sk);
}

void dccp_write_xmit(struct sock *sk, int block)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct sk_buff *skb;

	while ((skb = skb_peek(&sk->sk_write_queue))) {
		int err = ccid_hc_tx_send_packet(dp->dccps_hc_tx_ccid, sk, skb);

		if (err > 0) {
			if (!block) {
				sk_reset_timer(sk, &dp->dccps_xmit_timer,
						msecs_to_jiffies(err)+jiffies);
				break;
			} else
				err = dccp_wait_for_ccid(sk, skb);
			if (err && err != -EINTR)
				DCCP_BUG("err=%d after dccp_wait_for_ccid", err);
		}

		skb_dequeue(&sk->sk_write_queue);
		if (err == 0) {
			struct dccp_skb_cb *dcb = DCCP_SKB_CB(skb);
			const int len = skb->len;

			if (sk->sk_state == DCCP_PARTOPEN) {
				/* See 8.1.5.  Handshake Completion */
				inet_csk_schedule_ack(sk);
				inet_csk_reset_xmit_timer(sk, ICSK_TIME_DACK,
						  inet_csk(sk)->icsk_rto,
						  DCCP_RTO_MAX);
				dcb->dccpd_type = DCCP_PKT_DATAACK;
			} else if (dccp_ack_pending(sk))
				dcb->dccpd_type = DCCP_PKT_DATAACK;
			else
				dcb->dccpd_type = DCCP_PKT_DATA;

			err = dccp_transmit_skb(sk, skb);
			ccid_hc_tx_packet_sent(dp->dccps_hc_tx_ccid, sk, 0, len);
			if (err)
				DCCP_BUG("err=%d after ccid_hc_tx_packet_sent",
					 err);
		} else {
			dccp_pr_debug("packet discarded\n");
			kfree(skb);
		}
	}
}

int dccp_retransmit_skb(struct sock *sk, struct sk_buff *skb)
{
	if (inet_csk(sk)->icsk_af_ops->rebuild_header(sk) != 0)
		return -EHOSTUNREACH; /* Routing failure or similar. */

	return dccp_transmit_skb(sk, (skb_cloned(skb) ?
				      pskb_copy(skb, GFP_ATOMIC):
				      skb_clone(skb, GFP_ATOMIC)));
}

struct sk_buff *dccp_make_response(struct sock *sk, struct dst_entry *dst,
				   struct request_sock *req)
{
	struct dccp_hdr *dh;
	struct dccp_request_sock *dreq;
	const u32 dccp_header_size = sizeof(struct dccp_hdr) +
				     sizeof(struct dccp_hdr_ext) +
				     sizeof(struct dccp_hdr_response);
	struct sk_buff *skb = sock_wmalloc(sk, sk->sk_prot->max_header, 1,
					   GFP_ATOMIC);
	if (skb == NULL)
		return NULL;

	/* Reserve space for headers. */
	skb_reserve(skb, sk->sk_prot->max_header);

	skb->dst = dst_clone(dst);

	dreq = dccp_rsk(req);
	if (inet_rsk(req)->acked)	/* increase ISS upon retransmission */
		dccp_inc_seqno(&dreq->dreq_iss);
	DCCP_SKB_CB(skb)->dccpd_type = DCCP_PKT_RESPONSE;
	DCCP_SKB_CB(skb)->dccpd_seq  = dreq->dreq_iss;

	if (dccp_insert_options(sk, skb)) {
		kfree_skb(skb);
		return NULL;
	}

	/* Build and checksum header */
	dh = dccp_zeroed_hdr(skb, dccp_header_size);

	dh->dccph_sport	= inet_sk(sk)->sport;
	dh->dccph_dport	= inet_rsk(req)->rmt_port;
	dh->dccph_doff	= (dccp_header_size +
			   DCCP_SKB_CB(skb)->dccpd_opt_len) / 4;
	dh->dccph_type	= DCCP_PKT_RESPONSE;
	dh->dccph_x	= 1;
	dccp_hdr_set_seq(dh, dreq->dreq_iss);
	dccp_hdr_set_ack(dccp_hdr_ack_bits(skb), dreq->dreq_isr);
	dccp_hdr_response(skb)->dccph_resp_service = dreq->dreq_service;

	dccp_csum_outgoing(skb);

	/* We use `acked' to remember that a Response was already sent. */
	inet_rsk(req)->acked = 1;
	DCCP_INC_STATS(DCCP_MIB_OUTSEGS);
	return skb;
}

EXPORT_SYMBOL_GPL(dccp_make_response);

static struct sk_buff *dccp_make_reset(struct sock *sk, struct dst_entry *dst,
				       const enum dccp_reset_codes code)
{
	struct dccp_hdr *dh;
	struct dccp_sock *dp = dccp_sk(sk);
	const u32 dccp_header_size = sizeof(struct dccp_hdr) +
				     sizeof(struct dccp_hdr_ext) +
				     sizeof(struct dccp_hdr_reset);
	struct sk_buff *skb = sock_wmalloc(sk, sk->sk_prot->max_header, 1,
					   GFP_ATOMIC);
	if (skb == NULL)
		return NULL;

	/* Reserve space for headers. */
	skb_reserve(skb, sk->sk_prot->max_header);

	skb->dst = dst_clone(dst);

	dccp_inc_seqno(&dp->dccps_gss);

	DCCP_SKB_CB(skb)->dccpd_reset_code = code;
	DCCP_SKB_CB(skb)->dccpd_type	   = DCCP_PKT_RESET;
	DCCP_SKB_CB(skb)->dccpd_seq	   = dp->dccps_gss;

	if (dccp_insert_options(sk, skb)) {
		kfree_skb(skb);
		return NULL;
	}

	dh = dccp_zeroed_hdr(skb, dccp_header_size);

	dh->dccph_sport	= inet_sk(sk)->sport;
	dh->dccph_dport	= inet_sk(sk)->dport;
	dh->dccph_doff	= (dccp_header_size +
			   DCCP_SKB_CB(skb)->dccpd_opt_len) / 4;
	dh->dccph_type	= DCCP_PKT_RESET;
	dh->dccph_x	= 1;
	dccp_hdr_set_seq(dh, dp->dccps_gss);
	dccp_hdr_set_ack(dccp_hdr_ack_bits(skb), dp->dccps_gsr);

	dccp_hdr_reset(skb)->dccph_reset_code = code;
	inet_csk(sk)->icsk_af_ops->send_check(sk, 0, skb);

	DCCP_INC_STATS(DCCP_MIB_OUTSEGS);
	return skb;
}

int dccp_send_reset(struct sock *sk, enum dccp_reset_codes code)
{
	/*
	 * FIXME: what if rebuild_header fails?
	 * Should we be doing a rebuild_header here?
	 */
	int err = inet_sk_rebuild_header(sk);

	if (err == 0) {
		struct sk_buff *skb = dccp_make_reset(sk, sk->sk_dst_cache,
						      code);
		if (skb != NULL) {
			memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));
			err = inet_csk(sk)->icsk_af_ops->queue_xmit(skb, sk, 0);
			return net_xmit_eval(err);
		}
	}

	return err;
}

/*
 * Do all connect socket setups that can be done AF independent.
 */
static inline void dccp_connect_init(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct dst_entry *dst = __sk_dst_get(sk);
	struct inet_connection_sock *icsk = inet_csk(sk);

	sk->sk_err = 0;
	sock_reset_flag(sk, SOCK_DONE);
	
	dccp_sync_mss(sk, dst_mtu(dst));

	/*
	 * SWL and AWL are initially adjusted so that they are not less than
	 * the initial Sequence Numbers received and sent, respectively:
	 *	SWL := max(GSR + 1 - floor(W/4), ISR),
	 *	AWL := max(GSS - W' + 1, ISS).
	 * These adjustments MUST be applied only at the beginning of the
	 * connection.
	 */
	dccp_update_gss(sk, dp->dccps_iss);
	dccp_set_seqno(&dp->dccps_awl, max48(dp->dccps_awl, dp->dccps_iss));

	/* S.GAR - greatest valid acknowledgement number received on a non-Sync;
	 *         initialized to S.ISS (sec. 8.5)                            */
	dp->dccps_gar = dp->dccps_iss;

	icsk->icsk_retransmits = 0;
	init_timer(&dp->dccps_xmit_timer);
	dp->dccps_xmit_timer.data = (unsigned long)sk;
	dp->dccps_xmit_timer.function = dccp_write_xmit_timer;
}

int dccp_connect(struct sock *sk)
{
	struct sk_buff *skb;
	struct inet_connection_sock *icsk = inet_csk(sk);

	dccp_connect_init(sk);

	skb = alloc_skb(sk->sk_prot->max_header, sk->sk_allocation);
	if (unlikely(skb == NULL))
		return -ENOBUFS;

	/* Reserve space for headers. */
	skb_reserve(skb, sk->sk_prot->max_header);

	DCCP_SKB_CB(skb)->dccpd_type = DCCP_PKT_REQUEST;

	dccp_skb_entail(sk, skb);
	dccp_transmit_skb(sk, skb_clone(skb, GFP_KERNEL));
	DCCP_INC_STATS(DCCP_MIB_ACTIVEOPENS);

	/* Timer for repeating the REQUEST until an answer. */
	inet_csk_reset_xmit_timer(sk, ICSK_TIME_RETRANS,
				  icsk->icsk_rto, DCCP_RTO_MAX);
	return 0;
}

EXPORT_SYMBOL_GPL(dccp_connect);

void dccp_send_ack(struct sock *sk)
{
	/* If we have been reset, we may not send again. */
	if (sk->sk_state != DCCP_CLOSED) {
		struct sk_buff *skb = alloc_skb(sk->sk_prot->max_header,
						GFP_ATOMIC);

		if (skb == NULL) {
			inet_csk_schedule_ack(sk);
			inet_csk(sk)->icsk_ack.ato = TCP_ATO_MIN;
			inet_csk_reset_xmit_timer(sk, ICSK_TIME_DACK,
						  TCP_DELACK_MAX,
						  DCCP_RTO_MAX);
			return;
		}

		/* Reserve space for headers */
		skb_reserve(skb, sk->sk_prot->max_header);
		DCCP_SKB_CB(skb)->dccpd_type = DCCP_PKT_ACK;
		dccp_transmit_skb(sk, skb);
	}
}

EXPORT_SYMBOL_GPL(dccp_send_ack);

void dccp_send_delayed_ack(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	/*
	 * FIXME: tune this timer. elapsed time fixes the skew, so no problem
	 * with using 2s, and active senders also piggyback the ACK into a
	 * DATAACK packet, so this is really for quiescent senders.
	 */
	unsigned long timeout = jiffies + 2 * HZ;

	/* Use new timeout only if there wasn't a older one earlier. */
	if (icsk->icsk_ack.pending & ICSK_ACK_TIMER) {
		/* If delack timer was blocked or is about to expire,
		 * send ACK now.
		 *
		 * FIXME: check the "about to expire" part
		 */
		if (icsk->icsk_ack.blocked) {
			dccp_send_ack(sk);
			return;
		}

		if (!time_before(timeout, icsk->icsk_ack.timeout))
			timeout = icsk->icsk_ack.timeout;
	}
	icsk->icsk_ack.pending |= ICSK_ACK_SCHED | ICSK_ACK_TIMER;
	icsk->icsk_ack.timeout = timeout;
	sk_reset_timer(sk, &icsk->icsk_delack_timer, timeout);
}

void dccp_send_sync(struct sock *sk, const u64 seq,
		    const enum dccp_pkt_type pkt_type)
{
	/*
	 * We are not putting this on the write queue, so
	 * dccp_transmit_skb() will set the ownership to this
	 * sock.
	 */
	struct sk_buff *skb = alloc_skb(sk->sk_prot->max_header, GFP_ATOMIC);

	if (skb == NULL)
		/* FIXME: how to make sure the sync is sent? */
		return;

	/* Reserve space for headers and prepare control bits. */
	skb_reserve(skb, sk->sk_prot->max_header);
	DCCP_SKB_CB(skb)->dccpd_type = pkt_type;
	DCCP_SKB_CB(skb)->dccpd_seq = seq;

	dccp_transmit_skb(sk, skb);
}

EXPORT_SYMBOL_GPL(dccp_send_sync);

/*
 * Send a DCCP_PKT_CLOSE/CLOSEREQ. The caller locks the socket for us. This
 * cannot be allowed to fail queueing a DCCP_PKT_CLOSE/CLOSEREQ frame under
 * any circumstances.
 */
void dccp_send_close(struct sock *sk, const int active)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct sk_buff *skb;
	const gfp_t prio = active ? GFP_KERNEL : GFP_ATOMIC;

	skb = alloc_skb(sk->sk_prot->max_header, prio);
	if (skb == NULL)
		return;

	/* Reserve space for headers and prepare control bits. */
	skb_reserve(skb, sk->sk_prot->max_header);
	DCCP_SKB_CB(skb)->dccpd_type = dp->dccps_role == DCCP_ROLE_CLIENT ?
					DCCP_PKT_CLOSE : DCCP_PKT_CLOSEREQ;

	if (active) {
		dccp_write_xmit(sk, 1);
		dccp_skb_entail(sk, skb);
		dccp_transmit_skb(sk, skb_clone(skb, prio));
		/* FIXME do we need a retransmit timer here? */
	} else
		dccp_transmit_skb(sk, skb);
}
