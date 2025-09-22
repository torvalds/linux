/* Generate code from machine description to compute values of attributes.
   Copyright (C) 1991, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Richard Kenner (kenner@vlsi1.ultra.nyu.edu)

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

/* This program handles insn attributes and the DEFINE_DELAY and
   DEFINE_INSN_RESERVATION definitions.

   It produces a series of functions named `get_attr_...', one for each insn
   attribute.  Each of these is given the rtx for an insn and returns a member
   of the enum for the attribute.

   These subroutines have the form of a `switch' on the INSN_CODE (via
   `recog_memoized').  Each case either returns a constant attribute value
   or a value that depends on tests on other attributes, the form of
   operands, or some random C expression (encoded with a SYMBOL_REF
   expression).

   If the attribute `alternative', or a random C expression is present,
   `constrain_operands' is called.  If either of these cases of a reference to
   an operand is found, `extract_insn' is called.

   The special attribute `length' is also recognized.  For this operand,
   expressions involving the address of an operand or the current insn,
   (address (pc)), are valid.  In this case, an initial pass is made to
   set all lengths that do not depend on address.  Those that do are set to
   the maximum length.  Then each insn that depends on an address is checked
   and possibly has its length changed.  The process repeats until no further
   changed are made.  The resulting lengths are saved for use by
   `get_attr_length'.

   A special form of DEFINE_ATTR, where the expression for default value is a
   CONST expression, indicates an attribute that is constant for a given run
   of the compiler.  The subroutine generated for these attributes has no
   parameters as it does not depend on any particular insn.  Constant
   attributes are typically used to specify which variety of processor is
   used.

   Internal attributes are defined to handle DEFINE_DELAY and
   DEFINE_INSN_RESERVATION.  Special routines are output for these cases.

   This program works by keeping a list of possible values for each attribute.
   These include the basic attribute choices, default values for attribute, and
   all derived quantities.

   As the description file is read, the definition for each insn is saved in a
   `struct insn_def'.   When the file reading is complete, a `struct insn_ent'
   is created for each insn and chained to the corresponding attribute value,
   either that specified, or the default.

   An optimization phase is then run.  This simplifies expressions for each
   insn.  EQ_ATTR tests are resolved, whenever possible, to a test that
   indicates when the attribute has the specified value for the insn.  This
   avoids recursive calls during compilation.

   The strategy used when processing DEFINE_DELAY definitions is to create
   arbitrarily complex expressions and have the optimization simplify them.

   Once optimization is complete, any required routines and definitions
   will be written.

   An optimization that is not yet implemented is to hoist the constant
   expressions entirely out of the routines and definitions that are written.
   A way to do this is to iterate over all possible combinations of values
   for constant attributes and generate a set of functions for that given
   combination.  An initialization function would be written that evaluates
   the attributes and installs the corresponding set of routines and
   definitions (each would be accessed through a pointer).

   We use the flags in an RTX as follows:
   `unchanging' (ATTR_IND_SIMPLIFIED_P): This rtx is fully simplified
      independent of the insn code.
   `in_struct' (ATTR_CURR_SIMPLIFIED_P): This rtx is fully simplified
      for the insn code currently being processed (see optimize_attrs).
   `return_val' (ATTR_PERMANENT_P): This rtx is permanent and unique
      (see attr_rtx).  */

#define ATTR_IND_SIMPLIFIED_P(RTX) (RTX_FLAG((RTX), unchanging))
#define ATTR_CURR_SIMPLIFIED_P(RTX) (RTX_FLAG((RTX), in_struct))
#define ATTR_PERMANENT_P(RTX) (RTX_FLAG((RTX), return_val))

#if 0
#define strcmp_check(S1, S2) ((S1) == (S2)		\
			      ? 0			\
			      : (gcc_assert (strcmp ((S1), (S2))), 1))
#else
#define strcmp_check(S1, S2) ((S1) != (S2))
#endif

#include "bconfig.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "gensupport.h"
#include "obstack.h"
#include "errors.h"

/* Flags for make_internal_attr's `special' parameter.  */
#define ATTR_NONE		0
#define ATTR_SPECIAL		(1 << 0)

static struct obstack obstack1, obstack2;
static struct obstack *hash_obstack = &obstack1;
static struct obstack *temp_obstack = &obstack2;

/* enough space to reserve for printing out ints */
#define MAX_DIGITS (HOST_BITS_PER_INT * 3 / 10 + 3)

/* Define structures used to record attributes and values.  */

/* As each DEFINE_INSN, DEFINE_PEEPHOLE, or DEFINE_ASM_ATTRIBUTES is
   encountered, we store all the relevant information into a
   `struct insn_def'.  This is done to allow attribute definitions to occur
   anywhere in the file.  */

struct insn_def
{
  struct insn_def *next;	/* Next insn in chain.  */
  rtx def;			/* The DEFINE_...  */
  int insn_code;		/* Instruction number.  */
  int insn_index;		/* Expression numer in file, for errors.  */
  int lineno;			/* Line number.  */
  int num_alternatives;		/* Number of alternatives.  */
  int vec_idx;			/* Index of attribute vector in `def'.  */
};

/* Once everything has been read in, we store in each attribute value a list
   of insn codes that have that value.  Here is the structure used for the
   list.  */

struct insn_ent
{
  struct insn_ent *next;	/* Next in chain.  */
  struct insn_def *def;		/* Instruction definition.  */
};

/* Each value of an attribute (either constant or computed) is assigned a
   structure which is used as the listhead of the insns that have that
   value.  */

struct attr_value
{
  rtx value;			/* Value of attribute.  */
  struct attr_value *next;	/* Next attribute value in chain.  */
  struct insn_ent *first_insn;	/* First insn with this value.  */
  int num_insns;		/* Number of insns with this value.  */
  int has_asm_insn;		/* True if this value used for `asm' insns */
};

/* Structure for each attribute.  */

struct attr_desc
{
  char *name;			/* Name of attribute.  */
  struct attr_desc *next;	/* Next attribute.  */
  struct attr_value *first_value; /* First value of this attribute.  */
  struct attr_value *default_val; /* Default value for this attribute.  */
  int lineno : 24;		/* Line number.  */
  unsigned is_numeric	: 1;	/* Values of this attribute are numeric.  */
  unsigned is_const	: 1;	/* Attribute value constant for each run.  */
  unsigned is_special	: 1;	/* Don't call `write_attr_set'.  */
};

/* Structure for each DEFINE_DELAY.  */

struct delay_desc
{
  rtx def;			/* DEFINE_DELAY expression.  */
  struct delay_desc *next;	/* Next DEFINE_DELAY.  */
  int num;			/* Number of DEFINE_DELAY, starting at 1.  */
  int lineno;			/* Line number.  */
};

/* Listheads of above structures.  */

/* This one is indexed by the first character of the attribute name.  */
#define MAX_ATTRS_INDEX 256
static struct attr_desc *attrs[MAX_ATTRS_INDEX];
static struct insn_def *defs;
static struct delay_desc *delays;

/* Other variables.  */

static int insn_code_number;
static int insn_index_number;
static int got_define_asm_attributes;
static int must_extract;
static int must_constrain;
static int address_used;
static int length_used;
static int num_delays;
static int have_annul_true, have_annul_false;
static int num_insn_ents;

/* Stores, for each insn code, the number of constraint alternatives.  */

static int *insn_n_alternatives;

/* Stores, for each insn code, a bitmap that has bits on for each possible
   alternative.  */

static int *insn_alternatives;

/* Used to simplify expressions.  */

static rtx true_rtx, false_rtx;

/* Used to reduce calls to `strcmp' */

static const char *alternative_name;
static const char *length_str;
static const char *delay_type_str;
static const char *delay_1_0_str;
static const char *num_delay_slots_str;

/* Simplify an expression.  Only call the routine if there is something to
   simplify.  */
#define SIMPLIFY_TEST_EXP(EXP,INSN_CODE,INSN_INDEX)	\
  (ATTR_IND_SIMPLIFIED_P (EXP) || ATTR_CURR_SIMPLIFIED_P (EXP) ? (EXP)	\
   : simplify_test_exp (EXP, INSN_CODE, INSN_INDEX))

#define DEF_ATTR_STRING(S) (attr_string ((S), strlen (S)))

/* Forward declarations of functions used before their definitions, only.  */
static char *attr_string           (const char *, int);
static char *attr_printf           (unsigned int, const char *, ...)
  ATTRIBUTE_PRINTF_2;
static rtx make_numeric_value      (int);
static struct attr_desc *find_attr (const char **, int);
static rtx mk_attr_alt             (int);
static char *next_comma_elt	   (const char **);
static rtx insert_right_side	   (enum rtx_code, rtx, rtx, int, int);
static rtx copy_boolean		   (rtx);
static int compares_alternatives_p (rtx);
static void make_internal_attr     (const char *, rtx, int);
static void insert_insn_ent        (struct attr_value *, struct insn_ent *);
static void walk_attr_value	   (rtx);
static int max_attr_value	   (rtx, int*);
static int min_attr_value	   (rtx, int*);
static int or_attr_value	   (rtx, int*);
static rtx simplify_test_exp	   (rtx, int, int);
static rtx simplify_test_exp_in_temp (rtx, int, int);
static rtx copy_rtx_unchanging	   (rtx);
static bool attr_alt_subset_p      (rtx, rtx);
static bool attr_alt_subset_of_compl_p (rtx, rtx);
static void clear_struct_flag      (rtx);
static void write_attr_valueq	   (struct attr_desc *, const char *);
static struct attr_value *find_most_used  (struct attr_desc *);
static void write_attr_set	   (struct attr_desc *, int, rtx,
				    const char *, const char *, rtx,
				    int, int);
static void write_attr_case	   (struct attr_desc *, struct attr_value *,
				    int, const char *, const char *, int, rtx);
static void write_attr_value	   (struct attr_desc *, rtx);
static void write_upcase	   (const char *);
static void write_indent	   (int);
static rtx identity_fn		   (rtx);
static rtx zero_fn		   (rtx);
static rtx one_fn		   (rtx);
static rtx max_fn		   (rtx);
static rtx min_fn		   (rtx);

#define oballoc(size) obstack_alloc (hash_obstack, size)

/* Hash table for sharing RTL and strings.  */

/* Each hash table slot is a bucket containing a chain of these structures.
   Strings are given negative hash codes; RTL expressions are given positive
   hash codes.  */

struct attr_hash
{
  struct attr_hash *next;	/* Next structure in the bucket.  */
  int hashcode;			/* Hash code of this rtx or string.  */
  union
    {
      char *str;		/* The string (negative hash codes) */
      rtx rtl;			/* or the RTL recorded here.  */
    } u;
};

/* Now here is the hash table.  When recording an RTL, it is added to
   the slot whose index is the hash code mod the table size.  Note
   that the hash table is used for several kinds of RTL (see attr_rtx)
   and for strings.  While all these live in the same table, they are
   completely independent, and the hash code is computed differently
   for each.  */

#define RTL_HASH_SIZE 4093
static struct attr_hash *attr_hash_table[RTL_HASH_SIZE];

/* Here is how primitive or already-shared RTL's hash
   codes are made.  */
#define RTL_HASH(RTL) ((long) (RTL) & 0777777)

/* Add an entry to the hash table for RTL with hash code HASHCODE.  */

static void
attr_hash_add_rtx (int hashcode, rtx rtl)
{
  struct attr_hash *h;

  h = obstack_alloc (hash_obstack, sizeof (struct attr_hash));
  h->hashcode = hashcode;
  h->u.rtl = rtl;
  h->next = attr_hash_table[hashcode % RTL_HASH_SIZE];
  attr_hash_table[hashcode % RTL_HASH_SIZE] = h;
}

/* Add an entry to the hash table for STRING with hash code HASHCODE.  */

static void
attr_hash_add_string (int hashcode, char *str)
{
  struct attr_hash *h;

  h = obstack_alloc (hash_obstack, sizeof (struct attr_hash));
  h->hashcode = -hashcode;
  h->u.str = str;
  h->next = attr_hash_table[hashcode % RTL_HASH_SIZE];
  attr_hash_table[hashcode % RTL_HASH_SIZE] = h;
}

/* Generate an RTL expression, but avoid duplicates.
   Set the ATTR_PERMANENT_P flag for these permanent objects.

   In some cases we cannot uniquify; then we return an ordinary
   impermanent rtx with ATTR_PERMANENT_P clear.

   Args are as follows:

   rtx attr_rtx (code, [element1, ..., elementn])  */

static rtx
attr_rtx_1 (enum rtx_code code, va_list p)
{
  rtx rt_val = NULL_RTX;/* RTX to return to caller...		*/
  int hashcode;
  struct attr_hash *h;
  struct obstack *old_obstack = rtl_obstack;

  /* For each of several cases, search the hash table for an existing entry.
     Use that entry if one is found; otherwise create a new RTL and add it
     to the table.  */

  if (GET_RTX_CLASS (code) == RTX_UNARY)
    {
      rtx arg0 = va_arg (p, rtx);

      /* A permanent object cannot point to impermanent ones.  */
      if (! ATTR_PERMANENT_P (arg0))
	{
	  rt_val = rtx_alloc (code);
	  XEXP (rt_val, 0) = arg0;
	  return rt_val;
	}

      hashcode = ((HOST_WIDE_INT) code + RTL_HASH (arg0));
      for (h = attr_hash_table[hashcode % RTL_HASH_SIZE]; h; h = h->next)
	if (h->hashcode == hashcode
	    && GET_CODE (h->u.rtl) == code
	    && XEXP (h->u.rtl, 0) == arg0)
	  return h->u.rtl;

      if (h == 0)
	{
	  rtl_obstack = hash_obstack;
	  rt_val = rtx_alloc (code);
	  XEXP (rt_val, 0) = arg0;
	}
    }
  else if (GET_RTX_CLASS (code) == RTX_BIN_ARITH
  	   || GET_RTX_CLASS (code) == RTX_COMM_ARITH
  	   || GET_RTX_CLASS (code) == RTX_COMPARE
  	   || GET_RTX_CLASS (code) == RTX_COMM_COMPARE)
    {
      rtx arg0 = va_arg (p, rtx);
      rtx arg1 = va_arg (p, rtx);

      /* A permanent object cannot point to impermanent ones.  */
      if (! ATTR_PERMANENT_P (arg0) || ! ATTR_PERMANENT_P (arg1))
	{
	  rt_val = rtx_alloc (code);
	  XEXP (rt_val, 0) = arg0;
	  XEXP (rt_val, 1) = arg1;
	  return rt_val;
	}

      hashcode = ((HOST_WIDE_INT) code + RTL_HASH (arg0) + RTL_HASH (arg1));
      for (h = attr_hash_table[hashcode % RTL_HASH_SIZE]; h; h = h->next)
	if (h->hashcode == hashcode
	    && GET_CODE (h->u.rtl) == code
	    && XEXP (h->u.rtl, 0) == arg0
	    && XEXP (h->u.rtl, 1) == arg1)
	  return h->u.rtl;

      if (h == 0)
	{
	  rtl_obstack = hash_obstack;
	  rt_val = rtx_alloc (code);
	  XEXP (rt_val, 0) = arg0;
	  XEXP (rt_val, 1) = arg1;
	}
    }
  else if (GET_RTX_LENGTH (code) == 1
	   && GET_RTX_FORMAT (code)[0] == 's')
    {
      char *arg0 = va_arg (p, char *);

      arg0 = DEF_ATTR_STRING (arg0);

      hashcode = ((HOST_WIDE_INT) code + RTL_HASH (arg0));
      for (h = attr_hash_table[hashcode % RTL_HASH_SIZE]; h; h = h->next)
	if (h->hashcode == hashcode
	    && GET_CODE (h->u.rtl) == code
	    && XSTR (h->u.rtl, 0) == arg0)
	  return h->u.rtl;

      if (h == 0)
	{
	  rtl_obstack = hash_obstack;
	  rt_val = rtx_alloc (code);
	  XSTR (rt_val, 0) = arg0;
	}
    }
  else if (GET_RTX_LENGTH (code) == 2
	   && GET_RTX_FORMAT (code)[0] == 's'
	   && GET_RTX_FORMAT (code)[1] == 's')
    {
      char *arg0 = va_arg (p, char *);
      char *arg1 = va_arg (p, char *);

      hashcode = ((HOST_WIDE_INT) code + RTL_HASH (arg0) + RTL_HASH (arg1));
      for (h = attr_hash_table[hashcode % RTL_HASH_SIZE]; h; h = h->next)
	if (h->hashcode == hashcode
	    && GET_CODE (h->u.rtl) == code
	    && XSTR (h->u.rtl, 0) == arg0
	    && XSTR (h->u.rtl, 1) == arg1)
	  return h->u.rtl;

      if (h == 0)
	{
	  rtl_obstack = hash_obstack;
	  rt_val = rtx_alloc (code);
	  XSTR (rt_val, 0) = arg0;
	  XSTR (rt_val, 1) = arg1;
	}
    }
  else if (code == CONST_INT)
    {
      HOST_WIDE_INT arg0 = va_arg (p, HOST_WIDE_INT);
      if (arg0 == 0)
	return false_rtx;
      else if (arg0 == 1)
	return true_rtx;
      else
	goto nohash;
    }
  else
    {
      int i;		/* Array indices...			*/
      const char *fmt;	/* Current rtx's format...		*/
    nohash:
      rt_val = rtx_alloc (code);	/* Allocate the storage space.  */

      fmt = GET_RTX_FORMAT (code);	/* Find the right format...  */
      for (i = 0; i < GET_RTX_LENGTH (code); i++)
	{
	  switch (*fmt++)
	    {
	    case '0':		/* Unused field.  */
	      break;

	    case 'i':		/* An integer?  */
	      XINT (rt_val, i) = va_arg (p, int);
	      break;

	    case 'w':		/* A wide integer? */
	      XWINT (rt_val, i) = va_arg (p, HOST_WIDE_INT);
	      break;

	    case 's':		/* A string?  */
	      XSTR (rt_val, i) = va_arg (p, char *);
	      break;

	    case 'e':		/* An expression?  */
	    case 'u':		/* An insn?  Same except when printing.  */
	      XEXP (rt_val, i) = va_arg (p, rtx);
	      break;

	    case 'E':		/* An RTX vector?  */
	      XVEC (rt_val, i) = va_arg (p, rtvec);
	      break;

	    default:
	      gcc_unreachable ();
	    }
	}
      return rt_val;
    }

  rtl_obstack = old_obstack;
  attr_hash_add_rtx (hashcode, rt_val);
  ATTR_PERMANENT_P (rt_val) = 1;
  return rt_val;
}

static rtx
attr_rtx (enum rtx_code code, ...)
{
  rtx result;
  va_list p;

  va_start (p, code);
  result = attr_rtx_1 (code, p);
  va_end (p);
  return result;
}

/* Create a new string printed with the printf line arguments into a space
   of at most LEN bytes:

   rtx attr_printf (len, format, [arg1, ..., argn])  */

static char *
attr_printf (unsigned int len, const char *fmt, ...)
{
  char str[256];
  va_list p;

  va_start (p, fmt);

  gcc_assert (len < sizeof str); /* Leave room for \0.  */

  vsprintf (str, fmt, p);
  va_end (p);

  return DEF_ATTR_STRING (str);
}

static rtx
attr_eq (const char *name, const char *value)
{
  return attr_rtx (EQ_ATTR, DEF_ATTR_STRING (name), DEF_ATTR_STRING (value));
}

static const char *
attr_numeral (int n)
{
  return XSTR (make_numeric_value (n), 0);
}

/* Return a permanent (possibly shared) copy of a string STR (not assumed
   to be null terminated) with LEN bytes.  */

static char *
attr_string (const char *str, int len)
{
  struct attr_hash *h;
  int hashcode;
  int i;
  char *new_str;

  /* Compute the hash code.  */
  hashcode = (len + 1) * 613 + (unsigned) str[0];
  for (i = 1; i < len; i += 2)
    hashcode = ((hashcode * 613) + (unsigned) str[i]);
  if (hashcode < 0)
    hashcode = -hashcode;

  /* Search the table for the string.  */
  for (h = attr_hash_table[hashcode % RTL_HASH_SIZE]; h; h = h->next)
    if (h->hashcode == -hashcode && h->u.str[0] == str[0]
	&& !strncmp (h->u.str, str, len))
      return h->u.str;			/* <-- return if found.  */

  /* Not found; create a permanent copy and add it to the hash table.  */
  new_str = obstack_alloc (hash_obstack, len + 1);
  memcpy (new_str, str, len);
  new_str[len] = '\0';
  attr_hash_add_string (hashcode, new_str);

  return new_str;			/* Return the new string.  */
}

/* Check two rtx's for equality of contents,
   taking advantage of the fact that if both are hashed
   then they can't be equal unless they are the same object.  */

static int
attr_equal_p (rtx x, rtx y)
{
  return (x == y || (! (ATTR_PERMANENT_P (x) && ATTR_PERMANENT_P (y))
		     && rtx_equal_p (x, y)));
}

/* Copy an attribute value expression,
   descending to all depths, but not copying any
   permanent hashed subexpressions.  */

static rtx
attr_copy_rtx (rtx orig)
{
  rtx copy;
  int i, j;
  RTX_CODE code;
  const char *format_ptr;

  /* No need to copy a permanent object.  */
  if (ATTR_PERMANENT_P (orig))
    return orig;

  code = GET_CODE (orig);

  switch (code)
    {
    case REG:
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case SYMBOL_REF:
    case CODE_LABEL:
    case PC:
    case CC0:
      return orig;

    default:
      break;
    }

  copy = rtx_alloc (code);
  PUT_MODE (copy, GET_MODE (orig));
  ATTR_IND_SIMPLIFIED_P (copy) = ATTR_IND_SIMPLIFIED_P (orig);
  ATTR_CURR_SIMPLIFIED_P (copy) = ATTR_CURR_SIMPLIFIED_P (orig);
  ATTR_PERMANENT_P (copy) = ATTR_PERMANENT_P (orig);

  format_ptr = GET_RTX_FORMAT (GET_CODE (copy));

  for (i = 0; i < GET_RTX_LENGTH (GET_CODE (copy)); i++)
    {
      switch (*format_ptr++)
	{
	case 'e':
	  XEXP (copy, i) = XEXP (orig, i);
	  if (XEXP (orig, i) != NULL)
	    XEXP (copy, i) = attr_copy_rtx (XEXP (orig, i));
	  break;

	case 'E':
	case 'V':
	  XVEC (copy, i) = XVEC (orig, i);
	  if (XVEC (orig, i) != NULL)
	    {
	      XVEC (copy, i) = rtvec_alloc (XVECLEN (orig, i));
	      for (j = 0; j < XVECLEN (copy, i); j++)
		XVECEXP (copy, i, j) = attr_copy_rtx (XVECEXP (orig, i, j));
	    }
	  break;

	case 'n':
	case 'i':
	  XINT (copy, i) = XINT (orig, i);
	  break;

	case 'w':
	  XWINT (copy, i) = XWINT (orig, i);
	  break;

	case 's':
	case 'S':
	  XSTR (copy, i) = XSTR (orig, i);
	  break;

	default:
	  gcc_unreachable ();
	}
    }
  return copy;
}

/* Given a test expression for an attribute, ensure it is validly formed.
   IS_CONST indicates whether the expression is constant for each compiler
   run (a constant expression may not test any particular insn).

   Convert (eq_attr "att" "a1,a2") to (ior (eq_attr ... ) (eq_attrq ..))
   and (eq_attr "att" "!a1") to (not (eq_attr "att" "a1")).  Do the latter
   test first so that (eq_attr "att" "!a1,a2,a3") works as expected.

   Update the string address in EQ_ATTR expression to be the same used
   in the attribute (or `alternative_name') to speed up subsequent
   `find_attr' calls and eliminate most `strcmp' calls.

   Return the new expression, if any.  */

static rtx
check_attr_test (rtx exp, int is_const, int lineno)
{
  struct attr_desc *attr;
  struct attr_value *av;
  const char *name_ptr, *p;
  rtx orexp, newexp;

  switch (GET_CODE (exp))
    {
    case EQ_ATTR:
      /* Handle negation test.  */
      if (XSTR (exp, 1)[0] == '!')
	return check_attr_test (attr_rtx (NOT,
					  attr_eq (XSTR (exp, 0),
						   &XSTR (exp, 1)[1])),
				is_const, lineno);

      else if (n_comma_elts (XSTR (exp, 1)) == 1)
	{
	  attr = find_attr (&XSTR (exp, 0), 0);
	  if (attr == NULL)
	    {
	      if (! strcmp (XSTR (exp, 0), "alternative"))
		return mk_attr_alt (1 << atoi (XSTR (exp, 1)));
	      else
		fatal ("unknown attribute `%s' in EQ_ATTR", XSTR (exp, 0));
	    }

	  if (is_const && ! attr->is_const)
	    fatal ("constant expression uses insn attribute `%s' in EQ_ATTR",
		   XSTR (exp, 0));

	  /* Copy this just to make it permanent,
	     so expressions using it can be permanent too.  */
	  exp = attr_eq (XSTR (exp, 0), XSTR (exp, 1));

	  /* It shouldn't be possible to simplify the value given to a
	     constant attribute, so don't expand this until it's time to
	     write the test expression.  */
	  if (attr->is_const)
	    ATTR_IND_SIMPLIFIED_P (exp) = 1;

	  if (attr->is_numeric)
	    {
	      for (p = XSTR (exp, 1); *p; p++)
		if (! ISDIGIT (*p))
		  fatal ("attribute `%s' takes only numeric values",
			 XSTR (exp, 0));
	    }
	  else
	    {
	      for (av = attr->first_value; av; av = av->next)
		if (GET_CODE (av->value) == CONST_STRING
		    && ! strcmp (XSTR (exp, 1), XSTR (av->value, 0)))
		  break;

	      if (av == NULL)
		fatal ("unknown value `%s' for `%s' attribute",
		       XSTR (exp, 1), XSTR (exp, 0));
	    }
	}
      else
	{
	  if (! strcmp (XSTR (exp, 0), "alternative"))
	    {
	      int set = 0;

	      name_ptr = XSTR (exp, 1);
	      while ((p = next_comma_elt (&name_ptr)) != NULL)
		set |= 1 << atoi (p);

	      return mk_attr_alt (set);
	    }
	  else
	    {
	      /* Make an IOR tree of the possible values.  */
	      orexp = false_rtx;
	      name_ptr = XSTR (exp, 1);
	      while ((p = next_comma_elt (&name_ptr)) != NULL)
		{
		  newexp = attr_eq (XSTR (exp, 0), p);
		  orexp = insert_right_side (IOR, orexp, newexp, -2, -2);
		}

	      return check_attr_test (orexp, is_const, lineno);
	    }
	}
      break;

    case ATTR_FLAG:
      break;

    case CONST_INT:
      /* Either TRUE or FALSE.  */
      if (XWINT (exp, 0))
	return true_rtx;
      else
	return false_rtx;

    case IOR:
    case AND:
      XEXP (exp, 0) = check_attr_test (XEXP (exp, 0), is_const, lineno);
      XEXP (exp, 1) = check_attr_test (XEXP (exp, 1), is_const, lineno);
      break;

    case NOT:
      XEXP (exp, 0) = check_attr_test (XEXP (exp, 0), is_const, lineno);
      break;

    case MATCH_OPERAND:
      if (is_const)
	fatal ("RTL operator \"%s\" not valid in constant attribute test",
	       GET_RTX_NAME (GET_CODE (exp)));
      /* These cases can't be simplified.  */
      ATTR_IND_SIMPLIFIED_P (exp) = 1;
      break;

    case LE:  case LT:  case GT:  case GE:
    case LEU: case LTU: case GTU: case GEU:
    case NE:  case EQ:
      if (GET_CODE (XEXP (exp, 0)) == SYMBOL_REF
	  && GET_CODE (XEXP (exp, 1)) == SYMBOL_REF)
	exp = attr_rtx (GET_CODE (exp),
			attr_rtx (SYMBOL_REF, XSTR (XEXP (exp, 0), 0)),
			attr_rtx (SYMBOL_REF, XSTR (XEXP (exp, 1), 0)));
      /* These cases can't be simplified.  */
      ATTR_IND_SIMPLIFIED_P (exp) = 1;
      break;

    case SYMBOL_REF:
      if (is_const)
	{
	  /* These cases are valid for constant attributes, but can't be
	     simplified.  */
	  exp = attr_rtx (SYMBOL_REF, XSTR (exp, 0));
	  ATTR_IND_SIMPLIFIED_P (exp) = 1;
	  break;
	}
    default:
      fatal ("RTL operator \"%s\" not valid in attribute test",
	     GET_RTX_NAME (GET_CODE (exp)));
    }

  return exp;
}

/* Given an expression, ensure that it is validly formed and that all named
   attribute values are valid for the given attribute.  Issue a fatal error
   if not.  If no attribute is specified, assume a numeric attribute.

   Return a perhaps modified replacement expression for the value.  */

static rtx
check_attr_value (rtx exp, struct attr_desc *attr)
{
  struct attr_value *av;
  const char *p;
  int i;

  switch (GET_CODE (exp))
    {
    case CONST_INT:
      if (attr && ! attr->is_numeric)
	{
	  message_with_line (attr->lineno,
			     "CONST_INT not valid for non-numeric attribute %s",
			     attr->name);
	  have_error = 1;
	  break;
	}

      if (INTVAL (exp) < 0)
	{
	  message_with_line (attr->lineno,
			     "negative numeric value specified for attribute %s",
			     attr->name);
	  have_error = 1;
	  break;
	}
      break;

    case CONST_STRING:
      if (! strcmp (XSTR (exp, 0), "*"))
	break;

      if (attr == 0 || attr->is_numeric)
	{
	  p = XSTR (exp, 0);
	  for (; *p; p++)
	    if (! ISDIGIT (*p))
	      {
		message_with_line (attr ? attr->lineno : 0,
				   "non-numeric value for numeric attribute %s",
				   attr ? attr->name : "internal");
		have_error = 1;
		break;
	      }
	  break;
	}

      for (av = attr->first_value; av; av = av->next)
	if (GET_CODE (av->value) == CONST_STRING
	    && ! strcmp (XSTR (av->value, 0), XSTR (exp, 0)))
	  break;

      if (av == NULL)
	{
	  message_with_line (attr->lineno,
			     "unknown value `%s' for `%s' attribute",
			     XSTR (exp, 0), attr ? attr->name : "internal");
	  have_error = 1;
	}
      break;

    case IF_THEN_ELSE:
      XEXP (exp, 0) = check_attr_test (XEXP (exp, 0),
				       attr ? attr->is_const : 0,
				       attr ? attr->lineno : 0);
      XEXP (exp, 1) = check_attr_value (XEXP (exp, 1), attr);
      XEXP (exp, 2) = check_attr_value (XEXP (exp, 2), attr);
      break;

    case PLUS:
    case MINUS:
    case MULT:
    case DIV:
    case MOD:
      if (attr && !attr->is_numeric)
	{
	  message_with_line (attr->lineno,
			     "invalid operation `%s' for non-numeric attribute value",
			     GET_RTX_NAME (GET_CODE (exp)));
	  have_error = 1;
	  break;
	}
      /* Fall through.  */

    case IOR:
    case AND:
      XEXP (exp, 0) = check_attr_value (XEXP (exp, 0), attr);
      XEXP (exp, 1) = check_attr_value (XEXP (exp, 1), attr);
      break;

    case FFS:
    case CLZ:
    case CTZ:
    case POPCOUNT:
    case PARITY:
      XEXP (exp, 0) = check_attr_value (XEXP (exp, 0), attr);
      break;

    case COND:
      if (XVECLEN (exp, 0) % 2 != 0)
	{
	  message_with_line (attr->lineno,
			     "first operand of COND must have even length");
	  have_error = 1;
	  break;
	}

      for (i = 0; i < XVECLEN (exp, 0); i += 2)
	{
	  XVECEXP (exp, 0, i) = check_attr_test (XVECEXP (exp, 0, i),
						 attr ? attr->is_const : 0,
						 attr ? attr->lineno : 0);
	  XVECEXP (exp, 0, i + 1)
	    = check_attr_value (XVECEXP (exp, 0, i + 1), attr);
	}

      XEXP (exp, 1) = check_attr_value (XEXP (exp, 1), attr);
      break;

    case ATTR:
      {
	struct attr_desc *attr2 = find_attr (&XSTR (exp, 0), 0);
	if (attr2 == NULL)
	  {
	    message_with_line (attr ? attr->lineno : 0,
			       "unknown attribute `%s' in ATTR",
			       XSTR (exp, 0));
	    have_error = 1;
	  }
	else if (attr && attr->is_const && ! attr2->is_const)
	  {
	    message_with_line (attr->lineno,
		"non-constant attribute `%s' referenced from `%s'",
		XSTR (exp, 0), attr->name);
	    have_error = 1;
	  }
	else if (attr
		 && attr->is_numeric != attr2->is_numeric)
	  {
	    message_with_line (attr->lineno,
		"numeric attribute mismatch calling `%s' from `%s'",
		XSTR (exp, 0), attr->name);
	    have_error = 1;
	  }
      }
      break;

    case SYMBOL_REF:
      /* A constant SYMBOL_REF is valid as a constant attribute test and
         is expanded later by make_canonical into a COND.  In a non-constant
         attribute test, it is left be.  */
      return attr_rtx (SYMBOL_REF, XSTR (exp, 0));

    default:
      message_with_line (attr ? attr->lineno : 0,
			 "invalid operation `%s' for attribute value",
			 GET_RTX_NAME (GET_CODE (exp)));
      have_error = 1;
      break;
    }

  return exp;
}

/* Given an SET_ATTR_ALTERNATIVE expression, convert to the canonical SET.
   It becomes a COND with each test being (eq_attr "alternative" "n") */

static rtx
convert_set_attr_alternative (rtx exp, struct insn_def *id)
{
  int num_alt = id->num_alternatives;
  rtx condexp;
  int i;

  if (XVECLEN (exp, 1) != num_alt)
    {
      message_with_line (id->lineno,
			 "bad number of entries in SET_ATTR_ALTERNATIVE");
      have_error = 1;
      return NULL_RTX;
    }

  /* Make a COND with all tests but the last.  Select the last value via the
     default.  */
  condexp = rtx_alloc (COND);
  XVEC (condexp, 0) = rtvec_alloc ((num_alt - 1) * 2);

  for (i = 0; i < num_alt - 1; i++)
    {
      const char *p;
      p = attr_numeral (i);

      XVECEXP (condexp, 0, 2 * i) = attr_eq (alternative_name, p);
      XVECEXP (condexp, 0, 2 * i + 1) = XVECEXP (exp, 1, i);
    }

  XEXP (condexp, 1) = XVECEXP (exp, 1, i);

  return attr_rtx (SET, attr_rtx (ATTR, XSTR (exp, 0)), condexp);
}

/* Given a SET_ATTR, convert to the appropriate SET.  If a comma-separated
   list of values is given, convert to SET_ATTR_ALTERNATIVE first.  */

static rtx
convert_set_attr (rtx exp, struct insn_def *id)
{
  rtx newexp;
  const char *name_ptr;
  char *p;
  int n;

  /* See how many alternative specified.  */
  n = n_comma_elts (XSTR (exp, 1));
  if (n == 1)
    return attr_rtx (SET,
		     attr_rtx (ATTR, XSTR (exp, 0)),
		     attr_rtx (CONST_STRING, XSTR (exp, 1)));

  newexp = rtx_alloc (SET_ATTR_ALTERNATIVE);
  XSTR (newexp, 0) = XSTR (exp, 0);
  XVEC (newexp, 1) = rtvec_alloc (n);

  /* Process each comma-separated name.  */
  name_ptr = XSTR (exp, 1);
  n = 0;
  while ((p = next_comma_elt (&name_ptr)) != NULL)
    XVECEXP (newexp, 1, n++) = attr_rtx (CONST_STRING, p);

  return convert_set_attr_alternative (newexp, id);
}

/* Scan all definitions, checking for validity.  Also, convert any SET_ATTR
   and SET_ATTR_ALTERNATIVE expressions to the corresponding SET
   expressions.  */

static void
check_defs (void)
{
  struct insn_def *id;
  struct attr_desc *attr;
  int i;
  rtx value;

  for (id = defs; id; id = id->next)
    {
      if (XVEC (id->def, id->vec_idx) == NULL)
	continue;

      for (i = 0; i < XVECLEN (id->def, id->vec_idx); i++)
	{
	  value = XVECEXP (id->def, id->vec_idx, i);
	  switch (GET_CODE (value))
	    {
	    case SET:
	      if (GET_CODE (XEXP (value, 0)) != ATTR)
		{
		  message_with_line (id->lineno, "bad attribute set");
		  have_error = 1;
		  value = NULL_RTX;
		}
	      break;

	    case SET_ATTR_ALTERNATIVE:
	      value = convert_set_attr_alternative (value, id);
	      break;

	    case SET_ATTR:
	      value = convert_set_attr (value, id);
	      break;

	    default:
	      message_with_line (id->lineno, "invalid attribute code %s",
				 GET_RTX_NAME (GET_CODE (value)));
	      have_error = 1;
	      value = NULL_RTX;
	    }
	  if (value == NULL_RTX)
	    continue;

	  if ((attr = find_attr (&XSTR (XEXP (value, 0), 0), 0)) == NULL)
	    {
	      message_with_line (id->lineno, "unknown attribute %s",
				 XSTR (XEXP (value, 0), 0));
	      have_error = 1;
	      continue;
	    }

	  XVECEXP (id->def, id->vec_idx, i) = value;
	  XEXP (value, 1) = check_attr_value (XEXP (value, 1), attr);
	}
    }
}

/* Given a valid expression for an attribute value, remove any IF_THEN_ELSE
   expressions by converting them into a COND.  This removes cases from this
   program.  Also, replace an attribute value of "*" with the default attribute
   value.  */

static rtx
make_canonical (struct attr_desc *attr, rtx exp)
{
  int i;
  rtx newexp;

  switch (GET_CODE (exp))
    {
    case CONST_INT:
      exp = make_numeric_value (INTVAL (exp));
      break;

    case CONST_STRING:
      if (! strcmp (XSTR (exp, 0), "*"))
	{
	  if (attr == 0 || attr->default_val == 0)
	    fatal ("(attr_value \"*\") used in invalid context");
	  exp = attr->default_val->value;
	}
      else
	XSTR (exp, 0) = DEF_ATTR_STRING (XSTR (exp, 0));

      break;

    case SYMBOL_REF:
      if (!attr->is_const || ATTR_IND_SIMPLIFIED_P (exp))
	break;
      /* The SYMBOL_REF is constant for a given run, so mark it as unchanging.
	 This makes the COND something that won't be considered an arbitrary
	 expression by walk_attr_value.  */
      ATTR_IND_SIMPLIFIED_P (exp) = 1;
      exp = check_attr_value (exp, attr);
      break;

    case IF_THEN_ELSE:
      newexp = rtx_alloc (COND);
      XVEC (newexp, 0) = rtvec_alloc (2);
      XVECEXP (newexp, 0, 0) = XEXP (exp, 0);
      XVECEXP (newexp, 0, 1) = XEXP (exp, 1);

      XEXP (newexp, 1) = XEXP (exp, 2);

      exp = newexp;
      /* Fall through to COND case since this is now a COND.  */

    case COND:
      {
	int allsame = 1;
	rtx defval;

	/* First, check for degenerate COND.  */
	if (XVECLEN (exp, 0) == 0)
	  return make_canonical (attr, XEXP (exp, 1));
	defval = XEXP (exp, 1) = make_canonical (attr, XEXP (exp, 1));

	for (i = 0; i < XVECLEN (exp, 0); i += 2)
	  {
	    XVECEXP (exp, 0, i) = copy_boolean (XVECEXP (exp, 0, i));
	    XVECEXP (exp, 0, i + 1)
	      = make_canonical (attr, XVECEXP (exp, 0, i + 1));
	    if (! rtx_equal_p (XVECEXP (exp, 0, i + 1), defval))
	      allsame = 0;
	  }
	if (allsame)
	  return defval;
      }
      break;

    default:
      break;
    }

  return exp;
}

static rtx
copy_boolean (rtx exp)
{
  if (GET_CODE (exp) == AND || GET_CODE (exp) == IOR)
    return attr_rtx (GET_CODE (exp), copy_boolean (XEXP (exp, 0)),
		     copy_boolean (XEXP (exp, 1)));
  if (GET_CODE (exp) == MATCH_OPERAND)
    {
      XSTR (exp, 1) = DEF_ATTR_STRING (XSTR (exp, 1));
      XSTR (exp, 2) = DEF_ATTR_STRING (XSTR (exp, 2));
    }
  else if (GET_CODE (exp) == EQ_ATTR)
    {
      XSTR (exp, 0) = DEF_ATTR_STRING (XSTR (exp, 0));
      XSTR (exp, 1) = DEF_ATTR_STRING (XSTR (exp, 1));
    }

  return exp;
}

/* Given a value and an attribute description, return a `struct attr_value *'
   that represents that value.  This is either an existing structure, if the
   value has been previously encountered, or a newly-created structure.

   `insn_code' is the code of an insn whose attribute has the specified
   value (-2 if not processing an insn).  We ensure that all insns for
   a given value have the same number of alternatives if the value checks
   alternatives.  */

static struct attr_value *
get_attr_value (rtx value, struct attr_desc *attr, int insn_code)
{
  struct attr_value *av;
  int num_alt = 0;

  value = make_canonical (attr, value);
  if (compares_alternatives_p (value))
    {
      if (insn_code < 0 || insn_alternatives == NULL)
	fatal ("(eq_attr \"alternatives\" ...) used in non-insn context");
      else
	num_alt = insn_alternatives[insn_code];
    }

  for (av = attr->first_value; av; av = av->next)
    if (rtx_equal_p (value, av->value)
	&& (num_alt == 0 || av->first_insn == NULL
	    || insn_alternatives[av->first_insn->def->insn_code]))
      return av;

  av = oballoc (sizeof (struct attr_value));
  av->value = value;
  av->next = attr->first_value;
  attr->first_value = av;
  av->first_insn = NULL;
  av->num_insns = 0;
  av->has_asm_insn = 0;

  return av;
}

/* After all DEFINE_DELAYs have been read in, create internal attributes
   to generate the required routines.

   First, we compute the number of delay slots for each insn (as a COND of
   each of the test expressions in DEFINE_DELAYs).  Then, if more than one
   delay type is specified, we compute a similar function giving the
   DEFINE_DELAY ordinal for each insn.

   Finally, for each [DEFINE_DELAY, slot #] pair, we compute an attribute that
   tells whether a given insn can be in that delay slot.

   Normal attribute filling and optimization expands these to contain the
   information needed to handle delay slots.  */

static void
expand_delays (void)
{
  struct delay_desc *delay;
  rtx condexp;
  rtx newexp;
  int i;
  char *p;

  /* First, generate data for `num_delay_slots' function.  */

  condexp = rtx_alloc (COND);
  XVEC (condexp, 0) = rtvec_alloc (num_delays * 2);
  XEXP (condexp, 1) = make_numeric_value (0);

  for (i = 0, delay = delays; delay; i += 2, delay = delay->next)
    {
      XVECEXP (condexp, 0, i) = XEXP (delay->def, 0);
      XVECEXP (condexp, 0, i + 1)
	= make_numeric_value (XVECLEN (delay->def, 1) / 3);
    }

  make_internal_attr (num_delay_slots_str, condexp, ATTR_NONE);

  /* If more than one delay type, do the same for computing the delay type.  */
  if (num_delays > 1)
    {
      condexp = rtx_alloc (COND);
      XVEC (condexp, 0) = rtvec_alloc (num_delays * 2);
      XEXP (condexp, 1) = make_numeric_value (0);

      for (i = 0, delay = delays; delay; i += 2, delay = delay->next)
	{
	  XVECEXP (condexp, 0, i) = XEXP (delay->def, 0);
	  XVECEXP (condexp, 0, i + 1) = make_numeric_value (delay->num);
	}

      make_internal_attr (delay_type_str, condexp, ATTR_SPECIAL);
    }

  /* For each delay possibility and delay slot, compute an eligibility
     attribute for non-annulled insns and for each type of annulled (annul
     if true and annul if false).  */
  for (delay = delays; delay; delay = delay->next)
    {
      for (i = 0; i < XVECLEN (delay->def, 1); i += 3)
	{
	  condexp = XVECEXP (delay->def, 1, i);
	  if (condexp == 0)
	    condexp = false_rtx;
	  newexp = attr_rtx (IF_THEN_ELSE, condexp,
			     make_numeric_value (1), make_numeric_value (0));

	  p = attr_printf (sizeof "*delay__" + MAX_DIGITS * 2,
			   "*delay_%d_%d", delay->num, i / 3);
	  make_internal_attr (p, newexp, ATTR_SPECIAL);

	  if (have_annul_true)
	    {
	      condexp = XVECEXP (delay->def, 1, i + 1);
	      if (condexp == 0) condexp = false_rtx;
	      newexp = attr_rtx (IF_THEN_ELSE, condexp,
				 make_numeric_value (1),
				 make_numeric_value (0));
	      p = attr_printf (sizeof "*annul_true__" + MAX_DIGITS * 2,
			       "*annul_true_%d_%d", delay->num, i / 3);
	      make_internal_attr (p, newexp, ATTR_SPECIAL);
	    }

	  if (have_annul_false)
	    {
	      condexp = XVECEXP (delay->def, 1, i + 2);
	      if (condexp == 0) condexp = false_rtx;
	      newexp = attr_rtx (IF_THEN_ELSE, condexp,
				 make_numeric_value (1),
				 make_numeric_value (0));
	      p = attr_printf (sizeof "*annul_false__" + MAX_DIGITS * 2,
			       "*annul_false_%d_%d", delay->num, i / 3);
	      make_internal_attr (p, newexp, ATTR_SPECIAL);
	    }
	}
    }
}

/* Once all attributes and insns have been read and checked, we construct for
   each attribute value a list of all the insns that have that value for
   the attribute.  */

static void
fill_attr (struct attr_desc *attr)
{
  struct attr_value *av;
  struct insn_ent *ie;
  struct insn_def *id;
  int i;
  rtx value;

  /* Don't fill constant attributes.  The value is independent of
     any particular insn.  */
  if (attr->is_const)
    return;

  for (id = defs; id; id = id->next)
    {
      /* If no value is specified for this insn for this attribute, use the
	 default.  */
      value = NULL;
      if (XVEC (id->def, id->vec_idx))
	for (i = 0; i < XVECLEN (id->def, id->vec_idx); i++)
	  if (! strcmp_check (XSTR (XEXP (XVECEXP (id->def, id->vec_idx, i), 0), 0),
			      attr->name))
	    value = XEXP (XVECEXP (id->def, id->vec_idx, i), 1);

      if (value == NULL)
	av = attr->default_val;
      else
	av = get_attr_value (value, attr, id->insn_code);

      ie = oballoc (sizeof (struct insn_ent));
      ie->def = id;
      insert_insn_ent (av, ie);
    }
}

/* Given an expression EXP, see if it is a COND or IF_THEN_ELSE that has a
   test that checks relative positions of insns (uses MATCH_DUP or PC).
   If so, replace it with what is obtained by passing the expression to
   ADDRESS_FN.  If not but it is a COND or IF_THEN_ELSE, call this routine
   recursively on each value (including the default value).  Otherwise,
   return the value returned by NO_ADDRESS_FN applied to EXP.  */

static rtx
substitute_address (rtx exp, rtx (*no_address_fn) (rtx),
		    rtx (*address_fn) (rtx))
{
  int i;
  rtx newexp;

  if (GET_CODE (exp) == COND)
    {
      /* See if any tests use addresses.  */
      address_used = 0;
      for (i = 0; i < XVECLEN (exp, 0); i += 2)
	walk_attr_value (XVECEXP (exp, 0, i));

      if (address_used)
	return (*address_fn) (exp);

      /* Make a new copy of this COND, replacing each element.  */
      newexp = rtx_alloc (COND);
      XVEC (newexp, 0) = rtvec_alloc (XVECLEN (exp, 0));
      for (i = 0; i < XVECLEN (exp, 0); i += 2)
	{
	  XVECEXP (newexp, 0, i) = XVECEXP (exp, 0, i);
	  XVECEXP (newexp, 0, i + 1)
	    = substitute_address (XVECEXP (exp, 0, i + 1),
				  no_address_fn, address_fn);
	}

      XEXP (newexp, 1) = substitute_address (XEXP (exp, 1),
					     no_address_fn, address_fn);

      return newexp;
    }

  else if (GET_CODE (exp) == IF_THEN_ELSE)
    {
      address_used = 0;
      walk_attr_value (XEXP (exp, 0));
      if (address_used)
	return (*address_fn) (exp);

      return attr_rtx (IF_THEN_ELSE,
		       substitute_address (XEXP (exp, 0),
					   no_address_fn, address_fn),
		       substitute_address (XEXP (exp, 1),
					   no_address_fn, address_fn),
		       substitute_address (XEXP (exp, 2),
					   no_address_fn, address_fn));
    }

  return (*no_address_fn) (exp);
}

/* Make new attributes from the `length' attribute.  The following are made,
   each corresponding to a function called from `shorten_branches' or
   `get_attr_length':

   *insn_default_length		This is the length of the insn to be returned
				by `get_attr_length' before `shorten_branches'
				has been called.  In each case where the length
				depends on relative addresses, the largest
				possible is used.  This routine is also used
				to compute the initial size of the insn.

   *insn_variable_length_p	This returns 1 if the insn's length depends
				on relative addresses, zero otherwise.

   *insn_current_length		This is only called when it is known that the
				insn has a variable length and returns the
				current length, based on relative addresses.
  */

static void
make_length_attrs (void)
{
  static const char *new_names[] =
    {
      "*insn_default_length",
      "*insn_min_length",
      "*insn_variable_length_p",
      "*insn_current_length"
    };
  static rtx (*const no_address_fn[]) (rtx)
    = {identity_fn,identity_fn, zero_fn, zero_fn};
  static rtx (*const address_fn[]) (rtx)
    = {max_fn, min_fn, one_fn, identity_fn};
  size_t i;
  struct attr_desc *length_attr, *new_attr;
  struct attr_value *av, *new_av;
  struct insn_ent *ie, *new_ie;

  /* See if length attribute is defined.  If so, it must be numeric.  Make
     it special so we don't output anything for it.  */
  length_attr = find_attr (&length_str, 0);
  if (length_attr == 0)
    return;

  if (! length_attr->is_numeric)
    fatal ("length attribute must be numeric");

  length_attr->is_const = 0;
  length_attr->is_special = 1;

  /* Make each new attribute, in turn.  */
  for (i = 0; i < ARRAY_SIZE (new_names); i++)
    {
      make_internal_attr (new_names[i],
			  substitute_address (length_attr->default_val->value,
					      no_address_fn[i], address_fn[i]),
			  ATTR_NONE);
      new_attr = find_attr (&new_names[i], 0);
      for (av = length_attr->first_value; av; av = av->next)
	for (ie = av->first_insn; ie; ie = ie->next)
	  {
	    new_av = get_attr_value (substitute_address (av->value,
							 no_address_fn[i],
							 address_fn[i]),
				     new_attr, ie->def->insn_code);
	    new_ie = oballoc (sizeof (struct insn_ent));
	    new_ie->def = ie->def;
	    insert_insn_ent (new_av, new_ie);
	  }
    }
}

/* Utility functions called from above routine.  */

static rtx
identity_fn (rtx exp)
{
  return exp;
}

static rtx
zero_fn (rtx exp ATTRIBUTE_UNUSED)
{
  return make_numeric_value (0);
}

static rtx
one_fn (rtx exp ATTRIBUTE_UNUSED)
{
  return make_numeric_value (1);
}

static rtx
max_fn (rtx exp)
{
  int unknown;
  return make_numeric_value (max_attr_value (exp, &unknown));
}

static rtx
min_fn (rtx exp)
{
  int unknown;
  return make_numeric_value (min_attr_value (exp, &unknown));
}

static void
write_length_unit_log (void)
{
  struct attr_desc *length_attr = find_attr (&length_str, 0);
  struct attr_value *av;
  struct insn_ent *ie;
  unsigned int length_unit_log, length_or;
  int unknown = 0;

  if (length_attr == 0)
    return;
  length_or = or_attr_value (length_attr->default_val->value, &unknown);
  for (av = length_attr->first_value; av; av = av->next)
    for (ie = av->first_insn; ie; ie = ie->next)
      length_or |= or_attr_value (av->value, &unknown);

  if (unknown)
    length_unit_log = 0;
  else
    {
      length_or = ~length_or;
      for (length_unit_log = 0; length_or & 1; length_or >>= 1)
	length_unit_log++;
    }
  printf ("const int length_unit_log = %u;\n", length_unit_log);
}

/* Take a COND expression and see if any of the conditions in it can be
   simplified.  If any are known true or known false for the particular insn
   code, the COND can be further simplified.

   Also call ourselves on any COND operations that are values of this COND.

   We do not modify EXP; rather, we make and return a new rtx.  */

static rtx
simplify_cond (rtx exp, int insn_code, int insn_index)
{
  int i, j;
  /* We store the desired contents here,
     then build a new expression if they don't match EXP.  */
  rtx defval = XEXP (exp, 1);
  rtx new_defval = XEXP (exp, 1);
  int len = XVECLEN (exp, 0);
  rtx *tests = XNEWVEC (rtx, len);
  int allsame = 1;
  rtx ret;

  /* This lets us free all storage allocated below, if appropriate.  */
  obstack_finish (rtl_obstack);

  memcpy (tests, XVEC (exp, 0)->elem, len * sizeof (rtx));

  /* See if default value needs simplification.  */
  if (GET_CODE (defval) == COND)
    new_defval = simplify_cond (defval, insn_code, insn_index);

  /* Simplify the subexpressions, and see what tests we can get rid of.  */

  for (i = 0; i < len; i += 2)
    {
      rtx newtest, newval;

      /* Simplify this test.  */
      newtest = simplify_test_exp_in_temp (tests[i], insn_code, insn_index);
      tests[i] = newtest;

      newval = tests[i + 1];
      /* See if this value may need simplification.  */
      if (GET_CODE (newval) == COND)
	newval = simplify_cond (newval, insn_code, insn_index);

      /* Look for ways to delete or combine this test.  */
      if (newtest == true_rtx)
	{
	  /* If test is true, make this value the default
	     and discard this + any following tests.  */
	  len = i;
	  defval = tests[i + 1];
	  new_defval = newval;
	}

      else if (newtest == false_rtx)
	{
	  /* If test is false, discard it and its value.  */
	  for (j = i; j < len - 2; j++)
	    tests[j] = tests[j + 2];
	  i -= 2;
	  len -= 2;
	}

      else if (i > 0 && attr_equal_p (newval, tests[i - 1]))
	{
	  /* If this value and the value for the prev test are the same,
	     merge the tests.  */

	  tests[i - 2]
	    = insert_right_side (IOR, tests[i - 2], newtest,
				 insn_code, insn_index);

	  /* Delete this test/value.  */
	  for (j = i; j < len - 2; j++)
	    tests[j] = tests[j + 2];
	  len -= 2;
	  i -= 2;
	}

      else
	tests[i + 1] = newval;
    }

  /* If the last test in a COND has the same value
     as the default value, that test isn't needed.  */

  while (len > 0 && attr_equal_p (tests[len - 1], new_defval))
    len -= 2;

  /* See if we changed anything.  */
  if (len != XVECLEN (exp, 0) || new_defval != XEXP (exp, 1))
    allsame = 0;
  else
    for (i = 0; i < len; i++)
      if (! attr_equal_p (tests[i], XVECEXP (exp, 0, i)))
	{
	  allsame = 0;
	  break;
	}

  if (len == 0)
    {
      if (GET_CODE (defval) == COND)
	ret = simplify_cond (defval, insn_code, insn_index);
      else
	ret = defval;
    }
  else if (allsame)
    ret = exp;
  else
    {
      rtx newexp = rtx_alloc (COND);

      XVEC (newexp, 0) = rtvec_alloc (len);
      memcpy (XVEC (newexp, 0)->elem, tests, len * sizeof (rtx));
      XEXP (newexp, 1) = new_defval;
      ret = newexp;
    }
  free (tests);
  return ret;
}

/* Remove an insn entry from an attribute value.  */

static void
remove_insn_ent (struct attr_value *av, struct insn_ent *ie)
{
  struct insn_ent *previe;

  if (av->first_insn == ie)
    av->first_insn = ie->next;
  else
    {
      for (previe = av->first_insn; previe->next != ie; previe = previe->next)
	;
      previe->next = ie->next;
    }

  av->num_insns--;
  if (ie->def->insn_code == -1)
    av->has_asm_insn = 0;

  num_insn_ents--;
}

/* Insert an insn entry in an attribute value list.  */

static void
insert_insn_ent (struct attr_value *av, struct insn_ent *ie)
{
  ie->next = av->first_insn;
  av->first_insn = ie;
  av->num_insns++;
  if (ie->def->insn_code == -1)
    av->has_asm_insn = 1;

  num_insn_ents++;
}

/* This is a utility routine to take an expression that is a tree of either
   AND or IOR expressions and insert a new term.  The new term will be
   inserted at the right side of the first node whose code does not match
   the root.  A new node will be created with the root's code.  Its left
   side will be the old right side and its right side will be the new
   term.

   If the `term' is itself a tree, all its leaves will be inserted.  */

static rtx
insert_right_side (enum rtx_code code, rtx exp, rtx term, int insn_code, int insn_index)
{
  rtx newexp;

  /* Avoid consing in some special cases.  */
  if (code == AND && term == true_rtx)
    return exp;
  if (code == AND && term == false_rtx)
    return false_rtx;
  if (code == AND && exp == true_rtx)
    return term;
  if (code == AND && exp == false_rtx)
    return false_rtx;
  if (code == IOR && term == true_rtx)
    return true_rtx;
  if (code == IOR && term == false_rtx)
    return exp;
  if (code == IOR && exp == true_rtx)
    return true_rtx;
  if (code == IOR && exp == false_rtx)
    return term;
  if (attr_equal_p (exp, term))
    return exp;

  if (GET_CODE (term) == code)
    {
      exp = insert_right_side (code, exp, XEXP (term, 0),
			       insn_code, insn_index);
      exp = insert_right_side (code, exp, XEXP (term, 1),
			       insn_code, insn_index);

      return exp;
    }

  if (GET_CODE (exp) == code)
    {
      rtx new = insert_right_side (code, XEXP (exp, 1),
				   term, insn_code, insn_index);
      if (new != XEXP (exp, 1))
	/* Make a copy of this expression and call recursively.  */
	newexp = attr_rtx (code, XEXP (exp, 0), new);
      else
	newexp = exp;
    }
  else
    {
      /* Insert the new term.  */
      newexp = attr_rtx (code, exp, term);
    }

  return simplify_test_exp_in_temp (newexp, insn_code, insn_index);
}

/* If we have an expression which AND's a bunch of
	(not (eq_attrq "alternative" "n"))
   terms, we may have covered all or all but one of the possible alternatives.
   If so, we can optimize.  Similarly for IOR's of EQ_ATTR.

   This routine is passed an expression and either AND or IOR.  It returns a
   bitmask indicating which alternatives are mentioned within EXP.  */

static int
compute_alternative_mask (rtx exp, enum rtx_code code)
{
  const char *string;
  if (GET_CODE (exp) == code)
    return compute_alternative_mask (XEXP (exp, 0), code)
	   | compute_alternative_mask (XEXP (exp, 1), code);

  else if (code == AND && GET_CODE (exp) == NOT
	   && GET_CODE (XEXP (exp, 0)) == EQ_ATTR
	   && XSTR (XEXP (exp, 0), 0) == alternative_name)
    string = XSTR (XEXP (exp, 0), 1);

  else if (code == IOR && GET_CODE (exp) == EQ_ATTR
	   && XSTR (exp, 0) == alternative_name)
    string = XSTR (exp, 1);

  else if (GET_CODE (exp) == EQ_ATTR_ALT)
    {
      if (code == AND && XINT (exp, 1))
	return XINT (exp, 0);

      if (code == IOR && !XINT (exp, 1))
	return XINT (exp, 0);

      return 0;
    }
  else
    return 0;

  if (string[1] == 0)
    return 1 << (string[0] - '0');
  return 1 << atoi (string);
}

/* Given I, a single-bit mask, return RTX to compare the `alternative'
   attribute with the value represented by that bit.  */

static rtx
make_alternative_compare (int mask)
{
  return mk_attr_alt (mask);
}

/* If we are processing an (eq_attr "attr" "value") test, we find the value
   of "attr" for this insn code.  From that value, we can compute a test
   showing when the EQ_ATTR will be true.  This routine performs that
   computation.  If a test condition involves an address, we leave the EQ_ATTR
   intact because addresses are only valid for the `length' attribute.

   EXP is the EQ_ATTR expression and VALUE is the value of that attribute
   for the insn corresponding to INSN_CODE and INSN_INDEX.  */

static rtx
evaluate_eq_attr (rtx exp, rtx value, int insn_code, int insn_index)
{
  rtx orexp, andexp;
  rtx right;
  rtx newexp;
  int i;

  switch (GET_CODE (value))
    {
    case CONST_STRING:
      if (! strcmp_check (XSTR (value, 0), XSTR (exp, 1)))
	newexp = true_rtx;
      else
	newexp = false_rtx;
      break;
      
    case SYMBOL_REF:
      {
	char *p;
	char string[256];
	
	gcc_assert (GET_CODE (exp) == EQ_ATTR);
	gcc_assert (strlen (XSTR (exp, 0)) + strlen (XSTR (exp, 1)) + 2
		    <= 256);
	
	strcpy (string, XSTR (exp, 0));
	strcat (string, "_");
	strcat (string, XSTR (exp, 1));
	for (p = string; *p; p++)
	  *p = TOUPPER (*p);
	
	newexp = attr_rtx (EQ, value,
			   attr_rtx (SYMBOL_REF,
				     DEF_ATTR_STRING (string)));
	break;
      }

    case COND:
      /* We construct an IOR of all the cases for which the
	 requested attribute value is present.  Since we start with
	 FALSE, if it is not present, FALSE will be returned.
	  
	 Each case is the AND of the NOT's of the previous conditions with the
	 current condition; in the default case the current condition is TRUE.
	  
	 For each possible COND value, call ourselves recursively.
	  
	 The extra TRUE and FALSE expressions will be eliminated by another
	 call to the simplification routine.  */

      orexp = false_rtx;
      andexp = true_rtx;

      for (i = 0; i < XVECLEN (value, 0); i += 2)
	{
	  rtx this = simplify_test_exp_in_temp (XVECEXP (value, 0, i),
						insn_code, insn_index);

	  right = insert_right_side (AND, andexp, this,
				     insn_code, insn_index);
	  right = insert_right_side (AND, right,
				     evaluate_eq_attr (exp,
						       XVECEXP (value, 0,
								i + 1),
						       insn_code, insn_index),
				     insn_code, insn_index);
	  orexp = insert_right_side (IOR, orexp, right,
				     insn_code, insn_index);

	  /* Add this condition into the AND expression.  */
	  newexp = attr_rtx (NOT, this);
	  andexp = insert_right_side (AND, andexp, newexp,
				      insn_code, insn_index);
	}

      /* Handle the default case.  */
      right = insert_right_side (AND, andexp,
				 evaluate_eq_attr (exp, XEXP (value, 1),
						   insn_code, insn_index),
				 insn_code, insn_index);
      newexp = insert_right_side (IOR, orexp, right, insn_code, insn_index);
      break;

    default:
      gcc_unreachable ();
    }

  /* If uses an address, must return original expression.  But set the
     ATTR_IND_SIMPLIFIED_P bit so we don't try to simplify it again.  */

  address_used = 0;
  walk_attr_value (newexp);

  if (address_used)
    {
      if (! ATTR_IND_SIMPLIFIED_P (exp))
	return copy_rtx_unchanging (exp);
      return exp;
    }
  else
    return newexp;
}

/* This routine is called when an AND of a term with a tree of AND's is
   encountered.  If the term or its complement is present in the tree, it
   can be replaced with TRUE or FALSE, respectively.

   Note that (eq_attr "att" "v1") and (eq_attr "att" "v2") cannot both
   be true and hence are complementary.

   There is one special case:  If we see
	(and (not (eq_attr "att" "v1"))
	     (eq_attr "att" "v2"))
   this can be replaced by (eq_attr "att" "v2").  To do this we need to
   replace the term, not anything in the AND tree.  So we pass a pointer to
   the term.  */

static rtx
simplify_and_tree (rtx exp, rtx *pterm, int insn_code, int insn_index)
{
  rtx left, right;
  rtx newexp;
  rtx temp;
  int left_eliminates_term, right_eliminates_term;

  if (GET_CODE (exp) == AND)
    {
      left  = simplify_and_tree (XEXP (exp, 0), pterm, insn_code, insn_index);
      right = simplify_and_tree (XEXP (exp, 1), pterm, insn_code, insn_index);
      if (left != XEXP (exp, 0) || right != XEXP (exp, 1))
	{
	  newexp = attr_rtx (AND, left, right);

	  exp = simplify_test_exp_in_temp (newexp, insn_code, insn_index);
	}
    }

  else if (GET_CODE (exp) == IOR)
    {
      /* For the IOR case, we do the same as above, except that we can
         only eliminate `term' if both sides of the IOR would do so.  */
      temp = *pterm;
      left = simplify_and_tree (XEXP (exp, 0), &temp, insn_code, insn_index);
      left_eliminates_term = (temp == true_rtx);

      temp = *pterm;
      right = simplify_and_tree (XEXP (exp, 1), &temp, insn_code, insn_index);
      right_eliminates_term = (temp == true_rtx);

      if (left_eliminates_term && right_eliminates_term)
	*pterm = true_rtx;

      if (left != XEXP (exp, 0) || right != XEXP (exp, 1))
	{
	  newexp = attr_rtx (IOR, left, right);

	  exp = simplify_test_exp_in_temp (newexp, insn_code, insn_index);
	}
    }

  /* Check for simplifications.  Do some extra checking here since this
     routine is called so many times.  */

  if (exp == *pterm)
    return true_rtx;

  else if (GET_CODE (exp) == NOT && XEXP (exp, 0) == *pterm)
    return false_rtx;

  else if (GET_CODE (*pterm) == NOT && exp == XEXP (*pterm, 0))
    return false_rtx;

  else if (GET_CODE (exp) == EQ_ATTR_ALT && GET_CODE (*pterm) == EQ_ATTR_ALT)
    {
      if (attr_alt_subset_p (*pterm, exp))
	return true_rtx;

      if (attr_alt_subset_of_compl_p (*pterm, exp))
	return false_rtx;

      if (attr_alt_subset_p (exp, *pterm))
	*pterm = true_rtx;
	
      return exp;
    }

  else if (GET_CODE (exp) == EQ_ATTR && GET_CODE (*pterm) == EQ_ATTR)
    {
      if (XSTR (exp, 0) != XSTR (*pterm, 0))
	return exp;

      if (! strcmp_check (XSTR (exp, 1), XSTR (*pterm, 1)))
	return true_rtx;
      else
	return false_rtx;
    }

  else if (GET_CODE (*pterm) == EQ_ATTR && GET_CODE (exp) == NOT
	   && GET_CODE (XEXP (exp, 0)) == EQ_ATTR)
    {
      if (XSTR (*pterm, 0) != XSTR (XEXP (exp, 0), 0))
	return exp;

      if (! strcmp_check (XSTR (*pterm, 1), XSTR (XEXP (exp, 0), 1)))
	return false_rtx;
      else
	return true_rtx;
    }

  else if (GET_CODE (exp) == EQ_ATTR && GET_CODE (*pterm) == NOT
	   && GET_CODE (XEXP (*pterm, 0)) == EQ_ATTR)
    {
      if (XSTR (exp, 0) != XSTR (XEXP (*pterm, 0), 0))
	return exp;

      if (! strcmp_check (XSTR (exp, 1), XSTR (XEXP (*pterm, 0), 1)))
	return false_rtx;
      else
	*pterm = true_rtx;
    }

  else if (GET_CODE (exp) == NOT && GET_CODE (*pterm) == NOT)
    {
      if (attr_equal_p (XEXP (exp, 0), XEXP (*pterm, 0)))
	return true_rtx;
    }

  else if (GET_CODE (exp) == NOT)
    {
      if (attr_equal_p (XEXP (exp, 0), *pterm))
	return false_rtx;
    }

  else if (GET_CODE (*pterm) == NOT)
    {
      if (attr_equal_p (XEXP (*pterm, 0), exp))
	return false_rtx;
    }

  else if (attr_equal_p (exp, *pterm))
    return true_rtx;

  return exp;
}

/* Similar to `simplify_and_tree', but for IOR trees.  */

static rtx
simplify_or_tree (rtx exp, rtx *pterm, int insn_code, int insn_index)
{
  rtx left, right;
  rtx newexp;
  rtx temp;
  int left_eliminates_term, right_eliminates_term;

  if (GET_CODE (exp) == IOR)
    {
      left  = simplify_or_tree (XEXP (exp, 0), pterm, insn_code, insn_index);
      right = simplify_or_tree (XEXP (exp, 1), pterm, insn_code, insn_index);
      if (left != XEXP (exp, 0) || right != XEXP (exp, 1))
	{
	  newexp = attr_rtx (GET_CODE (exp), left, right);

	  exp = simplify_test_exp_in_temp (newexp, insn_code, insn_index);
	}
    }

  else if (GET_CODE (exp) == AND)
    {
      /* For the AND case, we do the same as above, except that we can
         only eliminate `term' if both sides of the AND would do so.  */
      temp = *pterm;
      left = simplify_or_tree (XEXP (exp, 0), &temp, insn_code, insn_index);
      left_eliminates_term = (temp == false_rtx);

      temp = *pterm;
      right = simplify_or_tree (XEXP (exp, 1), &temp, insn_code, insn_index);
      right_eliminates_term = (temp == false_rtx);

      if (left_eliminates_term && right_eliminates_term)
	*pterm = false_rtx;

      if (left != XEXP (exp, 0) || right != XEXP (exp, 1))
	{
	  newexp = attr_rtx (GET_CODE (exp), left, right);

	  exp = simplify_test_exp_in_temp (newexp, insn_code, insn_index);
	}
    }

  if (attr_equal_p (exp, *pterm))
    return false_rtx;

  else if (GET_CODE (exp) == NOT && attr_equal_p (XEXP (exp, 0), *pterm))
    return true_rtx;

  else if (GET_CODE (*pterm) == NOT && attr_equal_p (XEXP (*pterm, 0), exp))
    return true_rtx;

  else if (GET_CODE (*pterm) == EQ_ATTR && GET_CODE (exp) == NOT
	   && GET_CODE (XEXP (exp, 0)) == EQ_ATTR
	   && XSTR (*pterm, 0) == XSTR (XEXP (exp, 0), 0))
    *pterm = false_rtx;

  else if (GET_CODE (exp) == EQ_ATTR && GET_CODE (*pterm) == NOT
	   && GET_CODE (XEXP (*pterm, 0)) == EQ_ATTR
	   && XSTR (exp, 0) == XSTR (XEXP (*pterm, 0), 0))
    return false_rtx;

  return exp;
}

/* Compute approximate cost of the expression.  Used to decide whether
   expression is cheap enough for inline.  */
static int
attr_rtx_cost (rtx x)
{
  int cost = 0;
  enum rtx_code code;
  if (!x)
    return 0;
  code = GET_CODE (x);
  switch (code)
    {
    case MATCH_OPERAND:
      if (XSTR (x, 1)[0])
	return 10;
      else
	return 0;

    case EQ_ATTR_ALT:
      return 0;

    case EQ_ATTR:
      /* Alternatives don't result into function call.  */
      if (!strcmp_check (XSTR (x, 0), alternative_name))
	return 0;
      else
	return 5;
    default:
      {
	int i, j;
	const char *fmt = GET_RTX_FORMAT (code);
	for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
	  {
	    switch (fmt[i])
	      {
	      case 'V':
	      case 'E':
		for (j = 0; j < XVECLEN (x, i); j++)
		  cost += attr_rtx_cost (XVECEXP (x, i, j));
		break;
	      case 'e':
		cost += attr_rtx_cost (XEXP (x, i));
		break;
	      }
	  }
      }
      break;
    }
  return cost;
}

/* Simplify test expression and use temporary obstack in order to avoid
   memory bloat.  Use ATTR_IND_SIMPLIFIED to avoid unnecessary simplifications
   and avoid unnecessary copying if possible.  */

static rtx
simplify_test_exp_in_temp (rtx exp, int insn_code, int insn_index)
{
  rtx x;
  struct obstack *old;
  if (ATTR_IND_SIMPLIFIED_P (exp))
    return exp;
  old = rtl_obstack;
  rtl_obstack = temp_obstack;
  x = simplify_test_exp (exp, insn_code, insn_index);
  rtl_obstack = old;
  if (x == exp || rtl_obstack == temp_obstack)
    return x;
  return attr_copy_rtx (x);
}

/* Returns true if S1 is a subset of S2.  */

static bool
attr_alt_subset_p (rtx s1, rtx s2)
{
  switch ((XINT (s1, 1) << 1) | XINT (s2, 1))
    {
    case (0 << 1) | 0:
      return !(XINT (s1, 0) &~ XINT (s2, 0));

    case (0 << 1) | 1:
      return !(XINT (s1, 0) & XINT (s2, 0));

    case (1 << 1) | 0:
      return false;

    case (1 << 1) | 1:
      return !(XINT (s2, 0) &~ XINT (s1, 0));

    default:
      gcc_unreachable ();
    }
}

/* Returns true if S1 is a subset of complement of S2.  */

static bool
attr_alt_subset_of_compl_p (rtx s1, rtx s2)
{
  switch ((XINT (s1, 1) << 1) | XINT (s2, 1))
    {
    case (0 << 1) | 0:
      return !(XINT (s1, 0) & XINT (s2, 0));

    case (0 << 1) | 1:
      return !(XINT (s1, 0) & ~XINT (s2, 0));

    case (1 << 1) | 0:
      return !(XINT (s2, 0) &~ XINT (s1, 0));

    case (1 << 1) | 1:
      return false;

    default:
      gcc_unreachable ();
    }
}

/* Return EQ_ATTR_ALT expression representing intersection of S1 and S2.  */

static rtx
attr_alt_intersection (rtx s1, rtx s2)
{
  rtx result = rtx_alloc (EQ_ATTR_ALT);

  switch ((XINT (s1, 1) << 1) | XINT (s2, 1))
    {
    case (0 << 1) | 0:
      XINT (result, 0) = XINT (s1, 0) & XINT (s2, 0);
      break;
    case (0 << 1) | 1:
      XINT (result, 0) = XINT (s1, 0) & ~XINT (s2, 0);
      break;
    case (1 << 1) | 0:
      XINT (result, 0) = XINT (s2, 0) & ~XINT (s1, 0);
      break;
    case (1 << 1) | 1:
      XINT (result, 0) = XINT (s1, 0) | XINT (s2, 0);
      break;
    default:
      gcc_unreachable ();
    }
  XINT (result, 1) = XINT (s1, 1) & XINT (s2, 1);

  return result;
}

/* Return EQ_ATTR_ALT expression representing union of S1 and S2.  */

static rtx
attr_alt_union (rtx s1, rtx s2)
{
  rtx result = rtx_alloc (EQ_ATTR_ALT);

  switch ((XINT (s1, 1) << 1) | XINT (s2, 1))
    {
    case (0 << 1) | 0:
      XINT (result, 0) = XINT (s1, 0) | XINT (s2, 0);
      break;
    case (0 << 1) | 1:
      XINT (result, 0) = XINT (s2, 0) & ~XINT (s1, 0);
      break;
    case (1 << 1) | 0:
      XINT (result, 0) = XINT (s1, 0) & ~XINT (s2, 0);
      break;
    case (1 << 1) | 1:
      XINT (result, 0) = XINT (s1, 0) & XINT (s2, 0);
      break;
    default:
      gcc_unreachable ();
    }

  XINT (result, 1) = XINT (s1, 1) | XINT (s2, 1);
  return result;
}

/* Return EQ_ATTR_ALT expression representing complement of S.  */

static rtx
attr_alt_complement (rtx s)
{
  rtx result = rtx_alloc (EQ_ATTR_ALT);

  XINT (result, 0) = XINT (s, 0);
  XINT (result, 1) = 1 - XINT (s, 1);

  return result;
}

/* Return EQ_ATTR_ALT expression representing set containing elements set
   in E.  */

static rtx
mk_attr_alt (int e)
{
  rtx result = rtx_alloc (EQ_ATTR_ALT);

  XINT (result, 0) = e;
  XINT (result, 1) = 0;

  return result;
}

/* Given an expression, see if it can be simplified for a particular insn
   code based on the values of other attributes being tested.  This can
   eliminate nested get_attr_... calls.

   Note that if an endless recursion is specified in the patterns, the
   optimization will loop.  However, it will do so in precisely the cases where
   an infinite recursion loop could occur during compilation.  It's better that
   it occurs here!  */

static rtx
simplify_test_exp (rtx exp, int insn_code, int insn_index)
{
  rtx left, right;
  struct attr_desc *attr;
  struct attr_value *av;
  struct insn_ent *ie;
  int i;
  rtx newexp = exp;
  bool left_alt, right_alt;

  /* Don't re-simplify something we already simplified.  */
  if (ATTR_IND_SIMPLIFIED_P (exp) || ATTR_CURR_SIMPLIFIED_P (exp))
    return exp;

  switch (GET_CODE (exp))
    {
    case AND:
      left = SIMPLIFY_TEST_EXP (XEXP (exp, 0), insn_code, insn_index);
      if (left == false_rtx)
	return false_rtx;
      right = SIMPLIFY_TEST_EXP (XEXP (exp, 1), insn_code, insn_index);
      if (right == false_rtx)
	return false_rtx;

      if (GET_CODE (left) == EQ_ATTR_ALT
	  && GET_CODE (right) == EQ_ATTR_ALT)
	{
	  exp = attr_alt_intersection (left, right);
	  return simplify_test_exp (exp, insn_code, insn_index);
	}

      /* If either side is an IOR and we have (eq_attr "alternative" ..")
	 present on both sides, apply the distributive law since this will
	 yield simplifications.  */
      if ((GET_CODE (left) == IOR || GET_CODE (right) == IOR)
	  && compute_alternative_mask (left, IOR)
	  && compute_alternative_mask (right, IOR))
	{
	  if (GET_CODE (left) == IOR)
	    {
	      rtx tem = left;
	      left = right;
	      right = tem;
	    }

	  newexp = attr_rtx (IOR,
			     attr_rtx (AND, left, XEXP (right, 0)),
			     attr_rtx (AND, left, XEXP (right, 1)));

	  return SIMPLIFY_TEST_EXP (newexp, insn_code, insn_index);
	}

      /* Try with the term on both sides.  */
      right = simplify_and_tree (right, &left, insn_code, insn_index);
      if (left == XEXP (exp, 0) && right == XEXP (exp, 1))
	left = simplify_and_tree (left, &right, insn_code, insn_index);

      if (left == false_rtx || right == false_rtx)
	return false_rtx;
      else if (left == true_rtx)
	{
	  return right;
	}
      else if (right == true_rtx)
	{
	  return left;
	}
      /* See if all or all but one of the insn's alternatives are specified
	 in this tree.  Optimize if so.  */

      if (GET_CODE (left) == NOT)
	left_alt = (GET_CODE (XEXP (left, 0)) == EQ_ATTR
		    && XSTR (XEXP (left, 0), 0) == alternative_name);
      else
	left_alt = (GET_CODE (left) == EQ_ATTR_ALT
		    && XINT (left, 1));

      if (GET_CODE (right) == NOT)
	right_alt = (GET_CODE (XEXP (right, 0)) == EQ_ATTR
		     && XSTR (XEXP (right, 0), 0) == alternative_name);
      else
	right_alt = (GET_CODE (right) == EQ_ATTR_ALT
		     && XINT (right, 1));

      if (insn_code >= 0
	  && (GET_CODE (left) == AND
	      || left_alt
	      || GET_CODE (right) == AND
	      || right_alt))
	{
	  i = compute_alternative_mask (exp, AND);
	  if (i & ~insn_alternatives[insn_code])
	    fatal ("invalid alternative specified for pattern number %d",
		   insn_index);

	  /* If all alternatives are excluded, this is false.  */
	  i ^= insn_alternatives[insn_code];
	  if (i == 0)
	    return false_rtx;
	  else if ((i & (i - 1)) == 0 && insn_alternatives[insn_code] > 1)
	    {
	      /* If just one excluded, AND a comparison with that one to the
		 front of the tree.  The others will be eliminated by
		 optimization.  We do not want to do this if the insn has one
		 alternative and we have tested none of them!  */
	      left = make_alternative_compare (i);
	      right = simplify_and_tree (exp, &left, insn_code, insn_index);
	      newexp = attr_rtx (AND, left, right);

	      return SIMPLIFY_TEST_EXP (newexp, insn_code, insn_index);
	    }
	}

      if (left != XEXP (exp, 0) || right != XEXP (exp, 1))
	{
	  newexp = attr_rtx (AND, left, right);
	  return SIMPLIFY_TEST_EXP (newexp, insn_code, insn_index);
	}
      break;

    case IOR:
      left = SIMPLIFY_TEST_EXP (XEXP (exp, 0), insn_code, insn_index);
      if (left == true_rtx)
	return true_rtx;
      right = SIMPLIFY_TEST_EXP (XEXP (exp, 1), insn_code, insn_index);
      if (right == true_rtx)
	return true_rtx;

      if (GET_CODE (left) == EQ_ATTR_ALT
	  && GET_CODE (right) == EQ_ATTR_ALT)
	{
	  exp = attr_alt_union (left, right);
	  return simplify_test_exp (exp, insn_code, insn_index);
	}

      right = simplify_or_tree (right, &left, insn_code, insn_index);
      if (left == XEXP (exp, 0) && right == XEXP (exp, 1))
	left = simplify_or_tree (left, &right, insn_code, insn_index);

      if (right == true_rtx || left == true_rtx)
	return true_rtx;
      else if (left == false_rtx)
	{
	  return right;
	}
      else if (right == false_rtx)
	{
	  return left;
	}

      /* Test for simple cases where the distributive law is useful.  I.e.,
	    convert (ior (and (x) (y))
			 (and (x) (z)))
	    to      (and (x)
			 (ior (y) (z)))
       */

      else if (GET_CODE (left) == AND && GET_CODE (right) == AND
	       && attr_equal_p (XEXP (left, 0), XEXP (right, 0)))
	{
	  newexp = attr_rtx (IOR, XEXP (left, 1), XEXP (right, 1));

	  left = XEXP (left, 0);
	  right = newexp;
	  newexp = attr_rtx (AND, left, right);
	  return SIMPLIFY_TEST_EXP (newexp, insn_code, insn_index);
	}

      /* See if all or all but one of the insn's alternatives are specified
	 in this tree.  Optimize if so.  */

      else if (insn_code >= 0
	       && (GET_CODE (left) == IOR
		   || (GET_CODE (left) == EQ_ATTR_ALT
		       && !XINT (left, 1))
		   || (GET_CODE (left) == EQ_ATTR
		       && XSTR (left, 0) == alternative_name)
		   || GET_CODE (right) == IOR
		   || (GET_CODE (right) == EQ_ATTR_ALT
		       && !XINT (right, 1))
		   || (GET_CODE (right) == EQ_ATTR
		       && XSTR (right, 0) == alternative_name)))
	{
	  i = compute_alternative_mask (exp, IOR);
	  if (i & ~insn_alternatives[insn_code])
	    fatal ("invalid alternative specified for pattern number %d",
		   insn_index);

	  /* If all alternatives are included, this is true.  */
	  i ^= insn_alternatives[insn_code];
	  if (i == 0)
	    return true_rtx;
	  else if ((i & (i - 1)) == 0 && insn_alternatives[insn_code] > 1)
	    {
	      /* If just one excluded, IOR a comparison with that one to the
		 front of the tree.  The others will be eliminated by
		 optimization.  We do not want to do this if the insn has one
		 alternative and we have tested none of them!  */
	      left = make_alternative_compare (i);
	      right = simplify_and_tree (exp, &left, insn_code, insn_index);
	      newexp = attr_rtx (IOR, attr_rtx (NOT, left), right);

	      return SIMPLIFY_TEST_EXP (newexp, insn_code, insn_index);
	    }
	}

      if (left != XEXP (exp, 0) || right != XEXP (exp, 1))
	{
	  newexp = attr_rtx (IOR, left, right);
	  return SIMPLIFY_TEST_EXP (newexp, insn_code, insn_index);
	}
      break;

    case NOT:
      if (GET_CODE (XEXP (exp, 0)) == NOT)
	{
	  left = SIMPLIFY_TEST_EXP (XEXP (XEXP (exp, 0), 0),
				    insn_code, insn_index);
	  return left;
	}

      left = SIMPLIFY_TEST_EXP (XEXP (exp, 0), insn_code, insn_index);
      if (GET_CODE (left) == NOT)
	return XEXP (left, 0);

      if (left == false_rtx)
	return true_rtx;
      if (left == true_rtx)
	return false_rtx;

      if (GET_CODE (left) == EQ_ATTR_ALT)
	{
	  exp = attr_alt_complement (left);
	  return simplify_test_exp (exp, insn_code, insn_index);
	}

      /* Try to apply De`Morgan's laws.  */
      if (GET_CODE (left) == IOR)
	{
	  newexp = attr_rtx (AND,
			     attr_rtx (NOT, XEXP (left, 0)),
			     attr_rtx (NOT, XEXP (left, 1)));

	  newexp = SIMPLIFY_TEST_EXP (newexp, insn_code, insn_index);
	}
      else if (GET_CODE (left) == AND)
	{
	  newexp = attr_rtx (IOR,
			     attr_rtx (NOT, XEXP (left, 0)),
			     attr_rtx (NOT, XEXP (left, 1)));

	  newexp = SIMPLIFY_TEST_EXP (newexp, insn_code, insn_index);
	}
      else if (left != XEXP (exp, 0))
	{
	  newexp = attr_rtx (NOT, left);
	}
      break;

    case EQ_ATTR_ALT:
      if (!XINT (exp, 0))
	return XINT (exp, 1) ? true_rtx : false_rtx;
      break;

    case EQ_ATTR:
      if (XSTR (exp, 0) == alternative_name)
	{
	  newexp = mk_attr_alt (1 << atoi (XSTR (exp, 1)));
	  break;
	}

      /* Look at the value for this insn code in the specified attribute.
	 We normally can replace this comparison with the condition that
	 would give this insn the values being tested for.  */
      if (insn_code >= 0
	  && (attr = find_attr (&XSTR (exp, 0), 0)) != NULL)
	for (av = attr->first_value; av; av = av->next)
	  for (ie = av->first_insn; ie; ie = ie->next)
	    if (ie->def->insn_code == insn_code)
	      {
		rtx x;
		x = evaluate_eq_attr (exp, av->value, insn_code, insn_index);
		x = SIMPLIFY_TEST_EXP (x, insn_code, insn_index);
		if (attr_rtx_cost(x) < 20)
		  return x;
	      }
      break;

    default:
      break;
    }

  /* We have already simplified this expression.  Simplifying it again
     won't buy anything unless we weren't given a valid insn code
     to process (i.e., we are canonicalizing something.).  */
  if (insn_code != -2
      && ! ATTR_IND_SIMPLIFIED_P (newexp))
    return copy_rtx_unchanging (newexp);

  return newexp;
}

/* Optimize the attribute lists by seeing if we can determine conditional
   values from the known values of other attributes.  This will save subroutine
   calls during the compilation.  */

static void
optimize_attrs (void)
{
  struct attr_desc *attr;
  struct attr_value *av;
  struct insn_ent *ie;
  rtx newexp;
  int i;
  struct attr_value_list
  {
    struct attr_value *av;
    struct insn_ent *ie;
    struct attr_desc *attr;
    struct attr_value_list *next;
  };
  struct attr_value_list **insn_code_values;
  struct attr_value_list *ivbuf;
  struct attr_value_list *iv;

  /* For each insn code, make a list of all the insn_ent's for it,
     for all values for all attributes.  */

  if (num_insn_ents == 0)
    return;

  /* Make 2 extra elements, for "code" values -2 and -1.  */
  insn_code_values = XCNEWVEC (struct attr_value_list *, insn_code_number + 2);

  /* Offset the table address so we can index by -2 or -1.  */
  insn_code_values += 2;

  iv = ivbuf = XNEWVEC (struct attr_value_list, num_insn_ents);

  for (i = 0; i < MAX_ATTRS_INDEX; i++)
    for (attr = attrs[i]; attr; attr = attr->next)
      for (av = attr->first_value; av; av = av->next)
	for (ie = av->first_insn; ie; ie = ie->next)
	  {
	    iv->attr = attr;
	    iv->av = av;
	    iv->ie = ie;
	    iv->next = insn_code_values[ie->def->insn_code];
	    insn_code_values[ie->def->insn_code] = iv;
	    iv++;
	  }

  /* Sanity check on num_insn_ents.  */
  gcc_assert (iv == ivbuf + num_insn_ents);

  /* Process one insn code at a time.  */
  for (i = -2; i < insn_code_number; i++)
    {
      /* Clear the ATTR_CURR_SIMPLIFIED_P flag everywhere relevant.
	 We use it to mean "already simplified for this insn".  */
      for (iv = insn_code_values[i]; iv; iv = iv->next)
	clear_struct_flag (iv->av->value);

      for (iv = insn_code_values[i]; iv; iv = iv->next)
	{
	  struct obstack *old = rtl_obstack;

	  attr = iv->attr;
	  av = iv->av;
	  ie = iv->ie;
	  if (GET_CODE (av->value) != COND)
	    continue;

	  rtl_obstack = temp_obstack;
	  newexp = av->value;
	  while (GET_CODE (newexp) == COND)
	    {
	      rtx newexp2 = simplify_cond (newexp, ie->def->insn_code,
					   ie->def->insn_index);
	      if (newexp2 == newexp)
		break;
	      newexp = newexp2;
	    }

	  rtl_obstack = old;
	  if (newexp != av->value)
	    {
	      newexp = attr_copy_rtx (newexp);
	      remove_insn_ent (av, ie);
	      av = get_attr_value (newexp, attr, ie->def->insn_code);
	      iv->av = av;
	      insert_insn_ent (av, ie);
	    }
	}
    }

  free (ivbuf);
  free (insn_code_values - 2);
}

/* Clear the ATTR_CURR_SIMPLIFIED_P flag in EXP and its subexpressions.  */

static void
clear_struct_flag (rtx x)
{
  int i;
  int j;
  enum rtx_code code;
  const char *fmt;

  ATTR_CURR_SIMPLIFIED_P (x) = 0;
  if (ATTR_IND_SIMPLIFIED_P (x))
    return;

  code = GET_CODE (x);

  switch (code)
    {
    case REG:
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case SYMBOL_REF:
    case CODE_LABEL:
    case PC:
    case CC0:
    case EQ_ATTR:
    case ATTR_FLAG:
      return;

    default:
      break;
    }

  /* Compare the elements.  If any pair of corresponding elements
     fail to match, return 0 for the whole things.  */

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      switch (fmt[i])
	{
	case 'V':
	case 'E':
	  for (j = 0; j < XVECLEN (x, i); j++)
	    clear_struct_flag (XVECEXP (x, i, j));
	  break;

	case 'e':
	  clear_struct_flag (XEXP (x, i));
	  break;
	}
    }
}

/* Create table entries for DEFINE_ATTR.  */

static void
gen_attr (rtx exp, int lineno)
{
  struct attr_desc *attr;
  struct attr_value *av;
  const char *name_ptr;
  char *p;

  /* Make a new attribute structure.  Check for duplicate by looking at
     attr->default_val, since it is initialized by this routine.  */
  attr = find_attr (&XSTR (exp, 0), 1);
  if (attr->default_val)
    {
      message_with_line (lineno, "duplicate definition for attribute %s",
			 attr->name);
      message_with_line (attr->lineno, "previous definition");
      have_error = 1;
      return;
    }
  attr->lineno = lineno;

  if (*XSTR (exp, 1) == '\0')
    attr->is_numeric = 1;
  else
    {
      name_ptr = XSTR (exp, 1);
      while ((p = next_comma_elt (&name_ptr)) != NULL)
	{
	  av = oballoc (sizeof (struct attr_value));
	  av->value = attr_rtx (CONST_STRING, p);
	  av->next = attr->first_value;
	  attr->first_value = av;
	  av->first_insn = NULL;
	  av->num_insns = 0;
	  av->has_asm_insn = 0;
	}
    }

  if (GET_CODE (XEXP (exp, 2)) == CONST)
    {
      attr->is_const = 1;
      if (attr->is_numeric)
	{
	  message_with_line (lineno,
			     "constant attributes may not take numeric values");
	  have_error = 1;
	}

      /* Get rid of the CONST node.  It is allowed only at top-level.  */
      XEXP (exp, 2) = XEXP (XEXP (exp, 2), 0);
    }

  if (! strcmp_check (attr->name, length_str) && ! attr->is_numeric)
    {
      message_with_line (lineno,
			 "`length' attribute must take numeric values");
      have_error = 1;
    }

  /* Set up the default value.  */
  XEXP (exp, 2) = check_attr_value (XEXP (exp, 2), attr);
  attr->default_val = get_attr_value (XEXP (exp, 2), attr, -2);
}

/* Given a pattern for DEFINE_PEEPHOLE or DEFINE_INSN, return the number of
   alternatives in the constraints.  Assume all MATCH_OPERANDs have the same
   number of alternatives as this should be checked elsewhere.  */

static int
count_alternatives (rtx exp)
{
  int i, j, n;
  const char *fmt;

  if (GET_CODE (exp) == MATCH_OPERAND)
    return n_comma_elts (XSTR (exp, 2));

  for (i = 0, fmt = GET_RTX_FORMAT (GET_CODE (exp));
       i < GET_RTX_LENGTH (GET_CODE (exp)); i++)
    switch (*fmt++)
      {
      case 'e':
      case 'u':
	n = count_alternatives (XEXP (exp, i));
	if (n)
	  return n;
	break;

      case 'E':
      case 'V':
	if (XVEC (exp, i) != NULL)
	  for (j = 0; j < XVECLEN (exp, i); j++)
	    {
	      n = count_alternatives (XVECEXP (exp, i, j));
	      if (n)
		return n;
	    }
      }

  return 0;
}

/* Returns nonzero if the given expression contains an EQ_ATTR with the
   `alternative' attribute.  */

static int
compares_alternatives_p (rtx exp)
{
  int i, j;
  const char *fmt;

  if (GET_CODE (exp) == EQ_ATTR && XSTR (exp, 0) == alternative_name)
    return 1;

  for (i = 0, fmt = GET_RTX_FORMAT (GET_CODE (exp));
       i < GET_RTX_LENGTH (GET_CODE (exp)); i++)
    switch (*fmt++)
      {
      case 'e':
      case 'u':
	if (compares_alternatives_p (XEXP (exp, i)))
	  return 1;
	break;

      case 'E':
	for (j = 0; j < XVECLEN (exp, i); j++)
	  if (compares_alternatives_p (XVECEXP (exp, i, j)))
	    return 1;
	break;
      }

  return 0;
}

/* Returns nonzero is INNER is contained in EXP.  */

static int
contained_in_p (rtx inner, rtx exp)
{
  int i, j;
  const char *fmt;

  if (rtx_equal_p (inner, exp))
    return 1;

  for (i = 0, fmt = GET_RTX_FORMAT (GET_CODE (exp));
       i < GET_RTX_LENGTH (GET_CODE (exp)); i++)
    switch (*fmt++)
      {
      case 'e':
      case 'u':
	if (contained_in_p (inner, XEXP (exp, i)))
	  return 1;
	break;

      case 'E':
	for (j = 0; j < XVECLEN (exp, i); j++)
	  if (contained_in_p (inner, XVECEXP (exp, i, j)))
	    return 1;
	break;
      }

  return 0;
}

/* Process DEFINE_PEEPHOLE, DEFINE_INSN, and DEFINE_ASM_ATTRIBUTES.  */

static void
gen_insn (rtx exp, int lineno)
{
  struct insn_def *id;

  id = oballoc (sizeof (struct insn_def));
  id->next = defs;
  defs = id;
  id->def = exp;
  id->lineno = lineno;

  switch (GET_CODE (exp))
    {
    case DEFINE_INSN:
      id->insn_code = insn_code_number;
      id->insn_index = insn_index_number;
      id->num_alternatives = count_alternatives (exp);
      if (id->num_alternatives == 0)
	id->num_alternatives = 1;
      id->vec_idx = 4;
      break;

    case DEFINE_PEEPHOLE:
      id->insn_code = insn_code_number;
      id->insn_index = insn_index_number;
      id->num_alternatives = count_alternatives (exp);
      if (id->num_alternatives == 0)
	id->num_alternatives = 1;
      id->vec_idx = 3;
      break;

    case DEFINE_ASM_ATTRIBUTES:
      id->insn_code = -1;
      id->insn_index = -1;
      id->num_alternatives = 1;
      id->vec_idx = 0;
      got_define_asm_attributes = 1;
      break;

    default:
      gcc_unreachable ();
    }
}

/* Process a DEFINE_DELAY.  Validate the vector length, check if annul
   true or annul false is specified, and make a `struct delay_desc'.  */

static void
gen_delay (rtx def, int lineno)
{
  struct delay_desc *delay;
  int i;

  if (XVECLEN (def, 1) % 3 != 0)
    {
      message_with_line (lineno,
			 "number of elements in DEFINE_DELAY must be multiple of three");
      have_error = 1;
      return;
    }

  for (i = 0; i < XVECLEN (def, 1); i += 3)
    {
      if (XVECEXP (def, 1, i + 1))
	have_annul_true = 1;
      if (XVECEXP (def, 1, i + 2))
	have_annul_false = 1;
    }

  delay = oballoc (sizeof (struct delay_desc));
  delay->def = def;
  delay->num = ++num_delays;
  delay->next = delays;
  delay->lineno = lineno;
  delays = delay;
}

/* Given a piece of RTX, print a C expression to test its truth value.
   We use AND and IOR both for logical and bit-wise operations, so
   interpret them as logical unless they are inside a comparison expression.
   The first bit of FLAGS will be nonzero in that case.

   Set the second bit of FLAGS to make references to attribute values use
   a cached local variable instead of calling a function.  */

static void
write_test_expr (rtx exp, int flags)
{
  int comparison_operator = 0;
  RTX_CODE code;
  struct attr_desc *attr;

  /* In order not to worry about operator precedence, surround our part of
     the expression with parentheses.  */

  printf ("(");
  code = GET_CODE (exp);
  switch (code)
    {
    /* Binary operators.  */
    case GEU: case GTU:
    case LEU: case LTU:
      printf ("(unsigned) ");
      /* Fall through.  */

    case EQ: case NE:
    case GE: case GT:
    case LE: case LT:
      comparison_operator = 1;

    case PLUS:   case MINUS:  case MULT:     case DIV:      case MOD:
    case AND:    case IOR:    case XOR:
    case ASHIFT: case LSHIFTRT: case ASHIFTRT:
      write_test_expr (XEXP (exp, 0), flags | comparison_operator);
      switch (code)
	{
	case EQ:
	  printf (" == ");
	  break;
	case NE:
	  printf (" != ");
	  break;
	case GE:
	  printf (" >= ");
	  break;
	case GT:
	  printf (" > ");
	  break;
	case GEU:
	  printf (" >= (unsigned) ");
	  break;
	case GTU:
	  printf (" > (unsigned) ");
	  break;
	case LE:
	  printf (" <= ");
	  break;
	case LT:
	  printf (" < ");
	  break;
	case LEU:
	  printf (" <= (unsigned) ");
	  break;
	case LTU:
	  printf (" < (unsigned) ");
	  break;
	case PLUS:
	  printf (" + ");
	  break;
	case MINUS:
	  printf (" - ");
	  break;
	case MULT:
	  printf (" * ");
	  break;
	case DIV:
	  printf (" / ");
	  break;
	case MOD:
	  printf (" %% ");
	  break;
	case AND:
	  if (flags & 1)
	    printf (" & ");
	  else
	    printf (" && ");
	  break;
	case IOR:
	  if (flags & 1)
	    printf (" | ");
	  else
	    printf (" || ");
	  break;
	case XOR:
	  printf (" ^ ");
	  break;
	case ASHIFT:
	  printf (" << ");
	  break;
	case LSHIFTRT:
	case ASHIFTRT:
	  printf (" >> ");
	  break;
	default:
	  gcc_unreachable ();
	}

      write_test_expr (XEXP (exp, 1), flags | comparison_operator);
      break;

    case NOT:
      /* Special-case (not (eq_attrq "alternative" "x")) */
      if (! (flags & 1) && GET_CODE (XEXP (exp, 0)) == EQ_ATTR
	  && XSTR (XEXP (exp, 0), 0) == alternative_name)
	{
	  printf ("which_alternative != %s", XSTR (XEXP (exp, 0), 1));
	  break;
	}

      /* Otherwise, fall through to normal unary operator.  */

    /* Unary operators.  */
    case ABS:  case NEG:
      switch (code)
	{
	case NOT:
	  if (flags & 1)
	    printf ("~ ");
	  else
	    printf ("! ");
	  break;
	case ABS:
	  printf ("abs ");
	  break;
	case NEG:
	  printf ("-");
	  break;
	default:
	  gcc_unreachable ();
	}

      write_test_expr (XEXP (exp, 0), flags);
      break;

    case EQ_ATTR_ALT:
	{
	  int set = XINT (exp, 0), bit = 0;

	  if (flags & 1)
	    fatal ("EQ_ATTR_ALT not valid inside comparison");

	  if (!set)
	    fatal ("Empty EQ_ATTR_ALT should be optimized out");

	  if (!(set & (set - 1)))
	    {
	      if (!(set & 0xffff))
		{
		  bit += 16;
		  set >>= 16;
		}
	      if (!(set & 0xff))
		{
		  bit += 8;
		  set >>= 8;
		}
	      if (!(set & 0xf))
		{
		  bit += 4;
		  set >>= 4;
		}
	      if (!(set & 0x3))
		{
		  bit += 2;
		  set >>= 2;
		}
	      if (!(set & 1))
		bit++;

	      printf ("which_alternative %s= %d",
		      XINT (exp, 1) ? "!" : "=", bit);
	    }
	  else
	    {
	      printf ("%s((1 << which_alternative) & 0x%x)",
		      XINT (exp, 1) ? "!" : "", set);
	    }
	}
      break;

    /* Comparison test of an attribute with a value.  Most of these will
       have been removed by optimization.   Handle "alternative"
       specially and give error if EQ_ATTR present inside a comparison.  */
    case EQ_ATTR:
      if (flags & 1)
	fatal ("EQ_ATTR not valid inside comparison");

      if (XSTR (exp, 0) == alternative_name)
	{
	  printf ("which_alternative == %s", XSTR (exp, 1));
	  break;
	}

      attr = find_attr (&XSTR (exp, 0), 0);
      gcc_assert (attr);

      /* Now is the time to expand the value of a constant attribute.  */
      if (attr->is_const)
	{
	  write_test_expr (evaluate_eq_attr (exp, attr->default_val->value,
					     -2, -2),
			   flags);
	}
      else
	{
	  if (flags & 2)
	    printf ("attr_%s", attr->name);
	  else
	    printf ("get_attr_%s (insn)", attr->name);
	  printf (" == ");
	  write_attr_valueq (attr, XSTR (exp, 1));
	}
      break;

    /* Comparison test of flags for define_delays.  */
    case ATTR_FLAG:
      if (flags & 1)
	fatal ("ATTR_FLAG not valid inside comparison");
      printf ("(flags & ATTR_FLAG_%s) != 0", XSTR (exp, 0));
      break;

    /* See if an operand matches a predicate.  */
    case MATCH_OPERAND:
      /* If only a mode is given, just ensure the mode matches the operand.
	 If neither a mode nor predicate is given, error.  */
      if (XSTR (exp, 1) == NULL || *XSTR (exp, 1) == '\0')
	{
	  if (GET_MODE (exp) == VOIDmode)
	    fatal ("null MATCH_OPERAND specified as test");
	  else
	    printf ("GET_MODE (operands[%d]) == %smode",
		    XINT (exp, 0), GET_MODE_NAME (GET_MODE (exp)));
	}
      else
	printf ("%s (operands[%d], %smode)",
		XSTR (exp, 1), XINT (exp, 0), GET_MODE_NAME (GET_MODE (exp)));
      break;

    /* Constant integer.  */
    case CONST_INT:
      printf (HOST_WIDE_INT_PRINT_DEC, XWINT (exp, 0));
      break;

    /* A random C expression.  */
    case SYMBOL_REF:
      print_c_condition (XSTR (exp, 0));
      break;

    /* The address of the branch target.  */
    case MATCH_DUP:
      printf ("INSN_ADDRESSES_SET_P () ? INSN_ADDRESSES (INSN_UID (GET_CODE (operands[%d]) == LABEL_REF ? XEXP (operands[%d], 0) : operands[%d])) : 0",
	      XINT (exp, 0), XINT (exp, 0), XINT (exp, 0));
      break;

    case PC:
      /* The address of the current insn.  We implement this actually as the
	 address of the current insn for backward branches, but the last
	 address of the next insn for forward branches, and both with
	 adjustments that account for the worst-case possible stretching of
	 intervening alignments between this insn and its destination.  */
      printf ("insn_current_reference_address (insn)");
      break;

    case CONST_STRING:
      printf ("%s", XSTR (exp, 0));
      break;

    case IF_THEN_ELSE:
      write_test_expr (XEXP (exp, 0), flags & 2);
      printf (" ? ");
      write_test_expr (XEXP (exp, 1), flags | 1);
      printf (" : ");
      write_test_expr (XEXP (exp, 2), flags | 1);
      break;

    default:
      fatal ("bad RTX code `%s' in attribute calculation\n",
	     GET_RTX_NAME (code));
    }

  printf (")");
}

/* Given an attribute value, return the maximum CONST_STRING argument
   encountered.  Set *UNKNOWNP and return INT_MAX if the value is unknown.  */

static int
max_attr_value (rtx exp, int *unknownp)
{
  int current_max;
  int i, n;

  switch (GET_CODE (exp))
    {
    case CONST_STRING:
      current_max = atoi (XSTR (exp, 0));
      break;

    case COND:
      current_max = max_attr_value (XEXP (exp, 1), unknownp);
      for (i = 0; i < XVECLEN (exp, 0); i += 2)
	{
	  n = max_attr_value (XVECEXP (exp, 0, i + 1), unknownp);
	  if (n > current_max)
	    current_max = n;
	}
      break;

    case IF_THEN_ELSE:
      current_max = max_attr_value (XEXP (exp, 1), unknownp);
      n = max_attr_value (XEXP (exp, 2), unknownp);
      if (n > current_max)
	current_max = n;
      break;

    default:
      *unknownp = 1;
      current_max = INT_MAX;
      break;
    }

  return current_max;
}

/* Given an attribute value, return the minimum CONST_STRING argument
   encountered.  Set *UNKNOWNP and return 0 if the value is unknown.  */

static int
min_attr_value (rtx exp, int *unknownp)
{
  int current_min;
  int i, n;

  switch (GET_CODE (exp))
    {
    case CONST_STRING:
      current_min = atoi (XSTR (exp, 0));
      break;

    case COND:
      current_min = min_attr_value (XEXP (exp, 1), unknownp);
      for (i = 0; i < XVECLEN (exp, 0); i += 2)
	{
	  n = min_attr_value (XVECEXP (exp, 0, i + 1), unknownp);
	  if (n < current_min)
	    current_min = n;
	}
      break;

    case IF_THEN_ELSE:
      current_min = min_attr_value (XEXP (exp, 1), unknownp);
      n = min_attr_value (XEXP (exp, 2), unknownp);
      if (n < current_min)
	current_min = n;
      break;

    default:
      *unknownp = 1;
      current_min = INT_MAX;
      break;
    }

  return current_min;
}

/* Given an attribute value, return the result of ORing together all
   CONST_STRING arguments encountered.  Set *UNKNOWNP and return -1
   if the numeric value is not known.  */

static int
or_attr_value (rtx exp, int *unknownp)
{
  int current_or;
  int i;

  switch (GET_CODE (exp))
    {
    case CONST_STRING:
      current_or = atoi (XSTR (exp, 0));
      break;

    case COND:
      current_or = or_attr_value (XEXP (exp, 1), unknownp);
      for (i = 0; i < XVECLEN (exp, 0); i += 2)
	current_or |= or_attr_value (XVECEXP (exp, 0, i + 1), unknownp);
      break;

    case IF_THEN_ELSE:
      current_or = or_attr_value (XEXP (exp, 1), unknownp);
      current_or |= or_attr_value (XEXP (exp, 2), unknownp);
      break;

    default:
      *unknownp = 1;
      current_or = -1;
      break;
    }

  return current_or;
}

/* Scan an attribute value, possibly a conditional, and record what actions
   will be required to do any conditional tests in it.

   Specifically, set
	`must_extract'	  if we need to extract the insn operands
	`must_constrain'  if we must compute `which_alternative'
	`address_used'	  if an address expression was used
	`length_used'	  if an (eq_attr "length" ...) was used
 */

static void
walk_attr_value (rtx exp)
{
  int i, j;
  const char *fmt;
  RTX_CODE code;

  if (exp == NULL)
    return;

  code = GET_CODE (exp);
  switch (code)
    {
    case SYMBOL_REF:
      if (! ATTR_IND_SIMPLIFIED_P (exp))
	/* Since this is an arbitrary expression, it can look at anything.
	   However, constant expressions do not depend on any particular
	   insn.  */
	must_extract = must_constrain = 1;
      return;

    case MATCH_OPERAND:
      must_extract = 1;
      return;

    case EQ_ATTR_ALT:
      must_extract = must_constrain = 1;
      break;

    case EQ_ATTR:
      if (XSTR (exp, 0) == alternative_name)
	must_extract = must_constrain = 1;
      else if (strcmp_check (XSTR (exp, 0), length_str) == 0)
	length_used = 1;
      return;

    case MATCH_DUP:
      must_extract = 1;
      address_used = 1;
      return;

    case PC:
      address_used = 1;
      return;

    case ATTR_FLAG:
      return;

    default:
      break;
    }

  for (i = 0, fmt = GET_RTX_FORMAT (code); i < GET_RTX_LENGTH (code); i++)
    switch (*fmt++)
      {
      case 'e':
      case 'u':
	walk_attr_value (XEXP (exp, i));
	break;

      case 'E':
	if (XVEC (exp, i) != NULL)
	  for (j = 0; j < XVECLEN (exp, i); j++)
	    walk_attr_value (XVECEXP (exp, i, j));
	break;
      }
}

/* Write out a function to obtain the attribute for a given INSN.  */

static void
write_attr_get (struct attr_desc *attr)
{
  struct attr_value *av, *common_av;

  /* Find the most used attribute value.  Handle that as the `default' of the
     switch we will generate.  */
  common_av = find_most_used (attr);

  /* Write out start of function, then all values with explicit `case' lines,
     then a `default', then the value with the most uses.  */
  if (!attr->is_numeric)
    printf ("enum attr_%s\n", attr->name);
  else
    printf ("int\n");

  /* If the attribute name starts with a star, the remainder is the name of
     the subroutine to use, instead of `get_attr_...'.  */
  if (attr->name[0] == '*')
    printf ("%s (rtx insn ATTRIBUTE_UNUSED)\n", &attr->name[1]);
  else if (attr->is_const == 0)
    printf ("get_attr_%s (rtx insn ATTRIBUTE_UNUSED)\n", attr->name);
  else
    {
      printf ("get_attr_%s (void)\n", attr->name);
      printf ("{\n");

      for (av = attr->first_value; av; av = av->next)
	if (av->num_insns == 1)
	  write_attr_set (attr, 2, av->value, "return", ";",
			  true_rtx, av->first_insn->def->insn_code,
			  av->first_insn->def->insn_index);
	else if (av->num_insns != 0)
	  write_attr_set (attr, 2, av->value, "return", ";",
			  true_rtx, -2, 0);

      printf ("}\n\n");
      return;
    }

  printf ("{\n");
  printf ("  switch (recog_memoized (insn))\n");
  printf ("    {\n");

  for (av = attr->first_value; av; av = av->next)
    if (av != common_av)
      write_attr_case (attr, av, 1, "return", ";", 4, true_rtx);

  write_attr_case (attr, common_av, 0, "return", ";", 4, true_rtx);
  printf ("    }\n}\n\n");
}

/* Given an AND tree of known true terms (because we are inside an `if' with
   that as the condition or are in an `else' clause) and an expression,
   replace any known true terms with TRUE.  Use `simplify_and_tree' to do
   the bulk of the work.  */

static rtx
eliminate_known_true (rtx known_true, rtx exp, int insn_code, int insn_index)
{
  rtx term;

  known_true = SIMPLIFY_TEST_EXP (known_true, insn_code, insn_index);

  if (GET_CODE (known_true) == AND)
    {
      exp = eliminate_known_true (XEXP (known_true, 0), exp,
				  insn_code, insn_index);
      exp = eliminate_known_true (XEXP (known_true, 1), exp,
				  insn_code, insn_index);
    }
  else
    {
      term = known_true;
      exp = simplify_and_tree (exp, &term, insn_code, insn_index);
    }

  return exp;
}

/* Write out a series of tests and assignment statements to perform tests and
   sets of an attribute value.  We are passed an indentation amount and prefix
   and suffix strings to write around each attribute value (e.g., "return"
   and ";").  */

static void
write_attr_set (struct attr_desc *attr, int indent, rtx value,
		const char *prefix, const char *suffix, rtx known_true,
		int insn_code, int insn_index)
{
  if (GET_CODE (value) == COND)
    {
      /* Assume the default value will be the default of the COND unless we
	 find an always true expression.  */
      rtx default_val = XEXP (value, 1);
      rtx our_known_true = known_true;
      rtx newexp;
      int first_if = 1;
      int i;

      for (i = 0; i < XVECLEN (value, 0); i += 2)
	{
	  rtx testexp;
	  rtx inner_true;

	  testexp = eliminate_known_true (our_known_true,
					  XVECEXP (value, 0, i),
					  insn_code, insn_index);
	  newexp = attr_rtx (NOT, testexp);
	  newexp = insert_right_side (AND, our_known_true, newexp,
				      insn_code, insn_index);

	  /* If the test expression is always true or if the next `known_true'
	     expression is always false, this is the last case, so break
	     out and let this value be the `else' case.  */
	  if (testexp == true_rtx || newexp == false_rtx)
	    {
	      default_val = XVECEXP (value, 0, i + 1);
	      break;
	    }

	  /* Compute the expression to pass to our recursive call as being
	     known true.  */
	  inner_true = insert_right_side (AND, our_known_true,
					  testexp, insn_code, insn_index);

	  /* If this is always false, skip it.  */
	  if (inner_true == false_rtx)
	    continue;

	  write_indent (indent);
	  printf ("%sif ", first_if ? "" : "else ");
	  first_if = 0;
	  write_test_expr (testexp, 0);
	  printf ("\n");
	  write_indent (indent + 2);
	  printf ("{\n");

	  write_attr_set (attr, indent + 4,
			  XVECEXP (value, 0, i + 1), prefix, suffix,
			  inner_true, insn_code, insn_index);
	  write_indent (indent + 2);
	  printf ("}\n");
	  our_known_true = newexp;
	}

      if (! first_if)
	{
	  write_indent (indent);
	  printf ("else\n");
	  write_indent (indent + 2);
	  printf ("{\n");
	}

      write_attr_set (attr, first_if ? indent : indent + 4, default_val,
		      prefix, suffix, our_known_true, insn_code, insn_index);

      if (! first_if)
	{
	  write_indent (indent + 2);
	  printf ("}\n");
	}
    }
  else
    {
      write_indent (indent);
      printf ("%s ", prefix);
      write_attr_value (attr, value);
      printf ("%s\n", suffix);
    }
}

/* Write a series of case statements for every instruction in list IE.
   INDENT is the amount of indentation to write before each case.  */

static void
write_insn_cases (struct insn_ent *ie, int indent)
{
  for (; ie != 0; ie = ie->next)
    if (ie->def->insn_code != -1)
      {
	write_indent (indent);
	if (GET_CODE (ie->def->def) == DEFINE_PEEPHOLE)
	  printf ("case %d:  /* define_peephole, line %d */\n",
		  ie->def->insn_code, ie->def->lineno);
	else
	  printf ("case %d:  /* %s */\n",
		  ie->def->insn_code, XSTR (ie->def->def, 0));
      }
}

/* Write out the computation for one attribute value.  */

static void
write_attr_case (struct attr_desc *attr, struct attr_value *av,
		 int write_case_lines, const char *prefix, const char *suffix,
		 int indent, rtx known_true)
{
  if (av->num_insns == 0)
    return;

  if (av->has_asm_insn)
    {
      write_indent (indent);
      printf ("case -1:\n");
      write_indent (indent + 2);
      printf ("if (GET_CODE (PATTERN (insn)) != ASM_INPUT\n");
      write_indent (indent + 2);
      printf ("    && asm_noperands (PATTERN (insn)) < 0)\n");
      write_indent (indent + 2);
      printf ("  fatal_insn_not_found (insn);\n");
    }

  if (write_case_lines)
    write_insn_cases (av->first_insn, indent);
  else
    {
      write_indent (indent);
      printf ("default:\n");
    }

  /* See what we have to do to output this value.  */
  must_extract = must_constrain = address_used = 0;
  walk_attr_value (av->value);

  if (must_constrain)
    {
      write_indent (indent + 2);
      printf ("extract_constrain_insn_cached (insn);\n");
    }
  else if (must_extract)
    {
      write_indent (indent + 2);
      printf ("extract_insn_cached (insn);\n");
    }

  if (av->num_insns == 1)
    write_attr_set (attr, indent + 2, av->value, prefix, suffix,
		    known_true, av->first_insn->def->insn_code,
		    av->first_insn->def->insn_index);
  else
    write_attr_set (attr, indent + 2, av->value, prefix, suffix,
		    known_true, -2, 0);

  if (strncmp (prefix, "return", 6))
    {
      write_indent (indent + 2);
      printf ("break;\n");
    }
  printf ("\n");
}

/* Search for uses of non-const attributes and write code to cache them.  */

static int
write_expr_attr_cache (rtx p, struct attr_desc *attr)
{
  const char *fmt;
  int i, ie, j, je;

  if (GET_CODE (p) == EQ_ATTR)
    {
      if (XSTR (p, 0) != attr->name)
	return 0;

      if (!attr->is_numeric)
	printf ("  enum attr_%s ", attr->name);
      else
	printf ("  int ");

      printf ("attr_%s = get_attr_%s (insn);\n", attr->name, attr->name);
      return 1;
    }

  fmt = GET_RTX_FORMAT (GET_CODE (p));
  ie = GET_RTX_LENGTH (GET_CODE (p));
  for (i = 0; i < ie; i++)
    {
      switch (*fmt++)
	{
	case 'e':
	  if (write_expr_attr_cache (XEXP (p, i), attr))
	    return 1;
	  break;

	case 'E':
	  je = XVECLEN (p, i);
	  for (j = 0; j < je; ++j)
	    if (write_expr_attr_cache (XVECEXP (p, i, j), attr))
	      return 1;
	  break;
	}
    }

  return 0;
}

/* Utilities to write in various forms.  */

static void
write_attr_valueq (struct attr_desc *attr, const char *s)
{
  if (attr->is_numeric)
    {
      int num = atoi (s);

      printf ("%d", num);

      if (num > 9 || num < 0)
	printf (" /* 0x%x */", num);
    }
  else
    {
      write_upcase (attr->name);
      printf ("_");
      write_upcase (s);
    }
}

static void
write_attr_value (struct attr_desc *attr, rtx value)
{
  int op;

  switch (GET_CODE (value))
    {
    case CONST_STRING:
      write_attr_valueq (attr, XSTR (value, 0));
      break;

    case CONST_INT:
      printf (HOST_WIDE_INT_PRINT_DEC, INTVAL (value));
      break;

    case SYMBOL_REF:
      print_c_condition (XSTR (value, 0));
      break;

    case ATTR:
      {
	struct attr_desc *attr2 = find_attr (&XSTR (value, 0), 0);
	printf ("get_attr_%s (%s)", attr2->name,
		(attr2->is_const ? "" : "insn"));
      }
      break;

    case PLUS:
      op = '+';
      goto do_operator;
    case MINUS:
      op = '-';
      goto do_operator;
    case MULT:
      op = '*';
      goto do_operator;
    case DIV:
      op = '/';
      goto do_operator;
    case MOD:
      op = '%';
      goto do_operator;

    do_operator:
      write_attr_value (attr, XEXP (value, 0));
      putchar (' ');
      putchar (op);
      putchar (' ');
      write_attr_value (attr, XEXP (value, 1));
      break;

    default:
      gcc_unreachable ();
    }
}

static void
write_upcase (const char *str)
{
  while (*str)
    {
      /* The argument of TOUPPER should not have side effects.  */
      putchar (TOUPPER(*str));
      str++;
    }
}

static void
write_indent (int indent)
{
  for (; indent > 8; indent -= 8)
    printf ("\t");

  for (; indent; indent--)
    printf (" ");
}

/* Write a subroutine that is given an insn that requires a delay slot, a
   delay slot ordinal, and a candidate insn.  It returns nonzero if the
   candidate can be placed in the specified delay slot of the insn.

   We can write as many as three subroutines.  `eligible_for_delay'
   handles normal delay slots, `eligible_for_annul_true' indicates that
   the specified insn can be annulled if the branch is true, and likewise
   for `eligible_for_annul_false'.

   KIND is a string distinguishing these three cases ("delay", "annul_true",
   or "annul_false").  */

static void
write_eligible_delay (const char *kind)
{
  struct delay_desc *delay;
  int max_slots;
  char str[50];
  const char *pstr;
  struct attr_desc *attr;
  struct attr_value *av, *common_av;
  int i;

  /* Compute the maximum number of delay slots required.  We use the delay
     ordinal times this number plus one, plus the slot number as an index into
     the appropriate predicate to test.  */

  for (delay = delays, max_slots = 0; delay; delay = delay->next)
    if (XVECLEN (delay->def, 1) / 3 > max_slots)
      max_slots = XVECLEN (delay->def, 1) / 3;

  /* Write function prelude.  */

  printf ("int\n");
  printf ("eligible_for_%s (rtx delay_insn ATTRIBUTE_UNUSED, int slot, rtx candidate_insn, int flags ATTRIBUTE_UNUSED)\n",
	  kind);
  printf ("{\n");
  printf ("  rtx insn;\n");
  printf ("\n");
  printf ("  gcc_assert (slot < %d);\n", max_slots);
  printf ("\n");
  /* Allow dbr_schedule to pass labels, etc.  This can happen if try_split
     converts a compound instruction into a loop.  */
  printf ("  if (!INSN_P (candidate_insn))\n");
  printf ("    return 0;\n");
  printf ("\n");

  /* If more than one delay type, find out which type the delay insn is.  */

  if (num_delays > 1)
    {
      attr = find_attr (&delay_type_str, 0);
      gcc_assert (attr);
      common_av = find_most_used (attr);

      printf ("  insn = delay_insn;\n");
      printf ("  switch (recog_memoized (insn))\n");
      printf ("    {\n");

      sprintf (str, " * %d;\n      break;", max_slots);
      for (av = attr->first_value; av; av = av->next)
	if (av != common_av)
	  write_attr_case (attr, av, 1, "slot +=", str, 4, true_rtx);

      write_attr_case (attr, common_av, 0, "slot +=", str, 4, true_rtx);
      printf ("    }\n\n");

      /* Ensure matched.  Otherwise, shouldn't have been called.  */
      printf ("  gcc_assert (slot >= %d);\n\n", max_slots);
    }

  /* If just one type of delay slot, write simple switch.  */
  if (num_delays == 1 && max_slots == 1)
    {
      printf ("  insn = candidate_insn;\n");
      printf ("  switch (recog_memoized (insn))\n");
      printf ("    {\n");

      attr = find_attr (&delay_1_0_str, 0);
      gcc_assert (attr);
      common_av = find_most_used (attr);

      for (av = attr->first_value; av; av = av->next)
	if (av != common_av)
	  write_attr_case (attr, av, 1, "return", ";", 4, true_rtx);

      write_attr_case (attr, common_av, 0, "return", ";", 4, true_rtx);
      printf ("    }\n");
    }

  else
    {
      /* Write a nested CASE.  The first indicates which condition we need to
	 test, and the inner CASE tests the condition.  */
      printf ("  insn = candidate_insn;\n");
      printf ("  switch (slot)\n");
      printf ("    {\n");

      for (delay = delays; delay; delay = delay->next)
	for (i = 0; i < XVECLEN (delay->def, 1); i += 3)
	  {
	    printf ("    case %d:\n",
		    (i / 3) + (num_delays == 1 ? 0 : delay->num * max_slots));
	    printf ("      switch (recog_memoized (insn))\n");
	    printf ("\t{\n");

	    sprintf (str, "*%s_%d_%d", kind, delay->num, i / 3);
	    pstr = str;
	    attr = find_attr (&pstr, 0);
	    gcc_assert (attr);
	    common_av = find_most_used (attr);

	    for (av = attr->first_value; av; av = av->next)
	      if (av != common_av)
		write_attr_case (attr, av, 1, "return", ";", 8, true_rtx);

	    write_attr_case (attr, common_av, 0, "return", ";", 8, true_rtx);
	    printf ("      }\n");
	  }

      printf ("    default:\n");
      printf ("      gcc_unreachable ();\n");
      printf ("    }\n");
    }

  printf ("}\n\n");
}

/* This page contains miscellaneous utility routines.  */

/* Given a pointer to a (char *), return a malloc'ed string containing the
   next comma-separated element.  Advance the pointer to after the string
   scanned, or the end-of-string.  Return NULL if at end of string.  */

static char *
next_comma_elt (const char **pstr)
{
  const char *start;

  start = scan_comma_elt (pstr);

  if (start == NULL)
    return NULL;

  return attr_string (start, *pstr - start);
}

/* Return a `struct attr_desc' pointer for a given named attribute.  If CREATE
   is nonzero, build a new attribute, if one does not exist.  *NAME_P is
   replaced by a pointer to a canonical copy of the string.  */

static struct attr_desc *
find_attr (const char **name_p, int create)
{
  struct attr_desc *attr;
  int index;
  const char *name = *name_p;

  /* Before we resort to using `strcmp', see if the string address matches
     anywhere.  In most cases, it should have been canonicalized to do so.  */
  if (name == alternative_name)
    return NULL;

  index = name[0] & (MAX_ATTRS_INDEX - 1);
  for (attr = attrs[index]; attr; attr = attr->next)
    if (name == attr->name)
      return attr;

  /* Otherwise, do it the slow way.  */
  for (attr = attrs[index]; attr; attr = attr->next)
    if (name[0] == attr->name[0] && ! strcmp (name, attr->name))
      {
	*name_p = attr->name;
	return attr;
      }

  if (! create)
    return NULL;

  attr = oballoc (sizeof (struct attr_desc));
  attr->name = DEF_ATTR_STRING (name);
  attr->first_value = attr->default_val = NULL;
  attr->is_numeric = attr->is_const = attr->is_special = 0;
  attr->next = attrs[index];
  attrs[index] = attr;

  *name_p = attr->name;

  return attr;
}

/* Create internal attribute with the given default value.  */

static void
make_internal_attr (const char *name, rtx value, int special)
{
  struct attr_desc *attr;

  attr = find_attr (&name, 1);
  gcc_assert (!attr->default_val);

  attr->is_numeric = 1;
  attr->is_const = 0;
  attr->is_special = (special & ATTR_SPECIAL) != 0;
  attr->default_val = get_attr_value (value, attr, -2);
}

/* Find the most used value of an attribute.  */

static struct attr_value *
find_most_used (struct attr_desc *attr)
{
  struct attr_value *av;
  struct attr_value *most_used;
  int nuses;

  most_used = NULL;
  nuses = -1;

  for (av = attr->first_value; av; av = av->next)
    if (av->num_insns > nuses)
      nuses = av->num_insns, most_used = av;

  return most_used;
}

/* Return (attr_value "n") */

static rtx
make_numeric_value (int n)
{
  static rtx int_values[20];
  rtx exp;
  char *p;

  gcc_assert (n >= 0);

  if (n < 20 && int_values[n])
    return int_values[n];

  p = attr_printf (MAX_DIGITS, "%d", n);
  exp = attr_rtx (CONST_STRING, p);

  if (n < 20)
    int_values[n] = exp;

  return exp;
}

static rtx
copy_rtx_unchanging (rtx orig)
{
  if (ATTR_IND_SIMPLIFIED_P (orig) || ATTR_CURR_SIMPLIFIED_P (orig))
    return orig;

  ATTR_CURR_SIMPLIFIED_P (orig) = 1;
  return orig;
}

/* Determine if an insn has a constant number of delay slots, i.e., the
   number of delay slots is not a function of the length of the insn.  */

static void
write_const_num_delay_slots (void)
{
  struct attr_desc *attr = find_attr (&num_delay_slots_str, 0);
  struct attr_value *av;

  if (attr)
    {
      printf ("int\nconst_num_delay_slots (rtx insn)\n");
      printf ("{\n");
      printf ("  switch (recog_memoized (insn))\n");
      printf ("    {\n");

      for (av = attr->first_value; av; av = av->next)
	{
	  length_used = 0;
	  walk_attr_value (av->value);
	  if (length_used)
	    write_insn_cases (av->first_insn, 4);
	}

      printf ("    default:\n");
      printf ("      return 1;\n");
      printf ("    }\n}\n\n");
    }
}

/* Synthetic attributes used by insn-automata.c and the scheduler.
   These are primarily concerned with (define_insn_reservation)
   patterns.  */

struct insn_reserv
{
  struct insn_reserv *next;

  const char *name;
  int default_latency;
  rtx condexp;

  /* Sequence number of this insn.  */
  int insn_num;

  /* Whether a (define_bypass) construct names this insn in its
     output list.  */
  bool bypassed;
};

static struct insn_reserv *all_insn_reservs = 0;
static struct insn_reserv **last_insn_reserv_p = &all_insn_reservs;
static size_t n_insn_reservs;

/* Store information from a DEFINE_INSN_RESERVATION for future
   attribute generation.  */
static void
gen_insn_reserv (rtx def)
{
  struct insn_reserv *decl = oballoc (sizeof (struct insn_reserv));

  decl->name            = DEF_ATTR_STRING (XSTR (def, 0));
  decl->default_latency = XINT (def, 1);
  decl->condexp         = check_attr_test (XEXP (def, 2), 0, 0);
  decl->insn_num        = n_insn_reservs;
  decl->bypassed	= false;
  decl->next            = 0;
  
  *last_insn_reserv_p = decl;
  last_insn_reserv_p  = &decl->next;
  n_insn_reservs++;
}

/* Store information from a DEFINE_BYPASS for future attribute
   generation.  The only thing we care about is the list of output
   insns, which will later be used to tag reservation structures with
   a 'bypassed' bit.  */

struct bypass_list
{
  struct bypass_list *next;
  const char *insn;
};

static struct bypass_list *all_bypasses;
static size_t n_bypasses;

static void
gen_bypass_1 (const char *s, size_t len)
{
  struct bypass_list *b;

  if (len == 0)
    return;

  s = attr_string (s, len);
  for (b = all_bypasses; b; b = b->next)
    if (s == b->insn)
      return;  /* already got that one */

  b = oballoc (sizeof (struct bypass_list));
  b->insn = s;
  b->next = all_bypasses;
  all_bypasses = b;
  n_bypasses++;
}

static void
gen_bypass (rtx def)
{
  const char *p, *base;

  for (p = base = XSTR (def, 1); *p; p++)
    if (*p == ',')
      {
	gen_bypass_1 (base, p - base);
	do
	  p++;
	while (ISSPACE (*p));
	base = p;
      }
  gen_bypass_1 (base, p - base);
}

/* Find and mark all of the bypassed insns.  */
static void
process_bypasses (void)
{
  struct bypass_list *b;
  struct insn_reserv *r;

  /* The reservation list is likely to be much longer than the bypass
     list.  */
  for (r = all_insn_reservs; r; r = r->next)
    for (b = all_bypasses; b; b = b->next)
      if (r->name == b->insn)
	r->bypassed = true;
}

/* Create all of the attributes that describe automaton properties.  */
static void
make_automaton_attrs (void)
{
  int i;
  struct insn_reserv *decl;
  rtx code_exp, lats_exp, byps_exp;

  if (n_insn_reservs == 0)
    return;

  code_exp = rtx_alloc (COND);
  lats_exp = rtx_alloc (COND);
  
  XVEC (code_exp, 0) = rtvec_alloc (n_insn_reservs * 2);
  XVEC (lats_exp, 0) = rtvec_alloc (n_insn_reservs * 2);

  XEXP (code_exp, 1) = make_numeric_value (n_insn_reservs + 1);
  XEXP (lats_exp, 1) = make_numeric_value (0);

  for (decl = all_insn_reservs, i = 0;
       decl;
       decl = decl->next, i += 2)
    {
      XVECEXP (code_exp, 0, i)   = decl->condexp;
      XVECEXP (lats_exp, 0, i)   = decl->condexp;
      
      XVECEXP (code_exp, 0, i+1) = make_numeric_value (decl->insn_num);
      XVECEXP (lats_exp, 0, i+1) = make_numeric_value (decl->default_latency);
    }

  if (n_bypasses == 0)
    byps_exp = make_numeric_value (0);
  else
    {
      process_bypasses ();

      byps_exp = rtx_alloc (COND);
      XVEC (byps_exp, 0) = rtvec_alloc (n_bypasses * 2);
      XEXP (byps_exp, 1) = make_numeric_value (0);
      for (decl = all_insn_reservs, i = 0;
	   decl;
	   decl = decl->next)
	if (decl->bypassed)
	  {
	    XVECEXP (byps_exp, 0, i)   = decl->condexp;
	    XVECEXP (byps_exp, 0, i+1) = make_numeric_value (1);
	    i += 2;
	  }
    }

  make_internal_attr ("*internal_dfa_insn_code", code_exp, ATTR_NONE);
  make_internal_attr ("*insn_default_latency",   lats_exp, ATTR_NONE);
  make_internal_attr ("*bypass_p",               byps_exp, ATTR_NONE);
}

int
main (int argc, char **argv)
{
  rtx desc;
  struct attr_desc *attr;
  struct insn_def *id;
  rtx tem;
  int i;

  progname = "genattrtab";

  if (init_md_reader_args (argc, argv) != SUCCESS_EXIT_CODE)
    return (FATAL_EXIT_CODE);

  obstack_init (hash_obstack);
  obstack_init (temp_obstack);

  /* Set up true and false rtx's */
  true_rtx = rtx_alloc (CONST_INT);
  XWINT (true_rtx, 0) = 1;
  false_rtx = rtx_alloc (CONST_INT);
  XWINT (false_rtx, 0) = 0;
  ATTR_IND_SIMPLIFIED_P (true_rtx) = ATTR_IND_SIMPLIFIED_P (false_rtx) = 1;
  ATTR_PERMANENT_P (true_rtx) = ATTR_PERMANENT_P (false_rtx) = 1;

  alternative_name = DEF_ATTR_STRING ("alternative");
  length_str = DEF_ATTR_STRING ("length");
  delay_type_str = DEF_ATTR_STRING ("*delay_type");
  delay_1_0_str = DEF_ATTR_STRING ("*delay_1_0");
  num_delay_slots_str = DEF_ATTR_STRING ("*num_delay_slots");

  printf ("/* Generated automatically by the program `genattrtab'\n\
from the machine description file `md'.  */\n\n");

  /* Read the machine description.  */

  while (1)
    {
      int lineno;

      desc = read_md_rtx (&lineno, &insn_code_number);
      if (desc == NULL)
	break;

      switch (GET_CODE (desc))
	{
	case DEFINE_INSN:
	case DEFINE_PEEPHOLE:
	case DEFINE_ASM_ATTRIBUTES:
	  gen_insn (desc, lineno);
	  break;

	case DEFINE_ATTR:
	  gen_attr (desc, lineno);
	  break;

	case DEFINE_DELAY:
	  gen_delay (desc, lineno);
	  break;

	case DEFINE_INSN_RESERVATION:
	  gen_insn_reserv (desc);
	  break;

	case DEFINE_BYPASS:
	  gen_bypass (desc);
	  break;

	default:
	  break;
	}
      if (GET_CODE (desc) != DEFINE_ASM_ATTRIBUTES)
	insn_index_number++;
    }

  if (have_error)
    return FATAL_EXIT_CODE;

  insn_code_number++;

  /* If we didn't have a DEFINE_ASM_ATTRIBUTES, make a null one.  */
  if (! got_define_asm_attributes)
    {
      tem = rtx_alloc (DEFINE_ASM_ATTRIBUTES);
      XVEC (tem, 0) = rtvec_alloc (0);
      gen_insn (tem, 0);
    }

  /* Expand DEFINE_DELAY information into new attribute.  */
  if (num_delays)
    expand_delays ();

  printf ("#include \"config.h\"\n");
  printf ("#include \"system.h\"\n");
  printf ("#include \"coretypes.h\"\n");
  printf ("#include \"tm.h\"\n");
  printf ("#include \"rtl.h\"\n");
  printf ("#include \"tm_p.h\"\n");
  printf ("#include \"insn-config.h\"\n");
  printf ("#include \"recog.h\"\n");
  printf ("#include \"regs.h\"\n");
  printf ("#include \"real.h\"\n");
  printf ("#include \"output.h\"\n");
  printf ("#include \"insn-attr.h\"\n");
  printf ("#include \"toplev.h\"\n");
  printf ("#include \"flags.h\"\n");
  printf ("#include \"function.h\"\n");
  printf ("\n");
  printf ("#define operands recog_data.operand\n\n");

  /* Make `insn_alternatives'.  */
  insn_alternatives = oballoc (insn_code_number * sizeof (int));
  for (id = defs; id; id = id->next)
    if (id->insn_code >= 0)
      insn_alternatives[id->insn_code] = (1 << id->num_alternatives) - 1;

  /* Make `insn_n_alternatives'.  */
  insn_n_alternatives = oballoc (insn_code_number * sizeof (int));
  for (id = defs; id; id = id->next)
    if (id->insn_code >= 0)
      insn_n_alternatives[id->insn_code] = id->num_alternatives;

  /* Construct extra attributes for automata.  */
  make_automaton_attrs ();

  /* Prepare to write out attribute subroutines by checking everything stored
     away and building the attribute cases.  */

  check_defs ();

  for (i = 0; i < MAX_ATTRS_INDEX; i++)
    for (attr = attrs[i]; attr; attr = attr->next)
      attr->default_val->value
	= check_attr_value (attr->default_val->value, attr);

  if (have_error)
    return FATAL_EXIT_CODE;

  for (i = 0; i < MAX_ATTRS_INDEX; i++)
    for (attr = attrs[i]; attr; attr = attr->next)
      fill_attr (attr);

  /* Construct extra attributes for `length'.  */
  make_length_attrs ();

  /* Perform any possible optimizations to speed up compilation.  */
  optimize_attrs ();

  /* Now write out all the `gen_attr_...' routines.  Do these before the
     special routines so that they get defined before they are used.  */

  for (i = 0; i < MAX_ATTRS_INDEX; i++)
    for (attr = attrs[i]; attr; attr = attr->next)
      {
	if (! attr->is_special && ! attr->is_const)
	  write_attr_get (attr);
      }

  /* Write out delay eligibility information, if DEFINE_DELAY present.
     (The function to compute the number of delay slots will be written
     below.)  */
  if (num_delays)
    {
      write_eligible_delay ("delay");
      if (have_annul_true)
	write_eligible_delay ("annul_true");
      if (have_annul_false)
	write_eligible_delay ("annul_false");
    }

  /* Write out constant delay slot info.  */
  write_const_num_delay_slots ();

  write_length_unit_log ();

  fflush (stdout);
  return (ferror (stdout) != 0 ? FATAL_EXIT_CODE : SUCCESS_EXIT_CODE);
}
