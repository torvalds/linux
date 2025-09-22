/* Definitions of target machine for GNU compiler, for PRO.
   Copyright (C) 1996, 1997, 2002, 2003, 2004 Free Software Foundation, Inc.

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

/* Make GCC agree with types.h.  */
#undef SIZE_TYPE
#undef PTRDIFF_TYPE

#define SIZE_TYPE "unsigned int"
#define PTRDIFF_TYPE "int"

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
	if (!c_dialect_cxx () && !flag_iso)	\
	  {					\
	    builtin_define ("hppa");		\
	    builtin_define_std ("PWB");		\
	  }					\
	builtin_define ("__pro__");		\
	builtin_assert ("system=pro");		\
    }						\
  while (0)

/* Like the default, except no -lg.  */
#undef LIB_SPEC
#define LIB_SPEC "%{!p:%{!pg:-lc}}%{p: -L/lib/libp/ -lc}%{pg: -L/lib/libp/ -lc}"

/* hpux8 and later have C++ compatible include files, so do not
   pretend they are `extern "C"'.  */
#define NO_IMPLICIT_EXTERN_C

/* We don't want a crt0.o to get linked in automatically, we want the
   linker script to pull it in.  */
#undef STARTFILE_SPEC
#define STARTFILE_SPEC ""

/* We need to override the following two macros defined in elfos.h since
   the .comm directive has a different syntax and it can't be used for
   local common symbols.  */
#undef ASM_OUTPUT_ALIGNED_COMMON
#define ASM_OUTPUT_ALIGNED_COMMON(FILE, NAME, SIZE, ALIGN)              \
  pa_asm_output_aligned_common (FILE, NAME, SIZE, ALIGN)

#undef ASM_OUTPUT_ALIGNED_LOCAL
#define ASM_OUTPUT_ALIGNED_LOCAL(FILE, NAME, SIZE, ALIGN)               \
  pa_asm_output_aligned_local (FILE, NAME, SIZE, ALIGN)
