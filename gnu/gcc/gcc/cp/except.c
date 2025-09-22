/* Handle exceptional things in C++.
   Copyright (C) 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005  Free Software Foundation, Inc.
   Contributed by Michael Tiemann <tiemann@cygnus.com>
   Rewritten by Mike Stump <mrs@cygnus.com>, based upon an
   initial re-implementation courtesy Tad Hunt.

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
#include "rtl.h"
#include "expr.h"
#include "libfuncs.h"
#include "cp-tree.h"
#include "flags.h"
#include "output.h"
#include "except.h"
#include "toplev.h"
#include "tree-inline.h"
#include "tree-iterator.h"
#include "target.h"

static void push_eh_cleanup (tree);
static tree prepare_eh_type (tree);
static tree build_eh_type_type (tree);
static tree do_begin_catch (void);
static int dtor_nothrow (tree);
static tree do_end_catch (tree);
static bool decl_is_java_type (tree decl, int err);
static void initialize_handler_parm (tree, tree);
static tree do_allocate_exception (tree);
static tree wrap_cleanups_r (tree *, int *, void *);
static int complete_ptr_ref_or_void_ptr_p (tree, tree);
static bool is_admissible_throw_operand (tree);
static int can_convert_eh (tree, tree);
static tree cp_protect_cleanup_actions (void);

/* Sets up all the global eh stuff that needs to be initialized at the
   start of compilation.  */

void
init_exception_processing (void)
{
  tree tmp;

  /* void std::terminate (); */
  push_namespace (std_identifier);
  tmp = build_function_type (void_type_node, void_list_node);
  terminate_node = build_cp_library_fn_ptr ("terminate", tmp);
  TREE_THIS_VOLATILE (terminate_node) = 1;
  TREE_NOTHROW (terminate_node) = 1;
  pop_namespace ();

  /* void __cxa_call_unexpected(void *); */
  tmp = tree_cons (NULL_TREE, ptr_type_node, void_list_node);
  tmp = build_function_type (void_type_node, tmp);
  call_unexpected_node
    = push_throw_library_fn (get_identifier ("__cxa_call_unexpected"), tmp);

  eh_personality_libfunc = init_one_libfunc (USING_SJLJ_EXCEPTIONS
					     ? "__gxx_personality_sj0"
					     : "__gxx_personality_v0");
  if (targetm.arm_eabi_unwinder)
    unwind_resume_libfunc = init_one_libfunc ("__cxa_end_cleanup");
  else
    default_init_unwind_resume_libfunc ();

  lang_eh_runtime_type = build_eh_type_type;
  lang_protect_cleanup_actions = &cp_protect_cleanup_actions;
}

/* Returns an expression to be executed if an unhandled exception is
   propagated out of a cleanup region.  */

static tree
cp_protect_cleanup_actions (void)
{
  /* [except.terminate]

     When the destruction of an object during stack unwinding exits
     using an exception ... void terminate(); is called.  */
  return build_call (terminate_node, NULL_TREE);
}

static tree
prepare_eh_type (tree type)
{
  if (type == NULL_TREE)
    return type;
  if (type == error_mark_node)
    return error_mark_node;

  /* peel back references, so they match.  */
  type = non_reference (type);

  /* Peel off cv qualifiers.  */
  type = TYPE_MAIN_VARIANT (type);

  return type;
}

/* Return the type info for TYPE as used by EH machinery.  */
tree
eh_type_info (tree type)
{
  tree exp;

  if (type == NULL_TREE || type == error_mark_node)
    return type;

  if (decl_is_java_type (type, 0))
    exp = build_java_class_ref (TREE_TYPE (type));
  else
    exp = get_tinfo_decl (type);

  return exp;
}

/* Build the address of a typeinfo decl for use in the runtime
   matching field of the exception model.  */

static tree
build_eh_type_type (tree type)
{
  tree exp = eh_type_info (type);

  if (!exp)
    return NULL;

  mark_used (exp);

  return convert (ptr_type_node, build_address (exp));
}

tree
build_exc_ptr (void)
{
  return build0 (EXC_PTR_EXPR, ptr_type_node);
}

/* Build up a call to __cxa_get_exception_ptr so that we can build a
   copy constructor for the thrown object.  */

static tree
do_get_exception_ptr (void)
{
  tree fn;

  fn = get_identifier ("__cxa_get_exception_ptr");
  if (!get_global_value_if_present (fn, &fn))
    {
      /* Declare void* __cxa_get_exception_ptr (void *).  */
      tree tmp = tree_cons (NULL_TREE, ptr_type_node, void_list_node);
      fn = push_library_fn (fn, build_function_type (ptr_type_node, tmp));
    }

  return build_function_call (fn, tree_cons (NULL_TREE, build_exc_ptr (),
					     NULL_TREE));
}

/* Build up a call to __cxa_begin_catch, to tell the runtime that the
   exception has been handled.  */

static tree
do_begin_catch (void)
{
  tree fn;

  fn = get_identifier ("__cxa_begin_catch");
  if (!get_global_value_if_present (fn, &fn))
    {
      /* Declare void* __cxa_begin_catch (void *).  */
      tree tmp = tree_cons (NULL_TREE, ptr_type_node, void_list_node);
      fn = push_library_fn (fn, build_function_type (ptr_type_node, tmp));
    }

  return build_function_call (fn, tree_cons (NULL_TREE, build_exc_ptr (),
					     NULL_TREE));
}

/* Returns nonzero if cleaning up an exception of type TYPE (which can be
   NULL_TREE for a ... handler) will not throw an exception.  */

static int
dtor_nothrow (tree type)
{
  if (type == NULL_TREE)
    return 0;

  if (!CLASS_TYPE_P (type))
    return 1;

  if (CLASSTYPE_LAZY_DESTRUCTOR (type))
    lazily_declare_fn (sfk_destructor, type);

  return TREE_NOTHROW (CLASSTYPE_DESTRUCTORS (type));
}

/* Build up a call to __cxa_end_catch, to destroy the exception object
   for the current catch block if no others are currently using it.  */

static tree
do_end_catch (tree type)
{
  tree fn, cleanup;

  fn = get_identifier ("__cxa_end_catch");
  if (!get_global_value_if_present (fn, &fn))
    {
      /* Declare void __cxa_end_catch ().  */
      fn = push_void_library_fn (fn, void_list_node);
      /* This can throw if the destructor for the exception throws.  */
      TREE_NOTHROW (fn) = 0;
    }

  cleanup = build_function_call (fn, NULL_TREE);
  TREE_NOTHROW (cleanup) = dtor_nothrow (type);

  return cleanup;
}

/* This routine creates the cleanup for the current exception.  */

static void
push_eh_cleanup (tree type)
{
  finish_decl_cleanup (NULL_TREE, do_end_catch (type));
}

/* Return nonzero value if DECL is a Java type suitable for catch or
   throw.  */

static bool
decl_is_java_type (tree decl, int err)
{
  bool r = (TREE_CODE (decl) == POINTER_TYPE
	    && TREE_CODE (TREE_TYPE (decl)) == RECORD_TYPE
	    && TYPE_FOR_JAVA (TREE_TYPE (decl)));

  if (err)
    {
      if (TREE_CODE (decl) == REFERENCE_TYPE
	  && TREE_CODE (TREE_TYPE (decl)) == RECORD_TYPE
	  && TYPE_FOR_JAVA (TREE_TYPE (decl)))
	{
	  /* Can't throw a reference.  */
	  error ("type %qT is disallowed in Java %<throw%> or %<catch%>",
		 decl);
	}

      if (r)
	{
	  tree jthrow_node
	    = IDENTIFIER_GLOBAL_VALUE (get_identifier ("jthrowable"));

	  if (jthrow_node == NULL_TREE)
	    fatal_error
	      ("call to Java %<catch%> or %<throw%> with %<jthrowable%> undefined");

	  jthrow_node = TREE_TYPE (TREE_TYPE (jthrow_node));

	  if (! DERIVED_FROM_P (jthrow_node, TREE_TYPE (decl)))
	    {
	      /* Thrown object must be a Throwable.  */
	      error ("type %qT is not derived from %<java::lang::Throwable%>",
		     TREE_TYPE (decl));
	    }
	}
    }

  return r;
}

/* Select the personality routine to be used for exception handling,
   or issue an error if we need two different ones in the same
   translation unit.
   ??? At present eh_personality_libfunc is set to
   __gxx_personality_(sj|v)0 in init_exception_processing - should it
   be done here instead?  */
void
choose_personality_routine (enum languages lang)
{
  static enum {
    chose_none,
    chose_cpp,
    chose_java,
    gave_error
  } state;

  switch (state)
    {
    case gave_error:
      return;

    case chose_cpp:
      if (lang != lang_cplusplus)
	goto give_error;
      return;

    case chose_java:
      if (lang != lang_java)
	goto give_error;
      return;

    case chose_none:
      ; /* Proceed to language selection.  */
    }

  switch (lang)
    {
    case lang_cplusplus:
      state = chose_cpp;
      break;

    case lang_java:
      state = chose_java;
      eh_personality_libfunc = init_one_libfunc (USING_SJLJ_EXCEPTIONS
						 ? "__gcj_personality_sj0"
						 : "__gcj_personality_v0");
      break;

    default:
      gcc_unreachable ();
    }
  return;

 give_error:
  error ("mixing C++ and Java catches in a single translation unit");
  state = gave_error;
}

/* Initialize the catch parameter DECL.  */

static void
initialize_handler_parm (tree decl, tree exp)
{
  tree init;
  tree init_type;

  /* Make sure we mark the catch param as used, otherwise we'll get a
     warning about an unused ((anonymous)).  */
  TREE_USED (decl) = 1;

  /* Figure out the type that the initializer is.  Pointers are returned
     adjusted by value from __cxa_begin_catch.  Others are returned by
     reference.  */
  init_type = TREE_TYPE (decl);
  if (!POINTER_TYPE_P (init_type))
    init_type = build_reference_type (init_type);

  choose_personality_routine (decl_is_java_type (init_type, 0)
			      ? lang_java : lang_cplusplus);

  /* Since pointers are passed by value, initialize a reference to
     pointer catch parm with the address of the temporary.  */
  if (TREE_CODE (init_type) == REFERENCE_TYPE
      && TYPE_PTR_P (TREE_TYPE (init_type)))
    exp = build_unary_op (ADDR_EXPR, exp, 1);

  exp = ocp_convert (init_type, exp, CONV_IMPLICIT|CONV_FORCE_TEMP, 0);

  init = convert_from_reference (exp);

  /* If the constructor for the catch parm exits via an exception, we
     must call terminate.  See eh23.C.  */
  if (TYPE_NEEDS_CONSTRUCTING (TREE_TYPE (decl)))
    {
      /* Generate the copy constructor call directly so we can wrap it.
	 See also expand_default_init.  */
      init = ocp_convert (TREE_TYPE (decl), init,
			  CONV_IMPLICIT|CONV_FORCE_TEMP, 0);
      init = build1 (MUST_NOT_THROW_EXPR, TREE_TYPE (init), init);
    }

  decl = pushdecl (decl);

  start_decl_1 (decl, true);
  cp_finish_decl (decl, init, /*init_const_expr_p=*/false, NULL_TREE,
		  LOOKUP_ONLYCONVERTING|DIRECT_BIND);
}

/* Call this to start a catch block.  DECL is the catch parameter.  */

tree
expand_start_catch_block (tree decl)
{
  tree exp;
  tree type;

  if (! doing_eh (1))
    return NULL_TREE;

  /* Make sure this declaration is reasonable.  */
  if (decl && !complete_ptr_ref_or_void_ptr_p (TREE_TYPE (decl), NULL_TREE))
    decl = error_mark_node;

  if (decl)
    type = prepare_eh_type (TREE_TYPE (decl));
  else
    type = NULL_TREE;

  if (decl && decl_is_java_type (type, 1))
    {
      /* Java only passes object via pointer and doesn't require
	 adjusting.  The java object is immediately before the
	 generic exception header.  */
      exp = build_exc_ptr ();
      exp = build1 (NOP_EXPR, build_pointer_type (type), exp);
      exp = build2 (MINUS_EXPR, TREE_TYPE (exp), exp,
		    TYPE_SIZE_UNIT (TREE_TYPE (exp)));
      exp = build_indirect_ref (exp, NULL);
      initialize_handler_parm (decl, exp);
      return type;
    }

  /* Call __cxa_end_catch at the end of processing the exception.  */
  push_eh_cleanup (type);

  /* If there's no decl at all, then all we need to do is make sure
     to tell the runtime that we've begun handling the exception.  */
  if (decl == NULL || decl == error_mark_node)
    finish_expr_stmt (do_begin_catch ());

  /* If the C++ object needs constructing, we need to do that before
     calling __cxa_begin_catch, so that std::uncaught_exception gets
     the right value during the copy constructor.  */
  else if (flag_use_cxa_get_exception_ptr
	   && TYPE_NEEDS_CONSTRUCTING (TREE_TYPE (decl)))
    {
      exp = do_get_exception_ptr ();
      initialize_handler_parm (decl, exp);
      finish_expr_stmt (do_begin_catch ());
    }

  /* Otherwise the type uses a bitwise copy, and we don't have to worry
     about the value of std::uncaught_exception and therefore can do the
     copy with the return value of __cxa_end_catch instead.  */
  else
    {
      tree init = do_begin_catch ();
      tree init_type = type;

      /* Pointers are passed by values, everything else by reference.  */
      if (!TYPE_PTR_P (type))
	init_type = build_pointer_type (type);
      if (init_type != TREE_TYPE (init))
	init = build1 (NOP_EXPR, init_type, init);
      exp = create_temporary_var (init_type);
      DECL_REGISTER (exp) = 1;
      cp_finish_decl (exp, init, /*init_const_expr=*/false,
		      NULL_TREE, LOOKUP_ONLYCONVERTING);
      initialize_handler_parm (decl, exp);
    }

  return type;
}


/* Call this to end a catch block.  Its responsible for emitting the
   code to handle jumping back to the correct place, and for emitting
   the label to jump to if this catch block didn't match.  */

void
expand_end_catch_block (void)
{
  if (! doing_eh (1))
    return;

  /* The exception being handled is rethrown if control reaches the end of
     a handler of the function-try-block of a constructor or destructor.  */
  if (in_function_try_handler
      && (DECL_CONSTRUCTOR_P (current_function_decl)
	  || DECL_DESTRUCTOR_P (current_function_decl)))
    finish_expr_stmt (build_throw (NULL_TREE));
}

tree
begin_eh_spec_block (void)
{
  tree r = build_stmt (EH_SPEC_BLOCK, NULL_TREE, NULL_TREE);
  add_stmt (r);
  EH_SPEC_STMTS (r) = push_stmt_list ();
  return r;
}

void
finish_eh_spec_block (tree raw_raises, tree eh_spec_block)
{
  tree raises;

  EH_SPEC_STMTS (eh_spec_block) = pop_stmt_list (EH_SPEC_STMTS (eh_spec_block));

  /* Strip cv quals, etc, from the specification types.  */
  for (raises = NULL_TREE;
       raw_raises && TREE_VALUE (raw_raises);
       raw_raises = TREE_CHAIN (raw_raises))
    {
      tree type = prepare_eh_type (TREE_VALUE (raw_raises));
      tree tinfo = eh_type_info (type);

      mark_used (tinfo);
      raises = tree_cons (NULL_TREE, type, raises);
    }

  EH_SPEC_RAISES (eh_spec_block) = raises;
}

/* Return a pointer to a buffer for an exception object of type TYPE.  */

static tree
do_allocate_exception (tree type)
{
  tree fn;

  fn = get_identifier ("__cxa_allocate_exception");
  if (!get_global_value_if_present (fn, &fn))
    {
      /* Declare void *__cxa_allocate_exception(size_t).  */
      tree tmp = tree_cons (NULL_TREE, size_type_node, void_list_node);
      fn = push_library_fn (fn, build_function_type (ptr_type_node, tmp));
    }

  return build_function_call (fn, tree_cons (NULL_TREE, size_in_bytes (type),
					     NULL_TREE));
}

/* Call __cxa_free_exception from a cleanup.  This is never invoked
   directly, but see the comment for stabilize_throw_expr.  */

static tree
do_free_exception (tree ptr)
{
  tree fn;

  fn = get_identifier ("__cxa_free_exception");
  if (!get_global_value_if_present (fn, &fn))
    {
      /* Declare void __cxa_free_exception (void *).  */
      fn = push_void_library_fn (fn, tree_cons (NULL_TREE, ptr_type_node,
						void_list_node));
    }

  return build_function_call (fn, tree_cons (NULL_TREE, ptr, NULL_TREE));
}

/* Wrap all cleanups for TARGET_EXPRs in MUST_NOT_THROW_EXPR.
   Called from build_throw via walk_tree_without_duplicates.  */

static tree
wrap_cleanups_r (tree *tp, int *walk_subtrees ATTRIBUTE_UNUSED,
		 void *data ATTRIBUTE_UNUSED)
{
  tree exp = *tp;
  tree cleanup;

  /* Don't walk into types.  */
  if (TYPE_P (exp))
    {
      *walk_subtrees = 0;
      return NULL_TREE;
    }
  if (TREE_CODE (exp) != TARGET_EXPR)
    return NULL_TREE;

  cleanup = TARGET_EXPR_CLEANUP (exp);
  if (cleanup)
    {
      cleanup = build1 (MUST_NOT_THROW_EXPR, void_type_node, cleanup);
      TARGET_EXPR_CLEANUP (exp) = cleanup;
    }

  /* Keep iterating.  */
  return NULL_TREE;
}

/* Build a throw expression.  */

tree
build_throw (tree exp)
{
  tree fn;

  if (exp == error_mark_node)
    return exp;

  if (processing_template_decl)
    {
      if (cfun)
	current_function_returns_abnormally = 1;
      return build_min (THROW_EXPR, void_type_node, exp);
    }

  if (exp == null_node)
    warning (0, "throwing NULL, which has integral, not pointer type");

  if (exp != NULL_TREE)
    {
      if (!is_admissible_throw_operand (exp))
	return error_mark_node;
    }

  if (! doing_eh (1))
    return error_mark_node;

  if (exp && decl_is_java_type (TREE_TYPE (exp), 1))
    {
      tree fn = get_identifier ("_Jv_Throw");
      if (!get_global_value_if_present (fn, &fn))
	{
	  /* Declare void _Jv_Throw (void *).  */
	  tree tmp = tree_cons (NULL_TREE, ptr_type_node, void_list_node);
	  tmp = build_function_type (ptr_type_node, tmp);
	  fn = push_throw_library_fn (fn, tmp);
	}
      else if (really_overloaded_fn (fn))
	{
	  error ("%qD should never be overloaded", fn);
	  return error_mark_node;
	}
      fn = OVL_CURRENT (fn);
      exp = build_function_call (fn, tree_cons (NULL_TREE, exp, NULL_TREE));
    }
  else if (exp)
    {
      tree throw_type;
      tree temp_type;
      tree cleanup;
      tree object, ptr;
      tree tmp;
      tree temp_expr, allocate_expr;
      bool elided;

      /* The CLEANUP_TYPE is the internal type of a destructor.  */
      if (!cleanup_type)
	{
	  tmp = void_list_node;
	  tmp = tree_cons (NULL_TREE, ptr_type_node, tmp);
	  tmp = build_function_type (void_type_node, tmp);
	  cleanup_type = build_pointer_type (tmp);
	}

      fn = get_identifier ("__cxa_throw");
      if (!get_global_value_if_present (fn, &fn))
	{
	  /* Declare void __cxa_throw (void*, void*, void (*)(void*)).  */
	  /* ??? Second argument is supposed to be "std::type_info*".  */
	  tmp = void_list_node;
	  tmp = tree_cons (NULL_TREE, cleanup_type, tmp);
	  tmp = tree_cons (NULL_TREE, ptr_type_node, tmp);
	  tmp = tree_cons (NULL_TREE, ptr_type_node, tmp);
	  tmp = build_function_type (void_type_node, tmp);
	  fn = push_throw_library_fn (fn, tmp);
	}

      /* [except.throw]

	 A throw-expression initializes a temporary object, the type
	 of which is determined by removing any top-level
	 cv-qualifiers from the static type of the operand of throw
	 and adjusting the type from "array of T" or "function return
	 T" to "pointer to T" or "pointer to function returning T"
	 respectively.  */
      temp_type = is_bitfield_expr_with_lowered_type (exp);
      if (!temp_type)
	temp_type = type_decays_to (TYPE_MAIN_VARIANT (TREE_TYPE (exp)));

      /* OK, this is kind of wacky.  The standard says that we call
	 terminate when the exception handling mechanism, after
	 completing evaluation of the expression to be thrown but
	 before the exception is caught (_except.throw_), calls a
	 user function that exits via an uncaught exception.

	 So we have to protect the actual initialization of the
	 exception object with terminate(), but evaluate the
	 expression first.  Since there could be temps in the
	 expression, we need to handle that, too.  We also expand
	 the call to __cxa_allocate_exception first (which doesn't
	 matter, since it can't throw).  */

      /* Allocate the space for the exception.  */
      allocate_expr = do_allocate_exception (temp_type);
      allocate_expr = get_target_expr (allocate_expr);
      ptr = TARGET_EXPR_SLOT (allocate_expr);
      object = build_nop (build_pointer_type (temp_type), ptr);
      object = build_indirect_ref (object, NULL);

      elided = (TREE_CODE (exp) == TARGET_EXPR);

      /* And initialize the exception object.  */
      if (CLASS_TYPE_P (temp_type))
	{
	  /* Call the copy constructor.  */
	  exp = (build_special_member_call
		 (object, complete_ctor_identifier,
		  build_tree_list (NULL_TREE, exp),
		  TREE_TYPE (object),
		  LOOKUP_NORMAL | LOOKUP_ONLYCONVERTING));
	  if (exp == error_mark_node)
	    {
	      error ("  in thrown expression");
	      return error_mark_node;
	    }
	}
      else
	exp = build2 (INIT_EXPR, temp_type, object,
		      decay_conversion (exp));

      /* Pre-evaluate the thrown expression first, since if we allocated
	 the space first we would have to deal with cleaning it up if
	 evaluating this expression throws.

	 The case where EXP the initializer is a cast or a function
	 returning a class is a bit of a grey area in the standard; it's
	 unclear whether or not it should be allowed to throw.  We used to
	 say no, as that allowed us to optimize this case without worrying
	 about deallocating the exception object if it does.  But that
	 conflicted with expectations (PR 13944) and the EDG compiler; now
	 we wrap the initialization in a TRY_CATCH_EXPR to call
	 do_free_exception rather than in a MUST_NOT_THROW_EXPR, for this
	 case only.

	 BUT: Issue 475 may do away with this inconsistency by removing the
	 terminate() in this situation.

	 Note that we don't check the return value from stabilize_init
	 because it will only return false in cases where elided is true,
	 and therefore we don't need to work around the failure to
	 preevaluate.  */
      temp_expr = NULL_TREE;
      stabilize_init (exp, &temp_expr);

      /* Wrap the initialization in a CLEANUP_POINT_EXPR so that cleanups
	 for temporaries within the initialization are run before the one
	 for the exception object, preserving LIFO order.  */
      exp = build1 (CLEANUP_POINT_EXPR, void_type_node, exp);

      if (elided)
	exp = build2 (TRY_CATCH_EXPR, void_type_node, exp,
		      do_free_exception (ptr));
      else
	exp = build1 (MUST_NOT_THROW_EXPR, void_type_node, exp);

      /* Prepend the allocation.  */
      exp = build2 (COMPOUND_EXPR, TREE_TYPE (exp), allocate_expr, exp);
      if (temp_expr)
	{
	  /* Prepend the calculation of the throw expression.  Also, force
	     any cleanups from the expression to be evaluated here so that
	     we don't have to do them during unwinding.  But first wrap
	     them in MUST_NOT_THROW_EXPR, since they are run after the
	     exception object is initialized.  */
	  walk_tree_without_duplicates (&temp_expr, wrap_cleanups_r, 0);
	  exp = build2 (COMPOUND_EXPR, TREE_TYPE (exp), temp_expr, exp);
	  exp = build1 (CLEANUP_POINT_EXPR, TREE_TYPE (exp), exp);
	}

      throw_type = build_eh_type_type (prepare_eh_type (TREE_TYPE (object)));

      if (TYPE_HAS_NONTRIVIAL_DESTRUCTOR (TREE_TYPE (object)))
	{
	  cleanup = lookup_fnfields (TYPE_BINFO (TREE_TYPE (object)),
				     complete_dtor_identifier, 0);
	  cleanup = BASELINK_FUNCTIONS (cleanup);
	  mark_used (cleanup);
	  cxx_mark_addressable (cleanup);
	  /* Pretend it's a normal function.  */
	  cleanup = build1 (ADDR_EXPR, cleanup_type, cleanup);
	}
      else
	cleanup = build_int_cst (cleanup_type, 0);

      tmp = tree_cons (NULL_TREE, cleanup, NULL_TREE);
      tmp = tree_cons (NULL_TREE, throw_type, tmp);
      tmp = tree_cons (NULL_TREE, ptr, tmp);
      /* ??? Indicate that this function call throws throw_type.  */
      tmp = build_function_call (fn, tmp);

      /* Tack on the initialization stuff.  */
      exp = build2 (COMPOUND_EXPR, TREE_TYPE (tmp), exp, tmp);
    }
  else
    {
      /* Rethrow current exception.  */

      tree fn = get_identifier ("__cxa_rethrow");
      if (!get_global_value_if_present (fn, &fn))
	{
	  /* Declare void __cxa_rethrow (void).  */
	  fn = push_throw_library_fn
	    (fn, build_function_type (void_type_node, void_list_node));
	}

      /* ??? Indicate that this function call allows exceptions of the type
	 of the enclosing catch block (if known).  */
      exp = build_function_call (fn, NULL_TREE);
    }

  exp = build1 (THROW_EXPR, void_type_node, exp);

  return exp;
}

/* Make sure TYPE is complete, pointer to complete, reference to
   complete, or pointer to cv void. Issue diagnostic on failure.
   Return the zero on failure and nonzero on success. FROM can be
   the expr or decl from whence TYPE came, if available.  */

static int
complete_ptr_ref_or_void_ptr_p (tree type, tree from)
{
  int is_ptr;

  /* Check complete.  */
  type = complete_type_or_else (type, from);
  if (!type)
    return 0;

  /* Or a pointer or ref to one, or cv void *.  */
  is_ptr = TREE_CODE (type) == POINTER_TYPE;
  if (is_ptr || TREE_CODE (type) == REFERENCE_TYPE)
    {
      tree core = TREE_TYPE (type);

      if (is_ptr && VOID_TYPE_P (core))
	/* OK */;
      else if (!complete_type_or_else (core, from))
	return 0;
    }
  return 1;
}

/* Return truth-value if EXPRESSION is admissible in throw-expression,
   i.e. if it is not of incomplete type or a pointer/reference to such
   a type or of an abstract class type.  */

static bool
is_admissible_throw_operand (tree expr)
{
  tree type = TREE_TYPE (expr);

  /* 15.1/4 [...] The type of the throw-expression shall not be an
	    incomplete type, or a pointer or a reference to an incomplete
	    type, other than void*, const void*, volatile void*, or
	    const volatile void*.  Except for these restriction and the
	    restrictions on type matching mentioned in 15.3, the operand
	    of throw is treated exactly as a function argument in a call
	    (5.2.2) or the operand of a return statement.  */
  if (!complete_ptr_ref_or_void_ptr_p (type, expr))
    return false;

  /* 10.4/3 An abstract class shall not be used as a parameter type,
	    as a function return type or as type of an explicit
	    conversion.  */
  else if (CLASS_TYPE_P (type) && CLASSTYPE_PURE_VIRTUALS (type))
    {
      error ("expression %qE of abstract class type %qT cannot "
	     "be used in throw-expression", expr, type);
      return false;
    }

  return true;
}

/* Returns nonzero if FN is a declaration of a standard C library
   function which is known not to throw.

   [lib.res.on.exception.handling]: None of the functions from the
   Standard C library shall report an error by throwing an
   exception, unless it calls a program-supplied function that
   throws an exception.  */

#include "cfns.h"

int
nothrow_libfn_p (tree fn)
{
  tree id;

  if (TREE_PUBLIC (fn)
      && DECL_EXTERNAL (fn)
      && DECL_NAMESPACE_SCOPE_P (fn)
      && DECL_EXTERN_C_P (fn))
    /* OK */;
  else
    /* Can't be a C library function.  */
    return 0;

  /* Being a C library function, DECL_ASSEMBLER_NAME == DECL_NAME
     unless the system headers are playing rename tricks, and if
     they are, we don't want to be confused by them.  */
  id = DECL_NAME (fn);
  return !!libc_name_p (IDENTIFIER_POINTER (id), IDENTIFIER_LENGTH (id));
}

/* Returns nonzero if an exception of type FROM will be caught by a
   handler for type TO, as per [except.handle].  */

static int
can_convert_eh (tree to, tree from)
{
  to = non_reference (to);
  from = non_reference (from);

  if (TREE_CODE (to) == POINTER_TYPE && TREE_CODE (from) == POINTER_TYPE)
    {
      to = TREE_TYPE (to);
      from = TREE_TYPE (from);

      if (! at_least_as_qualified_p (to, from))
	return 0;

      if (TREE_CODE (to) == VOID_TYPE)
	return 1;

      /* Else fall through.  */
    }

  if (CLASS_TYPE_P (to) && CLASS_TYPE_P (from)
      && PUBLICLY_UNIQUELY_DERIVED_P (to, from))
    return 1;

  return 0;
}

/* Check whether any of the handlers in I are shadowed by another handler
   accepting TYPE.  Note that the shadowing may not be complete; even if
   an exception of type B would be caught by a handler for A, there could
   be a derived class C for which A is an ambiguous base but B is not, so
   the handler for B would catch an exception of type C.  */

static void
check_handlers_1 (tree master, tree_stmt_iterator i)
{
  tree type = TREE_TYPE (master);

  for (; !tsi_end_p (i); tsi_next (&i))
    {
      tree handler = tsi_stmt (i);
      if (TREE_TYPE (handler) && can_convert_eh (type, TREE_TYPE (handler)))
	{
	  warning (0, "%Hexception of type %qT will be caught",
		   EXPR_LOCUS (handler), TREE_TYPE (handler));
	  warning (0, "%H   by earlier handler for %qT",
		   EXPR_LOCUS (master), type);
	  break;
	}
    }
}

/* Given a STATEMENT_LIST of HANDLERs, make sure that they're OK.  */

void
check_handlers (tree handlers)
{
  tree_stmt_iterator i;

  /* If we don't have a STATEMENT_LIST, then we've just got one
     handler, and thus nothing to warn about.  */
  if (TREE_CODE (handlers) != STATEMENT_LIST)
    return;

  i = tsi_start (handlers);
  if (!tsi_end_p (i))
    while (1)
      {
	tree handler = tsi_stmt (i);
	tsi_next (&i);

	/* No more handlers; nothing to shadow.  */
	if (tsi_end_p (i))
	  break;
	if (TREE_TYPE (handler) == NULL_TREE)
	  pedwarn ("%H%<...%> handler must be the last handler for"
		   " its try block", EXPR_LOCUS (handler));
	else
	  check_handlers_1 (handler, i);
      }
}
