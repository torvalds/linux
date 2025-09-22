/* This file contains the definitions and documentation for the common
   tree codes used in the GNU C and C++ compilers (see c-common.def
   for the standard codes).
   Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.
   Written by Benjamin Chelf (chelf@codesourcery.com).

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
#include "function.h"
#include "splay-tree.h"
#include "varray.h"
#include "c-common.h"
#include "except.h"
/* In order for the format checking to accept the C frontend
   diagnostic framework extensions, you must define this token before
   including toplev.h.  */
#define GCC_DIAG_STYLE __gcc_cdiag__
#include "toplev.h"
#include "flags.h"
#include "ggc.h"
#include "rtl.h"
#include "output.h"
#include "timevar.h"
#include "predict.h"
#include "tree-inline.h"
#include "tree-gimple.h"
#include "langhooks.h"

/* Create an empty statement tree rooted at T.  */

tree
push_stmt_list (void)
{
  tree t;
  t = alloc_stmt_list ();
  TREE_CHAIN (t) = cur_stmt_list;
  cur_stmt_list = t;
  return t;
}

/* Finish the statement tree rooted at T.  */

tree
pop_stmt_list (tree t)
{
  tree u = cur_stmt_list, chain;

  /* Pop statement lists until we reach the target level.  The extra
     nestings will be due to outstanding cleanups.  */
  while (1)
    {
      chain = TREE_CHAIN (u);
      TREE_CHAIN (u) = NULL_TREE;
      if (t == u)
	break;
      u = chain;
    }
  cur_stmt_list = chain;

  /* If the statement list is completely empty, just return it.  This is
     just as good small as build_empty_stmt, with the advantage that
     statement lists are merged when they appended to one another.  So
     using the STATEMENT_LIST avoids pathological buildup of EMPTY_STMT_P
     statements.  */
  if (TREE_SIDE_EFFECTS (t))
    {
      tree_stmt_iterator i = tsi_start (t);

      /* If the statement list contained exactly one statement, then
	 extract it immediately.  */
      if (tsi_one_before_end_p (i))
	{
	  u = tsi_stmt (i);
	  tsi_delink (&i);
	  free_stmt_list (t);
	  t = u;
	}
    }

  return t;
}

/* Build a generic statement based on the given type of node and
   arguments. Similar to `build_nt', except that we set
   EXPR_LOCATION to be the current source location.  */
/* ??? This should be obsolete with the lineno_stmt productions
   in the grammar.  */

tree
build_stmt (enum tree_code code, ...)
{
  tree ret;
  int length, i;
  va_list p;
  bool side_effects;

  va_start (p, code);

  ret = make_node (code);
  TREE_TYPE (ret) = void_type_node;
  length = TREE_CODE_LENGTH (code);
  SET_EXPR_LOCATION (ret, input_location);

  /* TREE_SIDE_EFFECTS will already be set for statements with
     implicit side effects.  Here we make sure it is set for other
     expressions by checking whether the parameters have side
     effects.  */

  side_effects = false;
  for (i = 0; i < length; i++)
    {
      tree t = va_arg (p, tree);
      if (t && !TYPE_P (t))
	side_effects |= TREE_SIDE_EFFECTS (t);
      TREE_OPERAND (ret, i) = t;
    }

  TREE_SIDE_EFFECTS (ret) |= side_effects;

  va_end (p);
  return ret;
}

/* Let the back-end know about DECL.  */

void
emit_local_var (tree decl)
{
  /* Create RTL for this variable.  */
  if (!DECL_RTL_SET_P (decl))
    {
      if (DECL_HARD_REGISTER (decl))
	/* The user specified an assembler name for this variable.
	   Set that up now.  */
	rest_of_decl_compilation (decl, 0, 0);
      else
	expand_decl (decl);
    }
}

/* Create a CASE_LABEL_EXPR tree node and return it.  */

tree
build_case_label (tree low_value, tree high_value, tree label_decl)
{
  return build_stmt (CASE_LABEL_EXPR, low_value, high_value, label_decl);
}
