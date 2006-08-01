#ifndef _ARCH_I386_LOCAL_H
#define _ARCH_I386_LOCAL_H

#include <linux/percpu.h>

typedef struct
{
	volatile long counter;
} local_t;

#define LOCAL_INIT(i)	{ (i) }

#define local_read(v)	((v)->counter)
#define local_set(v,i)	(((v)->counter) = (i))

static __inline__ void local_inc(local_t *v)
{
	__asm__ __volatile__(
		"incl %0"
		:"+m" (v->counter));
}

static __inline__ void local_dec(local_t *v)
{
	__asm__ __volatile__(
		"decl %0"
		:"+m" (v->counter));
}

static __inline__ void local_add(long i, local_t *v)
{
	__asm__ __volatile__(
		"addl %1,%0"
		:"+m" (v->counter)
		:"ir" (i));
}

static __inline__ void local_sub(long i, local_t *v)
{
	__asm__ __volatile__(
		"subl %1,%0"
		:"+m" (v->counter)
		:"ir" (i));
}

/* On x86, these are no better than the atomic variants. */
#define __local_inc(l)		local_inc(l)
#define __local_dec(l)		local_dec(l)
#define __local_add(i,l)	local_add((i),(l))
#define __local_sub(i,l)	local_sub((i),(l))

/* Use these for per-cpu local_t variables: on some archs they are
 * much more efficient than these naive implementations.  Note they take
 * a variable, not an address.
 */

/* Need to disable preemption for the cpu local counters otherwise we could
   still access a variable of a previous CPU in a non atomic way. */
#define cpu_local_wrap_v(v)	 	\
	({ local_t res__;		\
	   preempt_disable(); 		\
	   res__ = (v);			\
	   preempt_enable();		\
	   res__; })
#define cpu_local_wrap(v)		\
	({ preempt_disable();		\
	   v;				\
	   preempt_enable(); })		\

#define cpu_local_read(v)    cpu_local_wrap_v(local_read(&__get_cpu_var(v)))
#define cpu_local_set(v, i)  cpu_local_wrap(local_set(&__get_cpu_var(v), (i)))
#define cpu_local_inc(v)     cpu_local_wrap(local_inc(&__get_cpu_var(v)))
#define cpu_local_dec(v)     cpu_local_wrap(local_dec(&__get_cpu_var(v)))
#define cpu_local_add(i, v)  cpu_local_wrap(local_add((i), &__get_cpu_var(v)))
#define cpu_local_sub(i, v)  cpu_local_wrap(local_sub((i), &__get_cpu_var(v)))

#define __cpu_local_inc(v)	cpu_local_inc(v)
#define __cpu_local_dec(v)	cpu_local_dec(v)
#define __cpu_local_add(i, v)	cpu_local_add((i), (v))
#define __cpu_local_sub(i, v)	cpu_local_sub((i), (v))

#endif /* _ARCH_I386_LOCAL_H */
