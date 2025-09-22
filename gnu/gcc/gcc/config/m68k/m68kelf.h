/* m68kelf support, derived from m68kv4.h */

/* Target definitions for GNU compiler for mc680x0 running System V.4
   Copyright (C) 1991, 1993, 2000, 2002, 2003, 2004
   Free Software Foundation, Inc.

   Written by Ron Guilmette (rfg@netcom.com) and Fred Fish (fnf@cygnus.com).

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


#ifndef SWBEG_ASM_OP
#define SWBEG_ASM_OP "\t.swbeg\t"
#endif

/* Here are four prefixes that are used by asm_fprintf to
   facilitate customization for alternate assembler syntaxes.
   Machines with no likelihood of an alternate syntax need not
   define these and need not use asm_fprintf.  */

/* The prefix for register names.  Note that REGISTER_NAMES
   is supposed to include this prefix. Also note that this is NOT an
   fprintf format string, it is a literal string */

#undef REGISTER_PREFIX
#define REGISTER_PREFIX "%"

/* The prefix for local (compiler generated) labels.
   These labels will not appear in the symbol table.  */

#undef LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX "."

/* The prefix to add to user-visible assembler symbols.  */

#undef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX ""

/* The prefix for immediate operands.  */

#undef  IMMEDIATE_PREFIX
#define IMMEDIATE_PREFIX "#"

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

/* This is how to output an assembler line that says to advance the
   location counter to a multiple of 2**LOG bytes.  */

#undef ASM_OUTPUT_ALIGN
#define ASM_OUTPUT_ALIGN(FILE,LOG)				\
do {								\
  if ((LOG) > 0)						\
    fprintf ((FILE), "%s%u\n", ALIGN_ASM_OP, 1 << (LOG));	\
} while (0)

/* Use proper assembler syntax for these macros.  */
#undef ASM_OUTPUT_REG_PUSH
#define ASM_OUTPUT_REG_PUSH(FILE,REGNO)  \
  asm_fprintf (FILE, "\t%Omove.l %s,-(%Rsp)\n", reg_names[REGNO])

#undef ASM_OUTPUT_REG_POP
#define ASM_OUTPUT_REG_POP(FILE,REGNO)  \
  asm_fprintf (FILE, "\t%Omove.l (%Rsp)+,%s\n", reg_names[REGNO])

/*  Override the definition of NO_DOLLAR_IN_LABEL in svr4.h, for special
    g++ assembler names.  When this is defined, g++ uses embedded '.'
    characters and some m68k assemblers have problems with this.  The
    chances are much greater that any particular assembler will permit
    embedded '$' characters.  */

#undef NO_DOLLAR_IN_LABEL

/* Define PCC_STATIC_STRUCT_RETURN if the convention on the target machine
   is to use the nonreentrant technique for returning structure and union
   values, as commonly implemented by the AT&T Portable C Compiler (PCC).
   When defined, the gcc option -fpcc-struct-return can be used to cause
   this form to be generated.  When undefined, the option does nothing.
   For m68k SVR4, the convention is to use a reentrant technique compatible
   with the gcc default, so override the definition of this macro in m68k.h */

#undef PCC_STATIC_STRUCT_RETURN

/* Local common symbols are declared to the assembler with ".lcomm" rather
   than ".bss", so override the definition in svr4.h */

#undef BSS_ASM_OP
#define BSS_ASM_OP	"\t.lcomm\t"

/* Register in which address to store a structure value is passed to a
   function.  The default in m68k.h is a1.  For m68k/SVR4 it is a0.  */

#undef M68K_STRUCT_VALUE_REGNUM
#define M68K_STRUCT_VALUE_REGNUM 8

#define ASM_COMMENT_START "|"

/* Define how the m68k registers should be numbered for Dwarf output.
   The numbering provided here should be compatible with the native
   SVR4 SDB debugger in the m68k/SVR4 reference port, where d0-d7
   are 0-7, a0-a8 are 8-15, and fp0-fp7 are 16-23.  */

#undef DBX_REGISTER_NUMBER
#define DBX_REGISTER_NUMBER(REGNO) (REGNO)

/* The ASM_OUTPUT_SKIP macro is first defined in m68k.h, using ".skip".
   It is then overridden by m68k/sgs.h to use ".space", and again by svr4.h
   to use ".zero".  The m68k/SVR4 assembler uses ".space", so repeat the
   definition from m68k/sgs.h here.  Note that ASM_NO_SKIP_IN_TEXT is
   defined in m68k/sgs.h, so we don't have to repeat it here.  */

#undef ASM_OUTPUT_SKIP
#define ASM_OUTPUT_SKIP(FILE,SIZE)  \
  fprintf (FILE, "%s%u\n", SPACE_ASM_OP, (int)(SIZE))

#if 0
/* SVR4 m68k assembler is bitching on the `comm i,1,1' which askes for 
   1 byte alignment. Don't generate alignment for COMMON seems to be
   safer until we the assembler is fixed.  */
#undef ASM_OUTPUT_ALIGNED_COMMON
/* Same problem with this one.  */
#undef ASM_OUTPUT_ALIGNED_LOCAL
#endif

/* The `string' directive on m68k svr4 does not handle string with
   escape char (i.e., `\') right. Use normal way to output ASCII bytes
   seems to be safer.  */
#undef ASM_OUTPUT_ASCII
#define ASM_OUTPUT_ASCII(FILE,PTR,LEN)				\
do {								\
  register int sp = 0, ch;					\
  fputs (integer_asm_op (1, TRUE), (FILE));			\
  do {								\
    ch = (PTR)[sp];						\
    if (ch > ' ' && ! (ch & 0x80) && ch != '\\')		\
      {								\
	fprintf ((FILE), "'%c", ch);				\
      }								\
    else							\
      {								\
	fprintf ((FILE), "0x%x", ch);				\
      }								\
    if (++sp < (LEN))						\
      {								\
	if ((sp % 10) == 0)					\
	  {							\
	    fprintf ((FILE), "\n%s", integer_asm_op (1, TRUE));	\
	  }							\
	else							\
	  {							\
	    putc (',', (FILE));					\
	  }							\
      }								\
  } while (sp < (LEN));						\
  putc ('\n', (FILE));						\
} while (0)

#undef ASM_OUTPUT_COMMON
#undef ASM_OUTPUT_LOCAL
#define ASM_OUTPUT_COMMON(FILE, NAME, SIZE, ROUNDED)  \
( fputs (".comm ", (FILE)),			\
  assemble_name ((FILE), (NAME)),		\
  fprintf ((FILE), ",%u\n", (int)(SIZE)))

#define ASM_OUTPUT_LOCAL(FILE, NAME, SIZE, ROUNDED)  \
( fputs (".lcomm ", (FILE)),			\
  assemble_name ((FILE), (NAME)),		\
  fprintf ((FILE), ",%u\n", (int)(SIZE)))

/* Currently, JUMP_TABLES_IN_TEXT_SECTION must be defined in order to
   keep switch tables in the text section.  */
   
#define JUMP_TABLES_IN_TEXT_SECTION 1

/* Override the definition in svr4.h. In m68k svr4, using swbeg is the 
   standard way to do switch table.  */
#undef ASM_OUTPUT_BEFORE_CASE_LABEL
#define ASM_OUTPUT_BEFORE_CASE_LABEL(FILE,PREFIX,NUM,TABLE)		\
  fprintf ((FILE), "%s&%d\n", SWBEG_ASM_OP, XVECLEN (PATTERN (TABLE), 1));
/* end of stuff from m68kv4.h */

#undef ENDFILE_SPEC
#define ENDFILE_SPEC "crtend.o%s"

#undef	STARTFILE_SPEC
#define STARTFILE_SPEC "crtbegin.o%s"

/* If defined, a C expression whose value is a string containing the
   assembler operation to identify the following data as
   uninitialized global data.  If not defined, and neither
   `ASM_OUTPUT_BSS' nor `ASM_OUTPUT_ALIGNED_BSS' are defined,
   uninitialized global data will be output in the data section if
   `-fno-common' is passed, otherwise `ASM_OUTPUT_COMMON' will be
   used.  */
#ifndef BSS_SECTION_ASM_OP
#define BSS_SECTION_ASM_OP	"\t.section\t.bss"
#endif

/* Like `ASM_OUTPUT_BSS' except takes the required alignment as a
   separate, explicit argument.  If you define this macro, it is used
   in place of `ASM_OUTPUT_BSS', and gives you more flexibility in
   handling the required alignment of the variable.  The alignment is
   specified as the number of bits.

   Try to use function `asm_output_aligned_bss' defined in file
   `varasm.c' when defining this macro.  */
#ifndef ASM_OUTPUT_ALIGNED_BSS
#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN) \
  asm_output_aligned_bss (FILE, DECL, NAME, SIZE, ALIGN)
#endif
