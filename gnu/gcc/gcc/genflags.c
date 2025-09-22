/* Generate from machine description:
   - some flags HAVE_... saying which simple standard instructions are
   available for this machine.
   Copyright (C) 1987, 1991, 1995, 1998,
   1999, 2000, 2003, 2004 Free Software Foundation, Inc.

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
#include "obstack.h"
#include "errors.h"
#include "gensupport.h"

/* Obstack to remember insns with.  */
static struct obstack obstack;

/* Max size of names encountered.  */
static int max_id_len;

/* Max operand encountered in a scan over some insn.  */
static int max_opno;

static void max_operand_1 (rtx);
static int num_operands (rtx);
static void gen_proto (rtx);
static void gen_macro (const char *, int, int);
static void gen_insn (rtx);

/* Count the number of match_operand's found.  */

static void
max_operand_1 (rtx x)
{
  RTX_CODE code;
  int i;
  int len;
  const char *fmt;

  if (x == 0)
    return;

  code = GET_CODE (x);

  if (code == MATCH_OPERAND || code == MATCH_OPERATOR
      || code == MATCH_PARALLEL)
    max_opno = MAX (max_opno, XINT (x, 0));

  fmt = GET_RTX_FORMAT (code);
  len = GET_RTX_LENGTH (code);
  for (i = 0; i < len; i++)
    {
      if (fmt[i] == 'e' || fmt[i] == 'u')
	max_operand_1 (XEXP (x, i));
      else if (fmt[i] == 'E')
	{
	  int j;
	  for (j = 0; j < XVECLEN (x, i); j++)
	    max_operand_1 (XVECEXP (x, i, j));
	}
    }
}

static int
num_operands (rtx insn)
{
  int len = XVECLEN (insn, 1);
  int i;

  max_opno = -1;

  for (i = 0; i < len; i++)
    max_operand_1 (XVECEXP (insn, 1, i));

  return max_opno + 1;
}

/* Print out a wrapper macro for a function which corrects the number
   of arguments it takes.  Any missing arguments are assumed to be at
   the end.  */
static void
gen_macro (const char *name, int real, int expect)
{
  int i;

  gcc_assert (real <= expect);
  gcc_assert (real);

  /* #define GEN_CALL(A, B, C, D) gen_call((A), (B)) */
  fputs ("#define GEN_", stdout);
  for (i = 0; name[i]; i++)
    putchar (TOUPPER (name[i]));

  putchar('(');
  for (i = 0; i < expect - 1; i++)
    printf ("%c, ", i + 'A');
  printf ("%c) gen_%s (", i + 'A', name);

  for (i = 0; i < real - 1; i++)
    printf ("(%c), ", i + 'A');
  printf ("(%c))\n", i + 'A');
}

/* Print out prototype information for a generator function.  If the
   insn pattern has been elided, print out a dummy generator that
   does nothing.  */

static void
gen_proto (rtx insn)
{
  int num = num_operands (insn);
  int i;
  const char *name = XSTR (insn, 0);
  int truth = maybe_eval_c_test (XSTR (insn, 2));

  /* Many md files don't refer to the last two operands passed to the
     call patterns.  This means their generator functions will be two
     arguments too short.  Instead of changing every md file to touch
     those operands, we wrap the prototypes in macros that take the
     correct number of arguments.  */
  if (name[0] == 'c' || name[0] == 's')
    {
      if (!strcmp (name, "call")
	  || !strcmp (name, "call_pop")
	  || !strcmp (name, "sibcall")
	  || !strcmp (name, "sibcall_pop"))
	gen_macro (name, num, 4);
      else if (!strcmp (name, "call_value")
	       || !strcmp (name, "call_value_pop")
	       || !strcmp (name, "sibcall_value")
	       || !strcmp (name, "sibcall_value_pop"))
	gen_macro (name, num, 5);
    }

  if (truth != 0)
    printf ("extern rtx        gen_%-*s (", max_id_len, name);
  else
    printf ("static inline rtx gen_%-*s (", max_id_len, name);

  if (num == 0)
    fputs ("void", stdout);
  else
    {
      for (i = 1; i < num; i++)
	fputs ("rtx, ", stdout);

      fputs ("rtx", stdout);
    }

  puts (");");

  /* Some back ends want to take the address of generator functions,
     so we cannot simply use #define for these dummy definitions.  */
  if (truth == 0)
    {
      printf ("static inline rtx\ngen_%s", name);
      if (num > 0)
	{
	  putchar ('(');
	  for (i = 0; i < num-1; i++)
	    printf ("rtx ARG_UNUSED (%c), ", 'a' + i);
	  printf ("rtx ARG_UNUSED (%c))\n", 'a' + i);
	}
      else
	puts ("(void)");
      puts ("{\n  return 0;\n}");
    }

}

static void
gen_insn (rtx insn)
{
  const char *name = XSTR (insn, 0);
  const char *p;
  int len;
  int truth = maybe_eval_c_test (XSTR (insn, 2));

  /* Don't mention instructions whose names are the null string
     or begin with '*'.  They are in the machine description just
     to be recognized.  */
  if (name[0] == 0 || name[0] == '*')
    return;

  len = strlen (name);

  if (len > max_id_len)
    max_id_len = len;

  if (truth == 0)
    /* Emit nothing.  */;
  else if (truth == 1)
    printf ("#define HAVE_%s 1\n", name);
  else
    {
      /* Write the macro definition, putting \'s at the end of each line,
	 if more than one.  */
      printf ("#define HAVE_%s (", name);
      for (p = XSTR (insn, 2); *p; p++)
	{
	  if (IS_VSPACE (*p))
	    fputs (" \\\n", stdout);
	  else
	    putchar (*p);
	}
      fputs (")\n", stdout);
    }

  obstack_grow (&obstack, &insn, sizeof (rtx));
}

int
main (int argc, char **argv)
{
  rtx desc;
  rtx dummy;
  rtx *insns;
  rtx *insn_ptr;

  progname = "genflags";
  obstack_init (&obstack);

  /* We need to see all the possibilities.  Elided insns may have
     direct calls to their generators in C code.  */
  insn_elision = 0;

  if (init_md_reader_args (argc, argv) != SUCCESS_EXIT_CODE)
    return (FATAL_EXIT_CODE);

  puts ("/* Generated automatically by the program `genflags'");
  puts ("   from the machine description file `md'.  */\n");
  puts ("#ifndef GCC_INSN_FLAGS_H");
  puts ("#define GCC_INSN_FLAGS_H\n");

  /* Read the machine description.  */

  while (1)
    {
      int line_no, insn_code_number = 0;

      desc = read_md_rtx (&line_no, &insn_code_number);
      if (desc == NULL)
	break;
      if (GET_CODE (desc) == DEFINE_INSN || GET_CODE (desc) == DEFINE_EXPAND)
	gen_insn (desc);
    }

  /* Print out the prototypes now.  */
  dummy = (rtx) 0;
  obstack_grow (&obstack, &dummy, sizeof (rtx));
  insns = XOBFINISH (&obstack, rtx *);

  for (insn_ptr = insns; *insn_ptr; insn_ptr++)
    gen_proto (*insn_ptr);

  puts("\n#endif /* GCC_INSN_FLAGS_H */");

  if (ferror (stdout) || fflush (stdout) || fclose (stdout))
    return FATAL_EXIT_CODE;

  return SUCCESS_EXIT_CODE;
}
