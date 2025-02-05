// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_DEVMAP);
	__uint(max_entries, 8);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} tx_port SEC(".maps");

SEC("xdp")
int xdp_redirect_map_0(struct xdp_md *xdp)
{
	return bpf_redirect_map(&tx_port, 0, 0);
}

SEC("xdp")
int xdp_redirect_map_1(struct xdp_md *xdp)
{
	return bpf_redirect_map(&tx_port, 1, 0);
}

SEC("xdp")
int xdp_redirect_map_2(struct xdp_md *xdp)
{
	return bpf_redirect_map(&tx_port, 2, 0);
}

char _license[] SEC("license") = "GPL";
