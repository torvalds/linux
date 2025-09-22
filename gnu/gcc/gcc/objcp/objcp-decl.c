/* Process the ObjC-specific declarations and variables for 
   the Objective-C++ compiler.
   Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Ziemowit Laski  <zlaski@apple.com>

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "expr.h"
#include "cp-tree.h"
#include "c-common.h"
#include "flags.h"
#include "input.h"
#include "except.h"
#include "output.h"
#include "toplev.h"
#include "cpplib.h"
#include "debug.h"
#include "target.h"
#include "varray.h"

#include "objc-act.h"
#include "objcp-decl.h"

/* Hacks to simulate start_struct() and finish_struct(). */

tree 
objcp_start_struct (enum tree_code code ATTRIBUTE_UNUSED, tree name)
{
  tree s;
  /* The idea here is to mimic the actions that the C++ parser takes when
     constructing 'extern "C" struct NAME {'.  */
  push_lang_context (lang_name_c);

  if (!name)
    name = make_anon_name ();

  s = xref_tag (record_type, name, ts_global, 0);
  CLASSTYPE_DECLARED_CLASS (s) = 0;  /* this is a 'struct', not a 'class'.  */
  xref_basetypes (s, NULL_TREE);     /* no base classes here!  */

  return begin_class_definition (s, NULL_TREE);
}

tree 
objcp_finish_struct (tree t, tree fieldlist, tree attributes)
{
  tree field, next_field;

  for (field = fieldlist; field; field = next_field)
  {
    next_field = TREE_CHAIN (field);      /* insert one field at a time;  */
    TREE_CHAIN (field) = NULL_TREE;       /* otherwise, grokfield croaks. */
    finish_member_declaration (field);
  }
  t = finish_struct (t, attributes);
  pop_lang_context ();

  return t;
}

void
objcp_finish_function (void)
{
  /* The C++ flavor of 'finish_function' does not generate RTL -- one has
     to call 'expand_or_defer_fn' to do that.  */
  expand_or_defer_fn (finish_function (0));
}

tree
objcp_xref_tag (enum tree_code code ATTRIBUTE_UNUSED, tree name)
{
  return xref_tag (record_type, name, ts_global, false);
}

int
objcp_comptypes (tree type1, tree type2)
{     
  return comptypes (type1, type2, COMPARE_STRICT);
}

tree
objcp_begin_compound_stmt (int flags ATTRIBUTE_UNUSED)
{
  return begin_compound_stmt (0);
}

tree
objcp_end_compound_stmt (tree stmt, int flags ATTRIBUTE_UNUSED)
{
  /* The following has been snarfed from
     cp/semantics.c:finish_compound_stmt().  */
  if (TREE_CODE (stmt) == BIND_EXPR)
    BIND_EXPR_BODY (stmt) = do_poplevel (BIND_EXPR_BODY (stmt));
  else if (STATEMENT_LIST_NO_SCOPE (stmt))
    stmt = pop_stmt_list (stmt);
  else
    stmt = do_poplevel (stmt);

  return stmt;
}
