/* SPDX-License-Identifier: LGPL-2.1-only OR MIT */
/*
 * rseq-x86-thread-pointer.h
 *
 * (C) Copyright 2021 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 */

#ifndef _RSEQ_X86_THREAD_POINTER
#define _RSEQ_X86_THREAD_POINTER

#include <features.h>

#ifdef __cplusplus
extern "C" {
#endif

#if __GNUC_PREREQ (11, 1)
static inline void *rseq_thread_pointer(void)
{
	return __builtin_thread_pointer();
}
#else
static inline void *rseq_thread_pointer(void)
{
	void *__result;

# ifdef __x86_64__
	__asm__ ("mov %%fs:0, %0" : "=r" (__result));
# else
	__asm__ ("mov %%gs:0, %0" : "=r" (__result));
# endif
	return __result;
}
#endif /* !GCC 11 */

#ifdef __cplusplus
}
#endif

#endif
