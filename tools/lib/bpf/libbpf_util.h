/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
/* Copyright (c) 2019 Facebook */

#ifndef __LIBBPF_LIBBPF_UTIL_H
#define __LIBBPF_LIBBPF_UTIL_H

#include <stdbool.h>
#include <linux/compiler.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Use these barrier functions instead of smp_[rw]mb() when they are
 * used in a libbpf header file. That way they can be built into the
 * application that uses libbpf.
 */
#if defined(__i386__) || defined(__x86_64__)
# define libbpf_smp_store_release(p, v)					\
	do {								\
		asm volatile("" : : : "memory");			\
		WRITE_ONCE(*p, v);					\
	} while (0)
# define libbpf_smp_load_acquire(p)					\
	({								\
		typeof(*p) ___p1 = READ_ONCE(*p);			\
		asm volatile("" : : : "memory");			\
		___p1;							\
	})
#elif defined(__aarch64__)
# define libbpf_smp_store_release(p, v)					\
		asm volatile ("stlr %w1, %0" : "=Q" (*p) : "r" (v) : "memory")
# define libbpf_smp_load_acquire(p)					\
	({								\
		typeof(*p) ___p1;					\
		asm volatile ("ldar %w0, %1"				\
			      : "=r" (___p1) : "Q" (*p) : "memory");	\
		___p1;							\
	})
#elif defined(__riscv)
# define libbpf_smp_store_release(p, v)					\
	do {								\
		asm volatile ("fence rw,w" : : : "memory");		\
		WRITE_ONCE(*p, v);					\
	} while (0)
# define libbpf_smp_load_acquire(p)					\
	({								\
		typeof(*p) ___p1 = READ_ONCE(*p);			\
		asm volatile ("fence r,rw" : : : "memory");		\
		___p1;							\
	})
#endif

#ifndef libbpf_smp_store_release
#define libbpf_smp_store_release(p, v)					\
	do {								\
		__sync_synchronize();					\
		WRITE_ONCE(*p, v);					\
	} while (0)
#endif

#ifndef libbpf_smp_load_acquire
#define libbpf_smp_load_acquire(p)					\
	({								\
		typeof(*p) ___p1 = READ_ONCE(*p);			\
		__sync_synchronize();					\
		___p1;							\
	})
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
