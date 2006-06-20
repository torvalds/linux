/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003, 2004 Ralf Baechle <ralf@linux-mips.org>
 * Copyright (C) MIPS Technologies, Inc.
 *   written by Ralf Baechle <ralf@linux-mips.org>
 */
#ifndef _ASM_HAZARDS_H
#define _ASM_HAZARDS_H


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

#elif defined(CONFIG_CPU_R10000) || defined(CONFIG_CPU_RM9000)

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
	"	.macro	_ssnop					\n"
	"	sll	$0, $0, 1				\n"
	"	.endm						\n"
	"							\n"
	"	.macro	_ehb					\n"
	"	sll	$0, $0, 3				\n"
	"	.endm						\n");

#ifdef CONFIG_CPU_RM9000

/*
 * RM9000 hazards.  When the JTLB is updated by tlbwi or tlbwr, a subsequent
 * use of the JTLB for instructions should not occur for 4 cpu cycles and use
 * for data translations should not occur for 3 cpu cycles.
 */

#define mtc0_tlbw_hazard()						\
	__asm__ __volatile__(						\
	"	.set	mips32					\n"	\
	"	_ssnop						\n"	\
	"	_ssnop						\n"	\
	"	_ssnop						\n"	\
	"	_ssnop						\n"	\
	"	.set	mips0					\n")

#define tlbw_use_hazard()						\
	__asm__ __volatile__(						\
	"	.set	mips32					\n"	\
	"	_ssnop						\n"	\
	"	_ssnop						\n"	\
	"	_ssnop						\n"	\
	"	_ssnop						\n"	\
	"	.set	mips0					\n")

#else

/*
 * Overkill warning ...
 */
#define mtc0_tlbw_hazard()						\
	__asm__ __volatile__(						\
	"	.set	noreorder				\n"	\
	"	nop						\n"	\
	"	nop						\n"	\
	"	nop						\n"	\
	"	nop						\n"	\
	"	nop						\n"	\
	"	nop						\n"	\
	"	.set	reorder					\n")

#define tlbw_use_hazard()						\
	__asm__ __volatile__(						\
	"	.set	noreorder				\n"	\
	"	nop						\n"	\
	"	nop						\n"	\
	"	nop						\n"	\
	"	nop						\n"	\
	"	nop						\n"	\
	"	nop						\n"	\
	"	.set	reorder					\n")

#endif

/*
 * Interrupt enable/disable hazards
 * Some processors have hazards when modifying
 * the status register to change the interrupt state
 */

#ifdef CONFIG_CPU_MIPSR2

__asm__("	.macro	irq_enable_hazard			\n"
	"	_ehb						\n"
	"	.endm						\n"
	"							\n"
	"	.macro	irq_disable_hazard			\n"
	"	_ehb						\n"
	"	.endm						\n");

#elif defined(CONFIG_CPU_R10000) || defined(CONFIG_CPU_RM9000)

/*
 * R10000 rocks - all hazards handled in hardware, so this becomes a nobrainer.
 */

__asm__(
	"	.macro	irq_enable_hazard			\n"
	"	.endm						\n"
	"							\n"
	"	.macro	irq_disable_hazard			\n"
	"	.endm						\n");

#else

/*
 * Default for classic MIPS processors.  Assume worst case hazards but don't
 * care about the irq_enable_hazard - sooner or later the hardware will
 * enable it and we don't care when exactly.
 */

__asm__(
	"	#						\n"
	"	# There is a hazard but we do not care		\n"
	"	#						\n"
	"	.macro\tirq_enable_hazard			\n"
	"	.endm						\n"
	"							\n"
	"	.macro\tirq_disable_hazard			\n"
	"	_ssnop						\n"
	"	_ssnop						\n"
	"	_ssnop						\n"
	"	.endm						\n");

#endif

#define irq_enable_hazard()						\
	__asm__ __volatile__("irq_enable_hazard")
#define irq_disable_hazard()						\
	__asm__ __volatile__("irq_disable_hazard")


/*
 * Back-to-back hazards -
 *
 * What is needed to separate a move to cp0 from a subsequent read from the
 * same cp0 register?
 */
#ifdef CONFIG_CPU_MIPSR2

__asm__("	.macro	back_to_back_c0_hazard			\n"
	"	_ehb						\n"
	"	.endm						\n");

#elif defined(CONFIG_CPU_R10000) || defined(CONFIG_CPU_RM9000) || \
      defined(CONFIG_CPU_SB1)

__asm__("	.macro	back_to_back_c0_hazard			\n"
	"	.endm						\n");

#else

__asm__("	.macro	back_to_back_c0_hazard			\n"
	"	.set	noreorder				\n"
	"	_ssnop						\n"
	"	_ssnop						\n"
	"	_ssnop						\n"
	"	.set	reorder					\n"
	"	.endm");

#endif

#define back_to_back_c0_hazard()					\
	__asm__ __volatile__("back_to_back_c0_hazard")


/*
 * Instruction execution hazard
 */
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

extern void mips_ihb(void);

#endif /* __ASSEMBLY__ */

#endif /* _ASM_HAZARDS_H */
