// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "../../../include/linux/filter.h"
#include "bpf_misc.h"

/* r0-r5 after the loop and the number of iterations */
__u64 regs_64[7];
__u64 regs_128[7];

/*
 * fentry prog with 64 or more bytes of stack uses private stack.
 * Loop until may_goto expires. r0-r5 should stay zero.
 */
#define TIMED_MAY_GOTO_PRIV_STACK(size, func)				\
SEC("fentry/" #func)							\
__naked void priv_stack_##size(void)					\
{									\
	asm volatile (							\
	"r6 = 0;"							\
	"*(u64 *)(r10 - " #size ") = r6;"				\
	"r7 = %[regs] ll;"						\
	"r0 = 0;"							\
	"r1 = 0;"							\
	"r2 = 0;"							\
	"r3 = 0;"							\
	"r4 = 0;"							\
	"r5 = 0;"							\
"1:"									\
	".8byte %[may_goto];"						\
	"r6 += 1;"							\
	"goto 1b;"							\
	"*(u64 *)(r7 + 0) = r0;"					\
	"*(u64 *)(r7 + 8) = r1;"					\
	"*(u64 *)(r7 + 16) = r2;"					\
	"*(u64 *)(r7 + 24) = r3;"					\
	"*(u64 *)(r7 + 32) = r4;"					\
	"*(u64 *)(r7 + 40) = r5;"					\
	"*(u64 *)(r7 + 48) = r6;"					\
	"r0 = 0;"							\
	"exit;"								\
	:								\
	: [regs]"i"(&regs_##size),					\
	  __imm_insn(may_goto, BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, 0, 2, 0)) \
	: __clobber_all);						\
}

TIMED_MAY_GOTO_PRIV_STACK(64, bpf_fentry_test1)
TIMED_MAY_GOTO_PRIV_STACK(128, bpf_fentry_test2)

char _license[] SEC("license") = "GPL";
