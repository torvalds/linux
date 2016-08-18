/* Management of Tx window, Tx resend, ACKs and out-of-sequence reception
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/circ_buf.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/udp.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

/*
 * propose an ACK be sent
 */
void __rxrpc_propose_ACK(struct rxrpc_call *call, u8 ack_reason,
			 u32 serial, bool immediate)
{
	unsigned long expiry;
	s8 prior = rxrpc_ack_priority[ack_reason];

	ASSERTCMP(prior, >, 0);

	_enter("{%d},%s,%%%x,%u",
	       call->debug_id, rxrpc_acks(ack_reason), serial, immediate);

	if (prior < rxrpc_ack_priority[call->ackr_reason]) {
		if (immediate)
			goto cancel_timer;
		return;
	}

	/* update DELAY, IDLE, REQUESTED and PING_RESPONSE ACK serial
	 * numbers */
	if (prior == rxrpc_ack_priority[call->ackr_reason]) {
		if (prior <= 4)
			call->ackr_serial = serial;
		if (immediate)
			goto cancel_timer;
		return;
	}

	call->ackr_reason = ack_reason;
	call->ackr_serial = serial;

	switch (ack_reason) {
	case RXRPC_ACK_DELAY:
		_debug("run delay timer");
		expiry = rxrpc_soft_ack_delay;
		goto run_timer;

	case RXRPC_ACK_IDLE:
		if (!immediate) {
			_debug("run defer timer");
			expiry = rxrpc_idle_ack_delay;
			goto run_timer;
		}
		goto cancel_timer;

	case RXRPC_ACK_REQUESTED:
		expiry = rxrpc_requested_ack_delay;
		if (!expiry)
			goto cancel_timer;
		if (!immediate || serial == 1) {
			_debug("run defer timer");
			goto run_timer;
		}

	default:
		_debug("immediate ACK");
		goto cancel_timer;
	}

run_timer:
	expiry += jiffies;
	if (!timer_pending(&call->ack_timer) ||
	    time_after(call->ack_timer.expires, expiry))
		mod_timer(&call->ack_timer, expiry);
	return;

cancel_timer:
	_debug("cancel timer %%%u", serial);
	try_to_del_timer_sync(&call->ack_timer);
	read_lock_bh(&call->state_lock);
	if (call->state <= RXRPC_CALL_COMPLETE &&
	    !test_and_set_bit(RXRPC_CALL_EV_ACK, &call->events))
		rxrpc_queue_call(call);
	read_unlock_bh(&call->state_lock);
}

/*
 * propose an ACK be sent, locking the call structure
 */
void rxrpc_propose_ACK(struct rxrpc_call *call, u8 ack_reason,
		       u32 serial, bool immediate)
{
	s8 prior = rxrpc_ack_priority[ack_reason];

	if (prior > rxrpc_ack_priority[call->ackr_reason]) {
		spin_lock_bh(&call->lock);
		__rxrpc_propose_ACK(call, ack_reason, serial, immediate);
		spin_unlock_bh(&call->lock);
	}
}

/*
 * set the resend timer
 */
static void rxrpc_set_resend(struct rxrpc_call *call, u8 resend,
			     unsigned long resend_at)
{
	read_lock_bh(&call->state_lock);
	if (call->state >= RXRPC_CALL_COMPLETE)
		resend = 0;

	if (resend & 1) {
		_debug("SET RESEND");
		set_bit(RXRPC_CALL_EV_RESEND, &call->events);
	}

	if (resend & 2) {
		_debug("MODIFY RESEND TIMER");
		set_bit(RXRPC_CALL_RUN_RTIMER, &call->flags);
		mod_timer(&call->resend_timer, resend_at);
	} else {
		_debug("KILL RESEND TIMER");
		del_timer_sync(&call->resend_timer);
		clear_bit(RXRPC_CALL_EV_RESEND_TIMER, &call->events);
		clear_bit(RXRPC_CALL_RUN_RTIMER, &call->flags);
	}
	read_unlock_bh(&call->state_lock);
}

/*
 * resend packets
 */
static void rxrpc_resend(struct rxrpc_call *call)
{
	struct rxrpc_wire_header *whdr;
	struct rxrpc_skb_priv *sp;
	struct sk_buff *txb;
	unsigned long *p_txb, resend_at;
	bool stop;
	int loop;
	u8 resend;

	_enter("{%d,%d,%d,%d},",
	       call->acks_hard, call->acks_unacked,
	       atomic_read(&call->sequence),
	       CIRC_CNT(call->acks_head, call->acks_tail, call->acks_winsz));

	stop = false;
	resend = 0;
	resend_at = 0;

	for (loop = call->acks_tail;
	     loop != call->acks_head || stop;
	     loop = (loop + 1) &  (call->acks_winsz - 1)
	     ) {
		p_txb = call->acks_window + loop;
		smp_read_barrier_depends();
		if (*p_txb & 1)
			continue;

		txb = (struct sk_buff *) *p_txb;
		sp = rxrpc_skb(txb);

		if (sp->need_resend) {
			sp->need_resend = false;

			/* each Tx packet has a new serial number */
			sp->hdr.serial = atomic_inc_return(&call->conn->serial);

			whdr = (struct rxrpc_wire_header *)txb->head;
			whdr->serial = htonl(sp->hdr.serial);

			_proto("Tx DATA %%%u { #%d }",
			       sp->hdr.serial, sp->hdr.seq);
			if (rxrpc_send_data_packet(call->conn, txb) < 0) {
				stop = true;
				sp->resend_at = jiffies + 3;
			} else {
				sp->resend_at =
					jiffies + rxrpc_resend_timeout;
			}
		}

		if (time_after_eq(jiffies + 1, sp->resend_at)) {
			sp->need_resend = true;
			resend |= 1;
		} else if (resend & 2) {
			if (time_before(sp->resend_at, resend_at))
				resend_at = sp->resend_at;
		} else {
			resend_at = sp->resend_at;
			resend |= 2;
		}
	}

	rxrpc_set_resend(call, resend, resend_at);
	_leave("");
}

/*
 * handle resend timer expiry
 */
static void rxrpc_resend_timer(struct rxrpc_call *call)
{
	struct rxrpc_skb_priv *sp;
	struct sk_buff *txb;
	unsigned long *p_txb, resend_at;
	int loop;
	u8 resend;

	_enter("%d,%d,%d",
	       call->acks_tail, call->acks_unacked, call->acks_head);

	if (call->state >= RXRPC_CALL_COMPLETE)
		return;

	resend = 0;
	resend_at = 0;

	for (loop = call->acks_unacked;
	     loop != call->acks_head;
	     loop = (loop + 1) &  (call->acks_winsz - 1)
	     ) {
		p_txb = call->acks_window + loop;
		smp_read_barrier_depends();
		txb = (struct sk_buff *) (*p_txb & ~1);
		sp = rxrpc_skb(txb);

		ASSERT(!(*p_txb & 1));

		if (sp->need_resend) {
			;
		} else if (time_after_eq(jiffies + 1, sp->resend_at)) {
			sp->need_resend = true;
			resend |= 1;
		} else if (resend & 2) {
			if (time_before(sp->resend_at, resend_at))
				resend_at = sp->resend_at;
		} else {
			resend_at = sp->resend_at;
			resend |= 2;
		}
	}

	rxrpc_set_resend(call, resend, resend_at);
	_leave("");
}

/*
 * process soft ACKs of our transmitted packets
 * - these indicate packets the peer has or has not received, but hasn't yet
 *   given to the consumer, and so can still be discarded and re-requested
 */
static int rxrpc_process_soft_ACKs(struct rxrpc_call *call,
				   struct rxrpc_ackpacket *ack,
				   struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp;
	struct sk_buff *txb;
	unsigned long *p_txb, resend_at;
	int loop;
	u8 sacks[RXRPC_MAXACKS], resend;

	_enter("{%d,%d},{%d},",
	       call->acks_hard,
	       CIRC_CNT(call->acks_head, call->acks_tail, call->acks_winsz),
	       ack->nAcks);

	if (skb_copy_bits(skb, 0, sacks, ack->nAcks) < 0)
		goto protocol_error;

	resend = 0;
	resend_at = 0;
	for (loop = 0; loop < ack->nAcks; loop++) {
		p_txb = call->acks_window;
		p_txb += (call->acks_tail + loop) & (call->acks_winsz - 1);
		smp_read_barrier_depends();
		txb = (struct sk_buff *) (*p_txb & ~1);
		sp = rxrpc_skb(txb);

		switch (sacks[loop]) {
		case RXRPC_ACK_TYPE_ACK:
			sp->need_resend = false;
			*p_txb |= 1;
			break;
		case RXRPC_ACK_TYPE_NACK:
			sp->need_resend = true;
			*p_txb &= ~1;
			resend = 1;
			break;
		default:
			_debug("Unsupported ACK type %d", sacks[loop]);
			goto protocol_error;
		}
	}

	smp_mb();
	call->acks_unacked = (call->acks_tail + loop) & (call->acks_winsz - 1);

	/* anything not explicitly ACK'd is implicitly NACK'd, but may just not
	 * have been received or processed yet by the far end */
	for (loop = call->acks_unacked;
	     loop != call->acks_head;
	     loop = (loop + 1) &  (call->acks_winsz - 1)
	     ) {
		p_txb = call->acks_window + loop;
		smp_read_barrier_depends();
		txb = (struct sk_buff *) (*p_txb & ~1);
		sp = rxrpc_skb(txb);

		if (*p_txb & 1) {
			/* packet must have been discarded */
			sp->need_resend = true;
			*p_txb &= ~1;
			resend |= 1;
		} else if (sp->need_resend) {
			;
		} else if (time_after_eq(jiffies + 1, sp->resend_at)) {
			sp->need_resend = true;
			resend |= 1;
		} else if (resend & 2) {
			if (time_before(sp->resend_at, resend_at))
				resend_at = sp->resend_at;
		} else {
			resend_at = sp->resend_at;
			resend |= 2;
		}
	}

	rxrpc_set_resend(call, resend, resend_at);
	_leave(" = 0");
	return 0;

protocol_error:
	_leave(" = -EPROTO");
	return -EPROTO;
}

/*
 * discard hard-ACK'd packets from the Tx window
 */
static void rxrpc_rotate_tx_window(struct rxrpc_call *call, u32 hard)
{
	unsigned long _skb;
	int tail = call->acks_tail, old_tail;
	int win = CIRC_CNT(call->acks_head, tail, call->acks_winsz);

	_enter("{%u,%u},%u", call->acks_hard, win, hard);

	ASSERTCMP(hard - call->acks_hard, <=, win);

	while (call->acks_hard < hard) {
		smp_read_barrier_depends();
		_skb = call->acks_window[tail] & ~1;
		rxrpc_free_skb((struct sk_buff *) _skb);
		old_tail = tail;
		tail = (tail + 1) & (call->acks_winsz - 1);
		call->acks_tail = tail;
		if (call->acks_unacked == old_tail)
			call->acks_unacked = tail;
		call->acks_hard++;
	}

	wake_up(&call->tx_waitq);
}

/*
 * clear the Tx window in the event of a failure
 */
static void rxrpc_clear_tx_window(struct rxrpc_call *call)
{
	rxrpc_rotate_tx_window(call, atomic_read(&call->sequence));
}

/*
 * drain the out of sequence received packet queue into the packet Rx queue
 */
static int rxrpc_drain_rx_oos_queue(struct rxrpc_call *call)
{
	struct rxrpc_skb_priv *sp;
	struct sk_buff *skb;
	bool terminal;
	int ret;

	_enter("{%d,%d}", call->rx_data_post, call->rx_first_oos);

	spin_lock_bh(&call->lock);

	ret = -ECONNRESET;
	if (test_bit(RXRPC_CALL_RELEASED, &call->flags))
		goto socket_unavailable;

	skb = skb_dequeue(&call->rx_oos_queue);
	if (skb) {
		sp = rxrpc_skb(skb);

		_debug("drain OOS packet %d [%d]",
		       sp->hdr.seq, call->rx_first_oos);

		if (sp->hdr.seq != call->rx_first_oos) {
			skb_queue_head(&call->rx_oos_queue, skb);
			call->rx_first_oos = rxrpc_skb(skb)->hdr.seq;
			_debug("requeue %p {%u}", skb, call->rx_first_oos);
		} else {
			skb->mark = RXRPC_SKB_MARK_DATA;
			terminal = ((sp->hdr.flags & RXRPC_LAST_PACKET) &&
				!(sp->hdr.flags & RXRPC_CLIENT_INITIATED));
			ret = rxrpc_queue_rcv_skb(call, skb, true, terminal);
			BUG_ON(ret < 0);
			_debug("drain #%u", call->rx_data_post);
			call->rx_data_post++;

			/* find out what the next packet is */
			skb = skb_peek(&call->rx_oos_queue);
			if (skb)
				call->rx_first_oos = rxrpc_skb(skb)->hdr.seq;
			else
				call->rx_first_oos = 0;
			_debug("peek %p {%u}", skb, call->rx_first_oos);
		}
	}

	ret = 0;
socket_unavailable:
	spin_unlock_bh(&call->lock);
	_leave(" = %d", ret);
	return ret;
}

/*
 * insert an out of sequence packet into the buffer
 */
static void rxrpc_insert_oos_packet(struct rxrpc_call *call,
				    struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp, *psp;
	struct sk_buff *p;
	u32 seq;

	sp = rxrpc_skb(skb);
	seq = sp->hdr.seq;
	_enter(",,{%u}", seq);

	skb->destructor = rxrpc_packet_destructor;
	ASSERTCMP(sp->call, ==, NULL);
	sp->call = call;
	rxrpc_get_call(call);
	atomic_inc(&call->skb_count);

	/* insert into the buffer in sequence order */
	spin_lock_bh(&call->lock);

	skb_queue_walk(&call->rx_oos_queue, p) {
		psp = rxrpc_skb(p);
		if (psp->hdr.seq > seq) {
			_debug("insert oos #%u before #%u", seq, psp->hdr.seq);
			skb_insert(p, skb, &call->rx_oos_queue);
			goto inserted;
		}
	}

	_debug("append oos #%u", seq);
	skb_queue_tail(&call->rx_oos_queue, skb);
inserted:

	/* we might now have a new front to the queue */
	if (call->rx_first_oos == 0 || seq < call->rx_first_oos)
		call->rx_first_oos = seq;

	read_lock(&call->state_lock);
	if (call->state < RXRPC_CALL_COMPLETE &&
	    call->rx_data_post == call->rx_first_oos) {
		_debug("drain rx oos now");
		set_bit(RXRPC_CALL_EV_DRAIN_RX_OOS, &call->events);
	}
	read_unlock(&call->state_lock);

	spin_unlock_bh(&call->lock);
	_leave(" [stored #%u]", call->rx_first_oos);
}

/*
 * clear the Tx window on final ACK reception
 */
static void rxrpc_zap_tx_window(struct rxrpc_call *call)
{
	struct rxrpc_skb_priv *sp;
	struct sk_buff *skb;
	unsigned long _skb, *acks_window;
	u8 winsz = call->acks_winsz;
	int tail;

	acks_window = call->acks_window;
	call->acks_window = NULL;

	while (CIRC_CNT(call->acks_head, call->acks_tail, winsz) > 0) {
		tail = call->acks_tail;
		smp_read_barrier_depends();
		_skb = acks_window[tail] & ~1;
		smp_mb();
		call->acks_tail = (call->acks_tail + 1) & (winsz - 1);

		skb = (struct sk_buff *) _skb;
		sp = rxrpc_skb(skb);
		_debug("+++ clear Tx %u", sp->hdr.seq);
		rxrpc_free_skb(skb);
	}

	kfree(acks_window);
}

/*
 * process the extra information that may be appended to an ACK packet
 */
static void rxrpc_extract_ackinfo(struct rxrpc_call *call, struct sk_buff *skb,
				  unsigned int latest, int nAcks)
{
	struct rxrpc_ackinfo ackinfo;
	struct rxrpc_peer *peer;
	unsigned int mtu;

	if (skb_copy_bits(skb, nAcks + 3, &ackinfo, sizeof(ackinfo)) < 0) {
		_leave(" [no ackinfo]");
		return;
	}

	_proto("Rx ACK %%%u Info { rx=%u max=%u rwin=%u jm=%u }",
	       latest,
	       ntohl(ackinfo.rxMTU), ntohl(ackinfo.maxMTU),
	       ntohl(ackinfo.rwind), ntohl(ackinfo.jumbo_max));

	mtu = min(ntohl(ackinfo.rxMTU), ntohl(ackinfo.maxMTU));

	peer = call->conn->params.peer;
	if (mtu < peer->maxdata) {
		spin_lock_bh(&peer->lock);
		peer->maxdata = mtu;
		peer->mtu = mtu + peer->hdrsize;
		spin_unlock_bh(&peer->lock);
		_net("Net MTU %u (maxdata %u)", peer->mtu, peer->maxdata);
	}
}

/*
 * process packets in the reception queue
 */
static int rxrpc_process_rx_queue(struct rxrpc_call *call,
				  u32 *_abort_code)
{
	struct rxrpc_ackpacket ack;
	struct rxrpc_skb_priv *sp;
	struct sk_buff *skb;
	bool post_ACK;
	int latest;
	u32 hard, tx;

	_enter("");

process_further:
	skb = skb_dequeue(&call->rx_queue);
	if (!skb)
		return -EAGAIN;

	_net("deferred skb %p", skb);

	sp = rxrpc_skb(skb);

	_debug("process %s [st %d]", rxrpc_pkts[sp->hdr.type], call->state);

	post_ACK = false;

	switch (sp->hdr.type) {
		/* data packets that wind up here have been received out of
		 * order, need security processing or are jumbo packets */
	case RXRPC_PACKET_TYPE_DATA:
		_proto("OOSQ DATA %%%u { #%u }", sp->hdr.serial, sp->hdr.seq);

		/* secured packets must be verified and possibly decrypted */
		if (call->conn->security->verify_packet(call, skb,
							_abort_code) < 0)
			goto protocol_error;

		rxrpc_insert_oos_packet(call, skb);
		goto process_further;

		/* partial ACK to process */
	case RXRPC_PACKET_TYPE_ACK:
		if (skb_copy_bits(skb, 0, &ack, sizeof(ack)) < 0) {
			_debug("extraction failure");
			goto protocol_error;
		}
		if (!skb_pull(skb, sizeof(ack)))
			BUG();

		latest = sp->hdr.serial;
		hard = ntohl(ack.firstPacket);
		tx = atomic_read(&call->sequence);

		_proto("Rx ACK %%%u { m=%hu f=#%u p=#%u s=%%%u r=%s n=%u }",
		       latest,
		       ntohs(ack.maxSkew),
		       hard,
		       ntohl(ack.previousPacket),
		       ntohl(ack.serial),
		       rxrpc_acks(ack.reason),
		       ack.nAcks);

		rxrpc_extract_ackinfo(call, skb, latest, ack.nAcks);

		if (ack.reason == RXRPC_ACK_PING) {
			_proto("Rx ACK %%%u PING Request", latest);
			rxrpc_propose_ACK(call, RXRPC_ACK_PING_RESPONSE,
					  sp->hdr.serial, true);
		}

		/* discard any out-of-order or duplicate ACKs */
		if (latest - call->acks_latest <= 0) {
			_debug("discard ACK %d <= %d",
			       latest, call->acks_latest);
			goto discard;
		}
		call->acks_latest = latest;

		if (call->state != RXRPC_CALL_CLIENT_SEND_REQUEST &&
		    call->state != RXRPC_CALL_CLIENT_AWAIT_REPLY &&
		    call->state != RXRPC_CALL_SERVER_SEND_REPLY &&
		    call->state != RXRPC_CALL_SERVER_AWAIT_ACK)
			goto discard;

		_debug("Tx=%d H=%u S=%d", tx, call->acks_hard, call->state);

		if (hard > 0) {
			if (hard - 1 > tx) {
				_debug("hard-ACK'd packet %d not transmitted"
				       " (%d top)",
				       hard - 1, tx);
				goto protocol_error;
			}

			if ((call->state == RXRPC_CALL_CLIENT_AWAIT_REPLY ||
			     call->state == RXRPC_CALL_SERVER_AWAIT_ACK) &&
			    hard > tx) {
				call->acks_hard = tx;
				goto all_acked;
			}

			smp_rmb();
			rxrpc_rotate_tx_window(call, hard - 1);
		}

		if (ack.nAcks > 0) {
			if (hard - 1 + ack.nAcks > tx) {
				_debug("soft-ACK'd packet %d+%d not"
				       " transmitted (%d top)",
				       hard - 1, ack.nAcks, tx);
				goto protocol_error;
			}

			if (rxrpc_process_soft_ACKs(call, &ack, skb) < 0)
				goto protocol_error;
		}
		goto discard;

		/* complete ACK to process */
	case RXRPC_PACKET_TYPE_ACKALL:
		goto all_acked;

		/* abort and busy are handled elsewhere */
	case RXRPC_PACKET_TYPE_BUSY:
	case RXRPC_PACKET_TYPE_ABORT:
		BUG();

		/* connection level events - also handled elsewhere */
	case RXRPC_PACKET_TYPE_CHALLENGE:
	case RXRPC_PACKET_TYPE_RESPONSE:
	case RXRPC_PACKET_TYPE_DEBUG:
		BUG();
	}

	/* if we've had a hard ACK that covers all the packets we've sent, then
	 * that ends that phase of the operation */
all_acked:
	write_lock_bh(&call->state_lock);
	_debug("ack all %d", call->state);

	switch (call->state) {
	case RXRPC_CALL_CLIENT_AWAIT_REPLY:
		call->state = RXRPC_CALL_CLIENT_RECV_REPLY;
		break;
	case RXRPC_CALL_SERVER_AWAIT_ACK:
		_debug("srv complete");
		call->state = RXRPC_CALL_COMPLETE;
		post_ACK = true;
		break;
	case RXRPC_CALL_CLIENT_SEND_REQUEST:
	case RXRPC_CALL_SERVER_RECV_REQUEST:
		goto protocol_error_unlock; /* can't occur yet */
	default:
		write_unlock_bh(&call->state_lock);
		goto discard; /* assume packet left over from earlier phase */
	}

	write_unlock_bh(&call->state_lock);

	/* if all the packets we sent are hard-ACK'd, then we can discard
	 * whatever we've got left */
	_debug("clear Tx %d",
	       CIRC_CNT(call->acks_head, call->acks_tail, call->acks_winsz));

	del_timer_sync(&call->resend_timer);
	clear_bit(RXRPC_CALL_RUN_RTIMER, &call->flags);
	clear_bit(RXRPC_CALL_EV_RESEND_TIMER, &call->events);

	if (call->acks_window)
		rxrpc_zap_tx_window(call);

	if (post_ACK) {
		/* post the final ACK message for userspace to pick up */
		_debug("post ACK");
		skb->mark = RXRPC_SKB_MARK_FINAL_ACK;
		sp->call = call;
		rxrpc_get_call(call);
		atomic_inc(&call->skb_count);
		spin_lock_bh(&call->lock);
		if (rxrpc_queue_rcv_skb(call, skb, true, true) < 0)
			BUG();
		spin_unlock_bh(&call->lock);
		goto process_further;
	}

discard:
	rxrpc_free_skb(skb);
	goto process_further;

protocol_error_unlock:
	write_unlock_bh(&call->state_lock);
protocol_error:
	rxrpc_free_skb(skb);
	_leave(" = -EPROTO");
	return -EPROTO;
}

/*
 * post a message to the socket Rx queue for recvmsg() to pick up
 */
static int rxrpc_post_message(struct rxrpc_call *call, u32 mark, u32 error,
			      bool fatal)
{
	struct rxrpc_skb_priv *sp;
	struct sk_buff *skb;
	int ret;

	_enter("{%d,%lx},%u,%u,%d",
	       call->debug_id, call->flags, mark, error, fatal);

	/* remove timers and things for fatal messages */
	if (fatal) {
		del_timer_sync(&call->resend_timer);
		del_timer_sync(&call->ack_timer);
		clear_bit(RXRPC_CALL_RUN_RTIMER, &call->flags);
	}

	if (mark != RXRPC_SKB_MARK_NEW_CALL &&
	    !test_bit(RXRPC_CALL_HAS_USERID, &call->flags)) {
		_leave("[no userid]");
		return 0;
	}

	if (!test_bit(RXRPC_CALL_TERMINAL_MSG, &call->flags)) {
		skb = alloc_skb(0, GFP_NOFS);
		if (!skb)
			return -ENOMEM;

		rxrpc_new_skb(skb);

		skb->mark = mark;

		sp = rxrpc_skb(skb);
		memset(sp, 0, sizeof(*sp));
		sp->error = error;
		sp->call = call;
		rxrpc_get_call(call);
		atomic_inc(&call->skb_count);

		spin_lock_bh(&call->lock);
		ret = rxrpc_queue_rcv_skb(call, skb, true, fatal);
		spin_unlock_bh(&call->lock);
		BUG_ON(ret < 0);
	}

	return 0;
}

/*
 * handle background processing of incoming call packets and ACK / abort
 * generation
 */
void rxrpc_process_call(struct work_struct *work)
{
	struct rxrpc_call *call =
		container_of(work, struct rxrpc_call, processor);
	struct rxrpc_wire_header whdr;
	struct rxrpc_ackpacket ack;
	struct rxrpc_ackinfo ackinfo;
	struct msghdr msg;
	struct kvec iov[5];
	enum rxrpc_call_event genbit;
	unsigned long bits;
	__be32 data, pad;
	size_t len;
	int loop, nbit, ioc, ret, mtu;
	u32 serial, abort_code = RX_PROTOCOL_ERROR;
	u8 *acks = NULL;

	//printk("\n--------------------\n");
	_enter("{%d,%s,%lx} [%lu]",
	       call->debug_id, rxrpc_call_states[call->state], call->events,
	       (jiffies - call->creation_jif) / (HZ / 10));

	if (test_and_set_bit(RXRPC_CALL_PROC_BUSY, &call->flags)) {
		_debug("XXXXXXXXXXXXX RUNNING ON MULTIPLE CPUS XXXXXXXXXXXXX");
		return;
	}

	if (!call->conn)
		goto skip_msg_init;

	/* there's a good chance we're going to have to send a message, so set
	 * one up in advance */
	msg.msg_name	= &call->conn->params.peer->srx.transport;
	msg.msg_namelen	= call->conn->params.peer->srx.transport_len;
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= 0;

	whdr.epoch	= htonl(call->conn->proto.epoch);
	whdr.cid	= htonl(call->cid);
	whdr.callNumber	= htonl(call->call_id);
	whdr.seq	= 0;
	whdr.type	= RXRPC_PACKET_TYPE_ACK;
	whdr.flags	= call->conn->out_clientflag;
	whdr.userStatus	= 0;
	whdr.securityIndex = call->conn->security_ix;
	whdr._rsvd	= 0;
	whdr.serviceId	= htons(call->service_id);

	memset(iov, 0, sizeof(iov));
	iov[0].iov_base	= &whdr;
	iov[0].iov_len	= sizeof(whdr);
skip_msg_init:

	/* deal with events of a final nature */
	if (test_bit(RXRPC_CALL_EV_RCVD_ERROR, &call->events)) {
		enum rxrpc_skb_mark mark;
		int error;

		clear_bit(RXRPC_CALL_EV_CONN_ABORT, &call->events);
		clear_bit(RXRPC_CALL_EV_REJECT_BUSY, &call->events);
		clear_bit(RXRPC_CALL_EV_ABORT, &call->events);

		error = call->error_report;
		if (error < RXRPC_LOCAL_ERROR_OFFSET) {
			mark = RXRPC_SKB_MARK_NET_ERROR;
			_debug("post net error %d", error);
		} else {
			mark = RXRPC_SKB_MARK_LOCAL_ERROR;
			error -= RXRPC_LOCAL_ERROR_OFFSET;
			_debug("post net local error %d", error);
		}

		if (rxrpc_post_message(call, mark, error, true) < 0)
			goto no_mem;
		clear_bit(RXRPC_CALL_EV_RCVD_ERROR, &call->events);
		goto kill_ACKs;
	}

	if (test_bit(RXRPC_CALL_EV_CONN_ABORT, &call->events)) {
		ASSERTCMP(call->state, >, RXRPC_CALL_COMPLETE);

		clear_bit(RXRPC_CALL_EV_REJECT_BUSY, &call->events);
		clear_bit(RXRPC_CALL_EV_ABORT, &call->events);

		_debug("post conn abort");

		if (rxrpc_post_message(call, RXRPC_SKB_MARK_LOCAL_ERROR,
				       call->conn->error, true) < 0)
			goto no_mem;
		clear_bit(RXRPC_CALL_EV_CONN_ABORT, &call->events);
		goto kill_ACKs;
	}

	if (test_bit(RXRPC_CALL_EV_REJECT_BUSY, &call->events)) {
		whdr.type = RXRPC_PACKET_TYPE_BUSY;
		genbit = RXRPC_CALL_EV_REJECT_BUSY;
		goto send_message;
	}

	if (test_bit(RXRPC_CALL_EV_ABORT, &call->events)) {
		ASSERTCMP(call->state, >, RXRPC_CALL_COMPLETE);

		if (rxrpc_post_message(call, RXRPC_SKB_MARK_LOCAL_ERROR,
				       ECONNABORTED, true) < 0)
			goto no_mem;
		whdr.type = RXRPC_PACKET_TYPE_ABORT;
		data = htonl(call->local_abort);
		iov[1].iov_base = &data;
		iov[1].iov_len = sizeof(data);
		genbit = RXRPC_CALL_EV_ABORT;
		goto send_message;
	}

	if (test_bit(RXRPC_CALL_EV_ACK_FINAL, &call->events)) {
		genbit = RXRPC_CALL_EV_ACK_FINAL;

		ack.bufferSpace	= htons(8);
		ack.maxSkew	= 0;
		ack.serial	= 0;
		ack.reason	= RXRPC_ACK_IDLE;
		ack.nAcks	= 0;
		call->ackr_reason = 0;

		spin_lock_bh(&call->lock);
		ack.serial	= htonl(call->ackr_serial);
		ack.previousPacket = htonl(call->ackr_prev_seq);
		ack.firstPacket	= htonl(call->rx_data_eaten + 1);
		spin_unlock_bh(&call->lock);

		pad = 0;

		iov[1].iov_base = &ack;
		iov[1].iov_len	= sizeof(ack);
		iov[2].iov_base = &pad;
		iov[2].iov_len	= 3;
		iov[3].iov_base = &ackinfo;
		iov[3].iov_len	= sizeof(ackinfo);
		goto send_ACK;
	}

	if (call->events & ((1 << RXRPC_CALL_EV_RCVD_BUSY) |
			    (1 << RXRPC_CALL_EV_RCVD_ABORT))
	    ) {
		u32 mark;

		if (test_bit(RXRPC_CALL_EV_RCVD_ABORT, &call->events))
			mark = RXRPC_SKB_MARK_REMOTE_ABORT;
		else
			mark = RXRPC_SKB_MARK_BUSY;

		_debug("post abort/busy");
		rxrpc_clear_tx_window(call);
		if (rxrpc_post_message(call, mark, ECONNABORTED, true) < 0)
			goto no_mem;

		clear_bit(RXRPC_CALL_EV_RCVD_BUSY, &call->events);
		clear_bit(RXRPC_CALL_EV_RCVD_ABORT, &call->events);
		goto kill_ACKs;
	}

	if (test_and_clear_bit(RXRPC_CALL_EV_RCVD_ACKALL, &call->events)) {
		_debug("do implicit ackall");
		rxrpc_clear_tx_window(call);
	}

	if (test_bit(RXRPC_CALL_EV_LIFE_TIMER, &call->events)) {
		write_lock_bh(&call->state_lock);
		if (call->state <= RXRPC_CALL_COMPLETE) {
			call->state = RXRPC_CALL_LOCALLY_ABORTED;
			call->local_abort = RX_CALL_TIMEOUT;
			set_bit(RXRPC_CALL_EV_ABORT, &call->events);
		}
		write_unlock_bh(&call->state_lock);

		_debug("post timeout");
		if (rxrpc_post_message(call, RXRPC_SKB_MARK_LOCAL_ERROR,
				       ETIME, true) < 0)
			goto no_mem;

		clear_bit(RXRPC_CALL_EV_LIFE_TIMER, &call->events);
		goto kill_ACKs;
	}

	/* deal with assorted inbound messages */
	if (!skb_queue_empty(&call->rx_queue)) {
		switch (rxrpc_process_rx_queue(call, &abort_code)) {
		case 0:
		case -EAGAIN:
			break;
		case -ENOMEM:
			goto no_mem;
		case -EKEYEXPIRED:
		case -EKEYREJECTED:
		case -EPROTO:
			rxrpc_abort_call(call, abort_code);
			goto kill_ACKs;
		}
	}

	/* handle resending */
	if (test_and_clear_bit(RXRPC_CALL_EV_RESEND_TIMER, &call->events))
		rxrpc_resend_timer(call);
	if (test_and_clear_bit(RXRPC_CALL_EV_RESEND, &call->events))
		rxrpc_resend(call);

	/* consider sending an ordinary ACK */
	if (test_bit(RXRPC_CALL_EV_ACK, &call->events)) {
		_debug("send ACK: window: %d - %d { %lx }",
		       call->rx_data_eaten, call->ackr_win_top,
		       call->ackr_window[0]);

		if (call->state > RXRPC_CALL_SERVER_ACK_REQUEST &&
		    call->ackr_reason != RXRPC_ACK_PING_RESPONSE) {
			/* ACK by sending reply DATA packet in this state */
			clear_bit(RXRPC_CALL_EV_ACK, &call->events);
			goto maybe_reschedule;
		}

		genbit = RXRPC_CALL_EV_ACK;

		acks = kzalloc(call->ackr_win_top - call->rx_data_eaten,
			       GFP_NOFS);
		if (!acks)
			goto no_mem;

		//hdr.flags	= RXRPC_SLOW_START_OK;
		ack.bufferSpace	= htons(8);
		ack.maxSkew	= 0;

		spin_lock_bh(&call->lock);
		ack.reason	= call->ackr_reason;
		ack.serial	= htonl(call->ackr_serial);
		ack.previousPacket = htonl(call->ackr_prev_seq);
		ack.firstPacket = htonl(call->rx_data_eaten + 1);

		ack.nAcks = 0;
		for (loop = 0; loop < RXRPC_ACKR_WINDOW_ASZ; loop++) {
			nbit = loop * BITS_PER_LONG;
			for (bits = call->ackr_window[loop]; bits; bits >>= 1
			     ) {
				_debug("- l=%d n=%d b=%lx", loop, nbit, bits);
				if (bits & 1) {
					acks[nbit] = RXRPC_ACK_TYPE_ACK;
					ack.nAcks = nbit + 1;
				}
				nbit++;
			}
		}
		call->ackr_reason = 0;
		spin_unlock_bh(&call->lock);

		pad = 0;

		iov[1].iov_base = &ack;
		iov[1].iov_len	= sizeof(ack);
		iov[2].iov_base = acks;
		iov[2].iov_len	= ack.nAcks;
		iov[3].iov_base = &pad;
		iov[3].iov_len	= 3;
		iov[4].iov_base = &ackinfo;
		iov[4].iov_len	= sizeof(ackinfo);

		switch (ack.reason) {
		case RXRPC_ACK_REQUESTED:
		case RXRPC_ACK_DUPLICATE:
		case RXRPC_ACK_OUT_OF_SEQUENCE:
		case RXRPC_ACK_EXCEEDS_WINDOW:
		case RXRPC_ACK_NOSPACE:
		case RXRPC_ACK_PING:
		case RXRPC_ACK_PING_RESPONSE:
			goto send_ACK_with_skew;
		case RXRPC_ACK_DELAY:
		case RXRPC_ACK_IDLE:
			goto send_ACK;
		}
	}

	/* handle completion of security negotiations on an incoming
	 * connection */
	if (test_and_clear_bit(RXRPC_CALL_EV_SECURED, &call->events)) {
		_debug("secured");
		spin_lock_bh(&call->lock);

		if (call->state == RXRPC_CALL_SERVER_SECURING) {
			_debug("securing");
			write_lock(&call->socket->call_lock);
			if (!test_bit(RXRPC_CALL_RELEASED, &call->flags) &&
			    !test_bit(RXRPC_CALL_EV_RELEASE, &call->events)) {
				_debug("not released");
				call->state = RXRPC_CALL_SERVER_ACCEPTING;
				list_move_tail(&call->accept_link,
					       &call->socket->acceptq);
			}
			write_unlock(&call->socket->call_lock);
			read_lock(&call->state_lock);
			if (call->state < RXRPC_CALL_COMPLETE)
				set_bit(RXRPC_CALL_EV_POST_ACCEPT, &call->events);
			read_unlock(&call->state_lock);
		}

		spin_unlock_bh(&call->lock);
		if (!test_bit(RXRPC_CALL_EV_POST_ACCEPT, &call->events))
			goto maybe_reschedule;
	}

	/* post a notification of an acceptable connection to the app */
	if (test_bit(RXRPC_CALL_EV_POST_ACCEPT, &call->events)) {
		_debug("post accept");
		if (rxrpc_post_message(call, RXRPC_SKB_MARK_NEW_CALL,
				       0, false) < 0)
			goto no_mem;
		clear_bit(RXRPC_CALL_EV_POST_ACCEPT, &call->events);
		goto maybe_reschedule;
	}

	/* handle incoming call acceptance */
	if (test_and_clear_bit(RXRPC_CALL_EV_ACCEPTED, &call->events)) {
		_debug("accepted");
		ASSERTCMP(call->rx_data_post, ==, 0);
		call->rx_data_post = 1;
		read_lock_bh(&call->state_lock);
		if (call->state < RXRPC_CALL_COMPLETE)
			set_bit(RXRPC_CALL_EV_DRAIN_RX_OOS, &call->events);
		read_unlock_bh(&call->state_lock);
	}

	/* drain the out of sequence received packet queue into the packet Rx
	 * queue */
	if (test_and_clear_bit(RXRPC_CALL_EV_DRAIN_RX_OOS, &call->events)) {
		while (call->rx_data_post == call->rx_first_oos)
			if (rxrpc_drain_rx_oos_queue(call) < 0)
				break;
		goto maybe_reschedule;
	}

	if (test_bit(RXRPC_CALL_EV_RELEASE, &call->events)) {
		rxrpc_release_call(call);
		clear_bit(RXRPC_CALL_EV_RELEASE, &call->events);
	}

	/* other events may have been raised since we started checking */
	goto maybe_reschedule;

send_ACK_with_skew:
	ack.maxSkew = htons(atomic_read(&call->conn->hi_serial) -
			    ntohl(ack.serial));
send_ACK:
	mtu = call->conn->params.peer->if_mtu;
	mtu -= call->conn->params.peer->hdrsize;
	ackinfo.maxMTU	= htonl(mtu);
	ackinfo.rwind	= htonl(rxrpc_rx_window_size);

	/* permit the peer to send us jumbo packets if it wants to */
	ackinfo.rxMTU	= htonl(rxrpc_rx_mtu);
	ackinfo.jumbo_max = htonl(rxrpc_rx_jumbo_max);

	serial = atomic_inc_return(&call->conn->serial);
	whdr.serial = htonl(serial);
	_proto("Tx ACK %%%u { m=%hu f=#%u p=#%u s=%%%u r=%s n=%u }",
	       serial,
	       ntohs(ack.maxSkew),
	       ntohl(ack.firstPacket),
	       ntohl(ack.previousPacket),
	       ntohl(ack.serial),
	       rxrpc_acks(ack.reason),
	       ack.nAcks);

	del_timer_sync(&call->ack_timer);
	if (ack.nAcks > 0)
		set_bit(RXRPC_CALL_TX_SOFT_ACK, &call->flags);
	goto send_message_2;

send_message:
	_debug("send message");

	serial = atomic_inc_return(&call->conn->serial);
	whdr.serial = htonl(serial);
	_proto("Tx %s %%%u", rxrpc_pkts[whdr.type], serial);
send_message_2:

	len = iov[0].iov_len;
	ioc = 1;
	if (iov[4].iov_len) {
		ioc = 5;
		len += iov[4].iov_len;
		len += iov[3].iov_len;
		len += iov[2].iov_len;
		len += iov[1].iov_len;
	} else if (iov[3].iov_len) {
		ioc = 4;
		len += iov[3].iov_len;
		len += iov[2].iov_len;
		len += iov[1].iov_len;
	} else if (iov[2].iov_len) {
		ioc = 3;
		len += iov[2].iov_len;
		len += iov[1].iov_len;
	} else if (iov[1].iov_len) {
		ioc = 2;
		len += iov[1].iov_len;
	}

	ret = kernel_sendmsg(call->conn->params.local->socket,
			     &msg, iov, ioc, len);
	if (ret < 0) {
		_debug("sendmsg failed: %d", ret);
		read_lock_bh(&call->state_lock);
		if (call->state < RXRPC_CALL_DEAD)
			rxrpc_queue_call(call);
		read_unlock_bh(&call->state_lock);
		goto error;
	}

	switch (genbit) {
	case RXRPC_CALL_EV_ABORT:
		clear_bit(genbit, &call->events);
		clear_bit(RXRPC_CALL_EV_RCVD_ABORT, &call->events);
		goto kill_ACKs;

	case RXRPC_CALL_EV_ACK_FINAL:
		write_lock_bh(&call->state_lock);
		if (call->state == RXRPC_CALL_CLIENT_FINAL_ACK)
			call->state = RXRPC_CALL_COMPLETE;
		write_unlock_bh(&call->state_lock);
		goto kill_ACKs;

	default:
		clear_bit(genbit, &call->events);
		switch (call->state) {
		case RXRPC_CALL_CLIENT_AWAIT_REPLY:
		case RXRPC_CALL_CLIENT_RECV_REPLY:
		case RXRPC_CALL_SERVER_RECV_REQUEST:
		case RXRPC_CALL_SERVER_ACK_REQUEST:
			_debug("start ACK timer");
			rxrpc_propose_ACK(call, RXRPC_ACK_DELAY,
					  call->ackr_serial, false);
		default:
			break;
		}
		goto maybe_reschedule;
	}

kill_ACKs:
	del_timer_sync(&call->ack_timer);
	if (test_and_clear_bit(RXRPC_CALL_EV_ACK_FINAL, &call->events))
		rxrpc_put_call(call);
	clear_bit(RXRPC_CALL_EV_ACK, &call->events);

maybe_reschedule:
	if (call->events || !skb_queue_empty(&call->rx_queue)) {
		read_lock_bh(&call->state_lock);
		if (call->state < RXRPC_CALL_DEAD)
			rxrpc_queue_call(call);
		read_unlock_bh(&call->state_lock);
	}

	/* don't leave aborted connections on the accept queue */
	if (call->state >= RXRPC_CALL_COMPLETE &&
	    !list_empty(&call->accept_link)) {
		_debug("X unlinking once-pending call %p { e=%lx f=%lx c=%x }",
		       call, call->events, call->flags, call->conn->proto.cid);

		read_lock_bh(&call->state_lock);
		if (!test_bit(RXRPC_CALL_RELEASED, &call->flags) &&
		    !test_and_set_bit(RXRPC_CALL_EV_RELEASE, &call->events))
			rxrpc_queue_call(call);
		read_unlock_bh(&call->state_lock);
	}

error:
	clear_bit(RXRPC_CALL_PROC_BUSY, &call->flags);
	kfree(acks);

	/* because we don't want two CPUs both processing the work item for one
	 * call at the same time, we use a flag to note when it's busy; however
	 * this means there's a race between clearing the flag and setting the
	 * work pending bit and the work item being processed again */
	if (call->events && !work_pending(&call->processor)) {
		_debug("jumpstart %x", call->conn->proto.cid);
		rxrpc_queue_call(call);
	}

	_leave("");
	return;

no_mem:
	_debug("out of memory");
	goto maybe_reschedule;
}
