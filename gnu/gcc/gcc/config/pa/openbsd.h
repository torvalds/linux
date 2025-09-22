/* Configuration file for an hppa risc OpenBSD target.

   Copyright (C) 1999 Free Software Foundation, Inc.

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */


#include <pa/pa32-regs.h>
#define OBSD_HAS_DECLARE_FUNCTION_NAME
#define OBSD_HAS_DECLARE_FUNCTION_SIZE
#define OBSD_HAS_DECLARE_OBJECT
#include <openbsd.h>

/* Turn off various SOM crap we don't want.  */
#undef TARGET_ELF32
#define TARGET_ELF32 1

#undef MAX_OFILE_ALIGNMENT
#define MAX_OFILE_ALIGNMENT 0x8000

/* Run-time target specifications. */
#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
	OPENBSD_OS_CPP_BUILTINS_ELF();		\
    }						\
  while (0)

/* We don't use profile counters.  */
#define NO_DEFERRED_PROFILE_COUNTERS 1

/* Define the strings used for the special svr4 .type and .size directives.
   These strings generally do not vary from one system running svr4 to
   another, but if a given system (e.g. m88k running svr) needs to use
   different pseudo-op names for these, they may be overridden in the
   file which includes this one.  */

#undef STRING_ASM_OP
#define STRING_ASM_OP   "\t.stringz\t"

#define TEXT_SECTION_ASM_OP "\t.text"
#define DATA_SECTION_ASM_OP "\t.data"
#define BSS_SECTION_ASM_OP "\t.section\t.bss"

/* We want local labels to start with period if made with asm_fprintf.  */
#undef LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX "."

/* Define these to generate the Linux/ELF/SysV style of internal
   labels all the time - i.e. to be compatible with
   ASM_GENERATE_INTERNAL_LABEL in <elfos.h>.  Compare these with the
   ones in pa.h and note the lack of dollar signs in these.  FIXME:
   shouldn't we fix pa.h to use ASM_GENERATE_INTERNAL_LABEL instead? */

#undef ASM_OUTPUT_ADDR_VEC_ELT
#define ASM_OUTPUT_ADDR_VEC_ELT(FILE, VALUE) \
  if (TARGET_BIG_SWITCH)					\
    fprintf (FILE, "\t.word .L%d\n", VALUE);			\
  else								\
    fprintf (FILE, "\tb .L%d\n\tnop\n", VALUE)

#undef ASM_OUTPUT_ADDR_DIFF_ELT
#define ASM_OUTPUT_ADDR_DIFF_ELT(FILE, BODY, VALUE, REL) \
  if (TARGET_BIG_SWITCH)					\
    fprintf (FILE, "\t.word .L%d-.L%d\n", VALUE, REL);		\
  else								\
    fprintf (FILE, "\tb .L%d\n\tnop\n", VALUE)

/* Use the default.  */
#undef ASM_OUTPUT_LABEL

/* NOTE: (*targetm.asm_out.internal_label)() is defined for us by elfos.h, and
   does what we want (i.e. uses colons).  It must be compatible with
   ASM_GENERATE_INTERNAL_LABEL(), so do not define it here.  */

/* Use the default.  */
#undef ASM_OUTPUT_INTERNAL_LABEL

/* Use the default.  */
#undef TARGET_ASM_GLOBALIZE_LABEL

/* FIXME: Hacked from the <elfos.h> one so that we avoid multiple
   labels in a function declaration (since pa.c seems determined to do
   it differently)  */

#undef ASM_DECLARE_FUNCTION_NAME
#define ASM_DECLARE_FUNCTION_NAME(FILE, NAME, DECL)		\
  do								\
    {								\
      ASM_OUTPUT_TYPE_DIRECTIVE (FILE, NAME, "function");	\
      ASM_DECLARE_RESULT (FILE, DECL_RESULT (DECL));		\
    }								\
  while (0)

/* As well as globalizing the label, we need to encode the label
   to ensure a plabel is generated in an indirect call.  */

#undef ASM_OUTPUT_EXTERNAL_LIBCALL
#define ASM_OUTPUT_EXTERNAL_LIBCALL(FILE, FUN)			\
  do								\
    {								\
      if (!FUNCTION_NAME_P (XSTR (FUN, 0)))			\
        hppa_encode_label (FUN);				\
      (*targetm.asm_out.globalize_label) (FILE, XSTR (FUN, 0));	\
    }								\
  while (0)

/* OpenBSD always uses gas. */
#undef TARGET_GAS
#define TARGET_GAS 1

/* XXX OpenBSD/hppa has a non-standard .comm */

#undef ASM_OUTPUT_ALIGNED_COMMON
#define ASM_OUTPUT_ALIGNED_COMMON(FILE, NAME, SIZE, ALIGN)  		\
  do									\
    {									\
      switch_to_section (bss_section);					\
      assemble_name((FILE), (NAME));					\
      fprintf((FILE), "\t.align %d\n", ((ALIGN) / BITS_PER_UNIT));	\
      fprintf ((FILE), "\t.comm %d\n",					\
	       MAX ((SIZE), ((ALIGN) / BITS_PER_UNIT)));		\
    }									\
  while (0)

#undef ASM_OUTPUT_ALIGNED_LOCAL
#define ASM_OUTPUT_ALIGNED_LOCAL(FILE, NAME, SIZE, ALIGN)  		\
  do									\
    {									\
      switch_to_section (bss_section);					\
      fprintf((FILE), "\t.align %d\n", ((ALIGN) / BITS_PER_UNIT));	\
      assemble_name((FILE), (NAME));					\
      fprintf ((FILE), "\t.block %d\n",	(SIZE));			\
    }									\
  while (0)

#undef TARGET_SCHED_DEFAULT
#define TARGET_SCHED_DEFAULT PROCESSOR_700

#undef LINK_SPEC
#define LINK_SPEC \
  "%{!shared:%{!nostdlib:%{!r*:%{!e*:-e __start}}}} \
   %{shared:-shared} %{R*} \
   %{static:-Bstatic} \
   %{!static:-Bdynamic} \
   %{rdynamic:-export-dynamic} \
   %{assert*} \
   %{!static:%{!dynamic-linker:-dynamic-linker /usr/libexec/ld.so}}"

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

#undef JUMP_TABLES_DEFAULT
#define JUMP_TABLES_DEFAULT 0	/* incompatible with --executable-text */

/* As an elf system, we need crtbegin/crtend stuff.  */
#undef STARTFILE_SPEC
#define STARTFILE_SPEC "\
	%{!shared: %{pg:gcrt0%O%s} %{!pg:%{p:gcrt0%O%s} \
	%{!p:%{!static:crt0%O%s} %{static:%{nopie:crt0%O%s} \
	%{!nopie:rcrt0%O%s}}}} crtbegin%O%s} %{shared:crtbeginS%O%s}"
#undef ENDFILE_SPEC
#define ENDFILE_SPEC "%{!shared:crtend%O%s} %{shared:crtendS%O%s}"
