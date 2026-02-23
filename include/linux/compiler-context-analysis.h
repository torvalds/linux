/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Macros and attributes for compiler-based static context analysis.
 */

#ifndef _LINUX_COMPILER_CONTEXT_ANALYSIS_H
#define _LINUX_COMPILER_CONTEXT_ANALYSIS_H

#if defined(WARN_CONTEXT_ANALYSIS) && !defined(__CHECKER__) && !defined(__GENKSYMS__)

/*
 * These attributes define new context lock (Clang: capability) types.
 * Internal only.
 */
# define __ctx_lock_type(name)			__attribute__((capability(#name)))
# define __reentrant_ctx_lock			__attribute__((reentrant_capability))
# define __acquires_ctx_lock(...)		__attribute__((acquire_capability(__VA_ARGS__)))
# define __acquires_shared_ctx_lock(...)	__attribute__((acquire_shared_capability(__VA_ARGS__)))
# define __try_acquires_ctx_lock(ret, var)	__attribute__((try_acquire_capability(ret, var)))
# define __try_acquires_shared_ctx_lock(ret, var) __attribute__((try_acquire_shared_capability(ret, var)))
# define __releases_ctx_lock(...)		__attribute__((release_capability(__VA_ARGS__)))
# define __releases_shared_ctx_lock(...)	__attribute__((release_shared_capability(__VA_ARGS__)))
# define __returns_ctx_lock(var)		__attribute__((lock_returned(var)))

/*
 * The below are used to annotate code being checked. Internal only.
 */
# define __excludes_ctx_lock(...)		__attribute__((locks_excluded(__VA_ARGS__)))
# define __requires_ctx_lock(...)		__attribute__((requires_capability(__VA_ARGS__)))
# define __requires_shared_ctx_lock(...)	__attribute__((requires_shared_capability(__VA_ARGS__)))

/*
 * The "assert_capability" attribute is a bit confusingly named. It does not
 * generate a check. Instead, it tells the analysis to *assume* the capability
 * is held. This is used for augmenting runtime assertions, that can then help
 * with patterns beyond the compiler's static reasoning abilities.
 */
# define __assumes_ctx_lock(...)		__attribute__((assert_capability(__VA_ARGS__)))
# define __assumes_shared_ctx_lock(...)	__attribute__((assert_shared_capability(__VA_ARGS__)))

/**
 * __guarded_by - struct member and globals attribute, declares variable
 *                only accessible within active context
 *
 * Declares that the struct member or global variable is only accessible within
 * the context entered by the given context lock. Read operations on the data
 * require shared access, while write operations require exclusive access.
 *
 * .. code-block:: c
 *
 *	struct some_state {
 *		spinlock_t lock;
 *		long counter __guarded_by(&lock);
 *	};
 */
# define __guarded_by(...)		__attribute__((guarded_by(__VA_ARGS__)))

/**
 * __pt_guarded_by - struct member and globals attribute, declares pointed-to
 *                   data only accessible within active context
 *
 * Declares that the data pointed to by the struct member pointer or global
 * pointer is only accessible within the context entered by the given context
 * lock. Read operations on the data require shared access, while write
 * operations require exclusive access.
 *
 * .. code-block:: c
 *
 *	struct some_state {
 *		spinlock_t lock;
 *		long *counter __pt_guarded_by(&lock);
 *	};
 */
# define __pt_guarded_by(...)		__attribute__((pt_guarded_by(__VA_ARGS__)))

/**
 * context_lock_struct() - declare or define a context lock struct
 * @name: struct name
 *
 * Helper to declare or define a struct type that is also a context lock.
 *
 * .. code-block:: c
 *
 *	context_lock_struct(my_handle) {
 *		int foo;
 *		long bar;
 *	};
 *
 *	struct some_state {
 *		...
 *	};
 *	// ... declared elsewhere ...
 *	context_lock_struct(some_state);
 *
 * Note: The implementation defines several helper functions that can acquire
 * and release the context lock.
 */
# define context_lock_struct(name, ...)									\
	struct __ctx_lock_type(name) __VA_ARGS__ name;							\
	static __always_inline void __acquire_ctx_lock(const struct name *var)				\
		__attribute__((overloadable)) __no_context_analysis __acquires_ctx_lock(var) { }	\
	static __always_inline void __acquire_shared_ctx_lock(const struct name *var)			\
		__attribute__((overloadable)) __no_context_analysis __acquires_shared_ctx_lock(var) { } \
	static __always_inline bool __try_acquire_ctx_lock(const struct name *var, bool ret)		\
		__attribute__((overloadable)) __no_context_analysis __try_acquires_ctx_lock(1, var)	\
	{ return ret; }											\
	static __always_inline bool __try_acquire_shared_ctx_lock(const struct name *var, bool ret)	\
		__attribute__((overloadable)) __no_context_analysis __try_acquires_shared_ctx_lock(1, var) \
	{ return ret; }											\
	static __always_inline void __release_ctx_lock(const struct name *var)				\
		__attribute__((overloadable)) __no_context_analysis __releases_ctx_lock(var) { }	\
	static __always_inline void __release_shared_ctx_lock(const struct name *var)			\
		__attribute__((overloadable)) __no_context_analysis __releases_shared_ctx_lock(var) { } \
	static __always_inline void __assume_ctx_lock(const struct name *var)				\
		__attribute__((overloadable)) __assumes_ctx_lock(var) { }				\
	static __always_inline void __assume_shared_ctx_lock(const struct name *var)			\
		__attribute__((overloadable)) __assumes_shared_ctx_lock(var) { }			\
	struct name

/**
 * disable_context_analysis() - disables context analysis
 *
 * Disables context analysis. Must be paired with a later
 * enable_context_analysis().
 */
# define disable_context_analysis()				\
	__diag_push();						\
	__diag_ignore_all("-Wunknown-warning-option", "")	\
	__diag_ignore_all("-Wthread-safety", "")		\
	__diag_ignore_all("-Wthread-safety-pointer", "")

/**
 * enable_context_analysis() - re-enables context analysis
 *
 * Re-enables context analysis. Must be paired with a prior
 * disable_context_analysis().
 */
# define enable_context_analysis() __diag_pop()

/**
 * __no_context_analysis - function attribute, disables context analysis
 *
 * Function attribute denoting that context analysis is disabled for the
 * whole function. Prefer use of `context_unsafe()` where possible.
 */
# define __no_context_analysis	__attribute__((no_thread_safety_analysis))

#else /* !WARN_CONTEXT_ANALYSIS */

# define __ctx_lock_type(name)
# define __reentrant_ctx_lock
# define __acquires_ctx_lock(...)
# define __acquires_shared_ctx_lock(...)
# define __try_acquires_ctx_lock(ret, var)
# define __try_acquires_shared_ctx_lock(ret, var)
# define __releases_ctx_lock(...)
# define __releases_shared_ctx_lock(...)
# define __assumes_ctx_lock(...)
# define __assumes_shared_ctx_lock(...)
# define __returns_ctx_lock(var)
# define __guarded_by(...)
# define __pt_guarded_by(...)
# define __excludes_ctx_lock(...)
# define __requires_ctx_lock(...)
# define __requires_shared_ctx_lock(...)
# define __acquire_ctx_lock(var)			do { } while (0)
# define __acquire_shared_ctx_lock(var)		do { } while (0)
# define __try_acquire_ctx_lock(var, ret)		(ret)
# define __try_acquire_shared_ctx_lock(var, ret)	(ret)
# define __release_ctx_lock(var)			do { } while (0)
# define __release_shared_ctx_lock(var)		do { } while (0)
# define __assume_ctx_lock(var)			do { (void)(var); } while (0)
# define __assume_shared_ctx_lock(var)			do { (void)(var); } while (0)
# define context_lock_struct(name, ...)		struct __VA_ARGS__ name
# define disable_context_analysis()
# define enable_context_analysis()
# define __no_context_analysis

#endif /* WARN_CONTEXT_ANALYSIS */

/**
 * context_unsafe() - disable context checking for contained code
 *
 * Disables context checking for contained statements or expression.
 *
 * .. code-block:: c
 *
 *	struct some_data {
 *		spinlock_t lock;
 *		int counter __guarded_by(&lock);
 *	};
 *
 *	int foo(struct some_data *d)
 *	{
 *		// ...
 *		// other code that is still checked ...
 *		// ...
 *		return context_unsafe(d->counter);
 *	}
 */
#define context_unsafe(...)		\
({					\
	disable_context_analysis();	\
	__VA_ARGS__;			\
	enable_context_analysis()	\
})

/**
 * __context_unsafe() - function attribute, disable context checking
 * @comment: comment explaining why opt-out is safe
 *
 * Function attribute denoting that context analysis is disabled for the
 * whole function. Forces adding an inline comment as argument.
 */
#define __context_unsafe(comment) __no_context_analysis

/**
 * context_unsafe_alias() - helper to insert a context lock "alias barrier"
 * @p: pointer aliasing a context lock or object containing context locks
 *
 * No-op function that acts as a "context lock alias barrier", where the
 * analysis rightfully detects that we're switching aliases, but the switch is
 * considered safe but beyond the analysis reasoning abilities.
 *
 * This should be inserted before the first use of such an alias.
 *
 * Implementation Note: The compiler ignores aliases that may be reassigned but
 * their value cannot be determined (e.g. when passing a non-const pointer to an
 * alias as a function argument).
 */
#define context_unsafe_alias(p) _context_unsafe_alias((void **)&(p))
static inline void _context_unsafe_alias(void **p) { }

/**
 * token_context_lock() - declare an abstract global context lock instance
 * @name: token context lock name
 *
 * Helper that declares an abstract global context lock instance @name, but not
 * backed by a real data structure (linker error if accidentally referenced).
 * The type name is `__ctx_lock_@name`.
 */
#define token_context_lock(name, ...)					\
	context_lock_struct(__ctx_lock_##name, ##__VA_ARGS__) {};	\
	extern const struct __ctx_lock_##name *name

/**
 * token_context_lock_instance() - declare another instance of a global context lock
 * @ctx: token context lock previously declared with token_context_lock()
 * @name: name of additional global context lock instance
 *
 * Helper that declares an additional instance @name of the same token context
 * lock class @ctx. This is helpful where multiple related token contexts are
 * declared, to allow using the same underlying type (`__ctx_lock_@ctx`) as
 * function arguments.
 */
#define token_context_lock_instance(ctx, name)		\
	extern const struct __ctx_lock_##ctx *name

/*
 * Common keywords for static context analysis.
 */

/**
 * __must_hold() - function attribute, caller must hold exclusive context lock
 *
 * Function attribute declaring that the caller must hold the given context
 * lock instance(s) exclusively.
 */
#define __must_hold(...)	__requires_ctx_lock(__VA_ARGS__)

/**
 * __must_not_hold() - function attribute, caller must not hold context lock
 *
 * Function attribute declaring that the caller must not hold the given context
 * lock instance(s).
 */
#define __must_not_hold(...)	__excludes_ctx_lock(__VA_ARGS__)

/**
 * __acquires() - function attribute, function acquires context lock exclusively
 *
 * Function attribute declaring that the function acquires the given context
 * lock instance(s) exclusively, but does not release them.
 */
#define __acquires(...)		__acquires_ctx_lock(__VA_ARGS__)

/*
 * Clang's analysis does not care precisely about the value, only that it is
 * either zero or non-zero. So the __cond_acquires() interface might be
 * misleading if we say that @ret is the value returned if acquired. Instead,
 * provide symbolic variants which we translate.
 */
#define __cond_acquires_impl_true(x, ...)     __try_acquires##__VA_ARGS__##_ctx_lock(1, x)
#define __cond_acquires_impl_false(x, ...)    __try_acquires##__VA_ARGS__##_ctx_lock(0, x)
#define __cond_acquires_impl_nonzero(x, ...)  __try_acquires##__VA_ARGS__##_ctx_lock(1, x)
#define __cond_acquires_impl_0(x, ...)        __try_acquires##__VA_ARGS__##_ctx_lock(0, x)
#define __cond_acquires_impl_nonnull(x, ...)  __try_acquires##__VA_ARGS__##_ctx_lock(1, x)
#define __cond_acquires_impl_NULL(x, ...)     __try_acquires##__VA_ARGS__##_ctx_lock(0, x)

/**
 * __cond_acquires() - function attribute, function conditionally
 *                     acquires a context lock exclusively
 * @ret: abstract value returned by function if context lock acquired
 * @x: context lock instance pointer
 *
 * Function attribute declaring that the function conditionally acquires the
 * given context lock instance @x exclusively, but does not release it. The
 * function return value @ret denotes when the context lock is acquired.
 *
 * @ret may be one of: true, false, nonzero, 0, nonnull, NULL.
 */
#define __cond_acquires(ret, x) __cond_acquires_impl_##ret(x)

/**
 * __releases() - function attribute, function releases a context lock exclusively
 *
 * Function attribute declaring that the function releases the given context
 * lock instance(s) exclusively. The associated context(s) must be active on
 * entry.
 */
#define __releases(...)		__releases_ctx_lock(__VA_ARGS__)

/**
 * __acquire() - function to acquire context lock exclusively
 * @x: context lock instance pointer
 *
 * No-op function that acquires the given context lock instance @x exclusively.
 */
#define __acquire(x)		__acquire_ctx_lock(x)

/**
 * __release() - function to release context lock exclusively
 * @x: context lock instance pointer
 *
 * No-op function that releases the given context lock instance @x.
 */
#define __release(x)		__release_ctx_lock(x)

/**
 * __must_hold_shared() - function attribute, caller must hold shared context lock
 *
 * Function attribute declaring that the caller must hold the given context
 * lock instance(s) with shared access.
 */
#define __must_hold_shared(...)	__requires_shared_ctx_lock(__VA_ARGS__)

/**
 * __acquires_shared() - function attribute, function acquires context lock shared
 *
 * Function attribute declaring that the function acquires the given
 * context lock instance(s) with shared access, but does not release them.
 */
#define __acquires_shared(...)	__acquires_shared_ctx_lock(__VA_ARGS__)

/**
 * __cond_acquires_shared() - function attribute, function conditionally
 *                            acquires a context lock shared
 * @ret: abstract value returned by function if context lock acquired
 * @x: context lock instance pointer
 *
 * Function attribute declaring that the function conditionally acquires the
 * given context lock instance @x with shared access, but does not release it.
 * The function return value @ret denotes when the context lock is acquired.
 *
 * @ret may be one of: true, false, nonzero, 0, nonnull, NULL.
 */
#define __cond_acquires_shared(ret, x) __cond_acquires_impl_##ret(x, _shared)

/**
 * __releases_shared() - function attribute, function releases a
 *                       context lock shared
 *
 * Function attribute declaring that the function releases the given context
 * lock instance(s) with shared access. The associated context(s) must be
 * active on entry.
 */
#define __releases_shared(...)	__releases_shared_ctx_lock(__VA_ARGS__)

/**
 * __acquire_shared() - function to acquire context lock shared
 * @x: context lock instance pointer
 *
 * No-op function that acquires the given context lock instance @x with shared
 * access.
 */
#define __acquire_shared(x)	__acquire_shared_ctx_lock(x)

/**
 * __release_shared() - function to release context lock shared
 * @x: context lock instance pointer
 *
 * No-op function that releases the given context lock instance @x with shared
 * access.
 */
#define __release_shared(x)	__release_shared_ctx_lock(x)

/**
 * __acquire_ret() - helper to acquire context lock of return value
 * @call: call expression
 * @ret_expr: acquire expression that uses __ret
 */
#define __acquire_ret(call, ret_expr)		\
	({					\
		__auto_type __ret = call;	\
		__acquire(ret_expr);		\
		__ret;				\
	})

/**
 * __acquire_shared_ret() - helper to acquire context lock shared of return value
 * @call: call expression
 * @ret_expr: acquire shared expression that uses __ret
 */
#define __acquire_shared_ret(call, ret_expr)	\
	({					\
		__auto_type __ret = call;	\
		__acquire_shared(ret_expr);	\
		__ret;				\
	})

/*
 * Attributes to mark functions returning acquired context locks.
 *
 * This is purely cosmetic to help readability, and should be used with the
 * above macros as follows:
 *
 *   struct foo { spinlock_t lock; ... };
 *   ...
 *   #define myfunc(...) __acquire_ret(_myfunc(__VA_ARGS__), &__ret->lock)
 *   struct foo *_myfunc(int bar) __acquires_ret;
 *   ...
 */
#define __acquires_ret		__no_context_analysis
#define __acquires_shared_ret	__no_context_analysis

#endif /* _LINUX_COMPILER_CONTEXT_ANALYSIS_H */
