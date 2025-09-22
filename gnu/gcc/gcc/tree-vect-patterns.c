/* Analysis Utilities for Loop Vectorization.
   Copyright (C) 2006 Free Software Foundation, Inc.
   Contributed by Dorit Nuzman <dorit@il.ibm.com>

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
#include "ggc.h"
#include "tree.h"

#include "target.h"
#include "basic-block.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "timevar.h"
#include "cfgloop.h"
#include "expr.h"
#include "optabs.h"
#include "params.h"
#include "tree-data-ref.h"
#include "tree-vectorizer.h"
#include "recog.h"
#include "toplev.h"

/* Function prototypes */
static void vect_pattern_recog_1 
  (tree (* ) (tree, tree *, tree *), block_stmt_iterator);
static bool widened_name_p (tree, tree, tree *, tree *);

/* Pattern recognition functions  */
static tree vect_recog_widen_sum_pattern (tree, tree *, tree *);
static tree vect_recog_widen_mult_pattern (tree, tree *, tree *);
static tree vect_recog_dot_prod_pattern (tree, tree *, tree *);
static vect_recog_func_ptr vect_vect_recog_func_ptrs[NUM_PATTERNS] = {
	vect_recog_widen_mult_pattern,
	vect_recog_widen_sum_pattern,
	vect_recog_dot_prod_pattern};


/* Function widened_name_p

   Check whether NAME, an ssa-name used in USE_STMT,
   is a result of a type-promotion, such that:
     DEF_STMT: NAME = NOP (name0)
   where the type of name0 (HALF_TYPE) is smaller than the type of NAME. 
*/

static bool
widened_name_p (tree name, tree use_stmt, tree *half_type, tree *def_stmt)
{
  tree dummy;
  loop_vec_info loop_vinfo;
  stmt_vec_info stmt_vinfo;
  tree expr;
  tree type = TREE_TYPE (name);
  tree oprnd0;
  enum vect_def_type dt;
  tree def;

  stmt_vinfo = vinfo_for_stmt (use_stmt);
  loop_vinfo = STMT_VINFO_LOOP_VINFO (stmt_vinfo);

  if (!vect_is_simple_use (name, loop_vinfo, def_stmt, &def, &dt))
    return false;

  if (dt != vect_loop_def
      && dt != vect_invariant_def && dt != vect_constant_def)
    return false;

  if (! *def_stmt)
    return false;

  if (TREE_CODE (*def_stmt) != MODIFY_EXPR)
    return false;

  expr = TREE_OPERAND (*def_stmt, 1);
  if (TREE_CODE (expr) != NOP_EXPR)
    return false;

  oprnd0 = TREE_OPERAND (expr, 0);

  *half_type = TREE_TYPE (oprnd0);
  if (!INTEGRAL_TYPE_P (type) || !INTEGRAL_TYPE_P (*half_type)
      || (TYPE_UNSIGNED (type) != TYPE_UNSIGNED (*half_type))
      || (TYPE_PRECISION (type) < (TYPE_PRECISION (*half_type) * 2)))
    return false;

  if (!vect_is_simple_use (oprnd0, loop_vinfo, &dummy, &dummy, &dt))
    return false;

  if (dt != vect_invariant_def && dt != vect_constant_def
      && dt != vect_loop_def)
    return false;

  return true;
}


/* Function vect_recog_dot_prod_pattern

   Try to find the following pattern:

     type x_t, y_t;
     TYPE1 prod;
     TYPE2 sum = init;
   loop:
     sum_0 = phi <init, sum_1>
     S1  x_t = ...
     S2  y_t = ...
     S3  x_T = (TYPE1) x_t;
     S4  y_T = (TYPE1) y_t;
     S5  prod = x_T * y_T;
     [S6  prod = (TYPE2) prod;  #optional]
     S7  sum_1 = prod + sum_0;

   where 'TYPE1' is exactly double the size of type 'type', and 'TYPE2' is the 
   same size of 'TYPE1' or bigger. This is a special case of a reduction 
   computation.
      
   Input:

   * LAST_STMT: A stmt from which the pattern search begins. In the example,
   when this function is called with S7, the pattern {S3,S4,S5,S6,S7} will be
   detected.

   Output:

   * TYPE_IN: The type of the input arguments to the pattern.

   * TYPE_OUT: The type of the output  of this pattern.

   * Return value: A new stmt that will be used to replace the sequence of
   stmts that constitute the pattern. In this case it will be:
        WIDEN_DOT_PRODUCT <x_t, y_t, sum_0>
*/

static tree
vect_recog_dot_prod_pattern (tree last_stmt, tree *type_in, tree *type_out)
{
  tree stmt, expr;
  tree oprnd0, oprnd1;
  tree oprnd00, oprnd01;
  stmt_vec_info stmt_vinfo = vinfo_for_stmt (last_stmt);
  tree type, half_type;
  tree pattern_expr;
  tree prod_type;

  if (TREE_CODE (last_stmt) != MODIFY_EXPR)
    return NULL;

  expr = TREE_OPERAND (last_stmt, 1);
  type = TREE_TYPE (expr);

  /* Look for the following pattern 
          DX = (TYPE1) X;
          DY = (TYPE1) Y;
          DPROD = DX * DY; 
          DDPROD = (TYPE2) DPROD;
          sum_1 = DDPROD + sum_0;
     In which 
     - DX is double the size of X
     - DY is double the size of Y
     - DX, DY, DPROD all have the same type
     - sum is the same size of DPROD or bigger
     - sum has been recognized as a reduction variable.

     This is equivalent to:
       DPROD = X w* Y;          #widen mult
       sum_1 = DPROD w+ sum_0;  #widen summation
     or
       DPROD = X w* Y;          #widen mult
       sum_1 = DPROD + sum_0;   #summation
   */

  /* Starting from LAST_STMT, follow the defs of its uses in search
     of the above pattern.  */

  if (TREE_CODE (expr) != PLUS_EXPR)
    return NULL;

  if (STMT_VINFO_IN_PATTERN_P (stmt_vinfo))
    {
      /* Has been detected as widening-summation?  */

      stmt = STMT_VINFO_RELATED_STMT (stmt_vinfo);
      expr = TREE_OPERAND (stmt, 1);
      type = TREE_TYPE (expr);
      if (TREE_CODE (expr) != WIDEN_SUM_EXPR)
        return NULL;
      oprnd0 = TREE_OPERAND (expr, 0);
      oprnd1 = TREE_OPERAND (expr, 1);
      half_type = TREE_TYPE (oprnd0);
    }
  else
    {
      tree def_stmt;

      if (STMT_VINFO_DEF_TYPE (stmt_vinfo) != vect_reduction_def)
        return NULL;
      oprnd0 = TREE_OPERAND (expr, 0);
      oprnd1 = TREE_OPERAND (expr, 1);
      if (TYPE_MAIN_VARIANT (TREE_TYPE (oprnd0)) != TYPE_MAIN_VARIANT (type)
          || TYPE_MAIN_VARIANT (TREE_TYPE (oprnd1)) != TYPE_MAIN_VARIANT (type))
        return NULL;
      stmt = last_stmt;

      if (widened_name_p (oprnd0, stmt, &half_type, &def_stmt))
        {
          stmt = def_stmt;
          expr = TREE_OPERAND (stmt, 1);
          oprnd0 = TREE_OPERAND (expr, 0);
        }
      else
        half_type = type;
    }

  /* So far so good. Since last_stmt was detected as a (summation) reduction,
     we know that oprnd1 is the reduction variable (defined by a loop-header
     phi), and oprnd0 is an ssa-name defined by a stmt in the loop body.
     Left to check that oprnd0 is defined by a (widen_)mult_expr  */

  prod_type = half_type;
  stmt = SSA_NAME_DEF_STMT (oprnd0);
  gcc_assert (stmt);
  stmt_vinfo = vinfo_for_stmt (stmt);
  gcc_assert (stmt_vinfo);
  if (STMT_VINFO_DEF_TYPE (stmt_vinfo) != vect_loop_def)
    return NULL;
  expr = TREE_OPERAND (stmt, 1);
  if (TREE_CODE (expr) != MULT_EXPR)
    return NULL;
  if (STMT_VINFO_IN_PATTERN_P (stmt_vinfo))
    {
      /* Has been detected as a widening multiplication?  */

      stmt = STMT_VINFO_RELATED_STMT (stmt_vinfo);
      expr = TREE_OPERAND (stmt, 1);
      if (TREE_CODE (expr) != WIDEN_MULT_EXPR)
        return NULL;
      stmt_vinfo = vinfo_for_stmt (stmt);
      gcc_assert (stmt_vinfo);
      gcc_assert (STMT_VINFO_DEF_TYPE (stmt_vinfo) == vect_loop_def);
      oprnd00 = TREE_OPERAND (expr, 0);
      oprnd01 = TREE_OPERAND (expr, 1);
    }
  else
    {
      tree half_type0, half_type1;
      tree def_stmt;
      tree oprnd0, oprnd1;

      oprnd0 = TREE_OPERAND (expr, 0);
      oprnd1 = TREE_OPERAND (expr, 1);
      if (TYPE_MAIN_VARIANT (TREE_TYPE (oprnd0)) 
				!= TYPE_MAIN_VARIANT (prod_type)
          || TYPE_MAIN_VARIANT (TREE_TYPE (oprnd1)) 
				!= TYPE_MAIN_VARIANT (prod_type))
        return NULL;
      if (!widened_name_p (oprnd0, stmt, &half_type0, &def_stmt))
        return NULL;
      oprnd00 = TREE_OPERAND (TREE_OPERAND (def_stmt, 1), 0);
      if (!widened_name_p (oprnd1, stmt, &half_type1, &def_stmt))
        return NULL;
      oprnd01 = TREE_OPERAND (TREE_OPERAND (def_stmt, 1), 0);
      if (TYPE_MAIN_VARIANT (half_type0) != TYPE_MAIN_VARIANT (half_type1))
        return NULL;
      if (TYPE_PRECISION (prod_type) != TYPE_PRECISION (half_type0) * 2)
	return NULL;
    }

  half_type = TREE_TYPE (oprnd00);
  *type_in = half_type;
  *type_out = type;
  
  /* Pattern detected. Create a stmt to be used to replace the pattern: */
  pattern_expr = build3 (DOT_PROD_EXPR, type, oprnd00, oprnd01, oprnd1);
  if (vect_print_dump_info (REPORT_DETAILS))
    {
      fprintf (vect_dump, "vect_recog_dot_prod_pattern: detected: ");
      print_generic_expr (vect_dump, pattern_expr, TDF_SLIM);
    }
  return pattern_expr;
}


/* Function vect_recog_widen_mult_pattern

   Try to find the following pattern:

     type a_t, b_t;
     TYPE a_T, b_T, prod_T;

     S1  a_t = ;
     S2  b_t = ;
     S3  a_T = (TYPE) a_t;
     S4  b_T = (TYPE) b_t;
     S5  prod_T = a_T * b_T;

   where type 'TYPE' is at least double the size of type 'type'.

   Input:

   * LAST_STMT: A stmt from which the pattern search begins. In the example,
   when this function is called with S5, the pattern {S3,S4,S5} is be detected.

   Output:

   * TYPE_IN: The type of the input arguments to the pattern.

   * TYPE_OUT: The type of the output  of this pattern.

   * Return value: A new stmt that will be used to replace the sequence of
   stmts that constitute the pattern. In this case it will be:
        WIDEN_MULT <a_t, b_t>
*/

static tree
vect_recog_widen_mult_pattern (tree last_stmt ATTRIBUTE_UNUSED, 
			       tree *type_in ATTRIBUTE_UNUSED, 
			       tree *type_out ATTRIBUTE_UNUSED)
{
  /* Yet to be implemented.   */
  return NULL;
}


/* Function vect_recog_widen_sum_pattern

   Try to find the following pattern:

     type x_t; 
     TYPE x_T, sum = init;
   loop:
     sum_0 = phi <init, sum_1>
     S1  x_t = *p;
     S2  x_T = (TYPE) x_t;
     S3  sum_1 = x_T + sum_0;

   where type 'TYPE' is at least double the size of type 'type', i.e - we're 
   summing elements of type 'type' into an accumulator of type 'TYPE'. This is
   a special case of a reduction computation.

   Input:

   * LAST_STMT: A stmt from which the pattern search begins. In the example,
   when this function is called with S3, the pattern {S2,S3} will be detected.
        
   Output:
      
   * TYPE_IN: The type of the input arguments to the pattern.

   * TYPE_OUT: The type of the output of this pattern.

   * Return value: A new stmt that will be used to replace the sequence of
   stmts that constitute the pattern. In this case it will be:
        WIDEN_SUM <x_t, sum_0>
*/

static tree
vect_recog_widen_sum_pattern (tree last_stmt, tree *type_in, tree *type_out)
{
  tree stmt, expr;
  tree oprnd0, oprnd1;
  stmt_vec_info stmt_vinfo = vinfo_for_stmt (last_stmt);
  tree type, half_type;
  tree pattern_expr;

  if (TREE_CODE (last_stmt) != MODIFY_EXPR)
    return NULL;

  expr = TREE_OPERAND (last_stmt, 1);
  type = TREE_TYPE (expr);

  /* Look for the following pattern
          DX = (TYPE) X;
          sum_1 = DX + sum_0;
     In which DX is at least double the size of X, and sum_1 has been
     recognized as a reduction variable.
   */

  /* Starting from LAST_STMT, follow the defs of its uses in search
     of the above pattern.  */

  if (TREE_CODE (expr) != PLUS_EXPR)
    return NULL;

  if (STMT_VINFO_DEF_TYPE (stmt_vinfo) != vect_reduction_def)
    return NULL;

  oprnd0 = TREE_OPERAND (expr, 0);
  oprnd1 = TREE_OPERAND (expr, 1);
  if (TYPE_MAIN_VARIANT (TREE_TYPE (oprnd0)) != TYPE_MAIN_VARIANT (type)
      || TYPE_MAIN_VARIANT (TREE_TYPE (oprnd1)) != TYPE_MAIN_VARIANT (type))
    return NULL;

  /* So far so good. Since last_stmt was detected as a (summation) reduction,
     we know that oprnd1 is the reduction variable (defined by a loop-header
     phi), and oprnd0 is an ssa-name defined by a stmt in the loop body.
     Left to check that oprnd0 is defined by a cast from type 'type' to type
     'TYPE'.  */

  if (!widened_name_p (oprnd0, last_stmt, &half_type, &stmt))
    return NULL;

  oprnd0 = TREE_OPERAND (TREE_OPERAND (stmt, 1), 0);
  *type_in = half_type;
  *type_out = type;

  /* Pattern detected. Create a stmt to be used to replace the pattern: */
  pattern_expr = build2 (WIDEN_SUM_EXPR, type, oprnd0, oprnd1);
  if (vect_print_dump_info (REPORT_DETAILS))
    {
      fprintf (vect_dump, "vect_recog_widen_sum_pattern: detected: ");
      print_generic_expr (vect_dump, pattern_expr, TDF_SLIM);
    }
  return pattern_expr;
}


/* Function vect_pattern_recog_1 

   Input:
   PATTERN_RECOG_FUNC: A pointer to a function that detects a certain
        computation pattern.
   STMT: A stmt from which the pattern search should start.

   If PATTERN_RECOG_FUNC successfully detected the pattern, it creates an
   expression that computes the same functionality and can be used to 
   replace the sequence of stmts that are involved in the pattern. 

   Output:
   This function checks if the expression returned by PATTERN_RECOG_FUNC is 
   supported in vector form by the target.  We use 'TYPE_IN' to obtain the 
   relevant vector type. If 'TYPE_IN' is already a vector type, then this 
   indicates that target support had already been checked by PATTERN_RECOG_FUNC.
   If 'TYPE_OUT' is also returned by PATTERN_RECOG_FUNC, we check that it fits
   to the available target pattern.

   This function also does some bookkeeping, as explained in the documentation 
   for vect_recog_pattern.  */

static void
vect_pattern_recog_1 (
	tree (* vect_recog_func) (tree, tree *, tree *),
	block_stmt_iterator si)
{
  tree stmt = bsi_stmt (si);
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  stmt_vec_info pattern_stmt_info;
  loop_vec_info loop_vinfo = STMT_VINFO_LOOP_VINFO (stmt_info);
  tree pattern_expr;
  tree pattern_vectype;
  tree type_in, type_out;
  tree pattern_type;
  enum tree_code code;
  tree var, var_name;
  stmt_ann_t ann;

  pattern_expr = (* vect_recog_func) (stmt, &type_in, &type_out);
  if (!pattern_expr) 
    return; 
 
  if (VECTOR_MODE_P (TYPE_MODE (type_in))) 
    { 
      /* No need to check target support (already checked by the pattern 
         recognition function).  */ 
      pattern_vectype = type_in;
    }
  else
    {
      enum tree_code vec_mode;
      enum insn_code icode;
      optab optab;

      /* Check target support  */
      pattern_vectype = get_vectype_for_scalar_type (type_in);
      optab = optab_for_tree_code (TREE_CODE (pattern_expr), pattern_vectype);
      vec_mode = TYPE_MODE (pattern_vectype);
      if (!optab
          || (icode = optab->handlers[(int) vec_mode].insn_code) ==
              CODE_FOR_nothing
          || (type_out
              && (insn_data[icode].operand[0].mode !=
                  TYPE_MODE (get_vectype_for_scalar_type (type_out)))))
	return;
    }

  /* Found a vectorizable pattern.  */
  if (vect_print_dump_info (REPORT_DETAILS))
    {
      fprintf (vect_dump, "pattern recognized: "); 
      print_generic_expr (vect_dump, pattern_expr, TDF_SLIM);
    }
  
  /* Mark the stmts that are involved in the pattern,
     create a new stmt to express the pattern and insert it.  */
  code = TREE_CODE (pattern_expr);
  pattern_type = TREE_TYPE (pattern_expr);
  var = create_tmp_var (pattern_type, "patt");
  add_referenced_var (var);
  var_name = make_ssa_name (var, NULL_TREE);
  pattern_expr = build2 (MODIFY_EXPR, void_type_node, var_name, pattern_expr);
  SSA_NAME_DEF_STMT (var_name) = pattern_expr;
  bsi_insert_before (&si, pattern_expr, BSI_SAME_STMT);
  ann = stmt_ann (pattern_expr);
  set_stmt_info (ann, new_stmt_vec_info (pattern_expr, loop_vinfo));
  pattern_stmt_info = vinfo_for_stmt (pattern_expr);
  
  STMT_VINFO_RELATED_STMT (pattern_stmt_info) = stmt;
  STMT_VINFO_DEF_TYPE (pattern_stmt_info) = STMT_VINFO_DEF_TYPE (stmt_info);
  STMT_VINFO_VECTYPE (pattern_stmt_info) = pattern_vectype;
  STMT_VINFO_IN_PATTERN_P (stmt_info) = true;
  STMT_VINFO_RELATED_STMT (stmt_info) = pattern_expr;

  return;
}


/* Function vect_pattern_recog

   Input:
   LOOP_VINFO - a struct_loop_info of a loop in which we want to look for
        computation idioms.

   Output - for each computation idiom that is detected we insert a new stmt
        that provides the same functionality and that can be vectorized. We
        also record some information in the struct_stmt_info of the relevant
        stmts, as explained below:

   At the entry to this function we have the following stmts, with the
   following initial value in the STMT_VINFO fields:

         stmt                     in_pattern_p  related_stmt    vec_stmt
         S1: a_i = ....                 -       -               -
         S2: a_2 = ..use(a_i)..         -       -               -
         S3: a_1 = ..use(a_2)..         -       -               -
         S4: a_0 = ..use(a_1)..         -       -               -
         S5: ... = ..use(a_0)..         -       -               -

   Say the sequence {S1,S2,S3,S4} was detected as a pattern that can be
   represented by a single stmt. We then:
   - create a new stmt S6 that will replace the pattern.
   - insert the new stmt S6 before the last stmt in the pattern
   - fill in the STMT_VINFO fields as follows:

                                  in_pattern_p  related_stmt    vec_stmt
         S1: a_i = ....                 -       -               -       
         S2: a_2 = ..use(a_i)..         -       -               -
         S3: a_1 = ..use(a_2)..         -       -               -
       > S6: a_new = ....               -       S4              -
         S4: a_0 = ..use(a_1)..         true    S6              -
         S5: ... = ..use(a_0)..         -       -               -

   (the last stmt in the pattern (S4) and the new pattern stmt (S6) point
    to each other through the RELATED_STMT field).

   S6 will be marked as relevant in vect_mark_stmts_to_be_vectorized instead
   of S4 because it will replace all its uses.  Stmts {S1,S2,S3} will
   remain irrelevant unless used by stmts other than S4.

   If vectorization succeeds, vect_transform_stmt will skip over {S1,S2,S3}
   (because they are marked as irrelevant). It will vectorize S6, and record
   a pointer to the new vector stmt VS6 both from S6 (as usual), and also 
   from S4. We do that so that when we get to vectorizing stmts that use the
   def of S4 (like S5 that uses a_0), we'll know where to take the relevant
   vector-def from. S4 will be skipped, and S5 will be vectorized as usual:

                                  in_pattern_p  related_stmt    vec_stmt
         S1: a_i = ....                 -       -               -
         S2: a_2 = ..use(a_i)..         -       -               -
         S3: a_1 = ..use(a_2)..         -       -               -
       > VS6: va_new = ....             -       -               -
         S6: a_new = ....               -       S4              VS6
         S4: a_0 = ..use(a_1)..         true    S6              VS6
       > VS5: ... = ..vuse(va_new)..    -       -               -
         S5: ... = ..use(a_0)..         -       -               -

   DCE could then get rid of {S1,S2,S3,S4,S5,S6} (if their defs are not used
   elsewhere), and we'll end up with:

        VS6: va_new = .... 
        VS5: ... = ..vuse(va_new)..

   If vectorization does not succeed, DCE will clean S6 away (its def is
   not used), and we'll end up with the original sequence.
*/

void
vect_pattern_recog (loop_vec_info loop_vinfo)
{
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  basic_block *bbs = LOOP_VINFO_BBS (loop_vinfo);
  unsigned int nbbs = loop->num_nodes;
  block_stmt_iterator si;
  tree stmt;
  unsigned int i, j;
  tree (* vect_recog_func_ptr) (tree, tree *, tree *);

  if (vect_print_dump_info (REPORT_DETAILS))
    fprintf (vect_dump, "=== vect_pattern_recog ===");

  /* Scan through the loop stmts, applying the pattern recognition
     functions starting at each stmt visited:  */
  for (i = 0; i < nbbs; i++)
    {
      basic_block bb = bbs[i];
      for (si = bsi_start (bb); !bsi_end_p (si); bsi_next (&si))
        {
          stmt = bsi_stmt (si);

          /* Scan over all generic vect_recog_xxx_pattern functions.  */
          for (j = 0; j < NUM_PATTERNS; j++)
            {
              vect_recog_func_ptr = vect_vect_recog_func_ptrs[j];
              vect_pattern_recog_1 (vect_recog_func_ptr, si);
            }
        }
    }
}
