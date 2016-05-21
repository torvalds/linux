#ifndef _KERNEL_H
#define _KERNEL_H

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>

#include "../../include/linux/compiler.h"

#define CONFIG_SHMEM
#define CONFIG_SWAP

#define RADIX_TREE_MAP_SHIFT	3

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
#endif /* _KERNEL_H */
