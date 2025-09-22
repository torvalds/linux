/* Definitions for SH running OpenBSD using ELF
   Copyright (C) 2002 - 2006 Free Software Foundation, Inc.
   Adapted from the NetBSD configuration contributed by Wasabi Systems, Inc.

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
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* Get generic OpenBSD definitions. */
#include <openbsd.h>

#define TARGET_VERSION_ENDIAN "le"
#define TARGET_VERSION_CPU "sh"

/* Enable DWARF 2 exceptions.  */
#undef DWARF2_UNWIND_INFO
#define DWARF2_UNWIND_INFO 1

#undef TARGET_CPU_DEFAULT
#define TARGET_CPU_DEFAULT SELECT_SH4

#undef TARGET_DEFAULT
#define TARGET_DEFAULT \
  (TARGET_CPU_DEFAULT | MASK_USERMODE | TARGET_ENDIAN_DEFAULT)

#define TARGET_OS_CPP_BUILTINS()	OPENBSD_OS_CPP_BUILTINS_ELF()

/* Layout of source language data types */

/* This must agree with <machine/_types.h> */
#undef SIZE_TYPE
#define SIZE_TYPE "long unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "long int"

#undef INTMAX_TYPE
#define INTMAX_TYPE "long long int"

#undef UINTMAX_TYPE
#define UINTMAX_TYPE "long long unsigned int"

#undef WCHAR_TYPE
#define WCHAR_TYPE "int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

#undef LINK_DEFAULT_CPU_EMUL
#define LINK_DEFAULT_CPU_EMUL ""

#undef SUBTARGET_LINK_EMUL_SUFFIX
#define SUBTARGET_LINK_EMUL_SUFFIX "_obsd"

#undef LINK_EMUL_PREFIX
#define LINK_EMUL_PREFIX "sh%{!mb:l}elf"

#undef SUBTARGET_LINK_SPEC
#ifdef OBSD_NO_DYNAMIC_LIBRARIES
#define SUBTARGET_LINK_SPEC \
  "%{g:%{!nostdlib:-L/usr/lib/debug}} %{!nostdlib:%{!r*:%{!e*:-e __start}}} -dc -dp %{assert*}"
#else
#define SUBTARGET_LINK_SPEC \
  "%{g:%{!nostdlib:-L/usr/lib/debug}} \
   %{!shared:%{!nostdlib:%{!r*:%{!e*:-e __start}}}} \
   %{shared:-Bshareable -x} -dc -dp %{R*} \
   %{static:-Bstatic} \
   %{rdynamic:-export-dynamic} \
   %{assert*} \
   %{!static:%{!dynamic-linker:-dynamic-linker /usr/libexec/ld.so}}"
#endif


#undef LINK_SPEC
#define LINK_SPEC SH_LINK_SPEC

/* As an elf system, we need crtbegin/crtend stuff.  */
#undef STARTFILE_SPEC
#define STARTFILE_SPEC "\
	%{!shared: %{pg:gcrt0%O%s} %{!pg:%{p:gcrt0%O%s} \
	%{!p:%{!static:crt0%O%s} %{static:%{nopie:crt0%O%s} \
	%{!nopie:rcrt0%O%s}}}} crtbegin%O%s} %{shared:crtbeginS%O%s}"
#undef ENDFILE_SPEC
#define ENDFILE_SPEC "%{!shared:crtend%O%s} %{shared:crtendS%O%s}"

/* Needed for ELF (inspired by netbsd-elf).  */
#undef LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX	"."

/* Provide a CPP_SPEC appropriate for OpenBSD.  */
#undef SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC OBSD_CPP_SPEC

/* Define because we use the label and we do not need them. */
#define NO_PROFILE_COUNTERS 1
 
#undef FUNCTION_PROFILER
#define FUNCTION_PROFILER(STREAM,LABELNO)				\
do									\
  {									\
    if (TARGET_SHMEDIA32 || TARGET_SHMEDIA64)				\
      {									\
	/* FIXME */							\
	sorry ("unimplemented-shmedia profiling");			\
      }									\
    else								\
      {									\
        fprintf((STREAM), "\tmov.l\t%sLP%d,r1\n",			\
                LOCAL_LABEL_PREFIX, (LABELNO));				\
        fprintf((STREAM), "\tmova\t%sLP%dr,r0\n",			\
                LOCAL_LABEL_PREFIX, (LABELNO));				\
        fprintf((STREAM), "\tjmp\t@r1\n");				\
        fprintf((STREAM), "\tnop\n");					\
        fprintf((STREAM), "\t.align\t2\n");				\
        fprintf((STREAM), "%sLP%d:\t.long\t__mcount\n",			\
                LOCAL_LABEL_PREFIX, (LABELNO));				\
        fprintf((STREAM), "%sLP%dr:\n", LOCAL_LABEL_PREFIX, (LABELNO));	\
      }									\
  }									\
while (0)

/* Since libgcc is compiled with -fpic for this target, we can't use
   __sdivsi3_1 as the division strategy for -O0 and -Os.  */
#undef SH_DIV_STRATEGY_DEFAULT
#define SH_DIV_STRATEGY_DEFAULT SH_DIV_CALL2
#undef SH_DIV_STR_FOR_SIZE
#define SH_DIV_STR_FOR_SIZE "call2"
