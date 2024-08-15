// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_DEVMAP);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(struct bpf_devmap_val));
	__uint(max_entries, 4);
} dm_ports SEC(".maps");

/* valid program on DEVMAP entry via SEC name;
 * has access to egress and ingress ifindex
 */
SEC("xdp/devmap")
int xdp_dummy_dm(struct xdp_md *ctx)
{
	return XDP_PASS;
}

SEC("xdp.frags/devmap")
int xdp_dummy_dm_frags(struct xdp_md *ctx)
{
	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
