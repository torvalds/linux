/* Common configuration file for NetBSD a.out targets.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Contributed by Wasabi Systems, Inc.

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

/* TARGET_OS_CPP_BUILTINS() common to all NetBSD a.out targets.  */
#define NETBSD_OS_CPP_BUILTINS_AOUT()		\
  do						\
    {						\
      NETBSD_OS_CPP_BUILTINS_COMMON();		\
    }						\
  while (0)

/* This defines which switch letters take arguments.  */

#undef SWITCH_TAKES_ARG
#define SWITCH_TAKES_ARG(CHAR)		\
  (DEFAULT_SWITCH_TAKES_ARG(CHAR)	\
   || (CHAR) == 'R')


/* Provide an ASM_SPEC appropriate for NetBSD.  Currently we only deal
   with the options for generating PIC code.  */

#undef ASM_SPEC
#define ASM_SPEC "%{fpic|fpie:-k} %{fPIC|fPIE:-k -K}"

#define AS_NEEDS_DASH_FOR_PIPED_INPUT


/* Provide a STARTFILE_SPEC appropriate for NetBSD a.out.  Here we
   provide support for the special GCC option -static.  */

#undef STARTFILE_SPEC
#define STARTFILE_SPEC			\
  "%{!shared:				\
     %{pg:gcrt0%O%s}			\
     %{!pg:				\
       %{p:mcrt0%O%s}			\
       %{!p:				\
	 %{!static:crt0%O%s}		\
	 %{static:scrt0%O%s}}}}"

/* Provide a LINK_SPEC appropriate for NetBSD a.out.  Here we provide
   support for the special GCC options -static, -assert, and -nostdlib.  */

#undef NETBSD_LINK_SPEC_AOUT
#define NETBSD_LINK_SPEC_AOUT		\
  "%{nostdlib:-nostdlib}		\
   %{!shared:				\
     %{!nostdlib:			\
       %{!r*:				\
	 %{!e*:-e start}}}		\
     -dc -dp				\
     %{static:-Bstatic}}		\
   %{shared:-Bshareable}		\
   %{R*}				\
   %{assert*}"

/* Default LINK_SPEC.  */
#undef LINK_SPEC
#define LINK_SPEC NETBSD_LINK_SPEC_AOUT

/* Some imports from svr4.h in support of shared libraries.  */

/* Define the strings used for the .type, .size, and .set directives.
   These strings generally do not vary from one system running NetBSD
   to another, but if a given system needs to use different pseudo-op
   names for these, they may be overridden in the file included after
   this one.  */

#undef TYPE_ASM_OP
#undef SIZE_ASM_OP
#undef SET_ASM_OP                
#define TYPE_ASM_OP	"\t.type\t"
#define SIZE_ASM_OP	"\t.size\t"
#define SET_ASM_OP	"\t.set\t"


/* This is how we tell the assembler that a symbol is weak.  */

#undef ASM_WEAKEN_LABEL
#define ASM_WEAKEN_LABEL(FILE,NAME)					\
  do 									\
    {									\
      fputs ("\t.globl\t", FILE); assemble_name (FILE, NAME);		\
      fputc ('\n', FILE);						\
      fputs ("\t.weak\t", FILE); assemble_name (FILE, NAME);		\
      fputc ('\n', FILE);						\
    }									\
  while (0)


/* The following macro defines the format used to output the second
   operand of the .type assembler directive.  Different svr4 assemblers
   expect various different forms of this operand.  The one given here
   is just a default.  You may need to override it in your machine-
   specific tm.h file (depending on the particulars of your assembler).  */

#undef TYPE_OPERAND_FMT
#define TYPE_OPERAND_FMT	"@%s"


/* Write the extra assembler code needed to declare a function's result.
   Most svr4 assemblers don't require any special declaration of the
   result value, but there are exceptions.  */

#ifndef ASM_DECLARE_RESULT
#define ASM_DECLARE_RESULT(FILE, RESULT)
#endif


/* These macros generate the special .type and .size directives which
   are used to set the corresponding fields of the linker symbol table
   entries in an ELF object file under SVR4 (and a.out on NetBSD).
   These macros also output the starting labels for the relevant
   functions/objects.  */

/* Write the extra assembler code needed to declare a function properly.
   Some svr4 assemblers need to also have something extra said about the
   function's return value.  We allow for that here.  */

#undef ASM_DECLARE_FUNCTION_NAME
#define ASM_DECLARE_FUNCTION_NAME(FILE, NAME, DECL)			\
  do									\
    {									\
      ASM_OUTPUT_TYPE_DIRECTIVE (FILE, NAME, "function");		\
      ASM_DECLARE_RESULT (FILE, DECL_RESULT (DECL));			\
      ASM_OUTPUT_LABEL(FILE, NAME);					\
    }									\
  while (0)


/* Write the extra assembler code needed to declare an object properly.  */

#define ASM_DECLARE_OBJECT_NAME(FILE, NAME, DECL)		\
  do								\
    {								\
      HOST_WIDE_INT size;					\
								\
      ASM_OUTPUT_TYPE_DIRECTIVE (FILE, NAME, "object");		\
								\
      size_directive_output = 0;				\
      if (!flag_inhibit_size_directive				\
	  && (DECL) && DECL_SIZE (DECL))			\
	{							\
	  size_directive_output = 1;				\
	  size = int_size_in_bytes (TREE_TYPE (DECL));		\
	  ASM_OUTPUT_SIZE_DIRECTIVE (FILE, NAME, size);		\
	}							\
								\
      ASM_OUTPUT_LABEL (FILE, NAME);				\
    }								\
  while (0)

/* Output the size directive for a decl in rest_of_decl_compilation
   in the case where we did not do so before the initializer.
   Once we find the error_mark_node, we know that the value of
   size_directive_output was set
   by ASM_DECLARE_OBJECT_NAME when it was run for the same decl.  */

#undef ASM_FINISH_DECLARE_OBJECT
#define ASM_FINISH_DECLARE_OBJECT(FILE, DECL, TOP_LEVEL, AT_END)	\
  do									\
    {									\
      const char *name = XSTR (XEXP (DECL_RTL (DECL), 0), 0);		\
      HOST_WIDE_INT size;						\
      if (!flag_inhibit_size_directive && DECL_SIZE (DECL)		\
	  && ! AT_END && TOP_LEVEL					\
	  && DECL_INITIAL (DECL) == error_mark_node			\
	  && !size_directive_output)					\
	{								\
	  size_directive_output = 1;					\
	  size = int_size_in_bytes (TREE_TYPE (DECL));			\
	  ASM_OUTPUT_SIZE_DIRECTIVE (FILE, name, size);			\
	}								\
    }									\
  while (0)


/* This is how to declare the size of a function.  */

#undef ASM_DECLARE_FUNCTION_SIZE
#define ASM_DECLARE_FUNCTION_SIZE(FILE, FNAME, DECL)			\
  do									\
    {									\
      if (!flag_inhibit_size_directive)					\
	ASM_OUTPUT_MEASURED_SIZE (FILE, FNAME);				\
    }									\
  while (0)
