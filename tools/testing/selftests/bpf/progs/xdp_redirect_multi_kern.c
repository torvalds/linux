// SPDX-License-Identifier: GPL-2.0
#define KBUILD_MODNAME "foo"
#include <string.h>
#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/ip.h>
#include <linux/ipv6.h>

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

/* One map use devmap, another one use devmap_hash for testing */
struct {
	__uint(type, BPF_MAP_TYPE_DEVMAP);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
	__uint(max_entries, 1024);
} map_all SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_DEVMAP_HASH);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(struct bpf_devmap_val));
	__uint(max_entries, 128);
} map_egress SEC(".maps");

/* map to store egress interfaces mac addresses */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, __u32);
	__type(value, __be64);
	__uint(max_entries, 128);
} mac_map SEC(".maps");

SEC("xdp_redirect_map_multi")
int xdp_redirect_map_multi_prog(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	int if_index = ctx->ingress_ifindex;
	struct ethhdr *eth = data;
	__u16 h_proto;
	__u64 nh_off;

	nh_off = sizeof(*eth);
	if (data + nh_off > data_end)
		return XDP_DROP;

	h_proto = eth->h_proto;

	/* Using IPv4 for (BPF_F_BROADCAST | BPF_F_EXCLUDE_INGRESS) testing */
	if (h_proto == bpf_htons(ETH_P_IP))
		return bpf_redirect_map(&map_all, 0,
					BPF_F_BROADCAST | BPF_F_EXCLUDE_INGRESS);
	/* Using IPv6 for none flag testing */
	else if (h_proto == bpf_htons(ETH_P_IPV6))
		return bpf_redirect_map(&map_all, if_index, 0);
	/* All others for BPF_F_BROADCAST testing */
	else
		return bpf_redirect_map(&map_all, 0, BPF_F_BROADCAST);
}

/* The following 2 progs are for 2nd devmap prog testing */
SEC("xdp_redirect_map_ingress")
int xdp_redirect_map_all_prog(struct xdp_md *ctx)
{
	return bpf_redirect_map(&map_egress, 0,
				BPF_F_BROADCAST | BPF_F_EXCLUDE_INGRESS);
}

SEC("xdp_devmap/map_prog")
int xdp_devmap_prog(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	__u32 key = ctx->egress_ifindex;
	struct ethhdr *eth = data;
	__u64 nh_off;
	__be64 *mac;

	nh_off = sizeof(*eth);
	if (data + nh_off > data_end)
		return XDP_DROP;

	mac = bpf_map_lookup_elem(&mac_map, &key);
	if (mac)
		__builtin_memcpy(eth->h_source, mac, ETH_ALEN);

	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
