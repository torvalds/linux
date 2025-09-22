/* Support for GCC on MIPS using WindISS simulator.
   Copyright (C) 2002, 2003, 2004 Free Software Foundation, Inc.
   Contributed by CodeSourcery, LLC. 

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

#undef TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (MIPS WindISS)");

/* Combination of mips.h and svr4.h.  */
#undef  SWITCH_TAKES_ARG
#define SWITCH_TAKES_ARG(CHAR)          \
  (DEFAULT_SWITCH_TAKES_ARG (CHAR)      \
   || (CHAR) == 'G'                     \
   || (CHAR) == 'h'                     \
   || (CHAR) == 'x'                     \
   || (CHAR) == 'z')

#undef SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC \
"%{!DCPU=*: %{mips3|mips4|mips64:-DCPU=MIPS64;:-DCPU=MIPS32}} \
  %{EL|mel:-DMIPSEL;:-DMIPSEB} \
  %{msoft-float:-DSOFT_FLOAT} \
  %{mips1:-D_WRS_R3K_EXC_SUPPORT}"

#undef  ASM_SPEC
#define ASM_SPEC "\
%{!G:-G 0} %{G*} %(endian_spec) %{mips1} %{mips2} %{mips3} %{mips4} \
%{mips32} %{mips32r2} %{mips64} \
%{mips16:%{!mno-mips16:-mips16}} %{mno-mips16:-no-mips16} \
%(subtarget_asm_optimizing_spec) \
%(subtarget_asm_debugging_spec) \
%{mabi=*} %{!mabi*: %(asm_abi_default_spec)} \
%{mgp32} %{mgp64} %{march=*} %{mxgot:-xgot} \
%{mtune=*} %{v} \
%(subtarget_asm_spec)"

#undef LINK_SPEC
/* LINK_SPEC is clobbered in svr4.h. ugh!  */
#define LINK_SPEC "\
-m elf32mipswindiss \
%{!G:-G 0} %{G*} %{mips1} %{mips2} %{mips3} %{mips4} %{mips32} %{mips64} \
%{bestGnum}"

/* Diab libs MIPS{,E,F,L,M,W,X,Y,Z}{,H,N,S}
   
   .
   E - Elf (small-data/const=8
   F - Elf Far (small-data/const=0)
   L - Little Elf
   M - Little Elf Far
   W - elf32 bigmips
   X - elf32 bigmips (far?)
   Y - elf32 littlemips
   Z - elf32 littlemips (far?)

   . - Integer routines
   H - Hard float
   N - No float
   S - Soft float

   Want {F,M}{,H,S}

*/

#undef LIB_SPEC
#define LIB_SPEC "--start-group -li -lcfp -lwindiss -lram -limpl -limpfp --end-group"

#undef STARTFILE_SPEC
#define STARTFILE_SPEC "crt0.o%s crtbegin.o%s"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC "crtend.o%s"

/* We have no shared libraries.  These two shouldn't be necessary.  */
#undef LINK_SHLIB_SPEC
#define LINK_SHLIB_SPEC ""
#undef LINK_EH_SPEC
#define LINK_EH_SPEC ""

#undef CRTSAVRES_DEFAULT_SPEC
#define CRTSAVRES_DEFAULT_SPEC ""

/* No sdata.  */
#undef MIPS_DEFAULT_GVALUE
#define MIPS_DEFAULT_GVALUE 0
