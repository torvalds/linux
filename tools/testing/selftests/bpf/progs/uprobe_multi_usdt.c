// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/usdt.bpf.h>

char _license[] SEC("license") = "GPL";

int count;

SEC("usdt")
int usdt0(struct pt_regs *ctx)
{
	count++;
	return 0;
}
