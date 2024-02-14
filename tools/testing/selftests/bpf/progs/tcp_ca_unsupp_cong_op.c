// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

SEC("struct_ops/unsupp_cong_op_get_info")
size_t BPF_PROG(unsupp_cong_op_get_info, struct sock *sk, u32 ext, int *attr,
		union tcp_cc_info *info)
{
	return 0;
}

SEC(".struct_ops")
struct tcp_congestion_ops unsupp_cong_op = {
	.get_info = (void *)unsupp_cong_op_get_info,
	.name = "bpf_unsupp_op",
};
