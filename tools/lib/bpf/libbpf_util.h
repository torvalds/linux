/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
/* Copyright (c) 2019 Facebook */

#ifndef __LIBBPF_LIBBPF_UTIL_H
#define __LIBBPF_LIBBPF_UTIL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Use these barrier functions instead of smp_[rw]mb() when they are
 * used in a libbpf header file. That way they can be built into the
 * application that uses libbpf.
 */
#if defined(__i386__) || defined(__x86_64__)
# define libbpf_smp_rmb() asm volatile("" : : : "memory")
# define libbpf_smp_wmb() asm volatile("" : : : "memory")
# define libbpf_smp_mb() \
	asm volatile("lock; addl $0,-4(%%rsp)" : : : "memory", "cc")
/* Hinders stores to be observed before older loads. */
# define libbpf_smp_rwmb() asm volatile("" : : : "memory")
#elif defined(__aarch64__)
# define libbpf_smp_rmb() asm volatile("dmb ishld" : : : "memory")
# define libbpf_smp_wmb() asm volatile("dmb ishst" : : : "memory")
# define libbpf_smp_mb() asm volatile("dmb ish" : : : "memory")
# define libbpf_smp_rwmb() libbpf_smp_mb()
#elif defined(__arm__)
/* These are only valid for armv7 and above */
# define libbpf_smp_rmb() asm volatile("dmb ish" : : : "memory")
# define libbpf_smp_wmb() asm volatile("dmb ishst" : : : "memory")
# define libbpf_smp_mb() asm volatile("dmb ish" : : : "memory")
# define libbpf_smp_rwmb() libbpf_smp_mb()
#else
/* Architecture missing native barrier functions. */
# define libbpf_smp_rmb() __sync_synchronize()
# define libbpf_smp_wmb() __sync_synchronize()
# define libbpf_smp_mb() __sync_synchronize()
# define libbpf_smp_rwmb() __sync_synchronize()
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
