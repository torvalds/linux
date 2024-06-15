// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("socket")
__description("or_jmp32_k: bit ops + branch on unknown value")
__failure
__msg("R0 invalid mem access 'scalar'")
__naked void or_jmp32_k(void)
{
	asm volatile ("					\
	r0 = 0xffffffff;				\
	r0 /= 1;					\
	r1 = 0;						\
	w1 = -1;					\
	w1 >>= 1;					\
	w0 &= w1;					\
	w0 |= 2;					\
	if w0 != 0x7ffffffd goto l1;			\
	r0 = 1;						\
	exit;						\
l3:							\
	r0 = 5;						\
	*(u64*)(r0 - 8) = r0;				\
	exit;						\
l2:							\
	w0 -= 0xe;					\
	if w0 == 1 goto l3;				\
	r0 = 4;						\
	exit;						\
l1:							\
	w0 -= 0x7ffffff0;				\
	if w0 s>= 0xe goto l2;				\
	r0 = 3;						\
	exit;						\
"	::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
