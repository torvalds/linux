/* Base configuration file for all FreeBSD targets.
   Copyright (C) 1999, 2000, 2001, 2004, 2005 Free Software Foundation, Inc.

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

/* Common FreeBSD configuration. 
   All FreeBSD architectures should include this file, which will specify
   their commonalities.
   Adapted from gcc/config/freebsd.h by 
   David O'Brien <obrien@FreeBSD.org>
   Loren J. Rittle <ljrittle@acm.org>.  */


/* In case we need to know.  */
#define USING_CONFIG_FREEBSD_SPEC 1

/* This defines which switch letters take arguments.  On FreeBSD, most of
   the normal cases (defined in gcc.c) apply, and we also have -h* and
   -z* options (for the linker) (coming from SVR4).
   We also have -R (alias --rpath), no -z, --soname (-h), --assert etc.  */

#define FBSD_SWITCH_TAKES_ARG(CHAR)					\
  (DEFAULT_SWITCH_TAKES_ARG (CHAR)					\
    || (CHAR) == 'h'							\
    || (CHAR) == 'z' /* ignored by ld */				\
    || (CHAR) == 'R')

/* This defines which multi-letter switches take arguments.  */

#define FBSD_WORD_SWITCH_TAKES_ARG(STR)					\
  (DEFAULT_WORD_SWITCH_TAKES_ARG (STR)					\
   || !strcmp ((STR), "rpath") || !strcmp ((STR), "rpath-link")		\
   || !strcmp ((STR), "soname") || !strcmp ((STR), "defsym") 		\
   || !strcmp ((STR), "assert") || !strcmp ((STR), "dynamic-linker"))

#define FBSD_TARGET_OS_CPP_BUILTINS()					\
  do									\
    {									\
	builtin_define_with_int_value ("__FreeBSD__", FBSD_MAJOR);	\
	builtin_define_std ("unix");					\
	builtin_define ("__KPRINTF_ATTRIBUTE__");		       	\
	builtin_assert ("system=unix");					\
	builtin_assert ("system=bsd");					\
	builtin_assert ("system=FreeBSD");				\
	FBSD_TARGET_CPU_CPP_BUILTINS();					\
    }									\
  while (0)

/* Define the default FreeBSD-specific per-CPU hook code.  */
#define FBSD_TARGET_CPU_CPP_BUILTINS() do {} while (0)

/* Provide a CPP_SPEC appropriate for FreeBSD.  We just deal with the GCC 
   option `-posix', and PIC issues.  */

#define FBSD_CPP_SPEC "							\
  %(cpp_cpu)								\
  %(cpp_arch)								\
  %{posix:-D_POSIX_SOURCE}"

/* Provide a STARTFILE_SPEC appropriate for FreeBSD.  Here we add
   the magical crtbegin.o file (see crtstuff.c) which provides part 
	of the support for getting C++ file-scope static object constructed 
	before entering `main'.  */
   
#define FBSD_STARTFILE_SPEC \
  "%{!shared: \
     %{pg:gcrt1.o%s} %{!pg:%{p:gcrt1.o%s} \
		       %{!p:%{profile:gcrt1.o%s} \
			 %{!profile:crt1.o%s}}}} \
   crti.o%s %{!shared:crtbegin.o%s} %{shared:crtbeginS.o%s}"

/* Provide a ENDFILE_SPEC appropriate for FreeBSD.  Here we tack on
   the magical crtend.o file (see crtstuff.c) which provides part of 
	the support for getting C++ file-scope static object constructed 
	before entering `main', followed by a normal "finalizer" file, 
	`crtn.o'.  */

#define FBSD_ENDFILE_SPEC \
  "%{!shared:crtend.o%s} %{shared:crtendS.o%s} crtn.o%s"

/* Provide a LIB_SPEC appropriate for FreeBSD as configured and as
   required by the user-land thread model.  Before __FreeBSD_version
   500016, select the appropriate libc, depending on whether we're
   doing profiling or need threads support.  At __FreeBSD_version
   500016 and later, when threads support is requested include both
   -lc and the threading lib instead of only -lc_r.  To make matters
   interesting, we can't actually use __FreeBSD_version provided by
   <osreldate.h> directly since it breaks cross-compiling.  As a final
   twist, make it a hard error if -pthread is provided on the command
   line and gcc was configured with --disable-threads (this will help
   avoid bug reports from users complaining about threading when they
   misconfigured the gcc bootstrap but are later consulting FreeBSD
   manual pages that refer to the mythical -pthread option).  */

/* Provide a LIB_SPEC appropriate for FreeBSD.  Just select the appropriate
   libc, depending on whether we're doing profiling or need threads support.
   (similar to the default, except no -lg, and no -p).  */

#ifdef FBSD_NO_THREADS
#define FBSD_LIB_SPEC "							\
  %{pthread: %eThe -pthread option is only supported on FreeBSD when gcc \
is built with the --enable-threads configure-time option.}		\
  %{!shared:								\
    %{!pg: -lc}								\
    %{pg:  -lc_p}							\
  }"
#else
#if FBSD_MAJOR < 5
#define FBSD_LIB_SPEC "							\
  %{!shared:								\
    %{!pg:								\
      %{!pthread:-lc}							\
      %{pthread:-lc_r}}							\
    %{pg:								\
      %{!pthread:-lc_p}							\
      %{pthread:-lc_r_p}}						\
  }"
#else
#define FBSD_LIB_SPEC "							\
  %{!shared:								\
    %{!pg: %{pthread:-lpthread} -lc}					\
    %{pg:  %{pthread:-lpthread_p} -lc_p}				\
  }"
#endif
#endif

#if FBSD_MAJOR < 6
#define FBSD_DYNAMIC_LINKER "/usr/libexec/ld-elf.so.1"
#else
#define FBSD_DYNAMIC_LINKER "/libexec/ld-elf.so.1"
#endif
