// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/const_or.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("tracepoint")
__description("constant register |= constant should keep constant type")
__success
__naked void constant_should_keep_constant_type(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -48;					\
	r2 = 34;					\
	r2 |= 13;					\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
	exit;						\
"	:
	: __imm(bpf_probe_read_kernel)
	: __clobber_all);
}

SEC("tracepoint")
__description("constant register |= constant should not bypass stack boundary checks")
__failure __msg("invalid indirect access to stack R1 off=-48 size=58")
__naked void not_bypass_stack_boundary_checks_1(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -48;					\
	r2 = 34;					\
	r2 |= 24;					\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
	exit;						\
"	:
	: __imm(bpf_probe_read_kernel)
	: __clobber_all);
}

SEC("tracepoint")
__description("constant register |= constant register should keep constant type")
__success
__naked void register_should_keep_constant_type(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -48;					\
	r2 = 34;					\
	r4 = 13;					\
	r2 |= r4;					\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
	exit;						\
"	:
	: __imm(bpf_probe_read_kernel)
	: __clobber_all);
}

SEC("tracepoint")
__description("constant register |= constant register should not bypass stack boundary checks")
__failure __msg("invalid indirect access to stack R1 off=-48 size=58")
__naked void not_bypass_stack_boundary_checks_2(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -48;					\
	r2 = 34;					\
	r4 = 24;					\
	r2 |= r4;					\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
	exit;						\
"	:
	: __imm(bpf_probe_read_kernel)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
