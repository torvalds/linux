// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include "bpf_helpers.h"

struct bpf_map_def SEC("maps") tx_port = {
	.type = BPF_MAP_TYPE_DEVMAP,
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.max_entries = 8,
};

SEC("redirect_map_0")
int xdp_redirect_map_0(struct xdp_md *xdp)
{
	return bpf_redirect_map(&tx_port, 0, 0);
}

SEC("redirect_map_1")
int xdp_redirect_map_1(struct xdp_md *xdp)
{
	return bpf_redirect_map(&tx_port, 1, 0);
}

SEC("redirect_map_2")
int xdp_redirect_map_2(struct xdp_md *xdp)
{
	return bpf_redirect_map(&tx_port, 2, 0);
}

char _license[] SEC("license") = "GPL";
