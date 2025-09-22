/* Configuration file for an m68k OpenBSD target.
   Copyright (C) 1999, 2002, 2003 Free Software Foundation, Inc.

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

/* Target OS builtins.  */
#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
	builtin_define ("__unix__");		\
	builtin_define ("__OpenBSD__");		\
	builtin_assert ("system=unix");		\
	builtin_assert ("system=OpenBSD");	\
   }						\
  while (0)

/* Define __HAVE_68881__ in preprocessor, unless -msoft-float is specified.
   This will control the use of inline 68881 insns in certain macros.  */
#undef CPP_SPEC
#define CPP_SPEC "%{!msoft-float:-D__HAVE_68881__ -D__HAVE_FPU__} %{posix:-D_POSIX_SOURCE} %{pthread:-D_REENTRANT}"

/* m68k as needs to know about the processor subtype.  */
#undef ASM_SPEC
#define ASM_SPEC "%{m68030} %{m68040} %{m68060} %{fpic|fpie:-k} %{fPIC|fPIE:-k -K}"

#define AS_NEEDS_DASH_FOR_PIPED_INPUT

/* Layout of source language data types.  */

/* This must agree with <machine/ansi.h> */
#undef SIZE_TYPE
#define SIZE_TYPE "unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "int"

#undef WCHAR_TYPE
#define WCHAR_TYPE "int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

/* Storage layout.  */

/* Every structure or union's size must be a multiple of 2 bytes.  */
#define STRUCTURE_SIZE_BOUNDARY 16

/* Specific options for DBX Output.  */

/* This is BSD, so it wants DBX format.  */
#define DBX_DEBUGGING_INFO 1

/* Do not break .stabs pseudos into continuations.  */
#define DBX_CONTIN_LENGTH 0

/* This is the char to use for continuation (in case we need to turn
   continuation back on).  */
#define DBX_CONTIN_CHAR '?'

/* Stack & calling: aggregate returns.  */

/* Don't default to pcc-struct-return, because gcc is the only compiler, and
   we want to retain compatibility with older gcc versions.  */
#define DEFAULT_PCC_STRUCT_RETURN 0

/* Assembler format: exception region output.  */

/* All configurations that don't use elf must be explicit about not using
   dwarf unwind information.  */
#define DWARF2_UNWIND_INFO 0
