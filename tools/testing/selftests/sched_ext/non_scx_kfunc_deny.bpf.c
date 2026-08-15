/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Verify that context-sensitive SCX kfuncs (even "unlocked" ones) are
 * restricted to only SCX struct_ops programs. Non-SCX struct_ops programs,
 * such as TCP congestion control programs, should be rejected by the BPF
 * verifier when attempting to call these kfuncs.
 *
 * Copyright (C) 2026 Ching-Chun (Jim) Huang <jserv@ccns.ncku.edu.tw>
 * Copyright (C) 2026 Cheng-Yang Chou <yphbchou0911@gmail.com>
 */

#include <scx/common.bpf.h>

SEC("struct_ops/ssthresh")
__u32 BPF_PROG(tcp_ca_ssthresh, struct sock *sk)
{
	/*
	 * This call should be rejected by the verifier because this is a
	 * TCP congestion control program (non-SCX struct_ops).
	 */
	scx_bpf_kick_cpu(0, 0);
	return 2;
}

SEC("struct_ops/cong_avoid")
void BPF_PROG(tcp_ca_cong_avoid, struct sock *sk, __u32 ack, __u32 acked) {}

SEC("struct_ops/undo_cwnd")
__u32 BPF_PROG(tcp_ca_undo_cwnd, struct sock *sk) { return 2; }

SEC(".struct_ops")
struct tcp_congestion_ops tcp_non_scx_ca = {
	.ssthresh   = (void *)tcp_ca_ssthresh,
	.cong_avoid = (void *)tcp_ca_cong_avoid,
	.undo_cwnd  = (void *)tcp_ca_undo_cwnd,
	.name       = "tcp_kfunc_deny",
};

char _license[] SEC("license") = "GPL";
