/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BPF_TCP_HELPERS_H
#define __BPF_TCP_HELPERS_H

#include <stdbool.h>
#include <linux/types.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_tracing.h>

#define BPF_STRUCT_OPS(name, args...) \
SEC("struct_ops/"#name) \
BPF_PROG(name, args)

#define tcp_jiffies32 ((__u32)bpf_jiffies64())

struct sock_common {
	unsigned char	skc_state;
	__u16		skc_num;
} __attribute__((preserve_access_index));

enum sk_pacing {
	SK_PACING_NONE		= 0,
	SK_PACING_NEEDED	= 1,
	SK_PACING_FQ		= 2,
};

struct sock {
	struct sock_common	__sk_common;
	unsigned long		sk_pacing_rate;
	__u32			sk_pacing_status; /* see enum sk_pacing */
} __attribute__((preserve_access_index));

struct inet_sock {
	struct sock		sk;
} __attribute__((preserve_access_index));

struct inet_connection_sock {
	struct inet_sock	  icsk_inet;
	__u8			  icsk_ca_state:6,
				  icsk_ca_setsockopt:1,
				  icsk_ca_dst_locked:1;
	struct {
		__u8		  pending;
	} icsk_ack;
	__u64			  icsk_ca_priv[104 / sizeof(__u64)];
} __attribute__((preserve_access_index));

struct request_sock {
	struct sock_common		__req_common;
} __attribute__((preserve_access_index));

struct tcp_sock {
	struct inet_connection_sock	inet_conn;

	__u32	rcv_nxt;
	__u32	snd_nxt;
	__u32	snd_una;
	__u32	window_clamp;
	__u8	ecn_flags;
	__u32	delivered;
	__u32	delivered_ce;
	__u32	snd_cwnd;
	__u32	snd_cwnd_cnt;
	__u32	snd_cwnd_clamp;
	__u32	snd_ssthresh;
	__u8	syn_data:1,	/* SYN includes data */
		syn_fastopen:1,	/* SYN includes Fast Open option */
		syn_fastopen_exp:1,/* SYN includes Fast Open exp. option */
		syn_fastopen_ch:1, /* Active TFO re-enabling probe */
		syn_data_acked:1,/* data in SYN is acked by SYN-ACK */
		save_syn:1,	/* Save headers of SYN packet */
		is_cwnd_limited:1,/* forward progress limited by snd_cwnd? */
		syn_smc:1;	/* SYN includes SMC */
	__u32	max_packets_out;
	__u32	lsndtime;
	__u32	prior_cwnd;
	__u64	tcp_mstamp;	/* most recent packet received/sent */
} __attribute__((preserve_access_index));

static __always_inline struct inet_connection_sock *inet_csk(const struct sock *sk)
{
	return (struct inet_connection_sock *)sk;
}

static __always_inline void *inet_csk_ca(const struct sock *sk)
{
	return (void *)inet_csk(sk)->icsk_ca_priv;
}

static __always_inline struct tcp_sock *tcp_sk(const struct sock *sk)
{
	return (struct tcp_sock *)sk;
}

static __always_inline bool before(__u32 seq1, __u32 seq2)
{
	return (__s32)(seq1-seq2) < 0;
}
#define after(seq2, seq1) 	before(seq1, seq2)

#define	TCP_ECN_OK		1
#define	TCP_ECN_QUEUE_CWR	2
#define	TCP_ECN_DEMAND_CWR	4
#define	TCP_ECN_SEEN		8

enum inet_csk_ack_state_t {
	ICSK_ACK_SCHED	= 1,
	ICSK_ACK_TIMER  = 2,
	ICSK_ACK_PUSHED = 4,
	ICSK_ACK_PUSHED2 = 8,
	ICSK_ACK_NOW = 16	/* Send the next ACK immediately (once) */
};

enum tcp_ca_event {
	CA_EVENT_TX_START = 0,
	CA_EVENT_CWND_RESTART = 1,
	CA_EVENT_COMPLETE_CWR = 2,
	CA_EVENT_LOSS = 3,
	CA_EVENT_ECN_NO_CE = 4,
	CA_EVENT_ECN_IS_CE = 5,
};

struct ack_sample {
	__u32 pkts_acked;
	__s32 rtt_us;
	__u32 in_flight;
} __attribute__((preserve_access_index));

struct rate_sample {
	__u64  prior_mstamp; /* starting timestamp for interval */
	__u32  prior_delivered;	/* tp->delivered at "prior_mstamp" */
	__s32  delivered;		/* number of packets delivered over interval */
	long interval_us;	/* time for tp->delivered to incr "delivered" */
	__u32 snd_interval_us;	/* snd interval for delivered packets */
	__u32 rcv_interval_us;	/* rcv interval for delivered packets */
	long rtt_us;		/* RTT of last (S)ACKed packet (or -1) */
	int  losses;		/* number of packets marked lost upon ACK */
	__u32  acked_sacked;	/* number of packets newly (S)ACKed upon ACK */
	__u32  prior_in_flight;	/* in flight before this ACK */
	bool is_app_limited;	/* is sample from packet with bubble in pipe? */
	bool is_retrans;	/* is sample from retransmission? */
	bool is_ack_delayed;	/* is this (likely) a delayed ACK? */
} __attribute__((preserve_access_index));

#define TCP_CA_NAME_MAX		16
#define TCP_CONG_NEEDS_ECN	0x2

struct tcp_congestion_ops {
	char name[TCP_CA_NAME_MAX];
	__u32 flags;

	/* initialize private data (optional) */
	void (*init)(struct sock *sk);
	/* cleanup private data  (optional) */
	void (*release)(struct sock *sk);

	/* return slow start threshold (required) */
	__u32 (*ssthresh)(struct sock *sk);
	/* do new cwnd calculation (required) */
	void (*cong_avoid)(struct sock *sk, __u32 ack, __u32 acked);
	/* call before changing ca_state (optional) */
	void (*set_state)(struct sock *sk, __u8 new_state);
	/* call when cwnd event occurs (optional) */
	void (*cwnd_event)(struct sock *sk, enum tcp_ca_event ev);
	/* call when ack arrives (optional) */
	void (*in_ack_event)(struct sock *sk, __u32 flags);
	/* new value of cwnd after loss (required) */
	__u32  (*undo_cwnd)(struct sock *sk);
	/* hook for packet ack accounting (optional) */
	void (*pkts_acked)(struct sock *sk, const struct ack_sample *sample);
	/* override sysctl_tcp_min_tso_segs */
	__u32 (*min_tso_segs)(struct sock *sk);
	/* returns the multiplier used in tcp_sndbuf_expand (optional) */
	__u32 (*sndbuf_expand)(struct sock *sk);
	/* call when packets are delivered to update cwnd and pacing rate,
	 * after all the ca_state processing. (optional)
	 */
	void (*cong_control)(struct sock *sk, const struct rate_sample *rs);
	void *owner;
};

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min_not_zero(x, y) ({			\
	typeof(x) __x = (x);			\
	typeof(y) __y = (y);			\
	__x == 0 ? __y : ((__y == 0) ? __x : min(__x, __y)); })

static __always_inline __u32 tcp_slow_start(struct tcp_sock *tp, __u32 acked)
{
	__u32 cwnd = min(tp->snd_cwnd + acked, tp->snd_ssthresh);

	acked -= cwnd - tp->snd_cwnd;
	tp->snd_cwnd = min(cwnd, tp->snd_cwnd_clamp);

	return acked;
}

static __always_inline bool tcp_in_slow_start(const struct tcp_sock *tp)
{
	return tp->snd_cwnd < tp->snd_ssthresh;
}

static __always_inline bool tcp_is_cwnd_limited(const struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);

	/* If in slow start, ensure cwnd grows to twice what was ACKed. */
	if (tcp_in_slow_start(tp))
		return tp->snd_cwnd < 2 * tp->max_packets_out;

	return !!BPF_CORE_READ_BITFIELD(tp, is_cwnd_limited);
}

static __always_inline void tcp_cong_avoid_ai(struct tcp_sock *tp, __u32 w, __u32 acked)
{
	/* If credits accumulated at a higher w, apply them gently now. */
	if (tp->snd_cwnd_cnt >= w) {
		tp->snd_cwnd_cnt = 0;
		tp->snd_cwnd++;
	}

	tp->snd_cwnd_cnt += acked;
	if (tp->snd_cwnd_cnt >= w) {
		__u32 delta = tp->snd_cwnd_cnt / w;

		tp->snd_cwnd_cnt -= delta * w;
		tp->snd_cwnd += delta;
	}
	tp->snd_cwnd = min(tp->snd_cwnd, tp->snd_cwnd_clamp);
}

#endif
