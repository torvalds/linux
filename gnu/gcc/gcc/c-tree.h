/* Definitions for C parsing and type checking.
   Copyright (C) 1987, 1993, 1994, 1995, 1997, 1998,
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

#ifndef GCC_C_TREE_H
#define GCC_C_TREE_H

#include "c-common.h"
#include "toplev.h"
#include "diagnostic.h"

/* struct lang_identifier is private to c-decl.c, but langhooks.c needs to
   know how big it is.  This is sanity-checked in c-decl.c.  */
#define C_SIZEOF_STRUCT_LANG_IDENTIFIER \
  (sizeof (struct c_common_identifier) + 3 * sizeof (void *))

/* Language-specific declaration information.  */

struct lang_decl GTY(())
{
  char dummy;
};

/* In a RECORD_TYPE or UNION_TYPE, nonzero if any component is read-only.  */
#define C_TYPE_FIELDS_READONLY(TYPE) TREE_LANG_FLAG_1 (TYPE)

/* In a RECORD_TYPE or UNION_TYPE, nonzero if any component is volatile.  */
#define C_TYPE_FIELDS_VOLATILE(TYPE) TREE_LANG_FLAG_2 (TYPE)

/* In a RECORD_TYPE or UNION_TYPE or ENUMERAL_TYPE
   nonzero if the definition of the type has already started.  */
#define C_TYPE_BEING_DEFINED(TYPE) TYPE_LANG_FLAG_0 (TYPE)

/* In an incomplete RECORD_TYPE or UNION_TYPE, a list of variable
   declarations whose type would be completed by completing that type.  */
#define C_TYPE_INCOMPLETE_VARS(TYPE) TYPE_VFIELD (TYPE)

/* In an IDENTIFIER_NODE, nonzero if this identifier is actually a
   keyword.  C_RID_CODE (node) is then the RID_* value of the keyword,
   and C_RID_YYCODE is the token number wanted by Yacc.  */
#define C_IS_RESERVED_WORD(ID) TREE_LANG_FLAG_0 (ID)

struct lang_type GTY(())
{
  /* In a RECORD_TYPE, a sorted array of the fields of the type.  */
  struct sorted_fields_type * GTY ((reorder ("resort_sorted_fields"))) s;
  /* In an ENUMERAL_TYPE, the min and max values.  */
  tree enum_min;
  tree enum_max;
  /* In a RECORD_TYPE, information specific to Objective-C, such
     as a list of adopted protocols or a pointer to a corresponding
     @interface.  See objc/objc-act.h for details.  */
  tree objc_info;
};

/* Record whether a type or decl was written with nonconstant size.
   Note that TYPE_SIZE may have simplified to a constant.  */
#define C_TYPE_VARIABLE_SIZE(TYPE) TYPE_LANG_FLAG_1 (TYPE)
#define C_DECL_VARIABLE_SIZE(TYPE) DECL_LANG_FLAG_0 (TYPE)

/* Record whether a typedef for type `int' was actually `signed int'.  */
#define C_TYPEDEF_EXPLICITLY_SIGNED(EXP) DECL_LANG_FLAG_1 (EXP)

/* For a FUNCTION_DECL, nonzero if it was defined without an explicit
   return type.  */
#define C_FUNCTION_IMPLICIT_INT(EXP) DECL_LANG_FLAG_1 (EXP)

/* For a FUNCTION_DECL, nonzero if it was an implicit declaration.  */
#define C_DECL_IMPLICIT(EXP) DECL_LANG_FLAG_2 (EXP)

/* For FUNCTION_DECLs, evaluates true if the decl is built-in but has
   been declared.  */
#define C_DECL_DECLARED_BUILTIN(EXP)		\
  DECL_LANG_FLAG_3 (FUNCTION_DECL_CHECK (EXP))

/* For FUNCTION_DECLs, evaluates true if the decl is built-in, has a
   built-in prototype and does not have a non-built-in prototype.  */
#define C_DECL_BUILTIN_PROTOTYPE(EXP)		\
  DECL_LANG_FLAG_6 (FUNCTION_DECL_CHECK (EXP))

/* Record whether a decl was declared register.  This is strictly a
   front-end flag, whereas DECL_REGISTER is used for code generation;
   they may differ for structures with volatile fields.  */
#define C_DECL_REGISTER(EXP) DECL_LANG_FLAG_4 (EXP)

/* Record whether a decl was used in an expression anywhere except an
   unevaluated operand of sizeof / typeof / alignof.  This is only
   used for functions declared static but not defined, though outside
   sizeof and typeof it is set for other function decls as well.  */
#define C_DECL_USED(EXP) DECL_LANG_FLAG_5 (FUNCTION_DECL_CHECK (EXP))

/* Record whether a label was defined in a statement expression which
   has finished and so can no longer be jumped to.  */
#define C_DECL_UNJUMPABLE_STMT_EXPR(EXP)	\
  DECL_LANG_FLAG_6 (LABEL_DECL_CHECK (EXP))

/* Record whether a label was the subject of a goto from outside the
   current level of statement expression nesting and so cannot be
   defined right now.  */
#define C_DECL_UNDEFINABLE_STMT_EXPR(EXP)	\
  DECL_LANG_FLAG_7 (LABEL_DECL_CHECK (EXP))

/* Record whether a label was defined in the scope of an identifier
   with variably modified type which has finished and so can no longer
   be jumped to.  */
#define C_DECL_UNJUMPABLE_VM(EXP)	\
  DECL_LANG_FLAG_3 (LABEL_DECL_CHECK (EXP))

/* Record whether a label was the subject of a goto from outside the
   current level of scopes of identifiers with variably modified type
   and so cannot be defined right now.  */
#define C_DECL_UNDEFINABLE_VM(EXP)	\
  DECL_LANG_FLAG_5 (LABEL_DECL_CHECK (EXP))

/* Record whether a variable has been declared threadprivate by
   #pragma omp threadprivate.  */
#define C_DECL_THREADPRIVATE_P(DECL) DECL_LANG_FLAG_3 (VAR_DECL_CHECK (DECL))

/* Nonzero for a decl which either doesn't exist or isn't a prototype.
   N.B. Could be simplified if all built-in decls had complete prototypes
   (but this is presently difficult because some of them need FILE*).  */
#define C_DECL_ISNT_PROTOTYPE(EXP)			\
       (EXP == 0					\
	|| (TYPE_ARG_TYPES (TREE_TYPE (EXP)) == 0	\
	    && !DECL_BUILT_IN (EXP)))

/* For FUNCTION_TYPE, a hidden list of types of arguments.  The same as
   TYPE_ARG_TYPES for functions with prototypes, but created for functions
   without prototypes.  */
#define TYPE_ACTUAL_ARG_TYPES(NODE) TYPE_LANG_SLOT_1 (NODE)

/* Record parser information about an expression that is irrelevant
   for code generation alongside a tree representing its value.  */
struct c_expr
{
  /* The value of the expression.  */
  tree value;
  /* Record the original binary operator of an expression, which may
     have been changed by fold, STRING_CST for unparenthesized string
     constants, or ERROR_MARK for other expressions (including
     parenthesized expressions).  */
  enum tree_code original_code;
};

/* A kind of type specifier.  Note that this information is currently
   only used to distinguish tag definitions, tag references and typeof
   uses.  */
enum c_typespec_kind {
  /* A reserved keyword type specifier.  */
  ctsk_resword,
  /* A reference to a tag, previously declared, such as "struct foo".
     This includes where the previous declaration was as a different
     kind of tag, in which case this is only valid if shadowing that
     tag in an inner scope.  */
  ctsk_tagref,
  /* A reference to a tag, not previously declared in a visible
     scope.  */
  ctsk_tagfirstref,
  /* A definition of a tag such as "struct foo { int a; }".  */
  ctsk_tagdef,
  /* A typedef name.  */
  ctsk_typedef,
  /* An ObjC-specific kind of type specifier.  */
  ctsk_objc,
  /* A typeof specifier.  */
  ctsk_typeof
};

/* A type specifier: this structure is created in the parser and
   passed to declspecs_add_type only.  */
struct c_typespec {
  /* What kind of type specifier this is.  */
  enum c_typespec_kind kind;
  /* The specifier itself.  */
  tree spec;
};

/* A storage class specifier.  */
enum c_storage_class {
  csc_none,
  csc_auto,
  csc_extern,
  csc_register,
  csc_static,
  csc_typedef
};

/* A type specifier keyword "void", "_Bool", "char", "int", "float",
   "double", or none of these.  */
enum c_typespec_keyword {
  cts_none,
  cts_void,
  cts_bool,
  cts_char,
  cts_int,
  cts_float,
  cts_double,
  cts_dfloat32,
  cts_dfloat64,
  cts_dfloat128
};

/* A sequence of declaration specifiers in C.  */
struct c_declspecs {
  /* The type specified, if a single type specifier such as a struct,
     union or enum specifier, typedef name or typeof specifies the
     whole type, or NULL_TREE if none or a keyword such as "void" or
     "char" is used.  Does not include qualifiers.  */
  tree type;
  /* The attributes from a typedef decl.  */
  tree decl_attr;
  /* When parsing, the attributes.  Outside the parser, this will be
     NULL; attributes (possibly from multiple lists) will be passed
     separately.  */
  tree attrs;
  /* Any type specifier keyword used such as "int", not reflecting
     modifiers such as "short", or cts_none if none.  */
  enum c_typespec_keyword typespec_word;
  /* The storage class specifier, or csc_none if none.  */
  enum c_storage_class storage_class;
  /* Whether any declaration specifiers have been seen at all.  */
  BOOL_BITFIELD declspecs_seen_p : 1;
  /* Whether a type specifier has been seen.  */
  BOOL_BITFIELD type_seen_p : 1;
  /* Whether something other than a storage class specifier or
     attribute has been seen.  This is used to warn for the
     obsolescent usage of storage class specifiers other than at the
     start of the list.  (Doing this properly would require function
     specifiers to be handled separately from storage class
     specifiers.)  */
  BOOL_BITFIELD non_sc_seen_p : 1;
  /* Whether the type is specified by a typedef or typeof name.  */
  BOOL_BITFIELD typedef_p : 1;
  /* Whether a struct, union or enum type either had its content
     defined by a type specifier in the list or was the first visible
     declaration of its tag.  */
  BOOL_BITFIELD tag_defined_p : 1;
  /* Whether the type is explicitly "signed" or specified by a typedef
     whose type is explicitly "signed".  */
  BOOL_BITFIELD explicit_signed_p : 1;
  /* Whether the specifiers include a deprecated typedef.  */
  BOOL_BITFIELD deprecated_p : 1;
  /* Whether the type defaulted to "int" because there were no type
     specifiers.  */
  BOOL_BITFIELD default_int_p;
  /* Whether "long" was specified.  */
  BOOL_BITFIELD long_p : 1;
  /* Whether "long" was specified more than once.  */
  BOOL_BITFIELD long_long_p : 1;
  /* Whether "short" was specified.  */
  BOOL_BITFIELD short_p : 1;
  /* Whether "signed" was specified.  */
  BOOL_BITFIELD signed_p : 1;
  /* Whether "unsigned" was specified.  */
  BOOL_BITFIELD unsigned_p : 1;
  /* Whether "complex" was specified.  */
  BOOL_BITFIELD complex_p : 1;
  /* Whether "inline" was specified.  */
  BOOL_BITFIELD inline_p : 1;
  /* Whether "__thread" was specified.  */
  BOOL_BITFIELD thread_p : 1;
  /* Whether "const" was specified.  */
  BOOL_BITFIELD const_p : 1;
  /* Whether "volatile" was specified.  */
  BOOL_BITFIELD volatile_p : 1;
  /* Whether "restrict" was specified.  */
  BOOL_BITFIELD restrict_p : 1;
};

/* The various kinds of declarators in C.  */
enum c_declarator_kind {
  /* An identifier.  */
  cdk_id,
  /* A function.  */
  cdk_function,
  /* An array.  */
  cdk_array,
  /* A pointer.  */
  cdk_pointer,
  /* Parenthesized declarator with nested attributes.  */
  cdk_attrs
};

/* Information about the parameters in a function declarator.  */
struct c_arg_info {
  /* A list of parameter decls.  */
  tree parms;
  /* A list of structure, union and enum tags defined.  */
  tree tags;
  /* A list of argument types to go in the FUNCTION_TYPE.  */
  tree types;
  /* A list of non-parameter decls (notably enumeration constants)
     defined with the parameters.  */
  tree others;
  /* A list of VLA sizes from the parameters.  In a function
     definition, these are used to ensure that side-effects in sizes
     of arrays converted to pointers (such as a parameter int i[n++])
     take place; otherwise, they are ignored.  */
  tree pending_sizes;
  /* True when these arguments had [*].  */
  BOOL_BITFIELD had_vla_unspec : 1;
};

/* A declarator.  */
struct c_declarator {
  /* The kind of declarator.  */
  enum c_declarator_kind kind;
  /* Except for cdk_id, the contained declarator.  For cdk_id, NULL.  */
  struct c_declarator *declarator;
  location_t id_loc; /* Currently only set for cdk_id. */
  union {
    /* For identifiers, an IDENTIFIER_NODE or NULL_TREE if an abstract
       declarator.  */
    tree id;
    /* For functions.  */
    struct c_arg_info *arg_info;
    /* For arrays.  */
    struct {
      /* The array dimension, or NULL for [] and [*].  */
      tree dimen;
      /* The qualifiers inside [].  */
      int quals;
      /* The attributes (currently ignored) inside [].  */
      tree attrs;
      /* Whether [static] was used.  */
      BOOL_BITFIELD static_p : 1;
      /* Whether [*] was used.  */
      BOOL_BITFIELD vla_unspec_p : 1;
    } array;
    /* For pointers, the qualifiers on the pointer type.  */
    int pointer_quals;
    /* For attributes.  */
    tree attrs;
  } u;
};

/* A type name.  */
struct c_type_name {
  /* The declaration specifiers.  */
  struct c_declspecs *specs;
  /* The declarator.  */
  struct c_declarator *declarator;
};

/* A parameter.  */
struct c_parm {
  /* The declaration specifiers, minus any prefix attributes.  */
  struct c_declspecs *specs;
  /* The attributes.  */
  tree attrs;
  /* The declarator.  */
  struct c_declarator *declarator;
};

/* Save and restore the variables in this file and elsewhere
   that keep track of the progress of compilation of the current function.
   Used for nested functions.  */

struct language_function GTY(())
{
  struct c_language_function base;
  tree x_break_label;
  tree x_cont_label;
  struct c_switch * GTY((skip)) x_switch_stack;
  struct c_arg_info * GTY((skip)) arg_info;
  int returns_value;
  int returns_null;
  int returns_abnormally;
  int warn_about_return_type;
  int extern_inline;
};

/* Save lists of labels used or defined in particular contexts.
   Allocated on the parser obstack.  */

struct c_label_list
{
  /* The label at the head of the list.  */
  tree label;
  /* The rest of the list.  */
  struct c_label_list *next;
};

/* Statement expression context.  */

struct c_label_context_se
{
  /* The labels defined at this level of nesting.  */
  struct c_label_list *labels_def;
  /* The labels used at this level of nesting.  */
  struct c_label_list *labels_used;
  /* The next outermost context.  */
  struct c_label_context_se *next;
};

/* Context of variably modified declarations.  */

struct c_label_context_vm
{
  /* The labels defined at this level of nesting.  */
  struct c_label_list *labels_def;
  /* The labels used at this level of nesting.  */
  struct c_label_list *labels_used;
  /* The scope of this context.  Multiple contexts may be at the same
     numbered scope, since each variably modified declaration starts a
     new context.  */
  unsigned scope;
  /* The next outermost context.  */
  struct c_label_context_vm *next;
};


/* in c-parser.c */
extern void c_parse_init (void);

/* in c-aux-info.c */
extern void gen_aux_info_record (tree, int, int, int);

/* in c-decl.c */
extern struct obstack parser_obstack;
extern tree c_break_label;
extern tree c_cont_label;

extern int global_bindings_p (void);
extern void push_scope (void);
extern tree pop_scope (void);
extern void insert_block (tree);
extern void c_expand_body (tree);

extern void c_init_decl_processing (void);
extern void c_dup_lang_specific_decl (tree);
extern void c_print_identifier (FILE *, tree, int);
extern int quals_from_declspecs (const struct c_declspecs *);
extern struct c_declarator *build_array_declarator (tree, struct c_declspecs *,
						    bool, bool);
extern tree build_enumerator (tree, tree);
extern tree check_for_loop_decls (void);
extern void mark_forward_parm_decls (void);
extern void declare_parm_level (void);
extern void undeclared_variable (tree, location_t);
extern tree declare_label (tree);
extern tree define_label (location_t, tree);
extern void c_maybe_initialize_eh (void);
extern void finish_decl (tree, tree, tree);
extern tree finish_enum (tree, tree, tree);
extern void finish_function (void);
extern tree finish_struct (tree, tree, tree);
extern struct c_arg_info *get_parm_info (bool);
extern tree grokfield (struct c_declarator *, struct c_declspecs *, tree);
extern tree groktypename (struct c_type_name *);
extern tree grokparm (const struct c_parm *);
extern tree implicitly_declare (tree);
extern void keep_next_level (void);
extern void pending_xref_error (void);
extern void c_push_function_context (struct function *);
extern void c_pop_function_context (struct function *);
extern void push_parm_decl (const struct c_parm *);
extern struct c_declarator *set_array_declarator_inner (struct c_declarator *,
							struct c_declarator *,
							bool);
extern tree builtin_function (const char *, tree, int, enum built_in_class,
			      const char *, tree);
extern void shadow_tag (const struct c_declspecs *);
extern void shadow_tag_warned (const struct c_declspecs *, int);
extern tree start_enum (tree);
extern int  start_function (struct c_declspecs *, struct c_declarator *, tree);
extern tree start_decl (struct c_declarator *, struct c_declspecs *, bool,
			tree);
extern tree start_struct (enum tree_code, tree);
extern void store_parm_decls (void);
extern void store_parm_decls_from (struct c_arg_info *);
extern tree xref_tag (enum tree_code, tree);
extern struct c_typespec parser_xref_tag (enum tree_code, tree);
extern int c_expand_decl (tree);
extern struct c_parm *build_c_parm (struct c_declspecs *, tree,
				    struct c_declarator *);
extern struct c_declarator *build_attrs_declarator (tree,
						    struct c_declarator *);
extern struct c_declarator *build_function_declarator (struct c_arg_info *,
						       struct c_declarator *);
extern struct c_declarator *build_id_declarator (tree);
extern struct c_declarator *make_pointer_declarator (struct c_declspecs *,
						     struct c_declarator *);
extern struct c_declspecs *build_null_declspecs (void);
extern struct c_declspecs *declspecs_add_qual (struct c_declspecs *, tree);
extern struct c_declspecs *declspecs_add_type (struct c_declspecs *,
					       struct c_typespec);
extern struct c_declspecs *declspecs_add_scspec (struct c_declspecs *, tree);
extern struct c_declspecs *declspecs_add_attrs (struct c_declspecs *, tree);
extern struct c_declspecs *finish_declspecs (struct c_declspecs *);

/* in c-objc-common.c */
extern int c_disregard_inline_limits (tree);
extern int c_cannot_inline_tree_fn (tree *);
extern bool c_objc_common_init (void);
extern bool c_missing_noreturn_ok_p (tree);
extern tree c_objc_common_truthvalue_conversion (tree expr);
extern bool c_warn_unused_global_decl (tree);
extern void c_initialize_diagnostics (diagnostic_context *);
extern bool c_vla_unspec_p (tree x, tree fn);

#define c_build_type_variant(TYPE, CONST_P, VOLATILE_P)		  \
  c_build_qualified_type ((TYPE),				  \
			  ((CONST_P) ? TYPE_QUAL_CONST : 0) |	  \
			  ((VOLATILE_P) ? TYPE_QUAL_VOLATILE : 0))

/* in c-typeck.c */
extern int in_alignof;
extern int in_sizeof;
extern int in_typeof;

extern struct c_switch *c_switch_stack;
extern struct c_label_context_se *label_context_stack_se;
extern struct c_label_context_vm *label_context_stack_vm;

extern tree require_complete_type (tree);
extern int same_translation_unit_p (tree, tree);
extern int comptypes (tree, tree);
extern bool c_vla_type_p (tree);
extern bool c_mark_addressable (tree);
extern void c_incomplete_type_error (tree, tree);
extern tree c_type_promotes_to (tree);
extern struct c_expr default_function_array_conversion (struct c_expr);
extern tree composite_type (tree, tree);
extern tree build_component_ref (tree, tree);
extern tree build_array_ref (tree, tree);
extern tree build_external_ref (tree, int, location_t);
extern void pop_maybe_used (bool);
extern struct c_expr c_expr_sizeof_expr (struct c_expr);
extern struct c_expr c_expr_sizeof_type (struct c_type_name *);
extern struct c_expr parser_build_unary_op (enum tree_code, struct c_expr);
extern struct c_expr parser_build_binary_op (enum tree_code, struct c_expr,
					     struct c_expr);
extern tree build_conditional_expr (tree, tree, tree);
extern tree build_compound_expr (tree, tree);
extern tree c_cast_expr (struct c_type_name *, tree);
extern tree build_c_cast (tree, tree);
extern void store_init_value (tree, tree);
extern void error_init (const char *);
extern void pedwarn_init (const char *);
extern void maybe_warn_string_init (tree, struct c_expr);
extern void start_init (tree, tree, int);
extern void finish_init (void);
extern void really_start_incremental_init (tree);
extern void push_init_level (int);
extern struct c_expr pop_init_level (int);
extern void set_init_index (tree, tree);
extern void set_init_label (tree);
extern void process_init_element (struct c_expr);
extern tree build_compound_literal (tree, tree);
extern tree c_start_case (tree);
extern void c_finish_case (tree);
extern tree build_asm_expr (tree, tree, tree, tree, bool);
extern tree build_asm_stmt (tree, tree);
extern tree c_convert_parm_for_inlining (tree, tree, tree, int);
extern int c_types_compatible_p (tree, tree);
extern tree c_begin_compound_stmt (bool);
extern tree c_end_compound_stmt (tree, bool);
extern void c_finish_if_stmt (location_t, tree, tree, tree, bool);
extern void c_finish_loop (location_t, tree, tree, tree, tree, tree, bool);
extern tree c_begin_stmt_expr (void);
extern tree c_finish_stmt_expr (tree);
extern tree c_process_expr_stmt (tree);
extern tree c_finish_expr_stmt (tree);
extern tree c_finish_return (tree);
extern tree c_finish_bc_stmt (tree *, bool);
extern tree c_finish_goto_label (tree);
extern tree c_finish_goto_ptr (tree);
extern void c_begin_vm_scope (unsigned int);
extern void c_end_vm_scope (unsigned int);
extern tree c_expr_to_decl (tree, bool *, bool *, bool *);
extern tree c_begin_omp_parallel (void);
extern tree c_finish_omp_parallel (tree, tree);
extern tree c_finish_omp_clauses (tree);

/* Set to 0 at beginning of a function definition, set to 1 if
   a return statement that specifies a return value is seen.  */

extern int current_function_returns_value;

/* Set to 0 at beginning of a function definition, set to 1 if
   a return statement with no argument is seen.  */

extern int current_function_returns_null;

/* Set to 0 at beginning of a function definition, set to 1 if
   a call to a noreturn function is seen.  */

extern int current_function_returns_abnormally;

/* Nonzero means we are reading code that came from a system header file.  */

extern int system_header_p;

/* True means global_bindings_p should return false even if the scope stack
   says we are in file scope.  */

extern bool c_override_global_bindings_to_false;

/* True means we've initialized exception handling.  */
extern bool c_eh_initialized_p;

/* In c-decl.c */
extern void c_finish_incomplete_decl (tree);
extern void c_write_global_declarations (void);

/* In order for the format checking to accept the C frontend
   diagnostic framework extensions, you must include this file before
   toplev.h, not after.  */
#if GCC_VERSION >= 4001
#define ATTRIBUTE_GCC_CDIAG(m, n) __attribute__ ((__format__ (GCC_DIAG_STYLE, m ,n))) ATTRIBUTE_NONNULL(m)
#else
#define ATTRIBUTE_GCC_CDIAG(m, n) ATTRIBUTE_NONNULL(m)
#endif

extern void pedwarn_c90 (const char *, ...) ATTRIBUTE_GCC_CDIAG(1,2);
extern void pedwarn_c99 (const char *, ...) ATTRIBUTE_GCC_CDIAG(1,2);

#endif /* ! GCC_C_TREE_H */
