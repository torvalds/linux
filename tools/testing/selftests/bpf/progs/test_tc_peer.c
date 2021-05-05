// SPDX-License-Identifier: GPL-2.0
#include <stdint.h>
#include <stdbool.h>

#include <linux/bpf.h>
#include <linux/stddef.h>
#include <linux/pkt_cls.h>

#include <bpf/bpf_helpers.h>

static volatile const __u32 IFINDEX_SRC;
static volatile const __u32 IFINDEX_DST;

SEC("classifier/chk_egress")
int tc_chk(struct __sk_buff *skb)
{
	return TC_ACT_SHOT;
}

SEC("classifier/dst_ingress")
int tc_dst(struct __sk_buff *skb)
{
	return bpf_redirect_peer(IFINDEX_SRC, 0);
}

SEC("classifier/src_ingress")
int tc_src(struct __sk_buff *skb)
{
	return bpf_redirect_peer(IFINDEX_DST, 0);
}

char __license[] SEC("license") = "GPL";
