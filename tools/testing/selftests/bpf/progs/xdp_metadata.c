// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include "xdp_metadata.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

struct {
	__uint(type, BPF_MAP_TYPE_XSKMAP);
	__uint(max_entries, 4);
	__type(key, __u32);
	__type(value, __u32);
} xsk SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} prog_arr SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_DEVMAP);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(struct bpf_devmap_val));
	__uint(max_entries, 1);
} dev_map SEC(".maps");

extern int bpf_xdp_metadata_rx_timestamp(const struct xdp_md *ctx,
					 __u64 *timestamp) __ksym;
extern int bpf_xdp_metadata_rx_hash(const struct xdp_md *ctx, __u32 *hash,
				    enum xdp_rss_hash_type *rss_type) __ksym;
extern int bpf_xdp_metadata_rx_vlan_tag(const struct xdp_md *ctx,
					__be16 *vlan_proto,
					__u16 *vlan_tci) __ksym;

SEC("xdp")
int rx(struct xdp_md *ctx)
{
	void *data, *data_meta, *data_end;
	struct ipv6hdr *ip6h = NULL;
	struct ethhdr *eth = NULL;
	struct udphdr *udp = NULL;
	struct iphdr *iph = NULL;
	struct xdp_meta *meta;
	u64 timestamp = -1;
	int ret;

	data = (void *)(long)ctx->data;
	data_end = (void *)(long)ctx->data_end;
	eth = data;
	if (eth + 1 < data_end) {
		if (eth->h_proto == bpf_htons(ETH_P_IP)) {
			iph = (void *)(eth + 1);
			if (iph + 1 < data_end && iph->protocol == IPPROTO_UDP)
				udp = (void *)(iph + 1);
		}
		if (eth->h_proto == bpf_htons(ETH_P_IPV6)) {
			ip6h = (void *)(eth + 1);
			if (ip6h + 1 < data_end && ip6h->nexthdr == IPPROTO_UDP)
				udp = (void *)(ip6h + 1);
		}
		if (udp && udp + 1 > data_end)
			udp = NULL;
	}

	if (!udp)
		return XDP_PASS;

	/* Forwarding UDP:8080 to AF_XDP */
	if (udp->dest != bpf_htons(8080))
		return XDP_PASS;

	/* Reserve enough for all custom metadata. */

	ret = bpf_xdp_adjust_meta(ctx, -(int)sizeof(struct xdp_meta));
	if (ret != 0)
		return XDP_DROP;

	data = (void *)(long)ctx->data;
	data_meta = (void *)(long)ctx->data_meta;

	if (data_meta + sizeof(struct xdp_meta) > data)
		return XDP_DROP;

	meta = data_meta;

	/* Export metadata. */

	/* We expect veth bpf_xdp_metadata_rx_timestamp to return 0 HW
	 * timestamp, so put some non-zero value into AF_XDP frame for
	 * the userspace.
	 */
	bpf_xdp_metadata_rx_timestamp(ctx, &timestamp);
	if (timestamp == 0)
		meta->rx_timestamp = 1;

	bpf_xdp_metadata_rx_hash(ctx, &meta->rx_hash, &meta->rx_hash_type);
	bpf_xdp_metadata_rx_vlan_tag(ctx, &meta->rx_vlan_proto,
				     &meta->rx_vlan_tci);

	return bpf_redirect_map(&xsk, ctx->rx_queue_index, XDP_PASS);
}

SEC("xdp")
int redirect(struct xdp_md *ctx)
{
	return bpf_redirect_map(&dev_map, ctx->rx_queue_index, XDP_PASS);
}

char _license[] SEC("license") = "GPL";
