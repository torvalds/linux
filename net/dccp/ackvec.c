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

#include "ackvec.h"
#include "dccp.h"

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/slab.h>

#include <net/sock.h>

static struct kmem_cache *dccp_ackvec_slab;
static struct kmem_cache *dccp_ackvec_record_slab;

struct dccp_ackvec *dccp_ackvec_alloc(const gfp_t priority)
{
	struct dccp_ackvec *av = kmem_cache_zalloc(dccp_ackvec_slab, priority);

	if (av != NULL) {
		av->av_buf_head	= DCCPAV_MAX_ACKVEC_LEN - 1;
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

static void dccp_ackvec_insert_avr(struct dccp_ackvec *av,
				   struct dccp_ackvec_record *avr)
{
	/*
	 * AVRs are sorted by seqno. Since we are sending them in order, we
	 * just add the AVR at the head of the list.
	 * -sorbo.
	 */
	if (!list_empty(&av->av_records)) {
		const struct dccp_ackvec_record *head =
					list_entry(av->av_records.next,
						   struct dccp_ackvec_record,
						   avr_node);
		BUG_ON(before48(avr->avr_ack_seqno, head->avr_ack_seqno));
	}

	list_add(&avr->avr_node, &av->av_records);
}

int dccp_insert_option_ackvec(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct dccp_ackvec *av = dp->dccps_hc_rx_ackvec;
	/* Figure out how many options do we need to represent the ackvec */
	const u8 nr_opts = DIV_ROUND_UP(av->av_vec_len, DCCP_SINGLE_OPT_MAXLEN);
	u16 len = av->av_vec_len + 2 * nr_opts;
	u8 i, nonce = 0;
	const unsigned char *tail, *from;
	unsigned char *to;
	struct dccp_ackvec_record *avr;

	if (DCCP_SKB_CB(skb)->dccpd_opt_len + len > DCCP_MAX_OPT_LEN)
		return -1;

	avr = kmem_cache_alloc(dccp_ackvec_record_slab, GFP_ATOMIC);
	if (avr == NULL)
		return -1;

	DCCP_SKB_CB(skb)->dccpd_opt_len += len;

	to   = skb_push(skb, len);
	len  = av->av_vec_len;
	from = av->av_buf + av->av_buf_head;
	tail = av->av_buf + DCCPAV_MAX_ACKVEC_LEN;

	for (i = 0; i < nr_opts; ++i) {
		int copylen = len;

		if (len > DCCP_SINGLE_OPT_MAXLEN)
			copylen = DCCP_SINGLE_OPT_MAXLEN;

		/*
		 * RFC 4340, 12.2: Encode the Nonce Echo for this Ack Vector via
		 * its type; ack_nonce is the sum of all individual buf_nonce's.
		 */
		nonce ^= av->av_buf_nonce[i];

		*to++ = DCCPO_ACK_VECTOR_0 + av->av_buf_nonce[i];
		*to++ = copylen + 2;

		/* Check if buf_head wraps */
		if (from + copylen > tail) {
			const u16 tailsize = tail - from;

			memcpy(to, from, tailsize);
			to	+= tailsize;
			len	-= tailsize;
			copylen	-= tailsize;
			from	= av->av_buf;
		}

		memcpy(to, from, copylen);
		from += copylen;
		to   += copylen;
		len  -= copylen;
	}

	/*
	 * Each sent Ack Vector is recorded in the list, as per A.2 of RFC 4340.
	 */
	avr->avr_ack_seqno  = DCCP_SKB_CB(skb)->dccpd_seq;
	avr->avr_ack_ptr    = av->av_buf_head;
	avr->avr_ack_ackno  = av->av_buf_ackno;
	avr->avr_ack_nonce  = nonce;
	avr->avr_ack_runlen = dccp_ackvec_runlen(av->av_buf + av->av_buf_head);

	dccp_ackvec_insert_avr(av, avr);

	dccp_pr_debug("%s ACK Vector 0, len=%d, ack_seqno=%llu, "
		      "ack_ackno=%llu\n",
		      dccp_role(sk), avr->avr_ack_runlen,
		      (unsigned long long)avr->avr_ack_seqno,
		      (unsigned long long)avr->avr_ack_ackno);
	return 0;
}

/*
 * If several packets are missing, the HC-Receiver may prefer to enter multiple
 * bytes with run length 0, rather than a single byte with a larger run length;
 * this simplifies table updates if one of the missing packets arrives.
 */
static inline int dccp_ackvec_set_buf_head_state(struct dccp_ackvec *av,
						 const unsigned int packets,
						 const unsigned char state)
{
	unsigned int gap;
	long new_head;

	if (av->av_vec_len + packets > DCCPAV_MAX_ACKVEC_LEN)
		return -ENOBUFS;

	gap	 = packets - 1;
	new_head = av->av_buf_head - packets;

	if (new_head < 0) {
		if (gap > 0) {
			memset(av->av_buf, DCCPAV_NOT_RECEIVED,
			       gap + new_head + 1);
			gap = -new_head;
		}
		new_head += DCCPAV_MAX_ACKVEC_LEN;
	}

	av->av_buf_head = new_head;

	if (gap > 0)
		memset(av->av_buf + av->av_buf_head + 1,
		       DCCPAV_NOT_RECEIVED, gap);

	av->av_buf[av->av_buf_head] = state;
	av->av_vec_len += packets;
	return 0;
}

/*
 * Implements the RFC 4340, Appendix A
 */
int dccp_ackvec_add(struct dccp_ackvec *av, const struct sock *sk,
		    const u64 ackno, const u8 state)
{
	u8 *cur_head = av->av_buf + av->av_buf_head,
	   *buf_end  = av->av_buf + DCCPAV_MAX_ACKVEC_LEN;
	/*
	 * Check at the right places if the buffer is full, if it is, tell the
	 * caller to start dropping packets till the HC-Sender acks our ACK
	 * vectors, when we will free up space in av_buf.
	 *
	 * We may well decide to do buffer compression, etc, but for now lets
	 * just drop.
	 *
	 * From Appendix A.1.1 (`New Packets'):
	 *
	 *	Of course, the circular buffer may overflow, either when the
	 *	HC-Sender is sending data at a very high rate, when the
	 *	HC-Receiver's acknowledgements are not reaching the HC-Sender,
	 *	or when the HC-Sender is forgetting to acknowledge those acks
	 *	(so the HC-Receiver is unable to clean up old state). In this
	 *	case, the HC-Receiver should either compress the buffer (by
	 *	increasing run lengths when possible), transfer its state to
	 *	a larger buffer, or, as a last resort, drop all received
	 *	packets, without processing them whatsoever, until its buffer
	 *	shrinks again.
	 */

	/* See if this is the first ackno being inserted */
	if (av->av_vec_len == 0) {
		*cur_head = state;
		av->av_vec_len = 1;
	} else if (after48(ackno, av->av_buf_ackno)) {
		const u64 delta = dccp_delta_seqno(av->av_buf_ackno, ackno);

		/*
		 * Look if the state of this packet is the same as the
		 * previous ackno and if so if we can bump the head len.
		 */
		if (delta == 1 && dccp_ackvec_state(cur_head) == state &&
		    dccp_ackvec_runlen(cur_head) < DCCPAV_MAX_RUNLEN)
			*cur_head += 1;
		else if (dccp_ackvec_set_buf_head_state(av, delta, state))
			return -ENOBUFS;
	} else {
		/*
		 * A.1.2.  Old Packets
		 *
		 *	When a packet with Sequence Number S <= buf_ackno
		 *	arrives, the HC-Receiver will scan the table for
		 *	the byte corresponding to S. (Indexing structures
		 *	could reduce the complexity of this scan.)
		 */
		u64 delta = dccp_delta_seqno(ackno, av->av_buf_ackno);

		while (1) {
			const u8 len = dccp_ackvec_runlen(cur_head);
			/*
			 * valid packets not yet in av_buf have a reserved
			 * entry, with a len equal to 0.
			 */
			if (*cur_head == DCCPAV_NOT_RECEIVED && delta == 0) {
				dccp_pr_debug("Found %llu reserved seat!\n",
					      (unsigned long long)ackno);
				*cur_head = state;
				goto out;
			}
			/* len == 0 means one packet */
			if (delta < len + 1)
				goto out_duplicate;

			delta -= len + 1;
			if (++cur_head == buf_end)
				cur_head = av->av_buf;
		}
	}

	av->av_buf_ackno = ackno;
out:
	return 0;

out_duplicate:
	/* Duplicate packet */
	dccp_pr_debug("Received a dup or already considered lost "
		      "packet: %llu\n", (unsigned long long)ackno);
	return -EILSEQ;
}

static void dccp_ackvec_throw_record(struct dccp_ackvec *av,
				     struct dccp_ackvec_record *avr)
{
	struct dccp_ackvec_record *next;

	/* sort out vector length */
	if (av->av_buf_head <= avr->avr_ack_ptr)
		av->av_vec_len = avr->avr_ack_ptr - av->av_buf_head;
	else
		av->av_vec_len = DCCPAV_MAX_ACKVEC_LEN - 1 -
				 av->av_buf_head + avr->avr_ack_ptr;

	/* free records */
	list_for_each_entry_safe_from(avr, next, &av->av_records, avr_node) {
		list_del(&avr->avr_node);
		kmem_cache_free(dccp_ackvec_record_slab, avr);
	}
}

void dccp_ackvec_check_rcv_ackno(struct dccp_ackvec *av, struct sock *sk,
				 const u64 ackno)
{
	struct dccp_ackvec_record *avr;

	/*
	 * If we traverse backwards, it should be faster when we have large
	 * windows. We will be receiving ACKs for stuff we sent a while back
	 * -sorbo.
	 */
	list_for_each_entry_reverse(avr, &av->av_records, avr_node) {
		if (ackno == avr->avr_ack_seqno) {
			dccp_pr_debug("%s ACK packet 0, len=%d, ack_seqno=%llu, "
				      "ack_ackno=%llu, ACKED!\n",
				      dccp_role(sk), avr->avr_ack_runlen,
				      (unsigned long long)avr->avr_ack_seqno,
				      (unsigned long long)avr->avr_ack_ackno);
			dccp_ackvec_throw_record(av, avr);
			break;
		} else if (avr->avr_ack_seqno > ackno)
			break; /* old news */
	}
}

static void dccp_ackvec_check_rcv_ackvector(struct dccp_ackvec *av,
					    struct sock *sk, u64 *ackno,
					    const unsigned char len,
					    const unsigned char *vector)
{
	unsigned char i;
	struct dccp_ackvec_record *avr;

	/* Check if we actually sent an ACK vector */
	if (list_empty(&av->av_records))
		return;

	i = len;
	/*
	 * XXX
	 * I think it might be more efficient to work backwards. See comment on
	 * rcv_ackno. -sorbo.
	 */
	avr = list_entry(av->av_records.next, struct dccp_ackvec_record, avr_node);
	while (i--) {
		const u8 rl = dccp_ackvec_runlen(vector);
		u64 ackno_end_rl;

		dccp_set_seqno(&ackno_end_rl, *ackno - rl);

		/*
		 * If our AVR sequence number is greater than the ack, go
		 * forward in the AVR list until it is not so.
		 */
		list_for_each_entry_from(avr, &av->av_records, avr_node) {
			if (!after48(avr->avr_ack_seqno, *ackno))
				goto found;
		}
		/* End of the av_records list, not found, exit */
		break;
found:
		if (between48(avr->avr_ack_seqno, ackno_end_rl, *ackno)) {
			if (dccp_ackvec_state(vector) != DCCPAV_NOT_RECEIVED) {
				dccp_pr_debug("%s ACK vector 0, len=%d, "
					      "ack_seqno=%llu, ack_ackno=%llu, "
					      "ACKED!\n",
					      dccp_role(sk), len,
					      (unsigned long long)
					      avr->avr_ack_seqno,
					      (unsigned long long)
					      avr->avr_ack_ackno);
				dccp_ackvec_throw_record(av, avr);
				break;
			}
			/*
			 * If it wasn't received, continue scanning... we might
			 * find another one.
			 */
		}

		dccp_set_seqno(ackno, ackno_end_rl - 1);
		++vector;
	}
}

int dccp_ackvec_parse(struct sock *sk, const struct sk_buff *skb,
		      u64 *ackno, const u8 opt, const u8 *value, const u8 len)
{
	if (len > DCCP_SINGLE_OPT_MAXLEN)
		return -1;

	/* dccp_ackvector_print(DCCP_SKB_CB(skb)->dccpd_ack_seq, value, len); */
	dccp_ackvec_check_rcv_ackvector(dccp_sk(sk)->dccps_hc_rx_ackvec, sk,
					ackno, len, value);
	return 0;
}

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
	if (dccp_ackvec_slab != NULL) {
		kmem_cache_destroy(dccp_ackvec_slab);
		dccp_ackvec_slab = NULL;
	}
	if (dccp_ackvec_record_slab != NULL) {
		kmem_cache_destroy(dccp_ackvec_record_slab);
		dccp_ackvec_record_slab = NULL;
	}
}
