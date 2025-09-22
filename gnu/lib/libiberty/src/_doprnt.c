/* Provide a version of _doprnt in terms of fprintf.
   Copyright (C) 1998, 1999, 2000, 2001, 2002   Free Software Foundation, Inc.
   Contributed by Kaveh Ghazi  (ghazi@caip.rutgers.edu)  3/29/98

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "ansidecl.h"
#include "safe-ctype.h"

#include <stdio.h>
#include <stdarg.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#undef _doprnt

#ifdef HAVE__DOPRNT
#define TEST
#endif

#ifdef TEST /* Make sure to use the internal one.  */
#define _doprnt my_doprnt
#endif

#define COPY_VA_INT \
  do { \
	 const int value = abs (va_arg (ap, int)); \
	 char buf[32]; \
	 ptr++; /* Go past the asterisk.  */ \
	 *sptr = '\0'; /* NULL terminate sptr.  */ \
	 sprintf(buf, "%d", value); \
	 strcat(sptr, buf); \
	 while (*sptr) sptr++; \
     } while (0)

#define PRINT_CHAR(CHAR) \
  do { \
	 putc(CHAR, stream); \
	 ptr++; \
	 total_printed++; \
	 continue; \
     } while (0)

#define PRINT_TYPE(TYPE) \
  do { \
	int result; \
	TYPE value = va_arg (ap, TYPE); \
	*sptr++ = *ptr++; /* Copy the type specifier.  */ \
	*sptr = '\0'; /* NULL terminate sptr.  */ \
	result = fprintf(stream, specifier, value); \
	if (result == -1) \
	  return -1; \
	else \
	  { \
	    total_printed += result; \
	    continue; \
	  } \
      } while (0)

int
_doprnt (const char *format, va_list ap, FILE *stream)
{
  const char * ptr = format;
  char specifier[128];
  int total_printed = 0;
  
  while (*ptr != '\0')
    {
      if (*ptr != '%') /* While we have regular characters, print them.  */
	PRINT_CHAR(*ptr);
      else /* We got a format specifier! */
	{
	  char * sptr = specifier;
	  int wide_width = 0, short_width = 0;
	  
	  *sptr++ = *ptr++; /* Copy the % and move forward.  */

	  while (strchr ("-+ #0", *ptr)) /* Move past flags.  */
	    *sptr++ = *ptr++;

	  if (*ptr == '*')
	    COPY_VA_INT;
	  else
	    while (ISDIGIT(*ptr)) /* Handle explicit numeric value.  */
	      *sptr++ = *ptr++;
	  
	  if (*ptr == '.')
	    {
	      *sptr++ = *ptr++; /* Copy and go past the period.  */
	      if (*ptr == '*')
		COPY_VA_INT;
	      else
		while (ISDIGIT(*ptr)) /* Handle explicit numeric value.  */
		  *sptr++ = *ptr++;
	    }
	  while (strchr ("hlL", *ptr))
	    {
	      switch (*ptr)
		{
		case 'h':
		  short_width = 1;
		  break;
		case 'l':
		  wide_width++;
		  break;
		case 'L':
		  wide_width = 2;
		  break;
		default:
		  abort();
		}
	      *sptr++ = *ptr++;
	    }

	  switch (*ptr)
	    {
	    case 'd':
	    case 'i':
	    case 'o':
	    case 'u':
	    case 'x':
	    case 'X':
	    case 'c':
	      {
		/* Short values are promoted to int, so just copy it
                   as an int and trust the C library printf to cast it
                   to the right width.  */
		if (short_width)
		  PRINT_TYPE(int);
		else
		  {
		    switch (wide_width)
		      {
		      case 0:
			PRINT_TYPE(int);
			break;
		      case 1:
			PRINT_TYPE(long);
			break;
		      case 2:
		      default:
#if defined(__GNUC__) || defined(HAVE_LONG_LONG)
			PRINT_TYPE(long long);
#else
			PRINT_TYPE(long); /* Fake it and hope for the best.  */
#endif
			break;
		      } /* End of switch (wide_width) */
		  } /* End of else statement */
	      } /* End of integer case */
	      break;
	    case 'f':
	    case 'e':
	    case 'E':
	    case 'g':
	    case 'G':
	      {
		if (wide_width == 0)
		  PRINT_TYPE(double);
		else
		  {
#if defined(__GNUC__) || defined(HAVE_LONG_DOUBLE)
		    PRINT_TYPE(long double);
#else
		    PRINT_TYPE(double); /* Fake it and hope for the best.  */
#endif
		  }
	      }
	      break;
	    case 's':
	      PRINT_TYPE(char *);
	      break;
	    case 'p':
	      PRINT_TYPE(void *);
	      break;
	    case '%':
	      PRINT_CHAR('%');
	      break;
	    default:
	      abort();
	    } /* End of switch (*ptr) */
	} /* End of else statement */
    }

  return total_printed;
}

#ifdef TEST

#include <math.h>
#ifndef M_PI
#define M_PI (3.1415926535897932385)
#endif

#define RESULT(x) do \
{ \
    int i = (x); \
    printf ("printed %d characters\n", i); \
    fflush(stdin); \
} while (0)

static int checkit (const char * format, ...) ATTRIBUTE_PRINTF_1;

static int
checkit (const char* format, ...)
{
  int result;
  VA_OPEN (args, format);
  VA_FIXEDARG (args, char *, format);

  result = _doprnt (format, args, stdout);
  VA_CLOSE (args);

  return result;
}

int
main (void)
{
  RESULT(checkit ("<%d>\n", 0x12345678));
  RESULT(printf ("<%d>\n", 0x12345678));

  RESULT(checkit ("<%200d>\n", 5));
  RESULT(printf ("<%200d>\n", 5));

  RESULT(checkit ("<%.300d>\n", 6));
  RESULT(printf ("<%.300d>\n", 6));

  RESULT(checkit ("<%100.150d>\n", 7));
  RESULT(printf ("<%100.150d>\n", 7));

  RESULT(checkit ("<%s>\n",
		  "jjjjjjjjjiiiiiiiiiiiiiiioooooooooooooooooppppppppppppaa\n\
777777777777777777333333333333366666666666622222222222777777777777733333"));
  RESULT(printf ("<%s>\n",
		 "jjjjjjjjjiiiiiiiiiiiiiiioooooooooooooooooppppppppppppaa\n\
777777777777777777333333333333366666666666622222222222777777777777733333"));

  RESULT(checkit ("<%f><%0+#f>%s%d%s>\n",
		  1.0, 1.0, "foo", 77, "asdjffffffffffffffiiiiiiiiiiixxxxx"));
  RESULT(printf ("<%f><%0+#f>%s%d%s>\n",
		 1.0, 1.0, "foo", 77, "asdjffffffffffffffiiiiiiiiiiixxxxx"));

  RESULT(checkit ("<%4f><%.4f><%%><%4.4f>\n", M_PI, M_PI, M_PI));
  RESULT(printf ("<%4f><%.4f><%%><%4.4f>\n", M_PI, M_PI, M_PI));

  RESULT(checkit ("<%*f><%.*f><%%><%*.*f>\n", 3, M_PI, 3, M_PI, 3, 3, M_PI));
  RESULT(printf ("<%*f><%.*f><%%><%*.*f>\n", 3, M_PI, 3, M_PI, 3, 3, M_PI));

  RESULT(checkit ("<%d><%i><%o><%u><%x><%X><%c>\n",
		  75, 75, 75, 75, 75, 75, 75));
  RESULT(printf ("<%d><%i><%o><%u><%x><%X><%c>\n",
		 75, 75, 75, 75, 75, 75, 75));

  RESULT(checkit ("<%d><%i><%o><%u><%x><%X><%c>\n",
		  75, 75, 75, 75, 75, 75, 75));
  RESULT(printf ("<%d><%i><%o><%u><%x><%X><%c>\n",
		 75, 75, 75, 75, 75, 75, 75));

  RESULT(checkit ("Testing (hd) short: <%d><%ld><%hd><%hd><%d>\n", 123, (long)234, 345, 123456789, 456));
  RESULT(printf ("Testing (hd) short: <%d><%ld><%hd><%hd><%d>\n", 123, (long)234, 345, 123456789, 456));

#if defined(__GNUC__) || defined (HAVE_LONG_LONG)
  RESULT(checkit ("Testing (lld) long long: <%d><%lld><%d>\n", 123, 234234234234234234LL, 345));
  RESULT(printf ("Testing (lld) long long: <%d><%lld><%d>\n", 123, 234234234234234234LL, 345));
  RESULT(checkit ("Testing (Ld) long long: <%d><%Ld><%d>\n", 123, 234234234234234234LL, 345));
  RESULT(printf ("Testing (Ld) long long: <%d><%Ld><%d>\n", 123, 234234234234234234LL, 345));
#endif

#if defined(__GNUC__) || defined (HAVE_LONG_DOUBLE)
  RESULT(checkit ("Testing (Lf) long double: <%.20f><%.20Lf><%0+#.20f>\n",
		  1.23456, 1.234567890123456789L, 1.23456));
  RESULT(printf ("Testing (Lf) long double: <%.20f><%.20Lf><%0+#.20f>\n",
		 1.23456, 1.234567890123456789L, 1.23456));
#endif

  return 0;
}
#endif /* TEST */
