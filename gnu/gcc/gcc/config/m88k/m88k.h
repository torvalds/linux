/* Definitions of target machine for GNU compiler for
   Motorola m88100 in an 88open OCS/BCS environment.
   Copyright (C) 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002 Free Software Foundation, Inc.
   Contributed by Michael Tiemann (tiemann@cygnus.com).
   Currently maintained by (gcc@dg-rtp.dg.com)

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

/* The m88100 port of GCC mostly adheres to the various standards from 88open.
   These documents used to be available by writing to:

	88open Consortium Ltd.
	100 Homeland Court, Suite 800
	San Jose, CA  95112
	(408) 436-6600

   In brief, the current standards are:

   Binary Compatibility Standard, Release 1.1A, May 1991
	This provides for portability of application-level software at the
	executable level for AT&T System V Release 3.2.

   Object Compatibility Standard, Release 1.1A, May 1991
	This provides for portability of application-level software at the
	object file and library level for C, Fortran, and Cobol, and again,
	largely for SVR3.

   Under development are standards for AT&T System V Release 4, based on the
   [generic] System V Application Binary Interface from AT&T.  These include:

   System V Application Binary Interface, Motorola 88000 Processor Supplement
	Another document from AT&T for SVR4 specific to the m88100.
	Available from Prentice Hall.

   System V Application Binary Interface, Motorola 88000 Processor Supplement,
   Release 1.1, Draft H, May 6, 1991
	A proposed update to the AT&T document from 88open.

   System V ABI Implementation Guide for the M88000 Processor,
   Release 1.0, January 1991
	A companion ABI document from 88open.  */

/* External types used.  */

/* What instructions are needed to manufacture an integer constant.  */
enum m88k_instruction {
  m88k_zero,
  m88k_or,
  m88k_subu,
  m88k_or_lo16,
  m88k_or_lo8,
  m88k_set,
  m88k_oru_hi16,
  m88k_oru_or
};

/* Which processor to schedule for.  The elements of the enumeration
   must match exactly the cpu attribute in the m88k.md machine description. */

enum processor_type {
  PROCESSOR_M88100,
  PROCESSOR_M88110,
  PROCESSOR_M88000
};

/* Recast the cpu class to be the cpu attribute.  */
#define m88k_cpu_attr ((enum attr_cpu)m88k_cpu)

/* External variables/functions defined in m88k.c.  */

extern char m88k_volatile_code;
extern int m88k_case_index;

extern rtx m88k_compare_reg;
extern rtx m88k_compare_op0;
extern rtx m88k_compare_op1;

extern enum processor_type m88k_cpu;

/*** Controlling the Compilation Driver, `gcc' ***/
/* Show we can debug even without a frame pointer.  */
#define CAN_DEBUG_WITHOUT_FP

/* If -m88100 is in effect, add -D__m88100__; similarly for -m88110.
   Here, the CPU_DEFAULT is assumed to be -m88100.  */
#undef	CPP_SPEC
#define CPP_SPEC "%{!m88000:%{!m88100:%{m88110:-D__m88110__}}}		\
		  %{!m88000:%{!m88110:-D__m88100__}}"

/*** Run-time Target Specification ***/

#define VERSION_INFO	"m88k"
#define TARGET_VERSION fprintf (stderr, " (%s)", VERSION_INFO)

#define TARGET_DEFAULT		(MASK_CHECK_ZERO_DIV)
#define CPU_DEFAULT		MASK_88100

#define OVERRIDE_OPTIONS	m88k_override_options ()

/* Run-time target specifications.  */
#define TARGET_CPU_CPP_BUILTINS()					\
  do									\
    {									\
      builtin_define ("__m88k");					\
      builtin_define ("__m88k__");					\
      builtin_assert ("cpu=m88k");					\
      builtin_assert ("machine=m88k");					\
      if (TARGET_88100)							\
	builtin_define ("__mc88100__");					\
      else if (TARGET_88110)						\
	builtin_define ("__mc88110__");					\
      else								\
	builtin_define ("__mc88000__");					\
    }									\
  while (0)


/*** Storage Layout ***/

/* Sizes in bits of the various types.  */
#define SHORT_TYPE_SIZE		16
#define INT_TYPE_SIZE		32
#define LONG_TYPE_SIZE		32
#define LONG_LONG_TYPE_SIZE	64
#define FLOAT_TYPE_SIZE		32
#define DOUBLE_TYPE_SIZE	64
#define LONG_DOUBLE_TYPE_SIZE	64

/* Define this if most significant bit is lowest numbered
   in instructions that operate on numbered bit-fields.
   Somewhat arbitrary.  It matches the bit field patterns.  */
#define BITS_BIG_ENDIAN 1

/* Define this if most significant byte of a word is the lowest numbered.
   That is true on the m88000.  */
#define BYTES_BIG_ENDIAN 1

/* Define this if most significant word of a multiword number is the lowest
   numbered.
   For the m88000 we can decide arbitrarily since there are no machine
   instructions for them.  */
#define WORDS_BIG_ENDIAN 1

/* Width of a word, in units (bytes).  */
#define UNITS_PER_WORD 4

/* A macro to update MODE and UNSIGNEDP when an object whose type is TYPE and
   which has the specified mode and signedness is to be stored in a register.
   This macro is only called when TYPE is a scalar type.  */
#define PROMOTE_MODE(MODE,UNSIGNEDP,TYPE)				\
do									\
  {									\
    if (GET_MODE_CLASS (MODE) == MODE_INT				\
	&& GET_MODE_SIZE (MODE) < UNITS_PER_WORD)			\
      (MODE) = SImode;							\
  }									\
while (0)

/* Allocation boundary (in *bits*) for storing arguments in argument list.  */
#define PARM_BOUNDARY 32

/* Largest alignment for stack parameters (if greater than PARM_BOUNDARY).  */
#define MAX_PARM_BOUNDARY 64

/* Boundary (in *bits*) on which stack pointer should be aligned.  */
#define STACK_BOUNDARY 128

/* Allocation boundary (in *bits*) for the code of a function.  */
#define FUNCTION_BOUNDARY 32

/* No data type wants to be aligned rounder than this.  */
#define BIGGEST_ALIGNMENT 64

/* The best alignment to use in cases where we have a choice.  */
#define FASTEST_ALIGNMENT (TARGET_88100 ? 32 : 64)

/* Make strings 4/8 byte aligned so strcpy from constants will be faster.  */
#define CONSTANT_ALIGNMENT(EXP, ALIGN)					\
  ((TREE_CODE (EXP) == STRING_CST					\
    && (ALIGN) < FASTEST_ALIGNMENT)					\
   ? FASTEST_ALIGNMENT : (ALIGN))

/* Make arrays of chars 4/8 byte aligned for the same reasons.  */
#define DATA_ALIGNMENT(TYPE, ALIGN)					\
  (TREE_CODE (TYPE) == ARRAY_TYPE					\
   && TYPE_MODE (TREE_TYPE (TYPE)) == QImode				\
   && (ALIGN) < FASTEST_ALIGNMENT ? FASTEST_ALIGNMENT : (ALIGN))

/* Make local arrays of chars 4/8 byte aligned for the same reasons.  */
#define LOCAL_ALIGNMENT(TYPE, ALIGN) DATA_ALIGNMENT (TYPE, ALIGN)

/* Alignment of field after `int : 0' in a structure.
   Ignored with PCC_BITFIELD_TYPE_MATTERS.  */
/* #define EMPTY_FIELD_BOUNDARY 8 */

/* Every structure's size must be a multiple of this.  */
#define STRUCTURE_SIZE_BOUNDARY 8

/* Set this nonzero if move instructions will actually fail to work
   when given unaligned data.  */
#define STRICT_ALIGNMENT 1

/* A bit-field declared as `int' forces `int' alignment for the struct.  */
#define PCC_BITFIELD_TYPE_MATTERS 1

/*** Register Usage ***/

/* No register prefixes by default.  Will be overriden if necessary.  */
#undef REGISTER_PREFIX

/* Number of actual hardware registers.

   The hardware registers are assigned numbers for the compiler
   from 0 to just below FIRST_PSEUDO_REGISTER.
   All registers that the compiler knows about must be given numbers,
   even those that are not normally considered general registers.

   The m88100 has a General Register File (GRF) of 32 32-bit registers.
   The m88110 adds an Extended Register File (XRF) of 32 80-bit registers.

   There are also two fake registers:
   - ARG_POINTER_REGNUM abuses r0 (which is always zero and never used
     as a working register), and will always get eliminated in favour of
     HARD_FRAME_POINTER_REGNUM or STACK_POINTER_REGNUM.
   - FRAME_POINTER_REGNUM, which is equivalent to the above, and thus
     will also always get eliminated.  */
#define FIRST_EXTENDED_REGISTER 32
#define LAST_EXTENDED_REGISTER 63
#define FIRST_PSEUDO_REGISTER 64

/*  General notes on extended registers, their use and misuse.

    Possible good uses:

    spill area instead of memory.
      -waste if only used once

    floating point calculations
      -probably a waste unless we have run out of general purpose registers

    freeing up general purpose registers
      -e.g. may be able to have more loop invariants if floating
       point is moved into extended registers.


    I've noticed wasteful moves into and out of extended registers; e.g. a load
    into x21, then inside a loop a move into r24, then r24 used as input to
    an fadd.  Why not just load into r24 to begin with?  Maybe the new cse.c
    will address this.  This wastes a move, but the load,store and move could
    have been saved had extended registers been used throughout.
    E.g. in the code following code, if z and xz are placed in extended
    registers, there is no need to save preserve registers.

	long c=1,d=1,e=1,f=1,g=1,h=1,i=1,j=1,k;

	double z=0,xz=4.5;

	foo(a,b)
	long a,b;
	{
	  while (a < b)
	    {
	      k = b + c + d + e + f + g + h + a + i + j++;
	      z += xz;
	      a++;
	    }
	  printf("k= %d; z=%f;\n", k, z);
	}

    I've found that it is possible to change the constraints (putting * before
    the 'r' constraints int the fadd.ddd instruction) and get the entire
    addition and store to go into extended registers.  However, this also
    forces simple addition and return of floating point arguments to a
    function into extended registers.  Not the correct solution.

    Found the following note in local-alloc.c which may explain why I can't
    get both registers to be in extended registers since two are allocated in
    local-alloc and one in global-alloc.  Doesn't explain (I don't believe)
    why an extended register is used instead of just using the preserve
    register.

	from local-alloc.c:
	We have provision to exempt registers, even when they are contained
	within the block, that can be tied to others that are not contained in
	it.
	This is so that global_alloc could process them both and tie them then.
	But this is currently disabled since tying in global_alloc is not
	yet implemented.

    The explanation of why the preserved register is not used is as follows,
    I believe.  The registers are being allocated in order.  Tying is not
    done so efficiently, so when it comes time to do the first allocation,
    there are no registers left to use without spilling except extended
    registers.  Then when the next pseudo register needs a hard reg, there
    are still no registers to be had for free, but this one must be a GRF
    reg instead of an extended reg, so a preserve register is spilled.  Thus
    the move from extended to GRF is necessitated.  I do not believe this can
    be 'fixed' through the files in config/m88k.

    gcc seems to sometimes make worse use of register allocation -- not
    counting moves -- whenever extended registers are present.  For example in
    the whetstone, the simple for loop (slightly modified)
      for(i = 1; i <= n1; i++)
	{
	  x1 = (x1 + x2 + x3 - x4) * t;
	  x2 = (x1 + x2 - x3 + x4) * t;
	  x3 = (x1 - x2 + x3 + x4) * t;
	  x4 = (x1 + x2 + x3 + x4) * t;
	}
    in general loads the high bits of the addresses of x2-x4 and i into
    registers outside the loop.  Whenever extended registers are used, it loads
    all of these inside the loop. My conjecture is that since the 88110 has so
    many registers, and gcc makes no distinction at this point -- just that
    they are not fixed, that in loop.c it believes it can expect a number of
    registers to be available.  Then it allocates 'too many' in local-alloc
    which causes problems later.  'Too many' are allocated because a large
    portion of the registers are extended registers and cannot be used for
    certain purposes ( e.g. hold the address of a variable).  When this loop is
    compiled on its own, the problem does not occur.  I don't know the solution
    yet, though it is probably in the base sources.  Possibly a different way
    to calculate "threshold".  */

/* 1 for registers that have pervasive standard uses and are not available
   for the register allocator.  Registers r14-r25 and x22-x29 are expected
   to be preserved across function calls.

   On the 88000, the standard uses of the General Register File (GRF) are:
   Reg 0	= Pseudo argument pointer (hardware fixed to 0).
   Reg 1	= Subroutine return pointer (hardware).
   Reg 2-9	= Parameter registers (OCS).
   Reg 10	= OCS reserved temporary.
   Reg 11	= Static link if needed [OCS reserved temporary].
   Reg 12	= Address of structure return (OCS).
   Reg 13	= OCS reserved temporary.
   Reg 14-25	= Preserved register set.
   Reg 26-29	= Reserved by OCS and ABI.
   Reg 30	= Frame pointer (Common use).
   Reg 31	= Stack pointer.

   The following follows the current 88open UCS specification for the
   Extended Register File (XRF):
   Reg 32       = x0		Always equal to zero
   Reg 33-53	= x1-x21	Temporary registers (Caller Save)
   Reg 54-61	= x22-x29	Preserver registers (Callee Save)
   Reg 62-63	= x30-x31	Reserved for future ABI use.

   Note:  The current 88110 extended register mapping is subject to change.
	  The bias towards caller-save registers is based on the
	  presumption that memory traffic can potentially be reduced by
	  allowing the "caller" to save only that part of the register
	  which is actually being used.  (i.e. don't do a st.x if a st.d
	  is sufficient).  Also, in scientific code (a.k.a. Fortran), the
	  large number of variables defined in common blocks may require
	  that almost all registers be saved across calls anyway.  */

#define FIXED_REGISTERS							\
 {1, 0, 0, 0,  0, 0, 0, 0,   0, 0, 0, 0,  0, 0, 0, 0,			\
  0, 0, 0, 0,  0, 0, 0, 0,   0, 0, 1, 1,  1, 1, 1, 1,			\
  1, 0, 0, 0,  0, 0, 0, 0,   0, 0, 0, 0,  0, 0, 0, 0,			\
  0, 0, 0, 0,  0, 0, 0, 0,   0, 0, 0, 0,  0, 0, 1, 1}

/* 1 for registers not available across function calls.
   These must include the FIXED_REGISTERS and also any
   registers that can be used without being saved.
   The latter must include the registers where values are returned
   and the register where structure-value addresses are passed.
   Aside from that, you can include as many other registers as you like.  */

#define CALL_USED_REGISTERS						\
 {1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 0, 0,			\
  0, 0, 0, 0,  0, 0, 0, 0,   0, 0, 1, 1,  1, 1, 1, 1,			\
  1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1,			\
  1, 1, 1, 1,  1, 1, 0, 0,   0, 0, 0, 0,  0, 0, 1, 1}

/* Macro to conditionally modify fixed_regs/call_used_regs.  */
#define CONDITIONAL_REGISTER_USAGE					\
  {									\
    if (! TARGET_88110)							\
      {									\
	int i;								\
	  for (i = FIRST_EXTENDED_REGISTER; i <= LAST_EXTENDED_REGISTER;\
	       i++)							\
	    {								\
	      fixed_regs[i] = 1;					\
	      call_used_regs[i] = 1;					\
	    }								\
      }									\
    if (flag_pic)							\
      {									\
	fixed_regs[PIC_OFFSET_TABLE_REGNUM] = 1;			\
	call_used_regs[PIC_OFFSET_TABLE_REGNUM] = 1;			\
      }									\
  }

/* True if register is an extended register.  */
#define XRF_REGNO_P(N)							\
  ((N) <= LAST_EXTENDED_REGISTER && (N) >= FIRST_EXTENDED_REGISTER)
 
/* Return number of consecutive hard regs needed starting at reg REGNO
   to hold something of mode MODE.
   This is ordinarily the length in words of a value of mode MODE
   but can be less for certain modes in special long registers.

   On the m88000, GRF registers hold 32-bits and XRF registers hold 80-bits.
   An XRF register can hold any mode, but two GRF registers are required
   for larger modes.  */
#define HARD_REGNO_NREGS(REGNO, MODE)					\
  (XRF_REGNO_P (REGNO)							\
   ? 1 : ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD))

/* Value is 1 if hard register REGNO can hold a value of machine-mode MODE.

   For double integers, we never put the value into an odd register so that
   the operators don't run into the situation where the high part of one of
   the inputs is the low part of the result register.  (It's ok if the output
   registers are the same as the input registers.)  The XRF registers can
   hold all modes, but only DF and SF modes can be manipulated in these
   registers.  The compiler should be allowed to use these as a fast spill
   area.  */
#define HARD_REGNO_MODE_OK(REGNO, MODE)					\
  (XRF_REGNO_P (REGNO)							\
    ? (TARGET_88110 && GET_MODE_CLASS (MODE) == MODE_FLOAT)		\
    : (((MODE) != DImode && (MODE) != DFmode && (MODE) != DCmode)	\
       || ((REGNO) & 1) == 0))

/* Value is 1 if it is a good idea to tie two pseudo registers
   when one has mode MODE1 and one has mode MODE2.
   If HARD_REGNO_MODE_OK could produce different values for MODE1 and MODE2,
   for any hard reg, then this must be 0 for correct output.  */
#define MODES_TIEABLE_P(MODE1, MODE2)					\
  (((MODE1) == DFmode || (MODE1) == DCmode || (MODE1) == DImode		\
    || (TARGET_88110 && GET_MODE_CLASS (MODE1) == MODE_FLOAT))		\
   == ((MODE2) == DFmode || (MODE2) == DCmode || (MODE2) == DImode	\
       || (TARGET_88110 && GET_MODE_CLASS (MODE2) == MODE_FLOAT)))

/* Specify the registers used for certain standard purposes.
   The values of these macros are register numbers.  */

/* the m88000 pc isn't overloaded on a register that the compiler knows about.  */
/* #define PC_REGNUM  */

/* Register to use for pushing function arguments.  */
#define STACK_POINTER_REGNUM 31

/* Base register for access to local variables of the function.  */
#define FRAME_POINTER_REGNUM 0
#define HARD_FRAME_POINTER_REGNUM 30

/* Base register for access to arguments of the function.  */
#define ARG_POINTER_REGNUM 0

/* Register used in cases where a temporary is known to be safe to use.  */
#define TEMP_REGNUM 10

/* Register in which static-chain is passed to a function.  */
#define STATIC_CHAIN_REGNUM 11

/* Register in which address to store a structure value
   is passed to a function.  */
#define M88K_STRUCT_VALUE_REGNUM 12

/* Register to hold the addressing base for position independent
   code access to data items.  */
#define PIC_OFFSET_TABLE_REGNUM (flag_pic ? 25 : INVALID_REGNUM)

/* Order in which registers are preferred (most to least).  Use temp
   registers, then param registers top down.  Preserve registers are
   top down to maximize use of double memory ops for register save.
   The 88open reserved registers (r26-r29 and x30-x31) may commonly be used
   in most environments with the -fcall-used- or -fcall-saved- options.  */
#define REG_ALLOC_ORDER							\
 {									\
  13, 12, 11, 10, 29, 28, 27, 26,					\
  62, 63,  9,  8,  7,  6,  5,  4,					\
   3,  2,  1, 53, 52, 51, 50, 49,					\
  48, 47, 46, 45, 44, 43, 42, 41,					\
  40, 39, 38, 37, 36, 35, 34, 33,					\
  25, 24, 23, 22, 21, 20, 19, 18,					\
  17, 16, 15, 14, 61, 60, 59, 58,					\
  57, 56, 55, 54, 30, 31,  0, 32}

/* Order for leaf functions.  */
#define REG_LEAF_ALLOC_ORDER						\
 {									\
   9,  8,  7,  6, 13, 12, 11, 10,					\
  29, 28, 27, 26, 62, 63,  5,  4,					\
   3,  2,  0, 53, 52, 51, 50, 49,					\
  48, 47, 46, 45, 44, 43, 42, 41,					\
  40, 39, 38, 37, 36, 35, 34, 33,					\
  25, 24, 23, 22, 21, 20, 19, 18,					\
  17, 16, 15, 14, 61, 60, 59, 58,					\
  57, 56, 55, 54, 30, 31,  1, 32}

/* Switch between the leaf and non-leaf orderings.  The purpose is to avoid
   write-over scoreboard delays between caller and callee.  */
#define ORDER_REGS_FOR_LOCAL_ALLOC m88k_order_regs_for_local_alloc ()

/*** Register Classes ***/

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

/* The m88000 hardware has two kinds of registers.  In addition, we denote
   the arg pointer as a separate class.  */

enum reg_class { NO_REGS, AP_REG, XRF_REGS, GENERAL_REGS, AGRF_REGS,
		 XGRF_REGS, ALL_REGS, LIM_REG_CLASSES };

#define N_REG_CLASSES (int) LIM_REG_CLASSES

/* Give names of register classes as strings for dump file.   */
#define REG_CLASS_NAMES							\
  { "NO_REGS", "AP_REG", "XRF_REGS", "GENERAL_REGS", "AGRF_REGS",	\
    "XGRF_REGS", "ALL_REGS" }

/* Define which registers fit in which classes.
   This is an initializer for a vector of HARD_REG_SET
   of length N_REG_CLASSES.  */
#define REG_CLASS_CONTENTS						\
  { { 0x00000000, 0x00000000 },						\
    { 0x00000001, 0x00000000 },						\
    { 0x00000000, 0xffffffff },						\
    { 0xfffffffe, 0x00000000 },						\
    { 0xffffffff, 0x00000000 },						\
    { 0xfffffffe, 0xffffffff },						\
    { 0xffffffff, 0xffffffff } }

/* The same information, inverted:
   Return the class number of the smallest class containing
   reg number REGNO.  This could be a conditional expression
   or could index an array.  */
extern const enum reg_class m88k_regno_reg_class[FIRST_PSEUDO_REGISTER];

#define REGNO_REG_CLASS(REGNO)	m88k_regno_reg_class[(REGNO)]

/* The class value for index registers, and the one for base regs.  */
#define BASE_REG_CLASS AGRF_REGS
#define INDEX_REG_CLASS GENERAL_REGS

/* Macros to check register numbers against specific register classes.
   These assume that REGNO is a hard or pseudo reg number.
   They give nonzero only if REGNO is a hard reg of the suitable class
   or a pseudo reg currently allocated to a suitable hard reg.
   Since they use reg_renumber, they are safe only once reg_renumber
   has been allocated, which happens in local-alloc.c.  */
#define REGNO_OK_FOR_BASE_P(REGNO)					\
  m88k_regno_ok_for_base_p (REGNO)
#define REGNO_OK_FOR_INDEX_P(REGNO)					\
  m88k_regno_ok_for_index_p (REGNO)

/* Given an rtx X being reloaded into a reg required to be
   in class CLASS, return the class of reg to actually use.
   In general this is just CLASS; but on some machines
   in some cases it is preferable to use a more restrictive class.
   Double constants should be in a register iff they can be made cheaply.  */
#define PREFERRED_RELOAD_CLASS(X,CLASS)					\
   (CONSTANT_P (X) && ((CLASS) == XRF_REGS) ? NO_REGS : (CLASS))

/* Return the register class of a scratch register needed to load IN
   into a register of class CLASS in MODE.  On the m88k, when PIC, we
   need a temporary when loading some addresses into a register.  */
#define SECONDARY_INPUT_RELOAD_CLASS(CLASS, MODE, IN)			\
  ((flag_pic && pic_address_needs_scratch (IN)) ? GENERAL_REGS : NO_REGS)

/* Return the maximum number of consecutive registers
   needed to represent mode MODE in a register of class CLASS.  */
#define CLASS_MAX_NREGS(CLASS, MODE)					\
  ((((CLASS) == XRF_REGS) ? 1						\
    : ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)))

/* Quick tests for certain values.  */
#define SMALL_INT(X) (SMALL_INTVAL (INTVAL (X)))
#define SMALL_INTVAL(I) ((unsigned HOST_WIDE_INT) (I) < 0x10000)
#define ADD_INT(X) (ADD_INTVAL (INTVAL (X)))
#define ADD_INTVAL(I) ((unsigned HOST_WIDE_INT) (I) + 0xffff < 0x1ffff)
#define POWER_OF_2(I) ((I) && POWER_OF_2_or_0(I))
#define POWER_OF_2_or_0(I) (((I) & ((unsigned HOST_WIDE_INT)(I) - 1)) == 0)

/*** Describing Stack Layout ***/

/* Define this if pushing a word on the stack moves the stack pointer
   to a smaller address.  */
#define STACK_GROWS_DOWNWARD

/* Define this if the addresses of local variable slots are at negative
   offsets from the (logical) frame pointer.  */
#define FRAME_GROWS_DOWNWARD 1

/* Offset from the frame pointer to the first local variable slot to be
   allocated. For the m88k, the debugger wants the return address (r1)
   stored at location r30+4, and the previous frame pointer stored at
   location r30.  But since we use a logical frame pointer, these
   details are hidden. */
#define STARTING_FRAME_OFFSET 0

/* If we generate an insn to push BYTES bytes, this says how many the
   stack pointer really advances by.  The m88k has no push instruction.  */
/*  #define PUSH_ROUNDING(BYTES) */

/* If defined, the maximum amount of space required for outgoing arguments
   will be computed and placed into the variable
   `current_function_outgoing_args_size'.  No space will be pushed
   onto the stack for each call; instead, the function prologue should
   increase the stack frame size by this amount.  */
#define ACCUMULATE_OUTGOING_ARGS 1

/* Offset from the stack pointer register to the first location at which
   outgoing arguments are placed.  Use the default value zero.  */
/* #define STACK_POINTER_OFFSET 0 */

/* Offset of first parameter from the argument pointer register value.
   Using an argument pointer, this is 0 for the m88k.  GCC knows
   how to eliminate the argument pointer references if necessary.  */
#define FIRST_PARM_OFFSET(FNDECL) 0

/* Define this if functions should assume that stack space has been
   allocated for arguments even when their values are passed in
   registers.

   The value of this macro is the size, in bytes, of the area reserved for
   arguments passed in registers.

   This space can either be allocated by the caller or be a part of the
   machine-dependent stack frame: `OUTGOING_REG_PARM_STACK_SPACE'
   says which.  */
/* #undef REG_PARM_STACK_SPACE(FNDECL) */

/* Define this macro if REG_PARM_STACK_SPACE is defined but stack
   parameters don't skip the area specified by REG_PARM_STACK_SPACE.
   Normally, when a parameter is not passed in registers, it is placed on
   the stack beyond the REG_PARM_STACK_SPACE area.  Defining this macro
   suppresses this behavior and causes the parameter to be passed on the
   stack in its natural location.  */
/* #undef STACK_PARMS_IN_REG_PARM_AREA */

/* Define this if it is the responsibility of the caller to allocate the
   area reserved for arguments passed in registers.  If
   `ACCUMULATE_OUTGOING_ARGS' is also defined, the only effect of this
   macro is to determine whether the space is included in
   `current_function_outgoing_args_size'.  */
/* #define OUTGOING_REG_PARM_STACK_SPACE */

/* Offset from the stack pointer register to an item dynamically allocated
   on the stack, e.g., by `alloca'.

   The default value for this macro is `STACK_POINTER_OFFSET' plus the
   length of the outgoing arguments.  The default is correct for most
   machines.  See `function.c' for details.  */
/* #define STACK_DYNAMIC_OFFSET(FUNDECL) ... */

/* Value is the number of bytes of arguments automatically
   popped when returning from a subroutine call.
   FUNDECL is the declaration node of the function (as a tree),
   FUNTYPE is the data type of the function (as a tree),
   or for a library call it is an identifier node for the subroutine name.
   SIZE is the number of bytes of arguments passed on the stack.  */
#define RETURN_POPS_ARGS(FUNDECL,FUNTYPE,SIZE) 0

/* Define how to find the value returned by a function.
   VALTYPE is the data type of the value (as a tree).
   If the precise function being called is known, FUNC is its FUNCTION_DECL;
   otherwise, FUNC is 0.  */
#define FUNCTION_VALUE(VALTYPE, FUNC)					\
  gen_rtx_REG (TYPE_MODE (VALTYPE)					\
	       == BLKmode ? SImode : TYPE_MODE (VALTYPE), 2)

/* Define this if it differs from FUNCTION_VALUE.  */
/* #define FUNCTION_OUTGOING_VALUE(VALTYPE, FUNC) ... */

/* Don't default to pcc-struct-return, because we have already specified
   exactly how to return structures in the RETURN_IN_MEMORY macro.  */
#define DEFAULT_PCC_STRUCT_RETURN 0

/* Define how to find the value returned by a library function
   assuming the value has mode MODE.  */
#define LIBCALL_VALUE(MODE)  gen_rtx_REG (MODE, 2)

/* True if N is a possible register number for a function value
   as seen by the caller.  */
#define FUNCTION_VALUE_REGNO_P(N) ((N) == 2)

/* Determine whether a function argument is passed in a register, and
   which register.  See m88k.c.  */
#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED)				\
  m88k_function_arg (CUM, MODE, TYPE, NAMED)

/* Define this if it differs from FUNCTION_ARG.  */
/* #define FUNCTION_INCOMING_ARG(CUM, MODE, TYPE, NAMED) ... */

/* A C type for declaring a variable that is used as the first argument
   of `FUNCTION_ARG' and other related values.  It suffices to count
   the number of words of argument so far.  */
#define CUMULATIVE_ARGS int

/* Initialize a variable CUM of type CUMULATIVE_ARGS for a call to a
   function whose data type is FNTYPE.  For a library call, FNTYPE is 0. */
#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, INDIRECT, N_NAMED_ARGS) \
 ((CUM) = 0)

/* Update the summarizer variable to advance past an argument in an
   argument list.  See m88k.c.  */
#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED)			\
  m88k_function_arg_advance (& (CUM), MODE, TYPE, NAMED)

/* True if N is a possible register number for function argument passing.
   On the m88000, these are registers 2 through 9.  */
#define FUNCTION_ARG_REGNO_P(N) ((N) <= 9 && (N) >= 2)

/* A C expression which determines whether, and in which direction,
   to pad out an argument with extra space.  The value should be of
   type `enum direction': either `upward' to pad above the argument,
   `downward' to pad below, or `none' to inhibit padding.

   This macro does not control the *amount* of padding; that is always
   just enough to reach the next multiple of `FUNCTION_ARG_BOUNDARY'.  */
#define FUNCTION_ARG_PADDING(MODE, TYPE)				\
  ((MODE) == BLKmode							\
   || ((TYPE) && (TREE_CODE (TYPE) == RECORD_TYPE			\
		  || TREE_CODE (TYPE) == UNION_TYPE))			\
   ? upward : GET_MODE_BITSIZE (MODE) < PARM_BOUNDARY ? downward : none)

/* If defined, a C expression that gives the alignment boundary, in bits,
   of an argument with the specified mode and type.  If it is not defined,
   `PARM_BOUNDARY' is used for all arguments.  */
#define FUNCTION_ARG_BOUNDARY(MODE, TYPE)				\
  (((TYPE) ? TYPE_ALIGN (TYPE) : GET_MODE_BITSIZE (MODE))		\
    <= PARM_BOUNDARY ? PARM_BOUNDARY : 2 * PARM_BOUNDARY)

/* Implement `va_start' for varargs and stdarg.  */
#define EXPAND_BUILTIN_VA_START(valist, nextarg)			\
  m88k_va_start (valist, nextarg)

/* Output assembler code to FILE to increment profiler label # LABELNO
   for profiling a function entry.  */
#define FUNCTION_PROFILER(FILE, LABELNO)				\
  output_function_profiler (FILE, LABELNO, "mcount")

/* Maximum length in instructions of the code output by FUNCTION_PROFILER.  */
#define FUNCTION_PROFILER_LENGTH (4*(5+3+1+5))

/* EXIT_IGNORE_STACK should be nonzero if, when returning from a function,
   the stack pointer does not matter.  The value is tested only in
   functions that have frame pointers.
   No definition is equivalent to always zero.  */
#define EXIT_IGNORE_STACK (1)

/* Value should be nonzero if functions must have frame pointers.
   Zero means the frame pointer need not be set up (and parms
   may be accessed via the stack pointer) in functions that seem suitable.
   This is computed in `reload', in reload1.c.  */
#define FRAME_POINTER_REQUIRED						\
(current_function_profile || !leaf_function_p ())

/* Define registers used by the epilogue and return instruction.  */
#define EPILOGUE_USES(REGNO)						\
(reload_completed && ((REGNO) == 1					\
		      || (current_function_profile			\
			  && (REGNO) == HARD_FRAME_POINTER_REGNUM)))

/* Before the prologue, RA is in r1.  */
#define INCOMING_RETURN_ADDR_RTX gen_rtx_REG (Pmode, 1)
#define DWARF_FRAME_RETURN_COLUMN DWARF_FRAME_REGNUM (1)

/* Where to find the lowest frame.  */
#define INITIAL_FRAME_ADDRESS_RTX \
  (current_function_accesses_prior_frames = 1, hard_frame_pointer_rtx)

/* Where to find the return address in the given frame.  */
#define RETURN_ADDR_RTX(COUNT, FRAME) \
  gen_rtx_MEM (Pmode, plus_constant (FRAME, 4))

/* Definitions for register eliminations.

   We have two registers that can be eliminated on the m88k.  First, the
   frame pointer register can often be eliminated in favor of the stack
   pointer register.  Secondly, the argument pointer register can always be
   eliminated; it is replaced with either the stack or frame pointer.  */

/* This is an array of structures.  Each structure initializes one pair
   of eliminable registers.  The "from" register number is given first,
   followed by "to".  Eliminations of the same "from" register are listed
   in order of preference.  */
#define ELIMINABLE_REGS							\
{{ FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM},				\
 { FRAME_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM}}

/* Given FROM and TO register numbers, say whether this elimination
   is allowed.  */
#define CAN_ELIMINATE(FROM, TO)						\
  ((TO) == HARD_FRAME_POINTER_REGNUM					\
   || (/*(TO) == STACK_POINTER_REGNUM &&*/ !frame_pointer_needed))

/* Define the offset between two registers, one to be eliminated, and the other
   its replacement, at the start of a routine.  */
#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET)			\
  ((OFFSET) = m88k_initial_elimination_offset(FROM, TO))

/*** Trampolines for Nested Functions ***/

#ifndef FINALIZE_TRAMPOLINE
#define FINALIZE_TRAMPOLINE(TRAMP)
#endif

/* Output assembler code for a block containing the constant parts
   of a trampoline, leaving space for the variable parts.

   This block is placed on the stack and filled in.  It is aligned
   0 mod 128 and those portions that are executed are constant.
   This should work for instruction caches that have cache lines up
   to the aligned amount (128 is arbitrary), provided no other code
   producer is attempting to play the same game.  This of course is
   in violation of any number of 88open standards.  */

#define TRAMPOLINE_TEMPLATE(FILE)					\
{									\
  char buf[256];							\
  static int labelno = 0;						\
  labelno++;								\
  ASM_GENERATE_INTERNAL_LABEL (buf, "LTRMP", labelno);			\
  /* Save the return address (r1) in the static chain reg (r11).  */	\
  asm_fprintf (FILE, "\tor\t %R%s,%R%s,0\n",				\
	       reg_names[11], reg_names[1]);				\
  /* Locate this block; transfer to the next instruction.  */		\
  fprintf (FILE, "\tbsr\t %s\n", &buf[1]);				\
  assemble_name (FILE, buf);						\
  fputs (":", FILE);							\
  /* Save r10; use it as the relative pointer; restore r1.  */		\
  asm_fprintf (FILE, "\tst\t %R%s,%R%s,24\n",				\
	       reg_names[10], reg_names[1]);				\
  asm_fprintf (FILE, "\tor\t %R%s,%R%s,0\n",				\
	       reg_names[10], reg_names[1]);				\
  asm_fprintf (FILE, "\tor\t %R%s,%R%s,0\n",				\
	       reg_names[1], reg_names[11]);				\
  /* Load the function's address and go there.  */			\
  asm_fprintf (FILE, "\tld\t %R%s,%R%s,32\n",				\
	       reg_names[11], reg_names[10]);				\
  asm_fprintf (FILE, "\tjmp.n\t %R%s\n", reg_names[11]);		\
  /* Restore r10 and load the static chain register.  */		\
  asm_fprintf (FILE, "\tld.d\t %R%s,%R%s,24\n",				\
	       reg_names[10], reg_names[10]);				\
  /* Storage: r10 save area, static chain, function address.  */	\
  assemble_aligned_integer (UNITS_PER_WORD, const0_rtx);		\
  assemble_aligned_integer (UNITS_PER_WORD, const0_rtx);		\
  assemble_aligned_integer (UNITS_PER_WORD, const0_rtx);		\
}

/* Length in units of the trampoline for entering a nested function.
   This is really two components.  The first 32 bytes are fixed and
   must be copied; the last 12 bytes are just storage that's filled
   in later.  So for allocation purposes, it's 32+12 bytes, but for
   initialization purposes, it's 32 bytes.  */

#define TRAMPOLINE_SIZE (32+12)

/* Alignment required for a trampoline.  128 is used to find the
   beginning of a line in the instruction cache and to allow for
   instruction cache lines of up to 128 bytes.  */

#define TRAMPOLINE_ALIGNMENT 128

/* Emit RTL insns to initialize the variable parts of a trampoline.
   FNADDR is an RTX for the address of the function's pure code.
   CXT is an RTX for the static chain value for the function.  */

#define INITIALIZE_TRAMPOLINE(TRAMP, FNADDR, CXT)			\
{									\
  emit_move_insn (gen_rtx_MEM (SImode, plus_constant (TRAMP, 40)),	\
		  FNADDR);						\
  emit_move_insn (gen_rtx_MEM (SImode, plus_constant (TRAMP, 36)),	\
		  CXT);							\
  FINALIZE_TRAMPOLINE (TRAMP);						\
}

/*** Addressing Modes ***/

#define SELECT_CC_MODE(OP,X,Y) CCmode

/* #define HAVE_POST_INCREMENT 0 */
/* #define HAVE_POST_DECREMENT 0 */

/* #define HAVE_PRE_DECREMENT 0 */
/* #define HAVE_PRE_INCREMENT 0 */

/* Recognize any constant value that is a valid address.
   When PIC, we do not accept an address that would require a scratch reg
   to load into a register.  */

#define CONSTANT_ADDRESS_P(X)						\
  (GET_CODE (X) == LABEL_REF || GET_CODE (X) == SYMBOL_REF		\
   || CONST_INT_P (X) || GET_CODE (X) == HIGH				\
   || (GET_CODE (X) == CONST						\
       && ! (flag_pic && pic_address_needs_scratch (X))))


/* Maximum number of registers that can appear in a valid memory address.  */
#define MAX_REGS_PER_ADDRESS 2

/* The condition for memory shift insns.  */
#define SCALED_ADDRESS_P(ADDR)						\
  (GET_CODE (ADDR) == PLUS						\
   && (GET_CODE (XEXP (ADDR, 0)) == MULT				\
       || GET_CODE (XEXP (ADDR, 1)) == MULT))

/* GO_IF_LEGITIMATE_ADDRESS recognizes an RTL expression
   that is a valid memory address for an instruction.
   The MODE argument is the machine mode for the MEM expression
   that wants to use this address.

   On the m88000, a legitimate address has the form REG, REG+REG,
   REG+SMALLINT, REG+(REG*modesize) (REG[REG]), or SMALLINT.

   The register elimination process should deal with the argument
   pointer and frame pointer changing to REG+SMALLINT.  */

#ifdef REG_OK_STRICT
#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR)				\
{									\
  if (m88k_legitimate_address_p (MODE, X, 1))				\
    goto ADDR;								\
}
#else
#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR)				\
{									\
  if (m88k_legitimate_address_p (MODE, X, 0))				\
    goto ADDR;								\
}
#endif

/* Try machine-dependent ways of modifying an illegitimate address
   to be legitimate.  If we find one, return the new, valid address.
   This macro is used in only one place: `memory_address' in explow.c.

   OLDX is the address as it was before break_out_memory_refs was called.
   In some cases it is useful to look at this to decide what needs to be done.

   MODE and WIN are passed so that this macro can use
   GO_IF_LEGITIMATE_ADDRESS.

   It is always safe for this macro to do nothing.  It exists to recognize
   opportunities to optimize the output.  */

#define LEGITIMIZE_ADDRESS(X,OLDX,MODE,WIN)				\
{									\
  (X) = m88k_legitimize_address (X, MODE);				\
  if (memory_address_p (MODE, X))					\
    goto WIN;								\
}

/* Go to LABEL if ADDR (a legitimate address expression)
   has an effect that depends on the machine mode it is used for.
   On the m88000 this is never true.  */

#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR,LABEL)

/* Nonzero if the constant value X is a legitimate general operand.
   It is given that X satisfies CONSTANT_P or is a CONST_DOUBLE.  */
#define LEGITIMATE_CONSTANT_P(X) (1)

/* Define this, so that when PIC, reload won't try to reload invalid
   addresses which require two reload registers.  */
#define LEGITIMATE_PIC_OPERAND_P(X)  (! pic_address_needs_scratch (X))


/*** Condition Code Information ***/

/* When using a register to hold the condition codes, the cc_status
   mechanism cannot be used.  */
#define NOTICE_UPDATE_CC(EXP, INSN) (0)

/*** Miscellaneous Parameters ***/

/* The case table contains either words or branch instructions.  This says
   which.  We always claim that the vector is PC-relative.  It is position
   independent when -fpic is used.  */
#define CASE_VECTOR_INSNS (TARGET_88100 || flag_pic)

/* An alias for a machine mode name.  This is the machine mode that
   elements of a jump-table should have.  */
#define CASE_VECTOR_MODE SImode

/* Define as C expression which evaluates to nonzero if the tablejump
   instruction expects the table to contain offsets from the address of the
   table.
   Do not define this if the table should contain absolute addresses. */
#define CASE_VECTOR_PC_RELATIVE 1

/* Define this if control falls through a `case' insn when the index
   value is out of range.  This means the specified default-label is
   actually ignored by the `case' insn proper.  */
/* #define CASE_DROPS_THROUGH */

/* Define this to be the smallest number of different values for which it
   is best to use a jump-table instead of a tree of conditional branches.
   The default is 4 for machines with a casesi instruction and 5 otherwise.
   The best 88110 number is around 7, though the exact number isn't yet
   known.  A third alternative for the 88110 is to use a binary tree of
   bb1 instructions on bits 2/1/0 if the range is dense.  This may not
   win very much though.  */
#define CASE_VALUES_THRESHOLD (TARGET_88100 ? 4 : 7)

/* Define this as 1 if `char' should by default be signed; else as 0.  */
#define DEFAULT_SIGNED_CHAR 1

/* The 88open ABI says size_t is unsigned int.  */
#define SIZE_TYPE "unsigned int"

/* Handle #pragma pack and sometimes #pragma weak.  */
#define HANDLE_SYSV_PRAGMA 1

/* Max number of bytes we can move from memory to memory
   in one reasonably fast instruction.  */
#define MOVE_MAX 8

/* Zero if access to memory by bytes is faster.  */
#define SLOW_BYTE_ACCESS 1

/* Define if operations between registers always perform the operation
   on the full register even if a narrower mode is specified.  */
#define WORD_REGISTER_OPERATIONS

/* Define if loading in MODE, an integral mode narrower than BITS_PER_WORD
   will either zero-extend or sign-extend.  The value of this macro should
   be the code that says which one of the two operations is implicitly
   done, UNKNOWN if none.  */
/* #define LOAD_EXTEND_OP(MODE) UNKNOWN */

/* Define if loading short immediate values into registers sign extends.  */
/* #define SHORT_IMMEDIATES_SIGN_EXTEND */

/* Value is 1 if truncating an integer of INPREC bits to OUTPREC bits
   is done just by pretending it is already truncated.  */
#define TRULY_NOOP_TRUNCATION(OUTPREC, INPREC) 1

/* Define this if addresses of constant functions
   shouldn't be put through pseudo regs where they can be cse'd.
   Desirable on machines where ordinary constants are expensive
   but a CALL with constant address is cheap.  */
#define NO_FUNCTION_CSE

/* We assume that the store-condition-codes instructions store 0 for false
   and some other value for true.  This is the value stored for true.  */
#define STORE_FLAG_VALUE (-1)

/* Specify the machine mode that pointers have.
   After generation of rtl, the compiler makes no further distinction
   between pointers and any other objects of this machine mode.  */
#define Pmode SImode

/* A function address in a call instruction
   is a word address (for indexing purposes)
   so give the MEM rtx word mode.  */
#define FUNCTION_MODE SImode

/* A barrier will be aligned so account for the possible expansion.
   A volatile load may be preceded by a serializing instruction.
   Account for profiling code output at NOTE_INSN_PROLOGUE_END.
   Account for block profiling code at basic block boundaries.  */
#define ADJUST_INSN_LENGTH(RTX, LENGTH)					\
  if (BARRIER_P (RTX)							\
      || (TARGET_SERIALIZE_VOLATILE					\
	  && NONJUMP_INSN_P (RTX)					\
	  && GET_CODE (PATTERN (RTX)) == SET				\
	  && ((MEM_P (SET_SRC (PATTERN (RTX)))				\
	       && MEM_VOLATILE_P (SET_SRC (PATTERN (RTX)))))))		\
    (LENGTH) += 4;							\
  else if (NOTE_P (RTX)							\
	   && NOTE_LINE_NUMBER (RTX) == NOTE_INSN_PROLOGUE_END)		\
    {									\
      if (current_function_profile)					\
	(LENGTH) += FUNCTION_PROFILER_LENGTH;				\
    }									\

/* Track the state of the last volatile memory reference.  Clear the
   state with CC_STATUS_INIT for now.  */
#define CC_STATUS_INIT							\
  do {									\
    m88k_volatile_code = '\0';						\
  } while (0)

/* A C expressions returning the cost of moving data of MODE from a register
   to or from memory.  This is more costly than between registers.  */
#define MEMORY_MOVE_COST(MODE,CLASS,IN) 4

/* Provide the cost of a branch.  Exact meaning under development.  */
#define BRANCH_COST (TARGET_88100 ? 1 : 2)

/* Do not break .stabs pseudos into continuations.  */
#define DBX_CONTIN_LENGTH 0

/*** Output of Assembler Code ***/

/* Control the assembler format that we output.  */

/* A C string constant describing how to begin a comment in the target
   assembler language.  The compiler assumes that the comment will end at
   the end of the line.  */
#define ASM_COMMENT_START ";"

#define ASM_OUTPUT_SOURCE_FILENAME(FILE, NAME)				\
  do {									\
    fputs (FILE_ASM_OP, FILE);						\
    output_quoted_string (FILE, NAME);					\
    putc ('\n', FILE);							\
  } while (0)

/* Output to assembler file text saying following lines
   may contain character constants, extra white space, comments, etc.  */
#define ASM_APP_ON ""

/* Output to assembler file text saying following lines
   no longer contain unusual constructs.  */
#define ASM_APP_OFF ""

/* Format the assembly opcode so that the arguments are all aligned.
   The maximum instruction size is 8 characters (fxxx.xxx), so a tab and a
   space will do to align the output.  Abandon the output if a `%' is
   encountered.  */
#define ASM_OUTPUT_OPCODE(STREAM, PTR)					\
  {									\
    int ch;								\
    const char *orig_ptr;						\
									\
    for (orig_ptr = (PTR);						\
	 (ch = *(PTR)) && ch != ' ' && ch != '\t' && ch != '\n' && ch != '%'; \
	 (PTR)++)							\
      putc (ch, STREAM);						\
									\
    if (ch == ' ' && orig_ptr != (PTR) && (PTR) - orig_ptr < 8)		\
      putc ('\t', STREAM);						\
  }

/* How to refer to registers in assembler output.
   This sequence is indexed by compiler's hard-register-number.  */

#define REGISTER_NAMES							\
  { "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",		\
    "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",		\
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",		\
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",		\
    "x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",		\
    "x8",  "x9",  "x10", "x11", "x12", "x13", "x14", "x15",		\
    "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",		\
    "x24", "x25", "x26", "x27", "x28", "x29", "x30", "x31"}

/* Define additional names for use in asm clobbers and asm declarations.

   We define the fake Condition Code register as an alias for reg 0 (which
   is our `condition code' register), so that condition codes can easily
   be clobbered by an asm.  The carry bit in the PSR is now used.  */

#define ADDITIONAL_REGISTER_NAMES	{{"psr", 0}, {"cc", 0}}

/* Epilogue for case labels.  This jump instruction is called by casesi
   to transfer to the appropriate branch instruction within the table.
   The label `@L<n>e' is coined to mark the end of the table.  */
#define ASM_OUTPUT_CASE_END(FILE, NUM, TABLE)				\
  do {									\
    if (CASE_VECTOR_INSNS)						\
      {									\
	char label[256];						\
	ASM_GENERATE_INTERNAL_LABEL (label, "L", NUM);			\
	fprintf (FILE, "%se:\n", &label[1]);				\
	if (! flag_delayed_branch)					\
	  asm_fprintf (FILE, "\tlda\t %R%s,%R%s[%R%s]\n", reg_names[1],	\
		       reg_names[1], reg_names[m88k_case_index]);	\
	asm_fprintf (FILE, "\tjmp\t %R%s\n", reg_names[1]);		\
      }									\
  } while (0)

/* This is how to output an element of a case-vector that is absolute.  */
#define ASM_OUTPUT_ADDR_VEC_ELT(FILE, VALUE)				\
  do {									\
    char buffer[256];							\
    ASM_GENERATE_INTERNAL_LABEL (buffer, "L", VALUE);			\
    fprintf (FILE, CASE_VECTOR_INSNS ? "\tbr\t %s\n" : "\tword\t %s\n",	\
	     &buffer[1]);						\
  } while (0)

/* This is how to output an element of a case-vector that is relative.  */
#define ASM_OUTPUT_ADDR_DIFF_ELT(FILE, BODY, VALUE, REL)		\
  ASM_OUTPUT_ADDR_VEC_ELT (FILE, VALUE)

/* This is how to output an assembler line
   that says to advance the location counter
   to a multiple of 2**LOG bytes.  */
#define ASM_OUTPUT_ALIGN(FILE,LOG)					\
  if ((LOG) != 0)							\
    fprintf (FILE, "%s%d\n", ALIGN_ASM_OP, 1<<(LOG))

/* This is how to output an insn to push a register on the stack.
   It need not be very fast code.  */
#define ASM_OUTPUT_REG_PUSH(FILE,REGNO)					\
  asm_fprintf (FILE, "\tsubu\t %R%s,%R%s,%d\n\tst\t %R%s,%R%s,0\n",	\
	       reg_names[STACK_POINTER_REGNUM],				\
	       reg_names[STACK_POINTER_REGNUM],				\
	       (STACK_BOUNDARY / BITS_PER_UNIT),			\
	       reg_names[REGNO],					\
	       reg_names[STACK_POINTER_REGNUM])

/* This is how to output an insn to pop a register from the stack.  */
#define ASM_OUTPUT_REG_POP(FILE,REGNO)					\
  asm_fprintf (FILE, "\tld\t %R%s,%R%s,0\n\taddu\t %R%s,%R%s,%d\n",	\
	       reg_names[REGNO],					\
	       reg_names[STACK_POINTER_REGNUM],				\
	       reg_names[STACK_POINTER_REGNUM],				\
	       reg_names[STACK_POINTER_REGNUM],				\
	       (STACK_BOUNDARY / BITS_PER_UNIT))

/* Jump tables consist of branch instructions and should be output in
   the text section.  When we use a table of addresses, we explicitly
   change to the readonly data section.  */
#define JUMP_TABLES_IN_TEXT_SECTION 1

/* Print operand X (an rtx) in assembler syntax to file FILE.
   CODE is a letter or dot (`z' in `%z0') or 0 if no letter was specified.
   For `%' followed by punctuation, CODE is the punctuation and X is null.  */
#define PRINT_OPERAND_PUNCT_VALID_P(c)					\
  ((c) == '#' || (c) == '.' || (c) == '!' || (c) == '*' || (c) == ';')

#define PRINT_OPERAND(FILE, X, CODE) print_operand (FILE, X, CODE)

/* Print a memory address as an operand to reference that memory location.  */
#define PRINT_OPERAND_ADDRESS(FILE, ADDR) print_operand_address (FILE, ADDR)

/* DWARF unwinding needs two scratch registers; we choose to use r8-r9.  */
#define EH_RETURN_DATA_REGNO(N) \
  ((N) < 2 ? (N) + 6 : INVALID_REGNUM)

#define EH_RETURN_HANDLER_RTX \
  RETURN_ADDR_RTX (0, hard_frame_pointer_rtx)

/* Select a format to encode pointers in exception handling data.  CODE
   is 0 for data, 1 for code labels, 2 for function pointers.  GLOBAL is
   true if the symbol may be affected by dynamic relocations.  */
#define ASM_PREFERRED_EH_DATA_FORMAT(CODE, GLOBAL)			\
  ((flag_pic || GLOBAL) ? DW_EH_PE_aligned : DW_EH_PE_absptr)

#define AVOID_CCMODE_COPIES
