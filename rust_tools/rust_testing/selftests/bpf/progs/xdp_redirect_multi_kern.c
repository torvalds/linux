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

/* map to store redirect flags for each protocol*/
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, __u16);
	__type(value, __u64);
	__uint(max_entries, 16);
} redirect_flags SEC(".maps");

SEC("xdp")
int xdp_redirect_map_multi_prog(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	int if_index = ctx->ingress_ifindex;
	struct ethhdr *eth = data;
	__u64 *flags_from_map;
	__u16 h_proto;
	__u64 nh_off;
	__u64 flags;

	nh_off = sizeof(*eth);
	if (data + nh_off > data_end)
		return XDP_DROP;

	h_proto = bpf_htons(eth->h_proto);

	flags_from_map = bpf_map_lookup_elem(&redirect_flags, &h_proto);

	/* Default flags for IPv4 : (BPF_F_BROADCAST | BPF_F_EXCLUDE_INGRESS) */
	if (h_proto == ETH_P_IP) {
		flags = flags_from_map ? *flags_from_map : BPF_F_BROADCAST | BPF_F_EXCLUDE_INGRESS;
		return bpf_redirect_map(&map_all, 0, flags);
	}
	/* Default flags for IPv6 : 0 */
	if (h_proto == ETH_P_IPV6) {
		flags = flags_from_map ? *flags_from_map : 0;
		return bpf_redirect_map(&map_all, if_index, flags);
	}
	/* Default flags for others BPF_F_BROADCAST : 0 */
	else {
		flags = flags_from_map ? *flags_from_map : BPF_F_BROADCAST;
		return bpf_redirect_map(&map_all, 0, flags);
	}
}

/* The following 2 progs are for 2nd devmap prog testing */
SEC("xdp")
int xdp_redirect_map_all_prog(struct xdp_md *ctx)
{
	return bpf_redirect_map(&map_egress, 0,
				BPF_F_BROADCAST | BPF_F_EXCLUDE_INGRESS);
}

SEC("xdp/devmap")
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
