/* Structures that hang off cpp_identifier, for PCH.
   Copyright (C) 1986, 1987, 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004 Free Software Foundation, Inc.

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
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "cpplib.h"

#if !defined (HAVE_UCHAR)
typedef unsigned char uchar;
#endif

#define U (const unsigned char *)  /* Intended use: U"string" */

/* Chained list of answers to an assertion.  */
struct answer GTY(())
{
  struct answer *next;
  unsigned int count;
  cpp_token GTY ((length ("%h.count"))) first[1];
};

/* Each macro definition is recorded in a cpp_macro structure.
   Variadic macros cannot occur with traditional cpp.  */
struct cpp_macro GTY(())
{
  /* Parameters, if any.  */
  cpp_hashnode ** GTY ((nested_ptr (union tree_node,
		"%h ? CPP_HASHNODE (GCC_IDENT_TO_HT_IDENT (%h)) : NULL",
			"%h ? HT_IDENT_TO_GCC_IDENT (HT_NODE (%h)) : NULL"),
			length ("%h.paramc")))
    params;

  /* Replacement tokens (ISO) or replacement text (traditional).  See
     comment at top of cpptrad.c for how traditional function-like
     macros are encoded.  */
  union cpp_macro_u
  {
    cpp_token * GTY ((tag ("0"), length ("%0.count"))) tokens;
    const unsigned char * GTY ((tag ("1"))) text;
  } GTY ((desc ("%1.traditional"))) exp;

  /* Definition line number.  */
  source_location line;

  /* Number of tokens in expansion, or bytes for traditional macros.  */
  unsigned int count;

  /* Number of parameters.  */
  unsigned short paramc;

  /* If a function-like macro.  */
  unsigned int fun_like : 1;

  /* If a variadic macro.  */
  unsigned int variadic : 1;

  /* If macro defined in system header.  */
  unsigned int syshdr   : 1;

  /* Nonzero if it has been expanded or had its existence tested.  */
  unsigned int used     : 1;

  /* Indicate which field of 'exp' is in use.  */
  unsigned int traditional : 1;
};
