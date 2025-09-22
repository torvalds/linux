/* Definitions for MIPS running Linux-based GNU systems with ELF format.
   Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
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

#undef WCHAR_TYPE
#define WCHAR_TYPE "int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

/* If defined, a C expression whose value is a string containing the
   assembler operation to identify the following data as
   uninitialized global data.  If not defined, and neither
   `ASM_OUTPUT_BSS' nor `ASM_OUTPUT_ALIGNED_BSS' are defined,
   uninitialized global data will be output in the data section if
   `-fno-common' is passed, otherwise `ASM_OUTPUT_COMMON' will be
   used.  */
#define BSS_SECTION_ASM_OP	"\t.section\t.bss"

#define ASM_OUTPUT_ALIGNED_BSS mips_output_aligned_bss

#undef ASM_DECLARE_OBJECT_NAME
#define ASM_DECLARE_OBJECT_NAME mips_declare_object_name

#undef TARGET_VERSION
#if TARGET_ENDIAN_DEFAULT == 0
#define TARGET_VERSION fprintf (stderr, " (MIPSel GNU/Linux with ELF)");
#else
#define TARGET_VERSION fprintf (stderr, " (MIPS GNU/Linux with ELF)");
#endif

#undef MD_EXEC_PREFIX
#undef MD_STARTFILE_PREFIX

/* If we don't set MASK_ABICALLS, we can't default to PIC.  */
#undef TARGET_DEFAULT
#define TARGET_DEFAULT MASK_ABICALLS

#define TARGET_OS_CPP_BUILTINS()				\
  do {								\
    LINUX_TARGET_OS_CPP_BUILTINS();				\
    /* The GNU C++ standard library requires this.  */		\
    if (c_dialect_cxx ())					\
      builtin_define ("_GNU_SOURCE");				\
    								\
    if (mips_abi == ABI_N32)					\
      {								\
        builtin_define ("_ABIN32=2");				\
        builtin_define ("_MIPS_SIM=_ABIN32");			\
        builtin_define ("_MIPS_SZLONG=32");			\
        builtin_define ("_MIPS_SZPTR=32");			\
      }								\
    else if (mips_abi == ABI_64)				\
      {								\
        builtin_define ("_ABI64=3");				\
        builtin_define ("_MIPS_SIM=_ABI64");			\
        builtin_define ("_MIPS_SZLONG=64");			\
        builtin_define ("_MIPS_SZPTR=64");			\
      }								\
    else							\
      {								\
	builtin_define ("_ABIO32=1");				\
	builtin_define ("_MIPS_SIM=_ABIO32");			\
        builtin_define ("_MIPS_SZLONG=32");			\
        builtin_define ("_MIPS_SZPTR=32");			\
      }								\
    if (TARGET_FLOAT64)						\
      builtin_define ("_MIPS_FPSET=32");			\
    else							\
      builtin_define ("_MIPS_FPSET=16");			\
    								\
    builtin_define ("_MIPS_SZINT=32");				\
  } while (0)

#undef SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC "%{posix:-D_POSIX_SOURCE} %{pthread:-D_REENTRANT}"

/* A standard GNU/Linux mapping.  On most targets, it is included in
   CC1_SPEC itself by config/linux.h, but mips.h overrides CC1_SPEC
   and provides this hook instead.  */
#undef SUBTARGET_CC1_SPEC
#define SUBTARGET_CC1_SPEC "%{profile:-p}"

/* From iris5.h */
/* -G is incompatible with -KPIC which is the default, so only allow objects
   in the small data section if the user explicitly asks for it.  */
#undef MIPS_DEFAULT_GVALUE
#define MIPS_DEFAULT_GVALUE 0

#define GLIBC_DYNAMIC_LINKER "/lib/ld.so.1"

/* Borrowed from sparc/linux.h */
#undef LINK_SPEC
#define LINK_SPEC \
 "%(endian_spec) \
  %{shared:-shared} \
  %{!shared: \
    %{!ibcs: \
      %{!static: \
        %{rdynamic:-export-dynamic} \
        %{!dynamic-linker:-dynamic-linker " LINUX_DYNAMIC_LINKER "}} \
        %{static:-static}}}"

#undef SUBTARGET_ASM_SPEC
#define SUBTARGET_ASM_SPEC "%{mabi=64: -64} %{!mno-abicalls:-KPIC}"

/* The MIPS assembler has different syntax for .set. We set it to
   .dummy to trap any errors.  */
#undef SET_ASM_OP
#define SET_ASM_OP "\t.dummy\t"

#undef ASM_OUTPUT_DEF
#define ASM_OUTPUT_DEF(FILE,LABEL1,LABEL2)				\
 do {									\
	fputc ( '\t', FILE);						\
	assemble_name (FILE, LABEL1);					\
	fputs ( " = ", FILE);						\
	assemble_name (FILE, LABEL2);					\
	fputc ( '\n', FILE);						\
 } while (0)

#undef ASM_DECLARE_FUNCTION_NAME
#define ASM_DECLARE_FUNCTION_NAME(STREAM, NAME, DECL)			\
  do {									\
    if (!flag_inhibit_size_directive)					\
      {									\
	fputs ("\t.ent\t", STREAM);					\
	assemble_name (STREAM, NAME);					\
	putc ('\n', STREAM);						\
      }									\
    ASM_OUTPUT_TYPE_DIRECTIVE (STREAM, NAME, "function");		\
    assemble_name (STREAM, NAME);					\
    fputs (":\n", STREAM);						\
  } while (0)

#undef ASM_DECLARE_FUNCTION_SIZE
#define ASM_DECLARE_FUNCTION_SIZE(STREAM, NAME, DECL)			\
  do {									\
    if (!flag_inhibit_size_directive)					\
      {									\
	fputs ("\t.end\t", STREAM);					\
	assemble_name (STREAM, NAME);					\
	putc ('\n', STREAM);						\
      }									\
  } while (0)

/* Tell function_prologue in mips.c that we have already output the .ent/.end
   pseudo-ops.  */
#undef FUNCTION_NAME_ALREADY_DECLARED
#define FUNCTION_NAME_ALREADY_DECLARED 1

/* The glibc _mcount stub will save $v0 for us.  Don't mess with saving
   it, since ASM_OUTPUT_REG_PUSH/ASM_OUTPUT_REG_POP do not work in the
   presence of $gp-relative calls.  */
#undef ASM_OUTPUT_REG_PUSH
#undef ASM_OUTPUT_REG_POP

#undef LIB_SPEC
#define LIB_SPEC "\
%{shared: -lc} \
%{!shared: %{pthread:-lpthread} \
  %{profile:-lc_p} %{!profile: -lc}}"

#define MD_UNWIND_SUPPORT "config/mips/linux-unwind.h"
