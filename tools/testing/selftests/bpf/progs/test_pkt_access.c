// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 Facebook
 */
#include <stddef.h>
#include <string.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/pkt_cls.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define barrier() __asm__ __volatile__("": : :"memory")

/* llvm will optimize both subprograms into exactly the same BPF assembly
 *
 * Disassembly of section .text:
 *
 * 0000000000000000 test_pkt_access_subprog1:
 * ; 	return skb->len * 2;
 *        0:	61 10 00 00 00 00 00 00	r0 = *(u32 *)(r1 + 0)
 *        1:	64 00 00 00 01 00 00 00	w0 <<= 1
 *        2:	95 00 00 00 00 00 00 00	exit
 *
 * 0000000000000018 test_pkt_access_subprog2:
 * ; 	return skb->len * val;
 *        3:	61 10 00 00 00 00 00 00	r0 = *(u32 *)(r1 + 0)
 *        4:	64 00 00 00 01 00 00 00	w0 <<= 1
 *        5:	95 00 00 00 00 00 00 00	exit
 *
 * Which makes it an interesting test for BTF-enabled verifier.
 */
static __attribute__ ((noinline))
int test_pkt_access_subprog1(volatile struct __sk_buff *skb)
{
	return skb->len * 2;
}

static __attribute__ ((noinline))
int test_pkt_access_subprog2(int val, volatile struct __sk_buff *skb)
{
	return skb->len * val;
}

#define MAX_STACK (512 - 2 * 32)

__attribute__ ((noinline))
int get_skb_len(struct __sk_buff *skb)
{
	volatile char buf[MAX_STACK] = {};

	return skb->len;
}

__attribute__ ((noinline))
int get_constant(long val)
{
	return val - 122;
}

int get_skb_ifindex(int, struct __sk_buff *skb, int);

__attribute__ ((noinline))
int test_pkt_access_subprog3(int val, struct __sk_buff *skb)
{
	return get_skb_len(skb) * get_skb_ifindex(val, skb, get_constant(123));
}

__attribute__ ((noinline))
int get_skb_ifindex(int val, struct __sk_buff *skb, int var)
{
	volatile char buf[MAX_STACK] = {};

	return skb->ifindex * val * var;
}

__attribute__ ((noinline))
int test_pkt_write_access_subprog(struct __sk_buff *skb, __u32 off)
{
	void *data = (void *)(long)skb->data;
	void *data_end = (void *)(long)skb->data_end;
	struct tcphdr *tcp = NULL;

	if (off > sizeof(struct ethhdr) + sizeof(struct ipv6hdr))
		return -1;

	tcp = data + off;
	if (tcp + 1 > data_end)
		return -1;
	/* make modification to the packet data */
	tcp->check++;
	return 0;
}

SEC("tc")
int test_pkt_access(struct __sk_buff *skb)
{
	void *data_end = (void *)(long)skb->data_end;
	void *data = (void *)(long)skb->data;
	struct ethhdr *eth = (struct ethhdr *)(data);
	struct tcphdr *tcp = NULL;
	__u8 proto = 255;
	__u64 ihl_len;

	if (eth + 1 > data_end)
		return TC_ACT_SHOT;

	if (eth->h_proto == bpf_htons(ETH_P_IP)) {
		struct iphdr *iph = (struct iphdr *)(eth + 1);

		if (iph + 1 > data_end)
			return TC_ACT_SHOT;
		ihl_len = iph->ihl * 4;
		proto = iph->protocol;
		tcp = (struct tcphdr *)((void *)(iph) + ihl_len);
	} else if (eth->h_proto == bpf_htons(ETH_P_IPV6)) {
		struct ipv6hdr *ip6h = (struct ipv6hdr *)(eth + 1);

		if (ip6h + 1 > data_end)
			return TC_ACT_SHOT;
		ihl_len = sizeof(*ip6h);
		proto = ip6h->nexthdr;
		tcp = (struct tcphdr *)((void *)(ip6h) + ihl_len);
	}

	if (test_pkt_access_subprog1(skb) != skb->len * 2)
		return TC_ACT_SHOT;
	if (test_pkt_access_subprog2(2, skb) != skb->len * 2)
		return TC_ACT_SHOT;
	if (test_pkt_access_subprog3(3, skb) != skb->len * 3 * skb->ifindex)
		return TC_ACT_SHOT;
	if (tcp) {
		if (test_pkt_write_access_subprog(skb, (void *)tcp - data))
			return TC_ACT_SHOT;
		if (((void *)(tcp) + 20) > data_end || proto != 6)
			return TC_ACT_SHOT;
		barrier(); /* to force ordering of checks */
		if (((void *)(tcp) + 18) > data_end)
			return TC_ACT_SHOT;
		if (tcp->urg_ptr == 123)
			return TC_ACT_OK;
	}

	return TC_ACT_UNSPEC;
}
