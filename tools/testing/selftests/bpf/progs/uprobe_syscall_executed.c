// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <string.h>

struct pt_regs regs;

char _license[] SEC("license") = "GPL";

int executed = 0;
int pid;

SEC("uretprobe.multi")
int test_uretprobe_multi(struct pt_regs *ctx)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	executed++;
	return 0;
}
