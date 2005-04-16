/* irc-regs.h: on-chip interrupt controller registers
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_IRC_REGS
#define _ASM_IRC_REGS

#define __reg(ADDR) (*(volatile unsigned long *)(ADDR))

#define __get_TM0()	({ __reg(0xfeff9800); })
#define __get_TM1()	({ __reg(0xfeff9808); })
#define __set_TM1(V)	do { __reg(0xfeff9808) = (V); mb(); } while(0)

#define __set_TM1x(XI,V)			\
do {						\
	int shift = (XI) * 2 + 16;		\
	unsigned long tm1 = __reg(0xfeff9808);	\
	tm1 &= ~(0x3 << shift);			\
	tm1 |= (V) << shift;			\
	__reg(0xfeff9808) = tm1;		\
	mb();					\
} while(0)

#define __get_RS(C)	({ (__reg(0xfeff9810) >> ((C)+16)) & 1; })

#define __clr_RC(C)	do { __reg(0xfeff9818) = 1 << ((C)+16); mb(); } while(0)

#define __get_MASK(C)	({ (__reg(0xfeff9820) >> ((C)+16)) & 1; })
#define __set_MASK(C)	do { __reg(0xfeff9820) |=  1 << ((C)+16); mb(); } while(0)
#define __clr_MASK(C)	do { __reg(0xfeff9820) &=  ~(1 << ((C)+16)); mb(); } while(0)

#define __get_MASK_all() __get_MASK(0)
#define __set_MASK_all() __set_MASK(0)
#define __clr_MASK_all() __clr_MASK(0)

#define __get_IRL()	({ (__reg(0xfeff9828) >> 16) & 0xf; })
#define __clr_IRL()	do { __reg(0xfeff9828) = 0x100000; mb(); } while(0)

#define __get_IRR(N)	({ __reg(0xfeff9840 + (N) * 8); })
#define __set_IRR(N,V)	do { __reg(0xfeff9840 + (N) * 8) = (V); } while(0)

#define __get_IITMR(N)	({ __reg(0xfeff9880 + (N) * 8); })
#define __set_IITMR(N,V) do { __reg(0xfeff9880 + (N) * 8) = (V); } while(0)


#endif /* _ASM_IRC_REGS */
