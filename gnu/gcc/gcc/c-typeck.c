/* Build expressions with type checking for C compiler.
   Copyright (C) 1987, 1988, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

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


/* This file is part of the C front end.
   It contains routines to build C expressions given their operands,
   including computing the types of the result, C-specific error checks,
   and some optimization.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "langhooks.h"
#include "c-tree.h"
#include "tm_p.h"
#include "flags.h"
#include "output.h"
#include "expr.h"
#include "toplev.h"
#include "intl.h"
#include "ggc.h"
#include "target.h"
#include "tree-iterator.h"
#include "tree-gimple.h"
#include "tree-flow.h"

/* Possible cases of implicit bad conversions.  Used to select
   diagnostic messages in convert_for_assignment.  */
enum impl_conv {
  ic_argpass,
  ic_argpass_nonproto,
  ic_assign,
  ic_init,
  ic_return
};

/* The level of nesting inside "__alignof__".  */
int in_alignof;

/* The level of nesting inside "sizeof".  */
int in_sizeof;

/* The level of nesting inside "typeof".  */
int in_typeof;

struct c_label_context_se *label_context_stack_se;
struct c_label_context_vm *label_context_stack_vm;

/* Nonzero if we've already printed a "missing braces around initializer"
   message within this initializer.  */
static int missing_braces_mentioned;

static int require_constant_value;
static int require_constant_elements;

static bool null_pointer_constant_p (tree);
static tree qualify_type (tree, tree);
static int tagged_types_tu_compatible_p (tree, tree);
static int comp_target_types (tree, tree);
static int function_types_compatible_p (tree, tree);
static int type_lists_compatible_p (tree, tree);
static tree decl_constant_value_for_broken_optimization (tree);
static tree lookup_field (tree, tree);
static tree convert_arguments (tree, tree, tree, tree);
static tree pointer_diff (tree, tree);
static tree convert_for_assignment (tree, tree, enum impl_conv, tree, tree,
				    int);
static tree valid_compound_expr_initializer (tree, tree);
static void push_string (const char *);
static void push_member_name (tree);
static int spelling_length (void);
static char *print_spelling (char *);
static void warning_init (const char *);
static tree digest_init (tree, tree, bool, int);
static void output_init_element (tree, bool, tree, tree, int);
static void output_pending_init_elements (int);
static int set_designator (int);
static void push_range_stack (tree);
static void add_pending_init (tree, tree);
static void set_nonincremental_init (void);
static void set_nonincremental_init_from_string (tree);
static tree find_init_member (tree);
static void readonly_error (tree, enum lvalue_use);
static int lvalue_or_else (tree, enum lvalue_use);
static int lvalue_p (tree);
static void record_maybe_used_decl (tree);
static int comptypes_internal (tree, tree);

/* Return true if EXP is a null pointer constant, false otherwise.  */

static bool
null_pointer_constant_p (tree expr)
{
  /* This should really operate on c_expr structures, but they aren't
     yet available everywhere required.  */
  tree type = TREE_TYPE (expr);
  return (TREE_CODE (expr) == INTEGER_CST
	  && !TREE_CONSTANT_OVERFLOW (expr)
	  && integer_zerop (expr)
	  && (INTEGRAL_TYPE_P (type)
	      || (TREE_CODE (type) == POINTER_TYPE
		  && VOID_TYPE_P (TREE_TYPE (type))
		  && TYPE_QUALS (TREE_TYPE (type)) == TYPE_UNQUALIFIED)));
}
/* This is a cache to hold if two types are compatible or not.  */

struct tagged_tu_seen_cache {
  const struct tagged_tu_seen_cache * next;
  tree t1;
  tree t2;
  /* The return value of tagged_types_tu_compatible_p if we had seen
     these two types already.  */
  int val;
};

static const struct tagged_tu_seen_cache * tagged_tu_seen_base;
static void free_all_tagged_tu_seen_up_to (const struct tagged_tu_seen_cache *);

/* Do `exp = require_complete_type (exp);' to make sure exp
   does not have an incomplete type.  (That includes void types.)  */

tree
require_complete_type (tree value)
{
  tree type = TREE_TYPE (value);

  if (value == error_mark_node || type == error_mark_node)
    return error_mark_node;

  /* First, detect a valid value with a complete type.  */
  if (COMPLETE_TYPE_P (type))
    return value;

  c_incomplete_type_error (value, type);
  return error_mark_node;
}

/* Print an error message for invalid use of an incomplete type.
   VALUE is the expression that was used (or 0 if that isn't known)
   and TYPE is the type that was invalid.  */

void
c_incomplete_type_error (tree value, tree type)
{
  const char *type_code_string;

  /* Avoid duplicate error message.  */
  if (TREE_CODE (type) == ERROR_MARK)
    return;

  if (value != 0 && (TREE_CODE (value) == VAR_DECL
		     || TREE_CODE (value) == PARM_DECL))
    error ("%qD has an incomplete type", value);
  else
    {
    retry:
      /* We must print an error message.  Be clever about what it says.  */

      switch (TREE_CODE (type))
	{
	case RECORD_TYPE:
	  type_code_string = "struct";
	  break;

	case UNION_TYPE:
	  type_code_string = "union";
	  break;

	case ENUMERAL_TYPE:
	  type_code_string = "enum";
	  break;

	case VOID_TYPE:
	  error ("invalid use of void expression");
	  return;

	case ARRAY_TYPE:
	  if (TYPE_DOMAIN (type))
	    {
	      if (TYPE_MAX_VALUE (TYPE_DOMAIN (type)) == NULL)
		{
		  error ("invalid use of flexible array member");
		  return;
		}
	      type = TREE_TYPE (type);
	      goto retry;
	    }
	  error ("invalid use of array with unspecified bounds");
	  return;

	default:
	  gcc_unreachable ();
	}

      if (TREE_CODE (TYPE_NAME (type)) == IDENTIFIER_NODE)
	error ("invalid use of undefined type %<%s %E%>",
	       type_code_string, TYPE_NAME (type));
      else
	/* If this type has a typedef-name, the TYPE_NAME is a TYPE_DECL.  */
	error ("invalid use of incomplete typedef %qD", TYPE_NAME (type));
    }
}

/* Given a type, apply default promotions wrt unnamed function
   arguments and return the new type.  */

tree
c_type_promotes_to (tree type)
{
  if (TYPE_MAIN_VARIANT (type) == float_type_node)
    return double_type_node;

  if (c_promoting_integer_type_p (type))
    {
      /* Preserve unsignedness if not really getting any wider.  */
      if (TYPE_UNSIGNED (type)
	  && (TYPE_PRECISION (type) == TYPE_PRECISION (integer_type_node)))
	return unsigned_type_node;
      return integer_type_node;
    }

  return type;
}

/* Return a variant of TYPE which has all the type qualifiers of LIKE
   as well as those of TYPE.  */

static tree
qualify_type (tree type, tree like)
{
  return c_build_qualified_type (type,
				 TYPE_QUALS (type) | TYPE_QUALS (like));
}

/* Return true iff the given tree T is a variable length array.  */

bool
c_vla_type_p (tree t)
{
  if (TREE_CODE (t) == ARRAY_TYPE
      && C_TYPE_VARIABLE_SIZE (t))
    return true;
  return false;
}

/* Return the composite type of two compatible types.

   We assume that comptypes has already been done and returned
   nonzero; if that isn't so, this may crash.  In particular, we
   assume that qualifiers match.  */

tree
composite_type (tree t1, tree t2)
{
  enum tree_code code1;
  enum tree_code code2;
  tree attributes;

  /* Save time if the two types are the same.  */

  if (t1 == t2) return t1;

  /* If one type is nonsense, use the other.  */
  if (t1 == error_mark_node)
    return t2;
  if (t2 == error_mark_node)
    return t1;

  code1 = TREE_CODE (t1);
  code2 = TREE_CODE (t2);

  /* Merge the attributes.  */
  attributes = targetm.merge_type_attributes (t1, t2);

  /* If one is an enumerated type and the other is the compatible
     integer type, the composite type might be either of the two
     (DR#013 question 3).  For consistency, use the enumerated type as
     the composite type.  */

  if (code1 == ENUMERAL_TYPE && code2 == INTEGER_TYPE)
    return t1;
  if (code2 == ENUMERAL_TYPE && code1 == INTEGER_TYPE)
    return t2;

  gcc_assert (code1 == code2);

  switch (code1)
    {
    case POINTER_TYPE:
      /* For two pointers, do this recursively on the target type.  */
      {
	tree pointed_to_1 = TREE_TYPE (t1);
	tree pointed_to_2 = TREE_TYPE (t2);
	tree target = composite_type (pointed_to_1, pointed_to_2);
	t1 = build_pointer_type (target);
	t1 = build_type_attribute_variant (t1, attributes);
	return qualify_type (t1, t2);
      }

    case ARRAY_TYPE:
      {
	tree elt = composite_type (TREE_TYPE (t1), TREE_TYPE (t2));
	int quals;
	tree unqual_elt;
	tree d1 = TYPE_DOMAIN (t1);
	tree d2 = TYPE_DOMAIN (t2);
	bool d1_variable, d2_variable;
	bool d1_zero, d2_zero;

	/* We should not have any type quals on arrays at all.  */
	gcc_assert (!TYPE_QUALS (t1) && !TYPE_QUALS (t2));

	d1_zero = d1 == 0 || !TYPE_MAX_VALUE (d1);
	d2_zero = d2 == 0 || !TYPE_MAX_VALUE (d2);

	d1_variable = (!d1_zero
		       && (TREE_CODE (TYPE_MIN_VALUE (d1)) != INTEGER_CST
			   || TREE_CODE (TYPE_MAX_VALUE (d1)) != INTEGER_CST));
	d2_variable = (!d2_zero
		       && (TREE_CODE (TYPE_MIN_VALUE (d2)) != INTEGER_CST
			   || TREE_CODE (TYPE_MAX_VALUE (d2)) != INTEGER_CST));
	d1_variable = d1_variable || (d1_zero && c_vla_type_p (t1));
	d2_variable = d2_variable || (d2_zero && c_vla_type_p (t2));

	/* Save space: see if the result is identical to one of the args.  */
	if (elt == TREE_TYPE (t1) && TYPE_DOMAIN (t1)
	    && (d2_variable || d2_zero || !d1_variable))
	  return build_type_attribute_variant (t1, attributes);
	if (elt == TREE_TYPE (t2) && TYPE_DOMAIN (t2)
	    && (d1_variable || d1_zero || !d2_variable))
	  return build_type_attribute_variant (t2, attributes);

	if (elt == TREE_TYPE (t1) && !TYPE_DOMAIN (t2) && !TYPE_DOMAIN (t1))
	  return build_type_attribute_variant (t1, attributes);
	if (elt == TREE_TYPE (t2) && !TYPE_DOMAIN (t2) && !TYPE_DOMAIN (t1))
	  return build_type_attribute_variant (t2, attributes);

	/* Merge the element types, and have a size if either arg has
	   one.  We may have qualifiers on the element types.  To set
	   up TYPE_MAIN_VARIANT correctly, we need to form the
	   composite of the unqualified types and add the qualifiers
	   back at the end.  */
	quals = TYPE_QUALS (strip_array_types (elt));
	unqual_elt = c_build_qualified_type (elt, TYPE_UNQUALIFIED);
	t1 = build_array_type (unqual_elt,
			       TYPE_DOMAIN ((TYPE_DOMAIN (t1)
					     && (d2_variable
						 || d2_zero
						 || !d1_variable))
					    ? t1
					    : t2));
	t1 = c_build_qualified_type (t1, quals);
	return build_type_attribute_variant (t1, attributes);
      }

    case ENUMERAL_TYPE:
    case RECORD_TYPE:
    case UNION_TYPE:
      if (attributes != NULL)
	{
	  /* Try harder not to create a new aggregate type.  */
	  if (attribute_list_equal (TYPE_ATTRIBUTES (t1), attributes))
	    return t1;
	  if (attribute_list_equal (TYPE_ATTRIBUTES (t2), attributes))
	    return t2;
	}
      return build_type_attribute_variant (t1, attributes);

    case FUNCTION_TYPE:
      /* Function types: prefer the one that specified arg types.
	 If both do, merge the arg types.  Also merge the return types.  */
      {
	tree valtype = composite_type (TREE_TYPE (t1), TREE_TYPE (t2));
	tree p1 = TYPE_ARG_TYPES (t1);
	tree p2 = TYPE_ARG_TYPES (t2);
	int len;
	tree newargs, n;
	int i;

	/* Save space: see if the result is identical to one of the args.  */
	if (valtype == TREE_TYPE (t1) && !TYPE_ARG_TYPES (t2))
	  return build_type_attribute_variant (t1, attributes);
	if (valtype == TREE_TYPE (t2) && !TYPE_ARG_TYPES (t1))
	  return build_type_attribute_variant (t2, attributes);

	/* Simple way if one arg fails to specify argument types.  */
	if (TYPE_ARG_TYPES (t1) == 0)
	 {
	    t1 = build_function_type (valtype, TYPE_ARG_TYPES (t2));
	    t1 = build_type_attribute_variant (t1, attributes);
	    return qualify_type (t1, t2);
	 }
	if (TYPE_ARG_TYPES (t2) == 0)
	 {
	   t1 = build_function_type (valtype, TYPE_ARG_TYPES (t1));
	   t1 = build_type_attribute_variant (t1, attributes);
	   return qualify_type (t1, t2);
	 }

	/* If both args specify argument types, we must merge the two
	   lists, argument by argument.  */
	/* Tell global_bindings_p to return false so that variable_size
	   doesn't die on VLAs in parameter types.  */
	c_override_global_bindings_to_false = true;

	len = list_length (p1);
	newargs = 0;

	for (i = 0; i < len; i++)
	  newargs = tree_cons (NULL_TREE, NULL_TREE, newargs);

	n = newargs;

	for (; p1;
	     p1 = TREE_CHAIN (p1), p2 = TREE_CHAIN (p2), n = TREE_CHAIN (n))
	  {
	    /* A null type means arg type is not specified.
	       Take whatever the other function type has.  */
	    if (TREE_VALUE (p1) == 0)
	      {
		TREE_VALUE (n) = TREE_VALUE (p2);
		goto parm_done;
	      }
	    if (TREE_VALUE (p2) == 0)
	      {
		TREE_VALUE (n) = TREE_VALUE (p1);
		goto parm_done;
	      }

	    /* Given  wait (union {union wait *u; int *i} *)
	       and  wait (union wait *),
	       prefer  union wait *  as type of parm.  */
	    if (TREE_CODE (TREE_VALUE (p1)) == UNION_TYPE
		&& TREE_VALUE (p1) != TREE_VALUE (p2))
	      {
		tree memb;
		tree mv2 = TREE_VALUE (p2);
		if (mv2 && mv2 != error_mark_node
		    && TREE_CODE (mv2) != ARRAY_TYPE)
		  mv2 = TYPE_MAIN_VARIANT (mv2);
		for (memb = TYPE_FIELDS (TREE_VALUE (p1));
		     memb; memb = TREE_CHAIN (memb))
		  {
		    tree mv3 = TREE_TYPE (memb);
		    if (mv3 && mv3 != error_mark_node
			&& TREE_CODE (mv3) != ARRAY_TYPE)
		      mv3 = TYPE_MAIN_VARIANT (mv3);
		    if (comptypes (mv3, mv2))
		      {
			TREE_VALUE (n) = composite_type (TREE_TYPE (memb),
							 TREE_VALUE (p2));
			if (pedantic)
			  pedwarn ("function types not truly compatible in ISO C");
			goto parm_done;
		      }
		  }
	      }
	    if (TREE_CODE (TREE_VALUE (p2)) == UNION_TYPE
		&& TREE_VALUE (p2) != TREE_VALUE (p1))
	      {
		tree memb;
		tree mv1 = TREE_VALUE (p1);
		if (mv1 && mv1 != error_mark_node
		    && TREE_CODE (mv1) != ARRAY_TYPE)
		  mv1 = TYPE_MAIN_VARIANT (mv1);
		for (memb = TYPE_FIELDS (TREE_VALUE (p2));
		     memb; memb = TREE_CHAIN (memb))
		  {
		    tree mv3 = TREE_TYPE (memb);
		    if (mv3 && mv3 != error_mark_node
			&& TREE_CODE (mv3) != ARRAY_TYPE)
		      mv3 = TYPE_MAIN_VARIANT (mv3);
		    if (comptypes (mv3, mv1))
		      {
			TREE_VALUE (n) = composite_type (TREE_TYPE (memb),
							 TREE_VALUE (p1));
			if (pedantic)
			  pedwarn ("function types not truly compatible in ISO C");
			goto parm_done;
		      }
		  }
	      }
	    TREE_VALUE (n) = composite_type (TREE_VALUE (p1), TREE_VALUE (p2));
	  parm_done: ;
	  }

	c_override_global_bindings_to_false = false;
	t1 = build_function_type (valtype, newargs);
	t1 = qualify_type (t1, t2);
	/* ... falls through ...  */
      }

    default:
      return build_type_attribute_variant (t1, attributes);
    }

}

/* Return the type of a conditional expression between pointers to
   possibly differently qualified versions of compatible types.

   We assume that comp_target_types has already been done and returned
   nonzero; if that isn't so, this may crash.  */

static tree
common_pointer_type (tree t1, tree t2)
{
  tree attributes;
  tree pointed_to_1, mv1;
  tree pointed_to_2, mv2;
  tree target;

  /* Save time if the two types are the same.  */

  if (t1 == t2) return t1;

  /* If one type is nonsense, use the other.  */
  if (t1 == error_mark_node)
    return t2;
  if (t2 == error_mark_node)
    return t1;

  gcc_assert (TREE_CODE (t1) == POINTER_TYPE
	      && TREE_CODE (t2) == POINTER_TYPE);

  /* Merge the attributes.  */
  attributes = targetm.merge_type_attributes (t1, t2);

  /* Find the composite type of the target types, and combine the
     qualifiers of the two types' targets.  Do not lose qualifiers on
     array element types by taking the TYPE_MAIN_VARIANT.  */
  mv1 = pointed_to_1 = TREE_TYPE (t1);
  mv2 = pointed_to_2 = TREE_TYPE (t2);
  if (TREE_CODE (mv1) != ARRAY_TYPE)
    mv1 = TYPE_MAIN_VARIANT (pointed_to_1);
  if (TREE_CODE (mv2) != ARRAY_TYPE)
    mv2 = TYPE_MAIN_VARIANT (pointed_to_2);
  target = composite_type (mv1, mv2);
  t1 = build_pointer_type (c_build_qualified_type
			   (target,
			    TYPE_QUALS (pointed_to_1) |
			    TYPE_QUALS (pointed_to_2)));
  return build_type_attribute_variant (t1, attributes);
}

/* Return the common type for two arithmetic types under the usual
   arithmetic conversions.  The default conversions have already been
   applied, and enumerated types converted to their compatible integer
   types.  The resulting type is unqualified and has no attributes.

   This is the type for the result of most arithmetic operations
   if the operands have the given two types.  */

static tree
c_common_type (tree t1, tree t2)
{
  enum tree_code code1;
  enum tree_code code2;

  /* If one type is nonsense, use the other.  */
  if (t1 == error_mark_node)
    return t2;
  if (t2 == error_mark_node)
    return t1;

  if (TYPE_QUALS (t1) != TYPE_UNQUALIFIED)
    t1 = TYPE_MAIN_VARIANT (t1);

  if (TYPE_QUALS (t2) != TYPE_UNQUALIFIED)
    t2 = TYPE_MAIN_VARIANT (t2);

  if (TYPE_ATTRIBUTES (t1) != NULL_TREE)
    t1 = build_type_attribute_variant (t1, NULL_TREE);

  if (TYPE_ATTRIBUTES (t2) != NULL_TREE)
    t2 = build_type_attribute_variant (t2, NULL_TREE);

  /* Save time if the two types are the same.  */

  if (t1 == t2) return t1;

  code1 = TREE_CODE (t1);
  code2 = TREE_CODE (t2);

  gcc_assert (code1 == VECTOR_TYPE || code1 == COMPLEX_TYPE
	      || code1 == REAL_TYPE || code1 == INTEGER_TYPE);
  gcc_assert (code2 == VECTOR_TYPE || code2 == COMPLEX_TYPE
	      || code2 == REAL_TYPE || code2 == INTEGER_TYPE);

  /* When one operand is a decimal float type, the other operand cannot be
     a generic float type or a complex type.  We also disallow vector types
     here.  */
  if ((DECIMAL_FLOAT_TYPE_P (t1) || DECIMAL_FLOAT_TYPE_P (t2))
      && !(DECIMAL_FLOAT_TYPE_P (t1) && DECIMAL_FLOAT_TYPE_P (t2)))
    {
      if (code1 == VECTOR_TYPE || code2 == VECTOR_TYPE)
	{
	  error ("can%'t mix operands of decimal float and vector types");
	  return error_mark_node;
	}
      if (code1 == COMPLEX_TYPE || code2 == COMPLEX_TYPE)
	{
	  error ("can%'t mix operands of decimal float and complex types");
	  return error_mark_node;
	}
      if (code1 == REAL_TYPE && code2 == REAL_TYPE)
	{
	  error ("can%'t mix operands of decimal float and other float types");
	  return error_mark_node;
	}
    }

  /* If one type is a vector type, return that type.  (How the usual
     arithmetic conversions apply to the vector types extension is not
     precisely specified.)  */
  if (code1 == VECTOR_TYPE)
    return t1;

  if (code2 == VECTOR_TYPE)
    return t2;

  /* If one type is complex, form the common type of the non-complex
     components, then make that complex.  Use T1 or T2 if it is the
     required type.  */
  if (code1 == COMPLEX_TYPE || code2 == COMPLEX_TYPE)
    {
      tree subtype1 = code1 == COMPLEX_TYPE ? TREE_TYPE (t1) : t1;
      tree subtype2 = code2 == COMPLEX_TYPE ? TREE_TYPE (t2) : t2;
      tree subtype = c_common_type (subtype1, subtype2);

      if (code1 == COMPLEX_TYPE && TREE_TYPE (t1) == subtype)
	return t1;
      else if (code2 == COMPLEX_TYPE && TREE_TYPE (t2) == subtype)
	return t2;
      else
	return build_complex_type (subtype);
    }

  /* If only one is real, use it as the result.  */

  if (code1 == REAL_TYPE && code2 != REAL_TYPE)
    return t1;

  if (code2 == REAL_TYPE && code1 != REAL_TYPE)
    return t2;

  /* If both are real and either are decimal floating point types, use
     the decimal floating point type with the greater precision. */

  if (code1 == REAL_TYPE && code2 == REAL_TYPE)
    {
      if (TYPE_MAIN_VARIANT (t1) == dfloat128_type_node
	  || TYPE_MAIN_VARIANT (t2) == dfloat128_type_node)
	return dfloat128_type_node;
      else if (TYPE_MAIN_VARIANT (t1) == dfloat64_type_node
	       || TYPE_MAIN_VARIANT (t2) == dfloat64_type_node)
	return dfloat64_type_node;
      else if (TYPE_MAIN_VARIANT (t1) == dfloat32_type_node
	       || TYPE_MAIN_VARIANT (t2) == dfloat32_type_node)
	return dfloat32_type_node;
    }

  /* Both real or both integers; use the one with greater precision.  */

  if (TYPE_PRECISION (t1) > TYPE_PRECISION (t2))
    return t1;
  else if (TYPE_PRECISION (t2) > TYPE_PRECISION (t1))
    return t2;

  /* Same precision.  Prefer long longs to longs to ints when the
     same precision, following the C99 rules on integer type rank
     (which are equivalent to the C90 rules for C90 types).  */

  if (TYPE_MAIN_VARIANT (t1) == long_long_unsigned_type_node
      || TYPE_MAIN_VARIANT (t2) == long_long_unsigned_type_node)
    return long_long_unsigned_type_node;

  if (TYPE_MAIN_VARIANT (t1) == long_long_integer_type_node
      || TYPE_MAIN_VARIANT (t2) == long_long_integer_type_node)
    {
      if (TYPE_UNSIGNED (t1) || TYPE_UNSIGNED (t2))
	return long_long_unsigned_type_node;
      else
	return long_long_integer_type_node;
    }

  if (TYPE_MAIN_VARIANT (t1) == long_unsigned_type_node
      || TYPE_MAIN_VARIANT (t2) == long_unsigned_type_node)
    return long_unsigned_type_node;

  if (TYPE_MAIN_VARIANT (t1) == long_integer_type_node
      || TYPE_MAIN_VARIANT (t2) == long_integer_type_node)
    {
      /* But preserve unsignedness from the other type,
	 since long cannot hold all the values of an unsigned int.  */
      if (TYPE_UNSIGNED (t1) || TYPE_UNSIGNED (t2))
	return long_unsigned_type_node;
      else
	return long_integer_type_node;
    }

  /* Likewise, prefer long double to double even if same size.  */
  if (TYPE_MAIN_VARIANT (t1) == long_double_type_node
      || TYPE_MAIN_VARIANT (t2) == long_double_type_node)
    return long_double_type_node;

  /* Otherwise prefer the unsigned one.  */

  if (TYPE_UNSIGNED (t1))
    return t1;
  else
    return t2;
}

/* Wrapper around c_common_type that is used by c-common.c and other
   front end optimizations that remove promotions.  ENUMERAL_TYPEs
   are allowed here and are converted to their compatible integer types.
   BOOLEAN_TYPEs are allowed here and return either boolean_type_node or
   preferably a non-Boolean type as the common type.  */
tree
common_type (tree t1, tree t2)
{
  if (TREE_CODE (t1) == ENUMERAL_TYPE)
    t1 = c_common_type_for_size (TYPE_PRECISION (t1), 1);
  if (TREE_CODE (t2) == ENUMERAL_TYPE)
    t2 = c_common_type_for_size (TYPE_PRECISION (t2), 1);

  /* If both types are BOOLEAN_TYPE, then return boolean_type_node.  */
  if (TREE_CODE (t1) == BOOLEAN_TYPE
      && TREE_CODE (t2) == BOOLEAN_TYPE)
    return boolean_type_node;

  /* If either type is BOOLEAN_TYPE, then return the other.  */
  if (TREE_CODE (t1) == BOOLEAN_TYPE)
    return t2;
  if (TREE_CODE (t2) == BOOLEAN_TYPE)
    return t1;

  return c_common_type (t1, t2);
}

/* Return 1 if TYPE1 and TYPE2 are compatible types for assignment
   or various other operations.  Return 2 if they are compatible
   but a warning may be needed if you use them together.  */

int
comptypes (tree type1, tree type2)
{
  const struct tagged_tu_seen_cache * tagged_tu_seen_base1 = tagged_tu_seen_base;
  int val;

  val = comptypes_internal (type1, type2);
  free_all_tagged_tu_seen_up_to (tagged_tu_seen_base1);

  return val;
}

/* Return 1 if TYPE1 and TYPE2 are compatible types for assignment
   or various other operations.  Return 2 if they are compatible
   but a warning may be needed if you use them together.  This
   differs from comptypes, in that we don't free the seen types.  */

static int
comptypes_internal (tree type1, tree type2)
{
  tree t1 = type1;
  tree t2 = type2;
  int attrval, val;

  /* Suppress errors caused by previously reported errors.  */

  if (t1 == t2 || !t1 || !t2
      || TREE_CODE (t1) == ERROR_MARK || TREE_CODE (t2) == ERROR_MARK)
    return 1;

  /* If either type is the internal version of sizetype, return the
     language version.  */
  if (TREE_CODE (t1) == INTEGER_TYPE && TYPE_IS_SIZETYPE (t1)
      && TYPE_ORIG_SIZE_TYPE (t1))
    t1 = TYPE_ORIG_SIZE_TYPE (t1);

  if (TREE_CODE (t2) == INTEGER_TYPE && TYPE_IS_SIZETYPE (t2)
      && TYPE_ORIG_SIZE_TYPE (t2))
    t2 = TYPE_ORIG_SIZE_TYPE (t2);


  /* Enumerated types are compatible with integer types, but this is
     not transitive: two enumerated types in the same translation unit
     are compatible with each other only if they are the same type.  */

  if (TREE_CODE (t1) == ENUMERAL_TYPE && TREE_CODE (t2) != ENUMERAL_TYPE)
    t1 = c_common_type_for_size (TYPE_PRECISION (t1), TYPE_UNSIGNED (t1));
  else if (TREE_CODE (t2) == ENUMERAL_TYPE && TREE_CODE (t1) != ENUMERAL_TYPE)
    t2 = c_common_type_for_size (TYPE_PRECISION (t2), TYPE_UNSIGNED (t2));

  if (t1 == t2)
    return 1;

  /* Different classes of types can't be compatible.  */

  if (TREE_CODE (t1) != TREE_CODE (t2))
    return 0;

  /* Qualifiers must match. C99 6.7.3p9 */

  if (TYPE_QUALS (t1) != TYPE_QUALS (t2))
    return 0;

  /* Allow for two different type nodes which have essentially the same
     definition.  Note that we already checked for equality of the type
     qualifiers (just above).  */

  if (TREE_CODE (t1) != ARRAY_TYPE
      && TYPE_MAIN_VARIANT (t1) == TYPE_MAIN_VARIANT (t2))
    return 1;

  /* 1 if no need for warning yet, 2 if warning cause has been seen.  */
  if (!(attrval = targetm.comp_type_attributes (t1, t2)))
     return 0;

  /* 1 if no need for warning yet, 2 if warning cause has been seen.  */
  val = 0;

  switch (TREE_CODE (t1))
    {
    case POINTER_TYPE:
      /* Do not remove mode or aliasing information.  */
      if (TYPE_MODE (t1) != TYPE_MODE (t2)
	  || TYPE_REF_CAN_ALIAS_ALL (t1) != TYPE_REF_CAN_ALIAS_ALL (t2))
	break;
      val = (TREE_TYPE (t1) == TREE_TYPE (t2)
	     ? 1 : comptypes_internal (TREE_TYPE (t1), TREE_TYPE (t2)));
      break;

    case FUNCTION_TYPE:
      val = function_types_compatible_p (t1, t2);
      break;

    case ARRAY_TYPE:
      {
	tree d1 = TYPE_DOMAIN (t1);
	tree d2 = TYPE_DOMAIN (t2);
	bool d1_variable, d2_variable;
	bool d1_zero, d2_zero;
	val = 1;

	/* Target types must match incl. qualifiers.  */
	if (TREE_TYPE (t1) != TREE_TYPE (t2)
	    && 0 == (val = comptypes_internal (TREE_TYPE (t1), TREE_TYPE (t2))))
	  return 0;

	/* Sizes must match unless one is missing or variable.  */
	if (d1 == 0 || d2 == 0 || d1 == d2)
	  break;

	d1_zero = !TYPE_MAX_VALUE (d1);
	d2_zero = !TYPE_MAX_VALUE (d2);

	d1_variable = (!d1_zero
		       && (TREE_CODE (TYPE_MIN_VALUE (d1)) != INTEGER_CST
			   || TREE_CODE (TYPE_MAX_VALUE (d1)) != INTEGER_CST));
	d2_variable = (!d2_zero
		       && (TREE_CODE (TYPE_MIN_VALUE (d2)) != INTEGER_CST
			   || TREE_CODE (TYPE_MAX_VALUE (d2)) != INTEGER_CST));
	d1_variable = d1_variable || (d1_zero && c_vla_type_p (t1));
	d2_variable = d2_variable || (d2_zero && c_vla_type_p (t2));

	if (d1_variable || d2_variable)
	  break;
	if (d1_zero && d2_zero)
	  break;
	if (d1_zero || d2_zero
	    || !tree_int_cst_equal (TYPE_MIN_VALUE (d1), TYPE_MIN_VALUE (d2))
	    || !tree_int_cst_equal (TYPE_MAX_VALUE (d1), TYPE_MAX_VALUE (d2)))
	  val = 0;

	break;
      }

    case ENUMERAL_TYPE:
    case RECORD_TYPE:
    case UNION_TYPE:
      if (val != 1 && !same_translation_unit_p (t1, t2))
	{
	  tree a1 = TYPE_ATTRIBUTES (t1);
	  tree a2 = TYPE_ATTRIBUTES (t2);

	  if (! attribute_list_contained (a1, a2)
	      && ! attribute_list_contained (a2, a1))
	    break;

	  if (attrval != 2)
	    return tagged_types_tu_compatible_p (t1, t2);
	  val = tagged_types_tu_compatible_p (t1, t2);
	}
      break;

    case VECTOR_TYPE:
      val = TYPE_VECTOR_SUBPARTS (t1) == TYPE_VECTOR_SUBPARTS (t2)
	    && comptypes_internal (TREE_TYPE (t1), TREE_TYPE (t2));
      break;

    default:
      break;
    }
  return attrval == 2 && val == 1 ? 2 : val;
}

/* Return 1 if TTL and TTR are pointers to types that are equivalent,
   ignoring their qualifiers.  */

static int
comp_target_types (tree ttl, tree ttr)
{
  int val;
  tree mvl, mvr;

  /* Do not lose qualifiers on element types of array types that are
     pointer targets by taking their TYPE_MAIN_VARIANT.  */
  mvl = TREE_TYPE (ttl);
  mvr = TREE_TYPE (ttr);
  if (TREE_CODE (mvl) != ARRAY_TYPE)
    mvl = TYPE_MAIN_VARIANT (mvl);
  if (TREE_CODE (mvr) != ARRAY_TYPE)
    mvr = TYPE_MAIN_VARIANT (mvr);
  val = comptypes (mvl, mvr);

  if (val == 2 && pedantic)
    pedwarn ("types are not quite compatible");
  return val;
}

/* Subroutines of `comptypes'.  */

/* Determine whether two trees derive from the same translation unit.
   If the CONTEXT chain ends in a null, that tree's context is still
   being parsed, so if two trees have context chains ending in null,
   they're in the same translation unit.  */
int
same_translation_unit_p (tree t1, tree t2)
{
  while (t1 && TREE_CODE (t1) != TRANSLATION_UNIT_DECL)
    switch (TREE_CODE_CLASS (TREE_CODE (t1)))
      {
      case tcc_declaration:
	t1 = DECL_CONTEXT (t1); break;
      case tcc_type:
	t1 = TYPE_CONTEXT (t1); break;
      case tcc_exceptional:
	t1 = BLOCK_SUPERCONTEXT (t1); break;  /* assume block */
      default: gcc_unreachable ();
      }

  while (t2 && TREE_CODE (t2) != TRANSLATION_UNIT_DECL)
    switch (TREE_CODE_CLASS (TREE_CODE (t2)))
      {
      case tcc_declaration:
	t2 = DECL_CONTEXT (t2); break;
      case tcc_type:
	t2 = TYPE_CONTEXT (t2); break;
      case tcc_exceptional:
	t2 = BLOCK_SUPERCONTEXT (t2); break;  /* assume block */
      default: gcc_unreachable ();
      }

  return t1 == t2;
}

/* Allocate the seen two types, assuming that they are compatible. */

static struct tagged_tu_seen_cache *
alloc_tagged_tu_seen_cache (tree t1, tree t2)
{
  struct tagged_tu_seen_cache *tu = XNEW (struct tagged_tu_seen_cache);
  tu->next = tagged_tu_seen_base;
  tu->t1 = t1;
  tu->t2 = t2;

  tagged_tu_seen_base = tu;

  /* The C standard says that two structures in different translation
     units are compatible with each other only if the types of their
     fields are compatible (among other things).  We assume that they
     are compatible until proven otherwise when building the cache.
     An example where this can occur is:
     struct a
     {
       struct a *next;
     };
     If we are comparing this against a similar struct in another TU,
     and did not assume they were compatible, we end up with an infinite
     loop.  */
  tu->val = 1;
  return tu;
}

/* Free the seen types until we get to TU_TIL. */

static void
free_all_tagged_tu_seen_up_to (const struct tagged_tu_seen_cache *tu_til)
{
  const struct tagged_tu_seen_cache *tu = tagged_tu_seen_base;
  while (tu != tu_til)
    {
      struct tagged_tu_seen_cache *tu1 = (struct tagged_tu_seen_cache*)tu;
      tu = tu1->next;
      free (tu1);
    }
  tagged_tu_seen_base = tu_til;
}

/* Return 1 if two 'struct', 'union', or 'enum' types T1 and T2 are
   compatible.  If the two types are not the same (which has been
   checked earlier), this can only happen when multiple translation
   units are being compiled.  See C99 6.2.7 paragraph 1 for the exact
   rules.  */

static int
tagged_types_tu_compatible_p (tree t1, tree t2)
{
  tree s1, s2;
  bool needs_warning = false;

  /* We have to verify that the tags of the types are the same.  This
     is harder than it looks because this may be a typedef, so we have
     to go look at the original type.  It may even be a typedef of a
     typedef...
     In the case of compiler-created builtin structs the TYPE_DECL
     may be a dummy, with no DECL_ORIGINAL_TYPE.  Don't fault.  */
  while (TYPE_NAME (t1)
	 && TREE_CODE (TYPE_NAME (t1)) == TYPE_DECL
	 && DECL_ORIGINAL_TYPE (TYPE_NAME (t1)))
    t1 = DECL_ORIGINAL_TYPE (TYPE_NAME (t1));

  while (TYPE_NAME (t2)
	 && TREE_CODE (TYPE_NAME (t2)) == TYPE_DECL
	 && DECL_ORIGINAL_TYPE (TYPE_NAME (t2)))
    t2 = DECL_ORIGINAL_TYPE (TYPE_NAME (t2));

  /* C90 didn't have the requirement that the two tags be the same.  */
  if (flag_isoc99 && TYPE_NAME (t1) != TYPE_NAME (t2))
    return 0;

  /* C90 didn't say what happened if one or both of the types were
     incomplete; we choose to follow C99 rules here, which is that they
     are compatible.  */
  if (TYPE_SIZE (t1) == NULL
      || TYPE_SIZE (t2) == NULL)
    return 1;

  {
    const struct tagged_tu_seen_cache * tts_i;
    for (tts_i = tagged_tu_seen_base; tts_i != NULL; tts_i = tts_i->next)
      if (tts_i->t1 == t1 && tts_i->t2 == t2)
	return tts_i->val;
  }

  switch (TREE_CODE (t1))
    {
    case ENUMERAL_TYPE:
      {
	struct tagged_tu_seen_cache *tu = alloc_tagged_tu_seen_cache (t1, t2);
	/* Speed up the case where the type values are in the same order.  */
	tree tv1 = TYPE_VALUES (t1);
	tree tv2 = TYPE_VALUES (t2);

	if (tv1 == tv2)
	  {
	    return 1;
	  }

	for (;tv1 && tv2; tv1 = TREE_CHAIN (tv1), tv2 = TREE_CHAIN (tv2))
	  {
	    if (TREE_PURPOSE (tv1) != TREE_PURPOSE (tv2))
	      break;
	    if (simple_cst_equal (TREE_VALUE (tv1), TREE_VALUE (tv2)) != 1)
	      {
		tu->val = 0;
		return 0;
	      }
	  }

	if (tv1 == NULL_TREE && tv2 == NULL_TREE)
	  {
	    return 1;
	  }
	if (tv1 == NULL_TREE || tv2 == NULL_TREE)
	  {
	    tu->val = 0;
	    return 0;
	  }

	if (list_length (TYPE_VALUES (t1)) != list_length (TYPE_VALUES (t2)))
	  {
	    tu->val = 0;
	    return 0;
	  }

	for (s1 = TYPE_VALUES (t1); s1; s1 = TREE_CHAIN (s1))
	  {
	    s2 = purpose_member (TREE_PURPOSE (s1), TYPE_VALUES (t2));
	    if (s2 == NULL
		|| simple_cst_equal (TREE_VALUE (s1), TREE_VALUE (s2)) != 1)
	      {
		tu->val = 0;
		return 0;
	      }
	  }
	return 1;
      }

    case UNION_TYPE:
      {
	struct tagged_tu_seen_cache *tu = alloc_tagged_tu_seen_cache (t1, t2);
	if (list_length (TYPE_FIELDS (t1)) != list_length (TYPE_FIELDS (t2)))
	  {
	    tu->val = 0;
	    return 0;
	  }

	/*  Speed up the common case where the fields are in the same order. */
	for (s1 = TYPE_FIELDS (t1), s2 = TYPE_FIELDS (t2); s1 && s2;
	     s1 = TREE_CHAIN (s1), s2 = TREE_CHAIN (s2))
	  {
	    int result;


	    if (DECL_NAME (s1) == NULL
		|| DECL_NAME (s1) != DECL_NAME (s2))
	      break;
	    result = comptypes_internal (TREE_TYPE (s1), TREE_TYPE (s2));
	    if (result == 0)
	      {
		tu->val = 0;
		return 0;
	      }
	    if (result == 2)
	      needs_warning = true;

	    if (TREE_CODE (s1) == FIELD_DECL
		&& simple_cst_equal (DECL_FIELD_BIT_OFFSET (s1),
				     DECL_FIELD_BIT_OFFSET (s2)) != 1)
	      {
		tu->val = 0;
		return 0;
	      }
	  }
	if (!s1 && !s2)
	  {
	    tu->val = needs_warning ? 2 : 1;
	    return tu->val;
	  }

	for (s1 = TYPE_FIELDS (t1); s1; s1 = TREE_CHAIN (s1))
	  {
	    bool ok = false;

	    if (DECL_NAME (s1) != NULL)
	      for (s2 = TYPE_FIELDS (t2); s2; s2 = TREE_CHAIN (s2))
		if (DECL_NAME (s1) == DECL_NAME (s2))
		  {
		    int result;
		    result = comptypes_internal (TREE_TYPE (s1), TREE_TYPE (s2));
		    if (result == 0)
		      {
			tu->val = 0;
			return 0;
		      }
		    if (result == 2)
		      needs_warning = true;

		    if (TREE_CODE (s1) == FIELD_DECL
			&& simple_cst_equal (DECL_FIELD_BIT_OFFSET (s1),
					     DECL_FIELD_BIT_OFFSET (s2)) != 1)
		      break;

		    ok = true;
		    break;
		  }
	    if (!ok)
	      {
		tu->val = 0;
		return 0;
	      }
	  }
	tu->val = needs_warning ? 2 : 10;
	return tu->val;
      }

    case RECORD_TYPE:
      {
	struct tagged_tu_seen_cache *tu = alloc_tagged_tu_seen_cache (t1, t2);

	for (s1 = TYPE_FIELDS (t1), s2 = TYPE_FIELDS (t2);
	     s1 && s2;
	     s1 = TREE_CHAIN (s1), s2 = TREE_CHAIN (s2))
	  {
	    int result;
	    if (TREE_CODE (s1) != TREE_CODE (s2)
		|| DECL_NAME (s1) != DECL_NAME (s2))
	      break;
	    result = comptypes_internal (TREE_TYPE (s1), TREE_TYPE (s2));
	    if (result == 0)
	      break;
	    if (result == 2)
	      needs_warning = true;

	    if (TREE_CODE (s1) == FIELD_DECL
		&& simple_cst_equal (DECL_FIELD_BIT_OFFSET (s1),
				     DECL_FIELD_BIT_OFFSET (s2)) != 1)
	      break;
	  }
	if (s1 && s2)
	  tu->val = 0;
	else
	  tu->val = needs_warning ? 2 : 1;
	return tu->val;
      }

    default:
      gcc_unreachable ();
    }
}

/* Return 1 if two function types F1 and F2 are compatible.
   If either type specifies no argument types,
   the other must specify a fixed number of self-promoting arg types.
   Otherwise, if one type specifies only the number of arguments,
   the other must specify that number of self-promoting arg types.
   Otherwise, the argument types must match.  */

static int
function_types_compatible_p (tree f1, tree f2)
{
  tree args1, args2;
  /* 1 if no need for warning yet, 2 if warning cause has been seen.  */
  int val = 1;
  int val1;
  tree ret1, ret2;

  ret1 = TREE_TYPE (f1);
  ret2 = TREE_TYPE (f2);

  /* 'volatile' qualifiers on a function's return type used to mean
     the function is noreturn.  */
  if (TYPE_VOLATILE (ret1) != TYPE_VOLATILE (ret2))
    pedwarn ("function return types not compatible due to %<volatile%>");
  if (TYPE_VOLATILE (ret1))
    ret1 = build_qualified_type (TYPE_MAIN_VARIANT (ret1),
				 TYPE_QUALS (ret1) & ~TYPE_QUAL_VOLATILE);
  if (TYPE_VOLATILE (ret2))
    ret2 = build_qualified_type (TYPE_MAIN_VARIANT (ret2),
				 TYPE_QUALS (ret2) & ~TYPE_QUAL_VOLATILE);
  val = comptypes_internal (ret1, ret2);
  if (val == 0)
    return 0;

  args1 = TYPE_ARG_TYPES (f1);
  args2 = TYPE_ARG_TYPES (f2);

  /* An unspecified parmlist matches any specified parmlist
     whose argument types don't need default promotions.  */

  if (args1 == 0)
    {
      if (!self_promoting_args_p (args2))
	return 0;
      /* If one of these types comes from a non-prototype fn definition,
	 compare that with the other type's arglist.
	 If they don't match, ask for a warning (but no error).  */
      if (TYPE_ACTUAL_ARG_TYPES (f1)
	  && 1 != type_lists_compatible_p (args2, TYPE_ACTUAL_ARG_TYPES (f1)))
	val = 2;
      return val;
    }
  if (args2 == 0)
    {
      if (!self_promoting_args_p (args1))
	return 0;
      if (TYPE_ACTUAL_ARG_TYPES (f2)
	  && 1 != type_lists_compatible_p (args1, TYPE_ACTUAL_ARG_TYPES (f2)))
	val = 2;
      return val;
    }

  /* Both types have argument lists: compare them and propagate results.  */
  val1 = type_lists_compatible_p (args1, args2);
  return val1 != 1 ? val1 : val;
}

/* Check two lists of types for compatibility,
   returning 0 for incompatible, 1 for compatible,
   or 2 for compatible with warning.  */

static int
type_lists_compatible_p (tree args1, tree args2)
{
  /* 1 if no need for warning yet, 2 if warning cause has been seen.  */
  int val = 1;
  int newval = 0;

  while (1)
    {
      tree a1, mv1, a2, mv2;
      if (args1 == 0 && args2 == 0)
	return val;
      /* If one list is shorter than the other,
	 they fail to match.  */
      if (args1 == 0 || args2 == 0)
	return 0;
      mv1 = a1 = TREE_VALUE (args1);
      mv2 = a2 = TREE_VALUE (args2);
      if (mv1 && mv1 != error_mark_node && TREE_CODE (mv1) != ARRAY_TYPE)
	mv1 = TYPE_MAIN_VARIANT (mv1);
      if (mv2 && mv2 != error_mark_node && TREE_CODE (mv2) != ARRAY_TYPE)
	mv2 = TYPE_MAIN_VARIANT (mv2);
      /* A null pointer instead of a type
	 means there is supposed to be an argument
	 but nothing is specified about what type it has.
	 So match anything that self-promotes.  */
      if (a1 == 0)
	{
	  if (c_type_promotes_to (a2) != a2)
	    return 0;
	}
      else if (a2 == 0)
	{
	  if (c_type_promotes_to (a1) != a1)
	    return 0;
	}
      /* If one of the lists has an error marker, ignore this arg.  */
      else if (TREE_CODE (a1) == ERROR_MARK
	       || TREE_CODE (a2) == ERROR_MARK)
	;
      else if (!(newval = comptypes_internal (mv1, mv2)))
	{
	  /* Allow  wait (union {union wait *u; int *i} *)
	     and  wait (union wait *)  to be compatible.  */
	  if (TREE_CODE (a1) == UNION_TYPE
	      && (TYPE_NAME (a1) == 0
		  || TYPE_TRANSPARENT_UNION (a1))
	      && TREE_CODE (TYPE_SIZE (a1)) == INTEGER_CST
	      && tree_int_cst_equal (TYPE_SIZE (a1),
				     TYPE_SIZE (a2)))
	    {
	      tree memb;
	      for (memb = TYPE_FIELDS (a1);
		   memb; memb = TREE_CHAIN (memb))
		{
		  tree mv3 = TREE_TYPE (memb);
		  if (mv3 && mv3 != error_mark_node
		      && TREE_CODE (mv3) != ARRAY_TYPE)
		    mv3 = TYPE_MAIN_VARIANT (mv3);
		  if (comptypes_internal (mv3, mv2))
		    break;
		}
	      if (memb == 0)
		return 0;
	    }
	  else if (TREE_CODE (a2) == UNION_TYPE
		   && (TYPE_NAME (a2) == 0
		       || TYPE_TRANSPARENT_UNION (a2))
		   && TREE_CODE (TYPE_SIZE (a2)) == INTEGER_CST
		   && tree_int_cst_equal (TYPE_SIZE (a2),
					  TYPE_SIZE (a1)))
	    {
	      tree memb;
	      for (memb = TYPE_FIELDS (a2);
		   memb; memb = TREE_CHAIN (memb))
		{
		  tree mv3 = TREE_TYPE (memb);
		  if (mv3 && mv3 != error_mark_node
		      && TREE_CODE (mv3) != ARRAY_TYPE)
		    mv3 = TYPE_MAIN_VARIANT (mv3);
		  if (comptypes_internal (mv3, mv1))
		    break;
		}
	      if (memb == 0)
		return 0;
	    }
	  else
	    return 0;
	}

      /* comptypes said ok, but record if it said to warn.  */
      if (newval > val)
	val = newval;

      args1 = TREE_CHAIN (args1);
      args2 = TREE_CHAIN (args2);
    }
}

/* Compute the size to increment a pointer by.  */

static tree
c_size_in_bytes (tree type)
{
  enum tree_code code = TREE_CODE (type);

  if (code == FUNCTION_TYPE || code == VOID_TYPE || code == ERROR_MARK)
    return size_one_node;

  if (!COMPLETE_OR_VOID_TYPE_P (type))
    {
      error ("arithmetic on pointer to an incomplete type");
      return size_one_node;
    }

  /* Convert in case a char is more than one unit.  */
  return size_binop (CEIL_DIV_EXPR, TYPE_SIZE_UNIT (type),
		     size_int (TYPE_PRECISION (char_type_node)
			       / BITS_PER_UNIT));
}

/* Return either DECL or its known constant value (if it has one).  */

tree
decl_constant_value (tree decl)
{
  if (/* Don't change a variable array bound or initial value to a constant
	 in a place where a variable is invalid.  Note that DECL_INITIAL
	 isn't valid for a PARM_DECL.  */
      current_function_decl != 0
      && TREE_CODE (decl) != PARM_DECL
      && !TREE_THIS_VOLATILE (decl)
      && TREE_READONLY (decl)
      && DECL_INITIAL (decl) != 0
      && TREE_CODE (DECL_INITIAL (decl)) != ERROR_MARK
      /* This is invalid if initial value is not constant.
	 If it has either a function call, a memory reference,
	 or a variable, then re-evaluating it could give different results.  */
      && TREE_CONSTANT (DECL_INITIAL (decl))
      /* Check for cases where this is sub-optimal, even though valid.  */
      && TREE_CODE (DECL_INITIAL (decl)) != CONSTRUCTOR)
    return DECL_INITIAL (decl);
  return decl;
}

/* Return either DECL or its known constant value (if it has one), but
   return DECL if pedantic or DECL has mode BLKmode.  This is for
   bug-compatibility with the old behavior of decl_constant_value
   (before GCC 3.0); every use of this function is a bug and it should
   be removed before GCC 3.1.  It is not appropriate to use pedantic
   in a way that affects optimization, and BLKmode is probably not the
   right test for avoiding misoptimizations either.  */

static tree
decl_constant_value_for_broken_optimization (tree decl)
{
  tree ret;

  if (pedantic || DECL_MODE (decl) == BLKmode)
    return decl;

  ret = decl_constant_value (decl);
  /* Avoid unwanted tree sharing between the initializer and current
     function's body where the tree can be modified e.g. by the
     gimplifier.  */
  if (ret != decl && TREE_STATIC (decl))
    ret = unshare_expr (ret);
  return ret;
}

/* Convert the array expression EXP to a pointer.  */
static tree
array_to_pointer_conversion (tree exp)
{
  tree orig_exp = exp;
  tree type = TREE_TYPE (exp);
  tree adr;
  tree restype = TREE_TYPE (type);
  tree ptrtype;

  gcc_assert (TREE_CODE (type) == ARRAY_TYPE);

  STRIP_TYPE_NOPS (exp);

  if (TREE_NO_WARNING (orig_exp))
    TREE_NO_WARNING (exp) = 1;

  ptrtype = build_pointer_type (restype);

  if (TREE_CODE (exp) == INDIRECT_REF)
    return convert (ptrtype, TREE_OPERAND (exp, 0));

  if (TREE_CODE (exp) == VAR_DECL)
    {
      /* We are making an ADDR_EXPR of ptrtype.  This is a valid
	 ADDR_EXPR because it's the best way of representing what
	 happens in C when we take the address of an array and place
	 it in a pointer to the element type.  */
      adr = build1 (ADDR_EXPR, ptrtype, exp);
      if (!c_mark_addressable (exp))
	return error_mark_node;
      TREE_SIDE_EFFECTS (adr) = 0;   /* Default would be, same as EXP.  */
      return adr;
    }

  /* This way is better for a COMPONENT_REF since it can
     simplify the offset for a component.  */
  adr = build_unary_op (ADDR_EXPR, exp, 1);
  return convert (ptrtype, adr);
}

/* Convert the function expression EXP to a pointer.  */
static tree
function_to_pointer_conversion (tree exp)
{
  tree orig_exp = exp;

  gcc_assert (TREE_CODE (TREE_TYPE (exp)) == FUNCTION_TYPE);

  STRIP_TYPE_NOPS (exp);

  if (TREE_NO_WARNING (orig_exp))
    TREE_NO_WARNING (exp) = 1;

  return build_unary_op (ADDR_EXPR, exp, 0);
}

/* Perform the default conversion of arrays and functions to pointers.
   Return the result of converting EXP.  For any other expression, just
   return EXP after removing NOPs.  */

struct c_expr
default_function_array_conversion (struct c_expr exp)
{
  tree orig_exp = exp.value;
  tree type = TREE_TYPE (exp.value);
  enum tree_code code = TREE_CODE (type);

  switch (code)
    {
    case ARRAY_TYPE:
      {
	bool not_lvalue = false;
	bool lvalue_array_p;

	while ((TREE_CODE (exp.value) == NON_LVALUE_EXPR
		|| TREE_CODE (exp.value) == NOP_EXPR
		|| TREE_CODE (exp.value) == CONVERT_EXPR)
	       && TREE_TYPE (TREE_OPERAND (exp.value, 0)) == type)
	  {
	    if (TREE_CODE (exp.value) == NON_LVALUE_EXPR)
	      not_lvalue = true;
	    exp.value = TREE_OPERAND (exp.value, 0);
	  }

	if (TREE_NO_WARNING (orig_exp))
	  TREE_NO_WARNING (exp.value) = 1;

	lvalue_array_p = !not_lvalue && lvalue_p (exp.value);
	if (!flag_isoc99 && !lvalue_array_p)
	  {
	    /* Before C99, non-lvalue arrays do not decay to pointers.
	       Normally, using such an array would be invalid; but it can
	       be used correctly inside sizeof or as a statement expression.
	       Thus, do not give an error here; an error will result later.  */
	    return exp;
	  }

	exp.value = array_to_pointer_conversion (exp.value);
      }
      break;
    case FUNCTION_TYPE:
      exp.value = function_to_pointer_conversion (exp.value);
      break;
    default:
      STRIP_TYPE_NOPS (exp.value);
      if (TREE_NO_WARNING (orig_exp))
	TREE_NO_WARNING (exp.value) = 1;
      break;
    }

  return exp;
}


/* EXP is an expression of integer type.  Apply the integer promotions
   to it and return the promoted value.  */

tree
perform_integral_promotions (tree exp)
{
  tree type = TREE_TYPE (exp);
  enum tree_code code = TREE_CODE (type);

  gcc_assert (INTEGRAL_TYPE_P (type));

  /* Normally convert enums to int,
     but convert wide enums to something wider.  */
  if (code == ENUMERAL_TYPE)
    {
      type = c_common_type_for_size (MAX (TYPE_PRECISION (type),
					  TYPE_PRECISION (integer_type_node)),
				     ((TYPE_PRECISION (type)
				       >= TYPE_PRECISION (integer_type_node))
				      && TYPE_UNSIGNED (type)));

      return convert (type, exp);
    }

  /* ??? This should no longer be needed now bit-fields have their
     proper types.  */
  if (TREE_CODE (exp) == COMPONENT_REF
      && DECL_C_BIT_FIELD (TREE_OPERAND (exp, 1))
      /* If it's thinner than an int, promote it like a
	 c_promoting_integer_type_p, otherwise leave it alone.  */
      && 0 > compare_tree_int (DECL_SIZE (TREE_OPERAND (exp, 1)),
			       TYPE_PRECISION (integer_type_node)))
    return convert (integer_type_node, exp);

  if (c_promoting_integer_type_p (type))
    {
      /* Preserve unsignedness if not really getting any wider.  */
      if (TYPE_UNSIGNED (type)
	  && TYPE_PRECISION (type) == TYPE_PRECISION (integer_type_node))
	return convert (unsigned_type_node, exp);

      return convert (integer_type_node, exp);
    }

  return exp;
}


/* Perform default promotions for C data used in expressions.
   Enumeral types or short or char are converted to int.
   In addition, manifest constants symbols are replaced by their values.  */

tree
default_conversion (tree exp)
{
  tree orig_exp;
  tree type = TREE_TYPE (exp);
  enum tree_code code = TREE_CODE (type);

  /* Functions and arrays have been converted during parsing.  */
  gcc_assert (code != FUNCTION_TYPE);
  if (code == ARRAY_TYPE)
    return exp;

  /* Constants can be used directly unless they're not loadable.  */
  if (TREE_CODE (exp) == CONST_DECL)
    exp = DECL_INITIAL (exp);

  /* Replace a nonvolatile const static variable with its value unless
     it is an array, in which case we must be sure that taking the
     address of the array produces consistent results.  */
  else if (optimize && TREE_CODE (exp) == VAR_DECL && code != ARRAY_TYPE)
    {
      exp = decl_constant_value_for_broken_optimization (exp);
      type = TREE_TYPE (exp);
    }

  /* Strip no-op conversions.  */
  orig_exp = exp;
  STRIP_TYPE_NOPS (exp);

  if (TREE_NO_WARNING (orig_exp))
    TREE_NO_WARNING (exp) = 1;

  if (INTEGRAL_TYPE_P (type))
    return perform_integral_promotions (exp);

  if (code == VOID_TYPE)
    {
      error ("void value not ignored as it ought to be");
      return error_mark_node;
    }
  return exp;
}

/* Look up COMPONENT in a structure or union DECL.

   If the component name is not found, returns NULL_TREE.  Otherwise,
   the return value is a TREE_LIST, with each TREE_VALUE a FIELD_DECL
   stepping down the chain to the component, which is in the last
   TREE_VALUE of the list.  Normally the list is of length one, but if
   the component is embedded within (nested) anonymous structures or
   unions, the list steps down the chain to the component.  */

static tree
lookup_field (tree decl, tree component)
{
  tree type = TREE_TYPE (decl);
  tree field;

  /* If TYPE_LANG_SPECIFIC is set, then it is a sorted array of pointers
     to the field elements.  Use a binary search on this array to quickly
     find the element.  Otherwise, do a linear search.  TYPE_LANG_SPECIFIC
     will always be set for structures which have many elements.  */

  if (TYPE_LANG_SPECIFIC (type) && TYPE_LANG_SPECIFIC (type)->s)
    {
      int bot, top, half;
      tree *field_array = &TYPE_LANG_SPECIFIC (type)->s->elts[0];

      field = TYPE_FIELDS (type);
      bot = 0;
      top = TYPE_LANG_SPECIFIC (type)->s->len;
      while (top - bot > 1)
	{
	  half = (top - bot + 1) >> 1;
	  field = field_array[bot+half];

	  if (DECL_NAME (field) == NULL_TREE)
	    {
	      /* Step through all anon unions in linear fashion.  */
	      while (DECL_NAME (field_array[bot]) == NULL_TREE)
		{
		  field = field_array[bot++];
		  if (TREE_CODE (TREE_TYPE (field)) == RECORD_TYPE
		      || TREE_CODE (TREE_TYPE (field)) == UNION_TYPE)
		    {
		      tree anon = lookup_field (field, component);

		      if (anon)
			return tree_cons (NULL_TREE, field, anon);
		    }
		}

	      /* Entire record is only anon unions.  */
	      if (bot > top)
		return NULL_TREE;

	      /* Restart the binary search, with new lower bound.  */
	      continue;
	    }

	  if (DECL_NAME (field) == component)
	    break;
	  if (DECL_NAME (field) < component)
	    bot += half;
	  else
	    top = bot + half;
	}

      if (DECL_NAME (field_array[bot]) == component)
	field = field_array[bot];
      else if (DECL_NAME (field) != component)
	return NULL_TREE;
    }
  else
    {
      for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
	{
	  if (DECL_NAME (field) == NULL_TREE
	      && (TREE_CODE (TREE_TYPE (field)) == RECORD_TYPE
		  || TREE_CODE (TREE_TYPE (field)) == UNION_TYPE))
	    {
	      tree anon = lookup_field (field, component);

	      if (anon)
		return tree_cons (NULL_TREE, field, anon);
	    }

	  if (DECL_NAME (field) == component)
	    break;
	}

      if (field == NULL_TREE)
	return NULL_TREE;
    }

  return tree_cons (NULL_TREE, field, NULL_TREE);
}

/* Make an expression to refer to the COMPONENT field of
   structure or union value DATUM.  COMPONENT is an IDENTIFIER_NODE.  */

tree
build_component_ref (tree datum, tree component)
{
  tree type = TREE_TYPE (datum);
  enum tree_code code = TREE_CODE (type);
  tree field = NULL;
  tree ref;

  if (!objc_is_public (datum, component))
    return error_mark_node;

  /* See if there is a field or component with name COMPONENT.  */

  if (code == RECORD_TYPE || code == UNION_TYPE)
    {
      if (!COMPLETE_TYPE_P (type))
	{
	  c_incomplete_type_error (NULL_TREE, type);
	  return error_mark_node;
	}

      field = lookup_field (datum, component);

      if (!field)
	{
	  error ("%qT has no member named %qE", type, component);
	  return error_mark_node;
	}

      /* Chain the COMPONENT_REFs if necessary down to the FIELD.
	 This might be better solved in future the way the C++ front
	 end does it - by giving the anonymous entities each a
	 separate name and type, and then have build_component_ref
	 recursively call itself.  We can't do that here.  */
      do
	{
	  tree subdatum = TREE_VALUE (field);
	  int quals;
	  tree subtype;

	  if (TREE_TYPE (subdatum) == error_mark_node)
	    return error_mark_node;

	  quals = TYPE_QUALS (strip_array_types (TREE_TYPE (subdatum)));
	  quals |= TYPE_QUALS (TREE_TYPE (datum));
	  subtype = c_build_qualified_type (TREE_TYPE (subdatum), quals);

	  ref = build3 (COMPONENT_REF, subtype, datum, subdatum,
			NULL_TREE);
	  if (TREE_READONLY (datum) || TREE_READONLY (subdatum))
	    TREE_READONLY (ref) = 1;
	  if (TREE_THIS_VOLATILE (datum) || TREE_THIS_VOLATILE (subdatum))
	    TREE_THIS_VOLATILE (ref) = 1;

	  if (TREE_DEPRECATED (subdatum))
	    warn_deprecated_use (subdatum);

	  datum = ref;

	  field = TREE_CHAIN (field);
	}
      while (field);

      return ref;
    }
  else if (code != ERROR_MARK)
    error ("request for member %qE in something not a structure or union",
	   component);

  return error_mark_node;
}

/* Given an expression PTR for a pointer, return an expression
   for the value pointed to.
   ERRORSTRING is the name of the operator to appear in error messages.  */

tree
build_indirect_ref (tree ptr, const char *errorstring)
{
  tree pointer = default_conversion (ptr);
  tree type = TREE_TYPE (pointer);

  if (TREE_CODE (type) == POINTER_TYPE)
    {
      if (TREE_CODE (pointer) == ADDR_EXPR
	  && (TREE_TYPE (TREE_OPERAND (pointer, 0))
	      == TREE_TYPE (type)))
	return TREE_OPERAND (pointer, 0);
      else
	{
	  tree t = TREE_TYPE (type);
	  tree ref;

	  ref = build1 (INDIRECT_REF, t, pointer);

	  if (!COMPLETE_OR_VOID_TYPE_P (t) && TREE_CODE (t) != ARRAY_TYPE)
	    {
	      error ("dereferencing pointer to incomplete type");
	      return error_mark_node;
	    }
	  if (VOID_TYPE_P (t) && skip_evaluation == 0)
	    warning (0, "dereferencing %<void *%> pointer");

	  /* We *must* set TREE_READONLY when dereferencing a pointer to const,
	     so that we get the proper error message if the result is used
	     to assign to.  Also, &* is supposed to be a no-op.
	     And ANSI C seems to specify that the type of the result
	     should be the const type.  */
	  /* A de-reference of a pointer to const is not a const.  It is valid
	     to change it via some other pointer.  */
	  TREE_READONLY (ref) = TYPE_READONLY (t);
	  TREE_SIDE_EFFECTS (ref)
	    = TYPE_VOLATILE (t) || TREE_SIDE_EFFECTS (pointer);
	  TREE_THIS_VOLATILE (ref) = TYPE_VOLATILE (t);
	  return ref;
	}
    }
  else if (TREE_CODE (pointer) != ERROR_MARK)
    error ("invalid type argument of %qs", errorstring);
  return error_mark_node;
}

/* This handles expressions of the form "a[i]", which denotes
   an array reference.

   This is logically equivalent in C to *(a+i), but we may do it differently.
   If A is a variable or a member, we generate a primitive ARRAY_REF.
   This avoids forcing the array out of registers, and can work on
   arrays that are not lvalues (for example, members of structures returned
   by functions).  */

tree
build_array_ref (tree array, tree index)
{
  bool swapped = false;
  if (TREE_TYPE (array) == error_mark_node
      || TREE_TYPE (index) == error_mark_node)
    return error_mark_node;

  if (TREE_CODE (TREE_TYPE (array)) != ARRAY_TYPE
      && TREE_CODE (TREE_TYPE (array)) != POINTER_TYPE)
    {
      tree temp;
      if (TREE_CODE (TREE_TYPE (index)) != ARRAY_TYPE
	  && TREE_CODE (TREE_TYPE (index)) != POINTER_TYPE)
	{
	  error ("subscripted value is neither array nor pointer");
	  return error_mark_node;
	}
      temp = array;
      array = index;
      index = temp;
      swapped = true;
    }

  if (!INTEGRAL_TYPE_P (TREE_TYPE (index)))
    {
      error ("array subscript is not an integer");
      return error_mark_node;
    }

  if (TREE_CODE (TREE_TYPE (TREE_TYPE (array))) == FUNCTION_TYPE)
    {
      error ("subscripted value is pointer to function");
      return error_mark_node;
    }

  /* ??? Existing practice has been to warn only when the char
     index is syntactically the index, not for char[array].  */
  if (!swapped)
     warn_array_subscript_with_type_char (index);

  /* Apply default promotions *after* noticing character types.  */
  index = default_conversion (index);

  gcc_assert (TREE_CODE (TREE_TYPE (index)) == INTEGER_TYPE);

  if (TREE_CODE (TREE_TYPE (array)) == ARRAY_TYPE)
    {
      tree rval, type;

      /* An array that is indexed by a non-constant
	 cannot be stored in a register; we must be able to do
	 address arithmetic on its address.
	 Likewise an array of elements of variable size.  */
      if (TREE_CODE (index) != INTEGER_CST
	  || (COMPLETE_TYPE_P (TREE_TYPE (TREE_TYPE (array)))
	      && TREE_CODE (TYPE_SIZE (TREE_TYPE (TREE_TYPE (array)))) != INTEGER_CST))
	{
	  if (!c_mark_addressable (array))
	    return error_mark_node;
	}
      /* An array that is indexed by a constant value which is not within
	 the array bounds cannot be stored in a register either; because we
	 would get a crash in store_bit_field/extract_bit_field when trying
	 to access a non-existent part of the register.  */
      if (TREE_CODE (index) == INTEGER_CST
	  && TYPE_DOMAIN (TREE_TYPE (array))
	  && !int_fits_type_p (index, TYPE_DOMAIN (TREE_TYPE (array))))
	{
	  if (!c_mark_addressable (array))
	    return error_mark_node;
	}

      if (pedantic)
	{
	  tree foo = array;
	  while (TREE_CODE (foo) == COMPONENT_REF)
	    foo = TREE_OPERAND (foo, 0);
	  if (TREE_CODE (foo) == VAR_DECL && C_DECL_REGISTER (foo))
	    pedwarn ("ISO C forbids subscripting %<register%> array");
	  else if (!flag_isoc99 && !lvalue_p (foo))
	    pedwarn ("ISO C90 forbids subscripting non-lvalue array");
	}

      type = TREE_TYPE (TREE_TYPE (array));
      if (TREE_CODE (type) != ARRAY_TYPE)
	type = TYPE_MAIN_VARIANT (type);
      rval = build4 (ARRAY_REF, type, array, index, NULL_TREE, NULL_TREE);
      /* Array ref is const/volatile if the array elements are
	 or if the array is.  */
      TREE_READONLY (rval)
	|= (TYPE_READONLY (TREE_TYPE (TREE_TYPE (array)))
	    | TREE_READONLY (array));
      TREE_SIDE_EFFECTS (rval)
	|= (TYPE_VOLATILE (TREE_TYPE (TREE_TYPE (array)))
	    | TREE_SIDE_EFFECTS (array));
      TREE_THIS_VOLATILE (rval)
	|= (TYPE_VOLATILE (TREE_TYPE (TREE_TYPE (array)))
	    /* This was added by rms on 16 Nov 91.
	       It fixes  vol struct foo *a;  a->elts[1]
	       in an inline function.
	       Hope it doesn't break something else.  */
	    | TREE_THIS_VOLATILE (array));
      return require_complete_type (fold (rval));
    }
  else
    {
      tree ar = default_conversion (array);

      if (ar == error_mark_node)
	return ar;

      gcc_assert (TREE_CODE (TREE_TYPE (ar)) == POINTER_TYPE);
      gcc_assert (TREE_CODE (TREE_TYPE (TREE_TYPE (ar))) != FUNCTION_TYPE);

      return build_indirect_ref (build_binary_op (PLUS_EXPR, ar, index, 0),
				 "array indexing");
    }
}

/* Build an external reference to identifier ID.  FUN indicates
   whether this will be used for a function call.  LOC is the source
   location of the identifier.  */
tree
build_external_ref (tree id, int fun, location_t loc)
{
  tree ref;
  tree decl = lookup_name (id);

  /* In Objective-C, an instance variable (ivar) may be preferred to
     whatever lookup_name() found.  */
  decl = objc_lookup_ivar (decl, id);

  if (decl && decl != error_mark_node)
    ref = decl;
  else if (fun)
    /* Implicit function declaration.  */
    ref = implicitly_declare (id);
  else if (decl == error_mark_node)
    /* Don't complain about something that's already been
       complained about.  */
    return error_mark_node;
  else
    {
      undeclared_variable (id, loc);
      return error_mark_node;
    }

  if (TREE_TYPE (ref) == error_mark_node)
    return error_mark_node;

  if (TREE_DEPRECATED (ref))
    warn_deprecated_use (ref);

  if (!skip_evaluation)
    assemble_external (ref);
  TREE_USED (ref) = 1;

  if (TREE_CODE (ref) == FUNCTION_DECL && !in_alignof)
    {
      if (!in_sizeof && !in_typeof)
	C_DECL_USED (ref) = 1;
      else if (DECL_INITIAL (ref) == 0
	       && DECL_EXTERNAL (ref)
	       && !TREE_PUBLIC (ref))
	record_maybe_used_decl (ref);
    }

  if (TREE_CODE (ref) == CONST_DECL)
    {
      used_types_insert (TREE_TYPE (ref));
      ref = DECL_INITIAL (ref);
      TREE_CONSTANT (ref) = 1;
      TREE_INVARIANT (ref) = 1;
    }
  else if (current_function_decl != 0
	   && !DECL_FILE_SCOPE_P (current_function_decl)
	   && (TREE_CODE (ref) == VAR_DECL
	       || TREE_CODE (ref) == PARM_DECL
	       || TREE_CODE (ref) == FUNCTION_DECL))
    {
      tree context = decl_function_context (ref);

      if (context != 0 && context != current_function_decl)
	DECL_NONLOCAL (ref) = 1;
    }

  return ref;
}

/* Record details of decls possibly used inside sizeof or typeof.  */
struct maybe_used_decl
{
  /* The decl.  */
  tree decl;
  /* The level seen at (in_sizeof + in_typeof).  */
  int level;
  /* The next one at this level or above, or NULL.  */
  struct maybe_used_decl *next;
};

static struct maybe_used_decl *maybe_used_decls;

/* Record that DECL, an undefined static function reference seen
   inside sizeof or typeof, might be used if the operand of sizeof is
   a VLA type or the operand of typeof is a variably modified
   type.  */

static void
record_maybe_used_decl (tree decl)
{
  struct maybe_used_decl *t = XOBNEW (&parser_obstack, struct maybe_used_decl);
  t->decl = decl;
  t->level = in_sizeof + in_typeof;
  t->next = maybe_used_decls;
  maybe_used_decls = t;
}

/* Pop the stack of decls possibly used inside sizeof or typeof.  If
   USED is false, just discard them.  If it is true, mark them used
   (if no longer inside sizeof or typeof) or move them to the next
   level up (if still inside sizeof or typeof).  */

void
pop_maybe_used (bool used)
{
  struct maybe_used_decl *p = maybe_used_decls;
  int cur_level = in_sizeof + in_typeof;
  while (p && p->level > cur_level)
    {
      if (used)
	{
	  if (cur_level == 0)
	    C_DECL_USED (p->decl) = 1;
	  else
	    p->level = cur_level;
	}
      p = p->next;
    }
  if (!used || cur_level == 0)
    maybe_used_decls = p;
}

/* Return the result of sizeof applied to EXPR.  */

struct c_expr
c_expr_sizeof_expr (struct c_expr expr)
{
  struct c_expr ret;
  if (expr.value == error_mark_node)
    {
      ret.value = error_mark_node;
      ret.original_code = ERROR_MARK;
      pop_maybe_used (false);
    }
  else
    {
      ret.value = c_sizeof (TREE_TYPE (expr.value));
      ret.original_code = ERROR_MARK;
      if (c_vla_type_p (TREE_TYPE (expr.value)))
	{
	  /* sizeof is evaluated when given a vla (C99 6.5.3.4p2).  */
	  ret.value = build2 (COMPOUND_EXPR, TREE_TYPE (ret.value), expr.value, ret.value);
	}
      pop_maybe_used (C_TYPE_VARIABLE_SIZE (TREE_TYPE (expr.value)));
    }
  return ret;
}

/* Return the result of sizeof applied to T, a structure for the type
   name passed to sizeof (rather than the type itself).  */

struct c_expr
c_expr_sizeof_type (struct c_type_name *t)
{
  tree type;
  struct c_expr ret;
  type = groktypename (t);
  ret.value = c_sizeof (type);
  ret.original_code = ERROR_MARK;
  pop_maybe_used (type != error_mark_node
		  ? C_TYPE_VARIABLE_SIZE (type) : false);
  return ret;
}

/* Build a function call to function FUNCTION with parameters PARAMS.
   PARAMS is a list--a chain of TREE_LIST nodes--in which the
   TREE_VALUE of each node is a parameter-expression.
   FUNCTION's data type may be a function type or a pointer-to-function.  */

tree
build_function_call (tree function, tree params)
{
  tree fntype, fundecl = 0;
  tree coerced_params;
  tree name = NULL_TREE, result;
  tree tem;

  /* Strip NON_LVALUE_EXPRs, etc., since we aren't using as an lvalue.  */
  STRIP_TYPE_NOPS (function);

  /* Convert anything with function type to a pointer-to-function.  */
  if (TREE_CODE (function) == FUNCTION_DECL)
    {
      /* Implement type-directed function overloading for builtins.
	 resolve_overloaded_builtin and targetm.resolve_overloaded_builtin
	 handle all the type checking.  The result is a complete expression
	 that implements this function call.  */
      tem = resolve_overloaded_builtin (function, params);
      if (tem)
	return tem;

      name = DECL_NAME (function);
      fundecl = function;
    }
  if (TREE_CODE (TREE_TYPE (function)) == FUNCTION_TYPE)
    function = function_to_pointer_conversion (function);

  /* For Objective-C, convert any calls via a cast to OBJC_TYPE_REF
     expressions, like those used for ObjC messenger dispatches.  */
  function = objc_rewrite_function_call (function, params);

  fntype = TREE_TYPE (function);

  if (TREE_CODE (fntype) == ERROR_MARK)
    return error_mark_node;

  if (!(TREE_CODE (fntype) == POINTER_TYPE
	&& TREE_CODE (TREE_TYPE (fntype)) == FUNCTION_TYPE))
    {
      error ("called object %qE is not a function", function);
      return error_mark_node;
    }

  if (fundecl && TREE_THIS_VOLATILE (fundecl))
    current_function_returns_abnormally = 1;

  /* fntype now gets the type of function pointed to.  */
  fntype = TREE_TYPE (fntype);

  /* Check that the function is called through a compatible prototype.
     If it is not, replace the call by a trap, wrapped up in a compound
     expression if necessary.  This has the nice side-effect to prevent
     the tree-inliner from generating invalid assignment trees which may
     blow up in the RTL expander later.  */
  if ((TREE_CODE (function) == NOP_EXPR
       || TREE_CODE (function) == CONVERT_EXPR)
      && TREE_CODE (tem = TREE_OPERAND (function, 0)) == ADDR_EXPR
      && TREE_CODE (tem = TREE_OPERAND (tem, 0)) == FUNCTION_DECL
      && !comptypes (fntype, TREE_TYPE (tem)))
    {
      tree return_type = TREE_TYPE (fntype);
      tree trap = build_function_call (built_in_decls[BUILT_IN_TRAP],
				       NULL_TREE);

      /* This situation leads to run-time undefined behavior.  We can't,
	 therefore, simply error unless we can prove that all possible
	 executions of the program must execute the code.  */
      warning (0, "function called through a non-compatible type");

      /* We can, however, treat "undefined" any way we please.
	 Call abort to encourage the user to fix the program.  */
      inform ("if this code is reached, the program will abort");

      if (VOID_TYPE_P (return_type))
	return trap;
      else
	{
	  tree rhs;

	  if (AGGREGATE_TYPE_P (return_type))
	    rhs = build_compound_literal (return_type,
					  build_constructor (return_type, 0));
	  else
	    rhs = fold_convert (return_type, integer_zero_node);

	  return build2 (COMPOUND_EXPR, return_type, trap, rhs);
	}
    }

  /* Convert the parameters to the types declared in the
     function prototype, or apply default promotions.  */

  coerced_params
    = convert_arguments (TYPE_ARG_TYPES (fntype), params, function, fundecl);

  if (coerced_params == error_mark_node)
    return error_mark_node;

  /* Check that the arguments to the function are valid.  */

  check_function_arguments (TYPE_ATTRIBUTES (fntype), coerced_params,
			    TYPE_ARG_TYPES (fntype));

  if (require_constant_value)
    {
      result = fold_build3_initializer (CALL_EXPR, TREE_TYPE (fntype),
					function, coerced_params, NULL_TREE);

      if (TREE_CONSTANT (result)
	  && (name == NULL_TREE
	      || strncmp (IDENTIFIER_POINTER (name), "__builtin_", 10) != 0))
	pedwarn_init ("initializer element is not constant");
    }
  else
    result = fold_build3 (CALL_EXPR, TREE_TYPE (fntype),
			  function, coerced_params, NULL_TREE);

  if (VOID_TYPE_P (TREE_TYPE (result)))
    return result;
  return require_complete_type (result);
}

/* Convert the argument expressions in the list VALUES
   to the types in the list TYPELIST.  The result is a list of converted
   argument expressions, unless there are too few arguments in which
   case it is error_mark_node.

   If TYPELIST is exhausted, or when an element has NULL as its type,
   perform the default conversions.

   PARMLIST is the chain of parm decls for the function being called.
   It may be 0, if that info is not available.
   It is used only for generating error messages.

   FUNCTION is a tree for the called function.  It is used only for
   error messages, where it is formatted with %qE.

   This is also where warnings about wrong number of args are generated.

   Both VALUES and the returned value are chains of TREE_LIST nodes
   with the elements of the list in the TREE_VALUE slots of those nodes.  */

static tree
convert_arguments (tree typelist, tree values, tree function, tree fundecl)
{
  tree typetail, valtail;
  tree result = NULL;
  int parmnum;
  tree selector;

  /* Change pointer to function to the function itself for
     diagnostics.  */
  if (TREE_CODE (function) == ADDR_EXPR
      && TREE_CODE (TREE_OPERAND (function, 0)) == FUNCTION_DECL)
    function = TREE_OPERAND (function, 0);

  /* Handle an ObjC selector specially for diagnostics.  */
  selector = objc_message_selector ();

  /* Scan the given expressions and types, producing individual
     converted arguments and pushing them on RESULT in reverse order.  */

  for (valtail = values, typetail = typelist, parmnum = 0;
       valtail;
       valtail = TREE_CHAIN (valtail), parmnum++)
    {
      tree type = typetail ? TREE_VALUE (typetail) : 0;
      tree val = TREE_VALUE (valtail);
      tree rname = function;
      int argnum = parmnum + 1;
      const char *invalid_func_diag;

      if (type == void_type_node)
	{
	  error ("too many arguments to function %qE", function);
	  break;
	}

      if (selector && argnum > 2)
	{
	  rname = selector;
	  argnum -= 2;
	}

      STRIP_TYPE_NOPS (val);

      val = require_complete_type (val);

      if (type != 0)
	{
	  /* Formal parm type is specified by a function prototype.  */
	  tree parmval;

	  if (type == error_mark_node || !COMPLETE_TYPE_P (type))
	    {
	      error ("type of formal parameter %d is incomplete", parmnum + 1);
	      parmval = val;
	    }
	  else
	    {
	      /* Optionally warn about conversions that
		 differ from the default conversions.  */
	      if (warn_conversion || warn_traditional)
		{
		  unsigned int formal_prec = TYPE_PRECISION (type);

		  if (INTEGRAL_TYPE_P (type)
		      && TREE_CODE (TREE_TYPE (val)) == REAL_TYPE)
		    warning (0, "passing argument %d of %qE as integer "
			     "rather than floating due to prototype",
			     argnum, rname);
		  if (INTEGRAL_TYPE_P (type)
		      && TREE_CODE (TREE_TYPE (val)) == COMPLEX_TYPE)
		    warning (0, "passing argument %d of %qE as integer "
			     "rather than complex due to prototype",
			     argnum, rname);
		  else if (TREE_CODE (type) == COMPLEX_TYPE
			   && TREE_CODE (TREE_TYPE (val)) == REAL_TYPE)
		    warning (0, "passing argument %d of %qE as complex "
			     "rather than floating due to prototype",
			     argnum, rname);
		  else if (TREE_CODE (type) == REAL_TYPE
			   && INTEGRAL_TYPE_P (TREE_TYPE (val)))
		    warning (0, "passing argument %d of %qE as floating "
			     "rather than integer due to prototype",
			     argnum, rname);
		  else if (TREE_CODE (type) == COMPLEX_TYPE
			   && INTEGRAL_TYPE_P (TREE_TYPE (val)))
		    warning (0, "passing argument %d of %qE as complex "
			     "rather than integer due to prototype",
			     argnum, rname);
		  else if (TREE_CODE (type) == REAL_TYPE
			   && TREE_CODE (TREE_TYPE (val)) == COMPLEX_TYPE)
		    warning (0, "passing argument %d of %qE as floating "
			     "rather than complex due to prototype",
			     argnum, rname);
		  /* ??? At some point, messages should be written about
		     conversions between complex types, but that's too messy
		     to do now.  */
		  else if (TREE_CODE (type) == REAL_TYPE
			   && TREE_CODE (TREE_TYPE (val)) == REAL_TYPE)
		    {
		      /* Warn if any argument is passed as `float',
			 since without a prototype it would be `double'.  */
		      if (formal_prec == TYPE_PRECISION (float_type_node)
			  && type != dfloat32_type_node)
			warning (0, "passing argument %d of %qE as %<float%> "
				 "rather than %<double%> due to prototype",
				 argnum, rname);

		      /* Warn if mismatch between argument and prototype
			 for decimal float types.  Warn of conversions with
			 binary float types and of precision narrowing due to
			 prototype. */
 		      else if (type != TREE_TYPE (val)
			       && (type == dfloat32_type_node
				   || type == dfloat64_type_node
				   || type == dfloat128_type_node
				   || TREE_TYPE (val) == dfloat32_type_node
				   || TREE_TYPE (val) == dfloat64_type_node
				   || TREE_TYPE (val) == dfloat128_type_node)
			       && (formal_prec
				   <= TYPE_PRECISION (TREE_TYPE (val))
				   || (type == dfloat128_type_node
				       && (TREE_TYPE (val)
					   != dfloat64_type_node
					   && (TREE_TYPE (val)
					       != dfloat32_type_node)))
				   || (type == dfloat64_type_node
				       && (TREE_TYPE (val)
					   != dfloat32_type_node))))
			warning (0, "passing argument %d of %qE as %qT "
				 "rather than %qT due to prototype",
				 argnum, rname, type, TREE_TYPE (val));

		    }
		  /* Detect integer changing in width or signedness.
		     These warnings are only activated with
		     -Wconversion, not with -Wtraditional.  */
		  else if (warn_conversion && INTEGRAL_TYPE_P (type)
			   && INTEGRAL_TYPE_P (TREE_TYPE (val)))
		    {
		      tree would_have_been = default_conversion (val);
		      tree type1 = TREE_TYPE (would_have_been);

		      if (TREE_CODE (type) == ENUMERAL_TYPE
			  && (TYPE_MAIN_VARIANT (type)
			      == TYPE_MAIN_VARIANT (TREE_TYPE (val))))
			/* No warning if function asks for enum
			   and the actual arg is that enum type.  */
			;
		      else if (formal_prec != TYPE_PRECISION (type1))
			warning (OPT_Wconversion, "passing argument %d of %qE "
				 "with different width due to prototype",
				 argnum, rname);
		      else if (TYPE_UNSIGNED (type) == TYPE_UNSIGNED (type1))
			;
		      /* Don't complain if the formal parameter type
			 is an enum, because we can't tell now whether
			 the value was an enum--even the same enum.  */
		      else if (TREE_CODE (type) == ENUMERAL_TYPE)
			;
		      else if (TREE_CODE (val) == INTEGER_CST
			       && int_fits_type_p (val, type))
			/* Change in signedness doesn't matter
			   if a constant value is unaffected.  */
			;
		      /* If the value is extended from a narrower
			 unsigned type, it doesn't matter whether we
			 pass it as signed or unsigned; the value
			 certainly is the same either way.  */
		      else if (TYPE_PRECISION (TREE_TYPE (val)) < TYPE_PRECISION (type)
			       && TYPE_UNSIGNED (TREE_TYPE (val)))
			;
		      else if (TYPE_UNSIGNED (type))
			warning (OPT_Wconversion, "passing argument %d of %qE "
				 "as unsigned due to prototype",
				 argnum, rname);
		      else
			warning (OPT_Wconversion, "passing argument %d of %qE "
				 "as signed due to prototype", argnum, rname);
		    }
		}

	      parmval = convert_for_assignment (type, val, ic_argpass,
						fundecl, function,
						parmnum + 1);

	      if (targetm.calls.promote_prototypes (fundecl ? TREE_TYPE (fundecl) : 0)
		  && INTEGRAL_TYPE_P (type)
		  && (TYPE_PRECISION (type) < TYPE_PRECISION (integer_type_node)))
		parmval = default_conversion (parmval);
	    }
	  result = tree_cons (NULL_TREE, parmval, result);
	}
      else if (TREE_CODE (TREE_TYPE (val)) == REAL_TYPE
	       && (TYPE_PRECISION (TREE_TYPE (val))
		   < TYPE_PRECISION (double_type_node))
	       && !DECIMAL_FLOAT_MODE_P (TYPE_MODE (TREE_TYPE (val))))
	/* Convert `float' to `double'.  */
	result = tree_cons (NULL_TREE, convert (double_type_node, val), result);
      else if ((invalid_func_diag =
		targetm.calls.invalid_arg_for_unprototyped_fn (typelist, fundecl, val)))
	{
	  error (invalid_func_diag);
	  return error_mark_node;
	}
      else
	/* Convert `short' and `char' to full-size `int'.  */
	result = tree_cons (NULL_TREE, default_conversion (val), result);

      if (typetail)
	typetail = TREE_CHAIN (typetail);
    }

  if (typetail != 0 && TREE_VALUE (typetail) != void_type_node)
    {
      error ("too few arguments to function %qE", function);
      return error_mark_node;
    }

  return nreverse (result);
}

/* This is the entry point used by the parser to build unary operators
   in the input.  CODE, a tree_code, specifies the unary operator, and
   ARG is the operand.  For unary plus, the C parser currently uses
   CONVERT_EXPR for code.  */

struct c_expr
parser_build_unary_op (enum tree_code code, struct c_expr arg)
{
  struct c_expr result;

  result.original_code = ERROR_MARK;
  result.value = build_unary_op (code, arg.value, 0);
  overflow_warning (result.value);
  return result;
}

/* This is the entry point used by the parser to build binary operators
   in the input.  CODE, a tree_code, specifies the binary operator, and
   ARG1 and ARG2 are the operands.  In addition to constructing the
   expression, we check for operands that were written with other binary
   operators in a way that is likely to confuse the user.  */

struct c_expr
parser_build_binary_op (enum tree_code code, struct c_expr arg1,
			struct c_expr arg2)
{
  struct c_expr result;

  enum tree_code code1 = arg1.original_code;
  enum tree_code code2 = arg2.original_code;

  result.value = build_binary_op (code, arg1.value, arg2.value, 1);
  result.original_code = code;

  if (TREE_CODE (result.value) == ERROR_MARK)
    return result;

  /* Check for cases such as x+y<<z which users are likely
     to misinterpret.  */
  if (warn_parentheses)
    {
      if (code == LSHIFT_EXPR || code == RSHIFT_EXPR)
	{
	  if (code1 == PLUS_EXPR || code1 == MINUS_EXPR
	      || code2 == PLUS_EXPR || code2 == MINUS_EXPR)
	    warning (OPT_Wparentheses,
		     "suggest parentheses around + or - inside shift");
	}

      if (code == TRUTH_ORIF_EXPR)
	{
	  if (code1 == TRUTH_ANDIF_EXPR
	      || code2 == TRUTH_ANDIF_EXPR)
	    warning (OPT_Wparentheses,
		     "suggest parentheses around && within ||");
	}

      if (code == BIT_IOR_EXPR)
	{
	  if (code1 == BIT_AND_EXPR || code1 == BIT_XOR_EXPR
	      || code1 == PLUS_EXPR || code1 == MINUS_EXPR
	      || code2 == BIT_AND_EXPR || code2 == BIT_XOR_EXPR
	      || code2 == PLUS_EXPR || code2 == MINUS_EXPR)
	    warning (OPT_Wparentheses,
		     "suggest parentheses around arithmetic in operand of |");
	  /* Check cases like x|y==z */
	  if (TREE_CODE_CLASS (code1) == tcc_comparison
	      || TREE_CODE_CLASS (code2) == tcc_comparison)
	    warning (OPT_Wparentheses,
		     "suggest parentheses around comparison in operand of |");
	}

      if (code == BIT_XOR_EXPR)
	{
	  if (code1 == BIT_AND_EXPR
	      || code1 == PLUS_EXPR || code1 == MINUS_EXPR
	      || code2 == BIT_AND_EXPR
	      || code2 == PLUS_EXPR || code2 == MINUS_EXPR)
	    warning (OPT_Wparentheses,
		     "suggest parentheses around arithmetic in operand of ^");
	  /* Check cases like x^y==z */
	  if (TREE_CODE_CLASS (code1) == tcc_comparison
	      || TREE_CODE_CLASS (code2) == tcc_comparison)
	    warning (OPT_Wparentheses,
		     "suggest parentheses around comparison in operand of ^");
	}

      if (code == BIT_AND_EXPR)
	{
	  if (code1 == PLUS_EXPR || code1 == MINUS_EXPR
	      || code2 == PLUS_EXPR || code2 == MINUS_EXPR)
	    warning (OPT_Wparentheses,
		     "suggest parentheses around + or - in operand of &");
	  /* Check cases like x&y==z */
	  if (TREE_CODE_CLASS (code1) == tcc_comparison
	      || TREE_CODE_CLASS (code2) == tcc_comparison)
	    warning (OPT_Wparentheses,
		     "suggest parentheses around comparison in operand of &");
	}
      /* Similarly, check for cases like 1<=i<=10 that are probably errors.  */
      if (TREE_CODE_CLASS (code) == tcc_comparison
	  && (TREE_CODE_CLASS (code1) == tcc_comparison
	      || TREE_CODE_CLASS (code2) == tcc_comparison))
	warning (OPT_Wparentheses, "comparisons like X<=Y<=Z do not "
		 "have their mathematical meaning");

    }

  /* Warn about comparisons against string literals, with the exception
     of testing for equality or inequality of a string literal with NULL.  */
  if (code == EQ_EXPR || code == NE_EXPR)
    {
      if ((code1 == STRING_CST && !integer_zerop (arg2.value))
	  || (code2 == STRING_CST && !integer_zerop (arg1.value)))
	warning (OPT_Waddress, 
                 "comparison with string literal results in unspecified behaviour");
    }
  else if (TREE_CODE_CLASS (code) == tcc_comparison
	   && (code1 == STRING_CST || code2 == STRING_CST))
    warning (OPT_Waddress, 
             "comparison with string literal results in unspecified behaviour");

  overflow_warning (result.value);

  return result;
}

/* Return a tree for the difference of pointers OP0 and OP1.
   The resulting tree has type int.  */

static tree
pointer_diff (tree op0, tree op1)
{
  tree restype = ptrdiff_type_node;

  tree target_type = TREE_TYPE (TREE_TYPE (op0));
  tree con0, con1, lit0, lit1;
  tree orig_op1 = op1;

  if (pedantic || warn_pointer_arith)
    {
      if (TREE_CODE (target_type) == VOID_TYPE)
	pedwarn ("pointer of type %<void *%> used in subtraction");
      if (TREE_CODE (target_type) == FUNCTION_TYPE)
	pedwarn ("pointer to a function used in subtraction");
    }

  /* If the conversion to ptrdiff_type does anything like widening or
     converting a partial to an integral mode, we get a convert_expression
     that is in the way to do any simplifications.
     (fold-const.c doesn't know that the extra bits won't be needed.
     split_tree uses STRIP_SIGN_NOPS, which leaves conversions to a
     different mode in place.)
     So first try to find a common term here 'by hand'; we want to cover
     at least the cases that occur in legal static initializers.  */
  if ((TREE_CODE (op0) == NOP_EXPR || TREE_CODE (op0) == CONVERT_EXPR)
      && (TYPE_PRECISION (TREE_TYPE (op0))
	  == TYPE_PRECISION (TREE_TYPE (TREE_OPERAND (op0, 0)))))
    con0 = TREE_OPERAND (op0, 0);
  else
    con0 = op0;
  if ((TREE_CODE (op1) == NOP_EXPR || TREE_CODE (op1) == CONVERT_EXPR)
      && (TYPE_PRECISION (TREE_TYPE (op1))
	  == TYPE_PRECISION (TREE_TYPE (TREE_OPERAND (op1, 0)))))
    con1 = TREE_OPERAND (op1, 0);
  else
    con1 = op1;

  if (TREE_CODE (con0) == PLUS_EXPR)
    {
      lit0 = TREE_OPERAND (con0, 1);
      con0 = TREE_OPERAND (con0, 0);
    }
  else
    lit0 = integer_zero_node;

  if (TREE_CODE (con1) == PLUS_EXPR)
    {
      lit1 = TREE_OPERAND (con1, 1);
      con1 = TREE_OPERAND (con1, 0);
    }
  else
    lit1 = integer_zero_node;

  if (operand_equal_p (con0, con1, 0))
    {
      op0 = lit0;
      op1 = lit1;
    }


  /* First do the subtraction as integers;
     then drop through to build the divide operator.
     Do not do default conversions on the minus operator
     in case restype is a short type.  */

  op0 = build_binary_op (MINUS_EXPR, convert (restype, op0),
			 convert (restype, op1), 0);
  /* This generates an error if op1 is pointer to incomplete type.  */
  if (!COMPLETE_OR_VOID_TYPE_P (TREE_TYPE (TREE_TYPE (orig_op1))))
    error ("arithmetic on pointer to an incomplete type");

  /* This generates an error if op0 is pointer to incomplete type.  */
  op1 = c_size_in_bytes (target_type);

  /* Divide by the size, in easiest possible way.  */
  return fold_build2 (EXACT_DIV_EXPR, restype, op0, convert (restype, op1));
}

/* Construct and perhaps optimize a tree representation
   for a unary operation.  CODE, a tree_code, specifies the operation
   and XARG is the operand.
   For any CODE other than ADDR_EXPR, FLAG nonzero suppresses
   the default promotions (such as from short to int).
   For ADDR_EXPR, the default promotions are not applied; FLAG nonzero
   allows non-lvalues; this is only used to handle conversion of non-lvalue
   arrays to pointers in C99.  */

tree
build_unary_op (enum tree_code code, tree xarg, int flag)
{
  /* No default_conversion here.  It causes trouble for ADDR_EXPR.  */
  tree arg = xarg;
  tree argtype = 0;
  enum tree_code typecode = TREE_CODE (TREE_TYPE (arg));
  tree val;
  int noconvert = flag;
  const char *invalid_op_diag;

  if (typecode == ERROR_MARK)
    return error_mark_node;
  if (typecode == ENUMERAL_TYPE || typecode == BOOLEAN_TYPE)
    typecode = INTEGER_TYPE;

  if ((invalid_op_diag
       = targetm.invalid_unary_op (code, TREE_TYPE (xarg))))
    {
      error (invalid_op_diag);
      return error_mark_node;
    }

  switch (code)
    {
    case CONVERT_EXPR:
      /* This is used for unary plus, because a CONVERT_EXPR
	 is enough to prevent anybody from looking inside for
	 associativity, but won't generate any code.  */
      if (!(typecode == INTEGER_TYPE || typecode == REAL_TYPE
	    || typecode == COMPLEX_TYPE
	    || typecode == VECTOR_TYPE))
	{
	  error ("wrong type argument to unary plus");
	  return error_mark_node;
	}
      else if (!noconvert)
	arg = default_conversion (arg);
      arg = non_lvalue (arg);
      break;

    case NEGATE_EXPR:
      if (!(typecode == INTEGER_TYPE || typecode == REAL_TYPE
	    || typecode == COMPLEX_TYPE
	    || typecode == VECTOR_TYPE))
	{
	  error ("wrong type argument to unary minus");
	  return error_mark_node;
	}
      else if (!noconvert)
	arg = default_conversion (arg);
      break;

    case BIT_NOT_EXPR:
      if (typecode == INTEGER_TYPE || typecode == VECTOR_TYPE)
	{
	  if (!noconvert)
	    arg = default_conversion (arg);
	}
      else if (typecode == COMPLEX_TYPE)
	{
	  code = CONJ_EXPR;
	  if (pedantic)
	    pedwarn ("ISO C does not support %<~%> for complex conjugation");
	  if (!noconvert)
	    arg = default_conversion (arg);
	}
      else
	{
	  error ("wrong type argument to bit-complement");
	  return error_mark_node;
	}
      break;

    case ABS_EXPR:
      if (!(typecode == INTEGER_TYPE || typecode == REAL_TYPE))
	{
	  error ("wrong type argument to abs");
	  return error_mark_node;
	}
      else if (!noconvert)
	arg = default_conversion (arg);
      break;

    case CONJ_EXPR:
      /* Conjugating a real value is a no-op, but allow it anyway.  */
      if (!(typecode == INTEGER_TYPE || typecode == REAL_TYPE
	    || typecode == COMPLEX_TYPE))
	{
	  error ("wrong type argument to conjugation");
	  return error_mark_node;
	}
      else if (!noconvert)
	arg = default_conversion (arg);
      break;

    case TRUTH_NOT_EXPR:
      if (typecode != INTEGER_TYPE
	  && typecode != REAL_TYPE && typecode != POINTER_TYPE
	  && typecode != COMPLEX_TYPE)
	{
	  error ("wrong type argument to unary exclamation mark");
	  return error_mark_node;
	}
      arg = c_objc_common_truthvalue_conversion (arg);
      return invert_truthvalue (arg);

    case REALPART_EXPR:
      if (TREE_CODE (arg) == COMPLEX_CST)
	return TREE_REALPART (arg);
      else if (TREE_CODE (TREE_TYPE (arg)) == COMPLEX_TYPE)
	return fold_build1 (REALPART_EXPR, TREE_TYPE (TREE_TYPE (arg)), arg);
      else
	return arg;

    case IMAGPART_EXPR:
      if (TREE_CODE (arg) == COMPLEX_CST)
	return TREE_IMAGPART (arg);
      else if (TREE_CODE (TREE_TYPE (arg)) == COMPLEX_TYPE)
	return fold_build1 (IMAGPART_EXPR, TREE_TYPE (TREE_TYPE (arg)), arg);
      else
	return convert (TREE_TYPE (arg), integer_zero_node);

    case PREINCREMENT_EXPR:
    case POSTINCREMENT_EXPR:
    case PREDECREMENT_EXPR:
    case POSTDECREMENT_EXPR:

      /* Increment or decrement the real part of the value,
	 and don't change the imaginary part.  */
      if (typecode == COMPLEX_TYPE)
	{
	  tree real, imag;

	  if (pedantic)
	    pedwarn ("ISO C does not support %<++%> and %<--%>"
		     " on complex types");

	  arg = stabilize_reference (arg);
	  real = build_unary_op (REALPART_EXPR, arg, 1);
	  imag = build_unary_op (IMAGPART_EXPR, arg, 1);
	  return build2 (COMPLEX_EXPR, TREE_TYPE (arg),
			 build_unary_op (code, real, 1), imag);
	}

      /* Report invalid types.  */

      if (typecode != POINTER_TYPE
	  && typecode != INTEGER_TYPE && typecode != REAL_TYPE)
	{
	  if (code == PREINCREMENT_EXPR || code == POSTINCREMENT_EXPR)
	    error ("wrong type argument to increment");
	  else
	    error ("wrong type argument to decrement");

	  return error_mark_node;
	}

      {
	tree inc;
	tree result_type = TREE_TYPE (arg);

	arg = get_unwidened (arg, 0);
	argtype = TREE_TYPE (arg);

	/* Compute the increment.  */

	if (typecode == POINTER_TYPE)
	  {
	    /* If pointer target is an undefined struct,
	       we just cannot know how to do the arithmetic.  */
	    if (!COMPLETE_OR_VOID_TYPE_P (TREE_TYPE (result_type)))
	      {
		if (code == PREINCREMENT_EXPR || code == POSTINCREMENT_EXPR)
		  error ("increment of pointer to unknown structure");
		else
		  error ("decrement of pointer to unknown structure");
	      }
	    else if ((pedantic || warn_pointer_arith)
		     && (TREE_CODE (TREE_TYPE (result_type)) == FUNCTION_TYPE
			 || TREE_CODE (TREE_TYPE (result_type)) == VOID_TYPE))
	      {
		if (code == PREINCREMENT_EXPR || code == POSTINCREMENT_EXPR)
		  pedwarn ("wrong type argument to increment");
		else
		  pedwarn ("wrong type argument to decrement");
	      }

	    inc = c_size_in_bytes (TREE_TYPE (result_type));
	  }
	else
	  inc = integer_one_node;

	inc = convert (argtype, inc);

	/* Complain about anything else that is not a true lvalue.  */
	if (!lvalue_or_else (arg, ((code == PREINCREMENT_EXPR
				    || code == POSTINCREMENT_EXPR)
				   ? lv_increment
				   : lv_decrement)))
	  return error_mark_node;

	/* Report a read-only lvalue.  */
	if (TREE_READONLY (arg))
	  {
	    readonly_error (arg,
			    ((code == PREINCREMENT_EXPR
			      || code == POSTINCREMENT_EXPR)
			     ? lv_increment : lv_decrement));
	    return error_mark_node;
	  }

	if (TREE_CODE (TREE_TYPE (arg)) == BOOLEAN_TYPE)
	  val = boolean_increment (code, arg);
	else
	  val = build2 (code, TREE_TYPE (arg), arg, inc);
	TREE_SIDE_EFFECTS (val) = 1;
	val = convert (result_type, val);
	if (TREE_CODE (val) != code)
	  TREE_NO_WARNING (val) = 1;
	return val;
      }

    case ADDR_EXPR:
      /* Note that this operation never does default_conversion.  */

      /* Let &* cancel out to simplify resulting code.  */
      if (TREE_CODE (arg) == INDIRECT_REF)
	{
	  /* Don't let this be an lvalue.  */
	  if (lvalue_p (TREE_OPERAND (arg, 0)))
	    return non_lvalue (TREE_OPERAND (arg, 0));
	  return TREE_OPERAND (arg, 0);
	}

      /* For &x[y], return x+y */
      if (TREE_CODE (arg) == ARRAY_REF)
	{
	  tree op0 = TREE_OPERAND (arg, 0);
	  if (!c_mark_addressable (op0))
	    return error_mark_node;
	  return build_binary_op (PLUS_EXPR,
				  (TREE_CODE (TREE_TYPE (op0)) == ARRAY_TYPE
				   ? array_to_pointer_conversion (op0)
				   : op0),
				  TREE_OPERAND (arg, 1), 1);
	}

      /* Anything not already handled and not a true memory reference
	 or a non-lvalue array is an error.  */
      else if (typecode != FUNCTION_TYPE && !flag
	       && !lvalue_or_else (arg, lv_addressof))
	return error_mark_node;

      /* Ordinary case; arg is a COMPONENT_REF or a decl.  */
      argtype = TREE_TYPE (arg);

      /* If the lvalue is const or volatile, merge that into the type
	 to which the address will point.  Note that you can't get a
	 restricted pointer by taking the address of something, so we
	 only have to deal with `const' and `volatile' here.  */
      if ((DECL_P (arg) || REFERENCE_CLASS_P (arg))
	  && (TREE_READONLY (arg) || TREE_THIS_VOLATILE (arg)))
	  argtype = c_build_type_variant (argtype,
					  TREE_READONLY (arg),
					  TREE_THIS_VOLATILE (arg));

      if (!c_mark_addressable (arg))
	return error_mark_node;

      gcc_assert (TREE_CODE (arg) != COMPONENT_REF
		  || !DECL_C_BIT_FIELD (TREE_OPERAND (arg, 1)));

      argtype = build_pointer_type (argtype);

      /* ??? Cope with user tricks that amount to offsetof.  Delete this
	 when we have proper support for integer constant expressions.  */
      val = get_base_address (arg);
      if (val && TREE_CODE (val) == INDIRECT_REF
          && TREE_CONSTANT (TREE_OPERAND (val, 0)))
	{
	  tree op0 = fold_convert (argtype, fold_offsetof (arg, val)), op1;

	  op1 = fold_convert (argtype, TREE_OPERAND (val, 0));
	  return fold_build2 (PLUS_EXPR, argtype, op0, op1);
	}

      val = build1 (ADDR_EXPR, argtype, arg);

      return val;

    default:
      gcc_unreachable ();
    }

  if (argtype == 0)
    argtype = TREE_TYPE (arg);
  return require_constant_value ? fold_build1_initializer (code, argtype, arg)
				: fold_build1 (code, argtype, arg);
}

/* Return nonzero if REF is an lvalue valid for this language.
   Lvalues can be assigned, unless their type has TYPE_READONLY.
   Lvalues can have their address taken, unless they have C_DECL_REGISTER.  */

static int
lvalue_p (tree ref)
{
  enum tree_code code = TREE_CODE (ref);

  switch (code)
    {
    case REALPART_EXPR:
    case IMAGPART_EXPR:
    case COMPONENT_REF:
      return lvalue_p (TREE_OPERAND (ref, 0));

    case COMPOUND_LITERAL_EXPR:
    case STRING_CST:
      return 1;

    case INDIRECT_REF:
    case ARRAY_REF:
    case VAR_DECL:
    case PARM_DECL:
    case RESULT_DECL:
    case ERROR_MARK:
      return (TREE_CODE (TREE_TYPE (ref)) != FUNCTION_TYPE
	      && TREE_CODE (TREE_TYPE (ref)) != METHOD_TYPE);

    case BIND_EXPR:
      return TREE_CODE (TREE_TYPE (ref)) == ARRAY_TYPE;

    default:
      return 0;
    }
}

/* Give an error for storing in something that is 'const'.  */

static void
readonly_error (tree arg, enum lvalue_use use)
{
  gcc_assert (use == lv_assign || use == lv_increment || use == lv_decrement
	      || use == lv_asm);
  /* Using this macro rather than (for example) arrays of messages
     ensures that all the format strings are checked at compile
     time.  */
#define READONLY_MSG(A, I, D, AS) (use == lv_assign ? (A)		\
				   : (use == lv_increment ? (I)		\
				   : (use == lv_decrement ? (D) : (AS))))
  if (TREE_CODE (arg) == COMPONENT_REF)
    {
      if (TYPE_READONLY (TREE_TYPE (TREE_OPERAND (arg, 0))))
	readonly_error (TREE_OPERAND (arg, 0), use);
      else
	error (READONLY_MSG (G_("assignment of read-only member %qD"),
			     G_("increment of read-only member %qD"),
			     G_("decrement of read-only member %qD"),
			     G_("read-only member %qD used as %<asm%> output")),
	       TREE_OPERAND (arg, 1));
    }
  else if (TREE_CODE (arg) == VAR_DECL)
    error (READONLY_MSG (G_("assignment of read-only variable %qD"),
			 G_("increment of read-only variable %qD"),
			 G_("decrement of read-only variable %qD"),
			 G_("read-only variable %qD used as %<asm%> output")),
	   arg);
  else
    error (READONLY_MSG (G_("assignment of read-only location"),
			 G_("increment of read-only location"),
			 G_("decrement of read-only location"),
			 G_("read-only location used as %<asm%> output")));
}


/* Return nonzero if REF is an lvalue valid for this language;
   otherwise, print an error message and return zero.  USE says
   how the lvalue is being used and so selects the error message.  */

static int
lvalue_or_else (tree ref, enum lvalue_use use)
{
  int win = lvalue_p (ref);

  if (!win)
    lvalue_error (use);

  return win;
}

/* Mark EXP saying that we need to be able to take the
   address of it; it should not be allocated in a register.
   Returns true if successful.  */

bool
c_mark_addressable (tree exp)
{
  tree x = exp;

  while (1)
    switch (TREE_CODE (x))
      {
      case COMPONENT_REF:
	if (DECL_C_BIT_FIELD (TREE_OPERAND (x, 1)))
	  {
	    error
	      ("cannot take address of bit-field %qD", TREE_OPERAND (x, 1));
	    return false;
	  }

	/* ... fall through ...  */

      case ADDR_EXPR:
      case ARRAY_REF:
      case REALPART_EXPR:
      case IMAGPART_EXPR:
	x = TREE_OPERAND (x, 0);
	break;

      case COMPOUND_LITERAL_EXPR:
      case CONSTRUCTOR:
	TREE_ADDRESSABLE (x) = 1;
	return true;

      case VAR_DECL:
      case CONST_DECL:
      case PARM_DECL:
      case RESULT_DECL:
	if (C_DECL_REGISTER (x)
	    && DECL_NONLOCAL (x))
	  {
	    if (TREE_PUBLIC (x) || TREE_STATIC (x) || DECL_EXTERNAL (x))
	      {
		error
		  ("global register variable %qD used in nested function", x);
		return false;
	      }
	    pedwarn ("register variable %qD used in nested function", x);
	  }
	else if (C_DECL_REGISTER (x))
	  {
	    if (TREE_PUBLIC (x) || TREE_STATIC (x) || DECL_EXTERNAL (x))
	      error ("address of global register variable %qD requested", x);
	    else
	      error ("address of register variable %qD requested", x);
	    return false;
	  }

	/* drops in */
      case FUNCTION_DECL:
	TREE_ADDRESSABLE (x) = 1;
	/* drops out */
      default:
	return true;
    }
}

/* Build and return a conditional expression IFEXP ? OP1 : OP2.  */

tree
build_conditional_expr (tree ifexp, tree op1, tree op2)
{
  tree type1;
  tree type2;
  enum tree_code code1;
  enum tree_code code2;
  tree result_type = NULL;
  tree orig_op1 = op1, orig_op2 = op2;

  /* Promote both alternatives.  */

  if (TREE_CODE (TREE_TYPE (op1)) != VOID_TYPE)
    op1 = default_conversion (op1);
  if (TREE_CODE (TREE_TYPE (op2)) != VOID_TYPE)
    op2 = default_conversion (op2);

  if (TREE_CODE (ifexp) == ERROR_MARK
      || TREE_CODE (TREE_TYPE (op1)) == ERROR_MARK
      || TREE_CODE (TREE_TYPE (op2)) == ERROR_MARK)
    return error_mark_node;

  type1 = TREE_TYPE (op1);
  code1 = TREE_CODE (type1);
  type2 = TREE_TYPE (op2);
  code2 = TREE_CODE (type2);

  /* C90 does not permit non-lvalue arrays in conditional expressions.
     In C99 they will be pointers by now.  */
  if (code1 == ARRAY_TYPE || code2 == ARRAY_TYPE)
    {
      error ("non-lvalue array in conditional expression");
      return error_mark_node;
    }

  /* Quickly detect the usual case where op1 and op2 have the same type
     after promotion.  */
  if (TYPE_MAIN_VARIANT (type1) == TYPE_MAIN_VARIANT (type2))
    {
      if (type1 == type2)
	result_type = type1;
      else
	result_type = TYPE_MAIN_VARIANT (type1);
    }
  else if ((code1 == INTEGER_TYPE || code1 == REAL_TYPE
	    || code1 == COMPLEX_TYPE)
	   && (code2 == INTEGER_TYPE || code2 == REAL_TYPE
	       || code2 == COMPLEX_TYPE))
    {
      result_type = c_common_type (type1, type2);

      /* If -Wsign-compare, warn here if type1 and type2 have
	 different signedness.  We'll promote the signed to unsigned
	 and later code won't know it used to be different.
	 Do this check on the original types, so that explicit casts
	 will be considered, but default promotions won't.  */
      if (warn_sign_compare && !skip_evaluation)
	{
	  int unsigned_op1 = TYPE_UNSIGNED (TREE_TYPE (orig_op1));
	  int unsigned_op2 = TYPE_UNSIGNED (TREE_TYPE (orig_op2));

	  if (unsigned_op1 ^ unsigned_op2)
	    {
	      bool ovf;

	      /* Do not warn if the result type is signed, since the
		 signed type will only be chosen if it can represent
		 all the values of the unsigned type.  */
	      if (!TYPE_UNSIGNED (result_type))
		/* OK */;
	      /* Do not warn if the signed quantity is an unsuffixed
		 integer literal (or some static constant expression
		 involving such literals) and it is non-negative.  */
	      else if ((unsigned_op2
			&& tree_expr_nonnegative_warnv_p (op1, &ovf))
		       || (unsigned_op1
			   && tree_expr_nonnegative_warnv_p (op2, &ovf)))
		/* OK */;
	      else
		warning (0, "signed and unsigned type in conditional expression");
	    }
	}
    }
  else if (code1 == VOID_TYPE || code2 == VOID_TYPE)
    {
      if (pedantic && (code1 != VOID_TYPE || code2 != VOID_TYPE))
	pedwarn ("ISO C forbids conditional expr with only one void side");
      result_type = void_type_node;
    }
  else if (code1 == POINTER_TYPE && code2 == POINTER_TYPE)
    {
      if (comp_target_types (type1, type2))
	result_type = common_pointer_type (type1, type2);
      else if (null_pointer_constant_p (orig_op1))
	result_type = qualify_type (type2, type1);
      else if (null_pointer_constant_p (orig_op2))
	result_type = qualify_type (type1, type2);
      else if (VOID_TYPE_P (TREE_TYPE (type1)))
	{
	  if (pedantic && TREE_CODE (TREE_TYPE (type2)) == FUNCTION_TYPE)
	    pedwarn ("ISO C forbids conditional expr between "
		     "%<void *%> and function pointer");
	  result_type = build_pointer_type (qualify_type (TREE_TYPE (type1),
							  TREE_TYPE (type2)));
	}
      else if (VOID_TYPE_P (TREE_TYPE (type2)))
	{
	  if (pedantic && TREE_CODE (TREE_TYPE (type1)) == FUNCTION_TYPE)
	    pedwarn ("ISO C forbids conditional expr between "
		     "%<void *%> and function pointer");
	  result_type = build_pointer_type (qualify_type (TREE_TYPE (type2),
							  TREE_TYPE (type1)));
	}
      else
	{
	  pedwarn ("pointer type mismatch in conditional expression");
	  result_type = build_pointer_type (void_type_node);
	}
    }
  else if (code1 == POINTER_TYPE && code2 == INTEGER_TYPE)
    {
      if (!null_pointer_constant_p (orig_op2))
	pedwarn ("pointer/integer type mismatch in conditional expression");
      else
	{
	  op2 = null_pointer_node;
	}
      result_type = type1;
    }
  else if (code2 == POINTER_TYPE && code1 == INTEGER_TYPE)
    {
      if (!null_pointer_constant_p (orig_op1))
	pedwarn ("pointer/integer type mismatch in conditional expression");
      else
	{
	  op1 = null_pointer_node;
	}
      result_type = type2;
    }

  if (!result_type)
    {
      if (flag_cond_mismatch)
	result_type = void_type_node;
      else
	{
	  error ("type mismatch in conditional expression");
	  return error_mark_node;
	}
    }

  /* Merge const and volatile flags of the incoming types.  */
  result_type
    = build_type_variant (result_type,
			  TREE_READONLY (op1) || TREE_READONLY (op2),
			  TREE_THIS_VOLATILE (op1) || TREE_THIS_VOLATILE (op2));

  if (result_type != TREE_TYPE (op1))
    op1 = convert_and_check (result_type, op1);
  if (result_type != TREE_TYPE (op2))
    op2 = convert_and_check (result_type, op2);

  return fold_build3 (COND_EXPR, result_type, ifexp, op1, op2);
}

/* Return a compound expression that performs two expressions and
   returns the value of the second of them.  */

tree
build_compound_expr (tree expr1, tree expr2)
{
  if (!TREE_SIDE_EFFECTS (expr1))
    {
      /* The left-hand operand of a comma expression is like an expression
	 statement: with -Wextra or -Wunused, we should warn if it doesn't have
	 any side-effects, unless it was explicitly cast to (void).  */
      if (warn_unused_value)
	{
	  if (VOID_TYPE_P (TREE_TYPE (expr1))
	      && (TREE_CODE (expr1) == NOP_EXPR
		  || TREE_CODE (expr1) == CONVERT_EXPR))
	    ; /* (void) a, b */
	  else if (VOID_TYPE_P (TREE_TYPE (expr1))
		   && TREE_CODE (expr1) == COMPOUND_EXPR
		   && (TREE_CODE (TREE_OPERAND (expr1, 1)) == CONVERT_EXPR
		       || TREE_CODE (TREE_OPERAND (expr1, 1)) == NOP_EXPR))
	    ; /* (void) a, (void) b, c */
	  else
	    warning (0, "left-hand operand of comma expression has no effect");
	}
    }

  /* With -Wunused, we should also warn if the left-hand operand does have
     side-effects, but computes a value which is not used.  For example, in
     `foo() + bar(), baz()' the result of the `+' operator is not used,
     so we should issue a warning.  */
  else if (warn_unused_value)
    warn_if_unused_value (expr1, input_location);

  if (expr2 == error_mark_node)
    return error_mark_node;

  return build2 (COMPOUND_EXPR, TREE_TYPE (expr2), expr1, expr2);
}

/* Build an expression representing a cast to type TYPE of expression EXPR.  */

tree
build_c_cast (tree type, tree expr)
{
  tree value = expr;

  if (type == error_mark_node || expr == error_mark_node)
    return error_mark_node;

  /* The ObjC front-end uses TYPE_MAIN_VARIANT to tie together types differing
     only in <protocol> qualifications.  But when constructing cast expressions,
     the protocols do matter and must be kept around.  */
  if (objc_is_object_ptr (type) && objc_is_object_ptr (TREE_TYPE (expr)))
    return build1 (NOP_EXPR, type, expr);

  type = TYPE_MAIN_VARIANT (type);

  if (TREE_CODE (type) == ARRAY_TYPE)
    {
      error ("cast specifies array type");
      return error_mark_node;
    }

  if (TREE_CODE (type) == FUNCTION_TYPE)
    {
      error ("cast specifies function type");
      return error_mark_node;
    }

  if (type == TYPE_MAIN_VARIANT (TREE_TYPE (value)))
    {
      if (pedantic)
	{
	  if (TREE_CODE (type) == RECORD_TYPE
	      || TREE_CODE (type) == UNION_TYPE)
	    pedwarn ("ISO C forbids casting nonscalar to the same type");
	}
    }
  else if (TREE_CODE (type) == UNION_TYPE)
    {
      tree field;

      for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
	if (comptypes (TYPE_MAIN_VARIANT (TREE_TYPE (field)),
		       TYPE_MAIN_VARIANT (TREE_TYPE (value))))
	  break;

      if (field)
	{
	  tree t;

	  if (pedantic)
	    pedwarn ("ISO C forbids casts to union type");
	  t = digest_init (type,
			   build_constructor_single (type, field, value),
			   true, 0);
	  TREE_CONSTANT (t) = TREE_CONSTANT (value);
	  TREE_INVARIANT (t) = TREE_INVARIANT (value);
	  return t;
	}
      error ("cast to union type from type not present in union");
      return error_mark_node;
    }
  else
    {
      tree otype, ovalue;

      if (type == void_type_node)
	return build1 (CONVERT_EXPR, type, value);

      otype = TREE_TYPE (value);

      /* Optionally warn about potentially worrisome casts.  */

      if (warn_cast_qual
	  && TREE_CODE (type) == POINTER_TYPE
	  && TREE_CODE (otype) == POINTER_TYPE)
	{
	  tree in_type = type;
	  tree in_otype = otype;
	  int added = 0;
	  int discarded = 0;

	  /* Check that the qualifiers on IN_TYPE are a superset of
	     the qualifiers of IN_OTYPE.  The outermost level of
	     POINTER_TYPE nodes is uninteresting and we stop as soon
	     as we hit a non-POINTER_TYPE node on either type.  */
	  do
	    {
	      in_otype = TREE_TYPE (in_otype);
	      in_type = TREE_TYPE (in_type);

	      /* GNU C allows cv-qualified function types.  'const'
		 means the function is very pure, 'volatile' means it
		 can't return.  We need to warn when such qualifiers
		 are added, not when they're taken away.  */
	      if (TREE_CODE (in_otype) == FUNCTION_TYPE
		  && TREE_CODE (in_type) == FUNCTION_TYPE)
		added |= (TYPE_QUALS (in_type) & ~TYPE_QUALS (in_otype));
	      else
		discarded |= (TYPE_QUALS (in_otype) & ~TYPE_QUALS (in_type));
	    }
	  while (TREE_CODE (in_type) == POINTER_TYPE
		 && TREE_CODE (in_otype) == POINTER_TYPE);

	  if (added)
	    warning (0, "cast adds new qualifiers to function type");

	  if (discarded)
	    /* There are qualifiers present in IN_OTYPE that are not
	       present in IN_TYPE.  */
	    warning (0, "cast discards qualifiers from pointer target type");
	}

      /* Warn about possible alignment problems.  */
      if (STRICT_ALIGNMENT
	  && TREE_CODE (type) == POINTER_TYPE
	  && TREE_CODE (otype) == POINTER_TYPE
	  && TREE_CODE (TREE_TYPE (otype)) != VOID_TYPE
	  && TREE_CODE (TREE_TYPE (otype)) != FUNCTION_TYPE
	  /* Don't warn about opaque types, where the actual alignment
	     restriction is unknown.  */
	  && !((TREE_CODE (TREE_TYPE (otype)) == UNION_TYPE
		|| TREE_CODE (TREE_TYPE (otype)) == RECORD_TYPE)
	       && TYPE_MODE (TREE_TYPE (otype)) == VOIDmode)
	  && TYPE_ALIGN (TREE_TYPE (type)) > TYPE_ALIGN (TREE_TYPE (otype)))
	warning (OPT_Wcast_align,
		 "cast increases required alignment of target type");

      if (TREE_CODE (type) == INTEGER_TYPE
	  && TREE_CODE (otype) == POINTER_TYPE
	  && TYPE_PRECISION (type) != TYPE_PRECISION (otype))
      /* Unlike conversion of integers to pointers, where the
         warning is disabled for converting constants because
         of cases such as SIG_*, warn about converting constant
         pointers to integers. In some cases it may cause unwanted
         sign extension, and a warning is appropriate.  */
	warning (OPT_Wpointer_to_int_cast,
		 "cast from pointer to integer of different size");

      if (TREE_CODE (value) == CALL_EXPR
	  && TREE_CODE (type) != TREE_CODE (otype))
	warning (OPT_Wbad_function_cast, "cast from function call of type %qT "
		 "to non-matching type %qT", otype, type);

      if (TREE_CODE (type) == POINTER_TYPE
	  && TREE_CODE (otype) == INTEGER_TYPE
	  && TYPE_PRECISION (type) != TYPE_PRECISION (otype)
	  /* Don't warn about converting any constant.  */
	  && !TREE_CONSTANT (value))
	warning (OPT_Wint_to_pointer_cast, "cast to pointer from integer "
		 "of different size");

      strict_aliasing_warning (otype, type, expr);

      /* If pedantic, warn for conversions between function and object
	 pointer types, except for converting a null pointer constant
	 to function pointer type.  */
      if (pedantic
	  && TREE_CODE (type) == POINTER_TYPE
	  && TREE_CODE (otype) == POINTER_TYPE
	  && TREE_CODE (TREE_TYPE (otype)) == FUNCTION_TYPE
	  && TREE_CODE (TREE_TYPE (type)) != FUNCTION_TYPE)
	pedwarn ("ISO C forbids conversion of function pointer to object pointer type");

      if (pedantic
	  && TREE_CODE (type) == POINTER_TYPE
	  && TREE_CODE (otype) == POINTER_TYPE
	  && TREE_CODE (TREE_TYPE (type)) == FUNCTION_TYPE
	  && TREE_CODE (TREE_TYPE (otype)) != FUNCTION_TYPE
	  && !null_pointer_constant_p (value))
	pedwarn ("ISO C forbids conversion of object pointer to function pointer type");

      ovalue = value;
      value = convert (type, value);

      /* Ignore any integer overflow caused by the cast.  */
      if (TREE_CODE (value) == INTEGER_CST)
	{
	  if (CONSTANT_CLASS_P (ovalue)
	      && (TREE_OVERFLOW (ovalue) || TREE_CONSTANT_OVERFLOW (ovalue)))
	    {
	      /* Avoid clobbering a shared constant.  */
	      value = copy_node (value);
	      TREE_OVERFLOW (value) = TREE_OVERFLOW (ovalue);
	      TREE_CONSTANT_OVERFLOW (value) = TREE_CONSTANT_OVERFLOW (ovalue);
	    }
	  else if (TREE_OVERFLOW (value) || TREE_CONSTANT_OVERFLOW (value))
	    /* Reset VALUE's overflow flags, ensuring constant sharing.  */
	    value = build_int_cst_wide (TREE_TYPE (value),
					TREE_INT_CST_LOW (value),
					TREE_INT_CST_HIGH (value));
	}
    }

  /* Don't let a cast be an lvalue.  */
  if (value == expr)
    value = non_lvalue (value);

  return value;
}

/* Interpret a cast of expression EXPR to type TYPE.  */
tree
c_cast_expr (struct c_type_name *type_name, tree expr)
{
  tree type;
  int saved_wsp = warn_strict_prototypes;

  /* This avoids warnings about unprototyped casts on
     integers.  E.g. "#define SIG_DFL (void(*)())0".  */
  if (TREE_CODE (expr) == INTEGER_CST)
    warn_strict_prototypes = 0;
  type = groktypename (type_name);
  warn_strict_prototypes = saved_wsp;

  return build_c_cast (type, expr);
}

/* Build an assignment expression of lvalue LHS from value RHS.
   MODIFYCODE is the code for a binary operator that we use
   to combine the old value of LHS with RHS to get the new value.
   Or else MODIFYCODE is NOP_EXPR meaning do a simple assignment.  */

tree
build_modify_expr (tree lhs, enum tree_code modifycode, tree rhs)
{
  tree result;
  tree newrhs;
  tree lhstype = TREE_TYPE (lhs);
  tree olhstype = lhstype;

  /* Types that aren't fully specified cannot be used in assignments.  */
  lhs = require_complete_type (lhs);

  /* Avoid duplicate error messages from operands that had errors.  */
  if (TREE_CODE (lhs) == ERROR_MARK || TREE_CODE (rhs) == ERROR_MARK)
    return error_mark_node;

  if (!lvalue_or_else (lhs, lv_assign))
    return error_mark_node;

  STRIP_TYPE_NOPS (rhs);

  newrhs = rhs;

  /* If a binary op has been requested, combine the old LHS value with the RHS
     producing the value we should actually store into the LHS.  */

  if (modifycode != NOP_EXPR)
    {
      lhs = stabilize_reference (lhs);
      newrhs = build_binary_op (modifycode, lhs, rhs, 1);
    }

  /* Give an error for storing in something that is 'const'.  */

  if (TREE_READONLY (lhs) || TYPE_READONLY (lhstype)
      || ((TREE_CODE (lhstype) == RECORD_TYPE
	   || TREE_CODE (lhstype) == UNION_TYPE)
	  && C_TYPE_FIELDS_READONLY (lhstype)))
    {
      readonly_error (lhs, lv_assign);
      return error_mark_node;
    }

  /* If storing into a structure or union member,
     it has probably been given type `int'.
     Compute the type that would go with
     the actual amount of storage the member occupies.  */

  if (TREE_CODE (lhs) == COMPONENT_REF
      && (TREE_CODE (lhstype) == INTEGER_TYPE
	  || TREE_CODE (lhstype) == BOOLEAN_TYPE
	  || TREE_CODE (lhstype) == REAL_TYPE
	  || TREE_CODE (lhstype) == ENUMERAL_TYPE))
    lhstype = TREE_TYPE (get_unwidened (lhs, 0));

  /* If storing in a field that is in actuality a short or narrower than one,
     we must store in the field in its actual type.  */

  if (lhstype != TREE_TYPE (lhs))
    {
      lhs = copy_node (lhs);
      TREE_TYPE (lhs) = lhstype;
    }

  /* Convert new value to destination type.  */

  newrhs = convert_for_assignment (lhstype, newrhs, ic_assign,
				   NULL_TREE, NULL_TREE, 0);
  if (TREE_CODE (newrhs) == ERROR_MARK)
    return error_mark_node;

  /* Emit ObjC write barrier, if necessary.  */
  if (c_dialect_objc () && flag_objc_gc)
    {
      result = objc_generate_write_barrier (lhs, modifycode, newrhs);
      if (result)
	return result;
    }

  /* Scan operands.  */

  result = build2 (MODIFY_EXPR, lhstype, lhs, newrhs);
  TREE_SIDE_EFFECTS (result) = 1;

  /* If we got the LHS in a different type for storing in,
     convert the result back to the nominal type of LHS
     so that the value we return always has the same type
     as the LHS argument.  */

  if (olhstype == TREE_TYPE (result))
    return result;
  return convert_for_assignment (olhstype, result, ic_assign,
				 NULL_TREE, NULL_TREE, 0);
}

/* Convert value RHS to type TYPE as preparation for an assignment
   to an lvalue of type TYPE.
   The real work of conversion is done by `convert'.
   The purpose of this function is to generate error messages
   for assignments that are not allowed in C.
   ERRTYPE says whether it is argument passing, assignment,
   initialization or return.

   FUNCTION is a tree for the function being called.
   PARMNUM is the number of the argument, for printing in error messages.  */

static tree
convert_for_assignment (tree type, tree rhs, enum impl_conv errtype,
			tree fundecl, tree function, int parmnum)
{
  enum tree_code codel = TREE_CODE (type);
  tree rhstype;
  enum tree_code coder;
  tree rname = NULL_TREE;
  bool objc_ok = false;

  if (errtype == ic_argpass || errtype == ic_argpass_nonproto)
    {
      tree selector;
      /* Change pointer to function to the function itself for
	 diagnostics.  */
      if (TREE_CODE (function) == ADDR_EXPR
	  && TREE_CODE (TREE_OPERAND (function, 0)) == FUNCTION_DECL)
	function = TREE_OPERAND (function, 0);

      /* Handle an ObjC selector specially for diagnostics.  */
      selector = objc_message_selector ();
      rname = function;
      if (selector && parmnum > 2)
	{
	  rname = selector;
	  parmnum -= 2;
	}
    }

  /* This macro is used to emit diagnostics to ensure that all format
     strings are complete sentences, visible to gettext and checked at
     compile time.  */
#define WARN_FOR_ASSIGNMENT(AR, AS, IN, RE)	\
  do {						\
    switch (errtype)				\
      {						\
      case ic_argpass:				\
	pedwarn (AR, parmnum, rname);		\
	break;					\
      case ic_argpass_nonproto:			\
	warning (0, AR, parmnum, rname);		\
	break;					\
      case ic_assign:				\
	pedwarn (AS);				\
	break;					\
      case ic_init:				\
	pedwarn (IN);				\
	break;					\
      case ic_return:				\
	pedwarn (RE);				\
	break;					\
      default:					\
	gcc_unreachable ();			\
      }						\
  } while (0)

  STRIP_TYPE_NOPS (rhs);

  if (optimize && TREE_CODE (rhs) == VAR_DECL
	   && TREE_CODE (TREE_TYPE (rhs)) != ARRAY_TYPE)
    rhs = decl_constant_value_for_broken_optimization (rhs);

  rhstype = TREE_TYPE (rhs);
  coder = TREE_CODE (rhstype);

  if (coder == ERROR_MARK)
    return error_mark_node;

  if (c_dialect_objc ())
    {
      int parmno;

      switch (errtype)
	{
	case ic_return:
	  parmno = 0;
	  break;

	case ic_assign:
	  parmno = -1;
	  break;

	case ic_init:
	  parmno = -2;
	  break;

	default:
	  parmno = parmnum;
	  break;
	}

      objc_ok = objc_compare_types (type, rhstype, parmno, rname);
    }

  if (TYPE_MAIN_VARIANT (type) == TYPE_MAIN_VARIANT (rhstype))
    {
      overflow_warning (rhs);
      return rhs;
    }

  if (coder == VOID_TYPE)
    {
      /* Except for passing an argument to an unprototyped function,
	 this is a constraint violation.  When passing an argument to
	 an unprototyped function, it is compile-time undefined;
	 making it a constraint in that case was rejected in
	 DR#252.  */
      error ("void value not ignored as it ought to be");
      return error_mark_node;
    }
  /* A type converts to a reference to it.
     This code doesn't fully support references, it's just for the
     special case of va_start and va_copy.  */
  if (codel == REFERENCE_TYPE
      && comptypes (TREE_TYPE (type), TREE_TYPE (rhs)) == 1)
    {
      if (!lvalue_p (rhs))
	{
	  error ("cannot pass rvalue to reference parameter");
	  return error_mark_node;
	}
      if (!c_mark_addressable (rhs))
	return error_mark_node;
      rhs = build1 (ADDR_EXPR, build_pointer_type (TREE_TYPE (rhs)), rhs);

      /* We already know that these two types are compatible, but they
	 may not be exactly identical.  In fact, `TREE_TYPE (type)' is
	 likely to be __builtin_va_list and `TREE_TYPE (rhs)' is
	 likely to be va_list, a typedef to __builtin_va_list, which
	 is different enough that it will cause problems later.  */
      if (TREE_TYPE (TREE_TYPE (rhs)) != TREE_TYPE (type))
	rhs = build1 (NOP_EXPR, build_pointer_type (TREE_TYPE (type)), rhs);

      rhs = build1 (NOP_EXPR, type, rhs);
      return rhs;
    }
  /* Some types can interconvert without explicit casts.  */
  else if (codel == VECTOR_TYPE && coder == VECTOR_TYPE
	   && vector_types_convertible_p (type, TREE_TYPE (rhs)))
    return convert (type, rhs);
  /* Arithmetic types all interconvert, and enum is treated like int.  */
  else if ((codel == INTEGER_TYPE || codel == REAL_TYPE
	    || codel == ENUMERAL_TYPE || codel == COMPLEX_TYPE
	    || codel == BOOLEAN_TYPE)
	   && (coder == INTEGER_TYPE || coder == REAL_TYPE
	       || coder == ENUMERAL_TYPE || coder == COMPLEX_TYPE
	       || coder == BOOLEAN_TYPE))
    return convert_and_check (type, rhs);

  /* Aggregates in different TUs might need conversion.  */
  if ((codel == RECORD_TYPE || codel == UNION_TYPE)
      && codel == coder
      && comptypes (type, rhstype))
    return convert_and_check (type, rhs);

  /* Conversion to a transparent union from its member types.
     This applies only to function arguments.  */
  if (codel == UNION_TYPE && TYPE_TRANSPARENT_UNION (type)
      && (errtype == ic_argpass || errtype == ic_argpass_nonproto))
    {
      tree memb, marginal_memb = NULL_TREE;

      for (memb = TYPE_FIELDS (type); memb ; memb = TREE_CHAIN (memb))
	{
	  tree memb_type = TREE_TYPE (memb);

	  if (comptypes (TYPE_MAIN_VARIANT (memb_type),
			 TYPE_MAIN_VARIANT (rhstype)))
	    break;

	  if (TREE_CODE (memb_type) != POINTER_TYPE)
	    continue;

	  if (coder == POINTER_TYPE)
	    {
	      tree ttl = TREE_TYPE (memb_type);
	      tree ttr = TREE_TYPE (rhstype);

	      /* Any non-function converts to a [const][volatile] void *
		 and vice versa; otherwise, targets must be the same.
		 Meanwhile, the lhs target must have all the qualifiers of
		 the rhs.  */
	      if (VOID_TYPE_P (ttl) || VOID_TYPE_P (ttr)
		  || comp_target_types (memb_type, rhstype))
		{
		  /* If this type won't generate any warnings, use it.  */
		  if (TYPE_QUALS (ttl) == TYPE_QUALS (ttr)
		      || ((TREE_CODE (ttr) == FUNCTION_TYPE
			   && TREE_CODE (ttl) == FUNCTION_TYPE)
			  ? ((TYPE_QUALS (ttl) | TYPE_QUALS (ttr))
			     == TYPE_QUALS (ttr))
			  : ((TYPE_QUALS (ttl) | TYPE_QUALS (ttr))
			     == TYPE_QUALS (ttl))))
		    break;

		  /* Keep looking for a better type, but remember this one.  */
		  if (!marginal_memb)
		    marginal_memb = memb;
		}
	    }

	  /* Can convert integer zero to any pointer type.  */
	  if (null_pointer_constant_p (rhs))
	    {
	      rhs = null_pointer_node;
	      break;
	    }
	}

      if (memb || marginal_memb)
	{
	  if (!memb)
	    {
	      /* We have only a marginally acceptable member type;
		 it needs a warning.  */
	      tree ttl = TREE_TYPE (TREE_TYPE (marginal_memb));
	      tree ttr = TREE_TYPE (rhstype);

	      /* Const and volatile mean something different for function
		 types, so the usual warnings are not appropriate.  */
	      if (TREE_CODE (ttr) == FUNCTION_TYPE
		  && TREE_CODE (ttl) == FUNCTION_TYPE)
		{
		  /* Because const and volatile on functions are
		     restrictions that say the function will not do
		     certain things, it is okay to use a const or volatile
		     function where an ordinary one is wanted, but not
		     vice-versa.  */
		  if (TYPE_QUALS (ttl) & ~TYPE_QUALS (ttr))
		    WARN_FOR_ASSIGNMENT (G_("passing argument %d of %qE "
					    "makes qualified function "
					    "pointer from unqualified"),
					 G_("assignment makes qualified "
					    "function pointer from "
					    "unqualified"),
					 G_("initialization makes qualified "
					    "function pointer from "
					    "unqualified"),
					 G_("return makes qualified function "
					    "pointer from unqualified"));
		}
	      else if (TYPE_QUALS (ttr) & ~TYPE_QUALS (ttl))
		WARN_FOR_ASSIGNMENT (G_("passing argument %d of %qE discards "
					"qualifiers from pointer target type"),
				     G_("assignment discards qualifiers "
					"from pointer target type"),
				     G_("initialization discards qualifiers "
					"from pointer target type"),
				     G_("return discards qualifiers from "
					"pointer target type"));

	      memb = marginal_memb;
	    }

	  if (pedantic && (!fundecl || !DECL_IN_SYSTEM_HEADER (fundecl)))
	    pedwarn ("ISO C prohibits argument conversion to union type");

	  return build_constructor_single (type, memb, rhs);
	}
    }

  /* Conversions among pointers */
  else if ((codel == POINTER_TYPE || codel == REFERENCE_TYPE)
	   && (coder == codel))
    {
      tree ttl = TREE_TYPE (type);
      tree ttr = TREE_TYPE (rhstype);
      tree mvl = ttl;
      tree mvr = ttr;
      bool is_opaque_pointer;
      int target_cmp = 0;   /* Cache comp_target_types () result.  */

      if (TREE_CODE (mvl) != ARRAY_TYPE)
	mvl = TYPE_MAIN_VARIANT (mvl);
      if (TREE_CODE (mvr) != ARRAY_TYPE)
	mvr = TYPE_MAIN_VARIANT (mvr);
      /* Opaque pointers are treated like void pointers.  */
      is_opaque_pointer = (targetm.vector_opaque_p (type)
			   || targetm.vector_opaque_p (rhstype))
	&& TREE_CODE (ttl) == VECTOR_TYPE
	&& TREE_CODE (ttr) == VECTOR_TYPE;

      /* C++ does not allow the implicit conversion void* -> T*.  However,
	 for the purpose of reducing the number of false positives, we
	 tolerate the special case of

		int *p = NULL;

	 where NULL is typically defined in C to be '(void *) 0'.  */
      if (VOID_TYPE_P (ttr) && rhs != null_pointer_node && !VOID_TYPE_P (ttl))
	warning (OPT_Wc___compat, "request for implicit conversion from "
		 "%qT to %qT not permitted in C++", rhstype, type);

      /* Check if the right-hand side has a format attribute but the
	 left-hand side doesn't.  */
      if (warn_missing_format_attribute
	  && check_missing_format_attribute (type, rhstype))
	{
	  switch (errtype)
	  {
	  case ic_argpass:
	  case ic_argpass_nonproto:
	    warning (OPT_Wmissing_format_attribute,
		     "argument %d of %qE might be "
		     "a candidate for a format attribute",
		     parmnum, rname);
	    break;
	  case ic_assign:
	    warning (OPT_Wmissing_format_attribute,
		     "assignment left-hand side might be "
		     "a candidate for a format attribute");
	    break;
	  case ic_init:
	    warning (OPT_Wmissing_format_attribute,
		     "initialization left-hand side might be "
		     "a candidate for a format attribute");
	    break;
	  case ic_return:
	    warning (OPT_Wmissing_format_attribute,
		     "return type might be "
		     "a candidate for a format attribute");
	    break;
	  default:
	    gcc_unreachable ();
	  }
	}

      /* Any non-function converts to a [const][volatile] void *
	 and vice versa; otherwise, targets must be the same.
	 Meanwhile, the lhs target must have all the qualifiers of the rhs.  */
      if (VOID_TYPE_P (ttl) || VOID_TYPE_P (ttr)
	  || (target_cmp = comp_target_types (type, rhstype))
	  || is_opaque_pointer
	  || (c_common_unsigned_type (mvl)
	      == c_common_unsigned_type (mvr)))
	{
	  if (pedantic
	      && ((VOID_TYPE_P (ttl) && TREE_CODE (ttr) == FUNCTION_TYPE)
		  ||
		  (VOID_TYPE_P (ttr)
		   && !null_pointer_constant_p (rhs)
		   && TREE_CODE (ttl) == FUNCTION_TYPE)))
	    WARN_FOR_ASSIGNMENT (G_("ISO C forbids passing argument %d of "
				    "%qE between function pointer "
				    "and %<void *%>"),
				 G_("ISO C forbids assignment between "
				    "function pointer and %<void *%>"),
				 G_("ISO C forbids initialization between "
				    "function pointer and %<void *%>"),
				 G_("ISO C forbids return between function "
				    "pointer and %<void *%>"));
	  /* Const and volatile mean something different for function types,
	     so the usual warnings are not appropriate.  */
	  else if (TREE_CODE (ttr) != FUNCTION_TYPE
		   && TREE_CODE (ttl) != FUNCTION_TYPE)
	    {
	      if (TYPE_QUALS (ttr) & ~TYPE_QUALS (ttl))
		{
		  /* Types differing only by the presence of the 'volatile'
		     qualifier are acceptable if the 'volatile' has been added
		     in by the Objective-C EH machinery.  */
		  if (!objc_type_quals_match (ttl, ttr))
		    WARN_FOR_ASSIGNMENT (G_("passing argument %d of %qE discards "
					    "qualifiers from pointer target type"),
					 G_("assignment discards qualifiers "
					    "from pointer target type"),
					 G_("initialization discards qualifiers "
					    "from pointer target type"),
					 G_("return discards qualifiers from "
					    "pointer target type"));
		}
	      /* If this is not a case of ignoring a mismatch in signedness,
		 no warning.  */
	      else if (VOID_TYPE_P (ttl) || VOID_TYPE_P (ttr)
		       || target_cmp)
		;
	      /* If there is a mismatch, do warn.  */
	      else if (warn_pointer_sign)
		WARN_FOR_ASSIGNMENT (G_("pointer targets in passing argument "
					"%d of %qE differ in signedness"),
				     G_("pointer targets in assignment "
					"differ in signedness"),
				     G_("pointer targets in initialization "
					"differ in signedness"),
				     G_("pointer targets in return differ "
					"in signedness"));
	    }
	  else if (TREE_CODE (ttl) == FUNCTION_TYPE
		   && TREE_CODE (ttr) == FUNCTION_TYPE)
	    {
	      /* Because const and volatile on functions are restrictions
		 that say the function will not do certain things,
		 it is okay to use a const or volatile function
		 where an ordinary one is wanted, but not vice-versa.  */
	      if (TYPE_QUALS (ttl) & ~TYPE_QUALS (ttr))
		WARN_FOR_ASSIGNMENT (G_("passing argument %d of %qE makes "
					"qualified function pointer "
					"from unqualified"),
				     G_("assignment makes qualified function "
					"pointer from unqualified"),
				     G_("initialization makes qualified "
					"function pointer from unqualified"),
				     G_("return makes qualified function "
					"pointer from unqualified"));
	    }
	}
      else
	/* Avoid warning about the volatile ObjC EH puts on decls.  */
	if (!objc_ok)
	  WARN_FOR_ASSIGNMENT (G_("passing argument %d of %qE from "
				  "incompatible pointer type"),
			       G_("assignment from incompatible pointer type"),
			       G_("initialization from incompatible "
				  "pointer type"),
			       G_("return from incompatible pointer type"));

      return convert (type, rhs);
    }
  else if (codel == POINTER_TYPE && coder == ARRAY_TYPE)
    {
      /* ??? This should not be an error when inlining calls to
	 unprototyped functions.  */
      error ("invalid use of non-lvalue array");
      return error_mark_node;
    }
  else if (codel == POINTER_TYPE && coder == INTEGER_TYPE)
    {
      /* An explicit constant 0 can convert to a pointer,
	 or one that results from arithmetic, even including
	 a cast to integer type.  */
      if (!null_pointer_constant_p (rhs))
	WARN_FOR_ASSIGNMENT (G_("passing argument %d of %qE makes "
				"pointer from integer without a cast"),
			     G_("assignment makes pointer from integer "
				"without a cast"),
			     G_("initialization makes pointer from "
				"integer without a cast"),
			     G_("return makes pointer from integer "
				"without a cast"));

      return convert (type, rhs);
    }
  else if (codel == INTEGER_TYPE && coder == POINTER_TYPE)
    {
      WARN_FOR_ASSIGNMENT (G_("passing argument %d of %qE makes integer "
			      "from pointer without a cast"),
			   G_("assignment makes integer from pointer "
			      "without a cast"),
			   G_("initialization makes integer from pointer "
			      "without a cast"),
			   G_("return makes integer from pointer "
			      "without a cast"));
      return convert (type, rhs);
    }
  else if (codel == BOOLEAN_TYPE && coder == POINTER_TYPE)
    return convert (type, rhs);

  switch (errtype)
    {
    case ic_argpass:
    case ic_argpass_nonproto:
      /* ??? This should not be an error when inlining calls to
	 unprototyped functions.  */
      error ("incompatible type for argument %d of %qE", parmnum, rname);
      break;
    case ic_assign:
      error ("incompatible types in assignment");
      break;
    case ic_init:
      error ("incompatible types in initialization");
      break;
    case ic_return:
      error ("incompatible types in return");
      break;
    default:
      gcc_unreachable ();
    }

  return error_mark_node;
}

/* Convert VALUE for assignment into inlined parameter PARM.  ARGNUM
   is used for error and warning reporting and indicates which argument
   is being processed.  */

tree
c_convert_parm_for_inlining (tree parm, tree value, tree fn, int argnum)
{
  tree ret, type;

  /* If FN was prototyped at the call site, the value has been converted
     already in convert_arguments.
     However, we might see a prototype now that was not in place when
     the function call was seen, so check that the VALUE actually matches
     PARM before taking an early exit.  */
  if (!value
      || (TYPE_ARG_TYPES (TREE_TYPE (fn))
	  && (TYPE_MAIN_VARIANT (TREE_TYPE (parm))
	      == TYPE_MAIN_VARIANT (TREE_TYPE (value)))))
    return value;

  type = TREE_TYPE (parm);
  ret = convert_for_assignment (type, value,
				ic_argpass_nonproto, fn,
				fn, argnum);
  if (targetm.calls.promote_prototypes (TREE_TYPE (fn))
      && INTEGRAL_TYPE_P (type)
      && (TYPE_PRECISION (type) < TYPE_PRECISION (integer_type_node)))
    ret = default_conversion (ret);
  return ret;
}

/* If VALUE is a compound expr all of whose expressions are constant, then
   return its value.  Otherwise, return error_mark_node.

   This is for handling COMPOUND_EXPRs as initializer elements
   which is allowed with a warning when -pedantic is specified.  */

static tree
valid_compound_expr_initializer (tree value, tree endtype)
{
  if (TREE_CODE (value) == COMPOUND_EXPR)
    {
      if (valid_compound_expr_initializer (TREE_OPERAND (value, 0), endtype)
	  == error_mark_node)
	return error_mark_node;
      return valid_compound_expr_initializer (TREE_OPERAND (value, 1),
					      endtype);
    }
  else if (!initializer_constant_valid_p (value, endtype))
    return error_mark_node;
  else
    return value;
}

/* Perform appropriate conversions on the initial value of a variable,
   store it in the declaration DECL,
   and print any error messages that are appropriate.
   If the init is invalid, store an ERROR_MARK.  */

void
store_init_value (tree decl, tree init)
{
  tree value, type;

  /* If variable's type was invalidly declared, just ignore it.  */

  type = TREE_TYPE (decl);
  if (TREE_CODE (type) == ERROR_MARK)
    return;

  /* Digest the specified initializer into an expression.  */

  value = digest_init (type, init, true, TREE_STATIC (decl));

  /* Store the expression if valid; else report error.  */

  if (!in_system_header
      && AGGREGATE_TYPE_P (TREE_TYPE (decl)) && !TREE_STATIC (decl))
    warning (OPT_Wtraditional, "traditional C rejects automatic "
	     "aggregate initialization");

  DECL_INITIAL (decl) = value;

  /* ANSI wants warnings about out-of-range constant initializers.  */
  STRIP_TYPE_NOPS (value);
  constant_expression_warning (value);

  /* Check if we need to set array size from compound literal size.  */
  if (TREE_CODE (type) == ARRAY_TYPE
      && TYPE_DOMAIN (type) == 0
      && value != error_mark_node)
    {
      tree inside_init = init;

      STRIP_TYPE_NOPS (inside_init);
      inside_init = fold (inside_init);

      if (TREE_CODE (inside_init) == COMPOUND_LITERAL_EXPR)
	{
	  tree cldecl = COMPOUND_LITERAL_EXPR_DECL (inside_init);

	  if (TYPE_DOMAIN (TREE_TYPE (cldecl)))
	    {
	      /* For int foo[] = (int [3]){1}; we need to set array size
		 now since later on array initializer will be just the
		 brace enclosed list of the compound literal.  */
	      type = build_distinct_type_copy (TYPE_MAIN_VARIANT (type));
	      TREE_TYPE (decl) = type;
	      TYPE_DOMAIN (type) = TYPE_DOMAIN (TREE_TYPE (cldecl));
	      layout_type (type);
	      layout_decl (cldecl, 0);
	    }
	}
    }
}

/* Methods for storing and printing names for error messages.  */

/* Implement a spelling stack that allows components of a name to be pushed
   and popped.  Each element on the stack is this structure.  */

struct spelling
{
  int kind;
  union
    {
      unsigned HOST_WIDE_INT i;
      const char *s;
    } u;
};

#define SPELLING_STRING 1
#define SPELLING_MEMBER 2
#define SPELLING_BOUNDS 3

static struct spelling *spelling;	/* Next stack element (unused).  */
static struct spelling *spelling_base;	/* Spelling stack base.  */
static int spelling_size;		/* Size of the spelling stack.  */

/* Macros to save and restore the spelling stack around push_... functions.
   Alternative to SAVE_SPELLING_STACK.  */

#define SPELLING_DEPTH() (spelling - spelling_base)
#define RESTORE_SPELLING_DEPTH(DEPTH) (spelling = spelling_base + (DEPTH))

/* Push an element on the spelling stack with type KIND and assign VALUE
   to MEMBER.  */

#define PUSH_SPELLING(KIND, VALUE, MEMBER)				\
{									\
  int depth = SPELLING_DEPTH ();					\
									\
  if (depth >= spelling_size)						\
    {									\
      spelling_size += 10;						\
      spelling_base = XRESIZEVEC (struct spelling, spelling_base,	\
				  spelling_size);			\
      RESTORE_SPELLING_DEPTH (depth);					\
    }									\
									\
  spelling->kind = (KIND);						\
  spelling->MEMBER = (VALUE);						\
  spelling++;								\
}

/* Push STRING on the stack.  Printed literally.  */

static void
push_string (const char *string)
{
  PUSH_SPELLING (SPELLING_STRING, string, u.s);
}

/* Push a member name on the stack.  Printed as '.' STRING.  */

static void
push_member_name (tree decl)
{
  const char *const string
    = DECL_NAME (decl) ? IDENTIFIER_POINTER (DECL_NAME (decl)) : "<anonymous>";
  PUSH_SPELLING (SPELLING_MEMBER, string, u.s);
}

/* Push an array bounds on the stack.  Printed as [BOUNDS].  */

static void
push_array_bounds (unsigned HOST_WIDE_INT bounds)
{
  PUSH_SPELLING (SPELLING_BOUNDS, bounds, u.i);
}

/* Compute the maximum size in bytes of the printed spelling.  */

static int
spelling_length (void)
{
  int size = 0;
  struct spelling *p;

  for (p = spelling_base; p < spelling; p++)
    {
      if (p->kind == SPELLING_BOUNDS)
	size += 25;
      else
	size += strlen (p->u.s) + 1;
    }

  return size;
}

/* Print the spelling to BUFFER and return it.  */

static char *
print_spelling (char *buffer)
{
  char *d = buffer;
  struct spelling *p;

  for (p = spelling_base; p < spelling; p++)
    if (p->kind == SPELLING_BOUNDS)
      {
	sprintf (d, "[" HOST_WIDE_INT_PRINT_UNSIGNED "]", p->u.i);
	d += strlen (d);
      }
    else
      {
	const char *s;
	if (p->kind == SPELLING_MEMBER)
	  *d++ = '.';
	for (s = p->u.s; (*d = *s++); d++)
	  ;
      }
  *d++ = '\0';
  return buffer;
}

/* Issue an error message for a bad initializer component.
   MSGID identifies the message.
   The component name is taken from the spelling stack.  */

void
error_init (const char *msgid)
{
  char *ofwhat;

  error ("%s", _(msgid));
  ofwhat = print_spelling ((char *) alloca (spelling_length () + 1));
  if (*ofwhat)
    error ("(near initialization for %qs)", ofwhat);
}

/* Issue a pedantic warning for a bad initializer component.
   MSGID identifies the message.
   The component name is taken from the spelling stack.  */

void
pedwarn_init (const char *msgid)
{
  char *ofwhat;

  pedwarn ("%s", _(msgid));
  ofwhat = print_spelling ((char *) alloca (spelling_length () + 1));
  if (*ofwhat)
    pedwarn ("(near initialization for %qs)", ofwhat);
}

/* Issue a warning for a bad initializer component.
   MSGID identifies the message.
   The component name is taken from the spelling stack.  */

static void
warning_init (const char *msgid)
{
  char *ofwhat;

  warning (0, "%s", _(msgid));
  ofwhat = print_spelling ((char *) alloca (spelling_length () + 1));
  if (*ofwhat)
    warning (0, "(near initialization for %qs)", ofwhat);
}

/* If TYPE is an array type and EXPR is a parenthesized string
   constant, warn if pedantic that EXPR is being used to initialize an
   object of type TYPE.  */

void
maybe_warn_string_init (tree type, struct c_expr expr)
{
  if (pedantic
      && TREE_CODE (type) == ARRAY_TYPE
      && TREE_CODE (expr.value) == STRING_CST
      && expr.original_code != STRING_CST)
    pedwarn_init ("array initialized from parenthesized string constant");
}

/* Digest the parser output INIT as an initializer for type TYPE.
   Return a C expression of type TYPE to represent the initial value.

   If INIT is a string constant, STRICT_STRING is true if it is
   unparenthesized or we should not warn here for it being parenthesized.
   For other types of INIT, STRICT_STRING is not used.

   REQUIRE_CONSTANT requests an error if non-constant initializers or
   elements are seen.  */

static tree
digest_init (tree type, tree init, bool strict_string, int require_constant)
{
  enum tree_code code = TREE_CODE (type);
  tree inside_init = init;

  if (type == error_mark_node
      || !init
      || init == error_mark_node
      || TREE_TYPE (init) == error_mark_node)
    return error_mark_node;

  STRIP_TYPE_NOPS (inside_init);

  inside_init = fold (inside_init);

  /* Initialization of an array of chars from a string constant
     optionally enclosed in braces.  */

  if (code == ARRAY_TYPE && inside_init
      && TREE_CODE (inside_init) == STRING_CST)
    {
      tree typ1 = TYPE_MAIN_VARIANT (TREE_TYPE (type));
      /* Note that an array could be both an array of character type
	 and an array of wchar_t if wchar_t is signed char or unsigned
	 char.  */
      bool char_array = (typ1 == char_type_node
			 || typ1 == signed_char_type_node
			 || typ1 == unsigned_char_type_node);
      bool wchar_array = !!comptypes (typ1, wchar_type_node);
      if (char_array || wchar_array)
	{
	  struct c_expr expr;
	  bool char_string;
	  expr.value = inside_init;
	  expr.original_code = (strict_string ? STRING_CST : ERROR_MARK);
	  maybe_warn_string_init (type, expr);

	  char_string
	    = (TYPE_MAIN_VARIANT (TREE_TYPE (TREE_TYPE (inside_init)))
	       == char_type_node);

	  if (comptypes (TYPE_MAIN_VARIANT (TREE_TYPE (inside_init)),
			 TYPE_MAIN_VARIANT (type)))
	    return inside_init;

	  if (!wchar_array && !char_string)
	    {
	      error_init ("char-array initialized from wide string");
	      return error_mark_node;
	    }
	  if (char_string && !char_array)
	    {
	      error_init ("wchar_t-array initialized from non-wide string");
	      return error_mark_node;
	    }

	  TREE_TYPE (inside_init) = type;
	  if (TYPE_DOMAIN (type) != 0
	      && TYPE_SIZE (type) != 0
	      && TREE_CODE (TYPE_SIZE (type)) == INTEGER_CST
	      /* Subtract 1 (or sizeof (wchar_t))
		 because it's ok to ignore the terminating null char
		 that is counted in the length of the constant.  */
	      && 0 > compare_tree_int (TYPE_SIZE_UNIT (type),
				       TREE_STRING_LENGTH (inside_init)
				       - ((TYPE_PRECISION (typ1)
					   != TYPE_PRECISION (char_type_node))
					  ? (TYPE_PRECISION (wchar_type_node)
					     / BITS_PER_UNIT)
					  : 1)))
	    pedwarn_init ("initializer-string for array of chars is too long");

	  return inside_init;
	}
      else if (INTEGRAL_TYPE_P (typ1))
	{
	  error_init ("array of inappropriate type initialized "
		      "from string constant");
	  return error_mark_node;
	}
    }

  /* Build a VECTOR_CST from a *constant* vector constructor.  If the
     vector constructor is not constant (e.g. {1,2,3,foo()}) then punt
     below and handle as a constructor.  */
  if (code == VECTOR_TYPE
      && TREE_CODE (TREE_TYPE (inside_init)) == VECTOR_TYPE
      && vector_types_convertible_p (TREE_TYPE (inside_init), type)
      && TREE_CONSTANT (inside_init))
    {
      if (TREE_CODE (inside_init) == VECTOR_CST
	  && comptypes (TYPE_MAIN_VARIANT (TREE_TYPE (inside_init)),
			TYPE_MAIN_VARIANT (type)))
	return inside_init;

      if (TREE_CODE (inside_init) == CONSTRUCTOR)
	{
	  unsigned HOST_WIDE_INT ix;
	  tree value;
	  bool constant_p = true;

	  /* Iterate through elements and check if all constructor
	     elements are *_CSTs.  */
	  FOR_EACH_CONSTRUCTOR_VALUE (CONSTRUCTOR_ELTS (inside_init), ix, value)
	    if (!CONSTANT_CLASS_P (value))
	      {
		constant_p = false;
		break;
	      }

	  if (constant_p)
	    return build_vector_from_ctor (type,
					   CONSTRUCTOR_ELTS (inside_init));
	}
    }

  /* Any type can be initialized
     from an expression of the same type, optionally with braces.  */

  if (inside_init && TREE_TYPE (inside_init) != 0
      && (comptypes (TYPE_MAIN_VARIANT (TREE_TYPE (inside_init)),
		     TYPE_MAIN_VARIANT (type))
	  || (code == ARRAY_TYPE
	      && comptypes (TREE_TYPE (inside_init), type))
	  || (code == VECTOR_TYPE
	      && comptypes (TREE_TYPE (inside_init), type))
	  || (code == POINTER_TYPE
	      && TREE_CODE (TREE_TYPE (inside_init)) == ARRAY_TYPE
	      && comptypes (TREE_TYPE (TREE_TYPE (inside_init)),
			    TREE_TYPE (type)))))
    {
      if (code == POINTER_TYPE)
	{
	  if (TREE_CODE (TREE_TYPE (inside_init)) == ARRAY_TYPE)
	    {
	      if (TREE_CODE (inside_init) == STRING_CST
		  || TREE_CODE (inside_init) == COMPOUND_LITERAL_EXPR)
		inside_init = array_to_pointer_conversion (inside_init);
	      else
		{
		  error_init ("invalid use of non-lvalue array");
		  return error_mark_node;
		}
	    }
	}

      if (code == VECTOR_TYPE)
	/* Although the types are compatible, we may require a
	   conversion.  */
	inside_init = convert (type, inside_init);

      if (require_constant
	  && (code == VECTOR_TYPE || !flag_isoc99)
	  && TREE_CODE (inside_init) == COMPOUND_LITERAL_EXPR)
	{
	  /* As an extension, allow initializing objects with static storage
	     duration with compound literals (which are then treated just as
	     the brace enclosed list they contain).  Also allow this for
	     vectors, as we can only assign them with compound literals.  */
	  tree decl = COMPOUND_LITERAL_EXPR_DECL (inside_init);
	  inside_init = DECL_INITIAL (decl);
	}

      if (code == ARRAY_TYPE && TREE_CODE (inside_init) != STRING_CST
	  && TREE_CODE (inside_init) != CONSTRUCTOR)
	{
	  error_init ("array initialized from non-constant array expression");
	  return error_mark_node;
	}

      if (optimize && TREE_CODE (inside_init) == VAR_DECL)
	inside_init = decl_constant_value_for_broken_optimization (inside_init);

      /* Compound expressions can only occur here if -pedantic or
	 -pedantic-errors is specified.  In the later case, we always want
	 an error.  In the former case, we simply want a warning.  */
      if (require_constant && pedantic
	  && TREE_CODE (inside_init) == COMPOUND_EXPR)
	{
	  inside_init
	    = valid_compound_expr_initializer (inside_init,
					       TREE_TYPE (inside_init));
	  if (inside_init == error_mark_node)
	    error_init ("initializer element is not constant");
	  else
	    pedwarn_init ("initializer element is not constant");
	  if (flag_pedantic_errors)
	    inside_init = error_mark_node;
	}
      else if (require_constant
	       && !initializer_constant_valid_p (inside_init,
						 TREE_TYPE (inside_init)))
	{
	  error_init ("initializer element is not constant");
	  inside_init = error_mark_node;
	}

      /* Added to enable additional -Wmissing-format-attribute warnings.  */
      if (TREE_CODE (TREE_TYPE (inside_init)) == POINTER_TYPE)
	inside_init = convert_for_assignment (type, inside_init, ic_init, NULL_TREE,
					      NULL_TREE, 0);
      return inside_init;
    }

  /* Handle scalar types, including conversions.  */

  if (code == INTEGER_TYPE || code == REAL_TYPE || code == POINTER_TYPE
      || code == ENUMERAL_TYPE || code == BOOLEAN_TYPE || code == COMPLEX_TYPE
      || code == VECTOR_TYPE)
    {
      if (TREE_CODE (TREE_TYPE (init)) == ARRAY_TYPE
	  && (TREE_CODE (init) == STRING_CST
	      || TREE_CODE (init) == COMPOUND_LITERAL_EXPR))
	init = array_to_pointer_conversion (init);
      inside_init
	= convert_for_assignment (type, init, ic_init,
				  NULL_TREE, NULL_TREE, 0);

      /* Check to see if we have already given an error message.  */
      if (inside_init == error_mark_node)
	;
      else if (require_constant && !TREE_CONSTANT (inside_init))
	{
	  error_init ("initializer element is not constant");
	  inside_init = error_mark_node;
	}
      else if (require_constant
	       && !initializer_constant_valid_p (inside_init,
						 TREE_TYPE (inside_init)))
	{
	  error_init ("initializer element is not computable at load time");
	  inside_init = error_mark_node;
	}

      return inside_init;
    }

  /* Come here only for records and arrays.  */

  if (COMPLETE_TYPE_P (type) && TREE_CODE (TYPE_SIZE (type)) != INTEGER_CST)
    {
      error_init ("variable-sized object may not be initialized");
      return error_mark_node;
    }

  error_init ("invalid initializer");
  return error_mark_node;
}

/* Handle initializers that use braces.  */

/* Type of object we are accumulating a constructor for.
   This type is always a RECORD_TYPE, UNION_TYPE or ARRAY_TYPE.  */
static tree constructor_type;

/* For a RECORD_TYPE or UNION_TYPE, this is the chain of fields
   left to fill.  */
static tree constructor_fields;

/* For an ARRAY_TYPE, this is the specified index
   at which to store the next element we get.  */
static tree constructor_index;

/* For an ARRAY_TYPE, this is the maximum index.  */
static tree constructor_max_index;

/* For a RECORD_TYPE, this is the first field not yet written out.  */
static tree constructor_unfilled_fields;

/* For an ARRAY_TYPE, this is the index of the first element
   not yet written out.  */
static tree constructor_unfilled_index;

/* In a RECORD_TYPE, the byte index of the next consecutive field.
   This is so we can generate gaps between fields, when appropriate.  */
static tree constructor_bit_index;

/* If we are saving up the elements rather than allocating them,
   this is the list of elements so far (in reverse order,
   most recent first).  */
static VEC(constructor_elt,gc) *constructor_elements;

/* 1 if constructor should be incrementally stored into a constructor chain,
   0 if all the elements should be kept in AVL tree.  */
static int constructor_incremental;

/* 1 if so far this constructor's elements are all compile-time constants.  */
static int constructor_constant;

/* 1 if so far this constructor's elements are all valid address constants.  */
static int constructor_simple;

/* 1 if this constructor is erroneous so far.  */
static int constructor_erroneous;

/* 1 if this constructor is a zero init. */
static int constructor_zeroinit;

/* Structure for managing pending initializer elements, organized as an
   AVL tree.  */

struct init_node
{
  struct init_node *left, *right;
  struct init_node *parent;
  int balance;
  tree purpose;
  tree value;
};

/* Tree of pending elements at this constructor level.
   These are elements encountered out of order
   which belong at places we haven't reached yet in actually
   writing the output.
   Will never hold tree nodes across GC runs.  */
static struct init_node *constructor_pending_elts;

/* The SPELLING_DEPTH of this constructor.  */
static int constructor_depth;

/* DECL node for which an initializer is being read.
   0 means we are reading a constructor expression
   such as (struct foo) {...}.  */
static tree constructor_decl;

/* Nonzero if this is an initializer for a top-level decl.  */
static int constructor_top_level;

/* Nonzero if there were any member designators in this initializer.  */
static int constructor_designated;

/* Nesting depth of designator list.  */
static int designator_depth;

/* Nonzero if there were diagnosed errors in this designator list.  */
static int designator_erroneous;


/* This stack has a level for each implicit or explicit level of
   structuring in the initializer, including the outermost one.  It
   saves the values of most of the variables above.  */

struct constructor_range_stack;

struct constructor_stack
{
  struct constructor_stack *next;
  tree type;
  tree fields;
  tree index;
  tree max_index;
  tree unfilled_index;
  tree unfilled_fields;
  tree bit_index;
  VEC(constructor_elt,gc) *elements;
  struct init_node *pending_elts;
  int offset;
  int depth;
  /* If value nonzero, this value should replace the entire
     constructor at this level.  */
  struct c_expr replacement_value;
  struct constructor_range_stack *range_stack;
  char constant;
  char simple;
  char implicit;
  char erroneous;
  char outer;
  char incremental;
  char designated;
};

static struct constructor_stack *constructor_stack;

/* This stack represents designators from some range designator up to
   the last designator in the list.  */

struct constructor_range_stack
{
  struct constructor_range_stack *next, *prev;
  struct constructor_stack *stack;
  tree range_start;
  tree index;
  tree range_end;
  tree fields;
};

static struct constructor_range_stack *constructor_range_stack;

/* This stack records separate initializers that are nested.
   Nested initializers can't happen in ANSI C, but GNU C allows them
   in cases like { ... (struct foo) { ... } ... }.  */

struct initializer_stack
{
  struct initializer_stack *next;
  tree decl;
  struct constructor_stack *constructor_stack;
  struct constructor_range_stack *constructor_range_stack;
  VEC(constructor_elt,gc) *elements;
  struct spelling *spelling;
  struct spelling *spelling_base;
  int spelling_size;
  char top_level;
  char require_constant_value;
  char require_constant_elements;
};

static struct initializer_stack *initializer_stack;

/* Prepare to parse and output the initializer for variable DECL.  */

void
start_init (tree decl, tree asmspec_tree ATTRIBUTE_UNUSED, int top_level)
{
  const char *locus;
  struct initializer_stack *p = XNEW (struct initializer_stack);

  p->decl = constructor_decl;
  p->require_constant_value = require_constant_value;
  p->require_constant_elements = require_constant_elements;
  p->constructor_stack = constructor_stack;
  p->constructor_range_stack = constructor_range_stack;
  p->elements = constructor_elements;
  p->spelling = spelling;
  p->spelling_base = spelling_base;
  p->spelling_size = spelling_size;
  p->top_level = constructor_top_level;
  p->next = initializer_stack;
  initializer_stack = p;

  constructor_decl = decl;
  constructor_designated = 0;
  constructor_top_level = top_level;

  if (decl != 0 && decl != error_mark_node)
    {
      require_constant_value = TREE_STATIC (decl);
      require_constant_elements
	= ((TREE_STATIC (decl) || (pedantic && !flag_isoc99))
	   /* For a scalar, you can always use any value to initialize,
	      even within braces.  */
	   && (TREE_CODE (TREE_TYPE (decl)) == ARRAY_TYPE
	       || TREE_CODE (TREE_TYPE (decl)) == RECORD_TYPE
	       || TREE_CODE (TREE_TYPE (decl)) == UNION_TYPE
	       || TREE_CODE (TREE_TYPE (decl)) == QUAL_UNION_TYPE));
      locus = IDENTIFIER_POINTER (DECL_NAME (decl));
    }
  else
    {
      require_constant_value = 0;
      require_constant_elements = 0;
      locus = "(anonymous)";
    }

  constructor_stack = 0;
  constructor_range_stack = 0;

  constructor_zeroinit = 0;
  missing_braces_mentioned = 0;

  spelling_base = 0;
  spelling_size = 0;
  RESTORE_SPELLING_DEPTH (0);

  if (locus)
    push_string (locus);
}

void
finish_init (void)
{
  struct initializer_stack *p = initializer_stack;

  /* Free the whole constructor stack of this initializer.  */
  while (constructor_stack)
    {
      struct constructor_stack *q = constructor_stack;
      constructor_stack = q->next;
      free (q);
    }

  gcc_assert (!constructor_range_stack);

  /* Pop back to the data of the outer initializer (if any).  */
  free (spelling_base);

  constructor_decl = p->decl;
  require_constant_value = p->require_constant_value;
  require_constant_elements = p->require_constant_elements;
  constructor_stack = p->constructor_stack;
  constructor_range_stack = p->constructor_range_stack;
  constructor_elements = p->elements;
  spelling = p->spelling;
  spelling_base = p->spelling_base;
  spelling_size = p->spelling_size;
  constructor_top_level = p->top_level;
  initializer_stack = p->next;
  free (p);
}

/* Call here when we see the initializer is surrounded by braces.
   This is instead of a call to push_init_level;
   it is matched by a call to pop_init_level.

   TYPE is the type to initialize, for a constructor expression.
   For an initializer for a decl, TYPE is zero.  */

void
really_start_incremental_init (tree type)
{
  struct constructor_stack *p = XNEW (struct constructor_stack);

  if (type == 0)
    type = TREE_TYPE (constructor_decl);

  if (targetm.vector_opaque_p (type))
    error ("opaque vector types cannot be initialized");

  p->type = constructor_type;
  p->fields = constructor_fields;
  p->index = constructor_index;
  p->max_index = constructor_max_index;
  p->unfilled_index = constructor_unfilled_index;
  p->unfilled_fields = constructor_unfilled_fields;
  p->bit_index = constructor_bit_index;
  p->elements = constructor_elements;
  p->constant = constructor_constant;
  p->simple = constructor_simple;
  p->erroneous = constructor_erroneous;
  p->pending_elts = constructor_pending_elts;
  p->depth = constructor_depth;
  p->replacement_value.value = 0;
  p->replacement_value.original_code = ERROR_MARK;
  p->implicit = 0;
  p->range_stack = 0;
  p->outer = 0;
  p->incremental = constructor_incremental;
  p->designated = constructor_designated;
  p->next = 0;
  constructor_stack = p;

  constructor_constant = 1;
  constructor_simple = 1;
  constructor_depth = SPELLING_DEPTH ();
  constructor_elements = 0;
  constructor_pending_elts = 0;
  constructor_type = type;
  constructor_incremental = 1;
  constructor_designated = 0;
  designator_depth = 0;
  designator_erroneous = 0;

  if (TREE_CODE (constructor_type) == RECORD_TYPE
      || TREE_CODE (constructor_type) == UNION_TYPE)
    {
      constructor_fields = TYPE_FIELDS (constructor_type);
      /* Skip any nameless bit fields at the beginning.  */
      while (constructor_fields != 0 && DECL_C_BIT_FIELD (constructor_fields)
	     && DECL_NAME (constructor_fields) == 0)
	constructor_fields = TREE_CHAIN (constructor_fields);

      constructor_unfilled_fields = constructor_fields;
      constructor_bit_index = bitsize_zero_node;
    }
  else if (TREE_CODE (constructor_type) == ARRAY_TYPE)
    {
      if (TYPE_DOMAIN (constructor_type))
	{
	  constructor_max_index
	    = TYPE_MAX_VALUE (TYPE_DOMAIN (constructor_type));

	  /* Detect non-empty initializations of zero-length arrays.  */
	  if (constructor_max_index == NULL_TREE
	      && TYPE_SIZE (constructor_type))
	    constructor_max_index = build_int_cst (NULL_TREE, -1);

	  /* constructor_max_index needs to be an INTEGER_CST.  Attempts
	     to initialize VLAs will cause a proper error; avoid tree
	     checking errors as well by setting a safe value.  */
	  if (constructor_max_index
	      && TREE_CODE (constructor_max_index) != INTEGER_CST)
	    constructor_max_index = build_int_cst (NULL_TREE, -1);

	  constructor_index
	    = convert (bitsizetype,
		       TYPE_MIN_VALUE (TYPE_DOMAIN (constructor_type)));
	}
      else
	{
	  constructor_index = bitsize_zero_node;
	  constructor_max_index = NULL_TREE;
	}

      constructor_unfilled_index = constructor_index;
    }
  else if (TREE_CODE (constructor_type) == VECTOR_TYPE)
    {
      /* Vectors are like simple fixed-size arrays.  */
      constructor_max_index =
	build_int_cst (NULL_TREE, TYPE_VECTOR_SUBPARTS (constructor_type) - 1);
      constructor_index = bitsize_zero_node;
      constructor_unfilled_index = constructor_index;
    }
  else
    {
      /* Handle the case of int x = {5}; */
      constructor_fields = constructor_type;
      constructor_unfilled_fields = constructor_type;
    }
}

/* Push down into a subobject, for initialization.
   If this is for an explicit set of braces, IMPLICIT is 0.
   If it is because the next element belongs at a lower level,
   IMPLICIT is 1 (or 2 if the push is because of designator list).  */

void
push_init_level (int implicit)
{
  struct constructor_stack *p;
  tree value = NULL_TREE;

  /* If we've exhausted any levels that didn't have braces,
     pop them now.  If implicit == 1, this will have been done in
     process_init_element; do not repeat it here because in the case
     of excess initializers for an empty aggregate this leads to an
     infinite cycle of popping a level and immediately recreating
     it.  */
  if (implicit != 1)
    {
      while (constructor_stack->implicit)
	{
	  if ((TREE_CODE (constructor_type) == RECORD_TYPE
	       || TREE_CODE (constructor_type) == UNION_TYPE)
	      && constructor_fields == 0)
	    process_init_element (pop_init_level (1));
	  else if (TREE_CODE (constructor_type) == ARRAY_TYPE
		   && constructor_max_index
		   && tree_int_cst_lt (constructor_max_index,
				       constructor_index))
	    process_init_element (pop_init_level (1));
	  else
	    break;
	}
    }

  /* Unless this is an explicit brace, we need to preserve previous
     content if any.  */
  if (implicit)
    {
      if ((TREE_CODE (constructor_type) == RECORD_TYPE
	   || TREE_CODE (constructor_type) == UNION_TYPE)
	  && constructor_fields)
	value = find_init_member (constructor_fields);
      else if (TREE_CODE (constructor_type) == ARRAY_TYPE)
	value = find_init_member (constructor_index);
    }

  p = XNEW (struct constructor_stack);
  p->type = constructor_type;
  p->fields = constructor_fields;
  p->index = constructor_index;
  p->max_index = constructor_max_index;
  p->unfilled_index = constructor_unfilled_index;
  p->unfilled_fields = constructor_unfilled_fields;
  p->bit_index = constructor_bit_index;
  p->elements = constructor_elements;
  p->constant = constructor_constant;
  p->simple = constructor_simple;
  p->erroneous = constructor_erroneous;
  p->pending_elts = constructor_pending_elts;
  p->depth = constructor_depth;
  p->replacement_value.value = 0;
  p->replacement_value.original_code = ERROR_MARK;
  p->implicit = implicit;
  p->outer = 0;
  p->incremental = constructor_incremental;
  p->designated = constructor_designated;
  p->next = constructor_stack;
  p->range_stack = 0;
  constructor_stack = p;

  constructor_constant = 1;
  constructor_simple = 1;
  constructor_depth = SPELLING_DEPTH ();
  constructor_elements = 0;
  constructor_incremental = 1;
  constructor_designated = 0;
  constructor_pending_elts = 0;
  if (!implicit)
    {
      p->range_stack = constructor_range_stack;
      constructor_range_stack = 0;
      designator_depth = 0;
      designator_erroneous = 0;
    }

  /* Don't die if an entire brace-pair level is superfluous
     in the containing level.  */
  if (constructor_type == 0)
    ;
  else if (TREE_CODE (constructor_type) == RECORD_TYPE
	   || TREE_CODE (constructor_type) == UNION_TYPE)
    {
      /* Don't die if there are extra init elts at the end.  */
      if (constructor_fields == 0)
	constructor_type = 0;
      else
	{
	  constructor_type = TREE_TYPE (constructor_fields);
	  push_member_name (constructor_fields);
	  constructor_depth++;
	}
    }
  else if (TREE_CODE (constructor_type) == ARRAY_TYPE)
    {
      constructor_type = TREE_TYPE (constructor_type);
      push_array_bounds (tree_low_cst (constructor_index, 1));
      constructor_depth++;
    }

  if (constructor_type == 0)
    {
      error_init ("extra brace group at end of initializer");
      constructor_fields = 0;
      constructor_unfilled_fields = 0;
      return;
    }

  if (value && TREE_CODE (value) == CONSTRUCTOR)
    {
      constructor_constant = TREE_CONSTANT (value);
      constructor_simple = TREE_STATIC (value);
      constructor_elements = CONSTRUCTOR_ELTS (value);
      if (!VEC_empty (constructor_elt, constructor_elements)
	  && (TREE_CODE (constructor_type) == RECORD_TYPE
	      || TREE_CODE (constructor_type) == ARRAY_TYPE))
	set_nonincremental_init ();
    }

  if (TREE_CODE (constructor_type) == RECORD_TYPE
	   || TREE_CODE (constructor_type) == UNION_TYPE)
    {
      constructor_fields = TYPE_FIELDS (constructor_type);
      /* Skip any nameless bit fields at the beginning.  */
      while (constructor_fields != 0 && DECL_C_BIT_FIELD (constructor_fields)
	     && DECL_NAME (constructor_fields) == 0)
	constructor_fields = TREE_CHAIN (constructor_fields);

      constructor_unfilled_fields = constructor_fields;
      constructor_bit_index = bitsize_zero_node;
    }
  else if (TREE_CODE (constructor_type) == VECTOR_TYPE)
    {
      /* Vectors are like simple fixed-size arrays.  */
      constructor_max_index =
	build_int_cst (NULL_TREE, TYPE_VECTOR_SUBPARTS (constructor_type) - 1);
      constructor_index = convert (bitsizetype, integer_zero_node);
      constructor_unfilled_index = constructor_index;
    }
  else if (TREE_CODE (constructor_type) == ARRAY_TYPE)
    {
      if (TYPE_DOMAIN (constructor_type))
	{
	  constructor_max_index
	    = TYPE_MAX_VALUE (TYPE_DOMAIN (constructor_type));

	  /* Detect non-empty initializations of zero-length arrays.  */
	  if (constructor_max_index == NULL_TREE
	      && TYPE_SIZE (constructor_type))
	    constructor_max_index = build_int_cst (NULL_TREE, -1);

	  /* constructor_max_index needs to be an INTEGER_CST.  Attempts
	     to initialize VLAs will cause a proper error; avoid tree
	     checking errors as well by setting a safe value.  */
	  if (constructor_max_index
	      && TREE_CODE (constructor_max_index) != INTEGER_CST)
	    constructor_max_index = build_int_cst (NULL_TREE, -1);

	  constructor_index
	    = convert (bitsizetype,
		       TYPE_MIN_VALUE (TYPE_DOMAIN (constructor_type)));
	}
      else
	constructor_index = bitsize_zero_node;

      constructor_unfilled_index = constructor_index;
      if (value && TREE_CODE (value) == STRING_CST)
	{
	  /* We need to split the char/wchar array into individual
	     characters, so that we don't have to special case it
	     everywhere.  */
	  set_nonincremental_init_from_string (value);
	}
    }
  else
    {
      if (constructor_type != error_mark_node)
	warning_init ("braces around scalar initializer");
      constructor_fields = constructor_type;
      constructor_unfilled_fields = constructor_type;
    }
}

/* At the end of an implicit or explicit brace level,
   finish up that level of constructor.  If a single expression
   with redundant braces initialized that level, return the
   c_expr structure for that expression.  Otherwise, the original_code
   element is set to ERROR_MARK.
   If we were outputting the elements as they are read, return 0 as the value
   from inner levels (process_init_element ignores that),
   but return error_mark_node as the value from the outermost level
   (that's what we want to put in DECL_INITIAL).
   Otherwise, return a CONSTRUCTOR expression as the value.  */

struct c_expr
pop_init_level (int implicit)
{
  struct constructor_stack *p;
  struct c_expr ret;
  ret.value = 0;
  ret.original_code = ERROR_MARK;

  if (implicit == 0)
    {
      /* When we come to an explicit close brace,
	 pop any inner levels that didn't have explicit braces.  */
      while (constructor_stack->implicit)
	process_init_element (pop_init_level (1));

      gcc_assert (!constructor_range_stack);
    }

  /* Now output all pending elements.  */
  constructor_incremental = 1;
  output_pending_init_elements (1);

  p = constructor_stack;

  /* Error for initializing a flexible array member, or a zero-length
     array member in an inappropriate context.  */
  if (constructor_type && constructor_fields
      && TREE_CODE (constructor_type) == ARRAY_TYPE
      && TYPE_DOMAIN (constructor_type)
      && !TYPE_MAX_VALUE (TYPE_DOMAIN (constructor_type)))
    {
      /* Silently discard empty initializations.  The parser will
	 already have pedwarned for empty brackets.  */
      if (integer_zerop (constructor_unfilled_index))
	constructor_type = NULL_TREE;
      else
	{
	  gcc_assert (!TYPE_SIZE (constructor_type));

	  if (constructor_depth > 2)
	    error_init ("initialization of flexible array member in a nested context");
	  else if (pedantic)
	    pedwarn_init ("initialization of a flexible array member");

	  /* We have already issued an error message for the existence
	     of a flexible array member not at the end of the structure.
	     Discard the initializer so that we do not die later.  */
	  if (TREE_CHAIN (constructor_fields) != NULL_TREE)
	    constructor_type = NULL_TREE;
	}
    }

  if (VEC_length (constructor_elt,constructor_elements) == 0)
     constructor_zeroinit = 1;
  else if (VEC_length (constructor_elt,constructor_elements) == 1 &&
      initializer_zerop (VEC_index (constructor_elt,constructor_elements,0)->value))
     constructor_zeroinit = 1;
  else
     constructor_zeroinit = 0;

  /* only warn for missing braces unless it is { 0 } */
  if (p->implicit == 1 && warn_missing_braces && !missing_braces_mentioned &&
      !constructor_zeroinit)
    {
      missing_braces_mentioned = 1;
      warning_init ("missing braces around initializer");
    }

  /* Warn when some struct elements are implicitly initialized to zero.  */
  if (warn_missing_field_initializers
      && constructor_type
      && TREE_CODE (constructor_type) == RECORD_TYPE
      && constructor_unfilled_fields)
    {
	/* Do not warn for flexible array members or zero-length arrays.  */
	while (constructor_unfilled_fields
	       && (!DECL_SIZE (constructor_unfilled_fields)
		   || integer_zerop (DECL_SIZE (constructor_unfilled_fields))))
	  constructor_unfilled_fields = TREE_CHAIN (constructor_unfilled_fields);

	/* Do not warn if this level of the initializer uses member
	   designators; it is likely to be deliberate.  */
	if (constructor_unfilled_fields && !constructor_designated)
	  {
	    push_member_name (constructor_unfilled_fields);
	    warning_init ("missing initializer");
	    RESTORE_SPELLING_DEPTH (constructor_depth);
	  }
    }

  /* Pad out the end of the structure.  */
  if (p->replacement_value.value)
    /* If this closes a superfluous brace pair,
       just pass out the element between them.  */
    ret = p->replacement_value;
  else if (constructor_type == 0)
    ;
  else if (TREE_CODE (constructor_type) != RECORD_TYPE
	   && TREE_CODE (constructor_type) != UNION_TYPE
	   && TREE_CODE (constructor_type) != ARRAY_TYPE
	   && TREE_CODE (constructor_type) != VECTOR_TYPE)
    {
      /* A nonincremental scalar initializer--just return
	 the element, after verifying there is just one.  */
      if (VEC_empty (constructor_elt,constructor_elements))
	{
	  if (!constructor_erroneous)
	    error_init ("empty scalar initializer");
	  ret.value = error_mark_node;
	}
      else if (VEC_length (constructor_elt,constructor_elements) != 1)
	{
	  error_init ("extra elements in scalar initializer");
	  ret.value = VEC_index (constructor_elt,constructor_elements,0)->value;
	}
      else
	ret.value = VEC_index (constructor_elt,constructor_elements,0)->value;
    }
  else
    {
      if (constructor_erroneous)
	ret.value = error_mark_node;
      else
	{
	  ret.value = build_constructor (constructor_type,
					 constructor_elements);
	  if (constructor_constant)
	    TREE_CONSTANT (ret.value) = TREE_INVARIANT (ret.value) = 1;
	  if (constructor_constant && constructor_simple)
	    TREE_STATIC (ret.value) = 1;
	}
    }

  constructor_type = p->type;
  constructor_fields = p->fields;
  constructor_index = p->index;
  constructor_max_index = p->max_index;
  constructor_unfilled_index = p->unfilled_index;
  constructor_unfilled_fields = p->unfilled_fields;
  constructor_bit_index = p->bit_index;
  constructor_elements = p->elements;
  constructor_constant = p->constant;
  constructor_simple = p->simple;
  constructor_erroneous = p->erroneous;
  constructor_incremental = p->incremental;
  constructor_designated = p->designated;
  constructor_pending_elts = p->pending_elts;
  constructor_depth = p->depth;
  if (!p->implicit)
    constructor_range_stack = p->range_stack;
  RESTORE_SPELLING_DEPTH (constructor_depth);

  constructor_stack = p->next;
  free (p);

  if (ret.value == 0 && constructor_stack == 0)
    ret.value = error_mark_node;
  return ret;
}

/* Common handling for both array range and field name designators.
   ARRAY argument is nonzero for array ranges.  Returns zero for success.  */

static int
set_designator (int array)
{
  tree subtype;
  enum tree_code subcode;

  /* Don't die if an entire brace-pair level is superfluous
     in the containing level.  */
  if (constructor_type == 0)
    return 1;

  /* If there were errors in this designator list already, bail out
     silently.  */
  if (designator_erroneous)
    return 1;

  if (!designator_depth)
    {
      gcc_assert (!constructor_range_stack);

      /* Designator list starts at the level of closest explicit
	 braces.  */
      while (constructor_stack->implicit)
	process_init_element (pop_init_level (1));
      constructor_designated = 1;
      return 0;
    }

  switch (TREE_CODE (constructor_type))
    {
    case  RECORD_TYPE:
    case  UNION_TYPE:
      subtype = TREE_TYPE (constructor_fields);
      if (subtype != error_mark_node)
	subtype = TYPE_MAIN_VARIANT (subtype);
      break;
    case ARRAY_TYPE:
      subtype = TYPE_MAIN_VARIANT (TREE_TYPE (constructor_type));
      break;
    default:
      gcc_unreachable ();
    }

  subcode = TREE_CODE (subtype);
  if (array && subcode != ARRAY_TYPE)
    {
      error_init ("array index in non-array initializer");
      return 1;
    }
  else if (!array && subcode != RECORD_TYPE && subcode != UNION_TYPE)
    {
      error_init ("field name not in record or union initializer");
      return 1;
    }

  constructor_designated = 1;
  push_init_level (2);
  return 0;
}

/* If there are range designators in designator list, push a new designator
   to constructor_range_stack.  RANGE_END is end of such stack range or
   NULL_TREE if there is no range designator at this level.  */

static void
push_range_stack (tree range_end)
{
  struct constructor_range_stack *p;

  p = GGC_NEW (struct constructor_range_stack);
  p->prev = constructor_range_stack;
  p->next = 0;
  p->fields = constructor_fields;
  p->range_start = constructor_index;
  p->index = constructor_index;
  p->stack = constructor_stack;
  p->range_end = range_end;
  if (constructor_range_stack)
    constructor_range_stack->next = p;
  constructor_range_stack = p;
}

/* Within an array initializer, specify the next index to be initialized.
   FIRST is that index.  If LAST is nonzero, then initialize a range
   of indices, running from FIRST through LAST.  */

void
set_init_index (tree first, tree last)
{
  if (set_designator (1))
    return;

  designator_erroneous = 1;

  if (!INTEGRAL_TYPE_P (TREE_TYPE (first))
      || (last && !INTEGRAL_TYPE_P (TREE_TYPE (last))))
    {
      error_init ("array index in initializer not of integer type");
      return;
    }

  if (TREE_CODE (first) != INTEGER_CST)
    error_init ("nonconstant array index in initializer");
  else if (last != 0 && TREE_CODE (last) != INTEGER_CST)
    error_init ("nonconstant array index in initializer");
  else if (TREE_CODE (constructor_type) != ARRAY_TYPE)
    error_init ("array index in non-array initializer");
  else if (tree_int_cst_sgn (first) == -1)
    error_init ("array index in initializer exceeds array bounds");
  else if (constructor_max_index
	   && tree_int_cst_lt (constructor_max_index, first))
    error_init ("array index in initializer exceeds array bounds");
  else
    {
      constructor_index = convert (bitsizetype, first);

      if (last)
	{
	  if (tree_int_cst_equal (first, last))
	    last = 0;
	  else if (tree_int_cst_lt (last, first))
	    {
	      error_init ("empty index range in initializer");
	      last = 0;
	    }
	  else
	    {
	      last = convert (bitsizetype, last);
	      if (constructor_max_index != 0
		  && tree_int_cst_lt (constructor_max_index, last))
		{
		  error_init ("array index range in initializer exceeds array bounds");
		  last = 0;
		}
	    }
	}

      designator_depth++;
      designator_erroneous = 0;
      if (constructor_range_stack || last)
	push_range_stack (last);
    }
}

/* Within a struct initializer, specify the next field to be initialized.  */

void
set_init_label (tree fieldname)
{
  tree anon = NULL_TREE;
  tree tail;

  if (set_designator (0))
    return;

  designator_erroneous = 1;

  if (TREE_CODE (constructor_type) != RECORD_TYPE
      && TREE_CODE (constructor_type) != UNION_TYPE)
    {
      error_init ("field name not in record or union initializer");
      return;
    }

  for (tail = TYPE_FIELDS (constructor_type); tail;
       tail = TREE_CHAIN (tail))
    {
      if (DECL_NAME (tail) == NULL_TREE
	  && (TREE_CODE (TREE_TYPE (tail)) == RECORD_TYPE
	      || TREE_CODE (TREE_TYPE (tail)) == UNION_TYPE))
	{
	  anon = lookup_field (tail, fieldname);
	  if (anon)
	    break;
	}

      if (DECL_NAME (tail) == fieldname)
	break;
    }

  if (tail == 0)
    error ("unknown field %qE specified in initializer", fieldname);

  while (tail)
    {
      constructor_fields = tail;
      designator_depth++;
      designator_erroneous = 0;
      if (constructor_range_stack)
	push_range_stack (NULL_TREE);

      if (anon)
	{
	  if (set_designator (0))
	    return;
	  tail = TREE_VALUE(anon);
	  anon = TREE_CHAIN(anon);
	}
      else
	tail = NULL_TREE;
    }
}

/* Add a new initializer to the tree of pending initializers.  PURPOSE
   identifies the initializer, either array index or field in a structure.
   VALUE is the value of that index or field.  */

static void
add_pending_init (tree purpose, tree value)
{
  struct init_node *p, **q, *r;

  q = &constructor_pending_elts;
  p = 0;

  if (TREE_CODE (constructor_type) == ARRAY_TYPE)
    {
      while (*q != 0)
	{
	  p = *q;
	  if (tree_int_cst_lt (purpose, p->purpose))
	    q = &p->left;
	  else if (tree_int_cst_lt (p->purpose, purpose))
	    q = &p->right;
	  else
	    {
	      if (TREE_SIDE_EFFECTS (p->value))
		warning_init ("initialized field with side-effects overwritten");
	      else if (warn_override_init)
		warning_init ("initialized field overwritten");
	      p->value = value;
	      return;
	    }
	}
    }
  else
    {
      tree bitpos;

      bitpos = bit_position (purpose);
      while (*q != NULL)
	{
	  p = *q;
	  if (tree_int_cst_lt (bitpos, bit_position (p->purpose)))
	    q = &p->left;
	  else if (p->purpose != purpose)
	    q = &p->right;
	  else
	    {
	      if (TREE_SIDE_EFFECTS (p->value))
		warning_init ("initialized field with side-effects overwritten");
	      else if (warn_override_init)
		warning_init ("initialized field overwritten");
	      p->value = value;
	      return;
	    }
	}
    }

  r = GGC_NEW (struct init_node);
  r->purpose = purpose;
  r->value = value;

  *q = r;
  r->parent = p;
  r->left = 0;
  r->right = 0;
  r->balance = 0;

  while (p)
    {
      struct init_node *s;

      if (r == p->left)
	{
	  if (p->balance == 0)
	    p->balance = -1;
	  else if (p->balance < 0)
	    {
	      if (r->balance < 0)
		{
		  /* L rotation.  */
		  p->left = r->right;
		  if (p->left)
		    p->left->parent = p;
		  r->right = p;

		  p->balance = 0;
		  r->balance = 0;

		  s = p->parent;
		  p->parent = r;
		  r->parent = s;
		  if (s)
		    {
		      if (s->left == p)
			s->left = r;
		      else
			s->right = r;
		    }
		  else
		    constructor_pending_elts = r;
		}
	      else
		{
		  /* LR rotation.  */
		  struct init_node *t = r->right;

		  r->right = t->left;
		  if (r->right)
		    r->right->parent = r;
		  t->left = r;

		  p->left = t->right;
		  if (p->left)
		    p->left->parent = p;
		  t->right = p;

		  p->balance = t->balance < 0;
		  r->balance = -(t->balance > 0);
		  t->balance = 0;

		  s = p->parent;
		  p->parent = t;
		  r->parent = t;
		  t->parent = s;
		  if (s)
		    {
		      if (s->left == p)
			s->left = t;
		      else
			s->right = t;
		    }
		  else
		    constructor_pending_elts = t;
		}
	      break;
	    }
	  else
	    {
	      /* p->balance == +1; growth of left side balances the node.  */
	      p->balance = 0;
	      break;
	    }
	}
      else /* r == p->right */
	{
	  if (p->balance == 0)
	    /* Growth propagation from right side.  */
	    p->balance++;
	  else if (p->balance > 0)
	    {
	      if (r->balance > 0)
		{
		  /* R rotation.  */
		  p->right = r->left;
		  if (p->right)
		    p->right->parent = p;
		  r->left = p;

		  p->balance = 0;
		  r->balance = 0;

		  s = p->parent;
		  p->parent = r;
		  r->parent = s;
		  if (s)
		    {
		      if (s->left == p)
			s->left = r;
		      else
			s->right = r;
		    }
		  else
		    constructor_pending_elts = r;
		}
	      else /* r->balance == -1 */
		{
		  /* RL rotation */
		  struct init_node *t = r->left;

		  r->left = t->right;
		  if (r->left)
		    r->left->parent = r;
		  t->right = r;

		  p->right = t->left;
		  if (p->right)
		    p->right->parent = p;
		  t->left = p;

		  r->balance = (t->balance < 0);
		  p->balance = -(t->balance > 0);
		  t->balance = 0;

		  s = p->parent;
		  p->parent = t;
		  r->parent = t;
		  t->parent = s;
		  if (s)
		    {
		      if (s->left == p)
			s->left = t;
		      else
			s->right = t;
		    }
		  else
		    constructor_pending_elts = t;
		}
	      break;
	    }
	  else
	    {
	      /* p->balance == -1; growth of right side balances the node.  */
	      p->balance = 0;
	      break;
	    }
	}

      r = p;
      p = p->parent;
    }
}

/* Build AVL tree from a sorted chain.  */

static void
set_nonincremental_init (void)
{
  unsigned HOST_WIDE_INT ix;
  tree index, value;

  if (TREE_CODE (constructor_type) != RECORD_TYPE
      && TREE_CODE (constructor_type) != ARRAY_TYPE)
    return;

  FOR_EACH_CONSTRUCTOR_ELT (constructor_elements, ix, index, value)
    add_pending_init (index, value);
  constructor_elements = 0;
  if (TREE_CODE (constructor_type) == RECORD_TYPE)
    {
      constructor_unfilled_fields = TYPE_FIELDS (constructor_type);
      /* Skip any nameless bit fields at the beginning.  */
      while (constructor_unfilled_fields != 0
	     && DECL_C_BIT_FIELD (constructor_unfilled_fields)
	     && DECL_NAME (constructor_unfilled_fields) == 0)
	constructor_unfilled_fields = TREE_CHAIN (constructor_unfilled_fields);

    }
  else if (TREE_CODE (constructor_type) == ARRAY_TYPE)
    {
      if (TYPE_DOMAIN (constructor_type))
	constructor_unfilled_index
	    = convert (bitsizetype,
		       TYPE_MIN_VALUE (TYPE_DOMAIN (constructor_type)));
      else
	constructor_unfilled_index = bitsize_zero_node;
    }
  constructor_incremental = 0;
}

/* Build AVL tree from a string constant.  */

static void
set_nonincremental_init_from_string (tree str)
{
  tree value, purpose, type;
  HOST_WIDE_INT val[2];
  const char *p, *end;
  int byte, wchar_bytes, charwidth, bitpos;

  gcc_assert (TREE_CODE (constructor_type) == ARRAY_TYPE);

  if (TYPE_PRECISION (TREE_TYPE (TREE_TYPE (str)))
      == TYPE_PRECISION (char_type_node))
    wchar_bytes = 1;
  else
    {
      gcc_assert (TYPE_PRECISION (TREE_TYPE (TREE_TYPE (str)))
		  == TYPE_PRECISION (wchar_type_node));
      wchar_bytes = TYPE_PRECISION (wchar_type_node) / BITS_PER_UNIT;
    }
  charwidth = TYPE_PRECISION (char_type_node);
  type = TREE_TYPE (constructor_type);
  p = TREE_STRING_POINTER (str);
  end = p + TREE_STRING_LENGTH (str);

  for (purpose = bitsize_zero_node;
       p < end && !tree_int_cst_lt (constructor_max_index, purpose);
       purpose = size_binop (PLUS_EXPR, purpose, bitsize_one_node))
    {
      if (wchar_bytes == 1)
	{
	  val[1] = (unsigned char) *p++;
	  val[0] = 0;
	}
      else
	{
	  val[0] = 0;
	  val[1] = 0;
	  for (byte = 0; byte < wchar_bytes; byte++)
	    {
	      if (BYTES_BIG_ENDIAN)
		bitpos = (wchar_bytes - byte - 1) * charwidth;
	      else
		bitpos = byte * charwidth;
	      val[bitpos < HOST_BITS_PER_WIDE_INT]
		|= ((unsigned HOST_WIDE_INT) ((unsigned char) *p++))
		   << (bitpos % HOST_BITS_PER_WIDE_INT);
	    }
	}

      if (!TYPE_UNSIGNED (type))
	{
	  bitpos = ((wchar_bytes - 1) * charwidth) + HOST_BITS_PER_CHAR;
	  if (bitpos < HOST_BITS_PER_WIDE_INT)
	    {
	      if (val[1] & (((HOST_WIDE_INT) 1) << (bitpos - 1)))
		{
		  val[1] |= ((HOST_WIDE_INT) -1) << bitpos;
		  val[0] = -1;
		}
	    }
	  else if (bitpos == HOST_BITS_PER_WIDE_INT)
	    {
	      if (val[1] < 0)
		val[0] = -1;
	    }
	  else if (val[0] & (((HOST_WIDE_INT) 1)
			     << (bitpos - 1 - HOST_BITS_PER_WIDE_INT)))
	    val[0] |= ((HOST_WIDE_INT) -1)
		      << (bitpos - HOST_BITS_PER_WIDE_INT);
	}

      value = build_int_cst_wide (type, val[1], val[0]);
      add_pending_init (purpose, value);
    }

  constructor_incremental = 0;
}

/* Return value of FIELD in pending initializer or zero if the field was
   not initialized yet.  */

static tree
find_init_member (tree field)
{
  struct init_node *p;

  if (TREE_CODE (constructor_type) == ARRAY_TYPE)
    {
      if (constructor_incremental
	  && tree_int_cst_lt (field, constructor_unfilled_index))
	set_nonincremental_init ();

      p = constructor_pending_elts;
      while (p)
	{
	  if (tree_int_cst_lt (field, p->purpose))
	    p = p->left;
	  else if (tree_int_cst_lt (p->purpose, field))
	    p = p->right;
	  else
	    return p->value;
	}
    }
  else if (TREE_CODE (constructor_type) == RECORD_TYPE)
    {
      tree bitpos = bit_position (field);

      if (constructor_incremental
	  && (!constructor_unfilled_fields
	      || tree_int_cst_lt (bitpos,
				  bit_position (constructor_unfilled_fields))))
	set_nonincremental_init ();

      p = constructor_pending_elts;
      while (p)
	{
	  if (field == p->purpose)
	    return p->value;
	  else if (tree_int_cst_lt (bitpos, bit_position (p->purpose)))
	    p = p->left;
	  else
	    p = p->right;
	}
    }
  else if (TREE_CODE (constructor_type) == UNION_TYPE)
    {
      if (!VEC_empty (constructor_elt, constructor_elements)
	  && (VEC_last (constructor_elt, constructor_elements)->index
	      == field))
	return VEC_last (constructor_elt, constructor_elements)->value;
    }
  return 0;
}

/* "Output" the next constructor element.
   At top level, really output it to assembler code now.
   Otherwise, collect it in a list from which we will make a CONSTRUCTOR.
   TYPE is the data type that the containing data type wants here.
   FIELD is the field (a FIELD_DECL) or the index that this element fills.
   If VALUE is a string constant, STRICT_STRING is true if it is
   unparenthesized or we should not warn here for it being parenthesized.
   For other types of VALUE, STRICT_STRING is not used.

   PENDING if non-nil means output pending elements that belong
   right after this element.  (PENDING is normally 1;
   it is 0 while outputting pending elements, to avoid recursion.)  */

static void
output_init_element (tree value, bool strict_string, tree type, tree field,
		     int pending)
{
  constructor_elt *celt;

  if (type == error_mark_node || value == error_mark_node)
    {
      constructor_erroneous = 1;
      return;
    }
  if (TREE_CODE (TREE_TYPE (value)) == ARRAY_TYPE
      && (TREE_CODE (value) == STRING_CST
	  || TREE_CODE (value) == COMPOUND_LITERAL_EXPR)
      && !(TREE_CODE (value) == STRING_CST
	   && TREE_CODE (type) == ARRAY_TYPE
	   && INTEGRAL_TYPE_P (TREE_TYPE (type)))
      && !comptypes (TYPE_MAIN_VARIANT (TREE_TYPE (value)),
		     TYPE_MAIN_VARIANT (type)))
    value = array_to_pointer_conversion (value);

  if (TREE_CODE (value) == COMPOUND_LITERAL_EXPR
      && require_constant_value && !flag_isoc99 && pending)
    {
      /* As an extension, allow initializing objects with static storage
	 duration with compound literals (which are then treated just as
	 the brace enclosed list they contain).  */
      tree decl = COMPOUND_LITERAL_EXPR_DECL (value);
      value = DECL_INITIAL (decl);
    }

  if (value == error_mark_node)
    constructor_erroneous = 1;
  else if (!TREE_CONSTANT (value))
    constructor_constant = 0;
  else if (!initializer_constant_valid_p (value, TREE_TYPE (value))
	   || ((TREE_CODE (constructor_type) == RECORD_TYPE
		|| TREE_CODE (constructor_type) == UNION_TYPE)
	       && DECL_C_BIT_FIELD (field)
	       && TREE_CODE (value) != INTEGER_CST))
    constructor_simple = 0;

  if (!initializer_constant_valid_p (value, TREE_TYPE (value)))
    {
      if (require_constant_value)
	{
	  error_init ("initializer element is not constant");
	  value = error_mark_node;
	}
      else if (require_constant_elements)
	pedwarn ("initializer element is not computable at load time");
    }

  /* If this field is empty (and not at the end of structure),
     don't do anything other than checking the initializer.  */
  if (field
      && (TREE_TYPE (field) == error_mark_node
	  || (COMPLETE_TYPE_P (TREE_TYPE (field))
	      && integer_zerop (TYPE_SIZE (TREE_TYPE (field)))
	      && (TREE_CODE (constructor_type) == ARRAY_TYPE
		  || TREE_CHAIN (field)))))
    return;

  value = digest_init (type, value, strict_string, require_constant_value);
  if (value == error_mark_node)
    {
      constructor_erroneous = 1;
      return;
    }

  /* If this element doesn't come next in sequence,
     put it on constructor_pending_elts.  */
  if (TREE_CODE (constructor_type) == ARRAY_TYPE
      && (!constructor_incremental
	  || !tree_int_cst_equal (field, constructor_unfilled_index)))
    {
      if (constructor_incremental
	  && tree_int_cst_lt (field, constructor_unfilled_index))
	set_nonincremental_init ();

      add_pending_init (field, value);
      return;
    }
  else if (TREE_CODE (constructor_type) == RECORD_TYPE
	   && (!constructor_incremental
	       || field != constructor_unfilled_fields))
    {
      /* We do this for records but not for unions.  In a union,
	 no matter which field is specified, it can be initialized
	 right away since it starts at the beginning of the union.  */
      if (constructor_incremental)
	{
	  if (!constructor_unfilled_fields)
	    set_nonincremental_init ();
	  else
	    {
	      tree bitpos, unfillpos;

	      bitpos = bit_position (field);
	      unfillpos = bit_position (constructor_unfilled_fields);

	      if (tree_int_cst_lt (bitpos, unfillpos))
		set_nonincremental_init ();
	    }
	}

      add_pending_init (field, value);
      return;
    }
  else if (TREE_CODE (constructor_type) == UNION_TYPE
	   && !VEC_empty (constructor_elt, constructor_elements))
    {
      if (TREE_SIDE_EFFECTS (VEC_last (constructor_elt,
				       constructor_elements)->value))
	warning_init ("initialized field with side-effects overwritten");
      else if (warn_override_init)
	warning_init ("initialized field overwritten");

      /* We can have just one union field set.  */
      constructor_elements = 0;
    }

  /* Otherwise, output this element either to
     constructor_elements or to the assembler file.  */

  celt = VEC_safe_push (constructor_elt, gc, constructor_elements, NULL);
  celt->index = field;
  celt->value = value;

  /* Advance the variable that indicates sequential elements output.  */
  if (TREE_CODE (constructor_type) == ARRAY_TYPE)
    constructor_unfilled_index
      = size_binop (PLUS_EXPR, constructor_unfilled_index,
		    bitsize_one_node);
  else if (TREE_CODE (constructor_type) == RECORD_TYPE)
    {
      constructor_unfilled_fields
	= TREE_CHAIN (constructor_unfilled_fields);

      /* Skip any nameless bit fields.  */
      while (constructor_unfilled_fields != 0
	     && DECL_C_BIT_FIELD (constructor_unfilled_fields)
	     && DECL_NAME (constructor_unfilled_fields) == 0)
	constructor_unfilled_fields =
	  TREE_CHAIN (constructor_unfilled_fields);
    }
  else if (TREE_CODE (constructor_type) == UNION_TYPE)
    constructor_unfilled_fields = 0;

  /* Now output any pending elements which have become next.  */
  if (pending)
    output_pending_init_elements (0);
}

/* Output any pending elements which have become next.
   As we output elements, constructor_unfilled_{fields,index}
   advances, which may cause other elements to become next;
   if so, they too are output.

   If ALL is 0, we return when there are
   no more pending elements to output now.

   If ALL is 1, we output space as necessary so that
   we can output all the pending elements.  */

static void
output_pending_init_elements (int all)
{
  struct init_node *elt = constructor_pending_elts;
  tree next;

 retry:

  /* Look through the whole pending tree.
     If we find an element that should be output now,
     output it.  Otherwise, set NEXT to the element
     that comes first among those still pending.  */

  next = 0;
  while (elt)
    {
      if (TREE_CODE (constructor_type) == ARRAY_TYPE)
	{
	  if (tree_int_cst_equal (elt->purpose,
				  constructor_unfilled_index))
	    output_init_element (elt->value, true,
				 TREE_TYPE (constructor_type),
				 constructor_unfilled_index, 0);
	  else if (tree_int_cst_lt (constructor_unfilled_index,
				    elt->purpose))
	    {
	      /* Advance to the next smaller node.  */
	      if (elt->left)
		elt = elt->left;
	      else
		{
		  /* We have reached the smallest node bigger than the
		     current unfilled index.  Fill the space first.  */
		  next = elt->purpose;
		  break;
		}
	    }
	  else
	    {
	      /* Advance to the next bigger node.  */
	      if (elt->right)
		elt = elt->right;
	      else
		{
		  /* We have reached the biggest node in a subtree.  Find
		     the parent of it, which is the next bigger node.  */
		  while (elt->parent && elt->parent->right == elt)
		    elt = elt->parent;
		  elt = elt->parent;
		  if (elt && tree_int_cst_lt (constructor_unfilled_index,
					      elt->purpose))
		    {
		      next = elt->purpose;
		      break;
		    }
		}
	    }
	}
      else if (TREE_CODE (constructor_type) == RECORD_TYPE
	       || TREE_CODE (constructor_type) == UNION_TYPE)
	{
	  tree ctor_unfilled_bitpos, elt_bitpos;

	  /* If the current record is complete we are done.  */
	  if (constructor_unfilled_fields == 0)
	    break;

	  ctor_unfilled_bitpos = bit_position (constructor_unfilled_fields);
	  elt_bitpos = bit_position (elt->purpose);
	  /* We can't compare fields here because there might be empty
	     fields in between.  */
	  if (tree_int_cst_equal (elt_bitpos, ctor_unfilled_bitpos))
	    {
	      constructor_unfilled_fields = elt->purpose;
	      output_init_element (elt->value, true, TREE_TYPE (elt->purpose),
				   elt->purpose, 0);
	    }
	  else if (tree_int_cst_lt (ctor_unfilled_bitpos, elt_bitpos))
	    {
	      /* Advance to the next smaller node.  */
	      if (elt->left)
		elt = elt->left;
	      else
		{
		  /* We have reached the smallest node bigger than the
		     current unfilled field.  Fill the space first.  */
		  next = elt->purpose;
		  break;
		}
	    }
	  else
	    {
	      /* Advance to the next bigger node.  */
	      if (elt->right)
		elt = elt->right;
	      else
		{
		  /* We have reached the biggest node in a subtree.  Find
		     the parent of it, which is the next bigger node.  */
		  while (elt->parent && elt->parent->right == elt)
		    elt = elt->parent;
		  elt = elt->parent;
		  if (elt
		      && (tree_int_cst_lt (ctor_unfilled_bitpos,
					   bit_position (elt->purpose))))
		    {
		      next = elt->purpose;
		      break;
		    }
		}
	    }
	}
    }

  /* Ordinarily return, but not if we want to output all
     and there are elements left.  */
  if (!(all && next != 0))
    return;

  /* If it's not incremental, just skip over the gap, so that after
     jumping to retry we will output the next successive element.  */
  if (TREE_CODE (constructor_type) == RECORD_TYPE
      || TREE_CODE (constructor_type) == UNION_TYPE)
    constructor_unfilled_fields = next;
  else if (TREE_CODE (constructor_type) == ARRAY_TYPE)
    constructor_unfilled_index = next;

  /* ELT now points to the node in the pending tree with the next
     initializer to output.  */
  goto retry;
}

/* Add one non-braced element to the current constructor level.
   This adjusts the current position within the constructor's type.
   This may also start or terminate implicit levels
   to handle a partly-braced initializer.

   Once this has found the correct level for the new element,
   it calls output_init_element.  */

void
process_init_element (struct c_expr value)
{
  tree orig_value = value.value;
  int string_flag = orig_value != 0 && TREE_CODE (orig_value) == STRING_CST;
  bool strict_string = value.original_code == STRING_CST;

  designator_depth = 0;
  designator_erroneous = 0;

  /* Handle superfluous braces around string cst as in
     char x[] = {"foo"}; */
  if (string_flag
      && constructor_type
      && TREE_CODE (constructor_type) == ARRAY_TYPE
      && INTEGRAL_TYPE_P (TREE_TYPE (constructor_type))
      && integer_zerop (constructor_unfilled_index))
    {
      if (constructor_stack->replacement_value.value)
	error_init ("excess elements in char array initializer");
      constructor_stack->replacement_value = value;
      return;
    }

  if (constructor_stack->replacement_value.value != 0)
    {
      error_init ("excess elements in struct initializer");
      return;
    }

  /* Ignore elements of a brace group if it is entirely superfluous
     and has already been diagnosed.  */
  if (constructor_type == 0)
    return;

  /* If we've exhausted any levels that didn't have braces,
     pop them now.  */
  while (constructor_stack->implicit)
    {
      if ((TREE_CODE (constructor_type) == RECORD_TYPE
	   || TREE_CODE (constructor_type) == UNION_TYPE)
	  && constructor_fields == 0)
	process_init_element (pop_init_level (1));
      else if (TREE_CODE (constructor_type) == ARRAY_TYPE
	       && (constructor_max_index == 0
		   || tree_int_cst_lt (constructor_max_index,
				       constructor_index)))
	process_init_element (pop_init_level (1));
      else
	break;
    }

  /* In the case of [LO ... HI] = VALUE, only evaluate VALUE once.  */
  if (constructor_range_stack)
    {
      /* If value is a compound literal and we'll be just using its
	 content, don't put it into a SAVE_EXPR.  */
      if (TREE_CODE (value.value) != COMPOUND_LITERAL_EXPR
	  || !require_constant_value
	  || flag_isoc99)
	value.value = save_expr (value.value);
    }

  while (1)
    {
      if (TREE_CODE (constructor_type) == RECORD_TYPE)
	{
	  tree fieldtype;
	  enum tree_code fieldcode;

	  if (constructor_fields == 0)
	    {
	      pedwarn_init ("excess elements in struct initializer");
	      break;
	    }

	  fieldtype = TREE_TYPE (constructor_fields);
	  if (fieldtype != error_mark_node)
	    fieldtype = TYPE_MAIN_VARIANT (fieldtype);
	  fieldcode = TREE_CODE (fieldtype);

	  /* Error for non-static initialization of a flexible array member.  */
	  if (fieldcode == ARRAY_TYPE
	      && !require_constant_value
	      && TYPE_SIZE (fieldtype) == NULL_TREE
	      && TREE_CHAIN (constructor_fields) == NULL_TREE)
	    {
	      error_init ("non-static initialization of a flexible array member");
	      break;
	    }

	  /* Accept a string constant to initialize a subarray.  */
	  if (value.value != 0
	      && fieldcode == ARRAY_TYPE
	      && INTEGRAL_TYPE_P (TREE_TYPE (fieldtype))
	      && string_flag)
	    value.value = orig_value;
	  /* Otherwise, if we have come to a subaggregate,
	     and we don't have an element of its type, push into it.  */
	  else if (value.value != 0
		   && value.value != error_mark_node
		   && TYPE_MAIN_VARIANT (TREE_TYPE (value.value)) != fieldtype
		   && (fieldcode == RECORD_TYPE || fieldcode == ARRAY_TYPE
		       || fieldcode == UNION_TYPE))
	    {
	      push_init_level (1);
	      continue;
	    }

	  if (value.value)
	    {
	      push_member_name (constructor_fields);
	      output_init_element (value.value, strict_string,
				   fieldtype, constructor_fields, 1);
	      RESTORE_SPELLING_DEPTH (constructor_depth);
	    }
	  else
	    /* Do the bookkeeping for an element that was
	       directly output as a constructor.  */
	    {
	      /* For a record, keep track of end position of last field.  */
	      if (DECL_SIZE (constructor_fields))
		constructor_bit_index
		  = size_binop (PLUS_EXPR,
				bit_position (constructor_fields),
				DECL_SIZE (constructor_fields));

	      /* If the current field was the first one not yet written out,
		 it isn't now, so update.  */
	      if (constructor_unfilled_fields == constructor_fields)
		{
		  constructor_unfilled_fields = TREE_CHAIN (constructor_fields);
		  /* Skip any nameless bit fields.  */
		  while (constructor_unfilled_fields != 0
			 && DECL_C_BIT_FIELD (constructor_unfilled_fields)
			 && DECL_NAME (constructor_unfilled_fields) == 0)
		    constructor_unfilled_fields =
		      TREE_CHAIN (constructor_unfilled_fields);
		}
	    }

	  constructor_fields = TREE_CHAIN (constructor_fields);
	  /* Skip any nameless bit fields at the beginning.  */
	  while (constructor_fields != 0
		 && DECL_C_BIT_FIELD (constructor_fields)
		 && DECL_NAME (constructor_fields) == 0)
	    constructor_fields = TREE_CHAIN (constructor_fields);
	}
      else if (TREE_CODE (constructor_type) == UNION_TYPE)
	{
	  tree fieldtype;
	  enum tree_code fieldcode;

	  if (constructor_fields == 0)
	    {
	      pedwarn_init ("excess elements in union initializer");
	      break;
	    }

	  fieldtype = TREE_TYPE (constructor_fields);
	  if (fieldtype != error_mark_node)
	    fieldtype = TYPE_MAIN_VARIANT (fieldtype);
	  fieldcode = TREE_CODE (fieldtype);

	  /* Warn that traditional C rejects initialization of unions.
	     We skip the warning if the value is zero.  This is done
	     under the assumption that the zero initializer in user
	     code appears conditioned on e.g. __STDC__ to avoid
	     "missing initializer" warnings and relies on default
	     initialization to zero in the traditional C case.
	     We also skip the warning if the initializer is designated,
	     again on the assumption that this must be conditional on
	     __STDC__ anyway (and we've already complained about the
	     member-designator already).  */
	  if (!in_system_header && !constructor_designated
	      && !(value.value && (integer_zerop (value.value)
				   || real_zerop (value.value))))
	    warning (OPT_Wtraditional, "traditional C rejects initialization "
		     "of unions");

	  /* Accept a string constant to initialize a subarray.  */
	  if (value.value != 0
	      && fieldcode == ARRAY_TYPE
	      && INTEGRAL_TYPE_P (TREE_TYPE (fieldtype))
	      && string_flag)
	    value.value = orig_value;
	  /* Otherwise, if we have come to a subaggregate,
	     and we don't have an element of its type, push into it.  */
	  else if (value.value != 0
		   && value.value != error_mark_node
		   && TYPE_MAIN_VARIANT (TREE_TYPE (value.value)) != fieldtype
		   && (fieldcode == RECORD_TYPE || fieldcode == ARRAY_TYPE
		       || fieldcode == UNION_TYPE))
	    {
	      push_init_level (1);
	      continue;
	    }

	  if (value.value)
	    {
	      push_member_name (constructor_fields);
	      output_init_element (value.value, strict_string,
				   fieldtype, constructor_fields, 1);
	      RESTORE_SPELLING_DEPTH (constructor_depth);
	    }
	  else
	    /* Do the bookkeeping for an element that was
	       directly output as a constructor.  */
	    {
	      constructor_bit_index = DECL_SIZE (constructor_fields);
	      constructor_unfilled_fields = TREE_CHAIN (constructor_fields);
	    }

	  constructor_fields = 0;
	}
      else if (TREE_CODE (constructor_type) == ARRAY_TYPE)
	{
	  tree elttype = TYPE_MAIN_VARIANT (TREE_TYPE (constructor_type));
	  enum tree_code eltcode = TREE_CODE (elttype);

	  /* Accept a string constant to initialize a subarray.  */
	  if (value.value != 0
	      && eltcode == ARRAY_TYPE
	      && INTEGRAL_TYPE_P (TREE_TYPE (elttype))
	      && string_flag)
	    value.value = orig_value;
	  /* Otherwise, if we have come to a subaggregate,
	     and we don't have an element of its type, push into it.  */
	  else if (value.value != 0
		   && value.value != error_mark_node
		   && TYPE_MAIN_VARIANT (TREE_TYPE (value.value)) != elttype
		   && (eltcode == RECORD_TYPE || eltcode == ARRAY_TYPE
		       || eltcode == UNION_TYPE))
	    {
	      push_init_level (1);
	      continue;
	    }

	  if (constructor_max_index != 0
	      && (tree_int_cst_lt (constructor_max_index, constructor_index)
		  || integer_all_onesp (constructor_max_index)))
	    {
	      pedwarn_init ("excess elements in array initializer");
	      break;
	    }

	  /* Now output the actual element.  */
	  if (value.value)
	    {
	      push_array_bounds (tree_low_cst (constructor_index, 1));
	      output_init_element (value.value, strict_string,
				   elttype, constructor_index, 1);
	      RESTORE_SPELLING_DEPTH (constructor_depth);
	    }

	  constructor_index
	    = size_binop (PLUS_EXPR, constructor_index, bitsize_one_node);

	  if (!value.value)
	    /* If we are doing the bookkeeping for an element that was
	       directly output as a constructor, we must update
	       constructor_unfilled_index.  */
	    constructor_unfilled_index = constructor_index;
	}
      else if (TREE_CODE (constructor_type) == VECTOR_TYPE)
	{
	  tree elttype = TYPE_MAIN_VARIANT (TREE_TYPE (constructor_type));

	 /* Do a basic check of initializer size.  Note that vectors
	    always have a fixed size derived from their type.  */
	  if (tree_int_cst_lt (constructor_max_index, constructor_index))
	    {
	      pedwarn_init ("excess elements in vector initializer");
	      break;
	    }

	  /* Now output the actual element.  */
	  if (value.value)
	    output_init_element (value.value, strict_string,
				 elttype, constructor_index, 1);

	  constructor_index
	    = size_binop (PLUS_EXPR, constructor_index, bitsize_one_node);

	  if (!value.value)
	    /* If we are doing the bookkeeping for an element that was
	       directly output as a constructor, we must update
	       constructor_unfilled_index.  */
	    constructor_unfilled_index = constructor_index;
	}

      /* Handle the sole element allowed in a braced initializer
	 for a scalar variable.  */
      else if (constructor_type != error_mark_node
	       && constructor_fields == 0)
	{
	  pedwarn_init ("excess elements in scalar initializer");
	  break;
	}
      else
	{
	  if (value.value)
	    output_init_element (value.value, strict_string,
				 constructor_type, NULL_TREE, 1);
	  constructor_fields = 0;
	}

      /* Handle range initializers either at this level or anywhere higher
	 in the designator stack.  */
      if (constructor_range_stack)
	{
	  struct constructor_range_stack *p, *range_stack;
	  int finish = 0;

	  range_stack = constructor_range_stack;
	  constructor_range_stack = 0;
	  while (constructor_stack != range_stack->stack)
	    {
	      gcc_assert (constructor_stack->implicit);
	      process_init_element (pop_init_level (1));
	    }
	  for (p = range_stack;
	       !p->range_end || tree_int_cst_equal (p->index, p->range_end);
	       p = p->prev)
	    {
	      gcc_assert (constructor_stack->implicit);
	      process_init_element (pop_init_level (1));
	    }

	  p->index = size_binop (PLUS_EXPR, p->index, bitsize_one_node);
	  if (tree_int_cst_equal (p->index, p->range_end) && !p->prev)
	    finish = 1;

	  while (1)
	    {
	      constructor_index = p->index;
	      constructor_fields = p->fields;
	      if (finish && p->range_end && p->index == p->range_start)
		{
		  finish = 0;
		  p->prev = 0;
		}
	      p = p->next;
	      if (!p)
		break;
	      push_init_level (2);
	      p->stack = constructor_stack;
	      if (p->range_end && tree_int_cst_equal (p->index, p->range_end))
		p->index = p->range_start;
	    }

	  if (!finish)
	    constructor_range_stack = range_stack;
	  continue;
	}

      break;
    }

  constructor_range_stack = 0;
}

/* Build a complete asm-statement, whose components are a CV_QUALIFIER
   (guaranteed to be 'volatile' or null) and ARGS (represented using
   an ASM_EXPR node).  */
tree
build_asm_stmt (tree cv_qualifier, tree args)
{
  if (!ASM_VOLATILE_P (args) && cv_qualifier)
    ASM_VOLATILE_P (args) = 1;
  return add_stmt (args);
}

/* Build an asm-expr, whose components are a STRING, some OUTPUTS,
   some INPUTS, and some CLOBBERS.  The latter three may be NULL.
   SIMPLE indicates whether there was anything at all after the
   string in the asm expression -- asm("blah") and asm("blah" : )
   are subtly different.  We use a ASM_EXPR node to represent this.  */
tree
build_asm_expr (tree string, tree outputs, tree inputs, tree clobbers,
		bool simple)
{
  tree tail;
  tree args;
  int i;
  const char *constraint;
  const char **oconstraints;
  bool allows_mem, allows_reg, is_inout;
  int ninputs, noutputs;

  ninputs = list_length (inputs);
  noutputs = list_length (outputs);
  oconstraints = (const char **) alloca (noutputs * sizeof (const char *));

  string = resolve_asm_operand_names (string, outputs, inputs);

  /* Remove output conversions that change the type but not the mode.  */
  for (i = 0, tail = outputs; tail; ++i, tail = TREE_CHAIN (tail))
    {
      tree output = TREE_VALUE (tail);

      /* ??? Really, this should not be here.  Users should be using a
	 proper lvalue, dammit.  But there's a long history of using casts
	 in the output operands.  In cases like longlong.h, this becomes a
	 primitive form of typechecking -- if the cast can be removed, then
	 the output operand had a type of the proper width; otherwise we'll
	 get an error.  Gross, but ...  */
      STRIP_NOPS (output);

      if (!lvalue_or_else (output, lv_asm))
	output = error_mark_node;

      if (output != error_mark_node
	  && (TREE_READONLY (output)
	      || TYPE_READONLY (TREE_TYPE (output))
	      || ((TREE_CODE (TREE_TYPE (output)) == RECORD_TYPE
		   || TREE_CODE (TREE_TYPE (output)) == UNION_TYPE)
		  && C_TYPE_FIELDS_READONLY (TREE_TYPE (output)))))
	readonly_error (output, lv_asm);

      constraint = TREE_STRING_POINTER (TREE_VALUE (TREE_PURPOSE (tail)));
      oconstraints[i] = constraint;

      if (parse_output_constraint (&constraint, i, ninputs, noutputs,
				   &allows_mem, &allows_reg, &is_inout))
	{
	  /* If the operand is going to end up in memory,
	     mark it addressable.  */
	  if (!allows_reg && !c_mark_addressable (output))
	    output = error_mark_node;
	}
      else
	output = error_mark_node;

      TREE_VALUE (tail) = output;
    }

  for (i = 0, tail = inputs; tail; ++i, tail = TREE_CHAIN (tail))
    {
      tree input;

      constraint = TREE_STRING_POINTER (TREE_VALUE (TREE_PURPOSE (tail)));
      input = TREE_VALUE (tail);

      if (parse_input_constraint (&constraint, i, ninputs, noutputs, 0,
				  oconstraints, &allows_mem, &allows_reg))
	{
	  /* If the operand is going to end up in memory,
	     mark it addressable.  */
	  if (!allows_reg && allows_mem)
	    {
	      /* Strip the nops as we allow this case.  FIXME, this really
		 should be rejected or made deprecated.  */
	      STRIP_NOPS (input);
	      if (!c_mark_addressable (input))
		input = error_mark_node;
	  }
	}
      else
	input = error_mark_node;

      TREE_VALUE (tail) = input;
    }

  args = build_stmt (ASM_EXPR, string, outputs, inputs, clobbers);

  /* asm statements without outputs, including simple ones, are treated
     as volatile.  */
  ASM_INPUT_P (args) = simple;
  ASM_VOLATILE_P (args) = (noutputs == 0);

  return args;
}

/* Generate a goto statement to LABEL.  */

tree
c_finish_goto_label (tree label)
{
  tree decl = lookup_label (label);
  if (!decl)
    return NULL_TREE;

  if (C_DECL_UNJUMPABLE_STMT_EXPR (decl))
    {
      error ("jump into statement expression");
      return NULL_TREE;
    }

  if (C_DECL_UNJUMPABLE_VM (decl))
    {
      error ("jump into scope of identifier with variably modified type");
      return NULL_TREE;
    }

  if (!C_DECL_UNDEFINABLE_STMT_EXPR (decl))
    {
      /* No jump from outside this statement expression context, so
	 record that there is a jump from within this context.  */
      struct c_label_list *nlist;
      nlist = XOBNEW (&parser_obstack, struct c_label_list);
      nlist->next = label_context_stack_se->labels_used;
      nlist->label = decl;
      label_context_stack_se->labels_used = nlist;
    }

  if (!C_DECL_UNDEFINABLE_VM (decl))
    {
      /* No jump from outside this context context of identifiers with
	 variably modified type, so record that there is a jump from
	 within this context.  */
      struct c_label_list *nlist;
      nlist = XOBNEW (&parser_obstack, struct c_label_list);
      nlist->next = label_context_stack_vm->labels_used;
      nlist->label = decl;
      label_context_stack_vm->labels_used = nlist;
    }

  TREE_USED (decl) = 1;
  return add_stmt (build1 (GOTO_EXPR, void_type_node, decl));
}

/* Generate a computed goto statement to EXPR.  */

tree
c_finish_goto_ptr (tree expr)
{
  if (pedantic)
    pedwarn ("ISO C forbids %<goto *expr;%>");
  expr = convert (ptr_type_node, expr);
  return add_stmt (build1 (GOTO_EXPR, void_type_node, expr));
}

/* Generate a C `return' statement.  RETVAL is the expression for what
   to return, or a null pointer for `return;' with no value.  */

tree
c_finish_return (tree retval)
{
  tree valtype = TREE_TYPE (TREE_TYPE (current_function_decl)), ret_stmt;
  bool no_warning = false;

  if (TREE_THIS_VOLATILE (current_function_decl))
    warning (0, "function declared %<noreturn%> has a %<return%> statement");

  if (!retval)
    {
      current_function_returns_null = 1;
      if ((warn_return_type || flag_isoc99)
	  && valtype != 0 && TREE_CODE (valtype) != VOID_TYPE)
	{
	  pedwarn_c99 ("%<return%> with no value, in "
		       "function returning non-void");
	  no_warning = true;
	}
    }
  else if (valtype == 0 || TREE_CODE (valtype) == VOID_TYPE)
    {
      current_function_returns_null = 1;
      if (pedantic || TREE_CODE (TREE_TYPE (retval)) != VOID_TYPE)
	pedwarn ("%<return%> with a value, in function returning void");
    }
  else
    {
      tree t = convert_for_assignment (valtype, retval, ic_return,
				       NULL_TREE, NULL_TREE, 0);
      tree res = DECL_RESULT (current_function_decl);
      tree inner;

      current_function_returns_value = 1;
      if (t == error_mark_node)
	return NULL_TREE;

      inner = t = convert (TREE_TYPE (res), t);

      /* Strip any conversions, additions, and subtractions, and see if
	 we are returning the address of a local variable.  Warn if so.  */
      while (1)
	{
	  switch (TREE_CODE (inner))
	    {
	    case NOP_EXPR:   case NON_LVALUE_EXPR:  case CONVERT_EXPR:
	    case PLUS_EXPR:
	      inner = TREE_OPERAND (inner, 0);
	      continue;

	    case MINUS_EXPR:
	      /* If the second operand of the MINUS_EXPR has a pointer
		 type (or is converted from it), this may be valid, so
		 don't give a warning.  */
	      {
		tree op1 = TREE_OPERAND (inner, 1);

		while (!POINTER_TYPE_P (TREE_TYPE (op1))
		       && (TREE_CODE (op1) == NOP_EXPR
			   || TREE_CODE (op1) == NON_LVALUE_EXPR
			   || TREE_CODE (op1) == CONVERT_EXPR))
		  op1 = TREE_OPERAND (op1, 0);

		if (POINTER_TYPE_P (TREE_TYPE (op1)))
		  break;

		inner = TREE_OPERAND (inner, 0);
		continue;
	      }

	    case ADDR_EXPR:
	      inner = TREE_OPERAND (inner, 0);

	      while (REFERENCE_CLASS_P (inner)
		     && TREE_CODE (inner) != INDIRECT_REF)
		inner = TREE_OPERAND (inner, 0);

	      if (DECL_P (inner)
		  && !DECL_EXTERNAL (inner)
		  && !TREE_STATIC (inner)
		  && DECL_CONTEXT (inner) == current_function_decl)
		warning (0, "function returns address of local variable");
	      break;

	    default:
	      break;
	    }

	  break;
	}

      retval = build2 (MODIFY_EXPR, TREE_TYPE (res), res, t);
    }

  ret_stmt = build_stmt (RETURN_EXPR, retval);
  TREE_NO_WARNING (ret_stmt) |= no_warning;
  return add_stmt (ret_stmt);
}

struct c_switch {
  /* The SWITCH_EXPR being built.  */
  tree switch_expr;

  /* The original type of the testing expression, i.e. before the
     default conversion is applied.  */
  tree orig_type;

  /* A splay-tree mapping the low element of a case range to the high
     element, or NULL_TREE if there is no high element.  Used to
     determine whether or not a new case label duplicates an old case
     label.  We need a tree, rather than simply a hash table, because
     of the GNU case range extension.  */
  splay_tree cases;

  /* Number of nested statement expressions within this switch
     statement; if nonzero, case and default labels may not
     appear.  */
  unsigned int blocked_stmt_expr;

  /* Scope of outermost declarations of identifiers with variably
     modified type within this switch statement; if nonzero, case and
     default labels may not appear.  */
  unsigned int blocked_vm;

  /* The next node on the stack.  */
  struct c_switch *next;
};

/* A stack of the currently active switch statements.  The innermost
   switch statement is on the top of the stack.  There is no need to
   mark the stack for garbage collection because it is only active
   during the processing of the body of a function, and we never
   collect at that point.  */

struct c_switch *c_switch_stack;

/* Start a C switch statement, testing expression EXP.  Return the new
   SWITCH_EXPR.  */

tree
c_start_case (tree exp)
{
  tree orig_type = error_mark_node;
  struct c_switch *cs;

  if (exp != error_mark_node)
    {
      orig_type = TREE_TYPE (exp);

      if (!INTEGRAL_TYPE_P (orig_type))
	{
	  if (orig_type != error_mark_node)
	    {
	      error ("switch quantity not an integer");
	      orig_type = error_mark_node;
	    }
	  exp = integer_zero_node;
	}
      else
	{
	  tree type = TYPE_MAIN_VARIANT (orig_type);

	  if (!in_system_header
	      && (type == long_integer_type_node
		  || type == long_unsigned_type_node))
	    warning (OPT_Wtraditional, "%<long%> switch expression not "
		     "converted to %<int%> in ISO C");

	  exp = default_conversion (exp);
	}
    }

  /* Add this new SWITCH_EXPR to the stack.  */
  cs = XNEW (struct c_switch);
  cs->switch_expr = build3 (SWITCH_EXPR, orig_type, exp, NULL_TREE, NULL_TREE);
  cs->orig_type = orig_type;
  cs->cases = splay_tree_new (case_compare, NULL, NULL);
  cs->blocked_stmt_expr = 0;
  cs->blocked_vm = 0;
  cs->next = c_switch_stack;
  c_switch_stack = cs;

  return add_stmt (cs->switch_expr);
}

/* Process a case label.  */

tree
do_case (tree low_value, tree high_value)
{
  tree label = NULL_TREE;

  if (c_switch_stack && !c_switch_stack->blocked_stmt_expr
      && !c_switch_stack->blocked_vm)
    {
      label = c_add_case_label (c_switch_stack->cases,
				SWITCH_COND (c_switch_stack->switch_expr),
				c_switch_stack->orig_type,
				low_value, high_value);
      if (label == error_mark_node)
	label = NULL_TREE;
    }
  else if (c_switch_stack && c_switch_stack->blocked_stmt_expr)
    {
      if (low_value)
	error ("case label in statement expression not containing "
	       "enclosing switch statement");
      else
	error ("%<default%> label in statement expression not containing "
	       "enclosing switch statement");
    }
  else if (c_switch_stack && c_switch_stack->blocked_vm)
    {
      if (low_value)
	error ("case label in scope of identifier with variably modified "
	       "type not containing enclosing switch statement");
      else
	error ("%<default%> label in scope of identifier with variably "
	       "modified type not containing enclosing switch statement");
    }
  else if (low_value)
    error ("case label not within a switch statement");
  else
    error ("%<default%> label not within a switch statement");

  return label;
}

/* Finish the switch statement.  */

void
c_finish_case (tree body)
{
  struct c_switch *cs = c_switch_stack;
  location_t switch_location;

  SWITCH_BODY (cs->switch_expr) = body;

  /* We must not be within a statement expression nested in the switch
     at this point; we might, however, be within the scope of an
     identifier with variably modified type nested in the switch.  */
  gcc_assert (!cs->blocked_stmt_expr);

  /* Emit warnings as needed.  */
  if (EXPR_HAS_LOCATION (cs->switch_expr))
    switch_location = EXPR_LOCATION (cs->switch_expr);
  else
    switch_location = input_location;
  c_do_switch_warnings (cs->cases, switch_location,
			TREE_TYPE (cs->switch_expr),
			SWITCH_COND (cs->switch_expr));

  /* Pop the stack.  */
  c_switch_stack = cs->next;
  splay_tree_delete (cs->cases);
  XDELETE (cs);
}

/* Emit an if statement.  IF_LOCUS is the location of the 'if'.  COND,
   THEN_BLOCK and ELSE_BLOCK are expressions to be used; ELSE_BLOCK
   may be null.  NESTED_IF is true if THEN_BLOCK contains another IF
   statement, and was not surrounded with parenthesis.  */

void
c_finish_if_stmt (location_t if_locus, tree cond, tree then_block,
		  tree else_block, bool nested_if)
{
  tree stmt;

  /* Diagnose an ambiguous else if if-then-else is nested inside if-then.  */
  if (warn_parentheses && nested_if && else_block == NULL)
    {
      tree inner_if = then_block;

      /* We know from the grammar productions that there is an IF nested
	 within THEN_BLOCK.  Due to labels and c99 conditional declarations,
	 it might not be exactly THEN_BLOCK, but should be the last
	 non-container statement within.  */
      while (1)
	switch (TREE_CODE (inner_if))
	  {
	  case COND_EXPR:
	    goto found;
	  case BIND_EXPR:
	    inner_if = BIND_EXPR_BODY (inner_if);
	    break;
	  case STATEMENT_LIST:
	    inner_if = expr_last (then_block);
	    break;
	  case TRY_FINALLY_EXPR:
	  case TRY_CATCH_EXPR:
	    inner_if = TREE_OPERAND (inner_if, 0);
	    break;
	  default:
	    gcc_unreachable ();
	  }
    found:

      if (COND_EXPR_ELSE (inner_if))
	 warning (OPT_Wparentheses,
		  "%Hsuggest explicit braces to avoid ambiguous %<else%>",
		  &if_locus);
    }

  empty_body_warning (then_block, else_block);

  stmt = build3 (COND_EXPR, void_type_node, cond, then_block, else_block);
  SET_EXPR_LOCATION (stmt, if_locus);
  add_stmt (stmt);
}

/* Emit a general-purpose loop construct.  START_LOCUS is the location of
   the beginning of the loop.  COND is the loop condition.  COND_IS_FIRST
   is false for DO loops.  INCR is the FOR increment expression.  BODY is
   the statement controlled by the loop.  BLAB is the break label.  CLAB is
   the continue label.  Everything is allowed to be NULL.  */

void
c_finish_loop (location_t start_locus, tree cond, tree incr, tree body,
	       tree blab, tree clab, bool cond_is_first)
{
  tree entry = NULL, exit = NULL, t;

  /* If the condition is zero don't generate a loop construct.  */
  if (cond && integer_zerop (cond))
    {
      if (cond_is_first)
	{
	  t = build_and_jump (&blab);
	  SET_EXPR_LOCATION (t, start_locus);
	  add_stmt (t);
	}
    }
  else
    {
      tree top = build1 (LABEL_EXPR, void_type_node, NULL_TREE);

      /* If we have an exit condition, then we build an IF with gotos either
	 out of the loop, or to the top of it.  If there's no exit condition,
	 then we just build a jump back to the top.  */
      exit = build_and_jump (&LABEL_EXPR_LABEL (top));

      if (cond && !integer_nonzerop (cond))
	{
	  /* Canonicalize the loop condition to the end.  This means
	     generating a branch to the loop condition.  Reuse the
	     continue label, if possible.  */
	  if (cond_is_first)
	    {
	      if (incr || !clab)
		{
		  entry = build1 (LABEL_EXPR, void_type_node, NULL_TREE);
		  t = build_and_jump (&LABEL_EXPR_LABEL (entry));
		}
	      else
		t = build1 (GOTO_EXPR, void_type_node, clab);
	      SET_EXPR_LOCATION (t, start_locus);
	      add_stmt (t);
	    }

	  t = build_and_jump (&blab);
	  exit = fold_build3 (COND_EXPR, void_type_node, cond, exit, t);
	  if (cond_is_first)
	    SET_EXPR_LOCATION (exit, start_locus);
	  else
	    SET_EXPR_LOCATION (exit, input_location);
	}

      add_stmt (top);
    }

  if (body)
    add_stmt (body);
  if (clab)
    add_stmt (build1 (LABEL_EXPR, void_type_node, clab));
  if (incr)
    add_stmt (incr);
  if (entry)
    add_stmt (entry);
  if (exit)
    add_stmt (exit);
  if (blab)
    add_stmt (build1 (LABEL_EXPR, void_type_node, blab));
}

tree
c_finish_bc_stmt (tree *label_p, bool is_break)
{
  bool skip;
  tree label = *label_p;

  /* In switch statements break is sometimes stylistically used after
     a return statement.  This can lead to spurious warnings about
     control reaching the end of a non-void function when it is
     inlined.  Note that we are calling block_may_fallthru with
     language specific tree nodes; this works because
     block_may_fallthru returns true when given something it does not
     understand.  */
  skip = !block_may_fallthru (cur_stmt_list);

  if (!label)
    {
      if (!skip)
	*label_p = label = create_artificial_label ();
    }
  else if (TREE_CODE (label) == LABEL_DECL)
    ;
  else switch (TREE_INT_CST_LOW (label))
    {
    case 0:
      if (is_break)
	error ("break statement not within loop or switch");
      else
	error ("continue statement not within a loop");
      return NULL_TREE;

    case 1:
      gcc_assert (is_break);
      error ("break statement used with OpenMP for loop");
      return NULL_TREE;

    default:
      gcc_unreachable ();
    }

  if (skip)
    return NULL_TREE;

  return add_stmt (build1 (GOTO_EXPR, void_type_node, label));
}

/* A helper routine for c_process_expr_stmt and c_finish_stmt_expr.  */

static void
emit_side_effect_warnings (tree expr)
{
  if (expr == error_mark_node)
    ;
  else if (!TREE_SIDE_EFFECTS (expr))
    {
      if (!VOID_TYPE_P (TREE_TYPE (expr)) && !TREE_NO_WARNING (expr))
	warning (0, "%Hstatement with no effect",
		 EXPR_HAS_LOCATION (expr) ? EXPR_LOCUS (expr) : &input_location);
    }
  else if (warn_unused_value)
    warn_if_unused_value (expr, input_location);
}

/* Process an expression as if it were a complete statement.  Emit
   diagnostics, but do not call ADD_STMT.  */

tree
c_process_expr_stmt (tree expr)
{
  if (!expr)
    return NULL_TREE;

  if (warn_sequence_point)
    verify_sequence_points (expr);

  if (TREE_TYPE (expr) != error_mark_node
      && !COMPLETE_OR_VOID_TYPE_P (TREE_TYPE (expr))
      && TREE_CODE (TREE_TYPE (expr)) != ARRAY_TYPE)
    error ("expression statement has incomplete type");

  /* If we're not processing a statement expression, warn about unused values.
     Warnings for statement expressions will be emitted later, once we figure
     out which is the result.  */
  if (!STATEMENT_LIST_STMT_EXPR (cur_stmt_list)
      && (extra_warnings || warn_unused_value))
    emit_side_effect_warnings (expr);

  /* If the expression is not of a type to which we cannot assign a line
     number, wrap the thing in a no-op NOP_EXPR.  */
  if (DECL_P (expr) || CONSTANT_CLASS_P (expr))
    expr = build1 (NOP_EXPR, TREE_TYPE (expr), expr);

  if (EXPR_P (expr))
    SET_EXPR_LOCATION (expr, input_location);

  return expr;
}

/* Emit an expression as a statement.  */

tree
c_finish_expr_stmt (tree expr)
{
  if (expr)
    return add_stmt (c_process_expr_stmt (expr));
  else
    return NULL;
}

/* Do the opposite and emit a statement as an expression.  To begin,
   create a new binding level and return it.  */

tree
c_begin_stmt_expr (void)
{
  tree ret;
  struct c_label_context_se *nstack;
  struct c_label_list *glist;

  /* We must force a BLOCK for this level so that, if it is not expanded
     later, there is a way to turn off the entire subtree of blocks that
     are contained in it.  */
  keep_next_level ();
  ret = c_begin_compound_stmt (true);
  if (c_switch_stack)
    {
      c_switch_stack->blocked_stmt_expr++;
      gcc_assert (c_switch_stack->blocked_stmt_expr != 0);
    }
  for (glist = label_context_stack_se->labels_used;
       glist != NULL;
       glist = glist->next)
    {
      C_DECL_UNDEFINABLE_STMT_EXPR (glist->label) = 1;
    }
  nstack = XOBNEW (&parser_obstack, struct c_label_context_se);
  nstack->labels_def = NULL;
  nstack->labels_used = NULL;
  nstack->next = label_context_stack_se;
  label_context_stack_se = nstack;

  /* Mark the current statement list as belonging to a statement list.  */
  STATEMENT_LIST_STMT_EXPR (ret) = 1;

  return ret;
}

tree
c_finish_stmt_expr (tree body)
{
  tree last, type, tmp, val;
  tree *last_p;
  struct c_label_list *dlist, *glist, *glist_prev = NULL;

  body = c_end_compound_stmt (body, true);
  if (c_switch_stack)
    {
      gcc_assert (c_switch_stack->blocked_stmt_expr != 0);
      c_switch_stack->blocked_stmt_expr--;
    }
  /* It is no longer possible to jump to labels defined within this
     statement expression.  */
  for (dlist = label_context_stack_se->labels_def;
       dlist != NULL;
       dlist = dlist->next)
    {
      C_DECL_UNJUMPABLE_STMT_EXPR (dlist->label) = 1;
    }
  /* It is again possible to define labels with a goto just outside
     this statement expression.  */
  for (glist = label_context_stack_se->next->labels_used;
       glist != NULL;
       glist = glist->next)
    {
      C_DECL_UNDEFINABLE_STMT_EXPR (glist->label) = 0;
      glist_prev = glist;
    }
  if (glist_prev != NULL)
    glist_prev->next = label_context_stack_se->labels_used;
  else
    label_context_stack_se->next->labels_used
      = label_context_stack_se->labels_used;
  label_context_stack_se = label_context_stack_se->next;

  /* Locate the last statement in BODY.  See c_end_compound_stmt
     about always returning a BIND_EXPR.  */
  last_p = &BIND_EXPR_BODY (body);
  last = BIND_EXPR_BODY (body);

 continue_searching:
  if (TREE_CODE (last) == STATEMENT_LIST)
    {
      tree_stmt_iterator i;

      /* This can happen with degenerate cases like ({ }).  No value.  */
      if (!TREE_SIDE_EFFECTS (last))
	return body;

      /* If we're supposed to generate side effects warnings, process
	 all of the statements except the last.  */
      if (extra_warnings || warn_unused_value)
	{
	  for (i = tsi_start (last); !tsi_one_before_end_p (i); tsi_next (&i))
	    emit_side_effect_warnings (tsi_stmt (i));
	}
      else
	i = tsi_last (last);
      last_p = tsi_stmt_ptr (i);
      last = *last_p;
    }

  /* If the end of the list is exception related, then the list was split
     by a call to push_cleanup.  Continue searching.  */
  if (TREE_CODE (last) == TRY_FINALLY_EXPR
      || TREE_CODE (last) == TRY_CATCH_EXPR)
    {
      last_p = &TREE_OPERAND (last, 0);
      last = *last_p;
      goto continue_searching;
    }

  /* In the case that the BIND_EXPR is not necessary, return the
     expression out from inside it.  */
  if (last == error_mark_node
      || (last == BIND_EXPR_BODY (body)
	  && BIND_EXPR_VARS (body) == NULL))
    {
      /* Do not warn if the return value of a statement expression is
	 unused.  */
      if (EXPR_P (last))
	TREE_NO_WARNING (last) = 1;
      return last;
    }

  /* Extract the type of said expression.  */
  type = TREE_TYPE (last);

  /* If we're not returning a value at all, then the BIND_EXPR that
     we already have is a fine expression to return.  */
  if (!type || VOID_TYPE_P (type))
    return body;

  /* Now that we've located the expression containing the value, it seems
     silly to make voidify_wrapper_expr repeat the process.  Create a
     temporary of the appropriate type and stick it in a TARGET_EXPR.  */
  tmp = create_tmp_var_raw (type, NULL);

  /* Unwrap a no-op NOP_EXPR as added by c_finish_expr_stmt.  This avoids
     tree_expr_nonnegative_p giving up immediately.  */
  val = last;
  if (TREE_CODE (val) == NOP_EXPR
      && TREE_TYPE (val) == TREE_TYPE (TREE_OPERAND (val, 0)))
    val = TREE_OPERAND (val, 0);

  *last_p = build2 (MODIFY_EXPR, void_type_node, tmp, val);
  SET_EXPR_LOCUS (*last_p, EXPR_LOCUS (last));

  return build4 (TARGET_EXPR, type, tmp, body, NULL_TREE, NULL_TREE);
}

/* Begin the scope of an identifier of variably modified type, scope
   number SCOPE.  Jumping from outside this scope to inside it is not
   permitted.  */

void
c_begin_vm_scope (unsigned int scope)
{
  struct c_label_context_vm *nstack;
  struct c_label_list *glist;

  gcc_assert (scope > 0);

  /* At file_scope, we don't have to do any processing.  */
  if (label_context_stack_vm == NULL)
    return;

  if (c_switch_stack && !c_switch_stack->blocked_vm)
    c_switch_stack->blocked_vm = scope;
  for (glist = label_context_stack_vm->labels_used;
       glist != NULL;
       glist = glist->next)
    {
      C_DECL_UNDEFINABLE_VM (glist->label) = 1;
    }
  nstack = XOBNEW (&parser_obstack, struct c_label_context_vm);
  nstack->labels_def = NULL;
  nstack->labels_used = NULL;
  nstack->scope = scope;
  nstack->next = label_context_stack_vm;
  label_context_stack_vm = nstack;
}

/* End a scope which may contain identifiers of variably modified
   type, scope number SCOPE.  */

void
c_end_vm_scope (unsigned int scope)
{
  if (label_context_stack_vm == NULL)
    return;
  if (c_switch_stack && c_switch_stack->blocked_vm == scope)
    c_switch_stack->blocked_vm = 0;
  /* We may have a number of nested scopes of identifiers with
     variably modified type, all at this depth.  Pop each in turn.  */
  while (label_context_stack_vm->scope == scope)
    {
      struct c_label_list *dlist, *glist, *glist_prev = NULL;

      /* It is no longer possible to jump to labels defined within this
	 scope.  */
      for (dlist = label_context_stack_vm->labels_def;
	   dlist != NULL;
	   dlist = dlist->next)
	{
	  C_DECL_UNJUMPABLE_VM (dlist->label) = 1;
	}
      /* It is again possible to define labels with a goto just outside
	 this scope.  */
      for (glist = label_context_stack_vm->next->labels_used;
	   glist != NULL;
	   glist = glist->next)
	{
	  C_DECL_UNDEFINABLE_VM (glist->label) = 0;
	  glist_prev = glist;
	}
      if (glist_prev != NULL)
	glist_prev->next = label_context_stack_vm->labels_used;
      else
	label_context_stack_vm->next->labels_used
	  = label_context_stack_vm->labels_used;
      label_context_stack_vm = label_context_stack_vm->next;
    }
}

/* Begin and end compound statements.  This is as simple as pushing
   and popping new statement lists from the tree.  */

tree
c_begin_compound_stmt (bool do_scope)
{
  tree stmt = push_stmt_list ();
  if (do_scope)
    push_scope ();
  return stmt;
}

tree
c_end_compound_stmt (tree stmt, bool do_scope)
{
  tree block = NULL;

  if (do_scope)
    {
      if (c_dialect_objc ())
	objc_clear_super_receiver ();
      block = pop_scope ();
    }

  stmt = pop_stmt_list (stmt);
  stmt = c_build_bind_expr (block, stmt);

  /* If this compound statement is nested immediately inside a statement
     expression, then force a BIND_EXPR to be created.  Otherwise we'll
     do the wrong thing for ({ { 1; } }) or ({ 1; { } }).  In particular,
     STATEMENT_LISTs merge, and thus we can lose track of what statement
     was really last.  */
  if (cur_stmt_list
      && STATEMENT_LIST_STMT_EXPR (cur_stmt_list)
      && TREE_CODE (stmt) != BIND_EXPR)
    {
      stmt = build3 (BIND_EXPR, void_type_node, NULL, stmt, NULL);
      TREE_SIDE_EFFECTS (stmt) = 1;
    }

  return stmt;
}

/* Queue a cleanup.  CLEANUP is an expression/statement to be executed
   when the current scope is exited.  EH_ONLY is true when this is not
   meant to apply to normal control flow transfer.  */

void
push_cleanup (tree ARG_UNUSED (decl), tree cleanup, bool eh_only)
{
  enum tree_code code;
  tree stmt, list;
  bool stmt_expr;

  code = eh_only ? TRY_CATCH_EXPR : TRY_FINALLY_EXPR;
  stmt = build_stmt (code, NULL, cleanup);
  add_stmt (stmt);
  stmt_expr = STATEMENT_LIST_STMT_EXPR (cur_stmt_list);
  list = push_stmt_list ();
  TREE_OPERAND (stmt, 0) = list;
  STATEMENT_LIST_STMT_EXPR (list) = stmt_expr;
}

/* Build a binary-operation expression without default conversions.
   CODE is the kind of expression to build.
   This function differs from `build' in several ways:
   the data type of the result is computed and recorded in it,
   warnings are generated if arg data types are invalid,
   special handling for addition and subtraction of pointers is known,
   and some optimization is done (operations on narrow ints
   are done in the narrower type when that gives the same result).
   Constant folding is also done before the result is returned.

   Note that the operands will never have enumeral types, or function
   or array types, because either they will have the default conversions
   performed or they have both just been converted to some other type in which
   the arithmetic is to be done.  */

tree
build_binary_op (enum tree_code code, tree orig_op0, tree orig_op1,
		 int convert_p)
{
  tree type0, type1;
  enum tree_code code0, code1;
  tree op0, op1;
  const char *invalid_op_diag;

  /* Expression code to give to the expression when it is built.
     Normally this is CODE, which is what the caller asked for,
     but in some special cases we change it.  */
  enum tree_code resultcode = code;

  /* Data type in which the computation is to be performed.
     In the simplest cases this is the common type of the arguments.  */
  tree result_type = NULL;

  /* Nonzero means operands have already been type-converted
     in whatever way is necessary.
     Zero means they need to be converted to RESULT_TYPE.  */
  int converted = 0;

  /* Nonzero means create the expression with this type, rather than
     RESULT_TYPE.  */
  tree build_type = 0;

  /* Nonzero means after finally constructing the expression
     convert it to this type.  */
  tree final_type = 0;

  /* Nonzero if this is an operation like MIN or MAX which can
     safely be computed in short if both args are promoted shorts.
     Also implies COMMON.
     -1 indicates a bitwise operation; this makes a difference
     in the exact conditions for when it is safe to do the operation
     in a narrower mode.  */
  int shorten = 0;

  /* Nonzero if this is a comparison operation;
     if both args are promoted shorts, compare the original shorts.
     Also implies COMMON.  */
  int short_compare = 0;

  /* Nonzero if this is a right-shift operation, which can be computed on the
     original short and then promoted if the operand is a promoted short.  */
  int short_shift = 0;

  /* Nonzero means set RESULT_TYPE to the common type of the args.  */
  int common = 0;

  /* True means types are compatible as far as ObjC is concerned.  */
  bool objc_ok;

  if (convert_p)
    {
      op0 = default_conversion (orig_op0);
      op1 = default_conversion (orig_op1);
    }
  else
    {
      op0 = orig_op0;
      op1 = orig_op1;
    }

  type0 = TREE_TYPE (op0);
  type1 = TREE_TYPE (op1);

  /* The expression codes of the data types of the arguments tell us
     whether the arguments are integers, floating, pointers, etc.  */
  code0 = TREE_CODE (type0);
  code1 = TREE_CODE (type1);

  /* Strip NON_LVALUE_EXPRs, etc., since we aren't using as an lvalue.  */
  STRIP_TYPE_NOPS (op0);
  STRIP_TYPE_NOPS (op1);

  /* If an error was already reported for one of the arguments,
     avoid reporting another error.  */

  if (code0 == ERROR_MARK || code1 == ERROR_MARK)
    return error_mark_node;

  if ((invalid_op_diag
       = targetm.invalid_binary_op (code, type0, type1)))
    {
      error (invalid_op_diag);
      return error_mark_node;
    }

  objc_ok = objc_compare_types (type0, type1, -3, NULL_TREE);

  switch (code)
    {
    case PLUS_EXPR:
      /* Handle the pointer + int case.  */
      if (code0 == POINTER_TYPE && code1 == INTEGER_TYPE)
	return pointer_int_sum (PLUS_EXPR, op0, op1);
      else if (code1 == POINTER_TYPE && code0 == INTEGER_TYPE)
	return pointer_int_sum (PLUS_EXPR, op1, op0);
      else
	common = 1;
      break;

    case MINUS_EXPR:
      /* Subtraction of two similar pointers.
	 We must subtract them as integers, then divide by object size.  */
      if (code0 == POINTER_TYPE && code1 == POINTER_TYPE
	  && comp_target_types (type0, type1))
	return pointer_diff (op0, op1);
      /* Handle pointer minus int.  Just like pointer plus int.  */
      else if (code0 == POINTER_TYPE && code1 == INTEGER_TYPE)
	return pointer_int_sum (MINUS_EXPR, op0, op1);
      else
	common = 1;
      break;

    case MULT_EXPR:
      common = 1;
      break;

    case TRUNC_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case ROUND_DIV_EXPR:
    case EXACT_DIV_EXPR:
      /* Floating point division by zero is a legitimate way to obtain
	 infinities and NaNs.  */
      if (skip_evaluation == 0 && integer_zerop (op1))
	warning (OPT_Wdiv_by_zero, "division by zero");

      if ((code0 == INTEGER_TYPE || code0 == REAL_TYPE
	   || code0 == COMPLEX_TYPE || code0 == VECTOR_TYPE)
	  && (code1 == INTEGER_TYPE || code1 == REAL_TYPE
	      || code1 == COMPLEX_TYPE || code1 == VECTOR_TYPE))
	{
	  enum tree_code tcode0 = code0, tcode1 = code1;

	  if (code0 == COMPLEX_TYPE || code0 == VECTOR_TYPE)
	    tcode0 = TREE_CODE (TREE_TYPE (TREE_TYPE (op0)));
	  if (code1 == COMPLEX_TYPE || code1 == VECTOR_TYPE)
	    tcode1 = TREE_CODE (TREE_TYPE (TREE_TYPE (op1)));

	  if (!(tcode0 == INTEGER_TYPE && tcode1 == INTEGER_TYPE))
	    resultcode = RDIV_EXPR;
	  else
	    /* Although it would be tempting to shorten always here, that
	       loses on some targets, since the modulo instruction is
	       undefined if the quotient can't be represented in the
	       computation mode.  We shorten only if unsigned or if
	       dividing by something we know != -1.  */
	    shorten = (TYPE_UNSIGNED (TREE_TYPE (orig_op0))
		       || (TREE_CODE (op1) == INTEGER_CST
			   && !integer_all_onesp (op1)));
	  common = 1;
	}
      break;

    case BIT_AND_EXPR:
    case BIT_IOR_EXPR:
    case BIT_XOR_EXPR:
      if (code0 == INTEGER_TYPE && code1 == INTEGER_TYPE)
	shorten = -1;
      else if (code0 == VECTOR_TYPE && code1 == VECTOR_TYPE)
	common = 1;
      break;

    case TRUNC_MOD_EXPR:
    case FLOOR_MOD_EXPR:
      if (skip_evaluation == 0 && integer_zerop (op1))
	warning (OPT_Wdiv_by_zero, "division by zero");

      if (code0 == INTEGER_TYPE && code1 == INTEGER_TYPE)
	{
	  /* Although it would be tempting to shorten always here, that loses
	     on some targets, since the modulo instruction is undefined if the
	     quotient can't be represented in the computation mode.  We shorten
	     only if unsigned or if dividing by something we know != -1.  */
	  shorten = (TYPE_UNSIGNED (TREE_TYPE (orig_op0))
		     || (TREE_CODE (op1) == INTEGER_CST
			 && !integer_all_onesp (op1)));
	  common = 1;
	}
      break;

    case TRUTH_ANDIF_EXPR:
    case TRUTH_ORIF_EXPR:
    case TRUTH_AND_EXPR:
    case TRUTH_OR_EXPR:
    case TRUTH_XOR_EXPR:
      if ((code0 == INTEGER_TYPE || code0 == POINTER_TYPE
	   || code0 == REAL_TYPE || code0 == COMPLEX_TYPE)
	  && (code1 == INTEGER_TYPE || code1 == POINTER_TYPE
	      || code1 == REAL_TYPE || code1 == COMPLEX_TYPE))
	{
	  /* Result of these operations is always an int,
	     but that does not mean the operands should be
	     converted to ints!  */
	  result_type = integer_type_node;
	  op0 = c_common_truthvalue_conversion (op0);
	  op1 = c_common_truthvalue_conversion (op1);
	  converted = 1;
	}
      break;

      /* Shift operations: result has same type as first operand;
	 always convert second operand to int.
	 Also set SHORT_SHIFT if shifting rightward.  */

    case RSHIFT_EXPR:
      if (code0 == INTEGER_TYPE && code1 == INTEGER_TYPE)
	{
	  if (TREE_CODE (op1) == INTEGER_CST && skip_evaluation == 0)
	    {
	      if (tree_int_cst_sgn (op1) < 0)
		warning (0, "right shift count is negative");
	      else
		{
		  if (!integer_zerop (op1))
		    short_shift = 1;

		  if (compare_tree_int (op1, TYPE_PRECISION (type0)) >= 0)
		    warning (0, "right shift count >= width of type");
		}
	    }

	  /* Use the type of the value to be shifted.  */
	  result_type = type0;
	  /* Convert the shift-count to an integer, regardless of size
	     of value being shifted.  */
	  if (TYPE_MAIN_VARIANT (TREE_TYPE (op1)) != integer_type_node)
	    op1 = convert (integer_type_node, op1);
	  /* Avoid converting op1 to result_type later.  */
	  converted = 1;
	}
      break;

    case LSHIFT_EXPR:
      if (code0 == INTEGER_TYPE && code1 == INTEGER_TYPE)
	{
	  if (TREE_CODE (op1) == INTEGER_CST && skip_evaluation == 0)
	    {
	      if (tree_int_cst_sgn (op1) < 0)
		warning (0, "left shift count is negative");

	      else if (compare_tree_int (op1, TYPE_PRECISION (type0)) >= 0)
		warning (0, "left shift count >= width of type");
	    }

	  /* Use the type of the value to be shifted.  */
	  result_type = type0;
	  /* Convert the shift-count to an integer, regardless of size
	     of value being shifted.  */
	  if (TYPE_MAIN_VARIANT (TREE_TYPE (op1)) != integer_type_node)
	    op1 = convert (integer_type_node, op1);
	  /* Avoid converting op1 to result_type later.  */
	  converted = 1;
	}
      break;

    case EQ_EXPR:
    case NE_EXPR:
      if (code0 == REAL_TYPE || code1 == REAL_TYPE)
	warning (OPT_Wfloat_equal,
		 "comparing floating point with == or != is unsafe");
      /* Result of comparison is always int,
	 but don't convert the args to int!  */
      build_type = integer_type_node;
      if ((code0 == INTEGER_TYPE || code0 == REAL_TYPE
	   || code0 == COMPLEX_TYPE)
	  && (code1 == INTEGER_TYPE || code1 == REAL_TYPE
	      || code1 == COMPLEX_TYPE))
	short_compare = 1;
      else if (code0 == POINTER_TYPE && code1 == POINTER_TYPE)
	{
	  tree tt0 = TREE_TYPE (type0);
	  tree tt1 = TREE_TYPE (type1);
	  /* Anything compares with void *.  void * compares with anything.
	     Otherwise, the targets must be compatible
	     and both must be object or both incomplete.  */
	  if (comp_target_types (type0, type1))
	    result_type = common_pointer_type (type0, type1);
	  else if (VOID_TYPE_P (tt0))
	    {
	      /* op0 != orig_op0 detects the case of something
		 whose value is 0 but which isn't a valid null ptr const.  */
	      if (pedantic && !null_pointer_constant_p (orig_op0)
		  && TREE_CODE (tt1) == FUNCTION_TYPE)
		pedwarn ("ISO C forbids comparison of %<void *%>"
			 " with function pointer");
	    }
	  else if (VOID_TYPE_P (tt1))
	    {
	      if (pedantic && !null_pointer_constant_p (orig_op1)
		  && TREE_CODE (tt0) == FUNCTION_TYPE)
		pedwarn ("ISO C forbids comparison of %<void *%>"
			 " with function pointer");
	    }
	  else
	    /* Avoid warning about the volatile ObjC EH puts on decls.  */
	    if (!objc_ok)
	      pedwarn ("comparison of distinct pointer types lacks a cast");

	  if (result_type == NULL_TREE)
	    result_type = ptr_type_node;
	}
      else if (code0 == POINTER_TYPE && null_pointer_constant_p (orig_op1))
	{
	  if (TREE_CODE (op0) == ADDR_EXPR
	      && DECL_P (TREE_OPERAND (op0, 0))
	      && (TREE_CODE (TREE_OPERAND (op0, 0)) == PARM_DECL
		  || TREE_CODE (TREE_OPERAND (op0, 0)) == LABEL_DECL
		  || !DECL_WEAK (TREE_OPERAND (op0, 0))))
	    warning (OPT_Waddress, "the address of %qD will never be NULL",
		     TREE_OPERAND (op0, 0));
	  result_type = type0;
	}
      else if (code1 == POINTER_TYPE && null_pointer_constant_p (orig_op0))
	{
	  if (TREE_CODE (op1) == ADDR_EXPR
	      && DECL_P (TREE_OPERAND (op1, 0))
	      && (TREE_CODE (TREE_OPERAND (op1, 0)) == PARM_DECL
		  || TREE_CODE (TREE_OPERAND (op1, 0)) == LABEL_DECL
		  || !DECL_WEAK (TREE_OPERAND (op1, 0))))
	    warning (OPT_Waddress, "the address of %qD will never be NULL",
		     TREE_OPERAND (op1, 0));
	  result_type = type1;
	}
      else if (code0 == POINTER_TYPE && code1 == INTEGER_TYPE)
	{
	  result_type = type0;
	  pedwarn ("comparison between pointer and integer");
	}
      else if (code0 == INTEGER_TYPE && code1 == POINTER_TYPE)
	{
	  result_type = type1;
	  pedwarn ("comparison between pointer and integer");
	}
      break;

    case LE_EXPR:
    case GE_EXPR:
    case LT_EXPR:
    case GT_EXPR:
      build_type = integer_type_node;
      if ((code0 == INTEGER_TYPE || code0 == REAL_TYPE)
	  && (code1 == INTEGER_TYPE || code1 == REAL_TYPE))
	short_compare = 1;
      else if (code0 == POINTER_TYPE && code1 == POINTER_TYPE)
	{
	  if (comp_target_types (type0, type1))
	    {
	      result_type = common_pointer_type (type0, type1);
	      if (!COMPLETE_TYPE_P (TREE_TYPE (type0))
		  != !COMPLETE_TYPE_P (TREE_TYPE (type1)))
		pedwarn ("comparison of complete and incomplete pointers");
	      else if (pedantic
		       && TREE_CODE (TREE_TYPE (type0)) == FUNCTION_TYPE)
		pedwarn ("ISO C forbids ordered comparisons of pointers to functions");
	    }
	  else
	    {
	      result_type = ptr_type_node;
	      pedwarn ("comparison of distinct pointer types lacks a cast");
	    }
	}
      else if (code0 == POINTER_TYPE && null_pointer_constant_p (orig_op1))
	{
	  result_type = type0;
	  if (pedantic || extra_warnings)
	    pedwarn ("ordered comparison of pointer with integer zero");
	}
      else if (code1 == POINTER_TYPE && null_pointer_constant_p (orig_op0))
	{
	  result_type = type1;
	  if (pedantic)
	    pedwarn ("ordered comparison of pointer with integer zero");
	}
      else if (code0 == POINTER_TYPE && code1 == INTEGER_TYPE)
	{
	  result_type = type0;
	  pedwarn ("comparison between pointer and integer");
	}
      else if (code0 == INTEGER_TYPE && code1 == POINTER_TYPE)
	{
	  result_type = type1;
	  pedwarn ("comparison between pointer and integer");
	}
      break;

    default:
      gcc_unreachable ();
    }

  if (code0 == ERROR_MARK || code1 == ERROR_MARK)
    return error_mark_node;

  if (code0 == VECTOR_TYPE && code1 == VECTOR_TYPE
      && (!tree_int_cst_equal (TYPE_SIZE (type0), TYPE_SIZE (type1))
	  || !same_scalar_type_ignoring_signedness (TREE_TYPE (type0),
						    TREE_TYPE (type1))))
    {
      binary_op_error (code);
      return error_mark_node;
    }

  if ((code0 == INTEGER_TYPE || code0 == REAL_TYPE || code0 == COMPLEX_TYPE
       || code0 == VECTOR_TYPE)
      &&
      (code1 == INTEGER_TYPE || code1 == REAL_TYPE || code1 == COMPLEX_TYPE
       || code1 == VECTOR_TYPE))
    {
      int none_complex = (code0 != COMPLEX_TYPE && code1 != COMPLEX_TYPE);

      if (shorten || common || short_compare)
	result_type = c_common_type (type0, type1);

      /* For certain operations (which identify themselves by shorten != 0)
	 if both args were extended from the same smaller type,
	 do the arithmetic in that type and then extend.

	 shorten !=0 and !=1 indicates a bitwise operation.
	 For them, this optimization is safe only if
	 both args are zero-extended or both are sign-extended.
	 Otherwise, we might change the result.
	 Eg, (short)-1 | (unsigned short)-1 is (int)-1
	 but calculated in (unsigned short) it would be (unsigned short)-1.  */

      if (shorten && none_complex)
	{
	  int unsigned0, unsigned1;
	  tree arg0, arg1;
	  int uns;
	  tree type;

	  /* Cast OP0 and OP1 to RESULT_TYPE.  Doing so prevents
	     excessive narrowing when we call get_narrower below.  For
	     example, suppose that OP0 is of unsigned int extended
	     from signed char and that RESULT_TYPE is long long int.
	     If we explicitly cast OP0 to RESULT_TYPE, OP0 would look
	     like

	       (long long int) (unsigned int) signed_char

	     which get_narrower would narrow down to

	       (unsigned int) signed char

	     If we do not cast OP0 first, get_narrower would return
	     signed_char, which is inconsistent with the case of the
	     explicit cast.  */
	  op0 = convert (result_type, op0);
	  op1 = convert (result_type, op1);

	  arg0 = get_narrower (op0, &unsigned0);
	  arg1 = get_narrower (op1, &unsigned1);

	  /* UNS is 1 if the operation to be done is an unsigned one.  */
	  uns = TYPE_UNSIGNED (result_type);

	  final_type = result_type;

	  /* Handle the case that OP0 (or OP1) does not *contain* a conversion
	     but it *requires* conversion to FINAL_TYPE.  */

	  if ((TYPE_PRECISION (TREE_TYPE (op0))
	       == TYPE_PRECISION (TREE_TYPE (arg0)))
	      && TREE_TYPE (op0) != final_type)
	    unsigned0 = TYPE_UNSIGNED (TREE_TYPE (op0));
	  if ((TYPE_PRECISION (TREE_TYPE (op1))
	       == TYPE_PRECISION (TREE_TYPE (arg1)))
	      && TREE_TYPE (op1) != final_type)
	    unsigned1 = TYPE_UNSIGNED (TREE_TYPE (op1));

	  /* Now UNSIGNED0 is 1 if ARG0 zero-extends to FINAL_TYPE.  */

	  /* For bitwise operations, signedness of nominal type
	     does not matter.  Consider only how operands were extended.  */
	  if (shorten == -1)
	    uns = unsigned0;

	  /* Note that in all three cases below we refrain from optimizing
	     an unsigned operation on sign-extended args.
	     That would not be valid.  */

	  /* Both args variable: if both extended in same way
	     from same width, do it in that width.
	     Do it unsigned if args were zero-extended.  */
	  if ((TYPE_PRECISION (TREE_TYPE (arg0))
	       < TYPE_PRECISION (result_type))
	      && (TYPE_PRECISION (TREE_TYPE (arg1))
		  == TYPE_PRECISION (TREE_TYPE (arg0)))
	      && unsigned0 == unsigned1
	      && (unsigned0 || !uns))
	    result_type
	      = c_common_signed_or_unsigned_type
	      (unsigned0, common_type (TREE_TYPE (arg0), TREE_TYPE (arg1)));
	  else if (TREE_CODE (arg0) == INTEGER_CST
		   && (unsigned1 || !uns)
		   && (TYPE_PRECISION (TREE_TYPE (arg1))
		       < TYPE_PRECISION (result_type))
		   && (type
		       = c_common_signed_or_unsigned_type (unsigned1,
							   TREE_TYPE (arg1)),
		       int_fits_type_p (arg0, type)))
	    result_type = type;
	  else if (TREE_CODE (arg1) == INTEGER_CST
		   && (unsigned0 || !uns)
		   && (TYPE_PRECISION (TREE_TYPE (arg0))
		       < TYPE_PRECISION (result_type))
		   && (type
		       = c_common_signed_or_unsigned_type (unsigned0,
							   TREE_TYPE (arg0)),
		       int_fits_type_p (arg1, type)))
	    result_type = type;
	}

      /* Shifts can be shortened if shifting right.  */

      if (short_shift)
	{
	  int unsigned_arg;
	  tree arg0 = get_narrower (op0, &unsigned_arg);

	  final_type = result_type;

	  if (arg0 == op0 && final_type == TREE_TYPE (op0))
	    unsigned_arg = TYPE_UNSIGNED (TREE_TYPE (op0));

	  if (TYPE_PRECISION (TREE_TYPE (arg0)) < TYPE_PRECISION (result_type)
	      /* We can shorten only if the shift count is less than the
		 number of bits in the smaller type size.  */
	      && compare_tree_int (op1, TYPE_PRECISION (TREE_TYPE (arg0))) < 0
	      /* We cannot drop an unsigned shift after sign-extension.  */
	      && (!TYPE_UNSIGNED (final_type) || unsigned_arg))
	    {
	      /* Do an unsigned shift if the operand was zero-extended.  */
	      result_type
		= c_common_signed_or_unsigned_type (unsigned_arg,
						    TREE_TYPE (arg0));
	      /* Convert value-to-be-shifted to that type.  */
	      if (TREE_TYPE (op0) != result_type)
		op0 = convert (result_type, op0);
	      converted = 1;
	    }
	}

      /* Comparison operations are shortened too but differently.
	 They identify themselves by setting short_compare = 1.  */

      if (short_compare)
	{
	  /* Don't write &op0, etc., because that would prevent op0
	     from being kept in a register.
	     Instead, make copies of the our local variables and
	     pass the copies by reference, then copy them back afterward.  */
	  tree xop0 = op0, xop1 = op1, xresult_type = result_type;
	  enum tree_code xresultcode = resultcode;
	  tree val
	    = shorten_compare (&xop0, &xop1, &xresult_type, &xresultcode);

	  if (val != 0)
	    return val;

	  op0 = xop0, op1 = xop1;
	  converted = 1;
	  resultcode = xresultcode;

	  if (warn_sign_compare && skip_evaluation == 0)
	    {
	      int op0_signed = !TYPE_UNSIGNED (TREE_TYPE (orig_op0));
	      int op1_signed = !TYPE_UNSIGNED (TREE_TYPE (orig_op1));
	      int unsignedp0, unsignedp1;
	      tree primop0 = get_narrower (op0, &unsignedp0);
	      tree primop1 = get_narrower (op1, &unsignedp1);

	      xop0 = orig_op0;
	      xop1 = orig_op1;
	      STRIP_TYPE_NOPS (xop0);
	      STRIP_TYPE_NOPS (xop1);

	      /* Give warnings for comparisons between signed and unsigned
		 quantities that may fail.

		 Do the checking based on the original operand trees, so that
		 casts will be considered, but default promotions won't be.

		 Do not warn if the comparison is being done in a signed type,
		 since the signed type will only be chosen if it can represent
		 all the values of the unsigned type.  */
	      if (!TYPE_UNSIGNED (result_type))
		/* OK */;
	      /* Do not warn if both operands are the same signedness.  */
	      else if (op0_signed == op1_signed)
		/* OK */;
	      else
		{
		  tree sop, uop;
		  bool ovf;

		  if (op0_signed)
		    sop = xop0, uop = xop1;
		  else
		    sop = xop1, uop = xop0;

		  /* Do not warn if the signed quantity is an
		     unsuffixed integer literal (or some static
		     constant expression involving such literals or a
		     conditional expression involving such literals)
		     and it is non-negative.  */
		  if (tree_expr_nonnegative_warnv_p (sop, &ovf))
		    /* OK */;
		  /* Do not warn if the comparison is an equality operation,
		     the unsigned quantity is an integral constant, and it
		     would fit in the result if the result were signed.  */
		  else if (TREE_CODE (uop) == INTEGER_CST
			   && (resultcode == EQ_EXPR || resultcode == NE_EXPR)
			   && int_fits_type_p
			   (uop, c_common_signed_type (result_type)))
		    /* OK */;
		  /* Do not warn if the unsigned quantity is an enumeration
		     constant and its maximum value would fit in the result
		     if the result were signed.  */
		  else if (TREE_CODE (uop) == INTEGER_CST
			   && TREE_CODE (TREE_TYPE (uop)) == ENUMERAL_TYPE
			   && int_fits_type_p
			   (TYPE_MAX_VALUE (TREE_TYPE (uop)),
			    c_common_signed_type (result_type)))
		    /* OK */;
		  else
		    warning (0, "comparison between signed and unsigned");
		}

	      /* Warn if two unsigned values are being compared in a size
		 larger than their original size, and one (and only one) is the
		 result of a `~' operator.  This comparison will always fail.

		 Also warn if one operand is a constant, and the constant
		 does not have all bits set that are set in the ~ operand
		 when it is extended.  */

	      if ((TREE_CODE (primop0) == BIT_NOT_EXPR)
		  != (TREE_CODE (primop1) == BIT_NOT_EXPR))
		{
		  if (TREE_CODE (primop0) == BIT_NOT_EXPR)
		    primop0 = get_narrower (TREE_OPERAND (primop0, 0),
					    &unsignedp0);
		  else
		    primop1 = get_narrower (TREE_OPERAND (primop1, 0),
					    &unsignedp1);

		  if (host_integerp (primop0, 0) || host_integerp (primop1, 0))
		    {
		      tree primop;
		      HOST_WIDE_INT constant, mask;
		      int unsignedp, bits;

		      if (host_integerp (primop0, 0))
			{
			  primop = primop1;
			  unsignedp = unsignedp1;
			  constant = tree_low_cst (primop0, 0);
			}
		      else
			{
			  primop = primop0;
			  unsignedp = unsignedp0;
			  constant = tree_low_cst (primop1, 0);
			}

		      bits = TYPE_PRECISION (TREE_TYPE (primop));
		      if (bits < TYPE_PRECISION (result_type)
			  && bits < HOST_BITS_PER_WIDE_INT && unsignedp)
			{
			  mask = (~(HOST_WIDE_INT) 0) << bits;
			  if ((mask & constant) != mask)
			    warning (0, "comparison of promoted ~unsigned with constant");
			}
		    }
		  else if (unsignedp0 && unsignedp1
			   && (TYPE_PRECISION (TREE_TYPE (primop0))
			       < TYPE_PRECISION (result_type))
			   && (TYPE_PRECISION (TREE_TYPE (primop1))
			       < TYPE_PRECISION (result_type)))
		    warning (0, "comparison of promoted ~unsigned with unsigned");
		}
	    }
	}
    }

  /* At this point, RESULT_TYPE must be nonzero to avoid an error message.
     If CONVERTED is zero, both args will be converted to type RESULT_TYPE.
     Then the expression will be built.
     It will be given type FINAL_TYPE if that is nonzero;
     otherwise, it will be given type RESULT_TYPE.  */

  if (!result_type)
    {
      binary_op_error (code);
      return error_mark_node;
    }

  if (!converted)
    {
      if (TREE_TYPE (op0) != result_type)
	op0 = convert_and_check (result_type, op0);
      if (TREE_TYPE (op1) != result_type)
	op1 = convert_and_check (result_type, op1);

      /* This can happen if one operand has a vector type, and the other
	 has a different type.  */
      if (TREE_CODE (op0) == ERROR_MARK || TREE_CODE (op1) == ERROR_MARK)
	return error_mark_node;
    }

  if (build_type == NULL_TREE)
    build_type = result_type;

  {
    /* Treat expressions in initializers specially as they can't trap.  */
    tree result = require_constant_value ? fold_build2_initializer (resultcode,
								    build_type,
								    op0, op1)
					 : fold_build2 (resultcode, build_type,
							op0, op1);

    if (final_type != 0)
      result = convert (final_type, result);
    return result;
  }
}


/* Convert EXPR to be a truth-value, validating its type for this
   purpose.  */

tree
c_objc_common_truthvalue_conversion (tree expr)
{
  switch (TREE_CODE (TREE_TYPE (expr)))
    {
    case ARRAY_TYPE:
      error ("used array that cannot be converted to pointer where scalar is required");
      return error_mark_node;

    case RECORD_TYPE:
      error ("used struct type value where scalar is required");
      return error_mark_node;

    case UNION_TYPE:
      error ("used union type value where scalar is required");
      return error_mark_node;

    case FUNCTION_TYPE:
      gcc_unreachable ();

    default:
      break;
    }

  /* ??? Should we also give an error for void and vectors rather than
     leaving those to give errors later?  */
  return c_common_truthvalue_conversion (expr);
}


/* Convert EXPR to a contained DECL, updating *TC, *TI and *SE as
   required.  */

tree
c_expr_to_decl (tree expr, bool *tc ATTRIBUTE_UNUSED,
		bool *ti ATTRIBUTE_UNUSED, bool *se)
{
  if (TREE_CODE (expr) == COMPOUND_LITERAL_EXPR)
    {
      tree decl = COMPOUND_LITERAL_EXPR_DECL (expr);
      /* Executing a compound literal inside a function reinitializes
	 it.  */
      if (!TREE_STATIC (decl))
	*se = true;
      return decl;
    }
  else
    return expr;
}

/* Like c_begin_compound_stmt, except force the retention of the BLOCK.  */

tree
c_begin_omp_parallel (void)
{
  tree block;

  keep_next_level ();
  block = c_begin_compound_stmt (true);

  return block;
}

tree
c_finish_omp_parallel (tree clauses, tree block)
{
  tree stmt;

  block = c_end_compound_stmt (block, true);

  stmt = make_node (OMP_PARALLEL);
  TREE_TYPE (stmt) = void_type_node;
  OMP_PARALLEL_CLAUSES (stmt) = clauses;
  OMP_PARALLEL_BODY (stmt) = block;

  return add_stmt (stmt);
}

/* For all elements of CLAUSES, validate them vs OpenMP constraints.
   Remove any elements from the list that are invalid.  */

tree
c_finish_omp_clauses (tree clauses)
{
  bitmap_head generic_head, firstprivate_head, lastprivate_head;
  tree c, t, *pc = &clauses;
  const char *name;

  bitmap_obstack_initialize (NULL);
  bitmap_initialize (&generic_head, &bitmap_default_obstack);
  bitmap_initialize (&firstprivate_head, &bitmap_default_obstack);
  bitmap_initialize (&lastprivate_head, &bitmap_default_obstack);

  for (pc = &clauses, c = clauses; c ; c = *pc)
    {
      bool remove = false;
      bool need_complete = false;
      bool need_implicitly_determined = false;

      switch (OMP_CLAUSE_CODE (c))
	{
	case OMP_CLAUSE_SHARED:
	  name = "shared";
	  need_implicitly_determined = true;
	  goto check_dup_generic;

	case OMP_CLAUSE_PRIVATE:
	  name = "private";
	  need_complete = true;
	  need_implicitly_determined = true;
	  goto check_dup_generic;

	case OMP_CLAUSE_REDUCTION:
	  name = "reduction";
	  need_implicitly_determined = true;
	  t = OMP_CLAUSE_DECL (c);
	  if (AGGREGATE_TYPE_P (TREE_TYPE (t))
	      || POINTER_TYPE_P (TREE_TYPE (t)))
	    {
	      error ("%qE has invalid type for %<reduction%>", t);
	      remove = true;
	    }
	  else if (FLOAT_TYPE_P (TREE_TYPE (t)))
	    {
	      enum tree_code r_code = OMP_CLAUSE_REDUCTION_CODE (c);
	      const char *r_name = NULL;

	      switch (r_code)
		{
		case PLUS_EXPR:
		case MULT_EXPR:
		case MINUS_EXPR:
		  break;
		case BIT_AND_EXPR:
		  r_name = "&";
		  break;
		case BIT_XOR_EXPR:
		  r_name = "^";
		  break;
		case BIT_IOR_EXPR:
		  r_name = "|";
		  break;
		case TRUTH_ANDIF_EXPR:
		  r_name = "&&";
		  break;
		case TRUTH_ORIF_EXPR:
		  r_name = "||";
		  break;
		default:
		  gcc_unreachable ();
		}
	      if (r_name)
		{
		  error ("%qE has invalid type for %<reduction(%s)%>",
			 t, r_name);
		  remove = true;
		}
	    }
	  goto check_dup_generic;

	case OMP_CLAUSE_COPYPRIVATE:
	  name = "copyprivate";
	  goto check_dup_generic;

	case OMP_CLAUSE_COPYIN:
	  name = "copyin";
	  t = OMP_CLAUSE_DECL (c);
	  if (TREE_CODE (t) != VAR_DECL || !DECL_THREAD_LOCAL_P (t))
	    {
	      error ("%qE must be %<threadprivate%> for %<copyin%>", t);
	      remove = true;
	    }
	  goto check_dup_generic;

	check_dup_generic:
	  t = OMP_CLAUSE_DECL (c);
	  if (TREE_CODE (t) != VAR_DECL && TREE_CODE (t) != PARM_DECL)
	    {
	      error ("%qE is not a variable in clause %qs", t, name);
	      remove = true;
	    }
	  else if (bitmap_bit_p (&generic_head, DECL_UID (t))
		   || bitmap_bit_p (&firstprivate_head, DECL_UID (t))
		   || bitmap_bit_p (&lastprivate_head, DECL_UID (t)))
	    {
	      error ("%qE appears more than once in data clauses", t);
	      remove = true;
	    }
	  else
	    bitmap_set_bit (&generic_head, DECL_UID (t));
	  break;

	case OMP_CLAUSE_FIRSTPRIVATE:
	  name = "firstprivate";
	  t = OMP_CLAUSE_DECL (c);
	  need_complete = true;
	  need_implicitly_determined = true;
	  if (TREE_CODE (t) != VAR_DECL && TREE_CODE (t) != PARM_DECL)
	    {
	      error ("%qE is not a variable in clause %<firstprivate%>", t);
	      remove = true;
	    }
	  else if (bitmap_bit_p (&generic_head, DECL_UID (t))
		   || bitmap_bit_p (&firstprivate_head, DECL_UID (t)))
	    {
	      error ("%qE appears more than once in data clauses", t);
	      remove = true;
	    }
	  else
	    bitmap_set_bit (&firstprivate_head, DECL_UID (t));
	  break;

	case OMP_CLAUSE_LASTPRIVATE:
	  name = "lastprivate";
	  t = OMP_CLAUSE_DECL (c);
	  need_complete = true;
	  need_implicitly_determined = true;
	  if (TREE_CODE (t) != VAR_DECL && TREE_CODE (t) != PARM_DECL)
	    {
	      error ("%qE is not a variable in clause %<lastprivate%>", t);
	      remove = true;
	    }
	  else if (bitmap_bit_p (&generic_head, DECL_UID (t))
		   || bitmap_bit_p (&lastprivate_head, DECL_UID (t)))
	    {
	      error ("%qE appears more than once in data clauses", t);
	      remove = true;
	    }
	  else
	    bitmap_set_bit (&lastprivate_head, DECL_UID (t));
	  break;

	case OMP_CLAUSE_IF:
	case OMP_CLAUSE_NUM_THREADS:
	case OMP_CLAUSE_SCHEDULE:
	case OMP_CLAUSE_NOWAIT:
	case OMP_CLAUSE_ORDERED:
	case OMP_CLAUSE_DEFAULT:
	  pc = &OMP_CLAUSE_CHAIN (c);
	  continue;

	default:
	  gcc_unreachable ();
	}

      if (!remove)
	{
	  t = OMP_CLAUSE_DECL (c);

	  if (need_complete)
	    {
	      t = require_complete_type (t);
	      if (t == error_mark_node)
		remove = true;
	    }

	  if (need_implicitly_determined)
	    {
	      const char *share_name = NULL;

	      if (TREE_CODE (t) == VAR_DECL && DECL_THREAD_LOCAL_P (t))
		share_name = "threadprivate";
	      else switch (c_omp_predetermined_sharing (t))
		{
		case OMP_CLAUSE_DEFAULT_UNSPECIFIED:
		  break;
		case OMP_CLAUSE_DEFAULT_SHARED:
		  share_name = "shared";
		  break;
		case OMP_CLAUSE_DEFAULT_PRIVATE:
		  share_name = "private";
		  break;
		default:
		  gcc_unreachable ();
		}
	      if (share_name)
		{
		  error ("%qE is predetermined %qs for %qs",
			 t, share_name, name);
		  remove = true;
		}
	    }
	}

      if (remove)
	*pc = OMP_CLAUSE_CHAIN (c);
      else
	pc = &OMP_CLAUSE_CHAIN (c);
    }

  bitmap_obstack_release (NULL);
  return clauses;
}
