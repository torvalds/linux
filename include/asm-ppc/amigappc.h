/*
** asm-ppc/amigappc.h -- This header defines some values and pointers for
**                        the Phase 5 PowerUp card.
**
** Copyright 1997, 1998 by Phase5, Germany.
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file COPYING in the main directory of this archive
** for more details.
**
** Created: 7/22/97 by Jesper Skov
*/

#ifdef __KERNEL__
#ifndef _M68K_AMIGAPPC_H
#define _M68K_AMIGAPPC_H

#ifndef __ASSEMBLY__

/* #include <asm/system.h> */
#define mb()  __asm__ __volatile__ ("sync" : : : "memory")

#define APUS_WRITE(_a_, _v_)				\
do {							\
	(*((volatile unsigned char *)(_a_)) = (_v_));	\
	mb();						\
} while (0)

#define APUS_READ(_a_, _v_)				\
do {							\
	(_v_) = (*((volatile unsigned char *)(_a_)));	\
	mb();						\
} while (0)
#endif /* ndef __ASSEMBLY__ */

/* Maybe add a [#ifdef WANT_ZTWOBASE] condition to amigahw.h? */
#define zTwoBase (0x80000000)

#define APUS_IPL_BASE   	(zTwoBase + 0x00f60000)
#define APUS_REG_RESET    	(APUS_IPL_BASE + 0x00)
#define APUS_REG_WAITSTATE    	(APUS_IPL_BASE + 0x10)
#define APUS_REG_SHADOW    	(APUS_IPL_BASE + 0x18)
#define APUS_REG_LOCK		(APUS_IPL_BASE + 0x20)
#define APUS_REG_INT    	(APUS_IPL_BASE + 0x28)
#define APUS_IPL_EMU		(APUS_IPL_BASE + 0x30)
#define APUS_INT_LVL		(APUS_IPL_BASE + 0x38)

#define REGSHADOW_SETRESET	(0x80)
#define REGSHADOW_SELFRESET	(0x40)

#define REGLOCK_SETRESET	(0x80)
#define REGLOCK_BLACKMAGICK1	(0x40)
#define REGLOCK_BLACKMAGICK2	(0x20)
#define REGLOCK_BLACKMAGICK3	(0x10)

#define REGWAITSTATE_SETRESET	(0x80)
#define REGWAITSTATE_PPCW	(0x08)
#define REGWAITSTATE_PPCR	(0x04)

#define REGRESET_SETRESET	(0x80)
#define REGRESET_PPCRESET	(0x10)
#define REGRESET_M68KRESET	(0x08)
#define REGRESET_AMIGARESET	(0x04)
#define REGRESET_AUXRESET	(0x02)
#define REGRESET_SCSIRESET	(0x01)

#define REGINT_SETRESET		(0x80)
#define REGINT_ENABLEIPL	(0x02)
#define REGINT_INTMASTER	(0x01)

#define IPLEMU_SETRESET		(0x80)
#define IPLEMU_DISABLEINT	(0x40)
#define IPLEMU_IPL2		(0x20)
#define IPLEMU_IPL1		(0x10)
#define IPLEMU_IPL0		(0x08)
#define IPLEMU_PPCIPL2		(0x04)
#define IPLEMU_PPCIPL1		(0x02)
#define IPLEMU_PPCIPL0		(0x01)
#define IPLEMU_IPLMASK		(IPLEMU_PPCIPL2|IPLEMU_PPCIPL1|IPLEMU_PPCIPL0)

#define INTLVL_SETRESET         (0x80)
#define INTLVL_MASK             (0x7f)

#endif /* _M68k_AMIGAPPC_H */
#endif /* __KERNEL__ */
