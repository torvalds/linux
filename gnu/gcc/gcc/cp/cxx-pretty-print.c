/* Implementation of subroutines for the GNU C++ pretty-printer.
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Gabriel Dos Reis <gdr@integrable-solutions.net>

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
#include "real.h"
#include "cxx-pretty-print.h"
#include "cp-tree.h"
#include "toplev.h"

static void pp_cxx_unqualified_id (cxx_pretty_printer *, tree);
static void pp_cxx_nested_name_specifier (cxx_pretty_printer *, tree);
static void pp_cxx_qualified_id (cxx_pretty_printer *, tree);
static void pp_cxx_assignment_expression (cxx_pretty_printer *, tree);
static void pp_cxx_expression (cxx_pretty_printer *, tree);
static void pp_cxx_template_argument_list (cxx_pretty_printer *, tree);
static void pp_cxx_type_specifier_seq (cxx_pretty_printer *, tree);
static void pp_cxx_ptr_operator (cxx_pretty_printer *, tree);
static void pp_cxx_type_id (cxx_pretty_printer *, tree);
static void pp_cxx_direct_abstract_declarator (cxx_pretty_printer *, tree);
static void pp_cxx_declarator (cxx_pretty_printer *, tree);
static void pp_cxx_parameter_declaration_clause (cxx_pretty_printer *, tree);
static void pp_cxx_abstract_declarator (cxx_pretty_printer *, tree);
static void pp_cxx_statement (cxx_pretty_printer *, tree);
static void pp_cxx_template_parameter (cxx_pretty_printer *, tree);
static void pp_cxx_cast_expression (cxx_pretty_printer *, tree);


static inline void
pp_cxx_nonconsecutive_character (cxx_pretty_printer *pp, int c)
{
  const char *p = pp_last_position_in_text (pp);

  if (p != NULL && *p == c)
    pp_cxx_whitespace (pp);
  pp_character (pp, c);
  pp_base (pp)->padding = pp_none;
}

#define pp_cxx_storage_class_specifier(PP, T) \
   pp_c_storage_class_specifier (pp_c_base (PP), T)
#define pp_cxx_expression_list(PP, T)    \
   pp_c_expression_list (pp_c_base (PP), T)
#define pp_cxx_space_for_pointer_operator(PP, T)  \
   pp_c_space_for_pointer_operator (pp_c_base (PP), T)
#define pp_cxx_init_declarator(PP, T)    \
   pp_c_init_declarator (pp_c_base (PP), T)
#define pp_cxx_call_argument_list(PP, T) \
   pp_c_call_argument_list (pp_c_base (PP), T)

void
pp_cxx_colon_colon (cxx_pretty_printer *pp)
{
  pp_colon_colon (pp);
  pp_base (pp)->padding = pp_none;
}

void
pp_cxx_begin_template_argument_list (cxx_pretty_printer *pp)
{
  pp_cxx_nonconsecutive_character (pp, '<');
}

void
pp_cxx_end_template_argument_list (cxx_pretty_printer *pp)
{
  pp_cxx_nonconsecutive_character (pp, '>');
}

void
pp_cxx_separate_with (cxx_pretty_printer *pp, int c)
{
  pp_separate_with (pp, c);
  pp_base (pp)->padding = pp_none;
}

/* Expressions.  */

static inline bool
is_destructor_name (tree name)
{
  return name == complete_dtor_identifier
    || name == base_dtor_identifier
    || name == deleting_dtor_identifier;
}

/* conversion-function-id:
      operator conversion-type-id

   conversion-type-id:
      type-specifier-seq conversion-declarator(opt)

   conversion-declarator:
      ptr-operator conversion-declarator(opt)  */

static inline void
pp_cxx_conversion_function_id (cxx_pretty_printer *pp, tree t)
{
  pp_cxx_identifier (pp, "operator");
  pp_cxx_type_specifier_seq (pp, TREE_TYPE (t));
}

static inline void
pp_cxx_template_id (cxx_pretty_printer *pp, tree t)
{
  pp_cxx_unqualified_id (pp, TREE_OPERAND (t, 0));
  pp_cxx_begin_template_argument_list (pp);
  pp_cxx_template_argument_list (pp, TREE_OPERAND (t, 1));
  pp_cxx_end_template_argument_list (pp);
}

/* unqualified-id:
     identifier
     operator-function-id
     conversion-function-id
     ~ class-name
     template-id  */

static void
pp_cxx_unqualified_id (cxx_pretty_printer *pp, tree t)
{
  enum tree_code code = TREE_CODE (t);
  switch (code)
    {
    case RESULT_DECL:
      pp_cxx_identifier (pp, "<return-value>");
      break;

    case OVERLOAD:
      t = OVL_CURRENT (t);
    case VAR_DECL:
    case PARM_DECL:
    case CONST_DECL:
    case TYPE_DECL:
    case FUNCTION_DECL:
    case NAMESPACE_DECL:
    case FIELD_DECL:
    case LABEL_DECL:
    case USING_DECL:
    case TEMPLATE_DECL:
      t = DECL_NAME (t);

    case IDENTIFIER_NODE:
      if (t == NULL)
	pp_cxx_identifier (pp, "<unnamed>");
      else if (IDENTIFIER_TYPENAME_P (t))
	pp_cxx_conversion_function_id (pp, t);
      else
	{
	  if (is_destructor_name (t))
	    {
	      pp_complement (pp);
	      /* FIXME: Why is this necessary? */
	      if (TREE_TYPE (t))
		t = constructor_name (TREE_TYPE (t));
	    }
	  pp_cxx_tree_identifier (pp, t);
	}
      break;

    case TEMPLATE_ID_EXPR:
      pp_cxx_template_id (pp, t);
      break;

    case BASELINK:
      pp_cxx_unqualified_id (pp, BASELINK_FUNCTIONS (t));
      break;

    case RECORD_TYPE:
    case UNION_TYPE:
    case ENUMERAL_TYPE:
      pp_cxx_unqualified_id (pp, TYPE_NAME (t));
      break;

    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_TEMPLATE_PARM:
      if (TYPE_IDENTIFIER (t))
	pp_cxx_unqualified_id (pp, TYPE_IDENTIFIER (t));
      else
	pp_cxx_canonical_template_parameter (pp, t);
      break;

    case TEMPLATE_PARM_INDEX:
      pp_cxx_unqualified_id (pp, TEMPLATE_PARM_DECL (t));
      break;

    default:
      pp_unsupported_tree (pp, t);
      break;
    }
}

/* Pretty-print out the token sequence ":: template" in template codes
   where it is needed to "inline declare" the (following) member as
   a template.  This situation arises when SCOPE of T is dependent
   on template parameters.  */

static inline void
pp_cxx_template_keyword_if_needed (cxx_pretty_printer *pp, tree scope, tree t)
{
  if (TREE_CODE (t) == TEMPLATE_ID_EXPR
      && TYPE_P (scope) && dependent_type_p (scope))
    pp_cxx_identifier (pp, "template");
}

/* nested-name-specifier:
      class-or-namespace-name :: nested-name-specifier(opt)
      class-or-namespace-name :: template nested-name-specifier   */

static void
pp_cxx_nested_name_specifier (cxx_pretty_printer *pp, tree t)
{
  if (t != NULL && t != pp->enclosing_scope)
    {
      tree scope = TYPE_P (t) ? TYPE_CONTEXT (t) : DECL_CONTEXT (t);
      pp_cxx_nested_name_specifier (pp, scope);
      pp_cxx_template_keyword_if_needed (pp, scope, t);
      pp_cxx_unqualified_id (pp, t);
      pp_cxx_colon_colon (pp);
    }
}

/* qualified-id:
      nested-name-specifier template(opt) unqualified-id  */

static void
pp_cxx_qualified_id (cxx_pretty_printer *pp, tree t)
{
  switch (TREE_CODE (t))
    {
      /* A pointer-to-member is always qualified.  */
    case PTRMEM_CST:
      pp_cxx_nested_name_specifier (pp, PTRMEM_CST_CLASS (t));
      pp_cxx_unqualified_id (pp, PTRMEM_CST_MEMBER (t));
      break;

      /* In Standard C++, functions cannot possibly be used as
	 nested-name-specifiers.  However, there are situations where
	 is "makes sense" to output the surrounding function name for the
	 purpose of emphasizing on the scope kind.  Just printing the
	 function name might not be sufficient as it may be overloaded; so,
	 we decorate the function with its signature too.
	 FIXME:  This is probably the wrong pretty-printing for conversion
	 functions and some function templates.  */
    case OVERLOAD:
      t = OVL_CURRENT (t);
    case FUNCTION_DECL:
      if (DECL_FUNCTION_MEMBER_P (t))
	pp_cxx_nested_name_specifier (pp, DECL_CONTEXT (t));
      pp_cxx_unqualified_id
	(pp, DECL_CONSTRUCTOR_P (t) ? DECL_CONTEXT (t) : t);
      pp_cxx_parameter_declaration_clause (pp, TREE_TYPE (t));
      break;

    case OFFSET_REF:
    case SCOPE_REF:
      pp_cxx_nested_name_specifier (pp, TREE_OPERAND (t, 0));
      pp_cxx_unqualified_id (pp, TREE_OPERAND (t, 1));
      break;

    default:
      {
	tree scope = TYPE_P (t) ? TYPE_CONTEXT (t) : DECL_CONTEXT (t);
	if (scope != pp->enclosing_scope)
	  {
	    pp_cxx_nested_name_specifier (pp, scope);
	    pp_cxx_template_keyword_if_needed (pp, scope, t);
	  }
	pp_cxx_unqualified_id (pp, t);
      }
      break;
    }
}


static void
pp_cxx_constant (cxx_pretty_printer *pp, tree t)
{
  switch (TREE_CODE (t))
    {
    case STRING_CST:
      {
	const bool in_parens = PAREN_STRING_LITERAL_P (t);
	if (in_parens)
	  pp_cxx_left_paren (pp);
	pp_c_constant (pp_c_base (pp), t);
	if (in_parens)
	  pp_cxx_right_paren (pp);
      }
      break;

    default:
      pp_c_constant (pp_c_base (pp), t);
      break;
    }
}

/* id-expression:
      unqualified-id
      qualified-id   */

static inline void
pp_cxx_id_expression (cxx_pretty_printer *pp, tree t)
{
  if (TREE_CODE (t) == OVERLOAD)
    t = OVL_CURRENT (t);
  if (DECL_P (t) && DECL_CONTEXT (t))
    pp_cxx_qualified_id (pp, t);
  else
    pp_cxx_unqualified_id (pp, t);
}

/* primary-expression:
     literal
     this
     :: identifier
     :: operator-function-id
     :: qualifier-id
     ( expression )
     id-expression   */

static void
pp_cxx_primary_expression (cxx_pretty_printer *pp, tree t)
{
  switch (TREE_CODE (t))
    {
    case INTEGER_CST:
    case REAL_CST:
    case STRING_CST:
      pp_cxx_constant (pp, t);
      break;

    case BASELINK:
      t = BASELINK_FUNCTIONS (t);
    case VAR_DECL:
    case PARM_DECL:
    case FIELD_DECL:
    case FUNCTION_DECL:
    case OVERLOAD:
    case CONST_DECL:
    case TEMPLATE_DECL:
      pp_cxx_id_expression (pp, t);
      break;

    case RESULT_DECL:
    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_TEMPLATE_PARM:
    case TEMPLATE_PARM_INDEX:
      pp_cxx_unqualified_id (pp, t);
      break;

    case STMT_EXPR:
      pp_cxx_left_paren (pp);
      pp_cxx_statement (pp, STMT_EXPR_STMT (t));
      pp_cxx_right_paren (pp);
      break;

    default:
      pp_c_primary_expression (pp_c_base (pp), t);
      break;
    }
}

/* postfix-expression:
     primary-expression
     postfix-expression [ expression ]
     postfix-expression ( expression-list(opt) )
     simple-type-specifier ( expression-list(opt) )
     typename ::(opt) nested-name-specifier identifier ( expression-list(opt) )
     typename ::(opt) nested-name-specifier template(opt)
				       template-id ( expression-list(opt) )
     postfix-expression . template(opt) ::(opt) id-expression
     postfix-expression -> template(opt) ::(opt) id-expression
     postfix-expression . pseudo-destructor-name
     postfix-expression -> pseudo-destructor-name
     postfix-expression ++
     postfix-expression --
     dynamic_cast < type-id > ( expression )
     static_cast < type-id > ( expression )
     reinterpret_cast < type-id > ( expression )
     const_cast < type-id > ( expression )
     typeid ( expression )
     typeif ( type-id )  */

static void
pp_cxx_postfix_expression (cxx_pretty_printer *pp, tree t)
{
  enum tree_code code = TREE_CODE (t);

  switch (code)
    {
    case AGGR_INIT_EXPR:
    case CALL_EXPR:
      {
	tree fun = TREE_OPERAND (t, 0);
	tree args = TREE_OPERAND (t, 1);
	tree saved_scope = pp->enclosing_scope;

	if (TREE_CODE (fun) == ADDR_EXPR)
	  fun = TREE_OPERAND (fun, 0);

	/* In templates, where there is no way to tell whether a given
	   call uses an actual member function.  So the parser builds
	   FUN as a COMPONENT_REF or a plain IDENTIFIER_NODE until
	   instantiation time.  */
	if (TREE_CODE (fun) != FUNCTION_DECL)
	  ;
	else if (DECL_NONSTATIC_MEMBER_FUNCTION_P (fun))
	  {
	    tree object = code == AGGR_INIT_EXPR && AGGR_INIT_VIA_CTOR_P (t)
	      ? TREE_OPERAND (t, 2)
	      : TREE_VALUE (args);

	    while (TREE_CODE (object) == NOP_EXPR)
	      object = TREE_OPERAND (object, 0);

	    if (TREE_CODE (object) == ADDR_EXPR)
	      object = TREE_OPERAND (object, 0);

	    if (TREE_CODE (TREE_TYPE (object)) != POINTER_TYPE)
	      {
		pp_cxx_postfix_expression (pp, object);
		pp_cxx_dot (pp);
	      }
	    else
	      {
		pp_cxx_postfix_expression (pp, object);
		pp_cxx_arrow (pp);
	      }
	    args = TREE_CHAIN (args);
	    pp->enclosing_scope = strip_pointer_operator (TREE_TYPE (object));
	  }

	pp_cxx_postfix_expression (pp, fun);
	pp->enclosing_scope = saved_scope;
	pp_cxx_call_argument_list (pp, args);
      }
      if (code == AGGR_INIT_EXPR && AGGR_INIT_VIA_CTOR_P (t))
	{
	  pp_cxx_separate_with (pp, ',');
	  pp_cxx_postfix_expression (pp, TREE_OPERAND (t, 2));
	}
      break;

    case BASELINK:
    case VAR_DECL:
    case PARM_DECL:
    case FIELD_DECL:
    case FUNCTION_DECL:
    case OVERLOAD:
    case CONST_DECL:
    case TEMPLATE_DECL:
    case RESULT_DECL:
      pp_cxx_primary_expression (pp, t);
      break;

    case DYNAMIC_CAST_EXPR:
    case STATIC_CAST_EXPR:
    case REINTERPRET_CAST_EXPR:
    case CONST_CAST_EXPR:
      if (code == DYNAMIC_CAST_EXPR)
	pp_cxx_identifier (pp, "dynamic_cast");
      else if (code == STATIC_CAST_EXPR)
	pp_cxx_identifier (pp, "static_cast");
      else if (code == REINTERPRET_CAST_EXPR)
	pp_cxx_identifier (pp, "reinterpret_cast");
      else
	pp_cxx_identifier (pp, "const_cast");
      pp_cxx_begin_template_argument_list (pp);
      pp_cxx_type_id (pp, TREE_TYPE (t));
      pp_cxx_end_template_argument_list (pp);
      pp_left_paren (pp);
      pp_cxx_expression (pp, TREE_OPERAND (t, 0));
      pp_right_paren (pp);
      break;

    case EMPTY_CLASS_EXPR:
      pp_cxx_type_id (pp, TREE_TYPE (t));
      pp_left_paren (pp);
      pp_right_paren (pp);
      break;

    case TYPEID_EXPR:
      t = TREE_OPERAND (t, 0);
      pp_cxx_identifier (pp, "typeid");
      pp_left_paren (pp);
      if (TYPE_P (t))
	pp_cxx_type_id (pp, t);
      else
	pp_cxx_expression (pp, t);
      pp_right_paren (pp);
      break;

    case PSEUDO_DTOR_EXPR:
      pp_cxx_postfix_expression (pp, TREE_OPERAND (t, 0));
      pp_cxx_dot (pp);
      pp_cxx_qualified_id (pp, TREE_OPERAND (t, 1));
      pp_cxx_colon_colon (pp);
      pp_complement (pp);
      pp_cxx_unqualified_id (pp, TREE_OPERAND (t, 2));
      break;

    case ARROW_EXPR:
      pp_cxx_postfix_expression (pp, TREE_OPERAND (t, 0));
      pp_cxx_arrow (pp);
      break;

    default:
      pp_c_postfix_expression (pp_c_base (pp), t);
      break;
    }
}

/* new-expression:
      ::(opt) new new-placement(opt) new-type-id new-initializer(opt)
      ::(opt) new new-placement(opt) ( type-id ) new-initializer(opt)

   new-placement:
      ( expression-list )

   new-type-id:
      type-specifier-seq new-declarator(opt)

   new-declarator:
      ptr-operator new-declarator(opt)
      direct-new-declarator

   direct-new-declarator
      [ expression ]
      direct-new-declarator [ constant-expression ]

   new-initializer:
      ( expression-list(opt) )  */

static void
pp_cxx_new_expression (cxx_pretty_printer *pp, tree t)
{
  enum tree_code code = TREE_CODE (t);
  switch (code)
    {
    case NEW_EXPR:
    case VEC_NEW_EXPR:
      if (NEW_EXPR_USE_GLOBAL (t))
	pp_cxx_colon_colon (pp);
      pp_cxx_identifier (pp, "new");
      if (TREE_OPERAND (t, 0))
	{
	  pp_cxx_call_argument_list (pp, TREE_OPERAND (t, 0));
	  pp_space (pp);
	}
      /* FIXME: array-types are built with one more element.  */
      pp_cxx_type_id (pp, TREE_OPERAND (t, 1));
      if (TREE_OPERAND (t, 2))
	{
	  pp_left_paren (pp);
	  t = TREE_OPERAND (t, 2);
	  if (TREE_CODE (t) == TREE_LIST)
	    pp_c_expression_list (pp_c_base (pp), t);
	  else if (t == void_zero_node)
	    ;			/* OK, empty initializer list.  */
	  else
	    pp_cxx_expression (pp, t);
	  pp_right_paren (pp);
	}
      break;

    default:
      pp_unsupported_tree (pp, t);
    }
}

/* delete-expression:
      ::(opt) delete cast-expression
      ::(opt) delete [ ] cast-expression   */

static void
pp_cxx_delete_expression (cxx_pretty_printer *pp, tree t)
{
  enum tree_code code = TREE_CODE (t);
  switch (code)
    {
    case DELETE_EXPR:
    case VEC_DELETE_EXPR:
      if (DELETE_EXPR_USE_GLOBAL (t))
	pp_cxx_colon_colon (pp);
      pp_cxx_identifier (pp, "delete");
      if (code == VEC_DELETE_EXPR)
	{
	  pp_left_bracket (pp);
	  pp_right_bracket (pp);
	}
      pp_c_cast_expression (pp_c_base (pp), TREE_OPERAND (t, 0));
      break;

    default:
      pp_unsupported_tree (pp, t);
    }
}

/* unary-expression:
      postfix-expression
      ++ cast-expression
      -- cast-expression
      unary-operator cast-expression
      sizeof unary-expression
      sizeof ( type-id )
      new-expression
      delete-expression

   unary-operator: one of
      *   &   +   -  !

   GNU extensions:
      __alignof__ unary-expression
      __alignof__ ( type-id )  */

static void
pp_cxx_unary_expression (cxx_pretty_printer *pp, tree t)
{
  enum tree_code code = TREE_CODE (t);
  switch (code)
    {
    case NEW_EXPR:
    case VEC_NEW_EXPR:
      pp_cxx_new_expression (pp, t);
      break;

    case DELETE_EXPR:
    case VEC_DELETE_EXPR:
      pp_cxx_delete_expression (pp, t);
      break;

    case SIZEOF_EXPR:
    case ALIGNOF_EXPR:
      pp_cxx_identifier (pp, code == SIZEOF_EXPR ? "sizeof" : "__alignof__");
      pp_cxx_whitespace (pp);
      if (TYPE_P (TREE_OPERAND (t, 0)))
	{
	  pp_cxx_left_paren (pp);
	  pp_cxx_type_id (pp, TREE_OPERAND (t, 0));
	  pp_cxx_right_paren (pp);
	}
      else
	pp_unary_expression (pp, TREE_OPERAND (t, 0));
      break;

    case UNARY_PLUS_EXPR:
      pp_plus (pp);
      pp_cxx_cast_expression (pp, TREE_OPERAND (t, 0));
      break;

    default:
      pp_c_unary_expression (pp_c_base (pp), t);
      break;
    }
}

/* cast-expression:
      unary-expression
      ( type-id ) cast-expression  */

static void
pp_cxx_cast_expression (cxx_pretty_printer *pp, tree t)
{
  switch (TREE_CODE (t))
    {
    case CAST_EXPR:
      pp_cxx_type_id (pp, TREE_TYPE (t));
      pp_cxx_call_argument_list (pp, TREE_OPERAND (t, 0));
      break;

    default:
      pp_c_cast_expression (pp_c_base (pp), t);
      break;
    }
}

/* pm-expression:
      cast-expression
      pm-expression .* cast-expression
      pm-expression ->* cast-expression  */

static void
pp_cxx_pm_expression (cxx_pretty_printer *pp, tree t)
{
  switch (TREE_CODE (t))
    {
      /* Handle unfortunate OFFESET_REF overloading here.  */
    case OFFSET_REF:
      if (TYPE_P (TREE_OPERAND (t, 0)))
	{
	  pp_cxx_qualified_id (pp, t);
	  break;
	}
      /* Else fall through.  */
    case MEMBER_REF:
    case DOTSTAR_EXPR:
      pp_cxx_pm_expression (pp, TREE_OPERAND (t, 0));
      pp_cxx_dot (pp);
      pp_star(pp);
      pp_cxx_cast_expression (pp, TREE_OPERAND (t, 1));
      break;


    default:
      pp_cxx_cast_expression (pp, t);
      break;
    }
}

/* multiplicative-expression:
      pm-expression
      multiplicative-expression * pm-expression
      multiplicative-expression / pm-expression
      multiplicative-expression % pm-expression  */

static void
pp_cxx_multiplicative_expression (cxx_pretty_printer *pp, tree e)
{
  enum tree_code code = TREE_CODE (e);
  switch (code)
    {
    case MULT_EXPR:
    case TRUNC_DIV_EXPR:
    case TRUNC_MOD_EXPR:
      pp_cxx_multiplicative_expression (pp, TREE_OPERAND (e, 0));
      pp_space (pp);
      if (code == MULT_EXPR)
	pp_star (pp);
      else if (code == TRUNC_DIV_EXPR)
	pp_slash (pp);
      else
	pp_modulo (pp);
      pp_space (pp);
      pp_cxx_pm_expression (pp, TREE_OPERAND (e, 1));
      break;

    default:
      pp_cxx_pm_expression (pp, e);
      break;
    }
}

/* conditional-expression:
      logical-or-expression
      logical-or-expression ?  expression  : assignment-expression  */

static void
pp_cxx_conditional_expression (cxx_pretty_printer *pp, tree e)
{
  if (TREE_CODE (e) == COND_EXPR)
    {
      pp_c_logical_or_expression (pp_c_base (pp), TREE_OPERAND (e, 0));
      pp_space (pp);
      pp_question (pp);
      pp_space (pp);
      pp_cxx_expression (pp, TREE_OPERAND (e, 1));
      pp_space (pp);
      pp_cxx_assignment_expression (pp, TREE_OPERAND (e, 2));
    }
  else
    pp_c_logical_or_expression (pp_c_base (pp), e);
}

/* Pretty-print a compound assignment operator token as indicated by T.  */

static void
pp_cxx_assignment_operator (cxx_pretty_printer *pp, tree t)
{
  const char *op;

  switch (TREE_CODE (t))
    {
    case NOP_EXPR:
      op = "=";
      break;

    case PLUS_EXPR:
      op = "+=";
      break;

    case MINUS_EXPR:
      op = "-=";
      break;

    case TRUNC_DIV_EXPR:
      op = "/=";
      break;

    case TRUNC_MOD_EXPR:
      op = "%=";
      break;

    default:
      op = tree_code_name[TREE_CODE (t)];
      break;
    }

  pp_cxx_identifier (pp, op);
}


/* assignment-expression:
      conditional-expression
      logical-or-expression assignment-operator assignment-expression
      throw-expression

   throw-expression:
       throw assignment-expression(opt)

   assignment-operator: one of
      =    *=    /=    %=    +=    -=    >>=    <<=    &=    ^=    |=  */

static void
pp_cxx_assignment_expression (cxx_pretty_printer *pp, tree e)
{
  switch (TREE_CODE (e))
    {
    case MODIFY_EXPR:
    case INIT_EXPR:
      pp_c_logical_or_expression (pp_c_base (pp), TREE_OPERAND (e, 0));
      pp_space (pp);
      pp_equal (pp);
      pp_space (pp);
      pp_cxx_assignment_expression (pp, TREE_OPERAND (e, 1));
      break;

    case THROW_EXPR:
      pp_cxx_identifier (pp, "throw");
      if (TREE_OPERAND (e, 0))
	pp_cxx_assignment_expression (pp, TREE_OPERAND (e, 0));
      break;

    case MODOP_EXPR:
      pp_c_logical_or_expression (pp_c_base (pp), TREE_OPERAND (e, 0));
      pp_cxx_assignment_operator (pp, TREE_OPERAND (e, 1));
      pp_cxx_assignment_expression (pp, TREE_OPERAND (e, 2));
      break;

    default:
      pp_cxx_conditional_expression (pp, e);
      break;
    }
}

static void
pp_cxx_expression (cxx_pretty_printer *pp, tree t)
{
  switch (TREE_CODE (t))
    {
    case STRING_CST:
    case INTEGER_CST:
    case REAL_CST:
      pp_cxx_constant (pp, t);
      break;

    case RESULT_DECL:
      pp_cxx_unqualified_id (pp, t);
      break;

#if 0
    case OFFSET_REF:
#endif
    case SCOPE_REF:
    case PTRMEM_CST:
      pp_cxx_qualified_id (pp, t);
      break;

    case OVERLOAD:
      t = OVL_CURRENT (t);
    case VAR_DECL:
    case PARM_DECL:
    case FIELD_DECL:
    case CONST_DECL:
    case FUNCTION_DECL:
    case BASELINK:
    case TEMPLATE_DECL:
    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_PARM_INDEX:
    case TEMPLATE_TEMPLATE_PARM:
    case STMT_EXPR:
      pp_cxx_primary_expression (pp, t);
      break;

    case CALL_EXPR:
    case DYNAMIC_CAST_EXPR:
    case STATIC_CAST_EXPR:
    case REINTERPRET_CAST_EXPR:
    case CONST_CAST_EXPR:
#if 0
    case MEMBER_REF:
#endif
    case EMPTY_CLASS_EXPR:
    case TYPEID_EXPR:
    case PSEUDO_DTOR_EXPR:
    case AGGR_INIT_EXPR:
    case ARROW_EXPR:
      pp_cxx_postfix_expression (pp, t);
      break;

    case NEW_EXPR:
    case VEC_NEW_EXPR:
      pp_cxx_new_expression (pp, t);
      break;

    case DELETE_EXPR:
    case VEC_DELETE_EXPR:
      pp_cxx_delete_expression (pp, t);
      break;

    case SIZEOF_EXPR:
    case ALIGNOF_EXPR:
      pp_cxx_unary_expression (pp, t);
      break;

    case CAST_EXPR:
      pp_cxx_cast_expression (pp, t);
      break;

    case OFFSET_REF:
    case MEMBER_REF:
    case DOTSTAR_EXPR:
      pp_cxx_pm_expression (pp, t);
      break;

    case MULT_EXPR:
    case TRUNC_DIV_EXPR:
    case TRUNC_MOD_EXPR:
      pp_cxx_multiplicative_expression (pp, t);
      break;

    case COND_EXPR:
      pp_cxx_conditional_expression (pp, t);
      break;

    case MODIFY_EXPR:
    case INIT_EXPR:
    case THROW_EXPR:
    case MODOP_EXPR:
      pp_cxx_assignment_expression (pp, t);
      break;

    case NON_DEPENDENT_EXPR:
    case MUST_NOT_THROW_EXPR:
      pp_cxx_expression (pp, t);
      break;

    default:
      pp_c_expression (pp_c_base (pp), t);
      break;
    }
}


/* Declarations.  */

/* function-specifier:
      inline
      virtual
      explicit   */

static void
pp_cxx_function_specifier (cxx_pretty_printer *pp, tree t)
{
  switch (TREE_CODE (t))
    {
    case FUNCTION_DECL:
      if (DECL_VIRTUAL_P (t))
	pp_cxx_identifier (pp, "virtual");
      else if (DECL_CONSTRUCTOR_P (t) && DECL_NONCONVERTING_P (t))
	pp_cxx_identifier (pp, "explicit");
      else
	pp_c_function_specifier (pp_c_base (pp), t);

    default:
      break;
    }
}

/* decl-specifier-seq:
      decl-specifier-seq(opt) decl-specifier

   decl-specifier:
      storage-class-specifier
      type-specifier
      function-specifier
      friend
      typedef  */

static void
pp_cxx_decl_specifier_seq (cxx_pretty_printer *pp, tree t)
{
  switch (TREE_CODE (t))
    {
    case VAR_DECL:
    case PARM_DECL:
    case CONST_DECL:
    case FIELD_DECL:
      pp_cxx_storage_class_specifier (pp, t);
      pp_cxx_decl_specifier_seq (pp, TREE_TYPE (t));
      break;

    case TYPE_DECL:
      pp_cxx_identifier (pp, "typedef");
      pp_cxx_decl_specifier_seq (pp, TREE_TYPE (t));
      break;

    case RECORD_TYPE:
      if (TYPE_PTRMEMFUNC_P (t))
	{
	  tree pfm = TYPE_PTRMEMFUNC_FN_TYPE (t);
	  pp_cxx_decl_specifier_seq (pp, TREE_TYPE (TREE_TYPE (pfm)));
	  pp_cxx_whitespace (pp);
	  pp_cxx_ptr_operator (pp, t);
	}
      break;

    case FUNCTION_DECL:
      /* Constructors don't have return types.  And conversion functions
	 do not have a type-specifier in their return types.  */
      if (DECL_CONSTRUCTOR_P (t) || DECL_CONV_FN_P (t))
	pp_cxx_function_specifier (pp, t);
      else if (DECL_NONSTATIC_MEMBER_FUNCTION_P (t))
	pp_cxx_decl_specifier_seq (pp, TREE_TYPE (TREE_TYPE (t)));
      else
	default:
      pp_c_declaration_specifiers (pp_c_base (pp), t);
      break;
    }
}

/* simple-type-specifier:
      ::(opt) nested-name-specifier(opt) type-name
      ::(opt) nested-name-specifier(opt) template(opt) template-id
      char
      wchar_t
      bool
      short
      int
      long
      signed
      unsigned
      float
      double
      void  */

static void
pp_cxx_simple_type_specifier (cxx_pretty_printer *pp, tree t)
{
  switch (TREE_CODE (t))
    {
    case RECORD_TYPE:
    case UNION_TYPE:
    case ENUMERAL_TYPE:
      pp_cxx_qualified_id (pp, t);
      break;

    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_TEMPLATE_PARM:
    case TEMPLATE_PARM_INDEX:
      pp_cxx_unqualified_id (pp, t);
      break;

    case TYPENAME_TYPE:
      pp_cxx_identifier (pp, "typename");
      pp_cxx_nested_name_specifier (pp, TYPE_CONTEXT (t));
      pp_cxx_unqualified_id (pp, TYPE_NAME (t));
      break;

    default:
      pp_c_type_specifier (pp_c_base (pp), t);
      break;
    }
}

/* type-specifier-seq:
      type-specifier type-specifier-seq(opt)

   type-specifier:
      simple-type-specifier
      class-specifier
      enum-specifier
      elaborated-type-specifier
      cv-qualifier   */

static void
pp_cxx_type_specifier_seq (cxx_pretty_printer *pp, tree t)
{
  switch (TREE_CODE (t))
    {
    case TEMPLATE_DECL:
    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_TEMPLATE_PARM:
    case TYPE_DECL:
    case BOUND_TEMPLATE_TEMPLATE_PARM:
      pp_cxx_cv_qualifier_seq (pp, t);
      pp_cxx_simple_type_specifier (pp, t);
      break;

    case METHOD_TYPE:
      pp_cxx_type_specifier_seq (pp, TREE_TYPE (t));
      pp_cxx_space_for_pointer_operator (pp, TREE_TYPE (t));
      pp_cxx_nested_name_specifier (pp, TYPE_METHOD_BASETYPE (t));
      break;

    default:
      if (!(TREE_CODE (t) == FUNCTION_DECL && DECL_CONSTRUCTOR_P (t)))
	pp_c_specifier_qualifier_list (pp_c_base (pp), t);
    }
}

/* ptr-operator:
      * cv-qualifier-seq(opt)
      &
      ::(opt) nested-name-specifier * cv-qualifier-seq(opt)  */

static void
pp_cxx_ptr_operator (cxx_pretty_printer *pp, tree t)
{
  if (!TYPE_P (t) && TREE_CODE (t) != TYPE_DECL)
    t = TREE_TYPE (t);
  switch (TREE_CODE (t))
    {
    case REFERENCE_TYPE:
    case POINTER_TYPE:
      if (TREE_CODE (TREE_TYPE (t)) == POINTER_TYPE
	  || TYPE_PTR_TO_MEMBER_P (TREE_TYPE (t)))
	pp_cxx_ptr_operator (pp, TREE_TYPE (t));
      if (TREE_CODE (t) == POINTER_TYPE)
	{
	  pp_star (pp);
	  pp_cxx_cv_qualifier_seq (pp, t);
	}
      else
	pp_ampersand (pp);
      break;

    case RECORD_TYPE:
      if (TYPE_PTRMEMFUNC_P (t))
	{
	  pp_cxx_left_paren (pp);
	  pp_cxx_nested_name_specifier (pp, TYPE_PTRMEMFUNC_OBJECT_TYPE (t));
	  pp_star (pp);
	  break;
	}
    case OFFSET_TYPE:
      if (TYPE_PTR_TO_MEMBER_P (t))
	{
	  if (TREE_CODE (TREE_TYPE (t)) == ARRAY_TYPE)
	    pp_cxx_left_paren (pp);
	  pp_cxx_nested_name_specifier (pp, TYPE_PTRMEM_CLASS_TYPE (t));
	  pp_star (pp);
	  pp_cxx_cv_qualifier_seq (pp, t);
	  break;
	}
      /* else fall through.  */

    default:
      pp_unsupported_tree (pp, t);
      break;
    }
}

static inline tree
pp_cxx_implicit_parameter_type (tree mf)
{
  return TREE_TYPE (TREE_VALUE (TYPE_ARG_TYPES (TREE_TYPE (mf))));
}

/*
   parameter-declaration:
      decl-specifier-seq declarator
      decl-specifier-seq declarator = assignment-expression
      decl-specifier-seq abstract-declarator(opt)
      decl-specifier-seq abstract-declarator(opt) assignment-expression  */

static inline void
pp_cxx_parameter_declaration (cxx_pretty_printer *pp, tree t)
{
  pp_cxx_decl_specifier_seq (pp, t);
  if (TYPE_P (t))
    pp_cxx_abstract_declarator (pp, t);
  else
    pp_cxx_declarator (pp, t);
}

/* parameter-declaration-clause:
      parameter-declaration-list(opt) ...(opt)
      parameter-declaration-list , ...

   parameter-declaration-list:
      parameter-declaration
      parameter-declaration-list , parameter-declaration  */

static void
pp_cxx_parameter_declaration_clause (cxx_pretty_printer *pp, tree t)
{
  tree args = TYPE_P (t) ? NULL : FUNCTION_FIRST_USER_PARM (t);
  tree types =
    TYPE_P (t) ? TYPE_ARG_TYPES (t) : FUNCTION_FIRST_USER_PARMTYPE (t);
  const bool abstract = args == NULL
    || pp_c_base (pp)->flags & pp_c_flag_abstract;
  bool first = true;

  /* Skip artificial parameter for nonstatic member functions.  */
  if (TREE_CODE (t) == METHOD_TYPE)
    types = TREE_CHAIN (types);

  pp_cxx_left_paren (pp);
  for (; args; args = TREE_CHAIN (args), types = TREE_CHAIN (types))
    {
      if (!first)
	pp_cxx_separate_with (pp, ',');
      first = false;
      pp_cxx_parameter_declaration (pp, abstract ? TREE_VALUE (types) : args);
      if (!abstract && pp_c_base (pp)->flags & pp_cxx_flag_default_argument)
	{
	  pp_cxx_whitespace (pp);
	  pp_equal (pp);
	  pp_cxx_whitespace (pp);
	  pp_cxx_assignment_expression (pp, TREE_PURPOSE (types));
	}
    }
  pp_cxx_right_paren (pp);
}

/* exception-specification:
      throw ( type-id-list(opt) )

   type-id-list
      type-id
      type-id-list , type-id   */

static void
pp_cxx_exception_specification (cxx_pretty_printer *pp, tree t)
{
  tree ex_spec = TYPE_RAISES_EXCEPTIONS (t);

  if (!TYPE_NOTHROW_P (t) && ex_spec == NULL)
    return;
  pp_cxx_identifier (pp, "throw");
  pp_cxx_left_paren (pp);
  for (; ex_spec && TREE_VALUE (ex_spec); ex_spec = TREE_CHAIN (ex_spec))
    {
      pp_cxx_type_id (pp, TREE_VALUE (ex_spec));
      if (TREE_CHAIN (ex_spec))
	pp_cxx_separate_with (pp, ',');
    }
  pp_cxx_right_paren (pp);
}

/* direct-declarator:
      declarator-id
      direct-declarator ( parameter-declaration-clause ) cv-qualifier-seq(opt)
					    exception-specification(opt)
      direct-declaration [ constant-expression(opt) ]
      ( declarator )  */

static void
pp_cxx_direct_declarator (cxx_pretty_printer *pp, tree t)
{
  switch (TREE_CODE (t))
    {
    case VAR_DECL:
    case PARM_DECL:
    case CONST_DECL:
    case FIELD_DECL:
      if (DECL_NAME (t))
	{
	  pp_cxx_space_for_pointer_operator (pp, TREE_TYPE (t));
	  pp_cxx_id_expression (pp, DECL_NAME (t));
	}
      pp_cxx_abstract_declarator (pp, TREE_TYPE (t));
      break;

    case FUNCTION_DECL:
      pp_cxx_space_for_pointer_operator (pp, TREE_TYPE (TREE_TYPE (t)));
      pp_cxx_id_expression (pp, t);
      pp_cxx_parameter_declaration_clause (pp, t);

      if (DECL_NONSTATIC_MEMBER_FUNCTION_P (t))
	{
	  pp_base (pp)->padding = pp_before;
	  pp_cxx_cv_qualifier_seq (pp, pp_cxx_implicit_parameter_type (t));
	}

      pp_cxx_exception_specification (pp, TREE_TYPE (t));
      break;

    case TYPENAME_TYPE:
    case TEMPLATE_DECL:
    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_PARM_INDEX:
    case TEMPLATE_TEMPLATE_PARM:
      break;

    default:
      pp_c_direct_declarator (pp_c_base (pp), t);
      break;
    }
}

/* declarator:
   direct-declarator
   ptr-operator declarator  */

static void
pp_cxx_declarator (cxx_pretty_printer *pp, tree t)
{
  pp_cxx_direct_declarator (pp, t);
}

/* ctor-initializer:
      : mem-initializer-list

   mem-initializer-list:
      mem-initializer
      mem-initializer , mem-initializer-list

   mem-initializer:
      mem-initializer-id ( expression-list(opt) )

   mem-initializer-id:
      ::(opt) nested-name-specifier(opt) class-name
      identifier   */

static void
pp_cxx_ctor_initializer (cxx_pretty_printer *pp, tree t)
{
  t = TREE_OPERAND (t, 0);
  pp_cxx_whitespace (pp);
  pp_colon (pp);
  pp_cxx_whitespace (pp);
  for (; t; t = TREE_CHAIN (t))
    {
      pp_cxx_primary_expression (pp, TREE_PURPOSE (t));
      pp_cxx_call_argument_list (pp, TREE_VALUE (t));
      if (TREE_CHAIN (t))
	pp_cxx_separate_with (pp, ',');
    }
}

/* function-definition:
      decl-specifier-seq(opt) declarator ctor-initializer(opt) function-body
      decl-specifier-seq(opt) declarator function-try-block  */

static void
pp_cxx_function_definition (cxx_pretty_printer *pp, tree t)
{
  tree saved_scope = pp->enclosing_scope;
  pp_cxx_decl_specifier_seq (pp, t);
  pp_cxx_declarator (pp, t);
  pp_needs_newline (pp) = true;
  pp->enclosing_scope = DECL_CONTEXT (t);
  if (DECL_SAVED_TREE (t))
    pp_cxx_statement (pp, DECL_SAVED_TREE (t));
  else
    {
      pp_cxx_semicolon (pp);
      pp_needs_newline (pp) = true;
    }
  pp_flush (pp);
  pp->enclosing_scope = saved_scope;
}

/* abstract-declarator:
      ptr-operator abstract-declarator(opt)
      direct-abstract-declarator  */

static void
pp_cxx_abstract_declarator (cxx_pretty_printer *pp, tree t)
{
  if (TYPE_PTRMEM_P (t) || TYPE_PTRMEMFUNC_P (t))
    pp_cxx_right_paren (pp);
  else if (POINTER_TYPE_P (t))
    {
      if (TREE_CODE (TREE_TYPE (t)) == ARRAY_TYPE
	  || TREE_CODE (TREE_TYPE (t)) == FUNCTION_TYPE)
	pp_cxx_right_paren (pp);
      t = TREE_TYPE (t);
    }
  pp_cxx_direct_abstract_declarator (pp, t);
}

/* direct-abstract-declarator:
      direct-abstract-declarator(opt) ( parameter-declaration-clause )
			   cv-qualifier-seq(opt) exception-specification(opt)
      direct-abstract-declarator(opt) [ constant-expression(opt) ]
      ( abstract-declarator )  */

static void
pp_cxx_direct_abstract_declarator (cxx_pretty_printer *pp, tree t)
{
  switch (TREE_CODE (t))
    {
    case REFERENCE_TYPE:
      pp_cxx_abstract_declarator (pp, t);
      break;

    case RECORD_TYPE:
      if (TYPE_PTRMEMFUNC_P (t))
	pp_cxx_direct_abstract_declarator (pp, TYPE_PTRMEMFUNC_FN_TYPE (t));
      break;

    case METHOD_TYPE:
    case FUNCTION_TYPE:
      pp_cxx_parameter_declaration_clause (pp, t);
      pp_cxx_direct_abstract_declarator (pp, TREE_TYPE (t));
      if (TREE_CODE (t) == METHOD_TYPE)
	{
	  pp_base (pp)->padding = pp_before;
	  pp_cxx_cv_qualifier_seq
	    (pp, TREE_TYPE (TREE_VALUE (TYPE_ARG_TYPES (t))));
	}
      pp_cxx_exception_specification (pp, t);
      break;

    case TYPENAME_TYPE:
    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_TEMPLATE_PARM:
    case BOUND_TEMPLATE_TEMPLATE_PARM:
    case UNBOUND_CLASS_TEMPLATE:
      break;

    default:
      pp_c_direct_abstract_declarator (pp_c_base (pp), t);
      break;
    }
}

/* type-id:
     type-specifier-seq abstract-declarator(opt) */

static void
pp_cxx_type_id (cxx_pretty_printer *pp, tree t)
{
  pp_flags saved_flags = pp_c_base (pp)->flags;
  pp_c_base (pp)->flags |= pp_c_flag_abstract;

  switch (TREE_CODE (t))
    {
    case TYPE_DECL:
    case UNION_TYPE:
    case RECORD_TYPE:
    case ENUMERAL_TYPE:
    case TYPENAME_TYPE:
    case BOUND_TEMPLATE_TEMPLATE_PARM:
    case UNBOUND_CLASS_TEMPLATE:
    case TEMPLATE_TEMPLATE_PARM:
    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_PARM_INDEX:
    case TEMPLATE_DECL:
    case TYPEOF_TYPE:
    case TEMPLATE_ID_EXPR:
      pp_cxx_type_specifier_seq (pp, t);
      break;

    default:
      pp_c_type_id (pp_c_base (pp), t);
      break;
    }

  pp_c_base (pp)->flags = saved_flags;
}

/* template-argument-list:
      template-argument
      template-argument-list, template-argument

   template-argument:
      assignment-expression
      type-id
      template-name   */

static void
pp_cxx_template_argument_list (cxx_pretty_printer *pp, tree t)
{
  int i;
  if (t == NULL)
    return;
  for (i = 0; i < TREE_VEC_LENGTH (t); ++i)
    {
      tree arg = TREE_VEC_ELT (t, i);
      if (i != 0)
	pp_cxx_separate_with (pp, ',');
      if (TYPE_P (arg) || (TREE_CODE (arg) == TEMPLATE_DECL
			   && TYPE_P (DECL_TEMPLATE_RESULT (arg))))
	pp_cxx_type_id (pp, arg);
      else
	pp_cxx_expression (pp, arg);
    }
}


static void
pp_cxx_exception_declaration (cxx_pretty_printer *pp, tree t)
{
  t = DECL_EXPR_DECL (t);
  pp_cxx_type_specifier_seq (pp, t);
  if (TYPE_P (t))
    pp_cxx_abstract_declarator (pp, t);
  else
    pp_cxx_declarator (pp, t);
}

/* Statements.  */

static void
pp_cxx_statement (cxx_pretty_printer *pp, tree t)
{
  switch (TREE_CODE (t))
    {
    case CTOR_INITIALIZER:
      pp_cxx_ctor_initializer (pp, t);
      break;

    case USING_STMT:
      pp_cxx_identifier (pp, "using");
      pp_cxx_identifier (pp, "namespace");
      if (DECL_CONTEXT (t))
	pp_cxx_nested_name_specifier (pp, DECL_CONTEXT (t));
      pp_cxx_qualified_id (pp, USING_STMT_NAMESPACE (t));
      break;

    case USING_DECL:
      pp_cxx_identifier (pp, "using");
      pp_cxx_nested_name_specifier (pp, USING_DECL_SCOPE (t));
      pp_cxx_unqualified_id (pp, DECL_NAME (t));
      break;

    case EH_SPEC_BLOCK:
      break;

      /* try-block:
	    try compound-statement handler-seq  */
    case TRY_BLOCK:
      pp_maybe_newline_and_indent (pp, 0);
      pp_cxx_identifier (pp, "try");
      pp_newline_and_indent (pp, 3);
      pp_cxx_statement (pp, TRY_STMTS (t));
      pp_newline_and_indent (pp, -3);
      if (CLEANUP_P (t))
	;
      else
	pp_cxx_statement (pp, TRY_HANDLERS (t));
      break;

      /*
	 handler-seq:
	    handler handler-seq(opt)

	 handler:
	 catch ( exception-declaration ) compound-statement

	 exception-declaration:
	    type-specifier-seq declarator
	    type-specifier-seq abstract-declarator
	    ...   */
    case HANDLER:
      pp_cxx_identifier (pp, "catch");
      pp_cxx_left_paren (pp);
      pp_cxx_exception_declaration (pp, HANDLER_PARMS (t));
      pp_cxx_right_paren (pp);
      pp_indentation (pp) += 3;
      pp_needs_newline (pp) = true;
      pp_cxx_statement (pp, HANDLER_BODY (t));
      pp_indentation (pp) -= 3;
      pp_needs_newline (pp) = true;
      break;

      /* selection-statement:
	    if ( expression ) statement
	    if ( expression ) statement else statement  */
    case IF_STMT:
      pp_cxx_identifier (pp, "if");
      pp_cxx_whitespace (pp);
      pp_cxx_left_paren (pp);
      pp_cxx_expression (pp, IF_COND (t));
      pp_cxx_right_paren (pp);
      pp_newline_and_indent (pp, 2);
      pp_cxx_statement (pp, THEN_CLAUSE (t));
      pp_newline_and_indent (pp, -2);
      if (ELSE_CLAUSE (t))
	{
	  tree else_clause = ELSE_CLAUSE (t);
	  pp_cxx_identifier (pp, "else");
	  if (TREE_CODE (else_clause) == IF_STMT)
	    pp_cxx_whitespace (pp);
	  else
	    pp_newline_and_indent (pp, 2);
	  pp_cxx_statement (pp, else_clause);
	  if (TREE_CODE (else_clause) != IF_STMT)
	    pp_newline_and_indent (pp, -2);
	}
      break;

    case SWITCH_STMT:
      pp_cxx_identifier (pp, "switch");
      pp_space (pp);
      pp_cxx_left_paren (pp);
      pp_cxx_expression (pp, SWITCH_STMT_COND (t));
      pp_cxx_right_paren (pp);
      pp_indentation (pp) += 3;
      pp_needs_newline (pp) = true;
      pp_cxx_statement (pp, SWITCH_STMT_BODY (t));
      pp_newline_and_indent (pp, -3);
      break;

      /* iteration-statement:
	    while ( expression ) statement
	    do statement while ( expression ) ;
	    for ( expression(opt) ; expression(opt) ; expression(opt) ) statement
	    for ( declaration expression(opt) ; expression(opt) ) statement  */
    case WHILE_STMT:
      pp_cxx_identifier (pp, "while");
      pp_space (pp);
      pp_cxx_left_paren (pp);
      pp_cxx_expression (pp, WHILE_COND (t));
      pp_cxx_right_paren (pp);
      pp_newline_and_indent (pp, 3);
      pp_cxx_statement (pp, WHILE_BODY (t));
      pp_indentation (pp) -= 3;
      pp_needs_newline (pp) = true;
      break;

    case DO_STMT:
      pp_cxx_identifier (pp, "do");
      pp_newline_and_indent (pp, 3);
      pp_cxx_statement (pp, DO_BODY (t));
      pp_newline_and_indent (pp, -3);
      pp_cxx_identifier (pp, "while");
      pp_space (pp);
      pp_cxx_left_paren (pp);
      pp_cxx_expression (pp, DO_COND (t));
      pp_cxx_right_paren (pp);
      pp_cxx_semicolon (pp);
      pp_needs_newline (pp) = true;
      break;

    case FOR_STMT:
      pp_cxx_identifier (pp, "for");
      pp_space (pp);
      pp_cxx_left_paren (pp);
      if (FOR_INIT_STMT (t))
	pp_cxx_statement (pp, FOR_INIT_STMT (t));
      else
	pp_cxx_semicolon (pp);
      pp_needs_newline (pp) = false;
      pp_cxx_whitespace (pp);
      if (FOR_COND (t))
	pp_cxx_expression (pp, FOR_COND (t));
      pp_cxx_semicolon (pp);
      pp_needs_newline (pp) = false;
      pp_cxx_whitespace (pp);
      if (FOR_EXPR (t))
	pp_cxx_expression (pp, FOR_EXPR (t));
      pp_cxx_right_paren (pp);
      pp_newline_and_indent (pp, 3);
      pp_cxx_statement (pp, FOR_BODY (t));
      pp_indentation (pp) -= 3;
      pp_needs_newline (pp) = true;
      break;

      /* jump-statement:
	    goto identifier;
	    continue ;
	    return expression(opt) ;  */
    case BREAK_STMT:
    case CONTINUE_STMT:
      pp_identifier (pp, TREE_CODE (t) == BREAK_STMT ? "break" : "continue");
      pp_cxx_semicolon (pp);
      pp_needs_newline (pp) = true;
      break;

      /* expression-statement:
	    expression(opt) ;  */
    case EXPR_STMT:
      pp_cxx_expression (pp, EXPR_STMT_EXPR (t));
      pp_cxx_semicolon (pp);
      pp_needs_newline (pp) = true;
      break;

    case CLEANUP_STMT:
      pp_cxx_identifier (pp, "try");
      pp_newline_and_indent (pp, 2);
      pp_cxx_statement (pp, CLEANUP_BODY (t));
      pp_newline_and_indent (pp, -2);
      pp_cxx_identifier (pp, CLEANUP_EH_ONLY (t) ? "catch" : "finally");
      pp_newline_and_indent (pp, 2);
      pp_cxx_statement (pp, CLEANUP_EXPR (t));
      pp_newline_and_indent (pp, -2);
      break;

    default:
      pp_c_statement (pp_c_base (pp), t);
      break;
    }
}

/* original-namespace-definition:
      namespace identifier { namespace-body }

  As an edge case, we also handle unnamed namespace definition here.  */

static void
pp_cxx_original_namespace_definition (cxx_pretty_printer *pp, tree t)
{
  pp_cxx_identifier (pp, "namespace");
  if (DECL_CONTEXT (t))
    pp_cxx_nested_name_specifier (pp, DECL_CONTEXT (t));
  if (DECL_NAME (t))
    pp_cxx_unqualified_id (pp, t);
  pp_cxx_whitespace (pp);
  pp_cxx_left_brace (pp);
  /* We do not print the namespace-body.  */
  pp_cxx_whitespace (pp);
  pp_cxx_right_brace (pp);
}

/* namespace-alias:
      identifier

   namespace-alias-definition:
      namespace identifier = qualified-namespace-specifier ;

   qualified-namespace-specifier:
      ::(opt) nested-name-specifier(opt) namespace-name   */

static void
pp_cxx_namespace_alias_definition (cxx_pretty_printer *pp, tree t)
{
  pp_cxx_identifier (pp, "namespace");
  if (DECL_CONTEXT (t))
    pp_cxx_nested_name_specifier (pp, DECL_CONTEXT (t));
  pp_cxx_unqualified_id (pp, t);
  pp_cxx_whitespace (pp);
  pp_equal (pp);
  pp_cxx_whitespace (pp);
  if (DECL_CONTEXT (DECL_NAMESPACE_ALIAS (t)))
    pp_cxx_nested_name_specifier (pp,
				  DECL_CONTEXT (DECL_NAMESPACE_ALIAS (t)));
  pp_cxx_qualified_id (pp, DECL_NAMESPACE_ALIAS (t));
  pp_cxx_semicolon (pp);
}

/* simple-declaration:
      decl-specifier-seq(opt) init-declarator-list(opt)  */

static void
pp_cxx_simple_declaration (cxx_pretty_printer *pp, tree t)
{
  pp_cxx_decl_specifier_seq (pp, t);
  pp_cxx_init_declarator (pp, t);
  pp_cxx_semicolon (pp);
  pp_needs_newline (pp) = true;
}

/*
  template-parameter-list:
     template-parameter
     template-parameter-list , template-parameter  */

static inline void
pp_cxx_template_parameter_list (cxx_pretty_printer *pp, tree t)
{
  const int n = TREE_VEC_LENGTH (t);
  int i;
  for (i = 0; i < n; ++i)
    {
      if (i)
	pp_cxx_separate_with (pp, ',');
      pp_cxx_template_parameter (pp, TREE_VEC_ELT (t, i));
    }
}

/* template-parameter:
      type-parameter
      parameter-declaration

   type-parameter:
     class identifier(opt)
     class identifier(op) = type-id
     typename identifier(opt)
     typename identifier(opt) = type-id
     template < template-parameter-list > class identifier(opt)
     template < template-parameter-list > class identifier(opt) = template-name  */

static void
pp_cxx_template_parameter (cxx_pretty_printer *pp, tree t)
{
  tree parameter =  TREE_VALUE (t);
  switch (TREE_CODE (parameter))
    {
    case TYPE_DECL:
      pp_cxx_identifier (pp, "class");
      if (DECL_NAME (parameter))
	pp_cxx_tree_identifier (pp, DECL_NAME (parameter));
      /* FIXME: Chech if we should print also default argument.  */
      break;

    case PARM_DECL:
      pp_cxx_parameter_declaration (pp, parameter);
      break;

    case TEMPLATE_DECL:
      break;

    default:
      pp_unsupported_tree (pp, t);
      break;
    }
}

/* Pretty-print a template parameter in the canonical form
   "template-parameter-<level>-<position in parameter list>".  */

void
pp_cxx_canonical_template_parameter (cxx_pretty_printer *pp, tree parm)
{
  const enum tree_code code = TREE_CODE (parm);

  /* Brings type template parameters to the canonical forms.  */
  if (code == TEMPLATE_TYPE_PARM || code == TEMPLATE_TEMPLATE_PARM
      || code == BOUND_TEMPLATE_TEMPLATE_PARM)
    parm = TEMPLATE_TYPE_PARM_INDEX (parm);

  pp_cxx_begin_template_argument_list (pp);
  pp_cxx_identifier (pp, "template-parameter-");
  pp_wide_integer (pp, TEMPLATE_PARM_LEVEL (parm));
  pp_minus (pp);
  pp_wide_integer (pp, TEMPLATE_PARM_IDX (parm) + 1);
  pp_cxx_end_template_argument_list (pp);
}

/*
  template-declaration:
     export(opt) template < template-parameter-list > declaration   */

static void
pp_cxx_template_declaration (cxx_pretty_printer *pp, tree t)
{
  tree tmpl = most_general_template (t);
  tree level;
  int i = 0;

  pp_maybe_newline_and_indent (pp, 0);
  for (level = DECL_TEMPLATE_PARMS (tmpl); level; level = TREE_CHAIN (level))
    {
      pp_cxx_identifier (pp, "template");
      pp_cxx_begin_template_argument_list (pp);
      pp_cxx_template_parameter_list (pp, TREE_VALUE (level));
      pp_cxx_end_template_argument_list (pp);
      pp_newline_and_indent (pp, 3);
      i += 3;
    }
  if (TREE_CODE (t) == FUNCTION_DECL && DECL_SAVED_TREE (t))
    pp_cxx_function_definition (pp, t);
  else
    pp_cxx_simple_declaration (pp, t);
}

static void
pp_cxx_explicit_specialization (cxx_pretty_printer *pp, tree t)
{
  pp_unsupported_tree (pp, t);
}

static void
pp_cxx_explicit_instantiation (cxx_pretty_printer *pp, tree t)
{
  pp_unsupported_tree (pp, t);
}

/*
    declaration:
       block-declaration
       function-definition
       template-declaration
       explicit-instantiation
       explicit-specialization
       linkage-specification
       namespace-definition

    block-declaration:
       simple-declaration
       asm-definition
       namespace-alias-definition
       using-declaration
       using-directive  */
void
pp_cxx_declaration (cxx_pretty_printer *pp, tree t)
{
  if (!DECL_LANG_SPECIFIC (t))
    pp_cxx_simple_declaration (pp, t);
  else if (DECL_USE_TEMPLATE (t))
    switch (DECL_USE_TEMPLATE (t))
      {
      case 1:
	pp_cxx_template_declaration (pp, t);
	break;

      case 2:
	pp_cxx_explicit_specialization (pp, t);
	break;

      case 3:
	pp_cxx_explicit_instantiation (pp, t);
	break;

      default:
	break;
      }
  else switch (TREE_CODE (t))
    {
    case VAR_DECL:
    case TYPE_DECL:
      pp_cxx_simple_declaration (pp, t);
      break;

    case FUNCTION_DECL:
      if (DECL_SAVED_TREE (t))
	pp_cxx_function_definition (pp, t);
      else
	pp_cxx_simple_declaration (pp, t);
      break;

    case NAMESPACE_DECL:
      if (DECL_NAMESPACE_ALIAS (t))
	pp_cxx_namespace_alias_definition (pp, t);
      else
	pp_cxx_original_namespace_definition (pp, t);
      break;

    default:
      pp_unsupported_tree (pp, t);
      break;
    }
}


typedef c_pretty_print_fn pp_fun;

/* Initialization of a C++ pretty-printer object.  */

void
pp_cxx_pretty_printer_init (cxx_pretty_printer *pp)
{
  pp_c_pretty_printer_init (pp_c_base (pp));
  pp_set_line_maximum_length (pp, 0);

  pp->c_base.declaration = (pp_fun) pp_cxx_declaration;
  pp->c_base.declaration_specifiers = (pp_fun) pp_cxx_decl_specifier_seq;
  pp->c_base.function_specifier = (pp_fun) pp_cxx_function_specifier;
  pp->c_base.type_specifier_seq = (pp_fun) pp_cxx_type_specifier_seq;
  pp->c_base.declarator = (pp_fun) pp_cxx_declarator;
  pp->c_base.direct_declarator = (pp_fun) pp_cxx_direct_declarator;
  pp->c_base.parameter_list = (pp_fun) pp_cxx_parameter_declaration_clause;
  pp->c_base.type_id = (pp_fun) pp_cxx_type_id;
  pp->c_base.abstract_declarator = (pp_fun) pp_cxx_abstract_declarator;
  pp->c_base.direct_abstract_declarator =
    (pp_fun) pp_cxx_direct_abstract_declarator;
  pp->c_base.simple_type_specifier = (pp_fun)pp_cxx_simple_type_specifier;

  /* pp->c_base.statement = (pp_fun) pp_cxx_statement;  */

  pp->c_base.constant = (pp_fun) pp_cxx_constant;
  pp->c_base.id_expression = (pp_fun) pp_cxx_id_expression;
  pp->c_base.primary_expression = (pp_fun) pp_cxx_primary_expression;
  pp->c_base.postfix_expression = (pp_fun) pp_cxx_postfix_expression;
  pp->c_base.unary_expression = (pp_fun) pp_cxx_unary_expression;
  pp->c_base.multiplicative_expression = (pp_fun) pp_cxx_multiplicative_expression;
  pp->c_base.conditional_expression = (pp_fun) pp_cxx_conditional_expression;
  pp->c_base.assignment_expression = (pp_fun) pp_cxx_assignment_expression;
  pp->c_base.expression = (pp_fun) pp_cxx_expression;
  pp->enclosing_scope = global_namespace;
}
