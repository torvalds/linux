#ifndef _I386_DELAY_H
#define _I386_DELAY_H

/*
 * Copyright (C) 1993 Linus Torvalds
 *
 * Delay routines calling functions in arch/i386/lib/delay.c
 */
 
/* Undefined functions to get compile-time errors */
extern void __bad_udelay(void);
extern void __bad_ndelay(void);

extern void __udelay(unsigned long usecs);
extern void __ndelay(unsigned long nsecs);
extern void __const_udelay(unsigned long usecs);
extern void __delay(unsigned long loops);

#if defined(CONFIG_PARAVIRT) && !defined(USE_REAL_TIME_DELAY)
#define udelay(n) paravirt_ops.const_udelay((n) * 0x10c7ul)

#define ndelay(n) paravirt_ops.const_udelay((n) * 5ul)

#else /* !PARAVIRT || USE_REAL_TIME_DELAY */

/* 0x10c7 is 2**32 / 1000000 (rounded up) */
#define udelay(n) (__builtin_constant_p(n) ? \
	((n) > 20000 ? __bad_udelay() : __const_udelay((n) * 0x10c7ul)) : \
	__udelay(n))

/* 0x5 is 2**32 / 1000000000 (rounded up) */
#define ndelay(n) (__builtin_constant_p(n) ? \
	((n) > 20000 ? __bad_ndelay() : __const_udelay((n) * 5ul)) : \
	__ndelay(n))
#endif

void use_tsc_delay(void);

#endif /* defined(_I386_DELAY_H) */
