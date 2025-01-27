// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/usdt.bpf.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";


SEC("uprobe.session")
__success
int uprobe_sesison_return_0(struct pt_regs *ctx)
{
	return 0;
}

SEC("uprobe.session")
__success
int uprobe_sesison_return_1(struct pt_regs *ctx)
{
	return 1;
}

SEC("uprobe.session")
__failure
__msg("At program exit the register R0 has smin=2 smax=2 should have been in [0, 1]")
int uprobe_sesison_return_2(struct pt_regs *ctx)
{
	return 2;
}
