/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003, 2004 Ralf Baechle
 */
#ifndef _ASM_HAZARDS_H
#define _ASM_HAZARDS_H

#include <linux/config.h>

#ifdef __ASSEMBLY__

	.macro	_ssnop
	sll	$0, $0, 1
	.endm

	.macro	_ehb
	sll	$0, $0, 3
	.endm

/*
 * RM9000 hazards.  When the JTLB is updated by tlbwi or tlbwr, a subsequent
 * use of the JTLB for instructions should not occur for 4 cpu cycles and use
 * for data translations should not occur for 3 cpu cycles.
 */
#ifdef CONFIG_CPU_RM9000

	.macro	mtc0_tlbw_hazard
	.set	push
	.set	mips32
	_ssnop; _ssnop; _ssnop; _ssnop
	.set	pop
	.endm

	.macro	tlbw_eret_hazard
	.set	push
	.set	mips32
	_ssnop; _ssnop; _ssnop; _ssnop
	.set	pop
	.endm

#else

/*
 * The taken branch will result in a two cycle penalty for the two killed
 * instructions on R4000 / R4400.  Other processors only have a single cycle
 * hazard so this is nice trick to have an optimal code for a range of
 * processors.
 */
	.macro	mtc0_tlbw_hazard
	b	. + 8
	.endm

	.macro	tlbw_eret_hazard
	.endm
#endif

/*
 * mtc0->mfc0 hazard
 * The 24K has a 2 cycle mtc0/mfc0 execution hazard.
 * It is a MIPS32R2 processor so ehb will clear the hazard.
 */

#ifdef CONFIG_CPU_MIPSR2
/*
 * Use a macro for ehb unless explicit support for MIPSR2 is enabled
 */

#define irq_enable_hazard
	_ehb

#define irq_disable_hazard
	_ehb

#elif defined(CONFIG_CPU_R10000) || defined(CONFIG_CPU_RM9000) || \
      defined(CONFIG_CPU_SB1)

/*
 * R10000 rocks - all hazards handled in hardware, so this becomes a nobrainer.
 */

#define irq_enable_hazard

#define irq_disable_hazard

#else

/*
 * Classic MIPS needs 1 - 3 nops or ssnops
 */
#define irq_enable_hazard
#define irq_disable_hazard						\
	_ssnop; _ssnop; _ssnop

#endif

#else /* __ASSEMBLY__ */

__asm__(
	"	.macro	_ssnop					\n\t"
	"	sll	$0, $2, 1				\n\t"
	"	.endm						\n\t"
	"							\n\t"
	"	.macro	_ehb					\n\t"
	"	sll	$0, $0, 3				\n\t"
	"	.endm						\n\t");

#ifdef CONFIG_CPU_RM9000

/*
 * RM9000 hazards.  When the JTLB is updated by tlbwi or tlbwr, a subsequent
 * use of the JTLB for instructions should not occur for 4 cpu cycles and use
 * for data translations should not occur for 3 cpu cycles.
 */

#define mtc0_tlbw_hazard()						\
	__asm__ __volatile__(						\
		".set\tmips32\n\t"					\
		"_ssnop; _ssnop; _ssnop; _ssnop\n\t"			\
		".set\tmips0")

#define tlbw_use_hazard()						\
	__asm__ __volatile__(						\
		".set\tmips32\n\t"					\
		"_ssnop; _ssnop; _ssnop; _ssnop\n\t"			\
		".set\tmips0")

#define back_to_back_c0_hazard()	do { } while (0)

#else

/*
 * Overkill warning ...
 */
#define mtc0_tlbw_hazard()						\
	__asm__ __volatile__(						\
		".set noreorder\n\t"					\
		"nop; nop; nop; nop; nop; nop;\n\t"			\
		".set reorder\n\t")

#define tlbw_use_hazard()						\
	__asm__ __volatile__(						\
		".set noreorder\n\t"					\
		"nop; nop; nop; nop; nop; nop;\n\t"			\
		".set reorder\n\t")

#endif

/*
 * Interrupt enable/disable hazards
 * Some processors have hazards when modifying
 * the status register to change the interrupt state
 */

#ifdef CONFIG_CPU_MIPSR2

__asm__(
	"	.macro\tirq_enable_hazard			\n\t"
	"	_ehb						\n\t"
	"	.endm						\n\t"
	"							\n\t"
	"	.macro\tirq_disable_hazard			\n\t"
	"	_ehb						\n\t"
	"	.endm						\n\t"
	"							\n\t"
	"	.macro\tback_to_back_c0_hazard			\n\t"
	"	_ehb						\n\t"
	"	.endm");

#define irq_enable_hazard()						\
	__asm__ __volatile__(						\
	"irq_enable_hazard")

#define irq_disable_hazard()						\
	__asm__ __volatile__(						\
	"irq_disable_hazard")

#define back_to_back_c0_hazard()					\
	__asm__ __volatile__(						\
	"back_to_back_c0_hazard")

#elif defined(CONFIG_CPU_R10000) || defined(CONFIG_CPU_RM9000) || \
      defined(CONFIG_CPU_SB1)

/*
 * R10000 rocks - all hazards handled in hardware, so this becomes a nobrainer.
 */

__asm__(
	"	.macro\tirq_enable_hazard			\n\t"
	"	.endm						\n\t"
	"							\n\t"
	"	.macro\tirq_disable_hazard			\n\t"
	"	.endm");

#define irq_enable_hazard()	do { } while (0)
#define irq_disable_hazard()	do { } while (0)

#define back_to_back_c0_hazard()	do { } while (0)

#else

/*
 * Default for classic MIPS processors.  Assume worst case hazards but don't
 * care about the irq_enable_hazard - sooner or later the hardware will
 * enable it and we don't care when exactly.
 */

__asm__(
	"	#						\n\t"
	"	# There is a hazard but we do not care		\n\t"
	"	#						\n\t"
	"	.macro\tirq_enable_hazard			\n\t"
	"	.endm						\n\t"
	"							\n\t"
	"	.macro\tirq_disable_hazard			\n\t"
	"	_ssnop; _ssnop; _ssnop				\n\t"
	"	.endm");

#define irq_enable_hazard()	do { } while (0)
#define irq_disable_hazard()						\
	__asm__ __volatile__(						\
	"irq_disable_hazard")

#define back_to_back_c0_hazard()					\
	__asm__ __volatile__(						\
	"	.set noreorder				\n"		\
	"	nop; nop; nop				\n"		\
	"	.set reorder				\n")

#endif

#ifdef CONFIG_CPU_MIPSR2
/*
 * gcc has a tradition of misscompiling the previous construct using the
 * address of a label as argument to inline assembler.  Gas otoh has the
 * annoying difference between la and dla which are only usable for 32-bit
 * rsp. 64-bit code, so can't be used without conditional compilation.
 * The alterantive is switching the assembler to 64-bit code which happens
 * to work right even for 32-bit code ...
 */
#define instruction_hazard()						\
do {									\
	unsigned long tmp;						\
									\
	__asm__ __volatile__(						\
	"	.set	mips64r2				\n"	\
	"	dla	%0, 1f					\n"	\
	"	jr.hb	%0					\n"	\
	"	.set	mips0					\n"	\
	"1:							\n"	\
	: "=r" (tmp));							\
} while (0)

#else
#define instruction_hazard() do { } while (0)
#endif

#endif /* __ASSEMBLY__ */

#endif /* _ASM_HAZARDS_H */
