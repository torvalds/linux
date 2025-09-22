/* Definitions of target machine for GNU compiler, for DEC Alpha w/ELF.
   Copyright (C) 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003
   Free Software Foundation, Inc.
   Contributed by Richard Henderson (rth@tamu.edu).

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

#undef OBJECT_FORMAT_COFF
#undef EXTENDED_COFF
#define OBJECT_FORMAT_ELF

/* ??? Move all SDB stuff from alpha.h to osf.h.  */
#undef SDB_DEBUGGING_INFO

#define DBX_DEBUGGING_INFO 1
#define DWARF2_DEBUGGING_INFO 1

#undef  PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG

#undef ASM_FINAL_SPEC

/* alpha/ doesn't use elfos.h for some reason.  */
#define TARGET_OBJFMT_CPP_BUILTINS()		\
  do						\
    {						\
	builtin_define ("__ELF__");		\
    }						\
  while (0)

#undef  CC1_SPEC
#define CC1_SPEC  "%{G*}"

#undef  ASM_SPEC
#define ASM_SPEC  "%{G*} %{relax:-relax} %{!gstabs*:-no-mdebug}%{gstabs*:-mdebug}"

#undef  IDENT_ASM_OP
#define IDENT_ASM_OP "\t.ident\t"

/* Output #ident as a .ident.  */
#undef  ASM_OUTPUT_IDENT
#define ASM_OUTPUT_IDENT(FILE, NAME) \
  fprintf (FILE, "%s\"%s\"\n", IDENT_ASM_OP, NAME);

/* This is how to allocate empty space in some section.  The .zero
   pseudo-op is used for this on most svr4 assemblers.  */

#undef  SKIP_ASM_OP
#define SKIP_ASM_OP	"\t.zero\t"

#undef  ASM_OUTPUT_SKIP
#define ASM_OUTPUT_SKIP(FILE, SIZE) \
  fprintf (FILE, "%s"HOST_WIDE_INT_PRINT_UNSIGNED"\n", SKIP_ASM_OP, (SIZE))

/* Output the label which precedes a jumptable.  Note that for all svr4
   systems where we actually generate jumptables (which is to say every
   svr4 target except i386, where we use casesi instead) we put the jump-
   tables into the .rodata section and since other stuff could have been
   put into the .rodata section prior to any given jumptable, we have to
   make sure that the location counter for the .rodata section gets pro-
   perly re-aligned prior to the actual beginning of the jump table.  */

#undef  ALIGN_ASM_OP
#define ALIGN_ASM_OP "\t.align\t"

#ifndef ASM_OUTPUT_BEFORE_CASE_LABEL
#define ASM_OUTPUT_BEFORE_CASE_LABEL(FILE, PREFIX, NUM, TABLE) \
  ASM_OUTPUT_ALIGN ((FILE), 2);
#endif

#undef  ASM_OUTPUT_CASE_LABEL
#define ASM_OUTPUT_CASE_LABEL(FILE, PREFIX, NUM, JUMPTABLE)		\
  do {									\
    ASM_OUTPUT_BEFORE_CASE_LABEL (FILE, PREFIX, NUM, JUMPTABLE)		\
    (*targetm.asm_out.internal_label) (FILE, PREFIX, NUM);			\
  } while (0)

/* The standard SVR4 assembler seems to require that certain builtin
   library routines (e.g. .udiv) be explicitly declared as .globl
   in each assembly file where they are referenced.  */

#undef  ASM_OUTPUT_EXTERNAL_LIBCALL
#define ASM_OUTPUT_EXTERNAL_LIBCALL(FILE, FUN)				\
  (*targetm.asm_out.globalize_label) (FILE, XSTR (FUN, 0))

/* This says how to output assembler code to declare an
   uninitialized external linkage data object.  Under SVR4,
   the linker seems to want the alignment of data objects
   to depend on their types.  We do exactly that here.  */

#undef  COMMON_ASM_OP
#define COMMON_ASM_OP	"\t.comm\t"

#undef  ASM_OUTPUT_ALIGNED_COMMON
#define ASM_OUTPUT_ALIGNED_COMMON(FILE, NAME, SIZE, ALIGN)		\
do {									\
  fprintf ((FILE), "%s", COMMON_ASM_OP);				\
  assemble_name ((FILE), (NAME));					\
  fprintf ((FILE), "," HOST_WIDE_INT_PRINT_UNSIGNED ",%u\n", (SIZE), (ALIGN) / BITS_PER_UNIT);	\
} while (0)

/* This says how to output assembler code to declare an
   uninitialized internal linkage data object.  Under SVR4,
   the linker seems to want the alignment of data objects
   to depend on their types.  We do exactly that here.  */

#undef  ASM_OUTPUT_ALIGNED_LOCAL
#define ASM_OUTPUT_ALIGNED_LOCAL(FILE, NAME, SIZE, ALIGN)		\
do {									\
  if ((SIZE) <= g_switch_value)						\
    switch_to_section (sbss_section);					\
  else									\
    switch_to_section (bss_section);					\
  ASM_OUTPUT_TYPE_DIRECTIVE (FILE, NAME, "object");			\
  if (!flag_inhibit_size_directive)					\
    ASM_OUTPUT_SIZE_DIRECTIVE (FILE, NAME, SIZE);			\
  ASM_OUTPUT_ALIGN ((FILE), exact_log2((ALIGN) / BITS_PER_UNIT));	\
  ASM_OUTPUT_LABEL(FILE, NAME);						\
  ASM_OUTPUT_SKIP((FILE), (SIZE) ? (SIZE) : 1);				\
} while (0)

/* This says how to output assembler code to declare an
   uninitialized external linkage data object.  */

#undef  ASM_OUTPUT_ALIGNED_BSS
#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN)		\
do {									\
  ASM_OUTPUT_ALIGNED_LOCAL (FILE, NAME, SIZE, ALIGN);			\
} while (0)

/* Biggest alignment supported by the object file format of this
   machine.  Use this macro to limit the alignment which can be
   specified using the `__attribute__ ((aligned (N)))' construct.  If
   not defined, the default value is `BIGGEST_ALIGNMENT'. 

   This value is really 2^63.  Since gcc figures the alignment in bits,
   we could only potentially get to 2^60 on suitable hosts.  Due to other
   considerations in varasm, we must restrict this to what fits in an int.  */

#undef  MAX_OFILE_ALIGNMENT
#define MAX_OFILE_ALIGNMENT \
  (1 << (HOST_BITS_PER_INT < 64 ? HOST_BITS_PER_INT - 2 : 62))

/* This is the pseudo-op used to generate a contiguous sequence of byte
   values from a double-quoted string WITHOUT HAVING A TERMINATING NUL
   AUTOMATICALLY APPENDED.  This is the same for most svr4 assemblers.  */

#undef  ASCII_DATA_ASM_OP
#define ASCII_DATA_ASM_OP	"\t.ascii\t"

#undef  READONLY_DATA_SECTION_ASM_OP
#define READONLY_DATA_SECTION_ASM_OP	"\t.section\t.rodata"
#undef  BSS_SECTION_ASM_OP
#define BSS_SECTION_ASM_OP	"\t.section\t.bss"
#undef  SBSS_SECTION_ASM_OP
#define SBSS_SECTION_ASM_OP	"\t.section\t.sbss,\"aw\""
#undef  SDATA_SECTION_ASM_OP
#define SDATA_SECTION_ASM_OP	"\t.section\t.sdata,\"aw\""

/* On svr4, we *do* have support for the .init and .fini sections, and we
   can put stuff in there to be executed before and after `main'.  We let
   crtstuff.c and other files know this by defining the following symbols.
   The definitions say how to change sections to the .init and .fini
   sections.  This is the same for all known svr4 assemblers.  */

#undef  INIT_SECTION_ASM_OP
#define INIT_SECTION_ASM_OP	"\t.section\t.init"
#undef  FINI_SECTION_ASM_OP
#define FINI_SECTION_ASM_OP	"\t.section\t.fini"

#ifdef HAVE_GAS_SUBSECTION_ORDERING

#define ASM_SECTION_START_OP	"\t.subsection\t-1"

/* Output assembly directive to move to the beginning of current section.  */
#define ASM_OUTPUT_SECTION_START(FILE)	\
  fprintf ((FILE), "%s\n", ASM_SECTION_START_OP)

#endif

/* Switch into a generic section.  */
#define TARGET_ASM_NAMED_SECTION  default_elf_asm_named_section
#define TARGET_ASM_SELECT_SECTION  default_elf_select_section

#define MAKE_DECL_ONE_ONLY(DECL) (DECL_WEAK (DECL) = 1)

/* Define the strings used for the special svr4 .type and .size directives.
   These strings generally do not vary from one system running svr4 to
   another, but if a given system (e.g. m88k running svr) needs to use
   different pseudo-op names for these, they may be overridden in the
   file which includes this one.  */

#undef  TYPE_ASM_OP
#define TYPE_ASM_OP	"\t.type\t"
#undef  SIZE_ASM_OP
#define SIZE_ASM_OP	"\t.size\t"

/* This is how we tell the assembler that a symbol is weak.  */

#undef  ASM_WEAKEN_LABEL
#define ASM_WEAKEN_LABEL(FILE, NAME) \
  do { fputs ("\t.weak\t", FILE); assemble_name (FILE, NAME); \
       fputc ('\n', FILE); } while (0)

/* This is how we tell the assembler that two symbols have the same value.  */

#undef  ASM_OUTPUT_DEF
#define ASM_OUTPUT_DEF(FILE, ALIAS, NAME)			\
  do {								\
    assemble_name(FILE, ALIAS);					\
    fputs(" = ", FILE);						\
    assemble_name(FILE, NAME);					\
    fputc('\n', FILE);						\
  } while (0)

#undef  ASM_OUTPUT_DEF_FROM_DECLS
#define ASM_OUTPUT_DEF_FROM_DECLS(FILE, DECL, TARGET)		\
  do {								\
    const char *alias = XSTR (XEXP (DECL_RTL (DECL), 0), 0);	\
    const char *name = IDENTIFIER_POINTER (TARGET);		\
    if (TREE_CODE (DECL) == FUNCTION_DECL)			\
      {								\
	fputc ('$', FILE);					\
	assemble_name (FILE, alias);				\
	fputs ("..ng = $", FILE);				\
	assemble_name (FILE, name);				\
	fputs ("..ng\n", FILE);					\
      }								\
    assemble_name(FILE, alias);					\
    fputs(" = ", FILE);						\
    assemble_name(FILE, name);					\
    fputc('\n', FILE);						\
  } while (0)

/* The following macro defines the format used to output the second
   operand of the .type assembler directive.  Different svr4 assemblers
   expect various different forms for this operand.  The one given here
   is just a default.  You may need to override it in your machine-
   specific tm.h file (depending upon the particulars of your assembler).  */

#undef  TYPE_OPERAND_FMT
#define TYPE_OPERAND_FMT	"@%s"

/* Write the extra assembler code needed to declare a function's result.
   Most svr4 assemblers don't require any special declaration of the
   result value, but there are exceptions.  */

#ifndef ASM_DECLARE_RESULT
#define ASM_DECLARE_RESULT(FILE, RESULT)
#endif

/* These macros generate the special .type and .size directives which
   are used to set the corresponding fields of the linker symbol table
   entries in an ELF object file under SVR4.  These macros also output
   the starting labels for the relevant functions/objects.  */

/* Write the extra assembler code needed to declare an object properly.  */

#undef  ASM_DECLARE_OBJECT_NAME
#define ASM_DECLARE_OBJECT_NAME(FILE, NAME, DECL)		\
  do {								\
    HOST_WIDE_INT size;						\
    ASM_OUTPUT_TYPE_DIRECTIVE (FILE, NAME, "object");		\
    size_directive_output = 0;					\
    if (!flag_inhibit_size_directive				\
	&& DECL_SIZE (DECL)					\
	&& (size = int_size_in_bytes (TREE_TYPE (DECL))) > 0)	\
      {								\
	size_directive_output = 1;				\
        ASM_OUTPUT_SIZE_DIRECTIVE (FILE, NAME, size);		\
      }								\
    ASM_OUTPUT_LABEL(FILE, NAME);				\
  } while (0)

/* Output the size directive for a decl in rest_of_decl_compilation
   in the case where we did not do so before the initializer.
   Once we find the error_mark_node, we know that the value of
   size_directive_output was set
   by ASM_DECLARE_OBJECT_NAME when it was run for the same decl.  */

#undef  ASM_FINISH_DECLARE_OBJECT
#define ASM_FINISH_DECLARE_OBJECT(FILE, DECL, TOP_LEVEL, AT_END)	\
  do {									\
    const char *name = XSTR (XEXP (DECL_RTL (DECL), 0), 0);		\
    HOST_WIDE_INT size;							\
    if (!flag_inhibit_size_directive					\
	&& DECL_SIZE (DECL)						\
	&& ! AT_END && TOP_LEVEL					\
	&& DECL_INITIAL (DECL) == error_mark_node			\
	&& !size_directive_output					\
	&& (size = int_size_in_bytes (TREE_TYPE (DECL))) > 0)		\
      {									\
	size_directive_output = 1;					\
	ASM_OUTPUT_SIZE_DIRECTIVE (FILE, name, size);			\
      }									\
  } while (0)

/* A table of bytes codes used by the ASM_OUTPUT_ASCII and
   ASM_OUTPUT_LIMITED_STRING macros.  Each byte in the table
   corresponds to a particular byte value [0..255].  For any
   given byte value, if the value in the corresponding table
   position is zero, the given character can be output directly.
   If the table value is 1, the byte must be output as a \ooo
   octal escape.  If the tables value is anything else, then the
   byte value should be output as a \ followed by the value
   in the table.  Note that we can use standard UN*X escape
   sequences for many control characters, but we don't use
   \a to represent BEL because some svr4 assemblers (e.g. on
   the i386) don't know about that.  Also, we don't use \v
   since some versions of gas, such as 2.2 did not accept it.  */

#undef  ESCAPES
#define ESCAPES \
"\1\1\1\1\1\1\1\1btn\1fr\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\
\0\0\"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\
\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\\\0\0\0\
\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\1\
\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\
\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\
\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\
\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1"

/* Some svr4 assemblers have a limit on the number of characters which
   can appear in the operand of a .string directive.  If your assembler
   has such a limitation, you should define STRING_LIMIT to reflect that
   limit.  Note that at least some svr4 assemblers have a limit on the
   actual number of bytes in the double-quoted string, and that they
   count each character in an escape sequence as one byte.  Thus, an
   escape sequence like \377 would count as four bytes.

   If your target assembler doesn't support the .string directive, you
   should define this to zero.  */

#undef  STRING_LIMIT
#define STRING_LIMIT	((unsigned) 256)
#undef  STRING_ASM_OP
#define STRING_ASM_OP	"\t.string\t"

/* GAS is the only Alpha/ELF assembler.  */
#undef  TARGET_GAS
#define TARGET_GAS	(1)

/* Provide a STARTFILE_SPEC appropriate for ELF.  Here we add the
   (even more) magical crtbegin.o file which provides part of the
   support for getting C++ file-scope static object constructed
   before entering `main'.  */

#undef	STARTFILE_SPEC
#ifdef HAVE_LD_PIE
#define STARTFILE_SPEC \
  "%{!shared: %{pg|p:gcrt1.o%s;pie:Scrt1.o%s;:crt1.o%s}}\
   crti.o%s %{static:crtbeginT.o%s;shared|pie:crtbeginS.o%s;:crtbegin.o%s}"
#else
#define STARTFILE_SPEC \
  "%{!shared: %{pg|p:gcrt1.o%s;:crt1.o%s}}\
   crti.o%s %{static:crtbeginT.o%s;shared|pie:crtbeginS.o%s;:crtbegin.o%s}"
#endif

/* Provide a ENDFILE_SPEC appropriate for ELF.  Here we tack on the
   magical crtend.o file which provides part of the support for
   getting C++ file-scope static object constructed before entering
   `main', followed by a normal ELF "finalizer" file, `crtn.o'.  */

#undef	ENDFILE_SPEC
#define ENDFILE_SPEC \
  "%{ffast-math|funsafe-math-optimizations:crtfastmath.o%s} \
   %{shared|pie:crtendS.o%s;:crtend.o%s} crtn.o%s"

/* We support #pragma.  */
#define HANDLE_SYSV_PRAGMA 1

/* Select a format to encode pointers in exception handling data.  CODE
   is 0 for data, 1 for code labels, 2 for function pointers.  GLOBAL is
   true if the symbol may be affected by dynamic relocations.

   Since application size is already constrained to <2GB by the form of
   the ldgp relocation, we can use a 32-bit pc-relative relocation to
   static data.  Dynamic data is accessed indirectly to allow for read
   only EH sections.  */
#define ASM_PREFERRED_EH_DATA_FORMAT(CODE,GLOBAL)       \
  (((GLOBAL) ? DW_EH_PE_indirect : 0) | DW_EH_PE_pcrel | DW_EH_PE_sdata4)

/* If defined, a C statement to be executed just prior to the output of
   assembler code for INSN.  */
#define FINAL_PRESCAN_INSN(INSN, OPVEC, NOPERANDS)	\
 (alpha_this_literal_sequence_number = 0,		\
  alpha_this_gpdisp_sequence_number = 0)
extern int alpha_this_literal_sequence_number;
extern int alpha_this_gpdisp_sequence_number;

/* Since the bits of the _init and _fini function is spread across
   many object files, each potentially with its own GP, we must assume
   we need to load our GP.  Further, the .init/.fini section can
   easily be more than 4MB away from the function to call so we can't
   use bsr.  */
#define CRT_CALL_STATIC_FUNCTION(SECTION_OP, FUNC)	\
   asm (SECTION_OP "\n"					\
"	br $29,1f\n"					\
"1:	ldgp $29,0($29)\n"				\
"	unop\n"						\
"	jsr $26," USER_LABEL_PREFIX #FUNC "\n"		\
"	.align 3\n"					\
"	.previous");

/* If we have the capability create headers for efficient EH lookup.
   As of Jan 2002, only glibc 2.2.4 can actually make use of this, but
   I imagine that other systems will catch up.  In the meantime, it
   doesn't harm to make sure that the data exists to be used later.  */
#if defined(HAVE_LD_EH_FRAME_HDR)
#define LINK_EH_SPEC "%{!static:--eh-frame-hdr} "
#endif
