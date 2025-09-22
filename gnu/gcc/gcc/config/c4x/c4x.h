/* Definitions of target machine for GNU compiler.  TMS320C[34]x
   Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
   2003, 2004, 2005 Free Software Foundation, Inc.

   Contributed by Michael Hayes (m.hayes@elec.canterbury.ac.nz)
              and Herman Ten Brugge (Haj.Ten.Brugge@net.HCC.nl).

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

/* RUN-TIME TARGET SPECIFICATION.  */

#define C4x   1

#define TARGET_CPU_CPP_BUILTINS()		\
  do						\
    {						\
      extern int flag_inline_trees;		\
      if (!TARGET_SMALL)			\
	builtin_define ("_BIGMODEL");		\
      if (!TARGET_MEMPARM)			\
	builtin_define ("_REGPARM");		\
      if (flag_inline_functions)		\
	builtin_define ("_INLINE");		\
      if (TARGET_C3X)				\
	{					\
	  builtin_define ("_TMS320C3x");	\
	  builtin_define ("_C3x");		\
	  if (TARGET_C30)			\
	    {					\
	      builtin_define ("_TMS320C30");	\
	      builtin_define ("_C30");		\
	    }					\
	  else if (TARGET_C31)			\
	    {					\
	      builtin_define ("_TMS320C31");	\
	      builtin_define ("_C31");		\
	    }					\
	  else if (TARGET_C32)			\
	    {					\
	      builtin_define ("_TMS320C32");	\
	      builtin_define ("_C32");		\
	    }					\
	  else if (TARGET_C33)			\
	    {					\
	      builtin_define ("_TMS320C33");	\
	      builtin_define ("_C33");		\
	    }					\
	}					\
      else					\
	{					\
	  builtin_define ("_TMS320C4x");	\
	  builtin_define ("_C4x");		\
	  if (TARGET_C40)			\
	    {					\
	      builtin_define ("_TMS320C40");	\
	      builtin_define ("_C40");		\
	    }					\
	  else if (TARGET_C44)			\
	    {					\
	      builtin_define ("_TMS320C44");	\
	      builtin_define ("_C44");		\
	    }					\
	}					\
    }						\
  while (0)

/* Define assembler options.  */

#define ASM_SPEC "\
%{!mcpu=30:%{!mcpu=31:%{!mcpu=32:%{!mcpu=33:%{!mcpu=40:%{!mcpu=44:\
%{!m30:%{!m31:%{!m32:%{!m33:%{!m40:%{!m44:-m40}}}}}}}}}}}} \
%{mcpu=30} \
%{mcpu=31} \
%{mcpu=32} \
%{mcpu=33} \
%{mcpu=40} \
%{mcpu=44} \
%{m30} \
%{m31} \
%{m32} \
%{m33} \
%{m40} \
%{m44} \
%{mmemparm} %{mregparm} %{!mmemparm:%{!mregparm:-mregparm}} \
%{mbig} %{msmall} %{!msmall:%{!mbig:-mbig}}"

/* Define linker options.  */

#define LINK_SPEC "\
%{m30:--architecture c3x} \
%{m31:--architecture c3x} \
%{m32:--architecture c3x} \
%{m33:--architecture c3x} \
%{mcpu=30:--architecture c3x} \
%{mcpu=31:--architecture c3x} \
%{mcpu=32:--architecture c3x} \
%{mcpu=33:--architecture c3x}"

/* Specify the end file to link with.  */

#define ENDFILE_SPEC ""

/* Caveats:
   Max iteration count for RPTB/RPTS is 2^31 + 1.
   Max iteration count for DB is 2^31 + 1 for C40, but 2^23 + 1 for C30.
   RPTS blocks interrupts.  */


extern int c4x_cpu_version;		/* Cpu version C30/31/32/33/40/44.  */

#define TARGET_INLINE		(! optimize_size) /* Inline MPYI.  */
#define TARGET_SMALL_REG_CLASS	0

#define TARGET_C3X		(c4x_cpu_version >= 30 \
				 && c4x_cpu_version <= 39)

#define TARGET_C30		(c4x_cpu_version == 30)
#define TARGET_C31		(c4x_cpu_version == 31)
#define TARGET_C32		(c4x_cpu_version == 32)
#define TARGET_C33		(c4x_cpu_version == 33)
#define TARGET_C40		(c4x_cpu_version == 40)
#define TARGET_C44		(c4x_cpu_version == 44)

/* Nonzero to use load_immed_addr pattern rather than forcing memory
   addresses into memory.  */
#define TARGET_LOAD_ADDRESS	(1 || (! TARGET_C3X && ! TARGET_SMALL))

/* Nonzero to convert direct memory references into HIGH/LO_SUM pairs
   during RTL generation.  */
#define TARGET_EXPOSE_LDP	0

/* Nonzero to force loading of direct memory references into a register.  */
#define TARGET_LOAD_DIRECT_MEMS	0

/* -mrpts            allows the use of the RPTS instruction irregardless.
   -mrpts=max-cycles will use RPTS if the number of cycles is constant
   and less than max-cycles.  */

#define TARGET_RPTS_CYCLES(CYCLES) (TARGET_RPTS || (CYCLES) < c4x_rpts_cycles)

/* Sometimes certain combinations of command options do not make sense
   on a particular target machine.  You can define a macro
   `OVERRIDE_OPTIONS' to take account of this.  This macro, if
   defined, is executed once just after all the command options have
   been parsed.  */

#define OVERRIDE_OPTIONS c4x_override_options ()

/* Define this to change the optimizations performed by default.  */

#define OPTIMIZATION_OPTIONS(LEVEL,SIZE) c4x_optimization_options(LEVEL, SIZE)

/* Run Time Target Specification.  */

#define TARGET_VERSION fprintf (stderr, " (TMS320C[34]x, TI syntax)");

/* Storage Layout.  */

#define BITS_BIG_ENDIAN		0
#define BYTES_BIG_ENDIAN	0
#define WORDS_BIG_ENDIAN	0

/* Technically, we are little endian, but we put the floats out as
   whole longs and this makes GCC put them out in the right order.  */

#define FLOAT_WORDS_BIG_ENDIAN	1

/* Note the ANSI C standard requires sizeof(char) = 1.  On the C[34]x
   all integral and floating point data types are stored in memory as
   32-bits (floating point types can be stored as 40-bits in the
   extended precision registers), so sizeof(char) = sizeof(short) =
   sizeof(int) = sizeof(long) = sizeof(float) = sizeof(double) = 1.  */

#define BITS_PER_UNIT		32
#define UNITS_PER_WORD		1
#define PARM_BOUNDARY	        32
#define STACK_BOUNDARY		32
#define FUNCTION_BOUNDARY	32
#define BIGGEST_ALIGNMENT	32
#define EMPTY_FIELD_BOUNDARY	32
#define STRICT_ALIGNMENT	0
#define TARGET_FLOAT_FORMAT	C4X_FLOAT_FORMAT
#define MAX_FIXED_MODE_SIZE	64 /* HImode.  */

/* If a structure has a floating point field then force structure
   to have BLKMODE, unless it is the only field.  */
#define MEMBER_TYPE_FORCES_BLK(FIELD, MODE) \
  (TREE_CODE (TREE_TYPE (FIELD)) == REAL_TYPE && (MODE) == VOIDmode)

/* Number of bits in the high and low parts of a two stage
   load of an immediate constant.  */
#define BITS_PER_HIGH 16
#define BITS_PER_LO_SUM 16

/* Define register numbers.  */

/* Extended-precision registers.  */

#define R0_REGNO   0
#define R1_REGNO   1
#define R2_REGNO   2
#define R3_REGNO   3
#define R4_REGNO   4
#define R5_REGNO   5
#define R6_REGNO   6
#define R7_REGNO   7

/* Auxiliary (address) registers.  */

#define AR0_REGNO  8
#define AR1_REGNO  9
#define AR2_REGNO 10
#define AR3_REGNO 11
#define AR4_REGNO 12
#define AR5_REGNO 13
#define AR6_REGNO 14
#define AR7_REGNO 15

/* Data page register.  */

#define DP_REGNO  16

/* Index registers.  */

#define IR0_REGNO 17
#define IR1_REGNO 18

/* Block size register.  */

#define BK_REGNO  19

/* Stack pointer.  */

#define SP_REGNO  20

/* Status register.  */

#define ST_REGNO  21

/* Misc. interrupt registers.  */

#define DIE_REGNO 22		/* C4x only.  */
#define IE_REGNO  22		/* C3x only.  */
#define IIE_REGNO 23		/* C4x only.  */
#define IF_REGNO  23		/* C3x only.  */
#define IIF_REGNO 24		/* C4x only.  */
#define IOF_REGNO 24		/* C3x only.  */

/* Repeat block registers.  */

#define RS_REGNO  25
#define RE_REGNO  26
#define RC_REGNO  27

/* Additional extended-precision registers.  */

#define R8_REGNO  28		/* C4x only.  */
#define R9_REGNO  29		/* C4x only.  */
#define R10_REGNO 30		/* C4x only.  */
#define R11_REGNO 31		/* C4x only.  */

#define FIRST_PSEUDO_REGISTER	32

/* Extended precision registers (low set).  */

#define IS_R0R1_REGNO(r) \
     ((unsigned int)((r) - R0_REGNO) <= (R1_REGNO - R0_REGNO))
#define IS_R2R3_REGNO(r) \
     ((unsigned int)((r) - R2_REGNO) <= (R3_REGNO - R2_REGNO))   
#define IS_EXT_LOW_REGNO(r) \
     ((unsigned int)((r) - R0_REGNO) <= (R7_REGNO - R0_REGNO))   

/* Extended precision registers (high set).  */

#define IS_EXT_HIGH_REGNO(r) \
(! TARGET_C3X \
 && ((unsigned int) ((r) - R8_REGNO) <= (R11_REGNO - R8_REGNO)))

/* Address registers.  */

#define IS_AUX_REGNO(r) \
    ((unsigned int)((r) - AR0_REGNO) <= (AR7_REGNO - AR0_REGNO))   
#define IS_ADDR_REGNO(r)   IS_AUX_REGNO(r)
#define IS_DP_REGNO(r)     ((r) == DP_REGNO)
#define IS_INDEX_REGNO(r)  (((r) == IR0_REGNO) || ((r) == IR1_REGNO))
#define IS_SP_REGNO(r)     ((r) == SP_REGNO)
#define IS_BK_REGNO(r)     (TARGET_BK && (r) == BK_REGNO)

/* Misc registers.  */

#define IS_ST_REGNO(r)     ((r) == ST_REGNO)
#define IS_RC_REGNO(r)     ((r) == RC_REGNO)
#define IS_REPEAT_REGNO(r) (((r) >= RS_REGNO) && ((r) <= RC_REGNO))

/* Composite register sets.  */

#define IS_ADDR_OR_INDEX_REGNO(r) (IS_ADDR_REGNO(r) || IS_INDEX_REGNO(r))
#define IS_EXT_REGNO(r)           (IS_EXT_LOW_REGNO(r) || IS_EXT_HIGH_REGNO(r))
#define IS_STD_REGNO(r)           (IS_ADDR_OR_INDEX_REGNO(r) \
				   || IS_REPEAT_REGNO(r) \
                                   || IS_SP_REGNO(r) \
		       		   || IS_BK_REGNO(r))
#define IS_INT_REGNO(r)           (IS_EXT_REGNO(r) || IS_STD_REGNO(r))
#define IS_GROUP1_REGNO(r)        (IS_ADDR_OR_INDEX_REGNO(r) || IS_BK_REGNO(r))
#define IS_INT_CALL_SAVED_REGNO(r) (((r) == R4_REGNO) || ((r) == R5_REGNO) \
                                    || ((r) == R8_REGNO))
#define IS_FLOAT_CALL_SAVED_REGNO(r) (((r) == R6_REGNO) || ((r) == R7_REGNO))

#define IS_PSEUDO_REGNO(r)            ((r) >= FIRST_PSEUDO_REGISTER)
#define IS_R0R1_OR_PSEUDO_REGNO(r)    (IS_R0R1_REGNO(r) || IS_PSEUDO_REGNO(r))
#define IS_R2R3_OR_PSEUDO_REGNO(r)    (IS_R2R3_REGNO(r) || IS_PSEUDO_REGNO(r))
#define IS_EXT_OR_PSEUDO_REGNO(r)     (IS_EXT_REGNO(r) || IS_PSEUDO_REGNO(r))
#define IS_STD_OR_PSEUDO_REGNO(r)     (IS_STD_REGNO(r) || IS_PSEUDO_REGNO(r))
#define IS_INT_OR_PSEUDO_REGNO(r)     (IS_INT_REGNO(r) || IS_PSEUDO_REGNO(r))
#define IS_ADDR_OR_PSEUDO_REGNO(r)    (IS_ADDR_REGNO(r) || IS_PSEUDO_REGNO(r))
#define IS_INDEX_OR_PSEUDO_REGNO(r)   (IS_INDEX_REGNO(r) || IS_PSEUDO_REGNO(r))
#define IS_EXT_LOW_OR_PSEUDO_REGNO(r) (IS_EXT_LOW_REGNO(r) \
				       || IS_PSEUDO_REGNO(r))
#define IS_DP_OR_PSEUDO_REGNO(r)      (IS_DP_REGNO(r) || IS_PSEUDO_REGNO(r))
#define IS_SP_OR_PSEUDO_REGNO(r)      (IS_SP_REGNO(r) || IS_PSEUDO_REGNO(r))
#define IS_ST_OR_PSEUDO_REGNO(r)      (IS_ST_REGNO(r) || IS_PSEUDO_REGNO(r))
#define IS_RC_OR_PSEUDO_REGNO(r)      (IS_RC_REGNO(r) || IS_PSEUDO_REGNO(r))

#define IS_PSEUDO_REG(op)          (IS_PSEUDO_REGNO(REGNO(op)))
#define IS_ADDR_REG(op)            (IS_ADDR_REGNO(REGNO(op)))
#define IS_INDEX_REG(op)           (IS_INDEX_REGNO(REGNO(op)))
#define IS_GROUP1_REG(r)           (IS_GROUP1_REGNO(REGNO(op)))
#define IS_SP_REG(op)              (IS_SP_REGNO(REGNO(op)))
#define IS_STD_REG(op)             (IS_STD_REGNO(REGNO(op)))
#define IS_EXT_REG(op)             (IS_EXT_REGNO(REGNO(op)))

#define IS_R0R1_OR_PSEUDO_REG(op)  (IS_R0R1_OR_PSEUDO_REGNO(REGNO(op)))
#define IS_R2R3_OR_PSEUDO_REG(op)  (IS_R2R3_OR_PSEUDO_REGNO(REGNO(op)))
#define IS_EXT_OR_PSEUDO_REG(op)   (IS_EXT_OR_PSEUDO_REGNO(REGNO(op)))
#define IS_STD_OR_PSEUDO_REG(op)   (IS_STD_OR_PSEUDO_REGNO(REGNO(op)))
#define IS_EXT_LOW_OR_PSEUDO_REG(op) (IS_EXT_LOW_OR_PSEUDO_REGNO(REGNO(op)))
#define IS_INT_OR_PSEUDO_REG(op)   (IS_INT_OR_PSEUDO_REGNO(REGNO(op)))

#define IS_ADDR_OR_PSEUDO_REG(op)  (IS_ADDR_OR_PSEUDO_REGNO(REGNO(op)))
#define IS_INDEX_OR_PSEUDO_REG(op) (IS_INDEX_OR_PSEUDO_REGNO(REGNO(op)))
#define IS_DP_OR_PSEUDO_REG(op)    (IS_DP_OR_PSEUDO_REGNO(REGNO(op)))
#define IS_SP_OR_PSEUDO_REG(op)    (IS_SP_OR_PSEUDO_REGNO(REGNO(op)))
#define IS_ST_OR_PSEUDO_REG(op)    (IS_ST_OR_PSEUDO_REGNO(REGNO(op)))
#define IS_RC_OR_PSEUDO_REG(op)    (IS_RC_OR_PSEUDO_REGNO(REGNO(op)))

/* 1 for registers that have pervasive standard uses
   and are not available for the register allocator.  */

#define FIXED_REGISTERS \
{									\
/* R0  R1  R2  R3  R4  R5  R6  R7 AR0 AR1 AR2 AR3 AR4 AR5 AR6 AR7.  */	\
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,	\
/* DP IR0 IR1  BK  SP  ST DIE IIE IIF  RS  RE  RC  R8  R9 R10 R11.  */	\
    1,  0,  0,  0,  1,  1,  1,  1,  1,  0,  0,  0,  0,  0,  0,  0	\
}

/* 1 for registers not available across function calls.
   These must include the FIXED_REGISTERS and also any
   registers that can be used without being saved.
   The latter must include the registers where values are returned
   and the register where structure-value addresses are passed.
   Aside from that, you can include as many other registers as you like.  
   
   Note that the extended precision registers are only saved in some
   modes.  The macro HARD_REGNO_CALL_CLOBBERED specifies which modes
   get clobbered for a given regno.  */

#define CALL_USED_REGISTERS \
{									\
/* R0  R1  R2  R3  R4  R5  R6  R7 AR0 AR1 AR2 AR3 AR4 AR5 AR6 AR7.  */	\
    1,  1,  1,  1,  0,  0,  0,  0,  1,  1,  1,  0,  0,  0,  0,  0,	\
/* DP IR0 IR1  BK  SP  ST DIE IIE IIF  RS  RE  RC  R8  R9 R10 R11.  */	\
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  1,  1,  1	\
}

/* Macro to conditionally modify fixed_regs/call_used_regs.  */

#define CONDITIONAL_REGISTER_USAGE			\
  {							\
    if (! TARGET_BK)					\
      {							\
	fixed_regs[BK_REGNO] = 1;			\
        call_used_regs[BK_REGNO] = 1;			\
        c4x_regclass_map[BK_REGNO] = NO_REGS;		\
      }							\
    if (TARGET_C3X)					\
      {							\
	 int i;                                          \
							 \
	 reg_names[DIE_REGNO] = "ie";  /* Clobber die.  */ \
	 reg_names[IF_REGNO] = "if";   /* Clobber iie.  */ \
	 reg_names[IOF_REGNO] = "iof"; /* Clobber iif.  */ \
	 						\
	 for (i = R8_REGNO; i <= R11_REGNO; i++)	\
	 {						\
	     fixed_regs[i] = call_used_regs[i] = 1;	\
	     c4x_regclass_map[i] = NO_REGS;		\
	 }						\
      }							\
    if (TARGET_PRESERVE_FLOAT)				\
      {							\
	c4x_caller_save_map[R6_REGNO] = HFmode;		\
	c4x_caller_save_map[R7_REGNO] = HFmode;		\
      }							\
   }

/* Order of Allocation of Registers.  */

/* List the order in which to allocate registers.  Each register must be
   listed once, even those in FIXED_REGISTERS.

   First allocate registers that don't need preservation across calls,
   except index and address registers.  Then allocate data registers
   that require preservation across calls (even though this invokes an
   extra overhead of having to save/restore these registers).  Next
   allocate the address and index registers, since using these
   registers for arithmetic can cause pipeline stalls.  Finally
   allocated the fixed registers which won't be allocated anyhow.  */

#define REG_ALLOC_ORDER					\
{R0_REGNO, R1_REGNO, R2_REGNO, R3_REGNO, 		\
 R9_REGNO, R10_REGNO, R11_REGNO,			\
 RS_REGNO, RE_REGNO, RC_REGNO, BK_REGNO,		\
 R4_REGNO, R5_REGNO, R6_REGNO, R7_REGNO, R8_REGNO,	\
 AR0_REGNO, AR1_REGNO, AR2_REGNO, AR3_REGNO,		\
 AR4_REGNO, AR5_REGNO, AR6_REGNO, AR7_REGNO,		\
 IR0_REGNO, IR1_REGNO,					\
 SP_REGNO, DP_REGNO, ST_REGNO, IE_REGNO, IF_REGNO, IOF_REGNO}

/* A C expression that is nonzero if hard register number REGNO2 can be
   considered for use as a rename register for REGNO1 */

#define HARD_REGNO_RENAME_OK(REGNO1,REGNO2) \
  c4x_hard_regno_rename_ok((REGNO1), (REGNO2))

/* Determine which register classes are very likely used by spill registers.
   local-alloc.c won't allocate pseudos that have these classes as their
   preferred class unless they are "preferred or nothing".  */

#define CLASS_LIKELY_SPILLED_P(CLASS) ((CLASS) == INDEX_REGS)

/* CCmode is wrongly defined in machmode.def.  It should have a size
   of UNITS_PER_WORD.  HFmode is 40-bits and thus fits within a single
   extended precision register.  Similarly, HCmode fits within two
   extended precision registers.  */

#define HARD_REGNO_NREGS(REGNO, MODE)				\
(((MODE) == CCmode || (MODE) == CC_NOOVmode) ? 1 : \
 ((MODE) == HFmode) ? 1 : \
 ((MODE) == HCmode) ? 2 : \
 ((GET_MODE_SIZE(MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD))


/* A C expression that is nonzero if the hard register REGNO is preserved
   across a call in mode MODE.  This does not have to include the call used
   registers.  */

#define HARD_REGNO_CALL_PART_CLOBBERED(REGNO, MODE)		              \
     ((IS_FLOAT_CALL_SAVED_REGNO (REGNO) && ! ((MODE) == QFmode))  	      \
      || (IS_INT_CALL_SAVED_REGNO (REGNO)				      \
	  && ! ((MODE) == QImode || (MODE) == HImode || (MODE) == Pmode)))

/* Specify the modes required to caller save a given hard regno.  */

#define HARD_REGNO_CALLER_SAVE_MODE(REGNO, NREGS, MODE) (c4x_caller_save_map[REGNO])

#define HARD_REGNO_MODE_OK(REGNO, MODE) c4x_hard_regno_mode_ok(REGNO, MODE)

/* A C expression that is nonzero if it is desirable to choose
   register allocation so as to avoid move instructions between a
   value of mode MODE1 and a value of mode MODE2.

   Value is 1 if it is a good idea to tie two pseudo registers
   when one has mode MODE1 and one has mode MODE2.
   If HARD_REGNO_MODE_OK could produce different values for MODE1 and MODE2,
   for any hard reg, then this must be 0 for correct output.  */

#define MODES_TIEABLE_P(MODE1, MODE2) 0


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
   
enum reg_class
  {
    NO_REGS,
    R0R1_REGS,			/* 't'.  */
    R2R3_REGS,			/* 'u'.  */
    EXT_LOW_REGS,		/* 'q'.  */
    EXT_REGS,			/* 'f'.  */
    ADDR_REGS,			/* 'a'.  */
    INDEX_REGS,			/* 'x'.  */
    BK_REG,			/* 'k'.  */
    SP_REG,			/* 'b'.  */
    RC_REG,			/* 'v'.  */
    COUNTER_REGS,		/*  */
    INT_REGS,			/* 'c'.  */
    GENERAL_REGS,		/* 'r'.  */
    DP_REG,			/* 'z'.  */
    ST_REG,			/* 'y'.  */
    ALL_REGS,
    LIM_REG_CLASSES
  };

#define N_REG_CLASSES (int) LIM_REG_CLASSES

#define REG_CLASS_NAMES \
{			\
   "NO_REGS",		\
   "R0R1_REGS",		\
   "R2R3_REGS",		\
   "EXT_LOW_REGS",	\
   "EXT_REGS",		\
   "ADDR_REGS",		\
   "INDEX_REGS",	\
   "BK_REG",		\
   "SP_REG",		\
   "RC_REG",		\
   "COUNTER_REGS",	\
   "INT_REGS",		\
   "GENERAL_REGS",	\
   "DP_REG",		\
   "ST_REG",		\
   "ALL_REGS"		\
}

/* Define which registers fit in which classes.
   This is an initializer for a vector of HARD_REG_SET
   of length N_REG_CLASSES.  RC is not included in GENERAL_REGS
   since the register allocator will often choose a general register
   in preference to RC for the decrement_and_branch_on_count pattern.  */

#define REG_CLASS_CONTENTS \
{						\
 {0x00000000}, /*     No registers.  */		\
 {0x00000003}, /* 't' R0-R1	.  */		\
 {0x0000000c}, /* 'u' R2-R3	.  */		\
 {0x000000ff}, /* 'q' R0-R7	.  */		\
 {0xf00000ff}, /* 'f' R0-R11       */		\
 {0x0000ff00}, /* 'a' AR0-AR7.  */		\
 {0x00060000}, /* 'x' IR0-IR1.  */		\
 {0x00080000}, /* 'k' BK.  */			\
 {0x00100000}, /* 'b' SP.  */			\
 {0x08000000}, /* 'v' RC.  */			\
 {0x0800ff00}, /*     RC,AR0-AR7.  */		\
 {0x0e1eff00}, /* 'c' AR0-AR7, IR0-IR1, BK, SP, RS, RE, RC.  */	\
 {0xfe1effff}, /* 'r' R0-R11, AR0-AR7, IR0-IR1, BK, SP, RS, RE, RC.  */\
 {0x00010000}, /* 'z' DP.  */			\
 {0x00200000}, /* 'y' ST.  */			\
 {0xffffffff}, /*     All registers.  */		\
}

/* The same information, inverted:
   Return the class number of the smallest class containing
   reg number REGNO.  This could be a conditional expression
   or could index an array.  */

#define REGNO_REG_CLASS(REGNO) (c4x_regclass_map[REGNO])

/* When SMALL_REGISTER_CLASSES is defined, the lifetime of registers
   explicitly used in the rtl is kept as short as possible.

   We only need to define SMALL_REGISTER_CLASSES if TARGET_PARALLEL_MPY
   is defined since the MPY|ADD insns require the classes R0R1_REGS and
   R2R3_REGS which are used by the function return registers (R0,R1) and
   the register arguments (R2,R3), respectively.  I'm reluctant to define
   this macro since it stomps on many potential optimizations.  Ideally
   it should have a register class argument so that not all the register
   classes gets penalized for the sake of a naughty few...  For long
   double arithmetic we need two additional registers that we can use as
   spill registers.  */

#define SMALL_REGISTER_CLASSES (TARGET_SMALL_REG_CLASS && TARGET_PARALLEL_MPY)

#define BASE_REG_CLASS	ADDR_REGS
#define INDEX_REG_CLASS INDEX_REGS

/*
  Register constraints for the C4x
 
  a - address reg (ar0-ar7)
  b - stack reg (sp)
  c - other gp int-only reg
  d - data/int reg (equiv. to f)
  f - data/float reg
  h - data/long double reg (equiv. to f)
  k - block count (bk)
  q - r0-r7
  t - r0-r1
  u - r2-r3
  v - repeat count (rc)
  x - index register (ir0-ir1)
  y - status register (st)
  z - dp reg (dp) 

  Memory/constant constraints for the C4x

  G - short float 16-bit
  I - signed 16-bit constant (sign extended)
  J - signed 8-bit constant (sign extended)  (C4x only)
  K - signed 5-bit constant (sign extended)  (C4x only for stik)
  L - unsigned 16-bit constant
  M - unsigned 8-bit constant                (C4x only)
  N - ones complement of unsigned 16-bit constant
  Q - indirect arx + 9-bit signed displacement
      (a *-arx(n) or *+arx(n) is used to account for the sign bit)
  R - indirect arx + 5-bit unsigned displacement  (C4x only)
  S - indirect arx + 0, 1, or irn displacement
  T - direct symbol ref
  > - indirect with autoincrement
  < - indirect with autodecrement
  } - indirect with post-modify
  { - indirect with pre-modify
  */

#define REG_CLASS_FROM_LETTER(CC)				\
     ( ((CC) == 'a') ? ADDR_REGS				\
     : ((CC) == 'b') ? SP_REG					\
     : ((CC) == 'c') ? INT_REGS					\
     : ((CC) == 'd') ? EXT_REGS					\
     : ((CC) == 'f') ? EXT_REGS					\
     : ((CC) == 'h') ? EXT_REGS					\
     : ((CC) == 'k') ? BK_REG					\
     : ((CC) == 'q') ? EXT_LOW_REGS				\
     : ((CC) == 't') ? R0R1_REGS				\
     : ((CC) == 'u') ? R2R3_REGS				\
     : ((CC) == 'v') ? RC_REG					\
     : ((CC) == 'x') ? INDEX_REGS				\
     : ((CC) == 'y') ? ST_REG					\
     : ((CC) == 'z') ? DP_REG					\
     : NO_REGS )

/* These assume that REGNO is a hard or pseudo reg number.
   They give nonzero only if REGNO is a hard reg of the suitable class
   or a pseudo reg currently allocated to a suitable hard reg.
   Since they use reg_renumber, they are safe only once reg_renumber
   has been allocated, which happens in local-alloc.c.  */

#define REGNO_OK_FOR_BASE_P(REGNO)  \
     (IS_ADDR_REGNO(REGNO) || IS_ADDR_REGNO((unsigned)reg_renumber[REGNO]))

#define REGNO_OK_FOR_INDEX_P(REGNO) \
     (IS_INDEX_REGNO(REGNO) || IS_INDEX_REGNO((unsigned)reg_renumber[REGNO]))

/* If we have to generate framepointer + constant prefer an ADDR_REGS
   register.  This avoids using EXT_REGS in addqi3_noclobber_reload.  */

#define PREFERRED_RELOAD_CLASS(X, CLASS)			\
     (GET_CODE (X) == PLUS					\
      && GET_MODE (X) == Pmode					\
      && GET_CODE (XEXP ((X), 0)) == REG			\
      && GET_MODE (XEXP ((X), 0)) == Pmode			\
      && REGNO (XEXP ((X), 0)) == FRAME_POINTER_REGNUM		\
      && GET_CODE (XEXP ((X), 1)) == CONST_INT			\
	? ADDR_REGS : (CLASS))

#define LIMIT_RELOAD_CLASS(X, CLASS) (CLASS)

#define SECONDARY_MEMORY_NEEDED(CLASS1, CLASS2, MODE) 0

#define CLASS_MAX_NREGS(CLASS, MODE)			\
(((MODE) == CCmode || (MODE) == CC_NOOVmode) ? 1 : ((MODE) == HFmode) ? 1 : \
((GET_MODE_SIZE(MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD))

#define IS_INT5_CONST(VAL) (((VAL) <= 15) && ((VAL) >= -16))	/* 'K'.  */

#define IS_UINT5_CONST(VAL) (((VAL) <= 31) && ((VAL) >= 0))	/* 'R'.  */

#define IS_INT8_CONST(VAL) (((VAL) <= 127) && ((VAL) >= -128))	/* 'J'.  */

#define IS_UINT8_CONST(VAL) (((VAL) <= 255) && ((VAL) >= 0))	/* 'M'.  */

#define IS_INT16_CONST(VAL) (((VAL) <= 32767) && ((VAL) >= -32768)) /* 'I'.  */

#define IS_UINT16_CONST(VAL) (((VAL) <= 65535) && ((VAL) >= 0))	/* 'L'.  */

#define IS_NOT_UINT16_CONST(VAL) IS_UINT16_CONST(~(VAL))	/* 'N'.  */

#define IS_HIGH_CONST(VAL) \
(! TARGET_C3X && (((VAL) & 0xffff) == 0)) /* 'O'.  */


#define IS_DISP1_CONST(VAL) (((VAL) <= 1) && ((VAL) >= -1)) /* 'S'.  */

#define IS_DISP8_CONST(VAL) (((VAL) <= 255) && ((VAL) >= -255))	/* 'Q'.  */

#define IS_DISP1_OFF_CONST(VAL) (IS_DISP1_CONST (VAL) \
				 && IS_DISP1_CONST (VAL + 1))

#define IS_DISP8_OFF_CONST(VAL) (IS_DISP8_CONST (VAL) \
				 && IS_DISP8_CONST (VAL + 1))

#define CONST_OK_FOR_LETTER_P(VAL, C)					\
        ( ((C) == 'I') ? (IS_INT16_CONST (VAL))				\
	: ((C) == 'J') ? (! TARGET_C3X && IS_INT8_CONST (VAL))		\
	: ((C) == 'K') ? (! TARGET_C3X && IS_INT5_CONST (VAL))		\
        : ((C) == 'L') ? (IS_UINT16_CONST (VAL))			\
	: ((C) == 'M') ? (! TARGET_C3X && IS_UINT8_CONST (VAL))		\
	: ((C) == 'N') ? (IS_NOT_UINT16_CONST (VAL))		        \
	: ((C) == 'O') ? (IS_HIGH_CONST (VAL))			        \
        : 0 )	

#define CONST_DOUBLE_OK_FOR_LETTER_P(OP, C) 				\
        ( ((C) == 'G') ? (fp_zero_operand (OP, QFmode))			\
	: ((C) == 'H') ? (c4x_H_constant (OP)) 				\
	: 0 )

#define EXTRA_CONSTRAINT(OP, C) \
        ( ((C) == 'Q') ? (c4x_Q_constraint (OP))			\
	: ((C) == 'R') ? (c4x_R_constraint (OP))			\
	: ((C) == 'S') ? (c4x_S_constraint (OP))			\
	: ((C) == 'T') ? (c4x_T_constraint (OP))			\
	: ((C) == 'U') ? (c4x_U_constraint (OP))			\
	: 0 )

#define SMALL_CONST(VAL, insn)						\
     (  ((insn == NULL_RTX) || (get_attr_data (insn) == DATA_INT16))	\
	? IS_INT16_CONST (VAL)						\
	: ( (get_attr_data (insn) == DATA_NOT_UINT16)			\
	    ? IS_NOT_UINT16_CONST (VAL)					\
	    :  ( (get_attr_data (insn) == DATA_HIGH_16)			\
	       ? IS_HIGH_CONST (VAL)					\
	       : IS_UINT16_CONST (VAL)					\
	    )								\
	  )								\
	)

/*
   I. Routine calling with arguments in registers
   ----------------------------------------------

   The TI C3x compiler has a rather unusual register passing algorithm.
   Data is passed in the following registers (in order):

   AR2, R2, R3, RC, RS, RE

   However, the first and second floating point values are always in R2
   and R3 (and all other floats are on the stack).  Structs are always
   passed on the stack.  If the last argument is an ellipsis, the
   previous argument is passed on the stack so that its address can be
   taken for the stdargs macros.

   Because of this, we have to pre-scan the list of arguments to figure
   out what goes where in the list.

   II. Routine calling with arguments on stack
   -------------------------------------------

   Let the subroutine declared as "foo(arg0, arg1, arg2);" have local
   variables loc0, loc1, and loc2.  After the function prologue has
   been executed, the stack frame will look like:

   [stack grows towards increasing addresses]
       I-------------I
   5   I saved reg1  I  <= SP points here
       I-------------I
   4   I saved reg0  I  
       I-------------I
   3   I       loc2  I  
       I-------------I  
   2   I       loc1  I  
       I-------------I  
   1   I       loc0  I  
       I-------------I
   0   I     old FP  I <= FP (AR3) points here
       I-------------I
   -1  I  return PC  I
       I-------------I
   -2  I       arg0  I  
       I-------------I  
   -3  I       arg1  I
       I-------------I  
   -4  I       arg2  I 
       I-------------I  

   All local variables (locn) are accessible by means of +FP(n+1)
   addressing, where n is the local variable number.

   All stack arguments (argn) are accessible by means of -FP(n-2).

   The stack pointer (SP) points to the last register saved in the
   prologue (regn).

   Note that a push instruction performs a preincrement of the stack
   pointer.  (STACK_PUSH_CODE == PRE_INC)

   III. Registers used in function calling convention
   --------------------------------------------------

   Preserved across calls: R4...R5 (only by PUSH,  i.e. lower 32 bits)
   R6...R7 (only by PUSHF, i.e. upper 32 bits)
   AR3...AR7

   (Because of this model, we only assign FP values in R6, R7 and
   only assign integer values in R4, R5.)

   These registers are saved at each function entry and restored at
   the exit. Also it is expected any of these not affected by any
   call to user-defined (not service) functions.

   Not preserved across calls: R0...R3
   R4...R5 (upper 8 bits)
   R6...R7 (lower 8 bits)
   AR0...AR2, IR0, IR1, BK, ST, RS, RE, RC

   These registers are used arbitrary in a function without being preserved.
   It is also expected that any of these can be clobbered by any call.

   Not used by GCC (except for in user "asm" statements):
   IE (DIE), IF (IIE), IOF (IIF)

   These registers are never used by GCC for any data, but can be used
   with "asm" statements.  */

#define C4X_ARG0 -2
#define C4X_LOC0 1

/* Basic Stack Layout.  */
     
/* The stack grows upward, stack frame grows upward, and args grow
   downward.  */

#define STARTING_FRAME_OFFSET		C4X_LOC0
#define FIRST_PARM_OFFSET(FNDECL)      (C4X_ARG0 + 1)
#define ARGS_GROW_DOWNWARD
#define STACK_POINTER_OFFSET 1

/* Define this if pushing a word on the stack
   makes the stack pointer a smaller address.  */

/* #define STACK_GROWS_DOWNWARD.  */
/* Like the dsp16xx, i370, i960, and we32k ports.  */

/* Define this to nonzero if the nominal address of the stack frame
   is at the high-address end of the local variables;
   that is, each additional local variable allocated
   goes at a more negative offset in the frame.  */

#define FRAME_GROWS_DOWNWARD 0


/* Registers That Address the Stack Frame.  */

#define STACK_POINTER_REGNUM	SP_REGNO	/* SP.  */
#define FRAME_POINTER_REGNUM	AR3_REGNO	/* AR3.  */
#define ARG_POINTER_REGNUM	AR3_REGNO	/* AR3.  */
#define STATIC_CHAIN_REGNUM	AR0_REGNO	/* AR0.  */

/* Eliminating Frame Pointer and Arg Pointer.  */

#define FRAME_POINTER_REQUIRED	0

#define INITIAL_FRAME_POINTER_OFFSET(DEPTH)			\
{								\
 int regno;							\
 int offset = 0;						\
  for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)	\
    if (regs_ever_live[regno] && ! call_used_regs[regno])	\
      offset += TARGET_PRESERVE_FLOAT				\
		&& IS_FLOAT_CALL_SAVED_REGNO (regno) ? 2 : 1;	\
  (DEPTH) = -(offset + get_frame_size ());			\
}

/* This is a hack...  We need to specify a register.  */
#define	ELIMINABLE_REGS 					\
  {{ FRAME_POINTER_REGNUM, FRAME_POINTER_REGNUM }}

#define	CAN_ELIMINATE(FROM, TO)					\
  (! (((FROM) == FRAME_POINTER_REGNUM && (TO) == STACK_POINTER_REGNUM) \
  || ((FROM) == FRAME_POINTER_REGNUM && (TO) == FRAME_POINTER_REGNUM)))

#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET)	 	\
{								\
 int regno;							\
 int offset = 0;						\
  for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)	\
    if (regs_ever_live[regno] && ! call_used_regs[regno])	\
      offset += TARGET_PRESERVE_FLOAT				\
		&& IS_FLOAT_CALL_SAVED_REGNO (regno) ? 2 : 1;	\
  (OFFSET) = -(offset + get_frame_size ());			\
}


/* Passing Function Arguments on the Stack.  */

#define	PUSH_ARGS 1
#define PUSH_ROUNDING(BYTES) (BYTES)
#define RETURN_POPS_ARGS(FUNDECL, FUNTYPE, STACK_SIZE) 0

/* The following structure is used by calls.c, function.c, c4x.c.  */

typedef struct c4x_args
{
  int floats;
  int ints;
  int maxfloats;
  int maxints;
  int init;
  int var;
  int prototype;
  int args;
}
CUMULATIVE_ARGS;

#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, INDIRECT, N_NAMED_ARGS) \
  (c4x_init_cumulative_args (&CUM, FNTYPE, LIBNAME))

#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED)	\
  (c4x_function_arg_advance (&CUM, MODE, TYPE, NAMED))

#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED) \
  (c4x_function_arg(&CUM, MODE, TYPE, NAMED))

/* Define the profitability of saving registers around calls.
   We disable caller save to avoid a bug in flow.c (this also affects
   other targets such as m68k).  Since we must use stf/sti,
   the profitability is marginal anyway.  */

#define CALLER_SAVE_PROFITABLE(REFS,CALLS) 0

/* 1 if N is a possible register number for function argument passing.  */

#define FUNCTION_ARG_REGNO_P(REGNO) \
	(  (   ((REGNO) == AR2_REGNO)	/* AR2.  */	\
	    || ((REGNO) == R2_REGNO)	/* R2.  */	\
	    || ((REGNO) == R3_REGNO)	/* R3.  */	\
	    || ((REGNO) == RC_REGNO)	/* RC.  */	\
	    || ((REGNO) == RS_REGNO)	/* RS.  */	\
	    || ((REGNO) == RE_REGNO))	/* RE.  */	\
	 ? 1						\
	 : 0)

/* How Scalar Function Values Are Returned.  */

#define FUNCTION_VALUE(VALTYPE, FUNC) \
	gen_rtx_REG (TYPE_MODE(VALTYPE), R0_REGNO)	/* Return in R0.  */

#define LIBCALL_VALUE(MODE) \
	gen_rtx_REG (MODE, R0_REGNO)	/* Return in R0.  */

#define FUNCTION_VALUE_REGNO_P(REGNO) ((REGNO) == R0_REGNO)

/* How Large Values Are Returned.  */

#define DEFAULT_PCC_STRUCT_RETURN	0

/* Generating Code for Profiling.  */

/* Note that the generated assembly uses the ^ operator to load the 16
   MSBs of the address.  This is not supported by the TI assembler. 
   The FUNCTION profiler needs a function mcount which gets passed
   a pointer to the LABELNO.  */

#define FUNCTION_PROFILER(FILE, LABELNO) 			\
     if (! TARGET_C3X)						\
     {								\
	fprintf (FILE, "\tpush\tar2\n");			\
	fprintf (FILE, "\tldhi\t^LP%d,ar2\n", (LABELNO));	\
	fprintf (FILE, "\tor\t#LP%d,ar2\n", (LABELNO));		\
	fprintf (FILE, "\tcall\tmcount\n");			\
	fprintf (FILE, "\tpop\tar2\n");				\
     }								\
     else							\
     {								\
	fprintf (FILE, "\tpush\tar2\n");			\
	fprintf (FILE, "\tldiu\t^LP%d,ar2\n", (LABELNO));	\
	fprintf (FILE, "\tlsh\t16,ar2\n");			\
	fprintf (FILE, "\tor\t#LP%d,ar2\n", (LABELNO));		\
	fprintf (FILE, "\tcall\tmcount\n");			\
	fprintf (FILE, "\tpop\tar2\n");				\
     }

/* CC_NOOVmode should be used when the first operand is a PLUS, MINUS, NEG
   or MULT.
   CCmode should be used when no special processing is needed.  */
#define SELECT_CC_MODE(OP,X,Y) \
  ((GET_CODE (X) == PLUS || GET_CODE (X) == MINUS		\
    || GET_CODE (X) == NEG || GET_CODE (X) == MULT		\
    || GET_MODE (X) == ABS					\
    || GET_CODE (Y) == PLUS || GET_CODE (Y) == MINUS		\
    || GET_CODE (Y) == NEG || GET_CODE (Y) == MULT		\
    || GET_MODE (Y) == ABS)					\
    ? CC_NOOVmode : CCmode)

/* Addressing Modes.  */

#define HAVE_POST_INCREMENT 1
#define HAVE_PRE_INCREMENT 1
#define HAVE_POST_DECREMENT 1
#define HAVE_PRE_DECREMENT 1
#define HAVE_PRE_MODIFY_REG 1
#define HAVE_POST_MODIFY_REG 1
#define HAVE_PRE_MODIFY_DISP 1
#define HAVE_POST_MODIFY_DISP 1

/* The number of insns that can be packed into a single opcode.  */
#define PACK_INSNS 2

/* Recognize any constant value that is a valid address. 
   We could allow arbitrary constant addresses in the large memory
   model but for the small memory model we can only accept addresses
   within the data page.  I suppose we could also allow
   CONST PLUS SYMBOL_REF.  */
#define CONSTANT_ADDRESS_P(X) (GET_CODE (X) == SYMBOL_REF)

/* Maximum number of registers that can appear in a valid memory
   address.  */
#define MAX_REGS_PER_ADDRESS	2

/* The macros REG_OK_FOR..._P assume that the arg is a REG rtx
   and check its validity for a certain class.
   We have two alternate definitions for each of them.
   The usual definition accepts all pseudo regs; the other rejects
   them unless they have been allocated suitable hard regs.
   The symbol REG_OK_STRICT causes the latter definition to be used.

   Most source files want to accept pseudo regs in the hope that
   they will get allocated to the class that the insn wants them to be in.
   Source files for reload pass need to be strict.
   After reload, it makes no difference, since pseudo regs have
   been eliminated by then.  */

#ifndef REG_OK_STRICT

/* Nonzero if X is a hard or pseudo reg that can be used as a base.  */

#define REG_OK_FOR_BASE_P(X) IS_ADDR_OR_PSEUDO_REG(X)

/* Nonzero if X is a hard or pseudo reg that can be used as an index.  */

#define REG_OK_FOR_INDEX_P(X) IS_INDEX_OR_PSEUDO_REG(X)

#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR)				\
{									\
  if (c4x_legitimate_address_p (MODE, X, 0))				\
    goto ADDR;								\
}

#else

/* Nonzero if X is a hard reg that can be used as an index.  */

#define REG_OK_FOR_INDEX_P(X) REGNO_OK_FOR_INDEX_P (REGNO (X))

/* Nonzero if X is a hard reg that can be used as a base reg.  */

#define REG_OK_FOR_BASE_P(X) REGNO_OK_FOR_BASE_P (REGNO (X))

#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR)				\
{									\
  if (c4x_legitimate_address_p (MODE, X, 1))				\
    goto ADDR;								\
}

#endif

#define LEGITIMIZE_ADDRESS(X, OLDX, MODE, WIN) \
{									\
  rtx new;								\
									\
  new = c4x_legitimize_address (X, MODE);				\
  if (new != NULL_RTX)							\
  {									\
    (X) = new;								\
    goto WIN;								\
  }									\
}

#define LEGITIMIZE_RELOAD_ADDRESS(X,MODE,OPNUM,TYPE,IND_LEVELS,WIN)     \
{									\
  if (MODE != HImode							\
      && MODE != HFmode							\
      && GET_MODE (X) != HImode						\
      && GET_MODE (X) != HFmode						\
      && (GET_CODE (X) == CONST						\
          || GET_CODE (X) == SYMBOL_REF					\
          || GET_CODE (X) == LABEL_REF))				\
    {									\
      if (! TARGET_SMALL)						\
	{								\
          int i;							\
      	  (X) = gen_rtx_LO_SUM (GET_MODE (X),				\
			      gen_rtx_HIGH (GET_MODE (X), X), X);	\
          i = push_reload (XEXP (X, 0), NULL_RTX,			\
			   &XEXP (X, 0), NULL,				\
		           DP_REG, GET_MODE (X), VOIDmode, 0, 0,	\
		           OPNUM, TYPE);				\
          /* The only valid reg is DP. This is a fixed reg and will	\
	     normally not be used so force it.  */			\
          rld[i].reg_rtx = gen_rtx_REG (Pmode, DP_REGNO); 		\
          rld[i].nocombine = 1; 					\
        }								\
      else								\
        {								\
          /* make_memloc in reload will substitute invalid memory       \
             references.  We need to fix them up.  */                   \
          (X) = gen_rtx_LO_SUM (Pmode, gen_rtx_REG (Pmode, DP_REGNO), (X)); \
        }								\
      goto WIN;								\
   }									\
  else if (MODE != HImode						\
           && MODE != HFmode						\
           && GET_MODE (X) != HImode					\
           && GET_MODE (X) != HFmode					\
           && GET_CODE (X) == LO_SUM					\
           && GET_CODE (XEXP (X,0)) == HIGH				\
           && (GET_CODE (XEXP (XEXP (X,0),0)) == CONST			\
               || GET_CODE (XEXP (XEXP (X,0),0)) == SYMBOL_REF		\
               || GET_CODE (XEXP (XEXP (X,0),0)) == LABEL_REF))		\
    {									\
      if (! TARGET_SMALL)						\
	{								\
          int i = push_reload (XEXP (X, 0), NULL_RTX,			\
			       &XEXP (X, 0), NULL,			\
		               DP_REG, GET_MODE (X), VOIDmode, 0, 0,	\
		               OPNUM, TYPE);				\
          /* The only valid reg is DP. This is a fixed reg and will	\
	     normally not be used so force it.  */			\
          rld[i].reg_rtx = gen_rtx_REG (Pmode, DP_REGNO); 		\
          rld[i].nocombine = 1; 					\
        }								\
      goto WIN;								\
   }									\
}

/* No mode-dependent addresses on the C4x are autoincrements.  */

#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR, LABEL)	\
  if (GET_CODE (ADDR) == PRE_DEC	\
      || GET_CODE (ADDR) == POST_DEC	\
      || GET_CODE (ADDR) == PRE_INC	\
      || GET_CODE (ADDR) == POST_INC	\
      || GET_CODE (ADDR) == POST_MODIFY	\
      || GET_CODE (ADDR) == PRE_MODIFY)	\
    goto LABEL


/* Nonzero if the constant value X is a legitimate general operand.
   It is given that X satisfies CONSTANT_P or is a CONST_DOUBLE. 

   The C4x can only load 16-bit immediate values, so we only allow a
   restricted subset of CONST_INT and CONST_DOUBLE.  Disallow
   LABEL_REF and SYMBOL_REF (except on the C40 with the big memory
   model) so that the symbols will be forced into the constant pool.
   On second thoughts, let's do this with the move expanders since
   the alias analysis has trouble if we force constant addresses
   into memory.
*/

#define LEGITIMATE_CONSTANT_P(X)				\
  ((GET_CODE (X) == CONST_DOUBLE && c4x_H_constant (X))		\
  || (GET_CODE (X) == CONST_INT)				\
  || (GET_CODE (X) == SYMBOL_REF)				\
  || (GET_CODE (X) == LABEL_REF)				\
  || (GET_CODE (X) == CONST)					\
  || (GET_CODE (X) == HIGH && ! TARGET_C3X)			\
  || (GET_CODE (X) == LO_SUM && ! TARGET_C3X))

#define LEGITIMATE_DISPLACEMENT_P(X) IS_DISP8_CONST (INTVAL (X))

/* Describing Relative Cost of Operations.  */

#define	CANONICALIZE_COMPARISON(CODE, OP0, OP1)		\
if (REG_P (OP1) && ! REG_P (OP0))			\
{							\
  rtx tmp = OP0; OP0 = OP1 ; OP1 = tmp;			\
  CODE = swap_condition (CODE);				\
}

#define EXT_CLASS_P(CLASS) (reg_class_subset_p (CLASS, EXT_REGS))
#define ADDR_CLASS_P(CLASS) (reg_class_subset_p (CLASS, ADDR_REGS))
#define INDEX_CLASS_P(CLASS) (reg_class_subset_p (CLASS, INDEX_REGS))
#define EXPENSIVE_CLASS_P(CLASS) (ADDR_CLASS_P(CLASS) \
                          || INDEX_CLASS_P(CLASS) || (CLASS) == SP_REG)

/* Compute extra cost of moving data between one register class
   and another.  */

#define REGISTER_MOVE_COST(MODE, FROM, TO)	2

/* Memory move cost is same as fast register move.  Maybe this should
   be bumped up?.  */

#define MEMORY_MOVE_COST(M,C,I)		4

/* Branches are kind of expensive (even with delayed branching) so
   make their cost higher.  */

#define BRANCH_COST			8

#define	WORD_REGISTER_OPERATIONS

/* Dividing the Output into Sections.  */

#define TEXT_SECTION_ASM_OP "\t.text"

#define DATA_SECTION_ASM_OP "\t.data"

#define READONLY_DATA_SECTION_ASM_OP "\t.sect\t\".const\""

/* Do not use .init section so __main will be called on startup. This will
   call __do_global_ctors and prepare for __do_global_dtors on exit.  */

#if 0
#define INIT_SECTION_ASM_OP  "\t.sect\t\".init\""
#endif

#define FINI_SECTION_ASM_OP  "\t.sect\t\".fini\""

/* Switch into a generic section.  */
#define TARGET_ASM_NAMED_SECTION c4x_asm_named_section


/* Overall Framework of an Assembler File.  */

#define ASM_COMMENT_START ";"

#define ASM_APP_ON ""
#define ASM_APP_OFF ""

#define ASM_OUTPUT_ASCII(FILE, PTR, LEN) c4x_output_ascii (FILE, PTR, LEN)

/* Output and Generation of Labels.  */

#define NO_DOT_IN_LABEL		/* Only required for TI format.  */

/* Globalizing directive for a label.  */
#define GLOBAL_ASM_OP "\t.global\t"

#define ASM_OUTPUT_EXTERNAL(FILE, DECL, NAME) \
c4x_external_ref (NAME)

/* The prefix to add to user-visible assembler symbols.  */

#define USER_LABEL_PREFIX "_"

/* This is how to store into the string LABEL
   the symbol_ref name of an internal numbered label where
   PREFIX is the class of label and NUM is the number within the class.
   This is suitable for output with `assemble_name'.  */

#define ASM_GENERATE_INTERNAL_LABEL(BUFFER, PREFIX, NUM) \
    sprintf (BUFFER, "*%s%lu", PREFIX, (unsigned long)(NUM))

/* A C statement to output to the stdio stream STREAM assembler code which
   defines (equates) the symbol NAME to have the value VALUE.  */

#define ASM_OUTPUT_DEF(STREAM, NAME, VALUE) 	\
do {						\
  assemble_name (STREAM, NAME);			\
  fprintf (STREAM, "\t.set\t%s\n", VALUE);	\
} while (0)

/* Output of Dispatch Tables.  */

/* This is how to output an element of a case-vector that is absolute.  */

#define ASM_OUTPUT_ADDR_VEC_ELT(FILE, VALUE) \
    fprintf (FILE, "\t.long\tL%d\n", VALUE);

/* This is how to output an element of a case-vector that is relative.  */

#define ASM_OUTPUT_ADDR_DIFF_ELT(FILE, BODY, VALUE, REL) \
    fprintf (FILE, "\t.long\tL%d-L%d\n", VALUE, REL);

#undef SIZE_TYPE
#define SIZE_TYPE "unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "int"

#undef WCHAR_TYPE
#define WCHAR_TYPE "long int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

#define INT_TYPE_SIZE		32
#define LONG_LONG_TYPE_SIZE	64
#define FLOAT_TYPE_SIZE		32
#define DOUBLE_TYPE_SIZE	32
#define LONG_DOUBLE_TYPE_SIZE	64 /* Actually only 40.  */

/* Output #ident as a .ident.  */

#define ASM_OUTPUT_IDENT(FILE, NAME) \
  fprintf (FILE, "\t.ident \"%s\"\n", NAME);

/* Output of Uninitialized Variables.  */

/* This says how to output an assembler line to define a local
   uninitialized variable.  */

#undef ASM_OUTPUT_LOCAL
#define ASM_OUTPUT_LOCAL(FILE, NAME, SIZE, ROUNDED)  \
( fputs ("\t.bss\t", FILE),			\
  assemble_name (FILE, (NAME)),		\
  fprintf (FILE, ",%u\n", (int)(ROUNDED)))

/* This says how to output an assembler line to define a global
   uninitialized variable.  */

#undef ASM_OUTPUT_COMMON
#define ASM_OUTPUT_COMMON(FILE, NAME, SIZE, ROUNDED)  \
(  fputs ("\t.globl\t", FILE),	\
   assemble_name (FILE, (NAME)),	\
   fputs ("\n\t.bss\t", FILE),	\
   assemble_name (FILE, (NAME)),	\
   fprintf (FILE, ",%u\n", (int)(ROUNDED)))

#undef ASM_OUTPUT_BSS
#define ASM_OUTPUT_BSS(FILE, DECL, NAME, SIZE, ALIGN)   \
(  fputs ("\t.globl\t", FILE),	\
   assemble_name (FILE, (NAME)),	\
   fputs ("\n\t.bss\t", FILE),	\
   assemble_name (FILE, (NAME)),	\
   fprintf (FILE, ",%u\n", (int)(SIZE)))

/* Macros Controlling Initialization Routines.  */

#define OBJECT_FORMAT_COFF
#define REAL_NM_FILE_NAME "c4x-nm"

/* Output of Assembler Instructions.  */

/* Register names when used for integer modes.  */

#define REGISTER_NAMES \
{								\
 "r0",   "r1", "r2",   "r3",  "r4",  "r5",  "r6",  "r7",	\
 "ar0", "ar1", "ar2", "ar3", "ar4", "ar5", "ar6", "ar7",	\
 "dp",  "ir0", "ir1",  "bk",  "sp",  "st", "die", "iie",	\
 "iif",	 "rs",  "re",  "rc",  "r8",  "r9", "r10", "r11"		\
}

/* Alternate register names when used for floating point modes.  */

#define FLOAT_REGISTER_NAMES \
{								\
 "f0",   "f1", "f2",   "f3",  "f4",  "f5",  "f6",  "f7",	\
 "ar0", "ar1", "ar2", "ar3", "ar4", "ar5", "ar6", "ar7",	\
 "dp",  "ir0", "ir1",  "bk",  "sp",  "st", "die", "iie",	\
 "iif",	 "rs",  "re",  "rc",  "f8",  "f9", "f10", "f11"		\
}

#define PRINT_OPERAND(FILE, X, CODE) c4x_print_operand(FILE, X, CODE)

/* Determine which codes are valid without a following integer.  These must
   not be alphabetic.  */

#define PRINT_OPERAND_PUNCT_VALID_P(CODE) ((CODE) == '#')

#define PRINT_OPERAND_ADDRESS(FILE, X) c4x_print_operand_address(FILE, X)

/* C4x specific pragmas.  */
#define REGISTER_TARGET_PRAGMAS() do {					  \
  c_register_pragma (0, "CODE_SECTION", c4x_pr_CODE_SECTION);		  \
  c_register_pragma (0, "DATA_SECTION", c4x_pr_DATA_SECTION);		  \
  c_register_pragma (0, "FUNC_CANNOT_INLINE", c4x_pr_ignored);		  \
  c_register_pragma (0, "FUNC_EXT_CALLED", c4x_pr_ignored);		  \
  c_register_pragma (0, "FUNC_IS_PURE", c4x_pr_FUNC_IS_PURE);		  \
  c_register_pragma (0, "FUNC_IS_SYSTEM", c4x_pr_ignored);		  \
  c_register_pragma (0, "FUNC_NEVER_RETURNS", c4x_pr_FUNC_NEVER_RETURNS); \
  c_register_pragma (0, "FUNC_NO_GLOBAL_ASG", c4x_pr_ignored);		  \
  c_register_pragma (0, "FUNC_NO_IND_ASG", c4x_pr_ignored);		  \
  c_register_pragma (0, "INTERRUPT", c4x_pr_INTERRUPT);			  \
} while (0)

/* Assembler Commands for Alignment.  */

#define ASM_OUTPUT_SKIP(FILE, SIZE) \
{ int c = SIZE; \
  for (; c > 0; --c) \
   fprintf (FILE,"\t.word\t0\n"); \
}

#define ASM_NO_SKIP_IN_TEXT 1

/* I'm not sure about this one.  FIXME.  */

#define ASM_OUTPUT_ALIGN(FILE, LOG)	\
  if ((LOG) != 0)			\
    fprintf (FILE, "\t.align\t%d\n", (1 << (LOG)))


/* Macros for SDB and DWARF Output  (use .sdef instead of .def
   to avoid conflict with TI's use of .def).  */

#define SDB_DELIM "\n"
#define SDB_DEBUGGING_INFO 1

/* Don't use octal since this can confuse gas for the c4x.  */
#define PUT_SDB_TYPE(a) fprintf(asm_out_file, "\t.type\t0x%x%s", a, SDB_DELIM)

#define PUT_SDB_DEF(A)				\
do { fprintf (asm_out_file, "\t.sdef\t");	\
     ASM_OUTPUT_LABELREF (asm_out_file, A); 	\
     fprintf (asm_out_file, SDB_DELIM); } while (0)

#define PUT_SDB_PLAIN_DEF(A)			\
  fprintf (asm_out_file,"\t.sdef\t.%s%s", A, SDB_DELIM)

#define PUT_SDB_BLOCK_START(LINE)		\
  fprintf (asm_out_file,			\
	   "\t.sdef\t.bb%s\t.val\t.%s\t.scl\t100%s\t.line\t%d%s\t.endef\n", \
	   SDB_DELIM, SDB_DELIM, SDB_DELIM, (LINE), SDB_DELIM)

#define PUT_SDB_BLOCK_END(LINE)			\
  fprintf (asm_out_file,			\
	   "\t.sdef\t.eb%s\t.val\t.%s\t.scl\t100%s\t.line\t%d%s\t.endef\n", \
	   SDB_DELIM, SDB_DELIM, SDB_DELIM, (LINE), SDB_DELIM)

#define PUT_SDB_FUNCTION_START(LINE)		\
  fprintf (asm_out_file,			\
	   "\t.sdef\t.bf%s\t.val\t.%s\t.scl\t101%s\t.line\t%d%s\t.endef\n", \
	   SDB_DELIM, SDB_DELIM, SDB_DELIM, (LINE), SDB_DELIM)

/* Note we output relative line numbers for .ef which gas converts
   to absolute line numbers.  The TI compiler outputs absolute line numbers
   in the .sym directive which gas does not support.  */
#define PUT_SDB_FUNCTION_END(LINE)		\
  fprintf (asm_out_file,			\
	   "\t.sdef\t.ef%s\t.val\t.%s\t.scl\t101%s\t.line\t%d%s\t.endef\n", \
	   SDB_DELIM, SDB_DELIM, SDB_DELIM, \
           (LINE), SDB_DELIM)

#define PUT_SDB_EPILOGUE_END(NAME)			\
do { fprintf (asm_out_file, "\t.sdef\t");		\
     ASM_OUTPUT_LABELREF (asm_out_file, NAME);		\
     fprintf (asm_out_file,				\
	      "%s\t.val\t.%s\t.scl\t-1%s\t.endef\n",	\
	      SDB_DELIM, SDB_DELIM, SDB_DELIM); } while (0)

/* Define this as 1 if `char' should by default be signed; else as 0.  */

#define DEFAULT_SIGNED_CHAR 1

/* A function address in a call instruction is a byte address (for
   indexing purposes) so give the MEM rtx a byte's mode.  */

#define FUNCTION_MODE QImode

#define SLOW_BYTE_ACCESS 0

/* Specify the machine mode that pointers have.  After generation of
   RTL, the compiler makes no further distinction between pointers and
   any other objects of this machine mode.  */

#define Pmode QImode

/* On the C4x we can write the following code. We have to clear the cache
   every time we execute it because the data in the stack could change.

   laj   $+4
   addi3 4,r11,ar0
   lda   *ar0,ar1
   lda   *+ar0(1),ar0
   bud   ar1
   nop
   nop
   or   1000h,st
   .word FNADDR
   .word CXT

   On the c3x this is a bit more difficult. We have to write self
   modifying code here. So we have to clear the cache every time
   we execute it because the data in the stack could change.

   ldiu TOP_OF_FUNCTION,ar1
   lsh  16,ar1
   or   BOTTOM_OF_FUNCTION,ar1
   ldiu TOP_OF_STATIC,ar0
   bud  ar1
   lsh  16,ar0
   or   BOTTOM_OF_STATIC,ar0
   or   1000h,st
   
  */

#define TRAMPOLINE_SIZE (TARGET_C3X ? 8 : 10)

#define TRAMPOLINE_TEMPLATE(FILE)				\
{								\
  if (TARGET_C3X)						\
    {								\
      fprintf (FILE, "\tldiu\t0,ar1\n");			\
      fprintf (FILE, "\tlsh\t16,ar1\n");			\
      fprintf (FILE, "\tor\t0,ar1\n");				\
      fprintf (FILE, "\tldiu\t0,ar0\n");			\
      fprintf (FILE, "\tbud\tar1\n");				\
      fprintf (FILE, "\tlsh\t16,ar0\n");			\
      fprintf (FILE, "\tor\t0,ar0\n");				\
      fprintf (FILE, "\tor\t1000h,st\n");			\
    }								\
  else								\
    {								\
      fprintf (FILE, "\tlaj\t$+4\n");				\
      fprintf (FILE, "\taddi3\t4,r11,ar0\n");			\
      fprintf (FILE, "\tlda\t*ar0,ar1\n");			\
      fprintf (FILE, "\tlda\t*+ar0(1),ar0\n");			\
      fprintf (FILE, "\tbud\tar1\n");				\
      fprintf (FILE, "\tnop\n");				\
      fprintf (FILE, "\tnop\n");				\
      fprintf (FILE, "\tor\t1000h,st\n");			\
      fprintf (FILE, "\t.word\t0\n");				\
      fprintf (FILE, "\t.word\t0\n");				\
    }								\
}

#define INITIALIZE_TRAMPOLINE(TRAMP, FNADDR, CXT)			\
{									\
  if (TARGET_C3X)							\
    {									\
      rtx tmp1, tmp2;							\
      tmp1 = expand_shift (RSHIFT_EXPR, QImode, FNADDR,			\
			   size_int (16), 0, 1);			\
      tmp2 = expand_shift (LSHIFT_EXPR, QImode,				\
			   GEN_INT (0x5069), size_int (16), 0, 1);	\
      emit_insn (gen_iorqi3 (tmp1, tmp1, tmp2));			\
      emit_move_insn (gen_rtx_MEM (QImode,				\
			       plus_constant (TRAMP, 0)), tmp1);	\
      tmp1 = expand_and (QImode, FNADDR, GEN_INT (0xffff), 0);		\
      tmp2 = expand_shift (LSHIFT_EXPR, QImode,				\
			   GEN_INT (0x1069), size_int (16), 0, 1);	\
      emit_insn (gen_iorqi3 (tmp1, tmp1, tmp2));			\
      emit_move_insn (gen_rtx_MEM (QImode,				\
			       plus_constant (TRAMP, 2)), tmp1);	\
      tmp1 = expand_shift (RSHIFT_EXPR, QImode, CXT,			\
			   size_int (16), 0, 1);			\
      tmp2 = expand_shift (LSHIFT_EXPR, QImode,				\
			   GEN_INT (0x5068), size_int (16), 0, 1);	\
      emit_insn (gen_iorqi3 (tmp1, tmp1, tmp2));			\
      emit_move_insn (gen_rtx_MEM (QImode,				\
			       plus_constant (TRAMP, 3)), tmp1);	\
      tmp1 = expand_and (QImode, CXT, GEN_INT (0xffff), 0);		\
      tmp2 = expand_shift (LSHIFT_EXPR, QImode,				\
			   GEN_INT (0x1068), size_int (16), 0, 1);	\
      emit_insn (gen_iorqi3 (tmp1, tmp1, tmp2));			\
      emit_move_insn (gen_rtx_MEM (QImode,				\
			       plus_constant (TRAMP, 6)), tmp1);	\
    }									\
  else									\
    {									\
      emit_move_insn (gen_rtx_MEM (QImode,				\
			       plus_constant (TRAMP, 8)), FNADDR); 	\
      emit_move_insn (gen_rtx_MEM (QImode,				\
			       plus_constant (TRAMP, 9)), CXT); 	\
    }									\
}

/* Specify the machine mode that this machine uses for the index in
   the tablejump instruction.  */

#define CASE_VECTOR_MODE Pmode

/* Max number of (32-bit) bytes we can move from memory to memory
   in one reasonably fast instruction.  */

#define MOVE_MAX 1

/* MOVE_RATIO is the number of move instructions that is better than a
   block move.  */

#define MOVE_RATIO 3

#define BSS_SECTION_ASM_OP "\t.bss"

#define ASM_OUTPUT_REG_PUSH(FILE, REGNO)  \
  fprintf (FILE, "\tpush\t%s\n", reg_names[REGNO])

/* This is how to output an insn to pop a register from the stack.
   It need not be very fast code.  */

#define ASM_OUTPUT_REG_POP(FILE, REGNO)  \
  fprintf (FILE, "\tpop\t%s\n", reg_names[REGNO])

/* Value is 1 if truncating an integer of INPREC bits to OUTPREC bits
   is done just by pretending it is already truncated.  */

#define TRULY_NOOP_TRUNCATION(OUTPREC, INPREC) 1

#define DBR_OUTPUT_SEQEND(FILE)				\
if (final_sequence != NULL_RTX)				\
{							\
 int count;						\
 rtx insn = XVECEXP (final_sequence, 0, 0); 		\
 int laj = GET_CODE (insn) == CALL_INSN 		\
	   || (GET_CODE (insn) == INSN			\
	       && GET_CODE (PATTERN (insn)) == TRAP_IF);\
							\
 count = dbr_sequence_length();				\
 while (count < (laj ? 2 : 3))				\
 {							\
    fputs("\tnop\n", FILE);				\
    count++;						\
 }							\
 if (laj)						\
    fputs("\tpush\tr11\n", FILE);			\
}

#define NO_FUNCTION_CSE

/* We don't want a leading tab.  */

#define ASM_OUTPUT_ASM(FILE, STRING) fprintf (FILE, "%s\n", STRING)

/* Define the intrinsic functions for the c3x/c4x.  */

enum c4x_builtins
{
			/*	intrinsic name		*/
  C4X_BUILTIN_FIX,	/*	fast_ftoi		*/
  C4X_BUILTIN_FIX_ANSI,	/*	ansi_ftoi		*/
  C4X_BUILTIN_MPYI,	/*	fast_imult (only C3x)	*/
  C4X_BUILTIN_TOIEEE,	/*	toieee	   (only C4x)	*/
  C4X_BUILTIN_FRIEEE,	/*	frieee	   (only C4x)	*/
  C4X_BUILTIN_RCPF	/*	fast_invf  (only C4x)	*/
};


/* Hack to overcome use of libgcc2.c using auto-host.h to determine
   HAVE_GAS_HIDDEN.  */
#undef HAVE_GAS_HIDDEN
