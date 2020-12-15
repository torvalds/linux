// SPDX-License-Identifier: GPL-2.0-only
#ifndef __SELFTESTS_X86_HELPERS_H
#define __SELFTESTS_X86_HELPERS_H

#include <asm/processor-flags.h>

static inline unsigned long get_eflags(void)
{
	unsigned long eflags;

	asm volatile (
#ifdef __x86_64__
		"subq $128, %%rsp\n\t"
		"pushfq\n\t"
		"popq %0\n\t"
		"addq $128, %%rsp"
#else
		"pushfl\n\t"
		"popl %0"
#endif
		: "=r" (eflags) :: "memory");

	return eflags;
}

static inline void set_eflags(unsigned long eflags)
{
	asm volatile (
#ifdef __x86_64__
		"subq $128, %%rsp\n\t"
		"pushq %0\n\t"
		"popfq\n\t"
		"addq $128, %%rsp"
#else
		"pushl %0\n\t"
		"popfl"
#endif
		:: "r" (eflags) : "flags", "memory");
}

#endif /* __SELFTESTS_X86_HELPERS_H */
