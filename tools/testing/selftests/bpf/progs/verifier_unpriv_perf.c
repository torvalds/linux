// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/unpriv.c */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("perf_event")
__description("unpriv: spill/fill of different pointers ldx")
__failure __msg("same insn cannot be used with different pointers")
__naked void fill_of_different_pointers_ldx(void)
{
	asm volatile ("					\
	r6 = r10;					\
	r6 += -8;					\
	if r1 == 0 goto l0_%=;				\
	r2 = r10;					\
	r2 += %[__imm_0];				\
	*(u64*)(r6 + 0) = r2;				\
l0_%=:	if r1 != 0 goto l1_%=;				\
	*(u64*)(r6 + 0) = r1;				\
l1_%=:	r1 = *(u64*)(r6 + 0);				\
	r1 = *(u64*)(r1 + %[sample_period]);		\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__imm_0,
		      -(__s32) offsetof(struct bpf_perf_event_data, sample_period) - 8),
	  __imm_const(sample_period,
		      offsetof(struct bpf_perf_event_data, sample_period))
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
