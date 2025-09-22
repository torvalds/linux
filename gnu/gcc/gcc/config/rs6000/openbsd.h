/* Configuration file for an rs6000 OpenBSD target.
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

/* XXX need to check ASM_WEAKEN_LABEL/ASM_GLOBALIZE_LABEL. */

/* Run-time target specifications. */
#undef TARGET_OS_CPP_BUILTINS	/* FIXME: sysv4.h should not define this! */
#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
	OPENBSD_OS_CPP_BUILTINS_ELF();		\
	builtin_define ("__PPC");		\
	builtin_define ("__PPC__");		\
	builtin_define ("__powerpc");		\
	builtin_define ("__powerpc__");		\
	builtin_assert ("cpu=powerpc");		\
	builtin_assert ("machine=powerpc");	\
    }						\
  while (0)

/* Override the default from rs6000.h to avoid conflicts with macros
   defined in OpenBSD header files.  */

#undef RS6000_CPU_CPP_ENDIAN_BUILTINS
#define RS6000_CPU_CPP_ENDIAN_BUILTINS()	\
  do						\
    {						\
      if (BYTES_BIG_ENDIAN)			\
	{					\
	  builtin_define ("__BIG_ENDIAN__");	\
	  builtin_assert ("machine=bigendian");	\
	}					\
      else					\
	{					\
	  builtin_define ("__LITTLE_ENDIAN__");	\
	  builtin_assert ("machine=littleendian"); \
	}					\
    }						\
  while (0)

#undef	CPP_OS_DEFAULT_SPEC
#define CPP_OS_DEFAULT_SPEC "%(cpp_os_openbsd)"

#undef LINK_SPEC
#define LINK_SPEC "%{shared:-shared} \
  %{!shared: \
    %{!static: \
      %{rdynamic:-export-dynamic} \
      %{!dynamic-linker:-dynamic-linker /usr/libexec/ld.so}} \
    %{static:-static}}"

#undef	LIB_DEFAULT_SPEC
#define LIB_DEFAULT_SPEC "%(lib_openbsd)"

#undef	STARTFILE_DEFAULT_SPEC
#define STARTFILE_DEFAULT_SPEC "%(startfile_openbsd)"

#undef CRTSAVRES_DEFAULT_SPEC 
#define CRTSAVRES_DEFAULT_SPEC ""

#undef	ENDFILE_DEFAULT_SPEC
#define ENDFILE_DEFAULT_SPEC "%(endfile_openbsd)"

#undef	LINK_START_DEFAULT_SPEC
#define LINK_START_DEFAULT_SPEC "%(link_start_openbsd)"

#undef	LINK_OS_DEFAULT_SPEC
#define LINK_OS_DEFAULT_SPEC "%(link_os_openbsd)"

#undef TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (PowerPC OpenBSD)");

/* Default ABI to use */
#undef RS6000_ABI_NAME 
#define RS6000_ABI_NAME "openbsd"

/* Define this macro as a C expression for the initializer of an
   array of string to tell the driver program which options are
   defaults for this target and thus do not need to be handled
   specially when using `MULTILIB_OPTIONS'.

   Do not define this macro if `MULTILIB_OPTIONS' is not defined in
   the target makefile fragment or if none of the options listed in
   `MULTILIB_OPTIONS' are set by default.  *Note Target Fragment::.  */

#undef	MULTILIB_DEFAULTS
#define	MULTILIB_DEFAULTS { "mbig", "mcall-openbsd" }

/* collect2 support (Macros for initialization). */


/* Don't tell collect2 we use COFF as we don't have (yet ?) a dynamic ld
   library with the proper functions to handle this -> collect2 will
   default to using nm. */
#undef OBJECT_FORMAT_COFF

/* Some code gets optimized incorrectly by move_movables() in loop.c */
#define	BROKEN_MOVE_MOVABLES_P

/* This must agree with <machine/_types.h> */
#undef SIZE_TYPE
#define SIZE_TYPE "long unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "long int"

#undef WCHAR_TYPE
#define WCHAR_TYPE	"int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE	32

#undef TRAMPOLINE_SIZE
#define TRAMPOLINE_SIZE 40

