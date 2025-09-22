/* Configuration for an i386 running MS-DOS with DJGPP.
   Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2004, 2005
   Free Software Foundation, Inc.

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

/* Support generation of DWARF2 debugging info.  */
#define DWARF2_DEBUGGING_INFO 1

/* Don't assume anything about the header files.  */
#define NO_IMPLICIT_EXTERN_C

#define HANDLE_SYSV_PRAGMA 1

/* Enable parsing of #pragma pack(push,<n>) and #pragma pack(pop).  */
#define HANDLE_PRAGMA_PACK_PUSH_POP 1

/* If defined, a C expression whose value is a string containing the
   assembler operation to identify the following data as
   uninitialized global data.  If not defined, and neither
   `ASM_OUTPUT_BSS' nor `ASM_OUTPUT_ALIGNED_BSS' are defined,
   uninitialized global data will be output in the data section if
   `-fno-common' is passed, otherwise `ASM_OUTPUT_COMMON' will be
   used.  */
#undef BSS_SECTION_ASM_OP
#define BSS_SECTION_ASM_OP "\t.section\t.bss"

/* Define the name of the .data section.  */
#undef DATA_SECTION_ASM_OP
#define DATA_SECTION_ASM_OP "\t.section .data"

/* Define the name of the .ident op.  */
#undef IDENT_ASM_OP
#define IDENT_ASM_OP "\t.ident\t"

/* Enable alias attribute support.  */
#ifndef SET_ASM_OP
#define SET_ASM_OP "\t.set\t"
#endif

/* Define the name of the .text section.  */
#undef TEXT_SECTION_ASM_OP
#define TEXT_SECTION_ASM_OP "\t.section .text"

/* Define standard DJGPP installation paths.  */
/* We override default /usr or /usr/local part with /dev/env/DJDIR which */
/* points to actual DJGPP installation directory.  */

/* Standard include directory */
#undef STANDARD_INCLUDE_DIR
#define STANDARD_INCLUDE_DIR "/dev/env/DJDIR/include/"

/* Search for as.exe and ld.exe in DJGPP's binary directory.  */ 
#undef MD_EXEC_PREFIX
#define MD_EXEC_PREFIX "/dev/env/DJDIR/bin/"

/* Standard DJGPP library and startup files */
#undef MD_STARTFILE_PREFIX
#define MD_STARTFILE_PREFIX "/dev/env/DJDIR/lib/"

/* Correctly handle absolute filename detection in cp/xref.c */
#define FILE_NAME_ABSOLUTE_P(NAME) \
        (((NAME)[0] == '/') || ((NAME)[0] == '\\') || \
        (((NAME)[0] >= 'A') && ((NAME)[0] <= 'z') && ((NAME)[1] == ':')))

#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
	builtin_define_std ("MSDOS");		\
	builtin_define_std ("GO32");		\
	builtin_assert ("system=msdos");	\
    }						\
  while (0)

/* Include <sys/version.h> so __DJGPP__ and __DJGPP_MINOR__ are defined.  */
#undef CPP_SPEC
#define CPP_SPEC "-remap %{posix:-D_POSIX_SOURCE} \
  -imacros %s../include/sys/version.h"

/* We need to override link_command_spec in gcc.c so support -Tdjgpp.djl.
   This cannot be done in LINK_SPECS as that LINK_SPECS is processed
   before library search directories are known by the linker.
   This avoids problems when specs file is not available. An alternate way,
   suggested by Robert Hoehne, is to use SUBTARGET_EXTRA_SPECS instead.
*/ 

#undef LINK_COMMAND_SPEC
#define LINK_COMMAND_SPEC \
"%{!fsyntax-only: \
%{!c:%{!M:%{!MM:%{!E:%{!S:%(linker) %l %X %{o*} %{A} %{d} %{e*} %{m} %{N} %{n} \
\t%{r} %{s} %{t} %{u*} %{x} %{z} %{Z}\
\t%{!A:%{!nostdlib:%{!nostartfiles:%S}}}\
\t%{static:} %{L*} %D %o\
\t%{!nostdlib:%{!nodefaultlibs:%G %L %G}}\
\t%{!A:%{!nostdlib:%{!nostartfiles:%E}}}\
\t-Tdjgpp.djl %{T*}}}}}}}\n\
%{!c:%{!M:%{!MM:%{!E:%{!S:stubify %{v} %{o*:%*} %{!o*:a.out} }}}}}"

/* Always just link in 'libc.a'.  */
#undef LIB_SPEC
#define LIB_SPEC "-lc"

/* Pick the right startup code depending on the -pg flag.  */
#undef STARTFILE_SPEC
#define STARTFILE_SPEC "%{pg:gcrt0.o%s}%{!pg:crt0.o%s}"

/* Make sure that gcc will not look for .h files in /usr/local/include 
   unless user explicitly requests it.  */
#undef LOCAL_INCLUDE_DIR

/* Switch into a generic section.  */
#define TARGET_ASM_NAMED_SECTION  default_coff_asm_named_section

/* This is how to output an assembler line
   that says to advance the location counter
   to a multiple of 2**LOG bytes.  */

#undef ASM_OUTPUT_ALIGN
#define ASM_OUTPUT_ALIGN(FILE,LOG) \
  if ((LOG) != 0) fprintf ((FILE), "\t.p2align %d\n", LOG)

/* This is how to output a global symbol in the BSS section.  */
#undef ASM_OUTPUT_ALIGNED_BSS
#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN) \
  asm_output_aligned_bss ((FILE), (DECL), (NAME), (SIZE), (ALIGN))

/* This is how to tell assembler that a symbol is weak  */ 
#undef ASM_WEAKEN_LABEL
#define ASM_WEAKEN_LABEL(FILE,NAME) \
  do { fputs ("\t.weak\t", FILE); assemble_name (FILE, NAME); \
       fputc ('\n', FILE); } while (0)

/* djgpp automatically calls its own version of __main, so don't define one
   in libgcc, nor call one in main().  */
#define HAS_INIT_SECTION

/* Definitions for types and sizes. Wide characters are 16-bits long so
   Win32 compiler add-ons will be wide character compatible.  */
#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 16

#undef WCHAR_TYPE
#define WCHAR_TYPE "short unsigned int"

#undef WINT_TYPE
#define WINT_TYPE "int"

#undef SIZE_TYPE
#define SIZE_TYPE "long unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "int"

/* Used to be defined in xm-djgpp.h, but moved here for cross-compilers.  */
#define LIBSTDCXX "-lstdcxx"

#define TARGET_VERSION fprintf (stderr, " (80386, MS-DOS DJGPP)"); 

/* Warn that -mbnu210 is now obsolete.  */
#undef  SUBTARGET_OVERRIDE_OPTIONS
#define SUBTARGET_OVERRIDE_OPTIONS \
do \
  { \
    if (TARGET_BNU210) \
      {	\
        warning (0, "-mbnu210 is ignored (option is obsolete)"); \
      }	\
  } \
while (0)

/* Support for C++ templates.  */
#undef MAKE_DECL_ONE_ONLY
#define MAKE_DECL_ONE_ONLY(DECL) (DECL_WEAK (DECL) = 1)
