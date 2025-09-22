/* Definitions for C++ name lookup routines.
   Copyright (C) 2003, 2004, 2005, 2006  Free Software Foundation, Inc.
   Contributed by Gabriel Dos Reis <gdr@integrable-solutions.net>

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
#include "flags.h"
#include "tree.h"
#include "cp-tree.h"
#include "name-lookup.h"
#include "timevar.h"
#include "toplev.h"
#include "diagnostic.h"
#include "debug.h"
#include "c-pragma.h"

/* The bindings for a particular name in a particular scope.  */

struct scope_binding {
  tree value;
  tree type;
};
#define EMPTY_SCOPE_BINDING { NULL_TREE, NULL_TREE }

static cxx_scope *innermost_nonclass_level (void);
static tree select_decl (const struct scope_binding *, int);
static cxx_binding *binding_for_name (cxx_scope *, tree);
static tree lookup_name_innermost_nonclass_level (tree);
static tree push_overloaded_decl (tree, int, bool);
static bool lookup_using_namespace (tree, struct scope_binding *, tree,
				    tree, int);
static bool qualified_lookup_using_namespace (tree, tree,
					      struct scope_binding *, int);
static tree lookup_type_current_level (tree);
static tree push_using_directive (tree);

/* The :: namespace.  */

tree global_namespace;

/* The name of the anonymous namespace, throughout this translation
   unit.  */
static GTY(()) tree anonymous_namespace_name;


/* Compute the chain index of a binding_entry given the HASH value of its
   name and the total COUNT of chains.  COUNT is assumed to be a power
   of 2.  */

#define ENTRY_INDEX(HASH, COUNT) (((HASH) >> 3) & ((COUNT) - 1))

/* A free list of "binding_entry"s awaiting for re-use.  */

static GTY((deletable)) binding_entry free_binding_entry = NULL;

/* Create a binding_entry object for (NAME, TYPE).  */

static inline binding_entry
binding_entry_make (tree name, tree type)
{
  binding_entry entry;

  if (free_binding_entry)
    {
      entry = free_binding_entry;
      free_binding_entry = entry->chain;
    }
  else
    entry = GGC_NEW (struct binding_entry_s);

  entry->name = name;
  entry->type = type;
  entry->chain = NULL;

  return entry;
}

/* Put ENTRY back on the free list.  */
#if 0
static inline void
binding_entry_free (binding_entry entry)
{
  entry->name = NULL;
  entry->type = NULL;
  entry->chain = free_binding_entry;
  free_binding_entry = entry;
}
#endif

/* The datatype used to implement the mapping from names to types at
   a given scope.  */
struct binding_table_s GTY(())
{
  /* Array of chains of "binding_entry"s  */
  binding_entry * GTY((length ("%h.chain_count"))) chain;

  /* The number of chains in this table.  This is the length of the
     the member "chain" considered as an array.  */
  size_t chain_count;

  /* Number of "binding_entry"s in this table.  */
  size_t entry_count;
};

/* Construct TABLE with an initial CHAIN_COUNT.  */

static inline void
binding_table_construct (binding_table table, size_t chain_count)
{
  table->chain_count = chain_count;
  table->entry_count = 0;
  table->chain = GGC_CNEWVEC (binding_entry, table->chain_count);
}

/* Make TABLE's entries ready for reuse.  */
#if 0
static void
binding_table_free (binding_table table)
{
  size_t i;
  size_t count;

  if (table == NULL)
    return;

  for (i = 0, count = table->chain_count; i < count; ++i)
    {
      binding_entry temp = table->chain[i];
      while (temp != NULL)
	{
	  binding_entry entry = temp;
	  temp = entry->chain;
	  binding_entry_free (entry);
	}
      table->chain[i] = NULL;
    }
  table->entry_count = 0;
}
#endif

/* Allocate a table with CHAIN_COUNT, assumed to be a power of two.  */

static inline binding_table
binding_table_new (size_t chain_count)
{
  binding_table table = GGC_NEW (struct binding_table_s);
  table->chain = NULL;
  binding_table_construct (table, chain_count);
  return table;
}

/* Expand TABLE to twice its current chain_count.  */

static void
binding_table_expand (binding_table table)
{
  const size_t old_chain_count = table->chain_count;
  const size_t old_entry_count = table->entry_count;
  const size_t new_chain_count = 2 * old_chain_count;
  binding_entry *old_chains = table->chain;
  size_t i;

  binding_table_construct (table, new_chain_count);
  for (i = 0; i < old_chain_count; ++i)
    {
      binding_entry entry = old_chains[i];
      for (; entry != NULL; entry = old_chains[i])
	{
	  const unsigned int hash = IDENTIFIER_HASH_VALUE (entry->name);
	  const size_t j = ENTRY_INDEX (hash, new_chain_count);

	  old_chains[i] = entry->chain;
	  entry->chain = table->chain[j];
	  table->chain[j] = entry;
	}
    }
  table->entry_count = old_entry_count;
}

/* Insert a binding for NAME to TYPE into TABLE.  */

static void
binding_table_insert (binding_table table, tree name, tree type)
{
  const unsigned int hash = IDENTIFIER_HASH_VALUE (name);
  const size_t i = ENTRY_INDEX (hash, table->chain_count);
  binding_entry entry = binding_entry_make (name, type);

  entry->chain = table->chain[i];
  table->chain[i] = entry;
  ++table->entry_count;

  if (3 * table->chain_count < 5 * table->entry_count)
    binding_table_expand (table);
}

/* Return the binding_entry, if any, that maps NAME.  */

binding_entry
binding_table_find (binding_table table, tree name)
{
  const unsigned int hash = IDENTIFIER_HASH_VALUE (name);
  binding_entry entry = table->chain[ENTRY_INDEX (hash, table->chain_count)];

  while (entry != NULL && entry->name != name)
    entry = entry->chain;

  return entry;
}

/* Apply PROC -- with DATA -- to all entries in TABLE.  */

void
binding_table_foreach (binding_table table, bt_foreach_proc proc, void *data)
{
  const size_t chain_count = table->chain_count;
  size_t i;

  for (i = 0; i < chain_count; ++i)
    {
      binding_entry entry = table->chain[i];
      for (; entry != NULL; entry = entry->chain)
	proc (entry, data);
    }
}

#ifndef ENABLE_SCOPE_CHECKING
#  define ENABLE_SCOPE_CHECKING 0
#else
#  define ENABLE_SCOPE_CHECKING 1
#endif

/* A free list of "cxx_binding"s, connected by their PREVIOUS.  */

static GTY((deletable)) cxx_binding *free_bindings;

/* Initialize VALUE and TYPE field for BINDING, and set the PREVIOUS
   field to NULL.  */

static inline void
cxx_binding_init (cxx_binding *binding, tree value, tree type)
{
  binding->value = value;
  binding->type = type;
  binding->previous = NULL;
}

/* (GC)-allocate a binding object with VALUE and TYPE member initialized.  */

static cxx_binding *
cxx_binding_make (tree value, tree type)
{
  cxx_binding *binding;
  if (free_bindings)
    {
      binding = free_bindings;
      free_bindings = binding->previous;
    }
  else
    binding = GGC_NEW (cxx_binding);

  cxx_binding_init (binding, value, type);

  return binding;
}

/* Put BINDING back on the free list.  */

static inline void
cxx_binding_free (cxx_binding *binding)
{
  binding->scope = NULL;
  binding->previous = free_bindings;
  free_bindings = binding;
}

/* Create a new binding for NAME (with the indicated VALUE and TYPE
   bindings) in the class scope indicated by SCOPE.  */

static cxx_binding *
new_class_binding (tree name, tree value, tree type, cxx_scope *scope)
{
  cp_class_binding *cb;
  cxx_binding *binding;

  if (VEC_length (cp_class_binding, scope->class_shadowed))
    {
      cp_class_binding *old_base;
      old_base = VEC_index (cp_class_binding, scope->class_shadowed, 0);
      if (VEC_reserve (cp_class_binding, gc, scope->class_shadowed, 1))
	{
	  /* Fixup the current bindings, as they might have moved.  */
	  size_t i;

	  for (i = 0;
	       VEC_iterate (cp_class_binding, scope->class_shadowed, i, cb);
	       i++)
	    {
	      cxx_binding **b;
	      b = &IDENTIFIER_BINDING (cb->identifier);
	      while (*b != &old_base[i].base)
		b = &((*b)->previous);
	      *b = &cb->base;
	    }
	}
      cb = VEC_quick_push (cp_class_binding, scope->class_shadowed, NULL);
    }
  else
    cb = VEC_safe_push (cp_class_binding, gc, scope->class_shadowed, NULL);

  cb->identifier = name;
  binding = &cb->base;
  binding->scope = scope;
  cxx_binding_init (binding, value, type);
  return binding;
}

/* Make DECL the innermost binding for ID.  The LEVEL is the binding
   level at which this declaration is being bound.  */

static void
push_binding (tree id, tree decl, cxx_scope* level)
{
  cxx_binding *binding;

  if (level != class_binding_level)
    {
      binding = cxx_binding_make (decl, NULL_TREE);
      binding->scope = level;
    }
  else
    binding = new_class_binding (id, decl, /*type=*/NULL_TREE, level);

  /* Now, fill in the binding information.  */
  binding->previous = IDENTIFIER_BINDING (id);
  INHERITED_VALUE_BINDING_P (binding) = 0;
  LOCAL_BINDING_P (binding) = (level != class_binding_level);

  /* And put it on the front of the list of bindings for ID.  */
  IDENTIFIER_BINDING (id) = binding;
}

/* Remove the binding for DECL which should be the innermost binding
   for ID.  */

void
pop_binding (tree id, tree decl)
{
  cxx_binding *binding;

  if (id == NULL_TREE)
    /* It's easiest to write the loops that call this function without
       checking whether or not the entities involved have names.  We
       get here for such an entity.  */
    return;

  /* Get the innermost binding for ID.  */
  binding = IDENTIFIER_BINDING (id);

  /* The name should be bound.  */
  gcc_assert (binding != NULL);

  /* The DECL will be either the ordinary binding or the type
     binding for this identifier.  Remove that binding.  */
  if (binding->value == decl)
    binding->value = NULL_TREE;
  else
    {
      gcc_assert (binding->type == decl);
      binding->type = NULL_TREE;
    }

  if (!binding->value && !binding->type)
    {
      /* We're completely done with the innermost binding for this
	 identifier.  Unhook it from the list of bindings.  */
      IDENTIFIER_BINDING (id) = binding->previous;

      /* Add it to the free list.  */
      cxx_binding_free (binding);
    }
}

/* BINDING records an existing declaration for a name in the current scope.
   But, DECL is another declaration for that same identifier in the
   same scope.  This is the `struct stat' hack whereby a non-typedef
   class name or enum-name can be bound at the same level as some other
   kind of entity.
   3.3.7/1

     A class name (9.1) or enumeration name (7.2) can be hidden by the
     name of an object, function, or enumerator declared in the same scope.
     If a class or enumeration name and an object, function, or enumerator
     are declared in the same scope (in any order) with the same name, the
     class or enumeration name is hidden wherever the object, function, or
     enumerator name is visible.

   It's the responsibility of the caller to check that
   inserting this name is valid here.  Returns nonzero if the new binding
   was successful.  */

static bool
supplement_binding (cxx_binding *binding, tree decl)
{
  tree bval = binding->value;
  bool ok = true;

  timevar_push (TV_NAME_LOOKUP);
  if (TREE_CODE (decl) == TYPE_DECL && DECL_ARTIFICIAL (decl))
    /* The new name is the type name.  */
    binding->type = decl;
  else if (/* BVAL is null when push_class_level_binding moves an
	      inherited type-binding out of the way to make room for a
	      new value binding.  */
	   !bval
	   /* BVAL is error_mark_node when DECL's name has been used
	      in a non-class scope prior declaration.  In that case,
	      we should have already issued a diagnostic; for graceful
	      error recovery purpose, pretend this was the intended
	      declaration for that name.  */
	   || bval == error_mark_node
	   /* If BVAL is anticipated but has not yet been declared,
	      pretend it is not there at all.  */
	   || (TREE_CODE (bval) == FUNCTION_DECL
	       && DECL_ANTICIPATED (bval)
	       && !DECL_HIDDEN_FRIEND_P (bval)))
    binding->value = decl;
  else if (TREE_CODE (bval) == TYPE_DECL && DECL_ARTIFICIAL (bval))
    {
      /* The old binding was a type name.  It was placed in
	 VALUE field because it was thought, at the point it was
	 declared, to be the only entity with such a name.  Move the
	 type name into the type slot; it is now hidden by the new
	 binding.  */
      binding->type = bval;
      binding->value = decl;
      binding->value_is_inherited = false;
    }
  else if (TREE_CODE (bval) == TYPE_DECL
	   && TREE_CODE (decl) == TYPE_DECL
	   && DECL_NAME (decl) == DECL_NAME (bval)
	   && binding->scope->kind != sk_class
	   && (same_type_p (TREE_TYPE (decl), TREE_TYPE (bval))
	       /* If either type involves template parameters, we must
		  wait until instantiation.  */
	       || uses_template_parms (TREE_TYPE (decl))
	       || uses_template_parms (TREE_TYPE (bval))))
    /* We have two typedef-names, both naming the same type to have
       the same name.  In general, this is OK because of:

	 [dcl.typedef]

	 In a given scope, a typedef specifier can be used to redefine
	 the name of any type declared in that scope to refer to the
	 type to which it already refers.

       However, in class scopes, this rule does not apply due to the
       stricter language in [class.mem] prohibiting redeclarations of
       members.  */
    ok = false;
  /* There can be two block-scope declarations of the same variable,
     so long as they are `extern' declarations.  However, there cannot
     be two declarations of the same static data member:

       [class.mem]

       A member shall not be declared twice in the
       member-specification.  */
  else if (TREE_CODE (decl) == VAR_DECL && TREE_CODE (bval) == VAR_DECL
	   && DECL_EXTERNAL (decl) && DECL_EXTERNAL (bval)
	   && !DECL_CLASS_SCOPE_P (decl))
    {
      duplicate_decls (decl, binding->value, /*newdecl_is_friend=*/false);
      ok = false;
    }
  else if (TREE_CODE (decl) == NAMESPACE_DECL
	   && TREE_CODE (bval) == NAMESPACE_DECL
	   && DECL_NAMESPACE_ALIAS (decl)
	   && DECL_NAMESPACE_ALIAS (bval)
	   && ORIGINAL_NAMESPACE (bval) == ORIGINAL_NAMESPACE (decl))
    /* [namespace.alias]

      In a declarative region, a namespace-alias-definition can be
      used to redefine a namespace-alias declared in that declarative
      region to refer only to the namespace to which it already
      refers.  */
    ok = false;
  else
    {
      error ("declaration of %q#D", decl);
      error ("conflicts with previous declaration %q+#D", bval);
      ok = false;
    }

  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, ok);
}

/* Add DECL to the list of things declared in B.  */

static void
add_decl_to_level (tree decl, cxx_scope *b)
{
  if (TREE_CODE (decl) == NAMESPACE_DECL
      && !DECL_NAMESPACE_ALIAS (decl))
    {
      TREE_CHAIN (decl) = b->namespaces;
      b->namespaces = decl;
    }
  else if (TREE_CODE (decl) == VAR_DECL && DECL_VIRTUAL_P (decl))
    {
      TREE_CHAIN (decl) = b->vtables;
      b->vtables = decl;
    }
  else
    {
      /* We build up the list in reverse order, and reverse it later if
	 necessary.  */
      TREE_CHAIN (decl) = b->names;
      b->names = decl;
      b->names_size++;

      /* If appropriate, add decl to separate list of statics.  We
	 include extern variables because they might turn out to be
	 static later.  It's OK for this list to contain a few false
	 positives.  */
      if (b->kind == sk_namespace)
	if ((TREE_CODE (decl) == VAR_DECL
	     && (TREE_STATIC (decl) || DECL_EXTERNAL (decl)))
	    || (TREE_CODE (decl) == FUNCTION_DECL
		&& (!TREE_PUBLIC (decl) || DECL_DECLARED_INLINE_P (decl))))
	  VEC_safe_push (tree, gc, b->static_decls, decl);
    }
}

/* Record a decl-node X as belonging to the current lexical scope.
   Check for errors (such as an incompatible declaration for the same
   name already seen in the same scope).  IS_FRIEND is true if X is
   declared as a friend.

   Returns either X or an old decl for the same name.
   If an old decl is returned, it may have been smashed
   to agree with what X says.  */

tree
pushdecl_maybe_friend (tree x, bool is_friend)
{
  tree t;
  tree name;
  int need_new_binding;

  timevar_push (TV_NAME_LOOKUP);

  if (x == error_mark_node)
    POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, error_mark_node);

  need_new_binding = 1;

  if (DECL_TEMPLATE_PARM_P (x))
    /* Template parameters have no context; they are not X::T even
       when declared within a class or namespace.  */
    ;
  else
    {
      if (current_function_decl && x != current_function_decl
	  /* A local declaration for a function doesn't constitute
	     nesting.  */
	  && TREE_CODE (x) != FUNCTION_DECL
	  /* A local declaration for an `extern' variable is in the
	     scope of the current namespace, not the current
	     function.  */
	  && !(TREE_CODE (x) == VAR_DECL && DECL_EXTERNAL (x))
	  && !DECL_CONTEXT (x))
	DECL_CONTEXT (x) = current_function_decl;

      /* If this is the declaration for a namespace-scope function,
	 but the declaration itself is in a local scope, mark the
	 declaration.  */
      if (TREE_CODE (x) == FUNCTION_DECL
	  && DECL_NAMESPACE_SCOPE_P (x)
	  && current_function_decl
	  && x != current_function_decl)
	DECL_LOCAL_FUNCTION_P (x) = 1;
    }

  name = DECL_NAME (x);
  if (name)
    {
      int different_binding_level = 0;

      if (TREE_CODE (name) == TEMPLATE_ID_EXPR)
	name = TREE_OPERAND (name, 0);

      /* In case this decl was explicitly namespace-qualified, look it
	 up in its namespace context.  */
      if (DECL_NAMESPACE_SCOPE_P (x) && namespace_bindings_p ())
	t = namespace_binding (name, DECL_CONTEXT (x));
      else
	t = lookup_name_innermost_nonclass_level (name);

      /* [basic.link] If there is a visible declaration of an entity
	 with linkage having the same name and type, ignoring entities
	 declared outside the innermost enclosing namespace scope, the
	 block scope declaration declares that same entity and
	 receives the linkage of the previous declaration.  */
      if (! t && current_function_decl && x != current_function_decl
	  && (TREE_CODE (x) == FUNCTION_DECL || TREE_CODE (x) == VAR_DECL)
	  && DECL_EXTERNAL (x))
	{
	  /* Look in block scope.  */
	  t = innermost_non_namespace_value (name);
	  /* Or in the innermost namespace.  */
	  if (! t)
	    t = namespace_binding (name, DECL_CONTEXT (x));
	  /* Does it have linkage?  Note that if this isn't a DECL, it's an
	     OVERLOAD, which is OK.  */
	  if (t && DECL_P (t) && ! (TREE_STATIC (t) || DECL_EXTERNAL (t)))
	    t = NULL_TREE;
	  if (t)
	    different_binding_level = 1;
	}

      /* If we are declaring a function, and the result of name-lookup
	 was an OVERLOAD, look for an overloaded instance that is
	 actually the same as the function we are declaring.  (If
	 there is one, we have to merge our declaration with the
	 previous declaration.)  */
      if (t && TREE_CODE (t) == OVERLOAD)
	{
	  tree match;

	  if (TREE_CODE (x) == FUNCTION_DECL)
	    for (match = t; match; match = OVL_NEXT (match))
	      {
		if (decls_match (OVL_CURRENT (match), x))
		  break;
	      }
	  else
	    /* Just choose one.  */
	    match = t;

	  if (match)
	    t = OVL_CURRENT (match);
	  else
	    t = NULL_TREE;
	}

      if (t && t != error_mark_node)
	{
	  if (different_binding_level)
	    {
	      if (decls_match (x, t))
		/* The standard only says that the local extern
		   inherits linkage from the previous decl; in
		   particular, default args are not shared.  Add
		   the decl into a hash table to make sure only
		   the previous decl in this case is seen by the
		   middle end.  */
		{
		  struct cxx_int_tree_map *h;
		  void **loc;

		  TREE_PUBLIC (x) = TREE_PUBLIC (t);

		  if (cp_function_chain->extern_decl_map == NULL)
		    cp_function_chain->extern_decl_map
		      = htab_create_ggc (20, cxx_int_tree_map_hash,
					 cxx_int_tree_map_eq, NULL);

		  h = GGC_NEW (struct cxx_int_tree_map);
		  h->uid = DECL_UID (x);
		  h->to = t;
		  loc = htab_find_slot_with_hash
			  (cp_function_chain->extern_decl_map, h,
			   h->uid, INSERT);
		  *(struct cxx_int_tree_map **) loc = h;
		}
	    }
	  else if (TREE_CODE (t) == PARM_DECL)
	    {
	      gcc_assert (DECL_CONTEXT (t));

	      /* Check for duplicate params.  */
	      if (duplicate_decls (x, t, is_friend))
		POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, t);
	    }
	  else if ((DECL_EXTERN_C_FUNCTION_P (x)
		    || DECL_FUNCTION_TEMPLATE_P (x))
		   && is_overloaded_fn (t))
	    /* Don't do anything just yet.  */;
	  else if (t == wchar_decl_node)
	    {
	      if (pedantic && ! DECL_IN_SYSTEM_HEADER (x))
		pedwarn ("redeclaration of %<wchar_t%> as %qT",
			 TREE_TYPE (x));

	      /* Throw away the redeclaration.  */
	      POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, t);
	    }
	  else
	    {
	      tree olddecl = duplicate_decls (x, t, is_friend);

	      /* If the redeclaration failed, we can stop at this
		 point.  */
	      if (olddecl == error_mark_node)
		POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, error_mark_node);

	      if (olddecl)
		{
		  if (TREE_CODE (t) == TYPE_DECL)
		    SET_IDENTIFIER_TYPE_VALUE (name, TREE_TYPE (t));

		  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, t);
		}
	      else if (DECL_MAIN_P (x) && TREE_CODE (t) == FUNCTION_DECL)
		{
		  /* A redeclaration of main, but not a duplicate of the
		     previous one.

		     [basic.start.main]

		     This function shall not be overloaded.  */
		  error ("invalid redeclaration of %q+D", t);
		  error ("as %qD", x);
		  /* We don't try to push this declaration since that
		     causes a crash.  */
		  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, x);
		}
	    }
	}

      if (TREE_CODE (x) == FUNCTION_DECL || DECL_FUNCTION_TEMPLATE_P (x))
	check_default_args (x);

      check_template_shadow (x);

      /* If this is a function conjured up by the backend, massage it
	 so it looks friendly.  */
      if (DECL_NON_THUNK_FUNCTION_P (x) && ! DECL_LANG_SPECIFIC (x))
	{
	  retrofit_lang_decl (x);
	  SET_DECL_LANGUAGE (x, lang_c);
	}

      if (DECL_NON_THUNK_FUNCTION_P (x) && ! DECL_FUNCTION_MEMBER_P (x))
	{
	  t = push_overloaded_decl (x, PUSH_LOCAL, is_friend);
	  if (t != x)
	    POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, t);
	  if (!namespace_bindings_p ())
	    /* We do not need to create a binding for this name;
	       push_overloaded_decl will have already done so if
	       necessary.  */
	    need_new_binding = 0;
	}
      else if (DECL_FUNCTION_TEMPLATE_P (x) && DECL_NAMESPACE_SCOPE_P (x))
	{
	  t = push_overloaded_decl (x, PUSH_GLOBAL, is_friend);
	  if (t == x)
	    add_decl_to_level (x, NAMESPACE_LEVEL (CP_DECL_CONTEXT (t)));
	  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, t);
	}

      /* If declaring a type as a typedef, copy the type (unless we're
	 at line 0), and install this TYPE_DECL as the new type's typedef
	 name.  See the extensive comment in ../c-decl.c (pushdecl).  */
      if (TREE_CODE (x) == TYPE_DECL)
	{
	  tree type = TREE_TYPE (x);
	  if (DECL_IS_BUILTIN (x))
	    {
	      if (TYPE_NAME (type) == 0)
		TYPE_NAME (type) = x;
	    }
	  else if (type != error_mark_node && TYPE_NAME (type) != x
		   /* We don't want to copy the type when all we're
		      doing is making a TYPE_DECL for the purposes of
		      inlining.  */
		   && (!TYPE_NAME (type)
		       || TYPE_NAME (type) != DECL_ABSTRACT_ORIGIN (x)))
	    {
	      DECL_ORIGINAL_TYPE (x) = type;
	      type = build_variant_type_copy (type);
	      TYPE_STUB_DECL (type) = TYPE_STUB_DECL (DECL_ORIGINAL_TYPE (x));
	      TYPE_NAME (type) = x;
	      TREE_TYPE (x) = type;
	    }

	  if (type != error_mark_node
	      && TYPE_NAME (type)
	      && TYPE_IDENTIFIER (type))
	    set_identifier_type_value (DECL_NAME (x), x);
	}

      /* Multiple external decls of the same identifier ought to match.

	 We get warnings about inline functions where they are defined.
	 We get warnings about other functions from push_overloaded_decl.

	 Avoid duplicate warnings where they are used.  */
      if (TREE_PUBLIC (x) && TREE_CODE (x) != FUNCTION_DECL)
	{
	  tree decl;

	  decl = IDENTIFIER_NAMESPACE_VALUE (name);
	  if (decl && TREE_CODE (decl) == OVERLOAD)
	    decl = OVL_FUNCTION (decl);

	  if (decl && decl != error_mark_node
	      && (DECL_EXTERNAL (decl) || TREE_PUBLIC (decl))
	      /* If different sort of thing, we already gave an error.  */
	      && TREE_CODE (decl) == TREE_CODE (x)
	      && !same_type_p (TREE_TYPE (x), TREE_TYPE (decl)))
	    {
	      pedwarn ("type mismatch with previous external decl of %q#D", x);
	      pedwarn ("previous external decl of %q+#D", decl);
	    }
	}

      if (TREE_CODE (x) == FUNCTION_DECL
	  && is_friend
	  && !flag_friend_injection)
	{
	  /* This is a new declaration of a friend function, so hide
	     it from ordinary function lookup.  */
	  DECL_ANTICIPATED (x) = 1;
	  DECL_HIDDEN_FRIEND_P (x) = 1;
	}

      /* This name is new in its binding level.
	 Install the new declaration and return it.  */
      if (namespace_bindings_p ())
	{
	  /* Install a global value.  */

	  /* If the first global decl has external linkage,
	     warn if we later see static one.  */
	  if (IDENTIFIER_GLOBAL_VALUE (name) == NULL_TREE && TREE_PUBLIC (x))
	    TREE_PUBLIC (name) = 1;

	  /* Bind the name for the entity.  */
	  if (!(TREE_CODE (x) == TYPE_DECL && DECL_ARTIFICIAL (x)
		&& t != NULL_TREE)
	      && (TREE_CODE (x) == TYPE_DECL
		  || TREE_CODE (x) == VAR_DECL
		  || TREE_CODE (x) == NAMESPACE_DECL
		  || TREE_CODE (x) == CONST_DECL
		  || TREE_CODE (x) == TEMPLATE_DECL))
	    SET_IDENTIFIER_NAMESPACE_VALUE (name, x);

	  /* If new decl is `static' and an `extern' was seen previously,
	     warn about it.  */
	  if (x != NULL_TREE && t != NULL_TREE && decls_match (x, t))
	    warn_extern_redeclared_static (x, t);
	}
      else
	{
	  /* Here to install a non-global value.  */
	  tree oldlocal = innermost_non_namespace_value (name);
	  tree oldglobal = IDENTIFIER_NAMESPACE_VALUE (name);

	  if (need_new_binding)
	    {
	      push_local_binding (name, x, 0);
	      /* Because push_local_binding will hook X on to the
		 current_binding_level's name list, we don't want to
		 do that again below.  */
	      need_new_binding = 0;
	    }

	  /* If this is a TYPE_DECL, push it into the type value slot.  */
	  if (TREE_CODE (x) == TYPE_DECL)
	    set_identifier_type_value (name, x);

	  /* Clear out any TYPE_DECL shadowed by a namespace so that
	     we won't think this is a type.  The C struct hack doesn't
	     go through namespaces.  */
	  if (TREE_CODE (x) == NAMESPACE_DECL)
	    set_identifier_type_value (name, NULL_TREE);

	  if (oldlocal)
	    {
	      tree d = oldlocal;

	      while (oldlocal
		     && TREE_CODE (oldlocal) == VAR_DECL
		     && DECL_DEAD_FOR_LOCAL (oldlocal))
		oldlocal = DECL_SHADOWED_FOR_VAR (oldlocal);

	      if (oldlocal == NULL_TREE)
		oldlocal = IDENTIFIER_NAMESPACE_VALUE (DECL_NAME (d));
	    }

	  /* If this is an extern function declaration, see if we
	     have a global definition or declaration for the function.  */
	  if (oldlocal == NULL_TREE
	      && DECL_EXTERNAL (x)
	      && oldglobal != NULL_TREE
	      && TREE_CODE (x) == FUNCTION_DECL
	      && TREE_CODE (oldglobal) == FUNCTION_DECL)
	    {
	      /* We have one.  Their types must agree.  */
	      if (decls_match (x, oldglobal))
		/* OK */;
	      else
		{
		  warning (0, "extern declaration of %q#D doesn't match", x);
		  warning (0, "global declaration %q+#D", oldglobal);
		}
	    }
	  /* If we have a local external declaration,
	     and no file-scope declaration has yet been seen,
	     then if we later have a file-scope decl it must not be static.  */
	  if (oldlocal == NULL_TREE
	      && oldglobal == NULL_TREE
	      && DECL_EXTERNAL (x)
	      && TREE_PUBLIC (x))
	    TREE_PUBLIC (name) = 1;

	  /* Warn if shadowing an argument at the top level of the body.  */
	  if (oldlocal != NULL_TREE && !DECL_EXTERNAL (x)
	      /* Inline decls shadow nothing.  */
	      && !DECL_FROM_INLINE (x)
	      && TREE_CODE (oldlocal) == PARM_DECL
	      /* Don't check the `this' parameter.  */
	      && !DECL_ARTIFICIAL (oldlocal))
	    {
	      bool err = false;

	      /* Don't complain if it's from an enclosing function.  */
	      if (DECL_CONTEXT (oldlocal) == current_function_decl
		  && TREE_CODE (x) != PARM_DECL)
		{
		  /* Go to where the parms should be and see if we find
		     them there.  */
		  struct cp_binding_level *b = current_binding_level->level_chain;

		  if (FUNCTION_NEEDS_BODY_BLOCK (current_function_decl))
		    /* Skip the ctor/dtor cleanup level.  */
		    b = b->level_chain;

		  /* ARM $8.3 */
		  if (b->kind == sk_function_parms)
		    {
		      error ("declaration of %q#D shadows a parameter", x);
		      err = true;
		    }
		}

	      if (warn_shadow && !err)
		{
		  warning (OPT_Wshadow, "declaration of %q#D shadows a parameter", x);
		  warning (OPT_Wshadow, "%Jshadowed declaration is here", oldlocal);
		}
	    }

	  /* Maybe warn if shadowing something else.  */
	  else if (warn_shadow && !DECL_EXTERNAL (x)
	      /* No shadow warnings for internally generated vars.  */
	      && ! DECL_ARTIFICIAL (x)
	      /* No shadow warnings for vars made for inlining.  */
	      && ! DECL_FROM_INLINE (x))
	    {
	      tree member;

	      if (current_class_ptr)
		member = lookup_member (current_class_type,
					name,
					/*protect=*/0,
					/*want_type=*/false);
	      else
		member = NULL_TREE;

	      if (member && !TREE_STATIC (member))
		{
		  /* Location of previous decl is not useful in this case.  */
		  warning (OPT_Wshadow, "declaration of %qD shadows a member of 'this'",
			   x);
		}
	      else if (oldlocal != NULL_TREE
		       && TREE_CODE (oldlocal) == VAR_DECL)
		{
		  warning (OPT_Wshadow, "declaration of %qD shadows a previous local", x);
		  warning (OPT_Wshadow, "%Jshadowed declaration is here", oldlocal);
		}
	      else if (oldglobal != NULL_TREE
		       && TREE_CODE (oldglobal) == VAR_DECL)
		/* XXX shadow warnings in outer-more namespaces */
		{
		  warning (OPT_Wshadow, "declaration of %qD shadows a global declaration",
			   x);
		  warning (OPT_Wshadow, "%Jshadowed declaration is here", oldglobal);
		}
	    }
	}

      if (TREE_CODE (x) == VAR_DECL)
	maybe_register_incomplete_var (x);
    }

  if (need_new_binding)
    add_decl_to_level (x,
		       DECL_NAMESPACE_SCOPE_P (x)
		       ? NAMESPACE_LEVEL (CP_DECL_CONTEXT (x))
		       : current_binding_level);

  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, x);
}

/* Record a decl-node X as belonging to the current lexical scope.  */

tree
pushdecl (tree x)
{
  return pushdecl_maybe_friend (x, false);
}

/* Enter DECL into the symbol table, if that's appropriate.  Returns
   DECL, or a modified version thereof.  */

tree
maybe_push_decl (tree decl)
{
  tree type = TREE_TYPE (decl);

  /* Add this decl to the current binding level, but not if it comes
     from another scope, e.g. a static member variable.  TEM may equal
     DECL or it may be a previous decl of the same name.  */
  if (decl == error_mark_node
      || (TREE_CODE (decl) != PARM_DECL
	  && DECL_CONTEXT (decl) != NULL_TREE
	  /* Definitions of namespace members outside their namespace are
	     possible.  */
	  && TREE_CODE (DECL_CONTEXT (decl)) != NAMESPACE_DECL)
      || (TREE_CODE (decl) == TEMPLATE_DECL && !namespace_bindings_p ())
      || TREE_CODE (type) == UNKNOWN_TYPE
      /* The declaration of a template specialization does not affect
	 the functions available for overload resolution, so we do not
	 call pushdecl.  */
      || (TREE_CODE (decl) == FUNCTION_DECL
	  && DECL_TEMPLATE_SPECIALIZATION (decl)))
    return decl;
  else
    return pushdecl (decl);
}

/* Bind DECL to ID in the current_binding_level, assumed to be a local
   binding level.  If PUSH_USING is set in FLAGS, we know that DECL
   doesn't really belong to this binding level, that it got here
   through a using-declaration.  */

void
push_local_binding (tree id, tree decl, int flags)
{
  struct cp_binding_level *b;

  /* Skip over any local classes.  This makes sense if we call
     push_local_binding with a friend decl of a local class.  */
  b = innermost_nonclass_level ();

  if (lookup_name_innermost_nonclass_level (id))
    {
      /* Supplement the existing binding.  */
      if (!supplement_binding (IDENTIFIER_BINDING (id), decl))
	/* It didn't work.  Something else must be bound at this
	   level.  Do not add DECL to the list of things to pop
	   later.  */
	return;
    }
  else
    /* Create a new binding.  */
    push_binding (id, decl, b);

  if (TREE_CODE (decl) == OVERLOAD || (flags & PUSH_USING))
    /* We must put the OVERLOAD into a TREE_LIST since the
       TREE_CHAIN of an OVERLOAD is already used.  Similarly for
       decls that got here through a using-declaration.  */
    decl = build_tree_list (NULL_TREE, decl);

  /* And put DECL on the list of things declared by the current
     binding level.  */
  add_decl_to_level (decl, b);
}

/* Check to see whether or not DECL is a variable that would have been
   in scope under the ARM, but is not in scope under the ANSI/ISO
   standard.  If so, issue an error message.  If name lookup would
   work in both cases, but return a different result, this function
   returns the result of ANSI/ISO lookup.  Otherwise, it returns
   DECL.  */

tree
check_for_out_of_scope_variable (tree decl)
{
  tree shadowed;

  /* We only care about out of scope variables.  */
  if (!(TREE_CODE (decl) == VAR_DECL && DECL_DEAD_FOR_LOCAL (decl)))
    return decl;

  shadowed = DECL_HAS_SHADOWED_FOR_VAR_P (decl)
    ? DECL_SHADOWED_FOR_VAR (decl) : NULL_TREE ;
  while (shadowed != NULL_TREE && TREE_CODE (shadowed) == VAR_DECL
	 && DECL_DEAD_FOR_LOCAL (shadowed))
    shadowed = DECL_HAS_SHADOWED_FOR_VAR_P (shadowed)
      ? DECL_SHADOWED_FOR_VAR (shadowed) : NULL_TREE;
  if (!shadowed)
    shadowed = IDENTIFIER_NAMESPACE_VALUE (DECL_NAME (decl));
  if (shadowed)
    {
      if (!DECL_ERROR_REPORTED (decl))
	{
	  warning (0, "name lookup of %qD changed", DECL_NAME (decl));
	  warning (0, "  matches this %q+D under ISO standard rules",
		   shadowed);
	  warning (0, "  matches this %q+D under old rules", decl);
	  DECL_ERROR_REPORTED (decl) = 1;
	}
      return shadowed;
    }

  /* If we have already complained about this declaration, there's no
     need to do it again.  */
  if (DECL_ERROR_REPORTED (decl))
    return decl;

  DECL_ERROR_REPORTED (decl) = 1;

  if (TREE_TYPE (decl) == error_mark_node)
    return decl;

  if (TYPE_HAS_NONTRIVIAL_DESTRUCTOR (TREE_TYPE (decl)))
    {
      error ("name lookup of %qD changed for new ISO %<for%> scoping",
	     DECL_NAME (decl));
      error ("  cannot use obsolete binding at %q+D because "
	     "it has a destructor", decl);
      return error_mark_node;
    }
  else
    {
      pedwarn ("name lookup of %qD changed for new ISO %<for%> scoping",
	       DECL_NAME (decl));
      pedwarn ("  using obsolete binding at %q+D", decl);
    }

  return decl;
}

/* true means unconditionally make a BLOCK for the next level pushed.  */

static bool keep_next_level_flag;

static int binding_depth = 0;
static int is_class_level = 0;

static void
indent (int depth)
{
  int i;

  for (i = 0; i < depth * 2; i++)
    putc (' ', stderr);
}

/* Return a string describing the kind of SCOPE we have.  */
static const char *
cxx_scope_descriptor (cxx_scope *scope)
{
  /* The order of this table must match the "scope_kind"
     enumerators.  */
  static const char* scope_kind_names[] = {
    "block-scope",
    "cleanup-scope",
    "try-scope",
    "catch-scope",
    "for-scope",
    "function-parameter-scope",
    "class-scope",
    "namespace-scope",
    "template-parameter-scope",
    "template-explicit-spec-scope"
  };
  const scope_kind kind = scope->explicit_spec_p
    ? sk_template_spec : scope->kind;

  return scope_kind_names[kind];
}

/* Output a debugging information about SCOPE when performing
   ACTION at LINE.  */
static void
cxx_scope_debug (cxx_scope *scope, int line, const char *action)
{
  const char *desc = cxx_scope_descriptor (scope);
  if (scope->this_entity)
    verbatim ("%s %s(%E) %p %d\n", action, desc,
	      scope->this_entity, (void *) scope, line);
  else
    verbatim ("%s %s %p %d\n", action, desc, (void *) scope, line);
}

/* Return the estimated initial size of the hashtable of a NAMESPACE
   scope.  */

static inline size_t
namespace_scope_ht_size (tree ns)
{
  tree name = DECL_NAME (ns);

  return name == std_identifier
    ? NAMESPACE_STD_HT_SIZE
    : (name == global_scope_name
       ? GLOBAL_SCOPE_HT_SIZE
       : NAMESPACE_ORDINARY_HT_SIZE);
}

/* A chain of binding_level structures awaiting reuse.  */

static GTY((deletable)) struct cp_binding_level *free_binding_level;

/* Insert SCOPE as the innermost binding level.  */

void
push_binding_level (struct cp_binding_level *scope)
{
  /* Add it to the front of currently active scopes stack.  */
  scope->level_chain = current_binding_level;
  current_binding_level = scope;
  keep_next_level_flag = false;

  if (ENABLE_SCOPE_CHECKING)
    {
      scope->binding_depth = binding_depth;
      indent (binding_depth);
      cxx_scope_debug (scope, input_line, "push");
      is_class_level = 0;
      binding_depth++;
    }
}

/* Create a new KIND scope and make it the top of the active scopes stack.
   ENTITY is the scope of the associated C++ entity (namespace, class,
   function); it is NULL otherwise.  */

cxx_scope *
begin_scope (scope_kind kind, tree entity)
{
  cxx_scope *scope;

  /* Reuse or create a struct for this binding level.  */
  if (!ENABLE_SCOPE_CHECKING && free_binding_level)
    {
      scope = free_binding_level;
      free_binding_level = scope->level_chain;
    }
  else
    scope = GGC_NEW (cxx_scope);
  memset (scope, 0, sizeof (cxx_scope));

  scope->this_entity = entity;
  scope->more_cleanups_ok = true;
  switch (kind)
    {
    case sk_cleanup:
      scope->keep = true;
      break;

    case sk_template_spec:
      scope->explicit_spec_p = true;
      kind = sk_template_parms;
      /* Fall through.  */
    case sk_template_parms:
    case sk_block:
    case sk_try:
    case sk_catch:
    case sk_for:
    case sk_class:
    case sk_function_parms:
    case sk_omp:
      scope->keep = keep_next_level_flag;
      break;

    case sk_namespace:
      NAMESPACE_LEVEL (entity) = scope;
      scope->static_decls =
	VEC_alloc (tree, gc,
		   DECL_NAME (entity) == std_identifier
		   || DECL_NAME (entity) == global_scope_name
		   ? 200 : 10);
      break;

    default:
      /* Should not happen.  */
      gcc_unreachable ();
      break;
    }
  scope->kind = kind;

  push_binding_level (scope);

  return scope;
}

/* We're about to leave current scope.  Pop the top of the stack of
   currently active scopes.  Return the enclosing scope, now active.  */

cxx_scope *
leave_scope (void)
{
  cxx_scope *scope = current_binding_level;

  if (scope->kind == sk_namespace && class_binding_level)
    current_binding_level = class_binding_level;

  /* We cannot leave a scope, if there are none left.  */
  if (NAMESPACE_LEVEL (global_namespace))
    gcc_assert (!global_scope_p (scope));

  if (ENABLE_SCOPE_CHECKING)
    {
      indent (--binding_depth);
      cxx_scope_debug (scope, input_line, "leave");
      if (is_class_level != (scope == class_binding_level))
	{
	  indent (binding_depth);
	  verbatim ("XXX is_class_level != (current_scope == class_scope)\n");
	}
      is_class_level = 0;
    }

#ifdef HANDLE_PRAGMA_VISIBILITY
  if (scope->has_visibility)
    pop_visibility ();
#endif

  /* Move one nesting level up.  */
  current_binding_level = scope->level_chain;

  /* Namespace-scopes are left most probably temporarily, not
     completely; they can be reopened later, e.g. in namespace-extension
     or any name binding activity that requires us to resume a
     namespace.  For classes, we cache some binding levels.  For other
     scopes, we just make the structure available for reuse.  */
  if (scope->kind != sk_namespace
      && scope->kind != sk_class)
    {
      scope->level_chain = free_binding_level;
      gcc_assert (!ENABLE_SCOPE_CHECKING
		  || scope->binding_depth == binding_depth);
      free_binding_level = scope;
    }

  /* Find the innermost enclosing class scope, and reset
     CLASS_BINDING_LEVEL appropriately.  */
  if (scope->kind == sk_class)
    {
      class_binding_level = NULL;
      for (scope = current_binding_level; scope; scope = scope->level_chain)
	if (scope->kind == sk_class)
	  {
	    class_binding_level = scope;
	    break;
	  }
    }

  return current_binding_level;
}

static void
resume_scope (struct cp_binding_level* b)
{
  /* Resuming binding levels is meant only for namespaces,
     and those cannot nest into classes.  */
  gcc_assert (!class_binding_level);
  /* Also, resuming a non-directly nested namespace is a no-no.  */
  gcc_assert (b->level_chain == current_binding_level);
  current_binding_level = b;
  if (ENABLE_SCOPE_CHECKING)
    {
      b->binding_depth = binding_depth;
      indent (binding_depth);
      cxx_scope_debug (b, input_line, "resume");
      is_class_level = 0;
      binding_depth++;
    }
}

/* Return the innermost binding level that is not for a class scope.  */

static cxx_scope *
innermost_nonclass_level (void)
{
  cxx_scope *b;

  b = current_binding_level;
  while (b->kind == sk_class)
    b = b->level_chain;

  return b;
}

/* We're defining an object of type TYPE.  If it needs a cleanup, but
   we're not allowed to add any more objects with cleanups to the current
   scope, create a new binding level.  */

void
maybe_push_cleanup_level (tree type)
{
  if (type != error_mark_node
      && TYPE_HAS_NONTRIVIAL_DESTRUCTOR (type)
      && current_binding_level->more_cleanups_ok == 0)
    {
      begin_scope (sk_cleanup, NULL);
      current_binding_level->statement_list = push_stmt_list ();
    }
}

/* Nonzero if we are currently in the global binding level.  */

int
global_bindings_p (void)
{
  return global_scope_p (current_binding_level);
}

/* True if we are currently in a toplevel binding level.  This
   means either the global binding level or a namespace in a toplevel
   binding level.  Since there are no non-toplevel namespace levels,
   this really means any namespace or template parameter level.  We
   also include a class whose context is toplevel.  */

bool
toplevel_bindings_p (void)
{
  struct cp_binding_level *b = innermost_nonclass_level ();

  return b->kind == sk_namespace || b->kind == sk_template_parms;
}

/* True if this is a namespace scope, or if we are defining a class
   which is itself at namespace scope, or whose enclosing class is
   such a class, etc.  */

bool
namespace_bindings_p (void)
{
  struct cp_binding_level *b = innermost_nonclass_level ();

  return b->kind == sk_namespace;
}

/* True if the current level needs to have a BLOCK made.  */

bool
kept_level_p (void)
{
  return (current_binding_level->blocks != NULL_TREE
	  || current_binding_level->keep
	  || current_binding_level->kind == sk_cleanup
	  || current_binding_level->names != NULL_TREE);
}

/* Returns the kind of the innermost scope.  */

scope_kind
innermost_scope_kind (void)
{
  return current_binding_level->kind;
}

/* Returns true if this scope was created to store template parameters.  */

bool
template_parm_scope_p (void)
{
  return innermost_scope_kind () == sk_template_parms;
}

/* If KEEP is true, make a BLOCK node for the next binding level,
   unconditionally.  Otherwise, use the normal logic to decide whether
   or not to create a BLOCK.  */

void
keep_next_level (bool keep)
{
  keep_next_level_flag = keep;
}

/* Return the list of declarations of the current level.
   Note that this list is in reverse order unless/until
   you nreverse it; and when you do nreverse it, you must
   store the result back using `storedecls' or you will lose.  */

tree
getdecls (void)
{
  return current_binding_level->names;
}

/* For debugging.  */
static int no_print_functions = 0;
static int no_print_builtins = 0;

static void
print_binding_level (struct cp_binding_level* lvl)
{
  tree t;
  int i = 0, len;
  fprintf (stderr, " blocks=%p", (void *) lvl->blocks);
  if (lvl->more_cleanups_ok)
    fprintf (stderr, " more-cleanups-ok");
  if (lvl->have_cleanups)
    fprintf (stderr, " have-cleanups");
  fprintf (stderr, "\n");
  if (lvl->names)
    {
      fprintf (stderr, " names:\t");
      /* We can probably fit 3 names to a line?  */
      for (t = lvl->names; t; t = TREE_CHAIN (t))
	{
	  if (no_print_functions && (TREE_CODE (t) == FUNCTION_DECL))
	    continue;
	  if (no_print_builtins
	      && (TREE_CODE (t) == TYPE_DECL)
	      && DECL_IS_BUILTIN (t))
	    continue;

	  /* Function decls tend to have longer names.  */
	  if (TREE_CODE (t) == FUNCTION_DECL)
	    len = 3;
	  else
	    len = 2;
	  i += len;
	  if (i > 6)
	    {
	      fprintf (stderr, "\n\t");
	      i = len;
	    }
	  print_node_brief (stderr, "", t, 0);
	  if (t == error_mark_node)
	    break;
	}
      if (i)
	fprintf (stderr, "\n");
    }
  if (VEC_length (cp_class_binding, lvl->class_shadowed))
    {
      size_t i;
      cp_class_binding *b;
      fprintf (stderr, " class-shadowed:");
      for (i = 0;
	   VEC_iterate(cp_class_binding, lvl->class_shadowed, i, b);
	   ++i)
	fprintf (stderr, " %s ", IDENTIFIER_POINTER (b->identifier));
      fprintf (stderr, "\n");
    }
  if (lvl->type_shadowed)
    {
      fprintf (stderr, " type-shadowed:");
      for (t = lvl->type_shadowed; t; t = TREE_CHAIN (t))
	{
	  fprintf (stderr, " %s ", IDENTIFIER_POINTER (TREE_PURPOSE (t)));
	}
      fprintf (stderr, "\n");
    }
}

void
print_other_binding_stack (struct cp_binding_level *stack)
{
  struct cp_binding_level *level;
  for (level = stack; !global_scope_p (level); level = level->level_chain)
    {
      fprintf (stderr, "binding level %p\n", (void *) level);
      print_binding_level (level);
    }
}

void
print_binding_stack (void)
{
  struct cp_binding_level *b;
  fprintf (stderr, "current_binding_level=%p\n"
	   "class_binding_level=%p\n"
	   "NAMESPACE_LEVEL (global_namespace)=%p\n",
	   (void *) current_binding_level, (void *) class_binding_level,
	   (void *) NAMESPACE_LEVEL (global_namespace));
  if (class_binding_level)
    {
      for (b = class_binding_level; b; b = b->level_chain)
	if (b == current_binding_level)
	  break;
      if (b)
	b = class_binding_level;
      else
	b = current_binding_level;
    }
  else
    b = current_binding_level;
  print_other_binding_stack (b);
  fprintf (stderr, "global:\n");
  print_binding_level (NAMESPACE_LEVEL (global_namespace));
}

/* Return the type associated with id.  */

tree
identifier_type_value (tree id)
{
  timevar_push (TV_NAME_LOOKUP);
  /* There is no type with that name, anywhere.  */
  if (REAL_IDENTIFIER_TYPE_VALUE (id) == NULL_TREE)
    POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, NULL_TREE);
  /* This is not the type marker, but the real thing.  */
  if (REAL_IDENTIFIER_TYPE_VALUE (id) != global_type_node)
    POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, REAL_IDENTIFIER_TYPE_VALUE (id));
  /* Have to search for it. It must be on the global level, now.
     Ask lookup_name not to return non-types.  */
  id = lookup_name_real (id, 2, 1, /*block_p=*/true, 0, LOOKUP_COMPLAIN);
  if (id)
    POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, TREE_TYPE (id));
  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, NULL_TREE);
}

/* Return the IDENTIFIER_GLOBAL_VALUE of T, for use in common code, since
   the definition of IDENTIFIER_GLOBAL_VALUE is different for C and C++.  */

tree
identifier_global_value	(tree t)
{
  return IDENTIFIER_GLOBAL_VALUE (t);
}

/* Push a definition of struct, union or enum tag named ID.  into
   binding_level B.  DECL is a TYPE_DECL for the type.  We assume that
   the tag ID is not already defined.  */

static void
set_identifier_type_value_with_scope (tree id, tree decl, cxx_scope *b)
{
  tree type;

  if (b->kind != sk_namespace)
    {
      /* Shadow the marker, not the real thing, so that the marker
	 gets restored later.  */
      tree old_type_value = REAL_IDENTIFIER_TYPE_VALUE (id);
      b->type_shadowed
	= tree_cons (id, old_type_value, b->type_shadowed);
      type = decl ? TREE_TYPE (decl) : NULL_TREE;
      TREE_TYPE (b->type_shadowed) = type;
    }
  else
    {
      cxx_binding *binding =
	binding_for_name (NAMESPACE_LEVEL (current_namespace), id);
      gcc_assert (decl);
      if (binding->value)
	supplement_binding (binding, decl);
      else
	binding->value = decl;

      /* Store marker instead of real type.  */
      type = global_type_node;
    }
  SET_IDENTIFIER_TYPE_VALUE (id, type);
}

/* As set_identifier_type_value_with_scope, but using
   current_binding_level.  */

void
set_identifier_type_value (tree id, tree decl)
{
  set_identifier_type_value_with_scope (id, decl, current_binding_level);
}

/* Return the name for the constructor (or destructor) for the
   specified class TYPE.  When given a template, this routine doesn't
   lose the specialization.  */

static inline tree
constructor_name_full (tree type)
{
  return TYPE_IDENTIFIER (TYPE_MAIN_VARIANT (type));
}

/* Return the name for the constructor (or destructor) for the
   specified class.  When given a template, return the plain
   unspecialized name.  */

tree
constructor_name (tree type)
{
  tree name;
  name = constructor_name_full (type);
  if (IDENTIFIER_TEMPLATE (name))
    name = IDENTIFIER_TEMPLATE (name);
  return name;
}

/* Returns TRUE if NAME is the name for the constructor for TYPE.  */

bool
constructor_name_p (tree name, tree type)
{
  tree ctor_name;

  if (!name)
    return false;

  if (TREE_CODE (name) != IDENTIFIER_NODE)
    return false;

  ctor_name = constructor_name_full (type);
  if (name == ctor_name)
    return true;
  if (IDENTIFIER_TEMPLATE (ctor_name)
      && name == IDENTIFIER_TEMPLATE (ctor_name))
    return true;
  return false;
}

/* Counter used to create anonymous type names.  */

static GTY(()) int anon_cnt;

/* Return an IDENTIFIER which can be used as a name for
   anonymous structs and unions.  */

tree
make_anon_name (void)
{
  char buf[32];

  sprintf (buf, ANON_AGGRNAME_FORMAT, anon_cnt++);
  return get_identifier (buf);
}

/* Return (from the stack of) the BINDING, if any, established at SCOPE.  */

static inline cxx_binding *
find_binding (cxx_scope *scope, cxx_binding *binding)
{
  timevar_push (TV_NAME_LOOKUP);

  for (; binding != NULL; binding = binding->previous)
    if (binding->scope == scope)
      POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, binding);

  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, (cxx_binding *)0);
}

/* Return the binding for NAME in SCOPE, if any.  Otherwise, return NULL.  */

static inline cxx_binding *
cxx_scope_find_binding_for_name (cxx_scope *scope, tree name)
{
  cxx_binding *b = IDENTIFIER_NAMESPACE_BINDINGS (name);
  if (b)
    {
      /* Fold-in case where NAME is used only once.  */
      if (scope == b->scope && b->previous == NULL)
	return b;
      return find_binding (scope, b);
    }
  return NULL;
}

/* Always returns a binding for name in scope.  If no binding is
   found, make a new one.  */

static cxx_binding *
binding_for_name (cxx_scope *scope, tree name)
{
  cxx_binding *result;

  result = cxx_scope_find_binding_for_name (scope, name);
  if (result)
    return result;
  /* Not found, make a new one.  */
  result = cxx_binding_make (NULL, NULL);
  result->previous = IDENTIFIER_NAMESPACE_BINDINGS (name);
  result->scope = scope;
  result->is_local = false;
  result->value_is_inherited = false;
  IDENTIFIER_NAMESPACE_BINDINGS (name) = result;
  return result;
}

/* Insert another USING_DECL into the current binding level, returning
   this declaration. If this is a redeclaration, do nothing, and
   return NULL_TREE if this not in namespace scope (in namespace
   scope, a using decl might extend any previous bindings).  */

static tree
push_using_decl (tree scope, tree name)
{
  tree decl;

  timevar_push (TV_NAME_LOOKUP);
  gcc_assert (TREE_CODE (scope) == NAMESPACE_DECL);
  gcc_assert (TREE_CODE (name) == IDENTIFIER_NODE);
  for (decl = current_binding_level->usings; decl; decl = TREE_CHAIN (decl))
    if (USING_DECL_SCOPE (decl) == scope && DECL_NAME (decl) == name)
      break;
  if (decl)
    POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP,
			    namespace_bindings_p () ? decl : NULL_TREE);
  decl = build_lang_decl (USING_DECL, name, NULL_TREE);
  USING_DECL_SCOPE (decl) = scope;
  TREE_CHAIN (decl) = current_binding_level->usings;
  current_binding_level->usings = decl;
  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, decl);
}

/* Same as pushdecl, but define X in binding-level LEVEL.  We rely on the
   caller to set DECL_CONTEXT properly.  */

tree
pushdecl_with_scope (tree x, cxx_scope *level, bool is_friend)
{
  struct cp_binding_level *b;
  tree function_decl = current_function_decl;

  timevar_push (TV_NAME_LOOKUP);
  current_function_decl = NULL_TREE;
  if (level->kind == sk_class)
    {
      b = class_binding_level;
      class_binding_level = level;
      pushdecl_class_level (x);
      class_binding_level = b;
    }
  else
    {
      b = current_binding_level;
      current_binding_level = level;
      x = pushdecl_maybe_friend (x, is_friend);
      current_binding_level = b;
    }
  current_function_decl = function_decl;
  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, x);
}

/* DECL is a FUNCTION_DECL for a non-member function, which may have
   other definitions already in place.  We get around this by making
   the value of the identifier point to a list of all the things that
   want to be referenced by that name.  It is then up to the users of
   that name to decide what to do with that list.

   DECL may also be a TEMPLATE_DECL, with a FUNCTION_DECL in its
   DECL_TEMPLATE_RESULT.  It is dealt with the same way.

   FLAGS is a bitwise-or of the following values:
     PUSH_LOCAL: Bind DECL in the current scope, rather than at
		 namespace scope.
     PUSH_USING: DECL is being pushed as the result of a using
		 declaration.

   IS_FRIEND is true if this is a friend declaration.

   The value returned may be a previous declaration if we guessed wrong
   about what language DECL should belong to (C or C++).  Otherwise,
   it's always DECL (and never something that's not a _DECL).  */

static tree
push_overloaded_decl (tree decl, int flags, bool is_friend)
{
  tree name = DECL_NAME (decl);
  tree old;
  tree new_binding;
  int doing_global = (namespace_bindings_p () || !(flags & PUSH_LOCAL));

  timevar_push (TV_NAME_LOOKUP);
  if (doing_global)
    old = namespace_binding (name, DECL_CONTEXT (decl));
  else
    old = lookup_name_innermost_nonclass_level (name);

  if (old)
    {
      if (TREE_CODE (old) == TYPE_DECL && DECL_ARTIFICIAL (old))
	{
	  tree t = TREE_TYPE (old);
	  if (IS_AGGR_TYPE (t) && warn_shadow
	      && (! DECL_IN_SYSTEM_HEADER (decl)
		  || ! DECL_IN_SYSTEM_HEADER (old)))
	    warning (0, "%q#D hides constructor for %q#T", decl, t);
	  old = NULL_TREE;
	}
      else if (is_overloaded_fn (old))
	{
	  tree tmp;

	  for (tmp = old; tmp; tmp = OVL_NEXT (tmp))
	    {
	      tree fn = OVL_CURRENT (tmp);
	      tree dup;

	      if (TREE_CODE (tmp) == OVERLOAD && OVL_USED (tmp)
		  && !(flags & PUSH_USING)
		  && compparms (TYPE_ARG_TYPES (TREE_TYPE (fn)),
				TYPE_ARG_TYPES (TREE_TYPE (decl)))
		  && ! decls_match (fn, decl))
		error ("%q#D conflicts with previous using declaration %q#D",
		       decl, fn);

	      dup = duplicate_decls (decl, fn, is_friend);
	      /* If DECL was a redeclaration of FN -- even an invalid
		 one -- pass that information along to our caller.  */
	      if (dup == fn || dup == error_mark_node)
		POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, dup);
	    }

	  /* We don't overload implicit built-ins.  duplicate_decls()
	     may fail to merge the decls if the new decl is e.g. a
	     template function.  */
	  if (TREE_CODE (old) == FUNCTION_DECL
	      && DECL_ANTICIPATED (old)
	      && !DECL_HIDDEN_FRIEND_P (old))
	    old = NULL;
	}
      else if (old == error_mark_node)
	/* Ignore the undefined symbol marker.  */
	old = NULL_TREE;
      else
	{
	  error ("previous non-function declaration %q+#D", old);
	  error ("conflicts with function declaration %q#D", decl);
	  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, decl);
	}
    }

  if (old || TREE_CODE (decl) == TEMPLATE_DECL
      /* If it's a using declaration, we always need to build an OVERLOAD,
	 because it's the only way to remember that the declaration comes
	 from 'using', and have the lookup behave correctly.  */
      || (flags & PUSH_USING))
    {
      if (old && TREE_CODE (old) != OVERLOAD)
	new_binding = ovl_cons (decl, ovl_cons (old, NULL_TREE));
      else
	new_binding = ovl_cons (decl, old);
      if (flags & PUSH_USING)
	OVL_USED (new_binding) = 1;
    }
  else
    /* NAME is not ambiguous.  */
    new_binding = decl;

  if (doing_global)
    set_namespace_binding (name, current_namespace, new_binding);
  else
    {
      /* We only create an OVERLOAD if there was a previous binding at
	 this level, or if decl is a template. In the former case, we
	 need to remove the old binding and replace it with the new
	 binding.  We must also run through the NAMES on the binding
	 level where the name was bound to update the chain.  */

      if (TREE_CODE (new_binding) == OVERLOAD && old)
	{
	  tree *d;

	  for (d = &IDENTIFIER_BINDING (name)->scope->names;
	       *d;
	       d = &TREE_CHAIN (*d))
	    if (*d == old
		|| (TREE_CODE (*d) == TREE_LIST
		    && TREE_VALUE (*d) == old))
	      {
		if (TREE_CODE (*d) == TREE_LIST)
		  /* Just replace the old binding with the new.  */
		  TREE_VALUE (*d) = new_binding;
		else
		  /* Build a TREE_LIST to wrap the OVERLOAD.  */
		  *d = tree_cons (NULL_TREE, new_binding,
				  TREE_CHAIN (*d));

		/* And update the cxx_binding node.  */
		IDENTIFIER_BINDING (name)->value = new_binding;
		POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, decl);
	      }

	  /* We should always find a previous binding in this case.  */
	  gcc_unreachable ();
	}

      /* Install the new binding.  */
      push_local_binding (name, new_binding, flags);
    }

  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, decl);
}

/* Check a non-member using-declaration. Return the name and scope
   being used, and the USING_DECL, or NULL_TREE on failure.  */

static tree
validate_nonmember_using_decl (tree decl, tree scope, tree name)
{
  /* [namespace.udecl]
       A using-declaration for a class member shall be a
       member-declaration.  */
  if (TYPE_P (scope))
    {
      error ("%qT is not a namespace", scope);
      return NULL_TREE;
    }
  else if (scope == error_mark_node)
    return NULL_TREE;

  if (TREE_CODE (decl) == TEMPLATE_ID_EXPR)
    {
      /* 7.3.3/5
	   A using-declaration shall not name a template-id.  */
      error ("a using-declaration cannot specify a template-id.  "
	     "Try %<using %D%>", name);
      return NULL_TREE;
    }

  if (TREE_CODE (decl) == NAMESPACE_DECL)
    {
      error ("namespace %qD not allowed in using-declaration", decl);
      return NULL_TREE;
    }

  if (TREE_CODE (decl) == SCOPE_REF)
    {
      /* It's a nested name with template parameter dependent scope.
	 This can only be using-declaration for class member.  */
      error ("%qT is not a namespace", TREE_OPERAND (decl, 0));
      return NULL_TREE;
    }

  if (is_overloaded_fn (decl))
    decl = get_first_fn (decl);

  gcc_assert (DECL_P (decl));

  /* Make a USING_DECL.  */
  return push_using_decl (scope, name);
}

/* Process local and global using-declarations.  */

static void
do_nonmember_using_decl (tree scope, tree name, tree oldval, tree oldtype,
			 tree *newval, tree *newtype)
{
  struct scope_binding decls = EMPTY_SCOPE_BINDING;

  *newval = *newtype = NULL_TREE;
  if (!qualified_lookup_using_namespace (name, scope, &decls, 0))
    /* Lookup error */
    return;

  if (!decls.value && !decls.type)
    {
      error ("%qD not declared", name);
      return;
    }

  /* It is impossible to overload a built-in function; any explicit
     declaration eliminates the built-in declaration.  So, if OLDVAL
     is a built-in, then we can just pretend it isn't there.  */
  if (oldval
      && TREE_CODE (oldval) == FUNCTION_DECL
      && DECL_ANTICIPATED (oldval)
      && !DECL_HIDDEN_FRIEND_P (oldval))
    oldval = NULL_TREE;

  /* Check for using functions.  */
  if (decls.value && is_overloaded_fn (decls.value))
    {
      tree tmp, tmp1;

      if (oldval && !is_overloaded_fn (oldval))
	{
	  if (!DECL_IMPLICIT_TYPEDEF_P (oldval))
	    error ("%qD is already declared in this scope", name);
	  oldval = NULL_TREE;
	}

      *newval = oldval;
      for (tmp = decls.value; tmp; tmp = OVL_NEXT (tmp))
	{
	  tree new_fn = OVL_CURRENT (tmp);

	  /* [namespace.udecl]

	     If a function declaration in namespace scope or block
	     scope has the same name and the same parameter types as a
	     function introduced by a using declaration the program is
	     ill-formed.  */
	  for (tmp1 = oldval; tmp1; tmp1 = OVL_NEXT (tmp1))
	    {
	      tree old_fn = OVL_CURRENT (tmp1);

	      if (new_fn == old_fn)
		/* The function already exists in the current namespace.  */
		break;
	      else if (OVL_USED (tmp1))
		continue; /* this is a using decl */
	      else if (compparms (TYPE_ARG_TYPES (TREE_TYPE (new_fn)),
				  TYPE_ARG_TYPES (TREE_TYPE (old_fn))))
		{
		  gcc_assert (!DECL_ANTICIPATED (old_fn)
			      || DECL_HIDDEN_FRIEND_P (old_fn));

		  /* There was already a non-using declaration in
		     this scope with the same parameter types. If both
		     are the same extern "C" functions, that's ok.  */
		  if (decls_match (new_fn, old_fn))
		    break;
		  else
		    {
		      error ("%qD is already declared in this scope", name);
		      break;
		    }
		}
	    }

	  /* If we broke out of the loop, there's no reason to add
	     this function to the using declarations for this
	     scope.  */
	  if (tmp1)
	    continue;

	  /* If we are adding to an existing OVERLOAD, then we no
	     longer know the type of the set of functions.  */
	  if (*newval && TREE_CODE (*newval) == OVERLOAD)
	    TREE_TYPE (*newval) = unknown_type_node;
	  /* Add this new function to the set.  */
	  *newval = build_overload (OVL_CURRENT (tmp), *newval);
	  /* If there is only one function, then we use its type.  (A
	     using-declaration naming a single function can be used in
	     contexts where overload resolution cannot be
	     performed.)  */
	  if (TREE_CODE (*newval) != OVERLOAD)
	    {
	      *newval = ovl_cons (*newval, NULL_TREE);
	      TREE_TYPE (*newval) = TREE_TYPE (OVL_CURRENT (tmp));
	    }
	  OVL_USED (*newval) = 1;
	}
    }
  else
    {
      *newval = decls.value;
      if (oldval && !decls_match (*newval, oldval))
	error ("%qD is already declared in this scope", name);
    }

  *newtype = decls.type;
  if (oldtype && *newtype && !same_type_p (oldtype, *newtype))
    {
      error ("using declaration %qD introduced ambiguous type %qT",
	     name, oldtype);
      return;
    }
}

/* Process a using-declaration at function scope.  */

void
do_local_using_decl (tree decl, tree scope, tree name)
{
  tree oldval, oldtype, newval, newtype;
  tree orig_decl = decl;

  decl = validate_nonmember_using_decl (decl, scope, name);
  if (decl == NULL_TREE)
    return;

  if (building_stmt_tree ()
      && at_function_scope_p ())
    add_decl_expr (decl);

  oldval = lookup_name_innermost_nonclass_level (name);
  oldtype = lookup_type_current_level (name);

  do_nonmember_using_decl (scope, name, oldval, oldtype, &newval, &newtype);

  if (newval)
    {
      if (is_overloaded_fn (newval))
	{
	  tree fn, term;

	  /* We only need to push declarations for those functions
	     that were not already bound in the current level.
	     The old value might be NULL_TREE, it might be a single
	     function, or an OVERLOAD.  */
	  if (oldval && TREE_CODE (oldval) == OVERLOAD)
	    term = OVL_FUNCTION (oldval);
	  else
	    term = oldval;
	  for (fn = newval; fn && OVL_CURRENT (fn) != term;
	       fn = OVL_NEXT (fn))
	    push_overloaded_decl (OVL_CURRENT (fn),
				  PUSH_LOCAL | PUSH_USING,
				  false);
	}
      else
	push_local_binding (name, newval, PUSH_USING);
    }
  if (newtype)
    {
      push_local_binding (name, newtype, PUSH_USING);
      set_identifier_type_value (name, newtype);
    }

  /* Emit debug info.  */
  if (!processing_template_decl)
    cp_emit_debug_info_for_using (orig_decl, current_scope());
}

/* Returns true if ROOT (a namespace, class, or function) encloses
   CHILD.  CHILD may be either a class type or a namespace.  */

bool
is_ancestor (tree root, tree child)
{
  gcc_assert ((TREE_CODE (root) == NAMESPACE_DECL
	       || TREE_CODE (root) == FUNCTION_DECL
	       || CLASS_TYPE_P (root)));
  gcc_assert ((TREE_CODE (child) == NAMESPACE_DECL
	       || CLASS_TYPE_P (child)));

  /* The global namespace encloses everything.  */
  if (root == global_namespace)
    return true;

  while (true)
    {
      /* If we've run out of scopes, stop.  */
      if (!child)
	return false;
      /* If we've reached the ROOT, it encloses CHILD.  */
      if (root == child)
	return true;
      /* Go out one level.  */
      if (TYPE_P (child))
	child = TYPE_NAME (child);
      child = DECL_CONTEXT (child);
    }
}

/* Enter the class or namespace scope indicated by T suitable for name
   lookup.  T can be arbitrary scope, not necessary nested inside the
   current scope.  Returns a non-null scope to pop iff pop_scope
   should be called later to exit this scope.  */

tree
push_scope (tree t)
{
  if (TREE_CODE (t) == NAMESPACE_DECL)
    push_decl_namespace (t);
  else if (CLASS_TYPE_P (t))
    {
      if (!at_class_scope_p ()
	  || !same_type_p (current_class_type, t))
	push_nested_class (t);
      else
	/* T is the same as the current scope.  There is therefore no
	   need to re-enter the scope.  Since we are not actually
	   pushing a new scope, our caller should not call
	   pop_scope.  */
	t = NULL_TREE;
    }

  return t;
}

/* Leave scope pushed by push_scope.  */

void
pop_scope (tree t)
{
  if (TREE_CODE (t) == NAMESPACE_DECL)
    pop_decl_namespace ();
  else if CLASS_TYPE_P (t)
    pop_nested_class ();
}

/* Subroutine of push_inner_scope.  */

static void
push_inner_scope_r (tree outer, tree inner)
{
  tree prev;

  if (outer == inner
      || (TREE_CODE (inner) != NAMESPACE_DECL && !CLASS_TYPE_P (inner)))
    return;

  prev = CP_DECL_CONTEXT (TREE_CODE (inner) == NAMESPACE_DECL ? inner : TYPE_NAME (inner));
  if (outer != prev)
    push_inner_scope_r (outer, prev);
  if (TREE_CODE (inner) == NAMESPACE_DECL)
    {
      struct cp_binding_level *save_template_parm = 0;
      /* Temporary take out template parameter scopes.  They are saved
	 in reversed order in save_template_parm.  */
      while (current_binding_level->kind == sk_template_parms)
	{
	  struct cp_binding_level *b = current_binding_level;
	  current_binding_level = b->level_chain;
	  b->level_chain = save_template_parm;
	  save_template_parm = b;
	}

      resume_scope (NAMESPACE_LEVEL (inner));
      current_namespace = inner;

      /* Restore template parameter scopes.  */
      while (save_template_parm)
	{
	  struct cp_binding_level *b = save_template_parm;
	  save_template_parm = b->level_chain;
	  b->level_chain = current_binding_level;
	  current_binding_level = b;
	}
    }
  else
    pushclass (inner);
}

/* Enter the scope INNER from current scope.  INNER must be a scope
   nested inside current scope.  This works with both name lookup and
   pushing name into scope.  In case a template parameter scope is present,
   namespace is pushed under the template parameter scope according to
   name lookup rule in 14.6.1/6.

   Return the former current scope suitable for pop_inner_scope.  */

tree
push_inner_scope (tree inner)
{
  tree outer = current_scope ();
  if (!outer)
    outer = current_namespace;

  push_inner_scope_r (outer, inner);
  return outer;
}

/* Exit the current scope INNER back to scope OUTER.  */

void
pop_inner_scope (tree outer, tree inner)
{
  if (outer == inner
      || (TREE_CODE (inner) != NAMESPACE_DECL && !CLASS_TYPE_P (inner)))
    return;

  while (outer != inner)
    {
      if (TREE_CODE (inner) == NAMESPACE_DECL)
	{
	  struct cp_binding_level *save_template_parm = 0;
	  /* Temporary take out template parameter scopes.  They are saved
	     in reversed order in save_template_parm.  */
	  while (current_binding_level->kind == sk_template_parms)
	    {
	      struct cp_binding_level *b = current_binding_level;
	      current_binding_level = b->level_chain;
	      b->level_chain = save_template_parm;
	      save_template_parm = b;
	    }

	  pop_namespace ();

	  /* Restore template parameter scopes.  */
	  while (save_template_parm)
	    {
	      struct cp_binding_level *b = save_template_parm;
	      save_template_parm = b->level_chain;
	      b->level_chain = current_binding_level;
	      current_binding_level = b;
	    }
	}
      else
	popclass ();

      inner = CP_DECL_CONTEXT (TREE_CODE (inner) == NAMESPACE_DECL ? inner : TYPE_NAME (inner));
    }
}

/* Do a pushlevel for class declarations.  */

void
pushlevel_class (void)
{
  if (ENABLE_SCOPE_CHECKING)
    is_class_level = 1;

  class_binding_level = begin_scope (sk_class, current_class_type);
}

/* ...and a poplevel for class declarations.  */

void
poplevel_class (void)
{
  struct cp_binding_level *level = class_binding_level;
  cp_class_binding *cb;
  size_t i;
  tree shadowed;

  timevar_push (TV_NAME_LOOKUP);
  gcc_assert (level != 0);

  /* If we're leaving a toplevel class, cache its binding level.  */
  if (current_class_depth == 1)
    previous_class_level = level;
  for (shadowed = level->type_shadowed;
       shadowed;
       shadowed = TREE_CHAIN (shadowed))
    SET_IDENTIFIER_TYPE_VALUE (TREE_PURPOSE (shadowed), TREE_VALUE (shadowed));

  /* Remove the bindings for all of the class-level declarations.  */
  if (level->class_shadowed)
    {
      for (i = 0;
	   VEC_iterate (cp_class_binding, level->class_shadowed, i, cb);
	   ++i)
	IDENTIFIER_BINDING (cb->identifier) = cb->base.previous;
      ggc_free (level->class_shadowed);
      level->class_shadowed = NULL;
    }

  /* Now, pop out of the binding level which we created up in the
     `pushlevel_class' routine.  */
  if (ENABLE_SCOPE_CHECKING)
    is_class_level = 1;

  leave_scope ();
  timevar_pop (TV_NAME_LOOKUP);
}

/* Set INHERITED_VALUE_BINDING_P on BINDING to true or false, as
   appropriate.  DECL is the value to which a name has just been
   bound.  CLASS_TYPE is the class in which the lookup occurred.  */

static void
set_inherited_value_binding_p (cxx_binding *binding, tree decl,
			       tree class_type)
{
  if (binding->value == decl && TREE_CODE (decl) != TREE_LIST)
    {
      tree context;

      if (TREE_CODE (decl) == OVERLOAD)
	context = CP_DECL_CONTEXT (OVL_CURRENT (decl));
      else
	{
	  gcc_assert (DECL_P (decl));
	  context = context_for_name_lookup (decl);
	}

      if (is_properly_derived_from (class_type, context))
	INHERITED_VALUE_BINDING_P (binding) = 1;
      else
	INHERITED_VALUE_BINDING_P (binding) = 0;
    }
  else if (binding->value == decl)
    /* We only encounter a TREE_LIST when there is an ambiguity in the
       base classes.  Such an ambiguity can be overridden by a
       definition in this class.  */
    INHERITED_VALUE_BINDING_P (binding) = 1;
  else
    INHERITED_VALUE_BINDING_P (binding) = 0;
}

/* Make the declaration of X appear in CLASS scope.  */

bool
pushdecl_class_level (tree x)
{
  tree name;
  bool is_valid = true;

  timevar_push (TV_NAME_LOOKUP);
  /* Get the name of X.  */
  if (TREE_CODE (x) == OVERLOAD)
    name = DECL_NAME (get_first_fn (x));
  else
    name = DECL_NAME (x);

  if (name)
    {
      is_valid = push_class_level_binding (name, x);
      if (TREE_CODE (x) == TYPE_DECL)
	set_identifier_type_value (name, x);
    }
  else if (ANON_AGGR_TYPE_P (TREE_TYPE (x)))
    {
      /* If X is an anonymous aggregate, all of its members are
	 treated as if they were members of the class containing the
	 aggregate, for naming purposes.  */
      tree f;

      for (f = TYPE_FIELDS (TREE_TYPE (x)); f; f = TREE_CHAIN (f))
	{
	  location_t save_location = input_location;
	  input_location = DECL_SOURCE_LOCATION (f);
	  if (!pushdecl_class_level (f))
	    is_valid = false;
	  input_location = save_location;
	}
    }
  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, is_valid);
}

/* Return the BINDING (if any) for NAME in SCOPE, which is a class
   scope.  If the value returned is non-NULL, and the PREVIOUS field
   is not set, callers must set the PREVIOUS field explicitly.  */

static cxx_binding *
get_class_binding (tree name, cxx_scope *scope)
{
  tree class_type;
  tree type_binding;
  tree value_binding;
  cxx_binding *binding;

  class_type = scope->this_entity;

  /* Get the type binding.  */
  type_binding = lookup_member (class_type, name,
				/*protect=*/2, /*want_type=*/true);
  /* Get the value binding.  */
  value_binding = lookup_member (class_type, name,
				 /*protect=*/2, /*want_type=*/false);

  if (value_binding
      && (TREE_CODE (value_binding) == TYPE_DECL
	  || DECL_CLASS_TEMPLATE_P (value_binding)
	  || (TREE_CODE (value_binding) == TREE_LIST
	      && TREE_TYPE (value_binding) == error_mark_node
	      && (TREE_CODE (TREE_VALUE (value_binding))
		  == TYPE_DECL))))
    /* We found a type binding, even when looking for a non-type
       binding.  This means that we already processed this binding
       above.  */
    ;
  else if (value_binding)
    {
      if (TREE_CODE (value_binding) == TREE_LIST
	  && TREE_TYPE (value_binding) == error_mark_node)
	/* NAME is ambiguous.  */
	;
      else if (BASELINK_P (value_binding))
	/* NAME is some overloaded functions.  */
	value_binding = BASELINK_FUNCTIONS (value_binding);
    }

  /* If we found either a type binding or a value binding, create a
     new binding object.  */
  if (type_binding || value_binding)
    {
      binding = new_class_binding (name,
				   value_binding,
				   type_binding,
				   scope);
      /* This is a class-scope binding, not a block-scope binding.  */
      LOCAL_BINDING_P (binding) = 0;
      set_inherited_value_binding_p (binding, value_binding, class_type);
    }
  else
    binding = NULL;

  return binding;
}

/* Make the declaration(s) of X appear in CLASS scope under the name
   NAME.  Returns true if the binding is valid.  */

bool
push_class_level_binding (tree name, tree x)
{
  cxx_binding *binding;
  tree decl = x;
  bool ok;

  timevar_push (TV_NAME_LOOKUP);
  /* The class_binding_level will be NULL if x is a template
     parameter name in a member template.  */
  if (!class_binding_level)
    POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, true);

  if (name == error_mark_node)
    POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, false);

  /* Check for invalid member names.  */
  gcc_assert (TYPE_BEING_DEFINED (current_class_type));
  /* We could have been passed a tree list if this is an ambiguous
     declaration. If so, pull the declaration out because
     check_template_shadow will not handle a TREE_LIST.  */
  if (TREE_CODE (decl) == TREE_LIST
      && TREE_TYPE (decl) == error_mark_node)
    decl = TREE_VALUE (decl);

  check_template_shadow (decl);

  /* [class.mem]

     If T is the name of a class, then each of the following shall
     have a name different from T:

     -- every static data member of class T;

     -- every member of class T that is itself a type;

     -- every enumerator of every member of class T that is an
	enumerated type;

     -- every member of every anonymous union that is a member of
	class T.

     (Non-static data members were also forbidden to have the same
     name as T until TC1.)  */
  if ((TREE_CODE (x) == VAR_DECL
       || TREE_CODE (x) == CONST_DECL
       || (TREE_CODE (x) == TYPE_DECL
	   && !DECL_SELF_REFERENCE_P (x))
       /* A data member of an anonymous union.  */
       || (TREE_CODE (x) == FIELD_DECL
	   && DECL_CONTEXT (x) != current_class_type))
      && DECL_NAME (x) == constructor_name (current_class_type))
    {
      tree scope = context_for_name_lookup (x);
      if (TYPE_P (scope) && same_type_p (scope, current_class_type))
	{
	  error ("%qD has the same name as the class in which it is "
		 "declared",
		 x);
	  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, false);
	}
    }

  /* Get the current binding for NAME in this class, if any.  */
  binding = IDENTIFIER_BINDING (name);
  if (!binding || binding->scope != class_binding_level)
    {
      binding = get_class_binding (name, class_binding_level);
      /* If a new binding was created, put it at the front of the
	 IDENTIFIER_BINDING list.  */
      if (binding)
	{
	  binding->previous = IDENTIFIER_BINDING (name);
	  IDENTIFIER_BINDING (name) = binding;
	}
    }

  /* If there is already a binding, then we may need to update the
     current value.  */
  if (binding && binding->value)
    {
      tree bval = binding->value;
      tree old_decl = NULL_TREE;

      if (INHERITED_VALUE_BINDING_P (binding))
	{
	  /* If the old binding was from a base class, and was for a
	     tag name, slide it over to make room for the new binding.
	     The old binding is still visible if explicitly qualified
	     with a class-key.  */
	  if (TREE_CODE (bval) == TYPE_DECL && DECL_ARTIFICIAL (bval)
	      && !(TREE_CODE (x) == TYPE_DECL && DECL_ARTIFICIAL (x)))
	    {
	      old_decl = binding->type;
	      binding->type = bval;
	      binding->value = NULL_TREE;
	      INHERITED_VALUE_BINDING_P (binding) = 0;
	    }
	  else
	    {
	      old_decl = bval;
	      /* Any inherited type declaration is hidden by the type
		 declaration in the derived class.  */
	      if (TREE_CODE (x) == TYPE_DECL && DECL_ARTIFICIAL (x))
		binding->type = NULL_TREE;
	    }
	}
      else if (TREE_CODE (x) == OVERLOAD && is_overloaded_fn (bval))
	old_decl = bval;
      else if (TREE_CODE (x) == USING_DECL && TREE_CODE (bval) == USING_DECL)
	POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, true);
      else if (TREE_CODE (x) == USING_DECL && is_overloaded_fn (bval))
	old_decl = bval;
      else if (TREE_CODE (bval) == USING_DECL && is_overloaded_fn (x))
	POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, true);

      if (old_decl && binding->scope == class_binding_level)
	{
	  binding->value = x;
	  /* It is always safe to clear INHERITED_VALUE_BINDING_P
	     here.  This function is only used to register bindings
	     from with the class definition itself.  */
	  INHERITED_VALUE_BINDING_P (binding) = 0;
	  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, true);
	}
    }

  /* Note that we declared this value so that we can issue an error if
     this is an invalid redeclaration of a name already used for some
     other purpose.  */
  note_name_declared_in_class (name, decl);

  /* If we didn't replace an existing binding, put the binding on the
     stack of bindings for the identifier, and update the shadowed
     list.  */
  if (binding && binding->scope == class_binding_level)
    /* Supplement the existing binding.  */
    ok = supplement_binding (binding, decl);
  else
    {
      /* Create a new binding.  */
      push_binding (name, decl, class_binding_level);
      ok = true;
    }

  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, ok);
}

/* Process "using SCOPE::NAME" in a class scope.  Return the
   USING_DECL created.  */

tree
do_class_using_decl (tree scope, tree name)
{
  /* The USING_DECL returned by this function.  */
  tree value;
  /* The declaration (or declarations) name by this using
     declaration.  NULL if we are in a template and cannot figure out
     what has been named.  */
  tree decl;
  /* True if SCOPE is a dependent type.  */
  bool scope_dependent_p;
  /* True if SCOPE::NAME is dependent.  */
  bool name_dependent_p;
  /* True if any of the bases of CURRENT_CLASS_TYPE are dependent.  */
  bool bases_dependent_p;
  tree binfo;
  tree base_binfo;
  int i;

  if (name == error_mark_node)
    return NULL_TREE;

  if (!scope || !TYPE_P (scope))
    {
      error ("using-declaration for non-member at class scope");
      return NULL_TREE;
    }

  /* Make sure the name is not invalid */
  if (TREE_CODE (name) == BIT_NOT_EXPR)
    {
      error ("%<%T::%D%> names destructor", scope, name);
      return NULL_TREE;
    }
  if (constructor_name_p (name, scope))
    {
      error ("%<%T::%D%> names constructor", scope, name);
      return NULL_TREE;
    }
  if (constructor_name_p (name, current_class_type))
    {
      error ("%<%T::%D%> names constructor in %qT",
	     scope, name, current_class_type);
      return NULL_TREE;
    }

  scope_dependent_p = dependent_type_p (scope);
  name_dependent_p = (scope_dependent_p
		      || (IDENTIFIER_TYPENAME_P (name)
			  && dependent_type_p (TREE_TYPE (name))));

  bases_dependent_p = false;
  if (processing_template_decl)
    for (binfo = TYPE_BINFO (current_class_type), i = 0;
	 BINFO_BASE_ITERATE (binfo, i, base_binfo);
	 i++)
      if (dependent_type_p (TREE_TYPE (base_binfo)))
	{
	  bases_dependent_p = true;
	  break;
	}

  decl = NULL_TREE;

  /* From [namespace.udecl]:

       A using-declaration used as a member-declaration shall refer to a
       member of a base class of the class being defined.

     In general, we cannot check this constraint in a template because
     we do not know the entire set of base classes of the current
     class type.  However, if all of the base classes are
     non-dependent, then we can avoid delaying the check until
     instantiation.  */
  if (!scope_dependent_p)
    {
      base_kind b_kind;
      binfo = lookup_base (current_class_type, scope, ba_any, &b_kind);
      if (b_kind < bk_proper_base)
	{
	  if (!bases_dependent_p)
	    {
	      error_not_base_type (scope, current_class_type);
	      return NULL_TREE;
	    }
	}
      else if (!name_dependent_p)
	{
	  decl = lookup_member (binfo, name, 0, false);
	  if (!decl)
	    {
	      error ("no members matching %<%T::%D%> in %q#T", scope, name,
		     scope);
	      return NULL_TREE;
	    }
	  /* The binfo from which the functions came does not matter.  */
	  if (BASELINK_P (decl))
	    decl = BASELINK_FUNCTIONS (decl);
	}
   }

  value = build_lang_decl (USING_DECL, name, NULL_TREE);
  USING_DECL_DECLS (value) = decl;
  USING_DECL_SCOPE (value) = scope;
  DECL_DEPENDENT_P (value) = !decl;

  return value;
}


/* Return the binding value for name in scope.  */

tree
namespace_binding (tree name, tree scope)
{
  cxx_binding *binding;

  if (scope == NULL)
    scope = global_namespace;
  else
    /* Unnecessary for the global namespace because it can't be an alias. */
    scope = ORIGINAL_NAMESPACE (scope);

  binding = cxx_scope_find_binding_for_name (NAMESPACE_LEVEL (scope), name);

  return binding ? binding->value : NULL_TREE;
}

/* Set the binding value for name in scope.  */

void
set_namespace_binding (tree name, tree scope, tree val)
{
  cxx_binding *b;

  timevar_push (TV_NAME_LOOKUP);
  if (scope == NULL_TREE)
    scope = global_namespace;
  b = binding_for_name (NAMESPACE_LEVEL (scope), name);
  if (!b->value || TREE_CODE (val) == OVERLOAD || val == error_mark_node)
    b->value = val;
  else
    supplement_binding (b, val);
  timevar_pop (TV_NAME_LOOKUP);
}

/* Set the context of a declaration to scope. Complain if we are not
   outside scope.  */

void
set_decl_namespace (tree decl, tree scope, bool friendp)
{
  tree old, fn;

  /* Get rid of namespace aliases.  */
  scope = ORIGINAL_NAMESPACE (scope);

  /* It is ok for friends to be qualified in parallel space.  */
  if (!friendp && !is_ancestor (current_namespace, scope))
    error ("declaration of %qD not in a namespace surrounding %qD",
	   decl, scope);
  DECL_CONTEXT (decl) = FROB_CONTEXT (scope);

  /* Writing "int N::i" to declare a variable within "N" is invalid.  */
  if (scope == current_namespace)
    {
      if (at_namespace_scope_p ())
	error ("explicit qualification in declaration of %qD",
	       decl);
      return;
    }

  /* See whether this has been declared in the namespace.  */
  old = lookup_qualified_name (scope, DECL_NAME (decl), false, true);
  if (!old)
    /* No old declaration at all.  */
    goto complain;
  if (!is_overloaded_fn (decl))
    /* Don't compare non-function decls with decls_match here, since
       it can't check for the correct constness at this
       point. pushdecl will find those errors later.  */
    return;
  /* Since decl is a function, old should contain a function decl.  */
  if (!is_overloaded_fn (old))
    goto complain;
  fn = OVL_CURRENT (old);
  if (!is_associated_namespace (scope, CP_DECL_CONTEXT (fn)))
    goto complain;
  /* A template can be explicitly specialized in any namespace.  */
  if (processing_explicit_instantiation)
    return;
  if (processing_template_decl || processing_specialization)
    /* We have not yet called push_template_decl to turn a
       FUNCTION_DECL into a TEMPLATE_DECL, so the declarations won't
       match.  But, we'll check later, when we construct the
       template.  */
    return;
  /* Instantiations or specializations of templates may be declared as
     friends in any namespace.  */
  if (friendp && DECL_USE_TEMPLATE (decl))
    return;
  if (is_overloaded_fn (old))
    {
      for (; old; old = OVL_NEXT (old))
	if (decls_match (decl, OVL_CURRENT (old)))
	  return;
    }
  else if (decls_match (decl, old))
      return;
 complain:
  error ("%qD should have been declared inside %qD", decl, scope);
}

/* Return the namespace where the current declaration is declared.  */

static tree
current_decl_namespace (void)
{
  tree result;
  /* If we have been pushed into a different namespace, use it.  */
  if (decl_namespace_list)
    return TREE_PURPOSE (decl_namespace_list);

  if (current_class_type)
    result = decl_namespace_context (current_class_type);
  else if (current_function_decl)
    result = decl_namespace_context (current_function_decl);
  else
    result = current_namespace;
  return result;
}

/* Push into the scope of the NAME namespace.  If NAME is NULL_TREE, then we
   select a name that is unique to this compilation unit.  */

void
push_namespace (tree name)
{
  push_namespace_with_attribs (name, NULL_TREE);
}

/* Same, but specify attributes to apply to the namespace.  The attributes
   only apply to the current namespace-body, not to any later extensions. */

void
push_namespace_with_attribs (tree name, tree attributes)
{
  tree d = NULL_TREE;
  int need_new = 1;
  int implicit_use = 0;
  bool anon = !name;

  timevar_push (TV_NAME_LOOKUP);

  /* We should not get here if the global_namespace is not yet constructed
     nor if NAME designates the global namespace:  The global scope is
     constructed elsewhere.  */
  gcc_assert (global_namespace != NULL && name != global_scope_name);

  if (anon)
    {
      /* The name of anonymous namespace is unique for the translation
	 unit.  */
      if (!anonymous_namespace_name)
	anonymous_namespace_name = get_file_function_name ('N');
      name = anonymous_namespace_name;
      d = IDENTIFIER_NAMESPACE_VALUE (name);
      if (d)
	/* Reopening anonymous namespace.  */
	need_new = 0;
      implicit_use = 1;
    }
  else
    {
      /* Check whether this is an extended namespace definition.  */
      d = IDENTIFIER_NAMESPACE_VALUE (name);
      if (d != NULL_TREE && TREE_CODE (d) == NAMESPACE_DECL)
	{
	  need_new = 0;
	  if (DECL_NAMESPACE_ALIAS (d))
	    {
	      error ("namespace alias %qD not allowed here, assuming %qD",
		     d, DECL_NAMESPACE_ALIAS (d));
	      d = DECL_NAMESPACE_ALIAS (d);
	    }
	}
    }

  if (need_new)
    {
      /* Make a new namespace, binding the name to it.  */
      d = build_lang_decl (NAMESPACE_DECL, name, void_type_node);
      DECL_CONTEXT (d) = FROB_CONTEXT (current_namespace);
      /* The name of this namespace is not visible to other translation
	 units if it is an anonymous namespace or member thereof.  */
      if (anon || decl_anon_ns_mem_p (current_namespace))
	TREE_PUBLIC (d) = 0;
      else
	TREE_PUBLIC (d) = 1;
      pushdecl (d);
      if (anon)
	{
	  /* Clear DECL_NAME for the benefit of debugging back ends.  */
	  SET_DECL_ASSEMBLER_NAME (d, name);
	  DECL_NAME (d) = NULL_TREE;
	}
      begin_scope (sk_namespace, d);
    }
  else
    resume_scope (NAMESPACE_LEVEL (d));

  if (implicit_use)
    do_using_directive (d);
  /* Enter the name space.  */
  current_namespace = d;

#ifdef HANDLE_PRAGMA_VISIBILITY
  /* Clear has_visibility in case a previous namespace-definition had a
     visibility attribute and this one doesn't.  */
  current_binding_level->has_visibility = 0;
  for (d = attributes; d; d = TREE_CHAIN (d))
    {
      tree name = TREE_PURPOSE (d);
      tree args = TREE_VALUE (d);
      tree x;

      if (! is_attribute_p ("visibility", name))
	{
	  warning (OPT_Wattributes, "%qs attribute directive ignored",
		   IDENTIFIER_POINTER (name));
	  continue;
	}

      x = args ? TREE_VALUE (args) : NULL_TREE;
      if (x == NULL_TREE || TREE_CODE (x) != STRING_CST || TREE_CHAIN (args))
	{
	  warning (OPT_Wattributes, "%qs attribute requires a single NTBS argument",
		   IDENTIFIER_POINTER (name));
	  continue;
	}

      current_binding_level->has_visibility = 1;
      push_visibility (TREE_STRING_POINTER (x));
      goto found;
    }
 found:
#endif

  timevar_pop (TV_NAME_LOOKUP);
}

/* Pop from the scope of the current namespace.  */

void
pop_namespace (void)
{
  gcc_assert (current_namespace != global_namespace);
  current_namespace = CP_DECL_CONTEXT (current_namespace);
  /* The binding level is not popped, as it might be re-opened later.  */
  leave_scope ();
}

/* Push into the scope of the namespace NS, even if it is deeply
   nested within another namespace.  */

void
push_nested_namespace (tree ns)
{
  if (ns == global_namespace)
    push_to_top_level ();
  else
    {
      push_nested_namespace (CP_DECL_CONTEXT (ns));
      push_namespace (DECL_NAME (ns));
    }
}

/* Pop back from the scope of the namespace NS, which was previously
   entered with push_nested_namespace.  */

void
pop_nested_namespace (tree ns)
{
  timevar_push (TV_NAME_LOOKUP);
  while (ns != global_namespace)
    {
      pop_namespace ();
      ns = CP_DECL_CONTEXT (ns);
    }

  pop_from_top_level ();
  timevar_pop (TV_NAME_LOOKUP);
}

/* Temporarily set the namespace for the current declaration.  */

void
push_decl_namespace (tree decl)
{
  if (TREE_CODE (decl) != NAMESPACE_DECL)
    decl = decl_namespace_context (decl);
  decl_namespace_list = tree_cons (ORIGINAL_NAMESPACE (decl),
				   NULL_TREE, decl_namespace_list);
}

/* [namespace.memdef]/2 */

void
pop_decl_namespace (void)
{
  decl_namespace_list = TREE_CHAIN (decl_namespace_list);
}

/* Return the namespace that is the common ancestor
   of two given namespaces.  */

static tree
namespace_ancestor (tree ns1, tree ns2)
{
  timevar_push (TV_NAME_LOOKUP);
  if (is_ancestor (ns1, ns2))
    POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, ns1);
  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP,
			  namespace_ancestor (CP_DECL_CONTEXT (ns1), ns2));
}

/* Process a namespace-alias declaration.  */

void
do_namespace_alias (tree alias, tree namespace)
{
  if (namespace == error_mark_node)
    return;

  gcc_assert (TREE_CODE (namespace) == NAMESPACE_DECL);

  namespace = ORIGINAL_NAMESPACE (namespace);

  /* Build the alias.  */
  alias = build_lang_decl (NAMESPACE_DECL, alias, void_type_node);
  DECL_NAMESPACE_ALIAS (alias) = namespace;
  DECL_EXTERNAL (alias) = 1;
  DECL_CONTEXT (alias) = FROB_CONTEXT (current_scope ());
  pushdecl (alias);

  /* Emit debug info for namespace alias.  */
  (*debug_hooks->global_decl) (alias);
}

/* Like pushdecl, only it places X in the current namespace,
   if appropriate.  */

tree
pushdecl_namespace_level (tree x, bool is_friend)
{
  struct cp_binding_level *b = current_binding_level;
  tree t;

  timevar_push (TV_NAME_LOOKUP);
  t = pushdecl_with_scope (x, NAMESPACE_LEVEL (current_namespace), is_friend);

  /* Now, the type_shadowed stack may screw us.  Munge it so it does
     what we want.  */
  if (TREE_CODE (t) == TYPE_DECL)
    {
      tree name = DECL_NAME (t);
      tree newval;
      tree *ptr = (tree *)0;
      for (; !global_scope_p (b); b = b->level_chain)
	{
	  tree shadowed = b->type_shadowed;
	  for (; shadowed; shadowed = TREE_CHAIN (shadowed))
	    if (TREE_PURPOSE (shadowed) == name)
	      {
		ptr = &TREE_VALUE (shadowed);
		/* Can't break out of the loop here because sometimes
		   a binding level will have duplicate bindings for
		   PT names.  It's gross, but I haven't time to fix it.  */
	      }
	}
      newval = TREE_TYPE (t);
      if (ptr == (tree *)0)
	{
	  /* @@ This shouldn't be needed.  My test case "zstring.cc" trips
	     up here if this is changed to an assertion.  --KR  */
	  SET_IDENTIFIER_TYPE_VALUE (name, t);
	}
      else
	{
	  *ptr = newval;
	}
    }
  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, t);
}

/* Insert USED into the using list of USER. Set INDIRECT_flag if this
   directive is not directly from the source. Also find the common
   ancestor and let our users know about the new namespace */
static void
add_using_namespace (tree user, tree used, bool indirect)
{
  tree t;
  timevar_push (TV_NAME_LOOKUP);
  /* Using oneself is a no-op.  */
  if (user == used)
    {
      timevar_pop (TV_NAME_LOOKUP);
      return;
    }
  gcc_assert (TREE_CODE (user) == NAMESPACE_DECL);
  gcc_assert (TREE_CODE (used) == NAMESPACE_DECL);
  /* Check if we already have this.  */
  t = purpose_member (used, DECL_NAMESPACE_USING (user));
  if (t != NULL_TREE)
    {
      if (!indirect)
	/* Promote to direct usage.  */
	TREE_INDIRECT_USING (t) = 0;
      timevar_pop (TV_NAME_LOOKUP);
      return;
    }

  /* Add used to the user's using list.  */
  DECL_NAMESPACE_USING (user)
    = tree_cons (used, namespace_ancestor (user, used),
		 DECL_NAMESPACE_USING (user));

  TREE_INDIRECT_USING (DECL_NAMESPACE_USING (user)) = indirect;

  /* Add user to the used's users list.  */
  DECL_NAMESPACE_USERS (used)
    = tree_cons (user, 0, DECL_NAMESPACE_USERS (used));

  /* Recursively add all namespaces used.  */
  for (t = DECL_NAMESPACE_USING (used); t; t = TREE_CHAIN (t))
    /* indirect usage */
    add_using_namespace (user, TREE_PURPOSE (t), 1);

  /* Tell everyone using us about the new used namespaces.  */
  for (t = DECL_NAMESPACE_USERS (user); t; t = TREE_CHAIN (t))
    add_using_namespace (TREE_PURPOSE (t), used, 1);
  timevar_pop (TV_NAME_LOOKUP);
}

/* Process a using-declaration not appearing in class or local scope.  */

void
do_toplevel_using_decl (tree decl, tree scope, tree name)
{
  tree oldval, oldtype, newval, newtype;
  tree orig_decl = decl;
  cxx_binding *binding;

  decl = validate_nonmember_using_decl (decl, scope, name);
  if (decl == NULL_TREE)
    return;

  binding = binding_for_name (NAMESPACE_LEVEL (current_namespace), name);

  oldval = binding->value;
  oldtype = binding->type;

  do_nonmember_using_decl (scope, name, oldval, oldtype, &newval, &newtype);

  /* Emit debug info.  */
  if (!processing_template_decl)
    cp_emit_debug_info_for_using (orig_decl, current_namespace);

  /* Copy declarations found.  */
  if (newval)
    binding->value = newval;
  if (newtype)
    binding->type = newtype;
}

/* Process a using-directive.  */

void
do_using_directive (tree namespace)
{
  tree context = NULL_TREE;

  if (namespace == error_mark_node)
    return;

  gcc_assert (TREE_CODE (namespace) == NAMESPACE_DECL);

  if (building_stmt_tree ())
    add_stmt (build_stmt (USING_STMT, namespace));
  namespace = ORIGINAL_NAMESPACE (namespace);

  if (!toplevel_bindings_p ())
    {
      push_using_directive (namespace);
      context = current_scope ();
    }
  else
    {
      /* direct usage */
      add_using_namespace (current_namespace, namespace, 0);
      if (current_namespace != global_namespace)
	context = current_namespace;
    }

  /* Emit debugging info.  */
  if (!processing_template_decl)
    (*debug_hooks->imported_module_or_decl) (namespace, context);
}

/* Deal with a using-directive seen by the parser.  Currently we only
   handle attributes here, since they cannot appear inside a template.  */

void
parse_using_directive (tree namespace, tree attribs)
{
  tree a;

  do_using_directive (namespace);

  for (a = attribs; a; a = TREE_CHAIN (a))
    {
      tree name = TREE_PURPOSE (a);
      if (is_attribute_p ("strong", name))
	{
	  if (!toplevel_bindings_p ())
	    error ("strong using only meaningful at namespace scope");
	  else if (namespace != error_mark_node)
	    {
	      if (!is_ancestor (current_namespace, namespace))
		error ("current namespace %qD does not enclose strongly used namespace %qD",
		       current_namespace, namespace);
	      DECL_NAMESPACE_ASSOCIATIONS (namespace)
		= tree_cons (current_namespace, 0,
			     DECL_NAMESPACE_ASSOCIATIONS (namespace));
	    }
	}
      else
	warning (OPT_Wattributes, "%qD attribute directive ignored", name);
    }
}

/* Like pushdecl, only it places X in the global scope if appropriate.
   Calls cp_finish_decl to register the variable, initializing it with
   *INIT, if INIT is non-NULL.  */

static tree
pushdecl_top_level_1 (tree x, tree *init, bool is_friend)
{
  timevar_push (TV_NAME_LOOKUP);
  push_to_top_level ();
  x = pushdecl_namespace_level (x, is_friend);
  if (init)
    finish_decl (x, *init, NULL_TREE);
  pop_from_top_level ();
  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, x);
}

/* Like pushdecl, only it places X in the global scope if appropriate.  */

tree
pushdecl_top_level (tree x)
{
  return pushdecl_top_level_1 (x, NULL, false);
}

/* Like pushdecl_top_level, but adding the IS_FRIEND parameter.  */

tree
pushdecl_top_level_maybe_friend (tree x, bool is_friend)
{
  return pushdecl_top_level_1 (x, NULL, is_friend);
}

/* Like pushdecl, only it places X in the global scope if
   appropriate.  Calls cp_finish_decl to register the variable,
   initializing it with INIT.  */

tree
pushdecl_top_level_and_finish (tree x, tree init)
{
  return pushdecl_top_level_1 (x, &init, false);
}

/* Combines two sets of overloaded functions into an OVERLOAD chain, removing
   duplicates.  The first list becomes the tail of the result.

   The algorithm is O(n^2).  We could get this down to O(n log n) by
   doing a sort on the addresses of the functions, if that becomes
   necessary.  */

static tree
merge_functions (tree s1, tree s2)
{
  for (; s2; s2 = OVL_NEXT (s2))
    {
      tree fn2 = OVL_CURRENT (s2);
      tree fns1;

      for (fns1 = s1; fns1; fns1 = OVL_NEXT (fns1))
	{
	  tree fn1 = OVL_CURRENT (fns1);

	  /* If the function from S2 is already in S1, there is no
	     need to add it again.  For `extern "C"' functions, we
	     might have two FUNCTION_DECLs for the same function, in
	     different namespaces; again, we only need one of them.  */
	  if (fn1 == fn2
	      || (DECL_EXTERN_C_P (fn1) && DECL_EXTERN_C_P (fn2)
		  && DECL_NAME (fn1) == DECL_NAME (fn2)))
	    break;
	}

      /* If we exhausted all of the functions in S1, FN2 is new.  */
      if (!fns1)
	s1 = build_overload (fn2, s1);
    }
  return s1;
}

/* This should return an error not all definitions define functions.
   It is not an error if we find two functions with exactly the
   same signature, only if these are selected in overload resolution.
   old is the current set of bindings, new the freshly-found binding.
   XXX Do we want to give *all* candidates in case of ambiguity?
   XXX In what way should I treat extern declarations?
   XXX I don't want to repeat the entire duplicate_decls here */

static void
ambiguous_decl (tree name, struct scope_binding *old, cxx_binding *new,
		int flags)
{
  tree val, type;
  gcc_assert (old != NULL);
  /* Copy the value.  */
  val = new->value;
  if (val)
    switch (TREE_CODE (val))
      {
      case TEMPLATE_DECL:
	/* If we expect types or namespaces, and not templates,
	   or this is not a template class.  */
	if ((LOOKUP_QUALIFIERS_ONLY (flags)
	     && !DECL_CLASS_TEMPLATE_P (val))
	    || hidden_name_p (val))
	  val = NULL_TREE;
	break;
      case TYPE_DECL:
	if (LOOKUP_NAMESPACES_ONLY (flags) || hidden_name_p (val))
	  val = NULL_TREE;
	break;
      case NAMESPACE_DECL:
	if (LOOKUP_TYPES_ONLY (flags))
	  val = NULL_TREE;
	break;
      case FUNCTION_DECL:
	/* Ignore built-in functions that are still anticipated.  */
	if (LOOKUP_QUALIFIERS_ONLY (flags) || hidden_name_p (val))
	  val = NULL_TREE;
	break;
      default:
	if (LOOKUP_QUALIFIERS_ONLY (flags))
	  val = NULL_TREE;
      }

  if (!old->value)
    old->value = val;
  else if (val && val != old->value)
    {
      if (is_overloaded_fn (old->value) && is_overloaded_fn (val))
	old->value = merge_functions (old->value, val);
      else
	{
	  old->value = tree_cons (NULL_TREE, old->value,
				  build_tree_list (NULL_TREE, new->value));
	  TREE_TYPE (old->value) = error_mark_node;
	}
    }
  /* ... and copy the type.  */
  type = new->type;
  if (LOOKUP_NAMESPACES_ONLY (flags))
    type = NULL_TREE;
  if (!old->type)
    old->type = type;
  else if (type && old->type != type)
    {
      if (flags & LOOKUP_COMPLAIN)
	{
	  error ("%qD denotes an ambiguous type",name);
	  error ("%J  first type here", TYPE_MAIN_DECL (old->type));
	  error ("%J  other type here", TYPE_MAIN_DECL (type));
	}
    }
}

/* Return the declarations that are members of the namespace NS.  */

tree
cp_namespace_decls (tree ns)
{
  return NAMESPACE_LEVEL (ns)->names;
}

/* Combine prefer_type and namespaces_only into flags.  */

static int
lookup_flags (int prefer_type, int namespaces_only)
{
  if (namespaces_only)
    return LOOKUP_PREFER_NAMESPACES;
  if (prefer_type > 1)
    return LOOKUP_PREFER_TYPES;
  if (prefer_type > 0)
    return LOOKUP_PREFER_BOTH;
  return 0;
}

/* Given a lookup that returned VAL, use FLAGS to decide if we want to
   ignore it or not.  Subroutine of lookup_name_real and
   lookup_type_scope.  */

static bool
qualify_lookup (tree val, int flags)
{
  if (val == NULL_TREE)
    return false;
  if ((flags & LOOKUP_PREFER_NAMESPACES) && TREE_CODE (val) == NAMESPACE_DECL)
    return true;
  if ((flags & LOOKUP_PREFER_TYPES)
      && (TREE_CODE (val) == TYPE_DECL || TREE_CODE (val) == TEMPLATE_DECL))
    return true;
  if (flags & (LOOKUP_PREFER_NAMESPACES | LOOKUP_PREFER_TYPES))
    return false;
  return true;
}

/* Given a lookup that returned VAL, decide if we want to ignore it or
   not based on DECL_ANTICIPATED.  */

bool
hidden_name_p (tree val)
{
  if (DECL_P (val)
      && DECL_LANG_SPECIFIC (val)
      && DECL_ANTICIPATED (val))
    return true;
  return false;
}

/* Remove any hidden friend functions from a possibly overloaded set
   of functions.  */

tree
remove_hidden_names (tree fns)
{
  if (!fns)
    return fns;

  if (TREE_CODE (fns) == FUNCTION_DECL && hidden_name_p (fns))
    fns = NULL_TREE;
  else if (TREE_CODE (fns) == OVERLOAD)
    {
      tree o;

      for (o = fns; o; o = OVL_NEXT (o))
	if (hidden_name_p (OVL_CURRENT (o)))
	  break;
      if (o)
	{
	  tree n = NULL_TREE;

	  for (o = fns; o; o = OVL_NEXT (o))
	    if (!hidden_name_p (OVL_CURRENT (o)))
	      n = build_overload (OVL_CURRENT (o), n);
	  fns = n;
	}
    }

  return fns;
}

/* Select the right _DECL from multiple choices.  */

static tree
select_decl (const struct scope_binding *binding, int flags)
{
  tree val;
  val = binding->value;

  timevar_push (TV_NAME_LOOKUP);
  if (LOOKUP_NAMESPACES_ONLY (flags))
    {
      /* We are not interested in types.  */
      if (val && (TREE_CODE (val) == NAMESPACE_DECL
		  || TREE_CODE (val) == TREE_LIST))
	POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, val);
      POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, NULL_TREE);
    }

  /* If looking for a type, or if there is no non-type binding, select
     the value binding.  */
  if (binding->type && (!val || (flags & LOOKUP_PREFER_TYPES)))
    val = binding->type;
  /* Don't return non-types if we really prefer types.  */
  else if (val && LOOKUP_TYPES_ONLY (flags)
	   && ! DECL_DECLARES_TYPE_P (val))
    val = NULL_TREE;

  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, val);
}

/* Unscoped lookup of a global: iterate over current namespaces,
   considering using-directives.  */

static tree
unqualified_namespace_lookup (tree name, int flags)
{
  tree initial = current_decl_namespace ();
  tree scope = initial;
  tree siter;
  struct cp_binding_level *level;
  tree val = NULL_TREE;
  struct scope_binding binding = EMPTY_SCOPE_BINDING;

  timevar_push (TV_NAME_LOOKUP);

  for (; !val; scope = CP_DECL_CONTEXT (scope))
    {
      cxx_binding *b =
	 cxx_scope_find_binding_for_name (NAMESPACE_LEVEL (scope), name);

      if (b)
	{
	  if (b->value
	      && ((flags & LOOKUP_HIDDEN) || !hidden_name_p (b->value)))
	    binding.value = b->value;
	  binding.type = b->type;
	}

      /* Add all _DECLs seen through local using-directives.  */
      for (level = current_binding_level;
	   level->kind != sk_namespace;
	   level = level->level_chain)
	if (!lookup_using_namespace (name, &binding, level->using_directives,
				     scope, flags))
	  /* Give up because of error.  */
	  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, error_mark_node);

      /* Add all _DECLs seen through global using-directives.  */
      /* XXX local and global using lists should work equally.  */
      siter = initial;
      while (1)
	{
	  if (!lookup_using_namespace (name, &binding,
				       DECL_NAMESPACE_USING (siter),
				       scope, flags))
	    /* Give up because of error.  */
	    POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, error_mark_node);
	  if (siter == scope) break;
	  siter = CP_DECL_CONTEXT (siter);
	}

      val = select_decl (&binding, flags);
      if (scope == global_namespace)
	break;
    }
  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, val);
}

/* Look up NAME (an IDENTIFIER_NODE) in SCOPE (either a NAMESPACE_DECL
   or a class TYPE).  If IS_TYPE_P is TRUE, then ignore non-type
   bindings.

   Returns a DECL (or OVERLOAD, or BASELINK) representing the
   declaration found.  If no suitable declaration can be found,
   ERROR_MARK_NODE is returned.  If COMPLAIN is true and SCOPE is
   neither a class-type nor a namespace a diagnostic is issued.  */

tree
lookup_qualified_name (tree scope, tree name, bool is_type_p, bool complain)
{
  int flags = 0;
  tree t = NULL_TREE;

  if (TREE_CODE (scope) == NAMESPACE_DECL)
    {
      struct scope_binding binding = EMPTY_SCOPE_BINDING;

      flags |= LOOKUP_COMPLAIN;
      if (is_type_p)
	flags |= LOOKUP_PREFER_TYPES;
      if (qualified_lookup_using_namespace (name, scope, &binding, flags))
	t = select_decl (&binding, flags);
    }
  else if (is_aggr_type (scope, complain))
    t = lookup_member (scope, name, 2, is_type_p);

  if (!t)
    return error_mark_node;
  return t;
}

/* Subroutine of unqualified_namespace_lookup:
   Add the bindings of NAME in used namespaces to VAL.
   We are currently looking for names in namespace SCOPE, so we
   look through USINGS for using-directives of namespaces
   which have SCOPE as a common ancestor with the current scope.
   Returns false on errors.  */

static bool
lookup_using_namespace (tree name, struct scope_binding *val,
			tree usings, tree scope, int flags)
{
  tree iter;
  timevar_push (TV_NAME_LOOKUP);
  /* Iterate over all used namespaces in current, searching for using
     directives of scope.  */
  for (iter = usings; iter; iter = TREE_CHAIN (iter))
    if (TREE_VALUE (iter) == scope)
      {
	tree used = ORIGINAL_NAMESPACE (TREE_PURPOSE (iter));
	cxx_binding *val1 =
	  cxx_scope_find_binding_for_name (NAMESPACE_LEVEL (used), name);
	/* Resolve ambiguities.  */
	if (val1)
	  ambiguous_decl (name, val, val1, flags);
      }
  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, val->value != error_mark_node);
}

/* [namespace.qual]
   Accepts the NAME to lookup and its qualifying SCOPE.
   Returns the name/type pair found into the cxx_binding *RESULT,
   or false on error.  */

static bool
qualified_lookup_using_namespace (tree name, tree scope,
				  struct scope_binding *result, int flags)
{
  /* Maintain a list of namespaces visited...  */
  tree seen = NULL_TREE;
  /* ... and a list of namespace yet to see.  */
  tree todo = NULL_TREE;
  tree todo_maybe = NULL_TREE;
  tree usings;
  timevar_push (TV_NAME_LOOKUP);
  /* Look through namespace aliases.  */
  scope = ORIGINAL_NAMESPACE (scope);
  while (scope && result->value != error_mark_node)
    {
      cxx_binding *binding =
	cxx_scope_find_binding_for_name (NAMESPACE_LEVEL (scope), name);
      seen = tree_cons (scope, NULL_TREE, seen);
      if (binding)
	ambiguous_decl (name, result, binding, flags);

      /* Consider strong using directives always, and non-strong ones
	 if we haven't found a binding yet.  ??? Shouldn't we consider
	 non-strong ones if the initial RESULT is non-NULL, but the
	 binding in the given namespace is?  */
      for (usings = DECL_NAMESPACE_USING (scope); usings;
	   usings = TREE_CHAIN (usings))
	/* If this was a real directive, and we have not seen it.  */
	if (!TREE_INDIRECT_USING (usings))
	  {
	    /* Try to avoid queuing the same namespace more than once,
	       the exception being when a namespace was already
	       enqueued for todo_maybe and then a strong using is
	       found for it.  We could try to remove it from
	       todo_maybe, but it's probably not worth the effort.  */
	    if (is_associated_namespace (scope, TREE_PURPOSE (usings))
		&& !purpose_member (TREE_PURPOSE (usings), seen)
		&& !purpose_member (TREE_PURPOSE (usings), todo))
	      todo = tree_cons (TREE_PURPOSE (usings), NULL_TREE, todo);
	    else if ((!result->value && !result->type)
		     && !purpose_member (TREE_PURPOSE (usings), seen)
		     && !purpose_member (TREE_PURPOSE (usings), todo)
		     && !purpose_member (TREE_PURPOSE (usings), todo_maybe))
	      todo_maybe = tree_cons (TREE_PURPOSE (usings), NULL_TREE,
				      todo_maybe);
	  }
      if (todo)
	{
	  scope = TREE_PURPOSE (todo);
	  todo = TREE_CHAIN (todo);
	}
      else if (todo_maybe
	       && (!result->value && !result->type))
	{
	  scope = TREE_PURPOSE (todo_maybe);
	  todo = TREE_CHAIN (todo_maybe);
	  todo_maybe = NULL_TREE;
	}
      else
	scope = NULL_TREE; /* If there never was a todo list.  */
    }
  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, result->value != error_mark_node);
}

/* Return the innermost non-namespace binding for NAME from a scope
   containing BINDING, or, if BINDING is NULL, the current scope.  If
   CLASS_P is false, then class bindings are ignored.  */

cxx_binding *
outer_binding (tree name,
	       cxx_binding *binding,
	       bool class_p)
{
  cxx_binding *outer;
  cxx_scope *scope;
  cxx_scope *outer_scope;

  if (binding)
    {
      scope = binding->scope->level_chain;
      outer = binding->previous;
    }
  else
    {
      scope = current_binding_level;
      outer = IDENTIFIER_BINDING (name);
    }
  outer_scope = outer ? outer->scope : NULL;

  /* Because we create class bindings lazily, we might be missing a
     class binding for NAME.  If there are any class binding levels
     between the LAST_BINDING_LEVEL and the scope in which OUTER was
     declared, we must lookup NAME in those class scopes.  */
  if (class_p)
    while (scope && scope != outer_scope && scope->kind != sk_namespace)
      {
	if (scope->kind == sk_class)
	  {
	    cxx_binding *class_binding;

	    class_binding = get_class_binding (name, scope);
	    if (class_binding)
	      {
		/* Thread this new class-scope binding onto the
		   IDENTIFIER_BINDING list so that future lookups
		   find it quickly.  */
		class_binding->previous = outer;
		if (binding)
		  binding->previous = class_binding;
		else
		  IDENTIFIER_BINDING (name) = class_binding;
		return class_binding;
	      }
	  }
	scope = scope->level_chain;
      }

  return outer;
}

/* Return the innermost block-scope or class-scope value binding for
   NAME, or NULL_TREE if there is no such binding.  */

tree
innermost_non_namespace_value (tree name)
{
  cxx_binding *binding;
  binding = outer_binding (name, /*binding=*/NULL, /*class_p=*/true);
  return binding ? binding->value : NULL_TREE;
}

/* Look up NAME in the current binding level and its superiors in the
   namespace of variables, functions and typedefs.  Return a ..._DECL
   node of some kind representing its definition if there is only one
   such declaration, or return a TREE_LIST with all the overloaded
   definitions if there are many, or return 0 if it is undefined.
   Hidden name, either friend declaration or built-in function, are
   not ignored.

   If PREFER_TYPE is > 0, we prefer TYPE_DECLs or namespaces.
   If PREFER_TYPE is > 1, we reject non-type decls (e.g. namespaces).
   Otherwise we prefer non-TYPE_DECLs.

   If NONCLASS is nonzero, bindings in class scopes are ignored.  If
   BLOCK_P is false, bindings in block scopes are ignored.  */

tree
lookup_name_real (tree name, int prefer_type, int nonclass, bool block_p,
		  int namespaces_only, int flags)
{
  cxx_binding *iter;
  tree val = NULL_TREE;

  timevar_push (TV_NAME_LOOKUP);
  /* Conversion operators are handled specially because ordinary
     unqualified name lookup will not find template conversion
     operators.  */
  if (IDENTIFIER_TYPENAME_P (name))
    {
      struct cp_binding_level *level;

      for (level = current_binding_level;
	   level && level->kind != sk_namespace;
	   level = level->level_chain)
	{
	  tree class_type;
	  tree operators;

	  /* A conversion operator can only be declared in a class
	     scope.  */
	  if (level->kind != sk_class)
	    continue;

	  /* Lookup the conversion operator in the class.  */
	  class_type = level->this_entity;
	  operators = lookup_fnfields (class_type, name, /*protect=*/0);
	  if (operators)
	    POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, operators);
	}

      POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, NULL_TREE);
    }

  flags |= lookup_flags (prefer_type, namespaces_only);

  /* First, look in non-namespace scopes.  */

  if (current_class_type == NULL_TREE)
    nonclass = 1;

  if (block_p || !nonclass)
    for (iter = outer_binding (name, NULL, !nonclass);
	 iter;
	 iter = outer_binding (name, iter, !nonclass))
      {
	tree binding;

	/* Skip entities we don't want.  */
	if (LOCAL_BINDING_P (iter) ? !block_p : nonclass)
	  continue;

	/* If this is the kind of thing we're looking for, we're done.  */
	if (qualify_lookup (iter->value, flags))
	  binding = iter->value;
	else if ((flags & LOOKUP_PREFER_TYPES)
		 && qualify_lookup (iter->type, flags))
	  binding = iter->type;
	else
	  binding = NULL_TREE;

	if (binding)
	  {
	    /* Only namespace-scope bindings can be hidden.  */
	    gcc_assert (!hidden_name_p (binding));
	    val = binding;
	    break;
	  }
      }

  /* Now lookup in namespace scopes.  */
  if (!val)
    val = unqualified_namespace_lookup (name, flags);

  /* If we have a single function from a using decl, pull it out.  */
  if (val && TREE_CODE (val) == OVERLOAD && !really_overloaded_fn (val))
    val = OVL_FUNCTION (val);

  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, val);
}

tree
lookup_name_nonclass (tree name)
{
  return lookup_name_real (name, 0, 1, /*block_p=*/true, 0, LOOKUP_COMPLAIN);
}

tree
lookup_function_nonclass (tree name, tree args, bool block_p)
{
  return
    lookup_arg_dependent (name,
			  lookup_name_real (name, 0, 1, block_p, 0,
					    LOOKUP_COMPLAIN),
			  args);
}

tree
lookup_name (tree name)
{
  return lookup_name_real (name, 0, 0, /*block_p=*/true, 0, LOOKUP_COMPLAIN);
}

tree
lookup_name_prefer_type (tree name, int prefer_type)
{
  return lookup_name_real (name, prefer_type, 0, /*block_p=*/true,
			   0, LOOKUP_COMPLAIN);
}

/* Look up NAME for type used in elaborated name specifier in
   the scopes given by SCOPE.  SCOPE can be either TS_CURRENT or
   TS_WITHIN_ENCLOSING_NON_CLASS.  Although not implied by the
   name, more scopes are checked if cleanup or template parameter
   scope is encountered.

   Unlike lookup_name_real, we make sure that NAME is actually
   declared in the desired scope, not from inheritance, nor using
   directive.  For using declaration, there is DR138 still waiting
   to be resolved.  Hidden name coming from an earlier friend
   declaration is also returned.

   A TYPE_DECL best matching the NAME is returned.  Catching error
   and issuing diagnostics are caller's responsibility.  */

tree
lookup_type_scope (tree name, tag_scope scope)
{
  cxx_binding *iter = NULL;
  tree val = NULL_TREE;

  timevar_push (TV_NAME_LOOKUP);

  /* Look in non-namespace scope first.  */
  if (current_binding_level->kind != sk_namespace)
    iter = outer_binding (name, NULL, /*class_p=*/ true);
  for (; iter; iter = outer_binding (name, iter, /*class_p=*/ true))
    {
      /* Check if this is the kind of thing we're looking for.
	 If SCOPE is TS_CURRENT, also make sure it doesn't come from
	 base class.  For ITER->VALUE, we can simply use
	 INHERITED_VALUE_BINDING_P.  For ITER->TYPE, we have to use
	 our own check.

	 We check ITER->TYPE before ITER->VALUE in order to handle
	   typedef struct C {} C;
	 correctly.  */

      if (qualify_lookup (iter->type, LOOKUP_PREFER_TYPES)
	  && (scope != ts_current
	      || LOCAL_BINDING_P (iter)
	      || DECL_CONTEXT (iter->type) == iter->scope->this_entity))
	val = iter->type;
      else if ((scope != ts_current
		|| !INHERITED_VALUE_BINDING_P (iter))
	       && qualify_lookup (iter->value, LOOKUP_PREFER_TYPES))
	val = iter->value;

      if (val)
	break;
    }

  /* Look in namespace scope.  */
  if (!val)
    {
      iter = cxx_scope_find_binding_for_name
	       (NAMESPACE_LEVEL (current_decl_namespace ()), name);

      if (iter)
	{
	  /* If this is the kind of thing we're looking for, we're done.  */
	  if (qualify_lookup (iter->type, LOOKUP_PREFER_TYPES))
	    val = iter->type;
	  else if (qualify_lookup (iter->value, LOOKUP_PREFER_TYPES))
	    val = iter->value;
	}

    }

  /* Type found, check if it is in the allowed scopes, ignoring cleanup
     and template parameter scopes.  */
  if (val)
    {
      struct cp_binding_level *b = current_binding_level;
      while (b)
	{
	  if (iter->scope == b)
	    POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, val);

	  if (b->kind == sk_cleanup || b->kind == sk_template_parms)
	    b = b->level_chain;
	  else if (b->kind == sk_class
		   && scope == ts_within_enclosing_non_class)
	    b = b->level_chain;
	  else
	    break;
	}
    }

  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, NULL_TREE);
}

/* Similar to `lookup_name' but look only in the innermost non-class
   binding level.  */

static tree
lookup_name_innermost_nonclass_level (tree name)
{
  struct cp_binding_level *b;
  tree t = NULL_TREE;

  timevar_push (TV_NAME_LOOKUP);
  b = innermost_nonclass_level ();

  if (b->kind == sk_namespace)
    {
      t = IDENTIFIER_NAMESPACE_VALUE (name);

      /* extern "C" function() */
      if (t != NULL_TREE && TREE_CODE (t) == TREE_LIST)
	t = TREE_VALUE (t);
    }
  else if (IDENTIFIER_BINDING (name)
	   && LOCAL_BINDING_P (IDENTIFIER_BINDING (name)))
    {
      cxx_binding *binding;
      binding = IDENTIFIER_BINDING (name);
      while (1)
	{
	  if (binding->scope == b
	      && !(TREE_CODE (binding->value) == VAR_DECL
		   && DECL_DEAD_FOR_LOCAL (binding->value)))
	    POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, binding->value);

	  if (b->kind == sk_cleanup)
	    b = b->level_chain;
	  else
	    break;
	}
    }

  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, t);
}

/* Like lookup_name_innermost_nonclass_level, but for types.  */

static tree
lookup_type_current_level (tree name)
{
  tree t = NULL_TREE;

  timevar_push (TV_NAME_LOOKUP);
  gcc_assert (current_binding_level->kind != sk_namespace);

  if (REAL_IDENTIFIER_TYPE_VALUE (name) != NULL_TREE
      && REAL_IDENTIFIER_TYPE_VALUE (name) != global_type_node)
    {
      struct cp_binding_level *b = current_binding_level;
      while (1)
	{
	  if (purpose_member (name, b->type_shadowed))
	    POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP,
				    REAL_IDENTIFIER_TYPE_VALUE (name));
	  if (b->kind == sk_cleanup)
	    b = b->level_chain;
	  else
	    break;
	}
    }

  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, t);
}

/* [basic.lookup.koenig] */
/* A nonzero return value in the functions below indicates an error.  */

struct arg_lookup
{
  tree name;
  tree args;
  tree namespaces;
  tree classes;
  tree functions;
};

static bool arg_assoc (struct arg_lookup*, tree);
static bool arg_assoc_args (struct arg_lookup*, tree);
static bool arg_assoc_type (struct arg_lookup*, tree);
static bool add_function (struct arg_lookup *, tree);
static bool arg_assoc_namespace (struct arg_lookup *, tree);
static bool arg_assoc_class (struct arg_lookup *, tree);
static bool arg_assoc_template_arg (struct arg_lookup*, tree);

/* Add a function to the lookup structure.
   Returns true on error.  */

static bool
add_function (struct arg_lookup *k, tree fn)
{
  /* We used to check here to see if the function was already in the list,
     but that's O(n^2), which is just too expensive for function lookup.
     Now we deal with the occasional duplicate in joust.  In doing this, we
     assume that the number of duplicates will be small compared to the
     total number of functions being compared, which should usually be the
     case.  */

  /* We must find only functions, or exactly one non-function.  */
  if (!k->functions)
    k->functions = fn;
  else if (fn == k->functions)
    ;
  else if (is_overloaded_fn (k->functions) && is_overloaded_fn (fn))
    k->functions = build_overload (fn, k->functions);
  else
    {
      tree f1 = OVL_CURRENT (k->functions);
      tree f2 = fn;
      if (is_overloaded_fn (f1))
	{
	  fn = f1; f1 = f2; f2 = fn;
	}
      error ("%q+D is not a function,", f1);
      error ("  conflict with %q+D", f2);
      error ("  in call to %qD", k->name);
      return true;
    }

  return false;
}

/* Returns true iff CURRENT has declared itself to be an associated
   namespace of SCOPE via a strong using-directive (or transitive chain
   thereof).  Both are namespaces.  */

bool
is_associated_namespace (tree current, tree scope)
{
  tree seen = NULL_TREE;
  tree todo = NULL_TREE;
  tree t;
  while (1)
    {
      if (scope == current)
	return true;
      seen = tree_cons (scope, NULL_TREE, seen);
      for (t = DECL_NAMESPACE_ASSOCIATIONS (scope); t; t = TREE_CHAIN (t))
	if (!purpose_member (TREE_PURPOSE (t), seen))
	  todo = tree_cons (TREE_PURPOSE (t), NULL_TREE, todo);
      if (todo)
	{
	  scope = TREE_PURPOSE (todo);
	  todo = TREE_CHAIN (todo);
	}
      else
	return false;
    }
}

/* Return whether FN is a friend of an associated class of ARG.  */

static bool
friend_of_associated_class_p (tree arg, tree fn)
{
  tree type;

  if (TYPE_P (arg))
    type = arg;
  else if (type_unknown_p (arg))
    return false;
  else
    type = TREE_TYPE (arg);

  /* If TYPE is a class, the class itself and all base classes are
     associated classes.  */
  if (CLASS_TYPE_P (type))
    {
      if (is_friend (type, fn))
	return true;

      if (TYPE_BINFO (type))
	{
	  tree binfo, base_binfo;
	  int i;

	  for (binfo = TYPE_BINFO (type), i = 0;
	       BINFO_BASE_ITERATE (binfo, i, base_binfo);
	       i++)
	    if (is_friend (BINFO_TYPE (base_binfo), fn))
	      return true;
	}
    }

  /* If TYPE is a class member, the class of which it is a member is
     an associated class.  */
  if ((CLASS_TYPE_P (type)
       || TREE_CODE (type) == UNION_TYPE
       || TREE_CODE (type) == ENUMERAL_TYPE)
      && TYPE_CONTEXT (type)
      && CLASS_TYPE_P (TYPE_CONTEXT (type))
      && is_friend (TYPE_CONTEXT (type), fn))
    return true;

  return false;
}

/* Add functions of a namespace to the lookup structure.
   Returns true on error.  */

static bool
arg_assoc_namespace (struct arg_lookup *k, tree scope)
{
  tree value;

  if (purpose_member (scope, k->namespaces))
    return 0;
  k->namespaces = tree_cons (scope, NULL_TREE, k->namespaces);

  /* Check out our super-users.  */
  for (value = DECL_NAMESPACE_ASSOCIATIONS (scope); value;
       value = TREE_CHAIN (value))
    if (arg_assoc_namespace (k, TREE_PURPOSE (value)))
      return true;

  value = namespace_binding (k->name, scope);
  if (!value)
    return false;

  for (; value; value = OVL_NEXT (value))
    {
      /* We don't want to find arbitrary hidden functions via argument
	 dependent lookup.  We only want to find friends of associated
	 classes.  */
      if (hidden_name_p (OVL_CURRENT (value)))
	{
	  tree args;

	  for (args = k->args; args; args = TREE_CHAIN (args))
	    if (friend_of_associated_class_p (TREE_VALUE (args),
					      OVL_CURRENT (value)))
	      break;
	  if (!args)
	    continue;
	}

      if (add_function (k, OVL_CURRENT (value)))
	return true;
    }

  return false;
}

/* Adds everything associated with a template argument to the lookup
   structure.  Returns true on error.  */

static bool
arg_assoc_template_arg (struct arg_lookup *k, tree arg)
{
  /* [basic.lookup.koenig]

     If T is a template-id, its associated namespaces and classes are
     ... the namespaces and classes associated with the types of the
     template arguments provided for template type parameters
     (excluding template template parameters); the namespaces in which
     any template template arguments are defined; and the classes in
     which any member templates used as template template arguments
     are defined.  [Note: non-type template arguments do not
     contribute to the set of associated namespaces.  ]  */

  /* Consider first template template arguments.  */
  if (TREE_CODE (arg) == TEMPLATE_TEMPLATE_PARM
      || TREE_CODE (arg) == UNBOUND_CLASS_TEMPLATE)
    return false;
  else if (TREE_CODE (arg) == TEMPLATE_DECL)
    {
      tree ctx = CP_DECL_CONTEXT (arg);

      /* It's not a member template.  */
      if (TREE_CODE (ctx) == NAMESPACE_DECL)
	return arg_assoc_namespace (k, ctx);
      /* Otherwise, it must be member template.  */
      else
	return arg_assoc_class (k, ctx);
    }
  /* It's not a template template argument, but it is a type template
     argument.  */
  else if (TYPE_P (arg))
    return arg_assoc_type (k, arg);
  /* It's a non-type template argument.  */
  else
    return false;
}

/* Adds everything associated with class to the lookup structure.
   Returns true on error.  */

static bool
arg_assoc_class (struct arg_lookup *k, tree type)
{
  tree list, friends, context;
  int i;

  /* Backend build structures, such as __builtin_va_list, aren't
     affected by all this.  */
  if (!CLASS_TYPE_P (type))
    return false;

  if (purpose_member (type, k->classes))
    return false;
  k->classes = tree_cons (type, NULL_TREE, k->classes);

  context = decl_namespace_context (type);
  if (arg_assoc_namespace (k, context))
    return true;

  if (TYPE_BINFO (type))
    {
      /* Process baseclasses.  */
      tree binfo, base_binfo;

      for (binfo = TYPE_BINFO (type), i = 0;
	   BINFO_BASE_ITERATE (binfo, i, base_binfo); i++)
	if (arg_assoc_class (k, BINFO_TYPE (base_binfo)))
	  return true;
    }

  /* Process friends.  */
  for (list = DECL_FRIENDLIST (TYPE_MAIN_DECL (type)); list;
       list = TREE_CHAIN (list))
    if (k->name == FRIEND_NAME (list))
      for (friends = FRIEND_DECLS (list); friends;
	   friends = TREE_CHAIN (friends))
	{
	  tree fn = TREE_VALUE (friends);

	  /* Only interested in global functions with potentially hidden
	     (i.e. unqualified) declarations.  */
	  if (CP_DECL_CONTEXT (fn) != context)
	    continue;
	  /* Template specializations are never found by name lookup.
	     (Templates themselves can be found, but not template
	     specializations.)  */
	  if (TREE_CODE (fn) == FUNCTION_DECL && DECL_USE_TEMPLATE (fn))
	    continue;
	  if (add_function (k, fn))
	    return true;
	}

  /* Process template arguments.  */
  if (CLASSTYPE_TEMPLATE_INFO (type)
      && PRIMARY_TEMPLATE_P (CLASSTYPE_TI_TEMPLATE (type)))
    {
      list = INNERMOST_TEMPLATE_ARGS (CLASSTYPE_TI_ARGS (type));
      for (i = 0; i < TREE_VEC_LENGTH (list); ++i)
	arg_assoc_template_arg (k, TREE_VEC_ELT (list, i));
    }

  return false;
}

/* Adds everything associated with a given type.
   Returns 1 on error.  */

static bool
arg_assoc_type (struct arg_lookup *k, tree type)
{
  /* As we do not get the type of non-type dependent expressions
     right, we can end up with such things without a type.  */
  if (!type)
    return false;

  if (TYPE_PTRMEM_P (type))
    {
      /* Pointer to member: associate class type and value type.  */
      if (arg_assoc_type (k, TYPE_PTRMEM_CLASS_TYPE (type)))
	return true;
      return arg_assoc_type (k, TYPE_PTRMEM_POINTED_TO_TYPE (type));
    }
  else switch (TREE_CODE (type))
    {
    case ERROR_MARK:
      return false;
    case VOID_TYPE:
    case INTEGER_TYPE:
    case REAL_TYPE:
    case COMPLEX_TYPE:
    case VECTOR_TYPE:
    case BOOLEAN_TYPE:
      return false;
    case RECORD_TYPE:
      if (TYPE_PTRMEMFUNC_P (type))
	return arg_assoc_type (k, TYPE_PTRMEMFUNC_FN_TYPE (type));
      return arg_assoc_class (k, type);
    case POINTER_TYPE:
    case REFERENCE_TYPE:
    case ARRAY_TYPE:
      return arg_assoc_type (k, TREE_TYPE (type));
    case UNION_TYPE:
    case ENUMERAL_TYPE:
      return arg_assoc_namespace (k, decl_namespace_context (type));
    case METHOD_TYPE:
      /* The basetype is referenced in the first arg type, so just
	 fall through.  */
    case FUNCTION_TYPE:
      /* Associate the parameter types.  */
      if (arg_assoc_args (k, TYPE_ARG_TYPES (type)))
	return true;
      /* Associate the return type.  */
      return arg_assoc_type (k, TREE_TYPE (type));
    case TEMPLATE_TYPE_PARM:
    case BOUND_TEMPLATE_TEMPLATE_PARM:
      return false;
    case TYPENAME_TYPE:
      return false;
    case LANG_TYPE:
      gcc_assert (type == unknown_type_node);
      return false;
    default:
      gcc_unreachable ();
    }
  return false;
}

/* Adds everything associated with arguments.  Returns true on error.  */

static bool
arg_assoc_args (struct arg_lookup *k, tree args)
{
  for (; args; args = TREE_CHAIN (args))
    if (arg_assoc (k, TREE_VALUE (args)))
      return true;
  return false;
}

/* Adds everything associated with a given tree_node.  Returns 1 on error.  */

static bool
arg_assoc (struct arg_lookup *k, tree n)
{
  if (n == error_mark_node)
    return false;

  if (TYPE_P (n))
    return arg_assoc_type (k, n);

  if (! type_unknown_p (n))
    return arg_assoc_type (k, TREE_TYPE (n));

  if (TREE_CODE (n) == ADDR_EXPR)
    n = TREE_OPERAND (n, 0);
  if (TREE_CODE (n) == COMPONENT_REF)
    n = TREE_OPERAND (n, 1);
  if (TREE_CODE (n) == OFFSET_REF)
    n = TREE_OPERAND (n, 1);
  while (TREE_CODE (n) == TREE_LIST)
    n = TREE_VALUE (n);
  if (TREE_CODE (n) == BASELINK)
    n = BASELINK_FUNCTIONS (n);

  if (TREE_CODE (n) == FUNCTION_DECL)
    return arg_assoc_type (k, TREE_TYPE (n));
  if (TREE_CODE (n) == TEMPLATE_ID_EXPR)
    {
      /* [basic.lookup.koenig]

	 If T is a template-id, its associated namespaces and classes
	 are the namespace in which the template is defined; for
	 member templates, the member template's class...  */
      tree template = TREE_OPERAND (n, 0);
      tree args = TREE_OPERAND (n, 1);
      tree ctx;
      int ix;

      if (TREE_CODE (template) == COMPONENT_REF)
	template = TREE_OPERAND (template, 1);

      /* First, the template.  There may actually be more than one if
	 this is an overloaded function template.  But, in that case,
	 we only need the first; all the functions will be in the same
	 namespace.  */
      template = OVL_CURRENT (template);

      ctx = CP_DECL_CONTEXT (template);

      if (TREE_CODE (ctx) == NAMESPACE_DECL)
	{
	  if (arg_assoc_namespace (k, ctx) == 1)
	    return true;
	}
      /* It must be a member template.  */
      else if (arg_assoc_class (k, ctx) == 1)
	return true;

      /* Now the arguments.  */
      if (args)
	for (ix = TREE_VEC_LENGTH (args); ix--;)
	  if (arg_assoc_template_arg (k, TREE_VEC_ELT (args, ix)) == 1)
	    return true;
    }
  else if (TREE_CODE (n) == OVERLOAD)
    {
      for (; n; n = OVL_CHAIN (n))
	if (arg_assoc_type (k, TREE_TYPE (OVL_FUNCTION (n))))
	  return true;
    }

  return false;
}

/* Performs Koenig lookup depending on arguments, where fns
   are the functions found in normal lookup.  */

tree
lookup_arg_dependent (tree name, tree fns, tree args)
{
  struct arg_lookup k;

  timevar_push (TV_NAME_LOOKUP);

  /* Remove any hidden friend functions from the list of functions
     found so far.  They will be added back by arg_assoc_class as
     appropriate.  */
  fns = remove_hidden_names (fns);

  k.name = name;
  k.args = args;
  k.functions = fns;
  k.classes = NULL_TREE;

  /* We previously performed an optimization here by setting
     NAMESPACES to the current namespace when it was safe. However, DR
     164 says that namespaces that were already searched in the first
     stage of template processing are searched again (potentially
     picking up later definitions) in the second stage. */
  k.namespaces = NULL_TREE;

  arg_assoc_args (&k, args);

  fns = k.functions;
  
  if (fns
      && TREE_CODE (fns) != VAR_DECL
      && !is_overloaded_fn (fns))
    {
      error ("argument dependent lookup finds %q+D", fns);
      error ("  in call to %qD", name);
      fns = error_mark_node;
    }
    
  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, fns);
}

/* Add namespace to using_directives. Return NULL_TREE if nothing was
   changed (i.e. there was already a directive), or the fresh
   TREE_LIST otherwise.  */

static tree
push_using_directive (tree used)
{
  tree ud = current_binding_level->using_directives;
  tree iter, ancestor;

  timevar_push (TV_NAME_LOOKUP);
  /* Check if we already have this.  */
  if (purpose_member (used, ud) != NULL_TREE)
    POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, NULL_TREE);

  ancestor = namespace_ancestor (current_decl_namespace (), used);
  ud = current_binding_level->using_directives;
  ud = tree_cons (used, ancestor, ud);
  current_binding_level->using_directives = ud;

  /* Recursively add all namespaces used.  */
  for (iter = DECL_NAMESPACE_USING (used); iter; iter = TREE_CHAIN (iter))
    push_using_directive (TREE_PURPOSE (iter));

  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, ud);
}

/* The type TYPE is being declared.  If it is a class template, or a
   specialization of a class template, do any processing required and
   perform error-checking.  If IS_FRIEND is nonzero, this TYPE is
   being declared a friend.  B is the binding level at which this TYPE
   should be bound.

   Returns the TYPE_DECL for TYPE, which may have been altered by this
   processing.  */

static tree
maybe_process_template_type_declaration (tree type, int is_friend,
					 cxx_scope *b)
{
  tree decl = TYPE_NAME (type);

  if (processing_template_parmlist)
    /* You can't declare a new template type in a template parameter
       list.  But, you can declare a non-template type:

	 template <class A*> struct S;

       is a forward-declaration of `A'.  */
    ;
  else if (b->kind == sk_namespace
	   && current_binding_level->kind != sk_namespace)
    /* If this new type is being injected into a containing scope,
       then it's not a template type.  */
    ;
  else
    {
      gcc_assert (IS_AGGR_TYPE (type) || TREE_CODE (type) == ENUMERAL_TYPE);

      if (processing_template_decl)
	{
	  /* This may change after the call to
	     push_template_decl_real, but we want the original value.  */
	  tree name = DECL_NAME (decl);

	  decl = push_template_decl_real (decl, is_friend);
	  /* If the current binding level is the binding level for the
	     template parameters (see the comment in
	     begin_template_parm_list) and the enclosing level is a class
	     scope, and we're not looking at a friend, push the
	     declaration of the member class into the class scope.  In the
	     friend case, push_template_decl will already have put the
	     friend into global scope, if appropriate.  */
	  if (TREE_CODE (type) != ENUMERAL_TYPE
	      && !is_friend && b->kind == sk_template_parms
	      && b->level_chain->kind == sk_class)
	    {
	      finish_member_declaration (CLASSTYPE_TI_TEMPLATE (type));

	      if (!COMPLETE_TYPE_P (current_class_type))
		{
		  maybe_add_class_template_decl_list (current_class_type,
						      type, /*friend_p=*/0);
		  /* Put this UTD in the table of UTDs for the class.  */
		  if (CLASSTYPE_NESTED_UTDS (current_class_type) == NULL)
		    CLASSTYPE_NESTED_UTDS (current_class_type) =
		      binding_table_new (SCOPE_DEFAULT_HT_SIZE);

		  binding_table_insert
		    (CLASSTYPE_NESTED_UTDS (current_class_type), name, type);
		}
	    }
	}
    }

  return decl;
}

/* Push a tag name NAME for struct/class/union/enum type TYPE.  In case
   that the NAME is a class template, the tag is processed but not pushed.

   The pushed scope depend on the SCOPE parameter:
   - When SCOPE is TS_CURRENT, put it into the inner-most non-sk_cleanup
     scope.
   - When SCOPE is TS_GLOBAL, put it in the inner-most non-class and
     non-template-parameter scope.  This case is needed for forward
     declarations.
   - When SCOPE is TS_WITHIN_ENCLOSING_NON_CLASS, this is similar to
     TS_GLOBAL case except that names within template-parameter scopes
     are not pushed at all.

   Returns TYPE upon success and ERROR_MARK_NODE otherwise.  */

tree
pushtag (tree name, tree type, tag_scope scope)
{
  struct cp_binding_level *b;
  tree decl;

  timevar_push (TV_NAME_LOOKUP);
  b = current_binding_level;
  while (/* Cleanup scopes are not scopes from the point of view of
	    the language.  */
	 b->kind == sk_cleanup
	 /* Neither are the scopes used to hold template parameters
	    for an explicit specialization.  For an ordinary template
	    declaration, these scopes are not scopes from the point of
	    view of the language.  */
	 || (b->kind == sk_template_parms
	     && (b->explicit_spec_p || scope == ts_global))
	 || (b->kind == sk_class
	     && (scope != ts_current
		 /* We may be defining a new type in the initializer
		    of a static member variable. We allow this when
		    not pedantic, and it is particularly useful for
		    type punning via an anonymous union.  */
		 || COMPLETE_TYPE_P (b->this_entity))))
    b = b->level_chain;

  gcc_assert (TREE_CODE (name) == IDENTIFIER_NODE);

  /* Do C++ gratuitous typedefing.  */
  if (IDENTIFIER_TYPE_VALUE (name) != type)
    {
      tree tdef;
      int in_class = 0;
      tree context = TYPE_CONTEXT (type);

      if (! context)
	{
	  tree cs = current_scope ();

	  if (scope == ts_current)
	    context = cs;
	  else if (cs != NULL_TREE && TYPE_P (cs))
	    /* When declaring a friend class of a local class, we want
	       to inject the newly named class into the scope
	       containing the local class, not the namespace
	       scope.  */
	    context = decl_function_context (get_type_decl (cs));
	}
      if (!context)
	context = current_namespace;

      if (b->kind == sk_class
	  || (b->kind == sk_template_parms
	      && b->level_chain->kind == sk_class))
	in_class = 1;

      if (current_lang_name == lang_name_java)
	TYPE_FOR_JAVA (type) = 1;

      tdef = create_implicit_typedef (name, type);
      DECL_CONTEXT (tdef) = FROB_CONTEXT (context);
      if (scope == ts_within_enclosing_non_class)
	{
	  /* This is a friend.  Make this TYPE_DECL node hidden from
	     ordinary name lookup.  Its corresponding TEMPLATE_DECL
	     will be marked in push_template_decl_real.  */
	  retrofit_lang_decl (tdef);
	  DECL_ANTICIPATED (tdef) = 1;
	  DECL_FRIEND_P (tdef) = 1;
	}

      decl = maybe_process_template_type_declaration
	(type, scope == ts_within_enclosing_non_class, b);
      if (decl == error_mark_node)
	POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, decl);

      if (! in_class)
	set_identifier_type_value_with_scope (name, tdef, b);

      if (b->kind == sk_class)
	{
	  if (!PROCESSING_REAL_TEMPLATE_DECL_P ())
	    /* Put this TYPE_DECL on the TYPE_FIELDS list for the
	       class.  But if it's a member template class, we want
	       the TEMPLATE_DECL, not the TYPE_DECL, so this is done
	       later.  */
	    finish_member_declaration (decl);
	  else
	    pushdecl_class_level (decl);
	}
      else if (b->kind != sk_template_parms)
	{
	  decl = pushdecl_with_scope (decl, b, /*is_friend=*/false);
	  if (decl == error_mark_node)
	    POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, decl);
	}

      TYPE_CONTEXT (type) = DECL_CONTEXT (decl);

      /* If this is a local class, keep track of it.  We need this
	 information for name-mangling, and so that it is possible to
	 find all function definitions in a translation unit in a
	 convenient way.  (It's otherwise tricky to find a member
	 function definition it's only pointed to from within a local
	 class.)  */
      if (TYPE_CONTEXT (type)
	  && TREE_CODE (TYPE_CONTEXT (type)) == FUNCTION_DECL)
	VEC_safe_push (tree, gc, local_classes, type);
    }
  if (b->kind == sk_class
      && !COMPLETE_TYPE_P (current_class_type))
    {
      maybe_add_class_template_decl_list (current_class_type,
					  type, /*friend_p=*/0);

      if (CLASSTYPE_NESTED_UTDS (current_class_type) == NULL)
	CLASSTYPE_NESTED_UTDS (current_class_type)
	  = binding_table_new (SCOPE_DEFAULT_HT_SIZE);

      binding_table_insert
	(CLASSTYPE_NESTED_UTDS (current_class_type), name, type);
    }

  decl = TYPE_NAME (type);
  gcc_assert (TREE_CODE (decl) == TYPE_DECL);
  TYPE_STUB_DECL (type) = decl;

  /* Set type visibility now if this is a forward declaration.  */
  TREE_PUBLIC (decl) = 1;
  determine_visibility (decl);

  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, type);
}

/* Subroutines for reverting temporarily to top-level for instantiation
   of templates and such.  We actually need to clear out the class- and
   local-value slots of all identifiers, so that only the global values
   are at all visible.  Simply setting current_binding_level to the global
   scope isn't enough, because more binding levels may be pushed.  */
struct saved_scope *scope_chain;

/* If ID has not already been marked, add an appropriate binding to
   *OLD_BINDINGS.  */

static void
store_binding (tree id, VEC(cxx_saved_binding,gc) **old_bindings)
{
  cxx_saved_binding *saved;

  if (!id || !IDENTIFIER_BINDING (id))
    return;

  if (IDENTIFIER_MARKED (id))
    return;

  IDENTIFIER_MARKED (id) = 1;

  saved = VEC_safe_push (cxx_saved_binding, gc, *old_bindings, NULL);
  saved->identifier = id;
  saved->binding = IDENTIFIER_BINDING (id);
  saved->real_type_value = REAL_IDENTIFIER_TYPE_VALUE (id);
  IDENTIFIER_BINDING (id) = NULL;
}

static void
store_bindings (tree names, VEC(cxx_saved_binding,gc) **old_bindings)
{
  tree t;

  timevar_push (TV_NAME_LOOKUP);
  for (t = names; t; t = TREE_CHAIN (t))
    {
      tree id;

      if (TREE_CODE (t) == TREE_LIST)
	id = TREE_PURPOSE (t);
      else
	id = DECL_NAME (t);

      store_binding (id, old_bindings);
    }
  timevar_pop (TV_NAME_LOOKUP);
}

/* Like store_bindings, but NAMES is a vector of cp_class_binding
   objects, rather than a TREE_LIST.  */

static void
store_class_bindings (VEC(cp_class_binding,gc) *names,
		      VEC(cxx_saved_binding,gc) **old_bindings)
{
  size_t i;
  cp_class_binding *cb;

  timevar_push (TV_NAME_LOOKUP);
  for (i = 0; VEC_iterate(cp_class_binding, names, i, cb); ++i)
    store_binding (cb->identifier, old_bindings);
  timevar_pop (TV_NAME_LOOKUP);
}

void
push_to_top_level (void)
{
  struct saved_scope *s;
  struct cp_binding_level *b;
  cxx_saved_binding *sb;
  size_t i;
  int need_pop;

  timevar_push (TV_NAME_LOOKUP);
  s = GGC_CNEW (struct saved_scope);

  b = scope_chain ? current_binding_level : 0;

  /* If we're in the middle of some function, save our state.  */
  if (cfun)
    {
      need_pop = 1;
      push_function_context_to (NULL_TREE);
    }
  else
    need_pop = 0;

  if (scope_chain && previous_class_level)
    store_class_bindings (previous_class_level->class_shadowed,
			  &s->old_bindings);

  /* Have to include the global scope, because class-scope decls
     aren't listed anywhere useful.  */
  for (; b; b = b->level_chain)
    {
      tree t;

      /* Template IDs are inserted into the global level. If they were
	 inserted into namespace level, finish_file wouldn't find them
	 when doing pending instantiations. Therefore, don't stop at
	 namespace level, but continue until :: .  */
      if (global_scope_p (b))
	break;

      store_bindings (b->names, &s->old_bindings);
      /* We also need to check class_shadowed to save class-level type
	 bindings, since pushclass doesn't fill in b->names.  */
      if (b->kind == sk_class)
	store_class_bindings (b->class_shadowed, &s->old_bindings);

      /* Unwind type-value slots back to top level.  */
      for (t = b->type_shadowed; t; t = TREE_CHAIN (t))
	SET_IDENTIFIER_TYPE_VALUE (TREE_PURPOSE (t), TREE_VALUE (t));
    }

  for (i = 0; VEC_iterate (cxx_saved_binding, s->old_bindings, i, sb); ++i)
    IDENTIFIER_MARKED (sb->identifier) = 0;

  s->prev = scope_chain;
  s->bindings = b;
  s->need_pop_function_context = need_pop;
  s->function_decl = current_function_decl;
  s->skip_evaluation = skip_evaluation;

  scope_chain = s;
  current_function_decl = NULL_TREE;
  current_lang_base = VEC_alloc (tree, gc, 10);
  current_lang_name = lang_name_cplusplus;
  current_namespace = global_namespace;
  push_class_stack ();
  skip_evaluation = 0;
  timevar_pop (TV_NAME_LOOKUP);
}

void
pop_from_top_level (void)
{
  struct saved_scope *s = scope_chain;
  cxx_saved_binding *saved;
  size_t i;

  timevar_push (TV_NAME_LOOKUP);
  /* Clear out class-level bindings cache.  */
  if (previous_class_level)
    invalidate_class_lookup_cache ();
  pop_class_stack ();

  current_lang_base = 0;

  scope_chain = s->prev;
  for (i = 0; VEC_iterate (cxx_saved_binding, s->old_bindings, i, saved); ++i)
    {
      tree id = saved->identifier;

      IDENTIFIER_BINDING (id) = saved->binding;
      SET_IDENTIFIER_TYPE_VALUE (id, saved->real_type_value);
    }

  /* If we were in the middle of compiling a function, restore our
     state.  */
  if (s->need_pop_function_context)
    pop_function_context_from (NULL_TREE);
  current_function_decl = s->function_decl;
  skip_evaluation = s->skip_evaluation;
  timevar_pop (TV_NAME_LOOKUP);
}

/* Pop off extraneous binding levels left over due to syntax errors.

   We don't pop past namespaces, as they might be valid.  */

void
pop_everything (void)
{
  if (ENABLE_SCOPE_CHECKING)
    verbatim ("XXX entering pop_everything ()\n");
  while (!toplevel_bindings_p ())
    {
      if (current_binding_level->kind == sk_class)
	pop_nested_class ();
      else
	poplevel (0, 0, 0);
    }
  if (ENABLE_SCOPE_CHECKING)
    verbatim ("XXX leaving pop_everything ()\n");
}

/* Emit debugging information for using declarations and directives.
   If input tree is overloaded fn then emit debug info for all
   candidates.  */

void
cp_emit_debug_info_for_using (tree t, tree context)
{
  /* Don't try to emit any debug information if we have errors.  */
  if (sorrycount || errorcount)
    return;

  /* Ignore this FUNCTION_DECL if it refers to a builtin declaration
     of a builtin function.  */
  if (TREE_CODE (t) == FUNCTION_DECL
      && DECL_EXTERNAL (t)
      && DECL_BUILT_IN (t))
    return;

  /* Do not supply context to imported_module_or_decl, if
     it is a global namespace.  */
  if (context == global_namespace)
    context = NULL_TREE;

  if (BASELINK_P (t))
    t = BASELINK_FUNCTIONS (t);

  /* FIXME: Handle TEMPLATE_DECLs.  */
  for (t = OVL_CURRENT (t); t; t = OVL_NEXT (t))
    if (TREE_CODE (t) != TEMPLATE_DECL)
      (*debug_hooks->imported_module_or_decl) (t, context);
}

#include "gt-cp-name-lookup.h"
