/* Definitions for StrongARM running FreeBSD using the ELF format
   Copyright (C) 2001, 2004 Free Software Foundation, Inc.
   Contributed by David E. O'Brien <obrien@FreeBSD.org> and BSDi.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */


#undef  SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS \
  { "fbsd_dynamic_linker", FBSD_DYNAMIC_LINKER }

#undef  SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC FBSD_CPP_SPEC

#undef	LINK_SPEC
#define LINK_SPEC "							\
  %{p:%nconsider using `-pg' instead of `-p' with gprof(1) }		\
  %{v:-V}								\
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

/* arm.h gets this wrong for FreeBSD.  We use the GCC defaults instead.  */

#undef  SIZE_TYPE
#define SIZE_TYPE	"unsigned int"

#undef  PTRDIFF_TYPE
#define PTRDIFF_TYPE	"int"

/* We use the GCC defaults here.  */
#undef WCHAR_TYPE

#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

#undef  SUBTARGET_CPU_DEFAULT
#define SUBTARGET_CPU_DEFAULT	TARGET_CPU_strongarm

#undef  TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (FreeBSD/StrongARM ELF)");
