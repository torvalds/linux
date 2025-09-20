// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

#define MAX_STACK_RAWTP 10

SEC("raw_tracepoint/sys_enter")
int bpf_prog2(void *ctx)
{
	__u64 stack[MAX_STACK_RAWTP];
	int error;

	/* set all the flags which should return -EINVAL */
	error = bpf_get_stack(ctx, stack, 0, -1);
	if (error < 0)
		goto loop;

	return error;
loop:
	while (1) {
		error++;
	}
}

char _license[] SEC("license") = "GPL";
