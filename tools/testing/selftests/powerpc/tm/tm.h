/*
 * Copyright 2015, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#ifndef _SELFTESTS_POWERPC_TM_TM_H
#define _SELFTESTS_POWERPC_TM_TM_H

#include <asm/tm.h>
#include <asm/cputable.h>
#include <stdbool.h>

#include "../utils.h"

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

static inline bool failure_is_nesting(void)
{
	return (__builtin_get_texasru() & 0x400000);
}

#endif /* _SELFTESTS_POWERPC_TM_TM_H */
