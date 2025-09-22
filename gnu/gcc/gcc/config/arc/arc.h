/* Definitions of target machine for GNU compiler, Argonaut ARC cpu.
   Copyright (C) 1994, 1995, 1997, 1998, 1999, 2000, 2001, 2002, 2004, 2005
   Free Software Foundation, Inc.

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

/* ??? This is an old port, and is undoubtedly suffering from bit rot.  */

/* Things to do:

   - incscc, decscc?
   - print active compiler options in assembler output
*/


#undef ASM_SPEC
#undef LINK_SPEC
#undef STARTFILE_SPEC
#undef ENDFILE_SPEC
#undef SIZE_TYPE
#undef PTRDIFF_TYPE
#undef WCHAR_TYPE
#undef WCHAR_TYPE_SIZE
#undef ASM_OUTPUT_LABELREF

/* Print subsidiary information on the compiler version in use.  */
#define TARGET_VERSION fprintf (stderr, " (arc)")

/* Names to predefine in the preprocessor for this target machine.  */
#define TARGET_CPU_CPP_BUILTINS()		\
  do						\
    {						\
	builtin_define ("__arc__");		\
	if (TARGET_BIG_ENDIAN)			\
	  builtin_define ("__big_endian__");	\
	if (arc_cpu_type == 0)			\
	  builtin_define ("__base__");		\
	builtin_assert ("cpu=arc");		\
	builtin_assert ("machine=arc");		\
    } while (0)

/* Pass -mmangle-cpu if we get -mcpu=*.
   Doing it this way lets one have it on as default with -mcpu=*,
   but also lets one turn it off with -mno-mangle-cpu.  */
#define CC1_SPEC "\
%{mcpu=*:-mmangle-cpu} \
%{EB:%{EL:%emay not use both -EB and -EL}} \
%{EB:-mbig-endian} %{EL:-mlittle-endian} \
"

#define ASM_SPEC "%{v} %{EB} %{EL}"

#define LINK_SPEC "%{v} %{EB} %{EL}"

#define STARTFILE_SPEC "%{!shared:crt0.o%s} crtinit.o%s"

#define ENDFILE_SPEC "crtfini.o%s"

/* Instruction set characteristics.
   These are internal macros, set by the appropriate -mcpu= option.  */

/* Nonzero means the cpu has a barrel shifter.  */
#define TARGET_SHIFTER 0

/* Which cpu we're compiling for.  */
extern int arc_cpu_type;

/* Check if CPU is an extension and set `arc_cpu_type' and `arc_mangle_cpu'
   appropriately.  The result should be nonzero if the cpu is recognized,
   otherwise zero.  This is intended to be redefined in a cover file.
   This is used by arc_init.  */
#define ARC_EXTENSION_CPU(cpu) 0

/* Sometimes certain combinations of command options do not make
   sense on a particular target machine.  You can define a macro
   `OVERRIDE_OPTIONS' to take account of this.  This macro, if
   defined, is executed once just after all the command options have
   been parsed.

   Don't use this macro to turn on various extra optimizations for
   `-O'.  That is what `OPTIMIZATION_OPTIONS' is for.  */


#define OVERRIDE_OPTIONS \
do {				\
  /* These need to be done at start up.  It's convenient to do them here.  */ \
  arc_init ();			\
} while (0)

/* Target machine storage layout.  */

/* Define this if most significant bit is lowest numbered
   in instructions that operate on numbered bit-fields.  */
#define BITS_BIG_ENDIAN 1

/* Define this if most significant byte of a word is the lowest numbered.  */
#define BYTES_BIG_ENDIAN (TARGET_BIG_ENDIAN)

/* Define this if most significant word of a multiword number is the lowest
   numbered.  */
#define WORDS_BIG_ENDIAN (TARGET_BIG_ENDIAN)

/* Define this to set the endianness to use in libgcc2.c, which can
   not depend on target_flags.  */
#ifdef __big_endian__
#define LIBGCC2_WORDS_BIG_ENDIAN 1
#else
#define LIBGCC2_WORDS_BIG_ENDIAN 0
#endif

/* Width of a word, in units (bytes).  */
#define UNITS_PER_WORD 4

/* Define this macro if it is advisable to hold scalars in registers
   in a wider mode than that declared by the program.  In such cases, 
   the value is constrained to be within the bounds of the declared
   type, but kept valid in the wider mode.  The signedness of the
   extension may differ from that of the type.  */
#define PROMOTE_MODE(MODE,UNSIGNEDP,TYPE) \
if (GET_MODE_CLASS (MODE) == MODE_INT		\
    && GET_MODE_SIZE (MODE) < UNITS_PER_WORD)	\
{						\
  (MODE) = SImode;				\
}

/* Allocation boundary (in *bits*) for storing arguments in argument list.  */
#define PARM_BOUNDARY 32

/* Boundary (in *bits*) on which stack pointer should be aligned.  */
#define STACK_BOUNDARY 64

/* ALIGN FRAMES on word boundaries */
#define ARC_STACK_ALIGN(LOC) (((LOC)+7) & ~7)

/* Allocation boundary (in *bits*) for the code of a function.  */
#define FUNCTION_BOUNDARY 32

/* Alignment of field after `int : 0' in a structure.  */
#define EMPTY_FIELD_BOUNDARY 32

/* Every structure's size must be a multiple of this.  */
#define STRUCTURE_SIZE_BOUNDARY 8

/* A bit-field declared as `int' forces `int' alignment for the struct.  */
#define PCC_BITFIELD_TYPE_MATTERS 1

/* No data type wants to be aligned rounder than this.  */
/* This is bigger than currently necessary for the ARC.  If 8 byte floats are
   ever added it's not clear whether they'll need such alignment or not.  For
   now we assume they will.  We can always relax it if necessary but the
   reverse isn't true.  */
#define BIGGEST_ALIGNMENT 64

/* The best alignment to use in cases where we have a choice.  */
#define FASTEST_ALIGNMENT 32

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
/* On the ARC the lower address bits are masked to 0 as necessary.  The chip
   won't croak when given an unaligned address, but the insn will still fail
   to produce the correct result.  */
#define STRICT_ALIGNMENT 1

/* Layout of source language data types.  */

#define SHORT_TYPE_SIZE		16
#define INT_TYPE_SIZE		32
#define LONG_TYPE_SIZE		32
#define LONG_LONG_TYPE_SIZE	64
#define FLOAT_TYPE_SIZE		32
#define DOUBLE_TYPE_SIZE	64
#define LONG_DOUBLE_TYPE_SIZE	64

/* Define this as 1 if `char' should by default be signed; else as 0.  */
#define DEFAULT_SIGNED_CHAR 1

#define SIZE_TYPE "long unsigned int"
#define PTRDIFF_TYPE "long int"
#define WCHAR_TYPE "short unsigned int"
#define WCHAR_TYPE_SIZE 16

/* Standard register usage.  */

/* Number of actual hardware registers.
   The hardware registers are assigned numbers for the compiler
   from 0 to just below FIRST_PSEUDO_REGISTER.
   All registers that the compiler knows about must be given numbers,
   even those that are not normally considered general registers.  */
/* Registers 61, 62, and 63 are not really registers and we needn't treat
   them as such.  We still need a register for the condition code.  */
#define FIRST_PSEUDO_REGISTER 62

/* 1 for registers that have pervasive standard uses
   and are not available for the register allocator.

   0-28  - general purpose registers
   29    - ilink1 (interrupt link register)
   30    - ilink2 (interrupt link register)
   31    - blink (branch link register)
   32-59 - reserved for extensions
   60    - LP_COUNT
   61    - condition code

   For doc purposes:
   61    - short immediate data indicator (setting flags)
   62    - long immediate data indicator
   63    - short immediate data indicator (not setting flags).

   The general purpose registers are further broken down into:
   0-7   - arguments/results
   8-15  - call used
   16-23 - call saved
   24    - call used, static chain pointer
   25    - call used, gptmp
   26    - global pointer
   27    - frame pointer
   28    - stack pointer

   By default, the extension registers are not available.  */

#define FIXED_REGISTERS \
{ 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 1, 1, 1, 1, 0,	\
				\
  1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1 }

/* 1 for registers not available across function calls.
   These must include the FIXED_REGISTERS and also any
   registers that can be used without being saved.
   The latter must include the registers where values are returned
   and the register where structure-value addresses are passed.
   Aside from that, you can include as many other registers as you like.  */

#define CALL_USED_REGISTERS \
{ 1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1,	\
  0, 0, 0, 0, 0, 0, 0, 0,	\
  1, 1, 1, 1, 1, 1, 1, 1,	\
				\
  1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1 }

/* If defined, an initializer for a vector of integers, containing the
   numbers of hard registers in the order in which GCC should
   prefer to use them (from most preferred to least).  */
#define REG_ALLOC_ORDER \
{ 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1,			\
  16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 31,			\
  32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,	\
  48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,		\
  27, 28, 29, 30 }

/* Macro to conditionally modify fixed_regs/call_used_regs.  */
#define CONDITIONAL_REGISTER_USAGE			\
do {							\
  if (PIC_OFFSET_TABLE_REGNUM != INVALID_REGNUM)	\
    {							\
      fixed_regs[PIC_OFFSET_TABLE_REGNUM] = 1;		\
      call_used_regs[PIC_OFFSET_TABLE_REGNUM] = 1;	\
    }							\
} while (0)

/* Return number of consecutive hard regs needed starting at reg REGNO
   to hold something of mode MODE.
   This is ordinarily the length in words of a value of mode MODE
   but can be less for certain modes in special long registers.  */
#define HARD_REGNO_NREGS(REGNO, MODE) \
((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* Value is 1 if hard register REGNO can hold a value of machine-mode MODE.  */
extern const unsigned int arc_hard_regno_mode_ok[];
extern unsigned int arc_mode_class[];
#define HARD_REGNO_MODE_OK(REGNO, MODE) \
((arc_hard_regno_mode_ok[REGNO] & arc_mode_class[MODE]) != 0)

/* A C expression that is nonzero if it is desirable to choose
   register allocation so as to avoid move instructions between a
   value of mode MODE1 and a value of mode MODE2.

   If `HARD_REGNO_MODE_OK (R, MODE1)' and `HARD_REGNO_MODE_OK (R,
   MODE2)' are ever different for any R, then `MODES_TIEABLE_P (MODE1,
   MODE2)' must be zero.  */

/* Tie QI/HI/SI modes together.  */
#define MODES_TIEABLE_P(MODE1, MODE2) \
(GET_MODE_CLASS (MODE1) == MODE_INT		\
 && GET_MODE_CLASS (MODE2) == MODE_INT		\
 && GET_MODE_SIZE (MODE1) <= UNITS_PER_WORD	\
 && GET_MODE_SIZE (MODE2) <= UNITS_PER_WORD)

/* Register classes and constants.  */

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
   class that represents their union.

   It is important that any condition codes have class NO_REGS.
   See `register_operand'.  */

enum reg_class {
  NO_REGS, LPCOUNT_REG, GENERAL_REGS, ALL_REGS, LIM_REG_CLASSES
};

#define N_REG_CLASSES (int) LIM_REG_CLASSES

/* Give names of register classes as strings for dump file.  */
#define REG_CLASS_NAMES \
{ "NO_REGS", "LPCOUNT_REG", "GENERAL_REGS", "ALL_REGS" }

/* Define which registers fit in which classes.
   This is an initializer for a vector of HARD_REG_SET
   of length N_REG_CLASSES.  */

#define REG_CLASS_CONTENTS \
{ {0, 0}, {0, 0x10000000}, {0xffffffff, 0xfffffff}, \
  {0xffffffff, 0x1fffffff} }

/* The same information, inverted:
   Return the class number of the smallest class containing
   reg number REGNO.  This could be a conditional expression
   or could index an array.  */
extern enum reg_class arc_regno_reg_class[FIRST_PSEUDO_REGISTER];
#define REGNO_REG_CLASS(REGNO) \
(arc_regno_reg_class[REGNO])

/* The class value for index registers, and the one for base regs.  */
#define INDEX_REG_CLASS GENERAL_REGS
#define BASE_REG_CLASS GENERAL_REGS

/* Get reg_class from a letter such as appears in the machine description.  */
#define REG_CLASS_FROM_LETTER(C) \
((C) == 'l' ? LPCOUNT_REG /* ??? needed? */ \
 : NO_REGS)

/* These assume that REGNO is a hard or pseudo reg number.
   They give nonzero only if REGNO is a hard reg of the suitable class
   or a pseudo reg currently allocated to a suitable hard reg.
   Since they use reg_renumber, they are safe only once reg_renumber
   has been allocated, which happens in local-alloc.c.  */
#define REGNO_OK_FOR_BASE_P(REGNO) \
((REGNO) < 32 || (unsigned) reg_renumber[REGNO] < 32)
#define REGNO_OK_FOR_INDEX_P(REGNO) \
((REGNO) < 32 || (unsigned) reg_renumber[REGNO] < 32)

/* Given an rtx X being reloaded into a reg required to be
   in class CLASS, return the class of reg to actually use.
   In general this is just CLASS; but on some machines
   in some cases it is preferable to use a more restrictive class.  */
#define PREFERRED_RELOAD_CLASS(X,CLASS) \
(CLASS)

/* Return the maximum number of consecutive registers
   needed to represent mode MODE in a register of class CLASS.  */
#define CLASS_MAX_NREGS(CLASS, MODE) \
((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* The letters I, J, K, L, M, N, O, P in a register constraint string
   can be used to stand for particular ranges of immediate operands.
   This macro defines what the ranges are.
   C is the letter, and VALUE is a constant value.
   Return 1 if VALUE is in the range specified by C.  */
/* 'I' is used for short immediates (always signed).
   'J' is used for long immediates.
   'K' is used for any constant up to 64 bits (for 64x32 situations?).  */

/* local to this file */
#define SMALL_INT(X) ((unsigned) ((X) + 0x100) < 0x200)
/* local to this file */
#define LARGE_INT(X) \
((X) >= (-(HOST_WIDE_INT) 0x7fffffff - 1) \
 && (unsigned HOST_WIDE_INT)(X) <= (unsigned HOST_WIDE_INT) 0xffffffff)

#define CONST_OK_FOR_LETTER_P(VALUE, C) \
((C) == 'I' ? SMALL_INT (VALUE)		\
 : (C) == 'J' ? LARGE_INT (VALUE)	\
 : (C) == 'K' ? 1			\
 : 0)

/* Similar, but for floating constants, and defining letters G and H.
   Here VALUE is the CONST_DOUBLE rtx itself.  */
/* 'G' is used for integer values for the multiplication insns where the
   operands are extended from 4 bytes to 8 bytes.
   'H' is used when any 64 bit constant is allowed.  */
#define CONST_DOUBLE_OK_FOR_LETTER_P(VALUE, C) \
((C) == 'G' ? arc_double_limm_p (VALUE) \
 : (C) == 'H' ? 1 \
 : 0)

/* A C expression that defines the optional machine-dependent constraint
   letters that can be used to segregate specific types of operands,
   usually memory references, for the target machine.  It should return 1 if
   VALUE corresponds to the operand type represented by the constraint letter
   C.  If C is not defined as an extra constraint, the value returned should
   be 0 regardless of VALUE.  */
/* ??? This currently isn't used.  Waiting for PIC.  */
#if 0
#define EXTRA_CONSTRAINT(VALUE, C) \
((C) == 'R' ? (SYMBOL_REF_FUNCTION_P (VALUE) || GET_CODE (VALUE) == LABEL_REF) \
 : 0)
#endif

/* Stack layout and stack pointer usage.  */

/* Define this macro if pushing a word onto the stack moves the stack
   pointer to a smaller address.  */
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

/* Offset from the stack pointer register to the first location at which
   outgoing arguments are placed.  */
#define STACK_POINTER_OFFSET FIRST_PARM_OFFSET (0)

/* Offset of first parameter from the argument pointer register value.  */
/* 4 bytes for each of previous fp, return address, and previous gp.
   4 byte reserved area for future considerations.  */
#define FIRST_PARM_OFFSET(FNDECL) 16

/* A C expression whose value is RTL representing the address in a
   stack frame where the pointer to the caller's frame is stored.
   Assume that FRAMEADDR is an RTL expression for the address of the
   stack frame itself.

   If you don't define this macro, the default is to return the value
   of FRAMEADDR--that is, the stack frame address is also the address
   of the stack word that points to the previous frame.  */
/* ??? unfinished */
/*define DYNAMIC_CHAIN_ADDRESS (FRAMEADDR)*/

/* A C expression whose value is RTL representing the value of the
   return address for the frame COUNT steps up from the current frame.
   FRAMEADDR is the frame pointer of the COUNT frame, or the frame
   pointer of the COUNT - 1 frame if `RETURN_ADDR_IN_PREVIOUS_FRAME'
   is defined.  */
/* The current return address is in r31.  The return address of anything
   farther back is at [%fp,4].  */
#if 0 /* The default value should work.  */
#define RETURN_ADDR_RTX(COUNT, FRAME) \
(((COUNT) == -1)							\
 ? gen_rtx_REG (Pmode, 31)						\
 : copy_to_reg (gen_rtx_MEM (Pmode,					\
			     memory_address (Pmode,			\
					     plus_constant ((FRAME),	\
							    UNITS_PER_WORD)))))
#endif

/* Register to use for pushing function arguments.  */
#define STACK_POINTER_REGNUM 28

/* Base register for access to local variables of the function.  */
#define FRAME_POINTER_REGNUM 27

/* Base register for access to arguments of the function.  */
#define ARG_POINTER_REGNUM FRAME_POINTER_REGNUM

/* Register in which static-chain is passed to a function.  This must
   not be a register used by the prologue.  */
#define STATIC_CHAIN_REGNUM 24

/* A C expression which is nonzero if a function must have and use a
   frame pointer.  This expression is evaluated in the reload pass.
   If its value is nonzero the function will have a frame pointer.  */
#define FRAME_POINTER_REQUIRED \
(current_function_calls_alloca)

/* C statement to store the difference between the frame pointer
   and the stack pointer values immediately after the function prologue.  */
#define INITIAL_FRAME_POINTER_OFFSET(VAR) \
((VAR) = arc_compute_frame_size (get_frame_size ()))

/* Function argument passing.  */

/* If defined, the maximum amount of space required for outgoing
   arguments will be computed and placed into the variable
   `current_function_outgoing_args_size'.  No space will be pushed
   onto the stack for each call; instead, the function prologue should
   increase the stack frame size by this amount.  */
#define ACCUMULATE_OUTGOING_ARGS 1

/* Value is the number of bytes of arguments automatically
   popped when returning from a subroutine call.
   FUNDECL is the declaration node of the function (as a tree),
   FUNTYPE is the data type of the function (as a tree),
   or for a library call it is an identifier node for the subroutine name.
   SIZE is the number of bytes of arguments passed on the stack.  */
#define RETURN_POPS_ARGS(DECL, FUNTYPE, SIZE) 0

/* Define a data type for recording info about an argument list
   during the scan of that argument list.  This data type should
   hold all necessary information about the function itself
   and about the args processed so far, enough to enable macros
   such as FUNCTION_ARG to determine where the next arg should go.  */
#define CUMULATIVE_ARGS int

/* Initialize a variable CUM of type CUMULATIVE_ARGS
   for a call to a function whose data type is FNTYPE.
   For a library call, FNTYPE is 0.  */
#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, INDIRECT, N_NAMED_ARGS) \
((CUM) = 0)

/* The number of registers used for parameter passing.  Local to this file.  */
#define MAX_ARC_PARM_REGS 8

/* 1 if N is a possible register number for function argument passing.  */
#define FUNCTION_ARG_REGNO_P(N) \
((unsigned) (N) < MAX_ARC_PARM_REGS)

/* The ROUND_ADVANCE* macros are local to this file.  */
/* Round SIZE up to a word boundary.  */
#define ROUND_ADVANCE(SIZE) \
(((SIZE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* Round arg MODE/TYPE up to the next word boundary.  */
#define ROUND_ADVANCE_ARG(MODE, TYPE) \
((MODE) == BLKmode				\
 ? ROUND_ADVANCE (int_size_in_bytes (TYPE))	\
 : ROUND_ADVANCE (GET_MODE_SIZE (MODE)))

/* Round CUM up to the necessary point for argument MODE/TYPE.  */
#define ROUND_ADVANCE_CUM(CUM, MODE, TYPE) \
((((MODE) == BLKmode ? TYPE_ALIGN (TYPE) : GET_MODE_BITSIZE (MODE)) \
  > BITS_PER_WORD)	\
 ? (((CUM) + 1) & ~1)	\
 : (CUM))

/* Return boolean indicating arg of type TYPE and mode MODE will be passed in
   a reg.  This includes arguments that have to be passed by reference as the
   pointer to them is passed in a reg if one is available (and that is what
   we're given).
   This macro is only used in this file.  */
#define PASS_IN_REG_P(CUM, MODE, TYPE) \
((CUM) < MAX_ARC_PARM_REGS						\
 && ((ROUND_ADVANCE_CUM ((CUM), (MODE), (TYPE))				\
      + ROUND_ADVANCE_ARG ((MODE), (TYPE))				\
      <= MAX_ARC_PARM_REGS)))

/* Determine where to put an argument to a function.
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
/* On the ARC the first MAX_ARC_PARM_REGS args are normally in registers
   and the rest are pushed.  */
#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED) \
(PASS_IN_REG_P ((CUM), (MODE), (TYPE))					\
 ? gen_rtx_REG ((MODE), ROUND_ADVANCE_CUM ((CUM), (MODE), (TYPE)))	\
 : 0)

/* Update the data in CUM to advance over an argument
   of mode MODE and data type TYPE.
   (TYPE is null for libcalls where that information may not be available.)  */
#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED) \
((CUM) = (ROUND_ADVANCE_CUM ((CUM), (MODE), (TYPE)) \
	  + ROUND_ADVANCE_ARG ((MODE), (TYPE))))

/* If defined, a C expression that gives the alignment boundary, in bits,
   of an argument with the specified mode and type.  If it is not defined, 
   PARM_BOUNDARY is used for all arguments.  */
#define FUNCTION_ARG_BOUNDARY(MODE, TYPE) \
(((TYPE) ? TYPE_ALIGN (TYPE) : GET_MODE_BITSIZE (MODE)) <= PARM_BOUNDARY \
 ? PARM_BOUNDARY \
 : 2 * PARM_BOUNDARY)

/* Function results.  */

/* Define how to find the value returned by a function.
   VALTYPE is the data type of the value (as a tree).
   If the precise function being called is known, FUNC is its FUNCTION_DECL;
   otherwise, FUNC is 0.  */
#define FUNCTION_VALUE(VALTYPE, FUNC) gen_rtx_REG (TYPE_MODE (VALTYPE), 0)

/* Define how to find the value returned by a library function
   assuming the value has mode MODE.  */
#define LIBCALL_VALUE(MODE) gen_rtx_REG (MODE, 0)

/* 1 if N is a possible register number for a function value
   as seen by the caller.  */
/* ??? What about r1 in DI/DF values.  */
#define FUNCTION_VALUE_REGNO_P(N) ((N) == 0)

/* Tell GCC to use TARGET_RETURN_IN_MEMORY.  */
#define DEFAULT_PCC_STRUCT_RETURN 0

/* EXIT_IGNORE_STACK should be nonzero if, when returning from a function,
   the stack pointer does not matter.  The value is tested only in
   functions that have frame pointers.
   No definition is equivalent to always zero.  */
#define EXIT_IGNORE_STACK 0

/* Epilogue delay slots.  */
#define DELAY_SLOTS_FOR_EPILOGUE arc_delay_slots_for_epilogue ()

#define ELIGIBLE_FOR_EPILOGUE_DELAY(TRIAL, SLOTS_FILLED) \
arc_eligible_for_epilogue_delay (TRIAL, SLOTS_FILLED)

/* Output assembler code to FILE to increment profiler label # LABELNO
   for profiling a function entry.  */
#define FUNCTION_PROFILER(FILE, LABELNO)

/* Trampolines.  */
/* ??? This doesn't work yet because GCC will use as the address of a nested
   function the address of the trampoline.  We need to use that address
   right shifted by 2.  It looks like we'll need PSImode after all. :-(  */

/* Output assembler code for a block containing the constant parts
   of a trampoline, leaving space for the variable parts.  */
/* On the ARC, the trampoline is quite simple as we have 32 bit immediate
   constants.

	mov r24,STATIC
	j.nd FUNCTION
*/
#define TRAMPOLINE_TEMPLATE(FILE) \
do { \
  assemble_aligned_integer (UNITS_PER_WORD, GEN_INT (0x631f7c00)); \
  assemble_aligned_integer (UNITS_PER_WORD, const0_rtx); \
  assemble_aligned_integer (UNITS_PER_WORD, GEN_INT (0x381f0000)); \
  assemble_aligned_integer (UNITS_PER_WORD, const0_rtx); \
} while (0)

/* Length in units of the trampoline for entering a nested function.  */
#define TRAMPOLINE_SIZE 16

/* Emit RTL insns to initialize the variable parts of a trampoline.
   FNADDR is an RTX for the address of the function's pure code.
   CXT is an RTX for the static chain value for the function.  */
#define INITIALIZE_TRAMPOLINE(TRAMP, FNADDR, CXT) \
do { \
  emit_move_insn (gen_rtx_MEM (SImode, plus_constant (TRAMP, 4)), CXT); \
  emit_move_insn (gen_rtx_MEM (SImode, plus_constant (TRAMP, 12)), FNADDR); \
  emit_insn (gen_flush_icache (validize_mem (gen_rtx_MEM (SImode, TRAMP)))); \
} while (0)

/* Addressing modes, and classification of registers for them.  */

/* Maximum number of registers that can appear in a valid memory address.  */
/* The `ld' insn allows 2, but the `st' insn only allows 1.  */
#define MAX_REGS_PER_ADDRESS 1

/* We have pre inc/dec (load/store with update).  */
#define HAVE_PRE_INCREMENT 1
#define HAVE_PRE_DECREMENT 1

/* Recognize any constant value that is a valid address.  */
#define CONSTANT_ADDRESS_P(X) \
(GET_CODE (X) == LABEL_REF || GET_CODE (X) == SYMBOL_REF	\
 || GET_CODE (X) == CONST_INT || GET_CODE (X) == CONST)

/* Nonzero if the constant value X is a legitimate general operand.
   We can handle any 32 or 64 bit constant.  */
/* "1" should work since the largest constant should be a 64 bit critter.  */
/* ??? Not sure what to do for 64x32 compiler.  */
#define LEGITIMATE_CONSTANT_P(X) 1

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

/* Nonzero if X is a hard reg that can be used as an index
   or if it is a pseudo reg.  */
#define REG_OK_FOR_INDEX_P(X) \
((unsigned) REGNO (X) - 32 >= FIRST_PSEUDO_REGISTER - 32)
/* Nonzero if X is a hard reg that can be used as a base reg
   or if it is a pseudo reg.  */
#define REG_OK_FOR_BASE_P(X) \
((unsigned) REGNO (X) - 32 >= FIRST_PSEUDO_REGISTER - 32)

#else

/* Nonzero if X is a hard reg that can be used as an index.  */
#define REG_OK_FOR_INDEX_P(X) REGNO_OK_FOR_INDEX_P (REGNO (X))
/* Nonzero if X is a hard reg that can be used as a base reg.  */
#define REG_OK_FOR_BASE_P(X) REGNO_OK_FOR_BASE_P (REGNO (X))

#endif

/* GO_IF_LEGITIMATE_ADDRESS recognizes an RTL expression
   that is a valid memory address for an instruction.
   The MODE argument is the machine mode for the MEM expression
   that wants to use this address.  */
/* The `ld' insn allows [reg],[reg+shimm],[reg+limm],[reg+reg],[limm]
   but the `st' insn only allows [reg],[reg+shimm],[limm].
   The only thing we can do is only allow the most strict case `st' and hope
   other parts optimize out the restrictions for `ld'.  */

/* local to this file */
#define RTX_OK_FOR_BASE_P(X) \
(REG_P (X) && REG_OK_FOR_BASE_P (X))

/* local to this file */
#define RTX_OK_FOR_INDEX_P(X) \
(0 && /*???*/ REG_P (X) && REG_OK_FOR_INDEX_P (X))

/* local to this file */
/* ??? Loads can handle any constant, stores can only handle small ones.  */
#define RTX_OK_FOR_OFFSET_P(X) \
(GET_CODE (X) == CONST_INT && SMALL_INT (INTVAL (X)))

#define LEGITIMATE_OFFSET_ADDRESS_P(MODE, X) \
(GET_CODE (X) == PLUS				\
 && RTX_OK_FOR_BASE_P (XEXP (X, 0))		\
 && (RTX_OK_FOR_INDEX_P (XEXP (X, 1))		\
     || RTX_OK_FOR_OFFSET_P (XEXP (X, 1))))

#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR)		\
{ if (RTX_OK_FOR_BASE_P (X))				\
    goto ADDR;						\
  if (LEGITIMATE_OFFSET_ADDRESS_P ((MODE), (X)))	\
    goto ADDR;						\
  if (GET_CODE (X) == CONST_INT && LARGE_INT (INTVAL (X))) \
    goto ADDR;						\
  if (GET_CODE (X) == SYMBOL_REF			\
	   || GET_CODE (X) == LABEL_REF			\
	   || GET_CODE (X) == CONST)			\
    goto ADDR;						\
  if ((GET_CODE (X) == PRE_DEC || GET_CODE (X) == PRE_INC) \
      /* We're restricted here by the `st' insn.  */	\
      && RTX_OK_FOR_BASE_P (XEXP ((X), 0)))		\
    goto ADDR;						\
}

/* Go to LABEL if ADDR (a legitimate address expression)
   has an effect that depends on the machine mode it is used for.  */
#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR, LABEL) \
{ if (GET_CODE (ADDR) == PRE_DEC)	\
    goto LABEL;				\
  if (GET_CODE (ADDR) == PRE_INC)	\
    goto LABEL;				\
}

/* Given a comparison code (EQ, NE, etc.) and the first operand of a COMPARE,
   return the mode to be used for the comparison.  */
#define SELECT_CC_MODE(OP, X, Y) \
arc_select_cc_mode (OP, X, Y)

/* Return nonzero if SELECT_CC_MODE will never return MODE for a
   floating point inequality comparison.  */
#define REVERSIBLE_CC_MODE(MODE) 1 /*???*/

/* Costs.  */

/* Compute extra cost of moving data between one register class
   and another.  */
#define REGISTER_MOVE_COST(MODE, CLASS1, CLASS2) 2

/* Compute the cost of moving data between registers and memory.  */
/* Memory is 3 times as expensive as registers.
   ??? Is that the right way to look at it?  */
#define MEMORY_MOVE_COST(MODE,CLASS,IN) \
(GET_MODE_SIZE (MODE) <= UNITS_PER_WORD ? 6 : 12)

/* The cost of a branch insn.  */
/* ??? What's the right value here?  Branches are certainly more
   expensive than reg->reg moves.  */
#define BRANCH_COST 2

/* Nonzero if access to memory by bytes is slow and undesirable.
   For RISC chips, it means that access to memory by bytes is no
   better than access by words when possible, so grab a whole word
   and maybe make use of that.  */
#define SLOW_BYTE_ACCESS 1

/* Define this macro if it is as good or better to call a constant
   function address than to call an address kept in a register.  */
/* On the ARC, calling through registers is slow.  */
#define NO_FUNCTION_CSE

/* Section selection.  */
/* WARNING: These section names also appear in dwarfout.c.  */

/* The names of the text, data, and readonly-data sections are runtime
   selectable.  */

#define ARC_SECTION_FORMAT		"\t.section %s"
#define ARC_DEFAULT_TEXT_SECTION	".text"
#define ARC_DEFAULT_DATA_SECTION	".data"
#define ARC_DEFAULT_RODATA_SECTION	".rodata"

extern const char *arc_text_section, *arc_data_section, *arc_rodata_section;

/* initfini.c uses this in an asm.  */
#if defined (CRT_INIT) || defined (CRT_FINI)
#define TEXT_SECTION_ASM_OP	"\t.section .text"
#else
#define TEXT_SECTION_ASM_OP	arc_text_section
#endif
#define DATA_SECTION_ASM_OP	arc_data_section

#undef  READONLY_DATA_SECTION_ASM_OP
#define READONLY_DATA_SECTION_ASM_OP	arc_rodata_section

#define BSS_SECTION_ASM_OP	"\t.section .bss"

/* Define this macro if jump tables (for tablejump insns) should be
   output in the text section, along with the assembler instructions.
   Otherwise, the readonly data section is used.
   This macro is irrelevant if there is no separate readonly data section.  */
/*#define JUMP_TABLES_IN_TEXT_SECTION*/

/* For DWARF.  Marginally different than default so output is "prettier"
   (and consistent with above).  */
#define PUSHSECTION_ASM_OP "\t.section "

/* Tell crtstuff.c we're using ELF.  */
#define OBJECT_FORMAT_ELF

/* PIC */

/* The register number of the register used to address a table of static
   data addresses in memory.  In some cases this register is defined by a
   processor's ``application binary interface'' (ABI).  When this macro
   is defined, RTL is generated for this register once, as with the stack
   pointer and frame pointer registers.  If this macro is not defined, it
   is up to the machine-dependent files to allocate such a register (if
   necessary).  */
#define PIC_OFFSET_TABLE_REGNUM  (flag_pic ? 26 : INVALID_REGNUM)

/* Define this macro if the register defined by PIC_OFFSET_TABLE_REGNUM is
   clobbered by calls.  Do not define this macro if PIC_OFFSET_TABLE_REGNUM
   is not defined.  */
/* This register is call-saved on the ARC.  */
/*#define PIC_OFFSET_TABLE_REG_CALL_CLOBBERED*/

/* A C expression that is nonzero if X is a legitimate immediate
   operand on the target machine when generating position independent code.
   You can assume that X satisfies CONSTANT_P, so you need not
   check this.  You can also assume `flag_pic' is true, so you need not
   check it either.  You need not define this macro if all constants
   (including SYMBOL_REF) can be immediate operands when generating
   position independent code.  */
/*#define LEGITIMATE_PIC_OPERAND_P(X)*/

/* Control the assembler format that we output.  */

/* A C string constant describing how to begin a comment in the target
   assembler language.  The compiler assumes that the comment will
   end at the end of the line.  */
#define ASM_COMMENT_START ";"

/* Output to assembler file text saying following lines
   may contain character constants, extra white space, comments, etc.  */
#define ASM_APP_ON ""

/* Output to assembler file text saying following lines
   no longer contain unusual constructs.  */
#define ASM_APP_OFF ""

/* Globalizing directive for a label.  */
#define GLOBAL_ASM_OP "\t.global\t"

/* This is how to output a reference to a user-level label named NAME.
   `assemble_name' uses this.  */
/* We mangle all user labels to provide protection from linking code
   compiled for different cpus.  */
/* We work around a dwarfout.c deficiency by watching for labels from it and
   not adding the '_' prefix nor the cpu suffix.  There is a comment in
   dwarfout.c that says it should be using (*targetm.asm_out.internal_label).  */
extern const char *arc_mangle_cpu;
#define ASM_OUTPUT_LABELREF(FILE, NAME) \
do {							\
  if ((NAME)[0] == '.' && (NAME)[1] == 'L')		\
    fprintf (FILE, "%s", NAME);				\
  else							\
    {							\
      fputc ('_', FILE);				\
      if (TARGET_MANGLE_CPU && arc_mangle_cpu != NULL)	\
	fprintf (FILE, "%s_", arc_mangle_cpu);		\
      fprintf (FILE, "%s", NAME);			\
    }							\
} while (0)

/* Assembler pseudo-op to equate one value with another.  */
/* ??? This is needed because dwarfout.c provides a default definition too
   late for defaults.h (which contains the default definition of ASM_OUTPUT_DEF
   that we use).  */
#define SET_ASM_OP "\t.set\t"

/* How to refer to registers in assembler output.
   This sequence is indexed by compiler's hard-register-number (see above).  */
#define REGISTER_NAMES \
{"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",		\
 "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",		\
 "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",	\
 "r24", "r25", "r26", "fp", "sp", "ilink1", "ilink2", "blink",	\
 "r32", "r33", "r34", "r35", "r36", "r37", "r38", "r39",	\
 "r40", "r41", "r42", "r43", "r44", "r45", "r46", "r47",	\
 "r48", "r49", "r50", "r51", "r52", "r53", "r54", "r55",	\
 "r56", "r57", "r58", "r59", "lp_count", "cc"}

/* Entry to the insn conditionalizer.  */
#define FINAL_PRESCAN_INSN(INSN, OPVEC, NOPERANDS) \
arc_final_prescan_insn (INSN, OPVEC, NOPERANDS)

/* A C expression which evaluates to true if CODE is a valid
   punctuation character for use in the `PRINT_OPERAND' macro.  */
extern char arc_punct_chars[256];
#define PRINT_OPERAND_PUNCT_VALID_P(CHAR) \
arc_punct_chars[(unsigned char) (CHAR)]

/* Print operand X (an rtx) in assembler syntax to file FILE.
   CODE is a letter or dot (`z' in `%z0') or 0 if no letter was specified.
   For `%' followed by punctuation, CODE is the punctuation and X is null.  */
#define PRINT_OPERAND(FILE, X, CODE) \
arc_print_operand (FILE, X, CODE)

/* A C compound statement to output to stdio stream STREAM the
   assembler syntax for an instruction operand that is a memory
   reference whose address is ADDR.  ADDR is an RTL expression.  */
#define PRINT_OPERAND_ADDRESS(FILE, ADDR) \
arc_print_operand_address (FILE, ADDR)

/* This is how to output an element of a case-vector that is absolute.  */
#define ASM_OUTPUT_ADDR_VEC_ELT(FILE, VALUE)  \
do {							\
  char label[30];					\
  ASM_GENERATE_INTERNAL_LABEL (label, "L", VALUE);	\
  fprintf (FILE, "\t.word %%st(");			\
  assemble_name (FILE, label);				\
  fprintf (FILE, ")\n");				\
} while (0)

/* This is how to output an element of a case-vector that is relative.  */
#define ASM_OUTPUT_ADDR_DIFF_ELT(FILE, BODY, VALUE, REL) \
do {							\
  char label[30];					\
  ASM_GENERATE_INTERNAL_LABEL (label, "L", VALUE);	\
  fprintf (FILE, "\t.word %%st(");			\
  assemble_name (FILE, label);				\
  fprintf (FILE, "-");					\
  ASM_GENERATE_INTERNAL_LABEL (label, "L", REL);	\
  assemble_name (FILE, label);				\
  fprintf (FILE, ")\n");				\
} while (0)

/* The desired alignment for the location counter at the beginning
   of a loop.  */
/* On the ARC, align loops to 32 byte boundaries (cache line size)
   if -malign-loops.  */
#define LOOP_ALIGN(LABEL) (TARGET_ALIGN_LOOPS ? 5 : 0)

/* This is how to output an assembler line
   that says to advance the location counter
   to a multiple of 2**LOG bytes.  */
#define ASM_OUTPUT_ALIGN(FILE,LOG) \
do { if ((LOG) != 0) fprintf (FILE, "\t.align %d\n", 1 << (LOG)); } while (0)

/* Debugging information.  */

/* Generate DBX and DWARF debugging information.  */
#define DBX_DEBUGGING_INFO 1

/* Prefer STABS (for now).  */
#undef PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE DBX_DEBUG

/* Turn off splitting of long stabs.  */
#define DBX_CONTIN_LENGTH 0

/* Miscellaneous.  */

/* Specify the machine mode that this machine uses
   for the index in the tablejump instruction.  */
#define CASE_VECTOR_MODE Pmode

/* Define if operations between registers always perform the operation
   on the full register even if a narrower mode is specified.  */
#define WORD_REGISTER_OPERATIONS

/* Define if loading in MODE, an integral mode narrower than BITS_PER_WORD
   will either zero-extend or sign-extend.  The value of this macro should
   be the code that says which one of the two operations is implicitly
   done, UNKNOWN if none.  */
#define LOAD_EXTEND_OP(MODE) ZERO_EXTEND

/* Max number of bytes we can move from memory to memory
   in one reasonably fast instruction.  */
#define MOVE_MAX 4

/* Define this to be nonzero if shift instructions ignore all but the low-order
   few bits.  */
#define SHIFT_COUNT_TRUNCATED 1

/* Value is 1 if truncating an integer of INPREC bits to OUTPREC bits
   is done just by pretending it is already truncated.  */
#define TRULY_NOOP_TRUNCATION(OUTPREC, INPREC) 1

/* Specify the machine mode that pointers have.
   After generation of rtl, the compiler makes no further distinction
   between pointers and any other objects of this machine mode.  */
/* ??? The arc doesn't have full 32 bit pointers, but making this PSImode has
   its own problems (you have to add extendpsisi2 and trucnsipsi2 but how does
   one do it without getting excess code?).  Try to avoid it.  */
#define Pmode SImode

/* A function address in a call instruction.  */
#define FUNCTION_MODE SImode

/* alloca should avoid clobbering the old register save area.  */
/* ??? Not defined in tm.texi.  */
#define SETJMP_VIA_SAVE_AREA

/* Define the information needed to generate branch and scc insns.  This is
   stored from the compare operation.  Note that we can't use "rtx" here
   since it hasn't been defined!  */
extern struct rtx_def *arc_compare_op0, *arc_compare_op1;

/* ARC function types.  */
enum arc_function_type {
  ARC_FUNCTION_UNKNOWN, ARC_FUNCTION_NORMAL,
  /* These are interrupt handlers.  The name corresponds to the register
     name that contains the return address.  */
  ARC_FUNCTION_ILINK1, ARC_FUNCTION_ILINK2
};
#define ARC_INTERRUPT_P(TYPE) \
((TYPE) == ARC_FUNCTION_ILINK1 || (TYPE) == ARC_FUNCTION_ILINK2)
/* Compute the type of a function from its DECL.  */


/* Implement `va_start' for varargs and stdarg.  */
#define EXPAND_BUILTIN_VA_START(valist, nextarg) \
  arc_va_start (valist, nextarg)
