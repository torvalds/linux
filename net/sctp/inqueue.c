/* SCTP kernel implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2002 International Business Machines, Corp.
 *
 * This file is part of the SCTP kernel implementation
 *
 * These functions are the methods for accessing the SCTP inqueue.
 *
 * An SCTP inqueue is a queue into which you push SCTP packets
 * (which might be bundles or fragments of chunks) and out of which you
 * pop SCTP whole chunks.
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
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson <karl@athena.chicago.il.us>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

/* Initialize an SCTP inqueue.  */
void sctp_inq_init(struct sctp_inq *queue)
{
	INIT_LIST_HEAD(&queue->in_chunk_list);
	queue->in_progress = NULL;

	/* Create a task for delivering data.  */
	INIT_WORK(&queue->immediate, NULL);
}

/* Release the memory associated with an SCTP inqueue.  */
void sctp_inq_free(struct sctp_inq *queue)
{
	struct sctp_chunk *chunk, *tmp;

	/* Empty the queue.  */
	list_for_each_entry_safe(chunk, tmp, &queue->in_chunk_list, list) {
		list_del_init(&chunk->list);
		sctp_chunk_free(chunk);
	}

	/* If there is a packet which is currently being worked on,
	 * free it as well.
	 */
	if (queue->in_progress) {
		sctp_chunk_free(queue->in_progress);
		queue->in_progress = NULL;
	}
}

/* Put a new packet in an SCTP inqueue.
 * We assume that packet->sctp_hdr is set and in host byte order.
 */
void sctp_inq_push(struct sctp_inq *q, struct sctp_chunk *chunk)
{
	/* Directly call the packet handling routine. */
	if (chunk->rcvr->dead) {
		sctp_chunk_free(chunk);
		return;
	}

	/* We are now calling this either from the soft interrupt
	 * or from the backlog processing.
	 * Eventually, we should clean up inqueue to not rely
	 * on the BH related data structures.
	 */
	list_add_tail(&chunk->list, &q->in_chunk_list);
	if (chunk->asoc)
		chunk->asoc->stats.ipackets++;
	q->immediate.func(&q->immediate);
}

/* Peek at the next chunk on the inqeue. */
struct sctp_chunkhdr *sctp_inq_peek(struct sctp_inq *queue)
{
	struct sctp_chunk *chunk;
	struct sctp_chunkhdr *ch = NULL;

	chunk = queue->in_progress;
	/* If there is no more chunks in this packet, say so */
	if (chunk->singleton ||
	    chunk->end_of_packet ||
	    chunk->pdiscard)
		    return NULL;

	ch = (struct sctp_chunkhdr *)chunk->chunk_end;

	return ch;
}


/* Extract a chunk from an SCTP inqueue.
 *
 * WARNING:  If you need to put the chunk on another queue, you need to
 * make a shallow copy (clone) of it.
 */
struct sctp_chunk *sctp_inq_pop(struct sctp_inq *queue)
{
	struct sctp_chunk *chunk;
	struct sctp_chunkhdr *ch = NULL;

	/* The assumption is that we are safe to process the chunks
	 * at this time.
	 */

	chunk = queue->in_progress;
	if (chunk) {
		/* There is a packet that we have been working on.
		 * Any post processing work to do before we move on?
		 */
		if (chunk->singleton ||
		    chunk->end_of_packet ||
		    chunk->pdiscard) {
			if (chunk->head_skb == chunk->skb) {
				chunk->skb = skb_shinfo(chunk->skb)->frag_list;
				goto new_skb;
			}
			if (chunk->skb->next) {
				chunk->skb = chunk->skb->next;
				goto new_skb;
			}

			if (chunk->head_skb)
				chunk->skb = chunk->head_skb;
			sctp_chunk_free(chunk);
			chunk = queue->in_progress = NULL;
		} else {
			/* Nothing to do. Next chunk in the packet, please. */
			ch = (struct sctp_chunkhdr *)chunk->chunk_end;
			/* Force chunk->skb->data to chunk->chunk_end.  */
			skb_pull(chunk->skb, chunk->chunk_end - chunk->skb->data);
			/* We are guaranteed to pull a SCTP header. */
		}
	}

	/* Do we need to take the next packet out of the queue to process? */
	if (!chunk) {
		struct list_head *entry;

next_chunk:
		/* Is the queue empty?  */
		entry = sctp_list_dequeue(&queue->in_chunk_list);
		if (!entry)
			return NULL;

		chunk = list_entry(entry, struct sctp_chunk, list);

		if ((skb_shinfo(chunk->skb)->gso_type & SKB_GSO_SCTP) == SKB_GSO_SCTP) {
			/* GSO-marked skbs but without frags, handle
			 * them normally
			 */
			if (skb_shinfo(chunk->skb)->frag_list)
				chunk->head_skb = chunk->skb;

			/* skbs with "cover letter" */
			if (chunk->head_skb && chunk->skb->data_len == chunk->skb->len)
				chunk->skb = skb_shinfo(chunk->skb)->frag_list;

			if (WARN_ON(!chunk->skb)) {
				__SCTP_INC_STATS(dev_net(chunk->skb->dev), SCTP_MIB_IN_PKT_DISCARDS);
				sctp_chunk_free(chunk);
				goto next_chunk;
			}
		}

		if (chunk->asoc)
			sock_rps_save_rxhash(chunk->asoc->base.sk, chunk->skb);

		queue->in_progress = chunk;

new_skb:
		/* This is the first chunk in the packet.  */
		ch = (struct sctp_chunkhdr *)chunk->skb->data;
		chunk->singleton = 1;
		chunk->data_accepted = 0;
		chunk->pdiscard = 0;
		chunk->auth = 0;
		chunk->has_asconf = 0;
		chunk->end_of_packet = 0;
		if (chunk->head_skb) {
			struct sctp_input_cb
				*cb = SCTP_INPUT_CB(chunk->skb),
				*head_cb = SCTP_INPUT_CB(chunk->head_skb);

			cb->chunk = head_cb->chunk;
			cb->af = head_cb->af;
		}
	}

	chunk->chunk_hdr = ch;
	chunk->chunk_end = ((__u8 *)ch) + SCTP_PAD4(ntohs(ch->length));
	skb_pull(chunk->skb, sizeof(*ch));
	chunk->subh.v = NULL; /* Subheader is no longer valid.  */

	if (chunk->chunk_end + sizeof(*ch) < skb_tail_pointer(chunk->skb)) {
		/* This is not a singleton */
		chunk->singleton = 0;
	} else if (chunk->chunk_end > skb_tail_pointer(chunk->skb)) {
		/* Discard inside state machine. */
		chunk->pdiscard = 1;
		chunk->chunk_end = skb_tail_pointer(chunk->skb);
	} else {
		/* We are at the end of the packet, so mark the chunk
		 * in case we need to send a SACK.
		 */
		chunk->end_of_packet = 1;
	}

	pr_debug("+++sctp_inq_pop+++ chunk:%p[%s], length:%d, skb->len:%d\n",
		 chunk, sctp_cname(SCTP_ST_CHUNK(chunk->chunk_hdr->type)),
		 ntohs(chunk->chunk_hdr->length), chunk->skb->len);

	return chunk;
}

/* Set a top-half handler.
 *
 * Originally, we the top-half handler was scheduled as a BH.  We now
 * call the handler directly in sctp_inq_push() at a time that
 * we know we are lock safe.
 * The intent is that this routine will pull stuff out of the
 * inqueue and process it.
 */
void sctp_inq_set_th_handler(struct sctp_inq *q, work_func_t callback)
{
	INIT_WORK(&q->immediate, callback);
}
