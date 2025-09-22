/* Definitions for Linux-based GNU systems with ELF format
   Copyright (C) 1995, 1996, 1997, 1998, 1999, 2000, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by Eric Youngdale.
   Modified for stabs-in-ELF by H.J. Lu (hjl@lucon.org).

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

/* Don't assume anything about the header files.  */
#define NO_IMPLICIT_EXTERN_C

#undef ASM_APP_ON
#define ASM_APP_ON "#APP\n"

#undef ASM_APP_OFF
#define ASM_APP_OFF "#NO_APP\n"

#undef MD_EXEC_PREFIX
#undef MD_STARTFILE_PREFIX

/* Provide a STARTFILE_SPEC appropriate for GNU/Linux.  Here we add
   the GNU/Linux magical crtbegin.o file (see crtstuff.c) which
   provides part of the support for getting C++ file-scope static
   object constructed before entering `main'.  */
   
#undef	STARTFILE_SPEC
#if defined HAVE_LD_PIE
#define STARTFILE_SPEC \
  "%{!shared: %{pg|p|profile:gcrt1.o%s;pie:Scrt1.o%s;:crt1.o%s}} \
   crti.o%s %{static:crtbeginT.o%s;shared|pie:crtbeginS.o%s;:crtbegin.o%s}"
#else
#define STARTFILE_SPEC \
  "%{!shared: %{pg|p|profile:gcrt1.o%s;:crt1.o%s}} \
   crti.o%s %{static:crtbeginT.o%s;shared|pie:crtbeginS.o%s;:crtbegin.o%s}"
#endif

/* Provide a ENDFILE_SPEC appropriate for GNU/Linux.  Here we tack on
   the GNU/Linux magical crtend.o file (see crtstuff.c) which
   provides part of the support for getting C++ file-scope static
   object constructed before entering `main', followed by a normal
   GNU/Linux "finalizer" file, `crtn.o'.  */

#undef	ENDFILE_SPEC
#define ENDFILE_SPEC \
  "%{shared|pie:crtendS.o%s;:crtend.o%s} crtn.o%s"

/* This is for -profile to use -lc_p instead of -lc.  */
#ifndef CC1_SPEC
#define CC1_SPEC "%{profile:-p}"
#endif

/* The GNU C++ standard library requires that these macros be defined.  */
#undef CPLUSPLUS_CPP_SPEC
#define CPLUSPLUS_CPP_SPEC "-D_GNU_SOURCE %(cpp)"

#undef	LIB_SPEC
#define LIB_SPEC \
  "%{pthread:-lpthread} \
   %{shared:-lc} \
   %{!shared:%{mieee-fp:-lieee} %{profile:-lc_p}%{!profile:-lc}}"

#define LINUX_TARGET_OS_CPP_BUILTINS()				\
    do {							\
	builtin_define ("__gnu_linux__");			\
	builtin_define_std ("linux");				\
	builtin_define_std ("unix");				\
	builtin_assert ("system=linux");			\
	builtin_assert ("system=unix");				\
	builtin_assert ("system=posix");			\
    } while (0)

#if defined(HAVE_LD_EH_FRAME_HDR)
#define LINK_EH_SPEC "%{!static:--eh-frame-hdr} "
#endif

/* Define this so we can compile MS code for use with WINE.  */
#define HANDLE_PRAGMA_PACK_PUSH_POP

#define LINK_GCC_C_SEQUENCE_SPEC \
  "%{static:--start-group} %G %L %{static:--end-group}%{!static:%G}"

/* Use --as-needed -lgcc_s for eh support.  */
#ifdef HAVE_LD_AS_NEEDED
#define USE_LD_AS_NEEDED 1
#endif

/* Determine which dynamic linker to use depending on whether GLIBC or
   uClibc is the default C library and whether -muclibc or -mglibc has
   been passed to change the default.  */
#if UCLIBC_DEFAULT
#define CHOOSE_DYNAMIC_LINKER(G, U) "%{mglibc:%{muclibc:%e-mglibc and -muclibc used together}" G ";:" U "}"
#else
#define CHOOSE_DYNAMIC_LINKER(G, U) "%{muclibc:%{mglibc:%e-mglibc and -muclibc used together}" U ";:" G "}"
#endif

/* For most targets the following definitions suffice;
   GLIBC_DYNAMIC_LINKER must be defined for each target using them, or
   GLIBC_DYNAMIC_LINKER32 and GLIBC_DYNAMIC_LINKER64 for targets
   supporting both 32-bit and 64-bit compilation.  */
#define UCLIBC_DYNAMIC_LINKER "/lib/ld-uClibc.so.0"
#define UCLIBC_DYNAMIC_LINKER32 "/lib/ld-uClibc.so.0"
#define UCLIBC_DYNAMIC_LINKER64 "/lib/ld64-uClibc.so.0"
#define LINUX_DYNAMIC_LINKER \
  CHOOSE_DYNAMIC_LINKER (GLIBC_DYNAMIC_LINKER, UCLIBC_DYNAMIC_LINKER)
#define LINUX_DYNAMIC_LINKER32 \
  CHOOSE_DYNAMIC_LINKER (GLIBC_DYNAMIC_LINKER32, UCLIBC_DYNAMIC_LINKER32)
#define LINUX_DYNAMIC_LINKER64 \
  CHOOSE_DYNAMIC_LINKER (GLIBC_DYNAMIC_LINKER64, UCLIBC_DYNAMIC_LINKER64)

/* Determine whether the entire c99 runtime
   is present in the runtime library.  */
#define TARGET_C99_FUNCTIONS (OPTION_GLIBC)

#define TARGET_POSIX_IO
