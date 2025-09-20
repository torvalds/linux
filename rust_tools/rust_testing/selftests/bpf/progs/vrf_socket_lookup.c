// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include <linux/ip.h>
#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/pkt_cls.h>
#include <stdbool.h>

int lookup_status;
bool test_xdp;
bool tcp_skc;

#define CUR_NS BPF_F_CURRENT_NETNS

static void socket_lookup(void *ctx, void *data_end, void *data)
{
	struct ethhdr *eth = data;
	struct bpf_sock_tuple *tp;
	struct bpf_sock *sk;
	struct iphdr *iph;
	int tplen;

	if (eth + 1 > data_end)
		return;

	if (eth->h_proto != bpf_htons(ETH_P_IP))
		return;

	iph = (struct iphdr *)(eth + 1);
	if (iph + 1 > data_end)
		return;

	tp = (struct bpf_sock_tuple *)&iph->saddr;
	tplen = sizeof(tp->ipv4);
	if ((void *)tp + tplen > data_end)
		return;

	switch (iph->protocol) {
	case IPPROTO_TCP:
		if (tcp_skc)
			sk = bpf_skc_lookup_tcp(ctx, tp, tplen, CUR_NS, 0);
		else
			sk = bpf_sk_lookup_tcp(ctx, tp, tplen, CUR_NS, 0);
		break;
	case IPPROTO_UDP:
		sk = bpf_sk_lookup_udp(ctx, tp, tplen, CUR_NS, 0);
		break;
	default:
		return;
	}

	lookup_status = 0;

	if (sk) {
		bpf_sk_release(sk);
		lookup_status = 1;
	}
}

SEC("tc")
int tc_socket_lookup(struct __sk_buff *skb)
{
	void *data_end = (void *)(long)skb->data_end;
	void *data = (void *)(long)skb->data;

	if (test_xdp)
		return TC_ACT_UNSPEC;

	socket_lookup(skb, data_end, data);
	return TC_ACT_UNSPEC;
}

SEC("xdp")
int xdp_socket_lookup(struct xdp_md *xdp)
{
	void *data_end = (void *)(long)xdp->data_end;
	void *data = (void *)(long)xdp->data;

	if (!test_xdp)
		return XDP_PASS;

	socket_lookup(xdp, data_end, data);
	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
