/* SPDX-License-Identifier: GPL-2.0 */

/*
 * This header provides generic wrappers for memory access instrumentation that
 * the compiler cannot emit for: KASAN, KCSAN.
 */
#ifndef _LINUX_INSTRUMENTED_H
#define _LINUX_INSTRUMENTED_H

#include <linux/compiler.h>
#include <linux/kasan-checks.h>
#include <linux/kcsan-checks.h>
#include <linux/types.h>

/**
 * instrument_read - instrument regular read access
 *
 * Instrument a regular read access. The instrumentation should be inserted
 * before the actual read happens.
 *
 * @ptr address of access
 * @size size of access
 */
static __always_inline void instrument_read(const volatile void *v, size_t size)
{
	kasan_check_read(v, size);
	kcsan_check_read(v, size);
}

/**
 * instrument_write - instrument regular write access
 *
 * Instrument a regular write access. The instrumentation should be inserted
 * before the actual write happens.
 *
 * @ptr address of access
 * @size size of access
 */
static __always_inline void instrument_write(const volatile void *v, size_t size)
{
	kasan_check_write(v, size);
	kcsan_check_write(v, size);
}

/**
 * instrument_atomic_read - instrument atomic read access
 *
 * Instrument an atomic read access. The instrumentation should be inserted
 * before the actual read happens.
 *
 * @ptr address of access
 * @size size of access
 */
static __always_inline void instrument_atomic_read(const volatile void *v, size_t size)
{
	kasan_check_read(v, size);
	kcsan_check_atomic_read(v, size);
}

/**
 * instrument_atomic_write - instrument atomic write access
 *
 * Instrument an atomic write access. The instrumentation should be inserted
 * before the actual write happens.
 *
 * @ptr address of access
 * @size size of access
 */
static __always_inline void instrument_atomic_write(const volatile void *v, size_t size)
{
	kasan_check_write(v, size);
	kcsan_check_atomic_write(v, size);
}

/**
 * instrument_copy_to_user - instrument reads of copy_to_user
 *
 * Instrument reads from kernel memory, that are due to copy_to_user (and
 * variants). The instrumentation must be inserted before the accesses.
 *
 * @to destination address
 * @from source address
 * @n number of bytes to copy
 */
static __always_inline void
instrument_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	kasan_check_read(from, n);
	kcsan_check_read(from, n);
}

/**
 * instrument_copy_from_user - instrument writes of copy_from_user
 *
 * Instrument writes to kernel memory, that are due to copy_from_user (and
 * variants). The instrumentation should be inserted before the accesses.
 *
 * @to destination address
 * @from source address
 * @n number of bytes to copy
 */
static __always_inline void
instrument_copy_from_user(const void *to, const void __user *from, unsigned long n)
{
	kasan_check_write(to, n);
	kcsan_check_write(to, n);
}

#endif /* _LINUX_INSTRUMENTED_H */
