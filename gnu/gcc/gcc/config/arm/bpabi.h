/* Configuration file for ARM BPABI targets.
   Copyright (C) 2004, 2005
   Free Software Foundation, Inc.
   Contributed by CodeSourcery, LLC   

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

/* Use the AAPCS ABI by default.  */
#define ARM_DEFAULT_ABI ARM_ABI_AAPCS

/* Assume that AAPCS ABIs should adhere to the full BPABI.  */ 
#define TARGET_BPABI (TARGET_AAPCS_BASED)

/* BPABI targets use EABI frame unwinding tables.  */
#define TARGET_UNWIND_INFO 1

/* Section 4.1 of the AAPCS requires the use of VFP format.  */
#undef FPUTYPE_DEFAULT
#define FPUTYPE_DEFAULT FPUTYPE_VFP

/* EABI targets should enable interworking by default.  */
#undef TARGET_DEFAULT
#define TARGET_DEFAULT MASK_INTERWORK

/* The ARM BPABI functions return a boolean; they use no special
   calling convention.  */
#define FLOAT_LIB_COMPARE_RETURNS_BOOL(MODE, COMPARISON) TARGET_BPABI

/* The BPABI integer comparison routines return { -1, 0, 1 }.  */
#define TARGET_LIB_INT_CMP_BIASED !TARGET_BPABI

/* Tell the assembler to build BPABI binaries.  */
#undef SUBTARGET_EXTRA_ASM_SPEC
#define SUBTARGET_EXTRA_ASM_SPEC "%{mabi=apcs-gnu|mabi=atpcs:-meabi=gnu;:-meabi=4}"

/* The generic link spec in elf.h does not support shared libraries.  */
#undef LINK_SPEC
#define LINK_SPEC "%{mbig-endian:-EB} %{mlittle-endian:-EL} "		\
  "%{static:-Bstatic} %{shared:-shared} %{symbolic:-Bsymbolic} "	\
  "-X"

#if defined (__thumb__)
#define RENAME_LIBRARY_SET ".thumb_set"
#else
#define RENAME_LIBRARY_SET ".set"
#endif

/* Make __aeabi_AEABI_NAME an alias for __GCC_NAME.  */
#define RENAME_LIBRARY(GCC_NAME, AEABI_NAME)		\
  __asm__ (".globl\t__aeabi_" #AEABI_NAME "\n"		\
	   RENAME_LIBRARY_SET "\t__aeabi_" #AEABI_NAME 	\
	     ", __" #GCC_NAME "\n");

/* Give some libgcc functions an additional __aeabi name.  */
#ifdef L_muldi3
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY (muldi3, lmul)
#endif
#ifdef L_muldi3
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY (muldi3, lmul)
#endif
#ifdef L_fixdfdi
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY (fixdfdi, d2lz)
#endif
#ifdef L_fixunsdfdi
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY (fixunsdfdi, d2ulz)
#endif
#ifdef L_fixsfdi
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY (fixsfdi, f2lz)
#endif
#ifdef L_fixunssfdi
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY (fixunssfdi, f2ulz)
#endif
#ifdef L_floatdidf
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY (floatdidf, l2d)
#endif
#ifdef L_floatdisf
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY (floatdisf, l2f)
#endif

/* The BPABI requires that we always use an out-of-line implementation
   of RTTI comparison, even if the target supports weak symbols,
   because the same object file might be used on a target that does
   not support merging symbols across DLL boundaries.  This macro is
   broken out separately so that it can be used within
   TARGET_OS_CPP_BUILTINS in configuration files for systems based on
   the BPABI.  */
#define TARGET_BPABI_CPP_BUILTINS()			\
  do							\
    {							\
      builtin_define ("__GXX_MERGED_TYPEINFO_NAMES=0");	\
    }							\
  while (false)

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS() \
  TARGET_BPABI_CPP_BUILTINS()

/* The BPABI specifies the use of .{init,fini}_array.  Therefore, we
   do not want GCC to put anything into the .{init,fini} sections.  */
#undef INIT_SECTION_ASM_OP
#undef FINI_SECTION_ASM_OP
#define INIT_ARRAY_SECTION_ASM_OP ARM_EABI_CTORS_SECTION_OP
#define FINI_ARRAY_SECTION_ASM_OP ARM_EABI_DTORS_SECTION_OP
