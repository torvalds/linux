#ifndef _ASM_GENERIC_ATOMIC_LONG_H
#define _ASM_GENERIC_ATOMIC_LONG_H
/*
 * Copyright (C) 2005 Silicon Graphics, Inc.
 *	Christoph Lameter
 *
 * Allows to provide arch independent atomic definitions without the need to
 * edit all arch specific atomic.h files.
 */

#include <asm/types.h>

/*
 * Suppport for atomic_long_t
 *
 * Casts for parameters are avoided for existing atomic functions in order to
 * avoid issues with cast-as-lval under gcc 4.x and other limitations that the
 * macros of a platform may have.
 */

#if BITS_PER_LONG == 64

typedef atomic64_t atomic_long_t;

#define ATOMIC_LONG_INIT(i)	ATOMIC64_INIT(i)
#define ATOMIC_LONG_PFX(x)	atomic64 ## x

#else

typedef atomic_t atomic_long_t;

#define ATOMIC_LONG_INIT(i)	ATOMIC_INIT(i)
#define ATOMIC_LONG_PFX(x)	atomic ## x

#endif

static inline long atomic_long_read(atomic_long_t *l)
{
	ATOMIC_LONG_PFX(_t) *v = (ATOMIC_LONG_PFX(_t) *)l;

	return (long)ATOMIC_LONG_PFX(_read)(v);
}

static inline void atomic_long_set(atomic_long_t *l, long i)
{
	ATOMIC_LONG_PFX(_t) *v = (ATOMIC_LONG_PFX(_t) *)l;

	ATOMIC_LONG_PFX(_set)(v, i);
}

static inline void atomic_long_inc(atomic_long_t *l)
{
	ATOMIC_LONG_PFX(_t) *v = (ATOMIC_LONG_PFX(_t) *)l;

	ATOMIC_LONG_PFX(_inc)(v);
}

static inline void atomic_long_dec(atomic_long_t *l)
{
	ATOMIC_LONG_PFX(_t) *v = (ATOMIC_LONG_PFX(_t) *)l;

	ATOMIC_LONG_PFX(_dec)(v);
}

static inline void atomic_long_add(long i, atomic_long_t *l)
{
	ATOMIC_LONG_PFX(_t) *v = (ATOMIC_LONG_PFX(_t) *)l;

	ATOMIC_LONG_PFX(_add)(i, v);
}

static inline void atomic_long_sub(long i, atomic_long_t *l)
{
	ATOMIC_LONG_PFX(_t) *v = (ATOMIC_LONG_PFX(_t) *)l;

	ATOMIC_LONG_PFX(_sub)(i, v);
}

static inline int atomic_long_sub_and_test(long i, atomic_long_t *l)
{
	ATOMIC_LONG_PFX(_t) *v = (ATOMIC_LONG_PFX(_t) *)l;

	return ATOMIC_LONG_PFX(_sub_and_test)(i, v);
}

static inline int atomic_long_dec_and_test(atomic_long_t *l)
{
	ATOMIC_LONG_PFX(_t) *v = (ATOMIC_LONG_PFX(_t) *)l;

	return ATOMIC_LONG_PFX(_dec_and_test)(v);
}

static inline int atomic_long_inc_and_test(atomic_long_t *l)
{
	ATOMIC_LONG_PFX(_t) *v = (ATOMIC_LONG_PFX(_t) *)l;

	return ATOMIC_LONG_PFX(_inc_and_test)(v);
}

static inline int atomic_long_add_negative(long i, atomic_long_t *l)
{
	ATOMIC_LONG_PFX(_t) *v = (ATOMIC_LONG_PFX(_t) *)l;

	return ATOMIC_LONG_PFX(_add_negative)(i, v);
}

static inline long atomic_long_add_return(long i, atomic_long_t *l)
{
	ATOMIC_LONG_PFX(_t) *v = (ATOMIC_LONG_PFX(_t) *)l;

	return (long)ATOMIC_LONG_PFX(_add_return)(i, v);
}

static inline long atomic_long_sub_return(long i, atomic_long_t *l)
{
	ATOMIC_LONG_PFX(_t) *v = (ATOMIC_LONG_PFX(_t) *)l;

	return (long)ATOMIC_LONG_PFX(_sub_return)(i, v);
}

static inline long atomic_long_inc_return(atomic_long_t *l)
{
	ATOMIC_LONG_PFX(_t) *v = (ATOMIC_LONG_PFX(_t) *)l;

	return (long)ATOMIC_LONG_PFX(_inc_return)(v);
}

static inline long atomic_long_dec_return(atomic_long_t *l)
{
	ATOMIC_LONG_PFX(_t) *v = (ATOMIC_LONG_PFX(_t) *)l;

	return (long)ATOMIC_LONG_PFX(_dec_return)(v);
}

static inline long atomic_long_add_unless(atomic_long_t *l, long a, long u)
{
	ATOMIC_LONG_PFX(_t) *v = (ATOMIC_LONG_PFX(_t) *)l;

	return (long)ATOMIC_LONG_PFX(_add_unless)(v, a, u);
}

#define atomic_long_inc_not_zero(l) \
	ATOMIC_LONG_PFX(_inc_not_zero)((ATOMIC_LONG_PFX(_t) *)(l))
#define atomic_long_cmpxchg(l, old, new) \
	(ATOMIC_LONG_PFX(_cmpxchg)((ATOMIC_LONG_PFX(_t) *)(l), (old), (new)))
#define atomic_long_xchg(v, new) \
	(ATOMIC_LONG_PFX(_xchg)((ATOMIC_LONG_PFX(_t) *)(v), (new)))

#endif  /*  _ASM_GENERIC_ATOMIC_LONG_H  */
