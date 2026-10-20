// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "../../../include/linux/filter.h"
#include "bpf_misc.h"

/* The targets are global_unwinds() and global_never_unwinds() in verifier_unwind.c */

int fentry_cnt;

SEC("?fentry/global_unwinds")
int BPF_PROG(fentry_unwind)
{
	fentry_cnt++;
	return 0;
}

SEC("?fexit/global_unwinds")
int BPF_PROG(fexit_unwind)
{
	return 0;
}

SEC("?freplace/global_unwinds")
unsigned long freplace_returns(void)
{
	return 9;
}

SEC("?freplace/global_unwinds")
__naked unsigned long freplace_unwinds(void)
{
	asm volatile (
		"r0 = 3;"
		".8byte %[unwind_exit];"
		:
		: __imm_insn(unwind_exit,
			     BPF_RAW_INSN(BPF_JMP | BPF_EXIT, 0, BPF_UNWIND_EXIT, 0, 0))
	);
}

char _license[] SEC("license") = "GPL";
