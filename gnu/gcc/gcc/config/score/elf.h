/* elf.h for Sunplus S+CORE processor
   Copyright (C) 2005 Free Software Foundation, Inc.

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

#define OBJECT_FORMAT_ELF

/* Biggest alignment supported by the object file format of this machine.  */
#undef  MAX_OFILE_ALIGNMENT
#define MAX_OFILE_ALIGNMENT        (32768 * 8)

/* Switch into a generic section.  */
#undef  TARGET_ASM_NAMED_SECTION
#define TARGET_ASM_NAMED_SECTION  default_elf_asm_named_section

/* The following macro defines the format used to output the second
   operand of the .type assembler directive.  */
#define TYPE_OPERAND_FMT        "@%s"

#undef TYPE_ASM_OP
#define TYPE_ASM_OP        "\t.type\t"

#undef SIZE_ASM_OP
#define SIZE_ASM_OP        "\t.size\t"

/* A c expression whose value is a string containing the
   assembler operation to identify the following data as
   uninitialized global data.  */
#ifndef BSS_SECTION_ASM_OP
#define BSS_SECTION_ASM_OP        "\t.section\t.bss"
#endif

#ifndef ASM_OUTPUT_ALIGNED_BSS
#define ASM_OUTPUT_ALIGNED_BSS asm_output_aligned_bss
#endif

#define ASM_OUTPUT_DEF(FILE, LABEL1, LABEL2)                       \
  do {                                                             \
    fputc ('\t', FILE);                                            \
    assemble_name (FILE, LABEL1);                                  \
    fputs (" = ", FILE);                                           \
    assemble_name (FILE, LABEL2);                                  \
    fputc ('\n', FILE);                                            \
 } while (0)


/* This is how we tell the assembler that a symbol is weak.  */
#undef  ASM_WEAKEN_LABEL
#define ASM_WEAKEN_LABEL(FILE, NAME) ASM_OUTPUT_WEAK_ALIAS (FILE, NAME, 0)

#define ASM_OUTPUT_WEAK_ALIAS(FILE, NAME, VALUE)      \
  do {                                                \
    fputs ("\t.weak\t", FILE);                        \
    assemble_name (FILE, NAME);                       \
    if (VALUE)                                        \
      {                                               \
        fputc (' ', FILE);                            \
        assemble_name (FILE, VALUE);                  \
      }                                               \
    fputc ('\n', FILE);                               \
 } while (0)

#define MAKE_DECL_ONE_ONLY(DECL) (DECL_WEAK (DECL) = 1)

/* On elf, we *do* have support for the .init and .fini sections, and we
   can put stuff in there to be executed before and after `main'.  We let
   crtstuff.c and other files know this by defining the following symbols.
   The definitions say how to change sections to the .init and .fini
   sections.  This is the same for all known elf assemblers.  */
#undef  INIT_SECTION_ASM_OP
#define INIT_SECTION_ASM_OP     "\t.section\t.init"
#undef  FINI_SECTION_ASM_OP
#define FINI_SECTION_ASM_OP     "\t.section\t.fini"

/* Don't set the target flags, this is done by the linker script */
#undef  LIB_SPEC
#define LIB_SPEC ""

#undef  STARTFILE_SPEC
#define STARTFILE_SPEC          "crti%O%s crtbegin%O%s"

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC            "crtend%O%s crtn%O%s"

/* We support #pragma.  */
#define HANDLE_SYSV_PRAGMA      1
