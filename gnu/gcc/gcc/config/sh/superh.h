/* Definitions of target machine for gcc for Super-H using sh-superh-elf.
   Copyright (C) 2001, 2006 Free Software Foundation, Inc.

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */


/* This header file is used when the vendor name is set to 'superh'.
   config.gcc already configured the compiler for SH4 only and switched
   the default endianess to little (although big endian is still available).
   This file configures the spec file to the default board configuration
   but in such a way that it can be overridden by a boardspecs file
   (using the -specs= option). This file is expected to disable the
   defaults and provide options --defsym _start and --defsym _stack
   which are required by the SuperH configuration of GNU ld.

   This file is intended to override sh.h.  */


#ifndef _SUPERH_H
#define _SUPERH_H
#endif


#undef TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (SuperH SH special %s)", __DATE__);

/* Override the linker spec strings to use the new emulation
   The specstrings are concatenated as follows
   LINK_EMUL_PREFIX.(''|'32'|'64'|LINK_DEFAULT_CPU_EMUL).SUBTARGET_LINK_EMUL_SUFFIX
*/
#undef LINK_EMUL_PREFIX
#undef SUBTARGET_LINK_EMUL_SUFFIX

#define LINK_EMUL_PREFIX "superh"
#define SUBTARGET_LINK_EMUL_SUFFIX ""

/* Add the SUBTARGET_LINK_SPEC to add the board and runtime support and
   change the endianness */
#undef SUBTARGET_LINK_SPEC
#if  TARGET_ENDIAN_DEFAULT == MASK_LITTLE_ENDIAN
#define SUBTARGET_LINK_SPEC "%(board_link) %(ldruntime) %{ml|!mb:-EL}%{mb:-EB}"
#else
#define SUBTARGET_LINK_SPEC "%(board_link) %(ldruntime) %{ml:-EL}%{mb|!ml:-EB}"
#endif


/* This is used by the link spec if the boardspecs file is not used (for whatever reason).
   If the boardspecs file overrides this then an alternative can be used. */
#undef SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS \
{ "board_link", "--defsym _start=0x1000 --defsym _stack=0x30000" }, \
{ "asruntime", "" }, \
{ "cppruntime", "-D__GDB_SIM__" }, \
{ "cc1runtime", "" }, \
{ "ldruntime", "" }, \
{ "libruntime", "-lc -lgloss" }


/* Set the SUBTARGET_CPP_SPEC to define __EMBEDDED_CROSS__ which has an effect
   on newlib and provide the runtime support */
#undef SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC \
"-D__EMBEDDED_CROSS__ %{m4-100*:-D__SH4_100__} %{m4-200*:-D__SH4_200__} %{m4-400:-D__SH4_400__} %{m4-500:-D__SH4_500__} \
%(cppruntime)"

/* Override the SUBTARGET_ASM_SPEC to add the runtime support */
#undef SUBTARGET_ASM_SPEC
#define SUBTARGET_ASM_SPEC "%{m4-100*|m4-200*:-isa=sh4} %{m4-400:-isa=sh4-nommu-nofpu} %{m4-500:-isa=sh4-nofpu} %(asruntime)"

/* Override the SUBTARGET_ASM_RELAX_SPEC so it doesn't interfere with the
   runtime support by adding -isa=sh4 in the wrong place.  */
#undef SUBTARGET_ASM_RELAX_SPEC
#define SUBTARGET_ASM_RELAX_SPEC "%{!m4-100*:%{!m4-200*:%{!m4-400:%{!m4-500:-isa=sh4}}}}"

/* Create the CC1_SPEC to add the runtime support */
#undef CC1_SPEC
#define CC1_SPEC "%(cc1runtime)"

#undef CC1PLUS_SPEC
#define CC1PLUS_SPEC "%(cc1runtime)"


/* Override the LIB_SPEC to add the runtime support */
#undef LIB_SPEC
#define LIB_SPEC "%{!shared:%{!symbolic:%(libruntime) -lc}} %{pg:-lprofile -lc}"

/* Override STARTFILE_SPEC to add profiling and MMU support.  */
#undef STARTFILE_SPEC
#define STARTFILE_SPEC \
  "%{!shared: %{!m4-400*: %{pg:gcrt1-mmu.o%s}%{!pg:crt1-mmu.o%s}}} \
   %{!shared: %{m4-400*: %{pg:gcrt1.o%s}%{!pg:crt1.o%s}}} \
   crti.o%s \
   %{!shared:crtbegin.o%s} %{shared:crtbeginS.o%s}"
