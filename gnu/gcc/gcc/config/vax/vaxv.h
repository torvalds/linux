/* Definitions of target machine for GNU compiler.  VAX sysV version.
   Copyright (C) 1988, 1993, 1996, 2000, 2002 Free Software Foundation, Inc.

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

#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define_std ("unix");		\
      builtin_assert ("system=svr3");		\
						\
      builtin_define_std ("vax");		\
      if (TARGET_G_FLOAT)			\
	builtin_define_std ("GFLOAT");		\
    }						\
  while (0)

/* Output #ident as a .ident.  */

#define ASM_OUTPUT_IDENT(FILE, NAME) fprintf (FILE, "\t.ident \"%s\"\n", NAME);

#undef DBX_DEBUGGING_INFO
#define SDB_DEBUGGING_INFO 1

#undef LIB_SPEC

/* The .file command should always begin the output.  */
#define TARGET_ASM_FILE_START_FILE_DIRECTIVE true

#undef ASM_OUTPUT_ALIGN
#define ASM_OUTPUT_ALIGN(FILE,LOG) \
  fprintf(FILE, "\t.align %d\n", 1 << (LOG))

#undef ASM_OUTPUT_LOCAL
#define ASM_OUTPUT_LOCAL(FILE,NAME,SIZE,ROUNDED)	\
( switch_to_section (data_section),			\
  assemble_name ((FILE), (NAME)),			\
  fprintf ((FILE), ":\n\t.space %u\n", (int)(ROUNDED)))

#define ASM_OUTPUT_ASCII(FILE,PTR,LEN)			\
do {							\
  const unsigned char *s = (const unsigned char *)(PTR);\
  size_t i, limit = (LEN);				\
  for (i = 0; i < limit; s++, i++)		\
    {							\
      if ((i % 8) == 0)					\
	fputs ("\n\t.byte\t", (FILE));			\
      fprintf ((FILE), "%s0x%x", (i%8?",":""), (unsigned)*s); \
    }							\
  fputs ("\n", (FILE));					\
} while (0)
