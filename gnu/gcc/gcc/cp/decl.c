/* Process declarations and variables for C++ compiler.
   Copyright (C) 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004, 2005, 2006  Free Software Foundation, Inc.
   Contributed by Michael Tiemann (tiemann@cygnus.com)

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
#include "tree-inline.h"
#include "decl.h"
#include "output.h"
#include "except.h"
#include "toplev.h"
#include "hashtab.h"
#include "tm_p.h"
#include "target.h"
#include "c-common.h"
#include "c-pragma.h"
#include "diagnostic.h"
#include "debug.h"
#include "timevar.h"
#include "tree-flow.h"

static tree grokparms (cp_parameter_declarator *, tree *);
static const char *redeclaration_error_message (tree, tree);

static int decl_jump_unsafe (tree);
static void require_complete_types_for_parms (tree);
static int ambi_op_p (enum tree_code);
static int unary_op_p (enum tree_code);
static void push_local_name (tree);
static tree grok_reference_init (tree, tree, tree, tree *);
static tree grokvardecl (tree, tree, const cp_decl_specifier_seq *,
			 int, int, tree);
static void record_unknown_type (tree, const char *);
static tree builtin_function_1 (const char *, tree, tree,
				enum built_in_function code,
				enum built_in_class cl, const char *,
				tree);
static tree build_library_fn_1 (tree, enum tree_code, tree);
static int member_function_or_else (tree, tree, enum overload_flags);
static void bad_specifiers (tree, const char *, int, int, int, int,
			    int);
static void check_for_uninitialized_const_var (tree);
static hashval_t typename_hash (const void *);
static int typename_compare (const void *, const void *);
static tree local_variable_p_walkfn (tree *, int *, void *);
static tree record_builtin_java_type (const char *, int);
static const char *tag_name (enum tag_types);
static tree lookup_and_check_tag (enum tag_types, tree, tag_scope, bool);
static int walk_namespaces_r (tree, walk_namespaces_fn, void *);
static void maybe_deduce_size_from_array_init (tree, tree);
static void layout_var_decl (tree);
static void maybe_commonize_var (tree);
static tree check_initializer (tree, tree, int, tree *);
static void make_rtl_for_nonlocal_decl (tree, tree, const char *);
static void save_function_data (tree);
static void check_function_type (tree, tree);
static void finish_constructor_body (void);
static void begin_destructor_body (void);
static void finish_destructor_body (void);
static tree create_array_type_for_decl (tree, tree, tree);
static tree get_atexit_node (void);
static tree get_dso_handle_node (void);
static tree start_cleanup_fn (void);
static void end_cleanup_fn (void);
static tree cp_make_fname_decl (tree, int);
static void initialize_predefined_identifiers (void);
static tree check_special_function_return_type
	(special_function_kind, tree, tree);
static tree push_cp_library_fn (enum tree_code, tree);
static tree build_cp_library_fn (tree, enum tree_code, tree);
static void store_parm_decls (tree);
static void initialize_local_var (tree, tree);
static void expand_static_init (tree, tree);
static tree next_initializable_field (tree);

/* The following symbols are subsumed in the cp_global_trees array, and
   listed here individually for documentation purposes.

   C++ extensions
	tree wchar_decl_node;

	tree vtable_entry_type;
	tree delta_type_node;
	tree __t_desc_type_node;

	tree class_type_node;
	tree unknown_type_node;

   Array type `vtable_entry_type[]'

	tree vtbl_type_node;
	tree vtbl_ptr_type_node;

   Namespaces,

	tree std_node;
	tree abi_node;

   A FUNCTION_DECL which can call `abort'.  Not necessarily the
   one that the user will declare, but sufficient to be called
   by routines that want to abort the program.

	tree abort_fndecl;

   The FUNCTION_DECL for the default `::operator delete'.

	tree global_delete_fndecl;

   Used by RTTI
	tree type_info_type_node, tinfo_decl_id, tinfo_decl_type;
	tree tinfo_var_id;  */

tree cp_global_trees[CPTI_MAX];

/* Indicates that there is a type value in some namespace, although
   that is not necessarily in scope at the moment.  */

tree global_type_node;

/* The node that holds the "name" of the global scope.  */
tree global_scope_name;

#define local_names cp_function_chain->x_local_names

/* A list of objects which have constructors or destructors
   which reside in the global scope.  The decl is stored in
   the TREE_VALUE slot and the initializer is stored
   in the TREE_PURPOSE slot.  */
tree static_aggregates;

/* -- end of C++ */

/* A node for the integer constants 2, and 3.  */

tree integer_two_node, integer_three_node;

/* Used only for jumps to as-yet undefined labels, since jumps to
   defined labels can have their validity checked immediately.  */

struct named_label_use_entry GTY(())
{
  struct named_label_use_entry *next;
  /* The binding level to which this entry is *currently* attached.
     This is initially the binding level in which the goto appeared,
     but is modified as scopes are closed.  */
  struct cp_binding_level *binding_level;
  /* The head of the names list that was current when the goto appeared,
     or the inner scope popped.  These are the decls that will *not* be
     skipped when jumping to the label.  */
  tree names_in_scope;
  /* The location of the goto, for error reporting.  */
  location_t o_goto_locus;
  /* True if an OpenMP structured block scope has been closed since
     the goto appeared.  This means that the branch from the label will
     illegally exit an OpenMP scope.  */
  bool in_omp_scope;
};

/* A list of all LABEL_DECLs in the function that have names.  Here so
   we can clear out their names' definitions at the end of the
   function, and so we can check the validity of jumps to these labels.  */

struct named_label_entry GTY(())
{
  /* The decl itself.  */
  tree label_decl;

  /* The binding level to which the label is *currently* attached.
     This is initially set to the binding level in which the label
     is defined, but is modified as scopes are closed.  */
  struct cp_binding_level *binding_level;
  /* The head of the names list that was current when the label was
     defined, or the inner scope popped.  These are the decls that will
     be skipped when jumping to the label.  */
  tree names_in_scope;
  /* A tree list of all decls from all binding levels that would be
     crossed by a backward branch to the label.  */
  tree bad_decls;

  /* A list of uses of the label, before the label is defined.  */
  struct named_label_use_entry *uses;

  /* The following bits are set after the label is defined, and are
     updated as scopes are popped.  They indicate that a backward jump
     to the label will illegally enter a scope of the given flavor.  */
  bool in_try_scope;
  bool in_catch_scope;
  bool in_omp_scope;
};

#define named_labels cp_function_chain->x_named_labels

/* The number of function bodies which we are currently processing.
   (Zero if we are at namespace scope, one inside the body of a
   function, two inside the body of a function in a local class, etc.)  */
int function_depth;

/* States indicating how grokdeclarator() should handle declspecs marked
   with __attribute__((deprecated)).  An object declared as
   __attribute__((deprecated)) suppresses warnings of uses of other
   deprecated items.  */

enum deprecated_states {
  DEPRECATED_NORMAL,
  DEPRECATED_SUPPRESS
};

static enum deprecated_states deprecated_state = DEPRECATED_NORMAL;


/* A TREE_LIST of VAR_DECLs.  The TREE_PURPOSE is a RECORD_TYPE or
   UNION_TYPE; the TREE_VALUE is a VAR_DECL with that type.  At the
   time the VAR_DECL was declared, the type was incomplete.  */

static GTY(()) tree incomplete_vars;

/* Returns the kind of template specialization we are currently
   processing, given that it's declaration contained N_CLASS_SCOPES
   explicit scope qualifications.  */

tmpl_spec_kind
current_tmpl_spec_kind (int n_class_scopes)
{
  int n_template_parm_scopes = 0;
  int seen_specialization_p = 0;
  int innermost_specialization_p = 0;
  struct cp_binding_level *b;

  /* Scan through the template parameter scopes.  */
  for (b = current_binding_level;
       b->kind == sk_template_parms;
       b = b->level_chain)
    {
      /* If we see a specialization scope inside a parameter scope,
	 then something is wrong.  That corresponds to a declaration
	 like:

	    template <class T> template <> ...

	 which is always invalid since [temp.expl.spec] forbids the
	 specialization of a class member template if the enclosing
	 class templates are not explicitly specialized as well.  */
      if (b->explicit_spec_p)
	{
	  if (n_template_parm_scopes == 0)
	    innermost_specialization_p = 1;
	  else
	    seen_specialization_p = 1;
	}
      else if (seen_specialization_p == 1)
	return tsk_invalid_member_spec;

      ++n_template_parm_scopes;
    }

  /* Handle explicit instantiations.  */
  if (processing_explicit_instantiation)
    {
      if (n_template_parm_scopes != 0)
	/* We've seen a template parameter list during an explicit
	   instantiation.  For example:

	     template <class T> template void f(int);

	   This is erroneous.  */
	return tsk_invalid_expl_inst;
      else
	return tsk_expl_inst;
    }

  if (n_template_parm_scopes < n_class_scopes)
    /* We've not seen enough template headers to match all the
       specialized classes present.  For example:

	 template <class T> void R<T>::S<T>::f(int);

       This is invalid; there needs to be one set of template
       parameters for each class.  */
    return tsk_insufficient_parms;
  else if (n_template_parm_scopes == n_class_scopes)
    /* We're processing a non-template declaration (even though it may
       be a member of a template class.)  For example:

	 template <class T> void S<T>::f(int);

       The `class T' maches the `S<T>', leaving no template headers
       corresponding to the `f'.  */
    return tsk_none;
  else if (n_template_parm_scopes > n_class_scopes + 1)
    /* We've got too many template headers.  For example:

	 template <> template <class T> void f (T);

       There need to be more enclosing classes.  */
    return tsk_excessive_parms;
  else
    /* This must be a template.  It's of the form:

	 template <class T> template <class U> void S<T>::f(U);

       This is a specialization if the innermost level was a
       specialization; otherwise it's just a definition of the
       template.  */
    return innermost_specialization_p ? tsk_expl_spec : tsk_template;
}

/* Exit the current scope.  */

void
finish_scope (void)
{
  poplevel (0, 0, 0);
}

/* When a label goes out of scope, check to see if that label was used
   in a valid manner, and issue any appropriate warnings or errors.  */

static void
pop_label (tree label, tree old_value)
{
  if (!processing_template_decl)
    {
      if (DECL_INITIAL (label) == NULL_TREE)
	{
	  location_t location;

	  error ("label %q+D used but not defined", label);
#ifdef USE_MAPPED_LOCATION
	  location = input_location; /* FIXME want (input_filename, (line)0) */
#else
	  location.file = input_filename;
	  location.line = 0;
#endif
	  /* Avoid crashing later.  */
	  define_label (location, DECL_NAME (label));
	}
      else if (!TREE_USED (label))
	warning (OPT_Wunused_label, "label %q+D defined but not used", label);
    }

  SET_IDENTIFIER_LABEL_VALUE (DECL_NAME (label), old_value);
}

/* At the end of a function, all labels declared within the function
   go out of scope.  BLOCK is the top-level block for the
   function.  */

static int
pop_labels_1 (void **slot, void *data)
{
  struct named_label_entry *ent = (struct named_label_entry *) *slot;
  tree block = (tree) data;

  pop_label (ent->label_decl, NULL_TREE);

  /* Put the labels into the "variables" of the top-level block,
     so debugger can see them.  */
  TREE_CHAIN (ent->label_decl) = BLOCK_VARS (block);
  BLOCK_VARS (block) = ent->label_decl;

  htab_clear_slot (named_labels, slot);

  return 1;
}

static void
pop_labels (tree block)
{
  if (named_labels)
    {
      htab_traverse (named_labels, pop_labels_1, block);
      named_labels = NULL;
    }
}

/* At the end of a block with local labels, restore the outer definition.  */

static void
pop_local_label (tree label, tree old_value)
{
  struct named_label_entry dummy;
  void **slot;

  pop_label (label, old_value);

  dummy.label_decl = label;
  slot = htab_find_slot (named_labels, &dummy, NO_INSERT);
  htab_clear_slot (named_labels, slot);
}

/* The following two routines are used to interface to Objective-C++.
   The binding level is purposely treated as an opaque type.  */

void *
objc_get_current_scope (void)
{
  return current_binding_level;
}

/* The following routine is used by the NeXT-style SJLJ exceptions;
   variables get marked 'volatile' so as to not be clobbered by
   _setjmp()/_longjmp() calls.  All variables in the current scope,
   as well as parent scopes up to (but not including) ENCLOSING_BLK
   shall be thusly marked.  */

void
objc_mark_locals_volatile (void *enclosing_blk)
{
  struct cp_binding_level *scope;

  for (scope = current_binding_level;
       scope && scope != enclosing_blk;
       scope = scope->level_chain)
    {
      tree decl;

      for (decl = scope->names; decl; decl = TREE_CHAIN (decl))
	objc_volatilize_decl (decl);

      /* Do not climb up past the current function.  */
      if (scope->kind == sk_function_parms)
	break;
    }
}

/* Update data for defined and undefined labels when leaving a scope.  */

static int
poplevel_named_label_1 (void **slot, void *data)
{
  struct named_label_entry *ent = (struct named_label_entry *) *slot;
  struct cp_binding_level *bl = (struct cp_binding_level *) data;
  struct cp_binding_level *obl = bl->level_chain;

  if (ent->binding_level == bl)
    {
      tree decl;

      for (decl = ent->names_in_scope; decl; decl = TREE_CHAIN (decl))
	if (decl_jump_unsafe (decl))
	  ent->bad_decls = tree_cons (NULL, decl, ent->bad_decls);

      ent->binding_level = obl;
      ent->names_in_scope = obl->names;
      switch (bl->kind)
	{
	case sk_try:
	  ent->in_try_scope = true;
	  break;
	case sk_catch:
	  ent->in_catch_scope = true;
	  break;
	case sk_omp:
	  ent->in_omp_scope = true;
	  break;
	default:
	  break;
	}
    }
  else if (ent->uses)
    {
      struct named_label_use_entry *use;

      for (use = ent->uses; use ; use = use->next)
	if (use->binding_level == bl)
	  {
	    use->binding_level = obl;
	    use->names_in_scope = obl->names;
	    if (bl->kind == sk_omp)
	      use->in_omp_scope = true;
	  }
    }

  return 1;
}

/* Exit a binding level.
   Pop the level off, and restore the state of the identifier-decl mappings
   that were in effect when this level was entered.

   If KEEP == 1, this level had explicit declarations, so
   and create a "block" (a BLOCK node) for the level
   to record its declarations and subblocks for symbol table output.

   If FUNCTIONBODY is nonzero, this level is the body of a function,
   so create a block as if KEEP were set and also clear out all
   label names.

   If REVERSE is nonzero, reverse the order of decls before putting
   them into the BLOCK.  */

tree
poplevel (int keep, int reverse, int functionbody)
{
  tree link;
  /* The chain of decls was accumulated in reverse order.
     Put it into forward order, just for cleanliness.  */
  tree decls;
  int tmp = functionbody;
  int real_functionbody;
  tree subblocks;
  tree block;
  tree decl;
  int leaving_for_scope;
  scope_kind kind;

  timevar_push (TV_NAME_LOOKUP);
 restart:

  block = NULL_TREE;

  gcc_assert (current_binding_level->kind != sk_class);

  real_functionbody = (current_binding_level->kind == sk_cleanup
		       ? ((functionbody = 0), tmp) : functionbody);
  subblocks = functionbody >= 0 ? current_binding_level->blocks : 0;

  gcc_assert (!VEC_length(cp_class_binding,
			  current_binding_level->class_shadowed));

  /* We used to use KEEP == 2 to indicate that the new block should go
     at the beginning of the list of blocks at this binding level,
     rather than the end.  This hack is no longer used.  */
  gcc_assert (keep == 0 || keep == 1);

  if (current_binding_level->keep)
    keep = 1;

  /* Any uses of undefined labels, and any defined labels, now operate
     under constraints of next binding contour.  */
  if (cfun && !functionbody && named_labels)
    htab_traverse (named_labels, poplevel_named_label_1,
		   current_binding_level);

  /* Get the decls in the order they were written.
     Usually current_binding_level->names is in reverse order.
     But parameter decls were previously put in forward order.  */

  if (reverse)
    current_binding_level->names
      = decls = nreverse (current_binding_level->names);
  else
    decls = current_binding_level->names;

  /* If there were any declarations or structure tags in that level,
     or if this level is a function body,
     create a BLOCK to record them for the life of this function.  */
  block = NULL_TREE;
  if (keep == 1 || functionbody)
    block = make_node (BLOCK);
  if (block != NULL_TREE)
    {
      BLOCK_VARS (block) = decls;
      BLOCK_SUBBLOCKS (block) = subblocks;
    }

  /* In each subblock, record that this is its superior.  */
  if (keep >= 0)
    for (link = subblocks; link; link = TREE_CHAIN (link))
      BLOCK_SUPERCONTEXT (link) = block;

  /* We still support the old for-scope rules, whereby the variables
     in a for-init statement were in scope after the for-statement
     ended.  We only use the new rules if flag_new_for_scope is
     nonzero.  */
  leaving_for_scope
    = current_binding_level->kind == sk_for && flag_new_for_scope == 1;

  /* Before we remove the declarations first check for unused variables.  */
  if (warn_unused_variable
      && !processing_template_decl)
    for (decl = getdecls (); decl; decl = TREE_CHAIN (decl))
      if (TREE_CODE (decl) == VAR_DECL
	  && ! TREE_USED (decl)
	  && ! DECL_IN_SYSTEM_HEADER (decl)
	  && DECL_NAME (decl) && ! DECL_ARTIFICIAL (decl))
	warning (OPT_Wunused_variable, "unused variable %q+D", decl);

  /* Remove declarations for all the DECLs in this level.  */
  for (link = decls; link; link = TREE_CHAIN (link))
    {
      if (leaving_for_scope && TREE_CODE (link) == VAR_DECL
	  && DECL_NAME (link))
	{
	  tree name = DECL_NAME (link);
	  cxx_binding *ob;
	  tree ns_binding;

	  ob = outer_binding (name,
			      IDENTIFIER_BINDING (name),
			      /*class_p=*/true);
	  if (!ob)
	    ns_binding = IDENTIFIER_NAMESPACE_VALUE (name);
	  else
	    ns_binding = NULL_TREE;

	  if (ob && ob->scope == current_binding_level->level_chain)
	    /* We have something like:

		 int i;
		 for (int i; ;);

	       and we are leaving the `for' scope.  There's no reason to
	       keep the binding of the inner `i' in this case.  */
	    pop_binding (name, link);
	  else if ((ob && (TREE_CODE (ob->value) == TYPE_DECL))
		   || (ns_binding && TREE_CODE (ns_binding) == TYPE_DECL))
	    /* Here, we have something like:

		 typedef int I;

		 void f () {
		   for (int I; ;);
		 }

	       We must pop the for-scope binding so we know what's a
	       type and what isn't.  */
	    pop_binding (name, link);
	  else
	    {
	      /* Mark this VAR_DECL as dead so that we can tell we left it
		 there only for backward compatibility.  */
	      DECL_DEAD_FOR_LOCAL (link) = 1;

	      /* Keep track of what should have happened when we
		 popped the binding.  */
	      if (ob && ob->value)
		{
		  SET_DECL_SHADOWED_FOR_VAR (link, ob->value);
		  DECL_HAS_SHADOWED_FOR_VAR_P (link) = 1;
		}

	      /* Add it to the list of dead variables in the next
		 outermost binding to that we can remove these when we
		 leave that binding.  */
	      current_binding_level->level_chain->dead_vars_from_for
		= tree_cons (NULL_TREE, link,
			     current_binding_level->level_chain->
			     dead_vars_from_for);

	      /* Although we don't pop the cxx_binding, we do clear
		 its SCOPE since the scope is going away now.  */
	      IDENTIFIER_BINDING (name)->scope
		= current_binding_level->level_chain;
	    }
	}
      else
	{
	  tree name;

	  /* Remove the binding.  */
	  decl = link;

	  if (TREE_CODE (decl) == TREE_LIST)
	    decl = TREE_VALUE (decl);
	  name = decl;

	  if (TREE_CODE (name) == OVERLOAD)
	    name = OVL_FUNCTION (name);

	  gcc_assert (DECL_P (name));
	  pop_binding (DECL_NAME (name), decl);
	}
    }

  /* Remove declarations for any `for' variables from inner scopes
     that we kept around.  */
  for (link = current_binding_level->dead_vars_from_for;
       link; link = TREE_CHAIN (link))
    pop_binding (DECL_NAME (TREE_VALUE (link)), TREE_VALUE (link));

  /* Restore the IDENTIFIER_TYPE_VALUEs.  */
  for (link = current_binding_level->type_shadowed;
       link; link = TREE_CHAIN (link))
    SET_IDENTIFIER_TYPE_VALUE (TREE_PURPOSE (link), TREE_VALUE (link));

  /* Restore the IDENTIFIER_LABEL_VALUEs for local labels.  */
  for (link = current_binding_level->shadowed_labels;
       link;
       link = TREE_CHAIN (link))
    pop_local_label (TREE_VALUE (link), TREE_PURPOSE (link));

  /* There may be OVERLOADs (wrapped in TREE_LISTs) on the BLOCK_VARs
     list if a `using' declaration put them there.  The debugging
     back-ends won't understand OVERLOAD, so we remove them here.
     Because the BLOCK_VARS are (temporarily) shared with
     CURRENT_BINDING_LEVEL->NAMES we must do this fixup after we have
     popped all the bindings.  */
  if (block)
    {
      tree* d;

      for (d = &BLOCK_VARS (block); *d; )
	{
	  if (TREE_CODE (*d) == TREE_LIST)
	    *d = TREE_CHAIN (*d);
	  else
	    d = &TREE_CHAIN (*d);
	}
    }

  /* If the level being exited is the top level of a function,
     check over all the labels.  */
  if (functionbody)
    {
      /* Since this is the top level block of a function, the vars are
	 the function's parameters.  Don't leave them in the BLOCK
	 because they are found in the FUNCTION_DECL instead.  */
      BLOCK_VARS (block) = 0;
      pop_labels (block);
    }

  kind = current_binding_level->kind;
  if (kind == sk_cleanup)
    {
      tree stmt;

      /* If this is a temporary binding created for a cleanup, then we'll
	 have pushed a statement list level.  Pop that, create a new
	 BIND_EXPR for the block, and insert it into the stream.  */
      stmt = pop_stmt_list (current_binding_level->statement_list);
      stmt = c_build_bind_expr (block, stmt);
      add_stmt (stmt);
    }

  leave_scope ();
  if (functionbody)
    DECL_INITIAL (current_function_decl) = block;
  else if (block)
    current_binding_level->blocks
      = chainon (current_binding_level->blocks, block);

  /* If we did not make a block for the level just exited,
     any blocks made for inner levels
     (since they cannot be recorded as subblocks in that level)
     must be carried forward so they will later become subblocks
     of something else.  */
  else if (subblocks)
    current_binding_level->blocks
      = chainon (current_binding_level->blocks, subblocks);

  /* Each and every BLOCK node created here in `poplevel' is important
     (e.g. for proper debugging information) so if we created one
     earlier, mark it as "used".  */
  if (block)
    TREE_USED (block) = 1;

  /* All temporary bindings created for cleanups are popped silently.  */
  if (kind == sk_cleanup)
    goto restart;

  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, block);
}

/* Insert BLOCK at the end of the list of subblocks of the
   current binding level.  This is used when a BIND_EXPR is expanded,
   to handle the BLOCK node inside the BIND_EXPR.  */

void
insert_block (tree block)
{
  TREE_USED (block) = 1;
  current_binding_level->blocks
    = chainon (current_binding_level->blocks, block);
}

/* Walk all the namespaces contained NAMESPACE, including NAMESPACE
   itself, calling F for each.  The DATA is passed to F as well.  */

static int
walk_namespaces_r (tree namespace, walk_namespaces_fn f, void* data)
{
  int result = 0;
  tree current = NAMESPACE_LEVEL (namespace)->namespaces;

  result |= (*f) (namespace, data);

  for (; current; current = TREE_CHAIN (current))
    result |= walk_namespaces_r (current, f, data);

  return result;
}

/* Walk all the namespaces, calling F for each.  The DATA is passed to
   F as well.  */

int
walk_namespaces (walk_namespaces_fn f, void* data)
{
  return walk_namespaces_r (global_namespace, f, data);
}

/* Call wrapup_globals_declarations for the globals in NAMESPACE.  If
   DATA is non-NULL, this is the last time we will call
   wrapup_global_declarations for this NAMESPACE.  */

int
wrapup_globals_for_namespace (tree namespace, void* data)
{
  struct cp_binding_level *level = NAMESPACE_LEVEL (namespace);
  VEC(tree,gc) *statics = level->static_decls;
  tree *vec = VEC_address (tree, statics);
  int len = VEC_length (tree, statics);
  int last_time = (data != 0);

  if (last_time)
    {
      check_global_declarations (vec, len);
      emit_debug_global_declarations (vec, len);
      return 0;
    }

  /* Write out any globals that need to be output.  */
  return wrapup_global_declarations (vec, len);
}


/* In C++, you don't have to write `struct S' to refer to `S'; you
   can just use `S'.  We accomplish this by creating a TYPE_DECL as
   if the user had written `typedef struct S S'.  Create and return
   the TYPE_DECL for TYPE.  */

tree
create_implicit_typedef (tree name, tree type)
{
  tree decl;

  decl = build_decl (TYPE_DECL, name, type);
  DECL_ARTIFICIAL (decl) = 1;
  /* There are other implicit type declarations, like the one *within*
     a class that allows you to write `S::S'.  We must distinguish
     amongst these.  */
  SET_DECL_IMPLICIT_TYPEDEF_P (decl);
  TYPE_NAME (type) = decl;

  return decl;
}

/* Remember a local name for name-mangling purposes.  */

static void
push_local_name (tree decl)
{
  size_t i, nelts;
  tree t, name;

  timevar_push (TV_NAME_LOOKUP);

  name = DECL_NAME (decl);

  nelts = VEC_length (tree, local_names);
  for (i = 0; i < nelts; i++)
    {
      t = VEC_index (tree, local_names, i);
      if (DECL_NAME (t) == name)
	{
	  if (!DECL_LANG_SPECIFIC (decl))
	    retrofit_lang_decl (decl);
	  DECL_LANG_SPECIFIC (decl)->decl_flags.u2sel = 1;
	  if (DECL_LANG_SPECIFIC (t))
	    DECL_DISCRIMINATOR (decl) = DECL_DISCRIMINATOR (t) + 1;
	  else
	    DECL_DISCRIMINATOR (decl) = 1;

	  VEC_replace (tree, local_names, i, decl);
	  timevar_pop (TV_NAME_LOOKUP);
	  return;
	}
    }

  VEC_safe_push (tree, gc, local_names, decl);
  timevar_pop (TV_NAME_LOOKUP);
}

/* Subroutine of duplicate_decls: return truthvalue of whether
   or not types of these decls match.

   For C++, we must compare the parameter list so that `int' can match
   `int&' in a parameter position, but `int&' is not confused with
   `const int&'.  */

int
decls_match (tree newdecl, tree olddecl)
{
  int types_match;

  if (newdecl == olddecl)
    return 1;

  if (TREE_CODE (newdecl) != TREE_CODE (olddecl))
    /* If the two DECLs are not even the same kind of thing, we're not
       interested in their types.  */
    return 0;

  if (TREE_CODE (newdecl) == FUNCTION_DECL)
    {
      tree f1 = TREE_TYPE (newdecl);
      tree f2 = TREE_TYPE (olddecl);
      tree p1 = TYPE_ARG_TYPES (f1);
      tree p2 = TYPE_ARG_TYPES (f2);

      if (CP_DECL_CONTEXT (newdecl) != CP_DECL_CONTEXT (olddecl)
	  && ! (DECL_EXTERN_C_P (newdecl)
		&& DECL_EXTERN_C_P (olddecl)))
	return 0;

      if (TREE_CODE (f1) != TREE_CODE (f2))
	return 0;

      if (same_type_p (TREE_TYPE (f1), TREE_TYPE (f2)))
	{
	  if (p2 == NULL_TREE && DECL_EXTERN_C_P (olddecl)
	      && (DECL_BUILT_IN (olddecl)
#ifndef NO_IMPLICIT_EXTERN_C
		  || (DECL_IN_SYSTEM_HEADER (newdecl) && !DECL_CLASS_SCOPE_P (newdecl))
		  || (DECL_IN_SYSTEM_HEADER (olddecl) && !DECL_CLASS_SCOPE_P (olddecl))
#endif
	      ))
	    {
	      types_match = self_promoting_args_p (p1);
	      if (p1 == void_list_node)
		TREE_TYPE (newdecl) = TREE_TYPE (olddecl);
	    }
#ifndef NO_IMPLICIT_EXTERN_C
	  else if (p1 == NULL_TREE
		   && (DECL_EXTERN_C_P (olddecl)
		       && DECL_IN_SYSTEM_HEADER (olddecl)
		       && !DECL_CLASS_SCOPE_P (olddecl))
		   && (DECL_EXTERN_C_P (newdecl)
		       && DECL_IN_SYSTEM_HEADER (newdecl)
		       && !DECL_CLASS_SCOPE_P (newdecl)))
	    {
	      types_match = self_promoting_args_p (p2);
	      TREE_TYPE (newdecl) = TREE_TYPE (olddecl);
	    }
#endif
	  else
	    types_match = compparms (p1, p2);
	}
      else
	types_match = 0;
    }
  else if (TREE_CODE (newdecl) == TEMPLATE_DECL)
    {
      if (TREE_CODE (DECL_TEMPLATE_RESULT (newdecl))
	  != TREE_CODE (DECL_TEMPLATE_RESULT (olddecl)))
	return 0;

      if (!comp_template_parms (DECL_TEMPLATE_PARMS (newdecl),
				DECL_TEMPLATE_PARMS (olddecl)))
	return 0;

      if (TREE_CODE (DECL_TEMPLATE_RESULT (newdecl)) == TYPE_DECL)
	types_match = same_type_p (TREE_TYPE (DECL_TEMPLATE_RESULT (olddecl)),
				   TREE_TYPE (DECL_TEMPLATE_RESULT (newdecl)));
      else
	types_match = decls_match (DECL_TEMPLATE_RESULT (olddecl),
				   DECL_TEMPLATE_RESULT (newdecl));
    }
  else
    {
      /* Need to check scope for variable declaration (VAR_DECL).
	 For typedef (TYPE_DECL), scope is ignored.  */
      if (TREE_CODE (newdecl) == VAR_DECL
	  && CP_DECL_CONTEXT (newdecl) != CP_DECL_CONTEXT (olddecl)
	  /* [dcl.link]
	     Two declarations for an object with C language linkage
	     with the same name (ignoring the namespace that qualify
	     it) that appear in different namespace scopes refer to
	     the same object.  */
	  && !(DECL_EXTERN_C_P (olddecl) && DECL_EXTERN_C_P (newdecl)))
	return 0;

      if (TREE_TYPE (newdecl) == error_mark_node)
	types_match = TREE_TYPE (olddecl) == error_mark_node;
      else if (TREE_TYPE (olddecl) == NULL_TREE)
	types_match = TREE_TYPE (newdecl) == NULL_TREE;
      else if (TREE_TYPE (newdecl) == NULL_TREE)
	types_match = 0;
      else
	types_match = comptypes (TREE_TYPE (newdecl),
				 TREE_TYPE (olddecl),
				 COMPARE_REDECLARATION);
    }

  return types_match;
}

/* If NEWDECL is `static' and an `extern' was seen previously,
   warn about it.  OLDDECL is the previous declaration.

   Note that this does not apply to the C++ case of declaring
   a variable `extern const' and then later `const'.

   Don't complain about built-in functions, since they are beyond
   the user's control.  */

void
warn_extern_redeclared_static (tree newdecl, tree olddecl)
{
  tree name;

  if (TREE_CODE (newdecl) == TYPE_DECL
      || TREE_CODE (newdecl) == TEMPLATE_DECL
      || TREE_CODE (newdecl) == CONST_DECL
      || TREE_CODE (newdecl) == NAMESPACE_DECL)
    return;

  /* Don't get confused by static member functions; that's a different
     use of `static'.  */
  if (TREE_CODE (newdecl) == FUNCTION_DECL
      && DECL_STATIC_FUNCTION_P (newdecl))
    return;

  /* If the old declaration was `static', or the new one isn't, then
     then everything is OK.  */
  if (DECL_THIS_STATIC (olddecl) || !DECL_THIS_STATIC (newdecl))
    return;

  /* It's OK to declare a builtin function as `static'.  */
  if (TREE_CODE (olddecl) == FUNCTION_DECL
      && DECL_ARTIFICIAL (olddecl))
    return;

  name = DECL_ASSEMBLER_NAME (newdecl);
  pedwarn ("%qD was declared %<extern%> and later %<static%>", newdecl);
  pedwarn ("previous declaration of %q+D", olddecl);
}

/* NEW_DECL is a redeclaration of OLD_DECL; both are functions or
   function templates.  If their exception specifications do not
   match, issue an a diagnostic.  */

static void
check_redeclaration_exception_specification (tree new_decl,
					     tree old_decl)
{
  tree new_type;
  tree old_type;
  tree new_exceptions;
  tree old_exceptions;

  new_type = TREE_TYPE (new_decl);
  new_exceptions = TYPE_RAISES_EXCEPTIONS (new_type);
  old_type = TREE_TYPE (old_decl);
  old_exceptions = TYPE_RAISES_EXCEPTIONS (old_type);

  /* [except.spec]

     If any declaration of a function has an exception-specification,
     all declarations, including the definition and an explicit
     specialization, of that function shall have an
     exception-specification with the same set of type-ids.  */
  if ((pedantic || ! DECL_IN_SYSTEM_HEADER (old_decl))
      && ! DECL_IS_BUILTIN (old_decl)
      && flag_exceptions
      && !comp_except_specs (new_exceptions, old_exceptions,
			     /*exact=*/true))
    {
      error ("declaration of %qF throws different exceptions", new_decl);
      error ("from previous declaration %q+F", old_decl);
    }
}

/* If NEWDECL is a redeclaration of OLDDECL, merge the declarations.
   If the redeclaration is invalid, a diagnostic is issued, and the
   error_mark_node is returned.  Otherwise, OLDDECL is returned.

   If NEWDECL is not a redeclaration of OLDDECL, NULL_TREE is
   returned.

   NEWDECL_IS_FRIEND is true if NEWDECL was declared as a friend.  */

tree
duplicate_decls (tree newdecl, tree olddecl, bool newdecl_is_friend)
{
  unsigned olddecl_uid = DECL_UID (olddecl);
  int olddecl_friend = 0, types_match = 0, hidden_friend = 0;
  int new_defines_function = 0;
  tree new_template;

  if (newdecl == olddecl)
    return olddecl;

  types_match = decls_match (newdecl, olddecl);

  /* If either the type of the new decl or the type of the old decl is an
     error_mark_node, then that implies that we have already issued an
     error (earlier) for some bogus type specification, and in that case,
     it is rather pointless to harass the user with yet more error message
     about the same declaration, so just pretend the types match here.  */
  if (TREE_TYPE (newdecl) == error_mark_node
      || TREE_TYPE (olddecl) == error_mark_node)
    return error_mark_node;

  if (DECL_P (olddecl)
      && TREE_CODE (newdecl) == FUNCTION_DECL
      && TREE_CODE (olddecl) == FUNCTION_DECL
      && (DECL_UNINLINABLE (newdecl) || DECL_UNINLINABLE (olddecl)))
    {
      if (DECL_DECLARED_INLINE_P (newdecl)
	  && DECL_UNINLINABLE (newdecl)
	  && lookup_attribute ("noinline", DECL_ATTRIBUTES (newdecl)))
	/* Already warned elsewhere.  */;
      else if (DECL_DECLARED_INLINE_P (olddecl)
	       && DECL_UNINLINABLE (olddecl)
	       && lookup_attribute ("noinline", DECL_ATTRIBUTES (olddecl)))
	/* Already warned.  */;
      else if (DECL_DECLARED_INLINE_P (newdecl)
	       && DECL_UNINLINABLE (olddecl)
	       && lookup_attribute ("noinline", DECL_ATTRIBUTES (olddecl)))
	{
	  warning (OPT_Wattributes, "function %q+D redeclared as inline",
		   newdecl);
	  warning (OPT_Wattributes, "previous declaration of %q+D "
		   "with attribute noinline", olddecl);
	}
      else if (DECL_DECLARED_INLINE_P (olddecl)
	       && DECL_UNINLINABLE (newdecl)
	       && lookup_attribute ("noinline", DECL_ATTRIBUTES (newdecl)))
	{
	  warning (OPT_Wattributes, "function %q+D redeclared with "
		   "attribute noinline", newdecl);
	  warning (OPT_Wattributes, "previous declaration of %q+D was inline",
		   olddecl);
	}
    }

  /* Check for redeclaration and other discrepancies.  */
  if (TREE_CODE (olddecl) == FUNCTION_DECL
      && DECL_ARTIFICIAL (olddecl))
    {
      gcc_assert (!DECL_HIDDEN_FRIEND_P (olddecl));
      if (TREE_CODE (newdecl) != FUNCTION_DECL)
	{
	  /* Avoid warnings redeclaring built-ins which have not been
	     explicitly declared.  */
	  if (DECL_ANTICIPATED (olddecl))
	    return NULL_TREE;

	  /* If you declare a built-in or predefined function name as static,
	     the old definition is overridden, but optionally warn this was a
	     bad choice of name.  */
	  if (! TREE_PUBLIC (newdecl))
	    {
	      warning (OPT_Wshadow, "shadowing %s function %q#D",
		       DECL_BUILT_IN (olddecl) ? "built-in" : "library",
		       olddecl);
	      /* Discard the old built-in function.  */
	      return NULL_TREE;
	    }
	  /* If the built-in is not ansi, then programs can override
	     it even globally without an error.  */
	  else if (! DECL_BUILT_IN (olddecl))
	    warning (0, "library function %q#D redeclared as non-function %q#D",
		     olddecl, newdecl);
	  else
	    {
	      error ("declaration of %q#D", newdecl);
	      error ("conflicts with built-in declaration %q#D",
		     olddecl);
	    }
	  return NULL_TREE;
	}
      else if (!types_match)
	{
	  /* Avoid warnings redeclaring built-ins which have not been
	     explicitly declared.  */
	  if (DECL_ANTICIPATED (olddecl))
	    {
	      /* Deal with fileptr_type_node.  FILE type is not known
		 at the time we create the builtins.  */
	      tree t1, t2;

	      for (t1 = TYPE_ARG_TYPES (TREE_TYPE (newdecl)),
		   t2 = TYPE_ARG_TYPES (TREE_TYPE (olddecl));
		   t1 || t2;
		   t1 = TREE_CHAIN (t1), t2 = TREE_CHAIN (t2))
		if (!t1 || !t2)
		  break;
		else if (TREE_VALUE (t2) == fileptr_type_node)
		  {
		    tree t = TREE_VALUE (t1);

		    if (TREE_CODE (t) == POINTER_TYPE
			&& TYPE_NAME (TREE_TYPE (t))
			&& DECL_NAME (TYPE_NAME (TREE_TYPE (t)))
			   == get_identifier ("FILE")
			&& compparms (TREE_CHAIN (t1), TREE_CHAIN (t2)))
		      {
			tree oldargs = TYPE_ARG_TYPES (TREE_TYPE (olddecl));

			TYPE_ARG_TYPES (TREE_TYPE (olddecl))
			  = TYPE_ARG_TYPES (TREE_TYPE (newdecl));
			types_match = decls_match (newdecl, olddecl);
			if (types_match)
			  return duplicate_decls (newdecl, olddecl,
						  newdecl_is_friend);
			TYPE_ARG_TYPES (TREE_TYPE (olddecl)) = oldargs;
		      }
		  }
		else if (! same_type_p (TREE_VALUE (t1), TREE_VALUE (t2)))
		  break;
	    }
	  else if ((DECL_EXTERN_C_P (newdecl)
		    && DECL_EXTERN_C_P (olddecl))
		   || compparms (TYPE_ARG_TYPES (TREE_TYPE (newdecl)),
				 TYPE_ARG_TYPES (TREE_TYPE (olddecl))))
	    {
	      /* A near match; override the builtin.  */

	      if (TREE_PUBLIC (newdecl))
		{
		  warning (0, "new declaration %q#D", newdecl);
		  warning (0, "ambiguates built-in declaration %q#D",
			   olddecl);
		}
	      else
		warning (OPT_Wshadow, "shadowing %s function %q#D",
			 DECL_BUILT_IN (olddecl) ? "built-in" : "library",
			 olddecl);
	    }
	  else
	    /* Discard the old built-in function.  */
	    return NULL_TREE;

	  /* Replace the old RTL to avoid problems with inlining.  */
	  COPY_DECL_RTL (newdecl, olddecl);
	}
      /* Even if the types match, prefer the new declarations type for
	 built-ins which have not been explicitly declared, for
	 exception lists, etc...  */
      else if (DECL_ANTICIPATED (olddecl))
	{
	  tree type = TREE_TYPE (newdecl);
	  tree attribs = (*targetm.merge_type_attributes)
	    (TREE_TYPE (olddecl), type);

	  type = cp_build_type_attribute_variant (type, attribs);
	  TREE_TYPE (newdecl) = TREE_TYPE (olddecl) = type;
	}

      /* Whether or not the builtin can throw exceptions has no
	 bearing on this declarator.  */
      TREE_NOTHROW (olddecl) = 0;

      if (DECL_THIS_STATIC (newdecl) && !DECL_THIS_STATIC (olddecl))
	{
	  /* If a builtin function is redeclared as `static', merge
	     the declarations, but make the original one static.  */
	  DECL_THIS_STATIC (olddecl) = 1;
	  TREE_PUBLIC (olddecl) = 0;

	  /* Make the old declaration consistent with the new one so
	     that all remnants of the builtin-ness of this function
	     will be banished.  */
	  SET_DECL_LANGUAGE (olddecl, DECL_LANGUAGE (newdecl));
	  COPY_DECL_RTL (newdecl, olddecl);
	}
    }
  else if (TREE_CODE (olddecl) != TREE_CODE (newdecl))
    {
      if ((TREE_CODE (olddecl) == TYPE_DECL && DECL_ARTIFICIAL (olddecl)
	   && TREE_CODE (newdecl) != TYPE_DECL
	   && ! (TREE_CODE (newdecl) == TEMPLATE_DECL
		 && TREE_CODE (DECL_TEMPLATE_RESULT (newdecl)) == TYPE_DECL))
	  || (TREE_CODE (newdecl) == TYPE_DECL && DECL_ARTIFICIAL (newdecl)
	      && TREE_CODE (olddecl) != TYPE_DECL
	      && ! (TREE_CODE (olddecl) == TEMPLATE_DECL
		    && (TREE_CODE (DECL_TEMPLATE_RESULT (olddecl))
			== TYPE_DECL))))
	{
	  /* We do nothing special here, because C++ does such nasty
	     things with TYPE_DECLs.  Instead, just let the TYPE_DECL
	     get shadowed, and know that if we need to find a TYPE_DECL
	     for a given name, we can look in the IDENTIFIER_TYPE_VALUE
	     slot of the identifier.  */
	  return NULL_TREE;
	}

      if ((TREE_CODE (newdecl) == FUNCTION_DECL
	   && DECL_FUNCTION_TEMPLATE_P (olddecl))
	  || (TREE_CODE (olddecl) == FUNCTION_DECL
	      && DECL_FUNCTION_TEMPLATE_P (newdecl)))
	return NULL_TREE;

      error ("%q#D redeclared as different kind of symbol", newdecl);
      if (TREE_CODE (olddecl) == TREE_LIST)
	olddecl = TREE_VALUE (olddecl);
      error ("previous declaration of %q+#D", olddecl);

      return error_mark_node;
    }
  else if (!types_match)
    {
      if (CP_DECL_CONTEXT (newdecl) != CP_DECL_CONTEXT (olddecl))
	/* These are certainly not duplicate declarations; they're
	   from different scopes.  */
	return NULL_TREE;

      if (TREE_CODE (newdecl) == TEMPLATE_DECL)
	{
	  /* The name of a class template may not be declared to refer to
	     any other template, class, function, object, namespace, value,
	     or type in the same scope.  */
	  if (TREE_CODE (DECL_TEMPLATE_RESULT (olddecl)) == TYPE_DECL
	      || TREE_CODE (DECL_TEMPLATE_RESULT (newdecl)) == TYPE_DECL)
	    {
	      error ("declaration of template %q#D", newdecl);
	      error ("conflicts with previous declaration %q+#D", olddecl);
	    }
	  else if (TREE_CODE (DECL_TEMPLATE_RESULT (olddecl)) == FUNCTION_DECL
		   && TREE_CODE (DECL_TEMPLATE_RESULT (newdecl)) == FUNCTION_DECL
		   && compparms (TYPE_ARG_TYPES (TREE_TYPE (DECL_TEMPLATE_RESULT (olddecl))),
				 TYPE_ARG_TYPES (TREE_TYPE (DECL_TEMPLATE_RESULT (newdecl))))
		   && comp_template_parms (DECL_TEMPLATE_PARMS (newdecl),
					   DECL_TEMPLATE_PARMS (olddecl))
		   /* Template functions can be disambiguated by
		      return type.  */
		   && same_type_p (TREE_TYPE (TREE_TYPE (newdecl)),
				   TREE_TYPE (TREE_TYPE (olddecl))))
	    {
	      error ("new declaration %q#D", newdecl);
	      error ("ambiguates old declaration %q+#D", olddecl);
	    }
	  return NULL_TREE;
	}
      if (TREE_CODE (newdecl) == FUNCTION_DECL)
	{
	  if (DECL_EXTERN_C_P (newdecl) && DECL_EXTERN_C_P (olddecl))
	    {
	      error ("declaration of C function %q#D conflicts with",
		     newdecl);
	      error ("previous declaration %q+#D here", olddecl);
	    }
	  else if (compparms (TYPE_ARG_TYPES (TREE_TYPE (newdecl)),
			      TYPE_ARG_TYPES (TREE_TYPE (olddecl))))
	    {
	      error ("new declaration %q#D", newdecl);
	      error ("ambiguates old declaration %q+#D", olddecl);
              return error_mark_node;
	    }
	  else
	    return NULL_TREE;
	}
      else
	{
	  error ("conflicting declaration %q#D", newdecl);
	  error ("%q+D has a previous declaration as %q#D", olddecl, olddecl);
	  return error_mark_node;
	}
    }
  else if (TREE_CODE (newdecl) == FUNCTION_DECL
	    && ((DECL_TEMPLATE_SPECIALIZATION (olddecl)
		 && (!DECL_TEMPLATE_INFO (newdecl)
		     || (DECL_TI_TEMPLATE (newdecl)
			 != DECL_TI_TEMPLATE (olddecl))))
		|| (DECL_TEMPLATE_SPECIALIZATION (newdecl)
		    && (!DECL_TEMPLATE_INFO (olddecl)
			|| (DECL_TI_TEMPLATE (olddecl)
			    != DECL_TI_TEMPLATE (newdecl))))))
    /* It's OK to have a template specialization and a non-template
       with the same type, or to have specializations of two
       different templates with the same type.  Note that if one is a
       specialization, and the other is an instantiation of the same
       template, that we do not exit at this point.  That situation
       can occur if we instantiate a template class, and then
       specialize one of its methods.  This situation is valid, but
       the declarations must be merged in the usual way.  */
    return NULL_TREE;
  else if (TREE_CODE (newdecl) == FUNCTION_DECL
	   && ((DECL_TEMPLATE_INSTANTIATION (olddecl)
		&& !DECL_USE_TEMPLATE (newdecl))
	       || (DECL_TEMPLATE_INSTANTIATION (newdecl)
		   && !DECL_USE_TEMPLATE (olddecl))))
    /* One of the declarations is a template instantiation, and the
       other is not a template at all.  That's OK.  */
    return NULL_TREE;
  else if (TREE_CODE (newdecl) == NAMESPACE_DECL)
    {
      /* In [namespace.alias] we have:

	   In a declarative region, a namespace-alias-definition can be
	   used to redefine a namespace-alias declared in that declarative
	   region to refer only to the namespace to which it already
	   refers.

	 Therefore, if we encounter a second alias directive for the same
	 alias, we can just ignore the second directive.  */
      if (DECL_NAMESPACE_ALIAS (newdecl)
	  && (DECL_NAMESPACE_ALIAS (newdecl)
	      == DECL_NAMESPACE_ALIAS (olddecl)))
	return olddecl;
      /* [namespace.alias]

	 A namespace-name or namespace-alias shall not be declared as
	 the name of any other entity in the same declarative region.
	 A namespace-name defined at global scope shall not be
	 declared as the name of any other entity in any global scope
	 of the program.  */
      error ("declaration of namespace %qD conflicts with", newdecl);
      error ("previous declaration of namespace %q+D here", olddecl);
      return error_mark_node;
    }
  else
    {
      const char *errmsg = redeclaration_error_message (newdecl, olddecl);
      if (errmsg)
	{
	  error (errmsg, newdecl);
	  if (DECL_NAME (olddecl) != NULL_TREE)
	    error ((DECL_INITIAL (olddecl) && namespace_bindings_p ())
			 ? "%q+#D previously defined here"
			 : "%q+#D previously declared here", olddecl);
	  return error_mark_node;
	}
      else if (TREE_CODE (olddecl) == FUNCTION_DECL
	       && DECL_INITIAL (olddecl) != NULL_TREE
	       && TYPE_ARG_TYPES (TREE_TYPE (olddecl)) == NULL_TREE
	       && TYPE_ARG_TYPES (TREE_TYPE (newdecl)) != NULL_TREE)
	{
	  /* Prototype decl follows defn w/o prototype.  */
	  warning (0, "prototype for %q+#D", newdecl);
	  warning (0, "%Jfollows non-prototype definition here", olddecl);
	}
      else if ((TREE_CODE (olddecl) == FUNCTION_DECL
		|| TREE_CODE (olddecl) == VAR_DECL)
	       && DECL_LANGUAGE (newdecl) != DECL_LANGUAGE (olddecl))
	{
	  /* [dcl.link]
	     If two declarations of the same function or object
	     specify different linkage-specifications ..., the program
	     is ill-formed.... Except for functions with C++ linkage,
	     a function declaration without a linkage specification
	     shall not precede the first linkage specification for
	     that function.  A function can be declared without a
	     linkage specification after an explicit linkage
	     specification has been seen; the linkage explicitly
	     specified in the earlier declaration is not affected by
	     such a function declaration.

	     DR 563 raises the question why the restrictions on
	     functions should not also apply to objects.  Older
	     versions of G++ silently ignore the linkage-specification
	     for this example:

	       namespace N { 
                 extern int i;
   	         extern "C" int i;
               }

             which is clearly wrong.  Therefore, we now treat objects
	     like functions.  */
	  if (current_lang_depth () == 0)
	    {
	      /* There is no explicit linkage-specification, so we use
		 the linkage from the previous declaration.  */
	      if (!DECL_LANG_SPECIFIC (newdecl))
		retrofit_lang_decl (newdecl);
	      SET_DECL_LANGUAGE (newdecl, DECL_LANGUAGE (olddecl));
	    }
	  else
	    {
	      error ("previous declaration of %q+#D with %qL linkage",
		     olddecl, DECL_LANGUAGE (olddecl));
	      error ("conflicts with new declaration with %qL linkage",
		     DECL_LANGUAGE (newdecl));
	    }
	}

      if (DECL_LANG_SPECIFIC (olddecl) && DECL_USE_TEMPLATE (olddecl))
	;
      else if (TREE_CODE (olddecl) == FUNCTION_DECL)
	{
	  tree t1 = TYPE_ARG_TYPES (TREE_TYPE (olddecl));
	  tree t2 = TYPE_ARG_TYPES (TREE_TYPE (newdecl));
	  int i = 1;

	  if (TREE_CODE (TREE_TYPE (newdecl)) == METHOD_TYPE)
	    t1 = TREE_CHAIN (t1), t2 = TREE_CHAIN (t2);

	  for (; t1 && t1 != void_list_node;
	       t1 = TREE_CHAIN (t1), t2 = TREE_CHAIN (t2), i++)
	    if (TREE_PURPOSE (t1) && TREE_PURPOSE (t2))
	      {
		if (1 == simple_cst_equal (TREE_PURPOSE (t1),
					   TREE_PURPOSE (t2)))
		  {
		    pedwarn ("default argument given for parameter %d of %q#D",
			     i, newdecl);
		    pedwarn ("after previous specification in %q+#D", olddecl);
		  }
		else
		  {
		    error ("default argument given for parameter %d of %q#D",
			   i, newdecl);
		    error ("after previous specification in %q+#D",
				 olddecl);
		  }
	      }

	  if (DECL_DECLARED_INLINE_P (newdecl)
	      && ! DECL_DECLARED_INLINE_P (olddecl)
	      && TREE_ADDRESSABLE (olddecl) && warn_inline)
	    {
	      warning (0, "%q#D was used before it was declared inline", newdecl);
	      warning (0, "%Jprevious non-inline declaration here", olddecl);
	    }
	}
    }

  /* Do not merge an implicit typedef with an explicit one.  In:

       class A;
       ...
       typedef class A A __attribute__ ((foo));

     the attribute should apply only to the typedef.  */
  if (TREE_CODE (olddecl) == TYPE_DECL
      && (DECL_IMPLICIT_TYPEDEF_P (olddecl)
	  || DECL_IMPLICIT_TYPEDEF_P (newdecl)))
    return NULL_TREE;

  /* If new decl is `static' and an `extern' was seen previously,
     warn about it.  */
  warn_extern_redeclared_static (newdecl, olddecl);

  /* We have committed to returning 1 at this point.  */
  if (TREE_CODE (newdecl) == FUNCTION_DECL)
    {
      /* Now that functions must hold information normally held
	 by field decls, there is extra work to do so that
	 declaration information does not get destroyed during
	 definition.  */
      if (DECL_VINDEX (olddecl))
	DECL_VINDEX (newdecl) = DECL_VINDEX (olddecl);
      if (DECL_CONTEXT (olddecl))
	DECL_CONTEXT (newdecl) = DECL_CONTEXT (olddecl);
      DECL_STATIC_CONSTRUCTOR (newdecl) |= DECL_STATIC_CONSTRUCTOR (olddecl);
      DECL_STATIC_DESTRUCTOR (newdecl) |= DECL_STATIC_DESTRUCTOR (olddecl);
      DECL_PURE_VIRTUAL_P (newdecl) |= DECL_PURE_VIRTUAL_P (olddecl);
      DECL_VIRTUAL_P (newdecl) |= DECL_VIRTUAL_P (olddecl);
      DECL_INVALID_OVERRIDER_P (newdecl) |= DECL_INVALID_OVERRIDER_P (olddecl);
      DECL_THIS_STATIC (newdecl) |= DECL_THIS_STATIC (olddecl);
      if (DECL_OVERLOADED_OPERATOR_P (olddecl) != ERROR_MARK)
	SET_OVERLOADED_OPERATOR_CODE
	  (newdecl, DECL_OVERLOADED_OPERATOR_P (olddecl));
      new_defines_function = DECL_INITIAL (newdecl) != NULL_TREE;

      /* Optionally warn about more than one declaration for the same
	 name, but don't warn about a function declaration followed by a
	 definition.  */
      if (warn_redundant_decls && ! DECL_ARTIFICIAL (olddecl)
	  && !(new_defines_function && DECL_INITIAL (olddecl) == NULL_TREE)
	  /* Don't warn about extern decl followed by definition.  */
	  && !(DECL_EXTERNAL (olddecl) && ! DECL_EXTERNAL (newdecl))
	  /* Don't warn about friends, let add_friend take care of it.  */
	  && ! (newdecl_is_friend || DECL_FRIEND_P (olddecl)))
	{
	  warning (OPT_Wredundant_decls, "redundant redeclaration of %qD in same scope", newdecl);
	  warning (OPT_Wredundant_decls, "previous declaration of %q+D", olddecl);
	}
    }

  /* Deal with C++: must preserve virtual function table size.  */
  if (TREE_CODE (olddecl) == TYPE_DECL)
    {
      tree newtype = TREE_TYPE (newdecl);
      tree oldtype = TREE_TYPE (olddecl);

      if (newtype != error_mark_node && oldtype != error_mark_node
	  && TYPE_LANG_SPECIFIC (newtype) && TYPE_LANG_SPECIFIC (oldtype))
	CLASSTYPE_FRIEND_CLASSES (newtype)
	  = CLASSTYPE_FRIEND_CLASSES (oldtype);

      DECL_ORIGINAL_TYPE (newdecl) = DECL_ORIGINAL_TYPE (olddecl);
    }

  /* Copy all the DECL_... slots specified in the new decl
     except for any that we copy here from the old type.  */
  DECL_ATTRIBUTES (newdecl)
    = (*targetm.merge_decl_attributes) (olddecl, newdecl);

  if (TREE_CODE (newdecl) == TEMPLATE_DECL)
    {
      tree old_result;
      tree new_result;
      old_result = DECL_TEMPLATE_RESULT (olddecl);
      new_result = DECL_TEMPLATE_RESULT (newdecl);
      TREE_TYPE (olddecl) = TREE_TYPE (old_result);
      DECL_TEMPLATE_SPECIALIZATIONS (olddecl)
	= chainon (DECL_TEMPLATE_SPECIALIZATIONS (olddecl),
		   DECL_TEMPLATE_SPECIALIZATIONS (newdecl));

      if (DECL_FUNCTION_TEMPLATE_P (newdecl))
	{
	  DECL_INLINE (old_result)
	    |= DECL_INLINE (new_result);
	  DECL_DECLARED_INLINE_P (old_result)
	    |= DECL_DECLARED_INLINE_P (new_result);
	  check_redeclaration_exception_specification (newdecl, olddecl);
	}

      /* If the new declaration is a definition, update the file and
	 line information on the declaration.  */
      if (DECL_INITIAL (old_result) == NULL_TREE
	  && DECL_INITIAL (new_result) != NULL_TREE)
	{
	  DECL_SOURCE_LOCATION (olddecl)
	    = DECL_SOURCE_LOCATION (old_result)
	    = DECL_SOURCE_LOCATION (newdecl);
	  if (DECL_FUNCTION_TEMPLATE_P (newdecl))
	    DECL_ARGUMENTS (old_result)
	      = DECL_ARGUMENTS (new_result);
	}

      return olddecl;
    }

  if (types_match)
    {
      /* Automatically handles default parameters.  */
      tree oldtype = TREE_TYPE (olddecl);
      tree newtype;

      /* Merge the data types specified in the two decls.  */
      newtype = merge_types (TREE_TYPE (newdecl), TREE_TYPE (olddecl));

      /* If merge_types produces a non-typedef type, just use the old type.  */
      if (TREE_CODE (newdecl) == TYPE_DECL
	  && newtype == DECL_ORIGINAL_TYPE (newdecl))
	newtype = oldtype;

      if (TREE_CODE (newdecl) == VAR_DECL)
	{
	  DECL_THIS_EXTERN (newdecl) |= DECL_THIS_EXTERN (olddecl);
	  DECL_INITIALIZED_P (newdecl) |= DECL_INITIALIZED_P (olddecl);
	  DECL_NONTRIVIALLY_INITIALIZED_P (newdecl)
	    |= DECL_NONTRIVIALLY_INITIALIZED_P (olddecl);
	  DECL_INITIALIZED_BY_CONSTANT_EXPRESSION_P (newdecl)
	    |= DECL_INITIALIZED_BY_CONSTANT_EXPRESSION_P (olddecl);

	  /* Merge the threadprivate attribute from OLDDECL into NEWDECL.  */
	  if (DECL_LANG_SPECIFIC (olddecl)
	      && CP_DECL_THREADPRIVATE_P (olddecl))
	    {
	      /* Allocate a LANG_SPECIFIC structure for NEWDECL, if needed.  */
	      if (!DECL_LANG_SPECIFIC (newdecl))
		retrofit_lang_decl (newdecl);

	      DECL_TLS_MODEL (newdecl) = DECL_TLS_MODEL (olddecl);
	      CP_DECL_THREADPRIVATE_P (newdecl) = 1;
	    }
	}

      /* Do this after calling `merge_types' so that default
	 parameters don't confuse us.  */
      else if (TREE_CODE (newdecl) == FUNCTION_DECL)
	check_redeclaration_exception_specification (newdecl, olddecl);
      TREE_TYPE (newdecl) = TREE_TYPE (olddecl) = newtype;

      if (TREE_CODE (newdecl) == FUNCTION_DECL)
	check_default_args (newdecl);

      /* Lay the type out, unless already done.  */
      if (! same_type_p (newtype, oldtype)
	  && TREE_TYPE (newdecl) != error_mark_node
	  && !(processing_template_decl && uses_template_parms (newdecl)))
	layout_type (TREE_TYPE (newdecl));

      if ((TREE_CODE (newdecl) == VAR_DECL
	   || TREE_CODE (newdecl) == PARM_DECL
	   || TREE_CODE (newdecl) == RESULT_DECL
	   || TREE_CODE (newdecl) == FIELD_DECL
	   || TREE_CODE (newdecl) == TYPE_DECL)
	  && !(processing_template_decl && uses_template_parms (newdecl)))
	layout_decl (newdecl, 0);

      /* Merge the type qualifiers.  */
      if (TREE_READONLY (newdecl))
	TREE_READONLY (olddecl) = 1;
      if (TREE_THIS_VOLATILE (newdecl))
	TREE_THIS_VOLATILE (olddecl) = 1;
      if (TREE_NOTHROW (newdecl))
	TREE_NOTHROW (olddecl) = 1;

      /* Merge deprecatedness.  */
      if (TREE_DEPRECATED (newdecl))
	TREE_DEPRECATED (olddecl) = 1;

      /* Merge the initialization information.  */
      if (DECL_INITIAL (newdecl) == NULL_TREE
	  && DECL_INITIAL (olddecl) != NULL_TREE)
	{
	  DECL_INITIAL (newdecl) = DECL_INITIAL (olddecl);
	  DECL_SOURCE_LOCATION (newdecl) = DECL_SOURCE_LOCATION (olddecl);
	  if (CAN_HAVE_FULL_LANG_DECL_P (newdecl)
	      && DECL_LANG_SPECIFIC (newdecl)
	      && DECL_LANG_SPECIFIC (olddecl))
	    {
	      DECL_SAVED_TREE (newdecl) = DECL_SAVED_TREE (olddecl);
	      DECL_STRUCT_FUNCTION (newdecl) = DECL_STRUCT_FUNCTION (olddecl);
	    }
	}

      /* Merge the section attribute.
	 We want to issue an error if the sections conflict but that must be
	 done later in decl_attributes since we are called before attributes
	 are assigned.  */
      if (DECL_SECTION_NAME (newdecl) == NULL_TREE)
	DECL_SECTION_NAME (newdecl) = DECL_SECTION_NAME (olddecl);

      if (TREE_CODE (newdecl) == FUNCTION_DECL)
	{
	  DECL_NO_INSTRUMENT_FUNCTION_ENTRY_EXIT (newdecl)
	    |= DECL_NO_INSTRUMENT_FUNCTION_ENTRY_EXIT (olddecl);
	  DECL_NO_LIMIT_STACK (newdecl) |= DECL_NO_LIMIT_STACK (olddecl);
	  TREE_THIS_VOLATILE (newdecl) |= TREE_THIS_VOLATILE (olddecl);
	  TREE_READONLY (newdecl) |= TREE_READONLY (olddecl);
	  TREE_NOTHROW (newdecl) |= TREE_NOTHROW (olddecl);
	  DECL_IS_MALLOC (newdecl) |= DECL_IS_MALLOC (olddecl);
	  DECL_IS_PURE (newdecl) |= DECL_IS_PURE (olddecl);
	  /* Keep the old RTL.  */
	  COPY_DECL_RTL (olddecl, newdecl);
	}
      else if (TREE_CODE (newdecl) == VAR_DECL
	       && (DECL_SIZE (olddecl) || !DECL_SIZE (newdecl)))
	{
	  /* Keep the old RTL.  We cannot keep the old RTL if the old
	     declaration was for an incomplete object and the new
	     declaration is not since many attributes of the RTL will
	     change.  */
	  COPY_DECL_RTL (olddecl, newdecl);
	}
    }
  /* If cannot merge, then use the new type and qualifiers,
     and don't preserve the old rtl.  */
  else
    {
      /* Clean out any memory we had of the old declaration.  */
      tree oldstatic = value_member (olddecl, static_aggregates);
      if (oldstatic)
	TREE_VALUE (oldstatic) = error_mark_node;

      TREE_TYPE (olddecl) = TREE_TYPE (newdecl);
      TREE_READONLY (olddecl) = TREE_READONLY (newdecl);
      TREE_THIS_VOLATILE (olddecl) = TREE_THIS_VOLATILE (newdecl);
      TREE_SIDE_EFFECTS (olddecl) = TREE_SIDE_EFFECTS (newdecl);
    }

  /* Merge the storage class information.  */
  merge_weak (newdecl, olddecl);

  DECL_ONE_ONLY (newdecl) |= DECL_ONE_ONLY (olddecl);
  DECL_DEFER_OUTPUT (newdecl) |= DECL_DEFER_OUTPUT (olddecl);
  TREE_PUBLIC (newdecl) = TREE_PUBLIC (olddecl);
  TREE_STATIC (olddecl) = TREE_STATIC (newdecl) |= TREE_STATIC (olddecl);
  if (! DECL_EXTERNAL (olddecl))
    DECL_EXTERNAL (newdecl) = 0;

  new_template = NULL_TREE;
  if (DECL_LANG_SPECIFIC (newdecl) && DECL_LANG_SPECIFIC (olddecl))
    {
      DECL_INTERFACE_KNOWN (newdecl) |= DECL_INTERFACE_KNOWN (olddecl);
      DECL_NOT_REALLY_EXTERN (newdecl) |= DECL_NOT_REALLY_EXTERN (olddecl);
      DECL_COMDAT (newdecl) |= DECL_COMDAT (olddecl);
      DECL_TEMPLATE_INSTANTIATED (newdecl)
	|= DECL_TEMPLATE_INSTANTIATED (olddecl);

      /* If the OLDDECL is an instantiation and/or specialization,
	 then the NEWDECL must be too.  But, it may not yet be marked
	 as such if the caller has created NEWDECL, but has not yet
	 figured out that it is a redeclaration.  */
      if (!DECL_USE_TEMPLATE (newdecl))
	DECL_USE_TEMPLATE (newdecl) = DECL_USE_TEMPLATE (olddecl);

      /* Don't really know how much of the language-specific
	 values we should copy from old to new.  */
      DECL_IN_AGGR_P (newdecl) = DECL_IN_AGGR_P (olddecl);
      DECL_LANG_SPECIFIC (newdecl)->decl_flags.u2 =
	DECL_LANG_SPECIFIC (olddecl)->decl_flags.u2;
      DECL_NONCONVERTING_P (newdecl) = DECL_NONCONVERTING_P (olddecl);
      DECL_REPO_AVAILABLE_P (newdecl) = DECL_REPO_AVAILABLE_P (olddecl);
      if (DECL_TEMPLATE_INFO (newdecl))
	new_template = DECL_TI_TEMPLATE (newdecl);
      DECL_TEMPLATE_INFO (newdecl) = DECL_TEMPLATE_INFO (olddecl);
      DECL_INITIALIZED_IN_CLASS_P (newdecl)
	|= DECL_INITIALIZED_IN_CLASS_P (olddecl);
      olddecl_friend = DECL_FRIEND_P (olddecl);
      hidden_friend = (DECL_ANTICIPATED (olddecl)
		       && DECL_HIDDEN_FRIEND_P (olddecl)
		       && newdecl_is_friend);

      /* Only functions have DECL_BEFRIENDING_CLASSES.  */
      if (TREE_CODE (newdecl) == FUNCTION_DECL
	  || DECL_FUNCTION_TEMPLATE_P (newdecl))
	{
	  DECL_BEFRIENDING_CLASSES (newdecl)
	    = chainon (DECL_BEFRIENDING_CLASSES (newdecl),
		       DECL_BEFRIENDING_CLASSES (olddecl));
	  /* DECL_THUNKS is only valid for virtual functions,
	     otherwise it is a DECL_FRIEND_CONTEXT.  */
	  if (DECL_VIRTUAL_P (newdecl))
	    DECL_THUNKS (newdecl) = DECL_THUNKS (olddecl);
	}
    }

  if (TREE_CODE (newdecl) == FUNCTION_DECL)
    {
      if (DECL_TEMPLATE_INSTANTIATION (olddecl)
	  && !DECL_TEMPLATE_INSTANTIATION (newdecl))
	{
	  /* If newdecl is not a specialization, then it is not a
	     template-related function at all.  And that means that we
	     should have exited above, returning 0.  */
	  gcc_assert (DECL_TEMPLATE_SPECIALIZATION (newdecl));

	  if (TREE_USED (olddecl))
	    /* From [temp.expl.spec]:

	       If a template, a member template or the member of a class
	       template is explicitly specialized then that
	       specialization shall be declared before the first use of
	       that specialization that would cause an implicit
	       instantiation to take place, in every translation unit in
	       which such a use occurs.  */
	    error ("explicit specialization of %qD after first use",
		      olddecl);

	  SET_DECL_TEMPLATE_SPECIALIZATION (olddecl);

	  /* Don't propagate visibility from the template to the
	     specialization here.  We'll do that in determine_visibility if
	     appropriate.  */
	  DECL_VISIBILITY_SPECIFIED (olddecl) = 0;

	  /* [temp.expl.spec/14] We don't inline explicit specialization
	     just because the primary template says so.  */
	}
      else
	{
	  if (DECL_PENDING_INLINE_INFO (newdecl) == 0)
	    DECL_PENDING_INLINE_INFO (newdecl) = DECL_PENDING_INLINE_INFO (olddecl);

	  DECL_DECLARED_INLINE_P (newdecl) |= DECL_DECLARED_INLINE_P (olddecl);

	  /* If either decl says `inline', this fn is inline, unless
	     its definition was passed already.  */
	  if (DECL_INLINE (newdecl) && DECL_INITIAL (olddecl) == NULL_TREE)
	    DECL_INLINE (olddecl) = 1;
	  DECL_INLINE (newdecl) = DECL_INLINE (olddecl);

	  DECL_UNINLINABLE (newdecl) = DECL_UNINLINABLE (olddecl)
	    = (DECL_UNINLINABLE (newdecl) || DECL_UNINLINABLE (olddecl));
	}

      /* Preserve abstractness on cloned [cd]tors.  */
      DECL_ABSTRACT (newdecl) = DECL_ABSTRACT (olddecl);

      if (! types_match)
	{
	  SET_DECL_LANGUAGE (olddecl, DECL_LANGUAGE (newdecl));
	  COPY_DECL_ASSEMBLER_NAME (newdecl, olddecl);
	  COPY_DECL_RTL (newdecl, olddecl);
	}
      if (! types_match || new_defines_function)
	{
	  /* These need to be copied so that the names are available.
	     Note that if the types do match, we'll preserve inline
	     info and other bits, but if not, we won't.  */
	  DECL_ARGUMENTS (olddecl) = DECL_ARGUMENTS (newdecl);
	  DECL_RESULT (olddecl) = DECL_RESULT (newdecl);
	}
      if (new_defines_function)
	/* If defining a function declared with other language
	   linkage, use the previously declared language linkage.  */
	SET_DECL_LANGUAGE (newdecl, DECL_LANGUAGE (olddecl));
      else if (types_match)
	{
	  /* If redeclaring a builtin function, and not a definition,
	     it stays built in.  */
	  if (DECL_BUILT_IN (olddecl))
	    {
	      DECL_BUILT_IN_CLASS (newdecl) = DECL_BUILT_IN_CLASS (olddecl);
	      DECL_FUNCTION_CODE (newdecl) = DECL_FUNCTION_CODE (olddecl);
	      /* If we're keeping the built-in definition, keep the rtl,
		 regardless of declaration matches.  */
	      COPY_DECL_RTL (olddecl, newdecl);
	    }

	  DECL_RESULT (newdecl) = DECL_RESULT (olddecl);
	  /* Don't clear out the arguments if we're redefining a function.  */
	  if (DECL_ARGUMENTS (olddecl))
	    DECL_ARGUMENTS (newdecl) = DECL_ARGUMENTS (olddecl);
	}
    }
  else if (TREE_CODE (newdecl) == NAMESPACE_DECL)
    NAMESPACE_LEVEL (newdecl) = NAMESPACE_LEVEL (olddecl);

  /* Now preserve various other info from the definition.  */
  TREE_ADDRESSABLE (newdecl) = TREE_ADDRESSABLE (olddecl);
  TREE_ASM_WRITTEN (newdecl) = TREE_ASM_WRITTEN (olddecl);
  DECL_COMMON (newdecl) = DECL_COMMON (olddecl);
  COPY_DECL_ASSEMBLER_NAME (olddecl, newdecl);

  /* Warn about conflicting visibility specifications.  */
  if (DECL_VISIBILITY_SPECIFIED (olddecl)
      && DECL_VISIBILITY_SPECIFIED (newdecl)
      && DECL_VISIBILITY (newdecl) != DECL_VISIBILITY (olddecl))
    {
      warning (OPT_Wattributes, "%q+D: visibility attribute ignored "
	       "because it", newdecl);
      warning (OPT_Wattributes, "%Jconflicts with previous "
	       "declaration here", olddecl);
    }
  /* Choose the declaration which specified visibility.  */
  if (DECL_VISIBILITY_SPECIFIED (olddecl))
    {
      DECL_VISIBILITY (newdecl) = DECL_VISIBILITY (olddecl);
      DECL_VISIBILITY_SPECIFIED (newdecl) = 1;
    }
  /* Init priority used to be merged from newdecl to olddecl by the memcpy,
     so keep this behavior.  */
  if (TREE_CODE (newdecl) == VAR_DECL && DECL_HAS_INIT_PRIORITY_P (newdecl))
    {
      SET_DECL_INIT_PRIORITY (olddecl, DECL_INIT_PRIORITY (newdecl));
      DECL_HAS_INIT_PRIORITY_P (olddecl) = 1;
    }

  /* The DECL_LANG_SPECIFIC information in OLDDECL will be replaced
     with that from NEWDECL below.  */
  if (DECL_LANG_SPECIFIC (olddecl))
    {
      gcc_assert (DECL_LANG_SPECIFIC (olddecl)
		  != DECL_LANG_SPECIFIC (newdecl));
      ggc_free (DECL_LANG_SPECIFIC (olddecl));
    }

  if (TREE_CODE (newdecl) == FUNCTION_DECL)
    {
      int function_size;

      function_size = sizeof (struct tree_decl_common);

      memcpy ((char *) olddecl + sizeof (struct tree_common),
	      (char *) newdecl + sizeof (struct tree_common),
	      function_size - sizeof (struct tree_common));

      memcpy ((char *) olddecl + sizeof (struct tree_decl_common),
	      (char *) newdecl + sizeof (struct tree_decl_common),
	      sizeof (struct tree_function_decl) - sizeof (struct tree_decl_common));
      if (new_template)
	/* If newdecl is a template instantiation, it is possible that
	   the following sequence of events has occurred:

	   o A friend function was declared in a class template.  The
	   class template was instantiated.

	   o The instantiation of the friend declaration was
	   recorded on the instantiation list, and is newdecl.

	   o Later, however, instantiate_class_template called pushdecl
	   on the newdecl to perform name injection.  But, pushdecl in
	   turn called duplicate_decls when it discovered that another
	   declaration of a global function with the same name already
	   existed.

	   o Here, in duplicate_decls, we decided to clobber newdecl.

	   If we're going to do that, we'd better make sure that
	   olddecl, and not newdecl, is on the list of
	   instantiations so that if we try to do the instantiation
	   again we won't get the clobbered declaration.  */
	reregister_specialization (newdecl,
				   new_template,
				   olddecl);
    }
  else
    {
      size_t size = tree_code_size (TREE_CODE (olddecl));
      memcpy ((char *) olddecl + sizeof (struct tree_common),
	      (char *) newdecl + sizeof (struct tree_common),
	      sizeof (struct tree_decl_common) - sizeof (struct tree_common));
      switch (TREE_CODE (olddecl))
	{
	case LABEL_DECL:
	case VAR_DECL:
	case RESULT_DECL:
	case PARM_DECL:
	case FIELD_DECL:
	case TYPE_DECL:
	case CONST_DECL:
	  {
	    memcpy ((char *) olddecl + sizeof (struct tree_decl_common),
		    (char *) newdecl + sizeof (struct tree_decl_common),
		    size - sizeof (struct tree_decl_common)
		    + TREE_CODE_LENGTH (TREE_CODE (newdecl)) * sizeof (char *));
	  }
	  break;
	default:
	  memcpy ((char *) olddecl + sizeof (struct tree_decl_common),
		  (char *) newdecl + sizeof (struct tree_decl_common),
		  sizeof (struct tree_decl_non_common) - sizeof (struct tree_decl_common)
		  + TREE_CODE_LENGTH (TREE_CODE (newdecl)) * sizeof (char *));
	  break;
	}
    }
  DECL_UID (olddecl) = olddecl_uid;
  if (olddecl_friend)
    DECL_FRIEND_P (olddecl) = 1;
  if (hidden_friend)
    {
      DECL_ANTICIPATED (olddecl) = 1;
      DECL_HIDDEN_FRIEND_P (olddecl) = 1;
    }

  /* NEWDECL contains the merged attribute lists.
     Update OLDDECL to be the same.  */
  DECL_ATTRIBUTES (olddecl) = DECL_ATTRIBUTES (newdecl);

  /* If OLDDECL had its DECL_RTL instantiated, re-invoke make_decl_rtl
    so that encode_section_info has a chance to look at the new decl
    flags and attributes.  */
  if (DECL_RTL_SET_P (olddecl)
      && (TREE_CODE (olddecl) == FUNCTION_DECL
	  || (TREE_CODE (olddecl) == VAR_DECL
	      && TREE_STATIC (olddecl))))
    make_decl_rtl (olddecl);

  /* The NEWDECL will no longer be needed.  Because every out-of-class
     declaration of a member results in a call to duplicate_decls,
     freeing these nodes represents in a significant savings.  */
  ggc_free (newdecl);

  return olddecl;
}

/* Return zero if the declaration NEWDECL is valid
   when the declaration OLDDECL (assumed to be for the same name)
   has already been seen.
   Otherwise return an error message format string with a %s
   where the identifier should go.  */

static const char *
redeclaration_error_message (tree newdecl, tree olddecl)
{
  if (TREE_CODE (newdecl) == TYPE_DECL)
    {
      /* Because C++ can put things into name space for free,
	 constructs like "typedef struct foo { ... } foo"
	 would look like an erroneous redeclaration.  */
      if (same_type_p (TREE_TYPE (newdecl), TREE_TYPE (olddecl)))
	return NULL;
      else
	return "redefinition of %q#D";
    }
  else if (TREE_CODE (newdecl) == FUNCTION_DECL)
    {
      /* If this is a pure function, its olddecl will actually be
	 the original initialization to `0' (which we force to call
	 abort()).  Don't complain about redefinition in this case.  */
      if (DECL_LANG_SPECIFIC (olddecl) && DECL_PURE_VIRTUAL_P (olddecl)
	  && DECL_INITIAL (olddecl) == NULL_TREE)
	return NULL;

      /* If both functions come from different namespaces, this is not
	 a redeclaration - this is a conflict with a used function.  */
      if (DECL_NAMESPACE_SCOPE_P (olddecl)
	  && DECL_CONTEXT (olddecl) != DECL_CONTEXT (newdecl)
	  && ! decls_match (olddecl, newdecl))
	return "%qD conflicts with used function";

      /* We'll complain about linkage mismatches in
	 warn_extern_redeclared_static.  */

      /* Defining the same name twice is no good.  */
      if (DECL_INITIAL (olddecl) != NULL_TREE
	  && DECL_INITIAL (newdecl) != NULL_TREE)
	{
	  if (DECL_NAME (olddecl) == NULL_TREE)
	    return "%q#D not declared in class";
	  else
	    return "redefinition of %q#D";
	}
      return NULL;
    }
  else if (TREE_CODE (newdecl) == TEMPLATE_DECL)
    {
      tree nt, ot;

      if (TREE_CODE (DECL_TEMPLATE_RESULT (newdecl)) == TYPE_DECL)
	{
	  if (COMPLETE_TYPE_P (TREE_TYPE (newdecl))
	      && COMPLETE_TYPE_P (TREE_TYPE (olddecl)))
	    return "redefinition of %q#D";
	  return NULL;
	}

      if (TREE_CODE (DECL_TEMPLATE_RESULT (newdecl)) != FUNCTION_DECL
	  || (DECL_TEMPLATE_RESULT (newdecl)
	      == DECL_TEMPLATE_RESULT (olddecl)))
	return NULL;

      nt = DECL_TEMPLATE_RESULT (newdecl);
      if (DECL_TEMPLATE_INFO (nt))
	nt = DECL_TEMPLATE_RESULT (template_for_substitution (nt));
      ot = DECL_TEMPLATE_RESULT (olddecl);
      if (DECL_TEMPLATE_INFO (ot))
	ot = DECL_TEMPLATE_RESULT (template_for_substitution (ot));
      if (DECL_INITIAL (nt) && DECL_INITIAL (ot))
	return "redefinition of %q#D";

      return NULL;
    }
  else if (TREE_CODE (newdecl) == VAR_DECL
	   && DECL_THREAD_LOCAL_P (newdecl) != DECL_THREAD_LOCAL_P (olddecl)
	   && (! DECL_LANG_SPECIFIC (olddecl)
	       || ! CP_DECL_THREADPRIVATE_P (olddecl)
	       || DECL_THREAD_LOCAL_P (newdecl)))
    {
      /* Only variables can be thread-local, and all declarations must
	 agree on this property.  */
      if (DECL_THREAD_LOCAL_P (newdecl))
	return "thread-local declaration of %q#D follows "
	       "non-thread-local declaration";
      else
	return "non-thread-local declaration of %q#D follows "
	       "thread-local declaration";
    }
  else if (toplevel_bindings_p () || DECL_NAMESPACE_SCOPE_P (newdecl))
    {
      /* The objects have been declared at namespace scope.  If either
	 is a member of an anonymous union, then this is an invalid
	 redeclaration.  For example:

	   int i;
	   union { int i; };

	   is invalid.  */
      if (DECL_ANON_UNION_VAR_P (newdecl)
	  || DECL_ANON_UNION_VAR_P (olddecl))
	return "redeclaration of %q#D";
      /* If at least one declaration is a reference, there is no
	 conflict.  For example:

	   int i = 3;
	   extern int i;

	 is valid.  */
      if (DECL_EXTERNAL (newdecl) || DECL_EXTERNAL (olddecl))
	return NULL;
      /* Reject two definitions.  */
      return "redefinition of %q#D";
    }
  else
    {
      /* Objects declared with block scope:  */
      /* Reject two definitions, and reject a definition
	 together with an external reference.  */
      if (!(DECL_EXTERNAL (newdecl) && DECL_EXTERNAL (olddecl)))
	return "redeclaration of %q#D";
      return NULL;
    }
}

/* Hash and equality functions for the named_label table.  */

static hashval_t
named_label_entry_hash (const void *data)
{
  const struct named_label_entry *ent = (const struct named_label_entry *) data;
  return DECL_UID (ent->label_decl);
}

static int
named_label_entry_eq (const void *a, const void *b)
{
  const struct named_label_entry *ent_a = (const struct named_label_entry *) a;
  const struct named_label_entry *ent_b = (const struct named_label_entry *) b;
  return ent_a->label_decl == ent_b->label_decl;
}

/* Create a new label, named ID.  */

static tree
make_label_decl (tree id, int local_p)
{
  struct named_label_entry *ent;
  void **slot;
  tree decl;

  decl = build_decl (LABEL_DECL, id, void_type_node);

  DECL_CONTEXT (decl) = current_function_decl;
  DECL_MODE (decl) = VOIDmode;
  C_DECLARED_LABEL_FLAG (decl) = local_p;

  /* Say where one reference is to the label, for the sake of the
     error if it is not defined.  */
  DECL_SOURCE_LOCATION (decl) = input_location;

  /* Record the fact that this identifier is bound to this label.  */
  SET_IDENTIFIER_LABEL_VALUE (id, decl);

  /* Create the label htab for the function on demand.  */
  if (!named_labels)
    named_labels = htab_create_ggc (13, named_label_entry_hash,
				    named_label_entry_eq, NULL);

  /* Record this label on the list of labels used in this function.
     We do this before calling make_label_decl so that we get the
     IDENTIFIER_LABEL_VALUE before the new label is declared.  */
  ent = GGC_CNEW (struct named_label_entry);
  ent->label_decl = decl;

  slot = htab_find_slot (named_labels, ent, INSERT);
  gcc_assert (*slot == NULL);
  *slot = ent;

  return decl;
}

/* Look for a label named ID in the current function.  If one cannot
   be found, create one.  (We keep track of used, but undefined,
   labels, and complain about them at the end of a function.)  */

tree
lookup_label (tree id)
{
  tree decl;

  timevar_push (TV_NAME_LOOKUP);
  /* You can't use labels at global scope.  */
  if (current_function_decl == NULL_TREE)
    {
      error ("label %qE referenced outside of any function", id);
      POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, NULL_TREE);
    }

  /* See if we've already got this label.  */
  decl = IDENTIFIER_LABEL_VALUE (id);
  if (decl != NULL_TREE && DECL_CONTEXT (decl) == current_function_decl)
    POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, decl);

  decl = make_label_decl (id, /*local_p=*/0);
  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, decl);
}

/* Declare a local label named ID.  */

tree
declare_local_label (tree id)
{
  tree decl, shadow;

  /* Add a new entry to the SHADOWED_LABELS list so that when we leave
     this scope we can restore the old value of IDENTIFIER_TYPE_VALUE.  */
  shadow = tree_cons (IDENTIFIER_LABEL_VALUE (id), NULL_TREE,
		      current_binding_level->shadowed_labels);
  current_binding_level->shadowed_labels = shadow;

  decl = make_label_decl (id, /*local_p=*/1);
  TREE_VALUE (shadow) = decl;

  return decl;
}

/* Returns nonzero if it is ill-formed to jump past the declaration of
   DECL.  Returns 2 if it's also a real problem.  */

static int
decl_jump_unsafe (tree decl)
{
  if (TREE_CODE (decl) != VAR_DECL || TREE_STATIC (decl)
      || TREE_TYPE (decl) == error_mark_node)
    return 0;

  if (TYPE_NEEDS_CONSTRUCTING (TREE_TYPE (decl))
      || DECL_NONTRIVIALLY_INITIALIZED_P (decl))
    return 2;

  if (pod_type_p (TREE_TYPE (decl)))
    return 0;

  /* The POD stuff is just pedantry; why should it matter if the class
     contains a field of pointer to member type?  */
  return 1;
}

/* A subroutine of check_previous_goto_1 to identify a branch to the user.  */

static void
identify_goto (tree decl, const location_t *locus)
{
  if (decl)
    pedwarn ("jump to label %qD", decl);
  else
    pedwarn ("jump to case label");
  if (locus)
    pedwarn ("%H  from here", locus);
}

/* Check that a single previously seen jump to a newly defined label
   is OK.  DECL is the LABEL_DECL or 0; LEVEL is the binding_level for
   the jump context; NAMES are the names in scope in LEVEL at the jump
   context; LOCUS is the source position of the jump or 0.  Returns
   true if all is well.  */

static bool
check_previous_goto_1 (tree decl, struct cp_binding_level* level, tree names,
		       bool exited_omp, const location_t *locus)
{
  struct cp_binding_level *b;
  bool identified = false, saw_eh = false, saw_omp = false;

  if (exited_omp)
    {
      identify_goto (decl, locus);
      error ("  exits OpenMP structured block");
      identified = saw_omp = true;
    }

  for (b = current_binding_level; b ; b = b->level_chain)
    {
      tree new_decls, old_decls = (b == level ? names : NULL_TREE);

      for (new_decls = b->names; new_decls != old_decls;
	   new_decls = TREE_CHAIN (new_decls))
	{
	  int problem = decl_jump_unsafe (new_decls);
	  if (! problem)
	    continue;

	  if (!identified)
	    {
	      identify_goto (decl, locus);
	      identified = true;
	    }
	  if (problem > 1)
	    error ("  crosses initialization of %q+#D", new_decls);
	  else
	    pedwarn ("  enters scope of non-POD %q+#D", new_decls);
	}

      if (b == level)
	break;
      if ((b->kind == sk_try || b->kind == sk_catch) && !saw_eh)
	{
	  if (!identified)
	    {
	      identify_goto (decl, locus);
	      identified = true;
	    }
	  if (b->kind == sk_try)
	    error ("  enters try block");
	  else
	    error ("  enters catch block");
	  saw_eh = true;
	}
      if (b->kind == sk_omp && !saw_omp)
	{
	  if (!identified)
	    {
	      identify_goto (decl, locus);
	      identified = true;
	    }
	  error ("  enters OpenMP structured block");
	  saw_omp = true;
	}
    }

  return !identified;
}

static void
check_previous_goto (tree decl, struct named_label_use_entry *use)
{
  check_previous_goto_1 (decl, use->binding_level,
			 use->names_in_scope, use->in_omp_scope,
			 &use->o_goto_locus);
}

static bool
check_switch_goto (struct cp_binding_level* level)
{
  return check_previous_goto_1 (NULL_TREE, level, level->names, false, NULL);
}

/* Check that a new jump to a label DECL is OK.  Called by
   finish_goto_stmt.  */

void
check_goto (tree decl)
{
  struct named_label_entry *ent, dummy;
  bool saw_catch = false, identified = false;
  tree bad;

  /* We can't know where a computed goto is jumping.
     So we assume that it's OK.  */
  if (TREE_CODE (decl) != LABEL_DECL)
    return;

  /* We didn't record any information about this label when we created it,
     and there's not much point since it's trivial to analyze as a return.  */
  if (decl == cdtor_label)
    return;

  dummy.label_decl = decl;
  ent = (struct named_label_entry *) htab_find (named_labels, &dummy);
  gcc_assert (ent != NULL);

  /* If the label hasn't been defined yet, defer checking.  */
  if (! DECL_INITIAL (decl))
    {
      struct named_label_use_entry *new_use;

      /* Don't bother creating another use if the last goto had the
	 same data, and will therefore create the same set of errors.  */
      if (ent->uses
	  && ent->uses->names_in_scope == current_binding_level->names)
	return;

      new_use = GGC_NEW (struct named_label_use_entry);
      new_use->binding_level = current_binding_level;
      new_use->names_in_scope = current_binding_level->names;
      new_use->o_goto_locus = input_location;
      new_use->in_omp_scope = false;

      new_use->next = ent->uses;
      ent->uses = new_use;
      return;
    }

  if (ent->in_try_scope || ent->in_catch_scope
      || ent->in_omp_scope || ent->bad_decls)
    {
      pedwarn ("jump to label %q+D", decl);
      pedwarn ("  from here");
      identified = true;
    }

  for (bad = ent->bad_decls; bad; bad = TREE_CHAIN (bad))
    {
      tree b = TREE_VALUE (bad);
      int u = decl_jump_unsafe (b);

      if (u > 1 && DECL_ARTIFICIAL (b))
	{
	  /* Can't skip init of __exception_info.  */
	  error ("%J  enters catch block", b);
	  saw_catch = true;
	}
      else if (u > 1)
	error ("  skips initialization of %q+#D", b);
      else
	pedwarn ("  enters scope of non-POD %q+#D", b);
    }

  if (ent->in_try_scope)
    error ("  enters try block");
  else if (ent->in_catch_scope && !saw_catch)
    error ("  enters catch block");

  if (ent->in_omp_scope)
    error ("  enters OpenMP structured block");
  else if (flag_openmp)
    {
      struct cp_binding_level *b;
      for (b = current_binding_level; b ; b = b->level_chain)
	{
	  if (b == ent->binding_level)
	    break;
	  if (b->kind == sk_omp)
	    {
	      if (!identified)
		{
		  pedwarn ("jump to label %q+D", decl);
		  pedwarn ("  from here");
		  identified = true;
		}
	      error ("  exits OpenMP structured block");
	      break;
	    }
	}
    }
}

/* Check that a return is ok wrt OpenMP structured blocks.
   Called by finish_return_stmt.  Returns true if all is well.  */

bool
check_omp_return (void)
{
  struct cp_binding_level *b;
  for (b = current_binding_level; b ; b = b->level_chain)
    if (b->kind == sk_omp)
      {
	error ("invalid exit from OpenMP structured block");
	return false;
      }
  return true;
}

/* Define a label, specifying the location in the source file.
   Return the LABEL_DECL node for the label.  */

tree
define_label (location_t location, tree name)
{
  struct named_label_entry *ent, dummy;
  struct cp_binding_level *p;
  tree decl;

  timevar_push (TV_NAME_LOOKUP);

  decl = lookup_label (name);

  dummy.label_decl = decl;
  ent = (struct named_label_entry *) htab_find (named_labels, &dummy);
  gcc_assert (ent != NULL);

  /* After labels, make any new cleanups in the function go into their
     own new (temporary) binding contour.  */
  for (p = current_binding_level;
       p->kind != sk_function_parms;
       p = p->level_chain)
    p->more_cleanups_ok = 0;

  if (name == get_identifier ("wchar_t"))
    pedwarn ("label named wchar_t");

  if (DECL_INITIAL (decl) != NULL_TREE)
    {
      error ("duplicate label %qD", decl);
      POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, error_mark_node);
    }
  else
    {
      struct named_label_use_entry *use;

      /* Mark label as having been defined.  */
      DECL_INITIAL (decl) = error_mark_node;
      /* Say where in the source.  */
      DECL_SOURCE_LOCATION (decl) = location;

      ent->binding_level = current_binding_level;
      ent->names_in_scope = current_binding_level->names;

      for (use = ent->uses; use ; use = use->next)
	check_previous_goto (decl, use);
      ent->uses = NULL;
    }

  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, decl);
}

struct cp_switch
{
  struct cp_binding_level *level;
  struct cp_switch *next;
  /* The SWITCH_STMT being built.  */
  tree switch_stmt;
  /* A splay-tree mapping the low element of a case range to the high
     element, or NULL_TREE if there is no high element.  Used to
     determine whether or not a new case label duplicates an old case
     label.  We need a tree, rather than simply a hash table, because
     of the GNU case range extension.  */
  splay_tree cases;
};

/* A stack of the currently active switch statements.  The innermost
   switch statement is on the top of the stack.  There is no need to
   mark the stack for garbage collection because it is only active
   during the processing of the body of a function, and we never
   collect at that point.  */

static struct cp_switch *switch_stack;

/* Called right after a switch-statement condition is parsed.
   SWITCH_STMT is the switch statement being parsed.  */

void
push_switch (tree switch_stmt)
{
  struct cp_switch *p = XNEW (struct cp_switch);
  p->level = current_binding_level;
  p->next = switch_stack;
  p->switch_stmt = switch_stmt;
  p->cases = splay_tree_new (case_compare, NULL, NULL);
  switch_stack = p;
}

void
pop_switch (void)
{
  struct cp_switch *cs = switch_stack;
  location_t switch_location;

  /* Emit warnings as needed.  */
  if (EXPR_HAS_LOCATION (cs->switch_stmt))
    switch_location = EXPR_LOCATION (cs->switch_stmt);
  else
    switch_location = input_location;
  if (!processing_template_decl)
    c_do_switch_warnings (cs->cases, switch_location,
			  SWITCH_STMT_TYPE (cs->switch_stmt),
			  SWITCH_STMT_COND (cs->switch_stmt));

  splay_tree_delete (cs->cases);
  switch_stack = switch_stack->next;
  free (cs);
}

/* Note that we've seen a definition of a case label, and complain if this
   is a bad place for one.  */

tree
finish_case_label (tree low_value, tree high_value)
{
  tree cond, r;
  struct cp_binding_level *p;

  if (processing_template_decl)
    {
      tree label;

      /* For templates, just add the case label; we'll do semantic
	 analysis at instantiation-time.  */
      label = build_decl (LABEL_DECL, NULL_TREE, NULL_TREE);
      return add_stmt (build_case_label (low_value, high_value, label));
    }

  /* Find the condition on which this switch statement depends.  */
  cond = SWITCH_STMT_COND (switch_stack->switch_stmt);
  if (cond && TREE_CODE (cond) == TREE_LIST)
    cond = TREE_VALUE (cond);

  if (!check_switch_goto (switch_stack->level))
    return error_mark_node;

  r = c_add_case_label (switch_stack->cases, cond, TREE_TYPE (cond),
			low_value, high_value);

  /* After labels, make any new cleanups in the function go into their
     own new (temporary) binding contour.  */
  for (p = current_binding_level;
       p->kind != sk_function_parms;
       p = p->level_chain)
    p->more_cleanups_ok = 0;

  return r;
}

/* Hash a TYPENAME_TYPE.  K is really of type `tree'.  */

static hashval_t
typename_hash (const void* k)
{
  hashval_t hash;
  tree t = (tree) k;

  hash = (htab_hash_pointer (TYPE_CONTEXT (t))
	  ^ htab_hash_pointer (DECL_NAME (TYPE_NAME (t))));

  return hash;
}

typedef struct typename_info {
  tree scope;
  tree name;
  tree template_id;
  bool enum_p;
  bool class_p;
} typename_info;

/* Compare two TYPENAME_TYPEs.  K1 and K2 are really of type `tree'.  */

static int
typename_compare (const void * k1, const void * k2)
{
  tree t1;
  const typename_info *t2;

  t1 = (tree) k1;
  t2 = (const typename_info *) k2;

  return (DECL_NAME (TYPE_NAME (t1)) == t2->name
	  && TYPE_CONTEXT (t1) == t2->scope
	  && TYPENAME_TYPE_FULLNAME (t1) == t2->template_id
	  && TYPENAME_IS_ENUM_P (t1) == t2->enum_p
	  && TYPENAME_IS_CLASS_P (t1) == t2->class_p);
}

/* Build a TYPENAME_TYPE.  If the type is `typename T::t', CONTEXT is
   the type of `T', NAME is the IDENTIFIER_NODE for `t'.

   Returns the new TYPENAME_TYPE.  */

static GTY ((param_is (union tree_node))) htab_t typename_htab;

static tree
build_typename_type (tree context, tree name, tree fullname,
		     enum tag_types tag_type)
{
  tree t;
  tree d;
  typename_info ti;
  void **e;
  hashval_t hash;

  if (typename_htab == NULL)
    typename_htab = htab_create_ggc (61, &typename_hash,
				     &typename_compare, NULL);

  ti.scope = FROB_CONTEXT (context);
  ti.name = name;
  ti.template_id = fullname;
  ti.enum_p = tag_type == enum_type;
  ti.class_p = (tag_type == class_type
		|| tag_type == record_type
		|| tag_type == union_type);
  hash =  (htab_hash_pointer (ti.scope)
	   ^ htab_hash_pointer (ti.name));

  /* See if we already have this type.  */
  e = htab_find_slot_with_hash (typename_htab, &ti, hash, INSERT);
  if (*e)
    t = (tree) *e;
  else
    {
      /* Build the TYPENAME_TYPE.  */
      t = make_aggr_type (TYPENAME_TYPE);
      TYPE_CONTEXT (t) = ti.scope;
      TYPENAME_TYPE_FULLNAME (t) = ti.template_id;
      TYPENAME_IS_ENUM_P (t) = ti.enum_p;
      TYPENAME_IS_CLASS_P (t) = ti.class_p;

      /* Build the corresponding TYPE_DECL.  */
      d = build_decl (TYPE_DECL, name, t);
      TYPE_NAME (TREE_TYPE (d)) = d;
      TYPE_STUB_DECL (TREE_TYPE (d)) = d;
      DECL_CONTEXT (d) = FROB_CONTEXT (context);
      DECL_ARTIFICIAL (d) = 1;

      /* Store it in the hash table.  */
      *e = t;
    }

  return t;
}

/* Resolve `typename CONTEXT::NAME'.  TAG_TYPE indicates the tag
   provided to name the type.  Returns an appropriate type, unless an
   error occurs, in which case error_mark_node is returned.  If we
   locate a non-artificial TYPE_DECL and TF_KEEP_TYPE_DECL is set, we
   return that, rather than the _TYPE it corresponds to, in other
   cases we look through the type decl.  If TF_ERROR is set, complain
   about errors, otherwise be quiet.  */

tree
make_typename_type (tree context, tree name, enum tag_types tag_type,
		    tsubst_flags_t complain)
{
  tree fullname;
  tree t;
  bool want_template;

  if (name == error_mark_node
      || context == NULL_TREE
      || context == error_mark_node)
    return error_mark_node;

  if (TYPE_P (name))
    {
      if (!(TYPE_LANG_SPECIFIC (name)
	    && (CLASSTYPE_IS_TEMPLATE (name)
		|| CLASSTYPE_USE_TEMPLATE (name))))
	name = TYPE_IDENTIFIER (name);
      else
	/* Create a TEMPLATE_ID_EXPR for the type.  */
	name = build_nt (TEMPLATE_ID_EXPR,
			 CLASSTYPE_TI_TEMPLATE (name),
			 CLASSTYPE_TI_ARGS (name));
    }
  else if (TREE_CODE (name) == TYPE_DECL)
    name = DECL_NAME (name);

  fullname = name;

  if (TREE_CODE (name) == TEMPLATE_ID_EXPR)
    {
      name = TREE_OPERAND (name, 0);
      if (TREE_CODE (name) == TEMPLATE_DECL)
	name = TREE_OPERAND (fullname, 0) = DECL_NAME (name);
      else if (TREE_CODE (name) == OVERLOAD)
	{
	  error ("%qD is not a type", name);
	  return error_mark_node;
	}
    }
  if (TREE_CODE (name) == TEMPLATE_DECL)
    {
      error ("%qD used without template parameters", name);
      return error_mark_node;
    }
  gcc_assert (TREE_CODE (name) == IDENTIFIER_NODE);
  gcc_assert (TYPE_P (context));

  /* When the CONTEXT is a dependent type,  NAME could refer to a
     dependent base class of CONTEXT.  So we cannot peek inside it,
     even if CONTEXT is a currently open scope.  */
  if (dependent_type_p (context))
    return build_typename_type (context, name, fullname, tag_type);

  if (!IS_AGGR_TYPE (context))
    {
      if (complain & tf_error)
	error ("%q#T is not a class", context);
      return error_mark_node;
    }
  
  want_template = TREE_CODE (fullname) == TEMPLATE_ID_EXPR;
  
  /* We should only set WANT_TYPE when we're a nested typename type.
     Then we can give better diagnostics if we find a non-type.  */
  t = lookup_field (context, name, 0, /*want_type=*/true);
  if (!t)
    {
      if (complain & tf_error)
	error (want_template ? "no class template named %q#T in %q#T"
	       : "no type named %q#T in %q#T", name, context);
      return error_mark_node;
    }
  
  if (want_template && !DECL_CLASS_TEMPLATE_P (t))
    {
      if (complain & tf_error)
	error ("%<typename %T::%D%> names %q#T, which is not a class template",
	       context, name, t);
      return error_mark_node;
    }
  if (!want_template && TREE_CODE (t) != TYPE_DECL)
    {
      if (complain & tf_error)
	error ("%<typename %T::%D%> names %q#T, which is not a type",
	       context, name, t);
      return error_mark_node;
    }
  
  if (complain & tf_error)
    perform_or_defer_access_check (TYPE_BINFO (context), t, t);

  if (want_template)
    return lookup_template_class (t, TREE_OPERAND (fullname, 1),
				  NULL_TREE, context,
				  /*entering_scope=*/0,
				  tf_warning_or_error | tf_user);
  
  if (DECL_ARTIFICIAL (t) || !(complain & tf_keep_type_decl))
    t = TREE_TYPE (t);
  
  return t;
}

/* Resolve `CONTEXT::template NAME'.  Returns a TEMPLATE_DECL if the name
   can be resolved or an UNBOUND_CLASS_TEMPLATE, unless an error occurs,
   in which case error_mark_node is returned.

   If PARM_LIST is non-NULL, also make sure that the template parameter
   list of TEMPLATE_DECL matches.

   If COMPLAIN zero, don't complain about any errors that occur.  */

tree
make_unbound_class_template (tree context, tree name, tree parm_list,
			     tsubst_flags_t complain)
{
  tree t;
  tree d;

  if (TYPE_P (name))
    name = TYPE_IDENTIFIER (name);
  else if (DECL_P (name))
    name = DECL_NAME (name);
  gcc_assert (TREE_CODE (name) == IDENTIFIER_NODE);

  if (!dependent_type_p (context)
      || currently_open_class (context))
    {
      tree tmpl = NULL_TREE;

      if (IS_AGGR_TYPE (context))
	tmpl = lookup_field (context, name, 0, false);

      if (!tmpl || !DECL_CLASS_TEMPLATE_P (tmpl))
	{
	  if (complain & tf_error)
	    error ("no class template named %q#T in %q#T", name, context);
	  return error_mark_node;
	}

      if (parm_list
	  && !comp_template_parms (DECL_TEMPLATE_PARMS (tmpl), parm_list))
	{
	  if (complain & tf_error)
	    {
	      error ("template parameters do not match template");
	      error ("%q+D declared here", tmpl);
	    }
	  return error_mark_node;
	}

      if (complain & tf_error)
	perform_or_defer_access_check (TYPE_BINFO (context), tmpl, tmpl);

      return tmpl;
    }

  /* Build the UNBOUND_CLASS_TEMPLATE.  */
  t = make_aggr_type (UNBOUND_CLASS_TEMPLATE);
  TYPE_CONTEXT (t) = FROB_CONTEXT (context);
  TREE_TYPE (t) = NULL_TREE;

  /* Build the corresponding TEMPLATE_DECL.  */
  d = build_decl (TEMPLATE_DECL, name, t);
  TYPE_NAME (TREE_TYPE (d)) = d;
  TYPE_STUB_DECL (TREE_TYPE (d)) = d;
  DECL_CONTEXT (d) = FROB_CONTEXT (context);
  DECL_ARTIFICIAL (d) = 1;
  DECL_TEMPLATE_PARMS (d) = parm_list;

  return t;
}



/* Push the declarations of builtin types into the namespace.
   RID_INDEX is the index of the builtin type in the array
   RID_POINTERS.  NAME is the name used when looking up the builtin
   type.  TYPE is the _TYPE node for the builtin type.  */

void
record_builtin_type (enum rid rid_index,
		     const char* name,
		     tree type)
{
  tree rname = NULL_TREE, tname = NULL_TREE;
  tree tdecl = NULL_TREE;

  if ((int) rid_index < (int) RID_MAX)
    rname = ridpointers[(int) rid_index];
  if (name)
    tname = get_identifier (name);

  /* The calls to SET_IDENTIFIER_GLOBAL_VALUE below should be
     eliminated.  Built-in types should not be looked up name; their
     names are keywords that the parser can recognize.  However, there
     is code in c-common.c that uses identifier_global_value to look
     up built-in types by name.  */
  if (tname)
    {
      tdecl = build_decl (TYPE_DECL, tname, type);
      DECL_ARTIFICIAL (tdecl) = 1;
      SET_IDENTIFIER_GLOBAL_VALUE (tname, tdecl);
    }
  if (rname)
    {
      if (!tdecl)
	{
	  tdecl = build_decl (TYPE_DECL, rname, type);
	  DECL_ARTIFICIAL (tdecl) = 1;
	}
      SET_IDENTIFIER_GLOBAL_VALUE (rname, tdecl);
    }

  if (!TYPE_NAME (type))
    TYPE_NAME (type) = tdecl;

  if (tdecl)
    debug_hooks->type_decl (tdecl, 0);
}

/* Record one of the standard Java types.
 * Declare it as having the given NAME.
 * If SIZE > 0, it is the size of one of the integral types;
 * otherwise it is the negative of the size of one of the other types.  */

static tree
record_builtin_java_type (const char* name, int size)
{
  tree type, decl;
  if (size > 0)
    type = make_signed_type (size);
  else if (size > -32)
    { /* "__java_char" or ""__java_boolean".  */
      type = make_unsigned_type (-size);
      /*if (size == -1)	TREE_SET_CODE (type, BOOLEAN_TYPE);*/
    }
  else
    { /* "__java_float" or ""__java_double".  */
      type = make_node (REAL_TYPE);
      TYPE_PRECISION (type) = - size;
      layout_type (type);
    }
  record_builtin_type (RID_MAX, name, type);
  decl = TYPE_NAME (type);

  /* Suppress generate debug symbol entries for these types,
     since for normal C++ they are just clutter.
     However, push_lang_context undoes this if extern "Java" is seen.  */
  DECL_IGNORED_P (decl) = 1;

  TYPE_FOR_JAVA (type) = 1;
  return type;
}

/* Push a type into the namespace so that the back-ends ignore it.  */

static void
record_unknown_type (tree type, const char* name)
{
  tree decl = pushdecl (build_decl (TYPE_DECL, get_identifier (name), type));
  /* Make sure the "unknown type" typedecl gets ignored for debug info.  */
  DECL_IGNORED_P (decl) = 1;
  TYPE_DECL_SUPPRESS_DEBUG (decl) = 1;
  TYPE_SIZE (type) = TYPE_SIZE (void_type_node);
  TYPE_ALIGN (type) = 1;
  TYPE_USER_ALIGN (type) = 0;
  TYPE_MODE (type) = TYPE_MODE (void_type_node);
}

/* A string for which we should create an IDENTIFIER_NODE at
   startup.  */

typedef struct predefined_identifier
{
  /* The name of the identifier.  */
  const char *const name;
  /* The place where the IDENTIFIER_NODE should be stored.  */
  tree *const node;
  /* Nonzero if this is the name of a constructor or destructor.  */
  const int ctor_or_dtor_p;
} predefined_identifier;

/* Create all the predefined identifiers.  */

static void
initialize_predefined_identifiers (void)
{
  const predefined_identifier *pid;

  /* A table of identifiers to create at startup.  */
  static const predefined_identifier predefined_identifiers[] = {
    { "C++", &lang_name_cplusplus, 0 },
    { "C", &lang_name_c, 0 },
    { "Java", &lang_name_java, 0 },
    /* Some of these names have a trailing space so that it is
       impossible for them to conflict with names written by users.  */
    { "__ct ", &ctor_identifier, 1 },
    { "__base_ctor ", &base_ctor_identifier, 1 },
    { "__comp_ctor ", &complete_ctor_identifier, 1 },
    { "__dt ", &dtor_identifier, 1 },
    { "__comp_dtor ", &complete_dtor_identifier, 1 },
    { "__base_dtor ", &base_dtor_identifier, 1 },
    { "__deleting_dtor ", &deleting_dtor_identifier, 1 },
    { IN_CHARGE_NAME, &in_charge_identifier, 0 },
    { "nelts", &nelts_identifier, 0 },
    { THIS_NAME, &this_identifier, 0 },
    { VTABLE_DELTA_NAME, &delta_identifier, 0 },
    { VTABLE_PFN_NAME, &pfn_identifier, 0 },
    { "_vptr", &vptr_identifier, 0 },
    { "__vtt_parm", &vtt_parm_identifier, 0 },
    { "::", &global_scope_name, 0 },
    { "std", &std_identifier, 0 },
    { NULL, NULL, 0 }
  };

  for (pid = predefined_identifiers; pid->name; ++pid)
    {
      *pid->node = get_identifier (pid->name);
      if (pid->ctor_or_dtor_p)
	IDENTIFIER_CTOR_OR_DTOR_P (*pid->node) = 1;
    }
}

/* Create the predefined scalar types of C,
   and some nodes representing standard constants (0, 1, (void *)0).
   Initialize the global binding level.
   Make definitions for built-in primitive functions.  */

void
cxx_init_decl_processing (void)
{
  tree void_ftype;
  tree void_ftype_ptr;

  build_common_tree_nodes (flag_signed_char, false);

  /* Create all the identifiers we need.  */
  initialize_predefined_identifiers ();

  /* Create the global variables.  */
  push_to_top_level ();

  current_function_decl = NULL_TREE;
  current_binding_level = NULL;
  /* Enter the global namespace.  */
  gcc_assert (global_namespace == NULL_TREE);
  global_namespace = build_lang_decl (NAMESPACE_DECL, global_scope_name,
				      void_type_node);
  TREE_PUBLIC (global_namespace) = 1;
  begin_scope (sk_namespace, global_namespace);

  current_lang_name = NULL_TREE;

  /* Adjust various flags based on command-line settings.  */
  if (!flag_permissive)
    flag_pedantic_errors = 1;
  if (!flag_no_inline)
    {
      flag_inline_trees = 1;
      flag_no_inline = 1;
    }
  if (flag_inline_functions)
    flag_inline_trees = 2;

  /* Force minimum function alignment if using the least significant
     bit of function pointers to store the virtual bit.  */
  if (TARGET_PTRMEMFUNC_VBIT_LOCATION == ptrmemfunc_vbit_in_pfn
      && force_align_functions_log < 1)
    force_align_functions_log = 1;

  /* Initially, C.  */
  current_lang_name = lang_name_c;

  /* Create the `std' namespace.  */
  push_namespace (std_identifier);
  std_node = current_namespace;
  pop_namespace ();

  c_common_nodes_and_builtins ();

  java_byte_type_node = record_builtin_java_type ("__java_byte", 8);
  java_short_type_node = record_builtin_java_type ("__java_short", 16);
  java_int_type_node = record_builtin_java_type ("__java_int", 32);
  java_long_type_node = record_builtin_java_type ("__java_long", 64);
  java_float_type_node = record_builtin_java_type ("__java_float", -32);
  java_double_type_node = record_builtin_java_type ("__java_double", -64);
  java_char_type_node = record_builtin_java_type ("__java_char", -16);
  java_boolean_type_node = record_builtin_java_type ("__java_boolean", -1);

  integer_two_node = build_int_cst (NULL_TREE, 2);
  integer_three_node = build_int_cst (NULL_TREE, 3);

  record_builtin_type (RID_BOOL, "bool", boolean_type_node);
  truthvalue_type_node = boolean_type_node;
  truthvalue_false_node = boolean_false_node;
  truthvalue_true_node = boolean_true_node;

  empty_except_spec = build_tree_list (NULL_TREE, NULL_TREE);

#if 0
  record_builtin_type (RID_MAX, NULL, string_type_node);
#endif

  delta_type_node = ptrdiff_type_node;
  vtable_index_type = ptrdiff_type_node;

  vtt_parm_type = build_pointer_type (const_ptr_type_node);
  void_ftype = build_function_type (void_type_node, void_list_node);
  void_ftype_ptr = build_function_type (void_type_node,
					tree_cons (NULL_TREE,
						   ptr_type_node,
						   void_list_node));
  void_ftype_ptr
    = build_exception_variant (void_ftype_ptr, empty_except_spec);

  /* C++ extensions */

  unknown_type_node = make_node (UNKNOWN_TYPE);
  record_unknown_type (unknown_type_node, "unknown type");

  /* Indirecting an UNKNOWN_TYPE node yields an UNKNOWN_TYPE node.  */
  TREE_TYPE (unknown_type_node) = unknown_type_node;

  /* Looking up TYPE_POINTER_TO and TYPE_REFERENCE_TO yield the same
     result.  */
  TYPE_POINTER_TO (unknown_type_node) = unknown_type_node;
  TYPE_REFERENCE_TO (unknown_type_node) = unknown_type_node;

  {
    /* Make sure we get a unique function type, so we can give
       its pointer type a name.  (This wins for gdb.) */
    tree vfunc_type = make_node (FUNCTION_TYPE);
    TREE_TYPE (vfunc_type) = integer_type_node;
    TYPE_ARG_TYPES (vfunc_type) = NULL_TREE;
    layout_type (vfunc_type);

    vtable_entry_type = build_pointer_type (vfunc_type);
  }
  record_builtin_type (RID_MAX, VTBL_PTR_TYPE, vtable_entry_type);

  vtbl_type_node
    = build_cplus_array_type (vtable_entry_type, NULL_TREE);
  layout_type (vtbl_type_node);
  vtbl_type_node = build_qualified_type (vtbl_type_node, TYPE_QUAL_CONST);
  record_builtin_type (RID_MAX, NULL, vtbl_type_node);
  vtbl_ptr_type_node = build_pointer_type (vtable_entry_type);
  layout_type (vtbl_ptr_type_node);
  record_builtin_type (RID_MAX, NULL, vtbl_ptr_type_node);

  push_namespace (get_identifier ("__cxxabiv1"));
  abi_node = current_namespace;
  pop_namespace ();

  global_type_node = make_node (LANG_TYPE);
  record_unknown_type (global_type_node, "global type");

  /* Now, C++.  */
  current_lang_name = lang_name_cplusplus;

  {
    tree bad_alloc_id;
    tree bad_alloc_type_node;
    tree bad_alloc_decl;
    tree newtype, deltype;
    tree ptr_ftype_sizetype;

    push_namespace (std_identifier);
    bad_alloc_id = get_identifier ("bad_alloc");
    bad_alloc_type_node = make_aggr_type (RECORD_TYPE);
    TYPE_CONTEXT (bad_alloc_type_node) = current_namespace;
    bad_alloc_decl
      = create_implicit_typedef (bad_alloc_id, bad_alloc_type_node);
    DECL_CONTEXT (bad_alloc_decl) = current_namespace;
    TYPE_STUB_DECL (bad_alloc_type_node) = bad_alloc_decl;
    pop_namespace ();

    ptr_ftype_sizetype
      = build_function_type (ptr_type_node,
			     tree_cons (NULL_TREE,
					size_type_node,
					void_list_node));
    newtype = build_exception_variant
      (ptr_ftype_sizetype, add_exception_specifier
       (NULL_TREE, bad_alloc_type_node, -1));
    deltype = build_exception_variant (void_ftype_ptr, empty_except_spec);
    push_cp_library_fn (NEW_EXPR, newtype);
    push_cp_library_fn (VEC_NEW_EXPR, newtype);
    global_delete_fndecl = push_cp_library_fn (DELETE_EXPR, deltype);
    push_cp_library_fn (VEC_DELETE_EXPR, deltype);
  }

  abort_fndecl
    = build_library_fn_ptr ("__cxa_pure_virtual", void_ftype);

  /* Perform other language dependent initializations.  */
  init_class_processing ();
  init_rtti_processing ();

  if (flag_exceptions)
    init_exception_processing ();

  if (! supports_one_only ())
    flag_weak = 0;

  make_fname_decl = cp_make_fname_decl;
  start_fname_decls ();

  /* Show we use EH for cleanups.  */
  if (flag_exceptions)
    using_eh_for_cleanups ();
}

/* Generate an initializer for a function naming variable from
   NAME. NAME may be NULL, to indicate a dependent name.  TYPE_P is
   filled in with the type of the init.  */

tree
cp_fname_init (const char* name, tree *type_p)
{
  tree domain = NULL_TREE;
  tree type;
  tree init = NULL_TREE;
  size_t length = 0;

  if (name)
    {
      length = strlen (name);
      domain = build_index_type (size_int (length));
      init = build_string (length + 1, name);
    }

  type = build_qualified_type (char_type_node, TYPE_QUAL_CONST);
  type = build_cplus_array_type (type, domain);

  *type_p = type;

  if (init)
    TREE_TYPE (init) = type;
  else
    init = error_mark_node;

  return init;
}

/* Create the VAR_DECL for __FUNCTION__ etc. ID is the name to give the
   decl, NAME is the initialization string and TYPE_DEP indicates whether
   NAME depended on the type of the function. We make use of that to detect
   __PRETTY_FUNCTION__ inside a template fn. This is being done
   lazily at the point of first use, so we mustn't push the decl now.  */

static tree
cp_make_fname_decl (tree id, int type_dep)
{
  const char *const name = (type_dep && processing_template_decl
			    ? NULL : fname_as_string (type_dep));
  tree type;
  tree init = cp_fname_init (name, &type);
  tree decl = build_decl (VAR_DECL, id, type);

  if (name)
    free ((char *) name);

  /* As we're using pushdecl_with_scope, we must set the context.  */
  DECL_CONTEXT (decl) = current_function_decl;
  DECL_PRETTY_FUNCTION_P (decl) = type_dep;

  TREE_STATIC (decl) = 1;
  TREE_READONLY (decl) = 1;
  DECL_ARTIFICIAL (decl) = 1;

  TREE_USED (decl) = 1;

  if (current_function_decl)
    {
      struct cp_binding_level *b = current_binding_level;
      while (b->level_chain->kind != sk_function_parms)
	b = b->level_chain;
      pushdecl_with_scope (decl, b, /*is_friend=*/false);
      cp_finish_decl (decl, init, /*init_const_expr_p=*/false, NULL_TREE,
		      LOOKUP_ONLYCONVERTING);
    }
  else
    pushdecl_top_level_and_finish (decl, init);

  return decl;
}

/* Make a definition for a builtin function named NAME in the current
   namespace, whose data type is TYPE and whose context is CONTEXT.
   TYPE should be a function type with argument types.

   CLASS and CODE tell later passes how to compile calls to this function.
   See tree.h for possible values.

   If LIBNAME is nonzero, use that for DECL_ASSEMBLER_NAME,
   the name to be called if we can't opencode the function.
   If ATTRS is nonzero, use that for the function's attribute
   list.  */

static tree
builtin_function_1 (const char* name,
		    tree type,
		    tree context,
		    enum built_in_function code,
		    enum built_in_class class,
		    const char* libname,
		    tree attrs)
{
  tree decl = build_library_fn_1 (get_identifier (name), ERROR_MARK, type);
  DECL_BUILT_IN_CLASS (decl) = class;
  DECL_FUNCTION_CODE (decl) = code;
  DECL_CONTEXT (decl) = context;

  pushdecl (decl);

  /* Since `pushdecl' relies on DECL_ASSEMBLER_NAME instead of DECL_NAME,
     we cannot change DECL_ASSEMBLER_NAME until we have installed this
     function in the namespace.  */
  if (libname)
    SET_DECL_ASSEMBLER_NAME (decl, get_identifier (libname));

  /* A function in the user's namespace should have an explicit
     declaration before it is used.  Mark the built-in function as
     anticipated but not actually declared.  */
  if (name[0] != '_' || name[1] != '_')
    DECL_ANTICIPATED (decl) = 1;

  /* Possibly apply some default attributes to this built-in function.  */
  if (attrs)
    decl_attributes (&decl, attrs, ATTR_FLAG_BUILT_IN);
  else
    decl_attributes (&decl, NULL_TREE, 0);

  return decl;
}

/* Entry point for the benefit of c_common_nodes_and_builtins.

   Make a definition for a builtin function named NAME and whose data type
   is TYPE.  TYPE should be a function type with argument types.  This
   function places the anticipated declaration in the global namespace
   and additionally in the std namespace if appropriate.

   CLASS and CODE tell later passes how to compile calls to this function.
   See tree.h for possible values.

   If LIBNAME is nonzero, use that for DECL_ASSEMBLER_NAME,
   the name to be called if we can't opencode the function.

   If ATTRS is nonzero, use that for the function's attribute
   list.  */

tree
builtin_function (const char* name,
		  tree type,
		  int code,
		  enum built_in_class cl,
		  const char* libname,
		  tree attrs)
{
  /* All builtins that don't begin with an '_' should additionally
     go in the 'std' namespace.  */
  if (name[0] != '_')
    {
      push_namespace (std_identifier);
      builtin_function_1 (name, type, std_node, code, cl, libname, attrs);
      pop_namespace ();
    }

  return builtin_function_1 (name, type, NULL_TREE, code,
			     cl, libname, attrs);
}

/* Generate a FUNCTION_DECL with the typical flags for a runtime library
   function.  Not called directly.  */

static tree
build_library_fn_1 (tree name, enum tree_code operator_code, tree type)
{
  tree fn = build_lang_decl (FUNCTION_DECL, name, type);
  DECL_EXTERNAL (fn) = 1;
  TREE_PUBLIC (fn) = 1;
  DECL_ARTIFICIAL (fn) = 1;
  SET_OVERLOADED_OPERATOR_CODE (fn, operator_code);
  SET_DECL_LANGUAGE (fn, lang_c);
  /* Runtime library routines are, by definition, available in an
     external shared object.  */
  DECL_VISIBILITY (fn) = VISIBILITY_DEFAULT;
  DECL_VISIBILITY_SPECIFIED (fn) = 1;
  return fn;
}

/* Returns the _DECL for a library function with C linkage.
   We assume that such functions never throw; if this is incorrect,
   callers should unset TREE_NOTHROW.  */

tree
build_library_fn (tree name, tree type)
{
  tree fn = build_library_fn_1 (name, ERROR_MARK, type);
  TREE_NOTHROW (fn) = 1;
  return fn;
}

/* Returns the _DECL for a library function with C++ linkage.  */

static tree
build_cp_library_fn (tree name, enum tree_code operator_code, tree type)
{
  tree fn = build_library_fn_1 (name, operator_code, type);
  TREE_NOTHROW (fn) = TYPE_NOTHROW_P (type);
  DECL_CONTEXT (fn) = FROB_CONTEXT (current_namespace);
  SET_DECL_LANGUAGE (fn, lang_cplusplus);
  return fn;
}

/* Like build_library_fn, but takes a C string instead of an
   IDENTIFIER_NODE.  */

tree
build_library_fn_ptr (const char* name, tree type)
{
  return build_library_fn (get_identifier (name), type);
}

/* Like build_cp_library_fn, but takes a C string instead of an
   IDENTIFIER_NODE.  */

tree
build_cp_library_fn_ptr (const char* name, tree type)
{
  return build_cp_library_fn (get_identifier (name), ERROR_MARK, type);
}

/* Like build_library_fn, but also pushes the function so that we will
   be able to find it via IDENTIFIER_GLOBAL_VALUE.  */

tree
push_library_fn (tree name, tree type)
{
  tree fn = build_library_fn (name, type);
  pushdecl_top_level (fn);
  return fn;
}

/* Like build_cp_library_fn, but also pushes the function so that it
   will be found by normal lookup.  */

static tree
push_cp_library_fn (enum tree_code operator_code, tree type)
{
  tree fn = build_cp_library_fn (ansi_opname (operator_code),
				 operator_code,
				 type);
  pushdecl (fn);
  return fn;
}

/* Like push_library_fn, but takes a TREE_LIST of parm types rather than
   a FUNCTION_TYPE.  */

tree
push_void_library_fn (tree name, tree parmtypes)
{
  tree type = build_function_type (void_type_node, parmtypes);
  return push_library_fn (name, type);
}

/* Like push_library_fn, but also note that this function throws
   and does not return.  Used for __throw_foo and the like.  */

tree
push_throw_library_fn (tree name, tree type)
{
  tree fn = push_library_fn (name, type);
  TREE_THIS_VOLATILE (fn) = 1;
  TREE_NOTHROW (fn) = 0;
  return fn;
}

/* When we call finish_struct for an anonymous union, we create
   default copy constructors and such.  But, an anonymous union
   shouldn't have such things; this function undoes the damage to the
   anonymous union type T.

   (The reason that we create the synthesized methods is that we don't
   distinguish `union { int i; }' from `typedef union { int i; } U'.
   The first is an anonymous union; the second is just an ordinary
   union type.)  */

void
fixup_anonymous_aggr (tree t)
{
  tree *q;

  /* Wipe out memory of synthesized methods.  */
  TYPE_HAS_CONSTRUCTOR (t) = 0;
  TYPE_HAS_DEFAULT_CONSTRUCTOR (t) = 0;
  TYPE_HAS_INIT_REF (t) = 0;
  TYPE_HAS_CONST_INIT_REF (t) = 0;
  TYPE_HAS_ASSIGN_REF (t) = 0;
  TYPE_HAS_CONST_ASSIGN_REF (t) = 0;

  /* Splice the implicitly generated functions out of the TYPE_METHODS
     list.  */
  q = &TYPE_METHODS (t);
  while (*q)
    {
      if (DECL_ARTIFICIAL (*q))
	*q = TREE_CHAIN (*q);
      else
	q = &TREE_CHAIN (*q);
    }

  /* ISO C++ 9.5.3.  Anonymous unions may not have function members.  */
  if (TYPE_METHODS (t))
    error ("%Jan anonymous union cannot have function members",
	   TYPE_MAIN_DECL (t));

  /* Anonymous aggregates cannot have fields with ctors, dtors or complex
     assignment operators (because they cannot have these methods themselves).
     For anonymous unions this is already checked because they are not allowed
     in any union, otherwise we have to check it.  */
  if (TREE_CODE (t) != UNION_TYPE)
    {
      tree field, type;

      for (field = TYPE_FIELDS (t); field; field = TREE_CHAIN (field))
	if (TREE_CODE (field) == FIELD_DECL)
	  {
	    type = TREE_TYPE (field);
	    if (CLASS_TYPE_P (type))
	      {
		if (TYPE_NEEDS_CONSTRUCTING (type))
		  error ("member %q+#D with constructor not allowed "
			 "in anonymous aggregate", field);
		if (TYPE_HAS_NONTRIVIAL_DESTRUCTOR (type))
		  error ("member %q+#D with destructor not allowed "
			 "in anonymous aggregate", field);
		if (TYPE_HAS_COMPLEX_ASSIGN_REF (type))
		  error ("member %q+#D with copy assignment operator "
			 "not allowed in anonymous aggregate", field);
	      }
	  }
    }
}

/* Make sure that a declaration with no declarator is well-formed, i.e.
   just declares a tagged type or anonymous union.

   Returns the type declared; or NULL_TREE if none.  */

tree
check_tag_decl (cp_decl_specifier_seq *declspecs)
{
  int saw_friend = declspecs->specs[(int)ds_friend] != 0;
  int saw_typedef = declspecs->specs[(int)ds_typedef] != 0;
  /* If a class, struct, or enum type is declared by the DECLSPECS
     (i.e, if a class-specifier, enum-specifier, or non-typename
     elaborated-type-specifier appears in the DECLSPECS),
     DECLARED_TYPE is set to the corresponding type.  */
  tree declared_type = NULL_TREE;
  bool error_p = false;

  if (declspecs->multiple_types_p)
    error ("multiple types in one declaration");
  else if (declspecs->redefined_builtin_type)
    {
      if (!in_system_header)
	pedwarn ("redeclaration of C++ built-in type %qT",
		 declspecs->redefined_builtin_type);
      return NULL_TREE;
    }

  if (declspecs->type
      && TYPE_P (declspecs->type)
      && ((TREE_CODE (declspecs->type) != TYPENAME_TYPE
	   && IS_AGGR_TYPE (declspecs->type))
	  || TREE_CODE (declspecs->type) == ENUMERAL_TYPE))
    declared_type = declspecs->type;
  else if (declspecs->type == error_mark_node)
    error_p = true;
  if (declared_type == NULL_TREE && ! saw_friend && !error_p)
    pedwarn ("declaration does not declare anything");
  /* Check for an anonymous union.  */
  else if (declared_type && IS_AGGR_TYPE_CODE (TREE_CODE (declared_type))
	   && TYPE_ANONYMOUS_P (declared_type))
    {
      /* 7/3 In a simple-declaration, the optional init-declarator-list
	 can be omitted only when declaring a class (clause 9) or
	 enumeration (7.2), that is, when the decl-specifier-seq contains
	 either a class-specifier, an elaborated-type-specifier with
	 a class-key (9.1), or an enum-specifier.  In these cases and
	 whenever a class-specifier or enum-specifier is present in the
	 decl-specifier-seq, the identifiers in these specifiers are among
	 the names being declared by the declaration (as class-name,
	 enum-names, or enumerators, depending on the syntax).  In such
	 cases, and except for the declaration of an unnamed bit-field (9.6),
	 the decl-specifier-seq shall introduce one or more names into the
	 program, or shall redeclare a name introduced by a previous
	 declaration.  [Example:
	     enum { };			// ill-formed
	     typedef class { };		// ill-formed
	 --end example]  */
      if (saw_typedef)
	{
	  error ("missing type-name in typedef-declaration");
	  return NULL_TREE;
	}
      /* Anonymous unions are objects, so they can have specifiers.  */;
      SET_ANON_AGGR_TYPE_P (declared_type);

      if (TREE_CODE (declared_type) != UNION_TYPE && pedantic
	  && !in_system_header)
	pedwarn ("ISO C++ prohibits anonymous structs");
    }

  else
    {
      if (declspecs->specs[(int)ds_inline]
	  || declspecs->specs[(int)ds_virtual])
	error ("%qs can only be specified for functions",
	       declspecs->specs[(int)ds_inline]
	       ? "inline" : "virtual");
      else if (saw_friend
	       && (!current_class_type
		   || current_scope () != current_class_type))
	error ("%<friend%> can only be specified inside a class");
      else if (declspecs->specs[(int)ds_explicit])
	error ("%<explicit%> can only be specified for constructors");
      else if (declspecs->storage_class)
	error ("a storage class can only be specified for objects "
	       "and functions");
      else if (declspecs->specs[(int)ds_const]
	       || declspecs->specs[(int)ds_volatile]
	       || declspecs->specs[(int)ds_restrict]
	       || declspecs->specs[(int)ds_thread])
	error ("qualifiers can only be specified for objects "
	       "and functions");
    }

  return declared_type;
}

/* Called when a declaration is seen that contains no names to declare.
   If its type is a reference to a structure, union or enum inherited
   from a containing scope, shadow that tag name for the current scope
   with a forward reference.
   If its type defines a new named structure or union
   or defines an enum, it is valid but we need not do anything here.
   Otherwise, it is an error.

   C++: may have to grok the declspecs to learn about static,
   complain for anonymous unions.

   Returns the TYPE declared -- or NULL_TREE if none.  */

tree
shadow_tag (cp_decl_specifier_seq *declspecs)
{
  tree t = check_tag_decl (declspecs);

  if (!t)
    return NULL_TREE;

  if (declspecs->attributes)
    {
      warning (0, "attribute ignored in declaration of %q+#T", t);
      warning (0, "attribute for %q+#T must follow the %qs keyword",
	       t, class_key_or_enum_as_string (t));

    }

  if (maybe_process_partial_specialization (t) == error_mark_node)
    return NULL_TREE;

  /* This is where the variables in an anonymous union are
     declared.  An anonymous union declaration looks like:
     union { ... } ;
     because there is no declarator after the union, the parser
     sends that declaration here.  */
  if (ANON_AGGR_TYPE_P (t))
    {
      fixup_anonymous_aggr (t);

      if (TYPE_FIELDS (t))
	{
	  tree decl = grokdeclarator (/*declarator=*/NULL,
				      declspecs, NORMAL, 0, NULL);
	  finish_anon_union (decl);
	}
    }

  return t;
}

/* Decode a "typename", such as "int **", returning a ..._TYPE node.  */

tree
groktypename (cp_decl_specifier_seq *type_specifiers,
	      const cp_declarator *declarator)
{
  tree attrs;
  tree type;
  attrs = type_specifiers->attributes;
  type_specifiers->attributes = NULL_TREE;
  type = grokdeclarator (declarator, type_specifiers, TYPENAME, 0, &attrs);
  if (attrs)
    cplus_decl_attributes (&type, attrs, 0);
  return type;
}

/* Decode a declarator in an ordinary declaration or data definition.
   This is called as soon as the type information and variable name
   have been parsed, before parsing the initializer if any.
   Here we create the ..._DECL node, fill in its type,
   and put it on the list of decls for the current context.
   The ..._DECL node is returned as the value.

   Exception: for arrays where the length is not specified,
   the type is left null, to be filled in by `cp_finish_decl'.

   Function definitions do not come here; they go to start_function
   instead.  However, external and forward declarations of functions
   do go through here.  Structure field declarations are done by
   grokfield and not through here.  */

tree
start_decl (const cp_declarator *declarator,
	    cp_decl_specifier_seq *declspecs,
	    int initialized,
	    tree attributes,
	    tree prefix_attributes,
	    tree *pushed_scope_p)
{
  tree decl;
  tree type, tem;
  tree context;
  bool was_public;

  *pushed_scope_p = NULL_TREE;

  /* An object declared as __attribute__((deprecated)) suppresses
     warnings of uses of other deprecated items.  */
  if (lookup_attribute ("deprecated", attributes))
    deprecated_state = DEPRECATED_SUPPRESS;

  attributes = chainon (attributes, prefix_attributes);

  decl = grokdeclarator (declarator, declspecs, NORMAL, initialized,
			 &attributes);

  deprecated_state = DEPRECATED_NORMAL;

  if (decl == NULL_TREE || TREE_CODE (decl) == VOID_TYPE
      || decl == error_mark_node)
    return error_mark_node;

  type = TREE_TYPE (decl);

  context = DECL_CONTEXT (decl);

  if (context)
    {
      *pushed_scope_p = push_scope (context);

      /* We are only interested in class contexts, later.  */
      if (TREE_CODE (context) == NAMESPACE_DECL)
	context = NULL_TREE;
    }

  if (initialized)
    /* Is it valid for this decl to have an initializer at all?
       If not, set INITIALIZED to zero, which will indirectly
       tell `cp_finish_decl' to ignore the initializer once it is parsed.  */
    switch (TREE_CODE (decl))
      {
      case TYPE_DECL:
	error ("typedef %qD is initialized (use __typeof__ instead)", decl);
	return error_mark_node;

      case FUNCTION_DECL:
	error ("function %q#D is initialized like a variable", decl);
	return error_mark_node;

      default:
	break;
      }

  if (initialized)
    {
      if (! toplevel_bindings_p ()
	  && DECL_EXTERNAL (decl))
	warning (0, "declaration of %q#D has %<extern%> and is initialized",
		 decl);
      DECL_EXTERNAL (decl) = 0;
      if (toplevel_bindings_p ())
	TREE_STATIC (decl) = 1;
    }

  /* Set attributes here so if duplicate decl, will have proper attributes.  */
  cplus_decl_attributes (&decl, attributes, 0);

  /* Dllimported symbols cannot be defined.  Static data members (which
     can be initialized in-class and dllimported) go through grokfield,
     not here, so we don't need to exclude those decls when checking for
     a definition.  */
  if (initialized && DECL_DLLIMPORT_P (decl))
    {
      error ("definition of %q#D is marked %<dllimport%>", decl);
      DECL_DLLIMPORT_P (decl) = 0;
    }

  /* If #pragma weak was used, mark the decl weak now.  */
  maybe_apply_pragma_weak (decl);

  if (TREE_CODE (decl) == FUNCTION_DECL
      && DECL_DECLARED_INLINE_P (decl)
      && DECL_UNINLINABLE (decl)
      && lookup_attribute ("noinline", DECL_ATTRIBUTES (decl)))
    warning (0, "inline function %q+D given attribute noinline", decl);

  if (context && COMPLETE_TYPE_P (complete_type (context)))
    {
      if (TREE_CODE (decl) == VAR_DECL)
	{
	  tree field = lookup_field (context, DECL_NAME (decl), 0, false);
	  if (field == NULL_TREE || TREE_CODE (field) != VAR_DECL)
	    error ("%q#D is not a static member of %q#T", decl, context);
	  else
	    {
	      if (DECL_CONTEXT (field) != context)
		{
		  if (!same_type_p (DECL_CONTEXT (field), context))
		    pedwarn ("ISO C++ does not permit %<%T::%D%> "
			     "to be defined as %<%T::%D%>",
			     DECL_CONTEXT (field), DECL_NAME (decl),
			     context, DECL_NAME (decl));
		  DECL_CONTEXT (decl) = DECL_CONTEXT (field);
		}
	      if (processing_specialization
		  && template_class_depth (context) == 0
		  && CLASSTYPE_TEMPLATE_SPECIALIZATION (context))
		error ("template header not allowed in member definition "
		       "of explicitly specialized class");
	      /* Static data member are tricky; an in-class initialization
		 still doesn't provide a definition, so the in-class
		 declaration will have DECL_EXTERNAL set, but will have an
		 initialization.  Thus, duplicate_decls won't warn
		 about this situation, and so we check here.  */
	      if (initialized && DECL_INITIALIZED_IN_CLASS_P (field))
		error ("duplicate initialization of %qD", decl);
	      if (duplicate_decls (decl, field, /*newdecl_is_friend=*/false))
		decl = field;
	    }
	}
      else
	{
	  tree field = check_classfn (context, decl,
				      (processing_template_decl
				       > template_class_depth (context))
				      ? current_template_parms
				      : NULL_TREE);
	  if (field && duplicate_decls (decl, field,
					/*newdecl_is_friend=*/false))
	    decl = field;
	}

      /* cp_finish_decl sets DECL_EXTERNAL if DECL_IN_AGGR_P is set.  */
      DECL_IN_AGGR_P (decl) = 0;
      /* Do not mark DECL as an explicit specialization if it was not
	 already marked as an instantiation; a declaration should
	 never be marked as a specialization unless we know what
	 template is being specialized.  */
      if (DECL_LANG_SPECIFIC (decl) && DECL_USE_TEMPLATE (decl))
	{
	  SET_DECL_TEMPLATE_SPECIALIZATION (decl);

	  /* [temp.expl.spec] An explicit specialization of a static data
	     member of a template is a definition if the declaration
	     includes an initializer; otherwise, it is a declaration.

	     We check for processing_specialization so this only applies
	     to the new specialization syntax.  */
	  if (!initialized && processing_specialization)
	    DECL_EXTERNAL (decl) = 1;
	}

      if (DECL_EXTERNAL (decl) && ! DECL_TEMPLATE_SPECIALIZATION (decl))
	pedwarn ("declaration of %q#D outside of class is not definition",
		 decl);
    }

  was_public = TREE_PUBLIC (decl);

  /* Enter this declaration into the symbol table.  */
  tem = maybe_push_decl (decl);

  if (processing_template_decl)
    tem = push_template_decl (tem);
  if (tem == error_mark_node)
    return error_mark_node;

  /* Tell the back-end to use or not use .common as appropriate.  If we say
     -fconserve-space, we want this to save .data space, at the expense of
     wrong semantics.  If we say -fno-conserve-space, we want this to
     produce errors about redefs; to do this we force variables into the
     data segment.  */
  if (flag_conserve_space
      && TREE_CODE (tem) == VAR_DECL
      && TREE_PUBLIC (tem)
      && !DECL_THREAD_LOCAL_P (tem)
      && !have_global_bss_p ())
    DECL_COMMON (tem) = 1;

  if (TREE_CODE (tem) == VAR_DECL
      && DECL_NAMESPACE_SCOPE_P (tem) && !TREE_PUBLIC (tem) && !was_public
      && !DECL_THIS_STATIC (tem) && !DECL_ARTIFICIAL (tem))
    {
      /* This is a const variable with implicit 'static'.  Set
	 DECL_THIS_STATIC so we can tell it from variables that are
	 !TREE_PUBLIC because of the anonymous namespace.  */
      gcc_assert (cp_type_readonly (TREE_TYPE (tem)));
      DECL_THIS_STATIC (tem) = 1;
    }

  if (!processing_template_decl && TREE_CODE (tem) == VAR_DECL)
    start_decl_1 (tem, initialized);

  return tem;
}

void
start_decl_1 (tree decl, bool initialized)
{
  tree type;

  gcc_assert (!processing_template_decl);

  if (error_operand_p (decl))
    return;

  gcc_assert (TREE_CODE (decl) == VAR_DECL);
  type = TREE_TYPE (decl);

  if (initialized)
    /* Is it valid for this decl to have an initializer at all?
       If not, set INITIALIZED to zero, which will indirectly
       tell `cp_finish_decl' to ignore the initializer once it is parsed.  */
    {
      /* Don't allow initializations for incomplete types except for
	 arrays which might be completed by the initialization.  */
      if (COMPLETE_TYPE_P (complete_type (type)))
	;			/* A complete type is ok.  */
      else if (TREE_CODE (type) != ARRAY_TYPE)
	{
	  error ("variable %q#D has initializer but incomplete type", decl);
	  initialized = 0;
	  type = TREE_TYPE (decl) = error_mark_node;
	}
      else if (!COMPLETE_TYPE_P (complete_type (TREE_TYPE (type))))
	{
	  if (DECL_LANG_SPECIFIC (decl) && DECL_TEMPLATE_INFO (decl))
	    error ("elements of array %q#D have incomplete type", decl);
	  /* else we already gave an error in start_decl.  */
	  initialized = 0;
	}
    }
  else if (IS_AGGR_TYPE (type)
	   && ! DECL_EXTERNAL (decl))
    {
      if (!COMPLETE_TYPE_P (complete_type (type)))
	{
	  error ("aggregate %q#D has incomplete type and cannot be defined",
		 decl);
	  /* Change the type so that assemble_variable will give
	     DECL an rtl we can live with: (mem (const_int 0)).  */
	  type = TREE_TYPE (decl) = error_mark_node;
	}
      else
	{
	  /* If any base type in the hierarchy of TYPE needs a constructor,
	     then we set initialized to 1.  This way any nodes which are
	     created for the purposes of initializing this aggregate
	     will live as long as it does.  This is necessary for global
	     aggregates which do not have their initializers processed until
	     the end of the file.  */
	  initialized = TYPE_NEEDS_CONSTRUCTING (type);
	}
    }

  /* Create a new scope to hold this declaration if necessary.
     Whether or not a new scope is necessary cannot be determined
     until after the type has been completed; if the type is a
     specialization of a class template it is not until after
     instantiation has occurred that TYPE_HAS_NONTRIVIAL_DESTRUCTOR
     will be set correctly.  */
  maybe_push_cleanup_level (type);
}

/* Handle initialization of references.  DECL, TYPE, and INIT have the
   same meaning as in cp_finish_decl.  *CLEANUP must be NULL on entry,
   but will be set to a new CLEANUP_STMT if a temporary is created
   that must be destroyed subsequently.

   Returns an initializer expression to use to initialize DECL, or
   NULL if the initialization can be performed statically.

   Quotes on semantics can be found in ARM 8.4.3.  */

static tree
grok_reference_init (tree decl, tree type, tree init, tree *cleanup)
{
  tree tmp;

  if (init == NULL_TREE)
    {
      if ((DECL_LANG_SPECIFIC (decl) == 0
	   || DECL_IN_AGGR_P (decl) == 0)
	  && ! DECL_THIS_EXTERN (decl))
	error ("%qD declared as reference but not initialized", decl);
      return NULL_TREE;
    }

  if (TREE_CODE (init) == CONSTRUCTOR)
    {
      error ("ISO C++ forbids use of initializer list to "
	     "initialize reference %qD", decl);
      return NULL_TREE;
    }

  if (TREE_CODE (init) == TREE_LIST)
    init = build_x_compound_expr_from_list (init, "initializer");

  if (TREE_CODE (TREE_TYPE (type)) != ARRAY_TYPE
      && TREE_CODE (TREE_TYPE (init)) == ARRAY_TYPE)
    /* Note: default conversion is only called in very special cases.  */
    init = decay_conversion (init);

  /* Convert INIT to the reference type TYPE.  This may involve the
     creation of a temporary, whose lifetime must be the same as that
     of the reference.  If so, a DECL_EXPR for the temporary will be
     added just after the DECL_EXPR for DECL.  That's why we don't set
     DECL_INITIAL for local references (instead assigning to them
     explicitly); we need to allow the temporary to be initialized
     first.  */
  tmp = initialize_reference (type, init, decl, cleanup);

  if (tmp == error_mark_node)
    return NULL_TREE;
  else if (tmp == NULL_TREE)
    {
      error ("cannot initialize %qT from %qT", type, TREE_TYPE (init));
      return NULL_TREE;
    }

  if (TREE_STATIC (decl) && !TREE_CONSTANT (tmp))
    return tmp;

  DECL_INITIAL (decl) = tmp;

  return NULL_TREE;
}

/* Designated initializers in arrays are not supported in GNU C++.
   The parser cannot detect this error since it does not know whether
   a given brace-enclosed initializer is for a class type or for an
   array.  This function checks that CE does not use a designated
   initializer.  If it does, an error is issued.  Returns true if CE
   is valid, i.e., does not have a designated initializer.  */

static bool
check_array_designated_initializer (const constructor_elt *ce)
{
  /* Designated initializers for array elements arenot supported.  */
  if (ce->index)
    {
      /* The parser only allows identifiers as designated
	 intializers.  */
      gcc_assert (TREE_CODE (ce->index) == IDENTIFIER_NODE);
      error ("name %qD used in a GNU-style designated "
	     "initializer for an array", ce->index);
      return false;
    }

  return true;
}

/* When parsing `int a[] = {1, 2};' we don't know the size of the
   array until we finish parsing the initializer.  If that's the
   situation we're in, update DECL accordingly.  */

static void
maybe_deduce_size_from_array_init (tree decl, tree init)
{
  tree type = TREE_TYPE (decl);

  if (TREE_CODE (type) == ARRAY_TYPE
      && TYPE_DOMAIN (type) == NULL_TREE
      && TREE_CODE (decl) != TYPE_DECL)
    {
      /* do_default is really a C-ism to deal with tentative definitions.
	 But let's leave it here to ease the eventual merge.  */
      int do_default = !DECL_EXTERNAL (decl);
      tree initializer = init ? init : DECL_INITIAL (decl);
      int failure = 0;

      /* Check that there are no designated initializers in INIT, as
	 those are not supported in GNU C++, and as the middle-end
	 will crash if presented with a non-numeric designated
	 initializer.  */
      if (initializer && TREE_CODE (initializer) == CONSTRUCTOR)
	{
	  VEC(constructor_elt,gc) *v = CONSTRUCTOR_ELTS (initializer);
	  constructor_elt *ce;
	  HOST_WIDE_INT i;
	  for (i = 0; 
	       VEC_iterate (constructor_elt, v, i, ce);
	       ++i)
	    if (!check_array_designated_initializer (ce))
	      failure = 1;
	}

      if (!failure)
	{
	  failure = cp_complete_array_type (&TREE_TYPE (decl), initializer,
					    do_default);
	  if (failure == 1)
	    {
	      error ("initializer fails to determine size of %qD", decl);
	      TREE_TYPE (decl) = error_mark_node;
	    }
	  else if (failure == 2)
	    {
	      if (do_default)
		{
		  error ("array size missing in %qD", decl);
		  TREE_TYPE (decl) = error_mark_node;
		}
	      /* If a `static' var's size isn't known, make it extern as
		 well as static, so it does not get allocated.  If it's not
		 `static', then don't mark it extern; finish_incomplete_decl
		 will give it a default size and it will get allocated.  */
	      else if (!pedantic && TREE_STATIC (decl) && !TREE_PUBLIC (decl))
		DECL_EXTERNAL (decl) = 1;
	    }
	  else if (failure == 3)
	    {
	      error ("zero-size array %qD", decl);
	      TREE_TYPE (decl) = error_mark_node;
	    }
	}

      cp_apply_type_quals_to_decl (cp_type_quals (TREE_TYPE (decl)), decl);

      layout_decl (decl, 0);
    }
}

/* Set DECL_SIZE, DECL_ALIGN, etc. for DECL (a VAR_DECL), and issue
   any appropriate error messages regarding the layout.  */

static void
layout_var_decl (tree decl)
{
  tree type;

  type = TREE_TYPE (decl);
  if (type == error_mark_node)
    return;

  /* If we haven't already layed out this declaration, do so now.
     Note that we must not call complete type for an external object
     because it's type might involve templates that we are not
     supposed to instantiate yet.  (And it's perfectly valid to say
     `extern X x' for some incomplete type `X'.)  */
  if (!DECL_EXTERNAL (decl))
    complete_type (type);
  if (!DECL_SIZE (decl)
      && TREE_TYPE (decl) != error_mark_node
      && (COMPLETE_TYPE_P (type)
	  || (TREE_CODE (type) == ARRAY_TYPE
	      && !TYPE_DOMAIN (type)
	      && COMPLETE_TYPE_P (TREE_TYPE (type)))))
    layout_decl (decl, 0);

  if (!DECL_EXTERNAL (decl) && DECL_SIZE (decl) == NULL_TREE)
    {
      /* An automatic variable with an incomplete type: that is an error.
	 Don't talk about array types here, since we took care of that
	 message in grokdeclarator.  */
      error ("storage size of %qD isn't known", decl);
      TREE_TYPE (decl) = error_mark_node;
    }
#if 0
  /* Keep this code around in case we later want to control debug info
     based on whether a type is "used".  (jason 1999-11-11) */

  else if (!DECL_EXTERNAL (decl) && IS_AGGR_TYPE (ttype))
    /* Let debugger know it should output info for this type.  */
    note_debug_info_needed (ttype);

  if (TREE_STATIC (decl) && DECL_CLASS_SCOPE_P (decl))
    note_debug_info_needed (DECL_CONTEXT (decl));
#endif

  if ((DECL_EXTERNAL (decl) || TREE_STATIC (decl))
      && DECL_SIZE (decl) != NULL_TREE
      && ! TREE_CONSTANT (DECL_SIZE (decl)))
    {
      if (TREE_CODE (DECL_SIZE (decl)) == INTEGER_CST)
	constant_expression_warning (DECL_SIZE (decl));
      else
	error ("storage size of %qD isn't constant", decl);
    }
}

/* If a local static variable is declared in an inline function, or if
   we have a weak definition, we must endeavor to create only one
   instance of the variable at link-time.  */

static void
maybe_commonize_var (tree decl)
{
  /* Static data in a function with comdat linkage also has comdat
     linkage.  */
  if (TREE_STATIC (decl)
      /* Don't mess with __FUNCTION__.  */
      && ! DECL_ARTIFICIAL (decl)
      && DECL_FUNCTION_SCOPE_P (decl)
      /* Unfortunately, import_export_decl has not always been called
	 before the function is processed, so we cannot simply check
	 DECL_COMDAT.  */
      && (DECL_COMDAT (DECL_CONTEXT (decl))
	  || ((DECL_DECLARED_INLINE_P (DECL_CONTEXT (decl))
	       || DECL_TEMPLATE_INSTANTIATION (DECL_CONTEXT (decl)))
	      && TREE_PUBLIC (DECL_CONTEXT (decl)))))
    {
      if (flag_weak)
	{
	  /* With weak symbols, we simply make the variable COMDAT;
	     that will cause copies in multiple translations units to
	     be merged.  */
	  comdat_linkage (decl);
	}
      else
	{
	  if (DECL_INITIAL (decl) == NULL_TREE
	      || DECL_INITIAL (decl) == error_mark_node)
	    {
	      /* Without weak symbols, we can use COMMON to merge
		 uninitialized variables.  */
	      TREE_PUBLIC (decl) = 1;
	      DECL_COMMON (decl) = 1;
	    }
	  else
	    {
	      /* While for initialized variables, we must use internal
		 linkage -- which means that multiple copies will not
		 be merged.  */
	      TREE_PUBLIC (decl) = 0;
	      DECL_COMMON (decl) = 0;
	      warning (0, "sorry: semantics of inline function static "
		       "data %q+#D are wrong (you'll wind up "
		       "with multiple copies)", decl);
	      warning (0, "%J  you can work around this by removing "
		       "the initializer",
		       decl);
	    }
	}
    }
  else if (DECL_LANG_SPECIFIC (decl) && DECL_COMDAT (decl))
    /* Set it up again; we might have set DECL_INITIAL since the last
       time.  */
    comdat_linkage (decl);
}

/* Issue an error message if DECL is an uninitialized const variable.  */

static void
check_for_uninitialized_const_var (tree decl)
{
  tree type = TREE_TYPE (decl);

  /* ``Unless explicitly declared extern, a const object does not have
     external linkage and must be initialized. ($8.4; $12.1)'' ARM
     7.1.6 */
  if (TREE_CODE (decl) == VAR_DECL
      && TREE_CODE (type) != REFERENCE_TYPE
      && CP_TYPE_CONST_P (type)
      && !TYPE_NEEDS_CONSTRUCTING (type)
      && !DECL_INITIAL (decl))
    error ("uninitialized const %qD", decl);
}


/* Structure holding the current initializer being processed by reshape_init.
   CUR is a pointer to the current element being processed, END is a pointer
   after the last element present in the initializer.  */
typedef struct reshape_iterator_t
{
  constructor_elt *cur;
  constructor_elt *end;
} reshape_iter;

static tree reshape_init_r (tree, reshape_iter *, bool);

/* FIELD is a FIELD_DECL or NULL.  In the former case, the value
   returned is the next FIELD_DECL (possibly FIELD itself) that can be
   initialized.  If there are no more such fields, the return value
   will be NULL.  */

static tree
next_initializable_field (tree field)
{
  while (field
	 && (TREE_CODE (field) != FIELD_DECL
	     || (DECL_C_BIT_FIELD (field) && !DECL_NAME (field))
	     || DECL_ARTIFICIAL (field)))
    field = TREE_CHAIN (field);

  return field;
}

/* Subroutine of reshape_init_array and reshape_init_vector, which does
   the actual work. ELT_TYPE is the element type of the array. MAX_INDEX is an
   INTEGER_CST representing the size of the array minus one (the maximum index),
   or NULL_TREE if the array was declared without specifying the size. D is
   the iterator within the constructor.  */

static tree
reshape_init_array_1 (tree elt_type, tree max_index, reshape_iter *d)
{
  tree new_init;
  bool sized_array_p = (max_index != NULL_TREE);
  unsigned HOST_WIDE_INT max_index_cst = 0;
  unsigned HOST_WIDE_INT index;

  /* The initializer for an array is always a CONSTRUCTOR.  */
  new_init = build_constructor (NULL_TREE, NULL);

  if (sized_array_p)
    {
      /* Minus 1 is used for zero sized arrays.  */
      if (integer_all_onesp (max_index))
	return new_init;

      if (host_integerp (max_index, 1))
	max_index_cst = tree_low_cst (max_index, 1);
      /* sizetype is sign extended, not zero extended.  */
      else
	max_index_cst = tree_low_cst (fold_convert (size_type_node, max_index),
				      1);
    }

  /* Loop until there are no more initializers.  */
  for (index = 0;
       d->cur != d->end && (!sized_array_p || index <= max_index_cst);
       ++index)
    {
      tree elt_init;

      check_array_designated_initializer (d->cur);
      elt_init = reshape_init_r (elt_type, d, /*first_initializer_p=*/false);
      if (elt_init == error_mark_node)
	return error_mark_node;
      CONSTRUCTOR_APPEND_ELT (CONSTRUCTOR_ELTS (new_init), NULL_TREE, elt_init);
    }

  return new_init;
}

/* Subroutine of reshape_init_r, processes the initializers for arrays.
   Parameters are the same of reshape_init_r.  */

static tree
reshape_init_array (tree type, reshape_iter *d)
{
  tree max_index = NULL_TREE;

  gcc_assert (TREE_CODE (type) == ARRAY_TYPE);

  if (TYPE_DOMAIN (type))
    max_index = array_type_nelts (type);

  return reshape_init_array_1 (TREE_TYPE (type), max_index, d);
}

/* Subroutine of reshape_init_r, processes the initializers for vectors.
   Parameters are the same of reshape_init_r.  */

static tree
reshape_init_vector (tree type, reshape_iter *d)
{
  tree max_index = NULL_TREE;
  tree rtype;

  gcc_assert (TREE_CODE (type) == VECTOR_TYPE);

  if (COMPOUND_LITERAL_P (d->cur->value))
    {
      tree value = d->cur->value;
      if (!same_type_p (TREE_TYPE (value), type))
	{
	  error ("invalid type %qT as initializer for a vector of type %qT",
		TREE_TYPE (d->cur->value), type);
	  value = error_mark_node;
	}
      ++d->cur;
      return value;
    }

  /* For a vector, the representation type is a struct
      containing a single member which is an array of the
      appropriate size.  */
  rtype = TYPE_DEBUG_REPRESENTATION_TYPE (type);
  if (rtype && TYPE_DOMAIN (TREE_TYPE (TYPE_FIELDS (rtype))))
    max_index = array_type_nelts (TREE_TYPE (TYPE_FIELDS (rtype)));

  return reshape_init_array_1 (TREE_TYPE (type), max_index, d);
}

/* Subroutine of reshape_init_r, processes the initializers for classes
   or union. Parameters are the same of reshape_init_r.  */

static tree
reshape_init_class (tree type, reshape_iter *d, bool first_initializer_p)
{
  tree field;
  tree new_init;

  gcc_assert (CLASS_TYPE_P (type));

  /* The initializer for a class is always a CONSTRUCTOR.  */
  new_init = build_constructor (NULL_TREE, NULL);
  field = next_initializable_field (TYPE_FIELDS (type));

  if (!field)
    {
      /* [dcl.init.aggr]

	An initializer for an aggregate member that is an
	empty class shall have the form of an empty
	initializer-list {}.  */
      if (!first_initializer_p)
	{
	  error ("initializer for %qT must be brace-enclosed", type);
	  return error_mark_node;
	}
      return new_init;
    }

  /* Loop through the initializable fields, gathering initializers.  */
  while (d->cur != d->end)
    {
      tree field_init;

      /* Handle designated initializers, as an extension.  */
      if (d->cur->index)
	{
	  field = lookup_field_1 (type, d->cur->index, /*want_type=*/false);

	  if (!field || TREE_CODE (field) != FIELD_DECL)
	    {
	      error ("%qT has no non-static data member named %qD", type,
		    d->cur->index);
	      return error_mark_node;
	    }
	}

      /* If we processed all the member of the class, we are done.  */
      if (!field)
	break;

      field_init = reshape_init_r (TREE_TYPE (field), d,
				   /*first_initializer_p=*/false);
      CONSTRUCTOR_APPEND_ELT (CONSTRUCTOR_ELTS (new_init), field, field_init);

      /* [dcl.init.aggr]

	When a union  is  initialized with a brace-enclosed
	initializer, the braces shall only contain an
	initializer for the first member of the union.  */
      if (TREE_CODE (type) == UNION_TYPE)
	break;

      field = next_initializable_field (TREE_CHAIN (field));
    }

  return new_init;
}

/* Subroutine of reshape_init, which processes a single initializer (part of
   a CONSTRUCTOR). TYPE is the type of the variable being initialized, D is the
   iterator within the CONSTRUCTOR which points to the initializer to process.
   FIRST_INITIALIZER_P is true if this is the first initializer of the
   CONSTRUCTOR node.  */

static tree
reshape_init_r (tree type, reshape_iter *d, bool first_initializer_p)
{
  tree init = d->cur->value;

  /* A non-aggregate type is always initialized with a single
     initializer.  */
  if (!CP_AGGREGATE_TYPE_P (type))
    {
      /* It is invalid to initialize a non-aggregate type with a
	 brace-enclosed initializer.
	 We need to check for BRACE_ENCLOSED_INITIALIZER_P here because
	 of g++.old-deja/g++.mike/p7626.C: a pointer-to-member constant is
	 a CONSTRUCTOR (with a record type).  */
      if (TREE_CODE (init) == CONSTRUCTOR
	  && BRACE_ENCLOSED_INITIALIZER_P (init))  /* p7626.C */
	{
	  error ("braces around scalar initializer for type %qT", type);
	  init = error_mark_node;
	}

      d->cur++;
      return init;
    }

  /* [dcl.init.aggr]

     All implicit type conversions (clause _conv_) are considered when
     initializing the aggregate member with an initializer from an
     initializer-list.  If the initializer can initialize a member,
     the member is initialized.  Otherwise, if the member is itself a
     non-empty subaggregate, brace elision is assumed and the
     initializer is considered for the initialization of the first
     member of the subaggregate.  */
  if (TREE_CODE (init) != CONSTRUCTOR
      && can_convert_arg (type, TREE_TYPE (init), init, LOOKUP_NORMAL))
    {
      d->cur++;
      return init;
    }

  /* [dcl.init.string]

      A char array (whether plain char, signed char, or unsigned char)
      can be initialized by a string-literal (optionally enclosed in
      braces); a wchar_t array can be initialized by a wide
      string-literal (optionally enclosed in braces).  */
  if (TREE_CODE (type) == ARRAY_TYPE
      && char_type_p (TYPE_MAIN_VARIANT (TREE_TYPE (type))))
    {
      tree str_init = init;

      /* Strip one level of braces if and only if they enclose a single
	 element (as allowed by [dcl.init.string]).  */
      if (!first_initializer_p
	  && TREE_CODE (str_init) == CONSTRUCTOR
	  && VEC_length (constructor_elt, CONSTRUCTOR_ELTS (str_init)) == 1)
	{
	  str_init = VEC_index (constructor_elt,
				CONSTRUCTOR_ELTS (str_init), 0)->value;
	}

      /* If it's a string literal, then it's the initializer for the array
	 as a whole. Otherwise, continue with normal initialization for
	 array types (one value per array element).  */
      if (TREE_CODE (str_init) == STRING_CST)
	{
	  d->cur++;
	  return str_init;
	}
    }

  /* The following cases are about aggregates. If we are not within a full
     initializer already, and there is not a CONSTRUCTOR, it means that there
     is a missing set of braces (that is, we are processing the case for
     which reshape_init exists).  */
  if (!first_initializer_p)
    {
      if (TREE_CODE (init) == CONSTRUCTOR)
	{
	  if (TREE_TYPE (init) && TYPE_PTRMEMFUNC_P (TREE_TYPE (init)))
	    /* There is no need to reshape pointer-to-member function
	       initializers, as they are always constructed correctly
	       by the front end.  */
           ;
	  else if (COMPOUND_LITERAL_P (init))
	  /* For a nested compound literal, there is no need to reshape since
	     brace elision is not allowed. Even if we decided to allow it,
	     we should add a call to reshape_init in finish_compound_literal,
	     before calling digest_init, so changing this code would still
	     not be necessary.  */
	    gcc_assert (!BRACE_ENCLOSED_INITIALIZER_P (init));
	  else
	    {
	      ++d->cur;
	      gcc_assert (BRACE_ENCLOSED_INITIALIZER_P (init));
	      return reshape_init (type, init);
	    }
	}

      warning (OPT_Wmissing_braces, "missing braces around initializer for %qT",
	       type);
    }

  /* Dispatch to specialized routines.  */
  if (CLASS_TYPE_P (type))
    return reshape_init_class (type, d, first_initializer_p);
  else if (TREE_CODE (type) == ARRAY_TYPE)
    return reshape_init_array (type, d);
  else if (TREE_CODE (type) == VECTOR_TYPE)
    return reshape_init_vector (type, d);
  else
    gcc_unreachable();
}

/* Undo the brace-elision allowed by [dcl.init.aggr] in a
   brace-enclosed aggregate initializer.

   INIT is the CONSTRUCTOR containing the list of initializers describing
   a brace-enclosed initializer for an entity of the indicated aggregate TYPE.
   It may not presently match the shape of the TYPE; for example:

     struct S { int a; int b; };
     struct S a[] = { 1, 2, 3, 4 };

   Here INIT will hold a VEC of four elements, rather than a
   VEC of two elements, each itself a VEC of two elements.  This
   routine transforms INIT from the former form into the latter.  The
   revised CONSTRUCTOR node is returned.  */

tree
reshape_init (tree type, tree init)
{
  VEC(constructor_elt, gc) *v;
  reshape_iter d;
  tree new_init;

  gcc_assert (BRACE_ENCLOSED_INITIALIZER_P (init));

  v = CONSTRUCTOR_ELTS (init);

  /* An empty constructor does not need reshaping, and it is always a valid
     initializer.  */
  if (VEC_empty (constructor_elt, v))
    return init;

  /* Recurse on this CONSTRUCTOR.  */
  d.cur = VEC_index (constructor_elt, v, 0);
  d.end = d.cur + VEC_length (constructor_elt, v);

  new_init = reshape_init_r (type, &d, true);
  if (new_init == error_mark_node)
    return error_mark_node;

  /* Make sure all the element of the constructor were used. Otherwise,
     issue an error about exceeding initializers.  */
  if (d.cur != d.end)
    error ("too many initializers for %qT", type);

  return new_init;
}

/* Verify INIT (the initializer for DECL), and record the
   initialization in DECL_INITIAL, if appropriate.  CLEANUP is as for
   grok_reference_init.

   If the return value is non-NULL, it is an expression that must be
   evaluated dynamically to initialize DECL.  */

static tree
check_initializer (tree decl, tree init, int flags, tree *cleanup)
{
  tree type = TREE_TYPE (decl);
  tree init_code = NULL;

  /* Things that are going to be initialized need to have complete
     type.  */
  TREE_TYPE (decl) = type = complete_type (TREE_TYPE (decl));

  if (type == error_mark_node)
    /* We will have already complained.  */
    return NULL_TREE;

  if (TREE_CODE (type) == ARRAY_TYPE)
    {
      tree element_type = TREE_TYPE (type);

      /* The array type itself need not be complete, because the
	 initializer may tell us how many elements are in the array.
	 But, the elements of the array must be complete.  */
      if (!COMPLETE_TYPE_P (complete_type (element_type)))
	{
	  error ("elements of array %q#D have incomplete type", decl);
	  return NULL_TREE;
	}
      /* It is not valid to initialize an a VLA.  */
      if (init
	  && ((COMPLETE_TYPE_P (type) && !TREE_CONSTANT (TYPE_SIZE (type)))
	      || !TREE_CONSTANT (TYPE_SIZE (element_type))))
	{
	  error ("variable-sized object %qD may not be initialized", decl);
	  return NULL_TREE;
	}
    }
  else if (!COMPLETE_TYPE_P (type))
    {
      error ("%qD has incomplete type", decl);
      TREE_TYPE (decl) = error_mark_node;
      return NULL_TREE;
    }
  else
    /* There is no way to make a variable-sized class type in GNU C++.  */
    gcc_assert (TREE_CONSTANT (TYPE_SIZE (type)));
  
  if (!CP_AGGREGATE_TYPE_P (type)
      && init && BRACE_ENCLOSED_INITIALIZER_P (init)
      && VEC_length (constructor_elt, CONSTRUCTOR_ELTS (init)) != 1)
    {
      error ("scalar object %qD requires one element in initializer", decl);
      TREE_TYPE (decl) = error_mark_node;
      return NULL_TREE;
    }

  if (TREE_CODE (decl) == CONST_DECL)
    {
      gcc_assert (TREE_CODE (type) != REFERENCE_TYPE);

      DECL_INITIAL (decl) = init;

      gcc_assert (init != NULL_TREE);
      init = NULL_TREE;
    }
  else if (!DECL_EXTERNAL (decl) && TREE_CODE (type) == REFERENCE_TYPE)
    init = grok_reference_init (decl, type, init, cleanup);
  else if (init)
    {
      /* Do not reshape constructors of vectors (they don't need to be
	 reshaped.  */
      if (TREE_CODE (init) == CONSTRUCTOR
	  && !COMPOUND_LITERAL_P (init)
	  && !TREE_TYPE (init))  /* ptrmemfunc */
	{
	  init = reshape_init (type, init);

	  if ((*targetm.vector_opaque_p) (type))
	    {
	      error ("opaque vector types cannot be initialized");
	      init = error_mark_node;
	    }
	}

      /* If DECL has an array type without a specific bound, deduce the
	 array size from the initializer.  */
      maybe_deduce_size_from_array_init (decl, init);
      type = TREE_TYPE (decl);
      if (type == error_mark_node)
	return NULL_TREE;

      if (TYPE_HAS_CONSTRUCTOR (type) || TYPE_NEEDS_CONSTRUCTING (type))
	{
	  if (TREE_CODE (type) == ARRAY_TYPE)
	    goto initialize_aggr;
	  else if (TREE_CODE (init) == CONSTRUCTOR)
	    {
	      if (TYPE_NON_AGGREGATE_CLASS (type))
		{
		  error ("%qD must be initialized by constructor, "
			 "not by %<{...}%>",
			 decl);
		  init = error_mark_node;
		}
	      else
		goto dont_use_constructor;
	    }
	  else
	    {
	      int saved_stmts_are_full_exprs_p;

	    initialize_aggr:
	      saved_stmts_are_full_exprs_p = 0;
	      if (building_stmt_tree ())
		{
		  saved_stmts_are_full_exprs_p = stmts_are_full_exprs_p ();
		  current_stmt_tree ()->stmts_are_full_exprs_p = 1;
		}
	      init = build_aggr_init (decl, init, flags);
	      if (building_stmt_tree ())
		current_stmt_tree ()->stmts_are_full_exprs_p =
		  saved_stmts_are_full_exprs_p;
	      return init;
	    }
	}
      else
	{
	dont_use_constructor:
	  if (TREE_CODE (init) != TREE_VEC)
	    {
	      init_code = store_init_value (decl, init);
	      if (pedantic && TREE_CODE (type) == ARRAY_TYPE
		  && DECL_INITIAL (decl)
		  && TREE_CODE (DECL_INITIAL (decl)) == STRING_CST
		  && PAREN_STRING_LITERAL_P (DECL_INITIAL (decl)))
		warning (0, "array %qD initialized by parenthesized string literal %qE",
			 decl, DECL_INITIAL (decl));
	      init = NULL;
	    }
	}
    }
  else if (DECL_EXTERNAL (decl))
    ;
  else if (TYPE_P (type) && TYPE_NEEDS_CONSTRUCTING (type))
    goto initialize_aggr;
  else if (IS_AGGR_TYPE (type))
    {
      tree core_type = strip_array_types (type);

      if (CLASSTYPE_READONLY_FIELDS_NEED_INIT (core_type))
	error ("structure %qD with uninitialized const members", decl);
      if (CLASSTYPE_REF_FIELDS_NEED_INIT (core_type))
	error ("structure %qD with uninitialized reference members", decl);

      check_for_uninitialized_const_var (decl);
    }
  else
    check_for_uninitialized_const_var (decl);

  if (init && init != error_mark_node)
    init_code = build2 (INIT_EXPR, type, decl, init);

  return init_code;
}

/* If DECL is not a local variable, give it RTL.  */

static void
make_rtl_for_nonlocal_decl (tree decl, tree init, const char* asmspec)
{
  int toplev = toplevel_bindings_p ();
  int defer_p;
  const char *filename;

  /* Set the DECL_ASSEMBLER_NAME for the object.  */
  if (asmspec)
    {
      /* The `register' keyword, when used together with an
	 asm-specification, indicates that the variable should be
	 placed in a particular register.  */
      if (TREE_CODE (decl) == VAR_DECL && DECL_REGISTER (decl))
	{
	  set_user_assembler_name (decl, asmspec);
	  DECL_HARD_REGISTER (decl) = 1;
	}
      else
	{
	  if (TREE_CODE (decl) == FUNCTION_DECL
	      && DECL_BUILT_IN_CLASS (decl) == BUILT_IN_NORMAL)
	    set_builtin_user_assembler_name (decl, asmspec);
	  set_user_assembler_name (decl, asmspec);
	}
    }

  /* Handle non-variables up front.  */
  if (TREE_CODE (decl) != VAR_DECL)
    {
      rest_of_decl_compilation (decl, toplev, at_eof);
      return;
    }

  /* If we see a class member here, it should be a static data
     member.  */
  if (DECL_LANG_SPECIFIC (decl) && DECL_IN_AGGR_P (decl))
    {
      gcc_assert (TREE_STATIC (decl));
      /* An in-class declaration of a static data member should be
	 external; it is only a declaration, and not a definition.  */
      if (init == NULL_TREE)
	gcc_assert (! TREE_PUBLIC (decl) || DECL_EXTERNAL (decl));
    }

  /* We don't create any RTL for local variables.  */
  if (DECL_FUNCTION_SCOPE_P (decl) && !TREE_STATIC (decl))
    return;

  /* We defer emission of local statics until the corresponding
     DECL_EXPR is expanded.  */
  defer_p = DECL_FUNCTION_SCOPE_P (decl) || DECL_VIRTUAL_P (decl);

  /* We try to defer namespace-scope static constants so that they are
     not emitted into the object file unnecessarily.  */
  filename = input_filename;
  if (!DECL_VIRTUAL_P (decl)
      && TREE_READONLY (decl)
      && DECL_INITIAL (decl) != NULL_TREE
      && DECL_INITIAL (decl) != error_mark_node
      && filename != NULL
      && ! EMPTY_CONSTRUCTOR_P (DECL_INITIAL (decl))
      && toplev
      && !TREE_PUBLIC (decl))
    {
      /* Fool with the linkage of static consts according to #pragma
	 interface.  */
      struct c_fileinfo *finfo = get_fileinfo (filename);
      if (!finfo->interface_unknown && !TREE_PUBLIC (decl))
	{
	  TREE_PUBLIC (decl) = 1;
	  DECL_EXTERNAL (decl) = finfo->interface_only;
	}

      defer_p = 1;
    }
  /* Likewise for template instantiations.  */
  else if (DECL_LANG_SPECIFIC (decl)
	   && DECL_IMPLICIT_INSTANTIATION (decl))
    defer_p = 1;

  /* If we're not deferring, go ahead and assemble the variable.  */
  if (!defer_p)
    rest_of_decl_compilation (decl, toplev, at_eof);
}

/* Generate code to initialize DECL (a local variable).  */

static void
initialize_local_var (tree decl, tree init)
{
  tree type = TREE_TYPE (decl);
  tree cleanup;

  gcc_assert (TREE_CODE (decl) == VAR_DECL
	      || TREE_CODE (decl) == RESULT_DECL);
  gcc_assert (!TREE_STATIC (decl));

  if (DECL_SIZE (decl) == NULL_TREE)
    {
      /* If we used it already as memory, it must stay in memory.  */
      DECL_INITIAL (decl) = NULL_TREE;
      TREE_ADDRESSABLE (decl) = TREE_USED (decl);
    }

  if (DECL_SIZE (decl) && type != error_mark_node)
    {
      int already_used;

      /* Compute and store the initial value.  */
      already_used = TREE_USED (decl) || TREE_USED (type);

      /* Perform the initialization.  */
      if (init)
	{
	  int saved_stmts_are_full_exprs_p;

	  gcc_assert (building_stmt_tree ());
	  saved_stmts_are_full_exprs_p = stmts_are_full_exprs_p ();
	  current_stmt_tree ()->stmts_are_full_exprs_p = 1;
	  finish_expr_stmt (init);
	  current_stmt_tree ()->stmts_are_full_exprs_p =
	    saved_stmts_are_full_exprs_p;
	}

      /* Set this to 0 so we can tell whether an aggregate which was
	 initialized was ever used.  Don't do this if it has a
	 destructor, so we don't complain about the 'resource
	 allocation is initialization' idiom.  Now set
	 attribute((unused)) on types so decls of that type will be
	 marked used. (see TREE_USED, above.)  */
      if (TYPE_NEEDS_CONSTRUCTING (type)
	  && ! already_used
	  && TYPE_HAS_TRIVIAL_DESTRUCTOR (type)
	  && DECL_NAME (decl))
	TREE_USED (decl) = 0;
      else if (already_used)
	TREE_USED (decl) = 1;
    }

  /* Generate a cleanup, if necessary.  */
  cleanup = cxx_maybe_build_cleanup (decl);
  if (DECL_SIZE (decl) && cleanup)
    finish_decl_cleanup (decl, cleanup);
}

/* DECL is a VAR_DECL for a compiler-generated variable with static
   storage duration (like a virtual table) whose initializer is a
   compile-time constant.  INIT must be either a TREE_LIST of values,
   or a CONSTRUCTOR.  Initialize the variable and provide it to the
   back end.  */

void
initialize_artificial_var (tree decl, tree init)
{
  gcc_assert (DECL_ARTIFICIAL (decl));
  if (TREE_CODE (init) == TREE_LIST)
    init = build_constructor_from_list (NULL_TREE, init);
  gcc_assert (TREE_CODE (init) == CONSTRUCTOR);
  DECL_INITIAL (decl) = init;
  DECL_INITIALIZED_P (decl) = 1;
  determine_visibility (decl);
  layout_var_decl (decl);
  maybe_commonize_var (decl);
  make_rtl_for_nonlocal_decl (decl, init, /*asmspec=*/NULL);
}

/* INIT is the initializer for a variable, as represented by the
   parser.  Returns true iff INIT is value-dependent.  */

static bool
value_dependent_init_p (tree init)
{
  if (TREE_CODE (init) == TREE_LIST)
    /* A parenthesized initializer, e.g.: int i (3, 2); ? */
    return any_value_dependent_elements_p (init);
  else if (TREE_CODE (init) == CONSTRUCTOR)
  /* A brace-enclosed initializer, e.g.: int i = { 3 }; ? */
    {
      VEC(constructor_elt, gc) *elts;
      size_t nelts;
      size_t i;

      elts = CONSTRUCTOR_ELTS (init);
      nelts = VEC_length (constructor_elt, elts);
      for (i = 0; i < nelts; ++i)
	if (value_dependent_init_p (VEC_index (constructor_elt,
					       elts, i)->value))
	  return true;
    }
  else
    /* It must be a simple expression, e.g., int i = 3;  */
    return value_dependent_expression_p (init);
  
  return false;
}

/* Finish processing of a declaration;
   install its line number and initial value.
   If the length of an array type is not known before,
   it must be determined now, from the initial value, or it is an error.

   INIT is the initializer (if any) for DECL.  If INIT_CONST_EXPR_P is
   true, then INIT is an integral constant expression.

   FLAGS is LOOKUP_ONLYCONVERTING if the = init syntax was used, else 0
   if the (init) syntax was used.  */

void
cp_finish_decl (tree decl, tree init, bool init_const_expr_p,
		tree asmspec_tree, int flags)
{
  tree type;
  tree cleanup;
  const char *asmspec = NULL;
  int was_readonly = 0;
  bool var_definition_p = false;
  int saved_processing_template_decl;

  if (decl == error_mark_node)
    return;
  else if (! decl)
    {
      if (init)
	error ("assignment (not initialization) in declaration");
      return;
    }

  gcc_assert (TREE_CODE (decl) != RESULT_DECL);
  /* Parameters are handled by store_parm_decls, not cp_finish_decl.  */
  gcc_assert (TREE_CODE (decl) != PARM_DECL);

  type = TREE_TYPE (decl);
  if (type == error_mark_node)
    return;

  /* Assume no cleanup is required.  */
  cleanup = NULL_TREE;
  saved_processing_template_decl = processing_template_decl;

  /* If a name was specified, get the string.  */
  if (global_scope_p (current_binding_level))
    asmspec_tree = maybe_apply_renaming_pragma (decl, asmspec_tree);
  if (asmspec_tree && asmspec_tree != error_mark_node)
    asmspec = TREE_STRING_POINTER (asmspec_tree);

  if (current_class_type
      && CP_DECL_CONTEXT (decl) == current_class_type
      && TYPE_BEING_DEFINED (current_class_type)
      && (DECL_INITIAL (decl) || init))
    DECL_INITIALIZED_IN_CLASS_P (decl) = 1;

  if (processing_template_decl)
    {
      bool type_dependent_p;

      /* Add this declaration to the statement-tree.  */
      if (at_function_scope_p ())
	add_decl_expr (decl);

      type_dependent_p = dependent_type_p (type);

      if (init && init_const_expr_p)
	{
	  DECL_INITIALIZED_BY_CONSTANT_EXPRESSION_P (decl) = 1;
	  if (DECL_INTEGRAL_CONSTANT_VAR_P (decl))
	    TREE_CONSTANT (decl) = 1;
	}

      /* Generally, initializers in templates are expanded when the
	 template is instantiated.  But, if DECL is an integral
	 constant static data member, then it can be used in future
	 integral constant expressions, and its value must be
	 available. */
      if (!(init
	    && DECL_CLASS_SCOPE_P (decl)
	    && DECL_INTEGRAL_CONSTANT_VAR_P (decl)
	    && !type_dependent_p
	    && !value_dependent_init_p (init)))
	{
	  if (init)
	    DECL_INITIAL (decl) = init;
	  if (TREE_CODE (decl) == VAR_DECL
	      && !DECL_PRETTY_FUNCTION_P (decl)
	      && !type_dependent_p)
	    maybe_deduce_size_from_array_init (decl, init);
	  goto finish_end;
	}

      init = fold_non_dependent_expr (init);
      processing_template_decl = 0;
    }

  /* Take care of TYPE_DECLs up front.  */
  if (TREE_CODE (decl) == TYPE_DECL)
    {
      if (type != error_mark_node
	  && IS_AGGR_TYPE (type) && DECL_NAME (decl))
	{
	  if (TREE_TYPE (DECL_NAME (decl)) && TREE_TYPE (decl) != type)
	    warning (0, "shadowing previous type declaration of %q#D", decl);
	  set_identifier_type_value (DECL_NAME (decl), decl);
	}

      /* If we have installed this as the canonical typedef for this
	 type, and that type has not been defined yet, delay emitting
	 the debug information for it, as we will emit it later.  */
      if (TYPE_MAIN_DECL (TREE_TYPE (decl)) == decl
	  && !COMPLETE_TYPE_P (TREE_TYPE (decl)))
	TYPE_DECL_SUPPRESS_DEBUG (decl) = 1;

      rest_of_decl_compilation (decl, DECL_CONTEXT (decl) == NULL_TREE,
				at_eof);
      goto finish_end;
    }

  /* A reference will be modified here, as it is initialized.  */
  if (! DECL_EXTERNAL (decl)
      && TREE_READONLY (decl)
      && TREE_CODE (type) == REFERENCE_TYPE)
    {
      was_readonly = 1;
      TREE_READONLY (decl) = 0;
    }

  if (TREE_CODE (decl) == VAR_DECL)
    {
      /* Only PODs can have thread-local storage.  Other types may require
	 various kinds of non-trivial initialization.  */
      if (DECL_THREAD_LOCAL_P (decl) && !pod_type_p (TREE_TYPE (decl)))
	error ("%qD cannot be thread-local because it has non-POD type %qT",
	       decl, TREE_TYPE (decl));
      /* If this is a local variable that will need a mangled name,
	 register it now.  We must do this before processing the
	 initializer for the variable, since the initialization might
	 require a guard variable, and since the mangled name of the
	 guard variable will depend on the mangled name of this
	 variable.  */
      if (!processing_template_decl
	  && DECL_FUNCTION_SCOPE_P (decl)
	  && TREE_STATIC (decl)
	  && !DECL_ARTIFICIAL (decl))
	push_local_name (decl);
      /* Convert the initializer to the type of DECL, if we have not
	 already initialized DECL.  */
      if (!DECL_INITIALIZED_P (decl)
	  /* If !DECL_EXTERNAL then DECL is being defined.  In the
	     case of a static data member initialized inside the
	     class-specifier, there can be an initializer even if DECL
	     is *not* defined.  */
	  && (!DECL_EXTERNAL (decl) || init))
	{
	  if (init)
	    {
	      DECL_NONTRIVIALLY_INITIALIZED_P (decl) = 1;
	      if (init_const_expr_p)
		{
		  DECL_INITIALIZED_BY_CONSTANT_EXPRESSION_P (decl) = 1;
		  if (DECL_INTEGRAL_CONSTANT_VAR_P (decl))
		    TREE_CONSTANT (decl) = 1;
		}
	    }
	  init = check_initializer (decl, init, flags, &cleanup);
	  /* Thread-local storage cannot be dynamically initialized.  */
	  if (DECL_THREAD_LOCAL_P (decl) && init)
	    {
	      error ("%qD is thread-local and so cannot be dynamically "
		     "initialized", decl);
	      init = NULL_TREE;
	    }

	  /* Check that the initializer for a static data member was a
	     constant.  Although we check in the parser that the
	     initializer is an integral constant expression, we do not
	     simplify division-by-zero at the point at which it
	     occurs.  Therefore, in:

	       struct S { static const int i = 7 / 0; };

	     we issue an error at this point.  It would
	     probably be better to forbid division by zero in
	     integral constant expressions.  */
	  if (DECL_EXTERNAL (decl) && init)
	    {
	      error ("%qD cannot be initialized by a non-constant expression"
		     " when being declared", decl);
	      DECL_INITIALIZED_IN_CLASS_P (decl) = 0;
	      init = NULL_TREE;
	    }

	  /* Handle:

	     [dcl.init]

	     The memory occupied by any object of static storage
	     duration is zero-initialized at program startup before
	     any other initialization takes place.

	     We cannot create an appropriate initializer until after
	     the type of DECL is finalized.  If DECL_INITIAL is set,
	     then the DECL is statically initialized, and any
	     necessary zero-initialization has already been performed.  */
	  if (TREE_STATIC (decl) && !DECL_INITIAL (decl))
	    DECL_INITIAL (decl) = build_zero_init (TREE_TYPE (decl),
						   /*nelts=*/NULL_TREE,
						   /*static_storage_p=*/true);
	  /* Remember that the initialization for this variable has
	     taken place.  */
	  DECL_INITIALIZED_P (decl) = 1;
	  /* This declaration is the definition of this variable,
	     unless we are initializing a static data member within
	     the class specifier.  */
	  if (!DECL_EXTERNAL (decl))
	    var_definition_p = true;
	}
      /* If the variable has an array type, lay out the type, even if
	 there is no initializer.  It is valid to index through the
	 array, and we must get TYPE_ALIGN set correctly on the array
	 type.  */
      else if (TREE_CODE (type) == ARRAY_TYPE)
	layout_type (type);
    }

  /* Add this declaration to the statement-tree.  This needs to happen
     after the call to check_initializer so that the DECL_EXPR for a
     reference temp is added before the DECL_EXPR for the reference itself.  */
  if (at_function_scope_p ())
    add_decl_expr (decl);

  /* Let the middle end know about variables and functions -- but not
     static data members in uninstantiated class templates.  */
  if (!saved_processing_template_decl
      && (TREE_CODE (decl) == VAR_DECL 
	  || TREE_CODE (decl) == FUNCTION_DECL))
    {
      if (TREE_CODE (decl) == VAR_DECL)
	{
	  layout_var_decl (decl);
	  maybe_commonize_var (decl);
	}

      make_rtl_for_nonlocal_decl (decl, init, asmspec);

      /* Check for abstractness of the type. Notice that there is no
	 need to strip array types here since the check for those types
	 is already done within create_array_type_for_decl.  */
      if (TREE_CODE (type) == FUNCTION_TYPE
	  || TREE_CODE (type) == METHOD_TYPE)
	abstract_virtuals_error (decl, TREE_TYPE (type));
      else
	abstract_virtuals_error (decl, type);

      /* This needs to happen after the linkage is set. */
      determine_visibility (decl);

      if (TREE_CODE (decl) == FUNCTION_DECL
	  || TREE_TYPE (decl) == error_mark_node)
	/* No initialization required.  */
	;
      else if (DECL_EXTERNAL (decl)
	       && ! (DECL_LANG_SPECIFIC (decl)
		     && DECL_NOT_REALLY_EXTERN (decl)))
	{
	  if (init)
	    DECL_INITIAL (decl) = init;
	}
      else
	{
	  /* A variable definition.  */
	  if (DECL_FUNCTION_SCOPE_P (decl))
	    {
	      /* Initialize the local variable.  */
	      if (processing_template_decl)
		DECL_INITIAL (decl) = init;
	      else if (!TREE_STATIC (decl))
		initialize_local_var (decl, init);
	    }

	  /* If a variable is defined, and then a subsequent
	     definition with external linkage is encountered, we will
	     get here twice for the same variable.  We want to avoid
	     calling expand_static_init more than once.  For variables
	     that are not static data members, we can call
	     expand_static_init only when we actually process the
	     initializer.  It is not legal to redeclare a static data
	     member, so this issue does not arise in that case.  */
	  if (var_definition_p && TREE_STATIC (decl))
	    {
              /* If a TREE_READONLY variable needs initialization
		 at runtime, it is no longer readonly and we need to
		 avoid MEM_READONLY_P being set on RTL created for it.  */
	      if (init)
		{
		  if (TREE_READONLY (decl))
		    TREE_READONLY (decl) = 0;
		  was_readonly = 0;
		}
	      expand_static_init (decl, init);
	    }
	}
    }

  /* If a CLEANUP_STMT was created to destroy a temporary bound to a
     reference, insert it in the statement-tree now.  */
  if (cleanup)
    push_cleanup (decl, cleanup, false);

 finish_end:
  processing_template_decl = saved_processing_template_decl;

  if (was_readonly)
    TREE_READONLY (decl) = 1;

  /* If this was marked 'used', be sure it will be output.  */
  if (lookup_attribute ("used", DECL_ATTRIBUTES (decl)))
    mark_decl_referenced (decl);
}

/* This is here for a midend callback from c-common.c.  */

void
finish_decl (tree decl, tree init, tree asmspec_tree)
{
  cp_finish_decl (decl, init, /*init_const_expr_p=*/false, asmspec_tree, 0);
}

/* Returns a declaration for a VAR_DECL as if:

     extern "C" TYPE NAME;

   had been seen.  Used to create compiler-generated global
   variables.  */

static tree
declare_global_var (tree name, tree type)
{
  tree decl;

  push_to_top_level ();
  decl = build_decl (VAR_DECL, name, type);
  TREE_PUBLIC (decl) = 1;
  DECL_EXTERNAL (decl) = 1;
  DECL_ARTIFICIAL (decl) = 1;
  /* If the user has explicitly declared this variable (perhaps
     because the code we are compiling is part of a low-level runtime
     library), then it is possible that our declaration will be merged
     with theirs by pushdecl.  */
  decl = pushdecl (decl);
  finish_decl (decl, NULL_TREE, NULL_TREE);
  pop_from_top_level ();

  return decl;
}

/* Returns a pointer to the `atexit' function.  Note that if
   FLAG_USE_CXA_ATEXIT is nonzero, then this will actually be the new
   `__cxa_atexit' function specified in the IA64 C++ ABI.  */

static tree
get_atexit_node (void)
{
  tree atexit_fndecl;
  tree arg_types;
  tree fn_type;
  tree fn_ptr_type;
  const char *name;
  bool use_aeabi_atexit;

  if (atexit_node)
    return atexit_node;

  if (flag_use_cxa_atexit)
    {
      /* The declaration for `__cxa_atexit' is:

	   int __cxa_atexit (void (*)(void *), void *, void *)

	 We build up the argument types and then then function type
	 itself.  */

      use_aeabi_atexit = targetm.cxx.use_aeabi_atexit ();
      /* First, build the pointer-to-function type for the first
	 argument.  */
      arg_types = tree_cons (NULL_TREE, ptr_type_node, void_list_node);
      fn_type = build_function_type (void_type_node, arg_types);
      fn_ptr_type = build_pointer_type (fn_type);
      /* Then, build the rest of the argument types.  */
      arg_types = tree_cons (NULL_TREE, ptr_type_node, void_list_node);
      if (use_aeabi_atexit)
	{
	  arg_types = tree_cons (NULL_TREE, fn_ptr_type, arg_types);
	  arg_types = tree_cons (NULL_TREE, ptr_type_node, arg_types);
	}
      else
	{
	  arg_types = tree_cons (NULL_TREE, ptr_type_node, arg_types);
	  arg_types = tree_cons (NULL_TREE, fn_ptr_type, arg_types);
	}
      /* And the final __cxa_atexit type.  */
      fn_type = build_function_type (integer_type_node, arg_types);
      fn_ptr_type = build_pointer_type (fn_type);
      if (use_aeabi_atexit)
	name = "__aeabi_atexit";
      else
	name = "__cxa_atexit";
    }
  else
    {
      /* The declaration for `atexit' is:

	   int atexit (void (*)());

	 We build up the argument types and then then function type
	 itself.  */
      fn_type = build_function_type (void_type_node, void_list_node);
      fn_ptr_type = build_pointer_type (fn_type);
      arg_types = tree_cons (NULL_TREE, fn_ptr_type, void_list_node);
      /* Build the final atexit type.  */
      fn_type = build_function_type (integer_type_node, arg_types);
      name = "atexit";
    }

  /* Now, build the function declaration.  */
  push_lang_context (lang_name_c);
  atexit_fndecl = build_library_fn_ptr (name, fn_type);
  mark_used (atexit_fndecl);
  pop_lang_context ();
  atexit_node = decay_conversion (atexit_fndecl);

  return atexit_node;
}

/* Returns the __dso_handle VAR_DECL.  */

static tree
get_dso_handle_node (void)
{
  if (dso_handle_node)
    return dso_handle_node;

  /* Declare the variable.  */
  dso_handle_node = declare_global_var (get_identifier ("__dso_handle"),
					ptr_type_node);

  return dso_handle_node;
}

/* Begin a new function with internal linkage whose job will be simply
   to destroy some particular variable.  */

static GTY(()) int start_cleanup_cnt;

static tree
start_cleanup_fn (void)
{
  char name[32];
  tree parmtypes;
  tree fntype;
  tree fndecl;

  push_to_top_level ();

  /* No need to mangle this.  */
  push_lang_context (lang_name_c);

  /* Build the parameter-types.  */
  parmtypes = void_list_node;
  /* Functions passed to __cxa_atexit take an additional parameter.
     We'll just ignore it.  After we implement the new calling
     convention for destructors, we can eliminate the use of
     additional cleanup functions entirely in the -fnew-abi case.  */
  if (flag_use_cxa_atexit)
    parmtypes = tree_cons (NULL_TREE, ptr_type_node, parmtypes);
  /* Build the function type itself.  */
  fntype = build_function_type (void_type_node, parmtypes);
  /* Build the name of the function.  */
  sprintf (name, "__tcf_%d", start_cleanup_cnt++);
  /* Build the function declaration.  */
  fndecl = build_lang_decl (FUNCTION_DECL, get_identifier (name), fntype);
  /* It's a function with internal linkage, generated by the
     compiler.  */
  TREE_PUBLIC (fndecl) = 0;
  DECL_ARTIFICIAL (fndecl) = 1;
  /* Make the function `inline' so that it is only emitted if it is
     actually needed.  It is unlikely that it will be inlined, since
     it is only called via a function pointer, but we avoid unnecessary
     emissions this way.  */
  DECL_INLINE (fndecl) = 1;
  DECL_DECLARED_INLINE_P (fndecl) = 1;
  DECL_INTERFACE_KNOWN (fndecl) = 1;
  /* Build the parameter.  */
  if (flag_use_cxa_atexit)
    {
      tree parmdecl;

      parmdecl = cp_build_parm_decl (NULL_TREE, ptr_type_node);
      DECL_CONTEXT (parmdecl) = fndecl;
      TREE_USED (parmdecl) = 1;
      DECL_ARGUMENTS (fndecl) = parmdecl;
    }

  pushdecl (fndecl);
  start_preparsed_function (fndecl, NULL_TREE, SF_PRE_PARSED);

  pop_lang_context ();

  return current_function_decl;
}

/* Finish the cleanup function begun by start_cleanup_fn.  */

static void
end_cleanup_fn (void)
{
  expand_or_defer_fn (finish_function (0));

  pop_from_top_level ();
}

/* Generate code to handle the destruction of DECL, an object with
   static storage duration.  */

tree
register_dtor_fn (tree decl)
{
  tree cleanup;
  tree compound_stmt;
  tree args;
  tree fcall;

  if (TYPE_HAS_TRIVIAL_DESTRUCTOR (TREE_TYPE (decl)))
    return void_zero_node;

  /* Call build_cleanup before we enter the anonymous function so that
     any access checks will be done relative to the current scope,
     rather than the scope of the anonymous function.  */
  build_cleanup (decl);

  /* Now start the function.  */
  cleanup = start_cleanup_fn ();

  /* Now, recompute the cleanup.  It may contain SAVE_EXPRs that refer
     to the original function, rather than the anonymous one.  That
     will make the back-end think that nested functions are in use,
     which causes confusion.  */

  push_deferring_access_checks (dk_no_check);
  fcall = build_cleanup (decl);
  pop_deferring_access_checks ();

  /* Create the body of the anonymous function.  */
  compound_stmt = begin_compound_stmt (BCS_FN_BODY);
  finish_expr_stmt (fcall);
  finish_compound_stmt (compound_stmt);
  end_cleanup_fn ();

  /* Call atexit with the cleanup function.  */
  cxx_mark_addressable (cleanup);
  mark_used (cleanup);
  cleanup = build_unary_op (ADDR_EXPR, cleanup, 0);
  if (flag_use_cxa_atexit)
    {
      args = tree_cons (NULL_TREE,
			build_unary_op (ADDR_EXPR, get_dso_handle_node (), 0),
			NULL_TREE);
      if (targetm.cxx.use_aeabi_atexit ())
	{
	  args = tree_cons (NULL_TREE, cleanup, args);
	  args = tree_cons (NULL_TREE, null_pointer_node, args);
	}
      else
	{
	  args = tree_cons (NULL_TREE, null_pointer_node, args);
	  args = tree_cons (NULL_TREE, cleanup, args);
	}
    }
  else
    args = tree_cons (NULL_TREE, cleanup, NULL_TREE);
  return build_function_call (get_atexit_node (), args);
}

/* DECL is a VAR_DECL with static storage duration.  INIT, if present,
   is its initializer.  Generate code to handle the construction
   and destruction of DECL.  */

static void
expand_static_init (tree decl, tree init)
{
  gcc_assert (TREE_CODE (decl) == VAR_DECL);
  gcc_assert (TREE_STATIC (decl));

  /* Some variables require no initialization.  */
  if (!init
      && !TYPE_NEEDS_CONSTRUCTING (TREE_TYPE (decl))
      && TYPE_HAS_TRIVIAL_DESTRUCTOR (TREE_TYPE (decl)))
    return;

  if (DECL_FUNCTION_SCOPE_P (decl))
    {
      /* Emit code to perform this initialization but once.  */
      tree if_stmt = NULL_TREE, inner_if_stmt = NULL_TREE;
      tree then_clause = NULL_TREE, inner_then_clause = NULL_TREE;
      tree guard, guard_addr, guard_addr_list;
      tree acquire_fn, release_fn, abort_fn;
      tree flag, begin;

      /* Emit code to perform this initialization but once.  This code
	 looks like:

	   static <type> guard;
	   if (!guard.first_byte) {
	     if (__cxa_guard_acquire (&guard)) {
	       bool flag = false;
	       try {
		 // Do initialization.
		 flag = true; __cxa_guard_release (&guard);
		 // Register variable for destruction at end of program.
	       } catch {
		 if (!flag) __cxa_guard_abort (&guard);
	       }
	   }

	 Note that the `flag' variable is only set to 1 *after* the
	 initialization is complete.  This ensures that an exception,
	 thrown during the construction, will cause the variable to
	 reinitialized when we pass through this code again, as per:

	   [stmt.dcl]

	   If the initialization exits by throwing an exception, the
	   initialization is not complete, so it will be tried again
	   the next time control enters the declaration.

	 This process should be thread-safe, too; multiple threads
	 should not be able to initialize the variable more than
	 once.  */

      /* Create the guard variable.  */
      guard = get_guard (decl);

      /* This optimization isn't safe on targets with relaxed memory
	 consistency.  On such targets we force synchronization in
	 __cxa_guard_acquire.  */
      if (!targetm.relaxed_ordering || !flag_threadsafe_statics)
	{
	  /* Begin the conditional initialization.  */
	  if_stmt = begin_if_stmt ();
	  finish_if_stmt_cond (get_guard_cond (guard), if_stmt);
	  then_clause = begin_compound_stmt (BCS_NO_SCOPE);
	}

      if (flag_threadsafe_statics)
	{
	  guard_addr = build_address (guard);
	  guard_addr_list = build_tree_list (NULL_TREE, guard_addr);

	  acquire_fn = get_identifier ("__cxa_guard_acquire");
	  release_fn = get_identifier ("__cxa_guard_release");
	  abort_fn = get_identifier ("__cxa_guard_abort");
	  if (!get_global_value_if_present (acquire_fn, &acquire_fn))
	    {
	      tree argtypes = tree_cons (NULL_TREE, TREE_TYPE (guard_addr),
					 void_list_node);
	      tree vfntype = build_function_type (void_type_node, argtypes);
	      acquire_fn = push_library_fn
		(acquire_fn, build_function_type (integer_type_node, argtypes));
	      release_fn = push_library_fn (release_fn, vfntype);
	      abort_fn = push_library_fn (abort_fn, vfntype);
	    }
	  else
	    {
	      release_fn = identifier_global_value (release_fn);
	      abort_fn = identifier_global_value (abort_fn);
	    }

	  inner_if_stmt = begin_if_stmt ();
	  finish_if_stmt_cond (build_call (acquire_fn, guard_addr_list),
			       inner_if_stmt);

	  inner_then_clause = begin_compound_stmt (BCS_NO_SCOPE);
	  begin = get_target_expr (boolean_false_node);
	  flag = TARGET_EXPR_SLOT (begin);

	  TARGET_EXPR_CLEANUP (begin)
	    = build3 (COND_EXPR, void_type_node, flag,
		      void_zero_node,
		      build_call (abort_fn, guard_addr_list));
	  CLEANUP_EH_ONLY (begin) = 1;

	  /* Do the initialization itself.  */
	  init = add_stmt_to_compound (begin, init);
	  init = add_stmt_to_compound
	    (init, build2 (MODIFY_EXPR, void_type_node, flag, boolean_true_node));
	  init = add_stmt_to_compound
	    (init, build_call (release_fn, guard_addr_list));
	}
      else
	init = add_stmt_to_compound (init, set_guard (guard));

      /* Use atexit to register a function for destroying this static
	 variable.  */
      init = add_stmt_to_compound (init, register_dtor_fn (decl));

      finish_expr_stmt (init);

      if (flag_threadsafe_statics)
	{
	  finish_compound_stmt (inner_then_clause);
	  finish_then_clause (inner_if_stmt);
	  finish_if_stmt (inner_if_stmt);
	}

      if (!targetm.relaxed_ordering || !flag_threadsafe_statics)
	{
	  finish_compound_stmt (then_clause);
	  finish_then_clause (if_stmt);
	  finish_if_stmt (if_stmt);
	}
    }
  else
    static_aggregates = tree_cons (init, decl, static_aggregates);
}


/* Make TYPE a complete type based on INITIAL_VALUE.
   Return 0 if successful, 1 if INITIAL_VALUE can't be deciphered,
   2 if there was no information (in which case assume 0 if DO_DEFAULT),
   3 if the initializer list is empty (in pedantic mode). */

int
cp_complete_array_type (tree *ptype, tree initial_value, bool do_default)
{
  int failure;
  tree type, elt_type;

  if (initial_value)
    {
      /* An array of character type can be initialized from a
	 brace-enclosed string constant.

	 FIXME: this code is duplicated from reshape_init. Probably
	 we should just call reshape_init here?  */
      if (char_type_p (TYPE_MAIN_VARIANT (TREE_TYPE (*ptype)))
	  && TREE_CODE (initial_value) == CONSTRUCTOR
	  && !VEC_empty (constructor_elt, CONSTRUCTOR_ELTS (initial_value)))
	{
	  VEC(constructor_elt,gc) *v = CONSTRUCTOR_ELTS (initial_value);
	  tree value = VEC_index (constructor_elt, v, 0)->value;

	  if (TREE_CODE (value) == STRING_CST
	      && VEC_length (constructor_elt, v) == 1)
	    initial_value = value;
	}
    }

  failure = complete_array_type (ptype, initial_value, do_default);

  /* We can create the array before the element type is complete, which
     means that we didn't have these two bits set in the original type
     either.  In completing the type, we are expected to propagate these
     bits.  See also complete_type which does the same thing for arrays
     of fixed size.  */
  type = *ptype;
  if (TYPE_DOMAIN (type))
    {
      elt_type = TREE_TYPE (type);
      TYPE_NEEDS_CONSTRUCTING (type) = TYPE_NEEDS_CONSTRUCTING (elt_type);
      TYPE_HAS_NONTRIVIAL_DESTRUCTOR (type)
	= TYPE_HAS_NONTRIVIAL_DESTRUCTOR (elt_type);
    }

  return failure;
}

/* Return zero if something is declared to be a member of type
   CTYPE when in the context of CUR_TYPE.  STRING is the error
   message to print in that case.  Otherwise, quietly return 1.  */

static int
member_function_or_else (tree ctype, tree cur_type, enum overload_flags flags)
{
  if (ctype && ctype != cur_type)
    {
      if (flags == DTOR_FLAG)
	error ("destructor for alien class %qT cannot be a member", ctype);
      else
	error ("constructor for alien class %qT cannot be a member", ctype);
      return 0;
    }
  return 1;
}

/* Subroutine of `grokdeclarator'.  */

/* Generate errors possibly applicable for a given set of specifiers.
   This is for ARM $7.1.2.  */

static void
bad_specifiers (tree object,
		const char* type,
		int virtualp,
		int quals,
		int inlinep,
		int friendp,
		int raises)
{
  if (virtualp)
    error ("%qD declared as a %<virtual%> %s", object, type);
  if (inlinep)
    error ("%qD declared as an %<inline%> %s", object, type);
  if (quals)
    error ("%<const%> and %<volatile%> function specifiers on "
	   "%qD invalid in %s declaration",
	   object, type);
  if (friendp)
    error ("%q+D declared as a friend", object);
  if (raises
      && (TREE_CODE (object) == TYPE_DECL
	  || (!TYPE_PTRFN_P (TREE_TYPE (object))
	      && !TYPE_REFFN_P (TREE_TYPE (object))
	      && !TYPE_PTRMEMFUNC_P (TREE_TYPE (object)))))
    error ("%q+D declared with an exception specification", object);
}

/* DECL is a member function or static data member and is presently
   being defined.  Check that the definition is taking place in a
   valid namespace.  */

static void
check_class_member_definition_namespace (tree decl)
{
  /* These checks only apply to member functions and static data
     members.  */
  gcc_assert (TREE_CODE (decl) == FUNCTION_DECL
	      || TREE_CODE (decl) == VAR_DECL);
  /* We check for problems with specializations in pt.c in
     check_specialization_namespace, where we can issue better
     diagnostics.  */
  if (processing_specialization)
    return;
  /* There are no restrictions on the placement of
     explicit instantiations.  */
  if (processing_explicit_instantiation)
    return;
  /* [class.mfct]

     A member function definition that appears outside of the
     class definition shall appear in a namespace scope enclosing
     the class definition.

     [class.static.data]

     The definition for a static data member shall appear in a
     namespace scope enclosing the member's class definition.  */
  if (!is_ancestor (current_namespace, DECL_CONTEXT (decl)))
    pedwarn ("definition of %qD is not in namespace enclosing %qT",
	     decl, DECL_CONTEXT (decl));
}

/* Build a PARM_DECL for the "this" parameter.  TYPE is the
   METHOD_TYPE for a non-static member function; QUALS are the
   cv-qualifiers that apply to the function.  */

tree
build_this_parm (tree type, cp_cv_quals quals)
{
  tree this_type;
  tree qual_type;
  tree parm;
  cp_cv_quals this_quals;

  this_type = TREE_VALUE (TYPE_ARG_TYPES (type));
  /* The `this' parameter is implicitly `const'; it cannot be
     assigned to.  */
  this_quals = (quals & TYPE_QUAL_RESTRICT) | TYPE_QUAL_CONST;
  qual_type = cp_build_qualified_type (this_type, this_quals);
  parm = build_artificial_parm (this_identifier, qual_type);
  cp_apply_type_quals_to_decl (this_quals, parm);
  return parm;
}

/* CTYPE is class type, or null if non-class.
   TYPE is type this FUNCTION_DECL should have, either FUNCTION_TYPE
   or METHOD_TYPE.
   DECLARATOR is the function's name.
   PARMS is a chain of PARM_DECLs for the function.
   VIRTUALP is truthvalue of whether the function is virtual or not.
   FLAGS are to be passed through to `grokclassfn'.
   QUALS are qualifiers indicating whether the function is `const'
   or `volatile'.
   RAISES is a list of exceptions that this function can raise.
   CHECK is 1 if we must find this method in CTYPE, 0 if we should
   not look, and -1 if we should not call `grokclassfn' at all.

   SFK is the kind of special function (if any) for the new function.

   Returns `NULL_TREE' if something goes wrong, after issuing
   applicable error messages.  */

static tree
grokfndecl (tree ctype,
	    tree type,
	    tree declarator,
	    tree parms,
	    tree orig_declarator,
	    int virtualp,
	    enum overload_flags flags,
	    cp_cv_quals quals,
	    tree raises,
	    int check,
	    int friendp,
	    int publicp,
	    int inlinep,
	    special_function_kind sfk,
	    bool funcdef_flag,
	    int template_count,
	    tree in_namespace,
	    tree* attrlist)
{
  tree decl;
  int staticp = ctype && TREE_CODE (type) == FUNCTION_TYPE;
  tree t;

  if (raises)
    type = build_exception_variant (type, raises);

  decl = build_lang_decl (FUNCTION_DECL, declarator, type);
  if (TREE_CODE (type) == METHOD_TYPE)
    {
      tree parm;
      parm = build_this_parm (type, quals);
      TREE_CHAIN (parm) = parms;
      parms = parm;
    }
  DECL_ARGUMENTS (decl) = parms;
  /* Propagate volatile out from type to decl.  */
  if (TYPE_VOLATILE (type))
    TREE_THIS_VOLATILE (decl) = 1;

  if (friendp
      && TREE_CODE (orig_declarator) == TEMPLATE_ID_EXPR)
    {
      if (funcdef_flag)
	error
	  ("defining explicit specialization %qD in friend declaration",
	   orig_declarator);
      else
	{
	  tree fns = TREE_OPERAND (orig_declarator, 0);
	  tree args = TREE_OPERAND (orig_declarator, 1);

	  if (PROCESSING_REAL_TEMPLATE_DECL_P ())
	    {
	      /* Something like `template <class T> friend void f<T>()'.  */
	      error ("invalid use of template-id %qD in declaration "
		     "of primary template",
		     orig_declarator);
	      return NULL_TREE;
	    }


	  /* A friend declaration of the form friend void f<>().  Record
	     the information in the TEMPLATE_ID_EXPR.  */
	  SET_DECL_IMPLICIT_INSTANTIATION (decl);

	  if (TREE_CODE (fns) == COMPONENT_REF)
	    {
	      /* Due to bison parser ickiness, we will have already looked
		 up an operator_name or PFUNCNAME within the current class
		 (see template_id in parse.y). If the current class contains
		 such a name, we'll get a COMPONENT_REF here. Undo that.  */

	      gcc_assert (TREE_TYPE (TREE_OPERAND (fns, 0))
			  == current_class_type);
	      fns = TREE_OPERAND (fns, 1);
	    }
	  gcc_assert (TREE_CODE (fns) == IDENTIFIER_NODE
		      || TREE_CODE (fns) == OVERLOAD);
	  DECL_TEMPLATE_INFO (decl) = tree_cons (fns, args, NULL_TREE);

	  for (t = TYPE_ARG_TYPES (TREE_TYPE (decl)); t; t = TREE_CHAIN (t))
	    if (TREE_PURPOSE (t)
		&& TREE_CODE (TREE_PURPOSE (t)) == DEFAULT_ARG)
	    {
	      error ("default arguments are not allowed in declaration "
		     "of friend template specialization %qD",
		     decl);
	      return NULL_TREE;
	    }

	  if (inlinep)
	    {
	      error ("%<inline%> is not allowed in declaration of friend "
		     "template specialization %qD",
		     decl);
	      return NULL_TREE;
	    }
	}
    }

  /* If this decl has namespace scope, set that up.  */
  if (in_namespace)
    set_decl_namespace (decl, in_namespace, friendp);
  else if (!ctype)
    DECL_CONTEXT (decl) = FROB_CONTEXT (current_namespace);

  /* `main' and builtins have implicit 'C' linkage.  */
  if ((MAIN_NAME_P (declarator)
       || (IDENTIFIER_LENGTH (declarator) > 10
	   && IDENTIFIER_POINTER (declarator)[0] == '_'
	   && IDENTIFIER_POINTER (declarator)[1] == '_'
	   && strncmp (IDENTIFIER_POINTER (declarator)+2, "builtin_", 8) == 0))
      && current_lang_name == lang_name_cplusplus
      && ctype == NULL_TREE
      /* NULL_TREE means global namespace.  */
      && DECL_CONTEXT (decl) == NULL_TREE)
    SET_DECL_LANGUAGE (decl, lang_c);

  /* Should probably propagate const out from type to decl I bet (mrs).  */
  if (staticp)
    {
      DECL_STATIC_FUNCTION_P (decl) = 1;
      DECL_CONTEXT (decl) = ctype;
    }

  if (ctype)
    {
      DECL_CONTEXT (decl) = ctype;
      if (funcdef_flag)
	check_class_member_definition_namespace (decl);
    }

  if (ctype == NULL_TREE && DECL_MAIN_P (decl))
    {
      if (processing_template_decl)
	error ("cannot declare %<::main%> to be a template");
      if (inlinep)
	error ("cannot declare %<::main%> to be inline");
      if (!publicp)
	error ("cannot declare %<::main%> to be static");
      inlinep = 0;
      publicp = 1;
    }

  /* Members of anonymous types and local classes have no linkage; make
     them internal.  If a typedef is made later, this will be changed.  */
  if (ctype && (TYPE_ANONYMOUS_P (ctype)
		|| decl_function_context (TYPE_MAIN_DECL (ctype))))
    publicp = 0;

  if (publicp)
    {
      /* [basic.link]: A name with no linkage (notably, the name of a class
	 or enumeration declared in a local scope) shall not be used to
	 declare an entity with linkage.

	 Only check this for public decls for now.  See core 319, 389.  */
      t = no_linkage_check (TREE_TYPE (decl),
			    /*relaxed_p=*/false);
      if (t)
	{
	  if (TYPE_ANONYMOUS_P (t))
	    {
	      if (DECL_EXTERN_C_P (decl))
		/* Allow this; it's pretty common in C.  */;
	      else
		{
		  pedwarn ("non-local function %q#D uses anonymous type",
			      decl);
		  if (DECL_ORIGINAL_TYPE (TYPE_NAME (t)))
		    pedwarn ("%q+#D does not refer to the unqualified "
			     "type, so it is not used for linkage",
			     TYPE_NAME (t));
		}
	    }
	  else
	    pedwarn ("non-local function %q#D uses local type %qT", decl, t);
	}
    }

  TREE_PUBLIC (decl) = publicp;
  if (! publicp)
    {
      DECL_INTERFACE_KNOWN (decl) = 1;
      DECL_NOT_REALLY_EXTERN (decl) = 1;
    }

  /* If the declaration was declared inline, mark it as such.  */
  if (inlinep)
    DECL_DECLARED_INLINE_P (decl) = 1;
  /* We inline functions that are explicitly declared inline, or, when
     the user explicitly asks us to, all functions.  */
  if (DECL_DECLARED_INLINE_P (decl)
      || (flag_inline_trees == 2 && !DECL_INLINE (decl) && funcdef_flag))
    DECL_INLINE (decl) = 1;

  DECL_EXTERNAL (decl) = 1;
  if (quals && TREE_CODE (type) == FUNCTION_TYPE)
    {
      error ("%smember function %qD cannot have cv-qualifier",
	     (ctype ? "static " : "non-"), decl);
      quals = TYPE_UNQUALIFIED;
    }

  if (IDENTIFIER_OPNAME_P (DECL_NAME (decl))
      && !grok_op_properties (decl, /*complain=*/true))
    return NULL_TREE;

  if (ctype && decl_function_context (decl))
    DECL_NO_STATIC_CHAIN (decl) = 1;

  if (funcdef_flag)
    /* Make the init_value nonzero so pushdecl knows this is not
       tentative.  error_mark_node is replaced later with the BLOCK.  */
    DECL_INITIAL (decl) = error_mark_node;

  if (TYPE_NOTHROW_P (type) || nothrow_libfn_p (decl))
    TREE_NOTHROW (decl) = 1;

  /* Caller will do the rest of this.  */
  if (check < 0)
    return decl;

  if (ctype != NULL_TREE)
    {
      if (sfk == sfk_constructor)
	DECL_CONSTRUCTOR_P (decl) = 1;

      grokclassfn (ctype, decl, flags);
    }

  decl = check_explicit_specialization (orig_declarator, decl,
					template_count,
					2 * funcdef_flag +
					4 * (friendp != 0));
  if (decl == error_mark_node)
    return NULL_TREE;

  if (attrlist)
    {
      cplus_decl_attributes (&decl, *attrlist, 0);
      *attrlist = NULL_TREE;
    }

  /* Check main's type after attributes have been applied.  */
  if (ctype == NULL_TREE && DECL_MAIN_P (decl)
      && !same_type_p (TREE_TYPE (TREE_TYPE (decl)),
		       integer_type_node))
    {
      tree oldtypeargs = TYPE_ARG_TYPES (TREE_TYPE (decl));
      tree newtype;
      error ("%<::main%> must return %<int%>");
      newtype = build_function_type (integer_type_node, oldtypeargs);
      TREE_TYPE (decl) = newtype;
    }

  if (ctype != NULL_TREE
      && (! TYPE_FOR_JAVA (ctype) || check_java_method (decl))
      && check)
    {
      tree old_decl;

      old_decl = check_classfn (ctype, decl,
				(processing_template_decl
				 > template_class_depth (ctype))
				? current_template_parms
				: NULL_TREE);
      if (old_decl)
	{
	  tree ok;
	  tree pushed_scope;

	  if (TREE_CODE (old_decl) == TEMPLATE_DECL)
	    /* Because grokfndecl is always supposed to return a
	       FUNCTION_DECL, we pull out the DECL_TEMPLATE_RESULT
	       here.  We depend on our callers to figure out that its
	       really a template that's being returned.  */
	    old_decl = DECL_TEMPLATE_RESULT (old_decl);

	  if (DECL_STATIC_FUNCTION_P (old_decl)
	      && TREE_CODE (TREE_TYPE (decl)) == METHOD_TYPE)
	    /* Remove the `this' parm added by grokclassfn.
	       XXX Isn't this done in start_function, too?  */
	    revert_static_member_fn (decl);
	  if (DECL_ARTIFICIAL (old_decl))
	    error ("definition of implicitly-declared %qD", old_decl);

	  /* Since we've smashed OLD_DECL to its
	     DECL_TEMPLATE_RESULT, we must do the same to DECL.  */
	  if (TREE_CODE (decl) == TEMPLATE_DECL)
	    decl = DECL_TEMPLATE_RESULT (decl);

	  /* Attempt to merge the declarations.  This can fail, in
	     the case of some invalid specialization declarations.  */
	  pushed_scope = push_scope (ctype);
	  ok = duplicate_decls (decl, old_decl, friendp);
	  if (pushed_scope)
	    pop_scope (pushed_scope);
	  if (!ok)
	    {
	      error ("no %q#D member function declared in class %qT",
		     decl, ctype);
	      return NULL_TREE;
	    }
	  return old_decl;
	}
    }

  if (DECL_CONSTRUCTOR_P (decl) && !grok_ctor_properties (ctype, decl))
    return NULL_TREE;

  if (ctype == NULL_TREE || check)
    return decl;

  if (virtualp)
    DECL_VIRTUAL_P (decl) = 1;

  return decl;
}

/* DECL is a VAR_DECL for a static data member.  Set flags to reflect
   the linkage that DECL will receive in the object file.  */

static void
set_linkage_for_static_data_member (tree decl)
{
  /* A static data member always has static storage duration and
     external linkage.  Note that static data members are forbidden in
     local classes -- the only situation in which a class has
     non-external linkage.  */
  TREE_PUBLIC (decl) = 1;
  TREE_STATIC (decl) = 1;
  /* For non-template classes, static data members are always put
     out in exactly those files where they are defined, just as
     with ordinary namespace-scope variables.  */
  if (!processing_template_decl)
    DECL_INTERFACE_KNOWN (decl) = 1;
}

/* Create a VAR_DECL named NAME with the indicated TYPE.

   If SCOPE is non-NULL, it is the class type or namespace containing
   the variable.  If SCOPE is NULL, the variable should is created in
   the innermost enclosings scope.  */

static tree
grokvardecl (tree type,
	     tree name,
	     const cp_decl_specifier_seq *declspecs,
	     int initialized,
	     int constp,
	     tree scope)
{
  tree decl;
  tree explicit_scope;

  gcc_assert (!name || TREE_CODE (name) == IDENTIFIER_NODE);

  /* Compute the scope in which to place the variable, but remember
     whether or not that scope was explicitly specified by the user.   */
  explicit_scope = scope;
  if (!scope)
    {
      /* An explicit "extern" specifier indicates a namespace-scope
	 variable.  */
      if (declspecs->storage_class == sc_extern)
	scope = current_namespace;
      else if (!at_function_scope_p ())
	scope = current_scope ();
    }

  if (scope
      && (/* If the variable is a namespace-scope variable declared in a
	     template, we need DECL_LANG_SPECIFIC.  */
	  (TREE_CODE (scope) == NAMESPACE_DECL && processing_template_decl)
	  /* Similarly for namespace-scope variables with language linkage
	     other than C++.  */
	  || (TREE_CODE (scope) == NAMESPACE_DECL
	      && current_lang_name != lang_name_cplusplus)
	  /* Similarly for static data members.  */
	  || TYPE_P (scope)))
    decl = build_lang_decl (VAR_DECL, name, type);
  else
    decl = build_decl (VAR_DECL, name, type);

  if (explicit_scope && TREE_CODE (explicit_scope) == NAMESPACE_DECL)
    set_decl_namespace (decl, explicit_scope, 0);
  else
    DECL_CONTEXT (decl) = FROB_CONTEXT (scope);

  if (declspecs->storage_class == sc_extern)
    {
      DECL_THIS_EXTERN (decl) = 1;
      DECL_EXTERNAL (decl) = !initialized;
    }

  if (DECL_CLASS_SCOPE_P (decl))
    {
      set_linkage_for_static_data_member (decl);
      /* This function is only called with out-of-class definitions.  */
      DECL_EXTERNAL (decl) = 0;
      check_class_member_definition_namespace (decl);
    }
  /* At top level, either `static' or no s.c. makes a definition
     (perhaps tentative), and absence of `static' makes it public.  */
  else if (toplevel_bindings_p ())
    {
      TREE_PUBLIC (decl) = (declspecs->storage_class != sc_static
			    && (DECL_THIS_EXTERN (decl) || ! constp));
      TREE_STATIC (decl) = ! DECL_EXTERNAL (decl);
    }
  /* Not at top level, only `static' makes a static definition.  */
  else
    {
      TREE_STATIC (decl) = declspecs->storage_class == sc_static;
      TREE_PUBLIC (decl) = DECL_EXTERNAL (decl);
    }

  if (declspecs->specs[(int)ds_thread])
    {
      if (targetm.have_tls)
	DECL_TLS_MODEL (decl) = decl_default_tls_model (decl);
      else
	/* A mere warning is sure to result in improper semantics
	   at runtime.  Don't bother to allow this to compile.  */
	error ("thread-local storage not supported for this target");
    }

  if (TREE_PUBLIC (decl))
    {
      /* [basic.link]: A name with no linkage (notably, the name of a class
	 or enumeration declared in a local scope) shall not be used to
	 declare an entity with linkage.

	 Only check this for public decls for now.  */
      tree t = no_linkage_check (TREE_TYPE (decl), /*relaxed_p=*/false);
      if (t)
	{
	  if (TYPE_ANONYMOUS_P (t))
	    {
	      if (DECL_EXTERN_C_P (decl))
		/* Allow this; it's pretty common in C.  */
		  ;
	      else
		{
		  /* DRs 132, 319 and 389 seem to indicate types with
		     no linkage can only be used to declare extern "C"
		     entities.  Since it's not always an error in the
		     ISO C++ 90 Standard, we only issue a warning.  */
		  warning (0, "non-local variable %q#D uses anonymous type",
			   decl);
		  if (DECL_ORIGINAL_TYPE (TYPE_NAME (t)))
		    warning (0, "%q+#D does not refer to the unqualified "
			     "type, so it is not used for linkage",
			     TYPE_NAME (t));
		}
	    }
	  else
	    warning (0, "non-local variable %q#D uses local type %qT", decl, t);
	}
    }
  else
    DECL_INTERFACE_KNOWN (decl) = 1;

  return decl;
}

/* Create and return a canonical pointer to member function type, for
   TYPE, which is a POINTER_TYPE to a METHOD_TYPE.  */

tree
build_ptrmemfunc_type (tree type)
{
  tree field, fields;
  tree t;
  tree unqualified_variant = NULL_TREE;

  if (type == error_mark_node)
    return type;

  /* If a canonical type already exists for this type, use it.  We use
     this method instead of type_hash_canon, because it only does a
     simple equality check on the list of field members.  */

  if ((t = TYPE_GET_PTRMEMFUNC_TYPE (type)))
    return t;

  /* Make sure that we always have the unqualified pointer-to-member
     type first.  */
  if (cp_type_quals (type) != TYPE_UNQUALIFIED)
    unqualified_variant
      = build_ptrmemfunc_type (TYPE_MAIN_VARIANT (type));

  t = make_aggr_type (RECORD_TYPE);
  xref_basetypes (t, NULL_TREE);

  /* Let the front-end know this is a pointer to member function...  */
  TYPE_PTRMEMFUNC_FLAG (t) = 1;
  /* ... and not really an aggregate.  */
  SET_IS_AGGR_TYPE (t, 0);

  field = build_decl (FIELD_DECL, pfn_identifier, type);
  fields = field;

  field = build_decl (FIELD_DECL, delta_identifier, delta_type_node);
  TREE_CHAIN (field) = fields;
  fields = field;

  finish_builtin_struct (t, "__ptrmemfunc_type", fields, ptr_type_node);

  /* Zap out the name so that the back-end will give us the debugging
     information for this anonymous RECORD_TYPE.  */
  TYPE_NAME (t) = NULL_TREE;

  /* If this is not the unqualified form of this pointer-to-member
     type, set the TYPE_MAIN_VARIANT for this type to be the
     unqualified type.  Since they are actually RECORD_TYPEs that are
     not variants of each other, we must do this manually.  */
  if (cp_type_quals (type) != TYPE_UNQUALIFIED)
    {
      t = build_qualified_type (t, cp_type_quals (type));
      TYPE_MAIN_VARIANT (t) = unqualified_variant;
      TYPE_NEXT_VARIANT (t) = TYPE_NEXT_VARIANT (unqualified_variant);
      TYPE_NEXT_VARIANT (unqualified_variant) = t;
    }

  /* Cache this pointer-to-member type so that we can find it again
     later.  */
  TYPE_SET_PTRMEMFUNC_TYPE (type, t);

  return t;
}

/* Create and return a pointer to data member type.  */

tree
build_ptrmem_type (tree class_type, tree member_type)
{
  if (TREE_CODE (member_type) == METHOD_TYPE)
    {
      tree arg_types;

      arg_types = TYPE_ARG_TYPES (member_type);
      class_type = (cp_build_qualified_type
		    (class_type,
		     cp_type_quals (TREE_TYPE (TREE_VALUE (arg_types)))));
      member_type
	= build_method_type_directly (class_type,
				      TREE_TYPE (member_type),
				      TREE_CHAIN (arg_types));
      return build_ptrmemfunc_type (build_pointer_type (member_type));
    }
  else
    {
      gcc_assert (TREE_CODE (member_type) != FUNCTION_TYPE);
      return build_offset_type (class_type, member_type);
    }
}

/* DECL is a VAR_DECL defined in-class, whose TYPE is also given.
   Check to see that the definition is valid.  Issue appropriate error
   messages.  Return 1 if the definition is particularly bad, or 0
   otherwise.  */

int
check_static_variable_definition (tree decl, tree type)
{
  /* Motion 10 at San Diego: If a static const integral data member is
     initialized with an integral constant expression, the initializer
     may appear either in the declaration (within the class), or in
     the definition, but not both.  If it appears in the class, the
     member is a member constant.  The file-scope definition is always
     required.  */
  if (!ARITHMETIC_TYPE_P (type) && TREE_CODE (type) != ENUMERAL_TYPE)
    {
      error ("invalid in-class initialization of static data member "
	     "of non-integral type %qT",
	     type);
      /* If we just return the declaration, crashes will sometimes
	 occur.  We therefore return void_type_node, as if this were a
	 friend declaration, to cause callers to completely ignore
	 this declaration.  */
      return 1;
    }
  else if (!CP_TYPE_CONST_P (type))
    error ("ISO C++ forbids in-class initialization of non-const "
	   "static member %qD",
	   decl);
  else if (pedantic && !INTEGRAL_TYPE_P (type))
    pedwarn ("ISO C++ forbids initialization of member constant "
	     "%qD of non-integral type %qT", decl, type);

  return 0;
}

/* Given the SIZE (i.e., number of elements) in an array, compute an
   appropriate index type for the array.  If non-NULL, NAME is the
   name of the thing being declared.  */

tree
compute_array_index_type (tree name, tree size)
{
  tree type;
  tree itype;

  if (error_operand_p (size))
    return error_mark_node;

  type = TREE_TYPE (size);
  /* The array bound must be an integer type.  */
  if (!dependent_type_p (type) && !INTEGRAL_TYPE_P (type))
    {
      if (name)
	error ("size of array %qD has non-integral type %qT", name, type);
      else
	error ("size of array has non-integral type %qT", type);
      size = integer_one_node;
      type = TREE_TYPE (size);
    }

  if (abi_version_at_least (2)
      /* We should only handle value dependent expressions specially.  */
      ? value_dependent_expression_p (size)
      /* But for abi-1, we handled all instances in templates. This
	 effects the manglings produced.  */
      : processing_template_decl)
    return build_index_type (build_min (MINUS_EXPR, sizetype,
					size, integer_one_node));

  /* The size might be the result of a cast.  */
  STRIP_TYPE_NOPS (size);

  /* It might be a const variable or enumeration constant.  */
  size = integral_constant_value (size);

  /* Normally, the array-bound will be a constant.  */
  if (TREE_CODE (size) == INTEGER_CST)
    {
      /* Check to see if the array bound overflowed.  Make that an
	 error, no matter how generous we're being.  */
      int old_flag_pedantic_errors = flag_pedantic_errors;
      int old_pedantic = pedantic;
      pedantic = flag_pedantic_errors = 1;
      constant_expression_warning (size);
      pedantic = old_pedantic;
      flag_pedantic_errors = old_flag_pedantic_errors;

      /* An array must have a positive number of elements.  */
      if (INT_CST_LT (size, integer_zero_node))
	{
	  if (name)
	    error ("size of array %qD is negative", name);
	  else
	    error ("size of array is negative");
	  size = integer_one_node;
	}
      /* As an extension we allow zero-sized arrays.  We always allow
	 them in system headers because glibc uses them.  */
      else if (integer_zerop (size) && pedantic && !in_system_header)
	{
	  if (name)
	    pedwarn ("ISO C++ forbids zero-size array %qD", name);
	  else
	    pedwarn ("ISO C++ forbids zero-size array");
	}
    }
  else if (TREE_CONSTANT (size))
    {
      /* `(int) &fn' is not a valid array bound.  */
      if (name)
	error ("size of array %qD is not an integral constant-expression",
	       name);
      else
	error ("size of array is not an integral constant-expression");
      size = integer_one_node;
    }
  else if (pedantic)
    {
      if (name)
	pedwarn ("ISO C++ forbids variable-size array %qD", name);
      else
	pedwarn ("ISO C++ forbids variable-size array");
    }

  if (processing_template_decl && !TREE_CONSTANT (size))
    /* A variable sized array.  */
    itype = build_min (MINUS_EXPR, sizetype, size, integer_one_node);
  else
    {
      HOST_WIDE_INT saved_processing_template_decl;

      /* Compute the index of the largest element in the array.  It is
	 one less than the number of elements in the array.  We save
	 and restore PROCESSING_TEMPLATE_DECL so that computations in
	 cp_build_binary_op will be appropriately folded.  */
      saved_processing_template_decl = processing_template_decl;
      processing_template_decl = 0;
      itype = cp_build_binary_op (MINUS_EXPR,
				  cp_convert (ssizetype, size),
				  cp_convert (ssizetype, integer_one_node));
      itype = fold (itype);
      processing_template_decl = saved_processing_template_decl;

      if (!TREE_CONSTANT (itype))
	/* A variable sized array.  */
	itype = variable_size (itype);
      /* Make sure that there was no overflow when creating to a signed
	 index type.  (For example, on a 32-bit machine, an array with
	 size 2^32 - 1 is too big.)  */
      else if (TREE_CODE (itype) == INTEGER_CST
	       && TREE_OVERFLOW (itype))
	{
	  error ("overflow in array dimension");
	  TREE_OVERFLOW (itype) = 0;
	}
    }

  /* Create and return the appropriate index type.  */
  return build_index_type (itype);
}

/* Returns the scope (if any) in which the entity declared by
   DECLARATOR will be located.  If the entity was declared with an
   unqualified name, NULL_TREE is returned.  */

tree
get_scope_of_declarator (const cp_declarator *declarator)
{
  while (declarator && declarator->kind != cdk_id)
    declarator = declarator->declarator;

  /* If the declarator-id is a SCOPE_REF, the scope in which the
     declaration occurs is the first operand.  */
  if (declarator
      && declarator->u.id.qualifying_scope)
    return declarator->u.id.qualifying_scope;

  /* Otherwise, the declarator is not a qualified name; the entity will
     be declared in the current scope.  */
  return NULL_TREE;
}

/* Returns an ARRAY_TYPE for an array with SIZE elements of the
   indicated TYPE.  If non-NULL, NAME is the NAME of the declaration
   with this type.  */

static tree
create_array_type_for_decl (tree name, tree type, tree size)
{
  tree itype = NULL_TREE;
  const char* error_msg;

  /* If things have already gone awry, bail now.  */
  if (type == error_mark_node || size == error_mark_node)
    return error_mark_node;

  /* Assume that everything will go OK.  */
  error_msg = NULL;

  /* There are some types which cannot be array elements.  */
  switch (TREE_CODE (type))
    {
    case VOID_TYPE:
      error_msg = "array of void";
      break;

    case FUNCTION_TYPE:
      error_msg = "array of functions";
      break;

    case REFERENCE_TYPE:
      error_msg = "array of references";
      break;

    case METHOD_TYPE:
      error_msg = "array of function members";
      break;

    default:
      break;
    }

  /* If something went wrong, issue an error-message and return.  */
  if (error_msg)
    {
      if (name)
	error ("declaration of %qD as %s", name, error_msg);
      else
	error ("creating %s", error_msg);

      return error_mark_node;
    }

  /* [dcl.array]

     The constant expressions that specify the bounds of the arrays
     can be omitted only for the first member of the sequence.  */
  if (TREE_CODE (type) == ARRAY_TYPE && !TYPE_DOMAIN (type))
    {
      if (name)
	error ("declaration of %qD as multidimensional array must "
	       "have bounds for all dimensions except the first",
	       name);
      else
	error ("multidimensional array must have bounds for all "
	       "dimensions except the first");

      return error_mark_node;
    }

  /* Figure out the index type for the array.  */
  if (size)
    itype = compute_array_index_type (name, size);

  /* [dcl.array]
     T is called the array element type; this type shall not be [...] an
     abstract class type.  */
  abstract_virtuals_error (name, type);

  return build_cplus_array_type (type, itype);
}

/* Check that it's OK to declare a function with the indicated TYPE.
   SFK indicates the kind of special function (if any) that this
   function is.  OPTYPE is the type given in a conversion operator
   declaration, or the class type for a constructor/destructor.
   Returns the actual return type of the function; that
   may be different than TYPE if an error occurs, or for certain
   special functions.  */

static tree
check_special_function_return_type (special_function_kind sfk,
				    tree type,
				    tree optype)
{
  switch (sfk)
    {
    case sfk_constructor:
      if (type)
	error ("return type specification for constructor invalid");

      if (targetm.cxx.cdtor_returns_this () && !TYPE_FOR_JAVA (optype))
	type = build_pointer_type (optype);
      else
	type = void_type_node;
      break;

    case sfk_destructor:
      if (type)
	error ("return type specification for destructor invalid");
      /* We can't use the proper return type here because we run into
	 problems with ambiguous bases and covariant returns.
	 Java classes are left unchanged because (void *) isn't a valid
	 Java type, and we don't want to change the Java ABI.  */
      if (targetm.cxx.cdtor_returns_this () && !TYPE_FOR_JAVA (optype))
	type = build_pointer_type (void_type_node);
      else
	type = void_type_node;
      break;

    case sfk_conversion:
      if (type && !same_type_p (type, optype))
	error ("operator %qT declared to return %qT", optype, type);
      else if (type)
	pedwarn ("return type specified for %<operator %T%>",  optype);
      type = optype;
      break;

    default:
      gcc_unreachable ();
    }

  return type;
}

/* A variable or data member (whose unqualified name is IDENTIFIER)
   has been declared with the indicated TYPE.  If the TYPE is not
   acceptable, issue an error message and return a type to use for
   error-recovery purposes.  */

tree
check_var_type (tree identifier, tree type)
{
  if (VOID_TYPE_P (type))
    {
      if (!identifier)
	error ("unnamed variable or field declared void");
      else if (TREE_CODE (identifier) == IDENTIFIER_NODE)
	{
	  gcc_assert (!IDENTIFIER_OPNAME_P (identifier));
	  error ("variable or field %qE declared void", identifier);
	}
      else
	error ("variable or field declared void");
      type = error_mark_node;
    }

  return type;
}

/* Given declspecs and a declarator (abstract or otherwise), determine
   the name and type of the object declared and construct a DECL node
   for it.

   DECLSPECS is a chain of tree_list nodes whose value fields
    are the storage classes and type specifiers.

   DECL_CONTEXT says which syntactic context this declaration is in:
     NORMAL for most contexts.  Make a VAR_DECL or FUNCTION_DECL or TYPE_DECL.
     FUNCDEF for a function definition.  Like NORMAL but a few different
      error messages in each case.  Return value may be zero meaning
      this definition is too screwy to try to parse.
     MEMFUNCDEF for a function definition.  Like FUNCDEF but prepares to
      handle member functions (which have FIELD context).
      Return value may be zero meaning this definition is too screwy to
      try to parse.
     PARM for a parameter declaration (either within a function prototype
      or before a function body).  Make a PARM_DECL, or return void_type_node.
     CATCHPARM for a parameter declaration before a catch clause.
     TYPENAME if for a typename (in a cast or sizeof).
      Don't make a DECL node; just return the ..._TYPE node.
     FIELD for a struct or union field; make a FIELD_DECL.
     BITFIELD for a field with specified width.
   INITIALIZED is 1 if the decl has an initializer.

   ATTRLIST is a pointer to the list of attributes, which may be NULL
   if there are none; *ATTRLIST may be modified if attributes from inside
   the declarator should be applied to the declaration.

   When this function is called, scoping variables (such as
   CURRENT_CLASS_TYPE) should reflect the scope in which the
   declaration occurs, not the scope in which the new declaration will
   be placed.  For example, on:

     void S::f() { ... }

   when grokdeclarator is called for `S::f', the CURRENT_CLASS_TYPE
   should not be `S'.

   Returns a DECL (if a declarator is present), a TYPE (if there is no
   declarator, in cases like "struct S;"), or the ERROR_MARK_NODE if an
   error occurs. */

tree
grokdeclarator (const cp_declarator *declarator,
		const cp_decl_specifier_seq *declspecs,
		enum decl_context decl_context,
		int initialized,
		tree* attrlist)
{
  tree type = NULL_TREE;
  int longlong = 0;
  int virtualp, explicitp, friendp, inlinep, staticp;
  int explicit_int = 0;
  int explicit_char = 0;
  int defaulted_int = 0;
  tree dependent_name = NULL_TREE;

  tree typedef_decl = NULL_TREE;
  const char *name = NULL;
  tree typedef_type = NULL_TREE;
  /* True if this declarator is a function definition.  */
  bool funcdef_flag = false;
  cp_declarator_kind innermost_code = cdk_error;
  int bitfield = 0;
#if 0
  /* See the code below that used this.  */
  tree decl_attr = NULL_TREE;
#endif

  /* Keep track of what sort of function is being processed
     so that we can warn about default return values, or explicit
     return values which do not match prescribed defaults.  */
  special_function_kind sfk = sfk_none;

  tree dname = NULL_TREE;
  tree ctor_return_type = NULL_TREE;
  enum overload_flags flags = NO_SPECIAL;
  /* cv-qualifiers that apply to the declarator, for a declaration of
     a member function.  */
  cp_cv_quals memfn_quals = TYPE_UNQUALIFIED;
  /* cv-qualifiers that apply to the type specified by the DECLSPECS.  */
  int type_quals;
  tree raises = NULL_TREE;
  int template_count = 0;
  tree returned_attrs = NULL_TREE;
  tree parms = NULL_TREE;
  const cp_declarator *id_declarator;
  /* The unqualified name of the declarator; either an
     IDENTIFIER_NODE, BIT_NOT_EXPR, or TEMPLATE_ID_EXPR.  */
  tree unqualified_id;
  /* The class type, if any, in which this entity is located,
     or NULL_TREE if none.  Note that this value may be different from
     the current class type; for example if an attempt is made to declare
     "A::f" inside "B", this value will be "A".  */
  tree ctype = current_class_type;
  /* The NAMESPACE_DECL for the namespace in which this entity is
     located.  If an unqualified name is used to declare the entity,
     this value will be NULL_TREE, even if the entity is located at
     namespace scope.  */
  tree in_namespace = NULL_TREE;
  cp_storage_class storage_class;
  bool unsigned_p, signed_p, short_p, long_p, thread_p;
  bool type_was_error_mark_node = false;

  signed_p = declspecs->specs[(int)ds_signed];
  unsigned_p = declspecs->specs[(int)ds_unsigned];
  short_p = declspecs->specs[(int)ds_short];
  long_p = declspecs->specs[(int)ds_long];
  longlong = declspecs->specs[(int)ds_long] >= 2;
  thread_p = declspecs->specs[(int)ds_thread];

  if (decl_context == FUNCDEF)
    funcdef_flag = true, decl_context = NORMAL;
  else if (decl_context == MEMFUNCDEF)
    funcdef_flag = true, decl_context = FIELD;
  else if (decl_context == BITFIELD)
    bitfield = 1, decl_context = FIELD;

  /* Look inside a declarator for the name being declared
     and get it as a string, for an error message.  */
  for (id_declarator = declarator;
       id_declarator;
       id_declarator = id_declarator->declarator)
    {
      if (id_declarator->kind != cdk_id)
	innermost_code = id_declarator->kind;

      switch (id_declarator->kind)
	{
	case cdk_function:
	  if (id_declarator->declarator
	      && id_declarator->declarator->kind == cdk_id)
	    {
	      sfk = id_declarator->declarator->u.id.sfk;
	      if (sfk == sfk_destructor)
		flags = DTOR_FLAG;
	    }
	  break;

	case cdk_id:
	  {
	    tree qualifying_scope = id_declarator->u.id.qualifying_scope;
	    tree decl = id_declarator->u.id.unqualified_name;
	    if (!decl)
	      break;
	    if (qualifying_scope)
	      {
		if (at_function_scope_p ())
		  {
		    /* [dcl.meaning] 

		       A declarator-id shall not be qualified except
		       for ... 

		       None of the cases are permitted in block
		       scope.  */
		    if (qualifying_scope == global_namespace)
		      error ("invalid use of qualified-name %<::%D%>",
			     decl);
		    else if (TYPE_P (qualifying_scope))
		      error ("invalid use of qualified-name %<%T::%D%>",
			     qualifying_scope, decl);
		    else 
		      error ("invalid use of qualified-name %<%D::%D%>",
			     qualifying_scope, decl);
		    return error_mark_node;
		  }
		else if (TYPE_P (qualifying_scope))
		  {
		    ctype = qualifying_scope;
		    if (innermost_code != cdk_function
			&& current_class_type
			&& !UNIQUELY_DERIVED_FROM_P (ctype,
						     current_class_type))
		      {
			error ("type %qT is not derived from type %qT",
			       ctype, current_class_type);
			return error_mark_node;
		      }
		  }
		else if (TREE_CODE (qualifying_scope) == NAMESPACE_DECL)
		  in_namespace = qualifying_scope;
	      }
	    switch (TREE_CODE (decl))
	      {
	      case BIT_NOT_EXPR:
		{
		  tree type;

		  if (innermost_code != cdk_function)
		    {
		      error ("declaration of %qD as non-function", decl);
		      return error_mark_node;
		    }
		  else if (!qualifying_scope
			   && !(current_class_type && at_class_scope_p ()))
		    {
		      error ("declaration of %qD as non-member", decl);
		      return error_mark_node;
		    }

		  type = TREE_OPERAND (decl, 0);
		  name = IDENTIFIER_POINTER (constructor_name (type));
		  dname = decl;
		}
		break;

	      case TEMPLATE_ID_EXPR:
		{
		  tree fns = TREE_OPERAND (decl, 0);

		  dname = fns;
		  if (TREE_CODE (dname) != IDENTIFIER_NODE)
		    {
		      gcc_assert (is_overloaded_fn (dname));
		      dname = DECL_NAME (get_first_fn (dname));
		    }
		}
		/* Fall through.  */

	      case IDENTIFIER_NODE:
		if (TREE_CODE (decl) == IDENTIFIER_NODE)
		  dname = decl;

		if (C_IS_RESERVED_WORD (dname))
		  {
		    error ("declarator-id missing; using reserved word %qD",
			   dname);
		    name = IDENTIFIER_POINTER (dname);
		  }
		else if (!IDENTIFIER_TYPENAME_P (dname))
		  name = IDENTIFIER_POINTER (dname);
		else
		  {
		    gcc_assert (flags == NO_SPECIAL);
		    flags = TYPENAME_FLAG;
		    ctor_return_type = TREE_TYPE (dname);
		    sfk = sfk_conversion;
		    if (is_typename_at_global_scope (dname))
		      name = IDENTIFIER_POINTER (dname);
		    else
		      name = "<invalid operator>";
		  }
		break;

	      default:
		gcc_unreachable ();
	      }
	    break;

	  case cdk_array:
	  case cdk_pointer:
	  case cdk_reference:
	  case cdk_ptrmem:
	    break;

	  case cdk_error:
	    return error_mark_node;

	  default:
	    gcc_unreachable ();
	  }
	}
      if (id_declarator->kind == cdk_id)
	break;
    }

  /* [dcl.fct.edf]

     The declarator in a function-definition shall have the form
     D1 ( parameter-declaration-clause) ...  */
  if (funcdef_flag && innermost_code != cdk_function)
    {
      error ("function definition does not declare parameters");
      return error_mark_node;
    }

  if (((dname && IDENTIFIER_OPNAME_P (dname)) || flags == TYPENAME_FLAG)
      && innermost_code != cdk_function
      && ! (ctype && !declspecs->any_specifiers_p))
    {
      error ("declaration of %qD as non-function", dname);
      return error_mark_node;
    }

  /* Anything declared one level down from the top level
     must be one of the parameters of a function
     (because the body is at least two levels down).  */

  /* This heuristic cannot be applied to C++ nodes! Fixed, however,
     by not allowing C++ class definitions to specify their parameters
     with xdecls (must be spec.d in the parmlist).

     Since we now wait to push a class scope until we are sure that
     we are in a legitimate method context, we must set oldcname
     explicitly (since current_class_name is not yet alive).

     We also want to avoid calling this a PARM if it is in a namespace.  */

  if (decl_context == NORMAL && !toplevel_bindings_p ())
    {
      struct cp_binding_level *b = current_binding_level;
      current_binding_level = b->level_chain;
      if (current_binding_level != 0 && toplevel_bindings_p ())
	decl_context = PARM;
      current_binding_level = b;
    }

  if (name == NULL)
    name = decl_context == PARM ? "parameter" : "type name";

  /* If there were multiple types specified in the decl-specifier-seq,
     issue an error message.  */
  if (declspecs->multiple_types_p)
    {
      error ("two or more data types in declaration of %qs", name);
      return error_mark_node;
    }

  /* Extract the basic type from the decl-specifier-seq.  */
  type = declspecs->type;
  if (type == error_mark_node)
    {
      type = NULL_TREE;
      type_was_error_mark_node = true;
    }
  /* If the entire declaration is itself tagged as deprecated then
     suppress reports of deprecated items.  */
  if (type && TREE_DEPRECATED (type)
      && deprecated_state != DEPRECATED_SUPPRESS)
    warn_deprecated_use (type);
  if (type && TREE_CODE (type) == TYPE_DECL)
    {
      typedef_decl = type;
      type = TREE_TYPE (typedef_decl);
    }
  /* No type at all: default to `int', and set DEFAULTED_INT
     because it was not a user-defined typedef.  */
  if (type == NULL_TREE && (signed_p || unsigned_p || long_p || short_p))
    {
      /* These imply 'int'.  */
      type = integer_type_node;
      defaulted_int = 1;
    }
  /* Gather flags.  */
  explicit_int = declspecs->explicit_int_p;
  explicit_char = declspecs->explicit_char_p;

#if 0
  /* See the code below that used this.  */
  if (typedef_decl)
    decl_attr = DECL_ATTRIBUTES (typedef_decl);
#endif
  typedef_type = type;


  if (sfk != sfk_conversion)
    ctor_return_type = ctype;

  if (sfk != sfk_none)
    type = check_special_function_return_type (sfk, type,
					       ctor_return_type);
  else if (type == NULL_TREE)
    {
      int is_main;

      explicit_int = -1;

      /* We handle `main' specially here, because 'main () { }' is so
	 common.  With no options, it is allowed.  With -Wreturn-type,
	 it is a warning.  It is only an error with -pedantic-errors.  */
      is_main = (funcdef_flag
		 && dname && MAIN_NAME_P (dname)
		 && ctype == NULL_TREE
		 && in_namespace == NULL_TREE
		 && current_namespace == global_namespace);

      if (type_was_error_mark_node)
	/* We've already issued an error, don't complain more.  */;
      else if (in_system_header || flag_ms_extensions)
	/* Allow it, sigh.  */;
      else if (pedantic || ! is_main)
	pedwarn ("ISO C++ forbids declaration of %qs with no type", name);
      else if (warn_return_type)
	warning (0, "ISO C++ forbids declaration of %qs with no type", name);

      type = integer_type_node;
    }

  ctype = NULL_TREE;

  /* Now process the modifiers that were specified
     and check for invalid combinations.  */

  /* Long double is a special combination.  */
  if (long_p && !longlong && TYPE_MAIN_VARIANT (type) == double_type_node)
    {
      long_p = false;
      type = build_qualified_type (long_double_type_node,
				   cp_type_quals (type));
    }

  /* Check all other uses of type modifiers.  */

  if (unsigned_p || signed_p || long_p || short_p)
    {
      int ok = 0;

      if ((signed_p || unsigned_p) && TREE_CODE (type) != INTEGER_TYPE)
	error ("%<signed%> or %<unsigned%> invalid for %qs", name);
      else if (signed_p && unsigned_p)
	error ("%<signed%> and %<unsigned%> specified together for %qs", name);
      else if (longlong && TREE_CODE (type) != INTEGER_TYPE)
	error ("%<long long%> invalid for %qs", name);
      else if (long_p && TREE_CODE (type) == REAL_TYPE)
	error ("%<long%> invalid for %qs", name);
      else if (short_p && TREE_CODE (type) == REAL_TYPE)
	error ("%<short%> invalid for %qs", name);
      else if ((long_p || short_p) && TREE_CODE (type) != INTEGER_TYPE)
	error ("%<long%> or %<short%> invalid for %qs", name);
      else if ((long_p || short_p) && explicit_char)
	error ("%<long%> or %<short%> specified with char for %qs", name);
      else if (long_p && short_p)
	error ("%<long%> and %<short%> specified together for %qs", name);
      else
	{
	  ok = 1;
	  if (!explicit_int && !defaulted_int && !explicit_char && pedantic)
	    {
	      pedwarn ("long, short, signed or unsigned used invalidly for %qs",
		       name);
	      if (flag_pedantic_errors)
		ok = 0;
	    }
	}

      /* Discard the type modifiers if they are invalid.  */
      if (! ok)
	{
	  unsigned_p = false;
	  signed_p = false;
	  long_p = false;
	  short_p = false;
	  longlong = 0;
	}
    }

  /* Decide whether an integer type is signed or not.
     Optionally treat bitfields as signed by default.  */
  if (unsigned_p
      /* [class.bit]

	 It is implementation-defined whether a plain (neither
	 explicitly signed or unsigned) char, short, int, or long
	 bit-field is signed or unsigned.

	 Naturally, we extend this to long long as well.  Note that
	 this does not include wchar_t.  */
      || (bitfield && !flag_signed_bitfields
	  && !signed_p
	  /* A typedef for plain `int' without `signed' can be
	     controlled just like plain `int', but a typedef for
	     `signed int' cannot be so controlled.  */
	  && !(typedef_decl
	       && C_TYPEDEF_EXPLICITLY_SIGNED (typedef_decl))
	  && TREE_CODE (type) == INTEGER_TYPE
	  && !same_type_p (TYPE_MAIN_VARIANT (type), wchar_type_node)))
    {
      if (longlong)
	type = long_long_unsigned_type_node;
      else if (long_p)
	type = long_unsigned_type_node;
      else if (short_p)
	type = short_unsigned_type_node;
      else if (type == char_type_node)
	type = unsigned_char_type_node;
      else if (typedef_decl)
	type = c_common_unsigned_type (type);
      else
	type = unsigned_type_node;
    }
  else if (signed_p && type == char_type_node)
    type = signed_char_type_node;
  else if (longlong)
    type = long_long_integer_type_node;
  else if (long_p)
    type = long_integer_type_node;
  else if (short_p)
    type = short_integer_type_node;

  if (declspecs->specs[(int)ds_complex])
    {
      if (TREE_CODE (type) != INTEGER_TYPE && TREE_CODE (type) != REAL_TYPE)
	error ("complex invalid for %qs", name);
      /* If we just have "complex", it is equivalent to
	 "complex double", but if any modifiers at all are specified it is
	 the complex form of TYPE.  E.g, "complex short" is
	 "complex short int".  */

      else if (defaulted_int && ! longlong
	       && ! (long_p || short_p || signed_p || unsigned_p))
	type = complex_double_type_node;
      else if (type == integer_type_node)
	type = complex_integer_type_node;
      else if (type == float_type_node)
	type = complex_float_type_node;
      else if (type == double_type_node)
	type = complex_double_type_node;
      else if (type == long_double_type_node)
	type = complex_long_double_type_node;
      else
	type = build_complex_type (type);
    }

  type_quals = TYPE_UNQUALIFIED;
  if (declspecs->specs[(int)ds_const])
    type_quals |= TYPE_QUAL_CONST;
  if (declspecs->specs[(int)ds_volatile])
    type_quals |= TYPE_QUAL_VOLATILE;
  if (declspecs->specs[(int)ds_restrict])
    type_quals |= TYPE_QUAL_RESTRICT;
  if (sfk == sfk_conversion && type_quals != TYPE_UNQUALIFIED)
    error ("qualifiers are not allowed on declaration of %<operator %T%>",
	   ctor_return_type);

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
      type_quals = TYPE_UNQUALIFIED;
    }
  type_quals |= cp_type_quals (type);
  type = cp_build_qualified_type_real
    (type, type_quals, ((typedef_decl && !DECL_ARTIFICIAL (typedef_decl)
			 ? tf_ignore_bad_quals : 0) | tf_warning_or_error));
  /* We might have ignored or rejected some of the qualifiers.  */
  type_quals = cp_type_quals (type);

  staticp = 0;
  inlinep = !! declspecs->specs[(int)ds_inline];
  virtualp = !! declspecs->specs[(int)ds_virtual];
  explicitp = !! declspecs->specs[(int)ds_explicit];

  storage_class = declspecs->storage_class;
  if (storage_class == sc_static)
    staticp = 1 + (decl_context == FIELD);

  if (virtualp && staticp == 2)
    {
      error ("member %qD cannot be declared both virtual and static", dname);
      storage_class = sc_none;
      staticp = 0;
    }
  friendp = !! declspecs->specs[(int)ds_friend];

  if (dependent_name && !friendp)
    {
      error ("%<%T::%D%> is not a valid declarator", ctype, dependent_name);
      return error_mark_node;
    }

  /* Issue errors about use of storage classes for parameters.  */
  if (decl_context == PARM)
    {
      if (declspecs->specs[(int)ds_typedef])
	{
	  error ("typedef declaration invalid in parameter declaration");
	  return error_mark_node;
	}
      else if (storage_class == sc_static
	       || storage_class == sc_extern
	       || thread_p)
	error ("storage class specifiers invalid in parameter declarations");
    }

  /* Give error if `virtual' is used outside of class declaration.  */
  if (virtualp
      && (current_class_name == NULL_TREE || decl_context != FIELD))
    {
      error ("virtual outside class declaration");
      virtualp = 0;
    }

  /* Static anonymous unions are dealt with here.  */
  if (staticp && decl_context == TYPENAME
      && declspecs->type
      && ANON_AGGR_TYPE_P (declspecs->type))
    decl_context = FIELD;

  /* Warn about storage classes that are invalid for certain
     kinds of declarations (parameters, typenames, etc.).  */
  if (thread_p
      && ((storage_class
	   && storage_class != sc_extern
	   && storage_class != sc_static)
	  || declspecs->specs[(int)ds_typedef]))
    {
      error ("multiple storage classes in declaration of %qs", name);
      thread_p = false;
    }
  if (declspecs->conflicting_specifiers_p)
    {
      error ("conflicting specifiers in declaration of %qs", name);
      storage_class = sc_none;
    }
  else if (decl_context != NORMAL
	   && ((storage_class != sc_none
		&& storage_class != sc_mutable)
	       || thread_p))
    {
      if ((decl_context == PARM || decl_context == CATCHPARM)
	  && (storage_class == sc_register
	      || storage_class == sc_auto))
	;
      else if (declspecs->specs[(int)ds_typedef])
	;
      else if (decl_context == FIELD
	       /* C++ allows static class elements.  */
	       && storage_class == sc_static)
	/* C++ also allows inlines and signed and unsigned elements,
	   but in those cases we don't come in here.  */
	;
      else
	{
	  if (decl_context == FIELD)
	    error ("storage class specified for %qs", name);
	  else
	    {
	      if (decl_context == PARM || decl_context == CATCHPARM)
		error ("storage class specified for parameter %qs", name);
	      else
		error ("storage class specified for typename");
	    }
	  if (storage_class == sc_register
	      || storage_class == sc_auto
	      || storage_class == sc_extern
	      || thread_p)
	    storage_class = sc_none;
	}
    }
  else if (storage_class == sc_extern && initialized
	   && !funcdef_flag)
    {
      if (toplevel_bindings_p ())
	{
	  /* It's common practice (and completely valid) to have a const
	     be initialized and declared extern.  */
	  if (!(type_quals & TYPE_QUAL_CONST))
	    warning (0, "%qs initialized and declared %<extern%>", name);
	}
      else
	error ("%qs has both %<extern%> and initializer", name);
    }
  else if (storage_class == sc_extern && funcdef_flag
	   && ! toplevel_bindings_p ())
    error ("nested function %qs declared %<extern%>", name);
  else if (toplevel_bindings_p ())
    {
      if (storage_class == sc_auto)
	error ("top-level declaration of %qs specifies %<auto%>", name);
    }
  else if (thread_p
	   && storage_class != sc_extern
	   && storage_class != sc_static)
    {
      error ("function-scope %qs implicitly auto and declared %<__thread%>",
	     name);
      thread_p = false;
    }

  if (storage_class && friendp)
    error ("storage class specifiers invalid in friend function declarations");

  if (!id_declarator)
    unqualified_id = NULL_TREE;
  else
    {
      unqualified_id = id_declarator->u.id.unqualified_name;
      switch (TREE_CODE (unqualified_id))
	{
	case BIT_NOT_EXPR:
	  unqualified_id
	    = constructor_name (TREE_OPERAND (unqualified_id, 0));
	  break;

	case IDENTIFIER_NODE:
	case TEMPLATE_ID_EXPR:
	  break;

	default:
	  gcc_unreachable ();
	}
    }

  /* Determine the type of the entity declared by recurring on the
     declarator.  */
  for (; declarator; declarator = declarator->declarator)
    {
      const cp_declarator *inner_declarator;
      tree attrs;

      if (type == error_mark_node)
	return error_mark_node;

      attrs = declarator->attributes;
      if (attrs)
	{
	  int attr_flags;

	  attr_flags = 0;
	  if (declarator == NULL || declarator->kind == cdk_id)
	    attr_flags |= (int) ATTR_FLAG_DECL_NEXT;
	  if (declarator->kind == cdk_function)
	    attr_flags |= (int) ATTR_FLAG_FUNCTION_NEXT;
	  if (declarator->kind == cdk_array)
	    attr_flags |= (int) ATTR_FLAG_ARRAY_NEXT;
	  returned_attrs = decl_attributes (&type,
					    chainon (returned_attrs, attrs),
					    attr_flags);
	}

      if (declarator->kind == cdk_id)
	break;

      inner_declarator = declarator->declarator;

      switch (declarator->kind)
	{
	case cdk_array:
	  type = create_array_type_for_decl (dname, type,
					     declarator->u.array.bounds);
	  break;

	case cdk_function:
	  {
	    tree arg_types;
	    int funcdecl_p;

	    /* Declaring a function type.
	       Make sure we have a valid type for the function to return.  */

	    /* We now know that the TYPE_QUALS don't apply to the
	       decl, but to its return type.  */
	    type_quals = TYPE_UNQUALIFIED;

	    /* Warn about some types functions can't return.  */

	    if (TREE_CODE (type) == FUNCTION_TYPE)
	      {
		error ("%qs declared as function returning a function", name);
		type = integer_type_node;
	      }
	    if (TREE_CODE (type) == ARRAY_TYPE)
	      {
		error ("%qs declared as function returning an array", name);
		type = integer_type_node;
	      }

	    /* Pick up type qualifiers which should be applied to `this'.  */
	    memfn_quals = declarator->u.function.qualifiers;

	    /* Pick up the exception specifications.  */
	    raises = declarator->u.function.exception_specification;

	    /* Say it's a definition only for the CALL_EXPR
	       closest to the identifier.  */
	    funcdecl_p = inner_declarator && inner_declarator->kind == cdk_id;

	    if (ctype == NULL_TREE
		&& decl_context == FIELD
		&& funcdecl_p
		&& (friendp == 0 || dname == current_class_name))
	      ctype = current_class_type;

	    if (ctype && (sfk == sfk_constructor
			  || sfk == sfk_destructor))
	      {
		/* We are within a class's scope. If our declarator name
		   is the same as the class name, and we are defining
		   a function, then it is a constructor/destructor, and
		   therefore returns a void type.  */

		/* ISO C++ 12.4/2.  A destructor may not be declared
		   const or volatile.  A destructor may not be
		   static.

		   ISO C++ 12.1.  A constructor may not be declared
		   const or volatile.  A constructor may not be
		   virtual.  A constructor may not be static.  */
		if (staticp == 2)
		  error ((flags == DTOR_FLAG)
			 ? "destructor cannot be static member function"
			 : "constructor cannot be static member function");
		if (memfn_quals)
		  {
		    error ((flags == DTOR_FLAG)
			   ? "destructors may not be cv-qualified"
			   : "constructors may not be cv-qualified");
		    memfn_quals = TYPE_UNQUALIFIED;
		  }

		if (decl_context == FIELD
		    && !member_function_or_else (ctype,
						 current_class_type,
						 flags))
		  return error_mark_node;

		if (flags != DTOR_FLAG)
		  {
		    /* It's a constructor.  */
		    if (explicitp == 1)
		      explicitp = 2;
		    if (virtualp)
		      {
			pedwarn ("constructors cannot be declared virtual");
			virtualp = 0;
		      }
		    if (decl_context == FIELD
			&& sfk != sfk_constructor)
		      return error_mark_node;
		  }
		if (decl_context == FIELD)
		  staticp = 0;
	      }
	    else if (friendp)
	      {
		if (initialized)
		  error ("can't initialize friend function %qs", name);
		if (virtualp)
		  {
		    /* Cannot be both friend and virtual.  */
		    error ("virtual functions cannot be friends");
		    friendp = 0;
		  }
		if (decl_context == NORMAL)
		  error ("friend declaration not in class definition");
		if (current_function_decl && funcdef_flag)
		  error ("can't define friend function %qs in a local "
			 "class definition",
			 name);
	      }

	    arg_types = grokparms (declarator->u.function.parameters,
				   &parms);

	    if (inner_declarator
		&& inner_declarator->kind == cdk_id
		&& inner_declarator->u.id.sfk == sfk_destructor
		&& arg_types != void_list_node)
	      {
		error ("destructors may not have parameters");
		arg_types = void_list_node;
		parms = NULL_TREE;
	      }

	    type = build_function_type (type, arg_types);
	  }
	  break;

	case cdk_pointer:
	case cdk_reference:
	case cdk_ptrmem:
	  /* Filter out pointers-to-references and references-to-references.
	     We can get these if a TYPE_DECL is used.  */

	  if (TREE_CODE (type) == REFERENCE_TYPE)
	    {
	      error (declarator->kind == cdk_reference
		     ? "cannot declare reference to %q#T"
		     : "cannot declare pointer to %q#T", type);
	      type = TREE_TYPE (type);
	    }
	  else if (VOID_TYPE_P (type))
	    {
	      if (declarator->kind == cdk_reference)
		error ("cannot declare reference to %q#T", type);
	      else if (declarator->kind == cdk_ptrmem)
		error ("cannot declare pointer to %q#T member", type);
	    }

	  /* We now know that the TYPE_QUALS don't apply to the decl,
	     but to the target of the pointer.  */
	  type_quals = TYPE_UNQUALIFIED;

	  if (declarator->kind == cdk_ptrmem
	      && (TREE_CODE (type) == FUNCTION_TYPE || memfn_quals))
	    {
	      memfn_quals |= cp_type_quals (type);
	      type = build_memfn_type (type,
				       declarator->u.pointer.class_type,
				       memfn_quals);
	      memfn_quals = TYPE_UNQUALIFIED;
	    }

	  if (declarator->kind == cdk_reference)
	    {
	      if (!VOID_TYPE_P (type))
		type = build_reference_type (type);
	    }
	  else if (TREE_CODE (type) == METHOD_TYPE)
	    type = build_ptrmemfunc_type (build_pointer_type (type));
	  else if (declarator->kind == cdk_ptrmem)
	    {
	      gcc_assert (TREE_CODE (declarator->u.pointer.class_type)
			  != NAMESPACE_DECL);
	      if (declarator->u.pointer.class_type == error_mark_node)
		/* We will already have complained.  */
		type = error_mark_node;
	      else
		type = build_ptrmem_type (declarator->u.pointer.class_type,
					  type);
	    }
	  else
	    type = build_pointer_type (type);

	  /* Process a list of type modifier keywords (such as
	     const or volatile) that were given inside the `*' or `&'.  */

	  if (declarator->u.pointer.qualifiers)
	    {
	      type
		= cp_build_qualified_type (type,
					   declarator->u.pointer.qualifiers);
	      type_quals = cp_type_quals (type);
	    }
	  ctype = NULL_TREE;
	  break;

	case cdk_error:
	  break;

	default:
	  gcc_unreachable ();
	}
    }

  if (unqualified_id && TREE_CODE (unqualified_id) == TEMPLATE_ID_EXPR
      && TREE_CODE (type) != FUNCTION_TYPE
      && TREE_CODE (type) != METHOD_TYPE)
    {
      error ("template-id %qD used as a declarator",
	     unqualified_id);
      unqualified_id = dname;
    }

  /* If TYPE is a FUNCTION_TYPE, but the function name was explicitly
     qualified with a class-name, turn it into a METHOD_TYPE, unless
     we know that the function is static.  We take advantage of this
     opportunity to do other processing that pertains to entities
     explicitly declared to be class members.  Note that if DECLARATOR
     is non-NULL, we know it is a cdk_id declarator; otherwise, we
     would not have exited the loop above.  */
  if (declarator
      && declarator->u.id.qualifying_scope
      && TYPE_P (declarator->u.id.qualifying_scope))
    {
      tree t;

      ctype = declarator->u.id.qualifying_scope;
      ctype = TYPE_MAIN_VARIANT (ctype);
      t = ctype;
      while (t != NULL_TREE && CLASS_TYPE_P (t))
	{
	  /* You're supposed to have one `template <...>' for every
	     template class, but you don't need one for a full
	     specialization.  For example:

	       template <class T> struct S{};
	       template <> struct S<int> { void f(); };
	       void S<int>::f () {}

	     is correct; there shouldn't be a `template <>' for the
	     definition of `S<int>::f'.  */
	  if (CLASSTYPE_TEMPLATE_SPECIALIZATION (t)
	      && !any_dependent_template_arguments_p (CLASSTYPE_TI_ARGS (t)))
	    /* T is an explicit (not partial) specialization.  All
	       containing classes must therefore also be explicitly
	       specialized.  */
	    break;
	  if ((CLASSTYPE_USE_TEMPLATE (t) || CLASSTYPE_IS_TEMPLATE (t))
	      && PRIMARY_TEMPLATE_P (CLASSTYPE_TI_TEMPLATE (t)))
	    template_count += 1;

	  t = TYPE_MAIN_DECL (t);
	  t = DECL_CONTEXT (t);
	}

      if (ctype == current_class_type)
	{
	  if (friendp)
	    pedwarn ("member functions are implicitly friends of their class");
	  else
	    pedwarn ("extra qualification %<%T::%> on member %qs",
		     ctype, name);
	}
      else if (/* If the qualifying type is already complete, then we
		  can skip the following checks.  */
	       !COMPLETE_TYPE_P (ctype)
	       && (/* If the function is being defined, then
		      qualifying type must certainly be complete.  */
		   funcdef_flag
		   /* A friend declaration of "T::f" is OK, even if
		      "T" is a template parameter.  But, if this
		      function is not a friend, the qualifying type
		      must be a class.  */
		   || (!friendp && !CLASS_TYPE_P (ctype))
		   /* For a declaration, the type need not be
		      complete, if either it is dependent (since there
		      is no meaningful definition of complete in that
		      case) or the qualifying class is currently being
		      defined.  */
		   || !(dependent_type_p (ctype)
			|| currently_open_class (ctype)))
	       /* Check that the qualifying type is complete.  */
	       && !complete_type_or_else (ctype, NULL_TREE))
	return error_mark_node;
      else if (TREE_CODE (type) == FUNCTION_TYPE)
	{
	  tree sname = declarator->u.id.unqualified_name;

	  if (current_class_type
	      && (!friendp || funcdef_flag))
	    {
	      error (funcdef_flag
		     ? "cannot define member function %<%T::%s%> within %<%T%>"
		     : "cannot declare member function %<%T::%s%> within %<%T%>",
		     ctype, name, current_class_type);
	      return error_mark_node;
	    }

	  if (TREE_CODE (sname) == IDENTIFIER_NODE
	      && NEW_DELETE_OPNAME_P (sname))
	    /* Overloaded operator new and operator delete
	       are always static functions.  */
	    ;
	  else
	    type = build_memfn_type (type, ctype, memfn_quals);
	}
      else if (declspecs->specs[(int)ds_typedef]
	       && current_class_type)
	{
	  error ("cannot declare member %<%T::%s%> within %qT",
		 ctype, name, current_class_type);
	  return error_mark_node;
	}
    }

  /* Now TYPE has the actual type.  */

  if (returned_attrs)
    {
      if (attrlist)
	*attrlist = chainon (returned_attrs, *attrlist);
      else
	attrlist = &returned_attrs;
    }

  /* Did array size calculations overflow?  */

  if (TREE_CODE (type) == ARRAY_TYPE
      && COMPLETE_TYPE_P (type)
      && TREE_CODE (TYPE_SIZE_UNIT (type)) == INTEGER_CST
      && TREE_OVERFLOW (TYPE_SIZE_UNIT (type)))
    {
      error ("size of array %qs is too large", name);
      /* If we proceed with the array type as it is, we'll eventually
	 crash in tree_low_cst().  */
      type = error_mark_node;
    }

  if ((decl_context == FIELD || decl_context == PARM)
      && !processing_template_decl
      && variably_modified_type_p (type, NULL_TREE))
    {
      if (decl_context == FIELD)
	error ("data member may not have variably modified type %qT", type);
      else
	error ("parameter may not have variably modified type %qT", type);
      type = error_mark_node;
    }

  if (explicitp == 1 || (explicitp && friendp))
    {
      /* [dcl.fct.spec] The explicit specifier shall only be used in
	 declarations of constructors within a class definition.  */
      error ("only declarations of constructors can be %<explicit%>");
      explicitp = 0;
    }

  if (storage_class == sc_mutable)
    {
      if (decl_context != FIELD || friendp)
	{
	  error ("non-member %qs cannot be declared %<mutable%>", name);
	  storage_class = sc_none;
	}
      else if (decl_context == TYPENAME || declspecs->specs[(int)ds_typedef])
	{
	  error ("non-object member %qs cannot be declared %<mutable%>", name);
	  storage_class = sc_none;
	}
      else if (TREE_CODE (type) == FUNCTION_TYPE
	       || TREE_CODE (type) == METHOD_TYPE)
	{
	  error ("function %qs cannot be declared %<mutable%>", name);
	  storage_class = sc_none;
	}
      else if (staticp)
	{
	  error ("static %qs cannot be declared %<mutable%>", name);
	  storage_class = sc_none;
	}
      else if (type_quals & TYPE_QUAL_CONST)
	{
	  error ("const %qs cannot be declared %<mutable%>", name);
	  storage_class = sc_none;
	}
    }

  /* If this is declaring a typedef name, return a TYPE_DECL.  */
  if (declspecs->specs[(int)ds_typedef] && decl_context != TYPENAME)
    {
      tree decl;

      /* Note that the grammar rejects storage classes
	 in typenames, fields or parameters.  */
      if (current_lang_name == lang_name_java)
	TYPE_FOR_JAVA (type) = 1;

      /* This declaration:

	   typedef void f(int) const;

	 declares a function type which is not a member of any
	 particular class, but which is cv-qualified; for
	 example "f S::*" declares a pointer to a const-qualified
	 member function of S.  We record the cv-qualification in the
	 function type.  */
      if (memfn_quals && TREE_CODE (type) == FUNCTION_TYPE)
	type = cp_build_qualified_type (type, memfn_quals);

      if (decl_context == FIELD)
	decl = build_lang_decl (TYPE_DECL, unqualified_id, type);
      else
	decl = build_decl (TYPE_DECL, unqualified_id, type);
      if (id_declarator && declarator->u.id.qualifying_scope)
	error ("%Jtypedef name may not be a nested-name-specifier", decl);

      if (decl_context != FIELD)
	{
	  if (!current_function_decl)
	    DECL_CONTEXT (decl) = FROB_CONTEXT (current_namespace);
	  else if (DECL_MAYBE_IN_CHARGE_CONSTRUCTOR_P (current_function_decl)
		   || (DECL_MAYBE_IN_CHARGE_DESTRUCTOR_P
		       (current_function_decl)))
	    /* The TYPE_DECL is "abstract" because there will be
	       clones of this constructor/destructor, and there will
	       be copies of this TYPE_DECL generated in those
	       clones.  */
	    DECL_ABSTRACT (decl) = 1;
	}
      else if (constructor_name_p (unqualified_id, current_class_type))
	pedwarn ("ISO C++ forbids nested type %qD with same name "
		 "as enclosing class",
		 unqualified_id);

      /* If the user declares "typedef struct {...} foo" then the
	 struct will have an anonymous name.  Fill that name in now.
	 Nothing can refer to it, so nothing needs know about the name
	 change.  */
      if (type != error_mark_node
	  && unqualified_id
	  && TYPE_NAME (type)
	  && TREE_CODE (TYPE_NAME (type)) == TYPE_DECL
	  && TYPE_ANONYMOUS_P (type)
	  /* Don't do this if there are attributes.  */
	  && (!attrlist || !*attrlist)
	  && cp_type_quals (type) == TYPE_UNQUALIFIED)
	{
	  tree oldname = TYPE_NAME (type);
	  tree t;

	  /* Replace the anonymous name with the real name everywhere.  */
	  for (t = TYPE_MAIN_VARIANT (type); t; t = TYPE_NEXT_VARIANT (t))
	    if (TYPE_NAME (t) == oldname)
	      TYPE_NAME (t) = decl;

	  if (TYPE_LANG_SPECIFIC (type))
	    TYPE_WAS_ANONYMOUS (type) = 1;

	  /* If this is a typedef within a template class, the nested
	     type is a (non-primary) template.  The name for the
	     template needs updating as well.  */
	  if (TYPE_LANG_SPECIFIC (type) && CLASSTYPE_TEMPLATE_INFO (type))
	    DECL_NAME (CLASSTYPE_TI_TEMPLATE (type))
	      = TYPE_IDENTIFIER (type);

	  /* FIXME remangle member functions; member functions of a
	     type with external linkage have external linkage.  */
	}

	/* Any qualifiers on a function type typedef have already been
	   dealt with. */
      if (memfn_quals && !ctype && TREE_CODE (type) == FUNCTION_TYPE)
	memfn_quals = TYPE_UNQUALIFIED;

      if (signed_p
	  || (typedef_decl && C_TYPEDEF_EXPLICITLY_SIGNED (typedef_decl)))
	C_TYPEDEF_EXPLICITLY_SIGNED (decl) = 1;

      bad_specifiers (decl, "type", virtualp,
		      memfn_quals != TYPE_UNQUALIFIED,
		      inlinep, friendp, raises != NULL_TREE);

      return decl;
    }

  /* Detect the case of an array type of unspecified size
     which came, as such, direct from a typedef name.
     We must copy the type, so that the array's domain can be
     individually set by the object's initializer.  */

  if (type && typedef_type
      && TREE_CODE (type) == ARRAY_TYPE && !TYPE_DOMAIN (type)
      && TYPE_MAIN_VARIANT (type) == TYPE_MAIN_VARIANT (typedef_type))
    type = build_cplus_array_type (TREE_TYPE (type), NULL_TREE);

  /* Detect where we're using a typedef of function type to declare a
     function. PARMS will not be set, so we must create it now.  */

  if (type == typedef_type && TREE_CODE (type) == FUNCTION_TYPE)
    {
      tree decls = NULL_TREE;
      tree args;

      for (args = TYPE_ARG_TYPES (type); args; args = TREE_CHAIN (args))
	{
	  tree decl = cp_build_parm_decl (NULL_TREE, TREE_VALUE (args));

	  TREE_CHAIN (decl) = decls;
	  decls = decl;
	}

      parms = nreverse (decls);

      if (decl_context != TYPENAME)
	{
	  /* A cv-qualifier-seq shall only be part of the function type
	     for a non-static member function. [8.3.5/4 dcl.fct] */
	  if (cp_type_quals (type) != TYPE_UNQUALIFIED
	      && (current_class_type == NULL_TREE || staticp) )
	    {
	      error ("qualified function types cannot be used to declare %s functions",
		     (staticp? "static member" : "free"));
	      type = TYPE_MAIN_VARIANT (type);
	    }

	  /* The qualifiers on the function type become the qualifiers on
	     the non-static member function. */
	  memfn_quals |= cp_type_quals (type);
	}
    }

  /* If this is a type name (such as, in a cast or sizeof),
     compute the type and return it now.  */

  if (decl_context == TYPENAME)
    {
      /* Note that the grammar rejects storage classes
	 in typenames, fields or parameters.  */
      if (type_quals != TYPE_UNQUALIFIED)
	type_quals = TYPE_UNQUALIFIED;

      /* Special case: "friend class foo" looks like a TYPENAME context.  */
      if (friendp)
	{
	  if (type_quals != TYPE_UNQUALIFIED)
	    {
	      error ("type qualifiers specified for friend class declaration");
	      type_quals = TYPE_UNQUALIFIED;
	    }
	  if (inlinep)
	    {
	      error ("%<inline%> specified for friend class declaration");
	      inlinep = 0;
	    }

	  if (!current_aggr)
	    {
	      /* Don't allow friend declaration without a class-key.  */
	      if (TREE_CODE (type) == TEMPLATE_TYPE_PARM)
		pedwarn ("template parameters cannot be friends");
	      else if (TREE_CODE (type) == TYPENAME_TYPE)
		pedwarn ("friend declaration requires class-key, "
			 "i.e. %<friend class %T::%D%>",
			 TYPE_CONTEXT (type), TYPENAME_TYPE_FULLNAME (type));
	      else
		pedwarn ("friend declaration requires class-key, "
			 "i.e. %<friend %#T%>",
			 type);
	    }

	  /* Only try to do this stuff if we didn't already give up.  */
	  if (type != integer_type_node)
	    {
	      /* A friendly class?  */
	      if (current_class_type)
		make_friend_class (current_class_type, TYPE_MAIN_VARIANT (type),
				   /*complain=*/true);
	      else
		error ("trying to make class %qT a friend of global scope",
		       type);

	      type = void_type_node;
	    }
	}
      else if (memfn_quals)
	{
	  if (ctype == NULL_TREE)
	    {
	      if (TREE_CODE (type) != METHOD_TYPE)
		error ("invalid qualifiers on non-member function type");
	      else
		ctype = TYPE_METHOD_BASETYPE (type);
	    }
	  if (ctype)
	    type = build_memfn_type (type, ctype, memfn_quals);
	}

      return type;
    }
  else if (unqualified_id == NULL_TREE && decl_context != PARM
	   && decl_context != CATCHPARM
	   && TREE_CODE (type) != UNION_TYPE
	   && ! bitfield)
    {
      error ("abstract declarator %qT used as declaration", type);
      return error_mark_node;
    }

  /* Only functions may be declared using an operator-function-id.  */
  if (unqualified_id
      && IDENTIFIER_OPNAME_P (unqualified_id)
      && TREE_CODE (type) != FUNCTION_TYPE
      && TREE_CODE (type) != METHOD_TYPE)
    {
      error ("declaration of %qD as non-function", unqualified_id);
      return error_mark_node;
    }

  /* We don't check parameter types here because we can emit a better
     error message later.  */
  if (decl_context != PARM)
    {
      type = check_var_type (unqualified_id, type);
      if (type == error_mark_node)
        return error_mark_node;
    }

  /* Now create the decl, which may be a VAR_DECL, a PARM_DECL
     or a FUNCTION_DECL, depending on DECL_CONTEXT and TYPE.  */

  if (decl_context == PARM || decl_context == CATCHPARM)
    {
      if (ctype || in_namespace)
	error ("cannot use %<::%> in parameter declaration");

      /* A parameter declared as an array of T is really a pointer to T.
	 One declared as a function is really a pointer to a function.
	 One declared as a member is really a pointer to member.  */

      if (TREE_CODE (type) == ARRAY_TYPE)
	{
	  /* Transfer const-ness of array into that of type pointed to.  */
	  type = build_pointer_type (TREE_TYPE (type));
	  type_quals = TYPE_UNQUALIFIED;
	}
      else if (TREE_CODE (type) == FUNCTION_TYPE)
	type = build_pointer_type (type);
    }

  {
    tree decl;

    if (decl_context == PARM)
      {
	decl = cp_build_parm_decl (unqualified_id, type);

	bad_specifiers (decl, "parameter", virtualp,
			memfn_quals != TYPE_UNQUALIFIED,
			inlinep, friendp, raises != NULL_TREE);
      }
    else if (decl_context == FIELD)
      {
	/* The C99 flexible array extension.  */
	if (!staticp && TREE_CODE (type) == ARRAY_TYPE
	    && TYPE_DOMAIN (type) == NULL_TREE)
	  {
	    tree itype = compute_array_index_type (dname, integer_zero_node);
	    type = build_cplus_array_type (TREE_TYPE (type), itype);
	  }

	if (type == error_mark_node)
	  {
	    /* Happens when declaring arrays of sizes which
	       are error_mark_node, for example.  */
	    decl = NULL_TREE;
	  }
	else if (in_namespace && !friendp)
	  {
	    /* Something like struct S { int N::j; };  */
	    error ("invalid use of %<::%>");
	    return error_mark_node;
	  }
	else if (TREE_CODE (type) == FUNCTION_TYPE)
	  {
	    int publicp = 0;
	    tree function_context;

	    if (friendp == 0)
	      {
		if (ctype == NULL_TREE)
		  ctype = current_class_type;

		if (ctype == NULL_TREE)
		  {
		    error ("can't make %qD into a method -- not in a class",
			   unqualified_id);
		    return error_mark_node;
		  }

		/* ``A union may [ ... ] not [ have ] virtual functions.''
		   ARM 9.5 */
		if (virtualp && TREE_CODE (ctype) == UNION_TYPE)
		  {
		    error ("function %qD declared virtual inside a union",
			   unqualified_id);
		    return error_mark_node;
		  }

		if (NEW_DELETE_OPNAME_P (unqualified_id))
		  {
		    if (virtualp)
		      {
			error ("%qD cannot be declared virtual, since it "
			       "is always static",
			       unqualified_id);
			virtualp = 0;
		      }
		  }
		else if (staticp < 2)
		  type = build_memfn_type (type, ctype, memfn_quals);
	      }

	    /* Check that the name used for a destructor makes sense.  */
	    if (sfk == sfk_destructor)
	      {
		if (!ctype)
		  {
		    gcc_assert (friendp);
		    error ("expected qualified name in friend declaration "
			   "for destructor %qD",
			   id_declarator->u.id.unqualified_name);
		    return error_mark_node;
		  }

		if (!same_type_p (TREE_OPERAND
				  (id_declarator->u.id.unqualified_name, 0),
				  ctype))
		  {
		    error ("declaration of %qD as member of %qT",
			   id_declarator->u.id.unqualified_name, ctype);
		    return error_mark_node;
		  }
	      }

	    /* Tell grokfndecl if it needs to set TREE_PUBLIC on the node.  */
	    function_context = (ctype != NULL_TREE) ?
	      decl_function_context (TYPE_MAIN_DECL (ctype)) : NULL_TREE;
	    publicp = (! friendp || ! staticp)
	      && function_context == NULL_TREE;
	    decl = grokfndecl (ctype, type,
			       TREE_CODE (unqualified_id) != TEMPLATE_ID_EXPR
			       ? unqualified_id : dname,
			       parms,
			       unqualified_id,
			       virtualp, flags, memfn_quals, raises,
			       friendp ? -1 : 0, friendp, publicp, inlinep,
			       sfk,
			       funcdef_flag, template_count, in_namespace, attrlist);
	    if (decl == NULL_TREE)
	      return error_mark_node;
#if 0
	    /* This clobbers the attrs stored in `decl' from `attrlist'.  */
	    /* The decl and setting of decl_attr is also turned off.  */
	    decl = build_decl_attribute_variant (decl, decl_attr);
#endif

	    /* [class.conv.ctor]

	       A constructor declared without the function-specifier
	       explicit that can be called with a single parameter
	       specifies a conversion from the type of its first
	       parameter to the type of its class.  Such a constructor
	       is called a converting constructor.  */
	    if (explicitp == 2)
	      DECL_NONCONVERTING_P (decl) = 1;
	    else if (DECL_CONSTRUCTOR_P (decl))
	      {
		/* The constructor can be called with exactly one
		   parameter if there is at least one parameter, and
		   any subsequent parameters have default arguments.
		   Ignore any compiler-added parms.  */
		tree arg_types = FUNCTION_FIRST_USER_PARMTYPE (decl);

		if (arg_types == void_list_node
		    || (arg_types
			&& TREE_CHAIN (arg_types)
			&& TREE_CHAIN (arg_types) != void_list_node
			&& !TREE_PURPOSE (TREE_CHAIN (arg_types))))
		  DECL_NONCONVERTING_P (decl) = 1;
	      }
	  }
	else if (TREE_CODE (type) == METHOD_TYPE)
	  {
	    /* We only get here for friend declarations of
	       members of other classes.  */
	    /* All method decls are public, so tell grokfndecl to set
	       TREE_PUBLIC, also.  */
	    decl = grokfndecl (ctype, type,
			       TREE_CODE (unqualified_id) != TEMPLATE_ID_EXPR
			       ? unqualified_id : dname,
			       parms,
			       unqualified_id,
			       virtualp, flags, memfn_quals, raises,
			       friendp ? -1 : 0, friendp, 1, 0, sfk,
			       funcdef_flag, template_count, in_namespace,
			       attrlist);
	    if (decl == NULL_TREE)
	      return error_mark_node;
	  }
	else if (!staticp && !dependent_type_p (type)
		 && !COMPLETE_TYPE_P (complete_type (type))
		 && (TREE_CODE (type) != ARRAY_TYPE || initialized == 0))
	  {
	    if (unqualified_id)
	      error ("field %qD has incomplete type", unqualified_id);
	    else
	      error ("name %qT has incomplete type", type);

	    /* If we're instantiating a template, tell them which
	       instantiation made the field's type be incomplete.  */
	    if (current_class_type
		&& TYPE_NAME (current_class_type)
		&& IDENTIFIER_TEMPLATE (TYPE_IDENTIFIER (current_class_type))
		&& declspecs->type
		&& declspecs->type == type)
	      error ("  in instantiation of template %qT",
		     current_class_type);

	    return error_mark_node;
	  }
	else
	  {
	    if (friendp)
	      {
		error ("%qE is neither function nor member function; "
		       "cannot be declared friend", unqualified_id);
		friendp = 0;
	      }
	    decl = NULL_TREE;
	  }

	if (friendp)
	  {
	    /* Friends are treated specially.  */
	    if (ctype == current_class_type)
	      ;  /* We already issued a pedwarn.  */
	    else if (decl && DECL_NAME (decl))
	      {
		if (template_class_depth (current_class_type) == 0)
		  {
		    decl = check_explicit_specialization
		      (unqualified_id, decl, template_count,
		       2 * funcdef_flag + 4);
		    if (decl == error_mark_node)
		      return error_mark_node;
		  }

		decl = do_friend (ctype, unqualified_id, decl,
				  *attrlist, flags,
				  funcdef_flag);
		return decl;
	      }
	    else
	      return error_mark_node;
	  }

	/* Structure field.  It may not be a function, except for C++.  */

	if (decl == NULL_TREE)
	  {
	    if (initialized)
	      {
		if (!staticp)
		  {
		    /* An attempt is being made to initialize a non-static
		       member.  But, from [class.mem]:

		       4 A member-declarator can contain a
		       constant-initializer only if it declares a static
		       member (_class.static_) of integral or enumeration
		       type, see _class.static.data_.

		       This used to be relatively common practice, but
		       the rest of the compiler does not correctly
		       handle the initialization unless the member is
		       static so we make it static below.  */
		    pedwarn ("ISO C++ forbids initialization of member %qD",
			     unqualified_id);
		    pedwarn ("making %qD static", unqualified_id);
		    staticp = 1;
		  }

		if (uses_template_parms (type))
		  /* We'll check at instantiation time.  */
		  ;
		else if (check_static_variable_definition (unqualified_id,
							   type))
		  /* If we just return the declaration, crashes
		     will sometimes occur.  We therefore return
		     void_type_node, as if this was a friend
		     declaration, to cause callers to completely
		     ignore this declaration.  */
		  return error_mark_node;
	      }

	    if (staticp)
	      {
		/* C++ allows static class members.  All other work
		   for this is done by grokfield.  */
		decl = build_lang_decl (VAR_DECL, unqualified_id, type);
		set_linkage_for_static_data_member (decl);
		/* Even if there is an in-class initialization, DECL
		   is considered undefined until an out-of-class
		   definition is provided.  */
		DECL_EXTERNAL (decl) = 1;

		if (thread_p)
		  {
		    if (targetm.have_tls)
		      DECL_TLS_MODEL (decl) = decl_default_tls_model (decl);
		    else
		      /* A mere warning is sure to result in improper
			 semantics at runtime.  Don't bother to allow this to
			 compile.  */
		      error ("thread-local storage not supported for this target");
		  }
	      }
	    else
	      {
		decl = build_decl (FIELD_DECL, unqualified_id, type);
		DECL_NONADDRESSABLE_P (decl) = bitfield;
		if (storage_class == sc_mutable)
		  {
		    DECL_MUTABLE_P (decl) = 1;
		    storage_class = sc_none;
		  }
	      }

	    bad_specifiers (decl, "field", virtualp,
			    memfn_quals != TYPE_UNQUALIFIED,
			    inlinep, friendp, raises != NULL_TREE);
	  }
      }
    else if (TREE_CODE (type) == FUNCTION_TYPE
	     || TREE_CODE (type) == METHOD_TYPE)
      {
	tree original_name;
	int publicp = 0;

	if (!unqualified_id)
	  return error_mark_node;

	if (TREE_CODE (unqualified_id) == TEMPLATE_ID_EXPR)
	  original_name = dname;
	else
	  original_name = unqualified_id;

	if (storage_class == sc_auto)
	  error ("storage class %<auto%> invalid for function %qs", name);
	else if (storage_class == sc_register)
	  error ("storage class %<register%> invalid for function %qs", name);
	else if (thread_p)
	  error ("storage class %<__thread%> invalid for function %qs", name);

	/* Function declaration not at top level.
	   Storage classes other than `extern' are not allowed
	   and `extern' makes no difference.  */
	if (! toplevel_bindings_p ()
	    && (storage_class == sc_static
		|| declspecs->specs[(int)ds_inline])
	    && pedantic)
	  {
	    if (storage_class == sc_static)
	      pedwarn ("%<static%> specified invalid for function %qs "
		       "declared out of global scope", name);
	    else
	      pedwarn ("%<inline%> specifier invalid for function %qs "
		       "declared out of global scope", name);
	  }

	if (ctype == NULL_TREE)
	  {
	    if (virtualp)
	      {
		error ("virtual non-class function %qs", name);
		virtualp = 0;
	      }
	  }
	else if (TREE_CODE (type) == FUNCTION_TYPE && staticp < 2
		 && !NEW_DELETE_OPNAME_P (original_name))
	  type = build_method_type_directly (ctype,
					     TREE_TYPE (type),
					     TYPE_ARG_TYPES (type));

	/* Record presence of `static'.  */
	publicp = (ctype != NULL_TREE
		   || storage_class == sc_extern
		   || storage_class != sc_static);

	decl = grokfndecl (ctype, type, original_name, parms, unqualified_id,
			   virtualp, flags, memfn_quals, raises,
			   1, friendp,
			   publicp, inlinep, sfk, funcdef_flag,
			   template_count, in_namespace, attrlist);
	if (decl == NULL_TREE)
	  return error_mark_node;

	if (staticp == 1)
	  {
	    int invalid_static = 0;

	    /* Don't allow a static member function in a class, and forbid
	       declaring main to be static.  */
	    if (TREE_CODE (type) == METHOD_TYPE)
	      {
		pedwarn ("cannot declare member function %qD to have "
			 "static linkage", decl);
		invalid_static = 1;
	      }
	    else if (current_function_decl)
	      {
		/* FIXME need arm citation */
		error ("cannot declare static function inside another function");
		invalid_static = 1;
	      }

	    if (invalid_static)
	      {
		staticp = 0;
		storage_class = sc_none;
	      }
	  }
      }
    else
      {
	/* It's a variable.  */

	/* An uninitialized decl with `extern' is a reference.  */
	decl = grokvardecl (type, unqualified_id,
			    declspecs,
			    initialized,
			    (type_quals & TYPE_QUAL_CONST) != 0,
			    ctype ? ctype : in_namespace);
	bad_specifiers (decl, "variable", virtualp,
			memfn_quals != TYPE_UNQUALIFIED,
			inlinep, friendp, raises != NULL_TREE);

	if (ctype)
	  {
	    DECL_CONTEXT (decl) = ctype;
	    if (staticp == 1)
	      {
		pedwarn ("%<static%> may not be used when defining "
			 "(as opposed to declaring) a static data member");
		staticp = 0;
		storage_class = sc_none;
	      }
	    if (storage_class == sc_register && TREE_STATIC (decl))
	      {
		error ("static member %qD declared %<register%>", decl);
		storage_class = sc_none;
	      }
	    if (storage_class == sc_extern && pedantic)
	      {
		pedwarn ("cannot explicitly declare member %q#D to have "
			 "extern linkage",
			 decl);
		storage_class = sc_none;
	      }
	  }
      }

    /* Record `register' declaration for warnings on &
       and in case doing stupid register allocation.  */

    if (storage_class == sc_register)
      DECL_REGISTER (decl) = 1;
    else if (storage_class == sc_extern)
      DECL_THIS_EXTERN (decl) = 1;
    else if (storage_class == sc_static)
      DECL_THIS_STATIC (decl) = 1;

    /* Record constancy and volatility.  There's no need to do this
       when processing a template; we'll do this for the instantiated
       declaration based on the type of DECL.  */
    if (!processing_template_decl)
      cp_apply_type_quals_to_decl (type_quals, decl);

    return decl;
  }
}

/* Subroutine of start_function.  Ensure that each of the parameter
   types (as listed in PARMS) is complete, as is required for a
   function definition.  */

static void
require_complete_types_for_parms (tree parms)
{
  for (; parms; parms = TREE_CHAIN (parms))
    {
      if (dependent_type_p (TREE_TYPE (parms)))
	continue;
      if (!VOID_TYPE_P (TREE_TYPE (parms))
	  && complete_type_or_else (TREE_TYPE (parms), parms))
	{
	  relayout_decl (parms);
	  DECL_ARG_TYPE (parms) = type_passed_as (TREE_TYPE (parms));
	}
      else
	/* grokparms or complete_type_or_else will have already issued
	   an error.  */
	TREE_TYPE (parms) = error_mark_node;
    }
}

/* Returns nonzero if T is a local variable.  */

int
local_variable_p (tree t)
{
  if ((TREE_CODE (t) == VAR_DECL
       /* A VAR_DECL with a context that is a _TYPE is a static data
	  member.  */
       && !TYPE_P (CP_DECL_CONTEXT (t))
       /* Any other non-local variable must be at namespace scope.  */
       && !DECL_NAMESPACE_SCOPE_P (t))
      || (TREE_CODE (t) == PARM_DECL))
    return 1;

  return 0;
}

/* Returns nonzero if T is an automatic local variable or a label.
   (These are the declarations that need to be remapped when the code
   containing them is duplicated.)  */

int
nonstatic_local_decl_p (tree t)
{
  return ((local_variable_p (t) && !TREE_STATIC (t))
	  || TREE_CODE (t) == LABEL_DECL
	  || TREE_CODE (t) == RESULT_DECL);
}

/* Like local_variable_p, but suitable for use as a tree-walking
   function.  */

static tree
local_variable_p_walkfn (tree *tp, int *walk_subtrees,
			 void *data ATTRIBUTE_UNUSED)
{
  if (local_variable_p (*tp) && !DECL_ARTIFICIAL (*tp))
    return *tp;
  else if (TYPE_P (*tp))
    *walk_subtrees = 0;

  return NULL_TREE;
}


/* Check that ARG, which is a default-argument expression for a
   parameter DECL, is valid.  Returns ARG, or ERROR_MARK_NODE, if
   something goes wrong.  DECL may also be a _TYPE node, rather than a
   DECL, if there is no DECL available.  */

tree
check_default_argument (tree decl, tree arg)
{
  tree var;
  tree decl_type;

  if (TREE_CODE (arg) == DEFAULT_ARG)
    /* We get a DEFAULT_ARG when looking at an in-class declaration
       with a default argument.  Ignore the argument for now; we'll
       deal with it after the class is complete.  */
    return arg;

  if (TYPE_P (decl))
    {
      decl_type = decl;
      decl = NULL_TREE;
    }
  else
    decl_type = TREE_TYPE (decl);

  if (arg == error_mark_node
      || decl == error_mark_node
      || TREE_TYPE (arg) == error_mark_node
      || decl_type == error_mark_node)
    /* Something already went wrong.  There's no need to check
       further.  */
    return error_mark_node;

  /* [dcl.fct.default]

     A default argument expression is implicitly converted to the
     parameter type.  */
  if (!TREE_TYPE (arg)
      || !can_convert_arg (decl_type, TREE_TYPE (arg), arg, LOOKUP_NORMAL))
    {
      if (decl)
	error ("default argument for %q#D has type %qT",
	       decl, TREE_TYPE (arg));
      else
	error ("default argument for parameter of type %qT has type %qT",
	       decl_type, TREE_TYPE (arg));

      return error_mark_node;
    }

  /* [dcl.fct.default]

     Local variables shall not be used in default argument
     expressions.

     The keyword `this' shall not be used in a default argument of a
     member function.  */
  var = walk_tree_without_duplicates (&arg, local_variable_p_walkfn,
				      NULL);
  if (var)
    {
      error ("default argument %qE uses local variable %qD", arg, var);
      return error_mark_node;
    }

  /* All is well.  */
  return arg;
}

/* Decode the list of parameter types for a function type.
   Given the list of things declared inside the parens,
   return a list of types.

   If this parameter does not end with an ellipsis, we append
   void_list_node.

   *PARMS is set to the chain of PARM_DECLs created.  */

static tree
grokparms (cp_parameter_declarator *first_parm, tree *parms)
{
  tree result = NULL_TREE;
  tree decls = NULL_TREE;
  int ellipsis = !first_parm || first_parm->ellipsis_p;
  cp_parameter_declarator *parm;
  int any_error = 0;

  for (parm = first_parm; parm != NULL; parm = parm->next)
    {
      tree type = NULL_TREE;
      tree init = parm->default_argument;
      tree attrs;
      tree decl;

      if (parm == no_parameters)
	break;

      attrs = parm->decl_specifiers.attributes;
      parm->decl_specifiers.attributes = NULL_TREE;
      decl = grokdeclarator (parm->declarator, &parm->decl_specifiers,
			     PARM, init != NULL_TREE, &attrs);
      if (! decl || TREE_TYPE (decl) == error_mark_node)
	continue;

      if (attrs)
	cplus_decl_attributes (&decl, attrs, 0);

      type = TREE_TYPE (decl);
      if (VOID_TYPE_P (type))
	{
	  if (same_type_p (type, void_type_node)
	      && DECL_SELF_REFERENCE_P (type)
	      && !DECL_NAME (decl) && !result && !parm->next && !ellipsis)
	    /* this is a parmlist of `(void)', which is ok.  */
	    break;
	  cxx_incomplete_type_error (decl, type);
	  /* It's not a good idea to actually create parameters of
	     type `void'; other parts of the compiler assume that a
	     void type terminates the parameter list.  */
	  type = error_mark_node;
	  TREE_TYPE (decl) = error_mark_node;
	}

      if (type != error_mark_node)
	{
	  /* Top-level qualifiers on the parameters are
	     ignored for function types.  */
	  type = cp_build_qualified_type (type, 0);
	  if (TREE_CODE (type) == METHOD_TYPE)
	    {
	      error ("parameter %qD invalidly declared method type", decl);
	      type = build_pointer_type (type);
	      TREE_TYPE (decl) = type;
	    }
	  else if (abstract_virtuals_error (decl, type))
	    any_error = 1;  /* Seems like a good idea.  */
	  else if (POINTER_TYPE_P (type))
	    {
	      /* [dcl.fct]/6, parameter types cannot contain pointers
		 (references) to arrays of unknown bound.  */
	      tree t = TREE_TYPE (type);
	      int ptr = TYPE_PTR_P (type);

	      while (1)
		{
		  if (TYPE_PTR_P (t))
		    ptr = 1;
		  else if (TREE_CODE (t) != ARRAY_TYPE)
		    break;
		  else if (!TYPE_DOMAIN (t))
		    break;
		  t = TREE_TYPE (t);
		}
	      if (TREE_CODE (t) == ARRAY_TYPE)
		error ("parameter %qD includes %s to array of unknown "
		       "bound %qT",
		       decl, ptr ? "pointer" : "reference", t);
	    }

	  if (any_error)
	    init = NULL_TREE;
	  else if (init && !processing_template_decl)
	    init = check_default_argument (decl, init);
	}

      TREE_CHAIN (decl) = decls;
      decls = decl;
      result = tree_cons (init, type, result);
    }
  decls = nreverse (decls);
  result = nreverse (result);
  if (!ellipsis)
    result = chainon (result, void_list_node);
  *parms = decls;

  return result;
}


/* D is a constructor or overloaded `operator='.

   Let T be the class in which D is declared. Then, this function
   returns:

   -1 if D's is an ill-formed constructor or copy assignment operator
      whose first parameter is of type `T'.
   0  if D is not a copy constructor or copy assignment
      operator.
   1  if D is a copy constructor or copy assignment operator whose
      first parameter is a reference to const qualified T.
   2  if D is a copy constructor or copy assignment operator whose
      first parameter is a reference to non-const qualified T.

   This function can be used as a predicate. Positive values indicate
   a copy constructor and nonzero values indicate a copy assignment
   operator.  */

int
copy_fn_p (tree d)
{
  tree args;
  tree arg_type;
  int result = 1;

  gcc_assert (DECL_FUNCTION_MEMBER_P (d));

  if (TREE_CODE (d) == TEMPLATE_DECL
      || (DECL_TEMPLATE_INFO (d)
	  && DECL_MEMBER_TEMPLATE_P (DECL_TI_TEMPLATE (d))))
    /* Instantiations of template member functions are never copy
       functions.  Note that member functions of templated classes are
       represented as template functions internally, and we must
       accept those as copy functions.  */
    return 0;

  args = FUNCTION_FIRST_USER_PARMTYPE (d);
  if (!args)
    return 0;

  arg_type = TREE_VALUE (args);
  if (arg_type == error_mark_node)
    return 0;

  if (TYPE_MAIN_VARIANT (arg_type) == DECL_CONTEXT (d))
    {
      /* Pass by value copy assignment operator.  */
      result = -1;
    }
  else if (TREE_CODE (arg_type) == REFERENCE_TYPE
	   && TYPE_MAIN_VARIANT (TREE_TYPE (arg_type)) == DECL_CONTEXT (d))
    {
      if (CP_TYPE_CONST_P (TREE_TYPE (arg_type)))
	result = 2;
    }
  else
    return 0;

  args = TREE_CHAIN (args);

  if (args && args != void_list_node && !TREE_PURPOSE (args))
    /* There are more non-optional args.  */
    return 0;

  return result;
}

/* Remember any special properties of member function DECL.  */

void grok_special_member_properties (tree decl)
{
  tree class_type;

  if (!DECL_NONSTATIC_MEMBER_FUNCTION_P (decl))
    return;

  class_type = DECL_CONTEXT (decl);
  if (DECL_CONSTRUCTOR_P (decl))
    {
      int ctor = copy_fn_p (decl);

      TYPE_HAS_CONSTRUCTOR (class_type) = 1;

      if (ctor > 0)
	{
	  /* [class.copy]

	     A non-template constructor for class X is a copy
	     constructor if its first parameter is of type X&, const
	     X&, volatile X& or const volatile X&, and either there
	     are no other parameters or else all other parameters have
	     default arguments.  */
	  TYPE_HAS_INIT_REF (class_type) = 1;
	  if (ctor > 1)
	    TYPE_HAS_CONST_INIT_REF (class_type) = 1;
	}
      else if (sufficient_parms_p (FUNCTION_FIRST_USER_PARMTYPE (decl)))
	TYPE_HAS_DEFAULT_CONSTRUCTOR (class_type) = 1;
    }
  else if (DECL_OVERLOADED_OPERATOR_P (decl) == NOP_EXPR)
    {
      /* [class.copy]

	 A non-template assignment operator for class X is a copy
	 assignment operator if its parameter is of type X, X&, const
	 X&, volatile X& or const volatile X&.  */

      int assop = copy_fn_p (decl);

      if (assop)
	{
	  TYPE_HAS_ASSIGN_REF (class_type) = 1;
	  if (assop != 1)
	    TYPE_HAS_CONST_ASSIGN_REF (class_type) = 1;
	}
    }
}

/* Check a constructor DECL has the correct form.  Complains
   if the class has a constructor of the form X(X).  */

int
grok_ctor_properties (tree ctype, tree decl)
{
  int ctor_parm = copy_fn_p (decl);

  if (ctor_parm < 0)
    {
      /* [class.copy]

	 A declaration of a constructor for a class X is ill-formed if
	 its first parameter is of type (optionally cv-qualified) X
	 and either there are no other parameters or else all other
	 parameters have default arguments.

	 We *don't* complain about member template instantiations that
	 have this form, though; they can occur as we try to decide
	 what constructor to use during overload resolution.  Since
	 overload resolution will never prefer such a constructor to
	 the non-template copy constructor (which is either explicitly
	 or implicitly defined), there's no need to worry about their
	 existence.  Theoretically, they should never even be
	 instantiated, but that's hard to forestall.  */
      error ("invalid constructor; you probably meant %<%T (const %T&)%>",
		ctype, ctype);
      return 0;
    }

  return 1;
}

/* An operator with this code is unary, but can also be binary.  */

static int
ambi_op_p (enum tree_code code)
{
  return (code == INDIRECT_REF
	  || code == ADDR_EXPR
	  || code == UNARY_PLUS_EXPR
	  || code == NEGATE_EXPR
	  || code == PREINCREMENT_EXPR
	  || code == PREDECREMENT_EXPR);
}

/* An operator with this name can only be unary.  */

static int
unary_op_p (enum tree_code code)
{
  return (code == TRUTH_NOT_EXPR
	  || code == BIT_NOT_EXPR
	  || code == COMPONENT_REF
	  || code == TYPE_EXPR);
}

/* DECL is a declaration for an overloaded operator.  If COMPLAIN is true,
   errors are issued for invalid declarations.  */

bool
grok_op_properties (tree decl, bool complain)
{
  tree argtypes = TYPE_ARG_TYPES (TREE_TYPE (decl));
  tree argtype;
  int methodp = (TREE_CODE (TREE_TYPE (decl)) == METHOD_TYPE);
  tree name = DECL_NAME (decl);
  enum tree_code operator_code;
  int arity;
  bool ellipsis_p;
  tree class_type;

  /* Count the number of arguments and check for ellipsis.  */
  for (argtype = argtypes, arity = 0;
       argtype && argtype != void_list_node;
       argtype = TREE_CHAIN (argtype))
    ++arity;
  ellipsis_p = !argtype;

  class_type = DECL_CONTEXT (decl);
  if (class_type && !CLASS_TYPE_P (class_type))
    class_type = NULL_TREE;

  if (DECL_CONV_FN_P (decl))
    operator_code = TYPE_EXPR;
  else
    do
      {
#define DEF_OPERATOR(NAME, CODE, MANGLING, ARITY, ASSN_P)	\
	if (ansi_opname (CODE) == name)				\
	  {							\
	    operator_code = (CODE);				\
	    break;						\
	  }							\
	else if (ansi_assopname (CODE) == name)			\
	  {							\
	    operator_code = (CODE);				\
	    DECL_ASSIGNMENT_OPERATOR_P (decl) = 1;		\
	    break;						\
	  }

#include "operators.def"
#undef DEF_OPERATOR

	gcc_unreachable ();
      }
    while (0);
  gcc_assert (operator_code != LAST_CPLUS_TREE_CODE);
  SET_OVERLOADED_OPERATOR_CODE (decl, operator_code);

  if (class_type)
    switch (operator_code)
      {
      case NEW_EXPR:
	TYPE_HAS_NEW_OPERATOR (class_type) = 1;
	break;

      case DELETE_EXPR:
	TYPE_GETS_DELETE (class_type) |= 1;
	break;

      case VEC_NEW_EXPR:
	TYPE_HAS_ARRAY_NEW_OPERATOR (class_type) = 1;
	break;

      case VEC_DELETE_EXPR:
	TYPE_GETS_DELETE (class_type) |= 2;
	break;

      default:
	break;
      }

    /* [basic.std.dynamic.allocation]/1:

       A program is ill-formed if an allocation function is declared
       in a namespace scope other than global scope or declared static
       in global scope.

       The same also holds true for deallocation functions.  */
  if (operator_code == NEW_EXPR || operator_code == VEC_NEW_EXPR
      || operator_code == DELETE_EXPR || operator_code == VEC_DELETE_EXPR)
    {
      if (DECL_NAMESPACE_SCOPE_P (decl))
	{
	  if (CP_DECL_CONTEXT (decl) != global_namespace)
	    {
	      error ("%qD may not be declared within a namespace", decl);
	      return false;
	    }
	  else if (!TREE_PUBLIC (decl))
	    {
	      error ("%qD may not be declared as static", decl);
	      return false;
	    }
	}
    }

  if (operator_code == NEW_EXPR || operator_code == VEC_NEW_EXPR)
    TREE_TYPE (decl) = coerce_new_type (TREE_TYPE (decl));
  else if (operator_code == DELETE_EXPR || operator_code == VEC_DELETE_EXPR)
    TREE_TYPE (decl) = coerce_delete_type (TREE_TYPE (decl));
  else
    {
      /* An operator function must either be a non-static member function
	 or have at least one parameter of a class, a reference to a class,
	 an enumeration, or a reference to an enumeration.  13.4.0.6 */
      if (! methodp || DECL_STATIC_FUNCTION_P (decl))
	{
	  if (operator_code == TYPE_EXPR
	      || operator_code == CALL_EXPR
	      || operator_code == COMPONENT_REF
	      || operator_code == ARRAY_REF
	      || operator_code == NOP_EXPR)
	    {
	      error ("%qD must be a nonstatic member function", decl);
	      return false;
	    }
	  else
	    {
	      tree p;

	      if (DECL_STATIC_FUNCTION_P (decl))
		{
		  error ("%qD must be either a non-static member "
			 "function or a non-member function", decl);
		  return false;
		}

	      for (p = argtypes; p && p != void_list_node; p = TREE_CHAIN (p))
		{
		  tree arg = non_reference (TREE_VALUE (p));
		  if (arg == error_mark_node)
		    return false;

		  /* IS_AGGR_TYPE, rather than CLASS_TYPE_P, is used
		     because these checks are performed even on
		     template functions.  */
		  if (IS_AGGR_TYPE (arg) || TREE_CODE (arg) == ENUMERAL_TYPE)
		    break;
		}

	      if (!p || p == void_list_node)
		{
		  if (complain)
		    error ("%qD must have an argument of class or "
			   "enumerated type", decl);
		  return false;
		}
	    }
	}

      /* There are no restrictions on the arguments to an overloaded
	 "operator ()".  */
      if (operator_code == CALL_EXPR)
	return true;

      /* Warn about conversion operators that will never be used.  */
      if (IDENTIFIER_TYPENAME_P (name)
	  && ! DECL_TEMPLATE_INFO (decl)
	  && warn_conversion
	  /* Warn only declaring the function; there is no need to
	     warn again about out-of-class definitions.  */
	  && class_type == current_class_type)
	{
	  tree t = TREE_TYPE (name);
	  int ref = (TREE_CODE (t) == REFERENCE_TYPE);
	  const char *what = 0;

	  if (ref)
	    t = TYPE_MAIN_VARIANT (TREE_TYPE (t));

	  if (TREE_CODE (t) == VOID_TYPE)
	    what = "void";
	  else if (class_type)
	    {
	      if (t == class_type)
		what = "the same type";
	      /* Don't force t to be complete here.  */
	      else if (IS_AGGR_TYPE (t)
		       && COMPLETE_TYPE_P (t)
		       && DERIVED_FROM_P (t, class_type))
		what = "a base class";
	    }

	  if (what)
	    warning (OPT_Wconversion, "conversion to %s%s will never use a type "
		     "conversion operator",
		     ref ? "a reference to " : "", what);
	}

      if (operator_code == COND_EXPR)
	{
	  /* 13.4.0.3 */
	  error ("ISO C++ prohibits overloading operator ?:");
	  return false;
	}
      else if (ellipsis_p)
	{
	  error ("%qD must not have variable number of arguments", decl);
	  return false;
	}
      else if (ambi_op_p (operator_code))
	{
	  if (arity == 1)
	    /* We pick the one-argument operator codes by default, so
	       we don't have to change anything.  */
	    ;
	  else if (arity == 2)
	    {
	      /* If we thought this was a unary operator, we now know
		 it to be a binary operator.  */
	      switch (operator_code)
		{
		case INDIRECT_REF:
		  operator_code = MULT_EXPR;
		  break;

		case ADDR_EXPR:
		  operator_code = BIT_AND_EXPR;
		  break;

		case UNARY_PLUS_EXPR:
		  operator_code = PLUS_EXPR;
		  break;

		case NEGATE_EXPR:
		  operator_code = MINUS_EXPR;
		  break;

		case PREINCREMENT_EXPR:
		  operator_code = POSTINCREMENT_EXPR;
		  break;

		case PREDECREMENT_EXPR:
		  operator_code = POSTDECREMENT_EXPR;
		  break;

		default:
		  gcc_unreachable ();
		}

	      SET_OVERLOADED_OPERATOR_CODE (decl, operator_code);

	      if ((operator_code == POSTINCREMENT_EXPR
		   || operator_code == POSTDECREMENT_EXPR)
		  && ! processing_template_decl
		  && ! same_type_p (TREE_VALUE (TREE_CHAIN (argtypes)), integer_type_node))
		{
		  if (methodp)
		    error ("postfix %qD must take %<int%> as its argument",
			   decl);
		  else
		    error ("postfix %qD must take %<int%> as its second "
			   "argument", decl);
		  return false;
		}
	    }
	  else
	    {
	      if (methodp)
		error ("%qD must take either zero or one argument", decl);
	      else
		error ("%qD must take either one or two arguments", decl);
	      return false;
	    }

	  /* More Effective C++ rule 6.  */
	  if (warn_ecpp
	      && (operator_code == POSTINCREMENT_EXPR
		  || operator_code == POSTDECREMENT_EXPR
		  || operator_code == PREINCREMENT_EXPR
		  || operator_code == PREDECREMENT_EXPR))
	    {
	      tree arg = TREE_VALUE (argtypes);
	      tree ret = TREE_TYPE (TREE_TYPE (decl));
	      if (methodp || TREE_CODE (arg) == REFERENCE_TYPE)
		arg = TREE_TYPE (arg);
	      arg = TYPE_MAIN_VARIANT (arg);
	      if (operator_code == PREINCREMENT_EXPR
		  || operator_code == PREDECREMENT_EXPR)
		{
		  if (TREE_CODE (ret) != REFERENCE_TYPE
		      || !same_type_p (TYPE_MAIN_VARIANT (TREE_TYPE (ret)),
				       arg))
		    warning (OPT_Weffc__, "prefix %qD should return %qT", decl,
			     build_reference_type (arg));
		}
	      else
		{
		  if (!same_type_p (TYPE_MAIN_VARIANT (ret), arg))
		    warning (OPT_Weffc__, "postfix %qD should return %qT", decl, arg);
		}
	    }
	}
      else if (unary_op_p (operator_code))
	{
	  if (arity != 1)
	    {
	      if (methodp)
		error ("%qD must take %<void%>", decl);
	      else
		error ("%qD must take exactly one argument", decl);
	      return false;
	    }
	}
      else /* if (binary_op_p (operator_code)) */
	{
	  if (arity != 2)
	    {
	      if (methodp)
		error ("%qD must take exactly one argument", decl);
	      else
		error ("%qD must take exactly two arguments", decl);
	      return false;
	    }

	  /* More Effective C++ rule 7.  */
	  if (warn_ecpp
	      && (operator_code == TRUTH_ANDIF_EXPR
		  || operator_code == TRUTH_ORIF_EXPR
		  || operator_code == COMPOUND_EXPR))
	    warning (OPT_Weffc__, "user-defined %qD always evaluates both arguments",
		     decl);
	}

      /* Effective C++ rule 23.  */
      if (warn_ecpp
	  && arity == 2
	  && !DECL_ASSIGNMENT_OPERATOR_P (decl)
	  && (operator_code == PLUS_EXPR
	      || operator_code == MINUS_EXPR
	      || operator_code == TRUNC_DIV_EXPR
	      || operator_code == MULT_EXPR
	      || operator_code == TRUNC_MOD_EXPR)
	  && TREE_CODE (TREE_TYPE (TREE_TYPE (decl))) == REFERENCE_TYPE)
	warning (OPT_Weffc__, "%qD should return by value", decl);

      /* [over.oper]/8 */
      for (; argtypes && argtypes != void_list_node;
	  argtypes = TREE_CHAIN (argtypes))
	if (TREE_PURPOSE (argtypes))
	  {
	    TREE_PURPOSE (argtypes) = NULL_TREE;
	    if (operator_code == POSTINCREMENT_EXPR
		|| operator_code == POSTDECREMENT_EXPR)
	      {
		if (pedantic)
		  pedwarn ("%qD cannot have default arguments", decl);
	      }
	    else
	      {
		error ("%qD cannot have default arguments", decl);
		return false;
	      }
	  }
    }
  return true;
}

/* Return a string giving the keyword associate with CODE.  */

static const char *
tag_name (enum tag_types code)
{
  switch (code)
    {
    case record_type:
      return "struct";
    case class_type:
      return "class";
    case union_type:
      return "union";
    case enum_type:
      return "enum";
    case typename_type:
      return "typename";
    default:
      gcc_unreachable ();
    }
}

/* Name lookup in an elaborated-type-specifier (after the keyword
   indicated by TAG_CODE) has found the TYPE_DECL DECL.  If the
   elaborated-type-specifier is invalid, issue a diagnostic and return
   error_mark_node; otherwise, return the *_TYPE to which it referred.
   If ALLOW_TEMPLATE_P is true, TYPE may be a class template.  */

tree
check_elaborated_type_specifier (enum tag_types tag_code,
				 tree decl,
				 bool allow_template_p)
{
  tree type;

  /* In the case of:

       struct S { struct S *p; };

     name lookup will find the TYPE_DECL for the implicit "S::S"
     typedef.  Adjust for that here.  */
  if (DECL_SELF_REFERENCE_P (decl))
    decl = TYPE_NAME (TREE_TYPE (decl));

  type = TREE_TYPE (decl);

  /* Check TEMPLATE_TYPE_PARM first because DECL_IMPLICIT_TYPEDEF_P
     is false for this case as well.  */
  if (TREE_CODE (type) == TEMPLATE_TYPE_PARM)
    {
      error ("using template type parameter %qT after %qs",
	     type, tag_name (tag_code));
      return error_mark_node;
    }
  /*   [dcl.type.elab]

       If the identifier resolves to a typedef-name or a template
       type-parameter, the elaborated-type-specifier is ill-formed.

     In other words, the only legitimate declaration to use in the
     elaborated type specifier is the implicit typedef created when
     the type is declared.  */
  else if (!DECL_IMPLICIT_TYPEDEF_P (decl)
	   && tag_code != typename_type)
    {
      error ("using typedef-name %qD after %qs", decl, tag_name (tag_code));
      error ("%q+D has a previous declaration here", decl);
      return error_mark_node;
    }
  else if (TREE_CODE (type) != RECORD_TYPE
	   && TREE_CODE (type) != UNION_TYPE
	   && tag_code != enum_type
	   && tag_code != typename_type)
    {
      error ("%qT referred to as %qs", type, tag_name (tag_code));
      error ("%q+T has a previous declaration here", type);
      return error_mark_node;
    }
  else if (TREE_CODE (type) != ENUMERAL_TYPE
	   && tag_code == enum_type)
    {
      error ("%qT referred to as enum", type);
      error ("%q+T has a previous declaration here", type);
      return error_mark_node;
    }
  else if (!allow_template_p
	   && TREE_CODE (type) == RECORD_TYPE
	   && CLASSTYPE_IS_TEMPLATE (type))
    {
      /* If a class template appears as elaborated type specifier
	 without a template header such as:

	   template <class T> class C {};
	   void f(class C);		// No template header here

	 then the required template argument is missing.  */
      error ("template argument required for %<%s %T%>",
	     tag_name (tag_code),
	     DECL_NAME (CLASSTYPE_TI_TEMPLATE (type)));
      return error_mark_node;
    }

  return type;
}

/* Lookup NAME in elaborate type specifier in scope according to
   SCOPE and issue diagnostics if necessary.
   Return *_TYPE node upon success, NULL_TREE when the NAME is not
   found, and ERROR_MARK_NODE for type error.  */

static tree
lookup_and_check_tag (enum tag_types tag_code, tree name,
		      tag_scope scope, bool template_header_p)
{
  tree t;
  tree decl;
  if (scope == ts_global)
    {
      /* First try ordinary name lookup, ignoring hidden class name
	 injected via friend declaration.  */
      decl = lookup_name_prefer_type (name, 2);
      /* If that fails, the name will be placed in the smallest
	 non-class, non-function-prototype scope according to 3.3.1/5.
	 We may already have a hidden name declared as friend in this
	 scope.  So lookup again but not ignoring hidden names.
	 If we find one, that name will be made visible rather than
	 creating a new tag.  */
      if (!decl)
	decl = lookup_type_scope (name, ts_within_enclosing_non_class);
    }
  else
    decl = lookup_type_scope (name, scope);

  if (decl && DECL_CLASS_TEMPLATE_P (decl))
    decl = DECL_TEMPLATE_RESULT (decl);

  if (decl && TREE_CODE (decl) == TYPE_DECL)
    {
      /* Look for invalid nested type:
	   class C {
	     class C {};
	   };  */
      if (scope == ts_current && DECL_SELF_REFERENCE_P (decl))
	{
	  error ("%qD has the same name as the class in which it is "
		 "declared",
		 decl);
	  return error_mark_node;
	}

      /* Two cases we need to consider when deciding if a class
	 template is allowed as an elaborated type specifier:
	 1. It is a self reference to its own class.
	 2. It comes with a template header.

	 For example:

	   template <class T> class C {
	     class C *c1;		// DECL_SELF_REFERENCE_P is true
	     class D;
	   };
	   template <class U> class C; // template_header_p is true
	   template <class T> class C<T>::D {
	     class C *c2;		// DECL_SELF_REFERENCE_P is true
	   };  */

      t = check_elaborated_type_specifier (tag_code,
					   decl,
					   template_header_p
					   | DECL_SELF_REFERENCE_P (decl));
      return t;
    }
  else
    return NULL_TREE;
}

/* Get the struct, enum or union (TAG_CODE says which) with tag NAME.
   Define the tag as a forward-reference if it is not defined.

   If a declaration is given, process it here, and report an error if
   multiple declarations are not identical.

   SCOPE is TS_CURRENT when this is also a definition.  Only look in
   the current frame for the name (since C++ allows new names in any
   scope.)  It is TS_WITHIN_ENCLOSING_NON_CLASS if this is a friend
   declaration.  Only look beginning from the current scope outward up
   till the nearest non-class scope.  Otherwise it is TS_GLOBAL.

   TEMPLATE_HEADER_P is true when this declaration is preceded by
   a set of template parameters.  */

tree
xref_tag (enum tag_types tag_code, tree name,
	  tag_scope scope, bool template_header_p)
{
  enum tree_code code;
  tree t;
  tree context = NULL_TREE;

  timevar_push (TV_NAME_LOOKUP);

  gcc_assert (TREE_CODE (name) == IDENTIFIER_NODE);

  switch (tag_code)
    {
    case record_type:
    case class_type:
      code = RECORD_TYPE;
      break;
    case union_type:
      code = UNION_TYPE;
      break;
    case enum_type:
      code = ENUMERAL_TYPE;
      break;
    default:
      gcc_unreachable ();
    }

  /* In case of anonymous name, xref_tag is only called to
     make type node and push name.  Name lookup is not required.  */
  if (ANON_AGGRNAME_P (name))
    t = NULL_TREE;
  else
    t = lookup_and_check_tag  (tag_code, name,
			       scope, template_header_p);

  if (t == error_mark_node)
    POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, error_mark_node);

  if (scope != ts_current && t && current_class_type
      && template_class_depth (current_class_type)
      && template_header_p)
    {
      /* Since SCOPE is not TS_CURRENT, we are not looking at a
	 definition of this tag.  Since, in addition, we are currently
	 processing a (member) template declaration of a template
	 class, we must be very careful; consider:

	   template <class X>
	   struct S1

	   template <class U>
	   struct S2
	   { template <class V>
	   friend struct S1; };

	 Here, the S2::S1 declaration should not be confused with the
	 outer declaration.  In particular, the inner version should
	 have a template parameter of level 2, not level 1.  This
	 would be particularly important if the member declaration
	 were instead:

	   template <class V = U> friend struct S1;

	 say, when we should tsubst into `U' when instantiating
	 S2.  On the other hand, when presented with:

	   template <class T>
	   struct S1 {
	     template <class U>
	     struct S2 {};
	     template <class U>
	     friend struct S2;
	   };

	 we must find the inner binding eventually.  We
	 accomplish this by making sure that the new type we
	 create to represent this declaration has the right
	 TYPE_CONTEXT.  */
      context = TYPE_CONTEXT (t);
      t = NULL_TREE;
    }

  if (! t)
    {
      /* If no such tag is yet defined, create a forward-reference node
	 and record it as the "definition".
	 When a real declaration of this type is found,
	 the forward-reference will be altered into a real type.  */
      if (code == ENUMERAL_TYPE)
	{
	  error ("use of enum %q#D without previous declaration", name);
	  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, error_mark_node);
	}
      else
	{
	  t = make_aggr_type (code);
	  TYPE_CONTEXT (t) = context;
	  t = pushtag (name, t, scope);
	}
    }
  else
    {
      if (template_header_p && IS_AGGR_TYPE (t))
        {
	  if (!redeclare_class_template (t, current_template_parms))
            POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, error_mark_node);
        }
      else if (!processing_template_decl
	       && CLASS_TYPE_P (t)
	       && CLASSTYPE_IS_TEMPLATE (t))
	{
	  error ("redeclaration of %qT as a non-template", t);
	  error ("previous declaration %q+D", t);
	  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, error_mark_node);
	}

      /* Make injected friend class visible.  */
      if (scope != ts_within_enclosing_non_class
	  && hidden_name_p (TYPE_NAME (t)))
	{
	  DECL_ANTICIPATED (TYPE_NAME (t)) = 0;
	  DECL_FRIEND_P (TYPE_NAME (t)) = 0;

	  if (TYPE_TEMPLATE_INFO (t))
	    {
	      DECL_ANTICIPATED (TYPE_TI_TEMPLATE (t)) = 0;
	      DECL_FRIEND_P (TYPE_TI_TEMPLATE (t)) = 0;
	    }
	}
    }

  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, t);
}

tree
xref_tag_from_type (tree old, tree id, tag_scope scope)
{
  enum tag_types tag_kind;

  if (TREE_CODE (old) == RECORD_TYPE)
    tag_kind = (CLASSTYPE_DECLARED_CLASS (old) ? class_type : record_type);
  else
    tag_kind  = union_type;

  if (id == NULL_TREE)
    id = TYPE_IDENTIFIER (old);

  return xref_tag (tag_kind, id, scope, false);
}

/* Create the binfo hierarchy for REF with (possibly NULL) base list
   BASE_LIST.  For each element on BASE_LIST the TREE_PURPOSE is an
   access_* node, and the TREE_VALUE is the type of the base-class.
   Non-NULL TREE_TYPE indicates virtual inheritance.  
 
   Returns true if the binfo heirarchy was successfully created,
   false if an error was detected. */

bool
xref_basetypes (tree ref, tree base_list)
{
  tree *basep;
  tree binfo, base_binfo;
  unsigned max_vbases = 0; /* Maximum direct & indirect virtual bases.  */
  unsigned max_bases = 0;  /* Maximum direct bases.  */
  int i;
  tree default_access;
  tree igo_prev; /* Track Inheritance Graph Order.  */

  if (ref == error_mark_node)
    return false;

  /* The base of a derived class is private by default, all others are
     public.  */
  default_access = (TREE_CODE (ref) == RECORD_TYPE
		    && CLASSTYPE_DECLARED_CLASS (ref)
		    ? access_private_node : access_public_node);

  /* First, make sure that any templates in base-classes are
     instantiated.  This ensures that if we call ourselves recursively
     we do not get confused about which classes are marked and which
     are not.  */
  basep = &base_list;
  while (*basep)
    {
      tree basetype = TREE_VALUE (*basep);

      if (!(processing_template_decl && uses_template_parms (basetype))
	  && !complete_type_or_else (basetype, NULL))
	/* An incomplete type.  Remove it from the list.  */
	*basep = TREE_CHAIN (*basep);
      else
	{
	  max_bases++;
	  if (TREE_TYPE (*basep))
	    max_vbases++;
	  if (CLASS_TYPE_P (basetype))
	    max_vbases += VEC_length (tree, CLASSTYPE_VBASECLASSES (basetype));
	  basep = &TREE_CHAIN (*basep);
	}
    }

  TYPE_MARKED_P (ref) = 1;

  /* The binfo slot should be empty, unless this is an (ill-formed)
     redefinition.  */
  gcc_assert (!TYPE_BINFO (ref) || TYPE_SIZE (ref));
  gcc_assert (TYPE_MAIN_VARIANT (ref) == ref);

  binfo = make_tree_binfo (max_bases);

  TYPE_BINFO (ref) = binfo;
  BINFO_OFFSET (binfo) = size_zero_node;
  BINFO_TYPE (binfo) = ref;

  if (max_bases)
    {
      BINFO_BASE_ACCESSES (binfo) = VEC_alloc (tree, gc, max_bases);
      /* An aggregate cannot have baseclasses.  */
      CLASSTYPE_NON_AGGREGATE (ref) = 1;

      if (TREE_CODE (ref) == UNION_TYPE)
        {
	  error ("derived union %qT invalid", ref);
          return false;
        }
    }

  if (max_bases > 1)
    {
      if (TYPE_FOR_JAVA (ref))
        {
	  error ("Java class %qT cannot have multiple bases", ref);
          return false;
        }
    }

  if (max_vbases)
    {
      CLASSTYPE_VBASECLASSES (ref) = VEC_alloc (tree, gc, max_vbases);

      if (TYPE_FOR_JAVA (ref))
        {
	  error ("Java class %qT cannot have virtual bases", ref);
          return false;
        }
    }

  for (igo_prev = binfo; base_list; base_list = TREE_CHAIN (base_list))
    {
      tree access = TREE_PURPOSE (base_list);
      int via_virtual = TREE_TYPE (base_list) != NULL_TREE;
      tree basetype = TREE_VALUE (base_list);

      if (access == access_default_node)
	access = default_access;

      if (TREE_CODE (basetype) == TYPE_DECL)
	basetype = TREE_TYPE (basetype);
      if (TREE_CODE (basetype) != RECORD_TYPE
	  && TREE_CODE (basetype) != TYPENAME_TYPE
	  && TREE_CODE (basetype) != TEMPLATE_TYPE_PARM
	  && TREE_CODE (basetype) != BOUND_TEMPLATE_TEMPLATE_PARM)
	{
	  error ("base type %qT fails to be a struct or class type",
		 basetype);
	  return false;
	}

      if (TYPE_FOR_JAVA (basetype) && (current_lang_depth () == 0))
	TYPE_FOR_JAVA (ref) = 1;

      base_binfo = NULL_TREE;
      if (CLASS_TYPE_P (basetype) && !dependent_type_p (basetype))
	{
	  base_binfo = TYPE_BINFO (basetype);
	  /* The original basetype could have been a typedef'd type.  */
	  basetype = BINFO_TYPE (base_binfo);

	  /* Inherit flags from the base.  */
	  TYPE_HAS_NEW_OPERATOR (ref)
	    |= TYPE_HAS_NEW_OPERATOR (basetype);
	  TYPE_HAS_ARRAY_NEW_OPERATOR (ref)
	    |= TYPE_HAS_ARRAY_NEW_OPERATOR (basetype);
	  TYPE_GETS_DELETE (ref) |= TYPE_GETS_DELETE (basetype);
	  TYPE_HAS_CONVERSION (ref) |= TYPE_HAS_CONVERSION (basetype);
	  CLASSTYPE_DIAMOND_SHAPED_P (ref)
	    |= CLASSTYPE_DIAMOND_SHAPED_P (basetype);
	  CLASSTYPE_REPEATED_BASE_P (ref)
	    |= CLASSTYPE_REPEATED_BASE_P (basetype);
	}

      /* We must do this test after we've seen through a typedef
	 type.  */
      if (TYPE_MARKED_P (basetype))
	{
	  if (basetype == ref)
	    error ("recursive type %qT undefined", basetype);
	  else
	    error ("duplicate base type %qT invalid", basetype);
	  return false;
	}
      TYPE_MARKED_P (basetype) = 1;

      base_binfo = copy_binfo (base_binfo, basetype, ref,
			       &igo_prev, via_virtual);
      if (!BINFO_INHERITANCE_CHAIN (base_binfo))
	BINFO_INHERITANCE_CHAIN (base_binfo) = binfo;

      BINFO_BASE_APPEND (binfo, base_binfo);
      BINFO_BASE_ACCESS_APPEND (binfo, access);
    }

  if (VEC_space (tree, CLASSTYPE_VBASECLASSES (ref), 1))
    /* If we have space in the vbase vector, we must have shared at
       least one of them, and are therefore diamond shaped.  */
    CLASSTYPE_DIAMOND_SHAPED_P (ref) = 1;

  /* Unmark all the types.  */
  for (i = 0; BINFO_BASE_ITERATE (binfo, i, base_binfo); i++)
    TYPE_MARKED_P (BINFO_TYPE (base_binfo)) = 0;
  TYPE_MARKED_P (ref) = 0;

  /* Now see if we have a repeated base type.  */
  if (!CLASSTYPE_REPEATED_BASE_P (ref))
    {
      for (base_binfo = binfo; base_binfo;
	   base_binfo = TREE_CHAIN (base_binfo))
	{
	  if (TYPE_MARKED_P (BINFO_TYPE (base_binfo)))
	    {
	      CLASSTYPE_REPEATED_BASE_P (ref) = 1;
	      break;
	    }
	  TYPE_MARKED_P (BINFO_TYPE (base_binfo)) = 1;
	}
      for (base_binfo = binfo; base_binfo;
	   base_binfo = TREE_CHAIN (base_binfo))
	if (TYPE_MARKED_P (BINFO_TYPE (base_binfo)))
	  TYPE_MARKED_P (BINFO_TYPE (base_binfo)) = 0;
	else
	  break;
    }

  return true;
}


/* Begin compiling the definition of an enumeration type.
   NAME is its name.
   Returns the type object, as yet incomplete.
   Also records info about it so that build_enumerator
   may be used to declare the individual values as they are read.  */

tree
start_enum (tree name)
{
  tree enumtype;

  gcc_assert (TREE_CODE (name) == IDENTIFIER_NODE);

  /* If this is the real definition for a previous forward reference,
     fill in the contents in the same object that used to be the
     forward reference.  */

  enumtype = lookup_and_check_tag (enum_type, name,
				   /*tag_scope=*/ts_current,
				   /*template_header_p=*/false);

  if (enumtype != NULL_TREE && TREE_CODE (enumtype) == ENUMERAL_TYPE)
    {
      error ("multiple definition of %q#T", enumtype);
      error ("%Jprevious definition here", TYPE_MAIN_DECL (enumtype));
      /* Clear out TYPE_VALUES, and start again.  */
      TYPE_VALUES (enumtype) = NULL_TREE;
    }
  else
    {
      /* In case of error, make a dummy enum to allow parsing to
	 continue.  */
      if (enumtype == error_mark_node)
	name = make_anon_name ();

      enumtype = make_node (ENUMERAL_TYPE);
      enumtype = pushtag (name, enumtype, /*tag_scope=*/ts_current);
    }

  return enumtype;
}

/* After processing and defining all the values of an enumeration type,
   install their decls in the enumeration type and finish it off.
   ENUMTYPE is the type object and VALUES a list of name-value pairs.  */

void
finish_enum (tree enumtype)
{
  tree values;
  tree decl;
  tree value;
  tree minnode;
  tree maxnode;
  tree t;
  bool unsignedp;
  bool use_short_enum;
  int lowprec;
  int highprec;
  int precision;
  integer_type_kind itk;
  tree underlying_type = NULL_TREE;

  /* We built up the VALUES in reverse order.  */
  TYPE_VALUES (enumtype) = nreverse (TYPE_VALUES (enumtype));

  /* For an enum defined in a template, just set the type of the values;
     all further processing is postponed until the template is
     instantiated.  We need to set the type so that tsubst of a CONST_DECL
     works.  */
  if (processing_template_decl)
    {
      for (values = TYPE_VALUES (enumtype);
	   values;
	   values = TREE_CHAIN (values))
	TREE_TYPE (TREE_VALUE (values)) = enumtype;
      if (at_function_scope_p ())
	add_stmt (build_min (TAG_DEFN, enumtype));
      return;
    }

  /* Determine the minimum and maximum values of the enumerators.  */
  if (TYPE_VALUES (enumtype))
    {
      minnode = maxnode = NULL_TREE;

      for (values = TYPE_VALUES (enumtype);
	   values;
	   values = TREE_CHAIN (values))
	{
	  decl = TREE_VALUE (values);

	  /* [dcl.enum]: Following the closing brace of an enum-specifier,
	     each enumerator has the type of its enumeration.  Prior to the
	     closing brace, the type of each enumerator is the type of its
	     initializing value.  */
	  TREE_TYPE (decl) = enumtype;

	  /* Update the minimum and maximum values, if appropriate.  */
	  value = DECL_INITIAL (decl);
	  if (value == error_mark_node)
	    value = integer_zero_node;
	  /* Figure out what the minimum and maximum values of the
	     enumerators are.  */
	  if (!minnode)
	    minnode = maxnode = value;
	  else if (tree_int_cst_lt (maxnode, value))
	    maxnode = value;
	  else if (tree_int_cst_lt (value, minnode))
	    minnode = value;
	}
    }
  else
    /* [dcl.enum]

       If the enumerator-list is empty, the underlying type is as if
       the enumeration had a single enumerator with value 0.  */
    minnode = maxnode = integer_zero_node;

  /* Compute the number of bits require to represent all values of the
     enumeration.  We must do this before the type of MINNODE and
     MAXNODE are transformed, since min_precision relies on the
     TREE_TYPE of the value it is passed.  */
  unsignedp = tree_int_cst_sgn (minnode) >= 0;
  lowprec = min_precision (minnode, unsignedp);
  highprec = min_precision (maxnode, unsignedp);
  precision = MAX (lowprec, highprec);

  /* Determine the underlying type of the enumeration.

       [dcl.enum]

       The underlying type of an enumeration is an integral type that
       can represent all the enumerator values defined in the
       enumeration.  It is implementation-defined which integral type is
       used as the underlying type for an enumeration except that the
       underlying type shall not be larger than int unless the value of
       an enumerator cannot fit in an int or unsigned int.

     We use "int" or an "unsigned int" as the underlying type, even if
     a smaller integral type would work, unless the user has
     explicitly requested that we use the smallest possible type.  The
     user can request that for all enumerations with a command line
     flag, or for just one enumeration with an attribute.  */

  use_short_enum = flag_short_enums
    || lookup_attribute ("packed", TYPE_ATTRIBUTES (enumtype));

  for (itk = (use_short_enum ? itk_char : itk_int);
       itk != itk_none;
       itk++)
    {
      underlying_type = integer_types[itk];
      if (TYPE_PRECISION (underlying_type) >= precision
	  && TYPE_UNSIGNED (underlying_type) == unsignedp)
	break;
    }
  if (itk == itk_none)
    {
      /* DR 377

	 IF no integral type can represent all the enumerator values, the
	 enumeration is ill-formed.  */
      error ("no integral type can represent all of the enumerator values "
	     "for %qT", enumtype);
      precision = TYPE_PRECISION (long_long_integer_type_node);
      underlying_type = integer_types[itk_unsigned_long_long];
    }

  /* Compute the minium and maximum values for the type.

     [dcl.enum]

     For an enumeration where emin is the smallest enumerator and emax
     is the largest, the values of the enumeration are the values of the
     underlying type in the range bmin to bmax, where bmin and bmax are,
     respectively, the smallest and largest values of the smallest bit-
     field that can store emin and emax.  */

  /* The middle-end currently assumes that types with TYPE_PRECISION
     narrower than their underlying type are suitably zero or sign
     extended to fill their mode.  g++ doesn't make these guarantees.
     Until the middle-end can represent such paradoxical types, we
     set the TYPE_PRECISION to the width of the underlying type.  */
  TYPE_PRECISION (enumtype) = TYPE_PRECISION (underlying_type);

  set_min_and_max_values_for_integral_type (enumtype, precision, unsignedp);

  /* [dcl.enum]

     The value of sizeof() applied to an enumeration type, an object
     of an enumeration type, or an enumerator, is the value of sizeof()
     applied to the underlying type.  */
  TYPE_SIZE (enumtype) = TYPE_SIZE (underlying_type);
  TYPE_SIZE_UNIT (enumtype) = TYPE_SIZE_UNIT (underlying_type);
  TYPE_MODE (enumtype) = TYPE_MODE (underlying_type);
  TYPE_ALIGN (enumtype) = TYPE_ALIGN (underlying_type);
  TYPE_USER_ALIGN (enumtype) = TYPE_USER_ALIGN (underlying_type);
  TYPE_UNSIGNED (enumtype) = TYPE_UNSIGNED (underlying_type);

  /* Convert each of the enumerators to the type of the underlying
     type of the enumeration.  */
  for (values = TYPE_VALUES (enumtype); values; values = TREE_CHAIN (values))
    {
      location_t saved_location;

      decl = TREE_VALUE (values);
      saved_location = input_location;
      input_location = DECL_SOURCE_LOCATION (decl);
      value = perform_implicit_conversion (underlying_type,
					   DECL_INITIAL (decl));
      input_location = saved_location;

      /* Do not clobber shared ints.  */
      value = copy_node (value);

      TREE_TYPE (value) = enumtype;
      DECL_INITIAL (decl) = value;
      TREE_VALUE (values) = value;
    }

  /* Fix up all variant types of this enum type.  */
  for (t = TYPE_MAIN_VARIANT (enumtype); t; t = TYPE_NEXT_VARIANT (t))
    {
      TYPE_VALUES (t) = TYPE_VALUES (enumtype);
      TYPE_MIN_VALUE (t) = TYPE_MIN_VALUE (enumtype);
      TYPE_MAX_VALUE (t) = TYPE_MAX_VALUE (enumtype);
      TYPE_SIZE (t) = TYPE_SIZE (enumtype);
      TYPE_SIZE_UNIT (t) = TYPE_SIZE_UNIT (enumtype);
      TYPE_MODE (t) = TYPE_MODE (enumtype);
      TYPE_PRECISION (t) = TYPE_PRECISION (enumtype);
      TYPE_ALIGN (t) = TYPE_ALIGN (enumtype);
      TYPE_USER_ALIGN (t) = TYPE_USER_ALIGN (enumtype);
      TYPE_UNSIGNED (t) = TYPE_UNSIGNED (enumtype);
    }

  /* Finish debugging output for this type.  */
  rest_of_type_compilation (enumtype, namespace_bindings_p ());
}

/* Build and install a CONST_DECL for an enumeration constant of the
   enumeration type ENUMTYPE whose NAME and VALUE (if any) are provided.
   Assignment of sequential values by default is handled here.  */

void
build_enumerator (tree name, tree value, tree enumtype)
{
  tree decl;
  tree context;
  tree type;

  /* If the VALUE was erroneous, pretend it wasn't there; that will
     result in the enum being assigned the next value in sequence.  */
  if (value == error_mark_node)
    value = NULL_TREE;

  /* Remove no-op casts from the value.  */
  if (value)
    STRIP_TYPE_NOPS (value);

  if (! processing_template_decl)
    {
      /* Validate and default VALUE.  */
      if (value != NULL_TREE)
	{
	  value = integral_constant_value (value);

	  if (TREE_CODE (value) == INTEGER_CST)
	    {
	      value = perform_integral_promotions (value);
	      constant_expression_warning (value);
	    }
	  else
	    {
	      error ("enumerator value for %qD not integer constant", name);
	      value = NULL_TREE;
	    }
	}

      /* Default based on previous value.  */
      if (value == NULL_TREE)
	{
	  if (TYPE_VALUES (enumtype))
	    {
	      HOST_WIDE_INT hi;
	      unsigned HOST_WIDE_INT lo;
	      tree prev_value;
	      bool overflowed;

	      /* The next value is the previous value plus one.  We can
		 safely assume that the previous value is an INTEGER_CST.
		 add_double doesn't know the type of the target expression,
		 so we must check with int_fits_type_p as well.  */
	      prev_value = DECL_INITIAL (TREE_VALUE (TYPE_VALUES (enumtype)));
	      overflowed = add_double (TREE_INT_CST_LOW (prev_value),
				       TREE_INT_CST_HIGH (prev_value),
				       1, 0, &lo, &hi);
	      value = build_int_cst_wide (TREE_TYPE (prev_value), lo, hi);
	      overflowed |= !int_fits_type_p (value, TREE_TYPE (prev_value));

	      if (overflowed)
		{
		  error ("overflow in enumeration values at %qD", name);
		  value = error_mark_node;
		}
	    }
	  else
	    value = integer_zero_node;
	}

      /* Remove no-op casts from the value.  */
      STRIP_TYPE_NOPS (value);
    }

  /* C++ associates enums with global, function, or class declarations.  */
  context = current_scope ();

  /* Build the actual enumeration constant.  Note that the enumeration
    constants have the type of their initializers until the
    enumeration is complete:

      [ dcl.enum ]

      Following the closing brace of an enum-specifier, each enumer-
      ator has the type of its enumeration.  Prior to the closing
      brace, the type of each enumerator is the type of its
      initializing value.

    In finish_enum we will reset the type.  Of course, if we're
    processing a template, there may be no value.  */
  type = value ? TREE_TYPE (value) : NULL_TREE;

  if (context && context == current_class_type)
    /* This enum declaration is local to the class.  We need the full
       lang_decl so that we can record DECL_CLASS_CONTEXT, for example.  */
    decl = build_lang_decl (CONST_DECL, name, type);
  else
    /* It's a global enum, or it's local to a function.  (Note local to
      a function could mean local to a class method.  */
    decl = build_decl (CONST_DECL, name, type);

  DECL_CONTEXT (decl) = FROB_CONTEXT (context);
  TREE_CONSTANT (decl) = 1;
  TREE_INVARIANT (decl) = 1;
  TREE_READONLY (decl) = 1;
  DECL_INITIAL (decl) = value;

  if (context && context == current_class_type)
    /* In something like `struct S { enum E { i = 7 }; };' we put `i'
       on the TYPE_FIELDS list for `S'.  (That's so that you can say
       things like `S::i' later.)  */
    finish_member_declaration (decl);
  else
    pushdecl (decl);

  /* Add this enumeration constant to the list for this type.  */
  TYPE_VALUES (enumtype) = tree_cons (name, decl, TYPE_VALUES (enumtype));
}


/* We're defining DECL.  Make sure that it's type is OK.  */

static void
check_function_type (tree decl, tree current_function_parms)
{
  tree fntype = TREE_TYPE (decl);
  tree return_type = complete_type (TREE_TYPE (fntype));

  /* In a function definition, arg types must be complete.  */
  require_complete_types_for_parms (current_function_parms);

  if (dependent_type_p (return_type))
    return;
  if (!COMPLETE_OR_VOID_TYPE_P (return_type))
    {
      tree args = TYPE_ARG_TYPES (fntype);

      error ("return type %q#T is incomplete", return_type);

      /* Make it return void instead.  */
      if (TREE_CODE (fntype) == METHOD_TYPE)
	fntype = build_method_type_directly (TREE_TYPE (TREE_VALUE (args)),
					     void_type_node,
					     TREE_CHAIN (args));
      else
	fntype = build_function_type (void_type_node, args);
      TREE_TYPE (decl)
	= build_exception_variant (fntype,
				   TYPE_RAISES_EXCEPTIONS (TREE_TYPE (decl)));
    }
  else
    abstract_virtuals_error (decl, TREE_TYPE (fntype));
}

/* Create the FUNCTION_DECL for a function definition.
   DECLSPECS and DECLARATOR are the parts of the declaration;
   they describe the function's name and the type it returns,
   but twisted together in a fashion that parallels the syntax of C.

   FLAGS is a bitwise or of SF_PRE_PARSED (indicating that the
   DECLARATOR is really the DECL for the function we are about to
   process and that DECLSPECS should be ignored), SF_INCLASS_INLINE
   indicating that the function is an inline defined in-class.

   This function creates a binding context for the function body
   as well as setting up the FUNCTION_DECL in current_function_decl.

   For C++, we must first check whether that datum makes any sense.
   For example, "class A local_a(1,2);" means that variable local_a
   is an aggregate of type A, which should have a constructor
   applied to it with the argument list [1, 2].  */

void
start_preparsed_function (tree decl1, tree attrs, int flags)
{
  tree ctype = NULL_TREE;
  tree fntype;
  tree restype;
  int doing_friend = 0;
  struct cp_binding_level *bl;
  tree current_function_parms;
  struct c_fileinfo *finfo
    = get_fileinfo (LOCATION_FILE (DECL_SOURCE_LOCATION (decl1)));
  bool honor_interface;

  /* Sanity check.  */
  gcc_assert (TREE_CODE (TREE_VALUE (void_list_node)) == VOID_TYPE);
  gcc_assert (TREE_CHAIN (void_list_node) == NULL_TREE);

  fntype = TREE_TYPE (decl1);
  if (TREE_CODE (fntype) == METHOD_TYPE)
    ctype = TYPE_METHOD_BASETYPE (fntype);

  /* ISO C++ 11.4/5.  A friend function defined in a class is in
     the (lexical) scope of the class in which it is defined.  */
  if (!ctype && DECL_FRIEND_P (decl1))
    {
      ctype = DECL_FRIEND_CONTEXT (decl1);

      /* CTYPE could be null here if we're dealing with a template;
	 for example, `inline friend float foo()' inside a template
	 will have no CTYPE set.  */
      if (ctype && TREE_CODE (ctype) != RECORD_TYPE)
	ctype = NULL_TREE;
      else
	doing_friend = 1;
    }

  if (DECL_DECLARED_INLINE_P (decl1)
      && lookup_attribute ("noinline", attrs))
    warning (0, "inline function %q+D given attribute noinline", decl1);

  if (DECL_MAYBE_IN_CHARGE_CONSTRUCTOR_P (decl1))
    /* This is a constructor, we must ensure that any default args
       introduced by this definition are propagated to the clones
       now. The clones are used directly in overload resolution.  */
    adjust_clone_args (decl1);

  /* Sometimes we don't notice that a function is a static member, and
     build a METHOD_TYPE for it.  Fix that up now.  */
  if (ctype != NULL_TREE && DECL_STATIC_FUNCTION_P (decl1)
      && TREE_CODE (TREE_TYPE (decl1)) == METHOD_TYPE)
    {
      revert_static_member_fn (decl1);
      ctype = NULL_TREE;
    }

  /* Set up current_class_type, and enter the scope of the class, if
     appropriate.  */
  if (ctype)
    push_nested_class (ctype);
  else if (DECL_STATIC_FUNCTION_P (decl1))
    push_nested_class (DECL_CONTEXT (decl1));

  /* Now that we have entered the scope of the class, we must restore
     the bindings for any template parameters surrounding DECL1, if it
     is an inline member template.  (Order is important; consider the
     case where a template parameter has the same name as a field of
     the class.)  It is not until after this point that
     PROCESSING_TEMPLATE_DECL is guaranteed to be set up correctly.  */
  if (flags & SF_INCLASS_INLINE)
    maybe_begin_member_template_processing (decl1);

  /* Effective C++ rule 15.  */
  if (warn_ecpp
      && DECL_OVERLOADED_OPERATOR_P (decl1) == NOP_EXPR
      && TREE_CODE (TREE_TYPE (fntype)) == VOID_TYPE)
    warning (OPT_Weffc__, "%<operator=%> should return a reference to %<*this%>");

  /* Make the init_value nonzero so pushdecl knows this is not tentative.
     error_mark_node is replaced below (in poplevel) with the BLOCK.  */
  if (!DECL_INITIAL (decl1))
    DECL_INITIAL (decl1) = error_mark_node;

  /* This function exists in static storage.
     (This does not mean `static' in the C sense!)  */
  TREE_STATIC (decl1) = 1;

  /* We must call push_template_decl after current_class_type is set
     up.  (If we are processing inline definitions after exiting a
     class scope, current_class_type will be NULL_TREE until set above
     by push_nested_class.)  */
  if (processing_template_decl)
    {
      /* FIXME: Handle error_mark_node more gracefully.  */
      tree newdecl1 = push_template_decl (decl1);
      if (newdecl1 != error_mark_node)
	decl1 = newdecl1;
    }

  /* We are now in the scope of the function being defined.  */
  current_function_decl = decl1;

  /* Save the parm names or decls from this function's declarator
     where store_parm_decls will find them.  */
  current_function_parms = DECL_ARGUMENTS (decl1);

  /* Make sure the parameter and return types are reasonable.  When
     you declare a function, these types can be incomplete, but they
     must be complete when you define the function.  */
  check_function_type (decl1, current_function_parms);

  /* Build the return declaration for the function.  */
  restype = TREE_TYPE (fntype);
  /* Promote the value to int before returning it.  */
  if (c_promoting_integer_type_p (restype))
    restype = type_promotes_to (restype);
  if (DECL_RESULT (decl1) == NULL_TREE)
    {
      tree resdecl;

      resdecl = build_decl (RESULT_DECL, 0, TYPE_MAIN_VARIANT (restype));
      DECL_ARTIFICIAL (resdecl) = 1;
      DECL_IGNORED_P (resdecl) = 1;
      DECL_RESULT (decl1) = resdecl;

      cp_apply_type_quals_to_decl (cp_type_quals (restype), resdecl);
    }

  /* Initialize RTL machinery.  We cannot do this until
     CURRENT_FUNCTION_DECL and DECL_RESULT are set up.  We do this
     even when processing a template; this is how we get
     CFUN set up, and our per-function variables initialized.
     FIXME factor out the non-RTL stuff.  */
  bl = current_binding_level;
  allocate_struct_function (decl1);
  current_binding_level = bl;

  /* Even though we're inside a function body, we still don't want to
     call expand_expr to calculate the size of a variable-sized array.
     We haven't necessarily assigned RTL to all variables yet, so it's
     not safe to try to expand expressions involving them.  */
  cfun->x_dont_save_pending_sizes_p = 1;

  /* Start the statement-tree, start the tree now.  */
  DECL_SAVED_TREE (decl1) = push_stmt_list ();

  /* Let the user know we're compiling this function.  */
  announce_function (decl1);

  /* Record the decl so that the function name is defined.
     If we already have a decl for this name, and it is a FUNCTION_DECL,
     use the old decl.  */
  if (!processing_template_decl && !(flags & SF_PRE_PARSED))
    {
      /* A specialization is not used to guide overload resolution.  */
      if (!DECL_FUNCTION_MEMBER_P (decl1)
	  && !(DECL_USE_TEMPLATE (decl1) &&
	       PRIMARY_TEMPLATE_P (DECL_TI_TEMPLATE (decl1))))
	{
	  tree olddecl = pushdecl (decl1);

	  if (olddecl == error_mark_node)
	    /* If something went wrong when registering the declaration,
	       use DECL1; we have to have a FUNCTION_DECL to use when
	       parsing the body of the function.  */
	    ;
	  else
	    /* Otherwise, OLDDECL is either a previous declaration of
	       the same function or DECL1 itself.  */
	    decl1 = olddecl;
	}
      else
	{
	  /* We need to set the DECL_CONTEXT.  */
	  if (!DECL_CONTEXT (decl1) && DECL_TEMPLATE_INFO (decl1))
	    DECL_CONTEXT (decl1) = DECL_CONTEXT (DECL_TI_TEMPLATE (decl1));
	}
      fntype = TREE_TYPE (decl1);

      /* If #pragma weak applies, mark the decl appropriately now.
	 The pragma only applies to global functions.  Because
	 determining whether or not the #pragma applies involves
	 computing the mangled name for the declaration, we cannot
	 apply the pragma until after we have merged this declaration
	 with any previous declarations; if the original declaration
	 has a linkage specification, that specification applies to
	 the definition as well, and may affect the mangled name.  */
      if (!DECL_CONTEXT (decl1))
	maybe_apply_pragma_weak (decl1);
    }

  /* Reset these in case the call to pushdecl changed them.  */
  current_function_decl = decl1;
  cfun->decl = decl1;

  /* If we are (erroneously) defining a function that we have already
     defined before, wipe out what we knew before.  */
  if (!DECL_PENDING_INLINE_P (decl1))
    DECL_SAVED_FUNCTION_DATA (decl1) = NULL;

  if (ctype && !doing_friend && !DECL_STATIC_FUNCTION_P (decl1))
    {
      /* We know that this was set up by `grokclassfn'.  We do not
	 wait until `store_parm_decls', since evil parse errors may
	 never get us to that point.  Here we keep the consistency
	 between `current_class_type' and `current_class_ptr'.  */
      tree t = DECL_ARGUMENTS (decl1);

      gcc_assert (t != NULL_TREE && TREE_CODE (t) == PARM_DECL);
      gcc_assert (TREE_CODE (TREE_TYPE (t)) == POINTER_TYPE);

      cp_function_chain->x_current_class_ref
	= build_indirect_ref (t, NULL);
      cp_function_chain->x_current_class_ptr = t;

      /* Constructors and destructors need to know whether they're "in
	 charge" of initializing virtual base classes.  */
      t = TREE_CHAIN (t);
      if (DECL_HAS_IN_CHARGE_PARM_P (decl1))
	{
	  current_in_charge_parm = t;
	  t = TREE_CHAIN (t);
	}
      if (DECL_HAS_VTT_PARM_P (decl1))
	{
	  gcc_assert (DECL_NAME (t) == vtt_parm_identifier);
	  current_vtt_parm = t;
	}
    }

  honor_interface = (!DECL_TEMPLATE_INSTANTIATION (decl1)
		     /* Implicitly-defined methods (like the
			destructor for a class in which no destructor
			is explicitly declared) must not be defined
			until their definition is needed.  So, we
			ignore interface specifications for
			compiler-generated functions.  */
		     && !DECL_ARTIFICIAL (decl1));

  if (DECL_INTERFACE_KNOWN (decl1))
    {
      tree ctx = decl_function_context (decl1);

      if (DECL_NOT_REALLY_EXTERN (decl1))
	DECL_EXTERNAL (decl1) = 0;

      if (ctx != NULL_TREE && DECL_DECLARED_INLINE_P (ctx)
	  && TREE_PUBLIC (ctx))
	/* This is a function in a local class in an extern inline
	   function.  */
	comdat_linkage (decl1);
    }
  /* If this function belongs to an interface, it is public.
     If it belongs to someone else's interface, it is also external.
     This only affects inlines and template instantiations.  */
  else if (!finfo->interface_unknown && honor_interface)
    {
      if (DECL_DECLARED_INLINE_P (decl1)
	  || DECL_TEMPLATE_INSTANTIATION (decl1)
	  || processing_template_decl)
	{
	  DECL_EXTERNAL (decl1)
	    = (finfo->interface_only
	       || (DECL_DECLARED_INLINE_P (decl1)
		   && ! flag_implement_inlines
		   && !DECL_VINDEX (decl1)));

	  /* For WIN32 we also want to put these in linkonce sections.  */
	  maybe_make_one_only (decl1);
	}
      else
	DECL_EXTERNAL (decl1) = 0;
      DECL_INTERFACE_KNOWN (decl1) = 1;
      /* If this function is in an interface implemented in this file,
	 make sure that the backend knows to emit this function
	 here.  */
      if (!DECL_EXTERNAL (decl1))
	mark_needed (decl1);
    }
  else if (finfo->interface_unknown && finfo->interface_only
	   && honor_interface)
    {
      /* If MULTIPLE_SYMBOL_SPACES is defined and we saw a #pragma
	 interface, we will have both finfo->interface_unknown and
	 finfo->interface_only set.  In that case, we don't want to
	 use the normal heuristics because someone will supply a
	 #pragma implementation elsewhere, and deducing it here would
	 produce a conflict.  */
      comdat_linkage (decl1);
      DECL_EXTERNAL (decl1) = 0;
      DECL_INTERFACE_KNOWN (decl1) = 1;
      DECL_DEFER_OUTPUT (decl1) = 1;
    }
  else
    {
      /* This is a definition, not a reference.
	 So clear DECL_EXTERNAL.  */
      DECL_EXTERNAL (decl1) = 0;

      if ((DECL_DECLARED_INLINE_P (decl1)
	   || DECL_TEMPLATE_INSTANTIATION (decl1))
	  && ! DECL_INTERFACE_KNOWN (decl1)
	  /* Don't try to defer nested functions for now.  */
	  && ! decl_function_context (decl1))
	DECL_DEFER_OUTPUT (decl1) = 1;
      else
	DECL_INTERFACE_KNOWN (decl1) = 1;
    }

  /* Determine the ELF visibility attribute for the function.  We must not
     do this before calling "pushdecl", as we must allow "duplicate_decls"
     to merge any attributes appropriately.  We also need to wait until
     linkage is set.  */
  if (!DECL_CLONED_FUNCTION_P (decl1))
    determine_visibility (decl1);

  begin_scope (sk_function_parms, decl1);

  ++function_depth;

  if (DECL_DESTRUCTOR_P (decl1)
      || (DECL_CONSTRUCTOR_P (decl1)
	  && targetm.cxx.cdtor_returns_this ()))
    {
      cdtor_label = build_decl (LABEL_DECL, NULL_TREE, NULL_TREE);
      DECL_CONTEXT (cdtor_label) = current_function_decl;
    }

  start_fname_decls ();

  store_parm_decls (current_function_parms);
}


/* Like start_preparsed_function, except that instead of a
   FUNCTION_DECL, this function takes DECLSPECS and DECLARATOR.

   Returns 1 on success.  If the DECLARATOR is not suitable for a function
   (it defines a datum instead), we return 0, which tells
   yyparse to report a parse error.  */

int
start_function (cp_decl_specifier_seq *declspecs,
		const cp_declarator *declarator,
		tree attrs)
{
  tree decl1;

  decl1 = grokdeclarator (declarator, declspecs, FUNCDEF, 1, &attrs);
  /* If the declarator is not suitable for a function definition,
     cause a syntax error.  */
  if (decl1 == NULL_TREE || TREE_CODE (decl1) != FUNCTION_DECL)
    return 0;

  if (DECL_MAIN_P (decl1))
    /* main must return int.  grokfndecl should have corrected it
       (and issued a diagnostic) if the user got it wrong.  */
    gcc_assert (same_type_p (TREE_TYPE (TREE_TYPE (decl1)),
			     integer_type_node));

  start_preparsed_function (decl1, attrs, /*flags=*/SF_DEFAULT);

  return 1;
}

/* Returns true iff an EH_SPEC_BLOCK should be created in the body of
   FN.  */

static bool
use_eh_spec_block (tree fn)
{
  return (flag_exceptions && flag_enforce_eh_specs
	  && !processing_template_decl
	  && TYPE_RAISES_EXCEPTIONS (TREE_TYPE (fn))
	  /* We insert the EH_SPEC_BLOCK only in the original
	     function; then, it is copied automatically to the
	     clones.  */
	  && !DECL_CLONED_FUNCTION_P (fn)
	  /* Implicitly-generated constructors and destructors have
	     exception specifications.  However, those specifications
	     are the union of the possible exceptions specified by the
	     constructors/destructors for bases and members, so no
	     unallowed exception will ever reach this function.  By
	     not creating the EH_SPEC_BLOCK we save a little memory,
	     and we avoid spurious warnings about unreachable
	     code.  */
	  && !DECL_ARTIFICIAL (fn));
}

/* Store the parameter declarations into the current function declaration.
   This is called after parsing the parameter declarations, before
   digesting the body of the function.

   Also install to binding contour return value identifier, if any.  */

static void
store_parm_decls (tree current_function_parms)
{
  tree fndecl = current_function_decl;
  tree parm;

  /* This is a chain of any other decls that came in among the parm
     declarations.  If a parm is declared with  enum {foo, bar} x;
     then CONST_DECLs for foo and bar are put here.  */
  tree nonparms = NULL_TREE;

  if (current_function_parms)
    {
      /* This case is when the function was defined with an ANSI prototype.
	 The parms already have decls, so we need not do anything here
	 except record them as in effect
	 and complain if any redundant old-style parm decls were written.  */

      tree specparms = current_function_parms;
      tree next;

      /* Must clear this because it might contain TYPE_DECLs declared
	     at class level.  */
      current_binding_level->names = NULL;

      /* If we're doing semantic analysis, then we'll call pushdecl
	     for each of these.  We must do them in reverse order so that
	     they end in the correct forward order.  */
      specparms = nreverse (specparms);

      for (parm = specparms; parm; parm = next)
	{
	  next = TREE_CHAIN (parm);
	  if (TREE_CODE (parm) == PARM_DECL)
	    {
	      if (DECL_NAME (parm) == NULL_TREE
		  || TREE_CODE (parm) != VOID_TYPE)
		pushdecl (parm);
	      else
		error ("parameter %qD declared void", parm);
	    }
	  else
	    {
	      /* If we find an enum constant or a type tag,
		 put it aside for the moment.  */
	      TREE_CHAIN (parm) = NULL_TREE;
	      nonparms = chainon (nonparms, parm);
	    }
	}

      /* Get the decls in their original chain order and record in the
	 function.  This is all and only the PARM_DECLs that were
	 pushed into scope by the loop above.  */
      DECL_ARGUMENTS (fndecl) = getdecls ();
    }
  else
    DECL_ARGUMENTS (fndecl) = NULL_TREE;

  /* Now store the final chain of decls for the arguments
     as the decl-chain of the current lexical scope.
     Put the enumerators in as well, at the front so that
     DECL_ARGUMENTS is not modified.  */
  current_binding_level->names = chainon (nonparms, DECL_ARGUMENTS (fndecl));

  if (use_eh_spec_block (current_function_decl))
    current_eh_spec_block = begin_eh_spec_block ();
}


/* We have finished doing semantic analysis on DECL, but have not yet
   generated RTL for its body.  Save away our current state, so that
   when we want to generate RTL later we know what to do.  */

static void
save_function_data (tree decl)
{
  struct language_function *f;

  /* Save the language-specific per-function data so that we can
     get it back when we really expand this function.  */
  gcc_assert (!DECL_PENDING_INLINE_P (decl));

  /* Make a copy.  */
  f = GGC_NEW (struct language_function);
  memcpy (f, cp_function_chain, sizeof (struct language_function));
  DECL_SAVED_FUNCTION_DATA (decl) = f;

  /* Clear out the bits we don't need.  */
  f->base.x_stmt_tree.x_cur_stmt_list = NULL_TREE;
  f->bindings = NULL;
  f->x_local_names = NULL;
}


/* Set the return value of the constructor (if present).  */

static void
finish_constructor_body (void)
{
  tree val;
  tree exprstmt;

  if (targetm.cxx.cdtor_returns_this ())
    {
      /* Any return from a constructor will end up here.  */
      add_stmt (build_stmt (LABEL_EXPR, cdtor_label));

      val = DECL_ARGUMENTS (current_function_decl);
      val = build2 (MODIFY_EXPR, TREE_TYPE (val),
		    DECL_RESULT (current_function_decl), val);
      /* Return the address of the object.  */
      exprstmt = build_stmt (RETURN_EXPR, val);
      add_stmt (exprstmt);
    }
}

/* Do all the processing for the beginning of a destructor; set up the
   vtable pointers and cleanups for bases and members.  */

static void
begin_destructor_body (void)
{
  tree compound_stmt;

  /* If the CURRENT_CLASS_TYPE is incomplete, we will have already
     issued an error message.  We still want to try to process the
     body of the function, but initialize_vtbl_ptrs will crash if
     TYPE_BINFO is NULL.  */
  if (COMPLETE_TYPE_P (current_class_type))
    {
      compound_stmt = begin_compound_stmt (0);
      /* Make all virtual function table pointers in non-virtual base
	 classes point to CURRENT_CLASS_TYPE's virtual function
	 tables.  */
      initialize_vtbl_ptrs (current_class_ptr);
      finish_compound_stmt (compound_stmt);

      /* And insert cleanups for our bases and members so that they
	 will be properly destroyed if we throw.  */
      push_base_cleanups ();
    }
}

/* At the end of every destructor we generate code to delete the object if
   necessary.  Do that now.  */

static void
finish_destructor_body (void)
{
  tree exprstmt;

  /* Any return from a destructor will end up here; that way all base
     and member cleanups will be run when the function returns.  */
  add_stmt (build_stmt (LABEL_EXPR, cdtor_label));

  /* In a virtual destructor, we must call delete.  */
  if (DECL_VIRTUAL_P (current_function_decl))
    {
      tree if_stmt;
      tree virtual_size = cxx_sizeof (current_class_type);

      /* [class.dtor]

      At the point of definition of a virtual destructor (including
      an implicit definition), non-placement operator delete shall
      be looked up in the scope of the destructor's class and if
      found shall be accessible and unambiguous.  */
      exprstmt = build_op_delete_call(DELETE_EXPR, current_class_ptr,
				      virtual_size,
				      /*global_p=*/false,
				      /*placement=*/NULL_TREE,
				      /*alloc_fn=*/NULL_TREE);

      if_stmt = begin_if_stmt ();
      finish_if_stmt_cond (build2 (BIT_AND_EXPR, integer_type_node,
				   current_in_charge_parm,
				   integer_one_node),
			   if_stmt);
      finish_expr_stmt (exprstmt);
      finish_then_clause (if_stmt);
      finish_if_stmt (if_stmt);
    }

  if (targetm.cxx.cdtor_returns_this ())
    {
      tree val;

      val = DECL_ARGUMENTS (current_function_decl);
      val = build2 (MODIFY_EXPR, TREE_TYPE (val),
		    DECL_RESULT (current_function_decl), val);
      /* Return the address of the object.  */
      exprstmt = build_stmt (RETURN_EXPR, val);
      add_stmt (exprstmt);
    }
}

/* Do the necessary processing for the beginning of a function body, which
   in this case includes member-initializers, but not the catch clauses of
   a function-try-block.  Currently, this means opening a binding level
   for the member-initializers (in a ctor) and member cleanups (in a dtor).  */

tree
begin_function_body (void)
{
  tree stmt;

  if (! FUNCTION_NEEDS_BODY_BLOCK (current_function_decl))
    return NULL_TREE;

  if (processing_template_decl)
    /* Do nothing now.  */;
  else
    /* Always keep the BLOCK node associated with the outermost pair of
       curly braces of a function.  These are needed for correct
       operation of dwarfout.c.  */
    keep_next_level (true);

  stmt = begin_compound_stmt (BCS_FN_BODY);

  if (processing_template_decl)
    /* Do nothing now.  */;
  else if (DECL_DESTRUCTOR_P (current_function_decl))
    begin_destructor_body ();

  return stmt;
}

/* Do the processing for the end of a function body.  Currently, this means
   closing out the cleanups for fully-constructed bases and members, and in
   the case of the destructor, deleting the object if desired.  Again, this
   is only meaningful for [cd]tors, since they are the only functions where
   there is a significant distinction between the main body and any
   function catch clauses.  Handling, say, main() return semantics here
   would be wrong, as flowing off the end of a function catch clause for
   main() would also need to return 0.  */

void
finish_function_body (tree compstmt)
{
  if (compstmt == NULL_TREE)
    return;

  /* Close the block.  */
  finish_compound_stmt (compstmt);

  if (processing_template_decl)
    /* Do nothing now.  */;
  else if (DECL_CONSTRUCTOR_P (current_function_decl))
    finish_constructor_body ();
  else if (DECL_DESTRUCTOR_P (current_function_decl))
    finish_destructor_body ();
}

/* Given a function, returns the BLOCK corresponding to the outermost level
   of curly braces, skipping the artificial block created for constructor
   initializers.  */

static tree
outer_curly_brace_block (tree fndecl)
{
  tree block = BLOCK_SUBBLOCKS (DECL_INITIAL (fndecl));
  if (FUNCTION_NEEDS_BODY_BLOCK (current_function_decl))
    /* Skip the artificial function body block.  */
    block = BLOCK_SUBBLOCKS (block);
  return block;
}

/* Finish up a function declaration and compile that function
   all the way to assembler language output.  The free the storage
   for the function definition.

   FLAGS is a bitwise or of the following values:
     2 - INCLASS_INLINE
       We just finished processing the body of an in-class inline
       function definition.  (This processing will have taken place
       after the class definition is complete.)  */

tree
finish_function (int flags)
{
  tree fndecl = current_function_decl;
  tree fntype, ctype = NULL_TREE;
  int inclass_inline = (flags & 2) != 0;
  int nested;

  /* When we get some parse errors, we can end up without a
     current_function_decl, so cope.  */
  if (fndecl == NULL_TREE)
    return error_mark_node;

  if (DECL_NONSTATIC_MEMBER_FUNCTION_P (fndecl)
      && DECL_VIRTUAL_P (fndecl)
      && !processing_template_decl)
    {
      tree fnclass = DECL_CONTEXT (fndecl);
      if (fndecl == CLASSTYPE_KEY_METHOD (fnclass))
	keyed_classes = tree_cons (NULL_TREE, fnclass, keyed_classes);
    }

  nested = function_depth > 1;
  fntype = TREE_TYPE (fndecl);

  /*  TREE_READONLY (fndecl) = 1;
      This caused &foo to be of type ptr-to-const-function
      which then got a warning when stored in a ptr-to-function variable.  */

  gcc_assert (building_stmt_tree ());

  /* For a cloned function, we've already got all the code we need;
     there's no need to add any extra bits.  */
  if (!DECL_CLONED_FUNCTION_P (fndecl))
    {
      if (DECL_MAIN_P (current_function_decl))
	{
	  tree stmt;

	  /* Make it so that `main' always returns 0 by default (or
	     1 for VMS).  */
#if VMS_TARGET
	  stmt = finish_return_stmt (integer_one_node);
#else
	  stmt = finish_return_stmt (integer_zero_node);
#endif
	  /* Hack.  We don't want the middle-end to warn that this
	     return is unreachable, so put the statement on the
	     special line 0.  */
#ifdef USE_MAPPED_LOCATION
	  SET_EXPR_LOCATION (stmt, UNKNOWN_LOCATION);
#else
	  annotate_with_file_line (stmt, input_filename, 0);
#endif
	}

      if (use_eh_spec_block (current_function_decl))
	finish_eh_spec_block (TYPE_RAISES_EXCEPTIONS
			      (TREE_TYPE (current_function_decl)),
			      current_eh_spec_block);
    }

  /* If we're saving up tree structure, tie off the function now.  */
  DECL_SAVED_TREE (fndecl) = pop_stmt_list (DECL_SAVED_TREE (fndecl));

  finish_fname_decls ();

  /* If this function can't throw any exceptions, remember that.  */
  if (!processing_template_decl
      && !cp_function_chain->can_throw
      && !flag_non_call_exceptions
      && !DECL_REPLACEABLE_P (fndecl))
    TREE_NOTHROW (fndecl) = 1;

  /* This must come after expand_function_end because cleanups might
     have declarations (from inline functions) that need to go into
     this function's blocks.  */

  /* If the current binding level isn't the outermost binding level
     for this function, either there is a bug, or we have experienced
     syntax errors and the statement tree is malformed.  */
  if (current_binding_level->kind != sk_function_parms)
    {
      /* Make sure we have already experienced errors.  */
      gcc_assert (errorcount);

      /* Throw away the broken statement tree and extra binding
	 levels.  */
      DECL_SAVED_TREE (fndecl) = alloc_stmt_list ();

      while (current_binding_level->kind != sk_function_parms)
	{
	  if (current_binding_level->kind == sk_class)
	    pop_nested_class ();
	  else
	    poplevel (0, 0, 0);
	}
    }
  poplevel (1, 0, 1);

  /* Statements should always be full-expressions at the outermost set
     of curly braces for a function.  */
  gcc_assert (stmts_are_full_exprs_p ());

  /* Set up the named return value optimization, if we can.  Candidate
     variables are selected in check_return_value.  */
  if (current_function_return_value)
    {
      tree r = current_function_return_value;
      tree outer;

      if (r != error_mark_node
	  /* This is only worth doing for fns that return in memory--and
	     simpler, since we don't have to worry about promoted modes.  */
	  && aggregate_value_p (TREE_TYPE (TREE_TYPE (fndecl)), fndecl)
	  /* Only allow this for variables declared in the outer scope of
	     the function so we know that their lifetime always ends with a
	     return; see g++.dg/opt/nrv6.C.  We could be more flexible if
	     we were to do this optimization in tree-ssa.  */
	  && (outer = outer_curly_brace_block (fndecl))
	  && chain_member (r, BLOCK_VARS (outer)))
	finalize_nrv (&DECL_SAVED_TREE (fndecl), r, DECL_RESULT (fndecl));

      current_function_return_value = NULL_TREE;
    }

  /* Remember that we were in class scope.  */
  if (current_class_name)
    ctype = current_class_type;

  /* Must mark the RESULT_DECL as being in this function.  */
  DECL_CONTEXT (DECL_RESULT (fndecl)) = fndecl;

  /* Set the BLOCK_SUPERCONTEXT of the outermost function scope to point
     to the FUNCTION_DECL node itself.  */
  BLOCK_SUPERCONTEXT (DECL_INITIAL (fndecl)) = fndecl;

  /* Save away current state, if appropriate.  */
  if (!processing_template_decl)
    save_function_data (fndecl);

  /* Complain if there's just no return statement.  */
  if (warn_return_type
      && TREE_CODE (TREE_TYPE (fntype)) != VOID_TYPE
      && !dependent_type_p (TREE_TYPE (fntype))
      && !current_function_returns_value && !current_function_returns_null
      /* Don't complain if we abort or throw.  */
      && !current_function_returns_abnormally
      && !DECL_NAME (DECL_RESULT (fndecl))
      /* Normally, with -Wreturn-type, flow will complain.  Unless we're an
	 inline function, as we might never be compiled separately.  */
      && (DECL_INLINE (fndecl) || processing_template_decl)
      /* Structor return values (if any) are set by the compiler.  */
      && !DECL_CONSTRUCTOR_P (fndecl)
      && !DECL_DESTRUCTOR_P (fndecl))
    warning (OPT_Wreturn_type, "no return statement in function returning non-void");

  /* Store the end of the function, so that we get good line number
     info for the epilogue.  */
  cfun->function_end_locus = input_location;

  /* Genericize before inlining.  */
  if (!processing_template_decl)
    {
      struct language_function *f = DECL_SAVED_FUNCTION_DATA (fndecl);
      cp_genericize (fndecl);
      /* Clear out the bits we don't need.  */
      f->x_current_class_ptr = NULL;
      f->x_current_class_ref = NULL;
      f->x_eh_spec_block = NULL;
      f->x_in_charge_parm = NULL;
      f->x_vtt_parm = NULL;
      f->x_return_value = NULL;
      f->bindings = NULL;
      f->extern_decl_map = NULL;

      /* Handle attribute((warn_unused_result)).  Relies on gimple input.  */
      c_warn_unused_result (&DECL_SAVED_TREE (fndecl));
    }
  /* Clear out the bits we don't need.  */
  local_names = NULL;

  /* We're leaving the context of this function, so zap cfun.  It's still in
     DECL_STRUCT_FUNCTION, and we'll restore it in tree_rest_of_compilation.  */
  cfun = NULL;
  current_function_decl = NULL;

  /* If this is an in-class inline definition, we may have to pop the
     bindings for the template parameters that we added in
     maybe_begin_member_template_processing when start_function was
     called.  */
  if (inclass_inline)
    maybe_end_member_template_processing ();

  /* Leave the scope of the class.  */
  if (ctype)
    pop_nested_class ();

  --function_depth;

  /* Clean up.  */
  if (! nested)
    /* Let the error reporting routines know that we're outside a
       function.  For a nested function, this value is used in
       cxx_pop_function_context and then reset via pop_function_context.  */
    current_function_decl = NULL_TREE;

  return fndecl;
}

/* Create the FUNCTION_DECL for a function definition.
   DECLSPECS and DECLARATOR are the parts of the declaration;
   they describe the return type and the name of the function,
   but twisted together in a fashion that parallels the syntax of C.

   This function creates a binding context for the function body
   as well as setting up the FUNCTION_DECL in current_function_decl.

   Returns a FUNCTION_DECL on success.

   If the DECLARATOR is not suitable for a function (it defines a datum
   instead), we return 0, which tells yyparse to report a parse error.

   May return void_type_node indicating that this method is actually
   a friend.  See grokfield for more details.

   Came here with a `.pushlevel' .

   DO NOT MAKE ANY CHANGES TO THIS CODE WITHOUT MAKING CORRESPONDING
   CHANGES TO CODE IN `grokfield'.  */

tree
start_method (cp_decl_specifier_seq *declspecs,
	      const cp_declarator *declarator, tree attrlist)
{
  tree fndecl = grokdeclarator (declarator, declspecs, MEMFUNCDEF, 0,
				&attrlist);

  if (fndecl == error_mark_node)
    return error_mark_node;

  if (fndecl == NULL || TREE_CODE (fndecl) != FUNCTION_DECL)
    {
      error ("invalid member function declaration");
      return error_mark_node;
    }

  if (attrlist)
    cplus_decl_attributes (&fndecl, attrlist, 0);

  /* Pass friends other than inline friend functions back.  */
  if (fndecl == void_type_node)
    return fndecl;

  if (DECL_IN_AGGR_P (fndecl))
    {
      if (DECL_CONTEXT (fndecl)
	  && TREE_CODE (DECL_CONTEXT (fndecl)) != NAMESPACE_DECL)
	error ("%qD is already defined in class %qT", fndecl,
	       DECL_CONTEXT (fndecl));
      return error_mark_node;
    }

  check_template_shadow (fndecl);

  DECL_DECLARED_INLINE_P (fndecl) = 1;
  if (flag_default_inline)
    DECL_INLINE (fndecl) = 1;

  /* We process method specializations in finish_struct_1.  */
  if (processing_template_decl && !DECL_TEMPLATE_SPECIALIZATION (fndecl))
    {
      fndecl = push_template_decl (fndecl);
      if (fndecl == error_mark_node)
	return fndecl;
    }

  if (! DECL_FRIEND_P (fndecl))
    {
      if (TREE_CHAIN (fndecl))
	{
	  fndecl = copy_node (fndecl);
	  TREE_CHAIN (fndecl) = NULL_TREE;
	}
    }

  finish_decl (fndecl, NULL_TREE, NULL_TREE);

  /* Make a place for the parms.  */
  begin_scope (sk_function_parms, fndecl);

  DECL_IN_AGGR_P (fndecl) = 1;
  return fndecl;
}

/* Go through the motions of finishing a function definition.
   We don't compile this method until after the whole class has
   been processed.

   FINISH_METHOD must return something that looks as though it
   came from GROKFIELD (since we are defining a method, after all).

   This is called after parsing the body of the function definition.
   STMTS is the chain of statements that makes up the function body.

   DECL is the ..._DECL that `start_method' provided.  */

tree
finish_method (tree decl)
{
  tree fndecl = decl;
  tree old_initial;

  tree link;

  if (decl == void_type_node)
    return decl;

  old_initial = DECL_INITIAL (fndecl);

  /* Undo the level for the parms (from start_method).
     This is like poplevel, but it causes nothing to be
     saved.  Saving information here confuses symbol-table
     output routines.  Besides, this information will
     be correctly output when this method is actually
     compiled.  */

  /* Clear out the meanings of the local variables of this level;
     also record in each decl which block it belongs to.  */

  for (link = current_binding_level->names; link; link = TREE_CHAIN (link))
    {
      if (DECL_NAME (link) != NULL_TREE)
	pop_binding (DECL_NAME (link), link);
      gcc_assert (TREE_CODE (link) != FUNCTION_DECL);
      DECL_CONTEXT (link) = NULL_TREE;
    }

  poplevel (0, 0, 0);

  DECL_INITIAL (fndecl) = old_initial;

  /* We used to check if the context of FNDECL was different from
     current_class_type as another way to get inside here.  This didn't work
     for String.cc in libg++.  */
  if (DECL_FRIEND_P (fndecl))
    {
      VEC_safe_push (tree, gc, CLASSTYPE_INLINE_FRIENDS (current_class_type),
		     fndecl);
      decl = void_type_node;
    }

  return decl;
}


/* VAR is a VAR_DECL.  If its type is incomplete, remember VAR so that
   we can lay it out later, when and if its type becomes complete.  */

void
maybe_register_incomplete_var (tree var)
{
  gcc_assert (TREE_CODE (var) == VAR_DECL);

  /* Keep track of variables with incomplete types.  */
  if (!processing_template_decl && TREE_TYPE (var) != error_mark_node
      && DECL_EXTERNAL (var))
    {
      tree inner_type = TREE_TYPE (var);

      while (TREE_CODE (inner_type) == ARRAY_TYPE)
	inner_type = TREE_TYPE (inner_type);
      inner_type = TYPE_MAIN_VARIANT (inner_type);

      if ((!COMPLETE_TYPE_P (inner_type) && CLASS_TYPE_P (inner_type))
	  /* RTTI TD entries are created while defining the type_info.  */
	  || (TYPE_LANG_SPECIFIC (inner_type)
	      && TYPE_BEING_DEFINED (inner_type)))
	incomplete_vars = tree_cons (inner_type, var, incomplete_vars);
    }
}

/* Called when a class type (given by TYPE) is defined.  If there are
   any existing VAR_DECLs whose type hsa been completed by this
   declaration, update them now.  */

void
complete_vars (tree type)
{
  tree *list = &incomplete_vars;

  gcc_assert (CLASS_TYPE_P (type));
  while (*list)
    {
      if (same_type_p (type, TREE_PURPOSE (*list)))
	{
	  tree var = TREE_VALUE (*list);
	  tree type = TREE_TYPE (var);
	  /* Complete the type of the variable.  The VAR_DECL itself
	     will be laid out in expand_expr.  */
	  complete_type (type);
	  cp_apply_type_quals_to_decl (cp_type_quals (type), var);
	  /* Remove this entry from the list.  */
	  *list = TREE_CHAIN (*list);
	}
      else
	list = &TREE_CHAIN (*list);
    }

  /* Check for pending declarations which may have abstract type.  */
  complete_type_check_abstract (type);
}

/* If DECL is of a type which needs a cleanup, build that cleanup
   here.  */

tree
cxx_maybe_build_cleanup (tree decl)
{
  tree type = TREE_TYPE (decl);

  if (type != error_mark_node && TYPE_HAS_NONTRIVIAL_DESTRUCTOR (type))
    {
      int flags = LOOKUP_NORMAL|LOOKUP_DESTRUCTOR;
      tree rval;
      bool has_vbases = (TREE_CODE (type) == RECORD_TYPE
			 && CLASSTYPE_VBASECLASSES (type));

      if (TREE_CODE (type) == ARRAY_TYPE)
	rval = decl;
      else
	{
	  cxx_mark_addressable (decl);
	  rval = build_unary_op (ADDR_EXPR, decl, 0);
	}

      /* Optimize for space over speed here.  */
      if (!has_vbases || flag_expensive_optimizations)
	flags |= LOOKUP_NONVIRTUAL;

      rval = build_delete (TREE_TYPE (rval), rval,
			   sfk_complete_destructor, flags, 0);

      return rval;
    }
  return NULL_TREE;
}

/* When a stmt has been parsed, this function is called.  */

void
finish_stmt (void)
{
}

/* DECL was originally constructed as a non-static member function,
   but turned out to be static.  Update it accordingly.  */

void
revert_static_member_fn (tree decl)
{
  tree tmp;
  tree function = TREE_TYPE (decl);
  tree args = TYPE_ARG_TYPES (function);

  if (cp_type_quals (TREE_TYPE (TREE_VALUE (args)))
      != TYPE_UNQUALIFIED)
    error ("static member function %q#D declared with type qualifiers", decl);

  args = TREE_CHAIN (args);
  tmp = build_function_type (TREE_TYPE (function), args);
  tmp = build_qualified_type (tmp, cp_type_quals (function));
  tmp = build_exception_variant (tmp,
				 TYPE_RAISES_EXCEPTIONS (function));
  TREE_TYPE (decl) = tmp;
  if (DECL_ARGUMENTS (decl))
    DECL_ARGUMENTS (decl) = TREE_CHAIN (DECL_ARGUMENTS (decl));
  DECL_STATIC_FUNCTION_P (decl) = 1;
}

/* Initialize the variables used during compilation of a C++
   function.  */

void
cxx_push_function_context (struct function * f)
{
  struct language_function *p = GGC_CNEW (struct language_function);
  f->language = p;

  /* Whenever we start a new function, we destroy temporaries in the
     usual way.  */
  current_stmt_tree ()->stmts_are_full_exprs_p = 1;

  if (f->decl)
    {
      tree fn = f->decl;

      if (DECL_SAVED_FUNCTION_DATA (fn))
	{
	  /* If we already parsed this function, and we're just expanding it
	     now, restore saved state.  */
	  *cp_function_chain = *DECL_SAVED_FUNCTION_DATA (fn);

	  /* We don't need the saved data anymore.  Unless this is an inline
	     function; we need the named return value info for
	     declare_return_variable.  */
	  if (! DECL_INLINE (fn))
	    DECL_SAVED_FUNCTION_DATA (fn) = NULL;
	}
    }
}

/* Free the language-specific parts of F, now that we've finished
   compiling the function.  */

void
cxx_pop_function_context (struct function * f)
{
  f->language = 0;
}

/* Return which tree structure is used by T, or TS_CP_GENERIC if T is
   one of the language-independent trees.  */

enum cp_tree_node_structure_enum
cp_tree_node_structure (union lang_tree_node * t)
{
  switch (TREE_CODE (&t->generic))
    {
    case DEFAULT_ARG:		return TS_CP_DEFAULT_ARG;
    case IDENTIFIER_NODE:	return TS_CP_IDENTIFIER;
    case OVERLOAD:		return TS_CP_OVERLOAD;
    case TEMPLATE_PARM_INDEX:	return TS_CP_TPI;
    case TINST_LEVEL:		return TS_CP_TINST_LEVEL;
    case PTRMEM_CST:		return TS_CP_PTRMEM;
    case BASELINK:		return TS_CP_BASELINK;
    default:			return TS_CP_GENERIC;
    }
}

/* Build the void_list_node (void_type_node having been created).  */
tree
build_void_list_node (void)
{
  tree t = build_tree_list (NULL_TREE, void_type_node);
  return t;
}

bool
cp_missing_noreturn_ok_p (tree decl)
{
  /* A missing noreturn is ok for the `main' function.  */
  return DECL_MAIN_P (decl);
}

/* Return the COMDAT group into which DECL should be placed.  */

const char *
cxx_comdat_group (tree decl)
{
  tree name;

  /* Virtual tables, construction virtual tables, and virtual table
     tables all go in a single COMDAT group, named after the primary
     virtual table.  */
  if (TREE_CODE (decl) == VAR_DECL && DECL_VTABLE_OR_VTT_P (decl))
    name = DECL_ASSEMBLER_NAME (CLASSTYPE_VTABLES (DECL_CONTEXT (decl)));
  /* For all other DECLs, the COMDAT group is the mangled name of the
     declaration itself.  */
  else
    {
      while (DECL_THUNK_P (decl))
	{
	  /* If TARGET_USE_LOCAL_THUNK_ALIAS_P, use_thunk puts the thunk
	     into the same section as the target function.  In that case
	     we must return target's name.  */
	  tree target = THUNK_TARGET (decl);
	  if (TARGET_USE_LOCAL_THUNK_ALIAS_P (target)
	      && DECL_SECTION_NAME (target) != NULL
	      && DECL_ONE_ONLY (target))
	    decl = target;
	  else
	    break;
	}
      name = DECL_ASSEMBLER_NAME (decl);
    }

  return IDENTIFIER_POINTER (name);
}

#include "gt-cp-decl.h"
