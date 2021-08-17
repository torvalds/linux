// SPDX-License-Identifier: GPL-2.0
#define KBUILD_MODNAME "foo"
#include <uapi/linux/bpf.h>
#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_DEVMAP_HASH);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
	__uint(max_entries, 32);
} forward_map_general SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_DEVMAP_HASH);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(struct bpf_devmap_val));
	__uint(max_entries, 32);
} forward_map_native SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__type(key, u32);
	__type(value, long);
	__uint(max_entries, 1);
} rxcnt SEC(".maps");

/* map to store egress interfaces mac addresses, set the
 * max_entries to 1 and extend it in user sapce prog.
 */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, u32);
	__type(value, __be64);
	__uint(max_entries, 1);
} mac_map SEC(".maps");

static int xdp_redirect_map(struct xdp_md *ctx, void *forward_map)
{
	long *value;
	u32 key = 0;

	/* count packet in global counter */
	value = bpf_map_lookup_elem(&rxcnt, &key);
	if (value)
		*value += 1;

	return bpf_redirect_map(forward_map, key,
				BPF_F_BROADCAST | BPF_F_EXCLUDE_INGRESS);
}

SEC("xdp_redirect_general")
int xdp_redirect_map_general(struct xdp_md *ctx)
{
	return xdp_redirect_map(ctx, &forward_map_general);
}

SEC("xdp_redirect_native")
int xdp_redirect_map_native(struct xdp_md *ctx)
{
	return xdp_redirect_map(ctx, &forward_map_native);
}

SEC("xdp_devmap/map_prog")
int xdp_devmap_prog(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	u32 key = ctx->egress_ifindex;
	struct ethhdr *eth = data;
	__be64 *mac;
	u64 nh_off;

	nh_off = sizeof(*eth);
	if (data + nh_off > data_end)
		return XDP_DROP;

	mac = bpf_map_lookup_elem(&mac_map, &key);
	if (mac)
		__builtin_memcpy(eth->h_source, mac, ETH_ALEN);

	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
