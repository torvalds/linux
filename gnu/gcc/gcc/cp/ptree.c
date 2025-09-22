/* Prints out trees in human readable form.
   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Hacked by Michael Tiemann (tiemann@cygnus.com)

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
#include "cp-tree.h"

void
cxx_print_decl (FILE *file, tree node, int indent)
{
  if (TREE_CODE (node) == FIELD_DECL)
    {
      if (DECL_MUTABLE_P (node))
	{
	  indent_to (file, indent + 3);
	  fprintf (file, " mutable ");
	}
      return;
    }

  if (!CODE_CONTAINS_STRUCT (TREE_CODE (node), TS_DECL_COMMON)
      || !DECL_LANG_SPECIFIC (node))
    return;
  indent_to (file, indent + 3);
  if (TREE_CODE (node) == FUNCTION_DECL
      && DECL_PENDING_INLINE_INFO (node))
    fprintf (file, " pending-inline-info %p",
	     (void *) DECL_PENDING_INLINE_INFO (node));
  if (TREE_CODE (node) == TYPE_DECL
      && DECL_SORTED_FIELDS (node))
    fprintf (file, " sorted-fields %p",
	     (void *) DECL_SORTED_FIELDS (node));
  if ((TREE_CODE (node) == FUNCTION_DECL || TREE_CODE (node) == VAR_DECL)
      && DECL_TEMPLATE_INFO (node))
    fprintf (file, " template-info %p",
	     (void *) DECL_TEMPLATE_INFO (node));
}

void
cxx_print_type (FILE *file, tree node, int indent)
{
  switch (TREE_CODE (node))
    {
    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_TEMPLATE_PARM:
    case BOUND_TEMPLATE_TEMPLATE_PARM:
      indent_to (file, indent + 3);
      fprintf (file, "index " HOST_WIDE_INT_PRINT_DEC " level "
	       HOST_WIDE_INT_PRINT_DEC " orig_level " HOST_WIDE_INT_PRINT_DEC,
	       TEMPLATE_TYPE_IDX (node), TEMPLATE_TYPE_LEVEL (node),
	       TEMPLATE_TYPE_ORIG_LEVEL (node));
      return;

    case FUNCTION_TYPE:
    case METHOD_TYPE:
      if (TYPE_RAISES_EXCEPTIONS (node))
	print_node (file, "throws", TYPE_RAISES_EXCEPTIONS (node), indent + 4);
      return;

    case RECORD_TYPE:
    case UNION_TYPE:
      break;

    default:
      return;
    }

  if (TYPE_PTRMEMFUNC_P (node))
    print_node (file, "ptrmemfunc fn type", TYPE_PTRMEMFUNC_FN_TYPE (node),
		indent + 4);

  if (! CLASS_TYPE_P (node))
    return;

  indent_to (file, indent + 3);

  if (TYPE_NEEDS_CONSTRUCTING (node))
    fputs ( "needs-constructor", file);
  if (TYPE_HAS_NONTRIVIAL_DESTRUCTOR (node))
    fputs (" needs-destructor", file);
  if (TYPE_HAS_DEFAULT_CONSTRUCTOR (node))
    fputs (" X()", file);
  if (TYPE_HAS_CONVERSION (node))
    fputs (" has-type-conversion", file);
  if (TYPE_HAS_INIT_REF (node))
    {
      if (TYPE_HAS_CONST_INIT_REF (node))
	fputs (" X(constX&)", file);
      else
	fputs (" X(X&)", file);
    }
  if (TYPE_HAS_NEW_OPERATOR (node))
    fputs (" new", file);
  if (TYPE_HAS_ARRAY_NEW_OPERATOR (node))
    fputs (" new[]", file);
  if (TYPE_GETS_DELETE (node) & 1)
    fputs (" delete", file);
  if (TYPE_GETS_DELETE (node) & 2)
    fputs (" delete[]", file);
  if (TYPE_HAS_ASSIGN_REF (node))
    fputs (" this=(X&)", file);

  if (TREE_CODE (node) == RECORD_TYPE)
    {
      if (TYPE_BINFO (node))
	fprintf (file, " n_parents=%d",
		 BINFO_N_BASE_BINFOS (TYPE_BINFO (node)));
      else
	fprintf (file, " no-binfo");

      fprintf (file, " use_template=%d", CLASSTYPE_USE_TEMPLATE (node));
      if (CLASSTYPE_INTERFACE_ONLY (node))
	fprintf (file, " interface-only");
      if (CLASSTYPE_INTERFACE_UNKNOWN (node))
	fprintf (file, " interface-unknown");
    }
}


static void
cxx_print_binding (FILE *stream, cxx_binding *binding, const char *prefix)
{
  fprintf (stream, "%s <%p>",
	   prefix, (void *) binding);
}

void
cxx_print_identifier (FILE *file, tree node, int indent)
{
  if (indent == 0)
    fprintf (file, " ");
  else
    indent_to (file, indent);
  cxx_print_binding (file, IDENTIFIER_NAMESPACE_BINDINGS (node), "bindings");
  if (indent == 0)
    fprintf (file, " ");
  else
    indent_to (file, indent);
  cxx_print_binding (file, IDENTIFIER_BINDING (node), "local bindings");
  print_node (file, "label", IDENTIFIER_LABEL_VALUE (node), indent + 4);
  print_node (file, "template", IDENTIFIER_TEMPLATE (node), indent + 4);
}

void
cxx_print_xnode (FILE *file, tree node, int indent)
{
  switch (TREE_CODE (node))
    {
    case BASELINK:
      print_node (file, "functions", BASELINK_FUNCTIONS (node), indent + 4);
      print_node (file, "binfo", BASELINK_BINFO (node), indent + 4);
      print_node (file, "access_binfo", BASELINK_ACCESS_BINFO (node),
		  indent + 4);
      break;
    case OVERLOAD:
      print_node (file, "function", OVL_FUNCTION (node), indent+4);
      print_node (file, "chain", TREE_CHAIN (node), indent+4);
      break;
    case TEMPLATE_PARM_INDEX:
      indent_to (file, indent + 3);
      fprintf (file, "index " HOST_WIDE_INT_PRINT_DEC " level "
	       HOST_WIDE_INT_PRINT_DEC " orig_level " HOST_WIDE_INT_PRINT_DEC,
	       TEMPLATE_PARM_IDX (node), TEMPLATE_PARM_LEVEL (node),
	       TEMPLATE_PARM_ORIG_LEVEL (node));
      break;
    default:
      break;
    }
}
