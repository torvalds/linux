// SPDX-License-Identifier: GPL-2.0

#include "bpf_tracing_net.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

#define USEC_PER_SEC 1000000UL

#define min(a, b) ((a) < (b) ? (a) : (b))

static unsigned int tcp_left_out(const struct tcp_sock *tp)
{
	return tp->sacked_out + tp->lost_out;
}

static unsigned int tcp_packets_in_flight(const struct tcp_sock *tp)
{
	return tp->packets_out - tcp_left_out(tp) + tp->retrans_out;
}

SEC("struct_ops")
void BPF_PROG(write_sk_pacing_init, struct sock *sk)
{
#ifdef ENABLE_ATOMICS_TESTS
	__sync_bool_compare_and_swap(&sk->sk_pacing_status, SK_PACING_NONE,
				     SK_PACING_NEEDED);
#else
	sk->sk_pacing_status = SK_PACING_NEEDED;
#endif
}

SEC("struct_ops")
void BPF_PROG(write_sk_pacing_cong_control, struct sock *sk,
	      const struct rate_sample *rs)
{
	struct tcp_sock *tp = tcp_sk(sk);
	unsigned long rate =
		((tp->snd_cwnd * tp->mss_cache * USEC_PER_SEC) << 3) /
		(tp->srtt_us ?: 1U << 3);
	sk->sk_pacing_rate = min(rate, sk->sk_max_pacing_rate);
	tp->app_limited = (tp->delivered + tcp_packets_in_flight(tp)) ?: 1;
}

SEC("struct_ops")
__u32 BPF_PROG(write_sk_pacing_ssthresh, struct sock *sk)
{
	return tcp_sk(sk)->snd_ssthresh;
}

SEC("struct_ops")
__u32 BPF_PROG(write_sk_pacing_undo_cwnd, struct sock *sk)
{
	return tcp_sk(sk)->snd_cwnd;
}

SEC(".struct_ops")
struct tcp_congestion_ops write_sk_pacing = {
	.init = (void *)write_sk_pacing_init,
	.cong_control = (void *)write_sk_pacing_cong_control,
	.ssthresh = (void *)write_sk_pacing_ssthresh,
	.undo_cwnd = (void *)write_sk_pacing_undo_cwnd,
	.name = "bpf_w_sk_pacing",
};
