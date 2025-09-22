/* Configuration file for sparc64 OpenBSD target.
   Copyright (C) 1999, 2005 Free Software Foundation, Inc.

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
#define TARGET_VERSION fprintf (stderr, " (sparc64 OpenBSD ELF)")

/* XXX - do we really want HARD_QUAD? */
#undef TARGET_DEFAULT
#define TARGET_DEFAULT \
(MASK_V9 + MASK_PTR64 + MASK_64BIT /* + MASK_HARD_QUAD */ \
 + MASK_APP_REGS + MASK_FPU + MASK_STACK_BIAS + MASK_LONG_DOUBLE_128)

#undef SPARC_DEFAULT_CMODEL
#define SPARC_DEFAULT_CMODEL CM_MEDMID

/* Target OS builtins.  */
#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
	OPENBSD_OS_CPP_BUILTINS();		\
	builtin_define ("__sparc64__");		\
	builtin_define ("__sparcv9__");		\
	builtin_define ("__sparc_v9__");	\
	builtin_define ("__arch64__");		\
    }						\
  while (0)

#undef CPP_SUBTARGET_SPEC
#define CPP_SUBTARGET_SPEC ""

#undef MD_EXEC_PREFIX
#undef MD_STARTFILE_PREFIX

#undef ASM_SPEC
#ifdef PIE_DEFAULT
#define ASM_SPEC "\
%{v:-V} -s %{fpic|fPIC:-K PIC} %{!fno-pie: %{!p: %{!pg: -K PIC}}} \
%{mlittle-endian:-EL} \
%(asm_cpu) %(asm_arch) \
"
#else
#define ASM_SPEC "\
%{v:-V} -s %{fpic|fPIC|fpie|fPIE:-K PIC} \
%{mlittle-endian:-EL} \
%(asm_cpu) %(asm_arch) \
"
#endif

/* Layout of source language data types.  */
#undef WCHAR_TYPE
#define WCHAR_TYPE "int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

#undef WINT_TYPE
#define WINT_TYPE "int"

#undef INTMAX_TYPE
#define INTMAX_TYPE "long long int"

#undef UINTMAX_TYPE
#define UINTMAX_TYPE "long long unsigned int"

#undef LONG_DOUBLE_TYPE_SIZE
#define LONG_DOUBLE_TYPE_SIZE 128

#undef JUMP_TABLES_DEFAULT
#define JUMP_TABLES_DEFAULT 0	/* incompatible with --executable-text */

#undef LINK_SPEC
#define LINK_SPEC \
  "%{!shared:%{!nostdlib:%{!r*:%{!e*:-e __start}}}} \
   %{shared:-shared} %{R*} \
   %{static:-Bstatic} \
   %{!static:-Bdynamic} \
   %{rdynamic:-export-dynamic} \
   %{assert*} \
   %{!static:%{!dynamic-linker:-dynamic-linker /usr/libexec/ld.so}}"

/* As an elf system, we need crtbegin/crtend stuff.  */
#undef STARTFILE_SPEC
#define STARTFILE_SPEC "\
	%{!shared: %{pg:gcrt0%O%s} %{!pg:%{p:gcrt0%O%s} \
	%{!p:%{!static:crt0%O%s} %{static:%{nopie:crt0%O%s} \
	%{!nopie:rcrt0%O%s}}}} crtbegin%O%s} %{shared:crtbeginS%O%s}"
#undef ENDFILE_SPEC
#define ENDFILE_SPEC "%{!shared:crtend%O%s} %{shared:crtendS%O%s}"

/* We use GNU ld so undefine this so that attribute((init_priority)) works.  */
#undef CTORS_SECTION_ASM_OP
#undef DTORS_SECTION_ASM_OP
