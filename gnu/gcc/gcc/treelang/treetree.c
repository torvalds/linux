/* TREELANG Compiler interface to GCC's middle end (treetree.c)
   Called by the parser.

   If you want a working example of how to write a front end to GCC,
   you are in the right place.

   Copyright (C) 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

   This code is based on toy.c written by Richard Kenner.

   It was later modified by Jonathan Bartlett whose changes have all
   been removed (by Tim Josling).

   Various bits and pieces were cloned from the GCC main tree, as
   GCC evolved, for COBOLForGCC, by Tim Josling.

   It was adapted to TREELANG by Tim Josling 2001.

   Updated to function-at-a-time by James A. Morrison, 2004.

   -----------------------------------------------------------------------

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

   -----------------------------------------------------------------------  */

/* Assumption: garbage collection is never called implicitly.  It will
   not be called 'at any time' when short of memory.  It will only be
   called explicitly at the end of each function.  This removes the
   need for a *lot* of bother to ensure everything is in the mark trees
   at all times.  */

/* Note, it is OK to use GCC extensions such as long long in a compiler front
   end.  This is because the GCC front ends are built using GCC.   */

/* GCC headers.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "tree-dump.h"
#include "tree-iterator.h"
#include "tree-gimple.h"
#include "function.h"
#include "flags.h"
#include "output.h"
#include "ggc.h"
#include "toplev.h"
#include "varray.h"
#include "langhooks-def.h"
#include "langhooks.h"
#include "target.h"

#include "cgraph.h"

#include "treelang.h"
#include "treetree.h"
#include "opts.h"

extern int option_main;
extern char **file_names;

/* Types expected by gcc's garbage collector.
   These types exist to allow language front-ends to
   add extra information in gcc's parse tree data structure.
   But the treelang front end doesn't use them -- it has
   its own parse tree data structure.
   We define them here only to satisfy gcc's garbage collector.  */

/* Language-specific identifier information.  */

struct lang_identifier GTY(())
{
  struct tree_identifier common;
};

/* Language-specific tree node information.  */

union lang_tree_node 
  GTY((desc ("TREE_CODE (&%h.generic) == IDENTIFIER_NODE")))
{
  union tree_node GTY ((tag ("0"), 
			desc ("tree_node_structure (&%h)"))) 
    generic;
  struct lang_identifier GTY ((tag ("1"))) identifier;
};

/* Language-specific type information.  */

struct lang_type GTY(())
{
  char junk; /* dummy field to ensure struct is not empty */
};

/* Language-specific declaration information.  */

struct lang_decl GTY(())
{
  char junk; /* dummy field to ensure struct is not empty */
};

struct language_function GTY(())
{
  char junk; /* dummy field to ensure struct is not empty */
};

static bool tree_mark_addressable (tree exp);
static tree tree_lang_type_for_size (unsigned precision, int unsignedp);
static tree tree_lang_type_for_mode (enum machine_mode mode, int unsignedp);
static tree tree_lang_unsigned_type (tree type_node);
static tree tree_lang_signed_type (tree type_node);
static tree tree_lang_signed_or_unsigned_type (int unsignedp, tree type);

/* Functions to keep track of the current scope.  */
static void pushlevel (int ignore);
static tree poplevel (int keep, int reverse, int functionbody);
static tree pushdecl (tree decl);
static tree* getstmtlist (void);

/* Langhooks.  */
static tree builtin_function (const char *name, tree type, int function_code,
			      enum built_in_class class,
			      const char *library_name,
			      tree attrs);
extern const struct attribute_spec treelang_attribute_table[];
static tree getdecls (void);
static int global_bindings_p (void);
static void insert_block (tree);

static void tree_push_type_decl (tree id, tree type_node);
static void treelang_expand_function (tree fndecl);

/* The front end language hooks (addresses of code for this front
   end).  These are not really very language-dependent, i.e.
   treelang, C, Mercury, etc. can all use almost the same definitions.  */

#undef LANG_HOOKS_MARK_ADDRESSABLE
#define LANG_HOOKS_MARK_ADDRESSABLE tree_mark_addressable
#undef LANG_HOOKS_SIGNED_TYPE
#define LANG_HOOKS_SIGNED_TYPE tree_lang_signed_type
#undef LANG_HOOKS_UNSIGNED_TYPE
#define LANG_HOOKS_UNSIGNED_TYPE tree_lang_unsigned_type
#undef LANG_HOOKS_SIGNED_OR_UNSIGNED_TYPE
#define LANG_HOOKS_SIGNED_OR_UNSIGNED_TYPE tree_lang_signed_or_unsigned_type
#undef LANG_HOOKS_TYPE_FOR_MODE
#define LANG_HOOKS_TYPE_FOR_MODE tree_lang_type_for_mode
#undef LANG_HOOKS_TYPE_FOR_SIZE
#define LANG_HOOKS_TYPE_FOR_SIZE tree_lang_type_for_size
#undef LANG_HOOKS_PARSE_FILE
#define LANG_HOOKS_PARSE_FILE treelang_parse_file
#undef LANG_HOOKS_ATTRIBUTE_TABLE
#define LANG_HOOKS_ATTRIBUTE_TABLE treelang_attribute_table

#undef LANG_HOOKS_CALLGRAPH_EXPAND_FUNCTION
#define LANG_HOOKS_CALLGRAPH_EXPAND_FUNCTION treelang_expand_function

/* #undef LANG_HOOKS_TYPES_COMPATIBLE_P
#define LANG_HOOKS_TYPES_COMPATIBLE_P hook_bool_tree_tree_true
*/
/* Hook routines and data unique to treelang.  */

#undef LANG_HOOKS_INIT
#define LANG_HOOKS_INIT treelang_init
#undef LANG_HOOKS_NAME
#define LANG_HOOKS_NAME	"GNU treelang"
#undef LANG_HOOKS_FINISH
#define LANG_HOOKS_FINISH		treelang_finish
#undef LANG_HOOKS_INIT_OPTIONS
#define LANG_HOOKS_INIT_OPTIONS  treelang_init_options
#undef LANG_HOOKS_HANDLE_OPTION
#define LANG_HOOKS_HANDLE_OPTION treelang_handle_option
const struct lang_hooks lang_hooks = LANG_HOOKS_INITIALIZER;

/* Tree code type/name/code tables.  */

#define DEFTREECODE(SYM, NAME, TYPE, LENGTH) TYPE,

const enum tree_code_class tree_code_type[] = {
#include "tree.def"
  tcc_exceptional
};
#undef DEFTREECODE

#define DEFTREECODE(SYM, NAME, TYPE, LENGTH) LENGTH,

const unsigned char tree_code_length[] = {
#include "tree.def"
  0
};
#undef DEFTREECODE

#define DEFTREECODE(SYM, NAME, TYPE, LEN) NAME,

const char *const tree_code_name[] = {
#include "tree.def"
  "@@dummy"
};
#undef DEFTREECODE

/* Number of bits in int and char - accessed by front end.  */

unsigned int tree_code_int_size = SIZEOF_INT * HOST_BITS_PER_CHAR;

unsigned int tree_code_char_size = HOST_BITS_PER_CHAR;

/* Return the tree stuff for this type TYPE_NUM.  */

tree
tree_code_get_type (int type_num)
{
  switch (type_num)
    {
    case SIGNED_CHAR:
      return signed_char_type_node;

    case UNSIGNED_CHAR:
      return unsigned_char_type_node;

    case SIGNED_INT:
      return integer_type_node;

    case UNSIGNED_INT:
      return unsigned_type_node;

    case VOID_TYPE:
      return void_type_node;

    default:
      gcc_unreachable ();
    }
}

/* Output the code for the start of an if statement.  The test
   expression is EXP (true if not zero), and the stmt occurred at line
   LINENO in file FILENAME.  */

void
tree_code_if_start (tree exp, location_t loc)
{
  tree cond_exp, cond;
  cond_exp = fold_build2 (NE_EXPR, boolean_type_node, exp,
			  build_int_cst (TREE_TYPE (exp), 0));
  SET_EXPR_LOCATION (cond_exp, loc);
  cond = build3 (COND_EXPR, void_type_node, cond_exp, NULL_TREE,
                 NULL_TREE);
  SET_EXPR_LOCATION (cond, loc);
  append_to_statement_list_force (cond, getstmtlist ());
  pushlevel (0);
}

/* Output the code for the else of an if statement.  The else occurred
   at line LINENO in file FILENAME.  */

void
tree_code_if_else (location_t loc ATTRIBUTE_UNUSED)
{
  tree stmts = *getstmtlist ();
  tree block = poplevel (1, 0, 0);
  if (BLOCK_VARS (block))
    {
      tree bindexpr = build3 (BIND_EXPR, void_type_node, BLOCK_VARS (block),
                              stmts, block);
      stmts = alloc_stmt_list ();
      append_to_statement_list (bindexpr, &stmts);
    }

  TREE_OPERAND (STATEMENT_LIST_TAIL (*getstmtlist ())->stmt, 1) = stmts;
  pushlevel (0);
}

/* Output the code for the end_if an if statement.  The end_if (final brace)
   occurred at line LINENO in file FILENAME.  */

void
tree_code_if_end (location_t loc ATTRIBUTE_UNUSED)
{
  tree stmts = *getstmtlist ();
  tree block = poplevel (1, 0, 0);
  if (BLOCK_VARS (block))
    {
       tree bindexpr = build3 (BIND_EXPR, void_type_node, BLOCK_VARS (block),
                               stmts, block);
       stmts = alloc_stmt_list ();
       append_to_statement_list (bindexpr, &stmts);
    }

  TREE_OPERAND (STATEMENT_LIST_TAIL (*getstmtlist ())->stmt, 2) = stmts;
}

/* Create a function.  The prototype name is NAME, storage class is
   STORAGE_CLASS, type of return variable is RET_TYPE, parameter lists
   is PARMS, returns decl for this function.  */

tree
tree_code_create_function_prototype (unsigned char* chars,
				     unsigned int storage_class,
				     unsigned int ret_type,
				     struct prod_token_parm_item* parms,
				     location_t loc)
{

  tree id;
  struct prod_token_parm_item* parm;
  tree type_list = NULL_TREE;
  tree type_node;
  tree fn_type;
  tree fn_decl;
  tree parm_list = NULL_TREE;

  /* Build the type.  */
  id = get_identifier ((const char*)chars);
  for (parm = parms; parm; parm = parm->tp.par.next)
    {
      gcc_assert (parm->category == parameter_category);
      type_node = tree_code_get_type (parm->type);
      type_list = tree_cons (NULL_TREE, type_node, type_list);
    }
  /* Last parm if void indicates fixed length list (as opposed to
     printf style va_* list).  */
  type_list = tree_cons (NULL_TREE, void_type_node, type_list);

  /* The back end needs them in reverse order.  */
  type_list = nreverse (type_list);

  type_node = tree_code_get_type (ret_type);
  fn_type = build_function_type (type_node, type_list);

  id = get_identifier ((const char*)chars);
  fn_decl = build_decl (FUNCTION_DECL, id, fn_type);

  /* Nested functions not supported here.  */
  DECL_CONTEXT (fn_decl) = NULL_TREE;
  DECL_SOURCE_LOCATION (fn_decl) = loc;

  TREE_PUBLIC (fn_decl) = 0;
  DECL_EXTERNAL (fn_decl) = 0;
  TREE_STATIC (fn_decl) = 0;
  switch (storage_class)
    {
    case STATIC_STORAGE:
      break;

    case EXTERNAL_DEFINITION_STORAGE:
      TREE_PUBLIC (fn_decl) = 1;
      break;

    case EXTERNAL_REFERENCE_STORAGE:
      DECL_EXTERNAL (fn_decl) = 1;
      break;

    case AUTOMATIC_STORAGE:
    default:
      gcc_unreachable ();
    }

  /* Make the argument variable decls.  */
  for (parm = parms; parm; parm = parm->tp.par.next)
    {
      tree parm_decl = build_decl (PARM_DECL, get_identifier
                                   ((const char*) (parm->tp.par.variable_name)),
                                   tree_code_get_type (parm->type));

      /* Some languages have different nominal and real types.  */
      DECL_ARG_TYPE (parm_decl) = TREE_TYPE (parm_decl);
      gcc_assert (DECL_ARG_TYPE (parm_decl));
      gcc_assert (fn_decl);
      DECL_CONTEXT (parm_decl) = fn_decl;
      DECL_SOURCE_LOCATION (parm_decl) = loc;
      parm_list = chainon (parm_decl, parm_list);
    }

  /* Back into reverse order as the back end likes them.  */
  parm_list = nreverse (parm_list);

  DECL_ARGUMENTS (fn_decl) = parm_list;

  /* Save the decls for use when the args are referred to.  */
  for (parm = parms; parm_list;
       parm_list = TREE_CHAIN (parm_list),
	parm = parm->tp.par.next)
    {
      gcc_assert (parm); /* Too few.  */
      *parm->tp.par.where_to_put_var_tree = parm_list;
    }
  gcc_assert (!parm); /* Too many.  */

  /* Process declaration of function defined elsewhere.  */
  rest_of_decl_compilation (fn_decl, 1, 0);

  return fn_decl;
}


/* Output code for start of function; the decl of the function is in
   PREV_SAVED (as created by tree_code_create_function_prototype),
   the function is at line number LINENO in file FILENAME.  The
   parameter details are in the lists PARMS. Returns nothing.  */

void
tree_code_create_function_initial (tree prev_saved,
				   location_t loc)
{
  tree fn_decl;
  tree resultdecl;

  fn_decl = prev_saved;
  gcc_assert (fn_decl);

  /* Output message if not -quiet.  */
  announce_function (fn_decl);

  /* This has something to do with forcing output also.  */
  pushdecl (fn_decl);

  /* Set current function for error msgs etc.  */
  current_function_decl = fn_decl;
  DECL_INITIAL (fn_decl) = error_mark_node;

  DECL_SOURCE_LOCATION (fn_decl) = loc;

  /* Create a DECL for the functions result.  */
  resultdecl =
    build_decl (RESULT_DECL, NULL_TREE, TREE_TYPE (TREE_TYPE (fn_decl)));
  DECL_CONTEXT (resultdecl) = fn_decl;
  DECL_ARTIFICIAL (resultdecl) = 1;
  DECL_IGNORED_P (resultdecl) = 1;
  DECL_SOURCE_LOCATION (resultdecl) = loc;
  DECL_RESULT (fn_decl) = resultdecl;

  /* Create a new level at the start of the function.  */

  pushlevel (0);

  TREE_STATIC (fn_decl) = 1;
}

/* Wrapup a function contained in file FILENAME, ending at line LINENO.  */
void
tree_code_create_function_wrapup (location_t loc)
{
  tree block;
  tree fn_decl;
  tree stmts = *getstmtlist ();

  fn_decl = current_function_decl;

  /* Pop the level.  */

  block = poplevel (1, 0, 1);

  /* And attach it to the function.  */

  DECL_SAVED_TREE (fn_decl) = build3 (BIND_EXPR, void_type_node,
                                      BLOCK_VARS (block),
			              stmts, block);

  allocate_struct_function (fn_decl);
  cfun->function_end_locus = loc;

  /* Dump the original tree to a file.  */
  dump_function (TDI_original, fn_decl);

  /* Convert current function to GIMPLE for the middle end.  */
  gimplify_function_tree (fn_decl);
  dump_function (TDI_generic, fn_decl);

  /* We are not inside of any scope now.  */
  current_function_decl = NULL_TREE;
  cfun = NULL;

  /* Pass the current function off to the middle end.  */
  (void)cgraph_node (fn_decl);
  cgraph_finalize_function (fn_decl, false);
}

/* Create a variable.

   The storage class is STORAGE_CLASS (eg LOCAL).
   The name is CHARS/LENGTH.
   The type is EXPRESSION_TYPE (eg UNSIGNED_TYPE).
   The init tree is INIT.  */

tree
tree_code_create_variable (unsigned int storage_class,
			   unsigned char* chars,
			   unsigned int length,
			   unsigned int expression_type,
			   tree init,
			   location_t loc)
{
  tree var_type;
  tree var_id;
  tree var_decl;

  /* 1. Build the type.  */
  var_type = tree_code_get_type (expression_type);

  /* 2. Build the name.  */
  gcc_assert (chars[length] == 0); /* Should be null terminated.  */

  var_id = get_identifier ((const char*)chars);

  /* 3. Build the decl and set up init.  */
  var_decl = build_decl (VAR_DECL, var_id, var_type);

  /* 3a. Initialization.  */
  if (init)
    DECL_INITIAL (var_decl) = fold_convert (var_type, init);
  else
    DECL_INITIAL (var_decl) = NULL_TREE;

  gcc_assert (TYPE_SIZE (var_type) != 0); /* Did not calculate size.  */

  DECL_CONTEXT (var_decl) = current_function_decl;

  DECL_SOURCE_LOCATION (var_decl) = loc;

  DECL_EXTERNAL (var_decl) = 0;
  TREE_PUBLIC (var_decl) = 0;
  TREE_STATIC (var_decl) = 0;
  /* Set the storage mode and whether only visible in the same file.  */
  switch (storage_class)
    {
    case STATIC_STORAGE:
      TREE_STATIC (var_decl) = 1;
      break;

    case AUTOMATIC_STORAGE:
      break;

    case EXTERNAL_DEFINITION_STORAGE:
      TREE_PUBLIC (var_decl) = 1;
      break;

    case EXTERNAL_REFERENCE_STORAGE:
      DECL_EXTERNAL (var_decl) = 1;
      break;

    default:
      gcc_unreachable ();
    }

  TYPE_NAME (TREE_TYPE (var_decl)) = TYPE_NAME (var_type);
  return pushdecl (copy_node (var_decl));
}


/* Generate code for return statement.  Type is in TYPE, expression
   is in EXP if present.  */

void
tree_code_generate_return (tree type, tree exp)
{
  tree setret;
#ifdef ENABLE_CHECKING
  tree param;

  for (param = DECL_ARGUMENTS (current_function_decl);
       param;
       param = TREE_CHAIN (param))
    gcc_assert (DECL_CONTEXT (param) == current_function_decl);
#endif

  if (exp && TREE_TYPE (TREE_TYPE (current_function_decl)) != void_type_node)
    {
      setret = fold_build2 (MODIFY_EXPR, type, 
                            DECL_RESULT (current_function_decl),
                            fold_convert (type, exp));
      TREE_SIDE_EFFECTS (setret) = 1;
      TREE_USED (setret) = 1;
      setret = build1 (RETURN_EXPR, type, setret);
      /* Use EXPR_LOCUS so we don't lose any information about the file we
	 are compiling.  */
      SET_EXPR_LOCUS (setret, EXPR_LOCUS (exp));
    }
   else
     setret = build1 (RETURN_EXPR, type, NULL_TREE);

   append_to_statement_list_force (setret, getstmtlist ());
}


/* Output the code for this expression statement CODE.  */

void
tree_code_output_expression_statement (tree code, location_t loc)
{
  /* Output the line number information.  */
  SET_EXPR_LOCATION (code, loc);
  TREE_USED (code) = 1;
  TREE_SIDE_EFFECTS (code) = 1;
  /* put CODE into the code list.  */
  append_to_statement_list_force (code, getstmtlist ());
}

/* Return a tree for a constant integer value in the token TOK.  No
   size checking is done.  */

tree
tree_code_get_integer_value (unsigned char* chars, unsigned int length)
{
  long long int val = 0;
  unsigned int ix;
  unsigned int start = 0;
  int negative = 1;
  switch (chars[0])
    {
    case (unsigned char)'-':
      negative = -1;
      start = 1;
      break;

    case (unsigned char)'+':
      start = 1;
      break;

    default:
      break;
    }
  for (ix = start; ix < length; ix++)
    val = val * 10 + chars[ix] - (unsigned char)'0';
  val = val*negative;
  return build_int_cst_wide (start == 1 ?
				integer_type_node : unsigned_type_node,
			     val & 0xffffffff, (val >> 32) & 0xffffffff);
}

/* Return the tree for an expression, type EXP_TYPE (see treetree.h)
   with tree type TYPE and with operands1 OP1, OP2 (maybe), OP3 (maybe).  */
tree
tree_code_get_expression (unsigned int exp_type,
                          tree type, tree op1, tree op2,
			  tree op3 ATTRIBUTE_UNUSED,
			  location_t loc)
{
  tree ret1;
  int operator;

  switch (exp_type)
    {
    case EXP_ASSIGN:
      gcc_assert (op1 && op2);
      operator = MODIFY_EXPR;
      ret1 = fold_build2 (operator, void_type_node, op1,
                          fold_convert (TREE_TYPE (op1), op2));

      break;

    case EXP_PLUS:
      operator = PLUS_EXPR;
      goto binary_expression;

    case EXP_MINUS:
      operator = MINUS_EXPR;
      goto binary_expression;

    case EXP_EQUALS:
      operator = EQ_EXPR;
      goto binary_expression;

    /* Expand a binary expression.  Ensure the operands are the right type.  */
    binary_expression:
      gcc_assert (op1 && op2);
      ret1  =  fold_build2 (operator, type,
			    fold_convert (type, op1),
			    fold_convert (type, op2));
      break;

      /* Reference to a variable.  This is dead easy, just return the
         decl for the variable.  If the TYPE is different than the
         variable type, convert it.  However, to keep accurate location
	 information we wrap it in a NOP_EXPR is is easily stripped.  */
    case EXP_REFERENCE:
      gcc_assert (op1);
      TREE_USED (op1) = 1;
      if (type == TREE_TYPE (op1))
        ret1 = build1 (NOP_EXPR, type, op1);
      else
        ret1 = fold_convert (type, op1);
      break;

    case EXP_FUNCTION_INVOCATION:
      gcc_assert (op1);
      gcc_assert(TREE_TYPE (TREE_TYPE (op1)) == type);
      TREE_USED (op1) = 1;
      ret1 = build_function_call_expr(op1, op2);
      break;

    default:
      gcc_unreachable ();
    }

  /* Declarations already have a location and constants can be shared so they
     shouldn't a location set on them.  */
  if (! DECL_P (ret1) && ! TREE_CONSTANT (ret1))
    SET_EXPR_LOCATION (ret1, loc);
  return ret1;
}

/* Init parameter list and return empty list.  */

tree
tree_code_init_parameters (void)
{
  return NULL_TREE;
}

/* Add a parameter EXP whose expression type is EXP_PROTO to list
   LIST, returning the new list.  */

tree
tree_code_add_parameter (tree list, tree proto_exp, tree exp)
{
  tree new_exp;
  new_exp = tree_cons (NULL_TREE,
                       fold_convert (TREE_TYPE (proto_exp),
				     exp), NULL_TREE);
  if (!list)
    return new_exp;
  return chainon (new_exp, list);
}

/* Get a stringpool entry for a string S of length L.  This is needed
   because the GTY routines don't mark strings, forcing you to put
   them into stringpool, which is never freed.  */

const char*
get_string (const char *s, size_t l)
{
  tree t;
  t = get_identifier_with_length (s, l);
  return IDENTIFIER_POINTER(t);
}
  
/* Save typing debug_tree all the time. Dump a tree T pretty and
   concise.  */

void dt (tree t);

void
dt (tree t)
{
  debug_tree (t);
}

/* Routines Expected by gcc:  */

/* These are used to build types for various sizes.  The code below
   is a simplified version of that of GNAT.  */

#ifndef MAX_BITS_PER_WORD
#define MAX_BITS_PER_WORD  BITS_PER_WORD
#endif

/* This variable keeps a table for types for each precision so that we only 
   allocate each of them once. Signed and unsigned types are kept separate.  */
static GTY(()) tree signed_and_unsigned_types[MAX_BITS_PER_WORD + 1][2];

/* Mark EXP saying that we need to be able to take the
   address of it; it should not be allocated in a register.
   Value is 1 if successful.  
   
   This implementation was copied from c-decl.c. */

static bool
tree_mark_addressable (tree exp)
{
  register tree x = exp;
  while (1)
    switch (TREE_CODE (x))
      {
      case COMPONENT_REF:
      case ADDR_EXPR:
      case ARRAY_REF:
      case REALPART_EXPR:
      case IMAGPART_EXPR:
	x = TREE_OPERAND (x, 0);
	break;
  
      case CONSTRUCTOR:
	TREE_ADDRESSABLE (x) = 1;
	return 1;

      case VAR_DECL:
      case CONST_DECL:
      case PARM_DECL:
      case RESULT_DECL:
	if (DECL_REGISTER (x) && !TREE_ADDRESSABLE (x)
	    && DECL_NONLOCAL (x))
	  {
	    if (TREE_PUBLIC (x))
	      {
		error ("Global register variable %qD used in nested function.",
		       x);
		return 0;
	      }
	    pedwarn ("Register variable %qD used in nested function.", x);
	  }
	else if (DECL_REGISTER (x) && !TREE_ADDRESSABLE (x))
	  {
	    if (TREE_PUBLIC (x))
	      {
		error ("Address of global register variable %qD requested.",
		       x);
		return 0;
	      }

	    pedwarn ("Address of register variable %qD requested.", x);
	  }

	/* drops in */
      case FUNCTION_DECL:
	TREE_ADDRESSABLE (x) = 1;

      default:
	return 1;
    }
}
  
/* Return an integer type with the number of bits of precision given by  
   PRECISION.  UNSIGNEDP is nonzero if the type is unsigned; otherwise
   it is a signed type.  */
  
static tree
tree_lang_type_for_size (unsigned precision, int unsignedp)
{
  tree t;

  if (precision <= MAX_BITS_PER_WORD
      && signed_and_unsigned_types[precision][unsignedp] != 0)
    return signed_and_unsigned_types[precision][unsignedp];

  if (unsignedp)
    t = signed_and_unsigned_types[precision][1]
      = make_unsigned_type (precision);
  else
    t = signed_and_unsigned_types[precision][0]
      = make_signed_type (precision);
  
  return t;
}

/* Return a data type that has machine mode MODE.  UNSIGNEDP selects
   an unsigned type; otherwise a signed type is returned.  */

static tree
tree_lang_type_for_mode (enum machine_mode mode, int unsignedp)
{
  if (SCALAR_INT_MODE_P (mode))
    return tree_lang_type_for_size (GET_MODE_BITSIZE (mode), unsignedp);
  else
    return NULL_TREE;
}

/* Return the unsigned version of a TYPE_NODE, a scalar type.  */

static tree
tree_lang_unsigned_type (tree type_node)
{
  return tree_lang_type_for_size (TYPE_PRECISION (type_node), 1);
}

/* Return the signed version of a TYPE_NODE, a scalar type.  */

static tree
tree_lang_signed_type (tree type_node)
{
  return tree_lang_type_for_size (TYPE_PRECISION (type_node), 0);
}

/* Return a type the same as TYPE except unsigned or signed according to
   UNSIGNEDP.  */

static tree
tree_lang_signed_or_unsigned_type (int unsignedp, tree type)
{
  if (! INTEGRAL_TYPE_P (type) || TYPE_UNSIGNED (type) == unsignedp)
    return type;
  else
    return tree_lang_type_for_size (TYPE_PRECISION (type), unsignedp);
}

/* These functions and variables deal with binding contours.  We only
   need these functions for the list of PARM_DECLs, but we leave the
   functions more general; these are a simplified version of the
   functions from GNAT.  */

/* For each binding contour we allocate a binding_level structure which records
   the entities defined or declared in that contour. Contours include:

	the global one
	one for each subprogram definition
	one for each compound statement (declare block)

   Binding contours are used to create GCC tree BLOCK nodes.  */

struct binding_level
{
  /* A chain of ..._DECL nodes for all variables, constants, functions,
     parameters and type declarations.  These ..._DECL nodes are chained
     through the TREE_CHAIN field. Note that these ..._DECL nodes are stored
     in the reverse of the order supplied to be compatible with the
     back-end.  */
  tree names;
  /* For each level (except the global one), a chain of BLOCK nodes for all
     the levels that were entered and exited one level down from this one.  */
  tree blocks;

  tree stmts;
  /* The binding level containing this one (the enclosing binding level). */
  struct binding_level *level_chain;
};

/* The binding level currently in effect.  */
static struct binding_level *current_binding_level = NULL;

/* The outermost binding level. This binding level is created when the
   compiler is started and it will exist through the entire compilation.  */
static struct binding_level *global_binding_level;

/* Binding level structures are initialized by copying this one.  */
static struct binding_level clear_binding_level = {NULL, NULL, NULL, NULL };

/* Return non-zero if we are currently in the global binding level.  */

static int
global_bindings_p (void)
{
  return current_binding_level == global_binding_level ? -1 : 0;
}


/* Return the list of declarations in the current level. Note that this list
   is in reverse order (it has to be so for back-end compatibility).  */

static tree
getdecls (void)
{
  return current_binding_level->names;
}

/* Return a STATMENT_LIST for the current block.  */

static tree*
getstmtlist (void)
{
  return &current_binding_level->stmts;
}

/* Enter a new binding level. The input parameter is ignored, but has to be
   specified for back-end compatibility.  */

static void
pushlevel (int ignore ATTRIBUTE_UNUSED)
{
  struct binding_level *newlevel = XNEW (struct binding_level);

  *newlevel = clear_binding_level;

  /* Add this level to the front of the chain (stack) of levels that are
     active.  */
  newlevel->level_chain = current_binding_level;
  current_binding_level = newlevel;
  current_binding_level->stmts = alloc_stmt_list ();
}

/* Exit a binding level.
   Pop the level off, and restore the state of the identifier-decl mappings
   that were in effect when this level was entered.

   If KEEP is nonzero, this level had explicit declarations, so
   and create a "block" (a BLOCK node) for the level
   to record its declarations and subblocks for symbol table output.

   If FUNCTIONBODY is nonzero, this level is the body of a function,
   so create a block as if KEEP were set and also clear out all
   label names.

   If REVERSE is nonzero, reverse the order of decls before putting
   them into the BLOCK.  */

static tree
poplevel (int keep, int reverse, int functionbody)
{
  /* Points to a BLOCK tree node. This is the BLOCK node constructed for the
     binding level that we are about to exit and which is returned by this
     routine.  */
  tree block_node = NULL_TREE;
  tree decl_chain;
  tree subblock_chain = current_binding_level->blocks;
  tree subblock_node;

  /* Reverse the list of *_DECL nodes if desired.  Note that the ..._DECL
     nodes chained through the `names' field of current_binding_level are in
     reverse order except for PARM_DECL node, which are explicitly stored in
     the right order.  */
  decl_chain = (reverse) ? nreverse (current_binding_level->names)
			 : current_binding_level->names;

  /* If there were any declarations in the current binding level, or if this
     binding level is a function body, or if there are any nested blocks then
     create a BLOCK node to record them for the life of this function.  */
  if (keep || functionbody)
    block_node = build_block (keep ? decl_chain : 0, subblock_chain, 0, 0);

  /* Record the BLOCK node just built as the subblock its enclosing scope.  */
  for (subblock_node = subblock_chain; subblock_node;
       subblock_node = TREE_CHAIN (subblock_node))
    BLOCK_SUPERCONTEXT (subblock_node) = block_node;

  /* Clear out the meanings of the local variables of this level.  */

  for (subblock_node = decl_chain; subblock_node;
       subblock_node = TREE_CHAIN (subblock_node))
    if (DECL_NAME (subblock_node) != 0)
      /* If the identifier was used or addressed via a local extern decl,  
	 don't forget that fact.   */
      if (DECL_EXTERNAL (subblock_node))
	{
	  if (TREE_USED (subblock_node))
	    TREE_USED (DECL_NAME (subblock_node)) = 1;
	}

  /* Pop the current level.  */
  current_binding_level = current_binding_level->level_chain;

  if (functionbody)
    {
      /* This is the top level block of a function.  */
      DECL_INITIAL (current_function_decl) = block_node;
    }
  else if (block_node)
    {
      current_binding_level->blocks
	= chainon (current_binding_level->blocks, block_node);
    }

  /* If we did not make a block for the level just exited, any blocks made for
     inner levels (since they cannot be recorded as subblocks in that level)
     must be carried forward so they will later become subblocks of something
     else.  */
  else if (subblock_chain)
    current_binding_level->blocks
      = chainon (current_binding_level->blocks, subblock_chain);
  if (block_node)
    TREE_USED (block_node) = 1;

  return block_node;
}

/* Insert BLOCK at the end of the list of subblocks of the
   current binding level.  This is used when a BIND_EXPR is expanded,
   to handle the BLOCK node inside the BIND_EXPR.  */

static void
insert_block (tree block)
{
  TREE_USED (block) = 1;
  current_binding_level->blocks
    = chainon (current_binding_level->blocks, block);
}


/* Records a ..._DECL node DECL as belonging to the current lexical scope.
   Returns the ..._DECL node. */

tree
pushdecl (tree decl)
{
  /* External objects aren't nested, other objects may be.  */
    
  if ((DECL_EXTERNAL (decl)) || (decl==current_function_decl))
    DECL_CONTEXT (decl) = 0;
  else
    DECL_CONTEXT (decl) = current_function_decl;

  /* Put the declaration on the list.  The list of declarations is in reverse
     order. The list will be reversed later if necessary.  This needs to be
     this way for compatibility with the back-end.  */

  TREE_CHAIN (decl) = current_binding_level->names;
  current_binding_level->names = decl;

  /* For the declaration of a type, set its name if it is not already set. */

  if (TREE_CODE (decl) == TYPE_DECL
      && TYPE_NAME (TREE_TYPE (decl)) == 0)
    TYPE_NAME (TREE_TYPE (decl)) = DECL_NAME (decl);

  /* Put automatic variables into the intermediate representation.  */
  if (TREE_CODE (decl) == VAR_DECL && !DECL_EXTERNAL (decl)
      && !TREE_STATIC (decl) && !TREE_PUBLIC (decl))
    tree_code_output_expression_statement (build1 (DECL_EXPR, void_type_node,
                                                   decl),
                                           DECL_SOURCE_LOCATION (decl));
  return decl;
}


static void
tree_push_type_decl(tree id, tree type_node)
{
  tree decl = build_decl (TYPE_DECL, id, type_node);
  TYPE_NAME (type_node) = id;
  pushdecl (decl);
}

#define NULL_BINDING_LEVEL (struct binding_level *) NULL                        

/* Create the predefined scalar types of C,
   and some nodes representing standard constants (0, 1, (void *) 0).
   Initialize the global binding level.
   Make definitions for built-in primitive functions.  */

void
treelang_init_decl_processing (void)
{
  current_function_decl = NULL;
  current_binding_level = NULL_BINDING_LEVEL;
  pushlevel (0);	/* make the binding_level structure for global names */
  global_binding_level = current_binding_level;

  build_common_tree_nodes (flag_signed_char, false);

  /* set standard type names */

  /* Define `int' and `char' last so that they are not overwritten.  */
  tree_push_type_decl (NULL_TREE, intQI_type_node);
  tree_push_type_decl (NULL_TREE, intHI_type_node);
  tree_push_type_decl (NULL_TREE, intSI_type_node);
  tree_push_type_decl (NULL_TREE, intDI_type_node);
#if HOST_BITS_PER_WIDE_INT >= 64
  tree_push_type_decl (NULL_TREE, intTI_type_node);
#endif
  tree_push_type_decl (NULL_TREE, unsigned_intQI_type_node);
  tree_push_type_decl (NULL_TREE, unsigned_intHI_type_node);
  tree_push_type_decl (NULL_TREE, unsigned_intSI_type_node);
  tree_push_type_decl (NULL_TREE, unsigned_intDI_type_node);
#if HOST_BITS_PER_WIDE_INT >= 64
  tree_push_type_decl (NULL_TREE, unsigned_intTI_type_node);
#endif

  tree_push_type_decl (get_identifier ("int"), integer_type_node);
  tree_push_type_decl (get_identifier ("char"), char_type_node);
  tree_push_type_decl (get_identifier ("long int"),
			      long_integer_type_node);
  tree_push_type_decl (get_identifier ("unsigned int"),
			      unsigned_type_node);
  tree_push_type_decl (get_identifier ("long unsigned int"),
			      long_unsigned_type_node);
  tree_push_type_decl (get_identifier ("long long int"),
			      long_long_integer_type_node);
  tree_push_type_decl (get_identifier ("long long unsigned int"),
			      long_long_unsigned_type_node);
  tree_push_type_decl (get_identifier ("short int"),
			      short_integer_type_node);
  tree_push_type_decl (get_identifier ("short unsigned int"),
			      short_unsigned_type_node);
  tree_push_type_decl (get_identifier ("signed char"),
			      signed_char_type_node);
  tree_push_type_decl (get_identifier ("unsigned char"),
			      unsigned_char_type_node);
  size_type_node = make_unsigned_type (POINTER_SIZE);
  tree_push_type_decl (get_identifier ("size_t"), size_type_node);
  set_sizetype (size_type_node);

  build_common_tree_nodes_2 (/* short_double= */ 0);

  tree_push_type_decl (get_identifier ("float"), float_type_node);
  tree_push_type_decl (get_identifier ("double"), double_type_node);
  tree_push_type_decl (get_identifier ("long double"), long_double_type_node);
  tree_push_type_decl (get_identifier ("void"), void_type_node);

  build_common_builtin_nodes ();
  (*targetm.init_builtins) ();

  pedantic_lvalues = pedantic;
}

static tree
handle_attribute (tree *node, tree name, tree ARG_UNUSED (args),
		  int ARG_UNUSED (flags), bool *no_add_attrs)
{
  if (TREE_CODE (*node) == FUNCTION_DECL)
    {
      if (strcmp (IDENTIFIER_POINTER (name), "const") == 0)
	TREE_READONLY (*node) = 1;
      if (strcmp (IDENTIFIER_POINTER (name), "nothrow") == 0)
	TREE_NOTHROW (*node) = 1;
    }
  else
    {
      warning (OPT_Wattributes, "%qD attribute ignored", name);
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

const struct attribute_spec treelang_attribute_table[] =
{
  { "const", 0, 0, true, false, false, handle_attribute },
  { "nothrow", 0, 0, true, false, false, handle_attribute },
  { NULL, 0, 0, false, false, false, NULL },
};

/* Return a definition for a builtin function named NAME and whose data type
   is TYPE.  TYPE should be a function type with argument types.
   FUNCTION_CODE tells later passes how to compile calls to this function.
   See tree.h for its possible values.

   If LIBRARY_NAME is nonzero, use that for DECL_ASSEMBLER_NAME,
   the name to be called if we can't opencode the function.  If
   ATTRS is nonzero, use that for the function's attribute list.

   copied from gcc/c-decl.c
*/

static tree
builtin_function (const char *name, tree type, int function_code,
		  enum built_in_class class, const char *library_name,
		  tree attrs)
{
  tree decl = build_decl (FUNCTION_DECL, get_identifier (name), type);
  DECL_EXTERNAL (decl) = 1;
  TREE_PUBLIC (decl) = 1;
  if (library_name)
    SET_DECL_ASSEMBLER_NAME (decl, get_identifier (library_name));
  pushdecl (decl);
  DECL_BUILT_IN_CLASS (decl) = class;
  DECL_FUNCTION_CODE (decl) = function_code;

  /* Possibly apply some default attributes to this built-in function.  */
  if (attrs)
    decl_attributes (&decl, attrs, ATTR_FLAG_BUILT_IN);
  else
    decl_attributes (&decl, NULL_TREE, 0);

  return decl;
}

/* Treelang expand function langhook.  */

static void
treelang_expand_function (tree fndecl)
{
  /* We have nothing special to do while expanding functions for treelang.  */
  tree_rest_of_compilation (fndecl);
}

#include "debug.h" /* for debug_hooks, needed by gt-treelang-treetree.h */
#include "gt-treelang-treetree.h"
