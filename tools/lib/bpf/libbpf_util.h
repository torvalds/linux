/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
/* Copyright (c) 2019 Facebook */

#ifndef __LIBBPF_LIBBPF_UTIL_H
#define __LIBBPF_LIBBPF_UTIL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Load-Acquire Store-Release barriers used by the XDP socket
 * library. The following macros should *NOT* be considered part of
 * the xsk.h API, and is subject to change anytime.
 *
 * LIBRARY INTERNAL
 */

#define __XSK_READ_ONCE(x) (*(volatile typeof(x) *)&x)
#define __XSK_WRITE_ONCE(x, v) (*(volatile typeof(x) *)&x) = (v)

#if defined(__i386__) || defined(__x86_64__)
# define libbpf_smp_store_release(p, v)					\
	do {								\
		asm volatile("" : : : "memory");			\
		__XSK_WRITE_ONCE(*p, v);				\
	} while (0)
# define libbpf_smp_load_acquire(p)					\
	({								\
		typeof(*p) ___p1 = __XSK_READ_ONCE(*p);			\
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
		__XSK_WRITE_ONCE(*p, v);				\
	} while (0)
# define libbpf_smp_load_acquire(p)					\
	({								\
		typeof(*p) ___p1 = __XSK_READ_ONCE(*p);			\
		asm volatile ("fence r,rw" : : : "memory");		\
		___p1;							\
	})
#endif

#ifndef libbpf_smp_store_release
#define libbpf_smp_store_release(p, v)					\
	do {								\
		__sync_synchronize();					\
		__XSK_WRITE_ONCE(*p, v);				\
	} while (0)
#endif

#ifndef libbpf_smp_load_acquire
#define libbpf_smp_load_acquire(p)					\
	({								\
		typeof(*p) ___p1 = __XSK_READ_ONCE(*p);			\
		__sync_synchronize();					\
		___p1;							\
	})
#endif

/* LIBRARY INTERNAL -- END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
