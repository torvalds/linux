/* Separate lexical analyzer for GNU C++.
   Copyright (C) 1987, 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Hacked by Michael Tiemann (tiemann@cygnus.com)

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


/* This file is the lexical analyzer for GNU C++.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "input.h"
#include "tree.h"
#include "cp-tree.h"
#include "cpplib.h"
#include "flags.h"
#include "c-pragma.h"
#include "toplev.h"
#include "output.h"
#include "tm_p.h"
#include "timevar.h"

static int interface_strcmp (const char *);
static void init_cp_pragma (void);

static tree parse_strconst_pragma (const char *, int);
static void handle_pragma_vtable (cpp_reader *);
static void handle_pragma_unit (cpp_reader *);
static void handle_pragma_interface (cpp_reader *);
static void handle_pragma_implementation (cpp_reader *);
static void handle_pragma_java_exceptions (cpp_reader *);

static void init_operators (void);
static void copy_lang_type (tree);

/* A constraint that can be tested at compile time.  */
#define CONSTRAINT(name, expr) extern int constraint_##name [(expr) ? 1 : -1]

/* Functions and data structures for #pragma interface.

   `#pragma implementation' means that the main file being compiled
   is considered to implement (provide) the classes that appear in
   its main body.  I.e., if this is file "foo.cc", and class `bar'
   is defined in "foo.cc", then we say that "foo.cc implements bar".

   All main input files "implement" themselves automagically.

   `#pragma interface' means that unless this file (of the form "foo.h"
   is not presently being included by file "foo.cc", the
   CLASSTYPE_INTERFACE_ONLY bit gets set.  The effect is that none
   of the vtables nor any of the inline functions defined in foo.h
   will ever be output.

   There are cases when we want to link files such as "defs.h" and
   "main.cc".  In this case, we give "defs.h" a `#pragma interface',
   and "main.cc" has `#pragma implementation "defs.h"'.  */

struct impl_files
{
  const char *filename;
  struct impl_files *next;
};

static struct impl_files *impl_file_chain;


void
cxx_finish (void)
{
  c_common_finish ();
}

/* A mapping from tree codes to operator name information.  */
operator_name_info_t operator_name_info[(int) LAST_CPLUS_TREE_CODE];
/* Similar, but for assignment operators.  */
operator_name_info_t assignment_operator_name_info[(int) LAST_CPLUS_TREE_CODE];

/* Initialize data structures that keep track of operator names.  */

#define DEF_OPERATOR(NAME, C, M, AR, AP) \
 CONSTRAINT (C, sizeof "operator " + sizeof NAME <= 256);
#include "operators.def"
#undef DEF_OPERATOR

static void
init_operators (void)
{
  tree identifier;
  char buffer[256];
  struct operator_name_info_t *oni;

#define DEF_OPERATOR(NAME, CODE, MANGLING, ARITY, ASSN_P)		    \
  sprintf (buffer, ISALPHA (NAME[0]) ? "operator %s" : "operator%s", NAME); \
  identifier = get_identifier (buffer);					    \
  IDENTIFIER_OPNAME_P (identifier) = 1;					    \
									    \
  oni = (ASSN_P								    \
	 ? &assignment_operator_name_info[(int) CODE]			    \
	 : &operator_name_info[(int) CODE]);				    \
  oni->identifier = identifier;						    \
  oni->name = NAME;							    \
  oni->mangled_name = MANGLING;						    \
  oni->arity = ARITY;

#include "operators.def"
#undef DEF_OPERATOR

  operator_name_info[(int) ERROR_MARK].identifier
    = get_identifier ("<invalid operator>");

  /* Handle some special cases.  These operators are not defined in
     the language, but can be produced internally.  We may need them
     for error-reporting.  (Eventually, we should ensure that this
     does not happen.  Error messages involving these operators will
     be confusing to users.)  */

  operator_name_info [(int) INIT_EXPR].name
    = operator_name_info [(int) MODIFY_EXPR].name;
  operator_name_info [(int) EXACT_DIV_EXPR].name = "(ceiling /)";
  operator_name_info [(int) CEIL_DIV_EXPR].name = "(ceiling /)";
  operator_name_info [(int) FLOOR_DIV_EXPR].name = "(floor /)";
  operator_name_info [(int) ROUND_DIV_EXPR].name = "(round /)";
  operator_name_info [(int) CEIL_MOD_EXPR].name = "(ceiling %)";
  operator_name_info [(int) FLOOR_MOD_EXPR].name = "(floor %)";
  operator_name_info [(int) ROUND_MOD_EXPR].name = "(round %)";
  operator_name_info [(int) ABS_EXPR].name = "abs";
  operator_name_info [(int) TRUTH_AND_EXPR].name = "strict &&";
  operator_name_info [(int) TRUTH_OR_EXPR].name = "strict ||";
  operator_name_info [(int) RANGE_EXPR].name = "...";
  operator_name_info [(int) UNARY_PLUS_EXPR].name = "+";

  assignment_operator_name_info [(int) EXACT_DIV_EXPR].name
    = "(exact /=)";
  assignment_operator_name_info [(int) CEIL_DIV_EXPR].name
    = "(ceiling /=)";
  assignment_operator_name_info [(int) FLOOR_DIV_EXPR].name
    = "(floor /=)";
  assignment_operator_name_info [(int) ROUND_DIV_EXPR].name
    = "(round /=)";
  assignment_operator_name_info [(int) CEIL_MOD_EXPR].name
    = "(ceiling %=)";
  assignment_operator_name_info [(int) FLOOR_MOD_EXPR].name
    = "(floor %=)";
  assignment_operator_name_info [(int) ROUND_MOD_EXPR].name
    = "(round %=)";
}

/* The reserved keyword table.  */
struct resword
{
  const char *const word;
  ENUM_BITFIELD(rid) const rid : 16;
  const unsigned int disable   : 16;
};

/* Disable mask.  Keywords are disabled if (reswords[i].disable & mask) is
   _true_.  */
#define D_EXT		0x01	/* GCC extension */
#define D_ASM		0x02	/* in C99, but has a switch to turn it off */
#define D_OBJC		0x04	/* Objective C++ only */

CONSTRAINT(ridbits_fit, RID_LAST_MODIFIER < sizeof(unsigned long) * CHAR_BIT);

static const struct resword reswords[] =
{
  { "_Complex",		RID_COMPLEX,	0 },
  { "__FUNCTION__",	RID_FUNCTION_NAME, 0 },
  { "__PRETTY_FUNCTION__", RID_PRETTY_FUNCTION_NAME, 0 },
  { "__alignof",	RID_ALIGNOF,	0 },
  { "__alignof__",	RID_ALIGNOF,	0 },
  { "__asm",		RID_ASM,	0 },
  { "__asm__",		RID_ASM,	0 },
  { "__attribute",	RID_ATTRIBUTE,	0 },
  { "__attribute__",	RID_ATTRIBUTE,	0 },
  { "__builtin_offsetof", RID_OFFSETOF, 0 },
  { "__builtin_va_arg",	RID_VA_ARG,	0 },
  { "__complex",	RID_COMPLEX,	0 },
  { "__complex__",	RID_COMPLEX,	0 },
  { "__const",		RID_CONST,	0 },
  { "__const__",	RID_CONST,	0 },
  { "__extension__",	RID_EXTENSION,	0 },
  { "__func__",		RID_C99_FUNCTION_NAME,	0 },
  { "__imag",		RID_IMAGPART,	0 },
  { "__imag__",		RID_IMAGPART,	0 },
  { "__inline",		RID_INLINE,	0 },
  { "__inline__",	RID_INLINE,	0 },
  { "__label__",	RID_LABEL,	0 },
  { "__null",		RID_NULL,	0 },
  { "__real",		RID_REALPART,	0 },
  { "__real__",		RID_REALPART,	0 },
  { "__restrict",	RID_RESTRICT,	0 },
  { "__restrict__",	RID_RESTRICT,	0 },
  { "__signed",		RID_SIGNED,	0 },
  { "__signed__",	RID_SIGNED,	0 },
  { "__thread",		RID_THREAD,	0 },
  { "__typeof",		RID_TYPEOF,	0 },
  { "__typeof__",	RID_TYPEOF,	0 },
  { "__volatile",	RID_VOLATILE,	0 },
  { "__volatile__",	RID_VOLATILE,	0 },
  { "asm",		RID_ASM,	D_ASM },
  { "auto",		RID_AUTO,	0 },
  { "bool",		RID_BOOL,	0 },
  { "break",		RID_BREAK,	0 },
  { "case",		RID_CASE,	0 },
  { "catch",		RID_CATCH,	0 },
  { "char",		RID_CHAR,	0 },
  { "class",		RID_CLASS,	0 },
  { "const",		RID_CONST,	0 },
  { "const_cast",	RID_CONSTCAST,	0 },
  { "continue",		RID_CONTINUE,	0 },
  { "default",		RID_DEFAULT,	0 },
  { "delete",		RID_DELETE,	0 },
  { "do",		RID_DO,		0 },
  { "double",		RID_DOUBLE,	0 },
  { "dynamic_cast",	RID_DYNCAST,	0 },
  { "else",		RID_ELSE,	0 },
  { "enum",		RID_ENUM,	0 },
  { "explicit",		RID_EXPLICIT,	0 },
  { "export",		RID_EXPORT,	0 },
  { "extern",		RID_EXTERN,	0 },
  { "false",		RID_FALSE,	0 },
  { "float",		RID_FLOAT,	0 },
  { "for",		RID_FOR,	0 },
  { "friend",		RID_FRIEND,	0 },
  { "goto",		RID_GOTO,	0 },
  { "if",		RID_IF,		0 },
  { "inline",		RID_INLINE,	0 },
  { "int",		RID_INT,	0 },
  { "long",		RID_LONG,	0 },
  { "mutable",		RID_MUTABLE,	0 },
  { "namespace",	RID_NAMESPACE,	0 },
  { "new",		RID_NEW,	0 },
  { "operator",		RID_OPERATOR,	0 },
  { "private",		RID_PRIVATE,	0 },
  { "protected",	RID_PROTECTED,	0 },
  { "public",		RID_PUBLIC,	0 },
  { "register",		RID_REGISTER,	0 },
  { "reinterpret_cast",	RID_REINTCAST,	0 },
  { "return",		RID_RETURN,	0 },
  { "short",		RID_SHORT,	0 },
  { "signed",		RID_SIGNED,	0 },
  { "sizeof",		RID_SIZEOF,	0 },
  { "static",		RID_STATIC,	0 },
  { "static_cast",	RID_STATCAST,	0 },
  { "struct",		RID_STRUCT,	0 },
  { "switch",		RID_SWITCH,	0 },
  { "template",		RID_TEMPLATE,	0 },
  { "this",		RID_THIS,	0 },
  { "throw",		RID_THROW,	0 },
  { "true",		RID_TRUE,	0 },
  { "try",		RID_TRY,	0 },
  { "typedef",		RID_TYPEDEF,	0 },
  { "typename",		RID_TYPENAME,	0 },
  { "typeid",		RID_TYPEID,	0 },
  { "typeof",		RID_TYPEOF,	D_ASM|D_EXT },
  { "union",		RID_UNION,	0 },
  { "unsigned",		RID_UNSIGNED,	0 },
  { "using",		RID_USING,	0 },
  { "virtual",		RID_VIRTUAL,	0 },
  { "void",		RID_VOID,	0 },
  { "volatile",		RID_VOLATILE,	0 },
  { "wchar_t",		RID_WCHAR,	0 },
  { "while",		RID_WHILE,	0 },

  /* The remaining keywords are specific to Objective-C++.  NB:
     All of them will remain _disabled_, since they are context-
     sensitive.  */

  /* These ObjC keywords are recognized only immediately after
     an '@'.  NB: The following C++ keywords double as
     ObjC keywords in this context: RID_CLASS, RID_PRIVATE,
     RID_PROTECTED, RID_PUBLIC, RID_THROW, RID_TRY and RID_CATCH.  */
  { "compatibility_alias", RID_AT_ALIAS,	D_OBJC },
  { "defs",		RID_AT_DEFS,		D_OBJC },
  { "encode",		RID_AT_ENCODE,		D_OBJC },
  { "end",		RID_AT_END,		D_OBJC },
  { "implementation",	RID_AT_IMPLEMENTATION,	D_OBJC },
  { "interface",	RID_AT_INTERFACE,	D_OBJC },
  { "protocol",		RID_AT_PROTOCOL,	D_OBJC },
  { "selector",		RID_AT_SELECTOR,	D_OBJC },
  { "finally",		RID_AT_FINALLY,		D_OBJC },
  { "synchronized",	RID_AT_SYNCHRONIZED,	D_OBJC },
  /* These are recognized only in protocol-qualifier context.  */
  { "bycopy",		RID_BYCOPY,		D_OBJC },
  { "byref",		RID_BYREF,		D_OBJC },
  { "in",		RID_IN,			D_OBJC },
  { "inout",		RID_INOUT,		D_OBJC },
  { "oneway",		RID_ONEWAY,		D_OBJC },
  { "out",		RID_OUT,		D_OBJC },
};

void
init_reswords (void)
{
  unsigned int i;
  tree id;
  int mask = ((flag_no_asm ? D_ASM : 0)
	      | D_OBJC
	      | (flag_no_gnu_keywords ? D_EXT : 0));

  ridpointers = GGC_CNEWVEC (tree, (int) RID_MAX);
  for (i = 0; i < ARRAY_SIZE (reswords); i++)
    {
      id = get_identifier (reswords[i].word);
      C_RID_CODE (id) = reswords[i].rid;
      ridpointers [(int) reswords[i].rid] = id;
      if (! (reswords[i].disable & mask))
	C_IS_RESERVED_WORD (id) = 1;
    }
}

static void
init_cp_pragma (void)
{
  c_register_pragma (0, "vtable", handle_pragma_vtable);
  c_register_pragma (0, "unit", handle_pragma_unit);
  c_register_pragma (0, "interface", handle_pragma_interface);
  c_register_pragma (0, "implementation", handle_pragma_implementation);
  c_register_pragma ("GCC", "interface", handle_pragma_interface);
  c_register_pragma ("GCC", "implementation", handle_pragma_implementation);
  c_register_pragma ("GCC", "java_exceptions", handle_pragma_java_exceptions);
}

/* TRUE if a code represents a statement.  */

bool statement_code_p[MAX_TREE_CODES];

/* Initialize the C++ front end.  This function is very sensitive to
   the exact order that things are done here.  It would be nice if the
   initialization done by this routine were moved to its subroutines,
   and the ordering dependencies clarified and reduced.  */
bool
cxx_init (void)
{
  unsigned int i;
  static const enum tree_code stmt_codes[] = {
   CTOR_INITIALIZER,	TRY_BLOCK,	HANDLER,
   EH_SPEC_BLOCK,	USING_STMT,	TAG_DEFN,
   IF_STMT,		CLEANUP_STMT,	FOR_STMT,
   WHILE_STMT,		DO_STMT,	BREAK_STMT,
   CONTINUE_STMT,	SWITCH_STMT,	EXPR_STMT
  };

  memset (&statement_code_p, 0, sizeof (statement_code_p));
  for (i = 0; i < ARRAY_SIZE (stmt_codes); i++)
    statement_code_p[stmt_codes[i]] = true;

  /* We cannot just assign to input_filename because it has already
     been initialized and will be used later as an N_BINCL for stabs+
     debugging.  */
#ifdef USE_MAPPED_LOCATION
  push_srcloc (BUILTINS_LOCATION);
#else
  push_srcloc ("<built-in>", 0);
#endif

  init_reswords ();
  init_tree ();
  init_cp_semantics ();
  init_operators ();
  init_method ();
  init_error ();

  current_function_decl = NULL;

  class_type_node = ridpointers[(int) RID_CLASS];

  cxx_init_decl_processing ();

  /* The fact that G++ uses COMDAT for many entities (inline
     functions, template instantiations, virtual tables, etc.) mean
     that it is fundamentally unreliable to try to make decisions
     about whether or not to output a particular entity until the end
     of the compilation.  However, the inliner requires that functions
     be provided to the back end if they are to be inlined.
     Therefore, we always use unit-at-a-time mode; in that mode, we
     can provide entities to the back end and it will decide what to
     emit based on what is actually needed.  */
  flag_unit_at_a_time = 1;

  if (c_common_init () == false)
    {
      pop_srcloc();
      return false;
    }

  init_cp_pragma ();

  init_repo ();

  pop_srcloc();
  return true;
}

/* Return nonzero if S is not considered part of an
   INTERFACE/IMPLEMENTATION pair.  Otherwise, return 0.  */

static int
interface_strcmp (const char* s)
{
  /* Set the interface/implementation bits for this scope.  */
  struct impl_files *ifiles;
  const char *s1;

  for (ifiles = impl_file_chain; ifiles; ifiles = ifiles->next)
    {
      const char *t1 = ifiles->filename;
      s1 = s;

      if (*s1 != *t1 || *s1 == 0)
	continue;

      while (*s1 == *t1 && *s1 != 0)
	s1++, t1++;

      /* A match.  */
      if (*s1 == *t1)
	return 0;

      /* Don't get faked out by xxx.yyy.cc vs xxx.zzz.cc.  */
      if (strchr (s1, '.') || strchr (t1, '.'))
	continue;

      if (*s1 == '\0' || s1[-1] != '.' || t1[-1] != '.')
	continue;

      /* A match.  */
      return 0;
    }

  /* No matches.  */
  return 1;
}



/* Parse a #pragma whose sole argument is a string constant.
   If OPT is true, the argument is optional.  */
static tree
parse_strconst_pragma (const char* name, int opt)
{
  tree result, x;
  enum cpp_ttype t;

  t = pragma_lex (&result);
  if (t == CPP_STRING)
    {
      if (pragma_lex (&x) != CPP_EOF)
	warning (0, "junk at end of #pragma %s", name);
      return result;
    }

  if (t == CPP_EOF && opt)
    return NULL_TREE;

  error ("invalid #pragma %s", name);
  return error_mark_node;
}

static void
handle_pragma_vtable (cpp_reader* dfile ATTRIBUTE_UNUSED )
{
  parse_strconst_pragma ("vtable", 0);
  sorry ("#pragma vtable no longer supported");
}

static void
handle_pragma_unit (cpp_reader* dfile ATTRIBUTE_UNUSED )
{
  /* Validate syntax, but don't do anything.  */
  parse_strconst_pragma ("unit", 0);
}

static void
handle_pragma_interface (cpp_reader* dfile ATTRIBUTE_UNUSED )
{
  tree fname = parse_strconst_pragma ("interface", 1);
  struct c_fileinfo *finfo;
  const char *filename;

  if (fname == error_mark_node)
    return;
  else if (fname == 0)
    filename = lbasename (input_filename);
  else
    filename = ggc_strdup (TREE_STRING_POINTER (fname));

  finfo = get_fileinfo (input_filename);

  if (impl_file_chain == 0)
    {
      /* If this is zero at this point, then we are
	 auto-implementing.  */
      if (main_input_filename == 0)
	main_input_filename = input_filename;
    }

  finfo->interface_only = interface_strcmp (filename);
  /* If MULTIPLE_SYMBOL_SPACES is set, we cannot assume that we can see
     a definition in another file.  */
  if (!MULTIPLE_SYMBOL_SPACES || !finfo->interface_only)
    finfo->interface_unknown = 0;
}

/* Note that we have seen a #pragma implementation for the key MAIN_FILENAME.
   We used to only allow this at toplevel, but that restriction was buggy
   in older compilers and it seems reasonable to allow it in the headers
   themselves, too.  It only needs to precede the matching #p interface.

   We don't touch finfo->interface_only or finfo->interface_unknown;
   the user must specify a matching #p interface for this to have
   any effect.  */

static void
handle_pragma_implementation (cpp_reader* dfile ATTRIBUTE_UNUSED )
{
  tree fname = parse_strconst_pragma ("implementation", 1);
  const char *filename;
  struct impl_files *ifiles = impl_file_chain;

  if (fname == error_mark_node)
    return;

  if (fname == 0)
    {
      if (main_input_filename)
	filename = main_input_filename;
      else
	filename = input_filename;
      filename = lbasename (filename);
    }
  else
    {
      filename = ggc_strdup (TREE_STRING_POINTER (fname));
#if 0
      /* We currently cannot give this diagnostic, as we reach this point
	 only after cpplib has scanned the entire translation unit, so
	 cpp_included always returns true.  A plausible fix is to compare
	 the current source-location cookie with the first source-location
	 cookie (if any) of the filename, but this requires completing the
	 --enable-mapped-location project first.  See PR 17577.  */
      if (cpp_included (parse_in, filename))
	warning (0, "#pragma implementation for %qs appears after "
		 "file is included", filename);
#endif
    }

  for (; ifiles; ifiles = ifiles->next)
    {
      if (! strcmp (ifiles->filename, filename))
	break;
    }
  if (ifiles == 0)
    {
      ifiles = XNEW (struct impl_files);
      ifiles->filename = filename;
      ifiles->next = impl_file_chain;
      impl_file_chain = ifiles;
    }
}

/* Indicate that this file uses Java-personality exception handling.  */
static void
handle_pragma_java_exceptions (cpp_reader* dfile ATTRIBUTE_UNUSED)
{
  tree x;
  if (pragma_lex (&x) != CPP_EOF)
    warning (0, "junk at end of #pragma GCC java_exceptions");

  choose_personality_routine (lang_java);
}

/* Issue an error message indicating that the lookup of NAME (an
   IDENTIFIER_NODE) failed.  Returns the ERROR_MARK_NODE.  */

tree
unqualified_name_lookup_error (tree name)
{
  if (IDENTIFIER_OPNAME_P (name))
    {
      if (name != ansi_opname (ERROR_MARK))
	error ("%qD not defined", name);
    }
  else
    {
      error ("%qD was not declared in this scope", name);
      /* Prevent repeated error messages by creating a VAR_DECL with
	 this NAME in the innermost block scope.  */
      if (current_function_decl)
	{
	  tree decl;
	  decl = build_decl (VAR_DECL, name, error_mark_node);
	  DECL_CONTEXT (decl) = current_function_decl;
	  push_local_binding (name, decl, 0);
	  /* Mark the variable as used so that we do not get warnings
	     about it being unused later.  */
	  TREE_USED (decl) = 1;
	}
    }

  return error_mark_node;
}

/* Like unqualified_name_lookup_error, but NAME is an unqualified-id
   used as a function.  Returns an appropriate expression for
   NAME.  */

tree
unqualified_fn_lookup_error (tree name)
{
  if (processing_template_decl)
    {
      /* In a template, it is invalid to write "f()" or "f(3)" if no
	 declaration of "f" is available.  Historically, G++ and most
	 other compilers accepted that usage since they deferred all name
	 lookup until instantiation time rather than doing unqualified
	 name lookup at template definition time; explain to the user what
	 is going wrong.

	 Note that we have the exact wording of the following message in
	 the manual (trouble.texi, node "Name lookup"), so they need to
	 be kept in synch.  */
      pedwarn ("there are no arguments to %qD that depend on a template "
	       "parameter, so a declaration of %qD must be available",
	       name, name);

      if (!flag_permissive)
	{
	  static bool hint;
	  if (!hint)
	    {
	      error ("(if you use %<-fpermissive%>, G++ will accept your "
		     "code, but allowing the use of an undeclared name is "
		     "deprecated)");
	      hint = true;
	    }
	}
      return name;
    }

  return unqualified_name_lookup_error (name);
}

tree
build_lang_decl (enum tree_code code, tree name, tree type)
{
  tree t;

  t = build_decl (code, name, type);
  retrofit_lang_decl (t);

  /* All nesting of C++ functions is lexical; there is never a "static
     chain" in the sense of GNU C nested functions.  */
  if (code == FUNCTION_DECL)
    DECL_NO_STATIC_CHAIN (t) = 1;

  return t;
}

/* Add DECL_LANG_SPECIFIC info to T.  Called from build_lang_decl
   and pushdecl (for functions generated by the backend).  */

void
retrofit_lang_decl (tree t)
{
  struct lang_decl *ld;
  size_t size;

  if (CAN_HAVE_FULL_LANG_DECL_P (t))
    size = sizeof (struct lang_decl);
  else
    size = sizeof (struct lang_decl_flags);

  ld = GGC_CNEWVAR (struct lang_decl, size);

  ld->decl_flags.can_be_full = CAN_HAVE_FULL_LANG_DECL_P (t) ? 1 : 0;
  ld->decl_flags.u1sel = TREE_CODE (t) == NAMESPACE_DECL ? 1 : 0;
  ld->decl_flags.u2sel = 0;
  if (ld->decl_flags.can_be_full)
    ld->u.f.u3sel = TREE_CODE (t) == FUNCTION_DECL ? 1 : 0;

  DECL_LANG_SPECIFIC (t) = ld;
  if (current_lang_name == lang_name_cplusplus
      || decl_linkage (t) == lk_none)
    SET_DECL_LANGUAGE (t, lang_cplusplus);
  else if (current_lang_name == lang_name_c)
    SET_DECL_LANGUAGE (t, lang_c);
  else if (current_lang_name == lang_name_java)
    SET_DECL_LANGUAGE (t, lang_java);
  else
    gcc_unreachable ();

#ifdef GATHER_STATISTICS
  tree_node_counts[(int)lang_decl] += 1;
  tree_node_sizes[(int)lang_decl] += size;
#endif
}

void
cxx_dup_lang_specific_decl (tree node)
{
  int size;
  struct lang_decl *ld;

  if (! DECL_LANG_SPECIFIC (node))
    return;

  if (!CAN_HAVE_FULL_LANG_DECL_P (node))
    size = sizeof (struct lang_decl_flags);
  else
    size = sizeof (struct lang_decl);
  ld = GGC_NEWVAR (struct lang_decl, size);
  memcpy (ld, DECL_LANG_SPECIFIC (node), size);
  DECL_LANG_SPECIFIC (node) = ld;

#ifdef GATHER_STATISTICS
  tree_node_counts[(int)lang_decl] += 1;
  tree_node_sizes[(int)lang_decl] += size;
#endif
}

/* Copy DECL, including any language-specific parts.  */

tree
copy_decl (tree decl)
{
  tree copy;

  copy = copy_node (decl);
  cxx_dup_lang_specific_decl (copy);
  return copy;
}

/* Replace the shared language-specific parts of NODE with a new copy.  */

static void
copy_lang_type (tree node)
{
  int size;
  struct lang_type *lt;

  if (! TYPE_LANG_SPECIFIC (node))
    return;

  if (TYPE_LANG_SPECIFIC (node)->u.h.is_lang_type_class)
    size = sizeof (struct lang_type);
  else
    size = sizeof (struct lang_type_ptrmem);
  lt = GGC_NEWVAR (struct lang_type, size);
  memcpy (lt, TYPE_LANG_SPECIFIC (node), size);
  TYPE_LANG_SPECIFIC (node) = lt;

#ifdef GATHER_STATISTICS
  tree_node_counts[(int)lang_type] += 1;
  tree_node_sizes[(int)lang_type] += size;
#endif
}

/* Copy TYPE, including any language-specific parts.  */

tree
copy_type (tree type)
{
  tree copy;

  copy = copy_node (type);
  copy_lang_type (copy);
  return copy;
}

tree
cxx_make_type (enum tree_code code)
{
  tree t = make_node (code);

  /* Create lang_type structure.  */
  if (IS_AGGR_TYPE_CODE (code)
      || code == BOUND_TEMPLATE_TEMPLATE_PARM)
    {
      struct lang_type *pi = GGC_CNEW (struct lang_type);

      TYPE_LANG_SPECIFIC (t) = pi;
      pi->u.c.h.is_lang_type_class = 1;

#ifdef GATHER_STATISTICS
      tree_node_counts[(int)lang_type] += 1;
      tree_node_sizes[(int)lang_type] += sizeof (struct lang_type);
#endif
    }

  /* Set up some flags that give proper default behavior.  */
  if (IS_AGGR_TYPE_CODE (code))
    {
      struct c_fileinfo *finfo = get_fileinfo (input_filename);
      SET_CLASSTYPE_INTERFACE_UNKNOWN_X (t, finfo->interface_unknown);
      CLASSTYPE_INTERFACE_ONLY (t) = finfo->interface_only;
    }

  return t;
}

tree
make_aggr_type (enum tree_code code)
{
  tree t = cxx_make_type (code);

  if (IS_AGGR_TYPE_CODE (code))
    SET_IS_AGGR_TYPE (t, 1);

  return t;
}
