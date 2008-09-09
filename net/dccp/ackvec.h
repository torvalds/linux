#ifndef _ACKVEC_H
#define _ACKVEC_H
/*
 *  net/dccp/ackvec.h
 *
 *  An implementation of the DCCP protocol
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@mandriva.com>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 */

#include <linux/compiler.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/types.h>

/* Read about the ECN nonce to see why it is 253 */
#define DCCP_MAX_ACKVEC_OPT_LEN 253
/* We can spread an ack vector across multiple options */
#define DCCP_MAX_ACKVEC_LEN (DCCP_MAX_ACKVEC_OPT_LEN * 2)

#define DCCP_ACKVEC_STATE_RECEIVED	0
#define DCCP_ACKVEC_STATE_ECN_MARKED	(1 << 6)
#define DCCP_ACKVEC_STATE_NOT_RECEIVED	(3 << 6)

#define DCCP_ACKVEC_STATE_MASK		0xC0 /* 11000000 */
#define DCCP_ACKVEC_LEN_MASK		0x3F /* 00111111 */

/** struct dccp_ackvec - ack vector
 *
 * This data structure is the one defined in RFC 4340, Appendix A.
 *
 * @av_buf_head - circular buffer head
 * @av_buf_tail - circular buffer tail
 * @av_buf_ackno - ack # of the most recent packet acknowledgeable in the
 *		       buffer (i.e. %av_buf_head)
 * @av_buf_nonce - the one-bit sum of the ECN Nonces on all packets acked
 * 		       by the buffer with State 0
 *
 * Additionally, the HC-Receiver must keep some information about the
 * Ack Vectors it has recently sent. For each packet sent carrying an
 * Ack Vector, it remembers four variables:
 *
 * @av_records - list of dccp_ackvec_record
 * @av_ack_nonce - the one-bit sum of the ECN Nonces for all State 0.
 *
 * @av_time - the time in usecs
 * @av_buf - circular buffer of acknowledgeable packets
 */
struct dccp_ackvec {
	u64			av_buf_ackno;
	struct list_head	av_records;
	ktime_t			av_time;
	u16			av_buf_head;
	u16			av_vec_len;
	u8			av_buf_nonce;
	u8			av_ack_nonce;
	u8			av_buf[DCCP_MAX_ACKVEC_LEN];
};

/** struct dccp_ackvec_record - ack vector record
 *
 * ACK vector record as defined in Appendix A of spec.
 *
 * The list is sorted by avr_ack_seqno
 *
 * @avr_node - node in av_records
 * @avr_ack_seqno - sequence number of the packet this record was sent on
 * @avr_ack_ackno - sequence number being acknowledged
 * @avr_ack_ptr - pointer into av_buf where this record starts
 * @avr_ack_nonce - av_ack_nonce at the time this record was sent
 * @avr_sent_len - lenght of the record in av_buf
 */
struct dccp_ackvec_record {
	struct list_head avr_node;
	u64		 avr_ack_seqno;
	u64		 avr_ack_ackno;
	u16		 avr_ack_ptr;
	u16		 avr_sent_len;
	u8		 avr_ack_nonce;
};

struct sock;
struct sk_buff;

#ifdef CONFIG_IP_DCCP_ACKVEC
extern int dccp_ackvec_init(void);
extern void dccp_ackvec_exit(void);

extern struct dccp_ackvec *dccp_ackvec_alloc(const gfp_t priority);
extern void dccp_ackvec_free(struct dccp_ackvec *av);

extern int dccp_ackvec_add(struct dccp_ackvec *av, const struct sock *sk,
			   const u64 ackno, const u8 state);

extern void dccp_ackvec_check_rcv_ackno(struct dccp_ackvec *av,
					struct sock *sk, const u64 ackno);
extern int dccp_ackvec_parse(struct sock *sk, const struct sk_buff *skb,
			     u64 *ackno, const u8 opt,
			     const u8 *value, const u8 len);

extern int dccp_insert_option_ackvec(struct sock *sk, struct sk_buff *skb);

static inline int dccp_ackvec_pending(const struct dccp_ackvec *av)
{
	return av->av_vec_len;
}
#else /* CONFIG_IP_DCCP_ACKVEC */
static inline int dccp_ackvec_init(void)
{
	return 0;
}

static inline void dccp_ackvec_exit(void)
{
}

static inline struct dccp_ackvec *dccp_ackvec_alloc(const gfp_t priority)
{
	return NULL;
}

static inline void dccp_ackvec_free(struct dccp_ackvec *av)
{
}

static inline int dccp_ackvec_add(struct dccp_ackvec *av, const struct sock *sk,
				  const u64 ackno, const u8 state)
{
	return -1;
}

static inline void dccp_ackvec_check_rcv_ackno(struct dccp_ackvec *av,
					       struct sock *sk, const u64 ackno)
{
}

static inline int dccp_ackvec_parse(struct sock *sk, const struct sk_buff *skb,
				    const u64 *ackno, const u8 opt,
				    const u8 *value, const u8 len)
{
	return -1;
}

static inline int dccp_insert_option_ackvec(const struct sock *sk,
					    const struct sk_buff *skb)
{
	return -1;
}

static inline int dccp_ackvec_pending(const struct dccp_ackvec *av)
{
	return 0;
}
#endif /* CONFIG_IP_DCCP_ACKVEC */
#endif /* _ACKVEC_H */
