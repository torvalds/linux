/* Subroutines shared by all languages that are variants of C.
   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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
#include "intl.h"
#include "tree.h"
#include "flags.h"
#include "output.h"
#include "c-pragma.h"
#include "rtl.h"
#include "ggc.h"
#include "varray.h"
#include "expr.h"
#include "c-common.h"
#include "diagnostic.h"
#include "tm_p.h"
#include "obstack.h"
#include "cpplib.h"
#include "target.h"
#include "langhooks.h"
#include "tree-inline.h"
#include "c-tree.h"
#include "toplev.h"
#include "tree-iterator.h"
#include "hashtab.h"
#include "tree-mudflap.h"
#include "opts.h"
#include "real.h"
#include "cgraph.h"

cpp_reader *parse_in;		/* Declared in c-pragma.h.  */

/* We let tm.h override the types used here, to handle trivial differences
   such as the choice of unsigned int or long unsigned int for size_t.
   When machines start needing nontrivial differences in the size type,
   it would be best to do something here to figure out automatically
   from other information what type to use.  */

#ifndef SIZE_TYPE
#define SIZE_TYPE "long unsigned int"
#endif

#ifndef PID_TYPE
#define PID_TYPE "int"
#endif

#ifndef WCHAR_TYPE
#define WCHAR_TYPE "int"
#endif

/* WCHAR_TYPE gets overridden by -fshort-wchar.  */
#define MODIFIED_WCHAR_TYPE \
	(flag_short_wchar ? "short unsigned int" : WCHAR_TYPE)

#ifndef PTRDIFF_TYPE
#define PTRDIFF_TYPE "long int"
#endif

#ifndef WINT_TYPE
#define WINT_TYPE "unsigned int"
#endif

#ifndef INTMAX_TYPE
#define INTMAX_TYPE ((INT_TYPE_SIZE == LONG_LONG_TYPE_SIZE)	\
		     ? "int"					\
		     : ((LONG_TYPE_SIZE == LONG_LONG_TYPE_SIZE)	\
			? "long int"				\
			: "long long int"))
#endif

#ifndef UINTMAX_TYPE
#define UINTMAX_TYPE ((INT_TYPE_SIZE == LONG_LONG_TYPE_SIZE)	\
		     ? "unsigned int"				\
		     : ((LONG_TYPE_SIZE == LONG_LONG_TYPE_SIZE)	\
			? "long unsigned int"			\
			: "long long unsigned int"))
#endif

/* The following symbols are subsumed in the c_global_trees array, and
   listed here individually for documentation purposes.

   INTEGER_TYPE and REAL_TYPE nodes for the standard data types.

	tree short_integer_type_node;
	tree long_integer_type_node;
	tree long_long_integer_type_node;

	tree short_unsigned_type_node;
	tree long_unsigned_type_node;
	tree long_long_unsigned_type_node;

	tree truthvalue_type_node;
	tree truthvalue_false_node;
	tree truthvalue_true_node;

	tree ptrdiff_type_node;

	tree unsigned_char_type_node;
	tree signed_char_type_node;
	tree wchar_type_node;
	tree signed_wchar_type_node;
	tree unsigned_wchar_type_node;

	tree float_type_node;
	tree double_type_node;
	tree long_double_type_node;

	tree complex_integer_type_node;
	tree complex_float_type_node;
	tree complex_double_type_node;
	tree complex_long_double_type_node;

	tree dfloat32_type_node;
	tree dfloat64_type_node;
	tree_dfloat128_type_node;

	tree intQI_type_node;
	tree intHI_type_node;
	tree intSI_type_node;
	tree intDI_type_node;
	tree intTI_type_node;

	tree unsigned_intQI_type_node;
	tree unsigned_intHI_type_node;
	tree unsigned_intSI_type_node;
	tree unsigned_intDI_type_node;
	tree unsigned_intTI_type_node;

	tree widest_integer_literal_type_node;
	tree widest_unsigned_literal_type_node;

   Nodes for types `void *' and `const void *'.

	tree ptr_type_node, const_ptr_type_node;

   Nodes for types `char *' and `const char *'.

	tree string_type_node, const_string_type_node;

   Type `char[SOMENUMBER]'.
   Used when an array of char is needed and the size is irrelevant.

	tree char_array_type_node;

   Type `int[SOMENUMBER]' or something like it.
   Used when an array of int needed and the size is irrelevant.

	tree int_array_type_node;

   Type `wchar_t[SOMENUMBER]' or something like it.
   Used when a wide string literal is created.

	tree wchar_array_type_node;

   Type `int ()' -- used for implicit declaration of functions.

	tree default_function_type;

   A VOID_TYPE node, packaged in a TREE_LIST.

	tree void_list_node;

  The lazily created VAR_DECLs for __FUNCTION__, __PRETTY_FUNCTION__,
  and __func__. (C doesn't generate __FUNCTION__ and__PRETTY_FUNCTION__
  VAR_DECLS, but C++ does.)

	tree function_name_decl_node;
	tree pretty_function_name_decl_node;
	tree c99_function_name_decl_node;

  Stack of nested function name VAR_DECLs.

	tree saved_function_name_decls;

*/

tree c_global_trees[CTI_MAX];

/* Switches common to the C front ends.  */

/* Nonzero if prepreprocessing only.  */

int flag_preprocess_only;

/* Nonzero means don't output line number information.  */

char flag_no_line_commands;

/* Nonzero causes -E output not to be done, but directives such as
   #define that have side effects are still obeyed.  */

char flag_no_output;

/* Nonzero means dump macros in some fashion.  */

char flag_dump_macros;

/* Nonzero means pass #include lines through to the output.  */

char flag_dump_includes;

/* Nonzero means process PCH files while preprocessing.  */

bool flag_pch_preprocess;

/* The file name to which we should write a precompiled header, or
   NULL if no header will be written in this compile.  */

const char *pch_file;

/* Nonzero if an ISO standard was selected.  It rejects macros in the
   user's namespace.  */
int flag_iso;

/* Nonzero if -undef was given.  It suppresses target built-in macros
   and assertions.  */
int flag_undef;

/* Nonzero means don't recognize the non-ANSI builtin functions.  */

int flag_no_builtin;

/* Nonzero means don't recognize the non-ANSI builtin functions.
   -ansi sets this.  */

int flag_no_nonansi_builtin;

/* Nonzero means give `double' the same size as `float'.  */

int flag_short_double;

/* Nonzero means give `wchar_t' the same size as `short'.  */

int flag_short_wchar;

/* Nonzero means allow Microsoft extensions without warnings or errors.  */
int flag_ms_extensions;

/* Nonzero means don't recognize the keyword `asm'.  */

int flag_no_asm;

/* Nonzero means to treat bitfields as signed unless they say `unsigned'.  */

int flag_signed_bitfields = 1;

/* Warn about #pragma directives that are not recognized.  */

int warn_unknown_pragmas; /* Tri state variable.  */

/* Warn about format/argument anomalies in calls to formatted I/O functions
   (*printf, *scanf, strftime, strfmon, etc.).  */

int warn_format;

/* Warn about using __null (as NULL in C++) as sentinel.  For code compiled
   with GCC this doesn't matter as __null is guaranteed to have the right
   size.  */

int warn_strict_null_sentinel;

/* Zero means that faster, ...NonNil variants of objc_msgSend...
   calls will be used in ObjC; passing nil receivers to such calls
   will most likely result in crashes.  */
int flag_nil_receivers = 1;

/* Nonzero means that code generation will be altered to support
   "zero-link" execution.  This currently affects ObjC only, but may
   affect other languages in the future.  */
int flag_zero_link = 0;

/* Nonzero means emit an '__OBJC, __image_info' for the current translation
   unit.  It will inform the ObjC runtime that class definition(s) herein
   contained are to replace one(s) previously loaded.  */
int flag_replace_objc_classes = 0;

/* C/ObjC language option variables.  */


/* Nonzero means allow type mismatches in conditional expressions;
   just make their values `void'.  */

int flag_cond_mismatch;

/* Nonzero means enable C89 Amendment 1 features.  */

int flag_isoc94;

/* Nonzero means use the ISO C99 dialect of C.  */

int flag_isoc99;

/* Nonzero means that we have builtin functions, and main is an int.  */

int flag_hosted = 1;

/* Warn if main is suspicious.  */

int warn_main;


/* ObjC language option variables.  */


/* Open and close the file for outputting class declarations, if
   requested (ObjC).  */

int flag_gen_declaration;

/* Tells the compiler that this is a special run.  Do not perform any
   compiling, instead we are to test some platform dependent features
   and output a C header file with appropriate definitions.  */

int print_struct_values;

/* Tells the compiler what is the constant string class for Objc.  */

const char *constant_string_class_name;


/* C++ language option variables.  */


/* Nonzero means don't recognize any extension keywords.  */

int flag_no_gnu_keywords;

/* Nonzero means do emit exported implementations of functions even if
   they can be inlined.  */

int flag_implement_inlines = 1;

/* Nonzero means that implicit instantiations will be emitted if needed.  */

int flag_implicit_templates = 1;

/* Nonzero means that implicit instantiations of inline templates will be
   emitted if needed, even if instantiations of non-inline templates
   aren't.  */

int flag_implicit_inline_templates = 1;

/* Nonzero means generate separate instantiation control files and
   juggle them at link time.  */

int flag_use_repository;

/* Nonzero if we want to issue diagnostics that the standard says are not
   required.  */

int flag_optional_diags = 1;

/* Nonzero means we should attempt to elide constructors when possible.  */

int flag_elide_constructors = 1;

/* Nonzero means that member functions defined in class scope are
   inline by default.  */

int flag_default_inline = 1;

/* Controls whether compiler generates 'type descriptor' that give
   run-time type information.  */

int flag_rtti = 1;

/* Nonzero if we want to conserve space in the .o files.  We do this
   by putting uninitialized data and runtime initialized data into
   .common instead of .data at the expense of not flagging multiple
   definitions.  */

int flag_conserve_space;

/* Nonzero if we want to obey access control semantics.  */

int flag_access_control = 1;

/* Nonzero if we want to check the return value of new and avoid calling
   constructors if it is a null pointer.  */

int flag_check_new;

/* Nonzero if we want the new ISO rules for pushing a new scope for `for'
   initialization variables.
   0: Old rules, set by -fno-for-scope.
   2: New ISO rules, set by -ffor-scope.
   1: Try to implement new ISO rules, but with backup compatibility
   (and warnings).  This is the default, for now.  */

int flag_new_for_scope = 1;

/* Nonzero if we want to emit defined symbols with common-like linkage as
   weak symbols where possible, in order to conform to C++ semantics.
   Otherwise, emit them as local symbols.  */

int flag_weak = 1;

/* 0 means we want the preprocessor to not emit line directives for
   the current working directory.  1 means we want it to do it.  -1
   means we should decide depending on whether debugging information
   is being emitted or not.  */

int flag_working_directory = -1;

/* Nonzero to use __cxa_atexit, rather than atexit, to register
   destructors for local statics and global objects.  '2' means it has been
   set nonzero as a default, not by a command-line flag.  */

int flag_use_cxa_atexit = DEFAULT_USE_CXA_ATEXIT;

/* Nonzero to use __cxa_get_exception_ptr in C++ exception-handling
   code.  '2' means it has not been set explicitly on the command line.  */

int flag_use_cxa_get_exception_ptr = 2;

/* Nonzero means make the default pedwarns warnings instead of errors.
   The value of this flag is ignored if -pedantic is specified.  */

int flag_permissive;

/* Nonzero means to implement standard semantics for exception
   specifications, calling unexpected if an exception is thrown that
   doesn't match the specification.  Zero means to treat them as
   assertions and optimize accordingly, but not check them.  */

int flag_enforce_eh_specs = 1;

/* Nonzero means to generate thread-safe code for initializing local
   statics.  */

int flag_threadsafe_statics = 1;

/* Nonzero means warn about implicit declarations.  */

int warn_implicit = 1;

/* Maximum template instantiation depth.  This limit is rather
   arbitrary, but it exists to limit the time it takes to notice
   infinite template instantiations.  */

int max_tinst_depth = 500;



/* The elements of `ridpointers' are identifier nodes for the reserved
   type names and storage classes.  It is indexed by a RID_... value.  */
tree *ridpointers;

tree (*make_fname_decl) (tree, int);

/* Nonzero means the expression being parsed will never be evaluated.
   This is a count, since unevaluated expressions can nest.  */
int skip_evaluation;

/* Information about how a function name is generated.  */
struct fname_var_t
{
  tree *const decl;	/* pointer to the VAR_DECL.  */
  const unsigned rid;	/* RID number for the identifier.  */
  const int pretty;	/* How pretty is it? */
};

/* The three ways of getting then name of the current function.  */

const struct fname_var_t fname_vars[] =
{
  /* C99 compliant __func__, must be first.  */
  {&c99_function_name_decl_node, RID_C99_FUNCTION_NAME, 0},
  /* GCC __FUNCTION__ compliant.  */
  {&function_name_decl_node, RID_FUNCTION_NAME, 0},
  /* GCC __PRETTY_FUNCTION__ compliant.  */
  {&pretty_function_name_decl_node, RID_PRETTY_FUNCTION_NAME, 1},
  {NULL, 0, 0},
};

static int constant_fits_type_p (tree, tree);
static tree check_case_value (tree);
static bool check_case_bounds (tree, tree, tree *, tree *);

static tree handle_packed_attribute (tree *, tree, tree, int, bool *);
static tree handle_nocommon_attribute (tree *, tree, tree, int, bool *);
static tree handle_common_attribute (tree *, tree, tree, int, bool *);
static tree handle_noreturn_attribute (tree *, tree, tree, int, bool *);
static tree handle_noinline_attribute (tree *, tree, tree, int, bool *);
static tree handle_always_inline_attribute (tree *, tree, tree, int,
					    bool *);
static tree handle_gnu_inline_attribute (tree *, tree, tree, int,
					 bool *);
static tree handle_flatten_attribute (tree *, tree, tree, int, bool *);
static tree handle_used_attribute (tree *, tree, tree, int, bool *);
static tree handle_unused_attribute (tree *, tree, tree, int, bool *);
static tree handle_externally_visible_attribute (tree *, tree, tree, int,
						 bool *);
static tree handle_const_attribute (tree *, tree, tree, int, bool *);
static tree handle_transparent_union_attribute (tree *, tree, tree,
						int, bool *);
static tree handle_constructor_attribute (tree *, tree, tree, int, bool *);
static tree handle_destructor_attribute (tree *, tree, tree, int, bool *);
static tree handle_mode_attribute (tree *, tree, tree, int, bool *);
static tree handle_section_attribute (tree *, tree, tree, int, bool *);
static tree handle_aligned_attribute (tree *, tree, tree, int, bool *);
static tree handle_weak_attribute (tree *, tree, tree, int, bool *) ;
static tree handle_alias_attribute (tree *, tree, tree, int, bool *);
static tree handle_weakref_attribute (tree *, tree, tree, int, bool *) ;
static tree handle_visibility_attribute (tree *, tree, tree, int,
					 bool *);
static tree handle_tls_model_attribute (tree *, tree, tree, int,
					bool *);
static tree handle_no_instrument_function_attribute (tree *, tree,
						     tree, int, bool *);
static tree handle_malloc_attribute (tree *, tree, tree, int, bool *);
static tree handle_returns_twice_attribute (tree *, tree, tree, int, bool *);
static tree handle_no_limit_stack_attribute (tree *, tree, tree, int,
					     bool *);
static tree handle_pure_attribute (tree *, tree, tree, int, bool *);
static tree handle_novops_attribute (tree *, tree, tree, int, bool *);
static tree handle_deprecated_attribute (tree *, tree, tree, int,
					 bool *);
static tree handle_vector_size_attribute (tree *, tree, tree, int,
					  bool *);
static tree handle_nonnull_attribute (tree *, tree, tree, int, bool *);
static tree handle_nothrow_attribute (tree *, tree, tree, int, bool *);
static tree handle_cleanup_attribute (tree *, tree, tree, int, bool *);
static tree handle_warn_unused_result_attribute (tree *, tree, tree, int,
						 bool *);
static tree handle_sentinel_attribute (tree *, tree, tree, int, bool *);

static void check_function_nonnull (tree, tree);
static void check_nonnull_arg (void *, tree, unsigned HOST_WIDE_INT);
static bool nonnull_check_p (tree, unsigned HOST_WIDE_INT);
static bool get_nonnull_operand (tree, unsigned HOST_WIDE_INT *);
static int resort_field_decl_cmp (const void *, const void *);

/* Table of machine-independent attributes common to all C-like languages.  */
const struct attribute_spec c_common_attribute_table[] =
{
  /* { name, min_len, max_len, decl_req, type_req, fn_type_req, handler } */
  { "packed",                 0, 0, false, false, false,
			      handle_packed_attribute },
  { "nocommon",               0, 0, true,  false, false,
			      handle_nocommon_attribute },
  { "common",                 0, 0, true,  false, false,
			      handle_common_attribute },
  /* FIXME: logically, noreturn attributes should be listed as
     "false, true, true" and apply to function types.  But implementing this
     would require all the places in the compiler that use TREE_THIS_VOLATILE
     on a decl to identify non-returning functions to be located and fixed
     to check the function type instead.  */
  { "noreturn",               0, 0, true,  false, false,
			      handle_noreturn_attribute },
  { "volatile",               0, 0, true,  false, false,
			      handle_noreturn_attribute },
  { "noinline",               0, 0, true,  false, false,
			      handle_noinline_attribute },
  { "always_inline",          0, 0, true,  false, false,
			      handle_always_inline_attribute },
  { "gnu_inline",             0, 0, true,  false, false,
			      handle_gnu_inline_attribute },
  { "flatten",                0, 0, true,  false, false,
			      handle_flatten_attribute },
  { "used",                   0, 0, true,  false, false,
			      handle_used_attribute },
  { "unused",                 0, 0, false, false, false,
			      handle_unused_attribute },
  { "externally_visible",     0, 0, true,  false, false,
			      handle_externally_visible_attribute },
  /* The same comments as for noreturn attributes apply to const ones.  */
  { "const",                  0, 0, true,  false, false,
			      handle_const_attribute },
  { "transparent_union",      0, 0, false, false, false,
			      handle_transparent_union_attribute },
  { "constructor",            0, 0, true,  false, false,
			      handle_constructor_attribute },
  { "destructor",             0, 0, true,  false, false,
			      handle_destructor_attribute },
  { "mode",                   1, 1, false,  true, false,
			      handle_mode_attribute },
  { "section",                1, 1, true,  false, false,
			      handle_section_attribute },
  { "aligned",                0, 1, false, false, false,
			      handle_aligned_attribute },
  { "weak",                   0, 0, true,  false, false,
			      handle_weak_attribute },
  { "alias",                  1, 1, true,  false, false,
			      handle_alias_attribute },
  { "weakref",                0, 1, true,  false, false,
			      handle_weakref_attribute },
  { "no_instrument_function", 0, 0, true,  false, false,
			      handle_no_instrument_function_attribute },
  { "malloc",                 0, 0, true,  false, false,
			      handle_malloc_attribute },
  { "returns_twice",          0, 0, true,  false, false,
			      handle_returns_twice_attribute },
  { "no_stack_limit",         0, 0, true,  false, false,
			      handle_no_limit_stack_attribute },
  { "pure",                   0, 0, true,  false, false,
			      handle_pure_attribute },
  /* For internal use (marking of builtins) only.  The name contains space
     to prevent its usage in source code.  */
  { "no vops",                0, 0, true,  false, false,
			      handle_novops_attribute },
  { "deprecated",             0, 0, false, false, false,
			      handle_deprecated_attribute },
  { "vector_size",	      1, 1, false, true, false,
			      handle_vector_size_attribute },
  { "bounded",		      3, 4, false, true, false, 
			      handle_bounded_attribute },
  { "visibility",	      1, 1, false, false, false,
			      handle_visibility_attribute },
  { "tls_model",	      1, 1, true,  false, false,
			      handle_tls_model_attribute },
  { "nonnull",                0, -1, false, true, true,
			      handle_nonnull_attribute },
  { "nothrow",                0, 0, true,  false, false,
			      handle_nothrow_attribute },
  { "may_alias",	      0, 0, false, true, false, NULL },
  { "cleanup",		      1, 1, true, false, false,
			      handle_cleanup_attribute },
  { "warn_unused_result",     0, 0, false, true, true,
			      handle_warn_unused_result_attribute },
  { "sentinel",               0, 1, false, true, true,
			      handle_sentinel_attribute },
  { NULL,                     0, 0, false, false, false, NULL }
};

/* Give the specifications for the format attributes, used by C and all
   descendants.  */

const struct attribute_spec c_common_format_attribute_table[] =
{
  /* { name, min_len, max_len, decl_req, type_req, fn_type_req, handler } */
  { "format",                 3, 3, false, true,  true,
			      handle_format_attribute },
  { "format_arg",             1, 1, false, true,  true,
			      handle_format_arg_attribute },
  { NULL,                     0, 0, false, false, false, NULL }
};

/* Push current bindings for the function name VAR_DECLS.  */

void
start_fname_decls (void)
{
  unsigned ix;
  tree saved = NULL_TREE;

  for (ix = 0; fname_vars[ix].decl; ix++)
    {
      tree decl = *fname_vars[ix].decl;

      if (decl)
	{
	  saved = tree_cons (decl, build_int_cst (NULL_TREE, ix), saved);
	  *fname_vars[ix].decl = NULL_TREE;
	}
    }
  if (saved || saved_function_name_decls)
    /* Normally they'll have been NULL, so only push if we've got a
       stack, or they are non-NULL.  */
    saved_function_name_decls = tree_cons (saved, NULL_TREE,
					   saved_function_name_decls);
}

/* Finish up the current bindings, adding them into the current function's
   statement tree.  This must be done _before_ finish_stmt_tree is called.
   If there is no current function, we must be at file scope and no statements
   are involved. Pop the previous bindings.  */

void
finish_fname_decls (void)
{
  unsigned ix;
  tree stmts = NULL_TREE;
  tree stack = saved_function_name_decls;

  for (; stack && TREE_VALUE (stack); stack = TREE_CHAIN (stack))
    append_to_statement_list (TREE_VALUE (stack), &stmts);

  if (stmts)
    {
      tree *bodyp = &DECL_SAVED_TREE (current_function_decl);

      if (TREE_CODE (*bodyp) == BIND_EXPR)
	bodyp = &BIND_EXPR_BODY (*bodyp);

      append_to_statement_list_force (*bodyp, &stmts);
      *bodyp = stmts;
    }

  for (ix = 0; fname_vars[ix].decl; ix++)
    *fname_vars[ix].decl = NULL_TREE;

  if (stack)
    {
      /* We had saved values, restore them.  */
      tree saved;

      for (saved = TREE_PURPOSE (stack); saved; saved = TREE_CHAIN (saved))
	{
	  tree decl = TREE_PURPOSE (saved);
	  unsigned ix = TREE_INT_CST_LOW (TREE_VALUE (saved));

	  *fname_vars[ix].decl = decl;
	}
      stack = TREE_CHAIN (stack);
    }
  saved_function_name_decls = stack;
}

/* Return the text name of the current function, suitably prettified
   by PRETTY_P.  Return string must be freed by caller.  */

const char *
fname_as_string (int pretty_p)
{
  const char *name = "top level";
  char *namep;
  int vrb = 2;

  if (!pretty_p)
    {
      name = "";
      vrb = 0;
    }

  if (current_function_decl)
    name = lang_hooks.decl_printable_name (current_function_decl, vrb);

  if (c_lex_string_translate)
    {
      int len = strlen (name) + 3; /* Two for '"'s.  One for NULL.  */
      cpp_string cstr = { 0, 0 }, strname;

      namep = XNEWVEC (char, len);
      snprintf (namep, len, "\"%s\"", name);
      strname.text = (unsigned char *) namep;
      strname.len = len - 1;

      if (cpp_interpret_string (parse_in, &strname, 1, &cstr, false))
	{
	  XDELETEVEC (namep);
	  return (char *) cstr.text;
	}
    }
  else
    namep = xstrdup (name);

  return namep;
}

/* Expand DECL if it declares an entity not handled by the
   common code.  */

int
c_expand_decl (tree decl)
{
  if (TREE_CODE (decl) == VAR_DECL && !TREE_STATIC (decl))
    {
      /* Let the back-end know about this variable.  */
      if (!anon_aggr_type_p (TREE_TYPE (decl)))
	emit_local_var (decl);
      else
	expand_anon_union_decl (decl, NULL_TREE,
				DECL_ANON_UNION_ELEMS (decl));
    }
  else
    return 0;

  return 1;
}


/* Return the VAR_DECL for a const char array naming the current
   function. If the VAR_DECL has not yet been created, create it
   now. RID indicates how it should be formatted and IDENTIFIER_NODE
   ID is its name (unfortunately C and C++ hold the RID values of
   keywords in different places, so we can't derive RID from ID in
   this language independent code.  */

tree
fname_decl (unsigned int rid, tree id)
{
  unsigned ix;
  tree decl = NULL_TREE;

  for (ix = 0; fname_vars[ix].decl; ix++)
    if (fname_vars[ix].rid == rid)
      break;

  decl = *fname_vars[ix].decl;
  if (!decl)
    {
      /* If a tree is built here, it would normally have the lineno of
	 the current statement.  Later this tree will be moved to the
	 beginning of the function and this line number will be wrong.
	 To avoid this problem set the lineno to 0 here; that prevents
	 it from appearing in the RTL.  */
      tree stmts;
      location_t saved_location = input_location;
#ifdef USE_MAPPED_LOCATION
      input_location = UNKNOWN_LOCATION;
#else
      input_line = 0;
#endif

      stmts = push_stmt_list ();
      decl = (*make_fname_decl) (id, fname_vars[ix].pretty);
      stmts = pop_stmt_list (stmts);
      if (!IS_EMPTY_STMT (stmts))
	saved_function_name_decls
	  = tree_cons (decl, stmts, saved_function_name_decls);
      *fname_vars[ix].decl = decl;
      input_location = saved_location;
    }
  if (!ix && !current_function_decl)
    pedwarn ("%qD is not defined outside of function scope", decl);

  return decl;
}

/* Given a STRING_CST, give it a suitable array-of-chars data type.  */

tree
fix_string_type (tree value)
{
  const int wchar_bytes = TYPE_PRECISION (wchar_type_node) / BITS_PER_UNIT;
  const int wide_flag = TREE_TYPE (value) == wchar_array_type_node;
  int length = TREE_STRING_LENGTH (value);
  int nchars;
  tree e_type, i_type, a_type;

  /* Compute the number of elements, for the array type.  */
  nchars = wide_flag ? length / wchar_bytes : length;

  /* C89 2.2.4.1, C99 5.2.4.1 (Translation limits).  The analogous
     limit in C++98 Annex B is very large (65536) and is not normative,
     so we do not diagnose it (warn_overlength_strings is forced off
     in c_common_post_options).  */
  if (warn_overlength_strings)
    {
      const int nchars_max = flag_isoc99 ? 4095 : 509;
      const int relevant_std = flag_isoc99 ? 99 : 90;
      if (nchars - 1 > nchars_max)
	/* Translators: The %d after 'ISO C' will be 90 or 99.  Do not
	   separate the %d from the 'C'.  'ISO' should not be
	   translated, but it may be moved after 'C%d' in languages
	   where modifiers follow nouns.  */
	pedwarn ("string length %qd is greater than the length %qd "
		 "ISO C%d compilers are required to support",
		 nchars - 1, nchars_max, relevant_std);
    }

  /* Create the array type for the string constant.  The ISO C++
     standard says that a string literal has type `const char[N]' or
     `const wchar_t[N]'.  We use the same logic when invoked as a C
     front-end with -Wwrite-strings.
     ??? We should change the type of an expression depending on the
     state of a warning flag.  We should just be warning -- see how
     this is handled in the C++ front-end for the deprecated implicit
     conversion from string literals to `char*' or `wchar_t*'.

     The C++ front end relies on TYPE_MAIN_VARIANT of a cv-qualified
     array type being the unqualified version of that type.
     Therefore, if we are constructing an array of const char, we must
     construct the matching unqualified array type first.  The C front
     end does not require this, but it does no harm, so we do it
     unconditionally.  */
  e_type = wide_flag ? wchar_type_node : char_type_node;
  i_type = build_index_type (build_int_cst (NULL_TREE, nchars - 1));
  a_type = build_array_type (e_type, i_type);
  if (c_dialect_cxx() || warn_write_strings)
    a_type = c_build_qualified_type (a_type, TYPE_QUAL_CONST);

  TREE_TYPE (value) = a_type;
  TREE_CONSTANT (value) = 1;
  TREE_INVARIANT (value) = 1;
  TREE_READONLY (value) = 1;
  TREE_STATIC (value) = 1;
  return value;
}

/* Print a warning if a constant expression had overflow in folding.
   Invoke this function on every expression that the language
   requires to be a constant expression.
   Note the ANSI C standard says it is erroneous for a
   constant expression to overflow.  */

void
constant_expression_warning (tree value)
{
  if ((TREE_CODE (value) == INTEGER_CST || TREE_CODE (value) == REAL_CST
       || TREE_CODE (value) == VECTOR_CST
       || TREE_CODE (value) == COMPLEX_CST)
      && TREE_CONSTANT_OVERFLOW (value)
      && warn_overflow
      && pedantic)
    pedwarn ("overflow in constant expression");
}

/* Print a warning if an expression had overflow in folding.
   Invoke this function on every expression that
   (1) appears in the source code, and
   (2) might be a constant expression that overflowed, and
   (3) is not already checked by convert_and_check;
   however, do not invoke this function on operands of explicit casts.  */

void
overflow_warning (tree value)
{
  if ((TREE_CODE (value) == INTEGER_CST
       || (TREE_CODE (value) == COMPLEX_CST
	   && TREE_CODE (TREE_REALPART (value)) == INTEGER_CST))
      && TREE_OVERFLOW (value))
    {
      TREE_OVERFLOW (value) = 0;
      if (skip_evaluation == 0)
	warning (OPT_Woverflow, "integer overflow in expression");
    }
  else if ((TREE_CODE (value) == REAL_CST
	    || (TREE_CODE (value) == COMPLEX_CST
		&& TREE_CODE (TREE_REALPART (value)) == REAL_CST))
	   && TREE_OVERFLOW (value))
    {
      TREE_OVERFLOW (value) = 0;
      if (skip_evaluation == 0)
	warning (OPT_Woverflow, "floating point overflow in expression");
    }
  else if (TREE_CODE (value) == VECTOR_CST && TREE_OVERFLOW (value))
    {
      TREE_OVERFLOW (value) = 0;
      if (skip_evaluation == 0)
	warning (OPT_Woverflow, "vector overflow in expression");
    }
}

/* Print a warning if a large constant is truncated to unsigned,
   or if -Wconversion is used and a constant < 0 is converted to unsigned.
   Invoke this function on every expression that might be implicitly
   converted to an unsigned type.  */

static void
unsigned_conversion_warning (tree result, tree operand)
{
  tree type = TREE_TYPE (result);

  if (TREE_CODE (operand) == INTEGER_CST
      && TREE_CODE (type) == INTEGER_TYPE
      && TYPE_UNSIGNED (type)
      && skip_evaluation == 0
      && !int_fits_type_p (operand, type))
    {
      if (!int_fits_type_p (operand, c_common_signed_type (type)))
	/* This detects cases like converting -129 or 256 to unsigned char.  */
	warning (OPT_Woverflow,
		 "large integer implicitly truncated to unsigned type");
      else
	warning (OPT_Wconversion,
		 "negative integer implicitly converted to unsigned type");
    }
}

/* Print a warning about casts that might indicate violation
   of strict aliasing rules if -Wstrict-aliasing is used and
   strict aliasing mode is in effect. OTYPE is the original
   TREE_TYPE of EXPR, and TYPE the type we're casting to. */

void
strict_aliasing_warning (tree otype, tree type, tree expr)
{
  if (flag_strict_aliasing && warn_strict_aliasing
      && POINTER_TYPE_P (type) && POINTER_TYPE_P (otype)
      && TREE_CODE (expr) == ADDR_EXPR
      && (DECL_P (TREE_OPERAND (expr, 0))
          || handled_component_p (TREE_OPERAND (expr, 0)))
      && !VOID_TYPE_P (TREE_TYPE (type)))
    {
      /* Casting the address of an object to non void pointer. Warn
         if the cast breaks type based aliasing.  */
      if (!COMPLETE_TYPE_P (TREE_TYPE (type)))
        warning (OPT_Wstrict_aliasing, "type-punning to incomplete type "
                 "might break strict-aliasing rules");
      else
        {
          HOST_WIDE_INT set1 = get_alias_set (TREE_TYPE (TREE_OPERAND (expr, 0)));
          HOST_WIDE_INT set2 = get_alias_set (TREE_TYPE (type));

          if (!alias_sets_conflict_p (set1, set2))
            warning (OPT_Wstrict_aliasing, "dereferencing type-punned "
                     "pointer will break strict-aliasing rules");
          else if (warn_strict_aliasing > 1
                  && !alias_sets_might_conflict_p (set1, set2))
            warning (OPT_Wstrict_aliasing, "dereferencing type-punned "
                     "pointer might break strict-aliasing rules");
        }
    }
}


/* Print a warning about if (); or if () .. else; constructs
   via the special empty statement node that we create.  INNER_THEN
   and INNER_ELSE are the statement lists of the if and the else
   block.  */

void
empty_body_warning (tree inner_then, tree inner_else)
{
  if (extra_warnings)
    {
      if (TREE_CODE (inner_then) == STATEMENT_LIST
	  && STATEMENT_LIST_TAIL (inner_then))
	inner_then = STATEMENT_LIST_TAIL (inner_then)->stmt;

      if (inner_else && TREE_CODE (inner_else) == STATEMENT_LIST
	  && STATEMENT_LIST_TAIL (inner_else))
	inner_else = STATEMENT_LIST_TAIL (inner_else)->stmt;

      if (IS_EMPTY_STMT (inner_then) && !inner_else)
	warning (OPT_Wextra, "%Hempty body in an if-statement",
		 EXPR_LOCUS (inner_then));

      if (inner_else && IS_EMPTY_STMT (inner_else))
	warning (OPT_Wextra, "%Hempty body in an else-statement",
		 EXPR_LOCUS (inner_else));
   }
}

  
/* Nonzero if constant C has a value that is permissible
   for type TYPE (an INTEGER_TYPE).  */

static int
constant_fits_type_p (tree c, tree type)
{
  if (TREE_CODE (c) == INTEGER_CST)
    return int_fits_type_p (c, type);

  c = convert (type, c);
  return !TREE_OVERFLOW (c);
}

/* Nonzero if vector types T1 and T2 can be converted to each other
   without an explicit cast.  */
int
vector_types_convertible_p (tree t1, tree t2)
{
  return targetm.vector_opaque_p (t1)
	 || targetm.vector_opaque_p (t2)
	 || (tree_int_cst_equal (TYPE_SIZE (t1), TYPE_SIZE (t2))
	     && (TREE_CODE (TREE_TYPE (t1)) != REAL_TYPE ||
		 TYPE_PRECISION (t1) == TYPE_PRECISION (t2))
	     && INTEGRAL_TYPE_P (TREE_TYPE (t1))
		== INTEGRAL_TYPE_P (TREE_TYPE (t2)));
}

/* Convert EXPR to TYPE, warning about conversion problems with constants.
   Invoke this function on every expression that is converted implicitly,
   i.e. because of language rules and not because of an explicit cast.  */

tree
convert_and_check (tree type, tree expr)
{
  tree t = convert (type, expr);
  if (TREE_CODE (t) == INTEGER_CST)
    {
      if (TREE_OVERFLOW (t))
	{
	  TREE_OVERFLOW (t) = 0;

	  /* Do not diagnose overflow in a constant expression merely
	     because a conversion overflowed.  */
	  TREE_CONSTANT_OVERFLOW (t) = CONSTANT_CLASS_P (expr)
                                       && TREE_CONSTANT_OVERFLOW (expr);

	  /* No warning for converting 0x80000000 to int.  */
	  if (!(TYPE_UNSIGNED (type) < TYPE_UNSIGNED (TREE_TYPE (expr))
		&& TREE_CODE (TREE_TYPE (expr)) == INTEGER_TYPE
		&& TYPE_PRECISION (type) == TYPE_PRECISION (TREE_TYPE (expr))))
	    /* If EXPR fits in the unsigned version of TYPE,
	       don't warn unless pedantic.  */
	    if ((pedantic
		 || TYPE_UNSIGNED (type)
		 || !constant_fits_type_p (expr,
					   c_common_unsigned_type (type)))
		&& skip_evaluation == 0)
	      warning (OPT_Woverflow,
                       "overflow in implicit constant conversion");
	}
      else
	unsigned_conversion_warning (t, expr);
    }
  return t;
}

/* A node in a list that describes references to variables (EXPR), which are
   either read accesses if WRITER is zero, or write accesses, in which case
   WRITER is the parent of EXPR.  */
struct tlist
{
  struct tlist *next;
  tree expr, writer;
};

/* Used to implement a cache the results of a call to verify_tree.  We only
   use this for SAVE_EXPRs.  */
struct tlist_cache
{
  struct tlist_cache *next;
  struct tlist *cache_before_sp;
  struct tlist *cache_after_sp;
  tree expr;
};

/* Obstack to use when allocating tlist structures, and corresponding
   firstobj.  */
static struct obstack tlist_obstack;
static char *tlist_firstobj = 0;

/* Keep track of the identifiers we've warned about, so we can avoid duplicate
   warnings.  */
static struct tlist *warned_ids;
/* SAVE_EXPRs need special treatment.  We process them only once and then
   cache the results.  */
static struct tlist_cache *save_expr_cache;

static void add_tlist (struct tlist **, struct tlist *, tree, int);
static void merge_tlist (struct tlist **, struct tlist *, int);
static void verify_tree (tree, struct tlist **, struct tlist **, tree);
static int warning_candidate_p (tree);
static void warn_for_collisions (struct tlist *);
static void warn_for_collisions_1 (tree, tree, struct tlist *, int);
static struct tlist *new_tlist (struct tlist *, tree, tree);

/* Create a new struct tlist and fill in its fields.  */
static struct tlist *
new_tlist (struct tlist *next, tree t, tree writer)
{
  struct tlist *l;
  l = XOBNEW (&tlist_obstack, struct tlist);
  l->next = next;
  l->expr = t;
  l->writer = writer;
  return l;
}

/* Add duplicates of the nodes found in ADD to the list *TO.  If EXCLUDE_WRITER
   is nonnull, we ignore any node we find which has a writer equal to it.  */

static void
add_tlist (struct tlist **to, struct tlist *add, tree exclude_writer, int copy)
{
  while (add)
    {
      struct tlist *next = add->next;
      if (!copy)
	add->next = *to;
      if (!exclude_writer || add->writer != exclude_writer)
	*to = copy ? new_tlist (*to, add->expr, add->writer) : add;
      add = next;
    }
}

/* Merge the nodes of ADD into TO.  This merging process is done so that for
   each variable that already exists in TO, no new node is added; however if
   there is a write access recorded in ADD, and an occurrence on TO is only
   a read access, then the occurrence in TO will be modified to record the
   write.  */

static void
merge_tlist (struct tlist **to, struct tlist *add, int copy)
{
  struct tlist **end = to;

  while (*end)
    end = &(*end)->next;

  while (add)
    {
      int found = 0;
      struct tlist *tmp2;
      struct tlist *next = add->next;

      for (tmp2 = *to; tmp2; tmp2 = tmp2->next)
	if (tmp2->expr == add->expr)
	  {
	    found = 1;
	    if (!tmp2->writer)
	      tmp2->writer = add->writer;
	  }
      if (!found)
	{
	  *end = copy ? add : new_tlist (NULL, add->expr, add->writer);
	  end = &(*end)->next;
	  *end = 0;
	}
      add = next;
    }
}

/* WRITTEN is a variable, WRITER is its parent.  Warn if any of the variable
   references in list LIST conflict with it, excluding reads if ONLY writers
   is nonzero.  */

static void
warn_for_collisions_1 (tree written, tree writer, struct tlist *list,
		       int only_writes)
{
  struct tlist *tmp;

  /* Avoid duplicate warnings.  */
  for (tmp = warned_ids; tmp; tmp = tmp->next)
    if (tmp->expr == written)
      return;

  while (list)
    {
      if (list->expr == written
	  && list->writer != writer
	  && (!only_writes || list->writer)
	  && DECL_NAME (list->expr))
	{
	  warned_ids = new_tlist (warned_ids, written, NULL_TREE);
	  warning (0, "operation on %qE may be undefined", list->expr);
	}
      list = list->next;
    }
}

/* Given a list LIST of references to variables, find whether any of these
   can cause conflicts due to missing sequence points.  */

static void
warn_for_collisions (struct tlist *list)
{
  struct tlist *tmp;

  for (tmp = list; tmp; tmp = tmp->next)
    {
      if (tmp->writer)
	warn_for_collisions_1 (tmp->expr, tmp->writer, list, 0);
    }
}

/* Return nonzero if X is a tree that can be verified by the sequence point
   warnings.  */
static int
warning_candidate_p (tree x)
{
  return TREE_CODE (x) == VAR_DECL || TREE_CODE (x) == PARM_DECL;
}

/* Walk the tree X, and record accesses to variables.  If X is written by the
   parent tree, WRITER is the parent.
   We store accesses in one of the two lists: PBEFORE_SP, and PNO_SP.  If this
   expression or its only operand forces a sequence point, then everything up
   to the sequence point is stored in PBEFORE_SP.  Everything else gets stored
   in PNO_SP.
   Once we return, we will have emitted warnings if any subexpression before
   such a sequence point could be undefined.  On a higher level, however, the
   sequence point may not be relevant, and we'll merge the two lists.

   Example: (b++, a) + b;
   The call that processes the COMPOUND_EXPR will store the increment of B
   in PBEFORE_SP, and the use of A in PNO_SP.  The higher-level call that
   processes the PLUS_EXPR will need to merge the two lists so that
   eventually, all accesses end up on the same list (and we'll warn about the
   unordered subexpressions b++ and b.

   A note on merging.  If we modify the former example so that our expression
   becomes
     (b++, b) + a
   care must be taken not simply to add all three expressions into the final
   PNO_SP list.  The function merge_tlist takes care of that by merging the
   before-SP list of the COMPOUND_EXPR into its after-SP list in a special
   way, so that no more than one access to B is recorded.  */

static void
verify_tree (tree x, struct tlist **pbefore_sp, struct tlist **pno_sp,
	     tree writer)
{
  struct tlist *tmp_before, *tmp_nosp, *tmp_list2, *tmp_list3;
  enum tree_code code;
  enum tree_code_class cl;

  /* X may be NULL if it is the operand of an empty statement expression
     ({ }).  */
  if (x == NULL)
    return;

 restart:
  code = TREE_CODE (x);
  cl = TREE_CODE_CLASS (code);

  if (warning_candidate_p (x))
    {
      *pno_sp = new_tlist (*pno_sp, x, writer);
      return;
    }

  switch (code)
    {
    case CONSTRUCTOR:
      return;

    case COMPOUND_EXPR:
    case TRUTH_ANDIF_EXPR:
    case TRUTH_ORIF_EXPR:
      tmp_before = tmp_nosp = tmp_list3 = 0;
      verify_tree (TREE_OPERAND (x, 0), &tmp_before, &tmp_nosp, NULL_TREE);
      warn_for_collisions (tmp_nosp);
      merge_tlist (pbefore_sp, tmp_before, 0);
      merge_tlist (pbefore_sp, tmp_nosp, 0);
      verify_tree (TREE_OPERAND (x, 1), &tmp_list3, pno_sp, NULL_TREE);
      merge_tlist (pbefore_sp, tmp_list3, 0);
      return;

    case COND_EXPR:
      tmp_before = tmp_list2 = 0;
      verify_tree (TREE_OPERAND (x, 0), &tmp_before, &tmp_list2, NULL_TREE);
      warn_for_collisions (tmp_list2);
      merge_tlist (pbefore_sp, tmp_before, 0);
      merge_tlist (pbefore_sp, tmp_list2, 1);

      tmp_list3 = tmp_nosp = 0;
      verify_tree (TREE_OPERAND (x, 1), &tmp_list3, &tmp_nosp, NULL_TREE);
      warn_for_collisions (tmp_nosp);
      merge_tlist (pbefore_sp, tmp_list3, 0);

      tmp_list3 = tmp_list2 = 0;
      verify_tree (TREE_OPERAND (x, 2), &tmp_list3, &tmp_list2, NULL_TREE);
      warn_for_collisions (tmp_list2);
      merge_tlist (pbefore_sp, tmp_list3, 0);
      /* Rather than add both tmp_nosp and tmp_list2, we have to merge the
	 two first, to avoid warning for (a ? b++ : b++).  */
      merge_tlist (&tmp_nosp, tmp_list2, 0);
      add_tlist (pno_sp, tmp_nosp, NULL_TREE, 0);
      return;

    case PREDECREMENT_EXPR:
    case PREINCREMENT_EXPR:
    case POSTDECREMENT_EXPR:
    case POSTINCREMENT_EXPR:
      verify_tree (TREE_OPERAND (x, 0), pno_sp, pno_sp, x);
      return;

    case MODIFY_EXPR:
      tmp_before = tmp_nosp = tmp_list3 = 0;
      verify_tree (TREE_OPERAND (x, 1), &tmp_before, &tmp_nosp, NULL_TREE);
      verify_tree (TREE_OPERAND (x, 0), &tmp_list3, &tmp_list3, x);
      /* Expressions inside the LHS are not ordered wrt. the sequence points
	 in the RHS.  Example:
	   *a = (a++, 2)
	 Despite the fact that the modification of "a" is in the before_sp
	 list (tmp_before), it conflicts with the use of "a" in the LHS.
	 We can handle this by adding the contents of tmp_list3
	 to those of tmp_before, and redoing the collision warnings for that
	 list.  */
      add_tlist (&tmp_before, tmp_list3, x, 1);
      warn_for_collisions (tmp_before);
      /* Exclude the LHS itself here; we first have to merge it into the
	 tmp_nosp list.  This is done to avoid warning for "a = a"; if we
	 didn't exclude the LHS, we'd get it twice, once as a read and once
	 as a write.  */
      add_tlist (pno_sp, tmp_list3, x, 0);
      warn_for_collisions_1 (TREE_OPERAND (x, 0), x, tmp_nosp, 1);

      merge_tlist (pbefore_sp, tmp_before, 0);
      if (warning_candidate_p (TREE_OPERAND (x, 0)))
	merge_tlist (&tmp_nosp, new_tlist (NULL, TREE_OPERAND (x, 0), x), 0);
      add_tlist (pno_sp, tmp_nosp, NULL_TREE, 1);
      return;

    case CALL_EXPR:
      /* We need to warn about conflicts among arguments and conflicts between
	 args and the function address.  Side effects of the function address,
	 however, are not ordered by the sequence point of the call.  */
      tmp_before = tmp_nosp = tmp_list2 = tmp_list3 = 0;
      verify_tree (TREE_OPERAND (x, 0), &tmp_before, &tmp_nosp, NULL_TREE);
      if (TREE_OPERAND (x, 1))
	verify_tree (TREE_OPERAND (x, 1), &tmp_list2, &tmp_list3, NULL_TREE);
      merge_tlist (&tmp_list3, tmp_list2, 0);
      add_tlist (&tmp_before, tmp_list3, NULL_TREE, 0);
      add_tlist (&tmp_before, tmp_nosp, NULL_TREE, 0);
      warn_for_collisions (tmp_before);
      add_tlist (pbefore_sp, tmp_before, NULL_TREE, 0);
      return;

    case TREE_LIST:
      /* Scan all the list, e.g. indices of multi dimensional array.  */
      while (x)
	{
	  tmp_before = tmp_nosp = 0;
	  verify_tree (TREE_VALUE (x), &tmp_before, &tmp_nosp, NULL_TREE);
	  merge_tlist (&tmp_nosp, tmp_before, 0);
	  add_tlist (pno_sp, tmp_nosp, NULL_TREE, 0);
	  x = TREE_CHAIN (x);
	}
      return;

    case SAVE_EXPR:
      {
	struct tlist_cache *t;
	for (t = save_expr_cache; t; t = t->next)
	  if (t->expr == x)
	    break;

	if (!t)
	  {
	    t = XOBNEW (&tlist_obstack, struct tlist_cache);
	    t->next = save_expr_cache;
	    t->expr = x;
	    save_expr_cache = t;

	    tmp_before = tmp_nosp = 0;
	    verify_tree (TREE_OPERAND (x, 0), &tmp_before, &tmp_nosp, NULL_TREE);
	    warn_for_collisions (tmp_nosp);

	    tmp_list3 = 0;
	    while (tmp_nosp)
	      {
		struct tlist *t = tmp_nosp;
		tmp_nosp = t->next;
		merge_tlist (&tmp_list3, t, 0);
	      }
	    t->cache_before_sp = tmp_before;
	    t->cache_after_sp = tmp_list3;
	  }
	merge_tlist (pbefore_sp, t->cache_before_sp, 1);
	add_tlist (pno_sp, t->cache_after_sp, NULL_TREE, 1);
	return;
      }

    default:
      /* For other expressions, simply recurse on their operands.
	 Manual tail recursion for unary expressions.
	 Other non-expressions need not be processed.  */
      if (cl == tcc_unary)
	{
	  x = TREE_OPERAND (x, 0);
	  writer = 0;
	  goto restart;
	}
      else if (IS_EXPR_CODE_CLASS (cl))
	{
	  int lp;
	  int max = TREE_CODE_LENGTH (TREE_CODE (x));
	  for (lp = 0; lp < max; lp++)
	    {
	      tmp_before = tmp_nosp = 0;
	      verify_tree (TREE_OPERAND (x, lp), &tmp_before, &tmp_nosp, 0);
	      merge_tlist (&tmp_nosp, tmp_before, 0);
	      add_tlist (pno_sp, tmp_nosp, NULL_TREE, 0);
	    }
	}
      return;
    }
}

/* Try to warn for undefined behavior in EXPR due to missing sequence
   points.  */

void
verify_sequence_points (tree expr)
{
  struct tlist *before_sp = 0, *after_sp = 0;

  warned_ids = 0;
  save_expr_cache = 0;
  if (tlist_firstobj == 0)
    {
      gcc_obstack_init (&tlist_obstack);
      tlist_firstobj = (char *) obstack_alloc (&tlist_obstack, 0);
    }

  verify_tree (expr, &before_sp, &after_sp, 0);
  warn_for_collisions (after_sp);
  obstack_free (&tlist_obstack, tlist_firstobj);
}

/* Validate the expression after `case' and apply default promotions.  */

static tree
check_case_value (tree value)
{
  if (value == NULL_TREE)
    return value;

  /* ??? Can we ever get nops here for a valid case value?  We
     shouldn't for C.  */
  STRIP_TYPE_NOPS (value);
  /* In C++, the following is allowed:

       const int i = 3;
       switch (...) { case i: ... }

     So, we try to reduce the VALUE to a constant that way.  */
  if (c_dialect_cxx ())
    {
      value = decl_constant_value (value);
      STRIP_TYPE_NOPS (value);
      value = fold (value);
    }

  if (TREE_CODE (value) == INTEGER_CST)
    /* Promote char or short to int.  */
    value = perform_integral_promotions (value);
  else if (value != error_mark_node)
    {
      error ("case label does not reduce to an integer constant");
      value = error_mark_node;
    }

  constant_expression_warning (value);

  return value;
}

/* See if the case values LOW and HIGH are in the range of the original
   type (i.e. before the default conversion to int) of the switch testing
   expression.
   TYPE is the promoted type of the testing expression, and ORIG_TYPE is
   the type before promoting it.  CASE_LOW_P is a pointer to the lower
   bound of the case label, and CASE_HIGH_P is the upper bound or NULL
   if the case is not a case range.
   The caller has to make sure that we are not called with NULL for
   CASE_LOW_P (i.e. the default case).
   Returns true if the case label is in range of ORIG_TYPE (saturated or
   untouched) or false if the label is out of range.  */

static bool
check_case_bounds (tree type, tree orig_type,
		   tree *case_low_p, tree *case_high_p)
{
  tree min_value, max_value;
  tree case_low = *case_low_p;
  tree case_high = case_high_p ? *case_high_p : case_low;

  /* If there was a problem with the original type, do nothing.  */
  if (orig_type == error_mark_node)
    return true;

  min_value = TYPE_MIN_VALUE (orig_type);
  max_value = TYPE_MAX_VALUE (orig_type);

  /* Case label is less than minimum for type.  */
  if (tree_int_cst_compare (case_low, min_value) < 0
      && tree_int_cst_compare (case_high, min_value) < 0)
    {
      warning (0, "case label value is less than minimum value for type");
      return false;
    }

  /* Case value is greater than maximum for type.  */
  if (tree_int_cst_compare (case_low, max_value) > 0
      && tree_int_cst_compare (case_high, max_value) > 0)
    {
      warning (0, "case label value exceeds maximum value for type");
      return false;
    }

  /* Saturate lower case label value to minimum.  */
  if (tree_int_cst_compare (case_high, min_value) >= 0
      && tree_int_cst_compare (case_low, min_value) < 0)
    {
      warning (0, "lower value in case label range"
	       " less than minimum value for type");
      case_low = min_value;
    }

  /* Saturate upper case label value to maximum.  */
  if (tree_int_cst_compare (case_low, max_value) <= 0
      && tree_int_cst_compare (case_high, max_value) > 0)
    {
      warning (0, "upper value in case label range"
	       " exceeds maximum value for type");
      case_high = max_value;
    }

  if (*case_low_p != case_low)
    *case_low_p = convert (type, case_low);
  if (case_high_p && *case_high_p != case_high)
    *case_high_p = convert (type, case_high);

  return true;
}

/* Return an integer type with BITS bits of precision,
   that is unsigned if UNSIGNEDP is nonzero, otherwise signed.  */

tree
c_common_type_for_size (unsigned int bits, int unsignedp)
{
  if (bits == TYPE_PRECISION (integer_type_node))
    return unsignedp ? unsigned_type_node : integer_type_node;

  if (bits == TYPE_PRECISION (signed_char_type_node))
    return unsignedp ? unsigned_char_type_node : signed_char_type_node;

  if (bits == TYPE_PRECISION (short_integer_type_node))
    return unsignedp ? short_unsigned_type_node : short_integer_type_node;

  if (bits == TYPE_PRECISION (long_integer_type_node))
    return unsignedp ? long_unsigned_type_node : long_integer_type_node;

  if (bits == TYPE_PRECISION (long_long_integer_type_node))
    return (unsignedp ? long_long_unsigned_type_node
	    : long_long_integer_type_node);

  if (bits == TYPE_PRECISION (widest_integer_literal_type_node))
    return (unsignedp ? widest_unsigned_literal_type_node
	    : widest_integer_literal_type_node);

  if (bits <= TYPE_PRECISION (intQI_type_node))
    return unsignedp ? unsigned_intQI_type_node : intQI_type_node;

  if (bits <= TYPE_PRECISION (intHI_type_node))
    return unsignedp ? unsigned_intHI_type_node : intHI_type_node;

  if (bits <= TYPE_PRECISION (intSI_type_node))
    return unsignedp ? unsigned_intSI_type_node : intSI_type_node;

  if (bits <= TYPE_PRECISION (intDI_type_node))
    return unsignedp ? unsigned_intDI_type_node : intDI_type_node;

  return 0;
}

/* Used for communication between c_common_type_for_mode and
   c_register_builtin_type.  */
static GTY(()) tree registered_builtin_types;

/* Return a data type that has machine mode MODE.
   If the mode is an integer,
   then UNSIGNEDP selects between signed and unsigned types.  */

tree
c_common_type_for_mode (enum machine_mode mode, int unsignedp)
{
  tree t;

  if (mode == TYPE_MODE (integer_type_node))
    return unsignedp ? unsigned_type_node : integer_type_node;

  if (mode == TYPE_MODE (signed_char_type_node))
    return unsignedp ? unsigned_char_type_node : signed_char_type_node;

  if (mode == TYPE_MODE (short_integer_type_node))
    return unsignedp ? short_unsigned_type_node : short_integer_type_node;

  if (mode == TYPE_MODE (long_integer_type_node))
    return unsignedp ? long_unsigned_type_node : long_integer_type_node;

  if (mode == TYPE_MODE (long_long_integer_type_node))
    return unsignedp ? long_long_unsigned_type_node : long_long_integer_type_node;

  if (mode == TYPE_MODE (widest_integer_literal_type_node))
    return unsignedp ? widest_unsigned_literal_type_node
		     : widest_integer_literal_type_node;

  if (mode == QImode)
    return unsignedp ? unsigned_intQI_type_node : intQI_type_node;

  if (mode == HImode)
    return unsignedp ? unsigned_intHI_type_node : intHI_type_node;

  if (mode == SImode)
    return unsignedp ? unsigned_intSI_type_node : intSI_type_node;

  if (mode == DImode)
    return unsignedp ? unsigned_intDI_type_node : intDI_type_node;

#if HOST_BITS_PER_WIDE_INT >= 64
  if (mode == TYPE_MODE (intTI_type_node))
    return unsignedp ? unsigned_intTI_type_node : intTI_type_node;
#endif

  if (mode == TYPE_MODE (float_type_node))
    return float_type_node;

  if (mode == TYPE_MODE (double_type_node))
    return double_type_node;

  if (mode == TYPE_MODE (long_double_type_node))
    return long_double_type_node;

  if (mode == TYPE_MODE (void_type_node))
    return void_type_node;

  if (mode == TYPE_MODE (build_pointer_type (char_type_node)))
    return (unsignedp
	    ? make_unsigned_type (GET_MODE_PRECISION (mode))
	    : make_signed_type (GET_MODE_PRECISION (mode)));

  if (mode == TYPE_MODE (build_pointer_type (integer_type_node)))
    return (unsignedp
	    ? make_unsigned_type (GET_MODE_PRECISION (mode))
	    : make_signed_type (GET_MODE_PRECISION (mode)));

  if (COMPLEX_MODE_P (mode))
    {
      enum machine_mode inner_mode;
      tree inner_type;

      if (mode == TYPE_MODE (complex_float_type_node))
	return complex_float_type_node;
      if (mode == TYPE_MODE (complex_double_type_node))
	return complex_double_type_node;
      if (mode == TYPE_MODE (complex_long_double_type_node))
	return complex_long_double_type_node;

      if (mode == TYPE_MODE (complex_integer_type_node) && !unsignedp)
	return complex_integer_type_node;

      inner_mode = GET_MODE_INNER (mode);
      inner_type = c_common_type_for_mode (inner_mode, unsignedp);
      if (inner_type != NULL_TREE)
	return build_complex_type (inner_type);
    }
  else if (VECTOR_MODE_P (mode))
    {
      enum machine_mode inner_mode = GET_MODE_INNER (mode);
      tree inner_type = c_common_type_for_mode (inner_mode, unsignedp);
      if (inner_type != NULL_TREE)
	return build_vector_type_for_mode (inner_type, mode);
    }

  if (mode == TYPE_MODE (dfloat32_type_node))
    return dfloat32_type_node;
  if (mode == TYPE_MODE (dfloat64_type_node))
    return dfloat64_type_node;
  if (mode == TYPE_MODE (dfloat128_type_node))
    return dfloat128_type_node;

  for (t = registered_builtin_types; t; t = TREE_CHAIN (t))
    if (TYPE_MODE (TREE_VALUE (t)) == mode)
      return TREE_VALUE (t);

  return 0;
}

/* Return an unsigned type the same as TYPE in other respects.  */
tree
c_common_unsigned_type (tree type)
{
  tree type1 = TYPE_MAIN_VARIANT (type);
  if (type1 == signed_char_type_node || type1 == char_type_node)
    return unsigned_char_type_node;
  if (type1 == integer_type_node)
    return unsigned_type_node;
  if (type1 == short_integer_type_node)
    return short_unsigned_type_node;
  if (type1 == long_integer_type_node)
    return long_unsigned_type_node;
  if (type1 == long_long_integer_type_node)
    return long_long_unsigned_type_node;
  if (type1 == widest_integer_literal_type_node)
    return widest_unsigned_literal_type_node;
#if HOST_BITS_PER_WIDE_INT >= 64
  if (type1 == intTI_type_node)
    return unsigned_intTI_type_node;
#endif
  if (type1 == intDI_type_node)
    return unsigned_intDI_type_node;
  if (type1 == intSI_type_node)
    return unsigned_intSI_type_node;
  if (type1 == intHI_type_node)
    return unsigned_intHI_type_node;
  if (type1 == intQI_type_node)
    return unsigned_intQI_type_node;

  return c_common_signed_or_unsigned_type (1, type);
}

/* Return a signed type the same as TYPE in other respects.  */

tree
c_common_signed_type (tree type)
{
  tree type1 = TYPE_MAIN_VARIANT (type);
  if (type1 == unsigned_char_type_node || type1 == char_type_node)
    return signed_char_type_node;
  if (type1 == unsigned_type_node)
    return integer_type_node;
  if (type1 == short_unsigned_type_node)
    return short_integer_type_node;
  if (type1 == long_unsigned_type_node)
    return long_integer_type_node;
  if (type1 == long_long_unsigned_type_node)
    return long_long_integer_type_node;
  if (type1 == widest_unsigned_literal_type_node)
    return widest_integer_literal_type_node;
#if HOST_BITS_PER_WIDE_INT >= 64
  if (type1 == unsigned_intTI_type_node)
    return intTI_type_node;
#endif
  if (type1 == unsigned_intDI_type_node)
    return intDI_type_node;
  if (type1 == unsigned_intSI_type_node)
    return intSI_type_node;
  if (type1 == unsigned_intHI_type_node)
    return intHI_type_node;
  if (type1 == unsigned_intQI_type_node)
    return intQI_type_node;

  return c_common_signed_or_unsigned_type (0, type);
}

/* Return a type the same as TYPE except unsigned or
   signed according to UNSIGNEDP.  */

tree
c_common_signed_or_unsigned_type (int unsignedp, tree type)
{
  if (!INTEGRAL_TYPE_P (type)
      || TYPE_UNSIGNED (type) == unsignedp)
    return type;

  /* For ENUMERAL_TYPEs in C++, must check the mode of the types, not
     the precision; they have precision set to match their range, but
     may use a wider mode to match an ABI.  If we change modes, we may
     wind up with bad conversions.  For INTEGER_TYPEs in C, must check
     the precision as well, so as to yield correct results for
     bit-field types.  C++ does not have these separate bit-field
     types, and producing a signed or unsigned variant of an
     ENUMERAL_TYPE may cause other problems as well.  */

#define TYPE_OK(node)							    \
  (TYPE_MODE (type) == TYPE_MODE (node)					    \
   && (c_dialect_cxx () || TYPE_PRECISION (type) == TYPE_PRECISION (node)))
  if (TYPE_OK (signed_char_type_node))
    return unsignedp ? unsigned_char_type_node : signed_char_type_node;
  if (TYPE_OK (integer_type_node))
    return unsignedp ? unsigned_type_node : integer_type_node;
  if (TYPE_OK (short_integer_type_node))
    return unsignedp ? short_unsigned_type_node : short_integer_type_node;
  if (TYPE_OK (long_integer_type_node))
    return unsignedp ? long_unsigned_type_node : long_integer_type_node;
  if (TYPE_OK (long_long_integer_type_node))
    return (unsignedp ? long_long_unsigned_type_node
	    : long_long_integer_type_node);
  if (TYPE_OK (widest_integer_literal_type_node))
    return (unsignedp ? widest_unsigned_literal_type_node
	    : widest_integer_literal_type_node);

#if HOST_BITS_PER_WIDE_INT >= 64
  if (TYPE_OK (intTI_type_node))
    return unsignedp ? unsigned_intTI_type_node : intTI_type_node;
#endif
  if (TYPE_OK (intDI_type_node))
    return unsignedp ? unsigned_intDI_type_node : intDI_type_node;
  if (TYPE_OK (intSI_type_node))
    return unsignedp ? unsigned_intSI_type_node : intSI_type_node;
  if (TYPE_OK (intHI_type_node))
    return unsignedp ? unsigned_intHI_type_node : intHI_type_node;
  if (TYPE_OK (intQI_type_node))
    return unsignedp ? unsigned_intQI_type_node : intQI_type_node;
#undef TYPE_OK

  if (c_dialect_cxx ())
    return type;
  else
    return build_nonstandard_integer_type (TYPE_PRECISION (type), unsignedp);
}

/* Build a bit-field integer type for the given WIDTH and UNSIGNEDP.  */

tree
c_build_bitfield_integer_type (unsigned HOST_WIDE_INT width, int unsignedp)
{
  /* Extended integer types of the same width as a standard type have
     lesser rank, so those of the same width as int promote to int or
     unsigned int and are valid for printf formats expecting int or
     unsigned int.  To avoid such special cases, avoid creating
     extended integer types for bit-fields if a standard integer type
     is available.  */
  if (width == TYPE_PRECISION (integer_type_node))
    return unsignedp ? unsigned_type_node : integer_type_node;
  if (width == TYPE_PRECISION (signed_char_type_node))
    return unsignedp ? unsigned_char_type_node : signed_char_type_node;
  if (width == TYPE_PRECISION (short_integer_type_node))
    return unsignedp ? short_unsigned_type_node : short_integer_type_node;
  if (width == TYPE_PRECISION (long_integer_type_node))
    return unsignedp ? long_unsigned_type_node : long_integer_type_node;
  if (width == TYPE_PRECISION (long_long_integer_type_node))
    return (unsignedp ? long_long_unsigned_type_node
	    : long_long_integer_type_node);
  return build_nonstandard_integer_type (width, unsignedp);
}

/* The C version of the register_builtin_type langhook.  */

void
c_register_builtin_type (tree type, const char* name)
{
  tree decl;

  decl = build_decl (TYPE_DECL, get_identifier (name), type);
  DECL_ARTIFICIAL (decl) = 1;
  if (!TYPE_NAME (type))
    TYPE_NAME (type) = decl;
  pushdecl (decl);

  registered_builtin_types = tree_cons (0, type, registered_builtin_types);
}


/* Return the minimum number of bits needed to represent VALUE in a
   signed or unsigned type, UNSIGNEDP says which.  */

unsigned int
min_precision (tree value, int unsignedp)
{
  int log;

  /* If the value is negative, compute its negative minus 1.  The latter
     adjustment is because the absolute value of the largest negative value
     is one larger than the largest positive value.  This is equivalent to
     a bit-wise negation, so use that operation instead.  */

  if (tree_int_cst_sgn (value) < 0)
    value = fold_build1 (BIT_NOT_EXPR, TREE_TYPE (value), value);

  /* Return the number of bits needed, taking into account the fact
     that we need one more bit for a signed than unsigned type.  */

  if (integer_zerop (value))
    log = 0;
  else
    log = tree_floor_log2 (value);

  return log + 1 + !unsignedp;
}

/* Print an error message for invalid operands to arith operation
   CODE.  */

void
binary_op_error (enum tree_code code)
{
  const char *opname;

  switch (code)
    {
    case PLUS_EXPR:
      opname = "+"; break;
    case MINUS_EXPR:
      opname = "-"; break;
    case MULT_EXPR:
      opname = "*"; break;
    case MAX_EXPR:
      opname = "max"; break;
    case MIN_EXPR:
      opname = "min"; break;
    case EQ_EXPR:
      opname = "=="; break;
    case NE_EXPR:
      opname = "!="; break;
    case LE_EXPR:
      opname = "<="; break;
    case GE_EXPR:
      opname = ">="; break;
    case LT_EXPR:
      opname = "<"; break;
    case GT_EXPR:
      opname = ">"; break;
    case LSHIFT_EXPR:
      opname = "<<"; break;
    case RSHIFT_EXPR:
      opname = ">>"; break;
    case TRUNC_MOD_EXPR:
    case FLOOR_MOD_EXPR:
      opname = "%"; break;
    case TRUNC_DIV_EXPR:
    case FLOOR_DIV_EXPR:
      opname = "/"; break;
    case BIT_AND_EXPR:
      opname = "&"; break;
    case BIT_IOR_EXPR:
      opname = "|"; break;
    case TRUTH_ANDIF_EXPR:
      opname = "&&"; break;
    case TRUTH_ORIF_EXPR:
      opname = "||"; break;
    case BIT_XOR_EXPR:
      opname = "^"; break;
    default:
      gcc_unreachable ();
    }
  error ("invalid operands to binary %s", opname);
}

/* Subroutine of build_binary_op, used for comparison operations.
   See if the operands have both been converted from subword integer types
   and, if so, perhaps change them both back to their original type.
   This function is also responsible for converting the two operands
   to the proper common type for comparison.

   The arguments of this function are all pointers to local variables
   of build_binary_op: OP0_PTR is &OP0, OP1_PTR is &OP1,
   RESTYPE_PTR is &RESULT_TYPE and RESCODE_PTR is &RESULTCODE.

   If this function returns nonzero, it means that the comparison has
   a constant value.  What this function returns is an expression for
   that value.  */

tree
shorten_compare (tree *op0_ptr, tree *op1_ptr, tree *restype_ptr,
		 enum tree_code *rescode_ptr)
{
  tree type;
  tree op0 = *op0_ptr;
  tree op1 = *op1_ptr;
  int unsignedp0, unsignedp1;
  int real1, real2;
  tree primop0, primop1;
  enum tree_code code = *rescode_ptr;

  /* Throw away any conversions to wider types
     already present in the operands.  */

  primop0 = get_narrower (op0, &unsignedp0);
  primop1 = get_narrower (op1, &unsignedp1);

  /* Handle the case that OP0 does not *contain* a conversion
     but it *requires* conversion to FINAL_TYPE.  */

  if (op0 == primop0 && TREE_TYPE (op0) != *restype_ptr)
    unsignedp0 = TYPE_UNSIGNED (TREE_TYPE (op0));
  if (op1 == primop1 && TREE_TYPE (op1) != *restype_ptr)
    unsignedp1 = TYPE_UNSIGNED (TREE_TYPE (op1));

  /* If one of the operands must be floated, we cannot optimize.  */
  real1 = TREE_CODE (TREE_TYPE (primop0)) == REAL_TYPE;
  real2 = TREE_CODE (TREE_TYPE (primop1)) == REAL_TYPE;

  /* If first arg is constant, swap the args (changing operation
     so value is preserved), for canonicalization.  Don't do this if
     the second arg is 0.  */

  if (TREE_CONSTANT (primop0)
      && !integer_zerop (primop1) && !real_zerop (primop1))
    {
      tree tem = primop0;
      int temi = unsignedp0;
      primop0 = primop1;
      primop1 = tem;
      tem = op0;
      op0 = op1;
      op1 = tem;
      *op0_ptr = op0;
      *op1_ptr = op1;
      unsignedp0 = unsignedp1;
      unsignedp1 = temi;
      temi = real1;
      real1 = real2;
      real2 = temi;

      switch (code)
	{
	case LT_EXPR:
	  code = GT_EXPR;
	  break;
	case GT_EXPR:
	  code = LT_EXPR;
	  break;
	case LE_EXPR:
	  code = GE_EXPR;
	  break;
	case GE_EXPR:
	  code = LE_EXPR;
	  break;
	default:
	  break;
	}
      *rescode_ptr = code;
    }

  /* If comparing an integer against a constant more bits wide,
     maybe we can deduce a value of 1 or 0 independent of the data.
     Or else truncate the constant now
     rather than extend the variable at run time.

     This is only interesting if the constant is the wider arg.
     Also, it is not safe if the constant is unsigned and the
     variable arg is signed, since in this case the variable
     would be sign-extended and then regarded as unsigned.
     Our technique fails in this case because the lowest/highest
     possible unsigned results don't follow naturally from the
     lowest/highest possible values of the variable operand.
     For just EQ_EXPR and NE_EXPR there is another technique that
     could be used: see if the constant can be faithfully represented
     in the other operand's type, by truncating it and reextending it
     and see if that preserves the constant's value.  */

  if (!real1 && !real2
      && TREE_CODE (primop1) == INTEGER_CST
      && TYPE_PRECISION (TREE_TYPE (primop0)) < TYPE_PRECISION (*restype_ptr))
    {
      int min_gt, max_gt, min_lt, max_lt;
      tree maxval, minval;
      /* 1 if comparison is nominally unsigned.  */
      int unsignedp = TYPE_UNSIGNED (*restype_ptr);
      tree val;

      type = c_common_signed_or_unsigned_type (unsignedp0,
					       TREE_TYPE (primop0));

      maxval = TYPE_MAX_VALUE (type);
      minval = TYPE_MIN_VALUE (type);

      if (unsignedp && !unsignedp0)
	*restype_ptr = c_common_signed_type (*restype_ptr);

      if (TREE_TYPE (primop1) != *restype_ptr)
	{
	  /* Convert primop1 to target type, but do not introduce
	     additional overflow.  We know primop1 is an int_cst.  */
	  tree tmp = build_int_cst_wide (*restype_ptr,
					 TREE_INT_CST_LOW (primop1),
					 TREE_INT_CST_HIGH (primop1));

	  primop1 = force_fit_type (tmp, 0, TREE_OVERFLOW (primop1),
				    TREE_CONSTANT_OVERFLOW (primop1));
	}
      if (type != *restype_ptr)
	{
	  minval = convert (*restype_ptr, minval);
	  maxval = convert (*restype_ptr, maxval);
	}

      if (unsignedp && unsignedp0)
	{
	  min_gt = INT_CST_LT_UNSIGNED (primop1, minval);
	  max_gt = INT_CST_LT_UNSIGNED (primop1, maxval);
	  min_lt = INT_CST_LT_UNSIGNED (minval, primop1);
	  max_lt = INT_CST_LT_UNSIGNED (maxval, primop1);
	}
      else
	{
	  min_gt = INT_CST_LT (primop1, minval);
	  max_gt = INT_CST_LT (primop1, maxval);
	  min_lt = INT_CST_LT (minval, primop1);
	  max_lt = INT_CST_LT (maxval, primop1);
	}

      val = 0;
      /* This used to be a switch, but Genix compiler can't handle that.  */
      if (code == NE_EXPR)
	{
	  if (max_lt || min_gt)
	    val = truthvalue_true_node;
	}
      else if (code == EQ_EXPR)
	{
	  if (max_lt || min_gt)
	    val = truthvalue_false_node;
	}
      else if (code == LT_EXPR)
	{
	  if (max_lt)
	    val = truthvalue_true_node;
	  if (!min_lt)
	    val = truthvalue_false_node;
	}
      else if (code == GT_EXPR)
	{
	  if (min_gt)
	    val = truthvalue_true_node;
	  if (!max_gt)
	    val = truthvalue_false_node;
	}
      else if (code == LE_EXPR)
	{
	  if (!max_gt)
	    val = truthvalue_true_node;
	  if (min_gt)
	    val = truthvalue_false_node;
	}
      else if (code == GE_EXPR)
	{
	  if (!min_lt)
	    val = truthvalue_true_node;
	  if (max_lt)
	    val = truthvalue_false_node;
	}

      /* If primop0 was sign-extended and unsigned comparison specd,
	 we did a signed comparison above using the signed type bounds.
	 But the comparison we output must be unsigned.

	 Also, for inequalities, VAL is no good; but if the signed
	 comparison had *any* fixed result, it follows that the
	 unsigned comparison just tests the sign in reverse
	 (positive values are LE, negative ones GE).
	 So we can generate an unsigned comparison
	 against an extreme value of the signed type.  */

      if (unsignedp && !unsignedp0)
	{
	  if (val != 0)
	    switch (code)
	      {
	      case LT_EXPR:
	      case GE_EXPR:
		primop1 = TYPE_MIN_VALUE (type);
		val = 0;
		break;

	      case LE_EXPR:
	      case GT_EXPR:
		primop1 = TYPE_MAX_VALUE (type);
		val = 0;
		break;

	      default:
		break;
	      }
	  type = c_common_unsigned_type (type);
	}

      if (TREE_CODE (primop0) != INTEGER_CST)
	{
	  if (val == truthvalue_false_node)
	    warning (0, "comparison is always false due to limited range of data type");
	  if (val == truthvalue_true_node)
	    warning (0, "comparison is always true due to limited range of data type");
	}

      if (val != 0)
	{
	  /* Don't forget to evaluate PRIMOP0 if it has side effects.  */
	  if (TREE_SIDE_EFFECTS (primop0))
	    return build2 (COMPOUND_EXPR, TREE_TYPE (val), primop0, val);
	  return val;
	}

      /* Value is not predetermined, but do the comparison
	 in the type of the operand that is not constant.
	 TYPE is already properly set.  */
    }

  /* If either arg is decimal float and the other is float, find the
     proper common type to use for comparison.  */
  else if (real1 && real2
	   && (DECIMAL_FLOAT_MODE_P (TYPE_MODE (TREE_TYPE (primop0)))
	       || DECIMAL_FLOAT_MODE_P (TYPE_MODE (TREE_TYPE (primop1)))))
    type = common_type (TREE_TYPE (primop0), TREE_TYPE (primop1));

  else if (real1 && real2
	   && (TYPE_PRECISION (TREE_TYPE (primop0))
	       == TYPE_PRECISION (TREE_TYPE (primop1))))
    type = TREE_TYPE (primop0);

  /* If args' natural types are both narrower than nominal type
     and both extend in the same manner, compare them
     in the type of the wider arg.
     Otherwise must actually extend both to the nominal
     common type lest different ways of extending
     alter the result.
     (eg, (short)-1 == (unsigned short)-1  should be 0.)  */

  else if (unsignedp0 == unsignedp1 && real1 == real2
	   && TYPE_PRECISION (TREE_TYPE (primop0)) < TYPE_PRECISION (*restype_ptr)
	   && TYPE_PRECISION (TREE_TYPE (primop1)) < TYPE_PRECISION (*restype_ptr))
    {
      type = common_type (TREE_TYPE (primop0), TREE_TYPE (primop1));
      type = c_common_signed_or_unsigned_type (unsignedp0
					       || TYPE_UNSIGNED (*restype_ptr),
					       type);
      /* Make sure shorter operand is extended the right way
	 to match the longer operand.  */
      primop0
	= convert (c_common_signed_or_unsigned_type (unsignedp0,
						     TREE_TYPE (primop0)),
		   primop0);
      primop1
	= convert (c_common_signed_or_unsigned_type (unsignedp1,
						     TREE_TYPE (primop1)),
		   primop1);
    }
  else
    {
      /* Here we must do the comparison on the nominal type
	 using the args exactly as we received them.  */
      type = *restype_ptr;
      primop0 = op0;
      primop1 = op1;

      if (!real1 && !real2 && integer_zerop (primop1)
	  && TYPE_UNSIGNED (*restype_ptr))
	{
	  tree value = 0;
	  switch (code)
	    {
	    case GE_EXPR:
	      /* All unsigned values are >= 0, so we warn if extra warnings
		 are requested.  However, if OP0 is a constant that is
		 >= 0, the signedness of the comparison isn't an issue,
		 so suppress the warning.  */
	      if (extra_warnings && !in_system_header
		  && !(TREE_CODE (primop0) == INTEGER_CST
		       && !TREE_OVERFLOW (convert (c_common_signed_type (type),
						   primop0))))
		warning (0, "comparison of unsigned expression >= 0 is always true");
	      value = truthvalue_true_node;
	      break;

	    case LT_EXPR:
	      if (extra_warnings && !in_system_header
		  && !(TREE_CODE (primop0) == INTEGER_CST
		       && !TREE_OVERFLOW (convert (c_common_signed_type (type),
						   primop0))))
		warning (0, "comparison of unsigned expression < 0 is always false");
	      value = truthvalue_false_node;
	      break;

	    default:
	      break;
	    }

	  if (value != 0)
	    {
	      /* Don't forget to evaluate PRIMOP0 if it has side effects.  */
	      if (TREE_SIDE_EFFECTS (primop0))
		return build2 (COMPOUND_EXPR, TREE_TYPE (value),
			       primop0, value);
	      return value;
	    }
	}
    }

  *op0_ptr = convert (type, primop0);
  *op1_ptr = convert (type, primop1);

  *restype_ptr = truthvalue_type_node;

  return 0;
}

/* Return a tree for the sum or difference (RESULTCODE says which)
   of pointer PTROP and integer INTOP.  */

tree
pointer_int_sum (enum tree_code resultcode, tree ptrop, tree intop)
{
  tree size_exp, ret;

  /* The result is a pointer of the same type that is being added.  */

  tree result_type = TREE_TYPE (ptrop);

  if (TREE_CODE (TREE_TYPE (result_type)) == VOID_TYPE)
    {
      if (pedantic || warn_pointer_arith)
	pedwarn ("pointer of type %<void *%> used in arithmetic");
      size_exp = integer_one_node;
    }
  else if (TREE_CODE (TREE_TYPE (result_type)) == FUNCTION_TYPE)
    {
      if (pedantic || warn_pointer_arith)
	pedwarn ("pointer to a function used in arithmetic");
      size_exp = integer_one_node;
    }
  else if (TREE_CODE (TREE_TYPE (result_type)) == METHOD_TYPE)
    {
      if (pedantic || warn_pointer_arith)
	pedwarn ("pointer to member function used in arithmetic");
      size_exp = integer_one_node;
    }
  else
    size_exp = size_in_bytes (TREE_TYPE (result_type));

  /* We are manipulating pointer values, so we don't need to warn
     about relying on undefined signed overflow.  We disable the
     warning here because we use integer types so fold won't know that
     they are really pointers.  */
  fold_defer_overflow_warnings ();

  /* If what we are about to multiply by the size of the elements
     contains a constant term, apply distributive law
     and multiply that constant term separately.
     This helps produce common subexpressions.  */

  if ((TREE_CODE (intop) == PLUS_EXPR || TREE_CODE (intop) == MINUS_EXPR)
      && !TREE_CONSTANT (intop)
      && TREE_CONSTANT (TREE_OPERAND (intop, 1))
      && TREE_CONSTANT (size_exp)
      /* If the constant comes from pointer subtraction,
	 skip this optimization--it would cause an error.  */
      && TREE_CODE (TREE_TYPE (TREE_OPERAND (intop, 0))) == INTEGER_TYPE
      /* If the constant is unsigned, and smaller than the pointer size,
	 then we must skip this optimization.  This is because it could cause
	 an overflow error if the constant is negative but INTOP is not.  */
      && (!TYPE_UNSIGNED (TREE_TYPE (intop))
	  || (TYPE_PRECISION (TREE_TYPE (intop))
	      == TYPE_PRECISION (TREE_TYPE (ptrop)))))
    {
      enum tree_code subcode = resultcode;
      tree int_type = TREE_TYPE (intop);
      if (TREE_CODE (intop) == MINUS_EXPR)
	subcode = (subcode == PLUS_EXPR ? MINUS_EXPR : PLUS_EXPR);
      /* Convert both subexpression types to the type of intop,
	 because weird cases involving pointer arithmetic
	 can result in a sum or difference with different type args.  */
      ptrop = build_binary_op (subcode, ptrop,
			       convert (int_type, TREE_OPERAND (intop, 1)), 1);
      intop = convert (int_type, TREE_OPERAND (intop, 0));
    }

  /* Convert the integer argument to a type the same size as sizetype
     so the multiply won't overflow spuriously.  */

  if (TYPE_PRECISION (TREE_TYPE (intop)) != TYPE_PRECISION (sizetype)
      || TYPE_UNSIGNED (TREE_TYPE (intop)) != TYPE_UNSIGNED (sizetype))
    intop = convert (c_common_type_for_size (TYPE_PRECISION (sizetype),
					     TYPE_UNSIGNED (sizetype)), intop);

  /* Replace the integer argument with a suitable product by the object size.
     Do this multiplication as signed, then convert to the appropriate
     pointer type (actually unsigned integral).  */

  intop = convert (result_type,
		   build_binary_op (MULT_EXPR, intop,
				    convert (TREE_TYPE (intop), size_exp), 1));

  /* Create the sum or difference.  */
  ret = fold_build2 (resultcode, result_type, ptrop, intop);

  fold_undefer_and_ignore_overflow_warnings ();

  return ret;
}

/* Prepare expr to be an argument of a TRUTH_NOT_EXPR,
   or for an `if' or `while' statement or ?..: exp.  It should already
   have been validated to be of suitable type; otherwise, a bad
   diagnostic may result.

   This preparation consists of taking the ordinary
   representation of an expression expr and producing a valid tree
   boolean expression describing whether expr is nonzero.  We could
   simply always do build_binary_op (NE_EXPR, expr, truthvalue_false_node, 1),
   but we optimize comparisons, &&, ||, and !.

   The resulting type should always be `truthvalue_type_node'.  */

tree
c_common_truthvalue_conversion (tree expr)
{
  switch (TREE_CODE (expr))
    {
    case EQ_EXPR:   case NE_EXPR:   case UNEQ_EXPR: case LTGT_EXPR:
    case LE_EXPR:   case GE_EXPR:   case LT_EXPR:   case GT_EXPR:
    case UNLE_EXPR: case UNGE_EXPR: case UNLT_EXPR: case UNGT_EXPR:
    case ORDERED_EXPR: case UNORDERED_EXPR:
      if (TREE_TYPE (expr) == truthvalue_type_node)
	return expr;
      return build2 (TREE_CODE (expr), truthvalue_type_node,
		     TREE_OPERAND (expr, 0), TREE_OPERAND (expr, 1));

    case TRUTH_ANDIF_EXPR:
    case TRUTH_ORIF_EXPR:
    case TRUTH_AND_EXPR:
    case TRUTH_OR_EXPR:
    case TRUTH_XOR_EXPR:
      if (TREE_TYPE (expr) == truthvalue_type_node)
	return expr;
      return build2 (TREE_CODE (expr), truthvalue_type_node,
		 c_common_truthvalue_conversion (TREE_OPERAND (expr, 0)),
		 c_common_truthvalue_conversion (TREE_OPERAND (expr, 1)));

    case TRUTH_NOT_EXPR:
      if (TREE_TYPE (expr) == truthvalue_type_node)
	return expr;
      return build1 (TREE_CODE (expr), truthvalue_type_node,
		 c_common_truthvalue_conversion (TREE_OPERAND (expr, 0)));

    case ERROR_MARK:
      return expr;

    case INTEGER_CST:
      /* Avoid integer_zerop to ignore TREE_CONSTANT_OVERFLOW.  */
      return (TREE_INT_CST_LOW (expr) != 0 || TREE_INT_CST_HIGH (expr) != 0)
	     ? truthvalue_true_node
	     : truthvalue_false_node;

    case REAL_CST:
      return real_compare (NE_EXPR, &TREE_REAL_CST (expr), &dconst0)
	     ? truthvalue_true_node
	     : truthvalue_false_node;

    case FUNCTION_DECL:
      expr = build_unary_op (ADDR_EXPR, expr, 0);
      /* Fall through.  */

    case ADDR_EXPR:
      {
 	tree inner = TREE_OPERAND (expr, 0);
	if (DECL_P (inner)
	    && (TREE_CODE (inner) == PARM_DECL
		|| TREE_CODE (inner) == LABEL_DECL
		|| !DECL_WEAK (inner)))
	  {
            /* Common Ada/Pascal programmer's mistake.  We always warn
	       about this since it is so bad.  */
	    warning (OPT_Waddress,
		     "the address of %qD will always evaluate as %<true%>",
		     inner);
	    return truthvalue_true_node;
	  }

	/* If we are taking the address of an external decl, it might be
	   zero if it is weak, so we cannot optimize.  */
	if (DECL_P (inner)
	    && DECL_EXTERNAL (inner))
	  break;

	if (TREE_SIDE_EFFECTS (inner))
	  return build2 (COMPOUND_EXPR, truthvalue_type_node,
			 inner, truthvalue_true_node);
	else
	  return truthvalue_true_node;
      }

    case COMPLEX_EXPR:
      return build_binary_op ((TREE_SIDE_EFFECTS (TREE_OPERAND (expr, 1))
			       ? TRUTH_OR_EXPR : TRUTH_ORIF_EXPR),
		c_common_truthvalue_conversion (TREE_OPERAND (expr, 0)),
		c_common_truthvalue_conversion (TREE_OPERAND (expr, 1)),
			      0);

    case NEGATE_EXPR:
    case ABS_EXPR:
    case FLOAT_EXPR:
      /* These don't change whether an object is nonzero or zero.  */
      return c_common_truthvalue_conversion (TREE_OPERAND (expr, 0));

    case LROTATE_EXPR:
    case RROTATE_EXPR:
      /* These don't change whether an object is zero or nonzero, but
	 we can't ignore them if their second arg has side-effects.  */
      if (TREE_SIDE_EFFECTS (TREE_OPERAND (expr, 1)))
	return build2 (COMPOUND_EXPR, truthvalue_type_node,
		       TREE_OPERAND (expr, 1),
		       c_common_truthvalue_conversion (TREE_OPERAND (expr, 0)));
      else
	return c_common_truthvalue_conversion (TREE_OPERAND (expr, 0));

    case COND_EXPR:
      /* Distribute the conversion into the arms of a COND_EXPR.  */
      return fold_build3 (COND_EXPR, truthvalue_type_node,
		TREE_OPERAND (expr, 0),
		c_common_truthvalue_conversion (TREE_OPERAND (expr, 1)),
		c_common_truthvalue_conversion (TREE_OPERAND (expr, 2)));

    case CONVERT_EXPR:
    case NOP_EXPR:
      /* Don't cancel the effect of a CONVERT_EXPR from a REFERENCE_TYPE,
	 since that affects how `default_conversion' will behave.  */
      if (TREE_CODE (TREE_TYPE (expr)) == REFERENCE_TYPE
	  || TREE_CODE (TREE_TYPE (TREE_OPERAND (expr, 0))) == REFERENCE_TYPE)
	break;
      /* If this is widening the argument, we can ignore it.  */
      if (TYPE_PRECISION (TREE_TYPE (expr))
	  >= TYPE_PRECISION (TREE_TYPE (TREE_OPERAND (expr, 0))))
	return c_common_truthvalue_conversion (TREE_OPERAND (expr, 0));
      break;

    case MODIFY_EXPR:
      if (!TREE_NO_WARNING (expr))
	warning (OPT_Wparentheses,
		 "suggest parentheses around assignment used as truth value");
      break;

    default:
      break;
    }

  if (TREE_CODE (TREE_TYPE (expr)) == COMPLEX_TYPE)
    {
      tree t = save_expr (expr);
      return (build_binary_op
	      ((TREE_SIDE_EFFECTS (expr)
		? TRUTH_OR_EXPR : TRUTH_ORIF_EXPR),
	c_common_truthvalue_conversion (build_unary_op (REALPART_EXPR, t, 0)),
	c_common_truthvalue_conversion (build_unary_op (IMAGPART_EXPR, t, 0)),
	       0));
    }

  return build_binary_op (NE_EXPR, expr, integer_zero_node, 1);
}

static void def_builtin_1  (enum built_in_function fncode,
			    const char *name,
			    enum built_in_class fnclass,
			    tree fntype, tree libtype,
			    bool both_p, bool fallback_p, bool nonansi_p,
			    tree fnattrs, bool implicit_p);

/* Make a variant type in the proper way for C/C++, propagating qualifiers
   down to the element type of an array.  */

tree
c_build_qualified_type (tree type, int type_quals)
{
  if (type == error_mark_node)
    return type;

  if (TREE_CODE (type) == ARRAY_TYPE)
    {
      tree t;
      tree element_type = c_build_qualified_type (TREE_TYPE (type),
						  type_quals);

      /* See if we already have an identically qualified type.  */
      for (t = TYPE_MAIN_VARIANT (type); t; t = TYPE_NEXT_VARIANT (t))
	{
	  if (TYPE_QUALS (strip_array_types (t)) == type_quals
	      && TYPE_NAME (t) == TYPE_NAME (type)
	      && TYPE_CONTEXT (t) == TYPE_CONTEXT (type)
	      && attribute_list_equal (TYPE_ATTRIBUTES (t),
				       TYPE_ATTRIBUTES (type)))
	    break;
	}
      if (!t)
	{
	  t = build_variant_type_copy (type);
	  TREE_TYPE (t) = element_type;
	}
      return t;
    }

  /* A restrict-qualified pointer type must be a pointer to object or
     incomplete type.  Note that the use of POINTER_TYPE_P also allows
     REFERENCE_TYPEs, which is appropriate for C++.  */
  if ((type_quals & TYPE_QUAL_RESTRICT)
      && (!POINTER_TYPE_P (type)
	  || !C_TYPE_OBJECT_OR_INCOMPLETE_P (TREE_TYPE (type))))
    {
      error ("invalid use of %<restrict%>");
      type_quals &= ~TYPE_QUAL_RESTRICT;
    }

  return build_qualified_type (type, type_quals);
}

/* Apply the TYPE_QUALS to the new DECL.  */

void
c_apply_type_quals_to_decl (int type_quals, tree decl)
{
  tree type = TREE_TYPE (decl);

  if (type == error_mark_node)
    return;

  if (((type_quals & TYPE_QUAL_CONST)
       || (type && TREE_CODE (type) == REFERENCE_TYPE))
      /* An object declared 'const' is only readonly after it is
	 initialized.  We don't have any way of expressing this currently,
	 so we need to be conservative and unset TREE_READONLY for types
	 with constructors.  Otherwise aliasing code will ignore stores in
	 an inline constructor.  */
      && !(type && TYPE_NEEDS_CONSTRUCTING (type)))
    TREE_READONLY (decl) = 1;
  if (type_quals & TYPE_QUAL_VOLATILE)
    {
      TREE_SIDE_EFFECTS (decl) = 1;
      TREE_THIS_VOLATILE (decl) = 1;
    }
  if (type_quals & TYPE_QUAL_RESTRICT)
    {
      while (type && TREE_CODE (type) == ARRAY_TYPE)
	/* Allow 'restrict' on arrays of pointers.
	   FIXME currently we just ignore it.  */
	type = TREE_TYPE (type);
      if (!type
	  || !POINTER_TYPE_P (type)
	  || !C_TYPE_OBJECT_OR_INCOMPLETE_P (TREE_TYPE (type)))
	error ("invalid use of %<restrict%>");
      else if (flag_strict_aliasing && type == TREE_TYPE (decl))
	/* Indicate we need to make a unique alias set for this pointer.
	   We can't do it here because it might be pointing to an
	   incomplete type.  */
	DECL_POINTER_ALIAS_SET (decl) = -2;
    }
}

/* Hash function for the problem of multiple type definitions in
   different files.  This must hash all types that will compare
   equal via comptypes to the same value.  In practice it hashes
   on some of the simple stuff and leaves the details to comptypes.  */

static hashval_t
c_type_hash (const void *p)
{
  int i = 0;
  int shift, size;
  tree t = (tree) p;
  tree t2;
  switch (TREE_CODE (t))
    {
    /* For pointers, hash on pointee type plus some swizzling.  */
    case POINTER_TYPE:
      return c_type_hash (TREE_TYPE (t)) ^ 0x3003003;
    /* Hash on number of elements and total size.  */
    case ENUMERAL_TYPE:
      shift = 3;
      t2 = TYPE_VALUES (t);
      break;
    case RECORD_TYPE:
      shift = 0;
      t2 = TYPE_FIELDS (t);
      break;
    case QUAL_UNION_TYPE:
      shift = 1;
      t2 = TYPE_FIELDS (t);
      break;
    case UNION_TYPE:
      shift = 2;
      t2 = TYPE_FIELDS (t);
      break;
    default:
      gcc_unreachable ();
    }
  for (; t2; t2 = TREE_CHAIN (t2))
    i++;
  size = TREE_INT_CST_LOW (TYPE_SIZE (t));
  return ((size << 24) | (i << shift));
}

static GTY((param_is (union tree_node))) htab_t type_hash_table;

/* Return the typed-based alias set for T, which may be an expression
   or a type.  Return -1 if we don't do anything special.  */

HOST_WIDE_INT
c_common_get_alias_set (tree t)
{
  tree u;
  PTR *slot;

  /* Permit type-punning when accessing a union, provided the access
     is directly through the union.  For example, this code does not
     permit taking the address of a union member and then storing
     through it.  Even the type-punning allowed here is a GCC
     extension, albeit a common and useful one; the C standard says
     that such accesses have implementation-defined behavior.  */
  for (u = t;
       TREE_CODE (u) == COMPONENT_REF || TREE_CODE (u) == ARRAY_REF;
       u = TREE_OPERAND (u, 0))
    if (TREE_CODE (u) == COMPONENT_REF
	&& TREE_CODE (TREE_TYPE (TREE_OPERAND (u, 0))) == UNION_TYPE)
      return 0;

  /* That's all the expressions we handle specially.  */
  if (!TYPE_P (t))
    return -1;

  /* The C standard guarantees that any object may be accessed via an
     lvalue that has character type.  */
  if (t == char_type_node
      || t == signed_char_type_node
      || t == unsigned_char_type_node)
    return 0;

  /* If it has the may_alias attribute, it can alias anything.  */
  if (lookup_attribute ("may_alias", TYPE_ATTRIBUTES (t)))
    return 0;

  /* The C standard specifically allows aliasing between signed and
     unsigned variants of the same type.  We treat the signed
     variant as canonical.  */
  if (TREE_CODE (t) == INTEGER_TYPE && TYPE_UNSIGNED (t))
    {
      tree t1 = c_common_signed_type (t);

      /* t1 == t can happen for boolean nodes which are always unsigned.  */
      if (t1 != t)
	return get_alias_set (t1);
    }
  else if (POINTER_TYPE_P (t))
    {
      tree t1;

      /* Unfortunately, there is no canonical form of a pointer type.
	 In particular, if we have `typedef int I', then `int *', and
	 `I *' are different types.  So, we have to pick a canonical
	 representative.  We do this below.

	 Technically, this approach is actually more conservative that
	 it needs to be.  In particular, `const int *' and `int *'
	 should be in different alias sets, according to the C and C++
	 standard, since their types are not the same, and so,
	 technically, an `int **' and `const int **' cannot point at
	 the same thing.

	 But, the standard is wrong.  In particular, this code is
	 legal C++:

	    int *ip;
	    int **ipp = &ip;
	    const int* const* cipp = ipp;

	 And, it doesn't make sense for that to be legal unless you
	 can dereference IPP and CIPP.  So, we ignore cv-qualifiers on
	 the pointed-to types.  This issue has been reported to the
	 C++ committee.  */
      t1 = build_type_no_quals (t);
      if (t1 != t)
	return get_alias_set (t1);
    }

  /* Handle the case of multiple type nodes referring to "the same" type,
     which occurs with IMA.  These share an alias set.  FIXME:  Currently only
     C90 is handled.  (In C99 type compatibility is not transitive, which
     complicates things mightily. The alias set splay trees can theoretically
     represent this, but insertion is tricky when you consider all the
     different orders things might arrive in.) */

  if (c_language != clk_c || flag_isoc99)
    return -1;

  /* Save time if there's only one input file.  */
  if (num_in_fnames == 1)
    return -1;

  /* Pointers need special handling if they point to any type that
     needs special handling (below).  */
  if (TREE_CODE (t) == POINTER_TYPE)
    {
      tree t2;
      /* Find bottom type under any nested POINTERs.  */
      for (t2 = TREE_TYPE (t);
     TREE_CODE (t2) == POINTER_TYPE;
     t2 = TREE_TYPE (t2))
  ;
      if (TREE_CODE (t2) != RECORD_TYPE
    && TREE_CODE (t2) != ENUMERAL_TYPE
    && TREE_CODE (t2) != QUAL_UNION_TYPE
    && TREE_CODE (t2) != UNION_TYPE)
  return -1;
      if (TYPE_SIZE (t2) == 0)
  return -1;
    }
  /* These are the only cases that need special handling.  */
  if (TREE_CODE (t) != RECORD_TYPE
      && TREE_CODE (t) != ENUMERAL_TYPE
      && TREE_CODE (t) != QUAL_UNION_TYPE
      && TREE_CODE (t) != UNION_TYPE
      && TREE_CODE (t) != POINTER_TYPE)
    return -1;
  /* Undefined? */
  if (TYPE_SIZE (t) == 0)
    return -1;

  /* Look up t in hash table.  Only one of the compatible types within each
     alias set is recorded in the table.  */
  if (!type_hash_table)
    type_hash_table = htab_create_ggc (1021, c_type_hash,
	    (htab_eq) lang_hooks.types_compatible_p,
	    NULL);
  slot = htab_find_slot (type_hash_table, t, INSERT);
  if (*slot != NULL)
    {
      TYPE_ALIAS_SET (t) = TYPE_ALIAS_SET ((tree)*slot);
      return TYPE_ALIAS_SET ((tree)*slot);
    }
  else
    /* Our caller will assign and record (in t) a new alias set; all we need
       to do is remember t in the hash table.  */
    *slot = t;

  return -1;
}

/* Compute the value of 'sizeof (TYPE)' or '__alignof__ (TYPE)', where the
   second parameter indicates which OPERATOR is being applied.  The COMPLAIN
   flag controls whether we should diagnose possibly ill-formed
   constructs or not.  */

tree
c_sizeof_or_alignof_type (tree type, bool is_sizeof, int complain)
{
  const char *op_name;
  tree value = NULL;
  enum tree_code type_code = TREE_CODE (type);

  op_name = is_sizeof ? "sizeof" : "__alignof__";

  if (type_code == FUNCTION_TYPE)
    {
      if (is_sizeof)
	{
	  if (complain && (pedantic || warn_pointer_arith))
	    pedwarn ("invalid application of %<sizeof%> to a function type");
	  value = size_one_node;
	}
      else
	value = size_int (FUNCTION_BOUNDARY / BITS_PER_UNIT);
    }
  else if (type_code == VOID_TYPE || type_code == ERROR_MARK)
    {
      if (type_code == VOID_TYPE
	  && complain && (pedantic || warn_pointer_arith))
	pedwarn ("invalid application of %qs to a void type", op_name);
      value = size_one_node;
    }
  else if (!COMPLETE_TYPE_P (type))
    {
      if (complain)
	error ("invalid application of %qs to incomplete type %qT ",
	       op_name, type);
      value = size_zero_node;
    }
  else
    {
      if (is_sizeof)
	/* Convert in case a char is more than one unit.  */
	value = size_binop (CEIL_DIV_EXPR, TYPE_SIZE_UNIT (type),
			    size_int (TYPE_PRECISION (char_type_node)
				      / BITS_PER_UNIT));
      else
	value = size_int (TYPE_ALIGN_UNIT (type));
    }

  /* VALUE will have an integer type with TYPE_IS_SIZETYPE set.
     TYPE_IS_SIZETYPE means that certain things (like overflow) will
     never happen.  However, this node should really have type
     `size_t', which is just a typedef for an ordinary integer type.  */
  value = fold_convert (size_type_node, value);
  gcc_assert (!TYPE_IS_SIZETYPE (TREE_TYPE (value)));

  return value;
}

/* Implement the __alignof keyword: Return the minimum required
   alignment of EXPR, measured in bytes.  For VAR_DECL's and
   FIELD_DECL's return DECL_ALIGN (which can be set from an
   "aligned" __attribute__ specification).  */

tree
c_alignof_expr (tree expr)
{
  tree t;

  if (TREE_CODE (expr) == VAR_DECL)
    t = size_int (DECL_ALIGN_UNIT (expr));

  else if (TREE_CODE (expr) == COMPONENT_REF
	   && DECL_C_BIT_FIELD (TREE_OPERAND (expr, 1)))
    {
      error ("%<__alignof%> applied to a bit-field");
      t = size_one_node;
    }
  else if (TREE_CODE (expr) == COMPONENT_REF
	   && TREE_CODE (TREE_OPERAND (expr, 1)) == FIELD_DECL)
    t = size_int (DECL_ALIGN_UNIT (TREE_OPERAND (expr, 1)));

  else if (TREE_CODE (expr) == INDIRECT_REF)
    {
      tree t = TREE_OPERAND (expr, 0);
      tree best = t;
      int bestalign = TYPE_ALIGN (TREE_TYPE (TREE_TYPE (t)));

      while ((TREE_CODE (t) == NOP_EXPR || TREE_CODE (t) == CONVERT_EXPR)
	     && TREE_CODE (TREE_TYPE (TREE_OPERAND (t, 0))) == POINTER_TYPE)
	{
	  int thisalign;

	  t = TREE_OPERAND (t, 0);
	  thisalign = TYPE_ALIGN (TREE_TYPE (TREE_TYPE (t)));
	  if (thisalign > bestalign)
	    best = t, bestalign = thisalign;
	}
      return c_alignof (TREE_TYPE (TREE_TYPE (best)));
    }
  else
    return c_alignof (TREE_TYPE (expr));

  return fold_convert (size_type_node, t);
}

/* Handle C and C++ default attributes.  */

enum built_in_attribute
{
#define DEF_ATTR_NULL_TREE(ENUM) ENUM,
#define DEF_ATTR_INT(ENUM, VALUE) ENUM,
#define DEF_ATTR_IDENT(ENUM, STRING) ENUM,
#define DEF_ATTR_TREE_LIST(ENUM, PURPOSE, VALUE, CHAIN) ENUM,
#include "builtin-attrs.def"
#undef DEF_ATTR_NULL_TREE
#undef DEF_ATTR_INT
#undef DEF_ATTR_IDENT
#undef DEF_ATTR_TREE_LIST
  ATTR_LAST
};

static GTY(()) tree built_in_attributes[(int) ATTR_LAST];

static void c_init_attributes (void);

enum c_builtin_type
{
#define DEF_PRIMITIVE_TYPE(NAME, VALUE) NAME,
#define DEF_FUNCTION_TYPE_0(NAME, RETURN) NAME,
#define DEF_FUNCTION_TYPE_1(NAME, RETURN, ARG1) NAME,
#define DEF_FUNCTION_TYPE_2(NAME, RETURN, ARG1, ARG2) NAME,
#define DEF_FUNCTION_TYPE_3(NAME, RETURN, ARG1, ARG2, ARG3) NAME,
#define DEF_FUNCTION_TYPE_4(NAME, RETURN, ARG1, ARG2, ARG3, ARG4) NAME,
#define DEF_FUNCTION_TYPE_5(NAME, RETURN, ARG1, ARG2, ARG3, ARG4, ARG5) NAME,
#define DEF_FUNCTION_TYPE_6(NAME, RETURN, ARG1, ARG2, ARG3, ARG4, ARG5, ARG6) NAME,
#define DEF_FUNCTION_TYPE_7(NAME, RETURN, ARG1, ARG2, ARG3, ARG4, ARG5, ARG6, ARG7) NAME,
#define DEF_FUNCTION_TYPE_VAR_0(NAME, RETURN) NAME,
#define DEF_FUNCTION_TYPE_VAR_1(NAME, RETURN, ARG1) NAME,
#define DEF_FUNCTION_TYPE_VAR_2(NAME, RETURN, ARG1, ARG2) NAME,
#define DEF_FUNCTION_TYPE_VAR_3(NAME, RETURN, ARG1, ARG2, ARG3) NAME,
#define DEF_FUNCTION_TYPE_VAR_4(NAME, RETURN, ARG1, ARG2, ARG3, ARG4) NAME,
#define DEF_FUNCTION_TYPE_VAR_5(NAME, RETURN, ARG1, ARG2, ARG3, ARG4, ARG6) \
  NAME,
#define DEF_POINTER_TYPE(NAME, TYPE) NAME,
#include "builtin-types.def"
#undef DEF_PRIMITIVE_TYPE
#undef DEF_FUNCTION_TYPE_0
#undef DEF_FUNCTION_TYPE_1
#undef DEF_FUNCTION_TYPE_2
#undef DEF_FUNCTION_TYPE_3
#undef DEF_FUNCTION_TYPE_4
#undef DEF_FUNCTION_TYPE_5
#undef DEF_FUNCTION_TYPE_6
#undef DEF_FUNCTION_TYPE_7
#undef DEF_FUNCTION_TYPE_VAR_0
#undef DEF_FUNCTION_TYPE_VAR_1
#undef DEF_FUNCTION_TYPE_VAR_2
#undef DEF_FUNCTION_TYPE_VAR_3
#undef DEF_FUNCTION_TYPE_VAR_4
#undef DEF_FUNCTION_TYPE_VAR_5
#undef DEF_POINTER_TYPE
  BT_LAST
};

typedef enum c_builtin_type builtin_type;

/* A temporary array for c_common_nodes_and_builtins.  Used in
   communication with def_fn_type.  */
static tree builtin_types[(int) BT_LAST + 1];

/* A helper function for c_common_nodes_and_builtins.  Build function type
   for DEF with return type RET and N arguments.  If VAR is true, then the
   function should be variadic after those N arguments.

   Takes special care not to ICE if any of the types involved are
   error_mark_node, which indicates that said type is not in fact available
   (see builtin_type_for_size).  In which case the function type as a whole
   should be error_mark_node.  */

static void
def_fn_type (builtin_type def, builtin_type ret, bool var, int n, ...)
{
  tree args = NULL, t;
  va_list list;
  int i;

  va_start (list, n);
  for (i = 0; i < n; ++i)
    {
      builtin_type a = va_arg (list, builtin_type);
      t = builtin_types[a];
      if (t == error_mark_node)
	goto egress;
      args = tree_cons (NULL_TREE, t, args);
    }
  va_end (list);

  args = nreverse (args);
  if (!var)
    args = chainon (args, void_list_node);

  t = builtin_types[ret];
  if (t == error_mark_node)
    goto egress;
  t = build_function_type (t, args);

 egress:
  builtin_types[def] = t;
}

/* Build tree nodes and builtin functions common to both C and C++ language
   frontends.  */

void
c_common_nodes_and_builtins (void)
{
  int wchar_type_size;
  tree array_domain_type;
  tree va_list_ref_type_node;
  tree va_list_arg_type_node;

  /* Define `int' and `char' first so that dbx will output them first.  */
  record_builtin_type (RID_INT, NULL, integer_type_node);
  record_builtin_type (RID_CHAR, "char", char_type_node);

  /* `signed' is the same as `int'.  FIXME: the declarations of "signed",
     "unsigned long", "long long unsigned" and "unsigned short" were in C++
     but not C.  Are the conditionals here needed?  */
  if (c_dialect_cxx ())
    record_builtin_type (RID_SIGNED, NULL, integer_type_node);
  record_builtin_type (RID_LONG, "long int", long_integer_type_node);
  record_builtin_type (RID_UNSIGNED, "unsigned int", unsigned_type_node);
  record_builtin_type (RID_MAX, "long unsigned int",
		       long_unsigned_type_node);
  if (c_dialect_cxx ())
    record_builtin_type (RID_MAX, "unsigned long", long_unsigned_type_node);
  record_builtin_type (RID_MAX, "long long int",
		       long_long_integer_type_node);
  record_builtin_type (RID_MAX, "long long unsigned int",
		       long_long_unsigned_type_node);
  if (c_dialect_cxx ())
    record_builtin_type (RID_MAX, "long long unsigned",
			 long_long_unsigned_type_node);
  record_builtin_type (RID_SHORT, "short int", short_integer_type_node);
  record_builtin_type (RID_MAX, "short unsigned int",
		       short_unsigned_type_node);
  if (c_dialect_cxx ())
    record_builtin_type (RID_MAX, "unsigned short",
			 short_unsigned_type_node);

  /* Define both `signed char' and `unsigned char'.  */
  record_builtin_type (RID_MAX, "signed char", signed_char_type_node);
  record_builtin_type (RID_MAX, "unsigned char", unsigned_char_type_node);

  /* These are types that c_common_type_for_size and
     c_common_type_for_mode use.  */
  lang_hooks.decls.pushdecl (build_decl (TYPE_DECL, NULL_TREE,
					 intQI_type_node));
  lang_hooks.decls.pushdecl (build_decl (TYPE_DECL, NULL_TREE,
					 intHI_type_node));
  lang_hooks.decls.pushdecl (build_decl (TYPE_DECL, NULL_TREE,
					 intSI_type_node));
  lang_hooks.decls.pushdecl (build_decl (TYPE_DECL, NULL_TREE,
					 intDI_type_node));
#if HOST_BITS_PER_WIDE_INT >= 64
  if (targetm.scalar_mode_supported_p (TImode))
    lang_hooks.decls.pushdecl (build_decl (TYPE_DECL,
					   get_identifier ("__int128_t"),
					   intTI_type_node));
#endif
  lang_hooks.decls.pushdecl (build_decl (TYPE_DECL, NULL_TREE,
					 unsigned_intQI_type_node));
  lang_hooks.decls.pushdecl (build_decl (TYPE_DECL, NULL_TREE,
					 unsigned_intHI_type_node));
  lang_hooks.decls.pushdecl (build_decl (TYPE_DECL, NULL_TREE,
					 unsigned_intSI_type_node));
  lang_hooks.decls.pushdecl (build_decl (TYPE_DECL, NULL_TREE,
					 unsigned_intDI_type_node));
#if HOST_BITS_PER_WIDE_INT >= 64
  if (targetm.scalar_mode_supported_p (TImode))
    lang_hooks.decls.pushdecl (build_decl (TYPE_DECL,
					   get_identifier ("__uint128_t"),
					   unsigned_intTI_type_node));
#endif

  /* Create the widest literal types.  */
  widest_integer_literal_type_node
    = make_signed_type (HOST_BITS_PER_WIDE_INT * 2);
  lang_hooks.decls.pushdecl (build_decl (TYPE_DECL, NULL_TREE,
					 widest_integer_literal_type_node));

  widest_unsigned_literal_type_node
    = make_unsigned_type (HOST_BITS_PER_WIDE_INT * 2);
  lang_hooks.decls.pushdecl (build_decl (TYPE_DECL, NULL_TREE,
					 widest_unsigned_literal_type_node));

  /* `unsigned long' is the standard type for sizeof.
     Note that stddef.h uses `unsigned long',
     and this must agree, even if long and int are the same size.  */
  size_type_node =
    TREE_TYPE (identifier_global_value (get_identifier (SIZE_TYPE)));
  signed_size_type_node = c_common_signed_type (size_type_node);
  set_sizetype (size_type_node);

  pid_type_node =
    TREE_TYPE (identifier_global_value (get_identifier (PID_TYPE)));

  build_common_tree_nodes_2 (flag_short_double);

  record_builtin_type (RID_FLOAT, NULL, float_type_node);
  record_builtin_type (RID_DOUBLE, NULL, double_type_node);
  record_builtin_type (RID_MAX, "long double", long_double_type_node);

  /* Only supported decimal floating point extension if the target
     actually supports underlying modes. */
  if (targetm.scalar_mode_supported_p (SDmode) 
      && targetm.scalar_mode_supported_p (DDmode)
      && targetm.scalar_mode_supported_p (TDmode))
    {
      record_builtin_type (RID_DFLOAT32, NULL, dfloat32_type_node);
      record_builtin_type (RID_DFLOAT64, NULL, dfloat64_type_node);
      record_builtin_type (RID_DFLOAT128, NULL, dfloat128_type_node);
    }

  lang_hooks.decls.pushdecl (build_decl (TYPE_DECL,
					 get_identifier ("complex int"),
					 complex_integer_type_node));
  lang_hooks.decls.pushdecl (build_decl (TYPE_DECL,
					 get_identifier ("complex float"),
					 complex_float_type_node));
  lang_hooks.decls.pushdecl (build_decl (TYPE_DECL,
					 get_identifier ("complex double"),
					 complex_double_type_node));
  lang_hooks.decls.pushdecl
    (build_decl (TYPE_DECL, get_identifier ("complex long double"),
		 complex_long_double_type_node));

  if (c_dialect_cxx ())
    /* For C++, make fileptr_type_node a distinct void * type until
       FILE type is defined.  */
    fileptr_type_node = build_variant_type_copy (ptr_type_node);

  record_builtin_type (RID_VOID, NULL, void_type_node);

  /* This node must not be shared.  */
  void_zero_node = make_node (INTEGER_CST);
  TREE_TYPE (void_zero_node) = void_type_node;

  void_list_node = build_void_list_node ();

  /* Make a type to be the domain of a few array types
     whose domains don't really matter.
     200 is small enough that it always fits in size_t
     and large enough that it can hold most function names for the
     initializations of __FUNCTION__ and __PRETTY_FUNCTION__.  */
  array_domain_type = build_index_type (size_int (200));

  /* Make a type for arrays of characters.
     With luck nothing will ever really depend on the length of this
     array type.  */
  char_array_type_node
    = build_array_type (char_type_node, array_domain_type);

  /* Likewise for arrays of ints.  */
  int_array_type_node
    = build_array_type (integer_type_node, array_domain_type);

  string_type_node = build_pointer_type (char_type_node);
  const_string_type_node
    = build_pointer_type (build_qualified_type
			  (char_type_node, TYPE_QUAL_CONST));

  /* This is special for C++ so functions can be overloaded.  */
  wchar_type_node = get_identifier (MODIFIED_WCHAR_TYPE);
  wchar_type_node = TREE_TYPE (identifier_global_value (wchar_type_node));
  wchar_type_size = TYPE_PRECISION (wchar_type_node);
  if (c_dialect_cxx ())
    {
      if (TYPE_UNSIGNED (wchar_type_node))
	wchar_type_node = make_unsigned_type (wchar_type_size);
      else
	wchar_type_node = make_signed_type (wchar_type_size);
      record_builtin_type (RID_WCHAR, "wchar_t", wchar_type_node);
    }
  else
    {
      signed_wchar_type_node = c_common_signed_type (wchar_type_node);
      unsigned_wchar_type_node = c_common_unsigned_type (wchar_type_node);
    }

  /* This is for wide string constants.  */
  wchar_array_type_node
    = build_array_type (wchar_type_node, array_domain_type);

  wint_type_node =
    TREE_TYPE (identifier_global_value (get_identifier (WINT_TYPE)));

  intmax_type_node =
    TREE_TYPE (identifier_global_value (get_identifier (INTMAX_TYPE)));
  uintmax_type_node =
    TREE_TYPE (identifier_global_value (get_identifier (UINTMAX_TYPE)));

  default_function_type = build_function_type (integer_type_node, NULL_TREE);
  ptrdiff_type_node
    = TREE_TYPE (identifier_global_value (get_identifier (PTRDIFF_TYPE)));
  unsigned_ptrdiff_type_node = c_common_unsigned_type (ptrdiff_type_node);

  lang_hooks.decls.pushdecl
    (build_decl (TYPE_DECL, get_identifier ("__builtin_va_list"),
		 va_list_type_node));

  if (TREE_CODE (va_list_type_node) == ARRAY_TYPE)
    {
      va_list_arg_type_node = va_list_ref_type_node =
	build_pointer_type (TREE_TYPE (va_list_type_node));
    }
  else
    {
      va_list_arg_type_node = va_list_type_node;
      va_list_ref_type_node = build_reference_type (va_list_type_node);
    }

#define DEF_PRIMITIVE_TYPE(ENUM, VALUE) \
  builtin_types[ENUM] = VALUE;
#define DEF_FUNCTION_TYPE_0(ENUM, RETURN) \
  def_fn_type (ENUM, RETURN, 0, 0);
#define DEF_FUNCTION_TYPE_1(ENUM, RETURN, ARG1) \
  def_fn_type (ENUM, RETURN, 0, 1, ARG1);
#define DEF_FUNCTION_TYPE_2(ENUM, RETURN, ARG1, ARG2) \
  def_fn_type (ENUM, RETURN, 0, 2, ARG1, ARG2);
#define DEF_FUNCTION_TYPE_3(ENUM, RETURN, ARG1, ARG2, ARG3) \
  def_fn_type (ENUM, RETURN, 0, 3, ARG1, ARG2, ARG3);
#define DEF_FUNCTION_TYPE_4(ENUM, RETURN, ARG1, ARG2, ARG3, ARG4) \
  def_fn_type (ENUM, RETURN, 0, 4, ARG1, ARG2, ARG3, ARG4);
#define DEF_FUNCTION_TYPE_5(ENUM, RETURN, ARG1, ARG2, ARG3, ARG4, ARG5)	\
  def_fn_type (ENUM, RETURN, 0, 5, ARG1, ARG2, ARG3, ARG4, ARG5);
#define DEF_FUNCTION_TYPE_6(ENUM, RETURN, ARG1, ARG2, ARG3, ARG4, ARG5, \
			    ARG6)					\
  def_fn_type (ENUM, RETURN, 0, 6, ARG1, ARG2, ARG3, ARG4, ARG5, ARG6);
#define DEF_FUNCTION_TYPE_7(ENUM, RETURN, ARG1, ARG2, ARG3, ARG4, ARG5, \
			    ARG6, ARG7)					\
  def_fn_type (ENUM, RETURN, 0, 7, ARG1, ARG2, ARG3, ARG4, ARG5, ARG6, ARG7);
#define DEF_FUNCTION_TYPE_VAR_0(ENUM, RETURN) \
  def_fn_type (ENUM, RETURN, 1, 0);
#define DEF_FUNCTION_TYPE_VAR_1(ENUM, RETURN, ARG1) \
  def_fn_type (ENUM, RETURN, 1, 1, ARG1);
#define DEF_FUNCTION_TYPE_VAR_2(ENUM, RETURN, ARG1, ARG2) \
  def_fn_type (ENUM, RETURN, 1, 2, ARG1, ARG2);
#define DEF_FUNCTION_TYPE_VAR_3(ENUM, RETURN, ARG1, ARG2, ARG3) \
  def_fn_type (ENUM, RETURN, 1, 3, ARG1, ARG2, ARG3);
#define DEF_FUNCTION_TYPE_VAR_4(ENUM, RETURN, ARG1, ARG2, ARG3, ARG4) \
  def_fn_type (ENUM, RETURN, 1, 4, ARG1, ARG2, ARG3, ARG4);
#define DEF_FUNCTION_TYPE_VAR_5(ENUM, RETURN, ARG1, ARG2, ARG3, ARG4, ARG5) \
  def_fn_type (ENUM, RETURN, 1, 5, ARG1, ARG2, ARG3, ARG4, ARG5);
#define DEF_POINTER_TYPE(ENUM, TYPE) \
  builtin_types[(int) ENUM] = build_pointer_type (builtin_types[(int) TYPE]);

#include "builtin-types.def"

#undef DEF_PRIMITIVE_TYPE
#undef DEF_FUNCTION_TYPE_1
#undef DEF_FUNCTION_TYPE_2
#undef DEF_FUNCTION_TYPE_3
#undef DEF_FUNCTION_TYPE_4
#undef DEF_FUNCTION_TYPE_5
#undef DEF_FUNCTION_TYPE_6
#undef DEF_FUNCTION_TYPE_VAR_0
#undef DEF_FUNCTION_TYPE_VAR_1
#undef DEF_FUNCTION_TYPE_VAR_2
#undef DEF_FUNCTION_TYPE_VAR_3
#undef DEF_FUNCTION_TYPE_VAR_4
#undef DEF_FUNCTION_TYPE_VAR_5
#undef DEF_POINTER_TYPE
  builtin_types[(int) BT_LAST] = NULL_TREE;

  c_init_attributes ();

#define DEF_BUILTIN(ENUM, NAME, CLASS, TYPE, LIBTYPE, BOTH_P, FALLBACK_P, \
		    NONANSI_P, ATTRS, IMPLICIT, COND)			\
  if (NAME && COND)							\
    def_builtin_1 (ENUM, NAME, CLASS,                                   \
		   builtin_types[(int) TYPE],                           \
		   builtin_types[(int) LIBTYPE],                        \
		   BOTH_P, FALLBACK_P, NONANSI_P,                       \
		   built_in_attributes[(int) ATTRS], IMPLICIT);
#include "builtins.def"
#undef DEF_BUILTIN

  build_common_builtin_nodes ();

  targetm.init_builtins ();
  if (flag_mudflap)
    mudflap_init ();

  main_identifier_node = get_identifier ("main");

  /* Create the built-in __null node.  It is important that this is
     not shared.  */
  null_node = make_node (INTEGER_CST);
  TREE_TYPE (null_node) = c_common_type_for_size (POINTER_SIZE, 0);

  /* Since builtin_types isn't gc'ed, don't export these nodes.  */
  memset (builtin_types, 0, sizeof (builtin_types));
}

/* Look up the function in built_in_decls that corresponds to DECL
   and set ASMSPEC as its user assembler name.  DECL must be a
   function decl that declares a builtin.  */

void
set_builtin_user_assembler_name (tree decl, const char *asmspec)
{
  tree builtin;
  gcc_assert (TREE_CODE (decl) == FUNCTION_DECL
	      && DECL_BUILT_IN_CLASS (decl) == BUILT_IN_NORMAL
	      && asmspec != 0);

  builtin = built_in_decls [DECL_FUNCTION_CODE (decl)];
  set_user_assembler_name (builtin, asmspec);
  if (DECL_FUNCTION_CODE (decl) == BUILT_IN_MEMCPY)
    init_block_move_fn (decl, asmspec);
  else if (DECL_FUNCTION_CODE (decl) == BUILT_IN_MEMSET)
    init_block_clear_fn (decl, asmspec);
}

/* The number of named compound-literals generated thus far.  */
static GTY(()) int compound_literal_number;

/* Set DECL_NAME for DECL, a VAR_DECL for a compound-literal.  */

void
set_compound_literal_name (tree decl)
{
  char *name;
  ASM_FORMAT_PRIVATE_NAME (name, "__compound_literal",
			   compound_literal_number);
  compound_literal_number++;
  DECL_NAME (decl) = get_identifier (name);
}

tree
build_va_arg (tree expr, tree type)
{
  return build1 (VA_ARG_EXPR, type, expr);
}


/* Linked list of disabled built-in functions.  */

typedef struct disabled_builtin
{
  const char *name;
  struct disabled_builtin *next;
} disabled_builtin;
static disabled_builtin *disabled_builtins = NULL;

static bool builtin_function_disabled_p (const char *);

/* Disable a built-in function specified by -fno-builtin-NAME.  If NAME
   begins with "__builtin_", give an error.  */

void
disable_builtin_function (const char *name)
{
  if (strncmp (name, "__builtin_", strlen ("__builtin_")) == 0)
    error ("cannot disable built-in function %qs", name);
  else
    {
      disabled_builtin *new_disabled_builtin = XNEW (disabled_builtin);
      new_disabled_builtin->name = name;
      new_disabled_builtin->next = disabled_builtins;
      disabled_builtins = new_disabled_builtin;
    }
}


/* Return true if the built-in function NAME has been disabled, false
   otherwise.  */

static bool
builtin_function_disabled_p (const char *name)
{
  disabled_builtin *p;
  for (p = disabled_builtins; p != NULL; p = p->next)
    {
      if (strcmp (name, p->name) == 0)
	return true;
    }
  return false;
}


/* Worker for DEF_BUILTIN.
   Possibly define a builtin function with one or two names.
   Does not declare a non-__builtin_ function if flag_no_builtin, or if
   nonansi_p and flag_no_nonansi_builtin.  */

static void
def_builtin_1 (enum built_in_function fncode,
	       const char *name,
	       enum built_in_class fnclass,
	       tree fntype, tree libtype,
	       bool both_p, bool fallback_p, bool nonansi_p,
	       tree fnattrs, bool implicit_p)
{
  tree decl;
  const char *libname;

  if (fntype == error_mark_node)
    return;

  gcc_assert ((!both_p && !fallback_p)
	      || !strncmp (name, "__builtin_",
			   strlen ("__builtin_")));

  libname = name + strlen ("__builtin_");
  decl = lang_hooks.builtin_function (name, fntype, fncode, fnclass,
				      (fallback_p ? libname : NULL),
				      fnattrs);
  if (both_p
      && !flag_no_builtin && !builtin_function_disabled_p (libname)
      && !(nonansi_p && flag_no_nonansi_builtin))
    lang_hooks.builtin_function (libname, libtype, fncode, fnclass,
				 NULL, fnattrs);

  built_in_decls[(int) fncode] = decl;
  if (implicit_p)
    implicit_built_in_decls[(int) fncode] = decl;
}

/* Nonzero if the type T promotes to int.  This is (nearly) the
   integral promotions defined in ISO C99 6.3.1.1/2.  */

bool
c_promoting_integer_type_p (tree t)
{
  switch (TREE_CODE (t))
    {
    case INTEGER_TYPE:
      return (TYPE_MAIN_VARIANT (t) == char_type_node
	      || TYPE_MAIN_VARIANT (t) == signed_char_type_node
	      || TYPE_MAIN_VARIANT (t) == unsigned_char_type_node
	      || TYPE_MAIN_VARIANT (t) == short_integer_type_node
	      || TYPE_MAIN_VARIANT (t) == short_unsigned_type_node
	      || TYPE_PRECISION (t) < TYPE_PRECISION (integer_type_node));

    case ENUMERAL_TYPE:
      /* ??? Technically all enumerations not larger than an int
	 promote to an int.  But this is used along code paths
	 that only want to notice a size change.  */
      return TYPE_PRECISION (t) < TYPE_PRECISION (integer_type_node);

    case BOOLEAN_TYPE:
      return 1;

    default:
      return 0;
    }
}

/* Return 1 if PARMS specifies a fixed number of parameters
   and none of their types is affected by default promotions.  */

int
self_promoting_args_p (tree parms)
{
  tree t;
  for (t = parms; t; t = TREE_CHAIN (t))
    {
      tree type = TREE_VALUE (t);

      if (type == error_mark_node)
	continue;

      if (TREE_CHAIN (t) == 0 && type != void_type_node)
	return 0;

      if (type == 0)
	return 0;

      if (TYPE_MAIN_VARIANT (type) == float_type_node)
	return 0;

      if (c_promoting_integer_type_p (type))
	return 0;
    }
  return 1;
}

/* Recursively examines the array elements of TYPE, until a non-array
   element type is found.  */

tree
strip_array_types (tree type)
{
  while (TREE_CODE (type) == ARRAY_TYPE)
    type = TREE_TYPE (type);

  return type;
}

/* Recursively remove any '*' or '&' operator from TYPE.  */
tree
strip_pointer_operator (tree t)
{
  while (POINTER_TYPE_P (t))
    t = TREE_TYPE (t);
  return t;
}

/* Used to compare case labels.  K1 and K2 are actually tree nodes
   representing case labels, or NULL_TREE for a `default' label.
   Returns -1 if K1 is ordered before K2, -1 if K1 is ordered after
   K2, and 0 if K1 and K2 are equal.  */

int
case_compare (splay_tree_key k1, splay_tree_key k2)
{
  /* Consider a NULL key (such as arises with a `default' label) to be
     smaller than anything else.  */
  if (!k1)
    return k2 ? -1 : 0;
  else if (!k2)
    return k1 ? 1 : 0;

  return tree_int_cst_compare ((tree) k1, (tree) k2);
}

/* Process a case label for the range LOW_VALUE ... HIGH_VALUE.  If
   LOW_VALUE and HIGH_VALUE are both NULL_TREE then this case label is
   actually a `default' label.  If only HIGH_VALUE is NULL_TREE, then
   case label was declared using the usual C/C++ syntax, rather than
   the GNU case range extension.  CASES is a tree containing all the
   case ranges processed so far; COND is the condition for the
   switch-statement itself.  Returns the CASE_LABEL_EXPR created, or
   ERROR_MARK_NODE if no CASE_LABEL_EXPR is created.  */

tree
c_add_case_label (splay_tree cases, tree cond, tree orig_type,
		  tree low_value, tree high_value)
{
  tree type;
  tree label;
  tree case_label;
  splay_tree_node node;

  /* Create the LABEL_DECL itself.  */
  label = create_artificial_label ();

  /* If there was an error processing the switch condition, bail now
     before we get more confused.  */
  if (!cond || cond == error_mark_node)
    goto error_out;

  if ((low_value && TREE_TYPE (low_value)
       && POINTER_TYPE_P (TREE_TYPE (low_value)))
      || (high_value && TREE_TYPE (high_value)
	  && POINTER_TYPE_P (TREE_TYPE (high_value))))
    {
      error ("pointers are not permitted as case values");
      goto error_out;
    }

  /* Case ranges are a GNU extension.  */
  if (high_value && pedantic)
    pedwarn ("range expressions in switch statements are non-standard");

  type = TREE_TYPE (cond);
  if (low_value)
    {
      low_value = check_case_value (low_value);
      low_value = convert_and_check (type, low_value);
      if (low_value == error_mark_node)
	goto error_out;
    }
  if (high_value)
    {
      high_value = check_case_value (high_value);
      high_value = convert_and_check (type, high_value);
      if (high_value == error_mark_node)
	goto error_out;
    }

  if (low_value && high_value)
    {
      /* If the LOW_VALUE and HIGH_VALUE are the same, then this isn't
	 really a case range, even though it was written that way.
	 Remove the HIGH_VALUE to simplify later processing.  */
      if (tree_int_cst_equal (low_value, high_value))
	high_value = NULL_TREE;
      else if (!tree_int_cst_lt (low_value, high_value))
	warning (0, "empty range specified");
    }

  /* See if the case is in range of the type of the original testing
     expression.  If both low_value and high_value are out of range,
     don't insert the case label and return NULL_TREE.  */
  if (low_value
      && !check_case_bounds (type, orig_type,
			     &low_value, high_value ? &high_value : NULL))
    return NULL_TREE;

  /* Look up the LOW_VALUE in the table of case labels we already
     have.  */
  node = splay_tree_lookup (cases, (splay_tree_key) low_value);
  /* If there was not an exact match, check for overlapping ranges.
     There's no need to do this if there's no LOW_VALUE or HIGH_VALUE;
     that's a `default' label and the only overlap is an exact match.  */
  if (!node && (low_value || high_value))
    {
      splay_tree_node low_bound;
      splay_tree_node high_bound;

      /* Even though there wasn't an exact match, there might be an
	 overlap between this case range and another case range.
	 Since we've (inductively) not allowed any overlapping case
	 ranges, we simply need to find the greatest low case label
	 that is smaller that LOW_VALUE, and the smallest low case
	 label that is greater than LOW_VALUE.  If there is an overlap
	 it will occur in one of these two ranges.  */
      low_bound = splay_tree_predecessor (cases,
					  (splay_tree_key) low_value);
      high_bound = splay_tree_successor (cases,
					 (splay_tree_key) low_value);

      /* Check to see if the LOW_BOUND overlaps.  It is smaller than
	 the LOW_VALUE, so there is no need to check unless the
	 LOW_BOUND is in fact itself a case range.  */
      if (low_bound
	  && CASE_HIGH ((tree) low_bound->value)
	  && tree_int_cst_compare (CASE_HIGH ((tree) low_bound->value),
				    low_value) >= 0)
	node = low_bound;
      /* Check to see if the HIGH_BOUND overlaps.  The low end of that
	 range is bigger than the low end of the current range, so we
	 are only interested if the current range is a real range, and
	 not an ordinary case label.  */
      else if (high_bound
	       && high_value
	       && (tree_int_cst_compare ((tree) high_bound->key,
					 high_value)
		   <= 0))
	node = high_bound;
    }
  /* If there was an overlap, issue an error.  */
  if (node)
    {
      tree duplicate = CASE_LABEL ((tree) node->value);

      if (high_value)
	{
	  error ("duplicate (or overlapping) case value");
	  error ("%Jthis is the first entry overlapping that value", duplicate);
	}
      else if (low_value)
	{
	  error ("duplicate case value") ;
	  error ("%Jpreviously used here", duplicate);
	}
      else
	{
	  error ("multiple default labels in one switch");
	  error ("%Jthis is the first default label", duplicate);
	}
      goto error_out;
    }

  /* Add a CASE_LABEL to the statement-tree.  */
  case_label = add_stmt (build_case_label (low_value, high_value, label));
  /* Register this case label in the splay tree.  */
  splay_tree_insert (cases,
		     (splay_tree_key) low_value,
		     (splay_tree_value) case_label);

  return case_label;

 error_out:
  /* Add a label so that the back-end doesn't think that the beginning of
     the switch is unreachable.  Note that we do not add a case label, as
     that just leads to duplicates and thence to failure later on.  */
  if (!cases->root)
    {
      tree t = create_artificial_label ();
      add_stmt (build_stmt (LABEL_EXPR, t));
    }
  return error_mark_node;
}

/* Subroutines of c_do_switch_warnings, called via splay_tree_foreach.
   Used to verify that case values match up with enumerator values.  */

static void
match_case_to_enum_1 (tree key, tree type, tree label)
{
  char buf[2 + 2*HOST_BITS_PER_WIDE_INT/4 + 1];

  /* ??? Not working too hard to print the double-word value.
     Should perhaps be done with %lwd in the diagnostic routines?  */
  if (TREE_INT_CST_HIGH (key) == 0)
    snprintf (buf, sizeof (buf), HOST_WIDE_INT_PRINT_UNSIGNED,
	      TREE_INT_CST_LOW (key));
  else if (!TYPE_UNSIGNED (type)
	   && TREE_INT_CST_HIGH (key) == -1
	   && TREE_INT_CST_LOW (key) != 0)
    snprintf (buf, sizeof (buf), "-" HOST_WIDE_INT_PRINT_UNSIGNED,
	      -TREE_INT_CST_LOW (key));
  else
    snprintf (buf, sizeof (buf), HOST_WIDE_INT_PRINT_DOUBLE_HEX,
	      TREE_INT_CST_HIGH (key), TREE_INT_CST_LOW (key));

  if (TYPE_NAME (type) == 0)
    warning (0, "%Jcase value %qs not in enumerated type",
	     CASE_LABEL (label), buf);
  else
    warning (0, "%Jcase value %qs not in enumerated type %qT",
	     CASE_LABEL (label), buf, type);
}

/* Subroutine of c_do_switch_warnings, called via splay_tree_foreach.
   Used to verify that case values match up with enumerator values.  */

static int
match_case_to_enum (splay_tree_node node, void *data)
{
  tree label = (tree) node->value;
  tree type = (tree) data;

  /* Skip default case.  */
  if (!CASE_LOW (label))
    return 0;

  /* If CASE_LOW_SEEN is not set, that means CASE_LOW did not appear
     when we did our enum->case scan.  Reset our scratch bit after.  */
  if (!CASE_LOW_SEEN (label))
    match_case_to_enum_1 (CASE_LOW (label), type, label);
  else
    CASE_LOW_SEEN (label) = 0;

  /* If CASE_HIGH is non-null, we have a range.  If CASE_HIGH_SEEN is
     not set, that means that CASE_HIGH did not appear when we did our
     enum->case scan.  Reset our scratch bit after.  */
  if (CASE_HIGH (label))
    {
      if (!CASE_HIGH_SEEN (label))
	match_case_to_enum_1 (CASE_HIGH (label), type, label);
      else
	CASE_HIGH_SEEN (label) = 0;
    }

  return 0;
}

/* Handle -Wswitch*.  Called from the front end after parsing the
   switch construct.  */
/* ??? Should probably be somewhere generic, since other languages
   besides C and C++ would want this.  At the moment, however, C/C++
   are the only tree-ssa languages that support enumerations at all,
   so the point is moot.  */

void
c_do_switch_warnings (splay_tree cases, location_t switch_location,
		      tree type, tree cond)
{
  splay_tree_node default_node;
  splay_tree_node node;
  tree chain;

  if (!warn_switch && !warn_switch_enum && !warn_switch_default)
    return;

  default_node = splay_tree_lookup (cases, (splay_tree_key) NULL);
  if (!default_node)
    warning (OPT_Wswitch_default, "%Hswitch missing default case",
	     &switch_location);

  /* From here on, we only care about about enumerated types.  */
  if (!type || TREE_CODE (type) != ENUMERAL_TYPE)
    return;

  /* If the switch expression was an enumerated type, check that
     exactly all enumeration literals are covered by the cases.
     The check is made when -Wswitch was specified and there is no
     default case, or when -Wswitch-enum was specified.  */

  if (!warn_switch_enum
      && !(warn_switch && !default_node))
    return;

  /* Clearing COND if it is not an integer constant simplifies
     the tests inside the loop below.  */
  if (TREE_CODE (cond) != INTEGER_CST)
    cond = NULL_TREE;

  /* The time complexity here is O(N*lg(N)) worst case, but for the
      common case of monotonically increasing enumerators, it is
      O(N), since the nature of the splay tree will keep the next
      element adjacent to the root at all times.  */

  for (chain = TYPE_VALUES (type); chain; chain = TREE_CHAIN (chain))
    {
      tree value = TREE_VALUE (chain);
      node = splay_tree_lookup (cases, (splay_tree_key) value);
      if (node)
	{
	  /* Mark the CASE_LOW part of the case entry as seen.  */
	  tree label = (tree) node->value;
	  CASE_LOW_SEEN (label) = 1;
	  continue;
	}

      /* Even though there wasn't an exact match, there might be a
	 case range which includes the enumator's value.  */
      node = splay_tree_predecessor (cases, (splay_tree_key) value);
      if (node && CASE_HIGH ((tree) node->value))
	{
	  tree label = (tree) node->value;
	  int cmp = tree_int_cst_compare (CASE_HIGH (label), value);
	  if (cmp >= 0)
	    {
	      /* If we match the upper bound exactly, mark the CASE_HIGH
		 part of the case entry as seen.  */
	      if (cmp == 0)
		CASE_HIGH_SEEN (label) = 1;
	      continue;
	    }
	}

      /* We've now determined that this enumerated literal isn't
	 handled by the case labels of the switch statement.  */

      /* If the switch expression is a constant, we only really care
	 about whether that constant is handled by the switch.  */
      if (cond && tree_int_cst_compare (cond, value))
	continue;

      warning (0, "%Henumeration value %qE not handled in switch",
	       &switch_location, TREE_PURPOSE (chain));
    }

  /* Warn if there are case expressions that don't correspond to
     enumerators.  This can occur since C and C++ don't enforce
     type-checking of assignments to enumeration variables.

     The time complexity here is now always O(N) worst case, since
     we should have marked both the lower bound and upper bound of
     every disjoint case label, with CASE_LOW_SEEN and CASE_HIGH_SEEN
     above.  This scan also resets those fields.  */
  splay_tree_foreach (cases, match_case_to_enum, type);
}

/* Finish an expression taking the address of LABEL (an
   IDENTIFIER_NODE).  Returns an expression for the address.  */

tree
finish_label_address_expr (tree label)
{
  tree result;

  if (pedantic)
    pedwarn ("taking the address of a label is non-standard");

  if (label == error_mark_node)
    return error_mark_node;

  label = lookup_label (label);
  if (label == NULL_TREE)
    result = null_pointer_node;
  else
    {
      TREE_USED (label) = 1;
      result = build1 (ADDR_EXPR, ptr_type_node, label);
      /* The current function in not necessarily uninlinable.
	 Computed gotos are incompatible with inlining, but the value
	 here could be used only in a diagnostic, for example.  */
    }

  return result;
}

/* Hook used by expand_expr to expand language-specific tree codes.  */
/* The only things that should go here are bits needed to expand
   constant initializers.  Everything else should be handled by the
   gimplification routines.  */

rtx
c_expand_expr (tree exp, rtx target, enum machine_mode tmode,
	       int modifier /* Actually enum_modifier.  */,
	       rtx *alt_rtl)
{
  switch (TREE_CODE (exp))
    {
    case COMPOUND_LITERAL_EXPR:
      {
	/* Initialize the anonymous variable declared in the compound
	   literal, then return the variable.  */
	tree decl = COMPOUND_LITERAL_EXPR_DECL (exp);
	emit_local_var (decl);
	return expand_expr_real (decl, target, tmode, modifier, alt_rtl);
      }

    default:
      gcc_unreachable ();
    }
}

/* Hook used by staticp to handle language-specific tree codes.  */

tree
c_staticp (tree exp)
{
  return (TREE_CODE (exp) == COMPOUND_LITERAL_EXPR
	  && TREE_STATIC (COMPOUND_LITERAL_EXPR_DECL (exp))
	  ? exp : NULL);
}


/* Given a boolean expression ARG, return a tree representing an increment
   or decrement (as indicated by CODE) of ARG.  The front end must check for
   invalid cases (e.g., decrement in C++).  */
tree
boolean_increment (enum tree_code code, tree arg)
{
  tree val;
  tree true_res = boolean_true_node;

  arg = stabilize_reference (arg);
  switch (code)
    {
    case PREINCREMENT_EXPR:
      val = build2 (MODIFY_EXPR, TREE_TYPE (arg), arg, true_res);
      break;
    case POSTINCREMENT_EXPR:
      val = build2 (MODIFY_EXPR, TREE_TYPE (arg), arg, true_res);
      arg = save_expr (arg);
      val = build2 (COMPOUND_EXPR, TREE_TYPE (arg), val, arg);
      val = build2 (COMPOUND_EXPR, TREE_TYPE (arg), arg, val);
      break;
    case PREDECREMENT_EXPR:
      val = build2 (MODIFY_EXPR, TREE_TYPE (arg), arg,
		    invert_truthvalue (arg));
      break;
    case POSTDECREMENT_EXPR:
      val = build2 (MODIFY_EXPR, TREE_TYPE (arg), arg,
		    invert_truthvalue (arg));
      arg = save_expr (arg);
      val = build2 (COMPOUND_EXPR, TREE_TYPE (arg), val, arg);
      val = build2 (COMPOUND_EXPR, TREE_TYPE (arg), arg, val);
      break;
    default:
      gcc_unreachable ();
    }
  TREE_SIDE_EFFECTS (val) = 1;
  return val;
}

/* Built-in macros for stddef.h, that require macros defined in this
   file.  */
void
c_stddef_cpp_builtins(void)
{
  builtin_define_with_value ("__SIZE_TYPE__", SIZE_TYPE, 0);
  builtin_define_with_value ("__PTRDIFF_TYPE__", PTRDIFF_TYPE, 0);
  builtin_define_with_value ("__WCHAR_TYPE__", MODIFIED_WCHAR_TYPE, 0);
  builtin_define_with_value ("__WINT_TYPE__", WINT_TYPE, 0);
  builtin_define_with_value ("__INTMAX_TYPE__", INTMAX_TYPE, 0);
  builtin_define_with_value ("__UINTMAX_TYPE__", UINTMAX_TYPE, 0);
}

static void
c_init_attributes (void)
{
  /* Fill in the built_in_attributes array.  */
#define DEF_ATTR_NULL_TREE(ENUM)				\
  built_in_attributes[(int) ENUM] = NULL_TREE;
#define DEF_ATTR_INT(ENUM, VALUE)				\
  built_in_attributes[(int) ENUM] = build_int_cst (NULL_TREE, VALUE);
#define DEF_ATTR_IDENT(ENUM, STRING)				\
  built_in_attributes[(int) ENUM] = get_identifier (STRING);
#define DEF_ATTR_TREE_LIST(ENUM, PURPOSE, VALUE, CHAIN)	\
  built_in_attributes[(int) ENUM]			\
    = tree_cons (built_in_attributes[(int) PURPOSE],	\
		 built_in_attributes[(int) VALUE],	\
		 built_in_attributes[(int) CHAIN]);
#include "builtin-attrs.def"
#undef DEF_ATTR_NULL_TREE
#undef DEF_ATTR_INT
#undef DEF_ATTR_IDENT
#undef DEF_ATTR_TREE_LIST
}

/* Attribute handlers common to C front ends.  */

/* Handle a "packed" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_packed_attribute (tree *node, tree name, tree ARG_UNUSED (args),
			 int flags, bool *no_add_attrs)
{
  if (TYPE_P (*node))
    {
      if (!(flags & (int) ATTR_FLAG_TYPE_IN_PLACE))
	*node = build_variant_type_copy (*node);
      TYPE_PACKED (*node) = 1;
    }
  else if (TREE_CODE (*node) == FIELD_DECL)
    {
      if (TYPE_ALIGN (TREE_TYPE (*node)) <= BITS_PER_UNIT)
	warning (OPT_Wattributes,
		 "%qE attribute ignored for field of type %qT",
		 name, TREE_TYPE (*node));
      else
	DECL_PACKED (*node) = 1;
    }
  /* We can't set DECL_PACKED for a VAR_DECL, because the bit is
     used for DECL_REGISTER.  It wouldn't mean anything anyway.
     We can't set DECL_PACKED on the type of a TYPE_DECL, because
     that changes what the typedef is typing.  */
  else
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "nocommon" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_nocommon_attribute (tree *node, tree name,
			   tree ARG_UNUSED (args),
			   int ARG_UNUSED (flags), bool *no_add_attrs)
{
  if (TREE_CODE (*node) == VAR_DECL)
    DECL_COMMON (*node) = 0;
  else
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "common" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_common_attribute (tree *node, tree name, tree ARG_UNUSED (args),
			 int ARG_UNUSED (flags), bool *no_add_attrs)
{
  if (TREE_CODE (*node) == VAR_DECL)
    DECL_COMMON (*node) = 1;
  else
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "noreturn" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_noreturn_attribute (tree *node, tree name, tree ARG_UNUSED (args),
			   int ARG_UNUSED (flags), bool *no_add_attrs)
{
  tree type = TREE_TYPE (*node);

  /* See FIXME comment in c_common_attribute_table.  */
  if (TREE_CODE (*node) == FUNCTION_DECL)
    TREE_THIS_VOLATILE (*node) = 1;
  else if (TREE_CODE (type) == POINTER_TYPE
	   && TREE_CODE (TREE_TYPE (type)) == FUNCTION_TYPE)
    TREE_TYPE (*node)
      = build_pointer_type
	(build_type_variant (TREE_TYPE (type),
			     TYPE_READONLY (TREE_TYPE (type)), 1));
  else
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "noinline" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_noinline_attribute (tree *node, tree name,
			   tree ARG_UNUSED (args),
			   int ARG_UNUSED (flags), bool *no_add_attrs)
{
  if (TREE_CODE (*node) == FUNCTION_DECL)
    DECL_UNINLINABLE (*node) = 1;
  else
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "always_inline" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_always_inline_attribute (tree *node, tree name,
				tree ARG_UNUSED (args),
				int ARG_UNUSED (flags),
				bool *no_add_attrs)
{
  if (TREE_CODE (*node) == FUNCTION_DECL)
    {
      /* Do nothing else, just set the attribute.  We'll get at
	 it later with lookup_attribute.  */
    }
  else
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "gnu_inline" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_gnu_inline_attribute (tree *node, tree name,
			     tree ARG_UNUSED (args),
			     int ARG_UNUSED (flags),
			     bool *no_add_attrs)
{
  if (TREE_CODE (*node) == FUNCTION_DECL && DECL_DECLARED_INLINE_P (*node))
    {
      /* Do nothing else, just set the attribute.  We'll get at
	 it later with lookup_attribute.  */
    }
  else
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "flatten" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_flatten_attribute (tree *node, tree name,
			  tree args ATTRIBUTE_UNUSED,
			  int flags ATTRIBUTE_UNUSED, bool *no_add_attrs)
{
  if (TREE_CODE (*node) == FUNCTION_DECL)
    /* Do nothing else, just set the attribute.  We'll get at
       it later with lookup_attribute.  */
    ;
  else
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}


/* Handle a "used" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_used_attribute (tree *pnode, tree name, tree ARG_UNUSED (args),
		       int ARG_UNUSED (flags), bool *no_add_attrs)
{
  tree node = *pnode;

  if (TREE_CODE (node) == FUNCTION_DECL
      || (TREE_CODE (node) == VAR_DECL && TREE_STATIC (node)))
    {
      TREE_USED (node) = 1;
      DECL_PRESERVE_P (node) = 1;
    }
  else
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "unused" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_unused_attribute (tree *node, tree name, tree ARG_UNUSED (args),
			 int flags, bool *no_add_attrs)
{
  if (DECL_P (*node))
    {
      tree decl = *node;

      if (TREE_CODE (decl) == PARM_DECL
	  || TREE_CODE (decl) == VAR_DECL
	  || TREE_CODE (decl) == FUNCTION_DECL
	  || TREE_CODE (decl) == LABEL_DECL
	  || TREE_CODE (decl) == TYPE_DECL)
	TREE_USED (decl) = 1;
      else
	{
	  warning (OPT_Wattributes, "%qE attribute ignored", name);
	  *no_add_attrs = true;
	}
    }
  else
    {
      if (!(flags & (int) ATTR_FLAG_TYPE_IN_PLACE))
	*node = build_variant_type_copy (*node);
      TREE_USED (*node) = 1;
    }

  return NULL_TREE;
}

/* Handle a "externally_visible" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_externally_visible_attribute (tree *pnode, tree name,
				     tree ARG_UNUSED (args),
				     int ARG_UNUSED (flags),
				     bool *no_add_attrs)
{
  tree node = *pnode;

  if (TREE_CODE (node) == FUNCTION_DECL || TREE_CODE (node) == VAR_DECL)
    {
      if ((!TREE_STATIC (node) && TREE_CODE (node) != FUNCTION_DECL
	   && !DECL_EXTERNAL (node)) || !TREE_PUBLIC (node))
	{
	  warning (OPT_Wattributes,
		   "%qE attribute have effect only on public objects", name);
	  *no_add_attrs = true;
	}
    }
  else
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "const" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_const_attribute (tree *node, tree name, tree ARG_UNUSED (args),
			int ARG_UNUSED (flags), bool *no_add_attrs)
{
  tree type = TREE_TYPE (*node);

  /* See FIXME comment on noreturn in c_common_attribute_table.  */
  if (TREE_CODE (*node) == FUNCTION_DECL)
    TREE_READONLY (*node) = 1;
  else if (TREE_CODE (type) == POINTER_TYPE
	   && TREE_CODE (TREE_TYPE (type)) == FUNCTION_TYPE)
    TREE_TYPE (*node)
      = build_pointer_type
	(build_type_variant (TREE_TYPE (type), 1,
			     TREE_THIS_VOLATILE (TREE_TYPE (type))));
  else
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "transparent_union" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_transparent_union_attribute (tree *node, tree name,
				    tree ARG_UNUSED (args), int flags,
				    bool *no_add_attrs)
{
  tree type = NULL;

  *no_add_attrs = true;

  if (DECL_P (*node))
    {
      if (TREE_CODE (*node) != TYPE_DECL)
	goto ignored;
      node = &TREE_TYPE (*node);
      type = *node;
    }
  else if (TYPE_P (*node))
    type = *node;
  else
    goto ignored;

  if (TREE_CODE (type) == UNION_TYPE)
    {
      /* When IN_PLACE is set, leave the check for FIELDS and MODE to
	 the code in finish_struct.  */
      if (!(flags & (int) ATTR_FLAG_TYPE_IN_PLACE))
	{
	  if (TYPE_FIELDS (type) == NULL_TREE
	      || TYPE_MODE (type) != DECL_MODE (TYPE_FIELDS (type)))
	    goto ignored;

	  /* A type variant isn't good enough, since we don't a cast
	     to such a type removed as a no-op.  */
	  *node = type = build_duplicate_type (type);
	}

      TYPE_TRANSPARENT_UNION (type) = 1;
      return NULL_TREE;
    }

 ignored:
  warning (OPT_Wattributes, "%qE attribute ignored", name);
  return NULL_TREE;
}

/* Handle a "constructor" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_constructor_attribute (tree *node, tree name,
			      tree ARG_UNUSED (args),
			      int ARG_UNUSED (flags),
			      bool *no_add_attrs)
{
  tree decl = *node;
  tree type = TREE_TYPE (decl);

  if (TREE_CODE (decl) == FUNCTION_DECL
      && TREE_CODE (type) == FUNCTION_TYPE
      && decl_function_context (decl) == 0)
    {
      DECL_STATIC_CONSTRUCTOR (decl) = 1;
      TREE_USED (decl) = 1;
    }
  else
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "destructor" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_destructor_attribute (tree *node, tree name,
			     tree ARG_UNUSED (args),
			     int ARG_UNUSED (flags),
			     bool *no_add_attrs)
{
  tree decl = *node;
  tree type = TREE_TYPE (decl);

  if (TREE_CODE (decl) == FUNCTION_DECL
      && TREE_CODE (type) == FUNCTION_TYPE
      && decl_function_context (decl) == 0)
    {
      DECL_STATIC_DESTRUCTOR (decl) = 1;
      TREE_USED (decl) = 1;
    }
  else
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "mode" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_mode_attribute (tree *node, tree name, tree args,
		       int ARG_UNUSED (flags), bool *no_add_attrs)
{
  tree type = *node;

  *no_add_attrs = true;

  if (TREE_CODE (TREE_VALUE (args)) != IDENTIFIER_NODE)
    warning (OPT_Wattributes, "%qE attribute ignored", name);
  else
    {
      int j;
      const char *p = IDENTIFIER_POINTER (TREE_VALUE (args));
      int len = strlen (p);
      enum machine_mode mode = VOIDmode;
      tree typefm;
      bool valid_mode;

      if (len > 4 && p[0] == '_' && p[1] == '_'
	  && p[len - 1] == '_' && p[len - 2] == '_')
	{
	  char *newp = (char *) alloca (len - 1);

	  strcpy (newp, &p[2]);
	  newp[len - 4] = '\0';
	  p = newp;
	}

      /* Change this type to have a type with the specified mode.
	 First check for the special modes.  */
      if (!strcmp (p, "byte"))
	mode = byte_mode;
      else if (!strcmp (p, "word"))
	mode = word_mode;
      else if (!strcmp (p, "pointer"))
	mode = ptr_mode;
      else
	for (j = 0; j < NUM_MACHINE_MODES; j++)
	  if (!strcmp (p, GET_MODE_NAME (j)))
	    {
	      mode = (enum machine_mode) j;
	      break;
	    }

      if (mode == VOIDmode)
	{
	  error ("unknown machine mode %qs", p);
	  return NULL_TREE;
	}

      valid_mode = false;
      switch (GET_MODE_CLASS (mode))
	{
	case MODE_INT:
	case MODE_PARTIAL_INT:
	case MODE_FLOAT:
	case MODE_DECIMAL_FLOAT:
	  valid_mode = targetm.scalar_mode_supported_p (mode);
	  break;

	case MODE_COMPLEX_INT:
	case MODE_COMPLEX_FLOAT:
	  valid_mode = targetm.scalar_mode_supported_p (GET_MODE_INNER (mode));
	  break;

	case MODE_VECTOR_INT:
	case MODE_VECTOR_FLOAT:
	  warning (OPT_Wattributes, "specifying vector types with "
		   "__attribute__ ((mode)) is deprecated");
	  warning (OPT_Wattributes,
		   "use __attribute__ ((vector_size)) instead");
	  valid_mode = vector_mode_valid_p (mode);
	  break;

	default:
	  break;
	}
      if (!valid_mode)
	{
	  error ("unable to emulate %qs", p);
	  return NULL_TREE;
	}

      if (POINTER_TYPE_P (type))
	{
	  tree (*fn)(tree, enum machine_mode, bool);

	  if (!targetm.valid_pointer_mode (mode))
	    {
	      error ("invalid pointer mode %qs", p);
	      return NULL_TREE;
	    }

	  if (TREE_CODE (type) == POINTER_TYPE)
	    fn = build_pointer_type_for_mode;
	  else
	    fn = build_reference_type_for_mode;
	  typefm = fn (TREE_TYPE (type), mode, false);
	}
      else
	typefm = lang_hooks.types.type_for_mode (mode, TYPE_UNSIGNED (type));

      if (typefm == NULL_TREE)
	{
	  error ("no data type for mode %qs", p);
	  return NULL_TREE;
	}
      else if (TREE_CODE (type) == ENUMERAL_TYPE)
	{
	  /* For enumeral types, copy the precision from the integer
	     type returned above.  If not an INTEGER_TYPE, we can't use
	     this mode for this type.  */
	  if (TREE_CODE (typefm) != INTEGER_TYPE)
	    {
	      error ("cannot use mode %qs for enumeral types", p);
	      return NULL_TREE;
	    }

	  if (flags & ATTR_FLAG_TYPE_IN_PLACE)
	    {
	      TYPE_PRECISION (type) = TYPE_PRECISION (typefm);
	      typefm = type;
	    }
	  else
	    {
	      /* We cannot build a type variant, as there's code that assumes
		 that TYPE_MAIN_VARIANT has the same mode.  This includes the
		 debug generators.  Instead, create a subrange type.  This
		 results in all of the enumeral values being emitted only once
		 in the original, and the subtype gets them by reference.  */
	      if (TYPE_UNSIGNED (type))
		typefm = make_unsigned_type (TYPE_PRECISION (typefm));
	      else
		typefm = make_signed_type (TYPE_PRECISION (typefm));
	      TREE_TYPE (typefm) = type;
	    }
	}
      else if (VECTOR_MODE_P (mode)
	       ? TREE_CODE (type) != TREE_CODE (TREE_TYPE (typefm))
	       : TREE_CODE (type) != TREE_CODE (typefm))
	{
	  error ("mode %qs applied to inappropriate type", p);
	  return NULL_TREE;
	}

      *node = typefm;
    }

  return NULL_TREE;
}

/* Handle a "section" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_section_attribute (tree *node, tree ARG_UNUSED (name), tree args,
			  int ARG_UNUSED (flags), bool *no_add_attrs)
{
  tree decl = *node;

  if (targetm.have_named_sections)
    {
      user_defined_section_attribute = true;

      if ((TREE_CODE (decl) == FUNCTION_DECL
	   || TREE_CODE (decl) == VAR_DECL)
	  && TREE_CODE (TREE_VALUE (args)) == STRING_CST)
	{
	  if (TREE_CODE (decl) == VAR_DECL
	      && current_function_decl != NULL_TREE
	      && !TREE_STATIC (decl))
	    {
	      error ("%Jsection attribute cannot be specified for "
		     "local variables", decl);
	      *no_add_attrs = true;
	    }

	  /* The decl may have already been given a section attribute
	     from a previous declaration.  Ensure they match.  */
	  else if (DECL_SECTION_NAME (decl) != NULL_TREE
		   && strcmp (TREE_STRING_POINTER (DECL_SECTION_NAME (decl)),
			      TREE_STRING_POINTER (TREE_VALUE (args))) != 0)
	    {
	      error ("section of %q+D conflicts with previous declaration",
		     *node);
	      *no_add_attrs = true;
	    }
	  else
	    DECL_SECTION_NAME (decl) = TREE_VALUE (args);
	}
      else
	{
	  error ("section attribute not allowed for %q+D", *node);
	  *no_add_attrs = true;
	}
    }
  else
    {
      error ("%Jsection attributes are not supported for this target", *node);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "aligned" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_aligned_attribute (tree *node, tree ARG_UNUSED (name), tree args,
			  int flags, bool *no_add_attrs)
{
  tree decl = NULL_TREE;
  tree *type = NULL;
  int is_type = 0;
  tree align_expr = (args ? TREE_VALUE (args)
		     : size_int (BIGGEST_ALIGNMENT / BITS_PER_UNIT));
  int i;

  if (DECL_P (*node))
    {
      decl = *node;
      type = &TREE_TYPE (decl);
      is_type = TREE_CODE (*node) == TYPE_DECL;
    }
  else if (TYPE_P (*node))
    type = node, is_type = 1;

  if (TREE_CODE (align_expr) != INTEGER_CST)
    {
      error ("requested alignment is not a constant");
      *no_add_attrs = true;
    }
  else if ((i = tree_log2 (align_expr)) == -1)
    {
      error ("requested alignment is not a power of 2");
      *no_add_attrs = true;
    }
  else if (i > HOST_BITS_PER_INT - 2)
    {
      error ("requested alignment is too large");
      *no_add_attrs = true;
    }
  else if (is_type)
    {
      /* If we have a TYPE_DECL, then copy the type, so that we
	 don't accidentally modify a builtin type.  See pushdecl.  */
      if (decl && TREE_TYPE (decl) != error_mark_node
	  && DECL_ORIGINAL_TYPE (decl) == NULL_TREE)
	{
	  tree tt = TREE_TYPE (decl);
	  *type = build_variant_type_copy (*type);
	  DECL_ORIGINAL_TYPE (decl) = tt;
	  TYPE_NAME (*type) = decl;
	  TREE_USED (*type) = TREE_USED (decl);
	  TREE_TYPE (decl) = *type;
	}
      else if (!(flags & (int) ATTR_FLAG_TYPE_IN_PLACE))
	*type = build_variant_type_copy (*type);

      TYPE_ALIGN (*type) = (1 << i) * BITS_PER_UNIT;
      TYPE_USER_ALIGN (*type) = 1;
    }
  else if (TREE_CODE (decl) != VAR_DECL
	   && TREE_CODE (decl) != FIELD_DECL)
    {
      error ("alignment may not be specified for %q+D", decl);
      *no_add_attrs = true;
    }
  else
    {
      DECL_ALIGN (decl) = (1 << i) * BITS_PER_UNIT;
      DECL_USER_ALIGN (decl) = 1;
    }

  return NULL_TREE;
}

/* Handle a "weak" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_weak_attribute (tree *node, tree name,
		       tree ARG_UNUSED (args),
		       int ARG_UNUSED (flags),
		       bool * ARG_UNUSED (no_add_attrs))
{
  if (TREE_CODE (*node) == FUNCTION_DECL
      || TREE_CODE (*node) == VAR_DECL)
    declare_weak (*node);
  else
    warning (OPT_Wattributes, "%qE attribute ignored", name);
    	

  return NULL_TREE;
}

/* Handle an "alias" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_alias_attribute (tree *node, tree name, tree args,
			int ARG_UNUSED (flags), bool *no_add_attrs)
{
  tree decl = *node;

  if ((TREE_CODE (decl) == FUNCTION_DECL && DECL_INITIAL (decl))
      || (TREE_CODE (decl) != FUNCTION_DECL 
	  && TREE_PUBLIC (decl) && !DECL_EXTERNAL (decl))
      /* A static variable declaration is always a tentative definition,
	 but the alias is a non-tentative definition which overrides.  */
      || (TREE_CODE (decl) != FUNCTION_DECL 
	  && ! TREE_PUBLIC (decl) && DECL_INITIAL (decl)))
    {
      error ("%q+D defined both normally and as an alias", decl);
      *no_add_attrs = true;
    }

  /* Note that the very first time we process a nested declaration,
     decl_function_context will not be set.  Indeed, *would* never
     be set except for the DECL_INITIAL/DECL_EXTERNAL frobbery that
     we do below.  After such frobbery, pushdecl would set the context.
     In any case, this is never what we want.  */
  else if (decl_function_context (decl) == 0 && current_function_decl == NULL)
    {
      tree id;

      id = TREE_VALUE (args);
      if (TREE_CODE (id) != STRING_CST)
	{
	  error ("alias argument not a string");
	  *no_add_attrs = true;
	  return NULL_TREE;
	}
      id = get_identifier (TREE_STRING_POINTER (id));
      /* This counts as a use of the object pointed to.  */
      TREE_USED (id) = 1;

      if (TREE_CODE (decl) == FUNCTION_DECL)
	DECL_INITIAL (decl) = error_mark_node;
      else
	{
	  if (lookup_attribute ("weakref", DECL_ATTRIBUTES (decl)))
	    DECL_EXTERNAL (decl) = 1;
	  else
	    DECL_EXTERNAL (decl) = 0;
	  TREE_STATIC (decl) = 1;
	}
    }
  else
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "weakref" attribute; arguments as in struct
   attribute_spec.handler.  */

static tree
handle_weakref_attribute (tree *node, tree ARG_UNUSED (name), tree args,
			  int flags, bool *no_add_attrs)
{
  tree attr = NULL_TREE;

  /* We must ignore the attribute when it is associated with
     local-scoped decls, since attribute alias is ignored and many
     such symbols do not even have a DECL_WEAK field.  */
  if (decl_function_context (*node) || current_function_decl)
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
      return NULL_TREE;
    }

  /* The idea here is that `weakref("name")' mutates into `weakref,
     alias("name")', and weakref without arguments, in turn,
     implicitly adds weak. */

  if (args)
    {
      attr = tree_cons (get_identifier ("alias"), args, attr);
      attr = tree_cons (get_identifier ("weakref"), NULL_TREE, attr);

      *no_add_attrs = true;

      decl_attributes (node, attr, flags);
    }
  else
    {
      if (lookup_attribute ("alias", DECL_ATTRIBUTES (*node)))
	error ("%Jweakref attribute must appear before alias attribute",
	       *node);

      /* Can't call declare_weak because it wants this to be TREE_PUBLIC,
	 and that isn't supported; and because it wants to add it to
	 the list of weak decls, which isn't helpful.  */
      DECL_WEAK (*node) = 1;
    }

  return NULL_TREE;
}

/* Handle an "visibility" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_visibility_attribute (tree *node, tree name, tree args,
			     int ARG_UNUSED (flags),
			     bool *ARG_UNUSED (no_add_attrs))
{
  tree decl = *node;
  tree id = TREE_VALUE (args);
  enum symbol_visibility vis;

  if (TYPE_P (*node))
    {
      if (TREE_CODE (*node) == ENUMERAL_TYPE)
	/* OK */;
      else if (TREE_CODE (*node) != RECORD_TYPE && TREE_CODE (*node) != UNION_TYPE)
	{
	  warning (OPT_Wattributes, "%qE attribute ignored on non-class types",
		   name);
	  return NULL_TREE;
	}
      else if (TYPE_FIELDS (*node))
	{
	  error ("%qE attribute ignored because %qT is already defined",
		 name, *node);
	  return NULL_TREE;
	}
    }
  else if (decl_function_context (decl) != 0 || !TREE_PUBLIC (decl))
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      return NULL_TREE;
    }

  if (TREE_CODE (id) != STRING_CST)
    {
      error ("visibility argument not a string");
      return NULL_TREE;
    }

  /*  If this is a type, set the visibility on the type decl.  */
  if (TYPE_P (decl))
    {
      decl = TYPE_NAME (decl);
      if (!decl)
	return NULL_TREE;
      if (TREE_CODE (decl) == IDENTIFIER_NODE)
	{
	   warning (OPT_Wattributes, "%qE attribute ignored on types",
		    name);
	   return NULL_TREE;
	}
    }

  if (strcmp (TREE_STRING_POINTER (id), "default") == 0)
    vis = VISIBILITY_DEFAULT;
  else if (strcmp (TREE_STRING_POINTER (id), "internal") == 0)
    vis = VISIBILITY_INTERNAL;
  else if (strcmp (TREE_STRING_POINTER (id), "hidden") == 0)
    vis = VISIBILITY_HIDDEN;
  else if (strcmp (TREE_STRING_POINTER (id), "protected") == 0)
    vis = VISIBILITY_PROTECTED;
  else
    {
      error ("visibility argument must be one of \"default\", \"hidden\", \"protected\" or \"internal\"");
      vis = VISIBILITY_DEFAULT;
    }

  if (DECL_VISIBILITY_SPECIFIED (decl)
      && vis != DECL_VISIBILITY (decl)
      && lookup_attribute ("visibility", (TYPE_P (*node)
					  ? TYPE_ATTRIBUTES (*node)
					  : DECL_ATTRIBUTES (decl))))
    error ("%qD redeclared with different visibility", decl);

  DECL_VISIBILITY (decl) = vis;
  DECL_VISIBILITY_SPECIFIED (decl) = 1;

  /* Go ahead and attach the attribute to the node as well.  This is needed
     so we can determine whether we have VISIBILITY_DEFAULT because the
     visibility was not specified, or because it was explicitly overridden
     from the containing scope.  */

  return NULL_TREE;
}

/* Determine the ELF symbol visibility for DECL, which is either a
   variable or a function.  It is an error to use this function if a
   definition of DECL is not available in this translation unit.
   Returns true if the final visibility has been determined by this
   function; false if the caller is free to make additional
   modifications.  */

bool
c_determine_visibility (tree decl)
{
  gcc_assert (TREE_CODE (decl) == VAR_DECL
	      || TREE_CODE (decl) == FUNCTION_DECL);

  /* If the user explicitly specified the visibility with an
     attribute, honor that.  DECL_VISIBILITY will have been set during
     the processing of the attribute.  We check for an explicit
     attribute, rather than just checking DECL_VISIBILITY_SPECIFIED,
     to distinguish the use of an attribute from the use of a "#pragma
     GCC visibility push(...)"; in the latter case we still want other
     considerations to be able to overrule the #pragma.  */
  if (lookup_attribute ("visibility", DECL_ATTRIBUTES (decl)))
    return true;

  /* Anything that is exported must have default visibility.  */
  if (TARGET_DLLIMPORT_DECL_ATTRIBUTES
      && lookup_attribute ("dllexport", DECL_ATTRIBUTES (decl)))
    {
      DECL_VISIBILITY (decl) = VISIBILITY_DEFAULT;
      DECL_VISIBILITY_SPECIFIED (decl) = 1;
      return true;
    }

  /* Set default visibility to whatever the user supplied with
     visibility_specified depending on #pragma GCC visibility.  */
  if (!DECL_VISIBILITY_SPECIFIED (decl))
    {
      DECL_VISIBILITY (decl) = default_visibility;
      DECL_VISIBILITY_SPECIFIED (decl) = visibility_options.inpragma;
    }
  return false;
}

/* Handle an "tls_model" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_tls_model_attribute (tree *node, tree name, tree args,
			    int ARG_UNUSED (flags), bool *no_add_attrs)
{
  tree id;
  tree decl = *node;
  enum tls_model kind;

  *no_add_attrs = true;

  if (!DECL_THREAD_LOCAL_P (decl))
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      return NULL_TREE;
    }

  kind = DECL_TLS_MODEL (decl);
  id = TREE_VALUE (args);
  if (TREE_CODE (id) != STRING_CST)
    {
      error ("tls_model argument not a string");
      return NULL_TREE;
    }

  if (!strcmp (TREE_STRING_POINTER (id), "local-exec"))
    kind = TLS_MODEL_LOCAL_EXEC;
  else if (!strcmp (TREE_STRING_POINTER (id), "initial-exec"))
    kind = TLS_MODEL_INITIAL_EXEC;
  else if (!strcmp (TREE_STRING_POINTER (id), "local-dynamic"))
    kind = optimize ? TLS_MODEL_LOCAL_DYNAMIC : TLS_MODEL_GLOBAL_DYNAMIC;
  else if (!strcmp (TREE_STRING_POINTER (id), "global-dynamic"))
    kind = TLS_MODEL_GLOBAL_DYNAMIC;
  else
    error ("tls_model argument must be one of \"local-exec\", \"initial-exec\", \"local-dynamic\" or \"global-dynamic\"");

  DECL_TLS_MODEL (decl) = kind;
  return NULL_TREE;
}

/* Handle a "no_instrument_function" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_no_instrument_function_attribute (tree *node, tree name,
					 tree ARG_UNUSED (args),
					 int ARG_UNUSED (flags),
					 bool *no_add_attrs)
{
  tree decl = *node;

  if (TREE_CODE (decl) != FUNCTION_DECL)
    {
      error ("%J%qE attribute applies only to functions", decl, name);
      *no_add_attrs = true;
    }
  else if (DECL_INITIAL (decl))
    {
      error ("%Jcan%'t set %qE attribute after definition", decl, name);
      *no_add_attrs = true;
    }
  else
    DECL_NO_INSTRUMENT_FUNCTION_ENTRY_EXIT (decl) = 1;

  return NULL_TREE;
}

/* Handle a "malloc" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_malloc_attribute (tree *node, tree name, tree ARG_UNUSED (args),
			 int ARG_UNUSED (flags), bool *no_add_attrs)
{
  if (TREE_CODE (*node) == FUNCTION_DECL
      && POINTER_TYPE_P (TREE_TYPE (TREE_TYPE (*node))))
    DECL_IS_MALLOC (*node) = 1;
  else
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "returns_twice" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_returns_twice_attribute (tree *node, tree name, tree ARG_UNUSED (args),
			 int ARG_UNUSED (flags), bool *no_add_attrs)
{
  if (TREE_CODE (*node) == FUNCTION_DECL)
    DECL_IS_RETURNS_TWICE (*node) = 1;
  else
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "no_limit_stack" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_no_limit_stack_attribute (tree *node, tree name,
				 tree ARG_UNUSED (args),
				 int ARG_UNUSED (flags),
				 bool *no_add_attrs)
{
  tree decl = *node;

  if (TREE_CODE (decl) != FUNCTION_DECL)
    {
      error ("%J%qE attribute applies only to functions", decl, name);
      *no_add_attrs = true;
    }
  else if (DECL_INITIAL (decl))
    {
      error ("%Jcan%'t set %qE attribute after definition", decl, name);
      *no_add_attrs = true;
    }
  else
    DECL_NO_LIMIT_STACK (decl) = 1;

  return NULL_TREE;
}

/* Handle a "pure" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_pure_attribute (tree *node, tree name, tree ARG_UNUSED (args),
		       int ARG_UNUSED (flags), bool *no_add_attrs)
{
  if (TREE_CODE (*node) == FUNCTION_DECL)
    DECL_IS_PURE (*node) = 1;
  /* ??? TODO: Support types.  */
  else
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "no vops" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_novops_attribute (tree *node, tree ARG_UNUSED (name),
			 tree ARG_UNUSED (args), int ARG_UNUSED (flags),
			 bool *ARG_UNUSED (no_add_attrs))
{
  gcc_assert (TREE_CODE (*node) == FUNCTION_DECL);
  DECL_IS_NOVOPS (*node) = 1;
  return NULL_TREE;
}

/* Handle a "deprecated" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_deprecated_attribute (tree *node, tree name,
			     tree ARG_UNUSED (args), int flags,
			     bool *no_add_attrs)
{
  tree type = NULL_TREE;
  int warn = 0;
  tree what = NULL_TREE;

  if (DECL_P (*node))
    {
      tree decl = *node;
      type = TREE_TYPE (decl);

      if (TREE_CODE (decl) == TYPE_DECL
	  || TREE_CODE (decl) == PARM_DECL
	  || TREE_CODE (decl) == VAR_DECL
	  || TREE_CODE (decl) == FUNCTION_DECL
	  || TREE_CODE (decl) == FIELD_DECL)
	TREE_DEPRECATED (decl) = 1;
      else
	warn = 1;
    }
  else if (TYPE_P (*node))
    {
      if (!(flags & (int) ATTR_FLAG_TYPE_IN_PLACE))
	*node = build_variant_type_copy (*node);
      TREE_DEPRECATED (*node) = 1;
      type = *node;
    }
  else
    warn = 1;

  if (warn)
    {
      *no_add_attrs = true;
      if (type && TYPE_NAME (type))
	{
	  if (TREE_CODE (TYPE_NAME (type)) == IDENTIFIER_NODE)
	    what = TYPE_NAME (*node);
	  else if (TREE_CODE (TYPE_NAME (type)) == TYPE_DECL
		   && DECL_NAME (TYPE_NAME (type)))
	    what = DECL_NAME (TYPE_NAME (type));
	}
      if (what)
	warning (OPT_Wattributes, "%qE attribute ignored for %qE", name, what);
      else
	warning (OPT_Wattributes, "%qE attribute ignored", name);
    }

  return NULL_TREE;
}

/* Handle a "vector_size" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_vector_size_attribute (tree *node, tree name, tree args,
			      int ARG_UNUSED (flags),
			      bool *no_add_attrs)
{
  unsigned HOST_WIDE_INT vecsize, nunits;
  enum machine_mode orig_mode;
  tree type = *node, new_type, size;

  *no_add_attrs = true;

  size = TREE_VALUE (args);

  if (!host_integerp (size, 1))
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      return NULL_TREE;
    }

  /* Get the vector size (in bytes).  */
  vecsize = tree_low_cst (size, 1);

  /* We need to provide for vector pointers, vector arrays, and
     functions returning vectors.  For example:

       __attribute__((vector_size(16))) short *foo;

     In this case, the mode is SI, but the type being modified is
     HI, so we need to look further.  */

  while (POINTER_TYPE_P (type)
	 || TREE_CODE (type) == FUNCTION_TYPE
	 || TREE_CODE (type) == METHOD_TYPE
	 || TREE_CODE (type) == ARRAY_TYPE)
    type = TREE_TYPE (type);

  /* Get the mode of the type being modified.  */
  orig_mode = TYPE_MODE (type);

  if (TREE_CODE (type) == RECORD_TYPE
      || TREE_CODE (type) == UNION_TYPE
      || TREE_CODE (type) == VECTOR_TYPE
      || (!SCALAR_FLOAT_MODE_P (orig_mode)
	  && GET_MODE_CLASS (orig_mode) != MODE_INT)
      || !host_integerp (TYPE_SIZE_UNIT (type), 1))
    {
      error ("invalid vector type for attribute %qE", name);
      return NULL_TREE;
    }

  if (vecsize % tree_low_cst (TYPE_SIZE_UNIT (type), 1))
    {
      error ("vector size not an integral multiple of component size");
      return NULL;
    }

  if (vecsize == 0)
    {
      error ("zero vector size");
      return NULL;
    }

  /* Calculate how many units fit in the vector.  */
  nunits = vecsize / tree_low_cst (TYPE_SIZE_UNIT (type), 1);
  if (nunits & (nunits - 1))
    {
      error ("number of components of the vector not a power of two");
      return NULL_TREE;
    }

  new_type = build_vector_type (type, nunits);

  /* Build back pointers if needed.  */
  *node = reconstruct_complex_type (*node, new_type);

  return NULL_TREE;
}

/* Handle the "nonnull" attribute.  */
static tree
handle_nonnull_attribute (tree *node, tree ARG_UNUSED (name),
			  tree args, int ARG_UNUSED (flags),
			  bool *no_add_attrs)
{
  tree type = *node;
  unsigned HOST_WIDE_INT attr_arg_num;

  /* If no arguments are specified, all pointer arguments should be
     non-null.  Verify a full prototype is given so that the arguments
     will have the correct types when we actually check them later.  */
  if (!args)
    {
      if (!TYPE_ARG_TYPES (type))
	{
	  error ("nonnull attribute without arguments on a non-prototype");
	  *no_add_attrs = true;
	}
      return NULL_TREE;
    }

  /* Argument list specified.  Verify that each argument number references
     a pointer argument.  */
  for (attr_arg_num = 1; args; args = TREE_CHAIN (args))
    {
      tree argument;
      unsigned HOST_WIDE_INT arg_num = 0, ck_num;

      if (!get_nonnull_operand (TREE_VALUE (args), &arg_num))
	{
	  error ("nonnull argument has invalid operand number (argument %lu)",
		 (unsigned long) attr_arg_num);
	  *no_add_attrs = true;
	  return NULL_TREE;
	}

      argument = TYPE_ARG_TYPES (type);
      if (argument)
	{
	  for (ck_num = 1; ; ck_num++)
	    {
	      if (!argument || ck_num == arg_num)
		break;
	      argument = TREE_CHAIN (argument);
	    }

	  if (!argument
	      || TREE_CODE (TREE_VALUE (argument)) == VOID_TYPE)
	    {
	      error ("nonnull argument with out-of-range operand number (argument %lu, operand %lu)",
		     (unsigned long) attr_arg_num, (unsigned long) arg_num);
	      *no_add_attrs = true;
	      return NULL_TREE;
	    }

	  if (TREE_CODE (TREE_VALUE (argument)) != POINTER_TYPE)
	    {
	      error ("nonnull argument references non-pointer operand (argument %lu, operand %lu)",
		   (unsigned long) attr_arg_num, (unsigned long) arg_num);
	      *no_add_attrs = true;
	      return NULL_TREE;
	    }
	}
    }

  return NULL_TREE;
}

/* Check the argument list of a function call for null in argument slots
   that are marked as requiring a non-null pointer argument.  */

static void
check_function_nonnull (tree attrs, tree params)
{
  tree a, args, param;
  int param_num;

  for (a = attrs; a; a = TREE_CHAIN (a))
    {
      if (is_attribute_p ("nonnull", TREE_PURPOSE (a)))
	{
	  args = TREE_VALUE (a);

	  /* Walk the argument list.  If we encounter an argument number we
	     should check for non-null, do it.  If the attribute has no args,
	     then every pointer argument is checked (in which case the check
	     for pointer type is done in check_nonnull_arg).  */
	  for (param = params, param_num = 1; ;
	       param_num++, param = TREE_CHAIN (param))
	    {
	      if (!param)
	break;
	      if (!args || nonnull_check_p (args, param_num))
	check_function_arguments_recurse (check_nonnull_arg, NULL,
					  TREE_VALUE (param),
					  param_num);
	    }
	}
    }
}

/* Check that the Nth argument of a function call (counting backwards
   from the end) is a (pointer)0.  */

static void
check_function_sentinel (tree attrs, tree params, tree typelist)
{
  tree attr = lookup_attribute ("sentinel", attrs);

  if (attr)
    {
      /* Skip over the named arguments.  */
      while (typelist && params)
      {
	typelist = TREE_CHAIN (typelist);
	params = TREE_CHAIN (params);
      }

      if (typelist || !params)
	warning (OPT_Wformat,
		 "not enough variable arguments to fit a sentinel");
      else
	{
	  tree sentinel, end;
	  unsigned pos = 0;

	  if (TREE_VALUE (attr))
	    {
	      tree p = TREE_VALUE (TREE_VALUE (attr));
	      pos = TREE_INT_CST_LOW (p);
	    }

	  sentinel = end = params;

	  /* Advance `end' ahead of `sentinel' by `pos' positions.  */
	  while (pos > 0 && TREE_CHAIN (end))
	    {
	      pos--;
	      end = TREE_CHAIN (end);
	    }
	  if (pos > 0)
	    {
	      warning (OPT_Wformat,
		       "not enough variable arguments to fit a sentinel");
	      return;
	    }

	  /* Now advance both until we find the last parameter.  */
	  while (TREE_CHAIN (end))
	    {
	      end = TREE_CHAIN (end);
	      sentinel = TREE_CHAIN (sentinel);
	    }

	  /* Validate the sentinel.  */
	  if ((!POINTER_TYPE_P (TREE_TYPE (TREE_VALUE (sentinel)))
	       || !integer_zerop (TREE_VALUE (sentinel)))
	      /* Although __null (in C++) is only an integer we allow it
		 nevertheless, as we are guaranteed that it's exactly
		 as wide as a pointer, and we don't want to force
		 users to cast the NULL they have written there.
		 We warn with -Wstrict-null-sentinel, though.  */
	      && (warn_strict_null_sentinel
		  || null_node != TREE_VALUE (sentinel)))
	    warning (OPT_Wformat, "missing sentinel in function call");
	}
    }
}

/* Helper for check_function_nonnull; given a list of operands which
   must be non-null in ARGS, determine if operand PARAM_NUM should be
   checked.  */

static bool
nonnull_check_p (tree args, unsigned HOST_WIDE_INT param_num)
{
  unsigned HOST_WIDE_INT arg_num = 0;

  for (; args; args = TREE_CHAIN (args))
    {
      bool found = get_nonnull_operand (TREE_VALUE (args), &arg_num);

      gcc_assert (found);

      if (arg_num == param_num)
	return true;
    }
  return false;
}

/* Check that the function argument PARAM (which is operand number
   PARAM_NUM) is non-null.  This is called by check_function_nonnull
   via check_function_arguments_recurse.  */

static void
check_nonnull_arg (void * ARG_UNUSED (ctx), tree param,
		   unsigned HOST_WIDE_INT param_num)
{
  /* Just skip checking the argument if it's not a pointer.  This can
     happen if the "nonnull" attribute was given without an operand
     list (which means to check every pointer argument).  */

  if (TREE_CODE (TREE_TYPE (param)) != POINTER_TYPE)
    return;

  if (integer_zerop (param))
    warning (OPT_Wnonnull, "null argument where non-null required "
	     "(argument %lu)", (unsigned long) param_num);
}

/* Helper for nonnull attribute handling; fetch the operand number
   from the attribute argument list.  */

static bool
get_nonnull_operand (tree arg_num_expr, unsigned HOST_WIDE_INT *valp)
{
  /* Verify the arg number is a constant.  */
  if (TREE_CODE (arg_num_expr) != INTEGER_CST
      || TREE_INT_CST_HIGH (arg_num_expr) != 0)
    return false;

  *valp = TREE_INT_CST_LOW (arg_num_expr);
  return true;
}

/* Handle a "nothrow" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_nothrow_attribute (tree *node, tree name, tree ARG_UNUSED (args),
			  int ARG_UNUSED (flags), bool *no_add_attrs)
{
  if (TREE_CODE (*node) == FUNCTION_DECL)
    TREE_NOTHROW (*node) = 1;
  /* ??? TODO: Support types.  */
  else
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "cleanup" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
handle_cleanup_attribute (tree *node, tree name, tree args,
			  int ARG_UNUSED (flags), bool *no_add_attrs)
{
  tree decl = *node;
  tree cleanup_id, cleanup_decl;

  /* ??? Could perhaps support cleanups on TREE_STATIC, much like we do
     for global destructors in C++.  This requires infrastructure that
     we don't have generically at the moment.  It's also not a feature
     we'd be missing too much, since we do have attribute constructor.  */
  if (TREE_CODE (decl) != VAR_DECL || TREE_STATIC (decl))
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
      return NULL_TREE;
    }

  /* Verify that the argument is a function in scope.  */
  /* ??? We could support pointers to functions here as well, if
     that was considered desirable.  */
  cleanup_id = TREE_VALUE (args);
  if (TREE_CODE (cleanup_id) != IDENTIFIER_NODE)
    {
      error ("cleanup argument not an identifier");
      *no_add_attrs = true;
      return NULL_TREE;
    }
  cleanup_decl = lookup_name (cleanup_id);
  if (!cleanup_decl || TREE_CODE (cleanup_decl) != FUNCTION_DECL)
    {
      error ("cleanup argument not a function");
      *no_add_attrs = true;
      return NULL_TREE;
    }

  /* That the function has proper type is checked with the
     eventual call to build_function_call.  */

  return NULL_TREE;
}

/* Handle a "warn_unused_result" attribute.  No special handling.  */

static tree
handle_warn_unused_result_attribute (tree *node, tree name,
			       tree ARG_UNUSED (args),
			       int ARG_UNUSED (flags), bool *no_add_attrs)
{
  /* Ignore the attribute for functions not returning any value.  */
  if (VOID_TYPE_P (TREE_TYPE (*node)))
    {
      warning (OPT_Wattributes, "%qE attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "sentinel" attribute.  */

static tree
handle_sentinel_attribute (tree *node, tree name, tree args,
			   int ARG_UNUSED (flags), bool *no_add_attrs)
{
  tree params = TYPE_ARG_TYPES (*node);

  if (!params)
    {
      warning (OPT_Wattributes,
	       "%qE attribute requires prototypes with named arguments", name);
      *no_add_attrs = true;
    }
  else
    {
      while (TREE_CHAIN (params))
	params = TREE_CHAIN (params);

      if (VOID_TYPE_P (TREE_VALUE (params)))
	{
	  warning (OPT_Wattributes,
		   "%qE attribute only applies to variadic functions", name);
	  *no_add_attrs = true;
	}
    }

  if (args)
    {
      tree position = TREE_VALUE (args);

      if (TREE_CODE (position) != INTEGER_CST)
	{
	  warning (0, "requested position is not an integer constant");
	  *no_add_attrs = true;
	}
      else
	{
	  if (tree_int_cst_lt (position, integer_zero_node))
	    {
	      warning (0, "requested position is less than zero");
	      *no_add_attrs = true;
	    }
	}
    }

  return NULL_TREE;
}

/* Check for valid arguments being passed to a function.  */
void
check_function_arguments (tree attrs, tree params, tree typelist)
{
  /* Check for null being passed in a pointer argument that must be
     non-null.  We also need to do this if format checking is enabled.  */

  if (warn_nonnull)
    check_function_nonnull (attrs, params);

  /* Check for errors in format strings.  */

  if (warn_format || warn_missing_format_attribute)
      check_function_format (attrs, params);

  if (warn_format)
    check_function_sentinel (attrs, params, typelist);

  if (warn_bounded)
    check_function_bounded (NULL, attrs, params);
}

/* Generic argument checking recursion routine.  PARAM is the argument to
   be checked.  PARAM_NUM is the number of the argument.  CALLBACK is invoked
   once the argument is resolved.  CTX is context for the callback.  */
void
check_function_arguments_recurse (void (*callback)
				  (void *, tree, unsigned HOST_WIDE_INT),
				  void *ctx, tree param,
				  unsigned HOST_WIDE_INT param_num)
{
  if ((TREE_CODE (param) == NOP_EXPR || TREE_CODE (param) == CONVERT_EXPR)
      && (TYPE_PRECISION (TREE_TYPE (param))
	  == TYPE_PRECISION (TREE_TYPE (TREE_OPERAND (param, 0)))))
    {
      /* Strip coercion.  */
      check_function_arguments_recurse (callback, ctx,
					TREE_OPERAND (param, 0), param_num);
      return;
    }

  if (TREE_CODE (param) == CALL_EXPR)
    {
      tree type = TREE_TYPE (TREE_TYPE (TREE_OPERAND (param, 0)));
      tree attrs;
      bool found_format_arg = false;

      /* See if this is a call to a known internationalization function
	 that modifies a format arg.  Such a function may have multiple
	 format_arg attributes (for example, ngettext).  */

      for (attrs = TYPE_ATTRIBUTES (type);
	   attrs;
	   attrs = TREE_CHAIN (attrs))
	if (is_attribute_p ("format_arg", TREE_PURPOSE (attrs)))
	  {
	    tree inner_args;
	    tree format_num_expr;
	    int format_num;
	    int i;

	    /* Extract the argument number, which was previously checked
	       to be valid.  */
	    format_num_expr = TREE_VALUE (TREE_VALUE (attrs));

	    gcc_assert (TREE_CODE (format_num_expr) == INTEGER_CST
			&& !TREE_INT_CST_HIGH (format_num_expr));

	    format_num = TREE_INT_CST_LOW (format_num_expr);

	    for (inner_args = TREE_OPERAND (param, 1), i = 1;
		 inner_args != 0;
		 inner_args = TREE_CHAIN (inner_args), i++)
	      if (i == format_num)
		{
		  check_function_arguments_recurse (callback, ctx,
						    TREE_VALUE (inner_args),
						    param_num);
		  found_format_arg = true;
		  break;
		}
	  }

      /* If we found a format_arg attribute and did a recursive check,
	 we are done with checking this argument.  Otherwise, we continue
	 and this will be considered a non-literal.  */
      if (found_format_arg)
	return;
    }

  if (TREE_CODE (param) == COND_EXPR)
    {
      /* Check both halves of the conditional expression.  */
      check_function_arguments_recurse (callback, ctx,
					TREE_OPERAND (param, 1), param_num);
      check_function_arguments_recurse (callback, ctx,
					TREE_OPERAND (param, 2), param_num);
      return;
    }

  (*callback) (ctx, param, param_num);
}

/* Function to help qsort sort FIELD_DECLs by name order.  */

int
field_decl_cmp (const void *x_p, const void *y_p)
{
  const tree *const x = (const tree *const) x_p;
  const tree *const y = (const tree *const) y_p;

  if (DECL_NAME (*x) == DECL_NAME (*y))
    /* A nontype is "greater" than a type.  */
    return (TREE_CODE (*y) == TYPE_DECL) - (TREE_CODE (*x) == TYPE_DECL);
  if (DECL_NAME (*x) == NULL_TREE)
    return -1;
  if (DECL_NAME (*y) == NULL_TREE)
    return 1;
  if (DECL_NAME (*x) < DECL_NAME (*y))
    return -1;
  return 1;
}

static struct {
  gt_pointer_operator new_value;
  void *cookie;
} resort_data;

/* This routine compares two fields like field_decl_cmp but using the
pointer operator in resort_data.  */

static int
resort_field_decl_cmp (const void *x_p, const void *y_p)
{
  const tree *const x = (const tree *const) x_p;
  const tree *const y = (const tree *const) y_p;

  if (DECL_NAME (*x) == DECL_NAME (*y))
    /* A nontype is "greater" than a type.  */
    return (TREE_CODE (*y) == TYPE_DECL) - (TREE_CODE (*x) == TYPE_DECL);
  if (DECL_NAME (*x) == NULL_TREE)
    return -1;
  if (DECL_NAME (*y) == NULL_TREE)
    return 1;
  {
    tree d1 = DECL_NAME (*x);
    tree d2 = DECL_NAME (*y);
    resort_data.new_value (&d1, resort_data.cookie);
    resort_data.new_value (&d2, resort_data.cookie);
    if (d1 < d2)
      return -1;
  }
  return 1;
}

/* Resort DECL_SORTED_FIELDS because pointers have been reordered.  */

void
resort_sorted_fields (void *obj,
		      void * ARG_UNUSED (orig_obj),
		      gt_pointer_operator new_value,
		      void *cookie)
{
  struct sorted_fields_type *sf = (struct sorted_fields_type *) obj;
  resort_data.new_value = new_value;
  resort_data.cookie = cookie;
  qsort (&sf->elts[0], sf->len, sizeof (tree),
	 resort_field_decl_cmp);
}

/* Subroutine of c_parse_error.
   Return the result of concatenating LHS and RHS. RHS is really
   a string literal, its first character is indicated by RHS_START and
   RHS_SIZE is its length (including the terminating NUL character).

   The caller is responsible for deleting the returned pointer.  */

static char *
catenate_strings (const char *lhs, const char *rhs_start, int rhs_size)
{
  const int lhs_size = strlen (lhs);
  char *result = XNEWVEC (char, lhs_size + rhs_size);
  strncpy (result, lhs, lhs_size);
  strncpy (result + lhs_size, rhs_start, rhs_size);
  return result;
}

/* Issue the error given by GMSGID, indicating that it occurred before
   TOKEN, which had the associated VALUE.  */

void
c_parse_error (const char *gmsgid, enum cpp_ttype token, tree value)
{
#define catenate_messages(M1, M2) catenate_strings ((M1), (M2), sizeof (M2))

  char *message = NULL;

  if (token == CPP_EOF)
    message = catenate_messages (gmsgid, " at end of input");
  else if (token == CPP_CHAR || token == CPP_WCHAR)
    {
      unsigned int val = TREE_INT_CST_LOW (value);
      const char *const ell = (token == CPP_CHAR) ? "" : "L";
      if (val <= UCHAR_MAX && ISGRAPH (val))
	message = catenate_messages (gmsgid, " before %s'%c'");
      else
	message = catenate_messages (gmsgid, " before %s'\\x%x'");

      error (message, ell, val);
      free (message);
      message = NULL;
    }
  else if (token == CPP_STRING || token == CPP_WSTRING)
    message = catenate_messages (gmsgid, " before string constant");
  else if (token == CPP_NUMBER)
    message = catenate_messages (gmsgid, " before numeric constant");
  else if (token == CPP_NAME)
    {
      message = catenate_messages (gmsgid, " before %qE");
      error (message, value);
      free (message);
      message = NULL;
    }
  else if (token == CPP_PRAGMA)
    message = catenate_messages (gmsgid, " before %<#pragma%>");
  else if (token == CPP_PRAGMA_EOL)
    message = catenate_messages (gmsgid, " before end of line");
  else if (token < N_TTYPES)
    {
      message = catenate_messages (gmsgid, " before %qs token");
      error (message, cpp_type2name (token));
      free (message);
      message = NULL;
    }
  else
    error (gmsgid);

  if (message)
    {
      error (message);
      free (message);
    }
#undef catenate_messages
}

/* Walk a gimplified function and warn for functions whose return value is
   ignored and attribute((warn_unused_result)) is set.  This is done before
   inlining, so we don't have to worry about that.  */

void
c_warn_unused_result (tree *top_p)
{
  tree t = *top_p;
  tree_stmt_iterator i;
  tree fdecl, ftype;

  switch (TREE_CODE (t))
    {
    case STATEMENT_LIST:
      for (i = tsi_start (*top_p); !tsi_end_p (i); tsi_next (&i))
	c_warn_unused_result (tsi_stmt_ptr (i));
      break;

    case COND_EXPR:
      c_warn_unused_result (&COND_EXPR_THEN (t));
      c_warn_unused_result (&COND_EXPR_ELSE (t));
      break;
    case BIND_EXPR:
      c_warn_unused_result (&BIND_EXPR_BODY (t));
      break;
    case TRY_FINALLY_EXPR:
    case TRY_CATCH_EXPR:
      c_warn_unused_result (&TREE_OPERAND (t, 0));
      c_warn_unused_result (&TREE_OPERAND (t, 1));
      break;
    case CATCH_EXPR:
      c_warn_unused_result (&CATCH_BODY (t));
      break;
    case EH_FILTER_EXPR:
      c_warn_unused_result (&EH_FILTER_FAILURE (t));
      break;

    case CALL_EXPR:
      if (TREE_USED (t))
	break;

      /* This is a naked call, as opposed to a CALL_EXPR nested inside
	 a MODIFY_EXPR.  All calls whose value is ignored should be
	 represented like this.  Look for the attribute.  */
      fdecl = get_callee_fndecl (t);
      if (fdecl)
	ftype = TREE_TYPE (fdecl);
      else
	{
	  ftype = TREE_TYPE (TREE_OPERAND (t, 0));
	  /* Look past pointer-to-function to the function type itself.  */
	  ftype = TREE_TYPE (ftype);
	}

      if (lookup_attribute ("warn_unused_result", TYPE_ATTRIBUTES (ftype)))
	{
	  if (fdecl)
	    warning (0, "%Hignoring return value of %qD, "
		     "declared with attribute warn_unused_result",
		     EXPR_LOCUS (t), fdecl);
	  else
	    warning (0, "%Hignoring return value of function "
		     "declared with attribute warn_unused_result",
		     EXPR_LOCUS (t));
	}
      break;

    default:
      /* Not a container, not a call, or a call whose value is used.  */
      break;
    }
}

/* Convert a character from the host to the target execution character
   set.  cpplib handles this, mostly.  */

HOST_WIDE_INT
c_common_to_target_charset (HOST_WIDE_INT c)
{
  /* Character constants in GCC proper are sign-extended under -fsigned-char,
     zero-extended under -fno-signed-char.  cpplib insists that characters
     and character constants are always unsigned.  Hence we must convert
     back and forth.  */
  cppchar_t uc = ((cppchar_t)c) & ((((cppchar_t)1) << CHAR_BIT)-1);

  uc = cpp_host_to_exec_charset (parse_in, uc);

  if (flag_signed_char)
    return ((HOST_WIDE_INT)uc) << (HOST_BITS_PER_WIDE_INT - CHAR_TYPE_SIZE)
			       >> (HOST_BITS_PER_WIDE_INT - CHAR_TYPE_SIZE);
  else
    return uc;
}

/* Build the result of __builtin_offsetof.  EXPR is a nested sequence of
   component references, with STOP_REF, or alternatively an INDIRECT_REF of
   NULL, at the bottom; much like the traditional rendering of offsetof as a
   macro.  Returns the folded and properly cast result.  */

static tree
fold_offsetof_1 (tree expr, tree stop_ref)
{
  enum tree_code code = PLUS_EXPR;
  tree base, off, t;

  if (expr == stop_ref && TREE_CODE (expr) != ERROR_MARK)
    return size_zero_node;

  switch (TREE_CODE (expr))
    {
    case ERROR_MARK:
      return expr;

    case VAR_DECL:
      error ("cannot apply %<offsetof%> to static data member %qD", expr);
      return error_mark_node;

    case CALL_EXPR:
      error ("cannot apply %<offsetof%> when %<operator[]%> is overloaded");
      return error_mark_node;

    case INTEGER_CST:
      gcc_assert (integer_zerop (expr));
      return size_zero_node;

    case NOP_EXPR:
    case INDIRECT_REF:
      base = fold_offsetof_1 (TREE_OPERAND (expr, 0), stop_ref);
      gcc_assert (base == error_mark_node || base == size_zero_node);
      return base;

    case COMPONENT_REF:
      base = fold_offsetof_1 (TREE_OPERAND (expr, 0), stop_ref);
      if (base == error_mark_node)
	return base;

      t = TREE_OPERAND (expr, 1);
      if (DECL_C_BIT_FIELD (t))
	{
	  error ("attempt to take address of bit-field structure "
		 "member %qD", t);
	  return error_mark_node;
	}
      off = size_binop (PLUS_EXPR, DECL_FIELD_OFFSET (t),
			size_int (tree_low_cst (DECL_FIELD_BIT_OFFSET (t), 1)
				  / BITS_PER_UNIT));
      break;

    case ARRAY_REF:
      base = fold_offsetof_1 (TREE_OPERAND (expr, 0), stop_ref);
      if (base == error_mark_node)
	return base;

      t = TREE_OPERAND (expr, 1);
      if (TREE_CODE (t) == INTEGER_CST && tree_int_cst_sgn (t) < 0)
	{
	  code = MINUS_EXPR;
	  t = fold_build1 (NEGATE_EXPR, TREE_TYPE (t), t);
	}
      t = convert (sizetype, t);
      off = size_binop (MULT_EXPR, TYPE_SIZE_UNIT (TREE_TYPE (expr)), t);
      break;

    case COMPOUND_EXPR:
      /* Handle static members of volatile structs.  */
      t = TREE_OPERAND (expr, 1);
      gcc_assert (TREE_CODE (t) == VAR_DECL);
      return fold_offsetof_1 (t, stop_ref);

    default:
      gcc_unreachable ();
    }

  return size_binop (code, base, off);
}

tree
fold_offsetof (tree expr, tree stop_ref)
{
  /* Convert back from the internal sizetype to size_t.  */
  return convert (size_type_node, fold_offsetof_1 (expr, stop_ref));
}

/* Print an error message for an invalid lvalue.  USE says
   how the lvalue is being used and so selects the error message.  */

void
lvalue_error (enum lvalue_use use)
{
  switch (use)
    {
    case lv_assign:
      error ("lvalue required as left operand of assignment");
      break;
    case lv_increment:
      error ("lvalue required as increment operand");
      break;
    case lv_decrement:
      error ("lvalue required as decrement operand");
      break;
    case lv_addressof:
      error ("lvalue required as unary %<&%> operand");
      break;
    case lv_asm:
      error ("lvalue required in asm statement");
      break;
    default:
      gcc_unreachable ();
    }
}

/* *PTYPE is an incomplete array.  Complete it with a domain based on
   INITIAL_VALUE.  If INITIAL_VALUE is not present, use 1 if DO_DEFAULT
   is true.  Return 0 if successful, 1 if INITIAL_VALUE can't be deciphered,
   2 if INITIAL_VALUE was NULL, and 3 if INITIAL_VALUE was empty.  */

int
complete_array_type (tree *ptype, tree initial_value, bool do_default)
{
  tree maxindex, type, main_type, elt, unqual_elt;
  int failure = 0, quals;

  maxindex = size_zero_node;
  if (initial_value)
    {
      if (TREE_CODE (initial_value) == STRING_CST)
	{
	  int eltsize
	    = int_size_in_bytes (TREE_TYPE (TREE_TYPE (initial_value)));
	  maxindex = size_int (TREE_STRING_LENGTH (initial_value)/eltsize - 1);
	}
      else if (TREE_CODE (initial_value) == CONSTRUCTOR)
	{
	  VEC(constructor_elt,gc) *v = CONSTRUCTOR_ELTS (initial_value);

	  if (VEC_empty (constructor_elt, v))
	    {
	      if (pedantic)
		failure = 3;
	      maxindex = integer_minus_one_node;
	    }
	  else
	    {
	      tree curindex;
	      unsigned HOST_WIDE_INT cnt;
	      constructor_elt *ce;

	      if (VEC_index (constructor_elt, v, 0)->index)
		maxindex = fold_convert (sizetype,
					 VEC_index (constructor_elt,
						    v, 0)->index);
	      curindex = maxindex;

	      for (cnt = 1;
		   VEC_iterate (constructor_elt, v, cnt, ce);
		   cnt++)
		{
		  if (ce->index)
		    curindex = fold_convert (sizetype, ce->index);
		  else
		    curindex = size_binop (PLUS_EXPR, curindex, size_one_node);

		  if (tree_int_cst_lt (maxindex, curindex))
		    maxindex = curindex;
		}
	    }
	}
      else
	{
	  /* Make an error message unless that happened already.  */
	  if (initial_value != error_mark_node)
	    failure = 1;
	}
    }
  else
    {
      failure = 2;
      if (!do_default)
	return failure;
    }

  type = *ptype;
  elt = TREE_TYPE (type);
  quals = TYPE_QUALS (strip_array_types (elt));
  if (quals == 0)
    unqual_elt = elt;
  else
    unqual_elt = c_build_qualified_type (elt, TYPE_UNQUALIFIED);

  /* Using build_distinct_type_copy and modifying things afterward instead
     of using build_array_type to create a new type preserves all of the
     TYPE_LANG_FLAG_? bits that the front end may have set.  */
  main_type = build_distinct_type_copy (TYPE_MAIN_VARIANT (type));
  TREE_TYPE (main_type) = unqual_elt;
  TYPE_DOMAIN (main_type) = build_index_type (maxindex);
  layout_type (main_type);

  if (quals == 0)
    type = main_type;
  else
    type = c_build_qualified_type (main_type, quals);

  *ptype = type;
  return failure;
}


/* Used to help initialize the builtin-types.def table.  When a type of
   the correct size doesn't exist, use error_mark_node instead of NULL.
   The later results in segfaults even when a decl using the type doesn't
   get invoked.  */

tree
builtin_type_for_size (int size, bool unsignedp)
{
  tree type = lang_hooks.types.type_for_size (size, unsignedp);
  return type ? type : error_mark_node;
}

/* A helper function for resolve_overloaded_builtin in resolving the
   overloaded __sync_ builtins.  Returns a positive power of 2 if the
   first operand of PARAMS is a pointer to a supported data type.
   Returns 0 if an error is encountered.  */

static int
sync_resolve_size (tree function, tree params)
{
  tree type;
  int size;

  if (params == NULL)
    {
      error ("too few arguments to function %qE", function);
      return 0;
    }

  type = TREE_TYPE (TREE_VALUE (params));
  if (TREE_CODE (type) != POINTER_TYPE)
    goto incompatible;

  type = TREE_TYPE (type);
  if (!INTEGRAL_TYPE_P (type) && !POINTER_TYPE_P (type))
    goto incompatible;

  size = tree_low_cst (TYPE_SIZE_UNIT (type), 1);
  if (size == 1 || size == 2 || size == 4 || size == 8 || size == 16)
    return size;

 incompatible:
  error ("incompatible type for argument %d of %qE", 1, function);
  return 0;
}

/* A helper function for resolve_overloaded_builtin.  Adds casts to
   PARAMS to make arguments match up with those of FUNCTION.  Drops
   the variadic arguments at the end.  Returns false if some error
   was encountered; true on success.  */

static bool
sync_resolve_params (tree orig_function, tree function, tree params)
{
  tree arg_types = TYPE_ARG_TYPES (TREE_TYPE (function));
  tree ptype;
  int number;

  /* We've declared the implementation functions to use "volatile void *"
     as the pointer parameter, so we shouldn't get any complaints from the
     call to check_function_arguments what ever type the user used.  */
  arg_types = TREE_CHAIN (arg_types);
  ptype = TREE_TYPE (TREE_TYPE (TREE_VALUE (params)));
  number = 2;

  /* For the rest of the values, we need to cast these to FTYPE, so that we
     don't get warnings for passing pointer types, etc.  */
  while (arg_types != void_list_node)
    {
      tree val;

      params = TREE_CHAIN (params);
      if (params == NULL)
	{
	  error ("too few arguments to function %qE", orig_function);
	  return false;
	}

      /* ??? Ideally for the first conversion we'd use convert_for_assignment
	 so that we get warnings for anything that doesn't match the pointer
	 type.  This isn't portable across the C and C++ front ends atm.  */
      val = TREE_VALUE (params);
      val = convert (ptype, val);
      val = convert (TREE_VALUE (arg_types), val);
      TREE_VALUE (params) = val;

      arg_types = TREE_CHAIN (arg_types);
      number++;
    }

  /* The definition of these primitives is variadic, with the remaining
     being "an optional list of variables protected by the memory barrier".
     No clue what that's supposed to mean, precisely, but we consider all
     call-clobbered variables to be protected so we're safe.  */
  TREE_CHAIN (params) = NULL;

  return true;
}

/* A helper function for resolve_overloaded_builtin.  Adds a cast to
   RESULT to make it match the type of the first pointer argument in
   PARAMS.  */

static tree
sync_resolve_return (tree params, tree result)
{
  tree ptype = TREE_TYPE (TREE_TYPE (TREE_VALUE (params)));
  ptype = TYPE_MAIN_VARIANT (ptype);
  return convert (ptype, result);
}

/* Some builtin functions are placeholders for other expressions.  This
   function should be called immediately after parsing the call expression
   before surrounding code has committed to the type of the expression.

   FUNCTION is the DECL that has been invoked; it is known to be a builtin.
   PARAMS is the argument list for the call.  The return value is non-null
   when expansion is complete, and null if normal processing should
   continue.  */

tree
resolve_overloaded_builtin (tree function, tree params)
{
  enum built_in_function orig_code = DECL_FUNCTION_CODE (function);
  switch (DECL_BUILT_IN_CLASS (function))
    {
    case BUILT_IN_NORMAL:
      break;
    case BUILT_IN_MD:
      if (targetm.resolve_overloaded_builtin)
	return targetm.resolve_overloaded_builtin (function, params);
      else
	return NULL_TREE;
    default:
      return NULL_TREE;
    }

  /* Handle BUILT_IN_NORMAL here.  */
  switch (orig_code)
    {
    case BUILT_IN_FETCH_AND_ADD_N:
    case BUILT_IN_FETCH_AND_SUB_N:
    case BUILT_IN_FETCH_AND_OR_N:
    case BUILT_IN_FETCH_AND_AND_N:
    case BUILT_IN_FETCH_AND_XOR_N:
    case BUILT_IN_FETCH_AND_NAND_N:
    case BUILT_IN_ADD_AND_FETCH_N:
    case BUILT_IN_SUB_AND_FETCH_N:
    case BUILT_IN_OR_AND_FETCH_N:
    case BUILT_IN_AND_AND_FETCH_N:
    case BUILT_IN_XOR_AND_FETCH_N:
    case BUILT_IN_NAND_AND_FETCH_N:
    case BUILT_IN_BOOL_COMPARE_AND_SWAP_N:
    case BUILT_IN_VAL_COMPARE_AND_SWAP_N:
    case BUILT_IN_LOCK_TEST_AND_SET_N:
    case BUILT_IN_LOCK_RELEASE_N:
      {
	int n = sync_resolve_size (function, params);
	tree new_function, result;

	if (n == 0)
	  return error_mark_node;

	new_function = built_in_decls[orig_code + exact_log2 (n) + 1];
	if (!sync_resolve_params (function, new_function, params))
	  return error_mark_node;

	result = build_function_call (new_function, params);
	if (orig_code != BUILT_IN_BOOL_COMPARE_AND_SWAP_N
	    && orig_code != BUILT_IN_LOCK_RELEASE_N)
	  result = sync_resolve_return (params, result);

	return result;
      }

    default:
      return NULL_TREE;
    }
}

/* Ignoring their sign, return true if two scalar types are the same.  */
bool
same_scalar_type_ignoring_signedness (tree t1, tree t2)
{
  enum tree_code c1 = TREE_CODE (t1), c2 = TREE_CODE (t2);

  gcc_assert ((c1 == INTEGER_TYPE || c1 == REAL_TYPE)
	      && (c2 == INTEGER_TYPE || c2 == REAL_TYPE));

  /* Equality works here because c_common_signed_type uses
     TYPE_MAIN_VARIANT.  */
  return lang_hooks.types.signed_type (t1)
    == lang_hooks.types.signed_type (t2);
}

/* Check for missing format attributes on function pointers.  LTYPE is
   the new type or left-hand side type.  RTYPE is the old type or
   right-hand side type.  Returns TRUE if LTYPE is missing the desired
   attribute.  */

bool
check_missing_format_attribute (tree ltype, tree rtype)
{
  tree const ttr = TREE_TYPE (rtype), ttl = TREE_TYPE (ltype);
  tree ra;

  for (ra = TYPE_ATTRIBUTES (ttr); ra; ra = TREE_CHAIN (ra))
    if (is_attribute_p ("format", TREE_PURPOSE (ra)))
      break;
  if (ra)
    {
      tree la;
      for (la = TYPE_ATTRIBUTES (ttl); la; la = TREE_CHAIN (la))
	if (is_attribute_p ("format", TREE_PURPOSE (la)))
	  break;
      return !la;
    }
  else
    return false;
}

/* Subscripting with type char is likely to lose on a machine where
   chars are signed.  So warn on any machine, but optionally.  Don't
   warn for unsigned char since that type is safe.  Don't warn for
   signed char because anyone who uses that must have done so
   deliberately. Furthermore, we reduce the false positive load by
   warning only for non-constant value of type char.  */

void
warn_array_subscript_with_type_char (tree index)
{
  if (TYPE_MAIN_VARIANT (TREE_TYPE (index)) == char_type_node
      && TREE_CODE (index) != INTEGER_CST)
    warning (OPT_Wchar_subscripts, "array subscript has type %<char%>");
}


#include "gt-c-common.h"
