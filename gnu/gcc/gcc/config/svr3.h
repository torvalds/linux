/* Operating system specific defines to be used when targeting GCC for
   generic System V Release 3 system.
   Copyright (C) 1991, 1996, 2000, 2002, 2004 Free Software Foundation, Inc.
   Contributed by Ron Guilmette (rfg@monkeys.com).

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
Boston, MA 02110-1301, USA. */

/* Define a symbol indicating that we are using svr3.h.  */
#define USING_SVR3_H

/* Define a symbol so that libgcc* can know what sort of operating
   environment and assembler syntax we are targeting for.  */
#define SVR3_target

/* Assembler, linker, library, and startfile spec's.  */

/* The .file command should always begin the output.  */
#define TARGET_ASM_FILE_START_FILE_DIRECTIVE true

/* This says how to output an assembler line
   to define a global common symbol.  */
/* We don't use ROUNDED because the standard compiler doesn't,
   and the linker gives error messages if a common symbol
   has more than one length value.  */

#undef ASM_OUTPUT_COMMON
#define ASM_OUTPUT_COMMON(FILE, NAME, SIZE, ROUNDED)  \
( fputs (".comm ", (FILE)),			\
  assemble_name ((FILE), (NAME)),		\
  fprintf ((FILE), ",%lu\n", (unsigned long)(SIZE)))

/* This says how to output an assembler line
   to define a local common symbol.  */

/* Note that using bss_section here caused errors
   in building shared libraries on system V.3.  */
#undef ASM_OUTPUT_LOCAL
#define ASM_OUTPUT_LOCAL(FILE, NAME, SIZE, ROUNDED)	\
  do {							\
    int align = exact_log2 (ROUNDED);			\
    if (align > 2) align = 2;				\
    switch_to_section (data_section);			\
    ASM_OUTPUT_ALIGN ((FILE), align == -1 ? 2 : align);	\
    ASM_OUTPUT_LABEL ((FILE), (NAME));			\
    fprintf ((FILE), "\t.set .,.+%u\n", (int)(ROUNDED));	\
  } while (0)

/* Output #ident as a .ident.  */

#undef  ASM_OUTPUT_IDENT
#define ASM_OUTPUT_IDENT(FILE, NAME) \
  fprintf (FILE, "\t.ident \"%s\"\n", NAME);

/* Use periods rather than dollar signs in special g++ assembler names.  */

#define NO_DOLLAR_IN_LABEL

/* System V Release 3 uses COFF debugging info.  */

#define SDB_DEBUGGING_INFO 1

/* We don't want to output DBX debugging information.  */

#undef DBX_DEBUGGING_INFO

/* Define the actual types of some ANSI-mandated types.  These
   definitions should work for most SVR3 systems.  */

#undef SIZE_TYPE
#define SIZE_TYPE "unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "int"

#undef WCHAR_TYPE
#define WCHAR_TYPE "long int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE BITS_PER_WORD

/* The prefix to add to user-visible assembler symbols.

   For System V Release 3 the convention is to prepend a leading
   underscore onto user-level symbol names.  */

#undef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX "_"

/* This is how to store into the string LABEL
   the symbol_ref name of an internal numbered label where
   PREFIX is the class of label and NUM is the number within the class.
   This is suitable for output with `assemble_name'.

   For most svr3 systems, the convention is that any symbol which begins
   with a period is not put into the linker symbol table by the assembler.  */

#undef ASM_GENERATE_INTERNAL_LABEL
#define ASM_GENERATE_INTERNAL_LABEL(LABEL,PREFIX,NUM)	\
  sprintf (LABEL, "*%s%s%ld", LOCAL_LABEL_PREFIX, PREFIX, (long)(NUM))

/* We want local labels to start with period if made with asm_fprintf.  */
#undef LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX "."

/* Support const sections and the ctors and dtors sections for g++.  */

/* Define a few machine-specific details of the implementation of
   constructors.

   The __CTORS_LIST__ goes in the .init section.  Define CTOR_LIST_BEGIN
   and CTOR_LIST_END to contribute to the .init section an instruction to
   push a word containing 0 (or some equivalent of that).

   Define TARGET_ASM_CONSTRUCTOR to push the address of the constructor.  */

#define INIT_SECTION_ASM_OP     "\t.section\t.init"
#define FINI_SECTION_ASM_OP     "\t.section .fini,\"x\""
#define DTORS_SECTION_ASM_OP    FINI_SECTION_ASM_OP

/* CTOR_LIST_BEGIN and CTOR_LIST_END are machine-dependent
   because they push on the stack.  */

#ifndef STACK_GROWS_DOWNWARD

/* Constructor list on stack is in reverse order.  Go to the end of the
   list and go backwards to call constructors in the right order.  */
#define DO_GLOBAL_CTORS_BODY					\
do {								\
  func_ptr *p, *beg = alloca (0);				\
  for (p = beg; *p; p++)					\
    ;								\
  while (p != beg)						\
    (*--p) ();							\
} while (0)

#else

/* Constructor list on stack is in correct order.  Just call them.  */
#define DO_GLOBAL_CTORS_BODY					\
do {								\
  func_ptr *p, *beg = alloca (0);				\
  for (p = beg; *p; )						\
    (*p++) ();							\
} while (0)

#endif /* STACK_GROWS_DOWNWARD */
