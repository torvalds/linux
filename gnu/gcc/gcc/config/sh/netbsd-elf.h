/* Definitions for SH running NetBSD using ELF
   Copyright (C) 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Wasabi Systems, Inc.

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

/* Run-time Target Specification.  */
#if TARGET_ENDIAN_DEFAULT == MASK_LITTLE_ENDIAN
#define TARGET_VERSION_ENDIAN "le"
#else
#define TARGET_VERSION_ENDIAN ""
#endif

#if TARGET_CPU_DEFAULT & MASK_SH5
#if TARGET_CPU_DEFAULT & MASK_SH_E
#define TARGET_VERSION_CPU "sh5"
#else
#define TARGET_VERSION_CPU "sh64"
#endif /* MASK_SH_E */
#else
#define TARGET_VERSION_CPU "sh"
#endif /* MASK_SH5 */

#undef TARGET_VERSION
#define TARGET_VERSION	fprintf (stderr, " (NetBSD/%s%s ELF)",		\
                                 TARGET_VERSION_CPU, TARGET_VERSION_ENDIAN)


/* Extra specs needed for NetBSD SuperH ELF targets.  */

#undef SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS						\
  { "netbsd_entry_point", NETBSD_ENTRY_POINT },


#define TARGET_OS_CPP_BUILTINS()					\
  do									\
    {									\
      NETBSD_OS_CPP_BUILTINS_ELF();					\
      builtin_define ("__NO_LEADING_UNDERSCORES__");			\
    }									\
  while (0)

/* Provide a LINK_SPEC appropriate for a NetBSD/sh ELF target.
   We use the SH_LINK_SPEC from sh/sh.h, and define the appropriate
   SUBTARGET_LINK_SPEC that pulls in what we need from a generic
   NetBSD ELF LINK_SPEC.  */

/* LINK_EMUL_PREFIX from sh/elf.h */

#undef SUBTARGET_LINK_EMUL_SUFFIX
#define SUBTARGET_LINK_EMUL_SUFFIX "_nbsd"

#undef SUBTARGET_LINK_SPEC
#define SUBTARGET_LINK_SPEC NETBSD_LINK_SPEC_ELF

#undef LINK_SPEC
#define LINK_SPEC SH_LINK_SPEC

#define NETBSD_ENTRY_POINT "__start"

/* Provide a CPP_SPEC appropriate for NetBSD.  */
#undef SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC NETBSD_CPP_SPEC

#undef TARGET_DEFAULT
#define TARGET_DEFAULT \
  (TARGET_CPU_DEFAULT | MASK_USERMODE | TARGET_ENDIAN_DEFAULT)

/* Define because we use the label and we do not need them.  */
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
