/* Like vsprintf but provides a pointer to malloc'd storage, which must
   be freed by the caller.
   Copyright (C) 1994, 2003 Free Software Foundation, Inc.

This file is part of the libiberty library.
Libiberty is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

Libiberty is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with libiberty; see the file COPYING.LIB.  If
not, write to the Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
Boston, MA 02110-1301, USA.  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <ansidecl.h>
#include <stdarg.h>
#if !defined (va_copy) && defined (__va_copy)
# define va_copy(d,s)  __va_copy((d),(s))
#endif
#include <stdio.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#else
extern unsigned long strtoul ();
extern PTR malloc ();
#endif
#include "libiberty.h"

#ifdef TEST
int global_total_width;
#endif

/*

@deftypefn Extension int vasprintf (char **@var{resptr}, const char *@var{format}, va_list @var{args})

Like @code{vsprintf}, but instead of passing a pointer to a buffer,
you pass a pointer to a pointer.  This function will compute the size
of the buffer needed, allocate memory with @code{malloc}, and store a
pointer to the allocated memory in @code{*@var{resptr}}.  The value
returned is the same as @code{vsprintf} would return.  If memory could
not be allocated, minus one is returned and @code{NULL} is stored in
@code{*@var{resptr}}.

@end deftypefn

*/

static int int_vasprintf (char **, const char *, va_list);

static int
int_vasprintf (char **result, const char *format, va_list args)
{
  const char *p = format;
  /* Add one to make sure that it is never zero, which might cause malloc
     to return NULL.  */
  int total_width = strlen (format) + 1;
  va_list ap;

#ifdef va_copy
  va_copy (ap, args);
#else
  memcpy ((PTR) &ap, (PTR) &args, sizeof (va_list));
#endif

  while (*p != '\0')
    {
      if (*p++ == '%')
	{
	  while (strchr ("-+ #0", *p))
	    ++p;
	  if (*p == '*')
	    {
	      ++p;
	      total_width += abs (va_arg (ap, int));
	    }
	  else
	    total_width += strtoul (p, (char **) &p, 10);
	  if (*p == '.')
	    {
	      ++p;
	      if (*p == '*')
		{
		  ++p;
		  total_width += abs (va_arg (ap, int));
		}
	      else
	      total_width += strtoul (p, (char **) &p, 10);
	    }
	  while (strchr ("hlL", *p))
	    ++p;
	  /* Should be big enough for any format specifier except %s and floats.  */
	  total_width += 30;
	  switch (*p)
	    {
	    case 'd':
	    case 'i':
	    case 'o':
	    case 'u':
	    case 'x':
	    case 'X':
	    case 'c':
	      (void) va_arg (ap, int);
	      break;
	    case 'f':
	    case 'e':
	    case 'E':
	    case 'g':
	    case 'G':
	      (void) va_arg (ap, double);
	      /* Since an ieee double can have an exponent of 307, we'll
		 make the buffer wide enough to cover the gross case. */
	      total_width += 307;
	      break;
	    case 's':
	      total_width += strlen (va_arg (ap, char *));
	      break;
	    case 'p':
	    case 'n':
	      (void) va_arg (ap, char *);
	      break;
	    }
	  p++;
	}
    }
#ifdef va_copy
  va_end (ap);
#endif
#ifdef TEST
  global_total_width = total_width;
#endif
  *result = (char *) malloc (total_width);
  if (*result != NULL)
    return vsprintf (*result, format, args);
  else
    return -1;
}

int
vasprintf (char **result, const char *format,
#if defined (_BSD_VA_LIST_) && defined (__FreeBSD__)
           _BSD_VA_LIST_ args)
#else
           va_list args)
#endif
{
  return int_vasprintf (result, format, args);
}

#ifdef TEST
static void ATTRIBUTE_PRINTF_1
checkit (const char *format, ...)
{
  char *result;
  VA_OPEN (args, format);
  VA_FIXEDARG (args, const char *, format);
  vasprintf (&result, format, args);
  VA_CLOSE (args);

  if (strlen (result) < (size_t) global_total_width)
    printf ("PASS: ");
  else
    printf ("FAIL: ");
  printf ("%d %s\n", global_total_width, result);

  free (result);
}

extern int main (void);

int
main (void)
{
  checkit ("%d", 0x12345678);
  checkit ("%200d", 5);
  checkit ("%.300d", 6);
  checkit ("%100.150d", 7);
  checkit ("%s", "jjjjjjjjjiiiiiiiiiiiiiiioooooooooooooooooppppppppppppaa\n\
777777777777777777333333333333366666666666622222222222777777777777733333");
  checkit ("%f%s%d%s", 1.0, "foo", 77, "asdjffffffffffffffiiiiiiiiiiixxxxx");

  return 0;
}
#endif /* TEST */
