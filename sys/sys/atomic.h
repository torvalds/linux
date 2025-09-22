/*	$OpenBSD: atomic.h,v 1.10 2025/01/03 21:27:41 bluhm Exp $ */
/*
 * Copyright (c) 2014 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2022 Alexander Bluhm <bluhm@openbsd.org>
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

#ifndef _SYS_ATOMIC_H_
#define _SYS_ATOMIC_H_

#include <machine/atomic.h>

/*
 * an arch wanting to provide its own implementations does so by defining
 * macros.
 */

/*
 * atomic_cas_*
 */

#ifndef atomic_cas_uint
static inline unsigned int
atomic_cas_uint(volatile unsigned int *p, unsigned int o, unsigned int n)
{
	return __sync_val_compare_and_swap(p, o, n);
}
#endif

#ifndef atomic_cas_ulong
static inline unsigned long
atomic_cas_ulong(volatile unsigned long *p, unsigned long o, unsigned long n)
{
	return __sync_val_compare_and_swap(p, o, n);
}
#endif

#ifndef atomic_cas_ptr
static inline void *
atomic_cas_ptr(volatile void *pp, void *o, void *n)
{
	void * volatile *p = (void * volatile *)pp;
	return __sync_val_compare_and_swap(p, o, n);
}
#endif

/*
 * atomic_swap_*
 */

#ifndef atomic_swap_uint
static inline unsigned int
atomic_swap_uint(volatile unsigned int *p, unsigned int v)
{
	return __sync_lock_test_and_set(p, v);
}
#endif

#ifndef atomic_swap_ulong
static inline unsigned long
atomic_swap_ulong(volatile unsigned long *p, unsigned long v)
{
	return __sync_lock_test_and_set(p, v);
}
#endif

#ifndef atomic_swap_ptr
static inline void *
atomic_swap_ptr(volatile void *pp, void *v)
{
	void * volatile *p = (void * volatile *)pp;
	return __sync_lock_test_and_set(p, v);
}
#endif

/*
 * atomic_add_*_nv - add and fetch
 */

#ifndef atomic_add_int_nv
static inline unsigned int
atomic_add_int_nv(volatile unsigned int *p, unsigned int v)
{
	return __sync_add_and_fetch(p, v);
}
#endif

#ifndef atomic_add_long_nv
static inline unsigned long
atomic_add_long_nv(volatile unsigned long *p, unsigned long v)
{
	return __sync_add_and_fetch(p, v);
}
#endif

/*
 * atomic_add - add
 */

#ifndef atomic_add_int
#define atomic_add_int(_p, _v) ((void)atomic_add_int_nv((_p), (_v)))
#endif

#ifndef atomic_add_long
#define atomic_add_long(_p, _v) ((void)atomic_add_long_nv((_p), (_v)))
#endif

/*
 * atomic_inc_*_nv - increment and fetch
 */

#ifndef atomic_inc_int_nv
#define atomic_inc_int_nv(_p) atomic_add_int_nv((_p), 1)
#endif

#ifndef atomic_inc_long_nv
#define atomic_inc_long_nv(_p) atomic_add_long_nv((_p), 1)
#endif

/*
 * atomic_inc_* - increment
 */

#ifndef atomic_inc_int
#define atomic_inc_int(_p) ((void)atomic_inc_int_nv(_p))
#endif

#ifndef atomic_inc_long
#define atomic_inc_long(_p) ((void)atomic_inc_long_nv(_p))
#endif

/*
 * atomic_sub_*_nv - sub and fetch
 */

#ifndef atomic_sub_int_nv
static inline unsigned int
atomic_sub_int_nv(volatile unsigned int *p, unsigned int v)
{
	return __sync_sub_and_fetch(p, v);
}
#endif

#ifndef atomic_sub_long_nv
static inline unsigned long
atomic_sub_long_nv(volatile unsigned long *p, unsigned long v)
{
	return __sync_sub_and_fetch(p, v);
}
#endif

/*
 * atomic_sub_* - sub
 */

#ifndef atomic_sub_int
#define atomic_sub_int(_p, _v) ((void)atomic_sub_int_nv((_p), (_v)))
#endif

#ifndef atomic_sub_long
#define atomic_sub_long(_p, _v) ((void)atomic_sub_long_nv((_p), (_v)))
#endif

/*
 * atomic_dec_*_nv - decrement and fetch
 */

#ifndef atomic_dec_int_nv
#define atomic_dec_int_nv(_p) atomic_sub_int_nv((_p), 1)
#endif

#ifndef atomic_dec_long_nv
#define atomic_dec_long_nv(_p) atomic_sub_long_nv((_p), 1)
#endif

/*
 * atomic_dec_* - decrement
 */

#ifndef atomic_dec_int
#define atomic_dec_int(_p) ((void)atomic_dec_int_nv(_p))
#endif

#ifndef atomic_dec_long
#define atomic_dec_long(_p) ((void)atomic_dec_long_nv(_p))
#endif

#ifdef _KERNEL
/*
 * atomic_load_* - read from memory
 */

static inline unsigned int
atomic_load_int(volatile const unsigned int *p)
{
	return *p;
}

static inline unsigned long
atomic_load_long(volatile const unsigned long *p)
{
	return *p;
}

/*
 * atomic_store_* - write to memory
 */

static inline void
atomic_store_int(volatile unsigned int *p, unsigned int v)
{
	*p = v;
}

static inline void
atomic_store_long(volatile unsigned long *p, unsigned long v)
{
	*p = v;
}
#endif /* _KERNEL */

/*
 * memory barriers
 */

#ifndef membar_enter
#define membar_enter() __sync_synchronize()
#endif

#ifndef membar_exit
#define membar_exit() __sync_synchronize()
#endif

#ifndef membar_producer
#define membar_producer() __sync_synchronize()
#endif

#ifndef membar_consumer
#define membar_consumer() __sync_synchronize()
#endif

#ifndef membar_sync
#define membar_sync() __sync_synchronize()
#endif

#ifndef membar_enter_after_atomic
#define membar_enter_after_atomic() membar_enter()
#endif

#ifndef membar_exit_before_atomic
#define membar_exit_before_atomic() membar_exit()
#endif

#ifdef _KERNEL

/*
 * Force any preceding reads to happen before any subsequent reads that
 * depend on the value returned by the preceding reads.
 */
static inline void
membar_datadep_consumer(void)
{
#ifdef __alpha__
	membar_consumer();
#endif
}

#define READ_ONCE(x) ({							\
	typeof(x) __tmp = *(volatile typeof(x) *)&(x);			\
	membar_datadep_consumer();					\
	__tmp;								\
})

#define WRITE_ONCE(x, val) ({						\
	typeof(x) __tmp = (val);					\
	*(volatile typeof(x) *)&(x) = __tmp;				\
	__tmp;								\
})

#endif /* _KERNEL */

#endif /* _SYS_ATOMIC_H_ */
