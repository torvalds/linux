/* Some code common to C++ and ObjC++ front ends.
   Copyright (C) 2004 Free Software Foundation, Inc.
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
#include "cp-tree.h"
#include "c-common.h"
#include "toplev.h"
#include "langhooks.h"
#include "langhooks-def.h"
#include "diagnostic.h"
#include "debug.h"
#include "cxx-pretty-print.h"
#include "cp-objcp-common.h"

/* Special routine to get the alias set for C++.  */

HOST_WIDE_INT
cxx_get_alias_set (tree t)
{
  if (IS_FAKE_BASE_TYPE (t))
    /* The base variant of a type must be in the same alias set as the
       complete type.  */
    return get_alias_set (TYPE_CONTEXT (t));

  /* Punt on PMFs until we canonicalize functions properly.  */
  if (TYPE_PTRMEMFUNC_P (t))
    return 0;

  return c_common_get_alias_set (t);
}

/* Called from check_global_declarations.  */

bool
cxx_warn_unused_global_decl (tree decl)
{
  if (TREE_CODE (decl) == FUNCTION_DECL && DECL_DECLARED_INLINE_P (decl))
    return false;
  if (DECL_IN_SYSTEM_HEADER (decl))
    return false;

  /* Const variables take the place of #defines in C++.  */
  if (TREE_CODE (decl) == VAR_DECL && TREE_READONLY (decl))
    return false;

  return true;
}

/* Langhook for expr_size: Tell the backend that the value of an expression
   of non-POD class type does not include any tail padding; a derived class
   might have allocated something there.  */

tree
cp_expr_size (tree exp)
{
  tree type = TREE_TYPE (exp);

  if (CLASS_TYPE_P (type))
    {
      /* The backend should not be interested in the size of an expression
	 of a type with both of these set; all copies of such types must go
	 through a constructor or assignment op.  */
      gcc_assert (!TYPE_HAS_COMPLEX_INIT_REF (type)
		  || !TYPE_HAS_COMPLEX_ASSIGN_REF (type)
		  /* But storing a CONSTRUCTOR isn't a copy.  */
		  || TREE_CODE (exp) == CONSTRUCTOR
		  /* And, the gimplifier will sometimes make a copy of
		     an aggregate.  In particular, for a case like:

			struct S { S(); };
			struct X { int a; S s; };
			X x = { 0 };

		     the gimplifier will create a temporary with
		     static storage duration, perform static
		     initialization of the temporary, and then copy
		     the result.  Since the "s" subobject is never
		     constructed, this is a valid transformation.  */
		  || CP_AGGREGATE_TYPE_P (type));

      /* This would be wrong for a type with virtual bases, but they are
	 caught by the assert above.  */
      return (is_empty_class (type)
	      ? size_zero_node
	      : CLASSTYPE_SIZE_UNIT (type));
    }
  else
    /* Use the default code.  */
    return lhd_expr_size (exp);
}

/* Langhook for tree_size: determine size of our 'x' and 'c' nodes.  */
size_t
cp_tree_size (enum tree_code code)
{
  switch (code)
    {
    case TINST_LEVEL:		return sizeof (struct tinst_level_s);
    case PTRMEM_CST:		return sizeof (struct ptrmem_cst);
    case BASELINK:		return sizeof (struct tree_baselink);
    case TEMPLATE_PARM_INDEX:	return sizeof (template_parm_index);
    case DEFAULT_ARG:		return sizeof (struct tree_default_arg);
    case OVERLOAD:		return sizeof (struct tree_overload);
    default:
      gcc_unreachable ();
    }
  /* NOTREACHED */
}

/* Returns true if T is a variably modified type, in the sense of C99.
   FN is as passed to variably_modified_p.
   This routine needs only check cases that cannot be handled by the
   language-independent logic in tree.c.  */

bool
cp_var_mod_type_p (tree type, tree fn)
{
  /* If TYPE is a pointer-to-member, it is variably modified if either
     the class or the member are variably modified.  */
  if (TYPE_PTR_TO_MEMBER_P (type))
    return (variably_modified_type_p (TYPE_PTRMEM_CLASS_TYPE (type), fn)
	    || variably_modified_type_p (TYPE_PTRMEM_POINTED_TO_TYPE (type),
					 fn));

  /* All other types are not variably modified.  */
  return false;
}

/* Construct a C++-aware pretty-printer for CONTEXT.  It is assumed
   that CONTEXT->printer is an already constructed basic pretty_printer.  */
void
cxx_initialize_diagnostics (diagnostic_context *context)
{
  pretty_printer *base = context->printer;
  cxx_pretty_printer *pp = XNEW (cxx_pretty_printer);
  memcpy (pp_base (pp), base, sizeof (pretty_printer));
  pp_cxx_pretty_printer_init (pp);
  context->printer = (pretty_printer *) pp;

  /* It is safe to free this object because it was previously malloc()'d.  */
  free (base);
}

/* This compares two types for equivalence ("compatible" in C-based languages).
   This routine should only return 1 if it is sure.  It should not be used
   in contexts where erroneously returning 0 causes problems.  */

int
cxx_types_compatible_p (tree x, tree y)
{
  if (same_type_ignoring_top_level_qualifiers_p (x, y))
    return 1;

  /* Once we get to the middle-end, references and pointers are
     interchangeable.  FIXME should we try to replace all references with
     pointers?  */
  if (POINTER_TYPE_P (x) && POINTER_TYPE_P (y)
      && TYPE_MODE (x) == TYPE_MODE (y)
      && TYPE_REF_CAN_ALIAS_ALL (x) == TYPE_REF_CAN_ALIAS_ALL (y)
      && same_type_p (TREE_TYPE (x), TREE_TYPE (y)))
    return 1;

  return 0;
}

tree
cxx_staticp (tree arg)
{
  switch (TREE_CODE (arg))
    {
    case BASELINK:
      return staticp (BASELINK_FUNCTIONS (arg));

    default:
      break;
    }
  
  return NULL_TREE;
}

/* Stubs to keep c-opts.c happy.  */
void
push_file_scope (void)
{
}

void
pop_file_scope (void)
{
}

/* c-pragma.c needs to query whether a decl has extern "C" linkage.  */
bool
has_c_linkage (tree decl)
{
  return DECL_EXTERN_C_P (decl);
}

static GTY ((if_marked ("tree_map_marked_p"), param_is (struct tree_map)))
     htab_t shadowed_var_for_decl;

/* Lookup a shadowed var for FROM, and return it if we find one.  */

tree
decl_shadowed_for_var_lookup (tree from)
{
  struct tree_map *h, in;
  in.from = from;

  h = (struct tree_map *) htab_find_with_hash (shadowed_var_for_decl, &in,
					       htab_hash_pointer (from));
  if (h)
    return h->to;
  return NULL_TREE;
}

/* Insert a mapping FROM->TO in the shadowed var hashtable.  */

void
decl_shadowed_for_var_insert (tree from, tree to)
{
  struct tree_map *h;
  void **loc;

  h = GGC_NEW (struct tree_map);
  h->hash = htab_hash_pointer (from);
  h->from = from;
  h->to = to;
  loc = htab_find_slot_with_hash (shadowed_var_for_decl, h, h->hash, INSERT);
  *(struct tree_map **) loc = h;
}

void
init_shadowed_var_for_decl (void)
{
  shadowed_var_for_decl = htab_create_ggc (512, tree_map_hash,
					   tree_map_eq, 0);
}


#include "gt-cp-cp-objcp-common.h"
