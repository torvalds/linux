// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <linux/types.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_tcp_helpers.h"

char _license[] SEC("license") = "X";

SEC("struct_ops")
void BPF_PROG(nogpltcp_init, struct sock *sk)
{
}

SEC(".struct_ops")
struct tcp_congestion_ops bpf_nogpltcp = {
	.init           = (void *)nogpltcp_init,
	.name           = "bpf_nogpltcp",
};
