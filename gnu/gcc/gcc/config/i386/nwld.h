/* nwld.h -- defines to be used when targeting GCC for some generic NetWare
   system while using the Novell linker.
   Copyright (C) 2004 Free Software Foundation, Inc.

   Written by Jan Beulich (jbeulich@novell.com)

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

#undef	LIB_SPEC
#define LIB_SPEC "-lc --def-file libc.def%s"

#undef	LIBGCC_SPEC
#define LIBGCC_SPEC "-lgcc %{!static-libgcc:--def-file libgcc.def%s}"

#undef  LINKER_NAME
#define LINKER_NAME "nwld"

#undef  LINK_SPEC
#define LINK_SPEC "--format:NLM --extensions:GNU" \
	" %{static:%{!nostdlib:%{!nodefaultlib:%eStatic linking is not supported.\n}}}"

#undef  LINK_GCC_C_SEQUENCE_SPEC
#define LINK_GCC_C_SEQUENCE_SPEC "%L %G"

/* In order to permit the linker to derive the output filename from the first
   input file, put the common startup code as the last object. */
#undef	STARTFILE_SPEC
#define STARTFILE_SPEC ""

#undef	ENDFILE_SPEC
#define ENDFILE_SPEC "crt0%O%s ../imports/%{!posix:libc}%{posix:posix}pre.gcc%O%s" \
	" --def-file %{!posix:libc}%{posix:posix}pre.def%s"

#define DRIVER_SELF_SPECS "%{!static-libgcc:-shared-libgcc}"

#define TARGET_SUB_SECTION_SEPARATOR "$"

void nwld_named_section_asm_out_constructor (rtx, int);
void nwld_named_section_asm_out_destructor (rtx, int);

#define TARGET_ASM_CONSTRUCTOR nwld_named_section_asm_out_constructor
#define TARGET_ASM_DESTRUCTOR  nwld_named_section_asm_out_destructor

#undef  EH_FRAME_SECTION_NAME
#define EH_FRAME_SECTION_NAME ".eh_frame"TARGET_SUB_SECTION_SEPARATOR

/* nwld does not currently support stabs debug info */
#undef DBX_DEBUGGING_INFO
