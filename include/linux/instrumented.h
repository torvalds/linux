/* SPDX-License-Identifier: GPL-2.0 */

/*
 * This header provides generic wrappers for memory access instrumentation that
 * the compiler cannot emit for: KASAN, KCSAN, KMSAN.
 */
#ifndef _LINUX_INSTRUMENTED_H
#define _LINUX_INSTRUMENTED_H

#include <linux/compiler.h>
#include <linux/kasan-checks.h>
#include <linux/kcsan-checks.h>
#include <linux/kmsan-checks.h>
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
 * instrument_read_write - instrument regular read-write access
 *
 * Instrument a regular write access. The instrumentation should be inserted
 * before the actual write happens.
 *
 * @ptr address of access
 * @size size of access
 */
static __always_inline void instrument_read_write(const volatile void *v, size_t size)
{
	kasan_check_write(v, size);
	kcsan_check_read_write(v, size);
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
 * instrument_atomic_read_write - instrument atomic read-write access
 *
 * Instrument an atomic read-write access. The instrumentation should be
 * inserted before the actual write happens.
 *
 * @ptr address of access
 * @size size of access
 */
static __always_inline void instrument_atomic_read_write(const volatile void *v, size_t size)
{
	kasan_check_write(v, size);
	kcsan_check_atomic_read_write(v, size);
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
	kmsan_copy_to_user(to, from, n, 0);
}

/**
 * instrument_copy_from_user_before - add instrumentation before copy_from_user
 *
 * Instrument writes to kernel memory, that are due to copy_from_user (and
 * variants). The instrumentation should be inserted before the accesses.
 *
 * @to destination address
 * @from source address
 * @n number of bytes to copy
 */
static __always_inline void
instrument_copy_from_user_before(const void *to, const void __user *from, unsigned long n)
{
	kasan_check_write(to, n);
	kcsan_check_write(to, n);
}

/**
 * instrument_copy_from_user_after - add instrumentation after copy_from_user
 *
 * Instrument writes to kernel memory, that are due to copy_from_user (and
 * variants). The instrumentation should be inserted after the accesses.
 *
 * @to destination address
 * @from source address
 * @n number of bytes to copy
 * @left number of bytes not copied (as returned by copy_from_user)
 */
static __always_inline void
instrument_copy_from_user_after(const void *to, const void __user *from,
				unsigned long n, unsigned long left)
{
	kmsan_unpoison_memory(to, n - left);
}

/**
 * instrument_get_user() - add instrumentation to get_user()-like macros
 *
 * get_user() and friends are fragile, so it may depend on the implementation
 * whether the instrumentation happens before or after the data is copied from
 * the userspace.
 *
 * @to destination variable, may not be address-taken
 */
#define instrument_get_user(to)				\
({							\
	u64 __tmp = (u64)(to);				\
	kmsan_unpoison_memory(&__tmp, sizeof(__tmp));	\
	to = __tmp;					\
})


/**
 * instrument_put_user() - add instrumentation to put_user()-like macros
 *
 * put_user() and friends are fragile, so it may depend on the implementation
 * whether the instrumentation happens before or after the data is copied from
 * the userspace.
 *
 * @from source address
 * @ptr userspace pointer to copy to
 * @size number of bytes to copy
 */
#define instrument_put_user(from, ptr, size)			\
({								\
	kmsan_copy_to_user(ptr, &from, sizeof(from), 0);	\
})

#endif /* _LINUX_INSTRUMENTED_H */
