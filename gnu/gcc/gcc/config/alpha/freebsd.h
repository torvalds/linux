/* Definitions for DEC Alpha/AXP running FreeBSD using the ELF format
   Copyright (C) 2000, 2002, 2004, 2005 Free Software Foundation, Inc.
   Contributed by David E. O'Brien <obrien@FreeBSD.org> and BSDi.

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


#undef  SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS \
  { "fbsd_dynamic_linker", FBSD_DYNAMIC_LINKER }

/* Provide a CPP_SPEC appropriate for FreeBSD/alpha.  Besides the
   dealing with the GCC option `-posix', we must deal with the Alpha's
   FP issues.  */

#undef  CPP_SPEC
#define CPP_SPEC "%(cpp_subtarget) %{posix:-D_POSIX_SOURCE}"

#define LINK_SPEC "%{G*} %{relax:-relax}				\
  %{p:%nconsider using `-pg' instead of `-p' with gprof(1)}		\
  %{Wl,*:%*}								\
  %{assert*} %{R*} %{rpath*} %{defsym*}					\
  %{shared:-Bshareable %{h*} %{soname*}}				\
  %{!shared:								\
    %{!static:								\
      %{rdynamic:-export-dynamic}					\
      %{!dynamic-linker:-dynamic-linker %(fbsd_dynamic_linker) }}	\
    %{static:-Bstatic}}							\
  %{symbolic:-Bsymbolic}"


/************************[  Target stuff  ]***********************************/

/* Define the actual types of some ANSI-mandated types.  
   Needs to agree with <machine/ansi.h>.  GCC defaults come from c-decl.c,
   c-common.c, and config/<arch>/<arch>.h.  */

/* alpha.h gets this wrong for FreeBSD.  We use the GCC defaults instead.  */
#undef WCHAR_TYPE

#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE	32

#undef  TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (FreeBSD/alpha ELF)");

#define TARGET_ELF	1

#undef  TARGET_DEFAULT
#define TARGET_DEFAULT	(MASK_FPREGS | MASK_GAS)

#undef HAS_INIT_SECTION

/* Show that we need a GP when profiling.  */
#undef  TARGET_PROFILING_NEEDS_GP
#define TARGET_PROFILING_NEEDS_GP 1

/* This is the char to use for continuation (in case we need to turn
   continuation back on).  */

#undef  DBX_CONTIN_CHAR
#define DBX_CONTIN_CHAR	'?'

/* Don't default to pcc-struct-return, we want to retain compatibility with
   older FreeBSD releases AND pcc-struct-return may not be reentrant.  */

#undef  DEFAULT_PCC_STRUCT_RETURN
#define DEFAULT_PCC_STRUCT_RETURN 0
