/* Transformations based on profile information for values.
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.

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
#include "rtl.h"
#include "expr.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "value-prof.h"
#include "output.h"
#include "flags.h"
#include "insn-config.h"
#include "recog.h"
#include "optabs.h"
#include "regs.h"
#include "ggc.h"
#include "tree-flow.h"
#include "tree-flow-inline.h"
#include "diagnostic.h"
#include "coverage.h"
#include "tree.h"
#include "gcov-io.h"
#include "timevar.h"
#include "tree-pass.h"
#include "toplev.h"

static struct value_prof_hooks *value_prof_hooks;

/* In this file value profile based optimizations are placed.  Currently the
   following optimizations are implemented (for more detailed descriptions
   see comments at value_profile_transformations):

   1) Division/modulo specialization.  Provided that we can determine that the
      operands of the division have some special properties, we may use it to
      produce more effective code.
   2) Speculative prefetching.  If we are able to determine that the difference
      between addresses accessed by a memory reference is usually constant, we
      may add the prefetch instructions.
      FIXME: This transformation was removed together with RTL based value
      profiling.

   Every such optimization should add its requirements for profiled values to
   insn_values_to_profile function.  This function is called from branch_prob
   in profile.c and the requested values are instrumented by it in the first
   compilation with -fprofile-arcs.  The optimization may then read the
   gathered data in the second compilation with -fbranch-probabilities.

   The measured data is pointed to from the histograms
   field of the statement annotation of the instrumented insns.  It is
   kept as a linked list of struct histogram_value_t's, which contain the
   same information as above.  */


static tree tree_divmod_fixed_value (tree, tree, tree, tree, 
				    tree, int, gcov_type, gcov_type);
static tree tree_mod_pow2 (tree, tree, tree, tree, int, gcov_type, gcov_type);
static tree tree_mod_subtract (tree, tree, tree, tree, int, int, int,
				gcov_type, gcov_type, gcov_type);
static bool tree_divmod_fixed_value_transform (tree);
static bool tree_mod_pow2_value_transform (tree);
static bool tree_mod_subtract_transform (tree);

/* The overall number of invocations of the counter should match execution count
   of basic block.  Report it as error rather than internal error as it might
   mean that user has misused the profile somehow.  */
static bool
check_counter (tree stmt, const char * name, gcov_type all, gcov_type bb_count)
{
  if (all != bb_count)
    {
      location_t * locus;
      locus = (stmt != NULL && EXPR_HAS_LOCATION (stmt)
	       ? EXPR_LOCUS (stmt)
	       : &DECL_SOURCE_LOCATION (current_function_decl));
      error ("%HCorrupted value profile: %s profiler overall count (%d) does not match BB count (%d)",
	     locus, name, (int)all, (int)bb_count);
      return true;
    }
  return false;
}

/* Tree based transformations. */
static bool
tree_value_profile_transformations (void)
{
  basic_block bb;
  block_stmt_iterator bsi;
  bool changed = false;

  FOR_EACH_BB (bb)
    {
      /* Ignore cold areas -- we are enlarging the code.  */
      if (!maybe_hot_bb_p (bb))
	continue;

      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	{
	  tree stmt = bsi_stmt (bsi);
	  stmt_ann_t ann = get_stmt_ann (stmt);
	  histogram_value th = ann->histograms;
	  if (!th)
	    continue;

	  if (dump_file)
	    {
	      fprintf (dump_file, "Trying transformations on insn ");
	      print_generic_stmt (dump_file, stmt, TDF_SLIM);
	    }

	  /* Transformations:  */
	  /* The order of things in this conditional controls which
	     transformation is used when more than one is applicable.  */
	  /* It is expected that any code added by the transformations
	     will be added before the current statement, and that the
	     current statement remain valid (although possibly
	     modified) upon return.  */
	  if (flag_value_profile_transformations
	      && (tree_mod_subtract_transform (stmt)
		  || tree_divmod_fixed_value_transform (stmt)
		  || tree_mod_pow2_value_transform (stmt)))
	    {
	      changed = true;
	      /* Original statement may no longer be in the same block. */
	      if (bb != bb_for_stmt (stmt))
		{
	          bb = bb_for_stmt (stmt);
		  bsi = bsi_for_stmt (stmt);
		}
	    }

	  /* Free extra storage from compute_value_histograms.  */
	  while (th)
	    {
	      free (th->hvalue.counters);
	      th = th->hvalue.next;
	    }
	  ann->histograms = 0;
        }
    }

  if (changed)
    {
      counts_to_freqs ();
    }

  return changed;
}

/* Generate code for transformation 1 (with OPERATION, operands OP1
   and OP2, whose value is expected to be VALUE, parent modify-expr STMT and
   probability of taking the optimal path PROB, which is equivalent to COUNT/ALL
   within roundoff error).  This generates the result into a temp and returns 
   the temp; it does not replace or alter the original STMT.  */
static tree
tree_divmod_fixed_value (tree stmt, tree operation, 
			 tree op1, tree op2, tree value, int prob, gcov_type count,
			 gcov_type all)
{
  tree stmt1, stmt2, stmt3;
  tree tmp1, tmp2, tmpv;
  tree label_decl1 = create_artificial_label ();
  tree label_decl2 = create_artificial_label ();
  tree label_decl3 = create_artificial_label ();
  tree label1, label2, label3;
  tree bb1end, bb2end, bb3end;
  basic_block bb, bb2, bb3, bb4;
  tree optype = TREE_TYPE (operation);
  edge e12, e13, e23, e24, e34;
  block_stmt_iterator bsi;

  bb = bb_for_stmt (stmt);
  bsi = bsi_for_stmt (stmt);

  tmpv = create_tmp_var (optype, "PROF");
  tmp1 = create_tmp_var (optype, "PROF");
  stmt1 = build2 (MODIFY_EXPR, optype, tmpv, fold_convert (optype, value));
  stmt2 = build2 (MODIFY_EXPR, optype, tmp1, op2);
  stmt3 = build3 (COND_EXPR, void_type_node,
	    build2 (NE_EXPR, boolean_type_node, tmp1, tmpv),
	    build1 (GOTO_EXPR, void_type_node, label_decl2),
	    build1 (GOTO_EXPR, void_type_node, label_decl1));
  bsi_insert_before (&bsi, stmt1, BSI_SAME_STMT);
  bsi_insert_before (&bsi, stmt2, BSI_SAME_STMT);
  bsi_insert_before (&bsi, stmt3, BSI_SAME_STMT);
  bb1end = stmt3;

  tmp2 = create_tmp_var (optype, "PROF");
  label1 = build1 (LABEL_EXPR, void_type_node, label_decl1);
  stmt1 = build2 (MODIFY_EXPR, optype, tmp2,
		  build2 (TREE_CODE (operation), optype, op1, tmpv));
  bsi_insert_before (&bsi, label1, BSI_SAME_STMT);
  bsi_insert_before (&bsi, stmt1, BSI_SAME_STMT);
  bb2end = stmt1;

  label2 = build1 (LABEL_EXPR, void_type_node, label_decl2);
  stmt1 = build2 (MODIFY_EXPR, optype, tmp2,
		  build2 (TREE_CODE (operation), optype, op1, op2));
  bsi_insert_before (&bsi, label2, BSI_SAME_STMT);
  bsi_insert_before (&bsi, stmt1, BSI_SAME_STMT);
  bb3end = stmt1;

  label3 = build1 (LABEL_EXPR, void_type_node, label_decl3);
  bsi_insert_before (&bsi, label3, BSI_SAME_STMT);

  /* Fix CFG. */
  /* Edge e23 connects bb2 to bb3, etc. */
  e12 = split_block (bb, bb1end);
  bb2 = e12->dest;
  bb2->count = count;
  e23 = split_block (bb2, bb2end);
  bb3 = e23->dest;
  bb3->count = all - count;
  e34 = split_block (bb3, bb3end);
  bb4 = e34->dest;
  bb4->count = all;

  e12->flags &= ~EDGE_FALLTHRU;
  e12->flags |= EDGE_FALSE_VALUE;
  e12->probability = prob;
  e12->count = count;

  e13 = make_edge (bb, bb3, EDGE_TRUE_VALUE);
  e13->probability = REG_BR_PROB_BASE - prob;
  e13->count = all - count;

  remove_edge (e23);
  
  e24 = make_edge (bb2, bb4, EDGE_FALLTHRU);
  e24->probability = REG_BR_PROB_BASE;
  e24->count = count;

  e34->probability = REG_BR_PROB_BASE;
  e34->count = all - count;

  return tmp2;
}

/* Do transform 1) on INSN if applicable.  */
static bool
tree_divmod_fixed_value_transform (tree stmt)
{
  stmt_ann_t ann = get_stmt_ann (stmt);
  histogram_value histogram;
  enum tree_code code;
  gcov_type val, count, all;
  tree modify, op, op1, op2, result, value, tree_val;
  int prob;

  modify = stmt;
  if (TREE_CODE (stmt) == RETURN_EXPR
      && TREE_OPERAND (stmt, 0)
      && TREE_CODE (TREE_OPERAND (stmt, 0)) == MODIFY_EXPR)
    modify = TREE_OPERAND (stmt, 0);
  if (TREE_CODE (modify) != MODIFY_EXPR)
    return false;
  op = TREE_OPERAND (modify, 1);
  if (!INTEGRAL_TYPE_P (TREE_TYPE (op)))
    return false;
  code = TREE_CODE (op);
  
  if (code != TRUNC_DIV_EXPR && code != TRUNC_MOD_EXPR)
    return false;

  op1 = TREE_OPERAND (op, 0);
  op2 = TREE_OPERAND (op, 1);
  if (!ann->histograms)
    return false;

  for (histogram = ann->histograms; histogram; histogram = histogram->hvalue.next)
    if (histogram->type == HIST_TYPE_SINGLE_VALUE)
      break;

  if (!histogram)
    return false;

  value = histogram->hvalue.value;
  val = histogram->hvalue.counters[0];
  count = histogram->hvalue.counters[1];
  all = histogram->hvalue.counters[2];

  /* We require that count is at least half of all; this means
     that for the transformation to fire the value must be constant
     at least 50% of time (and 75% gives the guarantee of usage).  */
  if (simple_cst_equal (op2, value) != 1 || 2 * count < all)
    return false;

  if (check_counter (stmt, "value", all, bb_for_stmt (stmt)->count))
    return false;

  /* Compute probability of taking the optimal path.  */
  prob = (count * REG_BR_PROB_BASE + all / 2) / all;

  tree_val = build_int_cst_wide (get_gcov_type (),
				 (unsigned HOST_WIDE_INT) val,
				 val >> (HOST_BITS_PER_WIDE_INT - 1) >> 1);
  result = tree_divmod_fixed_value (stmt, op, op1, op2, tree_val, prob, count, all);

  if (dump_file)
    {
      fprintf (dump_file, "Div/mod by constant ");
      print_generic_expr (dump_file, value, TDF_SLIM);
      fprintf (dump_file, "=");
      print_generic_expr (dump_file, tree_val, TDF_SLIM);
      fprintf (dump_file, " transformation on insn ");
      print_generic_stmt (dump_file, stmt, TDF_SLIM);
    }

  TREE_OPERAND (modify, 1) = result;

  return true;
}

/* Generate code for transformation 2 (with OPERATION, operands OP1
   and OP2, parent modify-expr STMT and probability of taking the optimal 
   path PROB, which is equivalent to COUNT/ALL within roundoff error).  
   This generates the result into a temp and returns 
   the temp; it does not replace or alter the original STMT.  */
static tree
tree_mod_pow2 (tree stmt, tree operation, tree op1, tree op2, int prob, 
	       gcov_type count, gcov_type all)
{
  tree stmt1, stmt2, stmt3, stmt4;
  tree tmp2, tmp3;
  tree label_decl1 = create_artificial_label ();
  tree label_decl2 = create_artificial_label ();
  tree label_decl3 = create_artificial_label ();
  tree label1, label2, label3;
  tree bb1end, bb2end, bb3end;
  basic_block bb, bb2, bb3, bb4;
  tree optype = TREE_TYPE (operation);
  edge e12, e13, e23, e24, e34;
  block_stmt_iterator bsi;
  tree result = create_tmp_var (optype, "PROF");

  bb = bb_for_stmt (stmt);
  bsi = bsi_for_stmt (stmt);

  tmp2 = create_tmp_var (optype, "PROF");
  tmp3 = create_tmp_var (optype, "PROF");
  stmt2 = build2 (MODIFY_EXPR, optype, tmp2, 
		  build2 (PLUS_EXPR, optype, op2, build_int_cst (optype, -1)));
  stmt3 = build2 (MODIFY_EXPR, optype, tmp3,
		  build2 (BIT_AND_EXPR, optype, tmp2, op2));
  stmt4 = build3 (COND_EXPR, void_type_node,
		  build2 (NE_EXPR, boolean_type_node,
			  tmp3, build_int_cst (optype, 0)),
		  build1 (GOTO_EXPR, void_type_node, label_decl2),
	 	  build1 (GOTO_EXPR, void_type_node, label_decl1));
  bsi_insert_before (&bsi, stmt2, BSI_SAME_STMT);
  bsi_insert_before (&bsi, stmt3, BSI_SAME_STMT);
  bsi_insert_before (&bsi, stmt4, BSI_SAME_STMT);
  bb1end = stmt4;

  /* tmp2 == op2-1 inherited from previous block */
  label1 = build1 (LABEL_EXPR, void_type_node, label_decl1);
  stmt1 = build2 (MODIFY_EXPR, optype, result,
		  build2 (BIT_AND_EXPR, optype, op1, tmp2));
  bsi_insert_before (&bsi, label1, BSI_SAME_STMT);
  bsi_insert_before (&bsi, stmt1, BSI_SAME_STMT);
  bb2end = stmt1;

  label2 = build1 (LABEL_EXPR, void_type_node, label_decl2);
  stmt1 = build2 (MODIFY_EXPR, optype, result,
		  build2 (TREE_CODE (operation), optype, op1, op2));
  bsi_insert_before (&bsi, label2, BSI_SAME_STMT);
  bsi_insert_before (&bsi, stmt1, BSI_SAME_STMT);
  bb3end = stmt1;

  label3 = build1 (LABEL_EXPR, void_type_node, label_decl3);
  bsi_insert_before (&bsi, label3, BSI_SAME_STMT);

  /* Fix CFG. */
  /* Edge e23 connects bb2 to bb3, etc. */
  e12 = split_block (bb, bb1end);
  bb2 = e12->dest;
  bb2->count = count;
  e23 = split_block (bb2, bb2end);
  bb3 = e23->dest;
  bb3->count = all - count;
  e34 = split_block (bb3, bb3end);
  bb4 = e34->dest;
  bb4->count = all;

  e12->flags &= ~EDGE_FALLTHRU;
  e12->flags |= EDGE_FALSE_VALUE;
  e12->probability = prob;
  e12->count = count;

  e13 = make_edge (bb, bb3, EDGE_TRUE_VALUE);
  e13->probability = REG_BR_PROB_BASE - prob;
  e13->count = all - count;

  remove_edge (e23);
  
  e24 = make_edge (bb2, bb4, EDGE_FALLTHRU);
  e24->probability = REG_BR_PROB_BASE;
  e24->count = count;

  e34->probability = REG_BR_PROB_BASE;
  e34->count = all - count;

  return result;
}

/* Do transform 2) on INSN if applicable.  */
static bool
tree_mod_pow2_value_transform (tree stmt)
{
  stmt_ann_t ann = get_stmt_ann (stmt);
  histogram_value histogram;
  enum tree_code code;
  gcov_type count, wrong_values, all;
  tree modify, op, op1, op2, result, value;
  int prob;

  modify = stmt;
  if (TREE_CODE (stmt) == RETURN_EXPR
      && TREE_OPERAND (stmt, 0)
      && TREE_CODE (TREE_OPERAND (stmt, 0)) == MODIFY_EXPR)
    modify = TREE_OPERAND (stmt, 0);
  if (TREE_CODE (modify) != MODIFY_EXPR)
    return false;
  op = TREE_OPERAND (modify, 1);
  if (!INTEGRAL_TYPE_P (TREE_TYPE (op)))
    return false;
  code = TREE_CODE (op);
  
  if (code != TRUNC_MOD_EXPR || !TYPE_UNSIGNED (TREE_TYPE (op)))
    return false;

  op1 = TREE_OPERAND (op, 0);
  op2 = TREE_OPERAND (op, 1);
  if (!ann->histograms)
    return false;

  for (histogram = ann->histograms; histogram; histogram = histogram->hvalue.next)
    if (histogram->type == HIST_TYPE_POW2)
      break;

  if (!histogram)
    return false;

  value = histogram->hvalue.value;
  wrong_values = histogram->hvalue.counters[0];
  count = histogram->hvalue.counters[1];

  /* We require that we hit a power of 2 at least half of all evaluations.  */
  if (simple_cst_equal (op2, value) != 1 || count < wrong_values)
    return false;

  if (dump_file)
    {
      fprintf (dump_file, "Mod power of 2 transformation on insn ");
      print_generic_stmt (dump_file, stmt, TDF_SLIM);
    }

  /* Compute probability of taking the optimal path.  */
  all = count + wrong_values;
  if (check_counter (stmt, "pow2", all, bb_for_stmt (stmt)->count))
    return false;

  prob = (count * REG_BR_PROB_BASE + all / 2) / all;

  result = tree_mod_pow2 (stmt, op, op1, op2, prob, count, all);

  TREE_OPERAND (modify, 1) = result;

  return true;
}

/* Generate code for transformations 3 and 4 (with OPERATION, operands OP1
   and OP2, parent modify-expr STMT, and NCOUNTS the number of cases to
   support.  Currently only NCOUNTS==0 or 1 is supported and this is
   built into this interface.  The probabilities of taking the optimal 
   paths are PROB1 and PROB2, which are equivalent to COUNT1/ALL and
   COUNT2/ALL respectively within roundoff error).  This generates the 
   result into a temp and returns the temp; it does not replace or alter 
   the original STMT.  */
/* FIXME: Generalize the interface to handle NCOUNTS > 1.  */

static tree
tree_mod_subtract (tree stmt, tree operation, tree op1, tree op2, 
		    int prob1, int prob2, int ncounts,
		    gcov_type count1, gcov_type count2, gcov_type all)
{
  tree stmt1, stmt2, stmt3;
  tree tmp1;
  tree label_decl1 = create_artificial_label ();
  tree label_decl2 = create_artificial_label ();
  tree label_decl3 = create_artificial_label ();
  tree label1, label2, label3;
  tree bb1end, bb2end = NULL_TREE, bb3end;
  basic_block bb, bb2, bb3, bb4;
  tree optype = TREE_TYPE (operation);
  edge e12, e23 = 0, e24, e34, e14;
  block_stmt_iterator bsi;
  tree result = create_tmp_var (optype, "PROF");

  bb = bb_for_stmt (stmt);
  bsi = bsi_for_stmt (stmt);

  tmp1 = create_tmp_var (optype, "PROF");
  stmt1 = build2 (MODIFY_EXPR, optype, result, op1);
  stmt2 = build2 (MODIFY_EXPR, optype, tmp1, op2);
  stmt3 = build3 (COND_EXPR, void_type_node,
	    build2 (LT_EXPR, boolean_type_node, result, tmp1),
	    build1 (GOTO_EXPR, void_type_node, label_decl3),
	    build1 (GOTO_EXPR, void_type_node, 
		    ncounts ? label_decl1 : label_decl2));
  bsi_insert_before (&bsi, stmt1, BSI_SAME_STMT);
  bsi_insert_before (&bsi, stmt2, BSI_SAME_STMT);
  bsi_insert_before (&bsi, stmt3, BSI_SAME_STMT);
  bb1end = stmt3;

  if (ncounts)	/* Assumed to be 0 or 1 */
    {
      label1 = build1 (LABEL_EXPR, void_type_node, label_decl1);
      stmt1 = build2 (MODIFY_EXPR, optype, result,
		      build2 (MINUS_EXPR, optype, result, tmp1));
      stmt2 = build3 (COND_EXPR, void_type_node,
		build2 (LT_EXPR, boolean_type_node, result, tmp1),
		build1 (GOTO_EXPR, void_type_node, label_decl3),
		build1 (GOTO_EXPR, void_type_node, label_decl2));
      bsi_insert_before (&bsi, label1, BSI_SAME_STMT);
      bsi_insert_before (&bsi, stmt1, BSI_SAME_STMT);
      bsi_insert_before (&bsi, stmt2, BSI_SAME_STMT);
      bb2end = stmt2;
    }

  /* Fallback case. */
  label2 = build1 (LABEL_EXPR, void_type_node, label_decl2);
  stmt1 = build2 (MODIFY_EXPR, optype, result,
		    build2 (TREE_CODE (operation), optype, result, tmp1));
  bsi_insert_before (&bsi, label2, BSI_SAME_STMT);
  bsi_insert_before (&bsi, stmt1, BSI_SAME_STMT);
  bb3end = stmt1;

  label3 = build1 (LABEL_EXPR, void_type_node, label_decl3);
  bsi_insert_before (&bsi, label3, BSI_SAME_STMT);

  /* Fix CFG. */
  /* Edge e23 connects bb2 to bb3, etc. */
  /* However block 3 is optional; if it is not there, references
     to 3 really refer to block 2. */
  e12 = split_block (bb, bb1end);
  bb2 = e12->dest;
  bb2->count = all - count1;
    
  if (ncounts)	/* Assumed to be 0 or 1.  */
    {
      e23 = split_block (bb2, bb2end);
      bb3 = e23->dest;
      bb3->count = all - count1 - count2;
    }

  e34 = split_block (ncounts ? bb3 : bb2, bb3end);
  bb4 = e34->dest;
  bb4->count = all;

  e12->flags &= ~EDGE_FALLTHRU;
  e12->flags |= EDGE_FALSE_VALUE;
  e12->probability = REG_BR_PROB_BASE - prob1;
  e12->count = all - count1;

  e14 = make_edge (bb, bb4, EDGE_TRUE_VALUE);
  e14->probability = prob1;
  e14->count = count1;

  if (ncounts)  /* Assumed to be 0 or 1.  */
    {
      e23->flags &= ~EDGE_FALLTHRU;
      e23->flags |= EDGE_FALSE_VALUE;
      e23->count = all - count1 - count2;
      e23->probability = REG_BR_PROB_BASE - prob2;

      e24 = make_edge (bb2, bb4, EDGE_TRUE_VALUE);
      e24->probability = prob2;
      e24->count = count2;
    }

  e34->probability = REG_BR_PROB_BASE;
  e34->count = all - count1 - count2;

  return result;
}

/* Do transforms 3) and 4) on INSN if applicable.  */
static bool
tree_mod_subtract_transform (tree stmt)
{
  stmt_ann_t ann = get_stmt_ann (stmt);
  histogram_value histogram;
  enum tree_code code;
  gcov_type count, wrong_values, all;
  tree modify, op, op1, op2, result, value;
  int prob1, prob2;
  unsigned int i;

  modify = stmt;
  if (TREE_CODE (stmt) == RETURN_EXPR
      && TREE_OPERAND (stmt, 0)
      && TREE_CODE (TREE_OPERAND (stmt, 0)) == MODIFY_EXPR)
    modify = TREE_OPERAND (stmt, 0);
  if (TREE_CODE (modify) != MODIFY_EXPR)
    return false;
  op = TREE_OPERAND (modify, 1);
  if (!INTEGRAL_TYPE_P (TREE_TYPE (op)))
    return false;
  code = TREE_CODE (op);
  
  if (code != TRUNC_MOD_EXPR || !TYPE_UNSIGNED (TREE_TYPE (op)))
    return false;

  op1 = TREE_OPERAND (op, 0);
  op2 = TREE_OPERAND (op, 1);
  if (!ann->histograms)
    return false;

  for (histogram = ann->histograms; histogram; histogram = histogram->hvalue.next)
    if (histogram->type == HIST_TYPE_INTERVAL)
      break;

  if (!histogram)
    return false;

  value = histogram->hvalue.value;
  all = 0;
  wrong_values = 0;
  for (i = 0; i < histogram->hdata.intvl.steps; i++)
    all += histogram->hvalue.counters[i];

  wrong_values += histogram->hvalue.counters[i];
  wrong_values += histogram->hvalue.counters[i+1];
  all += wrong_values;

  /* Compute probability of taking the optimal path.  */
  if (check_counter (stmt, "interval", all, bb_for_stmt (stmt)->count))
    return false;

  /* We require that we use just subtractions in at least 50% of all
     evaluations.  */
  count = 0;
  for (i = 0; i < histogram->hdata.intvl.steps; i++)
    {
      count += histogram->hvalue.counters[i];
      if (count * 2 >= all)
	break;
    }
  if (i == histogram->hdata.intvl.steps)
    return false;

  if (dump_file)
    {
      fprintf (dump_file, "Mod subtract transformation on insn ");
      print_generic_stmt (dump_file, stmt, TDF_SLIM);
    }

  /* Compute probability of taking the optimal path(s).  */
  prob1 = (histogram->hvalue.counters[0] * REG_BR_PROB_BASE + all / 2) / all;
  prob2 = (histogram->hvalue.counters[1] * REG_BR_PROB_BASE + all / 2) / all;

  /* In practice, "steps" is always 2.  This interface reflects this,
     and will need to be changed if "steps" can change.  */
  result = tree_mod_subtract (stmt, op, op1, op2, prob1, prob2, i,
			    histogram->hvalue.counters[0], 
			    histogram->hvalue.counters[1], all);

  TREE_OPERAND (modify, 1) = result;

  return true;
}

struct value_prof_hooks {
  /* Find list of values for which we want to measure histograms.  */
  void (*find_values_to_profile) (histogram_values *);

  /* Identify and exploit properties of values that are hard to analyze
     statically.  See value-prof.c for more detail.  */
  bool (*value_profile_transformations) (void);  
};

/* Find values inside STMT for that we want to measure histograms for
   division/modulo optimization.  */
static void
tree_divmod_values_to_profile (tree stmt, histogram_values *values)
{
  tree assign, lhs, rhs, divisor, op0, type;
  histogram_value hist;

  if (TREE_CODE (stmt) == RETURN_EXPR)
    assign = TREE_OPERAND (stmt, 0);
  else
    assign = stmt;

  if (!assign
      || TREE_CODE (assign) != MODIFY_EXPR)
    return;
  lhs = TREE_OPERAND (assign, 0);
  type = TREE_TYPE (lhs);
  if (!INTEGRAL_TYPE_P (type))
    return;

  rhs = TREE_OPERAND (assign, 1);
  switch (TREE_CODE (rhs))
    {
    case TRUNC_DIV_EXPR:
    case TRUNC_MOD_EXPR:
      divisor = TREE_OPERAND (rhs, 1);
      op0 = TREE_OPERAND (rhs, 0);

      VEC_reserve (histogram_value, heap, *values, 3);

      if (is_gimple_reg (divisor))
	{
	  /* Check for the case where the divisor is the same value most
	     of the time.  */
	  hist = ggc_alloc (sizeof (*hist));
	  hist->hvalue.value = divisor;
	  hist->hvalue.stmt = stmt;
	  hist->type = HIST_TYPE_SINGLE_VALUE;
	  VEC_quick_push (histogram_value, *values, hist);
	}

      /* For mod, check whether it is not often a noop (or replaceable by
	 a few subtractions).  */
      if (TREE_CODE (rhs) == TRUNC_MOD_EXPR
	  && TYPE_UNSIGNED (type))
	{
          /* Check for a special case where the divisor is power of 2.  */
	  hist = ggc_alloc (sizeof (*hist));
	  hist->hvalue.value = divisor;
	  hist->hvalue.stmt = stmt;
	  hist->type = HIST_TYPE_POW2;
	  VEC_quick_push (histogram_value, *values, hist);

	  hist = ggc_alloc (sizeof (*hist));
	  hist->hvalue.stmt = stmt;
	  hist->hvalue.value
		  = build2 (TRUNC_DIV_EXPR, type, op0, divisor);
	  hist->type = HIST_TYPE_INTERVAL;
	  hist->hdata.intvl.int_start = 0;
	  hist->hdata.intvl.steps = 2;
	  VEC_quick_push (histogram_value, *values, hist);
	}
      return;

    default:
      return;
    }
}

/* Find values inside STMT for that we want to measure histograms and adds
   them to list VALUES.  */

static void
tree_values_to_profile (tree stmt, histogram_values *values)
{
  if (flag_value_profile_transformations)
    tree_divmod_values_to_profile (stmt, values);
}

static void
tree_find_values_to_profile (histogram_values *values)
{
  basic_block bb;
  block_stmt_iterator bsi;
  unsigned i;
  histogram_value hist;

  *values = NULL;
  FOR_EACH_BB (bb)
    for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
      tree_values_to_profile (bsi_stmt (bsi), values);
  
  for (i = 0; VEC_iterate (histogram_value, *values, i, hist); i++)
    {
      switch (hist->type)
        {
	case HIST_TYPE_INTERVAL:
	  if (dump_file)
	    {
	      fprintf (dump_file, "Interval counter for tree ");
	      print_generic_expr (dump_file, hist->hvalue.stmt, 
				  TDF_SLIM);
	      fprintf (dump_file, ", range %d -- %d.\n",
		     hist->hdata.intvl.int_start,
		     (hist->hdata.intvl.int_start
		      + hist->hdata.intvl.steps - 1));
	    }
	  hist->n_counters = hist->hdata.intvl.steps + 2;
	  break;

	case HIST_TYPE_POW2:
	  if (dump_file)
	    {
	      fprintf (dump_file, "Pow2 counter for tree ");
	      print_generic_expr (dump_file, hist->hvalue.stmt, TDF_SLIM);
	      fprintf (dump_file, ".\n");
	    }
	  hist->n_counters = 2;
	  break;

	case HIST_TYPE_SINGLE_VALUE:
	  if (dump_file)
	    {
	      fprintf (dump_file, "Single value counter for tree ");
	      print_generic_expr (dump_file, hist->hvalue.stmt, TDF_SLIM);
	      fprintf (dump_file, ".\n");
	    }
	  hist->n_counters = 3;
	  break;

	case HIST_TYPE_CONST_DELTA:
	  if (dump_file)
	    {
	      fprintf (dump_file, "Constant delta counter for tree ");
	      print_generic_expr (dump_file, hist->hvalue.stmt, TDF_SLIM);
	      fprintf (dump_file, ".\n");
	    }
	  hist->n_counters = 4;
	  break;

	default:
	  gcc_unreachable ();
	}
    }
}

static struct value_prof_hooks tree_value_prof_hooks = {
  tree_find_values_to_profile,
  tree_value_profile_transformations
};

void
tree_register_value_prof_hooks (void)
{
  value_prof_hooks = &tree_value_prof_hooks;
  gcc_assert (ir_type ());
}

/* IR-independent entry points.  */
void
find_values_to_profile (histogram_values *values)
{
  (value_prof_hooks->find_values_to_profile) (values);
}

bool
value_profile_transformations (void)
{
  return (value_prof_hooks->value_profile_transformations) ();
}


