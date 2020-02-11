/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_KCSAN_CHECKS_H
#define _LINUX_KCSAN_CHECKS_H

#include <linux/types.h>

/*
 * ACCESS TYPE MODIFIERS
 *
 *   <none>: normal read access;
 *   WRITE : write access;
 *   ATOMIC: access is atomic;
 *   ASSERT: access is not a regular access, but an assertion;
 */
#define KCSAN_ACCESS_WRITE  0x1
#define KCSAN_ACCESS_ATOMIC 0x2
#define KCSAN_ACCESS_ASSERT 0x4

/*
 * __kcsan_*: Always calls into the runtime when KCSAN is enabled. This may be used
 * even in compilation units that selectively disable KCSAN, but must use KCSAN
 * to validate access to an address. Never use these in header files!
 */
#ifdef CONFIG_KCSAN
/**
 * __kcsan_check_access - check generic access for races
 *
 * @ptr address of access
 * @size size of access
 * @type access type modifier
 */
void __kcsan_check_access(const volatile void *ptr, size_t size, int type);

/**
 * kcsan_nestable_atomic_begin - begin nestable atomic region
 *
 * Accesses within the atomic region may appear to race with other accesses but
 * should be considered atomic.
 */
void kcsan_nestable_atomic_begin(void);

/**
 * kcsan_nestable_atomic_end - end nestable atomic region
 */
void kcsan_nestable_atomic_end(void);

/**
 * kcsan_flat_atomic_begin - begin flat atomic region
 *
 * Accesses within the atomic region may appear to race with other accesses but
 * should be considered atomic.
 */
void kcsan_flat_atomic_begin(void);

/**
 * kcsan_flat_atomic_end - end flat atomic region
 */
void kcsan_flat_atomic_end(void);

/**
 * kcsan_atomic_next - consider following accesses as atomic
 *
 * Force treating the next n memory accesses for the current context as atomic
 * operations.
 *
 * @n number of following memory accesses to treat as atomic.
 */
void kcsan_atomic_next(int n);

#else /* CONFIG_KCSAN */

static inline void __kcsan_check_access(const volatile void *ptr, size_t size,
					int type) { }

static inline void kcsan_nestable_atomic_begin(void)	{ }
static inline void kcsan_nestable_atomic_end(void)	{ }
static inline void kcsan_flat_atomic_begin(void)	{ }
static inline void kcsan_flat_atomic_end(void)		{ }
static inline void kcsan_atomic_next(int n)		{ }

#endif /* CONFIG_KCSAN */

/*
 * kcsan_*: Only calls into the runtime when the particular compilation unit has
 * KCSAN instrumentation enabled. May be used in header files.
 */
#ifdef __SANITIZE_THREAD__
#define kcsan_check_access __kcsan_check_access
#else
static inline void kcsan_check_access(const volatile void *ptr, size_t size,
				      int type) { }
#endif

/**
 * __kcsan_check_read - check regular read access for races
 *
 * @ptr address of access
 * @size size of access
 */
#define __kcsan_check_read(ptr, size) __kcsan_check_access(ptr, size, 0)

/**
 * __kcsan_check_write - check regular write access for races
 *
 * @ptr address of access
 * @size size of access
 */
#define __kcsan_check_write(ptr, size)                                         \
	__kcsan_check_access(ptr, size, KCSAN_ACCESS_WRITE)

/**
 * kcsan_check_read - check regular read access for races
 *
 * @ptr address of access
 * @size size of access
 */
#define kcsan_check_read(ptr, size) kcsan_check_access(ptr, size, 0)

/**
 * kcsan_check_write - check regular write access for races
 *
 * @ptr address of access
 * @size size of access
 */
#define kcsan_check_write(ptr, size)                                           \
	kcsan_check_access(ptr, size, KCSAN_ACCESS_WRITE)

/*
 * Check for atomic accesses: if atomic accesses are not ignored, this simply
 * aliases to kcsan_check_access(), otherwise becomes a no-op.
 */
#ifdef CONFIG_KCSAN_IGNORE_ATOMICS
#define kcsan_check_atomic_read(...)	do { } while (0)
#define kcsan_check_atomic_write(...)	do { } while (0)
#else
#define kcsan_check_atomic_read(ptr, size)                                     \
	kcsan_check_access(ptr, size, KCSAN_ACCESS_ATOMIC)
#define kcsan_check_atomic_write(ptr, size)                                    \
	kcsan_check_access(ptr, size, KCSAN_ACCESS_ATOMIC | KCSAN_ACCESS_WRITE)
#endif

/**
 * ASSERT_EXCLUSIVE_WRITER - assert no other threads are writing @var
 *
 * Assert that there are no other threads writing @var; other readers are
 * allowed. This assertion can be used to specify properties of concurrent code,
 * where violation cannot be detected as a normal data race.
 *
 * For example, if a per-CPU variable is only meant to be written by a single
 * CPU, but may be read from other CPUs; in this case, reads and writes must be
 * marked properly, however, if an off-CPU WRITE_ONCE() races with the owning
 * CPU's WRITE_ONCE(), would not constitute a data race but could be a harmful
 * race condition. Using this macro allows specifying this property in the code
 * and catch such bugs.
 *
 * @var variable to assert on
 */
#define ASSERT_EXCLUSIVE_WRITER(var)                                           \
	__kcsan_check_access(&(var), sizeof(var), KCSAN_ACCESS_ASSERT)

/**
 * ASSERT_EXCLUSIVE_ACCESS - assert no other threads are accessing @var
 *
 * Assert that no other thread is accessing @var (no readers nor writers). This
 * assertion can be used to specify properties of concurrent code, where
 * violation cannot be detected as a normal data race.
 *
 * For example, in a reference-counting algorithm where exclusive access is
 * expected after the refcount reaches 0. We can check that this property
 * actually holds as follows:
 *
 *	if (refcount_dec_and_test(&obj->refcnt)) {
 *		ASSERT_EXCLUSIVE_ACCESS(*obj);
 *		safely_dispose_of(obj);
 *	}
 *
 * @var variable to assert on
 */
#define ASSERT_EXCLUSIVE_ACCESS(var)                                           \
	__kcsan_check_access(&(var), sizeof(var), KCSAN_ACCESS_WRITE | KCSAN_ACCESS_ASSERT)

#endif /* _LINUX_KCSAN_CHECKS_H */
