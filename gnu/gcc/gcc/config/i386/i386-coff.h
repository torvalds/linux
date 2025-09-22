/* Definitions for "naked" Intel 386 using coff object format files
   and coff debugging info.

   Copyright (C) 1994, 2000, 2002, 2004 Free Software Foundation, Inc.

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


#define TARGET_VERSION fprintf (stderr, " (80386, COFF BSD syntax)");

#define TARGET_OS_CPP_BUILTINS() /* Sweet FA.  */

/* We want to be able to get DBX debugging information via -gstabs.  */

#define DBX_DEBUGGING_INFO 1

#undef PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE SDB_DEBUG

/* Switch into a generic section.  */
#define TARGET_ASM_NAMED_SECTION  default_coff_asm_named_section

/* Prefix for internally generated assembler labels.  If we aren't using
   underscores, we are using prefix `.'s to identify labels that should
   be ignored, as in `i386/gas.h' --karl@cs.umb.edu  */

#undef  LPREFIX
#define LPREFIX ".L"

/* The prefix to add to user-visible assembler symbols.  */

#undef  USER_LABEL_PREFIX
#define USER_LABEL_PREFIX ""

/* If user-symbols don't have underscores,
   then it must take more than `L' to identify
   a label that should be ignored.  */

/* This is how to store into the string BUF
   the symbol_ref name of an internal numbered label where
   PREFIX is the class of label and NUM is the number within the class.
   This is suitable for output with `assemble_name'.  */

#undef  ASM_GENERATE_INTERNAL_LABEL
#define ASM_GENERATE_INTERNAL_LABEL(BUF,PREFIX,NUMBER)	\
  sprintf ((BUF), ".%s%ld", (PREFIX), (long)(NUMBER))

/* GNU as expects alignment to be the number of bytes instead of the log for
   COFF targets.  */

#undef ASM_OUTPUT_ALIGN
#define ASM_OUTPUT_ALIGN(FILE,LOG) \
  if ((LOG)!=0) fprintf ((FILE), "\t.align %d\n", 1<<(LOG))

/* end of i386-coff.h */
