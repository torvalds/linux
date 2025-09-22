/* Alias analysis for GNU C
   Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by John Carr (jfc@mit.edu).

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
#include "rtl.h"
#include "tree.h"
#include "tm_p.h"
#include "function.h"
#include "alias.h"
#include "emit-rtl.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "flags.h"
#include "output.h"
#include "toplev.h"
#include "cselib.h"
#include "splay-tree.h"
#include "ggc.h"
#include "langhooks.h"
#include "timevar.h"
#include "target.h"
#include "cgraph.h"
#include "varray.h"
#include "tree-pass.h"
#include "ipa-type-escape.h"

/* The aliasing API provided here solves related but different problems:

   Say there exists (in c)

   struct X {
     struct Y y1;
     struct Z z2;
   } x1, *px1,  *px2;

   struct Y y2, *py;
   struct Z z2, *pz;


   py = &px1.y1;
   px2 = &x1;

   Consider the four questions:

   Can a store to x1 interfere with px2->y1?
   Can a store to x1 interfere with px2->z2?
   (*px2).z2
   Can a store to x1 change the value pointed to by with py?
   Can a store to x1 change the value pointed to by with pz?

   The answer to these questions can be yes, yes, yes, and maybe.

   The first two questions can be answered with a simple examination
   of the type system.  If structure X contains a field of type Y then
   a store thru a pointer to an X can overwrite any field that is
   contained (recursively) in an X (unless we know that px1 != px2).

   The last two of the questions can be solved in the same way as the
   first two questions but this is too conservative.  The observation
   is that in some cases analysis we can know if which (if any) fields
   are addressed and if those addresses are used in bad ways.  This
   analysis may be language specific.  In C, arbitrary operations may
   be applied to pointers.  However, there is some indication that
   this may be too conservative for some C++ types.

   The pass ipa-type-escape does this analysis for the types whose
   instances do not escape across the compilation boundary.

   Historically in GCC, these two problems were combined and a single
   data structure was used to represent the solution to these
   problems.  We now have two similar but different data structures,
   The data structure to solve the last two question is similar to the
   first, but does not contain have the fields in it whose address are
   never taken.  For types that do escape the compilation unit, the
   data structures will have identical information.
*/

/* The alias sets assigned to MEMs assist the back-end in determining
   which MEMs can alias which other MEMs.  In general, two MEMs in
   different alias sets cannot alias each other, with one important
   exception.  Consider something like:

     struct S { int i; double d; };

   a store to an `S' can alias something of either type `int' or type
   `double'.  (However, a store to an `int' cannot alias a `double'
   and vice versa.)  We indicate this via a tree structure that looks
   like:
	   struct S
	    /   \
	   /     \
	 |/_     _\|
	 int    double

   (The arrows are directed and point downwards.)
    In this situation we say the alias set for `struct S' is the
   `superset' and that those for `int' and `double' are `subsets'.

   To see whether two alias sets can point to the same memory, we must
   see if either alias set is a subset of the other. We need not trace
   past immediate descendants, however, since we propagate all
   grandchildren up one level.

   Alias set zero is implicitly a superset of all other alias sets.
   However, this is no actual entry for alias set zero.  It is an
   error to attempt to explicitly construct a subset of zero.  */

struct alias_set_entry GTY(())
{
  /* The alias set number, as stored in MEM_ALIAS_SET.  */
  HOST_WIDE_INT alias_set;

  /* The children of the alias set.  These are not just the immediate
     children, but, in fact, all descendants.  So, if we have:

       struct T { struct S s; float f; }

     continuing our example above, the children here will be all of
     `int', `double', `float', and `struct S'.  */
  splay_tree GTY((param1_is (int), param2_is (int))) children;

  /* Nonzero if would have a child of zero: this effectively makes this
     alias set the same as alias set zero.  */
  int has_zero_child;
};
typedef struct alias_set_entry *alias_set_entry;

static int rtx_equal_for_memref_p (rtx, rtx);
static rtx find_symbolic_term (rtx);
static int memrefs_conflict_p (int, rtx, int, rtx, HOST_WIDE_INT);
static void record_set (rtx, rtx, void *);
static int base_alias_check (rtx, rtx, enum machine_mode,
			     enum machine_mode);
static rtx find_base_value (rtx);
static int mems_in_disjoint_alias_sets_p (rtx, rtx);
static int insert_subset_children (splay_tree_node, void*);
static tree find_base_decl (tree);
static alias_set_entry get_alias_set_entry (HOST_WIDE_INT);
static rtx fixed_scalar_and_varying_struct_p (rtx, rtx, rtx, rtx,
					      int (*) (rtx, int));
static int aliases_everything_p (rtx);
static bool nonoverlapping_component_refs_p (tree, tree);
static tree decl_for_component_ref (tree);
static rtx adjust_offset_for_component_ref (tree, rtx);
static int nonoverlapping_memrefs_p (rtx, rtx);
static int write_dependence_p (rtx, rtx, int);

static void memory_modified_1 (rtx, rtx, void *);
static void record_alias_subset (HOST_WIDE_INT, HOST_WIDE_INT);

/* Set up all info needed to perform alias analysis on memory references.  */

/* Returns the size in bytes of the mode of X.  */
#define SIZE_FOR_MODE(X) (GET_MODE_SIZE (GET_MODE (X)))

/* Returns nonzero if MEM1 and MEM2 do not alias because they are in
   different alias sets.  We ignore alias sets in functions making use
   of variable arguments because the va_arg macros on some systems are
   not legal ANSI C.  */
#define DIFFERENT_ALIAS_SETS_P(MEM1, MEM2)			\
  mems_in_disjoint_alias_sets_p (MEM1, MEM2)

/* Cap the number of passes we make over the insns propagating alias
   information through set chains.   10 is a completely arbitrary choice.  */
#define MAX_ALIAS_LOOP_PASSES 10

/* reg_base_value[N] gives an address to which register N is related.
   If all sets after the first add or subtract to the current value
   or otherwise modify it so it does not point to a different top level
   object, reg_base_value[N] is equal to the address part of the source
   of the first set.

   A base address can be an ADDRESS, SYMBOL_REF, or LABEL_REF.  ADDRESS
   expressions represent certain special values: function arguments and
   the stack, frame, and argument pointers.

   The contents of an ADDRESS is not normally used, the mode of the
   ADDRESS determines whether the ADDRESS is a function argument or some
   other special value.  Pointer equality, not rtx_equal_p, determines whether
   two ADDRESS expressions refer to the same base address.

   The only use of the contents of an ADDRESS is for determining if the
   current function performs nonlocal memory memory references for the
   purposes of marking the function as a constant function.  */

static GTY(()) VEC(rtx,gc) *reg_base_value;
static rtx *new_reg_base_value;

/* We preserve the copy of old array around to avoid amount of garbage
   produced.  About 8% of garbage produced were attributed to this
   array.  */
static GTY((deletable)) VEC(rtx,gc) *old_reg_base_value;

/* Static hunks of RTL used by the aliasing code; these are initialized
   once per function to avoid unnecessary RTL allocations.  */
static GTY (()) rtx static_reg_base_value[FIRST_PSEUDO_REGISTER];

#define REG_BASE_VALUE(X)				\
  (REGNO (X) < VEC_length (rtx, reg_base_value)		\
   ? VEC_index (rtx, reg_base_value, REGNO (X)) : 0)

/* Vector indexed by N giving the initial (unchanging) value known for
   pseudo-register N.  This array is initialized in init_alias_analysis,
   and does not change until end_alias_analysis is called.  */
static GTY((length("reg_known_value_size"))) rtx *reg_known_value;

/* Indicates number of valid entries in reg_known_value.  */
static GTY(()) unsigned int reg_known_value_size;

/* Vector recording for each reg_known_value whether it is due to a
   REG_EQUIV note.  Future passes (viz., reload) may replace the
   pseudo with the equivalent expression and so we account for the
   dependences that would be introduced if that happens.

   The REG_EQUIV notes created in assign_parms may mention the arg
   pointer, and there are explicit insns in the RTL that modify the
   arg pointer.  Thus we must ensure that such insns don't get
   scheduled across each other because that would invalidate the
   REG_EQUIV notes.  One could argue that the REG_EQUIV notes are
   wrong, but solving the problem in the scheduler will likely give
   better code, so we do it here.  */
static bool *reg_known_equiv_p;

/* True when scanning insns from the start of the rtl to the
   NOTE_INSN_FUNCTION_BEG note.  */
static bool copying_arguments;

DEF_VEC_P(alias_set_entry);
DEF_VEC_ALLOC_P(alias_set_entry,gc);

/* The splay-tree used to store the various alias set entries.  */
static GTY (()) VEC(alias_set_entry,gc) *alias_sets;

/* Returns a pointer to the alias set entry for ALIAS_SET, if there is
   such an entry, or NULL otherwise.  */

static inline alias_set_entry
get_alias_set_entry (HOST_WIDE_INT alias_set)
{
  return VEC_index (alias_set_entry, alias_sets, alias_set);
}

/* Returns nonzero if the alias sets for MEM1 and MEM2 are such that
   the two MEMs cannot alias each other.  */

static inline int
mems_in_disjoint_alias_sets_p (rtx mem1, rtx mem2)
{
/* Perform a basic sanity check.  Namely, that there are no alias sets
   if we're not using strict aliasing.  This helps to catch bugs
   whereby someone uses PUT_CODE, but doesn't clear MEM_ALIAS_SET, or
   where a MEM is allocated in some way other than by the use of
   gen_rtx_MEM, and the MEM_ALIAS_SET is not cleared.  If we begin to
   use alias sets to indicate that spilled registers cannot alias each
   other, we might need to remove this check.  */
  gcc_assert (flag_strict_aliasing
	      || (!MEM_ALIAS_SET (mem1) && !MEM_ALIAS_SET (mem2)));

  return ! alias_sets_conflict_p (MEM_ALIAS_SET (mem1), MEM_ALIAS_SET (mem2));
}

/* Insert the NODE into the splay tree given by DATA.  Used by
   record_alias_subset via splay_tree_foreach.  */

static int
insert_subset_children (splay_tree_node node, void *data)
{
  splay_tree_insert ((splay_tree) data, node->key, node->value);

  return 0;
}

/* Return 1 if the two specified alias sets may conflict.  */

int
alias_sets_conflict_p (HOST_WIDE_INT set1, HOST_WIDE_INT set2)
{
  alias_set_entry ase;

  /* If have no alias set information for one of the operands, we have
     to assume it can alias anything.  */
  if (set1 == 0 || set2 == 0
      /* If the two alias sets are the same, they may alias.  */
      || set1 == set2)
    return 1;

  /* See if the first alias set is a subset of the second.  */
  ase = get_alias_set_entry (set1);
  if (ase != 0
      && (ase->has_zero_child
	  || splay_tree_lookup (ase->children,
				(splay_tree_key) set2)))
    return 1;

  /* Now do the same, but with the alias sets reversed.  */
  ase = get_alias_set_entry (set2);
  if (ase != 0
      && (ase->has_zero_child
	  || splay_tree_lookup (ase->children,
				(splay_tree_key) set1)))
    return 1;

  /* The two alias sets are distinct and neither one is the
     child of the other.  Therefore, they cannot alias.  */
  return 0;
}

/* Return 1 if the two specified alias sets might conflict, or if any subtype
   of these alias sets might conflict.  */

int
alias_sets_might_conflict_p (HOST_WIDE_INT set1, HOST_WIDE_INT set2)
{
  if (set1 == 0 || set2 == 0 || set1 == set2)
    return 1;

  return 0;
}


/* Return 1 if any MEM object of type T1 will always conflict (using the
   dependency routines in this file) with any MEM object of type T2.
   This is used when allocating temporary storage.  If T1 and/or T2 are
   NULL_TREE, it means we know nothing about the storage.  */

int
objects_must_conflict_p (tree t1, tree t2)
{
  HOST_WIDE_INT set1, set2;

  /* If neither has a type specified, we don't know if they'll conflict
     because we may be using them to store objects of various types, for
     example the argument and local variables areas of inlined functions.  */
  if (t1 == 0 && t2 == 0)
    return 0;

  /* If they are the same type, they must conflict.  */
  if (t1 == t2
      /* Likewise if both are volatile.  */
      || (t1 != 0 && TYPE_VOLATILE (t1) && t2 != 0 && TYPE_VOLATILE (t2)))
    return 1;

  set1 = t1 ? get_alias_set (t1) : 0;
  set2 = t2 ? get_alias_set (t2) : 0;

  /* Otherwise they conflict if they have no alias set or the same. We
     can't simply use alias_sets_conflict_p here, because we must make
     sure that every subtype of t1 will conflict with every subtype of
     t2 for which a pair of subobjects of these respective subtypes
     overlaps on the stack.  */
  return set1 == 0 || set2 == 0 || set1 == set2;
}

/* T is an expression with pointer type.  Find the DECL on which this
   expression is based.  (For example, in `a[i]' this would be `a'.)
   If there is no such DECL, or a unique decl cannot be determined,
   NULL_TREE is returned.  */

static tree
find_base_decl (tree t)
{
  tree d0, d1;

  if (t == 0 || t == error_mark_node || ! POINTER_TYPE_P (TREE_TYPE (t)))
    return 0;

  /* If this is a declaration, return it.  If T is based on a restrict
     qualified decl, return that decl.  */
  if (DECL_P (t))
    {
      if (TREE_CODE (t) == VAR_DECL && DECL_BASED_ON_RESTRICT_P (t))
	t = DECL_GET_RESTRICT_BASE (t);
      return t;
    }

  /* Handle general expressions.  It would be nice to deal with
     COMPONENT_REFs here.  If we could tell that `a' and `b' were the
     same, then `a->f' and `b->f' are also the same.  */
  switch (TREE_CODE_CLASS (TREE_CODE (t)))
    {
    case tcc_unary:
      return find_base_decl (TREE_OPERAND (t, 0));

    case tcc_binary:
      /* Return 0 if found in neither or both are the same.  */
      d0 = find_base_decl (TREE_OPERAND (t, 0));
      d1 = find_base_decl (TREE_OPERAND (t, 1));
      if (d0 == d1)
	return d0;
      else if (d0 == 0)
	return d1;
      else if (d1 == 0)
	return d0;
      else
	return 0;

    default:
      return 0;
    }
}

/* Return true if all nested component references handled by
   get_inner_reference in T are such that we should use the alias set
   provided by the object at the heart of T.

   This is true for non-addressable components (which don't have their
   own alias set), as well as components of objects in alias set zero.
   This later point is a special case wherein we wish to override the
   alias set used by the component, but we don't have per-FIELD_DECL
   assignable alias sets.  */

bool
component_uses_parent_alias_set (tree t)
{
  while (1)
    {
      /* If we're at the end, it vacuously uses its own alias set.  */
      if (!handled_component_p (t))
	return false;

      switch (TREE_CODE (t))
	{
	case COMPONENT_REF:
	  if (DECL_NONADDRESSABLE_P (TREE_OPERAND (t, 1)))
	    return true;
	  break;

	case ARRAY_REF:
	case ARRAY_RANGE_REF:
	  if (TYPE_NONALIASED_COMPONENT (TREE_TYPE (TREE_OPERAND (t, 0))))
	    return true;
	  break;

	case REALPART_EXPR:
	case IMAGPART_EXPR:
	  break;

	default:
	  /* Bitfields and casts are never addressable.  */
	  return true;
	}

      t = TREE_OPERAND (t, 0);
      if (get_alias_set (TREE_TYPE (t)) == 0)
	return true;
    }
}

/* Return the alias set for T, which may be either a type or an
   expression.  Call language-specific routine for help, if needed.  */

HOST_WIDE_INT
get_alias_set (tree t)
{
  HOST_WIDE_INT set;

  /* If we're not doing any alias analysis, just assume everything
     aliases everything else.  Also return 0 if this or its type is
     an error.  */
  if (! flag_strict_aliasing || t == error_mark_node
      || (! TYPE_P (t)
	  && (TREE_TYPE (t) == 0 || TREE_TYPE (t) == error_mark_node)))
    return 0;

  /* We can be passed either an expression or a type.  This and the
     language-specific routine may make mutually-recursive calls to each other
     to figure out what to do.  At each juncture, we see if this is a tree
     that the language may need to handle specially.  First handle things that
     aren't types.  */
  if (! TYPE_P (t))
    {
      tree inner = t;

      /* Remove any nops, then give the language a chance to do
	 something with this tree before we look at it.  */
      STRIP_NOPS (t);
      set = lang_hooks.get_alias_set (t);
      if (set != -1)
	return set;

      /* First see if the actual object referenced is an INDIRECT_REF from a
	 restrict-qualified pointer or a "void *".  */
      while (handled_component_p (inner))
	{
	  inner = TREE_OPERAND (inner, 0);
	  STRIP_NOPS (inner);
	}

      /* Check for accesses through restrict-qualified pointers.  */
      if (INDIRECT_REF_P (inner))
	{
	  tree decl = find_base_decl (TREE_OPERAND (inner, 0));

	  if (decl && DECL_POINTER_ALIAS_SET_KNOWN_P (decl))
	    {
	      /* If we haven't computed the actual alias set, do it now.  */
	      if (DECL_POINTER_ALIAS_SET (decl) == -2)
		{
		  tree pointed_to_type = TREE_TYPE (TREE_TYPE (decl));

		  /* No two restricted pointers can point at the same thing.
		     However, a restricted pointer can point at the same thing
		     as an unrestricted pointer, if that unrestricted pointer
		     is based on the restricted pointer.  So, we make the
		     alias set for the restricted pointer a subset of the
		     alias set for the type pointed to by the type of the
		     decl.  */
		  HOST_WIDE_INT pointed_to_alias_set
		    = get_alias_set (pointed_to_type);

		  if (pointed_to_alias_set == 0)
		    /* It's not legal to make a subset of alias set zero.  */
		    DECL_POINTER_ALIAS_SET (decl) = 0;
		  else if (AGGREGATE_TYPE_P (pointed_to_type))
		    /* For an aggregate, we must treat the restricted
		       pointer the same as an ordinary pointer.  If we
		       were to make the type pointed to by the
		       restricted pointer a subset of the pointed-to
		       type, then we would believe that other subsets
		       of the pointed-to type (such as fields of that
		       type) do not conflict with the type pointed to
		       by the restricted pointer.  */
		    DECL_POINTER_ALIAS_SET (decl)
		      = pointed_to_alias_set;
		  else
		    {
		      DECL_POINTER_ALIAS_SET (decl) = new_alias_set ();
		      record_alias_subset (pointed_to_alias_set,
					   DECL_POINTER_ALIAS_SET (decl));
		    }
		}

	      /* We use the alias set indicated in the declaration.  */
	      return DECL_POINTER_ALIAS_SET (decl);
	    }

	  /* If we have an INDIRECT_REF via a void pointer, we don't
	     know anything about what that might alias.  Likewise if the
	     pointer is marked that way.  */
	  else if (TREE_CODE (TREE_TYPE (inner)) == VOID_TYPE
		   || (TYPE_REF_CAN_ALIAS_ALL
		       (TREE_TYPE (TREE_OPERAND (inner, 0)))))
	    return 0;
	}

      /* Otherwise, pick up the outermost object that we could have a pointer
	 to, processing conversions as above.  */
      while (component_uses_parent_alias_set (t))
	{
	  t = TREE_OPERAND (t, 0);
	  STRIP_NOPS (t);
	}

      /* If we've already determined the alias set for a decl, just return
	 it.  This is necessary for C++ anonymous unions, whose component
	 variables don't look like union members (boo!).  */
      if (TREE_CODE (t) == VAR_DECL
	  && DECL_RTL_SET_P (t) && MEM_P (DECL_RTL (t)))
	return MEM_ALIAS_SET (DECL_RTL (t));

      /* Now all we care about is the type.  */
      t = TREE_TYPE (t);
    }

  /* Variant qualifiers don't affect the alias set, so get the main
     variant. If this is a type with a known alias set, return it.  */
  t = TYPE_MAIN_VARIANT (t);
  if (TYPE_ALIAS_SET_KNOWN_P (t))
    return TYPE_ALIAS_SET (t);

  /* See if the language has special handling for this type.  */
  set = lang_hooks.get_alias_set (t);
  if (set != -1)
    return set;

  /* There are no objects of FUNCTION_TYPE, so there's no point in
     using up an alias set for them.  (There are, of course, pointers
     and references to functions, but that's different.)  */
  else if (TREE_CODE (t) == FUNCTION_TYPE)
    set = 0;

  /* Unless the language specifies otherwise, let vector types alias
     their components.  This avoids some nasty type punning issues in
     normal usage.  And indeed lets vectors be treated more like an
     array slice.  */
  else if (TREE_CODE (t) == VECTOR_TYPE)
    set = get_alias_set (TREE_TYPE (t));

  else
    /* Otherwise make a new alias set for this type.  */
    set = new_alias_set ();

  TYPE_ALIAS_SET (t) = set;

  /* If this is an aggregate type, we must record any component aliasing
     information.  */
  if (AGGREGATE_TYPE_P (t) || TREE_CODE (t) == COMPLEX_TYPE)
    record_component_aliases (t);

  return set;
}

/* Return a brand-new alias set.  */

HOST_WIDE_INT
new_alias_set (void)
{
  if (flag_strict_aliasing)
    {
      if (alias_sets == 0)
	VEC_safe_push (alias_set_entry, gc, alias_sets, 0);
      VEC_safe_push (alias_set_entry, gc, alias_sets, 0);
      return VEC_length (alias_set_entry, alias_sets) - 1;
    }
  else
    return 0;
}

/* Indicate that things in SUBSET can alias things in SUPERSET, but that
   not everything that aliases SUPERSET also aliases SUBSET.  For example,
   in C, a store to an `int' can alias a load of a structure containing an
   `int', and vice versa.  But it can't alias a load of a 'double' member
   of the same structure.  Here, the structure would be the SUPERSET and
   `int' the SUBSET.  This relationship is also described in the comment at
   the beginning of this file.

   This function should be called only once per SUPERSET/SUBSET pair.

   It is illegal for SUPERSET to be zero; everything is implicitly a
   subset of alias set zero.  */

static void
record_alias_subset (HOST_WIDE_INT superset, HOST_WIDE_INT subset)
{
  alias_set_entry superset_entry;
  alias_set_entry subset_entry;

  /* It is possible in complex type situations for both sets to be the same,
     in which case we can ignore this operation.  */
  if (superset == subset)
    return;

  gcc_assert (superset);

  superset_entry = get_alias_set_entry (superset);
  if (superset_entry == 0)
    {
      /* Create an entry for the SUPERSET, so that we have a place to
	 attach the SUBSET.  */
      superset_entry = ggc_alloc (sizeof (struct alias_set_entry));
      superset_entry->alias_set = superset;
      superset_entry->children
	= splay_tree_new_ggc (splay_tree_compare_ints);
      superset_entry->has_zero_child = 0;
      VEC_replace (alias_set_entry, alias_sets, superset, superset_entry);
    }

  if (subset == 0)
    superset_entry->has_zero_child = 1;
  else
    {
      subset_entry = get_alias_set_entry (subset);
      /* If there is an entry for the subset, enter all of its children
	 (if they are not already present) as children of the SUPERSET.  */
      if (subset_entry)
	{
	  if (subset_entry->has_zero_child)
	    superset_entry->has_zero_child = 1;

	  splay_tree_foreach (subset_entry->children, insert_subset_children,
			      superset_entry->children);
	}

      /* Enter the SUBSET itself as a child of the SUPERSET.  */
      splay_tree_insert (superset_entry->children,
			 (splay_tree_key) subset, 0);
    }
}

/* Record that component types of TYPE, if any, are part of that type for
   aliasing purposes.  For record types, we only record component types
   for fields that are marked addressable.  For array types, we always
   record the component types, so the front end should not call this
   function if the individual component aren't addressable.  */

void
record_component_aliases (tree type)
{
  HOST_WIDE_INT superset = get_alias_set (type);
  tree field;

  if (superset == 0)
    return;

  switch (TREE_CODE (type))
    {
    case ARRAY_TYPE:
      if (! TYPE_NONALIASED_COMPONENT (type))
	record_alias_subset (superset, get_alias_set (TREE_TYPE (type)));
      break;

    case RECORD_TYPE:
    case UNION_TYPE:
    case QUAL_UNION_TYPE:
      /* Recursively record aliases for the base classes, if there are any.  */
      if (TYPE_BINFO (type))
	{
	  int i;
	  tree binfo, base_binfo;

	  for (binfo = TYPE_BINFO (type), i = 0;
	       BINFO_BASE_ITERATE (binfo, i, base_binfo); i++)
	    record_alias_subset (superset,
				 get_alias_set (BINFO_TYPE (base_binfo)));
	}
      for (field = TYPE_FIELDS (type); field != 0; field = TREE_CHAIN (field))
	if (TREE_CODE (field) == FIELD_DECL && ! DECL_NONADDRESSABLE_P (field))
	  record_alias_subset (superset, get_alias_set (TREE_TYPE (field)));
      break;

    case COMPLEX_TYPE:
      record_alias_subset (superset, get_alias_set (TREE_TYPE (type)));
      break;

    default:
      break;
    }
}

/* Allocate an alias set for use in storing and reading from the varargs
   spill area.  */

static GTY(()) HOST_WIDE_INT varargs_set = -1;

HOST_WIDE_INT
get_varargs_alias_set (void)
{
#if 1
  /* We now lower VA_ARG_EXPR, and there's currently no way to attach the
     varargs alias set to an INDIRECT_REF (FIXME!), so we can't
     consistently use the varargs alias set for loads from the varargs
     area.  So don't use it anywhere.  */
  return 0;
#else
  if (varargs_set == -1)
    varargs_set = new_alias_set ();

  return varargs_set;
#endif
}

/* Likewise, but used for the fixed portions of the frame, e.g., register
   save areas.  */

static GTY(()) HOST_WIDE_INT frame_set = -1;

HOST_WIDE_INT
get_frame_alias_set (void)
{
  if (frame_set == -1)
    frame_set = new_alias_set ();

  return frame_set;
}

/* Inside SRC, the source of a SET, find a base address.  */

static rtx
find_base_value (rtx src)
{
  unsigned int regno;

  switch (GET_CODE (src))
    {
    case SYMBOL_REF:
    case LABEL_REF:
      return src;

    case REG:
      regno = REGNO (src);
      /* At the start of a function, argument registers have known base
	 values which may be lost later.  Returning an ADDRESS
	 expression here allows optimization based on argument values
	 even when the argument registers are used for other purposes.  */
      if (regno < FIRST_PSEUDO_REGISTER && copying_arguments)
	return new_reg_base_value[regno];

      /* If a pseudo has a known base value, return it.  Do not do this
	 for non-fixed hard regs since it can result in a circular
	 dependency chain for registers which have values at function entry.

	 The test above is not sufficient because the scheduler may move
	 a copy out of an arg reg past the NOTE_INSN_FUNCTION_BEGIN.  */
      if ((regno >= FIRST_PSEUDO_REGISTER || fixed_regs[regno])
	  && regno < VEC_length (rtx, reg_base_value))
	{
	  /* If we're inside init_alias_analysis, use new_reg_base_value
	     to reduce the number of relaxation iterations.  */
	  if (new_reg_base_value && new_reg_base_value[regno]
	      && REG_N_SETS (regno) == 1)
	    return new_reg_base_value[regno];

	  if (VEC_index (rtx, reg_base_value, regno))
	    return VEC_index (rtx, reg_base_value, regno);
	}

      return 0;

    case MEM:
      /* Check for an argument passed in memory.  Only record in the
	 copying-arguments block; it is too hard to track changes
	 otherwise.  */
      if (copying_arguments
	  && (XEXP (src, 0) == arg_pointer_rtx
	      || (GET_CODE (XEXP (src, 0)) == PLUS
		  && XEXP (XEXP (src, 0), 0) == arg_pointer_rtx)))
	return gen_rtx_ADDRESS (VOIDmode, src);
      return 0;

    case CONST:
      src = XEXP (src, 0);
      if (GET_CODE (src) != PLUS && GET_CODE (src) != MINUS)
	break;

      /* ... fall through ...  */

    case PLUS:
    case MINUS:
      {
	rtx temp, src_0 = XEXP (src, 0), src_1 = XEXP (src, 1);

	/* If either operand is a REG that is a known pointer, then it
	   is the base.  */
	if (REG_P (src_0) && REG_POINTER (src_0))
	  return find_base_value (src_0);
	if (REG_P (src_1) && REG_POINTER (src_1))
	  return find_base_value (src_1);

	/* If either operand is a REG, then see if we already have
	   a known value for it.  */
	if (REG_P (src_0))
	  {
	    temp = find_base_value (src_0);
	    if (temp != 0)
	      src_0 = temp;
	  }

	if (REG_P (src_1))
	  {
	    temp = find_base_value (src_1);
	    if (temp!= 0)
	      src_1 = temp;
	  }

	/* If either base is named object or a special address
	   (like an argument or stack reference), then use it for the
	   base term.  */
	if (src_0 != 0
	    && (GET_CODE (src_0) == SYMBOL_REF
		|| GET_CODE (src_0) == LABEL_REF
		|| (GET_CODE (src_0) == ADDRESS
		    && GET_MODE (src_0) != VOIDmode)))
	  return src_0;

	if (src_1 != 0
	    && (GET_CODE (src_1) == SYMBOL_REF
		|| GET_CODE (src_1) == LABEL_REF
		|| (GET_CODE (src_1) == ADDRESS
		    && GET_MODE (src_1) != VOIDmode)))
	  return src_1;

	/* Guess which operand is the base address:
	   If either operand is a symbol, then it is the base.  If
	   either operand is a CONST_INT, then the other is the base.  */
	if (GET_CODE (src_1) == CONST_INT || CONSTANT_P (src_0))
	  return find_base_value (src_0);
	else if (GET_CODE (src_0) == CONST_INT || CONSTANT_P (src_1))
	  return find_base_value (src_1);

	return 0;
      }

    case LO_SUM:
      /* The standard form is (lo_sum reg sym) so look only at the
	 second operand.  */
      return find_base_value (XEXP (src, 1));

    case AND:
      /* If the second operand is constant set the base
	 address to the first operand.  */
      if (GET_CODE (XEXP (src, 1)) == CONST_INT && INTVAL (XEXP (src, 1)) != 0)
	return find_base_value (XEXP (src, 0));
      return 0;

    case TRUNCATE:
      if (GET_MODE_SIZE (GET_MODE (src)) < GET_MODE_SIZE (Pmode))
	break;
      /* Fall through.  */
    case HIGH:
    case PRE_INC:
    case PRE_DEC:
    case POST_INC:
    case POST_DEC:
    case PRE_MODIFY:
    case POST_MODIFY:
      return find_base_value (XEXP (src, 0));

    case ZERO_EXTEND:
    case SIGN_EXTEND:	/* used for NT/Alpha pointers */
      {
	rtx temp = find_base_value (XEXP (src, 0));

	if (temp != 0 && CONSTANT_P (temp))
	  temp = convert_memory_address (Pmode, temp);

	return temp;
      }

    default:
      break;
    }

  return 0;
}

/* Called from init_alias_analysis indirectly through note_stores.  */

/* While scanning insns to find base values, reg_seen[N] is nonzero if
   register N has been set in this function.  */
static char *reg_seen;

/* Addresses which are known not to alias anything else are identified
   by a unique integer.  */
static int unique_id;

static void
record_set (rtx dest, rtx set, void *data ATTRIBUTE_UNUSED)
{
  unsigned regno;
  rtx src;
  int n;

  if (!REG_P (dest))
    return;

  regno = REGNO (dest);

  gcc_assert (regno < VEC_length (rtx, reg_base_value));

  /* If this spans multiple hard registers, then we must indicate that every
     register has an unusable value.  */
  if (regno < FIRST_PSEUDO_REGISTER)
    n = hard_regno_nregs[regno][GET_MODE (dest)];
  else
    n = 1;
  if (n != 1)
    {
      while (--n >= 0)
	{
	  reg_seen[regno + n] = 1;
	  new_reg_base_value[regno + n] = 0;
	}
      return;
    }

  if (set)
    {
      /* A CLOBBER wipes out any old value but does not prevent a previously
	 unset register from acquiring a base address (i.e. reg_seen is not
	 set).  */
      if (GET_CODE (set) == CLOBBER)
	{
	  new_reg_base_value[regno] = 0;
	  return;
	}
      src = SET_SRC (set);
    }
  else
    {
      if (reg_seen[regno])
	{
	  new_reg_base_value[regno] = 0;
	  return;
	}
      reg_seen[regno] = 1;
      new_reg_base_value[regno] = gen_rtx_ADDRESS (Pmode,
						   GEN_INT (unique_id++));
      return;
    }

  /* If this is not the first set of REGNO, see whether the new value
     is related to the old one.  There are two cases of interest:

	(1) The register might be assigned an entirely new value
	    that has the same base term as the original set.

	(2) The set might be a simple self-modification that
	    cannot change REGNO's base value.

     If neither case holds, reject the original base value as invalid.
     Note that the following situation is not detected:

	 extern int x, y;  int *p = &x; p += (&y-&x);

     ANSI C does not allow computing the difference of addresses
     of distinct top level objects.  */
  if (new_reg_base_value[regno] != 0
      && find_base_value (src) != new_reg_base_value[regno])
    switch (GET_CODE (src))
      {
      case LO_SUM:
      case MINUS:
	if (XEXP (src, 0) != dest && XEXP (src, 1) != dest)
	  new_reg_base_value[regno] = 0;
	break;
      case PLUS:
	/* If the value we add in the PLUS is also a valid base value,
	   this might be the actual base value, and the original value
	   an index.  */
	{
	  rtx other = NULL_RTX;

	  if (XEXP (src, 0) == dest)
	    other = XEXP (src, 1);
	  else if (XEXP (src, 1) == dest)
	    other = XEXP (src, 0);

	  if (! other || find_base_value (other))
	    new_reg_base_value[regno] = 0;
	  break;
	}
      case AND:
	if (XEXP (src, 0) != dest || GET_CODE (XEXP (src, 1)) != CONST_INT)
	  new_reg_base_value[regno] = 0;
	break;
      default:
	new_reg_base_value[regno] = 0;
	break;
      }
  /* If this is the first set of a register, record the value.  */
  else if ((regno >= FIRST_PSEUDO_REGISTER || ! fixed_regs[regno])
	   && ! reg_seen[regno] && new_reg_base_value[regno] == 0)
    new_reg_base_value[regno] = find_base_value (src);

  reg_seen[regno] = 1;
}

/* Clear alias info for a register.  This is used if an RTL transformation
   changes the value of a register.  This is used in flow by AUTO_INC_DEC
   optimizations.  We don't need to clear reg_base_value, since flow only
   changes the offset.  */

void
clear_reg_alias_info (rtx reg)
{
  unsigned int regno = REGNO (reg);

  if (regno >= FIRST_PSEUDO_REGISTER)
    {
      regno -= FIRST_PSEUDO_REGISTER;
      if (regno < reg_known_value_size)
	{
	  reg_known_value[regno] = reg;
	  reg_known_equiv_p[regno] = false;
	}
    }
}

/* If a value is known for REGNO, return it.  */

rtx
get_reg_known_value (unsigned int regno)
{
  if (regno >= FIRST_PSEUDO_REGISTER)
    {
      regno -= FIRST_PSEUDO_REGISTER;
      if (regno < reg_known_value_size)
	return reg_known_value[regno];
    }
  return NULL;
}

/* Set it.  */

static void
set_reg_known_value (unsigned int regno, rtx val)
{
  if (regno >= FIRST_PSEUDO_REGISTER)
    {
      regno -= FIRST_PSEUDO_REGISTER;
      if (regno < reg_known_value_size)
	reg_known_value[regno] = val;
    }
}

/* Similarly for reg_known_equiv_p.  */

bool
get_reg_known_equiv_p (unsigned int regno)
{
  if (regno >= FIRST_PSEUDO_REGISTER)
    {
      regno -= FIRST_PSEUDO_REGISTER;
      if (regno < reg_known_value_size)
	return reg_known_equiv_p[regno];
    }
  return false;
}

static void
set_reg_known_equiv_p (unsigned int regno, bool val)
{
  if (regno >= FIRST_PSEUDO_REGISTER)
    {
      regno -= FIRST_PSEUDO_REGISTER;
      if (regno < reg_known_value_size)
	reg_known_equiv_p[regno] = val;
    }
}


/* Returns a canonical version of X, from the point of view alias
   analysis.  (For example, if X is a MEM whose address is a register,
   and the register has a known value (say a SYMBOL_REF), then a MEM
   whose address is the SYMBOL_REF is returned.)  */

rtx
canon_rtx (rtx x)
{
  /* Recursively look for equivalences.  */
  if (REG_P (x) && REGNO (x) >= FIRST_PSEUDO_REGISTER)
    {
      rtx t = get_reg_known_value (REGNO (x));
      if (t == x)
	return x;
      if (t)
	return canon_rtx (t);
    }

  if (GET_CODE (x) == PLUS)
    {
      rtx x0 = canon_rtx (XEXP (x, 0));
      rtx x1 = canon_rtx (XEXP (x, 1));

      if (x0 != XEXP (x, 0) || x1 != XEXP (x, 1))
	{
	  if (GET_CODE (x0) == CONST_INT)
	    return plus_constant (x1, INTVAL (x0));
	  else if (GET_CODE (x1) == CONST_INT)
	    return plus_constant (x0, INTVAL (x1));
	  return gen_rtx_PLUS (GET_MODE (x), x0, x1);
	}
    }

  /* This gives us much better alias analysis when called from
     the loop optimizer.   Note we want to leave the original
     MEM alone, but need to return the canonicalized MEM with
     all the flags with their original values.  */
  else if (MEM_P (x))
    x = replace_equiv_address_nv (x, canon_rtx (XEXP (x, 0)));

  return x;
}

/* Return 1 if X and Y are identical-looking rtx's.
   Expect that X and Y has been already canonicalized.

   We use the data in reg_known_value above to see if two registers with
   different numbers are, in fact, equivalent.  */

static int
rtx_equal_for_memref_p (rtx x, rtx y)
{
  int i;
  int j;
  enum rtx_code code;
  const char *fmt;

  if (x == 0 && y == 0)
    return 1;
  if (x == 0 || y == 0)
    return 0;

  if (x == y)
    return 1;

  code = GET_CODE (x);
  /* Rtx's of different codes cannot be equal.  */
  if (code != GET_CODE (y))
    return 0;

  /* (MULT:SI x y) and (MULT:HI x y) are NOT equivalent.
     (REG:SI x) and (REG:HI x) are NOT equivalent.  */

  if (GET_MODE (x) != GET_MODE (y))
    return 0;

  /* Some RTL can be compared without a recursive examination.  */
  switch (code)
    {
    case REG:
      return REGNO (x) == REGNO (y);

    case LABEL_REF:
      return XEXP (x, 0) == XEXP (y, 0);

    case SYMBOL_REF:
      return XSTR (x, 0) == XSTR (y, 0);

    case VALUE:
    case CONST_INT:
    case CONST_DOUBLE:
      /* There's no need to compare the contents of CONST_DOUBLEs or
	 CONST_INTs because pointer equality is a good enough
	 comparison for these nodes.  */
      return 0;

    default:
      break;
    }

  /* canon_rtx knows how to handle plus.  No need to canonicalize.  */
  if (code == PLUS)
    return ((rtx_equal_for_memref_p (XEXP (x, 0), XEXP (y, 0))
	     && rtx_equal_for_memref_p (XEXP (x, 1), XEXP (y, 1)))
	    || (rtx_equal_for_memref_p (XEXP (x, 0), XEXP (y, 1))
		&& rtx_equal_for_memref_p (XEXP (x, 1), XEXP (y, 0))));
  /* For commutative operations, the RTX match if the operand match in any
     order.  Also handle the simple binary and unary cases without a loop.  */
  if (COMMUTATIVE_P (x))
    {
      rtx xop0 = canon_rtx (XEXP (x, 0));
      rtx yop0 = canon_rtx (XEXP (y, 0));
      rtx yop1 = canon_rtx (XEXP (y, 1));

      return ((rtx_equal_for_memref_p (xop0, yop0)
	       && rtx_equal_for_memref_p (canon_rtx (XEXP (x, 1)), yop1))
	      || (rtx_equal_for_memref_p (xop0, yop1)
		  && rtx_equal_for_memref_p (canon_rtx (XEXP (x, 1)), yop0)));
    }
  else if (NON_COMMUTATIVE_P (x))
    {
      return (rtx_equal_for_memref_p (canon_rtx (XEXP (x, 0)),
				      canon_rtx (XEXP (y, 0)))
	      && rtx_equal_for_memref_p (canon_rtx (XEXP (x, 1)),
					 canon_rtx (XEXP (y, 1))));
    }
  else if (UNARY_P (x))
    return rtx_equal_for_memref_p (canon_rtx (XEXP (x, 0)),
				   canon_rtx (XEXP (y, 0)));

  /* Compare the elements.  If any pair of corresponding elements
     fail to match, return 0 for the whole things.

     Limit cases to types which actually appear in addresses.  */

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      switch (fmt[i])
	{
	case 'i':
	  if (XINT (x, i) != XINT (y, i))
	    return 0;
	  break;

	case 'E':
	  /* Two vectors must have the same length.  */
	  if (XVECLEN (x, i) != XVECLEN (y, i))
	    return 0;

	  /* And the corresponding elements must match.  */
	  for (j = 0; j < XVECLEN (x, i); j++)
	    if (rtx_equal_for_memref_p (canon_rtx (XVECEXP (x, i, j)),
					canon_rtx (XVECEXP (y, i, j))) == 0)
	      return 0;
	  break;

	case 'e':
	  if (rtx_equal_for_memref_p (canon_rtx (XEXP (x, i)),
				      canon_rtx (XEXP (y, i))) == 0)
	    return 0;
	  break;

	  /* This can happen for asm operands.  */
	case 's':
	  if (strcmp (XSTR (x, i), XSTR (y, i)))
	    return 0;
	  break;

	/* This can happen for an asm which clobbers memory.  */
	case '0':
	  break;

	  /* It is believed that rtx's at this level will never
	     contain anything but integers and other rtx's,
	     except for within LABEL_REFs and SYMBOL_REFs.  */
	default:
	  gcc_unreachable ();
	}
    }
  return 1;
}

/* Given an rtx X, find a SYMBOL_REF or LABEL_REF within
   X and return it, or return 0 if none found.  */

static rtx
find_symbolic_term (rtx x)
{
  int i;
  enum rtx_code code;
  const char *fmt;

  code = GET_CODE (x);
  if (code == SYMBOL_REF || code == LABEL_REF)
    return x;
  if (OBJECT_P (x))
    return 0;

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      rtx t;

      if (fmt[i] == 'e')
	{
	  t = find_symbolic_term (XEXP (x, i));
	  if (t != 0)
	    return t;
	}
      else if (fmt[i] == 'E')
	break;
    }
  return 0;
}

rtx
find_base_term (rtx x)
{
  cselib_val *val;
  struct elt_loc_list *l;

#if defined (FIND_BASE_TERM)
  /* Try machine-dependent ways to find the base term.  */
  x = FIND_BASE_TERM (x);
#endif

  switch (GET_CODE (x))
    {
    case REG:
      return REG_BASE_VALUE (x);

    case TRUNCATE:
      if (GET_MODE_SIZE (GET_MODE (x)) < GET_MODE_SIZE (Pmode))
	return 0;
      /* Fall through.  */
    case HIGH:
    case PRE_INC:
    case PRE_DEC:
    case POST_INC:
    case POST_DEC:
    case PRE_MODIFY:
    case POST_MODIFY:
      return find_base_term (XEXP (x, 0));

    case ZERO_EXTEND:
    case SIGN_EXTEND:	/* Used for Alpha/NT pointers */
      {
	rtx temp = find_base_term (XEXP (x, 0));

	if (temp != 0 && CONSTANT_P (temp))
	  temp = convert_memory_address (Pmode, temp);

	return temp;
      }

    case VALUE:
      val = CSELIB_VAL_PTR (x);
      if (!val)
	return 0;
      for (l = val->locs; l; l = l->next)
	if ((x = find_base_term (l->loc)) != 0)
	  return x;
      return 0;

    case CONST:
      x = XEXP (x, 0);
      if (GET_CODE (x) != PLUS && GET_CODE (x) != MINUS)
	return 0;
      /* Fall through.  */
    case LO_SUM:
    case PLUS:
    case MINUS:
      {
	rtx tmp1 = XEXP (x, 0);
	rtx tmp2 = XEXP (x, 1);

	/* This is a little bit tricky since we have to determine which of
	   the two operands represents the real base address.  Otherwise this
	   routine may return the index register instead of the base register.

	   That may cause us to believe no aliasing was possible, when in
	   fact aliasing is possible.

	   We use a few simple tests to guess the base register.  Additional
	   tests can certainly be added.  For example, if one of the operands
	   is a shift or multiply, then it must be the index register and the
	   other operand is the base register.  */

	if (tmp1 == pic_offset_table_rtx && CONSTANT_P (tmp2))
	  return find_base_term (tmp2);

	/* If either operand is known to be a pointer, then use it
	   to determine the base term.  */
	if (REG_P (tmp1) && REG_POINTER (tmp1))
	  return find_base_term (tmp1);

	if (REG_P (tmp2) && REG_POINTER (tmp2))
	  return find_base_term (tmp2);

	/* Neither operand was known to be a pointer.  Go ahead and find the
	   base term for both operands.  */
	tmp1 = find_base_term (tmp1);
	tmp2 = find_base_term (tmp2);

	/* If either base term is named object or a special address
	   (like an argument or stack reference), then use it for the
	   base term.  */
	if (tmp1 != 0
	    && (GET_CODE (tmp1) == SYMBOL_REF
		|| GET_CODE (tmp1) == LABEL_REF
		|| (GET_CODE (tmp1) == ADDRESS
		    && GET_MODE (tmp1) != VOIDmode)))
	  return tmp1;

	if (tmp2 != 0
	    && (GET_CODE (tmp2) == SYMBOL_REF
		|| GET_CODE (tmp2) == LABEL_REF
		|| (GET_CODE (tmp2) == ADDRESS
		    && GET_MODE (tmp2) != VOIDmode)))
	  return tmp2;

	/* We could not determine which of the two operands was the
	   base register and which was the index.  So we can determine
	   nothing from the base alias check.  */
	return 0;
      }

    case AND:
      if (GET_CODE (XEXP (x, 1)) == CONST_INT && INTVAL (XEXP (x, 1)) != 0)
	return find_base_term (XEXP (x, 0));
      return 0;

    case SYMBOL_REF:
    case LABEL_REF:
      return x;

    default:
      return 0;
    }
}

/* Return 0 if the addresses X and Y are known to point to different
   objects, 1 if they might be pointers to the same object.  */

static int
base_alias_check (rtx x, rtx y, enum machine_mode x_mode,
		  enum machine_mode y_mode)
{
  rtx x_base = find_base_term (x);
  rtx y_base = find_base_term (y);

  /* If the address itself has no known base see if a known equivalent
     value has one.  If either address still has no known base, nothing
     is known about aliasing.  */
  if (x_base == 0)
    {
      rtx x_c;

      if (! flag_expensive_optimizations || (x_c = canon_rtx (x)) == x)
	return 1;

      x_base = find_base_term (x_c);
      if (x_base == 0)
	return 1;
    }

  if (y_base == 0)
    {
      rtx y_c;
      if (! flag_expensive_optimizations || (y_c = canon_rtx (y)) == y)
	return 1;

      y_base = find_base_term (y_c);
      if (y_base == 0)
	return 1;
    }

  /* If the base addresses are equal nothing is known about aliasing.  */
  if (rtx_equal_p (x_base, y_base))
    return 1;

  /* The base addresses of the read and write are different expressions.
     If they are both symbols and they are not accessed via AND, there is
     no conflict.  We can bring knowledge of object alignment into play
     here.  For example, on alpha, "char a, b;" can alias one another,
     though "char a; long b;" cannot.  */
  if (GET_CODE (x_base) != ADDRESS && GET_CODE (y_base) != ADDRESS)
    {
      if (GET_CODE (x) == AND && GET_CODE (y) == AND)
	return 1;
      if (GET_CODE (x) == AND
	  && (GET_CODE (XEXP (x, 1)) != CONST_INT
	      || (int) GET_MODE_UNIT_SIZE (y_mode) < -INTVAL (XEXP (x, 1))))
	return 1;
      if (GET_CODE (y) == AND
	  && (GET_CODE (XEXP (y, 1)) != CONST_INT
	      || (int) GET_MODE_UNIT_SIZE (x_mode) < -INTVAL (XEXP (y, 1))))
	return 1;
      /* Differing symbols never alias.  */
      return 0;
    }

  /* If one address is a stack reference there can be no alias:
     stack references using different base registers do not alias,
     a stack reference can not alias a parameter, and a stack reference
     can not alias a global.  */
  if ((GET_CODE (x_base) == ADDRESS && GET_MODE (x_base) == Pmode)
      || (GET_CODE (y_base) == ADDRESS && GET_MODE (y_base) == Pmode))
    return 0;

  if (! flag_argument_noalias)
    return 1;

  if (flag_argument_noalias > 1)
    return 0;

  /* Weak noalias assertion (arguments are distinct, but may match globals).  */
  return ! (GET_MODE (x_base) == VOIDmode && GET_MODE (y_base) == VOIDmode);
}

/* Convert the address X into something we can use.  This is done by returning
   it unchanged unless it is a value; in the latter case we call cselib to get
   a more useful rtx.  */

rtx
get_addr (rtx x)
{
  cselib_val *v;
  struct elt_loc_list *l;

  if (GET_CODE (x) != VALUE)
    return x;
  v = CSELIB_VAL_PTR (x);
  if (v)
    {
      for (l = v->locs; l; l = l->next)
	if (CONSTANT_P (l->loc))
	  return l->loc;
      for (l = v->locs; l; l = l->next)
	if (!REG_P (l->loc) && !MEM_P (l->loc))
	  return l->loc;
      if (v->locs)
	return v->locs->loc;
    }
  return x;
}

/*  Return the address of the (N_REFS + 1)th memory reference to ADDR
    where SIZE is the size in bytes of the memory reference.  If ADDR
    is not modified by the memory reference then ADDR is returned.  */

static rtx
addr_side_effect_eval (rtx addr, int size, int n_refs)
{
  int offset = 0;

  switch (GET_CODE (addr))
    {
    case PRE_INC:
      offset = (n_refs + 1) * size;
      break;
    case PRE_DEC:
      offset = -(n_refs + 1) * size;
      break;
    case POST_INC:
      offset = n_refs * size;
      break;
    case POST_DEC:
      offset = -n_refs * size;
      break;

    default:
      return addr;
    }

  if (offset)
    addr = gen_rtx_PLUS (GET_MODE (addr), XEXP (addr, 0),
			 GEN_INT (offset));
  else
    addr = XEXP (addr, 0);
  addr = canon_rtx (addr);

  return addr;
}

/* Return nonzero if X and Y (memory addresses) could reference the
   same location in memory.  C is an offset accumulator.  When
   C is nonzero, we are testing aliases between X and Y + C.
   XSIZE is the size in bytes of the X reference,
   similarly YSIZE is the size in bytes for Y.
   Expect that canon_rtx has been already called for X and Y.

   If XSIZE or YSIZE is zero, we do not know the amount of memory being
   referenced (the reference was BLKmode), so make the most pessimistic
   assumptions.

   If XSIZE or YSIZE is negative, we may access memory outside the object
   being referenced as a side effect.  This can happen when using AND to
   align memory references, as is done on the Alpha.

   Nice to notice that varying addresses cannot conflict with fp if no
   local variables had their addresses taken, but that's too hard now.  */

static int
memrefs_conflict_p (int xsize, rtx x, int ysize, rtx y, HOST_WIDE_INT c)
{
  if (GET_CODE (x) == VALUE)
    x = get_addr (x);
  if (GET_CODE (y) == VALUE)
    y = get_addr (y);
  if (GET_CODE (x) == HIGH)
    x = XEXP (x, 0);
  else if (GET_CODE (x) == LO_SUM)
    x = XEXP (x, 1);
  else
    x = addr_side_effect_eval (x, xsize, 0);
  if (GET_CODE (y) == HIGH)
    y = XEXP (y, 0);
  else if (GET_CODE (y) == LO_SUM)
    y = XEXP (y, 1);
  else
    y = addr_side_effect_eval (y, ysize, 0);

  if (rtx_equal_for_memref_p (x, y))
    {
      if (xsize <= 0 || ysize <= 0)
	return 1;
      if (c >= 0 && xsize > c)
	return 1;
      if (c < 0 && ysize+c > 0)
	return 1;
      return 0;
    }

  /* This code used to check for conflicts involving stack references and
     globals but the base address alias code now handles these cases.  */

  if (GET_CODE (x) == PLUS)
    {
      /* The fact that X is canonicalized means that this
	 PLUS rtx is canonicalized.  */
      rtx x0 = XEXP (x, 0);
      rtx x1 = XEXP (x, 1);

      if (GET_CODE (y) == PLUS)
	{
	  /* The fact that Y is canonicalized means that this
	     PLUS rtx is canonicalized.  */
	  rtx y0 = XEXP (y, 0);
	  rtx y1 = XEXP (y, 1);

	  if (rtx_equal_for_memref_p (x1, y1))
	    return memrefs_conflict_p (xsize, x0, ysize, y0, c);
	  if (rtx_equal_for_memref_p (x0, y0))
	    return memrefs_conflict_p (xsize, x1, ysize, y1, c);
	  if (GET_CODE (x1) == CONST_INT)
	    {
	      if (GET_CODE (y1) == CONST_INT)
		return memrefs_conflict_p (xsize, x0, ysize, y0,
					   c - INTVAL (x1) + INTVAL (y1));
	      else
		return memrefs_conflict_p (xsize, x0, ysize, y,
					   c - INTVAL (x1));
	    }
	  else if (GET_CODE (y1) == CONST_INT)
	    return memrefs_conflict_p (xsize, x, ysize, y0, c + INTVAL (y1));

	  return 1;
	}
      else if (GET_CODE (x1) == CONST_INT)
	return memrefs_conflict_p (xsize, x0, ysize, y, c - INTVAL (x1));
    }
  else if (GET_CODE (y) == PLUS)
    {
      /* The fact that Y is canonicalized means that this
	 PLUS rtx is canonicalized.  */
      rtx y0 = XEXP (y, 0);
      rtx y1 = XEXP (y, 1);

      if (GET_CODE (y1) == CONST_INT)
	return memrefs_conflict_p (xsize, x, ysize, y0, c + INTVAL (y1));
      else
	return 1;
    }

  if (GET_CODE (x) == GET_CODE (y))
    switch (GET_CODE (x))
      {
      case MULT:
	{
	  /* Handle cases where we expect the second operands to be the
	     same, and check only whether the first operand would conflict
	     or not.  */
	  rtx x0, y0;
	  rtx x1 = canon_rtx (XEXP (x, 1));
	  rtx y1 = canon_rtx (XEXP (y, 1));
	  if (! rtx_equal_for_memref_p (x1, y1))
	    return 1;
	  x0 = canon_rtx (XEXP (x, 0));
	  y0 = canon_rtx (XEXP (y, 0));
	  if (rtx_equal_for_memref_p (x0, y0))
	    return (xsize == 0 || ysize == 0
		    || (c >= 0 && xsize > c) || (c < 0 && ysize+c > 0));

	  /* Can't properly adjust our sizes.  */
	  if (GET_CODE (x1) != CONST_INT)
	    return 1;
	  xsize /= INTVAL (x1);
	  ysize /= INTVAL (x1);
	  c /= INTVAL (x1);
	  return memrefs_conflict_p (xsize, x0, ysize, y0, c);
	}

      default:
	break;
      }

  /* Treat an access through an AND (e.g. a subword access on an Alpha)
     as an access with indeterminate size.  Assume that references
     besides AND are aligned, so if the size of the other reference is
     at least as large as the alignment, assume no other overlap.  */
  if (GET_CODE (x) == AND && GET_CODE (XEXP (x, 1)) == CONST_INT)
    {
      if (GET_CODE (y) == AND || ysize < -INTVAL (XEXP (x, 1)))
	xsize = -1;
      return memrefs_conflict_p (xsize, canon_rtx (XEXP (x, 0)), ysize, y, c);
    }
  if (GET_CODE (y) == AND && GET_CODE (XEXP (y, 1)) == CONST_INT)
    {
      /* ??? If we are indexing far enough into the array/structure, we
	 may yet be able to determine that we can not overlap.  But we
	 also need to that we are far enough from the end not to overlap
	 a following reference, so we do nothing with that for now.  */
      if (GET_CODE (x) == AND || xsize < -INTVAL (XEXP (y, 1)))
	ysize = -1;
      return memrefs_conflict_p (xsize, x, ysize, canon_rtx (XEXP (y, 0)), c);
    }

  if (CONSTANT_P (x))
    {
      if (GET_CODE (x) == CONST_INT && GET_CODE (y) == CONST_INT)
	{
	  c += (INTVAL (y) - INTVAL (x));
	  return (xsize <= 0 || ysize <= 0
		  || (c >= 0 && xsize > c) || (c < 0 && ysize+c > 0));
	}

      if (GET_CODE (x) == CONST)
	{
	  if (GET_CODE (y) == CONST)
	    return memrefs_conflict_p (xsize, canon_rtx (XEXP (x, 0)),
				       ysize, canon_rtx (XEXP (y, 0)), c);
	  else
	    return memrefs_conflict_p (xsize, canon_rtx (XEXP (x, 0)),
				       ysize, y, c);
	}
      if (GET_CODE (y) == CONST)
	return memrefs_conflict_p (xsize, x, ysize,
				   canon_rtx (XEXP (y, 0)), c);

      if (CONSTANT_P (y))
	return (xsize <= 0 || ysize <= 0
		|| (rtx_equal_for_memref_p (x, y)
		    && ((c >= 0 && xsize > c) || (c < 0 && ysize+c > 0))));

      return 1;
    }
  return 1;
}

/* Functions to compute memory dependencies.

   Since we process the insns in execution order, we can build tables
   to keep track of what registers are fixed (and not aliased), what registers
   are varying in known ways, and what registers are varying in unknown
   ways.

   If both memory references are volatile, then there must always be a
   dependence between the two references, since their order can not be
   changed.  A volatile and non-volatile reference can be interchanged
   though.

   A MEM_IN_STRUCT reference at a non-AND varying address can never
   conflict with a non-MEM_IN_STRUCT reference at a fixed address.  We
   also must allow AND addresses, because they may generate accesses
   outside the object being referenced.  This is used to generate
   aligned addresses from unaligned addresses, for instance, the alpha
   storeqi_unaligned pattern.  */

/* Read dependence: X is read after read in MEM takes place.  There can
   only be a dependence here if both reads are volatile.  */

int
read_dependence (rtx mem, rtx x)
{
  return MEM_VOLATILE_P (x) && MEM_VOLATILE_P (mem);
}

/* Returns MEM1 if and only if MEM1 is a scalar at a fixed address and
   MEM2 is a reference to a structure at a varying address, or returns
   MEM2 if vice versa.  Otherwise, returns NULL_RTX.  If a non-NULL
   value is returned MEM1 and MEM2 can never alias.  VARIES_P is used
   to decide whether or not an address may vary; it should return
   nonzero whenever variation is possible.
   MEM1_ADDR and MEM2_ADDR are the addresses of MEM1 and MEM2.  */

static rtx
fixed_scalar_and_varying_struct_p (rtx mem1, rtx mem2, rtx mem1_addr,
				   rtx mem2_addr,
				   int (*varies_p) (rtx, int))
{
  if (! flag_strict_aliasing)
    return NULL_RTX;

  if (MEM_SCALAR_P (mem1) && MEM_IN_STRUCT_P (mem2)
      && !varies_p (mem1_addr, 1) && varies_p (mem2_addr, 1))
    /* MEM1 is a scalar at a fixed address; MEM2 is a struct at a
       varying address.  */
    return mem1;

  if (MEM_IN_STRUCT_P (mem1) && MEM_SCALAR_P (mem2)
      && varies_p (mem1_addr, 1) && !varies_p (mem2_addr, 1))
    /* MEM2 is a scalar at a fixed address; MEM1 is a struct at a
       varying address.  */
    return mem2;

  return NULL_RTX;
}

/* Returns nonzero if something about the mode or address format MEM1
   indicates that it might well alias *anything*.  */

static int
aliases_everything_p (rtx mem)
{
  if (GET_CODE (XEXP (mem, 0)) == AND)
    /* If the address is an AND, it's very hard to know at what it is
       actually pointing.  */
    return 1;

  return 0;
}

/* Return true if we can determine that the fields referenced cannot
   overlap for any pair of objects.  */

static bool
nonoverlapping_component_refs_p (tree x, tree y)
{
  tree fieldx, fieldy, typex, typey, orig_y;

  do
    {
      /* The comparison has to be done at a common type, since we don't
	 know how the inheritance hierarchy works.  */
      orig_y = y;
      do
	{
	  fieldx = TREE_OPERAND (x, 1);
	  typex = TYPE_MAIN_VARIANT (DECL_FIELD_CONTEXT (fieldx));

	  y = orig_y;
	  do
	    {
	      fieldy = TREE_OPERAND (y, 1);
	      typey = TYPE_MAIN_VARIANT (DECL_FIELD_CONTEXT (fieldy));

	      if (typex == typey)
		goto found;

	      y = TREE_OPERAND (y, 0);
	    }
	  while (y && TREE_CODE (y) == COMPONENT_REF);

	  x = TREE_OPERAND (x, 0);
	}
      while (x && TREE_CODE (x) == COMPONENT_REF);
      /* Never found a common type.  */
      return false;

    found:
      /* If we're left with accessing different fields of a structure,
	 then no overlap.  */
      if (TREE_CODE (typex) == RECORD_TYPE
	  && fieldx != fieldy)
	return true;

      /* The comparison on the current field failed.  If we're accessing
	 a very nested structure, look at the next outer level.  */
      x = TREE_OPERAND (x, 0);
      y = TREE_OPERAND (y, 0);
    }
  while (x && y
	 && TREE_CODE (x) == COMPONENT_REF
	 && TREE_CODE (y) == COMPONENT_REF);

  return false;
}

/* Look at the bottom of the COMPONENT_REF list for a DECL, and return it.  */

static tree
decl_for_component_ref (tree x)
{
  do
    {
      x = TREE_OPERAND (x, 0);
    }
  while (x && TREE_CODE (x) == COMPONENT_REF);

  return x && DECL_P (x) ? x : NULL_TREE;
}

/* Walk up the COMPONENT_REF list and adjust OFFSET to compensate for the
   offset of the field reference.  */

static rtx
adjust_offset_for_component_ref (tree x, rtx offset)
{
  HOST_WIDE_INT ioffset;

  if (! offset)
    return NULL_RTX;

  ioffset = INTVAL (offset);
  do
    {
      tree offset = component_ref_field_offset (x);
      tree field = TREE_OPERAND (x, 1);

      if (! host_integerp (offset, 1))
	return NULL_RTX;
      ioffset += (tree_low_cst (offset, 1)
		  + (tree_low_cst (DECL_FIELD_BIT_OFFSET (field), 1)
		     / BITS_PER_UNIT));

      x = TREE_OPERAND (x, 0);
    }
  while (x && TREE_CODE (x) == COMPONENT_REF);

  return GEN_INT (ioffset);
}

/* Return nonzero if we can determine the exprs corresponding to memrefs
   X and Y and they do not overlap.  */

static int
nonoverlapping_memrefs_p (rtx x, rtx y)
{
  tree exprx = MEM_EXPR (x), expry = MEM_EXPR (y);
  rtx rtlx, rtly;
  rtx basex, basey;
  rtx moffsetx, moffsety;
  HOST_WIDE_INT offsetx = 0, offsety = 0, sizex, sizey, tem;

  /* Unless both have exprs, we can't tell anything.  */
  if (exprx == 0 || expry == 0)
    return 0;

  /* If both are field references, we may be able to determine something.  */
  if (TREE_CODE (exprx) == COMPONENT_REF
      && TREE_CODE (expry) == COMPONENT_REF
      && nonoverlapping_component_refs_p (exprx, expry))
    return 1;


  /* If the field reference test failed, look at the DECLs involved.  */
  moffsetx = MEM_OFFSET (x);
  if (TREE_CODE (exprx) == COMPONENT_REF)
    {
      if (TREE_CODE (expry) == VAR_DECL
	  && POINTER_TYPE_P (TREE_TYPE (expry)))
	{
	 tree field = TREE_OPERAND (exprx, 1);
	 tree fieldcontext = DECL_FIELD_CONTEXT (field);
	 if (ipa_type_escape_field_does_not_clobber_p (fieldcontext,
						       TREE_TYPE (field)))
	   return 1;
	}
      {
	tree t = decl_for_component_ref (exprx);
	if (! t)
	  return 0;
	moffsetx = adjust_offset_for_component_ref (exprx, moffsetx);
	exprx = t;
      }
    }
  else if (INDIRECT_REF_P (exprx))
    {
      exprx = TREE_OPERAND (exprx, 0);
      if (flag_argument_noalias < 2
	  || TREE_CODE (exprx) != PARM_DECL)
	return 0;
    }

  moffsety = MEM_OFFSET (y);
  if (TREE_CODE (expry) == COMPONENT_REF)
    {
      if (TREE_CODE (exprx) == VAR_DECL
	  && POINTER_TYPE_P (TREE_TYPE (exprx)))
	{
	 tree field = TREE_OPERAND (expry, 1);
	 tree fieldcontext = DECL_FIELD_CONTEXT (field);
	 if (ipa_type_escape_field_does_not_clobber_p (fieldcontext,
						       TREE_TYPE (field)))
	   return 1;
	}
      {
	tree t = decl_for_component_ref (expry);
	if (! t)
	  return 0;
	moffsety = adjust_offset_for_component_ref (expry, moffsety);
	expry = t;
      }
    }
  else if (INDIRECT_REF_P (expry))
    {
      expry = TREE_OPERAND (expry, 0);
      if (flag_argument_noalias < 2
	  || TREE_CODE (expry) != PARM_DECL)
	return 0;
    }

  if (! DECL_P (exprx) || ! DECL_P (expry))
    return 0;

  rtlx = DECL_RTL (exprx);
  rtly = DECL_RTL (expry);

  /* If either RTL is not a MEM, it must be a REG or CONCAT, meaning they
     can't overlap unless they are the same because we never reuse that part
     of the stack frame used for locals for spilled pseudos.  */
  if ((!MEM_P (rtlx) || !MEM_P (rtly))
      && ! rtx_equal_p (rtlx, rtly))
    return 1;

  /* Get the base and offsets of both decls.  If either is a register, we
     know both are and are the same, so use that as the base.  The only
     we can avoid overlap is if we can deduce that they are nonoverlapping
     pieces of that decl, which is very rare.  */
  basex = MEM_P (rtlx) ? XEXP (rtlx, 0) : rtlx;
  if (GET_CODE (basex) == PLUS && GET_CODE (XEXP (basex, 1)) == CONST_INT)
    offsetx = INTVAL (XEXP (basex, 1)), basex = XEXP (basex, 0);

  basey = MEM_P (rtly) ? XEXP (rtly, 0) : rtly;
  if (GET_CODE (basey) == PLUS && GET_CODE (XEXP (basey, 1)) == CONST_INT)
    offsety = INTVAL (XEXP (basey, 1)), basey = XEXP (basey, 0);

  /* If the bases are different, we know they do not overlap if both
     are constants or if one is a constant and the other a pointer into the
     stack frame.  Otherwise a different base means we can't tell if they
     overlap or not.  */
  if (! rtx_equal_p (basex, basey))
    return ((CONSTANT_P (basex) && CONSTANT_P (basey))
	    || (CONSTANT_P (basex) && REG_P (basey)
		&& REGNO_PTR_FRAME_P (REGNO (basey)))
	    || (CONSTANT_P (basey) && REG_P (basex)
		&& REGNO_PTR_FRAME_P (REGNO (basex))));

  sizex = (!MEM_P (rtlx) ? (int) GET_MODE_SIZE (GET_MODE (rtlx))
	   : MEM_SIZE (rtlx) ? INTVAL (MEM_SIZE (rtlx))
	   : -1);
  sizey = (!MEM_P (rtly) ? (int) GET_MODE_SIZE (GET_MODE (rtly))
	   : MEM_SIZE (rtly) ? INTVAL (MEM_SIZE (rtly)) :
	   -1);

  /* If we have an offset for either memref, it can update the values computed
     above.  */
  if (moffsetx)
    offsetx += INTVAL (moffsetx), sizex -= INTVAL (moffsetx);
  if (moffsety)
    offsety += INTVAL (moffsety), sizey -= INTVAL (moffsety);

  /* If a memref has both a size and an offset, we can use the smaller size.
     We can't do this if the offset isn't known because we must view this
     memref as being anywhere inside the DECL's MEM.  */
  if (MEM_SIZE (x) && moffsetx)
    sizex = INTVAL (MEM_SIZE (x));
  if (MEM_SIZE (y) && moffsety)
    sizey = INTVAL (MEM_SIZE (y));

  /* Put the values of the memref with the lower offset in X's values.  */
  if (offsetx > offsety)
    {
      tem = offsetx, offsetx = offsety, offsety = tem;
      tem = sizex, sizex = sizey, sizey = tem;
    }

  /* If we don't know the size of the lower-offset value, we can't tell
     if they conflict.  Otherwise, we do the test.  */
  return sizex >= 0 && offsety >= offsetx + sizex;
}

/* True dependence: X is read after store in MEM takes place.  */

int
true_dependence (rtx mem, enum machine_mode mem_mode, rtx x,
		 int (*varies) (rtx, int))
{
  rtx x_addr, mem_addr;
  rtx base;

  if (MEM_VOLATILE_P (x) && MEM_VOLATILE_P (mem))
    return 1;

  /* (mem:BLK (scratch)) is a special mechanism to conflict with everything.
     This is used in epilogue deallocation functions.  */
  if (GET_MODE (x) == BLKmode && GET_CODE (XEXP (x, 0)) == SCRATCH)
    return 1;
  if (GET_MODE (mem) == BLKmode && GET_CODE (XEXP (mem, 0)) == SCRATCH)
    return 1;
  if (MEM_ALIAS_SET (x) == ALIAS_SET_MEMORY_BARRIER
      || MEM_ALIAS_SET (mem) == ALIAS_SET_MEMORY_BARRIER)
    return 1;

  if (DIFFERENT_ALIAS_SETS_P (x, mem))
    return 0;

  /* Read-only memory is by definition never modified, and therefore can't
     conflict with anything.  We don't expect to find read-only set on MEM,
     but stupid user tricks can produce them, so don't die.  */
  if (MEM_READONLY_P (x))
    return 0;

  if (nonoverlapping_memrefs_p (mem, x))
    return 0;

  if (mem_mode == VOIDmode)
    mem_mode = GET_MODE (mem);

  x_addr = get_addr (XEXP (x, 0));
  mem_addr = get_addr (XEXP (mem, 0));

  base = find_base_term (x_addr);
  if (base && (GET_CODE (base) == LABEL_REF
	       || (GET_CODE (base) == SYMBOL_REF
		   && CONSTANT_POOL_ADDRESS_P (base))))
    return 0;

  if (! base_alias_check (x_addr, mem_addr, GET_MODE (x), mem_mode))
    return 0;

  x_addr = canon_rtx (x_addr);
  mem_addr = canon_rtx (mem_addr);

  if (! memrefs_conflict_p (GET_MODE_SIZE (mem_mode), mem_addr,
			    SIZE_FOR_MODE (x), x_addr, 0))
    return 0;

  if (aliases_everything_p (x))
    return 1;

  /* We cannot use aliases_everything_p to test MEM, since we must look
     at MEM_MODE, rather than GET_MODE (MEM).  */
  if (mem_mode == QImode || GET_CODE (mem_addr) == AND)
    return 1;

  /* In true_dependence we also allow BLKmode to alias anything.  Why
     don't we do this in anti_dependence and output_dependence?  */
  if (mem_mode == BLKmode || GET_MODE (x) == BLKmode)
    return 1;

  return ! fixed_scalar_and_varying_struct_p (mem, x, mem_addr, x_addr,
					      varies);
}

/* Canonical true dependence: X is read after store in MEM takes place.
   Variant of true_dependence which assumes MEM has already been
   canonicalized (hence we no longer do that here).
   The mem_addr argument has been added, since true_dependence computed
   this value prior to canonicalizing.  */

int
canon_true_dependence (rtx mem, enum machine_mode mem_mode, rtx mem_addr,
		       rtx x, int (*varies) (rtx, int))
{
  rtx x_addr;

  if (MEM_VOLATILE_P (x) && MEM_VOLATILE_P (mem))
    return 1;

  /* (mem:BLK (scratch)) is a special mechanism to conflict with everything.
     This is used in epilogue deallocation functions.  */
  if (GET_MODE (x) == BLKmode && GET_CODE (XEXP (x, 0)) == SCRATCH)
    return 1;
  if (GET_MODE (mem) == BLKmode && GET_CODE (XEXP (mem, 0)) == SCRATCH)
    return 1;
  if (MEM_ALIAS_SET (x) == ALIAS_SET_MEMORY_BARRIER
      || MEM_ALIAS_SET (mem) == ALIAS_SET_MEMORY_BARRIER)
    return 1;

  if (DIFFERENT_ALIAS_SETS_P (x, mem))
    return 0;

  /* Read-only memory is by definition never modified, and therefore can't
     conflict with anything.  We don't expect to find read-only set on MEM,
     but stupid user tricks can produce them, so don't die.  */
  if (MEM_READONLY_P (x))
    return 0;

  if (nonoverlapping_memrefs_p (x, mem))
    return 0;

  x_addr = get_addr (XEXP (x, 0));

  if (! base_alias_check (x_addr, mem_addr, GET_MODE (x), mem_mode))
    return 0;

  x_addr = canon_rtx (x_addr);
  if (! memrefs_conflict_p (GET_MODE_SIZE (mem_mode), mem_addr,
			    SIZE_FOR_MODE (x), x_addr, 0))
    return 0;

  if (aliases_everything_p (x))
    return 1;

  /* We cannot use aliases_everything_p to test MEM, since we must look
     at MEM_MODE, rather than GET_MODE (MEM).  */
  if (mem_mode == QImode || GET_CODE (mem_addr) == AND)
    return 1;

  /* In true_dependence we also allow BLKmode to alias anything.  Why
     don't we do this in anti_dependence and output_dependence?  */
  if (mem_mode == BLKmode || GET_MODE (x) == BLKmode)
    return 1;

  return ! fixed_scalar_and_varying_struct_p (mem, x, mem_addr, x_addr,
					      varies);
}

/* Returns nonzero if a write to X might alias a previous read from
   (or, if WRITEP is nonzero, a write to) MEM.  */

static int
write_dependence_p (rtx mem, rtx x, int writep)
{
  rtx x_addr, mem_addr;
  rtx fixed_scalar;
  rtx base;

  if (MEM_VOLATILE_P (x) && MEM_VOLATILE_P (mem))
    return 1;

  /* (mem:BLK (scratch)) is a special mechanism to conflict with everything.
     This is used in epilogue deallocation functions.  */
  if (GET_MODE (x) == BLKmode && GET_CODE (XEXP (x, 0)) == SCRATCH)
    return 1;
  if (GET_MODE (mem) == BLKmode && GET_CODE (XEXP (mem, 0)) == SCRATCH)
    return 1;
  if (MEM_ALIAS_SET (x) == ALIAS_SET_MEMORY_BARRIER
      || MEM_ALIAS_SET (mem) == ALIAS_SET_MEMORY_BARRIER)
    return 1;

  if (DIFFERENT_ALIAS_SETS_P (x, mem))
    return 0;

  /* A read from read-only memory can't conflict with read-write memory.  */
  if (!writep && MEM_READONLY_P (mem))
    return 0;

  if (nonoverlapping_memrefs_p (x, mem))
    return 0;

  x_addr = get_addr (XEXP (x, 0));
  mem_addr = get_addr (XEXP (mem, 0));

  if (! writep)
    {
      base = find_base_term (mem_addr);
      if (base && (GET_CODE (base) == LABEL_REF
		   || (GET_CODE (base) == SYMBOL_REF
		       && CONSTANT_POOL_ADDRESS_P (base))))
	return 0;
    }

  if (! base_alias_check (x_addr, mem_addr, GET_MODE (x),
			  GET_MODE (mem)))
    return 0;

  x_addr = canon_rtx (x_addr);
  mem_addr = canon_rtx (mem_addr);

  if (!memrefs_conflict_p (SIZE_FOR_MODE (mem), mem_addr,
			   SIZE_FOR_MODE (x), x_addr, 0))
    return 0;

  fixed_scalar
    = fixed_scalar_and_varying_struct_p (mem, x, mem_addr, x_addr,
					 rtx_addr_varies_p);

  return (!(fixed_scalar == mem && !aliases_everything_p (x))
	  && !(fixed_scalar == x && !aliases_everything_p (mem)));
}

/* Anti dependence: X is written after read in MEM takes place.  */

int
anti_dependence (rtx mem, rtx x)
{
  return write_dependence_p (mem, x, /*writep=*/0);
}

/* Output dependence: X is written after store in MEM takes place.  */

int
output_dependence (rtx mem, rtx x)
{
  return write_dependence_p (mem, x, /*writep=*/1);
}


void
init_alias_once (void)
{
  int i;

  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    /* Check whether this register can hold an incoming pointer
       argument.  FUNCTION_ARG_REGNO_P tests outgoing register
       numbers, so translate if necessary due to register windows.  */
    if (FUNCTION_ARG_REGNO_P (OUTGOING_REGNO (i))
	&& HARD_REGNO_MODE_OK (i, Pmode))
      static_reg_base_value[i]
	= gen_rtx_ADDRESS (VOIDmode, gen_rtx_REG (Pmode, i));

  static_reg_base_value[STACK_POINTER_REGNUM]
    = gen_rtx_ADDRESS (Pmode, stack_pointer_rtx);
  static_reg_base_value[ARG_POINTER_REGNUM]
    = gen_rtx_ADDRESS (Pmode, arg_pointer_rtx);
  static_reg_base_value[FRAME_POINTER_REGNUM]
    = gen_rtx_ADDRESS (Pmode, frame_pointer_rtx);
#if HARD_FRAME_POINTER_REGNUM != FRAME_POINTER_REGNUM
  static_reg_base_value[HARD_FRAME_POINTER_REGNUM]
    = gen_rtx_ADDRESS (Pmode, hard_frame_pointer_rtx);
#endif
}

/* Set MEMORY_MODIFIED when X modifies DATA (that is assumed
   to be memory reference.  */
static bool memory_modified;
static void
memory_modified_1 (rtx x, rtx pat ATTRIBUTE_UNUSED, void *data)
{
  if (MEM_P (x))
    {
      if (anti_dependence (x, (rtx)data) || output_dependence (x, (rtx)data))
	memory_modified = true;
    }
}


/* Return true when INSN possibly modify memory contents of MEM
   (i.e. address can be modified).  */
bool
memory_modified_in_insn_p (rtx mem, rtx insn)
{
  if (!INSN_P (insn))
    return false;
  memory_modified = false;
  note_stores (PATTERN (insn), memory_modified_1, mem);
  return memory_modified;
}

/* Initialize the aliasing machinery.  Initialize the REG_KNOWN_VALUE
   array.  */

void
init_alias_analysis (void)
{
  unsigned int maxreg = max_reg_num ();
  int changed, pass;
  int i;
  unsigned int ui;
  rtx insn;

  timevar_push (TV_ALIAS_ANALYSIS);

  reg_known_value_size = maxreg - FIRST_PSEUDO_REGISTER;
  reg_known_value = ggc_calloc (reg_known_value_size, sizeof (rtx));
  reg_known_equiv_p = xcalloc (reg_known_value_size, sizeof (bool));

  /* If we have memory allocated from the previous run, use it.  */
  if (old_reg_base_value)
    reg_base_value = old_reg_base_value;

  if (reg_base_value)
    VEC_truncate (rtx, reg_base_value, 0);

  VEC_safe_grow (rtx, gc, reg_base_value, maxreg);
  memset (VEC_address (rtx, reg_base_value), 0,
	  sizeof (rtx) * VEC_length (rtx, reg_base_value));

  new_reg_base_value = XNEWVEC (rtx, maxreg);
  reg_seen = XNEWVEC (char, maxreg);

  /* The basic idea is that each pass through this loop will use the
     "constant" information from the previous pass to propagate alias
     information through another level of assignments.

     This could get expensive if the assignment chains are long.  Maybe
     we should throttle the number of iterations, possibly based on
     the optimization level or flag_expensive_optimizations.

     We could propagate more information in the first pass by making use
     of REG_N_SETS to determine immediately that the alias information
     for a pseudo is "constant".

     A program with an uninitialized variable can cause an infinite loop
     here.  Instead of doing a full dataflow analysis to detect such problems
     we just cap the number of iterations for the loop.

     The state of the arrays for the set chain in question does not matter
     since the program has undefined behavior.  */

  pass = 0;
  do
    {
      /* Assume nothing will change this iteration of the loop.  */
      changed = 0;

      /* We want to assign the same IDs each iteration of this loop, so
	 start counting from zero each iteration of the loop.  */
      unique_id = 0;

      /* We're at the start of the function each iteration through the
	 loop, so we're copying arguments.  */
      copying_arguments = true;

      /* Wipe the potential alias information clean for this pass.  */
      memset (new_reg_base_value, 0, maxreg * sizeof (rtx));

      /* Wipe the reg_seen array clean.  */
      memset (reg_seen, 0, maxreg);

      /* Mark all hard registers which may contain an address.
	 The stack, frame and argument pointers may contain an address.
	 An argument register which can hold a Pmode value may contain
	 an address even if it is not in BASE_REGS.

	 The address expression is VOIDmode for an argument and
	 Pmode for other registers.  */

      memcpy (new_reg_base_value, static_reg_base_value,
	      FIRST_PSEUDO_REGISTER * sizeof (rtx));

      /* Walk the insns adding values to the new_reg_base_value array.  */
      for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
	{
	  if (INSN_P (insn))
	    {
	      rtx note, set;

#if defined (HAVE_prologue) || defined (HAVE_epilogue)
	      /* The prologue/epilogue insns are not threaded onto the
		 insn chain until after reload has completed.  Thus,
		 there is no sense wasting time checking if INSN is in
		 the prologue/epilogue until after reload has completed.  */
	      if (reload_completed
		  && prologue_epilogue_contains (insn))
		continue;
#endif

	      /* If this insn has a noalias note, process it,  Otherwise,
		 scan for sets.  A simple set will have no side effects
		 which could change the base value of any other register.  */

	      if (GET_CODE (PATTERN (insn)) == SET
		  && REG_NOTES (insn) != 0
		  && find_reg_note (insn, REG_NOALIAS, NULL_RTX))
		record_set (SET_DEST (PATTERN (insn)), NULL_RTX, NULL);
	      else
		note_stores (PATTERN (insn), record_set, NULL);

	      set = single_set (insn);

	      if (set != 0
		  && REG_P (SET_DEST (set))
		  && REGNO (SET_DEST (set)) >= FIRST_PSEUDO_REGISTER)
		{
		  unsigned int regno = REGNO (SET_DEST (set));
		  rtx src = SET_SRC (set);
		  rtx t;

		  if (REG_NOTES (insn) != 0
		      && (((note = find_reg_note (insn, REG_EQUAL, 0)) != 0
			   && REG_N_SETS (regno) == 1)
			  || (note = find_reg_note (insn, REG_EQUIV, NULL_RTX)) != 0)
		      && GET_CODE (XEXP (note, 0)) != EXPR_LIST
		      && ! rtx_varies_p (XEXP (note, 0), 1)
		      && ! reg_overlap_mentioned_p (SET_DEST (set),
						    XEXP (note, 0)))
		    {
		      set_reg_known_value (regno, XEXP (note, 0));
		      set_reg_known_equiv_p (regno,
			REG_NOTE_KIND (note) == REG_EQUIV);
		    }
		  else if (REG_N_SETS (regno) == 1
			   && GET_CODE (src) == PLUS
			   && REG_P (XEXP (src, 0))
			   && (t = get_reg_known_value (REGNO (XEXP (src, 0))))
			   && GET_CODE (XEXP (src, 1)) == CONST_INT)
		    {
		      t = plus_constant (t, INTVAL (XEXP (src, 1)));
		      set_reg_known_value (regno, t);
		      set_reg_known_equiv_p (regno, 0);
		    }
		  else if (REG_N_SETS (regno) == 1
			   && ! rtx_varies_p (src, 1))
		    {
		      set_reg_known_value (regno, src);
		      set_reg_known_equiv_p (regno, 0);
		    }
		}
	    }
	  else if (NOTE_P (insn)
		   && NOTE_LINE_NUMBER (insn) == NOTE_INSN_FUNCTION_BEG)
	    copying_arguments = false;
	}

      /* Now propagate values from new_reg_base_value to reg_base_value.  */
      gcc_assert (maxreg == (unsigned int) max_reg_num());

      for (ui = 0; ui < maxreg; ui++)
	{
	  if (new_reg_base_value[ui]
	      && new_reg_base_value[ui] != VEC_index (rtx, reg_base_value, ui)
	      && ! rtx_equal_p (new_reg_base_value[ui],
				VEC_index (rtx, reg_base_value, ui)))
	    {
	      VEC_replace (rtx, reg_base_value, ui, new_reg_base_value[ui]);
	      changed = 1;
	    }
	}
    }
  while (changed && ++pass < MAX_ALIAS_LOOP_PASSES);

  /* Fill in the remaining entries.  */
  for (i = 0; i < (int)reg_known_value_size; i++)
    if (reg_known_value[i] == 0)
      reg_known_value[i] = regno_reg_rtx[i + FIRST_PSEUDO_REGISTER];

  /* Simplify the reg_base_value array so that no register refers to
     another register, except to special registers indirectly through
     ADDRESS expressions.

     In theory this loop can take as long as O(registers^2), but unless
     there are very long dependency chains it will run in close to linear
     time.

     This loop may not be needed any longer now that the main loop does
     a better job at propagating alias information.  */
  pass = 0;
  do
    {
      changed = 0;
      pass++;
      for (ui = 0; ui < maxreg; ui++)
	{
	  rtx base = VEC_index (rtx, reg_base_value, ui);
	  if (base && REG_P (base))
	    {
	      unsigned int base_regno = REGNO (base);
	      if (base_regno == ui)		/* register set from itself */
		VEC_replace (rtx, reg_base_value, ui, 0);
	      else
		VEC_replace (rtx, reg_base_value, ui,
			     VEC_index (rtx, reg_base_value, base_regno));
	      changed = 1;
	    }
	}
    }
  while (changed && pass < MAX_ALIAS_LOOP_PASSES);

  /* Clean up.  */
  free (new_reg_base_value);
  new_reg_base_value = 0;
  free (reg_seen);
  reg_seen = 0;
  timevar_pop (TV_ALIAS_ANALYSIS);
}

void
end_alias_analysis (void)
{
  old_reg_base_value = reg_base_value;
  ggc_free (reg_known_value);
  reg_known_value = 0;
  reg_known_value_size = 0;
  free (reg_known_equiv_p);
  reg_known_equiv_p = 0;
}

#include "gt-alias.h"
