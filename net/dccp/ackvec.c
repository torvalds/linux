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
#include <linux/skbuff.h>

#include <net/sock.h>

int dccp_insert_option_ackvec(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct dccp_ackvec *av = dp->dccps_hc_rx_ackvec;
	int len = av->dccpav_vec_len + 2;
	struct timeval now;
	u32 elapsed_time;
	unsigned char *to, *from;

	dccp_timestamp(sk, &now);
	elapsed_time = timeval_delta(&now, &av->dccpav_time) / 10;

	if (elapsed_time != 0)
		dccp_insert_option_elapsed_time(sk, skb, elapsed_time);

	if (DCCP_SKB_CB(skb)->dccpd_opt_len + len > DCCP_MAX_OPT_LEN)
		return -1;

	/*
	 * XXX: now we have just one ack vector sent record, so
	 * we have to wait for it to be cleared.
	 *
	 * Of course this is not acceptable, but this is just for
	 * basic testing now.
	 */
	if (av->dccpav_ack_seqno != DCCP_MAX_SEQNO + 1)
		return -1;

	DCCP_SKB_CB(skb)->dccpd_opt_len += len;

	to    = skb_push(skb, len);
	*to++ = DCCPO_ACK_VECTOR_0;
	*to++ = len;

	len  = av->dccpav_vec_len;
	from = av->dccpav_buf + av->dccpav_buf_head;

	/* Check if buf_head wraps */
	if ((int)av->dccpav_buf_head + len > av->dccpav_vec_len) {
		const u32 tailsize = av->dccpav_vec_len - av->dccpav_buf_head;

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
	 *
	 * This implemention uses just one ack record for now.
	 */
	av->dccpav_ack_seqno = DCCP_SKB_CB(skb)->dccpd_seq;
	av->dccpav_ack_ptr   = av->dccpav_buf_head;
	av->dccpav_ack_ackno = av->dccpav_buf_ackno;
	av->dccpav_ack_nonce = av->dccpav_buf_nonce;
	av->dccpav_sent_len  = av->dccpav_vec_len;

	dccp_pr_debug("%sACK Vector 0, len=%d, ack_seqno=%llu, "
		      "ack_ackno=%llu\n",
		      debug_prefix, av->dccpav_sent_len,
		      (unsigned long long)av->dccpav_ack_seqno,
		      (unsigned long long)av->dccpav_ack_ackno);
	return -1;
}

struct dccp_ackvec *dccp_ackvec_alloc(const unsigned int len,
				      const gfp_t priority)
{
	struct dccp_ackvec *av;

	BUG_ON(len == 0);

	if (len > DCCP_MAX_ACKVEC_LEN)
		return NULL;

	av = kmalloc(sizeof(*av) + len, priority);
	if (av != NULL) {
		av->dccpav_buf_len	= len;
		av->dccpav_buf_head	=
			av->dccpav_buf_tail = av->dccpav_buf_len - 1;
		av->dccpav_buf_ackno	=
			av->dccpav_ack_ackno = av->dccpav_ack_seqno = ~0LLU;
		av->dccpav_buf_nonce = av->dccpav_buf_nonce = 0;
		av->dccpav_ack_ptr	= 0;
		av->dccpav_time.tv_sec	= 0;
		av->dccpav_time.tv_usec	= 0;
		av->dccpav_sent_len	= av->dccpav_vec_len = 0;
	}

	return av;
}

void dccp_ackvec_free(struct dccp_ackvec *av)
{
	kfree(av);
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
	signed long new_head;

	if (av->dccpav_vec_len + packets > av->dccpav_buf_len)
		return -ENOBUFS;

	gap	 = packets - 1;
	new_head = av->dccpav_buf_head - packets;

	if (new_head < 0) {
		if (gap > 0) {
			memset(av->dccpav_buf, DCCP_ACKVEC_STATE_NOT_RECEIVED,
			       gap + new_head + 1);
			gap = -new_head;
		}
		new_head += av->dccpav_buf_len;
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
			if (++index == av->dccpav_buf_len)
				index = 0;
		}
	}

	av->dccpav_buf_ackno = ackno;
	dccp_timestamp(sk, &av->dccpav_time);
out:
	dccp_pr_debug("");
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

static void dccp_ackvec_throw_away_ack_record(struct dccp_ackvec *av)
{
	/*
	 * As we're keeping track of the ack vector size (dccpav_vec_len) and
	 * the sent ack vector size (dccpav_sent_len) we don't need
	 * dccpav_buf_tail at all, but keep this code here as in the future
	 * we'll implement a vector of ack records, as suggested in
	 * draft-ietf-dccp-spec-11.txt Appendix A. -acme
	 */
#if 0
	u32 new_buf_tail = av->dccpav_ack_ptr + 1;
	if (new_buf_tail >= av->dccpav_vec_len)
		new_buf_tail -= av->dccpav_vec_len;
	av->dccpav_buf_tail = new_buf_tail;
#endif
	av->dccpav_vec_len -= av->dccpav_sent_len;
}

void dccp_ackvec_check_rcv_ackno(struct dccp_ackvec *av, struct sock *sk,
				 const u64 ackno)
{
	/* Check if we actually sent an ACK vector */
	if (av->dccpav_ack_seqno == DCCP_MAX_SEQNO + 1)
		return;

	if (ackno == av->dccpav_ack_seqno) {
#ifdef CONFIG_IP_DCCP_DEBUG
		struct dccp_sock *dp = dccp_sk(sk);
		const char *debug_prefix = dp->dccps_role == DCCP_ROLE_CLIENT ?
					"CLIENT rx ack: " : "server rx ack: ";
#endif
		dccp_pr_debug("%sACK packet 0, len=%d, ack_seqno=%llu, "
			      "ack_ackno=%llu, ACKED!\n",
			      debug_prefix, 1,
			      (unsigned long long)av->dccpav_ack_seqno,
			      (unsigned long long)av->dccpav_ack_ackno);
		dccp_ackvec_throw_away_ack_record(av);
		av->dccpav_ack_seqno = DCCP_MAX_SEQNO + 1;
	}
}

static void dccp_ackvec_check_rcv_ackvector(struct dccp_ackvec *av,
					    struct sock *sk, u64 ackno,
					    const unsigned char len,
					    const unsigned char *vector)
{
	unsigned char i;

	/* Check if we actually sent an ACK vector */
	if (av->dccpav_ack_seqno == DCCP_MAX_SEQNO + 1)
		return;
	/*
	 * We're in the receiver half connection, so if the received an ACK
	 * vector ackno (e.g. 50) before dccpav_ack_seqno (e.g. 52), we're
	 * not interested.
	 *
	 * Extra explanation with example:
	 * 
	 * if we received an ACK vector with ackno 50, it can only be acking
	 * 50, 49, 48, etc, not 52 (the seqno for the ACK vector we sent).
	 */
	/* dccp_pr_debug("is %llu < %llu? ", ackno, av->dccpav_ack_seqno); */
	if (before48(ackno, av->dccpav_ack_seqno)) {
		/* dccp_pr_debug_cat("yes\n"); */
		return;
	}
	/* dccp_pr_debug_cat("no\n"); */

	i = len;
	while (i--) {
		const u8 rl = *vector & DCCP_ACKVEC_LEN_MASK;
		u64 ackno_end_rl;

		dccp_set_seqno(&ackno_end_rl, ackno - rl);

		/*
		 * dccp_pr_debug("is %llu <= %llu <= %llu? ", ackno_end_rl,
		 * av->dccpav_ack_seqno, ackno);
		 */
		if (between48(av->dccpav_ack_seqno, ackno_end_rl, ackno)) {
			const u8 state = (*vector &
					  DCCP_ACKVEC_STATE_MASK) >> 6;
			/* dccp_pr_debug_cat("yes\n"); */

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
					      av->dccpav_ack_seqno,
					      (unsigned long long)
					      av->dccpav_ack_ackno);
				dccp_ackvec_throw_away_ack_record(av);
			}
			/*
			 * If dccpav_ack_seqno was not received, no problem
			 * we'll send another ACK vector.
			 */
			av->dccpav_ack_seqno = DCCP_MAX_SEQNO + 1;
			break;
		}
		/* dccp_pr_debug_cat("no\n"); */

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
