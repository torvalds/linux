#ifndef _ASM_GENERIC_LOCAL_H
#define _ASM_GENERIC_LOCAL_H

#include <linux/config.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <asm/atomic.h>
#include <asm/types.h>

/* An unsigned long type for operations which are atomic for a single
 * CPU.  Usually used in combination with per-cpu variables. */

/* Implement in terms of atomics. */

/* Don't use typedef: don't want them to be mixed with atomic_t's. */
typedef struct
{
	atomic_long_t a;
} local_t;

#define LOCAL_INIT(i)	{ ATOMIC_LONG_INIT(i) }

#define local_read(l)	((unsigned long)atomic_long_read(&(l)->a))
#define local_set(l,i)	atomic_long_set((&(l)->a),(i))
#define local_inc(l)	atomic_long_inc(&(l)->a)
#define local_dec(l)	atomic_long_dec(&(l)->a)
#define local_add(i,l)	atomic_long_add((i),(&(l)->a))
#define local_sub(i,l)	atomic_long_sub((i),(&(l)->a))

/* Non-atomic variants, ie. preemption disabled and won't be touched
 * in interrupt, etc.  Some archs can optimize this case well. */
#define __local_inc(l)		local_set((l), local_read(l) + 1)
#define __local_dec(l)		local_set((l), local_read(l) - 1)
#define __local_add(i,l)	local_set((l), local_read(l) + (i))
#define __local_sub(i,l)	local_set((l), local_read(l) - (i))

/* Use these for per-cpu local_t variables: on some archs they are
 * much more efficient than these naive implementations.  Note they take
 * a variable (eg. mystruct.foo), not an address.
 */
#define cpu_local_read(v)	local_read(&__get_cpu_var(v))
#define cpu_local_set(v, i)	local_set(&__get_cpu_var(v), (i))
#define cpu_local_inc(v)	local_inc(&__get_cpu_var(v))
#define cpu_local_dec(v)	local_dec(&__get_cpu_var(v))
#define cpu_local_add(i, v)	local_add((i), &__get_cpu_var(v))
#define cpu_local_sub(i, v)	local_sub((i), &__get_cpu_var(v))

/* Non-atomic increments, ie. preemption disabled and won't be touched
 * in interrupt, etc.  Some archs can optimize this case well.
 */
#define __cpu_local_inc(v)	__local_inc(&__get_cpu_var(v))
#define __cpu_local_dec(v)	__local_dec(&__get_cpu_var(v))
#define __cpu_local_add(i, v)	__local_add((i), &__get_cpu_var(v))
#define __cpu_local_sub(i, v)	__local_sub((i), &__get_cpu_var(v))

#endif /* _ASM_GENERIC_LOCAL_H */
