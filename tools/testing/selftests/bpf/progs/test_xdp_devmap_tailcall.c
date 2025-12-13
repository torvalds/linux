// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

SEC("xdp")
int xdp_devmap(struct xdp_md *ctx)
{
	return ctx->egress_ifindex;
}

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__array(values, int (void *));
} xdp_map SEC(".maps") = {
	.values = {
		[0] = (void *)&xdp_devmap,
	},
};

SEC("xdp")
int xdp_entry(struct xdp_md *ctx)
{
	bpf_tail_call(ctx, &xdp_map, 0);
	return 0;
}
