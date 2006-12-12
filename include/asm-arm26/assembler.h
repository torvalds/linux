/*
 * linux/include/asm-arm26/assembler.h
 *
 * This file contains arm architecture specific defines
 * for the different processors.
 *
 * Do not include any C declarations in this file - it is included by
 * assembler source.
 */
#ifndef __ASSEMBLY__
#error "Only include this from assembly code"
#endif

/*
 * Endian independent macros for shifting bytes within registers.
 */
#define pull            lsr
#define push            lsl
#define byte(x)         (x*8)

#ifdef __STDC__
#define LOADREGS(cond, base, reglist...)\
	ldm##cond	base,reglist^

#define RETINSTR(instr, regs...)\
	instr##s	regs
#else
#define LOADREGS(cond, base, reglist...)\
	ldm/**/cond	base,reglist^

#define RETINSTR(instr, regs...)\
	instr/**/s	regs
#endif

#define MODENOP\
	mov	r0, r0

#define MODE(savereg,tmpreg,mode) \
	mov	savereg, pc; \
	bic	tmpreg, savereg, $0x0c000003; \
	orr	tmpreg, tmpreg, $mode; \
	teqp	tmpreg, $0

#define RESTOREMODE(savereg) \
	teqp	savereg, $0

#define SAVEIRQS(tmpreg)

#define RESTOREIRQS(tmpreg)

#define DISABLEIRQS(tmpreg)\
	teqp	pc, $0x08000003

#define ENABLEIRQS(tmpreg)\
	teqp	pc, $0x00000003

#define USERMODE(tmpreg)\
	teqp	pc, $0x00000000;\
	mov	r0, r0

#define SVCMODE(tmpreg)\
	teqp	pc, $0x00000003;\
	mov	r0, r0


/*
 * Save the current IRQ state and disable IRQs
 * Note that this macro assumes FIQs are enabled, and
 * that the processor is in SVC mode.
 */
	.macro	save_and_disable_irqs, oldcpsr, temp
  mov \oldcpsr, pc
  orr \temp, \oldcpsr, #0x08000000
  teqp \temp, #0
  .endm

/*
 * Restore interrupt state previously stored in
 * a register
 * ** Actually do nothing on Arc - hope that the caller uses a MOVS PC soon
 * after!
 */
	.macro	restore_irqs, oldcpsr
  @ This be restore_irqs
  .endm

/*
 * These two are used to save LR/restore PC over a user-based access.
 * The old 26-bit architecture requires that we save lr (R14)
 */
	.macro	save_lr
	str	lr, [sp, #-4]!
	.endm

	.macro	restore_pc
	ldmfd	sp!, {pc}^
	.endm

#define USER(x...)				\
9999:	x;					\
	.section __ex_table,"a";		\
	.align	3;				\
	.long	9999b,9001f;			\
	.previous


