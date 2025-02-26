/* SPDX-License-Identifier: LGPL-2.1-only OR MIT */
#ifndef _RSEQ_OR1K_THREAD_POINTER
#define _RSEQ_OR1K_THREAD_POINTER

static inline void *rseq_thread_pointer(void)
{
	void *__thread_register;

	__asm__ ("l.or %0, r10, r0" : "=r" (__thread_register));
	return __thread_register;
}

#endif
