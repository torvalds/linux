#ifndef _DCCP_H
#define _DCCP_H
/*
 *  net/dccp/dccp.h
 *
 *  An implementation of the DCCP protocol
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *  Copyright (c) 2005-6 Ian McDonald <ian.mcdonald@jandi.co.nz>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 */

#include <linux/dccp.h>
#include <linux/ktime.h>
#include <net/snmp.h>
#include <net/sock.h>
#include <net/tcp.h>
#include "ackvec.h"

/*
 * 	DCCP - specific warning and debugging macros.
 */
#define DCCP_WARN(fmt, ...)						\
	net_warn_ratelimited("%s: " fmt, __func__, ##__VA_ARGS__)
#define DCCP_CRIT(fmt, a...) printk(KERN_CRIT fmt " at %s:%d/%s()\n", ##a, \
					 __FILE__, __LINE__, __func__)
#define DCCP_BUG(a...)       do { DCCP_CRIT("BUG: " a); dump_stack(); } while(0)
#define DCCP_BUG_ON(cond)    do { if (unlikely((cond) != 0))		   \
				     DCCP_BUG("\"%s\" holds (exception!)", \
					      __stringify(cond));          \
			     } while (0)

#define DCCP_PRINTK(enable, fmt, args...)	do { if (enable)	     \
							printk(fmt, ##args); \
						} while(0)
#define DCCP_PR_DEBUG(enable, fmt, a...)	DCCP_PRINTK(enable, KERN_DEBUG \
						  "%s: " fmt, __func__, ##a)

#ifdef CONFIG_IP_DCCP_DEBUG
extern bool dccp_debug;
#define dccp_pr_debug(format, a...)	  DCCP_PR_DEBUG(dccp_debug, format, ##a)
#define dccp_pr_debug_cat(format, a...)   DCCP_PRINTK(dccp_debug, format, ##a)
#define dccp_debug(fmt, a...)		  dccp_pr_debug_cat(KERN_DEBUG fmt, ##a)
#else
#define dccp_pr_debug(format, a...)
#define dccp_pr_debug_cat(format, a...)
#define dccp_debug(format, a...)
#endif

extern struct inet_hashinfo dccp_hashinfo;

extern struct percpu_counter dccp_orphan_count;

void dccp_time_wait(struct sock *sk, int state, int timeo);

/*
 *  Set safe upper bounds for header and option length. Since Data Offset is 8
 *  bits (RFC 4340, sec. 5.1), the total header length can never be more than
 *  4 * 255 = 1020 bytes. The largest possible header length is 28 bytes (X=1):
 *    - DCCP-Response with ACK Subheader and 4 bytes of Service code      OR
 *    - DCCP-Reset    with ACK Subheader and 4 bytes of Reset Code fields
 *  Hence a safe upper bound for the maximum option length is 1020-28 = 992
 */
#define MAX_DCCP_SPECIFIC_HEADER (255 * sizeof(uint32_t))
#define DCCP_MAX_PACKET_HDR 28
#define DCCP_MAX_OPT_LEN (MAX_DCCP_SPECIFIC_HEADER - DCCP_MAX_PACKET_HDR)
#define MAX_DCCP_HEADER (MAX_DCCP_SPECIFIC_HEADER + MAX_HEADER)

/* Upper bound for initial feature-negotiation overhead (padded to 32 bits) */
#define DCCP_FEATNEG_OVERHEAD	 (32 * sizeof(uint32_t))

#define DCCP_TIMEWAIT_LEN (60 * HZ) /* how long to wait to destroy TIME-WAIT
				     * state, about 60 seconds */

/* RFC 1122, 4.2.3.1 initial RTO value */
#define DCCP_TIMEOUT_INIT ((unsigned int)(3 * HZ))

/*
 * The maximum back-off value for retransmissions. This is needed for
 *  - retransmitting client-Requests (sec. 8.1.1),
 *  - retransmitting Close/CloseReq when closing (sec. 8.3),
 *  - feature-negotiation retransmission (sec. 6.6.3),
 *  - Acks in client-PARTOPEN state (sec. 8.1.5).
 */
#define DCCP_RTO_MAX ((unsigned int)(64 * HZ))

/*
 * RTT sampling: sanity bounds and fallback RTT value from RFC 4340, section 3.4
 */
#define DCCP_SANE_RTT_MIN	100
#define DCCP_FALLBACK_RTT	(USEC_PER_SEC / 5)
#define DCCP_SANE_RTT_MAX	(3 * USEC_PER_SEC)

/* sysctl variables for DCCP */
extern int  sysctl_dccp_request_retries;
extern int  sysctl_dccp_retries1;
extern int  sysctl_dccp_retries2;
extern int  sysctl_dccp_tx_qlen;
extern int  sysctl_dccp_sync_ratelimit;

/*
 *	48-bit sequence number arithmetic (signed and unsigned)
 */
#define INT48_MIN	  0x800000000000LL		/* 2^47	    */
#define UINT48_MAX	  0xFFFFFFFFFFFFLL		/* 2^48 - 1 */
#define COMPLEMENT48(x)	 (0x1000000000000LL - (x))	/* 2^48 - x */
#define TO_SIGNED48(x)	 (((x) < INT48_MIN)? (x) : -COMPLEMENT48( (x)))
#define TO_UNSIGNED48(x) (((x) >= 0)?	     (x) :  COMPLEMENT48(-(x)))
#define ADD48(a, b)	 (((a) + (b)) & UINT48_MAX)
#define SUB48(a, b)	 ADD48((a), COMPLEMENT48(b))

static inline void dccp_set_seqno(u64 *seqno, u64 value)
{
	*seqno = value & UINT48_MAX;
}

static inline void dccp_inc_seqno(u64 *seqno)
{
	*seqno = ADD48(*seqno, 1);
}

/* signed mod-2^48 distance: pos. if seqno1 < seqno2, neg. if seqno1 > seqno2 */
static inline s64 dccp_delta_seqno(const u64 seqno1, const u64 seqno2)
{
	u64 delta = SUB48(seqno2, seqno1);

	return TO_SIGNED48(delta);
}

/* is seq1 < seq2 ? */
static inline int before48(const u64 seq1, const u64 seq2)
{
	return (s64)((seq2 << 16) - (seq1 << 16)) > 0;
}

/* is seq1 > seq2 ? */
#define after48(seq1, seq2)	before48(seq2, seq1)

/* is seq2 <= seq1 <= seq3 ? */
static inline int between48(const u64 seq1, const u64 seq2, const u64 seq3)
{
	return (seq3 << 16) - (seq2 << 16) >= (seq1 << 16) - (seq2 << 16);
}

static inline u64 max48(const u64 seq1, const u64 seq2)
{
	return after48(seq1, seq2) ? seq1 : seq2;
}

/**
 * dccp_loss_count - Approximate the number of lost data packets in a burst loss
 * @s1:  last known sequence number before the loss ('hole')
 * @s2:  first sequence number seen after the 'hole'
 * @ndp: NDP count on packet with sequence number @s2
 */
static inline u64 dccp_loss_count(const u64 s1, const u64 s2, const u64 ndp)
{
	s64 delta = dccp_delta_seqno(s1, s2);

	WARN_ON(delta < 0);
	delta -= ndp + 1;

	return delta > 0 ? delta : 0;
}

/**
 * dccp_loss_free - Evaluate condition for data loss from RFC 4340, 7.7.1
 */
static inline bool dccp_loss_free(const u64 s1, const u64 s2, const u64 ndp)
{
	return dccp_loss_count(s1, s2, ndp) == 0;
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
};

DECLARE_SNMP_STAT(struct dccp_mib, dccp_statistics);
#define DCCP_INC_STATS(field)	    SNMP_INC_STATS(dccp_statistics, field)
#define DCCP_INC_STATS_BH(field)    SNMP_INC_STATS_BH(dccp_statistics, field)
#define DCCP_DEC_STATS(field)	    SNMP_DEC_STATS(dccp_statistics, field)

/*
 * 	Checksumming routines
 */
static inline unsigned int dccp_csum_coverage(const struct sk_buff *skb)
{
	const struct dccp_hdr* dh = dccp_hdr(skb);

	if (dh->dccph_cscov == 0)
		return skb->len;
	return (dh->dccph_doff + dh->dccph_cscov - 1) * sizeof(u32);
}

static inline void dccp_csum_outgoing(struct sk_buff *skb)
{
	unsigned int cov = dccp_csum_coverage(skb);

	if (cov >= skb->len)
		dccp_hdr(skb)->dccph_cscov = 0;

	skb->csum = skb_checksum(skb, 0, (cov > skb->len)? skb->len : cov, 0);
}

void dccp_v4_send_check(struct sock *sk, struct sk_buff *skb);

int dccp_retransmit_skb(struct sock *sk);

void dccp_send_ack(struct sock *sk);
void dccp_reqsk_send_ack(struct sock *sk, struct sk_buff *skb,
			 struct request_sock *rsk);

void dccp_send_sync(struct sock *sk, const u64 seq,
		    const enum dccp_pkt_type pkt_type);

/*
 * TX Packet Dequeueing Interface
 */
void dccp_qpolicy_push(struct sock *sk, struct sk_buff *skb);
bool dccp_qpolicy_full(struct sock *sk);
void dccp_qpolicy_drop(struct sock *sk, struct sk_buff *skb);
struct sk_buff *dccp_qpolicy_top(struct sock *sk);
struct sk_buff *dccp_qpolicy_pop(struct sock *sk);
bool dccp_qpolicy_param_ok(struct sock *sk, __be32 param);

/*
 * TX Packet Output and TX Timers
 */
void dccp_write_xmit(struct sock *sk);
void dccp_write_space(struct sock *sk);
void dccp_flush_write_queue(struct sock *sk, long *time_budget);

void dccp_init_xmit_timers(struct sock *sk);
static inline void dccp_clear_xmit_timers(struct sock *sk)
{
	inet_csk_clear_xmit_timers(sk);
}

unsigned int dccp_sync_mss(struct sock *sk, u32 pmtu);

const char *dccp_packet_name(const int type);

void dccp_set_state(struct sock *sk, const int state);
void dccp_done(struct sock *sk);

int dccp_reqsk_init(struct request_sock *rq, struct dccp_sock const *dp,
		    struct sk_buff const *skb);

int dccp_v4_conn_request(struct sock *sk, struct sk_buff *skb);

struct sock *dccp_create_openreq_child(struct sock *sk,
				       const struct request_sock *req,
				       const struct sk_buff *skb);

int dccp_v4_do_rcv(struct sock *sk, struct sk_buff *skb);

struct sock *dccp_v4_request_recv_sock(struct sock *sk, struct sk_buff *skb,
				       struct request_sock *req,
				       struct dst_entry *dst);
struct sock *dccp_check_req(struct sock *sk, struct sk_buff *skb,
			    struct request_sock *req,
			    struct request_sock **prev);

int dccp_child_process(struct sock *parent, struct sock *child,
		       struct sk_buff *skb);
int dccp_rcv_state_process(struct sock *sk, struct sk_buff *skb,
			   struct dccp_hdr *dh, unsigned int len);
int dccp_rcv_established(struct sock *sk, struct sk_buff *skb,
			 const struct dccp_hdr *dh, const unsigned int len);

int dccp_init_sock(struct sock *sk, const __u8 ctl_sock_initialized);
void dccp_destroy_sock(struct sock *sk);

void dccp_close(struct sock *sk, long timeout);
struct sk_buff *dccp_make_response(struct sock *sk, struct dst_entry *dst,
				   struct request_sock *req);

int dccp_connect(struct sock *sk);
int dccp_disconnect(struct sock *sk, int flags);
int dccp_getsockopt(struct sock *sk, int level, int optname,
		    char __user *optval, int __user *optlen);
int dccp_setsockopt(struct sock *sk, int level, int optname,
		    char __user *optval, unsigned int optlen);
#ifdef CONFIG_COMPAT
int compat_dccp_getsockopt(struct sock *sk, int level, int optname,
			   char __user *optval, int __user *optlen);
int compat_dccp_setsockopt(struct sock *sk, int level, int optname,
			   char __user *optval, unsigned int optlen);
#endif
int dccp_ioctl(struct sock *sk, int cmd, unsigned long arg);
int dccp_sendmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg,
		 size_t size);
int dccp_recvmsg(struct kiocb *iocb, struct sock *sk,
		 struct msghdr *msg, size_t len, int nonblock, int flags,
		 int *addr_len);
void dccp_shutdown(struct sock *sk, int how);
int inet_dccp_listen(struct socket *sock, int backlog);
unsigned int dccp_poll(struct file *file, struct socket *sock,
		       poll_table *wait);
int dccp_v4_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len);

struct sk_buff *dccp_ctl_make_reset(struct sock *sk, struct sk_buff *skb);
int dccp_send_reset(struct sock *sk, enum dccp_reset_codes code);
void dccp_send_close(struct sock *sk, const int active);
int dccp_invalid_packet(struct sk_buff *skb);
u32 dccp_sample_rtt(struct sock *sk, long delta);

static inline int dccp_bad_service_code(const struct sock *sk,
					const __be32 service)
{
	const struct dccp_sock *dp = dccp_sk(sk);

	if (dp->dccps_service == service)
		return 0;
	return !dccp_list_has_service(dp->dccps_service_list, service);
}

/**
 * dccp_skb_cb  -  DCCP per-packet control information
 * @dccpd_type: one of %dccp_pkt_type (or unknown)
 * @dccpd_ccval: CCVal field (5.1), see e.g. RFC 4342, 8.1
 * @dccpd_reset_code: one of %dccp_reset_codes
 * @dccpd_reset_data: Data1..3 fields (depend on @dccpd_reset_code)
 * @dccpd_opt_len: total length of all options (5.8) in the packet
 * @dccpd_seq: sequence number
 * @dccpd_ack_seq: acknowledgment number subheader field value
 *
 * This is used for transmission as well as for reception.
 */
struct dccp_skb_cb {
	union {
		struct inet_skb_parm	h4;
#if IS_ENABLED(CONFIG_IPV6)
		struct inet6_skb_parm	h6;
#endif
	} header;
	__u8  dccpd_type:4;
	__u8  dccpd_ccval:4;
	__u8  dccpd_reset_code,
	      dccpd_reset_data[3];
	__u16 dccpd_opt_len;
	__u64 dccpd_seq;
	__u64 dccpd_ack_seq;
};

#define DCCP_SKB_CB(__skb) ((struct dccp_skb_cb *)&((__skb)->cb[0]))

/* RFC 4340, sec. 7.7 */
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

/* RFC 4340, sec. 7.7 */
static inline int dccp_data_packet(const struct sk_buff *skb)
{
	const __u8 type = DCCP_SKB_CB(skb)->dccpd_type;

	return type == DCCP_PKT_DATA	 ||
	       type == DCCP_PKT_DATAACK  ||
	       type == DCCP_PKT_REQUEST  ||
	       type == DCCP_PKT_RESPONSE;
}

static inline int dccp_packet_without_ack(const struct sk_buff *skb)
{
	const __u8 type = DCCP_SKB_CB(skb)->dccpd_type;

	return type == DCCP_PKT_DATA || type == DCCP_PKT_REQUEST;
}

#define DCCP_PKT_WITHOUT_ACK_SEQ (UINT48_MAX << 2)

static inline void dccp_hdr_set_seq(struct dccp_hdr *dh, const u64 gss)
{
	struct dccp_hdr_ext *dhx = (struct dccp_hdr_ext *)((void *)dh +
							   sizeof(*dh));
	dh->dccph_seq2 = 0;
	dh->dccph_seq = htons((gss >> 32) & 0xfffff);
	dhx->dccph_seq_low = htonl(gss & 0xffffffff);
}

static inline void dccp_hdr_set_ack(struct dccp_hdr_ack_bits *dhack,
				    const u64 gsr)
{
	dhack->dccph_reserved1 = 0;
	dhack->dccph_ack_nr_high = htons(gsr >> 32);
	dhack->dccph_ack_nr_low  = htonl(gsr & 0xffffffff);
}

static inline void dccp_update_gsr(struct sock *sk, u64 seq)
{
	struct dccp_sock *dp = dccp_sk(sk);

	if (after48(seq, dp->dccps_gsr))
		dp->dccps_gsr = seq;
	/* Sequence validity window depends on remote Sequence Window (7.5.1) */
	dp->dccps_swl = SUB48(ADD48(dp->dccps_gsr, 1), dp->dccps_r_seq_win / 4);
	/*
	 * Adjust SWL so that it is not below ISR. In contrast to RFC 4340,
	 * 7.5.1 we perform this check beyond the initial handshake: W/W' are
	 * always > 32, so for the first W/W' packets in the lifetime of a
	 * connection we always have to adjust SWL.
	 * A second reason why we are doing this is that the window depends on
	 * the feature-remote value of Sequence Window: nothing stops the peer
	 * from updating this value while we are busy adjusting SWL for the
	 * first W packets (we would have to count from scratch again then).
	 * Therefore it is safer to always make sure that the Sequence Window
	 * is not artificially extended by a peer who grows SWL downwards by
	 * continually updating the feature-remote Sequence-Window.
	 * If sequence numbers wrap it is bad luck. But that will take a while
	 * (48 bit), and this measure prevents Sequence-number attacks.
	 */
	if (before48(dp->dccps_swl, dp->dccps_isr))
		dp->dccps_swl = dp->dccps_isr;
	dp->dccps_swh = ADD48(dp->dccps_gsr, (3 * dp->dccps_r_seq_win) / 4);
}

static inline void dccp_update_gss(struct sock *sk, u64 seq)
{
	struct dccp_sock *dp = dccp_sk(sk);

	dp->dccps_gss = seq;
	/* Ack validity window depends on local Sequence Window value (7.5.1) */
	dp->dccps_awl = SUB48(ADD48(dp->dccps_gss, 1), dp->dccps_l_seq_win);
	/* Adjust AWL so that it is not below ISS - see comment above for SWL */
	if (before48(dp->dccps_awl, dp->dccps_iss))
		dp->dccps_awl = dp->dccps_iss;
	dp->dccps_awh = dp->dccps_gss;
}

static inline int dccp_ackvec_pending(const struct sock *sk)
{
	return dccp_sk(sk)->dccps_hc_rx_ackvec != NULL &&
	       !dccp_ackvec_is_empty(dccp_sk(sk)->dccps_hc_rx_ackvec);
}

static inline int dccp_ack_pending(const struct sock *sk)
{
	return dccp_ackvec_pending(sk) || inet_csk_ack_scheduled(sk);
}

int dccp_feat_signal_nn_change(struct sock *sk, u8 feat, u64 nn_val);
int dccp_feat_finalise_settings(struct dccp_sock *dp);
int dccp_feat_server_ccid_dependencies(struct dccp_request_sock *dreq);
int dccp_feat_insert_opts(struct dccp_sock*, struct dccp_request_sock*,
			  struct sk_buff *skb);
int dccp_feat_activate_values(struct sock *sk, struct list_head *fn);
void dccp_feat_list_purge(struct list_head *fn_list);

int dccp_insert_options(struct sock *sk, struct sk_buff *skb);
int dccp_insert_options_rsk(struct dccp_request_sock *, struct sk_buff *);
u32 dccp_timestamp(void);
void dccp_timestamping_init(void);
int dccp_insert_option(struct sk_buff *skb, unsigned char option,
		       const void *value, unsigned char len);

#ifdef CONFIG_SYSCTL
int dccp_sysctl_init(void);
void dccp_sysctl_exit(void);
#else
static inline int dccp_sysctl_init(void)
{
	return 0;
}

static inline void dccp_sysctl_exit(void)
{
}
#endif

#endif /* _DCCP_H */
