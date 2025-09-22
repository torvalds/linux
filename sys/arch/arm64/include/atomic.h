/* $OpenBSD: atomic.h,v 1.4 2022/08/29 02:01:18 jsg Exp $ */

/* Public Domain */

#ifndef _MACHINE_ATOMIC_H_
#define _MACHINE_ATOMIC_H_

#define __membar(_f) do { __asm volatile(_f ::: "memory"); } while (0)

#define membar_enter()		__membar("dmb sy")
#define membar_exit()		__membar("dmb sy")
#define membar_producer()	__membar("dmb st")
#define membar_consumer()	__membar("dmb ld")
#define membar_sync()		__membar("dmb sy")

#if defined(_KERNEL)

/* virtio needs MP membars even on SP kernels */
#define virtio_membar_producer()	__membar("dmb st")
#define virtio_membar_consumer()	__membar("dmb ld")
#define virtio_membar_sync()		__membar("dmb sy")

/*
 * Set bits
 * *p = *p | v
 */
static inline void
atomic_setbits_int(volatile unsigned int *p, unsigned int v)
{
	unsigned int modified, tmp;

	__asm volatile (
	    "1:	ldxr %w0, [%x3]		\n\t"
	    "	orr %w0, %w0, %w2	\n\t"
	    "	stxr %w1, %w0, [%x3]	\n\t"
	    "	cbnz %w1, 1b		\n\t"
	    : "=&r" (tmp), "=&r" (modified)
	    : "r" (v), "r" (p)
	    : "memory", "cc"
	);
}

/*
 * Clear bits
 * *p = *p & (~v)
 */
static inline void
atomic_clearbits_int(volatile unsigned int *p, unsigned int v)
{
	unsigned int modified, tmp;

	__asm volatile (
	    "1:	ldxr %w0, [%x3]		\n\t"
	    "	bic %w0, %w0, %w2	\n\t"
	    "	stxr %w1, %w0, [%x3]	\n\t"
	    "	cbnz %w1, 1b		\n\t"
	    : "=&r" (tmp), "=&r" (modified)
	    : "r" (v), "r" (p)
	    : "memory", "cc"
	);
}

#endif /* defined(_KERNEL) */
#endif /* _MACHINE_ATOMIC_H_ */
