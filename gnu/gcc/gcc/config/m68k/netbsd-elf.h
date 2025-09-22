/* Definitions of target machine for GNU compiler,
   for m68k (including m68010) NetBSD platforms using the
   ELF object format.
   Copyright (C) 2002, 2003, 2004, 2006 Free Software Foundation, Inc.
   Contributed by Wasabi Systems. Inc.

   This file is derived from <m68k/m68kv4.h>, <m68k/m68kelf.h>,
   and <m68k/linux.h>.

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

#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
      NETBSD_OS_CPP_BUILTINS_ELF();		\
      builtin_define ("__m68k__");		\
      builtin_define ("__SVR4_ABI__");		\
      builtin_define ("__motorola__");		\
    }						\
  while (0)

/* Default target comes from config.gcc */
#undef TARGET_DEFAULT
#define TARGET_DEFAULT TARGET_CPU_DEFAULT


/* Don't try using XFmode on the 68010.  */ 
#undef LONG_DOUBLE_TYPE_SIZE
#define LONG_DOUBLE_TYPE_SIZE			\
  ((TARGET_68020 || TARGET_68040 || TARGET_68040_ONLY || \
    TARGET_68060) ? 80 : 64)

#ifdef __mc68010__
#define LIBGCC2_LONG_DOUBLE_TYPE_SIZE 64
#else
#define LIBGCC2_LONG_DOUBLE_TYPE_SIZE 80
#endif

#define EXTRA_SPECS \
  { "cpp_cpu_default_spec", CPP_CPU_DEFAULT_SPEC }, \
  { "cpp_cpu_spec",         CPP_CPU_SPEC }, \
  { "cpp_fpu_spec",         CPP_FPU_SPEC }, \
  { "asm_default_spec",     ASM_DEFAULT_SPEC }, \
  { "netbsd_cpp_spec",      NETBSD_CPP_SPEC }, \
  { "netbsd_entry_point",   NETBSD_ENTRY_POINT },


#define CPP_CPU_SPEC \
  "%{m68010:-D__mc68010__} \
   %{m68020:-D__mc68020__} \
   %{m68030:-D__mc68030__} \
   %{m68040:-D__mc68040__} \
   %(cpp_cpu_default_spec)"


#undef TARGET_VERSION
#if TARGET_DEFAULT & MASK_68020
#define TARGET_VERSION fprintf (stderr, " (NetBSD/m68k ELF)");
#define CPP_CPU_DEFAULT_SPEC "%{!m680*:-D__mc68020__}"
#define ASM_DEFAULT_SPEC "%{!m680*:-m68020}"
#else
#define TARGET_VERSION fprintf (stderr, " (NetBSD/68010 ELF)");
#define CPP_CPU_DEFAULT_SPEC "%{!m680*:-D__mc68010__}"
#define ASM_DEFAULT_SPEC "%{!m680*:-m68010}"
#endif


#if TARGET_DEFAULT & MASK_68881
#define CPP_FPU_SPEC "%{!msoft-float:-D__HAVE_68881__ -D__HAVE_FPU__}"
#else
#define CPP_FPU_SPEC "%{m68881:-D__HAVE_68881__ -D__HAVE_FPU__}"
#endif


/* Provide a CPP_SPEC appropriate for NetBSD m68k targets.  Currently we
   deal with the GCC option '-posix', as well as an indication as to
   whether or not use of the FPU is allowed.  */

#undef CPP_SPEC
#define CPP_SPEC \
  "%(netbsd_cpp_spec) %(cpp_cpu_spec) %(cpp_fpu_spec)"


/* Provide an ASM_SPEC appropriate for NetBSD m68k ELF targets.  We pass
   on some CPU options, as well as PIC code generation options.  */

#undef ASM_SPEC
#define ASM_SPEC \
  "%(asm_default_spec) \
    %{m68010} %{m68020} %{m68030} %{m68040} %{m68060} \
    %{fpic|fpie:-k} %{fPIC|fPIE:-k -K}"

#define AS_NEEDS_DASH_FOR_PIPED_INPUT

/* Provide a LINK_SPEC appropriate for a NetBSD/m68k ELF target.  */

#undef LINK_SPEC
#define LINK_SPEC NETBSD_LINK_SPEC_ELF

#define NETBSD_ENTRY_POINT "_start"

/* Output assembler code to FILE to increment profiler label # LABELNO
   for profiling a function only.  */

#undef FUNCTION_PROFILER
#define FUNCTION_PROFILER(FILE, LABELNO)				\
do									\
  {									\
    asm_fprintf (FILE, "\tlea (%LLP%d,%Rpc),%Ra1\n", (LABELNO));	\
    if (flag_pic)							\
      fprintf (FILE, "\tbsr.l __mcount@PLTPC\n");			\
    else								\
      fprintf (FILE, "\tjbsr __mcount\n");				\
  }									\
while (0)


/* Make gcc agree with <machine/ansi.h>  */

#undef SIZE_TYPE
#define SIZE_TYPE "unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "int"


/* XXX
   Here is a bunch of stuff lifted from m68kelf.h.  We don't use that
   file directly, because it has a lot of baggage we don't want.  */


/* The prefix for register names.  Note that REGISTER_NAMES
   is supposed to include this prefix.  Also note that this is NOT an
   fprintf format string, it is a literal string.  */

#undef REGISTER_PREFIX
#define REGISTER_PREFIX "%"


/* The prefix for local (compiler generated) lables.
   These labels will not appear in the symbol table.  */

#undef LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX "."


/* The prefix to add to user-visible assembler symbols.  */

#undef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX ""


/* The prefix for immediate operands.  */

#undef IMMEDIATE_PREFIX
#define IMMEDIATE_PREFIX "#"


#undef ASM_COMMENT_START
#define ASM_COMMENT_START "|"


/* Currently, JUMP_TABLES_IN_TEXT_SECTION must be defined in order to
   keep switch tables in the text section.  */

#undef JUMP_TABLES_IN_TEXT_SECTION
#define JUMP_TABLES_IN_TEXT_SECTION 1


/* Use the default action for outputting the case label.  */
#undef ASM_OUTPUT_CASE_LABEL
#define ASM_RETURN_CASE_JUMP				\
  do {							\
    if (TARGET_COLDFIRE)				\
      {							\
	if (ADDRESS_REG_P (operands[0]))		\
	  return "jmp %%pc@(2,%0:l)";			\
	else						\
	  return "ext%.l %0\n\tjmp %%pc@(2,%0:l)";	\
      }							\
    else						\
      return "jmp %%pc@(2,%0:w)";			\
  } while (0)


/* This is how to output an assembler line that says to advance the
   location counter to a multiple of 2**LOG bytes.  */

#undef ASM_OUTPUT_ALIGN
#define ASM_OUTPUT_ALIGN(FILE,LOG)					\
do									\
  {									\
    if ((LOG) > 0)							\
      fprintf ((FILE), "%s%u\n", ALIGN_ASM_OP, 1 << (LOG));		\
  }									\
while (0)


/* If defined, a C expression whose value is a string containing the
   assembler operation to identify the following data as uninitialized global
   data.  */

#define BSS_SECTION_ASM_OP	".section\t.bss"


/* Like `ASM_OUTPUT_BSS' except takes the required alignment as a
   separate, explicit argument.  If you define this macro, it is used
   in place of `ASM_OUTPUT_BSS', and gives you more flexibility in
   handling the required alignment of the variable.  The alignment is
   specified as the number of bits.

   Try to use function `asm_output_aligned_bss' defined in file
   `varasm.c' when defining this macro.  */

#undef ASM_OUTPUT_ALIGNED_BSS
#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN)		\
  asm_output_aligned_bss (FILE, DECL, NAME, SIZE, ALIGN)


#undef ASM_OUTPUT_COMMON
#define ASM_OUTPUT_COMMON(FILE, NAME, SIZE, ROUNDED)			\
( fputs (".comm ", (FILE)),						\
  assemble_name ((FILE), (NAME)),					\
  fprintf ((FILE), ",%u\n", (int)(SIZE)))

#undef ASM_OUTPUT_LOCAL
#define ASM_OUTPUT_LOCAL(FILE, NAME, SIZE, ROUNDED)			\
( fputs (".lcomm ", (FILE)),						\
  assemble_name ((FILE), (NAME)),					\
  fprintf ((FILE), ",%u\n", (int)(SIZE)))


/* XXX
   This is the end of the chunk lifted from m68kelf.h  */


/* XXX
   The following chunk is more or less lifted from m68kv4.h.
   We'd like to just #include that file, but it has not yet
   been converted to the new include style.

   Should there be a m68kv4-abi.h ??  */


/* Register in which address to store a structure value is passed to a
   function.  The default in m68k.h is a1.  For m68k/SVR4 it is a0. */

#undef M68K_STRUCT_VALUE_REGNUM
#define M68K_STRUCT_VALUE_REGNUM 8


/* Register in which static-chain is passed to a function.  The
   default isn m68k.h is a0, but that is already the struct value
   regnum.  Make it a1 instead.  */

#undef STATIC_CHAIN_REGNUM
#define STATIC_CHAIN_REGNUM 9


/* Now to renumber registers for dbx and gdb.
   We use the Sun-3 convention, which is:
   floating point registers have numbers 18 to 25, not
   16 to 23 as they do in the compiler.  */

#undef DBX_REGISTER_NUMBER
#define DBX_REGISTER_NUMBER(REGNO) ((REGNO) < 16 ? (REGNO) : (REGNO) + 2)


/* 1 if N is a possible register number for a function value.  For
   m68k/SVR4 allow d0, a0, or fp0 as return registers, for integral,
   pointer, or floating types, respectively.  Reject fp0 if not using
   a 68881 coprocessor.  */

#undef FUNCTION_VALUE_REGNO_P
#define FUNCTION_VALUE_REGNO_P(N)					\
  ((N) == 0 || (N) == 8 || (TARGET_68881 && (N) == 16))


/* Define this to be true when FUNCTION_VALUE_REGNO_P is true for
   more than one register.  */

#undef NEEDS_UNTYPED_CALL
#define NEEDS_UNTYPED_CALL 1


/* Define how to generate (in the callee) the output value of a
   function and how to find (in the caller) the value returned by a
   function.  VALTYPE is the data type of the value (as a tree).  If
   the precise function being called is known, FUNC is its
   FUNCTION_DECL; otherwise, FUNC is 0.  For m68k/SVR4 generate the
   result in d0, a0, or fp0 as appropriate.  */

#undef FUNCTION_VALUE
#define FUNCTION_VALUE(VALTYPE, FUNC)					\
  m68k_function_value (VALTYPE, FUNC)


/* For compatibility with the large body of existing code which does
   not always properly declare external functions returning pointer
   types, the m68k/SVR4 convention is to copy the value returned for
   pointer functions from a0 to d0 in the function epilogue, so that
   callers that have neglected to properly declare the callee can
   still find the correct return value.  */

extern int current_function_returns_pointer;
#define FUNCTION_EXTRA_EPILOGUE(FILE, SIZE) 				\
do									\
  {									\
    if (current_function_returns_pointer				\
	&& ! find_equiv_reg (0, get_last_insn (), 0, 0, 0, 8, Pmode))	\
      asm_fprintf (FILE, "\tmove.l %Ra0,%Rd0\n");			\
  }									\
while (0)


/* Define how to find the value returned by a library function
   assuming the value has mode MODE.
   For m68k/SVR4 look for integer values in d0, pointer values in d0
   (returned in both d0 and a0), and floating values in fp0.  */

#undef LIBCALL_VALUE
#define LIBCALL_VALUE(MODE)						\
  m68k_libcall_value (MODE)


/* Boundary (in *bits*) on which stack pointer should be aligned.
   The m68k/SVR4 convention is to keep the stack pointer longword aligned.  */

#undef STACK_BOUNDARY
#define STACK_BOUNDARY 32


/* Alignment of field after `int : 0' in a structure.
   For m68k/SVR4, this is the next longword boundary.  */

#undef EMPTY_FIELD_BOUNDARY
#define EMPTY_FIELD_BOUNDARY 32


/* No data type wants to be aligned rounder than this.
   For m68k/SVR4, some types (doubles for example) are aligned on 8 byte
   boundaries */

#undef BIGGEST_ALIGNMENT
#define BIGGEST_ALIGNMENT 64


/* For m68k SVR4, structures are returned using the reentrant
   technique.  */

#undef PCC_STATIC_STRUCT_RETURN


/* The svr4 ABI for the m68k says that records and unions are returned
   in memory.  */

#undef DEFAULT_PCC_STRUCT_RETURN
#define DEFAULT_PCC_STRUCT_RETURN 1

/* Output assembler code for a block containing the constant parts
   of a trampoline, leaving space for the variable parts.  */

/* On m68k svr4, the trampoline is different from the generic version
   in that we use a1 as the static call chain.  */

#undef TRAMPOLINE_TEMPLATE
#define TRAMPOLINE_TEMPLATE(FILE)					\
{									\
  assemble_aligned_integer (2, GEN_INT (0x227a));			\
  assemble_aligned_integer (2, GEN_INT (8));				\
  assemble_aligned_integer (2, GEN_INT (0x2f3a));			\
  assemble_aligned_integer (2, GEN_INT (8));				\
  assemble_aligned_integer (2, GEN_INT (0x4e75));			\
  assemble_aligned_integer (4, const0_rtx);				\
  assemble_aligned_integer (4, const0_rtx);				\
}

/* Redefine since we are using a different trampoline */
#undef TRAMPOLINE_SIZE
#define TRAMPOLINE_SIZE 18

/* Emit RTL insns to initialize the variable parts of a trampoline.
   FNADDR is an RTX for the address of the function's pure code.
   CXT is an RTX for the static chain value for the function.  */

#undef INITIALIZE_TRAMPOLINE
#define INITIALIZE_TRAMPOLINE(TRAMP, FNADDR, CXT)			\
{									\
  emit_move_insn (gen_rtx_MEM (SImode, plus_constant (TRAMP, 10)), CXT); \
  emit_move_insn (gen_rtx_MEM (SImode, plus_constant (TRAMP, 14)), FNADDR); \
}


/* XXX
   This is the end of the chunk lifted from m68kv4.h  */
