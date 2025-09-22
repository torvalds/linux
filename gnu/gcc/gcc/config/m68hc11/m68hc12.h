/* Definitions of target machine for GNU compiler, for m68hc12.
   Copyright (C) 1999, 2000, 2001, 2003 Free Software Foundation, Inc.
   Contributed by Stephane Carrez (stcarrez@nerim.fr).

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

/* Compile and assemble for a 68hc12 unless there is a -m68hc11 option.  */
#define ASM_SPEC                                                \
"%{m68hc11:-m68hc11}"                                           \
"%{m68hcs12:-m68hcs12}"                                         \
"%{!m68hc11:%{!m68hcs12:-m68hc12}}"
#define LIB_SPEC       ""
#define CC1_SPEC       ""

/* We need to tell the linker the target elf format.  Just pass an
   emulation option.  This can be overridden by -Wl option of gcc.  */
#define LINK_SPEC                                               \
"%{m68hc11:-m m68hc11elf}"                                      \
"%{m68hcs12:-m m68hc12elf}"                                     \
"%{!m68hc11:%{!m68hcs12:-m m68hc11elf}} %{mrelax:-relax}"

#define CPP_SPEC  \
"%{mshort:-D__HAVE_SHORT_INT__ -D__INT__=16}\
 %{!mshort:-D__INT__=32}\
 %{m68hc11:-Dmc6811 -DMC6811 -Dmc68hc11}\
 %{!m68hc11:%{!m68hc12:-Dmc6812 -DMC6812 -Dmc68hc12}}\
 %{m68hcs12:-Dmc6812 -DMC6812 -Dmc68hcs12}\
 %{fshort-double:-D__HAVE_SHORT_DOUBLE__}"

/* Default target_flags if no switches specified.  */
#define TARGET_DEFAULT		(MASK_M6812)
