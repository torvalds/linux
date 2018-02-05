/*
 *  net/dccp/ackvec.c
 *
 *  An implementation of Ack Vectors for the DCCP protocol
 *  Copyright (c) 2007 University of Aberdeen, Scotland, UK
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@ghostprotocols.net>
 *
 *      This program is free software; you can redistribute it and/or modify it
 *      under the terms of the GNU General Public License as published by the
 *      Free Software Foundation; version 2 of the License;
 */
#include "dccp.h"
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/export.h>

static struct kmem_cache *dccp_ackvec_slab;
static struct kmem_cache *dccp_ackvec_record_slab;

struct dccp_ackvec *dccp_ackvec_alloc(const gfp_t priority)
{
	struct dccp_ackvec *av = kmem_cache_zalloc(dccp_ackvec_slab, priority);

	if (av != NULL) {
		av->av_buf_head	= av->av_buf_tail = DCCPAV_MAX_ACKVEC_LEN - 1;
		INIT_LIST_HEAD(&av->av_records);
	}
	return av;
}

static void dccp_ackvec_purge_records(struct dccp_ackvec *av)
{
	struct dccp_ackvec_record *cur, *next;

	list_for_each_entry_safe(cur, next, &av->av_records, avr_node)
		kmem_cache_free(dccp_ackvec_record_slab, cur);
	INIT_LIST_HEAD(&av->av_records);
}

void dccp_ackvec_free(struct dccp_ackvec *av)
{
	if (likely(av != NULL)) {
		dccp_ackvec_purge_records(av);
		kmem_cache_free(dccp_ackvec_slab, av);
	}
}

/**
 * dccp_ackvec_update_records  -  Record information about sent Ack Vectors
 * @av:		Ack Vector records to update
 * @seqno:	Sequence number of the packet carrying the Ack Vector just sent
 * @nonce_sum:	The sum of all buffer nonces contained in the Ack Vector
 */
int dccp_ackvec_update_records(struct dccp_ackvec *av, u64 seqno, u8 nonce_sum)
{
	struct dccp_ackvec_record *avr;

	avr = kmem_cache_alloc(dccp_ackvec_record_slab, GFP_ATOMIC);
	if (avr == NULL)
		return -ENOBUFS;

	avr->avr_ack_seqno  = seqno;
	avr->avr_ack_ptr    = av->av_buf_head;
	avr->avr_ack_ackno  = av->av_buf_ackno;
	avr->avr_ack_nonce  = nonce_sum;
	avr->avr_ack_runlen = dccp_ackvec_runlen(av->av_buf + av->av_buf_head);
	/*
	 * When the buffer overflows, we keep no more than one record. This is
	 * the simplest way of disambiguating sender-Acks dating from before the
	 * overflow from sender-Acks which refer to after the overflow; a simple
	 * solution is preferable here since we are handling an exception.
	 */
	if (av->av_overflow)
		dccp_ackvec_purge_records(av);
	/*
	 * Since GSS is incremented for each packet, the list is automatically
	 * arranged in descending order of @ack_seqno.
	 */
	list_add(&avr->avr_node, &av->av_records);

	dccp_pr_debug("Added Vector, ack_seqno=%llu, ack_ackno=%llu (rl=%u)\n",
		      (unsigned long long)avr->avr_ack_seqno,
		      (unsigned long long)avr->avr_ack_ackno,
		      avr->avr_ack_runlen);
	return 0;
}

static struct dccp_ackvec_record *dccp_ackvec_lookup(struct list_head *av_list,
						     const u64 ackno)
{
	struct dccp_ackvec_record *avr;
	/*
	 * Exploit that records are inserted in descending order of sequence
	 * number, start with the oldest record first. If @ackno is `before'
	 * the earliest ack_ackno, the packet is too old to be considered.
	 */
	list_for_each_entry_reverse(avr, av_list, avr_node) {
		if (avr->avr_ack_seqno == ackno)
			return avr;
		if (before48(ackno, avr->avr_ack_seqno))
			break;
	}
	return NULL;
}

/*
 * Buffer index and length computation using modulo-buffersize arithmetic.
 * Note that, as pointers move from right to left, head is `before' tail.
 */
static inline u16 __ackvec_idx_add(const u16 a, const u16 b)
{
	return (a + b) % DCCPAV_MAX_ACKVEC_LEN;
}

static inline u16 __ackvec_idx_sub(const u16 a, const u16 b)
{
	return __ackvec_idx_add(a, DCCPAV_MAX_ACKVEC_LEN - b);
}

u16 dccp_ackvec_buflen(const struct dccp_ackvec *av)
{
	if (unlikely(av->av_overflow))
		return DCCPAV_MAX_ACKVEC_LEN;
	return __ackvec_idx_sub(av->av_buf_tail, av->av_buf_head);
}

/**
 * dccp_ackvec_update_old  -  Update previous state as per RFC 4340, 11.4.1
 * @av:		non-empty buffer to update
 * @distance:   negative or zero distance of @seqno from buf_ackno downward
 * @seqno:	the (old) sequence number whose record is to be updated
 * @state:	state in which packet carrying @seqno was received
 */
static void dccp_ackvec_update_old(struct dccp_ackvec *av, s64 distance,
				   u64 seqno, enum dccp_ackvec_states state)
{
	u16 ptr = av->av_buf_head;

	BUG_ON(distance > 0);
	if (unlikely(dccp_ackvec_is_empty(av)))
		return;

	do {
		u8 runlen = dccp_ackvec_runlen(av->av_buf + ptr);

		if (distance + runlen >= 0) {
			/*
			 * Only update the state if packet has not been received
			 * yet. This is OK as per the second table in RFC 4340,
			 * 11.4.1; i.e. here we are using the following table:
			 *                     RECEIVED
			 *                      0   1   3
			 *              S     +---+---+---+
			 *              T   0 | 0 | 0 | 0 |
			 *              O     +---+---+---+
			 *              R   1 | 1 | 1 | 1 |
			 *              E     +---+---+---+
			 *              D   3 | 0 | 1 | 3 |
			 *                    +---+---+---+
			 * The "Not Received" state was set by reserve_seats().
			 */
			if (av->av_buf[ptr] == DCCPAV_NOT_RECEIVED)
				av->av_buf[ptr] = state;
			else
				dccp_pr_debug("Not changing %llu state to %u\n",
					      (unsigned long long)seqno, state);
			break;
		}

		distance += runlen + 1;
		ptr	  = __ackvec_idx_add(ptr, 1);

	} while (ptr != av->av_buf_tail);
}

/* Mark @num entries after buf_head as "Not yet received". */
static void dccp_ackvec_reserve_seats(struct dccp_ackvec *av, u16 num)
{
	u16 start = __ackvec_idx_add(av->av_buf_head, 1),
	    len	  = DCCPAV_MAX_ACKVEC_LEN - start;

	/* check for buffer wrap-around */
	if (num > len) {
		memset(av->av_buf + start, DCCPAV_NOT_RECEIVED, len);
		start = 0;
		num  -= len;
	}
	if (num)
		memset(av->av_buf + start, DCCPAV_NOT_RECEIVED, num);
}

/**
 * dccp_ackvec_add_new  -  Record one or more new entries in Ack Vector buffer
 * @av:		 container of buffer to update (can be empty or non-empty)
 * @num_packets: number of packets to register (must be >= 1)
 * @seqno:	 sequence number of the first packet in @num_packets
 * @state:	 state in which packet carrying @seqno was received
 */
static void dccp_ackvec_add_new(struct dccp_ackvec *av, u32 num_packets,
				u64 seqno, enum dccp_ackvec_states state)
{
	u32 num_cells = num_packets;

	if (num_packets > DCCPAV_BURST_THRESH) {
		u32 lost_packets = num_packets - 1;

		DCCP_WARN("Warning: large burst loss (%u)\n", lost_packets);
		/*
		 * We received 1 packet and have a loss of size "num_packets-1"
		 * which we squeeze into num_cells-1 rather than reserving an
		 * entire byte for each lost packet.
		 * The reason is that the vector grows in O(burst_length); when
		 * it grows too large there will no room left for the payload.
		 * This is a trade-off: if a few packets out of the burst show
		 * up later, their state will not be changed; it is simply too
		 * costly to reshuffle/reallocate/copy the buffer each time.
		 * Should such problems persist, we will need to switch to a
		 * different underlying data structure.
		 */
		for (num_packets = num_cells = 1; lost_packets; ++num_cells) {
			u8 len = min_t(u32, lost_packets, DCCPAV_MAX_RUNLEN);

			av->av_buf_head = __ackvec_idx_sub(av->av_buf_head, 1);
			av->av_buf[av->av_buf_head] = DCCPAV_NOT_RECEIVED | len;

			lost_packets -= len;
		}
	}

	if (num_cells + dccp_ackvec_buflen(av) >= DCCPAV_MAX_ACKVEC_LEN) {
		DCCP_CRIT("Ack Vector buffer overflow: dropping old entries");
		av->av_overflow = true;
	}

	av->av_buf_head = __ackvec_idx_sub(av->av_buf_head, num_packets);
	if (av->av_overflow)
		av->av_buf_tail = av->av_buf_head;

	av->av_buf[av->av_buf_head] = state;
	av->av_buf_ackno	    = seqno;

	if (num_packets > 1)
		dccp_ackvec_reserve_seats(av, num_packets - 1);
}

/**
 * dccp_ackvec_input  -  Register incoming packet in the buffer
 */
void dccp_ackvec_input(struct dccp_ackvec *av, struct sk_buff *skb)
{
	u64 seqno = DCCP_SKB_CB(skb)->dccpd_seq;
	enum dccp_ackvec_states state = DCCPAV_RECEIVED;

	if (dccp_ackvec_is_empty(av)) {
		dccp_ackvec_add_new(av, 1, seqno, state);
		av->av_tail_ackno = seqno;

	} else {
		s64 num_packets = dccp_delta_seqno(av->av_buf_ackno, seqno);
		u8 *current_head = av->av_buf + av->av_buf_head;

		if (num_packets == 1 &&
		    dccp_ackvec_state(current_head) == state &&
		    dccp_ackvec_runlen(current_head) < DCCPAV_MAX_RUNLEN) {

			*current_head   += 1;
			av->av_buf_ackno = seqno;

		} else if (num_packets > 0) {
			dccp_ackvec_add_new(av, num_packets, seqno, state);
		} else {
			dccp_ackvec_update_old(av, num_packets, seqno, state);
		}
	}
}

/**
 * dccp_ackvec_clear_state  -  Perform house-keeping / garbage-collection
 * This routine is called when the peer acknowledges the receipt of Ack Vectors
 * up to and including @ackno. While based on on section A.3 of RFC 4340, here
 * are additional precautions to prevent corrupted buffer state. In particular,
 * we use tail_ackno to identify outdated records; it always marks the earliest
 * packet of group (2) in 11.4.2.
 */
void dccp_ackvec_clear_state(struct dccp_ackvec *av, const u64 ackno)
{
	struct dccp_ackvec_record *avr, *next;
	u8 runlen_now, eff_runlen;
	s64 delta;

	avr = dccp_ackvec_lookup(&av->av_records, ackno);
	if (avr == NULL)
		return;
	/*
	 * Deal with outdated acknowledgments: this arises when e.g. there are
	 * several old records and the acks from the peer come in slowly. In
	 * that case we may still have records that pre-date tail_ackno.
	 */
	delta = dccp_delta_seqno(av->av_tail_ackno, avr->avr_ack_ackno);
	if (delta < 0)
		goto free_records;
	/*
	 * Deal with overlapping Ack Vectors: don't subtract more than the
	 * number of packets between tail_ackno and ack_ackno.
	 */
	eff_runlen = delta < avr->avr_ack_runlen ? delta : avr->avr_ack_runlen;

	runlen_now = dccp_ackvec_runlen(av->av_buf + avr->avr_ack_ptr);
	/*
	 * The run length of Ack Vector cells does not decrease over time. If
	 * the run length is the same as at the time the Ack Vector was sent, we
	 * free the ack_ptr cell. That cell can however not be freed if the run
	 * length has increased: in this case we need to move the tail pointer
	 * backwards (towards higher indices), to its next-oldest neighbour.
	 */
	if (runlen_now > eff_runlen) {

		av->av_buf[avr->avr_ack_ptr] -= eff_runlen + 1;
		av->av_buf_tail = __ackvec_idx_add(avr->avr_ack_ptr, 1);

		/* This move may not have cleared the overflow flag. */
		if (av->av_overflow)
			av->av_overflow = (av->av_buf_head == av->av_buf_tail);
	} else {
		av->av_buf_tail	= avr->avr_ack_ptr;
		/*
		 * We have made sure that avr points to a valid cell within the
		 * buffer. This cell is either older than head, or equals head
		 * (empty buffer): in both cases we no longer have any overflow.
		 */
		av->av_overflow	= 0;
	}

	/*
	 * The peer has acknowledged up to and including ack_ackno. Hence the
	 * first packet in group (2) of 11.4.2 is the successor of ack_ackno.
	 */
	av->av_tail_ackno = ADD48(avr->avr_ack_ackno, 1);

free_records:
	list_for_each_entry_safe_from(avr, next, &av->av_records, avr_node) {
		list_del(&avr->avr_node);
		kmem_cache_free(dccp_ackvec_record_slab, avr);
	}
}

/*
 *	Routines to keep track of Ack Vectors received in an skb
 */
int dccp_ackvec_parsed_add(struct list_head *head, u8 *vec, u8 len, u8 nonce)
{
	struct dccp_ackvec_parsed *new = kmalloc(sizeof(*new), GFP_ATOMIC);

	if (new == NULL)
		return -ENOBUFS;
	new->vec   = vec;
	new->len   = len;
	new->nonce = nonce;

	list_add_tail(&new->node, head);
	return 0;
}
EXPORT_SYMBOL_GPL(dccp_ackvec_parsed_add);

void dccp_ackvec_parsed_cleanup(struct list_head *parsed_chunks)
{
	struct dccp_ackvec_parsed *cur, *next;

	list_for_each_entry_safe(cur, next, parsed_chunks, node)
		kfree(cur);
	INIT_LIST_HEAD(parsed_chunks);
}
EXPORT_SYMBOL_GPL(dccp_ackvec_parsed_cleanup);

int __init dccp_ackvec_init(void)
{
	dccp_ackvec_slab = kmem_cache_create("dccp_ackvec",
					     sizeof(struct dccp_ackvec), 0,
					     SLAB_HWCACHE_ALIGN, NULL);
	if (dccp_ackvec_slab == NULL)
		goto out_err;

	dccp_ackvec_record_slab = kmem_cache_create("dccp_ackvec_record",
					     sizeof(struct dccp_ackvec_record),
					     0, SLAB_HWCACHE_ALIGN, NULL);
	if (dccp_ackvec_record_slab == NULL)
		goto out_destroy_slab;

	return 0;

out_destroy_slab:
	kmem_cache_destroy(dccp_ackvec_slab);
	dccp_ackvec_slab = NULL;
out_err:
	DCCP_CRIT("Unable to create Ack Vector slab cache");
	return -ENOBUFS;
}

void dccp_ackvec_exit(void)
{
	kmem_cache_destroy(dccp_ackvec_slab);
	dccp_ackvec_slab = NULL;
	kmem_cache_destroy(dccp_ackvec_record_slab);
	dccp_ackvec_record_slab = NULL;
}
