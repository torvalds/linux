/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2015, Michael Ellerman, IBM Corp.
 */

#ifndef _SELFTESTS_POWERPC_TM_TM_H
#define _SELFTESTS_POWERPC_TM_TM_H

#include <stdbool.h>
#include <asm/tm.h>

#include "utils.h"
#include "reg.h"

#define TM_RETRIES 100

static inline bool have_htm(void)
{
#ifdef PPC_FEATURE2_HTM
	return have_hwcap2(PPC_FEATURE2_HTM);
#else
	printf("PPC_FEATURE2_HTM not defined, can't check AT_HWCAP2\n");
	return false;
#endif
}

static inline bool have_htm_nosc(void)
{
#ifdef PPC_FEATURE2_HTM_NOSC
	return have_hwcap2(PPC_FEATURE2_HTM_NOSC);
#else
	printf("PPC_FEATURE2_HTM_NOSC not defined, can't check AT_HWCAP2\n");
	return false;
#endif
}

/*
 * Transactional Memory was removed in ISA 3.1. A synthetic TM implementation
 * is provided on P10 for threads running in P8/P9 compatibility  mode. The
 * synthetic implementation immediately fails after tbegin. This failure sets
 * Bit 7 (Failure Persistent) and Bit 15 (Implementation-specific).
 */
static inline bool htm_is_synthetic(void)
{
	int i;

	/*
	 * Per the ISA, the Failure Persistent bit may be incorrect. Try a few
	 * times in case we got an Implementation-specific failure on a non ISA
	 * v3.1 system. On these systems the Implementation-specific failure
	 * should not be persistent.
	 */
	for (i = 0; i < TM_RETRIES; i++) {
		asm volatile(
		"tbegin.;"
		"beq 1f;"
		"tend.;"
		"1:"
		:
		:
		: "memory");

		if ((__builtin_get_texasr() & (TEXASR_FP | TEXASR_IC)) !=
		    (TEXASR_FP | TEXASR_IC))
			break;
	}
	return i == TM_RETRIES;
}

static inline long failure_code(void)
{
	return __builtin_get_texasru() >> 24;
}

static inline bool failure_is_persistent(void)
{
	return (failure_code() & TM_CAUSE_PERSISTENT) == TM_CAUSE_PERSISTENT;
}

static inline bool failure_is_syscall(void)
{
	return (failure_code() & TM_CAUSE_SYSCALL) == TM_CAUSE_SYSCALL;
}

static inline bool failure_is_unavailable(void)
{
	return (failure_code() & TM_CAUSE_FAC_UNAV) == TM_CAUSE_FAC_UNAV;
}

static inline bool failure_is_reschedule(void)
{
	if ((failure_code() & TM_CAUSE_RESCHED) == TM_CAUSE_RESCHED ||
	    (failure_code() & TM_CAUSE_KVM_RESCHED) == TM_CAUSE_KVM_RESCHED ||
	    (failure_code() & TM_CAUSE_KVM_FAC_UNAV) == TM_CAUSE_KVM_FAC_UNAV)
		return true;

	return false;
}

static inline bool failure_is_nesting(void)
{
	return (__builtin_get_texasru() & 0x400000);
}

static inline int tcheck(void)
{
	long cr;
	asm volatile ("tcheck 0" : "=r"(cr) : : "cr0");
	return (cr >> 28) & 4;
}

static inline bool tcheck_doomed(void)
{
	return tcheck() & 8;
}

static inline bool tcheck_active(void)
{
	return tcheck() & 4;
}

static inline bool tcheck_suspended(void)
{
	return tcheck() & 2;
}

static inline bool tcheck_transactional(void)
{
	return tcheck() & 6;
}

#endif /* _SELFTESTS_POWERPC_TM_TM_H */
