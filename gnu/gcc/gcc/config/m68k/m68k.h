/* Definitions of target machine for GCC for Motorola 680x0/ColdFire.
   Copyright (C) 1987, 1988, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.

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

/* We need to have MOTOROLA always defined (either 0 or 1) because we use
   if-statements and ?: on it.  This way we have compile-time error checking
   for both the MOTOROLA and MIT code paths.  We do rely on the host compiler
   to optimize away all constant tests.  */
#ifdef MOTOROLA
# undef MOTOROLA
# define MOTOROLA 1  /* Use the Motorola assembly syntax.  */
# define TARGET_VERSION fprintf (stderr, " (68k, Motorola syntax)")
#else
# define TARGET_VERSION fprintf (stderr, " (68k, MIT syntax)")
# define MOTOROLA 0  /* Use the MIT assembly syntax.  */
#endif

/* Note that some other tm.h files include this one and then override
   many of the definitions that relate to assembler syntax.  */

#define TARGET_CPU_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define ("__m68k__");		\
      builtin_define_std ("mc68000");		\
      if (TARGET_68040_ONLY)			\
	{					\
	  if (TARGET_68060)			\
	    builtin_define_std ("mc68060");	\
	  else					\
	    builtin_define_std ("mc68040");	\
	}					\
      else if (TARGET_68060) /* -m68020-60 */	\
	{					\
	  builtin_define_std ("mc68060");	\
	  builtin_define_std ("mc68040");	\
	  builtin_define_std ("mc68030");	\
	  builtin_define_std ("mc68020");	\
	}					\
      else if (TARGET_68040) /* -m68020-40 */	\
	{					\
	  builtin_define_std ("mc68040");	\
	  builtin_define_std ("mc68030");	\
	  builtin_define_std ("mc68020");	\
	}					\
      else if (TARGET_68030)			\
	builtin_define_std ("mc68030");		\
      else if (TARGET_68020)			\
	builtin_define_std ("mc68020");		\
      if (TARGET_68881)				\
	builtin_define ("__HAVE_68881__");	\
      if (TARGET_CPU32)				\
	{					\
	  builtin_define_std ("mc68332");	\
	  builtin_define_std ("mcpu32");	\
	}					\
      if (TARGET_COLDFIRE)			\
	builtin_define ("__mcoldfire__");	\
      if (TARGET_5200)				\
	builtin_define ("__mcf5200__");		\
      if (TARGET_528x)				\
	{					\
	  builtin_define ("__mcf528x__");	\
	  builtin_define ("__mcf5200__");	\
	}					\
      if (TARGET_CFV3)				\
	{					\
	  builtin_define ("__mcf5300__");	\
	  builtin_define ("__mcf5307__");	\
	}					\
      if (TARGET_CFV4)				\
	{					\
	  builtin_define ("__mcf5400__");	\
	  builtin_define ("__mcf5407__");	\
	}					\
      if (TARGET_CFV4E)				\
	{					\
	  builtin_define ("__mcfv4e__");	\
	}					\
      if (TARGET_CF_HWDIV)			\
	builtin_define ("__mcfhwdiv__");	\
      builtin_assert ("cpu=m68k");		\
      builtin_assert ("machine=m68k");		\
    }						\
  while (0)

/* Classify the groups of pseudo-ops used to assemble QI, HI and SI
   quantities.  */
#define INT_OP_STANDARD	0	/* .byte, .short, .long */
#define INT_OP_DOT_WORD	1	/* .byte, .word, .long */
#define INT_OP_NO_DOT   2	/* byte, short, long */
#define INT_OP_DC	3	/* dc.b, dc.w, dc.l */

/* Set the default.  */
#define INT_OP_GROUP INT_OP_DOT_WORD

/* Compile for a CPU32.  A 68020 without bitfields is a good
   heuristic for a CPU32.  */
#define TARGET_CPU32	(TARGET_68020 && !TARGET_BITFIELD)

/* Is the target a ColdFire?  */
#define MASK_COLDFIRE \
  (MASK_5200 | MASK_528x | MASK_CFV3 | MASK_CFV4 | MASK_CFV4E)
#define TARGET_COLDFIRE	((target_flags & MASK_COLDFIRE) != 0)

#define TARGET_COLDFIRE_FPU	TARGET_CFV4E

#define TARGET_HARD_FLOAT	(TARGET_68881 || TARGET_COLDFIRE_FPU)
/* Size (in bytes) of FPU registers.  */
#define TARGET_FP_REG_SIZE	(TARGET_COLDFIRE ? 8 : 12)


#define OVERRIDE_OPTIONS   override_options()

/* These are meant to be redefined in the host dependent files */
#define SUBTARGET_OVERRIDE_OPTIONS

/* target machine storage layout */

#define LONG_DOUBLE_TYPE_SIZE 80

/* Set the value of FLT_EVAL_METHOD in float.h.  When using 68040 fp
   instructions, we get proper intermediate rounding, otherwise we
   get extended precision results.  */
#define TARGET_FLT_EVAL_METHOD ((TARGET_68040_ONLY || ! TARGET_68881) ? 0 : 2)

#define BITS_BIG_ENDIAN 1
#define BYTES_BIG_ENDIAN 1
#define WORDS_BIG_ENDIAN 1

#define UNITS_PER_WORD 4

#define PARM_BOUNDARY (TARGET_SHORT ? 16 : 32)
#define STACK_BOUNDARY 16
#define FUNCTION_BOUNDARY 16
#define EMPTY_FIELD_BOUNDARY 16

/* No data type wants to be aligned rounder than this.
   Most published ABIs say that ints should be aligned on 16 bit
   boundaries, but CPUs with 32-bit busses get better performance
   aligned on 32-bit boundaries.  ColdFires without a misalignment
   module require 32-bit alignment.  */
#define BIGGEST_ALIGNMENT (TARGET_ALIGN_INT ? 32 : 16)

#define STRICT_ALIGNMENT (TARGET_STRICT_ALIGNMENT)

#define INT_TYPE_SIZE (TARGET_SHORT ? 16 : 32)

/* Define these to avoid dependence on meaning of `int'.  */
#define WCHAR_TYPE "long int"
#define WCHAR_TYPE_SIZE 32

/* Maximum number of library IDs we permit with -mid-shared-library.  */
#define MAX_LIBRARY_ID 255


/* Standard register usage.  */

/* For the m68k, we give the data registers numbers 0-7,
   the address registers numbers 010-017 (8-15),
   and the 68881 floating point registers numbers 020-027 (16-24).
   We also have a fake `arg-pointer' register 030 (25) used for
   register elimination.  */
#define FIRST_PSEUDO_REGISTER 25

/* All m68k targets (except AmigaOS) use %a5 as the PIC register  */
#define PIC_OFFSET_TABLE_REGNUM (flag_pic ? 13 : INVALID_REGNUM)

/* 1 for registers that have pervasive standard uses
   and are not available for the register allocator.
   On the m68k, only the stack pointer is such.
   Our fake arg-pointer is obviously fixed as well.  */
#define FIXED_REGISTERS        \
 {/* Data registers.  */       \
  0, 0, 0, 0, 0, 0, 0, 0,      \
                               \
  /* Address registers.  */    \
  0, 0, 0, 0, 0, 0, 0, 1,      \
                               \
  /* Floating point registers  \
     (if available).  */       \
  0, 0, 0, 0, 0, 0, 0, 0,      \
                               \
  /* Arg pointer.  */          \
  1 }

/* 1 for registers not available across function calls.
   These must include the FIXED_REGISTERS and also any
   registers that can be used without being saved.
   The latter must include the registers where values are returned
   and the register where structure-value addresses are passed.
   Aside from that, you can include as many other registers as you like.  */
#define CALL_USED_REGISTERS     \
 {/* Data registers.  */        \
  1, 1, 0, 0, 0, 0, 0, 0,       \
                                \
  /* Address registers.  */     \
  1, 1, 0, 0, 0, 0, 0, 1,       \
                                \
  /* Floating point registers   \
     (if available).  */        \
  1, 1, 0, 0, 0, 0, 0, 0,       \
                                \
  /* Arg pointer.  */           \
  1 }

#define REG_ALLOC_ORDER		\
{ /* d0/d1/a0/a1 */		\
  0, 1, 8, 9,			\
  /* d2-d7 */			\
  2, 3, 4, 5, 6, 7,		\
  /* a2-a7/arg */		\
  10, 11, 12, 13, 14, 15, 24,	\
  /* fp0-fp7 */			\
  16, 17, 18, 19, 20, 21, 22, 23\
}


/* Make sure everything's fine if we *don't* have a given processor.
   This assumes that putting a register in fixed_regs will keep the
   compiler's mitts completely off it.  We don't bother to zero it out
   of register classes.  */
#define CONDITIONAL_REGISTER_USAGE				\
{								\
  int i;							\
  HARD_REG_SET x;						\
  if (!TARGET_HARD_FLOAT)					\
    {								\
      COPY_HARD_REG_SET (x, reg_class_contents[(int)FP_REGS]);	\
      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)		\
        if (TEST_HARD_REG_BIT (x, i))				\
	  fixed_regs[i] = call_used_regs[i] = 1;		\
    }								\
  if (PIC_OFFSET_TABLE_REGNUM != INVALID_REGNUM)		\
    fixed_regs[PIC_OFFSET_TABLE_REGNUM]				\
      = call_used_regs[PIC_OFFSET_TABLE_REGNUM] = 1;		\
}

/* On the m68k, ordinary registers hold 32 bits worth;
   for the 68881 registers, a single register is always enough for
   anything that can be stored in them at all.  */
#define HARD_REGNO_NREGS(REGNO, MODE)   \
  ((REGNO) >= 16 ? GET_MODE_NUNITS (MODE)	\
   : ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD))

/* A C expression that is nonzero if hard register NEW_REG can be
   considered for use as a rename register for OLD_REG register.  */

#define HARD_REGNO_RENAME_OK(OLD_REG, NEW_REG) \
  m68k_hard_regno_rename_ok (OLD_REG, NEW_REG)

/* Value is true if hard register REGNO can hold a value of machine-mode MODE.
   On the 68000, the cpu registers can hold any mode except bytes in
   address registers, the 68881 registers can hold only SFmode or DFmode.  */

#define HARD_REGNO_MODE_OK(REGNO, MODE) \
  m68k_regno_mode_ok ((REGNO), (MODE))

#define MODES_TIEABLE_P(MODE1, MODE2)			\
  (! TARGET_HARD_FLOAT					\
   || ((GET_MODE_CLASS (MODE1) == MODE_FLOAT		\
	|| GET_MODE_CLASS (MODE1) == MODE_COMPLEX_FLOAT)	\
       == (GET_MODE_CLASS (MODE2) == MODE_FLOAT		\
	   || GET_MODE_CLASS (MODE2) == MODE_COMPLEX_FLOAT)))

/* Specify the registers used for certain standard purposes.
   The values of these macros are register numbers.  */

#define STACK_POINTER_REGNUM 15

/* Most m68k targets use %a6 as a frame pointer.  The AmigaOS
   ABI uses %a6 for shared library calls, therefore the frame
   pointer is shifted to %a5 on this target.  */
#define FRAME_POINTER_REGNUM 14

#define FRAME_POINTER_REQUIRED 0

/* Base register for access to arguments of the function.
 * This isn't a hardware register. It will be eliminated to the
 * stack pointer or frame pointer.
 */
#define ARG_POINTER_REGNUM 24

#define STATIC_CHAIN_REGNUM 8

/* Register in which address to store a structure value
   is passed to a function.  */
#define M68K_STRUCT_VALUE_REGNUM 9



/* The m68k has three kinds of registers, so eight classes would be
   a complete set.  One of them is not needed.  */
enum reg_class {
  NO_REGS, DATA_REGS,
  ADDR_REGS, FP_REGS,
  GENERAL_REGS, DATA_OR_FP_REGS,
  ADDR_OR_FP_REGS, ALL_REGS,
  LIM_REG_CLASSES };

#define N_REG_CLASSES (int) LIM_REG_CLASSES

#define REG_CLASS_NAMES \
 { "NO_REGS", "DATA_REGS",              \
   "ADDR_REGS", "FP_REGS",              \
   "GENERAL_REGS", "DATA_OR_FP_REGS",   \
   "ADDR_OR_FP_REGS", "ALL_REGS" }

#define REG_CLASS_CONTENTS \
{					\
  {0x00000000},  /* NO_REGS */		\
  {0x000000ff},  /* DATA_REGS */	\
  {0x0100ff00},  /* ADDR_REGS */	\
  {0x00ff0000},  /* FP_REGS */		\
  {0x0100ffff},  /* GENERAL_REGS */	\
  {0x00ff00ff},  /* DATA_OR_FP_REGS */	\
  {0x01ffff00},  /* ADDR_OR_FP_REGS */	\
  {0x01ffffff},  /* ALL_REGS */		\
}

extern enum reg_class regno_reg_class[];
#define REGNO_REG_CLASS(REGNO) (regno_reg_class[(REGNO)])
#define INDEX_REG_CLASS GENERAL_REGS
#define BASE_REG_CLASS ADDR_REGS

/* We do a trick here to modify the effective constraints on the
   machine description; we zorch the constraint letters that aren't
   appropriate for a specific target.  This allows us to guarantee
   that a specific kind of register will not be used for a given target
   without fiddling with the register classes above.  */
#define REG_CLASS_FROM_LETTER(C) \
  ((C) == 'a' ? ADDR_REGS :			\
   ((C) == 'd' ? DATA_REGS :			\
    ((C) == 'f' ? (TARGET_HARD_FLOAT ?		\
		   FP_REGS : NO_REGS) :		\
     NO_REGS)))

/* For the m68k, `I' is used for the range 1 to 8
   allowed as immediate shift counts and in addq.
   `J' is used for the range of signed numbers that fit in 16 bits.
   `K' is for numbers that moveq can't handle.
   `L' is for range -8 to -1, range of values that can be added with subq.
   `M' is for numbers that moveq+notb can't handle.
   'N' is for range 24 to 31, rotatert:SI 8 to 1 expressed as rotate.
   'O' is for 16 (for rotate using swap).
   'P' is for range 8 to 15, rotatert:HI 8 to 1 expressed as rotate.  */
#define CONST_OK_FOR_LETTER_P(VALUE, C) \
  ((C) == 'I' ? (VALUE) > 0 && (VALUE) <= 8 : \
   (C) == 'J' ? (VALUE) >= -0x8000 && (VALUE) <= 0x7FFF : \
   (C) == 'K' ? (VALUE) < -0x80 || (VALUE) >= 0x80 : \
   (C) == 'L' ? (VALUE) < 0 && (VALUE) >= -8 : \
   (C) == 'M' ? (VALUE) < -0x100 || (VALUE) >= 0x100 : \
   (C) == 'N' ? (VALUE) >= 24 && (VALUE) <= 31 : \
   (C) == 'O' ? (VALUE) == 16 : \
   (C) == 'P' ? (VALUE) >= 8 && (VALUE) <= 15 : 0)

/* "G" defines all of the floating constants that are *NOT* 68881
   constants.  This is so 68881 constants get reloaded and the
   fpmovecr is used.  */
#define CONST_DOUBLE_OK_FOR_LETTER_P(VALUE, C)  \
  ((C) == 'G' ? ! (TARGET_68881 && standard_68881_constant_p (VALUE)) : 0 )

/* `Q' means address register indirect addressing mode.
   `S' is for operands that satisfy 'm' when -mpcrel is in effect.
   `T' is for operands that satisfy 's' when -mpcrel is not in effect.
   `U' is for register offset addressing.  */
#define EXTRA_CONSTRAINT(OP,CODE)			\
  (((CODE) == 'S')					\
   ? (TARGET_PCREL					\
      && GET_CODE (OP) == MEM				\
      && (GET_CODE (XEXP (OP, 0)) == SYMBOL_REF		\
	  || GET_CODE (XEXP (OP, 0)) == LABEL_REF	\
	  || GET_CODE (XEXP (OP, 0)) == CONST))		\
   : 							\
  (((CODE) == 'T')					\
   ? ( !TARGET_PCREL 					\
      && (GET_CODE (OP) == SYMBOL_REF			\
	  || GET_CODE (OP) == LABEL_REF			\
	  || GET_CODE (OP) == CONST))			\
   :							\
  (((CODE) == 'Q')					\
   ? (GET_CODE (OP) == MEM 				\
      && GET_CODE (XEXP (OP, 0)) == REG)		\
   :							\
  (((CODE) == 'U')					\
   ? (GET_CODE (OP) == MEM 				\
      && GET_CODE (XEXP (OP, 0)) == PLUS		\
      && GET_CODE (XEXP (XEXP (OP, 0), 0)) == REG	\
      && GET_CODE (XEXP (XEXP (OP, 0), 1)) == CONST_INT) \
   :							\
   0))))

/* On the m68k, use a data reg if possible when the
   value is a constant in the range where moveq could be used
   and we ensure that QImodes are reloaded into data regs.  */
#define PREFERRED_RELOAD_CLASS(X,CLASS)  \
  ((GET_CODE (X) == CONST_INT			\
    && (unsigned) (INTVAL (X) + 0x80) < 0x100	\
    && (CLASS) != ADDR_REGS)			\
   ? DATA_REGS					\
   : (GET_MODE (X) == QImode && (CLASS) != ADDR_REGS) \
   ? DATA_REGS					\
   : (GET_CODE (X) == CONST_DOUBLE					\
      && GET_MODE_CLASS (GET_MODE (X)) == MODE_FLOAT)			\
   ? (TARGET_HARD_FLOAT && (CLASS == FP_REGS || CLASS == DATA_OR_FP_REGS) \
      ? FP_REGS : NO_REGS)						\
   : (TARGET_PCREL				\
      && (GET_CODE (X) == SYMBOL_REF || GET_CODE (X) == CONST \
	  || GET_CODE (X) == LABEL_REF))	\
   ? ADDR_REGS					\
   : (CLASS))

/* Force QImode output reloads from subregs to be allocated to data regs,
   since QImode stores from address regs are not supported.  We make the
   assumption that if the class is not ADDR_REGS, then it must be a superset
   of DATA_REGS.  */
#define LIMIT_RELOAD_CLASS(MODE, CLASS) \
  (((MODE) == QImode && (CLASS) != ADDR_REGS)	\
   ? DATA_REGS					\
   : (CLASS))

/* On the m68k, this is the size of MODE in words,
   except in the FP regs, where a single reg is always enough.  */
#define CLASS_MAX_NREGS(CLASS, MODE)	\
 ((CLASS) == FP_REGS ? 1 \
  : ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD))

/* Moves between fp regs and other regs are two insns.  */
#define REGISTER_MOVE_COST(MODE, CLASS1, CLASS2)	\
  (((CLASS1) == FP_REGS && (CLASS2) != FP_REGS)	        \
    || ((CLASS2) == FP_REGS && (CLASS1) != FP_REGS)	\
    ? 4 : 2)

/* Stack layout; function entry, exit and calling.  */

#define STACK_GROWS_DOWNWARD
#define FRAME_GROWS_DOWNWARD 1
#define STARTING_FRAME_OFFSET 0

/* On the 680x0, sp@- in a byte insn really pushes a word.
   On the ColdFire, sp@- in a byte insn pushes just a byte.  */
#define PUSH_ROUNDING(BYTES) (TARGET_COLDFIRE ? BYTES : ((BYTES) + 1) & ~1)

#define FIRST_PARM_OFFSET(FNDECL) 8

/* On the 68000, the RTS insn cannot pop anything.
   On the 68010, the RTD insn may be used to pop them if the number
     of args is fixed, but if the number is variable then the caller
     must pop them all.  RTD can't be used for library calls now
     because the library is compiled with the Unix compiler.
   Use of RTD is a selectable option, since it is incompatible with
   standard Unix calling sequences.  If the option is not selected,
   the caller must always pop the args.  */
#define RETURN_POPS_ARGS(FUNDECL,FUNTYPE,SIZE)   \
  ((TARGET_RTD && (!(FUNDECL) || TREE_CODE (FUNDECL) != IDENTIFIER_NODE)	\
    && (TYPE_ARG_TYPES (FUNTYPE) == 0				\
	|| (TREE_VALUE (tree_last (TYPE_ARG_TYPES (FUNTYPE)))	\
	    == void_type_node)))				\
   ? (SIZE) : 0)

/* On the m68k the return value is always in D0.  */
#define FUNCTION_VALUE(VALTYPE, FUNC)  \
  gen_rtx_REG (TYPE_MODE (VALTYPE), 0)

/* On the m68k the return value is always in D0.  */
#define LIBCALL_VALUE(MODE)  gen_rtx_REG (MODE, 0)

/* On the m68k, D0 is the only register used.  */
#define FUNCTION_VALUE_REGNO_P(N) ((N) == 0)

/* Define this to be true when FUNCTION_VALUE_REGNO_P is true for
   more than one register.
   XXX This macro is m68k specific and used only for m68kemb.h.  */
#define NEEDS_UNTYPED_CALL 0

#define PCC_STATIC_STRUCT_RETURN

/* On the m68k, all arguments are usually pushed on the stack.  */
#define FUNCTION_ARG_REGNO_P(N) 0

/* On the m68k, this is a single integer, which is a number of bytes
   of arguments scanned so far.  */
#define CUMULATIVE_ARGS int

/* On the m68k, the offset starts at 0.  */
#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, INDIRECT, N_NAMED_ARGS) \
 ((CUM) = 0)

#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED)	\
 ((CUM) += ((MODE) != BLKmode			\
	    ? (GET_MODE_SIZE (MODE) + 3) & ~3	\
	    : (int_size_in_bytes (TYPE) + 3) & ~3))

/* On the m68k all args are always pushed.  */
#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED) 0

#define FUNCTION_PROFILER(FILE, LABELNO)  \
  asm_fprintf (FILE, "\tlea %LLP%d,%Ra0\n\tjsr mcount\n", (LABELNO))

#define EXIT_IGNORE_STACK 1

/* Determine if the epilogue should be output as RTL.
   You should override this if you define FUNCTION_EXTRA_EPILOGUE.

   XXX This macro is m68k-specific and only used in m68k.md.  */
#define USE_RETURN_INSN use_return_insn ()

/* Output assembler code for a block containing the constant parts
   of a trampoline, leaving space for the variable parts.

   On the m68k, the trampoline looks like this:
     movl #STATIC,a0
     jmp  FUNCTION

   WARNING: Targets that may run on 68040+ cpus must arrange for
   the instruction cache to be flushed.  Previous incarnations of
   the m68k trampoline code attempted to get around this by either
   using an out-of-line transfer function or pc-relative data, but
   the fact remains that the code to jump to the transfer function
   or the code to load the pc-relative data needs to be flushed
   just as much as the "variable" portion of the trampoline.
   Recognizing that a cache flush is going to be required anyway,
   dispense with such notions and build a smaller trampoline.

   Since more instructions are required to move a template into
   place than to create it on the spot, don't use a template.  */

#define TRAMPOLINE_SIZE 12
#define TRAMPOLINE_ALIGNMENT 16

/* Targets redefine this to invoke code to either flush the cache,
   or enable stack execution (or both).  */
#ifndef FINALIZE_TRAMPOLINE
#define FINALIZE_TRAMPOLINE(TRAMP)
#endif

/* We generate a two-instructions program at address TRAMP :
	movea.l &CXT,%a0
	jmp FNADDR  */
#define INITIALIZE_TRAMPOLINE(TRAMP, FNADDR, CXT)			\
{									\
  emit_move_insn (gen_rtx_MEM (HImode, TRAMP), GEN_INT(0x207C));	\
  emit_move_insn (gen_rtx_MEM (SImode, plus_constant (TRAMP, 2)), CXT); \
  emit_move_insn (gen_rtx_MEM (HImode, plus_constant (TRAMP, 6)),	\
		  GEN_INT(0x4EF9));					\
  emit_move_insn (gen_rtx_MEM (SImode, plus_constant (TRAMP, 8)), FNADDR); \
  FINALIZE_TRAMPOLINE(TRAMP);						\
}

/* This is the library routine that is used to transfer control from the
   trampoline to the actual nested function.  It is defined for backward
   compatibility, for linking with object code that used the old trampoline
   definition.

   A colon is used with no explicit operands to cause the template string
   to be scanned for %-constructs.

   The function name __transfer_from_trampoline is not actually used.
   The function definition just permits use of "asm with operands"
   (though the operand list is empty).  */
#define TRANSFER_FROM_TRAMPOLINE				\
void								\
__transfer_from_trampoline ()					\
{								\
  register char *a0 asm ("%a0");				\
  asm (GLOBAL_ASM_OP "___trampoline");				\
  asm ("___trampoline:");					\
  asm volatile ("move%.l %0,%@" : : "m" (a0[22]));		\
  asm volatile ("move%.l %1,%0" : "=a" (a0) : "m" (a0[18]));	\
  asm ("rts":);							\
}

/* There are two registers that can always be eliminated on the m68k.
   The frame pointer and the arg pointer can be replaced by either the
   hard frame pointer or to the stack pointer, depending upon the
   circumstances.  The hard frame pointer is not used before reload and
   so it is not eligible for elimination.  */
#define ELIMINABLE_REGS					\
{{ ARG_POINTER_REGNUM, STACK_POINTER_REGNUM },		\
 { ARG_POINTER_REGNUM, FRAME_POINTER_REGNUM },		\
 { FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM }}

#define CAN_ELIMINATE(FROM, TO) \
  ((TO) == STACK_POINTER_REGNUM ? ! frame_pointer_needed : 1)

#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET)			\
  (OFFSET) = m68k_initial_elimination_offset(FROM, TO)

/* Addressing modes, and classification of registers for them.  */

#define HAVE_POST_INCREMENT 1
#define HAVE_PRE_DECREMENT 1

/* Macros to check register numbers against specific register classes.  */

#define REGNO_OK_FOR_INDEX_P(REGNO) \
((REGNO) < 16 || (unsigned) reg_renumber[REGNO] < 16)
#define REGNO_OK_FOR_BASE_P(REGNO) \
(((REGNO) ^ 010) < 8 || (unsigned) (reg_renumber[REGNO] ^ 010) < 8)
#define REGNO_OK_FOR_DATA_P(REGNO) \
((REGNO) < 8 || (unsigned) reg_renumber[REGNO] < 8)
#define REGNO_OK_FOR_FP_P(REGNO) \
(((REGNO) ^ 020) < 8 || (unsigned) (reg_renumber[REGNO] ^ 020) < 8)

/* Now macros that check whether X is a register and also,
   strictly, whether it is in a specified class.

   These macros are specific to the m68k, and may be used only
   in code for printing assembler insns and in conditions for
   define_optimization.  */

/* 1 if X is a data register.  */
#define DATA_REG_P(X) (REG_P (X) && REGNO_OK_FOR_DATA_P (REGNO (X)))

/* 1 if X is an fp register.  */
#define FP_REG_P(X) (REG_P (X) && REGNO_OK_FOR_FP_P (REGNO (X)))

/* 1 if X is an address register  */
#define ADDRESS_REG_P(X) (REG_P (X) && REGNO_OK_FOR_BASE_P (REGNO (X)))


#define MAX_REGS_PER_ADDRESS 2

#define CONSTANT_ADDRESS_P(X)   \
  (GET_CODE (X) == LABEL_REF || GET_CODE (X) == SYMBOL_REF		\
   || GET_CODE (X) == CONST_INT || GET_CODE (X) == CONST		\
   || GET_CODE (X) == HIGH)

/* Nonzero if the constant value X is a legitimate general operand.
   It is given that X satisfies CONSTANT_P or is a CONST_DOUBLE.  */
#define LEGITIMATE_CONSTANT_P(X) (GET_MODE (X) != XFmode)

#ifndef REG_OK_STRICT
#define PCREL_GENERAL_OPERAND_OK 0
#else
#define PCREL_GENERAL_OPERAND_OK (TARGET_PCREL)
#endif

#define LEGITIMATE_PIC_OPERAND_P(X)	\
  (! symbolic_operand (X, VOIDmode)				\
   || (GET_CODE (X) == SYMBOL_REF && SYMBOL_REF_FLAG (X))	\
   || PCREL_GENERAL_OPERAND_OK)

#ifndef REG_OK_STRICT

/* Nonzero if X is a hard reg that can be used as an index
   or if it is a pseudo reg.  */
#define REG_OK_FOR_INDEX_P(X) ((REGNO (X) ^ 020) >= 8)
/* Nonzero if X is a hard reg that can be used as a base reg
   or if it is a pseudo reg.  */
#define REG_OK_FOR_BASE_P(X) ((REGNO (X) & ~027) != 0)

#else

/* Nonzero if X is a hard reg that can be used as an index.  */
#define REG_OK_FOR_INDEX_P(X) REGNO_OK_FOR_INDEX_P (REGNO (X))
/* Nonzero if X is a hard reg that can be used as a base reg.  */
#define REG_OK_FOR_BASE_P(X) REGNO_OK_FOR_BASE_P (REGNO (X))

#endif

/* GO_IF_LEGITIMATE_ADDRESS recognizes an RTL expression
   that is a valid memory address for an instruction.
   The MODE argument is the machine mode for the MEM expression
   that wants to use this address.

   When generating PIC, an address involving a SYMBOL_REF is legitimate
   if and only if it is the sum of pic_offset_table_rtx and the SYMBOL_REF.
   We use LEGITIMATE_PIC_OPERAND_P to throw out the illegitimate addresses,
   and we explicitly check for the sum of pic_offset_table_rtx and a SYMBOL_REF.

   Likewise for a LABEL_REF when generating PIC.

   The other macros defined here are used only in GO_IF_LEGITIMATE_ADDRESS.  */

/* Allow SUBREG everywhere we allow REG.  This results in better code.  It
   also makes function inlining work when inline functions are called with
   arguments that are SUBREGs.  */

#define LEGITIMATE_BASE_REG_P(X)   \
  ((GET_CODE (X) == REG && REG_OK_FOR_BASE_P (X))	\
   || (GET_CODE (X) == SUBREG				\
       && GET_CODE (SUBREG_REG (X)) == REG		\
       && REG_OK_FOR_BASE_P (SUBREG_REG (X))))

#define INDIRECTABLE_1_ADDRESS_P(X)  \
  ((CONSTANT_ADDRESS_P (X) && (!flag_pic || LEGITIMATE_PIC_OPERAND_P (X))) \
   || LEGITIMATE_BASE_REG_P (X)						\
   || ((GET_CODE (X) == PRE_DEC || GET_CODE (X) == POST_INC)		\
       && LEGITIMATE_BASE_REG_P (XEXP (X, 0)))				\
   || (GET_CODE (X) == PLUS						\
       && LEGITIMATE_BASE_REG_P (XEXP (X, 0))				\
       && GET_CODE (XEXP (X, 1)) == CONST_INT				\
       && (TARGET_68020							\
	   || ((unsigned) INTVAL (XEXP (X, 1)) + 0x8000) < 0x10000))	\
   || (GET_CODE (X) == PLUS && XEXP (X, 0) == pic_offset_table_rtx 	\
       && flag_pic && GET_CODE (XEXP (X, 1)) == SYMBOL_REF)		\
   || (GET_CODE (X) == PLUS && XEXP (X, 0) == pic_offset_table_rtx 	\
       && flag_pic && GET_CODE (XEXP (X, 1)) == LABEL_REF))

#define GO_IF_NONINDEXED_ADDRESS(X, ADDR)  \
{ if (INDIRECTABLE_1_ADDRESS_P (X)) goto ADDR; }

/* Only labels on dispatch tables are valid for indexing from.  */
#define GO_IF_INDEXABLE_BASE(X, ADDR)				\
{ rtx temp;							\
  if (GET_CODE (X) == LABEL_REF					\
      && (temp = next_nonnote_insn (XEXP (X, 0))) != 0		\
      && GET_CODE (temp) == JUMP_INSN				\
      && (GET_CODE (PATTERN (temp)) == ADDR_VEC			\
	  || GET_CODE (PATTERN (temp)) == ADDR_DIFF_VEC))	\
    goto ADDR;							\
  if (LEGITIMATE_BASE_REG_P (X)) goto ADDR; }

#define GO_IF_INDEXING(X, ADDR)	\
{ if (GET_CODE (X) == PLUS && LEGITIMATE_INDEX_P (XEXP (X, 0)))		\
    { GO_IF_INDEXABLE_BASE (XEXP (X, 1), ADDR); }			\
  if (GET_CODE (X) == PLUS && LEGITIMATE_INDEX_P (XEXP (X, 1)))		\
    { GO_IF_INDEXABLE_BASE (XEXP (X, 0), ADDR); } }

#define GO_IF_INDEXED_ADDRESS(X, ADDR)	 \
{ GO_IF_INDEXING (X, ADDR);						\
  if (GET_CODE (X) == PLUS)						\
    { if (GET_CODE (XEXP (X, 1)) == CONST_INT				\
	  && (TARGET_68020 || (unsigned) INTVAL (XEXP (X, 1)) + 0x80 < 0x100))		\
	{ rtx go_temp = XEXP (X, 0); GO_IF_INDEXING (go_temp, ADDR); }	\
      if (GET_CODE (XEXP (X, 0)) == CONST_INT				\
	  && (TARGET_68020 || (unsigned) INTVAL (XEXP (X, 0)) + 0x80 < 0x100))		\
	{ rtx go_temp = XEXP (X, 1); GO_IF_INDEXING (go_temp, ADDR); } } }

/* ColdFire/5200 does not allow HImode index registers.  */
#define LEGITIMATE_INDEX_REG_P(X)   \
  ((GET_CODE (X) == REG && REG_OK_FOR_INDEX_P (X))	\
   || (! TARGET_COLDFIRE					\
       && GET_CODE (X) == SIGN_EXTEND			\
       && GET_CODE (XEXP (X, 0)) == REG			\
       && GET_MODE (XEXP (X, 0)) == HImode		\
       && REG_OK_FOR_INDEX_P (XEXP (X, 0)))		\
   || (GET_CODE (X) == SUBREG				\
       && GET_CODE (SUBREG_REG (X)) == REG		\
       && REG_OK_FOR_INDEX_P (SUBREG_REG (X))))

#define LEGITIMATE_INDEX_P(X)   \
   (LEGITIMATE_INDEX_REG_P (X)				\
    || ((TARGET_68020 || TARGET_COLDFIRE) && GET_CODE (X) == MULT \
	&& LEGITIMATE_INDEX_REG_P (XEXP (X, 0))		\
	&& GET_CODE (XEXP (X, 1)) == CONST_INT		\
	&& (INTVAL (XEXP (X, 1)) == 2			\
	    || INTVAL (XEXP (X, 1)) == 4		\
	    || (INTVAL (XEXP (X, 1)) == 8		\
		&& (TARGET_CFV4E || !TARGET_COLDFIRE)))))

/* Coldfire FPU only accepts addressing modes 2-5 */
#define GO_IF_COLDFIRE_FPU_LEGITIMATE_ADDRESS(MODE, X, ADDR)		\
{ if (LEGITIMATE_BASE_REG_P (X)						\
      || ((GET_CODE (X) == PRE_DEC || GET_CODE (X) == POST_INC)		\
          && LEGITIMATE_BASE_REG_P (XEXP (X, 0)))			\
      || ((GET_CODE (X) == PLUS) && LEGITIMATE_BASE_REG_P (XEXP (X, 0))	\
          && (GET_CODE (XEXP (X, 1)) == CONST_INT)			\
          && ((((unsigned) INTVAL (XEXP (X, 1)) + 0x8000) < 0x10000))))	\
  goto ADDR;}

/* If pic, we accept INDEX+LABEL, which is what do_tablejump makes.  */
#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR)				\
{ if (TARGET_COLDFIRE_FPU && (GET_MODE_CLASS (MODE) == MODE_FLOAT))	\
    {									\
      GO_IF_COLDFIRE_FPU_LEGITIMATE_ADDRESS (MODE, X, ADDR);		\
    }									\
  else									\
    {									\
      GO_IF_NONINDEXED_ADDRESS (X, ADDR);				\
      GO_IF_INDEXED_ADDRESS (X, ADDR);					\
      if (flag_pic && MODE == CASE_VECTOR_MODE && GET_CODE (X) == PLUS	\
	  && LEGITIMATE_INDEX_P (XEXP (X, 0))				\
	  && GET_CODE (XEXP (X, 1)) == LABEL_REF)			\
	goto ADDR;							\
    }}

/* Don't call memory_address_noforce for the address to fetch
   the switch offset.  This address is ok as it stands (see above),
   but memory_address_noforce would alter it.  */
#define PIC_CASE_VECTOR_ADDRESS(index) index

/* For the 68000, we handle X+REG by loading X into a register R and
   using R+REG.  R will go in an address reg and indexing will be used.
   However, if REG is a broken-out memory address or multiplication,
   nothing needs to be done because REG can certainly go in an address reg.  */
#define COPY_ONCE(Y) if (!copied) { Y = copy_rtx (Y); copied = ch = 1; }
#define LEGITIMIZE_ADDRESS(X,OLDX,MODE,WIN)   \
{ register int ch = (X) != (OLDX);					\
  if (GET_CODE (X) == PLUS)						\
    { int copied = 0;							\
      if (GET_CODE (XEXP (X, 0)) == MULT)				\
	{ COPY_ONCE (X); XEXP (X, 0) = force_operand (XEXP (X, 0), 0);}	\
      if (GET_CODE (XEXP (X, 1)) == MULT)				\
	{ COPY_ONCE (X); XEXP (X, 1) = force_operand (XEXP (X, 1), 0);}	\
      if (ch && GET_CODE (XEXP (X, 1)) == REG				\
	  && GET_CODE (XEXP (X, 0)) == REG)				\
	{ if (TARGET_CFV4E && GET_MODE_CLASS (MODE) == MODE_FLOAT)	\
	    { COPY_ONCE (X); X = force_operand (X, 0);}			\
	  goto WIN; }							\
      if (ch) { GO_IF_LEGITIMATE_ADDRESS (MODE, X, WIN); }		\
      if (GET_CODE (XEXP (X, 0)) == REG					\
	       || (GET_CODE (XEXP (X, 0)) == SIGN_EXTEND		\
		   && GET_CODE (XEXP (XEXP (X, 0), 0)) == REG		\
		   && GET_MODE (XEXP (XEXP (X, 0), 0)) == HImode))	\
	{ register rtx temp = gen_reg_rtx (Pmode);			\
	  register rtx val = force_operand (XEXP (X, 1), 0);		\
	  emit_move_insn (temp, val);					\
	  COPY_ONCE (X);						\
	  XEXP (X, 1) = temp;						\
	  if (TARGET_COLDFIRE_FPU && GET_MODE_CLASS (MODE) == MODE_FLOAT \
	      && GET_CODE (XEXP (X, 0)) == REG)				\
	    X = force_operand (X, 0);					\
	  goto WIN; }							\
      else if (GET_CODE (XEXP (X, 1)) == REG				\
	       || (GET_CODE (XEXP (X, 1)) == SIGN_EXTEND		\
		   && GET_CODE (XEXP (XEXP (X, 1), 0)) == REG		\
		   && GET_MODE (XEXP (XEXP (X, 1), 0)) == HImode))	\
	{ register rtx temp = gen_reg_rtx (Pmode);			\
	  register rtx val = force_operand (XEXP (X, 0), 0);		\
	  emit_move_insn (temp, val);					\
	  COPY_ONCE (X);						\
	  XEXP (X, 0) = temp;						\
	  if (TARGET_COLDFIRE_FPU && GET_MODE_CLASS (MODE) == MODE_FLOAT \
	      && GET_CODE (XEXP (X, 1)) == REG)				\
	    X = force_operand (X, 0);					\
	  goto WIN; }}}

/* On the 68000, only predecrement and postincrement address depend thus
   (the amount of decrement or increment being the length of the operand).  */
#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR,LABEL)	\
 if (GET_CODE (ADDR) == POST_INC || GET_CODE (ADDR) == PRE_DEC) goto LABEL

#define CASE_VECTOR_MODE HImode
#define CASE_VECTOR_PC_RELATIVE 1

#define DEFAULT_SIGNED_CHAR 1
#define MOVE_MAX 4
#define SLOW_BYTE_ACCESS 0

#define TRULY_NOOP_TRUNCATION(OUTPREC, INPREC) 1

#define STORE_FLAG_VALUE (-1)

#define Pmode SImode
#define FUNCTION_MODE QImode


/* Tell final.c how to eliminate redundant test instructions.  */

/* Here we define machine-dependent flags and fields in cc_status
   (see `conditions.h').  */

/* Set if the cc value is actually in the 68881, so a floating point
   conditional branch must be output.  */
#define CC_IN_68881 04000

/* On the 68000, all the insns to store in an address register fail to
   set the cc's.  However, in some cases these instructions can make it
   possibly invalid to use the saved cc's.  In those cases we clear out
   some or all of the saved cc's so they won't be used.  */
#define NOTICE_UPDATE_CC(EXP,INSN) notice_update_cc (EXP, INSN)

#define OUTPUT_JUMP(NORMAL, FLOAT, NO_OV)  \
do { if (cc_prev_status.flags & CC_IN_68881)			\
    return FLOAT;						\
  if (cc_prev_status.flags & CC_NO_OVERFLOW)			\
    return NO_OV;						\
  return NORMAL; } while (0)

/* Control the assembler format that we output.  */

#define ASM_APP_ON "#APP\n"
#define ASM_APP_OFF "#NO_APP\n"
#define TEXT_SECTION_ASM_OP "\t.text"
#define DATA_SECTION_ASM_OP "\t.data"
#define GLOBAL_ASM_OP "\t.globl\t"
#define REGISTER_PREFIX ""
#define LOCAL_LABEL_PREFIX ""
#define USER_LABEL_PREFIX "_"
#define IMMEDIATE_PREFIX "#"

#define REGISTER_NAMES \
{REGISTER_PREFIX"d0", REGISTER_PREFIX"d1", REGISTER_PREFIX"d2",	\
 REGISTER_PREFIX"d3", REGISTER_PREFIX"d4", REGISTER_PREFIX"d5",	\
 REGISTER_PREFIX"d6", REGISTER_PREFIX"d7",			\
 REGISTER_PREFIX"a0", REGISTER_PREFIX"a1", REGISTER_PREFIX"a2", \
 REGISTER_PREFIX"a3", REGISTER_PREFIX"a4", REGISTER_PREFIX"a5", \
 REGISTER_PREFIX"a6", REGISTER_PREFIX"sp",			\
 REGISTER_PREFIX"fp0", REGISTER_PREFIX"fp1", REGISTER_PREFIX"fp2", \
 REGISTER_PREFIX"fp3", REGISTER_PREFIX"fp4", REGISTER_PREFIX"fp5", \
 REGISTER_PREFIX"fp6", REGISTER_PREFIX"fp7", REGISTER_PREFIX"argptr" }

#define M68K_FP_REG_NAME REGISTER_PREFIX"fp"

/* Return a register name by index, handling %fp nicely.
   We don't replace %fp for targets that don't map it to %a6
   since it may confuse GAS.  */
#define M68K_REGNAME(r) ( \
  ((FRAME_POINTER_REGNUM == 14) \
    && ((r) == FRAME_POINTER_REGNUM) \
    && frame_pointer_needed) ? \
    M68K_FP_REG_NAME : reg_names[(r)])

/* On the Sun-3, the floating point registers have numbers
   18 to 25, not 16 to 23 as they do in the compiler.  */
#define DBX_REGISTER_NUMBER(REGNO) ((REGNO) < 16 ? (REGNO) : (REGNO) + 2)

/* Before the prologue, RA is at 0(%sp).  */
#define INCOMING_RETURN_ADDR_RTX \
  gen_rtx_MEM (VOIDmode, gen_rtx_REG (VOIDmode, STACK_POINTER_REGNUM))

/* After the prologue, RA is at 4(AP) in the current frame.  */
#define RETURN_ADDR_RTX(COUNT, FRAME)					   \
  ((COUNT) == 0								   \
   ? gen_rtx_MEM (Pmode, plus_constant (arg_pointer_rtx, UNITS_PER_WORD)) \
   : gen_rtx_MEM (Pmode, plus_constant (FRAME, UNITS_PER_WORD)))

/* We must not use the DBX register numbers for the DWARF 2 CFA column
   numbers because that maps to numbers beyond FIRST_PSEUDO_REGISTER.
   Instead use the identity mapping.  */
#define DWARF_FRAME_REGNUM(REG) REG

/* Before the prologue, the top of the frame is at 4(%sp).  */
#define INCOMING_FRAME_SP_OFFSET 4

/* Describe how we implement __builtin_eh_return.  */
#define EH_RETURN_DATA_REGNO(N) \
  ((N) < 2 ? (N) : INVALID_REGNUM)
#define EH_RETURN_STACKADJ_RTX	gen_rtx_REG (Pmode, 8)
#define EH_RETURN_HANDLER_RTX					    \
  gen_rtx_MEM (Pmode,						    \
	       gen_rtx_PLUS (Pmode, arg_pointer_rtx,		    \
			     plus_constant (EH_RETURN_STACKADJ_RTX, \
					    UNITS_PER_WORD)))

/* Select a format to encode pointers in exception handling data.  CODE
   is 0 for data, 1 for code labels, 2 for function pointers.  GLOBAL is
   true if the symbol may be affected by dynamic relocations.  */
#define ASM_PREFERRED_EH_DATA_FORMAT(CODE, GLOBAL)			   \
  (flag_pic								   \
   ? ((GLOBAL) ? DW_EH_PE_indirect : 0) | DW_EH_PE_pcrel | DW_EH_PE_sdata4 \
   : DW_EH_PE_absptr)

#define ASM_OUTPUT_LABELREF(FILE,NAME)	\
  asm_fprintf (FILE, "%U%s", NAME)

#define ASM_GENERATE_INTERNAL_LABEL(LABEL,PREFIX,NUM)	\
  sprintf (LABEL, "*%s%s%ld", LOCAL_LABEL_PREFIX, PREFIX, (long)(NUM))

#define ASM_OUTPUT_REG_PUSH(FILE,REGNO)  \
  asm_fprintf (FILE, "\tmovel %s,%Rsp@-\n", reg_names[REGNO])
#define ASM_OUTPUT_REG_POP(FILE,REGNO)  \
  asm_fprintf (FILE, "\tmovel %Rsp@+,%s\n", reg_names[REGNO])

/* The m68k does not use absolute case-vectors, but we must define this macro
   anyway.  */
#define ASM_OUTPUT_ADDR_VEC_ELT(FILE, VALUE)  \
  asm_fprintf (FILE, "\t.long %LL%d\n", VALUE)

#define ASM_OUTPUT_ADDR_DIFF_ELT(FILE, BODY, VALUE, REL)  \
  asm_fprintf (FILE, "\t.word %LL%d-%LL%d\n", VALUE, REL)

/* We don't have a way to align to more than a two-byte boundary, so do the
   best we can and don't complain.  */
#define ASM_OUTPUT_ALIGN(FILE,LOG)	\
  if ((LOG) >= 1)			\
    fprintf (FILE, "\t.even\n");

#define ASM_OUTPUT_SKIP(FILE,SIZE)  \
  fprintf (FILE, "\t.skip %u\n", (int)(SIZE))

#define ASM_OUTPUT_COMMON(FILE, NAME, SIZE, ROUNDED)  \
( fputs (".comm ", (FILE)),			\
  assemble_name ((FILE), (NAME)),		\
  fprintf ((FILE), ",%u\n", (int)(ROUNDED)))

#define ASM_OUTPUT_LOCAL(FILE, NAME, SIZE, ROUNDED)  \
( fputs (".lcomm ", (FILE)),			\
  assemble_name ((FILE), (NAME)),		\
  fprintf ((FILE), ",%u\n", (int)(ROUNDED)))

/* Output a float value (represented as a C double) as an immediate operand.
   This macro is m68k-specific.  */
#define ASM_OUTPUT_FLOAT_OPERAND(CODE,FILE,VALUE)		\
 do {								\
      if (CODE == 'f')						\
        {							\
          char dstr[30];					\
	  real_to_decimal (dstr, &(VALUE), sizeof (dstr), 9, 0); \
          asm_fprintf ((FILE), "%I0r%s", dstr);			\
        }							\
      else							\
        {							\
          long l;						\
          REAL_VALUE_TO_TARGET_SINGLE (VALUE, l);		\
          asm_fprintf ((FILE), "%I0x%lx", l);			\
        }							\
     } while (0)

/* Output a double value (represented as a C double) as an immediate operand.
   This macro is m68k-specific.  */
#define ASM_OUTPUT_DOUBLE_OPERAND(FILE,VALUE)				\
 do { char dstr[30];							\
      real_to_decimal (dstr, &(VALUE), sizeof (dstr), 0, 1);		\
      asm_fprintf (FILE, "%I0r%s", dstr);				\
    } while (0)

/* Note, long double immediate operands are not actually
   generated by m68k.md.  */
#define ASM_OUTPUT_LONG_DOUBLE_OPERAND(FILE,VALUE)			\
 do { char dstr[30];							\
      real_to_decimal (dstr, &(VALUE), sizeof (dstr), 0, 1);		\
      asm_fprintf (FILE, "%I0r%s", dstr);				\
    } while (0)

/* On the 68000, we use several CODE characters:
   '.' for dot needed in Motorola-style opcode names.
   '-' for an operand pushing on the stack:
       sp@-, -(sp) or -(%sp) depending on the style of syntax.
   '+' for an operand pushing on the stack:
       sp@+, (sp)+ or (%sp)+ depending on the style of syntax.
   '@' for a reference to the top word on the stack:
       sp@, (sp) or (%sp) depending on the style of syntax.
   '#' for an immediate operand prefix (# in MIT and Motorola syntax
       but & in SGS syntax).
   '!' for the fpcr register (used in some float-to-fixed conversions).
   '$' for the letter `s' in an op code, but only on the 68040.
   '&' for the letter `d' in an op code, but only on the 68040.
   '/' for register prefix needed by longlong.h.

   'b' for byte insn (no effect, on the Sun; this is for the ISI).
   'd' to force memory addressing to be absolute, not relative.
   'f' for float insn (print a CONST_DOUBLE as a float rather than in hex)
   'o' for operands to go directly to output_operand_address (bypassing
       print_operand_address--used only for SYMBOL_REFs under TARGET_PCREL)
   'x' for float insn (print a CONST_DOUBLE as a float rather than in hex),
       or print pair of registers as rx:ry.  */

#define PRINT_OPERAND_PUNCT_VALID_P(CODE)				\
  ((CODE) == '.' || (CODE) == '#' || (CODE) == '-'			\
   || (CODE) == '+' || (CODE) == '@' || (CODE) == '!'			\
   || (CODE) == '$' || (CODE) == '&' || (CODE) == '/')


/* See m68k.c for the m68k specific codes.  */
#define PRINT_OPERAND(FILE, X, CODE) print_operand (FILE, X, CODE)

#define PRINT_OPERAND_ADDRESS(FILE, ADDR) print_operand_address (FILE, ADDR)

/* Variables in m68k.c */
extern const char *m68k_library_id_string;
extern int m68k_last_compare_had_fp_operands;
