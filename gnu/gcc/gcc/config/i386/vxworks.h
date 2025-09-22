/* IA32 VxWorks target definitions for GNU compiler.
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
   Updated by CodeSourcery, LLC.

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

#define HANDLE_SYSV_PRAGMA 1

#undef  TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (80586, VxWorks syntax)");

#undef  ASM_SPEC
#define ASM_SPEC "%{v:-v} %{Qy:} %{n} %{T} %{Ym,*} %{Yd,*} %{Wa,*:%*}"

#define VXWORKS_CPU_DEFINE()				\
  do							\
    {							\
      if (TARGET_386)					\
        builtin_define ("CPU=I80386");			\
      else if (TARGET_486)				\
        builtin_define ("CPU=I80486");			\
      else if (TARGET_PENTIUM)				\
        {						\
          builtin_define ("CPU=PENTIUM");		\
          builtin_define ("CPU_VARIANT=PENTIUM");	\
        }						\
      else if (TARGET_PENTIUMPRO)			\
        {						\
          builtin_define ("CPU=PENTIUM2");		\
          builtin_define ("CPU_VARIANT=PENTIUMPRO");	\
        }						\
      else if (TARGET_PENTIUM4)				\
        {						\
          builtin_define ("CPU=PENTIUM4");		\
          builtin_define ("CPU_VARIANT=PENTIUM4");	\
        }						\
    }  							\
  while (0)

#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define ("__vxworks");		\
      builtin_define ("__VXWORKS__");		\
      builtin_assert ("system=unix");		\
						\
      VXWORKS_CPU_DEFINE();			\
    }						\
  while (0)

#undef  CPP_SPEC
#define CPP_SPEC VXWORKS_ADDITIONAL_CPP_SPEC
#undef  LIB_SPEC
#define LIB_SPEC VXWORKS_LIB_SPEC
#undef  STARTFILE_SPEC
#define STARTFILE_SPEC VXWORKS_STARTFILE_SPEC
#undef  ENDFILE_SPEC
#define ENDFILE_SPEC VXWORKS_ENDFILE_SPEC
#undef  LINK_SPEC
#define LINK_SPEC VXWORKS_LINK_SPEC

#undef  SUBTARGET_SWITCHES
#define SUBTARGET_SWITCHES EXTRA_SUBTARGET_SWITCHES

#undef SUBTARGET_OVERRIDE_OPTIONS
#define SUBTARGET_OVERRIDE_OPTIONS VXWORKS_OVERRIDE_OPTIONS

/* No _mcount profiling on VxWorks.  */
#undef FUNCTION_PROFILER
#define FUNCTION_PROFILER(FILE,LABELNO) VXWORKS_FUNCTION_PROFILER(FILE,LABELNO)
