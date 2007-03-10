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
#include <net/snmp.h>
#include <net/sock.h>
#include <net/tcp.h>
#include "ackvec.h"

/*
 * 	DCCP - specific warning and debugging macros.
 */
#define DCCP_WARN(fmt, a...) LIMIT_NETDEBUG(KERN_WARNING "%s: " fmt,       \
							__FUNCTION__, ##a)
#define DCCP_CRIT(fmt, a...) printk(KERN_CRIT fmt " at %s:%d/%s()\n", ##a, \
					 __FILE__, __LINE__, __FUNCTION__)
#define DCCP_BUG(a...)       do { DCCP_CRIT("BUG: " a); dump_stack(); } while(0)
#define DCCP_BUG_ON(cond)    do { if (unlikely((cond) != 0))		   \
				     DCCP_BUG("\"%s\" holds (exception!)", \
					      __stringify(cond));          \
			     } while (0)

#ifdef MODULE
#define DCCP_PRINTK(enable, fmt, args...)	do { if (enable)	     \
							printk(fmt, ##args); \
						} while(0)
#else
#define DCCP_PRINTK(enable, fmt, args...)	printk(fmt, ##args)
#endif
#define DCCP_PR_DEBUG(enable, fmt, a...)	DCCP_PRINTK(enable, KERN_DEBUG \
						  "%s: " fmt, __FUNCTION__, ##a)

#ifdef CONFIG_IP_DCCP_DEBUG
extern int dccp_debug;
#define dccp_pr_debug(format, a...)	  DCCP_PR_DEBUG(dccp_debug, format, ##a)
#define dccp_pr_debug_cat(format, a...)   DCCP_PRINTK(dccp_debug, format, ##a)
#else
#define dccp_pr_debug(format, a...)
#define dccp_pr_debug_cat(format, a...)
#endif

extern struct inet_hashinfo dccp_hashinfo;

extern atomic_t dccp_orphan_count;

extern void dccp_time_wait(struct sock *sk, int state, int timeo);

/*
 *  Set safe upper bounds for header and option length. Since Data Offset is 8
 *  bits (RFC 4340, sec. 5.1), the total header length can never be more than
 *  4 * 255 = 1020 bytes. The largest possible header length is 28 bytes (X=1):
 *    - DCCP-Response with ACK Subheader and 4 bytes of Service code      OR
 *    - DCCP-Reset    with ACK Subheader and 4 bytes of Reset Code fields
 *  Hence a safe upper bound for the maximum option length is 1020-28 = 992
 */
#define MAX_DCCP_SPECIFIC_HEADER (255 * sizeof(int))
#define DCCP_MAX_PACKET_HDR 28
#define DCCP_MAX_OPT_LEN (MAX_DCCP_SPECIFIC_HEADER - DCCP_MAX_PACKET_HDR)
#define MAX_DCCP_HEADER (MAX_DCCP_SPECIFIC_HEADER + MAX_HEADER)

#define DCCP_TIMEWAIT_LEN (60 * HZ) /* how long to wait to destroy TIME-WAIT
				     * state, about 60 seconds */

/* RFC 1122, 4.2.3.1 initial RTO value */
#define DCCP_TIMEOUT_INIT ((unsigned)(3 * HZ))

/* Maximal interval between probes for local resources.  */
#define DCCP_RESOURCE_PROBE_INTERVAL ((unsigned)(HZ / 2U))

#define DCCP_RTO_MAX ((unsigned)(120 * HZ)) /* FIXME: using TCP value */

/* sysctl variables for DCCP */
extern int  sysctl_dccp_request_retries;
extern int  sysctl_dccp_retries1;
extern int  sysctl_dccp_retries2;
extern int  sysctl_dccp_feat_sequence_window;
extern int  sysctl_dccp_feat_rx_ccid;
extern int  sysctl_dccp_feat_tx_ccid;
extern int  sysctl_dccp_feat_ack_ratio;
extern int  sysctl_dccp_feat_send_ack_vector;
extern int  sysctl_dccp_feat_send_ndp_count;
extern int  sysctl_dccp_tx_qlen;

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

/* is seq1 next seqno after seq2 */
static inline int follows48(const u64 seq1, const u64 seq2)
{
	int diff = (seq1 & 0xFFFF) - (seq2 & 0xFFFF);

	return diff==1;
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

/*
 * 	Checksumming routines
 */
static inline int dccp_csum_coverage(const struct sk_buff *skb)
{
	const struct dccp_hdr* dh = dccp_hdr(skb);

	if (dh->dccph_cscov == 0)
		return skb->len;
	return (dh->dccph_doff + dh->dccph_cscov - 1) * sizeof(u32);
}

static inline void dccp_csum_outgoing(struct sk_buff *skb)
{
	int cov = dccp_csum_coverage(skb);

	if (cov >= skb->len)
		dccp_hdr(skb)->dccph_cscov = 0;

	skb->csum = skb_checksum(skb, 0, (cov > skb->len)? skb->len : cov, 0);
}

extern void dccp_v4_send_check(struct sock *sk, int len, struct sk_buff *skb);

extern int  dccp_retransmit_skb(struct sock *sk, struct sk_buff *skb);

extern void dccp_send_ack(struct sock *sk);
extern void dccp_send_delayed_ack(struct sock *sk);
extern void dccp_reqsk_send_ack(struct sk_buff *sk, struct request_sock *rsk);

extern void dccp_send_sync(struct sock *sk, const u64 seq,
			   const enum dccp_pkt_type pkt_type);

extern void dccp_write_xmit(struct sock *sk, int block);
extern void dccp_write_xmit_timer(unsigned long data);
extern void dccp_write_space(struct sock *sk);

extern void dccp_init_xmit_timers(struct sock *sk);
static inline void dccp_clear_xmit_timers(struct sock *sk)
{
	inet_csk_clear_xmit_timers(sk);
}

extern unsigned int dccp_sync_mss(struct sock *sk, u32 pmtu);

extern const char *dccp_packet_name(const int type);
extern const char *dccp_state_name(const int state);

extern void dccp_set_state(struct sock *sk, const int state);
extern void dccp_done(struct sock *sk);

extern void dccp_reqsk_init(struct request_sock *req, struct sk_buff *skb);

extern int dccp_v4_conn_request(struct sock *sk, struct sk_buff *skb);

extern struct sock *dccp_create_openreq_child(struct sock *sk,
					      const struct request_sock *req,
					      const struct sk_buff *skb);

extern int dccp_v4_do_rcv(struct sock *sk, struct sk_buff *skb);

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

extern int dccp_init_sock(struct sock *sk, const __u8 ctl_sock_initialized);
extern int dccp_destroy_sock(struct sock *sk);

extern void		dccp_close(struct sock *sk, long timeout);
extern struct sk_buff	*dccp_make_response(struct sock *sk,
					    struct dst_entry *dst,
					    struct request_sock *req);

extern int	   dccp_connect(struct sock *sk);
extern int	   dccp_disconnect(struct sock *sk, int flags);
extern void	   dccp_hash(struct sock *sk);
extern void	   dccp_unhash(struct sock *sk);
extern int	   dccp_getsockopt(struct sock *sk, int level, int optname,
				   char __user *optval, int __user *optlen);
extern int	   dccp_setsockopt(struct sock *sk, int level, int optname,
				   char __user *optval, int optlen);
#ifdef CONFIG_COMPAT
extern int	   compat_dccp_getsockopt(struct sock *sk,
				int level, int optname,
				char __user *optval, int __user *optlen);
extern int	   compat_dccp_setsockopt(struct sock *sk,
				int level, int optname,
				char __user *optval, int optlen);
#endif
extern int	   dccp_ioctl(struct sock *sk, int cmd, unsigned long arg);
extern int	   dccp_sendmsg(struct kiocb *iocb, struct sock *sk,
				struct msghdr *msg, size_t size);
extern int	   dccp_recvmsg(struct kiocb *iocb, struct sock *sk,
				struct msghdr *msg, size_t len, int nonblock,
				int flags, int *addr_len);
extern void	   dccp_shutdown(struct sock *sk, int how);
extern int	   inet_dccp_listen(struct socket *sock, int backlog);
extern unsigned int dccp_poll(struct file *file, struct socket *sock,
			     poll_table *wait);
extern int	   dccp_v4_connect(struct sock *sk, struct sockaddr *uaddr,
				   int addr_len);

extern int	   dccp_send_reset(struct sock *sk, enum dccp_reset_codes code);
extern void	   dccp_send_close(struct sock *sk, const int active);
extern int	   dccp_invalid_packet(struct sk_buff *skb);

static inline int dccp_bad_service_code(const struct sock *sk,
					const __be32 service)
{
	const struct dccp_sock *dp = dccp_sk(sk);

	if (dp->dccps_service == service)
		return 0;
	return !dccp_list_has_service(dp->dccps_service_list, service);
}

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
	const struct dccp_minisock *dmsk = dccp_msk(sk);

	dp->dccps_gsr = seq;
	dccp_set_seqno(&dp->dccps_swl,
		       dp->dccps_gsr + 1 - (dmsk->dccpms_sequence_window / 4));
	dccp_set_seqno(&dp->dccps_swh,
		       dp->dccps_gsr + (3 * dmsk->dccpms_sequence_window) / 4);
}

static inline void dccp_update_gss(struct sock *sk, u64 seq)
{
	struct dccp_sock *dp = dccp_sk(sk);

	dp->dccps_awh = dp->dccps_gss = seq;
	dccp_set_seqno(&dp->dccps_awl,
		       (dp->dccps_gss -
			dccp_msk(sk)->dccpms_sequence_window + 1));
}

static inline int dccp_ack_pending(const struct sock *sk)
{
	const struct dccp_sock *dp = dccp_sk(sk);
	return dp->dccps_timestamp_echo != 0 ||
#ifdef CONFIG_IP_DCCP_ACKVEC
	       (dccp_msk(sk)->dccpms_send_ack_vector &&
		dccp_ackvec_pending(dp->dccps_hc_rx_ackvec)) ||
#endif
	       inet_csk_ack_scheduled(sk);
}

extern int dccp_insert_options(struct sock *sk, struct sk_buff *skb);
extern int dccp_insert_option_elapsed_time(struct sock *sk,
					    struct sk_buff *skb,
					    u32 elapsed_time);
extern int dccp_insert_option_timestamp(struct sock *sk,
					 struct sk_buff *skb);
extern int dccp_insert_option(struct sock *sk, struct sk_buff *skb,
			       unsigned char option,
			       const void *value, unsigned char len);

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
	DCCP_BUG_ON(tv->tv_sec < 0);
}

#ifdef CONFIG_SYSCTL
extern int dccp_sysctl_init(void);
extern void dccp_sysctl_exit(void);
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
