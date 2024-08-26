// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <string.h>

struct pt_regs regs;

char _license[] SEC("license") = "GPL";

int executed = 0;

SEC("uretprobe.multi")
int test(struct pt_regs *regs)
{
	executed = 1;
	return 0;
}
