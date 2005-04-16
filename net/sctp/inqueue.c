/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2002 International Business Machines, Corp.
 * 
 * This file is part of the SCTP kernel reference Implementation
 * 
 * These functions are the methods for accessing the SCTP inqueue.
 *
 * An SCTP inqueue is a queue into which you push SCTP packets
 * (which might be bundles or fragments of chunks) and out of which you
 * pop SCTP whole chunks.
 * 
 * The SCTP reference implementation is free software; 
 * you can redistribute it and/or modify it under the terms of 
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * The SCTP reference implementation is distributed in the hope that it 
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.  
 * 
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 * 
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by: 
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson <karl@athena.chicago.il.us>
 * 
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>
#include <linux/interrupt.h>

/* Initialize an SCTP inqueue.  */
void sctp_inq_init(struct sctp_inq *queue)
{
	skb_queue_head_init(&queue->in);
	queue->in_progress = NULL;

	/* Create a task for delivering data.  */
	INIT_WORK(&queue->immediate, NULL, NULL);

	queue->malloced = 0;
}

/* Release the memory associated with an SCTP inqueue.  */
void sctp_inq_free(struct sctp_inq *queue)
{
	struct sctp_chunk *chunk;

	/* Empty the queue.  */
	while ((chunk = (struct sctp_chunk *) skb_dequeue(&queue->in)) != NULL)
		sctp_chunk_free(chunk);

	/* If there is a packet which is currently being worked on,
	 * free it as well.
	 */
	if (queue->in_progress)
		sctp_chunk_free(queue->in_progress);

	if (queue->malloced) {
		/* Dump the master memory segment.  */
		kfree(queue);
	}
}

/* Put a new packet in an SCTP inqueue.
 * We assume that packet->sctp_hdr is set and in host byte order.
 */
void sctp_inq_push(struct sctp_inq *q, struct sctp_chunk *packet)
{
	/* Directly call the packet handling routine. */

	/* We are now calling this either from the soft interrupt
	 * or from the backlog processing.
	 * Eventually, we should clean up inqueue to not rely
	 * on the BH related data structures.
	 */
	skb_queue_tail(&(q->in), (struct sk_buff *) packet);
	q->immediate.func(q->immediate.data);
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
		}
	}

	/* Do we need to take the next packet out of the queue to process? */
	if (!chunk) {
		/* Is the queue empty?  */
        	if (skb_queue_empty(&queue->in))
			return NULL;

		chunk = queue->in_progress =
			(struct sctp_chunk *) skb_dequeue(&queue->in);

		/* This is the first chunk in the packet.  */
		chunk->singleton = 1;
		ch = (sctp_chunkhdr_t *) chunk->skb->data;
	}

        chunk->chunk_hdr = ch;
        chunk->chunk_end = ((__u8 *)ch) + WORD_ROUND(ntohs(ch->length));
	/* In the unlikely case of an IP reassembly, the skb could be
	 * non-linear. If so, update chunk_end so that it doesn't go past
	 * the skb->tail.
	 */
	if (unlikely(skb_is_nonlinear(chunk->skb))) {
		if (chunk->chunk_end > chunk->skb->tail)
			chunk->chunk_end = chunk->skb->tail;
	}
	skb_pull(chunk->skb, sizeof(sctp_chunkhdr_t));
	chunk->subh.v = NULL; /* Subheader is no longer valid.  */

	if (chunk->chunk_end < chunk->skb->tail) {
		/* This is not a singleton */
		chunk->singleton = 0;
	} else if (chunk->chunk_end > chunk->skb->tail) {
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

	SCTP_DEBUG_PRINTK("+++sctp_inq_pop+++ chunk %p[%s],"
			  " length %d, skb->len %d\n",chunk,
			  sctp_cname(SCTP_ST_CHUNK(chunk->chunk_hdr->type)),
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
void sctp_inq_set_th_handler(struct sctp_inq *q,
				 void (*callback)(void *), void *arg)
{
	INIT_WORK(&q->immediate, callback, arg);
}

