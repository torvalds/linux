#ifndef __ARCH_M68K_ATOMIC__
#define __ARCH_M68K_ATOMIC__

#include <linux/config.h>

#include <asm/system.h>	/* local_irq_XXX() */

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

/*
 * We do not have SMP m68k systems, so we don't have to deal with that.
 */

typedef struct { int counter; } atomic_t;
#define ATOMIC_INIT(i)	{ (i) }

#define atomic_read(v)		((v)->counter)
#define atomic_set(v, i)	(((v)->counter) = i)

static inline void atomic_add(int i, atomic_t *v)
{
	__asm__ __volatile__("addl %1,%0" : "+m" (*v) : "id" (i));
}

static inline void atomic_sub(int i, atomic_t *v)
{
	__asm__ __volatile__("subl %1,%0" : "+m" (*v) : "id" (i));
}

static inline void atomic_inc(atomic_t *v)
{
	__asm__ __volatile__("addql #1,%0" : "+m" (*v));
}

static inline void atomic_dec(atomic_t *v)
{
	__asm__ __volatile__("subql #1,%0" : "+m" (*v));
}

static inline int atomic_dec_and_test(atomic_t *v)
{
	char c;
	__asm__ __volatile__("subql #1,%1; seq %0" : "=d" (c), "+m" (*v));
	return c != 0;
}

static inline int atomic_inc_and_test(atomic_t *v)
{
	char c;
	__asm__ __volatile__("addql #1,%1; seq %0" : "=d" (c), "+m" (*v));
	return c != 0;
}

#ifdef CONFIG_RMW_INSNS
static inline int atomic_add_return(int i, atomic_t *v)
{
	int t, tmp;

	__asm__ __volatile__(
			"1:	movel %2,%1\n"
			"	addl %3,%1\n"
			"	casl %2,%1,%0\n"
			"	jne 1b"
			: "+m" (*v), "=&d" (t), "=&d" (tmp)
			: "g" (i), "2" (atomic_read(v)));
	return t;
}

static inline int atomic_sub_return(int i, atomic_t *v)
{
	int t, tmp;

	__asm__ __volatile__(
			"1:	movel %2,%1\n"
			"	subl %3,%1\n"
			"	casl %2,%1,%0\n"
			"	jne 1b"
			: "+m" (*v), "=&d" (t), "=&d" (tmp)
			: "g" (i), "2" (atomic_read(v)));
	return t;
}
#else /* !CONFIG_RMW_INSNS */
static inline int atomic_add_return(int i, atomic_t * v)
{
	unsigned long flags;
	int t;

	local_irq_save(flags);
	t = atomic_read(v);
	t += i;
	atomic_set(v, t);
	local_irq_restore(flags);

	return t;
}

static inline int atomic_sub_return(int i, atomic_t * v)
{
	unsigned long flags;
	int t;

	local_irq_save(flags);
	t = atomic_read(v);
	t -= i;
	atomic_set(v, t);
	local_irq_restore(flags);

	return t;
}
#endif /* !CONFIG_RMW_INSNS */

#define atomic_dec_return(v)	atomic_sub_return(1, (v))
#define atomic_inc_return(v)	atomic_add_return(1, (v))

static inline int atomic_sub_and_test(int i, atomic_t *v)
{
	char c;
	__asm__ __volatile__("subl %2,%1; seq %0" : "=d" (c), "+m" (*v): "g" (i));
	return c != 0;
}

static inline int atomic_add_negative(int i, atomic_t *v)
{
	char c;
	__asm__ __volatile__("addl %2,%1; smi %0" : "=d" (c), "+m" (*v): "g" (i));
	return c != 0;
}

static inline void atomic_clear_mask(unsigned long mask, unsigned long *v)
{
	__asm__ __volatile__("andl %1,%0" : "+m" (*v) : "id" (~(mask)));
}

static inline void atomic_set_mask(unsigned long mask, unsigned long *v)
{
	__asm__ __volatile__("orl %1,%0" : "+m" (*v) : "id" (mask));
}

#define atomic_cmpxchg(v, o, n) ((int)cmpxchg(&((v)->counter), (o), (n)))

#define atomic_add_unless(v, a, u)				\
({								\
	int c, old;						\
	c = atomic_read(v);					\
	while (c != (u) && (old = atomic_cmpxchg((v), c, c + (a))) != c) \
		c = old;					\
	c != (u);						\
})
#define atomic_inc_not_zero(v) atomic_add_unless((v), 1, 0)

/* Atomic operations are already serializing */
#define smp_mb__before_atomic_dec()	barrier()
#define smp_mb__after_atomic_dec()	barrier()
#define smp_mb__before_atomic_inc()	barrier()
#define smp_mb__after_atomic_inc()	barrier()

#endif /* __ARCH_M68K_ATOMIC __ */
