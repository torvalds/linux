/* Definitions of target machine for GNU compiler.
   Renesas H8/300 version generating elf
   Copyright (C) 2001, 2002, 2003, 2004 Free Software Foundation, Inc.
   Contributed by Steve Chamberlain (sac@cygnus.com),
   Jim Wilson (wilson@cygnus.com), and Doug Evans (dje@cygnus.com).

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

#ifndef GCC_H8300_ELF_H
#define GCC_H8300_ELF_H

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC "crtend.o%s %{pg:gcrtn.o%s}%{!pg:crtn.o%s}"

#undef	STARTFILE_SPEC
#define STARTFILE_SPEC "%{!shared: \
			 %{!symbolic: \
			  %{pg:gcrt0.o%s}%{!pg:%{p:mcrt0.o%s}%{!p:crt0.o%s}}}}\
			%{pg:gcrti.o%s}%{!pg:crti.o%s} \
			crtbegin.o%s"

#undef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX "_"

#define JUMP_TABLES_IN_TEXT_SECTION (flag_pic)

#undef LINK_SPEC
#define LINK_SPEC "%{mh:%{mn:-m h8300hnelf}} %{mh:%{!mn:-m h8300helf}} %{ms:%{mn:-m h8300snelf}} %{ms:%{!mn:-m h8300self}} %{msx:%{mn:-m h8300sxnelf;:-m h8300sxelf}}"

#endif /* h8300/elf.h */
