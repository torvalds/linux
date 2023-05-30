/* Copyright (c) 2016 PLUMgrid
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#define KBUILD_MODNAME "foo"
#include <uapi/linux/bpf.h>
#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__type(key, u32);
	__type(value, long);
	__uint(max_entries, 256);
} rxcnt SEC(".maps");

static void swap_src_dst_mac(void *data)
{
	unsigned short *p = data;
	unsigned short dst[3];

	dst[0] = p[0];
	dst[1] = p[1];
	dst[2] = p[2];
	p[0] = p[3];
	p[1] = p[4];
	p[2] = p[5];
	p[3] = dst[0];
	p[4] = dst[1];
	p[5] = dst[2];
}

static int parse_ipv4(void *data, u64 nh_off, void *data_end)
{
	struct iphdr *iph = data + nh_off;

	if (iph + 1 > data_end)
		return 0;
	return iph->protocol;
}

static int parse_ipv6(void *data, u64 nh_off, void *data_end)
{
	struct ipv6hdr *ip6h = data + nh_off;

	if (ip6h + 1 > data_end)
		return 0;
	return ip6h->nexthdr;
}

#define XDPBUFSIZE	60
SEC("xdp.frags")
int xdp_prog1(struct xdp_md *ctx)
{
	__u8 pkt[XDPBUFSIZE] = {};
	void *data_end = &pkt[XDPBUFSIZE-1];
	void *data = pkt;
	struct ethhdr *eth = data;
	int rc = XDP_DROP;
	long *value;
	u16 h_proto;
	u64 nh_off;
	u32 ipproto;

	if (bpf_xdp_load_bytes(ctx, 0, pkt, sizeof(pkt)))
		return rc;

	nh_off = sizeof(*eth);
	if (data + nh_off > data_end)
		return rc;

	h_proto = eth->h_proto;

	/* Handle VLAN tagged packet */
	if (h_proto == htons(ETH_P_8021Q) || h_proto == htons(ETH_P_8021AD)) {
		struct vlan_hdr *vhdr;

		vhdr = data + nh_off;
		nh_off += sizeof(struct vlan_hdr);
		if (data + nh_off > data_end)
			return rc;
		h_proto = vhdr->h_vlan_encapsulated_proto;
	}
	/* Handle double VLAN tagged packet */
	if (h_proto == htons(ETH_P_8021Q) || h_proto == htons(ETH_P_8021AD)) {
		struct vlan_hdr *vhdr;

		vhdr = data + nh_off;
		nh_off += sizeof(struct vlan_hdr);
		if (data + nh_off > data_end)
			return rc;
		h_proto = vhdr->h_vlan_encapsulated_proto;
	}

	if (h_proto == htons(ETH_P_IP))
		ipproto = parse_ipv4(data, nh_off, data_end);
	else if (h_proto == htons(ETH_P_IPV6))
		ipproto = parse_ipv6(data, nh_off, data_end);
	else
		ipproto = 0;

	value = bpf_map_lookup_elem(&rxcnt, &ipproto);
	if (value)
		*value += 1;

	if (ipproto == IPPROTO_UDP) {
		swap_src_dst_mac(data);

		if (bpf_xdp_store_bytes(ctx, 0, pkt, sizeof(pkt)))
			return rc;

		rc = XDP_TX;
	}

	return rc;
}

char _license[] SEC("license") = "GPL";
