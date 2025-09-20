// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

volatile const char bpf_metadata_a[] SEC(".rodata") = "foo";
volatile const int bpf_metadata_b SEC(".rodata") = 1;

SEC("cgroup_skb/egress")
int prog(struct xdp_md *ctx)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
