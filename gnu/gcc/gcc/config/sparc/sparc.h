/* Definitions of target machine for GNU compiler, for Sun SPARC.
   Copyright (C) 1987, 1988, 1989, 1992, 1994, 1995, 1996, 1997, 1998, 1999
   2000, 2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Michael Tiemann (tiemann@cygnus.com).
   64-bit SPARC-V9 support by Michael Tiemann, Jim Wilson, and Doug Evans,
   at Cygnus Support.

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

/* Note that some other tm.h files include this one and then override
   whatever definitions are necessary.  */

/* Define the specific costs for a given cpu */

struct processor_costs {
  /* Integer load */
  const int int_load;

  /* Integer signed load */
  const int int_sload;

  /* Integer zeroed load */
  const int int_zload;

  /* Float load */
  const int float_load;

  /* fmov, fneg, fabs */
  const int float_move;

  /* fadd, fsub */
  const int float_plusminus;

  /* fcmp */
  const int float_cmp;

  /* fmov, fmovr */
  const int float_cmove;

  /* fmul */
  const int float_mul;

  /* fdivs */
  const int float_div_sf;

  /* fdivd */
  const int float_div_df;

  /* fsqrts */
  const int float_sqrt_sf;

  /* fsqrtd */
  const int float_sqrt_df;

  /* umul/smul */
  const int int_mul;

  /* mulX */
  const int int_mulX;

  /* integer multiply cost for each bit set past the most
     significant 3, so the formula for multiply cost becomes:

	if (rs1 < 0)
	  highest_bit = highest_clear_bit(rs1);
	else
	  highest_bit = highest_set_bit(rs1);
	if (highest_bit < 3)
	  highest_bit = 3;
	cost = int_mul{,X} + ((highest_bit - 3) / int_mul_bit_factor);

     A value of zero indicates that the multiply costs is fixed,
     and not variable.  */
  const int int_mul_bit_factor;

  /* udiv/sdiv */
  const int int_div;

  /* divX */
  const int int_divX;

  /* movcc, movr */
  const int int_cmove;

  /* penalty for shifts, due to scheduling rules etc. */
  const int shift_penalty;
};

extern const struct processor_costs *sparc_costs;

/* Target CPU builtins.  FIXME: Defining sparc is for the benefit of
   Solaris only; otherwise just define __sparc__.  Sadly the headers
   are such a mess there is no Solaris-specific header.  */
#define TARGET_CPU_CPP_BUILTINS()		\
  do						\
    {						\
	builtin_define_std ("sparc");		\
	if (TARGET_64BIT)			\
	  { 					\
	    builtin_assert ("cpu=sparc64");	\
	    builtin_assert ("machine=sparc64");	\
	  }					\
	else					\
	  { 					\
	    builtin_assert ("cpu=sparc");	\
	    builtin_assert ("machine=sparc");	\
	  }					\
    }						\
  while (0)

/* Specify this in a cover file to provide bi-architecture (32/64) support.  */
/* #define SPARC_BI_ARCH */

/* Macro used later in this file to determine default architecture.  */
#define DEFAULT_ARCH32_P ((TARGET_DEFAULT & MASK_64BIT) == 0)

/* TARGET_ARCH{32,64} are the main macros to decide which of the two
   architectures to compile for.  We allow targets to choose compile time or
   runtime selection.  */
#ifdef IN_LIBGCC2
#if defined(__sparcv9) || defined(__arch64__)
#define TARGET_ARCH32 0
#else
#define TARGET_ARCH32 1
#endif /* sparc64 */
#else
#ifdef SPARC_BI_ARCH
#define TARGET_ARCH32 (! TARGET_64BIT)
#else
#define TARGET_ARCH32 (DEFAULT_ARCH32_P)
#endif /* SPARC_BI_ARCH */
#endif /* IN_LIBGCC2 */
#define TARGET_ARCH64 (! TARGET_ARCH32)

/* Code model selection in 64-bit environment.

   The machine mode used for addresses is 32-bit wide:

   TARGET_CM_32:     32-bit address space.
                     It is the code model used when generating 32-bit code.

   The machine mode used for addresses is 64-bit wide:

   TARGET_CM_MEDLOW: 32-bit address space.
                     The executable must be in the low 32 bits of memory.
                     This avoids generating %uhi and %ulo terms.  Programs
                     can be statically or dynamically linked.

   TARGET_CM_MEDMID: 44-bit address space.
                     The executable must be in the low 44 bits of memory,
                     and the %[hml]44 terms are used.  The text and data
                     segments have a maximum size of 2GB (31-bit span).
                     The maximum offset from any instruction to the label
                     _GLOBAL_OFFSET_TABLE_ is 2GB (31-bit span).

   TARGET_CM_MEDANY: 64-bit address space.
                     The text and data segments have a maximum size of 2GB
                     (31-bit span) and may be located anywhere in memory.
                     The maximum offset from any instruction to the label
                     _GLOBAL_OFFSET_TABLE_ is 2GB (31-bit span).

   TARGET_CM_EMBMEDANY: 64-bit address space.
                     The text and data segments have a maximum size of 2GB
                     (31-bit span) and may be located anywhere in memory.
                     The global register %g4 contains the start address of
                     the data segment.  Programs are statically linked and
                     PIC is not supported.

   Different code models are not supported in 32-bit environment.  */

enum cmodel {
  CM_32,
  CM_MEDLOW,
  CM_MEDMID,
  CM_MEDANY,
  CM_EMBMEDANY
};

/* One of CM_FOO.  */
extern enum cmodel sparc_cmodel;

/* V9 code model selection.  */
#define TARGET_CM_MEDLOW    (sparc_cmodel == CM_MEDLOW)
#define TARGET_CM_MEDMID    (sparc_cmodel == CM_MEDMID)
#define TARGET_CM_MEDANY    (sparc_cmodel == CM_MEDANY)
#define TARGET_CM_EMBMEDANY (sparc_cmodel == CM_EMBMEDANY)

#define SPARC_DEFAULT_CMODEL CM_32

/* The SPARC-V9 architecture defines a relaxed memory ordering model (RMO)
   which requires the following macro to be true if enabled.  Prior to V9,
   there are no instructions to even talk about memory synchronization.
   Note that the UltraSPARC III processors don't implement RMO, unlike the
   UltraSPARC II processors.  Niagara does not implement RMO either.

   Default to false; for example, Solaris never enables RMO, only ever uses
   total memory ordering (TMO).  */
#define SPARC_RELAXED_ORDERING false

/* Do not use the .note.GNU-stack convention by default.  */
#define NEED_INDICATE_EXEC_STACK 0

/* This is call-clobbered in the normal ABI, but is reserved in the
   home grown (aka upward compatible) embedded ABI.  */
#define EMBMEDANY_BASE_REG "%g4"

/* Values of TARGET_CPU_DEFAULT, set via -D in the Makefile,
   and specified by the user via --with-cpu=foo.
   This specifies the cpu implementation, not the architecture size.  */
/* Note that TARGET_CPU_v9 is assumed to start the list of 64-bit
   capable cpu's.  */
#define TARGET_CPU_sparc	0
#define TARGET_CPU_v7		0	/* alias for previous */
#define TARGET_CPU_sparclet	1
#define TARGET_CPU_sparclite	2
#define TARGET_CPU_v8		3	/* generic v8 implementation */
#define TARGET_CPU_supersparc	4
#define TARGET_CPU_hypersparc   5
#define TARGET_CPU_sparc86x	6
#define TARGET_CPU_sparclite86x	6
#define TARGET_CPU_v9		7	/* generic v9 implementation */
#define TARGET_CPU_sparcv9	7	/* alias */
#define TARGET_CPU_sparc64	7	/* alias */
#define TARGET_CPU_ultrasparc	8
#define TARGET_CPU_ultrasparc3	9
#define TARGET_CPU_niagara	10

#if TARGET_CPU_DEFAULT == TARGET_CPU_v9 \
 || TARGET_CPU_DEFAULT == TARGET_CPU_ultrasparc \
 || TARGET_CPU_DEFAULT == TARGET_CPU_ultrasparc3 \
 || TARGET_CPU_DEFAULT == TARGET_CPU_niagara

#define CPP_CPU32_DEFAULT_SPEC ""
#define ASM_CPU32_DEFAULT_SPEC ""

#if TARGET_CPU_DEFAULT == TARGET_CPU_v9
/* ??? What does Sun's CC pass?  */
#define CPP_CPU64_DEFAULT_SPEC "-D__sparc_v9__"
/* ??? It's not clear how other assemblers will handle this, so by default
   use GAS.  Sun's Solaris assembler recognizes -xarch=v8plus, but this case
   is handled in sol2.h.  */
#define ASM_CPU64_DEFAULT_SPEC "-Av9"
#endif
#if TARGET_CPU_DEFAULT == TARGET_CPU_ultrasparc
#define CPP_CPU64_DEFAULT_SPEC "-D__sparc_v9__"
#define ASM_CPU64_DEFAULT_SPEC "-Av9a"
#endif
#if TARGET_CPU_DEFAULT == TARGET_CPU_ultrasparc3
#define CPP_CPU64_DEFAULT_SPEC "-D__sparc_v9__"
#define ASM_CPU64_DEFAULT_SPEC "-Av9b"
#endif
#if TARGET_CPU_DEFAULT == TARGET_CPU_niagara
#define CPP_CPU64_DEFAULT_SPEC "-D__sparc_v9__"
#define ASM_CPU64_DEFAULT_SPEC "-Av9b"
#endif

#else

#define CPP_CPU64_DEFAULT_SPEC ""
#define ASM_CPU64_DEFAULT_SPEC ""

#if TARGET_CPU_DEFAULT == TARGET_CPU_sparc \
 || TARGET_CPU_DEFAULT == TARGET_CPU_v8
#define CPP_CPU32_DEFAULT_SPEC ""
#define ASM_CPU32_DEFAULT_SPEC ""
#endif

#if TARGET_CPU_DEFAULT == TARGET_CPU_sparclet
#define CPP_CPU32_DEFAULT_SPEC "-D__sparclet__"
#define ASM_CPU32_DEFAULT_SPEC "-Asparclet"
#endif

#if TARGET_CPU_DEFAULT == TARGET_CPU_sparclite
#define CPP_CPU32_DEFAULT_SPEC "-D__sparclite__"
#define ASM_CPU32_DEFAULT_SPEC "-Asparclite"
#endif

#if TARGET_CPU_DEFAULT == TARGET_CPU_supersparc
#define CPP_CPU32_DEFAULT_SPEC "-D__supersparc__ -D__sparc_v8__"
#define ASM_CPU32_DEFAULT_SPEC ""
#endif

#if TARGET_CPU_DEFAULT == TARGET_CPU_hypersparc
#define CPP_CPU32_DEFAULT_SPEC "-D__hypersparc__ -D__sparc_v8__"
#define ASM_CPU32_DEFAULT_SPEC ""
#endif

#if TARGET_CPU_DEFAULT == TARGET_CPU_sparclite86x
#define CPP_CPU32_DEFAULT_SPEC "-D__sparclite86x__"
#define ASM_CPU32_DEFAULT_SPEC "-Asparclite"
#endif

#endif

#if !defined(CPP_CPU32_DEFAULT_SPEC) || !defined(CPP_CPU64_DEFAULT_SPEC)
 #error Unrecognized value in TARGET_CPU_DEFAULT.
#endif

#ifdef SPARC_BI_ARCH

#define CPP_CPU_DEFAULT_SPEC \
(DEFAULT_ARCH32_P ? "\
%{m64:" CPP_CPU64_DEFAULT_SPEC "} \
%{!m64:" CPP_CPU32_DEFAULT_SPEC "} \
" : "\
%{m32:" CPP_CPU32_DEFAULT_SPEC "} \
%{!m32:" CPP_CPU64_DEFAULT_SPEC "} \
")
#define ASM_CPU_DEFAULT_SPEC \
(DEFAULT_ARCH32_P ? "\
%{m64:" ASM_CPU64_DEFAULT_SPEC "} \
%{!m64:" ASM_CPU32_DEFAULT_SPEC "} \
" : "\
%{m32:" ASM_CPU32_DEFAULT_SPEC "} \
%{!m32:" ASM_CPU64_DEFAULT_SPEC "} \
")

#else /* !SPARC_BI_ARCH */

#define CPP_CPU_DEFAULT_SPEC (DEFAULT_ARCH32_P ? CPP_CPU32_DEFAULT_SPEC : CPP_CPU64_DEFAULT_SPEC)
#define ASM_CPU_DEFAULT_SPEC (DEFAULT_ARCH32_P ? ASM_CPU32_DEFAULT_SPEC : ASM_CPU64_DEFAULT_SPEC)

#endif /* !SPARC_BI_ARCH */

/* Define macros to distinguish architectures.  */

/* Common CPP definitions used by CPP_SPEC amongst the various targets
   for handling -mcpu=xxx switches.  */
#define CPP_CPU_SPEC "\
%{msoft-float:-D_SOFT_FLOAT} \
%{mcypress:} \
%{msparclite:-D__sparclite__} \
%{mf930:-D__sparclite__} %{mf934:-D__sparclite__} \
%{mv8:-D__sparc_v8__} \
%{msupersparc:-D__supersparc__ -D__sparc_v8__} \
%{mcpu=sparclet:-D__sparclet__} %{mcpu=tsc701:-D__sparclet__} \
%{mcpu=sparclite:-D__sparclite__} \
%{mcpu=f930:-D__sparclite__} %{mcpu=f934:-D__sparclite__} \
%{mcpu=v8:-D__sparc_v8__} \
%{mcpu=supersparc:-D__supersparc__ -D__sparc_v8__} \
%{mcpu=hypersparc:-D__hypersparc__ -D__sparc_v8__} \
%{mcpu=sparclite86x:-D__sparclite86x__} \
%{mcpu=v9:-D__sparc_v9__} \
%{mcpu=ultrasparc:-D__sparc_v9__} \
%{mcpu=ultrasparc3:-D__sparc_v9__} \
%{mcpu=niagara:-D__sparc_v9__} \
%{!mcpu*:%{!mcypress:%{!msparclite:%{!mf930:%{!mf934:%{!mv8:%{!msupersparc:%(cpp_cpu_default)}}}}}}} \
"
#define CPP_ARCH32_SPEC ""
#define CPP_ARCH64_SPEC "-D__arch64__"

#define CPP_ARCH_DEFAULT_SPEC \
(DEFAULT_ARCH32_P ? CPP_ARCH32_SPEC : CPP_ARCH64_SPEC)

#define CPP_ARCH_SPEC "\
%{m32:%(cpp_arch32)} \
%{m64:%(cpp_arch64)} \
%{!m32:%{!m64:%(cpp_arch_default)}} \
"

/* Macros to distinguish endianness.  */
#define CPP_ENDIAN_SPEC "\
%{mlittle-endian:-D__LITTLE_ENDIAN__} \
%{mlittle-endian-data:-D__LITTLE_ENDIAN_DATA__}"

/* Macros to distinguish the particular subtarget.  */
#define CPP_SUBTARGET_SPEC ""

#define CPP_SPEC "%(cpp_cpu) %(cpp_arch) %(cpp_endian) %(cpp_subtarget)"

/* Prevent error on `-sun4' and `-target sun4' options.  */
/* This used to translate -dalign to -malign, but that is no good
   because it can't turn off the usual meaning of making debugging dumps.  */
/* Translate old style -m<cpu> into new style -mcpu=<cpu>.
   ??? Delete support for -m<cpu> for 2.9.  */

#define CC1_SPEC "\
%{sun4:} %{target:} \
%{mcypress:-mcpu=cypress} \
%{msparclite:-mcpu=sparclite} %{mf930:-mcpu=f930} %{mf934:-mcpu=f934} \
%{mv8:-mcpu=v8} %{msupersparc:-mcpu=supersparc} \
"

/* Override in target specific files.  */
#define ASM_CPU_SPEC "\
%{mcpu=sparclet:-Asparclet} %{mcpu=tsc701:-Asparclet} \
%{msparclite:-Asparclite} \
%{mf930:-Asparclite} %{mf934:-Asparclite} \
%{mcpu=sparclite:-Asparclite} \
%{mcpu=sparclite86x:-Asparclite} \
%{mcpu=f930:-Asparclite} %{mcpu=f934:-Asparclite} \
%{mv8plus:-Av8plus} \
%{mcpu=v9:-Av9} \
%{mcpu=ultrasparc:%{!mv8plus:-Av9a}} \
%{mcpu=ultrasparc3:%{!mv8plus:-Av9b}} \
%{mcpu=niagara:%{!mv8plus:-Av9b}} \
%{!mcpu*:%{!mcypress:%{!msparclite:%{!mf930:%{!mf934:%{!mv8:%{!msupersparc:%(asm_cpu_default)}}}}}}} \
"

/* Word size selection, among other things.
   This is what GAS uses.  Add %(asm_arch) to ASM_SPEC to enable.  */

#define ASM_ARCH32_SPEC "-32"
#ifdef HAVE_AS_REGISTER_PSEUDO_OP
#define ASM_ARCH64_SPEC "-64 -no-undeclared-regs"
#else
#define ASM_ARCH64_SPEC "-64"
#endif
#define ASM_ARCH_DEFAULT_SPEC \
(DEFAULT_ARCH32_P ? ASM_ARCH32_SPEC : ASM_ARCH64_SPEC)

#define ASM_ARCH_SPEC "\
%{m32:%(asm_arch32)} \
%{m64:%(asm_arch64)} \
%{!m32:%{!m64:%(asm_arch_default)}} \
"

#ifdef HAVE_AS_RELAX_OPTION
#define ASM_RELAX_SPEC "%{!mno-relax:-relax}"
#else
#define ASM_RELAX_SPEC ""
#endif

/* Special flags to the Sun-4 assembler when using pipe for input.  */

#define ASM_SPEC "\
%{R} %{!pg:%{!p:%{fpic|fPIC|fpie|fPIE:-k}}} %{keep-local-as-symbols:-L} \
%(asm_cpu) %(asm_relax)"

#define AS_NEEDS_DASH_FOR_PIPED_INPUT

/* This macro defines names of additional specifications to put in the specs
   that can be used in various specifications like CC1_SPEC.  Its definition
   is an initializer with a subgrouping for each command option.

   Each subgrouping contains a string constant, that defines the
   specification name, and a string constant that used by the GCC driver
   program.

   Do not define this macro if it does not need to do anything.  */

#define EXTRA_SPECS \
  { "cpp_cpu",		CPP_CPU_SPEC },		\
  { "cpp_cpu_default",	CPP_CPU_DEFAULT_SPEC },	\
  { "cpp_arch32",	CPP_ARCH32_SPEC },	\
  { "cpp_arch64",	CPP_ARCH64_SPEC },	\
  { "cpp_arch_default",	CPP_ARCH_DEFAULT_SPEC },\
  { "cpp_arch",		CPP_ARCH_SPEC },	\
  { "cpp_endian",	CPP_ENDIAN_SPEC },	\
  { "cpp_subtarget",	CPP_SUBTARGET_SPEC },	\
  { "asm_cpu",		ASM_CPU_SPEC },		\
  { "asm_cpu_default",	ASM_CPU_DEFAULT_SPEC },	\
  { "asm_arch32",	ASM_ARCH32_SPEC },	\
  { "asm_arch64",	ASM_ARCH64_SPEC },	\
  { "asm_relax",	ASM_RELAX_SPEC },	\
  { "asm_arch_default",	ASM_ARCH_DEFAULT_SPEC },\
  { "asm_arch",		ASM_ARCH_SPEC },	\
  SUBTARGET_EXTRA_SPECS

#define SUBTARGET_EXTRA_SPECS

/* Because libgcc can generate references back to libc (via .umul etc.) we have
   to list libc again after the second libgcc.  */
#define LINK_GCC_C_SEQUENCE_SPEC "%G %L %G %L"


#define PTRDIFF_TYPE (TARGET_ARCH64 ? "long int" : "int")
#define SIZE_TYPE (TARGET_ARCH64 ? "long unsigned int" : "unsigned int")

/* ??? This should be 32 bits for v9 but what can we do?  */
#define WCHAR_TYPE "short unsigned int"
#define WCHAR_TYPE_SIZE 16

/* Show we can debug even without a frame pointer.  */
#define CAN_DEBUG_WITHOUT_FP

/* Option handling.  */

#define OVERRIDE_OPTIONS  sparc_override_options ()

/* Mask of all CPU selection flags.  */
#define MASK_ISA \
(MASK_V8 + MASK_SPARCLITE + MASK_SPARCLET + MASK_V9 + MASK_DEPRECATED_V8_INSNS)

/* TARGET_HARD_MUL: Use hardware multiply instructions but not %y.
   TARGET_HARD_MUL32: Use hardware multiply instructions with rd %y
   to get high 32 bits.  False in V8+ or V9 because multiply stores
   a 64 bit result in a register.  */

#define TARGET_HARD_MUL32				\
  ((TARGET_V8 || TARGET_SPARCLITE			\
    || TARGET_SPARCLET || TARGET_DEPRECATED_V8_INSNS)	\
   && ! TARGET_V8PLUS && TARGET_ARCH32)

#define TARGET_HARD_MUL					\
  (TARGET_V8 || TARGET_SPARCLITE || TARGET_SPARCLET	\
   || TARGET_DEPRECATED_V8_INSNS || TARGET_V8PLUS)

/* MASK_APP_REGS must always be the default because that's what
   FIXED_REGISTERS is set to and -ffixed- is processed before
   CONDITIONAL_REGISTER_USAGE is called (where we process -mno-app-regs).  */
#define TARGET_DEFAULT (MASK_APP_REGS + MASK_FPU)

/* Processor type.
   These must match the values for the cpu attribute in sparc.md.  */
enum processor_type {
  PROCESSOR_V7,
  PROCESSOR_CYPRESS,
  PROCESSOR_V8,
  PROCESSOR_SUPERSPARC,
  PROCESSOR_SPARCLITE,
  PROCESSOR_F930,
  PROCESSOR_F934,
  PROCESSOR_HYPERSPARC,
  PROCESSOR_SPARCLITE86X,
  PROCESSOR_SPARCLET,
  PROCESSOR_TSC701,
  PROCESSOR_V9,
  PROCESSOR_ULTRASPARC,
  PROCESSOR_ULTRASPARC3,
  PROCESSOR_NIAGARA
};

/* This is set from -m{cpu,tune}=xxx.  */
extern enum processor_type sparc_cpu;

/* Recast the cpu class to be the cpu attribute.
   Every file includes us, but not every file includes insn-attr.h.  */
#define sparc_cpu_attr ((enum attr_cpu) sparc_cpu)

/* Support for a compile-time default CPU, et cetera.  The rules are:
   --with-cpu is ignored if -mcpu is specified.
   --with-tune is ignored if -mtune is specified.
   --with-float is ignored if -mhard-float, -msoft-float, -mfpu, or -mno-fpu
     are specified.  */
#define OPTION_DEFAULT_SPECS \
  {"cpu", "%{!mcpu=*:-mcpu=%(VALUE)}" }, \
  {"tune", "%{!mtune=*:-mtune=%(VALUE)}" }, \
  {"float", "%{!msoft-float:%{!mhard-float:%{!fpu:%{!no-fpu:-m%(VALUE)-float}}}}" }

/* sparc_select[0] is reserved for the default cpu.  */
struct sparc_cpu_select
{
  const char *string;
  const char *const name;
  const int set_tune_p;
  const int set_arch_p;
};

extern struct sparc_cpu_select sparc_select[];

/* target machine storage layout */

/* Define this if most significant bit is lowest numbered
   in instructions that operate on numbered bit-fields.  */
#define BITS_BIG_ENDIAN 1

/* Define this if most significant byte of a word is the lowest numbered.  */
#define BYTES_BIG_ENDIAN 1

/* Define this if most significant word of a multiword number is the lowest
   numbered.  */
#define WORDS_BIG_ENDIAN 1

/* Define this to set the endianness to use in libgcc2.c, which can
   not depend on target_flags.  */
#if defined (__LITTLE_ENDIAN__) || defined(__LITTLE_ENDIAN_DATA__)
#define LIBGCC2_WORDS_BIG_ENDIAN 0
#else
#define LIBGCC2_WORDS_BIG_ENDIAN 1
#endif

#define MAX_BITS_PER_WORD	64

/* Width of a word, in units (bytes).  */
#define UNITS_PER_WORD		(TARGET_ARCH64 ? 8 : 4)
#ifdef IN_LIBGCC2
#define MIN_UNITS_PER_WORD	UNITS_PER_WORD
#else
#define MIN_UNITS_PER_WORD	4
#endif

#define UNITS_PER_SIMD_WORD	(TARGET_VIS ? 8 : UNITS_PER_WORD)

/* Now define the sizes of the C data types.  */

#define SHORT_TYPE_SIZE		16
#define INT_TYPE_SIZE		32
#define LONG_TYPE_SIZE		(TARGET_ARCH64 ? 64 : 32)
#define LONG_LONG_TYPE_SIZE	64
#define FLOAT_TYPE_SIZE		32
#define DOUBLE_TYPE_SIZE	64
/* LONG_DOUBLE_TYPE_SIZE is defined per OS even though the
   SPARC ABI says that it is 128-bit wide.  */
/* #define LONG_DOUBLE_TYPE_SIZE	128 */

/* Width in bits of a pointer.
   See also the macro `Pmode' defined below.  */
#define POINTER_SIZE (TARGET_PTR64 ? 64 : 32)

/* If we have to extend pointers (only when TARGET_ARCH64 and not
   TARGET_PTR64), we want to do it unsigned.   This macro does nothing
   if ptr_mode and Pmode are the same.  */
#define POINTERS_EXTEND_UNSIGNED 1

/* For TARGET_ARCH64 we need this, as we don't have instructions
   for arithmetic operations which do zero/sign extension at the same time,
   so without this we end up with a srl/sra after every assignment to an
   user variable,  which means very very bad code.  */
#define PROMOTE_FUNCTION_MODE(MODE, UNSIGNEDP, TYPE) \
if (TARGET_ARCH64				\
    && GET_MODE_CLASS (MODE) == MODE_INT	\
    && GET_MODE_SIZE (MODE) < UNITS_PER_WORD)	\
  (MODE) = word_mode;

/* Allocation boundary (in *bits*) for storing arguments in argument list.  */
#define PARM_BOUNDARY (TARGET_ARCH64 ? 64 : 32)

/* Boundary (in *bits*) on which stack pointer should be aligned.  */
/* FIXME, this is wrong when TARGET_ARCH64 and TARGET_STACK_BIAS, because
   then %sp+2047 is 128-bit aligned so %sp is really only byte-aligned.  */
#define STACK_BOUNDARY (TARGET_ARCH64 ? 128 : 64)
/* Temporary hack until the FIXME above is fixed.  */
#define SPARC_STACK_BOUNDARY_HACK (TARGET_ARCH64 && TARGET_STACK_BIAS)

/* ALIGN FRAMES on double word boundaries */

#define SPARC_STACK_ALIGN(LOC) \
  (TARGET_ARCH64 ? (((LOC)+15) & ~15) : (((LOC)+7) & ~7))

/* Allocation boundary (in *bits*) for the code of a function.  */
#define FUNCTION_BOUNDARY 32

/* Alignment of field after `int : 0' in a structure.  */
#define EMPTY_FIELD_BOUNDARY (TARGET_ARCH64 ? 64 : 32)

/* Every structure's size must be a multiple of this.  */
#define STRUCTURE_SIZE_BOUNDARY 8

/* A bit-field declared as `int' forces `int' alignment for the struct.  */
#define PCC_BITFIELD_TYPE_MATTERS 1

/* No data type wants to be aligned rounder than this.  */
#define BIGGEST_ALIGNMENT (TARGET_ARCH64 ? 128 : 64)

/* The best alignment to use in cases where we have a choice.  */
#define FASTEST_ALIGNMENT 64

/* Define this macro as an expression for the alignment of a structure
   (given by STRUCT as a tree node) if the alignment computed in the
   usual way is COMPUTED and the alignment explicitly specified was
   SPECIFIED.

   The default is to use SPECIFIED if it is larger; otherwise, use
   the smaller of COMPUTED and `BIGGEST_ALIGNMENT' */
#define ROUND_TYPE_ALIGN(STRUCT, COMPUTED, SPECIFIED)	\
 (TARGET_FASTER_STRUCTS ?				\
  ((TREE_CODE (STRUCT) == RECORD_TYPE			\
    || TREE_CODE (STRUCT) == UNION_TYPE                 \
    || TREE_CODE (STRUCT) == QUAL_UNION_TYPE)           \
   && TYPE_FIELDS (STRUCT) != 0                         \
     ? MAX (MAX ((COMPUTED), (SPECIFIED)), BIGGEST_ALIGNMENT) \
     : MAX ((COMPUTED), (SPECIFIED)))			\
   :  MAX ((COMPUTED), (SPECIFIED)))

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

/* Make local arrays of chars word-aligned for the same reasons.  */
#define LOCAL_ALIGNMENT(TYPE, ALIGN) DATA_ALIGNMENT (TYPE, ALIGN)

/* Set this nonzero if move instructions will actually fail to work
   when given unaligned data.  */
#define STRICT_ALIGNMENT 1

/* Things that must be doubleword aligned cannot go in the text section,
   because the linker fails to align the text section enough!
   Put them in the data section.  This macro is only used in this file.  */
#define MAX_TEXT_ALIGN 32

/* Standard register usage.  */

/* Number of actual hardware registers.
   The hardware registers are assigned numbers for the compiler
   from 0 to just below FIRST_PSEUDO_REGISTER.
   All registers that the compiler knows about must be given numbers,
   even those that are not normally considered general registers.

   SPARC has 32 integer registers and 32 floating point registers.
   64 bit SPARC has 32 additional fp regs, but the odd numbered ones are not
   accessible.  We still account for them to simplify register computations
   (e.g.: in CLASS_MAX_NREGS).  There are also 4 fp condition code registers, so
   32+32+32+4 == 100.
   Register 100 is used as the integer condition code register.
   Register 101 is used as the soft frame pointer register.  */

#define FIRST_PSEUDO_REGISTER 102

#define SPARC_FIRST_FP_REG     32
/* Additional V9 fp regs.  */
#define SPARC_FIRST_V9_FP_REG  64
#define SPARC_LAST_V9_FP_REG   95
/* V9 %fcc[0123].  V8 uses (figuratively) %fcc0.  */
#define SPARC_FIRST_V9_FCC_REG 96
#define SPARC_LAST_V9_FCC_REG  99
/* V8 fcc reg.  */
#define SPARC_FCC_REG 96
/* Integer CC reg.  We don't distinguish %icc from %xcc.  */
#define SPARC_ICC_REG 100

/* Nonzero if REGNO is an fp reg.  */
#define SPARC_FP_REG_P(REGNO) \
((REGNO) >= SPARC_FIRST_FP_REG && (REGNO) <= SPARC_LAST_V9_FP_REG)

/* Argument passing regs.  */
#define SPARC_OUTGOING_INT_ARG_FIRST 8
#define SPARC_INCOMING_INT_ARG_FIRST 24
#define SPARC_FP_ARG_FIRST           32

/* 1 for registers that have pervasive standard uses
   and are not available for the register allocator.

   On non-v9 systems:
   g1 is free to use as temporary.
   g2-g4 are reserved for applications.  Gcc normally uses them as
   temporaries, but this can be disabled via the -mno-app-regs option.
   g5 through g7 are reserved for the operating system.

   On v9 systems:
   g1,g5 are free to use as temporaries, and are free to use between calls
   if the call is to an external function via the PLT.
   g4 is free to use as a temporary in the non-embedded case.
   g4 is reserved in the embedded case.
   g2-g3 are reserved for applications.  Gcc normally uses them as
   temporaries, but this can be disabled via the -mno-app-regs option.
   g6-g7 are reserved for the operating system (or application in
   embedded case).
   ??? Register 1 is used as a temporary by the 64 bit sethi pattern, so must
   currently be a fixed register until this pattern is rewritten.
   Register 1 is also used when restoring call-preserved registers in large
   stack frames.

   Registers fixed in arch32 and not arch64 (or vice-versa) are marked in
   CONDITIONAL_REGISTER_USAGE in order to properly handle -ffixed-.
*/

#define FIXED_REGISTERS  \
 {1, 0, 2, 2, 2, 2, 1, 1,	\
  0, 0, 0, 0, 0, 0, 1, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 1, 1,	\
				\
  0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0,	\
				\
  0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0,	\
				\
  0, 0, 0, 0, 0, 1}

/* 1 for registers not available across function calls.
   These must include the FIXED_REGISTERS and also any
   registers that can be used without being saved.
   The latter must include the registers where values are returned
   and the register where structure-value addresses are passed.
   Aside from that, you can include as many other registers as you like.  */

#define CALL_USED_REGISTERS  \
 {1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1,	\
  0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 1, 1,	\
				\
  1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1,	\
				\
  1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1,	\
				\
  1, 1, 1, 1, 1, 1}

/* If !TARGET_FPU, then make the fp registers and fp cc regs fixed so that
   they won't be allocated.  */

#define CONDITIONAL_REGISTER_USAGE				\
do								\
  {								\
    if (PIC_OFFSET_TABLE_REGNUM != INVALID_REGNUM)		\
      {								\
	fixed_regs[PIC_OFFSET_TABLE_REGNUM] = 1;		\
	call_used_regs[PIC_OFFSET_TABLE_REGNUM] = 1;		\
      }								\
    /* If the user has passed -f{fixed,call-{used,saved}}-g5 */	\
    /* then honor it.  */					\
    if (TARGET_ARCH32 && fixed_regs[5])				\
      fixed_regs[5] = 1;					\
    else if (TARGET_ARCH64 && fixed_regs[5] == 2)		\
      fixed_regs[5] = 0;					\
    if (! TARGET_V9)						\
      {								\
	int regno;						\
	for (regno = SPARC_FIRST_V9_FP_REG;			\
	     regno <= SPARC_LAST_V9_FP_REG;			\
	     regno++)						\
	  fixed_regs[regno] = 1;				\
	/* %fcc0 is used by v8 and v9.  */			\
	for (regno = SPARC_FIRST_V9_FCC_REG + 1;		\
	     regno <= SPARC_LAST_V9_FCC_REG;			\
	     regno++)						\
	  fixed_regs[regno] = 1;				\
      }								\
    if (! TARGET_FPU)						\
      {								\
	int regno;						\
	for (regno = 32; regno < SPARC_LAST_V9_FCC_REG; regno++) \
	  fixed_regs[regno] = 1;				\
      }								\
    /* If the user has passed -f{fixed,call-{used,saved}}-g2 */	\
    /* then honor it.  Likewise with g3 and g4.  */		\
    if (fixed_regs[2] == 2)					\
      fixed_regs[2] = ! TARGET_APP_REGS;			\
    if (fixed_regs[3] == 2)					\
      fixed_regs[3] = ! TARGET_APP_REGS;			\
    if (TARGET_ARCH32 && fixed_regs[4] == 2)			\
      fixed_regs[4] = ! TARGET_APP_REGS;			\
    else if (TARGET_CM_EMBMEDANY)				\
      fixed_regs[4] = 1;					\
    else if (fixed_regs[4] == 2)				\
      fixed_regs[4] = 0;					\
  }								\
while (0)

/* Return number of consecutive hard regs needed starting at reg REGNO
   to hold something of mode MODE.
   This is ordinarily the length in words of a value of mode MODE
   but can be less for certain modes in special long registers.

   On SPARC, ordinary registers hold 32 bits worth;
   this means both integer and floating point registers.
   On v9, integer regs hold 64 bits worth; floating point regs hold
   32 bits worth (this includes the new fp regs as even the odd ones are
   included in the hard register count).  */

#define HARD_REGNO_NREGS(REGNO, MODE) \
  (TARGET_ARCH64							\
   ? ((REGNO) < 32 || (REGNO) == FRAME_POINTER_REGNUM			\
      ? (GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD	\
      : (GET_MODE_SIZE (MODE) + 3) / 4)					\
   : ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD))

/* Due to the ARCH64 discrepancy above we must override this next
   macro too.  */
#define REGMODE_NATURAL_SIZE(MODE) \
  ((TARGET_ARCH64 && FLOAT_MODE_P (MODE)) ? 4 : UNITS_PER_WORD)

/* Value is 1 if hard register REGNO can hold a value of machine-mode MODE.
   See sparc.c for how we initialize this.  */
extern const int *hard_regno_mode_classes;
extern int sparc_mode_class[];

/* ??? Because of the funny way we pass parameters we should allow certain
   ??? types of float/complex values to be in integer registers during
   ??? RTL generation.  This only matters on arch32.  */
#define HARD_REGNO_MODE_OK(REGNO, MODE) \
  ((hard_regno_mode_classes[REGNO] & sparc_mode_class[MODE]) != 0)

/* Value is 1 if it is OK to rename a hard register FROM to another hard
   register TO.  We cannot rename %g1 as it may be used before the save
   register window instruction in the prologue.  */
#define HARD_REGNO_RENAME_OK(FROM, TO) ((FROM) != 1)

/* Value is 1 if it is a good idea to tie two pseudo registers
   when one has mode MODE1 and one has mode MODE2.
   If HARD_REGNO_MODE_OK could produce different values for MODE1 and MODE2,
   for any hard reg, then this must be 0 for correct output.

   For V9: SFmode can't be combined with other float modes, because they can't
   be allocated to the %d registers.  Also, DFmode won't fit in odd %f
   registers, but SFmode will.  */
#define MODES_TIEABLE_P(MODE1, MODE2) \
  ((MODE1) == (MODE2)						\
   || (GET_MODE_CLASS (MODE1) == GET_MODE_CLASS (MODE2)		\
       && (! TARGET_V9						\
	   || (GET_MODE_CLASS (MODE1) != MODE_FLOAT		\
	       || (MODE1 != SFmode && MODE2 != SFmode)))))

/* Specify the registers used for certain standard purposes.
   The values of these macros are register numbers.  */

/* Register to use for pushing function arguments.  */
#define STACK_POINTER_REGNUM 14

/* The stack bias (amount by which the hardware register is offset by).  */
#define SPARC_STACK_BIAS ((TARGET_ARCH64 && TARGET_STACK_BIAS) ? 2047 : 0)

/* Actual top-of-stack address is 92/176 greater than the contents of the
   stack pointer register for !v9/v9.  That is:
   - !v9: 64 bytes for the in and local registers, 4 bytes for structure return
     address, and 6*4 bytes for the 6 register parameters.
   - v9: 128 bytes for the in and local registers + 6*8 bytes for the integer
     parameter regs.  */
#define STACK_POINTER_OFFSET (FIRST_PARM_OFFSET(0) + SPARC_STACK_BIAS)

/* Base register for access to local variables of the function.  */
#define HARD_FRAME_POINTER_REGNUM 30

/* The soft frame pointer does not have the stack bias applied.  */
#define FRAME_POINTER_REGNUM 101

/* Given the stack bias, the stack pointer isn't actually aligned.  */
#define INIT_EXPANDERS							 \
  do {									 \
    if (cfun && cfun->emit->regno_pointer_align && SPARC_STACK_BIAS)	 \
      {									 \
	REGNO_POINTER_ALIGN (STACK_POINTER_REGNUM) = BITS_PER_UNIT;	 \
	REGNO_POINTER_ALIGN (HARD_FRAME_POINTER_REGNUM) = BITS_PER_UNIT; \
      }									 \
  } while (0)

/* Value should be nonzero if functions must have frame pointers.
   Zero means the frame pointer need not be set up (and parms
   may be accessed via the stack pointer) in functions that seem suitable.
   Used in flow.c, global.c, ra.c and reload1.c.  */
#define FRAME_POINTER_REQUIRED	\
  (! (leaf_function_p () && only_leaf_regs_used ()))

/* Base register for access to arguments of the function.  */
#define ARG_POINTER_REGNUM FRAME_POINTER_REGNUM

/* Register in which static-chain is passed to a function.  This must
   not be a register used by the prologue.  */
#define STATIC_CHAIN_REGNUM (TARGET_ARCH64 ? 5 : 2)

/* Register which holds offset table for position-independent
   data references.  */

#define PIC_OFFSET_TABLE_REGNUM (flag_pic ? 23 : INVALID_REGNUM)

/* Pick a default value we can notice from override_options:
   !v9: Default is on.
   v9: Default is off.  */

#define DEFAULT_PCC_STRUCT_RETURN -1

/* Functions which return large structures get the address
   to place the wanted value at offset 64 from the frame.
   Must reserve 64 bytes for the in and local registers.
   v9: Functions which return large structures get the address to place the
   wanted value from an invisible first argument.  */
#define STRUCT_VALUE_OFFSET 64

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

/* The SPARC has various kinds of registers: general, floating point,
   and condition codes [well, it has others as well, but none that we
   care directly about].

   For v9 we must distinguish between the upper and lower floating point
   registers because the upper ones can't hold SFmode values.
   HARD_REGNO_MODE_OK won't help here because reload assumes that register(s)
   satisfying a group need for a class will also satisfy a single need for
   that class.  EXTRA_FP_REGS is a bit of a misnomer as it covers all 64 fp
   regs.

   It is important that one class contains all the general and all the standard
   fp regs.  Otherwise find_reg() won't properly allocate int regs for moves,
   because reg_class_record() will bias the selection in favor of fp regs,
   because reg_class_subunion[GENERAL_REGS][FP_REGS] will yield FP_REGS,
   because FP_REGS > GENERAL_REGS.

   It is also important that one class contain all the general and all
   the fp regs.  Otherwise when spilling a DFmode reg, it may be from
   EXTRA_FP_REGS but find_reloads() may use class
   GENERAL_OR_FP_REGS. This will cause allocate_reload_reg() to die
   because the compiler thinks it doesn't have a spill reg when in
   fact it does.

   v9 also has 4 floating point condition code registers.  Since we don't
   have a class that is the union of FPCC_REGS with either of the others,
   it is important that it appear first.  Otherwise the compiler will die
   trying to compile _fixunsdfsi because fix_truncdfsi2 won't match its
   constraints.

   It is important that SPARC_ICC_REG have class NO_REGS.  Otherwise combine
   may try to use it to hold an SImode value.  See register_operand.
   ??? Should %fcc[0123] be handled similarly?
*/

enum reg_class { NO_REGS, FPCC_REGS, I64_REGS, GENERAL_REGS, FP_REGS,
		 EXTRA_FP_REGS, GENERAL_OR_FP_REGS, GENERAL_OR_EXTRA_FP_REGS,
		 ALL_REGS, LIM_REG_CLASSES };

#define N_REG_CLASSES (int) LIM_REG_CLASSES

/* Give names of register classes as strings for dump file.  */

#define REG_CLASS_NAMES \
  { "NO_REGS", "FPCC_REGS", "I64_REGS", "GENERAL_REGS", "FP_REGS",	\
     "EXTRA_FP_REGS", "GENERAL_OR_FP_REGS", "GENERAL_OR_EXTRA_FP_REGS",	\
     "ALL_REGS" }

/* Define which registers fit in which classes.
   This is an initializer for a vector of HARD_REG_SET
   of length N_REG_CLASSES.  */

#define REG_CLASS_CONTENTS				\
  {{0, 0, 0, 0},	/* NO_REGS */			\
   {0, 0, 0, 0xf},	/* FPCC_REGS */			\
   {0xffff, 0, 0, 0},	/* I64_REGS */			\
   {-1, 0, 0, 0x20},	/* GENERAL_REGS */		\
   {0, -1, 0, 0},	/* FP_REGS */			\
   {0, -1, -1, 0},	/* EXTRA_FP_REGS */		\
   {-1, -1, 0, 0x20},	/* GENERAL_OR_FP_REGS */	\
   {-1, -1, -1, 0x20},	/* GENERAL_OR_EXTRA_FP_REGS */	\
   {-1, -1, -1, 0x3f}}	/* ALL_REGS */

/* Defines invalid mode changes.  Borrowed from pa64-regs.h.

   SImode loads to floating-point registers are not zero-extended.
   The definition for LOAD_EXTEND_OP specifies that integer loads
   narrower than BITS_PER_WORD will be zero-extended.  As a result,
   we inhibit changes from SImode unless they are to a mode that is
   identical in size.  */

#define CANNOT_CHANGE_MODE_CLASS(FROM, TO, CLASS)		\
  (TARGET_ARCH64						\
   && (FROM) == SImode						\
   && GET_MODE_SIZE (FROM) != GET_MODE_SIZE (TO)		\
   ? reg_classes_intersect_p (CLASS, FP_REGS) : 0)

/* The same information, inverted:
   Return the class number of the smallest class containing
   reg number REGNO.  This could be a conditional expression
   or could index an array.  */

extern enum reg_class sparc_regno_reg_class[FIRST_PSEUDO_REGISTER];

#define REGNO_REG_CLASS(REGNO) sparc_regno_reg_class[(REGNO)]

/* This is the order in which to allocate registers normally.

   We put %f0-%f7 last among the float registers, so as to make it more
   likely that a pseudo-register which dies in the float return register
   area will get allocated to the float return register, thus saving a move
   instruction at the end of the function.

   Similarly for integer return value registers.

   We know in this case that we will not end up with a leaf function.

   The register allocator is given the global and out registers first
   because these registers are call clobbered and thus less useful to
   global register allocation.

   Next we list the local and in registers.  They are not call clobbered
   and thus very useful for global register allocation.  We list the input
   registers before the locals so that it is more likely the incoming
   arguments received in those registers can just stay there and not be
   reloaded.  */

#define REG_ALLOC_ORDER \
{ 1, 2, 3, 4, 5, 6, 7,			/* %g1-%g7 */	\
  13, 12, 11, 10, 9, 8, 		/* %o5-%o0 */	\
  15,					/* %o7 */	\
  16, 17, 18, 19, 20, 21, 22, 23,	/* %l0-%l7 */ 	\
  29, 28, 27, 26, 25, 24, 31,		/* %i5-%i0,%i7 */\
  40, 41, 42, 43, 44, 45, 46, 47,	/* %f8-%f15 */  \
  48, 49, 50, 51, 52, 53, 54, 55,	/* %f16-%f23 */ \
  56, 57, 58, 59, 60, 61, 62, 63,	/* %f24-%f31 */ \
  64, 65, 66, 67, 68, 69, 70, 71,	/* %f32-%f39 */ \
  72, 73, 74, 75, 76, 77, 78, 79,	/* %f40-%f47 */ \
  80, 81, 82, 83, 84, 85, 86, 87,	/* %f48-%f55 */ \
  88, 89, 90, 91, 92, 93, 94, 95,	/* %f56-%f63 */ \
  39, 38, 37, 36, 35, 34, 33, 32,	/* %f7-%f0 */   \
  96, 97, 98, 99,			/* %fcc0-3 */   \
  100, 0, 14, 30, 101}			/* %icc, %g0, %o6, %i6, %sfp */

/* This is the order in which to allocate registers for
   leaf functions.  If all registers can fit in the global and
   output registers, then we have the possibility of having a leaf
   function.

   The macro actually mentioned the input registers first,
   because they get renumbered into the output registers once
   we know really do have a leaf function.

   To be more precise, this register allocation order is used
   when %o7 is found to not be clobbered right before register
   allocation.  Normally, the reason %o7 would be clobbered is
   due to a call which could not be transformed into a sibling
   call.

   As a consequence, it is possible to use the leaf register
   allocation order and not end up with a leaf function.  We will
   not get suboptimal register allocation in that case because by
   definition of being potentially leaf, there were no function
   calls.  Therefore, allocation order within the local register
   window is not critical like it is when we do have function calls.  */

#define REG_LEAF_ALLOC_ORDER \
{ 1, 2, 3, 4, 5, 6, 7, 			/* %g1-%g7 */	\
  29, 28, 27, 26, 25, 24,		/* %i5-%i0 */	\
  15,					/* %o7 */	\
  13, 12, 11, 10, 9, 8,			/* %o5-%o0 */	\
  16, 17, 18, 19, 20, 21, 22, 23,	/* %l0-%l7 */	\
  40, 41, 42, 43, 44, 45, 46, 47,	/* %f8-%f15 */	\
  48, 49, 50, 51, 52, 53, 54, 55,	/* %f16-%f23 */	\
  56, 57, 58, 59, 60, 61, 62, 63,	/* %f24-%f31 */	\
  64, 65, 66, 67, 68, 69, 70, 71,	/* %f32-%f39 */	\
  72, 73, 74, 75, 76, 77, 78, 79,	/* %f40-%f47 */	\
  80, 81, 82, 83, 84, 85, 86, 87,	/* %f48-%f55 */	\
  88, 89, 90, 91, 92, 93, 94, 95,	/* %f56-%f63 */	\
  39, 38, 37, 36, 35, 34, 33, 32,	/* %f7-%f0 */	\
  96, 97, 98, 99,			/* %fcc0-3 */	\
  100, 0, 14, 30, 31, 101}		/* %icc, %g0, %o6, %i6, %i7, %sfp */

#define ORDER_REGS_FOR_LOCAL_ALLOC order_regs_for_local_alloc ()

extern char sparc_leaf_regs[];
#define LEAF_REGISTERS sparc_leaf_regs

extern char leaf_reg_remap[];
#define LEAF_REG_REMAP(REGNO) (leaf_reg_remap[REGNO])

/* The class value for index registers, and the one for base regs.  */
#define INDEX_REG_CLASS GENERAL_REGS
#define BASE_REG_CLASS GENERAL_REGS

/* Local macro to handle the two v9 classes of FP regs.  */
#define FP_REG_CLASS_P(CLASS) ((CLASS) == FP_REGS || (CLASS) == EXTRA_FP_REGS)

/* Get reg_class from a letter such as appears in the machine description.
   In the not-v9 case, coerce v9's 'e' class to 'f', so we can use 'e' in the
   .md file for v8 and v9.
   'd' and 'b' are used for single and double precision VIS operations,
   if TARGET_VIS.
   'h' is used for V8+ 64 bit global and out registers.  */

#define REG_CLASS_FROM_LETTER(C)		\
(TARGET_V9					\
 ? ((C) == 'f' ? FP_REGS			\
    : (C) == 'e' ? EXTRA_FP_REGS 		\
    : (C) == 'c' ? FPCC_REGS			\
    : ((C) == 'd' && TARGET_VIS) ? FP_REGS\
    : ((C) == 'b' && TARGET_VIS) ? EXTRA_FP_REGS\
    : ((C) == 'h' && TARGET_V8PLUS) ? I64_REGS\
    : NO_REGS)					\
 : ((C) == 'f' ? FP_REGS			\
    : (C) == 'e' ? FP_REGS			\
    : (C) == 'c' ? FPCC_REGS			\
    : NO_REGS))

/* The letters I, J, K, L, M, N, O, P in a register constraint string
   can be used to stand for particular ranges of CONST_INTs.
   This macro defines what the ranges are.
   C is the letter, and VALUE is a constant value.
   Return 1 if VALUE is in the range specified by C.

   `I' is used for the range of constants an insn can actually contain.
   `J' is used for the range which is just zero (since that is R0).
   `K' is used for constants which can be loaded with a single sethi insn.
   `L' is used for the range of constants supported by the movcc insns.
   `M' is used for the range of constants supported by the movrcc insns.
   `N' is like K, but for constants wider than 32 bits.
   `O' is used for the range which is just 4096.
   `P' is free.  */

/* Predicates for 10-bit, 11-bit and 13-bit signed constants.  */
#define SPARC_SIMM10_P(X) ((unsigned HOST_WIDE_INT) (X) + 0x200 < 0x400)
#define SPARC_SIMM11_P(X) ((unsigned HOST_WIDE_INT) (X) + 0x400 < 0x800)
#define SPARC_SIMM13_P(X) ((unsigned HOST_WIDE_INT) (X) + 0x1000 < 0x2000)

/* 10- and 11-bit immediates are only used for a few specific insns.
   SMALL_INT is used throughout the port so we continue to use it.  */
#define SMALL_INT(X) (SPARC_SIMM13_P (INTVAL (X)))

/* Predicate for constants that can be loaded with a sethi instruction.
   This is the general, 64-bit aware, bitwise version that ensures that
   only constants whose representation fits in the mask

     0x00000000fffffc00

   are accepted.  It will reject, for example, negative SImode constants
   on 64-bit hosts, so correct handling is to mask the value beforehand
   according to the mode of the instruction.  */
#define SPARC_SETHI_P(X) \
  (((unsigned HOST_WIDE_INT) (X) \
    & ((unsigned HOST_WIDE_INT) 0x3ff - GET_MODE_MASK (SImode) - 1)) == 0)

/* Version of the above predicate for SImode constants and below.  */
#define SPARC_SETHI32_P(X) \
  (SPARC_SETHI_P ((unsigned HOST_WIDE_INT) (X) & GET_MODE_MASK (SImode)))

#define CONST_OK_FOR_LETTER_P(VALUE, C)  \
  ((C) == 'I' ? SPARC_SIMM13_P (VALUE)			\
   : (C) == 'J' ? (VALUE) == 0				\
   : (C) == 'K' ? SPARC_SETHI32_P (VALUE)		\
   : (C) == 'L' ? SPARC_SIMM11_P (VALUE)		\
   : (C) == 'M' ? SPARC_SIMM10_P (VALUE)		\
   : (C) == 'N' ? SPARC_SETHI_P (VALUE)			\
   : (C) == 'O' ? (VALUE) == 4096			\
   : 0)

/* Similar, but for CONST_DOUBLEs, and defining letters G and H.
   Here VALUE is the CONST_DOUBLE rtx itself.  */

#define CONST_DOUBLE_OK_FOR_LETTER_P(VALUE, C)	\
  ((C) == 'G' ? const_zero_operand (VALUE, GET_MODE (VALUE))	\
   : (C) == 'H' ? arith_double_operand (VALUE, DImode)		\
   : 0)

/* Given an rtx X being reloaded into a reg required to be
   in class CLASS, return the class of reg to actually use.
   In general this is just CLASS; but on some machines
   in some cases it is preferable to use a more restrictive class.  */
/* - We can't load constants into FP registers.
   - We can't load FP constants into integer registers when soft-float,
     because there is no soft-float pattern with a r/F constraint.
   - We can't load FP constants into integer registers for TFmode unless
     it is 0.0L, because there is no movtf pattern with a r/F constraint.
   - Try and reload integer constants (symbolic or otherwise) back into
     registers directly, rather than having them dumped to memory.  */

#define PREFERRED_RELOAD_CLASS(X,CLASS)			\
  (CONSTANT_P (X)					\
   ? ((FP_REG_CLASS_P (CLASS)				\
       || (CLASS) == GENERAL_OR_FP_REGS			\
       || (CLASS) == GENERAL_OR_EXTRA_FP_REGS		\
       || (GET_MODE_CLASS (GET_MODE (X)) == MODE_FLOAT	\
	   && ! TARGET_FPU)				\
       || (GET_MODE (X) == TFmode			\
	   && ! const_zero_operand (X, TFmode)))	\
      ? NO_REGS						\
      : (!FP_REG_CLASS_P (CLASS)			\
         && GET_MODE_CLASS (GET_MODE (X)) == MODE_INT)	\
      ? GENERAL_REGS					\
      : (CLASS))					\
   : (CLASS))

/* Return the register class of a scratch register needed to load IN into
   a register of class CLASS in MODE.

   We need a temporary when loading/storing a HImode/QImode value
   between memory and the FPU registers.  This can happen when combine puts
   a paradoxical subreg in a float/fix conversion insn.

   We need a temporary when loading/storing a DFmode value between
   unaligned memory and the upper FPU registers.  */

#define SECONDARY_INPUT_RELOAD_CLASS(CLASS, MODE, IN)		\
  ((FP_REG_CLASS_P (CLASS)					\
    && ((MODE) == HImode || (MODE) == QImode)			\
    && (GET_CODE (IN) == MEM					\
        || ((GET_CODE (IN) == REG || GET_CODE (IN) == SUBREG)	\
            && true_regnum (IN) == -1)))			\
   ? GENERAL_REGS						\
   : ((CLASS) == EXTRA_FP_REGS && (MODE) == DFmode		\
      && GET_CODE (IN) == MEM && TARGET_ARCH32			\
      && ! mem_min_alignment ((IN), 8))				\
     ? FP_REGS							\
     : (((TARGET_CM_MEDANY					\
	  && symbolic_operand ((IN), (MODE)))			\
	 || (TARGET_CM_EMBMEDANY				\
	     && text_segment_operand ((IN), (MODE))))		\
	&& !flag_pic)						\
       ? GENERAL_REGS						\
       : NO_REGS)

#define SECONDARY_OUTPUT_RELOAD_CLASS(CLASS, MODE, IN)		\
  ((FP_REG_CLASS_P (CLASS)					\
     && ((MODE) == HImode || (MODE) == QImode)			\
     && (GET_CODE (IN) == MEM					\
         || ((GET_CODE (IN) == REG || GET_CODE (IN) == SUBREG)	\
             && true_regnum (IN) == -1)))			\
   ? GENERAL_REGS						\
   : ((CLASS) == EXTRA_FP_REGS && (MODE) == DFmode		\
      && GET_CODE (IN) == MEM && TARGET_ARCH32			\
      && ! mem_min_alignment ((IN), 8))				\
     ? FP_REGS							\
     : (((TARGET_CM_MEDANY					\
	  && symbolic_operand ((IN), (MODE)))			\
	 || (TARGET_CM_EMBMEDANY				\
	     && text_segment_operand ((IN), (MODE))))		\
	&& !flag_pic)						\
       ? GENERAL_REGS						\
       : NO_REGS)

/* On SPARC it is not possible to directly move data between
   GENERAL_REGS and FP_REGS.  */
#define SECONDARY_MEMORY_NEEDED(CLASS1, CLASS2, MODE) \
  (FP_REG_CLASS_P (CLASS1) != FP_REG_CLASS_P (CLASS2))

/* Return the stack location to use for secondary memory needed reloads.
   We want to use the reserved location just below the frame pointer.
   However, we must ensure that there is a frame, so use assign_stack_local
   if the frame size is zero.  */
#define SECONDARY_MEMORY_NEEDED_RTX(MODE) \
  (get_frame_size () == 0						\
   ? assign_stack_local (MODE, GET_MODE_SIZE (MODE), 0)			\
   : gen_rtx_MEM (MODE, plus_constant (frame_pointer_rtx,		\
				       STARTING_FRAME_OFFSET)))

/* Get_secondary_mem widens its argument to BITS_PER_WORD which loses on v9
   because the movsi and movsf patterns don't handle r/f moves.
   For v8 we copy the default definition.  */
#define SECONDARY_MEMORY_NEEDED_MODE(MODE) \
  (TARGET_ARCH64						\
   ? (GET_MODE_BITSIZE (MODE) < 32				\
      ? mode_for_size (32, GET_MODE_CLASS (MODE), 0)		\
      : MODE)							\
   : (GET_MODE_BITSIZE (MODE) < BITS_PER_WORD			\
      ? mode_for_size (BITS_PER_WORD, GET_MODE_CLASS (MODE), 0)	\
      : MODE))

/* Return the maximum number of consecutive registers
   needed to represent mode MODE in a register of class CLASS.  */
/* On SPARC, this is the size of MODE in words.  */
#define CLASS_MAX_NREGS(CLASS, MODE)	\
  (FP_REG_CLASS_P (CLASS) ? (GET_MODE_SIZE (MODE) + 3) / 4 \
   : (GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

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
/* This allows space for one TFmode floating point value, which is used
   by SECONDARY_MEMORY_NEEDED_RTX.  */
#define STARTING_FRAME_OFFSET \
  (TARGET_ARCH64 ? -16 \
   : (-SPARC_STACK_ALIGN (LONG_DOUBLE_TYPE_SIZE / BITS_PER_UNIT)))

/* Offset of first parameter from the argument pointer register value.
   !v9: This is 64 for the ins and locals, plus 4 for the struct-return reg
   even if this function isn't going to use it.
   v9: This is 128 for the ins and locals.  */
#define FIRST_PARM_OFFSET(FNDECL) \
  (TARGET_ARCH64 ? 16 * UNITS_PER_WORD : STRUCT_VALUE_OFFSET + UNITS_PER_WORD)

/* Offset from the argument pointer register value to the CFA.
   This is different from FIRST_PARM_OFFSET because the register window
   comes between the CFA and the arguments.  */
#define ARG_POINTER_CFA_OFFSET(FNDECL)  0

/* When a parameter is passed in a register, stack space is still
   allocated for it.
   !v9: All 6 possible integer registers have backing store allocated.
   v9: Only space for the arguments passed is allocated.  */
/* ??? Ideally, we'd use zero here (as the minimum), but zero has special
   meaning to the backend.  Further, we need to be able to detect if a
   varargs/unprototyped function is called, as they may want to spill more
   registers than we've provided space.  Ugly, ugly.  So for now we retain
   all 6 slots even for v9.  */
#define REG_PARM_STACK_SPACE(DECL) (6 * UNITS_PER_WORD)

/* Definitions for register elimination.  */

#define ELIMINABLE_REGS \
  {{ FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM}, \
   { FRAME_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM} }

/* The way this is structured, we can't eliminate SFP in favor of SP
   if the frame pointer is required: we want to use the SFP->HFP elimination
   in that case.  But the test in update_eliminables doesn't know we are
   assuming below that we only do the former elimination.  */
#define CAN_ELIMINATE(FROM, TO) \
  ((TO) == HARD_FRAME_POINTER_REGNUM || !FRAME_POINTER_REQUIRED)

/* We always pretend that this is a leaf function because if it's not,
   there's no point in trying to eliminate the frame pointer.  If it
   is a leaf function, we guessed right!  */
#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET) 			\
  do {									\
    if ((TO) == STACK_POINTER_REGNUM)					\
      (OFFSET) = sparc_compute_frame_size (get_frame_size (), 1);	\
    else								\
      (OFFSET) = 0;							\
    (OFFSET) += SPARC_STACK_BIAS;					\
  } while (0)

/* Keep the stack pointer constant throughout the function.
   This is both an optimization and a necessity: longjmp
   doesn't behave itself when the stack pointer moves within
   the function!  */
#define ACCUMULATE_OUTGOING_ARGS 1

/* Value is the number of bytes of arguments automatically
   popped when returning from a subroutine call.
   FUNDECL is the declaration node of the function (as a tree),
   FUNTYPE is the data type of the function (as a tree),
   or for a library call it is an identifier node for the subroutine name.
   SIZE is the number of bytes of arguments passed on the stack.  */

#define RETURN_POPS_ARGS(FUNDECL,FUNTYPE,SIZE) 0

/* Define this macro if the target machine has "register windows".  This
   C expression returns the register number as seen by the called function
   corresponding to register number OUT as seen by the calling function.
   Return OUT if register number OUT is not an outbound register.  */

#define INCOMING_REGNO(OUT) \
 (((OUT) < 8 || (OUT) > 15) ? (OUT) : (OUT) + 16)

/* Define this macro if the target machine has "register windows".  This
   C expression returns the register number as seen by the calling function
   corresponding to register number IN as seen by the called function.
   Return IN if register number IN is not an inbound register.  */

#define OUTGOING_REGNO(IN) \
 (((IN) < 24 || (IN) > 31) ? (IN) : (IN) - 16)

/* Define this macro if the target machine has register windows.  This
   C expression returns true if the register is call-saved but is in the
   register window.  */

#define LOCAL_REGNO(REGNO) \
  ((REGNO) >= 16 && (REGNO) <= 31)

/* Define how to find the value returned by a function.
   VALTYPE is the data type of the value (as a tree).
   If the precise function being called is known, FUNC is its FUNCTION_DECL;
   otherwise, FUNC is 0.  */

/* On SPARC the value is found in the first "output" register.  */

#define FUNCTION_VALUE(VALTYPE, FUNC) \
  function_value ((VALTYPE), TYPE_MODE (VALTYPE), 1)

/* But the called function leaves it in the first "input" register.  */

#define FUNCTION_OUTGOING_VALUE(VALTYPE, FUNC) \
  function_value ((VALTYPE), TYPE_MODE (VALTYPE), 0)

/* Define how to find the value returned by a library function
   assuming the value has mode MODE.  */

#define LIBCALL_VALUE(MODE) \
  function_value (NULL_TREE, (MODE), 1)

/* 1 if N is a possible register number for a function value
   as seen by the caller.
   On SPARC, the first "output" reg is used for integer values,
   and the first floating point register is used for floating point values.  */

#define FUNCTION_VALUE_REGNO_P(N) ((N) == 8 || (N) == 32)

/* Define the size of space to allocate for the return value of an
   untyped_call.  */

#define APPLY_RESULT_SIZE (TARGET_ARCH64 ? 24 : 16)

/* 1 if N is a possible register number for function argument passing.
   On SPARC, these are the "output" registers.  v9 also uses %f0-%f31.  */

#define FUNCTION_ARG_REGNO_P(N) \
(TARGET_ARCH64 \
 ? (((N) >= 8 && (N) <= 13) || ((N) >= 32 && (N) <= 63)) \
 : ((N) >= 8 && (N) <= 13))

/* Define a data type for recording info about an argument list
   during the scan of that argument list.  This data type should
   hold all necessary information about the function itself
   and about the args processed so far, enough to enable macros
   such as FUNCTION_ARG to determine where the next arg should go.

   On SPARC (!v9), this is a single integer, which is a number of words
   of arguments scanned so far (including the invisible argument,
   if any, which holds the structure-value-address).
   Thus 7 or more means all following args should go on the stack.

   For v9, we also need to know whether a prototype is present.  */

struct sparc_args {
  int words;       /* number of words passed so far */
  int prototype_p; /* nonzero if a prototype is present */
  int libcall_p;   /* nonzero if a library call */
};
#define CUMULATIVE_ARGS struct sparc_args

/* Initialize a variable CUM of type CUMULATIVE_ARGS
   for a call to a function whose data type is FNTYPE.
   For a library call, FNTYPE is 0.  */

#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, FNDECL, N_NAMED_ARGS) \
init_cumulative_args (& (CUM), (FNTYPE), (LIBNAME), (FNDECL));

/* Update the data in CUM to advance over an argument
   of mode MODE and data type TYPE.
   TYPE is null for libcalls where that information may not be available.  */

#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED) \
function_arg_advance (& (CUM), (MODE), (TYPE), (NAMED))

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

#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED) \
function_arg (& (CUM), (MODE), (TYPE), (NAMED), 0)

/* Define where a function finds its arguments.
   This is different from FUNCTION_ARG because of register windows.  */

#define FUNCTION_INCOMING_ARG(CUM, MODE, TYPE, NAMED) \
function_arg (& (CUM), (MODE), (TYPE), (NAMED), 1)

/* If defined, a C expression which determines whether, and in which direction,
   to pad out an argument with extra space.  The value should be of type
   `enum direction': either `upward' to pad above the argument,
   `downward' to pad below, or `none' to inhibit padding.  */

#define FUNCTION_ARG_PADDING(MODE, TYPE) \
function_arg_padding ((MODE), (TYPE))

/* If defined, a C expression that gives the alignment boundary, in bits,
   of an argument with the specified mode and type.  If it is not defined,
   PARM_BOUNDARY is used for all arguments.
   For sparc64, objects requiring 16 byte alignment are passed that way.  */

#define FUNCTION_ARG_BOUNDARY(MODE, TYPE) \
((TARGET_ARCH64					\
  && (GET_MODE_ALIGNMENT (MODE) == 128		\
      || ((TYPE) && TYPE_ALIGN (TYPE) == 128)))	\
 ? 128 : PARM_BOUNDARY)

/* Define the information needed to generate branch and scc insns.  This is
   stored from the compare operation.  Note that we can't use "rtx" here
   since it hasn't been defined!  */

extern GTY(()) rtx sparc_compare_op0;
extern GTY(()) rtx sparc_compare_op1;
extern GTY(()) rtx sparc_compare_emitted;


/* Generate the special assembly code needed to tell the assembler whatever
   it might need to know about the return value of a function.

   For SPARC assemblers, we need to output a .proc pseudo-op which conveys
   information to the assembler relating to peephole optimization (done in
   the assembler).  */

#define ASM_DECLARE_RESULT(FILE, RESULT) \
  fprintf ((FILE), "\t.proc\t0%lo\n", sparc_type_code (TREE_TYPE (RESULT)))

/* Output the special assembly code needed to tell the assembler some
   register is used as global register variable.

   SPARC 64bit psABI declares registers %g2 and %g3 as application
   registers and %g6 and %g7 as OS registers.  Any object using them
   should declare (for %g2/%g3 has to, for %g6/%g7 can) that it uses them
   and how they are used (scratch or some global variable).
   Linker will then refuse to link together objects which use those
   registers incompatibly.

   Unless the registers are used for scratch, two different global
   registers cannot be declared to the same name, so in the unlikely
   case of a global register variable occupying more than one register
   we prefix the second and following registers with .gnu.part1. etc.  */

extern GTY(()) char sparc_hard_reg_printed[8];

#ifdef HAVE_AS_REGISTER_PSEUDO_OP
#define ASM_DECLARE_REGISTER_GLOBAL(FILE, DECL, REGNO, NAME)		\
do {									\
  if (TARGET_ARCH64)							\
    {									\
      int end = HARD_REGNO_NREGS ((REGNO), DECL_MODE (decl)) + (REGNO); \
      int reg;								\
      for (reg = (REGNO); reg < 8 && reg < end; reg++)			\
	if ((reg & ~1) == 2 || (reg & ~1) == 6)				\
	  {								\
	    if (reg == (REGNO))						\
	      fprintf ((FILE), "\t.register\t%%g%d, %s\n", reg, (NAME)); \
	    else							\
	      fprintf ((FILE), "\t.register\t%%g%d, .gnu.part%d.%s\n",	\
		       reg, reg - (REGNO), (NAME));			\
	    sparc_hard_reg_printed[reg] = 1;				\
	  }								\
    }									\
} while (0)
#endif


/* Emit rtl for profiling.  */
#define PROFILE_HOOK(LABEL)   sparc_profile_hook (LABEL)

/* All the work done in PROFILE_HOOK, but still required.  */
#define FUNCTION_PROFILER(FILE, LABELNO) do { } while (0)

/* Set the name of the mcount function for the system.  */
#define MCOUNT_FUNCTION "*mcount"

/* EXIT_IGNORE_STACK should be nonzero if, when returning from a function,
   the stack pointer does not matter.  The value is tested only in
   functions that have frame pointers.
   No definition is equivalent to always zero.  */

#define EXIT_IGNORE_STACK	\
 (get_frame_size () != 0	\
  || current_function_calls_alloca || current_function_outgoing_args_size)

/* Define registers used by the epilogue and return instruction.  */
#define EPILOGUE_USES(REGNO) ((REGNO) == 31 \
  || (current_function_calls_eh_return && (REGNO) == 1))

/* Length in units of the trampoline for entering a nested function.  */

#define TRAMPOLINE_SIZE (TARGET_ARCH64 ? 32 : 16)

#define TRAMPOLINE_ALIGNMENT 128 /* 16 bytes */

/* Emit RTL insns to initialize the variable parts of a trampoline.
   FNADDR is an RTX for the address of the function's pure code.
   CXT is an RTX for the static chain value for the function.  */

#define INITIALIZE_TRAMPOLINE(TRAMP, FNADDR, CXT) \
    if (TARGET_ARCH64)						\
      sparc64_initialize_trampoline (TRAMP, FNADDR, CXT);	\
    else							\
      sparc_initialize_trampoline (TRAMP, FNADDR, CXT)

/* Implement `va_start' for varargs and stdarg.  */
#define EXPAND_BUILTIN_VA_START(valist, nextarg) \
  sparc_va_start (valist, nextarg)

/* Generate RTL to flush the register windows so as to make arbitrary frames
   available.  */
#define SETUP_FRAME_ADDRESSES()		\
  emit_insn (gen_flush_register_windows ())

/* Given an rtx for the address of a frame,
   return an rtx for the address of the word in the frame
   that holds the dynamic chain--the previous frame's address.  */
#define DYNAMIC_CHAIN_ADDRESS(frame)	\
  plus_constant (frame, 14 * UNITS_PER_WORD + SPARC_STACK_BIAS)

/* Given an rtx for the frame pointer,
   return an rtx for the address of the frame.  */
#define FRAME_ADDR_RTX(frame) plus_constant (frame, SPARC_STACK_BIAS)

/* The return address isn't on the stack, it is in a register, so we can't
   access it from the current frame pointer.  We can access it from the
   previous frame pointer though by reading a value from the register window
   save area.  */
#define RETURN_ADDR_IN_PREVIOUS_FRAME

/* This is the offset of the return address to the true next instruction to be
   executed for the current function.  */
#define RETURN_ADDR_OFFSET \
  (8 + 4 * (! TARGET_ARCH64 && current_function_returns_struct))

/* The current return address is in %i7.  The return address of anything
   farther back is in the register window save area at [%fp+60].  */
/* ??? This ignores the fact that the actual return address is +8 for normal
   returns, and +12 for structure returns.  */
#define RETURN_ADDR_RTX(count, frame)		\
  ((count == -1)				\
   ? gen_rtx_REG (Pmode, 31)			\
   : gen_rtx_MEM (Pmode,			\
		  memory_address (Pmode, plus_constant (frame, \
							15 * UNITS_PER_WORD \
							+ SPARC_STACK_BIAS))))

/* Before the prologue, the return address is %o7 + 8.  OK, sometimes it's
   +12, but always using +8 is close enough for frame unwind purposes.
   Actually, just using %o7 is close enough for unwinding, but %o7+8
   is something you can return to.  */
#define INCOMING_RETURN_ADDR_RTX \
  plus_constant (gen_rtx_REG (word_mode, 15), 8)
#define DWARF_FRAME_RETURN_COLUMN	DWARF_FRAME_REGNUM (15)

/* The offset from the incoming value of %sp to the top of the stack frame
   for the current function.  On sparc64, we have to account for the stack
   bias if present.  */
#define INCOMING_FRAME_SP_OFFSET SPARC_STACK_BIAS

/* Describe how we implement __builtin_eh_return.  */
#define EH_RETURN_DATA_REGNO(N) ((N) < 4 ? (N) + 24 : INVALID_REGNUM)
#define EH_RETURN_STACKADJ_RTX	gen_rtx_REG (Pmode, 1)	/* %g1 */
#define EH_RETURN_HANDLER_RTX	gen_rtx_REG (Pmode, 31)	/* %i7 */

/* Select a format to encode pointers in exception handling data.  CODE
   is 0 for data, 1 for code labels, 2 for function pointers.  GLOBAL is
   true if the symbol may be affected by dynamic relocations.

   If assembler and linker properly support .uaword %r_disp32(foo),
   then use PC relative 32-bit relocations instead of absolute relocs
   for shared libraries.  On sparc64, use pc relative 32-bit relocs even
   for binaries, to save memory.

   binutils 2.12 would emit a R_SPARC_DISP32 dynamic relocation if the
   symbol %r_disp32() is against was not local, but .hidden.  In that
   case, we have to use DW_EH_PE_absptr for pic personality.  */
#ifdef HAVE_AS_SPARC_UA_PCREL
#ifdef HAVE_AS_SPARC_UA_PCREL_HIDDEN
#define ASM_PREFERRED_EH_DATA_FORMAT(CODE,GLOBAL)			\
  (flag_pic								\
   ? (GLOBAL ? DW_EH_PE_indirect : 0) | DW_EH_PE_pcrel | DW_EH_PE_sdata4\
   : ((TARGET_ARCH64 && ! GLOBAL)					\
      ? (DW_EH_PE_pcrel | DW_EH_PE_sdata4)				\
      : DW_EH_PE_absptr))
#else
#define ASM_PREFERRED_EH_DATA_FORMAT(CODE,GLOBAL)			\
  (flag_pic								\
   ? (GLOBAL ? DW_EH_PE_absptr : (DW_EH_PE_pcrel | DW_EH_PE_sdata4))	\
   : ((TARGET_ARCH64 && ! GLOBAL)					\
      ? (DW_EH_PE_pcrel | DW_EH_PE_sdata4)				\
      : DW_EH_PE_absptr))
#endif

/* Emit a PC-relative relocation.  */
#define ASM_OUTPUT_DWARF_PCREL(FILE, SIZE, LABEL)	\
  do {							\
    fputs (integer_asm_op (SIZE, FALSE), FILE);		\
    fprintf (FILE, "%%r_disp%d(", SIZE * 8);		\
    assemble_name (FILE, LABEL);			\
    fputc (')', FILE);					\
  } while (0)
#endif

/* Addressing modes, and classification of registers for them.  */

/* Macros to check register numbers against specific register classes.  */

/* These assume that REGNO is a hard or pseudo reg number.
   They give nonzero only if REGNO is a hard reg of the suitable class
   or a pseudo reg currently allocated to a suitable hard reg.
   Since they use reg_renumber, they are safe only once reg_renumber
   has been allocated, which happens in local-alloc.c.  */

#define REGNO_OK_FOR_INDEX_P(REGNO) \
((REGNO) < 32 || (unsigned) reg_renumber[REGNO] < (unsigned)32	\
 || (REGNO) == FRAME_POINTER_REGNUM				\
 || reg_renumber[REGNO] == FRAME_POINTER_REGNUM)

#define REGNO_OK_FOR_BASE_P(REGNO)  REGNO_OK_FOR_INDEX_P (REGNO)

#define REGNO_OK_FOR_FP_P(REGNO) \
  (((unsigned) (REGNO) - 32 < (TARGET_V9 ? (unsigned)64 : (unsigned)32)) \
   || ((unsigned) reg_renumber[REGNO] - 32 < (TARGET_V9 ? (unsigned)64 : (unsigned)32)))
#define REGNO_OK_FOR_CCFP_P(REGNO) \
 (TARGET_V9 \
  && (((unsigned) (REGNO) - 96 < (unsigned)4) \
      || ((unsigned) reg_renumber[REGNO] - 96 < (unsigned)4)))

/* Now macros that check whether X is a register and also,
   strictly, whether it is in a specified class.

   These macros are specific to the SPARC, and may be used only
   in code for printing assembler insns and in conditions for
   define_optimization.  */

/* 1 if X is an fp register.  */

#define FP_REG_P(X) (REG_P (X) && REGNO_OK_FOR_FP_P (REGNO (X)))

/* Is X, a REG, an in or global register?  i.e. is regno 0..7 or 24..31 */
#define IN_OR_GLOBAL_P(X) (REGNO (X) < 8 || (REGNO (X) >= 24 && REGNO (X) <= 31))

/* Maximum number of registers that can appear in a valid memory address.  */

#define MAX_REGS_PER_ADDRESS 2

/* Recognize any constant value that is a valid address.
   When PIC, we do not accept an address that would require a scratch reg
   to load into a register.  */

#define CONSTANT_ADDRESS_P(X) constant_address_p (X)

/* Define this, so that when PIC, reload won't try to reload invalid
   addresses which require two reload registers.  */

#define LEGITIMATE_PIC_OPERAND_P(X) legitimate_pic_operand_p (X)

/* Nonzero if the constant value X is a legitimate general operand.
   Anything can be made to work except floating point constants.
   If TARGET_VIS, 0.0 can be made to work as well.  */

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

/* Optional extra constraints for this machine.

   'Q' handles floating point constants which can be moved into
       an integer register with a single sethi instruction.

   'R' handles floating point constants which can be moved into
       an integer register with a single mov instruction.

   'S' handles floating point constants which can be moved into
       an integer register using a high/lo_sum sequence.

   'T' handles memory addresses where the alignment is known to
       be at least 8 bytes.

   `U' handles all pseudo registers or a hard even numbered
       integer register, needed for ldd/std instructions.

   'W' handles the memory operand when moving operands in/out
       of 'e' constraint floating point registers.

   'Y' handles the zero vector constant.  */

#ifndef REG_OK_STRICT

/* Nonzero if X is a hard reg that can be used as an index
   or if it is a pseudo reg.  */
#define REG_OK_FOR_INDEX_P(X) \
  (REGNO (X) < 32				\
   || REGNO (X) == FRAME_POINTER_REGNUM		\
   || REGNO (X) >= FIRST_PSEUDO_REGISTER)

/* Nonzero if X is a hard reg that can be used as a base reg
   or if it is a pseudo reg.  */
#define REG_OK_FOR_BASE_P(X)  REG_OK_FOR_INDEX_P (X)

/* 'T', 'U' are for aligned memory loads which aren't needed for arch64.
   'W' is like 'T' but is assumed true on arch64.

   Remember to accept pseudo-registers for memory constraints if reload is
   in progress.  */

#define EXTRA_CONSTRAINT(OP, C) \
	sparc_extra_constraint_check(OP, C, 0)

#else

/* Nonzero if X is a hard reg that can be used as an index.  */
#define REG_OK_FOR_INDEX_P(X) REGNO_OK_FOR_INDEX_P (REGNO (X))
/* Nonzero if X is a hard reg that can be used as a base reg.  */
#define REG_OK_FOR_BASE_P(X) REGNO_OK_FOR_BASE_P (REGNO (X))

#define EXTRA_CONSTRAINT(OP, C) \
	sparc_extra_constraint_check(OP, C, 1)

#endif

/* Should gcc use [%reg+%lo(xx)+offset] addresses?  */

#ifdef HAVE_AS_OFFSETABLE_LO10
#define USE_AS_OFFSETABLE_LO10 1
#else
#define USE_AS_OFFSETABLE_LO10 0
#endif

/* GO_IF_LEGITIMATE_ADDRESS recognizes an RTL expression
   that is a valid memory address for an instruction.
   The MODE argument is the machine mode for the MEM expression
   that wants to use this address.

   On SPARC, the actual legitimate addresses must be REG+REG or REG+SMALLINT
   ordinarily.  This changes a bit when generating PIC.

   If you change this, execute "rm explow.o recog.o reload.o".  */

#define SYMBOLIC_CONST(X) symbolic_operand (X, VOIDmode)

#define RTX_OK_FOR_BASE_P(X)						\
  ((GET_CODE (X) == REG && REG_OK_FOR_BASE_P (X))			\
  || (GET_CODE (X) == SUBREG						\
      && GET_CODE (SUBREG_REG (X)) == REG				\
      && REG_OK_FOR_BASE_P (SUBREG_REG (X))))

#define RTX_OK_FOR_INDEX_P(X)						\
  ((GET_CODE (X) == REG && REG_OK_FOR_INDEX_P (X))			\
  || (GET_CODE (X) == SUBREG						\
      && GET_CODE (SUBREG_REG (X)) == REG				\
      && REG_OK_FOR_INDEX_P (SUBREG_REG (X))))

#define RTX_OK_FOR_OFFSET_P(X)						\
  (GET_CODE (X) == CONST_INT && INTVAL (X) >= -0x1000 && INTVAL (X) < 0x1000 - 8)

#define RTX_OK_FOR_OLO10_P(X)						\
  (GET_CODE (X) == CONST_INT && INTVAL (X) >= -0x1000 && INTVAL (X) < 0xc00 - 8)

#ifdef REG_OK_STRICT
#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR)		\
{							\
  if (legitimate_address_p (MODE, X, 1))		\
    goto ADDR;						\
}
#else
#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR)		\
{							\
  if (legitimate_address_p (MODE, X, 0))		\
    goto ADDR;						\
}
#endif

/* Go to LABEL if ADDR (a legitimate address expression)
   has an effect that depends on the machine mode it is used for.

   In PIC mode,

      (mem:HI [%l7+a])

   is not equivalent to
   
      (mem:QI [%l7+a]) (mem:QI [%l7+a+1])

   because [%l7+a+1] is interpreted as the address of (a+1).  */

#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR, LABEL)	\
{							\
  if (flag_pic == 1)					\
    {							\
      if (GET_CODE (ADDR) == PLUS)			\
	{						\
	  rtx op0 = XEXP (ADDR, 0);			\
	  rtx op1 = XEXP (ADDR, 1);			\
	  if (op0 == pic_offset_table_rtx		\
	      && SYMBOLIC_CONST (op1))			\
	    goto LABEL;					\
	}						\
    }							\
}

/* Try machine-dependent ways of modifying an illegitimate address
   to be legitimate.  If we find one, return the new, valid address.
   This macro is used in only one place: `memory_address' in explow.c.

   OLDX is the address as it was before break_out_memory_refs was called.
   In some cases it is useful to look at this to decide what needs to be done.

   MODE and WIN are passed so that this macro can use
   GO_IF_LEGITIMATE_ADDRESS.

   It is always safe for this macro to do nothing.  It exists to recognize
   opportunities to optimize the output.  */

/* On SPARC, change REG+N into REG+REG, and REG+(X*Y) into REG+REG.  */
#define LEGITIMIZE_ADDRESS(X,OLDX,MODE,WIN)	\
{						\
  (X) = legitimize_address (X, OLDX, MODE);	\
  if (memory_address_p (MODE, X))		\
    goto WIN;					\
}

/* Try a machine-dependent way of reloading an illegitimate address
   operand.  If we find one, push the reload and jump to WIN.  This
   macro is used in only one place: `find_reloads_address' in reload.c.

   For SPARC 32, we wish to handle addresses by splitting them into
   HIGH+LO_SUM pairs, retaining the LO_SUM in the memory reference.
   This cuts the number of extra insns by one.

   Do nothing when generating PIC code and the address is a
   symbolic operand or requires a scratch register.  */

#define LEGITIMIZE_RELOAD_ADDRESS(X,MODE,OPNUM,TYPE,IND_LEVELS,WIN)     \
do {                                                                    \
  /* Decompose SImode constants into hi+lo_sum.  We do have to 		\
     rerecognize what we produce, so be careful.  */			\
  if (CONSTANT_P (X)							\
      && (MODE != TFmode || TARGET_ARCH64)				\
      && GET_MODE (X) == SImode						\
      && GET_CODE (X) != LO_SUM && GET_CODE (X) != HIGH			\
      && ! (flag_pic							\
	    && (symbolic_operand (X, Pmode)				\
		|| pic_address_needs_scratch (X)))			\
      && sparc_cmodel <= CM_MEDLOW)					\
    {									\
      X = gen_rtx_LO_SUM (GET_MODE (X),					\
			  gen_rtx_HIGH (GET_MODE (X), X), X);		\
      push_reload (XEXP (X, 0), NULL_RTX, &XEXP (X, 0), NULL,		\
                   BASE_REG_CLASS, GET_MODE (X), VOIDmode, 0, 0,	\
                   OPNUM, TYPE);					\
      goto WIN;								\
    }									\
  /* ??? 64-bit reloads.  */						\
} while (0)

/* Specify the machine mode that this machine uses
   for the index in the tablejump instruction.  */
/* If we ever implement any of the full models (such as CM_FULLANY),
   this has to be DImode in that case */
#ifdef HAVE_GAS_SUBSECTION_ORDERING
#define CASE_VECTOR_MODE \
(! TARGET_PTR64 ? SImode : flag_pic ? SImode : TARGET_CM_MEDLOW ? SImode : DImode)
#else
/* If assembler does not have working .subsection -1, we use DImode for pic, as otherwise
   we have to sign extend which slows things down.  */
#define CASE_VECTOR_MODE \
(! TARGET_PTR64 ? SImode : flag_pic ? DImode : TARGET_CM_MEDLOW ? SImode : DImode)
#endif

/* Define this as 1 if `char' should by default be signed; else as 0.  */
#define DEFAULT_SIGNED_CHAR 1

/* Max number of bytes we can move from memory to memory
   in one reasonably fast instruction.  */
#define MOVE_MAX 8

/* If a memory-to-memory move would take MOVE_RATIO or more simple
   move-instruction pairs, we will do a movmem or libcall instead.  */

#define MOVE_RATIO (optimize_size ? 3 : 8)

/* Define if operations between registers always perform the operation
   on the full register even if a narrower mode is specified.  */
#define WORD_REGISTER_OPERATIONS

/* Define if loading in MODE, an integral mode narrower than BITS_PER_WORD
   will either zero-extend or sign-extend.  The value of this macro should
   be the code that says which one of the two operations is implicitly
   done, UNKNOWN if none.  */
#define LOAD_EXTEND_OP(MODE) ZERO_EXTEND

/* Nonzero if access to memory by bytes is slow and undesirable.
   For RISC chips, it means that access to memory by bytes is no
   better than access by words when possible, so grab a whole word
   and maybe make use of that.  */
#define SLOW_BYTE_ACCESS 1

/* Define this to be nonzero if shift instructions ignore all but the low-order
   few bits.  */
#define SHIFT_COUNT_TRUNCATED 1

/* Value is 1 if truncating an integer of INPREC bits to OUTPREC bits
   is done just by pretending it is already truncated.  */
#define TRULY_NOOP_TRUNCATION(OUTPREC, INPREC) 1

/* Specify the machine mode used for addresses.  */
#define Pmode (TARGET_ARCH64 ? DImode : SImode)

/* Given a comparison code (EQ, NE, etc.) and the first operand of a COMPARE,
   return the mode to be used for the comparison.  For floating-point,
   CCFP[E]mode is used.  CC_NOOVmode should be used when the first operand
   is a PLUS, MINUS, NEG, or ASHIFT.  CCmode should be used when no special
   processing is needed.  */
#define SELECT_CC_MODE(OP,X,Y)  select_cc_mode ((OP), (X), (Y))

/* Return nonzero if MODE implies a floating point inequality can be
   reversed.  For SPARC this is always true because we have a full
   compliment of ordered and unordered comparisons, but until generic
   code knows how to reverse it correctly we keep the old definition.  */
#define REVERSIBLE_CC_MODE(MODE) ((MODE) != CCFPEmode && (MODE) != CCFPmode)

/* A function address in a call instruction for indexing purposes.  */
#define FUNCTION_MODE Pmode

/* Define this if addresses of constant functions
   shouldn't be put through pseudo regs where they can be cse'd.
   Desirable on machines where ordinary constants are expensive
   but a CALL with constant address is cheap.  */
#define NO_FUNCTION_CSE

/* alloca should avoid clobbering the old register save area.  */
#define SETJMP_VIA_SAVE_AREA

/* The _Q_* comparison libcalls return booleans.  */
#define FLOAT_LIB_COMPARE_RETURNS_BOOL(MODE, COMPARISON) ((MODE) == TFmode)

/* Assume by default that the _Qp_* 64-bit libcalls are implemented such
   that the inputs are fully consumed before the output memory is clobbered.  */

#define TARGET_BUGGY_QP_LIB	0

/* Assume by default that we do not have the Solaris-specific conversion
   routines nor 64-bit integer multiply and divide routines.  */

#define SUN_CONVERSION_LIBFUNCS 	0
#define DITF_CONVERSION_LIBFUNCS	0
#define SUN_INTEGER_MULTIPLY_64 	0

/* Compute extra cost of moving data between one register class
   and another.  */
#define GENERAL_OR_I64(C) ((C) == GENERAL_REGS || (C) == I64_REGS)
#define REGISTER_MOVE_COST(MODE, CLASS1, CLASS2)		\
  (((FP_REG_CLASS_P (CLASS1) && GENERAL_OR_I64 (CLASS2)) \
    || (GENERAL_OR_I64 (CLASS1) && FP_REG_CLASS_P (CLASS2)) \
    || (CLASS1) == FPCC_REGS || (CLASS2) == FPCC_REGS)		\
   ? ((sparc_cpu == PROCESSOR_ULTRASPARC \
       || sparc_cpu == PROCESSOR_ULTRASPARC3 \
       || sparc_cpu == PROCESSOR_NIAGARA) ? 12 : 6) : 2)

/* Provide the cost of a branch.  For pre-v9 processors we use
   a value of 3 to take into account the potential annulling of
   the delay slot (which ends up being a bubble in the pipeline slot)
   plus a cycle to take into consideration the instruction cache
   effects.

   On v9 and later, which have branch prediction facilities, we set
   it to the depth of the pipeline as that is the cost of a
   mispredicted branch.

   On Niagara, normal branches insert 3 bubbles into the pipe
   and annulled branches insert 4 bubbles.  */

#define BRANCH_COST \
	((sparc_cpu == PROCESSOR_V9 \
	  || sparc_cpu == PROCESSOR_ULTRASPARC) \
	 ? 7 \
         : (sparc_cpu == PROCESSOR_ULTRASPARC3 \
            ? 9 \
	 : (sparc_cpu == PROCESSOR_NIAGARA \
	    ? 4 \
	 : 3)))

#define PREFETCH_BLOCK \
	((sparc_cpu == PROCESSOR_ULTRASPARC \
          || sparc_cpu == PROCESSOR_ULTRASPARC3 \
	  || sparc_cpu == PROCESSOR_NIAGARA) \
         ? 64 : 32)

#define SIMULTANEOUS_PREFETCHES \
	((sparc_cpu == PROCESSOR_ULTRASPARC \
	  || sparc_cpu == PROCESSOR_NIAGARA) \
         ? 2 \
         : (sparc_cpu == PROCESSOR_ULTRASPARC3 \
            ? 8 : 3))

/* Control the assembler format that we output.  */

/* A C string constant describing how to begin a comment in the target
   assembler language.  The compiler assumes that the comment will end at
   the end of the line.  */

#define ASM_COMMENT_START "!"

/* Output to assembler file text saying following lines
   may contain character constants, extra white space, comments, etc.  */

#define ASM_APP_ON ""

/* Output to assembler file text saying following lines
   no longer contain unusual constructs.  */

#define ASM_APP_OFF ""

/* How to refer to registers in assembler output.
   This sequence is indexed by compiler's hard-register-number (see above).  */

#define REGISTER_NAMES \
{"%g0", "%g1", "%g2", "%g3", "%g4", "%g5", "%g6", "%g7",		\
 "%o0", "%o1", "%o2", "%o3", "%o4", "%o5", "%sp", "%o7",		\
 "%l0", "%l1", "%l2", "%l3", "%l4", "%l5", "%l6", "%l7",		\
 "%i0", "%i1", "%i2", "%i3", "%i4", "%i5", "%fp", "%i7",		\
 "%f0", "%f1", "%f2", "%f3", "%f4", "%f5", "%f6", "%f7",		\
 "%f8", "%f9", "%f10", "%f11", "%f12", "%f13", "%f14", "%f15",		\
 "%f16", "%f17", "%f18", "%f19", "%f20", "%f21", "%f22", "%f23",	\
 "%f24", "%f25", "%f26", "%f27", "%f28", "%f29", "%f30", "%f31",	\
 "%f32", "%f33", "%f34", "%f35", "%f36", "%f37", "%f38", "%f39",	\
 "%f40", "%f41", "%f42", "%f43", "%f44", "%f45", "%f46", "%f47",	\
 "%f48", "%f49", "%f50", "%f51", "%f52", "%f53", "%f54", "%f55",	\
 "%f56", "%f57", "%f58", "%f59", "%f60", "%f61", "%f62", "%f63",	\
 "%fcc0", "%fcc1", "%fcc2", "%fcc3", "%icc", "%sfp" }

/* Define additional names for use in asm clobbers and asm declarations.  */

#define ADDITIONAL_REGISTER_NAMES \
{{"ccr", SPARC_ICC_REG}, {"cc", SPARC_ICC_REG}}

/* On Sun 4, this limit is 2048.  We use 1000 to be safe, since the length
   can run past this up to a continuation point.  Once we used 1500, but
   a single entry in C++ can run more than 500 bytes, due to the length of
   mangled symbol names.  dbxout.c should really be fixed to do
   continuations when they are actually needed instead of trying to
   guess...  */
#define DBX_CONTIN_LENGTH 1000

/* This is how to output a command to make the user-level label named NAME
   defined for reference from other files.  */

/* Globalizing directive for a label.  */
#define GLOBAL_ASM_OP "\t.global "

/* The prefix to add to user-visible assembler symbols.  */

#define USER_LABEL_PREFIX "_"

/* This is how to store into the string LABEL
   the symbol_ref name of an internal numbered label where
   PREFIX is the class of label and NUM is the number within the class.
   This is suitable for output with `assemble_name'.  */

#define ASM_GENERATE_INTERNAL_LABEL(LABEL,PREFIX,NUM)	\
  sprintf ((LABEL), "*%s%ld", (PREFIX), (long)(NUM))

/* This is how we hook in and defer the case-vector until the end of
   the function.  */
#define ASM_OUTPUT_ADDR_VEC(LAB,VEC) \
  sparc_defer_case_vector ((LAB),(VEC), 0)

#define ASM_OUTPUT_ADDR_DIFF_VEC(LAB,VEC) \
  sparc_defer_case_vector ((LAB),(VEC), 1)

/* This is how to output an element of a case-vector that is absolute.  */

#define ASM_OUTPUT_ADDR_VEC_ELT(FILE, VALUE)  \
do {									\
  char label[30];							\
  ASM_GENERATE_INTERNAL_LABEL (label, "L", VALUE);			\
  if (CASE_VECTOR_MODE == SImode)					\
    fprintf (FILE, "\t.word\t");					\
  else									\
    fprintf (FILE, "\t.xword\t");					\
  assemble_name (FILE, label);						\
  fputc ('\n', FILE);							\
} while (0)

/* This is how to output an element of a case-vector that is relative.
   (SPARC uses such vectors only when generating PIC.)  */

#define ASM_OUTPUT_ADDR_DIFF_ELT(FILE, BODY, VALUE, REL)		\
do {									\
  char label[30];							\
  ASM_GENERATE_INTERNAL_LABEL (label, "L", (VALUE));			\
  if (CASE_VECTOR_MODE == SImode)					\
    fprintf (FILE, "\t.word\t");					\
  else									\
    fprintf (FILE, "\t.xword\t");					\
  assemble_name (FILE, label);						\
  ASM_GENERATE_INTERNAL_LABEL (label, "L", (REL));			\
  fputc ('-', FILE);							\
  assemble_name (FILE, label);						\
  fputc ('\n', FILE);							\
} while (0)

/* This is what to output before and after case-vector (both
   relative and absolute).  If .subsection -1 works, we put case-vectors
   at the beginning of the current section.  */

#ifdef HAVE_GAS_SUBSECTION_ORDERING

#define ASM_OUTPUT_ADDR_VEC_START(FILE)					\
  fprintf(FILE, "\t.subsection\t-1\n")

#define ASM_OUTPUT_ADDR_VEC_END(FILE)					\
  fprintf(FILE, "\t.previous\n")

#endif

/* This is how to output an assembler line
   that says to advance the location counter
   to a multiple of 2**LOG bytes.  */

#define ASM_OUTPUT_ALIGN(FILE,LOG)	\
  if ((LOG) != 0)			\
    fprintf (FILE, "\t.align %d\n", (1<<(LOG)))

/* This is how to output an assembler line that says to advance
   the location counter to a multiple of 2**LOG bytes using the
   "nop" instruction as padding.  */
#define ASM_OUTPUT_ALIGN_WITH_NOP(FILE,LOG)   \
  if ((LOG) != 0)                             \
    fprintf (FILE, "\t.align %d,0x1000000\n", (1<<(LOG)))

#define ASM_OUTPUT_SKIP(FILE,SIZE)  \
  fprintf (FILE, "\t.skip "HOST_WIDE_INT_PRINT_UNSIGNED"\n", (SIZE))

/* This says how to output an assembler line
   to define a global common symbol.  */

#define ASM_OUTPUT_COMMON(FILE, NAME, SIZE, ROUNDED)  \
( fputs ("\t.common ", (FILE)),		\
  assemble_name ((FILE), (NAME)),		\
  fprintf ((FILE), ","HOST_WIDE_INT_PRINT_UNSIGNED",\"bss\"\n", (SIZE)))

/* This says how to output an assembler line to define a local common
   symbol.  */

#define ASM_OUTPUT_ALIGNED_LOCAL(FILE, NAME, SIZE, ALIGNED)		\
( fputs ("\t.reserve ", (FILE)),					\
  assemble_name ((FILE), (NAME)),					\
  fprintf ((FILE), ","HOST_WIDE_INT_PRINT_UNSIGNED",\"bss\",%u\n",	\
	   (SIZE), ((ALIGNED) / BITS_PER_UNIT)))

/* A C statement (sans semicolon) to output to the stdio stream
   FILE the assembler definition of uninitialized global DECL named
   NAME whose size is SIZE bytes and alignment is ALIGN bytes.
   Try to use asm_output_aligned_bss to implement this macro.  */

#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN)	\
  do {								\
    ASM_OUTPUT_ALIGNED_LOCAL (FILE, NAME, SIZE, ALIGN);		\
  } while (0)

#define IDENT_ASM_OP "\t.ident\t"

/* Output #ident as a .ident.  */

#define ASM_OUTPUT_IDENT(FILE, NAME) \
  fprintf (FILE, "%s\"%s\"\n", IDENT_ASM_OP, NAME);

/* Prettify the assembly.  */

extern int sparc_indent_opcode;

#define ASM_OUTPUT_OPCODE(FILE, PTR)	\
  do {					\
    if (sparc_indent_opcode)		\
      {					\
	putc (' ', FILE);		\
	sparc_indent_opcode = 0;	\
      }					\
  } while (0)

#define SPARC_SYMBOL_REF_TLS_P(RTX) \
  (GET_CODE (RTX) == SYMBOL_REF && SYMBOL_REF_TLS_MODEL (RTX) != 0)

#define PRINT_OPERAND_PUNCT_VALID_P(CHAR) \
  ((CHAR) == '#' || (CHAR) == '*' || (CHAR) == '('		\
   || (CHAR) == ')' || (CHAR) == '_' || (CHAR) == '&')

/* Print operand X (an rtx) in assembler syntax to file FILE.
   CODE is a letter or dot (`z' in `%z0') or 0 if no letter was specified.
   For `%' followed by punctuation, CODE is the punctuation and X is null.  */

#define PRINT_OPERAND(FILE, X, CODE) print_operand (FILE, X, CODE)

/* Print a memory address as an operand to reference that memory location.  */

#define PRINT_OPERAND_ADDRESS(FILE, ADDR)  \
{ register rtx base, index = 0;					\
  int offset = 0;						\
  register rtx addr = ADDR;					\
  if (GET_CODE (addr) == REG)					\
    fputs (reg_names[REGNO (addr)], FILE);			\
  else if (GET_CODE (addr) == PLUS)				\
    {								\
      if (GET_CODE (XEXP (addr, 0)) == CONST_INT)		\
	offset = INTVAL (XEXP (addr, 0)), base = XEXP (addr, 1);\
      else if (GET_CODE (XEXP (addr, 1)) == CONST_INT)		\
	offset = INTVAL (XEXP (addr, 1)), base = XEXP (addr, 0);\
      else							\
	base = XEXP (addr, 0), index = XEXP (addr, 1);		\
      if (GET_CODE (base) == LO_SUM)				\
	{							\
	  gcc_assert (USE_AS_OFFSETABLE_LO10			\
	      	      && TARGET_ARCH64				\
		      && ! TARGET_CM_MEDMID);			\
	  output_operand (XEXP (base, 0), 0);			\
	  fputs ("+%lo(", FILE);				\
	  output_address (XEXP (base, 1));			\
	  fprintf (FILE, ")+%d", offset);			\
	}							\
      else							\
	{							\
	  fputs (reg_names[REGNO (base)], FILE);		\
	  if (index == 0)					\
	    fprintf (FILE, "%+d", offset);			\
	  else if (GET_CODE (index) == REG)			\
	    fprintf (FILE, "+%s", reg_names[REGNO (index)]);	\
	  else if (GET_CODE (index) == SYMBOL_REF		\
		   || GET_CODE (index) == CONST)		\
	    fputc ('+', FILE), output_addr_const (FILE, index);	\
	  else gcc_unreachable ();				\
	}							\
    }								\
  else if (GET_CODE (addr) == MINUS				\
	   && GET_CODE (XEXP (addr, 1)) == LABEL_REF)		\
    {								\
      output_addr_const (FILE, XEXP (addr, 0));			\
      fputs ("-(", FILE);					\
      output_addr_const (FILE, XEXP (addr, 1));			\
      fputs ("-.)", FILE);					\
    }								\
  else if (GET_CODE (addr) == LO_SUM)				\
    {								\
      output_operand (XEXP (addr, 0), 0);			\
      if (TARGET_CM_MEDMID)					\
        fputs ("+%l44(", FILE);					\
      else							\
        fputs ("+%lo(", FILE);					\
      output_address (XEXP (addr, 1));				\
      fputc (')', FILE);					\
    }								\
  else if (flag_pic && GET_CODE (addr) == CONST			\
	   && GET_CODE (XEXP (addr, 0)) == MINUS		\
	   && GET_CODE (XEXP (XEXP (addr, 0), 1)) == CONST	\
	   && GET_CODE (XEXP (XEXP (XEXP (addr, 0), 1), 0)) == MINUS	\
	   && XEXP (XEXP (XEXP (XEXP (addr, 0), 1), 0), 1) == pc_rtx)	\
    {								\
      addr = XEXP (addr, 0);					\
      output_addr_const (FILE, XEXP (addr, 0));			\
      /* Group the args of the second CONST in parenthesis.  */	\
      fputs ("-(", FILE);					\
      /* Skip past the second CONST--it does nothing for us.  */\
      output_addr_const (FILE, XEXP (XEXP (addr, 1), 0));	\
      /* Close the parenthesis.  */				\
      fputc (')', FILE);					\
    }								\
  else								\
    {								\
      output_addr_const (FILE, addr);				\
    }								\
}

/* TLS support defaulting to original Sun flavor.  GNU extensions
   must be activated in separate configuration files.  */
#ifdef HAVE_AS_TLS
#define TARGET_TLS 1
#else
#define TARGET_TLS 0
#endif

#define TARGET_SUN_TLS TARGET_TLS
#define TARGET_GNU_TLS 0

/* The number of Pmode words for the setjmp buffer.  */
#define JMP_BUF_SIZE 12
