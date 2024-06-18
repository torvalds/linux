// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_tracing.h>

extern void bbr_init(struct sock *sk) __ksym;
extern void bbr_main(struct sock *sk, u32 ack, int flag, const struct rate_sample *rs) __ksym;
extern u32 bbr_sndbuf_expand(struct sock *sk) __ksym;
extern u32 bbr_undo_cwnd(struct sock *sk) __ksym;
extern void bbr_cwnd_event(struct sock *sk, enum tcp_ca_event event) __ksym;
extern u32 bbr_ssthresh(struct sock *sk) __ksym;
extern u32 bbr_min_tso_segs(struct sock *sk) __ksym;
extern void bbr_set_state(struct sock *sk, u8 new_state) __ksym;

extern void dctcp_init(struct sock *sk) __ksym;
extern void dctcp_update_alpha(struct sock *sk, u32 flags) __ksym;
extern void dctcp_cwnd_event(struct sock *sk, enum tcp_ca_event ev) __ksym;
extern u32 dctcp_ssthresh(struct sock *sk) __ksym;
extern u32 dctcp_cwnd_undo(struct sock *sk) __ksym;
extern void dctcp_state(struct sock *sk, u8 new_state) __ksym;

extern void cubictcp_init(struct sock *sk) __ksym;
extern u32 cubictcp_recalc_ssthresh(struct sock *sk) __ksym;
extern void cubictcp_cong_avoid(struct sock *sk, u32 ack, u32 acked) __ksym;
extern void cubictcp_state(struct sock *sk, u8 new_state) __ksym;
extern void cubictcp_cwnd_event(struct sock *sk, enum tcp_ca_event event) __ksym;
extern void cubictcp_acked(struct sock *sk, const struct ack_sample *sample) __ksym;

SEC("struct_ops")
void BPF_PROG(init, struct sock *sk)
{
	bbr_init(sk);
	dctcp_init(sk);
	cubictcp_init(sk);
}

SEC("struct_ops")
void BPF_PROG(in_ack_event, struct sock *sk, u32 flags)
{
	dctcp_update_alpha(sk, flags);
}

SEC("struct_ops")
void BPF_PROG(cong_control, struct sock *sk, u32 ack, int flag, const struct rate_sample *rs)
{
	bbr_main(sk, ack, flag, rs);
}

SEC("struct_ops")
void BPF_PROG(cong_avoid, struct sock *sk, u32 ack, u32 acked)
{
	cubictcp_cong_avoid(sk, ack, acked);
}

SEC("struct_ops")
u32 BPF_PROG(sndbuf_expand, struct sock *sk)
{
	return bbr_sndbuf_expand(sk);
}

SEC("struct_ops")
u32 BPF_PROG(undo_cwnd, struct sock *sk)
{
	bbr_undo_cwnd(sk);
	return dctcp_cwnd_undo(sk);
}

SEC("struct_ops")
void BPF_PROG(cwnd_event, struct sock *sk, enum tcp_ca_event event)
{
	bbr_cwnd_event(sk, event);
	dctcp_cwnd_event(sk, event);
	cubictcp_cwnd_event(sk, event);
}

SEC("struct_ops")
u32 BPF_PROG(ssthresh, struct sock *sk)
{
	bbr_ssthresh(sk);
	dctcp_ssthresh(sk);
	return cubictcp_recalc_ssthresh(sk);
}

SEC("struct_ops")
u32 BPF_PROG(min_tso_segs, struct sock *sk)
{
	return bbr_min_tso_segs(sk);
}

SEC("struct_ops")
void BPF_PROG(set_state, struct sock *sk, u8 new_state)
{
	bbr_set_state(sk, new_state);
	dctcp_state(sk, new_state);
	cubictcp_state(sk, new_state);
}

SEC("struct_ops")
void BPF_PROG(pkts_acked, struct sock *sk, const struct ack_sample *sample)
{
	cubictcp_acked(sk, sample);
}

SEC(".struct_ops")
struct tcp_congestion_ops tcp_ca_kfunc = {
	.init		= (void *)init,
	.in_ack_event	= (void *)in_ack_event,
	.cong_control	= (void *)cong_control,
	.cong_avoid	= (void *)cong_avoid,
	.sndbuf_expand	= (void *)sndbuf_expand,
	.undo_cwnd	= (void *)undo_cwnd,
	.cwnd_event	= (void *)cwnd_event,
	.ssthresh	= (void *)ssthresh,
	.min_tso_segs	= (void *)min_tso_segs,
	.set_state	= (void *)set_state,
	.pkts_acked     = (void *)pkts_acked,
	.name		= "tcp_ca_kfunc",
};

char _license[] SEC("license") = "GPL";
