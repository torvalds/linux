#ifndef _ALPHA_ATOMIC_H
#define _ALPHA_ATOMIC_H

#include <asm/barrier.h>

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc...
 *
 * But use these as seldom as possible since they are much slower
 * than regular operations.
 */


/*
 * Counter is volatile to make sure gcc doesn't try to be clever
 * and move things around on us. We need to use _exactly_ the address
 * the user gave us, not some alias that contains the same information.
 */
typedef struct { volatile int counter; } atomic_t;
typedef struct { volatile long counter; } atomic64_t;

#define ATOMIC_INIT(i)		( (atomic_t) { (i) } )
#define ATOMIC64_INIT(i)	( (atomic64_t) { (i) } )

#define atomic_read(v)		((v)->counter + 0)
#define atomic64_read(v)	((v)->counter + 0)

#define atomic_set(v,i)		((v)->counter = (i))
#define atomic64_set(v,i)	((v)->counter = (i))

/*
 * To get proper branch prediction for the main line, we must branch
 * forward to code at the end of this object's .text section, then
 * branch back to restart the operation.
 */

static __inline__ void atomic_add(int i, atomic_t * v)
{
	unsigned long temp;
	__asm__ __volatile__(
	"1:	ldl_l %0,%1\n"
	"	addl %0,%2,%0\n"
	"	stl_c %0,%1\n"
	"	beq %0,2f\n"
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (v->counter)
	:"Ir" (i), "m" (v->counter));
}

static __inline__ void atomic64_add(long i, atomic64_t * v)
{
	unsigned long temp;
	__asm__ __volatile__(
	"1:	ldq_l %0,%1\n"
	"	addq %0,%2,%0\n"
	"	stq_c %0,%1\n"
	"	beq %0,2f\n"
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (v->counter)
	:"Ir" (i), "m" (v->counter));
}

static __inline__ void atomic_sub(int i, atomic_t * v)
{
	unsigned long temp;
	__asm__ __volatile__(
	"1:	ldl_l %0,%1\n"
	"	subl %0,%2,%0\n"
	"	stl_c %0,%1\n"
	"	beq %0,2f\n"
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (v->counter)
	:"Ir" (i), "m" (v->counter));
}

static __inline__ void atomic64_sub(long i, atomic64_t * v)
{
	unsigned long temp;
	__asm__ __volatile__(
	"1:	ldq_l %0,%1\n"
	"	subq %0,%2,%0\n"
	"	stq_c %0,%1\n"
	"	beq %0,2f\n"
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (v->counter)
	:"Ir" (i), "m" (v->counter));
}


/*
 * Same as above, but return the result value
 */
static __inline__ long atomic_add_return(int i, atomic_t * v)
{
	long temp, result;
	smp_mb();
	__asm__ __volatile__(
	"1:	ldl_l %0,%1\n"
	"	addl %0,%3,%2\n"
	"	addl %0,%3,%0\n"
	"	stl_c %0,%1\n"
	"	beq %0,2f\n"
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (v->counter), "=&r" (result)
	:"Ir" (i), "m" (v->counter) : "memory");
	smp_mb();
	return result;
}

static __inline__ long atomic64_add_return(long i, atomic64_t * v)
{
	long temp, result;
	smp_mb();
	__asm__ __volatile__(
	"1:	ldq_l %0,%1\n"
	"	addq %0,%3,%2\n"
	"	addq %0,%3,%0\n"
	"	stq_c %0,%1\n"
	"	beq %0,2f\n"
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (v->counter), "=&r" (result)
	:"Ir" (i), "m" (v->counter) : "memory");
	smp_mb();
	return result;
}

static __inline__ long atomic_sub_return(int i, atomic_t * v)
{
	long temp, result;
	smp_mb();
	__asm__ __volatile__(
	"1:	ldl_l %0,%1\n"
	"	subl %0,%3,%2\n"
	"	subl %0,%3,%0\n"
	"	stl_c %0,%1\n"
	"	beq %0,2f\n"
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (v->counter), "=&r" (result)
	:"Ir" (i), "m" (v->counter) : "memory");
	smp_mb();
	return result;
}

static __inline__ long atomic64_sub_return(long i, atomic64_t * v)
{
	long temp, result;
	smp_mb();
	__asm__ __volatile__(
	"1:	ldq_l %0,%1\n"
	"	subq %0,%3,%2\n"
	"	subq %0,%3,%0\n"
	"	stq_c %0,%1\n"
	"	beq %0,2f\n"
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (v->counter), "=&r" (result)
	:"Ir" (i), "m" (v->counter) : "memory");
	smp_mb();
	return result;
}

#define atomic64_cmpxchg(v, old, new) (cmpxchg(&((v)->counter), old, new))
#define atomic64_xchg(v, new) (xchg(&((v)->counter), new))

#define atomic_cmpxchg(v, old, new) (cmpxchg(&((v)->counter), old, new))
#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

/**
 * atomic_add_unless - add unless the number is a given value
 * @v: pointer of type atomic_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as it was not @u.
 * Returns non-zero if @v was not @u, and zero otherwise.
 */
#define atomic_add_unless(v, a, u)				\
({								\
	__typeof__((v)->counter) c, old;			\
	c = atomic_read(v);					\
	for (;;) {						\
		if (unlikely(c == (u)))				\
			break;					\
		old = atomic_cmpxchg((v), c, c + (a));		\
		if (likely(old == c))				\
			break;					\
		c = old;					\
	}							\
	c != (u);						\
})
#define atomic_inc_not_zero(v) atomic_add_unless((v), 1, 0)

/**
 * atomic64_add_unless - add unless the number is a given value
 * @v: pointer of type atomic64_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as it was not @u.
 * Returns non-zero if @v was not @u, and zero otherwise.
 */
#define atomic64_add_unless(v, a, u)				\
({								\
	__typeof__((v)->counter) c, old;			\
	c = atomic64_read(v);					\
	for (;;) {						\
		if (unlikely(c == (u)))				\
			break;					\
		old = atomic64_cmpxchg((v), c, c + (a));	\
		if (likely(old == c))				\
			break;					\
		c = old;					\
	}							\
	c != (u);						\
})
#define atomic64_inc_not_zero(v) atomic64_add_unless((v), 1, 0)

#define atomic_add_negative(a, v) (atomic_add_return((a), (v)) < 0)
#define atomic64_add_negative(a, v) (atomic64_add_return((a), (v)) < 0)

#define atomic_dec_return(v) atomic_sub_return(1,(v))
#define atomic64_dec_return(v) atomic64_sub_return(1,(v))

#define atomic_inc_return(v) atomic_add_return(1,(v))
#define atomic64_inc_return(v) atomic64_add_return(1,(v))

#define atomic_sub_and_test(i,v) (atomic_sub_return((i), (v)) == 0)
#define atomic64_sub_and_test(i,v) (atomic64_sub_return((i), (v)) == 0)

#define atomic_inc_and_test(v) (atomic_add_return(1, (v)) == 0)
#define atomic64_inc_and_test(v) (atomic64_add_return(1, (v)) == 0)

#define atomic_dec_and_test(v) (atomic_sub_return(1, (v)) == 0)
#define atomic64_dec_and_test(v) (atomic64_sub_return(1, (v)) == 0)

#define atomic_inc(v) atomic_add(1,(v))
#define atomic64_inc(v) atomic64_add(1,(v))

#define atomic_dec(v) atomic_sub(1,(v))
#define atomic64_dec(v) atomic64_sub(1,(v))

#define smp_mb__before_atomic_dec()	smp_mb()
#define smp_mb__after_atomic_dec()	smp_mb()
#define smp_mb__before_atomic_inc()	smp_mb()
#define smp_mb__after_atomic_inc()	smp_mb()

#include <asm-generic/atomic.h>
#endif /* _ALPHA_ATOMIC_H */
