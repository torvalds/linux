/* Configuration file for an m88k OpenBSD ELF target.
   Copyright (C) 2000, 2004, 2005, 2012 Free Software Foundation, Inc.

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

#define REGISTER_PREFIX "%"
#undef ASM_COMMENT_START
#define ASM_COMMENT_START "|"

/* #define CTORS_SECTION_ASM_OP	"\t.section\t .ctors" */
/* #define DTORS_SECTION_ASM_OP	"\t.section\t .dtors" */

#define TEXT_SECTION_ASM_OP	"\t.text"
#define DATA_SECTION_ASM_OP	"\t.data"
#define FILE_ASM_OP		"\t.file\t"
#define BSS_ASM_OP		"\t.bss\t"
#define	REQUIRES_88110_ASM_OP	"\t.requires_88110\t"

#undef ASM_OUTPUT_INTERNAL_LABEL

/* This is how to output an assembler line
   that says to advance the location counter
   to a multiple of 2**LOG bytes.  */
#undef ASM_OUTPUT_ALIGN
#define ASM_OUTPUT_ALIGN(FILE,LOG)	\
  if ((LOG) != 0)			\
    fprintf (FILE, "%s%d\n", ALIGN_ASM_OP, (LOG))

#undef ASM_OUTPUT_WEAK_ALIAS
#define ASM_OUTPUT_WEAK_ALIAS(FILE,NAME,VALUE)		\
  do							\
    {							\
      fputs ("\t.weak\t", FILE);			\
      assemble_name (FILE, NAME);			\
      if (VALUE)					\
	{						\
	  fputs ("; ", FILE);				\
	  assemble_name (FILE, NAME);			\
	  fputs (" = ", FILE);				\
	  assemble_name (FILE, VALUE);			\
	}						\
      fputc ('\n', FILE);				\
    }							\
  while (0)

#undef	FUNCTION_PROFILER
#define FUNCTION_PROFILER(FILE, LABELNO) \
  output_function_profiler (FILE, LABELNO, "__mcount")

/* Run-time target specifications.  */
#define TARGET_OS_CPP_BUILTINS()			\
  OPENBSD_OS_CPP_BUILTINS_ELF()

/* Layout of source language data types. */

/* This must agree with <machine/_types.h> */
#undef SIZE_TYPE
#define SIZE_TYPE "long unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "long int"

#undef INTMAX_TYPE
#define INTMAX_TYPE "long long int"

#undef UINTMAX_TYPE
#define UINTMAX_TYPE "long long unsigned int"

#undef WCHAR_TYPE
#define WCHAR_TYPE "int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

#undef WINT_TYPE
#define WINT_TYPE "int"

/* Due to the split instruction and data caches, trampolines must cause the
   data cache to be synced before attempting to execute the trampoline code.
   Under OpenBSD, this is done by invoking trap #451 with r2 and r3 set to
   the address of the trampoline area and its size, respectively.  */
#undef FINALIZE_TRAMPOLINE
#define FINALIZE_TRAMPOLINE(TRAMP)					\
  emit_library_call(gen_rtx_SYMBOL_REF (Pmode, "__dcache_sync"),	\
		    0, VOIDmode, 2, (TRAMP), Pmode,			\
		    GEN_INT (TRAMPOLINE_SIZE), Pmode)

#if defined(CROSS_COMPILE) && !defined(ATTRIBUTE_UNUSED)
#define ATTRIBUTE_UNUSED
#endif
#undef TRANSFER_FROM_TRAMPOLINE
#define TRANSFER_FROM_TRAMPOLINE					\
extern void __dcache_sync(int, int);					\
void									\
__dcache_sync (addr, len)						\
     int addr ATTRIBUTE_UNUSED, len ATTRIBUTE_UNUSED;			\
{									\
  /* r2 and r3 are set by the caller and need not be modified */	\
  __asm __volatile ("tb0 0, %r0, 451");					\
}

#undef LINK_SPEC
#define LINK_SPEC \
  "%{!shared:%{!nostdlib:%{!r*:%{!e*:-e __start}}}} \
   %{shared:-shared} %{R*} \
   %{static:-Bstatic} \
   %{!static:-Bdynamic} \
   %{rdynamic:-export-dynamic} \
   %{assert*} \
   %{!static:%{!dynamic-linker:-dynamic-linker /usr/libexec/ld.so}}"

/* As an elf system, we need crtbegin/crtend stuff.  */
#undef STARTFILE_SPEC
#define STARTFILE_SPEC "\
	%{!shared: %{pg:gcrt0%O%s} %{!pg:%{p:gcrt0%O%s} %{!p:crt0%O%s}} \
	crtbegin%O%s} %{shared:crtbeginS%O%s}"
#undef ENDFILE_SPEC
#define ENDFILE_SPEC "%{!shared:crtend%O%s} %{shared:crtendS%O%s}"
