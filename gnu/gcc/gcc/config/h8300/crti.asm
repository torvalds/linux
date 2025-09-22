/* Copyright (C) 2001, 2002 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* The code in sections .init and .fini is supposed to be a single
   regular function.  The function in .init is called directly from
   start in crt0.asm.  The function in .fini is atexit()ed in crt0.asm
   too.

   crti.asm contributes the prologue of a function to these sections,
   and crtn.asm comes up the epilogue.  STARTFILE_SPEC should list
   crti.o before any other object files that might add code to .init
   or .fini sections, and ENDFILE_SPEC should list crtn.o after any
   such object files.  */

#ifdef __H8300H__
#ifdef __NORMAL_MODE__
	.h8300hn
#else
	.h8300h
#endif
#endif

#ifdef __H8300S__
#ifdef __NORMAL_MODE__
	.h8300sn
#else
	.h8300s
#endif
#endif
#ifdef __H8300SX__
#ifdef __NORMAL_MODE__
	.h8300sxn
#else
	.h8300sx
#endif
#endif

	.section .init
        .global  __init
__init:
        .section .fini
        .global  __fini
__fini:
