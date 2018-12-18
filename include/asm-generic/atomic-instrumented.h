/*
 * This file provides wrappers with KASAN instrumentation for atomic operations.
 * To use this functionality an arch's atomic.h file needs to define all
 * atomic operations with arch_ prefix (e.g. arch_atomic_read()) and include
 * this file at the end. This file provides atomic_read() that forwards to
 * arch_atomic_read() for actual atomic operation.
 * Note: if an arch atomic operation is implemented by means of other atomic
 * operations (e.g. atomic_read()/atomic_cmpxchg() loop), then it needs to use
 * arch_ variants (i.e. arch_atomic_read()/arch_atomic_cmpxchg()) to avoid
 * double instrumentation.
 */

#ifndef _LINUX_ATOMIC_INSTRUMENTED_H
#define _LINUX_ATOMIC_INSTRUMENTED_H

#include <linux/build_bug.h>
#include <linux/kasan-checks.h>

static __always_inline int atomic_read(const atomic_t *v)
{
	kasan_check_read(v, sizeof(*v));
	return arch_atomic_read(v);
}

static __always_inline s64 atomic64_read(const atomic64_t *v)
{
	kasan_check_read(v, sizeof(*v));
	return arch_atomic64_read(v);
}

static __always_inline void atomic_set(atomic_t *v, int i)
{
	kasan_check_write(v, sizeof(*v));
	arch_atomic_set(v, i);
}

static __always_inline void atomic64_set(atomic64_t *v, s64 i)
{
	kasan_check_write(v, sizeof(*v));
	arch_atomic64_set(v, i);
}

static __always_inline int atomic_xchg(atomic_t *v, int i)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic_xchg(v, i);
}

static __always_inline s64 atomic64_xchg(atomic64_t *v, s64 i)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic64_xchg(v, i);
}

static __always_inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic_cmpxchg(v, old, new);
}

static __always_inline s64 atomic64_cmpxchg(atomic64_t *v, s64 old, s64 new)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic64_cmpxchg(v, old, new);
}

#ifdef arch_atomic_try_cmpxchg
#define atomic_try_cmpxchg atomic_try_cmpxchg
static __always_inline bool atomic_try_cmpxchg(atomic_t *v, int *old, int new)
{
	kasan_check_write(v, sizeof(*v));
	kasan_check_read(old, sizeof(*old));
	return arch_atomic_try_cmpxchg(v, old, new);
}
#endif

#ifdef arch_atomic64_try_cmpxchg
#define atomic64_try_cmpxchg atomic64_try_cmpxchg
static __always_inline bool atomic64_try_cmpxchg(atomic64_t *v, s64 *old, s64 new)
{
	kasan_check_write(v, sizeof(*v));
	kasan_check_read(old, sizeof(*old));
	return arch_atomic64_try_cmpxchg(v, old, new);
}
#endif

#ifdef arch_atomic_fetch_add_unless
#define atomic_fetch_add_unless atomic_fetch_add_unless
static __always_inline int atomic_fetch_add_unless(atomic_t *v, int a, int u)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic_fetch_add_unless(v, a, u);
}
#endif

#ifdef arch_atomic64_fetch_add_unless
#define atomic64_fetch_add_unless atomic64_fetch_add_unless
static __always_inline s64 atomic64_fetch_add_unless(atomic64_t *v, s64 a, s64 u)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic64_fetch_add_unless(v, a, u);
}
#endif

#ifdef arch_atomic_inc
#define atomic_inc atomic_inc
static __always_inline void atomic_inc(atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	arch_atomic_inc(v);
}
#endif

#ifdef arch_atomic64_inc
#define atomic64_inc atomic64_inc
static __always_inline void atomic64_inc(atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	arch_atomic64_inc(v);
}
#endif

#ifdef arch_atomic_dec
#define atomic_dec atomic_dec
static __always_inline void atomic_dec(atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	arch_atomic_dec(v);
}
#endif

#ifdef atch_atomic64_dec
#define atomic64_dec
static __always_inline void atomic64_dec(atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	arch_atomic64_dec(v);
}
#endif

static __always_inline void atomic_add(int i, atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	arch_atomic_add(i, v);
}

static __always_inline void atomic64_add(s64 i, atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	arch_atomic64_add(i, v);
}

static __always_inline void atomic_sub(int i, atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	arch_atomic_sub(i, v);
}

static __always_inline void atomic64_sub(s64 i, atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	arch_atomic64_sub(i, v);
}

static __always_inline void atomic_and(int i, atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	arch_atomic_and(i, v);
}

static __always_inline void atomic64_and(s64 i, atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	arch_atomic64_and(i, v);
}

static __always_inline void atomic_or(int i, atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	arch_atomic_or(i, v);
}

static __always_inline void atomic64_or(s64 i, atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	arch_atomic64_or(i, v);
}

static __always_inline void atomic_xor(int i, atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	arch_atomic_xor(i, v);
}

static __always_inline void atomic64_xor(s64 i, atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	arch_atomic64_xor(i, v);
}

#ifdef arch_atomic_inc_return
#define atomic_inc_return atomic_inc_return
static __always_inline int atomic_inc_return(atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic_inc_return(v);
}
#endif

#ifdef arch_atomic64_in_return
#define atomic64_inc_return atomic64_inc_return
static __always_inline s64 atomic64_inc_return(atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic64_inc_return(v);
}
#endif

#ifdef arch_atomic_dec_return
#define atomic_dec_return atomic_dec_return
static __always_inline int atomic_dec_return(atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic_dec_return(v);
}
#endif

#ifdef arch_atomic64_dec_return
#define atomic64_dec_return atomic64_dec_return
static __always_inline s64 atomic64_dec_return(atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic64_dec_return(v);
}
#endif

#ifdef arch_atomic64_inc_not_zero
#define atomic64_inc_not_zero atomic64_inc_not_zero
static __always_inline bool atomic64_inc_not_zero(atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic64_inc_not_zero(v);
}
#endif

#ifdef arch_atomic64_dec_if_positive
#define atomic64_dec_if_positive atomic64_dec_if_positive
static __always_inline s64 atomic64_dec_if_positive(atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic64_dec_if_positive(v);
}
#endif

#ifdef arch_atomic_dec_and_test
#define atomic_dec_and_test atomic_dec_and_test
static __always_inline bool atomic_dec_and_test(atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic_dec_and_test(v);
}
#endif

#ifdef arch_atomic64_dec_and_test
#define atomic64_dec_and_test atomic64_dec_and_test
static __always_inline bool atomic64_dec_and_test(atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic64_dec_and_test(v);
}
#endif

#ifdef arch_atomic_inc_and_test
#define atomic_inc_and_test atomic_inc_and_test
static __always_inline bool atomic_inc_and_test(atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic_inc_and_test(v);
}
#endif

#ifdef arch_atomic64_inc_and_test
#define atomic64_inc_and_test atomic64_inc_and_test
static __always_inline bool atomic64_inc_and_test(atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic64_inc_and_test(v);
}
#endif

static __always_inline int atomic_add_return(int i, atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic_add_return(i, v);
}

static __always_inline s64 atomic64_add_return(s64 i, atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic64_add_return(i, v);
}

static __always_inline int atomic_sub_return(int i, atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic_sub_return(i, v);
}

static __always_inline s64 atomic64_sub_return(s64 i, atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic64_sub_return(i, v);
}

static __always_inline int atomic_fetch_add(int i, atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic_fetch_add(i, v);
}

static __always_inline s64 atomic64_fetch_add(s64 i, atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic64_fetch_add(i, v);
}

static __always_inline int atomic_fetch_sub(int i, atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic_fetch_sub(i, v);
}

static __always_inline s64 atomic64_fetch_sub(s64 i, atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic64_fetch_sub(i, v);
}

static __always_inline int atomic_fetch_and(int i, atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic_fetch_and(i, v);
}

static __always_inline s64 atomic64_fetch_and(s64 i, atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic64_fetch_and(i, v);
}

static __always_inline int atomic_fetch_or(int i, atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic_fetch_or(i, v);
}

static __always_inline s64 atomic64_fetch_or(s64 i, atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic64_fetch_or(i, v);
}

static __always_inline int atomic_fetch_xor(int i, atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic_fetch_xor(i, v);
}

static __always_inline s64 atomic64_fetch_xor(s64 i, atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic64_fetch_xor(i, v);
}

#ifdef arch_atomic_sub_and_test
#define atomic_sub_and_test atomic_sub_and_test
static __always_inline bool atomic_sub_and_test(int i, atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic_sub_and_test(i, v);
}
#endif

#ifdef arch_atomic64_sub_and_test
#define atomic64_sub_and_test atomic64_sub_and_test
static __always_inline bool atomic64_sub_and_test(s64 i, atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic64_sub_and_test(i, v);
}
#endif

#ifdef arch_atomic_add_negative
#define atomic_add_negative atomic_add_negative
static __always_inline bool atomic_add_negative(int i, atomic_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic_add_negative(i, v);
}
#endif

#ifdef arch_atomic64_add_negative
#define atomic64_add_negative atomic64_add_negative
static __always_inline bool atomic64_add_negative(s64 i, atomic64_t *v)
{
	kasan_check_write(v, sizeof(*v));
	return arch_atomic64_add_negative(i, v);
}
#endif

#define xchg(ptr, new)							\
({									\
	typeof(ptr) __ai_ptr = (ptr);					\
	kasan_check_write(__ai_ptr, sizeof(*__ai_ptr));			\
	arch_xchg(__ai_ptr, (new));					\
})

#define cmpxchg(ptr, old, new)						\
({									\
	typeof(ptr) __ai_ptr = (ptr);					\
	kasan_check_write(__ai_ptr, sizeof(*__ai_ptr));			\
	arch_cmpxchg(__ai_ptr, (old), (new));				\
})

#define sync_cmpxchg(ptr, old, new)					\
({									\
	typeof(ptr) __ai_ptr = (ptr);					\
	kasan_check_write(__ai_ptr, sizeof(*__ai_ptr));			\
	arch_sync_cmpxchg(__ai_ptr, (old), (new));			\
})

#define cmpxchg_local(ptr, old, new)					\
({									\
	typeof(ptr) __ai_ptr = (ptr);					\
	kasan_check_write(__ai_ptr, sizeof(*__ai_ptr));			\
	arch_cmpxchg_local(__ai_ptr, (old), (new));			\
})

#define cmpxchg64(ptr, old, new)					\
({									\
	typeof(ptr) __ai_ptr = (ptr);					\
	kasan_check_write(__ai_ptr, sizeof(*__ai_ptr));			\
	arch_cmpxchg64(__ai_ptr, (old), (new));				\
})

#define cmpxchg64_local(ptr, old, new)					\
({									\
	typeof(ptr) __ai_ptr = (ptr);					\
	kasan_check_write(__ai_ptr, sizeof(*__ai_ptr));			\
	arch_cmpxchg64_local(__ai_ptr, (old), (new));			\
})

#define cmpxchg_double(p1, p2, o1, o2, n1, n2)				\
({									\
	typeof(p1) __ai_p1 = (p1);					\
	kasan_check_write(__ai_p1, 2 * sizeof(*__ai_p1));		\
	arch_cmpxchg_double(__ai_p1, (p2), (o1), (o2), (n1), (n2));	\
})

#define cmpxchg_double_local(p1, p2, o1, o2, n1, n2)				\
({										\
	typeof(p1) __ai_p1 = (p1);						\
	kasan_check_write(__ai_p1, 2 * sizeof(*__ai_p1));			\
	arch_cmpxchg_double_local(__ai_p1, (p2), (o1), (o2), (n1), (n2));	\
})

#endif /* _LINUX_ATOMIC_INSTRUMENTED_H */
