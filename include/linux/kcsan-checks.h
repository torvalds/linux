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

/**
 * kcsan_set_access_mask - set access mask
 *
 * Set the access mask for all accesses for the current context if non-zero.
 * Only value changes to bits set in the mask will be reported.
 *
 * @mask bitmask
 */
void kcsan_set_access_mask(unsigned long mask);

#else /* CONFIG_KCSAN */

static inline void __kcsan_check_access(const volatile void *ptr, size_t size,
					int type) { }

static inline void kcsan_nestable_atomic_begin(void)	{ }
static inline void kcsan_nestable_atomic_end(void)	{ }
static inline void kcsan_flat_atomic_begin(void)	{ }
static inline void kcsan_flat_atomic_end(void)		{ }
static inline void kcsan_atomic_next(int n)		{ }
static inline void kcsan_set_access_mask(unsigned long mask) { }

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
 * ASSERT_EXCLUSIVE_WRITER - assert no concurrent writes to @var
 *
 * Assert that there are no concurrent writes to @var; other readers are
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
 * ASSERT_EXCLUSIVE_ACCESS - assert no concurrent accesses to @var
 *
 * Assert that there are no concurrent accesses to @var (no readers nor
 * writers). This assertion can be used to specify properties of concurrent
 * code, where violation cannot be detected as a normal data race.
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

/**
 * ASSERT_EXCLUSIVE_BITS - assert no concurrent writes to subset of bits in @var
 *
 * Bit-granular variant of ASSERT_EXCLUSIVE_WRITER(var).
 *
 * Assert that there are no concurrent writes to a subset of bits in @var;
 * concurrent readers are permitted. This assertion captures more detailed
 * bit-level properties, compared to the other (word granularity) assertions.
 * Only the bits set in @mask are checked for concurrent modifications, while
 * ignoring the remaining bits, i.e. concurrent writes (or reads) to ~@mask bits
 * are ignored.
 *
 * Use this for variables, where some bits must not be modified concurrently,
 * yet other bits are expected to be modified concurrently.
 *
 * For example, variables where, after initialization, some bits are read-only,
 * but other bits may still be modified concurrently. A reader may wish to
 * assert that this is true as follows:
 *
 *	ASSERT_EXCLUSIVE_BITS(flags, READ_ONLY_MASK);
 *	foo = (READ_ONCE(flags) & READ_ONLY_MASK) >> READ_ONLY_SHIFT;
 *
 *   Note: The access that immediately follows ASSERT_EXCLUSIVE_BITS() is
 *   assumed to access the masked bits only, and KCSAN optimistically assumes it
 *   is therefore safe, even in the presence of data races, and marking it with
 *   READ_ONCE() is optional from KCSAN's point-of-view. We caution, however,
 *   that it may still be advisable to do so, since we cannot reason about all
 *   compiler optimizations when it comes to bit manipulations (on the reader
 *   and writer side). If you are sure nothing can go wrong, we can write the
 *   above simply as:
 *
 *	ASSERT_EXCLUSIVE_BITS(flags, READ_ONLY_MASK);
 *	foo = (flags & READ_ONLY_MASK) >> READ_ONLY_SHIFT;
 *
 * Another example, where this may be used, is when certain bits of @var may
 * only be modified when holding the appropriate lock, but other bits may still
 * be modified concurrently. Writers, where other bits may change concurrently,
 * could use the assertion as follows:
 *
 *	spin_lock(&foo_lock);
 *	ASSERT_EXCLUSIVE_BITS(flags, FOO_MASK);
 *	old_flags = READ_ONCE(flags);
 *	new_flags = (old_flags & ~FOO_MASK) | (new_foo << FOO_SHIFT);
 *	if (cmpxchg(&flags, old_flags, new_flags) != old_flags) { ... }
 *	spin_unlock(&foo_lock);
 *
 * @var variable to assert on
 * @mask only check for modifications to bits set in @mask
 */
#define ASSERT_EXCLUSIVE_BITS(var, mask)                                       \
	do {                                                                   \
		kcsan_set_access_mask(mask);                                   \
		__kcsan_check_access(&(var), sizeof(var), KCSAN_ACCESS_ASSERT);\
		kcsan_set_access_mask(0);                                      \
		kcsan_atomic_next(1);                                          \
	} while (0)

#endif /* _LINUX_KCSAN_CHECKS_H */
