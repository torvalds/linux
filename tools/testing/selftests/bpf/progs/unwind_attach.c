// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "../../../include/linux/filter.h"
#include "bpf_misc.h"

/* The targets are global_unwinds() and global_never_unwinds() in verifier_unwind.c */

#define unwind_exit ".8byte %[insn_unwind_exit];"
#define __imm_unwind_exit \
	__imm_insn(insn_unwind_exit, \
		   BPF_RAW_INSN(BPF_JMP | BPF_EXIT, 0, BPF_UNWIND_EXIT, 0, 0))

extern void bpf_throw(__u64 cookie) __ksym;

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
		unwind_exit
		:
		: __imm_unwind_exit
	);
}

/* Not naked: the argument has to have a name in BTF */
__noinline int throws_cb(__u64 cookie)
{
	if (cookie)
		asm volatile ("r0 = 3;" unwind_exit :: __imm_unwind_exit : "r0");
	return 0;
}

/* It has no unwind_exit, so it can replace a subprog that cannot unwind */
SEC("?freplace/global_never_unwinds")
__attribute__((btf_decl_tag("exception_callback:throws_cb")))
unsigned long freplace_throws(void)
{
	bpf_throw(1);
	return 0;
}

char _license[] SEC("license") = "GPL";
