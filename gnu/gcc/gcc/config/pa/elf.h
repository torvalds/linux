/* Definitions for ELF assembler support.
   Copyright (C) 1999, 2003, 2005 Free Software Foundation, Inc.

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

/* So we can conditionalize small amounts of code in pa.c or pa.md.  */
#define OBJ_ELF

#define ENDFILE_SPEC "crtend.o%s"

#define STARTFILE_SPEC "%{!shared: \
			 %{!symbolic: \
			  %{pg:gcrt0.o%s}%{!pg:%{p:mcrt0.o%s}%{!p:crt0.o%s}}}}\
			crtbegin.o%s"

#define TEXT_SECTION_ASM_OP "\t.text"
#define DATA_SECTION_ASM_OP "\t.data"
#define BSS_SECTION_ASM_OP "\t.section\t.bss"

#define TARGET_ASM_FILE_START pa_elf_file_start

#undef ASM_DECLARE_FUNCTION_NAME
#define ASM_DECLARE_FUNCTION_NAME(FILE, NAME, DECL) \
do {  \
  if (TREE_PUBLIC (DECL)) \
    { \
      fputs ("\t.EXPORT ", FILE); \
      assemble_name (FILE, NAME); \
      fputs (",ENTRY\n", FILE); \
    } \
   } while (0)

/* This is how to output a command to make the user-level label
   named NAME defined for reference from other files.  We use
   assemble_name_raw instead of assemble_name since a symbol in
   a .IMPORT directive that isn't otherwise referenced is not
   placed in the symbol table of the assembled object.

   Failure to import a function reference can cause the HP linker
   to segmentation fault!

   Note that the SOM based tools need the symbol imported as a
   CODE symbol, while the ELF based tools require the symbol to
   be imported as an ENTRY symbol.  */

#define ASM_OUTPUT_EXTERNAL(FILE, DECL, NAME) \
  pa_hpux_asm_output_external ((FILE), (DECL), (NAME))
#define ASM_OUTPUT_EXTERNAL_REAL(FILE, DECL, NAME) \
  do { fputs ("\t.IMPORT ", FILE);					\
       assemble_name_raw (FILE, NAME);					\
       if (FUNCTION_NAME_P (NAME))     					\
	 fputs (",ENTRY\n", FILE);					\
       else								\
	 fputs (",DATA\n", FILE);					\
     } while (0)

/* The bogus HP assembler requires ALL external references to be
   "imported", even library calls. They look a bit different, so
   here's this macro.

   Also note not all libcall names are passed to
   targetm.encode_section_info (__main for example).  To make sure all
   libcall names have section info recorded in them, we do it here.  */

#undef ASM_OUTPUT_EXTERNAL_LIBCALL
#define ASM_OUTPUT_EXTERNAL_LIBCALL(FILE, RTL) \
  do { fputs ("\t.IMPORT ", FILE);					\
       if (!function_label_operand (RTL, VOIDmode))			\
	 hppa_encode_label (RTL);					\
       assemble_name (FILE, XSTR ((RTL), 0));		       		\
       fputs (",ENTRY\n", FILE);					\
     } while (0)

/* Biggest alignment supported by the object file format of this
   machine.  Use this macro to limit the alignment which can be
   specified using the `__attribute__ ((aligned (N)))' construct.  If
   not defined, the default value is `BIGGEST_ALIGNMENT'.  */
#define MAX_OFILE_ALIGNMENT (32768 * 8)
