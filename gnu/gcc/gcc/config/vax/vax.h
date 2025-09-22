/* Definitions of target machine for GNU compiler.  VAX version.
   Copyright (C) 1987, 1988, 1991, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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


/* Target CPU builtins.  */
#define TARGET_CPU_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define ("__vax__");		\
      builtin_assert ("cpu=vax");		\
      builtin_assert ("machine=vax");		\
      if (TARGET_G_FLOAT)			\
	{					\
	  builtin_define ("__GFLOAT");		\
	  builtin_define ("__GFLOAT__");	\
	}					\
    }						\
  while (0)

#define VMS_TARGET 0

/* Use -J option for long branch support with Unix assembler.  */

#define ASM_SPEC "-J"

/* Choose proper libraries depending on float format.
   Note that there are no profiling libraries for g-format.
   Also use -lg for the sake of dbx.  */

#define LIB_SPEC "%{g:-lg}\
 %{mg:%{lm:-lmg} -lcg \
  %{p:%eprofiling not supported with -mg\n}\
  %{pg:%eprofiling not supported with -mg\n}}\
 %{!mg:%{!p:%{!pg:-lc}}%{p:-lc_p}%{pg:-lc_p}}"

/* Print subsidiary information on the compiler version in use.  */

#ifndef TARGET_NAME	/* A more specific value might be supplied via -D.  */
#define TARGET_NAME "vax"
#endif
#define TARGET_VERSION fprintf (stderr, " (%s)", TARGET_NAME)

/* Run-time compilation parameters selecting different hardware subsets.  */

/* Nonzero if ELF.  Redefined by vax/elf.h.  */
#define TARGET_ELF 0

/* Default target_flags if no switches specified.  */

#ifndef TARGET_DEFAULT
#define TARGET_DEFAULT (MASK_UNIX_ASM)
#endif

#define OVERRIDE_OPTIONS override_options ()


/* Target machine storage layout */

/* Define this if most significant bit is lowest numbered
   in instructions that operate on numbered bit-fields.
   This is not true on the VAX.  */
#define BITS_BIG_ENDIAN 0

/* Define this if most significant byte of a word is the lowest numbered.  */
/* That is not true on the VAX.  */
#define BYTES_BIG_ENDIAN 0

/* Define this if most significant word of a multiword number is the lowest
   numbered.  */
/* This is not true on the VAX.  */
#define WORDS_BIG_ENDIAN 0

/* Width of a word, in units (bytes).  */
#define UNITS_PER_WORD 4

/* Allocation boundary (in *bits*) for storing arguments in argument list.  */
#define PARM_BOUNDARY 32

/* Allocation boundary (in *bits*) for the code of a function.  */
#define FUNCTION_BOUNDARY 16

/* Alignment of field after `int : 0' in a structure.  */
#define EMPTY_FIELD_BOUNDARY (TARGET_VAXC_ALIGNMENT ? 8 : 32)

/* Every structure's size must be a multiple of this.  */
#define STRUCTURE_SIZE_BOUNDARY 8

/* A bit-field declared as `int' forces `int' alignment for the struct.  */
#define PCC_BITFIELD_TYPE_MATTERS (!TARGET_VAXC_ALIGNMENT)

/* No data type wants to be aligned rounder than this.  */
#define BIGGEST_ALIGNMENT 32

/* No structure field wants to be aligned rounder than this.  */
#define BIGGEST_FIELD_ALIGNMENT (TARGET_VAXC_ALIGNMENT ? 8 : 32)

/* Set this nonzero if move instructions will actually fail to work
   when given unaligned data.  */
#define STRICT_ALIGNMENT 0

/* Let's keep the stack somewhat aligned.  */
#define STACK_BOUNDARY 32

/* The table of an ADDR_DIFF_VEC must be contiguous with the case
   opcode, it is part of the case instruction.  */
#define ADDR_VEC_ALIGN(ADDR_VEC) 0

/* Standard register usage.  */

/* Number of actual hardware registers.
   The hardware registers are assigned numbers for the compiler
   from 0 to just below FIRST_PSEUDO_REGISTER.
   All registers that the compiler knows about must be given numbers,
   even those that are not normally considered general registers.  */
#define FIRST_PSEUDO_REGISTER 16

/* 1 for registers that have pervasive standard uses
   and are not available for the register allocator.
   On the VAX, these are the AP, FP, SP and PC.  */
#define FIXED_REGISTERS {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1}

/* 1 for registers not available across function calls.
   These must include the FIXED_REGISTERS and also any
   registers that can be used without being saved.
   The latter must include the registers where values are returned
   and the register where structure-value addresses are passed.
   Aside from that, you can include as many other registers as you like.  */
#define CALL_USED_REGISTERS {1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1}

/* Return number of consecutive hard regs needed starting at reg REGNO
   to hold something of mode MODE.
   This is ordinarily the length in words of a value of mode MODE
   but can be less for certain modes in special long registers.
   On the VAX, all registers are one word long.  */
#define HARD_REGNO_NREGS(REGNO, MODE)	\
  ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* Value is 1 if hard register REGNO can hold a value of machine-mode MODE.
   On the VAX, all registers can hold all modes.  */
#define HARD_REGNO_MODE_OK(REGNO, MODE) 1

/* Value is 1 if it is a good idea to tie two pseudo registers
   when one has mode MODE1 and one has mode MODE2.
   If HARD_REGNO_MODE_OK could produce different values for MODE1 and MODE2,
   for any hard reg, then this must be 0 for correct output.  */
#define MODES_TIEABLE_P(MODE1, MODE2)  1

/* Specify the registers used for certain standard purposes.
   The values of these macros are register numbers.  */

/* VAX pc is overloaded on a register.  */
#define PC_REGNUM VAX_PC_REGNUM

/* Register to use for pushing function arguments.  */
#define STACK_POINTER_REGNUM VAX_SP_REGNUM

/* Base register for access to local variables of the function.  */
#define FRAME_POINTER_REGNUM VAX_FP_REGNUM

/* Value should be nonzero if functions must have frame pointers.
   Zero means the frame pointer need not be set up (and parms
   may be accessed via the stack pointer) in functions that seem suitable.
   This is computed in `reload', in reload1.c.  */
#define FRAME_POINTER_REQUIRED 1

/* Base register for access to arguments of the function.  */
#define ARG_POINTER_REGNUM VAX_AP_REGNUM

/* Register in which static-chain is passed to a function.  */
#define STATIC_CHAIN_REGNUM 0

/* Register in which address to store a structure value
   is passed to a function.  */
#define VAX_STRUCT_VALUE_REGNUM 1

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

/* The VAX has only one kind of registers, so NO_REGS and ALL_REGS
   are the only classes.  */

enum reg_class { NO_REGS, ALL_REGS, LIM_REG_CLASSES };

#define N_REG_CLASSES (int) LIM_REG_CLASSES

/* Since GENERAL_REGS is the same class as ALL_REGS,
   don't give it a different class number; just make it an alias.  */

#define GENERAL_REGS ALL_REGS

/* Give names of register classes as strings for dump file.  */

#define REG_CLASS_NAMES	\
  { "NO_REGS", "ALL_REGS" }

/* Define which registers fit in which classes.
   This is an initializer for a vector of HARD_REG_SET
   of length N_REG_CLASSES.  */

#define REG_CLASS_CONTENTS {{0}, {0xffff}}

/* The same information, inverted:
   Return the class number of the smallest class containing
   reg number REGNO.  This could be a conditional expression
   or could index an array.  */

#define REGNO_REG_CLASS(REGNO) ALL_REGS

/* The class value for index registers, and the one for base regs.  */

#define INDEX_REG_CLASS ALL_REGS
#define BASE_REG_CLASS ALL_REGS

/* Get reg_class from a letter such as appears in the machine description.  */

#define REG_CLASS_FROM_LETTER(C) NO_REGS

/* The letters I, J, K, L, M, N, and O in a register constraint string
   can be used to stand for particular ranges of immediate operands.
   This macro defines what the ranges are.
   C is the letter, and VALUE is a constant value.
   Return 1 if VALUE is in the range specified by C.

   `I' is the constant zero.
   `J' is a value between 0 .. 63 (inclusive)
   `K' is a value between -128 and 127 (inclusive)
   'L' is a value between -32768 and 32767 (inclusive)
   `M' is a value between 0 and 255 (inclusive)
   'N' is a value between 0 and 65535 (inclusive)
   `O' is a value between -63 and -1 (inclusive)  */

#define CONST_OK_FOR_LETTER_P(VALUE, C)				\
  (  (C) == 'I' ?	(VALUE) == 0				\
   : (C) == 'J' ?	0 <= (VALUE) && (VALUE) < 64		\
   : (C) == 'O' ?	-63 <= (VALUE) && (VALUE) < 0		\
   : (C) == 'K' ?	-128 <= (VALUE) && (VALUE) < 128	\
   : (C) == 'M' ?	0 <= (VALUE) && (VALUE) < 256		\
   : (C) == 'L' ?	-32768 <= (VALUE) && (VALUE) < 32768	\
   : (C) == 'N' ?	0 <= (VALUE) && (VALUE) < 65536		\
   : 0)

/* Similar, but for floating constants, and defining letters G and H.
   Here VALUE is the CONST_DOUBLE rtx itself.

   `G' is a floating-point zero.  */

#define CONST_DOUBLE_OK_FOR_LETTER_P(VALUE, C)		\
  ((C) == 'G' ? ((VALUE) == CONST0_RTX (DFmode)		\
		 || (VALUE) == CONST0_RTX (SFmode))	\
   : 0)

/* Optional extra constraints for this machine.

   For the VAX, `Q' means that OP is a MEM that does not have a mode-dependent
   address.  */

#define EXTRA_CONSTRAINT(OP, C)					\
  ((C) == 'Q'							\
   ? MEM_P (OP) && !mode_dependent_address_p (XEXP (OP, 0))	\
   : 0)

/* Given an rtx X being reloaded into a reg required to be
   in class CLASS, return the class of reg to actually use.
   In general this is just CLASS; but on some machines
   in some cases it is preferable to use a more restrictive class.  */

#define PREFERRED_RELOAD_CLASS(X,CLASS)  (CLASS)

/* Return the maximum number of consecutive registers
   needed to represent mode MODE in a register of class CLASS.  */
/* On the VAX, this is always the size of MODE in words,
   since all registers are the same size.  */
#define CLASS_MAX_NREGS(CLASS, MODE)	\
 ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

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

/* Given an rtx for the address of a frame,
   return an rtx for the address of the word in the frame
   that holds the dynamic chain--the previous frame's address.  */
#define DYNAMIC_CHAIN_ADDRESS(FRAME) plus_constant ((FRAME), 12)

/* If we generate an insn to push BYTES bytes,
   this says how many the stack pointer really advances by.
   On the VAX, -(sp) pushes only the bytes of the operands.  */
#define PUSH_ROUNDING(BYTES) (BYTES)

/* Offset of first parameter from the argument pointer register value.  */
#define FIRST_PARM_OFFSET(FNDECL) 4

/* Value is the number of bytes of arguments automatically
   popped when returning from a subroutine call.
   FUNDECL is the declaration node of the function (as a tree),
   FUNTYPE is the data type of the function (as a tree),
   or for a library call it is an identifier node for the subroutine name.
   SIZE is the number of bytes of arguments passed on the stack.

   On the VAX, the RET insn pops a maximum of 255 args for any function.  */

#define RETURN_POPS_ARGS(FUNDECL,FUNTYPE,SIZE)	\
  ((SIZE) > 255 * 4 ? 0 : (SIZE))

/* Define how to find the value returned by a function.
   VALTYPE is the data type of the value (as a tree).
   If the precise function being called is known, FUNC is its FUNCTION_DECL;
   otherwise, FUNC is 0.  */

/* On the VAX the return value is in R0 regardless.  */

#define FUNCTION_VALUE(VALTYPE, FUNC)	\
  gen_rtx_REG (TYPE_MODE (VALTYPE), 0)

/* Define how to find the value returned by a library function
   assuming the value has mode MODE.  */

/* On the VAX the return value is in R0 regardless.  */

#define LIBCALL_VALUE(MODE)  gen_rtx_REG (MODE, 0)

/* Define this if PCC uses the nonreentrant convention for returning
   structure and union values.  */

#define PCC_STATIC_STRUCT_RETURN

/* 1 if N is a possible register number for a function value.
   On the VAX, R0 is the only register thus used.  */

#define FUNCTION_VALUE_REGNO_P(N) ((N) == 0)

/* 1 if N is a possible register number for function argument passing.
   On the VAX, no registers are used in this way.  */

#define FUNCTION_ARG_REGNO_P(N) 0

/* Define a data type for recording info about an argument list
   during the scan of that argument list.  This data type should
   hold all necessary information about the function itself
   and about the args processed so far, enough to enable macros
   such as FUNCTION_ARG to determine where the next arg should go.

   On the VAX, this is a single integer, which is a number of bytes
   of arguments scanned so far.  */

#define CUMULATIVE_ARGS int

/* Initialize a variable CUM of type CUMULATIVE_ARGS
   for a call to a function whose data type is FNTYPE.
   For a library call, FNTYPE is 0.

   On the VAX, the offset starts at 0.  */

#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, INDIRECT, N_NAMED_ARGS) \
 ((CUM) = 0)

/* Update the data in CUM to advance over an argument
   of mode MODE and data type TYPE.
   (TYPE is null for libcalls where that information may not be available.)  */

#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED)	\
  ((CUM) += ((MODE) != BLKmode				\
	     ? (GET_MODE_SIZE (MODE) + 3) & ~3		\
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

/* On the VAX all args are pushed.  */

#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED) 0

/* Output assembler code to FILE to increment profiler label # LABELNO
   for profiling a function entry.  */

#define VAX_FUNCTION_PROFILER_NAME "mcount"
#define FUNCTION_PROFILER(FILE, LABELNO)			\
  do								\
    {								\
      char label[256];						\
      ASM_GENERATE_INTERNAL_LABEL (label, "LP", (LABELNO));	\
      fprintf (FILE, "\tmovab ");				\
      assemble_name (FILE, label);				\
      asm_fprintf (FILE, ",%Rr0\n\tjsb %s\n",			\
		   VAX_FUNCTION_PROFILER_NAME);			\
    }								\
  while (0)

/* EXIT_IGNORE_STACK should be nonzero if, when returning from a function,
   the stack pointer does not matter.  The value is tested only in
   functions that have frame pointers.
   No definition is equivalent to always zero.  */

#define EXIT_IGNORE_STACK 1

/* Store in the variable DEPTH the initial difference between the
   frame pointer reg contents and the stack pointer reg contents,
   as of the start of the function body.  This depends on the layout
   of the fixed parts of the stack frame and on how registers are saved.

   On the VAX, FRAME_POINTER_REQUIRED is always 1, so the definition of this
   macro doesn't matter.  But it must be defined.  */

#define INITIAL_FRAME_POINTER_OFFSET(DEPTH) (DEPTH) = 0;

/* Output assembler code for a block containing the constant parts
   of a trampoline, leaving space for the variable parts.  */

/* On the VAX, the trampoline contains an entry mask and two instructions:
     .word NN
     movl $STATIC,r0   (store the functions static chain)
     jmp  *$FUNCTION   (jump to function code at address FUNCTION)  */

#define TRAMPOLINE_TEMPLATE(FILE)					\
{									\
  assemble_aligned_integer (2, const0_rtx);				\
  assemble_aligned_integer (2, GEN_INT (0x8fd0));			\
  assemble_aligned_integer (4, const0_rtx);				\
  assemble_aligned_integer (1, GEN_INT (0x50 + STATIC_CHAIN_REGNUM));	\
  assemble_aligned_integer (2, GEN_INT (0x9f17));			\
  assemble_aligned_integer (4, const0_rtx);				\
}

/* Length in units of the trampoline for entering a nested function.  */

#define TRAMPOLINE_SIZE 15

/* Emit RTL insns to initialize the variable parts of a trampoline.
   FNADDR is an RTX for the address of the function's pure code.
   CXT is an RTX for the static chain value for the function.  */

/* We copy the register-mask from the function's pure code
   to the start of the trampoline.  */
#define INITIALIZE_TRAMPOLINE(TRAMP, FNADDR, CXT)			\
{									\
  emit_move_insn (gen_rtx_MEM (HImode, TRAMP),				\
		  gen_rtx_MEM (HImode, FNADDR));			\
  emit_move_insn (gen_rtx_MEM (SImode, plus_constant (TRAMP, 4)), CXT);	\
  emit_move_insn (gen_rtx_MEM (SImode, plus_constant (TRAMP, 11)),	\
		  plus_constant (FNADDR, 2));				\
  emit_insn (gen_sync_istream ());					\
}

/* Byte offset of return address in a stack frame.  The "saved PC" field
   is in element [4] when treating the frame as an array of longwords.  */

#define RETURN_ADDRESS_OFFSET	(4 * UNITS_PER_WORD)	/* 16 */

/* A C expression whose value is RTL representing the value of the return
   address for the frame COUNT steps up from the current frame.
   FRAMEADDR is already the frame pointer of the COUNT frame, so we
   can ignore COUNT.  */

#define RETURN_ADDR_RTX(COUNT, FRAME)					\
  ((COUNT == 0)								\
   ? gen_rtx_MEM (Pmode, plus_constant (FRAME, RETURN_ADDRESS_OFFSET))	\
   : (rtx) 0)


/* Addressing modes, and classification of registers for them.  */

#define HAVE_POST_INCREMENT 1

#define HAVE_PRE_DECREMENT 1

/* Macros to check register numbers against specific register classes.  */

/* These assume that REGNO is a hard or pseudo reg number.
   They give nonzero only if REGNO is a hard reg of the suitable class
   or a pseudo reg currently allocated to a suitable hard reg.
   Since they use reg_renumber, they are safe only once reg_renumber
   has been allocated, which happens in local-alloc.c.  */

#define REGNO_OK_FOR_INDEX_P(regno)	\
  ((regno) < FIRST_PSEUDO_REGISTER || reg_renumber[regno] >= 0)
#define REGNO_OK_FOR_BASE_P(regno)	\
  ((regno) < FIRST_PSEUDO_REGISTER || reg_renumber[regno] >= 0)

/* Maximum number of registers that can appear in a valid memory address.  */

#define MAX_REGS_PER_ADDRESS 2

/* 1 if X is an rtx for a constant that is a valid address.  */

#define CONSTANT_ADDRESS_P(X) legitimate_constant_address_p (X)

/* Nonzero if the constant value X is a legitimate general operand.
   It is given that X satisfies CONSTANT_P or is a CONST_DOUBLE.  */

#define LEGITIMATE_CONSTANT_P(X) legitimate_constant_p (X)

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
#define REG_OK_FOR_INDEX_P(X) 1

/* Nonzero if X is a hard reg that can be used as a base reg
   or if it is a pseudo reg.  */
#define REG_OK_FOR_BASE_P(X) 1

/* GO_IF_LEGITIMATE_ADDRESS recognizes an RTL expression
   that is a valid memory address for an instruction.  */
#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR) \
  { if (legitimate_address_p ((MODE), (X), 0)) goto ADDR; }

#else

/* Nonzero if X is a hard reg that can be used as an index.  */
#define REG_OK_FOR_INDEX_P(X) REGNO_OK_FOR_INDEX_P (REGNO (X))

/* Nonzero if X is a hard reg that can be used as a base reg.  */
#define REG_OK_FOR_BASE_P(X) REGNO_OK_FOR_BASE_P (REGNO (X))

/* GO_IF_LEGITIMATE_ADDRESS recognizes an RTL expression
   that is a valid memory address for an instruction.  */
#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR) \
  { if (legitimate_address_p ((MODE), (X), 1)) goto ADDR; }

#endif

/* Go to LABEL if ADDR (a legitimate address expression)
   has an effect that depends on the machine mode it is used for.  */
#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR, LABEL) \
  { if (vax_mode_dependent_address_p (ADDR)) goto LABEL; }

/* Specify the machine mode that this machine uses
   for the index in the tablejump instruction.  */
#define CASE_VECTOR_MODE HImode

/* Define as C expression which evaluates to nonzero if the tablejump
   instruction expects the table to contain offsets from the address of the
   table.
   Do not define this if the table should contain absolute addresses.  */
#define CASE_VECTOR_PC_RELATIVE 1

/* Indicate that jump tables go in the text section.  This is
   necessary when compiling PIC code.  */
#define JUMP_TABLES_IN_TEXT_SECTION 1

/* Define this as 1 if `char' should by default be signed; else as 0.  */
#define DEFAULT_SIGNED_CHAR 1

/* This flag, if defined, says the same insns that convert to a signed fixnum
   also convert validly to an unsigned one.  */
#define FIXUNS_TRUNC_LIKE_FIX_TRUNC

/* Max number of bytes we can move from memory to memory
   in one reasonably fast instruction.  */
#define MOVE_MAX 8

/* Nonzero if access to memory by bytes is slow and undesirable.  */
#define SLOW_BYTE_ACCESS 0

/* Define if shifts truncate the shift count
   which implies one can omit a sign-extension or zero-extension
   of a shift count.  */
/* #define SHIFT_COUNT_TRUNCATED */

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

/* This machine doesn't use IEEE floats.  */

#define TARGET_FLOAT_FORMAT VAX_FLOAT_FORMAT

/* Specify the cost of a branch insn; roughly the number of extra insns that
   should be added to avoid a branch.

   Branches are extremely cheap on the VAX while the shift insns often
   used to replace branches can be expensive.  */

#define BRANCH_COST 0

/* Tell final.c how to eliminate redundant test instructions.  */

/* Here we define machine-dependent flags and fields in cc_status
   (see `conditions.h').  No extra ones are needed for the VAX.  */

/* Store in cc_status the expressions
   that the condition codes will describe
   after execution of an instruction whose pattern is EXP.
   Do not alter them if the instruction would not alter the cc's.  */

#define NOTICE_UPDATE_CC(EXP, INSN)	\
  vax_notice_update_cc ((EXP), (INSN))

#define OUTPUT_JUMP(NORMAL, FLOAT, NO_OV)	\
  { if (cc_status.flags & CC_NO_OVERFLOW)	\
      return NO_OV;				\
    return NORMAL;				\
  }

/* Control the assembler format that we output.  */

/* A C string constant describing how to begin a comment in the target
   assembler language.  The compiler assumes that the comment will end at
   the end of the line.  */

#define ASM_COMMENT_START "#"

/* Output to assembler file text saying following lines
   may contain character constants, extra white space, comments, etc.  */

#define ASM_APP_ON "#APP\n"

/* Output to assembler file text saying following lines
   no longer contain unusual constructs.  */

#define ASM_APP_OFF "#NO_APP\n"

/* Output before read-only data.  */

#define TEXT_SECTION_ASM_OP "\t.text"

/* Output before writable data.  */

#define DATA_SECTION_ASM_OP "\t.data"

/* How to refer to registers in assembler output.
   This sequence is indexed by compiler's hard-register-number (see above).
   The register names will be prefixed by REGISTER_PREFIX, if any.  */

#define REGISTER_PREFIX ""
#define REGISTER_NAMES					\
  { "r0", "r1",  "r2",  "r3", "r4", "r5", "r6", "r7",	\
    "r8", "r9", "r10", "r11", "ap", "fp", "sp", "pc", }

/* This is BSD, so it wants DBX format.  */

#define DBX_DEBUGGING_INFO 1

/* Do not break .stabs pseudos into continuations.  */

#define DBX_CONTIN_LENGTH 0

/* This is the char to use for continuation (in case we need to turn
   continuation back on).  */

#define DBX_CONTIN_CHAR '?'

/* Don't use the `xsfoo;' construct in DBX output; this system
   doesn't support it.  */

#define DBX_NO_XREFS

/* Output the .stabs for a C `static' variable in the data section.  */
#define DBX_STATIC_STAB_DATA_SECTION

/* VAX specific: which type character is used for type double?  */

#define ASM_DOUBLE_CHAR (TARGET_G_FLOAT ? 'g' : 'd')

/* This is how to output a command to make the user-level label named NAME
   defined for reference from other files.  */

/* Globalizing directive for a label.  */
#define GLOBAL_ASM_OP ".globl "

/* The prefix to add to user-visible assembler symbols.  */

#define USER_LABEL_PREFIX "_"

/* This is how to store into the string LABEL
   the symbol_ref name of an internal numbered label where
   PREFIX is the class of label and NUM is the number within the class.
   This is suitable for output with `assemble_name'.  */

#define ASM_GENERATE_INTERNAL_LABEL(LABEL,PREFIX,NUM)	\
  sprintf (LABEL, "*%s%ld", PREFIX, (long)(NUM))

/* This is how to output an insn to push a register on the stack.
   It need not be very fast code.  */

#define ASM_OUTPUT_REG_PUSH(FILE,REGNO)  \
  fprintf (FILE, "\tpushl %s\n", reg_names[REGNO])

/* This is how to output an insn to pop a register from the stack.
   It need not be very fast code.  */

#define ASM_OUTPUT_REG_POP(FILE,REGNO)					\
  fprintf (FILE, "\tmovl (%s)+,%s\n", reg_names[STACK_POINTER_REGNUM],	\
	   reg_names[REGNO])

/* This is how to output an element of a case-vector that is absolute.
   (The VAX does not use such vectors,
   but we must define this macro anyway.)  */

#define ASM_OUTPUT_ADDR_VEC_ELT(FILE, VALUE)		\
  do							\
    {							\
      char label[256];					\
      ASM_GENERATE_INTERNAL_LABEL (label, "L", (VALUE));\
      fprintf (FILE, "\t.long ");			\
      assemble_name (FILE, label);			\
      fprintf (FILE, "\n");				\
    }							\
  while (0)

/* This is how to output an element of a case-vector that is relative.  */

#define ASM_OUTPUT_ADDR_DIFF_ELT(FILE, BODY, VALUE, REL)	\
  do								\
    {								\
      char label[256];						\
      ASM_GENERATE_INTERNAL_LABEL (label, "L", (VALUE));	\
      fprintf (FILE, "\t.word ");				\
      assemble_name (FILE, label);				\
      ASM_GENERATE_INTERNAL_LABEL (label, "L", (REL));		\
      fprintf (FILE, "-");					\
      assemble_name (FILE, label);				\
      fprintf (FILE, "\n");					\
    }								\
  while (0)

/* This is how to output an assembler line
   that says to advance the location counter
   to a multiple of 2**LOG bytes.  */

#define ASM_OUTPUT_ALIGN(FILE,LOG)  \
  fprintf (FILE, "\t.align %d\n", (LOG))

/* This is how to output an assembler line
   that says to advance the location counter by SIZE bytes.  */

#define ASM_OUTPUT_SKIP(FILE,SIZE)  \
  fprintf (FILE, "\t.space %u\n", (int)(SIZE))

/* This says how to output an assembler line
   to define a global common symbol.  */

#define ASM_OUTPUT_COMMON(FILE, NAME, SIZE, ROUNDED)	\
  ( fputs (".comm ", (FILE)),				\
    assemble_name ((FILE), (NAME)),			\
    fprintf ((FILE), ",%u\n", (int)(ROUNDED)))

/* This says how to output an assembler line
   to define a local common symbol.  */

#define ASM_OUTPUT_LOCAL(FILE, NAME, SIZE, ROUNDED)	\
  ( fputs (".lcomm ", (FILE)),				\
    assemble_name ((FILE), (NAME)),			\
    fprintf ((FILE), ",%u\n", (int)(ROUNDED)))

/* Store in OUTPUT a string (made with alloca) containing
   an assembler-name for a local static variable named NAME.
   LABELNO is an integer which is different for each call.  */

#define ASM_FORMAT_PRIVATE_NAME(OUTPUT, NAME, LABELNO)	\
  ( (OUTPUT) = (char *) alloca (strlen ((NAME)) + 10),	\
    sprintf ((OUTPUT), "%s.%d", (NAME), (LABELNO)))

/* Print an instruction operand X on file FILE.
   CODE is the code from the %-spec that requested printing this operand;
   if `%z3' was used to print operand 3, then CODE is 'z'.

VAX operand formatting codes:

 letter	   print
   C	reverse branch condition
   D	64-bit immediate operand
   B	the low 8 bits of the complement of a constant operand
   H	the low 16 bits of the complement of a constant operand
   M	a mask for the N highest bits of a word
   N	the complement of a constant integer operand
   P	constant operand plus 1
   R	32 - constant operand
   b	the low 8 bits of a negated constant operand
   h	the low 16 bits of a negated constant operand
   #	'd' or 'g' depending on whether dfloat or gfloat is used
   |	register prefix  */

/* The purpose of D is to get around a quirk or bug in VAX assembler
   whereby -1 in a 64-bit immediate operand means 0x00000000ffffffff,
   which is not a 64-bit minus one.  As a workaround, we output negative
   values in hex.  */
#if HOST_BITS_PER_WIDE_INT == 64
#  define NEG_HWI_PRINT_HEX16 HOST_WIDE_INT_PRINT_HEX
#else
#  define NEG_HWI_PRINT_HEX16 "0xffffffff%08lx"
#endif

#define PRINT_OPERAND_PUNCT_VALID_P(CODE)				\
  ((CODE) == '#' || (CODE) == '|')

#define PRINT_OPERAND(FILE, X, CODE)					\
{ if (CODE == '#') fputc (ASM_DOUBLE_CHAR, FILE);			\
  else if (CODE == '|')							\
    fputs (REGISTER_PREFIX, FILE);					\
  else if (CODE == 'C')							\
    fputs (rev_cond_name (X), FILE);					\
  else if (CODE == 'D' && CONST_INT_P (X) && INTVAL (X) < 0)		\
    fprintf (FILE, "$" NEG_HWI_PRINT_HEX16, INTVAL (X));		\
  else if (CODE == 'P' && CONST_INT_P (X))				\
    fprintf (FILE, "$" HOST_WIDE_INT_PRINT_DEC, INTVAL (X) + 1);	\
  else if (CODE == 'N' && CONST_INT_P (X))				\
    fprintf (FILE, "$" HOST_WIDE_INT_PRINT_DEC, ~ INTVAL (X));		\
  /* rotl instruction cannot deal with negative arguments.  */		\
  else if (CODE == 'R' && CONST_INT_P (X))				\
    fprintf (FILE, "$" HOST_WIDE_INT_PRINT_DEC, 32 - INTVAL (X));	\
  else if (CODE == 'H' && CONST_INT_P (X))				\
    fprintf (FILE, "$%d", (int) (0xffff & ~ INTVAL (X)));		\
  else if (CODE == 'h' && CONST_INT_P (X))				\
    fprintf (FILE, "$%d", (short) - INTVAL (x));			\
  else if (CODE == 'B' && CONST_INT_P (X))				\
    fprintf (FILE, "$%d", (int) (0xff & ~ INTVAL (X)));			\
  else if (CODE == 'b' && CONST_INT_P (X))				\
    fprintf (FILE, "$%d", (int) (0xff & - INTVAL (X)));			\
  else if (CODE == 'M' && CONST_INT_P (X))				\
    fprintf (FILE, "$%d", ~((1 << INTVAL (x)) - 1));			\
  else if (REG_P (X))							\
    fprintf (FILE, "%s", reg_names[REGNO (X)]);				\
  else if (MEM_P (X))							\
    output_address (XEXP (X, 0));					\
  else if (GET_CODE (X) == CONST_DOUBLE && GET_MODE (X) == SFmode)	\
    { char dstr[30];							\
      real_to_decimal (dstr, CONST_DOUBLE_REAL_VALUE (X),		\
		       sizeof (dstr), 0, 1);				\
      fprintf (FILE, "$0f%s", dstr); }					\
  else if (GET_CODE (X) == CONST_DOUBLE && GET_MODE (X) == DFmode)	\
    { char dstr[30];							\
      real_to_decimal (dstr, CONST_DOUBLE_REAL_VALUE (X),		\
		       sizeof (dstr), 0, 1);				\
      fprintf (FILE, "$0%c%s", ASM_DOUBLE_CHAR, dstr); }		\
  else { putc ('$', FILE); output_addr_const (FILE, X); }}

/* Print a memory operand whose address is X, on file FILE.
   This uses a function in output-vax.c.  */

#define PRINT_OPERAND_ADDRESS(FILE, ADDR)  \
  print_operand_address (FILE, ADDR)

/* This is a blatent lie.  However, it's good enough, since we don't
   actually have any code whatsoever for which this isn't overridden
   by the proper FDE definition.  */
#define INCOMING_RETURN_ADDR_RTX gen_rtx_REG (Pmode, PC_REGNUM)

