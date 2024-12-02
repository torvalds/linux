/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_LINUX_ASM_X86_BARRIER_H
#define _TOOLS_LINUX_ASM_X86_BARRIER_H

/*
 * Copied from the Linux kernel sources, and also moving code
 * out from tools/perf/perf-sys.h so as to make it be located
 * in a place similar as in the kernel sources.
 *
 * Force strict CPU ordering.
 * And yes, this is required on UP too when we're talking
 * to devices.
 */

#if defined(__i386__)
/*
 * Some non-Intel clones support out of order store. wmb() ceases to be a
 * nop for these.
 */
#define mb()	asm volatile("lock; addl $0,0(%%esp)" ::: "memory")
#define rmb()	asm volatile("lock; addl $0,0(%%esp)" ::: "memory")
#define wmb()	asm volatile("lock; addl $0,0(%%esp)" ::: "memory")
#elif defined(__x86_64__)
#define mb()	asm volatile("mfence" ::: "memory")
#define rmb()	asm volatile("lfence" ::: "memory")
#define wmb()	asm volatile("sfence" ::: "memory")
#define smp_rmb() barrier()
#define smp_wmb() barrier()
#define smp_mb()  asm volatile("lock; addl $0,-132(%%rsp)" ::: "memory", "cc")
#endif

#if defined(__x86_64__)
#define smp_store_release(p, v)			\
do {						\
	barrier();				\
	WRITE_ONCE(*p, v);			\
} while (0)

#define smp_load_acquire(p)			\
({						\
	typeof(*p) ___p1 = READ_ONCE(*p);	\
	barrier();				\
	___p1;					\
})
#endif /* defined(__x86_64__) */
#endif /* _TOOLS_LINUX_ASM_X86_BARRIER_H */
