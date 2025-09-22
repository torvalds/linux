/* Generate from machine description:
   - some macros CODE_FOR_... giving the insn_code_number value
   for each of the defined standard insn names.
   Copyright (C) 1987, 1991, 1995, 1998,
   1999, 2000, 2001, 2003, 2004 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */


#include "bconfig.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "errors.h"
#include "gensupport.h"

static void
gen_insn (rtx insn, int code)
{
  const char *name = XSTR (insn, 0);
  int truth = maybe_eval_c_test (XSTR (insn, 2));

  /* Don't mention instructions whose names are the null string
     or begin with '*'.  They are in the machine description just
     to be recognized.  */
  if (name[0] != 0 && name[0] != '*')
    {
      if (truth == 0)
	printf ("#define CODE_FOR_%s CODE_FOR_nothing\n", name);
      else
	printf ("  CODE_FOR_%s = %d,\n", name, code);
    }
}

int
main (int argc, char **argv)
{
  rtx desc;

  progname = "gencodes";

  /* We need to see all the possibilities.  Elided insns may have
     direct references to CODE_FOR_xxx in C code.  */
  insn_elision = 0;

  if (init_md_reader_args (argc, argv) != SUCCESS_EXIT_CODE)
    return (FATAL_EXIT_CODE);

  puts ("\
/* Generated automatically by the program `gencodes'\n\
   from the machine description file `md'.  */\n\
\n\
#ifndef GCC_INSN_CODES_H\n\
#define GCC_INSN_CODES_H\n\
\n\
enum insn_code {");

  /* Read the machine description.  */

  while (1)
    {
      int line_no;
      int insn_code_number;

      desc = read_md_rtx (&line_no, &insn_code_number);
      if (desc == NULL)
	break;

      if (GET_CODE (desc) == DEFINE_INSN || GET_CODE (desc) == DEFINE_EXPAND)
	gen_insn (desc, insn_code_number);
    }

  puts ("  CODE_FOR_nothing\n\
};\n\
\n\
#endif /* GCC_INSN_CODES_H */");

  if (ferror (stdout) || fflush (stdout) || fclose (stdout))
    return FATAL_EXIT_CODE;

  return SUCCESS_EXIT_CODE;
}
