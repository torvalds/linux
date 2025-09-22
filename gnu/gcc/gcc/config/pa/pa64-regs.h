/* Configuration for GCC-compiler for PA-RISC.
   Copyright (C) 1999, 2000, 2003, 2004 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* Standard register usage.

   It is safe to refer to actual register numbers in this file.  */

/* Number of actual hardware registers.
   The hardware registers are assigned numbers for the compiler
   from 0 to just below FIRST_PSEUDO_REGISTER.
   All registers that the compiler knows about must be given numbers,
   even those that are not normally considered general registers.

   HP-PA 2.0w has 32 fullword registers and 32 floating point
   registers. However, the floating point registers behave
   differently: the left and right halves of registers are addressable
   as 32 bit registers.

   Due to limitations within GCC itself, we do not expose the left/right
   half addressability when in wide mode.  This is not a major performance
   issue as using the halves independently triggers false dependency stalls
   anyway.  */

#define FIRST_PSEUDO_REGISTER 61  /* 32 general regs + 28 fp regs +
				     + 1 shift reg */

/* 1 for registers that have pervasive standard uses
   and are not available for the register allocator.

   On the HP-PA, these are:
   Reg 0	= 0 (hardware). However, 0 is used for condition code,
                  so is not fixed.
   Reg 1	= ADDIL target/Temporary (hardware).
   Reg 2	= Return Pointer
   Reg 3	= Frame Pointer
   Reg 4	= Frame Pointer (>8k varying frame with HP compilers only)
   Reg 4-18	= Preserved Registers
   Reg 19	= Linkage Table Register in HPUX 8.0 shared library scheme.
   Reg 20-22	= Temporary Registers
   Reg 23-26	= Temporary/Parameter Registers
   Reg 27	= Global Data Pointer (hp)
   Reg 28	= Temporary/Return Value register
   Reg 29	= Temporary/Static Chain/Return Value register #2
   Reg 30	= stack pointer
   Reg 31	= Temporary/Millicode Return Pointer (hp)

   Freg 0-3	= Status Registers	-- Not known to the compiler.
   Freg 4-7	= Arguments/Return Value
   Freg 8-11	= Temporary Registers
   Freg 12-21	= Preserved Registers
   Freg 22-31 = Temporary Registers

*/

#define FIXED_REGISTERS  \
 {0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 1, 0, 0, 1, 0, \
  /* fp registers */	  \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 0,		  \
  /* shift register */	  \
  0}

/* 1 for registers not available across function calls.
   These must include the FIXED_REGISTERS and also any
   registers that can be used without being saved.
   The latter must include the registers where values are returned
   and the register where structure-value addresses are passed.
   Aside from that, you can include as many other registers as you like.  */
#define CALL_USED_REGISTERS  \
 {1, 1, 1, 0, 0, 0, 0, 0, \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 1, 1, 1, 1, 1, \
  1, 1, 1, 1, 1, 1, 1, 1, \
  /* fp registers */	  \
  1, 1, 1, 1, 1, 1, 1, 1, \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 1, 1, 1, 1, 1, 1, \
  1, 1, 1, 1, 		  \
  /* shift register */    \
  1}

#define CONDITIONAL_REGISTER_USAGE \
{						\
  int i;					\
  if (TARGET_DISABLE_FPREGS || TARGET_SOFT_FLOAT)\
    {						\
      for (i = FP_REG_FIRST; i <= FP_REG_LAST; i++)\
	fixed_regs[i] = call_used_regs[i] = 1; 	\
    }						\
  if (flag_pic)					\
    fixed_regs[PIC_OFFSET_TABLE_REGNUM] = 1;	\
}

/* Allocate the call used registers first.  This should minimize
   the number of registers that need to be saved (as call used
   registers will generally not be allocated across a call).

   Experimentation has shown slightly better results by allocating
   FP registers first.  We allocate the caller-saved registers more
   or less in reverse order to their allocation as arguments.  */

#define REG_ALLOC_ORDER \
 {					\
  /* caller-saved fp regs.  */		\
  50, 51, 52, 53, 54, 55, 56, 57,	\
  58, 59, 39, 38, 37, 36, 35, 34,	\
  33, 32,				\
  /* caller-saved general regs.  */	\
  28, 31, 19, 20, 21, 22, 23, 24,	\
  25, 26, 29,  2,			\
  /* callee-saved fp regs.  */		\
  40, 41, 42, 43, 44, 45, 46, 47,	\
  48, 49,				\
  /* callee-saved general regs.  */	\
   3,  4,  5,  6,  7,  8,  9, 10, 	\
  11, 12, 13, 14, 15, 16, 17, 18,	\
  /* special registers.  */		\
   1, 27, 30,  0, 60}


/* Return number of consecutive hard regs needed starting at reg REGNO
   to hold something of mode MODE.
   This is ordinarily the length in words of a value of mode MODE
   but can be less for certain modes in special long registers.

   For PA64, GPRs and FPRs hold 64 bits worth.  We ignore the 32-bit
   addressability of the FPRs and pretend each register holds precisely
   WORD_SIZE bits.  Note that SCmode values are placed in a single FPR.
   Thus, any patterns defined to operate on these values would have to
   use the 32-bit addressability of the FPR registers.  */
#define HARD_REGNO_NREGS(REGNO, MODE)					\
  ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* These are the valid FP modes.  */
#define VALID_FP_MODE_P(MODE)						\
  ((MODE) == SFmode || (MODE) == DFmode					\
   || (MODE) == SCmode || (MODE) == DCmode				\
   || (MODE) == QImode || (MODE) == HImode || (MODE) == SImode		\
   || (MODE) == DImode)

/* Value is 1 if hard register REGNO can hold a value of machine-mode MODE.
   On the HP-PA, the cpu registers can hold any mode.  We
   force this to be an even register is it cannot hold the full mode.  */
#define HARD_REGNO_MODE_OK(REGNO, MODE) \
  ((REGNO) == 0								\
   ? (MODE) == CCmode || (MODE) == CCFPmode				\
   /* Make wide modes be in aligned registers.  */			\
   : FP_REGNO_P (REGNO)							\
     ? (VALID_FP_MODE_P (MODE)						\
	&& (GET_MODE_SIZE (MODE) <= 8					\
	    || (GET_MODE_SIZE (MODE) == 16 && ((REGNO) & 1) == 0)	\
	    || (GET_MODE_SIZE (MODE) == 32 && ((REGNO) & 3) == 0)))	\
   : (GET_MODE_SIZE (MODE) <= UNITS_PER_WORD				\
      || (GET_MODE_SIZE (MODE) == 2 * UNITS_PER_WORD			\
	  && ((((REGNO) & 1) == 1 && (REGNO) <= 25) || (REGNO) == 28))	\
      || (GET_MODE_SIZE (MODE) == 4 * UNITS_PER_WORD			\
	  && ((REGNO) & 3) == 3 && (REGNO) <= 23)))

/* How to renumber registers for dbx and gdb.

   Registers 0  - 31 remain unchanged.

   Registers 32 - 59 are mapped to 72, 74, 76 ...

   Register 60 is mapped to 32.  */
#define DBX_REGISTER_NUMBER(REGNO) \
  ((REGNO) <= 31 ? (REGNO) : ((REGNO) < 60 ? (REGNO - 32) * 2 + 72 : 32))

/* We must not use the DBX register numbers for the DWARF 2 CFA column
   numbers because that maps to numbers beyond FIRST_PSEUDO_REGISTER.
   Instead use the identity mapping.  */
#define DWARF_FRAME_REGNUM(REG) REG

/* Define the classes of registers for register constraints in the
   machine description.  Also define ranges of constants.

   One of the classes must always be named ALL_REGS and include all hard regs.
   If there is more than one class, another class must be named NO_REGS
   and contain no registers.

   The name GENERAL_REGS must be the name of a class (or an alias for
   another name such as ALL_REGS).  This is the class of registers
   that is allowed by "g" or "r" in a register constraint.
   Also, registers outside this class are allocated only when
   instructions express preferences for them.

   The classes must be numbered in nondecreasing order; that is,
   a larger-numbered class must never be contained completely
   in a smaller-numbered class.

   For any two classes, it is very desirable that there be another
   class that represents their union.  */

  /* The HP-PA has four kinds of registers: general regs, 1.0 fp regs,
     1.1 fp regs, and the high 1.1 fp regs, to which the operands of
     fmpyadd and fmpysub are restricted.  */

enum reg_class { NO_REGS, R1_REGS, GENERAL_REGS, FPUPPER_REGS, FP_REGS,
		 GENERAL_OR_FP_REGS, SHIFT_REGS, ALL_REGS, LIM_REG_CLASSES};

#define N_REG_CLASSES (int) LIM_REG_CLASSES

/* Give names of register classes as strings for dump file.  */

#define REG_CLASS_NAMES \
  {"NO_REGS", "R1_REGS", "GENERAL_REGS", "FPUPPER_REGS", "FP_REGS", \
   "GENERAL_OR_FP_REGS", "SHIFT_REGS", "ALL_REGS"}

/* Define which registers fit in which classes.
   This is an initializer for a vector of HARD_REG_SET
   of length N_REG_CLASSES. Register 0, the "condition code" register,
   is in no class.  */

#define REG_CLASS_CONTENTS	\
 {{0x00000000, 0x00000000},	/* NO_REGS */			\
  {0x00000002, 0x00000000},	/* R1_REGS */			\
  {0xfffffffe, 0x00000000},	/* GENERAL_REGS */		\
  {0x00000000, 0x00000000},	/* FPUPPER_REGS */			\
  {0x00000000, 0x0fffffff},	/* FP_REGS */			\
  {0xfffffffe, 0x0fffffff},	/* GENERAL_OR_FP_REGS */	\
  {0x00000000, 0x10000000},	/* SHIFT_REGS */		\
  {0xfffffffe, 0x1fffffff}}	/* ALL_REGS */

/* Defines invalid mode changes.

   SImode loads to floating-point registers are not zero-extended.
   The definition for LOAD_EXTEND_OP specifies that integer loads
   narrower than BITS_PER_WORD will be zero-extended.  As a result,
   we inhibit changes from SImode unless they are to a mode that is
   identical in size.  */

#define CANNOT_CHANGE_MODE_CLASS(FROM, TO, CLASS)		\
  ((FROM) == SImode && GET_MODE_SIZE (FROM) != GET_MODE_SIZE (TO)       \
   ? reg_classes_intersect_p (CLASS, FP_REGS) : 0)

/* Return the class number of the smallest class containing
   reg number REGNO.  This could be a conditional expression
   or could index an array.  */

#define REGNO_REG_CLASS(REGNO)						\
  ((REGNO) == 0 ? NO_REGS 						\
   : (REGNO) == 1 ? R1_REGS						\
   : (REGNO) < 32 ? GENERAL_REGS					\
   : (REGNO) < 60 ? FP_REGS						\
   : SHIFT_REGS)


/* Get reg_class from a letter such as appears in the machine description.  */
/* Keep 'x' for backward compatibility with user asm.  */
#define REG_CLASS_FROM_LETTER(C) \
  ((C) == 'f' ? FP_REGS :					\
   (C) == 'y' ? FP_REGS :					\
   (C) == 'x' ? FP_REGS :					\
   (C) == 'q' ? SHIFT_REGS :					\
   (C) == 'a' ? R1_REGS :					\
   (C) == 'Z' ? ALL_REGS : NO_REGS)


/* Return the maximum number of consecutive registers
   needed to represent mode MODE in a register of class CLASS.  */
#define CLASS_MAX_NREGS(CLASS, MODE)					\
  ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* 1 if N is a possible register number for function argument passing.  */

#define FUNCTION_ARG_REGNO_P(N) \
  ((((N) >= 19) && (N) <= 26) \
   || (! TARGET_SOFT_FLOAT && (N) >= 32 && (N) <= 39))

/* How to refer to registers in assembler output.
   This sequence is indexed by compiler's hard-register-number (see above).  */

#define REGISTER_NAMES \
{"%r0",   "%r1",    "%r2",   "%r3",    "%r4",   "%r5",    "%r6",   "%r7",    \
 "%r8",   "%r9",    "%r10",  "%r11",   "%r12",  "%r13",   "%r14",  "%r15",   \
 "%r16",  "%r17",   "%r18",  "%r19",   "%r20",  "%r21",   "%r22",  "%r23",   \
 "%r24",  "%r25",   "%r26",  "%r27",   "%r28",  "%r29",   "%r30",  "%r31",   \
 "%fr4",  "%fr5",   "%fr6",  "%fr7",   "%fr8",  "%fr9",   "%fr10", "%fr11",  \
 "%fr12", "%fr13",  "%fr14", "%fr15",  "%fr16", "%fr17",  "%fr18", "%fr19",  \
 "%fr20", "%fr21",  "%fr22", "%fr23",  "%fr24", "%fr25",  "%fr26", "%fr27",  \
 "%fr28", "%fr29",  "%fr30", "%fr31", "SAR"}

#define ADDITIONAL_REGISTER_NAMES \
 {{"%cr11",60}}

#define FP_SAVED_REG_LAST 49
#define FP_SAVED_REG_FIRST 40
#define FP_REG_STEP 1
#define FP_REG_FIRST 32
#define FP_REG_LAST 59
