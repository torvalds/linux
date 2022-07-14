/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2018 Covalent IO, Inc. http://covalent.io

#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/pkt_cls.h>
#include <linux/tcp.h>
#include <sys/socket.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

char _license[] SEC("license") = "GPL";

/* Fill 'tuple' with L3 info, and attempt to find L4. On fail, return NULL. */
static struct bpf_sock_tuple *get_tuple(void *data, __u64 nh_off,
					void *data_end, __u16 eth_proto,
					bool *ipv4)
{
	struct bpf_sock_tuple *result;
	__u8 proto = 0;
	__u64 ihl_len;

	if (eth_proto == bpf_htons(ETH_P_IP)) {
		struct iphdr *iph = (struct iphdr *)(data + nh_off);

		if (iph + 1 > data_end)
			return NULL;
		ihl_len = iph->ihl * 4;
		proto = iph->protocol;
		*ipv4 = true;
		result = (struct bpf_sock_tuple *)&iph->saddr;
	} else if (eth_proto == bpf_htons(ETH_P_IPV6)) {
		struct ipv6hdr *ip6h = (struct ipv6hdr *)(data + nh_off);

		if (ip6h + 1 > data_end)
			return NULL;
		ihl_len = sizeof(*ip6h);
		proto = ip6h->nexthdr;
		*ipv4 = true;
		result = (struct bpf_sock_tuple *)&ip6h->saddr;
	}

	if (data + nh_off + ihl_len > data_end || proto != IPPROTO_TCP)
		return NULL;

	return result;
}

SEC("?tc")
int sk_lookup_success(struct __sk_buff *skb)
{
	void *data_end = (void *)(long)skb->data_end;
	void *data = (void *)(long)skb->data;
	struct ethhdr *eth = (struct ethhdr *)(data);
	struct bpf_sock_tuple *tuple;
	struct bpf_sock *sk;
	size_t tuple_len;
	bool ipv4;

	if (eth + 1 > data_end)
		return TC_ACT_SHOT;

	tuple = get_tuple(data, sizeof(*eth), data_end, eth->h_proto, &ipv4);
	if (!tuple || tuple + sizeof *tuple > data_end)
		return TC_ACT_SHOT;

	tuple_len = ipv4 ? sizeof(tuple->ipv4) : sizeof(tuple->ipv6);
	sk = bpf_sk_lookup_tcp(skb, tuple, tuple_len, BPF_F_CURRENT_NETNS, 0);
	bpf_printk("sk=%d\n", sk ? 1 : 0);
	if (sk)
		bpf_sk_release(sk);
	return sk ? TC_ACT_OK : TC_ACT_UNSPEC;
}

SEC("?tc")
int sk_lookup_success_simple(struct __sk_buff *skb)
{
	struct bpf_sock_tuple tuple = {};
	struct bpf_sock *sk;

	sk = bpf_sk_lookup_tcp(skb, &tuple, sizeof(tuple), BPF_F_CURRENT_NETNS, 0);
	if (sk)
		bpf_sk_release(sk);
	return 0;
}

SEC("?tc")
int err_use_after_free(struct __sk_buff *skb)
{
	struct bpf_sock_tuple tuple = {};
	struct bpf_sock *sk;
	__u32 family = 0;

	sk = bpf_sk_lookup_tcp(skb, &tuple, sizeof(tuple), BPF_F_CURRENT_NETNS, 0);
	if (sk) {
		bpf_sk_release(sk);
		family = sk->family;
	}
	return family;
}

SEC("?tc")
int err_modify_sk_pointer(struct __sk_buff *skb)
{
	struct bpf_sock_tuple tuple = {};
	struct bpf_sock *sk;
	__u32 family;

	sk = bpf_sk_lookup_tcp(skb, &tuple, sizeof(tuple), BPF_F_CURRENT_NETNS, 0);
	if (sk) {
		sk += 1;
		bpf_sk_release(sk);
	}
	return 0;
}

SEC("?tc")
int err_modify_sk_or_null_pointer(struct __sk_buff *skb)
{
	struct bpf_sock_tuple tuple = {};
	struct bpf_sock *sk;
	__u32 family;

	sk = bpf_sk_lookup_tcp(skb, &tuple, sizeof(tuple), BPF_F_CURRENT_NETNS, 0);
	sk += 1;
	if (sk)
		bpf_sk_release(sk);
	return 0;
}

SEC("?tc")
int err_no_release(struct __sk_buff *skb)
{
	struct bpf_sock_tuple tuple = {};

	bpf_sk_lookup_tcp(skb, &tuple, sizeof(tuple), BPF_F_CURRENT_NETNS, 0);
	return 0;
}

SEC("?tc")
int err_release_twice(struct __sk_buff *skb)
{
	struct bpf_sock_tuple tuple = {};
	struct bpf_sock *sk;

	sk = bpf_sk_lookup_tcp(skb, &tuple, sizeof(tuple), BPF_F_CURRENT_NETNS, 0);
	bpf_sk_release(sk);
	bpf_sk_release(sk);
	return 0;
}

SEC("?tc")
int err_release_unchecked(struct __sk_buff *skb)
{
	struct bpf_sock_tuple tuple = {};
	struct bpf_sock *sk;

	sk = bpf_sk_lookup_tcp(skb, &tuple, sizeof(tuple), BPF_F_CURRENT_NETNS, 0);
	bpf_sk_release(sk);
	return 0;
}

void lookup_no_release(struct __sk_buff *skb)
{
	struct bpf_sock_tuple tuple = {};
	bpf_sk_lookup_tcp(skb, &tuple, sizeof(tuple), BPF_F_CURRENT_NETNS, 0);
}

SEC("?tc")
int err_no_release_subcall(struct __sk_buff *skb)
{
	lookup_no_release(skb);
	return 0;
}
