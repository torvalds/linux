/* Definitions of target machine for GNU compiler.
   Matsushita MN10300 series
   Copyright (C) 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Jeff Law (law@cygnus.com).

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


#undef ASM_SPEC
#undef LIB_SPEC
#undef ENDFILE_SPEC
#undef LINK_SPEC
#define LINK_SPEC "%{mrelax:--relax}"
#undef STARTFILE_SPEC
#define STARTFILE_SPEC "%{!mno-crt0:%{!shared:%{pg:gcrt0%O%s}%{!pg:%{p:mcrt0%O%s}%{!p:crt0%O%s}}}}"

/* Names to predefine in the preprocessor for this target machine.  */

#define TARGET_CPU_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define ("__mn10300__");		\
      builtin_define ("__MN10300__");		\
      builtin_assert ("cpu=mn10300");		\
      builtin_assert ("machine=mn10300");	\
    }						\
  while (0)

#define CPP_SPEC "%{mam33:-D__AM33__} %{mam33-2:-D__AM33__=2 -D__AM33_2__}"

extern GTY(()) int mn10300_unspec_int_label_counter;

enum processor_type {
  PROCESSOR_MN10300,
  PROCESSOR_AM33,
  PROCESSOR_AM33_2
};

extern enum processor_type mn10300_processor;

#define TARGET_AM33	(mn10300_processor >= PROCESSOR_AM33)
#define TARGET_AM33_2	(mn10300_processor == PROCESSOR_AM33_2)

#ifndef PROCESSOR_DEFAULT
#define PROCESSOR_DEFAULT PROCESSOR_MN10300
#endif

#define OVERRIDE_OPTIONS mn10300_override_options ()

/* Print subsidiary information on the compiler version in use.  */

#define TARGET_VERSION fprintf (stderr, " (MN10300)");


/* Target machine storage layout */

/* Define this if most significant bit is lowest numbered
   in instructions that operate on numbered bit-fields.
   This is not true on the Matsushita MN1003.  */
#define BITS_BIG_ENDIAN 0

/* Define this if most significant byte of a word is the lowest numbered.  */
/* This is not true on the Matsushita MN10300.  */
#define BYTES_BIG_ENDIAN 0

/* Define this if most significant word of a multiword number is lowest
   numbered.
   This is not true on the Matsushita MN10300.  */
#define WORDS_BIG_ENDIAN 0

/* Width of a word, in units (bytes).  */
#define UNITS_PER_WORD		4

/* Allocation boundary (in *bits*) for storing arguments in argument list.  */
#define PARM_BOUNDARY		32

/* The stack goes in 32 bit lumps.  */
#define STACK_BOUNDARY 		32

/* Allocation boundary (in *bits*) for the code of a function.
   8 is the minimum boundary; it's unclear if bigger alignments
   would improve performance.  */
#define FUNCTION_BOUNDARY 8

/* No data type wants to be aligned rounder than this.  */
#define BIGGEST_ALIGNMENT	32

/* Alignment of field after `int : 0' in a structure.  */
#define EMPTY_FIELD_BOUNDARY 32

/* Define this if move instructions will actually fail to work
   when given unaligned data.  */
#define STRICT_ALIGNMENT 1

/* Define this as 1 if `char' should by default be signed; else as 0.  */
#define DEFAULT_SIGNED_CHAR 0

/* Standard register usage.  */

/* Number of actual hardware registers.
   The hardware registers are assigned numbers for the compiler
   from 0 to just below FIRST_PSEUDO_REGISTER.

   All registers that the compiler knows about must be given numbers,
   even those that are not normally considered general registers.  */

#define FIRST_PSEUDO_REGISTER 50

/* Specify machine-specific register numbers.  */
#define FIRST_DATA_REGNUM 0
#define LAST_DATA_REGNUM 3
#define FIRST_ADDRESS_REGNUM 4
#define LAST_ADDRESS_REGNUM 8
#define FIRST_EXTENDED_REGNUM 10
#define LAST_EXTENDED_REGNUM 17
#define FIRST_FP_REGNUM 18
#define LAST_FP_REGNUM 49

/* Specify the registers used for certain standard purposes.
   The values of these macros are register numbers.  */

/* Register to use for pushing function arguments.  */
#define STACK_POINTER_REGNUM (LAST_ADDRESS_REGNUM+1)

/* Base register for access to local variables of the function.  */
#define FRAME_POINTER_REGNUM (LAST_ADDRESS_REGNUM-1)

/* Base register for access to arguments of the function.  This
   is a fake register and will be eliminated into either the frame
   pointer or stack pointer.  */
#define ARG_POINTER_REGNUM LAST_ADDRESS_REGNUM

/* Register in which static-chain is passed to a function.  */
#define STATIC_CHAIN_REGNUM (FIRST_ADDRESS_REGNUM+1)

/* 1 for registers that have pervasive standard uses
   and are not available for the register allocator.  */

#define FIXED_REGISTERS \
  { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 \
  , 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 \
  , 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 \
  }

/* 1 for registers not available across function calls.
   These must include the FIXED_REGISTERS and also any
   registers that can be used without being saved.
   The latter must include the registers where values are returned
   and the register where structure-value addresses are passed.
   Aside from that, you can include as many other registers as you
   like.  */

#define CALL_USED_REGISTERS \
  { 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 \
  , 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 \
  , 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 \
  }

#define REG_ALLOC_ORDER \
  { 0, 1, 4, 5, 2, 3, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17, 8, 9 \
  , 42, 43, 44, 45, 46, 47, 48, 49, 34, 35, 36, 37, 38, 39, 40, 41 \
  , 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33 \
  }

#define CONDITIONAL_REGISTER_USAGE \
{						\
  unsigned int i;				\
						\
  if (!TARGET_AM33)				\
    {						\
      for (i = FIRST_EXTENDED_REGNUM; 		\
	   i <= LAST_EXTENDED_REGNUM; i++) 	\
	fixed_regs[i] = call_used_regs[i] = 1; 	\
    }						\
  if (!TARGET_AM33_2)				\
    {						\
      for (i = FIRST_FP_REGNUM;			\
	   i <= LAST_FP_REGNUM; 		\
           i++) 				\
	fixed_regs[i] = call_used_regs[i] = 1;	\
    }						\
  if (flag_pic)					\
    fixed_regs[PIC_OFFSET_TABLE_REGNUM] =       \
    call_used_regs[PIC_OFFSET_TABLE_REGNUM] = 1;\
}

/* Return number of consecutive hard regs needed starting at reg REGNO
   to hold something of mode MODE.

   This is ordinarily the length in words of a value of mode MODE
   but can be less for certain modes in special long registers.  */

#define HARD_REGNO_NREGS(REGNO, MODE)   \
  ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* Value is 1 if hard register REGNO can hold a value of machine-mode
   MODE.  */

#define HARD_REGNO_MODE_OK(REGNO, MODE) \
 ((REGNO_REG_CLASS (REGNO) == DATA_REGS \
   || (TARGET_AM33 && REGNO_REG_CLASS (REGNO) == ADDRESS_REGS) \
   || REGNO_REG_CLASS (REGNO) == EXTENDED_REGS) \
  ? ((REGNO) & 1) == 0 || GET_MODE_SIZE (MODE) <= 4	\
  : ((REGNO) & 1) == 0 || GET_MODE_SIZE (MODE) == 4)

/* Value is 1 if it is a good idea to tie two pseudo registers
   when one has mode MODE1 and one has mode MODE2.
   If HARD_REGNO_MODE_OK could produce different values for MODE1 and MODE2,
   for any hard reg, then this must be 0 for correct output.  */
#define MODES_TIEABLE_P(MODE1, MODE2) \
  (TARGET_AM33  \
   || MODE1 == MODE2 \
   || (GET_MODE_SIZE (MODE1) <= 4 && GET_MODE_SIZE (MODE2) <= 4))

/* 4 data, and effectively 3 address registers is small as far as I'm
   concerned.  */
#define SMALL_REGISTER_CLASSES 1

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
   
enum reg_class {
  NO_REGS, DATA_REGS, ADDRESS_REGS, SP_REGS,
  DATA_OR_ADDRESS_REGS, SP_OR_ADDRESS_REGS, 
  EXTENDED_REGS, DATA_OR_EXTENDED_REGS, ADDRESS_OR_EXTENDED_REGS,
  SP_OR_EXTENDED_REGS, SP_OR_ADDRESS_OR_EXTENDED_REGS, 
  FP_REGS, FP_ACC_REGS,
  GENERAL_REGS, ALL_REGS, LIM_REG_CLASSES
};

#define N_REG_CLASSES (int) LIM_REG_CLASSES

/* Give names of register classes as strings for dump file.  */

#define REG_CLASS_NAMES \
{ "NO_REGS", "DATA_REGS", "ADDRESS_REGS", \
  "SP_REGS", "DATA_OR_ADDRESS_REGS", "SP_OR_ADDRESS_REGS", \
  "EXTENDED_REGS", \
  "DATA_OR_EXTENDED_REGS", "ADDRESS_OR_EXTENDED_REGS", \
  "SP_OR_EXTENDED_REGS", "SP_OR_ADDRESS_OR_EXTENDED_REGS", \
  "FP_REGS", "FP_ACC_REGS", \
  "GENERAL_REGS", "ALL_REGS", "LIM_REGS" }

/* Define which registers fit in which classes.
   This is an initializer for a vector of HARD_REG_SET
   of length N_REG_CLASSES.  */

#define REG_CLASS_CONTENTS  			\
{  { 0,	0 },		/* No regs      */	\
 { 0x0000f, 0 },	/* DATA_REGS */		\
 { 0x001f0, 0 },	/* ADDRESS_REGS */	\
 { 0x00200, 0 },	/* SP_REGS */		\
 { 0x001ff, 0 },	/* DATA_OR_ADDRESS_REGS */\
 { 0x003f0, 0 },	/* SP_OR_ADDRESS_REGS */\
 { 0x3fc00, 0 },	/* EXTENDED_REGS */	\
 { 0x3fc0f, 0 },	/* DATA_OR_EXTENDED_REGS */	\
 { 0x3fdf0, 0 },	/* ADDRESS_OR_EXTENDED_REGS */	\
 { 0x3fe00, 0 },	/* SP_OR_EXTENDED_REGS */	\
 { 0x3fff0, 0 },	/* SP_OR_ADDRESS_OR_EXTENDED_REGS */	\
 { 0xfffc0000, 0x3ffff }, /* FP_REGS */		\
 { 0x03fc0000, 0 },	/* FP_ACC_REGS */	\
 { 0x3fdff, 0 }, 	/* GENERAL_REGS */	\
 { 0xffffffff, 0x3ffff } /* ALL_REGS 	*/	\
}

/* The same information, inverted:
   Return the class number of the smallest class containing
   reg number REGNO.  This could be a conditional expression
   or could index an array.  */

#define REGNO_REG_CLASS(REGNO) \
  ((REGNO) <= LAST_DATA_REGNUM ? DATA_REGS : \
   (REGNO) <= LAST_ADDRESS_REGNUM ? ADDRESS_REGS : \
   (REGNO) == STACK_POINTER_REGNUM ? SP_REGS : \
   (REGNO) <= LAST_EXTENDED_REGNUM ? EXTENDED_REGS : \
   (REGNO) <= LAST_FP_REGNUM ? FP_REGS : \
   NO_REGS)

/* The class value for index registers, and the one for base regs.  */
#define INDEX_REG_CLASS DATA_OR_EXTENDED_REGS
#define BASE_REG_CLASS  SP_OR_ADDRESS_REGS

/* Get reg_class from a letter such as appears in the machine description.  */

#define REG_CLASS_FROM_LETTER(C) \
  ((C) == 'd' ? DATA_REGS : \
   (C) == 'a' ? ADDRESS_REGS : \
   (C) == 'y' ? SP_REGS : \
   ! TARGET_AM33 ? NO_REGS : \
   (C) == 'x' ? EXTENDED_REGS : \
   ! TARGET_AM33_2 ? NO_REGS : \
   (C) == 'f' ? FP_REGS : \
   (C) == 'A' ? FP_ACC_REGS : \
   NO_REGS)

/* Macros to check register numbers against specific register classes.  */

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

/* These assume that REGNO is a hard or pseudo reg number.
   They give nonzero only if REGNO is a hard reg of the suitable class
   or a pseudo reg currently allocated to a suitable hard reg.
   Since they use reg_renumber, they are safe only once reg_renumber
   has been allocated, which happens in local-alloc.c.  */

#ifndef REG_OK_STRICT
# define REG_STRICT 0
#else
# define REG_STRICT 1
#endif

# define REGNO_IN_RANGE_P(regno,min,max,strict) \
  (IN_RANGE ((regno), (min), (max)) 		\
   || ((strict)					\
       ? (reg_renumber				\
	  && reg_renumber[(regno)] >= (min)	\
	  && reg_renumber[(regno)] <= (max))	\
       : (regno) >= FIRST_PSEUDO_REGISTER))

#define REGNO_DATA_P(regno, strict) \
  (REGNO_IN_RANGE_P ((regno), FIRST_DATA_REGNUM, LAST_DATA_REGNUM, \
		     (strict)))
#define REGNO_ADDRESS_P(regno, strict) \
  (REGNO_IN_RANGE_P ((regno), FIRST_ADDRESS_REGNUM, LAST_ADDRESS_REGNUM, \
		     (strict)))
#define REGNO_SP_P(regno, strict) \
  (REGNO_IN_RANGE_P ((regno), STACK_POINTER_REGNUM, STACK_POINTER_REGNUM, \
		     (strict)))
#define REGNO_EXTENDED_P(regno, strict) \
  (REGNO_IN_RANGE_P ((regno), FIRST_EXTENDED_REGNUM, LAST_EXTENDED_REGNUM, \
		     (strict)))
#define REGNO_AM33_P(regno, strict) \
  (REGNO_DATA_P ((regno), (strict)) || REGNO_ADDRESS_P ((regno), (strict)) \
   || REGNO_EXTENDED_P ((regno), (strict)))
#define REGNO_FP_P(regno, strict) \
  (REGNO_IN_RANGE_P ((regno), FIRST_FP_REGNUM, LAST_FP_REGNUM, (strict)))

#define REGNO_STRICT_OK_FOR_BASE_P(regno, strict) \
  (REGNO_SP_P ((regno), (strict)) \
   || REGNO_ADDRESS_P ((regno), (strict)) \
   || REGNO_EXTENDED_P ((regno), (strict)))
#define REGNO_OK_FOR_BASE_P(regno) \
  (REGNO_STRICT_OK_FOR_BASE_P ((regno), REG_STRICT))
#define REG_OK_FOR_BASE_P(X) \
  (REGNO_OK_FOR_BASE_P (REGNO (X)))

#define REGNO_STRICT_OK_FOR_BIT_BASE_P(regno, strict) \
  (REGNO_SP_P ((regno), (strict)) || REGNO_ADDRESS_P ((regno), (strict)))
#define REGNO_OK_FOR_BIT_BASE_P(regno) \
  (REGNO_STRICT_OK_FOR_BIT_BASE_P ((regno), REG_STRICT))
#define REG_OK_FOR_BIT_BASE_P(X) \
  (REGNO_OK_FOR_BIT_BASE_P (REGNO (X)))

#define REGNO_STRICT_OK_FOR_INDEX_P(regno, strict) \
  (REGNO_DATA_P ((regno), (strict)) || REGNO_EXTENDED_P ((regno), (strict)))
#define REGNO_OK_FOR_INDEX_P(regno) \
  (REGNO_STRICT_OK_FOR_INDEX_P ((regno), REG_STRICT))
#define REG_OK_FOR_INDEX_P(X) \
  (REGNO_OK_FOR_INDEX_P (REGNO (X)))

/* Given an rtx X being reloaded into a reg required to be
   in class CLASS, return the class of reg to actually use.
   In general this is just CLASS; but on some machines
   in some cases it is preferable to use a more restrictive class.  */

#define PREFERRED_RELOAD_CLASS(X,CLASS)				\
  ((X) == stack_pointer_rtx && (CLASS) != SP_REGS		\
   ? ADDRESS_OR_EXTENDED_REGS					\
   : (GET_CODE (X) == MEM					\
      || (GET_CODE (X) == REG					\
	  && REGNO (X) >= FIRST_PSEUDO_REGISTER)		\
      || (GET_CODE (X) == SUBREG				\
	  && GET_CODE (SUBREG_REG (X)) == REG			\
	  && REGNO (SUBREG_REG (X)) >= FIRST_PSEUDO_REGISTER)	\
      ? LIMIT_RELOAD_CLASS (GET_MODE (X), CLASS)		\
      : (CLASS)))

#define PREFERRED_OUTPUT_RELOAD_CLASS(X,CLASS) \
  (X == stack_pointer_rtx && CLASS != SP_REGS \
   ? ADDRESS_OR_EXTENDED_REGS : CLASS)

#define LIMIT_RELOAD_CLASS(MODE, CLASS) \
  (!TARGET_AM33 && (MODE == QImode || MODE == HImode) ? DATA_REGS : CLASS)

#define SECONDARY_RELOAD_CLASS(CLASS,MODE,IN) \
  mn10300_secondary_reload_class(CLASS,MODE,IN)

/* Return the maximum number of consecutive registers
   needed to represent mode MODE in a register of class CLASS.  */

#define CLASS_MAX_NREGS(CLASS, MODE)	\
  ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* A class that contains registers which the compiler must always
   access in a mode that is the same size as the mode in which it
   loaded the register.  */
#define CLASS_CANNOT_CHANGE_SIZE FP_REGS

/* The letters I, J, K, L, M, N, O, P in a register constraint string
   can be used to stand for particular ranges of immediate operands.
   This macro defines what the ranges are.
   C is the letter, and VALUE is a constant value.
   Return 1 if VALUE is in the range specified by C.  */

#define INT_8_BITS(VALUE) ((unsigned) (VALUE) + 0x80 < 0x100)
#define INT_16_BITS(VALUE) ((unsigned) (VALUE) + 0x8000 < 0x10000)

#define CONST_OK_FOR_I(VALUE) ((VALUE) == 0)
#define CONST_OK_FOR_J(VALUE) ((VALUE) == 1)
#define CONST_OK_FOR_K(VALUE) ((VALUE) == 2)
#define CONST_OK_FOR_L(VALUE) ((VALUE) == 4)
#define CONST_OK_FOR_M(VALUE) ((VALUE) == 3)
#define CONST_OK_FOR_N(VALUE) ((VALUE) == 255 || (VALUE) == 65535)

#define CONST_OK_FOR_LETTER_P(VALUE, C) \
  ((C) == 'I' ? CONST_OK_FOR_I (VALUE) : \
   (C) == 'J' ? CONST_OK_FOR_J (VALUE) : \
   (C) == 'K' ? CONST_OK_FOR_K (VALUE) : \
   (C) == 'L' ? CONST_OK_FOR_L (VALUE) : \
   (C) == 'M' ? CONST_OK_FOR_M (VALUE) : \
   (C) == 'N' ? CONST_OK_FOR_N (VALUE) : 0)


/* Similar, but for floating constants, and defining letters G and H.
   Here VALUE is the CONST_DOUBLE rtx itself. 
     
  `G' is a floating-point zero.  */

#define CONST_DOUBLE_OK_FOR_LETTER_P(VALUE, C) \
  ((C) == 'G' ? (GET_MODE_CLASS (GET_MODE (VALUE)) == MODE_FLOAT	\
		 && (VALUE) == CONST0_RTX (GET_MODE (VALUE))) : 0)


/* Stack layout; function entry, exit and calling.  */

/* Define this if pushing a word on the stack
   makes the stack pointer a smaller address.  */

#define STACK_GROWS_DOWNWARD

/* Define this to nonzero if the nominal address of the stack frame
   is at the high-address end of the local variables;
   that is, each additional local variable allocated
   goes at a more negative offset in the frame.  */

#define FRAME_GROWS_DOWNWARD 1

/* Offset within stack frame to start allocating local variables at.
   If FRAME_GROWS_DOWNWARD, this is the offset to the END of the
   first local allocated.  Otherwise, it is the offset to the BEGINNING
   of the first local allocated.  */

#define STARTING_FRAME_OFFSET 0

/* Offset of first parameter from the argument pointer register value.  */
/* Is equal to the size of the saved fp + pc, even if an fp isn't
   saved since the value is used before we know.  */

#define FIRST_PARM_OFFSET(FNDECL) 4

#define ELIMINABLE_REGS				\
{{ ARG_POINTER_REGNUM, STACK_POINTER_REGNUM},	\
 { ARG_POINTER_REGNUM, FRAME_POINTER_REGNUM},	\
 { FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM}}

#define CAN_ELIMINATE(FROM, TO) 1

#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET) \
  OFFSET = initial_offset (FROM, TO)

/* We can debug without frame pointers on the mn10300, so eliminate
   them whenever possible.  */
#define FRAME_POINTER_REQUIRED 0
#define CAN_DEBUG_WITHOUT_FP

/* Value is the number of bytes of arguments automatically
   popped when returning from a subroutine call.
   FUNDECL is the declaration node of the function (as a tree),
   FUNTYPE is the data type of the function (as a tree),
   or for a library call it is an identifier node for the subroutine name.
   SIZE is the number of bytes of arguments passed on the stack.  */

#define RETURN_POPS_ARGS(FUNDECL,FUNTYPE,SIZE) 0

/* We use d0/d1 for passing parameters, so allocate 8 bytes of space
   for a register flushback area.  */
#define REG_PARM_STACK_SPACE(DECL) 8
#define OUTGOING_REG_PARM_STACK_SPACE
#define ACCUMULATE_OUTGOING_ARGS 1

/* So we can allocate space for return pointers once for the function
   instead of around every call.  */
#define STACK_POINTER_OFFSET 4

/* 1 if N is a possible register number for function argument passing.
   On the MN10300, no registers are used in this way.  */

#define FUNCTION_ARG_REGNO_P(N) ((N) <= 1)


/* Define a data type for recording info about an argument list
   during the scan of that argument list.  This data type should
   hold all necessary information about the function itself
   and about the args processed so far, enough to enable macros
   such as FUNCTION_ARG to determine where the next arg should go.

   On the MN10300, this is a single integer, which is a number of bytes
   of arguments scanned so far.  */

#define CUMULATIVE_ARGS struct cum_arg
struct cum_arg {int nbytes; };

/* Initialize a variable CUM of type CUMULATIVE_ARGS
   for a call to a function whose data type is FNTYPE.
   For a library call, FNTYPE is 0.

   On the MN10300, the offset starts at 0.  */

#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, INDIRECT, N_NAMED_ARGS) \
 ((CUM).nbytes = 0)

/* Update the data in CUM to advance over an argument
   of mode MODE and data type TYPE.
   (TYPE is null for libcalls where that information may not be available.)  */

#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED)	\
 ((CUM).nbytes += ((MODE) != BLKmode			\
	           ? (GET_MODE_SIZE (MODE) + 3) & ~3	\
	           : (int_size_in_bytes (TYPE) + 3) & ~3))

/* Define where to put the arguments to a function.
   Value is zero to push the argument on the stack,
   or a hard register in which to store the argument.

   MODE is the argument's machine mode.
   TYPE is the data type of the argument (as a tree).
    This is null for libcalls where that information may
    not be available.
   CUM is a variable of type CUMULATIVE_ARGS which gives info about
    the preceding args and about the function being called.
   NAMED is nonzero if this argument is a named parameter
    (otherwise it is an extra parameter matching an ellipsis).  */

/* On the MN10300 all args are pushed.  */   

#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED) \
  function_arg (&CUM, MODE, TYPE, NAMED)

/* Define how to find the value returned by a function.
   VALTYPE is the data type of the value (as a tree).
   If the precise function being called is known, FUNC is its FUNCTION_DECL;
   otherwise, FUNC is 0.  */

#define FUNCTION_VALUE(VALTYPE, FUNC) \
  mn10300_function_value (VALTYPE, FUNC, 0)
#define FUNCTION_OUTGOING_VALUE(VALTYPE, FUNC) \
  mn10300_function_value (VALTYPE, FUNC, 1)

/* Define how to find the value returned by a library function
   assuming the value has mode MODE.  */

#define LIBCALL_VALUE(MODE) gen_rtx_REG (MODE, FIRST_DATA_REGNUM)

/* 1 if N is a possible register number for a function value.  */

#define FUNCTION_VALUE_REGNO_P(N) \
  ((N) == FIRST_DATA_REGNUM || (N) == FIRST_ADDRESS_REGNUM)

#define DEFAULT_PCC_STRUCT_RETURN 0

/* EXIT_IGNORE_STACK should be nonzero if, when returning from a function,
   the stack pointer does not matter.  The value is tested only in
   functions that have frame pointers.
   No definition is equivalent to always zero.  */

#define EXIT_IGNORE_STACK 1

/* Output assembler code to FILE to increment profiler label # LABELNO
   for profiling a function entry.  */

#define FUNCTION_PROFILER(FILE, LABELNO) ;

#define TRAMPOLINE_TEMPLATE(FILE)			\
  do {							\
    fprintf (FILE, "\tadd -4,sp\n");			\
    fprintf (FILE, "\t.long 0x0004fffa\n");		\
    fprintf (FILE, "\tmov (0,sp),a0\n");		\
    fprintf (FILE, "\tadd 4,sp\n");			\
    fprintf (FILE, "\tmov (13,a0),a1\n");		\
    fprintf (FILE, "\tmov (17,a0),a0\n");		\
    fprintf (FILE, "\tjmp (a0)\n");			\
    fprintf (FILE, "\t.long 0\n");			\
    fprintf (FILE, "\t.long 0\n");			\
  } while (0)

/* Length in units of the trampoline for entering a nested function.  */

#define TRAMPOLINE_SIZE 0x1b

#define TRAMPOLINE_ALIGNMENT 32

/* Emit RTL insns to initialize the variable parts of a trampoline.
   FNADDR is an RTX for the address of the function's pure code.
   CXT is an RTX for the static chain value for the function.  */

#define INITIALIZE_TRAMPOLINE(TRAMP, FNADDR, CXT)			\
{									\
  emit_move_insn (gen_rtx_MEM (SImode, plus_constant ((TRAMP), 0x14)),	\
 		 (CXT));						\
  emit_move_insn (gen_rtx_MEM (SImode, plus_constant ((TRAMP), 0x18)),	\
		 (FNADDR));						\
}
/* A C expression whose value is RTL representing the value of the return
   address for the frame COUNT steps up from the current frame.

   On the mn10300, the return address is not at a constant location
   due to the frame layout.  Luckily, it is at a constant offset from
   the argument pointer, so we define RETURN_ADDR_RTX to return a
   MEM using arg_pointer_rtx.  Reload will replace arg_pointer_rtx
   with a reference to the stack/frame pointer + an appropriate offset.  */

#define RETURN_ADDR_RTX(COUNT, FRAME)   \
  ((COUNT == 0)                         \
   ? gen_rtx_MEM (Pmode, arg_pointer_rtx) \
   : (rtx) 0)

/* Implement `va_start' for varargs and stdarg.  */
#define EXPAND_BUILTIN_VA_START(valist, nextarg) \
  mn10300_va_start (valist, nextarg)

/* 1 if X is an rtx for a constant that is a valid address.  */

#define CONSTANT_ADDRESS_P(X)   CONSTANT_P (X)

/* Extra constraints.  */
 
#define OK_FOR_Q(OP) \
   (GET_CODE (OP) == MEM && ! CONSTANT_ADDRESS_P (XEXP (OP, 0)))

#define OK_FOR_R(OP) \
   (GET_CODE (OP) == MEM					\
    && GET_MODE (OP) == QImode					\
    && (CONSTANT_ADDRESS_P (XEXP (OP, 0))			\
	|| (GET_CODE (XEXP (OP, 0)) == REG			\
	    && REG_OK_FOR_BIT_BASE_P (XEXP (OP, 0))		\
	    && XEXP (OP, 0) != stack_pointer_rtx)		\
	|| (GET_CODE (XEXP (OP, 0)) == PLUS			\
	    && GET_CODE (XEXP (XEXP (OP, 0), 0)) == REG		\
	    && REG_OK_FOR_BIT_BASE_P (XEXP (XEXP (OP, 0), 0))	\
	    && XEXP (XEXP (OP, 0), 0) != stack_pointer_rtx	\
	    && GET_CODE (XEXP (XEXP (OP, 0), 1)) == CONST_INT	\
	    && INT_8_BITS (INTVAL (XEXP (XEXP (OP, 0), 1))))))
	 
#define OK_FOR_T(OP) \
   (GET_CODE (OP) == MEM					\
    && GET_MODE (OP) == QImode					\
    && (GET_CODE (XEXP (OP, 0)) == REG				\
	&& REG_OK_FOR_BIT_BASE_P (XEXP (OP, 0))			\
	&& XEXP (OP, 0) != stack_pointer_rtx))

#define EXTRA_CONSTRAINT(OP, C) \
 ((C) == 'R' ? OK_FOR_R (OP) \
  : (C) == 'Q' ? OK_FOR_Q (OP) \
  : (C) == 'S' && flag_pic \
  ? GET_CODE (OP) == UNSPEC && (XINT (OP, 1) == UNSPEC_PLT \
				|| XINT (OP, 1) == UNSPEC_PIC) \
  : (C) == 'S' ? GET_CODE (OP) == SYMBOL_REF \
  : (C) == 'T' ? OK_FOR_T (OP) \
  : 0)

/* Maximum number of registers that can appear in a valid memory address.  */

#define MAX_REGS_PER_ADDRESS 2


#define HAVE_POST_INCREMENT (TARGET_AM33)

/* GO_IF_LEGITIMATE_ADDRESS recognizes an RTL expression
   that is a valid memory address for an instruction.
   The MODE argument is the machine mode for the MEM expression
   that wants to use this address.

   The other macros defined here are used only in GO_IF_LEGITIMATE_ADDRESS,
   except for CONSTANT_ADDRESS_P which is actually
   machine-independent.

   On the mn10300, the value in the address register must be
   in the same memory space/segment as the effective address.

   This is problematical for reload since it does not understand
   that base+index != index+base in a memory reference.

   Note it is still possible to use reg+reg addressing modes,
   it's just much more difficult.  For a discussion of a possible
   workaround and solution, see the comments in pa.c before the
   function record_unscaled_index_insn_codes.  */

/* Accept either REG or SUBREG where a register is valid.  */
  
#define RTX_OK_FOR_BASE_P(X, strict)				\
  ((REG_P (X) && REGNO_STRICT_OK_FOR_BASE_P (REGNO (X),		\
 					     (strict))) 	\
   || (GET_CODE (X) == SUBREG && REG_P (SUBREG_REG (X))		\
       && REGNO_STRICT_OK_FOR_BASE_P (REGNO (SUBREG_REG (X)),	\
 				      (strict))))

#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR)    	\
do							\
  {							\
    if (legitimate_address_p ((MODE), (X), REG_STRICT))	\
      goto ADDR;					\
  }							\
while (0) 


/* Try machine-dependent ways of modifying an illegitimate address
   to be legitimate.  If we find one, return the new, valid address.
   This macro is used in only one place: `memory_address' in explow.c.

   OLDX is the address as it was before break_out_memory_refs was called.
   In some cases it is useful to look at this to decide what needs to be done.

   MODE and WIN are passed so that this macro can use
   GO_IF_LEGITIMATE_ADDRESS.

   It is always safe for this macro to do nothing.  It exists to recognize
   opportunities to optimize the output.  */

#define LEGITIMIZE_ADDRESS(X, OLDX, MODE, WIN)  \
{ rtx orig_x = (X);				\
  (X) = legitimize_address (X, OLDX, MODE);	\
  if ((X) != orig_x && memory_address_p (MODE, X)) \
    goto WIN; }

/* Go to LABEL if ADDR (a legitimate address expression)
   has an effect that depends on the machine mode it is used for.  */

#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR,LABEL)        \
  if (GET_CODE (ADDR) == POST_INC) \
    goto LABEL

/* Nonzero if the constant value X is a legitimate general operand.
   It is given that X satisfies CONSTANT_P or is a CONST_DOUBLE.  */

#define LEGITIMATE_CONSTANT_P(X) 1

/* Zero if this needs fixing up to become PIC.  */

#define LEGITIMATE_PIC_OPERAND_P(X) (legitimate_pic_operand_p (X))

/* Register to hold the addressing base for
   position independent code access to data items.  */
#define PIC_OFFSET_TABLE_REGNUM	PIC_REG

/* The name of the pseudo-symbol representing the Global Offset Table.  */
#define GOT_SYMBOL_NAME "*_GLOBAL_OFFSET_TABLE_"

#define SYMBOLIC_CONST_P(X)	\
((GET_CODE (X) == SYMBOL_REF || GET_CODE (X) == LABEL_REF)	\
  && ! LEGITIMATE_PIC_OPERAND_P (X))

/* Non-global SYMBOL_REFs have SYMBOL_REF_FLAG enabled.  */
#define MN10300_GLOBAL_P(X) (! SYMBOL_REF_FLAG (X))

/* Recognize machine-specific patterns that may appear within
   constants.  Used for PIC-specific UNSPECs.  */
#define OUTPUT_ADDR_CONST_EXTRA(STREAM, X, FAIL) \
  do									\
    if (GET_CODE (X) == UNSPEC && XVECLEN ((X), 0) == 1)	\
      {									\
	switch (XINT ((X), 1))						\
	  {								\
	  case UNSPEC_INT_LABEL:					\
	    asm_fprintf ((STREAM), ".%LLIL%d",				\
 			 INTVAL (XVECEXP ((X), 0, 0)));			\
	    break;							\
	  case UNSPEC_PIC:						\
	    /* GLOBAL_OFFSET_TABLE or local symbols, no suffix.  */	\
	    output_addr_const ((STREAM), XVECEXP ((X), 0, 0));		\
	    break;							\
	  case UNSPEC_GOT:						\
	    output_addr_const ((STREAM), XVECEXP ((X), 0, 0));		\
	    fputs ("@GOT", (STREAM));					\
	    break;							\
	  case UNSPEC_GOTOFF:						\
	    output_addr_const ((STREAM), XVECEXP ((X), 0, 0));		\
	    fputs ("@GOTOFF", (STREAM));				\
	    break;							\
	  case UNSPEC_PLT:						\
	    output_addr_const ((STREAM), XVECEXP ((X), 0, 0));		\
	    fputs ("@PLT", (STREAM));					\
	    break;							\
	  default:							\
	    goto FAIL;							\
	  }								\
	break;								\
      }									\
    else								\
      goto FAIL;							\
  while (0)

/* Tell final.c how to eliminate redundant test instructions.  */

/* Here we define machine-dependent flags and fields in cc_status
   (see `conditions.h').  No extra ones are needed for the VAX.  */

/* Store in cc_status the expressions
   that the condition codes will describe
   after execution of an instruction whose pattern is EXP.
   Do not alter them if the instruction would not alter the cc's.  */

#define CC_OVERFLOW_UNUSABLE 0x200
#define CC_NO_CARRY CC_NO_OVERFLOW
#define NOTICE_UPDATE_CC(EXP, INSN) notice_update_cc(EXP, INSN)

#define REGISTER_MOVE_COST(MODE, CLASS1, CLASS2) \
  ((CLASS1 == CLASS2 && (CLASS1 == ADDRESS_REGS || CLASS1 == DATA_REGS)) ? 2 :\
   ((CLASS1 == ADDRESS_REGS || CLASS1 == DATA_REGS) && \
    (CLASS2 == ADDRESS_REGS || CLASS2 == DATA_REGS)) ? 4 : \
   (CLASS1 == SP_REGS && CLASS2 == ADDRESS_REGS) ? 2 : \
   (CLASS1 == ADDRESS_REGS && CLASS2 == SP_REGS) ? 4 : \
   ! TARGET_AM33 ? 6 : \
   (CLASS1 == SP_REGS || CLASS2 == SP_REGS) ? 6 : \
   (CLASS1 == CLASS2 && CLASS1 == EXTENDED_REGS) ? 6 : \
   (CLASS1 == FP_REGS || CLASS2 == FP_REGS) ? 6 : \
   (CLASS1 == EXTENDED_REGS || CLASS2 == EXTENDED_REGS) ? 4 : \
   4)

/* Nonzero if access to memory by bytes or half words is no faster
   than accessing full words.  */
#define SLOW_BYTE_ACCESS 1

/* Dispatch tables on the mn10300 are extremely expensive in terms of code
   and readonly data size.  So we crank up the case threshold value to
   encourage a series of if/else comparisons to implement many small switch
   statements.  In theory, this value could be increased much more if we
   were solely optimizing for space, but we keep it "reasonable" to avoid
   serious code efficiency lossage.  */
#define CASE_VALUES_THRESHOLD 6

#define NO_FUNCTION_CSE

/* According expr.c, a value of around 6 should minimize code size, and
   for the MN10300 series, that's our primary concern.  */
#define MOVE_RATIO 6

#define TEXT_SECTION_ASM_OP "\t.section .text"
#define DATA_SECTION_ASM_OP "\t.section .data"
#define BSS_SECTION_ASM_OP "\t.section .bss"

#define ASM_COMMENT_START "#"

/* Output to assembler file text saying following lines
   may contain character constants, extra white space, comments, etc.  */

#define ASM_APP_ON "#APP\n"

/* Output to assembler file text saying following lines
   no longer contain unusual constructs.  */

#define ASM_APP_OFF "#NO_APP\n"

/* This says how to output the assembler to define a global
   uninitialized but not common symbol.
   Try to use asm_output_bss to implement this macro.  */

#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN) \
  asm_output_aligned_bss ((FILE), (DECL), (NAME), (SIZE), (ALIGN))

/* Globalizing directive for a label.  */
#define GLOBAL_ASM_OP "\t.global "

/* This is how to output a reference to a user-level label named NAME.
   `assemble_name' uses this.  */

#undef ASM_OUTPUT_LABELREF
#define ASM_OUTPUT_LABELREF(FILE, NAME) \
  fprintf (FILE, "_%s", (*targetm.strip_name_encoding) (NAME))

#define ASM_PN_FORMAT "%s___%lu"

/* This is how we tell the assembler that two symbols have the same value.  */

#define ASM_OUTPUT_DEF(FILE,NAME1,NAME2) \
  do { assemble_name(FILE, NAME1); 	 \
       fputs(" = ", FILE);		 \
       assemble_name(FILE, NAME2);	 \
       fputc('\n', FILE); } while (0)


/* How to refer to registers in assembler output.
   This sequence is indexed by compiler's hard-register-number (see above).  */

#define REGISTER_NAMES \
{ "d0", "d1", "d2", "d3", "a0", "a1", "a2", "a3", "ap", "sp", \
  "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7" \
, "fs0", "fs1", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7" \
, "fs8", "fs9", "fs10", "fs11", "fs12", "fs13", "fs14", "fs15" \
, "fs16", "fs17", "fs18", "fs19", "fs20", "fs21", "fs22", "fs23" \
, "fs24", "fs25", "fs26", "fs27", "fs28", "fs29", "fs30", "fs31" \
}

#define ADDITIONAL_REGISTER_NAMES \
{ {"r8",  4}, {"r9",  5}, {"r10", 6}, {"r11", 7}, \
  {"r12", 0}, {"r13", 1}, {"r14", 2}, {"r15", 3}, \
  {"e0", 10}, {"e1", 11}, {"e2", 12}, {"e3", 13}, \
  {"e4", 14}, {"e5", 15}, {"e6", 16}, {"e7", 17} \
, {"fd0", 18}, {"fd2", 20}, {"fd4", 22}, {"fd6", 24} \
, {"fd8", 26}, {"fd10", 28}, {"fd12", 30}, {"fd14", 32} \
, {"fd16", 34}, {"fd18", 36}, {"fd20", 38}, {"fd22", 40} \
, {"fd24", 42}, {"fd26", 44}, {"fd28", 46}, {"fd30", 48} \
}

/* Print an instruction operand X on file FILE.
   look in mn10300.c for details */

#define PRINT_OPERAND(FILE, X, CODE)  print_operand(FILE,X,CODE)

/* Print a memory operand whose address is X, on file FILE.
   This uses a function in output-vax.c.  */

#define PRINT_OPERAND_ADDRESS(FILE, ADDR) print_operand_address (FILE, ADDR)

#define ASM_OUTPUT_REG_PUSH(FILE,REGNO)
#define ASM_OUTPUT_REG_POP(FILE,REGNO)

/* This is how to output an element of a case-vector that is absolute.  */

#define ASM_OUTPUT_ADDR_VEC_ELT(FILE, VALUE) \
  fprintf (FILE, "\t%s .L%d\n", ".long", VALUE)

/* This is how to output an element of a case-vector that is relative.  */

#define ASM_OUTPUT_ADDR_DIFF_ELT(FILE, BODY, VALUE, REL) \
  fprintf (FILE, "\t%s .L%d-.L%d\n", ".long", VALUE, REL)

#define ASM_OUTPUT_ALIGN(FILE,LOG)	\
  if ((LOG) != 0)			\
    fprintf (FILE, "\t.align %d\n", (LOG))

/* We don't have to worry about dbx compatibility for the mn10300.  */
#define DEFAULT_GDB_EXTENSIONS 1

/* Use dwarf2 debugging info by default.  */
#undef PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG

#define DWARF2_ASM_LINE_DEBUG_INFO 1

/* GDB always assumes the current function's frame begins at the value
   of the stack pointer upon entry to the current function.  Accessing
   local variables and parameters passed on the stack is done using the
   base of the frame + an offset provided by GCC.

   For functions which have frame pointers this method works fine;
   the (frame pointer) == (stack pointer at function entry) and GCC provides
   an offset relative to the frame pointer.

   This loses for functions without a frame pointer; GCC provides an offset
   which is relative to the stack pointer after adjusting for the function's
   frame size.  GDB would prefer the offset to be relative to the value of
   the stack pointer at the function's entry.  Yuk!  */
#define DEBUGGER_AUTO_OFFSET(X) \
  ((GET_CODE (X) == PLUS ? INTVAL (XEXP (X, 1)) : 0) \
    + (frame_pointer_needed \
       ? 0 : -initial_offset (FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM)))

#define DEBUGGER_ARG_OFFSET(OFFSET, X) \
  ((GET_CODE (X) == PLUS ? OFFSET : 0) \
    + (frame_pointer_needed \
       ? 0 : -initial_offset (ARG_POINTER_REGNUM, STACK_POINTER_REGNUM)))

/* Specify the machine mode that this machine uses
   for the index in the tablejump instruction.  */
#define CASE_VECTOR_MODE Pmode

/* Define if operations between registers always perform the operation
   on the full register even if a narrower mode is specified.  */
#define WORD_REGISTER_OPERATIONS

#define LOAD_EXTEND_OP(MODE) ZERO_EXTEND

/* This flag, if defined, says the same insns that convert to a signed fixnum
   also convert validly to an unsigned one.  */
#define FIXUNS_TRUNC_LIKE_FIX_TRUNC

/* Max number of bytes we can move from memory to memory
   in one reasonably fast instruction.  */
#define MOVE_MAX	4

/* Define if shifts truncate the shift count
   which implies one can omit a sign-extension or zero-extension
   of a shift count.  */
#define SHIFT_COUNT_TRUNCATED 1

/* Value is 1 if truncating an integer of INPREC bits to OUTPREC bits
   is done just by pretending it is already truncated.  */
#define TRULY_NOOP_TRUNCATION(OUTPREC, INPREC) 1

/* Specify the machine mode that pointers have.
   After generation of rtl, the compiler makes no further distinction
   between pointers and any other objects of this machine mode.  */
#define Pmode SImode

/* A function address in a call instruction
   is a byte address (for indexing purposes)
   so give the MEM rtx a byte's mode.  */
#define FUNCTION_MODE QImode

/* The assembler op to get a word.  */

#define FILE_ASM_OP "\t.file\n"

typedef struct mn10300_cc_status_mdep
  {
    int fpCC;
  }
cc_status_mdep;

#define CC_STATUS_MDEP cc_status_mdep

#define CC_STATUS_MDEP_INIT (cc_status.mdep.fpCC = 0)
