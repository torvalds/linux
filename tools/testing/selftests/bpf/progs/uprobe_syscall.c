// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <string.h>

struct pt_regs regs;

char _license[] SEC("license") = "GPL";

SEC("uretprobe//proc/self/exe:uretprobe_regs_trigger")
int uretprobe(struct pt_regs *ctx)
{
	__builtin_memcpy(&regs, ctx, sizeof(regs));
	return 0;
}
