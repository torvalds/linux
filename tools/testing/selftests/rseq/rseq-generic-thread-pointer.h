/* SPDX-License-Identifier: LGPL-2.1-only OR MIT */
/*
 * rseq-generic-thread-pointer.h
 *
 * (C) Copyright 2021 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 */

#ifndef _RSEQ_GENERIC_THREAD_POINTER
#define _RSEQ_GENERIC_THREAD_POINTER

#ifdef __cplusplus
extern "C" {
#endif

/* Use gcc builtin thread pointer. */
static inline void *rseq_thread_pointer(void)
{
	return __builtin_thread_pointer();
}

#ifdef __cplusplus
}
#endif

#endif
