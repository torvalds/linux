/* call.c: Rx call routines
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <rxrpc/rxrpc.h>
#include <rxrpc/transport.h>
#include <rxrpc/peer.h>
#include <rxrpc/connection.h>
#include <rxrpc/call.h>
#include <rxrpc/message.h>
#include "internal.h"

__RXACCT_DECL(atomic_t rxrpc_call_count);
__RXACCT_DECL(atomic_t rxrpc_message_count);

LIST_HEAD(rxrpc_calls);
DECLARE_RWSEM(rxrpc_calls_sem);

unsigned rxrpc_call_rcv_timeout			= HZ/3;
static unsigned rxrpc_call_acks_timeout		= HZ/3;
static unsigned rxrpc_call_dfr_ack_timeout	= HZ/20;
static unsigned short rxrpc_call_max_resend	= HZ/10;

const char *rxrpc_call_states[] = {
	"COMPLETE",
	"ERROR",
	"SRVR_RCV_OPID",
	"SRVR_RCV_ARGS",
	"SRVR_GOT_ARGS",
	"SRVR_SND_REPLY",
	"SRVR_RCV_FINAL_ACK",
	"CLNT_SND_ARGS",
	"CLNT_RCV_REPLY",
	"CLNT_GOT_REPLY"
};

const char *rxrpc_call_error_states[] = {
	"NO_ERROR",
	"LOCAL_ABORT",
	"PEER_ABORT",
	"LOCAL_ERROR",
	"REMOTE_ERROR"
};

const char *rxrpc_pkts[] = {
	"?00",
	"data", "ack", "busy", "abort", "ackall", "chall", "resp", "debug",
	"?09", "?10", "?11", "?12", "?13", "?14", "?15"
};

static const char *rxrpc_acks[] = {
	"---", "REQ", "DUP", "SEQ", "WIN", "MEM", "PNG", "PNR", "DLY", "IDL",
	"-?-"
};

static const char _acktype[] = "NA-";

static void rxrpc_call_receive_packet(struct rxrpc_call *call);
static void rxrpc_call_receive_data_packet(struct rxrpc_call *call,
					   struct rxrpc_message *msg);
static void rxrpc_call_receive_ack_packet(struct rxrpc_call *call,
					  struct rxrpc_message *msg);
static void rxrpc_call_definitively_ACK(struct rxrpc_call *call,
					rxrpc_seq_t higest);
static void rxrpc_call_resend(struct rxrpc_call *call, rxrpc_seq_t highest);
static int __rxrpc_call_read_data(struct rxrpc_call *call);

static int rxrpc_call_record_ACK(struct rxrpc_call *call,
				 struct rxrpc_message *msg,
				 rxrpc_seq_t seq,
				 size_t count);

static int rxrpc_call_flush(struct rxrpc_call *call);

#define _state(call) \
	_debug("[[[ state %s ]]]", rxrpc_call_states[call->app_call_state]);

static void rxrpc_call_default_attn_func(struct rxrpc_call *call)
{
	wake_up(&call->waitq);
}

static void rxrpc_call_default_error_func(struct rxrpc_call *call)
{
	wake_up(&call->waitq);
}

static void rxrpc_call_default_aemap_func(struct rxrpc_call *call)
{
	switch (call->app_err_state) {
	case RXRPC_ESTATE_LOCAL_ABORT:
		call->app_abort_code = -call->app_errno;
	case RXRPC_ESTATE_PEER_ABORT:
		call->app_errno = -ECONNABORTED;
	default:
		break;
	}
}

static void __rxrpc_call_acks_timeout(unsigned long _call)
{
	struct rxrpc_call *call = (struct rxrpc_call *) _call;

	_debug("ACKS TIMEOUT %05lu", jiffies - call->cjif);

	call->flags |= RXRPC_CALL_ACKS_TIMO;
	rxrpc_krxiod_queue_call(call);
}

static void __rxrpc_call_rcv_timeout(unsigned long _call)
{
	struct rxrpc_call *call = (struct rxrpc_call *) _call;

	_debug("RCV TIMEOUT %05lu", jiffies - call->cjif);

	call->flags |= RXRPC_CALL_RCV_TIMO;
	rxrpc_krxiod_queue_call(call);
}

static void __rxrpc_call_ackr_timeout(unsigned long _call)
{
	struct rxrpc_call *call = (struct rxrpc_call *) _call;

	_debug("ACKR TIMEOUT %05lu",jiffies - call->cjif);

	call->flags |= RXRPC_CALL_ACKR_TIMO;
	rxrpc_krxiod_queue_call(call);
}

/*****************************************************************************/
/*
 * calculate a timeout based on an RTT value
 */
static inline unsigned long __rxrpc_rtt_based_timeout(struct rxrpc_call *call,
						      unsigned long val)
{
	unsigned long expiry = call->conn->peer->rtt / (1000000 / HZ);

	expiry += 10;
	if (expiry < HZ / 25)
		expiry = HZ / 25;
	if (expiry > HZ)
		expiry = HZ;

	_leave(" = %lu jiffies", expiry);
	return jiffies + expiry;
} /* end __rxrpc_rtt_based_timeout() */

/*****************************************************************************/
/*
 * create a new call record
 */
static inline int __rxrpc_create_call(struct rxrpc_connection *conn,
				      struct rxrpc_call **_call)
{
	struct rxrpc_call *call;

	_enter("%p", conn);

	/* allocate and initialise a call record */
	call = (struct rxrpc_call *) get_zeroed_page(GFP_KERNEL);
	if (!call) {
		_leave(" ENOMEM");
		return -ENOMEM;
	}

	atomic_set(&call->usage, 1);

	init_waitqueue_head(&call->waitq);
	spin_lock_init(&call->lock);
	INIT_LIST_HEAD(&call->link);
	INIT_LIST_HEAD(&call->acks_pendq);
	INIT_LIST_HEAD(&call->rcv_receiveq);
	INIT_LIST_HEAD(&call->rcv_krxiodq_lk);
	INIT_LIST_HEAD(&call->app_readyq);
	INIT_LIST_HEAD(&call->app_unreadyq);
	INIT_LIST_HEAD(&call->app_link);
	INIT_LIST_HEAD(&call->app_attn_link);

	init_timer(&call->acks_timeout);
	call->acks_timeout.data = (unsigned long) call;
	call->acks_timeout.function = __rxrpc_call_acks_timeout;

	init_timer(&call->rcv_timeout);
	call->rcv_timeout.data = (unsigned long) call;
	call->rcv_timeout.function = __rxrpc_call_rcv_timeout;

	init_timer(&call->ackr_dfr_timo);
	call->ackr_dfr_timo.data = (unsigned long) call;
	call->ackr_dfr_timo.function = __rxrpc_call_ackr_timeout;

	call->conn = conn;
	call->ackr_win_bot = 1;
	call->ackr_win_top = call->ackr_win_bot + RXRPC_CALL_ACK_WINDOW_SIZE - 1;
	call->ackr_prev_seq = 0;
	call->app_mark = RXRPC_APP_MARK_EOF;
	call->app_attn_func = rxrpc_call_default_attn_func;
	call->app_error_func = rxrpc_call_default_error_func;
	call->app_aemap_func = rxrpc_call_default_aemap_func;
	call->app_scr_alloc = call->app_scratch;

	call->cjif = jiffies;

	_leave(" = 0 (%p)", call);

	*_call = call;

	return 0;
} /* end __rxrpc_create_call() */

/*****************************************************************************/
/*
 * create a new call record for outgoing calls
 */
int rxrpc_create_call(struct rxrpc_connection *conn,
		      rxrpc_call_attn_func_t attn,
		      rxrpc_call_error_func_t error,
		      rxrpc_call_aemap_func_t aemap,
		      struct rxrpc_call **_call)
{
	DECLARE_WAITQUEUE(myself, current);

	struct rxrpc_call *call;
	int ret, cix, loop;

	_enter("%p", conn);

	/* allocate and initialise a call record */
	ret = __rxrpc_create_call(conn, &call);
	if (ret < 0) {
		_leave(" = %d", ret);
		return ret;
	}

	call->app_call_state = RXRPC_CSTATE_CLNT_SND_ARGS;
	if (attn)
		call->app_attn_func = attn;
	if (error)
		call->app_error_func = error;
	if (aemap)
		call->app_aemap_func = aemap;

	_state(call);

	spin_lock(&conn->lock);
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&conn->chanwait, &myself);

 try_again:
	/* try to find an unused channel */
	for (cix = 0; cix < 4; cix++)
		if (!conn->channels[cix])
			goto obtained_chan;

	/* no free channels - wait for one to become available */
	ret = -EINTR;
	if (signal_pending(current))
		goto error_unwait;

	spin_unlock(&conn->lock);

	schedule();
	set_current_state(TASK_INTERRUPTIBLE);

	spin_lock(&conn->lock);
	goto try_again;

	/* got a channel - now attach to the connection */
 obtained_chan:
	remove_wait_queue(&conn->chanwait, &myself);
	set_current_state(TASK_RUNNING);

	/* concoct a unique call number */
 next_callid:
	call->call_id = htonl(++conn->call_counter);
	for (loop = 0; loop < 4; loop++)
		if (conn->channels[loop] &&
		    conn->channels[loop]->call_id == call->call_id)
			goto next_callid;

	rxrpc_get_connection(conn);
	conn->channels[cix] = call; /* assign _after_ done callid check loop */
	do_gettimeofday(&conn->atime);
	call->chan_ix = htonl(cix);

	spin_unlock(&conn->lock);

	down_write(&rxrpc_calls_sem);
	list_add_tail(&call->call_link, &rxrpc_calls);
	up_write(&rxrpc_calls_sem);

	__RXACCT(atomic_inc(&rxrpc_call_count));
	*_call = call;

	_leave(" = 0 (call=%p cix=%u)", call, cix);
	return 0;

 error_unwait:
	remove_wait_queue(&conn->chanwait, &myself);
	set_current_state(TASK_RUNNING);
	spin_unlock(&conn->lock);

	free_page((unsigned long) call);
	_leave(" = %d", ret);
	return ret;
} /* end rxrpc_create_call() */

/*****************************************************************************/
/*
 * create a new call record for incoming calls
 */
int rxrpc_incoming_call(struct rxrpc_connection *conn,
			struct rxrpc_message *msg,
			struct rxrpc_call **_call)
{
	struct rxrpc_call *call;
	unsigned cix;
	int ret;

	cix = ntohl(msg->hdr.cid) & RXRPC_CHANNELMASK;

	_enter("%p,%u,%u", conn, ntohl(msg->hdr.callNumber), cix);

	/* allocate and initialise a call record */
	ret = __rxrpc_create_call(conn, &call);
	if (ret < 0) {
		_leave(" = %d", ret);
		return ret;
	}

	call->pkt_rcv_count = 1;
	call->app_call_state = RXRPC_CSTATE_SRVR_RCV_OPID;
	call->app_mark = sizeof(uint32_t);

	_state(call);

	/* attach to the connection */
	ret = -EBUSY;
	call->chan_ix = htonl(cix);
	call->call_id = msg->hdr.callNumber;

	spin_lock(&conn->lock);

	if (!conn->channels[cix] ||
	    conn->channels[cix]->app_call_state == RXRPC_CSTATE_COMPLETE ||
	    conn->channels[cix]->app_call_state == RXRPC_CSTATE_ERROR
	    ) {
		conn->channels[cix] = call;
		rxrpc_get_connection(conn);
		ret = 0;
	}

	spin_unlock(&conn->lock);

	if (ret < 0) {
		free_page((unsigned long) call);
		call = NULL;
	}

	if (ret == 0) {
		down_write(&rxrpc_calls_sem);
		list_add_tail(&call->call_link, &rxrpc_calls);
		up_write(&rxrpc_calls_sem);
		__RXACCT(atomic_inc(&rxrpc_call_count));
		*_call = call;
	}

	_leave(" = %d [%p]", ret, call);
	return ret;
} /* end rxrpc_incoming_call() */

/*****************************************************************************/
/*
 * free a call record
 */
void rxrpc_put_call(struct rxrpc_call *call)
{
	struct rxrpc_connection *conn = call->conn;
	struct rxrpc_message *msg;

	_enter("%p{u=%d}",call,atomic_read(&call->usage));

	/* sanity check */
	if (atomic_read(&call->usage) <= 0)
		BUG();

	/* to prevent a race, the decrement and the de-list must be effectively
	 * atomic */
	spin_lock(&conn->lock);
	if (likely(!atomic_dec_and_test(&call->usage))) {
		spin_unlock(&conn->lock);
		_leave("");
		return;
	}

	if (conn->channels[ntohl(call->chan_ix)] == call)
		conn->channels[ntohl(call->chan_ix)] = NULL;

	spin_unlock(&conn->lock);

	wake_up(&conn->chanwait);

	rxrpc_put_connection(conn);

	/* clear the timers and dequeue from krxiod */
	del_timer_sync(&call->acks_timeout);
	del_timer_sync(&call->rcv_timeout);
	del_timer_sync(&call->ackr_dfr_timo);

	rxrpc_krxiod_dequeue_call(call);

	/* clean up the contents of the struct */
	if (call->snd_nextmsg)
		rxrpc_put_message(call->snd_nextmsg);

	if (call->snd_ping)
		rxrpc_put_message(call->snd_ping);

	while (!list_empty(&call->acks_pendq)) {
		msg = list_entry(call->acks_pendq.next,
				 struct rxrpc_message, link);
		list_del(&msg->link);
		rxrpc_put_message(msg);
	}

	while (!list_empty(&call->rcv_receiveq)) {
		msg = list_entry(call->rcv_receiveq.next,
				 struct rxrpc_message, link);
		list_del(&msg->link);
		rxrpc_put_message(msg);
	}

	while (!list_empty(&call->app_readyq)) {
		msg = list_entry(call->app_readyq.next,
				 struct rxrpc_message, link);
		list_del(&msg->link);
		rxrpc_put_message(msg);
	}

	while (!list_empty(&call->app_unreadyq)) {
		msg = list_entry(call->app_unreadyq.next,
				 struct rxrpc_message, link);
		list_del(&msg->link);
		rxrpc_put_message(msg);
	}

	module_put(call->owner);

	down_write(&rxrpc_calls_sem);
	list_del(&call->call_link);
	up_write(&rxrpc_calls_sem);

	__RXACCT(atomic_dec(&rxrpc_call_count));
	free_page((unsigned long) call);

	_leave(" [destroyed]");
} /* end rxrpc_put_call() */

/*****************************************************************************/
/*
 * actually generate a normal ACK
 */
static inline int __rxrpc_call_gen_normal_ACK(struct rxrpc_call *call,
					      rxrpc_seq_t seq)
{
	struct rxrpc_message *msg;
	struct kvec diov[3];
	__be32 aux[4];
	int delta, ret;

	/* ACKs default to DELAY */
	if (!call->ackr.reason)
		call->ackr.reason = RXRPC_ACK_DELAY;

	_proto("Rx %05lu Sending ACK { m=%hu f=#%u p=#%u s=%%%u r=%s n=%u }",
	       jiffies - call->cjif,
	       ntohs(call->ackr.maxSkew),
	       ntohl(call->ackr.firstPacket),
	       ntohl(call->ackr.previousPacket),
	       ntohl(call->ackr.serial),
	       rxrpc_acks[call->ackr.reason],
	       call->ackr.nAcks);

	aux[0] = htonl(call->conn->peer->if_mtu);	/* interface MTU */
	aux[1] = htonl(1444);				/* max MTU */
	aux[2] = htonl(16);				/* rwind */
	aux[3] = htonl(4);				/* max packets */

	diov[0].iov_len  = sizeof(struct rxrpc_ackpacket);
	diov[0].iov_base = &call->ackr;
	diov[1].iov_len  = call->ackr_pend_cnt + 3;
	diov[1].iov_base = call->ackr_array;
	diov[2].iov_len  = sizeof(aux);
	diov[2].iov_base = &aux;

	/* build and send the message */
	ret = rxrpc_conn_newmsg(call->conn,call, RXRPC_PACKET_TYPE_ACK,
				3, diov, GFP_KERNEL, &msg);
	if (ret < 0)
		goto out;

	msg->seq = seq;
	msg->hdr.seq = htonl(seq);
	msg->hdr.flags |= RXRPC_SLOW_START_OK;

	ret = rxrpc_conn_sendmsg(call->conn, msg);
	rxrpc_put_message(msg);
	if (ret < 0)
		goto out;
	call->pkt_snd_count++;

	/* count how many actual ACKs there were at the front */
	for (delta = 0; delta < call->ackr_pend_cnt; delta++)
		if (call->ackr_array[delta] != RXRPC_ACK_TYPE_ACK)
			break;

	call->ackr_pend_cnt -= delta; /* all ACK'd to this point */

	/* crank the ACK window around */
	if (delta == 0) {
		/* un-ACK'd window */
	}
	else if (delta < RXRPC_CALL_ACK_WINDOW_SIZE) {
		/* partially ACK'd window
		 * - shuffle down to avoid losing out-of-sequence packets
		 */
		call->ackr_win_bot += delta;
		call->ackr_win_top += delta;

		memmove(&call->ackr_array[0],
			&call->ackr_array[delta],
			call->ackr_pend_cnt);

		memset(&call->ackr_array[call->ackr_pend_cnt],
		       RXRPC_ACK_TYPE_NACK,
		       sizeof(call->ackr_array) - call->ackr_pend_cnt);
	}
	else {
		/* fully ACK'd window
		 * - just clear the whole thing
		 */
		memset(&call->ackr_array,
		       RXRPC_ACK_TYPE_NACK,
		       sizeof(call->ackr_array));
	}

	/* clear this ACK */
	memset(&call->ackr, 0, sizeof(call->ackr));

 out:
	if (!call->app_call_state)
		printk("___ STATE 0 ___\n");
	return ret;
} /* end __rxrpc_call_gen_normal_ACK() */

/*****************************************************************************/
/*
 * note the reception of a packet in the call's ACK records and generate an
 * appropriate ACK packet if necessary
 * - returns 0 if packet should be processed, 1 if packet should be ignored
 *   and -ve on an error
 */
static int rxrpc_call_generate_ACK(struct rxrpc_call *call,
				   struct rxrpc_header *hdr,
				   struct rxrpc_ackpacket *ack)
{
	struct rxrpc_message *msg;
	rxrpc_seq_t seq;
	unsigned offset;
	int ret = 0, err;
	u8 special_ACK, do_ACK, force;

	_enter("%p,%p { seq=%d tp=%d fl=%02x }",
	       call, hdr, ntohl(hdr->seq), hdr->type, hdr->flags);

	seq = ntohl(hdr->seq);
	offset = seq - call->ackr_win_bot;
	do_ACK = RXRPC_ACK_DELAY;
	special_ACK = 0;
	force = (seq == 1);

	if (call->ackr_high_seq < seq)
		call->ackr_high_seq = seq;

	/* deal with generation of obvious special ACKs first */
	if (ack && ack->reason == RXRPC_ACK_PING) {
		special_ACK = RXRPC_ACK_PING_RESPONSE;
		ret = 1;
		goto gen_ACK;
	}

	if (seq < call->ackr_win_bot) {
		special_ACK = RXRPC_ACK_DUPLICATE;
		ret = 1;
		goto gen_ACK;
	}

	if (seq >= call->ackr_win_top) {
		special_ACK = RXRPC_ACK_EXCEEDS_WINDOW;
		ret = 1;
		goto gen_ACK;
	}

	if (call->ackr_array[offset] != RXRPC_ACK_TYPE_NACK) {
		special_ACK = RXRPC_ACK_DUPLICATE;
		ret = 1;
		goto gen_ACK;
	}

	/* okay... it's a normal data packet inside the ACK window */
	call->ackr_array[offset] = RXRPC_ACK_TYPE_ACK;

	if (offset < call->ackr_pend_cnt) {
	}
	else if (offset > call->ackr_pend_cnt) {
		do_ACK = RXRPC_ACK_OUT_OF_SEQUENCE;
		call->ackr_pend_cnt = offset;
		goto gen_ACK;
	}

	if (hdr->flags & RXRPC_REQUEST_ACK) {
		do_ACK = RXRPC_ACK_REQUESTED;
	}

	/* generate an ACK on the final packet of a reply just received */
	if (hdr->flags & RXRPC_LAST_PACKET) {
		if (call->conn->out_clientflag)
			force = 1;
	}
	else if (!(hdr->flags & RXRPC_MORE_PACKETS)) {
		do_ACK = RXRPC_ACK_REQUESTED;
	}

	/* re-ACK packets previously received out-of-order */
	for (offset++; offset < RXRPC_CALL_ACK_WINDOW_SIZE; offset++)
		if (call->ackr_array[offset] != RXRPC_ACK_TYPE_ACK)
			break;

	call->ackr_pend_cnt = offset;

	/* generate an ACK if we fill up the window */
	if (call->ackr_pend_cnt >= RXRPC_CALL_ACK_WINDOW_SIZE)
		force = 1;

 gen_ACK:
	_debug("%05lu ACKs pend=%u norm=%s special=%s%s",
	       jiffies - call->cjif,
	       call->ackr_pend_cnt,
	       rxrpc_acks[do_ACK],
	       rxrpc_acks[special_ACK],
	       force ? " immediate" :
	       do_ACK == RXRPC_ACK_REQUESTED ? " merge-req" :
	       hdr->flags & RXRPC_LAST_PACKET ? " finalise" :
	       " defer"
	       );

	/* send any pending normal ACKs if need be */
	if (call->ackr_pend_cnt > 0) {
		/* fill out the appropriate form */
		call->ackr.bufferSpace	= htons(RXRPC_CALL_ACK_WINDOW_SIZE);
		call->ackr.maxSkew	= htons(min(call->ackr_high_seq - seq,
						    65535U));
		call->ackr.firstPacket	= htonl(call->ackr_win_bot);
		call->ackr.previousPacket = call->ackr_prev_seq;
		call->ackr.serial	= hdr->serial;
		call->ackr.nAcks	= call->ackr_pend_cnt;

		if (do_ACK == RXRPC_ACK_REQUESTED)
			call->ackr.reason = do_ACK;

		/* generate the ACK immediately if necessary */
		if (special_ACK || force) {
			err = __rxrpc_call_gen_normal_ACK(
				call, do_ACK == RXRPC_ACK_DELAY ? 0 : seq);
			if (err < 0) {
				ret = err;
				goto out;
			}
		}
	}

	if (call->ackr.reason == RXRPC_ACK_REQUESTED)
		call->ackr_dfr_seq = seq;

	/* start the ACK timer if not running if there are any pending deferred
	 * ACKs */
	if (call->ackr_pend_cnt > 0 &&
	    call->ackr.reason != RXRPC_ACK_REQUESTED &&
	    !timer_pending(&call->ackr_dfr_timo)
	    ) {
		unsigned long timo;

		timo = rxrpc_call_dfr_ack_timeout + jiffies;

		_debug("START ACKR TIMER for cj=%lu", timo - call->cjif);

		spin_lock(&call->lock);
		mod_timer(&call->ackr_dfr_timo, timo);
		spin_unlock(&call->lock);
	}
	else if ((call->ackr_pend_cnt == 0 ||
		  call->ackr.reason == RXRPC_ACK_REQUESTED) &&
		 timer_pending(&call->ackr_dfr_timo)
		 ) {
		/* stop timer if no pending ACKs */
		_debug("CLEAR ACKR TIMER");
		del_timer_sync(&call->ackr_dfr_timo);
	}

	/* send a special ACK if one is required */
	if (special_ACK) {
		struct rxrpc_ackpacket ack;
		struct kvec diov[2];
		uint8_t acks[1] = { RXRPC_ACK_TYPE_ACK };

		/* fill out the appropriate form */
		ack.bufferSpace	= htons(RXRPC_CALL_ACK_WINDOW_SIZE);
		ack.maxSkew	= htons(min(call->ackr_high_seq - seq,
					    65535U));
		ack.firstPacket	= htonl(call->ackr_win_bot);
		ack.previousPacket = call->ackr_prev_seq;
		ack.serial	= hdr->serial;
		ack.reason	= special_ACK;
		ack.nAcks	= 0;

		_proto("Rx Sending s-ACK"
		       " { m=%hu f=#%u p=#%u s=%%%u r=%s n=%u }",
		       ntohs(ack.maxSkew),
		       ntohl(ack.firstPacket),
		       ntohl(ack.previousPacket),
		       ntohl(ack.serial),
		       rxrpc_acks[ack.reason],
		       ack.nAcks);

		diov[0].iov_len  = sizeof(struct rxrpc_ackpacket);
		diov[0].iov_base = &ack;
		diov[1].iov_len  = sizeof(acks);
		diov[1].iov_base = acks;

		/* build and send the message */
		err = rxrpc_conn_newmsg(call->conn,call, RXRPC_PACKET_TYPE_ACK,
					hdr->seq ? 2 : 1, diov,
					GFP_KERNEL,
					&msg);
		if (err < 0) {
			ret = err;
			goto out;
		}

		msg->seq = seq;
		msg->hdr.seq = htonl(seq);
		msg->hdr.flags |= RXRPC_SLOW_START_OK;

		err = rxrpc_conn_sendmsg(call->conn, msg);
		rxrpc_put_message(msg);
		if (err < 0) {
			ret = err;
			goto out;
		}
		call->pkt_snd_count++;
	}

 out:
	if (hdr->seq)
		call->ackr_prev_seq = hdr->seq;

	_leave(" = %d", ret);
	return ret;
} /* end rxrpc_call_generate_ACK() */

/*****************************************************************************/
/*
 * handle work to be done on a call
 * - includes packet reception and timeout processing
 */
void rxrpc_call_do_stuff(struct rxrpc_call *call)
{
	_enter("%p{flags=%lx}", call, call->flags);

	/* handle packet reception */
	if (call->flags & RXRPC_CALL_RCV_PKT) {
		_debug("- receive packet");
		call->flags &= ~RXRPC_CALL_RCV_PKT;
		rxrpc_call_receive_packet(call);
	}

	/* handle overdue ACKs */
	if (call->flags & RXRPC_CALL_ACKS_TIMO) {
		_debug("- overdue ACK timeout");
		call->flags &= ~RXRPC_CALL_ACKS_TIMO;
		rxrpc_call_resend(call, call->snd_seq_count);
	}

	/* handle lack of reception */
	if (call->flags & RXRPC_CALL_RCV_TIMO) {
		_debug("- reception timeout");
		call->flags &= ~RXRPC_CALL_RCV_TIMO;
		rxrpc_call_abort(call, -EIO);
	}

	/* handle deferred ACKs */
	if (call->flags & RXRPC_CALL_ACKR_TIMO ||
	    (call->ackr.nAcks > 0 && call->ackr.reason == RXRPC_ACK_REQUESTED)
	    ) {
		_debug("- deferred ACK timeout: cj=%05lu r=%s n=%u",
		       jiffies - call->cjif,
		       rxrpc_acks[call->ackr.reason],
		       call->ackr.nAcks);

		call->flags &= ~RXRPC_CALL_ACKR_TIMO;

		if (call->ackr.nAcks > 0 &&
		    call->app_call_state != RXRPC_CSTATE_ERROR) {
			/* generate ACK */
			__rxrpc_call_gen_normal_ACK(call, call->ackr_dfr_seq);
			call->ackr_dfr_seq = 0;
		}
	}

	_leave("");

} /* end rxrpc_call_do_stuff() */

/*****************************************************************************/
/*
 * send an abort message at call or connection level
 * - must be called with call->lock held
 * - the supplied error code is sent as the packet data
 */
static int __rxrpc_call_abort(struct rxrpc_call *call, int errno)
{
	struct rxrpc_connection *conn = call->conn;
	struct rxrpc_message *msg;
	struct kvec diov[1];
	int ret;
	__be32 _error;

	_enter("%p{%08x},%p{%d},%d",
	       conn, ntohl(conn->conn_id), call, ntohl(call->call_id), errno);

	/* if this call is already aborted, then just wake up any waiters */
	if (call->app_call_state == RXRPC_CSTATE_ERROR) {
		spin_unlock(&call->lock);
		call->app_error_func(call);
		_leave(" = 0");
		return 0;
	}

	rxrpc_get_call(call);

	/* change the state _with_ the lock still held */
	call->app_call_state	= RXRPC_CSTATE_ERROR;
	call->app_err_state	= RXRPC_ESTATE_LOCAL_ABORT;
	call->app_errno		= errno;
	call->app_mark		= RXRPC_APP_MARK_EOF;
	call->app_read_buf	= NULL;
	call->app_async_read	= 0;

	_state(call);

	/* ask the app to translate the error code */
	call->app_aemap_func(call);

	spin_unlock(&call->lock);

	/* flush any outstanding ACKs */
	del_timer_sync(&call->acks_timeout);
	del_timer_sync(&call->rcv_timeout);
	del_timer_sync(&call->ackr_dfr_timo);

	if (rxrpc_call_is_ack_pending(call))
		__rxrpc_call_gen_normal_ACK(call, 0);

	/* send the abort packet only if we actually traded some other
	 * packets */
	ret = 0;
	if (call->pkt_snd_count || call->pkt_rcv_count) {
		/* actually send the abort */
		_proto("Rx Sending Call ABORT { data=%d }",
		       call->app_abort_code);

		_error = htonl(call->app_abort_code);

		diov[0].iov_len  = sizeof(_error);
		diov[0].iov_base = &_error;

		ret = rxrpc_conn_newmsg(conn, call, RXRPC_PACKET_TYPE_ABORT,
					1, diov, GFP_KERNEL, &msg);
		if (ret == 0) {
			ret = rxrpc_conn_sendmsg(conn, msg);
			rxrpc_put_message(msg);
		}
	}

	/* tell the app layer to let go */
	call->app_error_func(call);

	rxrpc_put_call(call);

	_leave(" = %d", ret);
	return ret;
} /* end __rxrpc_call_abort() */

/*****************************************************************************/
/*
 * send an abort message at call or connection level
 * - the supplied error code is sent as the packet data
 */
int rxrpc_call_abort(struct rxrpc_call *call, int error)
{
	spin_lock(&call->lock);

	return __rxrpc_call_abort(call, error);

} /* end rxrpc_call_abort() */

/*****************************************************************************/
/*
 * process packets waiting for this call
 */
static void rxrpc_call_receive_packet(struct rxrpc_call *call)
{
	struct rxrpc_message *msg;
	struct list_head *_p;

	_enter("%p", call);

	rxrpc_get_call(call); /* must not go away too soon if aborted by
			       * app-layer */

	while (!list_empty(&call->rcv_receiveq)) {
		/* try to get next packet */
		_p = NULL;
		spin_lock(&call->lock);
		if (!list_empty(&call->rcv_receiveq)) {
			_p = call->rcv_receiveq.next;
			list_del_init(_p);
		}
		spin_unlock(&call->lock);

		if (!_p)
			break;

		msg = list_entry(_p, struct rxrpc_message, link);

		_proto("Rx %05lu Received %s packet (%%%u,#%u,%c%c%c%c%c)",
		       jiffies - call->cjif,
		       rxrpc_pkts[msg->hdr.type],
		       ntohl(msg->hdr.serial),
		       msg->seq,
		       msg->hdr.flags & RXRPC_JUMBO_PACKET	? 'j' : '-',
		       msg->hdr.flags & RXRPC_MORE_PACKETS	? 'm' : '-',
		       msg->hdr.flags & RXRPC_LAST_PACKET	? 'l' : '-',
		       msg->hdr.flags & RXRPC_REQUEST_ACK	? 'r' : '-',
		       msg->hdr.flags & RXRPC_CLIENT_INITIATED	? 'C' : 'S'
		       );

		switch (msg->hdr.type) {
			/* deal with data packets */
		case RXRPC_PACKET_TYPE_DATA:
			/* ACK the packet if necessary */
			switch (rxrpc_call_generate_ACK(call, &msg->hdr,
							NULL)) {
			case 0: /* useful packet */
				rxrpc_call_receive_data_packet(call, msg);
				break;
			case 1: /* duplicate or out-of-window packet */
				break;
			default:
				rxrpc_put_message(msg);
				goto out;
			}
			break;

			/* deal with ACK packets */
		case RXRPC_PACKET_TYPE_ACK:
			rxrpc_call_receive_ack_packet(call, msg);
			break;

			/* deal with abort packets */
		case RXRPC_PACKET_TYPE_ABORT: {
			__be32 _dbuf, *dp;

			dp = skb_header_pointer(msg->pkt, msg->offset,
						sizeof(_dbuf), &_dbuf);
			if (dp == NULL)
				printk("Rx Received short ABORT packet\n");

			_proto("Rx Received Call ABORT { data=%d }",
			       (dp ? ntohl(*dp) : 0));

			spin_lock(&call->lock);
			call->app_call_state	= RXRPC_CSTATE_ERROR;
			call->app_err_state	= RXRPC_ESTATE_PEER_ABORT;
			call->app_abort_code	= (dp ? ntohl(*dp) : 0);
			call->app_errno		= -ECONNABORTED;
			call->app_mark		= RXRPC_APP_MARK_EOF;
			call->app_read_buf	= NULL;
			call->app_async_read	= 0;

			/* ask the app to translate the error code */
			call->app_aemap_func(call);
			_state(call);
			spin_unlock(&call->lock);
			call->app_error_func(call);
			break;
		}
		default:
			/* deal with other packet types */
			_proto("Rx Unsupported packet type %u (#%u)",
			       msg->hdr.type, msg->seq);
			break;
		}

		rxrpc_put_message(msg);
	}

 out:
	rxrpc_put_call(call);
	_leave("");
} /* end rxrpc_call_receive_packet() */

/*****************************************************************************/
/*
 * process next data packet
 * - as the next data packet arrives:
 *   - it is queued on app_readyq _if_ it is the next one expected
 *     (app_ready_seq+1)
 *   - it is queued on app_unreadyq _if_ it is not the next one expected
 *   - if a packet placed on app_readyq completely fills a hole leading up to
 *     the first packet on app_unreadyq, then packets now in sequence are
 *     tranferred to app_readyq
 * - the application layer can only see packets on app_readyq
 *   (app_ready_qty bytes)
 * - the application layer is prodded every time a new packet arrives
 */
static void rxrpc_call_receive_data_packet(struct rxrpc_call *call,
					   struct rxrpc_message *msg)
{
	const struct rxrpc_operation *optbl, *op;
	struct rxrpc_message *pmsg;
	struct list_head *_p;
	int ret, lo, hi, rmtimo;
	__be32 opid;

	_enter("%p{%u},%p{%u}", call, ntohl(call->call_id), msg, msg->seq);

	rxrpc_get_message(msg);

	/* add to the unready queue if we'd have to create a hole in the ready
	 * queue otherwise */
	if (msg->seq != call->app_ready_seq + 1) {
		_debug("Call add packet %d to unreadyq", msg->seq);

		/* insert in seq order */
		list_for_each(_p, &call->app_unreadyq) {
			pmsg = list_entry(_p, struct rxrpc_message, link);
			if (pmsg->seq > msg->seq)
				break;
		}

		list_add_tail(&msg->link, _p);

		_leave(" [unreadyq]");
		return;
	}

	/* next in sequence - simply append into the call's ready queue */
	_debug("Call add packet %d to readyq (+%Zd => %Zd bytes)",
	       msg->seq, msg->dsize, call->app_ready_qty);

	spin_lock(&call->lock);
	call->app_ready_seq = msg->seq;
	call->app_ready_qty += msg->dsize;
	list_add_tail(&msg->link, &call->app_readyq);

	/* move unready packets to the readyq if we got rid of a hole */
	while (!list_empty(&call->app_unreadyq)) {
		pmsg = list_entry(call->app_unreadyq.next,
				  struct rxrpc_message, link);

		if (pmsg->seq != call->app_ready_seq + 1)
			break;

		/* next in sequence - just move list-to-list */
		_debug("Call transfer packet %d to readyq (+%Zd => %Zd bytes)",
		       pmsg->seq, pmsg->dsize, call->app_ready_qty);

		call->app_ready_seq = pmsg->seq;
		call->app_ready_qty += pmsg->dsize;
		list_move_tail(&pmsg->link, &call->app_readyq);
	}

	/* see if we've got the last packet yet */
	if (!list_empty(&call->app_readyq)) {
		pmsg = list_entry(call->app_readyq.prev,
				  struct rxrpc_message, link);
		if (pmsg->hdr.flags & RXRPC_LAST_PACKET) {
			call->app_last_rcv = 1;
			_debug("Last packet on readyq");
		}
	}

	switch (call->app_call_state) {
		/* do nothing if call already aborted */
	case RXRPC_CSTATE_ERROR:
		spin_unlock(&call->lock);
		_leave(" [error]");
		return;

		/* extract the operation ID from an incoming call if that's not
		 * yet been done */
	case RXRPC_CSTATE_SRVR_RCV_OPID:
		spin_unlock(&call->lock);

		/* handle as yet insufficient data for the operation ID */
		if (call->app_ready_qty < 4) {
			if (call->app_last_rcv)
				/* trouble - last packet seen */
				rxrpc_call_abort(call, -EINVAL);

			_leave("");
			return;
		}

		/* pull the operation ID out of the buffer */
		ret = rxrpc_call_read_data(call, &opid, sizeof(opid), 0);
		if (ret < 0) {
			printk("Unexpected error from read-data: %d\n", ret);
			if (call->app_call_state != RXRPC_CSTATE_ERROR)
				rxrpc_call_abort(call, ret);
			_leave("");
			return;
		}
		call->app_opcode = ntohl(opid);

		/* locate the operation in the available ops table */
		optbl = call->conn->service->ops_begin;
		lo = 0;
		hi = call->conn->service->ops_end - optbl;

		while (lo < hi) {
			int mid = (hi + lo) / 2;
			op = &optbl[mid];
			if (call->app_opcode == op->id)
				goto found_op;
			if (call->app_opcode > op->id)
				lo = mid + 1;
			else
				hi = mid;
		}

		/* search failed */
		kproto("Rx Client requested operation %d from %s service",
		       call->app_opcode, call->conn->service->name);
		rxrpc_call_abort(call, -EINVAL);
		_leave(" [inval]");
		return;

	found_op:
		_proto("Rx Client requested operation %s from %s service",
		       op->name, call->conn->service->name);

		/* we're now waiting for the argument block (unless the call
		 * was aborted) */
		spin_lock(&call->lock);
		if (call->app_call_state == RXRPC_CSTATE_SRVR_RCV_OPID ||
		    call->app_call_state == RXRPC_CSTATE_SRVR_SND_REPLY) {
			if (!call->app_last_rcv)
				call->app_call_state =
					RXRPC_CSTATE_SRVR_RCV_ARGS;
			else if (call->app_ready_qty > 0)
				call->app_call_state =
					RXRPC_CSTATE_SRVR_GOT_ARGS;
			else
				call->app_call_state =
					RXRPC_CSTATE_SRVR_SND_REPLY;
			call->app_mark = op->asize;
			call->app_user = op->user;
		}
		spin_unlock(&call->lock);

		_state(call);
		break;

	case RXRPC_CSTATE_SRVR_RCV_ARGS:
		/* change state if just received last packet of arg block */
		if (call->app_last_rcv)
			call->app_call_state = RXRPC_CSTATE_SRVR_GOT_ARGS;
		spin_unlock(&call->lock);

		_state(call);
		break;

	case RXRPC_CSTATE_CLNT_RCV_REPLY:
		/* change state if just received last packet of reply block */
		rmtimo = 0;
		if (call->app_last_rcv) {
			call->app_call_state = RXRPC_CSTATE_CLNT_GOT_REPLY;
			rmtimo = 1;
		}
		spin_unlock(&call->lock);

		if (rmtimo) {
			del_timer_sync(&call->acks_timeout);
			del_timer_sync(&call->rcv_timeout);
			del_timer_sync(&call->ackr_dfr_timo);
		}

		_state(call);
		break;

	default:
		/* deal with data reception in an unexpected state */
		printk("Unexpected state [[[ %u ]]]\n", call->app_call_state);
		__rxrpc_call_abort(call, -EBADMSG);
		_leave("");
		return;
	}

	if (call->app_call_state == RXRPC_CSTATE_CLNT_RCV_REPLY &&
	    call->app_last_rcv)
		BUG();

	/* otherwise just invoke the data function whenever we can satisfy its desire for more
	 * data
	 */
	_proto("Rx Received Op Data: st=%u qty=%Zu mk=%Zu%s",
	       call->app_call_state, call->app_ready_qty, call->app_mark,
	       call->app_last_rcv ? " last-rcvd" : "");

	spin_lock(&call->lock);

	ret = __rxrpc_call_read_data(call);
	switch (ret) {
	case 0:
		spin_unlock(&call->lock);
		call->app_attn_func(call);
		break;
	case -EAGAIN:
		spin_unlock(&call->lock);
		break;
	case -ECONNABORTED:
		spin_unlock(&call->lock);
		break;
	default:
		__rxrpc_call_abort(call, ret);
		break;
	}

	_state(call);

	_leave("");

} /* end rxrpc_call_receive_data_packet() */

/*****************************************************************************/
/*
 * received an ACK packet
 */
static void rxrpc_call_receive_ack_packet(struct rxrpc_call *call,
					  struct rxrpc_message *msg)
{
	struct rxrpc_ackpacket _ack, *ap;
	rxrpc_serial_net_t serial;
	rxrpc_seq_t seq;
	int ret;

	_enter("%p{%u},%p{%u}", call, ntohl(call->call_id), msg, msg->seq);

	/* extract the basic ACK record */
	ap = skb_header_pointer(msg->pkt, msg->offset, sizeof(_ack), &_ack);
	if (ap == NULL) {
		printk("Rx Received short ACK packet\n");
		return;
	}
	msg->offset += sizeof(_ack);

	serial = ap->serial;
	seq = ntohl(ap->firstPacket);

	_proto("Rx Received ACK %%%d { b=%hu m=%hu f=%u p=%u s=%u r=%s n=%u }",
	       ntohl(msg->hdr.serial),
	       ntohs(ap->bufferSpace),
	       ntohs(ap->maxSkew),
	       seq,
	       ntohl(ap->previousPacket),
	       ntohl(serial),
	       rxrpc_acks[ap->reason],
	       call->ackr.nAcks
	       );

	/* check the other side isn't ACK'ing a sequence number I haven't sent
	 * yet */
	if (ap->nAcks > 0 &&
	    (seq > call->snd_seq_count ||
	     seq + ap->nAcks - 1 > call->snd_seq_count)) {
		printk("Received ACK (#%u-#%u) for unsent packet\n",
		       seq, seq + ap->nAcks - 1);
		rxrpc_call_abort(call, -EINVAL);
		_leave("");
		return;
	}

	/* deal with RTT calculation */
	if (serial) {
		struct rxrpc_message *rttmsg;

		/* find the prompting packet */
		spin_lock(&call->lock);
		if (call->snd_ping && call->snd_ping->hdr.serial == serial) {
			/* it was a ping packet */
			rttmsg = call->snd_ping;
			call->snd_ping = NULL;
			spin_unlock(&call->lock);

			if (rttmsg) {
				rttmsg->rttdone = 1;
				rxrpc_peer_calculate_rtt(call->conn->peer,
							 rttmsg, msg);
				rxrpc_put_message(rttmsg);
			}
		}
		else {
			struct list_head *_p;

			/* it ought to be a data packet - look in the pending
			 * ACK list */
			list_for_each(_p, &call->acks_pendq) {
				rttmsg = list_entry(_p, struct rxrpc_message,
						    link);
				if (rttmsg->hdr.serial == serial) {
					if (rttmsg->rttdone)
						/* never do RTT twice without
						 * resending */
						break;

					rttmsg->rttdone = 1;
					rxrpc_peer_calculate_rtt(
						call->conn->peer, rttmsg, msg);
					break;
				}
			}
			spin_unlock(&call->lock);
		}
	}

	switch (ap->reason) {
		/* deal with negative/positive acknowledgement of data
		 * packets */
	case RXRPC_ACK_REQUESTED:
	case RXRPC_ACK_DELAY:
	case RXRPC_ACK_IDLE:
		rxrpc_call_definitively_ACK(call, seq - 1);

	case RXRPC_ACK_DUPLICATE:
	case RXRPC_ACK_OUT_OF_SEQUENCE:
	case RXRPC_ACK_EXCEEDS_WINDOW:
		call->snd_resend_cnt = 0;
		ret = rxrpc_call_record_ACK(call, msg, seq, ap->nAcks);
		if (ret < 0)
			rxrpc_call_abort(call, ret);
		break;

		/* respond to ping packets immediately */
	case RXRPC_ACK_PING:
		rxrpc_call_generate_ACK(call, &msg->hdr, ap);
		break;

		/* only record RTT on ping response packets */
	case RXRPC_ACK_PING_RESPONSE:
		if (call->snd_ping) {
			struct rxrpc_message *rttmsg;

			/* only do RTT stuff if the response matches the
			 * retained ping */
			rttmsg = NULL;
			spin_lock(&call->lock);
			if (call->snd_ping &&
			    call->snd_ping->hdr.serial == ap->serial) {
				rttmsg = call->snd_ping;
				call->snd_ping = NULL;
			}
			spin_unlock(&call->lock);

			if (rttmsg) {
				rttmsg->rttdone = 1;
				rxrpc_peer_calculate_rtt(call->conn->peer,
							 rttmsg, msg);
				rxrpc_put_message(rttmsg);
			}
		}
		break;

	default:
		printk("Unsupported ACK reason %u\n", ap->reason);
		break;
	}

	_leave("");
} /* end rxrpc_call_receive_ack_packet() */

/*****************************************************************************/
/*
 * record definitive ACKs for all messages up to and including the one with the
 * 'highest' seq
 */
static void rxrpc_call_definitively_ACK(struct rxrpc_call *call,
					rxrpc_seq_t highest)
{
	struct rxrpc_message *msg;
	int now_complete;

	_enter("%p{ads=%u},%u", call, call->acks_dftv_seq, highest);

	while (call->acks_dftv_seq < highest) {
		call->acks_dftv_seq++;

		_proto("Definitive ACK on packet #%u", call->acks_dftv_seq);

		/* discard those at front of queue until message with highest
		 * ACK is found */
		spin_lock(&call->lock);
		msg = NULL;
		if (!list_empty(&call->acks_pendq)) {
			msg = list_entry(call->acks_pendq.next,
					 struct rxrpc_message, link);
			list_del_init(&msg->link); /* dequeue */
			if (msg->state == RXRPC_MSG_SENT)
				call->acks_pend_cnt--;
		}
		spin_unlock(&call->lock);

		/* insanity check */
		if (!msg)
			panic("%s(): acks_pendq unexpectedly empty\n",
			      __FUNCTION__);

		if (msg->seq != call->acks_dftv_seq)
			panic("%s(): Packet #%u expected at front of acks_pendq"
			      " (#%u found)\n",
			      __FUNCTION__, call->acks_dftv_seq, msg->seq);

		/* discard the message */
		msg->state = RXRPC_MSG_DONE;
		rxrpc_put_message(msg);
	}

	/* if all sent packets are definitively ACK'd then prod any sleepers just in case */
	now_complete = 0;
	spin_lock(&call->lock);
	if (call->acks_dftv_seq == call->snd_seq_count) {
		if (call->app_call_state != RXRPC_CSTATE_COMPLETE) {
			call->app_call_state = RXRPC_CSTATE_COMPLETE;
			_state(call);
			now_complete = 1;
		}
	}
	spin_unlock(&call->lock);

	if (now_complete) {
		del_timer_sync(&call->acks_timeout);
		del_timer_sync(&call->rcv_timeout);
		del_timer_sync(&call->ackr_dfr_timo);
		call->app_attn_func(call);
	}

	_leave("");
} /* end rxrpc_call_definitively_ACK() */

/*****************************************************************************/
/*
 * record the specified amount of ACKs/NAKs
 */
static int rxrpc_call_record_ACK(struct rxrpc_call *call,
				 struct rxrpc_message *msg,
				 rxrpc_seq_t seq,
				 size_t count)
{
	struct rxrpc_message *dmsg;
	struct list_head *_p;
	rxrpc_seq_t highest;
	unsigned ix;
	size_t chunk;
	char resend, now_complete;
	u8 acks[16];

	_enter("%p{apc=%u ads=%u},%p,%u,%Zu",
	       call, call->acks_pend_cnt, call->acks_dftv_seq,
	       msg, seq, count);

	/* handle re-ACK'ing of definitively ACK'd packets (may be out-of-order
	 * ACKs) */
	if (seq <= call->acks_dftv_seq) {
		unsigned delta = call->acks_dftv_seq - seq;

		if (count <= delta) {
			_leave(" = 0 [all definitively ACK'd]");
			return 0;
		}

		seq += delta;
		count -= delta;
		msg->offset += delta;
	}

	highest = seq + count - 1;
	resend = 0;
	while (count > 0) {
		/* extract up to 16 ACK slots at a time */
		chunk = min(count, sizeof(acks));
		count -= chunk;

		memset(acks, 2, sizeof(acks));

		if (skb_copy_bits(msg->pkt, msg->offset, &acks, chunk) < 0) {
			printk("Rx Received short ACK packet\n");
			_leave(" = -EINVAL");
			return -EINVAL;
		}
		msg->offset += chunk;

		/* check that the ACK set is valid */
		for (ix = 0; ix < chunk; ix++) {
			switch (acks[ix]) {
			case RXRPC_ACK_TYPE_ACK:
				break;
			case RXRPC_ACK_TYPE_NACK:
				resend = 1;
				break;
			default:
				printk("Rx Received unsupported ACK state"
				       " %u\n", acks[ix]);
				_leave(" = -EINVAL");
				return -EINVAL;
			}
		}

		_proto("Rx ACK of packets #%u-#%u "
		       "[%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c] (pend=%u)",
		       seq, (unsigned) (seq + chunk - 1),
		       _acktype[acks[0x0]],
		       _acktype[acks[0x1]],
		       _acktype[acks[0x2]],
		       _acktype[acks[0x3]],
		       _acktype[acks[0x4]],
		       _acktype[acks[0x5]],
		       _acktype[acks[0x6]],
		       _acktype[acks[0x7]],
		       _acktype[acks[0x8]],
		       _acktype[acks[0x9]],
		       _acktype[acks[0xA]],
		       _acktype[acks[0xB]],
		       _acktype[acks[0xC]],
		       _acktype[acks[0xD]],
		       _acktype[acks[0xE]],
		       _acktype[acks[0xF]],
		       call->acks_pend_cnt
		       );

		/* mark the packets in the ACK queue as being provisionally
		 * ACK'd */
		ix = 0;
		spin_lock(&call->lock);

		/* find the first packet ACK'd/NAK'd here */
		list_for_each(_p, &call->acks_pendq) {
			dmsg = list_entry(_p, struct rxrpc_message, link);
			if (dmsg->seq == seq)
				goto found_first;
			_debug("- %u: skipping #%u", ix, dmsg->seq);
		}
		goto bad_queue;

	found_first:
		do {
			_debug("- %u: processing #%u (%c) apc=%u",
			       ix, dmsg->seq, _acktype[acks[ix]],
			       call->acks_pend_cnt);

			if (acks[ix] == RXRPC_ACK_TYPE_ACK) {
				if (dmsg->state == RXRPC_MSG_SENT)
					call->acks_pend_cnt--;
				dmsg->state = RXRPC_MSG_ACKED;
			}
			else {
				if (dmsg->state == RXRPC_MSG_ACKED)
					call->acks_pend_cnt++;
				dmsg->state = RXRPC_MSG_SENT;
			}
			ix++;
			seq++;

			_p = dmsg->link.next;
			dmsg = list_entry(_p, struct rxrpc_message, link);
		} while(ix < chunk &&
			_p != &call->acks_pendq &&
			dmsg->seq == seq);

		if (ix < chunk)
			goto bad_queue;

		spin_unlock(&call->lock);
	}

	if (resend)
		rxrpc_call_resend(call, highest);

	/* if all packets are provisionally ACK'd, then wake up anyone who's
	 * waiting for that */
	now_complete = 0;
	spin_lock(&call->lock);
	if (call->acks_pend_cnt == 0) {
		if (call->app_call_state == RXRPC_CSTATE_SRVR_RCV_FINAL_ACK) {
			call->app_call_state = RXRPC_CSTATE_COMPLETE;
			_state(call);
		}
		now_complete = 1;
	}
	spin_unlock(&call->lock);

	if (now_complete) {
		_debug("- wake up waiters");
		del_timer_sync(&call->acks_timeout);
		del_timer_sync(&call->rcv_timeout);
		del_timer_sync(&call->ackr_dfr_timo);
		call->app_attn_func(call);
	}

	_leave(" = 0 (apc=%u)", call->acks_pend_cnt);
	return 0;

 bad_queue:
	panic("%s(): acks_pendq in bad state (packet #%u absent)\n",
	      __FUNCTION__, seq);

} /* end rxrpc_call_record_ACK() */

/*****************************************************************************/
/*
 * transfer data from the ready packet queue to the asynchronous read buffer
 * - since this func is the only one going to look at packets queued on
 *   app_readyq, we don't need a lock to modify or access them, only to modify
 *   the queue pointers
 * - called with call->lock held
 * - the buffer must be in kernel space
 * - returns:
 *	0 if buffer filled
 *	-EAGAIN if buffer not filled and more data to come
 *	-EBADMSG if last packet received and insufficient data left
 *	-ECONNABORTED if the call has in an error state
 */
static int __rxrpc_call_read_data(struct rxrpc_call *call)
{
	struct rxrpc_message *msg;
	size_t qty;
	int ret;

	_enter("%p{as=%d buf=%p qty=%Zu/%Zu}",
	       call,
	       call->app_async_read, call->app_read_buf,
	       call->app_ready_qty, call->app_mark);

	/* check the state */
	switch (call->app_call_state) {
	case RXRPC_CSTATE_SRVR_RCV_ARGS:
	case RXRPC_CSTATE_CLNT_RCV_REPLY:
		if (call->app_last_rcv) {
			printk("%s(%p,%p,%Zd):"
			       " Inconsistent call state (%s, last pkt)",
			       __FUNCTION__,
			       call, call->app_read_buf, call->app_mark,
			       rxrpc_call_states[call->app_call_state]);
			BUG();
		}
		break;

	case RXRPC_CSTATE_SRVR_RCV_OPID:
	case RXRPC_CSTATE_SRVR_GOT_ARGS:
	case RXRPC_CSTATE_CLNT_GOT_REPLY:
		break;

	case RXRPC_CSTATE_SRVR_SND_REPLY:
		if (!call->app_last_rcv) {
			printk("%s(%p,%p,%Zd):"
			       " Inconsistent call state (%s, not last pkt)",
			       __FUNCTION__,
			       call, call->app_read_buf, call->app_mark,
			       rxrpc_call_states[call->app_call_state]);
			BUG();
		}
		_debug("Trying to read data from call in SND_REPLY state");
		break;

	case RXRPC_CSTATE_ERROR:
		_leave(" = -ECONNABORTED");
		return -ECONNABORTED;

	default:
		printk("reading in unexpected state [[[ %u ]]]\n",
		       call->app_call_state);
		BUG();
	}

	/* handle the case of not having an async buffer */
	if (!call->app_async_read) {
		if (call->app_mark == RXRPC_APP_MARK_EOF) {
			ret = call->app_last_rcv ? 0 : -EAGAIN;
		}
		else {
			if (call->app_mark >= call->app_ready_qty) {
				call->app_mark = RXRPC_APP_MARK_EOF;
				ret = 0;
			}
			else {
				ret = call->app_last_rcv ? -EBADMSG : -EAGAIN;
			}
		}

		_leave(" = %d [no buf]", ret);
		return 0;
	}

	while (!list_empty(&call->app_readyq) && call->app_mark > 0) {
		msg = list_entry(call->app_readyq.next,
				 struct rxrpc_message, link);

		/* drag as much data as we need out of this packet */
		qty = min(call->app_mark, msg->dsize);

		_debug("reading %Zu from skb=%p off=%lu",
		       qty, msg->pkt, msg->offset);

		if (call->app_read_buf)
			if (skb_copy_bits(msg->pkt, msg->offset,
					  call->app_read_buf, qty) < 0)
				panic("%s: Failed to copy data from packet:"
				      " (%p,%p,%Zd)",
				      __FUNCTION__,
				      call, call->app_read_buf, qty);

		/* if that packet is now empty, discard it */
		call->app_ready_qty -= qty;
		msg->dsize -= qty;

		if (msg->dsize == 0) {
			list_del_init(&msg->link);
			rxrpc_put_message(msg);
		}
		else {
			msg->offset += qty;
		}

		call->app_mark -= qty;
		if (call->app_read_buf)
			call->app_read_buf += qty;
	}

	if (call->app_mark == 0) {
		call->app_async_read = 0;
		call->app_mark = RXRPC_APP_MARK_EOF;
		call->app_read_buf = NULL;

		/* adjust the state if used up all packets */
		if (list_empty(&call->app_readyq) && call->app_last_rcv) {
			switch (call->app_call_state) {
			case RXRPC_CSTATE_SRVR_RCV_OPID:
				call->app_call_state = RXRPC_CSTATE_SRVR_SND_REPLY;
				call->app_mark = RXRPC_APP_MARK_EOF;
				_state(call);
				del_timer_sync(&call->rcv_timeout);
				break;
			case RXRPC_CSTATE_SRVR_GOT_ARGS:
				call->app_call_state = RXRPC_CSTATE_SRVR_SND_REPLY;
				_state(call);
				del_timer_sync(&call->rcv_timeout);
				break;
			default:
				call->app_call_state = RXRPC_CSTATE_COMPLETE;
				_state(call);
				del_timer_sync(&call->acks_timeout);
				del_timer_sync(&call->ackr_dfr_timo);
				del_timer_sync(&call->rcv_timeout);
				break;
			}
		}

		_leave(" = 0");
		return 0;
	}

	if (call->app_last_rcv) {
		_debug("Insufficient data (%Zu/%Zu)",
		       call->app_ready_qty, call->app_mark);
		call->app_async_read = 0;
		call->app_mark = RXRPC_APP_MARK_EOF;
		call->app_read_buf = NULL;

		_leave(" = -EBADMSG");
		return -EBADMSG;
	}

	_leave(" = -EAGAIN");
	return -EAGAIN;
} /* end __rxrpc_call_read_data() */

/*****************************************************************************/
/*
 * attempt to read the specified amount of data from the call's ready queue
 * into the buffer provided
 * - since this func is the only one going to look at packets queued on
 *   app_readyq, we don't need a lock to modify or access them, only to modify
 *   the queue pointers
 * - if the buffer pointer is NULL, then data is merely drained, not copied
 * - if flags&RXRPC_CALL_READ_BLOCK, then the function will wait until there is
 *   enough data or an error will be generated
 *   - note that the caller must have added the calling task to the call's wait
 *     queue beforehand
 * - if flags&RXRPC_CALL_READ_ALL, then an error will be generated if this
 *   function doesn't read all available data
 */
int rxrpc_call_read_data(struct rxrpc_call *call,
			 void *buffer, size_t size, int flags)
{
	int ret;

	_enter("%p{arq=%Zu},%p,%Zd,%x",
	       call, call->app_ready_qty, buffer, size, flags);

	spin_lock(&call->lock);

	if (unlikely(!!call->app_read_buf)) {
		spin_unlock(&call->lock);
		_leave(" = -EBUSY");
		return -EBUSY;
	}

	call->app_mark = size;
	call->app_read_buf = buffer;
	call->app_async_read = 1;
	call->app_read_count++;

	/* read as much data as possible */
	ret = __rxrpc_call_read_data(call);
	switch (ret) {
	case 0:
		if (flags & RXRPC_CALL_READ_ALL &&
		    (!call->app_last_rcv || call->app_ready_qty > 0)) {
			_leave(" = -EBADMSG");
			__rxrpc_call_abort(call, -EBADMSG);
			return -EBADMSG;
		}

		spin_unlock(&call->lock);
		call->app_attn_func(call);
		_leave(" = 0");
		return ret;

	case -ECONNABORTED:
		spin_unlock(&call->lock);
		_leave(" = %d [aborted]", ret);
		return ret;

	default:
		__rxrpc_call_abort(call, ret);
		_leave(" = %d", ret);
		return ret;

	case -EAGAIN:
		spin_unlock(&call->lock);

		if (!(flags & RXRPC_CALL_READ_BLOCK)) {
			_leave(" = -EAGAIN");
			return -EAGAIN;
		}

		/* wait for the data to arrive */
		_debug("blocking for data arrival");

		for (;;) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (!call->app_async_read || signal_pending(current))
				break;
			schedule();
		}
		set_current_state(TASK_RUNNING);

		if (signal_pending(current)) {
			_leave(" = -EINTR");
			return -EINTR;
		}

		if (call->app_call_state == RXRPC_CSTATE_ERROR) {
			_leave(" = -ECONNABORTED");
			return -ECONNABORTED;
		}

		_leave(" = 0");
		return 0;
	}

} /* end rxrpc_call_read_data() */

/*****************************************************************************/
/*
 * write data to a call
 * - the data may not be sent immediately if it doesn't fill a buffer
 * - if we can't queue all the data for buffering now, siov[] will have been
 *   adjusted to take account of what has been sent
 */
int rxrpc_call_write_data(struct rxrpc_call *call,
			  size_t sioc,
			  struct kvec *siov,
			  u8 rxhdr_flags,
			  gfp_t alloc_flags,
			  int dup_data,
			  size_t *size_sent)
{
	struct rxrpc_message *msg;
	struct kvec *sptr;
	size_t space, size, chunk, tmp;
	char *buf;
	int ret;

	_enter("%p,%Zu,%p,%02x,%x,%d,%p",
	       call, sioc, siov, rxhdr_flags, alloc_flags, dup_data,
	       size_sent);

	*size_sent = 0;
	size = 0;
	ret = -EINVAL;

	/* can't send more if we've sent last packet from this end */
	switch (call->app_call_state) {
	case RXRPC_CSTATE_SRVR_SND_REPLY:
	case RXRPC_CSTATE_CLNT_SND_ARGS:
		break;
	case RXRPC_CSTATE_ERROR:
		ret = call->app_errno;
	default:
		goto out;
	}

	/* calculate how much data we've been given */
	sptr = siov;
	for (; sioc > 0; sptr++, sioc--) {
		if (!sptr->iov_len)
			continue;

		if (!sptr->iov_base)
			goto out;

		size += sptr->iov_len;
	}

	_debug("- size=%Zu mtu=%Zu", size, call->conn->mtu_size);

	do {
		/* make sure there's a message under construction */
		if (!call->snd_nextmsg) {
			/* no - allocate a message with no data yet attached */
			ret = rxrpc_conn_newmsg(call->conn, call,
						RXRPC_PACKET_TYPE_DATA,
						0, NULL, alloc_flags,
						&call->snd_nextmsg);
			if (ret < 0)
				goto out;
			_debug("- allocated new message [ds=%Zu]",
			       call->snd_nextmsg->dsize);
		}

		msg = call->snd_nextmsg;
		msg->hdr.flags |= rxhdr_flags;

		/* deal with zero-length terminal packet */
		if (size == 0) {
			if (rxhdr_flags & RXRPC_LAST_PACKET) {
				ret = rxrpc_call_flush(call);
				if (ret < 0)
					goto out;
			}
			break;
		}

		/* work out how much space current packet has available */
		space = call->conn->mtu_size - msg->dsize;
		chunk = min(space, size);

		_debug("- [before] space=%Zu chunk=%Zu", space, chunk);

		while (!siov->iov_len)
			siov++;

		/* if we are going to have to duplicate the data then coalesce
		 * it too */
		if (dup_data) {
			/* don't allocate more that 1 page at a time */
			if (chunk > PAGE_SIZE)
				chunk = PAGE_SIZE;

			/* allocate a data buffer and attach to the message */
			buf = kmalloc(chunk, alloc_flags);
			if (unlikely(!buf)) {
				if (msg->dsize ==
				    sizeof(struct rxrpc_header)) {
					/* discard an empty msg and wind back
					 * the seq counter */
					rxrpc_put_message(msg);
					call->snd_nextmsg = NULL;
					call->snd_seq_count--;
				}

				ret = -ENOMEM;
				goto out;
			}

			tmp = msg->dcount++;
			set_bit(tmp, &msg->dfree);
			msg->data[tmp].iov_base = buf;
			msg->data[tmp].iov_len = chunk;
			msg->dsize += chunk;
			*size_sent += chunk;
			size -= chunk;

			/* load the buffer with data */
			while (chunk > 0) {
				tmp = min(chunk, siov->iov_len);
				memcpy(buf, siov->iov_base, tmp);
				buf += tmp;
				siov->iov_base += tmp;
				siov->iov_len -= tmp;
				if (!siov->iov_len)
					siov++;
				chunk -= tmp;
			}
		}
		else {
			/* we want to attach the supplied buffers directly */
			while (chunk > 0 &&
			       msg->dcount < RXRPC_MSG_MAX_IOCS) {
				tmp = msg->dcount++;
				msg->data[tmp].iov_base = siov->iov_base;
				msg->data[tmp].iov_len = siov->iov_len;
				msg->dsize += siov->iov_len;
				*size_sent += siov->iov_len;
				size -= siov->iov_len;
				chunk -= siov->iov_len;
				siov++;
			}
		}

		_debug("- [loaded] chunk=%Zu size=%Zu", chunk, size);

		/* dispatch the message when full, final or requesting ACK */
		if (msg->dsize >= call->conn->mtu_size || rxhdr_flags) {
			ret = rxrpc_call_flush(call);
			if (ret < 0)
				goto out;
		}

	} while(size > 0);

	ret = 0;
 out:
	_leave(" = %d (%Zd queued, %Zd rem)", ret, *size_sent, size);
	return ret;

} /* end rxrpc_call_write_data() */

/*****************************************************************************/
/*
 * flush outstanding packets to the network
 */
static int rxrpc_call_flush(struct rxrpc_call *call)
{
	struct rxrpc_message *msg;
	int ret = 0;

	_enter("%p", call);

	rxrpc_get_call(call);

	/* if there's a packet under construction, then dispatch it now */
	if (call->snd_nextmsg) {
		msg = call->snd_nextmsg;
		call->snd_nextmsg = NULL;

		if (msg->hdr.flags & RXRPC_LAST_PACKET) {
			msg->hdr.flags &= ~RXRPC_MORE_PACKETS;
			if (call->app_call_state != RXRPC_CSTATE_CLNT_SND_ARGS)
				msg->hdr.flags |= RXRPC_REQUEST_ACK;
		}
		else {
			msg->hdr.flags |= RXRPC_MORE_PACKETS;
		}

		_proto("Sending DATA message { ds=%Zu dc=%u df=%02lu }",
		       msg->dsize, msg->dcount, msg->dfree);

		/* queue and adjust call state */
		spin_lock(&call->lock);
		list_add_tail(&msg->link, &call->acks_pendq);

		/* decide what to do depending on current state and if this is
		 * the last packet */
		ret = -EINVAL;
		switch (call->app_call_state) {
		case RXRPC_CSTATE_SRVR_SND_REPLY:
			if (msg->hdr.flags & RXRPC_LAST_PACKET) {
				call->app_call_state =
					RXRPC_CSTATE_SRVR_RCV_FINAL_ACK;
				_state(call);
			}
			break;

		case RXRPC_CSTATE_CLNT_SND_ARGS:
			if (msg->hdr.flags & RXRPC_LAST_PACKET) {
				call->app_call_state =
					RXRPC_CSTATE_CLNT_RCV_REPLY;
				_state(call);
			}
			break;

		case RXRPC_CSTATE_ERROR:
			ret = call->app_errno;
		default:
			spin_unlock(&call->lock);
			goto out;
		}

		call->acks_pend_cnt++;

		mod_timer(&call->acks_timeout,
			  __rxrpc_rtt_based_timeout(call,
						    rxrpc_call_acks_timeout));

		spin_unlock(&call->lock);

		ret = rxrpc_conn_sendmsg(call->conn, msg);
		if (ret == 0)
			call->pkt_snd_count++;
	}

 out:
	rxrpc_put_call(call);

	_leave(" = %d", ret);
	return ret;

} /* end rxrpc_call_flush() */

/*****************************************************************************/
/*
 * resend NAK'd or unacknowledged packets up to the highest one specified
 */
static void rxrpc_call_resend(struct rxrpc_call *call, rxrpc_seq_t highest)
{
	struct rxrpc_message *msg;
	struct list_head *_p;
	rxrpc_seq_t seq = 0;

	_enter("%p,%u", call, highest);

	_proto("Rx Resend required");

	/* handle too many resends */
	if (call->snd_resend_cnt >= rxrpc_call_max_resend) {
		_debug("Aborting due to too many resends (rcv=%d)",
		       call->pkt_rcv_count);
		rxrpc_call_abort(call,
				 call->pkt_rcv_count > 0 ? -EIO : -ETIMEDOUT);
		_leave("");
		return;
	}

	spin_lock(&call->lock);
	call->snd_resend_cnt++;
	for (;;) {
		/* determine which the next packet we might need to ACK is */
		if (seq <= call->acks_dftv_seq)
			seq = call->acks_dftv_seq;
		seq++;

		if (seq > highest)
			break;

		/* look for the packet in the pending-ACK queue */
		list_for_each(_p, &call->acks_pendq) {
			msg = list_entry(_p, struct rxrpc_message, link);
			if (msg->seq == seq)
				goto found_msg;
		}

		panic("%s(%p,%d):"
		      " Inconsistent pending-ACK queue (ds=%u sc=%u sq=%u)\n",
		      __FUNCTION__, call, highest,
		      call->acks_dftv_seq, call->snd_seq_count, seq);

	found_msg:
		if (msg->state != RXRPC_MSG_SENT)
			continue; /* only un-ACK'd packets */

		rxrpc_get_message(msg);
		spin_unlock(&call->lock);

		/* send each message again (and ignore any errors we might
		 * incur) */
		_proto("Resending DATA message { ds=%Zu dc=%u df=%02lu }",
		       msg->dsize, msg->dcount, msg->dfree);

		if (rxrpc_conn_sendmsg(call->conn, msg) == 0)
			call->pkt_snd_count++;

		rxrpc_put_message(msg);

		spin_lock(&call->lock);
	}

	/* reset the timeout */
	mod_timer(&call->acks_timeout,
		  __rxrpc_rtt_based_timeout(call, rxrpc_call_acks_timeout));

	spin_unlock(&call->lock);

	_leave("");
} /* end rxrpc_call_resend() */

/*****************************************************************************/
/*
 * handle an ICMP error being applied to a call
 */
void rxrpc_call_handle_error(struct rxrpc_call *call, int local, int errno)
{
	_enter("%p{%u},%d", call, ntohl(call->call_id), errno);

	/* if this call is already aborted, then just wake up any waiters */
	if (call->app_call_state == RXRPC_CSTATE_ERROR) {
		call->app_error_func(call);
	}
	else {
		/* tell the app layer what happened */
		spin_lock(&call->lock);
		call->app_call_state = RXRPC_CSTATE_ERROR;
		_state(call);
		if (local)
			call->app_err_state = RXRPC_ESTATE_LOCAL_ERROR;
		else
			call->app_err_state = RXRPC_ESTATE_REMOTE_ERROR;
		call->app_errno		= errno;
		call->app_mark		= RXRPC_APP_MARK_EOF;
		call->app_read_buf	= NULL;
		call->app_async_read	= 0;

		/* map the error */
		call->app_aemap_func(call);

		del_timer_sync(&call->acks_timeout);
		del_timer_sync(&call->rcv_timeout);
		del_timer_sync(&call->ackr_dfr_timo);

		spin_unlock(&call->lock);

		call->app_error_func(call);
	}

	_leave("");
} /* end rxrpc_call_handle_error() */
