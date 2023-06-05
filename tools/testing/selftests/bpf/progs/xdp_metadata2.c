// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include "xdp_metadata.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

extern int bpf_xdp_metadata_rx_hash(const struct xdp_md *ctx, __u32 *hash,
				    enum xdp_rss_hash_type *rss_type) __ksym;

int called;

SEC("freplace/rx")
int freplace_rx(struct xdp_md *ctx)
{
	enum xdp_rss_hash_type type = 0;
	u32 hash = 0;
	/* Call _any_ metadata function to make sure we don't crash. */
	bpf_xdp_metadata_rx_hash(ctx, &hash, &type);
	called++;
	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
