/* Definitions of target machine GNU compiler.  IA-64 version.
   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by James E. Wilson <wilson@cygnus.com> and
   		  David Mosberger <davidm@hpl.hp.com>.

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

/* ??? Look at ABI group documents for list of preprocessor macros and
   other features required for ABI compliance.  */

/* ??? Functions containing a non-local goto target save many registers.  Why?
   See for instance execute/920428-2.c.  */


/* Run-time target specifications */

/* Target CPU builtins.  */
#define TARGET_CPU_CPP_BUILTINS()		\
do {						\
	builtin_assert("cpu=ia64");		\
	builtin_assert("machine=ia64");		\
	builtin_define("__ia64");		\
	builtin_define("__ia64__");		\
	builtin_define("__itanium__");		\
	if (TARGET_BIG_ENDIAN)			\
	  builtin_define("__BIG_ENDIAN__");	\
} while (0)

#ifndef SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS
#endif

#define EXTRA_SPECS \
  { "asm_extra", ASM_EXTRA_SPEC }, \
  SUBTARGET_EXTRA_SPECS

#define CC1_SPEC "%(cc1_cpu) "

#define ASM_EXTRA_SPEC ""

/* Variables which are this size or smaller are put in the sdata/sbss
   sections.  */
extern unsigned int ia64_section_threshold;

/* If the assembler supports thread-local storage, assume that the
   system does as well.  If a particular target system has an
   assembler that supports TLS -- but the rest of the system does not
   support TLS -- that system should explicit define TARGET_HAVE_TLS
   to false in its own configuration file.  */
#if !defined(TARGET_HAVE_TLS) && defined(HAVE_AS_TLS)
#define TARGET_HAVE_TLS true
#endif

#define TARGET_TLS14		(ia64_tls_size == 14)
#define TARGET_TLS22		(ia64_tls_size == 22)
#define TARGET_TLS64		(ia64_tls_size == 64)

#define TARGET_HPUX		0
#define TARGET_HPUX_LD		0

#ifndef TARGET_ILP32
#define TARGET_ILP32 0
#endif

#ifndef HAVE_AS_LTOFFX_LDXMOV_RELOCS
#define HAVE_AS_LTOFFX_LDXMOV_RELOCS 0
#endif

/* Values for TARGET_INLINE_FLOAT_DIV, TARGET_INLINE_INT_DIV, and
   TARGET_INLINE_SQRT.  */

enum ia64_inline_type
{
  INL_NO = 0,
  INL_MIN_LAT = 1,
  INL_MAX_THR = 2
};

/* Default target_flags if no switches are specified  */

#ifndef TARGET_DEFAULT
#define TARGET_DEFAULT (MASK_DWARF2_ASM)
#endif

#ifndef TARGET_CPU_DEFAULT
#define TARGET_CPU_DEFAULT 0
#endif

/* Which processor to schedule for. The cpu attribute defines a list
   that mirrors this list, so changes to ia64.md must be made at the
   same time.  */

enum processor_type
{
  PROCESSOR_ITANIUM,			/* Original Itanium.  */
  PROCESSOR_ITANIUM2,
  PROCESSOR_max
};

extern enum processor_type ia64_tune;

/* Sometimes certain combinations of command options do not make sense on a
   particular target machine.  You can define a macro `OVERRIDE_OPTIONS' to
   take account of this.  This macro, if defined, is executed once just after
   all the command options have been parsed.  */

#define OVERRIDE_OPTIONS ia64_override_options ()

/* Some machines may desire to change what optimizations are performed for
   various optimization levels.  This macro, if defined, is executed once just
   after the optimization level is determined and before the remainder of the
   command options have been parsed.  Values set in this macro are used as the
   default values for the other command line options.  */

/* #define OPTIMIZATION_OPTIONS(LEVEL,SIZE) */

/* Driver configuration */

/* A C string constant that tells the GCC driver program options to pass to
   `cc1'.  It can also specify how to translate options you give to GCC into
   options for GCC to pass to the `cc1'.  */

#undef CC1_SPEC
#define CC1_SPEC "%{G*}"

/* A C string constant that tells the GCC driver program options to pass to
   `cc1plus'.  It can also specify how to translate options you give to GCC
   into options for GCC to pass to the `cc1plus'.  */

/* #define CC1PLUS_SPEC "" */

/* Storage Layout */

/* Define this macro to have the value 1 if the most significant bit in a byte
   has the lowest number; otherwise define it to have the value zero.  */

#define BITS_BIG_ENDIAN 0

#define BYTES_BIG_ENDIAN (TARGET_BIG_ENDIAN != 0)

/* Define this macro to have the value 1 if, in a multiword object, the most
   significant word has the lowest number.  */

#define WORDS_BIG_ENDIAN (TARGET_BIG_ENDIAN != 0)

#if defined(__BIG_ENDIAN__)
#define LIBGCC2_WORDS_BIG_ENDIAN 1
#else
#define LIBGCC2_WORDS_BIG_ENDIAN 0
#endif

#define UNITS_PER_WORD 8

#define POINTER_SIZE (TARGET_ILP32 ? 32 : 64)

/* A C expression whose value is zero if pointers that need to be extended
   from being `POINTER_SIZE' bits wide to `Pmode' are sign-extended and one if
   they are zero-extended and negative one if there is a ptr_extend operation.

   You need not define this macro if the `POINTER_SIZE' is equal to the width
   of `Pmode'.  */
/* Need this for 32 bit pointers, see hpux.h for setting it.  */
/* #define POINTERS_EXTEND_UNSIGNED */

/* A macro to update MODE and UNSIGNEDP when an object whose type is TYPE and
   which has the specified mode and signedness is to be stored in a register.
   This macro is only called when TYPE is a scalar type.  */
#define PROMOTE_MODE(MODE,UNSIGNEDP,TYPE)				\
do									\
  {									\
    if (GET_MODE_CLASS (MODE) == MODE_INT				\
	&& GET_MODE_SIZE (MODE) < 4)					\
      (MODE) = SImode;							\
  }									\
while (0)

#define PARM_BOUNDARY 64

/* Define this macro if you wish to preserve a certain alignment for the stack
   pointer.  The definition is a C expression for the desired alignment
   (measured in bits).  */

#define STACK_BOUNDARY 128

/* Align frames on double word boundaries */
#ifndef IA64_STACK_ALIGN
#define IA64_STACK_ALIGN(LOC) (((LOC) + 15) & ~15)
#endif

#define FUNCTION_BOUNDARY 128

/* Optional x86 80-bit float, quad-precision 128-bit float, and quad-word
   128 bit integers all require 128 bit alignment.  */
#define BIGGEST_ALIGNMENT 128

/* If defined, a C expression to compute the alignment for a static variable.
   TYPE is the data type, and ALIGN is the alignment that the object
   would ordinarily have.  The value of this macro is used instead of that
   alignment to align the object.  */

#define DATA_ALIGNMENT(TYPE, ALIGN)		\
  (TREE_CODE (TYPE) == ARRAY_TYPE		\
   && TYPE_MODE (TREE_TYPE (TYPE)) == QImode	\
   && (ALIGN) < BITS_PER_WORD ? BITS_PER_WORD : (ALIGN))

/* If defined, a C expression to compute the alignment given to a constant that
   is being placed in memory.  CONSTANT is the constant and ALIGN is the
   alignment that the object would ordinarily have.  The value of this macro is
   used instead of that alignment to align the object.  */

#define CONSTANT_ALIGNMENT(EXP, ALIGN)  \
  (TREE_CODE (EXP) == STRING_CST	\
   && (ALIGN) < BITS_PER_WORD ? BITS_PER_WORD : (ALIGN))

#define STRICT_ALIGNMENT 1

/* Define this if you wish to imitate the way many other C compilers handle
   alignment of bitfields and the structures that contain them.
   The behavior is that the type written for a bit-field (`int', `short', or
   other integer type) imposes an alignment for the entire structure, as if the
   structure really did contain an ordinary field of that type.  In addition,
   the bit-field is placed within the structure so that it would fit within such
   a field, not crossing a boundary for it.  */
#define PCC_BITFIELD_TYPE_MATTERS 1

/* An integer expression for the size in bits of the largest integer machine
   mode that should actually be used.  */

/* Allow pairs of registers to be used, which is the intent of the default.  */
#define MAX_FIXED_MODE_SIZE GET_MODE_BITSIZE (TImode)

/* By default, the C++ compiler will use function addresses in the
   vtable entries.  Setting this nonzero tells the compiler to use
   function descriptors instead.  The value of this macro says how
   many words wide the descriptor is (normally 2).  It is assumed
   that the address of a function descriptor may be treated as a
   pointer to a function.

   For reasons known only to HP, the vtable entries (as opposed to
   normal function descriptors) are 16 bytes wide in 32-bit mode as
   well, even though the 3rd and 4th words are unused.  */
#define TARGET_VTABLE_USES_DESCRIPTORS (TARGET_ILP32 ? 4 : 2)

/* Due to silliness in the HPUX linker, vtable entries must be
   8-byte aligned even in 32-bit mode.  Rather than create multiple
   ABIs, force this restriction on everyone else too.  */
#define TARGET_VTABLE_ENTRY_ALIGN  64

/* Due to the above, we need extra padding for the data entries below 0
   to retain the alignment of the descriptors.  */
#define TARGET_VTABLE_DATA_ENTRY_DISTANCE (TARGET_ILP32 ? 2 : 1)

/* Layout of Source Language Data Types */

#define INT_TYPE_SIZE 32

#define SHORT_TYPE_SIZE 16

#define LONG_TYPE_SIZE (TARGET_ILP32 ? 32 : 64)

#define LONG_LONG_TYPE_SIZE 64

#define FLOAT_TYPE_SIZE 32

#define DOUBLE_TYPE_SIZE 64

/* long double is XFmode normally, TFmode for HPUX.  */
#define LONG_DOUBLE_TYPE_SIZE (TARGET_HPUX ? 128 : 80)

/* We always want the XFmode operations from libgcc2.c.  */
#define LIBGCC2_LONG_DOUBLE_TYPE_SIZE 80

#define DEFAULT_SIGNED_CHAR 1

/* A C expression for a string describing the name of the data type to use for
   size values.  The typedef name `size_t' is defined using the contents of the
   string.  */
/* ??? Needs to be defined for P64 code.  */
/* #define SIZE_TYPE */

/* A C expression for a string describing the name of the data type to use for
   the result of subtracting two pointers.  The typedef name `ptrdiff_t' is
   defined using the contents of the string.  See `SIZE_TYPE' above for more
   information.  */
/* ??? Needs to be defined for P64 code.  */
/* #define PTRDIFF_TYPE */

/* A C expression for a string describing the name of the data type to use for
   wide characters.  The typedef name `wchar_t' is defined using the contents
   of the string.  See `SIZE_TYPE' above for more information.  */
/* #define WCHAR_TYPE */

/* A C expression for the size in bits of the data type for wide characters.
   This is used in `cpp', which cannot make use of `WCHAR_TYPE'.  */
/* #define WCHAR_TYPE_SIZE */


/* Register Basics */

/* Number of hardware registers known to the compiler.
   We have 128 general registers, 128 floating point registers,
   64 predicate registers, 8 branch registers, one frame pointer,
   and several "application" registers.  */

#define FIRST_PSEUDO_REGISTER 334

/* Ranges for the various kinds of registers.  */
#define ADDL_REGNO_P(REGNO) ((unsigned HOST_WIDE_INT) (REGNO) <= 3)
#define GR_REGNO_P(REGNO) ((unsigned HOST_WIDE_INT) (REGNO) <= 127)
#define FR_REGNO_P(REGNO) ((REGNO) >= 128 && (REGNO) <= 255)
#define FP_REGNO_P(REGNO) ((REGNO) >= 128 && (REGNO) <= 254 && (REGNO) != 159)
#define PR_REGNO_P(REGNO) ((REGNO) >= 256 && (REGNO) <= 319)
#define BR_REGNO_P(REGNO) ((REGNO) >= 320 && (REGNO) <= 327)
#define GENERAL_REGNO_P(REGNO) \
  (GR_REGNO_P (REGNO) || (REGNO) == FRAME_POINTER_REGNUM)

#define GR_REG(REGNO) ((REGNO) + 0)
#define FR_REG(REGNO) ((REGNO) + 128)
#define PR_REG(REGNO) ((REGNO) + 256)
#define BR_REG(REGNO) ((REGNO) + 320)
#define OUT_REG(REGNO) ((REGNO) + 120)
#define IN_REG(REGNO) ((REGNO) + 112)
#define LOC_REG(REGNO) ((REGNO) + 32)

#define AR_CCV_REGNUM	329
#define AR_UNAT_REGNUM  330
#define AR_PFS_REGNUM	331
#define AR_LC_REGNUM	332
#define AR_EC_REGNUM	333

#define IN_REGNO_P(REGNO) ((REGNO) >= IN_REG (0) && (REGNO) <= IN_REG (7))
#define LOC_REGNO_P(REGNO) ((REGNO) >= LOC_REG (0) && (REGNO) <= LOC_REG (79))
#define OUT_REGNO_P(REGNO) ((REGNO) >= OUT_REG (0) && (REGNO) <= OUT_REG (7))

#define AR_M_REGNO_P(REGNO) ((REGNO) == AR_CCV_REGNUM \
			     || (REGNO) == AR_UNAT_REGNUM)
#define AR_I_REGNO_P(REGNO) ((REGNO) >= AR_PFS_REGNUM \
			     && (REGNO) < FIRST_PSEUDO_REGISTER)
#define AR_REGNO_P(REGNO) ((REGNO) >= AR_CCV_REGNUM \
			   && (REGNO) < FIRST_PSEUDO_REGISTER)


/* ??? Don't really need two sets of macros.  I like this one better because
   it is less typing.  */
#define R_GR(REGNO) GR_REG (REGNO)
#define R_FR(REGNO) FR_REG (REGNO)
#define R_PR(REGNO) PR_REG (REGNO)
#define R_BR(REGNO) BR_REG (REGNO)

/* An initializer that says which registers are used for fixed purposes all
   throughout the compiled code and are therefore not available for general
   allocation.

   r0: constant 0
   r1: global pointer (gp)
   r12: stack pointer (sp)
   r13: thread pointer (tp)
   f0: constant 0.0
   f1: constant 1.0
   p0: constant true
   fp: eliminable frame pointer */

/* The last 16 stacked regs are reserved for the 8 input and 8 output
   registers.  */

#define FIXED_REGISTERS \
{ /* General registers.  */				\
  1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  /* Floating-point registers.  */			\
  1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  /* Predicate registers.  */				\
  1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  /* Branch registers.  */				\
  0, 0, 0, 0, 0, 0, 0, 0,				\
  /*FP CCV UNAT PFS LC EC */				\
     1,  1,   1,  1, 0, 1				\
 }

/* Like `FIXED_REGISTERS' but has 1 for each register that is clobbered
   (in general) by function calls as well as for fixed registers.  This
   macro therefore identifies the registers that are not available for
   general allocation of values that must live across function calls.  */

#define CALL_USED_REGISTERS \
{ /* General registers.  */				\
  1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,	\
  /* Floating-point registers.  */			\
  1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	\
  /* Predicate registers.  */				\
  1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  /* Branch registers.  */				\
  1, 0, 0, 0, 0, 0, 1, 1,				\
  /*FP CCV UNAT PFS LC EC */				\
     1,  1,   1,  1, 0, 1				\
}

/* Like `CALL_USED_REGISTERS' but used to overcome a historical
   problem which makes CALL_USED_REGISTERS *always* include
   all the FIXED_REGISTERS.  Until this problem has been
   resolved this macro can be used to overcome this situation.
   In particular, block_propagate() requires this list
   be accurate, or we can remove registers which should be live.
   This macro is used in regs_invalidated_by_call.  */

#define CALL_REALLY_USED_REGISTERS \
{ /* General registers.  */				\
  0, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,	\
  /* Floating-point registers.  */			\
  0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	\
  /* Predicate registers.  */				\
  0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
  /* Branch registers.  */				\
  1, 0, 0, 0, 0, 0, 1, 1,				\
  /*FP CCV UNAT PFS LC EC */				\
     0,  1,   0,  1, 0, 0				\
}


/* Define this macro if the target machine has register windows.  This C
   expression returns the register number as seen by the called function
   corresponding to the register number OUT as seen by the calling function.
   Return OUT if register number OUT is not an outbound register.  */

#define INCOMING_REGNO(OUT) \
  ((unsigned) ((OUT) - OUT_REG (0)) < 8 ? IN_REG ((OUT) - OUT_REG (0)) : (OUT))

/* Define this macro if the target machine has register windows.  This C
   expression returns the register number as seen by the calling function
   corresponding to the register number IN as seen by the called function.
   Return IN if register number IN is not an inbound register.  */

#define OUTGOING_REGNO(IN) \
  ((unsigned) ((IN) - IN_REG (0)) < 8 ? OUT_REG ((IN) - IN_REG (0)) : (IN))

/* Define this macro if the target machine has register windows.  This
   C expression returns true if the register is call-saved but is in the
   register window.  */

#define LOCAL_REGNO(REGNO) \
  (IN_REGNO_P (REGNO) || LOC_REGNO_P (REGNO))

/* We define CCImode in ia64-modes.def so we need a selector.  */

#define SELECT_CC_MODE(OP,X,Y)  CCmode

/* Order of allocation of registers */

/* If defined, an initializer for a vector of integers, containing the numbers
   of hard registers in the order in which GCC should prefer to use them
   (from most preferred to least).

   If this macro is not defined, registers are used lowest numbered first (all
   else being equal).

   One use of this macro is on machines where the highest numbered registers
   must always be saved and the save-multiple-registers instruction supports
   only sequences of consecutive registers.  On such machines, define
   `REG_ALLOC_ORDER' to be an initializer that lists the highest numbered
   allocatable register first.  */

/* ??? Should the GR return value registers come before or after the rest
   of the caller-save GRs?  */

#define REG_ALLOC_ORDER							   \
{									   \
  /* Caller-saved general registers.  */				   \
  R_GR (14), R_GR (15), R_GR (16), R_GR (17),				   \
  R_GR (18), R_GR (19), R_GR (20), R_GR (21), R_GR (22), R_GR (23),	   \
  R_GR (24), R_GR (25), R_GR (26), R_GR (27), R_GR (28), R_GR (29),	   \
  R_GR (30), R_GR (31),							   \
  /* Output registers.  */						   \
  R_GR (120), R_GR (121), R_GR (122), R_GR (123), R_GR (124), R_GR (125),  \
  R_GR (126), R_GR (127),						   \
  /* Caller-saved general registers, also used for return values.  */	   \
  R_GR (8), R_GR (9), R_GR (10), R_GR (11),				   \
  /* addl caller-saved general registers.  */				   \
  R_GR (2), R_GR (3),							   \
  /* Caller-saved FP registers.  */					   \
  R_FR (6), R_FR (7),							   \
  /* Caller-saved FP registers, used for parameters and return values.  */ \
  R_FR (8), R_FR (9), R_FR (10), R_FR (11),				   \
  R_FR (12), R_FR (13), R_FR (14), R_FR (15),				   \
  /* Rotating caller-saved FP registers.  */				   \
  R_FR (32), R_FR (33), R_FR (34), R_FR (35),				   \
  R_FR (36), R_FR (37), R_FR (38), R_FR (39), R_FR (40), R_FR (41),	   \
  R_FR (42), R_FR (43), R_FR (44), R_FR (45), R_FR (46), R_FR (47),	   \
  R_FR (48), R_FR (49), R_FR (50), R_FR (51), R_FR (52), R_FR (53),	   \
  R_FR (54), R_FR (55), R_FR (56), R_FR (57), R_FR (58), R_FR (59),	   \
  R_FR (60), R_FR (61), R_FR (62), R_FR (63), R_FR (64), R_FR (65),	   \
  R_FR (66), R_FR (67), R_FR (68), R_FR (69), R_FR (70), R_FR (71),	   \
  R_FR (72), R_FR (73), R_FR (74), R_FR (75), R_FR (76), R_FR (77),	   \
  R_FR (78), R_FR (79), R_FR (80), R_FR (81), R_FR (82), R_FR (83),	   \
  R_FR (84), R_FR (85), R_FR (86), R_FR (87), R_FR (88), R_FR (89),	   \
  R_FR (90), R_FR (91), R_FR (92), R_FR (93), R_FR (94), R_FR (95),	   \
  R_FR (96), R_FR (97), R_FR (98), R_FR (99), R_FR (100), R_FR (101),	   \
  R_FR (102), R_FR (103), R_FR (104), R_FR (105), R_FR (106), R_FR (107),  \
  R_FR (108), R_FR (109), R_FR (110), R_FR (111), R_FR (112), R_FR (113),  \
  R_FR (114), R_FR (115), R_FR (116), R_FR (117), R_FR (118), R_FR (119),  \
  R_FR (120), R_FR (121), R_FR (122), R_FR (123), R_FR (124), R_FR (125),  \
  R_FR (126), R_FR (127),						   \
  /* Caller-saved predicate registers.  */				   \
  R_PR (6), R_PR (7), R_PR (8), R_PR (9), R_PR (10), R_PR (11),		   \
  R_PR (12), R_PR (13), R_PR (14), R_PR (15),				   \
  /* Rotating caller-saved predicate registers.  */			   \
  R_PR (16), R_PR (17),							   \
  R_PR (18), R_PR (19), R_PR (20), R_PR (21), R_PR (22), R_PR (23),	   \
  R_PR (24), R_PR (25), R_PR (26), R_PR (27), R_PR (28), R_PR (29),	   \
  R_PR (30), R_PR (31), R_PR (32), R_PR (33), R_PR (34), R_PR (35),	   \
  R_PR (36), R_PR (37), R_PR (38), R_PR (39), R_PR (40), R_PR (41),	   \
  R_PR (42), R_PR (43), R_PR (44), R_PR (45), R_PR (46), R_PR (47),	   \
  R_PR (48), R_PR (49), R_PR (50), R_PR (51), R_PR (52), R_PR (53),	   \
  R_PR (54), R_PR (55), R_PR (56), R_PR (57), R_PR (58), R_PR (59),	   \
  R_PR (60), R_PR (61), R_PR (62), R_PR (63),				   \
  /* Caller-saved branch registers.  */					   \
  R_BR (6), R_BR (7),							   \
									   \
  /* Stacked callee-saved general registers.  */			   \
  R_GR (32), R_GR (33), R_GR (34), R_GR (35),				   \
  R_GR (36), R_GR (37), R_GR (38), R_GR (39), R_GR (40), R_GR (41),	   \
  R_GR (42), R_GR (43), R_GR (44), R_GR (45), R_GR (46), R_GR (47),	   \
  R_GR (48), R_GR (49), R_GR (50), R_GR (51), R_GR (52), R_GR (53),	   \
  R_GR (54), R_GR (55), R_GR (56), R_GR (57), R_GR (58), R_GR (59),	   \
  R_GR (60), R_GR (61), R_GR (62), R_GR (63), R_GR (64), R_GR (65),	   \
  R_GR (66), R_GR (67), R_GR (68), R_GR (69), R_GR (70), R_GR (71),	   \
  R_GR (72), R_GR (73), R_GR (74), R_GR (75), R_GR (76), R_GR (77),	   \
  R_GR (78), R_GR (79), R_GR (80), R_GR (81), R_GR (82), R_GR (83),	   \
  R_GR (84), R_GR (85), R_GR (86), R_GR (87), R_GR (88), R_GR (89),	   \
  R_GR (90), R_GR (91), R_GR (92), R_GR (93), R_GR (94), R_GR (95),	   \
  R_GR (96), R_GR (97), R_GR (98), R_GR (99), R_GR (100), R_GR (101),	   \
  R_GR (102), R_GR (103), R_GR (104), R_GR (105), R_GR (106), R_GR (107),  \
  R_GR (108),								   \
  /* Input registers.  */						   \
  R_GR (112), R_GR (113), R_GR (114), R_GR (115), R_GR (116), R_GR (117),  \
  R_GR (118), R_GR (119),						   \
  /* Callee-saved general registers.  */				   \
  R_GR (4), R_GR (5), R_GR (6), R_GR (7),				   \
  /* Callee-saved FP registers.  */					   \
  R_FR (2), R_FR (3), R_FR (4), R_FR (5), R_FR (16), R_FR (17),		   \
  R_FR (18), R_FR (19), R_FR (20), R_FR (21), R_FR (22), R_FR (23),	   \
  R_FR (24), R_FR (25), R_FR (26), R_FR (27), R_FR (28), R_FR (29),	   \
  R_FR (30), R_FR (31),							   \
  /* Callee-saved predicate registers.  */				   \
  R_PR (1), R_PR (2), R_PR (3), R_PR (4), R_PR (5),			   \
  /* Callee-saved branch registers.  */					   \
  R_BR (1), R_BR (2), R_BR (3), R_BR (4), R_BR (5),			   \
									   \
  /* ??? Stacked registers reserved for fp, rp, and ar.pfs.  */		   \
  R_GR (109), R_GR (110), R_GR (111),					   \
									   \
  /* Special general registers.  */					   \
  R_GR (0), R_GR (1), R_GR (12), R_GR (13),				   \
  /* Special FP registers.  */						   \
  R_FR (0), R_FR (1),							   \
  /* Special predicate registers.  */					   \
  R_PR (0),								   \
  /* Special branch registers.  */					   \
  R_BR (0),								   \
  /* Other fixed registers.  */						   \
  FRAME_POINTER_REGNUM, 						   \
  AR_CCV_REGNUM, AR_UNAT_REGNUM, AR_PFS_REGNUM, AR_LC_REGNUM,		   \
  AR_EC_REGNUM		  						   \
}

/* How Values Fit in Registers */

/* A C expression for the number of consecutive hard registers, starting at
   register number REGNO, required to hold a value of mode MODE.  */

/* ??? We say that BImode PR values require two registers.  This allows us to
   easily store the normal and inverted values.  We use CCImode to indicate
   a single predicate register.  */

#define HARD_REGNO_NREGS(REGNO, MODE)					\
  ((REGNO) == PR_REG (0) && (MODE) == DImode ? 64			\
   : PR_REGNO_P (REGNO) && (MODE) == BImode ? 2				\
   : PR_REGNO_P (REGNO) && (MODE) == CCImode ? 1			\
   : FR_REGNO_P (REGNO) && (MODE) == XFmode ? 1				\
   : FR_REGNO_P (REGNO) && (MODE) == XCmode ? 2				\
   : (GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* A C expression that is nonzero if it is permissible to store a value of mode
   MODE in hard register number REGNO (or in several registers starting with
   that one).  */

#define HARD_REGNO_MODE_OK(REGNO, MODE)				\
  (FR_REGNO_P (REGNO) ?						\
     GET_MODE_CLASS (MODE) != MODE_CC &&			\
     (MODE) != BImode &&					\
     (MODE) != TFmode 						\
   : PR_REGNO_P (REGNO) ?					\
     (MODE) == BImode || GET_MODE_CLASS (MODE) == MODE_CC	\
   : GR_REGNO_P (REGNO) ?					\
     (MODE) != CCImode && (MODE) != XFmode && (MODE) != XCmode	\
   : AR_REGNO_P (REGNO) ? (MODE) == DImode			\
   : BR_REGNO_P (REGNO) ? (MODE) == DImode			\
   : 0)

/* A C expression that is nonzero if it is desirable to choose register
   allocation so as to avoid move instructions between a value of mode MODE1
   and a value of mode MODE2.

   If `HARD_REGNO_MODE_OK (R, MODE1)' and `HARD_REGNO_MODE_OK (R, MODE2)' are
   ever different for any R, then `MODES_TIEABLE_P (MODE1, MODE2)' must be
   zero.  */
/* Don't tie integer and FP modes, as that causes us to get integer registers
   allocated for FP instructions.  XFmode only supported in FP registers so
   we can't tie it with any other modes.  */
#define MODES_TIEABLE_P(MODE1, MODE2)			\
  (GET_MODE_CLASS (MODE1) == GET_MODE_CLASS (MODE2)	\
   && ((((MODE1) == XFmode) || ((MODE1) == XCmode))	\
       == (((MODE2) == XFmode) || ((MODE2) == XCmode)))	\
   && (((MODE1) == BImode) == ((MODE2) == BImode)))

/* Specify the modes required to caller save a given hard regno.
   We need to ensure floating pt regs are not saved as DImode.  */

#define HARD_REGNO_CALLER_SAVE_MODE(REGNO, NREGS, MODE) \
  ((FR_REGNO_P (REGNO) && (NREGS) == 1) ? XFmode        \
   : choose_hard_reg_mode ((REGNO), (NREGS), false))

/* Handling Leaf Functions */

/* A C initializer for a vector, indexed by hard register number, which
   contains 1 for a register that is allowable in a candidate for leaf function
   treatment.  */
/* ??? This might be useful.  */
/* #define LEAF_REGISTERS */

/* A C expression whose value is the register number to which REGNO should be
   renumbered, when a function is treated as a leaf function.  */
/* ??? This might be useful.  */
/* #define LEAF_REG_REMAP(REGNO) */


/* Register Classes */

/* An enumeral type that must be defined with all the register class names as
   enumeral values.  `NO_REGS' must be first.  `ALL_REGS' must be the last
   register class, followed by one more enumeral value, `LIM_REG_CLASSES',
   which is not a register class but rather tells how many classes there
   are.  */
/* ??? When compiling without optimization, it is possible for the only use of
   a pseudo to be a parameter load from the stack with a REG_EQUIV note.
   Regclass handles this case specially and does not assign any costs to the
   pseudo.  The pseudo then ends up using the last class before ALL_REGS.
   Thus we must not let either PR_REGS or BR_REGS be the last class.  The
   testcase for this is gcc.c-torture/execute/va-arg-7.c.  */
enum reg_class
{
  NO_REGS,
  PR_REGS,
  BR_REGS,
  AR_M_REGS,
  AR_I_REGS,
  ADDL_REGS,
  GR_REGS,
  FP_REGS,
  FR_REGS,
  GR_AND_BR_REGS,
  GR_AND_FR_REGS,
  ALL_REGS,
  LIM_REG_CLASSES
};

#define GENERAL_REGS GR_REGS

/* The number of distinct register classes.  */
#define N_REG_CLASSES ((int) LIM_REG_CLASSES)

/* An initializer containing the names of the register classes as C string
   constants.  These names are used in writing some of the debugging dumps.  */
#define REG_CLASS_NAMES \
{ "NO_REGS", "PR_REGS", "BR_REGS", "AR_M_REGS", "AR_I_REGS", \
  "ADDL_REGS", "GR_REGS", "FP_REGS", "FR_REGS", \
  "GR_AND_BR_REGS", "GR_AND_FR_REGS", "ALL_REGS" }

/* An initializer containing the contents of the register classes, as integers
   which are bit masks.  The Nth integer specifies the contents of class N.
   The way the integer MASK is interpreted is that register R is in the class
   if `MASK & (1 << R)' is 1.  */
#define REG_CLASS_CONTENTS \
{ 							\
  /* NO_REGS.  */					\
  { 0x00000000, 0x00000000, 0x00000000, 0x00000000,	\
    0x00000000, 0x00000000, 0x00000000, 0x00000000,	\
    0x00000000, 0x00000000, 0x0000 },			\
  /* PR_REGS.  */					\
  { 0x00000000, 0x00000000, 0x00000000, 0x00000000,	\
    0x00000000, 0x00000000, 0x00000000, 0x00000000,	\
    0xFFFFFFFF, 0xFFFFFFFF, 0x0000 },			\
  /* BR_REGS.  */					\
  { 0x00000000, 0x00000000, 0x00000000, 0x00000000,	\
    0x00000000, 0x00000000, 0x00000000, 0x00000000,	\
    0x00000000, 0x00000000, 0x00FF },			\
  /* AR_M_REGS.  */					\
  { 0x00000000, 0x00000000, 0x00000000, 0x00000000,	\
    0x00000000, 0x00000000, 0x00000000, 0x00000000,	\
    0x00000000, 0x00000000, 0x0600 },			\
  /* AR_I_REGS.  */					\
  { 0x00000000, 0x00000000, 0x00000000, 0x00000000,	\
    0x00000000, 0x00000000, 0x00000000, 0x00000000,	\
    0x00000000, 0x00000000, 0x3800 },			\
  /* ADDL_REGS.  */					\
  { 0x0000000F, 0x00000000, 0x00000000, 0x00000000,	\
    0x00000000, 0x00000000, 0x00000000, 0x00000000,	\
    0x00000000, 0x00000000, 0x0000 },			\
  /* GR_REGS.  */					\
  { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,	\
    0x00000000, 0x00000000, 0x00000000, 0x00000000,	\
    0x00000000, 0x00000000, 0x0100 },			\
  /* FP_REGS.  */					\
  { 0x00000000, 0x00000000, 0x00000000, 0x00000000,	\
    0x7FFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x7FFFFFFF,	\
    0x00000000, 0x00000000, 0x0000 },			\
  /* FR_REGS.  */					\
  { 0x00000000, 0x00000000, 0x00000000, 0x00000000,	\
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,	\
    0x00000000, 0x00000000, 0x0000 },			\
  /* GR_AND_BR_REGS.  */				\
  { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,	\
    0x00000000, 0x00000000, 0x00000000, 0x00000000,	\
    0x00000000, 0x00000000, 0x01FF },			\
  /* GR_AND_FR_REGS.  */				\
  { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,	\
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,	\
    0x00000000, 0x00000000, 0x0100 },			\
  /* ALL_REGS.  */					\
  { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,	\
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,	\
    0xFFFFFFFF, 0xFFFFFFFF, 0x3FFF },			\
}

/* A C expression whose value is a register class containing hard register
   REGNO.  In general there is more than one such class; choose a class which
   is "minimal", meaning that no smaller class also contains the register.  */
/* The NO_REGS case is primarily for the benefit of rws_access_reg, which
   may call here with private (invalid) register numbers, such as
   REG_VOLATILE.  */
#define REGNO_REG_CLASS(REGNO) \
(ADDL_REGNO_P (REGNO) ? ADDL_REGS	\
 : GENERAL_REGNO_P (REGNO) ? GR_REGS	\
 : FR_REGNO_P (REGNO) ? (REGNO) != R_FR (31) \
			&& (REGNO) != R_FR(127) ? FP_REGS : FR_REGS \
 : PR_REGNO_P (REGNO) ? PR_REGS		\
 : BR_REGNO_P (REGNO) ? BR_REGS		\
 : AR_M_REGNO_P (REGNO) ? AR_M_REGS	\
 : AR_I_REGNO_P (REGNO) ? AR_I_REGS	\
 : NO_REGS)

/* A macro whose definition is the name of the class to which a valid base
   register must belong.  A base register is one used in an address which is
   the register value plus a displacement.  */
#define BASE_REG_CLASS GENERAL_REGS

/* A macro whose definition is the name of the class to which a valid index
   register must belong.  An index register is one used in an address where its
   value is either multiplied by a scale factor or added to another register
   (as well as added to a displacement).  This is needed for POST_MODIFY.  */
#define INDEX_REG_CLASS GENERAL_REGS

/* A C expression which defines the machine-dependent operand constraint
   letters for register classes.  If CHAR is such a letter, the value should be
   the register class corresponding to it.  Otherwise, the value should be
   `NO_REGS'.  The register letter `r', corresponding to class `GENERAL_REGS',
   will not be passed to this macro; you do not need to handle it.  */

#define REG_CLASS_FROM_LETTER(CHAR) \
((CHAR) == 'f' ? FR_REGS		\
 : (CHAR) == 'a' ? ADDL_REGS		\
 : (CHAR) == 'b' ? BR_REGS		\
 : (CHAR) == 'c' ? PR_REGS		\
 : (CHAR) == 'd' ? AR_M_REGS		\
 : (CHAR) == 'e' ? AR_I_REGS		\
 : (CHAR) == 'x' ? FP_REGS		\
 : NO_REGS)

/* A C expression which is nonzero if register number NUM is suitable for use
   as a base register in operand addresses.  It may be either a suitable hard
   register or a pseudo register that has been allocated such a hard reg.  */
#define REGNO_OK_FOR_BASE_P(REGNO) \
  (GENERAL_REGNO_P (REGNO) || GENERAL_REGNO_P (reg_renumber[REGNO]))

/* A C expression which is nonzero if register number NUM is suitable for use
   as an index register in operand addresses.  It may be either a suitable hard
   register or a pseudo register that has been allocated such a hard reg.
   This is needed for POST_MODIFY.  */
#define REGNO_OK_FOR_INDEX_P(NUM) REGNO_OK_FOR_BASE_P (NUM)

/* A C expression that places additional restrictions on the register class to
   use when it is necessary to copy value X into a register in class CLASS.
   The value is a register class; perhaps CLASS, or perhaps another, smaller
   class.  */

#define PREFERRED_RELOAD_CLASS(X, CLASS) \
  ia64_preferred_reload_class (X, CLASS)

/* You should define this macro to indicate to the reload phase that it may
   need to allocate at least one register for a reload in addition to the
   register to contain the data.  Specifically, if copying X to a register
   CLASS in MODE requires an intermediate register, you should define this
   to return the largest register class all of whose registers can be used
   as intermediate registers or scratch registers.  */

#define SECONDARY_RELOAD_CLASS(CLASS, MODE, X) \
 ia64_secondary_reload_class (CLASS, MODE, X)

/* Certain machines have the property that some registers cannot be copied to
   some other registers without using memory.  Define this macro on those
   machines to be a C expression that is nonzero if objects of mode M in
   registers of CLASS1 can only be copied to registers of class CLASS2 by
   storing a register of CLASS1 into memory and loading that memory location
   into a register of CLASS2.  */

#if 0
/* ??? May need this, but since we've disallowed XFmode in GR_REGS,
   I'm not quite sure how it could be invoked.  The normal problems
   with unions should be solved with the addressof fiddling done by
   movxf and friends.  */
#define SECONDARY_MEMORY_NEEDED(CLASS1, CLASS2, MODE)			\
  (((MODE) == XFmode || (MODE) == XCmode)				\
   && (((CLASS1) == GR_REGS && (CLASS2) == FR_REGS)			\
       || ((CLASS1) == FR_REGS && (CLASS2) == GR_REGS)))
#endif

/* A C expression for the maximum number of consecutive registers of
   class CLASS needed to hold a value of mode MODE.
   This is closely related to the macro `HARD_REGNO_NREGS'.  */

#define CLASS_MAX_NREGS(CLASS, MODE) \
  ((MODE) == BImode && (CLASS) == PR_REGS ? 2			\
   : (((CLASS) == FR_REGS || (CLASS) == FP_REGS) && (MODE) == XFmode) ? 1 \
   : (((CLASS) == FR_REGS || (CLASS) == FP_REGS) && (MODE) == XCmode) ? 2 \
   : (GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* In FP regs, we can't change FP values to integer values and vice versa,
   but we can change e.g. DImode to SImode, and V2SFmode into DImode.  */

#define CANNOT_CHANGE_MODE_CLASS(FROM, TO, CLASS) 		\
  (SCALAR_FLOAT_MODE_P (FROM) != SCALAR_FLOAT_MODE_P (TO)	\
   ? reg_classes_intersect_p (CLASS, FR_REGS) : 0)

/* A C expression that defines the machine-dependent operand constraint
   letters (`I', `J', `K', .. 'P') that specify particular ranges of
   integer values.  */

/* 14 bit signed immediate for arithmetic instructions.  */
#define CONST_OK_FOR_I(VALUE) \
  ((unsigned HOST_WIDE_INT)(VALUE) + 0x2000 < 0x4000)
/* 22 bit signed immediate for arith instructions with r0/r1/r2/r3 source.  */
#define CONST_OK_FOR_J(VALUE) \
  ((unsigned HOST_WIDE_INT)(VALUE) + 0x200000 < 0x400000)
/* 8 bit signed immediate for logical instructions.  */
#define CONST_OK_FOR_K(VALUE) ((unsigned HOST_WIDE_INT)(VALUE) + 0x80 < 0x100)
/* 8 bit adjusted signed immediate for compare pseudo-ops.  */
#define CONST_OK_FOR_L(VALUE) ((unsigned HOST_WIDE_INT)(VALUE) + 0x7F < 0x100)
/* 6 bit unsigned immediate for shift counts.  */
#define CONST_OK_FOR_M(VALUE) ((unsigned HOST_WIDE_INT)(VALUE) < 0x40)
/* 9 bit signed immediate for load/store post-increments.  */
#define CONST_OK_FOR_N(VALUE) ((unsigned HOST_WIDE_INT)(VALUE) + 0x100 < 0x200)
/* 0 for r0.  Used by Linux kernel, do not change.  */
#define CONST_OK_FOR_O(VALUE) ((VALUE) == 0)
/* 0 or -1 for dep instruction.  */
#define CONST_OK_FOR_P(VALUE) ((VALUE) == 0 || (VALUE) == -1)

#define CONST_OK_FOR_LETTER_P(VALUE, C) \
  ia64_const_ok_for_letter_p (VALUE, C)

/* A C expression that defines the machine-dependent operand constraint letters
   (`G', `H') that specify particular ranges of `const_double' values.  */

/* 0.0 and 1.0 for fr0 and fr1.  */
#define CONST_DOUBLE_OK_FOR_G(VALUE) \
  ((VALUE) == CONST0_RTX (GET_MODE (VALUE))	\
   || (VALUE) == CONST1_RTX (GET_MODE (VALUE)))

#define CONST_DOUBLE_OK_FOR_LETTER_P(VALUE, C) \
  ia64_const_double_ok_for_letter_p (VALUE, C)

/* A C expression that defines the optional machine-dependent constraint
   letters (`Q', `R', `S', `T', `U') that can be used to segregate specific
   types of operands, usually memory references, for the target machine.  */

#define EXTRA_CONSTRAINT(VALUE, C) \
  ia64_extra_constraint (VALUE, C)

/* Document the constraints that can accept reloaded memory operands.  This is
   needed by the extended asm support, and by reload.  'Q' accepts mem, but
   only non-volatile mem.  Since we can't reload a volatile mem into a
   non-volatile mem, it can not be listed here.  */

#define EXTRA_MEMORY_CONSTRAINT(C, STR)  ((C) == 'S')

/* Basic Stack Layout */

/* Define this macro if pushing a word onto the stack moves the stack pointer
   to a smaller address.  */
#define STACK_GROWS_DOWNWARD 1

/* Define this macro to nonzero if the addresses of local variable slots
   are at negative offsets from the frame pointer.  */
#define FRAME_GROWS_DOWNWARD 0

/* Offset from the frame pointer to the first local variable slot to
   be allocated.  */
#define STARTING_FRAME_OFFSET 0

/* Offset from the stack pointer register to the first location at which
   outgoing arguments are placed.  If not specified, the default value of zero
   is used.  This is the proper value for most machines.  */
/* IA64 has a 16 byte scratch area that is at the bottom of the stack.  */
#define STACK_POINTER_OFFSET 16

/* Offset from the argument pointer register to the first argument's address.
   On some machines it may depend on the data type of the function.  */
#define FIRST_PARM_OFFSET(FUNDECL) 0

/* A C expression whose value is RTL representing the value of the return
   address for the frame COUNT steps up from the current frame, after the
   prologue.  */

/* ??? Frames other than zero would likely require interpreting the frame
   unwind info, so we don't try to support them.  We would also need to define
   DYNAMIC_CHAIN_ADDRESS and SETUP_FRAME_ADDRESS (for the reg stack flush).  */

#define RETURN_ADDR_RTX(COUNT, FRAME) \
  ia64_return_addr_rtx (COUNT, FRAME)

/* A C expression whose value is RTL representing the location of the incoming
   return address at the beginning of any function, before the prologue.  This
   RTL is either a `REG', indicating that the return value is saved in `REG',
   or a `MEM' representing a location in the stack.  This enables DWARF2
   unwind info for C++ EH.  */
#define INCOMING_RETURN_ADDR_RTX gen_rtx_REG (VOIDmode, BR_REG (0))

/* ??? This is not defined because of three problems.
   1) dwarf2out.c assumes that DWARF_FRAME_RETURN_COLUMN fits in one byte.
   The default value is FIRST_PSEUDO_REGISTER which doesn't.  This can be
   worked around by setting PC_REGNUM to FR_REG (0) which is an otherwise
   unused register number.
   2) dwarf2out_frame_debug core dumps while processing prologue insns.  We
   need to refine which insns have RTX_FRAME_RELATED_P set and which don't.
   3) It isn't possible to turn off EH frame info by defining DWARF2_UNIND_INFO
   to zero, despite what the documentation implies, because it is tested in
   a few places with #ifdef instead of #if.  */
#undef INCOMING_RETURN_ADDR_RTX

/* A C expression whose value is an integer giving the offset, in bytes, from
   the value of the stack pointer register to the top of the stack frame at the
   beginning of any function, before the prologue.  The top of the frame is
   defined to be the value of the stack pointer in the previous frame, just
   before the call instruction.  */
/* The CFA is past the red zone, not at the entry-point stack
   pointer.  */
#define INCOMING_FRAME_SP_OFFSET STACK_POINTER_OFFSET

/* We shorten debug info by using CFA-16 as DW_AT_frame_base.  */
#define CFA_FRAME_BASE_OFFSET(FUNDECL) (-INCOMING_FRAME_SP_OFFSET)


/* Register That Address the Stack Frame.  */

/* The register number of the stack pointer register, which must also be a
   fixed register according to `FIXED_REGISTERS'.  On most machines, the
   hardware determines which register this is.  */

#define STACK_POINTER_REGNUM 12

/* The register number of the frame pointer register, which is used to access
   automatic variables in the stack frame.  On some machines, the hardware
   determines which register this is.  On other machines, you can choose any
   register you wish for this purpose.  */

#define FRAME_POINTER_REGNUM 328

/* Base register for access to local variables of the function.  */
#define HARD_FRAME_POINTER_REGNUM  LOC_REG (79)

/* The register number of the arg pointer register, which is used to access the
   function's argument list.  */
/* r0 won't otherwise be used, so put the always eliminated argument pointer
   in it.  */
#define ARG_POINTER_REGNUM R_GR(0)

/* Due to the way varargs and argument spilling happens, the argument
   pointer is not 16-byte aligned like the stack pointer.  */
#define INIT_EXPANDERS					\
  do {							\
    if (cfun && cfun->emit->regno_pointer_align)	\
      REGNO_POINTER_ALIGN (ARG_POINTER_REGNUM) = 64;	\
  } while (0)

/* Register numbers used for passing a function's static chain pointer.  */
/* ??? The ABI sez the static chain should be passed as a normal parameter.  */
#define STATIC_CHAIN_REGNUM 15

/* Eliminating the Frame Pointer and the Arg Pointer */

/* A C expression which is nonzero if a function must have and use a frame
   pointer.  This expression is evaluated in the reload pass.  If its value is
   nonzero the function will have a frame pointer.  */
#define FRAME_POINTER_REQUIRED 0

/* Show we can debug even without a frame pointer.  */
#define CAN_DEBUG_WITHOUT_FP

/* If defined, this macro specifies a table of register pairs used to eliminate
   unneeded registers that point into the stack frame.  */

#define ELIMINABLE_REGS							\
{									\
  {ARG_POINTER_REGNUM,	 STACK_POINTER_REGNUM},				\
  {ARG_POINTER_REGNUM,	 HARD_FRAME_POINTER_REGNUM},			\
  {FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM},				\
  {FRAME_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM},			\
}

/* A C expression that returns nonzero if the compiler is allowed to try to
   replace register number FROM with register number TO.  The frame pointer
   is automatically handled.  */

#define CAN_ELIMINATE(FROM, TO) \
  (TO == BR_REG (0) ? current_function_is_leaf : 1)

/* This macro is similar to `INITIAL_FRAME_POINTER_OFFSET'.  It
   specifies the initial difference between the specified pair of
   registers.  This macro must be defined if `ELIMINABLE_REGS' is
   defined.  */
#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET) \
  ((OFFSET) = ia64_initial_elimination_offset ((FROM), (TO)))

/* Passing Function Arguments on the Stack */

/* If defined, the maximum amount of space required for outgoing arguments will
   be computed and placed into the variable
   `current_function_outgoing_args_size'.  */

#define ACCUMULATE_OUTGOING_ARGS 1

/* A C expression that should indicate the number of bytes of its own arguments
   that a function pops on returning, or 0 if the function pops no arguments
   and the caller must therefore pop them all after the function returns.  */

#define RETURN_POPS_ARGS(FUNDECL, FUNTYPE, STACK_SIZE) 0


/* Function Arguments in Registers */

#define MAX_ARGUMENT_SLOTS 8
#define MAX_INT_RETURN_SLOTS 4
#define GR_ARG_FIRST IN_REG (0)
#define GR_RET_FIRST GR_REG (8)
#define GR_RET_LAST  GR_REG (11)
#define FR_ARG_FIRST FR_REG (8)
#define FR_RET_FIRST FR_REG (8)
#define FR_RET_LAST  FR_REG (15)
#define AR_ARG_FIRST OUT_REG (0)

/* A C expression that controls whether a function argument is passed in a
   register, and which register.  */

#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED) \
  ia64_function_arg (&CUM, MODE, TYPE, NAMED, 0)

/* Define this macro if the target machine has "register windows", so that the
   register in which a function sees an arguments is not necessarily the same
   as the one in which the caller passed the argument.  */

#define FUNCTION_INCOMING_ARG(CUM, MODE, TYPE, NAMED) \
  ia64_function_arg (&CUM, MODE, TYPE, NAMED, 1)

/* A C type for declaring a variable that is used as the first argument of
   `FUNCTION_ARG' and other related values.  For some target machines, the type
   `int' suffices and can hold the number of bytes of argument so far.  */

typedef struct ia64_args
{
  int words;			/* # words of arguments so far  */
  int int_regs;			/* # GR registers used so far  */
  int fp_regs;			/* # FR registers used so far  */
  int prototype;		/* whether function prototyped  */
} CUMULATIVE_ARGS;

/* A C statement (sans semicolon) for initializing the variable CUM for the
   state at the beginning of the argument list.  */

#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, INDIRECT, N_NAMED_ARGS) \
do {									\
  (CUM).words = 0;							\
  (CUM).int_regs = 0;							\
  (CUM).fp_regs = 0;							\
  (CUM).prototype = ((FNTYPE) && TYPE_ARG_TYPES (FNTYPE)) || (LIBNAME);	\
} while (0)

/* Like `INIT_CUMULATIVE_ARGS' but overrides it for the purposes of finding the
   arguments for the function being compiled.  If this macro is undefined,
   `INIT_CUMULATIVE_ARGS' is used instead.  */

/* We set prototype to true so that we never try to return a PARALLEL from
   function_arg.  */
#define INIT_CUMULATIVE_INCOMING_ARGS(CUM, FNTYPE, LIBNAME) \
do {									\
  (CUM).words = 0;							\
  (CUM).int_regs = 0;							\
  (CUM).fp_regs = 0;							\
  (CUM).prototype = 1;							\
} while (0)

/* A C statement (sans semicolon) to update the summarizer variable CUM to
   advance past an argument in the argument list.  The values MODE, TYPE and
   NAMED describe that argument.  Once this is done, the variable CUM is
   suitable for analyzing the *following* argument with `FUNCTION_ARG'.  */

#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED) \
 ia64_function_arg_advance (&CUM, MODE, TYPE, NAMED)

/* If defined, a C expression that gives the alignment boundary, in bits, of an
   argument with the specified mode and type.  */

/* Return the alignment boundary in bits for an argument with a specified
   mode and type.  */

#define FUNCTION_ARG_BOUNDARY(MODE, TYPE) \
  ia64_function_arg_boundary (MODE, TYPE)

/* A C expression that is nonzero if REGNO is the number of a hard register in
   which function arguments are sometimes passed.  This does *not* include
   implicit arguments such as the static chain and the structure-value address.
   On many machines, no registers can be used for this purpose since all
   function arguments are pushed on the stack.  */
#define FUNCTION_ARG_REGNO_P(REGNO) \
(((REGNO) >= AR_ARG_FIRST && (REGNO) < (AR_ARG_FIRST + MAX_ARGUMENT_SLOTS)) \
 || ((REGNO) >= FR_ARG_FIRST && (REGNO) < (FR_ARG_FIRST + MAX_ARGUMENT_SLOTS)))

/* How Scalar Function Values are Returned */

/* A C expression to create an RTX representing the place where a function
   returns a value of data type VALTYPE.  */

#define FUNCTION_VALUE(VALTYPE, FUNC) \
  ia64_function_value (VALTYPE, FUNC)

/* A C expression to create an RTX representing the place where a library
   function returns a value of mode MODE.  */

#define LIBCALL_VALUE(MODE) \
  gen_rtx_REG (MODE,							\
	       (((GET_MODE_CLASS (MODE) == MODE_FLOAT			\
		 || GET_MODE_CLASS (MODE) == MODE_COMPLEX_FLOAT) &&	\
		      (MODE) != TFmode)	\
		? FR_RET_FIRST : GR_RET_FIRST))

/* A C expression that is nonzero if REGNO is the number of a hard register in
   which the values of called function may come back.  */

#define FUNCTION_VALUE_REGNO_P(REGNO)				\
  (((REGNO) >= GR_RET_FIRST && (REGNO) <= GR_RET_LAST)		\
   || ((REGNO) >= FR_RET_FIRST && (REGNO) <= FR_RET_LAST))


/* How Large Values are Returned */

#define DEFAULT_PCC_STRUCT_RETURN 0


/* Caller-Saves Register Allocation */

/* A C expression to determine whether it is worthwhile to consider placing a
   pseudo-register in a call-clobbered hard register and saving and restoring
   it around each function call.  The expression should be 1 when this is worth
   doing, and 0 otherwise.

   If you don't define this macro, a default is used which is good on most
   machines: `4 * CALLS < REFS'.  */
/* ??? Investigate.  */
/* #define CALLER_SAVE_PROFITABLE(REFS, CALLS) */


/* Function Entry and Exit */

/* Define this macro as a C expression that is nonzero if the return
   instruction or the function epilogue ignores the value of the stack pointer;
   in other words, if it is safe to delete an instruction to adjust the stack
   pointer before a return from the function.  */

#define EXIT_IGNORE_STACK 1

/* Define this macro as a C expression that is nonzero for registers
   used by the epilogue or the `return' pattern.  */

#define EPILOGUE_USES(REGNO) ia64_epilogue_uses (REGNO)

/* Nonzero for registers used by the exception handling mechanism.  */

#define EH_USES(REGNO) ia64_eh_uses (REGNO)

/* Output part N of a function descriptor for DECL.  For ia64, both
   words are emitted with a single relocation, so ignore N > 0.  */
#define ASM_OUTPUT_FDESC(FILE, DECL, PART)				\
do {									\
  if ((PART) == 0)							\
    {									\
      if (TARGET_ILP32)							\
        fputs ("\tdata8.ua @iplt(", FILE);				\
      else								\
        fputs ("\tdata16.ua @iplt(", FILE);				\
      mark_decl_referenced (DECL);					\
      assemble_name (FILE, XSTR (XEXP (DECL_RTL (DECL), 0), 0));	\
      fputs (")\n", FILE);						\
      if (TARGET_ILP32)							\
	fputs ("\tdata8.ua 0\n", FILE);					\
    }									\
} while (0)

/* Generating Code for Profiling.  */

/* A C statement or compound statement to output to FILE some assembler code to
   call the profiling subroutine `mcount'.  */

#undef FUNCTION_PROFILER
#define FUNCTION_PROFILER(FILE, LABELNO) \
  ia64_output_function_profiler(FILE, LABELNO)

/* Neither hpux nor linux use profile counters.  */
#define NO_PROFILE_COUNTERS 1

/* Trampolines for Nested Functions.  */

/* We need 32 bytes, so we can save the sp, ar.rnat, ar.bsp, and ar.pfs of
   the function containing a non-local goto target.  */

#define STACK_SAVEAREA_MODE(LEVEL) \
  ((LEVEL) == SAVE_NONLOCAL ? OImode : Pmode)

/* Output assembler code for a block containing the constant parts of
   a trampoline, leaving space for the variable parts.

   The trampoline should set the static chain pointer to value placed
   into the trampoline and should branch to the specified routine.
   To make the normal indirect-subroutine calling convention work,
   the trampoline must look like a function descriptor; the first
   word being the target address and the second being the target's
   global pointer.

   We abuse the concept of a global pointer by arranging for it
   to point to the data we need to load.  The complete trampoline
   has the following form:

		+-------------------+ \
	TRAMP:	| __ia64_trampoline | |
		+-------------------+  > fake function descriptor
		| TRAMP+16          | |
		+-------------------+ /
		| target descriptor |
		+-------------------+
		| static link	    |
		+-------------------+
*/

/* A C expression for the size in bytes of the trampoline, as an integer.  */

#define TRAMPOLINE_SIZE		32

/* Alignment required for trampolines, in bits.  */

#define TRAMPOLINE_ALIGNMENT	64

/* A C statement to initialize the variable parts of a trampoline.  */

#define INITIALIZE_TRAMPOLINE(ADDR, FNADDR, STATIC_CHAIN) \
  ia64_initialize_trampoline((ADDR), (FNADDR), (STATIC_CHAIN))

/* Addressing Modes */

/* Define this macro if the machine supports post-increment addressing.  */

#define HAVE_POST_INCREMENT 1
#define HAVE_POST_DECREMENT 1
#define HAVE_POST_MODIFY_DISP 1
#define HAVE_POST_MODIFY_REG 1

/* A C expression that is 1 if the RTX X is a constant which is a valid
   address.  */

#define CONSTANT_ADDRESS_P(X) 0

/* The max number of registers that can appear in a valid memory address.  */

#define MAX_REGS_PER_ADDRESS 2

/* A C compound statement with a conditional `goto LABEL;' executed if X (an
   RTX) is a legitimate memory address on the target machine for a memory
   operand of mode MODE.  */

#define LEGITIMATE_ADDRESS_REG(X)					\
  ((GET_CODE (X) == REG && REG_OK_FOR_BASE_P (X))			\
   || (GET_CODE (X) == SUBREG && GET_CODE (XEXP (X, 0)) == REG		\
       && REG_OK_FOR_BASE_P (XEXP (X, 0))))

#define LEGITIMATE_ADDRESS_DISP(R, X)					\
  (GET_CODE (X) == PLUS							\
   && rtx_equal_p (R, XEXP (X, 0))					\
   && (LEGITIMATE_ADDRESS_REG (XEXP (X, 1))				\
       || (GET_CODE (XEXP (X, 1)) == CONST_INT				\
	   && INTVAL (XEXP (X, 1)) >= -256				\
	   && INTVAL (XEXP (X, 1)) < 256)))

#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, LABEL) 			\
do {									\
  if (LEGITIMATE_ADDRESS_REG (X))					\
    goto LABEL;								\
  else if ((GET_CODE (X) == POST_INC || GET_CODE (X) == POST_DEC)	\
	   && LEGITIMATE_ADDRESS_REG (XEXP (X, 0))			\
	   && XEXP (X, 0) != arg_pointer_rtx)				\
    goto LABEL;								\
  else if (GET_CODE (X) == POST_MODIFY					\
	   && LEGITIMATE_ADDRESS_REG (XEXP (X, 0))			\
	   && XEXP (X, 0) != arg_pointer_rtx				\
	   && LEGITIMATE_ADDRESS_DISP (XEXP (X, 0), XEXP (X, 1)))	\
    goto LABEL;								\
} while (0)

/* A C expression that is nonzero if X (assumed to be a `reg' RTX) is valid for
   use as a base register.  */

#ifdef REG_OK_STRICT
#define REG_OK_FOR_BASE_P(X) REGNO_OK_FOR_BASE_P (REGNO (X))
#else
#define REG_OK_FOR_BASE_P(X) \
  (GENERAL_REGNO_P (REGNO (X)) || (REGNO (X) >= FIRST_PSEUDO_REGISTER))
#endif

/* A C expression that is nonzero if X (assumed to be a `reg' RTX) is valid for
   use as an index register.  This is needed for POST_MODIFY.  */

#define REG_OK_FOR_INDEX_P(X) REG_OK_FOR_BASE_P (X)

/* A C statement or compound statement with a conditional `goto LABEL;'
   executed if memory address X (an RTX) can have different meanings depending
   on the machine mode of the memory reference it is used for or if the address
   is valid for some modes but not others.  */

#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR, LABEL)			\
  if (GET_CODE (ADDR) == POST_DEC || GET_CODE (ADDR) == POST_INC)	\
    goto LABEL;

/* A C expression that is nonzero if X is a legitimate constant for an
   immediate operand on the target machine.  */

#define LEGITIMATE_CONSTANT_P(X) ia64_legitimate_constant_p (X)

/* Condition Code Status */

/* One some machines not all possible comparisons are defined, but you can
   convert an invalid comparison into a valid one.  */
/* ??? Investigate.  See the alpha definition.  */
/* #define CANONICALIZE_COMPARISON(CODE, OP0, OP1) */


/* Describing Relative Costs of Operations */

/* A C expression for the cost of moving data from a register in class FROM to
   one in class TO, using MODE.  */

#define REGISTER_MOVE_COST  ia64_register_move_cost

/* A C expression for the cost of moving data of mode M between a
   register and memory.  */
#define MEMORY_MOVE_COST(MODE,CLASS,IN) \
  ((CLASS) == GENERAL_REGS || (CLASS) == FR_REGS || (CLASS) == FP_REGS \
   || (CLASS) == GR_AND_FR_REGS ? 4 : 10)

/* A C expression for the cost of a branch instruction.  A value of 1 is the
   default; other values are interpreted relative to that.  Used by the
   if-conversion code as max instruction count.  */
/* ??? This requires investigation.  The primary effect might be how
   many additional insn groups we run into, vs how good the dynamic
   branch predictor is.  */

#define BRANCH_COST 6

/* Define this macro as a C expression which is nonzero if accessing less than
   a word of memory (i.e. a `char' or a `short') is no faster than accessing a
   word of memory.  */

#define SLOW_BYTE_ACCESS 1

/* Define this macro if it is as good or better to call a constant function
   address than to call an address kept in a register.

   Indirect function calls are more expensive that direct function calls, so
   don't cse function addresses.  */

#define NO_FUNCTION_CSE


/* Dividing the output into sections.  */

/* A C expression whose value is a string containing the assembler operation
   that should precede instructions and read-only data.  */

#define TEXT_SECTION_ASM_OP "\t.text"

/* A C expression whose value is a string containing the assembler operation to
   identify the following data as writable initialized data.  */

#define DATA_SECTION_ASM_OP "\t.data"

/* If defined, a C expression whose value is a string containing the assembler
   operation to identify the following data as uninitialized global data.  */

#define BSS_SECTION_ASM_OP "\t.bss"

#define IA64_DEFAULT_GVALUE 8

/* Position Independent Code.  */

/* The register number of the register used to address a table of static data
   addresses in memory.  */

/* ??? Should modify ia64.md to use pic_offset_table_rtx instead of
   gen_rtx_REG (DImode, 1).  */

/* ??? Should we set flag_pic?  Probably need to define
   LEGITIMIZE_PIC_OPERAND_P to make that work.  */

#define PIC_OFFSET_TABLE_REGNUM GR_REG (1)

/* Define this macro if the register defined by `PIC_OFFSET_TABLE_REGNUM' is
   clobbered by calls.  */

#define PIC_OFFSET_TABLE_REG_CALL_CLOBBERED


/* The Overall Framework of an Assembler File.  */

/* A C string constant describing how to begin a comment in the target
   assembler language.  The compiler assumes that the comment will end at the
   end of the line.  */

#define ASM_COMMENT_START "//"

/* A C string constant for text to be output before each `asm' statement or
   group of consecutive ones.  */

#define ASM_APP_ON (TARGET_GNU_AS ? "#APP\n" : "//APP\n")

/* A C string constant for text to be output after each `asm' statement or
   group of consecutive ones.  */

#define ASM_APP_OFF (TARGET_GNU_AS ? "#NO_APP\n" : "//NO_APP\n")

/* Output of Uninitialized Variables.  */

/* This is all handled by svr4.h.  */


/* Output and Generation of Labels.  */

/* A C statement (sans semicolon) to output to the stdio stream STREAM the
   assembler definition of a label named NAME.  */

/* See the ASM_OUTPUT_LABELREF definition in sysv4.h for an explanation of
   why ia64_asm_output_label exists.  */

extern int ia64_asm_output_label;
#define ASM_OUTPUT_LABEL(STREAM, NAME)					\
do {									\
  ia64_asm_output_label = 1;						\
  assemble_name (STREAM, NAME);						\
  fputs (":\n", STREAM);						\
  ia64_asm_output_label = 0;						\
} while (0)

/* Globalizing directive for a label.  */
#define GLOBAL_ASM_OP "\t.global "

/* A C statement (sans semicolon) to output to the stdio stream STREAM any text
   necessary for declaring the name of an external symbol named NAME which is
   referenced in this compilation but not defined.  */

#define ASM_OUTPUT_EXTERNAL(FILE, DECL, NAME) \
  ia64_asm_output_external (FILE, DECL, NAME)

/* A C statement to store into the string STRING a label whose name is made
   from the string PREFIX and the number NUM.  */

#define ASM_GENERATE_INTERNAL_LABEL(LABEL, PREFIX, NUM) \
do {									\
  sprintf (LABEL, "*.%s%d", PREFIX, NUM);				\
} while (0)

/* ??? Not sure if using a ? in the name for Intel as is safe.  */

#define ASM_PN_FORMAT (TARGET_GNU_AS ? "%s.%lu" : "%s?%lu")

/* A C statement to output to the stdio stream STREAM assembler code which
   defines (equates) the symbol NAME to have the value VALUE.  */

#define ASM_OUTPUT_DEF(STREAM, NAME, VALUE) \
do {									\
  assemble_name (STREAM, NAME);						\
  fputs (" = ", STREAM);						\
  assemble_name (STREAM, VALUE);					\
  fputc ('\n', STREAM);							\
} while (0)


/* Macros Controlling Initialization Routines.  */

/* This is handled by svr4.h and sysv4.h.  */


/* Output of Assembler Instructions.  */

/* A C initializer containing the assembler's names for the machine registers,
   each one as a C string constant.  */

#define REGISTER_NAMES \
{									\
  /* General registers.  */						\
  "ap", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9",		\
  "r10", "r11", "r12", "r13", "r14", "r15", "r16", "r17", "r18", "r19",	\
  "r20", "r21", "r22", "r23", "r24", "r25", "r26", "r27", "r28", "r29",	\
  "r30", "r31",								\
  /* Local registers.  */						\
  "loc0", "loc1", "loc2", "loc3", "loc4", "loc5", "loc6", "loc7",	\
  "loc8", "loc9", "loc10","loc11","loc12","loc13","loc14","loc15",	\
  "loc16","loc17","loc18","loc19","loc20","loc21","loc22","loc23",	\
  "loc24","loc25","loc26","loc27","loc28","loc29","loc30","loc31",	\
  "loc32","loc33","loc34","loc35","loc36","loc37","loc38","loc39",	\
  "loc40","loc41","loc42","loc43","loc44","loc45","loc46","loc47",	\
  "loc48","loc49","loc50","loc51","loc52","loc53","loc54","loc55",	\
  "loc56","loc57","loc58","loc59","loc60","loc61","loc62","loc63",	\
  "loc64","loc65","loc66","loc67","loc68","loc69","loc70","loc71",	\
  "loc72","loc73","loc74","loc75","loc76","loc77","loc78","loc79",	\
  /* Input registers.  */						\
  "in0",  "in1",  "in2",  "in3",  "in4",  "in5",  "in6",  "in7",	\
  /* Output registers.  */						\
  "out0", "out1", "out2", "out3", "out4", "out5", "out6", "out7",	\
  /* Floating-point registers.  */					\
  "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9",		\
  "f10", "f11", "f12", "f13", "f14", "f15", "f16", "f17", "f18", "f19",	\
  "f20", "f21", "f22", "f23", "f24", "f25", "f26", "f27", "f28", "f29",	\
  "f30", "f31", "f32", "f33", "f34", "f35", "f36", "f37", "f38", "f39",	\
  "f40", "f41", "f42", "f43", "f44", "f45", "f46", "f47", "f48", "f49",	\
  "f50", "f51", "f52", "f53", "f54", "f55", "f56", "f57", "f58", "f59",	\
  "f60", "f61", "f62", "f63", "f64", "f65", "f66", "f67", "f68", "f69",	\
  "f70", "f71", "f72", "f73", "f74", "f75", "f76", "f77", "f78", "f79",	\
  "f80", "f81", "f82", "f83", "f84", "f85", "f86", "f87", "f88", "f89",	\
  "f90", "f91", "f92", "f93", "f94", "f95", "f96", "f97", "f98", "f99",	\
  "f100","f101","f102","f103","f104","f105","f106","f107","f108","f109",\
  "f110","f111","f112","f113","f114","f115","f116","f117","f118","f119",\
  "f120","f121","f122","f123","f124","f125","f126","f127",		\
  /* Predicate registers.  */						\
  "p0", "p1", "p2", "p3", "p4", "p5", "p6", "p7", "p8", "p9",		\
  "p10", "p11", "p12", "p13", "p14", "p15", "p16", "p17", "p18", "p19",	\
  "p20", "p21", "p22", "p23", "p24", "p25", "p26", "p27", "p28", "p29",	\
  "p30", "p31", "p32", "p33", "p34", "p35", "p36", "p37", "p38", "p39",	\
  "p40", "p41", "p42", "p43", "p44", "p45", "p46", "p47", "p48", "p49",	\
  "p50", "p51", "p52", "p53", "p54", "p55", "p56", "p57", "p58", "p59",	\
  "p60", "p61", "p62", "p63",						\
  /* Branch registers.  */						\
  "b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7",			\
  /* Frame pointer.  Application registers.  */				\
  "sfp", "ar.ccv", "ar.unat", "ar.pfs", "ar.lc", "ar.ec",	\
}

/* If defined, a C initializer for an array of structures containing a name and
   a register number.  This macro defines additional names for hard registers,
   thus allowing the `asm' option in declarations to refer to registers using
   alternate names.  */

#define ADDITIONAL_REGISTER_NAMES \
{									\
  { "gp", R_GR (1) },							\
  { "sp", R_GR (12) },							\
  { "in0", IN_REG (0) },						\
  { "in1", IN_REG (1) },						\
  { "in2", IN_REG (2) },						\
  { "in3", IN_REG (3) },						\
  { "in4", IN_REG (4) },						\
  { "in5", IN_REG (5) },						\
  { "in6", IN_REG (6) },						\
  { "in7", IN_REG (7) },						\
  { "out0", OUT_REG (0) },						\
  { "out1", OUT_REG (1) },						\
  { "out2", OUT_REG (2) },						\
  { "out3", OUT_REG (3) },						\
  { "out4", OUT_REG (4) },						\
  { "out5", OUT_REG (5) },						\
  { "out6", OUT_REG (6) },						\
  { "out7", OUT_REG (7) },						\
  { "loc0", LOC_REG (0) },						\
  { "loc1", LOC_REG (1) },						\
  { "loc2", LOC_REG (2) },						\
  { "loc3", LOC_REG (3) },						\
  { "loc4", LOC_REG (4) },						\
  { "loc5", LOC_REG (5) },						\
  { "loc6", LOC_REG (6) },						\
  { "loc7", LOC_REG (7) },						\
  { "loc8", LOC_REG (8) }, 						\
  { "loc9", LOC_REG (9) }, 						\
  { "loc10", LOC_REG (10) }, 						\
  { "loc11", LOC_REG (11) }, 						\
  { "loc12", LOC_REG (12) }, 						\
  { "loc13", LOC_REG (13) }, 						\
  { "loc14", LOC_REG (14) }, 						\
  { "loc15", LOC_REG (15) }, 						\
  { "loc16", LOC_REG (16) }, 						\
  { "loc17", LOC_REG (17) }, 						\
  { "loc18", LOC_REG (18) }, 						\
  { "loc19", LOC_REG (19) }, 						\
  { "loc20", LOC_REG (20) }, 						\
  { "loc21", LOC_REG (21) }, 						\
  { "loc22", LOC_REG (22) }, 						\
  { "loc23", LOC_REG (23) }, 						\
  { "loc24", LOC_REG (24) }, 						\
  { "loc25", LOC_REG (25) }, 						\
  { "loc26", LOC_REG (26) }, 						\
  { "loc27", LOC_REG (27) }, 						\
  { "loc28", LOC_REG (28) }, 						\
  { "loc29", LOC_REG (29) }, 						\
  { "loc30", LOC_REG (30) }, 						\
  { "loc31", LOC_REG (31) }, 						\
  { "loc32", LOC_REG (32) }, 						\
  { "loc33", LOC_REG (33) }, 						\
  { "loc34", LOC_REG (34) }, 						\
  { "loc35", LOC_REG (35) }, 						\
  { "loc36", LOC_REG (36) }, 						\
  { "loc37", LOC_REG (37) }, 						\
  { "loc38", LOC_REG (38) }, 						\
  { "loc39", LOC_REG (39) }, 						\
  { "loc40", LOC_REG (40) }, 						\
  { "loc41", LOC_REG (41) }, 						\
  { "loc42", LOC_REG (42) }, 						\
  { "loc43", LOC_REG (43) }, 						\
  { "loc44", LOC_REG (44) }, 						\
  { "loc45", LOC_REG (45) }, 						\
  { "loc46", LOC_REG (46) }, 						\
  { "loc47", LOC_REG (47) }, 						\
  { "loc48", LOC_REG (48) }, 						\
  { "loc49", LOC_REG (49) }, 						\
  { "loc50", LOC_REG (50) }, 						\
  { "loc51", LOC_REG (51) }, 						\
  { "loc52", LOC_REG (52) }, 						\
  { "loc53", LOC_REG (53) }, 						\
  { "loc54", LOC_REG (54) }, 						\
  { "loc55", LOC_REG (55) }, 						\
  { "loc56", LOC_REG (56) }, 						\
  { "loc57", LOC_REG (57) }, 						\
  { "loc58", LOC_REG (58) }, 						\
  { "loc59", LOC_REG (59) }, 						\
  { "loc60", LOC_REG (60) }, 						\
  { "loc61", LOC_REG (61) }, 						\
  { "loc62", LOC_REG (62) }, 						\
  { "loc63", LOC_REG (63) }, 						\
  { "loc64", LOC_REG (64) }, 						\
  { "loc65", LOC_REG (65) }, 						\
  { "loc66", LOC_REG (66) }, 						\
  { "loc67", LOC_REG (67) }, 						\
  { "loc68", LOC_REG (68) }, 						\
  { "loc69", LOC_REG (69) }, 						\
  { "loc70", LOC_REG (70) }, 						\
  { "loc71", LOC_REG (71) }, 						\
  { "loc72", LOC_REG (72) }, 						\
  { "loc73", LOC_REG (73) }, 						\
  { "loc74", LOC_REG (74) }, 						\
  { "loc75", LOC_REG (75) }, 						\
  { "loc76", LOC_REG (76) }, 						\
  { "loc77", LOC_REG (77) }, 						\
  { "loc78", LOC_REG (78) }, 						\
  { "loc79", LOC_REG (79) }, 						\
}

/* A C compound statement to output to stdio stream STREAM the assembler syntax
   for an instruction operand X.  X is an RTL expression.  */

#define PRINT_OPERAND(STREAM, X, CODE) \
  ia64_print_operand (STREAM, X, CODE)

/* A C expression which evaluates to true if CODE is a valid punctuation
   character for use in the `PRINT_OPERAND' macro.  */

/* ??? Keep this around for now, as we might need it later.  */

#define PRINT_OPERAND_PUNCT_VALID_P(CODE) \
  ((CODE) == '+' || (CODE) == ',')

/* A C compound statement to output to stdio stream STREAM the assembler syntax
   for an instruction operand that is a memory reference whose address is X.  X
   is an RTL expression.  */

#define PRINT_OPERAND_ADDRESS(STREAM, X) \
  ia64_print_operand_address (STREAM, X)

/* If defined, C string expressions to be used for the `%R', `%L', `%U', and
   `%I' options of `asm_fprintf' (see `final.c').  */

#define REGISTER_PREFIX ""
#define LOCAL_LABEL_PREFIX "."
#define USER_LABEL_PREFIX ""
#define IMMEDIATE_PREFIX ""


/* Output of dispatch tables.  */

/* This macro should be provided on machines where the addresses in a dispatch
   table are relative to the table's own address.  */

/* ??? Depends on the pointer size.  */

#define ASM_OUTPUT_ADDR_DIFF_ELT(STREAM, BODY, VALUE, REL)	\
  do {								\
  if (TARGET_ILP32)						\
    fprintf (STREAM, "\tdata4 @pcrel(.L%d)\n", VALUE);		\
  else								\
    fprintf (STREAM, "\tdata8 @pcrel(.L%d)\n", VALUE);		\
  } while (0)

/* Jump tables only need 8 byte alignment.  */

#define ADDR_VEC_ALIGN(ADDR_VEC) 3


/* Assembler Commands for Exception Regions.  */

/* Select a format to encode pointers in exception handling data.  CODE
   is 0 for data, 1 for code labels, 2 for function pointers.  GLOBAL is
   true if the symbol may be affected by dynamic relocations.  */
#define ASM_PREFERRED_EH_DATA_FORMAT(CODE,GLOBAL)	\
  (((CODE) == 1 ? DW_EH_PE_textrel : DW_EH_PE_datarel)	\
   | ((GLOBAL) ? DW_EH_PE_indirect : 0)			\
   | (TARGET_ILP32 ? DW_EH_PE_udata4 : DW_EH_PE_udata8))

/* Handle special EH pointer encodings.  Absolute, pc-relative, and
   indirect are handled automatically.  */
#define ASM_MAYBE_OUTPUT_ENCODED_ADDR_RTX(FILE, ENCODING, SIZE, ADDR, DONE) \
  do {									\
    const char *reltag = NULL;						\
    if (((ENCODING) & 0xF0) == DW_EH_PE_textrel)			\
      reltag = "@segrel(";						\
    else if (((ENCODING) & 0xF0) == DW_EH_PE_datarel)			\
      reltag = "@gprel(";						\
    if (reltag)								\
      {									\
	fputs (integer_asm_op (SIZE, FALSE), FILE);			\
	fputs (reltag, FILE);						\
	assemble_name (FILE, XSTR (ADDR, 0));				\
	fputc (')', FILE);						\
	goto DONE;							\
      }									\
  } while (0)


/* Assembler Commands for Alignment.  */

/* ??? Investigate.  */

/* The alignment (log base 2) to put in front of LABEL, which follows
   a BARRIER.  */

/* #define LABEL_ALIGN_AFTER_BARRIER(LABEL) */

/* The desired alignment for the location counter at the beginning
   of a loop.  */

/* #define LOOP_ALIGN(LABEL) */

/* Define this macro if `ASM_OUTPUT_SKIP' should not be used in the text
   section because it fails put zeros in the bytes that are skipped.  */

#define ASM_NO_SKIP_IN_TEXT 1

/* A C statement to output to the stdio stream STREAM an assembler command to
   advance the location counter to a multiple of 2 to the POWER bytes.  */

#define ASM_OUTPUT_ALIGN(STREAM, POWER) \
  fprintf (STREAM, "\t.align %d\n", 1<<(POWER))


/* Macros Affecting all Debug Formats.  */

/* This is handled in svr4.h and sysv4.h.  */


/* Specific Options for DBX Output.  */

/* This is handled by dbxelf.h which is included by svr4.h.  */


/* Open ended Hooks for DBX Output.  */

/* Likewise.  */


/* File names in DBX format.  */

/* Likewise.  */


/* Macros for SDB and Dwarf Output.  */

/* Define this macro if GCC should produce dwarf version 2 format debugging
   output in response to the `-g' option.  */

#define DWARF2_DEBUGGING_INFO 1

/* We do not want call-frame info to be output, since debuggers are
   supposed to use the target unwind info.  Leave this undefined it
   TARGET_UNWIND_INFO might ever be false.  */

#define DWARF2_FRAME_INFO 0

#define DWARF2_ASM_LINE_DEBUG_INFO (TARGET_DWARF2_ASM)

/* Use tags for debug info labels, so that they don't break instruction
   bundles.  This also avoids getting spurious DV warnings from the
   assembler.  This is similar to (*targetm.asm_out.internal_label), except that we
   add brackets around the label.  */

#define ASM_OUTPUT_DEBUG_LABEL(FILE, PREFIX, NUM) \
  fprintf (FILE, TARGET_GNU_AS ? "[.%s%d:]\n" : ".%s%d:\n", PREFIX, NUM)

/* Use section-relative relocations for debugging offsets.  Unlike other
   targets that fake this by putting the section VMA at 0, IA-64 has
   proper relocations for them.  */
#define ASM_OUTPUT_DWARF_OFFSET(FILE, SIZE, LABEL, SECTION)	\
  do {								\
    fputs (integer_asm_op (SIZE, FALSE), FILE);			\
    fputs ("@secrel(", FILE);					\
    assemble_name (FILE, LABEL);				\
    fputc (')', FILE);						\
  } while (0)

/* Emit a PC-relative relocation.  */
#define ASM_OUTPUT_DWARF_PCREL(FILE, SIZE, LABEL)	\
  do {							\
    fputs (integer_asm_op (SIZE, FALSE), FILE);		\
    fputs ("@pcrel(", FILE);				\
    assemble_name (FILE, LABEL);			\
    fputc (')', FILE);					\
  } while (0)

/* Register Renaming Parameters.  */

/* A C expression that is nonzero if hard register number REGNO2 can be
   considered for use as a rename register for REGNO1 */

#define HARD_REGNO_RENAME_OK(REGNO1,REGNO2) \
  ia64_hard_regno_rename_ok((REGNO1), (REGNO2))


/* Miscellaneous Parameters.  */

/* Flag to mark data that is in the small address area (addressable
   via "addl", that is, within a 2MByte offset of 0.  */
#define SYMBOL_FLAG_SMALL_ADDR		(SYMBOL_FLAG_MACH_DEP << 0)
#define SYMBOL_REF_SMALL_ADDR_P(X)	\
	((SYMBOL_REF_FLAGS (X) & SYMBOL_FLAG_SMALL_ADDR) != 0)

/* An alias for a machine mode name.  This is the machine mode that elements of
   a jump-table should have.  */

#define CASE_VECTOR_MODE ptr_mode

/* Define as C expression which evaluates to nonzero if the tablejump
   instruction expects the table to contain offsets from the address of the
   table.  */

#define CASE_VECTOR_PC_RELATIVE 1

/* Define this macro if operations between registers with integral mode smaller
   than a word are always performed on the entire register.  */

#define WORD_REGISTER_OPERATIONS

/* Define this macro to be a C expression indicating when insns that read
   memory in MODE, an integral mode narrower than a word, set the bits outside
   of MODE to be either the sign-extension or the zero-extension of the data
   read.  */

#define LOAD_EXTEND_OP(MODE) ZERO_EXTEND

/* The maximum number of bytes that a single instruction can move quickly from
   memory to memory.  */
#define MOVE_MAX 8

/* A C expression which is nonzero if on this machine it is safe to "convert"
   an integer of INPREC bits to one of OUTPREC bits (where OUTPREC is smaller
   than INPREC) by merely operating on it as if it had only OUTPREC bits.  */

#define TRULY_NOOP_TRUNCATION(OUTPREC, INPREC) 1

/* A C expression describing the value returned by a comparison operator with
   an integral mode and stored by a store-flag instruction (`sCOND') when the
   condition is true.  */

/* ??? Investigate using STORE_FLAG_VALUE of -1 instead of 1.  */

/* An alias for the machine mode for pointers.  */

/* ??? This would change if we had ILP32 support.  */

#define Pmode DImode

/* An alias for the machine mode used for memory references to functions being
   called, in `call' RTL expressions.  */

#define FUNCTION_MODE Pmode

/* Define this macro to handle System V style pragmas: #pragma pack and
   #pragma weak.  Note, #pragma weak will only be supported if SUPPORT_WEAK is
   defined.  */

/* If this architecture supports prefetch, define this to be the number of
   prefetch commands that can be executed in parallel.

   ??? This number is bogus and needs to be replaced before the value is
   actually used in optimizations.  */

#define SIMULTANEOUS_PREFETCHES 6

/* If this architecture supports prefetch, define this to be the size of
   the cache line that is prefetched.  */

#define PREFETCH_BLOCK 32

#define HANDLE_SYSV_PRAGMA 1

/* A C expression for the maximum number of instructions to execute via
   conditional execution instructions instead of a branch.  A value of
   BRANCH_COST+1 is the default if the machine does not use
   cc0, and 1 if it does use cc0.  */
/* ??? Investigate.  */
#define MAX_CONDITIONAL_EXECUTE 12

extern int ia64_final_schedule;

#define TARGET_UNWIND_INFO	1

#define TARGET_UNWIND_TABLES_DEFAULT true

#define EH_RETURN_DATA_REGNO(N) ((N) < 4 ? (N) + 15 : INVALID_REGNUM)

/* This function contains machine specific function data.  */
struct machine_function GTY(())
{
  /* The new stack pointer when unwinding from EH.  */
  rtx ia64_eh_epilogue_sp;

  /* The new bsp value when unwinding from EH.  */
  rtx ia64_eh_epilogue_bsp;

  /* The GP value save register.  */
  rtx ia64_gp_save;

  /* The number of varargs registers to save.  */
  int n_varargs;

  /* The number of the next unwind state to copy.  */
  int state_num;
};

#define DONT_USE_BUILTIN_SETJMP

/* Output any profiling code before the prologue.  */

#undef  PROFILE_BEFORE_PROLOGUE
#define PROFILE_BEFORE_PROLOGUE 1

/* Initialize library function table. */
#undef TARGET_INIT_LIBFUNCS
#define TARGET_INIT_LIBFUNCS ia64_init_libfuncs


/* Switch on code for querying unit reservations.  */
#define CPU_UNITS_QUERY 1

/* Define this to change the optimizations performed by default.  */
#define OPTIMIZATION_OPTIONS(LEVEL, SIZE) \
  ia64_optimization_options ((LEVEL), (SIZE))

/* End of ia64.h */
