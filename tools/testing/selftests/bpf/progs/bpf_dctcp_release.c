// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include <stddef.h>
#include <linux/bpf.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/tcp.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_tcp_helpers.h"

char _license[] SEC("license") = "GPL";
const char cubic[] = "cubic";

void BPF_STRUCT_OPS(dctcp_nouse_release, struct sock *sk)
{
	bpf_setsockopt(sk, SOL_TCP, TCP_CONGESTION,
		       (void *)cubic, sizeof(cubic));
}

SEC(".struct_ops")
struct tcp_congestion_ops dctcp_rel = {
	.release	= (void *)dctcp_nouse_release,
	.name		= "bpf_dctcp_rel",
};
