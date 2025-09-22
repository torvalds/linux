/* Optimization of PHI nodes by converting them into straightline code.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

GCC is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
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
#include "rtl.h"
#include "flags.h"
#include "tm_p.h"
#include "basic-block.h"
#include "timevar.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-pass.h"
#include "tree-dump.h"
#include "langhooks.h"

static unsigned int tree_ssa_phiopt (void);
static bool conditional_replacement (basic_block, basic_block,
				     edge, edge, tree, tree, tree);
static bool value_replacement (basic_block, basic_block,
			       edge, edge, tree, tree, tree);
static bool minmax_replacement (basic_block, basic_block,
				edge, edge, tree, tree, tree);
static bool abs_replacement (basic_block, basic_block,
			     edge, edge, tree, tree, tree);
static void replace_phi_edge_with_variable (basic_block, edge, tree, tree);
static basic_block *blocks_in_phiopt_order (void);

/* This pass tries to replaces an if-then-else block with an
   assignment.  We have four kinds of transformations.  Some of these
   transformations are also performed by the ifcvt RTL optimizer.

   Conditional Replacement
   -----------------------

   This transformation, implemented in conditional_replacement,
   replaces

     bb0:
      if (cond) goto bb2; else goto bb1;
     bb1:
     bb2:
      x = PHI <0 (bb1), 1 (bb0), ...>;

   with

     bb0:
      x' = cond;
      goto bb2;
     bb2:
      x = PHI <x' (bb0), ...>;

   We remove bb1 as it becomes unreachable.  This occurs often due to
   gimplification of conditionals.

   Value Replacement
   -----------------

   This transformation, implemented in value_replacement, replaces

     bb0:
       if (a != b) goto bb2; else goto bb1;
     bb1:
     bb2:
       x = PHI <a (bb1), b (bb0), ...>;

   with

     bb0:
     bb2:
       x = PHI <b (bb0), ...>;

   This opportunity can sometimes occur as a result of other
   optimizations.

   ABS Replacement
   ---------------

   This transformation, implemented in abs_replacement, replaces

     bb0:
       if (a >= 0) goto bb2; else goto bb1;
     bb1:
       x = -a;
     bb2:
       x = PHI <x (bb1), a (bb0), ...>;

   with

     bb0:
       x' = ABS_EXPR< a >;
     bb2:
       x = PHI <x' (bb0), ...>;

   MIN/MAX Replacement
   -------------------

   This transformation, minmax_replacement replaces

     bb0:
       if (a <= b) goto bb2; else goto bb1;
     bb1:
     bb2:
       x = PHI <b (bb1), a (bb0), ...>;

   with

     bb0:
       x' = MIN_EXPR (a, b)
     bb2:
       x = PHI <x' (bb0), ...>;

   A similar transformation is done for MAX_EXPR.  */

static unsigned int
tree_ssa_phiopt (void)
{
  basic_block bb;
  basic_block *bb_order;
  unsigned n, i;
  bool cfgchanged = false;

  /* Search every basic block for COND_EXPR we may be able to optimize.

     We walk the blocks in order that guarantees that a block with
     a single predecessor is processed before the predecessor.
     This ensures that we collapse inner ifs before visiting the
     outer ones, and also that we do not try to visit a removed
     block.  */
  bb_order = blocks_in_phiopt_order ();
  n = n_basic_blocks - NUM_FIXED_BLOCKS;

  for (i = 0; i < n; i++) 
    {
      tree cond_expr;
      tree phi;
      basic_block bb1, bb2;
      edge e1, e2;
      tree arg0, arg1;

      bb = bb_order[i];

      cond_expr = last_stmt (bb);
      /* Check to see if the last statement is a COND_EXPR.  */
      if (!cond_expr
          || TREE_CODE (cond_expr) != COND_EXPR)
        continue;

      e1 = EDGE_SUCC (bb, 0);
      bb1 = e1->dest;
      e2 = EDGE_SUCC (bb, 1);
      bb2 = e2->dest;

      /* We cannot do the optimization on abnormal edges.  */
      if ((e1->flags & EDGE_ABNORMAL) != 0
          || (e2->flags & EDGE_ABNORMAL) != 0)
       continue;

      /* If either bb1's succ or bb2 or bb2's succ is non NULL.  */
      if (EDGE_COUNT (bb1->succs) == 0
          || bb2 == NULL
	  || EDGE_COUNT (bb2->succs) == 0)
        continue;

      /* Find the bb which is the fall through to the other.  */
      if (EDGE_SUCC (bb1, 0)->dest == bb2)
        ;
      else if (EDGE_SUCC (bb2, 0)->dest == bb1)
        {
	  basic_block bb_tmp = bb1;
	  edge e_tmp = e1;
	  bb1 = bb2;
	  bb2 = bb_tmp;
	  e1 = e2;
	  e2 = e_tmp;
	}
      else
        continue;

      e1 = EDGE_SUCC (bb1, 0);

      /* Make sure that bb1 is just a fall through.  */
      if (!single_succ_p (bb1)
	  || (e1->flags & EDGE_FALLTHRU) == 0)
        continue;

      /* Also make sure that bb1 only have one predecessor and that it
	 is bb.  */
      if (!single_pred_p (bb1)
          || single_pred (bb1) != bb)
	continue;

      phi = phi_nodes (bb2);

      /* Check to make sure that there is only one PHI node.
         TODO: we could do it with more than one iff the other PHI nodes
	 have the same elements for these two edges.  */
      if (!phi || PHI_CHAIN (phi) != NULL)
	continue;

      arg0 = PHI_ARG_DEF_TREE (phi, e1->dest_idx);
      arg1 = PHI_ARG_DEF_TREE (phi, e2->dest_idx);

      /* Something is wrong if we cannot find the arguments in the PHI
	 node.  */
      gcc_assert (arg0 != NULL && arg1 != NULL);

      /* Do the replacement of conditional if it can be done.  */
      if (conditional_replacement (bb, bb1, e1, e2, phi, arg0, arg1))
	cfgchanged = true;
      else if (value_replacement (bb, bb1, e1, e2, phi, arg0, arg1))
	cfgchanged = true;
      else if (abs_replacement (bb, bb1, e1, e2, phi, arg0, arg1))
	cfgchanged = true;
      else if (minmax_replacement (bb, bb1, e1, e2, phi, arg0, arg1))
	cfgchanged = true;
    }

  free (bb_order);
  
  /* If the CFG has changed, we should cleanup the CFG. */
  return cfgchanged ? TODO_cleanup_cfg : 0;
}

/* Returns the list of basic blocks in the function in an order that guarantees
   that if a block X has just a single predecessor Y, then Y is after X in the
   ordering.  */

static basic_block *
blocks_in_phiopt_order (void)
{
  basic_block x, y;
  basic_block *order = XNEWVEC (basic_block, n_basic_blocks);
  unsigned n = n_basic_blocks - NUM_FIXED_BLOCKS; 
  unsigned np, i;
  sbitmap visited = sbitmap_alloc (last_basic_block); 

#define MARK_VISITED(BB) (SET_BIT (visited, (BB)->index)) 
#define VISITED_P(BB) (TEST_BIT (visited, (BB)->index)) 

  sbitmap_zero (visited);

  MARK_VISITED (ENTRY_BLOCK_PTR);
  FOR_EACH_BB (x)
    {
      if (VISITED_P (x))
	continue;

      /* Walk the predecessors of x as long as they have precisely one
	 predecessor and add them to the list, so that they get stored
	 after x.  */
      for (y = x, np = 1;
	   single_pred_p (y) && !VISITED_P (single_pred (y));
	   y = single_pred (y))
	np++;
      for (y = x, i = n - np;
	   single_pred_p (y) && !VISITED_P (single_pred (y));
	   y = single_pred (y), i++)
	{
	  order[i] = y;
	  MARK_VISITED (y);
	}
      order[i] = y;
      MARK_VISITED (y);

      gcc_assert (i == n - 1);
      n -= np;
    }

  sbitmap_free (visited);
  gcc_assert (n == 0);
  return order;

#undef MARK_VISITED
#undef VISITED_P
}

/* Return TRUE if block BB has no executable statements, otherwise return
   FALSE.  */
bool
empty_block_p (basic_block bb)
{
  block_stmt_iterator bsi;

  /* BB must have no executable statements.  */
  bsi = bsi_start (bb);
  while (!bsi_end_p (bsi)
	  && (TREE_CODE (bsi_stmt (bsi)) == LABEL_EXPR
	      || IS_EMPTY_STMT (bsi_stmt (bsi))))
    bsi_next (&bsi);

  if (!bsi_end_p (bsi))
    return false;

  return true;
}

/* Replace PHI node element whose edge is E in block BB with variable NEW.
   Remove the edge from COND_BLOCK which does not lead to BB (COND_BLOCK
   is known to have two edges, one of which must reach BB).  */

static void
replace_phi_edge_with_variable (basic_block cond_block,
				edge e, tree phi, tree new)
{
  basic_block bb = bb_for_stmt (phi);
  basic_block block_to_remove;
  block_stmt_iterator bsi;

  /* Change the PHI argument to new.  */
  SET_USE (PHI_ARG_DEF_PTR (phi, e->dest_idx), new);

  /* Remove the empty basic block.  */
  if (EDGE_SUCC (cond_block, 0)->dest == bb)
    {
      EDGE_SUCC (cond_block, 0)->flags |= EDGE_FALLTHRU;
      EDGE_SUCC (cond_block, 0)->flags &= ~(EDGE_TRUE_VALUE | EDGE_FALSE_VALUE);
      EDGE_SUCC (cond_block, 0)->probability = REG_BR_PROB_BASE;
      EDGE_SUCC (cond_block, 0)->count += EDGE_SUCC (cond_block, 1)->count;

      block_to_remove = EDGE_SUCC (cond_block, 1)->dest;
    }
  else
    {
      EDGE_SUCC (cond_block, 1)->flags |= EDGE_FALLTHRU;
      EDGE_SUCC (cond_block, 1)->flags
	&= ~(EDGE_TRUE_VALUE | EDGE_FALSE_VALUE);
      EDGE_SUCC (cond_block, 1)->probability = REG_BR_PROB_BASE;
      EDGE_SUCC (cond_block, 1)->count += EDGE_SUCC (cond_block, 0)->count;

      block_to_remove = EDGE_SUCC (cond_block, 0)->dest;
    }
  delete_basic_block (block_to_remove);

  /* Eliminate the COND_EXPR at the end of COND_BLOCK.  */
  bsi = bsi_last (cond_block);
  bsi_remove (&bsi, true);

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file,
	      "COND_EXPR in block %d and PHI in block %d converted to straightline code.\n",
	      cond_block->index,
	      bb->index);
}

/*  The function conditional_replacement does the main work of doing the
    conditional replacement.  Return true if the replacement is done.
    Otherwise return false.
    BB is the basic block where the replacement is going to be done on.  ARG0
    is argument 0 from PHI.  Likewise for ARG1.  */

static bool
conditional_replacement (basic_block cond_bb, basic_block middle_bb,
			 edge e0, edge e1, tree phi,
			 tree arg0, tree arg1)
{
  tree result;
  tree old_result = NULL;
  tree new, cond;
  block_stmt_iterator bsi;
  edge true_edge, false_edge;
  tree new_var = NULL;
  tree new_var1;

  /* The PHI arguments have the constants 0 and 1, then convert
     it to the conditional.  */
  if ((integer_zerop (arg0) && integer_onep (arg1))
      || (integer_zerop (arg1) && integer_onep (arg0)))
    ;
  else
    return false;

  if (!empty_block_p (middle_bb))
    return false;

  /* If the condition is not a naked SSA_NAME and its type does not
     match the type of the result, then we have to create a new
     variable to optimize this case as it would likely create
     non-gimple code when the condition was converted to the
     result's type.  */
  cond = COND_EXPR_COND (last_stmt (cond_bb));
  result = PHI_RESULT (phi);
  if (TREE_CODE (cond) != SSA_NAME
      && !lang_hooks.types_compatible_p (TREE_TYPE (cond), TREE_TYPE (result)))
    {
      tree tmp;

      if (!COMPARISON_CLASS_P (cond))
	return false;

      tmp = create_tmp_var (TREE_TYPE (cond), NULL);
      add_referenced_var (tmp);
      new_var = make_ssa_name (tmp, NULL);
      old_result = cond;
      cond = new_var;
    }

  /* If the condition was a naked SSA_NAME and the type is not the
     same as the type of the result, then convert the type of the
     condition.  */
  if (!lang_hooks.types_compatible_p (TREE_TYPE (cond), TREE_TYPE (result)))
    cond = fold_convert (TREE_TYPE (result), cond);

  /* We need to know which is the true edge and which is the false
     edge so that we know when to invert the condition below.  */
  extract_true_false_edges_from_block (cond_bb, &true_edge, &false_edge);

  /* Insert our new statement at the end of conditional block before the
     COND_EXPR.  */
  bsi = bsi_last (cond_bb);
  bsi_insert_before (&bsi, build_empty_stmt (), BSI_NEW_STMT);

  if (old_result)
    {
      tree new1;

      new1 = build2 (TREE_CODE (old_result), TREE_TYPE (old_result),
		     TREE_OPERAND (old_result, 0),
		     TREE_OPERAND (old_result, 1));

      new1 = build2 (MODIFY_EXPR, TREE_TYPE (old_result), new_var, new1);
      SSA_NAME_DEF_STMT (new_var) = new1;

      bsi_insert_after (&bsi, new1, BSI_NEW_STMT);
    }

  new_var1 = duplicate_ssa_name (PHI_RESULT (phi), NULL);


  /* At this point we know we have a COND_EXPR with two successors.
     One successor is BB, the other successor is an empty block which
     falls through into BB.

     There is a single PHI node at the join point (BB) and its arguments
     are constants (0, 1).

     So, given the condition COND, and the two PHI arguments, we can
     rewrite this PHI into non-branching code:

       dest = (COND) or dest = COND'

     We use the condition as-is if the argument associated with the
     true edge has the value one or the argument associated with the
     false edge as the value zero.  Note that those conditions are not
     the same since only one of the outgoing edges from the COND_EXPR
     will directly reach BB and thus be associated with an argument.  */
  if ((e0 == true_edge && integer_onep (arg0))
      || (e0 == false_edge && integer_zerop (arg0))
      || (e1 == true_edge && integer_onep (arg1))
      || (e1 == false_edge && integer_zerop (arg1)))
    {
      new = build2 (MODIFY_EXPR, TREE_TYPE (new_var1), new_var1, cond);
    }
  else
    {
      tree cond1 = invert_truthvalue (cond);

      cond = cond1;

      /* If what we get back is a conditional expression, there is no
	  way that it can be gimple.  */
      if (TREE_CODE (cond) == COND_EXPR)
	{
	  release_ssa_name (new_var1);
	  return false;
	}

      /* If COND is not something we can expect to be reducible to a GIMPLE
	 condition, return early.  */
      if (is_gimple_cast (cond))
	cond1 = TREE_OPERAND (cond, 0);
      if (TREE_CODE (cond1) == TRUTH_NOT_EXPR
	  && !is_gimple_val (TREE_OPERAND (cond1, 0)))
	{
	  release_ssa_name (new_var1);
	  return false;
	}

      /* If what we get back is not gimple try to create it as gimple by
	 using a temporary variable.  */
      if (is_gimple_cast (cond)
	  && !is_gimple_val (TREE_OPERAND (cond, 0)))
	{
	  tree op0, tmp, cond_tmp;

	  /* Only "real" casts are OK here, not everything that is
	     acceptable to is_gimple_cast.  Make sure we don't do
	     anything stupid here.  */
	  gcc_assert (TREE_CODE (cond) == NOP_EXPR
		      || TREE_CODE (cond) == CONVERT_EXPR);

	  op0 = TREE_OPERAND (cond, 0);
	  tmp = create_tmp_var (TREE_TYPE (op0), NULL);
	  add_referenced_var (tmp);
	  cond_tmp = make_ssa_name (tmp, NULL);
	  new = build2 (MODIFY_EXPR, TREE_TYPE (cond_tmp), cond_tmp, op0);
	  SSA_NAME_DEF_STMT (cond_tmp) = new;

	  bsi_insert_after (&bsi, new, BSI_NEW_STMT);
	  cond = fold_convert (TREE_TYPE (result), cond_tmp);
	}

      new = build2 (MODIFY_EXPR, TREE_TYPE (new_var1), new_var1, cond);
    }

  bsi_insert_after (&bsi, new, BSI_NEW_STMT);

  SSA_NAME_DEF_STMT (new_var1) = new;

  replace_phi_edge_with_variable (cond_bb, e1, phi, new_var1);

  /* Note that we optimized this PHI.  */
  return true;
}

/*  The function value_replacement does the main work of doing the value
    replacement.  Return true if the replacement is done.  Otherwise return
    false.
    BB is the basic block where the replacement is going to be done on.  ARG0
    is argument 0 from the PHI.  Likewise for ARG1.  */

static bool
value_replacement (basic_block cond_bb, basic_block middle_bb,
		   edge e0, edge e1, tree phi,
		   tree arg0, tree arg1)
{
  tree cond;
  edge true_edge, false_edge;

  /* If the type says honor signed zeros we cannot do this
     optimization.  */
  if (HONOR_SIGNED_ZEROS (TYPE_MODE (TREE_TYPE (arg1))))
    return false;

  if (!empty_block_p (middle_bb))
    return false;

  cond = COND_EXPR_COND (last_stmt (cond_bb));

  /* This transformation is only valid for equality comparisons.  */
  if (TREE_CODE (cond) != NE_EXPR && TREE_CODE (cond) != EQ_EXPR)
    return false;

  /* We need to know which is the true edge and which is the false
      edge so that we know if have abs or negative abs.  */
  extract_true_false_edges_from_block (cond_bb, &true_edge, &false_edge);

  /* At this point we know we have a COND_EXPR with two successors.
     One successor is BB, the other successor is an empty block which
     falls through into BB.

     The condition for the COND_EXPR is known to be NE_EXPR or EQ_EXPR.

     There is a single PHI node at the join point (BB) with two arguments.

     We now need to verify that the two arguments in the PHI node match
     the two arguments to the equality comparison.  */

  if ((operand_equal_for_phi_arg_p (arg0, TREE_OPERAND (cond, 0))
       && operand_equal_for_phi_arg_p (arg1, TREE_OPERAND (cond, 1)))
      || (operand_equal_for_phi_arg_p (arg1, TREE_OPERAND (cond, 0))
	  && operand_equal_for_phi_arg_p (arg0, TREE_OPERAND (cond, 1))))
    {
      edge e;
      tree arg;

      /* For NE_EXPR, we want to build an assignment result = arg where
	 arg is the PHI argument associated with the true edge.  For
	 EQ_EXPR we want the PHI argument associated with the false edge.  */
      e = (TREE_CODE (cond) == NE_EXPR ? true_edge : false_edge);

      /* Unfortunately, E may not reach BB (it may instead have gone to
	 OTHER_BLOCK).  If that is the case, then we want the single outgoing
	 edge from OTHER_BLOCK which reaches BB and represents the desired
	 path from COND_BLOCK.  */
      if (e->dest == middle_bb)
	e = single_succ_edge (e->dest);

      /* Now we know the incoming edge to BB that has the argument for the
	 RHS of our new assignment statement.  */
      if (e0 == e)
	arg = arg0;
      else
	arg = arg1;

      replace_phi_edge_with_variable (cond_bb, e1, phi, arg);

      /* Note that we optimized this PHI.  */
      return true;
    }
  return false;
}

/*  The function minmax_replacement does the main work of doing the minmax
    replacement.  Return true if the replacement is done.  Otherwise return
    false.
    BB is the basic block where the replacement is going to be done on.  ARG0
    is argument 0 from the PHI.  Likewise for ARG1.  */

static bool
minmax_replacement (basic_block cond_bb, basic_block middle_bb,
		    edge e0, edge e1, tree phi,
		    tree arg0, tree arg1)
{
  tree result, type;
  tree cond, new;
  edge true_edge, false_edge;
  enum tree_code cmp, minmax, ass_code;
  tree smaller, larger, arg_true, arg_false;
  block_stmt_iterator bsi, bsi_from;

  type = TREE_TYPE (PHI_RESULT (phi));

  /* The optimization may be unsafe due to NaNs.  */
  if (HONOR_NANS (TYPE_MODE (type)))
    return false;

  cond = COND_EXPR_COND (last_stmt (cond_bb));
  cmp = TREE_CODE (cond);
  result = PHI_RESULT (phi);

  /* This transformation is only valid for order comparisons.  Record which
     operand is smaller/larger if the result of the comparison is true.  */
  if (cmp == LT_EXPR || cmp == LE_EXPR)
    {
      smaller = TREE_OPERAND (cond, 0);
      larger = TREE_OPERAND (cond, 1);
    }
  else if (cmp == GT_EXPR || cmp == GE_EXPR)
    {
      smaller = TREE_OPERAND (cond, 1);
      larger = TREE_OPERAND (cond, 0);
    }
  else
    return false;

  /* We need to know which is the true edge and which is the false
      edge so that we know if have abs or negative abs.  */
  extract_true_false_edges_from_block (cond_bb, &true_edge, &false_edge);

  /* Forward the edges over the middle basic block.  */
  if (true_edge->dest == middle_bb)
    true_edge = EDGE_SUCC (true_edge->dest, 0);
  if (false_edge->dest == middle_bb)
    false_edge = EDGE_SUCC (false_edge->dest, 0);

  if (true_edge == e0)
    {
      gcc_assert (false_edge == e1);
      arg_true = arg0;
      arg_false = arg1;
    }
  else
    {
      gcc_assert (false_edge == e0);
      gcc_assert (true_edge == e1);
      arg_true = arg1;
      arg_false = arg0;
    }

  if (empty_block_p (middle_bb))
    {
      if (operand_equal_for_phi_arg_p (arg_true, smaller)
	  && operand_equal_for_phi_arg_p (arg_false, larger))
	{
	  /* Case
	 
	     if (smaller < larger)
	     rslt = smaller;
	     else
	     rslt = larger;  */
	  minmax = MIN_EXPR;
	}
      else if (operand_equal_for_phi_arg_p (arg_false, smaller)
	       && operand_equal_for_phi_arg_p (arg_true, larger))
	minmax = MAX_EXPR;
      else
	return false;
    }
  else
    {
      /* Recognize the following case, assuming d <= u:

	 if (a <= u)
	   b = MAX (a, d);
	 x = PHI <b, u>

	 This is equivalent to

	 b = MAX (a, d);
	 x = MIN (b, u);  */

      tree assign = last_and_only_stmt (middle_bb);
      tree lhs, rhs, op0, op1, bound;

      if (!assign
	  || TREE_CODE (assign) != MODIFY_EXPR)
	return false;

      lhs = TREE_OPERAND (assign, 0);
      rhs = TREE_OPERAND (assign, 1);
      ass_code = TREE_CODE (rhs);
      if (ass_code != MAX_EXPR && ass_code != MIN_EXPR)
	return false;
      op0 = TREE_OPERAND (rhs, 0);
      op1 = TREE_OPERAND (rhs, 1);

      if (true_edge->src == middle_bb)
	{
	  /* We got here if the condition is true, i.e., SMALLER < LARGER.  */
	  if (!operand_equal_for_phi_arg_p (lhs, arg_true))
	    return false;

	  if (operand_equal_for_phi_arg_p (arg_false, larger))
	    {
	      /* Case

		 if (smaller < larger)
		   {
		     r' = MAX_EXPR (smaller, bound)
		   }
		 r = PHI <r', larger>  --> to be turned to MIN_EXPR.  */
	      if (ass_code != MAX_EXPR)
		return false;

	      minmax = MIN_EXPR;
	      if (operand_equal_for_phi_arg_p (op0, smaller))
		bound = op1;
	      else if (operand_equal_for_phi_arg_p (op1, smaller))
		bound = op0;
	      else
		return false;

	      /* We need BOUND <= LARGER.  */
	      if (!integer_nonzerop (fold_build2 (LE_EXPR, boolean_type_node,
						  bound, larger)))
		return false;
	    }
	  else if (operand_equal_for_phi_arg_p (arg_false, smaller))
	    {
	      /* Case

		 if (smaller < larger)
		   {
		     r' = MIN_EXPR (larger, bound)
		   }
		 r = PHI <r', smaller>  --> to be turned to MAX_EXPR.  */
	      if (ass_code != MIN_EXPR)
		return false;

	      minmax = MAX_EXPR;
	      if (operand_equal_for_phi_arg_p (op0, larger))
		bound = op1;
	      else if (operand_equal_for_phi_arg_p (op1, larger))
		bound = op0;
	      else
		return false;

	      /* We need BOUND >= SMALLER.  */
	      if (!integer_nonzerop (fold_build2 (GE_EXPR, boolean_type_node,
						  bound, smaller)))
		return false;
	    }
	  else
	    return false;
	}
      else
	{
	  /* We got here if the condition is false, i.e., SMALLER > LARGER.  */
	  if (!operand_equal_for_phi_arg_p (lhs, arg_false))
	    return false;

	  if (operand_equal_for_phi_arg_p (arg_true, larger))
	    {
	      /* Case

		 if (smaller > larger)
		   {
		     r' = MIN_EXPR (smaller, bound)
		   }
		 r = PHI <r', larger>  --> to be turned to MAX_EXPR.  */
	      if (ass_code != MIN_EXPR)
		return false;

	      minmax = MAX_EXPR;
	      if (operand_equal_for_phi_arg_p (op0, smaller))
		bound = op1;
	      else if (operand_equal_for_phi_arg_p (op1, smaller))
		bound = op0;
	      else
		return false;

	      /* We need BOUND >= LARGER.  */
	      if (!integer_nonzerop (fold_build2 (GE_EXPR, boolean_type_node,
						  bound, larger)))
		return false;
	    }
	  else if (operand_equal_for_phi_arg_p (arg_true, smaller))
	    {
	      /* Case

		 if (smaller > larger)
		   {
		     r' = MAX_EXPR (larger, bound)
		   }
		 r = PHI <r', smaller>  --> to be turned to MIN_EXPR.  */
	      if (ass_code != MAX_EXPR)
		return false;

	      minmax = MIN_EXPR;
	      if (operand_equal_for_phi_arg_p (op0, larger))
		bound = op1;
	      else if (operand_equal_for_phi_arg_p (op1, larger))
		bound = op0;
	      else
		return false;

	      /* We need BOUND <= SMALLER.  */
	      if (!integer_nonzerop (fold_build2 (LE_EXPR, boolean_type_node,
						  bound, smaller)))
		return false;
	    }
	  else
	    return false;
	}

      /* Move the statement from the middle block.  */
      bsi = bsi_last (cond_bb);
      bsi_from = bsi_last (middle_bb);
      bsi_move_before (&bsi_from, &bsi);
    }

  /* Emit the statement to compute min/max.  */
  result = duplicate_ssa_name (PHI_RESULT (phi), NULL);
  new = build2 (MODIFY_EXPR, type, result,
		build2 (minmax, type, arg0, arg1));
  SSA_NAME_DEF_STMT (result) = new;
  bsi = bsi_last (cond_bb);
  bsi_insert_before (&bsi, new, BSI_NEW_STMT);

  replace_phi_edge_with_variable (cond_bb, e1, phi, result);
  return true;
}

/*  The function absolute_replacement does the main work of doing the absolute
    replacement.  Return true if the replacement is done.  Otherwise return
    false.
    bb is the basic block where the replacement is going to be done on.  arg0
    is argument 0 from the phi.  Likewise for arg1.  */

static bool
abs_replacement (basic_block cond_bb, basic_block middle_bb,
		 edge e0 ATTRIBUTE_UNUSED, edge e1,
		 tree phi, tree arg0, tree arg1)
{
  tree result;
  tree new, cond;
  block_stmt_iterator bsi;
  edge true_edge, false_edge;
  tree assign;
  edge e;
  tree rhs, lhs;
  bool negate;
  enum tree_code cond_code;

  /* If the type says honor signed zeros we cannot do this
     optimization.  */
  if (HONOR_SIGNED_ZEROS (TYPE_MODE (TREE_TYPE (arg1))))
    return false;

  /* OTHER_BLOCK must have only one executable statement which must have the
     form arg0 = -arg1 or arg1 = -arg0.  */

  assign = last_and_only_stmt (middle_bb);
  /* If we did not find the proper negation assignment, then we can not
     optimize.  */
  if (assign == NULL)
    return false;
      
  /* If we got here, then we have found the only executable statement
     in OTHER_BLOCK.  If it is anything other than arg = -arg1 or
     arg1 = -arg0, then we can not optimize.  */
  if (TREE_CODE (assign) != MODIFY_EXPR)
    return false;

  lhs = TREE_OPERAND (assign, 0);
  rhs = TREE_OPERAND (assign, 1);

  if (TREE_CODE (rhs) != NEGATE_EXPR)
    return false;

  rhs = TREE_OPERAND (rhs, 0);
              
  /* The assignment has to be arg0 = -arg1 or arg1 = -arg0.  */
  if (!(lhs == arg0 && rhs == arg1)
      && !(lhs == arg1 && rhs == arg0))
    return false;

  cond = COND_EXPR_COND (last_stmt (cond_bb));
  result = PHI_RESULT (phi);

  /* Only relationals comparing arg[01] against zero are interesting.  */
  cond_code = TREE_CODE (cond);
  if (cond_code != GT_EXPR && cond_code != GE_EXPR
      && cond_code != LT_EXPR && cond_code != LE_EXPR)
    return false;

  /* Make sure the conditional is arg[01] OP y.  */
  if (TREE_OPERAND (cond, 0) != rhs)
    return false;

  if (FLOAT_TYPE_P (TREE_TYPE (TREE_OPERAND (cond, 1)))
	       ? real_zerop (TREE_OPERAND (cond, 1))
	       : integer_zerop (TREE_OPERAND (cond, 1)))
    ;
  else
    return false;

  /* We need to know which is the true edge and which is the false
     edge so that we know if have abs or negative abs.  */
  extract_true_false_edges_from_block (cond_bb, &true_edge, &false_edge);

  /* For GT_EXPR/GE_EXPR, if the true edge goes to OTHER_BLOCK, then we
     will need to negate the result.  Similarly for LT_EXPR/LE_EXPR if
     the false edge goes to OTHER_BLOCK.  */
  if (cond_code == GT_EXPR || cond_code == GE_EXPR)
    e = true_edge;
  else
    e = false_edge;

  if (e->dest == middle_bb)
    negate = true;
  else
    negate = false;

  result = duplicate_ssa_name (result, NULL);

  if (negate)
    {
      tree tmp = create_tmp_var (TREE_TYPE (result), NULL);
      add_referenced_var (tmp);
      lhs = make_ssa_name (tmp, NULL);
    }
  else
    lhs = result;

  /* Build the modify expression with abs expression.  */
  new = build2 (MODIFY_EXPR, TREE_TYPE (lhs),
		lhs, build1 (ABS_EXPR, TREE_TYPE (lhs), rhs));
  SSA_NAME_DEF_STMT (lhs) = new;

  bsi = bsi_last (cond_bb);
  bsi_insert_before (&bsi, new, BSI_NEW_STMT);

  if (negate)
    {
      /* Get the right BSI.  We want to insert after the recently
	 added ABS_EXPR statement (which we know is the first statement
	 in the block.  */
      new = build2 (MODIFY_EXPR, TREE_TYPE (result),
		    result, build1 (NEGATE_EXPR, TREE_TYPE (lhs), lhs));
      SSA_NAME_DEF_STMT (result) = new;

      bsi_insert_after (&bsi, new, BSI_NEW_STMT);
    }

  replace_phi_edge_with_variable (cond_bb, e1, phi, result);

  /* Note that we optimized this PHI.  */
  return true;
}


/* Always do these optimizations if we have SSA
   trees to work on.  */
static bool
gate_phiopt (void)
{
  return 1;
}

struct tree_opt_pass pass_phiopt =
{
  "phiopt",				/* name */
  gate_phiopt,				/* gate */
  tree_ssa_phiopt,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_PHIOPT,			/* tv_id */
  PROP_cfg | PROP_ssa | PROP_alias,	/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func
    | TODO_ggc_collect
    | TODO_verify_ssa
    | TODO_verify_flow
    | TODO_verify_stmts,		/* todo_flags_finish */
  0					/* letter */
};
