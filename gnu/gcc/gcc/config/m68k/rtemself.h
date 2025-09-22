/* Definitions for rtems targeting a Motorola m68k using elf.
   Copyright (C) 1999, 2000, 2002 National Research Council of Canada.
   Contributed by Charles-Antoine Gauthier (charles.gauthier@nrc.ca).

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


/* Target OS builtins.  */
#undef TARGET_OS_CPP_BUILTINS	/* Defined in m68kemb.h.  */
#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
	builtin_define_std ("mc68000");		\
	builtin_define ("__USE_INIT_FINI__");	\
	builtin_define ("__rtems__");		\
	builtin_assert ("system=rtems");	\
    }						\
  while (0)
