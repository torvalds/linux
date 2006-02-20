#ifndef _ASM_POWERPC_ATOMIC_H_
#define _ASM_POWERPC_ATOMIC_H_

/*
 * PowerPC atomic operations
 */

typedef struct { volatile int counter; } atomic_t;

#ifdef __KERNEL__
#include <linux/compiler.h>
#include <asm/synch.h>
#include <asm/asm-compat.h>

#define ATOMIC_INIT(i)		{ (i) }

#define atomic_read(v)		((v)->counter)
#define atomic_set(v,i)		(((v)->counter) = (i))

static __inline__ void atomic_add(int a, atomic_t *v)
{
	int t;

	__asm__ __volatile__(
"1:	lwarx	%0,0,%3		# atomic_add\n\
	add	%0,%2,%0\n"
	PPC405_ERR77(0,%3)
"	stwcx.	%0,0,%3 \n\
	bne-	1b"
	: "=&r" (t), "=m" (v->counter)
	: "r" (a), "r" (&v->counter), "m" (v->counter)
	: "cc");
}

static __inline__ int atomic_add_return(int a, atomic_t *v)
{
	int t;

	__asm__ __volatile__(
	LWSYNC_ON_SMP
"1:	lwarx	%0,0,%2		# atomic_add_return\n\
	add	%0,%1,%0\n"
	PPC405_ERR77(0,%2)
"	stwcx.	%0,0,%2 \n\
	bne-	1b"
	ISYNC_ON_SMP
	: "=&r" (t)
	: "r" (a), "r" (&v->counter)
	: "cc", "memory");

	return t;
}

#define atomic_add_negative(a, v)	(atomic_add_return((a), (v)) < 0)

static __inline__ void atomic_sub(int a, atomic_t *v)
{
	int t;

	__asm__ __volatile__(
"1:	lwarx	%0,0,%3		# atomic_sub\n\
	subf	%0,%2,%0\n"
	PPC405_ERR77(0,%3)
"	stwcx.	%0,0,%3 \n\
	bne-	1b"
	: "=&r" (t), "=m" (v->counter)
	: "r" (a), "r" (&v->counter), "m" (v->counter)
	: "cc");
}

static __inline__ int atomic_sub_return(int a, atomic_t *v)
{
	int t;

	__asm__ __volatile__(
	LWSYNC_ON_SMP
"1:	lwarx	%0,0,%2		# atomic_sub_return\n\
	subf	%0,%1,%0\n"
	PPC405_ERR77(0,%2)
"	stwcx.	%0,0,%2 \n\
	bne-	1b"
	ISYNC_ON_SMP
	: "=&r" (t)
	: "r" (a), "r" (&v->counter)
	: "cc", "memory");

	return t;
}

static __inline__ void atomic_inc(atomic_t *v)
{
	int t;

	__asm__ __volatile__(
"1:	lwarx	%0,0,%2		# atomic_inc\n\
	addic	%0,%0,1\n"
	PPC405_ERR77(0,%2)
"	stwcx.	%0,0,%2 \n\
	bne-	1b"
	: "=&r" (t), "=m" (v->counter)
	: "r" (&v->counter), "m" (v->counter)
	: "cc");
}

static __inline__ int atomic_inc_return(atomic_t *v)
{
	int t;

	__asm__ __volatile__(
	LWSYNC_ON_SMP
"1:	lwarx	%0,0,%1		# atomic_inc_return\n\
	addic	%0,%0,1\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%0,0,%1 \n\
	bne-	1b"
	ISYNC_ON_SMP
	: "=&r" (t)
	: "r" (&v->counter)
	: "cc", "memory");

	return t;
}

/*
 * atomic_inc_and_test - increment and test
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define atomic_inc_and_test(v) (atomic_inc_return(v) == 0)

static __inline__ void atomic_dec(atomic_t *v)
{
	int t;

	__asm__ __volatile__(
"1:	lwarx	%0,0,%2		# atomic_dec\n\
	addic	%0,%0,-1\n"
	PPC405_ERR77(0,%2)\
"	stwcx.	%0,0,%2\n\
	bne-	1b"
	: "=&r" (t), "=m" (v->counter)
	: "r" (&v->counter), "m" (v->counter)
	: "cc");
}

static __inline__ int atomic_dec_return(atomic_t *v)
{
	int t;

	__asm__ __volatile__(
	LWSYNC_ON_SMP
"1:	lwarx	%0,0,%1		# atomic_dec_return\n\
	addic	%0,%0,-1\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%0,0,%1\n\
	bne-	1b"
	ISYNC_ON_SMP
	: "=&r" (t)
	: "r" (&v->counter)
	: "cc", "memory");

	return t;
}

#define atomic_cmpxchg(v, o, n) ((int)cmpxchg(&((v)->counter), (o), (n)))
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
static __inline__ int atomic_add_unless(atomic_t *v, int a, int u)
{
	int t;

	__asm__ __volatile__ (
	LWSYNC_ON_SMP
"1:	lwarx	%0,0,%1		# atomic_add_unless\n\
	cmpw	0,%0,%3 \n\
	beq-	2f \n\
	add	%0,%2,%0 \n"
	PPC405_ERR77(0,%2)
"	stwcx.	%0,0,%1 \n\
	bne-	1b \n"
	ISYNC_ON_SMP
"	subf	%0,%2,%0 \n\
2:"
	: "=&r" (t)
	: "r" (&v->counter), "r" (a), "r" (u)
	: "cc", "memory");

	return t != u;
}

#define atomic_inc_not_zero(v) atomic_add_unless((v), 1, 0)

#define atomic_sub_and_test(a, v)	(atomic_sub_return((a), (v)) == 0)
#define atomic_dec_and_test(v)		(atomic_dec_return((v)) == 0)

/*
 * Atomically test *v and decrement if it is greater than 0.
 * The function returns the old value of *v minus 1.
 */
static __inline__ int atomic_dec_if_positive(atomic_t *v)
{
	int t;

	__asm__ __volatile__(
	LWSYNC_ON_SMP
"1:	lwarx	%0,0,%1		# atomic_dec_if_positive\n\
	addic.	%0,%0,-1\n\
	blt-	2f\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%0,0,%1\n\
	bne-	1b"
	ISYNC_ON_SMP
	"\n\
2:"	: "=&r" (t)
	: "r" (&v->counter)
	: "cc", "memory");

	return t;
}

#define smp_mb__before_atomic_dec()     smp_mb()
#define smp_mb__after_atomic_dec()      smp_mb()
#define smp_mb__before_atomic_inc()     smp_mb()
#define smp_mb__after_atomic_inc()      smp_mb()

#ifdef __powerpc64__

typedef struct { volatile long counter; } atomic64_t;

#define ATOMIC64_INIT(i)	{ (i) }

#define atomic64_read(v)	((v)->counter)
#define atomic64_set(v,i)	(((v)->counter) = (i))

static __inline__ void atomic64_add(long a, atomic64_t *v)
{
	long t;

	__asm__ __volatile__(
"1:	ldarx	%0,0,%3		# atomic64_add\n\
	add	%0,%2,%0\n\
	stdcx.	%0,0,%3 \n\
	bne-	1b"
	: "=&r" (t), "=m" (v->counter)
	: "r" (a), "r" (&v->counter), "m" (v->counter)
	: "cc");
}

static __inline__ long atomic64_add_return(long a, atomic64_t *v)
{
	long t;

	__asm__ __volatile__(
	LWSYNC_ON_SMP
"1:	ldarx	%0,0,%2		# atomic64_add_return\n\
	add	%0,%1,%0\n\
	stdcx.	%0,0,%2 \n\
	bne-	1b"
	ISYNC_ON_SMP
	: "=&r" (t)
	: "r" (a), "r" (&v->counter)
	: "cc", "memory");

	return t;
}

#define atomic64_add_negative(a, v)	(atomic64_add_return((a), (v)) < 0)

static __inline__ void atomic64_sub(long a, atomic64_t *v)
{
	long t;

	__asm__ __volatile__(
"1:	ldarx	%0,0,%3		# atomic64_sub\n\
	subf	%0,%2,%0\n\
	stdcx.	%0,0,%3 \n\
	bne-	1b"
	: "=&r" (t), "=m" (v->counter)
	: "r" (a), "r" (&v->counter), "m" (v->counter)
	: "cc");
}

static __inline__ long atomic64_sub_return(long a, atomic64_t *v)
{
	long t;

	__asm__ __volatile__(
	LWSYNC_ON_SMP
"1:	ldarx	%0,0,%2		# atomic64_sub_return\n\
	subf	%0,%1,%0\n\
	stdcx.	%0,0,%2 \n\
	bne-	1b"
	ISYNC_ON_SMP
	: "=&r" (t)
	: "r" (a), "r" (&v->counter)
	: "cc", "memory");

	return t;
}

static __inline__ void atomic64_inc(atomic64_t *v)
{
	long t;

	__asm__ __volatile__(
"1:	ldarx	%0,0,%2		# atomic64_inc\n\
	addic	%0,%0,1\n\
	stdcx.	%0,0,%2 \n\
	bne-	1b"
	: "=&r" (t), "=m" (v->counter)
	: "r" (&v->counter), "m" (v->counter)
	: "cc");
}

static __inline__ long atomic64_inc_return(atomic64_t *v)
{
	long t;

	__asm__ __volatile__(
	LWSYNC_ON_SMP
"1:	ldarx	%0,0,%1		# atomic64_inc_return\n\
	addic	%0,%0,1\n\
	stdcx.	%0,0,%1 \n\
	bne-	1b"
	ISYNC_ON_SMP
	: "=&r" (t)
	: "r" (&v->counter)
	: "cc", "memory");

	return t;
}

/*
 * atomic64_inc_and_test - increment and test
 * @v: pointer of type atomic64_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define atomic64_inc_and_test(v) (atomic64_inc_return(v) == 0)

static __inline__ void atomic64_dec(atomic64_t *v)
{
	long t;

	__asm__ __volatile__(
"1:	ldarx	%0,0,%2		# atomic64_dec\n\
	addic	%0,%0,-1\n\
	stdcx.	%0,0,%2\n\
	bne-	1b"
	: "=&r" (t), "=m" (v->counter)
	: "r" (&v->counter), "m" (v->counter)
	: "cc");
}

static __inline__ long atomic64_dec_return(atomic64_t *v)
{
	long t;

	__asm__ __volatile__(
	LWSYNC_ON_SMP
"1:	ldarx	%0,0,%1		# atomic64_dec_return\n\
	addic	%0,%0,-1\n\
	stdcx.	%0,0,%1\n\
	bne-	1b"
	ISYNC_ON_SMP
	: "=&r" (t)
	: "r" (&v->counter)
	: "cc", "memory");

	return t;
}

#define atomic64_sub_and_test(a, v)	(atomic64_sub_return((a), (v)) == 0)
#define atomic64_dec_and_test(v)	(atomic64_dec_return((v)) == 0)

/*
 * Atomically test *v and decrement if it is greater than 0.
 * The function returns the old value of *v minus 1.
 */
static __inline__ long atomic64_dec_if_positive(atomic64_t *v)
{
	long t;

	__asm__ __volatile__(
	LWSYNC_ON_SMP
"1:	ldarx	%0,0,%1		# atomic64_dec_if_positive\n\
	addic.	%0,%0,-1\n\
	blt-	2f\n\
	stdcx.	%0,0,%1\n\
	bne-	1b"
	ISYNC_ON_SMP
	"\n\
2:"	: "=&r" (t)
	: "r" (&v->counter)
	: "cc", "memory");

	return t;
}

#endif /* __powerpc64__ */

#include <asm-generic/atomic.h>
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_ATOMIC_H_ */
