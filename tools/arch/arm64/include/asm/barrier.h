/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_LINUX_ASM_AARCH64_BARRIER_H
#define _TOOLS_LINUX_ASM_AARCH64_BARRIER_H

/*
 * From tools/perf/perf-sys.h, last modified in:
 * f428ebd184c82a7914b2aa7e9f868918aaf7ea78 perf tools: Fix AAAAARGH64 memory barriers
 *
 * XXX: arch/arm64/include/asm/barrier.h in the kernel sources use dsb, is this
 * a case like for arm32 where we do things differently in userspace?
 */

#define mb()		asm volatile("dmb ish" ::: "memory")
#define wmb()		asm volatile("dmb ishst" ::: "memory")
#define rmb()		asm volatile("dmb ishld" ::: "memory")

#endif /* _TOOLS_LINUX_ASM_AARCH64_BARRIER_H */
