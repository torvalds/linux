/* Definitions of target machine for GNU compiler, Renesas M32R cpu.
   Copyright (C) 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004,
   2005, 2006 Free Software Foundation, Inc.

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
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* Things to do:
- longlong.h?
*/

#undef SWITCH_TAKES_ARG
#undef WORD_SWITCH_TAKES_ARG
#undef HANDLE_SYSV_PRAGMA
#undef SIZE_TYPE
#undef PTRDIFF_TYPE
#undef WCHAR_TYPE
#undef WCHAR_TYPE_SIZE
#undef TARGET_VERSION
#undef CPP_SPEC
#undef ASM_SPEC
#undef LINK_SPEC
#undef STARTFILE_SPEC
#undef ENDFILE_SPEC

#undef ASM_APP_ON
#undef ASM_APP_OFF


/* M32R/X overrides.  */
/* Print subsidiary information on the compiler version in use.  */
#define TARGET_VERSION fprintf (stderr, " (m32r/x/2)");

/* Additional flags for the preprocessor.  */
#define CPP_CPU_SPEC "%{m32rx:-D__M32RX__ -D__m32rx__ -U__M32R2__ -U__m32r2__} \
%{m32r2:-D__M32R2__ -D__m32r2__ -U__M32RX__ -U__m32rx__} \
%{m32r:-U__M32RX__  -U__m32rx__ -U__M32R2__ -U__m32r2__} \
 "

/* Assembler switches.  */
#define ASM_CPU_SPEC \
"%{m32r} %{m32rx} %{m32r2} %{!O0: %{O*: -O}} --no-warn-explicit-parallel-conflicts"

/* Use m32rx specific crt0/crtinit/crtfini files.  */
#define STARTFILE_CPU_SPEC "%{!shared:crt0.o%s} %{m32rx:m32rx/crtinit.o%s} %{!m32rx:crtinit.o%s}"
#define ENDFILE_CPU_SPEC "-lgloss %{m32rx:m32rx/crtfini.o%s} %{!m32rx:crtfini.o%s}"

/* Define this macro as a C expression for the initializer of an array of
   strings to tell the driver program which options are defaults for this
   target and thus do not need to be handled specially when using
   `MULTILIB_OPTIONS'.  */
#define SUBTARGET_MULTILIB_DEFAULTS , "m32r"

/* Number of additional registers the subtarget defines.  */
#define SUBTARGET_NUM_REGISTERS 1

/* 1 for registers that cannot be allocated.  */
#define SUBTARGET_FIXED_REGISTERS , 1

/* 1 for registers that are not available across function calls.  */
#define SUBTARGET_CALL_USED_REGISTERS , 1

/* Order to allocate model specific registers.  */
#define SUBTARGET_REG_ALLOC_ORDER , 19

/* Registers which are accumulators.  */
#define SUBTARGET_REG_CLASS_ACCUM 0x80000

/* All registers added.  */
#define SUBTARGET_REG_CLASS_ALL SUBTARGET_REG_CLASS_ACCUM

/* Additional accumulator registers.  */
#define SUBTARGET_ACCUM_P(REGNO) ((REGNO) == 19)

/* Define additional register names.  */
#define SUBTARGET_REGISTER_NAMES , "a1"
/* end M32R/X overrides.  */

/* Print subsidiary information on the compiler version in use.  */
#ifndef	TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (m32r)")
#endif

/* Switch  Recognition by gcc.c.  Add -G xx support.  */

#undef  SWITCH_TAKES_ARG
#define SWITCH_TAKES_ARG(CHAR) \
(DEFAULT_SWITCH_TAKES_ARG (CHAR) || (CHAR) == 'G')

/* Names to predefine in the preprocessor for this target machine.  */
/* __M32R__ is defined by the existing compiler so we use that.  */
#define TARGET_CPU_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define ("__M32R__");		\
      builtin_define ("__m32r__");		\
      builtin_assert ("cpu=m32r");		\
      builtin_assert ("machine=m32r");		\
      builtin_define (TARGET_BIG_ENDIAN		\
                      ? "__BIG_ENDIAN__" : "__LITTLE_ENDIAN__"); \
    }						\
  while (0)

/* This macro defines names of additional specifications to put in the specs
   that can be used in various specifications like CC1_SPEC.  Its definition
   is an initializer with a subgrouping for each command option.

   Each subgrouping contains a string constant, that defines the
   specification name, and a string constant that used by the GCC driver
   program.

   Do not define this macro if it does not need to do anything.  */

#ifndef SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS
#endif

#ifndef	ASM_CPU_SPEC
#define	ASM_CPU_SPEC ""
#endif

#ifndef	CPP_CPU_SPEC
#define	CPP_CPU_SPEC ""
#endif

#ifndef	CC1_CPU_SPEC
#define	CC1_CPU_SPEC ""
#endif

#ifndef	LINK_CPU_SPEC
#define	LINK_CPU_SPEC ""
#endif

#ifndef STARTFILE_CPU_SPEC
#define STARTFILE_CPU_SPEC "%{!shared:crt0.o%s} crtinit.o%s"
#endif

#ifndef ENDFILE_CPU_SPEC
#define ENDFILE_CPU_SPEC "-lgloss crtfini.o%s"
#endif

#ifndef RELAX_SPEC
#if 0 /* Not supported yet.  */
#define RELAX_SPEC "%{mrelax:-relax}"
#else
#define RELAX_SPEC ""
#endif
#endif

#define EXTRA_SPECS							\
  { "asm_cpu",			ASM_CPU_SPEC },				\
  { "cpp_cpu",			CPP_CPU_SPEC },				\
  { "cc1_cpu",			CC1_CPU_SPEC },				\
  { "link_cpu",			LINK_CPU_SPEC },			\
  { "startfile_cpu",		STARTFILE_CPU_SPEC },			\
  { "endfile_cpu",		ENDFILE_CPU_SPEC },			\
  { "relax",			RELAX_SPEC },				\
  SUBTARGET_EXTRA_SPECS

#define CPP_SPEC "%(cpp_cpu)"

#undef  CC1_SPEC
#define CC1_SPEC "%{G*} %(cc1_cpu)"

/* Options to pass on to the assembler.  */
#undef  ASM_SPEC
#define ASM_SPEC "%{v} %(asm_cpu) %(relax) %{fpic|fpie:-K PIC} %{fPIC|fPIE:-K PIC}"

#define LINK_SPEC "%{v} %(link_cpu) %(relax)"

#undef  STARTFILE_SPEC
#define STARTFILE_SPEC "%(startfile_cpu)"

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC "%(endfile_cpu)"

#undef LIB_SPEC

/* Run-time compilation parameters selecting different hardware subsets.  */

#define TARGET_M32R             (! TARGET_M32RX && ! TARGET_M32R2)

#ifndef TARGET_LITTLE_ENDIAN
#define TARGET_LITTLE_ENDIAN	0
#endif
#define TARGET_BIG_ENDIAN       (! TARGET_LITTLE_ENDIAN)

/* This defaults us to m32r.  */
#ifndef TARGET_CPU_DEFAULT
#define TARGET_CPU_DEFAULT 0
#endif

/* Code Models

   Code models are used to select between two choices of two separate
   possibilities (address space size, call insn to use):

   small: addresses use 24 bits, use bl to make calls
   medium: addresses use 32 bits, use bl to make calls (*1)
   large: addresses use 32 bits, use seth/add3/jl to make calls (*2)

   The fourth is "addresses use 24 bits, use seth/add3/jl to make calls" but
   using this one doesn't make much sense.

   (*1) The linker may eventually be able to relax seth/add3 -> ld24.
   (*2) The linker may eventually be able to relax seth/add3/jl -> bl.

   Internally these are recorded as TARGET_ADDR{24,32} and
   TARGET_CALL{26,32}.

   The __model__ attribute can be used to select the code model to use when
   accessing particular objects.  */

enum m32r_model { M32R_MODEL_SMALL, M32R_MODEL_MEDIUM, M32R_MODEL_LARGE };

extern enum m32r_model m32r_model;
#define TARGET_MODEL_SMALL  (m32r_model == M32R_MODEL_SMALL)
#define TARGET_MODEL_MEDIUM (m32r_model == M32R_MODEL_MEDIUM)
#define TARGET_MODEL_LARGE  (m32r_model == M32R_MODEL_LARGE)
#define TARGET_ADDR24       (m32r_model == M32R_MODEL_SMALL)
#define TARGET_ADDR32       (! TARGET_ADDR24)
#define TARGET_CALL26       (! TARGET_CALL32)
#define TARGET_CALL32       (m32r_model == M32R_MODEL_LARGE)

/* The default is the small model.  */
#ifndef M32R_MODEL_DEFAULT
#define M32R_MODEL_DEFAULT M32R_MODEL_SMALL
#endif

/* Small Data Area

   The SDA consists of sections .sdata, .sbss, and .scommon.
   .scommon isn't a real section, symbols in it have their section index
   set to SHN_M32R_SCOMMON, though support for it exists in the linker script.

   Two switches control the SDA:

   -G NNN        - specifies the maximum size of variable to go in the SDA

   -msdata=foo   - specifies how such variables are handled

        -msdata=none  - small data area is disabled

        -msdata=sdata - small data goes in the SDA, special code isn't
                        generated to use it, and special relocs aren't
                        generated

        -msdata=use   - small data goes in the SDA, special code is generated
                        to use the SDA and special relocs are generated

   The SDA is not multilib'd, it isn't necessary.
   MULTILIB_EXTRA_OPTS is set in tmake_file to -msdata=sdata so multilib'd
   libraries have small data in .sdata/SHN_M32R_SCOMMON so programs that use
   -msdata=use will successfully link with them (references in header files
   will cause the compiler to emit code that refers to library objects in
   .data).  ??? There can be a problem if the user passes a -G value greater
   than the default and a library object in a header file is that size.
   The default is 8 so this should be rare - if it occurs the user
   is required to rebuild the libraries or use a smaller value for -G.  */

/* Maximum size of variables that go in .sdata/.sbss.
   The -msdata=foo switch also controls how small variables are handled.  */
#ifndef SDATA_DEFAULT_SIZE
#define SDATA_DEFAULT_SIZE 8
#endif

enum m32r_sdata { M32R_SDATA_NONE, M32R_SDATA_SDATA, M32R_SDATA_USE };

extern enum m32r_sdata m32r_sdata;
#define TARGET_SDATA_NONE  (m32r_sdata == M32R_SDATA_NONE)
#define TARGET_SDATA_SDATA (m32r_sdata == M32R_SDATA_SDATA)
#define TARGET_SDATA_USE   (m32r_sdata == M32R_SDATA_USE)

/* Default is to disable the SDA
   [for upward compatibility with previous toolchains].  */
#ifndef M32R_SDATA_DEFAULT
#define M32R_SDATA_DEFAULT M32R_SDATA_NONE
#endif

/* Define this macro as a C expression for the initializer of an array of
   strings to tell the driver program which options are defaults for this
   target and thus do not need to be handled specially when using
   `MULTILIB_OPTIONS'.  */
#ifndef SUBTARGET_MULTILIB_DEFAULTS
#define SUBTARGET_MULTILIB_DEFAULTS
#endif

#ifndef MULTILIB_DEFAULTS
#define MULTILIB_DEFAULTS { "mmodel=small" SUBTARGET_MULTILIB_DEFAULTS }
#endif

/* Sometimes certain combinations of command options do not make
   sense on a particular target machine.  You can define a macro
   `OVERRIDE_OPTIONS' to take account of this.  This macro, if
   defined, is executed once just after all the command options have
   been parsed.

   Don't use this macro to turn on various extra optimizations for
   `-O'.  That is what `OPTIMIZATION_OPTIONS' is for.  */

#ifndef SUBTARGET_OVERRIDE_OPTIONS
#define SUBTARGET_OVERRIDE_OPTIONS
#endif

#define OVERRIDE_OPTIONS			\
  do						\
    {						\
      /* These need to be done at start up.	\
	 It's convenient to do them here.  */	\
      m32r_init ();				\
      SUBTARGET_OVERRIDE_OPTIONS		\
    }						\
  while (0)

#ifndef SUBTARGET_OPTIMIZATION_OPTIONS
#define SUBTARGET_OPTIMIZATION_OPTIONS
#endif

#define OPTIMIZATION_OPTIONS(LEVEL, SIZE)	\
  do						\
    {						\
      if (LEVEL == 1)				\
	flag_regmove = TRUE;			\
      						\
      if (SIZE)					\
	{					\
	  flag_omit_frame_pointer = TRUE;	\
	}					\
      						\
      SUBTARGET_OPTIMIZATION_OPTIONS		\
    }						\
  while (0)

/* Define this macro if debugging can be performed even without a
   frame pointer.  If this macro is defined, GCC will turn on the
   `-fomit-frame-pointer' option whenever `-O' is specified.  */
#define CAN_DEBUG_WITHOUT_FP

/* Target machine storage layout.  */

/* Define this if most significant bit is lowest numbered
   in instructions that operate on numbered bit-fields.  */
#define BITS_BIG_ENDIAN 1

/* Define this if most significant byte of a word is the lowest numbered.  */
#define BYTES_BIG_ENDIAN (TARGET_LITTLE_ENDIAN == 0)

/* Define this if most significant word of a multiword number is the lowest
   numbered.  */
#define WORDS_BIG_ENDIAN (TARGET_LITTLE_ENDIAN == 0)

/* Define this macro if WORDS_BIG_ENDIAN is not constant.  This must
   be a constant value with the same meaning as WORDS_BIG_ENDIAN,
   which will be used only when compiling libgcc2.c.  Typically the
   value will be set based on preprocessor defines.  */
/*#define LIBGCC2_WORDS_BIG_ENDIAN 1*/

/* Width of a word, in units (bytes).  */
#define UNITS_PER_WORD 4

/* Define this macro if it is advisable to hold scalars in registers
   in a wider mode than that declared by the program.  In such cases, 
   the value is constrained to be within the bounds of the declared
   type, but kept valid in the wider mode.  The signedness of the
   extension may differ from that of the type.  */
#define PROMOTE_MODE(MODE, UNSIGNEDP, TYPE)	\
  if (GET_MODE_CLASS (MODE) == MODE_INT		\
      && GET_MODE_SIZE (MODE) < UNITS_PER_WORD)	\
    {						\
      (MODE) = SImode;				\
    }

/* Allocation boundary (in *bits*) for storing arguments in argument list.  */
#define PARM_BOUNDARY 32

/* Boundary (in *bits*) on which stack pointer should be aligned.  */
#define STACK_BOUNDARY 32

/* ALIGN FRAMES on word boundaries */
#define M32R_STACK_ALIGN(LOC) (((LOC) + 3) & ~ 3)

/* Allocation boundary (in *bits*) for the code of a function.  */
#define FUNCTION_BOUNDARY 32

/* Alignment of field after `int : 0' in a structure.  */
#define EMPTY_FIELD_BOUNDARY 32

/* Every structure's size must be a multiple of this.  */
#define STRUCTURE_SIZE_BOUNDARY 8

/* A bit-field declared as `int' forces `int' alignment for the struct.  */
#define PCC_BITFIELD_TYPE_MATTERS 1

/* No data type wants to be aligned rounder than this.  */
#define BIGGEST_ALIGNMENT 32

/* The best alignment to use in cases where we have a choice.  */
#define FASTEST_ALIGNMENT 32

/* Make strings word-aligned so strcpy from constants will be faster.  */
#define CONSTANT_ALIGNMENT(EXP, ALIGN)	\
  ((TREE_CODE (EXP) == STRING_CST	\
    && (ALIGN) < FASTEST_ALIGNMENT)	\
   ? FASTEST_ALIGNMENT : (ALIGN))

/* Make arrays of chars word-aligned for the same reasons.  */
#define DATA_ALIGNMENT(TYPE, ALIGN)					\
  (TREE_CODE (TYPE) == ARRAY_TYPE					\
   && TYPE_MODE (TREE_TYPE (TYPE)) == QImode				\
   && (ALIGN) < FASTEST_ALIGNMENT ? FASTEST_ALIGNMENT : (ALIGN))

/* Set this nonzero if move instructions will actually fail to work
   when given unaligned data.  */
#define STRICT_ALIGNMENT 1

/* Define LAVEL_ALIGN to calculate code length of PNOP at labels.  */
#define LABEL_ALIGN(insn) 2

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

#define M32R_NUM_REGISTERS 	19

#ifndef SUBTARGET_NUM_REGISTERS
#define SUBTARGET_NUM_REGISTERS 0
#endif

#define FIRST_PSEUDO_REGISTER (M32R_NUM_REGISTERS + SUBTARGET_NUM_REGISTERS)
	
/* 1 for registers that have pervasive standard uses
   and are not available for the register allocator.

   0-3   - arguments/results
   4-5   - call used [4 is used as a tmp during prologue/epilogue generation]
   6     - call used, gptmp
   7     - call used, static chain pointer
   8-11  - call saved
   12    - call saved [reserved for global pointer]
   13    - frame pointer
   14    - subroutine link register
   15    - stack pointer
   16    - arg pointer
   17    - carry flag
   18	 - accumulator
   19    - accumulator 1 in the m32r/x
   By default, the extension registers are not available.  */

#ifndef SUBTARGET_FIXED_REGISTERS
#define SUBTARGET_FIXED_REGISTERS
#endif

#define FIXED_REGISTERS		\
{				\
  0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 1,	\
  1, 1, 1			\
  SUBTARGET_FIXED_REGISTERS	\
}

/* 1 for registers not available across function calls.
   These must include the FIXED_REGISTERS and also any
   registers that can be used without being saved.
   The latter must include the registers where values are returned
   and the register where structure-value addresses are passed.
   Aside from that, you can include as many other registers as you like.  */

#ifndef SUBTARGET_CALL_USED_REGISTERS
#define SUBTARGET_CALL_USED_REGISTERS
#endif

#define CALL_USED_REGISTERS	\
{				\
  1, 1, 1, 1, 1, 1, 1, 1,	\
  0, 0, 0, 0, 0, 0, 1, 1,	\
  1, 1, 1			\
  SUBTARGET_CALL_USED_REGISTERS	\
}

#define CALL_REALLY_USED_REGISTERS CALL_USED_REGISTERS

/* Zero or more C statements that may conditionally modify two variables
   `fixed_regs' and `call_used_regs' (both of type `char []') after they
   have been initialized from the two preceding macros.

   This is necessary in case the fixed or call-clobbered registers depend
   on target flags.

   You need not define this macro if it has no work to do.  */

#ifdef SUBTARGET_CONDITIONAL_REGISTER_USAGE
#define CONDITIONAL_REGISTER_USAGE SUBTARGET_CONDITIONAL_REGISTER_USAGE
#else
#define CONDITIONAL_REGISTER_USAGE			 \
  do							 \
    {							 \
      if (flag_pic)					 \
       {						 \
         fixed_regs[PIC_OFFSET_TABLE_REGNUM] = 1;	 \
         call_used_regs[PIC_OFFSET_TABLE_REGNUM] = 1;	 \
       }						 \
    }							 \
  while (0)
#endif

/* If defined, an initializer for a vector of integers, containing the
   numbers of hard registers in the order in which GCC should
   prefer to use them (from most preferred to least).  */

#ifndef SUBTARGET_REG_ALLOC_ORDER
#define SUBTARGET_REG_ALLOC_ORDER
#endif

#if 1 /* Better for int code.  */
#define REG_ALLOC_ORDER				\
{						\
  4,  5,  6,  7,  2,  3,  8,  9, 10,		\
  11, 12, 13, 14,  0,  1, 15, 16, 17, 18	\
  SUBTARGET_REG_ALLOC_ORDER			\
}

#else /* Better for fp code at expense of int code.  */
#define REG_ALLOC_ORDER				\
{						\
   0,  1,  2,  3,  4,  5,  6,  7,  8,		\
   9, 10, 11, 12, 13, 14, 15, 16, 17, 18	\
  SUBTARGET_REG_ALLOC_ORDER			\
}
#endif

/* Return number of consecutive hard regs needed starting at reg REGNO
   to hold something of mode MODE.
   This is ordinarily the length in words of a value of mode MODE
   but can be less for certain modes in special long registers.  */
#define HARD_REGNO_NREGS(REGNO, MODE) \
  ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* Value is 1 if hard register REGNO can hold a value of machine-mode MODE.  */
extern const unsigned int m32r_hard_regno_mode_ok[FIRST_PSEUDO_REGISTER];
extern unsigned int m32r_mode_class[];
#define HARD_REGNO_MODE_OK(REGNO, MODE) \
  ((m32r_hard_regno_mode_ok[REGNO] & m32r_mode_class[MODE]) != 0)

/* A C expression that is nonzero if it is desirable to choose
   register allocation so as to avoid move instructions between a
   value of mode MODE1 and a value of mode MODE2.

   If `HARD_REGNO_MODE_OK (R, MODE1)' and `HARD_REGNO_MODE_OK (R,
   MODE2)' are ever different for any R, then `MODES_TIEABLE_P (MODE1,
   MODE2)' must be zero.  */

/* Tie QI/HI/SI modes together.  */
#define MODES_TIEABLE_P(MODE1, MODE2) 		\
  (   GET_MODE_CLASS (MODE1) == MODE_INT	\
   && GET_MODE_CLASS (MODE2) == MODE_INT	\
   && GET_MODE_SIZE (MODE1) <= UNITS_PER_WORD	\
   && GET_MODE_SIZE (MODE2) <= UNITS_PER_WORD)

#define HARD_REGNO_RENAME_OK(OLD_REG, NEW_REG) \
  m32r_hard_regno_rename_ok (OLD_REG, NEW_REG)

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

enum reg_class
{
  NO_REGS, CARRY_REG, ACCUM_REGS, GENERAL_REGS, ALL_REGS, LIM_REG_CLASSES
};

#define N_REG_CLASSES ((int) LIM_REG_CLASSES)

/* Give names of register classes as strings for dump file.  */
#define REG_CLASS_NAMES \
  { "NO_REGS", "CARRY_REG", "ACCUM_REGS", "GENERAL_REGS", "ALL_REGS" }

/* Define which registers fit in which classes.
   This is an initializer for a vector of HARD_REG_SET
   of length N_REG_CLASSES.  */

#ifndef SUBTARGET_REG_CLASS_CARRY
#define SUBTARGET_REG_CLASS_CARRY 0
#endif

#ifndef SUBTARGET_REG_CLASS_ACCUM
#define SUBTARGET_REG_CLASS_ACCUM 0
#endif

#ifndef SUBTARGET_REG_CLASS_GENERAL
#define SUBTARGET_REG_CLASS_GENERAL 0
#endif

#ifndef SUBTARGET_REG_CLASS_ALL
#define SUBTARGET_REG_CLASS_ALL 0
#endif

#define REG_CLASS_CONTENTS						\
{									\
  { 0x00000 },								\
  { 0x20000 | SUBTARGET_REG_CLASS_CARRY },				\
  { 0x40000 | SUBTARGET_REG_CLASS_ACCUM },				\
  { 0x1ffff | SUBTARGET_REG_CLASS_GENERAL },				\
  { 0x7ffff | SUBTARGET_REG_CLASS_ALL },				\
}

/* The same information, inverted:
   Return the class number of the smallest class containing
   reg number REGNO.  This could be a conditional expression
   or could index an array.  */
extern enum reg_class m32r_regno_reg_class[FIRST_PSEUDO_REGISTER];
#define REGNO_REG_CLASS(REGNO) (m32r_regno_reg_class[REGNO])

/* The class value for index registers, and the one for base regs.  */
#define INDEX_REG_CLASS GENERAL_REGS
#define BASE_REG_CLASS GENERAL_REGS

#define REG_CLASS_FROM_LETTER(C)			\
  (  (C) == 'c'	? CARRY_REG				\
   : (C) == 'a'	? ACCUM_REGS				\
   :		  NO_REGS)

/* These assume that REGNO is a hard or pseudo reg number.
   They give nonzero only if REGNO is a hard reg of the suitable class
   or a pseudo reg currently allocated to a suitable hard reg.
   Since they use reg_renumber, they are safe only once reg_renumber
   has been allocated, which happens in local-alloc.c.  */
#define REGNO_OK_FOR_BASE_P(REGNO) \
  ((REGNO) < FIRST_PSEUDO_REGISTER			\
   ? GPR_P (REGNO) || (REGNO) == ARG_POINTER_REGNUM	\
   : GPR_P (reg_renumber[REGNO]))

#define REGNO_OK_FOR_INDEX_P(REGNO) REGNO_OK_FOR_BASE_P(REGNO)

/* Given an rtx X being reloaded into a reg required to be
   in class CLASS, return the class of reg to actually use.
   In general this is just CLASS; but on some machines
   in some cases it is preferable to use a more restrictive class.  */
#define PREFERRED_RELOAD_CLASS(X,CLASS) (CLASS)

/* Return the maximum number of consecutive registers
   needed to represent mode MODE in a register of class CLASS.  */
#define CLASS_MAX_NREGS(CLASS, MODE) \
  ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* The letters I, J, K, L, M, N, O, P in a register constraint string
   can be used to stand for particular ranges of immediate operands.
   This macro defines what the ranges are.
   C is the letter, and VALUE is a constant value.
   Return 1 if VALUE is in the range specified by C.  */
/* 'I' is used for 8 bit signed immediates.
   'J' is used for 16 bit signed immediates.
   'K' is used for 16 bit unsigned immediates.
   'L' is used for 16 bit immediates left shifted by 16 (sign ???).
   'M' is used for 24 bit unsigned immediates.
   'N' is used for any 32 bit non-symbolic value.
   'O' is used for 5 bit unsigned immediates (shift count).
   'P' is used for 16 bit signed immediates for compares
       (values in the range -32767 to +32768).  */

/* Return true if a value is inside a range.  */
#define IN_RANGE_P(VALUE, LOW, HIGH)					\
  (((unsigned HOST_WIDE_INT)((VALUE) - (LOW)))				\
   <= ((unsigned HOST_WIDE_INT)((HIGH) - (LOW))))

/* Local to this file.  */
#define INT8_P(X)      ((X) >= -   0x80 && (X) <= 0x7f)
#define INT16_P(X)     ((X) >= - 0x8000 && (X) <= 0x7fff)
#define CMP_INT16_P(X) ((X) >= - 0x7fff && (X) <= 0x8000)
#define UPPER16_P(X)  (((X) & 0xffff) == 0				\
		        && ((X) >> 16) >= - 0x8000			\
		        && ((X) >> 16) <= 0x7fff)
#define UINT16_P(X)   (((unsigned HOST_WIDE_INT) (X)) <= 0x0000ffff)
#define UINT24_P(X)   (((unsigned HOST_WIDE_INT) (X)) <= 0x00ffffff)
#define UINT32_P(X)   (((unsigned HOST_WIDE_INT) (X)) <= 0xffffffff)
#define UINT5_P(X)    ((X) >= 0 && (X) < 32)
#define INVERTED_SIGNED_8BIT(VAL) ((VAL) >= -127 && (VAL) <= 128)

#define CONST_OK_FOR_LETTER_P(VALUE, C)					\
  (  (C) == 'I' ? INT8_P (VALUE)					\
   : (C) == 'J' ? INT16_P (VALUE)					\
   : (C) == 'K' ? UINT16_P (VALUE)					\
   : (C) == 'L' ? UPPER16_P (VALUE)					\
   : (C) == 'M' ? UINT24_P (VALUE)					\
   : (C) == 'N' ? INVERTED_SIGNED_8BIT (VALUE)				\
   : (C) == 'O' ? UINT5_P (VALUE)					\
   : (C) == 'P' ? CMP_INT16_P (VALUE)					\
   : 0)

/* Similar, but for floating constants, and defining letters G and H.
   Here VALUE is the CONST_DOUBLE rtx itself.
   For the m32r, handle a few constants inline.
   ??? We needn't treat DI and DF modes differently, but for now we do.  */
#define CONST_DOUBLE_OK_FOR_LETTER_P(VALUE, C)				\
  (  (C) == 'G' ? easy_di_const (VALUE)					\
   : (C) == 'H' ? easy_df_const (VALUE)					\
   : 0)

/* A C expression that defines the optional machine-dependent constraint
   letters that can be used to segregate specific types of operands,
   usually memory references, for the target machine.  It should return 1 if
   VALUE corresponds to the operand type represented by the constraint letter
   C.  If C is not defined as an extra constraint, the value returned should
   be 0 regardless of VALUE.  */
/* Q is for symbolic addresses loadable with ld24.
   R is for symbolic addresses when ld24 can't be used.
   S is for stores with pre {inc,dec}rement
   T is for indirect of a pointer.
   U is for loads with post increment.  */

#define EXTRA_CONSTRAINT(VALUE, C)					\
  (  (C) == 'Q' ? ((TARGET_ADDR24 && GET_CODE (VALUE) == LABEL_REF)	\
		 || addr24_operand (VALUE, VOIDmode))			\
   : (C) == 'R' ? ((TARGET_ADDR32 && GET_CODE (VALUE) == LABEL_REF)	\
		 || addr32_operand (VALUE, VOIDmode))			\
   : (C) == 'S' ? (GET_CODE (VALUE) == MEM				\
		 && STORE_PREINC_PREDEC_P (GET_MODE (VALUE),		\
					   XEXP (VALUE, 0)))		\
   : (C) == 'T' ? (GET_CODE (VALUE) == MEM				\
		 && memreg_operand (VALUE, GET_MODE (VALUE)))		\
   : (C) == 'U' ? (GET_CODE (VALUE) == MEM				\
		 && LOAD_POSTINC_P (GET_MODE (VALUE),			\
				    XEXP (VALUE, 0)))			\
   : 0)

/* Stack layout and stack pointer usage.  */

/* Define this macro if pushing a word onto the stack moves the stack
   pointer to a smaller address.  */
#define STACK_GROWS_DOWNWARD

/* Offset from frame pointer to start allocating local variables at.
   If FRAME_GROWS_DOWNWARD, this is the offset to the END of the
   first local allocated.  Otherwise, it is the offset to the BEGINNING
   of the first local allocated.  */
/* The frame pointer points at the same place as the stack pointer, except if
   alloca has been called.  */
#define STARTING_FRAME_OFFSET \
  M32R_STACK_ALIGN (current_function_outgoing_args_size)

/* Offset from the stack pointer register to the first location at which
   outgoing arguments are placed.  */
#define STACK_POINTER_OFFSET 0

/* Offset of first parameter from the argument pointer register value.  */
#define FIRST_PARM_OFFSET(FNDECL) 0

/* Register to use for pushing function arguments.  */
#define STACK_POINTER_REGNUM 15

/* Base register for access to local variables of the function.  */
#define FRAME_POINTER_REGNUM 13

/* Base register for access to arguments of the function.  */
#define ARG_POINTER_REGNUM 16

/* Register in which static-chain is passed to a function.
   This must not be a register used by the prologue.  */
#define STATIC_CHAIN_REGNUM  7

/* These aren't official macros.  */
#define PROLOGUE_TMP_REGNUM  4
#define RETURN_ADDR_REGNUM  14
/* #define GP_REGNUM        12 */
#define CARRY_REGNUM        17
#define ACCUM_REGNUM        18
#define M32R_MAX_INT_REGS   16

#ifndef SUBTARGET_GPR_P
#define SUBTARGET_GPR_P(REGNO) 0
#endif

#ifndef SUBTARGET_ACCUM_P
#define SUBTARGET_ACCUM_P(REGNO) 0
#endif

#ifndef SUBTARGET_CARRY_P
#define SUBTARGET_CARRY_P(REGNO) 0
#endif

#define GPR_P(REGNO)   (IN_RANGE_P ((REGNO), 0, 15) || SUBTARGET_GPR_P (REGNO))
#define ACCUM_P(REGNO) ((REGNO) == ACCUM_REGNUM || SUBTARGET_ACCUM_P (REGNO))
#define CARRY_P(REGNO) ((REGNO) == CARRY_REGNUM || SUBTARGET_CARRY_P (REGNO))

/* Eliminating the frame and arg pointers.  */

/* A C expression which is nonzero if a function must have and use a
   frame pointer.  This expression is evaluated in the reload pass.
   If its value is nonzero the function will have a frame pointer.  */
#define FRAME_POINTER_REQUIRED current_function_calls_alloca

#if 0
/* C statement to store the difference between the frame pointer
   and the stack pointer values immediately after the function prologue.
   If `ELIMINABLE_REGS' is defined, this macro will be not be used and
   need not be defined.  */
#define INITIAL_FRAME_POINTER_OFFSET(VAR) \
((VAR) = m32r_compute_frame_size (get_frame_size ()))
#endif

/* If defined, this macro specifies a table of register pairs used to
   eliminate unneeded registers that point into the stack frame.  If
   it is not defined, the only elimination attempted by the compiler
   is to replace references to the frame pointer with references to
   the stack pointer.

   Note that the elimination of the argument pointer with the stack
   pointer is specified first since that is the preferred elimination.  */

#define ELIMINABLE_REGS					\
{{ FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM },	\
 { ARG_POINTER_REGNUM,	 STACK_POINTER_REGNUM },	\
 { ARG_POINTER_REGNUM,   FRAME_POINTER_REGNUM }}

/* A C expression that returns nonzero if the compiler is allowed to
   try to replace register number FROM-REG with register number
   TO-REG.  This macro need only be defined if `ELIMINABLE_REGS' is
   defined, and will usually be the constant 1, since most of the
   cases preventing register elimination are things that the compiler
   already knows about.  */

#define CAN_ELIMINATE(FROM, TO)						\
  ((FROM) == ARG_POINTER_REGNUM && (TO) == STACK_POINTER_REGNUM		\
   ? ! frame_pointer_needed						\
   : 1)

/* This macro is similar to `INITIAL_FRAME_POINTER_OFFSET'.  It
   specifies the initial difference between the specified pair of
   registers.  This macro must be defined if `ELIMINABLE_REGS' is
   defined.  */

#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET)				\
  do										\
    {										\
      int size = m32r_compute_frame_size (get_frame_size ());			\
										\
      if ((FROM) == FRAME_POINTER_REGNUM && (TO) == STACK_POINTER_REGNUM)	\
	(OFFSET) = 0;								\
      else if ((FROM) == ARG_POINTER_REGNUM && (TO) == FRAME_POINTER_REGNUM)	\
	(OFFSET) = size - current_function_pretend_args_size;			\
      else if ((FROM) == ARG_POINTER_REGNUM && (TO) == STACK_POINTER_REGNUM)	\
	(OFFSET) = size - current_function_pretend_args_size;			\
      else									\
	gcc_unreachable ();								\
    }										\
  while (0)

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
#define M32R_MAX_PARM_REGS 4

/* 1 if N is a possible register number for function argument passing.  */
#define FUNCTION_ARG_REGNO_P(N) \
  ((unsigned) (N) < M32R_MAX_PARM_REGS)

/* The ROUND_ADVANCE* macros are local to this file.  */
/* Round SIZE up to a word boundary.  */
#define ROUND_ADVANCE(SIZE) \
  (((SIZE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* Round arg MODE/TYPE up to the next word boundary.  */
#define ROUND_ADVANCE_ARG(MODE, TYPE) \
  ((MODE) == BLKmode				\
   ? ROUND_ADVANCE ((unsigned int) int_size_in_bytes (TYPE))	\
   : ROUND_ADVANCE ((unsigned int) GET_MODE_SIZE (MODE)))

/* Round CUM up to the necessary point for argument MODE/TYPE.  */
#define ROUND_ADVANCE_CUM(CUM, MODE, TYPE) (CUM)

/* Return boolean indicating arg of type TYPE and mode MODE will be passed in
   a reg.  This includes arguments that have to be passed by reference as the
   pointer to them is passed in a reg if one is available (and that is what
   we're given).
   This macro is only used in this file.  */
#define PASS_IN_REG_P(CUM, MODE, TYPE) \
  (ROUND_ADVANCE_CUM ((CUM), (MODE), (TYPE)) < M32R_MAX_PARM_REGS)

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
/* On the M32R the first M32R_MAX_PARM_REGS args are normally in registers
   and the rest are pushed.  */
#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED) \
  (PASS_IN_REG_P ((CUM), (MODE), (TYPE))			\
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
#if 0
/* We assume PARM_BOUNDARY == UNITS_PER_WORD here.  */
#define FUNCTION_ARG_BOUNDARY(MODE, TYPE) \
  (((TYPE) ? TYPE_ALIGN (TYPE) : GET_MODE_BITSIZE (MODE)) <= PARM_BOUNDARY \
   ? PARM_BOUNDARY : 2 * PARM_BOUNDARY)
#endif

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

/* Function entry and exit.  */

/* Initialize data used by insn expanders.  This is called from
   init_emit, once for each function, before code is generated.  */
#define INIT_EXPANDERS m32r_init_expanders ()

/* EXIT_IGNORE_STACK should be nonzero if, when returning from a function,
   the stack pointer does not matter.  The value is tested only in
   functions that have frame pointers.
   No definition is equivalent to always zero.  */
#define EXIT_IGNORE_STACK 1

/* Output assembler code to FILE to increment profiler label # LABELNO
   for profiling a function entry.  */
#undef  FUNCTION_PROFILER
#define FUNCTION_PROFILER(FILE, LABELNO)			\
  do								\
    {								\
      if (flag_pic)						\
	{							\
	  fprintf (FILE, "\tld24 r14,#mcount\n");		\
	  fprintf (FILE, "\tadd r14,r12\n");			\
	  fprintf (FILE, "\tld r14,@r14\n");			\
	  fprintf (FILE, "\tjl r14\n");				\
	}							\
      else							\
	{							\
	  if (TARGET_ADDR24)					\
	    fprintf (FILE, "\tbl mcount\n");			\
	  else							\
	    {							\
	      fprintf (FILE, "\tseth r14,#high(mcount)\n");	\
	      fprintf (FILE, "\tor3 r14,r14,#low(mcount)\n");	\
	      fprintf (FILE, "\tjl r14\n");			\
	    }							\
	}							\
      fprintf (FILE, "\taddi sp,#4\n");				\
    }								\
  while (0)

/* Trampolines.  */

/* On the M32R, the trampoline is:

        mv      r7, lr   -> bl L1        ; 178e 7e01
L1:     add3    r6, lr, #L2-L1           ; 86ae 000c (L2 - L1 = 12)
        mv      lr, r7   -> ld r7,@r6+   ; 1e87 27e6
        ld      r6, @r6  -> jmp r6       ; 26c6 1fc6
L2:     .word STATIC
        .word FUNCTION  */

#ifndef CACHE_FLUSH_FUNC
#define CACHE_FLUSH_FUNC "_flush_cache"
#endif
#ifndef CACHE_FLUSH_TRAP
#define CACHE_FLUSH_TRAP 12
#endif

/* Length in bytes of the trampoline for entering a nested function.  */
#define TRAMPOLINE_SIZE 24

/* Emit RTL insns to initialize the variable parts of a trampoline.
   FNADDR is an RTX for the address of the function's pure code.
   CXT is an RTX for the static chain value for the function.  */
#define INITIALIZE_TRAMPOLINE(TRAMP, FNADDR, CXT) 				\
  do										\
    {										\
      emit_move_insn (gen_rtx_MEM (SImode, plus_constant (TRAMP, 0)),		\
		      GEN_INT							\
		      (TARGET_LITTLE_ENDIAN ? 0x017e8e17 : 0x178e7e01));	\
      emit_move_insn (gen_rtx_MEM (SImode, plus_constant (TRAMP, 4)),		\
		      GEN_INT							\
		      (TARGET_LITTLE_ENDIAN ? 0x0c00ae86 : 0x86ae000c));	\
      emit_move_insn (gen_rtx_MEM (SImode, plus_constant (TRAMP, 8)),		\
		      GEN_INT							\
		      (TARGET_LITTLE_ENDIAN ? 0xe627871e : 0x1e8727e6));	\
      emit_move_insn (gen_rtx_MEM (SImode, plus_constant (TRAMP, 12)),		\
		      GEN_INT							\
		      (TARGET_LITTLE_ENDIAN ? 0xc616c626 : 0x26c61fc6));	\
      emit_move_insn (gen_rtx_MEM (SImode, plus_constant (TRAMP, 16)),		\
		      (CXT));							\
      emit_move_insn (gen_rtx_MEM (SImode, plus_constant (TRAMP, 20)),		\
		      (FNADDR));						\
      if (m32r_cache_flush_trap >= 0)						\
	emit_insn (gen_flush_icache (validize_mem (gen_rtx_MEM (SImode, TRAMP)),\
				     GEN_INT (m32r_cache_flush_trap) ));	\
      else if (m32r_cache_flush_func && m32r_cache_flush_func[0])		\
	emit_library_call (m32r_function_symbol (m32r_cache_flush_func), 	\
			   0, VOIDmode, 3, TRAMP, Pmode,			\
			   GEN_INT (TRAMPOLINE_SIZE), SImode,			\
			   GEN_INT (3), SImode);				\
    }										\
  while (0)

#define RETURN_ADDR_RTX(COUNT, FRAME) m32r_return_addr (COUNT)

#define INCOMING_RETURN_ADDR_RTX   gen_rtx_REG (Pmode, RETURN_ADDR_REGNUM)

/* Addressing modes, and classification of registers for them.  */

/* Maximum number of registers that can appear in a valid memory address.  */
#define MAX_REGS_PER_ADDRESS 1

/* We have post-inc load and pre-dec,pre-inc store,
   but only for 4 byte vals.  */
#define HAVE_PRE_DECREMENT  1
#define HAVE_PRE_INCREMENT  1
#define HAVE_POST_INCREMENT 1

/* Recognize any constant value that is a valid address.  */
#define CONSTANT_ADDRESS_P(X)   \
  (    GET_CODE (X) == LABEL_REF  \
   ||  GET_CODE (X) == SYMBOL_REF \
   ||  GET_CODE (X) == CONST_INT  \
   || (GET_CODE (X) == CONST      \
       && ! (flag_pic && ! m32r_legitimate_pic_operand_p (X))))

/* Nonzero if the constant value X is a legitimate general operand.
   We don't allow (plus symbol large-constant) as the relocations can't
   describe it.  INTVAL > 32767 handles both 16 bit and 24 bit relocations.
   We allow all CONST_DOUBLE's as the md file patterns will force the
   constant to memory if they can't handle them.  */

#define LEGITIMATE_CONSTANT_P(X)					\
  (! (GET_CODE (X) == CONST						\
      && GET_CODE (XEXP (X, 0)) == PLUS					\
      && GET_CODE (XEXP (XEXP (X, 0), 0)) == SYMBOL_REF			\
      && GET_CODE (XEXP (XEXP (X, 0), 1)) == CONST_INT			\
      && (unsigned HOST_WIDE_INT) INTVAL (XEXP (XEXP (X, 0), 1)) > 32767))

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

#ifdef REG_OK_STRICT

/* Nonzero if X is a hard reg that can be used as a base reg.  */
#define REG_OK_FOR_BASE_P(X) GPR_P (REGNO (X))
/* Nonzero if X is a hard reg that can be used as an index.  */
#define REG_OK_FOR_INDEX_P(X) REG_OK_FOR_BASE_P (X)

#else

/* Nonzero if X is a hard reg that can be used as a base reg
   or if it is a pseudo reg.  */
#define REG_OK_FOR_BASE_P(X)		\
  (GPR_P (REGNO (X))			\
   || (REGNO (X)) == ARG_POINTER_REGNUM	\
   || REGNO (X) >= FIRST_PSEUDO_REGISTER)
/* Nonzero if X is a hard reg that can be used as an index
   or if it is a pseudo reg.  */
#define REG_OK_FOR_INDEX_P(X) REG_OK_FOR_BASE_P (X)

#endif

/* GO_IF_LEGITIMATE_ADDRESS recognizes an RTL expression
   that is a valid memory address for an instruction.
   The MODE argument is the machine mode for the MEM expression
   that wants to use this address.  */

/* Local to this file.  */
#define RTX_OK_FOR_BASE_P(X) (REG_P (X) && REG_OK_FOR_BASE_P (X))

/* Local to this file.  */
#define RTX_OK_FOR_OFFSET_P(X) \
  (GET_CODE (X) == CONST_INT && INT16_P (INTVAL (X)))

/* Local to this file.  */
#define LEGITIMATE_OFFSET_ADDRESS_P(MODE, X)			\
  (GET_CODE (X) == PLUS						\
   && RTX_OK_FOR_BASE_P (XEXP (X, 0))				\
   && RTX_OK_FOR_OFFSET_P (XEXP (X, 1)))

/* Local to this file.  */
/* For LO_SUM addresses, do not allow them if the MODE is > 1 word,
   since more than one instruction will be required.  */
#define LEGITIMATE_LO_SUM_ADDRESS_P(MODE, X)			\
  (GET_CODE (X) == LO_SUM					\
   && (MODE != BLKmode && GET_MODE_SIZE (MODE) <= UNITS_PER_WORD)\
   && RTX_OK_FOR_BASE_P (XEXP (X, 0))				\
   && CONSTANT_P (XEXP (X, 1)))

/* Local to this file.  */
/* Is this a load and increment operation.  */
#define LOAD_POSTINC_P(MODE, X)					\
  (((MODE) == SImode || (MODE) == SFmode)			\
   && GET_CODE (X) == POST_INC					\
   && GET_CODE (XEXP (X, 0)) == REG				\
   && RTX_OK_FOR_BASE_P (XEXP (X, 0)))

/* Local to this file.  */
/* Is this an increment/decrement and store operation.  */
#define STORE_PREINC_PREDEC_P(MODE, X)				\
  (((MODE) == SImode || (MODE) == SFmode)			\
   && (GET_CODE (X) == PRE_INC || GET_CODE (X) == PRE_DEC)	\
   && GET_CODE (XEXP (X, 0)) == REG				\
   && RTX_OK_FOR_BASE_P (XEXP (X, 0)))

#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR)			\
  do								\
    {								\
      if (RTX_OK_FOR_BASE_P (X))				\
	goto ADDR;						\
      if (LEGITIMATE_OFFSET_ADDRESS_P ((MODE), (X)))		\
	goto ADDR;						\
      if (LEGITIMATE_LO_SUM_ADDRESS_P ((MODE), (X)))		\
	goto ADDR;						\
      if (LOAD_POSTINC_P ((MODE), (X)))				\
	goto ADDR;						\
      if (STORE_PREINC_PREDEC_P ((MODE), (X)))			\
	goto ADDR;						\
    }								\
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

#define LEGITIMIZE_ADDRESS(X, OLDX, MODE, WIN)			 \
  do								 \
    {								 \
      if (flag_pic)						 \
	(X) = m32r_legitimize_pic_address (X, NULL_RTX);	 \
      if (memory_address_p (MODE, X))				 \
	goto WIN;						 \
    }								 \
  while (0)

/* Go to LABEL if ADDR (a legitimate address expression)
   has an effect that depends on the machine mode it is used for.  */
#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR, LABEL)		\
  do								\
    {						 		\
      if (   GET_CODE (ADDR) == PRE_DEC		 		\
	  || GET_CODE (ADDR) == PRE_INC		 		\
	  || GET_CODE (ADDR) == POST_INC		 	\
	  || GET_CODE (ADDR) == LO_SUM)		 		\
	goto LABEL;					 	\
    }								\
  while (0)

/* Condition code usage.  */

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
#define MEMORY_MOVE_COST(MODE,CLASS,IN_P) \
(GET_MODE_SIZE (MODE) <= UNITS_PER_WORD ? 6 : 12)

/* The cost of a branch insn.  */
/* A value of 2 here causes GCC to avoid using branches in comparisons like
   while (a < N && a).  Branches aren't that expensive on the M32R so
   we define this as 1.  Defining it as 2 had a heavy hit in fp-bit.c.  */
#define BRANCH_COST ((TARGET_BRANCH_COST) ? 2 : 1)

/* Nonzero if access to memory by bytes is slow and undesirable.
   For RISC chips, it means that access to memory by bytes is no
   better than access by words when possible, so grab a whole word
   and maybe make use of that.  */
#define SLOW_BYTE_ACCESS 1

/* Define this macro if it is as good or better to call a constant
   function address than to call an address kept in a register.  */
#define NO_FUNCTION_CSE

/* Section selection.  */

#define TEXT_SECTION_ASM_OP	"\t.section .text"
#define DATA_SECTION_ASM_OP	"\t.section .data"
#define BSS_SECTION_ASM_OP	"\t.section .bss"

/* Define this macro if jump tables (for tablejump insns) should be
   output in the text section, along with the assembler instructions.
   Otherwise, the readonly data section is used.
   This macro is irrelevant if there is no separate readonly data section.  */
#define JUMP_TABLES_IN_TEXT_SECTION (flag_pic)

/* Position Independent Code.  */

/* The register number of the register used to address a table of static
   data addresses in memory.  In some cases this register is defined by a
   processor's ``application binary interface'' (ABI).  When this macro
   is defined, RTL is generated for this register once, as with the stack
   pointer and frame pointer registers.  If this macro is not defined, it
   is up to the machine-dependent files to allocate such a register (if
   necessary).  */
#define PIC_OFFSET_TABLE_REGNUM 12

/* Define this macro if the register defined by PIC_OFFSET_TABLE_REGNUM is
   clobbered by calls.  Do not define this macro if PIC_OFFSET_TABLE_REGNUM
   is not defined.  */
/* This register is call-saved on the M32R.  */
/*#define PIC_OFFSET_TABLE_REG_CALL_CLOBBERED*/

/* A C expression that is nonzero if X is a legitimate immediate
   operand on the target machine when generating position independent code.
   You can assume that X satisfies CONSTANT_P, so you need not
   check this.  You can also assume `flag_pic' is true, so you need not
   check it either.  You need not define this macro if all constants
   (including SYMBOL_REF) can be immediate operands when generating
   position independent code.  */
#define LEGITIMATE_PIC_OPERAND_P(X) m32r_legitimate_pic_operand_p (X)

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

/* We do not use DBX_LINES_FUNCTION_RELATIVE or
   dbxout_stab_value_internal_label_diff here because
   we need to use .debugsym for the line label.  */

#define DBX_OUTPUT_SOURCE_LINE(file, line, counter)			\
  do									\
    {									\
      const char * begin_label =					\
	XSTR (XEXP (DECL_RTL (current_function_decl), 0), 0);		\
      char label[64];							\
      ASM_GENERATE_INTERNAL_LABEL (label, "LM", counter);		\
									\
      dbxout_begin_stabn_sline (line);					\
      assemble_name (file, label);					\
      putc ('-', file);							\
      assemble_name (file, begin_label);				\
      fputs ("\n\t.debugsym ", file);					\
      assemble_name (file, label);					\
      putc ('\n', file);						\
      counter += 1;							\
     }									\
  while (0)

/* How to refer to registers in assembler output.
   This sequence is indexed by compiler's hard-register-number (see above).  */
#ifndef SUBTARGET_REGISTER_NAMES
#define SUBTARGET_REGISTER_NAMES
#endif

#define REGISTER_NAMES					\
{							\
  "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",	\
  "r8", "r9", "r10", "r11", "r12", "fp", "lr", "sp",	\
  "ap", "cbit", "a0"					\
  SUBTARGET_REGISTER_NAMES				\
}

/* If defined, a C initializer for an array of structures containing
   a name and a register number.  This macro defines additional names
   for hard registers, thus allowing the `asm' option in declarations
   to refer to registers using alternate names.  */
#ifndef SUBTARGET_ADDITIONAL_REGISTER_NAMES
#define SUBTARGET_ADDITIONAL_REGISTER_NAMES
#endif

#define ADDITIONAL_REGISTER_NAMES	\
{					\
  /*{ "gp", GP_REGNUM },*/		\
  { "r13", FRAME_POINTER_REGNUM },	\
  { "r14", RETURN_ADDR_REGNUM },	\
  { "r15", STACK_POINTER_REGNUM },	\
  SUBTARGET_ADDITIONAL_REGISTER_NAMES	\
}

/* A C expression which evaluates to true if CODE is a valid
   punctuation character for use in the `PRINT_OPERAND' macro.  */
extern char m32r_punct_chars[256];
#define PRINT_OPERAND_PUNCT_VALID_P(CHAR) \
  m32r_punct_chars[(unsigned char) (CHAR)]

/* Print operand X (an rtx) in assembler syntax to file FILE.
   CODE is a letter or dot (`z' in `%z0') or 0 if no letter was specified.
   For `%' followed by punctuation, CODE is the punctuation and X is null.  */
#define PRINT_OPERAND(FILE, X, CODE) \
  m32r_print_operand (FILE, X, CODE)

/* A C compound statement to output to stdio stream STREAM the
   assembler syntax for an instruction operand that is a memory
   reference whose address is ADDR.  ADDR is an RTL expression.  */
#define PRINT_OPERAND_ADDRESS(FILE, ADDR) \
  m32r_print_operand_address (FILE, ADDR)

/* If defined, C string expressions to be used for the `%R', `%L',
   `%U', and `%I' options of `asm_fprintf' (see `final.c').  These
   are useful when a single `md' file must support multiple assembler
   formats.  In that case, the various `tm.h' files can define these
   macros differently.  */
#define REGISTER_PREFIX		""
#define LOCAL_LABEL_PREFIX	".L"
#define USER_LABEL_PREFIX	""
#define IMMEDIATE_PREFIX	"#"

/* This is how to output an element of a case-vector that is absolute.  */
#define ASM_OUTPUT_ADDR_VEC_ELT(FILE, VALUE)		\
   do							\
     {							\
       char label[30];					\
       ASM_GENERATE_INTERNAL_LABEL (label, "L", VALUE);	\
       fprintf (FILE, "\t.word\t");			\
       assemble_name (FILE, label);			\
       fprintf (FILE, "\n");				\
     }							\
  while (0)

/* This is how to output an element of a case-vector that is relative.  */
#define ASM_OUTPUT_ADDR_DIFF_ELT(FILE, BODY, VALUE, REL)\
  do							\
    {							\
      char label[30];					\
      ASM_GENERATE_INTERNAL_LABEL (label, "L", VALUE);	\
      fprintf (FILE, "\t.word\t");			\
      assemble_name (FILE, label);			\
      fprintf (FILE, "-");				\
      ASM_GENERATE_INTERNAL_LABEL (label, "L", REL);	\
      assemble_name (FILE, label);			\
      fprintf (FILE, "\n");				\
    }							\
  while (0)

/* The desired alignment for the location counter at the beginning
   of a loop.  */
/* On the M32R, align loops to 32 byte boundaries (cache line size)
   if -malign-loops.  */
#define LOOP_ALIGN(LABEL) (TARGET_ALIGN_LOOPS ? 5 : 0)

/* Define this to be the maximum number of insns to move around when moving
   a loop test from the top of a loop to the bottom
   and seeing whether to duplicate it.  The default is thirty.

   Loop unrolling currently doesn't like this optimization, so
   disable doing if we are unrolling loops and saving space.  */
#define LOOP_TEST_THRESHOLD (optimize_size				\
			     && !flag_unroll_loops			\
			     && !flag_unroll_all_loops ? 2 : 30)

/* This is how to output an assembler line
   that says to advance the location counter
   to a multiple of 2**LOG bytes.  */
/* .balign is used to avoid confusion.  */
#define ASM_OUTPUT_ALIGN(FILE,LOG)			\
  do							\
    {							\
      if ((LOG) != 0)					\
	fprintf (FILE, "\t.balign %d\n", 1 << (LOG));	\
    }							\
  while (0)

/* Like `ASM_OUTPUT_COMMON' except takes the required alignment as a
   separate, explicit argument.  If you define this macro, it is used in
   place of `ASM_OUTPUT_COMMON', and gives you more flexibility in
   handling the required alignment of the variable.  The alignment is
   specified as the number of bits.  */

#define SCOMMON_ASM_OP "\t.scomm\t"

#undef  ASM_OUTPUT_ALIGNED_COMMON
#define ASM_OUTPUT_ALIGNED_COMMON(FILE, NAME, SIZE, ALIGN)		\
  do									\
    {									\
      if (! TARGET_SDATA_NONE						\
	  && (SIZE) > 0 && (SIZE) <= g_switch_value)			\
	fprintf ((FILE), "%s", SCOMMON_ASM_OP);				\
      else								\
	fprintf ((FILE), "%s", COMMON_ASM_OP);				\
      assemble_name ((FILE), (NAME));					\
      fprintf ((FILE), ",%u,%u\n", (int)(SIZE), (ALIGN) / BITS_PER_UNIT);\
    }									\
  while (0)

#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN)		\
  do									\
    {									\
      if (! TARGET_SDATA_NONE						\
          && (SIZE) > 0 && (SIZE) <= g_switch_value)			\
        switch_to_section (get_named_section (NULL, ".sbss", 0));	\
      else								\
        switch_to_section (bss_section);				\
      ASM_OUTPUT_ALIGN (FILE, floor_log2 (ALIGN / BITS_PER_UNIT));	\
      last_assemble_variable_decl = DECL;				\
      ASM_DECLARE_OBJECT_NAME (FILE, NAME, DECL);			\
      ASM_OUTPUT_SKIP (FILE, SIZE ? SIZE : 1);				\
    }									\
  while (0)

/* Debugging information.  */

/* Generate DBX and DWARF debugging information.  */
#define DBX_DEBUGGING_INFO    1
#define DWARF2_DEBUGGING_INFO 1

/* Use DWARF2 debugging info by default.  */
#undef  PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG

/* Turn off splitting of long stabs.  */
#define DBX_CONTIN_LENGTH 0

/* Miscellaneous.  */

/* Specify the machine mode that this machine uses
   for the index in the tablejump instruction.  */
#define CASE_VECTOR_MODE (flag_pic ? SImode : Pmode)

/* Define if operations between registers always perform the operation
   on the full register even if a narrower mode is specified.  */
#define WORD_REGISTER_OPERATIONS

/* Define if loading in MODE, an integral mode narrower than BITS_PER_WORD
   will either zero-extend or sign-extend.  The value of this macro should
   be the code that says which one of the two operations is implicitly
   done, UNKNOWN if none.  */
#define LOAD_EXTEND_OP(MODE) ZERO_EXTEND

/* Max number of bytes we can move from memory
   to memory in one reasonably fast instruction.  */
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
/* ??? The M32R doesn't have full 32 bit pointers, but making this PSImode has
   its own problems (you have to add extendpsisi2 and truncsipsi2).
   Try to avoid it.  */
#define Pmode SImode

/* A function address in a call instruction.  */
#define FUNCTION_MODE SImode

/* Define the information needed to generate branch and scc insns.  This is
   stored from the compare operation.  Note that we can't use "rtx" here
   since it hasn't been defined!  */
extern struct rtx_def * m32r_compare_op0;
extern struct rtx_def * m32r_compare_op1;

/* M32R function types.  */
enum m32r_function_type
{
  M32R_FUNCTION_UNKNOWN, M32R_FUNCTION_NORMAL, M32R_FUNCTION_INTERRUPT
};

#define M32R_INTERRUPT_P(TYPE) ((TYPE) == M32R_FUNCTION_INTERRUPT)

/* The maximum number of bytes to copy using pairs of load/store instructions.
   If a block is larger than this then a loop will be generated to copy
   MAX_MOVE_BYTES chunks at a time.  The value of 32 is a semi-arbitrary choice.
   A customer uses Dhrystome as their benchmark, and Dhrystone has a 31 byte
   string copy in it.  */
#define MAX_MOVE_BYTES 32
