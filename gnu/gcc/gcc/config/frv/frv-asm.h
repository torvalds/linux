/* Assembler Support.
   Copyright (C) 2000 Free Software Foundation, Inc.
   Contributed by Red Hat, Inc.

   This file is part of GCC.

   GCC is free software ; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation * either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY ; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* P(INSN): Emit INSN.P for VLIW machines, otherwise emit plain INSN.
   P2(INSN): Emit INSN.P on the FR500 and above, otherwise emit plain INSN.  */
#ifdef __FRV_VLIW__
#ifdef __STDC__
#define P(A) A.p
#else
#define P(A) A/**/.p
#endif
#if __FRV_VLIW__ > 2
#define P2(A) P(A)
#else
#define P2(A) A
#endif
#else
#define P(A) A
#define P2(A) A
#endif

/* Add underscore if necessary to external name.  */
#ifdef __FRV_UNDERSCORE__
#ifdef __STDC__
#define EXT(NAME) _##NAME
#else
#define EXT(NAME) _/**/NAME
#endif
#else
#define EXT(NAME) NAME
#endif
