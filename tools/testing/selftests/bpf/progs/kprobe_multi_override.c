// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

SEC("kprobe.multi")
int test_override(struct pt_regs *ctx)
{
	bpf_override_return(ctx, 123);
	return 0;
}
