#ifndef _ASM_M32R_ATOMIC_H
#define _ASM_M32R_ATOMIC_H

/*
 *  linux/include/asm-m32r/atomic.h
 *
 *  M32R version:
 *    Copyright (C) 2001, 2002  Hitoshi Yamamoto
 *    Copyright (C) 2004  Hirokazu Takata <takata at linux-m32r.org>
 */

#include <linux/config.h>
#include <asm/assembler.h>
#include <asm/system.h>

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

/*
 * Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
typedef struct { volatile int counter; } atomic_t;

#define ATOMIC_INIT(i)	{ (i) }

/**
 * atomic_read - read atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically reads the value of @v.
 */
#define atomic_read(v)	((v)->counter)

/**
 * atomic_set - set atomic variable
 * @v: pointer of type atomic_t
 * @i: required value
 *
 * Atomically sets the value of @v to @i.
 */
#define atomic_set(v,i)	(((v)->counter) = (i))

/**
 * atomic_add_return - add integer to atomic variable and return it
 * @i: integer value to add
 * @v: pointer of type atomic_t
 *
 * Atomically adds @i to @v and return (@i + @v).
 */
static __inline__ int atomic_add_return(int i, atomic_t *v)
{
	unsigned long flags;
	int result;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# atomic_add_return		\n\t"
		DCACHE_CLEAR("%0", "r4", "%1")
		M32R_LOCK" %0, @%1;		\n\t"
		"add	%0, %2;			\n\t"
		M32R_UNLOCK" %0, @%1;		\n\t"
		: "=&r" (result)
		: "r" (&v->counter), "r" (i)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r4"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);

	return result;
}

/**
 * atomic_sub_return - subtract integer from atomic variable and return it
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v and return (@v - @i).
 */
static __inline__ int atomic_sub_return(int i, atomic_t *v)
{
	unsigned long flags;
	int result;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# atomic_sub_return		\n\t"
		DCACHE_CLEAR("%0", "r4", "%1")
		M32R_LOCK" %0, @%1;		\n\t"
		"sub	%0, %2;			\n\t"
		M32R_UNLOCK" %0, @%1;		\n\t"
		: "=&r" (result)
		: "r" (&v->counter), "r" (i)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r4"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);

	return result;
}

/**
 * atomic_add - add integer to atomic variable
 * @i: integer value to add
 * @v: pointer of type atomic_t
 *
 * Atomically adds @i to @v.
 */
#define atomic_add(i,v) ((void) atomic_add_return((i), (v)))

/**
 * atomic_sub - subtract the atomic variable
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v.
 */
#define atomic_sub(i,v) ((void) atomic_sub_return((i), (v)))

/**
 * atomic_sub_and_test - subtract value from variable and test result
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v and returns
 * true if the result is zero, or false for all
 * other cases.
 */
#define atomic_sub_and_test(i,v) (atomic_sub_return((i), (v)) == 0)

/**
 * atomic_inc_return - increment atomic variable and return it
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1 and returns the result.
 */
static __inline__ int atomic_inc_return(atomic_t *v)
{
	unsigned long flags;
	int result;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# atomic_inc_return		\n\t"
		DCACHE_CLEAR("%0", "r4", "%1")
		M32R_LOCK" %0, @%1;		\n\t"
		"addi	%0, #1;			\n\t"
		M32R_UNLOCK" %0, @%1;		\n\t"
		: "=&r" (result)
		: "r" (&v->counter)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r4"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);

	return result;
}

/**
 * atomic_dec_return - decrement atomic variable and return it
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1 and returns the result.
 */
static __inline__ int atomic_dec_return(atomic_t *v)
{
	unsigned long flags;
	int result;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# atomic_dec_return		\n\t"
		DCACHE_CLEAR("%0", "r4", "%1")
		M32R_LOCK" %0, @%1;		\n\t"
		"addi	%0, #-1;		\n\t"
		M32R_UNLOCK" %0, @%1;		\n\t"
		: "=&r" (result)
		: "r" (&v->counter)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r4"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);

	return result;
}

/**
 * atomic_inc - increment atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1.
 */
#define atomic_inc(v) ((void)atomic_inc_return(v))

/**
 * atomic_dec - decrement atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1.
 */
#define atomic_dec(v) ((void)atomic_dec_return(v))

/**
 * atomic_inc_and_test - increment and test
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define atomic_inc_and_test(v) (atomic_inc_return(v) == 0)

/**
 * atomic_dec_and_test - decrement and test
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1 and
 * returns true if the result is 0, or false for all
 * other cases.
 */
#define atomic_dec_and_test(v) (atomic_dec_return(v) == 0)

/**
 * atomic_add_negative - add and test if negative
 * @v: pointer of type atomic_t
 * @i: integer value to add
 *
 * Atomically adds @i to @v and returns true
 * if the result is negative, or false when
 * result is greater than or equal to zero.
 */
#define atomic_add_negative(i,v) (atomic_add_return((i), (v)) < 0)

#define atomic_cmpxchg(v, o, n) ((int)cmpxchg(&((v)->counter), (o), (n)))

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
	int c, old;						\
	c = atomic_read(v);					\
	while (c != (u) && (old = atomic_cmpxchg((v), c, c + (a))) != c) \
		c = old;					\
	c != (u);						\
})
#define atomic_inc_not_zero(v) atomic_add_unless((v), 1, 0)

static __inline__ void atomic_clear_mask(unsigned long  mask, atomic_t *addr)
{
	unsigned long flags;
	unsigned long tmp;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# atomic_clear_mask		\n\t"
		DCACHE_CLEAR("%0", "r5", "%1")
		M32R_LOCK" %0, @%1;		\n\t"
		"and	%0, %2;			\n\t"
		M32R_UNLOCK" %0, @%1;		\n\t"
		: "=&r" (tmp)
		: "r" (addr), "r" (~mask)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r5"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);
}

static __inline__ void atomic_set_mask(unsigned long  mask, atomic_t *addr)
{
	unsigned long flags;
	unsigned long tmp;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# atomic_set_mask		\n\t"
		DCACHE_CLEAR("%0", "r5", "%1")
		M32R_LOCK" %0, @%1;		\n\t"
		"or	%0, %2;			\n\t"
		M32R_UNLOCK" %0, @%1;		\n\t"
		: "=&r" (tmp)
		: "r" (addr), "r" (mask)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r5"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);
}

/* Atomic operations are already serializing on m32r */
#define smp_mb__before_atomic_dec()	barrier()
#define smp_mb__after_atomic_dec()	barrier()
#define smp_mb__before_atomic_inc()	barrier()
#define smp_mb__after_atomic_inc()	barrier()

#endif	/* _ASM_M32R_ATOMIC_H */
