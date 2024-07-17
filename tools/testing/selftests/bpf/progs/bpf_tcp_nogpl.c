// SPDX-License-Identifier: GPL-2.0

#include "bpf_tracing_net.h"
#include <bpf/bpf_tracing.h>

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
