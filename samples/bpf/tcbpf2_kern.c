/* Copyright (c) 2016 VMware
 * Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#define KBUILD_MODNAME "foo"
#include <uapi/linux/bpf.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/if_packet.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/ipv6.h>
#include <uapi/linux/in.h>
#include <uapi/linux/tcp.h>
#include <uapi/linux/filter.h>
#include <uapi/linux/pkt_cls.h>
#include <net/ipv6.h>
#include "bpf_helpers.h"

#define _htonl __builtin_bswap32
#define ERROR(ret) do {\
		char fmt[] = "ERROR line:%d ret:%d\n";\
		bpf_trace_printk(fmt, sizeof(fmt), __LINE__, ret); \
	} while(0)

struct geneve_opt {
	__be16	opt_class;
	u8	type;
	u8	length:5;
	u8	r3:1;
	u8	r2:1;
	u8	r1:1;
	u8	opt_data[8]; /* hard-coded to 8 byte */
};

struct vxlan_metadata {
	u32     gbp;
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

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key), BPF_F_ZERO_CSUM_TX);
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

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key), BPF_F_ZERO_CSUM_TX);
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
	gopt.opt_class = 0x102; /* Open Virtual Networking (OVN) */
	gopt.type = 0x08;
	gopt.r1 = 0;
	gopt.r2 = 0;
	gopt.r3 = 0;
	gopt.length = 2; /* 4-byte multiple */
	*(int *) &gopt.opt_data = 0xdeadbeef;

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key), BPF_F_ZERO_CSUM_TX);
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

		if (tcp->dest == htons(5200))
			key.remote_ipv4 = 0xac100164; /* 172.16.1.100 */
		else if (tcp->dest == htons(5201))
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

	key.remote_ipv6[0] = _htonl(0x2401db00);
	key.tunnel_ttl = 64;

	if (iph->protocol == IPPROTO_ICMP) {
		key.remote_ipv6[3] = _htonl(1);
	} else {
		if (iph->protocol != IPPROTO_TCP || iph->ihl != 5) {
			ERROR(iph->protocol);
			return TC_ACT_SHOT;
		}

		if (tcp->dest == htons(5200)) {
			key.remote_ipv6[3] = _htonl(1);
		} else if (tcp->dest == htons(5201)) {
			key.remote_ipv6[3] = _htonl(2);
		} else {
			ERROR(tcp->dest);
			return TC_ACT_SHOT;
		}
	}

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key), BPF_F_TUNINFO_IPV6);
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

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key), BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	bpf_trace_printk(fmt, sizeof(fmt), _htonl(key.remote_ipv6[0]),
			 _htonl(key.remote_ipv6[3]));
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

	key.remote_ipv6[0] = _htonl(0x2401db00);
	key.tunnel_ttl = 64;

	if (iph->nexthdr == NEXTHDR_ICMP) {
		key.remote_ipv6[3] = _htonl(1);
	} else {
		if (iph->nexthdr != NEXTHDR_TCP) {
			ERROR(iph->nexthdr);
			return TC_ACT_SHOT;
		}

		if (tcp->dest == htons(5200)) {
			key.remote_ipv6[3] = _htonl(1);
		} else if (tcp->dest == htons(5201)) {
			key.remote_ipv6[3] = _htonl(2);
		} else {
			ERROR(tcp->dest);
			return TC_ACT_SHOT;
		}
	}

	ret = bpf_skb_set_tunnel_key(skb, &key, sizeof(key), BPF_F_TUNINFO_IPV6);
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

	ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key), BPF_F_TUNINFO_IPV6);
	if (ret < 0) {
		ERROR(ret);
		return TC_ACT_SHOT;
	}

	bpf_trace_printk(fmt, sizeof(fmt), _htonl(key.remote_ipv6[0]),
			 _htonl(key.remote_ipv6[3]));
	return TC_ACT_OK;
}


char _license[] SEC("license") = "GPL";
