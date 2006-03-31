#ifndef _ARCH_X8664_LOCAL_H
#define _ARCH_X8664_LOCAL_H

#include <linux/percpu.h>

typedef struct
{
	volatile unsigned long counter;
} local_t;

#define LOCAL_INIT(i)	{ (i) }

#define local_read(v)	((v)->counter)
#define local_set(v,i)	(((v)->counter) = (i))

static __inline__ void local_inc(local_t *v)
{
	__asm__ __volatile__(
		"incq %0"
		:"=m" (v->counter)
		:"m" (v->counter));
}

static __inline__ void local_dec(local_t *v)
{
	__asm__ __volatile__(
		"decq %0"
		:"=m" (v->counter)
		:"m" (v->counter));
}

static __inline__ void local_add(unsigned int i, local_t *v)
{
	__asm__ __volatile__(
		"addq %1,%0"
		:"=m" (v->counter)
		:"ir" (i), "m" (v->counter));
}

static __inline__ void local_sub(unsigned int i, local_t *v)
{
	__asm__ __volatile__(
		"subq %1,%0"
		:"=m" (v->counter)
		:"ir" (i), "m" (v->counter));
}

/* On x86-64 these are better than the atomic variants on SMP kernels
   because they dont use a lock prefix. */
#define __local_inc(l)		local_inc(l)
#define __local_dec(l)		local_dec(l)
#define __local_add(i,l)	local_add((i),(l))
#define __local_sub(i,l)	local_sub((i),(l))

/* Use these for per-cpu local_t variables: on some archs they are
 * much more efficient than these naive implementations.  Note they take
 * a variable, not an address.
 *
 * This could be done better if we moved the per cpu data directly
 * after GS.
 */
#define cpu_local_read(v)	local_read(&__get_cpu_var(v))
#define cpu_local_set(v, i)	local_set(&__get_cpu_var(v), (i))
#define cpu_local_inc(v)	local_inc(&__get_cpu_var(v))
#define cpu_local_dec(v)	local_dec(&__get_cpu_var(v))
#define cpu_local_add(i, v)	local_add((i), &__get_cpu_var(v))
#define cpu_local_sub(i, v)	local_sub((i), &__get_cpu_var(v))

#define __cpu_local_inc(v)	cpu_local_inc(v)
#define __cpu_local_dec(v)	cpu_local_dec(v)
#define __cpu_local_add(i, v)	cpu_local_add((i), (v))
#define __cpu_local_sub(i, v)	cpu_local_sub((i), (v))

#endif /* _ARCH_I386_LOCAL_H */
