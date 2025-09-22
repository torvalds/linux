/* Definitions of target machine for GNU compiler,
   for NetBSD/vax ELF systems.
   Copyright (C) 2002 Free Software Foundation, Inc.

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

/* Names to predefine in the preprocessor for this target OS.  */
#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
      NETBSD_OS_CPP_BUILTINS_ELF();		\
    }						\
  while (0)

#undef CPP_SPEC
#define CPP_SPEC NETBSD_CPP_SPEC

#define NETBSD_ENTRY_POINT "__start"

#undef LINK_SPEC
#if 1
/* FIXME: We must link all executables statically until PIC support
   is added to the compiler.  */
#define LINK_SPEC \
  "%{assert*} %{R*} %{rpath*} \
   %{shared:%ethe -shared option is not currently supported for VAX ELF} \
   %{!shared: \
     -dc -dp \
     %{!nostdlib: \
       %{!r*: \
	 %{!e*:-e %(netbsd_entry_point)}}} \
     %{!static:-static} \
     %{static:-static}}"
#else
#define LINK_SPEC NETBSD_LINK_SPEC_ELF
#endif

#define EXTRA_SPECS				\
  { "netbsd_entry_point", NETBSD_ENTRY_POINT },

/* We use gas, not the UNIX assembler.  */
#undef TARGET_DEFAULT
#define TARGET_DEFAULT 0

