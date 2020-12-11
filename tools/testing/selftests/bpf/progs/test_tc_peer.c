// SPDX-License-Identifier: GPL-2.0
#include <stdint.h>
#include <stdbool.h>

#include <linux/bpf.h>
#include <linux/stddef.h>
#include <linux/pkt_cls.h>

#include <bpf/bpf_helpers.h>

enum {
	dev_src,
	dev_dst,
};

struct bpf_map_def SEC("maps") ifindex_map = {
	.type		= BPF_MAP_TYPE_ARRAY,
	.key_size	= sizeof(int),
	.value_size	= sizeof(int),
	.max_entries	= 2,
};

static __always_inline int get_dev_ifindex(int which)
{
	int *ifindex = bpf_map_lookup_elem(&ifindex_map, &which);

	return ifindex ? *ifindex : 0;
}

SEC("chk_egress") int tc_chk(struct __sk_buff *skb)
{
	return TC_ACT_SHOT;
}

SEC("dst_ingress") int tc_dst(struct __sk_buff *skb)
{
	return bpf_redirect_peer(get_dev_ifindex(dev_src), 0);
}

SEC("src_ingress") int tc_src(struct __sk_buff *skb)
{
	return bpf_redirect_peer(get_dev_ifindex(dev_dst), 0);
}

char __license[] SEC("license") = "GPL";
