/* Definitions of target machine for GNU compiler.  "naked" 68020,
   a.out object files and debugging, version.
   Copyright (C) 1994, 1996, 2003 Free Software Foundation, Inc.

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

#define DBX_DEBUGGING_INFO 1
#undef SDB_DEBUGGING_INFO

/* If defined, a C expression whose value is a string containing the
   assembler operation to identify the following data as uninitialized global
   data.  */
#define BSS_SECTION_ASM_OP "\t.bss"

/* A C statement (sans semicolon) to output to the stdio stream
   FILE the assembler definition of uninitialized global DECL named
   NAME whose size is SIZE bytes.  The variable ROUNDED
   is the size rounded up to whatever alignment the caller wants.
   Try to use asm_output_bss to implement this macro.  */
/* a.out files typically can't handle arbitrary variable alignments so
   define ASM_OUTPUT_BSS instead of ASM_OUTPUT_ALIGNED_BSS.  */
#define ASM_OUTPUT_BSS(FILE, DECL, NAME, SIZE, ROUNDED) \
  asm_output_bss ((FILE), (DECL), (NAME), (SIZE), (ROUNDED))
