// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */

/* WARNING: This implementation is not necessarily the same
 * as the tcp_dctcp.c.  The purpose is mainly for testing
 * the kernel BPF logic.
 */

#include "bpf_tracing_net.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#ifndef EBUSY
#define EBUSY 16
#endif
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min_not_zero(x, y) ({			\
	typeof(x) __x = (x);			\
	typeof(y) __y = (y);			\
	__x == 0 ? __y : ((__y == 0) ? __x : min(__x, __y)); })
static bool before(__u32 seq1, __u32 seq2)
{
	return (__s32)(seq1-seq2) < 0;
}

char _license[] SEC("license") = "GPL";

volatile const char fallback_cc[TCP_CA_NAME_MAX];
const char bpf_dctcp[] = "bpf_dctcp";
const char tcp_cdg[] = "cdg";
char cc_res[TCP_CA_NAME_MAX];
int tcp_cdg_res = 0;
int stg_result = 0;
int ebusy_cnt = 0;

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, int);
} sk_stg_map SEC(".maps");

#define DCTCP_MAX_ALPHA	1024U

struct bpf_dctcp {
	__u32 old_delivered;
	__u32 old_delivered_ce;
	__u32 prior_rcv_nxt;
	__u32 dctcp_alpha;
	__u32 next_seq;
	__u32 ce_state;
	__u32 loss_cwnd;
};

static unsigned int dctcp_shift_g = 4; /* g = 1/2^4 */
static unsigned int dctcp_alpha_on_init = DCTCP_MAX_ALPHA;

static void dctcp_reset(const struct tcp_sock *tp, struct bpf_dctcp *ca)
{
	ca->next_seq = tp->snd_nxt;

	ca->old_delivered = tp->delivered;
	ca->old_delivered_ce = tp->delivered_ce;
}

SEC("struct_ops")
void BPF_PROG(bpf_dctcp_init, struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct bpf_dctcp *ca = inet_csk_ca(sk);
	int *stg;

	if (!(tp->ecn_flags & TCP_ECN_OK) && fallback_cc[0]) {
		/* Switch to fallback */
		if (bpf_setsockopt(sk, SOL_TCP, TCP_CONGESTION,
				   (void *)fallback_cc, sizeof(fallback_cc)) == -EBUSY)
			ebusy_cnt++;

		/* Switch back to myself and the recurred bpf_dctcp_init()
		 * will get -EBUSY for all bpf_setsockopt(TCP_CONGESTION),
		 * except the last "cdg" one.
		 */
		if (bpf_setsockopt(sk, SOL_TCP, TCP_CONGESTION,
				   (void *)bpf_dctcp, sizeof(bpf_dctcp)) == -EBUSY)
			ebusy_cnt++;

		/* Switch back to fallback */
		if (bpf_setsockopt(sk, SOL_TCP, TCP_CONGESTION,
				   (void *)fallback_cc, sizeof(fallback_cc)) == -EBUSY)
			ebusy_cnt++;

		/* Expecting -ENOTSUPP for tcp_cdg_res */
		tcp_cdg_res = bpf_setsockopt(sk, SOL_TCP, TCP_CONGESTION,
					     (void *)tcp_cdg, sizeof(tcp_cdg));
		bpf_getsockopt(sk, SOL_TCP, TCP_CONGESTION,
			       (void *)cc_res, sizeof(cc_res));
		return;
	}

	ca->prior_rcv_nxt = tp->rcv_nxt;
	ca->dctcp_alpha = min(dctcp_alpha_on_init, DCTCP_MAX_ALPHA);
	ca->loss_cwnd = 0;
	ca->ce_state = 0;

	stg = bpf_sk_storage_get(&sk_stg_map, (void *)tp, NULL, 0);
	if (stg) {
		stg_result = *stg;
		bpf_sk_storage_delete(&sk_stg_map, (void *)tp);
	}
	dctcp_reset(tp, ca);
}

SEC("struct_ops")
__u32 BPF_PROG(bpf_dctcp_ssthresh, struct sock *sk)
{
	struct bpf_dctcp *ca = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	ca->loss_cwnd = tp->snd_cwnd;
	return max(tp->snd_cwnd - ((tp->snd_cwnd * ca->dctcp_alpha) >> 11U), 2U);
}

SEC("struct_ops")
void BPF_PROG(bpf_dctcp_update_alpha, struct sock *sk, __u32 flags)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct bpf_dctcp *ca = inet_csk_ca(sk);

	/* Expired RTT */
	if (!before(tp->snd_una, ca->next_seq)) {
		__u32 delivered_ce = tp->delivered_ce - ca->old_delivered_ce;
		__u32 alpha = ca->dctcp_alpha;

		/* alpha = (1 - g) * alpha + g * F */

		alpha -= min_not_zero(alpha, alpha >> dctcp_shift_g);
		if (delivered_ce) {
			__u32 delivered = tp->delivered - ca->old_delivered;

			/* If dctcp_shift_g == 1, a 32bit value would overflow
			 * after 8 M packets.
			 */
			delivered_ce <<= (10 - dctcp_shift_g);
			delivered_ce /= max(1U, delivered);

			alpha = min(alpha + delivered_ce, DCTCP_MAX_ALPHA);
		}
		ca->dctcp_alpha = alpha;
		dctcp_reset(tp, ca);
	}
}

static void dctcp_react_to_loss(struct sock *sk)
{
	struct bpf_dctcp *ca = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	ca->loss_cwnd = tp->snd_cwnd;
	tp->snd_ssthresh = max(tp->snd_cwnd >> 1U, 2U);
}

SEC("struct_ops")
void BPF_PROG(bpf_dctcp_state, struct sock *sk, __u8 new_state)
{
	if (new_state == TCP_CA_Recovery &&
	    new_state != BPF_CORE_READ_BITFIELD(inet_csk(sk), icsk_ca_state))
		dctcp_react_to_loss(sk);
	/* We handle RTO in bpf_dctcp_cwnd_event to ensure that we perform only
	 * one loss-adjustment per RTT.
	 */
}

static void dctcp_ece_ack_cwr(struct sock *sk, __u32 ce_state)
{
	struct tcp_sock *tp = tcp_sk(sk);

	if (ce_state == 1)
		tp->ecn_flags |= TCP_ECN_DEMAND_CWR;
	else
		tp->ecn_flags &= ~TCP_ECN_DEMAND_CWR;
}

/* Minimal DCTP CE state machine:
 *
 * S:	0 <- last pkt was non-CE
 *	1 <- last pkt was CE
 */
static void dctcp_ece_ack_update(struct sock *sk, enum tcp_ca_event evt,
				 __u32 *prior_rcv_nxt, __u32 *ce_state)
{
	__u32 new_ce_state = (evt == CA_EVENT_ECN_IS_CE) ? 1 : 0;

	if (*ce_state != new_ce_state) {
		/* CE state has changed, force an immediate ACK to
		 * reflect the new CE state. If an ACK was delayed,
		 * send that first to reflect the prior CE state.
		 */
		if (inet_csk(sk)->icsk_ack.pending & ICSK_ACK_TIMER) {
			dctcp_ece_ack_cwr(sk, *ce_state);
			bpf_tcp_send_ack(sk, *prior_rcv_nxt);
		}
		inet_csk(sk)->icsk_ack.pending |= ICSK_ACK_NOW;
	}
	*prior_rcv_nxt = tcp_sk(sk)->rcv_nxt;
	*ce_state = new_ce_state;
	dctcp_ece_ack_cwr(sk, new_ce_state);
}

SEC("struct_ops")
void BPF_PROG(bpf_dctcp_cwnd_event, struct sock *sk, enum tcp_ca_event ev)
{
	struct bpf_dctcp *ca = inet_csk_ca(sk);

	switch (ev) {
	case CA_EVENT_ECN_IS_CE:
	case CA_EVENT_ECN_NO_CE:
		dctcp_ece_ack_update(sk, ev, &ca->prior_rcv_nxt, &ca->ce_state);
		break;
	case CA_EVENT_LOSS:
		dctcp_react_to_loss(sk);
		break;
	default:
		/* Don't care for the rest. */
		break;
	}
}

SEC("struct_ops")
__u32 BPF_PROG(bpf_dctcp_cwnd_undo, struct sock *sk)
{
	const struct bpf_dctcp *ca = inet_csk_ca(sk);

	return max(tcp_sk(sk)->snd_cwnd, ca->loss_cwnd);
}

extern void tcp_reno_cong_avoid(struct sock *sk, __u32 ack, __u32 acked) __ksym;

SEC("struct_ops")
void BPF_PROG(bpf_dctcp_cong_avoid, struct sock *sk, __u32 ack, __u32 acked)
{
	tcp_reno_cong_avoid(sk, ack, acked);
}

SEC(".struct_ops")
struct tcp_congestion_ops dctcp_nouse = {
	.init		= (void *)bpf_dctcp_init,
	.set_state	= (void *)bpf_dctcp_state,
	.flags		= TCP_CONG_NEEDS_ECN,
	.name		= "bpf_dctcp_nouse",
};

SEC(".struct_ops")
struct tcp_congestion_ops dctcp = {
	.init		= (void *)bpf_dctcp_init,
	.in_ack_event   = (void *)bpf_dctcp_update_alpha,
	.cwnd_event	= (void *)bpf_dctcp_cwnd_event,
	.ssthresh	= (void *)bpf_dctcp_ssthresh,
	.cong_avoid	= (void *)bpf_dctcp_cong_avoid,
	.undo_cwnd	= (void *)bpf_dctcp_cwnd_undo,
	.set_state	= (void *)bpf_dctcp_state,
	.flags		= TCP_CONG_NEEDS_ECN,
	.name		= "bpf_dctcp",
};
