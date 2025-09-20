// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

char LICENSE[] SEC("license") = "GPL";

SEC("xdp")
int xdp_handler(struct xdp_md *xdp)
{
	return 0;
}

SEC("tc")
int tc_handler(struct __sk_buff *skb)
{
	return 0;
}
