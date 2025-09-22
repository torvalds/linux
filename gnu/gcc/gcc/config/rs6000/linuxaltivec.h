/* Definitions of target machine for GNU compiler,
   for AltiVec enhanced PowerPC machines running GNU/Linux.
   Copyright (C) 2001, 2003 Free Software Foundation, Inc.
   Contributed by Aldy Hernandez (aldyh@redhat.com).

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
#define TARGET_VERSION fprintf (stderr, " (PowerPC AltiVec GNU/Linux)");

/* Override rs6000.h and sysv4.h definition.  */
#undef	TARGET_DEFAULT
#define	TARGET_DEFAULT (MASK_POWERPC | MASK_NEW_MNEMONICS | MASK_ALTIVEC)

#undef  SUBSUBTARGET_OVERRIDE_OPTIONS
#define SUBSUBTARGET_OVERRIDE_OPTIONS rs6000_altivec_abi = 1
