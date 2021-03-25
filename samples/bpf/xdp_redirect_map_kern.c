/* Copyright (c) 2017 Covalent IO, Inc. http://covalent.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
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

/* The 2nd xdp prog on egress does not support skb mode, so we define two
 * maps, tx_port_general and tx_port_native.
 */
struct {
	__uint(type, BPF_MAP_TYPE_DEVMAP);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
	__uint(max_entries, 100);
} tx_port_general SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_DEVMAP);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(struct bpf_devmap_val));
	__uint(max_entries, 100);
} tx_port_native SEC(".maps");

/* Count RX packets, as XDP bpf_prog doesn't get direct TX-success
 * feedback.  Redirect TX errors can be caught via a tracepoint.
 */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__type(key, u32);
	__type(value, long);
	__uint(max_entries, 1);
} rxcnt SEC(".maps");

/* map to store egress interface mac address */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, u32);
	__type(value, __be64);
	__uint(max_entries, 1);
} tx_mac SEC(".maps");

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

static __always_inline int xdp_redirect_map(struct xdp_md *ctx, void *redirect_map)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	int rc = XDP_DROP;
	long *value;
	u32 key = 0;
	u64 nh_off;
	int vport;

	nh_off = sizeof(*eth);
	if (data + nh_off > data_end)
		return rc;

	/* constant virtual port */
	vport = 0;

	/* count packet in global counter */
	value = bpf_map_lookup_elem(&rxcnt, &key);
	if (value)
		*value += 1;

	swap_src_dst_mac(data);

	/* send packet out physical port */
	return bpf_redirect_map(redirect_map, vport, 0);
}

SEC("xdp_redirect_general")
int xdp_redirect_map_general(struct xdp_md *ctx)
{
	return xdp_redirect_map(ctx, &tx_port_general);
}

SEC("xdp_redirect_native")
int xdp_redirect_map_native(struct xdp_md *ctx)
{
	return xdp_redirect_map(ctx, &tx_port_native);
}

SEC("xdp_devmap/map_prog")
int xdp_redirect_map_egress(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	__be64 *mac;
	u32 key = 0;
	u64 nh_off;

	nh_off = sizeof(*eth);
	if (data + nh_off > data_end)
		return XDP_DROP;

	mac = bpf_map_lookup_elem(&tx_mac, &key);
	if (mac)
		__builtin_memcpy(eth->h_source, mac, ETH_ALEN);

	return XDP_PASS;
}

/* Redirect require an XDP bpf_prog loaded on the TX device */
SEC("xdp_redirect_dummy")
int xdp_redirect_dummy_prog(struct xdp_md *ctx)
{
	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
