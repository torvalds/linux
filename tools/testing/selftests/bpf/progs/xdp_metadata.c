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

extern int bpf_xdp_metadata_rx_timestamp(const struct xdp_md *ctx,
					 __u64 *timestamp) __ksym;
extern int bpf_xdp_metadata_rx_hash(const struct xdp_md *ctx, __u32 *hash,
				    enum xdp_rss_hash_type *rss_type) __ksym;

SEC("xdp")
int rx(struct xdp_md *ctx)
{
	void *data, *data_meta;
	struct xdp_meta *meta;
	u64 timestamp = -1;
	int ret;

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

	return bpf_redirect_map(&xsk, ctx->rx_queue_index, XDP_PASS);
}

char _license[] SEC("license") = "GPL";
