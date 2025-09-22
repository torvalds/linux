/* Public domain. */

#ifndef _LINUX_COMPILER_H
#define _LINUX_COMPILER_H

#include <linux/kconfig.h>
#include <sys/atomic.h>		/* for READ_ONCE() WRITE_ONCE() */

#define unlikely(x)	__builtin_expect(!!(x), 0)
#define likely(x)	__builtin_expect(!!(x), 1)

#define __force
#define __acquires(x)
#define __releases(x)
#define __read_mostly
#define __iomem
#define __must_check
#define __init
#define __exit
#define __deprecated
#define __nonstring
#define __always_unused	__attribute__((__unused__))
#define __maybe_unused	__attribute__((__unused__))
#define __always_inline	inline __attribute__((__always_inline__))
#define noinline	__attribute__((__noinline__))
#define noinline_for_stack	 __attribute__((__noinline__))
#define fallthrough	do {} while (0)
#define __counted_by(x)
#define __cleanup(fn)	__attribute__((__cleanup__(fn)))

#define __PASTE(x,y) __CONCAT(x,y)

#ifndef __user
#define __user
#endif

#define barrier()	__asm volatile("" : : : "memory")

#define __printf(x, y)	__attribute__((__format__(__kprintf__,x,y)))

/* The Linux code doesn't meet our usual standards! */
#ifdef __clang__
#pragma clang diagnostic ignored "-Winitializer-overrides"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wgnu-variable-sized-type-not-at-end"
#else
#pragma GCC diagnostic ignored "-Wformat-zero-length"
#endif

#define __diag_push()
#define __diag_ignore_all(x, y)
#define __diag_pop()

#endif
