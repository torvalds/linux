/* Generate information regarding function declarations and definitions based
   on information stored in GCC's tree structure.  This code implements the
   -aux-info option.
   Copyright (C) 1989, 1991, 1994, 1995, 1997, 1998,
   1999, 2000, 2003, 2004 Free Software Foundation, Inc.
   Contributed by Ron Guilmette (rfg@segfault.us.com).

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
#include "flags.h"
#include "tree.h"
#include "c-tree.h"
#include "toplev.h"

enum formals_style_enum {
  ansi,
  k_and_r_names,
  k_and_r_decls
};
typedef enum formals_style_enum formals_style;


static const char *data_type;

static char *affix_data_type (const char *) ATTRIBUTE_MALLOC;
static const char *gen_formal_list_for_type (tree, formals_style);
static int   deserves_ellipsis (tree);
static const char *gen_formal_list_for_func_def (tree, formals_style);
static const char *gen_type (const char *, tree, formals_style);
static const char *gen_decl (tree, int, formals_style);

/* Given a string representing an entire type or an entire declaration
   which only lacks the actual "data-type" specifier (at its left end),
   affix the data-type specifier to the left end of the given type
   specification or object declaration.

   Because of C language weirdness, the data-type specifier (which normally
   goes in at the very left end) may have to be slipped in just to the
   right of any leading "const" or "volatile" qualifiers (there may be more
   than one).  Actually this may not be strictly necessary because it seems
   that GCC (at least) accepts `<data-type> const foo;' and treats it the
   same as `const <data-type> foo;' but people are accustomed to seeing
   `const char *foo;' and *not* `char const *foo;' so we try to create types
   that look as expected.  */

static char *
affix_data_type (const char *param)
{
  char *const type_or_decl = ASTRDUP (param);
  char *p = type_or_decl;
  char *qualifiers_then_data_type;
  char saved;

  /* Skip as many leading const's or volatile's as there are.  */

  for (;;)
    {
      if (!strncmp (p, "volatile ", 9))
	{
	  p += 9;
	  continue;
	}
      if (!strncmp (p, "const ", 6))
	{
	  p += 6;
	  continue;
	}
      break;
    }

  /* p now points to the place where we can insert the data type.  We have to
     add a blank after the data-type of course.  */

  if (p == type_or_decl)
    return concat (data_type, " ", type_or_decl, NULL);

  saved = *p;
  *p = '\0';
  qualifiers_then_data_type = concat (type_or_decl, data_type, NULL);
  *p = saved;
  return reconcat (qualifiers_then_data_type,
		   qualifiers_then_data_type, " ", p, NULL);
}

/* Given a tree node which represents some "function type", generate the
   source code version of a formal parameter list (of some given style) for
   this function type.  Return the whole formal parameter list (including
   a pair of surrounding parens) as a string.   Note that if the style
   we are currently aiming for is non-ansi, then we just return a pair
   of empty parens here.  */

static const char *
gen_formal_list_for_type (tree fntype, formals_style style)
{
  const char *formal_list = "";
  tree formal_type;

  if (style != ansi)
    return "()";

  formal_type = TYPE_ARG_TYPES (fntype);
  while (formal_type && TREE_VALUE (formal_type) != void_type_node)
    {
      const char *this_type;

      if (*formal_list)
	formal_list = concat (formal_list, ", ", NULL);

      this_type = gen_type ("", TREE_VALUE (formal_type), ansi);
      formal_list
	= ((strlen (this_type))
	   ? concat (formal_list, affix_data_type (this_type), NULL)
	   : concat (formal_list, data_type, NULL));

      formal_type = TREE_CHAIN (formal_type);
    }

  /* If we got to here, then we are trying to generate an ANSI style formal
     parameters list.

     New style prototyped ANSI formal parameter lists should in theory always
     contain some stuff between the opening and closing parens, even if it is
     only "void".

     The brutal truth though is that there is lots of old K&R code out there
     which contains declarations of "pointer-to-function" parameters and
     these almost never have fully specified formal parameter lists associated
     with them.  That is, the pointer-to-function parameters are declared
     with just empty parameter lists.

     In cases such as these, protoize should really insert *something* into
     the vacant parameter lists, but what?  It has no basis on which to insert
     anything in particular.

     Here, we make life easy for protoize by trying to distinguish between
     K&R empty parameter lists and new-style prototyped parameter lists
     that actually contain "void".  In the latter case we (obviously) want
     to output the "void" verbatim, and that what we do.  In the former case,
     we do our best to give protoize something nice to insert.

     This "something nice" should be something that is still valid (when
     re-compiled) but something that can clearly indicate to the user that
     more typing information (for the parameter list) should be added (by
     hand) at some convenient moment.

     The string chosen here is a comment with question marks in it.  */

  if (!*formal_list)
    {
      if (TYPE_ARG_TYPES (fntype))
	/* assert (TREE_VALUE (TYPE_ARG_TYPES (fntype)) == void_type_node);  */
	formal_list = "void";
      else
	formal_list = "/* ??? */";
    }
  else
    {
      /* If there were at least some parameters, and if the formals-types-list
	 petered out to a NULL (i.e. without being terminated by a
	 void_type_node) then we need to tack on an ellipsis.  */
      if (!formal_type)
	formal_list = concat (formal_list, ", ...", NULL);
    }

  return concat (" (", formal_list, ")", NULL);
}

/* For the generation of an ANSI prototype for a function definition, we have
   to look at the formal parameter list of the function's own "type" to
   determine if the function's formal parameter list should end with an
   ellipsis.  Given a tree node, the following function will return nonzero
   if the "function type" parameter list should end with an ellipsis.  */

static int
deserves_ellipsis (tree fntype)
{
  tree formal_type;

  formal_type = TYPE_ARG_TYPES (fntype);
  while (formal_type && TREE_VALUE (formal_type) != void_type_node)
    formal_type = TREE_CHAIN (formal_type);

  /* If there were at least some parameters, and if the formals-types-list
     petered out to a NULL (i.e. without being terminated by a void_type_node)
     then we need to tack on an ellipsis.  */

  return (!formal_type && TYPE_ARG_TYPES (fntype));
}

/* Generate a parameter list for a function definition (in some given style).

   Note that this routine has to be separate (and different) from the code that
   generates the prototype parameter lists for function declarations, because
   in the case of a function declaration, all we have to go on is a tree node
   representing the function's own "function type".  This can tell us the types
   of all of the formal parameters for the function, but it cannot tell us the
   actual *names* of each of the formal parameters.  We need to output those
   parameter names for each function definition.

   This routine gets a pointer to a tree node which represents the actual
   declaration of the given function, and this DECL node has a list of formal
   parameter (variable) declarations attached to it.  These formal parameter
   (variable) declaration nodes give us the actual names of the formal
   parameters for the given function definition.

   This routine returns a string which is the source form for the entire
   function formal parameter list.  */

static const char *
gen_formal_list_for_func_def (tree fndecl, formals_style style)
{
  const char *formal_list = "";
  tree formal_decl;

  formal_decl = DECL_ARGUMENTS (fndecl);
  while (formal_decl)
    {
      const char *this_formal;

      if (*formal_list && ((style == ansi) || (style == k_and_r_names)))
	formal_list = concat (formal_list, ", ", NULL);
      this_formal = gen_decl (formal_decl, 0, style);
      if (style == k_and_r_decls)
	formal_list = concat (formal_list, this_formal, "; ", NULL);
      else
	formal_list = concat (formal_list, this_formal, NULL);
      formal_decl = TREE_CHAIN (formal_decl);
    }
  if (style == ansi)
    {
      if (!DECL_ARGUMENTS (fndecl))
	formal_list = concat (formal_list, "void", NULL);
      if (deserves_ellipsis (TREE_TYPE (fndecl)))
	formal_list = concat (formal_list, ", ...", NULL);
    }
  if ((style == ansi) || (style == k_and_r_names))
    formal_list = concat (" (", formal_list, ")", NULL);
  return formal_list;
}

/* Generate a string which is the source code form for a given type (t).  This
   routine is ugly and complex because the C syntax for declarations is ugly
   and complex.  This routine is straightforward so long as *no* pointer types,
   array types, or function types are involved.

   In the simple cases, this routine will return the (string) value which was
   passed in as the "ret_val" argument.  Usually, this starts out either as an
   empty string, or as the name of the declared item (i.e. the formal function
   parameter variable).

   This routine will also return with the global variable "data_type" set to
   some string value which is the "basic" data-type of the given complete type.
   This "data_type" string can be concatenated onto the front of the returned
   string after this routine returns to its caller.

   In complicated cases involving pointer types, array types, or function
   types, the C declaration syntax requires an "inside out" approach, i.e. if
   you have a type which is a "pointer-to-function" type, you need to handle
   the "pointer" part first, but it also has to be "innermost" (relative to
   the declaration stuff for the "function" type).  Thus, is this case, you
   must prepend a "(*" and append a ")" to the name of the item (i.e. formal
   variable).  Then you must append and prepend the other info for the
   "function type" part of the overall type.

   To handle the "innermost precedence" rules of complicated C declarators, we
   do the following (in this routine).  The input parameter called "ret_val"
   is treated as a "seed".  Each time gen_type is called (perhaps recursively)
   some additional strings may be appended or prepended (or both) to the "seed"
   string.  If yet another (lower) level of the GCC tree exists for the given
   type (as in the case of a pointer type, an array type, or a function type)
   then the (wrapped) seed is passed to a (recursive) invocation of gen_type()
   this recursive invocation may again "wrap" the (new) seed with yet more
   declarator stuff, by appending, prepending (or both).  By the time the
   recursion bottoms out, the "seed value" at that point will have a value
   which is (almost) the complete source version of the declarator (except
   for the data_type info).  Thus, this deepest "seed" value is simply passed
   back up through all of the recursive calls until it is given (as the return
   value) to the initial caller of the gen_type() routine.  All that remains
   to do at this point is for the initial caller to prepend the "data_type"
   string onto the returned "seed".  */

static const char *
gen_type (const char *ret_val, tree t, formals_style style)
{
  tree chain_p;

  /* If there is a typedef name for this type, use it.  */
  if (TYPE_NAME (t) && TREE_CODE (TYPE_NAME (t)) == TYPE_DECL)
    data_type = IDENTIFIER_POINTER (DECL_NAME (TYPE_NAME (t)));
  else
    {
      switch (TREE_CODE (t))
	{
	case POINTER_TYPE:
	  if (TYPE_READONLY (t))
	    ret_val = concat ("const ", ret_val, NULL);
	  if (TYPE_VOLATILE (t))
	    ret_val = concat ("volatile ", ret_val, NULL);

	  ret_val = concat ("*", ret_val, NULL);

	  if (TREE_CODE (TREE_TYPE (t)) == ARRAY_TYPE || TREE_CODE (TREE_TYPE (t)) == FUNCTION_TYPE)
	    ret_val = concat ("(", ret_val, ")", NULL);

	  ret_val = gen_type (ret_val, TREE_TYPE (t), style);

	  return ret_val;

	case ARRAY_TYPE:
	  if (!COMPLETE_TYPE_P (t) || TREE_CODE (TYPE_SIZE (t)) != INTEGER_CST)
	    ret_val = gen_type (concat (ret_val, "[]", NULL),
				TREE_TYPE (t), style);
	  else if (int_size_in_bytes (t) == 0)
	    ret_val = gen_type (concat (ret_val, "[0]", NULL),
				TREE_TYPE (t), style);
	  else
	    {
	      int size = (int_size_in_bytes (t) / int_size_in_bytes (TREE_TYPE (t)));
	      char buff[10];
	      sprintf (buff, "[%d]", size);
	      ret_val = gen_type (concat (ret_val, buff, NULL),
				  TREE_TYPE (t), style);
	    }
	  break;

	case FUNCTION_TYPE:
	  ret_val = gen_type (concat (ret_val,
				      gen_formal_list_for_type (t, style),
				      NULL),
			      TREE_TYPE (t), style);
	  break;

	case IDENTIFIER_NODE:
	  data_type = IDENTIFIER_POINTER (t);
	  break;

	/* The following three cases are complicated by the fact that a
	   user may do something really stupid, like creating a brand new
	   "anonymous" type specification in a formal argument list (or as
	   part of a function return type specification).  For example:

		int f (enum { red, green, blue } color);

	   In such cases, we have no name that we can put into the prototype
	   to represent the (anonymous) type.  Thus, we have to generate the
	   whole darn type specification.  Yuck!  */

	case RECORD_TYPE:
	  if (TYPE_NAME (t))
	    data_type = IDENTIFIER_POINTER (TYPE_NAME (t));
	  else
	    {
	      data_type = "";
	      chain_p = TYPE_FIELDS (t);
	      while (chain_p)
		{
		  data_type = concat (data_type, gen_decl (chain_p, 0, ansi),
				      NULL);
		  chain_p = TREE_CHAIN (chain_p);
		  data_type = concat (data_type, "; ", NULL);
		}
	      data_type = concat ("{ ", data_type, "}", NULL);
	    }
	  data_type = concat ("struct ", data_type, NULL);
	  break;

	case UNION_TYPE:
	  if (TYPE_NAME (t))
	    data_type = IDENTIFIER_POINTER (TYPE_NAME (t));
	  else
	    {
	      data_type = "";
	      chain_p = TYPE_FIELDS (t);
	      while (chain_p)
		{
		  data_type = concat (data_type, gen_decl (chain_p, 0, ansi),
				      NULL);
		  chain_p = TREE_CHAIN (chain_p);
		  data_type = concat (data_type, "; ", NULL);
		}
	      data_type = concat ("{ ", data_type, "}", NULL);
	    }
	  data_type = concat ("union ", data_type, NULL);
	  break;

	case ENUMERAL_TYPE:
	  if (TYPE_NAME (t))
	    data_type = IDENTIFIER_POINTER (TYPE_NAME (t));
	  else
	    {
	      data_type = "";
	      chain_p = TYPE_VALUES (t);
	      while (chain_p)
		{
		  data_type = concat (data_type,
			IDENTIFIER_POINTER (TREE_PURPOSE (chain_p)), NULL);
		  chain_p = TREE_CHAIN (chain_p);
		  if (chain_p)
		    data_type = concat (data_type, ", ", NULL);
		}
	      data_type = concat ("{ ", data_type, " }", NULL);
	    }
	  data_type = concat ("enum ", data_type, NULL);
	  break;

	case TYPE_DECL:
	  data_type = IDENTIFIER_POINTER (DECL_NAME (t));
	  break;

	case INTEGER_TYPE:
	  data_type = IDENTIFIER_POINTER (DECL_NAME (TYPE_NAME (t)));
	  /* Normally, `unsigned' is part of the deal.  Not so if it comes
	     with a type qualifier.  */
	  if (TYPE_UNSIGNED (t) && TYPE_QUALS (t))
	    data_type = concat ("unsigned ", data_type, NULL);
	  break;

	case REAL_TYPE:
	  data_type = IDENTIFIER_POINTER (DECL_NAME (TYPE_NAME (t)));
	  break;

	case VOID_TYPE:
	  data_type = "void";
	  break;

	case ERROR_MARK:
	  data_type = "[ERROR]";
	  break;

	default:
	  gcc_unreachable ();
	}
    }
  if (TYPE_READONLY (t))
    ret_val = concat ("const ", ret_val, NULL);
  if (TYPE_VOLATILE (t))
    ret_val = concat ("volatile ", ret_val, NULL);
  if (TYPE_RESTRICT (t))
    ret_val = concat ("restrict ", ret_val, NULL);
  return ret_val;
}

/* Generate a string (source) representation of an entire entity declaration
   (using some particular style for function types).

   The given entity may be either a variable or a function.

   If the "is_func_definition" parameter is nonzero, assume that the thing
   we are generating a declaration for is a FUNCTION_DECL node which is
   associated with a function definition.  In this case, we can assume that
   an attached list of DECL nodes for function formal arguments is present.  */

static const char *
gen_decl (tree decl, int is_func_definition, formals_style style)
{
  const char *ret_val;

  if (DECL_NAME (decl))
    ret_val = IDENTIFIER_POINTER (DECL_NAME (decl));
  else
    ret_val = "";

  /* If we are just generating a list of names of formal parameters, we can
     simply return the formal parameter name (with no typing information
     attached to it) now.  */

  if (style == k_and_r_names)
    return ret_val;

  /* Note that for the declaration of some entity (either a function or a
     data object, like for instance a parameter) if the entity itself was
     declared as either const or volatile, then const and volatile properties
     are associated with just the declaration of the entity, and *not* with
     the `type' of the entity.  Thus, for such declared entities, we have to
     generate the qualifiers here.  */

  if (TREE_THIS_VOLATILE (decl))
    ret_val = concat ("volatile ", ret_val, NULL);
  if (TREE_READONLY (decl))
    ret_val = concat ("const ", ret_val, NULL);

  data_type = "";

  /* For FUNCTION_DECL nodes, there are two possible cases here.  First, if
     this FUNCTION_DECL node was generated from a function "definition", then
     we will have a list of DECL_NODE's, one for each of the function's formal
     parameters.  In this case, we can print out not only the types of each
     formal, but also each formal's name.  In the second case, this
     FUNCTION_DECL node came from an actual function declaration (and *not*
     a definition).  In this case, we do nothing here because the formal
     argument type-list will be output later, when the "type" of the function
     is added to the string we are building.  Note that the ANSI-style formal
     parameter list is considered to be a (suffix) part of the "type" of the
     function.  */

  if (TREE_CODE (decl) == FUNCTION_DECL && is_func_definition)
    {
      ret_val = concat (ret_val, gen_formal_list_for_func_def (decl, ansi),
			NULL);

      /* Since we have already added in the formals list stuff, here we don't
	 add the whole "type" of the function we are considering (which
	 would include its parameter-list info), rather, we only add in
	 the "type" of the "type" of the function, which is really just
	 the return-type of the function (and does not include the parameter
	 list info).  */

      ret_val = gen_type (ret_val, TREE_TYPE (TREE_TYPE (decl)), style);
    }
  else
    ret_val = gen_type (ret_val, TREE_TYPE (decl), style);

  ret_val = affix_data_type (ret_val);

  if (TREE_CODE (decl) != FUNCTION_DECL && C_DECL_REGISTER (decl))
    ret_val = concat ("register ", ret_val, NULL);
  if (TREE_PUBLIC (decl))
    ret_val = concat ("extern ", ret_val, NULL);
  if (TREE_CODE (decl) == FUNCTION_DECL && !TREE_PUBLIC (decl))
    ret_val = concat ("static ", ret_val, NULL);

  return ret_val;
}

extern FILE *aux_info_file;

/* Generate and write a new line of info to the aux-info (.X) file.  This
   routine is called once for each function declaration, and once for each
   function definition (even the implicit ones).  */

void
gen_aux_info_record (tree fndecl, int is_definition, int is_implicit,
		     int is_prototyped)
{
  if (flag_gen_aux_info)
    {
      static int compiled_from_record = 0;
      expanded_location xloc = expand_location (DECL_SOURCE_LOCATION (fndecl));

      /* Each output .X file must have a header line.  Write one now if we
	 have not yet done so.  */

      if (!compiled_from_record++)
	{
	  /* The first line tells which directory file names are relative to.
	     Currently, -aux-info works only for files in the working
	     directory, so just use a `.' as a placeholder for now.  */
	  fprintf (aux_info_file, "/* compiled from: . */\n");
	}

      /* Write the actual line of auxiliary info.  */

      fprintf (aux_info_file, "/* %s:%d:%c%c */ %s;",
	       xloc.file, xloc.line,
	       (is_implicit) ? 'I' : (is_prototyped) ? 'N' : 'O',
	       (is_definition) ? 'F' : 'C',
	       gen_decl (fndecl, is_definition, ansi));

      /* If this is an explicit function declaration, we need to also write
	 out an old-style (i.e. K&R) function header, just in case the user
	 wants to run unprotoize.  */

      if (is_definition)
	{
	  fprintf (aux_info_file, " /*%s %s*/",
		   gen_formal_list_for_func_def (fndecl, k_and_r_names),
		   gen_formal_list_for_func_def (fndecl, k_and_r_decls));
	}

      fprintf (aux_info_file, "\n");
    }
}
