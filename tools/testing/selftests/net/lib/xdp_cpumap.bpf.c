// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_CPUMAP);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} map_cpumap SEC(".maps");

SEC("xdp.frags")
int xdp_cpumap_prog_frags(struct xdp_md *ctx)
{
	return bpf_redirect_map(&map_cpumap, 0, XDP_DROP);
}

char _license[] SEC("license") = "GPL";
