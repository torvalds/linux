/* OS independent definitions for AMD x86-64.
   Copyright (C) 2001, 2005 Free Software Foundation, Inc.
   Contributed by Bo Thorsen <bo@suse.de>.

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

#undef ASM_COMMENT_START
#define ASM_COMMENT_START "#"

#undef DBX_REGISTER_NUMBER
#define DBX_REGISTER_NUMBER(n) \
  (TARGET_64BIT ? dbx64_register_map[n] : svr4_dbx_register_map[n])

/* Output assembler code to FILE to call the profiler.  */
#define NO_PROFILE_COUNTERS 1

#undef MCOUNT_NAME
#define MCOUNT_NAME "mcount"

#undef SIZE_TYPE
#define SIZE_TYPE (TARGET_64BIT ? "long unsigned int" : "unsigned int")

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE (TARGET_64BIT ? "long int" : "int")

#undef WCHAR_TYPE
#define WCHAR_TYPE "int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

#undef CC1_SPEC
#define CC1_SPEC "%(cc1_cpu) %{profile:-p}"

#undef ASM_SPEC
#define ASM_SPEC "%{v:-V} %{Qy:} %{!Qn:-Qy} %{n} %{T} %{Ym,*} %{Yd,*} \
 %{Wa,*:%*} %{m32:--32} %{m64:--64}"

#undef ASM_OUTPUT_ALIGNED_BSS
#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN) \
  x86_output_aligned_bss (FILE, DECL, NAME, SIZE, ALIGN)

#undef  ASM_OUTPUT_ALIGNED_COMMON
#define ASM_OUTPUT_ALIGNED_COMMON(FILE, NAME, SIZE, ALIGN)		\
  x86_elf_aligned_common (FILE, NAME, SIZE, ALIGN);

/* This is used to align code labels according to Intel recommendations.  */

#ifdef HAVE_GAS_MAX_SKIP_P2ALIGN
#define ASM_OUTPUT_MAX_SKIP_ALIGN(FILE,LOG,MAX_SKIP)			\
  do {									\
    if ((LOG) != 0) {							\
      if ((MAX_SKIP) == 0) fprintf ((FILE), "\t.p2align %d\n", (LOG));	\
      else fprintf ((FILE), "\t.p2align %d,,%d\n", (LOG), (MAX_SKIP));	\
    }									\
  } while (0)
#endif


/* i386 System V Release 4 uses DWARF debugging info.
   x86-64 ABI specifies DWARF2.  */

#define DWARF2_DEBUGGING_INFO 1
#define DWARF2_UNWIND_INFO 1

#undef PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG

#undef TARGET_ASM_SELECT_SECTION
#define TARGET_ASM_SELECT_SECTION  x86_64_elf_select_section

#undef TARGET_ASM_UNIQUE_SECTION
#define TARGET_ASM_UNIQUE_SECTION  x86_64_elf_unique_section
