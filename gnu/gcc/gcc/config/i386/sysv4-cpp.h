/* Target definitions for GCC for Intel 80386 running System V.4
   Copyright (C) 1991, 2001, 2002 Free Software Foundation, Inc.

   Written by Ron Guilmette (rfg@netcom.com).

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

#define TARGET_OS_CPP_BUILTINS()					\
  do									\
    {									\
	builtin_define_std ("unix");					\
	builtin_define ("__svr4__");					\
	builtin_assert ("system=unix");					\
	builtin_assert ("system=svr4");					\
    }									\
  while (0)

