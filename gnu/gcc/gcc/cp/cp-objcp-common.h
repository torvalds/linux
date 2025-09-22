/* Language hooks common to C++ and ObjC++ front ends.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.
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

#ifndef GCC_CP_OBJCP_COMMON
#define GCC_CP_OBJCP_COMMON

/* In cp/cp-lang.c and objcp/objcp-lang.c.  */

extern tree objcp_tsubst_copy_and_build (tree, tree, tsubst_flags_t,
					 tree, bool);

/* Lang hooks that are shared between C++ and ObjC++ are defined here.  Hooks
   specific to C++ or ObjC++ go in cp/cp-lang.c and objcp/objcp-lang.c,
   respectively.  */

#undef LANG_HOOKS_TREE_SIZE
#define LANG_HOOKS_TREE_SIZE cp_tree_size
#undef LANG_HOOKS_FINISH
#define LANG_HOOKS_FINISH cxx_finish
#undef LANG_HOOKS_CLEAR_BINDING_STACK
#define LANG_HOOKS_CLEAR_BINDING_STACK pop_everything
#undef LANG_HOOKS_INIT_OPTIONS
#define LANG_HOOKS_INIT_OPTIONS c_common_init_options
#undef LANG_HOOKS_INITIALIZE_DIAGNOSTICS
#define LANG_HOOKS_INITIALIZE_DIAGNOSTICS cxx_initialize_diagnostics
#undef LANG_HOOKS_HANDLE_OPTION
#define LANG_HOOKS_HANDLE_OPTION c_common_handle_option
#undef LANG_HOOKS_HANDLE_FILENAME
#define LANG_HOOKS_HANDLE_FILENAME c_common_handle_filename
#undef LANG_HOOKS_MISSING_ARGUMENT
#define LANG_HOOKS_MISSING_ARGUMENT c_common_missing_argument
#undef LANG_HOOKS_POST_OPTIONS
#define LANG_HOOKS_POST_OPTIONS c_common_post_options
#undef LANG_HOOKS_GET_ALIAS_SET
#define LANG_HOOKS_GET_ALIAS_SET cxx_get_alias_set
#undef LANG_HOOKS_EXPAND_CONSTANT
#define LANG_HOOKS_EXPAND_CONSTANT cplus_expand_constant
#undef LANG_HOOKS_EXPAND_EXPR
#define LANG_HOOKS_EXPAND_EXPR cxx_expand_expr
#undef LANG_HOOKS_EXPAND_DECL
#define LANG_HOOKS_EXPAND_DECL c_expand_decl
#undef LANG_HOOKS_PARSE_FILE
#define LANG_HOOKS_PARSE_FILE c_common_parse_file
#undef LANG_HOOKS_STATICP
#define LANG_HOOKS_STATICP cxx_staticp
#undef LANG_HOOKS_DUP_LANG_SPECIFIC_DECL
#define LANG_HOOKS_DUP_LANG_SPECIFIC_DECL cxx_dup_lang_specific_decl
#undef LANG_HOOKS_SET_DECL_ASSEMBLER_NAME
#define LANG_HOOKS_SET_DECL_ASSEMBLER_NAME mangle_decl
#undef LANG_HOOKS_MARK_ADDRESSABLE
#define LANG_HOOKS_MARK_ADDRESSABLE cxx_mark_addressable
#undef LANG_HOOKS_PRINT_STATISTICS
#define LANG_HOOKS_PRINT_STATISTICS cxx_print_statistics
#undef LANG_HOOKS_PRINT_XNODE
#define LANG_HOOKS_PRINT_XNODE cxx_print_xnode
#undef LANG_HOOKS_PRINT_DECL
#define LANG_HOOKS_PRINT_DECL cxx_print_decl
#undef LANG_HOOKS_PRINT_TYPE
#define LANG_HOOKS_PRINT_TYPE cxx_print_type
#undef LANG_HOOKS_PRINT_IDENTIFIER
#define LANG_HOOKS_PRINT_IDENTIFIER cxx_print_identifier
#undef LANG_HOOKS_TYPES_COMPATIBLE_P
#define LANG_HOOKS_TYPES_COMPATIBLE_P cxx_types_compatible_p
#undef LANG_HOOKS_PRINT_ERROR_FUNCTION
#define LANG_HOOKS_PRINT_ERROR_FUNCTION	cxx_print_error_function
#undef LANG_HOOKS_WARN_UNUSED_GLOBAL_DECL
#define LANG_HOOKS_WARN_UNUSED_GLOBAL_DECL cxx_warn_unused_global_decl
#undef LANG_HOOKS_WRITE_GLOBALS
#define LANG_HOOKS_WRITE_GLOBALS lhd_do_nothing
#undef LANG_HOOKS_COMDAT_GROUP
#define LANG_HOOKS_COMDAT_GROUP cxx_comdat_group

#undef LANG_HOOKS_FUNCTION_INIT
#define LANG_HOOKS_FUNCTION_INIT cxx_push_function_context
#undef LANG_HOOKS_FUNCTION_FINAL
#define LANG_HOOKS_FUNCTION_FINAL cxx_pop_function_context
#undef LANG_HOOKS_FUNCTION_MISSING_NORETURN_OK_P
#define LANG_HOOKS_FUNCTION_MISSING_NORETURN_OK_P cp_missing_noreturn_ok_p

/* Attribute hooks.  */
#undef LANG_HOOKS_COMMON_ATTRIBUTE_TABLE
#define LANG_HOOKS_COMMON_ATTRIBUTE_TABLE c_common_attribute_table
#undef LANG_HOOKS_FORMAT_ATTRIBUTE_TABLE
#define LANG_HOOKS_FORMAT_ATTRIBUTE_TABLE c_common_format_attribute_table
#undef LANG_HOOKS_ATTRIBUTE_TABLE
#define LANG_HOOKS_ATTRIBUTE_TABLE cxx_attribute_table

#undef LANG_HOOKS_TREE_INLINING_WALK_SUBTREES
#define LANG_HOOKS_TREE_INLINING_WALK_SUBTREES \
  cp_walk_subtrees
#undef LANG_HOOKS_TREE_INLINING_CANNOT_INLINE_TREE_FN
#define LANG_HOOKS_TREE_INLINING_CANNOT_INLINE_TREE_FN \
  cp_cannot_inline_tree_fn
#undef LANG_HOOKS_TREE_INLINING_ADD_PENDING_FN_DECLS
#define LANG_HOOKS_TREE_INLINING_ADD_PENDING_FN_DECLS \
  cp_add_pending_fn_decls
#undef LANG_HOOKS_TREE_INLINING_AUTO_VAR_IN_FN_P
#define LANG_HOOKS_TREE_INLINING_AUTO_VAR_IN_FN_P \
  cp_auto_var_in_fn_p
#undef LANG_HOOKS_TREE_INLINING_ANON_AGGR_TYPE_P
#define LANG_HOOKS_TREE_INLINING_ANON_AGGR_TYPE_P anon_aggr_type_p
#undef LANG_HOOKS_TREE_INLINING_VAR_MOD_TYPE_P
#define LANG_HOOKS_TREE_INLINING_VAR_MOD_TYPE_P cp_var_mod_type_p
#undef LANG_HOOKS_TREE_DUMP_DUMP_TREE_FN
#define LANG_HOOKS_TREE_DUMP_DUMP_TREE_FN cp_dump_tree
#undef LANG_HOOKS_TREE_DUMP_TYPE_QUALS_FN
#define LANG_HOOKS_TREE_DUMP_TYPE_QUALS_FN cp_type_quals
#undef LANG_HOOKS_EXPR_SIZE
#define LANG_HOOKS_EXPR_SIZE cp_expr_size

#undef LANG_HOOKS_CALLGRAPH_ANALYZE_EXPR
#define LANG_HOOKS_CALLGRAPH_ANALYZE_EXPR cxx_callgraph_analyze_expr
#undef LANG_HOOKS_CALLGRAPH_EXPAND_FUNCTION
#define LANG_HOOKS_CALLGRAPH_EXPAND_FUNCTION expand_body

#undef LANG_HOOKS_MAKE_TYPE
#define LANG_HOOKS_MAKE_TYPE cxx_make_type
#undef LANG_HOOKS_TYPE_FOR_MODE
#define LANG_HOOKS_TYPE_FOR_MODE c_common_type_for_mode
#undef LANG_HOOKS_TYPE_FOR_SIZE
#define LANG_HOOKS_TYPE_FOR_SIZE c_common_type_for_size
#undef LANG_HOOKS_SIGNED_TYPE
#define LANG_HOOKS_SIGNED_TYPE c_common_signed_type
#undef LANG_HOOKS_UNSIGNED_TYPE
#define LANG_HOOKS_UNSIGNED_TYPE c_common_unsigned_type
#undef LANG_HOOKS_SIGNED_OR_UNSIGNED_TYPE
#define LANG_HOOKS_SIGNED_OR_UNSIGNED_TYPE c_common_signed_or_unsigned_type
#undef LANG_HOOKS_INCOMPLETE_TYPE_ERROR
#define LANG_HOOKS_INCOMPLETE_TYPE_ERROR cxx_incomplete_type_error
#undef LANG_HOOKS_TYPE_PROMOTES_TO
#define LANG_HOOKS_TYPE_PROMOTES_TO cxx_type_promotes_to
#undef LANG_HOOKS_REGISTER_BUILTIN_TYPE
#define LANG_HOOKS_REGISTER_BUILTIN_TYPE c_register_builtin_type
#undef LANG_HOOKS_TO_TARGET_CHARSET
#define LANG_HOOKS_TO_TARGET_CHARSET c_common_to_target_charset
#undef LANG_HOOKS_GIMPLIFY_EXPR
#define LANG_HOOKS_GIMPLIFY_EXPR cp_gimplify_expr
#undef LANG_HOOKS_OMP_PREDETERMINED_SHARING
#define LANG_HOOKS_OMP_PREDETERMINED_SHARING cxx_omp_predetermined_sharing
#undef LANG_HOOKS_OMP_CLAUSE_DEFAULT_CTOR
#define LANG_HOOKS_OMP_CLAUSE_DEFAULT_CTOR cxx_omp_clause_default_ctor
#undef LANG_HOOKS_OMP_CLAUSE_COPY_CTOR
#define LANG_HOOKS_OMP_CLAUSE_COPY_CTOR cxx_omp_clause_copy_ctor
#undef LANG_HOOKS_OMP_CLAUSE_ASSIGN_OP
#define LANG_HOOKS_OMP_CLAUSE_ASSIGN_OP cxx_omp_clause_assign_op
#undef LANG_HOOKS_OMP_CLAUSE_DTOR
#define LANG_HOOKS_OMP_CLAUSE_DTOR cxx_omp_clause_dtor
#undef LANG_HOOKS_OMP_PRIVATIZE_BY_REFERENCE
#define LANG_HOOKS_OMP_PRIVATIZE_BY_REFERENCE cxx_omp_privatize_by_reference

#endif /* GCC_CP_OBJCP_COMMON */
