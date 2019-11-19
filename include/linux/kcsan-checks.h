/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_KCSAN_CHECKS_H
#define _LINUX_KCSAN_CHECKS_H

#include <linux/types.h>

/*
 * Access type modifiers.
 */
#define KCSAN_ACCESS_WRITE 0x1
#define KCSAN_ACCESS_ATOMIC 0x2

/*
 * __kcsan_*: Always calls into runtime when KCSAN is enabled. This may be used
 * even in compilation units that selectively disable KCSAN, but must use KCSAN
 * to validate access to an address.   Never use these in header files!
 */
#ifdef CONFIG_KCSAN
/**
 * __kcsan_check_access - check generic access for data race
 *
 * @ptr address of access
 * @size size of access
 * @type access type modifier
 */
void __kcsan_check_access(const volatile void *ptr, size_t size, int type);

#else
static inline void __kcsan_check_access(const volatile void *ptr, size_t size,
					int type) { }
#endif

/*
 * kcsan_*: Only calls into runtime when the particular compilation unit has
 * KCSAN instrumentation enabled. May be used in header files.
 */
#ifdef __SANITIZE_THREAD__
#define kcsan_check_access __kcsan_check_access
#else
static inline void kcsan_check_access(const volatile void *ptr, size_t size,
				      int type) { }
#endif

/**
 * __kcsan_check_read - check regular read access for data races
 *
 * @ptr address of access
 * @size size of access
 */
#define __kcsan_check_read(ptr, size) __kcsan_check_access(ptr, size, 0)

/**
 * __kcsan_check_write - check regular write access for data races
 *
 * @ptr address of access
 * @size size of access
 */
#define __kcsan_check_write(ptr, size)                                         \
	__kcsan_check_access(ptr, size, KCSAN_ACCESS_WRITE)

/**
 * kcsan_check_read - check regular read access for data races
 *
 * @ptr address of access
 * @size size of access
 */
#define kcsan_check_read(ptr, size) kcsan_check_access(ptr, size, 0)

/**
 * kcsan_check_write - check regular write access for data races
 *
 * @ptr address of access
 * @size size of access
 */
#define kcsan_check_write(ptr, size)                                           \
	kcsan_check_access(ptr, size, KCSAN_ACCESS_WRITE)

/*
 * Check for atomic accesses: if atomic access are not ignored, this simply
 * aliases to kcsan_check_access, otherwise becomes a no-op.
 */
#ifdef CONFIG_KCSAN_IGNORE_ATOMICS
#define kcsan_check_atomic_read(...)                                           \
	do {                                                                   \
	} while (0)
#define kcsan_check_atomic_write(...)                                          \
	do {                                                                   \
	} while (0)
#else
#define kcsan_check_atomic_read(ptr, size)                                     \
	kcsan_check_access(ptr, size, KCSAN_ACCESS_ATOMIC)
#define kcsan_check_atomic_write(ptr, size)                                    \
	kcsan_check_access(ptr, size, KCSAN_ACCESS_ATOMIC | KCSAN_ACCESS_WRITE)
#endif

#endif /* _LINUX_KCSAN_CHECKS_H */
