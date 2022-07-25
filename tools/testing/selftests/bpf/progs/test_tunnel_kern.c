// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2016 VMware
 * Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <stddef.h>
#include <string.h>
#include <arpa/inet.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/icmp.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/pkt_cls.h>
#include <linux/erspan.h>
#include <linux/udp.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define log_err(__ret) bpf_printk("ERROR line:%d ret:%d\n", __LINE__, __ret)

#define VXLAN_UDP_PORT 4789

/* Only IPv4 address assigned to veth1.
 * 172.16.1.200
 */
#define ASSIGNED_ADDR_VETH1 0xac1001c8

struct geneve_opt {
	__be16	opt_class;
	__u8	type;
	__u8	length:5;
	__u8	r3:1;
	__u8	r2:1;
	__u8	r1:1;
	__u8	opt_data[8]; /* hard-coded to 8 byte */
};

struct vxlanhdr {
	__be32 vx_flags;
	__be32 vx_vni;
} __attribute__((packed));

struct vxlan_metadata {
	__u32     gbp;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} local_ip_map SEC(".maps");

SEC("tc")
int gre_set_tunnel(struct __sk_buff *skb)
{
	int ret;
	struct bpf_tunnel_key key;

	__builtin_memset(&key, 0x0, sizeof(key));
	key.remote_ipv4 = 0xac100164; /* 172.16.1.100 */
	key.tunnel_id = 2;
	key.tunnel_tos = 0;
	key.tunnel_ttl = 64;

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_ZERO_CSUM_TX | BPF_F_SEQ_NUMBER);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("tc")
int gre_get_tunnel(struct __sk_buff *skb)
{
	int ret;
	struct bpf_tunnel_key key;

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key), 0);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	bpf_printk("key %d remote ip 0x%x\n", key.tunnel_id, key.remote_ipv4);
	return TC_ACT_OK;
}

SEC("tc")
int ip6gretap_set_tunnel(struct __sk_buff *skb)
{
	struct bpf_tunnel_key key;
	int ret;

	__builtin_memset(&key, 0x0, sizeof(key));
	key.remote_ipv6[3] = bpf_htonl(0x11); /* ::11 */
	key.tunnel_id = 2;
	key.tunnel_tos = 0;
	key.tunnel_ttl = 64;
	key.tunnel_label = 0xabcde;

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6 | BPF_F_ZERO_CSUM_TX |
				     BPF_F_SEQ_NUMBER);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("tc")
int ip6gretap_get_tunnel(struct __sk_buff *skb)
{
	struct bpf_tunnel_key key;
	int ret;

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	bpf_printk("key %d remote ip6 ::%x label %x\n",
		   key.tunnel_id, key.remote_ipv6[3], key.tunnel_label);

	return TC_ACT_OK;
}

SEC("tc")
int erspan_set_tunnel(struct __sk_buff *skb)
{
	struct bpf_tunnel_key key;
	struct erspan_metadata md;
	int ret;

	__builtin_memset(&key, 0x0, sizeof(key));
	key.remote_ipv4 = 0xac100164; /* 172.16.1.100 */
	key.tunnel_id = 2;
	key.tunnel_tos = 0;
	key.tunnel_ttl = 64;

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_ZERO_CSUM_TX);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	__builtin_memset(&md, 0, sizeof(md));
#ifdef ERSPAN_V1
	md.version = 1;
	md.u.index = bpf_htonl(123);
#else
	__u8 direction = 1;
	__u8 hwid = 7;

	md.version = 2;
	md.u.md2.dir = direction;
	md.u.md2.hwid = hwid & 0xf;
	md.u.md2.hwid_upper = (hwid >> 4) & 0x3;
#endif

	ret = bpf_skb_set_tunnel_opt(skb, &md, sizeof(md));
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("tc")
int erspan_get_tunnel(struct __sk_buff *skb)
{
	struct bpf_tunnel_key key;
	struct erspan_metadata md;
	__u32 index;
	int ret;

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key), 0);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	ret = bpf_skb_get_tunnel_opt(skb, &md, sizeof(md));
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	bpf_printk("key %d remote ip 0x%x erspan version %d\n",
		   key.tunnel_id, key.remote_ipv4, md.version);

#ifdef ERSPAN_V1
	index = bpf_ntohl(md.u.index);
	bpf_printk("\tindex %x\n", index);
#else
	bpf_printk("\tdirection %d hwid %x timestamp %u\n",
		   md.u.md2.dir,
		   (md.u.md2.hwid_upper << 4) + md.u.md2.hwid,
		   bpf_ntohl(md.u.md2.timestamp));
#endif

	return TC_ACT_OK;
}

SEC("tc")
int ip4ip6erspan_set_tunnel(struct __sk_buff *skb)
{
	struct bpf_tunnel_key key;
	struct erspan_metadata md;
	int ret;

	__builtin_memset(&key, 0x0, sizeof(key));
	key.remote_ipv6[3] = bpf_htonl(0x11);
	key.tunnel_id = 2;
	key.tunnel_tos = 0;
	key.tunnel_ttl = 64;

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	__builtin_memset(&md, 0, sizeof(md));

#ifdef ERSPAN_V1
	md.u.index = bpf_htonl(123);
	md.version = 1;
#else
	__u8 direction = 0;
	__u8 hwid = 17;

	md.version = 2;
	md.u.md2.dir = direction;
	md.u.md2.hwid = hwid & 0xf;
	md.u.md2.hwid_upper = (hwid >> 4) & 0x3;
#endif

	ret = bpf_skb_set_tunnel_opt(skb, &md, sizeof(md));
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("tc")
int ip4ip6erspan_get_tunnel(struct __sk_buff *skb)
{
	struct bpf_tunnel_key key;
	struct erspan_metadata md;
	__u32 index;
	int ret;

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	ret = bpf_skb_get_tunnel_opt(skb, &md, sizeof(md));
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	bpf_printk("ip6erspan get key %d remote ip6 ::%x erspan version %d\n",
		   key.tunnel_id, key.remote_ipv4, md.version);

#ifdef ERSPAN_V1
	index = bpf_ntohl(md.u.index);
	bpf_printk("\tindex %x\n", index);
#else
	bpf_printk("\tdirection %d hwid %x timestamp %u\n",
		   md.u.md2.dir,
		   (md.u.md2.hwid_upper << 4) + md.u.md2.hwid,
		   bpf_ntohl(md.u.md2.timestamp));
#endif

	return TC_ACT_OK;
}

SEC("tc")
int vxlan_set_tunnel_dst(struct __sk_buff *skb)
{
	int ret;
	struct bpf_tunnel_key key;
	struct vxlan_metadata md;
	__u32 index = 0;
	__u32 *local_ip = NULL;

	local_ip = bpf_map_lookup_elem(&local_ip_map, &index);
	if (!local_ip) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	__builtin_memset(&key, 0x0, sizeof(key));
	key.local_ipv4 = 0xac100164; /* 172.16.1.100 */
	key.remote_ipv4 = *local_ip;
	key.tunnel_id = 2;
	key.tunnel_tos = 0;
	key.tunnel_ttl = 64;

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_ZERO_CSUM_TX);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	md.gbp = 0x800FF; /* Set VXLAN Group Policy extension */
	ret = bpf_skb_set_tunnel_opt(skb, &md, sizeof(md));
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("tc")
int vxlan_set_tunnel_src(struct __sk_buff *skb)
{
	int ret;
	struct bpf_tunnel_key key;
	struct vxlan_metadata md;
	__u32 index = 0;
	__u32 *local_ip = NULL;

	local_ip = bpf_map_lookup_elem(&local_ip_map, &index);
	if (!local_ip) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	__builtin_memset(&key, 0x0, sizeof(key));
	key.local_ipv4 = *local_ip;
	key.remote_ipv4 = 0xac100164; /* 172.16.1.100 */
	key.tunnel_id = 2;
	key.tunnel_tos = 0;
	key.tunnel_ttl = 64;

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_ZERO_CSUM_TX);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	md.gbp = 0x800FF; /* Set VXLAN Group Policy extension */
	ret = bpf_skb_set_tunnel_opt(skb, &md, sizeof(md));
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("tc")
int vxlan_get_tunnel_src(struct __sk_buff *skb)
{
	int ret;
	struct bpf_tunnel_key key;
	struct vxlan_metadata md;
	__u32 orig_daddr;
	__u32 index = 0;

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key), 0);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	ret = bpf_skb_get_tunnel_opt(skb, &md, sizeof(md));
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	if (key.local_ipv4 != ASSIGNED_ADDR_VETH1 || md.gbp != 0x800FF) {
		bpf_printk("vxlan key %d local ip 0x%x remote ip 0x%x gbp 0x%x\n",
			   key.tunnel_id, key.local_ipv4,
			   key.remote_ipv4, md.gbp);
		log_err(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("tc")
int veth_set_outer_dst(struct __sk_buff *skb)
{
	struct ethhdr *eth = (struct ethhdr *)(long)skb->data;
	__u32 assigned_ip = bpf_htonl(ASSIGNED_ADDR_VETH1);
	void *data_end = (void *)(long)skb->data_end;
	struct udphdr *udph;
	struct iphdr *iph;
	__u32 index = 0;
	int ret = 0;
	int shrink;
	__s64 csum;

	if ((void *)eth + sizeof(*eth) > data_end) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	if (eth->h_proto != bpf_htons(ETH_P_IP))
		return TC_ACT_OK;

	iph = (struct iphdr *)(eth + 1);
	if ((void *)iph + sizeof(*iph) > data_end) {
		log_err(ret);
		return TC_ACT_SHOT;
	}
	if (iph->protocol != IPPROTO_UDP)
		return TC_ACT_OK;

	udph = (struct udphdr *)(iph + 1);
	if ((void *)udph + sizeof(*udph) > data_end) {
		log_err(ret);
		return TC_ACT_SHOT;
	}
	if (udph->dest != bpf_htons(VXLAN_UDP_PORT))
		return TC_ACT_OK;

	if (iph->daddr != assigned_ip) {
		csum = bpf_csum_diff(&iph->daddr, sizeof(__u32), &assigned_ip,
				     sizeof(__u32), 0);
		if (bpf_skb_store_bytes(skb, ETH_HLEN + offsetof(struct iphdr, daddr),
					&assigned_ip, sizeof(__u32), 0) < 0) {
			log_err(ret);
			return TC_ACT_SHOT;
		}
		if (bpf_l3_csum_replace(skb, ETH_HLEN + offsetof(struct iphdr, check),
					0, csum, 0) < 0) {
			log_err(ret);
			return TC_ACT_SHOT;
		}
		bpf_skb_change_type(skb, PACKET_HOST);
	}
	return TC_ACT_OK;
}

SEC("tc")
int ip6vxlan_set_tunnel_dst(struct __sk_buff *skb)
{
	struct bpf_tunnel_key key;
	int ret;
	__u32 index = 0;
	__u32 *local_ip;

	local_ip = bpf_map_lookup_elem(&local_ip_map, &index);
	if (!local_ip) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	__builtin_memset(&key, 0x0, sizeof(key));
	key.local_ipv6[3] = bpf_htonl(0x11); /* ::11 */
	key.remote_ipv6[3] = bpf_htonl(*local_ip);
	key.tunnel_id = 22;
	key.tunnel_tos = 0;
	key.tunnel_ttl = 64;

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("tc")
int ip6vxlan_set_tunnel_src(struct __sk_buff *skb)
{
	struct bpf_tunnel_key key;
	int ret;
	__u32 index = 0;
	__u32 *local_ip;

	local_ip = bpf_map_lookup_elem(&local_ip_map, &index);
	if (!local_ip) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	__builtin_memset(&key, 0x0, sizeof(key));
	key.local_ipv6[3] = bpf_htonl(*local_ip);
	key.remote_ipv6[3] = bpf_htonl(0x11); /* ::11 */
	key.tunnel_id = 22;
	key.tunnel_tos = 0;
	key.tunnel_ttl = 64;

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("tc")
int ip6vxlan_get_tunnel_src(struct __sk_buff *skb)
{
	struct bpf_tunnel_key key;
	int ret;
	__u32 index = 0;
	__u32 *local_ip;

	local_ip = bpf_map_lookup_elem(&local_ip_map, &index);
	if (!local_ip) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	if (bpf_ntohl(key.local_ipv6[3]) != *local_ip) {
		bpf_printk("ip6vxlan key %d local ip6 ::%x remote ip6 ::%x label 0x%x\n",
			   key.tunnel_id, bpf_ntohl(key.local_ipv6[3]),
			   bpf_ntohl(key.remote_ipv6[3]), key.tunnel_label);
		bpf_printk("local_ip 0x%x\n", *local_ip);
		log_err(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("tc")
int geneve_set_tunnel(struct __sk_buff *skb)
{
	int ret;
	struct bpf_tunnel_key key;
	struct geneve_opt gopt;

	__builtin_memset(&key, 0x0, sizeof(key));
	key.remote_ipv4 = 0xac100164; /* 172.16.1.100 */
	key.tunnel_id = 2;
	key.tunnel_tos = 0;
	key.tunnel_ttl = 64;

	__builtin_memset(&gopt, 0x0, sizeof(gopt));
	gopt.opt_class = bpf_htons(0x102); /* Open Virtual Networking (OVN) */
	gopt.type = 0x08;
	gopt.r1 = 0;
	gopt.r2 = 0;
	gopt.r3 = 0;
	gopt.length = 2; /* 4-byte multiple */
	*(int *) &gopt.opt_data = bpf_htonl(0xdeadbeef);

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_ZERO_CSUM_TX);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	ret = bpf_skb_set_tunnel_opt(skb, &gopt, sizeof(gopt));
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("tc")
int geneve_get_tunnel(struct __sk_buff *skb)
{
	int ret;
	struct bpf_tunnel_key key;
	struct geneve_opt gopt;

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key), 0);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	ret = bpf_skb_get_tunnel_opt(skb, &gopt, sizeof(gopt));
	if (ret < 0)
		gopt.opt_class = 0;

	bpf_printk("key %d remote ip 0x%x geneve class 0x%x\n",
		   key.tunnel_id, key.remote_ipv4, gopt.opt_class);
	return TC_ACT_OK;
}

SEC("tc")
int ip6geneve_set_tunnel(struct __sk_buff *skb)
{
	struct bpf_tunnel_key key;
	struct geneve_opt gopt;
	int ret;

	__builtin_memset(&key, 0x0, sizeof(key));
	key.remote_ipv6[3] = bpf_htonl(0x11); /* ::11 */
	key.tunnel_id = 22;
	key.tunnel_tos = 0;
	key.tunnel_ttl = 64;

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	__builtin_memset(&gopt, 0x0, sizeof(gopt));
	gopt.opt_class = bpf_htons(0x102); /* Open Virtual Networking (OVN) */
	gopt.type = 0x08;
	gopt.r1 = 0;
	gopt.r2 = 0;
	gopt.r3 = 0;
	gopt.length = 2; /* 4-byte multiple */
	*(int *) &gopt.opt_data = bpf_htonl(0xfeedbeef);

	ret = bpf_skb_set_tunnel_opt(skb, &gopt, sizeof(gopt));
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("tc")
int ip6geneve_get_tunnel(struct __sk_buff *skb)
{
	struct bpf_tunnel_key key;
	struct geneve_opt gopt;
	int ret;

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	ret = bpf_skb_get_tunnel_opt(skb, &gopt, sizeof(gopt));
	if (ret < 0)
		gopt.opt_class = 0;

	bpf_printk("key %d remote ip 0x%x geneve class 0x%x\n",
		   key.tunnel_id, key.remote_ipv4, gopt.opt_class);

	return TC_ACT_OK;
}

SEC("tc")
int ipip_set_tunnel(struct __sk_buff *skb)
{
	struct bpf_tunnel_key key = {};
	void *data = (void *)(long)skb->data;
	struct iphdr *iph = data;
	void *data_end = (void *)(long)skb->data_end;
	int ret;

	/* single length check */
	if (data + sizeof(*iph) > data_end) {
		log_err(1);
		return TC_ACT_SHOT;
	}

	key.tunnel_ttl = 64;
	if (iph->protocol == IPPROTO_ICMP) {
		key.remote_ipv4 = 0xac100164; /* 172.16.1.100 */
	}

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key), 0);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("tc")
int ipip_get_tunnel(struct __sk_buff *skb)
{
	int ret;
	struct bpf_tunnel_key key;

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key), 0);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	bpf_printk("remote ip 0x%x\n", key.remote_ipv4);
	return TC_ACT_OK;
}

SEC("tc")
int ipip6_set_tunnel(struct __sk_buff *skb)
{
	struct bpf_tunnel_key key = {};
	void *data = (void *)(long)skb->data;
	struct iphdr *iph = data;
	void *data_end = (void *)(long)skb->data_end;
	int ret;

	/* single length check */
	if (data + sizeof(*iph) > data_end) {
		log_err(1);
		return TC_ACT_SHOT;
	}

	__builtin_memset(&key, 0x0, sizeof(key));
	key.tunnel_ttl = 64;
	if (iph->protocol == IPPROTO_ICMP) {
		key.remote_ipv6[3] = bpf_htonl(0x11); /* ::11 */
	}

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("tc")
int ipip6_get_tunnel(struct __sk_buff *skb)
{
	int ret;
	struct bpf_tunnel_key key;

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	bpf_printk("remote ip6 %x::%x\n", bpf_htonl(key.remote_ipv6[0]),
		   bpf_htonl(key.remote_ipv6[3]));
	return TC_ACT_OK;
}

SEC("tc")
int ip6ip6_set_tunnel(struct __sk_buff *skb)
{
	struct bpf_tunnel_key key = {};
	void *data = (void *)(long)skb->data;
	struct ipv6hdr *iph = data;
	void *data_end = (void *)(long)skb->data_end;
	int ret;

	/* single length check */
	if (data + sizeof(*iph) > data_end) {
		log_err(1);
		return TC_ACT_SHOT;
	}

	key.tunnel_ttl = 64;
	if (iph->nexthdr == 58 /* NEXTHDR_ICMP */) {
		key.remote_ipv6[3] = bpf_htonl(0x11); /* ::11 */
	}

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("tc")
int ip6ip6_get_tunnel(struct __sk_buff *skb)
{
	int ret;
	struct bpf_tunnel_key key;

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		log_err(ret);
		return TC_ACT_SHOT;
	}

	bpf_printk("remote ip6 %x::%x\n", bpf_htonl(key.remote_ipv6[0]),
		   bpf_htonl(key.remote_ipv6[3]));
	return TC_ACT_OK;
}

SEC("tc")
int xfrm_get_state(struct __sk_buff *skb)
{
	struct bpf_xfrm_state x;
	int ret;

	ret = bpf_skb_get_xfrm_state(skb, 0, &x, sizeof(x), 0);
	if (ret < 0)
		return TC_ACT_OK;

	bpf_printk("reqid %d spi 0x%x remote ip 0x%x\n",
		   x.reqid, bpf_ntohl(x.spi),
		   bpf_ntohl(x.remote_ipv4));
	return TC_ACT_OK;
}

char _license[] SEC("license") = "GPL";
