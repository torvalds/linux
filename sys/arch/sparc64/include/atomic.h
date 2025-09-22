/*	$OpenBSD: atomic.h,v 1.15 2017/07/04 09:00:12 mpi Exp $	*/
/*
 * Copyright (c) 2007 Artur Grabowski <art@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _MACHINE_ATOMIC_H_
#define _MACHINE_ATOMIC_H_

static inline unsigned int
_atomic_cas_uint(volatile unsigned int *p, unsigned int e, unsigned int n)
{
	__asm volatile("cas [%2], %3, %0"
	    : "+r" (n), "=m" (*p)
	    : "r" (p), "r" (e), "m" (*p));

	return (n);
}
#define atomic_cas_uint(_p, _e, _n) _atomic_cas_uint((_p), (_e), (_n))

static inline unsigned long
_atomic_cas_ulong(volatile unsigned long *p, unsigned long e, unsigned long n)
{
	__asm volatile("casx [%2], %3, %0"
	    : "+r" (n), "=m" (*p)
	    : "r" (p), "r" (e), "m" (*p));

	return (n);
}
#define atomic_cas_ulong(_p, _e, _n) _atomic_cas_ulong((_p), (_e), (_n))

static inline void *
_atomic_cas_ptr(volatile void *p, void *e, void *n)
{
	__asm volatile("casx [%2], %3, %0"
	    : "+r" (n), "=m" (*(volatile unsigned long *)p)
	    : "r" (p), "r" (e), "m" (*(volatile unsigned long *)p));

	return (n);
}
#define atomic_cas_ptr(_p, _e, _n) _atomic_cas_ptr((_p), (_e), (_n))

#define _def_atomic_swap(_f, _t, _c)					\
static inline _t							\
_f(volatile _t *p, _t v)						\
{									\
	_t e;								\
	_t r;								\
									\
	r = (_t)*p;							\
	do {								\
		e = r;							\
		r = _c(p, e, v);					\
	} while (r != e);						\
									\
	return (r);							\
}

_def_atomic_swap(_atomic_swap_uint, unsigned int, atomic_cas_uint)
_def_atomic_swap(_atomic_swap_ulong, unsigned long, atomic_cas_ulong)
#undef _def_atomic_swap

static inline void *
_atomic_swap_ptr(volatile void *p, void *v)
{
	void *e, *r;

	r = *(void **)p;
	do {
		e = r;
		r = atomic_cas_ptr(p, e, v);
	} while (r != e);

	return (r);
}

#define atomic_swap_uint(_p, _v)  _atomic_swap_uint(_p, _v)
#define atomic_swap_ulong(_p, _v)  _atomic_swap_ulong(_p, _v)
#define atomic_swap_ptr(_p, _v)  _atomic_swap_ptr(_p, _v)

#define _def_atomic_op_nv(_f, _t, _c, _op)				\
static inline _t							\
_f(volatile _t *p, _t v)						\
{									\
	_t e, r, f;							\
									\
	r = *p;								\
	do {								\
		e = r;							\
		f = e _op v;						\
		r = _c(p, e, f);					\
	} while (r != e);						\
									\
	return (f);							\
}

_def_atomic_op_nv(_atomic_add_int_nv, unsigned int, atomic_cas_uint, +)
_def_atomic_op_nv(_atomic_add_long_nv, unsigned long, atomic_cas_ulong, +)
_def_atomic_op_nv(_atomic_sub_int_nv, unsigned int, atomic_cas_uint, -)
_def_atomic_op_nv(_atomic_sub_long_nv, unsigned long, atomic_cas_ulong, -)
#undef _def_atomic_op_nv

#define atomic_add_int_nv(_p, _v)  _atomic_add_int_nv(_p, _v)
#define atomic_add_long_nv(_p, _v)  _atomic_add_long_nv(_p, _v)
#define atomic_sub_int_nv(_p, _v)  _atomic_sub_int_nv(_p, _v)
#define atomic_sub_long_nv(_p, _v)  _atomic_sub_long_nv(_p, _v)

#define __membar(_m)		__asm volatile("membar " _m ::: "memory")

#define membar_enter()		__membar("#StoreLoad|#StoreStore")
#define membar_exit()		__membar("#LoadStore|#StoreStore")
#define membar_producer()	__membar("#StoreStore")
#define membar_consumer()	__membar("#LoadLoad")
#define membar_sync()		__membar("#Sync")

#if defined(_KERNEL)

static __inline void
atomic_setbits_int(volatile unsigned int *uip, unsigned int v)
{
	unsigned int e, r;

	r = *uip;
	do {
		e = r;
		r = atomic_cas_uint(uip, e, e | v);
	} while (r != e);
}

static __inline void
atomic_clearbits_int(volatile unsigned int *uip, unsigned int v)
{
	unsigned int e, r;

	r = *uip;
	do {
		e = r;
		r = atomic_cas_uint(uip, e, e & ~v);
	} while (r != e);
}

#endif /* defined(_KERNEL) */
#endif /* _MACHINE_ATOMIC_H_ */
