/* Subroutines for the C front end on the TMS320C[34]x
   Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001
   Free Software Foundation, Inc.

   Contributed by Michael Hayes (m.hayes@elec.canterbury.ac.nz)
              and Herman Ten Brugge (Haj.Ten.Brugge@net.HCC.nl).

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "toplev.h"
#include "cpplib.h"
#include "c-pragma.h"
#include "tm_p.h"

static int c4x_parse_pragma (const char *, tree *, tree *);

/* Handle machine specific pragmas for compatibility with existing
   compilers for the C3x/C4x.

   pragma				   attribute
   ----------------------------------------------------------
   CODE_SECTION(symbol,"section")          section("section")
   DATA_SECTION(symbol,"section")          section("section")
   FUNC_CANNOT_INLINE(function)            
   FUNC_EXT_CALLED(function)               
   FUNC_IS_PURE(function)                  const
   FUNC_IS_SYSTEM(function)                
   FUNC_NEVER_RETURNS(function)            noreturn
   FUNC_NO_GLOBAL_ASG(function)            
   FUNC_NO_IND_ASG(function)               
   INTERRUPT(function)                     interrupt

   */

/* Parse a C4x pragma, of the form ( function [, "section"] ) \n.
   FUNC is loaded with the IDENTIFIER_NODE of the function, SECT with
   the STRING_CST node of the string.  If SECT is null, then this
   pragma doesn't take a section string.  Returns 0 for a good pragma,
   -1 for a malformed pragma.  */
#define BAD(gmsgid, arg) \
  do { warning (OPT_Wpragmas, gmsgid, arg); return -1; } while (0)

static int
c4x_parse_pragma (name, func, sect)
     const char *name;
     tree *func;
     tree *sect;
{
  tree f, s, x;

  if (pragma_lex (&x) != CPP_OPEN_PAREN)
    BAD ("missing '(' after '#pragma %s' - ignored", name);

  if (pragma_lex (&f) != CPP_NAME)
    BAD ("missing function name in '#pragma %s' - ignored", name);

  if (sect)
    {
      if (pragma_lex (&x) != CPP_COMMA)
	BAD ("malformed '#pragma %s' - ignored", name);
      if (pragma_lex (&s) != CPP_STRING)
	BAD ("missing section name in '#pragma %s' - ignored", name);
      *sect = s;
    }

  if (pragma_lex (&x) != CPP_CLOSE_PAREN)
    BAD ("missing ')' for '#pragma %s' - ignored", name);

  if (pragma_lex (&x) != CPP_EOF)
    warning (OPT_Wpragmas, "junk at end of '#pragma %s'", name);

  *func = f;
  return 0;
}

void
c4x_pr_CODE_SECTION (pfile)
     cpp_reader *pfile ATTRIBUTE_UNUSED;
{
  tree func, sect;

  if (c4x_parse_pragma ("CODE_SECTION", &func, &sect))
    return;
  code_tree = chainon (code_tree,
		       build_tree_list (func,
					build_tree_list (NULL_TREE, sect)));
}

void
c4x_pr_DATA_SECTION (pfile)
     cpp_reader *pfile ATTRIBUTE_UNUSED;
{
  tree func, sect;

  if (c4x_parse_pragma ("DATA_SECTION", &func, &sect))
    return;
  data_tree = chainon (data_tree,
		       build_tree_list (func,
					build_tree_list (NULL_TREE, sect)));
}

void
c4x_pr_FUNC_IS_PURE (pfile)
     cpp_reader *pfile ATTRIBUTE_UNUSED;
{
  tree func;

  if (c4x_parse_pragma ("FUNC_IS_PURE", &func, 0))
    return;
  pure_tree = chainon (pure_tree, build_tree_list (func, NULL_TREE));
}

void
c4x_pr_FUNC_NEVER_RETURNS (pfile)
     cpp_reader *pfile ATTRIBUTE_UNUSED;
{
  tree func;

  if (c4x_parse_pragma ("FUNC_NEVER_RETURNS", &func, 0))
    return;
  noreturn_tree = chainon (noreturn_tree, build_tree_list (func, NULL_TREE));
}

void
c4x_pr_INTERRUPT (pfile)
     cpp_reader *pfile ATTRIBUTE_UNUSED;
{
  tree func;

  if (c4x_parse_pragma ("INTERRUPT", &func, 0))
    return;
  interrupt_tree = chainon (interrupt_tree, build_tree_list (func, NULL_TREE));
}

/* Used for FUNC_CANNOT_INLINE, FUNC_EXT_CALLED, FUNC_IS_SYSTEM,
   FUNC_NO_GLOBAL_ASG, and FUNC_NO_IND_ASG.  */
void
c4x_pr_ignored (pfile)
     cpp_reader *pfile ATTRIBUTE_UNUSED;
{
}
