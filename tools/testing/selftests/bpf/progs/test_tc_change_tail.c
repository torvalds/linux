// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

#ifndef PAGE_SIZE
#define PAGE_SIZE __PAGE_SIZE
#endif
#define BPF_SKB_MAX_LEN (PAGE_SIZE << 2)

long change_tail_ret = 1;

static __always_inline struct iphdr *parse_ip_header(struct __sk_buff *skb, int *ip_proto)
{
	void *data_end = (void *)(long)skb->data_end;
	void *data = (void *)(long)skb->data;
	struct ethhdr *eth = data;
	struct iphdr *iph;

	/* Verify Ethernet header */
	if ((void *)(data + sizeof(*eth)) > data_end)
		return NULL;

	/* Skip Ethernet header to get to IP header */
	iph = (void *)(data + sizeof(struct ethhdr));

	/* Verify IP header */
	if ((void *)(data + sizeof(struct ethhdr) + sizeof(*iph)) > data_end)
		return NULL;

	/* Basic IP header validation */
	if (iph->version != 4)  /* Only support IPv4 */
		return NULL;

	if (iph->ihl < 5)  /* Minimum IP header length */
		return NULL;

	*ip_proto = iph->protocol;
	return iph;
}

static __always_inline struct udphdr *parse_udp_header(struct __sk_buff *skb, struct iphdr *iph)
{
	void *data_end = (void *)(long)skb->data_end;
	void *hdr = (void *)iph;
	struct udphdr *udp;

	/* Calculate UDP header position */
	udp = hdr + (iph->ihl * 4);
	hdr = (void *)udp;

	/* Verify UDP header bounds */
	if ((void *)(hdr + sizeof(*udp)) > data_end)
		return NULL;

	return udp;
}

SEC("tc/ingress")
int change_tail(struct __sk_buff *skb)
{
	int len = skb->len;
	struct udphdr *udp;
	struct iphdr *iph;
	void *data_end;
	char *payload;
	int ip_proto;

	bpf_skb_pull_data(skb, len);

	data_end = (void *)(long)skb->data_end;
	iph = parse_ip_header(skb, &ip_proto);
	if (!iph)
		return TCX_PASS;

	if (ip_proto != IPPROTO_UDP)
		return TCX_PASS;

	udp = parse_udp_header(skb, iph);
	if (!udp)
		return TCX_PASS;

	payload = (char *)udp + (sizeof(struct udphdr));
	if (payload + 1 > (char *)data_end)
		return TCX_PASS;

	if (payload[0] == 'T') { /* Trim the packet */
		change_tail_ret = bpf_skb_change_tail(skb, len - 1, 0);
		if (!change_tail_ret)
			bpf_skb_change_tail(skb, len, 0);
		return TCX_PASS;
	} else if (payload[0] == 'G') { /* Grow the packet */
		change_tail_ret = bpf_skb_change_tail(skb, len + 1, 0);
		if (!change_tail_ret)
			bpf_skb_change_tail(skb, len, 0);
		return TCX_PASS;
	} else if (payload[0] == 'E') { /* Error */
		change_tail_ret = bpf_skb_change_tail(skb, BPF_SKB_MAX_LEN, 0);
		return TCX_PASS;
	} else if (payload[0] == 'Z') { /* Zero */
		change_tail_ret = bpf_skb_change_tail(skb, 0, 0);
		return TCX_PASS;
	}
	return TCX_DROP;
}

char _license[] SEC("license") = "GPL";
