#ifndef _KERNEL_H
#define _KERNEL_H

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>

#include "../../include/linux/compiler.h"
#include "../../include/linux/err.h"
#include "../../../include/linux/kconfig.h"

#ifdef BENCHMARK
#define RADIX_TREE_MAP_SHIFT	6
#else
#define RADIX_TREE_MAP_SHIFT	3
#endif

#ifndef NULL
#define NULL	0
#endif

#define BUG_ON(expr)	assert(!(expr))
#define WARN_ON(expr)	assert(!(expr))
#define __init
#define __must_check
#define panic(expr)
#define printk printf
#define __force
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#define pr_debug printk

#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define cpu_relax()	barrier()

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define container_of(ptr, type, member) ({                      \
	const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
	(type *)( (char *)__mptr - offsetof(type, member) );})
#define min(a, b) ((a) < (b) ? (a) : (b))

#define cond_resched()	sched_yield()

static inline int in_interrupt(void)
{
	return 0;
}

/*
 * This looks more complex than it should be. But we need to
 * get the type for the ~ right in round_down (it needs to be
 * as wide as the result!), and we want to evaluate the macro
 * arguments just once each.
 */
#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)
#define round_down(x, y) ((x) & ~__round_mask(x, y))

#define xchg(ptr, x)	uatomic_xchg(ptr, x)

#endif /* _KERNEL_H */
