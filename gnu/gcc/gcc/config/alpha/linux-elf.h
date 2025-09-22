/* Definitions of target machine for GNU compiler
   for Alpha Linux-based GNU systems using ELF.
   Copyright (C) 1996, 1997, 1998, 2001, 2002, 2003, 2006
   Free Software Foundation, Inc.
   Contributed by Richard Henderson.

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

#undef TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (Alpha GNU/Linux for ELF)");

#undef SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS \
{ "elf_dynamic_linker", ELF_DYNAMIC_LINKER },

#define GLIBC_DYNAMIC_LINKER	"/lib/ld-linux.so.2"
#define UCLIBC_DYNAMIC_LINKER "/lib/ld-uClibc.so.0"
#if UCLIBC_DEFAULT
#define CHOOSE_DYNAMIC_LINKER(G, U) "%{mglibc:%{muclibc:%e-mglibc and -muclibc used together}" G ";:" U "}"
#else
#define CHOOSE_DYNAMIC_LINKER(G, U) "%{muclibc:%{mglibc:%e-mglibc and -muclibc used together}" U ";:" G "}"
#endif
#define LINUX_DYNAMIC_LINKER \
  CHOOSE_DYNAMIC_LINKER (GLIBC_DYNAMIC_LINKER, UCLIBC_DYNAMIC_LINKER)

#define ELF_DYNAMIC_LINKER	LINUX_DYNAMIC_LINKER

#define LINK_SPEC "-m elf64alpha %{G*} %{relax:-relax}		\
  %{O*:-O3} %{!O*:-O1}						\
  %{shared:-shared}						\
  %{!shared:							\
    %{!static:							\
      %{rdynamic:-export-dynamic}				\
      %{!dynamic-linker:-dynamic-linker %(elf_dynamic_linker)}}	\
    %{static:-static}}"

#undef LIB_SPEC
#define LIB_SPEC \
"%{pthread:-lpthread} %{shared:-lc}%{!shared:%{profile:-lc_p}%{!profile:-lc}} "

#define TARGET_ASM_FILE_END file_end_indicate_exec_stack
