// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

SEC("xdp")
int xdp_tx(struct xdp_md *xdp)
{
	return XDP_TX;
}

char _license[] SEC("license") = "GPL";
