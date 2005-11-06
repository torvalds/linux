#ifndef _DCCP_H
#define _DCCP_H
/*
 *  net/dccp/dccp.h
 *
 *  An implementation of the DCCP protocol
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *  Copyright (c) 2005 Ian McDonald <iam4@cs.waikato.ac.nz>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/dccp.h>
#include <net/snmp.h>
#include <net/sock.h>
#include <net/tcp.h>
#include "ackvec.h"

#ifdef CONFIG_IP_DCCP_DEBUG
extern int dccp_debug;

#define dccp_pr_debug(format, a...) \
	do { if (dccp_debug) \
		printk(KERN_DEBUG "%s: " format, __FUNCTION__ , ##a); \
	} while (0)
#define dccp_pr_debug_cat(format, a...) do { if (dccp_debug) \
					     printk(format, ##a); } while (0)
#else
#define dccp_pr_debug(format, a...)
#define dccp_pr_debug_cat(format, a...)
#endif

extern struct inet_hashinfo dccp_hashinfo;

extern atomic_t dccp_orphan_count;
extern int dccp_tw_count;
extern void dccp_tw_deschedule(struct inet_timewait_sock *tw);

extern void dccp_time_wait(struct sock *sk, int state, int timeo);

/* FIXME: Right size this */
#define DCCP_MAX_OPT_LEN 128

#define DCCP_MAX_PACKET_HDR 32

#define MAX_DCCP_HEADER  (DCCP_MAX_PACKET_HDR + DCCP_MAX_OPT_LEN + MAX_HEADER)

#define DCCP_TIMEWAIT_LEN (60 * HZ) /* how long to wait to destroy TIME-WAIT
				     * state, about 60 seconds */

/* draft-ietf-dccp-spec-11.txt initial RTO value */
#define DCCP_TIMEOUT_INIT ((unsigned)(3 * HZ))

/* Maximal interval between probes for local resources.  */
#define DCCP_RESOURCE_PROBE_INTERVAL ((unsigned)(HZ / 2U))

#define DCCP_RTO_MAX ((unsigned)(120 * HZ)) /* FIXME: using TCP value */

extern struct proto dccp_v4_prot;

/* is seq1 < seq2 ? */
static inline int before48(const u64 seq1, const u64 seq2)
{
	return (s64)((seq1 << 16) - (seq2 << 16)) < 0;
}

/* is seq1 > seq2 ? */
static inline int after48(const u64 seq1, const u64 seq2)
{
	return (s64)((seq2 << 16) - (seq1 << 16)) < 0;
}

/* is seq2 <= seq1 <= seq3 ? */
static inline int between48(const u64 seq1, const u64 seq2, const u64 seq3)
{
	return (seq3 << 16) - (seq2 << 16) >= (seq1 << 16) - (seq2 << 16);
}

static inline u64 max48(const u64 seq1, const u64 seq2)
{
	return after48(seq1, seq2) ? seq1 : seq2;
}

enum {
	DCCP_MIB_NUM = 0,
	DCCP_MIB_ACTIVEOPENS,			/* ActiveOpens */
	DCCP_MIB_ESTABRESETS,			/* EstabResets */
	DCCP_MIB_CURRESTAB,			/* CurrEstab */
	DCCP_MIB_OUTSEGS,			/* OutSegs */ 
	DCCP_MIB_OUTRSTS,
	DCCP_MIB_ABORTONTIMEOUT,
	DCCP_MIB_TIMEOUTS,
	DCCP_MIB_ABORTFAILED,
	DCCP_MIB_PASSIVEOPENS,
	DCCP_MIB_ATTEMPTFAILS,
	DCCP_MIB_OUTDATAGRAMS,
	DCCP_MIB_INERRS,
	DCCP_MIB_OPTMANDATORYERROR,
	DCCP_MIB_INVALIDOPT,
	__DCCP_MIB_MAX
};

#define DCCP_MIB_MAX	__DCCP_MIB_MAX
struct dccp_mib {
	unsigned long	mibs[DCCP_MIB_MAX];
} __SNMP_MIB_ALIGN__;

DECLARE_SNMP_STAT(struct dccp_mib, dccp_statistics);
#define DCCP_INC_STATS(field)	    SNMP_INC_STATS(dccp_statistics, field)
#define DCCP_INC_STATS_BH(field)    SNMP_INC_STATS_BH(dccp_statistics, field)
#define DCCP_INC_STATS_USER(field)  SNMP_INC_STATS_USER(dccp_statistics, field)
#define DCCP_DEC_STATS(field)	    SNMP_DEC_STATS(dccp_statistics, field)
#define DCCP_ADD_STATS_BH(field, val) \
			SNMP_ADD_STATS_BH(dccp_statistics, field, val)
#define DCCP_ADD_STATS_USER(field, val)	\
			SNMP_ADD_STATS_USER(dccp_statistics, field, val)

extern int  dccp_retransmit_skb(struct sock *sk, struct sk_buff *skb);

extern int dccp_send_response(struct sock *sk);
extern void dccp_send_ack(struct sock *sk);
extern void dccp_send_delayed_ack(struct sock *sk);
extern void dccp_send_sync(struct sock *sk, const u64 seq,
			   const enum dccp_pkt_type pkt_type);

extern int dccp_write_xmit(struct sock *sk, struct sk_buff *skb, long *timeo);
extern void dccp_write_space(struct sock *sk);

extern void dccp_init_xmit_timers(struct sock *sk);
static inline void dccp_clear_xmit_timers(struct sock *sk)
{
	inet_csk_clear_xmit_timers(sk);
}

extern unsigned int dccp_sync_mss(struct sock *sk, u32 pmtu);

extern const char *dccp_packet_name(const int type);
extern const char *dccp_state_name(const int state);

static inline void dccp_set_state(struct sock *sk, const int state)
{
	const int oldstate = sk->sk_state;

	dccp_pr_debug("%s(%p) %-10.10s -> %s\n",
		      dccp_role(sk), sk,
		      dccp_state_name(oldstate), dccp_state_name(state));
	WARN_ON(state == oldstate);

	switch (state) {
	case DCCP_OPEN:
		if (oldstate != DCCP_OPEN)
			DCCP_INC_STATS(DCCP_MIB_CURRESTAB);
		break;

	case DCCP_CLOSED:
		if (oldstate == DCCP_CLOSING || oldstate == DCCP_OPEN)
			DCCP_INC_STATS(DCCP_MIB_ESTABRESETS);

		sk->sk_prot->unhash(sk);
		if (inet_csk(sk)->icsk_bind_hash != NULL &&
		    !(sk->sk_userlocks & SOCK_BINDPORT_LOCK))
			inet_put_port(&dccp_hashinfo, sk);
		/* fall through */
	default:
		if (oldstate == DCCP_OPEN)
			DCCP_DEC_STATS(DCCP_MIB_CURRESTAB);
	}

	/* Change state AFTER socket is unhashed to avoid closed
	 * socket sitting in hash tables.
	 */
	sk->sk_state = state;
}

static inline void dccp_done(struct sock *sk)
{
	dccp_set_state(sk, DCCP_CLOSED);
	dccp_clear_xmit_timers(sk);

	sk->sk_shutdown = SHUTDOWN_MASK;

	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_state_change(sk);
	else
		inet_csk_destroy_sock(sk);
}

static inline void dccp_openreq_init(struct request_sock *req,
				     struct dccp_sock *dp,
				     struct sk_buff *skb)
{
	/*
	 * FIXME: fill in the other req fields from the DCCP options
	 * received
	 */
	inet_rsk(req)->rmt_port = dccp_hdr(skb)->dccph_sport;
	inet_rsk(req)->acked	= 0;
	req->rcv_wnd = 0;
}

extern int dccp_v4_conn_request(struct sock *sk, struct sk_buff *skb);

extern struct sock *dccp_create_openreq_child(struct sock *sk,
					      const struct request_sock *req,
					      const struct sk_buff *skb);

extern int dccp_v4_do_rcv(struct sock *sk, struct sk_buff *skb);

extern void dccp_v4_err(struct sk_buff *skb, u32);

extern int dccp_v4_rcv(struct sk_buff *skb);

extern struct sock *dccp_v4_request_recv_sock(struct sock *sk,
					      struct sk_buff *skb,
					      struct request_sock *req,
					      struct dst_entry *dst);
extern struct sock *dccp_check_req(struct sock *sk, struct sk_buff *skb,
				   struct request_sock *req,
				   struct request_sock **prev);

extern int dccp_child_process(struct sock *parent, struct sock *child,
			      struct sk_buff *skb);
extern int dccp_rcv_state_process(struct sock *sk, struct sk_buff *skb,
				  struct dccp_hdr *dh, unsigned len);
extern int dccp_rcv_established(struct sock *sk, struct sk_buff *skb,
				const struct dccp_hdr *dh, const unsigned len);

extern void		dccp_close(struct sock *sk, long timeout);
extern struct sk_buff	*dccp_make_response(struct sock *sk,
					    struct dst_entry *dst,
					    struct request_sock *req);
extern struct sk_buff	*dccp_make_reset(struct sock *sk,
					 struct dst_entry *dst,
					 enum dccp_reset_codes code);

extern int	   dccp_connect(struct sock *sk);
extern int	   dccp_disconnect(struct sock *sk, int flags);
extern int	   dccp_getsockopt(struct sock *sk, int level, int optname,
				   char __user *optval, int __user *optlen);
extern int	   dccp_setsockopt(struct sock *sk, int level, int optname,
				   char __user *optval, int optlen);
extern int	   dccp_ioctl(struct sock *sk, int cmd, unsigned long arg);
extern int	   dccp_sendmsg(struct kiocb *iocb, struct sock *sk,
				struct msghdr *msg, size_t size);
extern int	   dccp_recvmsg(struct kiocb *iocb, struct sock *sk,
				struct msghdr *msg, size_t len, int nonblock,
				int flags, int *addr_len);
extern void	   dccp_shutdown(struct sock *sk, int how);

extern int	   dccp_v4_checksum(const struct sk_buff *skb,
				    const u32 saddr, const u32 daddr);

extern int	   dccp_v4_send_reset(struct sock *sk,
				      enum dccp_reset_codes code);
extern void	   dccp_send_close(struct sock *sk, const int active);

struct dccp_skb_cb {
	__u8  dccpd_type:4;
	__u8  dccpd_ccval:4;
	__u8  dccpd_reset_code;
	__u16 dccpd_opt_len;
	__u64 dccpd_seq;
	__u64 dccpd_ack_seq;
};

#define DCCP_SKB_CB(__skb) ((struct dccp_skb_cb *)&((__skb)->cb[0]))

static inline int dccp_non_data_packet(const struct sk_buff *skb)
{
	const __u8 type = DCCP_SKB_CB(skb)->dccpd_type;

	return type == DCCP_PKT_ACK	 ||
	       type == DCCP_PKT_CLOSE	 ||
	       type == DCCP_PKT_CLOSEREQ ||
	       type == DCCP_PKT_RESET	 ||
	       type == DCCP_PKT_SYNC	 ||
	       type == DCCP_PKT_SYNCACK;
}

static inline int dccp_packet_without_ack(const struct sk_buff *skb)
{
	const __u8 type = DCCP_SKB_CB(skb)->dccpd_type;

	return type == DCCP_PKT_DATA || type == DCCP_PKT_REQUEST;
}

#define DCCP_MAX_SEQNO ((((u64)1) << 48) - 1)
#define DCCP_PKT_WITHOUT_ACK_SEQ (DCCP_MAX_SEQNO << 2)

static inline void dccp_set_seqno(u64 *seqno, u64 value)
{
	if (value > DCCP_MAX_SEQNO)
		value -= DCCP_MAX_SEQNO + 1;
	*seqno = value;
}

static inline u64 dccp_delta_seqno(u64 seqno1, u64 seqno2)
{
	return ((seqno2 << 16) - (seqno1 << 16)) >> 16;
}

static inline void dccp_inc_seqno(u64 *seqno)
{
	if (++*seqno > DCCP_MAX_SEQNO)
		*seqno = 0;
}

static inline void dccp_hdr_set_seq(struct dccp_hdr *dh, const u64 gss)
{
	struct dccp_hdr_ext *dhx = (struct dccp_hdr_ext *)((void *)dh +
							   sizeof(*dh));

#if defined(__LITTLE_ENDIAN_BITFIELD)
	dh->dccph_seq	   = htonl((gss >> 32)) >> 8;
#elif defined(__BIG_ENDIAN_BITFIELD)
	dh->dccph_seq	   = htonl((gss >> 32));
#else
#error  "Adjust your <asm/byteorder.h> defines"
#endif
	dhx->dccph_seq_low = htonl(gss & 0xffffffff);
}

static inline void dccp_hdr_set_ack(struct dccp_hdr_ack_bits *dhack,
				    const u64 gsr)
{
#if defined(__LITTLE_ENDIAN_BITFIELD)
	dhack->dccph_ack_nr_high = htonl((gsr >> 32)) >> 8;
#elif defined(__BIG_ENDIAN_BITFIELD)
	dhack->dccph_ack_nr_high = htonl((gsr >> 32));
#else
#error  "Adjust your <asm/byteorder.h> defines"
#endif
	dhack->dccph_ack_nr_low  = htonl(gsr & 0xffffffff);
}

static inline void dccp_update_gsr(struct sock *sk, u64 seq)
{
	struct dccp_sock *dp = dccp_sk(sk);

	dp->dccps_gsr = seq;
	dccp_set_seqno(&dp->dccps_swl,
		       (dp->dccps_gsr + 1 -
		        (dp->dccps_options.dccpo_sequence_window / 4)));
	dccp_set_seqno(&dp->dccps_swh,
		       (dp->dccps_gsr +
			(3 * dp->dccps_options.dccpo_sequence_window) / 4));
}

static inline void dccp_update_gss(struct sock *sk, u64 seq)
{
	struct dccp_sock *dp = dccp_sk(sk);

	dp->dccps_awh = dp->dccps_gss = seq;
	dccp_set_seqno(&dp->dccps_awl,
		       (dp->dccps_gss -
			dp->dccps_options.dccpo_sequence_window + 1));
}
				
static inline int dccp_ack_pending(const struct sock *sk)
{
	const struct dccp_sock *dp = dccp_sk(sk);
	return dp->dccps_timestamp_echo != 0 ||
#ifdef CONFIG_IP_DCCP_ACKVEC
	       (dp->dccps_options.dccpo_send_ack_vector &&
		dccp_ackvec_pending(dp->dccps_hc_rx_ackvec)) ||
#endif
	       inet_csk_ack_scheduled(sk);
}

extern void dccp_insert_options(struct sock *sk, struct sk_buff *skb);
extern void dccp_insert_option_elapsed_time(struct sock *sk,
					    struct sk_buff *skb,
					    u32 elapsed_time);
extern void dccp_insert_option_timestamp(struct sock *sk,
					 struct sk_buff *skb);
extern void dccp_insert_option(struct sock *sk, struct sk_buff *skb,
			       unsigned char option,
			       const void *value, unsigned char len);

extern struct socket *dccp_ctl_socket;

extern void dccp_timestamp(const struct sock *sk, struct timeval *tv);

static inline suseconds_t timeval_usecs(const struct timeval *tv)
{
	return tv->tv_sec * USEC_PER_SEC + tv->tv_usec;
}

static inline suseconds_t timeval_delta(const struct timeval *large,
					const struct timeval *small)
{
	time_t	    secs  = large->tv_sec  - small->tv_sec;
	suseconds_t usecs = large->tv_usec - small->tv_usec;

	if (usecs < 0) {
		secs--;
		usecs += USEC_PER_SEC;
	}
	return secs * USEC_PER_SEC + usecs;
}

static inline void timeval_add_usecs(struct timeval *tv,
				     const suseconds_t usecs)
{
	tv->tv_usec += usecs;
	while (tv->tv_usec >= USEC_PER_SEC) {
		tv->tv_sec++;
		tv->tv_usec -= USEC_PER_SEC;
	}
}

static inline void timeval_sub_usecs(struct timeval *tv,
				     const suseconds_t usecs)
{
	tv->tv_usec -= usecs;
	while (tv->tv_usec < 0) {
		tv->tv_sec--;
		tv->tv_usec += USEC_PER_SEC;
	}
}

#endif /* _DCCP_H */
