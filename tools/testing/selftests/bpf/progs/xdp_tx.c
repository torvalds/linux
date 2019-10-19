// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include "bpf_helpers.h"

SEC("tx")
int xdp_tx(struct xdp_md *xdp)
{
	return XDP_TX;
}

char _license[] SEC("license") = "GPL";
