/*	$OpenBSD: atomic.h,v 1.19 2022/08/29 02:01:18 jsg Exp $	*/

/* Public Domain */

#ifndef _ARM_ATOMIC_H_
#define _ARM_ATOMIC_H_

/*
 * Compare and set:
 * ret = *ptr
 * if (ret == expect)
 * 	*ptr = new
 * return (ret)
 */
#define _def_atomic_cas(_f, _t)					\
static inline _t						\
_f(volatile _t *p, _t e, _t n)					\
{								\
	_t ret, modified;					\
								\
	__asm volatile (					\
	    "1:	ldrex %0, [%4]		\n\t"			\
	    "	cmp %0, %3		\n\t"			\
	    "	bne 2f			\n\t"			\
	    "	strex %1, %2, [%4]	\n\t"			\
	    "	cmp %1, #0		\n\t"			\
	    "	bne 1b			\n\t"			\
	    "	b 3f			\n\t"			\
	    "2:	clrex			\n\t"			\
	    "3:				\n\t"			\
	    : "=&r" (ret), "=&r" (modified)			\
	    : "r" (n), "r" (e), "r" (p)				\
	    : "memory", "cc"					\
	);							\
	return (ret);						\
}
_def_atomic_cas(_atomic_cas_uint, unsigned int)
_def_atomic_cas(_atomic_cas_ulong, unsigned long)
#undef _def_atomic_cas

#define atomic_cas_uint(_p, _e, _n) _atomic_cas_uint((_p), (_e), (_n))
#define atomic_cas_ulong(_p, _e, _n) _atomic_cas_ulong((_p), (_e), (_n))

static inline void *
_atomic_cas_ptr(volatile void *p, void *e, void *n)
{
	void *ret;
	unsigned long modified;

	__asm volatile (
	    "1:	ldrex %0, [%4]		\n\t"
	    "	cmp %0, %3		\n\t"
	    "	bne 2f			\n\t"
	    "	strex %1, %2, [%4]	\n\t"
	    "	cmp %1, #0		\n\t"
	    "	bne 1b			\n\t"
	    "	b 3f			\n\t"
	    "2:	clrex			\n\t"
	    "3:				\n\t"
	    : "=&r" (ret), "=&r" (modified)
	    : "r" (n), "r" (e), "r" (p)
	    : "memory", "cc"
	);
	return (ret);
}
#define atomic_cas_ptr(_p, _e, _n) _atomic_cas_ptr((_p), (_e), (_n))

/*
 * Swap:
 * ret = *p
 * *p = val
 * return (ret)
 */
#define _def_atomic_swap(_f, _t)				\
static inline _t						\
_f(volatile _t *p, _t v)					\
{								\
	_t ret, modified;					\
								\
	__asm volatile (					\
	    "1:	ldrex %0, [%3]		\n\t"			\
	    "	strex %1, %2, [%3]	\n\t"			\
	    "	cmp %1, #0		\n\t"			\
	    "	bne 1b			\n\t"			\
	    : "=&r" (ret), "=&r" (modified)			\
	    : "r" (v), "r" (p)					\
	    : "memory", "cc"					\
	);							\
	return (ret);						\
}
_def_atomic_swap(_atomic_swap_uint, unsigned int)
_def_atomic_swap(_atomic_swap_ulong, unsigned long)
#undef _def_atomic_swap

#define atomic_swap_uint(_p, _v) _atomic_swap_uint((_p), (_v))
#define atomic_swap_ulong(_p, _v) _atomic_swap_ulong((_p), (_v))

static inline void *
_atomic_swap_ptr(volatile void *p, void *v)
{
	void *ret;
	unsigned long modified;

	__asm volatile (
	    "1:	ldrex %0, [%3]		\n\t"
	    "	strex %1, %2, [%3]	\n\t"
	    "	cmp %1, #0		\n\t"
	    "	bne 1b			\n\t"
	    : "=&r" (ret), "=&r" (modified)
	    : "r" (v), "r" (p)
	    : "memory", "cc"
	);
	return (ret);
}
#define atomic_swap_ptr(_p, _v) _atomic_swap_ptr((_p), (_v))

/*
 * Increment returning the new value
 * *p += 1
 * return (*p)
 */
#define _def_atomic_inc_nv(_f, _t)				\
static inline _t						\
_f(volatile _t *p)						\
{								\
	_t ret, modified;					\
								\
	__asm volatile (					\
	   "1:	ldrex %0, [%2]		\n\t"			\
	    "	add %0, %0, #1		\n\t"			\
	    "	strex %1, %0, [%2]	\n\t"			\
	    "	cmp %1, #0		\n\t"			\
	    "	bne 1b			\n\t"			\
	    : "=&r" (ret), "=&r" (modified)			\
	    : "r" (p)						\
	    : "memory", "cc"					\
	);							\
	return (ret);						\
}
_def_atomic_inc_nv(_atomic_inc_int_nv, unsigned int)
_def_atomic_inc_nv(_atomic_inc_long_nv, unsigned long)
#undef _def_atomic_inc_nv

#define atomic_inc_int_nv(_p) _atomic_inc_int_nv((_p))
#define atomic_inc_long_nv(_p) _atomic_inc_long_nv((_p))

/*
 * Decrement returning the new value
 * *p -= 1
 * return (*p)
 */
#define _def_atomic_dec_nv(_f, _t)				\
static inline _t						\
_f(volatile _t *p)						\
{								\
	_t ret, modified;					\
								\
	__asm volatile (					\
	    "1:	ldrex %0, [%2]		\n\t"			\
	    "	sub %0, %0, #1		\n\t"			\
	    "	strex %1, %0, [%2]	\n\t"			\
	    "	cmp %1, #0		\n\t"			\
	    "	bne 1b			\n\t"			\
	    : "=&r" (ret), "=&r" (modified)			\
	    : "r" (p)						\
	    : "memory", "cc"					\
	);							\
	return (ret);						\
}
_def_atomic_dec_nv(_atomic_dec_int_nv, unsigned int)
_def_atomic_dec_nv(_atomic_dec_long_nv, unsigned long)
#undef _def_atomic_dec_nv

#define atomic_dec_int_nv(_p) _atomic_dec_int_nv((_p))
#define atomic_dec_long_nv(_p) _atomic_dec_long_nv((_p))

/*
 * Addition returning the new value
 * *p += v
 * return (*p)
 */
#define _def_atomic_add_nv(_f, _t)				\
static inline _t						\
_f(volatile _t *p, _t v)					\
{								\
	_t ret, modified;					\
								\
	__asm volatile (					\
	    "1:	ldrex %0, [%2]		\n\t"			\
	    "	add %0, %0, %3		\n\t"			\
	    "	strex %1, %0, [%2]	\n\t"			\
	    "	cmp %1, #0		\n\t"			\
	    "	bne 1b			\n\t"			\
	    : "=&r" (ret), "=&r" (modified)			\
	    : "r" (p), "r" (v)					\
	    : "memory", "cc"					\
	);							\
	return (ret);						\
}
_def_atomic_add_nv(_atomic_add_int_nv, unsigned int)
_def_atomic_add_nv(_atomic_add_long_nv, unsigned long)
#undef _def_atomic_add_nv

#define atomic_add_int_nv(_p, _v) _atomic_add_int_nv((_p), (_v))
#define atomic_add_long_nv(_p, _v) _atomic_add_long_nv((_p), (_v))

/*
 * Subtraction returning the new value
 * *p -= v
 * return (*p)
 */
#define _def_atomic_sub_nv(_f, _t)				\
static inline _t						\
_f(volatile _t *p, _t v)					\
{								\
	_t ret, modified;					\
								\
	__asm volatile (					\
	    "1:	ldrex %0, [%2]		\n\t"			\
	    "	sub %0, %0, %3		\n\t"			\
	    "	strex %1, %0, [%2]	\n\t"			\
	    "	cmp %1, #0		\n\t"			\
	    "	bne 1b			\n\t"			\
	    : "=&r" (ret), "=&r" (modified)			\
	    : "r" (p), "r" (v)					\
	    : "memory", "cc"					\
	);							\
	return (ret);						\
}
_def_atomic_sub_nv(_atomic_sub_int_nv, unsigned int)
_def_atomic_sub_nv(_atomic_sub_long_nv, unsigned long)
#undef _def_atomic_sub_nv

#define atomic_sub_int_nv(_p, _v) _atomic_sub_int_nv((_p), (_v))
#define atomic_sub_long_nv(_p, _v) _atomic_sub_long_nv((_p), (_v))

#define __membar(_f) do { __asm volatile(_f ::: "memory"); } while (0)

#define membar_enter()		__membar("dmb sy")
#define membar_exit()		__membar("dmb sy")
#define membar_producer()	__membar("dmb st")
#define membar_consumer()	__membar("dmb sy")
#define membar_sync()		__membar("dmb sy")

#if defined(_KERNEL)

/* virtio needs MP membars even on SP kernels */
#define virtio_membar_producer()	__membar("dmb st")
#define virtio_membar_consumer()	__membar("dmb sy")
#define virtio_membar_sync()		__membar("dmb sy")

/*
 * Set bits
 * *p = *p | v
 */
static inline void
atomic_setbits_int(volatile unsigned int *p, unsigned int v)
{
	unsigned int modified, tmp;

	__asm volatile (
	    "1:	ldrex %0, [%3]		\n\t"
	    "	orr %0, %0, %2		\n\t"
	    "	strex %1, %0, [%3]	\n\t"
	    "	cmp %1, #0		\n\t"
	    "	bne 1b			\n\t"
	    : "=&r" (tmp), "=&r" (modified)
	    : "r" (v), "r" (p)
	    : "memory", "cc"
	);
}

/*
 * Clear bits
 * *p = *p & (~v)
 */
static inline void
atomic_clearbits_int(volatile unsigned int *p, unsigned int v)
{
	unsigned int modified, tmp;

	__asm volatile (
	    "1:	ldrex %0, [%3]		\n\t"
	    "	bic %0, %0, %2		\n\t"
	    "	strex %1, %0, [%3]	\n\t"
	    "	cmp %1, #0		\n\t"
	    "	bne 1b			\n\t"
	    : "=&r" (tmp), "=&r" (modified)
	    : "r" (v), "r" (p)
	    : "memory", "cc"
	);
}

#endif /* defined(_KERNEL) */
#endif /* _ARM_ATOMIC_H_ */
