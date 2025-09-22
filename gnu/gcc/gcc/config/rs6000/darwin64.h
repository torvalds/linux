/* Target definitions for PowerPC running Darwin (Mac OS X).
   Copyright (C) 2006 Free Software Foundation, Inc.
   Contributed by Apple Computer Inc.

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
   along with GCC; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#undef  TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (Darwin/PowerPC64)");

#undef  TARGET_DEFAULT
#define TARGET_DEFAULT (MASK_POWERPC | MASK_POWERPC64 | MASK_64BIT \
			| MASK_MULTIPLE	| MASK_NEW_MNEMONICS | MASK_PPC_GFXOPT)

#undef DARWIN_ARCH_SPEC
#define DARWIN_ARCH_SPEC "ppc64"

#undef DARWIN_SUBARCH_SPEC
#define DARWIN_SUBARCH_SPEC DARWIN_ARCH_SPEC

#undef DARWIN_CRT2_SPEC
#define DARWIN_CRT2_SPEC ""
