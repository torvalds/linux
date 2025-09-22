/* SSA Jump Threading
   Copyright (C) 2005, 2006, 2007 Free Software Foundation, Inc.
   Contributed by Jeff Law  <law@redhat.com>

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "flags.h"
#include "rtl.h"
#include "tm_p.h"
#include "ggc.h"
#include "basic-block.h"
#include "cfgloop.h"
#include "output.h"
#include "expr.h"
#include "function.h"
#include "diagnostic.h"
#include "timevar.h"
#include "tree-dump.h"
#include "tree-flow.h"
#include "domwalk.h"
#include "real.h"
#include "tree-pass.h"
#include "tree-ssa-propagate.h"
#include "langhooks.h"
#include "params.h"

/* To avoid code explosion due to jump threading, we limit the
   number of statements we are going to copy.  This variable
   holds the number of statements currently seen that we'll have
   to copy as part of the jump threading process.  */
static int stmt_count;

/* Return TRUE if we may be able to thread an incoming edge into
   BB to an outgoing edge from BB.  Return FALSE otherwise.  */

bool
potentially_threadable_block (basic_block bb)
{
  block_stmt_iterator bsi;

  /* If BB has a single successor or a single predecessor, then
     there is no threading opportunity.  */
  if (single_succ_p (bb) || single_pred_p (bb))
    return false;

  /* If BB does not end with a conditional, switch or computed goto,
     then there is no threading opportunity.  */
  bsi = bsi_last (bb);
  if (bsi_end_p (bsi)
      || ! bsi_stmt (bsi)
      || (TREE_CODE (bsi_stmt (bsi)) != COND_EXPR
	  && TREE_CODE (bsi_stmt (bsi)) != GOTO_EXPR
	  && TREE_CODE (bsi_stmt (bsi)) != SWITCH_EXPR))
    return false;

  return true;
}

/* Return the LHS of any ASSERT_EXPR where OP appears as the first
   argument to the ASSERT_EXPR and in which the ASSERT_EXPR dominates
   BB.  If no such ASSERT_EXPR is found, return OP.  */

static tree
lhs_of_dominating_assert (tree op, basic_block bb, tree stmt)
{
  imm_use_iterator imm_iter;
  tree use_stmt;
  use_operand_p use_p;

  FOR_EACH_IMM_USE_FAST (use_p, imm_iter, op)
    {
      use_stmt = USE_STMT (use_p);
      if (use_stmt != stmt
          && TREE_CODE (use_stmt) == MODIFY_EXPR
          && TREE_CODE (TREE_OPERAND (use_stmt, 1)) == ASSERT_EXPR
          && TREE_OPERAND (TREE_OPERAND (use_stmt, 1), 0) == op
	  && dominated_by_p (CDI_DOMINATORS, bb, bb_for_stmt (use_stmt)))
	{
	  return TREE_OPERAND (use_stmt, 0);
	}
    }
  return op;
}


/* We record temporary equivalences created by PHI nodes or
   statements within the target block.  Doing so allows us to
   identify more jump threading opportunities, even in blocks
   with side effects.

   We keep track of those temporary equivalences in a stack
   structure so that we can unwind them when we're done processing
   a particular edge.  This routine handles unwinding the data
   structures.  */

static void
remove_temporary_equivalences (VEC(tree, heap) **stack)
{
  while (VEC_length (tree, *stack) > 0)
    {
      tree prev_value, dest;

      dest = VEC_pop (tree, *stack);

      /* A NULL value indicates we should stop unwinding, otherwise
	 pop off the next entry as they're recorded in pairs.  */
      if (dest == NULL)
	break;

      prev_value = VEC_pop (tree, *stack);
      SSA_NAME_VALUE (dest) = prev_value;
    }
}

/* Record a temporary equivalence, saving enough information so that
   we can restore the state of recorded equivalences when we're
   done processing the current edge.  */

static void
record_temporary_equivalence (tree x, tree y, VEC(tree, heap) **stack)
{
  tree prev_x = SSA_NAME_VALUE (x);

  if (TREE_CODE (y) == SSA_NAME)
    {
      tree tmp = SSA_NAME_VALUE (y);
      y = tmp ? tmp : y;
    }

  SSA_NAME_VALUE (x) = y;
  VEC_reserve (tree, heap, *stack, 2);
  VEC_quick_push (tree, *stack, prev_x);
  VEC_quick_push (tree, *stack, x);
}

/* Record temporary equivalences created by PHIs at the target of the
   edge E.  Record unwind information for the equivalences onto STACK. 

   If a PHI which prevents threading is encountered, then return FALSE
   indicating we should not thread this edge, else return TRUE.  */

static bool
record_temporary_equivalences_from_phis (edge e, VEC(tree, heap) **stack)
{
  tree phi;

  /* Each PHI creates a temporary equivalence, record them.
     These are context sensitive equivalences and will be removed
     later.  */
  for (phi = phi_nodes (e->dest); phi; phi = PHI_CHAIN (phi))
    {
      tree src = PHI_ARG_DEF_FROM_EDGE (phi, e);
      tree dst = PHI_RESULT (phi);

      /* If the desired argument is not the same as this PHI's result 
	 and it is set by a PHI in E->dest, then we can not thread
	 through E->dest.  */
      if (src != dst
	  && TREE_CODE (src) == SSA_NAME
	  && TREE_CODE (SSA_NAME_DEF_STMT (src)) == PHI_NODE
	  && bb_for_stmt (SSA_NAME_DEF_STMT (src)) == e->dest)
	return false;

      /* We consider any non-virtual PHI as a statement since it
	 count result in a constant assignment or copy operation.  */
      if (is_gimple_reg (dst))
	stmt_count++;

      record_temporary_equivalence (dst, src, stack);
    }
  return true;
}

/* Try to simplify each statement in E->dest, ultimately leading to
   a simplification of the COND_EXPR at the end of E->dest.

   Record unwind information for temporary equivalences onto STACK.

   Use SIMPLIFY (a pointer to a callback function) to further simplify
   statements using pass specific information. 

   We might consider marking just those statements which ultimately
   feed the COND_EXPR.  It's not clear if the overhead of bookkeeping
   would be recovered by trying to simplify fewer statements.

   If we are able to simplify a statement into the form
   SSA_NAME = (SSA_NAME | gimple invariant), then we can record
   a context sensitive equivalency which may help us simplify
   later statements in E->dest.  */

static tree
record_temporary_equivalences_from_stmts_at_dest (edge e,
						  VEC(tree, heap) **stack,
						  tree (*simplify) (tree,
								    tree))
{
  block_stmt_iterator bsi;
  tree stmt = NULL;
  int max_stmt_count;

  max_stmt_count = PARAM_VALUE (PARAM_MAX_JUMP_THREAD_DUPLICATION_STMTS);

  /* Walk through each statement in the block recording equivalences
     we discover.  Note any equivalences we discover are context
     sensitive (ie, are dependent on traversing E) and must be unwound
     when we're finished processing E.  */
  for (bsi = bsi_start (e->dest); ! bsi_end_p (bsi); bsi_next (&bsi))
    {
      tree cached_lhs = NULL;

      stmt = bsi_stmt (bsi);

      /* Ignore empty statements and labels.  */
      if (IS_EMPTY_STMT (stmt) || TREE_CODE (stmt) == LABEL_EXPR)
	continue;

      /* If the statement has volatile operands, then we assume we
	 can not thread through this block.  This is overly
	 conservative in some ways.  */
      if (TREE_CODE (stmt) == ASM_EXPR && ASM_VOLATILE_P (stmt))
	return NULL;

      /* If duplicating this block is going to cause too much code
	 expansion, then do not thread through this block.  */
      stmt_count++;
      if (stmt_count > max_stmt_count)
	return NULL;

      /* If this is not a MODIFY_EXPR which sets an SSA_NAME to a new
	 value, then do not try to simplify this statement as it will
	 not simplify in any way that is helpful for jump threading.  */
      if (TREE_CODE (stmt) != MODIFY_EXPR
	  || TREE_CODE (TREE_OPERAND (stmt, 0)) != SSA_NAME)
	continue;

      /* At this point we have a statement which assigns an RHS to an
	 SSA_VAR on the LHS.  We want to try and simplify this statement
	 to expose more context sensitive equivalences which in turn may
	 allow us to simplify the condition at the end of the loop. 

	 Handle simple copy operations as well as implied copies from
	 ASSERT_EXPRs.  */
      if (TREE_CODE (TREE_OPERAND (stmt, 1)) == SSA_NAME)
	cached_lhs = TREE_OPERAND (stmt, 1);
      else if (TREE_CODE (TREE_OPERAND (stmt, 1)) == ASSERT_EXPR)
	cached_lhs = TREE_OPERAND (TREE_OPERAND (stmt, 1), 0);
      else
	{
	  /* A statement that is not a trivial copy or ASSERT_EXPR.
	     We're going to temporarily copy propagate the operands
	     and see if that allows us to simplify this statement.  */
	  tree *copy, pre_fold_expr;
	  ssa_op_iter iter;
	  use_operand_p use_p;
	  unsigned int num, i = 0;

	  num = NUM_SSA_OPERANDS (stmt, (SSA_OP_USE | SSA_OP_VUSE));
	  copy = XCNEWVEC (tree, num);

	  /* Make a copy of the uses & vuses into USES_COPY, then cprop into
	     the operands.  */
	  FOR_EACH_SSA_USE_OPERAND (use_p, stmt, iter, SSA_OP_USE | SSA_OP_VUSE)
	    {
	      tree tmp = NULL;
	      tree use = USE_FROM_PTR (use_p);

	      copy[i++] = use;
	      if (TREE_CODE (use) == SSA_NAME)
		tmp = SSA_NAME_VALUE (use);
	      if (tmp && TREE_CODE (tmp) != VALUE_HANDLE)
		SET_USE (use_p, tmp);
	    }

	  /* Try to fold/lookup the new expression.  Inserting the
	     expression into the hash table is unlikely to help
	     Sadly, we have to handle conditional assignments specially
	     here, because fold expects all the operands of an expression
	     to be folded before the expression itself is folded, but we
	     can't just substitute the folded condition here.  */
	  if (TREE_CODE (TREE_OPERAND (stmt, 1)) == COND_EXPR)
	    {
	      tree cond = COND_EXPR_COND (TREE_OPERAND (stmt, 1));
	      cond = fold (cond);
	      if (cond == boolean_true_node)
		pre_fold_expr = COND_EXPR_THEN (TREE_OPERAND (stmt, 1));
	      else if (cond == boolean_false_node)
		pre_fold_expr = COND_EXPR_ELSE (TREE_OPERAND (stmt, 1));
	      else
		pre_fold_expr = TREE_OPERAND (stmt, 1);
	    }
	  else
	    pre_fold_expr = TREE_OPERAND (stmt, 1);

	  if (pre_fold_expr)
	    {
	      cached_lhs = fold (pre_fold_expr);
	      if (TREE_CODE (cached_lhs) != SSA_NAME
		  && !is_gimple_min_invariant (cached_lhs))
	        cached_lhs = (*simplify) (stmt, stmt);
	    }

	  /* Restore the statement's original uses/defs.  */
	  i = 0;
	  FOR_EACH_SSA_USE_OPERAND (use_p, stmt, iter, SSA_OP_USE | SSA_OP_VUSE)
	    SET_USE (use_p, copy[i++]);

	  free (copy);
	}

      /* Record the context sensitive equivalence if we were able
	 to simplify this statement.  */
      if (cached_lhs
	  && (TREE_CODE (cached_lhs) == SSA_NAME
	      || is_gimple_min_invariant (cached_lhs)))
	record_temporary_equivalence (TREE_OPERAND (stmt, 0),
				      cached_lhs,
				      stack);
    }
  return stmt;
}

/* Simplify the control statement at the end of the block E->dest.

   To avoid allocating memory unnecessarily, a scratch COND_EXPR
   is available to use/clobber in DUMMY_COND.

   Use SIMPLIFY (a pointer to a callback function) to further simplify
   a condition using pass specific information.

   Return the simplified condition or NULL if simplification could
   not be performed.  */

static tree
simplify_control_stmt_condition (edge e,
				 tree stmt,
				 tree dummy_cond,
				 tree (*simplify) (tree, tree),
				 bool handle_dominating_asserts)
{
  tree cond, cached_lhs;

  if (TREE_CODE (stmt) == COND_EXPR)
    cond = COND_EXPR_COND (stmt);
  else if (TREE_CODE (stmt) == GOTO_EXPR)
    cond = GOTO_DESTINATION (stmt);
  else
    cond = SWITCH_COND (stmt);

  /* For comparisons, we have to update both operands, then try
     to simplify the comparison.  */
  if (COMPARISON_CLASS_P (cond))
    {
      tree op0, op1;
      enum tree_code cond_code;

      op0 = TREE_OPERAND (cond, 0);
      op1 = TREE_OPERAND (cond, 1);
      cond_code = TREE_CODE (cond);

      /* Get the current value of both operands.  */
      if (TREE_CODE (op0) == SSA_NAME)
	{
          tree tmp = SSA_NAME_VALUE (op0);
	  if (tmp && TREE_CODE (tmp) != VALUE_HANDLE)
	    op0 = tmp;
	}

      if (TREE_CODE (op1) == SSA_NAME)
	{
	  tree tmp = SSA_NAME_VALUE (op1);
	  if (tmp && TREE_CODE (tmp) != VALUE_HANDLE)
	    op1 = tmp;
	}

      if (handle_dominating_asserts)
	{
	  /* Now see if the operand was consumed by an ASSERT_EXPR
	     which dominates E->src.  If so, we want to replace the
	     operand with the LHS of the ASSERT_EXPR.  */
	  if (TREE_CODE (op0) == SSA_NAME)
	    op0 = lhs_of_dominating_assert (op0, e->src, stmt);

	  if (TREE_CODE (op1) == SSA_NAME)
	    op1 = lhs_of_dominating_assert (op1, e->src, stmt);
	}

      /* We may need to canonicalize the comparison.  For
	 example, op0 might be a constant while op1 is an
	 SSA_NAME.  Failure to canonicalize will cause us to
	 miss threading opportunities.  */
      if (cond_code != SSA_NAME
	  && tree_swap_operands_p (op0, op1, false))
	{
	  tree tmp;
	  cond_code = swap_tree_comparison (TREE_CODE (cond));
	  tmp = op0;
	  op0 = op1;
	  op1 = tmp;
	}

      /* Stuff the operator and operands into our dummy conditional
	 expression.  */
      TREE_SET_CODE (COND_EXPR_COND (dummy_cond), cond_code);
      TREE_OPERAND (COND_EXPR_COND (dummy_cond), 0) = op0;
      TREE_OPERAND (COND_EXPR_COND (dummy_cond), 1) = op1;

      /* We absolutely do not care about any type conversions
         we only care about a zero/nonzero value.  */
      fold_defer_overflow_warnings ();

      cached_lhs = fold (COND_EXPR_COND (dummy_cond));
      while (TREE_CODE (cached_lhs) == NOP_EXPR
	     || TREE_CODE (cached_lhs) == CONVERT_EXPR
	     || TREE_CODE (cached_lhs) == NON_LVALUE_EXPR)
	cached_lhs = TREE_OPERAND (cached_lhs, 0);

      fold_undefer_overflow_warnings (is_gimple_min_invariant (cached_lhs),
				      stmt, WARN_STRICT_OVERFLOW_CONDITIONAL);

      /* If we have not simplified the condition down to an invariant,
	 then use the pass specific callback to simplify the condition.  */
      if (! is_gimple_min_invariant (cached_lhs))
	cached_lhs = (*simplify) (dummy_cond, stmt);
    }

  /* We can have conditionals which just test the state of a variable
     rather than use a relational operator.  These are simpler to handle.  */
  else if (TREE_CODE (cond) == SSA_NAME)
    {
      cached_lhs = cond;

      /* Get the variable's current value from the equivalency chains.

	 It is possible to get loops in the SSA_NAME_VALUE chains
	 (consider threading the backedge of a loop where we have
	 a loop invariant SSA_NAME used in the condition.  */
      if (cached_lhs
	  && TREE_CODE (cached_lhs) == SSA_NAME
	  && SSA_NAME_VALUE (cached_lhs))
	cached_lhs = SSA_NAME_VALUE (cached_lhs);

      /* If we're dominated by a suitable ASSERT_EXPR, then
	 update CACHED_LHS appropriately.  */
      if (handle_dominating_asserts && TREE_CODE (cached_lhs) == SSA_NAME)
	cached_lhs = lhs_of_dominating_assert (cached_lhs, e->src, stmt);

      /* If we haven't simplified to an invariant yet, then use the
	 pass specific callback to try and simplify it further.  */
      if (cached_lhs && ! is_gimple_min_invariant (cached_lhs))
        cached_lhs = (*simplify) (stmt, stmt);
    }
  else
    cached_lhs = NULL;

  return cached_lhs;
}

/* We are exiting E->src, see if E->dest ends with a conditional
   jump which has a known value when reached via E. 

   Special care is necessary if E is a back edge in the CFG as we
   may have already recorded equivalences for E->dest into our
   various tables, including the result of the conditional at
   the end of E->dest.  Threading opportunities are severely
   limited in that case to avoid short-circuiting the loop
   incorrectly.

   Note it is quite common for the first block inside a loop to
   end with a conditional which is either always true or always
   false when reached via the loop backedge.  Thus we do not want
   to blindly disable threading across a loop backedge.  */

void
thread_across_edge (tree dummy_cond,
		    edge e,
		    bool handle_dominating_asserts,
		    VEC(tree, heap) **stack,
		    tree (*simplify) (tree, tree))
{
  tree stmt;

  /* If E is a backedge, then we want to verify that the COND_EXPR,
     SWITCH_EXPR or GOTO_EXPR at the end of e->dest is not affected
     by any statements in e->dest.  If it is affected, then it is not
     safe to thread this edge.  */
  if (e->flags & EDGE_DFS_BACK)
    {
      ssa_op_iter iter;
      use_operand_p use_p;
      tree last = bsi_stmt (bsi_last (e->dest));

      FOR_EACH_SSA_USE_OPERAND (use_p, last, iter, SSA_OP_USE | SSA_OP_VUSE)
	{
	  tree use = USE_FROM_PTR (use_p);

          if (TREE_CODE (use) == SSA_NAME
	      && TREE_CODE (SSA_NAME_DEF_STMT (use)) != PHI_NODE
	      && bb_for_stmt (SSA_NAME_DEF_STMT (use)) == e->dest)
	    goto fail;
	}
    }
     
  stmt_count = 0;

  /* PHIs create temporary equivalences.  */
  if (!record_temporary_equivalences_from_phis (e, stack))
    goto fail;

  /* Now walk each statement recording any context sensitive
     temporary equivalences we can detect.  */
  stmt = record_temporary_equivalences_from_stmts_at_dest (e, stack, simplify);
  if (!stmt)
    goto fail;

  /* If we stopped at a COND_EXPR or SWITCH_EXPR, see if we know which arm
     will be taken.  */
  if (TREE_CODE (stmt) == COND_EXPR
      || TREE_CODE (stmt) == GOTO_EXPR
      || TREE_CODE (stmt) == SWITCH_EXPR)
    {
      tree cond;

      /* Extract and simplify the condition.  */
      cond = simplify_control_stmt_condition (e, stmt, dummy_cond, simplify, handle_dominating_asserts);

      if (cond && is_gimple_min_invariant (cond))
	{
	  edge taken_edge = find_taken_edge (e->dest, cond);
	  basic_block dest = (taken_edge ? taken_edge->dest : NULL);

	  if (dest == e->dest)
	    goto fail;

	  remove_temporary_equivalences (stack);
	  register_jump_thread (e, taken_edge);
	}
    }

 fail:
  remove_temporary_equivalences (stack);
}
