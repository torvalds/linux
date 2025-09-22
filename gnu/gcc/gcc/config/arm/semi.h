/* Definitions of target machine for GNU compiler.  ARM on semi-hosted platform
   Copyright (C) 1994, 1995, 1996, 1997, 2001, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Richard Earnshaw (richard.earnshaw@arm.com)

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
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#define STARTFILE_SPEC  "crt0.o%s"

#ifndef LIB_SPEC
#define LIB_SPEC "-lc"
#endif

#ifndef SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC "-D__semi__"
#endif

#ifndef LINK_SPEC
#define LINK_SPEC "%{mbig-endian:-EB} -X"
#endif

#ifndef TARGET_VERSION
#define TARGET_VERSION fputs (" (ARM/semi-hosted)", stderr);
#endif

#ifndef TARGET_DEFAULT_FLOAT_ABI
#define TARGET_DEFAULT_FLOAT_ABI ARM_FLOAT_ABI_HARD
#endif

#ifndef TARGET_DEFAULT
#define TARGET_DEFAULT (MASK_APCS_FRAME)
#endif

#ifndef SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS \
  { "subtarget_extra_asm_spec",	SUBTARGET_EXTRA_ASM_SPEC },
#endif

#ifndef SUBTARGET_EXTRA_ASM_SPEC
#define SUBTARGET_EXTRA_ASM_SPEC ""
#endif

/* The compiler supports PIC code generation, even though the binutils
   may not.  If we are asked to compile position independent code, we
   always pass -k to the assembler.  If it doesn't recognize it, then
   it will barf, which probably means that it doesn't know how to
   assemble PIC code.  This is what we want, since otherwise tools
   may incorrectly assume we support PIC compilation even if the
   binutils can't.  */
#ifndef ASM_SPEC
#define ASM_SPEC "\
%{fpic|fpie: -k} %{fPIC|fPIE: -k} \
%{mbig-endian:-EB} \
%{mcpu=*:-mcpu=%*} \
%{march=*:-march=%*} \
%{mapcs-float:-mfloat} \
%{msoft-float:-mfloat-abi=soft} %{mhard-float:-mfloat-abi=hard} \
%{mfloat-abi=*} %{mfpu=*} \
%{mthumb-interwork:-mthumb-interwork} \
%(subtarget_extra_asm_spec)"
#endif
