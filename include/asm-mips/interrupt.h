/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2003 by Ralf Baechle
 * Copyright (C) 1996 by Paul M. Antoine
 * Copyright (C) 1999 Silicon Graphics
 * Copyright (C) 2000 MIPS Technologies, Inc.
 */
#ifndef _ASM_INTERRUPT_H
#define _ASM_INTERRUPT_H

#include <asm/hazards.h>

__asm__ (
	".macro\tlocal_irq_enable\n\t"
	".set\tpush\n\t"
	".set\treorder\n\t"
	".set\tnoat\n\t"
	"mfc0\t$1,$12\n\t"
	"ori\t$1,0x1f\n\t"
	"xori\t$1,0x1e\n\t"
	"mtc0\t$1,$12\n\t"
	"irq_enable_hazard\n\t"
	".set\tpop\n\t"
	".endm");

static inline void local_irq_enable(void)
{
	__asm__ __volatile__(
		"local_irq_enable"
		: /* no outputs */
		: /* no inputs */
		: "memory");
}

/*
 * For cli() we have to insert nops to make sure that the new value
 * has actually arrived in the status register before the end of this
 * macro.
 * R4000/R4400 need three nops, the R4600 two nops and the R10000 needs
 * no nops at all.
 */
__asm__ (
	".macro\tlocal_irq_disable\n\t"
	".set\tpush\n\t"
	".set\tnoat\n\t"
	"mfc0\t$1,$12\n\t"
	"ori\t$1,1\n\t"
	"xori\t$1,1\n\t"
	".set\tnoreorder\n\t"
	"mtc0\t$1,$12\n\t"
	"irq_disable_hazard\n\t"
	".set\tpop\n\t"
	".endm");

static inline void local_irq_disable(void)
{
	__asm__ __volatile__(
		"local_irq_disable"
		: /* no outputs */
		: /* no inputs */
		: "memory");
}

__asm__ (
	".macro\tlocal_save_flags flags\n\t"
	".set\tpush\n\t"
	".set\treorder\n\t"
	"mfc0\t\\flags, $12\n\t"
	".set\tpop\n\t"
	".endm");

#define local_save_flags(x)						\
__asm__ __volatile__(							\
	"local_save_flags %0"						\
	: "=r" (x))

__asm__ (
	".macro\tlocal_irq_save result\n\t"
	".set\tpush\n\t"
	".set\treorder\n\t"
	".set\tnoat\n\t"
	"mfc0\t\\result, $12\n\t"
	"ori\t$1, \\result, 1\n\t"
	"xori\t$1, 1\n\t"
	".set\tnoreorder\n\t"
	"mtc0\t$1, $12\n\t"
	"irq_disable_hazard\n\t"
	".set\tpop\n\t"
	".endm");

#define local_irq_save(x)						\
__asm__ __volatile__(							\
	"local_irq_save\t%0"						\
	: "=r" (x)							\
	: /* no inputs */						\
	: "memory")

__asm__ (
	".macro\tlocal_irq_restore flags\n\t"
	".set\tnoreorder\n\t"
	".set\tnoat\n\t"
	"mfc0\t$1, $12\n\t"
	"andi\t\\flags, 1\n\t"
	"ori\t$1, 1\n\t"
	"xori\t$1, 1\n\t"
	"or\t\\flags, $1\n\t"
	"mtc0\t\\flags, $12\n\t"
	"irq_disable_hazard\n\t"
	".set\tat\n\t"
	".set\treorder\n\t"
	".endm");

#define local_irq_restore(flags)					\
do {									\
	unsigned long __tmp1;						\
									\
	__asm__ __volatile__(						\
		"local_irq_restore\t%0"					\
		: "=r" (__tmp1)						\
		: "0" (flags)						\
		: "memory");						\
} while(0)

#define irqs_disabled()							\
({									\
	unsigned long flags;						\
	local_save_flags(flags);					\
	!(flags & 1);							\
})

#endif /* _ASM_INTERRUPT_H */
