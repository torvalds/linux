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

#include <linux/config.h>
#include <linux/compiler.h>
#include <linux/time.h>
#include <linux/types.h>

/* Read about the ECN nonce to see why it is 253 */
#define DCCP_MAX_ACKVEC_LEN 253

#define DCCP_ACKVEC_STATE_RECEIVED	0
#define DCCP_ACKVEC_STATE_ECN_MARKED	(1 << 6)
#define DCCP_ACKVEC_STATE_NOT_RECEIVED	(3 << 6)

#define DCCP_ACKVEC_STATE_MASK		0xC0 /* 11000000 */
#define DCCP_ACKVEC_LEN_MASK		0x3F /* 00111111 */

/** struct dccp_ackvec - ack vector
 *
 * This data structure is the one defined in the DCCP draft
 * Appendix A.
 *
 * @dccpav_buf_head - circular buffer head
 * @dccpav_buf_tail - circular buffer tail
 * @dccpav_buf_ackno - ack # of the most recent packet acknowledgeable in the
 * 		       buffer (i.e. %dccpav_buf_head)
 * @dccpav_buf_nonce - the one-bit sum of the ECN Nonces on all packets acked
 * 		       by the buffer with State 0
 *
 * Additionally, the HC-Receiver must keep some information about the
 * Ack Vectors it has recently sent. For each packet sent carrying an
 * Ack Vector, it remembers four variables:
 *
 * @dccpav_ack_seqno - the Sequence Number used for the packet
 * 		       (HC-Receiver seqno)
 * @dccpav_ack_ptr - the value of buf_head at the time of acknowledgement.
 * @dccpav_ack_ackno - the Acknowledgement Number used for the packet
 * 		       (HC-Sender seqno)
 * @dccpav_ack_nonce - the one-bit sum of the ECN Nonces for all State 0.
 *
 * @dccpav_buf_len	- circular buffer length
 * @dccpav_time		- the time in usecs
 * @dccpav_buf - circular buffer of acknowledgeable packets
 */
struct dccp_ackvec {
	unsigned int	dccpav_buf_head;
	unsigned int	dccpav_buf_tail;
	u64		dccpav_buf_ackno;
	u64		dccpav_ack_seqno;
	u64		dccpav_ack_ackno;
	unsigned int	dccpav_ack_ptr;
	unsigned int	dccpav_sent_len;
	unsigned int	dccpav_vec_len;
	unsigned int	dccpav_buf_len;
	struct timeval	dccpav_time;
	u8		dccpav_buf_nonce;
	u8		dccpav_ack_nonce;
	u8		dccpav_buf[0];
};

struct sock;
struct sk_buff;

#ifdef CONFIG_IP_DCCP_ACKVEC
extern struct dccp_ackvec *dccp_ackvec_alloc(unsigned int len,
					  const gfp_t priority);
extern void dccp_ackvec_free(struct dccp_ackvec *av);

extern int dccp_ackvec_add(struct dccp_ackvec *av, const struct sock *sk,
			   const u64 ackno, const u8 state);

extern void dccp_ackvec_check_rcv_ackno(struct dccp_ackvec *av,
					struct sock *sk, const u64 ackno);
extern int dccp_ackvec_parse(struct sock *sk, const struct sk_buff *skb,
			     const u8 opt, const u8 *value, const u8 len);

extern int dccp_insert_option_ackvec(struct sock *sk, struct sk_buff *skb);

static inline int dccp_ackvec_pending(const struct dccp_ackvec *av)
{
	return av->dccpav_sent_len != av->dccpav_vec_len;
}
#else /* CONFIG_IP_DCCP_ACKVEC */
static inline struct dccp_ackvec *dccp_ackvec_alloc(unsigned int len,
					   const gfp_t priority)
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
				    const u8 opt, const u8 *value, const u8 len)
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
