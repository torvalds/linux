/* Definitions of target machine for GNU compiler, for HP PA-RISC
   Copyright (C) 1995, 1996, 1997, 2000, 2001, 2002, 2003, 2004
   Free Software Foundation, Inc.
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

/* GCC always defines __STDC__.  HP C++ compilers don't define it.  This
   causes trouble when sys/stdsyms.h is included.  As a work around,
   we define __STDC_EXT__.  A similar situation exists with respect to
   the definition of __cplusplus.  We define _INCLUDE_LONGLONG
   to prevent nlist.h from defining __STDC_32_MODE__ (no longlong
   support).  We define __STDCPP__ to get certain system headers
   (notably assert.h) to assume standard preprocessor behavior in C++.  */
#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()					\
  do									\
    {									\
	builtin_assert ("system=hpux");					\
	builtin_assert ("system=unix");					\
	builtin_define ("__hp9000s800");				\
	builtin_define ("__hp9000s800__");				\
	builtin_define ("__hpux");					\
	builtin_define ("__hpux__");					\
	builtin_define ("__unix");					\
	builtin_define ("__unix__");					\
	if (c_dialect_cxx ())						\
	  {								\
	    builtin_define ("_HPUX_SOURCE");				\
	    builtin_define ("_INCLUDE_LONGLONG");			\
	    builtin_define ("__STDC_EXT__");				\
	    builtin_define ("__STDCPP__");				\
	  }								\
	else if (!flag_iso)						\
	  {								\
	    builtin_define ("_HPUX_SOURCE");				\
	    if (preprocessing_trad_p ())				\
	      {								\
		builtin_define ("hp9000s800");				\
		builtin_define ("hppa");				\
		builtin_define ("hpux");				\
		builtin_define ("unix");				\
		builtin_define ("__CLASSIC_C__");			\
		builtin_define ("_PWB");				\
		builtin_define ("PWB");					\
	      }								\
	    else							\
	      builtin_define ("__STDC_EXT__");				\
	  }								\
	if (flag_pa_unix >= 1995)					\
	  {								\
	    builtin_define ("_XOPEN_UNIX");				\
	    builtin_define ("_XOPEN_SOURCE_EXTENDED");			\
	  }								\
	if (TARGET_SIO)							\
	  builtin_define ("_SIO");					\
	else								\
	  {								\
	    builtin_define ("__hp9000s700");				\
	    builtin_define ("__hp9000s700__");				\
	    builtin_define ("_WSIO");					\
	  }								\
    }									\
  while (0)

#define CPP_SPEC "%{threads: -D_REENTRANT -D_DCE_THREADS}"

/* We can debug dynamically linked executables on hpux9; we also want
   dereferencing of a NULL pointer to cause a SEGV.  */
#undef LINK_SPEC
#if ((TARGET_DEFAULT | TARGET_CPU_DEFAULT) & MASK_PA_11)
#define LINK_SPEC \
  "%{!mpa-risc-1-0:%{!march=1.0:%{!shared:-L/lib/pa1.1 -L/usr/lib/pa1.1 }}}\
   %{!shared:%{p:-L/lib/libp %{!static:\
     %nWarning: consider linking with `-static' as system libraries with\n\
     %n  profiling support are only provided in archive format}}}\
   %{!shared:%{pg:-L/lib/libp %{!static:\
     %nWarning: consider linking with `-static' as system libraries with\n\
     %n  profiling support are only provided in archive format}}}\
   -z %{mlinker-opt:-O} %{!shared:-u main}\
   %{static:-a archive} %{shared:-b}"
#else
#define LINK_SPEC \
  "%{!shared:%{p:-L/lib/libp %{!static:\
     %nWarning: consider linking with `-static' as system libraries with\n\
     %n  profiling support are only provided in archive format}}}\
   %{!shared:%{pg:-L/lib/libp %{!static:\
     %nWarning: consider linking with `-static' as system libraries with\n\
     %n  profiling support are only provided in archive format}}}\
   -z %{mlinker-opt:-O} %{!shared:-u main}\
   %{static:-a archive} %{shared:-b}"
#endif

/* Like the default, except no -lg.  */
#undef LIB_SPEC
#define LIB_SPEC \
  "%{!shared:\
     %{!p:%{!pg:\
       %{!threads:-lc %{static:%{!nolibdld:-a shared -ldld -a archive -lc}}}\
       %{threads:-lcma -lc_r}}}\
     %{p:%{!pg:-lc %{static:%{!nolibdld:-a shared -ldld -a archive -lc}}}}\
     %{pg:-lc %{static:%{!nolibdld:-a shared -ldld -a archive -lc}}}}"

#undef THREAD_MODEL_SPEC
#define THREAD_MODEL_SPEC "%{!threads:single}%{threads:dce}"

/* Under hpux10, the normal location of the `ld' and `as' programs is the
   /usr/ccs/bin directory.  */

#ifndef CROSS_COMPILE
#undef MD_EXEC_PREFIX
#define MD_EXEC_PREFIX "/usr/ccs/bin/"
#endif

/* Under hpux10, the normal location of the various *crt*.o files is
   the /usr/ccs/lib directory.  However, the profiling files are in
   /opt/langtools/lib.  */

#ifndef CROSS_COMPILE
#undef MD_STARTFILE_PREFIX
#define MD_STARTFILE_PREFIX "/usr/ccs/lib/"
#define MD_STARTFILE_PREFIX_1 "/opt/langtools/lib/"
#endif

/* hpux10 has the new HP assembler.  It's still lousy, but it's a whole lot
   better than the assembler shipped with older versions of hpux.  */
#undef NEW_HP_ASSEMBLER
#define NEW_HP_ASSEMBLER 1
