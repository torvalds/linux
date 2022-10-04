// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

static inline struct tcp_sock *tcp_sk(const struct sock *sk)
{
	return (struct tcp_sock *)sk;
}

SEC("struct_ops/incompl_cong_ops_ssthresh")
__u32 BPF_PROG(incompl_cong_ops_ssthresh, struct sock *sk)
{
	return tcp_sk(sk)->snd_ssthresh;
}

SEC("struct_ops/incompl_cong_ops_undo_cwnd")
__u32 BPF_PROG(incompl_cong_ops_undo_cwnd, struct sock *sk)
{
	return tcp_sk(sk)->snd_cwnd;
}

SEC(".struct_ops")
struct tcp_congestion_ops incompl_cong_ops = {
	/* Intentionally leaving out any of the required cong_avoid() and
	 * cong_control() here.
	 */
	.ssthresh = (void *)incompl_cong_ops_ssthresh,
	.undo_cwnd = (void *)incompl_cong_ops_undo_cwnd,
	.name = "bpf_incompl_ops",
};
