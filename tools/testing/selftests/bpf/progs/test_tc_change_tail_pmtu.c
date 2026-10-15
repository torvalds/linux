// SPDX-License-Identifier: GPL-2.0

#include <stdbool.h>
#include <stddef.h>

#include <linux/bpf.h>
#include <linux/icmp.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define ICMP_SAMPLE_LEN	(sizeof(struct iphdr) + 8)
#define ICMP_HDRS_LEN	(sizeof(struct iphdr) + sizeof(struct icmphdr))

__be16 server_port = 0;
__u16 pmtu = 0;

long change_tail_ret = 1;
long adjust_room_ret = 0;
bool icmp_sent = false;
bool icmp_err = false;

static __always_inline __sum16 csum_fold(__wsum csum)
{
	csum = (csum & 0xffff) + (csum >> 16);
	csum = (csum & 0xffff) + (csum >> 16);

	return (__sum16)~csum;
}

SEC("tc/egress")
int change_tail_icmp(struct __sk_buff *skb)
{
	__u8 smac[ETH_ALEN], dmac[ETH_ALEN];
	void *data, *data_end;
	struct icmphdr *icmp;
	struct ethhdr *eth;
	struct tcphdr *tcp;
	__be32 saddr, daddr;
	struct iphdr *ip;
	__wsum csum;

	if (icmp_sent || icmp_err)
		return TCX_PASS;

	data = (void *)(long)skb->data;
	data_end = (void *)(long)skb->data_end;

	eth = data;
	if ((void *)(eth + 1) > data_end)
		return TCX_PASS;
	if (eth->h_proto != bpf_htons(ETH_P_IP))
		return TCX_PASS;

	ip = (void *)(eth + 1);
	if ((void *)(ip + 1) > data_end)
		return TCX_PASS;
	if (ip->ihl != 5 || ip->protocol != IPPROTO_TCP)
		return TCX_PASS;

	tcp = (void *)(ip + 1);
	if ((void *)(tcp + 1) > data_end)
		return TCX_PASS;
	if (tcp->dest != server_port)
		return TCX_PASS;
	if (bpf_ntohs(ip->tot_len) <= sizeof(*ip) + tcp->doff * 4)
		return TCX_PASS;

	__builtin_memcpy(smac, eth->h_source, ETH_ALEN);
	__builtin_memcpy(dmac, eth->h_dest, ETH_ALEN);
	saddr = ip->saddr;
	daddr = ip->daddr;

	change_tail_ret = bpf_skb_change_tail(skb, ETH_HLEN + ICMP_SAMPLE_LEN, 0);
	if (change_tail_ret) {
		icmp_err = true;
		return TCX_PASS;
	}

	adjust_room_ret = bpf_skb_adjust_room(skb, ICMP_HDRS_LEN,
					      BPF_ADJ_ROOM_MAC,
					      BPF_F_ADJ_ROOM_NO_CSUM_RESET);
	if (adjust_room_ret) {
		icmp_err = true;
		return TCX_DROP;
	}

	data = (void *)(long)skb->data;
	data_end = (void *)(long)skb->data_end;

	eth = data;
	ip = (void *)(eth + 1);
	icmp = (void *)(ip + 1);
	if ((void *)icmp + sizeof(*icmp) + ICMP_SAMPLE_LEN > data_end) {
		icmp_err = true;
		return TCX_DROP;
	}

	__builtin_memcpy(eth->h_dest, smac, ETH_ALEN);
	__builtin_memcpy(eth->h_source, dmac, ETH_ALEN);

	__builtin_memset(icmp, 0, sizeof(*icmp));
	icmp->type = ICMP_DEST_UNREACH;
	icmp->code = ICMP_FRAG_NEEDED;
	icmp->un.frag.mtu = bpf_htons(pmtu);

	__builtin_memset(ip, 0, sizeof(*ip));
	ip->version = 4;
	ip->ihl = 5;
	ip->ttl = 64;
	ip->protocol = IPPROTO_ICMP;
	ip->tot_len = bpf_htons(ICMP_HDRS_LEN + ICMP_SAMPLE_LEN);
	ip->saddr = daddr;
	ip->daddr = saddr;

	csum = bpf_csum_diff(NULL, 0, (__be32 *)icmp,
			     sizeof(*icmp) + ICMP_SAMPLE_LEN, 0);
	icmp->checksum = csum_fold(csum);
	csum = bpf_csum_diff(NULL, 0, (__be32 *)ip, sizeof(*ip), 0);
	ip->check = csum_fold(csum);
	icmp_sent = true;
	return bpf_redirect(skb->ifindex, BPF_F_INGRESS);
}

char _license[] SEC("license") = "GPL";
