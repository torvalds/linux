/* Expression parsing for plural form selection.
   Copyright (C) 2000, 2001 Free Software Foundation, Inc.
   Written by Ulrich Drepper <drepper@cygnus.com>, 2000.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU Library General Public License as published
   by the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301,
   USA.  */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "plural-exp.h"

#if (defined __GNUC__ && !defined __APPLE_CC__) \
    || (defined __STDC_VERSION__ && __STDC_VERSION__ >= 199901L)

/* These structs are the constant expression for the germanic plural
   form determination.  It represents the expression  "n != 1".  */
static const struct expression plvar =
{
  .nargs = 0,
  .operation = var,
};
static const struct expression plone =
{
  .nargs = 0,
  .operation = num,
  .val =
  {
    .num = 1
  }
};
struct expression GERMANIC_PLURAL =
{
  .nargs = 2,
  .operation = not_equal,
  .val =
  {
    .args =
    {
      [0] = (struct expression *) &plvar,
      [1] = (struct expression *) &plone
    }
  }
};

# define INIT_GERMANIC_PLURAL()

#else

/* For compilers without support for ISO C 99 struct/union initializers:
   Initialization at run-time.  */

static struct expression plvar;
static struct expression plone;
struct expression GERMANIC_PLURAL;

static void
init_germanic_plural ()
{
  if (plone.val.num == 0)
    {
      plvar.nargs = 0;
      plvar.operation = var;

      plone.nargs = 0;
      plone.operation = num;
      plone.val.num = 1;

      GERMANIC_PLURAL.nargs = 2;
      GERMANIC_PLURAL.operation = not_equal;
      GERMANIC_PLURAL.val.args[0] = &plvar;
      GERMANIC_PLURAL.val.args[1] = &plone;
    }
}

# define INIT_GERMANIC_PLURAL() init_germanic_plural ()

#endif

void
internal_function
EXTRACT_PLURAL_EXPRESSION (nullentry, pluralp, npluralsp)
     const char *nullentry;
     struct expression **pluralp;
     unsigned long int *npluralsp;
{
  if (nullentry != NULL)
    {
      const char *plural;
      const char *nplurals;

      plural = strstr (nullentry, "plural=");
      nplurals = strstr (nullentry, "nplurals=");
      if (plural == NULL || nplurals == NULL)
	goto no_plural;
      else
	{
	  char *endp;
	  unsigned long int n;
	  struct parse_args args;

	  /* First get the number.  */
	  nplurals += 9;
	  while (*nplurals != '\0' && isspace ((unsigned char) *nplurals))
	    ++nplurals;
	  if (!(*nplurals >= '0' && *nplurals <= '9'))
	    goto no_plural;
#if defined HAVE_STRTOUL || defined _LIBC
	  n = strtoul (nplurals, &endp, 10);
#else
	  for (endp = nplurals, n = 0; *endp >= '0' && *endp <= '9'; endp++)
	    n = n * 10 + (*endp - '0');
#endif
	  if (nplurals == endp)
	    goto no_plural;
	  *npluralsp = n;

	  /* Due to the restrictions bison imposes onto the interface of the
	     scanner function we have to put the input string and the result
	     passed up from the parser into the same structure which address
	     is passed down to the parser.  */
	  plural += 7;
	  args.cp = plural;
	  if (PLURAL_PARSE (&args) != 0)
	    goto no_plural;
	  *pluralp = args.res;
	}
    }
  else
    {
      /* By default we are using the Germanic form: singular form only
         for `one', the plural form otherwise.  Yes, this is also what
         English is using since English is a Germanic language.  */
    no_plural:
      INIT_GERMANIC_PLURAL ();
      *pluralp = &GERMANIC_PLURAL;
      *npluralsp = 2;
    }
}
