#ifndef _PPC64_DELAY_H
#define _PPC64_DELAY_H

/*
 * Copyright 1996, Paul Mackerras.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * PPC64 Support added by Dave Engebretsen, Todd Inglett, Mike Corrigan,
 * Anton Blanchard.
 */

extern unsigned long tb_ticks_per_usec;

/* define these here to prevent circular dependencies */ 
#define __HMT_low()	asm volatile("or 1,1,1")
#define __HMT_medium()	asm volatile("or 2,2,2")
#define __barrier()	asm volatile("":::"memory")

static inline unsigned long __get_tb(void)
{
	unsigned long rval;

	asm volatile("mftb %0" : "=r" (rval));
	return rval;
}

static inline void __delay(unsigned long loops)
{
	unsigned long start = __get_tb();

	while((__get_tb()-start) < loops)
		__HMT_low();
	__HMT_medium();
	__barrier();
}

static inline void udelay(unsigned long usecs)
{
	unsigned long loops = tb_ticks_per_usec * usecs;

	__delay(loops);
}

#endif /* _PPC64_DELAY_H */
