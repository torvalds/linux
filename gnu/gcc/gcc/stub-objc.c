/* Stub functions for Objective-C and Objective-C++ routines
   that are called from within the C and C++ front-ends,
   respectively.
   Copyright (C) 1991, 1995, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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
#include "tree.h"
#include "c-common.h"

tree
objc_is_class_name (tree ARG_UNUSED (arg))
{
  return 0;
}

tree
objc_is_id (tree ARG_UNUSED (arg))
{
  return 0;
}

tree
objc_is_object_ptr (tree ARG_UNUSED (arg))
{
  return 0;
}

tree
objc_lookup_ivar (tree other, tree ARG_UNUSED (arg))
{
  /* Just use whatever C/C++ found.  */
  return other;
}

void
objc_check_decl (tree ARG_UNUSED (decl))
{
}
   
int
objc_is_reserved_word (tree ARG_UNUSED (ident))
{
  return 0;
}

bool
objc_compare_types (tree ARG_UNUSED (ltyp), tree ARG_UNUSED (rtyp),
		    int ARG_UNUSED (argno), tree ARG_UNUSED (callee))
{
  return false;
}

void
objc_volatilize_decl (tree ARG_UNUSED (decl))
{
}

bool
objc_type_quals_match (tree ARG_UNUSED (ltyp), tree ARG_UNUSED (rtyp))
{
  return false;
}

tree
objc_rewrite_function_call (tree function, tree ARG_UNUSED (params))
{
  return function;
}

tree
objc_message_selector (void)
{ 
  return 0;
}

void
objc_declare_alias (tree ARG_UNUSED (alias), tree ARG_UNUSED (orig))
{
}

void
objc_declare_class (tree ARG_UNUSED (list))
{
}

void
objc_declare_protocols (tree ARG_UNUSED (list))
{
}

void
objc_start_protocol (tree ARG_UNUSED (proto),
		     tree ARG_UNUSED (protorefs))
{
}

void
objc_start_class_interface (tree ARG_UNUSED (name),
			    tree ARG_UNUSED (super),
			    tree ARG_UNUSED (protos))
{
}

void
objc_start_category_interface (tree ARG_UNUSED (name),
			       tree ARG_UNUSED (categ),
			       tree ARG_UNUSED (protos))
{
}

void
objc_continue_interface (void)
{
}

void
objc_finish_interface (void)
{
}

void
objc_add_instance_variable (tree ARG_UNUSED (decl))
{
}

void
objc_set_visibility (int ARG_UNUSED (vis))
{
}

void
objc_set_method_type (enum tree_code ARG_UNUSED (code))
{
}

void
objc_start_class_implementation (tree ARG_UNUSED (name),
				 tree ARG_UNUSED (super))
{
}

void
objc_start_category_implementation (tree ARG_UNUSED (name),
				    tree ARG_UNUSED (categ))
{
}

void
objc_continue_implementation (void)
{
}

void
objc_clear_super_receiver (void)
{
}

void
objc_finish_implementation (void)
{
}

void
objc_add_method_declaration (tree ARG_UNUSED (signature))
{
}

void
objc_start_method_definition (tree ARG_UNUSED (signature))
{
}

void
objc_finish_method_definition (tree ARG_UNUSED (fndecl))
{
}

tree
objc_build_keyword_decl (tree ARG_UNUSED (selector),
			 tree ARG_UNUSED (typename),
			 tree ARG_UNUSED (identifier))
{
  return 0;
}

tree
objc_build_method_signature (tree ARG_UNUSED (rettype),
			     tree ARG_UNUSED (selectors),
			     tree ARG_UNUSED (optparms),
			     bool ARG_UNUSED (ellipsis))
{
  return 0;
}

tree
objc_build_encode_expr (tree ARG_UNUSED (expr))
{
  return 0;
}

tree
objc_build_protocol_expr (tree ARG_UNUSED (expr))
{
  return 0;
}

tree
objc_build_selector_expr (tree ARG_UNUSED (expr))
{
  return 0;
}

tree
objc_build_message_expr (tree ARG_UNUSED (expr))
{
  return 0;
}

tree
objc_build_string_object (tree ARG_UNUSED (str))
{
  return 0;
}

tree
objc_get_class_reference (tree ARG_UNUSED (name))
{
  return 0;
}

tree
objc_get_protocol_qualified_type (tree ARG_UNUSED (name),
				  tree ARG_UNUSED (protos))
{
  return 0;
}

int
objc_static_init_needed_p (void)
{
  return 0;
}

tree
objc_generate_static_init_call (tree ARG_UNUSED (ctors))
{
  return 0;
}

int
objc_is_public (tree ARG_UNUSED (expr), tree ARG_UNUSED (identifier))
{
  return 1;
}

tree
objc_get_class_ivars (tree ARG_UNUSED (name))
{
  return 0;
}

tree
objc_build_throw_stmt (tree ARG_UNUSED (expr))
{
  return 0;
}

tree
objc_build_synchronized (location_t ARG_UNUSED (start_locus),
			 tree ARG_UNUSED (mutex), tree ARG_UNUSED (body))
{
  return 0;
}

void
objc_begin_try_stmt (location_t ARG_UNUSED (try_locus), tree ARG_UNUSED (body))
{
}
   
void
objc_begin_catch_clause (tree ARG_UNUSED (decl))
{
}

void
objc_finish_catch_clause (void)
{
}

void
objc_build_finally_clause (location_t ARG_UNUSED (finally_locus),
			   tree ARG_UNUSED (body))
{
}

tree
objc_finish_try_stmt (void)
{
  return 0;
}

tree
objc_generate_write_barrier (tree ARG_UNUSED (lhs),
			     enum tree_code ARG_UNUSED (modifycode),
			     tree ARG_UNUSED (rhs))
{
  return 0;
}  
