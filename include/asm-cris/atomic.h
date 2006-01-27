/* $Id: atomic.h,v 1.3 2001/07/25 16:15:19 bjornw Exp $ */

#ifndef __ASM_CRIS_ATOMIC__
#define __ASM_CRIS_ATOMIC__

#include <asm/system.h>
#include <asm/arch/atomic.h>

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

typedef struct { volatile int counter; } atomic_t;

#define ATOMIC_INIT(i)  { (i) }

#define atomic_read(v) ((v)->counter)
#define atomic_set(v,i) (((v)->counter) = (i))

/* These should be written in asm but we do it in C for now. */

static inline void atomic_add(int i, volatile atomic_t *v)
{
	unsigned long flags;
	cris_atomic_save(v, flags);
	v->counter += i;
	cris_atomic_restore(v, flags);
}

static inline void atomic_sub(int i, volatile atomic_t *v)
{
	unsigned long flags;
	cris_atomic_save(v, flags);
	v->counter -= i;
	cris_atomic_restore(v, flags);
}

static inline int atomic_add_return(int i, volatile atomic_t *v)
{
	unsigned long flags;
	int retval;
	cris_atomic_save(v, flags);
	retval = (v->counter += i);
	cris_atomic_restore(v, flags);
	return retval;
}

#define atomic_add_negative(a, v)	(atomic_add_return((a), (v)) < 0)

static inline int atomic_sub_return(int i, volatile atomic_t *v)
{
	unsigned long flags;
	int retval;
	cris_atomic_save(v, flags);
	retval = (v->counter -= i);
	cris_atomic_restore(v, flags);
	return retval;
}

static inline int atomic_sub_and_test(int i, volatile atomic_t *v)
{
	int retval;
	unsigned long flags;
	cris_atomic_save(v, flags);
	retval = (v->counter -= i) == 0;
	cris_atomic_restore(v, flags);
	return retval;
}

static inline void atomic_inc(volatile atomic_t *v)
{
	unsigned long flags;
	cris_atomic_save(v, flags);
	(v->counter)++;
	cris_atomic_restore(v, flags);
}

static inline void atomic_dec(volatile atomic_t *v)
{
	unsigned long flags;
	cris_atomic_save(v, flags);
	(v->counter)--;
	cris_atomic_restore(v, flags);
}

static inline int atomic_inc_return(volatile atomic_t *v)
{
	unsigned long flags;
	int retval;
	cris_atomic_save(v, flags);
	retval = (v->counter)++;
	cris_atomic_restore(v, flags);
	return retval;
}

static inline int atomic_dec_return(volatile atomic_t *v)
{
	unsigned long flags;
	int retval;
	cris_atomic_save(v, flags);
	retval = (v->counter)--;
	cris_atomic_restore(v, flags);
	return retval;
}
static inline int atomic_dec_and_test(volatile atomic_t *v)
{
	int retval;
	unsigned long flags;
	cris_atomic_save(v, flags);
	retval = --(v->counter) == 0;
	cris_atomic_restore(v, flags);
	return retval;
}

static inline int atomic_inc_and_test(volatile atomic_t *v)
{
	int retval;
	unsigned long flags;
	cris_atomic_save(v, flags);
	retval = ++(v->counter) == 0;
	cris_atomic_restore(v, flags);
	return retval;
}

static inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	int ret;
	unsigned long flags;

	cris_atomic_save(v, flags);
	ret = v->counter;
	if (likely(ret == old))
		v->counter = new;
	cris_atomic_restore(v, flags);
	return ret;
}

#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

static inline int atomic_add_unless(atomic_t *v, int a, int u)
{
	int ret;
	unsigned long flags;

	cris_atomic_save(v, flags);
	ret = v->counter;
	if (ret != u)
		v->counter += a;
	cris_atomic_restore(v, flags);
	return ret != u;
}
#define atomic_inc_not_zero(v) atomic_add_unless((v), 1, 0)

/* Atomic operations are already serializing */
#define smp_mb__before_atomic_dec()    barrier()
#define smp_mb__after_atomic_dec()     barrier()
#define smp_mb__before_atomic_inc()    barrier()
#define smp_mb__after_atomic_inc()     barrier()

#include <asm-generic/atomic.h>
#endif
