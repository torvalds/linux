/* Copyright (C) 2017 Cavium, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
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
#include <linux/slab.h>
#include <net/ip_fib.h>

struct trie_value {
	__u8 prefix[4];
	__be64 value;
	int ifindex;
	int metric;
	__be32 gw;
};

/* Key for lpm_trie*/
union key_4 {
	u32 b32[2];
	u8 b8[8];
};

struct arp_entry {
	__be64 mac;
	__be32 dst;
};

struct direct_map {
	struct arp_entry arp;
	int ifindex;
	__be64 mac;
};

/* Map for trie implementation*/
struct {
	__uint(type, BPF_MAP_TYPE_LPM_TRIE);
	__uint(key_size, 8);
	__uint(value_size, sizeof(struct trie_value));
	__uint(max_entries, 50);
	__uint(map_flags, BPF_F_NO_PREALLOC);
} lpm_map SEC(".maps");

/* Map for counter*/
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__type(key, u32);
	__type(value, u64);
	__uint(max_entries, 256);
} rxcnt SEC(".maps");

/* Map for ARP table*/
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, __be32);
	__type(value, __be64);
	__uint(max_entries, 50);
} arp_table SEC(".maps");

/* Map to keep the exact match entries in the route table*/
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, __be32);
	__type(value, struct direct_map);
	__uint(max_entries, 50);
} exact_match SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_DEVMAP);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
	__uint(max_entries, 100);
} tx_port SEC(".maps");

/* Function to set source and destination mac of the packet */
static inline void set_src_dst_mac(void *data, void *src, void *dst)
{
	unsigned short *source = src;
	unsigned short *dest  = dst;
	unsigned short *p = data;

	__builtin_memcpy(p, dest, 6);
	__builtin_memcpy(p + 3, source, 6);
}

/* Parse IPV4 packet to get SRC, DST IP and protocol */
static inline int parse_ipv4(void *data, u64 nh_off, void *data_end,
			     __be32 *src, __be32 *dest)
{
	struct iphdr *iph = data + nh_off;

	if (iph + 1 > data_end)
		return 0;
	*src = iph->saddr;
	*dest = iph->daddr;
	return iph->protocol;
}

SEC("xdp_router_ipv4")
int xdp_router_ipv4_prog(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	__be64 *dest_mac = NULL, *src_mac = NULL;
	void *data = (void *)(long)ctx->data;
	struct trie_value *prefix_value;
	int rc = XDP_DROP, forward_to;
	struct ethhdr *eth = data;
	union key_4 key4;
	long *value;
	u16 h_proto;
	u32 ipproto;
	u64 nh_off;

	nh_off = sizeof(*eth);
	if (data + nh_off > data_end)
		return rc;

	h_proto = eth->h_proto;

	if (h_proto == htons(ETH_P_8021Q) || h_proto == htons(ETH_P_8021AD)) {
		struct vlan_hdr *vhdr;

		vhdr = data + nh_off;
		nh_off += sizeof(struct vlan_hdr);
		if (data + nh_off > data_end)
			return rc;
		h_proto = vhdr->h_vlan_encapsulated_proto;
	}
	if (h_proto == htons(ETH_P_ARP)) {
		return XDP_PASS;
	} else if (h_proto == htons(ETH_P_IP)) {
		struct direct_map *direct_entry;
		__be32 src_ip = 0, dest_ip = 0;

		ipproto = parse_ipv4(data, nh_off, data_end, &src_ip, &dest_ip);
		direct_entry = bpf_map_lookup_elem(&exact_match, &dest_ip);
		/* Check for exact match, this would give a faster lookup*/
		if (direct_entry && direct_entry->mac && direct_entry->arp.mac) {
			src_mac = &direct_entry->mac;
			dest_mac = &direct_entry->arp.mac;
			forward_to = direct_entry->ifindex;
		} else {
			/* Look up in the trie for lpm*/
			key4.b32[0] = 32;
			key4.b8[4] = dest_ip & 0xff;
			key4.b8[5] = (dest_ip >> 8) & 0xff;
			key4.b8[6] = (dest_ip >> 16) & 0xff;
			key4.b8[7] = (dest_ip >> 24) & 0xff;
			prefix_value = bpf_map_lookup_elem(&lpm_map, &key4);
			if (!prefix_value)
				return XDP_DROP;
			src_mac = &prefix_value->value;
			if (!src_mac)
				return XDP_DROP;
			dest_mac = bpf_map_lookup_elem(&arp_table, &dest_ip);
			if (!dest_mac) {
				if (!prefix_value->gw)
					return XDP_DROP;
				dest_ip = prefix_value->gw;
				dest_mac = bpf_map_lookup_elem(&arp_table, &dest_ip);
			}
			forward_to = prefix_value->ifindex;
		}
	} else {
		ipproto = 0;
	}
	if (src_mac && dest_mac) {
		set_src_dst_mac(data, src_mac, dest_mac);
		value = bpf_map_lookup_elem(&rxcnt, &ipproto);
		if (value)
			*value += 1;
		return  bpf_redirect_map(&tx_port, forward_to, 0);
	}
	return rc;
}

char _license[] SEC("license") = "GPL";
