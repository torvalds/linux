/* Definitions of target machine for gcc for Renesas / SuperH SH using ELF.
   Copyright (C) 1996, 1997, 2000, 2001, 2002, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Ian Lance Taylor <ian@cygnus.com>.

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

/* Let sh.c know this is ELF.  */
#undef TARGET_ELF
#define TARGET_ELF 1

/* Generate DWARF2 debugging information and make it the default */
#define DWARF2_DEBUGGING_INFO 1

#undef PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG

/* use a more compact format for line information */
#define DWARF2_ASM_LINE_DEBUG_INFO 1

/* WCHAR_TYPE / WCHAR_TYPE_SIZE are defined to long int / BITS_PER_WORD in
   svr4.h, but these work out as 64 bit for shmedia64.  */
#undef WCHAR_TYPE
/* #define WCHAR_TYPE (TARGET_SH5 ? "int" : "long int") */
#define WCHAR_TYPE SH_ELF_WCHAR_TYPE
   
#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32


/* The prefix to add to user-visible assembler symbols.  */

#undef LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX "."

#undef SIZE_TYPE
#define SIZE_TYPE (TARGET_SH5 ? "long unsigned int" : "unsigned int")

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE (TARGET_SH5 ? "long int" : "int")

/* Pass -ml and -mrelax to the assembler and linker.  */
#undef ASM_SPEC
#define ASM_SPEC SH_ASM_SPEC

#undef LINK_SPEC
#define LINK_SPEC SH_LINK_SPEC
#undef LINK_EMUL_PREFIX
#if TARGET_ENDIAN_DEFAULT == MASK_LITTLE_ENDIAN
#define LINK_EMUL_PREFIX "sh%{!mb:l}elf"
#else
#define LINK_EMUL_PREFIX "sh%{ml:l}elf"
#endif

/* svr4.h undefined DBX_REGISTER_NUMBER, so we need to define it
   again.  */
#define DBX_REGISTER_NUMBER(REGNO) SH_DBX_REGISTER_NUMBER (REGNO)

#undef ASM_GENERATE_INTERNAL_LABEL
#define ASM_GENERATE_INTERNAL_LABEL(STRING, PREFIX, NUM) \
  sprintf ((STRING), "*%s%s%ld", LOCAL_LABEL_PREFIX, (PREFIX), (long)(NUM))

#define DBX_LINES_FUNCTION_RELATIVE 1
#define DBX_OUTPUT_NULL_N_SO_AT_MAIN_SOURCE_FILE_END

#undef STARTFILE_SPEC
#define STARTFILE_SPEC \
  "%{!shared: crt1.o%s} crti.o%s \
   %{!shared:crtbegin.o%s} %{shared:crtbeginS.o%s}"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC \
  "%{!shared:crtend.o%s} %{shared:crtendS.o%s} crtn.o%s"

/* ASM_OUTPUT_CASE_LABEL is defined in elfos.h.  With it,
   a redundant .align was generated.  */
#undef  ASM_OUTPUT_CASE_LABEL
