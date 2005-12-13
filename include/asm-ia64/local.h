#ifndef _ASM_IA64_LOCAL_H
#define _ASM_IA64_LOCAL_H

/*
 * Copyright (C) 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/percpu.h>

typedef struct {
	atomic64_t val;
} local_t;

#define LOCAL_INIT(i)	((local_t) { { (i) } })
#define local_read(l)	atomic64_read(&(l)->val)
#define local_set(l, i)	atomic64_set(&(l)->val, i)
#define local_inc(l)	atomic64_inc(&(l)->val)
#define local_dec(l)	atomic64_dec(&(l)->val)
#define local_add(i, l)	atomic64_add((i), &(l)->val)
#define local_sub(i, l)	atomic64_sub((i), &(l)->val)

/* Non-atomic variants, i.e., preemption disabled and won't be touched in interrupt, etc.  */

#define __local_inc(l)		(++(l)->val.counter)
#define __local_dec(l)		(--(l)->val.counter)
#define __local_add(i,l)	((l)->val.counter += (i))
#define __local_sub(i,l)	((l)->val.counter -= (i))

/*
 * Use these for per-cpu local_t variables.  Note they take a variable (eg. mystruct.foo),
 * not an address.
 */
#define cpu_local_read(v)	local_read(&__ia64_per_cpu_var(v))
#define cpu_local_set(v, i)	local_set(&__ia64_per_cpu_var(v), (i))
#define cpu_local_inc(v)	local_inc(&__ia64_per_cpu_var(v))
#define cpu_local_dec(v)	local_dec(&__ia64_per_cpu_var(v))
#define cpu_local_add(i, v)	local_add((i), &__ia64_per_cpu_var(v))
#define cpu_local_sub(i, v)	local_sub((i), &__ia64_per_cpu_var(v))

/*
 * Non-atomic increments, i.e., preemption disabled and won't be touched in interrupt,
 * etc.
 */
#define __cpu_local_inc(v)	__local_inc(&__ia64_per_cpu_var(v))
#define __cpu_local_dec(v)	__local_dec(&__ia64_per_cpu_var(v))
#define __cpu_local_add(i, v)	__local_add((i), &__ia64_per_cpu_var(v))
#define __cpu_local_sub(i, v)	__local_sub((i), &__ia64_per_cpu_var(v))

#endif /* _ASM_IA64_LOCAL_H */
