/* Pragma handling for GCC for Renesas / SuperH SH.
   Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
   2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Joern Rennecke <joern.rennecke@st.com>.

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
#include "tm_p.h"

/* Handle machine specific pragmas to be semi-compatible with Renesas
   compiler.  */

/* Add ATTR to the attributes of the current function.  If there is no
   such function, save it to be added to the attributes of the next
   function.  */
static void
sh_add_function_attribute (const char *attr)
{
  tree id = get_identifier (attr);

  if (current_function_decl)
    decl_attributes (&current_function_decl,
		     tree_cons (id, NULL_TREE, NULL_TREE), 0);
  else
    {
      *sh_deferred_function_attributes_tail
	= tree_cons (id, NULL_TREE, *sh_deferred_function_attributes_tail);
      sh_deferred_function_attributes_tail
	= &TREE_CHAIN (*sh_deferred_function_attributes_tail);
    }
}

void
sh_pr_interrupt (struct cpp_reader *pfile ATTRIBUTE_UNUSED)
{
  sh_add_function_attribute ("interrupt_handler");
}

void
sh_pr_trapa (struct cpp_reader *pfile ATTRIBUTE_UNUSED)
{
  sh_add_function_attribute ("trapa_handler");
}

void
sh_pr_nosave_low_regs (struct cpp_reader *pfile ATTRIBUTE_UNUSED)
{
  sh_add_function_attribute ("nosave_low_regs");
}
