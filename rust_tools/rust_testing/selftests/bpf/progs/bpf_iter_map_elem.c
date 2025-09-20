// SPDX-License-Identifier: GPL-2.0-only

#include "vmlinux.h"
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

__u32 value_sum = 0;

SEC("iter/bpf_map_elem")
int dump_bpf_map_values(struct bpf_iter__bpf_map_elem *ctx)
{
	__u32 value = 0;

	if (ctx->value == (void *)0)
		return 0;

	bpf_probe_read_kernel(&value, sizeof(value), ctx->value);
	value_sum += value;
	return 0;
}
