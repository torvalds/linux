/* Process the ObjC-specific declarations and variables for 
   the Objective-C++ compiler.
   Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Ziemowit Laski  <zlaski@apple.com>

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

#ifndef GCC_OBJCP_DECL_H
#define GCC_OBJCP_DECL_H

extern tree objcp_start_struct (enum tree_code, tree);
extern tree objcp_finish_struct (tree, tree, tree);
extern void objcp_finish_function (void);
extern tree objcp_build_function_call (tree, tree);
extern tree objcp_xref_tag (enum tree_code, tree);
extern int objcp_comptypes (tree, tree);
extern tree objcp_builtin_function (const char *, tree, int, 
				    enum built_in_class, const char *, tree);
extern tree objcp_begin_compound_stmt (int);
extern tree objcp_end_compound_stmt (tree, int);

/* Now "cover up" the corresponding C++ functions if required (NB: the 
   OBJCP_ORIGINAL_FUNCTION macro, shown below, can still be used to
   invoke the original C++ functions if needed).  */
#ifdef OBJCP_REMAP_FUNCTIONS

#define start_struct(code, name) \
	objcp_start_struct (code, name)
#define finish_struct(t, fieldlist, attributes) \
	objcp_finish_struct (t, fieldlist, attributes)
#define finish_function() \
	objcp_finish_function ()
#define xref_tag(code, name) \
	objcp_xref_tag (code, name)
#define comptypes(type1, type2) \
	objcp_comptypes (type1, type2)
#define c_begin_compound_stmt(flags) \
	objcp_begin_compound_stmt (flags)
#define c_end_compound_stmt(stmt, flags) \
	objcp_end_compound_stmt (stmt, flags)

#undef OBJC_TYPE_NAME
#define OBJC_TYPE_NAME(type) \
  (TYPE_NAME (type) && TREE_CODE (TYPE_NAME (type)) == TYPE_DECL \
   ? DECL_NAME (TYPE_NAME (type)) \
   : TYPE_NAME (type))
#undef OBJC_SET_TYPE_NAME
#define OBJC_SET_TYPE_NAME(type, name) \
  if(TYPE_NAME (type) && TREE_CODE (TYPE_NAME (type)) == TYPE_DECL) \
    DECL_NAME (TYPE_NAME (type)) = name; \
  else \
    TYPE_NAME (type) = name;

#undef TYPE_OBJC_INFO
#define TYPE_OBJC_INFO(TYPE) LANG_TYPE_CLASS_CHECK (TYPE)->objc_info
#undef SIZEOF_OBJC_TYPE_LANG_SPECIFIC
#define SIZEOF_OBJC_TYPE_LANG_SPECIFIC sizeof (struct lang_type_class)
#undef ALLOC_OBJC_TYPE_LANG_SPECIFIC
#define ALLOC_OBJC_TYPE_LANG_SPECIFIC(NODE)				\
  do {									\
    TYPE_LANG_SPECIFIC (NODE) = GGC_CNEWVAR	                        \
      (struct lang_type, sizeof (struct lang_type_class));		\
    TYPE_LANG_SPECIFIC (NODE)->u.c.h.is_lang_type_class = 1;		\
  } while (0)

#define OBJCP_ORIGINAL_FUNCTION(name, args) 	(name)args

/* C++ marks ellipsis-free function parameters differently from C.  */
#undef OBJC_VOID_AT_END
#define OBJC_VOID_AT_END        void_list_node

#endif  /* OBJCP_REMAP_FUNCTIONS */

#endif /* ! GCC_OBJCP_DECL_H */
