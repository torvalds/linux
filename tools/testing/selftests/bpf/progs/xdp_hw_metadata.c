// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include "xdp_metadata.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

struct {
	__uint(type, BPF_MAP_TYPE_XSKMAP);
	__uint(max_entries, 256);
	__type(key, __u32);
	__type(value, __u32);
} xsk SEC(".maps");

extern int bpf_xdp_metadata_rx_timestamp(const struct xdp_md *ctx,
					 __u64 *timestamp) __ksym;
extern int bpf_xdp_metadata_rx_hash(const struct xdp_md *ctx,
				    __u32 *hash) __ksym;

SEC("xdp")
int rx(struct xdp_md *ctx)
{
	void *data, *data_meta, *data_end;
	struct ipv6hdr *ip6h = NULL;
	struct ethhdr *eth = NULL;
	struct udphdr *udp = NULL;
	struct iphdr *iph = NULL;
	struct xdp_meta *meta;
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

	if (udp->dest != bpf_htons(9091))
		return XDP_PASS;

	bpf_printk("forwarding UDP:9091 to AF_XDP");

	ret = bpf_xdp_adjust_meta(ctx, -(int)sizeof(struct xdp_meta));
	if (ret != 0) {
		bpf_printk("bpf_xdp_adjust_meta returned %d", ret);
		return XDP_PASS;
	}

	data = (void *)(long)ctx->data;
	data_meta = (void *)(long)ctx->data_meta;
	meta = data_meta;

	if (meta + 1 > data) {
		bpf_printk("bpf_xdp_adjust_meta doesn't appear to work");
		return XDP_PASS;
	}

	if (!bpf_xdp_metadata_rx_timestamp(ctx, &meta->rx_timestamp))
		bpf_printk("populated rx_timestamp with %llu", meta->rx_timestamp);
	else
		meta->rx_timestamp = 0; /* Used by AF_XDP as not avail signal */

	if (!bpf_xdp_metadata_rx_hash(ctx, &meta->rx_hash))
		bpf_printk("populated rx_hash with %u", meta->rx_hash);
	else
		meta->rx_hash = 0; /* Used by AF_XDP as not avail signal */

	return bpf_redirect_map(&xsk, ctx->rx_queue_index, XDP_PASS);
}

char _license[] SEC("license") = "GPL";
