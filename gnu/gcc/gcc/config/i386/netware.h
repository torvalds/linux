/* Core target definitions for GCC for Intel 80x86 running Netware.
   and using dwarf for the debugging format.
   Copyright (C) 1993, 1994, 2004 Free Software Foundation, Inc.

   Written by David V. Henkel-Wallace (gumby@cygnus.com)

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

#define TARGET_VERSION fprintf (stderr, " (x86 NetWare)");

#undef  CPP_SPEC
#define CPP_SPEC "%{posix:-D_POSIX_SOURCE} %{pthread:-D_REENTRANT}"

#undef	LIB_SPEC
#define LIB_SPEC ""

/* Kinda useless, but what the hell */
#undef	LINK_SPEC
#define LINK_SPEC "%{h*} %{V} %{v:%{!V:-V}} \
		   %{b} \
		   %{Qy:} %{!Qn:-Qy}"

#undef	STARTFILE_SPEC
#define STARTFILE_SPEC ""

#undef	ENDFILE_SPEC
#define ENDFILE_SPEC ""

#undef	RELATIVE_PREFIX_NOT_LINKDIR
#undef	LIBGCC_SPEC

#define TARGET_OS_CPP_BUILTINS()					\
  do									\
    {									\
	builtin_define_std ("IAPX386");					\
	builtin_define ("_M_IX86=300");					\
	builtin_define ("__netware__");					\
	builtin_assert ("system=netware");				\
	builtin_define ("__ELF__");					\
	builtin_define ("__cdecl=__attribute__((__cdecl__))");		\
	builtin_define ("__stdcall=__attribute__((__stdcall__))");	\
	builtin_define ("__fastcall=__attribute__((__fastcall__))");	\
	if (!flag_iso)							\
	  {								\
	    builtin_define ("_cdecl=__attribute__((__cdecl__))");	\
	    builtin_define ("_stdcall=__attribute__((__stdcall__))");	\
	    builtin_define ("_fastcall=__attribute__((__fastcall__))");	\
	  }								\
    }									\
  while (0)

#undef TARGET_CPU_DEFAULT
#define TARGET_CPU_DEFAULT TARGET_CPU_DEFAULT_pentium4

/* By default, target has a 80387, uses IEEE compatible arithmetic,
   returns float values in the 387, and uses MSVC bit field layout. */
#undef TARGET_SUBTARGET_DEFAULT
#define TARGET_SUBTARGET_DEFAULT (MASK_80387 | MASK_IEEE_FP | \
	MASK_FLOAT_RETURNS | MASK_ALIGN_DOUBLE | MASK_MS_BITFIELD_LAYOUT)

#undef MATH_LIBRARY
#define MATH_LIBRARY ""

/* Align doubles and long-longs in structures on qword boundaries.  */
#undef BIGGEST_FIELD_ALIGNMENT
#define BIGGEST_FIELD_ALIGNMENT 64

#undef DEFAULT_PCC_STRUCT_RETURN
#define DEFAULT_PCC_STRUCT_RETURN 0

/* Implicit arguments pointing to aggregate return values are to be
   removed by the caller.  */
#undef KEEP_AGGREGATE_RETURN_POINTER
#define KEEP_AGGREGATE_RETURN_POINTER 1

#undef DBX_REGISTER_NUMBER
#define DBX_REGISTER_NUMBER(n) (svr4_dbx_register_map[n])

/* Enable parsing of #pragma pack(push,<n>) and #pragma pack(pop).  */
#define HANDLE_PRAGMA_PACK_PUSH_POP

/* Default structure packing is 1-byte. */
#define TARGET_DEFAULT_PACK_STRUCT 1

#undef WCHAR_TYPE
#define WCHAR_TYPE "short unsigned int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 16

#undef WINT_TYPE
#define WINT_TYPE "int"

/* A C statement (sans semicolon) to output to the stdio stream
   FILE the assembler definition of uninitialized global DECL named
   NAME whose size is SIZE bytes and alignment is ALIGN bytes.
   Try to use asm_output_aligned_bss to implement this macro.  */

#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN) \
  asm_output_aligned_bss (FILE, DECL, NAME, SIZE, ALIGN)

/* Handle special EH pointer encodings.  Absolute, pc-relative, and
   indirect are handled automatically.  */
#define ASM_MAYBE_OUTPUT_ENCODED_ADDR_RTX(FILE, ENCODING, SIZE, ADDR, DONE) \
  do {									\
    if ((SIZE) == 4 && ((ENCODING) & 0x70) == DW_EH_PE_datarel)		\
      {									\
        fputs (ASM_LONG, FILE);			\
        assemble_name (FILE, XSTR (ADDR, 0));				\
	fputs (((ENCODING) & DW_EH_PE_indirect ? "@GOT" : "@GOTOFF"), FILE); \
        goto DONE;							\
      }									\
  } while (0)

/* there is no TLS support in NLMs/on NetWare */
#undef HAVE_AS_TLS

#define HAS_INIT_SECTION
#undef  INIT_SECTION_ASM_OP

#define CTOR_LISTS_DEFINED_EXTERNALLY

#undef  READONLY_DATA_SECTION_ASM_OP
#define READONLY_DATA_SECTION_ASM_OP    ".section\t.rodata"

/* Define this macro if references to a symbol must be treated
   differently depending on something about the variable or
   function named by the symbol (such as what section it is in).

   On i386 running NetWare, modify the assembler name with an underscore (_)
   prefix and a suffix consisting of an atsign (@) followed by a string of
   digits that represents the number of bytes of arguments passed to the
   function, if it has the attribute STDCALL. Alternatively, if it has the 
   REGPARM attribute, prefix it with an underscore (_), a digit representing
   the number of registers used, and an atsign (@). */
void i386_nlm_encode_section_info (tree, rtx, int);
const char *i386_nlm_strip_name_encoding (const char *);
#undef TARGET_ENCODE_SECTION_INFO
#define TARGET_ENCODE_SECTION_INFO  i386_nlm_encode_section_info
#undef  TARGET_STRIP_NAME_ENCODING
#define TARGET_STRIP_NAME_ENCODING  i386_nlm_strip_name_encoding
