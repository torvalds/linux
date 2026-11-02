// SPDX-License-Identifier: GPL-2.0
/*
 * PAPR (pseries) hcall support.
 */
#include "kvm_util.h"
#include "hcall.h"

s64 hcall0(u64 token)
{
	register uintptr_t r3 asm ("r3") = token;

	asm volatile("sc 1" : "+r"(r3) :
			    : "r0", "r4", "r5", "r6", "r7", "r8", "r9",
			      "r10", "r11", "r12", "ctr", "xer",
			      "cc", "memory");

	return r3;
}

s64 hcall1(u64 token, u64 arg1)
{
	register uintptr_t r3 asm ("r3") = token;
	register uintptr_t r4 asm ("r4") = arg1;

	asm volatile("sc 1" : "+r"(r3), "+r"(r4) :
			    : "r0", "r5", "r6", "r7", "r8", "r9",
			      "r10", "r11", "r12", "ctr", "xer",
			      "cc", "memory");

	return r3;
}

s64 hcall2(u64 token, u64 arg1, u64 arg2)
{
	register uintptr_t r3 asm ("r3") = token;
	register uintptr_t r4 asm ("r4") = arg1;
	register uintptr_t r5 asm ("r5") = arg2;

	asm volatile("sc 1" : "+r"(r3), "+r"(r4), "+r"(r5) :
			    : "r0", "r6", "r7", "r8", "r9",
			      "r10", "r11", "r12", "ctr", "xer",
			      "cc", "memory");

	return r3;
}
