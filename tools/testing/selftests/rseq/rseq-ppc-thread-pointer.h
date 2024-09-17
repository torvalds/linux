/* SPDX-License-Identifier: LGPL-2.1-only OR MIT */
/*
 * rseq-ppc-thread-pointer.h
 *
 * (C) Copyright 2021 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 */

#ifndef _RSEQ_PPC_THREAD_POINTER
#define _RSEQ_PPC_THREAD_POINTER

#ifdef __cplusplus
extern "C" {
#endif

static inline void *rseq_thread_pointer(void)
{
#ifdef __powerpc64__
	register void *__result asm ("r13");
#else
	register void *__result asm ("r2");
#endif
	asm ("" : "=r" (__result));
	return __result;
}

#ifdef __cplusplus
}
#endif

#endif
