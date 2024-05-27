// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "bpf_tracing_net.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";
const char cubic[] = "cubic";

SEC("struct_ops")
void BPF_PROG(dctcp_nouse_release, struct sock *sk)
{
	bpf_setsockopt(sk, SOL_TCP, TCP_CONGESTION,
		       (void *)cubic, sizeof(cubic));
}

SEC(".struct_ops")
struct tcp_congestion_ops dctcp_rel = {
	.release	= (void *)dctcp_nouse_release,
	.name		= "bpf_dctcp_rel",
};
