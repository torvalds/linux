/* Breadth-first and depth-first routines for
   searching multiple-inheritance lattice for GNU C++.
   Copyright (C) 1987, 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
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

/* High-level class interface.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "cp-tree.h"
#include "obstack.h"
#include "flags.h"
#include "rtl.h"
#include "output.h"
#include "toplev.h"

static int is_subobject_of_p (tree, tree);
static tree dfs_lookup_base (tree, void *);
static tree dfs_dcast_hint_pre (tree, void *);
static tree dfs_dcast_hint_post (tree, void *);
static tree dfs_debug_mark (tree, void *);
static tree dfs_walk_once_r (tree, tree (*pre_fn) (tree, void *),
			     tree (*post_fn) (tree, void *), void *data);
static void dfs_unmark_r (tree);
static int check_hidden_convs (tree, int, int, tree, tree, tree);
static tree split_conversions (tree, tree, tree, tree);
static int lookup_conversions_r (tree, int, int,
				 tree, tree, tree, tree, tree *, tree *);
static int look_for_overrides_r (tree, tree);
static tree lookup_field_r (tree, void *);
static tree dfs_accessible_post (tree, void *);
static tree dfs_walk_once_accessible_r (tree, bool, bool,
					tree (*pre_fn) (tree, void *),
					tree (*post_fn) (tree, void *),
					void *data);
static tree dfs_walk_once_accessible (tree, bool,
				      tree (*pre_fn) (tree, void *),
				      tree (*post_fn) (tree, void *),
				      void *data);
static tree dfs_access_in_type (tree, void *);
static access_kind access_in_type (tree, tree);
static int protected_accessible_p (tree, tree, tree);
static int friend_accessible_p (tree, tree, tree);
static int template_self_reference_p (tree, tree);
static tree dfs_get_pure_virtuals (tree, void *);


/* Variables for gathering statistics.  */
#ifdef GATHER_STATISTICS
static int n_fields_searched;
static int n_calls_lookup_field, n_calls_lookup_field_1;
static int n_calls_lookup_fnfields, n_calls_lookup_fnfields_1;
static int n_calls_get_base_type;
static int n_outer_fields_searched;
static int n_contexts_saved;
#endif /* GATHER_STATISTICS */


/* Data for lookup_base and its workers.  */

struct lookup_base_data_s
{
  tree t;		/* type being searched.  */
  tree base;		/* The base type we're looking for.  */
  tree binfo;		/* Found binfo.  */
  bool via_virtual;	/* Found via a virtual path.  */
  bool ambiguous;	/* Found multiply ambiguous */
  bool repeated_base;	/* Whether there are repeated bases in the
			    hierarchy.  */
  bool want_any;	/* Whether we want any matching binfo.  */
};

/* Worker function for lookup_base.  See if we've found the desired
   base and update DATA_ (a pointer to LOOKUP_BASE_DATA_S).  */

static tree
dfs_lookup_base (tree binfo, void *data_)
{
  struct lookup_base_data_s *data = (struct lookup_base_data_s *) data_;

  if (SAME_BINFO_TYPE_P (BINFO_TYPE (binfo), data->base))
    {
      if (!data->binfo)
	{
	  data->binfo = binfo;
	  data->via_virtual
	    = binfo_via_virtual (data->binfo, data->t) != NULL_TREE;

	  if (!data->repeated_base)
	    /* If there are no repeated bases, we can stop now.  */
	    return binfo;

	  if (data->want_any && !data->via_virtual)
	    /* If this is a non-virtual base, then we can't do
	       better.  */
	    return binfo;

	  return dfs_skip_bases;
	}
      else
	{
	  gcc_assert (binfo != data->binfo);

	  /* We've found more than one matching binfo.  */
	  if (!data->want_any)
	    {
	      /* This is immediately ambiguous.  */
	      data->binfo = NULL_TREE;
	      data->ambiguous = true;
	      return error_mark_node;
	    }

	  /* Prefer one via a non-virtual path.  */
	  if (!binfo_via_virtual (binfo, data->t))
	    {
	      data->binfo = binfo;
	      data->via_virtual = false;
	      return binfo;
	    }

	  /* There must be repeated bases, otherwise we'd have stopped
	     on the first base we found.  */
	  return dfs_skip_bases;
	}
    }

  return NULL_TREE;
}

/* Returns true if type BASE is accessible in T.  (BASE is known to be
   a (possibly non-proper) base class of T.)  If CONSIDER_LOCAL_P is
   true, consider any special access of the current scope, or access
   bestowed by friendship.  */

bool
accessible_base_p (tree t, tree base, bool consider_local_p)
{
  tree decl;

  /* [class.access.base]

     A base class is said to be accessible if an invented public
     member of the base class is accessible.

     If BASE is a non-proper base, this condition is trivially
     true.  */
  if (same_type_p (t, base))
    return true;
  /* Rather than inventing a public member, we use the implicit
     public typedef created in the scope of every class.  */
  decl = TYPE_FIELDS (base);
  while (!DECL_SELF_REFERENCE_P (decl))
    decl = TREE_CHAIN (decl);
  while (ANON_AGGR_TYPE_P (t))
    t = TYPE_CONTEXT (t);
  return accessible_p (t, decl, consider_local_p);
}

/* Lookup BASE in the hierarchy dominated by T.  Do access checking as
   ACCESS specifies.  Return the binfo we discover.  If KIND_PTR is
   non-NULL, fill with information about what kind of base we
   discovered.

   If the base is inaccessible, or ambiguous, and the ba_quiet bit is
   not set in ACCESS, then an error is issued and error_mark_node is
   returned.  If the ba_quiet bit is set, then no error is issued and
   NULL_TREE is returned.  */

tree
lookup_base (tree t, tree base, base_access access, base_kind *kind_ptr)
{
  tree binfo;
  tree t_binfo;
  base_kind bk;

  if (t == error_mark_node || base == error_mark_node)
    {
      if (kind_ptr)
	*kind_ptr = bk_not_base;
      return error_mark_node;
    }
  gcc_assert (TYPE_P (base));

  if (!TYPE_P (t))
    {
      t_binfo = t;
      t = BINFO_TYPE (t);
    }
  else
    {
      t = complete_type (TYPE_MAIN_VARIANT (t));
      t_binfo = TYPE_BINFO (t);
    }

  base = complete_type (TYPE_MAIN_VARIANT (base));

  if (t_binfo)
    {
      struct lookup_base_data_s data;

      data.t = t;
      data.base = base;
      data.binfo = NULL_TREE;
      data.ambiguous = data.via_virtual = false;
      data.repeated_base = CLASSTYPE_REPEATED_BASE_P (t);
      data.want_any = access == ba_any;

      dfs_walk_once (t_binfo, dfs_lookup_base, NULL, &data);
      binfo = data.binfo;

      if (!binfo)
	bk = data.ambiguous ? bk_ambig : bk_not_base;
      else if (binfo == t_binfo)
	bk = bk_same_type;
      else if (data.via_virtual)
	bk = bk_via_virtual;
      else
	bk = bk_proper_base;
    }
  else
    {
      binfo = NULL_TREE;
      bk = bk_not_base;
    }

  /* Check that the base is unambiguous and accessible.  */
  if (access != ba_any)
    switch (bk)
      {
      case bk_not_base:
	break;

      case bk_ambig:
	if (!(access & ba_quiet))
	  {
	    error ("%qT is an ambiguous base of %qT", base, t);
	    binfo = error_mark_node;
	  }
	break;

      default:
	if ((access & ba_check_bit)
	    /* If BASE is incomplete, then BASE and TYPE are probably
	       the same, in which case BASE is accessible.  If they
	       are not the same, then TYPE is invalid.  In that case,
	       there's no need to issue another error here, and
	       there's no implicit typedef to use in the code that
	       follows, so we skip the check.  */
	    && COMPLETE_TYPE_P (base)
	    && !accessible_base_p (t, base, !(access & ba_ignore_scope)))
	  {
	    if (!(access & ba_quiet))
	      {
		error ("%qT is an inaccessible base of %qT", base, t);
		binfo = error_mark_node;
	      }
	    else
	      binfo = NULL_TREE;
	    bk = bk_inaccessible;
	  }
	break;
      }

  if (kind_ptr)
    *kind_ptr = bk;

  return binfo;
}

/* Data for dcast_base_hint walker.  */

struct dcast_data_s
{
  tree subtype;   /* The base type we're looking for.  */
  int virt_depth; /* Number of virtual bases encountered from most
		     derived.  */
  tree offset;    /* Best hint offset discovered so far.  */
  bool repeated_base;  /* Whether there are repeated bases in the
			  hierarchy.  */
};

/* Worker for dcast_base_hint.  Search for the base type being cast
   from.  */

static tree
dfs_dcast_hint_pre (tree binfo, void *data_)
{
  struct dcast_data_s *data = (struct dcast_data_s *) data_;

  if (BINFO_VIRTUAL_P (binfo))
    data->virt_depth++;

  if (SAME_BINFO_TYPE_P (BINFO_TYPE (binfo), data->subtype))
    {
      if (data->virt_depth)
	{
	  data->offset = ssize_int (-1);
	  return data->offset;
	}
      if (data->offset)
	data->offset = ssize_int (-3);
      else
	data->offset = BINFO_OFFSET (binfo);

      return data->repeated_base ? dfs_skip_bases : data->offset;
    }

  return NULL_TREE;
}

/* Worker for dcast_base_hint.  Track the virtual depth.  */

static tree
dfs_dcast_hint_post (tree binfo, void *data_)
{
  struct dcast_data_s *data = (struct dcast_data_s *) data_;

  if (BINFO_VIRTUAL_P (binfo))
    data->virt_depth--;

  return NULL_TREE;
}

/* The dynamic cast runtime needs a hint about how the static SUBTYPE type
   started from is related to the required TARGET type, in order to optimize
   the inheritance graph search. This information is independent of the
   current context, and ignores private paths, hence get_base_distance is
   inappropriate. Return a TREE specifying the base offset, BOFF.
   BOFF >= 0, there is only one public non-virtual SUBTYPE base at offset BOFF,
      and there are no public virtual SUBTYPE bases.
   BOFF == -1, SUBTYPE occurs as multiple public virtual or non-virtual bases.
   BOFF == -2, SUBTYPE is not a public base.
   BOFF == -3, SUBTYPE occurs as multiple public non-virtual bases.  */

tree
dcast_base_hint (tree subtype, tree target)
{
  struct dcast_data_s data;

  data.subtype = subtype;
  data.virt_depth = 0;
  data.offset = NULL_TREE;
  data.repeated_base = CLASSTYPE_REPEATED_BASE_P (target);

  dfs_walk_once_accessible (TYPE_BINFO (target), /*friends=*/false,
			    dfs_dcast_hint_pre, dfs_dcast_hint_post, &data);
  return data.offset ? data.offset : ssize_int (-2);
}

/* Search for a member with name NAME in a multiple inheritance
   lattice specified by TYPE.  If it does not exist, return NULL_TREE.
   If the member is ambiguously referenced, return `error_mark_node'.
   Otherwise, return a DECL with the indicated name.  If WANT_TYPE is
   true, type declarations are preferred.  */

/* Do a 1-level search for NAME as a member of TYPE.  The caller must
   figure out whether it can access this field.  (Since it is only one
   level, this is reasonable.)  */

tree
lookup_field_1 (tree type, tree name, bool want_type)
{
  tree field;

  if (TREE_CODE (type) == TEMPLATE_TYPE_PARM
      || TREE_CODE (type) == BOUND_TEMPLATE_TEMPLATE_PARM
      || TREE_CODE (type) == TYPENAME_TYPE)
    /* The TYPE_FIELDS of a TEMPLATE_TYPE_PARM and
       BOUND_TEMPLATE_TEMPLATE_PARM are not fields at all;
       instead TYPE_FIELDS is the TEMPLATE_PARM_INDEX.  (Miraculously,
       the code often worked even when we treated the index as a list
       of fields!)
       The TYPE_FIELDS of TYPENAME_TYPE is its TYPENAME_TYPE_FULLNAME.  */
    return NULL_TREE;

  if (TYPE_NAME (type)
      && DECL_LANG_SPECIFIC (TYPE_NAME (type))
      && DECL_SORTED_FIELDS (TYPE_NAME (type)))
    {
      tree *fields = &DECL_SORTED_FIELDS (TYPE_NAME (type))->elts[0];
      int lo = 0, hi = DECL_SORTED_FIELDS (TYPE_NAME (type))->len;
      int i;

      while (lo < hi)
	{
	  i = (lo + hi) / 2;

#ifdef GATHER_STATISTICS
	  n_fields_searched++;
#endif /* GATHER_STATISTICS */

	  if (DECL_NAME (fields[i]) > name)
	    hi = i;
	  else if (DECL_NAME (fields[i]) < name)
	    lo = i + 1;
	  else
	    {
	      field = NULL_TREE;

	      /* We might have a nested class and a field with the
		 same name; we sorted them appropriately via
		 field_decl_cmp, so just look for the first or last
		 field with this name.  */
	      if (want_type)
		{
		  do
		    field = fields[i--];
		  while (i >= lo && DECL_NAME (fields[i]) == name);
		  if (TREE_CODE (field) != TYPE_DECL
		      && !DECL_CLASS_TEMPLATE_P (field))
		    field = NULL_TREE;
		}
	      else
		{
		  do
		    field = fields[i++];
		  while (i < hi && DECL_NAME (fields[i]) == name);
		}
	      return field;
	    }
	}
      return NULL_TREE;
    }

  field = TYPE_FIELDS (type);

#ifdef GATHER_STATISTICS
  n_calls_lookup_field_1++;
#endif /* GATHER_STATISTICS */
  for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
    {
#ifdef GATHER_STATISTICS
      n_fields_searched++;
#endif /* GATHER_STATISTICS */
      gcc_assert (DECL_P (field));
      if (DECL_NAME (field) == NULL_TREE
	  && ANON_AGGR_TYPE_P (TREE_TYPE (field)))
	{
	  tree temp = lookup_field_1 (TREE_TYPE (field), name, want_type);
	  if (temp)
	    return temp;
	}
      if (TREE_CODE (field) == USING_DECL)
	{
	  /* We generally treat class-scope using-declarations as
	     ARM-style access specifications, because support for the
	     ISO semantics has not been implemented.  So, in general,
	     there's no reason to return a USING_DECL, and the rest of
	     the compiler cannot handle that.  Once the class is
	     defined, USING_DECLs are purged from TYPE_FIELDS; see
	     handle_using_decl.  However, we make special efforts to
	     make using-declarations in class templates and class
	     template partial specializations work correctly.  */
	  if (!DECL_DEPENDENT_P (field))
	    continue;
	}

      if (DECL_NAME (field) == name
	  && (!want_type
	      || TREE_CODE (field) == TYPE_DECL
	      || DECL_CLASS_TEMPLATE_P (field)))
	return field;
    }
  /* Not found.  */
  if (name == vptr_identifier)
    {
      /* Give the user what s/he thinks s/he wants.  */
      if (TYPE_POLYMORPHIC_P (type))
	return TYPE_VFIELD (type);
    }
  return NULL_TREE;
}

/* Return the FUNCTION_DECL, RECORD_TYPE, UNION_TYPE, or
   NAMESPACE_DECL corresponding to the innermost non-block scope.  */

tree
current_scope (void)
{
  /* There are a number of cases we need to be aware of here:
			 current_class_type	current_function_decl
     global			NULL			NULL
     fn-local			NULL			SET
     class-local		SET			NULL
     class->fn			SET			SET
     fn->class			SET			SET

     Those last two make life interesting.  If we're in a function which is
     itself inside a class, we need decls to go into the fn's decls (our
     second case below).  But if we're in a class and the class itself is
     inside a function, we need decls to go into the decls for the class.  To
     achieve this last goal, we must see if, when both current_class_ptr and
     current_function_decl are set, the class was declared inside that
     function.  If so, we know to put the decls into the class's scope.  */
  if (current_function_decl && current_class_type
      && ((DECL_FUNCTION_MEMBER_P (current_function_decl)
	   && same_type_p (DECL_CONTEXT (current_function_decl),
			   current_class_type))
	  || (DECL_FRIEND_CONTEXT (current_function_decl)
	      && same_type_p (DECL_FRIEND_CONTEXT (current_function_decl),
			      current_class_type))))
    return current_function_decl;
  if (current_class_type)
    return current_class_type;
  if (current_function_decl)
    return current_function_decl;
  return current_namespace;
}

/* Returns nonzero if we are currently in a function scope.  Note
   that this function returns zero if we are within a local class, but
   not within a member function body of the local class.  */

int
at_function_scope_p (void)
{
  tree cs = current_scope ();
  return cs && TREE_CODE (cs) == FUNCTION_DECL;
}

/* Returns true if the innermost active scope is a class scope.  */

bool
at_class_scope_p (void)
{
  tree cs = current_scope ();
  return cs && TYPE_P (cs);
}

/* Returns true if the innermost active scope is a namespace scope.  */

bool
at_namespace_scope_p (void)
{
  tree cs = current_scope ();
  return cs && TREE_CODE (cs) == NAMESPACE_DECL;
}

/* Return the scope of DECL, as appropriate when doing name-lookup.  */

tree
context_for_name_lookup (tree decl)
{
  /* [class.union]

     For the purposes of name lookup, after the anonymous union
     definition, the members of the anonymous union are considered to
     have been defined in the scope in which the anonymous union is
     declared.  */
  tree context = DECL_CONTEXT (decl);

  while (context && TYPE_P (context) && ANON_AGGR_TYPE_P (context))
    context = TYPE_CONTEXT (context);
  if (!context)
    context = global_namespace;

  return context;
}

/* The accessibility routines use BINFO_ACCESS for scratch space
   during the computation of the accessibility of some declaration.  */

#define BINFO_ACCESS(NODE) \
  ((access_kind) ((TREE_PUBLIC (NODE) << 1) | TREE_PRIVATE (NODE)))

/* Set the access associated with NODE to ACCESS.  */

#define SET_BINFO_ACCESS(NODE, ACCESS)			\
  ((TREE_PUBLIC (NODE) = ((ACCESS) & 2) != 0),	\
   (TREE_PRIVATE (NODE) = ((ACCESS) & 1) != 0))

/* Called from access_in_type via dfs_walk.  Calculate the access to
   DATA (which is really a DECL) in BINFO.  */

static tree
dfs_access_in_type (tree binfo, void *data)
{
  tree decl = (tree) data;
  tree type = BINFO_TYPE (binfo);
  access_kind access = ak_none;

  if (context_for_name_lookup (decl) == type)
    {
      /* If we have descended to the scope of DECL, just note the
	 appropriate access.  */
      if (TREE_PRIVATE (decl))
	access = ak_private;
      else if (TREE_PROTECTED (decl))
	access = ak_protected;
      else
	access = ak_public;
    }
  else
    {
      /* First, check for an access-declaration that gives us more
	 access to the DECL.  The CONST_DECL for an enumeration
	 constant will not have DECL_LANG_SPECIFIC, and thus no
	 DECL_ACCESS.  */
      if (DECL_LANG_SPECIFIC (decl) && !DECL_DISCRIMINATOR_P (decl))
	{
	  tree decl_access = purpose_member (type, DECL_ACCESS (decl));

	  if (decl_access)
	    {
	      decl_access = TREE_VALUE (decl_access);

	      if (decl_access == access_public_node)
		access = ak_public;
	      else if (decl_access == access_protected_node)
		access = ak_protected;
	      else if (decl_access == access_private_node)
		access = ak_private;
	      else
		gcc_unreachable ();
	    }
	}

      if (!access)
	{
	  int i;
	  tree base_binfo;
	  VEC(tree,gc) *accesses;

	  /* Otherwise, scan our baseclasses, and pick the most favorable
	     access.  */
	  accesses = BINFO_BASE_ACCESSES (binfo);
	  for (i = 0; BINFO_BASE_ITERATE (binfo, i, base_binfo); i++)
	    {
	      tree base_access = VEC_index (tree, accesses, i);
	      access_kind base_access_now = BINFO_ACCESS (base_binfo);

	      if (base_access_now == ak_none || base_access_now == ak_private)
		/* If it was not accessible in the base, or only
		   accessible as a private member, we can't access it
		   all.  */
		base_access_now = ak_none;
	      else if (base_access == access_protected_node)
		/* Public and protected members in the base become
		   protected here.  */
		base_access_now = ak_protected;
	      else if (base_access == access_private_node)
		/* Public and protected members in the base become
		   private here.  */
		base_access_now = ak_private;

	      /* See if the new access, via this base, gives more
		 access than our previous best access.  */
	      if (base_access_now != ak_none
		  && (access == ak_none || base_access_now < access))
		{
		  access = base_access_now;

		  /* If the new access is public, we can't do better.  */
		  if (access == ak_public)
		    break;
		}
	    }
	}
    }

  /* Note the access to DECL in TYPE.  */
  SET_BINFO_ACCESS (binfo, access);

  return NULL_TREE;
}

/* Return the access to DECL in TYPE.  */

static access_kind
access_in_type (tree type, tree decl)
{
  tree binfo = TYPE_BINFO (type);

  /* We must take into account

       [class.paths]

       If a name can be reached by several paths through a multiple
       inheritance graph, the access is that of the path that gives
       most access.

    The algorithm we use is to make a post-order depth-first traversal
    of the base-class hierarchy.  As we come up the tree, we annotate
    each node with the most lenient access.  */
  dfs_walk_once (binfo, NULL, dfs_access_in_type, decl);

  return BINFO_ACCESS (binfo);
}

/* Returns nonzero if it is OK to access DECL through an object
   indicated by BINFO in the context of DERIVED.  */

static int
protected_accessible_p (tree decl, tree derived, tree binfo)
{
  access_kind access;

  /* We're checking this clause from [class.access.base]

       m as a member of N is protected, and the reference occurs in a
       member or friend of class N, or in a member or friend of a
       class P derived from N, where m as a member of P is private or
       protected.

    Here DERIVED is a possible P and DECL is m.  accessible_p will
    iterate over various values of N, but the access to m in DERIVED
    does not change.

    Note that I believe that the passage above is wrong, and should read
    "...is private or protected or public"; otherwise you get bizarre results
    whereby a public using-decl can prevent you from accessing a protected
    member of a base.  (jason 2000/02/28)  */

  /* If DERIVED isn't derived from m's class, then it can't be a P.  */
  if (!DERIVED_FROM_P (context_for_name_lookup (decl), derived))
    return 0;

  access = access_in_type (derived, decl);

  /* If m is inaccessible in DERIVED, then it's not a P.  */
  if (access == ak_none)
    return 0;

  /* [class.protected]

     When a friend or a member function of a derived class references
     a protected nonstatic member of a base class, an access check
     applies in addition to those described earlier in clause
     _class.access_) Except when forming a pointer to member
     (_expr.unary.op_), the access must be through a pointer to,
     reference to, or object of the derived class itself (or any class
     derived from that class) (_expr.ref_).  If the access is to form
     a pointer to member, the nested-name-specifier shall name the
     derived class (or any class derived from that class).  */
  if (DECL_NONSTATIC_MEMBER_P (decl))
    {
      /* We can tell through what the reference is occurring by
	 chasing BINFO up to the root.  */
      tree t = binfo;
      while (BINFO_INHERITANCE_CHAIN (t))
	t = BINFO_INHERITANCE_CHAIN (t);

      if (!DERIVED_FROM_P (derived, BINFO_TYPE (t)))
	return 0;
    }

  return 1;
}

/* Returns nonzero if SCOPE is a friend of a type which would be able
   to access DECL through the object indicated by BINFO.  */

static int
friend_accessible_p (tree scope, tree decl, tree binfo)
{
  tree befriending_classes;
  tree t;

  if (!scope)
    return 0;

  if (TREE_CODE (scope) == FUNCTION_DECL
      || DECL_FUNCTION_TEMPLATE_P (scope))
    befriending_classes = DECL_BEFRIENDING_CLASSES (scope);
  else if (TYPE_P (scope))
    befriending_classes = CLASSTYPE_BEFRIENDING_CLASSES (scope);
  else
    return 0;

  for (t = befriending_classes; t; t = TREE_CHAIN (t))
    if (protected_accessible_p (decl, TREE_VALUE (t), binfo))
      return 1;

  /* Nested classes have the same access as their enclosing types, as
     per DR 45 (this is a change from the standard).  */
  if (TYPE_P (scope))
    for (t = TYPE_CONTEXT (scope); t && TYPE_P (t); t = TYPE_CONTEXT (t))
      if (protected_accessible_p (decl, t, binfo))
	return 1;

  if (TREE_CODE (scope) == FUNCTION_DECL
      || DECL_FUNCTION_TEMPLATE_P (scope))
    {
      /* Perhaps this SCOPE is a member of a class which is a
	 friend.  */
      if (DECL_CLASS_SCOPE_P (scope)
	  && friend_accessible_p (DECL_CONTEXT (scope), decl, binfo))
	return 1;

      /* Or an instantiation of something which is a friend.  */
      if (DECL_TEMPLATE_INFO (scope))
	{
	  int ret;
	  /* Increment processing_template_decl to make sure that
	     dependent_type_p works correctly.  */
	  ++processing_template_decl;
	  ret = friend_accessible_p (DECL_TI_TEMPLATE (scope), decl, binfo);
	  --processing_template_decl;
	  return ret;
	}
    }

  return 0;
}

/* Called via dfs_walk_once_accessible from accessible_p */

static tree
dfs_accessible_post (tree binfo, void *data ATTRIBUTE_UNUSED)
{
  if (BINFO_ACCESS (binfo) != ak_none)
    {
      tree scope = current_scope ();
      if (scope && TREE_CODE (scope) != NAMESPACE_DECL
	  && is_friend (BINFO_TYPE (binfo), scope))
	return binfo;
    }

  return NULL_TREE;
}

/* DECL is a declaration from a base class of TYPE, which was the
   class used to name DECL.  Return nonzero if, in the current
   context, DECL is accessible.  If TYPE is actually a BINFO node,
   then we can tell in what context the access is occurring by looking
   at the most derived class along the path indicated by BINFO.  If
   CONSIDER_LOCAL is true, do consider special access the current
   scope or friendship thereof we might have.  */

int
accessible_p (tree type, tree decl, bool consider_local_p)
{
  tree binfo;
  tree scope;
  access_kind access;

  /* Nonzero if it's OK to access DECL if it has protected
     accessibility in TYPE.  */
  int protected_ok = 0;

  /* If this declaration is in a block or namespace scope, there's no
     access control.  */
  if (!TYPE_P (context_for_name_lookup (decl)))
    return 1;

  /* There is no need to perform access checks inside a thunk.  */
  scope = current_scope ();
  if (scope && DECL_THUNK_P (scope))
    return 1;

  /* In a template declaration, we cannot be sure whether the
     particular specialization that is instantiated will be a friend
     or not.  Therefore, all access checks are deferred until
     instantiation.  However, PROCESSING_TEMPLATE_DECL is set in the
     parameter list for a template (because we may see dependent types
     in default arguments for template parameters), and access
     checking should be performed in the outermost parameter list.  */
  if (processing_template_decl
      && (!processing_template_parmlist || processing_template_decl > 1))
    return 1;

  if (!TYPE_P (type))
    {
      binfo = type;
      type = BINFO_TYPE (type);
    }
  else
    binfo = TYPE_BINFO (type);

  /* [class.access.base]

     A member m is accessible when named in class N if

     --m as a member of N is public, or

     --m as a member of N is private, and the reference occurs in a
       member or friend of class N, or

     --m as a member of N is protected, and the reference occurs in a
       member or friend of class N, or in a member or friend of a
       class P derived from N, where m as a member of P is private or
       protected, or

     --there exists a base class B of N that is accessible at the point
       of reference, and m is accessible when named in class B.

    We walk the base class hierarchy, checking these conditions.  */

  if (consider_local_p)
    {
      /* Figure out where the reference is occurring.  Check to see if
	 DECL is private or protected in this scope, since that will
	 determine whether protected access is allowed.  */
      if (current_class_type)
	protected_ok = protected_accessible_p (decl,
					       current_class_type, binfo);

      /* Now, loop through the classes of which we are a friend.  */
      if (!protected_ok)
	protected_ok = friend_accessible_p (scope, decl, binfo);
    }

  /* Standardize the binfo that access_in_type will use.  We don't
     need to know what path was chosen from this point onwards.  */
  binfo = TYPE_BINFO (type);

  /* Compute the accessibility of DECL in the class hierarchy
     dominated by type.  */
  access = access_in_type (type, decl);
  if (access == ak_public
      || (access == ak_protected && protected_ok))
    return 1;

  if (!consider_local_p)
    return 0;

  /* Walk the hierarchy again, looking for a base class that allows
     access.  */
  return dfs_walk_once_accessible (binfo, /*friends=*/true,
				   NULL, dfs_accessible_post, NULL)
    != NULL_TREE;
}

struct lookup_field_info {
  /* The type in which we're looking.  */
  tree type;
  /* The name of the field for which we're looking.  */
  tree name;
  /* If non-NULL, the current result of the lookup.  */
  tree rval;
  /* The path to RVAL.  */
  tree rval_binfo;
  /* If non-NULL, the lookup was ambiguous, and this is a list of the
     candidates.  */
  tree ambiguous;
  /* If nonzero, we are looking for types, not data members.  */
  int want_type;
  /* If something went wrong, a message indicating what.  */
  const char *errstr;
};

/* Within the scope of a template class, you can refer to the to the
   current specialization with the name of the template itself.  For
   example:

     template <typename T> struct S { S* sp; }

   Returns nonzero if DECL is such a declaration in a class TYPE.  */

static int
template_self_reference_p (tree type, tree decl)
{
  return  (CLASSTYPE_USE_TEMPLATE (type)
	   && PRIMARY_TEMPLATE_P (CLASSTYPE_TI_TEMPLATE (type))
	   && TREE_CODE (decl) == TYPE_DECL
	   && DECL_ARTIFICIAL (decl)
	   && DECL_NAME (decl) == constructor_name (type));
}

/* Nonzero for a class member means that it is shared between all objects
   of that class.

   [class.member.lookup]:If the resulting set of declarations are not all
   from sub-objects of the same type, or the set has a  nonstatic  member
   and  includes members from distinct sub-objects, there is an ambiguity
   and the program is ill-formed.

   This function checks that T contains no nonstatic members.  */

int
shared_member_p (tree t)
{
  if (TREE_CODE (t) == VAR_DECL || TREE_CODE (t) == TYPE_DECL \
      || TREE_CODE (t) == CONST_DECL)
    return 1;
  if (is_overloaded_fn (t))
    {
      for (; t; t = OVL_NEXT (t))
	{
	  tree fn = OVL_CURRENT (t);
	  if (DECL_NONSTATIC_MEMBER_FUNCTION_P (fn))
	    return 0;
	}
      return 1;
    }
  return 0;
}

/* Routine to see if the sub-object denoted by the binfo PARENT can be
   found as a base class and sub-object of the object denoted by
   BINFO.  */

static int
is_subobject_of_p (tree parent, tree binfo)
{
  tree probe;

  for (probe = parent; probe; probe = BINFO_INHERITANCE_CHAIN (probe))
    {
      if (probe == binfo)
	return 1;
      if (BINFO_VIRTUAL_P (probe))
	return (binfo_for_vbase (BINFO_TYPE (probe), BINFO_TYPE (binfo))
		!= NULL_TREE);
    }
  return 0;
}

/* DATA is really a struct lookup_field_info.  Look for a field with
   the name indicated there in BINFO.  If this function returns a
   non-NULL value it is the result of the lookup.  Called from
   lookup_field via breadth_first_search.  */

static tree
lookup_field_r (tree binfo, void *data)
{
  struct lookup_field_info *lfi = (struct lookup_field_info *) data;
  tree type = BINFO_TYPE (binfo);
  tree nval = NULL_TREE;

  /* If this is a dependent base, don't look in it.  */
  if (BINFO_DEPENDENT_BASE_P (binfo))
    return NULL_TREE;

  /* If this base class is hidden by the best-known value so far, we
     don't need to look.  */
  if (lfi->rval_binfo && BINFO_INHERITANCE_CHAIN (binfo) == lfi->rval_binfo
      && !BINFO_VIRTUAL_P (binfo))
    return dfs_skip_bases;

  /* First, look for a function.  There can't be a function and a data
     member with the same name, and if there's a function and a type
     with the same name, the type is hidden by the function.  */
  if (!lfi->want_type)
    {
      int idx = lookup_fnfields_1 (type, lfi->name);
      if (idx >= 0)
	nval = VEC_index (tree, CLASSTYPE_METHOD_VEC (type), idx);
    }

  if (!nval)
    /* Look for a data member or type.  */
    nval = lookup_field_1 (type, lfi->name, lfi->want_type);

  /* If there is no declaration with the indicated name in this type,
     then there's nothing to do.  */
  if (!nval)
    goto done;

  /* If we're looking up a type (as with an elaborated type specifier)
     we ignore all non-types we find.  */
  if (lfi->want_type && TREE_CODE (nval) != TYPE_DECL
      && !DECL_CLASS_TEMPLATE_P (nval))
    {
      if (lfi->name == TYPE_IDENTIFIER (type))
	{
	  /* If the aggregate has no user defined constructors, we allow
	     it to have fields with the same name as the enclosing type.
	     If we are looking for that name, find the corresponding
	     TYPE_DECL.  */
	  for (nval = TREE_CHAIN (nval); nval; nval = TREE_CHAIN (nval))
	    if (DECL_NAME (nval) == lfi->name
		&& TREE_CODE (nval) == TYPE_DECL)
	      break;
	}
      else
	nval = NULL_TREE;
      if (!nval && CLASSTYPE_NESTED_UTDS (type) != NULL)
	{
	  binding_entry e = binding_table_find (CLASSTYPE_NESTED_UTDS (type),
						lfi->name);
	  if (e != NULL)
	    nval = TYPE_MAIN_DECL (e->type);
	  else
	    goto done;
	}
    }

  /* You must name a template base class with a template-id.  */
  if (!same_type_p (type, lfi->type)
      && template_self_reference_p (type, nval))
    goto done;

  /* If the lookup already found a match, and the new value doesn't
     hide the old one, we might have an ambiguity.  */
  if (lfi->rval_binfo
      && !is_subobject_of_p (lfi->rval_binfo, binfo))

    {
      if (nval == lfi->rval && shared_member_p (nval))
	/* The two things are really the same.  */
	;
      else if (is_subobject_of_p (binfo, lfi->rval_binfo))
	/* The previous value hides the new one.  */
	;
      else
	{
	  /* We have a real ambiguity.  We keep a chain of all the
	     candidates.  */
	  if (!lfi->ambiguous && lfi->rval)
	    {
	      /* This is the first time we noticed an ambiguity.  Add
		 what we previously thought was a reasonable candidate
		 to the list.  */
	      lfi->ambiguous = tree_cons (NULL_TREE, lfi->rval, NULL_TREE);
	      TREE_TYPE (lfi->ambiguous) = error_mark_node;
	    }

	  /* Add the new value.  */
	  lfi->ambiguous = tree_cons (NULL_TREE, nval, lfi->ambiguous);
	  TREE_TYPE (lfi->ambiguous) = error_mark_node;
	  lfi->errstr = "request for member %qD is ambiguous";
	}
    }
  else
    {
      lfi->rval = nval;
      lfi->rval_binfo = binfo;
    }

 done:
  /* Don't look for constructors or destructors in base classes.  */
  if (IDENTIFIER_CTOR_OR_DTOR_P (lfi->name))
    return dfs_skip_bases;
  return NULL_TREE;
}

/* Return a "baselink" with BASELINK_BINFO, BASELINK_ACCESS_BINFO,
   BASELINK_FUNCTIONS, and BASELINK_OPTYPE set to BINFO, ACCESS_BINFO,
   FUNCTIONS, and OPTYPE respectively.  */

tree
build_baselink (tree binfo, tree access_binfo, tree functions, tree optype)
{
  tree baselink;

  gcc_assert (TREE_CODE (functions) == FUNCTION_DECL
	      || TREE_CODE (functions) == TEMPLATE_DECL
	      || TREE_CODE (functions) == TEMPLATE_ID_EXPR
	      || TREE_CODE (functions) == OVERLOAD);
  gcc_assert (!optype || TYPE_P (optype));
  gcc_assert (TREE_TYPE (functions));

  baselink = make_node (BASELINK);
  TREE_TYPE (baselink) = TREE_TYPE (functions);
  BASELINK_BINFO (baselink) = binfo;
  BASELINK_ACCESS_BINFO (baselink) = access_binfo;
  BASELINK_FUNCTIONS (baselink) = functions;
  BASELINK_OPTYPE (baselink) = optype;

  return baselink;
}

/* Look for a member named NAME in an inheritance lattice dominated by
   XBASETYPE.  If PROTECT is 0 or two, we do not check access.  If it
   is 1, we enforce accessibility.  If PROTECT is zero, then, for an
   ambiguous lookup, we return NULL.  If PROTECT is 1, we issue error
   messages about inaccessible or ambiguous lookup.  If PROTECT is 2,
   we return a TREE_LIST whose TREE_TYPE is error_mark_node and whose
   TREE_VALUEs are the list of ambiguous candidates.

   WANT_TYPE is 1 when we should only return TYPE_DECLs.

   If nothing can be found return NULL_TREE and do not issue an error.  */

tree
lookup_member (tree xbasetype, tree name, int protect, bool want_type)
{
  tree rval, rval_binfo = NULL_TREE;
  tree type = NULL_TREE, basetype_path = NULL_TREE;
  struct lookup_field_info lfi;

  /* rval_binfo is the binfo associated with the found member, note,
     this can be set with useful information, even when rval is not
     set, because it must deal with ALL members, not just non-function
     members.  It is used for ambiguity checking and the hidden
     checks.  Whereas rval is only set if a proper (not hidden)
     non-function member is found.  */

  const char *errstr = 0;

  gcc_assert (TREE_CODE (name) == IDENTIFIER_NODE);

  if (TREE_CODE (xbasetype) == TREE_BINFO)
    {
      type = BINFO_TYPE (xbasetype);
      basetype_path = xbasetype;
    }
  else
    {
      if (!IS_AGGR_TYPE_CODE (TREE_CODE (xbasetype)))
	return NULL_TREE;
      type = xbasetype;
      xbasetype = NULL_TREE;
    }

  type = complete_type (type);
  if (!basetype_path)
    basetype_path = TYPE_BINFO (type);

  if (!basetype_path)
    return NULL_TREE;

#ifdef GATHER_STATISTICS
  n_calls_lookup_field++;
#endif /* GATHER_STATISTICS */

  memset (&lfi, 0, sizeof (lfi));
  lfi.type = type;
  lfi.name = name;
  lfi.want_type = want_type;
  dfs_walk_all (basetype_path, &lookup_field_r, NULL, &lfi);
  rval = lfi.rval;
  rval_binfo = lfi.rval_binfo;
  if (rval_binfo)
    type = BINFO_TYPE (rval_binfo);
  errstr = lfi.errstr;

  /* If we are not interested in ambiguities, don't report them;
     just return NULL_TREE.  */
  if (!protect && lfi.ambiguous)
    return NULL_TREE;

  if (protect == 2)
    {
      if (lfi.ambiguous)
	return lfi.ambiguous;
      else
	protect = 0;
    }

  /* [class.access]

     In the case of overloaded function names, access control is
     applied to the function selected by overloaded resolution.  

     We cannot check here, even if RVAL is only a single non-static
     member function, since we do not know what the "this" pointer
     will be.  For:

        class A { protected: void f(); };
        class B : public A { 
          void g(A *p) {
            f(); // OK
            p->f(); // Not OK.
          }
        };

    only the first call to "f" is valid.  However, if the function is
    static, we can check.  */
  if (rval && protect 
      && !really_overloaded_fn (rval)
      && !(TREE_CODE (rval) == FUNCTION_DECL
	   && DECL_NONSTATIC_MEMBER_FUNCTION_P (rval)))
    perform_or_defer_access_check (basetype_path, rval, rval);

  if (errstr && protect)
    {
      error (errstr, name, type);
      if (lfi.ambiguous)
	print_candidates (lfi.ambiguous);
      rval = error_mark_node;
    }

  if (rval && is_overloaded_fn (rval))
    rval = build_baselink (rval_binfo, basetype_path, rval,
			   (IDENTIFIER_TYPENAME_P (name)
			   ? TREE_TYPE (name): NULL_TREE));
  return rval;
}

/* Like lookup_member, except that if we find a function member we
   return NULL_TREE.  */

tree
lookup_field (tree xbasetype, tree name, int protect, bool want_type)
{
  tree rval = lookup_member (xbasetype, name, protect, want_type);

  /* Ignore functions, but propagate the ambiguity list.  */
  if (!error_operand_p (rval)
      && (rval && BASELINK_P (rval)))
    return NULL_TREE;

  return rval;
}

/* Like lookup_member, except that if we find a non-function member we
   return NULL_TREE.  */

tree
lookup_fnfields (tree xbasetype, tree name, int protect)
{
  tree rval = lookup_member (xbasetype, name, protect, /*want_type=*/false);

  /* Ignore non-functions, but propagate the ambiguity list.  */
  if (!error_operand_p (rval)
      && (rval && !BASELINK_P (rval)))
    return NULL_TREE;

  return rval;
}

/* Return the index in the CLASSTYPE_METHOD_VEC for CLASS_TYPE
   corresponding to "operator TYPE ()", or -1 if there is no such
   operator.  Only CLASS_TYPE itself is searched; this routine does
   not scan the base classes of CLASS_TYPE.  */

static int
lookup_conversion_operator (tree class_type, tree type)
{
  int tpl_slot = -1;

  if (TYPE_HAS_CONVERSION (class_type))
    {
      int i;
      tree fn;
      VEC(tree,gc) *methods = CLASSTYPE_METHOD_VEC (class_type);

      for (i = CLASSTYPE_FIRST_CONVERSION_SLOT;
	   VEC_iterate (tree, methods, i, fn); ++i)
	{
	  /* All the conversion operators come near the beginning of
	     the class.  Therefore, if FN is not a conversion
	     operator, there is no matching conversion operator in
	     CLASS_TYPE.  */
	  fn = OVL_CURRENT (fn);
	  if (!DECL_CONV_FN_P (fn))
	    break;

	  if (TREE_CODE (fn) == TEMPLATE_DECL)
	    /* All the templated conversion functions are on the same
	       slot, so remember it.  */
	    tpl_slot = i;
	  else if (same_type_p (DECL_CONV_FN_TYPE (fn), type))
	    return i;
	}
    }

  return tpl_slot;
}

/* TYPE is a class type. Return the index of the fields within
   the method vector with name NAME, or -1 is no such field exists.  */

int
lookup_fnfields_1 (tree type, tree name)
{
  VEC(tree,gc) *method_vec;
  tree fn;
  tree tmp;
  size_t i;

  if (!CLASS_TYPE_P (type))
    return -1;

  if (COMPLETE_TYPE_P (type))
    {
      if ((name == ctor_identifier
	   || name == base_ctor_identifier
	   || name == complete_ctor_identifier))
	{
	  if (CLASSTYPE_LAZY_DEFAULT_CTOR (type))
	    lazily_declare_fn (sfk_constructor, type);
	  if (CLASSTYPE_LAZY_COPY_CTOR (type))
	    lazily_declare_fn (sfk_copy_constructor, type);
	}
      else if (name == ansi_assopname(NOP_EXPR)
	       && CLASSTYPE_LAZY_ASSIGNMENT_OP (type))
	lazily_declare_fn (sfk_assignment_operator, type);
      else if ((name == dtor_identifier
		|| name == base_dtor_identifier
		|| name == complete_dtor_identifier
		|| name == deleting_dtor_identifier)
	       && CLASSTYPE_LAZY_DESTRUCTOR (type))
	lazily_declare_fn (sfk_destructor, type);
    }

  method_vec = CLASSTYPE_METHOD_VEC (type);
  if (!method_vec)
    return -1;

#ifdef GATHER_STATISTICS
  n_calls_lookup_fnfields_1++;
#endif /* GATHER_STATISTICS */

  /* Constructors are first...  */
  if (name == ctor_identifier)
    {
      fn = CLASSTYPE_CONSTRUCTORS (type);
      return fn ? CLASSTYPE_CONSTRUCTOR_SLOT : -1;
    }
  /* and destructors are second.  */
  if (name == dtor_identifier)
    {
      fn = CLASSTYPE_DESTRUCTORS (type);
      return fn ? CLASSTYPE_DESTRUCTOR_SLOT : -1;
    }
  if (IDENTIFIER_TYPENAME_P (name))
    return lookup_conversion_operator (type, TREE_TYPE (name));

  /* Skip the conversion operators.  */
  for (i = CLASSTYPE_FIRST_CONVERSION_SLOT;
       VEC_iterate (tree, method_vec, i, fn);
       ++i)
    if (!DECL_CONV_FN_P (OVL_CURRENT (fn)))
      break;

  /* If the type is complete, use binary search.  */
  if (COMPLETE_TYPE_P (type))
    {
      int lo;
      int hi;

      lo = i;
      hi = VEC_length (tree, method_vec);
      while (lo < hi)
	{
	  i = (lo + hi) / 2;

#ifdef GATHER_STATISTICS
	  n_outer_fields_searched++;
#endif /* GATHER_STATISTICS */

	  tmp = VEC_index (tree, method_vec, i);
	  tmp = DECL_NAME (OVL_CURRENT (tmp));
	  if (tmp > name)
	    hi = i;
	  else if (tmp < name)
	    lo = i + 1;
	  else
	    return i;
	}
    }
  else
    for (; VEC_iterate (tree, method_vec, i, fn); ++i)
      {
#ifdef GATHER_STATISTICS
	n_outer_fields_searched++;
#endif /* GATHER_STATISTICS */
	if (DECL_NAME (OVL_CURRENT (fn)) == name)
	  return i;
      }

  return -1;
}

/* Like lookup_fnfields_1, except that the name is extracted from
   FUNCTION, which is a FUNCTION_DECL or a TEMPLATE_DECL.  */

int
class_method_index_for_fn (tree class_type, tree function)
{
  gcc_assert (TREE_CODE (function) == FUNCTION_DECL
	      || DECL_FUNCTION_TEMPLATE_P (function));

  return lookup_fnfields_1 (class_type,
			    DECL_CONSTRUCTOR_P (function) ? ctor_identifier :
			    DECL_DESTRUCTOR_P (function) ? dtor_identifier :
			    DECL_NAME (function));
}


/* DECL is the result of a qualified name lookup.  QUALIFYING_SCOPE is
   the class or namespace used to qualify the name.  CONTEXT_CLASS is
   the class corresponding to the object in which DECL will be used.
   Return a possibly modified version of DECL that takes into account
   the CONTEXT_CLASS.

   In particular, consider an expression like `B::m' in the context of
   a derived class `D'.  If `B::m' has been resolved to a BASELINK,
   then the most derived class indicated by the BASELINK_BINFO will be
   `B', not `D'.  This function makes that adjustment.  */

tree
adjust_result_of_qualified_name_lookup (tree decl,
					tree qualifying_scope,
					tree context_class)
{
  if (context_class && context_class != error_mark_node
      && CLASS_TYPE_P (context_class)
      && CLASS_TYPE_P (qualifying_scope)
      && DERIVED_FROM_P (qualifying_scope, context_class)
      && BASELINK_P (decl))
    {
      tree base;

      /* Look for the QUALIFYING_SCOPE as a base of the CONTEXT_CLASS.
	 Because we do not yet know which function will be chosen by
	 overload resolution, we cannot yet check either accessibility
	 or ambiguity -- in either case, the choice of a static member
	 function might make the usage valid.  */
      base = lookup_base (context_class, qualifying_scope,
			  ba_unique | ba_quiet, NULL);
      if (base)
	{
	  BASELINK_ACCESS_BINFO (decl) = base;
	  BASELINK_BINFO (decl)
	    = lookup_base (base, BINFO_TYPE (BASELINK_BINFO (decl)),
			   ba_unique | ba_quiet,
			   NULL);
	}
    }

  return decl;
}


/* Walk the class hierarchy within BINFO, in a depth-first traversal.
   PRE_FN is called in preorder, while POST_FN is called in postorder.
   If PRE_FN returns DFS_SKIP_BASES, child binfos will not be
   walked.  If PRE_FN or POST_FN returns a different non-NULL value,
   that value is immediately returned and the walk is terminated.  One
   of PRE_FN and POST_FN can be NULL.  At each node, PRE_FN and
   POST_FN are passed the binfo to examine and the caller's DATA
   value.  All paths are walked, thus virtual and morally virtual
   binfos can be multiply walked.  */

tree
dfs_walk_all (tree binfo, tree (*pre_fn) (tree, void *),
	      tree (*post_fn) (tree, void *), void *data)
{
  tree rval;
  unsigned ix;
  tree base_binfo;

  /* Call the pre-order walking function.  */
  if (pre_fn)
    {
      rval = pre_fn (binfo, data);
      if (rval)
	{
	  if (rval == dfs_skip_bases)
	    goto skip_bases;
	  return rval;
	}
    }

  /* Find the next child binfo to walk.  */
  for (ix = 0; BINFO_BASE_ITERATE (binfo, ix, base_binfo); ix++)
    {
      rval = dfs_walk_all (base_binfo, pre_fn, post_fn, data);
      if (rval)
	return rval;
    }

 skip_bases:
  /* Call the post-order walking function.  */
  if (post_fn)
    {
      rval = post_fn (binfo, data);
      gcc_assert (rval != dfs_skip_bases);
      return rval;
    }

  return NULL_TREE;
}

/* Worker for dfs_walk_once.  This behaves as dfs_walk_all, except
   that binfos are walked at most once.  */

static tree
dfs_walk_once_r (tree binfo, tree (*pre_fn) (tree, void *),
		 tree (*post_fn) (tree, void *), void *data)
{
  tree rval;
  unsigned ix;
  tree base_binfo;

  /* Call the pre-order walking function.  */
  if (pre_fn)
    {
      rval = pre_fn (binfo, data);
      if (rval)
	{
	  if (rval == dfs_skip_bases)
	    goto skip_bases;

	  return rval;
	}
    }

  /* Find the next child binfo to walk.  */
  for (ix = 0; BINFO_BASE_ITERATE (binfo, ix, base_binfo); ix++)
    {
      if (BINFO_VIRTUAL_P (base_binfo))
	{
	  if (BINFO_MARKED (base_binfo))
	    continue;
	  BINFO_MARKED (base_binfo) = 1;
	}

      rval = dfs_walk_once_r (base_binfo, pre_fn, post_fn, data);
      if (rval)
	return rval;
    }

 skip_bases:
  /* Call the post-order walking function.  */
  if (post_fn)
    {
      rval = post_fn (binfo, data);
      gcc_assert (rval != dfs_skip_bases);
      return rval;
    }

  return NULL_TREE;
}

/* Worker for dfs_walk_once. Recursively unmark the virtual base binfos of
   BINFO.  */

static void
dfs_unmark_r (tree binfo)
{
  unsigned ix;
  tree base_binfo;

  /* Process the basetypes.  */
  for (ix = 0; BINFO_BASE_ITERATE (binfo, ix, base_binfo); ix++)
    {
      if (BINFO_VIRTUAL_P (base_binfo))
	{
	  if (!BINFO_MARKED (base_binfo))
	    continue;
	  BINFO_MARKED (base_binfo) = 0;
	}
      /* Only walk, if it can contain more virtual bases.  */
      if (CLASSTYPE_VBASECLASSES (BINFO_TYPE (base_binfo)))
	dfs_unmark_r (base_binfo);
    }
}

/* Like dfs_walk_all, except that binfos are not multiply walked.  For
   non-diamond shaped hierarchies this is the same as dfs_walk_all.
   For diamond shaped hierarchies we must mark the virtual bases, to
   avoid multiple walks.  */

tree
dfs_walk_once (tree binfo, tree (*pre_fn) (tree, void *),
	       tree (*post_fn) (tree, void *), void *data)
{
  static int active = 0;  /* We must not be called recursively. */
  tree rval;

  gcc_assert (pre_fn || post_fn);
  gcc_assert (!active);
  active++;

  if (!CLASSTYPE_DIAMOND_SHAPED_P (BINFO_TYPE (binfo)))
    /* We are not diamond shaped, and therefore cannot encounter the
       same binfo twice.  */
    rval = dfs_walk_all (binfo, pre_fn, post_fn, data);
  else
    {
      rval = dfs_walk_once_r (binfo, pre_fn, post_fn, data);
      if (!BINFO_INHERITANCE_CHAIN (binfo))
	{
	  /* We are at the top of the hierarchy, and can use the
	     CLASSTYPE_VBASECLASSES list for unmarking the virtual
	     bases.  */
	  VEC(tree,gc) *vbases;
	  unsigned ix;
	  tree base_binfo;

	  for (vbases = CLASSTYPE_VBASECLASSES (BINFO_TYPE (binfo)), ix = 0;
	       VEC_iterate (tree, vbases, ix, base_binfo); ix++)
	    BINFO_MARKED (base_binfo) = 0;
	}
      else
	dfs_unmark_r (binfo);
    }

  active--;

  return rval;
}

/* Worker function for dfs_walk_once_accessible.  Behaves like
   dfs_walk_once_r, except (a) FRIENDS_P is true if special
   access given by the current context should be considered, (b) ONCE
   indicates whether bases should be marked during traversal.  */

static tree
dfs_walk_once_accessible_r (tree binfo, bool friends_p, bool once,
			    tree (*pre_fn) (tree, void *),
			    tree (*post_fn) (tree, void *), void *data)
{
  tree rval = NULL_TREE;
  unsigned ix;
  tree base_binfo;

  /* Call the pre-order walking function.  */
  if (pre_fn)
    {
      rval = pre_fn (binfo, data);
      if (rval)
	{
	  if (rval == dfs_skip_bases)
	    goto skip_bases;

	  return rval;
	}
    }

  /* Find the next child binfo to walk.  */
  for (ix = 0; BINFO_BASE_ITERATE (binfo, ix, base_binfo); ix++)
    {
      bool mark = once && BINFO_VIRTUAL_P (base_binfo);

      if (mark && BINFO_MARKED (base_binfo))
	continue;

      /* If the base is inherited via private or protected
	 inheritance, then we can't see it, unless we are a friend of
	 the current binfo.  */
      if (BINFO_BASE_ACCESS (binfo, ix) != access_public_node)
	{
	  tree scope;
	  if (!friends_p)
	    continue;
	  scope = current_scope ();
	  if (!scope
	      || TREE_CODE (scope) == NAMESPACE_DECL
	      || !is_friend (BINFO_TYPE (binfo), scope))
	    continue;
	}

      if (mark)
	BINFO_MARKED (base_binfo) = 1;

      rval = dfs_walk_once_accessible_r (base_binfo, friends_p, once,
					 pre_fn, post_fn, data);
      if (rval)
	return rval;
    }

 skip_bases:
  /* Call the post-order walking function.  */
  if (post_fn)
    {
      rval = post_fn (binfo, data);
      gcc_assert (rval != dfs_skip_bases);
      return rval;
    }

  return NULL_TREE;
}

/* Like dfs_walk_once except that only accessible bases are walked.
   FRIENDS_P indicates whether friendship of the local context
   should be considered when determining accessibility.  */

static tree
dfs_walk_once_accessible (tree binfo, bool friends_p,
			    tree (*pre_fn) (tree, void *),
			    tree (*post_fn) (tree, void *), void *data)
{
  bool diamond_shaped = CLASSTYPE_DIAMOND_SHAPED_P (BINFO_TYPE (binfo));
  tree rval = dfs_walk_once_accessible_r (binfo, friends_p, diamond_shaped,
					  pre_fn, post_fn, data);

  if (diamond_shaped)
    {
      if (!BINFO_INHERITANCE_CHAIN (binfo))
	{
	  /* We are at the top of the hierarchy, and can use the
	     CLASSTYPE_VBASECLASSES list for unmarking the virtual
	     bases.  */
	  VEC(tree,gc) *vbases;
	  unsigned ix;
	  tree base_binfo;

	  for (vbases = CLASSTYPE_VBASECLASSES (BINFO_TYPE (binfo)), ix = 0;
	       VEC_iterate (tree, vbases, ix, base_binfo); ix++)
	    BINFO_MARKED (base_binfo) = 0;
	}
      else
	dfs_unmark_r (binfo);
    }
  return rval;
}

/* Check that virtual overrider OVERRIDER is acceptable for base function
   BASEFN. Issue diagnostic, and return zero, if unacceptable.  */

static int
check_final_overrider (tree overrider, tree basefn)
{
  tree over_type = TREE_TYPE (overrider);
  tree base_type = TREE_TYPE (basefn);
  tree over_return = TREE_TYPE (over_type);
  tree base_return = TREE_TYPE (base_type);
  tree over_throw = TYPE_RAISES_EXCEPTIONS (over_type);
  tree base_throw = TYPE_RAISES_EXCEPTIONS (base_type);
  int fail = 0;

  if (DECL_INVALID_OVERRIDER_P (overrider))
    return 0;

  if (same_type_p (base_return, over_return))
    /* OK */;
  else if ((CLASS_TYPE_P (over_return) && CLASS_TYPE_P (base_return))
	   || (TREE_CODE (base_return) == TREE_CODE (over_return)
	       && POINTER_TYPE_P (base_return)))
    {
      /* Potentially covariant.  */
      unsigned base_quals, over_quals;

      fail = !POINTER_TYPE_P (base_return);
      if (!fail)
	{
	  fail = cp_type_quals (base_return) != cp_type_quals (over_return);

	  base_return = TREE_TYPE (base_return);
	  over_return = TREE_TYPE (over_return);
	}
      base_quals = cp_type_quals (base_return);
      over_quals = cp_type_quals (over_return);

      if ((base_quals & over_quals) != over_quals)
	fail = 1;

      if (CLASS_TYPE_P (base_return) && CLASS_TYPE_P (over_return))
	{
	  tree binfo = lookup_base (over_return, base_return,
				    ba_check | ba_quiet, NULL);

	  if (!binfo)
	    fail = 1;
	}
      else if (!pedantic
	       && can_convert (TREE_TYPE (base_type), TREE_TYPE (over_type)))
	/* GNU extension, allow trivial pointer conversions such as
	   converting to void *, or qualification conversion.  */
	{
	  /* can_convert will permit user defined conversion from a
	     (reference to) class type. We must reject them.  */
	  over_return = non_reference (TREE_TYPE (over_type));
	  if (CLASS_TYPE_P (over_return))
	    fail = 2;
	  else
	    {
	      warning (0, "deprecated covariant return type for %q+#D",
			     overrider);
	      warning (0, "  overriding %q+#D", basefn);
	    }
	}
      else
	fail = 2;
    }
  else
    fail = 2;
  if (!fail)
    /* OK */;
  else
    {
      if (fail == 1)
	{
	  error ("invalid covariant return type for %q+#D", overrider);
	  error ("  overriding %q+#D", basefn);
	}
      else
	{
	  error ("conflicting return type specified for %q+#D", overrider);
	  error ("  overriding %q+#D", basefn);
	}
      DECL_INVALID_OVERRIDER_P (overrider) = 1;
      return 0;
    }

  /* Check throw specifier is at least as strict.  */
  if (!comp_except_specs (base_throw, over_throw, 0))
    {
      error ("looser throw specifier for %q+#F", overrider);
      error ("  overriding %q+#F", basefn);
      DECL_INVALID_OVERRIDER_P (overrider) = 1;
      return 0;
    }

  return 1;
}

/* Given a class TYPE, and a function decl FNDECL, look for
   virtual functions in TYPE's hierarchy which FNDECL overrides.
   We do not look in TYPE itself, only its bases.

   Returns nonzero, if we find any. Set FNDECL's DECL_VIRTUAL_P, if we
   find that it overrides anything.

   We check that every function which is overridden, is correctly
   overridden.  */

int
look_for_overrides (tree type, tree fndecl)
{
  tree binfo = TYPE_BINFO (type);
  tree base_binfo;
  int ix;
  int found = 0;

  for (ix = 0; BINFO_BASE_ITERATE (binfo, ix, base_binfo); ix++)
    {
      tree basetype = BINFO_TYPE (base_binfo);

      if (TYPE_POLYMORPHIC_P (basetype))
	found += look_for_overrides_r (basetype, fndecl);
    }
  return found;
}

/* Look in TYPE for virtual functions with the same signature as
   FNDECL.  */

tree
look_for_overrides_here (tree type, tree fndecl)
{
  int ix;

  /* If there are no methods in TYPE (meaning that only implicitly
     declared methods will ever be provided for TYPE), then there are
     no virtual functions.  */
  if (!CLASSTYPE_METHOD_VEC (type))
    return NULL_TREE;

  if (DECL_MAYBE_IN_CHARGE_DESTRUCTOR_P (fndecl))
    ix = CLASSTYPE_DESTRUCTOR_SLOT;
  else
    ix = lookup_fnfields_1 (type, DECL_NAME (fndecl));
  if (ix >= 0)
    {
      tree fns = VEC_index (tree, CLASSTYPE_METHOD_VEC (type), ix);

      for (; fns; fns = OVL_NEXT (fns))
	{
	  tree fn = OVL_CURRENT (fns);

	  if (!DECL_VIRTUAL_P (fn))
	    /* Not a virtual.  */;
	  else if (DECL_CONTEXT (fn) != type)
	    /* Introduced with a using declaration.  */;
	  else if (DECL_STATIC_FUNCTION_P (fndecl))
	    {
	      tree btypes = TYPE_ARG_TYPES (TREE_TYPE (fn));
	      tree dtypes = TYPE_ARG_TYPES (TREE_TYPE (fndecl));
	      if (compparms (TREE_CHAIN (btypes), dtypes))
		return fn;
	    }
	  else if (same_signature_p (fndecl, fn))
	    return fn;
	}
    }
  return NULL_TREE;
}

/* Look in TYPE for virtual functions overridden by FNDECL. Check both
   TYPE itself and its bases.  */

static int
look_for_overrides_r (tree type, tree fndecl)
{
  tree fn = look_for_overrides_here (type, fndecl);
  if (fn)
    {
      if (DECL_STATIC_FUNCTION_P (fndecl))
	{
	  /* A static member function cannot match an inherited
	     virtual member function.  */
	  error ("%q+#D cannot be declared", fndecl);
	  error ("  since %q+#D declared in base class", fn);
	}
      else
	{
	  /* It's definitely virtual, even if not explicitly set.  */
	  DECL_VIRTUAL_P (fndecl) = 1;
	  check_final_overrider (fndecl, fn);
	}
      return 1;
    }

  /* We failed to find one declared in this class. Look in its bases.  */
  return look_for_overrides (type, fndecl);
}

/* Called via dfs_walk from dfs_get_pure_virtuals.  */

static tree
dfs_get_pure_virtuals (tree binfo, void *data)
{
  tree type = (tree) data;

  /* We're not interested in primary base classes; the derived class
     of which they are a primary base will contain the information we
     need.  */
  if (!BINFO_PRIMARY_P (binfo))
    {
      tree virtuals;

      for (virtuals = BINFO_VIRTUALS (binfo);
	   virtuals;
	   virtuals = TREE_CHAIN (virtuals))
	if (DECL_PURE_VIRTUAL_P (BV_FN (virtuals)))
	  VEC_safe_push (tree, gc, CLASSTYPE_PURE_VIRTUALS (type),
			 BV_FN (virtuals));
    }

  return NULL_TREE;
}

/* Set CLASSTYPE_PURE_VIRTUALS for TYPE.  */

void
get_pure_virtuals (tree type)
{
  /* Clear the CLASSTYPE_PURE_VIRTUALS list; whatever is already there
     is going to be overridden.  */
  CLASSTYPE_PURE_VIRTUALS (type) = NULL;
  /* Now, run through all the bases which are not primary bases, and
     collect the pure virtual functions.  We look at the vtable in
     each class to determine what pure virtual functions are present.
     (A primary base is not interesting because the derived class of
     which it is a primary base will contain vtable entries for the
     pure virtuals in the base class.  */
  dfs_walk_once (TYPE_BINFO (type), NULL, dfs_get_pure_virtuals, type);
}

/* Debug info for C++ classes can get very large; try to avoid
   emitting it everywhere.

   Note that this optimization wins even when the target supports
   BINCL (if only slightly), and reduces the amount of work for the
   linker.  */

void
maybe_suppress_debug_info (tree t)
{
  if (write_symbols == NO_DEBUG)
    return;

  /* We might have set this earlier in cp_finish_decl.  */
  TYPE_DECL_SUPPRESS_DEBUG (TYPE_MAIN_DECL (t)) = 0;

  /* Always emit the information for each class every time. */
  if (flag_emit_class_debug_always)
    return;

  /* If we already know how we're handling this class, handle debug info
     the same way.  */
  if (CLASSTYPE_INTERFACE_KNOWN (t))
    {
      if (CLASSTYPE_INTERFACE_ONLY (t))
	TYPE_DECL_SUPPRESS_DEBUG (TYPE_MAIN_DECL (t)) = 1;
      /* else don't set it.  */
    }
  /* If the class has a vtable, write out the debug info along with
     the vtable.  */
  else if (TYPE_CONTAINS_VPTR_P (t))
    TYPE_DECL_SUPPRESS_DEBUG (TYPE_MAIN_DECL (t)) = 1;

  /* Otherwise, just emit the debug info normally.  */
}

/* Note that we want debugging information for a base class of a class
   whose vtable is being emitted.  Normally, this would happen because
   calling the constructor for a derived class implies calling the
   constructors for all bases, which involve initializing the
   appropriate vptr with the vtable for the base class; but in the
   presence of optimization, this initialization may be optimized
   away, so we tell finish_vtable_vardecl that we want the debugging
   information anyway.  */

static tree
dfs_debug_mark (tree binfo, void *data ATTRIBUTE_UNUSED)
{
  tree t = BINFO_TYPE (binfo);

  if (CLASSTYPE_DEBUG_REQUESTED (t))
    return dfs_skip_bases;

  CLASSTYPE_DEBUG_REQUESTED (t) = 1;

  return NULL_TREE;
}

/* Write out the debugging information for TYPE, whose vtable is being
   emitted.  Also walk through our bases and note that we want to
   write out information for them.  This avoids the problem of not
   writing any debug info for intermediate basetypes whose
   constructors, and thus the references to their vtables, and thus
   the vtables themselves, were optimized away.  */

void
note_debug_info_needed (tree type)
{
  if (TYPE_DECL_SUPPRESS_DEBUG (TYPE_NAME (type)))
    {
      TYPE_DECL_SUPPRESS_DEBUG (TYPE_NAME (type)) = 0;
      rest_of_type_compilation (type, toplevel_bindings_p ());
    }

  dfs_walk_all (TYPE_BINFO (type), dfs_debug_mark, NULL, 0);
}

void
print_search_statistics (void)
{
#ifdef GATHER_STATISTICS
  fprintf (stderr, "%d fields searched in %d[%d] calls to lookup_field[_1]\n",
	   n_fields_searched, n_calls_lookup_field, n_calls_lookup_field_1);
  fprintf (stderr, "%d fnfields searched in %d calls to lookup_fnfields\n",
	   n_outer_fields_searched, n_calls_lookup_fnfields);
  fprintf (stderr, "%d calls to get_base_type\n", n_calls_get_base_type);
#else /* GATHER_STATISTICS */
  fprintf (stderr, "no search statistics\n");
#endif /* GATHER_STATISTICS */
}

void
reinit_search_statistics (void)
{
#ifdef GATHER_STATISTICS
  n_fields_searched = 0;
  n_calls_lookup_field = 0, n_calls_lookup_field_1 = 0;
  n_calls_lookup_fnfields = 0, n_calls_lookup_fnfields_1 = 0;
  n_calls_get_base_type = 0;
  n_outer_fields_searched = 0;
  n_contexts_saved = 0;
#endif /* GATHER_STATISTICS */
}

/* Helper for lookup_conversions_r.  TO_TYPE is the type converted to
   by a conversion op in base BINFO.  VIRTUAL_DEPTH is nonzero if
   BINFO is morally virtual, and VIRTUALNESS is nonzero if virtual
   bases have been encountered already in the tree walk.  PARENT_CONVS
   is the list of lists of conversion functions that could hide CONV
   and OTHER_CONVS is the list of lists of conversion functions that
   could hide or be hidden by CONV, should virtualness be involved in
   the hierarchy.  Merely checking the conversion op's name is not
   enough because two conversion operators to the same type can have
   different names.  Return nonzero if we are visible.  */

static int
check_hidden_convs (tree binfo, int virtual_depth, int virtualness,
		    tree to_type, tree parent_convs, tree other_convs)
{
  tree level, probe;

  /* See if we are hidden by a parent conversion.  */
  for (level = parent_convs; level; level = TREE_CHAIN (level))
    for (probe = TREE_VALUE (level); probe; probe = TREE_CHAIN (probe))
      if (same_type_p (to_type, TREE_TYPE (probe)))
	return 0;

  if (virtual_depth || virtualness)
    {
     /* In a virtual hierarchy, we could be hidden, or could hide a
	conversion function on the other_convs list.  */
      for (level = other_convs; level; level = TREE_CHAIN (level))
	{
	  int we_hide_them;
	  int they_hide_us;
	  tree *prev, other;

	  if (!(virtual_depth || TREE_STATIC (level)))
	    /* Neither is morally virtual, so cannot hide each other.  */
	    continue;

	  if (!TREE_VALUE (level))
	    /* They evaporated away already.  */
	    continue;

	  they_hide_us = (virtual_depth
			  && original_binfo (binfo, TREE_PURPOSE (level)));
	  we_hide_them = (!they_hide_us && TREE_STATIC (level)
			  && original_binfo (TREE_PURPOSE (level), binfo));

	  if (!(we_hide_them || they_hide_us))
	    /* Neither is within the other, so no hiding can occur.  */
	    continue;

	  for (prev = &TREE_VALUE (level), other = *prev; other;)
	    {
	      if (same_type_p (to_type, TREE_TYPE (other)))
		{
		  if (they_hide_us)
		    /* We are hidden.  */
		    return 0;

		  if (we_hide_them)
		    {
		      /* We hide the other one.  */
		      other = TREE_CHAIN (other);
		      *prev = other;
		      continue;
		    }
		}
	      prev = &TREE_CHAIN (other);
	      other = *prev;
	    }
	}
    }
  return 1;
}

/* Helper for lookup_conversions_r.  PARENT_CONVS is a list of lists
   of conversion functions, the first slot will be for the current
   binfo, if MY_CONVS is non-NULL.  CHILD_CONVS is the list of lists
   of conversion functions from children of the current binfo,
   concatenated with conversions from elsewhere in the hierarchy --
   that list begins with OTHER_CONVS.  Return a single list of lists
   containing only conversions from the current binfo and its
   children.  */

static tree
split_conversions (tree my_convs, tree parent_convs,
		   tree child_convs, tree other_convs)
{
  tree t;
  tree prev;

  /* Remove the original other_convs portion from child_convs.  */
  for (prev = NULL, t = child_convs;
       t != other_convs; prev = t, t = TREE_CHAIN (t))
    continue;

  if (prev)
    TREE_CHAIN (prev) = NULL_TREE;
  else
    child_convs = NULL_TREE;

  /* Attach the child convs to any we had at this level.  */
  if (my_convs)
    {
      my_convs = parent_convs;
      TREE_CHAIN (my_convs) = child_convs;
    }
  else
    my_convs = child_convs;

  return my_convs;
}

/* Worker for lookup_conversions.  Lookup conversion functions in
   BINFO and its children.  VIRTUAL_DEPTH is nonzero, if BINFO is in
   a morally virtual base, and VIRTUALNESS is nonzero, if we've
   encountered virtual bases already in the tree walk.  PARENT_CONVS &
   PARENT_TPL_CONVS are lists of list of conversions within parent
   binfos.  OTHER_CONVS and OTHER_TPL_CONVS are conversions found
   elsewhere in the tree.  Return the conversions found within this
   portion of the graph in CONVS and TPL_CONVS.  Return nonzero is we
   encountered virtualness.  We keep template and non-template
   conversions separate, to avoid unnecessary type comparisons.

   The located conversion functions are held in lists of lists.  The
   TREE_VALUE of the outer list is the list of conversion functions
   found in a particular binfo.  The TREE_PURPOSE of both the outer
   and inner lists is the binfo at which those conversions were
   found.  TREE_STATIC is set for those lists within of morally
   virtual binfos.  The TREE_VALUE of the inner list is the conversion
   function or overload itself.  The TREE_TYPE of each inner list node
   is the converted-to type.  */

static int
lookup_conversions_r (tree binfo,
		      int virtual_depth, int virtualness,
		      tree parent_convs, tree parent_tpl_convs,
		      tree other_convs, tree other_tpl_convs,
		      tree *convs, tree *tpl_convs)
{
  int my_virtualness = 0;
  tree my_convs = NULL_TREE;
  tree my_tpl_convs = NULL_TREE;
  tree child_convs = NULL_TREE;
  tree child_tpl_convs = NULL_TREE;
  unsigned i;
  tree base_binfo;
  VEC(tree,gc) *method_vec = CLASSTYPE_METHOD_VEC (BINFO_TYPE (binfo));
  tree conv;

  /* If we have no conversion operators, then don't look.  */
  if (!TYPE_HAS_CONVERSION (BINFO_TYPE (binfo)))
    {
      *convs = *tpl_convs = NULL_TREE;

      return 0;
    }

  if (BINFO_VIRTUAL_P (binfo))
    virtual_depth++;

  /* First, locate the unhidden ones at this level.  */
  for (i = CLASSTYPE_FIRST_CONVERSION_SLOT;
       VEC_iterate (tree, method_vec, i, conv);
       ++i)
    {
      tree cur = OVL_CURRENT (conv);

      if (!DECL_CONV_FN_P (cur))
	break;

      if (TREE_CODE (cur) == TEMPLATE_DECL)
	{
	  /* Only template conversions can be overloaded, and we must
	     flatten them out and check each one individually.  */
	  tree tpls;

	  for (tpls = conv; tpls; tpls = OVL_NEXT (tpls))
	    {
	      tree tpl = OVL_CURRENT (tpls);
	      tree type = DECL_CONV_FN_TYPE (tpl);

	      if (check_hidden_convs (binfo, virtual_depth, virtualness,
				      type, parent_tpl_convs, other_tpl_convs))
		{
		  my_tpl_convs = tree_cons (binfo, tpl, my_tpl_convs);
		  TREE_TYPE (my_tpl_convs) = type;
		  if (virtual_depth)
		    {
		      TREE_STATIC (my_tpl_convs) = 1;
		      my_virtualness = 1;
		    }
		}
	    }
	}
      else
	{
	  tree name = DECL_NAME (cur);

	  if (!IDENTIFIER_MARKED (name))
	    {
	      tree type = DECL_CONV_FN_TYPE (cur);

	      if (check_hidden_convs (binfo, virtual_depth, virtualness,
				      type, parent_convs, other_convs))
		{
		  my_convs = tree_cons (binfo, conv, my_convs);
		  TREE_TYPE (my_convs) = type;
		  if (virtual_depth)
		    {
		      TREE_STATIC (my_convs) = 1;
		      my_virtualness = 1;
		    }
		  IDENTIFIER_MARKED (name) = 1;
		}
	    }
	}
    }

  if (my_convs)
    {
      parent_convs = tree_cons (binfo, my_convs, parent_convs);
      if (virtual_depth)
	TREE_STATIC (parent_convs) = 1;
    }

  if (my_tpl_convs)
    {
      parent_tpl_convs = tree_cons (binfo, my_tpl_convs, parent_tpl_convs);
      if (virtual_depth)
	TREE_STATIC (parent_tpl_convs) = 1;
    }

  child_convs = other_convs;
  child_tpl_convs = other_tpl_convs;

  /* Now iterate over each base, looking for more conversions.  */
  for (i = 0; BINFO_BASE_ITERATE (binfo, i, base_binfo); i++)
    {
      tree base_convs, base_tpl_convs;
      unsigned base_virtualness;

      base_virtualness = lookup_conversions_r (base_binfo,
					       virtual_depth, virtualness,
					       parent_convs, parent_tpl_convs,
					       child_convs, child_tpl_convs,
					       &base_convs, &base_tpl_convs);
      if (base_virtualness)
	my_virtualness = virtualness = 1;
      child_convs = chainon (base_convs, child_convs);
      child_tpl_convs = chainon (base_tpl_convs, child_tpl_convs);
    }

  /* Unmark the conversions found at this level  */
  for (conv = my_convs; conv; conv = TREE_CHAIN (conv))
    IDENTIFIER_MARKED (DECL_NAME (OVL_CURRENT (TREE_VALUE (conv)))) = 0;

  *convs = split_conversions (my_convs, parent_convs,
			      child_convs, other_convs);
  *tpl_convs = split_conversions (my_tpl_convs, parent_tpl_convs,
				  child_tpl_convs, other_tpl_convs);

  return my_virtualness;
}

/* Return a TREE_LIST containing all the non-hidden user-defined
   conversion functions for TYPE (and its base-classes).  The
   TREE_VALUE of each node is the FUNCTION_DECL of the conversion
   function.  The TREE_PURPOSE is the BINFO from which the conversion
   functions in this node were selected.  This function is effectively
   performing a set of member lookups as lookup_fnfield does, but
   using the type being converted to as the unique key, rather than the
   field name.  */

tree
lookup_conversions (tree type)
{
  tree convs, tpl_convs;
  tree list = NULL_TREE;

  complete_type (type);
  if (!TYPE_BINFO (type))
    return NULL_TREE;

  lookup_conversions_r (TYPE_BINFO (type), 0, 0,
			NULL_TREE, NULL_TREE, NULL_TREE, NULL_TREE,
			&convs, &tpl_convs);

  /* Flatten the list-of-lists */
  for (; convs; convs = TREE_CHAIN (convs))
    {
      tree probe, next;

      for (probe = TREE_VALUE (convs); probe; probe = next)
	{
	  next = TREE_CHAIN (probe);

	  TREE_CHAIN (probe) = list;
	  list = probe;
	}
    }

  for (; tpl_convs; tpl_convs = TREE_CHAIN (tpl_convs))
    {
      tree probe, next;

      for (probe = TREE_VALUE (tpl_convs); probe; probe = next)
	{
	  next = TREE_CHAIN (probe);

	  TREE_CHAIN (probe) = list;
	  list = probe;
	}
    }

  return list;
}

/* Returns the binfo of the first direct or indirect virtual base derived
   from BINFO, or NULL if binfo is not via virtual.  */

tree
binfo_from_vbase (tree binfo)
{
  for (; binfo; binfo = BINFO_INHERITANCE_CHAIN (binfo))
    {
      if (BINFO_VIRTUAL_P (binfo))
	return binfo;
    }
  return NULL_TREE;
}

/* Returns the binfo of the first direct or indirect virtual base derived
   from BINFO up to the TREE_TYPE, LIMIT, or NULL if binfo is not
   via virtual.  */

tree
binfo_via_virtual (tree binfo, tree limit)
{
  if (limit && !CLASSTYPE_VBASECLASSES (limit))
    /* LIMIT has no virtual bases, so BINFO cannot be via one.  */
    return NULL_TREE;

  for (; binfo && !SAME_BINFO_TYPE_P (BINFO_TYPE (binfo), limit);
       binfo = BINFO_INHERITANCE_CHAIN (binfo))
    {
      if (BINFO_VIRTUAL_P (binfo))
	return binfo;
    }
  return NULL_TREE;
}

/* BINFO is a base binfo in the complete type BINFO_TYPE (HERE).
   Find the equivalent binfo within whatever graph HERE is located.
   This is the inverse of original_binfo.  */

tree
copied_binfo (tree binfo, tree here)
{
  tree result = NULL_TREE;

  if (BINFO_VIRTUAL_P (binfo))
    {
      tree t;

      for (t = here; BINFO_INHERITANCE_CHAIN (t);
	   t = BINFO_INHERITANCE_CHAIN (t))
	continue;

      result = binfo_for_vbase (BINFO_TYPE (binfo), BINFO_TYPE (t));
    }
  else if (BINFO_INHERITANCE_CHAIN (binfo))
    {
      tree cbinfo;
      tree base_binfo;
      int ix;

      cbinfo = copied_binfo (BINFO_INHERITANCE_CHAIN (binfo), here);
      for (ix = 0; BINFO_BASE_ITERATE (cbinfo, ix, base_binfo); ix++)
	if (SAME_BINFO_TYPE_P (BINFO_TYPE (base_binfo), BINFO_TYPE (binfo)))
	  {
	    result = base_binfo;
	    break;
	  }
    }
  else
    {
      gcc_assert (SAME_BINFO_TYPE_P (BINFO_TYPE (here), BINFO_TYPE (binfo)));
      result = here;
    }

  gcc_assert (result);
  return result;
}

tree
binfo_for_vbase (tree base, tree t)
{
  unsigned ix;
  tree binfo;
  VEC(tree,gc) *vbases;

  for (vbases = CLASSTYPE_VBASECLASSES (t), ix = 0;
       VEC_iterate (tree, vbases, ix, binfo); ix++)
    if (SAME_BINFO_TYPE_P (BINFO_TYPE (binfo), base))
      return binfo;
  return NULL;
}

/* BINFO is some base binfo of HERE, within some other
   hierarchy. Return the equivalent binfo, but in the hierarchy
   dominated by HERE.  This is the inverse of copied_binfo.  If BINFO
   is not a base binfo of HERE, returns NULL_TREE.  */

tree
original_binfo (tree binfo, tree here)
{
  tree result = NULL;

  if (SAME_BINFO_TYPE_P (BINFO_TYPE (binfo), BINFO_TYPE (here)))
    result = here;
  else if (BINFO_VIRTUAL_P (binfo))
    result = (CLASSTYPE_VBASECLASSES (BINFO_TYPE (here))
	      ? binfo_for_vbase (BINFO_TYPE (binfo), BINFO_TYPE (here))
	      : NULL_TREE);
  else if (BINFO_INHERITANCE_CHAIN (binfo))
    {
      tree base_binfos;

      base_binfos = original_binfo (BINFO_INHERITANCE_CHAIN (binfo), here);
      if (base_binfos)
	{
	  int ix;
	  tree base_binfo;

	  for (ix = 0; (base_binfo = BINFO_BASE_BINFO (base_binfos, ix)); ix++)
	    if (SAME_BINFO_TYPE_P (BINFO_TYPE (base_binfo),
				   BINFO_TYPE (binfo)))
	      {
		result = base_binfo;
		break;
	      }
	}
    }

  return result;
}

