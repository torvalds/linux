/* Definitions of target machine for GNU compiler,
   for Motorola M*CORE Processor.
   Copyright (C) 1993, 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#ifndef GCC_MCORE_H
#define GCC_MCORE_H

/* RBE: need to move these elsewhere.  */
#undef	LIKE_PPC_ABI 
#define	MCORE_STRUCT_ARGS
/* RBE: end of "move elsewhere".  */

/* Run-time Target Specification.  */
#define TARGET_MCORE

/* Get tree.c to declare a target-specific specialization of
   merge_decl_attributes.  */
#define TARGET_DLLIMPORT_DECL_ATTRIBUTES 1

#define TARGET_CPU_CPP_BUILTINS()					  \
  do									  \
    {									  \
      builtin_define ("__mcore__");					  \
      builtin_define ("__MCORE__");					  \
      if (TARGET_LITTLE_END)						  \
        builtin_define ("__MCORELE__");					  \
      else								  \
        builtin_define ("__MCOREBE__");					  \
      if (TARGET_M340)							  \
        builtin_define ("__M340__");					  \
      else								  \
        builtin_define ("__M210__");					  \
    }									  \
  while (0)

/* If -m4align is ever re-enabled then add this line to the definition of CPP_SPEC
   %{!m4align:-D__MCORE_ALIGN_8__} %{m4align:-D__MCORE__ALIGN_4__}.  */
#undef  CPP_SPEC
#define CPP_SPEC "%{m210:%{mlittle-endian:%ethe m210 does not have little endian support}}"

/* We don't have a -lg library, so don't put it in the list.  */
#undef	LIB_SPEC
#define LIB_SPEC "%{!shared: %{!p:%{!pg:-lc}}%{p:-lc_p}%{pg:-lc_p}}"

#undef	ASM_SPEC
#define	ASM_SPEC "%{mbig-endian:-EB} %{m210:-cpu=210 -EB}"

#undef  LINK_SPEC
#define LINK_SPEC "%{mbig-endian:-EB} %{m210:-EB} -X"

#define TARGET_DEFAULT	\
  (MASK_HARDLIT		\
   | MASK_8ALIGN	\
   | MASK_DIV		\
   | MASK_RELAX_IMM	\
   | MASK_M340		\
   | MASK_LITTLE_END)

#ifndef MULTILIB_DEFAULTS
#define MULTILIB_DEFAULTS { "mlittle-endian", "m340" }
#endif

/* The ability to have 4 byte alignment is being suppressed for now.
   If this ability is reenabled, you must disable the definition below
   *and* edit t-mcore to enable multilibs for 4 byte alignment code.  */
#undef TARGET_8ALIGN
#define TARGET_8ALIGN 1

extern char * mcore_current_function_name;
 
/* The MCore ABI says that bitfields are unsigned by default.  */
#define CC1_SPEC "-funsigned-bitfields"

/* What options are we going to default to specific settings when
   -O* happens; the user can subsequently override these settings.
  
   Omitting the frame pointer is a very good idea on the MCore.
   Scheduling isn't worth anything on the current MCore implementation.  */
#define OPTIMIZATION_OPTIONS(LEVEL,SIZE)	\
{						\
  if (LEVEL)					\
    {						\
      flag_no_function_cse = 1;			\
      flag_omit_frame_pointer = 1;		\
						\
      if (LEVEL >= 2)				\
        {					\
          flag_caller_saves = 0;		\
          flag_schedule_insns = 0;		\
          flag_schedule_insns_after_reload = 0;	\
        }					\
    }						\
  if (SIZE)					\
    {						\
      target_flags &= ~MASK_HARDLIT;		\
    }						\
}

/* What options are we going to force to specific settings,
   regardless of what the user thought he wanted.
   We also use this for some post-processing of options.  */
#define OVERRIDE_OPTIONS  mcore_override_options ()

/* Target machine storage Layout.  */

#define PROMOTE_MODE(MODE,UNSIGNEDP,TYPE)  	\
  if (GET_MODE_CLASS (MODE) == MODE_INT         \
      && GET_MODE_SIZE (MODE) < UNITS_PER_WORD) \
    {						\
      (MODE) = SImode;				\
      (UNSIGNEDP) = 1;				\
    }

/* Define this if most significant bit is lowest numbered
   in instructions that operate on numbered bit-fields.  */
#define BITS_BIG_ENDIAN  0

/* Define this if most significant byte of a word is the lowest numbered.  */
#define BYTES_BIG_ENDIAN (! TARGET_LITTLE_END)

/* Define this if most significant word of a multiword number is the lowest
   numbered.  */
#define WORDS_BIG_ENDIAN (! TARGET_LITTLE_END)

#define LIBGCC2_WORDS_BIG_ENDIAN 1
#ifdef __MCORELE__
#undef  LIBGCC2_WORDS_BIG_ENDIAN
#define LIBGCC2_WORDS_BIG_ENDIAN 0
#endif

#define MAX_BITS_PER_WORD 32

/* Width of a word, in units (bytes).  */
#define UNITS_PER_WORD	4

/* A C expression for the size in bits of the type `long long' on the
   target machine.  If you don't define this, the default is two
   words.  */
#define LONG_LONG_TYPE_SIZE 64

/* Allocation boundary (in *bits*) for storing arguments in argument list.  */
#define PARM_BOUNDARY  	32

/* Doubles must be aligned to an 8 byte boundary.  */
#define FUNCTION_ARG_BOUNDARY(MODE, TYPE) \
  ((MODE != BLKmode && (GET_MODE_SIZE (MODE) == 8)) \
   ? BIGGEST_ALIGNMENT : PARM_BOUNDARY)
     
/* Boundary (in *bits*) on which stack pointer should be aligned.  */
#define STACK_BOUNDARY  (TARGET_8ALIGN ? 64 : 32)

/* Largest increment in UNITS we allow the stack to grow in a single operation.  */
extern int mcore_stack_increment;
#define STACK_UNITS_MAXSTEP  4096

/* Allocation boundary (in *bits*) for the code of a function.  */
#define FUNCTION_BOUNDARY  ((TARGET_OVERALIGN_FUNC) ? 32 : 16)

/* Alignment of field after `int : 0' in a structure.  */
#define EMPTY_FIELD_BOUNDARY  32

/* No data type wants to be aligned rounder than this.  */
#define BIGGEST_ALIGNMENT  (TARGET_8ALIGN ? 64 : 32)

/* The best alignment to use in cases where we have a choice.  */
#define FASTEST_ALIGNMENT 32

/* Every structures size must be a multiple of 8 bits.  */
#define STRUCTURE_SIZE_BOUNDARY 8

/* Look at the fundamental type that is used for a bit-field and use 
   that to impose alignment on the enclosing structure.
   struct s {int a:8}; should have same alignment as "int", not "char".  */
#define	PCC_BITFIELD_TYPE_MATTERS	1

/* Largest integer machine mode for structures.  If undefined, the default
   is GET_MODE_SIZE(DImode).  */
#define MAX_FIXED_MODE_SIZE 32

/* Make strings word-aligned so strcpy from constants will be faster.  */
#define CONSTANT_ALIGNMENT(EXP, ALIGN)  \
  ((TREE_CODE (EXP) == STRING_CST	\
    && (ALIGN) < FASTEST_ALIGNMENT)	\
   ? FASTEST_ALIGNMENT : (ALIGN))

/* Make arrays of chars word-aligned for the same reasons.  */
#define DATA_ALIGNMENT(TYPE, ALIGN)		\
  (TREE_CODE (TYPE) == ARRAY_TYPE		\
   && TYPE_MODE (TREE_TYPE (TYPE)) == QImode	\
   && (ALIGN) < FASTEST_ALIGNMENT ? FASTEST_ALIGNMENT : (ALIGN))
     
/* Set this nonzero if move instructions will actually fail to work
   when given unaligned data.  */
#define STRICT_ALIGNMENT 1

/* Standard register usage.  */

/* Register allocation for our first guess 

	r0		stack pointer
	r1		scratch, target reg for xtrb?
	r2-r7		arguments.
	r8-r14		call saved
	r15		link register
	ap		arg pointer (doesn't really exist, always eliminated)
	c               c bit
	fp		frame pointer (doesn't really exist, always eliminated)
	x19		two control registers.  */

/* Number of actual hardware registers.
   The hardware registers are assigned numbers for the compiler
   from 0 to just below FIRST_PSEUDO_REGISTER.
   All registers that the compiler knows about must be given numbers,
   even those that are not normally considered general registers.

   MCore has 16 integer registers and 2 control registers + the arg
   pointer.  */

#define FIRST_PSEUDO_REGISTER 20

#define R1_REG  1	/* Where literals are forced.  */
#define LK_REG	15	/* Overloaded on general register.  */
#define AP_REG  16	/* Fake arg pointer register.  */
/* RBE: mcore.md depends on CC_REG being set to 17.  */
#define CC_REG	17	/* Can't name it C_REG.  */
#define FP_REG  18	/* Fake frame pointer register.  */

/* Specify the registers used for certain standard purposes.
   The values of these macros are register numbers.  */


#undef PC_REGNUM /* Define this if the program counter is overloaded on a register.  */
#define STACK_POINTER_REGNUM 0 /* Register to use for pushing function arguments.  */
#define FRAME_POINTER_REGNUM 8 /* When we need FP, use r8.  */

/* The assembler's names for the registers.  RFP need not always be used as
   the Real framepointer; it can also be used as a normal general register.
   Note that the name `fp' is horribly misleading since `fp' is in fact only
   the argument-and-return-context pointer.  */
#define REGISTER_NAMES  				\
{				                   	\
  "sp", "r1", "r2",  "r3",  "r4",  "r5",  "r6",  "r7", 	\
  "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",	\
  "apvirtual",  "c", "fpvirtual", "x19" \
}

/* 1 for registers that have pervasive standard uses
   and are not available for the register allocator.  */
#define FIXED_REGISTERS  \
 /*  r0  r1  r2  r3  r4  r5  r6  r7  r8  r9  r10 r11 r12 r13 r14 r15 ap  c  fp x19 */ \
   { 1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1, 1, 1}

/* 1 for registers not available across function calls.
   These must include the FIXED_REGISTERS and also any
   registers that can be used without being saved.
   The latter must include the registers where values are returned
   and the register where structure-value addresses are passed.
   Aside from that, you can include as many other registers as you like.  */

/* RBE: r15 {link register} not available across calls,
   But we don't mark it that way here....  */
#define CALL_USED_REGISTERS \
 /*  r0  r1  r2  r3  r4  r5  r6  r7  r8  r9  r10 r11 r12 r13 r14 r15 ap  c   fp x19 */ \
   { 1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1, 1}

/* The order in which register should be allocated.  */
#define REG_ALLOC_ORDER  \
 /* r7  r6  r5  r4  r3  r2  r15 r14 r13 r12 r11 r10  r9  r8  r1  r0  ap  c   fp x19*/ \
  {  7,  6,  5,  4,  3,  2,  15, 14, 13, 12, 11, 10,  9,  8,  1,  0, 16, 17, 18, 19}

/* Return number of consecutive hard regs needed starting at reg REGNO
   to hold something of mode MODE.
   This is ordinarily the length in words of a value of mode MODE
   but can be less for certain modes in special long registers.

   On the MCore regs are UNITS_PER_WORD bits wide; */
#define HARD_REGNO_NREGS(REGNO, MODE)  \
   (((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD))

/* Value is 1 if hard register REGNO can hold a value of machine-mode MODE.
   We may keep double values in even registers.  */
#define HARD_REGNO_MODE_OK(REGNO, MODE)  \
  ((TARGET_8ALIGN && GET_MODE_SIZE (MODE) > UNITS_PER_WORD) ? (((REGNO) & 1) == 0) : (REGNO < 18))

/* Value is 1 if it is a good idea to tie two pseudo registers
   when one has mode MODE1 and one has mode MODE2.
   If HARD_REGNO_MODE_OK could produce different values for MODE1 and MODE2,
   for any hard reg, then this must be 0 for correct output.  */
#define MODES_TIEABLE_P(MODE1, MODE2) \
  ((MODE1) == (MODE2) || GET_MODE_CLASS (MODE1) == GET_MODE_CLASS (MODE2))

/* Value should be nonzero if functions must have frame pointers.
   Zero means the frame pointer need not be set up (and parms may be accessed
   via the stack pointer) in functions that seem suitable.  */
#define FRAME_POINTER_REQUIRED	0

/* Definitions for register eliminations.

   We have two registers that can be eliminated on the MCore.  First, the
   frame pointer register can often be eliminated in favor of the stack
   pointer register.  Secondly, the argument pointer register can always be
   eliminated; it is replaced with either the stack or frame pointer.  */

/* Base register for access to arguments of the function.  */
#define ARG_POINTER_REGNUM	16

/* Register in which the static-chain is passed to a function.  */
#define STATIC_CHAIN_REGNUM	1

/* This is an array of structures.  Each structure initializes one pair
   of eliminable registers.  The "from" register number is given first,
   followed by "to".  Eliminations of the same "from" register are listed
   in order of preference.  */
#define ELIMINABLE_REGS				\
{{ FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM},	\
 { ARG_POINTER_REGNUM,   STACK_POINTER_REGNUM},	\
 { ARG_POINTER_REGNUM,   FRAME_POINTER_REGNUM},}

/* Given FROM and TO register numbers, say whether this elimination
   is allowed.  */
#define CAN_ELIMINATE(FROM, TO) \
  (!((FROM) == FRAME_POINTER_REGNUM && FRAME_POINTER_REQUIRED))

/* Define the offset between two registers, one to be eliminated, and the other
   its replacement, at the start of a routine.  */
#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET) \
  OFFSET = mcore_initial_elimination_offset (FROM, TO)

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

/* The MCore has only general registers. There are
   also some special purpose registers: the T bit register, the
   procedure Link and the Count Registers.  */
enum reg_class
{
  NO_REGS,
  ONLYR1_REGS,
  LRW_REGS,
  GENERAL_REGS,
  C_REGS,
  ALL_REGS,
  LIM_REG_CLASSES
};

#define N_REG_CLASSES  (int) LIM_REG_CLASSES

/* Give names of register classes as strings for dump file.  */
#define REG_CLASS_NAMES  \
{			\
  "NO_REGS",		\
  "ONLYR1_REGS",	\
  "LRW_REGS",		\
  "GENERAL_REGS",	\
  "C_REGS",		\
  "ALL_REGS",		\
}

/* Define which registers fit in which classes.
   This is an initializer for a vector of HARD_REG_SET
   of length N_REG_CLASSES.  */

/* ??? STACK_POINTER_REGNUM should be excluded from LRW_REGS.  */
#define REG_CLASS_CONTENTS      	\
{					\
  {0x000000},  /* NO_REGS       */	\
  {0x000002},  /* ONLYR1_REGS   */	\
  {0x007FFE},  /* LRW_REGS      */	\
  {0x01FFFF},  /* GENERAL_REGS  */	\
  {0x020000},  /* C_REGS        */	\
  {0x0FFFFF}   /* ALL_REGS      */	\
}

/* The same information, inverted:
   Return the class number of the smallest class containing
   reg number REGNO.  This could be a conditional expression
   or could index an array.  */

extern const int regno_reg_class[FIRST_PSEUDO_REGISTER];
#define REGNO_REG_CLASS(REGNO) regno_reg_class[REGNO]

/* When defined, the compiler allows registers explicitly used in the
   rtl to be used as spill registers but prevents the compiler from
   extending the lifetime of these registers.  */
#define SMALL_REGISTER_CLASSES 1
 
/* The class value for index registers, and the one for base regs.  */
#define INDEX_REG_CLASS  NO_REGS
#define BASE_REG_CLASS	 GENERAL_REGS

/* Get reg_class from a letter such as appears in the machine 
   description.  */
extern const enum reg_class reg_class_from_letter[];

#define REG_CLASS_FROM_LETTER(C) \
   (ISLOWER (C) ? reg_class_from_letter[(C) - 'a'] : NO_REGS)

/* The letters I, J, K, L, M, N, O, and P in a register constraint string
   can be used to stand for particular ranges of immediate operands.
   This macro defines what the ranges are.
   C is the letter, and VALUE is a constant value.
   Return 1 if VALUE is in the range specified by C.
	I: loadable by movi (0..127)
	J: arithmetic operand 1..32
	K: shift operand 0..31
	L: negative arithmetic operand -1..-32
	M: powers of two, constants loadable by bgeni
	N: powers of two minus 1, constants loadable by bmaski, including -1
        O: allowed by cmov with two constants +/- 1 of each other
        P: values we will generate 'inline' -- without an 'lrw'

   Others defined for use after reload
        Q: constant 1
	R: a label
        S: 0/1/2 cleared bits out of 32	[for bclri's]
        T: 2 set bits out of 32	[for bseti's]
        U: constant 0
        xxxS: 1 cleared bit out of 32 (complement of power of 2). for bclri
        xxxT: 2 cleared bits out of 32. for pairs of bclris.  */
#define CONST_OK_FOR_I(VALUE) (((int)(VALUE)) >= 0 && ((int)(VALUE)) <= 0x7f)
#define CONST_OK_FOR_J(VALUE) (((int)(VALUE)) >  0 && ((int)(VALUE)) <= 32)
#define CONST_OK_FOR_L(VALUE) (((int)(VALUE)) <  0 && ((int)(VALUE)) >= -32)
#define CONST_OK_FOR_K(VALUE) (((int)(VALUE)) >= 0 && ((int)(VALUE)) <= 31)
#define CONST_OK_FOR_M(VALUE) (exact_log2 (VALUE) >= 0)
#define CONST_OK_FOR_N(VALUE) (((int)(VALUE)) == -1 || exact_log2 ((VALUE) + 1) >= 0)
#define CONST_OK_FOR_O(VALUE) (CONST_OK_FOR_I(VALUE) || \
                               CONST_OK_FOR_M(VALUE) || \
                               CONST_OK_FOR_N(VALUE) || \
                               CONST_OK_FOR_M((int)(VALUE) - 1) || \
                               CONST_OK_FOR_N((int)(VALUE) + 1))

#define CONST_OK_FOR_P(VALUE) (mcore_const_ok_for_inline (VALUE)) 

#define CONST_OK_FOR_LETTER_P(VALUE, C)     \
     ((C) == 'I' ? CONST_OK_FOR_I (VALUE)   \
    : (C) == 'J' ? CONST_OK_FOR_J (VALUE)   \
    : (C) == 'L' ? CONST_OK_FOR_L (VALUE)   \
    : (C) == 'K' ? CONST_OK_FOR_K (VALUE)   \
    : (C) == 'M' ? CONST_OK_FOR_M (VALUE)   \
    : (C) == 'N' ? CONST_OK_FOR_N (VALUE)   \
    : (C) == 'P' ? CONST_OK_FOR_P (VALUE)   \
    : (C) == 'O' ? CONST_OK_FOR_O (VALUE)   \
    : 0)

/* Similar, but for floating constants, and defining letters G and H.
   Here VALUE is the CONST_DOUBLE rtx itself.  */
#define CONST_DOUBLE_OK_FOR_LETTER_P(VALUE, C) \
   ((C) == 'G' ? CONST_OK_FOR_I (CONST_DOUBLE_HIGH (VALUE)) \
	      && CONST_OK_FOR_I (CONST_DOUBLE_LOW (VALUE))  \
    : 0)

/* Letters in the range `Q' through `U' in a register constraint string
   may be defined in a machine-dependent fashion to stand for arbitrary
   operand types.  */
#define EXTRA_CONSTRAINT(OP, C)				\
  ((C) == 'R' ? (GET_CODE (OP) == MEM			\
		 && GET_CODE (XEXP (OP, 0)) == LABEL_REF) \
   : (C) == 'S' ? (GET_CODE (OP) == CONST_INT \
                   && mcore_num_zeros (INTVAL (OP)) <= 2) \
   : (C) == 'T' ? (GET_CODE (OP) == CONST_INT \
                   && mcore_num_ones (INTVAL (OP)) == 2) \
   : (C) == 'Q' ? (GET_CODE (OP) == CONST_INT \
                   && INTVAL(OP) == 1) \
   : (C) == 'U' ? (GET_CODE (OP) == CONST_INT \
                   && INTVAL(OP) == 0) \
   : 0)

/* Given an rtx X being reloaded into a reg required to be
   in class CLASS, return the class of reg to actually use.
   In general this is just CLASS; but on some machines
   in some cases it is preferable to use a more restrictive class.  */
#define PREFERRED_RELOAD_CLASS(X, CLASS) mcore_reload_class (X, CLASS)

/* Return the register class of a scratch register needed to copy IN into
   or out of a register in CLASS in MODE.  If it can be done directly,
   NO_REGS is returned.  */
#define SECONDARY_RELOAD_CLASS(CLASS, MODE, X) \
  mcore_secondary_reload_class (CLASS, MODE, X)

/* Return the maximum number of consecutive registers
   needed to represent mode MODE in a register of class CLASS. 

   On MCore this is the size of MODE in words.  */
#define CLASS_MAX_NREGS(CLASS, MODE)  \
     (ROUND_ADVANCE (GET_MODE_SIZE (MODE)))

/* Stack layout; function entry, exit and calling.  */

/* Define the number of register that can hold parameters.
   These two macros are used only in other macro definitions below.  */
#define NPARM_REGS 6
#define FIRST_PARM_REG 2
#define FIRST_RET_REG 2

/* Define this if pushing a word on the stack
   makes the stack pointer a smaller address.  */
#define STACK_GROWS_DOWNWARD  

/* Offset within stack frame to start allocating local variables at.
   If FRAME_GROWS_DOWNWARD, this is the offset to the END of the
   first local allocated.  Otherwise, it is the offset to the BEGINNING
   of the first local allocated.  */
#define STARTING_FRAME_OFFSET  0

/* If defined, the maximum amount of space required for outgoing arguments
   will be computed and placed into the variable
   `current_function_outgoing_args_size'.  No space will be pushed
   onto the stack for each call; instead, the function prologue should
   increase the stack frame size by this amount.  */
#define ACCUMULATE_OUTGOING_ARGS 1

/* Offset of first parameter from the argument pointer register value.  */
#define FIRST_PARM_OFFSET(FNDECL)  0

/* Value is the number of byte of arguments automatically
   popped when returning from a subroutine call.
   FUNTYPE is the data type of the function (as a tree),
   or for a library call it is an identifier node for the subroutine name.
   SIZE is the number of bytes of arguments passed on the stack.

   On the MCore, the callee does not pop any of its arguments that were passed
   on the stack.  */
#define RETURN_POPS_ARGS(FUNDECL,FUNTYPE,SIZE) 0

/* Define how to find the value returned by a function.
   VALTYPE is the data type of the value (as a tree).
   If the precise function being called is known, FUNC is its FUNCTION_DECL;
   otherwise, FUNC is 0.  */
#define FUNCTION_VALUE(VALTYPE, FUNC)  mcore_function_value (VALTYPE, FUNC)

/* Don't default to pcc-struct-return, because gcc is the only compiler, and
   we want to retain compatibility with older gcc versions.  */
#define DEFAULT_PCC_STRUCT_RETURN 0

/* Define how to find the value returned by a library function
   assuming the value has mode MODE.  */
#define LIBCALL_VALUE(MODE)  gen_rtx_REG (MODE, FIRST_RET_REG)

/* 1 if N is a possible register number for a function value.
   On the MCore, only r4 can return results.  */
#define FUNCTION_VALUE_REGNO_P(REGNO)  ((REGNO) == FIRST_RET_REG)

/* 1 if N is a possible register number for function argument passing.  */
#define FUNCTION_ARG_REGNO_P(REGNO)  \
  ((REGNO) >= FIRST_PARM_REG && (REGNO) < (NPARM_REGS + FIRST_PARM_REG))

/* Define a data type for recording info about an argument list
   during the scan of that argument list.  This data type should
   hold all necessary information about the function itself
   and about the args processed so far, enough to enable macros
   such as FUNCTION_ARG to determine where the next arg should go.

   On MCore, this is a single integer, which is a number of words
   of arguments scanned so far (including the invisible argument,
   if any, which holds the structure-value-address).
   Thus NARGREGS or more means all following args should go on the stack.  */
#define CUMULATIVE_ARGS  int

#define ROUND_ADVANCE(SIZE)	\
  ((SIZE + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* Round a register number up to a proper boundary for an arg of mode 
   MODE. 
   
   We round to an even reg for things larger than a word.  */
#define ROUND_REG(X, MODE) 				\
  ((TARGET_8ALIGN 					\
   && GET_MODE_UNIT_SIZE ((MODE)) > UNITS_PER_WORD) 	\
   ? ((X) + ((X) & 1)) : (X))


/* Initialize a variable CUM of type CUMULATIVE_ARGS
   for a call to a function whose data type is FNTYPE.
   For a library call, FNTYPE is 0.

   On MCore, the offset always starts at 0: the first parm reg is always
   the same reg.  */
#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, INDIRECT, N_NAMED_ARGS) \
  ((CUM) = 0)

/* Update the data in CUM to advance over an argument
   of mode MODE and data type TYPE.
   (TYPE is null for libcalls where that information may not be
   available.)  */
#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED)	   \
 ((CUM) = (ROUND_REG ((CUM), (MODE))			   \
	   + ((NAMED) * mcore_num_arg_regs (MODE, TYPE)))) \

/* Define where to put the arguments to a function.  */
#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED) \
  mcore_function_arg (CUM, MODE, TYPE, NAMED)

/* Call the function profiler with a given profile label.  */
#define FUNCTION_PROFILER(STREAM,LABELNO)		\
{							\
  fprintf (STREAM, "	trap	1\n");			\
  fprintf (STREAM, "	.align	2\n");			\
  fprintf (STREAM, "	.long	LP%d\n", (LABELNO));	\
}

/* EXIT_IGNORE_STACK should be nonzero if, when returning from a function,
   the stack pointer does not matter.  The value is tested only in
   functions that have frame pointers.
   No definition is equivalent to always zero.  */
#define EXIT_IGNORE_STACK 0

/* Output assembler code for a block containing the constant parts
   of a trampoline, leaving space for the variable parts.

   On the MCore, the trampoline looks like:
   	lrw	r1,  function
     	lrw	r13, area
   	jmp	r13
   	or	r0, r0
    .literals                                                */
#define TRAMPOLINE_TEMPLATE(FILE)  		\
{						\
  fprintf ((FILE), "	.short	0x7102\n");	\
  fprintf ((FILE), "	.short	0x7d02\n");	\
  fprintf ((FILE), "	.short	0x00cd\n");     \
  fprintf ((FILE), "	.short	0x1e00\n");	\
  fprintf ((FILE), "	.long	0\n");		\
  fprintf ((FILE), "	.long	0\n");		\
}

/* Length in units of the trampoline for entering a nested function.  */
#define TRAMPOLINE_SIZE  12

/* Alignment required for a trampoline in bits.  */
#define TRAMPOLINE_ALIGNMENT  32

/* Emit RTL insns to initialize the variable parts of a trampoline.
   FNADDR is an RTX for the address of the function's pure code.
   CXT is an RTX for the static chain value for the function.  */
#define INITIALIZE_TRAMPOLINE(TRAMP, FNADDR, CXT)  \
{									\
  emit_move_insn (gen_rtx_MEM (SImode, plus_constant ((TRAMP), 8)),	\
		  (CXT));						\
  emit_move_insn (gen_rtx_MEM (SImode, plus_constant ((TRAMP), 12)),	\
		  (FNADDR));						\
}

/* Macros to check register numbers against specific register classes.  */

/* These assume that REGNO is a hard or pseudo reg number.
   They give nonzero only if REGNO is a hard reg of the suitable class
   or a pseudo reg currently allocated to a suitable hard reg.
   Since they use reg_renumber, they are safe only once reg_renumber
   has been allocated, which happens in local-alloc.c.  */
#define REGNO_OK_FOR_BASE_P(REGNO)  \
  ((REGNO) < AP_REG || (unsigned) reg_renumber[(REGNO)] < AP_REG)

#define REGNO_OK_FOR_INDEX_P(REGNO)   0

/* Maximum number of registers that can appear in a valid memory 
   address.  */
#define MAX_REGS_PER_ADDRESS 1

/* Recognize any constant value that is a valid address.  */
#define CONSTANT_ADDRESS_P(X) 	 (GET_CODE (X) == LABEL_REF)

/* Nonzero if the constant value X is a legitimate general operand.
   It is given that X satisfies CONSTANT_P or is a CONST_DOUBLE.

   On the MCore, allow anything but a double.  */
#define LEGITIMATE_CONSTANT_P(X) (GET_CODE(X) != CONST_DOUBLE)

/* The macros REG_OK_FOR..._P assume that the arg is a REG rtx
   and check its validity for a certain class.
   We have two alternate definitions for each of them.
   The usual definition accepts all pseudo regs; the other rejects
   them unless they have been allocated suitable hard regs.
   The symbol REG_OK_STRICT causes the latter definition to be used.  */
#ifndef REG_OK_STRICT

/* Nonzero if X is a hard reg that can be used as a base reg
   or if it is a pseudo reg.  */
#define REG_OK_FOR_BASE_P(X) \
    	(REGNO (X) <= 16 || REGNO (X) >= FIRST_PSEUDO_REGISTER)

/* Nonzero if X is a hard reg that can be used as an index
   or if it is a pseudo reg.  */
#define REG_OK_FOR_INDEX_P(X)	0

#else

/* Nonzero if X is a hard reg that can be used as a base reg.  */
#define REG_OK_FOR_BASE_P(X)	\
	REGNO_OK_FOR_BASE_P (REGNO (X))

/* Nonzero if X is a hard reg that can be used as an index.  */
#define REG_OK_FOR_INDEX_P(X)	0

#endif
/* GO_IF_LEGITIMATE_ADDRESS recognizes an RTL expression
   that is a valid memory address for an instruction.
   The MODE argument is the machine mode for the MEM expression
   that wants to use this address.

   The other macros defined here are used only in GO_IF_LEGITIMATE_ADDRESS.  */
#define BASE_REGISTER_RTX_P(X)  \
  (GET_CODE (X) == REG && REG_OK_FOR_BASE_P (X))

#define INDEX_REGISTER_RTX_P(X)  \
  (GET_CODE (X) == REG && REG_OK_FOR_INDEX_P (X))


/* Jump to LABEL if X is a valid address RTX.  This must also take
   REG_OK_STRICT into account when deciding about valid registers, but it uses
   the above macros so we are in luck.  
 
   Allow  REG
	  REG+disp 

   A legitimate index for a QI is 0..15, for HI is 0..30, for SI is 0..60,
   and for DI is 0..56 because we use two SI loads, etc.  */
#define GO_IF_LEGITIMATE_INDEX(MODE, REGNO, OP, LABEL)			\
  do									\
    {									\
      if (GET_CODE (OP) == CONST_INT) 					\
        {								\
	  if (GET_MODE_SIZE (MODE) >= 4					\
	      && (((unsigned)INTVAL (OP)) % 4) == 0			\
	      &&  ((unsigned)INTVAL (OP)) <= 64 - GET_MODE_SIZE (MODE))	\
	    goto LABEL;							\
	  if (GET_MODE_SIZE (MODE) == 2 				\
	      && (((unsigned)INTVAL (OP)) % 2) == 0			\
	      &&  ((unsigned)INTVAL (OP)) <= 30)			\
	    goto LABEL;							\
	  if (GET_MODE_SIZE (MODE) == 1 				\
	      && ((unsigned)INTVAL (OP)) <= 15)				\
	    goto LABEL;							\
        }								\
    }									\
  while (0)

#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, LABEL)                  \
{ 								  \
  if (BASE_REGISTER_RTX_P (X))					  \
    goto LABEL;							  \
  else if (GET_CODE (X) == PLUS || GET_CODE (X) == LO_SUM) 	  \
    {								  \
      rtx xop0 = XEXP (X,0);					  \
      rtx xop1 = XEXP (X,1);					  \
      if (BASE_REGISTER_RTX_P (xop0))				  \
	GO_IF_LEGITIMATE_INDEX (MODE, REGNO (xop0), xop1, LABEL); \
      if (BASE_REGISTER_RTX_P (xop1))				  \
	GO_IF_LEGITIMATE_INDEX (MODE, REGNO (xop1), xop0, LABEL); \
    }								  \
}								   
								   
/* Go to LABEL if ADDR (a legitimate address expression)
   has an effect that depends on the machine mode it is used for.  */
#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR,LABEL)  \
{									\
  if (   GET_CODE (ADDR) == PRE_DEC || GET_CODE (ADDR) == POST_DEC	\
      || GET_CODE (ADDR) == PRE_INC || GET_CODE (ADDR) == POST_INC)	\
    goto LABEL;								\
}

/* Specify the machine mode that this machine uses
   for the index in the tablejump instruction.  */
#define CASE_VECTOR_MODE SImode

/* 'char' is signed by default.  */
#define DEFAULT_SIGNED_CHAR  0

/* The type of size_t unsigned int.  */
#define SIZE_TYPE "unsigned int"

/* Max number of bytes we can move from memory to memory
   in one reasonably fast instruction.  */
#define MOVE_MAX 4

/* Define if operations between registers always perform the operation
   on the full register even if a narrower mode is specified.  */
#define WORD_REGISTER_OPERATIONS

/* Define if loading in MODE, an integral mode narrower than BITS_PER_WORD
   will either zero-extend or sign-extend.  The value of this macro should
   be the code that says which one of the two operations is implicitly
   done, UNKNOWN if none.  */
#define LOAD_EXTEND_OP(MODE) ZERO_EXTEND

/* Nonzero if access to memory by bytes is slow and undesirable.  */
#define SLOW_BYTE_ACCESS TARGET_SLOW_BYTES

/* Shift counts are truncated to 6-bits (0 to 63) instead of the expected
   5-bits, so we can not define SHIFT_COUNT_TRUNCATED to true for this
   target.  */
#define SHIFT_COUNT_TRUNCATED 0

/* All integers have the same format so truncation is easy.  */
#define TRULY_NOOP_TRUNCATION(OUTPREC,INPREC)  1

/* Define this if addresses of constant functions
   shouldn't be put through pseudo regs where they can be cse'd.
   Desirable on machines where ordinary constants are expensive
   but a CALL with constant address is cheap.  */
/* Why is this defined??? -- dac */
#define NO_FUNCTION_CSE 1

/* The machine modes of pointers and functions.  */
#define Pmode          SImode
#define FUNCTION_MODE  Pmode

/* Compute extra cost of moving data between one register class
   and another.  All register moves are cheap.  */
#define REGISTER_MOVE_COST(MODE, SRCCLASS, DSTCLASS) 2

#define WORD_REGISTER_OPERATIONS

/* Assembler output control.  */
#define ASM_COMMENT_START "\t//"

#define ASM_APP_ON	"// inline asm begin\n"
#define ASM_APP_OFF	"// inline asm end\n"

#define FILE_ASM_OP     "\t.file\n"

/* Switch to the text or data segment.  */
#define TEXT_SECTION_ASM_OP  "\t.text"
#define DATA_SECTION_ASM_OP  "\t.data"

/* Switch into a generic section.  */
#undef TARGET_ASM_NAMED_SECTION
#define TARGET_ASM_NAMED_SECTION  mcore_asm_named_section

/* This is how to output an insn to push a register on the stack.
   It need not be very fast code.  */
#define ASM_OUTPUT_REG_PUSH(FILE,REGNO)  \
  fprintf (FILE, "\tsubi\t %s,%d\n\tstw\t %s,(%s)\n",	\
	   reg_names[STACK_POINTER_REGNUM],		\
	   (STACK_BOUNDARY / BITS_PER_UNIT),		\
	   reg_names[REGNO],				\
	   reg_names[STACK_POINTER_REGNUM])

/* Length in instructions of the code output by ASM_OUTPUT_REG_PUSH.  */
#define REG_PUSH_LENGTH 2

/* This is how to output an insn to pop a register from the stack.  */
#define ASM_OUTPUT_REG_POP(FILE,REGNO)  \
  fprintf (FILE, "\tldw\t %s,(%s)\n\taddi\t %s,%d\n",	\
	   reg_names[REGNO],				\
	   reg_names[STACK_POINTER_REGNUM],		\
	   reg_names[STACK_POINTER_REGNUM],		\
	   (STACK_BOUNDARY / BITS_PER_UNIT))

  
/* Output a reference to a label.  */
#undef  ASM_OUTPUT_LABELREF
#define ASM_OUTPUT_LABELREF(STREAM, NAME)  \
  fprintf (STREAM, "%s%s", USER_LABEL_PREFIX, \
	   (* targetm.strip_name_encoding) (NAME))

/* This is how to output an assembler line
   that says to advance the location counter
   to a multiple of 2**LOG bytes.  */
#define ASM_OUTPUT_ALIGN(FILE,LOG)	\
  if ((LOG) != 0)			\
    fprintf (FILE, "\t.align\t%d\n", LOG)

#ifndef ASM_DECLARE_RESULT
#define ASM_DECLARE_RESULT(FILE, RESULT)
#endif

#define MULTIPLE_SYMBOL_SPACES 1

#define SUPPORTS_ONE_ONLY 1

/* A pair of macros to output things for the callgraph data.
   VALUE means (to the tools that reads this info later):
  	0 a call from src to dst
  	1 the call is special (e.g. dst is "unknown" or "alloca")
  	2 the call is special (e.g., the src is a table instead of routine)
  
   Frame sizes are augmented with timestamps to help later tools 
   differentiate between static entities with same names in different
   files.  */
extern long mcore_current_compilation_timestamp;
#define	ASM_OUTPUT_CG_NODE(FILE,SRCNAME,VALUE)				\
  do									\
    {									\
      if (mcore_current_compilation_timestamp == 0)			\
        mcore_current_compilation_timestamp = time (0);			\
      fprintf ((FILE),"\t.equ\t__$frame$size$_%s_$_%08lx,%d\n",		\
             (SRCNAME), mcore_current_compilation_timestamp, (VALUE));	\
    }									\
  while (0)

#define	ASM_OUTPUT_CG_EDGE(FILE,SRCNAME,DSTNAME,VALUE)		\
  do								\
    {								\
      fprintf ((FILE),"\t.equ\t__$function$call$_%s_$_%s,%d\n",	\
             (SRCNAME), (DSTNAME), (VALUE));			\
    }								\
  while (0)

/* Globalizing directive for a label.  */
#define GLOBAL_ASM_OP "\t.export\t"

/* The prefix to add to user-visible assembler symbols.  */
#undef  USER_LABEL_PREFIX
#define USER_LABEL_PREFIX ""

/* Make an internal label into a string.  */
#undef  ASM_GENERATE_INTERNAL_LABEL
#define ASM_GENERATE_INTERNAL_LABEL(STRING, PREFIX, NUM)  \
  sprintf (STRING, "*.%s%ld", PREFIX, (long) NUM)

/* Jump tables must be 32 bit aligned.  */
#undef  ASM_OUTPUT_CASE_LABEL
#define ASM_OUTPUT_CASE_LABEL(STREAM,PREFIX,NUM,TABLE) \
  fprintf (STREAM, "\t.align 2\n.%s%d:\n", PREFIX, NUM);

/* Output a relative address. Not needed since jump tables are absolute
   but we must define it anyway.  */
#define ASM_OUTPUT_ADDR_DIFF_ELT(STREAM,BODY,VALUE,REL)  \
  fputs ("- - - ASM_OUTPUT_ADDR_DIFF_ELT called!\n", STREAM)

/* Output an element of a dispatch table.  */
#define ASM_OUTPUT_ADDR_VEC_ELT(STREAM,VALUE)  \
    fprintf (STREAM, "\t.long\t.L%d\n", VALUE)

/* Output various types of constants.  */

/* This is how to output an assembler line
   that says to advance the location counter by SIZE bytes.  */
#undef  ASM_OUTPUT_SKIP
#define ASM_OUTPUT_SKIP(FILE,SIZE)  \
  fprintf (FILE, "\t.fill %d, 1\n", (int)(SIZE))

/* This says how to output an assembler line
   to define a global common symbol, with alignment information.  */
/* XXX - for now we ignore the alignment.  */     
#undef  ASM_OUTPUT_ALIGNED_COMMON
#define ASM_OUTPUT_ALIGNED_COMMON(FILE, NAME, SIZE, ALIGN)	\
  do								\
    {								\
      if (mcore_dllexport_name_p (NAME))			\
	MCORE_EXPORT_NAME (FILE, NAME)				\
      if (! mcore_dllimport_name_p (NAME))			\
        {							\
          fputs ("\t.comm\t", FILE);				\
          assemble_name (FILE, NAME);				\
          fprintf (FILE, ",%lu\n", (unsigned long)(SIZE));	\
        }							\
    }								\
  while (0)

/* This says how to output an assembler line
   to define a local common symbol....  */
#undef  ASM_OUTPUT_LOCAL
#define ASM_OUTPUT_LOCAL(FILE, NAME, SIZE, ROUNDED)	\
  (fputs ("\t.lcomm\t", FILE),				\
  assemble_name (FILE, NAME),				\
  fprintf (FILE, ",%d\n", (int)SIZE))

/* ... and how to define a local common symbol whose alignment
   we wish to specify.  ALIGN comes in as bits, we have to turn
   it into bytes.  */
#undef  ASM_OUTPUT_ALIGNED_LOCAL
#define ASM_OUTPUT_ALIGNED_LOCAL(FILE, NAME, SIZE, ALIGN)		\
  do									\
    {									\
      fputs ("\t.bss\t", (FILE));					\
      assemble_name ((FILE), (NAME));					\
      fprintf ((FILE), ",%d,%d\n", (int)(SIZE), (ALIGN) / BITS_PER_UNIT);\
    }									\
  while (0)

/* Print operand X (an rtx) in assembler syntax to file FILE.
   CODE is a letter or dot (`z' in `%z0') or 0 if no letter was specified.
   For `%' followed by punctuation, CODE is the punctuation and X is null.  */
#define PRINT_OPERAND(STREAM, X, CODE)  mcore_print_operand (STREAM, X, CODE)

/* Print a memory address as an operand to reference that memory location.  */
#define PRINT_OPERAND_ADDRESS(STREAM,X)  mcore_print_operand_address (STREAM, X)

#define PRINT_OPERAND_PUNCT_VALID_P(CHAR) \
  ((CHAR)=='.' || (CHAR) == '#' || (CHAR) == '*' || (CHAR) == '^' || (CHAR) == '!')

#endif /* ! GCC_MCORE_H */
