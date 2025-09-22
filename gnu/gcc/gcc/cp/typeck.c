/* Build expressions with type checking for C++ compiler.
   Copyright (C) 1987, 1988, 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
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


/* This file is part of the C++ front end.
   It contains routines to build C++ expressions given their operands,
   including computing the types of the result, C and C++ specific error
   checks, and some optimization.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "expr.h"
#include "cp-tree.h"
#include "tm_p.h"
#include "flags.h"
#include "output.h"
#include "toplev.h"
#include "diagnostic.h"
#include "target.h"
#include "convert.h"
#include "c-common.h"

static tree pfn_from_ptrmemfunc (tree);
static tree convert_for_assignment (tree, tree, const char *, tree, int);
static tree cp_pointer_int_sum (enum tree_code, tree, tree);
static tree rationalize_conditional_expr (enum tree_code, tree);
static int comp_ptr_ttypes_real (tree, tree, int);
static bool comp_except_types (tree, tree, bool);
static bool comp_array_types (tree, tree, bool);
static tree common_base_type (tree, tree);
static tree pointer_diff (tree, tree, tree);
static tree get_delta_difference (tree, tree, bool, bool);
static void casts_away_constness_r (tree *, tree *);
static bool casts_away_constness (tree, tree);
static void maybe_warn_about_returning_address_of_local (tree);
static tree lookup_destructor (tree, tree, tree);
static tree convert_arguments (tree, tree, tree, int);

/* Do `exp = require_complete_type (exp);' to make sure exp
   does not have an incomplete type.  (That includes void types.)
   Returns the error_mark_node if the VALUE does not have
   complete type when this function returns.  */

tree
require_complete_type (tree value)
{
  tree type;

  if (processing_template_decl || value == error_mark_node)
    return value;

  if (TREE_CODE (value) == OVERLOAD)
    type = unknown_type_node;
  else
    type = TREE_TYPE (value);

  if (type == error_mark_node)
    return error_mark_node;

  /* First, detect a valid value with a complete type.  */
  if (COMPLETE_TYPE_P (type))
    return value;

  if (complete_type_or_else (type, value))
    return value;
  else
    return error_mark_node;
}

/* Try to complete TYPE, if it is incomplete.  For example, if TYPE is
   a template instantiation, do the instantiation.  Returns TYPE,
   whether or not it could be completed, unless something goes
   horribly wrong, in which case the error_mark_node is returned.  */

tree
complete_type (tree type)
{
  if (type == NULL_TREE)
    /* Rather than crash, we return something sure to cause an error
       at some point.  */
    return error_mark_node;

  if (type == error_mark_node || COMPLETE_TYPE_P (type))
    ;
  else if (TREE_CODE (type) == ARRAY_TYPE && TYPE_DOMAIN (type))
    {
      tree t = complete_type (TREE_TYPE (type));
      unsigned int needs_constructing, has_nontrivial_dtor;
      if (COMPLETE_TYPE_P (t) && !dependent_type_p (type))
	layout_type (type);
      needs_constructing
	= TYPE_NEEDS_CONSTRUCTING (TYPE_MAIN_VARIANT (t));
      has_nontrivial_dtor
	= TYPE_HAS_NONTRIVIAL_DESTRUCTOR (TYPE_MAIN_VARIANT (t));
      for (t = TYPE_MAIN_VARIANT (type); t; t = TYPE_NEXT_VARIANT (t))
	{
	  TYPE_NEEDS_CONSTRUCTING (t) = needs_constructing;
	  TYPE_HAS_NONTRIVIAL_DESTRUCTOR (t) = has_nontrivial_dtor;
	}
    }
  else if (CLASS_TYPE_P (type) && CLASSTYPE_TEMPLATE_INSTANTIATION (type))
    instantiate_class_template (TYPE_MAIN_VARIANT (type));

  return type;
}

/* Like complete_type, but issue an error if the TYPE cannot be completed.
   VALUE is used for informative diagnostics.
   Returns NULL_TREE if the type cannot be made complete.  */

tree
complete_type_or_else (tree type, tree value)
{
  type = complete_type (type);
  if (type == error_mark_node)
    /* We already issued an error.  */
    return NULL_TREE;
  else if (!COMPLETE_TYPE_P (type))
    {
      cxx_incomplete_type_diagnostic (value, type, 0);
      return NULL_TREE;
    }
  else
    return type;
}

/* Return truthvalue of whether type of EXP is instantiated.  */

int
type_unknown_p (tree exp)
{
  return (TREE_CODE (exp) == TREE_LIST
	  || TREE_TYPE (exp) == unknown_type_node);
}


/* Return the common type of two parameter lists.
   We assume that comptypes has already been done and returned 1;
   if that isn't so, this may crash.

   As an optimization, free the space we allocate if the parameter
   lists are already common.  */

static tree
commonparms (tree p1, tree p2)
{
  tree oldargs = p1, newargs, n;
  int i, len;
  int any_change = 0;

  len = list_length (p1);
  newargs = tree_last (p1);

  if (newargs == void_list_node)
    i = 1;
  else
    {
      i = 0;
      newargs = 0;
    }

  for (; i < len; i++)
    newargs = tree_cons (NULL_TREE, NULL_TREE, newargs);

  n = newargs;

  for (i = 0; p1;
       p1 = TREE_CHAIN (p1), p2 = TREE_CHAIN (p2), n = TREE_CHAIN (n), i++)
    {
      if (TREE_PURPOSE (p1) && !TREE_PURPOSE (p2))
	{
	  TREE_PURPOSE (n) = TREE_PURPOSE (p1);
	  any_change = 1;
	}
      else if (! TREE_PURPOSE (p1))
	{
	  if (TREE_PURPOSE (p2))
	    {
	      TREE_PURPOSE (n) = TREE_PURPOSE (p2);
	      any_change = 1;
	    }
	}
      else
	{
	  if (1 != simple_cst_equal (TREE_PURPOSE (p1), TREE_PURPOSE (p2)))
	    any_change = 1;
	  TREE_PURPOSE (n) = TREE_PURPOSE (p2);
	}
      if (TREE_VALUE (p1) != TREE_VALUE (p2))
	{
	  any_change = 1;
	  TREE_VALUE (n) = merge_types (TREE_VALUE (p1), TREE_VALUE (p2));
	}
      else
	TREE_VALUE (n) = TREE_VALUE (p1);
    }
  if (! any_change)
    return oldargs;

  return newargs;
}

/* Given a type, perhaps copied for a typedef,
   find the "original" version of it.  */
static tree
original_type (tree t)
{
  int quals = cp_type_quals (t);
  while (t != error_mark_node
	 && TYPE_NAME (t) != NULL_TREE)
    {
      tree x = TYPE_NAME (t);
      if (TREE_CODE (x) != TYPE_DECL)
	break;
      x = DECL_ORIGINAL_TYPE (x);
      if (x == NULL_TREE)
	break;
      t = x;
    }
  return cp_build_qualified_type (t, quals);
}

/* T1 and T2 are arithmetic or enumeration types.  Return the type
   that will result from the "usual arithmetic conversions" on T1 and
   T2 as described in [expr].  */

tree
type_after_usual_arithmetic_conversions (tree t1, tree t2)
{
  enum tree_code code1 = TREE_CODE (t1);
  enum tree_code code2 = TREE_CODE (t2);
  tree attributes;

  /* FIXME: Attributes.  */
  gcc_assert (ARITHMETIC_TYPE_P (t1)
	      || TREE_CODE (t1) == VECTOR_TYPE
	      || TREE_CODE (t1) == ENUMERAL_TYPE);
  gcc_assert (ARITHMETIC_TYPE_P (t2)
	      || TREE_CODE (t2) == VECTOR_TYPE
	      || TREE_CODE (t2) == ENUMERAL_TYPE);

  /* In what follows, we slightly generalize the rules given in [expr] so
     as to deal with `long long' and `complex'.  First, merge the
     attributes.  */
  attributes = (*targetm.merge_type_attributes) (t1, t2);

  /* If one type is complex, form the common type of the non-complex
     components, then make that complex.  Use T1 or T2 if it is the
     required type.  */
  if (code1 == COMPLEX_TYPE || code2 == COMPLEX_TYPE)
    {
      tree subtype1 = code1 == COMPLEX_TYPE ? TREE_TYPE (t1) : t1;
      tree subtype2 = code2 == COMPLEX_TYPE ? TREE_TYPE (t2) : t2;
      tree subtype
	= type_after_usual_arithmetic_conversions (subtype1, subtype2);

      if (code1 == COMPLEX_TYPE && TREE_TYPE (t1) == subtype)
	return build_type_attribute_variant (t1, attributes);
      else if (code2 == COMPLEX_TYPE && TREE_TYPE (t2) == subtype)
	return build_type_attribute_variant (t2, attributes);
      else
	return build_type_attribute_variant (build_complex_type (subtype),
					     attributes);
    }

  if (code1 == VECTOR_TYPE)
    {
      /* When we get here we should have two vectors of the same size.
	 Just prefer the unsigned one if present.  */
      if (TYPE_UNSIGNED (t1))
	return build_type_attribute_variant (t1, attributes);
      else
	return build_type_attribute_variant (t2, attributes);
    }

  /* If only one is real, use it as the result.  */
  if (code1 == REAL_TYPE && code2 != REAL_TYPE)
    return build_type_attribute_variant (t1, attributes);
  if (code2 == REAL_TYPE && code1 != REAL_TYPE)
    return build_type_attribute_variant (t2, attributes);

  /* Perform the integral promotions.  */
  if (code1 != REAL_TYPE)
    {
      t1 = type_promotes_to (t1);
      t2 = type_promotes_to (t2);
    }

  /* Both real or both integers; use the one with greater precision.  */
  if (TYPE_PRECISION (t1) > TYPE_PRECISION (t2))
    return build_type_attribute_variant (t1, attributes);
  else if (TYPE_PRECISION (t2) > TYPE_PRECISION (t1))
    return build_type_attribute_variant (t2, attributes);

  /* The types are the same; no need to do anything fancy.  */
  if (TYPE_MAIN_VARIANT (t1) == TYPE_MAIN_VARIANT (t2))
    return build_type_attribute_variant (t1, attributes);

  if (code1 != REAL_TYPE)
    {
      /* If one is a sizetype, use it so size_binop doesn't blow up.  */
      if (TYPE_IS_SIZETYPE (t1) > TYPE_IS_SIZETYPE (t2))
	return build_type_attribute_variant (t1, attributes);
      if (TYPE_IS_SIZETYPE (t2) > TYPE_IS_SIZETYPE (t1))
	return build_type_attribute_variant (t2, attributes);

      /* If one is unsigned long long, then convert the other to unsigned
	 long long.  */
      if (same_type_p (TYPE_MAIN_VARIANT (t1), long_long_unsigned_type_node)
	  || same_type_p (TYPE_MAIN_VARIANT (t2), long_long_unsigned_type_node))
	return build_type_attribute_variant (long_long_unsigned_type_node,
					     attributes);
      /* If one is a long long, and the other is an unsigned long, and
	 long long can represent all the values of an unsigned long, then
	 convert to a long long.  Otherwise, convert to an unsigned long
	 long.  Otherwise, if either operand is long long, convert the
	 other to long long.

	 Since we're here, we know the TYPE_PRECISION is the same;
	 therefore converting to long long cannot represent all the values
	 of an unsigned long, so we choose unsigned long long in that
	 case.  */
      if (same_type_p (TYPE_MAIN_VARIANT (t1), long_long_integer_type_node)
	  || same_type_p (TYPE_MAIN_VARIANT (t2), long_long_integer_type_node))
	{
	  tree t = ((TYPE_UNSIGNED (t1) || TYPE_UNSIGNED (t2))
		    ? long_long_unsigned_type_node
		    : long_long_integer_type_node);
	  return build_type_attribute_variant (t, attributes);
	}

      /* Go through the same procedure, but for longs.  */
      if (same_type_p (TYPE_MAIN_VARIANT (t1), long_unsigned_type_node)
	  || same_type_p (TYPE_MAIN_VARIANT (t2), long_unsigned_type_node))
	return build_type_attribute_variant (long_unsigned_type_node,
					     attributes);
      if (same_type_p (TYPE_MAIN_VARIANT (t1), long_integer_type_node)
	  || same_type_p (TYPE_MAIN_VARIANT (t2), long_integer_type_node))
	{
	  tree t = ((TYPE_UNSIGNED (t1) || TYPE_UNSIGNED (t2))
		    ? long_unsigned_type_node : long_integer_type_node);
	  return build_type_attribute_variant (t, attributes);
	}
      /* Otherwise prefer the unsigned one.  */
      if (TYPE_UNSIGNED (t1))
	return build_type_attribute_variant (t1, attributes);
      else
	return build_type_attribute_variant (t2, attributes);
    }
  else
    {
      if (same_type_p (TYPE_MAIN_VARIANT (t1), long_double_type_node)
	  || same_type_p (TYPE_MAIN_VARIANT (t2), long_double_type_node))
	return build_type_attribute_variant (long_double_type_node,
					     attributes);
      if (same_type_p (TYPE_MAIN_VARIANT (t1), double_type_node)
	  || same_type_p (TYPE_MAIN_VARIANT (t2), double_type_node))
	return build_type_attribute_variant (double_type_node,
					     attributes);
      if (same_type_p (TYPE_MAIN_VARIANT (t1), float_type_node)
	  || same_type_p (TYPE_MAIN_VARIANT (t2), float_type_node))
	return build_type_attribute_variant (float_type_node,
					     attributes);

      /* Two floating-point types whose TYPE_MAIN_VARIANTs are none of
	 the standard C++ floating-point types.  Logic earlier in this
	 function has already eliminated the possibility that
	 TYPE_PRECISION (t2) != TYPE_PRECISION (t1), so there's no
	 compelling reason to choose one or the other.  */
      return build_type_attribute_variant (t1, attributes);
    }
}

/* Subroutine of composite_pointer_type to implement the recursive
   case.  See that function for documentation fo the parameters.  */

static tree
composite_pointer_type_r (tree t1, tree t2, const char* location)
{
  tree pointee1;
  tree pointee2;
  tree result_type;
  tree attributes;

  /* Determine the types pointed to by T1 and T2.  */
  if (TREE_CODE (t1) == POINTER_TYPE)
    {
      pointee1 = TREE_TYPE (t1);
      pointee2 = TREE_TYPE (t2);
    }
  else
    {
      pointee1 = TYPE_PTRMEM_POINTED_TO_TYPE (t1);
      pointee2 = TYPE_PTRMEM_POINTED_TO_TYPE (t2);
    }

  /* [expr.rel]

     Otherwise, the composite pointer type is a pointer type
     similar (_conv.qual_) to the type of one of the operands,
     with a cv-qualification signature (_conv.qual_) that is the
     union of the cv-qualification signatures of the operand
     types.  */
  if (same_type_ignoring_top_level_qualifiers_p (pointee1, pointee2))
    result_type = pointee1;
  else if ((TREE_CODE (pointee1) == POINTER_TYPE
	    && TREE_CODE (pointee2) == POINTER_TYPE)
	   || (TYPE_PTR_TO_MEMBER_P (pointee1)
	       && TYPE_PTR_TO_MEMBER_P (pointee2)))
    result_type = composite_pointer_type_r (pointee1, pointee2, location);
  else
    {
      pedwarn ("%s between distinct pointer types %qT and %qT "
	       "lacks a cast",
	       location, t1, t2);
      result_type = void_type_node;
    }
  result_type = cp_build_qualified_type (result_type,
					 (cp_type_quals (pointee1)
					  | cp_type_quals (pointee2)));
  /* If the original types were pointers to members, so is the
     result.  */
  if (TYPE_PTR_TO_MEMBER_P (t1))
    {
      if (!same_type_p (TYPE_PTRMEM_CLASS_TYPE (t1),
			TYPE_PTRMEM_CLASS_TYPE (t2)))
	pedwarn ("%s between distinct pointer types %qT and %qT "
		 "lacks a cast",
		 location, t1, t2);
      result_type = build_ptrmem_type (TYPE_PTRMEM_CLASS_TYPE (t1),
				       result_type);
    }
  else
    result_type = build_pointer_type (result_type);

  /* Merge the attributes.  */
  attributes = (*targetm.merge_type_attributes) (t1, t2);
  return build_type_attribute_variant (result_type, attributes);
}

/* Return the composite pointer type (see [expr.rel]) for T1 and T2.
   ARG1 and ARG2 are the values with those types.  The LOCATION is a
   string describing the current location, in case an error occurs.

   This routine also implements the computation of a common type for
   pointers-to-members as per [expr.eq].  */

tree
composite_pointer_type (tree t1, tree t2, tree arg1, tree arg2,
			const char* location)
{
  tree class1;
  tree class2;

  /* [expr.rel]

     If one operand is a null pointer constant, the composite pointer
     type is the type of the other operand.  */
  if (null_ptr_cst_p (arg1))
    return t2;
  if (null_ptr_cst_p (arg2))
    return t1;

  /* We have:

       [expr.rel]

       If one of the operands has type "pointer to cv1 void*", then
       the other has type "pointer to cv2T", and the composite pointer
       type is "pointer to cv12 void", where cv12 is the union of cv1
       and cv2.

    If either type is a pointer to void, make sure it is T1.  */
  if (TREE_CODE (t2) == POINTER_TYPE && VOID_TYPE_P (TREE_TYPE (t2)))
    {
      tree t;
      t = t1;
      t1 = t2;
      t2 = t;
    }

  /* Now, if T1 is a pointer to void, merge the qualifiers.  */
  if (TREE_CODE (t1) == POINTER_TYPE && VOID_TYPE_P (TREE_TYPE (t1)))
    {
      tree attributes;
      tree result_type;

      if (pedantic && TYPE_PTRFN_P (t2))
	pedwarn ("ISO C++ forbids %s between pointer of type %<void *%> "
		 "and pointer-to-function", location);
      result_type
	= cp_build_qualified_type (void_type_node,
				   (cp_type_quals (TREE_TYPE (t1))
				    | cp_type_quals (TREE_TYPE (t2))));
      result_type = build_pointer_type (result_type);
      /* Merge the attributes.  */
      attributes = (*targetm.merge_type_attributes) (t1, t2);
      return build_type_attribute_variant (result_type, attributes);
    }

  if (c_dialect_objc () && TREE_CODE (t1) == POINTER_TYPE
      && TREE_CODE (t2) == POINTER_TYPE)
    {
      if (objc_compare_types (t1, t2, -3, NULL_TREE))
	return t1;
    }

  /* [expr.eq] permits the application of a pointer conversion to
     bring the pointers to a common type.  */
  if (TREE_CODE (t1) == POINTER_TYPE && TREE_CODE (t2) == POINTER_TYPE
      && CLASS_TYPE_P (TREE_TYPE (t1))
      && CLASS_TYPE_P (TREE_TYPE (t2))
      && !same_type_ignoring_top_level_qualifiers_p (TREE_TYPE (t1),
						     TREE_TYPE (t2)))
    {
      class1 = TREE_TYPE (t1);
      class2 = TREE_TYPE (t2);

      if (DERIVED_FROM_P (class1, class2))
	t2 = (build_pointer_type
	      (cp_build_qualified_type (class1, TYPE_QUALS (class2))));
      else if (DERIVED_FROM_P (class2, class1))
	t1 = (build_pointer_type
	      (cp_build_qualified_type (class2, TYPE_QUALS (class1))));
      else
	{
	  error ("%s between distinct pointer types %qT and %qT "
		 "lacks a cast", location, t1, t2);
	  return error_mark_node;
	}
    }
  /* [expr.eq] permits the application of a pointer-to-member
     conversion to change the class type of one of the types.  */
  else if (TYPE_PTR_TO_MEMBER_P (t1)
	   && !same_type_p (TYPE_PTRMEM_CLASS_TYPE (t1),
			    TYPE_PTRMEM_CLASS_TYPE (t2)))
    {
      class1 = TYPE_PTRMEM_CLASS_TYPE (t1);
      class2 = TYPE_PTRMEM_CLASS_TYPE (t2);

      if (DERIVED_FROM_P (class1, class2))
	t1 = build_ptrmem_type (class2, TYPE_PTRMEM_POINTED_TO_TYPE (t1));
      else if (DERIVED_FROM_P (class2, class1))
	t2 = build_ptrmem_type (class1, TYPE_PTRMEM_POINTED_TO_TYPE (t2));
      else
	{
	  error ("%s between distinct pointer-to-member types %qT and %qT "
		 "lacks a cast", location, t1, t2);
	  return error_mark_node;
	}
    }

  return composite_pointer_type_r (t1, t2, location);
}

/* Return the merged type of two types.
   We assume that comptypes has already been done and returned 1;
   if that isn't so, this may crash.

   This just combines attributes and default arguments; any other
   differences would cause the two types to compare unalike.  */

tree
merge_types (tree t1, tree t2)
{
  enum tree_code code1;
  enum tree_code code2;
  tree attributes;

  /* Save time if the two types are the same.  */
  if (t1 == t2)
    return t1;
  if (original_type (t1) == original_type (t2))
    return t1;

  /* If one type is nonsense, use the other.  */
  if (t1 == error_mark_node)
    return t2;
  if (t2 == error_mark_node)
    return t1;

  /* Merge the attributes.  */
  attributes = (*targetm.merge_type_attributes) (t1, t2);

  if (TYPE_PTRMEMFUNC_P (t1))
    t1 = TYPE_PTRMEMFUNC_FN_TYPE (t1);
  if (TYPE_PTRMEMFUNC_P (t2))
    t2 = TYPE_PTRMEMFUNC_FN_TYPE (t2);

  code1 = TREE_CODE (t1);
  code2 = TREE_CODE (t2);

  switch (code1)
    {
    case POINTER_TYPE:
    case REFERENCE_TYPE:
      /* For two pointers, do this recursively on the target type.  */
      {
	tree target = merge_types (TREE_TYPE (t1), TREE_TYPE (t2));
	int quals = cp_type_quals (t1);

	if (code1 == POINTER_TYPE)
	  t1 = build_pointer_type (target);
	else
	  t1 = build_reference_type (target);
	t1 = build_type_attribute_variant (t1, attributes);
	t1 = cp_build_qualified_type (t1, quals);

	if (TREE_CODE (target) == METHOD_TYPE)
	  t1 = build_ptrmemfunc_type (t1);

	return t1;
      }

    case OFFSET_TYPE:
      {
	int quals;
	tree pointee;
	quals = cp_type_quals (t1);
	pointee = merge_types (TYPE_PTRMEM_POINTED_TO_TYPE (t1),
			       TYPE_PTRMEM_POINTED_TO_TYPE (t2));
	t1 = build_ptrmem_type (TYPE_PTRMEM_CLASS_TYPE (t1),
				pointee);
	t1 = cp_build_qualified_type (t1, quals);
	break;
      }

    case ARRAY_TYPE:
      {
	tree elt = merge_types (TREE_TYPE (t1), TREE_TYPE (t2));
	/* Save space: see if the result is identical to one of the args.  */
	if (elt == TREE_TYPE (t1) && TYPE_DOMAIN (t1))
	  return build_type_attribute_variant (t1, attributes);
	if (elt == TREE_TYPE (t2) && TYPE_DOMAIN (t2))
	  return build_type_attribute_variant (t2, attributes);
	/* Merge the element types, and have a size if either arg has one.  */
	t1 = build_cplus_array_type
	  (elt, TYPE_DOMAIN (TYPE_DOMAIN (t1) ? t1 : t2));
	break;
      }

    case FUNCTION_TYPE:
      /* Function types: prefer the one that specified arg types.
	 If both do, merge the arg types.  Also merge the return types.  */
      {
	tree valtype = merge_types (TREE_TYPE (t1), TREE_TYPE (t2));
	tree p1 = TYPE_ARG_TYPES (t1);
	tree p2 = TYPE_ARG_TYPES (t2);
	tree rval, raises;

	/* Save space: see if the result is identical to one of the args.  */
	if (valtype == TREE_TYPE (t1) && ! p2)
	  return cp_build_type_attribute_variant (t1, attributes);
	if (valtype == TREE_TYPE (t2) && ! p1)
	  return cp_build_type_attribute_variant (t2, attributes);

	/* Simple way if one arg fails to specify argument types.  */
	if (p1 == NULL_TREE || TREE_VALUE (p1) == void_type_node)
	  {
	    rval = build_function_type (valtype, p2);
	    if ((raises = TYPE_RAISES_EXCEPTIONS (t2)))
	      rval = build_exception_variant (rval, raises);
	    return cp_build_type_attribute_variant (rval, attributes);
	  }
	raises = TYPE_RAISES_EXCEPTIONS (t1);
	if (p2 == NULL_TREE || TREE_VALUE (p2) == void_type_node)
	  {
	    rval = build_function_type (valtype, p1);
	    if (raises)
	      rval = build_exception_variant (rval, raises);
	    return cp_build_type_attribute_variant (rval, attributes);
	  }

	rval = build_function_type (valtype, commonparms (p1, p2));
	t1 = build_exception_variant (rval, raises);
	break;
      }

    case METHOD_TYPE:
      {
	/* Get this value the long way, since TYPE_METHOD_BASETYPE
	   is just the main variant of this.  */
	tree basetype = TREE_TYPE (TREE_VALUE (TYPE_ARG_TYPES (t2)));
	tree raises = TYPE_RAISES_EXCEPTIONS (t1);
	tree t3;

	/* If this was a member function type, get back to the
	   original type of type member function (i.e., without
	   the class instance variable up front.  */
	t1 = build_function_type (TREE_TYPE (t1),
				  TREE_CHAIN (TYPE_ARG_TYPES (t1)));
	t2 = build_function_type (TREE_TYPE (t2),
				  TREE_CHAIN (TYPE_ARG_TYPES (t2)));
	t3 = merge_types (t1, t2);
	t3 = build_method_type_directly (basetype, TREE_TYPE (t3),
					 TYPE_ARG_TYPES (t3));
	t1 = build_exception_variant (t3, raises);
	break;
      }

    case TYPENAME_TYPE:
      /* There is no need to merge attributes into a TYPENAME_TYPE.
	 When the type is instantiated it will have whatever
	 attributes result from the instantiation.  */
      return t1;

    default:;
    }

  if (attribute_list_equal (TYPE_ATTRIBUTES (t1), attributes))
    return t1;
  else if (attribute_list_equal (TYPE_ATTRIBUTES (t2), attributes))
    return t2;
  else
    return cp_build_type_attribute_variant (t1, attributes);
}

/* Return the common type of two types.
   We assume that comptypes has already been done and returned 1;
   if that isn't so, this may crash.

   This is the type for the result of most arithmetic operations
   if the operands have the given two types.  */

tree
common_type (tree t1, tree t2)
{
  enum tree_code code1;
  enum tree_code code2;

  /* If one type is nonsense, bail.  */
  if (t1 == error_mark_node || t2 == error_mark_node)
    return error_mark_node;

  code1 = TREE_CODE (t1);
  code2 = TREE_CODE (t2);

  if ((ARITHMETIC_TYPE_P (t1) || code1 == ENUMERAL_TYPE
       || code1 == VECTOR_TYPE)
      && (ARITHMETIC_TYPE_P (t2) || code2 == ENUMERAL_TYPE
	  || code2 == VECTOR_TYPE))
    return type_after_usual_arithmetic_conversions (t1, t2);

  else if ((TYPE_PTR_P (t1) && TYPE_PTR_P (t2))
	   || (TYPE_PTRMEM_P (t1) && TYPE_PTRMEM_P (t2))
	   || (TYPE_PTRMEMFUNC_P (t1) && TYPE_PTRMEMFUNC_P (t2)))
    return composite_pointer_type (t1, t2, error_mark_node, error_mark_node,
				   "conversion");
  else
    gcc_unreachable ();
}

/* Compare two exception specifier types for exactness or subsetness, if
   allowed. Returns false for mismatch, true for match (same, or
   derived and !exact).

   [except.spec] "If a class X ... objects of class X or any class publicly
   and unambiguously derived from X. Similarly, if a pointer type Y * ...
   exceptions of type Y * or that are pointers to any type publicly and
   unambiguously derived from Y. Otherwise a function only allows exceptions
   that have the same type ..."
   This does not mention cv qualifiers and is different to what throw
   [except.throw] and catch [except.catch] will do. They will ignore the
   top level cv qualifiers, and allow qualifiers in the pointer to class
   example.

   We implement the letter of the standard.  */

static bool
comp_except_types (tree a, tree b, bool exact)
{
  if (same_type_p (a, b))
    return true;
  else if (!exact)
    {
      if (cp_type_quals (a) || cp_type_quals (b))
	return false;

      if (TREE_CODE (a) == POINTER_TYPE
	  && TREE_CODE (b) == POINTER_TYPE)
	{
	  a = TREE_TYPE (a);
	  b = TREE_TYPE (b);
	  if (cp_type_quals (a) || cp_type_quals (b))
	    return false;
	}

      if (TREE_CODE (a) != RECORD_TYPE
	  || TREE_CODE (b) != RECORD_TYPE)
	return false;

      if (PUBLICLY_UNIQUELY_DERIVED_P (a, b))
	return true;
    }
  return false;
}

/* Return true if TYPE1 and TYPE2 are equivalent exception specifiers.
   If EXACT is false, T2 can be stricter than T1 (according to 15.4/7),
   otherwise it must be exact. Exception lists are unordered, but
   we've already filtered out duplicates. Most lists will be in order,
   we should try to make use of that.  */

bool
comp_except_specs (tree t1, tree t2, bool exact)
{
  tree probe;
  tree base;
  int  length = 0;

  if (t1 == t2)
    return true;

  if (t1 == NULL_TREE)			   /* T1 is ...  */
    return t2 == NULL_TREE || !exact;
  if (!TREE_VALUE (t1))			   /* t1 is EMPTY */
    return t2 != NULL_TREE && !TREE_VALUE (t2);
  if (t2 == NULL_TREE)			   /* T2 is ...  */
    return false;
  if (TREE_VALUE (t1) && !TREE_VALUE (t2)) /* T2 is EMPTY, T1 is not */
    return !exact;

  /* Neither set is ... or EMPTY, make sure each part of T2 is in T1.
     Count how many we find, to determine exactness. For exact matching and
     ordered T1, T2, this is an O(n) operation, otherwise its worst case is
     O(nm).  */
  for (base = t1; t2 != NULL_TREE; t2 = TREE_CHAIN (t2))
    {
      for (probe = base; probe != NULL_TREE; probe = TREE_CHAIN (probe))
	{
	  tree a = TREE_VALUE (probe);
	  tree b = TREE_VALUE (t2);

	  if (comp_except_types (a, b, exact))
	    {
	      if (probe == base && exact)
		base = TREE_CHAIN (probe);
	      length++;
	      break;
	    }
	}
      if (probe == NULL_TREE)
	return false;
    }
  return !exact || base == NULL_TREE || length == list_length (t1);
}

/* Compare the array types T1 and T2.  ALLOW_REDECLARATION is true if
   [] can match [size].  */

static bool
comp_array_types (tree t1, tree t2, bool allow_redeclaration)
{
  tree d1;
  tree d2;
  tree max1, max2;

  if (t1 == t2)
    return true;

  /* The type of the array elements must be the same.  */
  if (!same_type_p (TREE_TYPE (t1), TREE_TYPE (t2)))
    return false;

  d1 = TYPE_DOMAIN (t1);
  d2 = TYPE_DOMAIN (t2);

  if (d1 == d2)
    return true;

  /* If one of the arrays is dimensionless, and the other has a
     dimension, they are of different types.  However, it is valid to
     write:

       extern int a[];
       int a[3];

     by [basic.link]:

       declarations for an array object can specify
       array types that differ by the presence or absence of a major
       array bound (_dcl.array_).  */
  if (!d1 || !d2)
    return allow_redeclaration;

  /* Check that the dimensions are the same.  */

  if (!cp_tree_equal (TYPE_MIN_VALUE (d1), TYPE_MIN_VALUE (d2)))
    return false;
  max1 = TYPE_MAX_VALUE (d1);
  max2 = TYPE_MAX_VALUE (d2);
  if (processing_template_decl && !abi_version_at_least (2)
      && !value_dependent_expression_p (max1)
      && !value_dependent_expression_p (max2))
    {
      /* With abi-1 we do not fold non-dependent array bounds, (and
	 consequently mangle them incorrectly).  We must therefore
	 fold them here, to verify the domains have the same
	 value.  */
      max1 = fold (max1);
      max2 = fold (max2);
    }

  if (!cp_tree_equal (max1, max2))
    return false;

  return true;
}

/* Return true if T1 and T2 are related as allowed by STRICT.  STRICT
   is a bitwise-or of the COMPARE_* flags.  */

bool
comptypes (tree t1, tree t2, int strict)
{
  if (t1 == t2)
    return true;

  /* Suppress errors caused by previously reported errors.  */
  if (t1 == error_mark_node || t2 == error_mark_node)
    return false;

  gcc_assert (TYPE_P (t1) && TYPE_P (t2));

  /* TYPENAME_TYPEs should be resolved if the qualifying scope is the
     current instantiation.  */
  if (TREE_CODE (t1) == TYPENAME_TYPE)
    {
      tree resolved = resolve_typename_type (t1, /*only_current_p=*/true);

      if (resolved != error_mark_node)
	t1 = resolved;
    }

  if (TREE_CODE (t2) == TYPENAME_TYPE)
    {
      tree resolved = resolve_typename_type (t2, /*only_current_p=*/true);

      if (resolved != error_mark_node)
	t2 = resolved;
    }

  /* If either type is the internal version of sizetype, use the
     language version.  */
  if (TREE_CODE (t1) == INTEGER_TYPE && TYPE_IS_SIZETYPE (t1)
      && TYPE_ORIG_SIZE_TYPE (t1))
    t1 = TYPE_ORIG_SIZE_TYPE (t1);

  if (TREE_CODE (t2) == INTEGER_TYPE && TYPE_IS_SIZETYPE (t2)
      && TYPE_ORIG_SIZE_TYPE (t2))
    t2 = TYPE_ORIG_SIZE_TYPE (t2);

  if (TYPE_PTRMEMFUNC_P (t1))
    t1 = TYPE_PTRMEMFUNC_FN_TYPE (t1);
  if (TYPE_PTRMEMFUNC_P (t2))
    t2 = TYPE_PTRMEMFUNC_FN_TYPE (t2);

  /* Different classes of types can't be compatible.  */
  if (TREE_CODE (t1) != TREE_CODE (t2))
    return false;

  /* Qualifiers must match.  For array types, we will check when we
     recur on the array element types.  */
  if (TREE_CODE (t1) != ARRAY_TYPE
      && TYPE_QUALS (t1) != TYPE_QUALS (t2))
    return false;
  if (TYPE_FOR_JAVA (t1) != TYPE_FOR_JAVA (t2))
    return false;

  /* Allow for two different type nodes which have essentially the same
     definition.  Note that we already checked for equality of the type
     qualifiers (just above).  */

  if (TREE_CODE (t1) != ARRAY_TYPE
      && TYPE_MAIN_VARIANT (t1) == TYPE_MAIN_VARIANT (t2))
    return true;

  /* Compare the types.  Break out if they could be the same.  */
  switch (TREE_CODE (t1))
    {
    case TEMPLATE_TEMPLATE_PARM:
    case BOUND_TEMPLATE_TEMPLATE_PARM:
      if (TEMPLATE_TYPE_IDX (t1) != TEMPLATE_TYPE_IDX (t2)
	  || TEMPLATE_TYPE_LEVEL (t1) != TEMPLATE_TYPE_LEVEL (t2))
	return false;
      if (!comp_template_parms
	  (DECL_TEMPLATE_PARMS (TEMPLATE_TEMPLATE_PARM_TEMPLATE_DECL (t1)),
	   DECL_TEMPLATE_PARMS (TEMPLATE_TEMPLATE_PARM_TEMPLATE_DECL (t2))))
	return false;
      if (TREE_CODE (t1) == TEMPLATE_TEMPLATE_PARM)
	break;
      /* Don't check inheritance.  */
      strict = COMPARE_STRICT;
      /* Fall through.  */

    case RECORD_TYPE:
    case UNION_TYPE:
      if (TYPE_TEMPLATE_INFO (t1) && TYPE_TEMPLATE_INFO (t2)
	  && (TYPE_TI_TEMPLATE (t1) == TYPE_TI_TEMPLATE (t2)
	      || TREE_CODE (t1) == BOUND_TEMPLATE_TEMPLATE_PARM)
	  && comp_template_args (TYPE_TI_ARGS (t1), TYPE_TI_ARGS (t2)))
	break;

      if ((strict & COMPARE_BASE) && DERIVED_FROM_P (t1, t2))
	break;
      else if ((strict & COMPARE_DERIVED) && DERIVED_FROM_P (t2, t1))
	break;

      return false;

    case OFFSET_TYPE:
      if (!comptypes (TYPE_OFFSET_BASETYPE (t1), TYPE_OFFSET_BASETYPE (t2),
		      strict & ~COMPARE_REDECLARATION))
	return false;
      if (!same_type_p (TREE_TYPE (t1), TREE_TYPE (t2)))
	return false;
      break;

    case POINTER_TYPE:
    case REFERENCE_TYPE:
      if (TYPE_MODE (t1) != TYPE_MODE (t2)
	  || TYPE_REF_CAN_ALIAS_ALL (t1) != TYPE_REF_CAN_ALIAS_ALL (t2)
	  || !same_type_p (TREE_TYPE (t1), TREE_TYPE (t2)))
	return false;
      break;

    case METHOD_TYPE:
    case FUNCTION_TYPE:
      if (!same_type_p (TREE_TYPE (t1), TREE_TYPE (t2)))
	return false;
      if (!compparms (TYPE_ARG_TYPES (t1), TYPE_ARG_TYPES (t2)))
	return false;
      break;

    case ARRAY_TYPE:
      /* Target types must match incl. qualifiers.  */
      if (!comp_array_types (t1, t2, !!(strict & COMPARE_REDECLARATION)))
	return false;
      break;

    case TEMPLATE_TYPE_PARM:
      if (TEMPLATE_TYPE_IDX (t1) != TEMPLATE_TYPE_IDX (t2)
	  || TEMPLATE_TYPE_LEVEL (t1) != TEMPLATE_TYPE_LEVEL (t2))
	return false;
      break;

    case TYPENAME_TYPE:
      if (!cp_tree_equal (TYPENAME_TYPE_FULLNAME (t1),
			  TYPENAME_TYPE_FULLNAME (t2)))
	return false;
      if (!same_type_p (TYPE_CONTEXT (t1), TYPE_CONTEXT (t2)))
	return false;
      break;

    case UNBOUND_CLASS_TEMPLATE:
      if (!cp_tree_equal (TYPE_IDENTIFIER (t1), TYPE_IDENTIFIER (t2)))
	return false;
      if (!same_type_p (TYPE_CONTEXT (t1), TYPE_CONTEXT (t2)))
	return false;
      break;

    case COMPLEX_TYPE:
      if (!same_type_p (TREE_TYPE (t1), TREE_TYPE (t2)))
	return false;
      break;

    case VECTOR_TYPE:
      if (TYPE_VECTOR_SUBPARTS (t1) != TYPE_VECTOR_SUBPARTS (t2)
	  || !same_type_p (TREE_TYPE (t1), TREE_TYPE (t2)))
	return false;
      break;

    default:
      return false;
    }

  /* If we get here, we know that from a target independent POV the
     types are the same.  Make sure the target attributes are also
     the same.  */
  return targetm.comp_type_attributes (t1, t2);
}

/* Returns 1 if TYPE1 is at least as qualified as TYPE2.  */

bool
at_least_as_qualified_p (tree type1, tree type2)
{
  int q1 = cp_type_quals (type1);
  int q2 = cp_type_quals (type2);

  /* All qualifiers for TYPE2 must also appear in TYPE1.  */
  return (q1 & q2) == q2;
}

/* Returns 1 if TYPE1 is more cv-qualified than TYPE2, -1 if TYPE2 is
   more cv-qualified that TYPE1, and 0 otherwise.  */

int
comp_cv_qualification (tree type1, tree type2)
{
  int q1 = cp_type_quals (type1);
  int q2 = cp_type_quals (type2);

  if (q1 == q2)
    return 0;

  if ((q1 & q2) == q2)
    return 1;
  else if ((q1 & q2) == q1)
    return -1;

  return 0;
}

/* Returns 1 if the cv-qualification signature of TYPE1 is a proper
   subset of the cv-qualification signature of TYPE2, and the types
   are similar.  Returns -1 if the other way 'round, and 0 otherwise.  */

int
comp_cv_qual_signature (tree type1, tree type2)
{
  if (comp_ptr_ttypes_real (type2, type1, -1))
    return 1;
  else if (comp_ptr_ttypes_real (type1, type2, -1))
    return -1;
  else
    return 0;
}

/* If two types share a common base type, return that basetype.
   If there is not a unique most-derived base type, this function
   returns ERROR_MARK_NODE.  */

static tree
common_base_type (tree tt1, tree tt2)
{
  tree best = NULL_TREE;
  int i;

  /* If one is a baseclass of another, that's good enough.  */
  if (UNIQUELY_DERIVED_FROM_P (tt1, tt2))
    return tt1;
  if (UNIQUELY_DERIVED_FROM_P (tt2, tt1))
    return tt2;

  /* Otherwise, try to find a unique baseclass of TT1
     that is shared by TT2, and follow that down.  */
  for (i = BINFO_N_BASE_BINFOS (TYPE_BINFO (tt1))-1; i >= 0; i--)
    {
      tree basetype = BINFO_TYPE (BINFO_BASE_BINFO (TYPE_BINFO (tt1), i));
      tree trial = common_base_type (basetype, tt2);

      if (trial)
	{
	  if (trial == error_mark_node)
	    return trial;
	  if (best == NULL_TREE)
	    best = trial;
	  else if (best != trial)
	    return error_mark_node;
	}
    }

  /* Same for TT2.  */
  for (i = BINFO_N_BASE_BINFOS (TYPE_BINFO (tt2))-1; i >= 0; i--)
    {
      tree basetype = BINFO_TYPE (BINFO_BASE_BINFO (TYPE_BINFO (tt2), i));
      tree trial = common_base_type (tt1, basetype);

      if (trial)
	{
	  if (trial == error_mark_node)
	    return trial;
	  if (best == NULL_TREE)
	    best = trial;
	  else if (best != trial)
	    return error_mark_node;
	}
    }
  return best;
}

/* Subroutines of `comptypes'.  */

/* Return true if two parameter type lists PARMS1 and PARMS2 are
   equivalent in the sense that functions with those parameter types
   can have equivalent types.  The two lists must be equivalent,
   element by element.  */

bool
compparms (tree parms1, tree parms2)
{
  tree t1, t2;

  /* An unspecified parmlist matches any specified parmlist
     whose argument types don't need default promotions.  */

  for (t1 = parms1, t2 = parms2;
       t1 || t2;
       t1 = TREE_CHAIN (t1), t2 = TREE_CHAIN (t2))
    {
      /* If one parmlist is shorter than the other,
	 they fail to match.  */
      if (!t1 || !t2)
	return false;
      if (!same_type_p (TREE_VALUE (t1), TREE_VALUE (t2)))
	return false;
    }
  return true;
}


/* Process a sizeof or alignof expression where the operand is a
   type.  */

tree
cxx_sizeof_or_alignof_type (tree type, enum tree_code op, bool complain)
{
  tree value;
  bool dependent_p;

  gcc_assert (op == SIZEOF_EXPR || op == ALIGNOF_EXPR);
  if (type == error_mark_node)
    return error_mark_node;

  type = non_reference (type);
  if (TREE_CODE (type) == METHOD_TYPE)
    {
      if (complain && (pedantic || warn_pointer_arith))
	pedwarn ("invalid application of %qs to a member function", 
		 operator_name_info[(int) op].name);
      value = size_one_node;
    }

  dependent_p = dependent_type_p (type);
  if (!dependent_p)
    complete_type (type);
  if (dependent_p
      /* VLA types will have a non-constant size.  In the body of an
	 uninstantiated template, we don't need to try to compute the
	 value, because the sizeof expression is not an integral
	 constant expression in that case.  And, if we do try to
	 compute the value, we'll likely end up with SAVE_EXPRs, which
	 the template substitution machinery does not expect to see.  */
      || (processing_template_decl 
	  && COMPLETE_TYPE_P (type)
	  && TREE_CODE (TYPE_SIZE (type)) != INTEGER_CST))
    {
      value = build_min (op, size_type_node, type);
      TREE_READONLY (value) = 1;
      return value;
    }

  return c_sizeof_or_alignof_type (complete_type (type),
				   op == SIZEOF_EXPR,
				   complain);
}

/* Process a sizeof expression where the operand is an expression.  */

static tree
cxx_sizeof_expr (tree e)
{
  if (e == error_mark_node)
    return error_mark_node;

  if (processing_template_decl)
    {
      e = build_min (SIZEOF_EXPR, size_type_node, e);
      TREE_SIDE_EFFECTS (e) = 0;
      TREE_READONLY (e) = 1;

      return e;
    }

  if (TREE_CODE (e) == COMPONENT_REF
      && TREE_CODE (TREE_OPERAND (e, 1)) == FIELD_DECL
      && DECL_C_BIT_FIELD (TREE_OPERAND (e, 1)))
    {
      error ("invalid application of %<sizeof%> to a bit-field");
      e = char_type_node;
    }
  else if (is_overloaded_fn (e))
    {
      pedwarn ("ISO C++ forbids applying %<sizeof%> to an expression of "
	       "function type");
      e = char_type_node;
    }
  else if (type_unknown_p (e))
    {
      cxx_incomplete_type_error (e, TREE_TYPE (e));
      e = char_type_node;
    }
  else
    e = TREE_TYPE (e);

  return cxx_sizeof_or_alignof_type (e, SIZEOF_EXPR, true);
}

/* Implement the __alignof keyword: Return the minimum required
   alignment of E, measured in bytes.  For VAR_DECL's and
   FIELD_DECL's return DECL_ALIGN (which can be set from an
   "aligned" __attribute__ specification).  */

static tree
cxx_alignof_expr (tree e)
{
  tree t;

  if (e == error_mark_node)
    return error_mark_node;

  if (processing_template_decl)
    {
      e = build_min (ALIGNOF_EXPR, size_type_node, e);
      TREE_SIDE_EFFECTS (e) = 0;
      TREE_READONLY (e) = 1;

      return e;
    }

  if (TREE_CODE (e) == VAR_DECL)
    t = size_int (DECL_ALIGN_UNIT (e));
  else if (TREE_CODE (e) == COMPONENT_REF
	   && TREE_CODE (TREE_OPERAND (e, 1)) == FIELD_DECL
	   && DECL_C_BIT_FIELD (TREE_OPERAND (e, 1)))
    {
      error ("invalid application of %<__alignof%> to a bit-field");
      t = size_one_node;
    }
  else if (TREE_CODE (e) == COMPONENT_REF
	   && TREE_CODE (TREE_OPERAND (e, 1)) == FIELD_DECL)
    t = size_int (DECL_ALIGN_UNIT (TREE_OPERAND (e, 1)));
  else if (is_overloaded_fn (e))
    {
      pedwarn ("ISO C++ forbids applying %<__alignof%> to an expression of "
	       "function type");
      t = size_one_node;
    }
  else if (type_unknown_p (e))
    {
      cxx_incomplete_type_error (e, TREE_TYPE (e));
      t = size_one_node;
    }
  else
    return cxx_sizeof_or_alignof_type (TREE_TYPE (e), ALIGNOF_EXPR, true);

  return fold_convert (size_type_node, t);
}

/* Process a sizeof or alignof expression E with code OP where the operand
   is an expression.  */

tree
cxx_sizeof_or_alignof_expr (tree e, enum tree_code op)
{
  if (op == SIZEOF_EXPR)
    return cxx_sizeof_expr (e);
  else
    return cxx_alignof_expr (e);
}

/* EXPR is being used in a context that is not a function call.
   Enforce:

     [expr.ref]

     The expression can be used only as the left-hand operand of a
     member function call.

     [expr.mptr.operator]

     If the result of .* or ->* is a function, then that result can be
     used only as the operand for the function call operator ().

   by issuing an error message if appropriate.  Returns true iff EXPR
   violates these rules.  */

bool
invalid_nonstatic_memfn_p (tree expr)
{
  if (TREE_CODE (TREE_TYPE (expr)) == METHOD_TYPE)
    {
      error ("invalid use of non-static member function");
      return true;
    }
  return false;
}

/* If EXP is a reference to a bitfield, and the type of EXP does not
   match the declared type of the bitfield, return the declared type
   of the bitfield.  Otherwise, return NULL_TREE.  */

tree
is_bitfield_expr_with_lowered_type (tree exp)
{
  switch (TREE_CODE (exp))
    {
    case COND_EXPR:
      if (!is_bitfield_expr_with_lowered_type (TREE_OPERAND (exp, 1)))
	return NULL_TREE;
      return is_bitfield_expr_with_lowered_type (TREE_OPERAND (exp, 2));

    case COMPOUND_EXPR:
      return is_bitfield_expr_with_lowered_type (TREE_OPERAND (exp, 1));

    case MODIFY_EXPR:
    case SAVE_EXPR:
      return is_bitfield_expr_with_lowered_type (TREE_OPERAND (exp, 0));

    case COMPONENT_REF:
      {
	tree field;
	
	field = TREE_OPERAND (exp, 1);
	if (TREE_CODE (field) != FIELD_DECL || !DECL_C_BIT_FIELD (field))
	  return NULL_TREE;
	if (same_type_ignoring_top_level_qualifiers_p
	    (TREE_TYPE (exp), DECL_BIT_FIELD_TYPE (field)))
	  return NULL_TREE;
	return DECL_BIT_FIELD_TYPE (field);
      }

    default:
      return NULL_TREE;
    }
}

/* Like is_bitfield_with_lowered_type, except that if EXP is not a
   bitfield with a lowered type, the type of EXP is returned, rather
   than NULL_TREE.  */

tree
unlowered_expr_type (tree exp)
{
  tree type;

  type = is_bitfield_expr_with_lowered_type (exp);
  if (!type)
    type = TREE_TYPE (exp);

  return type;
}

/* Perform the conversions in [expr] that apply when an lvalue appears
   in an rvalue context: the lvalue-to-rvalue, array-to-pointer, and
   function-to-pointer conversions.  In addition, manifest constants
   are replaced by their values, and bitfield references are converted
   to their declared types.

   Although the returned value is being used as an rvalue, this
   function does not wrap the returned expression in a
   NON_LVALUE_EXPR; the caller is expected to be mindful of the fact
   that the return value is no longer an lvalue.  */

tree
decay_conversion (tree exp)
{
  tree type;
  enum tree_code code;

  type = TREE_TYPE (exp);
  if (type == error_mark_node)
    return error_mark_node;

  if (type_unknown_p (exp))
    {
      cxx_incomplete_type_error (exp, TREE_TYPE (exp));
      return error_mark_node;
    }

  exp = decl_constant_value (exp);
  if (error_operand_p (exp))
    return error_mark_node;

  /* build_c_cast puts on a NOP_EXPR to make the result not an lvalue.
     Leave such NOP_EXPRs, since RHS is being used in non-lvalue context.  */
  code = TREE_CODE (type);
  if (code == VOID_TYPE)
    {
      error ("void value not ignored as it ought to be");
      return error_mark_node;
    }
  if (invalid_nonstatic_memfn_p (exp))
    return error_mark_node;
  if (code == FUNCTION_TYPE || is_overloaded_fn (exp))
    return build_unary_op (ADDR_EXPR, exp, 0);
  if (code == ARRAY_TYPE)
    {
      tree adr;
      tree ptrtype;

      if (TREE_CODE (exp) == INDIRECT_REF)
	return build_nop (build_pointer_type (TREE_TYPE (type)),
			  TREE_OPERAND (exp, 0));

      if (TREE_CODE (exp) == COMPOUND_EXPR)
	{
	  tree op1 = decay_conversion (TREE_OPERAND (exp, 1));
	  return build2 (COMPOUND_EXPR, TREE_TYPE (op1),
			 TREE_OPERAND (exp, 0), op1);
	}

      if (!lvalue_p (exp)
	  && ! (TREE_CODE (exp) == CONSTRUCTOR && TREE_STATIC (exp)))
	{
	  error ("invalid use of non-lvalue array");
	  return error_mark_node;
	}

      ptrtype = build_pointer_type (TREE_TYPE (type));

      if (TREE_CODE (exp) == VAR_DECL)
	{
	  if (!cxx_mark_addressable (exp))
	    return error_mark_node;
	  adr = build_nop (ptrtype, build_address (exp));
	  return adr;
	}
      /* This way is better for a COMPONENT_REF since it can
	 simplify the offset for a component.  */
      adr = build_unary_op (ADDR_EXPR, exp, 1);
      return cp_convert (ptrtype, adr);
    }

  /* If a bitfield is used in a context where integral promotion
     applies, then the caller is expected to have used
     default_conversion.  That function promotes bitfields correctly
     before calling this function.  At this point, if we have a
     bitfield referenced, we may assume that is not subject to
     promotion, and that, therefore, the type of the resulting rvalue
     is the declared type of the bitfield.  */
  exp = convert_bitfield_to_declared_type (exp);

  /* We do not call rvalue() here because we do not want to wrap EXP
     in a NON_LVALUE_EXPR.  */

  /* [basic.lval]

     Non-class rvalues always have cv-unqualified types.  */
  type = TREE_TYPE (exp);
  if (!CLASS_TYPE_P (type) && cp_type_quals (type))
    exp = build_nop (TYPE_MAIN_VARIANT (type), exp);

  return exp;
}

/* Perform prepatory conversions, as part of the "usual arithmetic
   conversions".  In particular, as per [expr]:

     Whenever an lvalue expression appears as an operand of an
     operator that expects the rvalue for that operand, the
     lvalue-to-rvalue, array-to-pointer, or function-to-pointer
     standard conversions are applied to convert the expression to an
     rvalue.

   In addition, we perform integral promotions here, as those are
   applied to both operands to a binary operator before determining
   what additional conversions should apply.  */

tree
default_conversion (tree exp)
{
  /* Perform the integral promotions first so that bitfield
     expressions (which may promote to "int", even if the bitfield is
     declared "unsigned") are promoted correctly.  */
  if (INTEGRAL_OR_ENUMERATION_TYPE_P (TREE_TYPE (exp)))
    exp = perform_integral_promotions (exp);
  /* Perform the other conversions.  */
  exp = decay_conversion (exp);

  return exp;
}

/* EXPR is an expression with an integral or enumeration type.
   Perform the integral promotions in [conv.prom], and return the
   converted value.  */

tree
perform_integral_promotions (tree expr)
{
  tree type;
  tree promoted_type;

  /* [conv.prom]

     If the bitfield has an enumerated type, it is treated as any
     other value of that type for promotion purposes.  */
  type = is_bitfield_expr_with_lowered_type (expr);
  if (!type || TREE_CODE (type) != ENUMERAL_TYPE)
    type = TREE_TYPE (expr);
  gcc_assert (INTEGRAL_OR_ENUMERATION_TYPE_P (type));
  promoted_type = type_promotes_to (type);
  if (type != promoted_type)
    expr = cp_convert (promoted_type, expr);
  return expr;
}

/* Take the address of an inline function without setting TREE_ADDRESSABLE
   or TREE_USED.  */

tree
inline_conversion (tree exp)
{
  if (TREE_CODE (exp) == FUNCTION_DECL)
    exp = build1 (ADDR_EXPR, build_pointer_type (TREE_TYPE (exp)), exp);

  return exp;
}

/* Returns nonzero iff exp is a STRING_CST or the result of applying
   decay_conversion to one.  */

int
string_conv_p (tree totype, tree exp, int warn)
{
  tree t;

  if (TREE_CODE (totype) != POINTER_TYPE)
    return 0;

  t = TREE_TYPE (totype);
  if (!same_type_p (t, char_type_node)
      && !same_type_p (t, wchar_type_node))
    return 0;

  if (TREE_CODE (exp) == STRING_CST)
    {
      /* Make sure that we don't try to convert between char and wchar_t.  */
      if (!same_type_p (TYPE_MAIN_VARIANT (TREE_TYPE (TREE_TYPE (exp))), t))
	return 0;
    }
  else
    {
      /* Is this a string constant which has decayed to 'const char *'?  */
      t = build_pointer_type (build_qualified_type (t, TYPE_QUAL_CONST));
      if (!same_type_p (TREE_TYPE (exp), t))
	return 0;
      STRIP_NOPS (exp);
      if (TREE_CODE (exp) != ADDR_EXPR
	  || TREE_CODE (TREE_OPERAND (exp, 0)) != STRING_CST)
	return 0;
    }

  /* This warning is not very useful, as it complains about printf.  */
  if (warn)
    warning (OPT_Wwrite_strings,
	     "deprecated conversion from string constant to %qT",
	     totype);

  return 1;
}

/* Given a COND_EXPR, MIN_EXPR, or MAX_EXPR in T, return it in a form that we
   can, for example, use as an lvalue.  This code used to be in
   unary_complex_lvalue, but we needed it to deal with `a = (d == c) ? b : c'
   expressions, where we're dealing with aggregates.  But now it's again only
   called from unary_complex_lvalue.  The case (in particular) that led to
   this was with CODE == ADDR_EXPR, since it's not an lvalue when we'd
   get it there.  */

static tree
rationalize_conditional_expr (enum tree_code code, tree t)
{
  /* For MIN_EXPR or MAX_EXPR, fold-const.c has arranged things so that
     the first operand is always the one to be used if both operands
     are equal, so we know what conditional expression this used to be.  */
  if (TREE_CODE (t) == MIN_EXPR || TREE_CODE (t) == MAX_EXPR)
    {
      /* The following code is incorrect if either operand side-effects.  */
      gcc_assert (!TREE_SIDE_EFFECTS (TREE_OPERAND (t, 0))
		  && !TREE_SIDE_EFFECTS (TREE_OPERAND (t, 1)));
      return
	build_conditional_expr (build_x_binary_op ((TREE_CODE (t) == MIN_EXPR
						    ? LE_EXPR : GE_EXPR),
						   TREE_OPERAND (t, 0),
						   TREE_OPERAND (t, 1),
						   /*overloaded_p=*/NULL),
			    build_unary_op (code, TREE_OPERAND (t, 0), 0),
			    build_unary_op (code, TREE_OPERAND (t, 1), 0));
    }

  return
    build_conditional_expr (TREE_OPERAND (t, 0),
			    build_unary_op (code, TREE_OPERAND (t, 1), 0),
			    build_unary_op (code, TREE_OPERAND (t, 2), 0));
}

/* Given the TYPE of an anonymous union field inside T, return the
   FIELD_DECL for the field.  If not found return NULL_TREE.  Because
   anonymous unions can nest, we must also search all anonymous unions
   that are directly reachable.  */

tree
lookup_anon_field (tree t, tree type)
{
  tree field;

  for (field = TYPE_FIELDS (t); field; field = TREE_CHAIN (field))
    {
      if (TREE_STATIC (field))
	continue;
      if (TREE_CODE (field) != FIELD_DECL || DECL_ARTIFICIAL (field))
	continue;

      /* If we find it directly, return the field.  */
      if (DECL_NAME (field) == NULL_TREE
	  && type == TYPE_MAIN_VARIANT (TREE_TYPE (field)))
	{
	  return field;
	}

      /* Otherwise, it could be nested, search harder.  */
      if (DECL_NAME (field) == NULL_TREE
	  && ANON_AGGR_TYPE_P (TREE_TYPE (field)))
	{
	  tree subfield = lookup_anon_field (TREE_TYPE (field), type);
	  if (subfield)
	    return subfield;
	}
    }
  return NULL_TREE;
}

/* Build an expression representing OBJECT.MEMBER.  OBJECT is an
   expression; MEMBER is a DECL or baselink.  If ACCESS_PATH is
   non-NULL, it indicates the path to the base used to name MEMBER.
   If PRESERVE_REFERENCE is true, the expression returned will have
   REFERENCE_TYPE if the MEMBER does.  Otherwise, the expression
   returned will have the type referred to by the reference.

   This function does not perform access control; that is either done
   earlier by the parser when the name of MEMBER is resolved to MEMBER
   itself, or later when overload resolution selects one of the
   functions indicated by MEMBER.  */

tree
build_class_member_access_expr (tree object, tree member,
				tree access_path, bool preserve_reference)
{
  tree object_type;
  tree member_scope;
  tree result = NULL_TREE;

  if (error_operand_p (object) || error_operand_p (member))
    return error_mark_node;

  gcc_assert (DECL_P (member) || BASELINK_P (member));

  /* [expr.ref]

     The type of the first expression shall be "class object" (of a
     complete type).  */
  object_type = TREE_TYPE (object);
  if (!currently_open_class (object_type)
      && !complete_type_or_else (object_type, object))
    return error_mark_node;
  if (!CLASS_TYPE_P (object_type))
    {
      error ("request for member %qD in %qE, which is of non-class type %qT",
	     member, object, object_type);
      return error_mark_node;
    }

  /* The standard does not seem to actually say that MEMBER must be a
     member of OBJECT_TYPE.  However, that is clearly what is
     intended.  */
  if (DECL_P (member))
    {
      member_scope = DECL_CLASS_CONTEXT (member);
      mark_used (member);
      if (TREE_DEPRECATED (member))
	warn_deprecated_use (member);
    }
  else
    member_scope = BINFO_TYPE (BASELINK_BINFO (member));
  /* If MEMBER is from an anonymous aggregate, MEMBER_SCOPE will
     presently be the anonymous union.  Go outwards until we find a
     type related to OBJECT_TYPE.  */
  while (ANON_AGGR_TYPE_P (member_scope)
	 && !same_type_ignoring_top_level_qualifiers_p (member_scope,
							object_type))
    member_scope = TYPE_CONTEXT (member_scope);
  if (!member_scope || !DERIVED_FROM_P (member_scope, object_type))
    {
      if (TREE_CODE (member) == FIELD_DECL)
	error ("invalid use of nonstatic data member %qE", member);
      else
	error ("%qD is not a member of %qT", member, object_type);
      return error_mark_node;
    }

  /* Transform `(a, b).x' into `(*(a, &b)).x', `(a ? b : c).x' into
     `(*(a ?  &b : &c)).x', and so on.  A COND_EXPR is only an lvalue
     in the frontend; only _DECLs and _REFs are lvalues in the backend.  */
  {
    tree temp = unary_complex_lvalue (ADDR_EXPR, object);
    if (temp)
      object = build_indirect_ref (temp, NULL);
  }

  /* In [expr.ref], there is an explicit list of the valid choices for
     MEMBER.  We check for each of those cases here.  */
  if (TREE_CODE (member) == VAR_DECL)
    {
      /* A static data member.  */
      result = member;
      /* If OBJECT has side-effects, they are supposed to occur.  */
      if (TREE_SIDE_EFFECTS (object))
	result = build2 (COMPOUND_EXPR, TREE_TYPE (result), object, result);
    }
  else if (TREE_CODE (member) == FIELD_DECL)
    {
      /* A non-static data member.  */
      bool null_object_p;
      int type_quals;
      tree member_type;

      null_object_p = (TREE_CODE (object) == INDIRECT_REF
		       && integer_zerop (TREE_OPERAND (object, 0)));

      /* Convert OBJECT to the type of MEMBER.  */
      if (!same_type_p (TYPE_MAIN_VARIANT (object_type),
			TYPE_MAIN_VARIANT (member_scope)))
	{
	  tree binfo;
	  base_kind kind;

	  binfo = lookup_base (access_path ? access_path : object_type,
			       member_scope, ba_unique,  &kind);
	  if (binfo == error_mark_node)
	    return error_mark_node;

	  /* It is invalid to try to get to a virtual base of a
	     NULL object.  The most common cause is invalid use of
	     offsetof macro.  */
	  if (null_object_p && kind == bk_via_virtual)
	    {
	      error ("invalid access to non-static data member %qD of "
		     "NULL object",
		     member);
	      error ("(perhaps the %<offsetof%> macro was used incorrectly)");
	      return error_mark_node;
	    }

	  /* Convert to the base.  */
	  object = build_base_path (PLUS_EXPR, object, binfo,
				    /*nonnull=*/1);
	  /* If we found the base successfully then we should be able
	     to convert to it successfully.  */
	  gcc_assert (object != error_mark_node);
	}

      /* Complain about other invalid uses of offsetof, even though they will
	 give the right answer.  Note that we complain whether or not they
	 actually used the offsetof macro, since there's no way to know at this
	 point.  So we just give a warning, instead of a pedwarn.  */
      /* Do not produce this warning for base class field references, because
	 we know for a fact that didn't come from offsetof.  This does occur
	 in various testsuite cases where a null object is passed where a
	 vtable access is required.  */
      if (null_object_p && warn_invalid_offsetof
	  && CLASSTYPE_NON_POD_P (object_type)
	  && !DECL_FIELD_IS_BASE (member)
	  && !skip_evaluation)
	{
	  warning (0, "invalid access to non-static data member %qD of NULL object",
		   member);
	  warning (0, "(perhaps the %<offsetof%> macro was used incorrectly)");
	}

      /* If MEMBER is from an anonymous aggregate, we have converted
	 OBJECT so that it refers to the class containing the
	 anonymous union.  Generate a reference to the anonymous union
	 itself, and recur to find MEMBER.  */
      if (ANON_AGGR_TYPE_P (DECL_CONTEXT (member))
	  /* When this code is called from build_field_call, the
	     object already has the type of the anonymous union.
	     That is because the COMPONENT_REF was already
	     constructed, and was then disassembled before calling
	     build_field_call.  After the function-call code is
	     cleaned up, this waste can be eliminated.  */
	  && (!same_type_ignoring_top_level_qualifiers_p
	      (TREE_TYPE (object), DECL_CONTEXT (member))))
	{
	  tree anonymous_union;

	  anonymous_union = lookup_anon_field (TREE_TYPE (object),
					       DECL_CONTEXT (member));
	  object = build_class_member_access_expr (object,
						   anonymous_union,
						   /*access_path=*/NULL_TREE,
						   preserve_reference);
	}

      /* Compute the type of the field, as described in [expr.ref].  */
      type_quals = TYPE_UNQUALIFIED;
      member_type = TREE_TYPE (member);
      if (TREE_CODE (member_type) != REFERENCE_TYPE)
	{
	  type_quals = (cp_type_quals (member_type)
			| cp_type_quals (object_type));

	  /* A field is const (volatile) if the enclosing object, or the
	     field itself, is const (volatile).  But, a mutable field is
	     not const, even within a const object.  */
	  if (DECL_MUTABLE_P (member))
	    type_quals &= ~TYPE_QUAL_CONST;
	  member_type = cp_build_qualified_type (member_type, type_quals);
	}

      result = build3 (COMPONENT_REF, member_type, object, member,
		       NULL_TREE);
      result = fold_if_not_in_template (result);

      /* Mark the expression const or volatile, as appropriate.  Even
	 though we've dealt with the type above, we still have to mark the
	 expression itself.  */
      if (type_quals & TYPE_QUAL_CONST)
	TREE_READONLY (result) = 1;
      if (type_quals & TYPE_QUAL_VOLATILE)
	TREE_THIS_VOLATILE (result) = 1;
    }
  else if (BASELINK_P (member))
    {
      /* The member is a (possibly overloaded) member function.  */
      tree functions;
      tree type;

      /* If the MEMBER is exactly one static member function, then we
	 know the type of the expression.  Otherwise, we must wait
	 until overload resolution has been performed.  */
      functions = BASELINK_FUNCTIONS (member);
      if (TREE_CODE (functions) == FUNCTION_DECL
	  && DECL_STATIC_FUNCTION_P (functions))
	type = TREE_TYPE (functions);
      else
	type = unknown_type_node;
      /* Note that we do not convert OBJECT to the BASELINK_BINFO
	 base.  That will happen when the function is called.  */
      result = build3 (COMPONENT_REF, type, object, member, NULL_TREE);
    }
  else if (TREE_CODE (member) == CONST_DECL)
    {
      /* The member is an enumerator.  */
      result = member;
      /* If OBJECT has side-effects, they are supposed to occur.  */
      if (TREE_SIDE_EFFECTS (object))
	result = build2 (COMPOUND_EXPR, TREE_TYPE (result),
			 object, result);
    }
  else
    {
      error ("invalid use of %qD", member);
      return error_mark_node;
    }

  if (!preserve_reference)
    /* [expr.ref]

       If E2 is declared to have type "reference to T", then ... the
       type of E1.E2 is T.  */
    result = convert_from_reference (result);

  return result;
}

/* Return the destructor denoted by OBJECT.SCOPE::~DTOR_NAME, or, if
   SCOPE is NULL, by OBJECT.~DTOR_NAME.  */

static tree
lookup_destructor (tree object, tree scope, tree dtor_name)
{
  tree object_type = TREE_TYPE (object);
  tree dtor_type = TREE_OPERAND (dtor_name, 0);
  tree expr;

  if (scope && !check_dtor_name (scope, dtor_type))
    {
      error ("qualified type %qT does not match destructor name ~%qT",
	     scope, dtor_type);
      return error_mark_node;
    }
  if (!DERIVED_FROM_P (dtor_type, TYPE_MAIN_VARIANT (object_type)))
    {
      error ("the type being destroyed is %qT, but the destructor refers to %qT",
	     TYPE_MAIN_VARIANT (object_type), dtor_type);
      return error_mark_node;
    }
  expr = lookup_member (dtor_type, complete_dtor_identifier,
			/*protect=*/1, /*want_type=*/false);
  expr = (adjust_result_of_qualified_name_lookup
	  (expr, dtor_type, object_type));
  return expr;
}

/* An expression of the form "A::template B" has been resolved to
   DECL.  Issue a diagnostic if B is not a template or template
   specialization.  */

void
check_template_keyword (tree decl)
{
  /* The standard says:

      [temp.names]

      If a name prefixed by the keyword template is not a member
      template, the program is ill-formed.

     DR 228 removed the restriction that the template be a member
     template.

     DR 96, if accepted would add the further restriction that explicit
     template arguments must be provided if the template keyword is
     used, but, as of 2005-10-16, that DR is still in "drafting".  If
     this DR is accepted, then the semantic checks here can be
     simplified, as the entity named must in fact be a template
     specialization, rather than, as at present, a set of overloaded
     functions containing at least one template function.  */
  if (TREE_CODE (decl) != TEMPLATE_DECL
      && TREE_CODE (decl) != TEMPLATE_ID_EXPR)
    {
      if (!is_overloaded_fn (decl))
	pedwarn ("%qD is not a template", decl);
      else
	{
	  tree fns;
	  fns = decl;
	  if (BASELINK_P (fns))
	    fns = BASELINK_FUNCTIONS (fns);
	  while (fns)
	    {
	      tree fn = OVL_CURRENT (fns);
	      if (TREE_CODE (fn) == TEMPLATE_DECL
		  || TREE_CODE (fn) == TEMPLATE_ID_EXPR)
		break;
	      if (TREE_CODE (fn) == FUNCTION_DECL
		  && DECL_USE_TEMPLATE (fn)
		  && PRIMARY_TEMPLATE_P (DECL_TI_TEMPLATE (fn)))
		break;
	      fns = OVL_NEXT (fns);
	    }
	  if (!fns)
	    pedwarn ("%qD is not a template", decl);
	}
    }
}

/* This function is called by the parser to process a class member
   access expression of the form OBJECT.NAME.  NAME is a node used by
   the parser to represent a name; it is not yet a DECL.  It may,
   however, be a BASELINK where the BASELINK_FUNCTIONS is a
   TEMPLATE_ID_EXPR.  Templates must be looked up by the parser, and
   there is no reason to do the lookup twice, so the parser keeps the
   BASELINK.  TEMPLATE_P is true iff NAME was explicitly declared to
   be a template via the use of the "A::template B" syntax.  */

tree
finish_class_member_access_expr (tree object, tree name, bool template_p)
{
  tree expr;
  tree object_type;
  tree member;
  tree access_path = NULL_TREE;
  tree orig_object = object;
  tree orig_name = name;

  if (object == error_mark_node || name == error_mark_node)
    return error_mark_node;

  /* If OBJECT is an ObjC class instance, we must obey ObjC access rules.  */
  if (!objc_is_public (object, name))
    return error_mark_node;

  object_type = TREE_TYPE (object);

  if (processing_template_decl)
    {
      if (/* If OBJECT_TYPE is dependent, so is OBJECT.NAME.  */
	  dependent_type_p (object_type)
	  /* If NAME is just an IDENTIFIER_NODE, then the expression
	     is dependent.  */
	  || TREE_CODE (object) == IDENTIFIER_NODE
	  /* If NAME is "f<args>", where either 'f' or 'args' is
	     dependent, then the expression is dependent.  */
	  || (TREE_CODE (name) == TEMPLATE_ID_EXPR
	      && dependent_template_id_p (TREE_OPERAND (name, 0),
					  TREE_OPERAND (name, 1)))
	  /* If NAME is "T::X" where "T" is dependent, then the
	     expression is dependent.  */
	  || (TREE_CODE (name) == SCOPE_REF
	      && TYPE_P (TREE_OPERAND (name, 0))
	      && dependent_type_p (TREE_OPERAND (name, 0))))
	return build_min_nt (COMPONENT_REF, object, name, NULL_TREE);
      object = build_non_dependent_expr (object);
    }

  /* [expr.ref]

     The type of the first expression shall be "class object" (of a
     complete type).  */
  if (!currently_open_class (object_type)
      && !complete_type_or_else (object_type, object))
    return error_mark_node;
  if (!CLASS_TYPE_P (object_type))
    {
      error ("request for member %qD in %qE, which is of non-class type %qT",
	     name, object, object_type);
      return error_mark_node;
    }

  if (BASELINK_P (name))
    /* A member function that has already been looked up.  */
    member = name;
  else
    {
      bool is_template_id = false;
      tree template_args = NULL_TREE;
      tree scope;

      if (TREE_CODE (name) == TEMPLATE_ID_EXPR)
	{
	  is_template_id = true;
	  template_args = TREE_OPERAND (name, 1);
	  name = TREE_OPERAND (name, 0);

	  if (TREE_CODE (name) == OVERLOAD)
	    name = DECL_NAME (get_first_fn (name));
	  else if (DECL_P (name))
	    name = DECL_NAME (name);
	}

      if (TREE_CODE (name) == SCOPE_REF)
	{
	  /* A qualified name.  The qualifying class or namespace `S'
	     has already been looked up; it is either a TYPE or a
	     NAMESPACE_DECL.  */
	  scope = TREE_OPERAND (name, 0);
	  name = TREE_OPERAND (name, 1);

	  /* If SCOPE is a namespace, then the qualified name does not
	     name a member of OBJECT_TYPE.  */
	  if (TREE_CODE (scope) == NAMESPACE_DECL)
	    {
	      error ("%<%D::%D%> is not a member of %qT",
		     scope, name, object_type);
	      return error_mark_node;
	    }

	  gcc_assert (CLASS_TYPE_P (scope));
	  gcc_assert (TREE_CODE (name) == IDENTIFIER_NODE
		      || TREE_CODE (name) == BIT_NOT_EXPR);

	  /* Find the base of OBJECT_TYPE corresponding to SCOPE.  */
	  access_path = lookup_base (object_type, scope, ba_check, NULL);
	  if (access_path == error_mark_node)
	    return error_mark_node;
	  if (!access_path)
	    {
	      error ("%qT is not a base of %qT", scope, object_type);
	      return error_mark_node;
	    }
	}
      else
	{
	  scope = NULL_TREE;
	  access_path = object_type;
	}

      if (TREE_CODE (name) == BIT_NOT_EXPR)
	member = lookup_destructor (object, scope, name);
      else
	{
	  /* Look up the member.  */
	  member = lookup_member (access_path, name, /*protect=*/1,
				  /*want_type=*/false);
	  if (member == NULL_TREE)
	    {
	      error ("%qD has no member named %qE", object_type, name);
	      return error_mark_node;
	    }
	  if (member == error_mark_node)
	    return error_mark_node;
	}

      if (is_template_id)
	{
	  tree template = member;

	  if (BASELINK_P (template))
	    template = lookup_template_function (template, template_args);
	  else
	    {
	      error ("%qD is not a member template function", name);
	      return error_mark_node;
	    }
	}
    }

  if (TREE_DEPRECATED (member))
    warn_deprecated_use (member);

  if (template_p)
    check_template_keyword (member);

  expr = build_class_member_access_expr (object, member, access_path,
					 /*preserve_reference=*/false);
  if (processing_template_decl && expr != error_mark_node)
    {
      if (BASELINK_P (member))
	{
	  if (TREE_CODE (orig_name) == SCOPE_REF)
	    BASELINK_QUALIFIED_P (member) = 1;
	  orig_name = member;
	}
      return build_min_non_dep (COMPONENT_REF, expr,
				orig_object, orig_name,
				NULL_TREE);
    }

  return expr;
}

/* Return an expression for the MEMBER_NAME field in the internal
   representation of PTRMEM, a pointer-to-member function.  (Each
   pointer-to-member function type gets its own RECORD_TYPE so it is
   more convenient to access the fields by name than by FIELD_DECL.)
   This routine converts the NAME to a FIELD_DECL and then creates the
   node for the complete expression.  */

tree
build_ptrmemfunc_access_expr (tree ptrmem, tree member_name)
{
  tree ptrmem_type;
  tree member;
  tree member_type;

  /* This code is a stripped down version of
     build_class_member_access_expr.  It does not work to use that
     routine directly because it expects the object to be of class
     type.  */
  ptrmem_type = TREE_TYPE (ptrmem);
  gcc_assert (TYPE_PTRMEMFUNC_P (ptrmem_type));
  member = lookup_member (ptrmem_type, member_name, /*protect=*/0,
			  /*want_type=*/false);
  member_type = cp_build_qualified_type (TREE_TYPE (member),
					 cp_type_quals (ptrmem_type));
  return fold_build3 (COMPONENT_REF, member_type,
		      ptrmem, member, NULL_TREE);
}

/* Given an expression PTR for a pointer, return an expression
   for the value pointed to.
   ERRORSTRING is the name of the operator to appear in error messages.

   This function may need to overload OPERATOR_FNNAME.
   Must also handle REFERENCE_TYPEs for C++.  */

tree
build_x_indirect_ref (tree expr, const char *errorstring)
{
  tree orig_expr = expr;
  tree rval;

  if (processing_template_decl)
    {
      if (type_dependent_expression_p (expr))
	return build_min_nt (INDIRECT_REF, expr);
      expr = build_non_dependent_expr (expr);
    }

  rval = build_new_op (INDIRECT_REF, LOOKUP_NORMAL, expr, NULL_TREE,
		       NULL_TREE, /*overloaded_p=*/NULL);
  if (!rval)
    rval = build_indirect_ref (expr, errorstring);

  if (processing_template_decl && rval != error_mark_node)
    return build_min_non_dep (INDIRECT_REF, rval, orig_expr);
  else
    return rval;
}

tree
build_indirect_ref (tree ptr, const char *errorstring)
{
  tree pointer, type;

  if (ptr == error_mark_node)
    return error_mark_node;

  if (ptr == current_class_ptr)
    return current_class_ref;

  pointer = (TREE_CODE (TREE_TYPE (ptr)) == REFERENCE_TYPE
	     ? ptr : decay_conversion (ptr));
  type = TREE_TYPE (pointer);

  if (POINTER_TYPE_P (type))
    {
      /* [expr.unary.op]

	 If the type of the expression is "pointer to T," the type
	 of  the  result  is  "T."

	 We must use the canonical variant because certain parts of
	 the back end, like fold, do pointer comparisons between
	 types.  */
      tree t = canonical_type_variant (TREE_TYPE (type));

      if (VOID_TYPE_P (t))
	{
	  /* A pointer to incomplete type (other than cv void) can be
	     dereferenced [expr.unary.op]/1  */
	  error ("%qT is not a pointer-to-object type", type);
	  return error_mark_node;
	}
      else if (TREE_CODE (pointer) == ADDR_EXPR
	       && same_type_p (t, TREE_TYPE (TREE_OPERAND (pointer, 0))))
	/* The POINTER was something like `&x'.  We simplify `*&x' to
	   `x'.  */
	return TREE_OPERAND (pointer, 0);
      else
	{
	  tree ref = build1 (INDIRECT_REF, t, pointer);

	  /* We *must* set TREE_READONLY when dereferencing a pointer to const,
	     so that we get the proper error message if the result is used
	     to assign to.  Also, &* is supposed to be a no-op.  */
	  TREE_READONLY (ref) = CP_TYPE_CONST_P (t);
	  TREE_THIS_VOLATILE (ref) = CP_TYPE_VOLATILE_P (t);
	  TREE_SIDE_EFFECTS (ref)
	    = (TREE_THIS_VOLATILE (ref) || TREE_SIDE_EFFECTS (pointer));
	  return ref;
	}
    }
  /* `pointer' won't be an error_mark_node if we were given a
     pointer to member, so it's cool to check for this here.  */
  else if (TYPE_PTR_TO_MEMBER_P (type))
    error ("invalid use of %qs on pointer to member", errorstring);
  else if (pointer != error_mark_node)
    {
      if (errorstring)
	error ("invalid type argument of %qs", errorstring);
      else
	error ("invalid type argument");
    }
  return error_mark_node;
}

/* This handles expressions of the form "a[i]", which denotes
   an array reference.

   This is logically equivalent in C to *(a+i), but we may do it differently.
   If A is a variable or a member, we generate a primitive ARRAY_REF.
   This avoids forcing the array out of registers, and can work on
   arrays that are not lvalues (for example, members of structures returned
   by functions).

   If INDEX is of some user-defined type, it must be converted to
   integer type.  Otherwise, to make a compatible PLUS_EXPR, it
   will inherit the type of the array, which will be some pointer type.  */

tree
build_array_ref (tree array, tree idx)
{
  if (idx == 0)
    {
      error ("subscript missing in array reference");
      return error_mark_node;
    }

  if (TREE_TYPE (array) == error_mark_node
      || TREE_TYPE (idx) == error_mark_node)
    return error_mark_node;

  /* If ARRAY is a COMPOUND_EXPR or COND_EXPR, move our reference
     inside it.  */
  switch (TREE_CODE (array))
    {
    case COMPOUND_EXPR:
      {
	tree value = build_array_ref (TREE_OPERAND (array, 1), idx);
	return build2 (COMPOUND_EXPR, TREE_TYPE (value),
		       TREE_OPERAND (array, 0), value);
      }

    case COND_EXPR:
      return build_conditional_expr
	(TREE_OPERAND (array, 0),
	 build_array_ref (TREE_OPERAND (array, 1), idx),
	 build_array_ref (TREE_OPERAND (array, 2), idx));

    default:
      break;
    }

  if (TREE_CODE (TREE_TYPE (array)) == ARRAY_TYPE)
    {
      tree rval, type;

      warn_array_subscript_with_type_char (idx);

      if (!INTEGRAL_OR_ENUMERATION_TYPE_P (TREE_TYPE (idx)))
	{
	  error ("array subscript is not an integer");
	  return error_mark_node;
	}

      /* Apply integral promotions *after* noticing character types.
	 (It is unclear why we do these promotions -- the standard
	 does not say that we should.  In fact, the natural thing would
	 seem to be to convert IDX to ptrdiff_t; we're performing
	 pointer arithmetic.)  */
      idx = perform_integral_promotions (idx);

      /* An array that is indexed by a non-constant
	 cannot be stored in a register; we must be able to do
	 address arithmetic on its address.
	 Likewise an array of elements of variable size.  */
      if (TREE_CODE (idx) != INTEGER_CST
	  || (COMPLETE_TYPE_P (TREE_TYPE (TREE_TYPE (array)))
	      && (TREE_CODE (TYPE_SIZE (TREE_TYPE (TREE_TYPE (array))))
		  != INTEGER_CST)))
	{
	  if (!cxx_mark_addressable (array))
	    return error_mark_node;
	}

      /* An array that is indexed by a constant value which is not within
	 the array bounds cannot be stored in a register either; because we
	 would get a crash in store_bit_field/extract_bit_field when trying
	 to access a non-existent part of the register.  */
      if (TREE_CODE (idx) == INTEGER_CST
	  && TYPE_DOMAIN (TREE_TYPE (array))
	  && ! int_fits_type_p (idx, TYPE_DOMAIN (TREE_TYPE (array))))
	{
	  if (!cxx_mark_addressable (array))
	    return error_mark_node;
	}

      if (pedantic && !lvalue_p (array))
	pedwarn ("ISO C++ forbids subscripting non-lvalue array");

      /* Note in C++ it is valid to subscript a `register' array, since
	 it is valid to take the address of something with that
	 storage specification.  */
      if (extra_warnings)
	{
	  tree foo = array;
	  while (TREE_CODE (foo) == COMPONENT_REF)
	    foo = TREE_OPERAND (foo, 0);
	  if (TREE_CODE (foo) == VAR_DECL && DECL_REGISTER (foo))
	    warning (OPT_Wextra, "subscripting array declared %<register%>");
	}

      type = TREE_TYPE (TREE_TYPE (array));
      rval = build4 (ARRAY_REF, type, array, idx, NULL_TREE, NULL_TREE);
      /* Array ref is const/volatile if the array elements are
	 or if the array is..  */
      TREE_READONLY (rval)
	|= (CP_TYPE_CONST_P (type) | TREE_READONLY (array));
      TREE_SIDE_EFFECTS (rval)
	|= (CP_TYPE_VOLATILE_P (type) | TREE_SIDE_EFFECTS (array));
      TREE_THIS_VOLATILE (rval)
	|= (CP_TYPE_VOLATILE_P (type) | TREE_THIS_VOLATILE (array));
      return require_complete_type (fold_if_not_in_template (rval));
    }

  {
    tree ar = default_conversion (array);
    tree ind = default_conversion (idx);

    /* Put the integer in IND to simplify error checking.  */
    if (TREE_CODE (TREE_TYPE (ar)) == INTEGER_TYPE)
      {
	tree temp = ar;
	ar = ind;
	ind = temp;
      }

    if (ar == error_mark_node)
      return ar;

    if (TREE_CODE (TREE_TYPE (ar)) != POINTER_TYPE)
      {
	error ("subscripted value is neither array nor pointer");
	return error_mark_node;
      }
    if (TREE_CODE (TREE_TYPE (ind)) != INTEGER_TYPE)
      {
	error ("array subscript is not an integer");
	return error_mark_node;
      }

    return build_indirect_ref (cp_build_binary_op (PLUS_EXPR, ar, ind),
			       "array indexing");
  }
}

/* Resolve a pointer to member function.  INSTANCE is the object
   instance to use, if the member points to a virtual member.

   This used to avoid checking for virtual functions if basetype
   has no virtual functions, according to an earlier ANSI draft.
   With the final ISO C++ rules, such an optimization is
   incorrect: A pointer to a derived member can be static_cast
   to pointer-to-base-member, as long as the dynamic object
   later has the right member.  */

tree
get_member_function_from_ptrfunc (tree *instance_ptrptr, tree function)
{
  if (TREE_CODE (function) == OFFSET_REF)
    function = TREE_OPERAND (function, 1);

  if (TYPE_PTRMEMFUNC_P (TREE_TYPE (function)))
    {
      tree idx, delta, e1, e2, e3, vtbl, basetype;
      tree fntype = TYPE_PTRMEMFUNC_FN_TYPE (TREE_TYPE (function));

      tree instance_ptr = *instance_ptrptr;
      tree instance_save_expr = 0;
      if (instance_ptr == error_mark_node)
	{
	  if (TREE_CODE (function) == PTRMEM_CST)
	    {
	      /* Extracting the function address from a pmf is only
		 allowed with -Wno-pmf-conversions. It only works for
		 pmf constants.  */
	      e1 = build_addr_func (PTRMEM_CST_MEMBER (function));
	      e1 = convert (fntype, e1);
	      return e1;
	    }
	  else
	    {
	      error ("object missing in use of %qE", function);
	      return error_mark_node;
	    }
	}

      if (TREE_SIDE_EFFECTS (instance_ptr))
	instance_ptr = instance_save_expr = save_expr (instance_ptr);

      if (TREE_SIDE_EFFECTS (function))
	function = save_expr (function);

      /* Start by extracting all the information from the PMF itself.  */
      e3 = pfn_from_ptrmemfunc (function);
      delta = build_ptrmemfunc_access_expr (function, delta_identifier);
      idx = build1 (NOP_EXPR, vtable_index_type, e3);
      switch (TARGET_PTRMEMFUNC_VBIT_LOCATION)
	{
	case ptrmemfunc_vbit_in_pfn:
	  e1 = cp_build_binary_op (BIT_AND_EXPR, idx, integer_one_node);
	  idx = cp_build_binary_op (MINUS_EXPR, idx, integer_one_node);
	  break;

	case ptrmemfunc_vbit_in_delta:
	  e1 = cp_build_binary_op (BIT_AND_EXPR, delta, integer_one_node);
	  delta = cp_build_binary_op (RSHIFT_EXPR, delta, integer_one_node);
	  break;

	default:
	  gcc_unreachable ();
	}

      /* Convert down to the right base before using the instance.  A
	 special case is that in a pointer to member of class C, C may
	 be incomplete.  In that case, the function will of course be
	 a member of C, and no conversion is required.  In fact,
	 lookup_base will fail in that case, because incomplete
	 classes do not have BINFOs.  */
      basetype = TYPE_METHOD_BASETYPE (TREE_TYPE (fntype));
      if (!same_type_ignoring_top_level_qualifiers_p
	  (basetype, TREE_TYPE (TREE_TYPE (instance_ptr))))
	{
	  basetype = lookup_base (TREE_TYPE (TREE_TYPE (instance_ptr)),
				  basetype, ba_check, NULL);
	  instance_ptr = build_base_path (PLUS_EXPR, instance_ptr, basetype,
					  1);
	  if (instance_ptr == error_mark_node)
	    return error_mark_node;
	}
      /* ...and then the delta in the PMF.  */
      instance_ptr = build2 (PLUS_EXPR, TREE_TYPE (instance_ptr),
			     instance_ptr, delta);

      /* Hand back the adjusted 'this' argument to our caller.  */
      *instance_ptrptr = instance_ptr;

      /* Next extract the vtable pointer from the object.  */
      vtbl = build1 (NOP_EXPR, build_pointer_type (vtbl_ptr_type_node),
		     instance_ptr);
      vtbl = build_indirect_ref (vtbl, NULL);

      /* Finally, extract the function pointer from the vtable.  */
      e2 = fold_build2 (PLUS_EXPR, TREE_TYPE (vtbl), vtbl, idx);
      e2 = build_indirect_ref (e2, NULL);
      TREE_CONSTANT (e2) = 1;
      TREE_INVARIANT (e2) = 1;

      /* When using function descriptors, the address of the
	 vtable entry is treated as a function pointer.  */
      if (TARGET_VTABLE_USES_DESCRIPTORS)
	e2 = build1 (NOP_EXPR, TREE_TYPE (e2),
		     build_unary_op (ADDR_EXPR, e2, /*noconvert=*/1));

      TREE_TYPE (e2) = TREE_TYPE (e3);
      e1 = build_conditional_expr (e1, e2, e3);

      /* Make sure this doesn't get evaluated first inside one of the
	 branches of the COND_EXPR.  */
      if (instance_save_expr)
	e1 = build2 (COMPOUND_EXPR, TREE_TYPE (e1),
		     instance_save_expr, e1);

      function = e1;
    }
  return function;
}

tree
build_function_call (tree function, tree params)
{
  tree fntype, fndecl;
  tree coerced_params;
  tree name = NULL_TREE;
  int is_method;
  tree original = function;

  /* For Objective-C, convert any calls via a cast to OBJC_TYPE_REF
     expressions, like those used for ObjC messenger dispatches.  */
  function = objc_rewrite_function_call (function, params);

  /* build_c_cast puts on a NOP_EXPR to make the result not an lvalue.
     Strip such NOP_EXPRs, since FUNCTION is used in non-lvalue context.  */
  if (TREE_CODE (function) == NOP_EXPR
      && TREE_TYPE (function) == TREE_TYPE (TREE_OPERAND (function, 0)))
    function = TREE_OPERAND (function, 0);

  if (TREE_CODE (function) == FUNCTION_DECL)
    {
      name = DECL_NAME (function);

      mark_used (function);
      fndecl = function;

      /* Convert anything with function type to a pointer-to-function.  */
      if (pedantic && DECL_MAIN_P (function))
	pedwarn ("ISO C++ forbids calling %<::main%> from within program");

      /* Differs from default_conversion by not setting TREE_ADDRESSABLE
	 (because calling an inline function does not mean the function
	 needs to be separately compiled).  */

      if (DECL_INLINE (function))
	function = inline_conversion (function);
      else
	function = build_addr_func (function);
    }
  else
    {
      fndecl = NULL_TREE;

      function = build_addr_func (function);
    }

  if (function == error_mark_node)
    return error_mark_node;

  fntype = TREE_TYPE (function);

  if (TYPE_PTRMEMFUNC_P (fntype))
    {
      error ("must use %<.*%> or %<->*%> to call pointer-to-member "
	     "function in %<%E (...)%>",
	     original);
      return error_mark_node;
    }

  is_method = (TREE_CODE (fntype) == POINTER_TYPE
	       && TREE_CODE (TREE_TYPE (fntype)) == METHOD_TYPE);

  if (!((TREE_CODE (fntype) == POINTER_TYPE
	 && TREE_CODE (TREE_TYPE (fntype)) == FUNCTION_TYPE)
	|| is_method
	|| TREE_CODE (function) == TEMPLATE_ID_EXPR))
    {
      error ("%qE cannot be used as a function", original);
      return error_mark_node;
    }

  /* fntype now gets the type of function pointed to.  */
  fntype = TREE_TYPE (fntype);

  /* Convert the parameters to the types declared in the
     function prototype, or apply default promotions.  */

  coerced_params = convert_arguments (TYPE_ARG_TYPES (fntype),
				      params, fndecl, LOOKUP_NORMAL);
  if (coerced_params == error_mark_node)
    return error_mark_node;

  /* Check for errors in format strings and inappropriately
     null parameters.  */

  check_function_arguments (TYPE_ATTRIBUTES (fntype), coerced_params,
			    TYPE_ARG_TYPES (fntype));

  return build_cxx_call (function, coerced_params);
}

/* Convert the actual parameter expressions in the list VALUES
   to the types in the list TYPELIST.
   If parmdecls is exhausted, or when an element has NULL as its type,
   perform the default conversions.

   NAME is an IDENTIFIER_NODE or 0.  It is used only for error messages.

   This is also where warnings about wrong number of args are generated.

   Return a list of expressions for the parameters as converted.

   Both VALUES and the returned value are chains of TREE_LIST nodes
   with the elements of the list in the TREE_VALUE slots of those nodes.

   In C++, unspecified trailing parameters can be filled in with their
   default arguments, if such were specified.  Do so here.  */

static tree
convert_arguments (tree typelist, tree values, tree fndecl, int flags)
{
  tree typetail, valtail;
  tree result = NULL_TREE;
  const char *called_thing = 0;
  int i = 0;

  /* Argument passing is always copy-initialization.  */
  flags |= LOOKUP_ONLYCONVERTING;

  if (fndecl)
    {
      if (TREE_CODE (TREE_TYPE (fndecl)) == METHOD_TYPE)
	{
	  if (DECL_NAME (fndecl) == NULL_TREE
	      || IDENTIFIER_HAS_TYPE_VALUE (DECL_NAME (fndecl)))
	    called_thing = "constructor";
	  else
	    called_thing = "member function";
	}
      else
	called_thing = "function";
    }

  for (valtail = values, typetail = typelist;
       valtail;
       valtail = TREE_CHAIN (valtail), i++)
    {
      tree type = typetail ? TREE_VALUE (typetail) : 0;
      tree val = TREE_VALUE (valtail);

      if (val == error_mark_node || type == error_mark_node)
	return error_mark_node;

      if (type == void_type_node)
	{
	  if (fndecl)
	    {
	      error ("too many arguments to %s %q+#D", called_thing, fndecl);
	      error ("at this point in file");
	    }
	  else
	    error ("too many arguments to function");
	  /* In case anybody wants to know if this argument
	     list is valid.  */
	  if (result)
	    TREE_TYPE (tree_last (result)) = error_mark_node;
	  break;
	}

      /* build_c_cast puts on a NOP_EXPR to make the result not an lvalue.
	 Strip such NOP_EXPRs, since VAL is used in non-lvalue context.  */
      if (TREE_CODE (val) == NOP_EXPR
	  && TREE_TYPE (val) == TREE_TYPE (TREE_OPERAND (val, 0))
	  && (type == 0 || TREE_CODE (type) != REFERENCE_TYPE))
	val = TREE_OPERAND (val, 0);

      if (type == 0 || TREE_CODE (type) != REFERENCE_TYPE)
	{
	  if (TREE_CODE (TREE_TYPE (val)) == ARRAY_TYPE
	      || TREE_CODE (TREE_TYPE (val)) == FUNCTION_TYPE
	      || TREE_CODE (TREE_TYPE (val)) == METHOD_TYPE)
	    val = decay_conversion (val);
	}

      if (val == error_mark_node)
	return error_mark_node;

      if (type != 0)
	{
	  /* Formal parm type is specified by a function prototype.  */
	  tree parmval;

	  if (!COMPLETE_TYPE_P (complete_type (type)))
	    {
	      if (fndecl)
		error ("parameter %P of %qD has incomplete type %qT",
		       i, fndecl, type);
	      else
		error ("parameter %P has incomplete type %qT", i, type);
	      parmval = error_mark_node;
	    }
	  else
	    {
	      parmval = convert_for_initialization
		(NULL_TREE, type, val, flags,
		 "argument passing", fndecl, i);
	      parmval = convert_for_arg_passing (type, parmval);
	    }

	  if (parmval == error_mark_node)
	    return error_mark_node;

	  result = tree_cons (NULL_TREE, parmval, result);
	}
      else
	{
	  if (fndecl && DECL_BUILT_IN (fndecl)
	      && DECL_FUNCTION_CODE (fndecl) == BUILT_IN_CONSTANT_P)
	    /* Don't do ellipsis conversion for __built_in_constant_p
	       as this will result in spurious warnings for non-POD
	       types.  */
	    val = require_complete_type (val);
	  else
	    val = convert_arg_to_ellipsis (val);

	  result = tree_cons (NULL_TREE, val, result);
	}

      if (typetail)
	typetail = TREE_CHAIN (typetail);
    }

  if (typetail != 0 && typetail != void_list_node)
    {
      /* See if there are default arguments that can be used.  */
      if (TREE_PURPOSE (typetail)
	  && TREE_CODE (TREE_PURPOSE (typetail)) != DEFAULT_ARG)
	{
	  for (; typetail != void_list_node; ++i)
	    {
	      tree parmval
		= convert_default_arg (TREE_VALUE (typetail),
				       TREE_PURPOSE (typetail),
				       fndecl, i);

	      if (parmval == error_mark_node)
		return error_mark_node;

	      result = tree_cons (0, parmval, result);
	      typetail = TREE_CHAIN (typetail);
	      /* ends with `...'.  */
	      if (typetail == NULL_TREE)
		break;
	    }
	}
      else
	{
	  if (fndecl)
	    {
	      error ("too few arguments to %s %q+#D", called_thing, fndecl);
	      error ("at this point in file");
	    }
	  else
	    error ("too few arguments to function");
	  return error_mark_node;
	}
    }

  return nreverse (result);
}

/* Build a binary-operation expression, after performing default
   conversions on the operands.  CODE is the kind of expression to build.  */

tree
build_x_binary_op (enum tree_code code, tree arg1, tree arg2,
		   bool *overloaded_p)
{
  tree orig_arg1;
  tree orig_arg2;
  tree expr;

  orig_arg1 = arg1;
  orig_arg2 = arg2;

  if (processing_template_decl)
    {
      if (type_dependent_expression_p (arg1)
	  || type_dependent_expression_p (arg2))
	return build_min_nt (code, arg1, arg2);
      arg1 = build_non_dependent_expr (arg1);
      arg2 = build_non_dependent_expr (arg2);
    }

  if (code == DOTSTAR_EXPR)
    expr = build_m_component_ref (arg1, arg2);
  else
    expr = build_new_op (code, LOOKUP_NORMAL, arg1, arg2, NULL_TREE,
			 overloaded_p);

  if (processing_template_decl && expr != error_mark_node)
    return build_min_non_dep (code, expr, orig_arg1, orig_arg2);

  return expr;
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

   Note that the operands will never have enumeral types
   because either they have just had the default conversions performed
   or they have both just been converted to some other type in which
   the arithmetic is to be done.

   C++: must do special pointer arithmetic when implementing
   multiple inheritance, and deal with pointer to member functions.  */

tree
build_binary_op (enum tree_code code, tree orig_op0, tree orig_op1,
		 int convert_p ATTRIBUTE_UNUSED)
{
  tree op0, op1;
  enum tree_code code0, code1;
  tree type0, type1;
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

  tree result;

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

  /* True if both operands have arithmetic type.  */
  bool arithmetic_types_p;

  /* Apply default conversions.  */
  op0 = orig_op0;
  op1 = orig_op1;

  if (code == TRUTH_AND_EXPR || code == TRUTH_ANDIF_EXPR
      || code == TRUTH_OR_EXPR || code == TRUTH_ORIF_EXPR
      || code == TRUTH_XOR_EXPR)
    {
      if (!really_overloaded_fn (op0))
	op0 = decay_conversion (op0);
      if (!really_overloaded_fn (op1))
	op1 = decay_conversion (op1);
    }
  else
    {
      if (!really_overloaded_fn (op0))
	op0 = default_conversion (op0);
      if (!really_overloaded_fn (op1))
	op1 = default_conversion (op1);
    }

  /* Strip NON_LVALUE_EXPRs, etc., since we aren't using as an lvalue.  */
  STRIP_TYPE_NOPS (op0);
  STRIP_TYPE_NOPS (op1);

  /* DTRT if one side is an overloaded function, but complain about it.  */
  if (type_unknown_p (op0))
    {
      tree t = instantiate_type (TREE_TYPE (op1), op0, tf_none);
      if (t != error_mark_node)
	{
	  pedwarn ("assuming cast to type %qT from overloaded function",
		   TREE_TYPE (t));
	  op0 = t;
	}
    }
  if (type_unknown_p (op1))
    {
      tree t = instantiate_type (TREE_TYPE (op0), op1, tf_none);
      if (t != error_mark_node)
	{
	  pedwarn ("assuming cast to type %qT from overloaded function",
		   TREE_TYPE (t));
	  op1 = t;
	}
    }

  type0 = TREE_TYPE (op0);
  type1 = TREE_TYPE (op1);

  /* The expression codes of the data types of the arguments tell us
     whether the arguments are integers, floating, pointers, etc.  */
  code0 = TREE_CODE (type0);
  code1 = TREE_CODE (type1);

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

  switch (code)
    {
    case MINUS_EXPR:
      /* Subtraction of two similar pointers.
	 We must subtract them as integers, then divide by object size.  */
      if (code0 == POINTER_TYPE && code1 == POINTER_TYPE
	  && same_type_ignoring_top_level_qualifiers_p (TREE_TYPE (type0),
							TREE_TYPE (type1)))
	return pointer_diff (op0, op1, common_type (type0, type1));
      /* In all other cases except pointer - int, the usual arithmetic
	 rules aply.  */
      else if (!(code0 == POINTER_TYPE && code1 == INTEGER_TYPE))
	{
	  common = 1;
	  break;
	}
      /* The pointer - int case is just like pointer + int; fall
	 through.  */
    case PLUS_EXPR:
      if ((code0 == POINTER_TYPE || code1 == POINTER_TYPE)
	  && (code0 == INTEGER_TYPE || code1 == INTEGER_TYPE))
	{
	  tree ptr_operand;
	  tree int_operand;
	  ptr_operand = ((code0 == POINTER_TYPE) ? op0 : op1);
	  int_operand = ((code0 == INTEGER_TYPE) ? op0 : op1);
	  if (processing_template_decl)
	    {
	      result_type = TREE_TYPE (ptr_operand);
	      break;
	    }
	  return cp_pointer_int_sum (code,
				     ptr_operand, 
				     int_operand);
	}
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
      if ((code0 == INTEGER_TYPE || code0 == REAL_TYPE
	   || code0 == COMPLEX_TYPE || code0 == VECTOR_TYPE)
	  && (code1 == INTEGER_TYPE || code1 == REAL_TYPE
	      || code1 == COMPLEX_TYPE || code1 == VECTOR_TYPE))
	{
	  enum tree_code tcode0 = code0, tcode1 = code1;

	  if (TREE_CODE (op1) == INTEGER_CST && integer_zerop (op1))
	    warning (OPT_Wdiv_by_zero, "division by zero in %<%E / 0%>", op0);
	  else if (TREE_CODE (op1) == REAL_CST && real_zerop (op1))
	    warning (OPT_Wdiv_by_zero, "division by zero in %<%E / 0.%>", op0);

	  if (tcode0 == COMPLEX_TYPE || tcode0 == VECTOR_TYPE)
	    tcode0 = TREE_CODE (TREE_TYPE (TREE_TYPE (op0)));
	  if (tcode1 == COMPLEX_TYPE || tcode1 == VECTOR_TYPE)
	    tcode1 = TREE_CODE (TREE_TYPE (TREE_TYPE (op1)));

	  if (!(tcode0 == INTEGER_TYPE && tcode1 == INTEGER_TYPE))
	    resultcode = RDIV_EXPR;
	  else
	    /* When dividing two signed integers, we have to promote to int.
	       unless we divide by a constant != -1.  Note that default
	       conversion will have been performed on the operands at this
	       point, so we have to dig out the original type to find out if
	       it was unsigned.  */
	    shorten = ((TREE_CODE (op0) == NOP_EXPR
			&& TYPE_UNSIGNED (TREE_TYPE (TREE_OPERAND (op0, 0))))
		       || (TREE_CODE (op1) == INTEGER_CST
			   && ! integer_all_onesp (op1)));

	  common = 1;
	}
      break;

    case BIT_AND_EXPR:
    case BIT_IOR_EXPR:
    case BIT_XOR_EXPR:
      if ((code0 == INTEGER_TYPE && code1 == INTEGER_TYPE)
	  || (code0 == VECTOR_TYPE && code1 == VECTOR_TYPE))
	shorten = -1;
      break;

    case TRUNC_MOD_EXPR:
    case FLOOR_MOD_EXPR:
      if (code1 == INTEGER_TYPE && integer_zerop (op1))
	warning (OPT_Wdiv_by_zero, "division by zero in %<%E %% 0%>", op0);
      else if (code1 == REAL_TYPE && real_zerop (op1))
	warning (OPT_Wdiv_by_zero, "division by zero in %<%E %% 0.%>", op0);

      if (code0 == INTEGER_TYPE && code1 == INTEGER_TYPE)
	{
	  /* Although it would be tempting to shorten always here, that loses
	     on some targets, since the modulo instruction is undefined if the
	     quotient can't be represented in the computation mode.  We shorten
	     only if unsigned or if dividing by something we know != -1.  */
	  shorten = ((TREE_CODE (op0) == NOP_EXPR
		      && TYPE_UNSIGNED (TREE_TYPE (TREE_OPERAND (op0, 0))))
		     || (TREE_CODE (op1) == INTEGER_CST
			 && ! integer_all_onesp (op1)));
	  common = 1;
	}
      break;

    case TRUTH_ANDIF_EXPR:
    case TRUTH_ORIF_EXPR:
    case TRUTH_AND_EXPR:
    case TRUTH_OR_EXPR:
      result_type = boolean_type_node;
      break;

      /* Shift operations: result has same type as first operand;
	 always convert second operand to int.
	 Also set SHORT_SHIFT if shifting rightward.  */

    case RSHIFT_EXPR:
      if (code0 == INTEGER_TYPE && code1 == INTEGER_TYPE)
	{
	  result_type = type0;
	  if (TREE_CODE (op1) == INTEGER_CST)
	    {
	      if (tree_int_cst_lt (op1, integer_zero_node))
		warning (0, "right shift count is negative");
	      else
		{
		  if (! integer_zerop (op1))
		    short_shift = 1;
		  if (compare_tree_int (op1, TYPE_PRECISION (type0)) >= 0)
		    warning (0, "right shift count >= width of type");
		}
	    }
	  /* Convert the shift-count to an integer, regardless of
	     size of value being shifted.  */
	  if (TYPE_MAIN_VARIANT (TREE_TYPE (op1)) != integer_type_node)
	    op1 = cp_convert (integer_type_node, op1);
	  /* Avoid converting op1 to result_type later.  */
	  converted = 1;
	}
      break;

    case LSHIFT_EXPR:
      if (code0 == INTEGER_TYPE && code1 == INTEGER_TYPE)
	{
	  result_type = type0;
	  if (TREE_CODE (op1) == INTEGER_CST)
	    {
	      if (tree_int_cst_lt (op1, integer_zero_node))
		warning (0, "left shift count is negative");
	      else if (compare_tree_int (op1, TYPE_PRECISION (type0)) >= 0)
		warning (0, "left shift count >= width of type");
	    }
	  /* Convert the shift-count to an integer, regardless of
	     size of value being shifted.  */
	  if (TYPE_MAIN_VARIANT (TREE_TYPE (op1)) != integer_type_node)
	    op1 = cp_convert (integer_type_node, op1);
	  /* Avoid converting op1 to result_type later.  */
	  converted = 1;
	}
      break;

    case RROTATE_EXPR:
    case LROTATE_EXPR:
      if (code0 == INTEGER_TYPE && code1 == INTEGER_TYPE)
	{
	  result_type = type0;
	  if (TREE_CODE (op1) == INTEGER_CST)
	    {
	      if (tree_int_cst_lt (op1, integer_zero_node))
		warning (0, "%s rotate count is negative",
			 (code == LROTATE_EXPR) ? "left" : "right");
	      else if (compare_tree_int (op1, TYPE_PRECISION (type0)) >= 0)
		warning (0, "%s rotate count >= width of type",
			 (code == LROTATE_EXPR) ? "left" : "right");
	    }
	  /* Convert the shift-count to an integer, regardless of
	     size of value being shifted.  */
	  if (TYPE_MAIN_VARIANT (TREE_TYPE (op1)) != integer_type_node)
	    op1 = cp_convert (integer_type_node, op1);
	}
      break;

    case EQ_EXPR:
    case NE_EXPR:
      if (code0 == REAL_TYPE || code1 == REAL_TYPE)
	warning (OPT_Wfloat_equal,
		 "comparing floating point with == or != is unsafe");
      if ((TREE_CODE (orig_op0) == STRING_CST && !integer_zerop (op1))
	  || (TREE_CODE (orig_op1) == STRING_CST && !integer_zerop (op0)))
	warning (OPT_Waddress, 
                 "comparison with string literal results in unspecified behaviour");

      build_type = boolean_type_node;
      if ((code0 == INTEGER_TYPE || code0 == REAL_TYPE
	   || code0 == COMPLEX_TYPE)
	  && (code1 == INTEGER_TYPE || code1 == REAL_TYPE
	      || code1 == COMPLEX_TYPE))
	short_compare = 1;
      else if ((code0 == POINTER_TYPE && code1 == POINTER_TYPE)
	       || (TYPE_PTRMEM_P (type0) && TYPE_PTRMEM_P (type1)))
	result_type = composite_pointer_type (type0, type1, op0, op1,
					      "comparison");
      else if ((code0 == POINTER_TYPE || TYPE_PTRMEM_P (type0))
	       && null_ptr_cst_p (op1))
	result_type = type0;
      else if ((code1 == POINTER_TYPE || TYPE_PTRMEM_P (type1))
	       && null_ptr_cst_p (op0))
	result_type = type1;
      else if (code0 == POINTER_TYPE && code1 == INTEGER_TYPE)
	{
	  result_type = type0;
	  error ("ISO C++ forbids comparison between pointer and integer");
	}
      else if (code0 == INTEGER_TYPE && code1 == POINTER_TYPE)
	{
	  result_type = type1;
	  error ("ISO C++ forbids comparison between pointer and integer");
	}
      else if (TYPE_PTRMEMFUNC_P (type0) && null_ptr_cst_p (op1))
	{
	  op0 = build_ptrmemfunc_access_expr (op0, pfn_identifier);
	  op1 = cp_convert (TREE_TYPE (op0), integer_zero_node);
	  result_type = TREE_TYPE (op0);
	}
      else if (TYPE_PTRMEMFUNC_P (type1) && null_ptr_cst_p (op0))
	return cp_build_binary_op (code, op1, op0);
      else if (TYPE_PTRMEMFUNC_P (type0) && TYPE_PTRMEMFUNC_P (type1)
	       && same_type_p (type0, type1))
	{
	  /* E will be the final comparison.  */
	  tree e;
	  /* E1 and E2 are for scratch.  */
	  tree e1;
	  tree e2;
	  tree pfn0;
	  tree pfn1;
	  tree delta0;
	  tree delta1;

	  if (TREE_SIDE_EFFECTS (op0))
	    op0 = save_expr (op0);
	  if (TREE_SIDE_EFFECTS (op1))
	    op1 = save_expr (op1);

	  /* We generate:

	     (op0.pfn == op1.pfn
	      && (!op0.pfn || op0.delta == op1.delta))

	     The reason for the `!op0.pfn' bit is that a NULL
	     pointer-to-member is any member with a zero PFN; the
	     DELTA field is unspecified.  */
	  pfn0 = pfn_from_ptrmemfunc (op0);
	  pfn1 = pfn_from_ptrmemfunc (op1);
	  delta0 = build_ptrmemfunc_access_expr (op0,
						 delta_identifier);
	  delta1 = build_ptrmemfunc_access_expr (op1,
						 delta_identifier);
	  e1 = cp_build_binary_op (EQ_EXPR, delta0, delta1);
	  e2 = cp_build_binary_op (EQ_EXPR,
				   pfn0,
				   cp_convert (TREE_TYPE (pfn0),
					       integer_zero_node));
	  e1 = cp_build_binary_op (TRUTH_ORIF_EXPR, e1, e2);
	  e2 = build2 (EQ_EXPR, boolean_type_node, pfn0, pfn1);
	  e = cp_build_binary_op (TRUTH_ANDIF_EXPR, e2, e1);
	  if (code == EQ_EXPR)
	    return e;
	  return cp_build_binary_op (EQ_EXPR, e, integer_zero_node);
	}
      else
	{
	  gcc_assert (!TYPE_PTRMEMFUNC_P (type0)
		      || !same_type_p (TYPE_PTRMEMFUNC_FN_TYPE (type0),
				       type1));
	  gcc_assert (!TYPE_PTRMEMFUNC_P (type1)
		      || !same_type_p (TYPE_PTRMEMFUNC_FN_TYPE (type1),
				       type0));
	}

      break;

    case MAX_EXPR:
    case MIN_EXPR:
      if ((code0 == INTEGER_TYPE || code0 == REAL_TYPE)
	   && (code1 == INTEGER_TYPE || code1 == REAL_TYPE))
	shorten = 1;
      else if (code0 == POINTER_TYPE && code1 == POINTER_TYPE)
	result_type = composite_pointer_type (type0, type1, op0, op1,
					      "comparison");
      break;

    case LE_EXPR:
    case GE_EXPR:
    case LT_EXPR:
    case GT_EXPR:
      if (TREE_CODE (orig_op0) == STRING_CST
	  || TREE_CODE (orig_op1) == STRING_CST)
	warning (OPT_Waddress, 
                 "comparison with string literal results in unspecified behaviour");

      build_type = boolean_type_node;
      if ((code0 == INTEGER_TYPE || code0 == REAL_TYPE)
	   && (code1 == INTEGER_TYPE || code1 == REAL_TYPE))
	short_compare = 1;
      else if (code0 == POINTER_TYPE && code1 == POINTER_TYPE)
	result_type = composite_pointer_type (type0, type1, op0, op1,
					      "comparison");
      else if (code0 == POINTER_TYPE && TREE_CODE (op1) == INTEGER_CST
	       && integer_zerop (op1))
	result_type = type0;
      else if (code1 == POINTER_TYPE && TREE_CODE (op0) == INTEGER_CST
	       && integer_zerop (op0))
	result_type = type1;
      else if (code0 == POINTER_TYPE && code1 == INTEGER_TYPE)
	{
	  result_type = type0;
	  pedwarn ("ISO C++ forbids comparison between pointer and integer");
	}
      else if (code0 == INTEGER_TYPE && code1 == POINTER_TYPE)
	{
	  result_type = type1;
	  pedwarn ("ISO C++ forbids comparison between pointer and integer");
	}
      break;

    case UNORDERED_EXPR:
    case ORDERED_EXPR:
    case UNLT_EXPR:
    case UNLE_EXPR:
    case UNGT_EXPR:
    case UNGE_EXPR:
    case UNEQ_EXPR:
      build_type = integer_type_node;
      if (code0 != REAL_TYPE || code1 != REAL_TYPE)
	{
	  error ("unordered comparison on non-floating point argument");
	  return error_mark_node;
	}
      common = 1;
      break;

    default:
      break;
    }

  if (((code0 == INTEGER_TYPE || code0 == REAL_TYPE || code0 == COMPLEX_TYPE)
       && (code1 == INTEGER_TYPE || code1 == REAL_TYPE
	   || code1 == COMPLEX_TYPE)))
    arithmetic_types_p = 1;
  else
    {
      arithmetic_types_p = 0;
      /* Vector arithmetic is only allowed when both sides are vectors.  */
      if (code0 == VECTOR_TYPE && code1 == VECTOR_TYPE)
	{
	  if (!tree_int_cst_equal (TYPE_SIZE (type0), TYPE_SIZE (type1))
	      || !same_scalar_type_ignoring_signedness (TREE_TYPE (type0),
							TREE_TYPE (type1)))
	    {
	      binary_op_error (code);
	      return error_mark_node;
	    }
	  arithmetic_types_p = 1;
	}
    }
  /* Determine the RESULT_TYPE, if it is not already known.  */
  if (!result_type
      && arithmetic_types_p
      && (shorten || common || short_compare))
    result_type = common_type (type0, type1);

  if (!result_type)
    {
      error ("invalid operands of types %qT and %qT to binary %qO",
	     TREE_TYPE (orig_op0), TREE_TYPE (orig_op1), code);
      return error_mark_node;
    }

  /* If we're in a template, the only thing we need to know is the
     RESULT_TYPE.  */
  if (processing_template_decl)
    return build2 (resultcode,
		   build_type ? build_type : result_type,
		   op0, op1);

  if (arithmetic_types_p)
    {
      int none_complex = (code0 != COMPLEX_TYPE && code1 != COMPLEX_TYPE);

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
	  tree arg0 = get_narrower (op0, &unsigned0);
	  tree arg1 = get_narrower (op1, &unsigned1);
	  /* UNS is 1 if the operation to be done is an unsigned one.  */
	  int uns = TYPE_UNSIGNED (result_type);
	  tree type;

	  final_type = result_type;

	  /* Handle the case that OP0 does not *contain* a conversion
	     but it *requires* conversion to FINAL_TYPE.  */

	  if (op0 == arg0 && TREE_TYPE (op0) != final_type)
	    unsigned0 = TYPE_UNSIGNED (TREE_TYPE (op0));
	  if (op1 == arg1 && TREE_TYPE (op1) != final_type)
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
	    result_type = c_common_signed_or_unsigned_type
	      (unsigned0, common_type (TREE_TYPE (arg0), TREE_TYPE (arg1)));
	  else if (TREE_CODE (arg0) == INTEGER_CST
		   && (unsigned1 || !uns)
		   && (TYPE_PRECISION (TREE_TYPE (arg1))
		       < TYPE_PRECISION (result_type))
		   && (type = c_common_signed_or_unsigned_type
		       (unsigned1, TREE_TYPE (arg1)),
		       int_fits_type_p (arg0, type)))
	    result_type = type;
	  else if (TREE_CODE (arg1) == INTEGER_CST
		   && (unsigned0 || !uns)
		   && (TYPE_PRECISION (TREE_TYPE (arg0))
		       < TYPE_PRECISION (result_type))
		   && (type = c_common_signed_or_unsigned_type
		       (unsigned0, TREE_TYPE (arg0)),
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
	      /* If arg is sign-extended and then unsigned-shifted,
		 we can simulate this with a signed shift in arg's type
		 only if the extended result is at least twice as wide
		 as the arg.  Otherwise, the shift could use up all the
		 ones made by sign-extension and bring in zeros.
		 We can't optimize that case at all, but in most machines
		 it never happens because available widths are 2**N.  */
	      && (!TYPE_UNSIGNED (final_type)
		  || unsigned_arg
		  || (((unsigned) 2 * TYPE_PRECISION (TREE_TYPE (arg0)))
		      <= TYPE_PRECISION (result_type))))
	    {
	      /* Do an unsigned shift if the operand was zero-extended.  */
	      result_type
		= c_common_signed_or_unsigned_type (unsigned_arg,
						    TREE_TYPE (arg0));
	      /* Convert value-to-be-shifted to that type.  */
	      if (TREE_TYPE (op0) != result_type)
		op0 = cp_convert (result_type, op0);
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
	    return cp_convert (boolean_type_node, val);
	  op0 = xop0, op1 = xop1;
	  converted = 1;
	  resultcode = xresultcode;
	}

      if ((short_compare || code == MIN_EXPR || code == MAX_EXPR)
	  && warn_sign_compare
	  /* Do not warn until the template is instantiated; we cannot
	     bound the ranges of the arguments until that point.  */
	  && !processing_template_decl)
	{
	  int op0_signed = !TYPE_UNSIGNED (TREE_TYPE (orig_op0));
	  int op1_signed = !TYPE_UNSIGNED (TREE_TYPE (orig_op1));

	  int unsignedp0, unsignedp1;
	  tree primop0 = get_narrower (op0, &unsignedp0);
	  tree primop1 = get_narrower (op1, &unsignedp1);

	  /* Check for comparison of different enum types.  */
	  if (TREE_CODE (TREE_TYPE (orig_op0)) == ENUMERAL_TYPE
	      && TREE_CODE (TREE_TYPE (orig_op1)) == ENUMERAL_TYPE
	      && TYPE_MAIN_VARIANT (TREE_TYPE (orig_op0))
		 != TYPE_MAIN_VARIANT (TREE_TYPE (orig_op1)))
	    {
	      warning (0, "comparison between types %q#T and %q#T",
		       TREE_TYPE (orig_op0), TREE_TYPE (orig_op1));
	    }

	  /* Give warnings for comparisons between signed and unsigned
	     quantities that may fail.  */
	  /* Do the checking based on the original operand trees, so that
	     casts will be considered, but default promotions won't be.  */

	  /* Do not warn if the comparison is being done in a signed type,
	     since the signed type will only be chosen if it can represent
	     all the values of the unsigned type.  */
	  if (!TYPE_UNSIGNED (result_type))
	    /* OK */;
	  /* Do not warn if both operands are unsigned.  */
	  else if (op0_signed == op1_signed)
	    /* OK */;
	  /* Do not warn if the signed quantity is an unsuffixed
	     integer literal (or some static constant expression
	     involving such literals or a conditional expression
	     involving such literals) and it is non-negative.  */
	  else if ((op0_signed && tree_expr_nonnegative_p (orig_op0))
		   || (op1_signed && tree_expr_nonnegative_p (orig_op1)))
	    /* OK */;
	  /* Do not warn if the comparison is an equality operation,
	     the unsigned quantity is an integral constant and it does
	     not use the most significant bit of result_type.  */
	  else if ((resultcode == EQ_EXPR || resultcode == NE_EXPR)
		   && ((op0_signed && TREE_CODE (orig_op1) == INTEGER_CST
			&& int_fits_type_p (orig_op1, c_common_signed_type
					    (result_type)))
			|| (op1_signed && TREE_CODE (orig_op0) == INTEGER_CST
			    && int_fits_type_p (orig_op0, c_common_signed_type
						(result_type)))))
	    /* OK */;
	  else
	    warning (0, "comparison between signed and unsigned integer expressions");

	  /* Warn if two unsigned values are being compared in a size
	     larger than their original size, and one (and only one) is the
	     result of a `~' operator.  This comparison will always fail.

	     Also warn if one operand is a constant, and the constant does not
	     have all bits set that are set in the ~ operand when it is
	     extended.  */

	  if ((TREE_CODE (primop0) == BIT_NOT_EXPR)
	      ^ (TREE_CODE (primop1) == BIT_NOT_EXPR))
	    {
	      if (TREE_CODE (primop0) == BIT_NOT_EXPR)
		primop0 = get_narrower (TREE_OPERAND (op0, 0), &unsignedp0);
	      if (TREE_CODE (primop1) == BIT_NOT_EXPR)
		primop1 = get_narrower (TREE_OPERAND (op1, 0), &unsignedp1);

	      if (host_integerp (primop0, 0) || host_integerp (primop1, 0))
		{
		  tree primop;
		  HOST_WIDE_INT constant, mask;
		  int unsignedp;
		  unsigned int bits;

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
		      && bits < HOST_BITS_PER_LONG && unsignedp)
		    {
		      mask = (~ (HOST_WIDE_INT) 0) << bits;
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

  /* If CONVERTED is zero, both args will be converted to type RESULT_TYPE.
     Then the expression will be built.
     It will be given type FINAL_TYPE if that is nonzero;
     otherwise, it will be given type RESULT_TYPE.  */

  /* Issue warnings about peculiar, but valid, uses of NULL.  */
  if (/* It's reasonable to use pointer values as operands of &&
	 and ||, so NULL is no exception.  */
      !(code == TRUTH_ANDIF_EXPR || code == TRUTH_ORIF_EXPR)
      && (/* If OP0 is NULL and OP1 is not a pointer, or vice versa.  */
	  (orig_op0 == null_node
	   && TREE_CODE (TREE_TYPE (op1)) != POINTER_TYPE)
	  /* Or vice versa.  */
	  || (orig_op1 == null_node
	      && TREE_CODE (TREE_TYPE (op0)) != POINTER_TYPE)
	  /* Or, both are NULL and the operation was not a comparison.  */
	  || (orig_op0 == null_node && orig_op1 == null_node
	      && code != EQ_EXPR && code != NE_EXPR)))
    /* Some sort of arithmetic operation involving NULL was
       performed.  Note that pointer-difference and pointer-addition
       have already been handled above, and so we don't end up here in
       that case.  */
    warning (0, "NULL used in arithmetic");

  if (! converted)
    {
      if (TREE_TYPE (op0) != result_type)
	op0 = cp_convert (result_type, op0);
      if (TREE_TYPE (op1) != result_type)
	op1 = cp_convert (result_type, op1);

      if (op0 == error_mark_node || op1 == error_mark_node)
	return error_mark_node;
    }

  if (build_type == NULL_TREE)
    build_type = result_type;

  result = build2 (resultcode, build_type, op0, op1);
  result = fold_if_not_in_template (result);
  if (final_type != 0)
    result = cp_convert (final_type, result);
  return result;
}

/* Return a tree for the sum or difference (RESULTCODE says which)
   of pointer PTROP and integer INTOP.  */

static tree
cp_pointer_int_sum (enum tree_code resultcode, tree ptrop, tree intop)
{
  tree res_type = TREE_TYPE (ptrop);

  /* pointer_int_sum() uses size_in_bytes() on the TREE_TYPE(res_type)
     in certain circumstance (when it's valid to do so).  So we need
     to make sure it's complete.  We don't need to check here, if we
     can actually complete it at all, as those checks will be done in
     pointer_int_sum() anyway.  */
  complete_type (TREE_TYPE (res_type));

  return pointer_int_sum (resultcode, ptrop,
			  fold_if_not_in_template (intop));
}

/* Return a tree for the difference of pointers OP0 and OP1.
   The resulting tree has type int.  */

static tree
pointer_diff (tree op0, tree op1, tree ptrtype)
{
  tree result;
  tree restype = ptrdiff_type_node;
  tree target_type = TREE_TYPE (ptrtype);

  if (!complete_type_or_else (target_type, NULL_TREE))
    return error_mark_node;

  if (pedantic || warn_pointer_arith)
    {
      if (TREE_CODE (target_type) == VOID_TYPE)
	pedwarn ("ISO C++ forbids using pointer of type %<void *%> in subtraction");
      if (TREE_CODE (target_type) == FUNCTION_TYPE)
	pedwarn ("ISO C++ forbids using pointer to a function in subtraction");
      if (TREE_CODE (target_type) == METHOD_TYPE)
	pedwarn ("ISO C++ forbids using pointer to a method in subtraction");
    }

  /* First do the subtraction as integers;
     then drop through to build the divide operator.  */

  op0 = cp_build_binary_op (MINUS_EXPR,
			    cp_convert (restype, op0),
			    cp_convert (restype, op1));

  /* This generates an error if op1 is a pointer to an incomplete type.  */
  if (!COMPLETE_TYPE_P (TREE_TYPE (TREE_TYPE (op1))))
    error ("invalid use of a pointer to an incomplete type in pointer arithmetic");

  op1 = (TYPE_PTROB_P (ptrtype)
	 ? size_in_bytes (target_type)
	 : integer_one_node);

  /* Do the division.  */

  result = build2 (EXACT_DIV_EXPR, restype, op0, cp_convert (restype, op1));
  return fold_if_not_in_template (result);
}

/* Construct and perhaps optimize a tree representation
   for a unary operation.  CODE, a tree_code, specifies the operation
   and XARG is the operand.  */

tree
build_x_unary_op (enum tree_code code, tree xarg)
{
  tree orig_expr = xarg;
  tree exp;
  int ptrmem = 0;

  if (processing_template_decl)
    {
      if (type_dependent_expression_p (xarg))
	return build_min_nt (code, xarg, NULL_TREE);

      xarg = build_non_dependent_expr (xarg);
    }

  exp = NULL_TREE;

  /* [expr.unary.op] says:

       The address of an object of incomplete type can be taken.

     (And is just the ordinary address operator, not an overloaded
     "operator &".)  However, if the type is a template
     specialization, we must complete the type at this point so that
     an overloaded "operator &" will be available if required.  */
  if (code == ADDR_EXPR
      && TREE_CODE (xarg) != TEMPLATE_ID_EXPR
      && ((CLASS_TYPE_P (TREE_TYPE (xarg))
	   && !COMPLETE_TYPE_P (complete_type (TREE_TYPE (xarg))))
	  || (TREE_CODE (xarg) == OFFSET_REF)))
    /* Don't look for a function.  */;
  else
    exp = build_new_op (code, LOOKUP_NORMAL, xarg, NULL_TREE, NULL_TREE,
			/*overloaded_p=*/NULL);
  if (!exp && code == ADDR_EXPR)
    {
      /*  A pointer to member-function can be formed only by saying
	  &X::mf.  */
      if (!flag_ms_extensions && TREE_CODE (TREE_TYPE (xarg)) == METHOD_TYPE
	  && (TREE_CODE (xarg) != OFFSET_REF || !PTRMEM_OK_P (xarg)))
	{
	  if (TREE_CODE (xarg) != OFFSET_REF
	      || !TYPE_P (TREE_OPERAND (xarg, 0)))
	    {
	      error ("invalid use of %qE to form a pointer-to-member-function",
		     xarg);
	      if (TREE_CODE (xarg) != OFFSET_REF)
		inform ("  a qualified-id is required");
	      return error_mark_node;
	    }
	  else
	    {
	      error ("parentheses around %qE cannot be used to form a"
		     " pointer-to-member-function",
		     xarg);
	      PTRMEM_OK_P (xarg) = 1;
	    }
	}

      if (TREE_CODE (xarg) == OFFSET_REF)
	{
	  ptrmem = PTRMEM_OK_P (xarg);

	  if (!ptrmem && !flag_ms_extensions
	      && TREE_CODE (TREE_TYPE (TREE_OPERAND (xarg, 1))) == METHOD_TYPE)
	    {
	      /* A single non-static member, make sure we don't allow a
		 pointer-to-member.  */
	      xarg = build2 (OFFSET_REF, TREE_TYPE (xarg),
			     TREE_OPERAND (xarg, 0),
			     ovl_cons (TREE_OPERAND (xarg, 1), NULL_TREE));
	      PTRMEM_OK_P (xarg) = ptrmem;
	    }
	}
      else if (TREE_CODE (xarg) == TARGET_EXPR)
	warning (0, "taking address of temporary");
      exp = build_unary_op (ADDR_EXPR, xarg, 0);
    }

  if (processing_template_decl && exp != error_mark_node)
    exp = build_min_non_dep (code, exp, orig_expr,
			     /*For {PRE,POST}{INC,DEC}REMENT_EXPR*/NULL_TREE);
  if (TREE_CODE (exp) == ADDR_EXPR)
    PTRMEM_OK_P (exp) = ptrmem;
  return exp;
}

/* Like c_common_truthvalue_conversion, but handle pointer-to-member
   constants, where a null value is represented by an INTEGER_CST of
   -1.  */

tree
cp_truthvalue_conversion (tree expr)
{
  tree type = TREE_TYPE (expr);
  if (TYPE_PTRMEM_P (type))
    return build_binary_op (NE_EXPR, expr, integer_zero_node, 1);
  else
    return c_common_truthvalue_conversion (expr);
}

/* Just like cp_truthvalue_conversion, but we want a CLEANUP_POINT_EXPR.  */

tree
condition_conversion (tree expr)
{
  tree t;
  if (processing_template_decl)
    return expr;
  t = perform_implicit_conversion (boolean_type_node, expr);
  t = fold_build_cleanup_point_expr (boolean_type_node, t);
  return t;
}

/* Return an ADDR_EXPR giving the address of T.  This function
   attempts no optimizations or simplifications; it is a low-level
   primitive.  */

tree
build_address (tree t)
{
  tree addr;

  if (error_operand_p (t) || !cxx_mark_addressable (t))
    return error_mark_node;

  addr = build1 (ADDR_EXPR, build_pointer_type (TREE_TYPE (t)), t);

  return addr;
}

/* Return a NOP_EXPR converting EXPR to TYPE.  */

tree
build_nop (tree type, tree expr)
{
  if (type == error_mark_node || error_operand_p (expr))
    return expr;
  return build1 (NOP_EXPR, type, expr);
}

/* C++: Must handle pointers to members.

   Perhaps type instantiation should be extended to handle conversion
   from aggregates to types we don't yet know we want?  (Or are those
   cases typically errors which should be reported?)

   NOCONVERT nonzero suppresses the default promotions
   (such as from short to int).  */

tree
build_unary_op (enum tree_code code, tree xarg, int noconvert)
{
  /* No default_conversion here.  It causes trouble for ADDR_EXPR.  */
  tree arg = xarg;
  tree argtype = 0;
  const char *errstring = NULL;
  tree val;
  const char *invalid_op_diag;

  if (arg == error_mark_node)
    return error_mark_node;

  if ((invalid_op_diag
       = targetm.invalid_unary_op ((code == UNARY_PLUS_EXPR
				    ? CONVERT_EXPR
				    : code),
				   TREE_TYPE (xarg))))
    {
      error (invalid_op_diag);
      return error_mark_node;
    }

  switch (code)
    {
    case UNARY_PLUS_EXPR:
    case NEGATE_EXPR:
      {
	int flags = WANT_ARITH | WANT_ENUM;
	/* Unary plus (but not unary minus) is allowed on pointers.  */
	if (code == UNARY_PLUS_EXPR)
	  flags |= WANT_POINTER;
	arg = build_expr_type_conversion (flags, arg, true);
	if (!arg)
	  errstring = (code == NEGATE_EXPR
		       ? "wrong type argument to unary minus"
		       : "wrong type argument to unary plus");
	else
	  {
	    if (!noconvert && CP_INTEGRAL_TYPE_P (TREE_TYPE (arg)))
	      arg = perform_integral_promotions (arg);

	    /* Make sure the result is not an lvalue: a unary plus or minus
	       expression is always a rvalue.  */
	    arg = rvalue (arg);
	  }
      }
      break;

    case BIT_NOT_EXPR:
      if (TREE_CODE (TREE_TYPE (arg)) == COMPLEX_TYPE)
	{
	  code = CONJ_EXPR;
	  if (!noconvert)
	    arg = default_conversion (arg);
	}
      else if (!(arg = build_expr_type_conversion (WANT_INT | WANT_ENUM
						   | WANT_VECTOR,
						   arg, true)))
	errstring = "wrong type argument to bit-complement";
      else if (!noconvert && CP_INTEGRAL_TYPE_P (TREE_TYPE (arg)))
	arg = perform_integral_promotions (arg);
      break;

    case ABS_EXPR:
      if (!(arg = build_expr_type_conversion (WANT_ARITH | WANT_ENUM, arg, true)))
	errstring = "wrong type argument to abs";
      else if (!noconvert)
	arg = default_conversion (arg);
      break;

    case CONJ_EXPR:
      /* Conjugating a real value is a no-op, but allow it anyway.  */
      if (!(arg = build_expr_type_conversion (WANT_ARITH | WANT_ENUM, arg, true)))
	errstring = "wrong type argument to conjugation";
      else if (!noconvert)
	arg = default_conversion (arg);
      break;

    case TRUTH_NOT_EXPR:
      arg = perform_implicit_conversion (boolean_type_node, arg);
      val = invert_truthvalue (arg);
      if (arg != error_mark_node)
	return val;
      errstring = "in argument to unary !";
      break;

    case NOP_EXPR:
      break;

    case REALPART_EXPR:
      if (TREE_CODE (arg) == COMPLEX_CST)
	return TREE_REALPART (arg);
      else if (TREE_CODE (TREE_TYPE (arg)) == COMPLEX_TYPE)
	{
	  arg = build1 (REALPART_EXPR, TREE_TYPE (TREE_TYPE (arg)), arg);
	  return fold_if_not_in_template (arg);
	}
      else
	return arg;

    case IMAGPART_EXPR:
      if (TREE_CODE (arg) == COMPLEX_CST)
	return TREE_IMAGPART (arg);
      else if (TREE_CODE (TREE_TYPE (arg)) == COMPLEX_TYPE)
	{
	  arg = build1 (IMAGPART_EXPR, TREE_TYPE (TREE_TYPE (arg)), arg);
	  return fold_if_not_in_template (arg);
	}
      else
	return cp_convert (TREE_TYPE (arg), integer_zero_node);

    case PREINCREMENT_EXPR:
    case POSTINCREMENT_EXPR:
    case PREDECREMENT_EXPR:
    case POSTDECREMENT_EXPR:
      /* Handle complex lvalues (when permitted)
	 by reduction to simpler cases.  */

      val = unary_complex_lvalue (code, arg);
      if (val != 0)
	return val;

      /* Increment or decrement the real part of the value,
	 and don't change the imaginary part.  */
      if (TREE_CODE (TREE_TYPE (arg)) == COMPLEX_TYPE)
	{
	  tree real, imag;

	  arg = stabilize_reference (arg);
	  real = build_unary_op (REALPART_EXPR, arg, 1);
	  imag = build_unary_op (IMAGPART_EXPR, arg, 1);
	  return build2 (COMPLEX_EXPR, TREE_TYPE (arg),
			 build_unary_op (code, real, 1), imag);
	}

      /* Report invalid types.  */

      if (!(arg = build_expr_type_conversion (WANT_ARITH | WANT_POINTER,
					      arg, true)))
	{
	  if (code == PREINCREMENT_EXPR)
	    errstring ="no pre-increment operator for type";
	  else if (code == POSTINCREMENT_EXPR)
	    errstring ="no post-increment operator for type";
	  else if (code == PREDECREMENT_EXPR)
	    errstring ="no pre-decrement operator for type";
	  else
	    errstring ="no post-decrement operator for type";
	  break;
	}

      /* Report something read-only.  */

      if (CP_TYPE_CONST_P (TREE_TYPE (arg))
	  || TREE_READONLY (arg))
	readonly_error (arg, ((code == PREINCREMENT_EXPR
			       || code == POSTINCREMENT_EXPR)
			      ? "increment" : "decrement"),
			0);

      {
	tree inc;
	tree declared_type;
	tree result_type = TREE_TYPE (arg);

	declared_type = unlowered_expr_type (arg);

	arg = get_unwidened (arg, 0);
	argtype = TREE_TYPE (arg);

	/* ARM $5.2.5 last annotation says this should be forbidden.  */
	if (TREE_CODE (argtype) == ENUMERAL_TYPE)
	  pedwarn ("ISO C++ forbids %sing an enum",
		   (code == PREINCREMENT_EXPR || code == POSTINCREMENT_EXPR)
		   ? "increment" : "decrement");

	/* Compute the increment.  */

	if (TREE_CODE (argtype) == POINTER_TYPE)
	  {
	    tree type = complete_type (TREE_TYPE (argtype));

	    if (!COMPLETE_OR_VOID_TYPE_P (type))
	      error ("cannot %s a pointer to incomplete type %qT",
		     ((code == PREINCREMENT_EXPR
		       || code == POSTINCREMENT_EXPR)
		      ? "increment" : "decrement"), TREE_TYPE (argtype));
	    else if ((pedantic || warn_pointer_arith)
		     && !TYPE_PTROB_P (argtype))
	      pedwarn ("ISO C++ forbids %sing a pointer of type %qT",
		       ((code == PREINCREMENT_EXPR
			 || code == POSTINCREMENT_EXPR)
			? "increment" : "decrement"), argtype);
	    inc = cxx_sizeof_nowarn (TREE_TYPE (argtype));
	  }
	else
	  inc = integer_one_node;

	inc = cp_convert (argtype, inc);

	/* Handle incrementing a cast-expression.  */

	switch (TREE_CODE (arg))
	  {
	  case NOP_EXPR:
	  case CONVERT_EXPR:
	  case FLOAT_EXPR:
	  case FIX_TRUNC_EXPR:
	  case FIX_FLOOR_EXPR:
	  case FIX_ROUND_EXPR:
	  case FIX_CEIL_EXPR:
	    {
	      tree incremented, modify, value, compound;
	      if (! lvalue_p (arg) && pedantic)
		pedwarn ("cast to non-reference type used as lvalue");
	      arg = stabilize_reference (arg);
	      if (code == PREINCREMENT_EXPR || code == PREDECREMENT_EXPR)
		value = arg;
	      else
		value = save_expr (arg);
	      incremented = build2 (((code == PREINCREMENT_EXPR
				      || code == POSTINCREMENT_EXPR)
				     ? PLUS_EXPR : MINUS_EXPR),
				    argtype, value, inc);

	      modify = build_modify_expr (arg, NOP_EXPR, incremented);
	      compound = build2 (COMPOUND_EXPR, TREE_TYPE (arg),
				 modify, value);

	      /* Eliminate warning about unused result of + or -.  */
	      TREE_NO_WARNING (compound) = 1;
	      return compound;
	    }

	  default:
	    break;
	  }

	/* Complain about anything else that is not a true lvalue.  */
	if (!lvalue_or_else (arg, ((code == PREINCREMENT_EXPR
				    || code == POSTINCREMENT_EXPR)
				   ? lv_increment : lv_decrement)))
	  return error_mark_node;

	/* Forbid using -- on `bool'.  */
	if (same_type_p (declared_type, boolean_type_node))
	  {
	    if (code == POSTDECREMENT_EXPR || code == PREDECREMENT_EXPR)
	      {
		error ("invalid use of %<--%> on bool variable %qD", arg);
		return error_mark_node;
	      }
	    val = boolean_increment (code, arg);
	  }
	else
	  val = build2 (code, TREE_TYPE (arg), arg, inc);

	TREE_SIDE_EFFECTS (val) = 1;
	return cp_convert (result_type, val);
      }

    case ADDR_EXPR:
      /* Note that this operation never does default_conversion
	 regardless of NOCONVERT.  */

      argtype = lvalue_type (arg);

      if (TREE_CODE (arg) == OFFSET_REF)
	goto offset_ref;

      if (TREE_CODE (argtype) == REFERENCE_TYPE)
	{
	  tree type = build_pointer_type (TREE_TYPE (argtype));
	  arg = build1 (CONVERT_EXPR, type, arg);
	  return arg;
	}
      else if (pedantic && DECL_MAIN_P (arg))
	/* ARM $3.4 */
	pedwarn ("ISO C++ forbids taking address of function %<::main%>");

      /* Let &* cancel out to simplify resulting code.  */
      if (TREE_CODE (arg) == INDIRECT_REF)
	{
	  /* We don't need to have `current_class_ptr' wrapped in a
	     NON_LVALUE_EXPR node.  */
	  if (arg == current_class_ref)
	    return current_class_ptr;

	  arg = TREE_OPERAND (arg, 0);
	  if (TREE_CODE (TREE_TYPE (arg)) == REFERENCE_TYPE)
	    {
	      tree type = build_pointer_type (TREE_TYPE (TREE_TYPE (arg)));
	      arg = build1 (CONVERT_EXPR, type, arg);
	    }
	  else
	    /* Don't let this be an lvalue.  */
	    arg = rvalue (arg);
	  return arg;
	}

      /* Uninstantiated types are all functions.  Taking the
	 address of a function is a no-op, so just return the
	 argument.  */

      gcc_assert (TREE_CODE (arg) != IDENTIFIER_NODE
		  || !IDENTIFIER_OPNAME_P (arg));

      if (TREE_CODE (arg) == COMPONENT_REF && type_unknown_p (arg)
	  && !really_overloaded_fn (TREE_OPERAND (arg, 1)))
	{
	  /* They're trying to take the address of a unique non-static
	     member function.  This is ill-formed (except in MS-land),
	     but let's try to DTRT.
	     Note: We only handle unique functions here because we don't
	     want to complain if there's a static overload; non-unique
	     cases will be handled by instantiate_type.  But we need to
	     handle this case here to allow casts on the resulting PMF.
	     We could defer this in non-MS mode, but it's easier to give
	     a useful error here.  */

	  /* Inside constant member functions, the `this' pointer
	     contains an extra const qualifier.  TYPE_MAIN_VARIANT
	     is used here to remove this const from the diagnostics
	     and the created OFFSET_REF.  */
	  tree base = TYPE_MAIN_VARIANT (TREE_TYPE (TREE_OPERAND (arg, 0)));
	  tree fn = get_first_fn (TREE_OPERAND (arg, 1));
	  mark_used (fn);

	  if (! flag_ms_extensions)
	    {
	      tree name = DECL_NAME (fn);
	      if (current_class_type
		  && TREE_OPERAND (arg, 0) == current_class_ref)
		/* An expression like &memfn.  */
		pedwarn ("ISO C++ forbids taking the address of an unqualified"
			 " or parenthesized non-static member function to form"
			 " a pointer to member function.  Say %<&%T::%D%>",
			 base, name);
	      else
		pedwarn ("ISO C++ forbids taking the address of a bound member"
			 " function to form a pointer to member function."
			 "  Say %<&%T::%D%>",
			 base, name);
	    }
	  arg = build_offset_ref (base, fn, /*address_p=*/true);
	}

    offset_ref:
      if (type_unknown_p (arg))
	return build1 (ADDR_EXPR, unknown_type_node, arg);

      /* Handle complex lvalues (when permitted)
	 by reduction to simpler cases.  */
      val = unary_complex_lvalue (code, arg);
      if (val != 0)
	return val;

      switch (TREE_CODE (arg))
	{
	case NOP_EXPR:
	case CONVERT_EXPR:
	case FLOAT_EXPR:
	case FIX_TRUNC_EXPR:
	case FIX_FLOOR_EXPR:
	case FIX_ROUND_EXPR:
	case FIX_CEIL_EXPR:
	  if (! lvalue_p (arg) && pedantic)
	    pedwarn ("ISO C++ forbids taking the address of a cast to a non-lvalue expression");
	  break;

	case BASELINK:
	  arg = BASELINK_FUNCTIONS (arg);
	  /* Fall through.  */

	case OVERLOAD:
	  arg = OVL_CURRENT (arg);
	  break;

	case OFFSET_REF:
	  /* Turn a reference to a non-static data member into a
	     pointer-to-member.  */
	  {
	    tree type;
	    tree t;

	    if (!PTRMEM_OK_P (arg))
	      return build_unary_op (code, arg, 0);

	    t = TREE_OPERAND (arg, 1);
	    if (TREE_CODE (TREE_TYPE (t)) == REFERENCE_TYPE)
	      {
		error ("cannot create pointer to reference member %qD", t);
		return error_mark_node;
	      }

	    type = build_ptrmem_type (context_for_name_lookup (t),
				      TREE_TYPE (t));
	    t = make_ptrmem_cst (type, TREE_OPERAND (arg, 1));
	    return t;
	  }

	default:
	  break;
	}

      /* Anything not already handled and not a true memory reference
	 is an error.  */
      if (TREE_CODE (argtype) != FUNCTION_TYPE
	  && TREE_CODE (argtype) != METHOD_TYPE
	  && TREE_CODE (arg) != OFFSET_REF
	  && !lvalue_or_else (arg, lv_addressof))
	return error_mark_node;

      if (argtype != error_mark_node)
	argtype = build_pointer_type (argtype);

      /* In a template, we are processing a non-dependent expression
	 so we can just form an ADDR_EXPR with the correct type.  */
      if (processing_template_decl)
	{
	  val = build_address (arg);
	  if (TREE_CODE (arg) == OFFSET_REF)
	    PTRMEM_OK_P (val) = PTRMEM_OK_P (arg);
	  return val;
	}

      if (TREE_CODE (arg) != COMPONENT_REF)
	{
	  val = build_address (arg);
	  if (TREE_CODE (arg) == OFFSET_REF)
	    PTRMEM_OK_P (val) = PTRMEM_OK_P (arg);
	}
      else if (TREE_CODE (TREE_OPERAND (arg, 1)) == BASELINK)
	{
	  tree fn = BASELINK_FUNCTIONS (TREE_OPERAND (arg, 1));

	  /* We can only get here with a single static member
	     function.  */
	  gcc_assert (TREE_CODE (fn) == FUNCTION_DECL
		      && DECL_STATIC_FUNCTION_P (fn));
	  mark_used (fn);
	  val = build_address (fn);
	  if (TREE_SIDE_EFFECTS (TREE_OPERAND (arg, 0)))
	    /* Do not lose object's side effects.  */
	    val = build2 (COMPOUND_EXPR, TREE_TYPE (val),
			  TREE_OPERAND (arg, 0), val);
	}
      else if (DECL_C_BIT_FIELD (TREE_OPERAND (arg, 1)))
	{
	  error ("attempt to take address of bit-field structure member %qD",
		 TREE_OPERAND (arg, 1));
	  return error_mark_node;
	}
      else
	{
	  tree object = TREE_OPERAND (arg, 0);
	  tree field = TREE_OPERAND (arg, 1);
	  gcc_assert (same_type_ignoring_top_level_qualifiers_p
		      (TREE_TYPE (object), decl_type_context (field)));
	  val = build_address (arg);
	}

      if (TREE_CODE (argtype) == POINTER_TYPE
	  && TREE_CODE (TREE_TYPE (argtype)) == METHOD_TYPE)
	{
	  build_ptrmemfunc_type (argtype);
	  val = build_ptrmemfunc (argtype, val, 0,
				  /*c_cast_p=*/false);
	}

      return val;

    default:
      break;
    }

  if (!errstring)
    {
      if (argtype == 0)
	argtype = TREE_TYPE (arg);
      return fold_if_not_in_template (build1 (code, argtype, arg));
    }

  error ("%s", errstring);
  return error_mark_node;
}

/* Apply unary lvalue-demanding operator CODE to the expression ARG
   for certain kinds of expressions which are not really lvalues
   but which we can accept as lvalues.

   If ARG is not a kind of expression we can handle, return
   NULL_TREE.  */

tree
unary_complex_lvalue (enum tree_code code, tree arg)
{
  /* Inside a template, making these kinds of adjustments is
     pointless; we are only concerned with the type of the
     expression.  */
  if (processing_template_decl)
    return NULL_TREE;

  /* Handle (a, b) used as an "lvalue".  */
  if (TREE_CODE (arg) == COMPOUND_EXPR)
    {
      tree real_result = build_unary_op (code, TREE_OPERAND (arg, 1), 0);
      return build2 (COMPOUND_EXPR, TREE_TYPE (real_result),
		     TREE_OPERAND (arg, 0), real_result);
    }

  /* Handle (a ? b : c) used as an "lvalue".  */
  if (TREE_CODE (arg) == COND_EXPR
      || TREE_CODE (arg) == MIN_EXPR || TREE_CODE (arg) == MAX_EXPR)
    return rationalize_conditional_expr (code, arg);

  /* Handle (a = b), (++a), and (--a) used as an "lvalue".  */
  if (TREE_CODE (arg) == MODIFY_EXPR
      || TREE_CODE (arg) == PREINCREMENT_EXPR
      || TREE_CODE (arg) == PREDECREMENT_EXPR)
    {
      tree lvalue = TREE_OPERAND (arg, 0);
      if (TREE_SIDE_EFFECTS (lvalue))
	{
	  lvalue = stabilize_reference (lvalue);
	  arg = build2 (TREE_CODE (arg), TREE_TYPE (arg),
			lvalue, TREE_OPERAND (arg, 1));
	}
      return unary_complex_lvalue
	(code, build2 (COMPOUND_EXPR, TREE_TYPE (lvalue), arg, lvalue));
    }

  if (code != ADDR_EXPR)
    return NULL_TREE;

  /* Handle (a = b) used as an "lvalue" for `&'.  */
  if (TREE_CODE (arg) == MODIFY_EXPR
      || TREE_CODE (arg) == INIT_EXPR)
    {
      tree real_result = build_unary_op (code, TREE_OPERAND (arg, 0), 0);
      arg = build2 (COMPOUND_EXPR, TREE_TYPE (real_result),
		    arg, real_result);
      TREE_NO_WARNING (arg) = 1;
      return arg;
    }

  if (TREE_CODE (TREE_TYPE (arg)) == FUNCTION_TYPE
      || TREE_CODE (TREE_TYPE (arg)) == METHOD_TYPE
      || TREE_CODE (arg) == OFFSET_REF)
    return NULL_TREE;

  /* We permit compiler to make function calls returning
     objects of aggregate type look like lvalues.  */
  {
    tree targ = arg;

    if (TREE_CODE (targ) == SAVE_EXPR)
      targ = TREE_OPERAND (targ, 0);

    if (TREE_CODE (targ) == CALL_EXPR && IS_AGGR_TYPE (TREE_TYPE (targ)))
      {
	if (TREE_CODE (arg) == SAVE_EXPR)
	  targ = arg;
	else
	  targ = build_cplus_new (TREE_TYPE (arg), arg);
	return build1 (ADDR_EXPR, build_pointer_type (TREE_TYPE (arg)), targ);
      }

    if (TREE_CODE (arg) == SAVE_EXPR && TREE_CODE (targ) == INDIRECT_REF)
      return build3 (SAVE_EXPR, build_pointer_type (TREE_TYPE (arg)),
		     TREE_OPERAND (targ, 0), current_function_decl, NULL);
  }

  /* Don't let anything else be handled specially.  */
  return NULL_TREE;
}

/* Mark EXP saying that we need to be able to take the
   address of it; it should not be allocated in a register.
   Value is true if successful.

   C++: we do not allow `current_class_ptr' to be addressable.  */

bool
cxx_mark_addressable (tree exp)
{
  tree x = exp;

  while (1)
    switch (TREE_CODE (x))
      {
      case ADDR_EXPR:
      case COMPONENT_REF:
      case ARRAY_REF:
      case REALPART_EXPR:
      case IMAGPART_EXPR:
	x = TREE_OPERAND (x, 0);
	break;

      case PARM_DECL:
	if (x == current_class_ptr)
	  {
	    error ("cannot take the address of %<this%>, which is an rvalue expression");
	    TREE_ADDRESSABLE (x) = 1; /* so compiler doesn't die later.  */
	    return true;
	  }
	/* Fall through.  */

      case VAR_DECL:
	/* Caller should not be trying to mark initialized
	   constant fields addressable.  */
	gcc_assert (DECL_LANG_SPECIFIC (x) == 0
		    || DECL_IN_AGGR_P (x) == 0
		    || TREE_STATIC (x)
		    || DECL_EXTERNAL (x));
	/* Fall through.  */

      case CONST_DECL:
      case RESULT_DECL:
	if (DECL_REGISTER (x) && !TREE_ADDRESSABLE (x)
	    && !DECL_ARTIFICIAL (x))
	  {
	    if (TREE_CODE (x) == VAR_DECL && DECL_HARD_REGISTER (x))
	      {
		error
		  ("address of explicit register variable %qD requested", x);
		return false;
	      }
	    else if (extra_warnings)
	      warning
		(OPT_Wextra, "address requested for %qD, which is declared %<register%>", x);
	  }
	TREE_ADDRESSABLE (x) = 1;
	return true;

      case FUNCTION_DECL:
	TREE_ADDRESSABLE (x) = 1;
	return true;

      case CONSTRUCTOR:
	TREE_ADDRESSABLE (x) = 1;
	return true;

      case TARGET_EXPR:
	TREE_ADDRESSABLE (x) = 1;
	cxx_mark_addressable (TREE_OPERAND (x, 0));
	return true;

      default:
	return true;
    }
}

/* Build and return a conditional expression IFEXP ? OP1 : OP2.  */

tree
build_x_conditional_expr (tree ifexp, tree op1, tree op2)
{
  tree orig_ifexp = ifexp;
  tree orig_op1 = op1;
  tree orig_op2 = op2;
  tree expr;

  if (processing_template_decl)
    {
      /* The standard says that the expression is type-dependent if
	 IFEXP is type-dependent, even though the eventual type of the
	 expression doesn't dependent on IFEXP.  */
      if (type_dependent_expression_p (ifexp)
	  /* As a GNU extension, the middle operand may be omitted.  */
	  || (op1 && type_dependent_expression_p (op1))
	  || type_dependent_expression_p (op2))
	return build_min_nt (COND_EXPR, ifexp, op1, op2);
      ifexp = build_non_dependent_expr (ifexp);
      if (op1)
	op1 = build_non_dependent_expr (op1);
      op2 = build_non_dependent_expr (op2);
    }

  expr = build_conditional_expr (ifexp, op1, op2);
  if (processing_template_decl && expr != error_mark_node)
    return build_min_non_dep (COND_EXPR, expr,
			      orig_ifexp, orig_op1, orig_op2);
  return expr;
}

/* Given a list of expressions, return a compound expression
   that performs them all and returns the value of the last of them.  */

tree build_x_compound_expr_from_list (tree list, const char *msg)
{
  tree expr = TREE_VALUE (list);

  if (TREE_CHAIN (list))
    {
      if (msg)
	pedwarn ("%s expression list treated as compound expression", msg);

      for (list = TREE_CHAIN (list); list; list = TREE_CHAIN (list))
	expr = build_x_compound_expr (expr, TREE_VALUE (list));
    }

  return expr;
}

/* Handle overloading of the ',' operator when needed.  */

tree
build_x_compound_expr (tree op1, tree op2)
{
  tree result;
  tree orig_op1 = op1;
  tree orig_op2 = op2;

  if (processing_template_decl)
    {
      if (type_dependent_expression_p (op1)
	  || type_dependent_expression_p (op2))
	return build_min_nt (COMPOUND_EXPR, op1, op2);
      op1 = build_non_dependent_expr (op1);
      op2 = build_non_dependent_expr (op2);
    }

  result = build_new_op (COMPOUND_EXPR, LOOKUP_NORMAL, op1, op2, NULL_TREE,
			 /*overloaded_p=*/NULL);
  if (!result)
    result = build_compound_expr (op1, op2);

  if (processing_template_decl && result != error_mark_node)
    return build_min_non_dep (COMPOUND_EXPR, result, orig_op1, orig_op2);

  return result;
}

/* Build a compound expression.  */

tree
build_compound_expr (tree lhs, tree rhs)
{
  lhs = convert_to_void (lhs, "left-hand operand of comma");

  if (lhs == error_mark_node || rhs == error_mark_node)
    return error_mark_node;

  if (TREE_CODE (rhs) == TARGET_EXPR)
    {
      /* If the rhs is a TARGET_EXPR, then build the compound
	 expression inside the target_expr's initializer. This
	 helps the compiler to eliminate unnecessary temporaries.  */
      tree init = TREE_OPERAND (rhs, 1);

      init = build2 (COMPOUND_EXPR, TREE_TYPE (init), lhs, init);
      TREE_OPERAND (rhs, 1) = init;

      return rhs;
    }

  return build2 (COMPOUND_EXPR, TREE_TYPE (rhs), lhs, rhs);
}

/* Issue a diagnostic message if casting from SRC_TYPE to DEST_TYPE
   casts away constness.  DIAG_FN gives the function to call if we
   need to issue a diagnostic; if it is NULL, no diagnostic will be
   issued.  DESCRIPTION explains what operation is taking place.  */

static void
check_for_casting_away_constness (tree src_type, tree dest_type,
				  void (*diag_fn)(const char *, ...) ATTRIBUTE_GCC_CXXDIAG(1,2),
				  const char *description)
{
  if (diag_fn && casts_away_constness (src_type, dest_type))
    diag_fn ("%s from type %qT to type %qT casts away constness",
	     description, src_type, dest_type);
}

/* Convert EXPR (an expression with pointer-to-member type) to TYPE
   (another pointer-to-member type in the same hierarchy) and return
   the converted expression.  If ALLOW_INVERSE_P is permitted, a
   pointer-to-derived may be converted to pointer-to-base; otherwise,
   only the other direction is permitted.  If C_CAST_P is true, this
   conversion is taking place as part of a C-style cast.  */

tree
convert_ptrmem (tree type, tree expr, bool allow_inverse_p,
		bool c_cast_p)
{
  if (TYPE_PTRMEM_P (type))
    {
      tree delta;

      if (TREE_CODE (expr) == PTRMEM_CST)
	expr = cplus_expand_constant (expr);
      delta = get_delta_difference (TYPE_PTRMEM_CLASS_TYPE (TREE_TYPE (expr)),
				    TYPE_PTRMEM_CLASS_TYPE (type),
				    allow_inverse_p,
				    c_cast_p);
      if (!integer_zerop (delta))
	expr = cp_build_binary_op (PLUS_EXPR,
				   build_nop (ptrdiff_type_node, expr),
				   delta);
      return build_nop (type, expr);
    }
  else
    return build_ptrmemfunc (TYPE_PTRMEMFUNC_FN_TYPE (type), expr,
			     allow_inverse_p, c_cast_p);
}

/* If EXPR is an INTEGER_CST and ORIG is an arithmetic constant, return
   a version of EXPR that has TREE_OVERFLOW and/or TREE_CONSTANT_OVERFLOW
   set iff they are set in ORIG.  Otherwise, return EXPR unchanged.  */

static tree
ignore_overflows (tree expr, tree orig)
{
  if (TREE_CODE (expr) == INTEGER_CST
      && CONSTANT_CLASS_P (orig)
      && TREE_CODE (orig) != STRING_CST
      && (TREE_OVERFLOW (expr) != TREE_OVERFLOW (orig)
	  || TREE_CONSTANT_OVERFLOW (expr)
	     != TREE_CONSTANT_OVERFLOW (orig)))
    {
      if (!TREE_OVERFLOW (orig) && !TREE_CONSTANT_OVERFLOW (orig))
	/* Ensure constant sharing.  */
	expr = build_int_cst_wide (TREE_TYPE (expr),
				   TREE_INT_CST_LOW (expr),
				   TREE_INT_CST_HIGH (expr));
      else
	{
	  /* Avoid clobbering a shared constant.  */
	  expr = copy_node (expr);
	  TREE_OVERFLOW (expr) = TREE_OVERFLOW (orig);
	  TREE_CONSTANT_OVERFLOW (expr)
	    = TREE_CONSTANT_OVERFLOW (orig);
	}
    }
  return expr;
}

/* Perform a static_cast from EXPR to TYPE.  When C_CAST_P is true,
   this static_cast is being attempted as one of the possible casts
   allowed by a C-style cast.  (In that case, accessibility of base
   classes is not considered, and it is OK to cast away
   constness.)  Return the result of the cast.  *VALID_P is set to
   indicate whether or not the cast was valid.  */

static tree
build_static_cast_1 (tree type, tree expr, bool c_cast_p,
		     bool *valid_p)
{
  tree intype;
  tree result;
  tree orig;
  void (*diag_fn)(const char*, ...) ATTRIBUTE_GCC_CXXDIAG(1,2);
  const char *desc;

  /* Assume the cast is valid.  */
  *valid_p = true;

  intype = TREE_TYPE (expr);

  /* Save casted types in the function's used types hash table.  */
  used_types_insert (type);

  /* Determine what to do when casting away constness.  */
  if (c_cast_p)
    {
      /* C-style casts are allowed to cast away constness.  With
	 WARN_CAST_QUAL, we still want to issue a warning.  */
      diag_fn = warn_cast_qual ? warning0 : NULL;
      desc = "cast";
    }
  else
    {
      /* A static_cast may not cast away constness.  */
      diag_fn = error;
      desc = "static_cast";
    }

  /* [expr.static.cast]

     An lvalue of type "cv1 B", where B is a class type, can be cast
     to type "reference to cv2 D", where D is a class derived (clause
     _class.derived_) from B, if a valid standard conversion from
     "pointer to D" to "pointer to B" exists (_conv.ptr_), cv2 is the
     same cv-qualification as, or greater cv-qualification than, cv1,
     and B is not a virtual base class of D.  */
  /* We check this case before checking the validity of "TYPE t =
     EXPR;" below because for this case:

       struct B {};
       struct D : public B { D(const B&); };
       extern B& b;
       void f() { static_cast<const D&>(b); }

     we want to avoid constructing a new D.  The standard is not
     completely clear about this issue, but our interpretation is
     consistent with other compilers.  */
  if (TREE_CODE (type) == REFERENCE_TYPE
      && CLASS_TYPE_P (TREE_TYPE (type))
      && CLASS_TYPE_P (intype)
      && real_lvalue_p (expr)
      && DERIVED_FROM_P (intype, TREE_TYPE (type))
      && can_convert (build_pointer_type (TYPE_MAIN_VARIANT (intype)),
		      build_pointer_type (TYPE_MAIN_VARIANT
					  (TREE_TYPE (type))))
      && (c_cast_p
	  || at_least_as_qualified_p (TREE_TYPE (type), intype)))
    {
      tree base;

      /* There is a standard conversion from "D*" to "B*" even if "B"
	 is ambiguous or inaccessible.  If this is really a
	 static_cast, then we check both for inaccessibility and
	 ambiguity.  However, if this is a static_cast being performed
	 because the user wrote a C-style cast, then accessibility is
	 not considered.  */
      base = lookup_base (TREE_TYPE (type), intype,
			  c_cast_p ? ba_unique : ba_check,
			  NULL);

      /* Convert from "B*" to "D*".  This function will check that "B"
	 is not a virtual base of "D".  */
      expr = build_base_path (MINUS_EXPR, build_address (expr),
			      base, /*nonnull=*/false);
      /* Convert the pointer to a reference -- but then remember that
	 there are no expressions with reference type in C++.  */
      return convert_from_reference (build_nop (type, expr));
    }

  orig = expr;

  /* [expr.static.cast]

     An expression e can be explicitly converted to a type T using a
     static_cast of the form static_cast<T>(e) if the declaration T
     t(e);" is well-formed, for some invented temporary variable
     t.  */
  result = perform_direct_initialization_if_possible (type, expr,
						      c_cast_p);
  if (result)
    {
      result = convert_from_reference (result);

      /* Ignore any integer overflow caused by the cast.  */
      result = ignore_overflows (result, orig);

      /* [expr.static.cast]

	 If T is a reference type, the result is an lvalue; otherwise,
	 the result is an rvalue.  */
      if (TREE_CODE (type) != REFERENCE_TYPE)
	result = rvalue (result);
      return result;
    }

  /* [expr.static.cast]

     Any expression can be explicitly converted to type cv void.  */
  if (TREE_CODE (type) == VOID_TYPE)
    return convert_to_void (expr, /*implicit=*/NULL);

  /* [expr.static.cast]

     The inverse of any standard conversion sequence (clause _conv_),
     other than the lvalue-to-rvalue (_conv.lval_), array-to-pointer
     (_conv.array_), function-to-pointer (_conv.func_), and boolean
     (_conv.bool_) conversions, can be performed explicitly using
     static_cast subject to the restriction that the explicit
     conversion does not cast away constness (_expr.const.cast_), and
     the following additional rules for specific cases:  */
  /* For reference, the conversions not excluded are: integral
     promotions, floating point promotion, integral conversions,
     floating point conversions, floating-integral conversions,
     pointer conversions, and pointer to member conversions.  */
  /* DR 128

     A value of integral _or enumeration_ type can be explicitly
     converted to an enumeration type.  */
  /* The effect of all that is that any conversion between any two
     types which are integral, floating, or enumeration types can be
     performed.  */
  if ((INTEGRAL_TYPE_P (type) || SCALAR_FLOAT_TYPE_P (type))
      && (INTEGRAL_TYPE_P (intype) || SCALAR_FLOAT_TYPE_P (intype)))
    {
      expr = ocp_convert (type, expr, CONV_C_CAST, LOOKUP_NORMAL);

      /* Ignore any integer overflow caused by the cast.  */
      expr = ignore_overflows (expr, orig);
      return expr;
    }

  if (TYPE_PTR_P (type) && TYPE_PTR_P (intype)
      && CLASS_TYPE_P (TREE_TYPE (type))
      && CLASS_TYPE_P (TREE_TYPE (intype))
      && can_convert (build_pointer_type (TYPE_MAIN_VARIANT
					  (TREE_TYPE (intype))),
		      build_pointer_type (TYPE_MAIN_VARIANT
					  (TREE_TYPE (type)))))
    {
      tree base;

      if (!c_cast_p)
	check_for_casting_away_constness (intype, type, diag_fn, desc);
      base = lookup_base (TREE_TYPE (type), TREE_TYPE (intype),
			  c_cast_p ? ba_unique : ba_check,
			  NULL);
      return build_base_path (MINUS_EXPR, expr, base, /*nonnull=*/false);
    }

  if ((TYPE_PTRMEM_P (type) && TYPE_PTRMEM_P (intype))
      || (TYPE_PTRMEMFUNC_P (type) && TYPE_PTRMEMFUNC_P (intype)))
    {
      tree c1;
      tree c2;
      tree t1;
      tree t2;

      c1 = TYPE_PTRMEM_CLASS_TYPE (intype);
      c2 = TYPE_PTRMEM_CLASS_TYPE (type);

      if (TYPE_PTRMEM_P (type))
	{
	  t1 = (build_ptrmem_type
		(c1,
		 TYPE_MAIN_VARIANT (TYPE_PTRMEM_POINTED_TO_TYPE (intype))));
	  t2 = (build_ptrmem_type
		(c2,
		 TYPE_MAIN_VARIANT (TYPE_PTRMEM_POINTED_TO_TYPE (type))));
	}
      else
	{
	  t1 = intype;
	  t2 = type;
	}
      if (can_convert (t1, t2))
	{
	  if (!c_cast_p)
	    check_for_casting_away_constness (intype, type, diag_fn,
					      desc);
	  return convert_ptrmem (type, expr, /*allow_inverse_p=*/1,
				 c_cast_p);
	}
    }

  /* [expr.static.cast]

     An rvalue of type "pointer to cv void" can be explicitly
     converted to a pointer to object type.  A value of type pointer
     to object converted to "pointer to cv void" and back to the
     original pointer type will have its original value.  */
  if (TREE_CODE (intype) == POINTER_TYPE
      && VOID_TYPE_P (TREE_TYPE (intype))
      && TYPE_PTROB_P (type))
    {
      if (!c_cast_p)
	check_for_casting_away_constness (intype, type, diag_fn, desc);
      return build_nop (type, expr);
    }

  *valid_p = false;
  return error_mark_node;
}

/* Return an expression representing static_cast<TYPE>(EXPR).  */

tree
build_static_cast (tree type, tree expr)
{
  tree result;
  bool valid_p;

  if (type == error_mark_node || expr == error_mark_node)
    return error_mark_node;

  if (processing_template_decl)
    {
      expr = build_min (STATIC_CAST_EXPR, type, expr);
      /* We don't know if it will or will not have side effects.  */
      TREE_SIDE_EFFECTS (expr) = 1;
      return convert_from_reference (expr);
    }

  /* build_c_cast puts on a NOP_EXPR to make the result not an lvalue.
     Strip such NOP_EXPRs if VALUE is being used in non-lvalue context.  */
  if (TREE_CODE (type) != REFERENCE_TYPE
      && TREE_CODE (expr) == NOP_EXPR
      && TREE_TYPE (expr) == TREE_TYPE (TREE_OPERAND (expr, 0)))
    expr = TREE_OPERAND (expr, 0);

  result = build_static_cast_1 (type, expr, /*c_cast_p=*/false, &valid_p);
  if (valid_p)
    return result;

  error ("invalid static_cast from type %qT to type %qT",
	 TREE_TYPE (expr), type);
  return error_mark_node;
}

/* EXPR is an expression with member function or pointer-to-member
   function type.  TYPE is a pointer type.  Converting EXPR to TYPE is
   not permitted by ISO C++, but we accept it in some modes.  If we
   are not in one of those modes, issue a diagnostic.  Return the
   converted expression.  */

tree
convert_member_func_to_ptr (tree type, tree expr)
{
  tree intype;
  tree decl;

  intype = TREE_TYPE (expr);
  gcc_assert (TYPE_PTRMEMFUNC_P (intype)
	      || TREE_CODE (intype) == METHOD_TYPE);

  if (pedantic || warn_pmf2ptr)
    pedwarn ("converting from %qT to %qT", intype, type);

  if (TREE_CODE (intype) == METHOD_TYPE)
    expr = build_addr_func (expr);
  else if (TREE_CODE (expr) == PTRMEM_CST)
    expr = build_address (PTRMEM_CST_MEMBER (expr));
  else
    {
      decl = maybe_dummy_object (TYPE_PTRMEM_CLASS_TYPE (intype), 0);
      decl = build_address (decl);
      expr = get_member_function_from_ptrfunc (&decl, expr);
    }

  return build_nop (type, expr);
}

/* Return a representation for a reinterpret_cast from EXPR to TYPE.
   If C_CAST_P is true, this reinterpret cast is being done as part of
   a C-style cast.  If VALID_P is non-NULL, *VALID_P is set to
   indicate whether or not reinterpret_cast was valid.  */

static tree
build_reinterpret_cast_1 (tree type, tree expr, bool c_cast_p,
			  bool *valid_p)
{
  tree intype;

  /* Assume the cast is invalid.  */
  if (valid_p)
    *valid_p = true;

  if (type == error_mark_node || error_operand_p (expr))
    return error_mark_node;

  intype = TREE_TYPE (expr);

  /* Save casted types in the function's used types hash table.  */
  used_types_insert (type);

  /* [expr.reinterpret.cast]
     An lvalue expression of type T1 can be cast to the type
     "reference to T2" if an expression of type "pointer to T1" can be
     explicitly converted to the type "pointer to T2" using a
     reinterpret_cast.  */
  if (TREE_CODE (type) == REFERENCE_TYPE)
    {
      if (! real_lvalue_p (expr))
	{
	  error ("invalid cast of an rvalue expression of type "
		 "%qT to type %qT",
		 intype, type);
	  return error_mark_node;
	}

      /* Warn about a reinterpret_cast from "A*" to "B&" if "A" and
	 "B" are related class types; the reinterpret_cast does not
	 adjust the pointer.  */
      if (TYPE_PTR_P (intype)
	  && (comptypes (TREE_TYPE (intype), TREE_TYPE (type),
			 COMPARE_BASE | COMPARE_DERIVED)))
	warning (0, "casting %qT to %qT does not dereference pointer",
		 intype, type);

      expr = build_unary_op (ADDR_EXPR, expr, 0);
      if (expr != error_mark_node)
	expr = build_reinterpret_cast_1
	  (build_pointer_type (TREE_TYPE (type)), expr, c_cast_p,
	   valid_p);
      if (expr != error_mark_node)
	expr = build_indirect_ref (expr, 0);
      return expr;
    }

  /* As a G++ extension, we consider conversions from member
     functions, and pointers to member functions to
     pointer-to-function and pointer-to-void types.  If
     -Wno-pmf-conversions has not been specified,
     convert_member_func_to_ptr will issue an error message.  */
  if ((TYPE_PTRMEMFUNC_P (intype)
       || TREE_CODE (intype) == METHOD_TYPE)
      && TYPE_PTR_P (type)
      && (TREE_CODE (TREE_TYPE (type)) == FUNCTION_TYPE
	  || VOID_TYPE_P (TREE_TYPE (type))))
    return convert_member_func_to_ptr (type, expr);

  /* If the cast is not to a reference type, the lvalue-to-rvalue,
     array-to-pointer, and function-to-pointer conversions are
     performed.  */
  expr = decay_conversion (expr);

  /* build_c_cast puts on a NOP_EXPR to make the result not an lvalue.
     Strip such NOP_EXPRs if VALUE is being used in non-lvalue context.  */
  if (TREE_CODE (expr) == NOP_EXPR
      && TREE_TYPE (expr) == TREE_TYPE (TREE_OPERAND (expr, 0)))
    expr = TREE_OPERAND (expr, 0);

  if (error_operand_p (expr))
    return error_mark_node;

  intype = TREE_TYPE (expr);

  /* [expr.reinterpret.cast]
     A pointer can be converted to any integral type large enough to
     hold it.  */
  if (CP_INTEGRAL_TYPE_P (type) && TYPE_PTR_P (intype))
    {
      if (TYPE_PRECISION (type) < TYPE_PRECISION (intype))
	pedwarn ("cast from %qT to %qT loses precision",
		 intype, type);
    }
  /* [expr.reinterpret.cast]
     A value of integral or enumeration type can be explicitly
     converted to a pointer.  */
  else if (TYPE_PTR_P (type) && INTEGRAL_OR_ENUMERATION_TYPE_P (intype))
    /* OK */
    ;
  else if ((TYPE_PTRFN_P (type) && TYPE_PTRFN_P (intype))
	   || (TYPE_PTRMEMFUNC_P (type) && TYPE_PTRMEMFUNC_P (intype)))
    return fold_if_not_in_template (build_nop (type, expr));
  else if ((TYPE_PTRMEM_P (type) && TYPE_PTRMEM_P (intype))
	   || (TYPE_PTROBV_P (type) && TYPE_PTROBV_P (intype)))
    {
      tree sexpr = expr;

      if (!c_cast_p)
	check_for_casting_away_constness (intype, type, error,
					  "reinterpret_cast");
      /* Warn about possible alignment problems.  */
      if (STRICT_ALIGNMENT && warn_cast_align
	  && !VOID_TYPE_P (type)
	  && TREE_CODE (TREE_TYPE (intype)) != FUNCTION_TYPE
	  && COMPLETE_TYPE_P (TREE_TYPE (type))
	  && COMPLETE_TYPE_P (TREE_TYPE (intype))
	  && TYPE_ALIGN (TREE_TYPE (type)) > TYPE_ALIGN (TREE_TYPE (intype)))
	warning (0, "cast from %qT to %qT increases required alignment of "
		 "target type",
		 intype, type);

      /* We need to strip nops here, because the frontend likes to
	 create (int *)&a for array-to-pointer decay, instead of &a[0].  */
      STRIP_NOPS (sexpr);
      strict_aliasing_warning (intype, type, sexpr);

      return fold_if_not_in_template (build_nop (type, expr));
    }
  else if ((TYPE_PTRFN_P (type) && TYPE_PTROBV_P (intype))
	   || (TYPE_PTRFN_P (intype) && TYPE_PTROBV_P (type)))
    {
      if (pedantic)
	/* Only issue a warning, as we have always supported this
	   where possible, and it is necessary in some cases.  DR 195
	   addresses this issue, but as of 2004/10/26 is still in
	   drafting.  */
	warning (0, "ISO C++ forbids casting between pointer-to-function and pointer-to-object");
      return fold_if_not_in_template (build_nop (type, expr));
    }
  else if (TREE_CODE (type) == VECTOR_TYPE)
    return fold_if_not_in_template (convert_to_vector (type, expr));
  else if (TREE_CODE (intype) == VECTOR_TYPE && INTEGRAL_TYPE_P (type))
    return fold_if_not_in_template (convert_to_integer (type, expr));
  else
    {
      if (valid_p)
	*valid_p = false;
      error ("invalid cast from type %qT to type %qT", intype, type);
      return error_mark_node;
    }

  return cp_convert (type, expr);
}

tree
build_reinterpret_cast (tree type, tree expr)
{
  if (type == error_mark_node || expr == error_mark_node)
    return error_mark_node;

  if (processing_template_decl)
    {
      tree t = build_min (REINTERPRET_CAST_EXPR, type, expr);

      if (!TREE_SIDE_EFFECTS (t)
	  && type_dependent_expression_p (expr))
	/* There might turn out to be side effects inside expr.  */
	TREE_SIDE_EFFECTS (t) = 1;
      return convert_from_reference (t);
    }

  return build_reinterpret_cast_1 (type, expr, /*c_cast_p=*/false,
				   /*valid_p=*/NULL);
}

/* Perform a const_cast from EXPR to TYPE.  If the cast is valid,
   return an appropriate expression.  Otherwise, return
   error_mark_node.  If the cast is not valid, and COMPLAIN is true,
   then a diagnostic will be issued.  If VALID_P is non-NULL, we are
   performing a C-style cast, its value upon return will indicate
   whether or not the conversion succeeded.  */

static tree
build_const_cast_1 (tree dst_type, tree expr, bool complain,
		    bool *valid_p)
{
  tree src_type;
  tree reference_type;

  /* Callers are responsible for handling error_mark_node as a
     destination type.  */
  gcc_assert (dst_type != error_mark_node);
  /* In a template, callers should be building syntactic
     representations of casts, not using this machinery.  */
  gcc_assert (!processing_template_decl);

  /* Assume the conversion is invalid.  */
  if (valid_p)
    *valid_p = false;

  if (!POINTER_TYPE_P (dst_type) && !TYPE_PTRMEM_P (dst_type))
    {
      if (complain)
	error ("invalid use of const_cast with type %qT, "
	       "which is not a pointer, "
	       "reference, nor a pointer-to-data-member type", dst_type);
      return error_mark_node;
    }

  if (TREE_CODE (TREE_TYPE (dst_type)) == FUNCTION_TYPE)
    {
      if (complain)
	error ("invalid use of const_cast with type %qT, which is a pointer "
	       "or reference to a function type", dst_type);
      return error_mark_node;
    }

  /* Save casted types in the function's used types hash table.  */
  used_types_insert (dst_type);

  src_type = TREE_TYPE (expr);
  /* Expressions do not really have reference types.  */
  if (TREE_CODE (src_type) == REFERENCE_TYPE)
    src_type = TREE_TYPE (src_type);

  /* [expr.const.cast]

     An lvalue of type T1 can be explicitly converted to an lvalue of
     type T2 using the cast const_cast<T2&> (where T1 and T2 are object
     types) if a pointer to T1 can be explicitly converted to the type
     pointer to T2 using a const_cast.  */
  if (TREE_CODE (dst_type) == REFERENCE_TYPE)
    {
      reference_type = dst_type;
      if (! real_lvalue_p (expr))
	{
	  if (complain)
	    error ("invalid const_cast of an rvalue of type %qT to type %qT",
		   src_type, dst_type);
	  return error_mark_node;
	}
      dst_type = build_pointer_type (TREE_TYPE (dst_type));
      src_type = build_pointer_type (src_type);
    }
  else
    {
      reference_type = NULL_TREE;
      /* If the destination type is not a reference type, the
	 lvalue-to-rvalue, array-to-pointer, and function-to-pointer
	 conversions are performed.  */
      src_type = type_decays_to (src_type);
      if (src_type == error_mark_node)
	return error_mark_node;
    }

  if ((TYPE_PTR_P (src_type) || TYPE_PTRMEM_P (src_type))
      && comp_ptr_ttypes_const (dst_type, src_type))
    {
      if (valid_p)
	{
	  *valid_p = true;
	  /* This cast is actually a C-style cast.  Issue a warning if
	     the user is making a potentially unsafe cast.  */
	  if (warn_cast_qual)
	    check_for_casting_away_constness (src_type, dst_type,
					      warning0,
					      "cast");
	}
      if (reference_type)
	{
	  expr = build_unary_op (ADDR_EXPR, expr, 0);
	  expr = build_nop (reference_type, expr);
	  return convert_from_reference (expr);
	}
      else
	{
	  expr = decay_conversion (expr);
	  /* build_c_cast puts on a NOP_EXPR to make the result not an
	     lvalue.  Strip such NOP_EXPRs if VALUE is being used in
	     non-lvalue context.  */
	  if (TREE_CODE (expr) == NOP_EXPR
	      && TREE_TYPE (expr) == TREE_TYPE (TREE_OPERAND (expr, 0)))
	    expr = TREE_OPERAND (expr, 0);
	  return build_nop (dst_type, expr);
	}
    }

  if (complain)
    error ("invalid const_cast from type %qT to type %qT",
	   src_type, dst_type);
  return error_mark_node;
}

tree
build_const_cast (tree type, tree expr)
{
  if (type == error_mark_node || error_operand_p (expr))
    return error_mark_node;

  if (processing_template_decl)
    {
      tree t = build_min (CONST_CAST_EXPR, type, expr);

      if (!TREE_SIDE_EFFECTS (t)
	  && type_dependent_expression_p (expr))
	/* There might turn out to be side effects inside expr.  */
	TREE_SIDE_EFFECTS (t) = 1;
      return convert_from_reference (t);
    }

  return build_const_cast_1 (type, expr, /*complain=*/true,
			     /*valid_p=*/NULL);
}

/* Build an expression representing an explicit C-style cast to type
   TYPE of expression EXPR.  */

tree
build_c_cast (tree type, tree expr)
{
  tree value = expr;
  tree result;
  bool valid_p;

  if (type == error_mark_node || error_operand_p (expr))
    return error_mark_node;

  if (processing_template_decl)
    {
      tree t = build_min (CAST_EXPR, type,
			  tree_cons (NULL_TREE, value, NULL_TREE));
      /* We don't know if it will or will not have side effects.  */
      TREE_SIDE_EFFECTS (t) = 1;
      return convert_from_reference (t);
    }

  /* Casts to a (pointer to a) specific ObjC class (or 'id' or
     'Class') should always be retained, because this information aids
     in method lookup.  */
  if (objc_is_object_ptr (type)
      && objc_is_object_ptr (TREE_TYPE (expr)))
    return build_nop (type, expr);

  /* build_c_cast puts on a NOP_EXPR to make the result not an lvalue.
     Strip such NOP_EXPRs if VALUE is being used in non-lvalue context.  */
  if (TREE_CODE (type) != REFERENCE_TYPE
      && TREE_CODE (value) == NOP_EXPR
      && TREE_TYPE (value) == TREE_TYPE (TREE_OPERAND (value, 0)))
    value = TREE_OPERAND (value, 0);

  if (TREE_CODE (type) == ARRAY_TYPE)
    {
      /* Allow casting from T1* to T2[] because Cfront allows it.
	 NIHCL uses it. It is not valid ISO C++ however.  */
      if (TREE_CODE (TREE_TYPE (expr)) == POINTER_TYPE)
	{
	  pedwarn ("ISO C++ forbids casting to an array type %qT", type);
	  type = build_pointer_type (TREE_TYPE (type));
	}
      else
	{
	  error ("ISO C++ forbids casting to an array type %qT", type);
	  return error_mark_node;
	}
    }

  if (TREE_CODE (type) == FUNCTION_TYPE
      || TREE_CODE (type) == METHOD_TYPE)
    {
      error ("invalid cast to function type %qT", type);
      return error_mark_node;
    }

  /* A C-style cast can be a const_cast.  */
  result = build_const_cast_1 (type, value, /*complain=*/false,
			       &valid_p);
  if (valid_p)
    return result;

  /* Or a static cast.  */
  result = build_static_cast_1 (type, value, /*c_cast_p=*/true,
				&valid_p);
  /* Or a reinterpret_cast.  */
  if (!valid_p)
    result = build_reinterpret_cast_1 (type, value, /*c_cast_p=*/true,
				       &valid_p);
  /* The static_cast or reinterpret_cast may be followed by a
     const_cast.  */
  if (valid_p
      /* A valid cast may result in errors if, for example, a
	 conversion to am ambiguous base class is required.  */
      && !error_operand_p (result))
    {
      tree result_type;

      /* Non-class rvalues always have cv-unqualified type.  */
      if (!CLASS_TYPE_P (type))
	type = TYPE_MAIN_VARIANT (type);
      result_type = TREE_TYPE (result);
      if (!CLASS_TYPE_P (result_type))
	result_type = TYPE_MAIN_VARIANT (result_type);
      /* If the type of RESULT does not match TYPE, perform a
	 const_cast to make it match.  If the static_cast or
	 reinterpret_cast succeeded, we will differ by at most
	 cv-qualification, so the follow-on const_cast is guaranteed
	 to succeed.  */
      if (!same_type_p (non_reference (type), non_reference (result_type)))
	{
	  result = build_const_cast_1 (type, result, false, &valid_p);
	  gcc_assert (valid_p);
	}
      return result;
    }

  return error_mark_node;
}

/* Build an assignment expression of lvalue LHS from value RHS.
   MODIFYCODE is the code for a binary operator that we use
   to combine the old value of LHS with RHS to get the new value.
   Or else MODIFYCODE is NOP_EXPR meaning do a simple assignment.

   C++: If MODIFYCODE is INIT_EXPR, then leave references unbashed.  */

tree
build_modify_expr (tree lhs, enum tree_code modifycode, tree rhs)
{
  tree result;
  tree newrhs = rhs;
  tree lhstype = TREE_TYPE (lhs);
  tree olhstype = lhstype;
  tree olhs = NULL_TREE;
  bool plain_assign = (modifycode == NOP_EXPR);

  /* Avoid duplicate error messages from operands that had errors.  */
  if (error_operand_p (lhs) || error_operand_p (rhs))
    return error_mark_node;

  /* Handle control structure constructs used as "lvalues".  */
  switch (TREE_CODE (lhs))
    {
      /* Handle --foo = 5; as these are valid constructs in C++.  */
    case PREDECREMENT_EXPR:
    case PREINCREMENT_EXPR:
      if (TREE_SIDE_EFFECTS (TREE_OPERAND (lhs, 0)))
	lhs = build2 (TREE_CODE (lhs), TREE_TYPE (lhs),
		      stabilize_reference (TREE_OPERAND (lhs, 0)),
		      TREE_OPERAND (lhs, 1));
      return build2 (COMPOUND_EXPR, lhstype,
		     lhs,
		     build_modify_expr (TREE_OPERAND (lhs, 0),
					modifycode, rhs));

      /* Handle (a, b) used as an "lvalue".  */
    case COMPOUND_EXPR:
      newrhs = build_modify_expr (TREE_OPERAND (lhs, 1),
				  modifycode, rhs);
      if (newrhs == error_mark_node)
	return error_mark_node;
      return build2 (COMPOUND_EXPR, lhstype,
		     TREE_OPERAND (lhs, 0), newrhs);

    case MODIFY_EXPR:
      if (TREE_SIDE_EFFECTS (TREE_OPERAND (lhs, 0)))
	lhs = build2 (TREE_CODE (lhs), TREE_TYPE (lhs),
		      stabilize_reference (TREE_OPERAND (lhs, 0)),
		      TREE_OPERAND (lhs, 1));
      newrhs = build_modify_expr (TREE_OPERAND (lhs, 0), modifycode, rhs);
      if (newrhs == error_mark_node)
	return error_mark_node;
      return build2 (COMPOUND_EXPR, lhstype, lhs, newrhs);

    case MIN_EXPR:
    case MAX_EXPR:
      /* MIN_EXPR and MAX_EXPR are currently only permitted as lvalues,
	 when neither operand has side-effects.  */
      if (!lvalue_or_else (lhs, lv_assign))
	return error_mark_node;

      gcc_assert (!TREE_SIDE_EFFECTS (TREE_OPERAND (lhs, 0))
		  && !TREE_SIDE_EFFECTS (TREE_OPERAND (lhs, 1)));

      lhs = build3 (COND_EXPR, TREE_TYPE (lhs),
		    build2 (TREE_CODE (lhs) == MIN_EXPR ? LE_EXPR : GE_EXPR,
			    boolean_type_node,
			    TREE_OPERAND (lhs, 0),
			    TREE_OPERAND (lhs, 1)),
		    TREE_OPERAND (lhs, 0),
		    TREE_OPERAND (lhs, 1));
      /* Fall through.  */

      /* Handle (a ? b : c) used as an "lvalue".  */
    case COND_EXPR:
      {
	/* Produce (a ? (b = rhs) : (c = rhs))
	   except that the RHS goes through a save-expr
	   so the code to compute it is only emitted once.  */
	tree cond;
	tree preeval = NULL_TREE;

	if (VOID_TYPE_P (TREE_TYPE (rhs)))
	  {
	    error ("void value not ignored as it ought to be");
	    return error_mark_node;
	  }

	rhs = stabilize_expr (rhs, &preeval);

	/* Check this here to avoid odd errors when trying to convert
	   a throw to the type of the COND_EXPR.  */
	if (!lvalue_or_else (lhs, lv_assign))
	  return error_mark_node;

	cond = build_conditional_expr
	  (TREE_OPERAND (lhs, 0),
	   build_modify_expr (TREE_OPERAND (lhs, 1),
			      modifycode, rhs),
	   build_modify_expr (TREE_OPERAND (lhs, 2),
			      modifycode, rhs));

	if (cond == error_mark_node)
	  return cond;
	/* Make sure the code to compute the rhs comes out
	   before the split.  */
	if (preeval)
	  cond = build2 (COMPOUND_EXPR, TREE_TYPE (lhs), preeval, cond);
	return cond;
      }

    default:
      break;
    }

  if (modifycode == INIT_EXPR)
    {
      if (TREE_CODE (rhs) == CONSTRUCTOR)
	{
	  if (! same_type_p (TREE_TYPE (rhs), lhstype))
	    /* Call convert to generate an error; see PR 11063.  */
	    rhs = convert (lhstype, rhs);
	  result = build2 (INIT_EXPR, lhstype, lhs, rhs);
	  TREE_SIDE_EFFECTS (result) = 1;
	  return result;
	}
      else if (! IS_AGGR_TYPE (lhstype))
	/* Do the default thing.  */;
      else
	{
	  result = build_special_member_call (lhs, complete_ctor_identifier,
					      build_tree_list (NULL_TREE, rhs),
					      lhstype, LOOKUP_NORMAL);
	  if (result == NULL_TREE)
	    return error_mark_node;
	  return result;
	}
    }
  else
    {
      lhs = require_complete_type (lhs);
      if (lhs == error_mark_node)
	return error_mark_node;

      if (modifycode == NOP_EXPR)
	{
	  /* `operator=' is not an inheritable operator.  */
	  if (! IS_AGGR_TYPE (lhstype))
	    /* Do the default thing.  */;
	  else
	    {
	      result = build_new_op (MODIFY_EXPR, LOOKUP_NORMAL,
				     lhs, rhs, make_node (NOP_EXPR),
				     /*overloaded_p=*/NULL);
	      if (result == NULL_TREE)
		return error_mark_node;
	      return result;
	    }
	  lhstype = olhstype;
	}
      else
	{
	  /* A binary op has been requested.  Combine the old LHS
	     value with the RHS producing the value we should actually
	     store into the LHS.  */

	  gcc_assert (!PROMOTES_TO_AGGR_TYPE (lhstype, REFERENCE_TYPE));
	  lhs = stabilize_reference (lhs);
	  newrhs = cp_build_binary_op (modifycode, lhs, rhs);
	  if (newrhs == error_mark_node)
	    {
	      error ("  in evaluation of %<%Q(%#T, %#T)%>", modifycode,
		     TREE_TYPE (lhs), TREE_TYPE (rhs));
	      return error_mark_node;
	    }

	  /* Now it looks like a plain assignment.  */
	  modifycode = NOP_EXPR;
	}
      gcc_assert (TREE_CODE (lhstype) != REFERENCE_TYPE);
      gcc_assert (TREE_CODE (TREE_TYPE (newrhs)) != REFERENCE_TYPE);
    }

  /* The left-hand side must be an lvalue.  */
  if (!lvalue_or_else (lhs, lv_assign))
    return error_mark_node;

  /* Warn about modifying something that is `const'.  Don't warn if
     this is initialization.  */
  if (modifycode != INIT_EXPR
      && (TREE_READONLY (lhs) || CP_TYPE_CONST_P (lhstype)
	  /* Functions are not modifiable, even though they are
	     lvalues.  */
	  || TREE_CODE (TREE_TYPE (lhs)) == FUNCTION_TYPE
	  || TREE_CODE (TREE_TYPE (lhs)) == METHOD_TYPE
	  /* If it's an aggregate and any field is const, then it is
	     effectively const.  */
	  || (CLASS_TYPE_P (lhstype)
	      && C_TYPE_FIELDS_READONLY (lhstype))))
    readonly_error (lhs, "assignment", 0);

  /* If storing into a structure or union member, it has probably been
     given type `int'.  Compute the type that would go with the actual
     amount of storage the member occupies.  */

  if (TREE_CODE (lhs) == COMPONENT_REF
      && (TREE_CODE (lhstype) == INTEGER_TYPE
	  || TREE_CODE (lhstype) == REAL_TYPE
	  || TREE_CODE (lhstype) == ENUMERAL_TYPE))
    {
      lhstype = TREE_TYPE (get_unwidened (lhs, 0));

      /* If storing in a field that is in actuality a short or narrower
	 than one, we must store in the field in its actual type.  */

      if (lhstype != TREE_TYPE (lhs))
	{
	  /* Avoid warnings converting integral types back into enums for
	     enum bit fields.  */
	  if (TREE_CODE (lhstype) == INTEGER_TYPE
	      && TREE_CODE (olhstype) == ENUMERAL_TYPE)
	    {
	      if (TREE_SIDE_EFFECTS (lhs))
		lhs = stabilize_reference (lhs);
	      olhs = lhs;
	    }
	  lhs = copy_node (lhs);
	  TREE_TYPE (lhs) = lhstype;
	}
    }

  /* Convert new value to destination type.  */

  if (TREE_CODE (lhstype) == ARRAY_TYPE)
    {
      int from_array;

      if (!same_or_base_type_p (TYPE_MAIN_VARIANT (lhstype),
				TYPE_MAIN_VARIANT (TREE_TYPE (rhs))))
	{
	  error ("incompatible types in assignment of %qT to %qT",
		 TREE_TYPE (rhs), lhstype);
	  return error_mark_node;
	}

      /* Allow array assignment in compiler-generated code.  */
      if (! DECL_ARTIFICIAL (current_function_decl))
	{
          /* This routine is used for both initialization and assignment.
             Make sure the diagnostic message differentiates the context.  */
          if (modifycode == INIT_EXPR)
            error ("array used as initializer");
          else
            error ("invalid array assignment");
	  return error_mark_node;
	}

      from_array = TREE_CODE (TREE_TYPE (newrhs)) == ARRAY_TYPE
		   ? 1 + (modifycode != INIT_EXPR): 0;
      return build_vec_init (lhs, NULL_TREE, newrhs,
			     /*explicit_default_init_p=*/false,
			     from_array);
    }

  if (modifycode == INIT_EXPR)
    newrhs = convert_for_initialization (lhs, lhstype, newrhs, LOOKUP_NORMAL,
					 "initialization", NULL_TREE, 0);
  else
    {
      /* Avoid warnings on enum bit fields.  */
      if (TREE_CODE (olhstype) == ENUMERAL_TYPE
	  && TREE_CODE (lhstype) == INTEGER_TYPE)
	{
	  newrhs = convert_for_assignment (olhstype, newrhs, "assignment",
					   NULL_TREE, 0);
	  newrhs = convert_force (lhstype, newrhs, 0);
	}
      else
	newrhs = convert_for_assignment (lhstype, newrhs, "assignment",
					 NULL_TREE, 0);
      if (TREE_CODE (newrhs) == CALL_EXPR
	  && TYPE_NEEDS_CONSTRUCTING (lhstype))
	newrhs = build_cplus_new (lhstype, newrhs);

      /* Can't initialize directly from a TARGET_EXPR, since that would
	 cause the lhs to be constructed twice, and possibly result in
	 accidental self-initialization.  So we force the TARGET_EXPR to be
	 expanded without a target.  */
      if (TREE_CODE (newrhs) == TARGET_EXPR)
	newrhs = build2 (COMPOUND_EXPR, TREE_TYPE (newrhs), newrhs,
			 TREE_OPERAND (newrhs, 0));
    }

  if (newrhs == error_mark_node)
    return error_mark_node;

  if (c_dialect_objc () && flag_objc_gc)
    {
      result = objc_generate_write_barrier (lhs, modifycode, newrhs);

      if (result)
	return result;
    }

  result = build2 (modifycode == NOP_EXPR ? MODIFY_EXPR : INIT_EXPR,
		   lhstype, lhs, newrhs);

  TREE_SIDE_EFFECTS (result) = 1;
  if (!plain_assign)
    TREE_NO_WARNING (result) = 1;

  /* If we got the LHS in a different type for storing in,
     convert the result back to the nominal type of LHS
     so that the value we return always has the same type
     as the LHS argument.  */

  if (olhstype == TREE_TYPE (result))
    return result;
  if (olhs)
    {
      result = build2 (COMPOUND_EXPR, olhstype, result, olhs);
      TREE_NO_WARNING (result) = 1;
      return result;
    }
  return convert_for_assignment (olhstype, result, "assignment",
				 NULL_TREE, 0);
}

tree
build_x_modify_expr (tree lhs, enum tree_code modifycode, tree rhs)
{
  if (processing_template_decl)
    return build_min_nt (MODOP_EXPR, lhs,
			 build_min_nt (modifycode, NULL_TREE, NULL_TREE), rhs);

  if (modifycode != NOP_EXPR)
    {
      tree rval = build_new_op (MODIFY_EXPR, LOOKUP_NORMAL, lhs, rhs,
				make_node (modifycode),
				/*overloaded_p=*/NULL);
      if (rval)
	{
	  TREE_NO_WARNING (rval) = 1;
	  return rval;
	}
    }
  return build_modify_expr (lhs, modifycode, rhs);
}


/* Get difference in deltas for different pointer to member function
   types.  Returns an integer constant of type PTRDIFF_TYPE_NODE.  If
   the conversion is invalid, the constant is zero.  If
   ALLOW_INVERSE_P is true, then allow reverse conversions as well.
   If C_CAST_P is true this conversion is taking place as part of a
   C-style cast.

   Note that the naming of FROM and TO is kind of backwards; the return
   value is what we add to a TO in order to get a FROM.  They are named
   this way because we call this function to find out how to convert from
   a pointer to member of FROM to a pointer to member of TO.  */

static tree
get_delta_difference (tree from, tree to,
		      bool allow_inverse_p,
		      bool c_cast_p)
{
  tree binfo;
  base_kind kind;
  tree result;

  /* Assume no conversion is required.  */
  result = integer_zero_node;
  binfo = lookup_base (to, from, c_cast_p ? ba_unique : ba_check, &kind);
  if (kind == bk_inaccessible || kind == bk_ambig)
    error ("   in pointer to member function conversion");
  else if (binfo)
    {
      if (kind != bk_via_virtual)
	result = BINFO_OFFSET (binfo);
      else
	{
	  tree virt_binfo = binfo_from_vbase (binfo);

	  /* This is a reinterpret cast, we choose to do nothing.  */
	  if (allow_inverse_p)
	    warning (0, "pointer to member cast via virtual base %qT",
		     BINFO_TYPE (virt_binfo));
	  else
	    error ("pointer to member conversion via virtual base %qT",
		   BINFO_TYPE (virt_binfo));
	}
    }
  else if (same_type_ignoring_top_level_qualifiers_p (from, to))
    /* Pointer to member of incomplete class is permitted*/;
  else if (!allow_inverse_p)
    {
      error_not_base_type (from, to);
      error ("   in pointer to member conversion");
    }
  else
    {
      binfo = lookup_base (from, to, c_cast_p ? ba_unique : ba_check, &kind);
      if (binfo)
	{
	  if (kind != bk_via_virtual)
	    result = size_diffop (size_zero_node, BINFO_OFFSET (binfo));
	  else
	    {
	      /* This is a reinterpret cast, we choose to do nothing.  */
	      tree virt_binfo = binfo_from_vbase (binfo);

	      warning (0, "pointer to member cast via virtual base %qT",
		       BINFO_TYPE (virt_binfo));
	    }
	}
    }

  return fold_if_not_in_template (convert_to_integer (ptrdiff_type_node,
						      result));
}

/* Return a constructor for the pointer-to-member-function TYPE using
   the other components as specified.  */

tree
build_ptrmemfunc1 (tree type, tree delta, tree pfn)
{
  tree u = NULL_TREE;
  tree delta_field;
  tree pfn_field;
  VEC(constructor_elt, gc) *v;

  /* Pull the FIELD_DECLs out of the type.  */
  pfn_field = TYPE_FIELDS (type);
  delta_field = TREE_CHAIN (pfn_field);

  /* Make sure DELTA has the type we want.  */
  delta = convert_and_check (delta_type_node, delta);

  /* Finish creating the initializer.  */
  v = VEC_alloc(constructor_elt, gc, 2);
  CONSTRUCTOR_APPEND_ELT(v, pfn_field, pfn);
  CONSTRUCTOR_APPEND_ELT(v, delta_field, delta);
  u = build_constructor (type, v);
  TREE_CONSTANT (u) = TREE_CONSTANT (pfn) & TREE_CONSTANT (delta);
  TREE_INVARIANT (u) = TREE_INVARIANT (pfn) & TREE_INVARIANT (delta);
  TREE_STATIC (u) = (TREE_CONSTANT (u)
		     && (initializer_constant_valid_p (pfn, TREE_TYPE (pfn))
			 != NULL_TREE)
		     && (initializer_constant_valid_p (delta, TREE_TYPE (delta))
			 != NULL_TREE));
  return u;
}

/* Build a constructor for a pointer to member function.  It can be
   used to initialize global variables, local variable, or used
   as a value in expressions.  TYPE is the POINTER to METHOD_TYPE we
   want to be.

   If FORCE is nonzero, then force this conversion, even if
   we would rather not do it.  Usually set when using an explicit
   cast.  A C-style cast is being processed iff C_CAST_P is true.

   Return error_mark_node, if something goes wrong.  */

tree
build_ptrmemfunc (tree type, tree pfn, int force, bool c_cast_p)
{
  tree fn;
  tree pfn_type;
  tree to_type;

  if (error_operand_p (pfn))
    return error_mark_node;

  pfn_type = TREE_TYPE (pfn);
  to_type = build_ptrmemfunc_type (type);

  /* Handle multiple conversions of pointer to member functions.  */
  if (TYPE_PTRMEMFUNC_P (pfn_type))
    {
      tree delta = NULL_TREE;
      tree npfn = NULL_TREE;
      tree n;

      if (!force
	  && !can_convert_arg (to_type, TREE_TYPE (pfn), pfn, LOOKUP_NORMAL))
	error ("invalid conversion to type %qT from type %qT",
	       to_type, pfn_type);

      n = get_delta_difference (TYPE_PTRMEMFUNC_OBJECT_TYPE (pfn_type),
				TYPE_PTRMEMFUNC_OBJECT_TYPE (to_type),
				force,
				c_cast_p);

      /* We don't have to do any conversion to convert a
	 pointer-to-member to its own type.  But, we don't want to
	 just return a PTRMEM_CST if there's an explicit cast; that
	 cast should make the expression an invalid template argument.  */
      if (TREE_CODE (pfn) != PTRMEM_CST)
	{
	  if (same_type_p (to_type, pfn_type))
	    return pfn;
	  else if (integer_zerop (n))
	    return build_reinterpret_cast (to_type, pfn);
	}

      if (TREE_SIDE_EFFECTS (pfn))
	pfn = save_expr (pfn);

      /* Obtain the function pointer and the current DELTA.  */
      if (TREE_CODE (pfn) == PTRMEM_CST)
	expand_ptrmemfunc_cst (pfn, &delta, &npfn);
      else
	{
	  npfn = build_ptrmemfunc_access_expr (pfn, pfn_identifier);
	  delta = build_ptrmemfunc_access_expr (pfn, delta_identifier);
	}

      /* Just adjust the DELTA field.  */
      gcc_assert  (same_type_ignoring_top_level_qualifiers_p
		   (TREE_TYPE (delta), ptrdiff_type_node));
      if (TARGET_PTRMEMFUNC_VBIT_LOCATION == ptrmemfunc_vbit_in_delta)
	n = cp_build_binary_op (LSHIFT_EXPR, n, integer_one_node);
      delta = cp_build_binary_op (PLUS_EXPR, delta, n);
      return build_ptrmemfunc1 (to_type, delta, npfn);
    }

  /* Handle null pointer to member function conversions.  */
  if (integer_zerop (pfn))
    {
      pfn = build_c_cast (type, integer_zero_node);
      return build_ptrmemfunc1 (to_type,
				integer_zero_node,
				pfn);
    }

  if (type_unknown_p (pfn))
    return instantiate_type (type, pfn, tf_warning_or_error);

  fn = TREE_OPERAND (pfn, 0);
  gcc_assert (TREE_CODE (fn) == FUNCTION_DECL
	      /* In a template, we will have preserved the
		 OFFSET_REF.  */
	      || (processing_template_decl && TREE_CODE (fn) == OFFSET_REF));
  return make_ptrmem_cst (to_type, fn);
}

/* Return the DELTA, IDX, PFN, and DELTA2 values for the PTRMEM_CST
   given by CST.

   ??? There is no consistency as to the types returned for the above
   values.  Some code acts as if it were a sizetype and some as if it were
   integer_type_node.  */

void
expand_ptrmemfunc_cst (tree cst, tree *delta, tree *pfn)
{
  tree type = TREE_TYPE (cst);
  tree fn = PTRMEM_CST_MEMBER (cst);
  tree ptr_class, fn_class;

  gcc_assert (TREE_CODE (fn) == FUNCTION_DECL);

  /* The class that the function belongs to.  */
  fn_class = DECL_CONTEXT (fn);

  /* The class that we're creating a pointer to member of.  */
  ptr_class = TYPE_PTRMEMFUNC_OBJECT_TYPE (type);

  /* First, calculate the adjustment to the function's class.  */
  *delta = get_delta_difference (fn_class, ptr_class, /*force=*/0,
				 /*c_cast_p=*/0);

  if (!DECL_VIRTUAL_P (fn))
    *pfn = convert (TYPE_PTRMEMFUNC_FN_TYPE (type), build_addr_func (fn));
  else
    {
      /* If we're dealing with a virtual function, we have to adjust 'this'
	 again, to point to the base which provides the vtable entry for
	 fn; the call will do the opposite adjustment.  */
      tree orig_class = DECL_CONTEXT (fn);
      tree binfo = binfo_or_else (orig_class, fn_class);
      *delta = build2 (PLUS_EXPR, TREE_TYPE (*delta),
		       *delta, BINFO_OFFSET (binfo));
      *delta = fold_if_not_in_template (*delta);

      /* We set PFN to the vtable offset at which the function can be
	 found, plus one (unless ptrmemfunc_vbit_in_delta, in which
	 case delta is shifted left, and then incremented).  */
      *pfn = DECL_VINDEX (fn);
      *pfn = build2 (MULT_EXPR, integer_type_node, *pfn,
		     TYPE_SIZE_UNIT (vtable_entry_type));
      *pfn = fold_if_not_in_template (*pfn);

      switch (TARGET_PTRMEMFUNC_VBIT_LOCATION)
	{
	case ptrmemfunc_vbit_in_pfn:
	  *pfn = build2 (PLUS_EXPR, integer_type_node, *pfn,
			 integer_one_node);
	  *pfn = fold_if_not_in_template (*pfn);
	  break;

	case ptrmemfunc_vbit_in_delta:
	  *delta = build2 (LSHIFT_EXPR, TREE_TYPE (*delta),
			   *delta, integer_one_node);
	  *delta = fold_if_not_in_template (*delta);
	  *delta = build2 (PLUS_EXPR, TREE_TYPE (*delta),
			   *delta, integer_one_node);
	  *delta = fold_if_not_in_template (*delta);
	  break;

	default:
	  gcc_unreachable ();
	}

      *pfn = build_nop (TYPE_PTRMEMFUNC_FN_TYPE (type), *pfn);
      *pfn = fold_if_not_in_template (*pfn);
    }
}

/* Return an expression for PFN from the pointer-to-member function
   given by T.  */

static tree
pfn_from_ptrmemfunc (tree t)
{
  if (TREE_CODE (t) == PTRMEM_CST)
    {
      tree delta;
      tree pfn;

      expand_ptrmemfunc_cst (t, &delta, &pfn);
      if (pfn)
	return pfn;
    }

  return build_ptrmemfunc_access_expr (t, pfn_identifier);
}

/* Convert value RHS to type TYPE as preparation for an assignment to
   an lvalue of type TYPE.  ERRTYPE is a string to use in error
   messages: "assignment", "return", etc.  If FNDECL is non-NULL, we
   are doing the conversion in order to pass the PARMNUMth argument of
   FNDECL.  */

static tree
convert_for_assignment (tree type, tree rhs,
			const char *errtype, tree fndecl, int parmnum)
{
  tree rhstype;
  enum tree_code coder;

  /* Strip NON_LVALUE_EXPRs since we aren't using as an lvalue.  */
  if (TREE_CODE (rhs) == NON_LVALUE_EXPR)
    rhs = TREE_OPERAND (rhs, 0);

  rhstype = TREE_TYPE (rhs);
  coder = TREE_CODE (rhstype);

  if (TREE_CODE (type) == VECTOR_TYPE && coder == VECTOR_TYPE
      && vector_types_convertible_p (type, rhstype))
    return convert (type, rhs);

  if (rhs == error_mark_node || rhstype == error_mark_node)
    return error_mark_node;
  if (TREE_CODE (rhs) == TREE_LIST && TREE_VALUE (rhs) == error_mark_node)
    return error_mark_node;

  /* The RHS of an assignment cannot have void type.  */
  if (coder == VOID_TYPE)
    {
      error ("void value not ignored as it ought to be");
      return error_mark_node;
    }

  /* Simplify the RHS if possible.  */
  if (TREE_CODE (rhs) == CONST_DECL)
    rhs = DECL_INITIAL (rhs);

  if (c_dialect_objc ())
    {
      int parmno;
      tree rname = fndecl;

      if (!strcmp (errtype, "assignment"))
	parmno = -1;
      else if (!strcmp (errtype, "initialization"))
	parmno = -2;
      else
	{
	  tree selector = objc_message_selector ();

	  parmno = parmnum;

	  if (selector && parmno > 1)
	    {
	      rname = selector;
	      parmno -= 1;
	    }
	}

      if (objc_compare_types (type, rhstype, parmno, rname))
	return convert (type, rhs);
    }

  /* [expr.ass]

     The expression is implicitly converted (clause _conv_) to the
     cv-unqualified type of the left operand.

     We allow bad conversions here because by the time we get to this point
     we are committed to doing the conversion.  If we end up doing a bad
     conversion, convert_like will complain.  */
  if (!can_convert_arg_bad (type, rhstype, rhs))
    {
      /* When -Wno-pmf-conversions is use, we just silently allow
	 conversions from pointers-to-members to plain pointers.  If
	 the conversion doesn't work, cp_convert will complain.  */
      if (!warn_pmf2ptr
	  && TYPE_PTR_P (type)
	  && TYPE_PTRMEMFUNC_P (rhstype))
	rhs = cp_convert (strip_top_quals (type), rhs);
      else
	{
	  /* If the right-hand side has unknown type, then it is an
	     overloaded function.  Call instantiate_type to get error
	     messages.  */
	  if (rhstype == unknown_type_node)
	    instantiate_type (type, rhs, tf_warning_or_error);
	  else if (fndecl)
	    error ("cannot convert %qT to %qT for argument %qP to %qD",
		   rhstype, type, parmnum, fndecl);
	  else
	    error ("cannot convert %qT to %qT in %s", rhstype, type, errtype);
	  return error_mark_node;
	}
    }
  if (warn_missing_format_attribute)
    {
      const enum tree_code codel = TREE_CODE (type);
      if ((codel == POINTER_TYPE || codel == REFERENCE_TYPE)
	  && coder == codel
	  && check_missing_format_attribute (type, rhstype))
	warning (OPT_Wmissing_format_attribute,
		 "%s might be a candidate for a format attribute",
		 errtype);
    }

  return perform_implicit_conversion (strip_top_quals (type), rhs);
}

/* Convert RHS to be of type TYPE.
   If EXP is nonzero, it is the target of the initialization.
   ERRTYPE is a string to use in error messages.

   Two major differences between the behavior of
   `convert_for_assignment' and `convert_for_initialization'
   are that references are bashed in the former, while
   copied in the latter, and aggregates are assigned in
   the former (operator=) while initialized in the
   latter (X(X&)).

   If using constructor make sure no conversion operator exists, if one does
   exist, an ambiguity exists.

   If flags doesn't include LOOKUP_COMPLAIN, don't complain about anything.  */

tree
convert_for_initialization (tree exp, tree type, tree rhs, int flags,
			    const char *errtype, tree fndecl, int parmnum)
{
  enum tree_code codel = TREE_CODE (type);
  tree rhstype;
  enum tree_code coder;

  /* build_c_cast puts on a NOP_EXPR to make the result not an lvalue.
     Strip such NOP_EXPRs, since RHS is used in non-lvalue context.  */
  if (TREE_CODE (rhs) == NOP_EXPR
      && TREE_TYPE (rhs) == TREE_TYPE (TREE_OPERAND (rhs, 0))
      && codel != REFERENCE_TYPE)
    rhs = TREE_OPERAND (rhs, 0);

  if (type == error_mark_node
      || rhs == error_mark_node
      || (TREE_CODE (rhs) == TREE_LIST && TREE_VALUE (rhs) == error_mark_node))
    return error_mark_node;

  if ((TREE_CODE (TREE_TYPE (rhs)) == ARRAY_TYPE
       && TREE_CODE (type) != ARRAY_TYPE
       && (TREE_CODE (type) != REFERENCE_TYPE
	   || TREE_CODE (TREE_TYPE (type)) != ARRAY_TYPE))
      || (TREE_CODE (TREE_TYPE (rhs)) == FUNCTION_TYPE
	  && (TREE_CODE (type) != REFERENCE_TYPE
	      || TREE_CODE (TREE_TYPE (type)) != FUNCTION_TYPE))
      || TREE_CODE (TREE_TYPE (rhs)) == METHOD_TYPE)
    rhs = decay_conversion (rhs);

  rhstype = TREE_TYPE (rhs);
  coder = TREE_CODE (rhstype);

  if (coder == ERROR_MARK)
    return error_mark_node;

  /* We accept references to incomplete types, so we can
     return here before checking if RHS is of complete type.  */

  if (codel == REFERENCE_TYPE)
    {
      /* This should eventually happen in convert_arguments.  */
      int savew = 0, savee = 0;

      if (fndecl)
	savew = warningcount, savee = errorcount;
      rhs = initialize_reference (type, rhs, /*decl=*/NULL_TREE,
				  /*cleanup=*/NULL);
      if (fndecl)
	{
	  if (warningcount > savew)
	    warning (0, "in passing argument %P of %q+D", parmnum, fndecl);
	  else if (errorcount > savee)
	    error ("in passing argument %P of %q+D", parmnum, fndecl);
	}
      return rhs;
    }

  if (exp != 0)
    exp = require_complete_type (exp);
  if (exp == error_mark_node)
    return error_mark_node;

  rhstype = non_reference (rhstype);

  type = complete_type (type);

  if (IS_AGGR_TYPE (type))
    return ocp_convert (type, rhs, CONV_IMPLICIT|CONV_FORCE_TEMP, flags);

  return convert_for_assignment (type, rhs, errtype, fndecl, parmnum);
}

/* If RETVAL is the address of, or a reference to, a local variable or
   temporary give an appropriate warning.  */

static void
maybe_warn_about_returning_address_of_local (tree retval)
{
  tree valtype = TREE_TYPE (DECL_RESULT (current_function_decl));
  tree whats_returned = retval;

  for (;;)
    {
      if (TREE_CODE (whats_returned) == COMPOUND_EXPR)
	whats_returned = TREE_OPERAND (whats_returned, 1);
      else if (TREE_CODE (whats_returned) == CONVERT_EXPR
	       || TREE_CODE (whats_returned) == NON_LVALUE_EXPR
	       || TREE_CODE (whats_returned) == NOP_EXPR)
	whats_returned = TREE_OPERAND (whats_returned, 0);
      else
	break;
    }

  if (TREE_CODE (whats_returned) != ADDR_EXPR)
    return;
  whats_returned = TREE_OPERAND (whats_returned, 0);

  if (TREE_CODE (valtype) == REFERENCE_TYPE)
    {
      if (TREE_CODE (whats_returned) == AGGR_INIT_EXPR
	  || TREE_CODE (whats_returned) == TARGET_EXPR)
	{
	  warning (0, "returning reference to temporary");
	  return;
	}
      if (TREE_CODE (whats_returned) == VAR_DECL
	  && DECL_NAME (whats_returned)
	  && TEMP_NAME_P (DECL_NAME (whats_returned)))
	{
	  warning (0, "reference to non-lvalue returned");
	  return;
	}
    }

  while (TREE_CODE (whats_returned) == COMPONENT_REF
	 || TREE_CODE (whats_returned) == ARRAY_REF)
    whats_returned = TREE_OPERAND (whats_returned, 0);

  if (DECL_P (whats_returned)
      && DECL_NAME (whats_returned)
      && DECL_FUNCTION_SCOPE_P (whats_returned)
      && !(TREE_STATIC (whats_returned)
	   || TREE_PUBLIC (whats_returned)))
    {
      if (TREE_CODE (valtype) == REFERENCE_TYPE)
	warning (0, "reference to local variable %q+D returned",
		 whats_returned);
      else
	warning (0, "address of local variable %q+D returned",
		 whats_returned);
      return;
    }
}

/* Check that returning RETVAL from the current function is valid.
   Return an expression explicitly showing all conversions required to
   change RETVAL into the function return type, and to assign it to
   the DECL_RESULT for the function.  Set *NO_WARNING to true if
   code reaches end of non-void function warning shouldn't be issued
   on this RETURN_EXPR.  */

tree
check_return_expr (tree retval, bool *no_warning)
{
  tree result;
  /* The type actually returned by the function, after any
     promotions.  */
  tree valtype;
  int fn_returns_value_p;

  *no_warning = false;

  /* A `volatile' function is one that isn't supposed to return, ever.
     (This is a G++ extension, used to get better code for functions
     that call the `volatile' function.)  */
  if (TREE_THIS_VOLATILE (current_function_decl))
    warning (0, "function declared %<noreturn%> has a %<return%> statement");

  /* Check for various simple errors.  */
  if (DECL_DESTRUCTOR_P (current_function_decl))
    {
      if (retval)
	error ("returning a value from a destructor");
      return NULL_TREE;
    }
  else if (DECL_CONSTRUCTOR_P (current_function_decl))
    {
      if (in_function_try_handler)
	/* If a return statement appears in a handler of the
	   function-try-block of a constructor, the program is ill-formed.  */
	error ("cannot return from a handler of a function-try-block of a constructor");
      else if (retval)
	/* You can't return a value from a constructor.  */
	error ("returning a value from a constructor");
      return NULL_TREE;
    }

  if (processing_template_decl)
    {
      current_function_returns_value = 1;
      return retval;
    }

  /* When no explicit return-value is given in a function with a named
     return value, the named return value is used.  */
  result = DECL_RESULT (current_function_decl);
  valtype = TREE_TYPE (result);
  gcc_assert (valtype != NULL_TREE);
  fn_returns_value_p = !VOID_TYPE_P (valtype);
  if (!retval && DECL_NAME (result) && fn_returns_value_p)
    retval = result;

  /* Check for a return statement with no return value in a function
     that's supposed to return a value.  */
  if (!retval && fn_returns_value_p)
    {
      pedwarn ("return-statement with no value, in function returning %qT",
	       valtype);
      /* Clear this, so finish_function won't say that we reach the
	 end of a non-void function (which we don't, we gave a
	 return!).  */
      current_function_returns_null = 0;
      /* And signal caller that TREE_NO_WARNING should be set on the
	 RETURN_EXPR to avoid control reaches end of non-void function
	 warnings in tree-cfg.c.  */
      *no_warning = true;
    }
  /* Check for a return statement with a value in a function that
     isn't supposed to return a value.  */
  else if (retval && !fn_returns_value_p)
    {
      if (VOID_TYPE_P (TREE_TYPE (retval)))
	/* You can return a `void' value from a function of `void'
	   type.  In that case, we have to evaluate the expression for
	   its side-effects.  */
	  finish_expr_stmt (retval);
      else
	pedwarn ("return-statement with a value, in function "
		 "returning 'void'");

      current_function_returns_null = 1;

      /* There's really no value to return, after all.  */
      return NULL_TREE;
    }
  else if (!retval)
    /* Remember that this function can sometimes return without a
       value.  */
    current_function_returns_null = 1;
  else
    /* Remember that this function did return a value.  */
    current_function_returns_value = 1;

  /* Check for erroneous operands -- but after giving ourselves a
     chance to provide an error about returning a value from a void
     function.  */
  if (error_operand_p (retval))
    {
      current_function_return_value = error_mark_node;
      return error_mark_node;
    }

  /* Only operator new(...) throw(), can return NULL [expr.new/13].  */
  if ((DECL_OVERLOADED_OPERATOR_P (current_function_decl) == NEW_EXPR
       || DECL_OVERLOADED_OPERATOR_P (current_function_decl) == VEC_NEW_EXPR)
      && !TYPE_NOTHROW_P (TREE_TYPE (current_function_decl))
      && ! flag_check_new
      && null_ptr_cst_p (retval))
    warning (0, "%<operator new%> must not return NULL unless it is "
	     "declared %<throw()%> (or -fcheck-new is in effect)");

  /* Effective C++ rule 15.  See also start_function.  */
  if (warn_ecpp
      && DECL_NAME (current_function_decl) == ansi_assopname(NOP_EXPR))
    {
      bool warn = true;

      /* The function return type must be a reference to the current
	class.  */
      if (TREE_CODE (valtype) == REFERENCE_TYPE
	  && same_type_ignoring_top_level_qualifiers_p
	      (TREE_TYPE (valtype), TREE_TYPE (current_class_ref)))
	{
	  /* Returning '*this' is obviously OK.  */
	  if (retval == current_class_ref)
	    warn = false;
	  /* If we are calling a function whose return type is the same of
	     the current class reference, it is ok.  */
	  else if (TREE_CODE (retval) == INDIRECT_REF
		   && TREE_CODE (TREE_OPERAND (retval, 0)) == CALL_EXPR)
	    warn = false;
	}

      if (warn)
	warning (OPT_Weffc__, "%<operator=%> should return a reference to %<*this%>");
    }

  /* The fabled Named Return Value optimization, as per [class.copy]/15:

     [...]      For  a function with a class return type, if the expression
     in the return statement is the name of a local  object,  and  the  cv-
     unqualified  type  of  the  local  object  is the same as the function
     return type, an implementation is permitted to omit creating the  tem-
     porary  object  to  hold  the function return value [...]

     So, if this is a value-returning function that always returns the same
     local variable, remember it.

     It might be nice to be more flexible, and choose the first suitable
     variable even if the function sometimes returns something else, but
     then we run the risk of clobbering the variable we chose if the other
     returned expression uses the chosen variable somehow.  And people expect
     this restriction, anyway.  (jason 2000-11-19)

     See finish_function and finalize_nrv for the rest of this optimization.  */

  if (fn_returns_value_p && flag_elide_constructors)
    {
      if (retval != NULL_TREE
	  && (current_function_return_value == NULL_TREE
	      || current_function_return_value == retval)
	  && TREE_CODE (retval) == VAR_DECL
	  && DECL_CONTEXT (retval) == current_function_decl
	  && ! TREE_STATIC (retval)
	  && (DECL_ALIGN (retval)
	      >= DECL_ALIGN (DECL_RESULT (current_function_decl)))
	  && same_type_p ((TYPE_MAIN_VARIANT
			   (TREE_TYPE (retval))),
			  (TYPE_MAIN_VARIANT
			   (TREE_TYPE (TREE_TYPE (current_function_decl))))))
	current_function_return_value = retval;
      else
	current_function_return_value = error_mark_node;
    }

  /* We don't need to do any conversions when there's nothing being
     returned.  */
  if (!retval)
    return NULL_TREE;

  /* Do any required conversions.  */
  if (retval == result || DECL_CONSTRUCTOR_P (current_function_decl))
    /* No conversions are required.  */
    ;
  else
    {
      /* The type the function is declared to return.  */
      tree functype = TREE_TYPE (TREE_TYPE (current_function_decl));

      /* The functype's return type will have been set to void, if it
	 was an incomplete type.  Just treat this as 'return;' */
      if (VOID_TYPE_P (functype))
	return error_mark_node;

      /* First convert the value to the function's return type, then
	 to the type of return value's location to handle the
	 case that functype is smaller than the valtype.  */
      retval = convert_for_initialization
	(NULL_TREE, functype, retval, LOOKUP_NORMAL|LOOKUP_ONLYCONVERTING,
	 "return", NULL_TREE, 0);
      retval = convert (valtype, retval);

      /* If the conversion failed, treat this just like `return;'.  */
      if (retval == error_mark_node)
	return retval;
      /* We can't initialize a register from a AGGR_INIT_EXPR.  */
      else if (! current_function_returns_struct
	       && TREE_CODE (retval) == TARGET_EXPR
	       && TREE_CODE (TREE_OPERAND (retval, 1)) == AGGR_INIT_EXPR)
	retval = build2 (COMPOUND_EXPR, TREE_TYPE (retval), retval,
			 TREE_OPERAND (retval, 0));
      else
	maybe_warn_about_returning_address_of_local (retval);
    }

  /* Actually copy the value returned into the appropriate location.  */
  if (retval && retval != result)
    retval = build2 (INIT_EXPR, TREE_TYPE (result), result, retval);

  return retval;
}


/* Returns nonzero if the pointer-type FROM can be converted to the
   pointer-type TO via a qualification conversion.  If CONSTP is -1,
   then we return nonzero if the pointers are similar, and the
   cv-qualification signature of FROM is a proper subset of that of TO.

   If CONSTP is positive, then all outer pointers have been
   const-qualified.  */

static int
comp_ptr_ttypes_real (tree to, tree from, int constp)
{
  bool to_more_cv_qualified = false;

  for (; ; to = TREE_TYPE (to), from = TREE_TYPE (from))
    {
      if (TREE_CODE (to) != TREE_CODE (from))
	return 0;

      if (TREE_CODE (from) == OFFSET_TYPE
	  && !same_type_p (TYPE_OFFSET_BASETYPE (from),
			   TYPE_OFFSET_BASETYPE (to)))
	return 0;

      /* Const and volatile mean something different for function types,
	 so the usual checks are not appropriate.  */
      if (TREE_CODE (to) != FUNCTION_TYPE && TREE_CODE (to) != METHOD_TYPE)
	{
	  /* In Objective-C++, some types may have been 'volatilized' by
	     the compiler for EH; when comparing them here, the volatile
	     qualification must be ignored.  */
	  bool objc_quals_match = objc_type_quals_match (to, from);

	  if (!at_least_as_qualified_p (to, from) && !objc_quals_match)
	    return 0;

	  if (!at_least_as_qualified_p (from, to) && !objc_quals_match)
	    {
	      if (constp == 0)
		return 0;
	      to_more_cv_qualified = true;
	    }

	  if (constp > 0)
	    constp &= TYPE_READONLY (to);
	}

      if (TREE_CODE (to) != POINTER_TYPE && !TYPE_PTRMEM_P (to))
	return ((constp >= 0 || to_more_cv_qualified)
		&& same_type_ignoring_top_level_qualifiers_p (to, from));
    }
}

/* When comparing, say, char ** to char const **, this function takes
   the 'char *' and 'char const *'.  Do not pass non-pointer/reference
   types to this function.  */

int
comp_ptr_ttypes (tree to, tree from)
{
  return comp_ptr_ttypes_real (to, from, 1);
}

/* Returns 1 if to and from are (possibly multi-level) pointers to the same
   type or inheritance-related types, regardless of cv-quals.  */

int
ptr_reasonably_similar (tree to, tree from)
{
  for (; ; to = TREE_TYPE (to), from = TREE_TYPE (from))
    {
      /* Any target type is similar enough to void.  */
      if (TREE_CODE (to) == VOID_TYPE
	  || TREE_CODE (from) == VOID_TYPE)
	return 1;

      if (TREE_CODE (to) != TREE_CODE (from))
	return 0;

      if (TREE_CODE (from) == OFFSET_TYPE
	  && comptypes (TYPE_OFFSET_BASETYPE (to),
			TYPE_OFFSET_BASETYPE (from),
			COMPARE_BASE | COMPARE_DERIVED))
	continue;

      if (TREE_CODE (to) == VECTOR_TYPE
	  && vector_types_convertible_p (to, from))
	return 1;

      if (TREE_CODE (to) == INTEGER_TYPE
	  && TYPE_PRECISION (to) == TYPE_PRECISION (from))
	return 1;

      if (TREE_CODE (to) == FUNCTION_TYPE)
	return 1;

      if (TREE_CODE (to) != POINTER_TYPE)
	return comptypes
	  (TYPE_MAIN_VARIANT (to), TYPE_MAIN_VARIANT (from),
	   COMPARE_BASE | COMPARE_DERIVED);
    }
}

/* Return true if TO and FROM (both of which are POINTER_TYPEs or
   pointer-to-member types) are the same, ignoring cv-qualification at
   all levels.  */

bool
comp_ptr_ttypes_const (tree to, tree from)
{
  for (; ; to = TREE_TYPE (to), from = TREE_TYPE (from))
    {
      if (TREE_CODE (to) != TREE_CODE (from))
	return false;

      if (TREE_CODE (from) == OFFSET_TYPE
	  && same_type_p (TYPE_OFFSET_BASETYPE (from),
			  TYPE_OFFSET_BASETYPE (to)))
	  continue;

      if (TREE_CODE (to) != POINTER_TYPE)
	return same_type_ignoring_top_level_qualifiers_p (to, from);
    }
}

/* Returns the type qualifiers for this type, including the qualifiers on the
   elements for an array type.  */

int
cp_type_quals (tree type)
{
  type = strip_array_types (type);
  if (type == error_mark_node)
    return TYPE_UNQUALIFIED;
  return TYPE_QUALS (type);
}

/* Returns nonzero if the TYPE is const from a C++ perspective: look inside
   arrays.  */

bool
cp_type_readonly (tree type)
{
  type = strip_array_types (type);
  return TYPE_READONLY (type);
}

/* Returns nonzero if the TYPE contains a mutable member.  */

bool
cp_has_mutable_p (tree type)
{
  type = strip_array_types (type);

  return CLASS_TYPE_P (type) && CLASSTYPE_HAS_MUTABLE (type);
}

/* Apply the TYPE_QUALS to the new DECL.  */
void
cp_apply_type_quals_to_decl (int type_quals, tree decl)
{
  tree type = TREE_TYPE (decl);

  if (type == error_mark_node)
    return;

  if (TREE_CODE (type) == FUNCTION_TYPE
      && type_quals != TYPE_UNQUALIFIED)
    {
      /* This was an error in C++98 (cv-qualifiers cannot be added to
	 a function type), but DR 295 makes the code well-formed by
	 dropping the extra qualifiers. */
      if (pedantic)
	{
	  tree bad_type = build_qualified_type (type, type_quals);
	  pedwarn ("ignoring %qV qualifiers added to function type %qT",
		   bad_type, type);
	}

      TREE_TYPE (decl) = TYPE_MAIN_VARIANT (type);
      return;
    }

  /* Avoid setting TREE_READONLY incorrectly.  */
  if (/* If the object has a constructor, the constructor may modify
	 the object.  */
      TYPE_NEEDS_CONSTRUCTING (type)
      /* If the type isn't complete, we don't know yet if it will need
	 constructing.  */
      || !COMPLETE_TYPE_P (type)
      /* If the type has a mutable component, that component might be
	 modified.  */
      || TYPE_HAS_MUTABLE_P (type))
    type_quals &= ~TYPE_QUAL_CONST;

  c_apply_type_quals_to_decl (type_quals, decl);
}

/* Subroutine of casts_away_constness.  Make T1 and T2 point at
   exemplar types such that casting T1 to T2 is casting away constness
   if and only if there is no implicit conversion from T1 to T2.  */

static void
casts_away_constness_r (tree *t1, tree *t2)
{
  int quals1;
  int quals2;

  /* [expr.const.cast]

     For multi-level pointer to members and multi-level mixed pointers
     and pointers to members (conv.qual), the "member" aspect of a
     pointer to member level is ignored when determining if a const
     cv-qualifier has been cast away.  */
  /* [expr.const.cast]

     For  two  pointer types:

	    X1 is T1cv1,1 * ... cv1,N *   where T1 is not a pointer type
	    X2 is T2cv2,1 * ... cv2,M *   where T2 is not a pointer type
	    K is min(N,M)

     casting from X1 to X2 casts away constness if, for a non-pointer
     type T there does not exist an implicit conversion (clause
     _conv_) from:

	    Tcv1,(N-K+1) * cv1,(N-K+2) * ... cv1,N *

     to

	    Tcv2,(M-K+1) * cv2,(M-K+2) * ... cv2,M *.  */
  if ((!TYPE_PTR_P (*t1) && !TYPE_PTRMEM_P (*t1))
      || (!TYPE_PTR_P (*t2) && !TYPE_PTRMEM_P (*t2)))
    {
      *t1 = cp_build_qualified_type (void_type_node,
				     cp_type_quals (*t1));
      *t2 = cp_build_qualified_type (void_type_node,
				     cp_type_quals (*t2));
      return;
    }

  quals1 = cp_type_quals (*t1);
  quals2 = cp_type_quals (*t2);

  if (TYPE_PTRMEM_P (*t1))
    *t1 = TYPE_PTRMEM_POINTED_TO_TYPE (*t1);
  else
    *t1 = TREE_TYPE (*t1);
  if (TYPE_PTRMEM_P (*t2))
    *t2 = TYPE_PTRMEM_POINTED_TO_TYPE (*t2);
  else
    *t2 = TREE_TYPE (*t2);

  casts_away_constness_r (t1, t2);
  *t1 = build_pointer_type (*t1);
  *t2 = build_pointer_type (*t2);
  *t1 = cp_build_qualified_type (*t1, quals1);
  *t2 = cp_build_qualified_type (*t2, quals2);
}

/* Returns nonzero if casting from TYPE1 to TYPE2 casts away
   constness.  */

static bool
casts_away_constness (tree t1, tree t2)
{
  if (TREE_CODE (t2) == REFERENCE_TYPE)
    {
      /* [expr.const.cast]

	 Casting from an lvalue of type T1 to an lvalue of type T2
	 using a reference cast casts away constness if a cast from an
	 rvalue of type "pointer to T1" to the type "pointer to T2"
	 casts away constness.  */
      t1 = (TREE_CODE (t1) == REFERENCE_TYPE ? TREE_TYPE (t1) : t1);
      return casts_away_constness (build_pointer_type (t1),
				   build_pointer_type (TREE_TYPE (t2)));
    }

  if (TYPE_PTRMEM_P (t1) && TYPE_PTRMEM_P (t2))
    /* [expr.const.cast]

       Casting from an rvalue of type "pointer to data member of X
       of type T1" to the type "pointer to data member of Y of type
       T2" casts away constness if a cast from an rvalue of type
       "pointer to T1" to the type "pointer to T2" casts away
       constness.  */
    return casts_away_constness
      (build_pointer_type (TYPE_PTRMEM_POINTED_TO_TYPE (t1)),
       build_pointer_type (TYPE_PTRMEM_POINTED_TO_TYPE (t2)));

  /* Casting away constness is only something that makes sense for
     pointer or reference types.  */
  if (TREE_CODE (t1) != POINTER_TYPE
      || TREE_CODE (t2) != POINTER_TYPE)
    return false;

  /* Top-level qualifiers don't matter.  */
  t1 = TYPE_MAIN_VARIANT (t1);
  t2 = TYPE_MAIN_VARIANT (t2);
  casts_away_constness_r (&t1, &t2);
  if (!can_convert (t2, t1))
    return true;

  return false;
}

/* If T is a REFERENCE_TYPE return the type to which T refers.
   Otherwise, return T itself.  */

tree
non_reference (tree t)
{
  if (TREE_CODE (t) == REFERENCE_TYPE)
    t = TREE_TYPE (t);
  return t;
}


/* Return nonzero if REF is an lvalue valid for this language;
   otherwise, print an error message and return zero.  USE says
   how the lvalue is being used and so selects the error message.  */

int
lvalue_or_else (tree ref, enum lvalue_use use)
{
  int win = lvalue_p (ref);

  if (!win)
    lvalue_error (use);

  return win;
}
