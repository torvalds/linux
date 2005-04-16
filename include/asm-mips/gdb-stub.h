/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 Andreas Busse
 * Copyright (C) 2003 Ralf Baechle
 */
#ifndef _ASM_GDB_STUB_H
#define _ASM_GDB_STUB_H


/*
 * important register numbers
 */

#define REG_EPC			37
#define REG_FP			72
#define REG_SP			29

/*
 * Stack layout for the GDB exception handler
 * Derived from the stack layout described in asm-mips/stackframe.h
 *
 * The first PTRSIZE*6 bytes are argument save space for C subroutines.
 */
#define NUMREGS			90

#define GDB_FR_REG0		(PTRSIZE*6)			/* 0 */
#define GDB_FR_REG1		((GDB_FR_REG0) + LONGSIZE)	/* 1 */
#define GDB_FR_REG2		((GDB_FR_REG1) + LONGSIZE)	/* 2 */
#define GDB_FR_REG3		((GDB_FR_REG2) + LONGSIZE)	/* 3 */
#define GDB_FR_REG4		((GDB_FR_REG3) + LONGSIZE)	/* 4 */
#define GDB_FR_REG5		((GDB_FR_REG4) + LONGSIZE)	/* 5 */
#define GDB_FR_REG6		((GDB_FR_REG5) + LONGSIZE)	/* 6 */
#define GDB_FR_REG7		((GDB_FR_REG6) + LONGSIZE)	/* 7 */
#define GDB_FR_REG8		((GDB_FR_REG7) + LONGSIZE)	/* 8 */
#define GDB_FR_REG9	        ((GDB_FR_REG8) + LONGSIZE)	/* 9 */
#define GDB_FR_REG10		((GDB_FR_REG9) + LONGSIZE)	/* 10 */
#define GDB_FR_REG11		((GDB_FR_REG10) + LONGSIZE)	/* 11 */
#define GDB_FR_REG12		((GDB_FR_REG11) + LONGSIZE)	/* 12 */
#define GDB_FR_REG13		((GDB_FR_REG12) + LONGSIZE)	/* 13 */
#define GDB_FR_REG14		((GDB_FR_REG13) + LONGSIZE)	/* 14 */
#define GDB_FR_REG15		((GDB_FR_REG14) + LONGSIZE)	/* 15 */
#define GDB_FR_REG16		((GDB_FR_REG15) + LONGSIZE)	/* 16 */
#define GDB_FR_REG17		((GDB_FR_REG16) + LONGSIZE)	/* 17 */
#define GDB_FR_REG18		((GDB_FR_REG17) + LONGSIZE)	/* 18 */
#define GDB_FR_REG19		((GDB_FR_REG18) + LONGSIZE)	/* 19 */
#define GDB_FR_REG20		((GDB_FR_REG19) + LONGSIZE)	/* 20 */
#define GDB_FR_REG21		((GDB_FR_REG20) + LONGSIZE)	/* 21 */
#define GDB_FR_REG22		((GDB_FR_REG21) + LONGSIZE)	/* 22 */
#define GDB_FR_REG23		((GDB_FR_REG22) + LONGSIZE)	/* 23 */
#define GDB_FR_REG24		((GDB_FR_REG23) + LONGSIZE)	/* 24 */
#define GDB_FR_REG25		((GDB_FR_REG24) + LONGSIZE)	/* 25 */
#define GDB_FR_REG26		((GDB_FR_REG25) + LONGSIZE)	/* 26 */
#define GDB_FR_REG27		((GDB_FR_REG26) + LONGSIZE)	/* 27 */
#define GDB_FR_REG28		((GDB_FR_REG27) + LONGSIZE)	/* 28 */
#define GDB_FR_REG29		((GDB_FR_REG28) + LONGSIZE)	/* 29 */
#define GDB_FR_REG30		((GDB_FR_REG29) + LONGSIZE)	/* 30 */
#define GDB_FR_REG31		((GDB_FR_REG30) + LONGSIZE)	/* 31 */

/*
 * Saved special registers
 */
#define GDB_FR_STATUS		((GDB_FR_REG31) + LONGSIZE)	/* 32 */
#define GDB_FR_LO		((GDB_FR_STATUS) + LONGSIZE)	/* 33 */
#define GDB_FR_HI		((GDB_FR_LO) + LONGSIZE)	/* 34 */
#define GDB_FR_BADVADDR		((GDB_FR_HI) + LONGSIZE)	/* 35 */
#define GDB_FR_CAUSE		((GDB_FR_BADVADDR) + LONGSIZE)	/* 36 */
#define GDB_FR_EPC		((GDB_FR_CAUSE) + LONGSIZE)	/* 37 */

/*
 * Saved floating point registers
 */
#define GDB_FR_FPR0		((GDB_FR_EPC) + LONGSIZE)	/* 38 */
#define GDB_FR_FPR1		((GDB_FR_FPR0) + LONGSIZE)	/* 39 */
#define GDB_FR_FPR2		((GDB_FR_FPR1) + LONGSIZE)	/* 40 */
#define GDB_FR_FPR3		((GDB_FR_FPR2) + LONGSIZE)	/* 41 */
#define GDB_FR_FPR4		((GDB_FR_FPR3) + LONGSIZE)	/* 42 */
#define GDB_FR_FPR5		((GDB_FR_FPR4) + LONGSIZE)	/* 43 */
#define GDB_FR_FPR6		((GDB_FR_FPR5) + LONGSIZE)	/* 44 */
#define GDB_FR_FPR7		((GDB_FR_FPR6) + LONGSIZE)	/* 45 */
#define GDB_FR_FPR8		((GDB_FR_FPR7) + LONGSIZE)	/* 46 */
#define GDB_FR_FPR9		((GDB_FR_FPR8) + LONGSIZE)	/* 47 */
#define GDB_FR_FPR10		((GDB_FR_FPR9) + LONGSIZE)	/* 48 */
#define GDB_FR_FPR11		((GDB_FR_FPR10) + LONGSIZE)	/* 49 */
#define GDB_FR_FPR12		((GDB_FR_FPR11) + LONGSIZE)	/* 50 */
#define GDB_FR_FPR13		((GDB_FR_FPR12) + LONGSIZE)	/* 51 */
#define GDB_FR_FPR14		((GDB_FR_FPR13) + LONGSIZE)	/* 52 */
#define GDB_FR_FPR15		((GDB_FR_FPR14) + LONGSIZE)	/* 53 */
#define GDB_FR_FPR16		((GDB_FR_FPR15) + LONGSIZE)	/* 54 */
#define GDB_FR_FPR17		((GDB_FR_FPR16) + LONGSIZE)	/* 55 */
#define GDB_FR_FPR18		((GDB_FR_FPR17) + LONGSIZE)	/* 56 */
#define GDB_FR_FPR19		((GDB_FR_FPR18) + LONGSIZE)	/* 57 */
#define GDB_FR_FPR20		((GDB_FR_FPR19) + LONGSIZE)	/* 58 */
#define GDB_FR_FPR21		((GDB_FR_FPR20) + LONGSIZE)	/* 59 */
#define GDB_FR_FPR22		((GDB_FR_FPR21) + LONGSIZE)	/* 60 */
#define GDB_FR_FPR23		((GDB_FR_FPR22) + LONGSIZE)	/* 61 */
#define GDB_FR_FPR24		((GDB_FR_FPR23) + LONGSIZE)	/* 62 */
#define GDB_FR_FPR25		((GDB_FR_FPR24) + LONGSIZE)	/* 63 */
#define GDB_FR_FPR26		((GDB_FR_FPR25) + LONGSIZE)	/* 64 */
#define GDB_FR_FPR27		((GDB_FR_FPR26) + LONGSIZE)	/* 65 */
#define GDB_FR_FPR28		((GDB_FR_FPR27) + LONGSIZE)	/* 66 */
#define GDB_FR_FPR29		((GDB_FR_FPR28) + LONGSIZE)	/* 67 */
#define GDB_FR_FPR30		((GDB_FR_FPR29) + LONGSIZE)	/* 68 */
#define GDB_FR_FPR31		((GDB_FR_FPR30) + LONGSIZE)	/* 69 */

#define GDB_FR_FSR		((GDB_FR_FPR31) + LONGSIZE)	/* 70 */
#define GDB_FR_FIR		((GDB_FR_FSR) + LONGSIZE)	/* 71 */
#define GDB_FR_FRP		((GDB_FR_FIR) + LONGSIZE)	/* 72 */

#define GDB_FR_DUMMY		((GDB_FR_FRP) + LONGSIZE)	/* 73, unused ??? */

/*
 * Again, CP0 registers
 */
#define GDB_FR_CP0_INDEX	((GDB_FR_DUMMY) + LONGSIZE)	/* 74 */
#define GDB_FR_CP0_RANDOM	((GDB_FR_CP0_INDEX) + LONGSIZE)	/* 75 */
#define GDB_FR_CP0_ENTRYLO0	((GDB_FR_CP0_RANDOM) + LONGSIZE)/* 76 */
#define GDB_FR_CP0_ENTRYLO1	((GDB_FR_CP0_ENTRYLO0) + LONGSIZE)/* 77 */
#define GDB_FR_CP0_CONTEXT	((GDB_FR_CP0_ENTRYLO1) + LONGSIZE)/* 78 */
#define GDB_FR_CP0_PAGEMASK	((GDB_FR_CP0_CONTEXT) + LONGSIZE)/* 79 */
#define GDB_FR_CP0_WIRED	((GDB_FR_CP0_PAGEMASK) + LONGSIZE)/* 80 */
#define GDB_FR_CP0_REG7		((GDB_FR_CP0_WIRED) + LONGSIZE)	/* 81 */
#define GDB_FR_CP0_REG8		((GDB_FR_CP0_REG7) + LONGSIZE)	/* 82 */
#define GDB_FR_CP0_REG9		((GDB_FR_CP0_REG8) + LONGSIZE)	/* 83 */
#define GDB_FR_CP0_ENTRYHI	((GDB_FR_CP0_REG9) + LONGSIZE)	/* 84 */
#define GDB_FR_CP0_REG11	((GDB_FR_CP0_ENTRYHI) + LONGSIZE)/* 85 */
#define GDB_FR_CP0_REG12	((GDB_FR_CP0_REG11) + LONGSIZE)	/* 86 */
#define GDB_FR_CP0_REG13	((GDB_FR_CP0_REG12) + LONGSIZE)	/* 87 */
#define GDB_FR_CP0_REG14	((GDB_FR_CP0_REG13) + LONGSIZE)	/* 88 */
#define GDB_FR_CP0_PRID		((GDB_FR_CP0_REG14) + LONGSIZE)	/* 89 */

#define GDB_FR_SIZE		((((GDB_FR_CP0_PRID) + LONGSIZE) + (PTRSIZE-1)) & ~(PTRSIZE-1))

#ifndef __ASSEMBLY__

/*
 * This is the same as above, but for the high-level
 * part of the GDB stub.
 */

struct gdb_regs {
	/*
	 * Pad bytes for argument save space on the stack
	 * 24/48 Bytes for 32/64 bit code
	 */
	unsigned long pad0[6];

	/*
	 * saved main processor registers
	 */
	long	 reg0,  reg1,  reg2,  reg3,  reg4,  reg5,  reg6,  reg7;
	long	 reg8,  reg9, reg10, reg11, reg12, reg13, reg14, reg15;
	long	reg16, reg17, reg18, reg19, reg20, reg21, reg22, reg23;
	long	reg24, reg25, reg26, reg27, reg28, reg29, reg30, reg31;

	/*
	 * Saved special registers
	 */
	long	cp0_status;
	long	lo;
	long	hi;
	long	cp0_badvaddr;
	long	cp0_cause;
	long	cp0_epc;

	/*
	 * Saved floating point registers
	 */
	long	fpr0,  fpr1,  fpr2,  fpr3,  fpr4,  fpr5,  fpr6,  fpr7;
	long	fpr8,  fpr9,  fpr10, fpr11, fpr12, fpr13, fpr14, fpr15;
	long	fpr16, fpr17, fpr18, fpr19, fpr20, fpr21, fpr22, fpr23;
	long	fpr24, fpr25, fpr26, fpr27, fpr28, fpr29, fpr30, fpr31;

	long	cp1_fsr;
	long	cp1_fir;

	/*
	 * Frame pointer
	 */
	long	frame_ptr;
	long    dummy;		/* unused */

	/*
	 * saved cp0 registers
	 */
	long	cp0_index;
	long	cp0_random;
	long	cp0_entrylo0;
	long	cp0_entrylo1;
	long	cp0_context;
	long	cp0_pagemask;
	long	cp0_wired;
	long	cp0_reg7;
	long	cp0_reg8;
	long	cp0_reg9;
	long	cp0_entryhi;
	long	cp0_reg11;
	long	cp0_reg12;
	long	cp0_reg13;
	long	cp0_reg14;
	long	cp0_prid;
};

/*
 * Prototypes
 */

extern int kgdb_enabled;
void set_debug_traps(void);
void set_async_breakpoint(unsigned long *epc);

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_GDB_STUB_H */
