/* SCTP kernel implementation
 * (C) Copyright IBM Corp. 2001, 2004
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * This abstraction carries sctp events to the ULP (sockets).
 *
 * This SCTP implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This SCTP implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Sridhar Samudrala     <sri@us.ibm.com>
 */

#include <linux/slab.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/busy_poll.h>
#include <net/sctp/structs.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

/* Forward declarations for internal helpers.  */
static struct sctp_ulpevent *sctp_ulpq_reasm(struct sctp_ulpq *ulpq,
					      struct sctp_ulpevent *);
static struct sctp_ulpevent *sctp_ulpq_order(struct sctp_ulpq *,
					      struct sctp_ulpevent *);
static void sctp_ulpq_reasm_drain(struct sctp_ulpq *ulpq);

/* 1st Level Abstractions */

/* Initialize a ULP queue from a block of memory.  */
struct sctp_ulpq *sctp_ulpq_init(struct sctp_ulpq *ulpq,
				 struct sctp_association *asoc)
{
	memset(ulpq, 0, sizeof(struct sctp_ulpq));

	ulpq->asoc = asoc;
	skb_queue_head_init(&ulpq->reasm);
	skb_queue_head_init(&ulpq->reasm_uo);
	skb_queue_head_init(&ulpq->lobby);
	ulpq->pd_mode  = 0;

	return ulpq;
}


/* Flush the reassembly and ordering queues.  */
void sctp_ulpq_flush(struct sctp_ulpq *ulpq)
{
	struct sk_buff *skb;
	struct sctp_ulpevent *event;

	while ((skb = __skb_dequeue(&ulpq->lobby)) != NULL) {
		event = sctp_skb2event(skb);
		sctp_ulpevent_free(event);
	}

	while ((skb = __skb_dequeue(&ulpq->reasm)) != NULL) {
		event = sctp_skb2event(skb);
		sctp_ulpevent_free(event);
	}

	while ((skb = __skb_dequeue(&ulpq->reasm_uo)) != NULL) {
		event = sctp_skb2event(skb);
		sctp_ulpevent_free(event);
	}
}

/* Dispose of a ulpqueue.  */
void sctp_ulpq_free(struct sctp_ulpq *ulpq)
{
	sctp_ulpq_flush(ulpq);
}

/* Process an incoming DATA chunk.  */
int sctp_ulpq_tail_data(struct sctp_ulpq *ulpq, struct sctp_chunk *chunk,
			gfp_t gfp)
{
	struct sk_buff_head temp;
	struct sctp_ulpevent *event;
	int event_eor = 0;

	/* Create an event from the incoming chunk. */
	event = sctp_ulpevent_make_rcvmsg(chunk->asoc, chunk, gfp);
	if (!event)
		return -ENOMEM;

	event->ssn = ntohs(chunk->subh.data_hdr->ssn);
	event->ppid = chunk->subh.data_hdr->ppid;

	/* Do reassembly if needed.  */
	event = sctp_ulpq_reasm(ulpq, event);

	/* Do ordering if needed.  */
	if ((event) && (event->msg_flags & MSG_EOR)) {
		/* Create a temporary list to collect chunks on.  */
		skb_queue_head_init(&temp);
		__skb_queue_tail(&temp, sctp_event2skb(event));

		event = sctp_ulpq_order(ulpq, event);
	}

	/* Send event to the ULP.  'event' is the sctp_ulpevent for
	 * very first SKB on the 'temp' list.
	 */
	if (event) {
		event_eor = (event->msg_flags & MSG_EOR) ? 1 : 0;
		sctp_ulpq_tail_event(ulpq, event);
	}

	return event_eor;
}

/* Add a new event for propagation to the ULP.  */
/* Clear the partial delivery mode for this socket.   Note: This
 * assumes that no association is currently in partial delivery mode.
 */
int sctp_clear_pd(struct sock *sk, struct sctp_association *asoc)
{
	struct sctp_sock *sp = sctp_sk(sk);

	if (atomic_dec_and_test(&sp->pd_mode)) {
		/* This means there are no other associations in PD, so
		 * we can go ahead and clear out the lobby in one shot
		 */
		if (!skb_queue_empty(&sp->pd_lobby)) {
			skb_queue_splice_tail_init(&sp->pd_lobby,
						   &sk->sk_receive_queue);
			return 1;
		}
	} else {
		/* There are other associations in PD, so we only need to
		 * pull stuff out of the lobby that belongs to the
		 * associations that is exiting PD (all of its notifications
		 * are posted here).
		 */
		if (!skb_queue_empty(&sp->pd_lobby) && asoc) {
			struct sk_buff *skb, *tmp;
			struct sctp_ulpevent *event;

			sctp_skb_for_each(skb, &sp->pd_lobby, tmp) {
				event = sctp_skb2event(skb);
				if (event->asoc == asoc) {
					__skb_unlink(skb, &sp->pd_lobby);
					__skb_queue_tail(&sk->sk_receive_queue,
							 skb);
				}
			}
		}
	}

	return 0;
}

/* Set the pd_mode on the socket and ulpq */
static void sctp_ulpq_set_pd(struct sctp_ulpq *ulpq)
{
	struct sctp_sock *sp = sctp_sk(ulpq->asoc->base.sk);

	atomic_inc(&sp->pd_mode);
	ulpq->pd_mode = 1;
}

/* Clear the pd_mode and restart any pending messages waiting for delivery. */
static int sctp_ulpq_clear_pd(struct sctp_ulpq *ulpq)
{
	ulpq->pd_mode = 0;
	sctp_ulpq_reasm_drain(ulpq);
	return sctp_clear_pd(ulpq->asoc->base.sk, ulpq->asoc);
}

/* If the SKB of 'event' is on a list, it is the first such member
 * of that list.
 */
int sctp_ulpq_tail_event(struct sctp_ulpq *ulpq, struct sctp_ulpevent *event)
{
	struct sock *sk = ulpq->asoc->base.sk;
	struct sctp_sock *sp = sctp_sk(sk);
	struct sk_buff_head *queue, *skb_list;
	struct sk_buff *skb = sctp_event2skb(event);
	int clear_pd = 0;

	skb_list = (struct sk_buff_head *) skb->prev;

	/* If the socket is just going to throw this away, do not
	 * even try to deliver it.
	 */
	if (sk->sk_shutdown & RCV_SHUTDOWN &&
	    (sk->sk_shutdown & SEND_SHUTDOWN ||
	     !sctp_ulpevent_is_notification(event)))
		goto out_free;

	if (!sctp_ulpevent_is_notification(event)) {
		sk_mark_napi_id(sk, skb);
		sk_incoming_cpu_update(sk);
	}
	/* Check if the user wishes to receive this event.  */
	if (!sctp_ulpevent_is_enabled(event, ulpq->asoc->subscribe))
		goto out_free;

	/* If we are in partial delivery mode, post to the lobby until
	 * partial delivery is cleared, unless, of course _this_ is
	 * the association the cause of the partial delivery.
	 */

	if (atomic_read(&sp->pd_mode) == 0) {
		queue = &sk->sk_receive_queue;
	} else {
		if (ulpq->pd_mode) {
			/* If the association is in partial delivery, we
			 * need to finish delivering the partially processed
			 * packet before passing any other data.  This is
			 * because we don't truly support stream interleaving.
			 */
			if ((event->msg_flags & MSG_NOTIFICATION) ||
			    (SCTP_DATA_NOT_FRAG ==
				    (event->msg_flags & SCTP_DATA_FRAG_MASK)))
				queue = &sp->pd_lobby;
			else {
				clear_pd = event->msg_flags & MSG_EOR;
				queue = &sk->sk_receive_queue;
			}
		} else {
			/*
			 * If fragment interleave is enabled, we
			 * can queue this to the receive queue instead
			 * of the lobby.
			 */
			if (sp->frag_interleave)
				queue = &sk->sk_receive_queue;
			else
				queue = &sp->pd_lobby;
		}
	}

	/* If we are harvesting multiple skbs they will be
	 * collected on a list.
	 */
	if (skb_list)
		skb_queue_splice_tail_init(skb_list, queue);
	else
		__skb_queue_tail(queue, skb);

	/* Did we just complete partial delivery and need to get
	 * rolling again?  Move pending data to the receive
	 * queue.
	 */
	if (clear_pd)
		sctp_ulpq_clear_pd(ulpq);

	if (queue == &sk->sk_receive_queue && !sp->data_ready_signalled) {
		if (!sock_owned_by_user(sk))
			sp->data_ready_signalled = 1;
		sk->sk_data_ready(sk);
	}
	return 1;

out_free:
	if (skb_list)
		sctp_queue_purge_ulpevents(skb_list);
	else
		sctp_ulpevent_free(event);

	return 0;
}

/* 2nd Level Abstractions */

/* Helper function to store chunks that need to be reassembled.  */
static void sctp_ulpq_store_reasm(struct sctp_ulpq *ulpq,
					 struct sctp_ulpevent *event)
{
	struct sk_buff *pos;
	struct sctp_ulpevent *cevent;
	__u32 tsn, ctsn;

	tsn = event->tsn;

	/* See if it belongs at the end. */
	pos = skb_peek_tail(&ulpq->reasm);
	if (!pos) {
		__skb_queue_tail(&ulpq->reasm, sctp_event2skb(event));
		return;
	}

	/* Short circuit just dropping it at the end. */
	cevent = sctp_skb2event(pos);
	ctsn = cevent->tsn;
	if (TSN_lt(ctsn, tsn)) {
		__skb_queue_tail(&ulpq->reasm, sctp_event2skb(event));
		return;
	}

	/* Find the right place in this list. We store them by TSN.  */
	skb_queue_walk(&ulpq->reasm, pos) {
		cevent = sctp_skb2event(pos);
		ctsn = cevent->tsn;

		if (TSN_lt(tsn, ctsn))
			break;
	}

	/* Insert before pos. */
	__skb_queue_before(&ulpq->reasm, pos, sctp_event2skb(event));

}

/* Helper function to return an event corresponding to the reassembled
 * datagram.
 * This routine creates a re-assembled skb given the first and last skb's
 * as stored in the reassembly queue. The skb's may be non-linear if the sctp
 * payload was fragmented on the way and ip had to reassemble them.
 * We add the rest of skb's to the first skb's fraglist.
 */
struct sctp_ulpevent *sctp_make_reassembled_event(struct net *net,
						  struct sk_buff_head *queue,
						  struct sk_buff *f_frag,
						  struct sk_buff *l_frag)
{
	struct sk_buff *pos;
	struct sk_buff *new = NULL;
	struct sctp_ulpevent *event;
	struct sk_buff *pnext, *last;
	struct sk_buff *list = skb_shinfo(f_frag)->frag_list;

	/* Store the pointer to the 2nd skb */
	if (f_frag == l_frag)
		pos = NULL;
	else
		pos = f_frag->next;

	/* Get the last skb in the f_frag's frag_list if present. */
	for (last = list; list; last = list, list = list->next)
		;

	/* Add the list of remaining fragments to the first fragments
	 * frag_list.
	 */
	if (last)
		last->next = pos;
	else {
		if (skb_cloned(f_frag)) {
			/* This is a cloned skb, we can't just modify
			 * the frag_list.  We need a new skb to do that.
			 * Instead of calling skb_unshare(), we'll do it
			 * ourselves since we need to delay the free.
			 */
			new = skb_copy(f_frag, GFP_ATOMIC);
			if (!new)
				return NULL;	/* try again later */

			sctp_skb_set_owner_r(new, f_frag->sk);

			skb_shinfo(new)->frag_list = pos;
		} else
			skb_shinfo(f_frag)->frag_list = pos;
	}

	/* Remove the first fragment from the reassembly queue.  */
	__skb_unlink(f_frag, queue);

	/* if we did unshare, then free the old skb and re-assign */
	if (new) {
		kfree_skb(f_frag);
		f_frag = new;
	}

	while (pos) {

		pnext = pos->next;

		/* Update the len and data_len fields of the first fragment. */
		f_frag->len += pos->len;
		f_frag->data_len += pos->len;

		/* Remove the fragment from the reassembly queue.  */
		__skb_unlink(pos, queue);

		/* Break if we have reached the last fragment.  */
		if (pos == l_frag)
			break;
		pos->next = pnext;
		pos = pnext;
	}

	event = sctp_skb2event(f_frag);
	SCTP_INC_STATS(net, SCTP_MIB_REASMUSRMSGS);

	return event;
}


/* Helper function to check if an incoming chunk has filled up the last
 * missing fragment in a SCTP datagram and return the corresponding event.
 */
static struct sctp_ulpevent *sctp_ulpq_retrieve_reassembled(struct sctp_ulpq *ulpq)
{
	struct sk_buff *pos;
	struct sctp_ulpevent *cevent;
	struct sk_buff *first_frag = NULL;
	__u32 ctsn, next_tsn;
	struct sctp_ulpevent *retval = NULL;
	struct sk_buff *pd_first = NULL;
	struct sk_buff *pd_last = NULL;
	size_t pd_len = 0;
	struct sctp_association *asoc;
	u32 pd_point;

	/* Initialized to 0 just to avoid compiler warning message.  Will
	 * never be used with this value. It is referenced only after it
	 * is set when we find the first fragment of a message.
	 */
	next_tsn = 0;

	/* The chunks are held in the reasm queue sorted by TSN.
	 * Walk through the queue sequentially and look for a sequence of
	 * fragmented chunks that complete a datagram.
	 * 'first_frag' and next_tsn are reset when we find a chunk which
	 * is the first fragment of a datagram. Once these 2 fields are set
	 * we expect to find the remaining middle fragments and the last
	 * fragment in order. If not, first_frag is reset to NULL and we
	 * start the next pass when we find another first fragment.
	 *
	 * There is a potential to do partial delivery if user sets
	 * SCTP_PARTIAL_DELIVERY_POINT option. Lets count some things here
	 * to see if can do PD.
	 */
	skb_queue_walk(&ulpq->reasm, pos) {
		cevent = sctp_skb2event(pos);
		ctsn = cevent->tsn;

		switch (cevent->msg_flags & SCTP_DATA_FRAG_MASK) {
		case SCTP_DATA_FIRST_FRAG:
			/* If this "FIRST_FRAG" is the first
			 * element in the queue, then count it towards
			 * possible PD.
			 */
			if (skb_queue_is_first(&ulpq->reasm, pos)) {
			    pd_first = pos;
			    pd_last = pos;
			    pd_len = pos->len;
			} else {
			    pd_first = NULL;
			    pd_last = NULL;
			    pd_len = 0;
			}

			first_frag = pos;
			next_tsn = ctsn + 1;
			break;

		case SCTP_DATA_MIDDLE_FRAG:
			if ((first_frag) && (ctsn == next_tsn)) {
				next_tsn++;
				if (pd_first) {
				    pd_last = pos;
				    pd_len += pos->len;
				}
			} else
				first_frag = NULL;
			break;

		case SCTP_DATA_LAST_FRAG:
			if (first_frag && (ctsn == next_tsn))
				goto found;
			else
				first_frag = NULL;
			break;
		}
	}

	asoc = ulpq->asoc;
	if (pd_first) {
		/* Make sure we can enter partial deliver.
		 * We can trigger partial delivery only if framgent
		 * interleave is set, or the socket is not already
		 * in  partial delivery.
		 */
		if (!sctp_sk(asoc->base.sk)->frag_interleave &&
		    atomic_read(&sctp_sk(asoc->base.sk)->pd_mode))
			goto done;

		cevent = sctp_skb2event(pd_first);
		pd_point = sctp_sk(asoc->base.sk)->pd_point;
		if (pd_point && pd_point <= pd_len) {
			retval = sctp_make_reassembled_event(sock_net(asoc->base.sk),
							     &ulpq->reasm,
							     pd_first,
							     pd_last);
			if (retval)
				sctp_ulpq_set_pd(ulpq);
		}
	}
done:
	return retval;
found:
	retval = sctp_make_reassembled_event(sock_net(ulpq->asoc->base.sk),
					     &ulpq->reasm, first_frag, pos);
	if (retval)
		retval->msg_flags |= MSG_EOR;
	goto done;
}

/* Retrieve the next set of fragments of a partial message. */
static struct sctp_ulpevent *sctp_ulpq_retrieve_partial(struct sctp_ulpq *ulpq)
{
	struct sk_buff *pos, *last_frag, *first_frag;
	struct sctp_ulpevent *cevent;
	__u32 ctsn, next_tsn;
	int is_last;
	struct sctp_ulpevent *retval;

	/* The chunks are held in the reasm queue sorted by TSN.
	 * Walk through the queue sequentially and look for the first
	 * sequence of fragmented chunks.
	 */

	if (skb_queue_empty(&ulpq->reasm))
		return NULL;

	last_frag = first_frag = NULL;
	retval = NULL;
	next_tsn = 0;
	is_last = 0;

	skb_queue_walk(&ulpq->reasm, pos) {
		cevent = sctp_skb2event(pos);
		ctsn = cevent->tsn;

		switch (cevent->msg_flags & SCTP_DATA_FRAG_MASK) {
		case SCTP_DATA_FIRST_FRAG:
			if (!first_frag)
				return NULL;
			goto done;
		case SCTP_DATA_MIDDLE_FRAG:
			if (!first_frag) {
				first_frag = pos;
				next_tsn = ctsn + 1;
				last_frag = pos;
			} else if (next_tsn == ctsn) {
				next_tsn++;
				last_frag = pos;
			} else
				goto done;
			break;
		case SCTP_DATA_LAST_FRAG:
			if (!first_frag)
				first_frag = pos;
			else if (ctsn != next_tsn)
				goto done;
			last_frag = pos;
			is_last = 1;
			goto done;
		default:
			return NULL;
		}
	}

	/* We have the reassembled event. There is no need to look
	 * further.
	 */
done:
	retval = sctp_make_reassembled_event(sock_net(ulpq->asoc->base.sk),
					&ulpq->reasm, first_frag, last_frag);
	if (retval && is_last)
		retval->msg_flags |= MSG_EOR;

	return retval;
}


/* Helper function to reassemble chunks.  Hold chunks on the reasm queue that
 * need reassembling.
 */
static struct sctp_ulpevent *sctp_ulpq_reasm(struct sctp_ulpq *ulpq,
						struct sctp_ulpevent *event)
{
	struct sctp_ulpevent *retval = NULL;

	/* Check if this is part of a fragmented message.  */
	if (SCTP_DATA_NOT_FRAG == (event->msg_flags & SCTP_DATA_FRAG_MASK)) {
		event->msg_flags |= MSG_EOR;
		return event;
	}

	sctp_ulpq_store_reasm(ulpq, event);
	if (!ulpq->pd_mode)
		retval = sctp_ulpq_retrieve_reassembled(ulpq);
	else {
		__u32 ctsn, ctsnap;

		/* Do not even bother unless this is the next tsn to
		 * be delivered.
		 */
		ctsn = event->tsn;
		ctsnap = sctp_tsnmap_get_ctsn(&ulpq->asoc->peer.tsn_map);
		if (TSN_lte(ctsn, ctsnap))
			retval = sctp_ulpq_retrieve_partial(ulpq);
	}

	return retval;
}

/* Retrieve the first part (sequential fragments) for partial delivery.  */
static struct sctp_ulpevent *sctp_ulpq_retrieve_first(struct sctp_ulpq *ulpq)
{
	struct sk_buff *pos, *last_frag, *first_frag;
	struct sctp_ulpevent *cevent;
	__u32 ctsn, next_tsn;
	struct sctp_ulpevent *retval;

	/* The chunks are held in the reasm queue sorted by TSN.
	 * Walk through the queue sequentially and look for a sequence of
	 * fragmented chunks that start a datagram.
	 */

	if (skb_queue_empty(&ulpq->reasm))
		return NULL;

	last_frag = first_frag = NULL;
	retval = NULL;
	next_tsn = 0;

	skb_queue_walk(&ulpq->reasm, pos) {
		cevent = sctp_skb2event(pos);
		ctsn = cevent->tsn;

		switch (cevent->msg_flags & SCTP_DATA_FRAG_MASK) {
		case SCTP_DATA_FIRST_FRAG:
			if (!first_frag) {
				first_frag = pos;
				next_tsn = ctsn + 1;
				last_frag = pos;
			} else
				goto done;
			break;

		case SCTP_DATA_MIDDLE_FRAG:
			if (!first_frag)
				return NULL;
			if (ctsn == next_tsn) {
				next_tsn++;
				last_frag = pos;
			} else
				goto done;
			break;

		case SCTP_DATA_LAST_FRAG:
			if (!first_frag)
				return NULL;
			else
				goto done;
			break;

		default:
			return NULL;
		}
	}

	/* We have the reassembled event. There is no need to look
	 * further.
	 */
done:
	retval = sctp_make_reassembled_event(sock_net(ulpq->asoc->base.sk),
					&ulpq->reasm, first_frag, last_frag);
	return retval;
}

/*
 * Flush out stale fragments from the reassembly queue when processing
 * a Forward TSN.
 *
 * RFC 3758, Section 3.6
 *
 * After receiving and processing a FORWARD TSN, the data receiver MUST
 * take cautions in updating its re-assembly queue.  The receiver MUST
 * remove any partially reassembled message, which is still missing one
 * or more TSNs earlier than or equal to the new cumulative TSN point.
 * In the event that the receiver has invoked the partial delivery API,
 * a notification SHOULD also be generated to inform the upper layer API
 * that the message being partially delivered will NOT be completed.
 */
void sctp_ulpq_reasm_flushtsn(struct sctp_ulpq *ulpq, __u32 fwd_tsn)
{
	struct sk_buff *pos, *tmp;
	struct sctp_ulpevent *event;
	__u32 tsn;

	if (skb_queue_empty(&ulpq->reasm))
		return;

	skb_queue_walk_safe(&ulpq->reasm, pos, tmp) {
		event = sctp_skb2event(pos);
		tsn = event->tsn;

		/* Since the entire message must be abandoned by the
		 * sender (item A3 in Section 3.5, RFC 3758), we can
		 * free all fragments on the list that are less then
		 * or equal to ctsn_point
		 */
		if (TSN_lte(tsn, fwd_tsn)) {
			__skb_unlink(pos, &ulpq->reasm);
			sctp_ulpevent_free(event);
		} else
			break;
	}
}

/*
 * Drain the reassembly queue.  If we just cleared parted delivery, it
 * is possible that the reassembly queue will contain already reassembled
 * messages.  Retrieve any such messages and give them to the user.
 */
static void sctp_ulpq_reasm_drain(struct sctp_ulpq *ulpq)
{
	struct sctp_ulpevent *event = NULL;
	struct sk_buff_head temp;

	if (skb_queue_empty(&ulpq->reasm))
		return;

	while ((event = sctp_ulpq_retrieve_reassembled(ulpq)) != NULL) {
		/* Do ordering if needed.  */
		if ((event) && (event->msg_flags & MSG_EOR)) {
			skb_queue_head_init(&temp);
			__skb_queue_tail(&temp, sctp_event2skb(event));

			event = sctp_ulpq_order(ulpq, event);
		}

		/* Send event to the ULP.  'event' is the
		 * sctp_ulpevent for  very first SKB on the  temp' list.
		 */
		if (event)
			sctp_ulpq_tail_event(ulpq, event);
	}
}


/* Helper function to gather skbs that have possibly become
 * ordered by an an incoming chunk.
 */
static void sctp_ulpq_retrieve_ordered(struct sctp_ulpq *ulpq,
					      struct sctp_ulpevent *event)
{
	struct sk_buff_head *event_list;
	struct sk_buff *pos, *tmp;
	struct sctp_ulpevent *cevent;
	struct sctp_stream *stream;
	__u16 sid, csid, cssn;

	sid = event->stream;
	stream  = &ulpq->asoc->stream;

	event_list = (struct sk_buff_head *) sctp_event2skb(event)->prev;

	/* We are holding the chunks by stream, by SSN.  */
	sctp_skb_for_each(pos, &ulpq->lobby, tmp) {
		cevent = (struct sctp_ulpevent *) pos->cb;
		csid = cevent->stream;
		cssn = cevent->ssn;

		/* Have we gone too far?  */
		if (csid > sid)
			break;

		/* Have we not gone far enough?  */
		if (csid < sid)
			continue;

		if (cssn != sctp_ssn_peek(stream, in, sid))
			break;

		/* Found it, so mark in the stream. */
		sctp_ssn_next(stream, in, sid);

		__skb_unlink(pos, &ulpq->lobby);

		/* Attach all gathered skbs to the event.  */
		__skb_queue_tail(event_list, pos);
	}
}

/* Helper function to store chunks needing ordering.  */
static void sctp_ulpq_store_ordered(struct sctp_ulpq *ulpq,
					   struct sctp_ulpevent *event)
{
	struct sk_buff *pos;
	struct sctp_ulpevent *cevent;
	__u16 sid, csid;
	__u16 ssn, cssn;

	pos = skb_peek_tail(&ulpq->lobby);
	if (!pos) {
		__skb_queue_tail(&ulpq->lobby, sctp_event2skb(event));
		return;
	}

	sid = event->stream;
	ssn = event->ssn;

	cevent = (struct sctp_ulpevent *) pos->cb;
	csid = cevent->stream;
	cssn = cevent->ssn;
	if (sid > csid) {
		__skb_queue_tail(&ulpq->lobby, sctp_event2skb(event));
		return;
	}

	if ((sid == csid) && SSN_lt(cssn, ssn)) {
		__skb_queue_tail(&ulpq->lobby, sctp_event2skb(event));
		return;
	}

	/* Find the right place in this list.  We store them by
	 * stream ID and then by SSN.
	 */
	skb_queue_walk(&ulpq->lobby, pos) {
		cevent = (struct sctp_ulpevent *) pos->cb;
		csid = cevent->stream;
		cssn = cevent->ssn;

		if (csid > sid)
			break;
		if (csid == sid && SSN_lt(ssn, cssn))
			break;
	}


	/* Insert before pos. */
	__skb_queue_before(&ulpq->lobby, pos, sctp_event2skb(event));
}

static struct sctp_ulpevent *sctp_ulpq_order(struct sctp_ulpq *ulpq,
					     struct sctp_ulpevent *event)
{
	__u16 sid, ssn;
	struct sctp_stream *stream;

	/* Check if this message needs ordering.  */
	if (event->msg_flags & SCTP_DATA_UNORDERED)
		return event;

	/* Note: The stream ID must be verified before this routine.  */
	sid = event->stream;
	ssn = event->ssn;
	stream  = &ulpq->asoc->stream;

	/* Is this the expected SSN for this stream ID?  */
	if (ssn != sctp_ssn_peek(stream, in, sid)) {
		/* We've received something out of order, so find where it
		 * needs to be placed.  We order by stream and then by SSN.
		 */
		sctp_ulpq_store_ordered(ulpq, event);
		return NULL;
	}

	/* Mark that the next chunk has been found.  */
	sctp_ssn_next(stream, in, sid);

	/* Go find any other chunks that were waiting for
	 * ordering.
	 */
	sctp_ulpq_retrieve_ordered(ulpq, event);

	return event;
}

/* Helper function to gather skbs that have possibly become
 * ordered by forward tsn skipping their dependencies.
 */
static void sctp_ulpq_reap_ordered(struct sctp_ulpq *ulpq, __u16 sid)
{
	struct sk_buff *pos, *tmp;
	struct sctp_ulpevent *cevent;
	struct sctp_ulpevent *event;
	struct sctp_stream *stream;
	struct sk_buff_head temp;
	struct sk_buff_head *lobby = &ulpq->lobby;
	__u16 csid, cssn;

	stream = &ulpq->asoc->stream;

	/* We are holding the chunks by stream, by SSN.  */
	skb_queue_head_init(&temp);
	event = NULL;
	sctp_skb_for_each(pos, lobby, tmp) {
		cevent = (struct sctp_ulpevent *) pos->cb;
		csid = cevent->stream;
		cssn = cevent->ssn;

		/* Have we gone too far?  */
		if (csid > sid)
			break;

		/* Have we not gone far enough?  */
		if (csid < sid)
			continue;

		/* see if this ssn has been marked by skipping */
		if (!SSN_lt(cssn, sctp_ssn_peek(stream, in, csid)))
			break;

		__skb_unlink(pos, lobby);
		if (!event)
			/* Create a temporary list to collect chunks on.  */
			event = sctp_skb2event(pos);

		/* Attach all gathered skbs to the event.  */
		__skb_queue_tail(&temp, pos);
	}

	/* If we didn't reap any data, see if the next expected SSN
	 * is next on the queue and if so, use that.
	 */
	if (event == NULL && pos != (struct sk_buff *)lobby) {
		cevent = (struct sctp_ulpevent *) pos->cb;
		csid = cevent->stream;
		cssn = cevent->ssn;

		if (csid == sid && cssn == sctp_ssn_peek(stream, in, csid)) {
			sctp_ssn_next(stream, in, csid);
			__skb_unlink(pos, lobby);
			__skb_queue_tail(&temp, pos);
			event = sctp_skb2event(pos);
		}
	}

	/* Send event to the ULP.  'event' is the sctp_ulpevent for
	 * very first SKB on the 'temp' list.
	 */
	if (event) {
		/* see if we have more ordered that we can deliver */
		sctp_ulpq_retrieve_ordered(ulpq, event);
		sctp_ulpq_tail_event(ulpq, event);
	}
}

/* Skip over an SSN. This is used during the processing of
 * Forwared TSN chunk to skip over the abandoned ordered data
 */
void sctp_ulpq_skip(struct sctp_ulpq *ulpq, __u16 sid, __u16 ssn)
{
	struct sctp_stream *stream;

	/* Note: The stream ID must be verified before this routine.  */
	stream  = &ulpq->asoc->stream;

	/* Is this an old SSN?  If so ignore. */
	if (SSN_lt(ssn, sctp_ssn_peek(stream, in, sid)))
		return;

	/* Mark that we are no longer expecting this SSN or lower. */
	sctp_ssn_skip(stream, in, sid, ssn);

	/* Go find any other chunks that were waiting for
	 * ordering and deliver them if needed.
	 */
	sctp_ulpq_reap_ordered(ulpq, sid);
}

__u16 sctp_ulpq_renege_list(struct sctp_ulpq *ulpq, struct sk_buff_head *list,
			    __u16 needed)
{
	__u16 freed = 0;
	__u32 tsn, last_tsn;
	struct sk_buff *skb, *flist, *last;
	struct sctp_ulpevent *event;
	struct sctp_tsnmap *tsnmap;

	tsnmap = &ulpq->asoc->peer.tsn_map;

	while ((skb = skb_peek_tail(list)) != NULL) {
		event = sctp_skb2event(skb);
		tsn = event->tsn;

		/* Don't renege below the Cumulative TSN ACK Point. */
		if (TSN_lte(tsn, sctp_tsnmap_get_ctsn(tsnmap)))
			break;

		/* Events in ordering queue may have multiple fragments
		 * corresponding to additional TSNs.  Sum the total
		 * freed space; find the last TSN.
		 */
		freed += skb_headlen(skb);
		flist = skb_shinfo(skb)->frag_list;
		for (last = flist; flist; flist = flist->next) {
			last = flist;
			freed += skb_headlen(last);
		}
		if (last)
			last_tsn = sctp_skb2event(last)->tsn;
		else
			last_tsn = tsn;

		/* Unlink the event, then renege all applicable TSNs. */
		__skb_unlink(skb, list);
		sctp_ulpevent_free(event);
		while (TSN_lte(tsn, last_tsn)) {
			sctp_tsnmap_renege(tsnmap, tsn);
			tsn++;
		}
		if (freed >= needed)
			return freed;
	}

	return freed;
}

/* Renege 'needed' bytes from the ordering queue. */
static __u16 sctp_ulpq_renege_order(struct sctp_ulpq *ulpq, __u16 needed)
{
	return sctp_ulpq_renege_list(ulpq, &ulpq->lobby, needed);
}

/* Renege 'needed' bytes from the reassembly queue. */
static __u16 sctp_ulpq_renege_frags(struct sctp_ulpq *ulpq, __u16 needed)
{
	return sctp_ulpq_renege_list(ulpq, &ulpq->reasm, needed);
}

/* Partial deliver the first message as there is pressure on rwnd. */
void sctp_ulpq_partial_delivery(struct sctp_ulpq *ulpq,
				gfp_t gfp)
{
	struct sctp_ulpevent *event;
	struct sctp_association *asoc;
	struct sctp_sock *sp;
	__u32 ctsn;
	struct sk_buff *skb;

	asoc = ulpq->asoc;
	sp = sctp_sk(asoc->base.sk);

	/* If the association is already in Partial Delivery mode
	 * we have nothing to do.
	 */
	if (ulpq->pd_mode)
		return;

	/* Data must be at or below the Cumulative TSN ACK Point to
	 * start partial delivery.
	 */
	skb = skb_peek(&asoc->ulpq.reasm);
	if (skb != NULL) {
		ctsn = sctp_skb2event(skb)->tsn;
		if (!TSN_lte(ctsn, sctp_tsnmap_get_ctsn(&asoc->peer.tsn_map)))
			return;
	}

	/* If the user enabled fragment interleave socket option,
	 * multiple associations can enter partial delivery.
	 * Otherwise, we can only enter partial delivery if the
	 * socket is not in partial deliver mode.
	 */
	if (sp->frag_interleave || atomic_read(&sp->pd_mode) == 0) {
		/* Is partial delivery possible?  */
		event = sctp_ulpq_retrieve_first(ulpq);
		/* Send event to the ULP.   */
		if (event) {
			sctp_ulpq_tail_event(ulpq, event);
			sctp_ulpq_set_pd(ulpq);
			return;
		}
	}
}

/* Renege some packets to make room for an incoming chunk.  */
void sctp_ulpq_renege(struct sctp_ulpq *ulpq, struct sctp_chunk *chunk,
		      gfp_t gfp)
{
	struct sctp_association *asoc = ulpq->asoc;
	__u32 freed = 0;
	__u16 needed;

	needed = ntohs(chunk->chunk_hdr->length) -
		 sizeof(struct sctp_data_chunk);

	if (skb_queue_empty(&asoc->base.sk->sk_receive_queue)) {
		freed = sctp_ulpq_renege_order(ulpq, needed);
		if (freed < needed)
			freed += sctp_ulpq_renege_frags(ulpq, needed - freed);
	}
	/* If able to free enough room, accept this chunk. */
	if (freed >= needed) {
		int retval = sctp_ulpq_tail_data(ulpq, chunk, gfp);
		/*
		 * Enter partial delivery if chunk has not been
		 * delivered; otherwise, drain the reassembly queue.
		 */
		if (retval <= 0)
			sctp_ulpq_partial_delivery(ulpq, gfp);
		else if (retval == 1)
			sctp_ulpq_reasm_drain(ulpq);
	}

	sk_mem_reclaim(asoc->base.sk);
}



/* Notify the application if an association is aborted and in
 * partial delivery mode.  Send up any pending received messages.
 */
void sctp_ulpq_abort_pd(struct sctp_ulpq *ulpq, gfp_t gfp)
{
	struct sctp_ulpevent *ev = NULL;
	struct sctp_sock *sp;
	struct sock *sk;

	if (!ulpq->pd_mode)
		return;

	sk = ulpq->asoc->base.sk;
	sp = sctp_sk(sk);
	if (sctp_ulpevent_type_enabled(ulpq->asoc->subscribe,
				       SCTP_PARTIAL_DELIVERY_EVENT))
		ev = sctp_ulpevent_make_pdapi(ulpq->asoc,
					      SCTP_PARTIAL_DELIVERY_ABORTED,
					      0, 0, 0, gfp);
	if (ev)
		__skb_queue_tail(&sk->sk_receive_queue, sctp_event2skb(ev));

	/* If there is data waiting, send it up the socket now. */
	if ((sctp_ulpq_clear_pd(ulpq) || ev) && !sp->data_ready_signalled) {
		sp->data_ready_signalled = 1;
		sk->sk_data_ready(sk);
	}
}
