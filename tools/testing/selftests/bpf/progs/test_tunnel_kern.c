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
#include <linux/types.h>
#include <linux/tcp.h>
#include <linux/socket.h>
#include <linux/pkt_cls.h>
#include <linux/erspan.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define ERROR(ret) do {\
		char fmt[] = "ERROR line:%d ret:%d\n";\
		bpf_trace_printk(fmt, sizeof(fmt), __LINE__, ret); \
	} while (0)

int _version SEC("version") = 1;

struct geneve_opt {
	__be16	opt_class;
	__u8	type;
	__u8	length:5;
	__u8	r3:1;
	__u8	r2:1;
	__u8	r1:1;
	__u8	opt_data[8]; /* hard-coded to 8 byte */
};

struct vxlan_metadata {
	__u32     gbp;
};

SEC("gre_set_tunnel")
int _gre_set_tunnel(struct __sk_buff *skb)
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
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("gre_get_tunnel")
int _gre_get_tunnel(struct __sk_buff *skb)
{
	int ret;
	struct bpf_tunnel_key key;
	char fmt[] = "key %d remote ip 0x%x\n";

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key), 0);
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	bpf_trace_printk(fmt, sizeof(fmt), key.tunnel_id, key.remote_ipv4);
	return TC_ACT_OK;
}

SEC("ip6gretap_set_tunnel")
int _ip6gretap_set_tunnel(struct __sk_buff *skb)
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
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("ip6gretap_get_tunnel")
int _ip6gretap_get_tunnel(struct __sk_buff *skb)
{
	char fmt[] = "key %d remote ip6 ::%x label %x\n";
	struct bpf_tunnel_key key;
	int ret;

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	bpf_trace_printk(fmt, sizeof(fmt),
			 key.tunnel_id, key.remote_ipv6[3], key.tunnel_label);

	return TC_ACT_OK;
}

SEC("erspan_set_tunnel")
int _erspan_set_tunnel(struct __sk_buff *skb)
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
		ERROR(ret);
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
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("erspan_get_tunnel")
int _erspan_get_tunnel(struct __sk_buff *skb)
{
	char fmt[] = "key %d remote ip 0x%x erspan version %d\n";
	struct bpf_tunnel_key key;
	struct erspan_metadata md;
	__u32 index;
	int ret;

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key), 0);
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	ret = bpf_skb_get_tunnel_opt(skb, &md, sizeof(md));
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	bpf_trace_printk(fmt, sizeof(fmt),
			key.tunnel_id, key.remote_ipv4, md.version);

#ifdef ERSPAN_V1
	char fmt2[] = "\tindex %x\n";

	index = bpf_ntohl(md.u.index);
	bpf_trace_printk(fmt2, sizeof(fmt2), index);
#else
	char fmt2[] = "\tdirection %d hwid %x timestamp %u\n";

	bpf_trace_printk(fmt2, sizeof(fmt2),
			 md.u.md2.dir,
			 (md.u.md2.hwid_upper << 4) + md.u.md2.hwid,
			 bpf_ntohl(md.u.md2.timestamp));
#endif

	return TC_ACT_OK;
}

SEC("ip4ip6erspan_set_tunnel")
int _ip4ip6erspan_set_tunnel(struct __sk_buff *skb)
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
		ERROR(ret);
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
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("ip4ip6erspan_get_tunnel")
int _ip4ip6erspan_get_tunnel(struct __sk_buff *skb)
{
	char fmt[] = "ip6erspan get key %d remote ip6 ::%x erspan version %d\n";
	struct bpf_tunnel_key key;
	struct erspan_metadata md;
	__u32 index;
	int ret;

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	ret = bpf_skb_get_tunnel_opt(skb, &md, sizeof(md));
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	bpf_trace_printk(fmt, sizeof(fmt),
			key.tunnel_id, key.remote_ipv4, md.version);

#ifdef ERSPAN_V1
	char fmt2[] = "\tindex %x\n";

	index = bpf_ntohl(md.u.index);
	bpf_trace_printk(fmt2, sizeof(fmt2), index);
#else
	char fmt2[] = "\tdirection %d hwid %x timestamp %u\n";

	bpf_trace_printk(fmt2, sizeof(fmt2),
			 md.u.md2.dir,
			 (md.u.md2.hwid_upper << 4) + md.u.md2.hwid,
			 bpf_ntohl(md.u.md2.timestamp));
#endif

	return TC_ACT_OK;
}

SEC("vxlan_set_tunnel")
int _vxlan_set_tunnel(struct __sk_buff *skb)
{
	int ret;
	struct bpf_tunnel_key key;
	struct vxlan_metadata md;

	__builtin_memset(&key, 0x0, sizeof(key));
	key.remote_ipv4 = 0xac100164; /* 172.16.1.100 */
	key.tunnel_id = 2;
	key.tunnel_tos = 0;
	key.tunnel_ttl = 64;

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_ZERO_CSUM_TX);
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	md.gbp = 0x800FF; /* Set VXLAN Group Policy extension */
	ret = bpf_skb_set_tunnel_opt(skb, &md, sizeof(md));
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("vxlan_get_tunnel")
int _vxlan_get_tunnel(struct __sk_buff *skb)
{
	int ret;
	struct bpf_tunnel_key key;
	struct vxlan_metadata md;
	char fmt[] = "key %d remote ip 0x%x vxlan gbp 0x%x\n";

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key), 0);
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	ret = bpf_skb_get_tunnel_opt(skb, &md, sizeof(md));
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	bpf_trace_printk(fmt, sizeof(fmt),
			key.tunnel_id, key.remote_ipv4, md.gbp);

	return TC_ACT_OK;
}

SEC("ip6vxlan_set_tunnel")
int _ip6vxlan_set_tunnel(struct __sk_buff *skb)
{
	struct bpf_tunnel_key key;
	int ret;

	__builtin_memset(&key, 0x0, sizeof(key));
	key.remote_ipv6[3] = bpf_htonl(0x11); /* ::11 */
	key.tunnel_id = 22;
	key.tunnel_tos = 0;
	key.tunnel_ttl = 64;

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("ip6vxlan_get_tunnel")
int _ip6vxlan_get_tunnel(struct __sk_buff *skb)
{
	char fmt[] = "key %d remote ip6 ::%x label %x\n";
	struct bpf_tunnel_key key;
	int ret;

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	bpf_trace_printk(fmt, sizeof(fmt),
			 key.tunnel_id, key.remote_ipv6[3], key.tunnel_label);

	return TC_ACT_OK;
}

SEC("geneve_set_tunnel")
int _geneve_set_tunnel(struct __sk_buff *skb)
{
	int ret, ret2;
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
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	ret = bpf_skb_set_tunnel_opt(skb, &gopt, sizeof(gopt));
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("geneve_get_tunnel")
int _geneve_get_tunnel(struct __sk_buff *skb)
{
	int ret;
	struct bpf_tunnel_key key;
	struct geneve_opt gopt;
	char fmt[] = "key %d remote ip 0x%x geneve class 0x%x\n";

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key), 0);
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	ret = bpf_skb_get_tunnel_opt(skb, &gopt, sizeof(gopt));
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	bpf_trace_printk(fmt, sizeof(fmt),
			key.tunnel_id, key.remote_ipv4, gopt.opt_class);
	return TC_ACT_OK;
}

SEC("ip6geneve_set_tunnel")
int _ip6geneve_set_tunnel(struct __sk_buff *skb)
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
		ERROR(ret);
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
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("ip6geneve_get_tunnel")
int _ip6geneve_get_tunnel(struct __sk_buff *skb)
{
	char fmt[] = "key %d remote ip 0x%x geneve class 0x%x\n";
	struct bpf_tunnel_key key;
	struct geneve_opt gopt;
	int ret;

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	ret = bpf_skb_get_tunnel_opt(skb, &gopt, sizeof(gopt));
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	bpf_trace_printk(fmt, sizeof(fmt),
			key.tunnel_id, key.remote_ipv4, gopt.opt_class);

	return TC_ACT_OK;
}

SEC("ipip_set_tunnel")
int _ipip_set_tunnel(struct __sk_buff *skb)
{
	struct bpf_tunnel_key key = {};
	void *data = (void *)(long)skb->data;
	struct iphdr *iph = data;
	struct tcphdr *tcp = data + sizeof(*iph);
	void *data_end = (void *)(long)skb->data_end;
	int ret;

	/* single length check */
	if (data + sizeof(*iph) + sizeof(*tcp) > data_end) {
		ERROR(1);
		return TC_ACT_SHOT;
	}

	key.tunnel_ttl = 64;
	if (iph->protocol == IPPROTO_ICMP) {
		key.remote_ipv4 = 0xac100164; /* 172.16.1.100 */
	} else {
		if (iph->protocol != IPPROTO_TCP || iph->ihl != 5)
			return TC_ACT_SHOT;

		if (tcp->dest == bpf_htons(5200))
			key.remote_ipv4 = 0xac100164; /* 172.16.1.100 */
		else if (tcp->dest == bpf_htons(5201))
			key.remote_ipv4 = 0xac100165; /* 172.16.1.101 */
		else
			return TC_ACT_SHOT;
	}

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key), 0);
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("ipip_get_tunnel")
int _ipip_get_tunnel(struct __sk_buff *skb)
{
	int ret;
	struct bpf_tunnel_key key;
	char fmt[] = "remote ip 0x%x\n";

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key), 0);
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	bpf_trace_printk(fmt, sizeof(fmt), key.remote_ipv4);
	return TC_ACT_OK;
}

SEC("ipip6_set_tunnel")
int _ipip6_set_tunnel(struct __sk_buff *skb)
{
	struct bpf_tunnel_key key = {};
	void *data = (void *)(long)skb->data;
	struct iphdr *iph = data;
	struct tcphdr *tcp = data + sizeof(*iph);
	void *data_end = (void *)(long)skb->data_end;
	int ret;

	/* single length check */
	if (data + sizeof(*iph) + sizeof(*tcp) > data_end) {
		ERROR(1);
		return TC_ACT_SHOT;
	}

	__builtin_memset(&key, 0x0, sizeof(key));
	key.remote_ipv6[3] = bpf_htonl(0x11); /* ::11 */
	key.tunnel_ttl = 64;

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("ipip6_get_tunnel")
int _ipip6_get_tunnel(struct __sk_buff *skb)
{
	int ret;
	struct bpf_tunnel_key key;
	char fmt[] = "remote ip6 %x::%x\n";

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	bpf_trace_printk(fmt, sizeof(fmt), bpf_htonl(key.remote_ipv6[0]),
			 bpf_htonl(key.remote_ipv6[3]));
	return TC_ACT_OK;
}

SEC("ip6ip6_set_tunnel")
int _ip6ip6_set_tunnel(struct __sk_buff *skb)
{
	struct bpf_tunnel_key key = {};
	void *data = (void *)(long)skb->data;
	struct ipv6hdr *iph = data;
	struct tcphdr *tcp = data + sizeof(*iph);
	void *data_end = (void *)(long)skb->data_end;
	int ret;

	/* single length check */
	if (data + sizeof(*iph) + sizeof(*tcp) > data_end) {
		ERROR(1);
		return TC_ACT_SHOT;
	}

	key.remote_ipv6[0] = bpf_htonl(0x2401db00);
	key.tunnel_ttl = 64;

	if (iph->nexthdr == 58 /* NEXTHDR_ICMP */) {
		key.remote_ipv6[3] = bpf_htonl(1);
	} else {
		if (iph->nexthdr != 6 /* NEXTHDR_TCP */) {
			ERROR(iph->nexthdr);
			return TC_ACT_SHOT;
		}

		if (tcp->dest == bpf_htons(5200)) {
			key.remote_ipv6[3] = bpf_htonl(1);
		} else if (tcp->dest == bpf_htons(5201)) {
			key.remote_ipv6[3] = bpf_htonl(2);
		} else {
			ERROR(tcp->dest);
			return TC_ACT_SHOT;
		}
	}

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

SEC("ip6ip6_get_tunnel")
int _ip6ip6_get_tunnel(struct __sk_buff *skb)
{
	int ret;
	struct bpf_tunnel_key key;
	char fmt[] = "remote ip6 %x::%x\n";

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key),
				     BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	bpf_trace_printk(fmt, sizeof(fmt), bpf_htonl(key.remote_ipv6[0]),
			 bpf_htonl(key.remote_ipv6[3]));
	return TC_ACT_OK;
}

SEC("xfrm_get_state")
int _xfrm_get_state(struct __sk_buff *skb)
{
	struct bpf_xfrm_state x;
	char fmt[] = "reqid %d spi 0x%x remote ip 0x%x\n";
	int ret;

	ret = bpf_skb_get_xfrm_state(skb, 0, &x, sizeof(x), 0);
	if (ret < 0)
		return TC_ACT_OK;

	bpf_trace_printk(fmt, sizeof(fmt), x.reqid, bpf_ntohl(x.spi),
			 bpf_ntohl(x.remote_ipv4));
	return TC_ACT_OK;
}

char _license[] SEC("license") = "GPL";
