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
	sctp_chunkhdr_t *ch = NULL;

	chunk = queue->in_progress;
	/* If there is no more chunks in this packet, say so */
	if (chunk->singleton ||
	    chunk->end_of_packet ||
	    chunk->pdiscard)
		    return NULL;

	ch = (sctp_chunkhdr_t *)chunk->chunk_end;

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
	sctp_chunkhdr_t *ch = NULL;

	/* The assumption is that we are safe to process the chunks
	 * at this time.
	 */

	if ((chunk = queue->in_progress)) {
		/* There is a packet that we have been working on.
		 * Any post processing work to do before we move on?
		 */
		if (chunk->singleton ||
		    chunk->end_of_packet ||
		    chunk->pdiscard) {
			sctp_chunk_free(chunk);
			chunk = queue->in_progress = NULL;
		} else {
			/* Nothing to do. Next chunk in the packet, please. */
			ch = (sctp_chunkhdr_t *) chunk->chunk_end;

			/* Force chunk->skb->data to chunk->chunk_end.  */
			skb_pull(chunk->skb,
				 chunk->chunk_end - chunk->skb->data);

			/* Verify that we have at least chunk headers
			 * worth of buffer left.
			 */
			if (skb_headlen(chunk->skb) < sizeof(sctp_chunkhdr_t)) {
				sctp_chunk_free(chunk);
				chunk = queue->in_progress = NULL;
			}
		}
	}

	/* Do we need to take the next packet out of the queue to process? */
	if (!chunk) {
		struct list_head *entry;

		/* Is the queue empty?  */
		if (list_empty(&queue->in_chunk_list))
			return NULL;

		entry = queue->in_chunk_list.next;
		chunk = queue->in_progress =
			list_entry(entry, struct sctp_chunk, list);
		list_del_init(entry);

		/* This is the first chunk in the packet.  */
		chunk->singleton = 1;
		ch = (sctp_chunkhdr_t *) chunk->skb->data;
		chunk->data_accepted = 0;
	}

	chunk->chunk_hdr = ch;
	chunk->chunk_end = ((__u8 *)ch) + WORD_ROUND(ntohs(ch->length));
	/* In the unlikely case of an IP reassembly, the skb could be
	 * non-linear. If so, update chunk_end so that it doesn't go past
	 * the skb->tail.
	 */
	if (unlikely(skb_is_nonlinear(chunk->skb))) {
		if (chunk->chunk_end > skb_tail_pointer(chunk->skb))
			chunk->chunk_end = skb_tail_pointer(chunk->skb);
	}
	skb_pull(chunk->skb, sizeof(sctp_chunkhdr_t));
	chunk->subh.v = NULL; /* Subheader is no longer valid.  */

	if (chunk->chunk_end < skb_tail_pointer(chunk->skb)) {
		/* This is not a singleton */
		chunk->singleton = 0;
	} else if (chunk->chunk_end > skb_tail_pointer(chunk->skb)) {
		/* RFC 2960, Section 6.10  Bundling
		 *
		 * Partial chunks MUST NOT be placed in an SCTP packet.
		 * If the receiver detects a partial chunk, it MUST drop
		 * the chunk.
		 *
		 * Since the end of the chunk is past the end of our buffer
		 * (which contains the whole packet, we can freely discard
		 * the whole packet.
		 */
		sctp_chunk_free(chunk);
		chunk = queue->in_progress = NULL;

		return NULL;
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
