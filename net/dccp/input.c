/*
 *  net/dccp/input.c
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
#include <linux/skbuff.h>

#include <net/sock.h>

#include "ackvec.h"
#include "ccid.h"
#include "dccp.h"

static void dccp_fin(struct sock *sk, struct sk_buff *skb)
{
	sk->sk_shutdown |= RCV_SHUTDOWN;
	sock_set_flag(sk, SOCK_DONE);
	__skb_pull(skb, dccp_hdr(skb)->dccph_doff * 4);
	__skb_queue_tail(&sk->sk_receive_queue, skb);
	skb_set_owner_r(skb, sk);
	sk->sk_data_ready(sk, 0);
}

static void dccp_rcv_close(struct sock *sk, struct sk_buff *skb)
{
	dccp_send_reset(sk, DCCP_RESET_CODE_CLOSED);
	dccp_fin(sk, skb);
	dccp_set_state(sk, DCCP_CLOSED);
	sk_wake_async(sk, 1, POLL_HUP);
}

static void dccp_rcv_closereq(struct sock *sk, struct sk_buff *skb)
{
	/*
	 *   Step 7: Check for unexpected packet types
	 *      If (S.is_server and P.type == CloseReq)
	 *	  Send Sync packet acknowledging P.seqno
	 *	  Drop packet and return
	 */
	if (dccp_sk(sk)->dccps_role != DCCP_ROLE_CLIENT) {
		dccp_send_sync(sk, DCCP_SKB_CB(skb)->dccpd_seq, DCCP_PKT_SYNC);
		return;
	}

	if (sk->sk_state != DCCP_CLOSING)
		dccp_set_state(sk, DCCP_CLOSING);
	dccp_send_close(sk, 0);
}

static void dccp_event_ack_recv(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);

	if (dccp_msk(sk)->dccpms_send_ack_vector)
		dccp_ackvec_check_rcv_ackno(dp->dccps_hc_rx_ackvec, sk,
					    DCCP_SKB_CB(skb)->dccpd_ack_seq);
}

static int dccp_check_seqno(struct sock *sk, struct sk_buff *skb)
{
	const struct dccp_hdr *dh = dccp_hdr(skb);
	struct dccp_sock *dp = dccp_sk(sk);
	u64 lswl, lawl;

	/*
	 *   Step 5: Prepare sequence numbers for Sync
	 *     If P.type == Sync or P.type == SyncAck,
	 *	  If S.AWL <= P.ackno <= S.AWH and P.seqno >= S.SWL,
	 *	     / * P is valid, so update sequence number variables
	 *		 accordingly.  After this update, P will pass the tests
	 *		 in Step 6.  A SyncAck is generated if necessary in
	 *		 Step 15 * /
	 *	     Update S.GSR, S.SWL, S.SWH
	 *	  Otherwise,
	 *	     Drop packet and return
	 */
	if (dh->dccph_type == DCCP_PKT_SYNC ||
	    dh->dccph_type == DCCP_PKT_SYNCACK) {
		if (between48(DCCP_SKB_CB(skb)->dccpd_ack_seq,
			      dp->dccps_awl, dp->dccps_awh) &&
		    !before48(DCCP_SKB_CB(skb)->dccpd_seq, dp->dccps_swl))
			dccp_update_gsr(sk, DCCP_SKB_CB(skb)->dccpd_seq);
		else
			return -1;
	}
	
	/*
	 *   Step 6: Check sequence numbers
	 *      Let LSWL = S.SWL and LAWL = S.AWL
	 *      If P.type == CloseReq or P.type == Close or P.type == Reset,
	 *	  LSWL := S.GSR + 1, LAWL := S.GAR
	 *      If LSWL <= P.seqno <= S.SWH
	 *	     and (P.ackno does not exist or LAWL <= P.ackno <= S.AWH),
	 *	  Update S.GSR, S.SWL, S.SWH
	 *	  If P.type != Sync,
	 *	     Update S.GAR
	 *      Otherwise,
	 *	  Send Sync packet acknowledging P.seqno
	 *	  Drop packet and return
	 */
	lswl = dp->dccps_swl;
	lawl = dp->dccps_awl;

	if (dh->dccph_type == DCCP_PKT_CLOSEREQ ||
	    dh->dccph_type == DCCP_PKT_CLOSE ||
	    dh->dccph_type == DCCP_PKT_RESET) {
		lswl = dp->dccps_gsr;
		dccp_inc_seqno(&lswl);
		lawl = dp->dccps_gar;
	}

	if (between48(DCCP_SKB_CB(skb)->dccpd_seq, lswl, dp->dccps_swh) &&
	    (DCCP_SKB_CB(skb)->dccpd_ack_seq == DCCP_PKT_WITHOUT_ACK_SEQ ||
	     between48(DCCP_SKB_CB(skb)->dccpd_ack_seq,
		       lawl, dp->dccps_awh))) {
		dccp_update_gsr(sk, DCCP_SKB_CB(skb)->dccpd_seq);

		if (dh->dccph_type != DCCP_PKT_SYNC &&
		    (DCCP_SKB_CB(skb)->dccpd_ack_seq !=
		     DCCP_PKT_WITHOUT_ACK_SEQ))
			dp->dccps_gar = DCCP_SKB_CB(skb)->dccpd_ack_seq;
	} else {
		DCCP_WARN("DCCP: Step 6 failed for %s packet, "
			  "(LSWL(%llu) <= P.seqno(%llu) <= S.SWH(%llu)) and "
			  "(P.ackno %s or LAWL(%llu) <= P.ackno(%llu) <= S.AWH(%llu), "
			  "sending SYNC...\n",  dccp_packet_name(dh->dccph_type),
			  (unsigned long long) lswl,
			  (unsigned long long) DCCP_SKB_CB(skb)->dccpd_seq,
			  (unsigned long long) dp->dccps_swh,
			  (DCCP_SKB_CB(skb)->dccpd_ack_seq ==
			        DCCP_PKT_WITHOUT_ACK_SEQ) ? "doesn't exist" : "exists",
			  (unsigned long long) lawl,
			  (unsigned long long) DCCP_SKB_CB(skb)->dccpd_ack_seq,
			  (unsigned long long) dp->dccps_awh);
		dccp_send_sync(sk, DCCP_SKB_CB(skb)->dccpd_seq, DCCP_PKT_SYNC);
		return -1;
	}

	return 0;
}

static int __dccp_rcv_established(struct sock *sk, struct sk_buff *skb,
				  const struct dccp_hdr *dh, const unsigned len)
{
	struct dccp_sock *dp = dccp_sk(sk);

	switch (dccp_hdr(skb)->dccph_type) {
	case DCCP_PKT_DATAACK:
	case DCCP_PKT_DATA:
		/*
		 * FIXME: check if sk_receive_queue is full, schedule DATA_DROPPED
		 * option if it is.
		 */
		__skb_pull(skb, dh->dccph_doff * 4);
		__skb_queue_tail(&sk->sk_receive_queue, skb);
		skb_set_owner_r(skb, sk);
		sk->sk_data_ready(sk, 0);
		return 0;
	case DCCP_PKT_ACK:
		goto discard;
	case DCCP_PKT_RESET:
		/*
		 *  Step 9: Process Reset
		 *	If P.type == Reset,
		 *		Tear down connection
		 *		S.state := TIMEWAIT
		 *		Set TIMEWAIT timer
		 *		Drop packet and return
		*/
		dccp_fin(sk, skb);
		dccp_time_wait(sk, DCCP_TIME_WAIT, 0);
		return 0;
	case DCCP_PKT_CLOSEREQ:
		dccp_rcv_closereq(sk, skb);
		goto discard;
	case DCCP_PKT_CLOSE:
		dccp_rcv_close(sk, skb);
		return 0;
	case DCCP_PKT_REQUEST:
		/* Step 7
		 *   or (S.is_server and P.type == Response)
		 *   or (S.is_client and P.type == Request)
		 *   or (S.state >= OPEN and P.type == Request
		 *	and P.seqno >= S.OSR)
		 *    or (S.state >= OPEN and P.type == Response
		 *	and P.seqno >= S.OSR)
		 *    or (S.state == RESPOND and P.type == Data),
		 *  Send Sync packet acknowledging P.seqno
		 *  Drop packet and return
		 */
		if (dp->dccps_role != DCCP_ROLE_LISTEN)
			goto send_sync;
		goto check_seq;
	case DCCP_PKT_RESPONSE:
		if (dp->dccps_role != DCCP_ROLE_CLIENT)
			goto send_sync;
check_seq:
		if (!before48(DCCP_SKB_CB(skb)->dccpd_seq, dp->dccps_osr)) {
send_sync:
			dccp_send_sync(sk, DCCP_SKB_CB(skb)->dccpd_seq,
				       DCCP_PKT_SYNC);
		}
		break;
	case DCCP_PKT_SYNC:
		dccp_send_sync(sk, DCCP_SKB_CB(skb)->dccpd_seq,
			       DCCP_PKT_SYNCACK);
		/*
		 * From RFC 4340, sec. 5.7
		 *
		 * As with DCCP-Ack packets, DCCP-Sync and DCCP-SyncAck packets
		 * MAY have non-zero-length application data areas, whose
		 * contents receivers MUST ignore.
		 */
		goto discard;
	}

	DCCP_INC_STATS_BH(DCCP_MIB_INERRS);
discard:
	__kfree_skb(skb);
	return 0;
}

int dccp_rcv_established(struct sock *sk, struct sk_buff *skb,
			 const struct dccp_hdr *dh, const unsigned len)
{
	struct dccp_sock *dp = dccp_sk(sk);

	if (dccp_check_seqno(sk, skb))
		goto discard;

	if (dccp_parse_options(sk, skb))
		goto discard;

	if (DCCP_SKB_CB(skb)->dccpd_ack_seq != DCCP_PKT_WITHOUT_ACK_SEQ)
		dccp_event_ack_recv(sk, skb);

	if (dccp_msk(sk)->dccpms_send_ack_vector &&
	    dccp_ackvec_add(dp->dccps_hc_rx_ackvec, sk,
			    DCCP_SKB_CB(skb)->dccpd_seq,
			    DCCP_ACKVEC_STATE_RECEIVED))
		goto discard;

	/*
	 * Deliver to the CCID module in charge.
	 * FIXME: Currently DCCP operates one-directional only, i.e. a listening
	 *        server is not at the same time a connecting client. There is
	 *        not much sense in delivering to both rx/tx sides at the moment
	 *        (only one is active at a time); when moving to bidirectional
	 *        service, this needs to be revised.
	 */
	if (dccp_sk(sk)->dccps_role == DCCP_ROLE_SERVER)
		ccid_hc_rx_packet_recv(dp->dccps_hc_rx_ccid, sk, skb);
	else
		ccid_hc_tx_packet_recv(dp->dccps_hc_tx_ccid, sk, skb);

	return __dccp_rcv_established(sk, skb, dh, len);
discard:
	__kfree_skb(skb);
	return 0;
}

EXPORT_SYMBOL_GPL(dccp_rcv_established);

static int dccp_rcv_request_sent_state_process(struct sock *sk,
					       struct sk_buff *skb,
					       const struct dccp_hdr *dh,
					       const unsigned len)
{
	/*
	 *  Step 4: Prepare sequence numbers in REQUEST
	 *     If S.state == REQUEST,
	 *	  If (P.type == Response or P.type == Reset)
	 *		and S.AWL <= P.ackno <= S.AWH,
	 *	     / * Set sequence number variables corresponding to the
	 *		other endpoint, so P will pass the tests in Step 6 * /
	 *	     Set S.GSR, S.ISR, S.SWL, S.SWH
	 *	     / * Response processing continues in Step 10; Reset
	 *		processing continues in Step 9 * /
	*/
	if (dh->dccph_type == DCCP_PKT_RESPONSE) {
		const struct inet_connection_sock *icsk = inet_csk(sk);
		struct dccp_sock *dp = dccp_sk(sk);

		/* Stop the REQUEST timer */
		inet_csk_clear_xmit_timer(sk, ICSK_TIME_RETRANS);
		BUG_TRAP(sk->sk_send_head != NULL);
		__kfree_skb(sk->sk_send_head);
		sk->sk_send_head = NULL;

		if (!between48(DCCP_SKB_CB(skb)->dccpd_ack_seq,
			       dp->dccps_awl, dp->dccps_awh)) {
			dccp_pr_debug("invalid ackno: S.AWL=%llu, "
				      "P.ackno=%llu, S.AWH=%llu \n",
				      (unsigned long long)dp->dccps_awl,
			   (unsigned long long)DCCP_SKB_CB(skb)->dccpd_ack_seq,
				      (unsigned long long)dp->dccps_awh);
			goto out_invalid_packet;
		}

		if (dccp_parse_options(sk, skb))
			goto out_invalid_packet;

                if (dccp_msk(sk)->dccpms_send_ack_vector &&
                    dccp_ackvec_add(dp->dccps_hc_rx_ackvec, sk,
                                    DCCP_SKB_CB(skb)->dccpd_seq,
                                    DCCP_ACKVEC_STATE_RECEIVED))
                        goto out_invalid_packet; /* FIXME: change error code */

		dp->dccps_isr = DCCP_SKB_CB(skb)->dccpd_seq;
		dccp_update_gsr(sk, dp->dccps_isr);
		/*
		 * SWL and AWL are initially adjusted so that they are not less than
		 * the initial Sequence Numbers received and sent, respectively:
		 *	SWL := max(GSR + 1 - floor(W/4), ISR),
		 *	AWL := max(GSS - W' + 1, ISS).
		 * These adjustments MUST be applied only at the beginning of the
		 * connection.
		 *
		 * AWL was adjusted in dccp_v4_connect -acme
		 */
		dccp_set_seqno(&dp->dccps_swl,
			       max48(dp->dccps_swl, dp->dccps_isr));

		dccp_sync_mss(sk, icsk->icsk_pmtu_cookie);

		/*
		 *    Step 10: Process REQUEST state (second part)
		 *       If S.state == REQUEST,
		 *	  / * If we get here, P is a valid Response from the
		 *	      server (see Step 4), and we should move to
		 *	      PARTOPEN state. PARTOPEN means send an Ack,
		 *	      don't send Data packets, retransmit Acks
		 *	      periodically, and always include any Init Cookie
		 *	      from the Response * /
		 *	  S.state := PARTOPEN
		 *	  Set PARTOPEN timer
		 *	  Continue with S.state == PARTOPEN
		 *	  / * Step 12 will send the Ack completing the
		 *	      three-way handshake * /
		 */
		dccp_set_state(sk, DCCP_PARTOPEN);

		/* Make sure socket is routed, for correct metrics. */
		icsk->icsk_af_ops->rebuild_header(sk);

		if (!sock_flag(sk, SOCK_DEAD)) {
			sk->sk_state_change(sk);
			sk_wake_async(sk, 0, POLL_OUT);
		}

		if (sk->sk_write_pending || icsk->icsk_ack.pingpong ||
		    icsk->icsk_accept_queue.rskq_defer_accept) {
			/* Save one ACK. Data will be ready after
			 * several ticks, if write_pending is set.
			 *
			 * It may be deleted, but with this feature tcpdumps
			 * look so _wonderfully_ clever, that I was not able
			 * to stand against the temptation 8)     --ANK
			 */
			/*
			 * OK, in DCCP we can as well do a similar trick, its
			 * even in the draft, but there is no need for us to
			 * schedule an ack here, as dccp_sendmsg does this for
			 * us, also stated in the draft. -acme
			 */
			__kfree_skb(skb);
			return 0;
		}
		dccp_send_ack(sk);
		return -1;
	}

out_invalid_packet:
	/* dccp_v4_do_rcv will send a reset */
	DCCP_SKB_CB(skb)->dccpd_reset_code = DCCP_RESET_CODE_PACKET_ERROR;
	return 1;
}

static int dccp_rcv_respond_partopen_state_process(struct sock *sk,
						   struct sk_buff *skb,
						   const struct dccp_hdr *dh,
						   const unsigned len)
{
	int queued = 0;

	switch (dh->dccph_type) {
	case DCCP_PKT_RESET:
		inet_csk_clear_xmit_timer(sk, ICSK_TIME_DACK);
		break;
	case DCCP_PKT_DATA:
		if (sk->sk_state == DCCP_RESPOND)
			break;
	case DCCP_PKT_DATAACK:
	case DCCP_PKT_ACK:
		/*
		 * FIXME: we should be reseting the PARTOPEN (DELACK) timer
		 * here but only if we haven't used the DELACK timer for
		 * something else, like sending a delayed ack for a TIMESTAMP
		 * echo, etc, for now were not clearing it, sending an extra
		 * ACK when there is nothing else to do in DELACK is not a big
		 * deal after all.
		 */

		/* Stop the PARTOPEN timer */
		if (sk->sk_state == DCCP_PARTOPEN)
			inet_csk_clear_xmit_timer(sk, ICSK_TIME_DACK);

		dccp_sk(sk)->dccps_osr = DCCP_SKB_CB(skb)->dccpd_seq;
		dccp_set_state(sk, DCCP_OPEN);

		if (dh->dccph_type == DCCP_PKT_DATAACK ||
		    dh->dccph_type == DCCP_PKT_DATA) {
			__dccp_rcv_established(sk, skb, dh, len);
			queued = 1; /* packet was queued
				       (by __dccp_rcv_established) */
		}
		break;
	}

	return queued;
}

int dccp_rcv_state_process(struct sock *sk, struct sk_buff *skb,
			   struct dccp_hdr *dh, unsigned len)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct dccp_skb_cb *dcb = DCCP_SKB_CB(skb);
	const int old_state = sk->sk_state;
	int queued = 0;

	/*
	 *  Step 3: Process LISTEN state
	 *
	 *     If S.state == LISTEN,
	 *	 If P.type == Request or P contains a valid Init Cookie option,
	 *	      (* Must scan the packet's options to check for Init
	 *		 Cookies.  Only Init Cookies are processed here,
	 *		 however; other options are processed in Step 8.  This
	 *		 scan need only be performed if the endpoint uses Init
	 *		 Cookies *)
	 *	      (* Generate a new socket and switch to that socket *)
	 *	      Set S := new socket for this port pair
	 *	      S.state = RESPOND
	 *	      Choose S.ISS (initial seqno) or set from Init Cookies
	 *	      Initialize S.GAR := S.ISS
	 *	      Set S.ISR, S.GSR, S.SWL, S.SWH from packet or Init
	 *	      Cookies Continue with S.state == RESPOND
	 *	      (* A Response packet will be generated in Step 11 *)
	 *	 Otherwise,
	 *	      Generate Reset(No Connection) unless P.type == Reset
	 *	      Drop packet and return
	 */
	if (sk->sk_state == DCCP_LISTEN) {
		if (dh->dccph_type == DCCP_PKT_REQUEST) {
			if (inet_csk(sk)->icsk_af_ops->conn_request(sk,
								    skb) < 0)
				return 1;

			/* FIXME: do congestion control initialization */
			goto discard;
		}
		if (dh->dccph_type == DCCP_PKT_RESET)
			goto discard;

		/* Caller (dccp_v4_do_rcv) will send Reset */
		dcb->dccpd_reset_code = DCCP_RESET_CODE_NO_CONNECTION;
		return 1;
	}

	if (sk->sk_state != DCCP_REQUESTING) {
		if (dccp_check_seqno(sk, skb))
			goto discard;

		/*
		 * Step 8: Process options and mark acknowledgeable
		 */
		if (dccp_parse_options(sk, skb))
			goto discard;

		if (dcb->dccpd_ack_seq != DCCP_PKT_WITHOUT_ACK_SEQ)
			dccp_event_ack_recv(sk, skb);

		if (dccp_msk(sk)->dccpms_send_ack_vector &&
		    dccp_ackvec_add(dp->dccps_hc_rx_ackvec, sk,
				    DCCP_SKB_CB(skb)->dccpd_seq,
				    DCCP_ACKVEC_STATE_RECEIVED))
			goto discard;

		/* XXX see the comments in dccp_rcv_established about this */
		if (dccp_sk(sk)->dccps_role == DCCP_ROLE_SERVER)
			ccid_hc_rx_packet_recv(dp->dccps_hc_rx_ccid, sk, skb);
		else
			ccid_hc_tx_packet_recv(dp->dccps_hc_tx_ccid, sk, skb);
	}

	/*
	 *  Step 9: Process Reset
	 *	If P.type == Reset,
	 *		Tear down connection
	 *		S.state := TIMEWAIT
	 *		Set TIMEWAIT timer
	 *		Drop packet and return
	*/
	if (dh->dccph_type == DCCP_PKT_RESET) {
		/*
		 * Queue the equivalent of TCP fin so that dccp_recvmsg
		 * exits the loop
		 */
		dccp_fin(sk, skb);
		dccp_time_wait(sk, DCCP_TIME_WAIT, 0);
		return 0;
		/*
		 *   Step 7: Check for unexpected packet types
		 *      If (S.is_server and P.type == CloseReq)
		 *	    or (S.is_server and P.type == Response)
		 *	    or (S.is_client and P.type == Request)
		 *	    or (S.state == RESPOND and P.type == Data),
		 *	  Send Sync packet acknowledging P.seqno
		 *	  Drop packet and return
		 */
	} else if ((dp->dccps_role != DCCP_ROLE_CLIENT &&
		    (dh->dccph_type == DCCP_PKT_RESPONSE ||
		     dh->dccph_type == DCCP_PKT_CLOSEREQ)) ||
		    (dp->dccps_role == DCCP_ROLE_CLIENT &&
		     dh->dccph_type == DCCP_PKT_REQUEST) ||
		    (sk->sk_state == DCCP_RESPOND &&
		     dh->dccph_type == DCCP_PKT_DATA)) {
		dccp_send_sync(sk, dcb->dccpd_seq, DCCP_PKT_SYNC);
		goto discard;
	} else if (dh->dccph_type == DCCP_PKT_CLOSEREQ) {
		dccp_rcv_closereq(sk, skb);
		goto discard;
	} else if (dh->dccph_type == DCCP_PKT_CLOSE) {
		dccp_rcv_close(sk, skb);
		return 0;
	}

	if (unlikely(dh->dccph_type == DCCP_PKT_SYNC)) {
		dccp_send_sync(sk, dcb->dccpd_seq, DCCP_PKT_SYNCACK);
		goto discard;
	}

	switch (sk->sk_state) {
	case DCCP_CLOSED:
		dcb->dccpd_reset_code = DCCP_RESET_CODE_NO_CONNECTION;
		return 1;

	case DCCP_REQUESTING:
		/* FIXME: do congestion control initialization */

		queued = dccp_rcv_request_sent_state_process(sk, skb, dh, len);
		if (queued >= 0)
			return queued;

		__kfree_skb(skb);
		return 0;

	case DCCP_RESPOND:
	case DCCP_PARTOPEN:
		queued = dccp_rcv_respond_partopen_state_process(sk, skb,
								 dh, len);
		break;
	}

	if (dh->dccph_type == DCCP_PKT_ACK ||
	    dh->dccph_type == DCCP_PKT_DATAACK) {
		switch (old_state) {
		case DCCP_PARTOPEN:
			sk->sk_state_change(sk);
			sk_wake_async(sk, 0, POLL_OUT);
			break;
		}
	}

	if (!queued) {
discard:
		__kfree_skb(skb);
	}
	return 0;
}

EXPORT_SYMBOL_GPL(dccp_rcv_state_process);
