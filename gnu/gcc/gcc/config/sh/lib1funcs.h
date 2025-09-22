/* Copyright (C) 1994, 1995, 1997, 1998, 1999, 2000, 2001, 2002, 2003,
   2004, 2005, 2006
   Free Software Foundation, Inc.

This file is free software; you can redistribute it and/or modify it
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

This file is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#ifdef __ELF__
#define LOCAL(X)	.L_##X
#define FUNC(X)		.type X,@function
#define HIDDEN_FUNC(X)	FUNC(X); .hidden X
#define HIDDEN_ALIAS(X,Y) ALIAS (X,Y); .hidden GLOBAL(X)
#define ENDFUNC0(X)	.Lfe_##X: .size X,.Lfe_##X-X
#define ENDFUNC(X)	ENDFUNC0(X)
#else
#define LOCAL(X)	L_##X
#define FUNC(X)
#define HIDDEN_FUNC(X)
#define HIDDEN_ALIAS(X,Y) ALIAS (X,Y)
#define ENDFUNC(X)
#endif

#define	CONCAT(A,B)	A##B
#define	GLOBAL0(U,X)	CONCAT(U,__##X)
#define	GLOBAL(X)	GLOBAL0(__USER_LABEL_PREFIX__,X)

#define ALIAS(X,Y)	.global GLOBAL(X); .set GLOBAL(X),GLOBAL(Y)

#ifdef __SH2A__
#undef FMOVD_WORKS
#define FMOVD_WORKS
#endif

#ifdef __LITTLE_ENDIAN__
#define DR00 fr1
#define DR01 fr0
#define DR20 fr3
#define DR21 fr2
#define DR40 fr5
#define DR41 fr4
#else /* !__LITTLE_ENDIAN__ */
#define DR00 fr0
#define DR01 fr1
#define DR20 fr2
#define DR21 fr3
#define DR40 fr4
#define DR41 fr5
#endif /* !__LITTLE_ENDIAN__ */

#ifdef __sh1__
#define SL(branch, dest, in_slot, in_slot_arg2) \
	in_slot, in_slot_arg2; branch dest
#define SL1(branch, dest, in_slot) \
	in_slot; branch dest
#else /* ! __sh1__ */
#define SL(branch, dest, in_slot, in_slot_arg2) \
	branch##.s dest; in_slot, in_slot_arg2
#define SL1(branch, dest, in_slot) \
	branch##/s dest; in_slot
#endif /* !__sh1__ */
