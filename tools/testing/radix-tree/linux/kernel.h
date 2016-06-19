#ifndef _KERNEL_H
#define _KERNEL_H

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>

#ifndef NULL
#define NULL	0
#endif

#define BUG_ON(expr)	assert(!(expr))
#define __init
#define __must_check
#define panic(expr)
#define printk printf
#define __force
#define likely(c) (c)
#define unlikely(c) (c)
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define container_of(ptr, type, member) ({                      \
	const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
	(type *)( (char *)__mptr - offsetof(type, member) );})
#define min(a, b) ((a) < (b) ? (a) : (b))

static inline int in_interrupt(void)
{
	return 0;
}
#endif /* _KERNEL_H */
