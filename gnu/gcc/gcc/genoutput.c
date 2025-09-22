/* Generate code from to output assembler insns as recognized from rtl.
   Copyright (C) 1987, 1988, 1992, 1994, 1995, 1997, 1998, 1999, 2000, 2002,
   2003, 2004, 2005 Free Software Foundation, Inc.

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


/* This program reads the machine description for the compiler target machine
   and produces a file containing these things:

   1. An array of `struct insn_data', which is indexed by insn code number,
   which contains:

     a. `name' is the name for that pattern.  Nameless patterns are
     given a name.

     b. `output' hold either the output template, an array of output
     templates, or an output function.

     c. `genfun' is the function to generate a body for that pattern,
     given operands as arguments.

     d. `n_operands' is the number of distinct operands in the pattern
     for that insn,

     e. `n_dups' is the number of match_dup's that appear in the insn's
     pattern.  This says how many elements of `recog_data.dup_loc' are
     significant after an insn has been recognized.

     f. `n_alternatives' is the number of alternatives in the constraints
     of each pattern.

     g. `output_format' tells what type of thing `output' is.

     h. `operand' is the base of an array of operand data for the insn.

   2. An array of `struct insn_operand data', used by `operand' above.

     a. `predicate', an int-valued function, is the match_operand predicate
     for this operand.

     b. `constraint' is the constraint for this operand.

     c. `address_p' indicates that the operand appears within ADDRESS
     rtx's.

     d. `mode' is the machine mode that that operand is supposed to have.

     e. `strict_low', is nonzero for operands contained in a STRICT_LOW_PART.

     f. `eliminable', is nonzero for operands that are matched normally by
     MATCH_OPERAND; it is zero for operands that should not be changed during
     register elimination such as MATCH_OPERATORs.

  The code number of an insn is simply its position in the machine
  description; code numbers are assigned sequentially to entries in
  the description, starting with code number 0.

  Thus, the following entry in the machine description

    (define_insn "clrdf"
      [(set (match_operand:DF 0 "general_operand" "")
	    (const_int 0))]
      ""
      "clrd %0")

  assuming it is the 25th entry present, would cause
  insn_data[24].template to be "clrd %0", and
  insn_data[24].n_operands to be 1.  */

#include "bconfig.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "errors.h"
#include "gensupport.h"

/* No instruction can have more operands than this.  Sorry for this
   arbitrary limit, but what machine will have an instruction with
   this many operands?  */

#define MAX_MAX_OPERANDS 40

static int n_occurrences		(int, const char *);
static const char *strip_whitespace	(const char *);

/* insns in the machine description are assigned sequential code numbers
   that are used by insn-recog.c (produced by genrecog) to communicate
   to insn-output.c (produced by this program).  */

static int next_code_number;

/* This counts all definitions in the md file,
   for the sake of error messages.  */

static int next_index_number;

/* This counts all operands used in the md file.  The first is null.  */

static int next_operand_number = 1;

/* Record in this chain all information about the operands we will output.  */

struct operand_data
{
  struct operand_data *next;
  int index;
  const char *predicate;
  const char *constraint;
  enum machine_mode mode;
  unsigned char n_alternatives;
  char address_p;
  char strict_low;
  char eliminable;
  char seen;
};

/* Begin with a null operand at index 0.  */

static struct operand_data null_operand =
{
  0, 0, "", "", VOIDmode, 0, 0, 0, 0, 0
};

static struct operand_data *odata = &null_operand;
static struct operand_data **odata_end = &null_operand.next;

/* Must match the constants in recog.h.  */

#define INSN_OUTPUT_FORMAT_NONE         0       /* abort */
#define INSN_OUTPUT_FORMAT_SINGLE       1       /* const char * */
#define INSN_OUTPUT_FORMAT_MULTI        2       /* const char * const * */
#define INSN_OUTPUT_FORMAT_FUNCTION     3       /* const char * (*)(...) */

/* Record in this chain all information that we will output,
   associated with the code number of the insn.  */

struct data
{
  struct data *next;
  const char *name;
  const char *template;
  int code_number;
  int index_number;
  const char *filename;
  int lineno;
  int n_operands;		/* Number of operands this insn recognizes */
  int n_dups;			/* Number times match_dup appears in pattern */
  int n_alternatives;		/* Number of alternatives in each constraint */
  int operand_number;		/* Operand index in the big array.  */
  int output_format;		/* INSN_OUTPUT_FORMAT_*.  */
  struct operand_data operand[MAX_MAX_OPERANDS];
};

/* This variable points to the first link in the insn chain.  */

static struct data *idata, **idata_end = &idata;

static void output_prologue (void);
static void output_operand_data (void);
static void output_insn_data (void);
static void output_get_insn_name (void);
static void scan_operands (struct data *, rtx, int, int);
static int compare_operands (struct operand_data *,
			     struct operand_data *);
static void place_operands (struct data *);
static void process_template (struct data *, const char *);
static void validate_insn_alternatives (struct data *);
static void validate_insn_operands (struct data *);
static void gen_insn (rtx, int);
static void gen_peephole (rtx, int);
static void gen_expand (rtx, int);
static void gen_split (rtx, int);

#ifdef USE_MD_CONSTRAINTS

struct constraint_data
{
  struct constraint_data *next_this_letter;
  int lineno;
  unsigned int namelen;
  const char name[1];
};

/* This is a complete list (unlike the one in genpreds.c) of constraint
   letters and modifiers with machine-independent meaning.  The only
   omission is digits, as these are handled specially.  */
static const char indep_constraints[] = ",=+%*?!#&<>EFVXgimnoprs";

static struct constraint_data *
constraints_by_letter_table[1 << CHAR_BIT];

static int mdep_constraint_len (const char *, int, int);
static void note_constraint (rtx, int);

#else  /* !USE_MD_CONSTRAINTS */

static void check_constraint_len (void);
static int constraint_len (const char *, int);

#endif /* !USE_MD_CONSTRAINTS */


static void
output_prologue (void)
{
  printf ("/* Generated automatically by the program `genoutput'\n\
   from the machine description file `md'.  */\n\n");

  printf ("#include \"config.h\"\n");
  printf ("#include \"system.h\"\n");
  printf ("#include \"coretypes.h\"\n");
  printf ("#include \"tm.h\"\n");
  printf ("#include \"flags.h\"\n");
  printf ("#include \"ggc.h\"\n");
  printf ("#include \"rtl.h\"\n");
  printf ("#include \"expr.h\"\n");
  printf ("#include \"insn-codes.h\"\n");
  printf ("#include \"tm_p.h\"\n");
  printf ("#include \"function.h\"\n");
  printf ("#include \"regs.h\"\n");
  printf ("#include \"hard-reg-set.h\"\n");
  printf ("#include \"real.h\"\n");
  printf ("#include \"insn-config.h\"\n\n");
  printf ("#include \"conditions.h\"\n");
  printf ("#include \"insn-attr.h\"\n\n");
  printf ("#include \"recog.h\"\n\n");
  printf ("#include \"toplev.h\"\n");
  printf ("#include \"output.h\"\n");
  printf ("#include \"target.h\"\n");
  printf ("#include \"tm-constrs.h\"\n");
}

static void
output_operand_data (void)
{
  struct operand_data *d;

  printf ("\nstatic const struct insn_operand_data operand_data[] = \n{\n");

  for (d = odata; d; d = d->next)
    {
      printf ("  {\n");

      printf ("    %s,\n",
	      d->predicate && d->predicate[0] ? d->predicate : "0");

      printf ("    \"%s\",\n", d->constraint ? d->constraint : "");

      printf ("    %smode,\n", GET_MODE_NAME (d->mode));

      printf ("    %d,\n", d->strict_low);

      printf ("    %d\n", d->eliminable);

      printf("  },\n");
    }
  printf("};\n\n\n");
}

static void
output_insn_data (void)
{
  struct data *d;
  int name_offset = 0;
  int next_name_offset;
  const char * last_name = 0;
  const char * next_name = 0;
  struct data *n;

  for (n = idata, next_name_offset = 1; n; n = n->next, next_name_offset++)
    if (n->name)
      {
	next_name = n->name;
	break;
      }

  printf ("#if GCC_VERSION >= 2007\n__extension__\n#endif\n");
  printf ("\nconst struct insn_data insn_data[] = \n{\n");

  for (d = idata; d; d = d->next)
    {
      printf ("  /* %s:%d */\n", d->filename, d->lineno);
      printf ("  {\n");

      if (d->name)
	{
	  printf ("    \"%s\",\n", d->name);
	  name_offset = 0;
	  last_name = d->name;
	  next_name = 0;
	  for (n = d->next, next_name_offset = 1; n;
	       n = n->next, next_name_offset++)
	    {
	      if (n->name)
		{
		  next_name = n->name;
		  break;
		}
	    }
	}
      else
	{
	  name_offset++;
	  if (next_name && (last_name == 0
			    || name_offset > next_name_offset / 2))
	    printf ("    \"%s-%d\",\n", next_name,
		    next_name_offset - name_offset);
	  else
	    printf ("    \"%s+%d\",\n", last_name, name_offset);
	}

      switch (d->output_format)
	{
	case INSN_OUTPUT_FORMAT_NONE:
	  printf ("#if HAVE_DESIGNATED_INITIALIZERS\n");
	  printf ("    { 0 },\n");
	  printf ("#else\n");
	  printf ("    { 0, 0, 0 },\n");
	  printf ("#endif\n");
	  break;
	case INSN_OUTPUT_FORMAT_SINGLE:
	  {
	    const char *p = d->template;
	    char prev = 0;

	    printf ("#if HAVE_DESIGNATED_INITIALIZERS\n");
	    printf ("    { .single =\n");
	    printf ("#else\n");
	    printf ("    {\n");
	    printf ("#endif\n");
	    printf ("    \"");
	    while (*p)
	      {
		if (IS_VSPACE (*p) && prev != '\\')
		  {
		    /* Preserve two consecutive \n's or \r's, but treat \r\n
		       as a single newline.  */
		    if (*p == '\n' && prev != '\r')
		      printf ("\\n\\\n");
		  }
		else
		  putchar (*p);
		prev = *p;
		++p;
	      }
	    printf ("\",\n");
	    printf ("#if HAVE_DESIGNATED_INITIALIZERS\n");
	    printf ("    },\n");
	    printf ("#else\n");
	    printf ("    0, 0 },\n");
	    printf ("#endif\n");
	  }
	  break;
	case INSN_OUTPUT_FORMAT_MULTI:
	  printf ("#if HAVE_DESIGNATED_INITIALIZERS\n");
	  printf ("    { .multi = output_%d },\n", d->code_number);
	  printf ("#else\n");
	  printf ("    { 0, output_%d, 0 },\n", d->code_number);
	  printf ("#endif\n");
	  break;
	case INSN_OUTPUT_FORMAT_FUNCTION:
	  printf ("#if HAVE_DESIGNATED_INITIALIZERS\n");
	  printf ("    { .function = output_%d },\n", d->code_number);
	  printf ("#else\n");
	  printf ("    { 0, 0, output_%d },\n", d->code_number);
	  printf ("#endif\n");
	  break;
	default:
	  gcc_unreachable ();
	}

      if (d->name && d->name[0] != '*')
	printf ("    (insn_gen_fn) gen_%s,\n", d->name);
      else
	printf ("    0,\n");

      printf ("    &operand_data[%d],\n", d->operand_number);
      printf ("    %d,\n", d->n_operands);
      printf ("    %d,\n", d->n_dups);
      printf ("    %d,\n", d->n_alternatives);
      printf ("    %d\n", d->output_format);

      printf("  },\n");
    }
  printf ("};\n\n\n");
}

static void
output_get_insn_name (void)
{
  printf ("const char *\n");
  printf ("get_insn_name (int code)\n");
  printf ("{\n");
  printf ("  if (code == NOOP_MOVE_INSN_CODE)\n");
  printf ("    return \"NOOP_MOVE\";\n");
  printf ("  else\n");
  printf ("    return insn_data[code].name;\n");
  printf ("}\n");
}


/* Stores in max_opno the largest operand number present in `part', if
   that is larger than the previous value of max_opno, and the rest of
   the operand data into `d->operand[i]'.

   THIS_ADDRESS_P is nonzero if the containing rtx was an ADDRESS.
   THIS_STRICT_LOW is nonzero if the containing rtx was a STRICT_LOW_PART.  */

static int max_opno;
static int num_dups;

static void
scan_operands (struct data *d, rtx part, int this_address_p,
	       int this_strict_low)
{
  int i, j;
  const char *format_ptr;
  int opno;

  if (part == 0)
    return;

  switch (GET_CODE (part))
    {
    case MATCH_OPERAND:
      opno = XINT (part, 0);
      if (opno > max_opno)
	max_opno = opno;
      if (max_opno >= MAX_MAX_OPERANDS)
	{
	  message_with_line (d->lineno,
			     "maximum number of operands exceeded");
	  have_error = 1;
	  return;
	}
      if (d->operand[opno].seen)
	{
	  message_with_line (d->lineno,
			     "repeated operand number %d\n", opno);
	  have_error = 1;
	}

      d->operand[opno].seen = 1;
      d->operand[opno].mode = GET_MODE (part);
      d->operand[opno].strict_low = this_strict_low;
      d->operand[opno].predicate = XSTR (part, 1);
      d->operand[opno].constraint = strip_whitespace (XSTR (part, 2));
      d->operand[opno].n_alternatives
	= n_occurrences (',', d->operand[opno].constraint) + 1;
      d->operand[opno].address_p = this_address_p;
      d->operand[opno].eliminable = 1;
      return;

    case MATCH_SCRATCH:
      opno = XINT (part, 0);
      if (opno > max_opno)
	max_opno = opno;
      if (max_opno >= MAX_MAX_OPERANDS)
	{
	  message_with_line (d->lineno,
			     "maximum number of operands exceeded");
	  have_error = 1;
	  return;
	}
      if (d->operand[opno].seen)
	{
	  message_with_line (d->lineno,
			     "repeated operand number %d\n", opno);
	  have_error = 1;
	}

      d->operand[opno].seen = 1;
      d->operand[opno].mode = GET_MODE (part);
      d->operand[opno].strict_low = 0;
      d->operand[opno].predicate = "scratch_operand";
      d->operand[opno].constraint = strip_whitespace (XSTR (part, 1));
      d->operand[opno].n_alternatives
	= n_occurrences (',', d->operand[opno].constraint) + 1;
      d->operand[opno].address_p = 0;
      d->operand[opno].eliminable = 0;
      return;

    case MATCH_OPERATOR:
    case MATCH_PARALLEL:
      opno = XINT (part, 0);
      if (opno > max_opno)
	max_opno = opno;
      if (max_opno >= MAX_MAX_OPERANDS)
	{
	  message_with_line (d->lineno,
			     "maximum number of operands exceeded");
	  have_error = 1;
	  return;
	}
      if (d->operand[opno].seen)
	{
	  message_with_line (d->lineno,
			     "repeated operand number %d\n", opno);
	  have_error = 1;
	}

      d->operand[opno].seen = 1;
      d->operand[opno].mode = GET_MODE (part);
      d->operand[opno].strict_low = 0;
      d->operand[opno].predicate = XSTR (part, 1);
      d->operand[opno].constraint = 0;
      d->operand[opno].address_p = 0;
      d->operand[opno].eliminable = 0;
      for (i = 0; i < XVECLEN (part, 2); i++)
	scan_operands (d, XVECEXP (part, 2, i), 0, 0);
      return;

    case MATCH_DUP:
    case MATCH_OP_DUP:
    case MATCH_PAR_DUP:
      ++num_dups;
      break;

    case ADDRESS:
      scan_operands (d, XEXP (part, 0), 1, 0);
      return;

    case STRICT_LOW_PART:
      scan_operands (d, XEXP (part, 0), 0, 1);
      return;

    default:
      break;
    }

  format_ptr = GET_RTX_FORMAT (GET_CODE (part));

  for (i = 0; i < GET_RTX_LENGTH (GET_CODE (part)); i++)
    switch (*format_ptr++)
      {
      case 'e':
      case 'u':
	scan_operands (d, XEXP (part, i), 0, 0);
	break;
      case 'E':
	if (XVEC (part, i) != NULL)
	  for (j = 0; j < XVECLEN (part, i); j++)
	    scan_operands (d, XVECEXP (part, i, j), 0, 0);
	break;
      }
}

/* Compare two operands for content equality.  */

static int
compare_operands (struct operand_data *d0, struct operand_data *d1)
{
  const char *p0, *p1;

  p0 = d0->predicate;
  if (!p0)
    p0 = "";
  p1 = d1->predicate;
  if (!p1)
    p1 = "";
  if (strcmp (p0, p1) != 0)
    return 0;

  p0 = d0->constraint;
  if (!p0)
    p0 = "";
  p1 = d1->constraint;
  if (!p1)
    p1 = "";
  if (strcmp (p0, p1) != 0)
    return 0;

  if (d0->mode != d1->mode)
    return 0;

  if (d0->strict_low != d1->strict_low)
    return 0;

  if (d0->eliminable != d1->eliminable)
    return 0;

  return 1;
}

/* Scan the list of operands we've already committed to output and either
   find a subsequence that is the same, or allocate a new one at the end.  */

static void
place_operands (struct data *d)
{
  struct operand_data *od, *od2;
  int i;

  if (d->n_operands == 0)
    {
      d->operand_number = 0;
      return;
    }

  /* Brute force substring search.  */
  for (od = odata, i = 0; od; od = od->next, i = 0)
    if (compare_operands (od, &d->operand[0]))
      {
	od2 = od->next;
	i = 1;
	while (1)
	  {
	    if (i == d->n_operands)
	      goto full_match;
	    if (od2 == NULL)
	      goto partial_match;
	    if (! compare_operands (od2, &d->operand[i]))
	      break;
	    ++i, od2 = od2->next;
	  }
      }

  /* Either partial match at the end of the list, or no match.  In either
     case, we tack on what operands are remaining to the end of the list.  */
 partial_match:
  d->operand_number = next_operand_number - i;
  for (; i < d->n_operands; ++i)
    {
      od2 = &d->operand[i];
      *odata_end = od2;
      odata_end = &od2->next;
      od2->index = next_operand_number++;
    }
  *odata_end = NULL;
  return;

 full_match:
  d->operand_number = od->index;
  return;
}


/* Process an assembler template from a define_insn or a define_peephole.
   It is either the assembler code template, a list of assembler code
   templates, or C code to generate the assembler code template.  */

static void
process_template (struct data *d, const char *template)
{
  const char *cp;
  int i;

  /* Templates starting with * contain straight code to be run.  */
  if (template[0] == '*')
    {
      d->template = 0;
      d->output_format = INSN_OUTPUT_FORMAT_FUNCTION;

      puts ("\nstatic const char *");
      printf ("output_%d (rtx *operands ATTRIBUTE_UNUSED, rtx insn ATTRIBUTE_UNUSED)\n",
	      d->code_number);
      puts ("{");
      print_rtx_ptr_loc (template);
      puts (template + 1);
      puts ("}");
    }

  /* If the assembler code template starts with a @ it is a newline-separated
     list of assembler code templates, one for each alternative.  */
  else if (template[0] == '@')
    {
      d->template = 0;
      d->output_format = INSN_OUTPUT_FORMAT_MULTI;

      printf ("\nstatic const char * const output_%d[] = {\n", d->code_number);

      for (i = 0, cp = &template[1]; *cp; )
	{
	  const char *ep, *sp;

	  while (ISSPACE (*cp))
	    cp++;

	  printf ("  \"");

	  for (ep = sp = cp; !IS_VSPACE (*ep) && *ep != '\0'; ++ep)
	    if (!ISSPACE (*ep))
	      sp = ep + 1;

	  if (sp != ep)
	    message_with_line (d->lineno,
			       "trailing whitespace in output template");

	  while (cp < sp)
	    {
	      putchar (*cp);
	      cp++;
	    }

	  printf ("\",\n");
	  i++;
	}
      if (i == 1)
	message_with_line (d->lineno,
			   "'@' is redundant for output template with single alternative");
      if (i != d->n_alternatives)
	{
	  message_with_line (d->lineno,
			     "wrong number of alternatives in the output template");
	  have_error = 1;
	}

      printf ("};\n");
    }
  else
    {
      d->template = template;
      d->output_format = INSN_OUTPUT_FORMAT_SINGLE;
    }
}

/* Check insn D for consistency in number of constraint alternatives.  */

static void
validate_insn_alternatives (struct data *d)
{
  int n = 0, start;

  /* Make sure all the operands have the same number of alternatives
     in their constraints.  Let N be that number.  */
  for (start = 0; start < d->n_operands; start++)
    if (d->operand[start].n_alternatives > 0)
      {
	int len, i;
	const char *p;
	char c;
	int which_alternative = 0;
	int alternative_count_unsure = 0;

	for (p = d->operand[start].constraint; (c = *p); p += len)
	  {
#ifdef USE_MD_CONSTRAINTS
	    if (ISSPACE (c) || strchr (indep_constraints, c))
	      len = 1;
	    else if (ISDIGIT (c))
	      {
		const char *q = p;
		do
		  q++;
		while (ISDIGIT (*q));
		len = q - p;
	      }
	    else
	      len = mdep_constraint_len (p, d->lineno, start);
#else
	    len = CONSTRAINT_LEN (c, p);

	    if (len < 1 || (len > 1 && strchr (",#*+=&%!0123456789", c)))
	      {
		message_with_line (d->lineno,
				   "invalid length %d for char '%c' in alternative %d of operand %d",
				    len, c, which_alternative, start);
		len = 1;
		have_error = 1;
	      }
#endif

	    if (c == ',')
	      {
	        which_alternative++;
		continue;
	      }

	    for (i = 1; i < len; i++)
	      if (p[i] == '\0')
		{
		  message_with_line (d->lineno,
				     "NUL in alternative %d of operand %d",
				     which_alternative, start);
		  alternative_count_unsure = 1;
		  break;
		}
	      else if (strchr (",#*", p[i]))
		{
		  message_with_line (d->lineno,
				     "'%c' in alternative %d of operand %d",
				     p[i], which_alternative, start);
		  alternative_count_unsure = 1;
		}
	  }
	if (alternative_count_unsure)
	  have_error = 1;
	else if (n == 0)
	  n = d->operand[start].n_alternatives;
	else if (n != d->operand[start].n_alternatives)
	  {
	    message_with_line (d->lineno,
			       "wrong number of alternatives in operand %d",
			       start);
	    have_error = 1;
	  }
      }

  /* Record the insn's overall number of alternatives.  */
  d->n_alternatives = n;
}

/* Verify that there are no gaps in operand numbers for INSNs.  */

static void
validate_insn_operands (struct data *d)
{
  int i;

  for (i = 0; i < d->n_operands; ++i)
    if (d->operand[i].seen == 0)
      {
	message_with_line (d->lineno, "missing operand %d", i);
	have_error = 1;
      }
}

/* Look at a define_insn just read.  Assign its code number.  Record
   on idata the template and the number of arguments.  If the insn has
   a hairy output action, output a function for now.  */

static void
gen_insn (rtx insn, int lineno)
{
  struct data *d = XNEW (struct data);
  int i;

  d->code_number = next_code_number;
  d->index_number = next_index_number;
  d->filename = read_rtx_filename;
  d->lineno = lineno;
  if (XSTR (insn, 0)[0])
    d->name = XSTR (insn, 0);
  else
    d->name = 0;

  /* Build up the list in the same order as the insns are seen
     in the machine description.  */
  d->next = 0;
  *idata_end = d;
  idata_end = &d->next;

  max_opno = -1;
  num_dups = 0;
  memset (d->operand, 0, sizeof (d->operand));

  for (i = 0; i < XVECLEN (insn, 1); i++)
    scan_operands (d, XVECEXP (insn, 1, i), 0, 0);

  d->n_operands = max_opno + 1;
  d->n_dups = num_dups;

#ifndef USE_MD_CONSTRAINTS
  check_constraint_len ();
#endif
  validate_insn_operands (d);
  validate_insn_alternatives (d);
  place_operands (d);
  process_template (d, XTMPL (insn, 3));
}

/* Look at a define_peephole just read.  Assign its code number.
   Record on idata the template and the number of arguments.
   If the insn has a hairy output action, output it now.  */

static void
gen_peephole (rtx peep, int lineno)
{
  struct data *d = XNEW (struct data);
  int i;

  d->code_number = next_code_number;
  d->index_number = next_index_number;
  d->filename = read_rtx_filename;
  d->lineno = lineno;
  d->name = 0;

  /* Build up the list in the same order as the insns are seen
     in the machine description.  */
  d->next = 0;
  *idata_end = d;
  idata_end = &d->next;

  max_opno = -1;
  num_dups = 0;
  memset (d->operand, 0, sizeof (d->operand));

  /* Get the number of operands by scanning all the patterns of the
     peephole optimizer.  But ignore all the rest of the information
     thus obtained.  */
  for (i = 0; i < XVECLEN (peep, 0); i++)
    scan_operands (d, XVECEXP (peep, 0, i), 0, 0);

  d->n_operands = max_opno + 1;
  d->n_dups = 0;

  validate_insn_alternatives (d);
  place_operands (d);
  process_template (d, XTMPL (peep, 2));
}

/* Process a define_expand just read.  Assign its code number,
   only for the purposes of `insn_gen_function'.  */

static void
gen_expand (rtx insn, int lineno)
{
  struct data *d = XNEW (struct data);
  int i;

  d->code_number = next_code_number;
  d->index_number = next_index_number;
  d->filename = read_rtx_filename;
  d->lineno = lineno;
  if (XSTR (insn, 0)[0])
    d->name = XSTR (insn, 0);
  else
    d->name = 0;

  /* Build up the list in the same order as the insns are seen
     in the machine description.  */
  d->next = 0;
  *idata_end = d;
  idata_end = &d->next;

  max_opno = -1;
  num_dups = 0;
  memset (d->operand, 0, sizeof (d->operand));

  /* Scan the operands to get the specified predicates and modes,
     since expand_binop needs to know them.  */

  if (XVEC (insn, 1))
    for (i = 0; i < XVECLEN (insn, 1); i++)
      scan_operands (d, XVECEXP (insn, 1, i), 0, 0);

  d->n_operands = max_opno + 1;
  d->n_dups = num_dups;
  d->template = 0;
  d->output_format = INSN_OUTPUT_FORMAT_NONE;

  validate_insn_alternatives (d);
  place_operands (d);
}

/* Process a define_split just read.  Assign its code number,
   only for reasons of consistency and to simplify genrecog.  */

static void
gen_split (rtx split, int lineno)
{
  struct data *d = XNEW (struct data);
  int i;

  d->code_number = next_code_number;
  d->index_number = next_index_number;
  d->filename = read_rtx_filename;
  d->lineno = lineno;
  d->name = 0;

  /* Build up the list in the same order as the insns are seen
     in the machine description.  */
  d->next = 0;
  *idata_end = d;
  idata_end = &d->next;

  max_opno = -1;
  num_dups = 0;
  memset (d->operand, 0, sizeof (d->operand));

  /* Get the number of operands by scanning all the patterns of the
     split patterns.  But ignore all the rest of the information thus
     obtained.  */
  for (i = 0; i < XVECLEN (split, 0); i++)
    scan_operands (d, XVECEXP (split, 0, i), 0, 0);

  d->n_operands = max_opno + 1;
  d->n_dups = 0;
  d->n_alternatives = 0;
  d->template = 0;
  d->output_format = INSN_OUTPUT_FORMAT_NONE;

  place_operands (d);
}

extern int main (int, char **);

int
main (int argc, char **argv)
{
  rtx desc;

  progname = "genoutput";

  if (init_md_reader_args (argc, argv) != SUCCESS_EXIT_CODE)
    return (FATAL_EXIT_CODE);

  output_prologue ();
  next_code_number = 0;
  next_index_number = 0;

  /* Read the machine description.  */

  while (1)
    {
      int line_no;

      desc = read_md_rtx (&line_no, &next_code_number);
      if (desc == NULL)
	break;

      switch (GET_CODE (desc))
	{
	case DEFINE_INSN:
	  gen_insn (desc, line_no);
	  break;

	case DEFINE_PEEPHOLE:
	  gen_peephole (desc, line_no);
	  break;

	case DEFINE_EXPAND:
	  gen_expand (desc, line_no);
	  break;

	case DEFINE_SPLIT:
	case DEFINE_PEEPHOLE2:
	  gen_split (desc, line_no);
	  break;

#ifdef USE_MD_CONSTRAINTS
	case DEFINE_CONSTRAINT:
	case DEFINE_REGISTER_CONSTRAINT:
	case DEFINE_ADDRESS_CONSTRAINT:
	case DEFINE_MEMORY_CONSTRAINT:
	  note_constraint (desc, line_no);
	  break;
#endif

	default:
	  break;
	}
      next_index_number++;
    }

  printf("\n\n");
  output_operand_data ();
  output_insn_data ();
  output_get_insn_name ();

  fflush (stdout);
  return (ferror (stdout) != 0 || have_error
	? FATAL_EXIT_CODE : SUCCESS_EXIT_CODE);
}

/* Return the number of occurrences of character C in string S or
   -1 if S is the null string.  */

static int
n_occurrences (int c, const char *s)
{
  int n = 0;

  if (s == 0 || *s == '\0')
    return -1;

  while (*s)
    n += (*s++ == c);

  return n;
}

/* Remove whitespace in `s' by moving up characters until the end.
   Return a new string.  */

static const char *
strip_whitespace (const char *s)
{
  char *p, *q;
  char ch;

  if (s == 0)
    return 0;

  p = q = XNEWVEC (char, strlen (s) + 1);
  while ((ch = *s++) != '\0')
    if (! ISSPACE (ch))
      *p++ = ch;

  *p = '\0';
  return q;
}

#ifdef USE_MD_CONSTRAINTS

/* Record just enough information about a constraint to allow checking
   of operand constraint strings above, in validate_insn_alternatives.
   Does not validate most properties of the constraint itself; does
   enforce no duplicate names, no overlap with MI constraints, and no
   prefixes.  EXP is the define_*constraint form, LINENO the line number
   reported by the reader.  */
static void
note_constraint (rtx exp, int lineno)
{
  const char *name = XSTR (exp, 0);
  unsigned int namelen = strlen (name);
  struct constraint_data **iter, **slot, *new;

  if (strchr (indep_constraints, name[0]))
    {
      if (name[1] == '\0')
	message_with_line (lineno, "constraint letter '%s' cannot be "
			   "redefined by the machine description", name);
      else
	message_with_line (lineno, "constraint name '%s' cannot be defined by "
			   "the machine description, as it begins with '%c'",
			   name, name[0]);
      have_error = 1;
      return;
    }

  slot = &constraints_by_letter_table[(unsigned int)name[0]];
  for (iter = slot; *iter; iter = &(*iter)->next_this_letter)
    {
      /* This causes slot to end up pointing to the
	 next_this_letter field of the last constraint with a name
	 of equal or greater length than the new constraint; hence
	 the new constraint will be inserted after all previous
	 constraints with names of the same length.  */
      if ((*iter)->namelen >= namelen)
	slot = iter;

      if (!strcmp ((*iter)->name, name))
	{
	  message_with_line (lineno, "redefinition of constraint '%s'", name);
	  message_with_line ((*iter)->lineno, "previous definition is here");
	  have_error = 1;
	  return;
	}
      else if (!strncmp ((*iter)->name, name, (*iter)->namelen))
	{
	  message_with_line (lineno, "defining constraint '%s' here", name);
	  message_with_line ((*iter)->lineno, "renders constraint '%s' "
			     "(defined here) a prefix", (*iter)->name);
	  have_error = 1;
	  return;
	}
      else if (!strncmp ((*iter)->name, name, namelen))
	{
	  message_with_line (lineno, "constraint '%s' is a prefix", name);
	  message_with_line ((*iter)->lineno, "of constraint '%s' "
			     "(defined here)", (*iter)->name);
	  have_error = 1;
	  return;
	}
    }
  new = xmalloc (sizeof (struct constraint_data) + namelen);
  strcpy ((char *)new + offsetof(struct constraint_data, name), name);
  new->namelen = namelen;
  new->lineno = lineno;
  new->next_this_letter = *slot;
  *slot = new;
}

/* Return the length of the constraint name beginning at position S
   of an operand constraint string, or issue an error message if there
   is no such constraint.  Does not expect to be called for generic
   constraints.  */
static int
mdep_constraint_len (const char *s, int lineno, int opno)
{
  struct constraint_data *p;

  p = constraints_by_letter_table[(unsigned int)s[0]];

  if (p)
    for (; p; p = p->next_this_letter)
      if (!strncmp (s, p->name, p->namelen))
	return p->namelen;

  message_with_line (lineno,
		     "error: undefined machine-specific constraint "
		     "at this point: \"%s\"", s);
  message_with_line (lineno, "note:  in operand %d", opno);
  have_error = 1;
  return 1; /* safe */
}

#else
/* Verify that DEFAULT_CONSTRAINT_LEN is used properly and not
   tampered with.  This isn't bullet-proof, but it should catch
   most genuine mistakes.  */
static void
check_constraint_len (void)
{
  const char *p;
  int d;

  for (p = ",#*+=&%!1234567890"; *p; p++)
    for (d = -9; d < 9; d++)
      gcc_assert (constraint_len (p, d) == d);
}

static int
constraint_len (const char *p, int genoutput_default_constraint_len)
{
  /* Check that we still match defaults.h .  First we do a generation-time
     check that fails if the value is not the expected one...  */
  gcc_assert (DEFAULT_CONSTRAINT_LEN (*p, p) == 1);
  /* And now a compile-time check that should give a diagnostic if the
     definition doesn't exactly match.  */
#define DEFAULT_CONSTRAINT_LEN(C,STR) 1
  /* Now re-define DEFAULT_CONSTRAINT_LEN so that we can verify it is
     being used.  */
#undef DEFAULT_CONSTRAINT_LEN
#define DEFAULT_CONSTRAINT_LEN(C,STR) \
  ((C) != *p || STR != p ? -1 : genoutput_default_constraint_len)
  return CONSTRAINT_LEN (*p, p);
  /* And set it back.  */
#undef DEFAULT_CONSTRAINT_LEN
#define DEFAULT_CONSTRAINT_LEN(C,STR) 1
}
#endif
