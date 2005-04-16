/*
 * linux/include/asm-arm/vfpmacros.h
 *
 * Assembler-only file containing VFP macros and register definitions.
 */
#include "vfp.h"

@ Macros to allow building with old toolkits (with no VFP support)
	.macro	VFPFMRX, rd, sysreg, cond
	MRC\cond	p10, 7, \rd, \sysreg, cr0, 0	@ FMRX	\rd, \sysreg
	.endm

	.macro	VFPFMXR, sysreg, rd, cond
	MCR\cond	p10, 7, \rd, \sysreg, cr0, 0	@ FMXR	\sysreg, \rd
	.endm

	@ read all the working registers back into the VFP
	.macro	VFPFLDMIA, base
	LDC	p11, cr0, [\base],#33*4		    @ FLDMIAX \base!, {d0-d15}
	.endm

	@ write all the working registers out of the VFP
	.macro	VFPFSTMIA, base
	STC	p11, cr0, [\base],#33*4		    @ FSTMIAX \base!, {d0-d15}
	.endm
