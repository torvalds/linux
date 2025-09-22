/* Process declarations and variables for C++ compiler.
   Copyright (C) 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005  Free Software Foundation, Inc.
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


/* Process declarations and symbol lookup for C++ front end.
   Also constructs types; the standard scalar types at initialization,
   and structure, union, array and enum types when they are declared.  */

/* ??? not all decl nodes are given the most useful possible
   line numbers.  For example, the CONST_DECLs for enum values.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "expr.h"
#include "flags.h"
#include "cp-tree.h"
#include "decl.h"
#include "output.h"
#include "except.h"
#include "toplev.h"
#include "timevar.h"
#include "cpplib.h"
#include "target.h"
#include "c-common.h"
#include "tree-mudflap.h"
#include "cgraph.h"
#include "tree-inline.h"
#include "c-pragma.h"
#include "tree-dump.h"
#include "intl.h"

extern cpp_reader *parse_in;

/* This structure contains information about the initializations
   and/or destructions required for a particular priority level.  */
typedef struct priority_info_s {
  /* Nonzero if there have been any initializations at this priority
     throughout the translation unit.  */
  int initializations_p;
  /* Nonzero if there have been any destructions at this priority
     throughout the translation unit.  */
  int destructions_p;
} *priority_info;

static void mark_vtable_entries (tree);
static bool maybe_emit_vtables (tree);
static bool acceptable_java_type (tree);
static tree start_objects (int, int);
static void finish_objects (int, int, tree);
static tree start_static_storage_duration_function (unsigned);
static void finish_static_storage_duration_function (tree);
static priority_info get_priority_info (int);
static void do_static_initialization_or_destruction (tree, bool);
static void one_static_initialization_or_destruction (tree, tree, bool);
static void generate_ctor_or_dtor_function (bool, int, location_t *);
static int generate_ctor_and_dtor_functions_for_priority (splay_tree_node,
							  void *);
static tree prune_vars_needing_no_initialization (tree *);
static void write_out_vars (tree);
static void import_export_class (tree);
static tree get_guard_bits (tree);
static void determine_visibility_from_class (tree, tree);

/* A list of static class variables.  This is needed, because a
   static class variable can be declared inside the class without
   an initializer, and then initialized, statically, outside the class.  */
static GTY(()) VEC(tree,gc) *pending_statics;

/* A list of functions which were declared inline, but which we
   may need to emit outline anyway.  */
static GTY(()) VEC(tree,gc) *deferred_fns;

/* Nonzero if we're done parsing and into end-of-file activities.  */

int at_eof;

/* Functions called along with real static constructors and destructors.  */

tree static_ctors;
tree static_dtors;



/* Return a member function type (a METHOD_TYPE), given FNTYPE (a
   FUNCTION_TYPE), CTYPE (class type), and QUALS (the cv-qualifiers
   that apply to the function).  */

tree
build_memfn_type (tree fntype, tree ctype, cp_cv_quals quals)
{
  tree raises;
  int type_quals;

  if (fntype == error_mark_node || ctype == error_mark_node)
    return error_mark_node;

  type_quals = quals & ~TYPE_QUAL_RESTRICT;
  ctype = cp_build_qualified_type (ctype, type_quals);
  fntype = build_method_type_directly (ctype, TREE_TYPE (fntype),
				       (TREE_CODE (fntype) == METHOD_TYPE
					? TREE_CHAIN (TYPE_ARG_TYPES (fntype))
					: TYPE_ARG_TYPES (fntype)));
  raises = TYPE_RAISES_EXCEPTIONS (fntype);
  if (raises)
    fntype = build_exception_variant (fntype, raises);

  return fntype;
}

/* Build a PARM_DECL with NAME and TYPE, and set DECL_ARG_TYPE
   appropriately.  */

tree
cp_build_parm_decl (tree name, tree type)
{
  tree parm = build_decl (PARM_DECL, name, type);
  /* DECL_ARG_TYPE is only used by the back end and the back end never
     sees templates.  */
  if (!processing_template_decl)
    DECL_ARG_TYPE (parm) = type_passed_as (type);
  return parm;
}

/* Returns a PARM_DECL for a parameter of the indicated TYPE, with the
   indicated NAME.  */

tree
build_artificial_parm (tree name, tree type)
{
  tree parm = cp_build_parm_decl (name, type);
  DECL_ARTIFICIAL (parm) = 1;
  /* All our artificial parms are implicitly `const'; they cannot be
     assigned to.  */
  TREE_READONLY (parm) = 1;
  return parm;
}

/* Constructors for types with virtual baseclasses need an "in-charge" flag
   saying whether this constructor is responsible for initialization of
   virtual baseclasses or not.  All destructors also need this "in-charge"
   flag, which additionally determines whether or not the destructor should
   free the memory for the object.

   This function adds the "in-charge" flag to member function FN if
   appropriate.  It is called from grokclassfn and tsubst.
   FN must be either a constructor or destructor.

   The in-charge flag follows the 'this' parameter, and is followed by the
   VTT parm (if any), then the user-written parms.  */

void
maybe_retrofit_in_chrg (tree fn)
{
  tree basetype, arg_types, parms, parm, fntype;

  /* If we've already add the in-charge parameter don't do it again.  */
  if (DECL_HAS_IN_CHARGE_PARM_P (fn))
    return;

  /* When processing templates we can't know, in general, whether or
     not we're going to have virtual baseclasses.  */
  if (processing_template_decl)
    return;

  /* We don't need an in-charge parameter for constructors that don't
     have virtual bases.  */
  if (DECL_CONSTRUCTOR_P (fn)
      && !CLASSTYPE_VBASECLASSES (DECL_CONTEXT (fn)))
    return;

  arg_types = TYPE_ARG_TYPES (TREE_TYPE (fn));
  basetype = TREE_TYPE (TREE_VALUE (arg_types));
  arg_types = TREE_CHAIN (arg_types);

  parms = TREE_CHAIN (DECL_ARGUMENTS (fn));

  /* If this is a subobject constructor or destructor, our caller will
     pass us a pointer to our VTT.  */
  if (CLASSTYPE_VBASECLASSES (DECL_CONTEXT (fn)))
    {
      parm = build_artificial_parm (vtt_parm_identifier, vtt_parm_type);

      /* First add it to DECL_ARGUMENTS between 'this' and the real args...  */
      TREE_CHAIN (parm) = parms;
      parms = parm;

      /* ...and then to TYPE_ARG_TYPES.  */
      arg_types = hash_tree_chain (vtt_parm_type, arg_types);

      DECL_HAS_VTT_PARM_P (fn) = 1;
    }

  /* Then add the in-charge parm (before the VTT parm).  */
  parm = build_artificial_parm (in_charge_identifier, integer_type_node);
  TREE_CHAIN (parm) = parms;
  parms = parm;
  arg_types = hash_tree_chain (integer_type_node, arg_types);

  /* Insert our new parameter(s) into the list.  */
  TREE_CHAIN (DECL_ARGUMENTS (fn)) = parms;

  /* And rebuild the function type.  */
  fntype = build_method_type_directly (basetype, TREE_TYPE (TREE_TYPE (fn)),
				       arg_types);
  if (TYPE_RAISES_EXCEPTIONS (TREE_TYPE (fn)))
    fntype = build_exception_variant (fntype,
				      TYPE_RAISES_EXCEPTIONS (TREE_TYPE (fn)));
  TREE_TYPE (fn) = fntype;

  /* Now we've got the in-charge parameter.  */
  DECL_HAS_IN_CHARGE_PARM_P (fn) = 1;
}

/* Classes overload their constituent function names automatically.
   When a function name is declared in a record structure,
   its name is changed to it overloaded name.  Since names for
   constructors and destructors can conflict, we place a leading
   '$' for destructors.

   CNAME is the name of the class we are grokking for.

   FUNCTION is a FUNCTION_DECL.  It was created by `grokdeclarator'.

   FLAGS contains bits saying what's special about today's
   arguments.  1 == DESTRUCTOR.  2 == OPERATOR.

   If FUNCTION is a destructor, then we must add the `auto-delete' field
   as a second parameter.  There is some hair associated with the fact
   that we must "declare" this variable in the manner consistent with the
   way the rest of the arguments were declared.

   QUALS are the qualifiers for the this pointer.  */

void
grokclassfn (tree ctype, tree function, enum overload_flags flags)
{
  tree fn_name = DECL_NAME (function);

  /* Even within an `extern "C"' block, members get C++ linkage.  See
     [dcl.link] for details.  */
  SET_DECL_LANGUAGE (function, lang_cplusplus);

  if (fn_name == NULL_TREE)
    {
      error ("name missing for member function");
      fn_name = get_identifier ("<anonymous>");
      DECL_NAME (function) = fn_name;
    }

  DECL_CONTEXT (function) = ctype;

  if (flags == DTOR_FLAG)
    DECL_DESTRUCTOR_P (function) = 1;

  if (flags == DTOR_FLAG || DECL_CONSTRUCTOR_P (function))
    maybe_retrofit_in_chrg (function);
}

/* Create an ARRAY_REF, checking for the user doing things backwards
   along the way.  */

tree
grok_array_decl (tree array_expr, tree index_exp)
{
  tree type;
  tree expr;
  tree orig_array_expr = array_expr;
  tree orig_index_exp = index_exp;

  if (error_operand_p (array_expr) || error_operand_p (index_exp))
    return error_mark_node;

  if (processing_template_decl)
    {
      if (type_dependent_expression_p (array_expr)
	  || type_dependent_expression_p (index_exp))
	return build_min_nt (ARRAY_REF, array_expr, index_exp,
			     NULL_TREE, NULL_TREE);
      array_expr = build_non_dependent_expr (array_expr);
      index_exp = build_non_dependent_expr (index_exp);
    }

  type = TREE_TYPE (array_expr);
  gcc_assert (type);
  type = non_reference (type);

  /* If they have an `operator[]', use that.  */
  if (IS_AGGR_TYPE (type) || IS_AGGR_TYPE (TREE_TYPE (index_exp)))
    expr = build_new_op (ARRAY_REF, LOOKUP_NORMAL,
			 array_expr, index_exp, NULL_TREE,
			 /*overloaded_p=*/NULL);
  else
    {
      tree p1, p2, i1, i2;

      /* Otherwise, create an ARRAY_REF for a pointer or array type.
	 It is a little-known fact that, if `a' is an array and `i' is
	 an int, you can write `i[a]', which means the same thing as
	 `a[i]'.  */
      if (TREE_CODE (type) == ARRAY_TYPE)
	p1 = array_expr;
      else
	p1 = build_expr_type_conversion (WANT_POINTER, array_expr, false);

      if (TREE_CODE (TREE_TYPE (index_exp)) == ARRAY_TYPE)
	p2 = index_exp;
      else
	p2 = build_expr_type_conversion (WANT_POINTER, index_exp, false);

      i1 = build_expr_type_conversion (WANT_INT | WANT_ENUM, array_expr,
				       false);
      i2 = build_expr_type_conversion (WANT_INT | WANT_ENUM, index_exp,
				       false);

      if ((p1 && i2) && (i1 && p2))
	error ("ambiguous conversion for array subscript");

      if (p1 && i2)
	array_expr = p1, index_exp = i2;
      else if (i1 && p2)
	array_expr = p2, index_exp = i1;
      else
	{
	  error ("invalid types %<%T[%T]%> for array subscript",
		 type, TREE_TYPE (index_exp));
	  return error_mark_node;
	}

      if (array_expr == error_mark_node || index_exp == error_mark_node)
	error ("ambiguous conversion for array subscript");

      expr = build_array_ref (array_expr, index_exp);
    }
  if (processing_template_decl && expr != error_mark_node)
    return build_min_non_dep (ARRAY_REF, expr, orig_array_expr, orig_index_exp,
			      NULL_TREE, NULL_TREE);
  return expr;
}

/* Given the cast expression EXP, checking out its validity.   Either return
   an error_mark_node if there was an unavoidable error, return a cast to
   void for trying to delete a pointer w/ the value 0, or return the
   call to delete.  If DOING_VEC is true, we handle things differently
   for doing an array delete.
   Implements ARM $5.3.4.  This is called from the parser.  */

tree
delete_sanity (tree exp, tree size, bool doing_vec, int use_global_delete)
{
  tree t, type;

  if (exp == error_mark_node)
    return exp;

  if (processing_template_decl)
    {
      t = build_min (DELETE_EXPR, void_type_node, exp, size);
      DELETE_EXPR_USE_GLOBAL (t) = use_global_delete;
      DELETE_EXPR_USE_VEC (t) = doing_vec;
      TREE_SIDE_EFFECTS (t) = 1;
      return t;
    }

  /* An array can't have been allocated by new, so complain.  */
  if (TREE_CODE (exp) == VAR_DECL
      && TREE_CODE (TREE_TYPE (exp)) == ARRAY_TYPE)
    warning (0, "deleting array %q#D", exp);

  t = build_expr_type_conversion (WANT_POINTER, exp, true);

  if (t == NULL_TREE || t == error_mark_node)
    {
      error ("type %q#T argument given to %<delete%>, expected pointer",
	     TREE_TYPE (exp));
      return error_mark_node;
    }

  type = TREE_TYPE (t);

  /* As of Valley Forge, you can delete a pointer to const.  */

  /* You can't delete functions.  */
  if (TREE_CODE (TREE_TYPE (type)) == FUNCTION_TYPE)
    {
      error ("cannot delete a function.  Only pointer-to-objects are "
	     "valid arguments to %<delete%>");
      return error_mark_node;
    }

  /* Deleting ptr to void is undefined behavior [expr.delete/3].  */
  if (TREE_CODE (TREE_TYPE (type)) == VOID_TYPE)
    {
      warning (0, "deleting %qT is undefined", type);
      doing_vec = 0;
    }

  /* Deleting a pointer with the value zero is valid and has no effect.  */
  if (integer_zerop (t))
    return build1 (NOP_EXPR, void_type_node, t);

  if (doing_vec)
    return build_vec_delete (t, /*maxindex=*/NULL_TREE,
			     sfk_deleting_destructor,
			     use_global_delete);
  else
    return build_delete (type, t, sfk_deleting_destructor,
			 LOOKUP_NORMAL, use_global_delete);
}

/* Report an error if the indicated template declaration is not the
   sort of thing that should be a member template.  */

void
check_member_template (tree tmpl)
{
  tree decl;

  gcc_assert (TREE_CODE (tmpl) == TEMPLATE_DECL);
  decl = DECL_TEMPLATE_RESULT (tmpl);

  if (TREE_CODE (decl) == FUNCTION_DECL
      || (TREE_CODE (decl) == TYPE_DECL
	  && IS_AGGR_TYPE (TREE_TYPE (decl))))
    {
      /* The parser rejects template declarations in local classes.  */
      gcc_assert (!current_function_decl);
      /* The parser rejects any use of virtual in a function template.  */
      gcc_assert (!(TREE_CODE (decl) == FUNCTION_DECL
		    && DECL_VIRTUAL_P (decl)));

      /* The debug-information generating code doesn't know what to do
	 with member templates.  */
      DECL_IGNORED_P (tmpl) = 1;
    }
  else
    error ("template declaration of %q#D", decl);
}

/* Return true iff TYPE is a valid Java parameter or return type.  */

static bool
acceptable_java_type (tree type)
{
  if (type == error_mark_node)
    return false;

  if (TREE_CODE (type) == VOID_TYPE || TYPE_FOR_JAVA (type))
    return true;
  if (TREE_CODE (type) == POINTER_TYPE || TREE_CODE (type) == REFERENCE_TYPE)
    {
      type = TREE_TYPE (type);
      if (TREE_CODE (type) == RECORD_TYPE)
	{
	  tree args;  int i;
	  if (! TYPE_FOR_JAVA (type))
	    return false;
	  if (! CLASSTYPE_TEMPLATE_INFO (type))
	    return true;
	  args = CLASSTYPE_TI_ARGS (type);
	  i = TREE_VEC_LENGTH (args);
	  while (--i >= 0)
	    {
	      type = TREE_VEC_ELT (args, i);
	      if (TREE_CODE (type) == POINTER_TYPE)
		type = TREE_TYPE (type);
	      if (! TYPE_FOR_JAVA (type))
		return false;
	    }
	  return true;
	}
    }
  return false;
}

/* For a METHOD in a Java class CTYPE, return true if
   the parameter and return types are valid Java types.
   Otherwise, print appropriate error messages, and return false.  */

bool
check_java_method (tree method)
{
  bool jerr = false;
  tree arg_types = TYPE_ARG_TYPES (TREE_TYPE (method));
  tree ret_type = TREE_TYPE (TREE_TYPE (method));

  if (!acceptable_java_type (ret_type))
    {
      error ("Java method %qD has non-Java return type %qT",
	     method, ret_type);
      jerr = true;
    }

  arg_types = TREE_CHAIN (arg_types);
  if (DECL_HAS_IN_CHARGE_PARM_P (method))
    arg_types = TREE_CHAIN (arg_types);
  if (DECL_HAS_VTT_PARM_P (method))
    arg_types = TREE_CHAIN (arg_types);

  for (; arg_types != NULL_TREE; arg_types = TREE_CHAIN (arg_types))
    {
      tree type = TREE_VALUE (arg_types);
      if (!acceptable_java_type (type))
	{
          if (type != error_mark_node)
	    error ("Java method %qD has non-Java parameter type %qT",
		   method, type);
	  jerr = true;
	}
    }
  return !jerr;
}

/* Sanity check: report error if this function FUNCTION is not
   really a member of the class (CTYPE) it is supposed to belong to.
   TEMPLATE_PARMS is used to specify the template parameters of a member
   template passed as FUNCTION_DECL. If the member template is passed as a
   TEMPLATE_DECL, it can be NULL since the parameters can be extracted
   from the declaration. If the function is not a function template, it
   must be NULL.
   It returns the original declaration for the function, or NULL_TREE
   if no declaration was found (and an error was emitted).  */

tree
check_classfn (tree ctype, tree function, tree template_parms)
{
  int ix;
  bool is_template;
  tree pushed_scope;
  
  if (DECL_USE_TEMPLATE (function)
      && !(TREE_CODE (function) == TEMPLATE_DECL
	   && DECL_TEMPLATE_SPECIALIZATION (function))
      && DECL_MEMBER_TEMPLATE_P (DECL_TI_TEMPLATE (function)))
    /* Since this is a specialization of a member template,
       we're not going to find the declaration in the class.
       For example, in:

	 struct S { template <typename T> void f(T); };
	 template <> void S::f(int);

       we're not going to find `S::f(int)', but there's no
       reason we should, either.  We let our callers know we didn't
       find the method, but we don't complain.  */
    return NULL_TREE;

  /* Basic sanity check: for a template function, the template parameters
     either were not passed, or they are the same of DECL_TEMPLATE_PARMS.  */
  if (TREE_CODE (function) == TEMPLATE_DECL)
    {
      gcc_assert (!template_parms
		  || comp_template_parms (template_parms,
					  DECL_TEMPLATE_PARMS (function)));
      template_parms = DECL_TEMPLATE_PARMS (function);
    }

  /* OK, is this a definition of a member template?  */
  is_template = (template_parms != NULL_TREE);

  /* We must enter the scope here, because conversion operators are
     named by target type, and type equivalence relies on typenames
     resolving within the scope of CTYPE.  */
  pushed_scope = push_scope (ctype);
  ix = class_method_index_for_fn (complete_type (ctype), function);
  if (ix >= 0)
    {
      VEC(tree,gc) *methods = CLASSTYPE_METHOD_VEC (ctype);
      tree fndecls, fndecl = 0;
      bool is_conv_op;
      const char *format = NULL;

      for (fndecls = VEC_index (tree, methods, ix);
	   fndecls; fndecls = OVL_NEXT (fndecls))
	{
	  tree p1, p2;

	  fndecl = OVL_CURRENT (fndecls);
	  p1 = TYPE_ARG_TYPES (TREE_TYPE (function));
	  p2 = TYPE_ARG_TYPES (TREE_TYPE (fndecl));

	  /* We cannot simply call decls_match because this doesn't
	     work for static member functions that are pretending to
	     be methods, and because the name may have been changed by
	     asm("new_name").  */

	   /* Get rid of the this parameter on functions that become
	      static.  */
	  if (DECL_STATIC_FUNCTION_P (fndecl)
	      && TREE_CODE (TREE_TYPE (function)) == METHOD_TYPE)
	    p1 = TREE_CHAIN (p1);

	  /* A member template definition only matches a member template
	     declaration.  */
	  if (is_template != (TREE_CODE (fndecl) == TEMPLATE_DECL))
	    continue;

	  if (same_type_p (TREE_TYPE (TREE_TYPE (function)),
			   TREE_TYPE (TREE_TYPE (fndecl)))
	      && compparms (p1, p2)
	      && (!is_template
		  || comp_template_parms (template_parms,
					  DECL_TEMPLATE_PARMS (fndecl)))
	      && (DECL_TEMPLATE_SPECIALIZATION (function)
		  == DECL_TEMPLATE_SPECIALIZATION (fndecl))
	      && (!DECL_TEMPLATE_SPECIALIZATION (function)
		  || (DECL_TI_TEMPLATE (function)
		      == DECL_TI_TEMPLATE (fndecl))))
	    break;
	}
      if (fndecls)
	{
	  if (pushed_scope)
	    pop_scope (pushed_scope);
	  return OVL_CURRENT (fndecls);
	}
      
      error ("prototype for %q#D does not match any in class %qT",
	     function, ctype);
      is_conv_op = DECL_CONV_FN_P (fndecl);

      if (is_conv_op)
	ix = CLASSTYPE_FIRST_CONVERSION_SLOT;
      fndecls = VEC_index (tree, methods, ix);
      while (fndecls)
	{
	  fndecl = OVL_CURRENT (fndecls);
	  fndecls = OVL_NEXT (fndecls);

	  if (!fndecls && is_conv_op)
	    {
	      if (VEC_length (tree, methods) > (size_t) ++ix)
		{
		  fndecls = VEC_index (tree, methods, ix);
		  if (!DECL_CONV_FN_P (OVL_CURRENT (fndecls)))
		    {
		      fndecls = NULL_TREE;
		      is_conv_op = false;
		    }
		}
	      else
		is_conv_op = false;
	    }
	  if (format)
	    format = "                %+#D";
	  else if (fndecls)
	    format = N_("candidates are: %+#D");
	  else
	    format = N_("candidate is: %+#D");
	  error (format, fndecl);
	}
    }
  else if (!COMPLETE_TYPE_P (ctype))
    cxx_incomplete_type_error (function, ctype);
  else
    error ("no %q#D member function declared in class %qT",
	   function, ctype);

  /* If we did not find the method in the class, add it to avoid
     spurious errors (unless the CTYPE is not yet defined, in which
     case we'll only confuse ourselves when the function is declared
     properly within the class.  */
  if (COMPLETE_TYPE_P (ctype))
    add_method (ctype, function, NULL_TREE);
  
  if (pushed_scope)
    pop_scope (pushed_scope);
  return NULL_TREE;
}

/* DECL is a function with vague linkage.  Remember it so that at the
   end of the translation unit we can decide whether or not to emit
   it.  */

void
note_vague_linkage_fn (tree decl)
{
  if (!DECL_DEFERRED_FN (decl))
    {
      DECL_DEFERRED_FN (decl) = 1;
      DECL_DEFER_OUTPUT (decl) = 1;
      VEC_safe_push (tree, gc, deferred_fns, decl);
    }
}

/* We have just processed the DECL, which is a static data member.
   The other parameters are as for cp_finish_decl.  */

void
finish_static_data_member_decl (tree decl,
				tree init, bool init_const_expr_p,
				tree asmspec_tree,
				int flags)
{
  DECL_CONTEXT (decl) = current_class_type;

  /* We cannot call pushdecl here, because that would fill in the
     TREE_CHAIN of our decl.  Instead, we modify cp_finish_decl to do
     the right thing, namely, to put this decl out straight away.  */

  if (! processing_template_decl)
    VEC_safe_push (tree, gc, pending_statics, decl);

  if (LOCAL_CLASS_P (current_class_type))
    pedwarn ("local class %q#T shall not have static data member %q#D",
	     current_class_type, decl);

  /* Static consts need not be initialized in the class definition.  */
  if (init != NULL_TREE && TYPE_NEEDS_CONSTRUCTING (TREE_TYPE (decl)))
    {
      static int explained = 0;

      error ("initializer invalid for static member with constructor");
      if (!explained)
	{
	  error ("(an out of class initialization is required)");
	  explained = 1;
	}
      init = NULL_TREE;
    }
  /* Force the compiler to know when an uninitialized static const
     member is being used.  */
  if (CP_TYPE_CONST_P (TREE_TYPE (decl)) && init == 0)
    TREE_USED (decl) = 1;
  DECL_INITIAL (decl) = init;
  DECL_IN_AGGR_P (decl) = 1;

  cp_finish_decl (decl, init, init_const_expr_p, asmspec_tree, flags);
}

/* DECLARATOR and DECLSPECS correspond to a class member.  The other
   parameters are as for cp_finish_decl.  Return the DECL for the
   class member declared.  */

tree
grokfield (const cp_declarator *declarator,
	   cp_decl_specifier_seq *declspecs,
	   tree init, bool init_const_expr_p,
	   tree asmspec_tree,
	   tree attrlist)
{
  tree value;
  const char *asmspec = 0;
  int flags = LOOKUP_ONLYCONVERTING;

  if (init
      && TREE_CODE (init) == TREE_LIST
      && TREE_VALUE (init) == error_mark_node
      && TREE_CHAIN (init) == NULL_TREE)
    init = NULL_TREE;

  value = grokdeclarator (declarator, declspecs, FIELD, init != 0, &attrlist);
  if (! value || error_operand_p (value))
    /* friend or constructor went bad.  */
    return error_mark_node;

  if (TREE_CODE (value) == TYPE_DECL && init)
    {
      error ("typedef %qD is initialized (use __typeof__ instead)", value);
      init = NULL_TREE;
    }

  /* Pass friendly classes back.  */
  if (value == void_type_node)
    return value;

  /* Pass friend decls back.  */
  if ((TREE_CODE (value) == FUNCTION_DECL
       || TREE_CODE (value) == TEMPLATE_DECL)
      && DECL_CONTEXT (value) != current_class_type)
    return value;

  if (DECL_NAME (value) != NULL_TREE
      && IDENTIFIER_POINTER (DECL_NAME (value))[0] == '_'
      && ! strcmp (IDENTIFIER_POINTER (DECL_NAME (value)), "_vptr"))
    error ("member %qD conflicts with virtual function table field name",
	   value);

  /* Stash away type declarations.  */
  if (TREE_CODE (value) == TYPE_DECL)
    {
      DECL_NONLOCAL (value) = 1;
      DECL_CONTEXT (value) = current_class_type;

      if (processing_template_decl)
	value = push_template_decl (value);

      if (attrlist)
	{
	  /* Avoid storing attributes in template parameters:
	     tsubst is not ready to handle them.  */
	  tree type = TREE_TYPE (value);
	  if (TREE_CODE (type) == TEMPLATE_TYPE_PARM
	      || TREE_CODE (type) == BOUND_TEMPLATE_TEMPLATE_PARM)
	    sorry ("applying attributes to template parameters is not implemented");
	  else
	    cplus_decl_attributes (&value, attrlist, 0);
	}

      return value;
    }

  if (DECL_IN_AGGR_P (value))
    {
      error ("%qD is already defined in %qT", value, DECL_CONTEXT (value));
      return void_type_node;
    }

  if (asmspec_tree && asmspec_tree != error_mark_node)
    asmspec = TREE_STRING_POINTER (asmspec_tree);

  if (init)
    {
      if (TREE_CODE (value) == FUNCTION_DECL)
	{
	  /* Initializers for functions are rejected early in the parser.
	     If we get here, it must be a pure specifier for a method.  */
	  if (TREE_CODE (TREE_TYPE (value)) == METHOD_TYPE)
	    {
	      gcc_assert (error_operand_p (init) || integer_zerop (init));
	      DECL_PURE_VIRTUAL_P (value) = 1;
	    }
	  else
	    {
	      gcc_assert (TREE_CODE (TREE_TYPE (value)) == FUNCTION_TYPE);
	      error ("initializer specified for static member function %qD",
		     value);
	    }
	}
      else if (pedantic && TREE_CODE (value) != VAR_DECL)
	/* Already complained in grokdeclarator.  */
	init = NULL_TREE;
      else if (!processing_template_decl)
	{
	  if (TREE_CODE (init) == CONSTRUCTOR)
	    init = digest_init (TREE_TYPE (value), init);
	  else
	    init = integral_constant_value (init);

	  if (init != error_mark_node && !TREE_CONSTANT (init))
	    {
	      /* We can allow references to things that are effectively
		 static, since references are initialized with the
		 address.  */
	      if (TREE_CODE (TREE_TYPE (value)) != REFERENCE_TYPE
		  || (TREE_STATIC (init) == 0
		      && (!DECL_P (init) || DECL_EXTERNAL (init) == 0)))
		{
		  error ("field initializer is not constant");
		  init = error_mark_node;
		}
	    }
	}
    }

  if (processing_template_decl
      && (TREE_CODE (value) == VAR_DECL || TREE_CODE (value) == FUNCTION_DECL))
    {
      value = push_template_decl (value);
      if (error_operand_p (value))
	return error_mark_node;
    }

  if (attrlist)
    cplus_decl_attributes (&value, attrlist, 0);

  switch (TREE_CODE (value))
    {
    case VAR_DECL:
      finish_static_data_member_decl (value, init, init_const_expr_p,
				      asmspec_tree, flags);
      return value;

    case FIELD_DECL:
      if (asmspec)
	error ("%<asm%> specifiers are not permitted on non-static data members");
      if (DECL_INITIAL (value) == error_mark_node)
	init = error_mark_node;
      cp_finish_decl (value, init, /*init_const_expr_p=*/false,
		      NULL_TREE, flags);
      DECL_INITIAL (value) = init;
      DECL_IN_AGGR_P (value) = 1;
      return value;

    case  FUNCTION_DECL:
      if (asmspec)
	set_user_assembler_name (value, asmspec);

      cp_finish_decl (value,
		      /*init=*/NULL_TREE,
		      /*init_const_expr_p=*/false,
		      asmspec_tree, flags);

      /* Pass friends back this way.  */
      if (DECL_FRIEND_P (value))
	return void_type_node;

      DECL_IN_AGGR_P (value) = 1;
      return value;

    default:
      gcc_unreachable ();
    }
  return NULL_TREE;
}

/* Like `grokfield', but for bitfields.
   WIDTH is non-NULL for bit fields only, and is an INTEGER_CST node.  */

tree
grokbitfield (const cp_declarator *declarator,
	      cp_decl_specifier_seq *declspecs, tree width)
{
  tree value = grokdeclarator (declarator, declspecs, BITFIELD, 0, NULL);

  if (value == error_mark_node) 
    return NULL_TREE; /* friends went bad.  */

  /* Pass friendly classes back.  */
  if (TREE_CODE (value) == VOID_TYPE)
    return void_type_node;

  if (!INTEGRAL_TYPE_P (TREE_TYPE (value))
      && (POINTER_TYPE_P (value)
          || !dependent_type_p (TREE_TYPE (value))))
    {
      error ("bit-field %qD with non-integral type", value);
      return error_mark_node;
    }

  if (TREE_CODE (value) == TYPE_DECL)
    {
      error ("cannot declare %qD to be a bit-field type", value);
      return NULL_TREE;
    }

  /* Usually, finish_struct_1 catches bitfields with invalid types.
     But, in the case of bitfields with function type, we confuse
     ourselves into thinking they are member functions, so we must
     check here.  */
  if (TREE_CODE (value) == FUNCTION_DECL)
    {
      error ("cannot declare bit-field %qD with function type",
	     DECL_NAME (value));
      return NULL_TREE;
    }

  if (DECL_IN_AGGR_P (value))
    {
      error ("%qD is already defined in the class %qT", value,
	     DECL_CONTEXT (value));
      return void_type_node;
    }

  if (TREE_STATIC (value))
    {
      error ("static member %qD cannot be a bit-field", value);
      return NULL_TREE;
    }
  finish_decl (value, NULL_TREE, NULL_TREE);

  if (width != error_mark_node)
    {
      constant_expression_warning (width);
      DECL_INITIAL (value) = width;
      SET_DECL_C_BIT_FIELD (value);
    }

  DECL_IN_AGGR_P (value) = 1;
  return value;
}


void
cplus_decl_attributes (tree *decl, tree attributes, int flags)
{
  if (*decl == NULL_TREE || *decl == void_type_node
      || *decl == error_mark_node)
    return;

  if (TREE_CODE (*decl) == TEMPLATE_DECL)
    decl = &DECL_TEMPLATE_RESULT (*decl);

  decl_attributes (decl, attributes, flags);

  if (TREE_CODE (*decl) == TYPE_DECL)
    SET_IDENTIFIER_TYPE_VALUE (DECL_NAME (*decl), TREE_TYPE (*decl));
}

/* Walks through the namespace- or function-scope anonymous union
   OBJECT, with the indicated TYPE, building appropriate VAR_DECLs.
   Returns one of the fields for use in the mangled name.  */

static tree
build_anon_union_vars (tree type, tree object)
{
  tree main_decl = NULL_TREE;
  tree field;

  /* Rather than write the code to handle the non-union case,
     just give an error.  */
  if (TREE_CODE (type) != UNION_TYPE)
    error ("anonymous struct not inside named type");

  for (field = TYPE_FIELDS (type);
       field != NULL_TREE;
       field = TREE_CHAIN (field))
    {
      tree decl;
      tree ref;

      if (DECL_ARTIFICIAL (field))
	continue;
      if (TREE_CODE (field) != FIELD_DECL)
	{
	  pedwarn ("%q+#D invalid; an anonymous union can only "
		   "have non-static data members", field);
	  continue;
	}

      if (TREE_PRIVATE (field))
	pedwarn ("private member %q+#D in anonymous union", field);
      else if (TREE_PROTECTED (field))
	pedwarn ("protected member %q+#D in anonymous union", field);

      if (processing_template_decl)
	ref = build_min_nt (COMPONENT_REF, object,
			    DECL_NAME (field), NULL_TREE);
      else
	ref = build_class_member_access_expr (object, field, NULL_TREE,
					      false);

      if (DECL_NAME (field))
	{
	  tree base;

	  decl = build_decl (VAR_DECL, DECL_NAME (field), TREE_TYPE (field));
	  DECL_ANON_UNION_VAR_P (decl) = 1;

	  base = get_base_address (object);
	  TREE_PUBLIC (decl) = TREE_PUBLIC (base);
	  TREE_STATIC (decl) = TREE_STATIC (base);
	  DECL_EXTERNAL (decl) = DECL_EXTERNAL (base);

	  SET_DECL_VALUE_EXPR (decl, ref);
	  DECL_HAS_VALUE_EXPR_P (decl) = 1;

	  decl = pushdecl (decl);
	}
      else if (ANON_AGGR_TYPE_P (TREE_TYPE (field)))
	decl = build_anon_union_vars (TREE_TYPE (field), ref);
      else
	decl = 0;

      if (main_decl == NULL_TREE)
	main_decl = decl;
    }

  return main_decl;
}

/* Finish off the processing of a UNION_TYPE structure.  If the union is an
   anonymous union, then all members must be laid out together.  PUBLIC_P
   is nonzero if this union is not declared static.  */

void
finish_anon_union (tree anon_union_decl)
{
  tree type;
  tree main_decl;
  bool public_p;

  if (anon_union_decl == error_mark_node)
    return;

  type = TREE_TYPE (anon_union_decl);
  public_p = TREE_PUBLIC (anon_union_decl);

  /* The VAR_DECL's context is the same as the TYPE's context.  */
  DECL_CONTEXT (anon_union_decl) = DECL_CONTEXT (TYPE_NAME (type));

  if (TYPE_FIELDS (type) == NULL_TREE)
    return;

  if (public_p)
    {
      error ("namespace-scope anonymous aggregates must be static");
      return;
    }

  main_decl = build_anon_union_vars (type, anon_union_decl);
  if (main_decl == error_mark_node)
    return;
  if (main_decl == NULL_TREE)
    {
      warning (0, "anonymous union with no members");
      return;
    }

  if (!processing_template_decl)
    {
      /* Use main_decl to set the mangled name.  */
      DECL_NAME (anon_union_decl) = DECL_NAME (main_decl);
      mangle_decl (anon_union_decl);
      DECL_NAME (anon_union_decl) = NULL_TREE;
    }

  pushdecl (anon_union_decl);
  if (building_stmt_tree ()
      && at_function_scope_p ())
    add_decl_expr (anon_union_decl);
  else if (!processing_template_decl)
    rest_of_decl_compilation (anon_union_decl,
			      toplevel_bindings_p (), at_eof);
}

/* Auxiliary functions to make type signatures for
   `operator new' and `operator delete' correspond to
   what compiler will be expecting.  */

tree
coerce_new_type (tree type)
{
  int e = 0;
  tree args = TYPE_ARG_TYPES (type);

  gcc_assert (TREE_CODE (type) == FUNCTION_TYPE);

  if (!same_type_p (TREE_TYPE (type), ptr_type_node))
    {
      e = 1;
      error ("%<operator new%> must return type %qT", ptr_type_node);
    }

  if (!args || args == void_list_node
      || !same_type_p (TREE_VALUE (args), size_type_node))
    {
      e = 2;
      if (args && args != void_list_node)
	args = TREE_CHAIN (args);
      pedwarn ("%<operator new%> takes type %<size_t%> (%qT) "
	       "as first parameter", size_type_node);
    }
  switch (e)
  {
    case 2:
      args = tree_cons (NULL_TREE, size_type_node, args);
      /* Fall through.  */
    case 1:
      type = build_exception_variant
	      (build_function_type (ptr_type_node, args),
	       TYPE_RAISES_EXCEPTIONS (type));
      /* Fall through.  */
    default:;
  }
  return type;
}

tree
coerce_delete_type (tree type)
{
  int e = 0;
  tree args = TYPE_ARG_TYPES (type);

  gcc_assert (TREE_CODE (type) == FUNCTION_TYPE);

  if (!same_type_p (TREE_TYPE (type), void_type_node))
    {
      e = 1;
      error ("%<operator delete%> must return type %qT", void_type_node);
    }

  if (!args || args == void_list_node
      || !same_type_p (TREE_VALUE (args), ptr_type_node))
    {
      e = 2;
      if (args && args != void_list_node)
	args = TREE_CHAIN (args);
      error ("%<operator delete%> takes type %qT as first parameter",
	     ptr_type_node);
    }
  switch (e)
  {
    case 2:
      args = tree_cons (NULL_TREE, ptr_type_node, args);
      /* Fall through.  */
    case 1:
      type = build_exception_variant
	      (build_function_type (void_type_node, args),
	       TYPE_RAISES_EXCEPTIONS (type));
      /* Fall through.  */
    default:;
  }

  return type;
}

static void
mark_vtable_entries (tree decl)
{
  tree fnaddr;
  unsigned HOST_WIDE_INT idx;

  FOR_EACH_CONSTRUCTOR_VALUE (CONSTRUCTOR_ELTS (DECL_INITIAL (decl)),
			      idx, fnaddr)
    {
      tree fn;

      STRIP_NOPS (fnaddr);

      if (TREE_CODE (fnaddr) != ADDR_EXPR
	  && TREE_CODE (fnaddr) != FDESC_EXPR)
	/* This entry is an offset: a virtual base class offset, a
	   virtual call offset, an RTTI offset, etc.  */
	continue;

      fn = TREE_OPERAND (fnaddr, 0);
      TREE_ADDRESSABLE (fn) = 1;
      /* When we don't have vcall offsets, we output thunks whenever
	 we output the vtables that contain them.  With vcall offsets,
	 we know all the thunks we'll need when we emit a virtual
	 function, so we emit the thunks there instead.  */
      if (DECL_THUNK_P (fn))
	use_thunk (fn, /*emit_p=*/0);
      mark_used (fn);
    }
}

/* Set DECL up to have the closest approximation of "initialized common"
   linkage available.  */

void
comdat_linkage (tree decl)
{
  if (flag_weak)
    make_decl_one_only (decl);
  else if (TREE_CODE (decl) == FUNCTION_DECL
	   || (TREE_CODE (decl) == VAR_DECL && DECL_ARTIFICIAL (decl)))
    /* We can just emit function and compiler-generated variables
       statically; having multiple copies is (for the most part) only
       a waste of space.

       There are two correctness issues, however: the address of a
       template instantiation with external linkage should be the
       same, independent of what translation unit asks for the
       address, and this will not hold when we emit multiple copies of
       the function.  However, there's little else we can do.

       Also, by default, the typeinfo implementation assumes that
       there will be only one copy of the string used as the name for
       each type.  Therefore, if weak symbols are unavailable, the
       run-time library should perform a more conservative check; it
       should perform a string comparison, rather than an address
       comparison.  */
    TREE_PUBLIC (decl) = 0;
  else
    {
      /* Static data member template instantiations, however, cannot
	 have multiple copies.  */
      if (DECL_INITIAL (decl) == 0
	  || DECL_INITIAL (decl) == error_mark_node)
	DECL_COMMON (decl) = 1;
      else if (EMPTY_CONSTRUCTOR_P (DECL_INITIAL (decl)))
	{
	  DECL_COMMON (decl) = 1;
	  DECL_INITIAL (decl) = error_mark_node;
	}
      else if (!DECL_EXPLICIT_INSTANTIATION (decl))
	{
	  /* We can't do anything useful; leave vars for explicit
	     instantiation.  */
	  DECL_EXTERNAL (decl) = 1;
	  DECL_NOT_REALLY_EXTERN (decl) = 0;
	}
    }

  if (DECL_LANG_SPECIFIC (decl))
    DECL_COMDAT (decl) = 1;
}

/* For win32 we also want to put explicit instantiations in
   linkonce sections, so that they will be merged with implicit
   instantiations; otherwise we get duplicate symbol errors.
   For Darwin we do not want explicit instantiations to be
   linkonce.  */

void
maybe_make_one_only (tree decl)
{
  /* We used to say that this was not necessary on targets that support weak
     symbols, because the implicit instantiations will defer to the explicit
     one.  However, that's not actually the case in SVR4; a strong definition
     after a weak one is an error.  Also, not making explicit
     instantiations one_only means that we can end up with two copies of
     some template instantiations.  */
  if (! flag_weak)
    return;

  /* We can't set DECL_COMDAT on functions, or cp_finish_file will think
     we can get away with not emitting them if they aren't used.  We need
     to for variables so that cp_finish_decl will update their linkage,
     because their DECL_INITIAL may not have been set properly yet.  */

  if (!TARGET_WEAK_NOT_IN_ARCHIVE_TOC
      || (! DECL_EXPLICIT_INSTANTIATION (decl)
	  && ! DECL_TEMPLATE_SPECIALIZATION (decl)))
    {
      make_decl_one_only (decl);

      if (TREE_CODE (decl) == VAR_DECL)
	{
	  DECL_COMDAT (decl) = 1;
	  /* Mark it needed so we don't forget to emit it.  */
	  mark_decl_referenced (decl);
	}
    }
}

/* Determine whether or not we want to specifically import or export CTYPE,
   using various heuristics.  */

static void
import_export_class (tree ctype)
{
  /* -1 for imported, 1 for exported.  */
  int import_export = 0;

  /* It only makes sense to call this function at EOF.  The reason is
     that this function looks at whether or not the first non-inline
     non-abstract virtual member function has been defined in this
     translation unit.  But, we can't possibly know that until we've
     seen the entire translation unit.  */
  gcc_assert (at_eof);

  if (CLASSTYPE_INTERFACE_KNOWN (ctype))
    return;

  /* If MULTIPLE_SYMBOL_SPACES is set and we saw a #pragma interface,
     we will have CLASSTYPE_INTERFACE_ONLY set but not
     CLASSTYPE_INTERFACE_KNOWN.  In that case, we don't want to use this
     heuristic because someone will supply a #pragma implementation
     elsewhere, and deducing it here would produce a conflict.  */
  if (CLASSTYPE_INTERFACE_ONLY (ctype))
    return;

  if (lookup_attribute ("dllimport", TYPE_ATTRIBUTES (ctype)))
    import_export = -1;
  else if (lookup_attribute ("dllexport", TYPE_ATTRIBUTES (ctype)))
    import_export = 1;
  else if (CLASSTYPE_IMPLICIT_INSTANTIATION (ctype)
	   && !flag_implicit_templates)
    /* For a template class, without -fimplicit-templates, check the
       repository.  If the virtual table is assigned to this
       translation unit, then export the class; otherwise, import
       it.  */
      import_export = repo_export_class_p (ctype) ? 1 : -1;
  else if (TYPE_POLYMORPHIC_P (ctype))
    {
      /* The ABI specifies that the virtual table and associated
	 information are emitted with the key method, if any.  */
      tree method = CLASSTYPE_KEY_METHOD (ctype);
      /* If weak symbol support is not available, then we must be
	 careful not to emit the vtable when the key function is
	 inline.  An inline function can be defined in multiple
	 translation units.  If we were to emit the vtable in each
	 translation unit containing a definition, we would get
	 multiple definition errors at link-time.  */
      if (method && (flag_weak || ! DECL_DECLARED_INLINE_P (method)))
	import_export = (DECL_REALLY_EXTERN (method) ? -1 : 1);
    }

  /* When MULTIPLE_SYMBOL_SPACES is set, we cannot count on seeing
     a definition anywhere else.  */
  if (MULTIPLE_SYMBOL_SPACES && import_export == -1)
    import_export = 0;

  /* Allow backends the chance to overrule the decision.  */
  if (targetm.cxx.import_export_class)
    import_export = targetm.cxx.import_export_class (ctype, import_export);

  if (import_export)
    {
      SET_CLASSTYPE_INTERFACE_KNOWN (ctype);
      CLASSTYPE_INTERFACE_ONLY (ctype) = (import_export < 0);
    }
}

/* Return true if VAR has already been provided to the back end; in that
   case VAR should not be modified further by the front end.  */
static bool
var_finalized_p (tree var)
{
  return cgraph_varpool_node (var)->finalized;
}

/* DECL is a VAR_DECL or FUNCTION_DECL which, for whatever reason,
   must be emitted in this translation unit.  Mark it as such.  */

void
mark_needed (tree decl)
{
  /* It's possible that we no longer need to set
     TREE_SYMBOL_REFERENCED here directly, but doing so is
     harmless.  */
  TREE_SYMBOL_REFERENCED (DECL_ASSEMBLER_NAME (decl)) = 1;
  mark_decl_referenced (decl);
}

/* DECL is either a FUNCTION_DECL or a VAR_DECL.  This function
   returns true if a definition of this entity should be provided in
   this object file.  Callers use this function to determine whether
   or not to let the back end know that a definition of DECL is
   available in this translation unit.  */

bool
decl_needed_p (tree decl)
{
  gcc_assert (TREE_CODE (decl) == VAR_DECL
	      || TREE_CODE (decl) == FUNCTION_DECL);
  /* This function should only be called at the end of the translation
     unit.  We cannot be sure of whether or not something will be
     COMDAT until that point.  */
  gcc_assert (at_eof);

  /* All entities with external linkage that are not COMDAT should be
     emitted; they may be referred to from other object files.  */
  if (TREE_PUBLIC (decl) && !DECL_COMDAT (decl))
    return true;
  /* If this entity was used, let the back-end see it; it will decide
     whether or not to emit it into the object file.  */
  if (TREE_USED (decl)
      || (DECL_ASSEMBLER_NAME_SET_P (decl)
	  && TREE_SYMBOL_REFERENCED (DECL_ASSEMBLER_NAME (decl))))
      return true;
  /* Otherwise, DECL does not need to be emitted -- yet.  A subsequent
     reference to DECL might cause it to be emitted later.  */
  return false;
}

/* If necessary, write out the vtables for the dynamic class CTYPE.
   Returns true if any vtables were emitted.  */

static bool
maybe_emit_vtables (tree ctype)
{
  tree vtbl;
  tree primary_vtbl;
  int needed = 0;

  /* If the vtables for this class have already been emitted there is
     nothing more to do.  */
  primary_vtbl = CLASSTYPE_VTABLES (ctype);
  if (var_finalized_p (primary_vtbl))
    return false;
  /* Ignore dummy vtables made by get_vtable_decl.  */
  if (TREE_TYPE (primary_vtbl) == void_type_node)
    return false;

  /* On some targets, we cannot determine the key method until the end
     of the translation unit -- which is when this function is
     called.  */
  if (!targetm.cxx.key_method_may_be_inline ())
    determine_key_method (ctype);

  /* See if any of the vtables are needed.  */
  for (vtbl = CLASSTYPE_VTABLES (ctype); vtbl; vtbl = TREE_CHAIN (vtbl))
    {
      import_export_decl (vtbl);
      if (DECL_NOT_REALLY_EXTERN (vtbl) && decl_needed_p (vtbl))
	needed = 1;
    }
  if (!needed)
    {
      /* If the references to this class' vtables are optimized away,
	 still emit the appropriate debugging information.  See
	 dfs_debug_mark.  */
      if (DECL_COMDAT (primary_vtbl)
	  && CLASSTYPE_DEBUG_REQUESTED (ctype))
	note_debug_info_needed (ctype);
      return false;
    }

  /* The ABI requires that we emit all of the vtables if we emit any
     of them.  */
  for (vtbl = CLASSTYPE_VTABLES (ctype); vtbl; vtbl = TREE_CHAIN (vtbl))
    {
      /* Mark entities references from the virtual table as used.  */
      mark_vtable_entries (vtbl);

      if (TREE_TYPE (DECL_INITIAL (vtbl)) == 0)
	{
	  tree expr = store_init_value (vtbl, DECL_INITIAL (vtbl));

	  /* It had better be all done at compile-time.  */
	  gcc_assert (!expr);
	}

      /* Write it out.  */
      DECL_EXTERNAL (vtbl) = 0;
      rest_of_decl_compilation (vtbl, 1, 1);

      /* Because we're only doing syntax-checking, we'll never end up
	 actually marking the variable as written.  */
      if (flag_syntax_only)
	TREE_ASM_WRITTEN (vtbl) = 1;
    }

  /* Since we're writing out the vtable here, also write the debug
     info.  */
  note_debug_info_needed (ctype);

  return true;
}

/* A special return value from type_visibility meaning internal
   linkage.  */

enum { VISIBILITY_ANON = VISIBILITY_INTERNAL+1 };

/* walk_tree helper function for type_visibility.  */

static tree
min_vis_r (tree *tp, int *walk_subtrees, void *data)
{
  int *vis_p = (int *)data;
  if (! TYPE_P (*tp))
    {
      *walk_subtrees = 0;
    }
  else if (CLASS_TYPE_P (*tp))
    {
      if (!TREE_PUBLIC (TYPE_MAIN_DECL (*tp)))
	{
	  *vis_p = VISIBILITY_ANON;
	  return *tp;
	}
      else if (CLASSTYPE_VISIBILITY (*tp) > *vis_p)
	*vis_p = CLASSTYPE_VISIBILITY (*tp);
    }
  return NULL;
}

/* Returns the visibility of TYPE, which is the minimum visibility of its
   component types.  */

static int
type_visibility (tree type)
{
  int vis = VISIBILITY_DEFAULT;
  walk_tree_without_duplicates (&type, min_vis_r, &vis);
  return vis;
}

/* Limit the visibility of DECL to VISIBILITY, if not explicitly
   specified (or if VISIBILITY is static).  */

static bool
constrain_visibility (tree decl, int visibility)
{
  if (visibility == VISIBILITY_ANON)
    {
      /* extern "C" declarations aren't affected by the anonymous
	 namespace.  */
      if (!DECL_EXTERN_C_P (decl))
	{
	  TREE_PUBLIC (decl) = 0;
	  DECL_INTERFACE_KNOWN (decl) = 1;
	  if (DECL_LANG_SPECIFIC (decl))
	    DECL_NOT_REALLY_EXTERN (decl) = 1;
	}
    }
  else if (visibility > DECL_VISIBILITY (decl)
	   && !DECL_VISIBILITY_SPECIFIED (decl))
    {
      DECL_VISIBILITY (decl) = visibility;
      return true;
    }
  return false;
}

/* Constrain the visibility of DECL based on the visibility of its template
   arguments.  */

static void
constrain_visibility_for_template (tree decl, tree targs)
{
  /* If this is a template instantiation, check the innermost
     template args for visibility constraints.  The outer template
     args are covered by the class check.  */
  tree args = INNERMOST_TEMPLATE_ARGS (targs);
  int i;
  for (i = TREE_VEC_LENGTH (args); i > 0; --i)
    {
      int vis = 0;

      tree arg = TREE_VEC_ELT (args, i-1);
      if (TYPE_P (arg))
	vis = type_visibility (arg);
      else if (TREE_TYPE (arg) && POINTER_TYPE_P (TREE_TYPE (arg)))
	{
	  STRIP_NOPS (arg);
	  if (TREE_CODE (arg) == ADDR_EXPR)
	    arg = TREE_OPERAND (arg, 0);
	  if (TREE_CODE (arg) == VAR_DECL
	      || TREE_CODE (arg) == FUNCTION_DECL)
	    {
	      if (! TREE_PUBLIC (arg))
		vis = VISIBILITY_ANON;
	      else
		vis = DECL_VISIBILITY (arg);
	    }
	}
      if (vis)
	constrain_visibility (decl, vis);
    }
}

/* Like c_determine_visibility, but with additional C++-specific
   behavior.

   Function-scope entities can rely on the function's visibility because
   it is set in start_preparsed_function.

   Class-scope entities cannot rely on the class's visibility until the end
   of the enclosing class definition.

   Note that because namespaces have multiple independent definitions,
   namespace visibility is handled elsewhere using the #pragma visibility
   machinery rather than by decorating the namespace declaration.

   The goal is for constraints from the type to give a diagnostic, and
   other constraints to be applied silently.  */

void
determine_visibility (tree decl)
{
  tree class_type = NULL_TREE;
  bool use_template;

  /* Remember that all decls get VISIBILITY_DEFAULT when built.  */

  /* Only relevant for names with external linkage.  */
  if (!TREE_PUBLIC (decl))
    return;

  /* Cloned constructors and destructors get the same visibility as
     the underlying function.  That should be set up in
     maybe_clone_body.  */
  gcc_assert (!DECL_CLONED_FUNCTION_P (decl));

  if (TREE_CODE (decl) == TYPE_DECL)
    {
      if (CLASS_TYPE_P (TREE_TYPE (decl)))
	use_template = CLASSTYPE_USE_TEMPLATE (TREE_TYPE (decl));
      else if (TYPE_TEMPLATE_INFO (TREE_TYPE (decl)))
	use_template = 1;
      else
	use_template = 0;
    }
  else if (DECL_LANG_SPECIFIC (decl))
    use_template = DECL_USE_TEMPLATE (decl);
  else
    use_template = 0;

  /* Anything that is exported must have default visibility.  */
  if (TARGET_DLLIMPORT_DECL_ATTRIBUTES
      && lookup_attribute ("dllexport",
			   TREE_CODE (decl) == TYPE_DECL
			   ? TYPE_ATTRIBUTES (TREE_TYPE (decl))
			   : DECL_ATTRIBUTES (decl)))
    {
      DECL_VISIBILITY (decl) = VISIBILITY_DEFAULT;
      DECL_VISIBILITY_SPECIFIED (decl) = 1;
    }

  /* If DECL is a member of a class, visibility specifiers on the
     class can influence the visibility of the DECL.  */
  if (DECL_CLASS_SCOPE_P (decl))
    class_type = DECL_CONTEXT (decl);
  else if (TREE_CODE (decl) == VAR_DECL
	   && DECL_TINFO_P (decl)
	   && CLASS_TYPE_P (TREE_TYPE (DECL_NAME (decl))))
    class_type = TREE_TYPE (DECL_NAME (decl));
  else
    {
      /* Not a class member.  */

      /* Virtual tables have DECL_CONTEXT set to their associated class,
	 so they are automatically handled above.  */
      gcc_assert (TREE_CODE (decl) != VAR_DECL
		  || !DECL_VTABLE_OR_VTT_P (decl));

      if (DECL_FUNCTION_SCOPE_P (decl) && ! DECL_VISIBILITY_SPECIFIED (decl))
	{
	  /* Local statics and classes get the visibility of their
	     containing function by default, except that
	     -fvisibility-inlines-hidden doesn't affect them.  */
	  tree fn = DECL_CONTEXT (decl);
	  if (DECL_VISIBILITY_SPECIFIED (fn) || ! DECL_CLASS_SCOPE_P (fn))
	    {
	      DECL_VISIBILITY (decl) = DECL_VISIBILITY (fn);
	      DECL_VISIBILITY_SPECIFIED (decl) = 
		DECL_VISIBILITY_SPECIFIED (fn);
	    }
	  else
	    determine_visibility_from_class (decl, DECL_CONTEXT (fn));

	  /* Local classes in templates have CLASSTYPE_USE_TEMPLATE set,
	     but have no TEMPLATE_INFO, so don't try to check it.  */
	  use_template = 0;
	}
      else if (TREE_CODE (decl) == VAR_DECL && DECL_TINFO_P (decl))
	{
	  /* tinfo visibility is based on the type it's for.  */
	  constrain_visibility
	    (decl, type_visibility (TREE_TYPE (DECL_NAME (decl))));
	}
      else if (use_template)
	/* Template instantiations and specializations get visibility based
	   on their template unless they override it with an attribute.  */;
      else if (! DECL_VISIBILITY_SPECIFIED (decl))
	{
	  /* Set default visibility to whatever the user supplied with
	     #pragma GCC visibility or a namespace visibility attribute.  */
	  DECL_VISIBILITY (decl) = default_visibility;
	  DECL_VISIBILITY_SPECIFIED (decl) = visibility_options.inpragma;
	}
    }

  if (use_template)
    {
      /* If the specialization doesn't specify visibility, use the
	 visibility from the template.  */
      tree tinfo = (TREE_CODE (decl) == TYPE_DECL
		    ? TYPE_TEMPLATE_INFO (TREE_TYPE (decl))
		    : DECL_TEMPLATE_INFO (decl));
      tree args = TI_ARGS (tinfo);
      
      if (args != error_mark_node)
	{
	  int depth = TMPL_ARGS_DEPTH (args);
	  tree pattern = DECL_TEMPLATE_RESULT (TI_TEMPLATE (tinfo));

	  if (!DECL_VISIBILITY_SPECIFIED (decl))
	    {
	      DECL_VISIBILITY (decl) = DECL_VISIBILITY (pattern);
	      DECL_VISIBILITY_SPECIFIED (decl)
		= DECL_VISIBILITY_SPECIFIED (pattern);
	    }

	  /* FIXME should TMPL_ARGS_DEPTH really return 1 for null input? */
	  if (args && depth > template_class_depth (class_type))
	    /* Limit visibility based on its template arguments.  */
	    constrain_visibility_for_template (decl, args);
	}
    }

  if (class_type)
    determine_visibility_from_class (decl, class_type);

  if (decl_anon_ns_mem_p (decl))
    /* Names in an anonymous namespace get internal linkage.
       This might change once we implement export.  */
    constrain_visibility (decl, VISIBILITY_ANON);
  else if (TREE_CODE (decl) != TYPE_DECL)
    {
      /* Propagate anonymity from type to decl.  */
      int tvis = type_visibility (TREE_TYPE (decl));
      if (tvis == VISIBILITY_ANON)
	constrain_visibility (decl, tvis);
    }
}

/* By default, static data members and function members receive
   the visibility of their containing class.  */

static void
determine_visibility_from_class (tree decl, tree class_type)
{
  if (visibility_options.inlines_hidden
      /* Don't do this for inline templates; specializations might not be
	 inline, and we don't want them to inherit the hidden
	 visibility.  We'll set it here for all inline instantiations.  */
      && !processing_template_decl
      && ! DECL_VISIBILITY_SPECIFIED (decl)
      && TREE_CODE (decl) == FUNCTION_DECL
      && DECL_DECLARED_INLINE_P (decl)
      && (! DECL_LANG_SPECIFIC (decl)
	  || ! DECL_EXPLICIT_INSTANTIATION (decl)))
    DECL_VISIBILITY (decl) = VISIBILITY_HIDDEN;
  else if (!DECL_VISIBILITY_SPECIFIED (decl))
    {
      /* Default to the class visibility.  */
      DECL_VISIBILITY (decl) = CLASSTYPE_VISIBILITY (class_type);
      DECL_VISIBILITY_SPECIFIED (decl)
	= CLASSTYPE_VISIBILITY_SPECIFIED (class_type);
    }

  /* Give the target a chance to override the visibility associated
     with DECL.  */
  if (TREE_CODE (decl) == VAR_DECL
      && (DECL_TINFO_P (decl)
	  || (DECL_VTABLE_OR_VTT_P (decl)
	      /* Construction virtual tables are not exported because
		 they cannot be referred to from other object files;
		 their name is not standardized by the ABI.  */
	      && !DECL_CONSTRUCTION_VTABLE_P (decl)))
      && TREE_PUBLIC (decl)
      && !DECL_REALLY_EXTERN (decl)
      && !DECL_VISIBILITY_SPECIFIED (decl)
      && !CLASSTYPE_VISIBILITY_SPECIFIED (class_type))
    targetm.cxx.determine_class_data_visibility (decl);
}

/* Constrain the visibility of a class TYPE based on the visibility of its
   field types.  Warn if any fields require lesser visibility.  */

void
constrain_class_visibility (tree type)
{
  tree binfo;
  tree t;
  int i;

  int vis = type_visibility (type);

  if (vis == VISIBILITY_ANON
      || DECL_IN_SYSTEM_HEADER (TYPE_MAIN_DECL (type)))
    return;

  /* Don't warn about visibility if the class has explicit visibility.  */
  if (CLASSTYPE_VISIBILITY_SPECIFIED (type))
    vis = VISIBILITY_INTERNAL;

  for (t = TYPE_FIELDS (type); t; t = TREE_CHAIN (t))
    if (TREE_CODE (t) == FIELD_DECL && TREE_TYPE (t) != error_mark_node)
      {
	tree ftype = strip_array_types (TREE_TYPE (t));
	int subvis = type_visibility (ftype);

	if (subvis == VISIBILITY_ANON)
	  warning (0, "\
%qT has a field %qD whose type uses the anonymous namespace",
		   type, t);
	else if (IS_AGGR_TYPE (ftype)
		 && vis < VISIBILITY_HIDDEN
		 && subvis >= VISIBILITY_HIDDEN)
	  warning (OPT_Wattributes, "\
%qT declared with greater visibility than the type of its field %qD",
		   type, t);
      }

  binfo = TYPE_BINFO (type);
  for (i = 0; BINFO_BASE_ITERATE (binfo, i, t); ++i)
    {
      int subvis = type_visibility (TREE_TYPE (t));

      if (subvis == VISIBILITY_ANON)
	warning (0, "\
%qT has a base %qT whose type uses the anonymous namespace",
		 type, TREE_TYPE (t));
      else if (vis < VISIBILITY_HIDDEN
	       && subvis >= VISIBILITY_HIDDEN)
	warning (OPT_Wattributes, "\
%qT declared with greater visibility than its base %qT",
		 type, TREE_TYPE (t));
    }
}

/* DECL is a FUNCTION_DECL or VAR_DECL.  If the object file linkage
   for DECL has not already been determined, do so now by setting
   DECL_EXTERNAL, DECL_COMDAT and other related flags.  Until this
   function is called entities with vague linkage whose definitions
   are available must have TREE_PUBLIC set.

   If this function decides to place DECL in COMDAT, it will set
   appropriate flags -- but will not clear DECL_EXTERNAL.  It is up to
   the caller to decide whether or not to clear DECL_EXTERNAL.  Some
   callers defer that decision until it is clear that DECL is actually
   required.  */

void
import_export_decl (tree decl)
{
  int emit_p;
  bool comdat_p;
  bool import_p;
  tree class_type = NULL_TREE;

  if (DECL_INTERFACE_KNOWN (decl))
    return;

  /* We cannot determine what linkage to give to an entity with vague
     linkage until the end of the file.  For example, a virtual table
     for a class will be defined if and only if the key method is
     defined in this translation unit.  As a further example, consider
     that when compiling a translation unit that uses PCH file with
     "-frepo" it would be incorrect to make decisions about what
     entities to emit when building the PCH; those decisions must be
     delayed until the repository information has been processed.  */
  gcc_assert (at_eof);
  /* Object file linkage for explicit instantiations is handled in
     mark_decl_instantiated.  For static variables in functions with
     vague linkage, maybe_commonize_var is used.

     Therefore, the only declarations that should be provided to this
     function are those with external linkage that are:

     * implicit instantiations of function templates

     * inline function

     * implicit instantiations of static data members of class
       templates

     * virtual tables

     * typeinfo objects

     Furthermore, all entities that reach this point must have a
     definition available in this translation unit.

     The following assertions check these conditions.  */
  gcc_assert (TREE_CODE (decl) == FUNCTION_DECL
	      || TREE_CODE (decl) == VAR_DECL);
  /* Any code that creates entities with TREE_PUBLIC cleared should
     also set DECL_INTERFACE_KNOWN.  */
  gcc_assert (TREE_PUBLIC (decl));
  if (TREE_CODE (decl) == FUNCTION_DECL)
    gcc_assert (DECL_IMPLICIT_INSTANTIATION (decl)
		|| DECL_FRIEND_PSEUDO_TEMPLATE_INSTANTIATION (decl)
		|| DECL_DECLARED_INLINE_P (decl));
  else
    gcc_assert (DECL_IMPLICIT_INSTANTIATION (decl)
		|| DECL_VTABLE_OR_VTT_P (decl)
		|| DECL_TINFO_P (decl));
  /* Check that a definition of DECL is available in this translation
     unit.  */
  gcc_assert (!DECL_REALLY_EXTERN (decl));

  /* Assume that DECL will not have COMDAT linkage.  */
  comdat_p = false;
  /* Assume that DECL will not be imported into this translation
     unit.  */
  import_p = false;

  /* See if the repository tells us whether or not to emit DECL in
     this translation unit.  */
  emit_p = repo_emit_p (decl);
  if (emit_p == 0)
    import_p = true;
  else if (emit_p == 1)
    {
      /* The repository indicates that this entity should be defined
	 here.  Make sure the back end honors that request.  */
      if (TREE_CODE (decl) == VAR_DECL)
	mark_needed (decl);
      else if (DECL_MAYBE_IN_CHARGE_CONSTRUCTOR_P (decl)
	       || DECL_MAYBE_IN_CHARGE_DESTRUCTOR_P (decl))
	{
	  tree clone;
	  FOR_EACH_CLONE (clone, decl)
	    mark_needed (clone);
	}
      else
	mark_needed (decl);
      /* Output the definition as an ordinary strong definition.  */
      DECL_EXTERNAL (decl) = 0;
      DECL_INTERFACE_KNOWN (decl) = 1;
      return;
    }

  if (import_p)
    /* We have already decided what to do with this DECL; there is no
       need to check anything further.  */
    ;
  else if (TREE_CODE (decl) == VAR_DECL && DECL_VTABLE_OR_VTT_P (decl))
    {
      class_type = DECL_CONTEXT (decl);
      import_export_class (class_type);
      if (TYPE_FOR_JAVA (class_type))
	import_p = true;
      else if (CLASSTYPE_INTERFACE_KNOWN (class_type)
	       && CLASSTYPE_INTERFACE_ONLY (class_type))
	import_p = true;
      else if ((!flag_weak || TARGET_WEAK_NOT_IN_ARCHIVE_TOC)
	       && !CLASSTYPE_USE_TEMPLATE (class_type)
	       && CLASSTYPE_KEY_METHOD (class_type)
	       && !DECL_DECLARED_INLINE_P (CLASSTYPE_KEY_METHOD (class_type)))
	/* The ABI requires that all virtual tables be emitted with
	   COMDAT linkage.  However, on systems where COMDAT symbols
	   don't show up in the table of contents for a static
	   archive, or on systems without weak symbols (where we
	   approximate COMDAT linkage by using internal linkage), the
	   linker will report errors about undefined symbols because
	   it will not see the virtual table definition.  Therefore,
	   in the case that we know that the virtual table will be
	   emitted in only one translation unit, we make the virtual
	   table an ordinary definition with external linkage.  */
	DECL_EXTERNAL (decl) = 0;
      else if (CLASSTYPE_INTERFACE_KNOWN (class_type))
	{
	  /* CLASS_TYPE is being exported from this translation unit,
	     so DECL should be defined here.  */
	  if (!flag_weak && CLASSTYPE_EXPLICIT_INSTANTIATION (class_type))
	    /* If a class is declared in a header with the "extern
	       template" extension, then it will not be instantiated,
	       even in translation units that would normally require
	       it.  Often such classes are explicitly instantiated in
	       one translation unit.  Therefore, the explicit
	       instantiation must be made visible to other translation
	       units.  */
	    DECL_EXTERNAL (decl) = 0;
	  else
	    {
	      /* The generic C++ ABI says that class data is always
		 COMDAT, even if there is a key function.  Some
		 variants (e.g., the ARM EABI) says that class data
		 only has COMDAT linkage if the class data might be
		 emitted in more than one translation unit.  When the
		 key method can be inline and is inline, we still have
		 to arrange for comdat even though
		 class_data_always_comdat is false.  */
	      if (!CLASSTYPE_KEY_METHOD (class_type)
		  || DECL_DECLARED_INLINE_P (CLASSTYPE_KEY_METHOD (class_type))
		  || targetm.cxx.class_data_always_comdat ())
		{
		  /* The ABI requires COMDAT linkage.  Normally, we
		     only emit COMDAT things when they are needed;
		     make sure that we realize that this entity is
		     indeed needed.  */
		  comdat_p = true;
		  mark_needed (decl);
		}
	    }
	}
      else if (!flag_implicit_templates
	       && CLASSTYPE_IMPLICIT_INSTANTIATION (class_type))
	import_p = true;
      else
	comdat_p = true;
    }
  else if (TREE_CODE (decl) == VAR_DECL && DECL_TINFO_P (decl))
    {
      tree type = TREE_TYPE (DECL_NAME (decl));
      if (CLASS_TYPE_P (type))
	{
	  class_type = type;
	  import_export_class (type);
	  if (CLASSTYPE_INTERFACE_KNOWN (type)
	      && TYPE_POLYMORPHIC_P (type)
	      && CLASSTYPE_INTERFACE_ONLY (type)
	      /* If -fno-rtti was specified, then we cannot be sure
		 that RTTI information will be emitted with the
		 virtual table of the class, so we must emit it
		 wherever it is used.  */
	      && flag_rtti)
	    import_p = true;
	  else
	    {
	      if (CLASSTYPE_INTERFACE_KNOWN (type)
		  && !CLASSTYPE_INTERFACE_ONLY (type))
		{
		  comdat_p = (targetm.cxx.class_data_always_comdat ()
			      || (CLASSTYPE_KEY_METHOD (type)
				  && DECL_DECLARED_INLINE_P (CLASSTYPE_KEY_METHOD (type))));
		  mark_needed (decl);
		  if (!flag_weak)
		    {
		      comdat_p = false;
		      DECL_EXTERNAL (decl) = 0;
		    }
		}
	      else
		comdat_p = true;
	    }
	}
      else
	comdat_p = true;
    }
  else if (DECL_TEMPLATE_INSTANTIATION (decl)
	   || DECL_FRIEND_PSEUDO_TEMPLATE_INSTANTIATION (decl))
    {
      /* DECL is an implicit instantiation of a function or static
	 data member.  */
      if (flag_implicit_templates
	  || (flag_implicit_inline_templates
	      && TREE_CODE (decl) == FUNCTION_DECL
	      && DECL_DECLARED_INLINE_P (decl)))
	comdat_p = true;
      else
	/* If we are not implicitly generating templates, then mark
	   this entity as undefined in this translation unit.  */
	import_p = true;
    }
  else if (DECL_FUNCTION_MEMBER_P (decl))
    {
      if (!DECL_DECLARED_INLINE_P (decl))
	{
	  tree ctype = DECL_CONTEXT (decl);
	  import_export_class (ctype);
	  if (CLASSTYPE_INTERFACE_KNOWN (ctype))
	    {
	      DECL_NOT_REALLY_EXTERN (decl)
		= ! (CLASSTYPE_INTERFACE_ONLY (ctype)
		     || (DECL_DECLARED_INLINE_P (decl)
			 && ! flag_implement_inlines
			 && !DECL_VINDEX (decl)));

	      if (!DECL_NOT_REALLY_EXTERN (decl))
		DECL_EXTERNAL (decl) = 1;

	      /* Always make artificials weak.  */
	      if (DECL_ARTIFICIAL (decl) && flag_weak)
		comdat_p = true;
	      else
		maybe_make_one_only (decl);
	    }
	}
      else
	comdat_p = true;
    }
  else
    comdat_p = true;

  if (import_p)
    {
      /* If we are importing DECL into this translation unit, mark is
	 an undefined here.  */
      DECL_EXTERNAL (decl) = 1;
      DECL_NOT_REALLY_EXTERN (decl) = 0;
    }
  else if (comdat_p)
    {
      /* If we decided to put DECL in COMDAT, mark it accordingly at
	 this point.  */
      comdat_linkage (decl);
    }

  DECL_INTERFACE_KNOWN (decl) = 1;
}

/* Return an expression that performs the destruction of DECL, which
   must be a VAR_DECL whose type has a non-trivial destructor, or is
   an array whose (innermost) elements have a non-trivial destructor.  */

tree
build_cleanup (tree decl)
{
  tree temp;
  tree type = TREE_TYPE (decl);

  /* This function should only be called for declarations that really
     require cleanups.  */
  gcc_assert (!TYPE_HAS_TRIVIAL_DESTRUCTOR (type));

  /* Treat all objects with destructors as used; the destructor may do
     something substantive.  */
  mark_used (decl);

  if (TREE_CODE (type) == ARRAY_TYPE)
    temp = decl;
  else
    {
      cxx_mark_addressable (decl);
      temp = build1 (ADDR_EXPR, build_pointer_type (type), decl);
    }
  temp = build_delete (TREE_TYPE (temp), temp,
		       sfk_complete_destructor,
		       LOOKUP_NORMAL|LOOKUP_NONVIRTUAL|LOOKUP_DESTRUCTOR, 0);
  return temp;
}

/* Returns the initialization guard variable for the variable DECL,
   which has static storage duration.  */

tree
get_guard (tree decl)
{
  tree sname;
  tree guard;

  sname = mangle_guard_variable (decl);
  guard = IDENTIFIER_GLOBAL_VALUE (sname);
  if (! guard)
    {
      tree guard_type;

      /* We use a type that is big enough to contain a mutex as well
	 as an integer counter.  */
      guard_type = targetm.cxx.guard_type ();
      guard = build_decl (VAR_DECL, sname, guard_type);

      /* The guard should have the same linkage as what it guards.  */
      TREE_PUBLIC (guard) = TREE_PUBLIC (decl);
      TREE_STATIC (guard) = TREE_STATIC (decl);
      DECL_COMMON (guard) = DECL_COMMON (decl);
      DECL_ONE_ONLY (guard) = DECL_ONE_ONLY (decl);
      if (TREE_PUBLIC (decl))
	DECL_WEAK (guard) = DECL_WEAK (decl);

      DECL_ARTIFICIAL (guard) = 1;
      DECL_IGNORED_P (guard) = 1;
      TREE_USED (guard) = 1;
      pushdecl_top_level_and_finish (guard, NULL_TREE);
    }
  return guard;
}

/* Return those bits of the GUARD variable that should be set when the
   guarded entity is actually initialized.  */

static tree
get_guard_bits (tree guard)
{
  if (!targetm.cxx.guard_mask_bit ())
    {
      /* We only set the first byte of the guard, in order to leave room
	 for a mutex in the high-order bits.  */
      guard = build1 (ADDR_EXPR,
		      build_pointer_type (TREE_TYPE (guard)),
		      guard);
      guard = build1 (NOP_EXPR,
		      build_pointer_type (char_type_node),
		      guard);
      guard = build1 (INDIRECT_REF, char_type_node, guard);
    }

  return guard;
}

/* Return an expression which determines whether or not the GUARD
   variable has already been initialized.  */

tree
get_guard_cond (tree guard)
{
  tree guard_value;

  /* Check to see if the GUARD is zero.  */
  guard = get_guard_bits (guard);

  /* Mask off all but the low bit.  */
  if (targetm.cxx.guard_mask_bit ())
    {
      guard_value = integer_one_node;
      if (!same_type_p (TREE_TYPE (guard_value), TREE_TYPE (guard)))
	guard_value = convert (TREE_TYPE (guard), guard_value);
	guard = cp_build_binary_op (BIT_AND_EXPR, guard, guard_value);
    }

  guard_value = integer_zero_node;
  if (!same_type_p (TREE_TYPE (guard_value), TREE_TYPE (guard)))
    guard_value = convert (TREE_TYPE (guard), guard_value);
  return cp_build_binary_op (EQ_EXPR, guard, guard_value);
}

/* Return an expression which sets the GUARD variable, indicating that
   the variable being guarded has been initialized.  */

tree
set_guard (tree guard)
{
  tree guard_init;

  /* Set the GUARD to one.  */
  guard = get_guard_bits (guard);
  guard_init = integer_one_node;
  if (!same_type_p (TREE_TYPE (guard_init), TREE_TYPE (guard)))
    guard_init = convert (TREE_TYPE (guard), guard_init);
  return build_modify_expr (guard, NOP_EXPR, guard_init);
}

/* Start the process of running a particular set of global constructors
   or destructors.  Subroutine of do_[cd]tors.  */

static tree
start_objects (int method_type, int initp)
{
  tree body;
  tree fndecl;
  char type[10];

  /* Make ctor or dtor function.  METHOD_TYPE may be 'I' or 'D'.  */

  if (initp != DEFAULT_INIT_PRIORITY)
    {
      char joiner;

#ifdef JOINER
      joiner = JOINER;
#else
      joiner = '_';
#endif

      sprintf (type, "%c%c%.5u", method_type, joiner, initp);
    }
  else
    sprintf (type, "%c", method_type);

  fndecl = build_lang_decl (FUNCTION_DECL,
			    get_file_function_name_long (type),
			    build_function_type (void_type_node,
						 void_list_node));
  start_preparsed_function (fndecl, /*attrs=*/NULL_TREE, SF_PRE_PARSED);

  /* It can be a static function as long as collect2 does not have
     to scan the object file to find its ctor/dtor routine.  */
  TREE_PUBLIC (current_function_decl) = ! targetm.have_ctors_dtors;

  /* Mark this declaration as used to avoid spurious warnings.  */
  TREE_USED (current_function_decl) = 1;

  /* Mark this function as a global constructor or destructor.  */
  if (method_type == 'I')
    DECL_GLOBAL_CTOR_P (current_function_decl) = 1;
  else
    DECL_GLOBAL_DTOR_P (current_function_decl) = 1;
  DECL_LANG_SPECIFIC (current_function_decl)->decl_flags.u2sel = 1;

  body = begin_compound_stmt (BCS_FN_BODY);

  /* We cannot allow these functions to be elided, even if they do not
     have external linkage.  And, there's no point in deferring
     compilation of these functions; they're all going to have to be
     out anyhow.  */
  DECL_INLINE (current_function_decl) = 0;
  DECL_UNINLINABLE (current_function_decl) = 1;

  return body;
}

/* Finish the process of running a particular set of global constructors
   or destructors.  Subroutine of do_[cd]tors.  */

static void
finish_objects (int method_type, int initp, tree body)
{
  tree fn;

  /* Finish up.  */
  finish_compound_stmt (body);
  fn = finish_function (0);
  expand_or_defer_fn (fn);

  /* When only doing semantic analysis, and no RTL generation, we
     can't call functions that directly emit assembly code; there is
     no assembly file in which to put the code.  */
  if (flag_syntax_only)
    return;

  if (targetm.have_ctors_dtors)
    {
      rtx fnsym = XEXP (DECL_RTL (fn), 0);
      cgraph_mark_needed_node (cgraph_node (fn));
      if (method_type == 'I')
	(* targetm.asm_out.constructor) (fnsym, initp);
      else
	(* targetm.asm_out.destructor) (fnsym, initp);
    }
}

/* The names of the parameters to the function created to handle
   initializations and destructions for objects with static storage
   duration.  */
#define INITIALIZE_P_IDENTIFIER "__initialize_p"
#define PRIORITY_IDENTIFIER "__priority"

/* The name of the function we create to handle initializations and
   destructions for objects with static storage duration.  */
#define SSDF_IDENTIFIER "__static_initialization_and_destruction"

/* The declaration for the __INITIALIZE_P argument.  */
static GTY(()) tree initialize_p_decl;

/* The declaration for the __PRIORITY argument.  */
static GTY(()) tree priority_decl;

/* The declaration for the static storage duration function.  */
static GTY(()) tree ssdf_decl;

/* All the static storage duration functions created in this
   translation unit.  */
static GTY(()) VEC(tree,gc) *ssdf_decls;

/* A map from priority levels to information about that priority
   level.  There may be many such levels, so efficient lookup is
   important.  */
static splay_tree priority_info_map;

/* Begins the generation of the function that will handle all
   initialization and destruction of objects with static storage
   duration.  The function generated takes two parameters of type
   `int': __INITIALIZE_P and __PRIORITY.  If __INITIALIZE_P is
   nonzero, it performs initializations.  Otherwise, it performs
   destructions.  It only performs those initializations or
   destructions with the indicated __PRIORITY.  The generated function
   returns no value.

   It is assumed that this function will only be called once per
   translation unit.  */

static tree
start_static_storage_duration_function (unsigned count)
{
  tree parm_types;
  tree type;
  tree body;
  char id[sizeof (SSDF_IDENTIFIER) + 1 /* '\0' */ + 32];

  /* Create the identifier for this function.  It will be of the form
     SSDF_IDENTIFIER_<number>.  */
  sprintf (id, "%s_%u", SSDF_IDENTIFIER, count);

  /* Create the parameters.  */
  parm_types = void_list_node;
  parm_types = tree_cons (NULL_TREE, integer_type_node, parm_types);
  parm_types = tree_cons (NULL_TREE, integer_type_node, parm_types);
  type = build_function_type (void_type_node, parm_types);

  /* Create the FUNCTION_DECL itself.  */
  ssdf_decl = build_lang_decl (FUNCTION_DECL,
			       get_identifier (id),
			       type);
  TREE_PUBLIC (ssdf_decl) = 0;
  DECL_ARTIFICIAL (ssdf_decl) = 1;

  /* Put this function in the list of functions to be called from the
     static constructors and destructors.  */
  if (!ssdf_decls)
    {
      ssdf_decls = VEC_alloc (tree, gc, 32);

      /* Take this opportunity to initialize the map from priority
	 numbers to information about that priority level.  */
      priority_info_map = splay_tree_new (splay_tree_compare_ints,
					  /*delete_key_fn=*/0,
					  /*delete_value_fn=*/
					  (splay_tree_delete_value_fn) &free);

      /* We always need to generate functions for the
	 DEFAULT_INIT_PRIORITY so enter it now.  That way when we walk
	 priorities later, we'll be sure to find the
	 DEFAULT_INIT_PRIORITY.  */
      get_priority_info (DEFAULT_INIT_PRIORITY);
    }

  VEC_safe_push (tree, gc, ssdf_decls, ssdf_decl);

  /* Create the argument list.  */
  initialize_p_decl = cp_build_parm_decl
    (get_identifier (INITIALIZE_P_IDENTIFIER), integer_type_node);
  DECL_CONTEXT (initialize_p_decl) = ssdf_decl;
  TREE_USED (initialize_p_decl) = 1;
  priority_decl = cp_build_parm_decl
    (get_identifier (PRIORITY_IDENTIFIER), integer_type_node);
  DECL_CONTEXT (priority_decl) = ssdf_decl;
  TREE_USED (priority_decl) = 1;

  TREE_CHAIN (initialize_p_decl) = priority_decl;
  DECL_ARGUMENTS (ssdf_decl) = initialize_p_decl;

  /* Put the function in the global scope.  */
  pushdecl (ssdf_decl);

  /* Start the function itself.  This is equivalent to declaring the
     function as:

       static void __ssdf (int __initialize_p, init __priority_p);

     It is static because we only need to call this function from the
     various constructor and destructor functions for this module.  */
  start_preparsed_function (ssdf_decl,
			    /*attrs=*/NULL_TREE,
			    SF_PRE_PARSED);

  /* Set up the scope of the outermost block in the function.  */
  body = begin_compound_stmt (BCS_FN_BODY);

  /* This function must not be deferred because we are depending on
     its compilation to tell us what is TREE_SYMBOL_REFERENCED.  */
  DECL_INLINE (ssdf_decl) = 0;
  DECL_UNINLINABLE (ssdf_decl) = 1;

  return body;
}

/* Finish the generation of the function which performs initialization
   and destruction of objects with static storage duration.  After
   this point, no more such objects can be created.  */

static void
finish_static_storage_duration_function (tree body)
{
  /* Close out the function.  */
  finish_compound_stmt (body);
  expand_or_defer_fn (finish_function (0));
}

/* Return the information about the indicated PRIORITY level.  If no
   code to handle this level has yet been generated, generate the
   appropriate prologue.  */

static priority_info
get_priority_info (int priority)
{
  priority_info pi;
  splay_tree_node n;

  n = splay_tree_lookup (priority_info_map,
			 (splay_tree_key) priority);
  if (!n)
    {
      /* Create a new priority information structure, and insert it
	 into the map.  */
      pi = XNEW (struct priority_info_s);
      pi->initializations_p = 0;
      pi->destructions_p = 0;
      splay_tree_insert (priority_info_map,
			 (splay_tree_key) priority,
			 (splay_tree_value) pi);
    }
  else
    pi = (priority_info) n->value;

  return pi;
}

/* The effective initialization priority of a DECL.  */

#define DECL_EFFECTIVE_INIT_PRIORITY(decl)				      \
	((!DECL_HAS_INIT_PRIORITY_P (decl) || DECL_INIT_PRIORITY (decl) == 0) \
	 ? DEFAULT_INIT_PRIORITY : DECL_INIT_PRIORITY (decl))

/* Whether a DECL needs a guard to protect it against multiple
   initialization.  */

#define NEEDS_GUARD_P(decl) (TREE_PUBLIC (decl) && (DECL_COMMON (decl)      \
						    || DECL_ONE_ONLY (decl) \
						    || DECL_WEAK (decl)))

/* Set up to handle the initialization or destruction of DECL.  If
   INITP is nonzero, we are initializing the variable.  Otherwise, we
   are destroying it.  */

static void
one_static_initialization_or_destruction (tree decl, tree init, bool initp)
{
  tree guard_if_stmt = NULL_TREE;
  tree guard;

  /* If we are supposed to destruct and there's a trivial destructor,
     nothing has to be done.  */
  if (!initp
      && TYPE_HAS_TRIVIAL_DESTRUCTOR (TREE_TYPE (decl)))
    return;

  /* Trick the compiler into thinking we are at the file and line
     where DECL was declared so that error-messages make sense, and so
     that the debugger will show somewhat sensible file and line
     information.  */
  input_location = DECL_SOURCE_LOCATION (decl);

  /* Because of:

       [class.access.spec]

       Access control for implicit calls to the constructors,
       the conversion functions, or the destructor called to
       create and destroy a static data member is performed as
       if these calls appeared in the scope of the member's
       class.

     we pretend we are in a static member function of the class of
     which the DECL is a member.  */
  if (member_p (decl))
    {
      DECL_CONTEXT (current_function_decl) = DECL_CONTEXT (decl);
      DECL_STATIC_FUNCTION_P (current_function_decl) = 1;
    }

  /* Assume we don't need a guard.  */
  guard = NULL_TREE;
  /* We need a guard if this is an object with external linkage that
     might be initialized in more than one place.  (For example, a
     static data member of a template, when the data member requires
     construction.)  */
  if (NEEDS_GUARD_P (decl))
    {
      tree guard_cond;

      guard = get_guard (decl);

      /* When using __cxa_atexit, we just check the GUARD as we would
	 for a local static.  */
      if (flag_use_cxa_atexit)
	{
	  /* When using __cxa_atexit, we never try to destroy
	     anything from a static destructor.  */
	  gcc_assert (initp);
	  guard_cond = get_guard_cond (guard);
	}
      /* If we don't have __cxa_atexit, then we will be running
	 destructors from .fini sections, or their equivalents.  So,
	 we need to know how many times we've tried to initialize this
	 object.  We do initializations only if the GUARD is zero,
	 i.e., if we are the first to initialize the variable.  We do
	 destructions only if the GUARD is one, i.e., if we are the
	 last to destroy the variable.  */
      else if (initp)
	guard_cond
	  = cp_build_binary_op (EQ_EXPR,
				build_unary_op (PREINCREMENT_EXPR,
						guard,
						/*noconvert=*/1),
				integer_one_node);
      else
	guard_cond
	  = cp_build_binary_op (EQ_EXPR,
				build_unary_op (PREDECREMENT_EXPR,
						guard,
						/*noconvert=*/1),
				integer_zero_node);

      guard_if_stmt = begin_if_stmt ();
      finish_if_stmt_cond (guard_cond, guard_if_stmt);
    }


  /* If we're using __cxa_atexit, we have not already set the GUARD,
     so we must do so now.  */
  if (guard && initp && flag_use_cxa_atexit)
    finish_expr_stmt (set_guard (guard));

  /* Perform the initialization or destruction.  */
  if (initp)
    {
      if (init)
	finish_expr_stmt (init);

      /* If we're using __cxa_atexit, register a function that calls the
	 destructor for the object.  */
      if (flag_use_cxa_atexit)
	finish_expr_stmt (register_dtor_fn (decl));
    }
  else
    finish_expr_stmt (build_cleanup (decl));

  /* Finish the guard if-stmt, if necessary.  */
  if (guard)
    {
      finish_then_clause (guard_if_stmt);
      finish_if_stmt (guard_if_stmt);
    }

  /* Now that we're done with DECL we don't need to pretend to be a
     member of its class any longer.  */
  DECL_CONTEXT (current_function_decl) = NULL_TREE;
  DECL_STATIC_FUNCTION_P (current_function_decl) = 0;
}

/* Generate code to do the initialization or destruction of the decls in VARS,
   a TREE_LIST of VAR_DECL with static storage duration.
   Whether initialization or destruction is performed is specified by INITP.  */

static void
do_static_initialization_or_destruction (tree vars, bool initp)
{
  tree node, init_if_stmt, cond;

  /* Build the outer if-stmt to check for initialization or destruction.  */
  init_if_stmt = begin_if_stmt ();
  cond = initp ? integer_one_node : integer_zero_node;
  cond = cp_build_binary_op (EQ_EXPR,
				  initialize_p_decl,
				  cond);
  finish_if_stmt_cond (cond, init_if_stmt);

  node = vars;
  do {
    tree decl = TREE_VALUE (node);
    tree priority_if_stmt;
    int priority;
    priority_info pi;

    /* If we don't need a destructor, there's nothing to do.  Avoid
       creating a possibly empty if-stmt.  */
    if (!initp && TYPE_HAS_TRIVIAL_DESTRUCTOR (TREE_TYPE (decl)))
      {
	node = TREE_CHAIN (node);
	continue;
      }

    /* Remember that we had an initialization or finalization at this
       priority.  */
    priority = DECL_EFFECTIVE_INIT_PRIORITY (decl);
    pi = get_priority_info (priority);
    if (initp)
      pi->initializations_p = 1;
    else
      pi->destructions_p = 1;

    /* Conditionalize this initialization on being in the right priority
       and being initializing/finalizing appropriately.  */
    priority_if_stmt = begin_if_stmt ();
    cond = cp_build_binary_op (EQ_EXPR,
			       priority_decl,
			       build_int_cst (NULL_TREE, priority));
    finish_if_stmt_cond (cond, priority_if_stmt);

    /* Process initializers with same priority.  */
    for (; node
	   && DECL_EFFECTIVE_INIT_PRIORITY (TREE_VALUE (node)) == priority;
	 node = TREE_CHAIN (node))
      /* Do one initialization or destruction.  */
      one_static_initialization_or_destruction (TREE_VALUE (node),
						TREE_PURPOSE (node), initp);

    /* Finish up the priority if-stmt body.  */
    finish_then_clause (priority_if_stmt);
    finish_if_stmt (priority_if_stmt);

  } while (node);

  /* Finish up the init/destruct if-stmt body.  */
  finish_then_clause (init_if_stmt);
  finish_if_stmt (init_if_stmt);
}

/* VARS is a list of variables with static storage duration which may
   need initialization and/or finalization.  Remove those variables
   that don't really need to be initialized or finalized, and return
   the resulting list.  The order in which the variables appear in
   VARS is in reverse order of the order in which they should actually
   be initialized.  The list we return is in the unreversed order;
   i.e., the first variable should be initialized first.  */

static tree
prune_vars_needing_no_initialization (tree *vars)
{
  tree *var = vars;
  tree result = NULL_TREE;

  while (*var)
    {
      tree t = *var;
      tree decl = TREE_VALUE (t);
      tree init = TREE_PURPOSE (t);

      /* Deal gracefully with error.  */
      if (decl == error_mark_node)
	{
	  var = &TREE_CHAIN (t);
	  continue;
	}

      /* The only things that can be initialized are variables.  */
      gcc_assert (TREE_CODE (decl) == VAR_DECL);

      /* If this object is not defined, we don't need to do anything
	 here.  */
      if (DECL_EXTERNAL (decl))
	{
	  var = &TREE_CHAIN (t);
	  continue;
	}

      /* Also, if the initializer already contains errors, we can bail
	 out now.  */
      if (init && TREE_CODE (init) == TREE_LIST
	  && value_member (error_mark_node, init))
	{
	  var = &TREE_CHAIN (t);
	  continue;
	}

      /* This variable is going to need initialization and/or
	 finalization, so we add it to the list.  */
      *var = TREE_CHAIN (t);
      TREE_CHAIN (t) = result;
      result = t;
    }

  return result;
}

/* Make sure we have told the back end about all the variables in
   VARS.  */

static void
write_out_vars (tree vars)
{
  tree v;

  for (v = vars; v; v = TREE_CHAIN (v))
    {
      tree var = TREE_VALUE (v);
      if (!var_finalized_p (var))
	{
	  import_export_decl (var);
	  rest_of_decl_compilation (var, 1, 1);
	}
    }
}

/* Generate a static constructor (if CONSTRUCTOR_P) or destructor
   (otherwise) that will initialize all gobal objects with static
   storage duration having the indicated PRIORITY.  */

static void
generate_ctor_or_dtor_function (bool constructor_p, int priority,
				location_t *locus)
{
  char function_key;
  tree arguments;
  tree fndecl;
  tree body;
  size_t i;

  input_location = *locus;
#ifdef USE_MAPPED_LOCATION
  /* ??? */
#else
  locus->line++;
#endif

  /* We use `I' to indicate initialization and `D' to indicate
     destruction.  */
  function_key = constructor_p ? 'I' : 'D';

  /* We emit the function lazily, to avoid generating empty
     global constructors and destructors.  */
  body = NULL_TREE;

  /* For Objective-C++, we may need to initialize metadata found in this module.
     This must be done _before_ any other static initializations.  */
  if (c_dialect_objc () && (priority == DEFAULT_INIT_PRIORITY)
      && constructor_p && objc_static_init_needed_p ())
    {
      body = start_objects (function_key, priority);
      static_ctors = objc_generate_static_init_call (static_ctors);
    }

  /* Call the static storage duration function with appropriate
     arguments.  */
  for (i = 0; VEC_iterate (tree, ssdf_decls, i, fndecl); ++i)
    {
      /* Calls to pure or const functions will expand to nothing.  */
      if (! (flags_from_decl_or_type (fndecl) & (ECF_CONST | ECF_PURE)))
	{
	  if (! body)
	    body = start_objects (function_key, priority);

	  arguments = tree_cons (NULL_TREE,
				 build_int_cst (NULL_TREE, priority),
				 NULL_TREE);
	  arguments = tree_cons (NULL_TREE,
				 build_int_cst (NULL_TREE, constructor_p),
				 arguments);
	  finish_expr_stmt (build_function_call (fndecl, arguments));
	}
    }

  /* If we're generating code for the DEFAULT_INIT_PRIORITY, throw in
     calls to any functions marked with attributes indicating that
     they should be called at initialization- or destruction-time.  */
  if (priority == DEFAULT_INIT_PRIORITY)
    {
      tree fns;

      for (fns = constructor_p ? static_ctors : static_dtors;
	   fns;
	   fns = TREE_CHAIN (fns))
	{
	  fndecl = TREE_VALUE (fns);

	  /* Calls to pure/const functions will expand to nothing.  */
	  if (! (flags_from_decl_or_type (fndecl) & (ECF_CONST | ECF_PURE)))
	    {
	      if (! body)
		body = start_objects (function_key, priority);
	      finish_expr_stmt (build_function_call (fndecl, NULL_TREE));
	    }
	}
    }

  /* Close out the function.  */
  if (body)
    finish_objects (function_key, priority, body);
}

/* Generate constructor and destructor functions for the priority
   indicated by N.  */

static int
generate_ctor_and_dtor_functions_for_priority (splay_tree_node n, void * data)
{
  location_t *locus = (location_t *) data;
  int priority = (int) n->key;
  priority_info pi = (priority_info) n->value;

  /* Generate the functions themselves, but only if they are really
     needed.  */
  if (pi->initializations_p
      || (priority == DEFAULT_INIT_PRIORITY && static_ctors))
    generate_ctor_or_dtor_function (/*constructor_p=*/true, priority, locus);
  if (pi->destructions_p
      || (priority == DEFAULT_INIT_PRIORITY && static_dtors))
    generate_ctor_or_dtor_function (/*constructor_p=*/false, priority, locus);

  /* Keep iterating.  */
  return 0;
}

/* Called via LANGHOOK_CALLGRAPH_ANALYZE_EXPR.  It is supposed to mark
   decls referenced from frontend specific constructs; it will be called
   only for language-specific tree nodes.

   Here we must deal with member pointers.  */

tree
cxx_callgraph_analyze_expr (tree *tp, int *walk_subtrees ATTRIBUTE_UNUSED,
			    tree from ATTRIBUTE_UNUSED)
{
  tree t = *tp;

  switch (TREE_CODE (t))
    {
    case PTRMEM_CST:
      if (TYPE_PTRMEMFUNC_P (TREE_TYPE (t)))
	cgraph_mark_needed_node (cgraph_node (PTRMEM_CST_MEMBER (t)));
      break;
    case BASELINK:
      if (TREE_CODE (BASELINK_FUNCTIONS (t)) == FUNCTION_DECL)
	cgraph_mark_needed_node (cgraph_node (BASELINK_FUNCTIONS (t)));
      break;
    case VAR_DECL:
      if (DECL_VTABLE_OR_VTT_P (t))
	{
	  /* The ABI requires that all virtual tables be emitted
	     whenever one of them is.  */
	  tree vtbl;
	  for (vtbl = CLASSTYPE_VTABLES (DECL_CONTEXT (t));
	       vtbl;
	       vtbl = TREE_CHAIN (vtbl))
	    mark_decl_referenced (vtbl);
	}
      else if (DECL_CONTEXT (t)
	       && TREE_CODE (DECL_CONTEXT (t)) == FUNCTION_DECL)
	/* If we need a static variable in a function, then we
	   need the containing function.  */
	mark_decl_referenced (DECL_CONTEXT (t));
      break;
    default:
      break;
    }

  return NULL;
}

/* Java requires that we be able to reference a local address for a
   method, and not be confused by PLT entries.  If hidden aliases are
   supported, emit one for each java function that we've emitted.  */

static void
build_java_method_aliases (void)
{
  struct cgraph_node *node;

#ifndef HAVE_GAS_HIDDEN
  return;
#endif

  for (node = cgraph_nodes; node ; node = node->next)
    {
      tree fndecl = node->decl;

      if (TREE_ASM_WRITTEN (fndecl)
	  && DECL_CONTEXT (fndecl)
	  && TYPE_P (DECL_CONTEXT (fndecl))
	  && TYPE_FOR_JAVA (DECL_CONTEXT (fndecl))
	  && TARGET_USE_LOCAL_THUNK_ALIAS_P (fndecl))
	{
	  /* Mangle the name in a predictable way; we need to reference
	     this from a java compiled object file.  */
	  tree oid, nid, alias;
	  const char *oname;
	  char *nname;

	  oid = DECL_ASSEMBLER_NAME (fndecl);
	  oname = IDENTIFIER_POINTER (oid);
	  gcc_assert (oname[0] == '_' && oname[1] == 'Z');
	  nname = ACONCAT (("_ZGA", oname+2, NULL));
	  nid = get_identifier (nname);

	  alias = make_alias_for (fndecl, nid);
	  TREE_PUBLIC (alias) = 1;
	  DECL_VISIBILITY (alias) = VISIBILITY_HIDDEN;

	  assemble_alias (alias, oid);
	}
    }
}

/* This routine is called from the last rule in yyparse ().
   Its job is to create all the code needed to initialize and
   destroy the global aggregates.  We do the destruction
   first, since that way we only need to reverse the decls once.  */

void
cp_finish_file (void)
{
  tree vars;
  bool reconsider;
  size_t i;
  location_t locus;
  unsigned ssdf_count = 0;
  int retries = 0;
  tree decl;

  locus = input_location;
  at_eof = 1;

  /* Bad parse errors.  Just forget about it.  */
  if (! global_bindings_p () || current_class_type || decl_namespace_list)
    return;

  if (pch_file)
    c_common_write_pch ();

#ifdef USE_MAPPED_LOCATION
  /* FIXME - huh? */
#else
  /* Otherwise, GDB can get confused, because in only knows
     about source for LINENO-1 lines.  */
  input_line -= 1;
#endif

  /* We now have to write out all the stuff we put off writing out.
     These include:

       o Template specializations that we have not yet instantiated,
	 but which are needed.
       o Initialization and destruction for non-local objects with
	 static storage duration.  (Local objects with static storage
	 duration are initialized when their scope is first entered,
	 and are cleaned up via atexit.)
       o Virtual function tables.

     All of these may cause others to be needed.  For example,
     instantiating one function may cause another to be needed, and
     generating the initializer for an object may cause templates to be
     instantiated, etc., etc.  */

  timevar_push (TV_VARCONST);

  emit_support_tinfos ();

  do
    {
      tree t;
      tree decl;

      reconsider = false;

      /* If there are templates that we've put off instantiating, do
	 them now.  */
      instantiate_pending_templates (retries);
      ggc_collect ();

      /* Write out virtual tables as required.  Note that writing out
	 the virtual table for a template class may cause the
	 instantiation of members of that class.  If we write out
	 vtables then we remove the class from our list so we don't
	 have to look at it again.  */

      while (keyed_classes != NULL_TREE
	     && maybe_emit_vtables (TREE_VALUE (keyed_classes)))
	{
	  reconsider = true;
	  keyed_classes = TREE_CHAIN (keyed_classes);
	}

      t = keyed_classes;
      if (t != NULL_TREE)
	{
	  tree next = TREE_CHAIN (t);

	  while (next)
	    {
	      if (maybe_emit_vtables (TREE_VALUE (next)))
		{
		  reconsider = true;
		  TREE_CHAIN (t) = TREE_CHAIN (next);
		}
	      else
		t = next;

	      next = TREE_CHAIN (t);
	    }
	}

      /* Write out needed type info variables.  We have to be careful
	 looping through unemitted decls, because emit_tinfo_decl may
	 cause other variables to be needed. New elements will be
	 appended, and we remove from the vector those that actually
	 get emitted.  */
      for (i = VEC_length (tree, unemitted_tinfo_decls);
	   VEC_iterate (tree, unemitted_tinfo_decls, --i, t);)
	if (emit_tinfo_decl (t))
	  {
	    reconsider = true;
	    VEC_unordered_remove (tree, unemitted_tinfo_decls, i);
	  }

      /* The list of objects with static storage duration is built up
	 in reverse order.  We clear STATIC_AGGREGATES so that any new
	 aggregates added during the initialization of these will be
	 initialized in the correct order when we next come around the
	 loop.  */
      vars = prune_vars_needing_no_initialization (&static_aggregates);

      if (vars)
	{
	  /* We need to start a new initialization function each time
	     through the loop.  That's because we need to know which
	     vtables have been referenced, and TREE_SYMBOL_REFERENCED
	     isn't computed until a function is finished, and written
	     out.  That's a deficiency in the back-end.  When this is
	     fixed, these initialization functions could all become
	     inline, with resulting performance improvements.  */
	  tree ssdf_body;

	  /* Set the line and file, so that it is obviously not from
	     the source file.  */
	  input_location = locus;
	  ssdf_body = start_static_storage_duration_function (ssdf_count);

	  /* Make sure the back end knows about all the variables.  */
	  write_out_vars (vars);

	  /* First generate code to do all the initializations.  */
	  if (vars)
	    do_static_initialization_or_destruction (vars, /*initp=*/true);

	  /* Then, generate code to do all the destructions.  Do these
	     in reverse order so that the most recently constructed
	     variable is the first destroyed.  If we're using
	     __cxa_atexit, then we don't need to do this; functions
	     were registered at initialization time to destroy the
	     local statics.  */
	  if (!flag_use_cxa_atexit && vars)
	    {
	      vars = nreverse (vars);
	      do_static_initialization_or_destruction (vars, /*initp=*/false);
	    }
	  else
	    vars = NULL_TREE;

	  /* Finish up the static storage duration function for this
	     round.  */
	  input_location = locus;
	  finish_static_storage_duration_function (ssdf_body);

	  /* All those initializations and finalizations might cause
	     us to need more inline functions, more template
	     instantiations, etc.  */
	  reconsider = true;
	  ssdf_count++;
#ifdef USE_MAPPED_LOCATION
	  /* ??? */
#else
	  locus.line++;
#endif
	}

      /* Go through the set of inline functions whose bodies have not
	 been emitted yet.  If out-of-line copies of these functions
	 are required, emit them.  */
      for (i = 0; VEC_iterate (tree, deferred_fns, i, decl); ++i)
	{
	  /* Does it need synthesizing?  */
	  if (DECL_ARTIFICIAL (decl) && ! DECL_INITIAL (decl)
	      && (! DECL_REALLY_EXTERN (decl) || DECL_INLINE (decl)))
	    {
	      /* Even though we're already at the top-level, we push
		 there again.  That way, when we pop back a few lines
		 hence, all of our state is restored.  Otherwise,
		 finish_function doesn't clean things up, and we end
		 up with CURRENT_FUNCTION_DECL set.  */
	      push_to_top_level ();
	      /* The decl's location will mark where it was first
		 needed.  Save that so synthesize method can indicate
		 where it was needed from, in case of error  */
	      input_location = DECL_SOURCE_LOCATION (decl);
	      synthesize_method (decl);
	      pop_from_top_level ();
	      reconsider = true;
	    }

	  if (!DECL_SAVED_TREE (decl))
	    continue;

	  /* We lie to the back-end, pretending that some functions
	     are not defined when they really are.  This keeps these
	     functions from being put out unnecessarily.  But, we must
	     stop lying when the functions are referenced, or if they
	     are not comdat since they need to be put out now.  If
	     DECL_INTERFACE_KNOWN, then we have already set
	     DECL_EXTERNAL appropriately, so there's no need to check
	     again, and we do not want to clear DECL_EXTERNAL if a
	     previous call to import_export_decl set it.

	     This is done in a separate for cycle, because if some
	     deferred function is contained in another deferred
	     function later in deferred_fns varray,
	     rest_of_compilation would skip this function and we
	     really cannot expand the same function twice.  */
	  import_export_decl (decl);
	  if (DECL_NOT_REALLY_EXTERN (decl)
	      && DECL_INITIAL (decl)
	      && decl_needed_p (decl))
	    DECL_EXTERNAL (decl) = 0;

	  /* If we're going to need to write this function out, and
	     there's already a body for it, create RTL for it now.
	     (There might be no body if this is a method we haven't
	     gotten around to synthesizing yet.)  */
	  if (!DECL_EXTERNAL (decl)
	      && decl_needed_p (decl)
	      && !TREE_ASM_WRITTEN (decl)
	      && !cgraph_node (decl)->local.finalized)
	    {
	      /* We will output the function; no longer consider it in this
		 loop.  */
	      DECL_DEFER_OUTPUT (decl) = 0;
	      /* Generate RTL for this function now that we know we
		 need it.  */
	      expand_or_defer_fn (decl);
	      /* If we're compiling -fsyntax-only pretend that this
		 function has been written out so that we don't try to
		 expand it again.  */
	      if (flag_syntax_only)
		TREE_ASM_WRITTEN (decl) = 1;
	      reconsider = true;
	    }
	}

      if (walk_namespaces (wrapup_globals_for_namespace, /*data=*/0))
	reconsider = true;

      /* Static data members are just like namespace-scope globals.  */
      for (i = 0; VEC_iterate (tree, pending_statics, i, decl); ++i)
	{
	  if (var_finalized_p (decl) || DECL_REALLY_EXTERN (decl))
	    continue;
	  import_export_decl (decl);
	  /* If this static data member is needed, provide it to the
	     back end.  */
	  if (DECL_NOT_REALLY_EXTERN (decl) && decl_needed_p (decl))
	    DECL_EXTERNAL (decl) = 0;
	}
      if (VEC_length (tree, pending_statics) != 0
	  && wrapup_global_declarations (VEC_address (tree, pending_statics),
					 VEC_length (tree, pending_statics)))
	reconsider = true;

      retries++;
    }
  while (reconsider);

  /* All used inline functions must have a definition at this point.  */
  for (i = 0; VEC_iterate (tree, deferred_fns, i, decl); ++i)
    {
      if (/* Check online inline functions that were actually used.  */
	  TREE_USED (decl) && DECL_DECLARED_INLINE_P (decl)
	  /* If the definition actually was available here, then the
	     fact that the function was not defined merely represents
	     that for some reason (use of a template repository,
	     #pragma interface, etc.) we decided not to emit the
	     definition here.  */
	  && !DECL_INITIAL (decl)
	  /* An explicit instantiation can be used to specify
	     that the body is in another unit. It will have
	     already verified there was a definition.  */
	  && !DECL_EXPLICIT_INSTANTIATION (decl))
	{
	  warning (0, "inline function %q+D used but never defined", decl);
	  /* Avoid a duplicate warning from check_global_declaration_1.  */
	  TREE_NO_WARNING (decl) = 1;
	}
    }

  /* We give C linkage to static constructors and destructors.  */
  push_lang_context (lang_name_c);

  /* Generate initialization and destruction functions for all
     priorities for which they are required.  */
  if (priority_info_map)
    splay_tree_foreach (priority_info_map,
			generate_ctor_and_dtor_functions_for_priority,
			/*data=*/&locus);
  else
    {
      /* If we have a ctor or this is obj-c++ and we need a static init,
	 call generate_ctor_or_dtor_function.  */
      if (static_ctors || (c_dialect_objc () && objc_static_init_needed_p ()))
	generate_ctor_or_dtor_function (/*constructor_p=*/true,
					DEFAULT_INIT_PRIORITY, &locus);
      if (static_dtors)
	generate_ctor_or_dtor_function (/*constructor_p=*/false,
					DEFAULT_INIT_PRIORITY, &locus);
    }

  /* We're done with the splay-tree now.  */
  if (priority_info_map)
    splay_tree_delete (priority_info_map);

  /* Generate any missing aliases.  */
  maybe_apply_pending_pragma_weaks ();

  /* We're done with static constructors, so we can go back to "C++"
     linkage now.  */
  pop_lang_context ();

  cgraph_finalize_compilation_unit ();
  cgraph_optimize ();

  /* Now, issue warnings about static, but not defined, functions,
     etc., and emit debugging information.  */
  walk_namespaces (wrapup_globals_for_namespace, /*data=*/&reconsider);
  if (VEC_length (tree, pending_statics) != 0)
    {
      check_global_declarations (VEC_address (tree, pending_statics),
				 VEC_length (tree, pending_statics));
      emit_debug_global_declarations (VEC_address (tree, pending_statics),
				      VEC_length (tree, pending_statics));
    }

  /* Generate hidden aliases for Java.  */
  build_java_method_aliases ();

  finish_repo ();

  /* The entire file is now complete.  If requested, dump everything
     to a file.  */
  {
    int flags;
    FILE *stream = dump_begin (TDI_tu, &flags);

    if (stream)
      {
	dump_node (global_namespace, flags & ~TDF_SLIM, stream);
	dump_end (TDI_tu, stream);
      }
  }

  timevar_pop (TV_VARCONST);

  if (flag_detailed_statistics)
    {
      dump_tree_statistics ();
      dump_time_statistics ();
    }
  input_location = locus;

#ifdef ENABLE_CHECKING
  validate_conversion_obstack ();
#endif /* ENABLE_CHECKING */
}

/* FN is an OFFSET_REF, DOTSTAR_EXPR or MEMBER_REF indicating the
   function to call in parse-tree form; it has not yet been
   semantically analyzed.  ARGS are the arguments to the function.
   They have already been semantically analyzed.  */

tree
build_offset_ref_call_from_tree (tree fn, tree args)
{
  tree orig_fn;
  tree orig_args;
  tree expr;
  tree object;

  orig_fn = fn;
  orig_args = args;
  object = TREE_OPERAND (fn, 0);

  if (processing_template_decl)
    {
      gcc_assert (TREE_CODE (fn) == DOTSTAR_EXPR
		  || TREE_CODE (fn) == MEMBER_REF);
      if (type_dependent_expression_p (fn)
	  || any_type_dependent_arguments_p (args))
	return build_min_nt (CALL_EXPR, fn, args, NULL_TREE);

      /* Transform the arguments and add the implicit "this"
	 parameter.  That must be done before the FN is transformed
	 because we depend on the form of FN.  */
      args = build_non_dependent_args (args);
      if (TREE_CODE (fn) == DOTSTAR_EXPR)
	object = build_unary_op (ADDR_EXPR, object, 0);
      object = build_non_dependent_expr (object);
      args = tree_cons (NULL_TREE, object, args);
      /* Now that the arguments are done, transform FN.  */
      fn = build_non_dependent_expr (fn);
    }

  /* A qualified name corresponding to a bound pointer-to-member is
     represented as an OFFSET_REF:

	struct B { void g(); };
	void (B::*p)();
	void B::g() { (this->*p)(); }  */
  if (TREE_CODE (fn) == OFFSET_REF)
    {
      tree object_addr = build_unary_op (ADDR_EXPR, object, 0);
      fn = TREE_OPERAND (fn, 1);
      fn = get_member_function_from_ptrfunc (&object_addr, fn);
      args = tree_cons (NULL_TREE, object_addr, args);
    }

  expr = build_function_call (fn, args);
  if (processing_template_decl && expr != error_mark_node)
    return build_min_non_dep (CALL_EXPR, expr, orig_fn, orig_args, NULL_TREE);
  return expr;
}


void
check_default_args (tree x)
{
  tree arg = TYPE_ARG_TYPES (TREE_TYPE (x));
  bool saw_def = false;
  int i = 0 - (TREE_CODE (TREE_TYPE (x)) == METHOD_TYPE);
  for (; arg && arg != void_list_node; arg = TREE_CHAIN (arg), ++i)
    {
      if (TREE_PURPOSE (arg))
	saw_def = true;
      else if (saw_def)
	{
	  error ("default argument missing for parameter %P of %q+#D", i, x);
	  TREE_PURPOSE (arg) = error_mark_node;
	}
    }
}

/* Mark DECL (either a _DECL or a BASELINK) as "used" in the program.
   If DECL is a specialization or implicitly declared class member,
   generate the actual definition.  */

void
mark_used (tree decl)
{
  HOST_WIDE_INT saved_processing_template_decl = 0;

  /* If DECL is a BASELINK for a single function, then treat it just
     like the DECL for the function.  Otherwise, if the BASELINK is
     for an overloaded function, we don't know which function was
     actually used until after overload resolution.  */
  if (TREE_CODE (decl) == BASELINK)
    {
      decl = BASELINK_FUNCTIONS (decl);
      if (really_overloaded_fn (decl))
	return;
      decl = OVL_CURRENT (decl);
    }

  TREE_USED (decl) = 1;
  if (DECL_CLONED_FUNCTION_P (decl))
    TREE_USED (DECL_CLONED_FUNCTION (decl)) = 1;
  /* If we don't need a value, then we don't need to synthesize DECL.  */
  if (skip_evaluation)
    return;
  /* Normally, we can wait until instantiation-time to synthesize
     DECL.  However, if DECL is a static data member initialized with
     a constant, we need the value right now because a reference to
     such a data member is not value-dependent.  */
  if (TREE_CODE (decl) == VAR_DECL
      && DECL_INITIALIZED_BY_CONSTANT_EXPRESSION_P (decl)
      && DECL_CLASS_SCOPE_P (decl))
    {
      /* Don't try to instantiate members of dependent types.  We
	 cannot just use dependent_type_p here because this function
	 may be called from fold_non_dependent_expr, and then we may
	 see dependent types, even though processing_template_decl
	 will not be set.  */
      if (CLASSTYPE_TEMPLATE_INFO ((DECL_CONTEXT (decl)))
	  && uses_template_parms (CLASSTYPE_TI_ARGS (DECL_CONTEXT (decl))))
	return;
      /* Pretend that we are not in a template, even if we are, so
	 that the static data member initializer will be processed.  */
      saved_processing_template_decl = processing_template_decl;
      processing_template_decl = 0;
    }

  if (processing_template_decl)
    return;

  if (TREE_CODE (decl) == FUNCTION_DECL && DECL_DECLARED_INLINE_P (decl)
      && !TREE_ASM_WRITTEN (decl))
    /* Remember it, so we can check it was defined.  */
    {
      if (DECL_DEFERRED_FN (decl))
	return;

      /* Remember the current location for a function we will end up
	 synthesizing.  Then we can inform the user where it was
	 required in the case of error.  */
      if (DECL_ARTIFICIAL (decl) && DECL_NONSTATIC_MEMBER_FUNCTION_P (decl)
	  && !DECL_THUNK_P (decl))
	DECL_SOURCE_LOCATION (decl) = input_location;

      note_vague_linkage_fn (decl);
    }

  /* Is it a synthesized method that needs to be synthesized?  */
  if (TREE_CODE (decl) == FUNCTION_DECL
      && DECL_NONSTATIC_MEMBER_FUNCTION_P (decl)
      && DECL_ARTIFICIAL (decl)
      && !DECL_THUNK_P (decl)
      && ! DECL_INITIAL (decl)
      /* Kludge: don't synthesize for default args.  Unfortunately this
	 rules out initializers of namespace-scoped objects too, but
	 it's sort-of ok if the implicit ctor or dtor decl keeps
	 pointing to the class location.  */
      && current_function_decl)
    {
      synthesize_method (decl);
      /* If we've already synthesized the method we don't need to
	 do the instantiation test below.  */
    }
  else if ((DECL_NON_THUNK_FUNCTION_P (decl) || TREE_CODE (decl) == VAR_DECL)
	   && DECL_LANG_SPECIFIC (decl) && DECL_TEMPLATE_INFO (decl)
	   && (!DECL_EXPLICIT_INSTANTIATION (decl)
	       || (TREE_CODE (decl) == FUNCTION_DECL
		   && DECL_INLINE (DECL_TEMPLATE_RESULT
				   (template_for_substitution (decl))))
	       /* We need to instantiate static data members so that there
		  initializers are available in integral constant
		  expressions.  */
	       || (TREE_CODE (decl) == VAR_DECL
		   && DECL_INITIALIZED_BY_CONSTANT_EXPRESSION_P (decl))))
    /* If this is a function or variable that is an instance of some
       template, we now know that we will need to actually do the
       instantiation. We check that DECL is not an explicit
       instantiation because that is not checked in instantiate_decl.

       We put off instantiating functions in order to improve compile
       times.  Maintaining a stack of active functions is expensive,
       and the inliner knows to instantiate any functions it might
       need.  Therefore, we always try to defer instantiation.  */
    instantiate_decl (decl, /*defer_ok=*/true,
		      /*expl_inst_class_mem_p=*/false);

  processing_template_decl = saved_processing_template_decl;
}

#include "gt-cp-decl2.h"
