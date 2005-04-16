/* delay.h: Linux delay routines on sparc64.
 *
 * Copyright (C) 1996, 2004 David S. Miller (davem@davemloft.net).
 *
 * Based heavily upon x86 variant which is:
 * Copyright (C) 1993 Linus Torvalds
 *
 * Delay routines calling functions in arch/sparc64/lib/delay.c
 */

#ifndef __SPARC64_DELAY_H
#define __SPARC64_DELAY_H

#include <linux/config.h>
#include <linux/param.h>
#include <asm/cpudata.h>

#ifndef __ASSEMBLY__

extern void __bad_udelay(void);
extern void __bad_ndelay(void);

extern void __udelay(unsigned long usecs);
extern void __ndelay(unsigned long nsecs);
extern void __const_udelay(unsigned long usecs);
extern void __delay(unsigned long loops);

#define udelay(n) (__builtin_constant_p(n) ? \
	((n) > 20000 ? __bad_udelay() : __const_udelay((n) * 0x10c7ul)) : \
	__udelay(n))
	
#define ndelay(n) (__builtin_constant_p(n) ? \
	((n) > 20000 ? __bad_ndelay() : __const_udelay((n) * 5ul)) : \
	__ndelay(n))

#endif /* !__ASSEMBLY__ */

#endif /* defined(__SPARC64_DELAY_H) */
