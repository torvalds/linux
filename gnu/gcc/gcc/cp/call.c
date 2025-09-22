/* Functions related to invoking methods and overloaded functions.
   Copyright (C) 1987, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Michael Tiemann (tiemann@cygnus.com) and
   modified by Brendan Kehoe (brendan@cygnus.com).

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


/* High-level class interface.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "cp-tree.h"
#include "output.h"
#include "flags.h"
#include "rtl.h"
#include "toplev.h"
#include "expr.h"
#include "diagnostic.h"
#include "intl.h"
#include "target.h"
#include "convert.h"

/* The various kinds of conversion.  */

typedef enum conversion_kind {
  ck_identity,
  ck_lvalue,
  ck_qual,
  ck_std,
  ck_ptr,
  ck_pmem,
  ck_base,
  ck_ref_bind,
  ck_user,
  ck_ambig,
  ck_rvalue
} conversion_kind;

/* The rank of the conversion.  Order of the enumerals matters; better
   conversions should come earlier in the list.  */

typedef enum conversion_rank {
  cr_identity,
  cr_exact,
  cr_promotion,
  cr_std,
  cr_pbool,
  cr_user,
  cr_ellipsis,
  cr_bad
} conversion_rank;

/* An implicit conversion sequence, in the sense of [over.best.ics].
   The first conversion to be performed is at the end of the chain.
   That conversion is always a cr_identity conversion.  */

typedef struct conversion conversion;
struct conversion {
  /* The kind of conversion represented by this step.  */
  conversion_kind kind;
  /* The rank of this conversion.  */
  conversion_rank rank;
  BOOL_BITFIELD user_conv_p : 1;
  BOOL_BITFIELD ellipsis_p : 1;
  BOOL_BITFIELD this_p : 1;
  BOOL_BITFIELD bad_p : 1;
  /* If KIND is ck_ref_bind ck_base_conv, true to indicate that a
     temporary should be created to hold the result of the
     conversion.  */
  BOOL_BITFIELD need_temporary_p : 1;
  /* If KIND is ck_identity or ck_base_conv, true to indicate that the
     copy constructor must be accessible, even though it is not being
     used.  */
  BOOL_BITFIELD check_copy_constructor_p : 1;
  /* If KIND is ck_ptr or ck_pmem, true to indicate that a conversion
     from a pointer-to-derived to pointer-to-base is being performed.  */
  BOOL_BITFIELD base_p : 1;
  /* The type of the expression resulting from the conversion.  */
  tree type;
  union {
    /* The next conversion in the chain.  Since the conversions are
       arranged from outermost to innermost, the NEXT conversion will
       actually be performed before this conversion.  This variant is
       used only when KIND is neither ck_identity nor ck_ambig.  */
    conversion *next;
    /* The expression at the beginning of the conversion chain.  This
       variant is used only if KIND is ck_identity or ck_ambig.  */
    tree expr;
  } u;
  /* The function candidate corresponding to this conversion
     sequence.  This field is only used if KIND is ck_user.  */
  struct z_candidate *cand;
};

#define CONVERSION_RANK(NODE)			\
  ((NODE)->bad_p ? cr_bad			\
   : (NODE)->ellipsis_p ? cr_ellipsis		\
   : (NODE)->user_conv_p ? cr_user		\
   : (NODE)->rank)

static struct obstack conversion_obstack;
static bool conversion_obstack_initialized;

static struct z_candidate * tourney (struct z_candidate *);
static int equal_functions (tree, tree);
static int joust (struct z_candidate *, struct z_candidate *, bool);
static int compare_ics (conversion *, conversion *);
static tree build_over_call (struct z_candidate *, int);
static tree build_java_interface_fn_ref (tree, tree);
#define convert_like(CONV, EXPR)				\
  convert_like_real ((CONV), (EXPR), NULL_TREE, 0, 0,		\
		     /*issue_conversion_warnings=*/true,	\
		     /*c_cast_p=*/false)
#define convert_like_with_context(CONV, EXPR, FN, ARGNO)	\
  convert_like_real ((CONV), (EXPR), (FN), (ARGNO), 0,		\
		     /*issue_conversion_warnings=*/true,	\
		     /*c_cast_p=*/false)
static tree convert_like_real (conversion *, tree, tree, int, int, bool,
			       bool);
static void op_error (enum tree_code, enum tree_code, tree, tree,
		      tree, const char *);
static tree build_object_call (tree, tree);
static tree resolve_args (tree);
static struct z_candidate *build_user_type_conversion_1 (tree, tree, int);
static void print_z_candidate (const char *, struct z_candidate *);
static void print_z_candidates (struct z_candidate *);
static tree build_this (tree);
static struct z_candidate *splice_viable (struct z_candidate *, bool, bool *);
static bool any_strictly_viable (struct z_candidate *);
static struct z_candidate *add_template_candidate
	(struct z_candidate **, tree, tree, tree, tree, tree,
	 tree, tree, int, unification_kind_t);
static struct z_candidate *add_template_candidate_real
	(struct z_candidate **, tree, tree, tree, tree, tree,
	 tree, tree, int, tree, unification_kind_t);
static struct z_candidate *add_template_conv_candidate
	(struct z_candidate **, tree, tree, tree, tree, tree, tree);
static void add_builtin_candidates
	(struct z_candidate **, enum tree_code, enum tree_code,
	 tree, tree *, int);
static void add_builtin_candidate
	(struct z_candidate **, enum tree_code, enum tree_code,
	 tree, tree, tree, tree *, tree *, int);
static bool is_complete (tree);
static void build_builtin_candidate
	(struct z_candidate **, tree, tree, tree, tree *, tree *,
	 int);
static struct z_candidate *add_conv_candidate
	(struct z_candidate **, tree, tree, tree, tree, tree);
static struct z_candidate *add_function_candidate
	(struct z_candidate **, tree, tree, tree, tree, tree, int);
static conversion *implicit_conversion (tree, tree, tree, bool, int);
static conversion *standard_conversion (tree, tree, tree, bool, int);
static conversion *reference_binding (tree, tree, tree, bool, int);
static conversion *build_conv (conversion_kind, tree, conversion *);
static bool is_subseq (conversion *, conversion *);
static tree maybe_handle_ref_bind (conversion **);
static void maybe_handle_implicit_object (conversion **);
static struct z_candidate *add_candidate
	(struct z_candidate **, tree, tree, size_t,
	 conversion **, tree, tree, int);
static tree source_type (conversion *);
static void add_warning (struct z_candidate *, struct z_candidate *);
static bool reference_related_p (tree, tree);
static bool reference_compatible_p (tree, tree);
static conversion *convert_class_to_reference (tree, tree, tree);
static conversion *direct_reference_binding (tree, conversion *);
static bool promoted_arithmetic_type_p (tree);
static conversion *conditional_conversion (tree, tree);
static char *name_as_c_string (tree, tree, bool *);
static tree call_builtin_trap (void);
static tree prep_operand (tree);
static void add_candidates (tree, tree, tree, bool, tree, tree,
			    int, struct z_candidate **);
static conversion *merge_conversion_sequences (conversion *, conversion *);
static bool magic_varargs_p (tree);
typedef void (*diagnostic_fn_t) (const char *, ...) ATTRIBUTE_GCC_CXXDIAG(1,2);
static tree build_temp (tree, tree, int, diagnostic_fn_t *);
static void check_constructor_callable (tree, tree);

/* Returns nonzero iff the destructor name specified in NAME matches BASETYPE.
   NAME can take many forms...  */

bool
check_dtor_name (tree basetype, tree name)
{
  /* Just accept something we've already complained about.  */
  if (name == error_mark_node)
    return true;

  if (TREE_CODE (name) == TYPE_DECL)
    name = TREE_TYPE (name);
  else if (TYPE_P (name))
    /* OK */;
  else if (TREE_CODE (name) == IDENTIFIER_NODE)
    {
      if ((IS_AGGR_TYPE (basetype) && name == constructor_name (basetype))
	  || (TREE_CODE (basetype) == ENUMERAL_TYPE
	      && name == TYPE_IDENTIFIER (basetype)))
	return true;
      else
	name = get_type_value (name);
    }
  else
    {
      /* In the case of:

	 template <class T> struct S { ~S(); };
	 int i;
	 i.~S();

	 NAME will be a class template.  */
      gcc_assert (DECL_CLASS_TEMPLATE_P (name));
      return false;
    }

  if (!name)
    return false;
  return same_type_p (TYPE_MAIN_VARIANT (basetype), TYPE_MAIN_VARIANT (name));
}

/* We want the address of a function or method.  We avoid creating a
   pointer-to-member function.  */

tree
build_addr_func (tree function)
{
  tree type = TREE_TYPE (function);

  /* We have to do these by hand to avoid real pointer to member
     functions.  */
  if (TREE_CODE (type) == METHOD_TYPE)
    {
      if (TREE_CODE (function) == OFFSET_REF)
	{
	  tree object = build_address (TREE_OPERAND (function, 0));
	  return get_member_function_from_ptrfunc (&object,
						   TREE_OPERAND (function, 1));
	}
      function = build_address (function);
    }
  else
    function = decay_conversion (function);

  return function;
}

/* Build a CALL_EXPR, we can handle FUNCTION_TYPEs, METHOD_TYPEs, or
   POINTER_TYPE to those.  Note, pointer to member function types
   (TYPE_PTRMEMFUNC_P) must be handled by our callers.  */

tree
build_call (tree function, tree parms)
{
  int is_constructor = 0;
  int nothrow;
  tree tmp;
  tree decl;
  tree result_type;
  tree fntype;

  function = build_addr_func (function);

  gcc_assert (TYPE_PTR_P (TREE_TYPE (function)));
  fntype = TREE_TYPE (TREE_TYPE (function));
  gcc_assert (TREE_CODE (fntype) == FUNCTION_TYPE
	      || TREE_CODE (fntype) == METHOD_TYPE);
  result_type = TREE_TYPE (fntype);

  if (TREE_CODE (function) == ADDR_EXPR
      && TREE_CODE (TREE_OPERAND (function, 0)) == FUNCTION_DECL)
    {
      decl = TREE_OPERAND (function, 0);
      if (!TREE_USED (decl))
	{
	  /* We invoke build_call directly for several library
	     functions.  These may have been declared normally if
	     we're building libgcc, so we can't just check
	     DECL_ARTIFICIAL.  */
	  gcc_assert (DECL_ARTIFICIAL (decl)
		      || !strncmp (IDENTIFIER_POINTER (DECL_NAME (decl)),
				   "__", 2));
	  mark_used (decl);
	}
    }
  else
    decl = NULL_TREE;

  /* We check both the decl and the type; a function may be known not to
     throw without being declared throw().  */
  nothrow = ((decl && TREE_NOTHROW (decl))
	     || TYPE_NOTHROW_P (TREE_TYPE (TREE_TYPE (function))));

  if (decl && TREE_THIS_VOLATILE (decl) && cfun)
    current_function_returns_abnormally = 1;

  if (decl && TREE_DEPRECATED (decl))
    warn_deprecated_use (decl);
  require_complete_eh_spec_types (fntype, decl);

  if (decl && DECL_CONSTRUCTOR_P (decl))
    is_constructor = 1;

  /* Don't pass empty class objects by value.  This is useful
     for tags in STL, which are used to control overload resolution.
     We don't need to handle other cases of copying empty classes.  */
  if (! decl || ! DECL_BUILT_IN (decl))
    for (tmp = parms; tmp; tmp = TREE_CHAIN (tmp))
      if (is_empty_class (TREE_TYPE (TREE_VALUE (tmp)))
	  && ! TREE_ADDRESSABLE (TREE_TYPE (TREE_VALUE (tmp))))
	{
	  tree t = build0 (EMPTY_CLASS_EXPR, TREE_TYPE (TREE_VALUE (tmp)));
	  TREE_VALUE (tmp) = build2 (COMPOUND_EXPR, TREE_TYPE (t),
				     TREE_VALUE (tmp), t);
	}

  function = build3 (CALL_EXPR, result_type, function, parms, NULL_TREE);
  TREE_HAS_CONSTRUCTOR (function) = is_constructor;
  TREE_NOTHROW (function) = nothrow;

  return function;
}

/* Build something of the form ptr->method (args)
   or object.method (args).  This can also build
   calls to constructors, and find friends.

   Member functions always take their class variable
   as a pointer.

   INSTANCE is a class instance.

   NAME is the name of the method desired, usually an IDENTIFIER_NODE.

   PARMS help to figure out what that NAME really refers to.

   BASETYPE_PATH, if non-NULL, contains a chain from the type of INSTANCE
   down to the real instance type to use for access checking.  We need this
   information to get protected accesses correct.

   FLAGS is the logical disjunction of zero or more LOOKUP_
   flags.  See cp-tree.h for more info.

   If this is all OK, calls build_function_call with the resolved
   member function.

   This function must also handle being called to perform
   initialization, promotion/coercion of arguments, and
   instantiation of default parameters.

   Note that NAME may refer to an instance variable name.  If
   `operator()()' is defined for the type of that field, then we return
   that result.  */

/* New overloading code.  */

typedef struct z_candidate z_candidate;

typedef struct candidate_warning candidate_warning;
struct candidate_warning {
  z_candidate *loser;
  candidate_warning *next;
};

struct z_candidate {
  /* The FUNCTION_DECL that will be called if this candidate is
     selected by overload resolution.  */
  tree fn;
  /* The arguments to use when calling this function.  */
  tree args;
  /* The implicit conversion sequences for each of the arguments to
     FN.  */
  conversion **convs;
  /* The number of implicit conversion sequences.  */
  size_t num_convs;
  /* If FN is a user-defined conversion, the standard conversion
     sequence from the type returned by FN to the desired destination
     type.  */
  conversion *second_conv;
  int viable;
  /* If FN is a member function, the binfo indicating the path used to
     qualify the name of FN at the call site.  This path is used to
     determine whether or not FN is accessible if it is selected by
     overload resolution.  The DECL_CONTEXT of FN will always be a
     (possibly improper) base of this binfo.  */
  tree access_path;
  /* If FN is a non-static member function, the binfo indicating the
     subobject to which the `this' pointer should be converted if FN
     is selected by overload resolution.  The type pointed to the by
     the `this' pointer must correspond to the most derived class
     indicated by the CONVERSION_PATH.  */
  tree conversion_path;
  tree template_decl;
  candidate_warning *warnings;
  z_candidate *next;
};

/* Returns true iff T is a null pointer constant in the sense of
   [conv.ptr].  */

bool
null_ptr_cst_p (tree t)
{
  /* [conv.ptr]

     A null pointer constant is an integral constant expression
     (_expr.const_) rvalue of integer type that evaluates to zero.  */
  t = integral_constant_value (t);
  if (t == null_node)
    return true;
  if (CP_INTEGRAL_TYPE_P (TREE_TYPE (t)) && integer_zerop (t))
    {
      STRIP_NOPS (t);
      if (!TREE_CONSTANT_OVERFLOW (t))
	return true;
    }
  return false;
}

/* Returns nonzero if PARMLIST consists of only default parms and/or
   ellipsis.  */

bool
sufficient_parms_p (tree parmlist)
{
  for (; parmlist && parmlist != void_list_node;
       parmlist = TREE_CHAIN (parmlist))
    if (!TREE_PURPOSE (parmlist))
      return false;
  return true;
}

/* Allocate N bytes of memory from the conversion obstack.  The memory
   is zeroed before being returned.  */

static void *
conversion_obstack_alloc (size_t n)
{
  void *p;
  if (!conversion_obstack_initialized)
    {
      gcc_obstack_init (&conversion_obstack);
      conversion_obstack_initialized = true;
    }
  p = obstack_alloc (&conversion_obstack, n);
  memset (p, 0, n);
  return p;
}

/* Dynamically allocate a conversion.  */

static conversion *
alloc_conversion (conversion_kind kind)
{
  conversion *c;
  c = (conversion *) conversion_obstack_alloc (sizeof (conversion));
  c->kind = kind;
  return c;
}

#ifdef ENABLE_CHECKING

/* Make sure that all memory on the conversion obstack has been
   freed.  */

void
validate_conversion_obstack (void)
{
  if (conversion_obstack_initialized)
    gcc_assert ((obstack_next_free (&conversion_obstack)
		 == obstack_base (&conversion_obstack)));
}

#endif /* ENABLE_CHECKING */

/* Dynamically allocate an array of N conversions.  */

static conversion **
alloc_conversions (size_t n)
{
  return (conversion **) conversion_obstack_alloc (n * sizeof (conversion *));
}

static conversion *
build_conv (conversion_kind code, tree type, conversion *from)
{
  conversion *t;
  conversion_rank rank = CONVERSION_RANK (from);

  /* We can't use buildl1 here because CODE could be USER_CONV, which
     takes two arguments.  In that case, the caller is responsible for
     filling in the second argument.  */
  t = alloc_conversion (code);
  t->type = type;
  t->u.next = from;

  switch (code)
    {
    case ck_ptr:
    case ck_pmem:
    case ck_base:
    case ck_std:
      if (rank < cr_std)
	rank = cr_std;
      break;

    case ck_qual:
      if (rank < cr_exact)
	rank = cr_exact;
      break;

    default:
      break;
    }
  t->rank = rank;
  t->user_conv_p = (code == ck_user || from->user_conv_p);
  t->bad_p = from->bad_p;
  t->base_p = false;
  return t;
}

/* Build a representation of the identity conversion from EXPR to
   itself.  The TYPE should match the type of EXPR, if EXPR is non-NULL.  */

static conversion *
build_identity_conv (tree type, tree expr)
{
  conversion *c;

  c = alloc_conversion (ck_identity);
  c->type = type;
  c->u.expr = expr;

  return c;
}

/* Converting from EXPR to TYPE was ambiguous in the sense that there
   were multiple user-defined conversions to accomplish the job.
   Build a conversion that indicates that ambiguity.  */

static conversion *
build_ambiguous_conv (tree type, tree expr)
{
  conversion *c;

  c = alloc_conversion (ck_ambig);
  c->type = type;
  c->u.expr = expr;

  return c;
}

tree
strip_top_quals (tree t)
{
  if (TREE_CODE (t) == ARRAY_TYPE)
    return t;
  return cp_build_qualified_type (t, 0);
}

/* Returns the standard conversion path (see [conv]) from type FROM to type
   TO, if any.  For proper handling of null pointer constants, you must
   also pass the expression EXPR to convert from.  If C_CAST_P is true,
   this conversion is coming from a C-style cast.  */

static conversion *
standard_conversion (tree to, tree from, tree expr, bool c_cast_p,
		     int flags)
{
  enum tree_code fcode, tcode;
  conversion *conv;
  bool fromref = false;

  to = non_reference (to);
  if (TREE_CODE (from) == REFERENCE_TYPE)
    {
      fromref = true;
      from = TREE_TYPE (from);
    }
  to = strip_top_quals (to);
  from = strip_top_quals (from);

  if ((TYPE_PTRFN_P (to) || TYPE_PTRMEMFUNC_P (to))
      && expr && type_unknown_p (expr))
    {
      expr = instantiate_type (to, expr, tf_conv);
      if (expr == error_mark_node)
	return NULL;
      from = TREE_TYPE (expr);
    }

  fcode = TREE_CODE (from);
  tcode = TREE_CODE (to);

  conv = build_identity_conv (from, expr);
  if (fcode == FUNCTION_TYPE || fcode == ARRAY_TYPE)
    {
      from = type_decays_to (from);
      fcode = TREE_CODE (from);
      conv = build_conv (ck_lvalue, from, conv);
    }
  else if (fromref || (expr && lvalue_p (expr)))
    {
      if (expr)
	{
	  tree bitfield_type;
	  bitfield_type = is_bitfield_expr_with_lowered_type (expr);
	  if (bitfield_type)
	    {
	      from = strip_top_quals (bitfield_type);
	      fcode = TREE_CODE (from);
	    }
	}
      conv = build_conv (ck_rvalue, from, conv);
    }

   /* Allow conversion between `__complex__' data types.  */
  if (tcode == COMPLEX_TYPE && fcode == COMPLEX_TYPE)
    {
      /* The standard conversion sequence to convert FROM to TO is
	 the standard conversion sequence to perform componentwise
	 conversion.  */
      conversion *part_conv = standard_conversion
	(TREE_TYPE (to), TREE_TYPE (from), NULL_TREE, c_cast_p, flags);

      if (part_conv)
	{
	  conv = build_conv (part_conv->kind, to, conv);
	  conv->rank = part_conv->rank;
	}
      else
	conv = NULL;

      return conv;
    }

  if (same_type_p (from, to))
    return conv;

  if ((tcode == POINTER_TYPE || TYPE_PTR_TO_MEMBER_P (to))
      && expr && null_ptr_cst_p (expr))
    conv = build_conv (ck_std, to, conv);
  else if ((tcode == INTEGER_TYPE && fcode == POINTER_TYPE)
	   || (tcode == POINTER_TYPE && fcode == INTEGER_TYPE))
    {
      /* For backwards brain damage compatibility, allow interconversion of
	 pointers and integers with a pedwarn.  */
      conv = build_conv (ck_std, to, conv);
      conv->bad_p = true;
    }
  else if (tcode == ENUMERAL_TYPE && fcode == INTEGER_TYPE)
    {
      /* For backwards brain damage compatibility, allow interconversion of
	 enums and integers with a pedwarn.  */
      conv = build_conv (ck_std, to, conv);
      conv->bad_p = true;
    }
  else if ((tcode == POINTER_TYPE && fcode == POINTER_TYPE)
	   || (TYPE_PTRMEM_P (to) && TYPE_PTRMEM_P (from)))
    {
      tree to_pointee;
      tree from_pointee;

      if (tcode == POINTER_TYPE
	  && same_type_ignoring_top_level_qualifiers_p (TREE_TYPE (from),
							TREE_TYPE (to)))
	;
      else if (VOID_TYPE_P (TREE_TYPE (to))
	       && !TYPE_PTRMEM_P (from)
	       && TREE_CODE (TREE_TYPE (from)) != FUNCTION_TYPE)
	{
	  from = build_pointer_type
	    (cp_build_qualified_type (void_type_node,
				      cp_type_quals (TREE_TYPE (from))));
	  conv = build_conv (ck_ptr, from, conv);
	}
      else if (TYPE_PTRMEM_P (from))
	{
	  tree fbase = TYPE_PTRMEM_CLASS_TYPE (from);
	  tree tbase = TYPE_PTRMEM_CLASS_TYPE (to);

	  if (DERIVED_FROM_P (fbase, tbase)
	      && (same_type_ignoring_top_level_qualifiers_p
		  (TYPE_PTRMEM_POINTED_TO_TYPE (from),
		   TYPE_PTRMEM_POINTED_TO_TYPE (to))))
	    {
	      from = build_ptrmem_type (tbase,
					TYPE_PTRMEM_POINTED_TO_TYPE (from));
	      conv = build_conv (ck_pmem, from, conv);
	    }
	  else if (!same_type_p (fbase, tbase))
	    return NULL;
	}
      else if (IS_AGGR_TYPE (TREE_TYPE (from))
	       && IS_AGGR_TYPE (TREE_TYPE (to))
	       /* [conv.ptr]

		  An rvalue of type "pointer to cv D," where D is a
		  class type, can be converted to an rvalue of type
		  "pointer to cv B," where B is a base class (clause
		  _class.derived_) of D.  If B is an inaccessible
		  (clause _class.access_) or ambiguous
		  (_class.member.lookup_) base class of D, a program
		  that necessitates this conversion is ill-formed.
		  Therefore, we use DERIVED_FROM_P, and do not check
		  access or uniqueness.  */
	       && DERIVED_FROM_P (TREE_TYPE (to), TREE_TYPE (from))
	       /* If FROM is not yet complete, then we must be parsing
		  the body of a class.  We know what's derived from
		  what, but we can't actually perform a
		  derived-to-base conversion.  For example, in:

		     struct D : public B { 
                       static const int i = sizeof((B*)(D*)0);
                     };

                  the D*-to-B* conversion is a reinterpret_cast, not a
		  static_cast.  */
	       && COMPLETE_TYPE_P (TREE_TYPE (from)))
	{
	  from =
	    cp_build_qualified_type (TREE_TYPE (to),
				     cp_type_quals (TREE_TYPE (from)));
	  from = build_pointer_type (from);
	  conv = build_conv (ck_ptr, from, conv);
	  conv->base_p = true;
	}

      if (tcode == POINTER_TYPE)
	{
	  to_pointee = TREE_TYPE (to);
	  from_pointee = TREE_TYPE (from);
	}
      else
	{
	  to_pointee = TYPE_PTRMEM_POINTED_TO_TYPE (to);
	  from_pointee = TYPE_PTRMEM_POINTED_TO_TYPE (from);
	}

      if (same_type_p (from, to))
	/* OK */;
      else if (c_cast_p && comp_ptr_ttypes_const (to, from))
	/* In a C-style cast, we ignore CV-qualification because we
	   are allowed to perform a static_cast followed by a
	   const_cast.  */
	conv = build_conv (ck_qual, to, conv);
      else if (!c_cast_p && comp_ptr_ttypes (to_pointee, from_pointee))
	conv = build_conv (ck_qual, to, conv);
      else if (expr && string_conv_p (to, expr, 0))
	/* converting from string constant to char *.  */
	conv = build_conv (ck_qual, to, conv);
      else if (ptr_reasonably_similar (to_pointee, from_pointee))
	{
	  conv = build_conv (ck_ptr, to, conv);
	  conv->bad_p = true;
	}
      else
	return NULL;

      from = to;
    }
  else if (TYPE_PTRMEMFUNC_P (to) && TYPE_PTRMEMFUNC_P (from))
    {
      tree fromfn = TREE_TYPE (TYPE_PTRMEMFUNC_FN_TYPE (from));
      tree tofn = TREE_TYPE (TYPE_PTRMEMFUNC_FN_TYPE (to));
      tree fbase = TREE_TYPE (TREE_VALUE (TYPE_ARG_TYPES (fromfn)));
      tree tbase = TREE_TYPE (TREE_VALUE (TYPE_ARG_TYPES (tofn)));

      if (!DERIVED_FROM_P (fbase, tbase)
	  || !same_type_p (TREE_TYPE (fromfn), TREE_TYPE (tofn))
	  || !compparms (TREE_CHAIN (TYPE_ARG_TYPES (fromfn)),
			 TREE_CHAIN (TYPE_ARG_TYPES (tofn)))
	  || cp_type_quals (fbase) != cp_type_quals (tbase))
	return NULL;

      from = cp_build_qualified_type (tbase, cp_type_quals (fbase));
      from = build_method_type_directly (from,
					 TREE_TYPE (fromfn),
					 TREE_CHAIN (TYPE_ARG_TYPES (fromfn)));
      from = build_ptrmemfunc_type (build_pointer_type (from));
      conv = build_conv (ck_pmem, from, conv);
      conv->base_p = true;
    }
  else if (tcode == BOOLEAN_TYPE)
    {
      /* [conv.bool]

	  An rvalue of arithmetic, enumeration, pointer, or pointer to
	  member type can be converted to an rvalue of type bool.  */
      if (ARITHMETIC_TYPE_P (from)
	  || fcode == ENUMERAL_TYPE
	  || fcode == POINTER_TYPE
	  || TYPE_PTR_TO_MEMBER_P (from))
	{
	  conv = build_conv (ck_std, to, conv);
	  if (fcode == POINTER_TYPE
	      || TYPE_PTRMEM_P (from)
	      || (TYPE_PTRMEMFUNC_P (from)
		  && conv->rank < cr_pbool))
	    conv->rank = cr_pbool;
	  return conv;
	}

      return NULL;
    }
  /* We don't check for ENUMERAL_TYPE here because there are no standard
     conversions to enum type.  */
  else if (tcode == INTEGER_TYPE || tcode == BOOLEAN_TYPE
	   || tcode == REAL_TYPE)
    {
      if (! (INTEGRAL_CODE_P (fcode) || fcode == REAL_TYPE))
	return NULL;
      conv = build_conv (ck_std, to, conv);

      /* Give this a better rank if it's a promotion.  */
      if (same_type_p (to, type_promotes_to (from))
	  && conv->u.next->rank <= cr_promotion)
	conv->rank = cr_promotion;
    }
  else if (fcode == VECTOR_TYPE && tcode == VECTOR_TYPE
	   && vector_types_convertible_p (from, to))
    return build_conv (ck_std, to, conv);
  else if (!(flags & LOOKUP_CONSTRUCTOR_CALLABLE)
	   && IS_AGGR_TYPE (to) && IS_AGGR_TYPE (from)
	   && is_properly_derived_from (from, to))
    {
      if (conv->kind == ck_rvalue)
	conv = conv->u.next;
      conv = build_conv (ck_base, to, conv);
      /* The derived-to-base conversion indicates the initialization
	 of a parameter with base type from an object of a derived
	 type.  A temporary object is created to hold the result of
	 the conversion.  */
      conv->need_temporary_p = true;
    }
  else
    return NULL;

  return conv;
}

/* Returns nonzero if T1 is reference-related to T2.  */

static bool
reference_related_p (tree t1, tree t2)
{
  t1 = TYPE_MAIN_VARIANT (t1);
  t2 = TYPE_MAIN_VARIANT (t2);

  /* [dcl.init.ref]

     Given types "cv1 T1" and "cv2 T2," "cv1 T1" is reference-related
     to "cv2 T2" if T1 is the same type as T2, or T1 is a base class
     of T2.  */
  return (same_type_p (t1, t2)
	  || (CLASS_TYPE_P (t1) && CLASS_TYPE_P (t2)
	      && DERIVED_FROM_P (t1, t2)));
}

/* Returns nonzero if T1 is reference-compatible with T2.  */

static bool
reference_compatible_p (tree t1, tree t2)
{
  /* [dcl.init.ref]

     "cv1 T1" is reference compatible with "cv2 T2" if T1 is
     reference-related to T2 and cv1 is the same cv-qualification as,
     or greater cv-qualification than, cv2.  */
  return (reference_related_p (t1, t2)
	  && at_least_as_qualified_p (t1, t2));
}

/* Determine whether or not the EXPR (of class type S) can be
   converted to T as in [over.match.ref].  */

static conversion *
convert_class_to_reference (tree t, tree s, tree expr)
{
  tree conversions;
  tree arglist;
  conversion *conv;
  tree reference_type;
  struct z_candidate *candidates;
  struct z_candidate *cand;
  bool any_viable_p;

  conversions = lookup_conversions (s);
  if (!conversions)
    return NULL;

  /* [over.match.ref]

     Assuming that "cv1 T" is the underlying type of the reference
     being initialized, and "cv S" is the type of the initializer
     expression, with S a class type, the candidate functions are
     selected as follows:

     --The conversion functions of S and its base classes are
       considered.  Those that are not hidden within S and yield type
       "reference to cv2 T2", where "cv1 T" is reference-compatible
       (_dcl.init.ref_) with "cv2 T2", are candidate functions.

     The argument list has one argument, which is the initializer
     expression.  */

  candidates = 0;

  /* Conceptually, we should take the address of EXPR and put it in
     the argument list.  Unfortunately, however, that can result in
     error messages, which we should not issue now because we are just
     trying to find a conversion operator.  Therefore, we use NULL,
     cast to the appropriate type.  */
  arglist = build_int_cst (build_pointer_type (s), 0);
  arglist = build_tree_list (NULL_TREE, arglist);

  reference_type = build_reference_type (t);

  while (conversions)
    {
      tree fns = TREE_VALUE (conversions);

      for (; fns; fns = OVL_NEXT (fns))
	{
	  tree f = OVL_CURRENT (fns);
	  tree t2 = TREE_TYPE (TREE_TYPE (f));

	  cand = NULL;

	  /* If this is a template function, try to get an exact
	     match.  */
	  if (TREE_CODE (f) == TEMPLATE_DECL)
	    {
	      cand = add_template_candidate (&candidates,
					     f, s,
					     NULL_TREE,
					     arglist,
					     reference_type,
					     TYPE_BINFO (s),
					     TREE_PURPOSE (conversions),
					     LOOKUP_NORMAL,
					     DEDUCE_CONV);

	      if (cand)
		{
		  /* Now, see if the conversion function really returns
		     an lvalue of the appropriate type.  From the
		     point of view of unification, simply returning an
		     rvalue of the right type is good enough.  */
		  f = cand->fn;
		  t2 = TREE_TYPE (TREE_TYPE (f));
		  if (TREE_CODE (t2) != REFERENCE_TYPE
		      || !reference_compatible_p (t, TREE_TYPE (t2)))
		    {
		      candidates = candidates->next;
		      cand = NULL;
		    }
		}
	    }
	  else if (TREE_CODE (t2) == REFERENCE_TYPE
		   && reference_compatible_p (t, TREE_TYPE (t2)))
	    cand = add_function_candidate (&candidates, f, s, arglist,
					   TYPE_BINFO (s),
					   TREE_PURPOSE (conversions),
					   LOOKUP_NORMAL);

	  if (cand)
	    {
	      conversion *identity_conv;
	      /* Build a standard conversion sequence indicating the
		 binding from the reference type returned by the
		 function to the desired REFERENCE_TYPE.  */
	      identity_conv
		= build_identity_conv (TREE_TYPE (TREE_TYPE
						  (TREE_TYPE (cand->fn))),
				       NULL_TREE);
	      cand->second_conv
		= (direct_reference_binding
		   (reference_type, identity_conv));
	      cand->second_conv->bad_p |= cand->convs[0]->bad_p;
	    }
	}
      conversions = TREE_CHAIN (conversions);
    }

  candidates = splice_viable (candidates, pedantic, &any_viable_p);
  /* If none of the conversion functions worked out, let our caller
     know.  */
  if (!any_viable_p)
    return NULL;

  cand = tourney (candidates);
  if (!cand)
    return NULL;

  /* Now that we know that this is the function we're going to use fix
     the dummy first argument.  */
  cand->args = tree_cons (NULL_TREE,
			  build_this (expr),
			  TREE_CHAIN (cand->args));

  /* Build a user-defined conversion sequence representing the
     conversion.  */
  conv = build_conv (ck_user,
		     TREE_TYPE (TREE_TYPE (cand->fn)),
		     build_identity_conv (TREE_TYPE (expr), expr));
  conv->cand = cand;

  /* Merge it with the standard conversion sequence from the
     conversion function's return type to the desired type.  */
  cand->second_conv = merge_conversion_sequences (conv, cand->second_conv);

  if (cand->viable == -1)
    conv->bad_p = true;

  return cand->second_conv;
}

/* A reference of the indicated TYPE is being bound directly to the
   expression represented by the implicit conversion sequence CONV.
   Return a conversion sequence for this binding.  */

static conversion *
direct_reference_binding (tree type, conversion *conv)
{
  tree t;

  gcc_assert (TREE_CODE (type) == REFERENCE_TYPE);
  gcc_assert (TREE_CODE (conv->type) != REFERENCE_TYPE);

  t = TREE_TYPE (type);

  /* [over.ics.rank]

     When a parameter of reference type binds directly
     (_dcl.init.ref_) to an argument expression, the implicit
     conversion sequence is the identity conversion, unless the
     argument expression has a type that is a derived class of the
     parameter type, in which case the implicit conversion sequence is
     a derived-to-base Conversion.

     If the parameter binds directly to the result of applying a
     conversion function to the argument expression, the implicit
     conversion sequence is a user-defined conversion sequence
     (_over.ics.user_), with the second standard conversion sequence
     either an identity conversion or, if the conversion function
     returns an entity of a type that is a derived class of the
     parameter type, a derived-to-base conversion.  */
  if (!same_type_ignoring_top_level_qualifiers_p (t, conv->type))
    {
      /* Represent the derived-to-base conversion.  */
      conv = build_conv (ck_base, t, conv);
      /* We will actually be binding to the base-class subobject in
	 the derived class, so we mark this conversion appropriately.
	 That way, convert_like knows not to generate a temporary.  */
      conv->need_temporary_p = false;
    }
  return build_conv (ck_ref_bind, type, conv);
}

/* Returns the conversion path from type FROM to reference type TO for
   purposes of reference binding.  For lvalue binding, either pass a
   reference type to FROM or an lvalue expression to EXPR.  If the
   reference will be bound to a temporary, NEED_TEMPORARY_P is set for
   the conversion returned.  If C_CAST_P is true, this
   conversion is coming from a C-style cast.  */

static conversion *
reference_binding (tree rto, tree rfrom, tree expr, bool c_cast_p, int flags)
{
  conversion *conv = NULL;
  tree to = TREE_TYPE (rto);
  tree from = rfrom;
  bool related_p;
  bool compatible_p;
  cp_lvalue_kind lvalue_p = clk_none;

  if (TREE_CODE (to) == FUNCTION_TYPE && expr && type_unknown_p (expr))
    {
      expr = instantiate_type (to, expr, tf_none);
      if (expr == error_mark_node)
	return NULL;
      from = TREE_TYPE (expr);
    }

  if (TREE_CODE (from) == REFERENCE_TYPE)
    {
      /* Anything with reference type is an lvalue.  */
      lvalue_p = clk_ordinary;
      from = TREE_TYPE (from);
    }
  else if (expr)
    lvalue_p = real_lvalue_p (expr);

  /* Figure out whether or not the types are reference-related and
     reference compatible.  We have do do this after stripping
     references from FROM.  */
  related_p = reference_related_p (to, from);
  /* If this is a C cast, first convert to an appropriately qualified
     type, so that we can later do a const_cast to the desired type.  */
  if (related_p && c_cast_p
      && !at_least_as_qualified_p (to, from))
    to = build_qualified_type (to, cp_type_quals (from));
  compatible_p = reference_compatible_p (to, from);

  if (lvalue_p && compatible_p)
    {
      /* [dcl.init.ref]

	 If the initializer expression

	 -- is an lvalue (but not an lvalue for a bit-field), and "cv1 T1"
	    is reference-compatible with "cv2 T2,"

	 the reference is bound directly to the initializer expression
	 lvalue.  */
      conv = build_identity_conv (from, expr);
      conv = direct_reference_binding (rto, conv);
      if ((lvalue_p & clk_bitfield) != 0
	  || ((lvalue_p & clk_packed) != 0 && !TYPE_PACKED (to)))
	/* For the purposes of overload resolution, we ignore the fact
	   this expression is a bitfield or packed field. (In particular,
	   [over.ics.ref] says specifically that a function with a
	   non-const reference parameter is viable even if the
	   argument is a bitfield.)

	   However, when we actually call the function we must create
	   a temporary to which to bind the reference.  If the
	   reference is volatile, or isn't const, then we cannot make
	   a temporary, so we just issue an error when the conversion
	   actually occurs.  */
	conv->need_temporary_p = true;

      return conv;
    }
  else if (CLASS_TYPE_P (from) && !(flags & LOOKUP_NO_CONVERSION))
    {
      /* [dcl.init.ref]

	 If the initializer expression

	 -- has a class type (i.e., T2 is a class type) can be
	    implicitly converted to an lvalue of type "cv3 T3," where
	    "cv1 T1" is reference-compatible with "cv3 T3".  (this
	    conversion is selected by enumerating the applicable
	    conversion functions (_over.match.ref_) and choosing the
	    best one through overload resolution.  (_over.match_).

	the reference is bound to the lvalue result of the conversion
	in the second case.  */
      conv = convert_class_to_reference (to, from, expr);
      if (conv)
	return conv;
    }

  /* From this point on, we conceptually need temporaries, even if we
     elide them.  Only the cases above are "direct bindings".  */
  if (flags & LOOKUP_NO_TEMP_BIND)
    return NULL;

  /* [over.ics.rank]

     When a parameter of reference type is not bound directly to an
     argument expression, the conversion sequence is the one required
     to convert the argument expression to the underlying type of the
     reference according to _over.best.ics_.  Conceptually, this
     conversion sequence corresponds to copy-initializing a temporary
     of the underlying type with the argument expression.  Any
     difference in top-level cv-qualification is subsumed by the
     initialization itself and does not constitute a conversion.  */

  /* [dcl.init.ref]

     Otherwise, the reference shall be to a non-volatile const type.  */
  if (!CP_TYPE_CONST_NON_VOLATILE_P (to))
    return NULL;

  /* [dcl.init.ref]

     If the initializer expression is an rvalue, with T2 a class type,
     and "cv1 T1" is reference-compatible with "cv2 T2", the reference
     is bound in one of the following ways:

     -- The reference is bound to the object represented by the rvalue
	or to a sub-object within that object.

     -- ...

     We use the first alternative.  The implicit conversion sequence
     is supposed to be same as we would obtain by generating a
     temporary.  Fortunately, if the types are reference compatible,
     then this is either an identity conversion or the derived-to-base
     conversion, just as for direct binding.  */
  if (CLASS_TYPE_P (from) && compatible_p)
    {
      conv = build_identity_conv (from, expr);
      conv = direct_reference_binding (rto, conv);
      if (!(flags & LOOKUP_CONSTRUCTOR_CALLABLE))
	conv->u.next->check_copy_constructor_p = true;
      return conv;
    }

  /* [dcl.init.ref]

     Otherwise, a temporary of type "cv1 T1" is created and
     initialized from the initializer expression using the rules for a
     non-reference copy initialization.  If T1 is reference-related to
     T2, cv1 must be the same cv-qualification as, or greater
     cv-qualification than, cv2; otherwise, the program is ill-formed.  */
  if (related_p && !at_least_as_qualified_p (to, from))
    return NULL;

  conv = implicit_conversion (to, from, expr, c_cast_p,
			      flags);
  if (!conv)
    return NULL;

  conv = build_conv (ck_ref_bind, rto, conv);
  /* This reference binding, unlike those above, requires the
     creation of a temporary.  */
  conv->need_temporary_p = true;

  return conv;
}

/* Returns the implicit conversion sequence (see [over.ics]) from type
   FROM to type TO.  The optional expression EXPR may affect the
   conversion.  FLAGS are the usual overloading flags.  Only
   LOOKUP_NO_CONVERSION is significant.  If C_CAST_P is true, this
   conversion is coming from a C-style cast.  */

static conversion *
implicit_conversion (tree to, tree from, tree expr, bool c_cast_p,
		     int flags)
{
  conversion *conv;

  if (from == error_mark_node || to == error_mark_node
      || expr == error_mark_node)
    return NULL;

  if (TREE_CODE (to) == REFERENCE_TYPE)
    conv = reference_binding (to, from, expr, c_cast_p, flags);
  else
    conv = standard_conversion (to, from, expr, c_cast_p, flags);

  if (conv)
    return conv;

  if (expr != NULL_TREE
      && (IS_AGGR_TYPE (from)
	  || IS_AGGR_TYPE (to))
      && (flags & LOOKUP_NO_CONVERSION) == 0)
    {
      struct z_candidate *cand;

      cand = build_user_type_conversion_1
	(to, expr, LOOKUP_ONLYCONVERTING);
      if (cand)
	conv = cand->second_conv;

      /* We used to try to bind a reference to a temporary here, but that
	 is now handled by the recursive call to this function at the end
	 of reference_binding.  */
      return conv;
    }

  return NULL;
}

/* Add a new entry to the list of candidates.  Used by the add_*_candidate
   functions.  */

static struct z_candidate *
add_candidate (struct z_candidate **candidates,
	       tree fn, tree args,
	       size_t num_convs, conversion **convs,
	       tree access_path, tree conversion_path,
	       int viable)
{
  struct z_candidate *cand = (struct z_candidate *)
    conversion_obstack_alloc (sizeof (struct z_candidate));

  cand->fn = fn;
  cand->args = args;
  cand->convs = convs;
  cand->num_convs = num_convs;
  cand->access_path = access_path;
  cand->conversion_path = conversion_path;
  cand->viable = viable;
  cand->next = *candidates;
  *candidates = cand;

  return cand;
}

/* Create an overload candidate for the function or method FN called with
   the argument list ARGLIST and add it to CANDIDATES.  FLAGS is passed on
   to implicit_conversion.

   CTYPE, if non-NULL, is the type we want to pretend this function
   comes from for purposes of overload resolution.  */

static struct z_candidate *
add_function_candidate (struct z_candidate **candidates,
			tree fn, tree ctype, tree arglist,
			tree access_path, tree conversion_path,
			int flags)
{
  tree parmlist = TYPE_ARG_TYPES (TREE_TYPE (fn));
  int i, len;
  conversion **convs;
  tree parmnode, argnode;
  tree orig_arglist;
  int viable = 1;

  /* At this point we should not see any functions which haven't been
     explicitly declared, except for friend functions which will have
     been found using argument dependent lookup.  */
  gcc_assert (!DECL_ANTICIPATED (fn) || DECL_HIDDEN_FRIEND_P (fn));

  /* The `this', `in_chrg' and VTT arguments to constructors are not
     considered in overload resolution.  */
  if (DECL_CONSTRUCTOR_P (fn))
    {
      parmlist = skip_artificial_parms_for (fn, parmlist);
      orig_arglist = arglist;
      arglist = skip_artificial_parms_for (fn, arglist);
    }
  else
    orig_arglist = arglist;

  len = list_length (arglist);
  convs = alloc_conversions (len);

  /* 13.3.2 - Viable functions [over.match.viable]
     First, to be a viable function, a candidate function shall have enough
     parameters to agree in number with the arguments in the list.

     We need to check this first; otherwise, checking the ICSes might cause
     us to produce an ill-formed template instantiation.  */

  parmnode = parmlist;
  for (i = 0; i < len; ++i)
    {
      if (parmnode == NULL_TREE || parmnode == void_list_node)
	break;
      parmnode = TREE_CHAIN (parmnode);
    }

  if (i < len && parmnode)
    viable = 0;

  /* Make sure there are default args for the rest of the parms.  */
  else if (!sufficient_parms_p (parmnode))
    viable = 0;

  if (! viable)
    goto out;

  /* Second, for F to be a viable function, there shall exist for each
     argument an implicit conversion sequence that converts that argument
     to the corresponding parameter of F.  */

  parmnode = parmlist;
  argnode = arglist;

  for (i = 0; i < len; ++i)
    {
      tree arg = TREE_VALUE (argnode);
      tree argtype = lvalue_type (arg);
      conversion *t;
      int is_this;

      if (parmnode == void_list_node)
	break;

      is_this = (i == 0 && DECL_NONSTATIC_MEMBER_FUNCTION_P (fn)
		 && ! DECL_CONSTRUCTOR_P (fn));

      if (parmnode)
	{
	  tree parmtype = TREE_VALUE (parmnode);

	  /* The type of the implicit object parameter ('this') for
	     overload resolution is not always the same as for the
	     function itself; conversion functions are considered to
	     be members of the class being converted, and functions
	     introduced by a using-declaration are considered to be
	     members of the class that uses them.

	     Since build_over_call ignores the ICS for the `this'
	     parameter, we can just change the parm type.  */
	  if (ctype && is_this)
	    {
	      parmtype
		= build_qualified_type (ctype,
					TYPE_QUALS (TREE_TYPE (parmtype)));
	      parmtype = build_pointer_type (parmtype);
	    }

	  t = implicit_conversion (parmtype, argtype, arg,
				   /*c_cast_p=*/false, flags);
	}
      else
	{
	  t = build_identity_conv (argtype, arg);
	  t->ellipsis_p = true;
	}

      if (t && is_this)
	t->this_p = true;

      convs[i] = t;
      if (! t)
	{
	  viable = 0;
	  break;
	}

      if (t->bad_p)
	viable = -1;

      if (parmnode)
	parmnode = TREE_CHAIN (parmnode);
      argnode = TREE_CHAIN (argnode);
    }

 out:
  return add_candidate (candidates, fn, orig_arglist, len, convs,
			access_path, conversion_path, viable);
}

/* Create an overload candidate for the conversion function FN which will
   be invoked for expression OBJ, producing a pointer-to-function which
   will in turn be called with the argument list ARGLIST, and add it to
   CANDIDATES.  FLAGS is passed on to implicit_conversion.

   Actually, we don't really care about FN; we care about the type it
   converts to.  There may be multiple conversion functions that will
   convert to that type, and we rely on build_user_type_conversion_1 to
   choose the best one; so when we create our candidate, we record the type
   instead of the function.  */

static struct z_candidate *
add_conv_candidate (struct z_candidate **candidates, tree fn, tree obj,
		    tree arglist, tree access_path, tree conversion_path)
{
  tree totype = TREE_TYPE (TREE_TYPE (fn));
  int i, len, viable, flags;
  tree parmlist, parmnode, argnode;
  conversion **convs;

  for (parmlist = totype; TREE_CODE (parmlist) != FUNCTION_TYPE; )
    parmlist = TREE_TYPE (parmlist);
  parmlist = TYPE_ARG_TYPES (parmlist);

  len = list_length (arglist) + 1;
  convs = alloc_conversions (len);
  parmnode = parmlist;
  argnode = arglist;
  viable = 1;
  flags = LOOKUP_NORMAL;

  /* Don't bother looking up the same type twice.  */
  if (*candidates && (*candidates)->fn == totype)
    return NULL;

  for (i = 0; i < len; ++i)
    {
      tree arg = i == 0 ? obj : TREE_VALUE (argnode);
      tree argtype = lvalue_type (arg);
      conversion *t;

      if (i == 0)
	t = implicit_conversion (totype, argtype, arg, /*c_cast_p=*/false,
				 flags);
      else if (parmnode == void_list_node)
	break;
      else if (parmnode)
	t = implicit_conversion (TREE_VALUE (parmnode), argtype, arg,
				 /*c_cast_p=*/false, flags);
      else
	{
	  t = build_identity_conv (argtype, arg);
	  t->ellipsis_p = true;
	}

      convs[i] = t;
      if (! t)
	break;

      if (t->bad_p)
	viable = -1;

      if (i == 0)
	continue;

      if (parmnode)
	parmnode = TREE_CHAIN (parmnode);
      argnode = TREE_CHAIN (argnode);
    }

  if (i < len)
    viable = 0;

  if (!sufficient_parms_p (parmnode))
    viable = 0;

  return add_candidate (candidates, totype, arglist, len, convs,
			access_path, conversion_path, viable);
}

static void
build_builtin_candidate (struct z_candidate **candidates, tree fnname,
			 tree type1, tree type2, tree *args, tree *argtypes,
			 int flags)
{
  conversion *t;
  conversion **convs;
  size_t num_convs;
  int viable = 1, i;
  tree types[2];

  types[0] = type1;
  types[1] = type2;

  num_convs =  args[2] ? 3 : (args[1] ? 2 : 1);
  convs = alloc_conversions (num_convs);

  for (i = 0; i < 2; ++i)
    {
      if (! args[i])
	break;

      t = implicit_conversion (types[i], argtypes[i], args[i],
			       /*c_cast_p=*/false, flags);
      if (! t)
	{
	  viable = 0;
	  /* We need something for printing the candidate.  */
	  t = build_identity_conv (types[i], NULL_TREE);
	}
      else if (t->bad_p)
	viable = 0;
      convs[i] = t;
    }

  /* For COND_EXPR we rearranged the arguments; undo that now.  */
  if (args[2])
    {
      convs[2] = convs[1];
      convs[1] = convs[0];
      t = implicit_conversion (boolean_type_node, argtypes[2], args[2],
			       /*c_cast_p=*/false, flags);
      if (t)
	convs[0] = t;
      else
	viable = 0;
    }

  add_candidate (candidates, fnname, /*args=*/NULL_TREE,
		 num_convs, convs,
		 /*access_path=*/NULL_TREE,
		 /*conversion_path=*/NULL_TREE,
		 viable);
}

static bool
is_complete (tree t)
{
  return COMPLETE_TYPE_P (complete_type (t));
}

/* Returns nonzero if TYPE is a promoted arithmetic type.  */

static bool
promoted_arithmetic_type_p (tree type)
{
  /* [over.built]

     In this section, the term promoted integral type is used to refer
     to those integral types which are preserved by integral promotion
     (including e.g.  int and long but excluding e.g.  char).
     Similarly, the term promoted arithmetic type refers to promoted
     integral types plus floating types.  */
  return ((INTEGRAL_TYPE_P (type)
	   && same_type_p (type_promotes_to (type), type))
	  || TREE_CODE (type) == REAL_TYPE);
}

/* Create any builtin operator overload candidates for the operator in
   question given the converted operand types TYPE1 and TYPE2.  The other
   args are passed through from add_builtin_candidates to
   build_builtin_candidate.

   TYPE1 and TYPE2 may not be permissible, and we must filter them.
   If CODE is requires candidates operands of the same type of the kind
   of which TYPE1 and TYPE2 are, we add both candidates
   CODE (TYPE1, TYPE1) and CODE (TYPE2, TYPE2).  */

static void
add_builtin_candidate (struct z_candidate **candidates, enum tree_code code,
		       enum tree_code code2, tree fnname, tree type1,
		       tree type2, tree *args, tree *argtypes, int flags)
{
  switch (code)
    {
    case POSTINCREMENT_EXPR:
    case POSTDECREMENT_EXPR:
      args[1] = integer_zero_node;
      type2 = integer_type_node;
      break;
    default:
      break;
    }

  switch (code)
    {

/* 4 For every pair T, VQ), where T is an arithmetic or  enumeration  type,
     and  VQ  is  either  volatile or empty, there exist candidate operator
     functions of the form
	     VQ T&   operator++(VQ T&);
	     T       operator++(VQ T&, int);
   5 For every pair T, VQ), where T is an enumeration type or an arithmetic
     type  other than bool, and VQ is either volatile or empty, there exist
     candidate operator functions of the form
	     VQ T&   operator--(VQ T&);
	     T       operator--(VQ T&, int);
   6 For every pair T, VQ), where T is  a  cv-qualified  or  cv-unqualified
     complete  object type, and VQ is either volatile or empty, there exist
     candidate operator functions of the form
	     T*VQ&   operator++(T*VQ&);
	     T*VQ&   operator--(T*VQ&);
	     T*      operator++(T*VQ&, int);
	     T*      operator--(T*VQ&, int);  */

    case POSTDECREMENT_EXPR:
    case PREDECREMENT_EXPR:
      if (TREE_CODE (type1) == BOOLEAN_TYPE)
	return;
    case POSTINCREMENT_EXPR:
    case PREINCREMENT_EXPR:
      if (ARITHMETIC_TYPE_P (type1) || TYPE_PTROB_P (type1))
	{
	  type1 = build_reference_type (type1);
	  break;
	}
      return;

/* 7 For every cv-qualified or cv-unqualified complete object type T, there
     exist candidate operator functions of the form

	     T&      operator*(T*);

   8 For every function type T, there exist candidate operator functions of
     the form
	     T&      operator*(T*);  */

    case INDIRECT_REF:
      if (TREE_CODE (type1) == POINTER_TYPE
	  && (TYPE_PTROB_P (type1)
	      || TREE_CODE (TREE_TYPE (type1)) == FUNCTION_TYPE))
	break;
      return;

/* 9 For every type T, there exist candidate operator functions of the form
	     T*      operator+(T*);

   10For  every  promoted arithmetic type T, there exist candidate operator
     functions of the form
	     T       operator+(T);
	     T       operator-(T);  */

    case UNARY_PLUS_EXPR: /* unary + */
      if (TREE_CODE (type1) == POINTER_TYPE)
	break;
    case NEGATE_EXPR:
      if (ARITHMETIC_TYPE_P (type1))
	break;
      return;

/* 11For every promoted integral type T,  there  exist  candidate  operator
     functions of the form
	     T       operator~(T);  */

    case BIT_NOT_EXPR:
      if (INTEGRAL_TYPE_P (type1))
	break;
      return;

/* 12For every quintuple C1, C2, T, CV1, CV2), where C2 is a class type, C1
     is the same type as C2 or is a derived class of C2, T  is  a  complete
     object type or a function type, and CV1 and CV2 are cv-qualifier-seqs,
     there exist candidate operator functions of the form
	     CV12 T& operator->*(CV1 C1*, CV2 T C2::*);
     where CV12 is the union of CV1 and CV2.  */

    case MEMBER_REF:
      if (TREE_CODE (type1) == POINTER_TYPE
	  && TYPE_PTR_TO_MEMBER_P (type2))
	{
	  tree c1 = TREE_TYPE (type1);
	  tree c2 = TYPE_PTRMEM_CLASS_TYPE (type2);

	  if (IS_AGGR_TYPE (c1) && DERIVED_FROM_P (c2, c1)
	      && (TYPE_PTRMEMFUNC_P (type2)
		  || is_complete (TYPE_PTRMEM_POINTED_TO_TYPE (type2))))
	    break;
	}
      return;

/* 13For every pair of promoted arithmetic types L and R, there exist  can-
     didate operator functions of the form
	     LR      operator*(L, R);
	     LR      operator/(L, R);
	     LR      operator+(L, R);
	     LR      operator-(L, R);
	     bool    operator<(L, R);
	     bool    operator>(L, R);
	     bool    operator<=(L, R);
	     bool    operator>=(L, R);
	     bool    operator==(L, R);
	     bool    operator!=(L, R);
     where  LR  is  the  result of the usual arithmetic conversions between
     types L and R.

   14For every pair of types T and I, where T  is  a  cv-qualified  or  cv-
     unqualified  complete  object  type and I is a promoted integral type,
     there exist candidate operator functions of the form
	     T*      operator+(T*, I);
	     T&      operator[](T*, I);
	     T*      operator-(T*, I);
	     T*      operator+(I, T*);
	     T&      operator[](I, T*);

   15For every T, where T is a pointer to complete object type, there exist
     candidate operator functions of the form112)
	     ptrdiff_t operator-(T, T);

   16For every pointer or enumeration type T, there exist candidate operator
     functions of the form
	     bool    operator<(T, T);
	     bool    operator>(T, T);
	     bool    operator<=(T, T);
	     bool    operator>=(T, T);
	     bool    operator==(T, T);
	     bool    operator!=(T, T);

   17For every pointer to member type T,  there  exist  candidate  operator
     functions of the form
	     bool    operator==(T, T);
	     bool    operator!=(T, T);  */

    case MINUS_EXPR:
      if (TYPE_PTROB_P (type1) && TYPE_PTROB_P (type2))
	break;
      if (TYPE_PTROB_P (type1) && INTEGRAL_TYPE_P (type2))
	{
	  type2 = ptrdiff_type_node;
	  break;
	}
    case MULT_EXPR:
    case TRUNC_DIV_EXPR:
      if (ARITHMETIC_TYPE_P (type1) && ARITHMETIC_TYPE_P (type2))
	break;
      return;

    case EQ_EXPR:
    case NE_EXPR:
      if ((TYPE_PTRMEMFUNC_P (type1) && TYPE_PTRMEMFUNC_P (type2))
	  || (TYPE_PTRMEM_P (type1) && TYPE_PTRMEM_P (type2)))
	break;
      if (TYPE_PTR_TO_MEMBER_P (type1) && null_ptr_cst_p (args[1]))
	{
	  type2 = type1;
	  break;
	}
      if (TYPE_PTR_TO_MEMBER_P (type2) && null_ptr_cst_p (args[0]))
	{
	  type1 = type2;
	  break;
	}
      /* Fall through.  */
    case LT_EXPR:
    case GT_EXPR:
    case LE_EXPR:
    case GE_EXPR:
    case MAX_EXPR:
    case MIN_EXPR:
      if (ARITHMETIC_TYPE_P (type1) && ARITHMETIC_TYPE_P (type2))
	break;
      if (TYPE_PTR_P (type1) && TYPE_PTR_P (type2))
	break;
      if (TREE_CODE (type1) == ENUMERAL_TYPE 
	  && TREE_CODE (type2) == ENUMERAL_TYPE)
	break;
      if (TYPE_PTR_P (type1) 
	  && null_ptr_cst_p (args[1])
	  && !uses_template_parms (type1))
	{
	  type2 = type1;
	  break;
	}
      if (null_ptr_cst_p (args[0]) 
	  && TYPE_PTR_P (type2)
	  && !uses_template_parms (type2))
	{
	  type1 = type2;
	  break;
	}
      return;

    case PLUS_EXPR:
      if (ARITHMETIC_TYPE_P (type1) && ARITHMETIC_TYPE_P (type2))
	break;
    case ARRAY_REF:
      if (INTEGRAL_TYPE_P (type1) && TYPE_PTROB_P (type2))
	{
	  type1 = ptrdiff_type_node;
	  break;
	}
      if (TYPE_PTROB_P (type1) && INTEGRAL_TYPE_P (type2))
	{
	  type2 = ptrdiff_type_node;
	  break;
	}
      return;

/* 18For  every pair of promoted integral types L and R, there exist candi-
     date operator functions of the form
	     LR      operator%(L, R);
	     LR      operator&(L, R);
	     LR      operator^(L, R);
	     LR      operator|(L, R);
	     L       operator<<(L, R);
	     L       operator>>(L, R);
     where LR is the result of the  usual  arithmetic  conversions  between
     types L and R.  */

    case TRUNC_MOD_EXPR:
    case BIT_AND_EXPR:
    case BIT_IOR_EXPR:
    case BIT_XOR_EXPR:
    case LSHIFT_EXPR:
    case RSHIFT_EXPR:
      if (INTEGRAL_TYPE_P (type1) && INTEGRAL_TYPE_P (type2))
	break;
      return;

/* 19For  every  triple  L, VQ, R), where L is an arithmetic or enumeration
     type, VQ is either volatile or empty, and R is a  promoted  arithmetic
     type, there exist candidate operator functions of the form
	     VQ L&   operator=(VQ L&, R);
	     VQ L&   operator*=(VQ L&, R);
	     VQ L&   operator/=(VQ L&, R);
	     VQ L&   operator+=(VQ L&, R);
	     VQ L&   operator-=(VQ L&, R);

   20For  every  pair T, VQ), where T is any type and VQ is either volatile
     or empty, there exist candidate operator functions of the form
	     T*VQ&   operator=(T*VQ&, T*);

   21For every pair T, VQ), where T is a pointer to member type and  VQ  is
     either  volatile or empty, there exist candidate operator functions of
     the form
	     VQ T&   operator=(VQ T&, T);

   22For every triple  T,  VQ,  I),  where  T  is  a  cv-qualified  or  cv-
     unqualified  complete object type, VQ is either volatile or empty, and
     I is a promoted integral type, there exist  candidate  operator  func-
     tions of the form
	     T*VQ&   operator+=(T*VQ&, I);
	     T*VQ&   operator-=(T*VQ&, I);

   23For  every  triple  L,  VQ,  R), where L is an integral or enumeration
     type, VQ is either volatile or empty, and R  is  a  promoted  integral
     type, there exist candidate operator functions of the form

	     VQ L&   operator%=(VQ L&, R);
	     VQ L&   operator<<=(VQ L&, R);
	     VQ L&   operator>>=(VQ L&, R);
	     VQ L&   operator&=(VQ L&, R);
	     VQ L&   operator^=(VQ L&, R);
	     VQ L&   operator|=(VQ L&, R);  */

    case MODIFY_EXPR:
      switch (code2)
	{
	case PLUS_EXPR:
	case MINUS_EXPR:
	  if (TYPE_PTROB_P (type1) && INTEGRAL_TYPE_P (type2))
	    {
	      type2 = ptrdiff_type_node;
	      break;
	    }
	case MULT_EXPR:
	case TRUNC_DIV_EXPR:
	  if (ARITHMETIC_TYPE_P (type1) && ARITHMETIC_TYPE_P (type2))
	    break;
	  return;

	case TRUNC_MOD_EXPR:
	case BIT_AND_EXPR:
	case BIT_IOR_EXPR:
	case BIT_XOR_EXPR:
	case LSHIFT_EXPR:
	case RSHIFT_EXPR:
	  if (INTEGRAL_TYPE_P (type1) && INTEGRAL_TYPE_P (type2))
	    break;
	  return;

	case NOP_EXPR:
	  if (ARITHMETIC_TYPE_P (type1) && ARITHMETIC_TYPE_P (type2))
	    break;
	  if ((TYPE_PTRMEMFUNC_P (type1) && TYPE_PTRMEMFUNC_P (type2))
	      || (TYPE_PTR_P (type1) && TYPE_PTR_P (type2))
	      || (TYPE_PTRMEM_P (type1) && TYPE_PTRMEM_P (type2))
	      || ((TYPE_PTRMEMFUNC_P (type1)
		   || TREE_CODE (type1) == POINTER_TYPE)
		  && null_ptr_cst_p (args[1])))
	    {
	      type2 = type1;
	      break;
	    }
	  return;

	default:
	  gcc_unreachable ();
	}
      type1 = build_reference_type (type1);
      break;

    case COND_EXPR:
      /* [over.built]

	 For every pair of promoted arithmetic types L and R, there
	 exist candidate operator functions of the form

	 LR operator?(bool, L, R);

	 where LR is the result of the usual arithmetic conversions
	 between types L and R.

	 For every type T, where T is a pointer or pointer-to-member
	 type, there exist candidate operator functions of the form T
	 operator?(bool, T, T);  */

      if (promoted_arithmetic_type_p (type1)
	  && promoted_arithmetic_type_p (type2))
	/* That's OK.  */
	break;

      /* Otherwise, the types should be pointers.  */
      if (!(TYPE_PTR_P (type1) || TYPE_PTR_TO_MEMBER_P (type1))
	  || !(TYPE_PTR_P (type2) || TYPE_PTR_TO_MEMBER_P (type2)))
	return;

      /* We don't check that the two types are the same; the logic
	 below will actually create two candidates; one in which both
	 parameter types are TYPE1, and one in which both parameter
	 types are TYPE2.  */
      break;

    default:
      gcc_unreachable ();
    }

  /* If we're dealing with two pointer types or two enumeral types,
     we need candidates for both of them.  */
  if (type2 && !same_type_p (type1, type2)
      && TREE_CODE (type1) == TREE_CODE (type2)
      && (TREE_CODE (type1) == REFERENCE_TYPE
	  || (TYPE_PTR_P (type1) && TYPE_PTR_P (type2))
	  || (TYPE_PTRMEM_P (type1) && TYPE_PTRMEM_P (type2))
	  || TYPE_PTRMEMFUNC_P (type1)
	  || IS_AGGR_TYPE (type1)
	  || TREE_CODE (type1) == ENUMERAL_TYPE))
    {
      build_builtin_candidate
	(candidates, fnname, type1, type1, args, argtypes, flags);
      build_builtin_candidate
	(candidates, fnname, type2, type2, args, argtypes, flags);
      return;
    }

  build_builtin_candidate
    (candidates, fnname, type1, type2, args, argtypes, flags);
}

tree
type_decays_to (tree type)
{
  if (TREE_CODE (type) == ARRAY_TYPE)
    return build_pointer_type (TREE_TYPE (type));
  if (TREE_CODE (type) == FUNCTION_TYPE)
    return build_pointer_type (type);
  return type;
}

/* There are three conditions of builtin candidates:

   1) bool-taking candidates.  These are the same regardless of the input.
   2) pointer-pair taking candidates.  These are generated for each type
      one of the input types converts to.
   3) arithmetic candidates.  According to the standard, we should generate
      all of these, but I'm trying not to...

   Here we generate a superset of the possible candidates for this particular
   case.  That is a subset of the full set the standard defines, plus some
   other cases which the standard disallows. add_builtin_candidate will
   filter out the invalid set.  */

static void
add_builtin_candidates (struct z_candidate **candidates, enum tree_code code,
			enum tree_code code2, tree fnname, tree *args,
			int flags)
{
  int ref1, i;
  int enum_p = 0;
  tree type, argtypes[3];
  /* TYPES[i] is the set of possible builtin-operator parameter types
     we will consider for the Ith argument.  These are represented as
     a TREE_LIST; the TREE_VALUE of each node is the potential
     parameter type.  */
  tree types[2];

  for (i = 0; i < 3; ++i)
    {
      if (args[i])
	argtypes[i]  = lvalue_type (args[i]);
      else
	argtypes[i] = NULL_TREE;
    }

  switch (code)
    {
/* 4 For every pair T, VQ), where T is an arithmetic or  enumeration  type,
     and  VQ  is  either  volatile or empty, there exist candidate operator
     functions of the form
		 VQ T&   operator++(VQ T&);  */

    case POSTINCREMENT_EXPR:
    case PREINCREMENT_EXPR:
    case POSTDECREMENT_EXPR:
    case PREDECREMENT_EXPR:
    case MODIFY_EXPR:
      ref1 = 1;
      break;

/* 24There also exist candidate operator functions of the form
	     bool    operator!(bool);
	     bool    operator&&(bool, bool);
	     bool    operator||(bool, bool);  */

    case TRUTH_NOT_EXPR:
      build_builtin_candidate
	(candidates, fnname, boolean_type_node,
	 NULL_TREE, args, argtypes, flags);
      return;

    case TRUTH_ORIF_EXPR:
    case TRUTH_ANDIF_EXPR:
      build_builtin_candidate
	(candidates, fnname, boolean_type_node,
	 boolean_type_node, args, argtypes, flags);
      return;

    case ADDR_EXPR:
    case COMPOUND_EXPR:
    case COMPONENT_REF:
      return;

    case COND_EXPR:
    case EQ_EXPR:
    case NE_EXPR:
    case LT_EXPR:
    case LE_EXPR:
    case GT_EXPR:
    case GE_EXPR:
      enum_p = 1;
      /* Fall through.  */

    default:
      ref1 = 0;
    }

  types[0] = types[1] = NULL_TREE;

  for (i = 0; i < 2; ++i)
    {
      if (! args[i])
	;
      else if (IS_AGGR_TYPE (argtypes[i]))
	{
	  tree convs;

	  if (i == 0 && code == MODIFY_EXPR && code2 == NOP_EXPR)
	    return;

	  convs = lookup_conversions (argtypes[i]);

	  if (code == COND_EXPR)
	    {
	      if (real_lvalue_p (args[i]))
		types[i] = tree_cons
		  (NULL_TREE, build_reference_type (argtypes[i]), types[i]);

	      types[i] = tree_cons
		(NULL_TREE, TYPE_MAIN_VARIANT (argtypes[i]), types[i]);
	    }

	  else if (! convs)
	    return;

	  for (; convs; convs = TREE_CHAIN (convs))
	    {
	      type = TREE_TYPE (TREE_TYPE (OVL_CURRENT (TREE_VALUE (convs))));

	      if (i == 0 && ref1
		  && (TREE_CODE (type) != REFERENCE_TYPE
		      || CP_TYPE_CONST_P (TREE_TYPE (type))))
		continue;

	      if (code == COND_EXPR && TREE_CODE (type) == REFERENCE_TYPE)
		types[i] = tree_cons (NULL_TREE, type, types[i]);

	      type = non_reference (type);
	      if (i != 0 || ! ref1)
		{
		  type = TYPE_MAIN_VARIANT (type_decays_to (type));
		  if (enum_p && TREE_CODE (type) == ENUMERAL_TYPE)
		    types[i] = tree_cons (NULL_TREE, type, types[i]);
		  if (INTEGRAL_TYPE_P (type))
		    type = type_promotes_to (type);
		}

	      if (! value_member (type, types[i]))
		types[i] = tree_cons (NULL_TREE, type, types[i]);
	    }
	}
      else
	{
	  if (code == COND_EXPR && real_lvalue_p (args[i]))
	    types[i] = tree_cons
	      (NULL_TREE, build_reference_type (argtypes[i]), types[i]);
	  type = non_reference (argtypes[i]);
	  if (i != 0 || ! ref1)
	    {
	      type = TYPE_MAIN_VARIANT (type_decays_to (type));
	      if (enum_p && TREE_CODE (type) == ENUMERAL_TYPE)
		types[i] = tree_cons (NULL_TREE, type, types[i]);
	      if (INTEGRAL_TYPE_P (type))
		type = type_promotes_to (type);
	    }
	  types[i] = tree_cons (NULL_TREE, type, types[i]);
	}
    }

  /* Run through the possible parameter types of both arguments,
     creating candidates with those parameter types.  */
  for (; types[0]; types[0] = TREE_CHAIN (types[0]))
    {
      if (types[1])
	for (type = types[1]; type; type = TREE_CHAIN (type))
	  add_builtin_candidate
	    (candidates, code, code2, fnname, TREE_VALUE (types[0]),
	     TREE_VALUE (type), args, argtypes, flags);
      else
	add_builtin_candidate
	  (candidates, code, code2, fnname, TREE_VALUE (types[0]),
	   NULL_TREE, args, argtypes, flags);
    }
}


/* If TMPL can be successfully instantiated as indicated by
   EXPLICIT_TARGS and ARGLIST, adds the instantiation to CANDIDATES.

   TMPL is the template.  EXPLICIT_TARGS are any explicit template
   arguments.  ARGLIST is the arguments provided at the call-site.
   The RETURN_TYPE is the desired type for conversion operators.  If
   OBJ is NULL_TREE, FLAGS and CTYPE are as for add_function_candidate.
   If an OBJ is supplied, FLAGS and CTYPE are ignored, and OBJ is as for
   add_conv_candidate.  */

static struct z_candidate*
add_template_candidate_real (struct z_candidate **candidates, tree tmpl,
			     tree ctype, tree explicit_targs, tree arglist,
			     tree return_type, tree access_path,
			     tree conversion_path, int flags, tree obj,
			     unification_kind_t strict)
{
  int ntparms = DECL_NTPARMS (tmpl);
  tree targs = make_tree_vec (ntparms);
  tree args_without_in_chrg = arglist;
  struct z_candidate *cand;
  int i;
  tree fn;

  /* We don't do deduction on the in-charge parameter, the VTT
     parameter or 'this'.  */
  if (DECL_NONSTATIC_MEMBER_FUNCTION_P (tmpl))
    args_without_in_chrg = TREE_CHAIN (args_without_in_chrg);

  if ((DECL_MAYBE_IN_CHARGE_CONSTRUCTOR_P (tmpl)
       || DECL_BASE_CONSTRUCTOR_P (tmpl))
      && CLASSTYPE_VBASECLASSES (DECL_CONTEXT (tmpl)))
    args_without_in_chrg = TREE_CHAIN (args_without_in_chrg);

  i = fn_type_unification (tmpl, explicit_targs, targs,
			   args_without_in_chrg,
			   return_type, strict, flags);

  if (i != 0)
    return NULL;

  fn = instantiate_template (tmpl, targs, tf_none);
  if (fn == error_mark_node)
    return NULL;

  /* In [class.copy]:

       A member function template is never instantiated to perform the
       copy of a class object to an object of its class type.

     It's a little unclear what this means; the standard explicitly
     does allow a template to be used to copy a class.  For example,
     in:

       struct A {
	 A(A&);
	 template <class T> A(const T&);
       };
       const A f ();
       void g () { A a (f ()); }

     the member template will be used to make the copy.  The section
     quoted above appears in the paragraph that forbids constructors
     whose only parameter is (a possibly cv-qualified variant of) the
     class type, and a logical interpretation is that the intent was
     to forbid the instantiation of member templates which would then
     have that form.  */
  if (DECL_CONSTRUCTOR_P (fn) && list_length (arglist) == 2)
    {
      tree arg_types = FUNCTION_FIRST_USER_PARMTYPE (fn);
      if (arg_types && same_type_p (TYPE_MAIN_VARIANT (TREE_VALUE (arg_types)),
				    ctype))
	return NULL;
    }

  if (obj != NULL_TREE)
    /* Aha, this is a conversion function.  */
    cand = add_conv_candidate (candidates, fn, obj, access_path,
			       conversion_path, arglist);
  else
    cand = add_function_candidate (candidates, fn, ctype,
				   arglist, access_path,
				   conversion_path, flags);
  if (DECL_TI_TEMPLATE (fn) != tmpl)
    /* This situation can occur if a member template of a template
       class is specialized.  Then, instantiate_template might return
       an instantiation of the specialization, in which case the
       DECL_TI_TEMPLATE field will point at the original
       specialization.  For example:

	 template <class T> struct S { template <class U> void f(U);
				       template <> void f(int) {}; };
	 S<double> sd;
	 sd.f(3);

       Here, TMPL will be template <class U> S<double>::f(U).
       And, instantiate template will give us the specialization
       template <> S<double>::f(int).  But, the DECL_TI_TEMPLATE field
       for this will point at template <class T> template <> S<T>::f(int),
       so that we can find the definition.  For the purposes of
       overload resolution, however, we want the original TMPL.  */
    cand->template_decl = tree_cons (tmpl, targs, NULL_TREE);
  else
    cand->template_decl = DECL_TEMPLATE_INFO (fn);

  return cand;
}


static struct z_candidate *
add_template_candidate (struct z_candidate **candidates, tree tmpl, tree ctype,
			tree explicit_targs, tree arglist, tree return_type,
			tree access_path, tree conversion_path, int flags,
			unification_kind_t strict)
{
  return
    add_template_candidate_real (candidates, tmpl, ctype,
				 explicit_targs, arglist, return_type,
				 access_path, conversion_path,
				 flags, NULL_TREE, strict);
}


static struct z_candidate *
add_template_conv_candidate (struct z_candidate **candidates, tree tmpl,
			     tree obj, tree arglist, tree return_type,
			     tree access_path, tree conversion_path)
{
  return
    add_template_candidate_real (candidates, tmpl, NULL_TREE, NULL_TREE,
				 arglist, return_type, access_path,
				 conversion_path, 0, obj, DEDUCE_CONV);
}

/* The CANDS are the set of candidates that were considered for
   overload resolution.  Return the set of viable candidates.  If none
   of the candidates were viable, set *ANY_VIABLE_P to true.  STRICT_P
   is true if a candidate should be considered viable only if it is
   strictly viable.  */

static struct z_candidate*
splice_viable (struct z_candidate *cands,
	       bool strict_p,
	       bool *any_viable_p)
{
  struct z_candidate *viable;
  struct z_candidate **last_viable;
  struct z_candidate **cand;

  viable = NULL;
  last_viable = &viable;
  *any_viable_p = false;

  cand = &cands;
  while (*cand)
    {
      struct z_candidate *c = *cand;
      if (strict_p ? c->viable == 1 : c->viable)
	{
	  *last_viable = c;
	  *cand = c->next;
	  c->next = NULL;
	  last_viable = &c->next;
	  *any_viable_p = true;
	}
      else
	cand = &c->next;
    }

  return viable ? viable : cands;
}

static bool
any_strictly_viable (struct z_candidate *cands)
{
  for (; cands; cands = cands->next)
    if (cands->viable == 1)
      return true;
  return false;
}

/* OBJ is being used in an expression like "OBJ.f (...)".  In other
   words, it is about to become the "this" pointer for a member
   function call.  Take the address of the object.  */

static tree
build_this (tree obj)
{
  /* In a template, we are only concerned about the type of the
     expression, so we can take a shortcut.  */
  if (processing_template_decl)
    return build_address (obj);

  return build_unary_op (ADDR_EXPR, obj, 0);
}

/* Returns true iff functions are equivalent. Equivalent functions are
   not '==' only if one is a function-local extern function or if
   both are extern "C".  */

static inline int
equal_functions (tree fn1, tree fn2)
{
  if (DECL_LOCAL_FUNCTION_P (fn1) || DECL_LOCAL_FUNCTION_P (fn2)
      || DECL_EXTERN_C_FUNCTION_P (fn1))
    return decls_match (fn1, fn2);
  return fn1 == fn2;
}

/* Print information about one overload candidate CANDIDATE.  MSGSTR
   is the text to print before the candidate itself.

   NOTE: Unlike most diagnostic functions in GCC, MSGSTR is expected
   to have been run through gettext by the caller.  This wart makes
   life simpler in print_z_candidates and for the translators.  */

static void
print_z_candidate (const char *msgstr, struct z_candidate *candidate)
{
  if (TREE_CODE (candidate->fn) == IDENTIFIER_NODE)
    {
      if (candidate->num_convs == 3)
	inform ("%s %D(%T, %T, %T) <built-in>", msgstr, candidate->fn,
		candidate->convs[0]->type,
		candidate->convs[1]->type,
		candidate->convs[2]->type);
      else if (candidate->num_convs == 2)
	inform ("%s %D(%T, %T) <built-in>", msgstr, candidate->fn,
		candidate->convs[0]->type,
		candidate->convs[1]->type);
      else
	inform ("%s %D(%T) <built-in>", msgstr, candidate->fn,
		candidate->convs[0]->type);
    }
  else if (TYPE_P (candidate->fn))
    inform ("%s %T <conversion>", msgstr, candidate->fn);
  else if (candidate->viable == -1)
    inform ("%s %+#D <near match>", msgstr, candidate->fn);
  else
    inform ("%s %+#D", msgstr, candidate->fn);
}

static void
print_z_candidates (struct z_candidate *candidates)
{
  const char *str;
  struct z_candidate *cand1;
  struct z_candidate **cand2;

  /* There may be duplicates in the set of candidates.  We put off
     checking this condition as long as possible, since we have no way
     to eliminate duplicates from a set of functions in less than n^2
     time.  Now we are about to emit an error message, so it is more
     permissible to go slowly.  */
  for (cand1 = candidates; cand1; cand1 = cand1->next)
    {
      tree fn = cand1->fn;
      /* Skip builtin candidates and conversion functions.  */
      if (TREE_CODE (fn) != FUNCTION_DECL)
	continue;
      cand2 = &cand1->next;
      while (*cand2)
	{
	  if (TREE_CODE ((*cand2)->fn) == FUNCTION_DECL
	      && equal_functions (fn, (*cand2)->fn))
	    *cand2 = (*cand2)->next;
	  else
	    cand2 = &(*cand2)->next;
	}
    }

  if (!candidates)
    return;

  str = _("candidates are:");
  print_z_candidate (str, candidates);
  if (candidates->next)
    {
      /* Indent successive candidates by the width of the translation
	 of the above string.  */
      size_t len = gcc_gettext_width (str) + 1;
      char *spaces = (char *) alloca (len);
      memset (spaces, ' ', len-1);
      spaces[len - 1] = '\0';

      candidates = candidates->next;
      do
	{
	  print_z_candidate (spaces, candidates);
	  candidates = candidates->next;
	}
      while (candidates);
    }
}

/* USER_SEQ is a user-defined conversion sequence, beginning with a
   USER_CONV.  STD_SEQ is the standard conversion sequence applied to
   the result of the conversion function to convert it to the final
   desired type.  Merge the two sequences into a single sequence,
   and return the merged sequence.  */

static conversion *
merge_conversion_sequences (conversion *user_seq, conversion *std_seq)
{
  conversion **t;

  gcc_assert (user_seq->kind == ck_user);

  /* Find the end of the second conversion sequence.  */
  t = &(std_seq);
  while ((*t)->kind != ck_identity)
    t = &((*t)->u.next);

  /* Replace the identity conversion with the user conversion
     sequence.  */
  *t = user_seq;

  /* The entire sequence is a user-conversion sequence.  */
  std_seq->user_conv_p = true;

  return std_seq;
}

/* Returns the best overload candidate to perform the requested
   conversion.  This function is used for three the overloading situations
   described in [over.match.copy], [over.match.conv], and [over.match.ref].
   If TOTYPE is a REFERENCE_TYPE, we're trying to find an lvalue binding as
   per [dcl.init.ref], so we ignore temporary bindings.  */

static struct z_candidate *
build_user_type_conversion_1 (tree totype, tree expr, int flags)
{
  struct z_candidate *candidates, *cand;
  tree fromtype = TREE_TYPE (expr);
  tree ctors = NULL_TREE;
  tree conv_fns = NULL_TREE;
  conversion *conv = NULL;
  tree args = NULL_TREE;
  bool any_viable_p;

  /* We represent conversion within a hierarchy using RVALUE_CONV and
     BASE_CONV, as specified by [over.best.ics]; these become plain
     constructor calls, as specified in [dcl.init].  */
  gcc_assert (!IS_AGGR_TYPE (fromtype) || !IS_AGGR_TYPE (totype)
	      || !DERIVED_FROM_P (totype, fromtype));

  if (IS_AGGR_TYPE (totype))
    ctors = lookup_fnfields (totype, complete_ctor_identifier, 0);

  if (IS_AGGR_TYPE (fromtype))
    conv_fns = lookup_conversions (fromtype);

  candidates = 0;
  flags |= LOOKUP_NO_CONVERSION;

  if (ctors)
    {
      tree t;

      ctors = BASELINK_FUNCTIONS (ctors);

      t = build_int_cst (build_pointer_type (totype), 0);
      args = build_tree_list (NULL_TREE, expr);
      /* We should never try to call the abstract or base constructor
	 from here.  */
      gcc_assert (!DECL_HAS_IN_CHARGE_PARM_P (OVL_CURRENT (ctors))
		  && !DECL_HAS_VTT_PARM_P (OVL_CURRENT (ctors)));
      args = tree_cons (NULL_TREE, t, args);
    }
  for (; ctors; ctors = OVL_NEXT (ctors))
    {
      tree ctor = OVL_CURRENT (ctors);
      if (DECL_NONCONVERTING_P (ctor))
	continue;

      if (TREE_CODE (ctor) == TEMPLATE_DECL)
	cand = add_template_candidate (&candidates, ctor, totype,
				       NULL_TREE, args, NULL_TREE,
				       TYPE_BINFO (totype),
				       TYPE_BINFO (totype),
				       flags,
				       DEDUCE_CALL);
      else
	cand = add_function_candidate (&candidates, ctor, totype,
				       args, TYPE_BINFO (totype),
				       TYPE_BINFO (totype),
				       flags);

      if (cand)
	cand->second_conv = build_identity_conv (totype, NULL_TREE);
    }

  if (conv_fns)
    args = build_tree_list (NULL_TREE, build_this (expr));

  for (; conv_fns; conv_fns = TREE_CHAIN (conv_fns))
    {
      tree fns;
      tree conversion_path = TREE_PURPOSE (conv_fns);
      int convflags = LOOKUP_NO_CONVERSION;

      /* If we are called to convert to a reference type, we are trying to
	 find an lvalue binding, so don't even consider temporaries.  If
	 we don't find an lvalue binding, the caller will try again to
	 look for a temporary binding.  */
      if (TREE_CODE (totype) == REFERENCE_TYPE)
	convflags |= LOOKUP_NO_TEMP_BIND;

      for (fns = TREE_VALUE (conv_fns); fns; fns = OVL_NEXT (fns))
	{
	  tree fn = OVL_CURRENT (fns);

	  /* [over.match.funcs] For conversion functions, the function
	     is considered to be a member of the class of the implicit
	     object argument for the purpose of defining the type of
	     the implicit object parameter.

	     So we pass fromtype as CTYPE to add_*_candidate.  */

	  if (TREE_CODE (fn) == TEMPLATE_DECL)
	    cand = add_template_candidate (&candidates, fn, fromtype,
					   NULL_TREE,
					   args, totype,
					   TYPE_BINFO (fromtype),
					   conversion_path,
					   flags,
					   DEDUCE_CONV);
	  else
	    cand = add_function_candidate (&candidates, fn, fromtype,
					   args,
					   TYPE_BINFO (fromtype),
					   conversion_path,
					   flags);

	  if (cand)
	    {
	      conversion *ics
		= implicit_conversion (totype,
				       TREE_TYPE (TREE_TYPE (cand->fn)),
				       0,
				       /*c_cast_p=*/false, convflags);

	      cand->second_conv = ics;

	      if (!ics)
		cand->viable = 0;
	      else if (candidates->viable == 1 && ics->bad_p)
		cand->viable = -1;
	    }
	}
    }

  candidates = splice_viable (candidates, pedantic, &any_viable_p);
  if (!any_viable_p)
    return NULL;

  cand = tourney (candidates);
  if (cand == 0)
    {
      if (flags & LOOKUP_COMPLAIN)
	{
	  error ("conversion from %qT to %qT is ambiguous",
		    fromtype, totype);
	  print_z_candidates (candidates);
	}

      cand = candidates;	/* any one will do */
      cand->second_conv = build_ambiguous_conv (totype, expr);
      cand->second_conv->user_conv_p = true;
      if (!any_strictly_viable (candidates))
	cand->second_conv->bad_p = true;
      /* If there are viable candidates, don't set ICS_BAD_FLAG; an
	 ambiguous conversion is no worse than another user-defined
	 conversion.  */

      return cand;
    }

  /* Build the user conversion sequence.  */
  conv = build_conv
    (ck_user,
     (DECL_CONSTRUCTOR_P (cand->fn)
      ? totype : non_reference (TREE_TYPE (TREE_TYPE (cand->fn)))),
     build_identity_conv (TREE_TYPE (expr), expr));
  conv->cand = cand;

  /* Combine it with the second conversion sequence.  */
  cand->second_conv = merge_conversion_sequences (conv,
						  cand->second_conv);

  if (cand->viable == -1)
    cand->second_conv->bad_p = true;

  return cand;
}

tree
build_user_type_conversion (tree totype, tree expr, int flags)
{
  struct z_candidate *cand
    = build_user_type_conversion_1 (totype, expr, flags);

  if (cand)
    {
      if (cand->second_conv->kind == ck_ambig)
	return error_mark_node;
      expr = convert_like (cand->second_conv, expr);
      return convert_from_reference (expr);
    }
  return NULL_TREE;
}

/* Do any initial processing on the arguments to a function call.  */

static tree
resolve_args (tree args)
{
  tree t;
  for (t = args; t; t = TREE_CHAIN (t))
    {
      tree arg = TREE_VALUE (t);

      if (error_operand_p (arg))
	return error_mark_node;
      else if (VOID_TYPE_P (TREE_TYPE (arg)))
	{
	  error ("invalid use of void expression");
	  return error_mark_node;
	}
      else if (invalid_nonstatic_memfn_p (arg))
	return error_mark_node;
    }
  return args;
}

/* Perform overload resolution on FN, which is called with the ARGS.

   Return the candidate function selected by overload resolution, or
   NULL if the event that overload resolution failed.  In the case
   that overload resolution fails, *CANDIDATES will be the set of
   candidates considered, and ANY_VIABLE_P will be set to true or
   false to indicate whether or not any of the candidates were
   viable.

   The ARGS should already have gone through RESOLVE_ARGS before this
   function is called.  */

static struct z_candidate *
perform_overload_resolution (tree fn,
			     tree args,
			     struct z_candidate **candidates,
			     bool *any_viable_p)
{
  struct z_candidate *cand;
  tree explicit_targs = NULL_TREE;
  int template_only = 0;

  *candidates = NULL;
  *any_viable_p = true;

  /* Check FN and ARGS.  */
  gcc_assert (TREE_CODE (fn) == FUNCTION_DECL
	      || TREE_CODE (fn) == TEMPLATE_DECL
	      || TREE_CODE (fn) == OVERLOAD
	      || TREE_CODE (fn) == TEMPLATE_ID_EXPR);
  gcc_assert (!args || TREE_CODE (args) == TREE_LIST);

  if (TREE_CODE (fn) == TEMPLATE_ID_EXPR)
    {
      explicit_targs = TREE_OPERAND (fn, 1);
      fn = TREE_OPERAND (fn, 0);
      template_only = 1;
    }

  /* Add the various candidate functions.  */
  add_candidates (fn, args, explicit_targs, template_only,
		  /*conversion_path=*/NULL_TREE,
		  /*access_path=*/NULL_TREE,
		  LOOKUP_NORMAL,
		  candidates);

  *candidates = splice_viable (*candidates, pedantic, any_viable_p);
  if (!*any_viable_p)
    return NULL;

  cand = tourney (*candidates);
  return cand;
}

/* Return an expression for a call to FN (a namespace-scope function,
   or a static member function) with the ARGS.  */

tree
build_new_function_call (tree fn, tree args, bool koenig_p)
{
  struct z_candidate *candidates, *cand;
  bool any_viable_p;
  void *p;
  tree result;

  args = resolve_args (args);
  if (args == error_mark_node)
    return error_mark_node;

  /* If this function was found without using argument dependent
     lookup, then we want to ignore any undeclared friend
     functions.  */
  if (!koenig_p)
    {
      tree orig_fn = fn;

      fn = remove_hidden_names (fn);
      if (!fn)
	{
	  error ("no matching function for call to %<%D(%A)%>",
		 DECL_NAME (OVL_CURRENT (orig_fn)), args);
	  return error_mark_node;
	}
    }

  /* Get the high-water mark for the CONVERSION_OBSTACK.  */
  p = conversion_obstack_alloc (0);

  cand = perform_overload_resolution (fn, args, &candidates, &any_viable_p);

  if (!cand)
    {
      if (!any_viable_p && candidates && ! candidates->next)
	return build_function_call (candidates->fn, args);
      if (TREE_CODE (fn) == TEMPLATE_ID_EXPR)
	fn = TREE_OPERAND (fn, 0);
      if (!any_viable_p)
	error ("no matching function for call to %<%D(%A)%>",
	       DECL_NAME (OVL_CURRENT (fn)), args);
      else
	error ("call of overloaded %<%D(%A)%> is ambiguous",
	       DECL_NAME (OVL_CURRENT (fn)), args);
      if (candidates)
	print_z_candidates (candidates);
      result = error_mark_node;
    }
  else
    result = build_over_call (cand, LOOKUP_NORMAL);

  /* Free all the conversions we allocated.  */
  obstack_free (&conversion_obstack, p);

  return result;
}

/* Build a call to a global operator new.  FNNAME is the name of the
   operator (either "operator new" or "operator new[]") and ARGS are
   the arguments provided.  *SIZE points to the total number of bytes
   required by the allocation, and is updated if that is changed here.
   *COOKIE_SIZE is non-NULL if a cookie should be used.  If this
   function determines that no cookie should be used, after all,
   *COOKIE_SIZE is set to NULL_TREE.  If FN is non-NULL, it will be
   set, upon return, to the allocation function called.  */

tree
build_operator_new_call (tree fnname, tree args,
			 tree *size, tree *cookie_size,
			 tree *fn)
{
  tree fns;
  struct z_candidate *candidates;
  struct z_candidate *cand;
  bool any_viable_p;

  if (fn)
    *fn = NULL_TREE;
  args = tree_cons (NULL_TREE, *size, args);
  args = resolve_args (args);
  if (args == error_mark_node)
    return args;

  /* Based on:

       [expr.new]

       If this lookup fails to find the name, or if the allocated type
       is not a class type, the allocation function's name is looked
       up in the global scope.

     we disregard block-scope declarations of "operator new".  */
  fns = lookup_function_nonclass (fnname, args, /*block_p=*/false);

  /* Figure out what function is being called.  */
  cand = perform_overload_resolution (fns, args, &candidates, &any_viable_p);

  /* If no suitable function could be found, issue an error message
     and give up.  */
  if (!cand)
    {
      if (!any_viable_p)
	error ("no matching function for call to %<%D(%A)%>",
	       DECL_NAME (OVL_CURRENT (fns)), args);
      else
	error ("call of overloaded %<%D(%A)%> is ambiguous",
	       DECL_NAME (OVL_CURRENT (fns)), args);
      if (candidates)
	print_z_candidates (candidates);
      return error_mark_node;
    }

   /* If a cookie is required, add some extra space.  Whether
      or not a cookie is required cannot be determined until
      after we know which function was called.  */
   if (*cookie_size)
     {
       bool use_cookie = true;
       if (!abi_version_at_least (2))
	 {
	   tree placement = TREE_CHAIN (args);
	   /* In G++ 3.2, the check was implemented incorrectly; it
	      looked at the placement expression, rather than the
	      type of the function.  */
	   if (placement && !TREE_CHAIN (placement)
	       && same_type_p (TREE_TYPE (TREE_VALUE (placement)),
			       ptr_type_node))
	     use_cookie = false;
	 }
       else
	 {
	   tree arg_types;

	   arg_types = TYPE_ARG_TYPES (TREE_TYPE (cand->fn));
	   /* Skip the size_t parameter.  */
	   arg_types = TREE_CHAIN (arg_types);
	   /* Check the remaining parameters (if any).  */
	   if (arg_types
	       && TREE_CHAIN (arg_types) == void_list_node
	       && same_type_p (TREE_VALUE (arg_types),
			       ptr_type_node))
	     use_cookie = false;
	 }
       /* If we need a cookie, adjust the number of bytes allocated.  */
       if (use_cookie)
	 {
	   /* Update the total size.  */
	   *size = size_binop (PLUS_EXPR, *size, *cookie_size);
	   /* Update the argument list to reflect the adjusted size.  */
	   TREE_VALUE (args) = *size;
	 }
       else
	 *cookie_size = NULL_TREE;
     }

   /* Tell our caller which function we decided to call.  */
   if (fn)
     *fn = cand->fn;

   /* Build the CALL_EXPR.  */
   return build_over_call (cand, LOOKUP_NORMAL);
}

static tree
build_object_call (tree obj, tree args)
{
  struct z_candidate *candidates = 0, *cand;
  tree fns, convs, mem_args = NULL_TREE;
  tree type = TREE_TYPE (obj);
  bool any_viable_p;
  tree result = NULL_TREE;
  void *p;

  if (TYPE_PTRMEMFUNC_P (type))
    {
      /* It's no good looking for an overloaded operator() on a
	 pointer-to-member-function.  */
      error ("pointer-to-member function %E cannot be called without an object; consider using .* or ->*", obj);
      return error_mark_node;
    }

  if (TYPE_BINFO (type))
    {
      fns = lookup_fnfields (TYPE_BINFO (type), ansi_opname (CALL_EXPR), 1);
      if (fns == error_mark_node)
	return error_mark_node;
    }
  else
    fns = NULL_TREE;

  args = resolve_args (args);

  if (args == error_mark_node)
    return error_mark_node;

  /* Get the high-water mark for the CONVERSION_OBSTACK.  */
  p = conversion_obstack_alloc (0);

  if (fns)
    {
      tree base = BINFO_TYPE (BASELINK_BINFO (fns));
      mem_args = tree_cons (NULL_TREE, build_this (obj), args);

      for (fns = BASELINK_FUNCTIONS (fns); fns; fns = OVL_NEXT (fns))
	{
	  tree fn = OVL_CURRENT (fns);
	  if (TREE_CODE (fn) == TEMPLATE_DECL)
	    add_template_candidate (&candidates, fn, base, NULL_TREE,
				    mem_args, NULL_TREE,
				    TYPE_BINFO (type),
				    TYPE_BINFO (type),
				    LOOKUP_NORMAL, DEDUCE_CALL);
	  else
	    add_function_candidate
	      (&candidates, fn, base, mem_args, TYPE_BINFO (type),
	       TYPE_BINFO (type), LOOKUP_NORMAL);
	}
    }

  convs = lookup_conversions (type);

  for (; convs; convs = TREE_CHAIN (convs))
    {
      tree fns = TREE_VALUE (convs);
      tree totype = TREE_TYPE (TREE_TYPE (OVL_CURRENT (fns)));

      if ((TREE_CODE (totype) == POINTER_TYPE
	   && TREE_CODE (TREE_TYPE (totype)) == FUNCTION_TYPE)
	  || (TREE_CODE (totype) == REFERENCE_TYPE
	      && TREE_CODE (TREE_TYPE (totype)) == FUNCTION_TYPE)
	  || (TREE_CODE (totype) == REFERENCE_TYPE
	      && TREE_CODE (TREE_TYPE (totype)) == POINTER_TYPE
	      && TREE_CODE (TREE_TYPE (TREE_TYPE (totype))) == FUNCTION_TYPE))
	for (; fns; fns = OVL_NEXT (fns))
	  {
	    tree fn = OVL_CURRENT (fns);
	    if (TREE_CODE (fn) == TEMPLATE_DECL)
	      add_template_conv_candidate
		(&candidates, fn, obj, args, totype,
		 /*access_path=*/NULL_TREE,
		 /*conversion_path=*/NULL_TREE);
	    else
	      add_conv_candidate (&candidates, fn, obj, args,
				  /*conversion_path=*/NULL_TREE,
				  /*access_path=*/NULL_TREE);
	  }
    }

  candidates = splice_viable (candidates, pedantic, &any_viable_p);
  if (!any_viable_p)
    {
      error ("no match for call to %<(%T) (%A)%>", TREE_TYPE (obj), args);
      print_z_candidates (candidates);
      result = error_mark_node;
    }
  else
    {
      cand = tourney (candidates);
      if (cand == 0)
	{
	  error ("call of %<(%T) (%A)%> is ambiguous", TREE_TYPE (obj), args);
	  print_z_candidates (candidates);
	  result = error_mark_node;
	}
      /* Since cand->fn will be a type, not a function, for a conversion
	 function, we must be careful not to unconditionally look at
	 DECL_NAME here.  */
      else if (TREE_CODE (cand->fn) == FUNCTION_DECL
	       && DECL_OVERLOADED_OPERATOR_P (cand->fn) == CALL_EXPR)
	result = build_over_call (cand, LOOKUP_NORMAL);
      else
	{
	  obj = convert_like_with_context (cand->convs[0], obj, cand->fn, -1);
	  obj = convert_from_reference (obj);
	  result = build_function_call (obj, args);
	}
    }

  /* Free all the conversions we allocated.  */
  obstack_free (&conversion_obstack, p);

  return result;
}

static void
op_error (enum tree_code code, enum tree_code code2,
	  tree arg1, tree arg2, tree arg3, const char *problem)
{
  const char *opname;

  if (code == MODIFY_EXPR)
    opname = assignment_operator_name_info[code2].name;
  else
    opname = operator_name_info[code].name;

  switch (code)
    {
    case COND_EXPR:
      error ("%s for ternary %<operator?:%> in %<%E ? %E : %E%>",
	     problem, arg1, arg2, arg3);
      break;

    case POSTINCREMENT_EXPR:
    case POSTDECREMENT_EXPR:
      error ("%s for %<operator%s%> in %<%E%s%>", problem, opname, arg1, opname);
      break;

    case ARRAY_REF:
      error ("%s for %<operator[]%> in %<%E[%E]%>", problem, arg1, arg2);
      break;

    case REALPART_EXPR:
    case IMAGPART_EXPR:
      error ("%s for %qs in %<%s %E%>", problem, opname, opname, arg1);
      break;

    default:
      if (arg2)
	error ("%s for %<operator%s%> in %<%E %s %E%>",
	       problem, opname, arg1, opname, arg2);
      else
	error ("%s for %<operator%s%> in %<%s%E%>",
	       problem, opname, opname, arg1);
      break;
    }
}

/* Return the implicit conversion sequence that could be used to
   convert E1 to E2 in [expr.cond].  */

static conversion *
conditional_conversion (tree e1, tree e2)
{
  tree t1 = non_reference (TREE_TYPE (e1));
  tree t2 = non_reference (TREE_TYPE (e2));
  conversion *conv;
  bool good_base;

  /* [expr.cond]

     If E2 is an lvalue: E1 can be converted to match E2 if E1 can be
     implicitly converted (clause _conv_) to the type "reference to
     T2", subject to the constraint that in the conversion the
     reference must bind directly (_dcl.init.ref_) to E1.  */
  if (real_lvalue_p (e2))
    {
      conv = implicit_conversion (build_reference_type (t2),
				  t1,
				  e1,
				  /*c_cast_p=*/false,
				  LOOKUP_NO_TEMP_BIND);
      if (conv)
	return conv;
    }

  /* [expr.cond]

     If E1 and E2 have class type, and the underlying class types are
     the same or one is a base class of the other: E1 can be converted
     to match E2 if the class of T2 is the same type as, or a base
     class of, the class of T1, and the cv-qualification of T2 is the
     same cv-qualification as, or a greater cv-qualification than, the
     cv-qualification of T1.  If the conversion is applied, E1 is
     changed to an rvalue of type T2 that still refers to the original
     source class object (or the appropriate subobject thereof).  */
  if (CLASS_TYPE_P (t1) && CLASS_TYPE_P (t2)
      && ((good_base = DERIVED_FROM_P (t2, t1)) || DERIVED_FROM_P (t1, t2)))
    {
      if (good_base && at_least_as_qualified_p (t2, t1))
	{
	  conv = build_identity_conv (t1, e1);
	  if (!same_type_p (TYPE_MAIN_VARIANT (t1),
			    TYPE_MAIN_VARIANT (t2)))
	    conv = build_conv (ck_base, t2, conv);
	  else
	    conv = build_conv (ck_rvalue, t2, conv);
	  return conv;
	}
      else
	return NULL;
    }
  else
    /* [expr.cond]

       Otherwise: E1 can be converted to match E2 if E1 can be implicitly
       converted to the type that expression E2 would have if E2 were
       converted to an rvalue (or the type it has, if E2 is an rvalue).  */
    return implicit_conversion (t2, t1, e1, /*c_cast_p=*/false,
				LOOKUP_NORMAL);
}

/* Implement [expr.cond].  ARG1, ARG2, and ARG3 are the three
   arguments to the conditional expression.  */

tree
build_conditional_expr (tree arg1, tree arg2, tree arg3)
{
  tree arg2_type;
  tree arg3_type;
  tree result = NULL_TREE;
  tree result_type = NULL_TREE;
  bool lvalue_p = true;
  struct z_candidate *candidates = 0;
  struct z_candidate *cand;
  void *p;

  /* As a G++ extension, the second argument to the conditional can be
     omitted.  (So that `a ? : c' is roughly equivalent to `a ? a :
     c'.)  If the second operand is omitted, make sure it is
     calculated only once.  */
  if (!arg2)
    {
      if (pedantic)
	pedwarn ("ISO C++ forbids omitting the middle term of a ?: expression");

      /* Make sure that lvalues remain lvalues.  See g++.oliva/ext1.C.  */
      if (real_lvalue_p (arg1))
	arg2 = arg1 = stabilize_reference (arg1);
      else
	arg2 = arg1 = save_expr (arg1);
    }

  /* [expr.cond]

     The first expr ession is implicitly converted to bool (clause
     _conv_).  */
  arg1 = perform_implicit_conversion (boolean_type_node, arg1);

  /* If something has already gone wrong, just pass that fact up the
     tree.  */
  if (error_operand_p (arg1)
      || error_operand_p (arg2)
      || error_operand_p (arg3))
    return error_mark_node;

  /* [expr.cond]

     If either the second or the third operand has type (possibly
     cv-qualified) void, then the lvalue-to-rvalue (_conv.lval_),
     array-to-pointer (_conv.array_), and function-to-pointer
     (_conv.func_) standard conversions are performed on the second
     and third operands.  */
  arg2_type = unlowered_expr_type (arg2);
  arg3_type = unlowered_expr_type (arg3);
  if (VOID_TYPE_P (arg2_type) || VOID_TYPE_P (arg3_type))
    {
      /* Do the conversions.  We don't these for `void' type arguments
	 since it can't have any effect and since decay_conversion
	 does not handle that case gracefully.  */
      if (!VOID_TYPE_P (arg2_type))
	arg2 = decay_conversion (arg2);
      if (!VOID_TYPE_P (arg3_type))
	arg3 = decay_conversion (arg3);
      arg2_type = TREE_TYPE (arg2);
      arg3_type = TREE_TYPE (arg3);

      /* [expr.cond]

	 One of the following shall hold:

	 --The second or the third operand (but not both) is a
	   throw-expression (_except.throw_); the result is of the
	   type of the other and is an rvalue.

	 --Both the second and the third operands have type void; the
	   result is of type void and is an rvalue.

	 We must avoid calling force_rvalue for expressions of type
	 "void" because it will complain that their value is being
	 used.  */
      if (TREE_CODE (arg2) == THROW_EXPR
	  && TREE_CODE (arg3) != THROW_EXPR)
	{
	  if (!VOID_TYPE_P (arg3_type))
	    arg3 = force_rvalue (arg3);
	  arg3_type = TREE_TYPE (arg3);
	  result_type = arg3_type;
	}
      else if (TREE_CODE (arg2) != THROW_EXPR
	       && TREE_CODE (arg3) == THROW_EXPR)
	{
	  if (!VOID_TYPE_P (arg2_type))
	    arg2 = force_rvalue (arg2);
	  arg2_type = TREE_TYPE (arg2);
	  result_type = arg2_type;
	}
      else if (VOID_TYPE_P (arg2_type) && VOID_TYPE_P (arg3_type))
	result_type = void_type_node;
      else
	{
	  error ("%qE has type %<void%> and is not a throw-expression",
		    VOID_TYPE_P (arg2_type) ? arg2 : arg3);
	  return error_mark_node;
	}

      lvalue_p = false;
      goto valid_operands;
    }
  /* [expr.cond]

     Otherwise, if the second and third operand have different types,
     and either has (possibly cv-qualified) class type, an attempt is
     made to convert each of those operands to the type of the other.  */
  else if (!same_type_p (arg2_type, arg3_type)
	   && (CLASS_TYPE_P (arg2_type) || CLASS_TYPE_P (arg3_type)))
    {
      conversion *conv2;
      conversion *conv3;

      /* Get the high-water mark for the CONVERSION_OBSTACK.  */
      p = conversion_obstack_alloc (0);

      conv2 = conditional_conversion (arg2, arg3);
      conv3 = conditional_conversion (arg3, arg2);

      /* [expr.cond]

	 If both can be converted, or one can be converted but the
	 conversion is ambiguous, the program is ill-formed.  If
	 neither can be converted, the operands are left unchanged and
	 further checking is performed as described below.  If exactly
	 one conversion is possible, that conversion is applied to the
	 chosen operand and the converted operand is used in place of
	 the original operand for the remainder of this section.  */
      if ((conv2 && !conv2->bad_p
	   && conv3 && !conv3->bad_p)
	  || (conv2 && conv2->kind == ck_ambig)
	  || (conv3 && conv3->kind == ck_ambig))
	{
	  error ("operands to ?: have different types %qT and %qT",
		 arg2_type, arg3_type);
	  result = error_mark_node;
	}
      else if (conv2 && (!conv2->bad_p || !conv3))
	{
	  arg2 = convert_like (conv2, arg2);
	  arg2 = convert_from_reference (arg2);
	  arg2_type = TREE_TYPE (arg2);
	  /* Even if CONV2 is a valid conversion, the result of the
	     conversion may be invalid.  For example, if ARG3 has type
	     "volatile X", and X does not have a copy constructor
	     accepting a "volatile X&", then even if ARG2 can be
	     converted to X, the conversion will fail.  */
	  if (error_operand_p (arg2))
	    result = error_mark_node;
	}
      else if (conv3 && (!conv3->bad_p || !conv2))
	{
	  arg3 = convert_like (conv3, arg3);
	  arg3 = convert_from_reference (arg3);
	  arg3_type = TREE_TYPE (arg3);
	  if (error_operand_p (arg3))
	    result = error_mark_node;
	}

      /* Free all the conversions we allocated.  */
      obstack_free (&conversion_obstack, p);

      if (result)
	return result;

      /* If, after the conversion, both operands have class type,
	 treat the cv-qualification of both operands as if it were the
	 union of the cv-qualification of the operands.

	 The standard is not clear about what to do in this
	 circumstance.  For example, if the first operand has type
	 "const X" and the second operand has a user-defined
	 conversion to "volatile X", what is the type of the second
	 operand after this step?  Making it be "const X" (matching
	 the first operand) seems wrong, as that discards the
	 qualification without actually performing a copy.  Leaving it
	 as "volatile X" seems wrong as that will result in the
	 conditional expression failing altogether, even though,
	 according to this step, the one operand could be converted to
	 the type of the other.  */
      if ((conv2 || conv3)
	  && CLASS_TYPE_P (arg2_type)
	  && TYPE_QUALS (arg2_type) != TYPE_QUALS (arg3_type))
	arg2_type = arg3_type =
	  cp_build_qualified_type (arg2_type,
				   TYPE_QUALS (arg2_type)
				   | TYPE_QUALS (arg3_type));
    }

  /* [expr.cond]

     If the second and third operands are lvalues and have the same
     type, the result is of that type and is an lvalue.  */
  if (real_lvalue_p (arg2)
      && real_lvalue_p (arg3)
      && same_type_p (arg2_type, arg3_type))
    {
      result_type = arg2_type;
      goto valid_operands;
    }

  /* [expr.cond]

     Otherwise, the result is an rvalue.  If the second and third
     operand do not have the same type, and either has (possibly
     cv-qualified) class type, overload resolution is used to
     determine the conversions (if any) to be applied to the operands
     (_over.match.oper_, _over.built_).  */
  lvalue_p = false;
  if (!same_type_p (arg2_type, arg3_type)
      && (CLASS_TYPE_P (arg2_type) || CLASS_TYPE_P (arg3_type)))
    {
      tree args[3];
      conversion *conv;
      bool any_viable_p;

      /* Rearrange the arguments so that add_builtin_candidate only has
	 to know about two args.  In build_builtin_candidates, the
	 arguments are unscrambled.  */
      args[0] = arg2;
      args[1] = arg3;
      args[2] = arg1;
      add_builtin_candidates (&candidates,
			      COND_EXPR,
			      NOP_EXPR,
			      ansi_opname (COND_EXPR),
			      args,
			      LOOKUP_NORMAL);

      /* [expr.cond]

	 If the overload resolution fails, the program is
	 ill-formed.  */
      candidates = splice_viable (candidates, pedantic, &any_viable_p);
      if (!any_viable_p)
	{
	  op_error (COND_EXPR, NOP_EXPR, arg1, arg2, arg3, "no match");
	  print_z_candidates (candidates);
	  return error_mark_node;
	}
      cand = tourney (candidates);
      if (!cand)
	{
	  op_error (COND_EXPR, NOP_EXPR, arg1, arg2, arg3, "no match");
	  print_z_candidates (candidates);
	  return error_mark_node;
	}

      /* [expr.cond]

	 Otherwise, the conversions thus determined are applied, and
	 the converted operands are used in place of the original
	 operands for the remainder of this section.  */
      conv = cand->convs[0];
      arg1 = convert_like (conv, arg1);
      conv = cand->convs[1];
      arg2 = convert_like (conv, arg2);
      conv = cand->convs[2];
      arg3 = convert_like (conv, arg3);
    }

  /* [expr.cond]

     Lvalue-to-rvalue (_conv.lval_), array-to-pointer (_conv.array_),
     and function-to-pointer (_conv.func_) standard conversions are
     performed on the second and third operands.

     We need to force the lvalue-to-rvalue conversion here for class types,
     so we get TARGET_EXPRs; trying to deal with a COND_EXPR of class rvalues
     that isn't wrapped with a TARGET_EXPR plays havoc with exception
     regions.  */

  arg2 = force_rvalue (arg2);
  if (!CLASS_TYPE_P (arg2_type))
    arg2_type = TREE_TYPE (arg2);

  arg3 = force_rvalue (arg3);
  if (!CLASS_TYPE_P (arg2_type))
    arg3_type = TREE_TYPE (arg3);

  if (arg2 == error_mark_node || arg3 == error_mark_node)
    return error_mark_node;

  /* [expr.cond]

     After those conversions, one of the following shall hold:

     --The second and third operands have the same type; the result  is  of
       that type.  */
  if (same_type_p (arg2_type, arg3_type))
    result_type = arg2_type;
  /* [expr.cond]

     --The second and third operands have arithmetic or enumeration
       type; the usual arithmetic conversions are performed to bring
       them to a common type, and the result is of that type.  */
  else if ((ARITHMETIC_TYPE_P (arg2_type)
	    || TREE_CODE (arg2_type) == ENUMERAL_TYPE)
	   && (ARITHMETIC_TYPE_P (arg3_type)
	       || TREE_CODE (arg3_type) == ENUMERAL_TYPE))
    {
      /* In this case, there is always a common type.  */
      result_type = type_after_usual_arithmetic_conversions (arg2_type,
							     arg3_type);

      if (TREE_CODE (arg2_type) == ENUMERAL_TYPE
	  && TREE_CODE (arg3_type) == ENUMERAL_TYPE)
	 warning (0, "enumeral mismatch in conditional expression: %qT vs %qT",
		   arg2_type, arg3_type);
      else if (extra_warnings
	       && ((TREE_CODE (arg2_type) == ENUMERAL_TYPE
		    && !same_type_p (arg3_type, type_promotes_to (arg2_type)))
		   || (TREE_CODE (arg3_type) == ENUMERAL_TYPE
		       && !same_type_p (arg2_type, type_promotes_to (arg3_type)))))
	warning (0, "enumeral and non-enumeral type in conditional expression");

      arg2 = perform_implicit_conversion (result_type, arg2);
      arg3 = perform_implicit_conversion (result_type, arg3);
    }
  /* [expr.cond]

     --The second and third operands have pointer type, or one has
       pointer type and the other is a null pointer constant; pointer
       conversions (_conv.ptr_) and qualification conversions
       (_conv.qual_) are performed to bring them to their composite
       pointer type (_expr.rel_).  The result is of the composite
       pointer type.

     --The second and third operands have pointer to member type, or
       one has pointer to member type and the other is a null pointer
       constant; pointer to member conversions (_conv.mem_) and
       qualification conversions (_conv.qual_) are performed to bring
       them to a common type, whose cv-qualification shall match the
       cv-qualification of either the second or the third operand.
       The result is of the common type.  */
  else if ((null_ptr_cst_p (arg2)
	    && (TYPE_PTR_P (arg3_type) || TYPE_PTR_TO_MEMBER_P (arg3_type)))
	   || (null_ptr_cst_p (arg3)
	       && (TYPE_PTR_P (arg2_type) || TYPE_PTR_TO_MEMBER_P (arg2_type)))
	   || (TYPE_PTR_P (arg2_type) && TYPE_PTR_P (arg3_type))
	   || (TYPE_PTRMEM_P (arg2_type) && TYPE_PTRMEM_P (arg3_type))
	   || (TYPE_PTRMEMFUNC_P (arg2_type) && TYPE_PTRMEMFUNC_P (arg3_type)))
    {
      result_type = composite_pointer_type (arg2_type, arg3_type, arg2,
					    arg3, "conditional expression");
      if (result_type == error_mark_node)
	return error_mark_node;
      arg2 = perform_implicit_conversion (result_type, arg2);
      arg3 = perform_implicit_conversion (result_type, arg3);
    }

  if (!result_type)
    {
      error ("operands to ?: have different types %qT and %qT",
	     arg2_type, arg3_type);
      return error_mark_node;
    }

 valid_operands:
  result = fold_if_not_in_template (build3 (COND_EXPR, result_type, arg1,
					    arg2, arg3));
  /* We can't use result_type below, as fold might have returned a
     throw_expr.  */

  if (!lvalue_p)
    {
      /* Expand both sides into the same slot, hopefully the target of
	 the ?: expression.  We used to check for TARGET_EXPRs here,
	 but now we sometimes wrap them in NOP_EXPRs so the test would
	 fail.  */
      if (CLASS_TYPE_P (TREE_TYPE (result)))
	result = get_target_expr (result);
      /* If this expression is an rvalue, but might be mistaken for an
	 lvalue, we must add a NON_LVALUE_EXPR.  */
      result = rvalue (result);
    }

  return result;
}

/* OPERAND is an operand to an expression.  Perform necessary steps
   required before using it.  If OPERAND is NULL_TREE, NULL_TREE is
   returned.  */

static tree
prep_operand (tree operand)
{
  if (operand)
    {
      if (CLASS_TYPE_P (TREE_TYPE (operand))
	  && CLASSTYPE_TEMPLATE_INSTANTIATION (TREE_TYPE (operand)))
	/* Make sure the template type is instantiated now.  */
	instantiate_class_template (TYPE_MAIN_VARIANT (TREE_TYPE (operand)));
    }

  return operand;
}

/* Add each of the viable functions in FNS (a FUNCTION_DECL or
   OVERLOAD) to the CANDIDATES, returning an updated list of
   CANDIDATES.  The ARGS are the arguments provided to the call,
   without any implicit object parameter.  The EXPLICIT_TARGS are
   explicit template arguments provided.  TEMPLATE_ONLY is true if
   only template functions should be considered.  CONVERSION_PATH,
   ACCESS_PATH, and FLAGS are as for add_function_candidate.  */

static void
add_candidates (tree fns, tree args,
		tree explicit_targs, bool template_only,
		tree conversion_path, tree access_path,
		int flags,
		struct z_candidate **candidates)
{
  tree ctype;
  tree non_static_args;

  ctype = conversion_path ? BINFO_TYPE (conversion_path) : NULL_TREE;
  /* Delay creating the implicit this parameter until it is needed.  */
  non_static_args = NULL_TREE;

  while (fns)
    {
      tree fn;
      tree fn_args;

      fn = OVL_CURRENT (fns);
      /* Figure out which set of arguments to use.  */
      if (DECL_NONSTATIC_MEMBER_FUNCTION_P (fn))
	{
	  /* If this function is a non-static member, prepend the implicit
	     object parameter.  */
	  if (!non_static_args)
	    non_static_args = tree_cons (NULL_TREE,
					 build_this (TREE_VALUE (args)),
					 TREE_CHAIN (args));
	  fn_args = non_static_args;
	}
      else
	/* Otherwise, just use the list of arguments provided.  */
	fn_args = args;

      if (TREE_CODE (fn) == TEMPLATE_DECL)
	add_template_candidate (candidates,
				fn,
				ctype,
				explicit_targs,
				fn_args,
				NULL_TREE,
				access_path,
				conversion_path,
				flags,
				DEDUCE_CALL);
      else if (!template_only)
	add_function_candidate (candidates,
				fn,
				ctype,
				fn_args,
				access_path,
				conversion_path,
				flags);
      fns = OVL_NEXT (fns);
    }
}

tree
build_new_op (enum tree_code code, int flags, tree arg1, tree arg2, tree arg3,
	      bool *overloaded_p)
{
  struct z_candidate *candidates = 0, *cand;
  tree arglist, fnname;
  tree args[3];
  tree result = NULL_TREE;
  bool result_valid_p = false;
  enum tree_code code2 = NOP_EXPR;
  conversion *conv;
  void *p;
  bool strict_p;
  bool any_viable_p;

  if (error_operand_p (arg1)
      || error_operand_p (arg2)
      || error_operand_p (arg3))
    return error_mark_node;

  if (code == MODIFY_EXPR)
    {
      code2 = TREE_CODE (arg3);
      arg3 = NULL_TREE;
      fnname = ansi_assopname (code2);
    }
  else
    fnname = ansi_opname (code);

  arg1 = prep_operand (arg1);

  switch (code)
    {
    case NEW_EXPR:
    case VEC_NEW_EXPR:
    case VEC_DELETE_EXPR:
    case DELETE_EXPR:
      /* Use build_op_new_call and build_op_delete_call instead.  */
      gcc_unreachable ();

    case CALL_EXPR:
      return build_object_call (arg1, arg2);

    default:
      break;
    }

  arg2 = prep_operand (arg2);
  arg3 = prep_operand (arg3);

  if (code == COND_EXPR)
    {
      if (arg2 == NULL_TREE
	  || TREE_CODE (TREE_TYPE (arg2)) == VOID_TYPE
	  || TREE_CODE (TREE_TYPE (arg3)) == VOID_TYPE
	  || (! IS_OVERLOAD_TYPE (TREE_TYPE (arg2))
	      && ! IS_OVERLOAD_TYPE (TREE_TYPE (arg3))))
	goto builtin;
    }
  else if (! IS_OVERLOAD_TYPE (TREE_TYPE (arg1))
	   && (! arg2 || ! IS_OVERLOAD_TYPE (TREE_TYPE (arg2))))
    goto builtin;

  if (code == POSTINCREMENT_EXPR || code == POSTDECREMENT_EXPR)
    arg2 = integer_zero_node;

  arglist = NULL_TREE;
  if (arg3)
    arglist = tree_cons (NULL_TREE, arg3, arglist);
  if (arg2)
    arglist = tree_cons (NULL_TREE, arg2, arglist);
  arglist = tree_cons (NULL_TREE, arg1, arglist);

  /* Get the high-water mark for the CONVERSION_OBSTACK.  */
  p = conversion_obstack_alloc (0);

  /* Add namespace-scope operators to the list of functions to
     consider.  */
  add_candidates (lookup_function_nonclass (fnname, arglist, /*block_p=*/true),
		  arglist, NULL_TREE, false, NULL_TREE, NULL_TREE,
		  flags, &candidates);
  /* Add class-member operators to the candidate set.  */
  if (CLASS_TYPE_P (TREE_TYPE (arg1)))
    {
      tree fns;

      fns = lookup_fnfields (TREE_TYPE (arg1), fnname, 1);
      if (fns == error_mark_node)
	{
	  result = error_mark_node;
	  goto user_defined_result_ready;
	}
      if (fns)
	add_candidates (BASELINK_FUNCTIONS (fns), arglist,
			NULL_TREE, false,
			BASELINK_BINFO (fns),
			TYPE_BINFO (TREE_TYPE (arg1)),
			flags, &candidates);
    }

  /* Rearrange the arguments for ?: so that add_builtin_candidate only has
     to know about two args; a builtin candidate will always have a first
     parameter of type bool.  We'll handle that in
     build_builtin_candidate.  */
  if (code == COND_EXPR)
    {
      args[0] = arg2;
      args[1] = arg3;
      args[2] = arg1;
    }
  else
    {
      args[0] = arg1;
      args[1] = arg2;
      args[2] = NULL_TREE;
    }

  add_builtin_candidates (&candidates, code, code2, fnname, args, flags);

  switch (code)
    {
    case COMPOUND_EXPR:
    case ADDR_EXPR:
      /* For these, the built-in candidates set is empty
	 [over.match.oper]/3.  We don't want non-strict matches
	 because exact matches are always possible with built-in
	 operators.  The built-in candidate set for COMPONENT_REF
	 would be empty too, but since there are no such built-in
	 operators, we accept non-strict matches for them.  */
      strict_p = true;
      break;

    default:
      strict_p = pedantic;
      break;
    }

  candidates = splice_viable (candidates, strict_p, &any_viable_p);
  if (!any_viable_p)
    {
      switch (code)
	{
	case POSTINCREMENT_EXPR:
	case POSTDECREMENT_EXPR:
	  /* Look for an `operator++ (int)'.  If they didn't have
	     one, then we fall back to the old way of doing things.  */
	  if (flags & LOOKUP_COMPLAIN)
	    pedwarn ("no %<%D(int)%> declared for postfix %qs, "
		     "trying prefix operator instead",
		     fnname,
		     operator_name_info[code].name);
	  if (code == POSTINCREMENT_EXPR)
	    code = PREINCREMENT_EXPR;
	  else
	    code = PREDECREMENT_EXPR;
	  result = build_new_op (code, flags, arg1, NULL_TREE, NULL_TREE,
				 overloaded_p);
	  break;

	  /* The caller will deal with these.  */
	case ADDR_EXPR:
	case COMPOUND_EXPR:
	case COMPONENT_REF:
	  result = NULL_TREE;
	  result_valid_p = true;
	  break;

	default:
	  if (flags & LOOKUP_COMPLAIN)
	    {
	      op_error (code, code2, arg1, arg2, arg3, "no match");
	      print_z_candidates (candidates);
	    }
	  result = error_mark_node;
	  break;
	}
    }
  else
    {
      cand = tourney (candidates);
      if (cand == 0)
	{
	  if (flags & LOOKUP_COMPLAIN)
	    {
	      op_error (code, code2, arg1, arg2, arg3, "ambiguous overload");
	      print_z_candidates (candidates);
	    }
	  result = error_mark_node;
	}
      else if (TREE_CODE (cand->fn) == FUNCTION_DECL)
	{
	  if (overloaded_p)
	    *overloaded_p = true;

	  result = build_over_call (cand, LOOKUP_NORMAL);
	}
      else
	{
	  /* Give any warnings we noticed during overload resolution.  */
	  if (cand->warnings)
	    {
	      struct candidate_warning *w;
	      for (w = cand->warnings; w; w = w->next)
		joust (cand, w->loser, 1);
	    }

	  /* Check for comparison of different enum types.  */
	  switch (code)
	    {
	    case GT_EXPR:
	    case LT_EXPR:
	    case GE_EXPR:
	    case LE_EXPR:
	    case EQ_EXPR:
	    case NE_EXPR:
	      if (TREE_CODE (TREE_TYPE (arg1)) == ENUMERAL_TYPE
		  && TREE_CODE (TREE_TYPE (arg2)) == ENUMERAL_TYPE
		  && (TYPE_MAIN_VARIANT (TREE_TYPE (arg1))
		      != TYPE_MAIN_VARIANT (TREE_TYPE (arg2))))
		{
		  warning (0, "comparison between %q#T and %q#T",
			   TREE_TYPE (arg1), TREE_TYPE (arg2));
		}
	      break;
	    default:
	      break;
	    }

	  /* We need to strip any leading REF_BIND so that bitfields
	     don't cause errors.  This should not remove any important
	     conversions, because builtins don't apply to class
	     objects directly.  */
	  conv = cand->convs[0];
	  if (conv->kind == ck_ref_bind)
	    conv = conv->u.next;
	  arg1 = convert_like (conv, arg1);
	  if (arg2)
	    {
	      conv = cand->convs[1];
	      if (conv->kind == ck_ref_bind)
		conv = conv->u.next;
	      arg2 = convert_like (conv, arg2);
	    }
	  if (arg3)
	    {
	      conv = cand->convs[2];
	      if (conv->kind == ck_ref_bind)
		conv = conv->u.next;
	      arg3 = convert_like (conv, arg3);
	    }
	}
    }

 user_defined_result_ready:

  /* Free all the conversions we allocated.  */
  obstack_free (&conversion_obstack, p);

  if (result || result_valid_p)
    return result;

 builtin:
  switch (code)
    {
    case MODIFY_EXPR:
      return build_modify_expr (arg1, code2, arg2);

    case INDIRECT_REF:
      return build_indirect_ref (arg1, "unary *");

    case PLUS_EXPR:
    case MINUS_EXPR:
    case MULT_EXPR:
    case TRUNC_DIV_EXPR:
    case GT_EXPR:
    case LT_EXPR:
    case GE_EXPR:
    case LE_EXPR:
    case EQ_EXPR:
    case NE_EXPR:
    case MAX_EXPR:
    case MIN_EXPR:
    case LSHIFT_EXPR:
    case RSHIFT_EXPR:
    case TRUNC_MOD_EXPR:
    case BIT_AND_EXPR:
    case BIT_IOR_EXPR:
    case BIT_XOR_EXPR:
    case TRUTH_ANDIF_EXPR:
    case TRUTH_ORIF_EXPR:
      return cp_build_binary_op (code, arg1, arg2);

    case UNARY_PLUS_EXPR:
    case NEGATE_EXPR:
    case BIT_NOT_EXPR:
    case TRUTH_NOT_EXPR:
    case PREINCREMENT_EXPR:
    case POSTINCREMENT_EXPR:
    case PREDECREMENT_EXPR:
    case POSTDECREMENT_EXPR:
    case REALPART_EXPR:
    case IMAGPART_EXPR:
      return build_unary_op (code, arg1, candidates != 0);

    case ARRAY_REF:
      return build_array_ref (arg1, arg2);

    case COND_EXPR:
      return build_conditional_expr (arg1, arg2, arg3);

    case MEMBER_REF:
      return build_m_component_ref (build_indirect_ref (arg1, NULL), arg2);

      /* The caller will deal with these.  */
    case ADDR_EXPR:
    case COMPONENT_REF:
    case COMPOUND_EXPR:
      return NULL_TREE;

    default:
      gcc_unreachable ();
    }
  return NULL_TREE;
}

/* Build a call to operator delete.  This has to be handled very specially,
   because the restrictions on what signatures match are different from all
   other call instances.  For a normal delete, only a delete taking (void *)
   or (void *, size_t) is accepted.  For a placement delete, only an exact
   match with the placement new is accepted.

   CODE is either DELETE_EXPR or VEC_DELETE_EXPR.
   ADDR is the pointer to be deleted.
   SIZE is the size of the memory block to be deleted.
   GLOBAL_P is true if the delete-expression should not consider
   class-specific delete operators.
   PLACEMENT is the corresponding placement new call, or NULL_TREE.

   If this call to "operator delete" is being generated as part to
   deallocate memory allocated via a new-expression (as per [expr.new]
   which requires that if the initialization throws an exception then
   we call a deallocation function), then ALLOC_FN is the allocation
   function.  */

tree
build_op_delete_call (enum tree_code code, tree addr, tree size,
		      bool global_p, tree placement,
		      tree alloc_fn)
{
  tree fn = NULL_TREE;
  tree fns, fnname, argtypes, args, type;
  int pass;

  if (addr == error_mark_node)
    return error_mark_node;

  type = strip_array_types (TREE_TYPE (TREE_TYPE (addr)));

  fnname = ansi_opname (code);

  if (CLASS_TYPE_P (type)
      && COMPLETE_TYPE_P (complete_type (type))
      && !global_p)
    /* In [class.free]

       If the result of the lookup is ambiguous or inaccessible, or if
       the lookup selects a placement deallocation function, the
       program is ill-formed.

       Therefore, we ask lookup_fnfields to complain about ambiguity.  */
    {
      fns = lookup_fnfields (TYPE_BINFO (type), fnname, 1);
      if (fns == error_mark_node)
	return error_mark_node;
    }
  else
    fns = NULL_TREE;

  if (fns == NULL_TREE)
    fns = lookup_name_nonclass (fnname);

  if (placement)
    {
      /* Get the parameter types for the allocation function that is
	 being called.  */
      gcc_assert (alloc_fn != NULL_TREE);
      argtypes = TREE_CHAIN (TYPE_ARG_TYPES (TREE_TYPE (alloc_fn)));
      /* Also the second argument.  */
      args = TREE_CHAIN (TREE_OPERAND (placement, 1));
    }
  else
    {
      /* First try it without the size argument.  */
      argtypes = void_list_node;
      args = NULL_TREE;
    }

  /* Strip const and volatile from addr.  */
  addr = cp_convert (ptr_type_node, addr);

  /* We make two tries at finding a matching `operator delete'.  On
     the first pass, we look for a one-operator (or placement)
     operator delete.  If we're not doing placement delete, then on
     the second pass we look for a two-argument delete.  */
  for (pass = 0; pass < (placement ? 1 : 2); ++pass)
    {
      /* Go through the `operator delete' functions looking for one
	 with a matching type.  */
      for (fn = BASELINK_P (fns) ? BASELINK_FUNCTIONS (fns) : fns;
	   fn;
	   fn = OVL_NEXT (fn))
	{
	  tree t;

	  /* The first argument must be "void *".  */
	  t = TYPE_ARG_TYPES (TREE_TYPE (OVL_CURRENT (fn)));
	  if (!same_type_p (TREE_VALUE (t), ptr_type_node))
	    continue;
	  t = TREE_CHAIN (t);
	  /* On the first pass, check the rest of the arguments.  */
	  if (pass == 0)
	    {
	      tree a = argtypes;
	      while (a && t)
		{
		  if (!same_type_p (TREE_VALUE (a), TREE_VALUE (t)))
		    break;
		  a = TREE_CHAIN (a);
		  t = TREE_CHAIN (t);
		}
	      if (!a && !t)
		break;
	    }
	  /* On the second pass, look for a function with exactly two
	     arguments: "void *" and "size_t".  */
	  else if (pass == 1
		   /* For "operator delete(void *, ...)" there will be
		      no second argument, but we will not get an exact
		      match above.  */
		   && t
		   && same_type_p (TREE_VALUE (t), sizetype)
		   && TREE_CHAIN (t) == void_list_node)
	    break;
	}

      /* If we found a match, we're done.  */
      if (fn)
	break;
    }

  /* If we have a matching function, call it.  */
  if (fn)
    {
      /* Make sure we have the actual function, and not an
	 OVERLOAD.  */
      fn = OVL_CURRENT (fn);

      /* If the FN is a member function, make sure that it is
	 accessible.  */
      if (DECL_CLASS_SCOPE_P (fn))
	perform_or_defer_access_check (TYPE_BINFO (type), fn, fn);

      if (pass == 0)
	args = tree_cons (NULL_TREE, addr, args);
      else
	args = tree_cons (NULL_TREE, addr,
			  build_tree_list (NULL_TREE, size));

      if (placement)
	{
	  /* The placement args might not be suitable for overload
	     resolution at this point, so build the call directly.  */
	  mark_used (fn);
	  return build_cxx_call (fn, args);
	}
      else
	return build_function_call (fn, args);
    }

  /* [expr.new]

     If no unambiguous matching deallocation function can be found,
     propagating the exception does not cause the object's memory to
     be freed.  */
  if (alloc_fn)
    {
      if (!placement)
	warning (0, "no corresponding deallocation function for `%D'", 
		 alloc_fn);
      return NULL_TREE;
    }

  error ("no suitable %<operator %s%> for %qT",
	 operator_name_info[(int)code].name, type);
  return error_mark_node;
}

/* If the current scope isn't allowed to access DECL along
   BASETYPE_PATH, give an error.  The most derived class in
   BASETYPE_PATH is the one used to qualify DECL. DIAG_DECL is
   the declaration to use in the error diagnostic.  */

bool
enforce_access (tree basetype_path, tree decl, tree diag_decl)
{
  gcc_assert (TREE_CODE (basetype_path) == TREE_BINFO);

  if (!accessible_p (basetype_path, decl, true))
    {
      if (TREE_PRIVATE (decl))
	error ("%q+#D is private", diag_decl);
      else if (TREE_PROTECTED (decl))
	error ("%q+#D is protected", diag_decl);
      else
	error ("%q+#D is inaccessible", diag_decl);
      error ("within this context");
      return false;
    }

  return true;
}

/* Check that a callable constructor to initialize a temporary of
   TYPE from an EXPR exists.  */

static void
check_constructor_callable (tree type, tree expr)
{
  build_special_member_call (NULL_TREE,
			     complete_ctor_identifier,
			     build_tree_list (NULL_TREE, expr),
			     type,
			     LOOKUP_NORMAL | LOOKUP_ONLYCONVERTING
			     | LOOKUP_NO_CONVERSION
			     | LOOKUP_CONSTRUCTOR_CALLABLE);
}

/* Initialize a temporary of type TYPE with EXPR.  The FLAGS are a
   bitwise or of LOOKUP_* values.  If any errors are warnings are
   generated, set *DIAGNOSTIC_FN to "error" or "warning",
   respectively.  If no diagnostics are generated, set *DIAGNOSTIC_FN
   to NULL.  */

static tree
build_temp (tree expr, tree type, int flags,
	    diagnostic_fn_t *diagnostic_fn)
{
  int savew, savee;

  savew = warningcount, savee = errorcount;
  expr = build_special_member_call (NULL_TREE,
				    complete_ctor_identifier,
				    build_tree_list (NULL_TREE, expr),
				    type, flags);
  if (warningcount > savew)
    *diagnostic_fn = warning0;
  else if (errorcount > savee)
    *diagnostic_fn = error;
  else
    *diagnostic_fn = NULL;
  return expr;
}


/* Perform the conversions in CONVS on the expression EXPR.  FN and
   ARGNUM are used for diagnostics.  ARGNUM is zero based, -1
   indicates the `this' argument of a method.  INNER is nonzero when
   being called to continue a conversion chain. It is negative when a
   reference binding will be applied, positive otherwise.  If
   ISSUE_CONVERSION_WARNINGS is true, warnings about suspicious
   conversions will be emitted if appropriate.  If C_CAST_P is true,
   this conversion is coming from a C-style cast; in that case,
   conversions to inaccessible bases are permitted.  */

static tree
convert_like_real (conversion *convs, tree expr, tree fn, int argnum,
		   int inner, bool issue_conversion_warnings,
		   bool c_cast_p)
{
  tree totype = convs->type;
  diagnostic_fn_t diagnostic_fn;

  if (convs->bad_p
      && convs->kind != ck_user
      && convs->kind != ck_ambig
      && convs->kind != ck_ref_bind)
    {
      conversion *t = convs;
      for (; t; t = convs->u.next)
	{
	  if (t->kind == ck_user || !t->bad_p)
	    {
	      expr = convert_like_real (t, expr, fn, argnum, 1,
					/*issue_conversion_warnings=*/false,
					/*c_cast_p=*/false);
	      break;
	    }
	  else if (t->kind == ck_ambig)
	    return convert_like_real (t, expr, fn, argnum, 1,
				      /*issue_conversion_warnings=*/false,
				      /*c_cast_p=*/false);
	  else if (t->kind == ck_identity)
	    break;
	}
      pedwarn ("invalid conversion from %qT to %qT", TREE_TYPE (expr), totype);
      if (fn)
	pedwarn ("  initializing argument %P of %qD", argnum, fn);
      return cp_convert (totype, expr);
    }

  if (issue_conversion_warnings)
    {
      tree t = non_reference (totype);

      /* Issue warnings about peculiar, but valid, uses of NULL.  */
      if (ARITHMETIC_TYPE_P (t) && expr == null_node)
	{
	  if (fn)
	    warning (OPT_Wconversion, "passing NULL to non-pointer argument %P of %qD",
		     argnum, fn);
	  else
	    warning (OPT_Wconversion, "converting to non-pointer type %qT from NULL", t);
	}

      /* Warn about assigning a floating-point type to an integer type.  */
      if (TREE_CODE (TREE_TYPE (expr)) == REAL_TYPE
	  && TREE_CODE (t) == INTEGER_TYPE)
	{
	  if (fn)
	    warning (OPT_Wconversion, "passing %qT for argument %P to %qD",
		     TREE_TYPE (expr), argnum, fn);
	  else
	    warning (OPT_Wconversion, "converting to %qT from %qT", t, TREE_TYPE (expr));
	}
    }

  switch (convs->kind)
    {
    case ck_user:
      {
	struct z_candidate *cand = convs->cand;
	tree convfn = cand->fn;
	tree args;

	if (DECL_CONSTRUCTOR_P (convfn))
	  {
	    tree t = build_int_cst (build_pointer_type (DECL_CONTEXT (convfn)),
				    0);

	    args = build_tree_list (NULL_TREE, expr);
	    /* We should never try to call the abstract or base constructor
	       from here.  */
	    gcc_assert (!DECL_HAS_IN_CHARGE_PARM_P (convfn)
			&& !DECL_HAS_VTT_PARM_P (convfn));
	    args = tree_cons (NULL_TREE, t, args);
	  }
	else
	  args = build_this (expr);
	expr = build_over_call (cand, LOOKUP_NORMAL);

	/* If this is a constructor or a function returning an aggr type,
	   we need to build up a TARGET_EXPR.  */
	if (DECL_CONSTRUCTOR_P (convfn))
	  expr = build_cplus_new (totype, expr);

	/* The result of the call is then used to direct-initialize the object
	   that is the destination of the copy-initialization.  [dcl.init]

	   Note that this step is not reflected in the conversion sequence;
	   it affects the semantics when we actually perform the
	   conversion, but is not considered during overload resolution.

	   If the target is a class, that means call a ctor.  */
	if (IS_AGGR_TYPE (totype)
	    && (inner >= 0 || !lvalue_p (expr)))
	  {
	    expr = (build_temp
		    (expr, totype,
		     /* Core issue 84, now a DR, says that we don't
			allow UDCs for these args (which deliberately
			breaks copy-init of an auto_ptr<Base> from an
			auto_ptr<Derived>).  */
		     LOOKUP_NORMAL|LOOKUP_ONLYCONVERTING|LOOKUP_NO_CONVERSION,
		     &diagnostic_fn));

	    if (diagnostic_fn)
	      {
		if (fn)
		  diagnostic_fn
		    ("  initializing argument %P of %qD from result of %qD",
		     argnum, fn, convfn);
		else
		 diagnostic_fn
		   ("  initializing temporary from result of %qD",  convfn);
	      }
	    expr = build_cplus_new (totype, expr);
	  }
	return expr;
      }
    case ck_identity:
      if (type_unknown_p (expr))
	expr = instantiate_type (totype, expr, tf_warning_or_error);
      /* Convert a constant to its underlying value, unless we are
	 about to bind it to a reference, in which case we need to
	 leave it as an lvalue.  */
      if (inner >= 0)
	expr = decl_constant_value (expr);
      if (convs->check_copy_constructor_p)
	check_constructor_callable (totype, expr);
      return expr;
    case ck_ambig:
      /* Call build_user_type_conversion again for the error.  */
      return build_user_type_conversion
	(totype, convs->u.expr, LOOKUP_NORMAL);

    default:
      break;
    };

  expr = convert_like_real (convs->u.next, expr, fn, argnum,
			    convs->kind == ck_ref_bind ? -1 : 1,
			    /*issue_conversion_warnings=*/false,
			    c_cast_p);
  if (expr == error_mark_node)
    return error_mark_node;

  switch (convs->kind)
    {
    case ck_rvalue:
      expr = convert_bitfield_to_declared_type (expr);
      if (! IS_AGGR_TYPE (totype))
	return expr;
      /* Else fall through.  */
    case ck_base:
      if (convs->kind == ck_base && !convs->need_temporary_p)
	{
	  /* We are going to bind a reference directly to a base-class
	     subobject of EXPR.  */
	  if (convs->check_copy_constructor_p)
	    check_constructor_callable (TREE_TYPE (expr), expr);
	  /* Build an expression for `*((base*) &expr)'.  */
	  expr = build_unary_op (ADDR_EXPR, expr, 0);
	  expr = convert_to_base (expr, build_pointer_type (totype),
				  !c_cast_p, /*nonnull=*/true);
	  expr = build_indirect_ref (expr, "implicit conversion");
	  return expr;
	}

      /* Copy-initialization where the cv-unqualified version of the source
	 type is the same class as, or a derived class of, the class of the
	 destination [is treated as direct-initialization].  [dcl.init] */
      expr = build_temp (expr, totype, LOOKUP_NORMAL|LOOKUP_ONLYCONVERTING,
			 &diagnostic_fn);
      if (diagnostic_fn && fn)
	diagnostic_fn ("  initializing argument %P of %qD", argnum, fn);
      return build_cplus_new (totype, expr);

    case ck_ref_bind:
      {
	tree ref_type = totype;

	/* If necessary, create a temporary.  */
	if (convs->need_temporary_p || !lvalue_p (expr))
	  {
	    tree type = convs->u.next->type;
	    cp_lvalue_kind lvalue = real_lvalue_p (expr);

	    if (!CP_TYPE_CONST_NON_VOLATILE_P (TREE_TYPE (ref_type)))
	      {
		/* If the reference is volatile or non-const, we
		   cannot create a temporary.  */
		if (lvalue & clk_bitfield)
		  error ("cannot bind bitfield %qE to %qT",
			 expr, ref_type);
		else if (lvalue & clk_packed)
		  error ("cannot bind packed field %qE to %qT",
			 expr, ref_type);
		else
		  error ("cannot bind rvalue %qE to %qT", expr, ref_type);
		return error_mark_node;
	      }
	    /* If the source is a packed field, and we must use a copy
	       constructor, then building the target expr will require
	       binding the field to the reference parameter to the
	       copy constructor, and we'll end up with an infinite
	       loop.  If we can use a bitwise copy, then we'll be
	       OK.  */
	    if ((lvalue & clk_packed)
		&& CLASS_TYPE_P (type)
		&& !TYPE_HAS_TRIVIAL_INIT_REF (type))
	      {
		error ("cannot bind packed field %qE to %qT",
		       expr, ref_type);
		return error_mark_node;
	      }
	    expr = build_target_expr_with_type (expr, type);
	  }

	/* Take the address of the thing to which we will bind the
	   reference.  */
	expr = build_unary_op (ADDR_EXPR, expr, 1);
	if (expr == error_mark_node)
	  return error_mark_node;

	/* Convert it to a pointer to the type referred to by the
	   reference.  This will adjust the pointer if a derived to
	   base conversion is being performed.  */
	expr = cp_convert (build_pointer_type (TREE_TYPE (ref_type)),
			   expr);
	/* Convert the pointer to the desired reference type.  */
	return build_nop (ref_type, expr);
      }

    case ck_lvalue:
      return decay_conversion (expr);

    case ck_qual:
      /* Warn about deprecated conversion if appropriate.  */
      string_conv_p (totype, expr, 1);
      break;

    case ck_ptr:
      if (convs->base_p)
	expr = convert_to_base (expr, totype, !c_cast_p,
				/*nonnull=*/false);
      return build_nop (totype, expr);

    case ck_pmem:
      return convert_ptrmem (totype, expr, /*allow_inverse_p=*/false,
			     c_cast_p);

    default:
      break;
    }

  if (issue_conversion_warnings)
    expr = convert_and_check (totype, expr);
  else
    expr = convert (totype, expr);

  return expr;
}

/* Build a call to __builtin_trap.  */

static tree
call_builtin_trap (void)
{
  tree fn = implicit_built_in_decls[BUILT_IN_TRAP];

  gcc_assert (fn != NULL);
  fn = build_call (fn, NULL_TREE);
  return fn;
}

/* ARG is being passed to a varargs function.  Perform any conversions
   required.  Return the converted value.  */

tree
convert_arg_to_ellipsis (tree arg)
{
  /* [expr.call]

     The lvalue-to-rvalue, array-to-pointer, and function-to-pointer
     standard conversions are performed.  */
  arg = decay_conversion (arg);
  /* [expr.call]

     If the argument has integral or enumeration type that is subject
     to the integral promotions (_conv.prom_), or a floating point
     type that is subject to the floating point promotion
     (_conv.fpprom_), the value of the argument is converted to the
     promoted type before the call.  */
  if (TREE_CODE (TREE_TYPE (arg)) == REAL_TYPE
      && (TYPE_PRECISION (TREE_TYPE (arg))
	  < TYPE_PRECISION (double_type_node)))
    arg = convert_to_real (double_type_node, arg);
  else if (INTEGRAL_OR_ENUMERATION_TYPE_P (TREE_TYPE (arg)))
    arg = perform_integral_promotions (arg);

  arg = require_complete_type (arg);

  if (arg != error_mark_node
      && !pod_type_p (TREE_TYPE (arg)))
    {
      /* Undefined behavior [expr.call] 5.2.2/7.  We used to just warn
	 here and do a bitwise copy, but now cp_expr_size will abort if we
	 try to do that.
	 If the call appears in the context of a sizeof expression,
	 there is no need to emit a warning, since the expression won't be
	 evaluated. We keep the builtin_trap just as a safety check.  */
      if (!skip_evaluation)
	warning (0, "cannot pass objects of non-POD type %q#T through %<...%>; "
		 "call will abort at runtime", TREE_TYPE (arg));
      arg = call_builtin_trap ();
      arg = build2 (COMPOUND_EXPR, integer_type_node, arg,
		    integer_zero_node);
    }

  return arg;
}

/* va_arg (EXPR, TYPE) is a builtin. Make sure it is not abused.  */

tree
build_x_va_arg (tree expr, tree type)
{
  if (processing_template_decl)
    return build_min (VA_ARG_EXPR, type, expr);

  type = complete_type_or_else (type, NULL_TREE);

  if (expr == error_mark_node || !type)
    return error_mark_node;

  if (! pod_type_p (type))
    {
      /* Remove reference types so we don't ICE later on.  */
      tree type1 = non_reference (type);
      /* Undefined behavior [expr.call] 5.2.2/7.  */
      warning (0, "cannot receive objects of non-POD type %q#T through %<...%>; "
	       "call will abort at runtime", type);
      expr = convert (build_pointer_type (type1), null_node);
      expr = build2 (COMPOUND_EXPR, TREE_TYPE (expr),
		     call_builtin_trap (), expr);
      expr = build_indirect_ref (expr, NULL);
      return expr;
    }

  return build_va_arg (expr, type);
}

/* TYPE has been given to va_arg.  Apply the default conversions which
   would have happened when passed via ellipsis.  Return the promoted
   type, or the passed type if there is no change.  */

tree
cxx_type_promotes_to (tree type)
{
  tree promote;

  /* Perform the array-to-pointer and function-to-pointer
     conversions.  */
  type = type_decays_to (type);

  promote = type_promotes_to (type);
  if (same_type_p (type, promote))
    promote = type;

  return promote;
}

/* ARG is a default argument expression being passed to a parameter of
   the indicated TYPE, which is a parameter to FN.  Do any required
   conversions.  Return the converted value.  */

tree
convert_default_arg (tree type, tree arg, tree fn, int parmnum)
{
  /* If the ARG is an unparsed default argument expression, the
     conversion cannot be performed.  */
  if (TREE_CODE (arg) == DEFAULT_ARG)
    {
      error ("the default argument for parameter %d of %qD has "
	     "not yet been parsed",
	     parmnum, fn);
      return error_mark_node;
    }

  if (fn && DECL_TEMPLATE_INFO (fn))
    arg = tsubst_default_argument (fn, type, arg);

  arg = break_out_target_exprs (arg);

  if (TREE_CODE (arg) == CONSTRUCTOR)
    {
      arg = digest_init (type, arg);
      arg = convert_for_initialization (0, type, arg, LOOKUP_NORMAL,
					"default argument", fn, parmnum);
    }
  else
    {
      /* We must make a copy of ARG, in case subsequent processing
	 alters any part of it.  For example, during gimplification a
	 cast of the form (T) &X::f (where "f" is a member function)
	 will lead to replacing the PTRMEM_CST for &X::f with a
	 VAR_DECL.  We can avoid the copy for constants, since they
	 are never modified in place.  */
      if (!CONSTANT_CLASS_P (arg))
	arg = unshare_expr (arg);
      arg = convert_for_initialization (0, type, arg, LOOKUP_NORMAL,
					"default argument", fn, parmnum);
      arg = convert_for_arg_passing (type, arg);
    }

  return arg;
}

/* Returns the type which will really be used for passing an argument of
   type TYPE.  */

tree
type_passed_as (tree type)
{
  /* Pass classes with copy ctors by invisible reference.  */
  if (TREE_ADDRESSABLE (type))
    {
      type = build_reference_type (type);
      /* There are no other pointers to this temporary.  */
      type = build_qualified_type (type, TYPE_QUAL_RESTRICT);
    }
  else if (targetm.calls.promote_prototypes (type)
	   && INTEGRAL_TYPE_P (type)
	   && COMPLETE_TYPE_P (type)
	   && INT_CST_LT_UNSIGNED (TYPE_SIZE (type),
				   TYPE_SIZE (integer_type_node)))
    type = integer_type_node;

  return type;
}

/* Actually perform the appropriate conversion.  */

tree
convert_for_arg_passing (tree type, tree val)
{
  val = convert_bitfield_to_declared_type (val);
  if (val == error_mark_node)
    ;
  /* Pass classes with copy ctors by invisible reference.  */
  else if (TREE_ADDRESSABLE (type))
    val = build1 (ADDR_EXPR, build_reference_type (type), val);
  else if (targetm.calls.promote_prototypes (type)
	   && INTEGRAL_TYPE_P (type)
	   && COMPLETE_TYPE_P (type)
	   && INT_CST_LT_UNSIGNED (TYPE_SIZE (type),
				   TYPE_SIZE (integer_type_node)))
    val = perform_integral_promotions (val);
  if (warn_missing_format_attribute)
    {
      tree rhstype = TREE_TYPE (val);
      const enum tree_code coder = TREE_CODE (rhstype);
      const enum tree_code codel = TREE_CODE (type);
      if ((codel == POINTER_TYPE || codel == REFERENCE_TYPE)
	  && coder == codel
	  && check_missing_format_attribute (type, rhstype))
	warning (OPT_Wmissing_format_attribute,
		 "argument of function call might be a candidate for a format attribute");
    }
  return val;
}

/* Returns true iff FN is a function with magic varargs, i.e. ones for
   which no conversions at all should be done.  This is true for some
   builtins which don't act like normal functions.  */

static bool
magic_varargs_p (tree fn)
{
  if (DECL_BUILT_IN (fn))
    switch (DECL_FUNCTION_CODE (fn))
      {
      case BUILT_IN_CLASSIFY_TYPE:
      case BUILT_IN_CONSTANT_P:
      case BUILT_IN_NEXT_ARG:
      case BUILT_IN_STDARG_START:
      case BUILT_IN_VA_START:
	return true;

      default:;
      }

  return false;
}

/* Subroutine of the various build_*_call functions.  Overload resolution
   has chosen a winning candidate CAND; build up a CALL_EXPR accordingly.
   ARGS is a TREE_LIST of the unconverted arguments to the call.  FLAGS is a
   bitmask of various LOOKUP_* flags which apply to the call itself.  */

static tree
build_over_call (struct z_candidate *cand, int flags)
{
  tree fn = cand->fn;
  tree args = cand->args;
  conversion **convs = cand->convs;
  conversion *conv;
  tree converted_args = NULL_TREE;
  tree parm = TYPE_ARG_TYPES (TREE_TYPE (fn));
  tree arg, val;
  int i = 0;
  int is_method = 0;

  /* In a template, there is no need to perform all of the work that
     is normally done.  We are only interested in the type of the call
     expression, i.e., the return type of the function.  Any semantic
     errors will be deferred until the template is instantiated.  */
  if (processing_template_decl)
    {
      tree expr;
      tree return_type;
      return_type = TREE_TYPE (TREE_TYPE (fn));
      expr = build3 (CALL_EXPR, return_type, fn, args, NULL_TREE);
      if (TREE_THIS_VOLATILE (fn) && cfun)
	current_function_returns_abnormally = 1;
      if (!VOID_TYPE_P (return_type))
	require_complete_type (return_type);
      return convert_from_reference (expr);
    }

  /* Give any warnings we noticed during overload resolution.  */
  if (cand->warnings)
    {
      struct candidate_warning *w;
      for (w = cand->warnings; w; w = w->next)
	joust (cand, w->loser, 1);
    }

  if (DECL_FUNCTION_MEMBER_P (fn))
    {
      /* If FN is a template function, two cases must be considered.
	 For example:

	   struct A {
	     protected:
	       template <class T> void f();
	   };
	   template <class T> struct B {
	     protected:
	       void g();
	   };
	   struct C : A, B<int> {
	     using A::f;	// #1
	     using B<int>::g;	// #2
	   };

	 In case #1 where `A::f' is a member template, DECL_ACCESS is
	 recorded in the primary template but not in its specialization.
	 We check access of FN using its primary template.

	 In case #2, where `B<int>::g' has a DECL_TEMPLATE_INFO simply
	 because it is a member of class template B, DECL_ACCESS is
	 recorded in the specialization `B<int>::g'.  We cannot use its
	 primary template because `B<T>::g' and `B<int>::g' may have
	 different access.  */
      if (DECL_TEMPLATE_INFO (fn)
	  && DECL_MEMBER_TEMPLATE_P (DECL_TI_TEMPLATE (fn)))
	perform_or_defer_access_check (cand->access_path,
				       DECL_TI_TEMPLATE (fn), fn);
      else
	perform_or_defer_access_check (cand->access_path, fn, fn);
    }

  if (args && TREE_CODE (args) != TREE_LIST)
    args = build_tree_list (NULL_TREE, args);
  arg = args;

  /* The implicit parameters to a constructor are not considered by overload
     resolution, and must be of the proper type.  */
  if (DECL_CONSTRUCTOR_P (fn))
    {
      converted_args = tree_cons (NULL_TREE, TREE_VALUE (arg), converted_args);
      arg = TREE_CHAIN (arg);
      parm = TREE_CHAIN (parm);
      /* We should never try to call the abstract constructor.  */
      gcc_assert (!DECL_HAS_IN_CHARGE_PARM_P (fn));

      if (DECL_HAS_VTT_PARM_P (fn))
	{
	  converted_args = tree_cons
	    (NULL_TREE, TREE_VALUE (arg), converted_args);
	  arg = TREE_CHAIN (arg);
	  parm = TREE_CHAIN (parm);
	}
    }
  /* Bypass access control for 'this' parameter.  */
  else if (TREE_CODE (TREE_TYPE (fn)) == METHOD_TYPE)
    {
      tree parmtype = TREE_VALUE (parm);
      tree argtype = TREE_TYPE (TREE_VALUE (arg));
      tree converted_arg;
      tree base_binfo;

      if (convs[i]->bad_p)
	pedwarn ("passing %qT as %<this%> argument of %q#D discards qualifiers",
		 TREE_TYPE (argtype), fn);

      /* [class.mfct.nonstatic]: If a nonstatic member function of a class
	 X is called for an object that is not of type X, or of a type
	 derived from X, the behavior is undefined.

	 So we can assume that anything passed as 'this' is non-null, and
	 optimize accordingly.  */
      gcc_assert (TREE_CODE (parmtype) == POINTER_TYPE);
      /* Convert to the base in which the function was declared.  */
      gcc_assert (cand->conversion_path != NULL_TREE);
      converted_arg = build_base_path (PLUS_EXPR,
				       TREE_VALUE (arg),
				       cand->conversion_path,
				       1);
      /* Check that the base class is accessible.  */
      if (!accessible_base_p (TREE_TYPE (argtype),
			      BINFO_TYPE (cand->conversion_path), true))
	error ("%qT is not an accessible base of %qT",
	       BINFO_TYPE (cand->conversion_path),
	       TREE_TYPE (argtype));
      /* If fn was found by a using declaration, the conversion path
	 will be to the derived class, not the base declaring fn. We
	 must convert from derived to base.  */
      base_binfo = lookup_base (TREE_TYPE (TREE_TYPE (converted_arg)),
				TREE_TYPE (parmtype), ba_unique, NULL);
      converted_arg = build_base_path (PLUS_EXPR, converted_arg,
				       base_binfo, 1);

      converted_args = tree_cons (NULL_TREE, converted_arg, converted_args);
      parm = TREE_CHAIN (parm);
      arg = TREE_CHAIN (arg);
      ++i;
      is_method = 1;
    }

  for (; arg && parm;
       parm = TREE_CHAIN (parm), arg = TREE_CHAIN (arg), ++i)
    {
      tree type = TREE_VALUE (parm);

      conv = convs[i];

      /* Don't make a copy here if build_call is going to.  */
      if (conv->kind == ck_rvalue
	  && !TREE_ADDRESSABLE (complete_type (type)))
	conv = conv->u.next;

      val = convert_like_with_context
	(conv, TREE_VALUE (arg), fn, i - is_method);

      val = convert_for_arg_passing (type, val);
      converted_args = tree_cons (NULL_TREE, val, converted_args);
    }

  /* Default arguments */
  for (; parm && parm != void_list_node; parm = TREE_CHAIN (parm), i++)
    converted_args
      = tree_cons (NULL_TREE,
		   convert_default_arg (TREE_VALUE (parm),
					TREE_PURPOSE (parm),
					fn, i - is_method),
		   converted_args);

  /* Ellipsis */
  for (; arg; arg = TREE_CHAIN (arg))
    {
      tree a = TREE_VALUE (arg);
      if (magic_varargs_p (fn))
	/* Do no conversions for magic varargs.  */;
      else
	a = convert_arg_to_ellipsis (a);
      converted_args = tree_cons (NULL_TREE, a, converted_args);
    }

  converted_args = nreverse (converted_args);

  check_function_arguments (TYPE_ATTRIBUTES (TREE_TYPE (fn)),
			    converted_args, TYPE_ARG_TYPES (TREE_TYPE (fn)));

  /* Avoid actually calling copy constructors and copy assignment operators,
     if possible.  */

  if (! flag_elide_constructors)
    /* Do things the hard way.  */;
  else if (cand->num_convs == 1 && DECL_COPY_CONSTRUCTOR_P (fn))
    {
      tree targ;
      arg = skip_artificial_parms_for (fn, converted_args);
      arg = TREE_VALUE (arg);

      /* Pull out the real argument, disregarding const-correctness.  */
      targ = arg;
      while (TREE_CODE (targ) == NOP_EXPR
	     || TREE_CODE (targ) == NON_LVALUE_EXPR
	     || TREE_CODE (targ) == CONVERT_EXPR)
	targ = TREE_OPERAND (targ, 0);
      if (TREE_CODE (targ) == ADDR_EXPR)
	{
	  targ = TREE_OPERAND (targ, 0);
	  if (!same_type_ignoring_top_level_qualifiers_p
	      (TREE_TYPE (TREE_TYPE (arg)), TREE_TYPE (targ)))
	    targ = NULL_TREE;
	}
      else
	targ = NULL_TREE;

      if (targ)
	arg = targ;
      else
	arg = build_indirect_ref (arg, 0);

      /* [class.copy]: the copy constructor is implicitly defined even if
	 the implementation elided its use.  */
      if (TYPE_HAS_COMPLEX_INIT_REF (DECL_CONTEXT (fn)))
	mark_used (fn);

      /* If we're creating a temp and we already have one, don't create a
	 new one.  If we're not creating a temp but we get one, use
	 INIT_EXPR to collapse the temp into our target.  Otherwise, if the
	 ctor is trivial, do a bitwise copy with a simple TARGET_EXPR for a
	 temp or an INIT_EXPR otherwise.  */
      if (integer_zerop (TREE_VALUE (args)))
	{
	  if (TREE_CODE (arg) == TARGET_EXPR)
	    return arg;
	  else if (TYPE_HAS_TRIVIAL_INIT_REF (DECL_CONTEXT (fn)))
	    return build_target_expr_with_type (arg, DECL_CONTEXT (fn));
	}
      else if (TREE_CODE (arg) == TARGET_EXPR
	       || TYPE_HAS_TRIVIAL_INIT_REF (DECL_CONTEXT (fn)))
	{
	  tree to = stabilize_reference
	    (build_indirect_ref (TREE_VALUE (args), 0));

	  val = build2 (INIT_EXPR, DECL_CONTEXT (fn), to, arg);
	  return val;
	}
    }
  else if (DECL_OVERLOADED_OPERATOR_P (fn) == NOP_EXPR
	   && copy_fn_p (fn)
	   && TYPE_HAS_TRIVIAL_ASSIGN_REF (DECL_CONTEXT (fn)))
    {
      tree to = stabilize_reference
	(build_indirect_ref (TREE_VALUE (converted_args), 0));
      tree type = TREE_TYPE (to);
      tree as_base = CLASSTYPE_AS_BASE (type);

      arg = TREE_VALUE (TREE_CHAIN (converted_args));
      if (tree_int_cst_equal (TYPE_SIZE (type), TYPE_SIZE (as_base)))
	{
	  arg = build_indirect_ref (arg, 0);
	  val = build2 (MODIFY_EXPR, TREE_TYPE (to), to, arg);
	}
      else
	{
	  /* We must only copy the non-tail padding parts.
	     Use __builtin_memcpy for the bitwise copy.  */

	  tree args, t;

	  args = tree_cons (NULL, TYPE_SIZE_UNIT (as_base), NULL);
	  args = tree_cons (NULL, arg, args);
	  t = build_unary_op (ADDR_EXPR, to, 0);
	  args = tree_cons (NULL, t, args);
	  t = implicit_built_in_decls[BUILT_IN_MEMCPY];
	  t = build_call (t, args);

	  t = convert (TREE_TYPE (TREE_VALUE (args)), t);
	  val = build_indirect_ref (t, 0);
	}

      return val;
    }

  mark_used (fn);

  if (DECL_VINDEX (fn) && (flags & LOOKUP_NONVIRTUAL) == 0)
    {
      tree t, *p = &TREE_VALUE (converted_args);
      tree binfo = lookup_base (TREE_TYPE (TREE_TYPE (*p)),
				DECL_CONTEXT (fn),
				ba_any, NULL);
      gcc_assert (binfo && binfo != error_mark_node);

      *p = build_base_path (PLUS_EXPR, *p, binfo, 1);
      if (TREE_SIDE_EFFECTS (*p))
	*p = save_expr (*p);
      t = build_pointer_type (TREE_TYPE (fn));
      if (DECL_CONTEXT (fn) && TYPE_JAVA_INTERFACE (DECL_CONTEXT (fn)))
	fn = build_java_interface_fn_ref (fn, *p);
      else
	fn = build_vfn_ref (*p, DECL_VINDEX (fn));
      TREE_TYPE (fn) = t;
    }
  else if (DECL_INLINE (fn))
    fn = inline_conversion (fn);
  else
    fn = build_addr_func (fn);

  return build_cxx_call (fn, converted_args);
}

/* Build and return a call to FN, using ARGS.  This function performs
   no overload resolution, conversion, or other high-level
   operations.  */

tree
build_cxx_call (tree fn, tree args)
{
  tree fndecl;

  fn = build_call (fn, args);

  /* If this call might throw an exception, note that fact.  */
  fndecl = get_callee_fndecl (fn);
  if ((!fndecl || !TREE_NOTHROW (fndecl))
      && at_function_scope_p ()
      && cfun)
    cp_function_chain->can_throw = 1;

  /* Some built-in function calls will be evaluated at compile-time in
     fold ().  */
  fn = fold_if_not_in_template (fn);

  if (VOID_TYPE_P (TREE_TYPE (fn)))
    return fn;

  fn = require_complete_type (fn);
  if (fn == error_mark_node)
    return error_mark_node;

  if (IS_AGGR_TYPE (TREE_TYPE (fn)))
    fn = build_cplus_new (TREE_TYPE (fn), fn);
  return convert_from_reference (fn);
}

static GTY(()) tree java_iface_lookup_fn;

/* Make an expression which yields the address of the Java interface
   method FN.  This is achieved by generating a call to libjava's
   _Jv_LookupInterfaceMethodIdx().  */

static tree
build_java_interface_fn_ref (tree fn, tree instance)
{
  tree lookup_args, lookup_fn, method, idx;
  tree klass_ref, iface, iface_ref;
  int i;

  if (!java_iface_lookup_fn)
    {
      tree endlink = build_void_list_node ();
      tree t = tree_cons (NULL_TREE, ptr_type_node,
			  tree_cons (NULL_TREE, ptr_type_node,
				     tree_cons (NULL_TREE, java_int_type_node,
						endlink)));
      java_iface_lookup_fn
	= builtin_function ("_Jv_LookupInterfaceMethodIdx",
			    build_function_type (ptr_type_node, t),
			    0, NOT_BUILT_IN, NULL, NULL_TREE);
    }

  /* Look up the pointer to the runtime java.lang.Class object for `instance'.
     This is the first entry in the vtable.  */
  klass_ref = build_vtbl_ref (build_indirect_ref (instance, 0),
			      integer_zero_node);

  /* Get the java.lang.Class pointer for the interface being called.  */
  iface = DECL_CONTEXT (fn);
  iface_ref = lookup_field (iface, get_identifier ("class$"), 0, false);
  if (!iface_ref || TREE_CODE (iface_ref) != VAR_DECL
      || DECL_CONTEXT (iface_ref) != iface)
    {
      error ("could not find class$ field in java interface type %qT",
		iface);
      return error_mark_node;
    }
  iface_ref = build_address (iface_ref);
  iface_ref = convert (build_pointer_type (iface), iface_ref);

  /* Determine the itable index of FN.  */
  i = 1;
  for (method = TYPE_METHODS (iface); method; method = TREE_CHAIN (method))
    {
      if (!DECL_VIRTUAL_P (method))
	continue;
      if (fn == method)
	break;
      i++;
    }
  idx = build_int_cst (NULL_TREE, i);

  lookup_args = tree_cons (NULL_TREE, klass_ref,
			   tree_cons (NULL_TREE, iface_ref,
				      build_tree_list (NULL_TREE, idx)));
  lookup_fn = build1 (ADDR_EXPR,
		      build_pointer_type (TREE_TYPE (java_iface_lookup_fn)),
		      java_iface_lookup_fn);
  return build3 (CALL_EXPR, ptr_type_node, lookup_fn, lookup_args, NULL_TREE);
}

/* Returns the value to use for the in-charge parameter when making a
   call to a function with the indicated NAME.

   FIXME:Can't we find a neater way to do this mapping?  */

tree
in_charge_arg_for_name (tree name)
{
 if (name == base_ctor_identifier
      || name == base_dtor_identifier)
    return integer_zero_node;
  else if (name == complete_ctor_identifier)
    return integer_one_node;
  else if (name == complete_dtor_identifier)
    return integer_two_node;
  else if (name == deleting_dtor_identifier)
    return integer_three_node;

  /* This function should only be called with one of the names listed
     above.  */
  gcc_unreachable ();
  return NULL_TREE;
}

/* Build a call to a constructor, destructor, or an assignment
   operator for INSTANCE, an expression with class type.  NAME
   indicates the special member function to call; ARGS are the
   arguments.  BINFO indicates the base of INSTANCE that is to be
   passed as the `this' parameter to the member function called.

   FLAGS are the LOOKUP_* flags to use when processing the call.

   If NAME indicates a complete object constructor, INSTANCE may be
   NULL_TREE.  In this case, the caller will call build_cplus_new to
   store the newly constructed object into a VAR_DECL.  */

tree
build_special_member_call (tree instance, tree name, tree args,
			   tree binfo, int flags)
{
  tree fns;
  /* The type of the subobject to be constructed or destroyed.  */
  tree class_type;

  gcc_assert (name == complete_ctor_identifier
	      || name == base_ctor_identifier
	      || name == complete_dtor_identifier
	      || name == base_dtor_identifier
	      || name == deleting_dtor_identifier
	      || name == ansi_assopname (NOP_EXPR));
  if (TYPE_P (binfo))
    {
      /* Resolve the name.  */
      if (!complete_type_or_else (binfo, NULL_TREE))
	return error_mark_node;

      binfo = TYPE_BINFO (binfo);
    }

  gcc_assert (binfo != NULL_TREE);

  class_type = BINFO_TYPE (binfo);

  /* Handle the special case where INSTANCE is NULL_TREE.  */
  if (name == complete_ctor_identifier && !instance)
    {
      instance = build_int_cst (build_pointer_type (class_type), 0);
      instance = build1 (INDIRECT_REF, class_type, instance);
    }
  else
    {
      if (name == complete_dtor_identifier
	  || name == base_dtor_identifier
	  || name == deleting_dtor_identifier)
	gcc_assert (args == NULL_TREE);

      /* Convert to the base class, if necessary.  */
      if (!same_type_ignoring_top_level_qualifiers_p
	  (TREE_TYPE (instance), BINFO_TYPE (binfo)))
	{
	  if (name != ansi_assopname (NOP_EXPR))
	    /* For constructors and destructors, either the base is
	       non-virtual, or it is virtual but we are doing the
	       conversion from a constructor or destructor for the
	       complete object.  In either case, we can convert
	       statically.  */
	    instance = convert_to_base_statically (instance, binfo);
	  else
	    /* However, for assignment operators, we must convert
	       dynamically if the base is virtual.  */
	    instance = build_base_path (PLUS_EXPR, instance,
					binfo, /*nonnull=*/1);
	}
    }

  gcc_assert (instance != NULL_TREE);

  fns = lookup_fnfields (binfo, name, 1);

  /* When making a call to a constructor or destructor for a subobject
     that uses virtual base classes, pass down a pointer to a VTT for
     the subobject.  */
  if ((name == base_ctor_identifier
       || name == base_dtor_identifier)
      && CLASSTYPE_VBASECLASSES (class_type))
    {
      tree vtt;
      tree sub_vtt;

      /* If the current function is a complete object constructor
	 or destructor, then we fetch the VTT directly.
	 Otherwise, we look it up using the VTT we were given.  */
      vtt = TREE_CHAIN (CLASSTYPE_VTABLES (current_class_type));
      vtt = decay_conversion (vtt);
      vtt = build3 (COND_EXPR, TREE_TYPE (vtt),
		    build2 (EQ_EXPR, boolean_type_node,
			    current_in_charge_parm, integer_zero_node),
		    current_vtt_parm,
		    vtt);
      gcc_assert (BINFO_SUBVTT_INDEX (binfo));
      sub_vtt = build2 (PLUS_EXPR, TREE_TYPE (vtt), vtt,
			BINFO_SUBVTT_INDEX (binfo));

      args = tree_cons (NULL_TREE, sub_vtt, args);
    }

  return build_new_method_call (instance, fns, args,
				TYPE_BINFO (BINFO_TYPE (binfo)),
				flags, /*fn=*/NULL);
}

/* Return the NAME, as a C string.  The NAME indicates a function that
   is a member of TYPE.  *FREE_P is set to true if the caller must
   free the memory returned.

   Rather than go through all of this, we should simply set the names
   of constructors and destructors appropriately, and dispense with
   ctor_identifier, dtor_identifier, etc.  */

static char *
name_as_c_string (tree name, tree type, bool *free_p)
{
  char *pretty_name;

  /* Assume that we will not allocate memory.  */
  *free_p = false;
  /* Constructors and destructors are special.  */
  if (IDENTIFIER_CTOR_OR_DTOR_P (name))
    {
      pretty_name
	= (char *) IDENTIFIER_POINTER (constructor_name (type));
      /* For a destructor, add the '~'.  */
      if (name == complete_dtor_identifier
	  || name == base_dtor_identifier
	  || name == deleting_dtor_identifier)
	{
	  pretty_name = concat ("~", pretty_name, NULL);
	  /* Remember that we need to free the memory allocated.  */
	  *free_p = true;
	}
    }
  else if (IDENTIFIER_TYPENAME_P (name))
    {
      pretty_name = concat ("operator ",
			    type_as_string (TREE_TYPE (name),
					    TFF_PLAIN_IDENTIFIER),
			    NULL);
      /* Remember that we need to free the memory allocated.  */
      *free_p = true;
    }
  else
    pretty_name = (char *) IDENTIFIER_POINTER (name);

  return pretty_name;
}

/* Build a call to "INSTANCE.FN (ARGS)".  If FN_P is non-NULL, it will
   be set, upon return, to the function called.  */

tree
build_new_method_call (tree instance, tree fns, tree args,
		       tree conversion_path, int flags,
		       tree *fn_p)
{
  struct z_candidate *candidates = 0, *cand;
  tree explicit_targs = NULL_TREE;
  tree basetype = NULL_TREE;
  tree access_binfo;
  tree optype;
  tree mem_args = NULL_TREE, instance_ptr;
  tree name;
  tree user_args;
  tree call;
  tree fn;
  tree class_type;
  int template_only = 0;
  bool any_viable_p;
  tree orig_instance;
  tree orig_fns;
  tree orig_args;
  void *p;

  gcc_assert (instance != NULL_TREE);

  /* We don't know what function we're going to call, yet.  */
  if (fn_p)
    *fn_p = NULL_TREE;

  if (error_operand_p (instance)
      || error_operand_p (fns)
      || args == error_mark_node)
    return error_mark_node;

  if (!BASELINK_P (fns))
    {
      error ("call to non-function %qD", fns);
      return error_mark_node;
    }

  orig_instance = instance;
  orig_fns = fns;
  orig_args = args;

  /* Dismantle the baselink to collect all the information we need.  */
  if (!conversion_path)
    conversion_path = BASELINK_BINFO (fns);
  access_binfo = BASELINK_ACCESS_BINFO (fns);
  optype = BASELINK_OPTYPE (fns);
  fns = BASELINK_FUNCTIONS (fns);
  if (TREE_CODE (fns) == TEMPLATE_ID_EXPR)
    {
      explicit_targs = TREE_OPERAND (fns, 1);
      fns = TREE_OPERAND (fns, 0);
      template_only = 1;
    }
  gcc_assert (TREE_CODE (fns) == FUNCTION_DECL
	      || TREE_CODE (fns) == TEMPLATE_DECL
	      || TREE_CODE (fns) == OVERLOAD);
  fn = get_first_fn (fns);
  name = DECL_NAME (fn);

  basetype = TYPE_MAIN_VARIANT (TREE_TYPE (instance));
  gcc_assert (CLASS_TYPE_P (basetype));

  if (processing_template_decl)
    {
      instance = build_non_dependent_expr (instance);
      args = build_non_dependent_args (orig_args);
    }

  /* The USER_ARGS are the arguments we will display to users if an
     error occurs.  The USER_ARGS should not include any
     compiler-generated arguments.  The "this" pointer hasn't been
     added yet.  However, we must remove the VTT pointer if this is a
     call to a base-class constructor or destructor.  */
  user_args = args;
  if (IDENTIFIER_CTOR_OR_DTOR_P (name))
    {
      /* Callers should explicitly indicate whether they want to construct
	 the complete object or just the part without virtual bases.  */
      gcc_assert (name != ctor_identifier);
      /* Similarly for destructors.  */
      gcc_assert (name != dtor_identifier);
      /* Remove the VTT pointer, if present.  */
      if ((name == base_ctor_identifier || name == base_dtor_identifier)
	  && CLASSTYPE_VBASECLASSES (basetype))
	user_args = TREE_CHAIN (user_args);
    }

  /* Process the argument list.  */
  args = resolve_args (args);
  if (args == error_mark_node)
    return error_mark_node;

  instance_ptr = build_this (instance);

  /* It's OK to call destructors on cv-qualified objects.  Therefore,
     convert the INSTANCE_PTR to the unqualified type, if necessary.  */
  if (DECL_DESTRUCTOR_P (fn))
    {
      tree type = build_pointer_type (basetype);
      if (!same_type_p (type, TREE_TYPE (instance_ptr)))
	instance_ptr = build_nop (type, instance_ptr);
      name = complete_dtor_identifier;
    }

  class_type = (conversion_path ? BINFO_TYPE (conversion_path) : NULL_TREE);
  mem_args = tree_cons (NULL_TREE, instance_ptr, args);

  /* Get the high-water mark for the CONVERSION_OBSTACK.  */
  p = conversion_obstack_alloc (0);

  for (fn = fns; fn; fn = OVL_NEXT (fn))
    {
      tree t = OVL_CURRENT (fn);
      tree this_arglist;

      /* We can end up here for copy-init of same or base class.  */
      if ((flags & LOOKUP_ONLYCONVERTING)
	  && DECL_NONCONVERTING_P (t))
	continue;

      if (DECL_NONSTATIC_MEMBER_FUNCTION_P (t))
	this_arglist = mem_args;
      else
	this_arglist = args;

      if (TREE_CODE (t) == TEMPLATE_DECL)
	/* A member template.  */
	add_template_candidate (&candidates, t,
				class_type,
				explicit_targs,
				this_arglist, optype,
				access_binfo,
				conversion_path,
				flags,
				DEDUCE_CALL);
      else if (! template_only)
	add_function_candidate (&candidates, t,
				class_type,
				this_arglist,
				access_binfo,
				conversion_path,
				flags);
    }

  candidates = splice_viable (candidates, pedantic, &any_viable_p);
  if (!any_viable_p)
    {
      if (!COMPLETE_TYPE_P (basetype))
	cxx_incomplete_type_error (instance_ptr, basetype);
      else
	{
	  char *pretty_name;
	  bool free_p;

	  pretty_name = name_as_c_string (name, basetype, &free_p);
	  error ("no matching function for call to %<%T::%s(%A)%#V%>",
		 basetype, pretty_name, user_args,
		 TREE_TYPE (TREE_TYPE (instance_ptr)));
	  if (free_p)
	    free (pretty_name);
	}
      print_z_candidates (candidates);
      call = error_mark_node;
    }
  else
    {
      cand = tourney (candidates);
      if (cand == 0)
	{
	  char *pretty_name;
	  bool free_p;

	  pretty_name = name_as_c_string (name, basetype, &free_p);
	  error ("call of overloaded %<%s(%A)%> is ambiguous", pretty_name,
		 user_args);
	  print_z_candidates (candidates);
	  if (free_p)
	    free (pretty_name);
	  call = error_mark_node;
	}
      else
	{
	  fn = cand->fn;

	  if (!(flags & LOOKUP_NONVIRTUAL)
	      && DECL_PURE_VIRTUAL_P (fn)
	      && instance == current_class_ref
	      && (DECL_CONSTRUCTOR_P (current_function_decl)
		  || DECL_DESTRUCTOR_P (current_function_decl)))
	    /* This is not an error, it is runtime undefined
	       behavior.  */
	    warning (0, (DECL_CONSTRUCTOR_P (current_function_decl) ?
		      "abstract virtual %q#D called from constructor"
		      : "abstract virtual %q#D called from destructor"),
		     fn);

	  if (TREE_CODE (TREE_TYPE (fn)) == METHOD_TYPE
	      && is_dummy_object (instance_ptr))
	    {
	      error ("cannot call member function %qD without object",
		     fn);
	      call = error_mark_node;
	    }
	  else
	    {
	      if (DECL_VINDEX (fn) && ! (flags & LOOKUP_NONVIRTUAL)
		  && resolves_to_fixed_type_p (instance, 0))
		flags |= LOOKUP_NONVIRTUAL;
	      /* Now we know what function is being called.  */
	      if (fn_p)
		*fn_p = fn;
	      /* Build the actual CALL_EXPR.  */
	      call = build_over_call (cand, flags);
	      /* In an expression of the form `a->f()' where `f' turns
		 out to be a static member function, `a' is
		 none-the-less evaluated.  */
	      if (TREE_CODE (TREE_TYPE (fn)) != METHOD_TYPE
		  && !is_dummy_object (instance_ptr)
		  && TREE_SIDE_EFFECTS (instance_ptr))
		call = build2 (COMPOUND_EXPR, TREE_TYPE (call),
			       instance_ptr, call);
	      else if (call != error_mark_node
		       && DECL_DESTRUCTOR_P (cand->fn)
		       && !VOID_TYPE_P (TREE_TYPE (call)))
		/* An explicit call of the form "x->~X()" has type
		   "void".  However, on platforms where destructors
		   return "this" (i.e., those where
		   targetm.cxx.cdtor_returns_this is true), such calls
		   will appear to have a return value of pointer type
		   to the low-level call machinery.  We do not want to
		   change the low-level machinery, since we want to be
		   able to optimize "delete f()" on such platforms as
		   "operator delete(~X(f()))" (rather than generating
		   "t = f(), ~X(t), operator delete (t)").  */
		call = build_nop (void_type_node, call);
	    }
	}
    }

  if (processing_template_decl && call != error_mark_node)
    call = (build_min_non_dep
	    (CALL_EXPR, call,
	     build_min_nt (COMPONENT_REF, orig_instance, orig_fns, NULL_TREE),
	     orig_args, NULL_TREE));

 /* Free all the conversions we allocated.  */
  obstack_free (&conversion_obstack, p);

  return call;
}

/* Returns true iff standard conversion sequence ICS1 is a proper
   subsequence of ICS2.  */

static bool
is_subseq (conversion *ics1, conversion *ics2)
{
  /* We can assume that a conversion of the same code
     between the same types indicates a subsequence since we only get
     here if the types we are converting from are the same.  */

  while (ics1->kind == ck_rvalue
	 || ics1->kind == ck_lvalue)
    ics1 = ics1->u.next;

  while (1)
    {
      while (ics2->kind == ck_rvalue
	     || ics2->kind == ck_lvalue)
	ics2 = ics2->u.next;

      if (ics2->kind == ck_user
	  || ics2->kind == ck_ambig
	  || ics2->kind == ck_identity)
	/* At this point, ICS1 cannot be a proper subsequence of
	   ICS2.  We can get a USER_CONV when we are comparing the
	   second standard conversion sequence of two user conversion
	   sequences.  */
	return false;

      ics2 = ics2->u.next;

      if (ics2->kind == ics1->kind
	  && same_type_p (ics2->type, ics1->type)
	  && same_type_p (ics2->u.next->type,
			  ics1->u.next->type))
	return true;
    }
}

/* Returns nonzero iff DERIVED is derived from BASE.  The inputs may
   be any _TYPE nodes.  */

bool
is_properly_derived_from (tree derived, tree base)
{
  if (!IS_AGGR_TYPE_CODE (TREE_CODE (derived))
      || !IS_AGGR_TYPE_CODE (TREE_CODE (base)))
    return false;

  /* We only allow proper derivation here.  The DERIVED_FROM_P macro
     considers every class derived from itself.  */
  return (!same_type_ignoring_top_level_qualifiers_p (derived, base)
	  && DERIVED_FROM_P (base, derived));
}

/* We build the ICS for an implicit object parameter as a pointer
   conversion sequence.  However, such a sequence should be compared
   as if it were a reference conversion sequence.  If ICS is the
   implicit conversion sequence for an implicit object parameter,
   modify it accordingly.  */

static void
maybe_handle_implicit_object (conversion **ics)
{
  if ((*ics)->this_p)
    {
      /* [over.match.funcs]

	 For non-static member functions, the type of the
	 implicit object parameter is "reference to cv X"
	 where X is the class of which the function is a
	 member and cv is the cv-qualification on the member
	 function declaration.  */
      conversion *t = *ics;
      tree reference_type;

      /* The `this' parameter is a pointer to a class type.  Make the
	 implicit conversion talk about a reference to that same class
	 type.  */
      reference_type = TREE_TYPE (t->type);
      reference_type = build_reference_type (reference_type);

      if (t->kind == ck_qual)
	t = t->u.next;
      if (t->kind == ck_ptr)
	t = t->u.next;
      t = build_identity_conv (TREE_TYPE (t->type), NULL_TREE);
      t = direct_reference_binding (reference_type, t);
      *ics = t;
    }
}

/* If *ICS is a REF_BIND set *ICS to the remainder of the conversion,
   and return the type to which the reference refers.  Otherwise,
   leave *ICS unchanged and return NULL_TREE.  */

static tree
maybe_handle_ref_bind (conversion **ics)
{
  if ((*ics)->kind == ck_ref_bind)
    {
      conversion *old_ics = *ics;
      tree type = TREE_TYPE (old_ics->type);
      *ics = old_ics->u.next;
      (*ics)->user_conv_p = old_ics->user_conv_p;
      (*ics)->bad_p = old_ics->bad_p;
      return type;
    }

  return NULL_TREE;
}

/* Compare two implicit conversion sequences according to the rules set out in
   [over.ics.rank].  Return values:

      1: ics1 is better than ics2
     -1: ics2 is better than ics1
      0: ics1 and ics2 are indistinguishable */

static int
compare_ics (conversion *ics1, conversion *ics2)
{
  tree from_type1;
  tree from_type2;
  tree to_type1;
  tree to_type2;
  tree deref_from_type1 = NULL_TREE;
  tree deref_from_type2 = NULL_TREE;
  tree deref_to_type1 = NULL_TREE;
  tree deref_to_type2 = NULL_TREE;
  conversion_rank rank1, rank2;

  /* REF_BINDING is nonzero if the result of the conversion sequence
     is a reference type.   In that case TARGET_TYPE is the
     type referred to by the reference.  */
  tree target_type1;
  tree target_type2;

  /* Handle implicit object parameters.  */
  maybe_handle_implicit_object (&ics1);
  maybe_handle_implicit_object (&ics2);

  /* Handle reference parameters.  */
  target_type1 = maybe_handle_ref_bind (&ics1);
  target_type2 = maybe_handle_ref_bind (&ics2);

  /* [over.ics.rank]

     When  comparing  the  basic forms of implicit conversion sequences (as
     defined in _over.best.ics_)

     --a standard conversion sequence (_over.ics.scs_) is a better
       conversion sequence than a user-defined conversion sequence
       or an ellipsis conversion sequence, and

     --a user-defined conversion sequence (_over.ics.user_) is a
       better conversion sequence than an ellipsis conversion sequence
       (_over.ics.ellipsis_).  */
  rank1 = CONVERSION_RANK (ics1);
  rank2 = CONVERSION_RANK (ics2);

  if (rank1 > rank2)
    return -1;
  else if (rank1 < rank2)
    return 1;

  if (rank1 == cr_bad)
    {
      /* XXX Isn't this an extension? */
      /* Both ICS are bad.  We try to make a decision based on what
	 would have happened if they'd been good.  */
      if (ics1->user_conv_p > ics2->user_conv_p
	  || ics1->rank  > ics2->rank)
	return -1;
      else if (ics1->user_conv_p < ics2->user_conv_p
	       || ics1->rank < ics2->rank)
	return 1;

      /* We couldn't make up our minds; try to figure it out below.  */
    }

  if (ics1->ellipsis_p)
    /* Both conversions are ellipsis conversions.  */
    return 0;

  /* User-defined  conversion sequence U1 is a better conversion sequence
     than another user-defined conversion sequence U2 if they contain the
     same user-defined conversion operator or constructor and if the sec-
     ond standard conversion sequence of U1 is  better  than  the  second
     standard conversion sequence of U2.  */

  if (ics1->user_conv_p)
    {
      conversion *t1;
      conversion *t2;

      for (t1 = ics1; t1->kind != ck_user; t1 = t1->u.next)
	if (t1->kind == ck_ambig)
	  return 0;
      for (t2 = ics2; t2->kind != ck_user; t2 = t2->u.next)
	if (t2->kind == ck_ambig)
	  return 0;

      if (t1->cand->fn != t2->cand->fn)
	return 0;

      /* We can just fall through here, after setting up
	 FROM_TYPE1 and FROM_TYPE2.  */
      from_type1 = t1->type;
      from_type2 = t2->type;
    }
  else
    {
      conversion *t1;
      conversion *t2;

      /* We're dealing with two standard conversion sequences.

	 [over.ics.rank]

	 Standard conversion sequence S1 is a better conversion
	 sequence than standard conversion sequence S2 if

	 --S1 is a proper subsequence of S2 (comparing the conversion
	   sequences in the canonical form defined by _over.ics.scs_,
	   excluding any Lvalue Transformation; the identity
	   conversion sequence is considered to be a subsequence of
	   any non-identity conversion sequence */

      t1 = ics1;
      while (t1->kind != ck_identity)
	t1 = t1->u.next;
      from_type1 = t1->type;

      t2 = ics2;
      while (t2->kind != ck_identity)
	t2 = t2->u.next;
      from_type2 = t2->type;
    }

  if (same_type_p (from_type1, from_type2))
    {
      if (is_subseq (ics1, ics2))
	return 1;
      if (is_subseq (ics2, ics1))
	return -1;
    }
  /* Otherwise, one sequence cannot be a subsequence of the other; they
     don't start with the same type.  This can happen when comparing the
     second standard conversion sequence in two user-defined conversion
     sequences.  */

  /* [over.ics.rank]

     Or, if not that,

     --the rank of S1 is better than the rank of S2 (by the rules
       defined below):

    Standard conversion sequences are ordered by their ranks: an Exact
    Match is a better conversion than a Promotion, which is a better
    conversion than a Conversion.

    Two conversion sequences with the same rank are indistinguishable
    unless one of the following rules applies:

    --A conversion that is not a conversion of a pointer, or pointer
      to member, to bool is better than another conversion that is such
      a conversion.

    The ICS_STD_RANK automatically handles the pointer-to-bool rule,
    so that we do not have to check it explicitly.  */
  if (ics1->rank < ics2->rank)
    return 1;
  else if (ics2->rank < ics1->rank)
    return -1;

  to_type1 = ics1->type;
  to_type2 = ics2->type;

  if (TYPE_PTR_P (from_type1)
      && TYPE_PTR_P (from_type2)
      && TYPE_PTR_P (to_type1)
      && TYPE_PTR_P (to_type2))
    {
      deref_from_type1 = TREE_TYPE (from_type1);
      deref_from_type2 = TREE_TYPE (from_type2);
      deref_to_type1 = TREE_TYPE (to_type1);
      deref_to_type2 = TREE_TYPE (to_type2);
    }
  /* The rules for pointers to members A::* are just like the rules
     for pointers A*, except opposite: if B is derived from A then
     A::* converts to B::*, not vice versa.  For that reason, we
     switch the from_ and to_ variables here.  */
  else if ((TYPE_PTRMEM_P (from_type1) && TYPE_PTRMEM_P (from_type2)
	    && TYPE_PTRMEM_P (to_type1) && TYPE_PTRMEM_P (to_type2))
	   || (TYPE_PTRMEMFUNC_P (from_type1)
	       && TYPE_PTRMEMFUNC_P (from_type2)
	       && TYPE_PTRMEMFUNC_P (to_type1)
	       && TYPE_PTRMEMFUNC_P (to_type2)))
    {
      deref_to_type1 = TYPE_PTRMEM_CLASS_TYPE (from_type1);
      deref_to_type2 = TYPE_PTRMEM_CLASS_TYPE (from_type2);
      deref_from_type1 = TYPE_PTRMEM_CLASS_TYPE (to_type1);
      deref_from_type2 = TYPE_PTRMEM_CLASS_TYPE (to_type2);
    }

  if (deref_from_type1 != NULL_TREE
      && IS_AGGR_TYPE_CODE (TREE_CODE (deref_from_type1))
      && IS_AGGR_TYPE_CODE (TREE_CODE (deref_from_type2)))
    {
      /* This was one of the pointer or pointer-like conversions.

	 [over.ics.rank]

	 --If class B is derived directly or indirectly from class A,
	   conversion of B* to A* is better than conversion of B* to
	   void*, and conversion of A* to void* is better than
	   conversion of B* to void*.  */
      if (TREE_CODE (deref_to_type1) == VOID_TYPE
	  && TREE_CODE (deref_to_type2) == VOID_TYPE)
	{
	  if (is_properly_derived_from (deref_from_type1,
					deref_from_type2))
	    return -1;
	  else if (is_properly_derived_from (deref_from_type2,
					     deref_from_type1))
	    return 1;
	}
      else if (TREE_CODE (deref_to_type1) == VOID_TYPE
	       || TREE_CODE (deref_to_type2) == VOID_TYPE)
	{
	  if (same_type_p (deref_from_type1, deref_from_type2))
	    {
	      if (TREE_CODE (deref_to_type2) == VOID_TYPE)
		{
		  if (is_properly_derived_from (deref_from_type1,
						deref_to_type1))
		    return 1;
		}
	      /* We know that DEREF_TO_TYPE1 is `void' here.  */
	      else if (is_properly_derived_from (deref_from_type1,
						 deref_to_type2))
		return -1;
	    }
	}
      else if (IS_AGGR_TYPE_CODE (TREE_CODE (deref_to_type1))
	       && IS_AGGR_TYPE_CODE (TREE_CODE (deref_to_type2)))
	{
	  /* [over.ics.rank]

	     --If class B is derived directly or indirectly from class A
	       and class C is derived directly or indirectly from B,

	     --conversion of C* to B* is better than conversion of C* to
	       A*,

	     --conversion of B* to A* is better than conversion of C* to
	       A*  */
	  if (same_type_p (deref_from_type1, deref_from_type2))
	    {
	      if (is_properly_derived_from (deref_to_type1,
					    deref_to_type2))
		return 1;
	      else if (is_properly_derived_from (deref_to_type2,
						 deref_to_type1))
		return -1;
	    }
	  else if (same_type_p (deref_to_type1, deref_to_type2))
	    {
	      if (is_properly_derived_from (deref_from_type2,
					    deref_from_type1))
		return 1;
	      else if (is_properly_derived_from (deref_from_type1,
						 deref_from_type2))
		return -1;
	    }
	}
    }
  else if (CLASS_TYPE_P (non_reference (from_type1))
	   && same_type_p (from_type1, from_type2))
    {
      tree from = non_reference (from_type1);

      /* [over.ics.rank]

	 --binding of an expression of type C to a reference of type
	   B& is better than binding an expression of type C to a
	   reference of type A&

	 --conversion of C to B is better than conversion of C to A,  */
      if (is_properly_derived_from (from, to_type1)
	  && is_properly_derived_from (from, to_type2))
	{
	  if (is_properly_derived_from (to_type1, to_type2))
	    return 1;
	  else if (is_properly_derived_from (to_type2, to_type1))
	    return -1;
	}
    }
  else if (CLASS_TYPE_P (non_reference (to_type1))
	   && same_type_p (to_type1, to_type2))
    {
      tree to = non_reference (to_type1);

      /* [over.ics.rank]

	 --binding of an expression of type B to a reference of type
	   A& is better than binding an expression of type C to a
	   reference of type A&,

	 --conversion of B to A is better than conversion of C to A  */
      if (is_properly_derived_from (from_type1, to)
	  && is_properly_derived_from (from_type2, to))
	{
	  if (is_properly_derived_from (from_type2, from_type1))
	    return 1;
	  else if (is_properly_derived_from (from_type1, from_type2))
	    return -1;
	}
    }

  /* [over.ics.rank]

     --S1 and S2 differ only in their qualification conversion and  yield
       similar  types  T1 and T2 (_conv.qual_), respectively, and the cv-
       qualification signature of type T1 is a proper subset of  the  cv-
       qualification signature of type T2  */
  if (ics1->kind == ck_qual
      && ics2->kind == ck_qual
      && same_type_p (from_type1, from_type2))
    return comp_cv_qual_signature (to_type1, to_type2);

  /* [over.ics.rank]

     --S1 and S2 are reference bindings (_dcl.init.ref_), and the
     types to which the references refer are the same type except for
     top-level cv-qualifiers, and the type to which the reference
     initialized by S2 refers is more cv-qualified than the type to
     which the reference initialized by S1 refers */

  if (target_type1 && target_type2
      && same_type_ignoring_top_level_qualifiers_p (to_type1, to_type2))
    return comp_cv_qualification (target_type2, target_type1);

  /* Neither conversion sequence is better than the other.  */
  return 0;
}

/* The source type for this standard conversion sequence.  */

static tree
source_type (conversion *t)
{
  for (;; t = t->u.next)
    {
      if (t->kind == ck_user
	  || t->kind == ck_ambig
	  || t->kind == ck_identity)
	return t->type;
    }
  gcc_unreachable ();
}

/* Note a warning about preferring WINNER to LOSER.  We do this by storing
   a pointer to LOSER and re-running joust to produce the warning if WINNER
   is actually used.  */

static void
add_warning (struct z_candidate *winner, struct z_candidate *loser)
{
  candidate_warning *cw = (candidate_warning *)
    conversion_obstack_alloc (sizeof (candidate_warning));
  cw->loser = loser;
  cw->next = winner->warnings;
  winner->warnings = cw;
}

/* Compare two candidates for overloading as described in
   [over.match.best].  Return values:

      1: cand1 is better than cand2
     -1: cand2 is better than cand1
      0: cand1 and cand2 are indistinguishable */

static int
joust (struct z_candidate *cand1, struct z_candidate *cand2, bool warn)
{
  int winner = 0;
  int off1 = 0, off2 = 0;
  size_t i;
  size_t len;

  /* Candidates that involve bad conversions are always worse than those
     that don't.  */
  if (cand1->viable > cand2->viable)
    return 1;
  if (cand1->viable < cand2->viable)
    return -1;

  /* If we have two pseudo-candidates for conversions to the same type,
     or two candidates for the same function, arbitrarily pick one.  */
  if (cand1->fn == cand2->fn
      && (IS_TYPE_OR_DECL_P (cand1->fn)))
    return 1;

  /* a viable function F1
     is defined to be a better function than another viable function F2  if
     for  all arguments i, ICSi(F1) is not a worse conversion sequence than
     ICSi(F2), and then */

  /* for some argument j, ICSj(F1) is a better conversion  sequence  than
     ICSj(F2) */

  /* For comparing static and non-static member functions, we ignore
     the implicit object parameter of the non-static function.  The
     standard says to pretend that the static function has an object
     parm, but that won't work with operator overloading.  */
  len = cand1->num_convs;
  if (len != cand2->num_convs)
    {
      int static_1 = DECL_STATIC_FUNCTION_P (cand1->fn);
      int static_2 = DECL_STATIC_FUNCTION_P (cand2->fn);

      gcc_assert (static_1 != static_2);

      if (static_1)
	off2 = 1;
      else
	{
	  off1 = 1;
	  --len;
	}
    }

  for (i = 0; i < len; ++i)
    {
      conversion *t1 = cand1->convs[i + off1];
      conversion *t2 = cand2->convs[i + off2];
      int comp = compare_ics (t1, t2);

      if (comp != 0)
	{
	  if (warn_sign_promo
	      && (CONVERSION_RANK (t1) + CONVERSION_RANK (t2)
		  == cr_std + cr_promotion)
	      && t1->kind == ck_std
	      && t2->kind == ck_std
	      && TREE_CODE (t1->type) == INTEGER_TYPE
	      && TREE_CODE (t2->type) == INTEGER_TYPE
	      && (TYPE_PRECISION (t1->type)
		  == TYPE_PRECISION (t2->type))
	      && (TYPE_UNSIGNED (t1->u.next->type)
		  || (TREE_CODE (t1->u.next->type)
		      == ENUMERAL_TYPE)))
	    {
	      tree type = t1->u.next->type;
	      tree type1, type2;
	      struct z_candidate *w, *l;
	      if (comp > 0)
		type1 = t1->type, type2 = t2->type,
		  w = cand1, l = cand2;
	      else
		type1 = t2->type, type2 = t1->type,
		  w = cand2, l = cand1;

	      if (warn)
		{
		  warning (OPT_Wsign_promo, "passing %qT chooses %qT over %qT",
			   type, type1, type2);
		  warning (OPT_Wsign_promo, "  in call to %qD", w->fn);
		}
	      else
		add_warning (w, l);
	    }

	  if (winner && comp != winner)
	    {
	      winner = 0;
	      goto tweak;
	    }
	  winner = comp;
	}
    }

  /* warn about confusing overload resolution for user-defined conversions,
     either between a constructor and a conversion op, or between two
     conversion ops.  */
  if (winner && warn_conversion && cand1->second_conv
      && (!DECL_CONSTRUCTOR_P (cand1->fn) || !DECL_CONSTRUCTOR_P (cand2->fn))
      && winner != compare_ics (cand1->second_conv, cand2->second_conv))
    {
      struct z_candidate *w, *l;
      bool give_warning = false;

      if (winner == 1)
	w = cand1, l = cand2;
      else
	w = cand2, l = cand1;

      /* We don't want to complain about `X::operator T1 ()'
	 beating `X::operator T2 () const', when T2 is a no less
	 cv-qualified version of T1.  */
      if (DECL_CONTEXT (w->fn) == DECL_CONTEXT (l->fn)
	  && !DECL_CONSTRUCTOR_P (w->fn) && !DECL_CONSTRUCTOR_P (l->fn))
	{
	  tree t = TREE_TYPE (TREE_TYPE (l->fn));
	  tree f = TREE_TYPE (TREE_TYPE (w->fn));

	  if (TREE_CODE (t) == TREE_CODE (f) && POINTER_TYPE_P (t))
	    {
	      t = TREE_TYPE (t);
	      f = TREE_TYPE (f);
	    }
	  if (!comp_ptr_ttypes (t, f))
	    give_warning = true;
	}
      else
	give_warning = true;

      if (!give_warning)
	/*NOP*/;
      else if (warn)
	{
	  tree source = source_type (w->convs[0]);
	  if (! DECL_CONSTRUCTOR_P (w->fn))
	    source = TREE_TYPE (source);
	  warning (OPT_Wconversion, "choosing %qD over %qD", w->fn, l->fn);
	  warning (OPT_Wconversion, "  for conversion from %qT to %qT",
		   source, w->second_conv->type);
	  inform ("  because conversion sequence for the argument is better");
	}
      else
	add_warning (w, l);
    }

  if (winner)
    return winner;

  /* or, if not that,
     F1 is a non-template function and F2 is a template function
     specialization.  */

  if (!cand1->template_decl && cand2->template_decl)
    return 1;
  else if (cand1->template_decl && !cand2->template_decl)
    return -1;

  /* or, if not that,
     F1 and F2 are template functions and the function template for F1 is
     more specialized than the template for F2 according to the partial
     ordering rules.  */

  if (cand1->template_decl && cand2->template_decl)
    {
      winner = more_specialized_fn
	(TI_TEMPLATE (cand1->template_decl),
	 TI_TEMPLATE (cand2->template_decl),
	 /* [temp.func.order]: The presence of unused ellipsis and default
	    arguments has no effect on the partial ordering of function
	    templates.   add_function_candidate() will not have
	    counted the "this" argument for constructors.  */
	 cand1->num_convs + DECL_CONSTRUCTOR_P (cand1->fn));
      if (winner)
	return winner;
    }

  /* or, if not that,
     the  context  is  an  initialization by user-defined conversion (see
     _dcl.init_  and  _over.match.user_)  and  the  standard   conversion
     sequence  from  the return type of F1 to the destination type (i.e.,
     the type of the entity being initialized)  is  a  better  conversion
     sequence  than the standard conversion sequence from the return type
     of F2 to the destination type.  */

  if (cand1->second_conv)
    {
      winner = compare_ics (cand1->second_conv, cand2->second_conv);
      if (winner)
	return winner;
    }

  /* Check whether we can discard a builtin candidate, either because we
     have two identical ones or matching builtin and non-builtin candidates.

     (Pedantically in the latter case the builtin which matched the user
     function should not be added to the overload set, but we spot it here.

     [over.match.oper]
     ... the builtin candidates include ...
     - do not have the same parameter type list as any non-template
       non-member candidate.  */

  if (TREE_CODE (cand1->fn) == IDENTIFIER_NODE
      || TREE_CODE (cand2->fn) == IDENTIFIER_NODE)
    {
      for (i = 0; i < len; ++i)
	if (!same_type_p (cand1->convs[i]->type,
			  cand2->convs[i]->type))
	  break;
      if (i == cand1->num_convs)
	{
	  if (cand1->fn == cand2->fn)
	    /* Two built-in candidates; arbitrarily pick one.  */
	    return 1;
	  else if (TREE_CODE (cand1->fn) == IDENTIFIER_NODE)
	    /* cand1 is built-in; prefer cand2.  */
	    return -1;
	  else
	    /* cand2 is built-in; prefer cand1.  */
	    return 1;
	}
    }

  /* If the two functions are the same (this can happen with declarations
     in multiple scopes and arg-dependent lookup), arbitrarily choose one.  */
  if (DECL_P (cand1->fn) && DECL_P (cand2->fn)
      && equal_functions (cand1->fn, cand2->fn))
    return 1;

tweak:

  /* Extension: If the worst conversion for one candidate is worse than the
     worst conversion for the other, take the first.  */
  if (!pedantic)
    {
      conversion_rank rank1 = cr_identity, rank2 = cr_identity;
      struct z_candidate *w = 0, *l = 0;

      for (i = 0; i < len; ++i)
	{
	  if (CONVERSION_RANK (cand1->convs[i+off1]) > rank1)
	    rank1 = CONVERSION_RANK (cand1->convs[i+off1]);
	  if (CONVERSION_RANK (cand2->convs[i + off2]) > rank2)
	    rank2 = CONVERSION_RANK (cand2->convs[i + off2]);
	}
      if (rank1 < rank2)
	winner = 1, w = cand1, l = cand2;
      if (rank1 > rank2)
	winner = -1, w = cand2, l = cand1;
      if (winner)
	{
	  if (warn)
	    {
	      pedwarn ("\
ISO C++ says that these are ambiguous, even \
though the worst conversion for the first is better than \
the worst conversion for the second:");
	      print_z_candidate (_("candidate 1:"), w);
	      print_z_candidate (_("candidate 2:"), l);
	    }
	  else
	    add_warning (w, l);
	  return winner;
	}
    }

  gcc_assert (!winner);
  return 0;
}

/* Given a list of candidates for overloading, find the best one, if any.
   This algorithm has a worst case of O(2n) (winner is last), and a best
   case of O(n/2) (totally ambiguous); much better than a sorting
   algorithm.  */

static struct z_candidate *
tourney (struct z_candidate *candidates)
{
  struct z_candidate *champ = candidates, *challenger;
  int fate;
  int champ_compared_to_predecessor = 0;

  /* Walk through the list once, comparing each current champ to the next
     candidate, knocking out a candidate or two with each comparison.  */

  for (challenger = champ->next; challenger; )
    {
      fate = joust (champ, challenger, 0);
      if (fate == 1)
	challenger = challenger->next;
      else
	{
	  if (fate == 0)
	    {
	      champ = challenger->next;
	      if (champ == 0)
		return NULL;
	      champ_compared_to_predecessor = 0;
	    }
	  else
	    {
	      champ = challenger;
	      champ_compared_to_predecessor = 1;
	    }

	  challenger = champ->next;
	}
    }

  /* Make sure the champ is better than all the candidates it hasn't yet
     been compared to.  */

  for (challenger = candidates;
       challenger != champ
	 && !(champ_compared_to_predecessor && challenger->next == champ);
       challenger = challenger->next)
    {
      fate = joust (champ, challenger, 0);
      if (fate != 1)
	return NULL;
    }

  return champ;
}

/* Returns nonzero if things of type FROM can be converted to TO.  */

bool
can_convert (tree to, tree from)
{
  return can_convert_arg (to, from, NULL_TREE, LOOKUP_NORMAL);
}

/* Returns nonzero if ARG (of type FROM) can be converted to TO.  */

bool
can_convert_arg (tree to, tree from, tree arg, int flags)
{
  conversion *t;
  void *p;
  bool ok_p;

  /* Get the high-water mark for the CONVERSION_OBSTACK.  */
  p = conversion_obstack_alloc (0);

  t  = implicit_conversion (to, from, arg, /*c_cast_p=*/false,
			    flags);
  ok_p = (t && !t->bad_p);

  /* Free all the conversions we allocated.  */
  obstack_free (&conversion_obstack, p);

  return ok_p;
}

/* Like can_convert_arg, but allows dubious conversions as well.  */

bool
can_convert_arg_bad (tree to, tree from, tree arg)
{
  conversion *t;
  void *p;

  /* Get the high-water mark for the CONVERSION_OBSTACK.  */
  p = conversion_obstack_alloc (0);
  /* Try to perform the conversion.  */
  t  = implicit_conversion (to, from, arg, /*c_cast_p=*/false,
			    LOOKUP_NORMAL);
  /* Free all the conversions we allocated.  */
  obstack_free (&conversion_obstack, p);

  return t != NULL;
}

/* Convert EXPR to TYPE.  Return the converted expression.

   Note that we allow bad conversions here because by the time we get to
   this point we are committed to doing the conversion.  If we end up
   doing a bad conversion, convert_like will complain.  */

tree
perform_implicit_conversion (tree type, tree expr)
{
  conversion *conv;
  void *p;

  if (error_operand_p (expr))
    return error_mark_node;

  /* Get the high-water mark for the CONVERSION_OBSTACK.  */
  p = conversion_obstack_alloc (0);

  conv = implicit_conversion (type, TREE_TYPE (expr), expr,
			      /*c_cast_p=*/false,
			      LOOKUP_NORMAL);
  if (!conv)
    {
      error ("could not convert %qE to %qT", expr, type);
      expr = error_mark_node;
    }
  else if (processing_template_decl)
    {
      /* In a template, we are only concerned about determining the
	 type of non-dependent expressions, so we do not have to
	 perform the actual conversion.  */
      if (TREE_TYPE (expr) != type)
	expr = build_nop (type, expr);
    }
  else
    expr = convert_like (conv, expr);

  /* Free all the conversions we allocated.  */
  obstack_free (&conversion_obstack, p);

  return expr;
}

/* Convert EXPR to TYPE (as a direct-initialization) if that is
   permitted.  If the conversion is valid, the converted expression is
   returned.  Otherwise, NULL_TREE is returned, except in the case
   that TYPE is a class type; in that case, an error is issued.  If
   C_CAST_P is true, then this direction initialization is taking
   place as part of a static_cast being attempted as part of a C-style
   cast.  */

tree
perform_direct_initialization_if_possible (tree type,
					   tree expr,
					   bool c_cast_p)
{
  conversion *conv;
  void *p;

  if (type == error_mark_node || error_operand_p (expr))
    return error_mark_node;
  /* [dcl.init]

     If the destination type is a (possibly cv-qualified) class type:

     -- If the initialization is direct-initialization ...,
     constructors are considered. ... If no constructor applies, or
     the overload resolution is ambiguous, the initialization is
     ill-formed.  */
  if (CLASS_TYPE_P (type))
    {
      expr = build_special_member_call (NULL_TREE, complete_ctor_identifier,
					build_tree_list (NULL_TREE, expr),
					type, LOOKUP_NORMAL);
      return build_cplus_new (type, expr);
    }

  /* Get the high-water mark for the CONVERSION_OBSTACK.  */
  p = conversion_obstack_alloc (0);

  conv = implicit_conversion (type, TREE_TYPE (expr), expr,
			      c_cast_p,
			      LOOKUP_NORMAL);
  if (!conv || conv->bad_p)
    expr = NULL_TREE;
  else
    expr = convert_like_real (conv, expr, NULL_TREE, 0, 0,
			      /*issue_conversion_warnings=*/false,
			      c_cast_p);

  /* Free all the conversions we allocated.  */
  obstack_free (&conversion_obstack, p);

  return expr;
}

/* DECL is a VAR_DECL whose type is a REFERENCE_TYPE.  The reference
   is being bound to a temporary.  Create and return a new VAR_DECL
   with the indicated TYPE; this variable will store the value to
   which the reference is bound.  */

tree
make_temporary_var_for_ref_to_temp (tree decl, tree type)
{
  tree var;

  /* Create the variable.  */
  var = create_temporary_var (type);

  /* Register the variable.  */
  if (TREE_STATIC (decl))
    {
      /* Namespace-scope or local static; give it a mangled name.  */
      tree name;

      TREE_STATIC (var) = 1;
      name = mangle_ref_init_variable (decl);
      DECL_NAME (var) = name;
      SET_DECL_ASSEMBLER_NAME (var, name);
      var = pushdecl_top_level (var);
    }
  else
    /* Create a new cleanup level if necessary.  */
    maybe_push_cleanup_level (type);

  return var;
}

/* Convert EXPR to the indicated reference TYPE, in a way suitable for
   initializing a variable of that TYPE.  If DECL is non-NULL, it is
   the VAR_DECL being initialized with the EXPR.  (In that case, the
   type of DECL will be TYPE.)  If DECL is non-NULL, then CLEANUP must
   also be non-NULL, and with *CLEANUP initialized to NULL.  Upon
   return, if *CLEANUP is no longer NULL, it will be an expression
   that should be pushed as a cleanup after the returned expression
   is used to initialize DECL.

   Return the converted expression.  */

tree
initialize_reference (tree type, tree expr, tree decl, tree *cleanup)
{
  conversion *conv;
  void *p;

  if (type == error_mark_node || error_operand_p (expr))
    return error_mark_node;

  /* Get the high-water mark for the CONVERSION_OBSTACK.  */
  p = conversion_obstack_alloc (0);

  conv = reference_binding (type, TREE_TYPE (expr), expr, /*c_cast_p=*/false,
			    LOOKUP_NORMAL);
  if (!conv || conv->bad_p)
    {
      if (!(TYPE_QUALS (TREE_TYPE (type)) & TYPE_QUAL_CONST)
	  && !real_lvalue_p (expr))
	error ("invalid initialization of non-const reference of "
	       "type %qT from a temporary of type %qT",
	       type, TREE_TYPE (expr));
      else
	error ("invalid initialization of reference of type "
	       "%qT from expression of type %qT", type,
	       TREE_TYPE (expr));
      return error_mark_node;
    }

  /* If DECL is non-NULL, then this special rule applies:

       [class.temporary]

       The temporary to which the reference is bound or the temporary
       that is the complete object to which the reference is bound
       persists for the lifetime of the reference.

       The temporaries created during the evaluation of the expression
       initializing the reference, except the temporary to which the
       reference is bound, are destroyed at the end of the
       full-expression in which they are created.

     In that case, we store the converted expression into a new
     VAR_DECL in a new scope.

     However, we want to be careful not to create temporaries when
     they are not required.  For example, given:

       struct B {};
       struct D : public B {};
       D f();
       const B& b = f();

     there is no need to copy the return value from "f"; we can just
     extend its lifetime.  Similarly, given:

       struct S {};
       struct T { operator S(); };
       T t;
       const S& s = t;

    we can extend the lifetime of the return value of the conversion
    operator.  */
  gcc_assert (conv->kind == ck_ref_bind);
  if (decl)
    {
      tree var;
      tree base_conv_type;

      /* Skip over the REF_BIND.  */
      conv = conv->u.next;
      /* If the next conversion is a BASE_CONV, skip that too -- but
	 remember that the conversion was required.  */
      if (conv->kind == ck_base)
	{
	  if (conv->check_copy_constructor_p)
	    check_constructor_callable (TREE_TYPE (expr), expr);
	  base_conv_type = conv->type;
	  conv = conv->u.next;
	}
      else
	base_conv_type = NULL_TREE;
      /* Perform the remainder of the conversion.  */
      expr = convert_like_real (conv, expr,
				/*fn=*/NULL_TREE, /*argnum=*/0,
				/*inner=*/-1,
				/*issue_conversion_warnings=*/true,
				/*c_cast_p=*/false);
      if (error_operand_p (expr))
	expr = error_mark_node;
      else
	{
	  if (!real_lvalue_p (expr))
	    {
	      tree init;
	      tree type;

	      /* Create the temporary variable.  */
	      type = TREE_TYPE (expr);
	      var = make_temporary_var_for_ref_to_temp (decl, type);
	      layout_decl (var, 0);
	      /* If the rvalue is the result of a function call it will be
		 a TARGET_EXPR.  If it is some other construct (such as a
		 member access expression where the underlying object is
		 itself the result of a function call), turn it into a
		 TARGET_EXPR here.  It is important that EXPR be a
		 TARGET_EXPR below since otherwise the INIT_EXPR will
		 attempt to make a bitwise copy of EXPR to initialize
		 VAR.  */
	      if (TREE_CODE (expr) != TARGET_EXPR)
		expr = get_target_expr (expr);
	      /* Create the INIT_EXPR that will initialize the temporary
		 variable.  */
	      init = build2 (INIT_EXPR, type, var, expr);
	      if (at_function_scope_p ())
		{
		  add_decl_expr (var);
		  *cleanup = cxx_maybe_build_cleanup (var);

		  /* We must be careful to destroy the temporary only
		     after its initialization has taken place.  If the
		     initialization throws an exception, then the
		     destructor should not be run.  We cannot simply
		     transform INIT into something like:

			 (INIT, ({ CLEANUP_STMT; }))

		     because emit_local_var always treats the
		     initializer as a full-expression.  Thus, the
		     destructor would run too early; it would run at the
		     end of initializing the reference variable, rather
		     than at the end of the block enclosing the
		     reference variable.

		     The solution is to pass back a cleanup expression
		     which the caller is responsible for attaching to
		     the statement tree.  */
		}
	      else
		{
		  rest_of_decl_compilation (var, /*toplev=*/1, at_eof);
		  if (TYPE_HAS_NONTRIVIAL_DESTRUCTOR (type))
		    static_aggregates = tree_cons (NULL_TREE, var,
						   static_aggregates);
		}
	      /* Use its address to initialize the reference variable.  */
	      expr = build_address (var);
	      if (base_conv_type)
		expr = convert_to_base (expr,
					build_pointer_type (base_conv_type),
					/*check_access=*/true,
					/*nonnull=*/true);
	      expr = build2 (COMPOUND_EXPR, TREE_TYPE (expr), init, expr);
	    }
	  else
	    /* Take the address of EXPR.  */
	    expr = build_unary_op (ADDR_EXPR, expr, 0);
	  /* If a BASE_CONV was required, perform it now.  */
	  if (base_conv_type)
	    expr = (perform_implicit_conversion
		    (build_pointer_type (base_conv_type), expr));
	  expr = build_nop (type, expr);
	}
    }
  else
    /* Perform the conversion.  */
    expr = convert_like (conv, expr);

  /* Free all the conversions we allocated.  */
  obstack_free (&conversion_obstack, p);

  return expr;
}

#include "gt-cp-call.h"
