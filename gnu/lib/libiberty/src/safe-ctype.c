/* <ctype.h> replacement macros.

   Copyright (C) 2000, 2001, 2002, 2003, 2004,
   2005 Free Software Foundation, Inc.
   Contributed by Zack Weinberg <zackw@stanford.edu>.

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

/*

@defvr Extension HOST_CHARSET
This macro indicates the basic character set and encoding used by the
host: more precisely, the encoding used for character constants in
preprocessor @samp{#if} statements (the C "execution character set").
It is defined by @file{safe-ctype.h}, and will be an integer constant
with one of the following values:

@ftable @code
@item HOST_CHARSET_UNKNOWN
The host character set is unknown - that is, not one of the next two
possibilities.

@item HOST_CHARSET_ASCII
The host character set is ASCII.

@item HOST_CHARSET_EBCDIC
The host character set is some variant of EBCDIC.  (Only one of the
nineteen EBCDIC varying characters is tested; exercise caution.)
@end ftable
@end defvr

@deffn  Extension ISALPHA  (@var{c})
@deffnx Extension ISALNUM  (@var{c})
@deffnx Extension ISBLANK  (@var{c})
@deffnx Extension ISCNTRL  (@var{c})
@deffnx Extension ISDIGIT  (@var{c})
@deffnx Extension ISGRAPH  (@var{c})
@deffnx Extension ISLOWER  (@var{c})
@deffnx Extension ISPRINT  (@var{c})
@deffnx Extension ISPUNCT  (@var{c})
@deffnx Extension ISSPACE  (@var{c})
@deffnx Extension ISUPPER  (@var{c})
@deffnx Extension ISXDIGIT (@var{c})

These twelve macros are defined by @file{safe-ctype.h}.  Each has the
same meaning as the corresponding macro (with name in lowercase)
defined by the standard header @file{ctype.h}.  For example,
@code{ISALPHA} returns true for alphabetic characters and false for
others.  However, there are two differences between these macros and
those provided by @file{ctype.h}:

@itemize @bullet
@item These macros are guaranteed to have well-defined behavior for all 
values representable by @code{signed char} and @code{unsigned char}, and
for @code{EOF}.

@item These macros ignore the current locale; they are true for these
fixed sets of characters:
@multitable {@code{XDIGIT}} {yada yada yada yada yada yada yada yada}
@item @code{ALPHA}  @tab @kbd{A-Za-z}
@item @code{ALNUM}  @tab @kbd{A-Za-z0-9}
@item @code{BLANK}  @tab @kbd{space tab}
@item @code{CNTRL}  @tab @code{!PRINT}
@item @code{DIGIT}  @tab @kbd{0-9}
@item @code{GRAPH}  @tab @code{ALNUM || PUNCT}
@item @code{LOWER}  @tab @kbd{a-z}
@item @code{PRINT}  @tab @code{GRAPH ||} @kbd{space}
@item @code{PUNCT}  @tab @kbd{`~!@@#$%^&*()_-=+[@{]@}\|;:'",<.>/?}
@item @code{SPACE}  @tab @kbd{space tab \n \r \f \v}
@item @code{UPPER}  @tab @kbd{A-Z}
@item @code{XDIGIT} @tab @kbd{0-9A-Fa-f}
@end multitable

Note that, if the host character set is ASCII or a superset thereof,
all these macros will return false for all values of @code{char} outside
the range of 7-bit ASCII.  In particular, both ISPRINT and ISCNTRL return
false for characters with numeric values from 128 to 255.
@end itemize
@end deffn

@deffn  Extension ISIDNUM         (@var{c})
@deffnx Extension ISIDST          (@var{c})
@deffnx Extension IS_VSPACE       (@var{c})
@deffnx Extension IS_NVSPACE      (@var{c})
@deffnx Extension IS_SPACE_OR_NUL (@var{c})
@deffnx Extension IS_ISOBASIC     (@var{c})
These six macros are defined by @file{safe-ctype.h} and provide
additional character classes which are useful when doing lexical
analysis of C or similar languages.  They are true for the following
sets of characters:

@multitable {@code{SPACE_OR_NUL}} {yada yada yada yada yada yada yada yada}
@item @code{IDNUM}        @tab @kbd{A-Za-z0-9_}
@item @code{IDST}         @tab @kbd{A-Za-z_}
@item @code{VSPACE}       @tab @kbd{\r \n}
@item @code{NVSPACE}      @tab @kbd{space tab \f \v \0}
@item @code{SPACE_OR_NUL} @tab @code{VSPACE || NVSPACE}
@item @code{ISOBASIC}     @tab @code{VSPACE || NVSPACE || PRINT}
@end multitable
@end deffn

*/

#include "ansidecl.h"
#include <safe-ctype.h>
#include <stdio.h>  /* for EOF */

#if EOF != -1
 #error "<safe-ctype.h> requires EOF == -1"
#endif

/* Shorthand */
#define bl _sch_isblank
#define cn _sch_iscntrl
#define di _sch_isdigit
#define is _sch_isidst
#define lo _sch_islower
#define nv _sch_isnvsp
#define pn _sch_ispunct
#define pr _sch_isprint
#define sp _sch_isspace
#define up _sch_isupper
#define vs _sch_isvsp
#define xd _sch_isxdigit

/* Masks.  */
#define L  (const unsigned short) (lo|is   |pr)	/* lower case letter */
#define XL (const unsigned short) (lo|is|xd|pr)	/* lowercase hex digit */
#define U  (const unsigned short) (up|is   |pr)	/* upper case letter */
#define XU (const unsigned short) (up|is|xd|pr)	/* uppercase hex digit */
#define D  (const unsigned short) (di   |xd|pr)	/* decimal digit */
#define P  (const unsigned short) (pn      |pr)	/* punctuation */
#define _  (const unsigned short) (pn|is   |pr)	/* underscore */

#define C  (const unsigned short) (         cn)	/* control character */
#define Z  (const unsigned short) (nv      |cn)	/* NUL */
#define M  (const unsigned short) (nv|sp   |cn)	/* cursor movement: \f \v */
#define V  (const unsigned short) (vs|sp   |cn)	/* vertical space: \r \n */
#define T  (const unsigned short) (nv|sp|bl|cn)	/* tab */
#define S  (const unsigned short) (nv|sp|bl|pr)	/* space */

/* Are we ASCII? */
#if HOST_CHARSET == HOST_CHARSET_ASCII

const unsigned short _sch_istable[256] =
{
  Z,  C,  C,  C,   C,  C,  C,  C,   /* NUL SOH STX ETX  EOT ENQ ACK BEL */
  C,  T,  V,  M,   M,  V,  C,  C,   /* BS  HT  LF  VT   FF  CR  SO  SI  */
  C,  C,  C,  C,   C,  C,  C,  C,   /* DLE DC1 DC2 DC3  DC4 NAK SYN ETB */
  C,  C,  C,  C,   C,  C,  C,  C,   /* CAN EM  SUB ESC  FS  GS  RS  US  */
  S,  P,  P,  P,   P,  P,  P,  P,   /* SP  !   "   #    $   %   &   '   */
  P,  P,  P,  P,   P,  P,  P,  P,   /* (   )   *   +    ,   -   .   /   */
  D,  D,  D,  D,   D,  D,  D,  D,   /* 0   1   2   3    4   5   6   7   */
  D,  D,  P,  P,   P,  P,  P,  P,   /* 8   9   :   ;    <   =   >   ?   */
  P, XU, XU, XU,  XU, XU, XU,  U,   /* @   A   B   C    D   E   F   G   */
  U,  U,  U,  U,   U,  U,  U,  U,   /* H   I   J   K    L   M   N   O   */
  U,  U,  U,  U,   U,  U,  U,  U,   /* P   Q   R   S    T   U   V   W   */
  U,  U,  U,  P,   P,  P,  P,  _,   /* X   Y   Z   [    \   ]   ^   _   */
  P, XL, XL, XL,  XL, XL, XL,  L,   /* `   a   b   c    d   e   f   g   */
  L,  L,  L,  L,   L,  L,  L,  L,   /* h   i   j   k    l   m   n   o   */
  L,  L,  L,  L,   L,  L,  L,  L,   /* p   q   r   s    t   u   v   w   */
  L,  L,  L,  P,   P,  P,  P,  C,   /* x   y   z   {    |   }   ~   DEL */

  /* high half of unsigned char is locale-specific, so all tests are
     false in "C" locale */
  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,
  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,
  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,
  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,

  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,
  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,
  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,
  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,
};

const unsigned char _sch_tolower[256] =
{
   0,  1,  2,  3,   4,  5,  6,  7,   8,  9, 10, 11,  12, 13, 14, 15,
  16, 17, 18, 19,  20, 21, 22, 23,  24, 25, 26, 27,  28, 29, 30, 31,
  32, 33, 34, 35,  36, 37, 38, 39,  40, 41, 42, 43,  44, 45, 46, 47,
  48, 49, 50, 51,  52, 53, 54, 55,  56, 57, 58, 59,  60, 61, 62, 63,
  64,

  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
  'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',

  91, 92, 93, 94, 95, 96,

  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
  'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',

 123,124,125,126,127,

 128,129,130,131, 132,133,134,135, 136,137,138,139, 140,141,142,143,
 144,145,146,147, 148,149,150,151, 152,153,154,155, 156,157,158,159,
 160,161,162,163, 164,165,166,167, 168,169,170,171, 172,173,174,175,
 176,177,178,179, 180,181,182,183, 184,185,186,187, 188,189,190,191,

 192,193,194,195, 196,197,198,199, 200,201,202,203, 204,205,206,207,
 208,209,210,211, 212,213,214,215, 216,217,218,219, 220,221,222,223,
 224,225,226,227, 228,229,230,231, 232,233,234,235, 236,237,238,239,
 240,241,242,243, 244,245,246,247, 248,249,250,251, 252,253,254,255,
};

const unsigned char _sch_toupper[256] =
{
   0,  1,  2,  3,   4,  5,  6,  7,   8,  9, 10, 11,  12, 13, 14, 15,
  16, 17, 18, 19,  20, 21, 22, 23,  24, 25, 26, 27,  28, 29, 30, 31,
  32, 33, 34, 35,  36, 37, 38, 39,  40, 41, 42, 43,  44, 45, 46, 47,
  48, 49, 50, 51,  52, 53, 54, 55,  56, 57, 58, 59,  60, 61, 62, 63,
  64,

  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
  'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',

  91, 92, 93, 94, 95, 96,

  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
  'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',

 123,124,125,126,127,

 128,129,130,131, 132,133,134,135, 136,137,138,139, 140,141,142,143,
 144,145,146,147, 148,149,150,151, 152,153,154,155, 156,157,158,159,
 160,161,162,163, 164,165,166,167, 168,169,170,171, 172,173,174,175,
 176,177,178,179, 180,181,182,183, 184,185,186,187, 188,189,190,191,

 192,193,194,195, 196,197,198,199, 200,201,202,203, 204,205,206,207,
 208,209,210,211, 212,213,214,215, 216,217,218,219, 220,221,222,223,
 224,225,226,227, 228,229,230,231, 232,233,234,235, 236,237,238,239,
 240,241,242,243, 244,245,246,247, 248,249,250,251, 252,253,254,255,
};

#else
# if HOST_CHARSET == HOST_CHARSET_EBCDIC
  #error "FIXME: write tables for EBCDIC"
# else
  #error "Unrecognized host character set"
# endif
#endif
