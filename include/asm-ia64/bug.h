#ifndef _ASM_IA64_BUG_H
#define _ASM_IA64_BUG_H

#if (__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
# define ia64_abort()	__builtin_trap()
#else
# define ia64_abort()	(*(volatile int *) 0 = 0)
#endif
#define BUG() do { printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); ia64_abort(); } while (0)

/* should this BUG should be made generic? */
#define HAVE_ARCH_BUG
#include <asm-generic/bug.h>

#endif
