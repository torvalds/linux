// SPDX-License-Identifier: GPL-2.0

#include <stddef.h>
#include <linux/bpf.h>
#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

enum {
	XDP_MODE = 0,
	XDP_PORT = 1,
} xdp_map_setup_keys;

enum {
	XDP_MODE_PASS = 0,
	XDP_MODE_DROP = 1,
} xdp_map_modes;

enum {
	STATS_RX = 0,
	STATS_PASS = 1,
	STATS_DROP = 2,
} xdp_stats;

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 2);
	__type(key, __u32);
	__type(value, __s32);
} map_xdp_setup SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 4);
	__type(key, __u32);
	__type(value, __u64);
} map_xdp_stats SEC(".maps");

static void record_stats(struct xdp_md *ctx, __u32 stat_type)
{
	__u64 *count;

	count = bpf_map_lookup_elem(&map_xdp_stats, &stat_type);

	if (count)
		__sync_fetch_and_add(count, 1);
}

static struct udphdr *filter_udphdr(struct xdp_md *ctx, __u16 port)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct udphdr *udph = NULL;
	struct ethhdr *eth = data;

	if (data + sizeof(*eth) > data_end)
		return NULL;

	if (eth->h_proto == bpf_htons(ETH_P_IP)) {
		struct iphdr *iph = data + sizeof(*eth);

		if (iph + 1 > (struct iphdr *)data_end ||
		    iph->protocol != IPPROTO_UDP)
			return NULL;

		udph = (void *)eth + sizeof(*iph) + sizeof(*eth);
	} else if (eth->h_proto  == bpf_htons(ETH_P_IPV6)) {
		struct ipv6hdr *ipv6h = data + sizeof(*eth);

		if (ipv6h + 1 > (struct ipv6hdr *)data_end ||
		    ipv6h->nexthdr != IPPROTO_UDP)
			return NULL;

		udph = (void *)eth + sizeof(*ipv6h) + sizeof(*eth);
	} else {
		return NULL;
	}

	if (udph + 1 > (struct udphdr *)data_end)
		return NULL;

	if (udph->dest != bpf_htons(port))
		return NULL;

	record_stats(ctx, STATS_RX);

	return udph;
}

static int xdp_mode_pass(struct xdp_md *ctx, __u16 port)
{
	struct udphdr *udph = NULL;

	udph = filter_udphdr(ctx, port);
	if (!udph)
		return XDP_PASS;

	record_stats(ctx, STATS_PASS);

	return XDP_PASS;
}

static int xdp_mode_drop_handler(struct xdp_md *ctx, __u16 port)
{
	struct udphdr *udph = NULL;

	udph = filter_udphdr(ctx, port);
	if (!udph)
		return XDP_PASS;

	record_stats(ctx, STATS_DROP);

	return XDP_DROP;
}

static int xdp_prog_common(struct xdp_md *ctx)
{
	__u32 key, *port;
	__s32 *mode;

	key = XDP_MODE;
	mode = bpf_map_lookup_elem(&map_xdp_setup, &key);
	if (!mode)
		return XDP_PASS;

	key = XDP_PORT;
	port = bpf_map_lookup_elem(&map_xdp_setup, &key);
	if (!port)
		return XDP_PASS;

	switch (*mode) {
	case XDP_MODE_PASS:
		return xdp_mode_pass(ctx, (__u16)(*port));
	case XDP_MODE_DROP:
		return xdp_mode_drop_handler(ctx, (__u16)(*port));
	}

	/* Default action is to simple pass */
	return XDP_PASS;
}

SEC("xdp")
int xdp_prog(struct xdp_md *ctx)
{
	return xdp_prog_common(ctx);
}

SEC("xdp.frags")
int xdp_prog_frags(struct xdp_md *ctx)
{
	return xdp_prog_common(ctx);
}

char _license[] SEC("license") = "GPL";
