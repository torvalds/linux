/* Compare strings while treating digits characters numerically.
   Copyright (C) 1997, 2002, 2005 Free Software Foundation, Inc.
   This file is part of the libiberty library.
   Contributed by Jean-François Bignolles <bignolle@ecoledoc.ibp.fr>, 1997.

   Libiberty is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   Libiberty is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301 USA.  */

#include "libiberty.h"
#include "safe-ctype.h"

/* 
@deftypefun int strverscmp (const char *@var{s1}, const char *@var{s2})
The @code{strverscmp} function compares the string @var{s1} against
@var{s2}, considering them as holding indices/version numbers.  Return
value follows the same conventions as found in the @code{strverscmp}
function.  In fact, if @var{s1} and @var{s2} contain no digits,
@code{strverscmp} behaves like @code{strcmp}.

Basically, we compare strings normally (character by character), until
we find a digit in each string - then we enter a special comparison
mode, where each sequence of digits is taken as a whole.  If we reach the
end of these two parts without noticing a difference, we return to the
standard comparison mode.  There are two types of numeric parts:
"integral" and "fractional" (those  begin with a '0'). The types
of the numeric parts affect the way we sort them:

@itemize @bullet
@item
integral/integral: we compare values as you would expect.

@item
fractional/integral: the fractional part is less than the integral one.
Again, no surprise.

@item
fractional/fractional: the things become a bit more complex.
If the common prefix contains only leading zeroes, the longest part is less
than the other one; else the comparison behaves normally.
@end itemize

@smallexample
strverscmp ("no digit", "no digit")
    @result{} 0    // @r{same behavior as strcmp.}
strverscmp ("item#99", "item#100")
    @result{} <0   // @r{same prefix, but 99 < 100.}
strverscmp ("alpha1", "alpha001")
    @result{} >0   // @r{fractional part inferior to integral one.}
strverscmp ("part1_f012", "part1_f01")
    @result{} >0   // @r{two fractional parts.}
strverscmp ("foo.009", "foo.0")
    @result{} <0   // @r{idem, but with leading zeroes only.}
@end smallexample

This function is especially useful when dealing with filename sorting,
because filenames frequently hold indices/version numbers.
@end deftypefun

*/

/* states: S_N: normal, S_I: comparing integral part, S_F: comparing
           fractional parts, S_Z: idem but with leading Zeroes only */
#define  S_N    0x0
#define  S_I    0x4
#define  S_F    0x8
#define  S_Z    0xC

/* result_type: CMP: return diff; LEN: compare using len_diff/diff */
#define  CMP    2
#define  LEN    3


/* Compare S1 and S2 as strings holding indices/version numbers,
   returning less than, equal to or greater than zero if S1 is less than,
   equal to or greater than S2 (for more info, see the Glibc texinfo doc).  */

int
strverscmp (const char *s1, const char *s2)
{
  const unsigned char *p1 = (const unsigned char *) s1;
  const unsigned char *p2 = (const unsigned char *) s2;
  unsigned char c1, c2;
  int state;
  int diff;

  /* Symbol(s)    0       [1-9]   others  (padding)
     Transition   (10) 0  (01) d  (00) x  (11) -   */
  static const unsigned int next_state[] =
    {
      /* state    x    d    0    - */
      /* S_N */  S_N, S_I, S_Z, S_N,
      /* S_I */  S_N, S_I, S_I, S_I,
      /* S_F */  S_N, S_F, S_F, S_F,
      /* S_Z */  S_N, S_F, S_Z, S_Z
    };

  static const int result_type[] =
    {
      /* state   x/x  x/d  x/0  x/-  d/x  d/d  d/0  d/-
                 0/x  0/d  0/0  0/-  -/x  -/d  -/0  -/- */

      /* S_N */  CMP, CMP, CMP, CMP, CMP, LEN, CMP, CMP,
                 CMP, CMP, CMP, CMP, CMP, CMP, CMP, CMP,
      /* S_I */  CMP, -1,  -1,  CMP, +1,  LEN, LEN, CMP,
                 +1,  LEN, LEN, CMP, CMP, CMP, CMP, CMP,
      /* S_F */  CMP, CMP, CMP, CMP, CMP, LEN, CMP, CMP,
                 CMP, CMP, CMP, CMP, CMP, CMP, CMP, CMP,
      /* S_Z */  CMP, +1,  +1,  CMP, -1,  CMP, CMP, CMP,
                 -1,  CMP, CMP, CMP
    };

  if (p1 == p2)
    return 0;

  c1 = *p1++;
  c2 = *p2++;
  /* Hint: '0' is a digit too.  */
  state = S_N | ((c1 == '0') + (ISDIGIT (c1) != 0));

  while ((diff = c1 - c2) == 0 && c1 != '\0')
    {
      state = next_state[state];
      c1 = *p1++;
      c2 = *p2++;
      state |= (c1 == '0') + (ISDIGIT (c1) != 0);
    }

  state = result_type[state << 2 | (((c2 == '0') + (ISDIGIT (c2) != 0)))];

  switch (state)
    {
    case CMP:
      return diff;
      
    case LEN:
      while (ISDIGIT (*p1++))
	if (!ISDIGIT (*p2++))
	  return 1;
      
      return ISDIGIT (*p2) ? -1 : diff;
      
    default:
      return state;
    }
}
