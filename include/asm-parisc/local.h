#ifndef _ARCH_PARISC_LOCAL_H
#define _ARCH_PARISC_LOCAL_H

#include <linux/percpu.h>
#include <asm/atomic.h>

typedef atomic_t local_t;

#define LOCAL_INIT(i)	ATOMIC_INIT(i)
#define local_read(v)	atomic_read(v)
#define local_set(v,i)	atomic_set(v,i)

#define local_inc(v)	atomic_inc(v)
#define local_dec(v)	atomic_dec(v)
#define local_add(i, v)	atomic_add(i, v)
#define local_sub(i, v)	atomic_sub(i, v)

#define __local_inc(v)		((v)->counter++)
#define __local_dec(v)		((v)->counter--)
#define __local_add(i,v)	((v)->counter+=(i))
#define __local_sub(i,v)	((v)->counter-=(i))

/* Use these for per-cpu local_t variables: on some archs they are
 * much more efficient than these naive implementations.  Note they take
 * a variable, not an address.
 */
#define cpu_local_read(v)	local_read(&__get_cpu_var(v))
#define cpu_local_set(v, i)	local_set(&__get_cpu_var(v), (i))

#define cpu_local_inc(v)	local_inc(&__get_cpu_var(v))
#define cpu_local_dec(v)	local_dec(&__get_cpu_var(v))
#define cpu_local_add(i, v)	local_add((i), &__get_cpu_var(v))
#define cpu_local_sub(i, v)	local_sub((i), &__get_cpu_var(v))

#define __cpu_local_inc(v)	__local_inc(&__get_cpu_var(v))
#define __cpu_local_dec(v)	__local_dec(&__get_cpu_var(v))
#define __cpu_local_add(i, v)	__local_add((i), &__get_cpu_var(v))
#define __cpu_local_sub(i, v)	__local_sub((i), &__get_cpu_var(v))

#endif /* _ARCH_PARISC_LOCAL_H */
