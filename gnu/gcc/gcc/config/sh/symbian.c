/* Routines for GCC for a Symbian OS targeted SH backend.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.
   Contributed by RedHat.
   Most of this code is stolen from i386/winnt.c.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "output.h"
#include "flags.h"
#include "tree.h"
#include "expr.h"
#include "tm_p.h"
#include "cp/cp-tree.h"	/* We need access to the OVL_... macros.  */
#include "toplev.h"

/* Select the level of debugging information to display.
   0 for no debugging.
   1 for informative messages about decisions to add attributes
   2 for verbose information about what is being done.  */
#define SYMBIAN_DEBUG 0
/* #define SYMBIAN_DEBUG 1 */
/* #define SYMBIAN_DEBUG 2 */

/* A unique character to encode declspec encoded objects.  */
#define SH_SYMBIAN_FLAG_CHAR "$"

/* Unique strings to prefix exported and imported objects.  */
#define DLL_IMPORT_PREFIX SH_SYMBIAN_FLAG_CHAR "i."
#define DLL_EXPORT_PREFIX SH_SYMBIAN_FLAG_CHAR "e."


/* Return the type that we should use to determine if DECL is
   imported or exported.  */

static tree
sh_symbian_associated_type (tree decl)
{
  tree t = NULL_TREE;

  if (TREE_CODE (TREE_TYPE (decl)) == METHOD_TYPE)
  /* Methods now inherit their dllimport/dllexport attributes correctly
     so there is no need to check their class.  In fact it is wrong to
     check their class since a method can remain unexported from an
     exported class.  */
    return t;

  /* Otherwise we can just take the DECL_CONTEXT as normal.  */
  if (DECL_CONTEXT (decl) && TYPE_P (DECL_CONTEXT (decl)))
    t = DECL_CONTEXT (decl);

  return t;
}

/* Return nonzero if DECL is a dllexport'd object.  */

bool
sh_symbian_dllexport_p (tree decl)
{
  tree exp;

  if (   TREE_CODE (decl) != VAR_DECL
      && TREE_CODE (decl) != FUNCTION_DECL)
    return false;

  exp = lookup_attribute ("dllexport", DECL_ATTRIBUTES (decl));

  /* Class members get the dllexport status of their class.  */
  if (exp == NULL)
    {
      tree class = sh_symbian_associated_type (decl);

      if (class)
	exp = lookup_attribute ("dllexport", TYPE_ATTRIBUTES (class));
    }
#if SYMBIAN_DEBUG
  if (exp)
    {
      print_node_brief (stderr, "dllexport:", decl, 0);
      fprintf (stderr, "\n");
    }
  else
#if SYMBIAN_DEBUG < 2
    if (TREE_CODE (decl) != FUNCTION_DECL)
#endif
    {
      print_node_brief (stderr, "no dllexport:", decl, 0);
      fprintf (stderr, "\n");
    }
#endif
  return exp ? true : false;
}

/* Return nonzero if DECL is a dllimport'd object.  */

static bool
sh_symbian_dllimport_p (tree decl)
{
  tree imp;

  if (   TREE_CODE (decl) != VAR_DECL
      && TREE_CODE (decl) != FUNCTION_DECL)
    return false;

  imp = lookup_attribute ("dllimport", DECL_ATTRIBUTES (decl));
  if (imp)
    return true;

  /* Class members get the dllimport status of their class.  */
  imp = sh_symbian_associated_type (decl);
  if (! imp)
    return false;

  imp = lookup_attribute ("dllimport", TYPE_ATTRIBUTES (imp));
  if (!imp)
    return false;

  /* Don't mark defined functions as dllimport.  If the definition itself
     was marked with dllimport, then sh_symbian_handle_dll_attribute reports
     an error. This handles the case when the definition overrides an
     earlier declaration.  */
  if (TREE_CODE (decl) ==  FUNCTION_DECL
      && DECL_INITIAL (decl)
      && !DECL_INLINE (decl))
    {
      /* Don't warn about artificial methods.  */
      if (!DECL_ARTIFICIAL (decl))
	warning (OPT_Wattributes, "function %q+D is defined after prior "
		 "declaration as dllimport: attribute ignored",
		 decl);
      return false;
    }

  /* We ignore the dllimport attribute for inline member functions.
     This differs from MSVC behavior which treats it like GNUC
     'extern inline' extension.   */
  else if (TREE_CODE (decl) == FUNCTION_DECL && DECL_INLINE (decl))
    {
      if (extra_warnings)
	warning (OPT_Wattributes, "inline function %q+D is declared as "
		 "dllimport: attribute ignored",
		 decl);
      return false;
    }

  /*  Don't allow definitions of static data members in dllimport
      class.  Just ignore the attribute for vtable data.  */
  else if (TREE_CODE (decl) == VAR_DECL
	   && TREE_STATIC (decl)
	   && TREE_PUBLIC (decl)
	   && !DECL_EXTERNAL (decl))
    {
      if (!DECL_VIRTUAL_P (decl))
	error ("definition of static data member %q+D of dllimport'd class",
	       decl);
      return false;
    }

  /* Since we can't treat a pointer to a dllimport'd symbol as a
     constant address, we turn off the attribute on C++ virtual
     methods to allow creation of vtables using thunks.  Don't mark
     artificial methods either (in sh_symbian_associated_type, only
     COMDAT artificial method get import status from class context).  */
  else if (TREE_CODE (TREE_TYPE (decl)) == METHOD_TYPE
	   && (DECL_VIRTUAL_P (decl) || DECL_ARTIFICIAL (decl)))
    return false;

  return true;
}

/* Return nonzero if SYMBOL is marked as being dllexport'd.  */

bool
sh_symbian_dllexport_name_p (const char *symbol)
{
  return strncmp (DLL_EXPORT_PREFIX, symbol,
		  strlen (DLL_EXPORT_PREFIX)) == 0;
}

/* Return nonzero if SYMBOL is marked as being dllimport'd.  */


bool
sh_symbian_dllimport_name_p (const char *symbol)
{
  return strncmp (DLL_IMPORT_PREFIX, symbol,
		  strlen (DLL_IMPORT_PREFIX)) == 0;
}

/* Mark a DECL as being dllexport'd.
   Note that we override the previous setting (e.g.: dllimport).  */

static void
sh_symbian_mark_dllexport (tree decl)
{
  const char *oldname;
  char *newname;
  rtx rtlname;
  tree idp;

  rtlname = XEXP (DECL_RTL (decl), 0);
  if (GET_CODE (rtlname) == MEM)
    rtlname = XEXP (rtlname, 0);
  gcc_assert (GET_CODE (rtlname) == SYMBOL_REF);
  oldname = XSTR (rtlname, 0);

  if (sh_symbian_dllimport_name_p (oldname))
    {
     /* Remove DLL_IMPORT_PREFIX.
	Note - we do not issue a warning here.  In Symbian's environment it
	is legitimate for a prototype to be marked as dllimport and the
	corresponding definition to be marked as dllexport.  The prototypes
	are in headers used everywhere and the definition is in a translation
	unit which has included the header in order to ensure argument
	correctness.  */
      oldname += strlen (DLL_IMPORT_PREFIX);
      DECL_DLLIMPORT_P (decl) = 0;
    }
  else if (sh_symbian_dllexport_name_p (oldname))
    return; /* Already done.  */

  newname = alloca (strlen (DLL_EXPORT_PREFIX) + strlen (oldname) + 1);
  sprintf (newname, "%s%s", DLL_EXPORT_PREFIX, oldname);

  /* We pass newname through get_identifier to ensure it has a unique
     address.  RTL processing can sometimes peek inside the symbol ref
     and compare the string's addresses to see if two symbols are
     identical.  */
  idp = get_identifier (newname);

  XEXP (DECL_RTL (decl), 0) =
    gen_rtx_SYMBOL_REF (Pmode, IDENTIFIER_POINTER (idp));
}

/* Mark a DECL as being dllimport'd.  */

static void
sh_symbian_mark_dllimport (tree decl)
{
  const char *oldname;
  char *newname;
  tree idp;
  rtx rtlname;
  rtx newrtl;

  rtlname = XEXP (DECL_RTL (decl), 0);
  if (GET_CODE (rtlname) == MEM)
    rtlname = XEXP (rtlname, 0);
  gcc_assert (GET_CODE (rtlname) == SYMBOL_REF);
  oldname = XSTR (rtlname, 0);

  if (sh_symbian_dllexport_name_p (oldname))
    {
      error ("%qs declared as both exported to and imported from a DLL",
             IDENTIFIER_POINTER (DECL_NAME (decl)));
    }
  else if (sh_symbian_dllimport_name_p (oldname))
    {
      /* Already done, but do a sanity check to prevent assembler errors.  */
      if (!DECL_EXTERNAL (decl) || !TREE_PUBLIC (decl))
	error ("failure in redeclaration of %q+D: dllimport'd symbol lacks external linkage",
	       decl);
    }
  else
    {
      newname = alloca (strlen (DLL_IMPORT_PREFIX) + strlen (oldname) + 1);
      sprintf (newname, "%s%s", DLL_IMPORT_PREFIX, oldname);

      /* We pass newname through get_identifier to ensure it has a unique
	 address.  RTL processing can sometimes peek inside the symbol ref
	 and compare the string's addresses to see if two symbols are
	 identical.  */
      idp = get_identifier (newname);
      newrtl = gen_rtx_SYMBOL_REF (Pmode, IDENTIFIER_POINTER (idp));
      XEXP (DECL_RTL (decl), 0) = newrtl;
    }
}

void
sh_symbian_encode_section_info (tree decl, rtx rtl, int first)
{
  default_encode_section_info (decl, rtl, first);

  /* Mark the decl so we can tell from the rtl whether
     the object is dllexport'd or dllimport'd.  */
  if (sh_symbian_dllexport_p (decl))
    sh_symbian_mark_dllexport (decl);
  else if (sh_symbian_dllimport_p (decl))
    sh_symbian_mark_dllimport (decl);
  /* It might be that DECL has already been marked as dllimport, but a
     subsequent definition nullified that.  The attribute is gone but
     DECL_RTL still has (DLL_IMPORT_PREFIX) prefixed. We need to remove
     that. Ditto for the DECL_DLLIMPORT_P flag.  */
  else if (  (TREE_CODE (decl) == FUNCTION_DECL
	   || TREE_CODE (decl) == VAR_DECL)
	   && DECL_RTL (decl) != NULL_RTX
	   && GET_CODE (DECL_RTL (decl)) == MEM
	   && GET_CODE (XEXP (DECL_RTL (decl), 0)) == MEM
	   && GET_CODE (XEXP (XEXP (DECL_RTL (decl), 0), 0)) == SYMBOL_REF
	   && sh_symbian_dllimport_name_p (XSTR (XEXP (XEXP (DECL_RTL (decl), 0), 0), 0)))
    {
      const char * oldname = XSTR (XEXP (XEXP (DECL_RTL (decl), 0), 0), 0);
      /* Remove DLL_IMPORT_PREFIX.  */
      tree idp = get_identifier (oldname + strlen (DLL_IMPORT_PREFIX));
      rtx newrtl = gen_rtx_SYMBOL_REF (Pmode, IDENTIFIER_POINTER (idp));

      warning (0, "%s %q+D %s after being referenced with dllimport linkage",
	       TREE_CODE (decl) == VAR_DECL ? "variable" : "function",
	       decl, (DECL_INITIAL (decl) || !DECL_EXTERNAL (decl))
	       ? "defined locally" : "redeclared without dllimport attribute");

      XEXP (DECL_RTL (decl), 0) = newrtl;

      DECL_DLLIMPORT_P (decl) = 0;
    }
}


/* Return the length of a function name prefix
    that starts with the character 'c'.  */

static int
sh_symbian_get_strip_length (int c)
{
  /* XXX Assumes strlen (DLL_EXPORT_PREFIX) == strlen (DLL_IMPORT_PREFIX).  */
  return (c == SH_SYMBIAN_FLAG_CHAR[0]) ? strlen (DLL_EXPORT_PREFIX) : 0;
}

/* Return a pointer to a function's name with any
   and all prefix encodings stripped from it.  */

const char *
sh_symbian_strip_name_encoding (const char *name)
{
  int skip;

  while ((skip = sh_symbian_get_strip_length (*name)))
    name += skip;

  return name;
}

/* Add the named attribute to the given node.  Copes with both DECLs and
   TYPEs.  Will only add the attribute if it is not already present.  */

static void
symbian_add_attribute (tree node, const char *attr_name)
{
  tree attrs;
  tree attr;

  attrs = DECL_P (node) ? DECL_ATTRIBUTES (node) : TYPE_ATTRIBUTES (node);

  if (lookup_attribute (attr_name, attrs) != NULL_TREE)
    return;

  attr = get_identifier (attr_name);

  if (DECL_P (node))
    DECL_ATTRIBUTES (node) = tree_cons (attr, NULL_TREE, attrs);
  else
    TYPE_ATTRIBUTES (node) = tree_cons (attr, NULL_TREE, attrs);

#if SYMBIAN_DEBUG
  fprintf (stderr, "propogate %s attribute", attr_name);
  print_node_brief (stderr, " to", node, 0);
  fprintf (stderr, "\n");
#endif
}

/* Handle a "dllimport" or "dllexport" attribute;
   arguments as in struct attribute_spec.handler.  */

tree
sh_symbian_handle_dll_attribute (tree *pnode, tree name, tree args,
				 int flags, bool *no_add_attrs)
{
  tree thunk;
  tree node = *pnode;
  const char *attr = IDENTIFIER_POINTER (name);

  /* These attributes may apply to structure and union types being
     created, but otherwise should pass to the declaration involved.  */
  if (!DECL_P (node))
    {
      if (flags & ((int) ATTR_FLAG_DECL_NEXT
		   | (int) ATTR_FLAG_FUNCTION_NEXT
		   | (int) ATTR_FLAG_ARRAY_NEXT))
	{
	  warning (OPT_Wattributes, "%qs attribute ignored", attr);
	  *no_add_attrs = true;
	  return tree_cons (name, args, NULL_TREE);
	}

      if (TREE_CODE (node) != RECORD_TYPE && TREE_CODE (node) != UNION_TYPE)
	{
	  warning (OPT_Wattributes, "%qs attribute ignored", attr);
	  *no_add_attrs = true;
	}

      return NULL_TREE;
    }

  /* Report error on dllimport ambiguities
     seen now before they cause any damage.  */
  else if (is_attribute_p ("dllimport", name))
    {
      if (TREE_CODE (node) == VAR_DECL)
	{
	  if (DECL_INITIAL (node))
	    {
	      error ("variable %q+D definition is marked dllimport",
		     node);
	      *no_add_attrs = true;
	    }

	  /* `extern' needn't be specified with dllimport.
	     Specify `extern' now and hope for the best.  Sigh.  */
	  DECL_EXTERNAL (node) = 1;
	  /* Also, implicitly give dllimport'd variables declared within
	     a function global scope, unless declared static.  */
	  if (current_function_decl != NULL_TREE && ! TREE_STATIC (node))
  	    TREE_PUBLIC (node) = 1;
	}
    }

  /* If the node is an overloaded constructor or destructor, then we must
     make sure that the attribute is propagated along the overload chain,
     as it is these overloaded functions which will be emitted, rather than
     the user declared constructor itself.  */
  if (TREE_CODE (TREE_TYPE (node)) == METHOD_TYPE
      && (DECL_CONSTRUCTOR_P (node) || DECL_DESTRUCTOR_P (node)))
    {
      tree overload;

      for (overload = OVL_CHAIN (node); overload; overload = OVL_CHAIN (overload))
	{
	  tree node_args;
	  tree func_args;
	  tree function = OVL_CURRENT (overload);

	  if (! function
	      || ! DECL_P (function)
	      || (DECL_CONSTRUCTOR_P (node) && ! DECL_CONSTRUCTOR_P (function))
	      || (DECL_DESTRUCTOR_P (node)  && ! DECL_DESTRUCTOR_P (function)))
	    continue;

	  /* The arguments must match as well.  */
	  for (node_args = DECL_ARGUMENTS (node), func_args = DECL_ARGUMENTS (function);
	       node_args && func_args;
	       node_args = TREE_CHAIN (node_args), func_args = TREE_CHAIN (func_args))
	    if (TREE_TYPE (node_args) != TREE_TYPE (func_args))
	      break;

	  if (node_args || func_args)
	    {
	      /* We can ignore an extraneous __in_chrg arguments in the node.
		 GCC generated destructors, for example, will have this.  */
	      if ((node_args == NULL_TREE
		   || func_args != NULL_TREE)
		  && strcmp (IDENTIFIER_POINTER (DECL_NAME (node)), "__in_chrg") != 0)
		continue;
	    }

	  symbian_add_attribute (function, attr);

	  /* Propagate the attribute to any function thunks as well.  */
	  for (thunk = DECL_THUNKS (function); thunk; thunk = TREE_CHAIN (thunk))
	    if (TREE_CODE (thunk) == FUNCTION_DECL)
	      symbian_add_attribute (thunk, attr);
	}
    }

  if (TREE_CODE (node) == FUNCTION_DECL && DECL_VIRTUAL_P (node))
    {
      /* Propagate the attribute to any thunks of this function.  */
      for (thunk = DECL_THUNKS (node); thunk; thunk = TREE_CHAIN (thunk))
	if (TREE_CODE (thunk) == FUNCTION_DECL)
	  symbian_add_attribute (thunk, attr);
    }

  /*  Report error if symbol is not accessible at global scope.  */
  if (!TREE_PUBLIC (node)
      && (   TREE_CODE (node) == VAR_DECL
	  || TREE_CODE (node) == FUNCTION_DECL))
    {
      error ("external linkage required for symbol %q+D because of %qs attribute",
	     node, IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

#if SYMBIAN_DEBUG
  print_node_brief (stderr, "mark node", node, 0);
  fprintf (stderr, " as %s\n", attr);
#endif

  return NULL_TREE;
}

/* This code implements a specification for exporting the vtable and rtti of
   classes that have members with the dllexport or dllexport attributes.
   This specification is defined here:

     http://www.armdevzone.com/EABI/exported_class.txt

   Basically it says that a class's vtable and rtti should be exported if
   the following rules apply:

   - If it has any non-inline non-pure virtual functions,
     at least one of these need to be declared dllimport
     OR any of the constructors is declared dllimport.

   AND

   - The class has an inline constructor/destructor and
     a key-function (placement of vtable uniquely defined) that
     is defined in this translation unit.

   The specification also says that for classes which will have their
   vtables and rtti exported that their base class(es) might also need a
   similar exporting if:

   - Every base class needs to have its vtable & rtti exported
     as well, if the following the conditions hold true:
     + The base class has a non-inline declared non-pure virtual function
     + The base class is polymorphic (has or inherits any virtual functions)
       or the base class has any virtual base classes.  */

/* Decide if a base class of a class should
   also have its vtable and rtti exported.  */

static void
symbian_possibly_export_base_class (tree base_class)
{
  VEC(tree,gc) *method_vec;
  int len;

  if (! (TYPE_CONTAINS_VPTR_P (base_class)))
    return;

  method_vec = CLASSTYPE_METHOD_VEC (base_class);
  len = method_vec ? VEC_length (tree, method_vec) : 0;

  for (;len --;)
    {
      tree member = VEC_index (tree, method_vec, len);

      if (! member)
	continue;

      for (member = OVL_CURRENT (member); member; member = OVL_NEXT (member))
	{
	  if (TREE_CODE (member) != FUNCTION_DECL)
	    continue;

	  if (DECL_CONSTRUCTOR_P (member) || DECL_DESTRUCTOR_P (member))
	    continue;

	  if (! DECL_VIRTUAL_P (member))
	    continue;

	  if (DECL_PURE_VIRTUAL_P (member))
	    continue;

	  if (DECL_INLINE (member))
	    continue;

	  break;
	}

      if (member)
	break;
    }

  if (len < 0)
    return;

  /* FIXME: According to the spec this base class should be exported, but
     a) how do we do this ? and
     b) it does not appear to be necessary for compliance with the Symbian
        OS which so far is the only consumer of this code.  */
#if SYMBIAN_DEBUG
  print_node_brief (stderr, "", base_class, 0);
  fprintf (stderr, " EXPORTed [base class of exported class]\n");
#endif
}

/* Decide if a class needs its vtable and rtti exporting.  */

static bool
symbian_export_vtable_and_rtti_p (tree ctype)
{
  bool inline_ctor_dtor;
  bool dllimport_ctor_dtor;
  bool dllimport_member;
  tree binfo, base_binfo;
  VEC(tree,gc) *method_vec;
  tree key;
  int i;
  int len;

  /* Make sure that we are examining a class...  */
  if (TREE_CODE (ctype) != RECORD_TYPE)
    {
#if SYMBIAN_DEBUG
      print_node_brief (stderr, "", ctype, 0);
      fprintf (stderr, " does NOT need to be EXPORTed [not a class]\n");
#endif
      return false;
    }

  /* If the class does not have a key function it
     does not need to have its vtable exported.  */
  if ((key = CLASSTYPE_KEY_METHOD (ctype)) == NULL_TREE)
    {
#if SYMBIAN_DEBUG
      print_node_brief (stderr, "", ctype, 0);
      fprintf (stderr, " does NOT need to be EXPORTed [no key function]\n");
#endif
      return false;
    }

  /* If the key fn has not been defined
     then the class should not be exported.  */
  if (! TREE_ASM_WRITTEN (key))
    {
#if SYMBIAN_DEBUG
      print_node_brief (stderr, "", ctype, 0);
      fprintf (stderr, " does NOT need to be EXPORTed [key function not defined]\n");
#endif
      return false;
    }

  /* Check the class's member functions.  */
  inline_ctor_dtor = false;
  dllimport_ctor_dtor = false;
  dllimport_member = false;

  method_vec = CLASSTYPE_METHOD_VEC (ctype);
  len = method_vec ? VEC_length (tree, method_vec) : 0;

  for (;len --;)
    {
      tree member = VEC_index (tree, method_vec, len);

      if (! member)
	continue;

      for (member = OVL_CURRENT (member); member; member = OVL_NEXT (member))
	{
	  if (TREE_CODE (member) != FUNCTION_DECL)
	    continue;

	  if (DECL_CONSTRUCTOR_P (member) || DECL_DESTRUCTOR_P (member))
	    {
	      if (DECL_INLINE (member)
		  /* Ignore C++ backend created inline ctors/dtors.  */
		  && (   DECL_MAYBE_IN_CHARGE_CONSTRUCTOR_P (member)
		      || DECL_MAYBE_IN_CHARGE_DESTRUCTOR_P (member)))
		inline_ctor_dtor = true;

	      if (lookup_attribute ("dllimport", DECL_ATTRIBUTES (member)))
		dllimport_ctor_dtor = true;
	    }
	  else
	    {
	      if (DECL_PURE_VIRTUAL_P (member))
		continue;

	      if (! DECL_VIRTUAL_P (member))
		continue;

	      if (DECL_INLINE (member))
		continue;

	      if (lookup_attribute ("dllimport", DECL_ATTRIBUTES (member)))
		dllimport_member = true;
	    }
	}
    }

  if (! dllimport_member && ! dllimport_ctor_dtor)
    {
#if SYMBIAN_DEBUG
      print_node_brief (stderr, "", ctype, 0);
      fprintf (stderr,
	       " does NOT need to be EXPORTed [no non-pure virtuals or ctors/dtors with dllimport]\n");
#endif
      return false;
    }

  if (! inline_ctor_dtor)
    {
#if SYMBIAN_DEBUG
      print_node_brief (stderr, "", ctype, 0);
      fprintf (stderr,
	       " does NOT need to be EXPORTed [no inline ctor/dtor]\n");
#endif
      return false;
    }

#if SYMBIAN_DEBUG
  print_node_brief (stderr, "", ctype, 0);
  fprintf (stderr, " DOES need to be EXPORTed\n");
#endif

  /* Now we must check and possibly export the base classes.  */
  for (i = 0, binfo = TYPE_BINFO (ctype);
       BINFO_BASE_ITERATE (binfo, i, base_binfo); i++)
    symbian_possibly_export_base_class (BINFO_TYPE (base_binfo));

  return true;
}

/* Add the named attribute to a class and its vtable and rtti.  */

static void
symbian_add_attribute_to_class_vtable_and_rtti (tree ctype, const char *attr_name)
{
  symbian_add_attribute (ctype, attr_name);

  /* If the vtable exists then they need annotating as well.  */
  if (CLASSTYPE_VTABLES (ctype))
    /* XXX - Do we need to annotate any vtables other than the primary ?  */
    symbian_add_attribute (CLASSTYPE_VTABLES (ctype), attr_name);

  /* If the rtti exists then it needs annotating as well.  */
  if (TYPE_MAIN_VARIANT (ctype)
      && CLASSTYPE_TYPEINFO_VAR (TYPE_MAIN_VARIANT (ctype)))
    symbian_add_attribute (CLASSTYPE_TYPEINFO_VAR (TYPE_MAIN_VARIANT (ctype)),
			   attr_name);
}

/* Decide if a class needs to have an attribute because
   one of its member functions has the attribute.  */

static bool
symbian_class_needs_attribute_p (tree ctype, const char *attribute_name)
{
  VEC(tree,gc) *method_vec;

  method_vec = CLASSTYPE_METHOD_VEC (ctype);

  /* If the key function has the attribute then the class needs it too.  */
  if (TYPE_POLYMORPHIC_P (ctype)
      && method_vec
      && lookup_attribute (attribute_name,
			   DECL_ATTRIBUTES (VEC_index (tree, method_vec, 0))))
    return true;

  /* Check the class's member functions.  */
  if (TREE_CODE (ctype) == RECORD_TYPE)
    {
      unsigned int len;

      len = method_vec ? VEC_length (tree, method_vec) : 0;

      for (;len --;)
	{
	  tree member = VEC_index (tree, method_vec, len);

	  if (! member)
	    continue;

	  for (member = OVL_CURRENT (member);
	       member;
	       member = OVL_NEXT (member))
	    {
	      if (TREE_CODE (member) != FUNCTION_DECL)
		continue;

	      if (DECL_PURE_VIRTUAL_P (member))
		continue;

	      if (! DECL_VIRTUAL_P (member))
		continue;

	      if (lookup_attribute (attribute_name, DECL_ATTRIBUTES (member)))
		{
#if SYMBIAN_DEBUG
		  print_node_brief (stderr, "", ctype, 0);
		  fprintf (stderr, " inherits %s because", attribute_name);
		  print_node_brief (stderr, "", member, 0);
		  fprintf (stderr, " has it.\n");
#endif
		  return true;
		}
	    }
	}
    }

#if SYMBIAN_DEBUG
  print_node_brief (stderr, "", ctype, 0);
  fprintf (stderr, " does not inherit %s\n", attribute_name);
#endif
  return false;
}

int
symbian_import_export_class (tree ctype, int import_export)
{
  const char *attr_name = NULL;

  /* If we are exporting the class but it does not have the dllexport
     attribute then we may need to add it.  Similarly imported classes
     may need the dllimport attribute.  */
  switch (import_export)
    {
    case  1: attr_name = "dllexport"; break;
    case -1: attr_name = "dllimport"; break;
    default: break;
    }

  if (attr_name
      && ! lookup_attribute (attr_name, TYPE_ATTRIBUTES (ctype)))
    {
      if (symbian_class_needs_attribute_p (ctype, attr_name))
	symbian_add_attribute_to_class_vtable_and_rtti (ctype, attr_name);

      /* Classes can be forced to export their
	 vtable and rtti under certain conditions.  */
      if (symbian_export_vtable_and_rtti_p (ctype))
	{
	  symbian_add_attribute_to_class_vtable_and_rtti (ctype, "dllexport");

	  /* Make sure that the class and its vtable are exported.  */
	  import_export = 1;

	  if (CLASSTYPE_VTABLES (ctype))
	    DECL_EXTERNAL (CLASSTYPE_VTABLES (ctype)) = 1;

	  /* Check to make sure that if the class has a key method that
	     it is now on the list of keyed classes.  That way its vtable
	     will be emitted.  */
	  if (CLASSTYPE_KEY_METHOD (ctype))
	    {
	      tree class;

	      for (class = keyed_classes; class; class = TREE_CHAIN (class))
		if (class == ctype)
		  break;

	      if (class == NULL_TREE)
		{
#if SYMBIAN_DEBUG
		  print_node_brief (stderr, "Add node", ctype, 0);
		  fprintf (stderr, " to the keyed classes list\n");
#endif
		  keyed_classes = tree_cons (NULL_TREE, ctype, keyed_classes);
		}
	    }

	  /* Make sure that the typeinfo will be emitted as well.  */
	  if (CLASS_TYPE_P (ctype))
	    TYPE_NEEDS_CONSTRUCTING (TREE_TYPE (CLASSTYPE_TYPEINFO_VAR (TYPE_MAIN_VARIANT (ctype)))) = 1;
	}
    }

  return import_export;
}

/* Dummy definition of this array for cc1 building purposes.  */
tree cp_global_trees[CPTI_MAX] __attribute__((weak));

#if defined ENABLE_TREE_CHECKING && (GCC_VERSION >= 2007)

/* Dummy version of this G++ function for building cc1.  */
void lang_check_failed (const char *, int, const char *) __attribute__((weak));

void
lang_check_failed (const char *file, int line, const char *function)
{
  internal_error ("lang_* check: failed in %s, at %s:%d",
		  function, trim_filename (file), line);
}
#endif /* ENABLE_TREE_CHECKING */
