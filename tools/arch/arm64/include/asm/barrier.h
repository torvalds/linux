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

#define smp_store_release(p, v)						\
do {									\
	union { typeof(*p) __val; char __c[1]; } __u =			\
		{ .__val = (v) }; 					\
									\
	switch (sizeof(*p)) {						\
	case 1:								\
		asm volatile ("stlrb %w1, %0"				\
				: "=Q" (*p)				\
				: "r" (*(__u8_alias_t *)__u.__c)	\
				: "memory");				\
		break;							\
	case 2:								\
		asm volatile ("stlrh %w1, %0"				\
				: "=Q" (*p)				\
				: "r" (*(__u16_alias_t *)__u.__c)	\
				: "memory");				\
		break;							\
	case 4:								\
		asm volatile ("stlr %w1, %0"				\
				: "=Q" (*p)				\
				: "r" (*(__u32_alias_t *)__u.__c)	\
				: "memory");				\
		break;							\
	case 8:								\
		asm volatile ("stlr %1, %0"				\
				: "=Q" (*p)				\
				: "r" (*(__u64_alias_t *)__u.__c)	\
				: "memory");				\
		break;							\
	default:							\
		/* Only to shut up gcc ... */				\
		mb();							\
		break;							\
	}								\
} while (0)

#define smp_load_acquire(p)						\
({									\
	union { typeof(*p) __val; char __c[1]; } __u =			\
		{ .__c = { 0 } };					\
									\
	switch (sizeof(*p)) {						\
	case 1:								\
		asm volatile ("ldarb %w0, %1"				\
			: "=r" (*(__u8_alias_t *)__u.__c)		\
			: "Q" (*p) : "memory");				\
		break;							\
	case 2:								\
		asm volatile ("ldarh %w0, %1"				\
			: "=r" (*(__u16_alias_t *)__u.__c)		\
			: "Q" (*p) : "memory");				\
		break;							\
	case 4:								\
		asm volatile ("ldar %w0, %1"				\
			: "=r" (*(__u32_alias_t *)__u.__c)		\
			: "Q" (*p) : "memory");				\
		break;							\
	case 8:								\
		asm volatile ("ldar %0, %1"				\
			: "=r" (*(__u64_alias_t *)__u.__c)		\
			: "Q" (*p) : "memory");				\
		break;							\
	default:							\
		/* Only to shut up gcc ... */				\
		mb();							\
		break;							\
	}								\
	__u.__val;							\
})

#endif /* _TOOLS_LINUX_ASM_AARCH64_BARRIER_H */
