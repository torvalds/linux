// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

int pid = 0;

SEC("kprobe.multi")
int test_override(struct pt_regs *ctx)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	bpf_override_return(ctx, 123);
	return 0;
}

SEC("kprobe")
int test_kprobe_override(struct pt_regs *ctx)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	bpf_override_return(ctx, 123);
	return 0;
}
