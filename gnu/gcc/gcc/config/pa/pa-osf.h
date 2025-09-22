/* Definitions of target machine for GNU compiler, for HP PA-RISC 1.1
   Copyright (C) 1991, 1995, 1996, 2002, 2003 Free Software Foundation, Inc.
   Contributed by Tim Moore (moore@defmacro.cs.utah.edu)

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

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
	if (TARGET_PA_11)			\
	  {					\
	    builtin_define_std ("hp700");	\
	    builtin_define_std ("HP700");	\
	  }					\
	else					\
	  {					\
	    builtin_define_std ("hp800");	\
	    builtin_define_std ("hp9k8");	\
	    builtin_define_std ("hp9000s800");	\
	  }					\
	builtin_define_std ("BIT_MSF");		\
	builtin_define_std ("BYTE_MSF");	\
	builtin_define_std ("PARISC");		\
	builtin_define_std ("REVARGV");		\
	builtin_define_std ("hp9000");		\
	builtin_define ("__pa_risc");		\
	builtin_define_std ("parisc");		\
	builtin_define_std ("spectrum");	\
	builtin_define_std ("unix");		\
	builtin_assert ("system=mach");		\
	builtin_assert ("system=unix");		\
    }						\
  while (0)

/* Don't default to pcc-struct-return, because gcc is the only compiler, and
   we want to retain compatibility with older gcc versions.  */
#define DEFAULT_PCC_STRUCT_RETURN 0

/* OSF1 on the PA still uses 16bit wchar_t.  */
#undef WCHAR_TYPE
#undef WCHAR_TYPE_SIZE

#define WCHAR_TYPE "short unsigned int"
#define WCHAR_TYPE_SIZE 16

/* OSF1 wants to be different and use unsigned long as size_t.  */
#undef SIZE_TYPE
#define SIZE_TYPE "long unsigned int"
