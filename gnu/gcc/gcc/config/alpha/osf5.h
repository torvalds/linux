/* Definitions of target machine for GNU compiler, for DEC Alpha on Tru64 5.
   Copyright (C) 2000, 2001, 2004, 2005, 2006 Free Software Foundation, Inc.

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

/* Tru64 5.1 uses IEEE QUAD format.  */
#undef TARGET_DEFAULT
#define TARGET_DEFAULT	(MASK_FPREGS | MASK_LONG_DOUBLE_128)

/* In Tru64 UNIX V5.1, Compaq introduced a new assembler
   (/usr/lib/cmplrs/cc/adu) which currently (versions between 3.04.29 and
   3.04.32) breaks mips-tfile.  Passing the undocumented -oldas flag reverts
   to using the old assembler (/usr/lib/cmplrs/cc/as[01]).

   The V5.0 and V5.0A assemblers silently ignore -oldas, so it can be
   specified here.

   It is clearly not desirable to depend on this undocumented flag, and
   Compaq wants -oldas to go away soon, but until they have released a
   new adu that works with mips-tfile, this is the only option.

   In some versions of the DTK, the assembler driver invokes ld after
   assembly.  This has been fixed in current versions, but adding -c
   works as expected for all versions.  */

#undef ASM_OLDAS_SPEC
#define ASM_OLDAS_SPEC "-oldas -c"

/* The linker appears to perform invalid code optimizations that result
   in the ldgp emitted for the exception_receiver pattern being incorrectly
   linked.  */
#undef TARGET_LD_BUGGY_LDGP
#define TARGET_LD_BUGGY_LDGP 1

/* Tru64 v5.1 has the float and long double forms of math functions.  */
#undef TARGET_C99_FUNCTIONS
#define TARGET_C99_FUNCTIONS  1

/* The native assembler doesn't understand parenthesis.  */
#define TARGET_ASM_OPEN_PAREN ""
#define TARGET_ASM_CLOSE_PAREN ""

