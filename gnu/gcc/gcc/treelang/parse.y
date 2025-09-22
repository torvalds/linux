/* -*- c -*- emacs mode c */
/* TREELANG Compiler parser.

---------------------------------------------------------------------

Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005
Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.

In other words, you are welcome to use, share and improve this program.
You are forbidden to forbid anyone else to use, share and improve
what you give them.   Help stamp out software-hoarding!

---------------------------------------------------------------------

Written by Tim Josling 1999-2001, based in part on other parts of
the GCC compiler.  */

/* Grammar Conflicts
   *****************
   There are no conflicts in this grammar.  Please keep it that way.  */

%{
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "timevar.h"
#include "tree.h"

#include "treelang.h"
#include "treetree.h"
#include "toplev.h"

#define YYDEBUG 1
#define YYPRINT(file, type, value) print_token (file, type, value)
#define YYERROR_VERBOSE YES

  /* My yylex routine used to intercept calls to flex generated code, to
     record lex time.  */
  int yylex (void);
  static inline int my_yylex (void);

  /* Call lex, but ensure time is charged to TV_LEX.  */
  static inline int
    my_yylex (void)
    {
      int res;
      timevar_push (TV_LEX);
      res = yylex ();
      timevar_pop (TV_LEX);
      return res;
    }
#define yylex my_yylex

  extern int option_parser_trace;

  /* Local prototypes.  */
  static void yyerror (const char *error_message);
  int yyparse (void);
  void print_token (FILE *file, unsigned int type ATTRIBUTE_UNUSED,
		    YYSTYPE value);
  static struct prod_token_parm_item *reverse_prod_list
    (struct prod_token_parm_item *old_first);
  static void ensure_not_void (unsigned int type,
			       struct prod_token_parm_item *name);
  static int check_type_match (int type_num, struct prod_token_parm_item *exp);
  static int get_common_type (struct prod_token_parm_item *type1,
			      struct prod_token_parm_item *type2);
  static struct prod_token_parm_item *make_integer_constant
    (struct prod_token_parm_item *value);
  static struct prod_token_parm_item *make_plus_expression
    (struct prod_token_parm_item *tok, struct prod_token_parm_item *op1,
     struct prod_token_parm_item *op2, int type_code, int prod_code);
  static void set_storage (struct prod_token_parm_item *prod);

  /* File global variables.  */
  static struct prod_token_parm_item *current_function = NULL;
%}

/* Not %raw - seems to have bugs.  */
%token_table

/* Punctuation.  */
%token RIGHT_BRACE
%token LEFT_BRACE
%token RIGHT_SQUARE_BRACKET
%token LEFT_SQUARE_BRACKET
%token RIGHT_PARENTHESIS
%token LEFT_PARENTHESIS
%token SEMICOLON
%token ASTERISK
%token COMMA
%right EQUALS
%right ASSIGN
%left  tl_PLUS
%left  tl_MINUS

/* Literals.  */
%token INTEGER

/* Keywords.  */
%token IF
%token ELSE
%token tl_RETURN
%token CHAR
%token INT
%token UNSIGNED
%token VOID
%token TYPEDEF
%token NAME
%token STATIC
%token AUTOMATIC
%token EXTERNAL_DEFINITION
%token EXTERNAL_REFERENCE

/* Tokens not passed to parser.  */
%token WHITESPACE
%token COMMENT

/* Pseudo tokens - productions.  */
%token PROD_VARIABLE_NAME
%token PROD_TYPE_NAME
%token PROD_FUNCTION_NAME
%token PROD_INTEGER_CONSTANT
%token PROD_PLUS_EXPRESSION
%token PROD_MINUS_EXPRESSION
%token PROD_ASSIGN_EXPRESSION
%token PROD_VARIABLE_REFERENCE_EXPRESSION
%token PROD_PARAMETER
%token PROD_FUNCTION_INVOCATION
%expect 0
%%

file:
/* Nil.  */ {
  /* Nothing to do.  */
}
|declarations {
  /* Nothing to do.  */
}
;


declarations:
declaration {
  /* Nothing to do.  */
}
| declarations declaration {
  /* Nothing to do.  */
}
;

declaration:
variable_def {
  /* Nothing to do.  */
}
|function_prototype {
  /* Nothing to do.  */
}
|function {
  /* Nothing to do.  */
}
;

variable_def:
storage typename NAME init_opt SEMICOLON {
  struct prod_token_parm_item *tok;
  struct prod_token_parm_item *prod;
  tok = $3;
  prod = make_production (PROD_VARIABLE_NAME, tok);
  SYMBOL_TABLE_NAME (prod) = tok;
  EXPRESSION_TYPE (prod) = $2;
  VAR_INIT (prod) = $4;
  NUMERIC_TYPE (prod) =
    NUMERIC_TYPE (( (struct prod_token_parm_item *)EXPRESSION_TYPE (prod)));
  ensure_not_void (NUMERIC_TYPE (prod), tok);
  if (insert_tree_name (prod))
    {
      YYERROR;
    }
  STORAGE_CLASS_TOKEN (prod) = $1;
  set_storage (prod);

  if (VAR_INIT (prod))
    {
      gcc_assert (((struct prod_token_parm_item *)
		   VAR_INIT (prod))->tp.pro.code);
      if (STORAGE_CLASS (prod) == EXTERNAL_REFERENCE_STORAGE)
	{
	  error("%Hexternal reference variable %q.*s has an initial value",
		&tok->tp.tok.location, tok->tp.tok.length, tok->tp.tok.chars);
	  YYERROR;
	  VAR_INIT (prod) = NULL;
	}

    }

  prod->tp.pro.code = tree_code_create_variable
    (STORAGE_CLASS (prod),
     ((struct prod_token_parm_item *)SYMBOL_TABLE_NAME (prod))->tp.tok.chars,
     ((struct prod_token_parm_item *)SYMBOL_TABLE_NAME (prod))->tp.tok.length,
     NUMERIC_TYPE (prod),
     VAR_INIT (prod) ?
     ((struct prod_token_parm_item *)VAR_INIT (prod))->tp.pro.code : NULL,
     tok->tp.tok.location);
  gcc_assert (prod->tp.pro.code);
}
;

storage:
STATIC
|AUTOMATIC
|EXTERNAL_DEFINITION
|EXTERNAL_REFERENCE
;

parameter:
typename NAME {
  struct prod_token_parm_item *tok;
  struct prod_token_parm_item *prod;
  struct prod_token_parm_item *prod2;
  tok = $2;
  prod = make_production (PROD_VARIABLE_NAME, tok);
  SYMBOL_TABLE_NAME (prod) = $2;
  EXPRESSION_TYPE (prod) = $1;
  NUMERIC_TYPE (prod) =
    NUMERIC_TYPE (( (struct prod_token_parm_item *)EXPRESSION_TYPE (prod)));
  ensure_not_void (NUMERIC_TYPE (prod), tok);
  if (insert_tree_name (prod))
    {
      YYERROR;
    }
  prod2 = make_production (PROD_PARAMETER, tok);
  VARIABLE (prod2) = prod;
  $$ = prod2;
}
;

function_prototype:
storage typename NAME LEFT_PARENTHESIS parameters_opt RIGHT_PARENTHESIS SEMICOLON {
  struct prod_token_parm_item *tok;
  struct prod_token_parm_item *prod;
  struct prod_token_parm_item *type;
  struct prod_token_parm_item *first_parms;
  struct prod_token_parm_item *last_parms;
  struct prod_token_parm_item *this_parms;
  struct prod_token_parm_item *this_parm;
  struct prod_token_parm_item *this_parm_var;
  tok = $3;
  prod = make_production (PROD_FUNCTION_NAME, $3);
  SYMBOL_TABLE_NAME (prod) = $3;
  EXPRESSION_TYPE (prod) = $2;
  NUMERIC_TYPE (prod) =
    NUMERIC_TYPE (( (struct prod_token_parm_item *)EXPRESSION_TYPE (prod)));
  PARAMETERS (prod) = reverse_prod_list ($5);
  insert_tree_name (prod);
  STORAGE_CLASS_TOKEN (prod) = $1;
  set_storage (prod);
  switch (STORAGE_CLASS (prod))
    {
    case STATIC_STORAGE:
    case EXTERNAL_DEFINITION_STORAGE:
    case EXTERNAL_REFERENCE_STORAGE:
      break;

    case AUTOMATIC_STORAGE:
      error ("%Hfunction %q.*s cannot be automatic",
	     &tok->tp.tok.location, tok->tp.tok.length, tok->tp.tok.chars);
      YYERROR;
      break;

    default:
      gcc_unreachable ();
    }
  type = EXPRESSION_TYPE (prod);
  /* Create a parameter list in a non-front end specific format.  */
  for (first_parms = NULL, last_parms = NULL, this_parm = PARAMETERS (prod);
       this_parm;
       this_parm = this_parm->tp.pro.next)
    {
      gcc_assert (this_parm->category == production_category);
      this_parm_var = VARIABLE (this_parm);

      gcc_assert (this_parm_var);
      gcc_assert (this_parm_var->category == production_category);
      gcc_assert (this_parm_var->tp.pro.main_token);

      this_parms = my_malloc (sizeof (struct prod_token_parm_item));

      this_parms->tp.par.variable_name =
	this_parm_var->tp.pro.main_token->tp.tok.chars;
      this_parms->category = parameter_category;
      this_parms->type = NUMERIC_TYPE
        (( (struct prod_token_parm_item *)EXPRESSION_TYPE (this_parm_var)));
      if (last_parms)
        {
          last_parms->tp.par.next = this_parms;
          last_parms = this_parms;
        }
      else
        {
          first_parms = this_parms;
          last_parms = this_parms;
        }
      this_parms->tp.par.where_to_put_var_tree =
        & (((struct prod_token_parm_item *)VARIABLE (this_parm))->tp.pro.code);
    }
  FIRST_PARMS (prod) = first_parms;

  prod->tp.pro.code =
    tree_code_create_function_prototype (tok->tp.tok.chars,
					 STORAGE_CLASS (prod),
					 NUMERIC_TYPE (type),
					 first_parms, tok->tp.tok.location);

#ifdef ENABLE_CHECKING
  /* Check all the parameters have code.  */
  for (this_parm = PARAMETERS (prod);
       this_parm;
       this_parm = this_parm->tp.pro.next)
    {
      gcc_assert ((struct prod_token_parm_item *)VARIABLE (this_parm));
      gcc_assert (((struct prod_token_parm_item *)
		   VARIABLE (this_parm))->tp.pro.code);
    }
#endif
}
;

function:
NAME LEFT_BRACE {
  struct prod_token_parm_item *proto;
  struct prod_token_parm_item search_prod;
  struct prod_token_parm_item *tok;
  tok = $1;
  SYMBOL_TABLE_NAME ((&search_prod)) = tok;
  search_prod.category = token_category;
  current_function = proto = lookup_tree_name (&search_prod);
  if (!proto)
    {
      error ("%Hno prototype found for %q.*s", &tok->tp.tok.location,
	     tok->tp.tok.length, tok->tp.tok.chars);
      YYERROR;
    }

  gcc_assert (proto->tp.pro.code);

  tree_code_create_function_initial (proto->tp.pro.code, tok->tp.tok.location);
}

variable_defs_opt statements_opt RIGHT_BRACE {
  struct prod_token_parm_item *tok;
  tok = $1;
  tree_code_create_function_wrapup (tok->tp.tok.location);
  current_function = NULL;
}
;

variable_defs_opt:
/* Nil.  */ {
  $$ = 0;
}
|variable_defs {
  $$ = $1;
}
;

statements_opt:
/* Nil.  */ {
  $$ = 0;
}
|statements {
  $$ = $1;
}
;

variable_defs:
variable_def {
  /* Nothing to do.  */
}
|variable_defs variable_def {
  /* Nothing to do.  */
}
;

typename:
INT {
  struct prod_token_parm_item *tok;
  struct prod_token_parm_item *prod;
  tok = $1;
  prod = make_production (PROD_TYPE_NAME, tok);
  NUMERIC_TYPE (prod) = SIGNED_INT;
  prod->tp.pro.code = tree_code_get_type (NUMERIC_TYPE (prod));
  $$ = prod;
}
|UNSIGNED INT {
  struct prod_token_parm_item *tok;
  struct prod_token_parm_item *prod;
  tok = $1;
  prod = make_production (PROD_TYPE_NAME, tok);
  NUMERIC_TYPE (prod) = UNSIGNED_INT;
  prod->tp.pro.code = tree_code_get_type (NUMERIC_TYPE (prod));
  $$ = prod;
}
|CHAR {
  struct prod_token_parm_item *tok;
  struct prod_token_parm_item *prod;
  tok = $1;
  prod = make_production (PROD_TYPE_NAME, tok);
  NUMERIC_TYPE (prod) = SIGNED_CHAR;
  prod->tp.pro.code = tree_code_get_type (NUMERIC_TYPE (prod));
  $$ = prod;
}
|UNSIGNED CHAR {
  struct prod_token_parm_item *tok;
  struct prod_token_parm_item *prod;
  tok = $1;
  prod = make_production (PROD_TYPE_NAME, tok);
  NUMERIC_TYPE (prod) = UNSIGNED_CHAR;
  prod->tp.pro.code = tree_code_get_type (NUMERIC_TYPE (prod));
  $$ = prod;
}
|VOID {
  struct prod_token_parm_item *tok;
  struct prod_token_parm_item *prod;
  tok = $1;
  prod = make_production (PROD_TYPE_NAME, tok);
  NUMERIC_TYPE (prod) = VOID_TYPE;
  prod->tp.pro.code = tree_code_get_type (NUMERIC_TYPE (prod));
  $$ = prod;
}
;

parameters_opt:
/* Nothing to do.  */ {
 $$ = 0;
}
| parameters {
 $$ = $1;
}
;

parameters:
parameter {
  /* Nothing to do.  */
  $$ = $1;
}
|parameters COMMA parameter {
  struct prod_token_parm_item *prod1;
  prod1 = $3;
  prod1->tp.pro.next = $1; /* Insert in reverse order.  */
  $$ = prod1;
}
;

statements:
statement {
  /* Nothing to do.  */
}
|statements statement {
  /* Nothing to do.  */
}
;

statement:
expression SEMICOLON {
  struct prod_token_parm_item *exp;
  exp = $1;
  tree_code_output_expression_statement (exp->tp.pro.code,
					 exp->tp.pro.main_token->tp.tok.location);
}
|return SEMICOLON {
  /* Nothing to do.  */
}
|if_statement {
  /* Nothing to do.  */
}
;

if_statement:
IF LEFT_PARENTHESIS expression RIGHT_PARENTHESIS {
  struct prod_token_parm_item *tok;
  struct prod_token_parm_item *exp;
  tok = $1;
  exp = $3;
  ensure_not_void (NUMERIC_TYPE (exp), exp->tp.pro.main_token);
  tree_code_if_start (exp->tp.pro.code, tok->tp.tok.location);
}
LEFT_BRACE variable_defs_opt statements_opt RIGHT_BRACE {
  /* Just let the statements flow.  */
}
ELSE {
  struct prod_token_parm_item *tok;
  tok = $1;
  tree_code_if_else (tok->tp.tok.location);
}
LEFT_BRACE variable_defs_opt statements_opt RIGHT_BRACE {
  struct prod_token_parm_item *tok;
  tok = $1;
  tree_code_if_end (tok->tp.tok.location);
}
;


return:
tl_RETURN expression_opt {
  struct prod_token_parm_item *type_prod;
  struct prod_token_parm_item *ret_tok = $1;
  struct prod_token_parm_item *exp = $2;

  type_prod = EXPRESSION_TYPE (current_function);
  if (NUMERIC_TYPE (type_prod) == VOID_TYPE)
    if (exp == NULL)
      tree_code_generate_return (type_prod->tp.pro.code, NULL);
    else
      {
	warning (0, "%Hredundant expression in return",
		 &ret_tok->tp.tok.location);
        tree_code_generate_return (type_prod->tp.pro.code, NULL);
       }
  else
    if (exp == NULL)
	error ("%Hexpression missing in return", &ret_tok->tp.tok.location);
    else
      {
        /* Check same type.  */
        if (check_type_match (NUMERIC_TYPE (type_prod), exp))
          {
	    gcc_assert (type_prod->tp.pro.code);
	    gcc_assert (exp->tp.pro.code);

            /* Generate the code. */
            tree_code_generate_return (type_prod->tp.pro.code,
				       exp->tp.pro.code);
          }
      }
}
;

expression_opt:
/* Nil.  */ {
  $$ = 0;
}
|expression {
  struct prod_token_parm_item *exp;
  exp = $1;
  gcc_assert (exp->tp.pro.code);

  $$ = $1;
}
;

expression:
INTEGER {
  $$ = make_integer_constant ($1);
}
|variable_ref {
  $$ = $1;
}
|expression tl_PLUS expression {
  struct prod_token_parm_item *tok = $2;
  struct prod_token_parm_item *op1 = $1;
  struct prod_token_parm_item *op2 = $3;
  int type_code = get_common_type (op1, op2);
  if (!type_code)
    YYERROR;
  $$ = make_plus_expression (tok, op1, op2, type_code, EXP_PLUS);
}
|expression tl_MINUS expression %prec tl_PLUS {
  struct prod_token_parm_item *tok = $2;
  struct prod_token_parm_item *op1 = $1;
  struct prod_token_parm_item *op2 = $3;
  int type_code = get_common_type (op1, op2);
  if (!type_code)
    YYERROR;
  $$ = make_plus_expression (tok, op1, op2, type_code, EXP_MINUS);
}
|expression EQUALS expression {
  struct prod_token_parm_item *tok = $2;
  struct prod_token_parm_item *op1 = $1;
  struct prod_token_parm_item *op2 = $3;
  int type_code = NUMERIC_TYPE (op1);
  if (!type_code)
    YYERROR;
  $$ = make_plus_expression
     (tok, op1, op2, type_code, EXP_EQUALS);
}
|variable_ref ASSIGN expression {
  struct prod_token_parm_item *tok = $2;
  struct prod_token_parm_item *op1 = $1;
  struct prod_token_parm_item *op2 = $3;
  int type_code = NUMERIC_TYPE (op1);
  if (!type_code)
    YYERROR;
  $$ = make_plus_expression
     (tok, op1, op2, type_code, EXP_ASSIGN);
}
|function_invocation {
  $$ = $1;
}
;

function_invocation:
NAME LEFT_PARENTHESIS expressions_with_commas_opt RIGHT_PARENTHESIS {
  struct prod_token_parm_item *prod;
  struct prod_token_parm_item *tok;
  struct prod_token_parm_item search_prod;
  struct prod_token_parm_item *proto;
  struct prod_token_parm_item *exp;
  struct prod_token_parm_item *exp_proto;
  struct prod_token_parm_item *var;
  int exp_proto_count;
  int exp_count;
  tree parms;
  tree type;

  tok = $1;
  prod = make_production (PROD_FUNCTION_INVOCATION, tok);
  SYMBOL_TABLE_NAME (prod) = tok;
  PARAMETERS (prod) = reverse_prod_list ($3);
  SYMBOL_TABLE_NAME ((&search_prod)) = tok;
  search_prod.category = token_category;
  proto = lookup_tree_name (&search_prod);
  if (!proto)
    {
      error ("%Hfunction prototype not found for %q.*s",
	     &tok->tp.tok.location, tok->tp.tok.length, tok->tp.tok.chars);
      YYERROR;
    }
  EXPRESSION_TYPE (prod) = EXPRESSION_TYPE (proto);
  NUMERIC_TYPE (prod) = NUMERIC_TYPE (proto);
  /* Count the expressions and ensure they match the prototype.  */
  for (exp_proto_count = 0, exp_proto = PARAMETERS (proto);
       exp_proto; exp_proto = exp_proto->tp.pro.next)
    exp_proto_count++;

  for (exp_count = 0, exp = PARAMETERS (prod); exp; exp = exp->tp.pro.next)
    exp_count++;

  if (exp_count !=  exp_proto_count)
    {
      error ("%Hexpression count mismatch %q.*s with prototype",
	     &tok->tp.tok.location, tok->tp.tok.length, tok->tp.tok.chars);
      YYERROR;
    }
  parms = tree_code_init_parameters ();
  for (exp_proto = PARAMETERS (proto), exp = PARAMETERS (prod);
       exp_proto;
       exp = exp->tp.pro.next, exp_proto = exp_proto->tp.pro.next)
    {
      gcc_assert (exp);
      gcc_assert (exp_proto);
      gcc_assert (exp->tp.pro.code);

      var = VARIABLE (exp_proto);

      gcc_assert (var);
      gcc_assert (var->tp.pro.code);

      parms = tree_code_add_parameter (parms, var->tp.pro.code,
                                       exp->tp.pro.code);
    }
  type = tree_code_get_type (NUMERIC_TYPE (prod));
  prod->tp.pro.code = tree_code_get_expression (EXP_FUNCTION_INVOCATION, type,
                                                proto->tp.pro.code,
						nreverse (parms),
                                                NULL, tok->tp.tok.location);
  $$ = prod;
}
;

expressions_with_commas_opt:
/* Nil.  */ {
$$ = 0
}
|expressions_with_commas { $$ = $1 }
;

expressions_with_commas:
expression {
  struct prod_token_parm_item *exp;
  exp = $1;
  ensure_not_void (NUMERIC_TYPE (exp), exp->tp.pro.main_token);
  $$ = $1;
}
|expressions_with_commas COMMA expression {
  struct prod_token_parm_item *exp;
  exp = $3;
  ensure_not_void (NUMERIC_TYPE (exp), exp->tp.pro.main_token);
  exp->tp.pro.next = $1; /* Reverse order.  */
  $$ = exp;
}
;

variable_ref:
NAME {
  struct prod_token_parm_item search_prod;
  struct prod_token_parm_item *prod;
  struct prod_token_parm_item *symbol_table_entry;
  struct prod_token_parm_item *tok;
  tree type;

  tok = $1;
  SYMBOL_TABLE_NAME ((&search_prod)) = tok;
  search_prod.category = token_category;
  symbol_table_entry = lookup_tree_name (&search_prod);
  if (!symbol_table_entry)
    {
      error ("%Hvariable %q.*s not defined",
	     &tok->tp.tok.location, tok->tp.tok.length, tok->tp.tok.chars);
      YYERROR;
    }

  prod = make_production (PROD_VARIABLE_REFERENCE_EXPRESSION, tok);
  NUMERIC_TYPE (prod) = NUMERIC_TYPE (symbol_table_entry);
  type = tree_code_get_type (NUMERIC_TYPE (prod));
  if (!NUMERIC_TYPE (prod))
    YYERROR;
  OP1 (prod) = $1;

  prod->tp.pro.code =
    tree_code_get_expression (EXP_REFERENCE, type,
			      symbol_table_entry->tp.pro.code, NULL, NULL,
			      tok->tp.tok.location);
  $$ = prod;
}
;

init_opt:
/* Nil.  */ {
  $$ = 0;
}
|init {
  /* Pass the initialization value up.  */
  $$ = $1;
};

init:
ASSIGN init_element {
  $$ = $2;
}
;

init_element:
INTEGER {
  $$ = make_integer_constant ($1);
}
;

%%

/* Print a token VALUE to file FILE.  Ignore TYPE which is the token
   type. */

void
print_token (FILE *file, unsigned int type ATTRIBUTE_UNUSED, YYSTYPE value)
{
  struct prod_token_parm_item *tok;
  unsigned int  ix;

  tok  =  value;
  fprintf (file, "%d \"", LOCATION_LINE (tok->tp.tok.location));
  for (ix  =  0; ix < tok->tp.tok.length; ix++)
    fprintf (file, "%c", tok->tp.tok.chars[ix]);

  fprintf (file, "\"");
}

/* Output a message ERROR_MESSAGE from the parser.  */
static void
yyerror (const char *error_message)
{
  struct prod_token_parm_item *tok;

  tok = yylval;
  if (tok)
    error ("%H%s", &tok->tp.tok.location, error_message);
  else
    error ("%s", error_message);
}

/* Reverse the order of a token list, linked by parse_next, old first
   token is OLD_FIRST.  */

static struct prod_token_parm_item*
reverse_prod_list (struct prod_token_parm_item *old_first)
{
  struct prod_token_parm_item *current;
  struct prod_token_parm_item *next;
  struct prod_token_parm_item *prev = NULL;

  current = old_first;
  prev = NULL;

  while (current)
    {
      gcc_assert (current->category == production_category);

      next = current->tp.pro.next;
      current->tp.pro.next = prev;
      prev = current;
      current = next;
    }
  return prev;
}

/* Ensure TYPE is not VOID. Use NAME as the token for the error location.  */

static void
ensure_not_void (unsigned int type, struct prod_token_parm_item* name)
{
  if (type == VOID_TYPE)
    error ("%Htype must not be void in this context",
	   &name->tp.tok.location);
}

/* Check TYPE1 and TYPE2 which are integral types.  Return the lowest
   common type (min is signed int).  */

static int
get_common_type (struct prod_token_parm_item *type1,
		 struct prod_token_parm_item *type2)
{
  if (NUMERIC_TYPE (type1) == UNSIGNED_INT)
    return UNSIGNED_INT;
  if (NUMERIC_TYPE (type2) == UNSIGNED_INT)
    return UNSIGNED_INT;

  return SIGNED_INT;
}

/* Check type (TYPE_NUM) and expression (EXP) match.  Return the 1 if
   OK else 0.  Must be exact match - same name unless it is an
   integral type.  */

static int
check_type_match (int type_num, struct prod_token_parm_item *exp)
{
  switch (type_num)
    {
    case SIGNED_INT:
    case UNSIGNED_INT:
    case SIGNED_CHAR:
    case UNSIGNED_CHAR:
      switch (NUMERIC_TYPE (exp))
        {
        case SIGNED_INT:
        case UNSIGNED_INT:
        case SIGNED_CHAR:
        case UNSIGNED_CHAR:
          return 1;

        case VOID_TYPE:
        default:
          gcc_unreachable ();
        }
      break;

    case VOID_TYPE:
    default:
      gcc_unreachable ();

    }
}

/* Make a production for an integer constant VALUE.  */

static struct prod_token_parm_item *
make_integer_constant (struct prod_token_parm_item* value)
{
  struct prod_token_parm_item *tok;
  struct prod_token_parm_item *prod;
  tok = value;
  prod = make_production (PROD_INTEGER_CONSTANT, tok);
  if ((tok->tp.tok.chars[0] == (unsigned char)'-')
      || (tok->tp.tok.chars[0] == (unsigned char)'+'))
    NUMERIC_TYPE (prod) = SIGNED_INT;
  else
    NUMERIC_TYPE (prod) = UNSIGNED_INT;
  prod->tp.pro.code = tree_code_get_integer_value (tok->tp.tok.chars,
						   tok->tp.tok.length);
  return prod;
}


/* Build a PROD_PLUS_EXPRESSION.  This is uses for PLUS, MINUS, ASSIGN
   and EQUALS expressions.  */

static struct prod_token_parm_item *
make_plus_expression (struct prod_token_parm_item *tok,
		      struct prod_token_parm_item *op1,
		      struct prod_token_parm_item *op2,
		      int type_code, int prod_code)
{
  struct prod_token_parm_item *prod;
  tree type;

  ensure_not_void (NUMERIC_TYPE (op1), op1->tp.pro.main_token);
  ensure_not_void (NUMERIC_TYPE (op2), op2->tp.pro.main_token);

  prod = make_production (PROD_PLUS_EXPRESSION, tok);

  NUMERIC_TYPE (prod) = type_code;
  type = tree_code_get_type (type_code);

  gcc_assert (type);

  OP1 (prod) = op1;
  OP2 (prod) = op2;

  prod->tp.pro.code = tree_code_get_expression (prod_code, type,
						op1->tp.pro.code,
						op2->tp.pro.code, NULL,
						tok->tp.tok.location);

  return prod;
}


/* Set STORAGE_CLASS in PROD according to CLASS_TOKEN.  */

static void
set_storage (struct prod_token_parm_item *prod)
{
  struct prod_token_parm_item *stg_class;
  stg_class = STORAGE_CLASS_TOKEN (prod);
  switch (stg_class->type)
    {
    case STATIC:
      STORAGE_CLASS (prod) = STATIC_STORAGE;
      break;

    case AUTOMATIC:
      STORAGE_CLASS (prod) = AUTOMATIC_STORAGE;
      break;

    case EXTERNAL_DEFINITION:
      STORAGE_CLASS (prod) = EXTERNAL_DEFINITION_STORAGE;
      break;

    case EXTERNAL_REFERENCE:
      STORAGE_CLASS (prod) = EXTERNAL_REFERENCE_STORAGE;
      break;

    default:
      gcc_unreachable ();
    }
}

/* Set parse trace.  */

void
treelang_debug (void)
{
  if (option_parser_trace)
    yydebug = 1;
}

#ifdef __XGETTEXT__
/* Depending on the version of Bison used to compile this grammar,
   it may issue generic diagnostics spelled "syntax error" or
   "parse error".  To prevent this from changing the translation
   template randomly, we list all the variants of this particular
   diagnostic here.  Translators: there is no fine distinction
   between diagnostics with "syntax error" in them, and diagnostics
   with "parse error" in them.  It's okay to give them both the same
   translation.  */
const char d1[] = N_("syntax error");
const char d2[] = N_("parse error");
const char d3[] = N_("syntax error; also virtual memory exhausted");
const char d4[] = N_("parse error; also virtual memory exhausted");
const char d5[] = N_("syntax error: cannot back up");
const char d6[] = N_("parse error: cannot back up");
#endif
