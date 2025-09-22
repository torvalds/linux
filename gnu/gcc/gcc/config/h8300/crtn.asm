/* Copyright (C) 2001 Free Software Foundation, Inc.
   This file was adapted from glibc sources.

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

/* See an explanation about .init and .fini in crti.asm.  */

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
	rts

	.section .fini
	rts
