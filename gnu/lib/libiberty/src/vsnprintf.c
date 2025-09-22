/* Implement the vsnprintf function.
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
   Written by Kaveh R. Ghazi <ghazi@caip.rutgers.edu>.

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

@deftypefn Supplemental int vsnprintf (char *@var{buf}, size_t @var{n}, const char *@var{format}, va_list @var{ap})

This function is similar to vsprintf, but it will print at most
@var{n} characters.  On error the return value is -1, otherwise it
returns the number of characters that would have been printed had
@var{n} been sufficiently large, regardless of the actual value of
@var{n}.  Note some pre-C99 system libraries do not implement this
correctly so users cannot generally rely on the return value if the
system version of this function is used.

@end deftypefn

*/

#include "config.h"
#include "ansidecl.h"

#include <stdarg.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include "libiberty.h"

/* This implementation relies on a working vasprintf.  */
int
vsnprintf (char *s, size_t n, const char *format, va_list ap)
{
  char *buf = 0;
  int result = vasprintf (&buf, format, ap);

  if (!buf)
    return -1;
  if (result < 0)
    {
      free (buf);
      return -1;
    }

  result = strlen (buf);
  if (n > 0)
    {
      if ((long) n > result)
	memcpy (s, buf, result+1);
      else
        {
	  memcpy (s, buf, n-1);
	  s[n - 1] = 0;
	}
    }
  free (buf);
  return result;
}

#ifdef TEST
/* Set the buffer to a known state.  */
#define CLEAR(BUF) do { memset ((BUF), 'X', sizeof (BUF)); (BUF)[14] = '\0'; } while (0)
/* For assertions.  */
#define VERIFY(P) do { if (!(P)) abort(); } while (0)

static int ATTRIBUTE_PRINTF_3
checkit (char *s, size_t n, const char *format, ...)
{
  int result;
  VA_OPEN (ap, format);
  VA_FIXEDARG (ap, char *, s);
  VA_FIXEDARG (ap, size_t, n);
  VA_FIXEDARG (ap, const char *, format);
  result = vsnprintf (s, n, format, ap);
  VA_CLOSE (ap);
  return result;
}

extern int main (void);
int
main (void)
{
  char buf[128];
  int status;
  
  CLEAR (buf);
  status = checkit (buf, 10, "%s:%d", "foobar", 9);
  VERIFY (status==8 && memcmp (buf, "foobar:9\0XXXXX\0", 15) == 0);

  CLEAR (buf);
  status = checkit (buf, 9, "%s:%d", "foobar", 9);
  VERIFY (status==8 && memcmp (buf, "foobar:9\0XXXXX\0", 15) == 0);

  CLEAR (buf);
  status = checkit (buf, 8, "%s:%d", "foobar", 9);
  VERIFY (status==8 && memcmp (buf, "foobar:\0XXXXXX\0", 15) == 0);

  CLEAR (buf);
  status = checkit (buf, 7, "%s:%d", "foobar", 9);
  VERIFY (status==8 && memcmp (buf, "foobar\0XXXXXXX\0", 15) == 0);

  CLEAR (buf);
  status = checkit (buf, 6, "%s:%d", "foobar", 9);
  VERIFY (status==8 && memcmp (buf, "fooba\0XXXXXXXX\0", 15) == 0);

  CLEAR (buf);
  status = checkit (buf, 2, "%s:%d", "foobar", 9);
  VERIFY (status==8 && memcmp (buf, "f\0XXXXXXXXXXXX\0", 15) == 0);

  CLEAR (buf);
  status = checkit (buf, 1, "%s:%d", "foobar", 9);
  VERIFY (status==8 && memcmp (buf, "\0XXXXXXXXXXXXX\0", 15) == 0);

  CLEAR (buf);
  status = checkit (buf, 0, "%s:%d", "foobar", 9);
  VERIFY (status==8 && memcmp (buf, "XXXXXXXXXXXXXX\0", 15) == 0);

  return 0;
}
#endif /* TEST */
