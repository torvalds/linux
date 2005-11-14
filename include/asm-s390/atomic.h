#ifndef __ARCH_S390_ATOMIC__
#define __ARCH_S390_ATOMIC__

/*
 *  include/asm-s390/atomic.h
 *
 *  S390 version
 *    Copyright (C) 1999-2003 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow,
 *		 Arnd Bergmann (arndb@de.ibm.com)
 *
 *  Derived from "include/asm-i386/bitops.h"
 *    Copyright (C) 1992, Linus Torvalds
 *
 */

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 * S390 uses 'Compare And Swap' for atomicity in SMP enviroment
 */

typedef struct {
	volatile int counter;
} __attribute__ ((aligned (4))) atomic_t;
#define ATOMIC_INIT(i)  { (i) }

#ifdef __KERNEL__

#define __CS_LOOP(ptr, op_val, op_string) ({				\
	typeof(ptr->counter) old_val, new_val;				\
        __asm__ __volatile__("   l     %0,0(%3)\n"			\
                             "0: lr    %1,%0\n"				\
                             op_string "  %1,%4\n"			\
                             "   cs    %0,%1,0(%3)\n"			\
                             "   jl    0b"				\
                             : "=&d" (old_val), "=&d" (new_val),	\
			       "=m" (((atomic_t *)(ptr))->counter)	\
			     : "a" (ptr), "d" (op_val),			\
			       "m" (((atomic_t *)(ptr))->counter)	\
			     : "cc", "memory" );			\
	new_val;							\
})
#define atomic_read(v)          ((v)->counter)
#define atomic_set(v,i)         (((v)->counter) = (i))

static __inline__ void atomic_add(int i, atomic_t * v)
{
	       __CS_LOOP(v, i, "ar");
}
static __inline__ int atomic_add_return(int i, atomic_t * v)
{
	return __CS_LOOP(v, i, "ar");
}
static __inline__ int atomic_add_negative(int i, atomic_t * v)
{
	return __CS_LOOP(v, i, "ar") < 0;
}
static __inline__ void atomic_sub(int i, atomic_t * v)
{
	       __CS_LOOP(v, i, "sr");
}
static __inline__ int atomic_sub_return(int i, atomic_t * v)
{
	return __CS_LOOP(v, i, "sr");
}
static __inline__ void atomic_inc(volatile atomic_t * v)
{
	       __CS_LOOP(v, 1, "ar");
}
static __inline__ int atomic_inc_return(volatile atomic_t * v)
{
	return __CS_LOOP(v, 1, "ar");
}

static __inline__ int atomic_inc_and_test(volatile atomic_t * v)
{
	return __CS_LOOP(v, 1, "ar") == 0;
}
static __inline__ void atomic_dec(volatile atomic_t * v)
{
	       __CS_LOOP(v, 1, "sr");
}
static __inline__ int atomic_dec_return(volatile atomic_t * v)
{
	return __CS_LOOP(v, 1, "sr");
}
static __inline__ int atomic_dec_and_test(volatile atomic_t * v)
{
	return __CS_LOOP(v, 1, "sr") == 0;
}
static __inline__ void atomic_clear_mask(unsigned long mask, atomic_t * v)
{
	       __CS_LOOP(v, ~mask, "nr");
}
static __inline__ void atomic_set_mask(unsigned long mask, atomic_t * v)
{
	       __CS_LOOP(v, mask, "or");
}
#undef __CS_LOOP

#ifdef __s390x__
typedef struct {
	volatile long long counter;
} __attribute__ ((aligned (8))) atomic64_t;
#define ATOMIC64_INIT(i)  { (i) }

#define __CSG_LOOP(ptr, op_val, op_string) ({				\
	typeof(ptr->counter) old_val, new_val;				\
        __asm__ __volatile__("   lg    %0,0(%3)\n"			\
                             "0: lgr   %1,%0\n"				\
                             op_string "  %1,%4\n"			\
                             "   csg   %0,%1,0(%3)\n"			\
                             "   jl    0b"				\
                             : "=&d" (old_val), "=&d" (new_val),	\
			       "=m" (((atomic_t *)(ptr))->counter)	\
			     : "a" (ptr), "d" (op_val),			\
			       "m" (((atomic_t *)(ptr))->counter)	\
			     : "cc", "memory" );			\
	new_val;							\
})
#define atomic64_read(v)          ((v)->counter)
#define atomic64_set(v,i)         (((v)->counter) = (i))

static __inline__ void atomic64_add(long long i, atomic64_t * v)
{
	       __CSG_LOOP(v, i, "agr");
}
static __inline__ long long atomic64_add_return(long long i, atomic64_t * v)
{
	return __CSG_LOOP(v, i, "agr");
}
static __inline__ long long atomic64_add_negative(long long i, atomic64_t * v)
{
	return __CSG_LOOP(v, i, "agr") < 0;
}
static __inline__ void atomic64_sub(long long i, atomic64_t * v)
{
	       __CSG_LOOP(v, i, "sgr");
}
static __inline__ void atomic64_inc(volatile atomic64_t * v)
{
	       __CSG_LOOP(v, 1, "agr");
}
static __inline__ long long atomic64_inc_return(volatile atomic64_t * v)
{
	return __CSG_LOOP(v, 1, "agr");
}
static __inline__ long long atomic64_inc_and_test(volatile atomic64_t * v)
{
	return __CSG_LOOP(v, 1, "agr") == 0;
}
static __inline__ void atomic64_dec(volatile atomic64_t * v)
{
	       __CSG_LOOP(v, 1, "sgr");
}
static __inline__ long long atomic64_dec_return(volatile atomic64_t * v)
{
	return __CSG_LOOP(v, 1, "sgr");
}
static __inline__ long long atomic64_dec_and_test(volatile atomic64_t * v)
{
	return __CSG_LOOP(v, 1, "sgr") == 0;
}
static __inline__ void atomic64_clear_mask(unsigned long mask, atomic64_t * v)
{
	       __CSG_LOOP(v, ~mask, "ngr");
}
static __inline__ void atomic64_set_mask(unsigned long mask, atomic64_t * v)
{
	       __CSG_LOOP(v, mask, "ogr");
}

#undef __CSG_LOOP
#endif

/*
  returns 0  if expected_oldval==value in *v ( swap was successful )
  returns 1  if unsuccessful.

  This is non-portable, use bitops or spinlocks instead!
*/
static __inline__ int
atomic_compare_and_swap(int expected_oldval,int new_val,atomic_t *v)
{
        int retval;

        __asm__ __volatile__(
                "  lr   %0,%3\n"
                "  cs   %0,%4,0(%2)\n"
                "  ipm  %0\n"
                "  srl  %0,28\n"
                "0:"
                : "=&d" (retval), "=m" (v->counter)
                : "a" (v), "d" (expected_oldval) , "d" (new_val),
		  "m" (v->counter) : "cc", "memory" );
        return retval;
}

#define atomic_cmpxchg(v, o, n) (atomic_compare_and_swap((o), (n), &((v)->counter)))

#define atomic_add_unless(v, a, u)				\
({								\
	int c, old;						\
	c = atomic_read(v);					\
	while (c != (u) && (old = atomic_cmpxchg((v), c, c + (a))) != c) \
		c = old;					\
	c != (u);						\
})
#define atomic_inc_not_zero(v) atomic_add_unless((v), 1, 0)

#define smp_mb__before_atomic_dec()	smp_mb()
#define smp_mb__after_atomic_dec()	smp_mb()
#define smp_mb__before_atomic_inc()	smp_mb()
#define smp_mb__after_atomic_inc()	smp_mb()

#endif /* __KERNEL__ */
#endif /* __ARCH_S390_ATOMIC__  */
