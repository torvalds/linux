/* $OpenBSD: atomic.h,v 1.25 2025/02/07 03:03:31 jsg Exp $ */
/**
 * \file drm_atomic.h
 * Atomic operations used in the DRM which may or may not be provided by the OS.
 * 
 * \author Eric Anholt <anholt@FreeBSD.org>
 */

/*-
 * Copyright 2004 Eric Anholt
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _DRM_LINUX_ATOMIC_H_
#define _DRM_LINUX_ATOMIC_H_

#include <sys/types.h>
#include <sys/mutex.h>
#include <machine/intr.h>
#include <linux/types.h>
#include <linux/compiler.h>	/* via x86/include/asm/atomic.h */

#define ATOMIC_INIT(x)		(x)

#define atomic_set(p, v)	WRITE_ONCE(*(p), (v))
#define atomic_read(p)		READ_ONCE(*(p))
#define atomic_inc(p)		__sync_fetch_and_add(p, 1)
#define atomic_dec(p)		__sync_fetch_and_sub(p, 1)
#define atomic_add(n, p)	__sync_fetch_and_add(p, n)
#define atomic_sub(n, p)	__sync_fetch_and_sub(p, n)
#define atomic_and(n, p)	__sync_fetch_and_and(p, n)
#define atomic_or(n, p)		atomic_setbits_int(p, n)
#define atomic_add_return(n, p) __sync_add_and_fetch(p, n)
#define atomic_sub_return(n, p) __sync_sub_and_fetch(p, n)
#define atomic_sub_and_test(n, p)	(atomic_sub_return(n, p) == 0)
#define atomic_inc_return(v)	atomic_add_return(1, (v))
#define atomic_dec_return(v)	atomic_sub_return(1, (v))
#define atomic_dec_and_test(v)	(atomic_dec_return(v) == 0)
#define atomic_inc_and_test(v)	(atomic_inc_return(v) == 0)
#define atomic_cmpxchg(p, o, n)	__sync_val_compare_and_swap(p, o, n)
#define cmpxchg(p, o, n)	__sync_val_compare_and_swap(p, o, n)
#define cmpxchg64(p, o, n)	__sync_val_compare_and_swap(p, o, n)
#define atomic_set_release(p, v)	atomic_set((p), (v))
#define atomic_andnot(bits, p)		atomic_clearbits_int(p,bits)
#define atomic_fetch_inc(p)		__sync_fetch_and_add(p, 1)
#define atomic_fetch_xor(n, p)		__sync_fetch_and_xor(p, n)

#define try_cmpxchg(p, op, n)						\
({									\
	__typeof(p) __op = (__typeof((p)))(op);				\
	__typeof(*(p)) __o = *__op;					\
	__typeof(*(p)) __p = __sync_val_compare_and_swap((p), (__o), (n)); \
	if (__p != __o)							\
		*__op = __p;						\
	(__p == __o);							\
})

static inline bool
atomic_try_cmpxchg(volatile int *p, int *op, int n)
{
	return try_cmpxchg(p, op, n);
}

static inline int
atomic_xchg(volatile int *v, int n)
{
	__sync_synchronize();
	return __sync_lock_test_and_set(v, n);
}

#define xchg(v, n)	__sync_lock_test_and_set(v, n)

static inline int
atomic_add_unless(volatile int *v, int n, int u)
{
	int o;

	do {
		o = *v;
		if (o == u)
			return 0;
	} while (__sync_val_compare_and_swap(v, o, o +n) != o);

	return 1;
}

#define atomic_inc_not_zero(v)	atomic_add_unless((v), 1, 0)

static inline int
atomic_dec_if_positive(volatile int *v)
{
	int r, o;

	do {
		o = *v;
		r = o - 1;
		if (r < 0)
			break;
	} while (__sync_val_compare_and_swap(v, o, r) != o);

	return r;
}

#define atomic_long_read(p)	READ_ONCE(*(p))

/* 32 bit powerpc lacks 64 bit atomics */
#if !defined(__powerpc__) || defined(__powerpc64__)

typedef int64_t atomic64_t __aligned(8);

#define ATOMIC64_INIT(x)	(x)

#define atomic64_set(p, v)	WRITE_ONCE(*(p), (v))
#define atomic64_read(p)	READ_ONCE(*(p))

static inline int64_t
atomic64_xchg(atomic64_t *v, int64_t n)
{
	__sync_synchronize();
	return __sync_lock_test_and_set(v, n);
}

static inline int64_t
atomic64_cmpxchg(atomic64_t *v, int64_t o, int64_t n)
{
	return __sync_val_compare_and_swap(v, o, n);
}

#define atomic64_add(n, p)	__sync_fetch_and_add_8(p, n)
#define atomic64_sub(n, p)	__sync_fetch_and_sub_8(p, n)
#define atomic64_inc(p)		__sync_fetch_and_add_8(p, 1)
#define atomic64_add_return(n, p) __sync_add_and_fetch_8(p, n)
#define atomic64_inc_return(p)	__sync_add_and_fetch_8(p, 1)

#else

extern struct mutex atomic64_mtx;

typedef struct {
	volatile int64_t val;
} atomic64_t;

#define ATOMIC64_INIT(x)	{ (x) }

static inline void
atomic64_set(atomic64_t *v, int64_t i)
{
	mtx_enter(&atomic64_mtx);
	v->val = i;
	mtx_leave(&atomic64_mtx);
}

static inline int64_t
atomic64_read(atomic64_t *v)
{
	int64_t val;

	mtx_enter(&atomic64_mtx);
	val = v->val;
	mtx_leave(&atomic64_mtx);

	return val;
}

static inline int64_t
atomic64_xchg(atomic64_t *v, int64_t n)
{
	int64_t val;

	mtx_enter(&atomic64_mtx);
	val = v->val;
	v->val = n;
	mtx_leave(&atomic64_mtx);

	return val;
}

static inline void
atomic64_add(int i, atomic64_t *v)
{
	mtx_enter(&atomic64_mtx);
	v->val += i;
	mtx_leave(&atomic64_mtx);
}

#define atomic64_inc(p)		atomic64_add(p, 1)

static inline int64_t
atomic64_add_return(int i, atomic64_t *v)
{
	int64_t val;

	mtx_enter(&atomic64_mtx);
	val = v->val + i;
	v->val = val;
	mtx_leave(&atomic64_mtx);

	return val;
}

#define atomic64_inc_return(p)		atomic64_add_return(1, p)

static inline void
atomic64_sub(int i, atomic64_t *v)
{
	mtx_enter(&atomic64_mtx);
	v->val -= i;
	mtx_leave(&atomic64_mtx);
}
#endif

#ifdef __LP64__
typedef int64_t atomic_long_t;
#define atomic_long_set(p, v)		atomic64_set(p, v)
#define atomic_long_xchg(v, n)		atomic64_xchg(v, n)
#define atomic_long_cmpxchg(p, o, n)	atomic_cmpxchg(p, o, n)
#define atomic_long_add(i, v)		atomic64_add(i, v)
#define atomic_long_sub(i, v)		atomic64_sub(i, v)
#else
typedef int32_t atomic_long_t;
#define atomic_long_set(p, v)		atomic_set(p, v)
#define atomic_long_xchg(v, n)		atomic_xchg(v, n)
#define atomic_long_cmpxchg(p, o, n)	atomic_cmpxchg(p, o, n)
#define atomic_long_add(i, v)		atomic_add(i, v)
#define atomic_long_sub(i, v)		atomic_sub(i, v)
#endif

static inline atomic_t
test_and_set_bit(u_int b, volatile void *p)
{
	unsigned int m = 1 << (b & 0x1f);
	unsigned int prev = __sync_fetch_and_or((volatile u_int *)p + (b >> 5), m);
	return (prev & m) != 0;
}

static inline void
clear_bit(u_int b, volatile void *p)
{
	atomic_clearbits_int(((volatile u_int *)p) + (b >> 5), 1 << (b & 0x1f));
}

static inline void
clear_bit_unlock(u_int b, volatile void *p)
{
	membar_enter();
	clear_bit(b, p);
}

static inline void
set_bit(u_int b, volatile void *p)
{
	atomic_setbits_int(((volatile u_int *)p) + (b >> 5), 1 << (b & 0x1f));
}

static inline void
__clear_bit(u_int b, volatile void *p)
{
	volatile u_int *ptr = (volatile u_int *)p;
	ptr[b >> 5] &= ~(1 << (b & 0x1f));
}

static inline void
__set_bit(u_int b, volatile void *p)
{
	volatile u_int *ptr = (volatile u_int *)p;
	ptr[b >> 5] |= (1 << (b & 0x1f));
}

static inline int
test_bit(u_int b, const volatile void *p)
{
	return !!(((volatile u_int *)p)[b >> 5] & (1 << (b & 0x1f)));
}

static inline int
__test_and_set_bit(u_int b, volatile void *p)
{
	unsigned int m = 1 << (b & 0x1f);
	volatile u_int *ptr = (volatile u_int *)p;
	unsigned int prev = ptr[b >> 5];
	ptr[b >> 5] |= m;
	
	return (prev & m) != 0;
}

static inline int
test_and_clear_bit(u_int b, volatile void *p)
{
	unsigned int m = 1 << (b & 0x1f);
	unsigned int prev = __sync_fetch_and_and((volatile u_int *)p + (b >> 5), ~m);
	return (prev & m) != 0;
}

static inline int
__test_and_clear_bit(u_int b, volatile void *p)
{
	volatile u_int *ptr = (volatile u_int *)p;
	int rv = !!(ptr[b >> 5] & (1 << (b & 0x1f)));
	ptr[b >> 5] &= ~(1 << (b & 0x1f));
	return rv;
}

static inline int
find_first_zero_bit(volatile void *p, int max)
{
	int b;
	volatile u_int *ptr = (volatile u_int *)p;

	for (b = 0; b < max; b += 32) {
		if (ptr[b >> 5] != ~0) {
			for (;;) {
				if ((ptr[b >> 5] & (1 << (b & 0x1f))) == 0)
					return b;
				b++;
			}
		}
	}
	return max;
}

static inline int
find_next_zero_bit(volatile void *p, int max, int b)
{
	volatile u_int *ptr = (volatile u_int *)p;

	for (; b < max; b += 32) {
		if (ptr[b >> 5] != ~0) {
			for (;;) {
				if ((ptr[b >> 5] & (1 << (b & 0x1f))) == 0)
					return b;
				b++;
			}
		}
	}
	return max;
}

static inline int
find_first_bit(volatile void *p, int max)
{
	int b;
	volatile u_int *ptr = (volatile u_int *)p;

	for (b = 0; b < max; b += 32) {
		if (ptr[b >> 5] != 0) {
			for (;;) {
				if (ptr[b >> 5] & (1 << (b & 0x1f)))
					return b;
				b++;
			}
		}
	}
	return max;
}

static inline int
find_next_bit(const volatile void *p, int max, int b)
{
	volatile u_int *ptr = (volatile u_int *)p;

	for (; b < max; b+= 32) {
		if (ptr[b >> 5] != 0) {
			for (;;) {
				if (ptr[b >> 5] & (1 << (b & 0x1f)))
					return b;
				b++;
			}
		}
	}
	return max;
}

#define for_each_set_bit(b, p, max) \
	for ((b) = find_first_bit((p), (max));			\
	     (b) < (max);					\
	     (b) = find_next_bit((p), (max), (b) + 1))

#define for_each_clear_bit(b, p, max) \
	for ((b) = find_first_zero_bit((p), (max));		\
	     (b) < (max);					\
	     (b) = find_next_zero_bit((p), (max), (b) + 1))

#if defined(__i386__)
#define rmb()	__asm volatile("lock; addl $0,-4(%%esp)" : : : "memory", "cc")
#define wmb()	__asm volatile("lock; addl $0,-4(%%esp)" : : : "memory", "cc")
#define mb()	__asm volatile("lock; addl $0,-4(%%esp)" : : : "memory", "cc")
#define smp_mb()	__asm volatile("lock; addl $0,-4(%%esp)" : : : "memory", "cc")
#define smp_rmb()	__membar("")
#define smp_wmb()	__membar("")
#define __smp_store_mb(var, value)	do { (void)xchg(&var, value); } while (0)
#define smp_mb__after_atomic()	do { } while (0)
#define smp_mb__before_atomic()	do { } while (0)
#elif defined(__amd64__)
#define rmb()	__membar("lfence")
#define wmb()	__membar("sfence")
#define mb()	__membar("mfence")
#define smp_mb()	__asm volatile("lock; addl $0,-4(%%rsp)" : : : "memory", "cc")
#define smp_rmb()	__membar("")
#define smp_wmb()	__membar("")
#define __smp_store_mb(var, value)	do { (void)xchg(&var, value); } while (0)
#define smp_mb__after_atomic()	do { } while (0)
#define smp_mb__before_atomic()	do { } while (0)
#elif defined(__aarch64__)
#define rmb()	__membar("dsb ld")
#define wmb()	__membar("dsb st")
#define mb()	__membar("dsb sy")
#define dma_rmb() __membar("dmb oshld")
#define dma_wmb() __membar("dmb oshst")
#define dma_mb() __membar("dmb osh")
#define smp_mb() __membar("dmb ish")
#elif defined(__arm__)
#define rmb()	__membar("dsb sy")
#define wmb()	__membar("dsb sy")
#define mb()	__membar("dsb sy")
#elif defined(__mips64__)
#define rmb()	mips_sync() 
#define wmb()	mips_sync()
#define mb()	mips_sync()
#elif defined(__powerpc64__)
#define rmb()	__membar("sync")
#define wmb()	__membar("sync")
#define mb()	__membar("sync")
#define smp_rmb()	__membar("lwsync")
#define smp_wmb()	__membar("lwsync")
#define smp_mb()	__membar("sync")
#elif defined(__powerpc__)
#define rmb()	__membar("sync")
#define wmb()	__membar("sync")
#define mb()	__membar("sync")
#define smp_wmb()	__membar("eieio")
#elif defined(__riscv)
#define rmb()	__membar("fence ir,ir")
#define wmb()	__membar("fence ow,ow")
#define mb()	__membar("fence iorw,iorw")
#define smp_rmb()	__membar("fence r,r")
#define smp_wmb()	__membar("fence w,w")
#define smp_mb()	__membar("fence rw,rw")
#elif defined(__sparc64__)
#define rmb()	membar_sync()
#define wmb()	membar_sync()
#define mb()	membar_sync()
#endif

#ifndef smp_rmb
#define smp_rmb()	rmb()
#endif

#ifndef smp_wmb
#define smp_wmb()	wmb()
#endif

#ifndef mmiowb
#define mmiowb()	wmb()
#endif

#ifndef smp_mb__before_atomic
#define smp_mb__before_atomic()	mb()
#endif

#ifndef smp_mb__after_atomic
#define smp_mb__after_atomic()	mb()
#endif

#ifndef smp_store_mb
#define smp_store_mb(x, v)	do { x = v; mb(); } while (0)
#endif

#ifndef smp_store_release
#define smp_store_release(x, v)	do { smp_mb(); WRITE_ONCE(*x, v); } while(0)
#endif

#ifndef smp_load_acquire
#define smp_load_acquire(x)			\
({						\
	__typeof(*x) _v = READ_ONCE(*x);	\
	smp_mb();				\
	_v;					\
})
#endif

#endif
