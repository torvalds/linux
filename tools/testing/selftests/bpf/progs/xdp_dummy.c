// SPDX-License-Identifier: GPL-2.0

#define KBUILD_MODNAME "xdp_dummy"
#include <linux/bpf.h>
#include "bpf_helpers.h"

SEC("xdp_dummy")
int xdp_dummy_prog(struct xdp_md *ctx)
{
	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
