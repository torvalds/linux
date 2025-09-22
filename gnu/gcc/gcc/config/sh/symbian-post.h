/* Definitions for the Symbian OS running on an SH part.
   This file is included after all the other target specific headers.

   Copyright (C) 2004 Free Software Foundation, Inc.
   Contributed by Red Hat.

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
   along with GCC; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#undef  TARGET_VERSION
#define TARGET_VERSION \
  fputs (" (Renesas SH for Symbian OS)", stderr);

#undef  LINK_EMUL_PREFIX
#define LINK_EMUL_PREFIX "shlsymbian"


#define SYMBIAN_EXPORT_NAME(NAME,FILE,DECL)			\
  do								\
    {								\
      if ((DECL && sh_symbian_dllexport_p (DECL))		\
         || sh_symbian_dllexport_name_p (NAME))			\
        {							\
          fprintf ((FILE), "\t.pushsection .directive\n");	\
          fprintf ((FILE), "\t.asciz \"EXPORT %s\\n\"\n",	\
	           sh_symbian_strip_name_encoding (NAME));	\
          fprintf ((FILE), "\t.popsection\n");			\
       }							\
    }								\
  while (0)

/* Output a function definition label.  */
#undef  ASM_DECLARE_FUNCTION_NAME
#define ASM_DECLARE_FUNCTION_NAME(FILE, NAME, DECL)		\
  do								\
    {								\
      SYMBIAN_EXPORT_NAME ((NAME), (FILE), (DECL));		\
      ASM_OUTPUT_TYPE_DIRECTIVE ((FILE), (NAME), "function");	\
      ASM_DECLARE_RESULT ((FILE), DECL_RESULT (DECL));		\
      ASM_OUTPUT_LABEL ((FILE), (NAME));			\
    }								\
  while (0)

/* Output the label for an initialized variable.  */
#undef  ASM_DECLARE_OBJECT_NAME
#define ASM_DECLARE_OBJECT_NAME(FILE, NAME, DECL)		\
  do								\
    {								\
      HOST_WIDE_INT size;					\
								\
      SYMBIAN_EXPORT_NAME ((NAME), (FILE), (DECL));		\
      ASM_OUTPUT_TYPE_DIRECTIVE ((FILE), (NAME), "object");	\
								\
      size_directive_output = 0;				\
      if (!flag_inhibit_size_directive				\
	  && (DECL)						\
          && DECL_SIZE (DECL))					\
	{							\
	  size_directive_output = 1;				\
	  size = int_size_in_bytes (TREE_TYPE (DECL));		\
	  ASM_OUTPUT_SIZE_DIRECTIVE ((FILE), (NAME), size);	\
	}							\
								\
      ASM_OUTPUT_LABEL ((FILE), (NAME));			\
    }								\
  while (0)

#undef  ASM_OUTPUT_LABELREF
#define ASM_OUTPUT_LABELREF(FILE, NAME)				\
  do								\
    {								\
      asm_fprintf ((FILE), "%U%s",				\
		   sh_symbian_strip_name_encoding (NAME));	\
    }								\
  while (0)
