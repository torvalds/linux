/*
 *  net/dccp/ackvec.c
 *
 *  An implementation of the DCCP protocol
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@ghostprotocols.net>
 *
 *      This program is free software; you can redistribute it and/or modify it
 *      under the terms of the GNU General Public License as published by the
 *      Free Software Foundation; version 2 of the License;
 */

#include "ackvec.h"
#include "dccp.h"

#include <linux/dccp.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/slab.h>

#include <net/sock.h>

static kmem_cache_t *dccp_ackvec_slab;
static kmem_cache_t *dccp_ackvec_record_slab;

static struct dccp_ackvec_record *dccp_ackvec_record_new(void)
{
	struct dccp_ackvec_record *avr =
			kmem_cache_alloc(dccp_ackvec_record_slab, GFP_ATOMIC);

	if (avr != NULL)
		INIT_LIST_HEAD(&avr->dccpavr_node);

	return avr;
}

static void dccp_ackvec_record_delete(struct dccp_ackvec_record *avr)
{
	if (unlikely(avr == NULL))
		return;
	/* Check if deleting a linked record */
	WARN_ON(!list_empty(&avr->dccpavr_node));
	kmem_cache_free(dccp_ackvec_record_slab, avr);
}

static void dccp_ackvec_insert_avr(struct dccp_ackvec *av,
				   struct dccp_ackvec_record *avr)
{
	/*
	 * AVRs are sorted by seqno. Since we are sending them in order, we
	 * just add the AVR at the head of the list.
	 * -sorbo.
	 */
	if (!list_empty(&av->dccpav_records)) {
		const struct dccp_ackvec_record *head =
					list_entry(av->dccpav_records.next,
						   struct dccp_ackvec_record,
						   dccpavr_node);
		BUG_ON(before48(avr->dccpavr_ack_seqno,
				head->dccpavr_ack_seqno));
	}

	list_add(&avr->dccpavr_node, &av->dccpav_records);
}

int dccp_insert_option_ackvec(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);
#ifdef CONFIG_IP_DCCP_DEBUG
	const char *debug_prefix = dp->dccps_role == DCCP_ROLE_CLIENT ?
				"CLIENT tx: " : "server tx: ";
#endif
	struct dccp_ackvec *av = dp->dccps_hc_rx_ackvec;
	int len = av->dccpav_vec_len + 2;
	struct timeval now;
	u32 elapsed_time;
	unsigned char *to, *from;
	struct dccp_ackvec_record *avr;

	if (DCCP_SKB_CB(skb)->dccpd_opt_len + len > DCCP_MAX_OPT_LEN)
		return -1;

	dccp_timestamp(sk, &now);
	elapsed_time = timeval_delta(&now, &av->dccpav_time) / 10;

	if (elapsed_time != 0 &&
	    dccp_insert_option_elapsed_time(sk, skb, elapsed_time))
		return -1;

	avr = dccp_ackvec_record_new();
	if (avr == NULL)
		return -1;

	DCCP_SKB_CB(skb)->dccpd_opt_len += len;

	to    = skb_push(skb, len);
	*to++ = DCCPO_ACK_VECTOR_0;
	*to++ = len;

	len  = av->dccpav_vec_len;
	from = av->dccpav_buf + av->dccpav_buf_head;

	/* Check if buf_head wraps */
	if ((int)av->dccpav_buf_head + len > DCCP_MAX_ACKVEC_LEN) {
		const u32 tailsize = DCCP_MAX_ACKVEC_LEN - av->dccpav_buf_head;

		memcpy(to, from, tailsize);
		to   += tailsize;
		len  -= tailsize;
		from = av->dccpav_buf;
	}

	memcpy(to, from, len);
	/*
	 *	From draft-ietf-dccp-spec-11.txt:
	 *
	 *	For each acknowledgement it sends, the HC-Receiver will add an
	 *	acknowledgement record.  ack_seqno will equal the HC-Receiver
	 *	sequence number it used for the ack packet; ack_ptr will equal
	 *	buf_head; ack_ackno will equal buf_ackno; and ack_nonce will
	 *	equal buf_nonce.
	 */
	avr->dccpavr_ack_seqno = DCCP_SKB_CB(skb)->dccpd_seq;
	avr->dccpavr_ack_ptr   = av->dccpav_buf_head;
	avr->dccpavr_ack_ackno = av->dccpav_buf_ackno;
	avr->dccpavr_ack_nonce = av->dccpav_buf_nonce;
	avr->dccpavr_sent_len  = av->dccpav_vec_len;

	dccp_ackvec_insert_avr(av, avr);

	dccp_pr_debug("%sACK Vector 0, len=%d, ack_seqno=%llu, "
		      "ack_ackno=%llu\n",
		      debug_prefix, avr->dccpavr_sent_len,
		      (unsigned long long)avr->dccpavr_ack_seqno,
		      (unsigned long long)avr->dccpavr_ack_ackno);
	return 0;
}

struct dccp_ackvec *dccp_ackvec_alloc(const gfp_t priority)
{
	struct dccp_ackvec *av = kmem_cache_alloc(dccp_ackvec_slab, priority);

	if (av != NULL) {
		av->dccpav_buf_head	=
			av->dccpav_buf_tail = DCCP_MAX_ACKVEC_LEN - 1;
		av->dccpav_buf_ackno	= DCCP_MAX_SEQNO + 1;
		av->dccpav_buf_nonce = av->dccpav_buf_nonce = 0;
		av->dccpav_ack_ptr	= 0;
		av->dccpav_time.tv_sec	= 0;
		av->dccpav_time.tv_usec	= 0;
		av->dccpav_sent_len	= av->dccpav_vec_len = 0;
		INIT_LIST_HEAD(&av->dccpav_records);
	}

	return av;
}

void dccp_ackvec_free(struct dccp_ackvec *av)
{
	if (unlikely(av == NULL))
		return;

	if (!list_empty(&av->dccpav_records)) {
		struct dccp_ackvec_record *avr, *next;

		list_for_each_entry_safe(avr, next, &av->dccpav_records,
					 dccpavr_node) {
			list_del_init(&avr->dccpavr_node);
			dccp_ackvec_record_delete(avr);
		}
	}

	kmem_cache_free(dccp_ackvec_slab, av);
}

static inline u8 dccp_ackvec_state(const struct dccp_ackvec *av,
				   const u8 index)
{
	return av->dccpav_buf[index] & DCCP_ACKVEC_STATE_MASK;
}

static inline u8 dccp_ackvec_len(const struct dccp_ackvec *av,
				 const u8 index)
{
	return av->dccpav_buf[index] & DCCP_ACKVEC_LEN_MASK;
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

	if (av->dccpav_vec_len + packets > DCCP_MAX_ACKVEC_LEN)
		return -ENOBUFS;

	gap	 = packets - 1;
	new_head = av->dccpav_buf_head - packets;

	if (new_head < 0) {
		if (gap > 0) {
			memset(av->dccpav_buf, DCCP_ACKVEC_STATE_NOT_RECEIVED,
			       gap + new_head + 1);
			gap = -new_head;
		}
		new_head += DCCP_MAX_ACKVEC_LEN;
	} 

	av->dccpav_buf_head = new_head;

	if (gap > 0)
		memset(av->dccpav_buf + av->dccpav_buf_head + 1,
		       DCCP_ACKVEC_STATE_NOT_RECEIVED, gap);

	av->dccpav_buf[av->dccpav_buf_head] = state;
	av->dccpav_vec_len += packets;
	return 0;
}

/*
 * Implements the draft-ietf-dccp-spec-11.txt Appendix A
 */
int dccp_ackvec_add(struct dccp_ackvec *av, const struct sock *sk,
		    const u64 ackno, const u8 state)
{
	/*
	 * Check at the right places if the buffer is full, if it is, tell the
	 * caller to start dropping packets till the HC-Sender acks our ACK
	 * vectors, when we will free up space in dccpav_buf.
	 *
	 * We may well decide to do buffer compression, etc, but for now lets
	 * just drop.
	 *
	 * From Appendix A:
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
	if (av->dccpav_vec_len == 0) {
		av->dccpav_buf[av->dccpav_buf_head] = state;
		av->dccpav_vec_len = 1;
	} else if (after48(ackno, av->dccpav_buf_ackno)) {
		const u64 delta = dccp_delta_seqno(av->dccpav_buf_ackno,
						   ackno);

		/*
		 * Look if the state of this packet is the same as the
		 * previous ackno and if so if we can bump the head len.
		 */
		if (delta == 1 &&
		    dccp_ackvec_state(av, av->dccpav_buf_head) == state &&
		    (dccp_ackvec_len(av, av->dccpav_buf_head) <
		     DCCP_ACKVEC_LEN_MASK))
			av->dccpav_buf[av->dccpav_buf_head]++;
		else if (dccp_ackvec_set_buf_head_state(av, delta, state))
			return -ENOBUFS;
	} else {
		/*
		 * A.1.2.  Old Packets
		 *
		 *	When a packet with Sequence Number S arrives, and
		 *	S <= buf_ackno, the HC-Receiver will scan the table
		 *	for the byte corresponding to S. (Indexing structures
		 *	could reduce the complexity of this scan.)
		 */
		u64 delta = dccp_delta_seqno(ackno, av->dccpav_buf_ackno);
		u8 index = av->dccpav_buf_head;

		while (1) {
			const u8 len = dccp_ackvec_len(av, index);
			const u8 state = dccp_ackvec_state(av, index);
			/*
			 * valid packets not yet in dccpav_buf have a reserved
			 * entry, with a len equal to 0.
			 */
			if (state == DCCP_ACKVEC_STATE_NOT_RECEIVED &&
			    len == 0 && delta == 0) { /* Found our
							 reserved seat! */
				dccp_pr_debug("Found %llu reserved seat!\n",
					      (unsigned long long)ackno);
				av->dccpav_buf[index] = state;
				goto out;
			}
			/* len == 0 means one packet */
			if (delta < len + 1)
				goto out_duplicate;

			delta -= len + 1;
			if (++index == DCCP_MAX_ACKVEC_LEN)
				index = 0;
		}
	}

	av->dccpav_buf_ackno = ackno;
	dccp_timestamp(sk, &av->dccpav_time);
out:
	return 0;

out_duplicate:
	/* Duplicate packet */
	dccp_pr_debug("Received a dup or already considered lost "
		      "packet: %llu\n", (unsigned long long)ackno);
	return -EILSEQ;
}

#ifdef CONFIG_IP_DCCP_DEBUG
void dccp_ackvector_print(const u64 ackno, const unsigned char *vector, int len)
{
	if (!dccp_debug)
		return;

	printk("ACK vector len=%d, ackno=%llu |", len,
	       (unsigned long long)ackno);

	while (len--) {
		const u8 state = (*vector & DCCP_ACKVEC_STATE_MASK) >> 6;
		const u8 rl = *vector & DCCP_ACKVEC_LEN_MASK;

		printk("%d,%d|", state, rl);
		++vector;
	}

	printk("\n");
}

void dccp_ackvec_print(const struct dccp_ackvec *av)
{
	dccp_ackvector_print(av->dccpav_buf_ackno,
			     av->dccpav_buf + av->dccpav_buf_head,
			     av->dccpav_vec_len);
}
#endif

static void dccp_ackvec_throw_record(struct dccp_ackvec *av,
				     struct dccp_ackvec_record *avr)
{
	struct dccp_ackvec_record *next;

	av->dccpav_buf_tail = avr->dccpavr_ack_ptr - 1;
	if (av->dccpav_buf_tail == 0)
		av->dccpav_buf_tail = DCCP_MAX_ACKVEC_LEN - 1;

	av->dccpav_vec_len -= avr->dccpavr_sent_len;

	/* free records */
	list_for_each_entry_safe_from(avr, next, &av->dccpav_records,
				      dccpavr_node) {
		list_del_init(&avr->dccpavr_node);
		dccp_ackvec_record_delete(avr);
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
	list_for_each_entry_reverse(avr, &av->dccpav_records, dccpavr_node) {
		if (ackno == avr->dccpavr_ack_seqno) {
#ifdef CONFIG_IP_DCCP_DEBUG
			struct dccp_sock *dp = dccp_sk(sk);
			const char *debug_prefix = dp->dccps_role == DCCP_ROLE_CLIENT ?
						"CLIENT rx ack: " : "server rx ack: ";
#endif
			dccp_pr_debug("%sACK packet 0, len=%d, ack_seqno=%llu, "
				      "ack_ackno=%llu, ACKED!\n",
				      debug_prefix, 1,
				      (unsigned long long)avr->dccpavr_ack_seqno,
				      (unsigned long long)avr->dccpavr_ack_ackno);
			dccp_ackvec_throw_record(av, avr);
			break;
		}
	}
}

static void dccp_ackvec_check_rcv_ackvector(struct dccp_ackvec *av,
					    struct sock *sk, u64 ackno,
					    const unsigned char len,
					    const unsigned char *vector)
{
	unsigned char i;
	struct dccp_ackvec_record *avr;

	/* Check if we actually sent an ACK vector */
	if (list_empty(&av->dccpav_records))
		return;

	i = len;
	/*
	 * XXX
	 * I think it might be more efficient to work backwards. See comment on
	 * rcv_ackno. -sorbo.
	 */
	avr = list_entry(av->dccpav_records.next, struct dccp_ackvec_record,
			 dccpavr_node);
	while (i--) {
		const u8 rl = *vector & DCCP_ACKVEC_LEN_MASK;
		u64 ackno_end_rl;

		dccp_set_seqno(&ackno_end_rl, ackno - rl);

		/*
		 * If our AVR sequence number is greater than the ack, go
		 * forward in the AVR list until it is not so.
		 */
		list_for_each_entry_from(avr, &av->dccpav_records,
					 dccpavr_node) {
			if (!after48(avr->dccpavr_ack_seqno, ackno))
				goto found;
		}
		/* End of the dccpav_records list, not found, exit */
		break;
found:
		if (between48(avr->dccpavr_ack_seqno, ackno_end_rl, ackno)) {
			const u8 state = (*vector &
					  DCCP_ACKVEC_STATE_MASK) >> 6;
			if (state != DCCP_ACKVEC_STATE_NOT_RECEIVED) {
#ifdef CONFIG_IP_DCCP_DEBUG
				struct dccp_sock *dp = dccp_sk(sk);
				const char *debug_prefix =
					dp->dccps_role == DCCP_ROLE_CLIENT ?
					"CLIENT rx ack: " : "server rx ack: ";
#endif
				dccp_pr_debug("%sACK vector 0, len=%d, "
					      "ack_seqno=%llu, ack_ackno=%llu, "
					      "ACKED!\n",
					      debug_prefix, len,
					      (unsigned long long)
					      avr->dccpavr_ack_seqno,
					      (unsigned long long)
					      avr->dccpavr_ack_ackno);
				dccp_ackvec_throw_record(av, avr);
				break;
			}
			/*
			 * If it wasn't received, continue scanning... we might
			 * find another one.
			 */
		}

		dccp_set_seqno(&ackno, ackno_end_rl - 1);
		++vector;
	}
}

int dccp_ackvec_parse(struct sock *sk, const struct sk_buff *skb,
		      const u8 opt, const u8 *value, const u8 len)
{
	if (len > DCCP_MAX_ACKVEC_LEN)
		return -1;

	/* dccp_ackvector_print(DCCP_SKB_CB(skb)->dccpd_ack_seq, value, len); */
	dccp_ackvec_check_rcv_ackvector(dccp_sk(sk)->dccps_hc_rx_ackvec, sk,
					DCCP_SKB_CB(skb)->dccpd_ack_seq,
				        len, value);
	return 0;
}

static char dccp_ackvec_slab_msg[] __initdata =
	KERN_CRIT "DCCP: Unable to create ack vectors slab caches\n";

int __init dccp_ackvec_init(void)
{
	dccp_ackvec_slab = kmem_cache_create("dccp_ackvec",
					     sizeof(struct dccp_ackvec), 0,
					     SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (dccp_ackvec_slab == NULL)
		goto out_err;

	dccp_ackvec_record_slab =
			kmem_cache_create("dccp_ackvec_record",
					  sizeof(struct dccp_ackvec_record),
					  0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (dccp_ackvec_record_slab == NULL)
		goto out_destroy_slab;

	return 0;

out_destroy_slab:
	kmem_cache_destroy(dccp_ackvec_slab);
	dccp_ackvec_slab = NULL;
out_err:
	printk(dccp_ackvec_slab_msg);
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
