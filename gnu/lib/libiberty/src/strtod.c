/* Implementation of strtod for systems with atof.
   Copyright (C) 1991, 1995, 2002 Free Software Foundation, Inc.

This file is part of the libiberty library.  This library is free
software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option)
any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.

As a special exception, if you link this library with files
compiled with a GNU compiler to produce an executable, this does not cause
the resulting executable to be covered by the GNU General Public License.
This exception does not however invalidate any other reasons why
the executable file might be covered by the GNU General Public License. */

/*

@deftypefn Supplemental double strtod (const char *@var{string}, char **@var{endptr})

This ISO C function converts the initial portion of @var{string} to a
@code{double}.  If @var{endptr} is not @code{NULL}, a pointer to the
character after the last character used in the conversion is stored in
the location referenced by @var{endptr}.  If no conversion is
performed, zero is returned and the value of @var{string} is stored in
the location referenced by @var{endptr}.

@end deftypefn

*/

#include "ansidecl.h"
#include "safe-ctype.h"

extern double atof (const char *);

/* Disclaimer: this is currently just used by CHILL in GDB and therefore
   has not been tested well.  It may have been tested for nothing except
   that it compiles.  */

double
strtod (char *str, char **ptr)
{
  char *p;

  if (ptr == (char **)0)
    return atof (str);
  
  p = str;
  
  while (ISSPACE (*p))
    ++p;
  
  if (*p == '+' || *p == '-')
    ++p;

  /* INF or INFINITY.  */
  if ((p[0] == 'i' || p[0] == 'I')
      && (p[1] == 'n' || p[1] == 'N')
      && (p[2] == 'f' || p[2] == 'F'))
    {
      if ((p[3] == 'i' || p[3] == 'I')
	  && (p[4] == 'n' || p[4] == 'N')
	  && (p[5] == 'i' || p[5] == 'I')
	  && (p[6] == 't' || p[6] == 'T')
	  && (p[7] == 'y' || p[7] == 'Y'))
	{
	  *ptr = p + 8;
	  return atof (str);
	}
      else
	{
	  *ptr = p + 3;
	  return atof (str);
	}
    }

  /* NAN or NAN(foo).  */
  if ((p[0] == 'n' || p[0] == 'N')
      && (p[1] == 'a' || p[1] == 'A')
      && (p[2] == 'n' || p[2] == 'N'))
    {
      p += 3;
      if (*p == '(')
	{
	  ++p;
	  while (*p != '\0' && *p != ')')
	    ++p;
	  if (*p == ')')
	    ++p;
	}
      *ptr = p;
      return atof (str);
    }

  /* digits, with 0 or 1 periods in it.  */
  if (ISDIGIT (*p) || *p == '.')
    {
      int got_dot = 0;
      while (ISDIGIT (*p) || (!got_dot && *p == '.'))
	{
	  if (*p == '.')
	    got_dot = 1;
	  ++p;
	}

      /* Exponent.  */
      if (*p == 'e' || *p == 'E')
	{
	  int i;
	  i = 1;
	  if (p[i] == '+' || p[i] == '-')
	    ++i;
	  if (ISDIGIT (p[i]))
	    {
	      while (ISDIGIT (p[i]))
		++i;
	      *ptr = p + i;
	      return atof (str);
	    }
	}
      *ptr = p;
      return atof (str);
    }
  /* Didn't find any digits.  Doesn't look like a number.  */
  *ptr = str;
  return 0.0;
}
