/* Definitions of target machine for GNU compiler.
   m68k series COFF object files and debugging, version.
   Copyright (C) 1994, 1996, 1997, 2000, 2002, 2003, 2004 Free Software Foundation, Inc.

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

/* This file is included after m68k.h by CPU COFF specific files.  It
   is not a complete target itself.  */

/* Used in m68k.c to include required support code.  */

#define M68K_TARGET_COFF 1

/* Generate sdb debugging information.  */

#define SDB_DEBUGGING_INFO 1

/* COFF symbols don't start with an underscore.  */

#undef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX ""

/* Use a prefix for local labels, just to be on the save side.  */

#undef LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX "."

/* Use a register prefix to avoid clashes with external symbols (classic
   example: `extern char PC;' in termcap).  */

#undef REGISTER_PREFIX
#define REGISTER_PREFIX "%"

/* In the machine description we can't use %R, because it will not be seen
   by ASM_FPRINTF.  (Isn't that a design bug?).  */

#undef REGISTER_PREFIX_MD
#define REGISTER_PREFIX_MD "%%"

/* config/m68k.md has an explicit reference to the program counter,
   prefix this by the register prefix.  */

#define ASM_RETURN_CASE_JUMP				\
  do {							\
    if (TARGET_COLDFIRE)				\
      {							\
	if (ADDRESS_REG_P (operands[0]))		\
	  return "jmp %%pc@(2,%0:l)";			\
	else						\
	  return "ext%.l %0\n\tjmp %%pc@(2,%0:l)";	\
      }							\
    else						\
      return "jmp %%pc@(2,%0:w)";			\
  } while (0)

#define TARGET_ASM_FILE_START_FILE_DIRECTIVE true

/* If defined, a C expression whose value is a string containing the
   assembler operation to identify the following data as uninitialized global
   data.  */

#define BSS_SECTION_ASM_OP	"\t.section\t.bss"

/* A C statement (sans semicolon) to output to the stdio stream
   FILE the assembler definition of uninitialized global DECL named
   NAME whose size is SIZE bytes and alignment is ALIGN bytes.
   Try to use asm_output_aligned_bss to implement this macro.  */

#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN) \
  asm_output_aligned_bss ((FILE), (DECL), (NAME), (SIZE), (ALIGN))

/* Switch into a generic section.  */
#undef TARGET_ASM_NAMED_SECTION
#define TARGET_ASM_NAMED_SECTION  m68k_coff_asm_named_section

/* Don't assume anything about startfiles.  */

#undef STARTFILE_SPEC
#define STARTFILE_SPEC ""
