/*
 * PowerPC atomic operations
 */

#ifndef _ASM_PPC_ATOMIC_H_
#define _ASM_PPC_ATOMIC_H_

typedef struct { volatile int counter; } atomic_t;

#ifdef __KERNEL__

#define ATOMIC_INIT(i)	{ (i) }

#define atomic_read(v)		((v)->counter)
#define atomic_set(v,i)		(((v)->counter) = (i))

extern void atomic_clear_mask(unsigned long mask, unsigned long *addr);

#ifdef CONFIG_SMP
#define SMP_SYNC	"sync"
#define SMP_ISYNC	"\n\tisync"
#else
#define SMP_SYNC	""
#define SMP_ISYNC
#endif

/* Erratum #77 on the 405 means we need a sync or dcbt before every stwcx.
 * The old ATOMIC_SYNC_FIX covered some but not all of this.
 */
#ifdef CONFIG_IBM405_ERR77
#define PPC405_ERR77(ra,rb)	"dcbt " #ra "," #rb ";"
#else
#define PPC405_ERR77(ra,rb)
#endif

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
"1:	lwarx	%0,0,%2		# atomic_add_return\n\
	add	%0,%1,%0\n"
	PPC405_ERR77(0,%2)
"	stwcx.	%0,0,%2 \n\
	bne-	1b"
	SMP_ISYNC
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
"1:	lwarx	%0,0,%2		# atomic_sub_return\n\
	subf	%0,%1,%0\n"
	PPC405_ERR77(0,%2)
"	stwcx.	%0,0,%2 \n\
	bne-	1b"
	SMP_ISYNC
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
"1:	lwarx	%0,0,%1		# atomic_inc_return\n\
	addic	%0,%0,1\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%0,0,%1 \n\
	bne-	1b"
	SMP_ISYNC
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
"1:	lwarx	%0,0,%1		# atomic_dec_return\n\
	addic	%0,%0,-1\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%0,0,%1\n\
	bne-	1b"
	SMP_ISYNC
	: "=&r" (t)
	: "r" (&v->counter)
	: "cc", "memory");

	return t;
}

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
"1:	lwarx	%0,0,%1		# atomic_dec_if_positive\n\
	addic.	%0,%0,-1\n\
	blt-	2f\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%0,0,%1\n\
	bne-	1b"
	SMP_ISYNC
	"\n\
2:"	: "=&r" (t)
	: "r" (&v->counter)
	: "cc", "memory");

	return t;
}

#define __MB	__asm__ __volatile__ (SMP_SYNC : : : "memory")
#define smp_mb__before_atomic_dec()	__MB
#define smp_mb__after_atomic_dec()	__MB
#define smp_mb__before_atomic_inc()	__MB
#define smp_mb__after_atomic_inc()	__MB

#endif /* __KERNEL__ */
#endif /* _ASM_PPC_ATOMIC_H_ */
