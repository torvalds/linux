/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_KCSAN_CHECKS_H
#define _LINUX_KCSAN_CHECKS_H

/* Note: Only include what is already included by compiler.h. */
#include <linux/compiler_attributes.h>
#include <linux/types.h>

/*
 * ACCESS TYPE MODIFIERS
 *
 *   <none>: normal read access;
 *   WRITE : write access;
 *   ATOMIC: access is atomic;
 *   ASSERT: access is not a regular access, but an assertion;
 *   SCOPED: access is a scoped access;
 */
#define KCSAN_ACCESS_WRITE  0x1
#define KCSAN_ACCESS_ATOMIC 0x2
#define KCSAN_ACCESS_ASSERT 0x4
#define KCSAN_ACCESS_SCOPED 0x8

/*
 * __kcsan_*: Always calls into the runtime when KCSAN is enabled. This may be used
 * even in compilation units that selectively disable KCSAN, but must use KCSAN
 * to validate access to an address. Never use these in header files!
 */
#ifdef CONFIG_KCSAN
/**
 * __kcsan_check_access - check generic access for races
 *
 * @ptr: address of access
 * @size: size of access
 * @type: access type modifier
 */
void __kcsan_check_access(const volatile void *ptr, size_t size, int type);

/**
 * kcsan_disable_current - disable KCSAN for the current context
 *
 * Supports nesting.
 */
void kcsan_disable_current(void);

/**
 * kcsan_enable_current - re-enable KCSAN for the current context
 *
 * Supports nesting.
 */
void kcsan_enable_current(void);
void kcsan_enable_current_nowarn(void); /* Safe in uaccess regions. */

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
 * @n: number of following memory accesses to treat as atomic.
 */
void kcsan_atomic_next(int n);

/**
 * kcsan_set_access_mask - set access mask
 *
 * Set the access mask for all accesses for the current context if non-zero.
 * Only value changes to bits set in the mask will be reported.
 *
 * @mask: bitmask
 */
void kcsan_set_access_mask(unsigned long mask);

/* Scoped access information. */
struct kcsan_scoped_access {
	struct list_head list;
	const volatile void *ptr;
	size_t size;
	int type;
};
/*
 * Automatically call kcsan_end_scoped_access() when kcsan_scoped_access goes
 * out of scope; relies on attribute "cleanup", which is supported by all
 * compilers that support KCSAN.
 */
#define __kcsan_cleanup_scoped                                                 \
	__maybe_unused __attribute__((__cleanup__(kcsan_end_scoped_access)))

/**
 * kcsan_begin_scoped_access - begin scoped access
 *
 * Begin scoped access and initialize @sa, which will cause KCSAN to
 * continuously check the memory range in the current thread until
 * kcsan_end_scoped_access() is called for @sa.
 *
 * Scoped accesses are implemented by appending @sa to an internal list for the
 * current execution context, and then checked on every call into the KCSAN
 * runtime.
 *
 * @ptr: address of access
 * @size: size of access
 * @type: access type modifier
 * @sa: struct kcsan_scoped_access to use for the scope of the access
 */
struct kcsan_scoped_access *
kcsan_begin_scoped_access(const volatile void *ptr, size_t size, int type,
			  struct kcsan_scoped_access *sa);

/**
 * kcsan_end_scoped_access - end scoped access
 *
 * End a scoped access, which will stop KCSAN checking the memory range.
 * Requires that kcsan_begin_scoped_access() was previously called once for @sa.
 *
 * @sa: a previously initialized struct kcsan_scoped_access
 */
void kcsan_end_scoped_access(struct kcsan_scoped_access *sa);


#else /* CONFIG_KCSAN */

static inline void __kcsan_check_access(const volatile void *ptr, size_t size,
					int type) { }

static inline void kcsan_disable_current(void)		{ }
static inline void kcsan_enable_current(void)		{ }
static inline void kcsan_enable_current_nowarn(void)	{ }
static inline void kcsan_nestable_atomic_begin(void)	{ }
static inline void kcsan_nestable_atomic_end(void)	{ }
static inline void kcsan_flat_atomic_begin(void)	{ }
static inline void kcsan_flat_atomic_end(void)		{ }
static inline void kcsan_atomic_next(int n)		{ }
static inline void kcsan_set_access_mask(unsigned long mask) { }

struct kcsan_scoped_access { };
#define __kcsan_cleanup_scoped __maybe_unused
static inline struct kcsan_scoped_access *
kcsan_begin_scoped_access(const volatile void *ptr, size_t size, int type,
			  struct kcsan_scoped_access *sa) { return sa; }
static inline void kcsan_end_scoped_access(struct kcsan_scoped_access *sa) { }

#endif /* CONFIG_KCSAN */

#ifdef __SANITIZE_THREAD__
/*
 * Only calls into the runtime when the particular compilation unit has KCSAN
 * instrumentation enabled. May be used in header files.
 */
#define kcsan_check_access __kcsan_check_access

/*
 * Only use these to disable KCSAN for accesses in the current compilation unit;
 * calls into libraries may still perform KCSAN checks.
 */
#define __kcsan_disable_current kcsan_disable_current
#define __kcsan_enable_current kcsan_enable_current_nowarn
#else
static inline void kcsan_check_access(const volatile void *ptr, size_t size,
				      int type) { }
static inline void __kcsan_enable_current(void)  { }
static inline void __kcsan_disable_current(void) { }
#endif

/**
 * __kcsan_check_read - check regular read access for races
 *
 * @ptr: address of access
 * @size: size of access
 */
#define __kcsan_check_read(ptr, size) __kcsan_check_access(ptr, size, 0)

/**
 * __kcsan_check_write - check regular write access for races
 *
 * @ptr: address of access
 * @size: size of access
 */
#define __kcsan_check_write(ptr, size)                                         \
	__kcsan_check_access(ptr, size, KCSAN_ACCESS_WRITE)

/**
 * kcsan_check_read - check regular read access for races
 *
 * @ptr: address of access
 * @size: size of access
 */
#define kcsan_check_read(ptr, size) kcsan_check_access(ptr, size, 0)

/**
 * kcsan_check_write - check regular write access for races
 *
 * @ptr: address of access
 * @size: size of access
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
 * For example, if we only have a single writer, but multiple concurrent
 * readers, to avoid data races, all these accesses must be marked; even
 * concurrent marked writes racing with the single writer are bugs.
 * Unfortunately, due to being marked, they are no longer data races. For cases
 * like these, we can use the macro as follows:
 *
 * .. code-block:: c
 *
 *	void writer(void) {
 *		spin_lock(&update_foo_lock);
 *		ASSERT_EXCLUSIVE_WRITER(shared_foo);
 *		WRITE_ONCE(shared_foo, ...);
 *		spin_unlock(&update_foo_lock);
 *	}
 *	void reader(void) {
 *		// update_foo_lock does not need to be held!
 *		... = READ_ONCE(shared_foo);
 *	}
 *
 * Note: ASSERT_EXCLUSIVE_WRITER_SCOPED(), if applicable, performs more thorough
 * checking if a clear scope where no concurrent writes are expected exists.
 *
 * @var: variable to assert on
 */
#define ASSERT_EXCLUSIVE_WRITER(var)                                           \
	__kcsan_check_access(&(var), sizeof(var), KCSAN_ACCESS_ASSERT)

/*
 * Helper macros for implementation of for ASSERT_EXCLUSIVE_*_SCOPED(). @id is
 * expected to be unique for the scope in which instances of kcsan_scoped_access
 * are declared.
 */
#define __kcsan_scoped_name(c, suffix) __kcsan_scoped_##c##suffix
#define __ASSERT_EXCLUSIVE_SCOPED(var, type, id)                               \
	struct kcsan_scoped_access __kcsan_scoped_name(id, _)                  \
		__kcsan_cleanup_scoped;                                        \
	struct kcsan_scoped_access *__kcsan_scoped_name(id, _dummy_p)          \
		__maybe_unused = kcsan_begin_scoped_access(                    \
			&(var), sizeof(var), KCSAN_ACCESS_SCOPED | (type),     \
			&__kcsan_scoped_name(id, _))

/**
 * ASSERT_EXCLUSIVE_WRITER_SCOPED - assert no concurrent writes to @var in scope
 *
 * Scoped variant of ASSERT_EXCLUSIVE_WRITER().
 *
 * Assert that there are no concurrent writes to @var for the duration of the
 * scope in which it is introduced. This provides a better way to fully cover
 * the enclosing scope, compared to multiple ASSERT_EXCLUSIVE_WRITER(), and
 * increases the likelihood for KCSAN to detect racing accesses.
 *
 * For example, it allows finding race-condition bugs that only occur due to
 * state changes within the scope itself:
 *
 * .. code-block:: c
 *
 *	void writer(void) {
 *		spin_lock(&update_foo_lock);
 *		{
 *			ASSERT_EXCLUSIVE_WRITER_SCOPED(shared_foo);
 *			WRITE_ONCE(shared_foo, 42);
 *			...
 *			// shared_foo should still be 42 here!
 *		}
 *		spin_unlock(&update_foo_lock);
 *	}
 *	void buggy(void) {
 *		if (READ_ONCE(shared_foo) == 42)
 *			WRITE_ONCE(shared_foo, 1); // bug!
 *	}
 *
 * @var: variable to assert on
 */
#define ASSERT_EXCLUSIVE_WRITER_SCOPED(var)                                    \
	__ASSERT_EXCLUSIVE_SCOPED(var, KCSAN_ACCESS_ASSERT, __COUNTER__)

/**
 * ASSERT_EXCLUSIVE_ACCESS - assert no concurrent accesses to @var
 *
 * Assert that there are no concurrent accesses to @var (no readers nor
 * writers). This assertion can be used to specify properties of concurrent
 * code, where violation cannot be detected as a normal data race.
 *
 * For example, where exclusive access is expected after determining no other
 * users of an object are left, but the object is not actually freed. We can
 * check that this property actually holds as follows:
 *
 * .. code-block:: c
 *
 *	if (refcount_dec_and_test(&obj->refcnt)) {
 *		ASSERT_EXCLUSIVE_ACCESS(*obj);
 *		do_some_cleanup(obj);
 *		release_for_reuse(obj);
 *	}
 *
 * Note: ASSERT_EXCLUSIVE_ACCESS_SCOPED(), if applicable, performs more thorough
 * checking if a clear scope where no concurrent accesses are expected exists.
 *
 * Note: For cases where the object is freed, `KASAN <kasan.html>`_ is a better
 * fit to detect use-after-free bugs.
 *
 * @var: variable to assert on
 */
#define ASSERT_EXCLUSIVE_ACCESS(var)                                           \
	__kcsan_check_access(&(var), sizeof(var), KCSAN_ACCESS_WRITE | KCSAN_ACCESS_ASSERT)

/**
 * ASSERT_EXCLUSIVE_ACCESS_SCOPED - assert no concurrent accesses to @var in scope
 *
 * Scoped variant of ASSERT_EXCLUSIVE_ACCESS().
 *
 * Assert that there are no concurrent accesses to @var (no readers nor writers)
 * for the entire duration of the scope in which it is introduced. This provides
 * a better way to fully cover the enclosing scope, compared to multiple
 * ASSERT_EXCLUSIVE_ACCESS(), and increases the likelihood for KCSAN to detect
 * racing accesses.
 *
 * @var: variable to assert on
 */
#define ASSERT_EXCLUSIVE_ACCESS_SCOPED(var)                                    \
	__ASSERT_EXCLUSIVE_SCOPED(var, KCSAN_ACCESS_WRITE | KCSAN_ACCESS_ASSERT, __COUNTER__)

/**
 * ASSERT_EXCLUSIVE_BITS - assert no concurrent writes to subset of bits in @var
 *
 * Bit-granular variant of ASSERT_EXCLUSIVE_WRITER().
 *
 * Assert that there are no concurrent writes to a subset of bits in @var;
 * concurrent readers are permitted. This assertion captures more detailed
 * bit-level properties, compared to the other (word granularity) assertions.
 * Only the bits set in @mask are checked for concurrent modifications, while
 * ignoring the remaining bits, i.e. concurrent writes (or reads) to ~mask bits
 * are ignored.
 *
 * Use this for variables, where some bits must not be modified concurrently,
 * yet other bits are expected to be modified concurrently.
 *
 * For example, variables where, after initialization, some bits are read-only,
 * but other bits may still be modified concurrently. A reader may wish to
 * assert that this is true as follows:
 *
 * .. code-block:: c
 *
 *	ASSERT_EXCLUSIVE_BITS(flags, READ_ONLY_MASK);
 *	foo = (READ_ONCE(flags) & READ_ONLY_MASK) >> READ_ONLY_SHIFT;
 *
 * Note: The access that immediately follows ASSERT_EXCLUSIVE_BITS() is assumed
 * to access the masked bits only, and KCSAN optimistically assumes it is
 * therefore safe, even in the presence of data races, and marking it with
 * READ_ONCE() is optional from KCSAN's point-of-view. We caution, however, that
 * it may still be advisable to do so, since we cannot reason about all compiler
 * optimizations when it comes to bit manipulations (on the reader and writer
 * side). If you are sure nothing can go wrong, we can write the above simply
 * as:
 *
 * .. code-block:: c
 *
 *	ASSERT_EXCLUSIVE_BITS(flags, READ_ONLY_MASK);
 *	foo = (flags & READ_ONLY_MASK) >> READ_ONLY_SHIFT;
 *
 * Another example, where this may be used, is when certain bits of @var may
 * only be modified when holding the appropriate lock, but other bits may still
 * be modified concurrently. Writers, where other bits may change concurrently,
 * could use the assertion as follows:
 *
 * .. code-block:: c
 *
 *	spin_lock(&foo_lock);
 *	ASSERT_EXCLUSIVE_BITS(flags, FOO_MASK);
 *	old_flags = flags;
 *	new_flags = (old_flags & ~FOO_MASK) | (new_foo << FOO_SHIFT);
 *	if (cmpxchg(&flags, old_flags, new_flags) != old_flags) { ... }
 *	spin_unlock(&foo_lock);
 *
 * @var: variable to assert on
 * @mask: only check for modifications to bits set in @mask
 */
#define ASSERT_EXCLUSIVE_BITS(var, mask)                                       \
	do {                                                                   \
		kcsan_set_access_mask(mask);                                   \
		__kcsan_check_access(&(var), sizeof(var), KCSAN_ACCESS_ASSERT);\
		kcsan_set_access_mask(0);                                      \
		kcsan_atomic_next(1);                                          \
	} while (0)

#endif /* _LINUX_KCSAN_CHECKS_H */
