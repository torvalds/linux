// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_CPUMAP);
	__type(key, __u32);
	__type(value, struct bpf_cpumap_val);
	__uint(max_entries, 1);
} cpu_map SEC(".maps");

SEC("xdp/cpumap")
int xdp_drop_prog(struct xdp_md *ctx)
{
	return XDP_DROP;
}

SEC("freplace")
int xdp_cpumap_prog(struct xdp_md *ctx)
{
	return bpf_redirect_map(&cpu_map, 0, XDP_PASS);
}

char _license[] SEC("license") = "GPL";
