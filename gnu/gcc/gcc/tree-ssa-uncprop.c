/* Routines for discovering and unpropagating edge equivalences.
   Copyright (C) 2005 Free Software Foundation, Inc.

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

/* The basic structure describing an equivalency created by traversing
   an edge.  Traversing the edge effectively means that we can assume
   that we've seen an assignment LHS = RHS.  */
struct edge_equivalency
{
  tree rhs;
  tree lhs;
};

/* This routine finds and records edge equivalences for every edge
   in the CFG.

   When complete, each edge that creates an equivalency will have an
   EDGE_EQUIVALENCY structure hanging off the edge's AUX field. 
   The caller is responsible for freeing the AUX fields.  */

static void
associate_equivalences_with_edges (void)
{
  basic_block bb;

  /* Walk over each block.  If the block ends with a control statement,
     then it might create a useful equivalence.  */
  FOR_EACH_BB (bb)
    {
      block_stmt_iterator bsi = bsi_last (bb);
      tree stmt;

      /* If the block does not end with a COND_EXPR or SWITCH_EXPR
	 then there is nothing to do.  */
      if (bsi_end_p (bsi))
	continue;

      stmt = bsi_stmt (bsi);

      if (!stmt)
	continue;

      /* A COND_EXPR may create an equivalency in a variety of different
	 ways.  */
      if (TREE_CODE (stmt) == COND_EXPR)
	{
	  tree cond = COND_EXPR_COND (stmt);
	  edge true_edge;
	  edge false_edge;
	  struct edge_equivalency *equivalency;

	  extract_true_false_edges_from_block (bb, &true_edge, &false_edge);

	  /* If the conditional is a single variable 'X', record 'X = 1'
	     for the true edge and 'X = 0' on the false edge.  */
	  if (TREE_CODE (cond) == SSA_NAME
	      && !SSA_NAME_OCCURS_IN_ABNORMAL_PHI (cond))
	    {
	      equivalency = XNEW (struct edge_equivalency);
	      equivalency->rhs = constant_boolean_node (1, TREE_TYPE (cond));
	      equivalency->lhs = cond;
	      true_edge->aux = equivalency;

	      equivalency = XNEW (struct edge_equivalency);
	      equivalency->rhs = constant_boolean_node (0, TREE_TYPE (cond));
	      equivalency->lhs = cond;
	      false_edge->aux = equivalency;
	    }
	  /* Equality tests may create one or two equivalences.  */
	  else if (TREE_CODE (cond) == EQ_EXPR || TREE_CODE (cond) == NE_EXPR)
	    {
	      tree op0 = TREE_OPERAND (cond, 0);
	      tree op1 = TREE_OPERAND (cond, 1);

	      /* Special case comparing booleans against a constant as we
		 know the value of OP0 on both arms of the branch.  i.e., we
		 can record an equivalence for OP0 rather than COND.  */
	      if (TREE_CODE (op0) == SSA_NAME
		  && !SSA_NAME_OCCURS_IN_ABNORMAL_PHI (op0)
		  && TREE_CODE (TREE_TYPE (op0)) == BOOLEAN_TYPE
		  && is_gimple_min_invariant (op1))
		{
		  if (TREE_CODE (cond) == EQ_EXPR)
		    {
		      equivalency = XNEW (struct edge_equivalency);
		      equivalency->lhs = op0;
		      equivalency->rhs = (integer_zerop (op1)
					  ? boolean_false_node
					  : boolean_true_node);
		      true_edge->aux = equivalency;

		      equivalency = XNEW (struct edge_equivalency);
		      equivalency->lhs = op0;
		      equivalency->rhs = (integer_zerop (op1)
					  ? boolean_true_node
					  : boolean_false_node);
		      false_edge->aux = equivalency;
		    }
		  else
		    {
		      equivalency = XNEW (struct edge_equivalency);
		      equivalency->lhs = op0;
		      equivalency->rhs = (integer_zerop (op1)
					  ? boolean_true_node
					  : boolean_false_node);
		      true_edge->aux = equivalency;

		      equivalency = XNEW (struct edge_equivalency);
		      equivalency->lhs = op0;
		      equivalency->rhs = (integer_zerop (op1)
					  ? boolean_false_node
					  : boolean_true_node);
		      false_edge->aux = equivalency;
		    }
		}

	      if (TREE_CODE (op0) == SSA_NAME
		  && !SSA_NAME_OCCURS_IN_ABNORMAL_PHI (op0)
		  && (is_gimple_min_invariant (op1)
		      || (TREE_CODE (op1) == SSA_NAME
			  && !SSA_NAME_OCCURS_IN_ABNORMAL_PHI (op1))))
		{
		  /* For IEEE, -0.0 == 0.0, so we don't necessarily know
		     the sign of a variable compared against zero.  If
		     we're honoring signed zeros, then we cannot record
		     this value unless we know that the value is nonzero.  */
		  if (HONOR_SIGNED_ZEROS (TYPE_MODE (TREE_TYPE (op0)))
		      && (TREE_CODE (op1) != REAL_CST
			  || REAL_VALUES_EQUAL (dconst0, TREE_REAL_CST (op1))))
		    continue;

		  equivalency = XNEW (struct edge_equivalency);
		  equivalency->lhs = op0;
		  equivalency->rhs = op1;
		  if (TREE_CODE (cond) == EQ_EXPR)
		    true_edge->aux = equivalency;
		  else 
		    false_edge->aux = equivalency;

		}
	    }

	  /* ??? TRUTH_NOT_EXPR can create an equivalence too.  */
	}

      /* For a SWITCH_EXPR, a case label which represents a single
	 value and which is the only case label which reaches the
	 target block creates an equivalence.  */
      if (TREE_CODE (stmt) == SWITCH_EXPR)
	{
	  tree cond = SWITCH_COND (stmt);

	  if (TREE_CODE (cond) == SSA_NAME
	      && !SSA_NAME_OCCURS_IN_ABNORMAL_PHI (cond))
	    {
	      tree labels = SWITCH_LABELS (stmt);
	      int i, n_labels = TREE_VEC_LENGTH (labels);
	      tree *info = XCNEWVEC (tree, n_basic_blocks);

	      /* Walk over the case label vector.  Record blocks
		 which are reached by a single case label which represents
		 a single value.  */
	      for (i = 0; i < n_labels; i++)
		{
		  tree label = TREE_VEC_ELT (labels, i);
		  basic_block bb = label_to_block (CASE_LABEL (label));


		  if (CASE_HIGH (label)
		      || !CASE_LOW (label)
		      || info[bb->index])
		    info[bb->index] = error_mark_node;
		  else
		    info[bb->index] = label;
		}

	      /* Now walk over the blocks to determine which ones were
		 marked as being reached by a useful case label.  */
	      for (i = 0; i < n_basic_blocks; i++)
		{
		  tree node = info[i];

		  if (node != NULL
		      && node != error_mark_node)
		    {
		      tree x = fold_convert (TREE_TYPE (cond), CASE_LOW (node));
		      struct edge_equivalency *equivalency;

		      /* Record an equivalency on the edge from BB to basic
			 block I.  */
		      equivalency = XNEW (struct edge_equivalency);
		      equivalency->rhs = x;
		      equivalency->lhs = cond;
		      find_edge (bb, BASIC_BLOCK (i))->aux = equivalency;
		    }
		}
	      free (info);
	    }
	}

    }
}


/* Translating out of SSA sometimes requires inserting copies and
   constant initializations on edges to eliminate PHI nodes.

   In some cases those copies and constant initializations are
   redundant because the target already has the value on the
   RHS of the assignment.

   We previously tried to catch these cases after translating
   out of SSA form.  However, that code often missed cases.  Worse
   yet, the cases it missed were also often missed by the RTL
   optimizers.  Thus the resulting code had redundant instructions.

   This pass attempts to detect these situations before translating
   out of SSA form.

   The key concept that this pass is built upon is that these
   redundant copies and constant initializations often occur
   due to constant/copy propagating equivalences resulting from
   COND_EXPRs and SWITCH_EXPRs.

   We want to do those propagations as they can sometimes allow
   the SSA optimizers to do a better job.  However, in the cases
   where such propagations do not result in further optimization,
   we would like to "undo" the propagation to avoid the redundant
   copies and constant initializations.

   This pass works by first associating equivalences with edges in
   the CFG.  For example, the edge leading from a SWITCH_EXPR to
   its associated CASE_LABEL will have an equivalency between
   SWITCH_COND and the value in the case label.

   Once we have found the edge equivalences, we proceed to walk
   the CFG in dominator order.  As we traverse edges we record
   equivalences associated with those edges we traverse.

   When we encounter a PHI node, we walk its arguments to see if we
   have an equivalence for the PHI argument.  If so, then we replace
   the argument.

   Equivalences are looked up based on their value (think of it as
   the RHS of an assignment).   A value may be an SSA_NAME or an
   invariant.  We may have several SSA_NAMEs with the same value,
   so with each value we have a list of SSA_NAMEs that have the
   same value.  */

/* As we enter each block we record the value for any edge equivalency
   leading to this block.  If no such edge equivalency exists, then we
   record NULL.  These equivalences are live until we leave the dominator
   subtree rooted at the block where we record the equivalency.  */
static VEC(tree,heap) *equiv_stack;

/* Global hash table implementing a mapping from invariant values
   to a list of SSA_NAMEs which have the same value.  We might be
   able to reuse tree-vn for this code.  */
static htab_t equiv;

/* Main structure for recording equivalences into our hash table.  */
struct equiv_hash_elt
{
  /* The value/key of this entry.  */
  tree value;

  /* List of SSA_NAMEs which have the same value/key.  */
  VEC(tree,heap) *equivalences;
};

static void uncprop_initialize_block (struct dom_walk_data *, basic_block);
static void uncprop_finalize_block (struct dom_walk_data *, basic_block);
static void uncprop_into_successor_phis (struct dom_walk_data *, basic_block);

/* Hashing and equality routines for the hash table.  */

static hashval_t
equiv_hash (const void *p)
{
  tree value = ((struct equiv_hash_elt *)p)->value;
  return iterative_hash_expr (value, 0);
}

static int
equiv_eq (const void *p1, const void *p2)
{
  tree value1 = ((struct equiv_hash_elt *)p1)->value;
  tree value2 = ((struct equiv_hash_elt *)p2)->value;

  return operand_equal_p (value1, value2, 0);
}

/* Free an instance of equiv_hash_elt.  */

static void
equiv_free (void *p)
{
  struct equiv_hash_elt *elt = (struct equiv_hash_elt *) p;
  VEC_free (tree, heap, elt->equivalences);
  free (elt);
}

/* Remove the most recently recorded equivalency for VALUE.  */

static void
remove_equivalence (tree value)
{
  struct equiv_hash_elt equiv_hash_elt, *equiv_hash_elt_p;
  void **slot;

  equiv_hash_elt.value = value;
  equiv_hash_elt.equivalences = NULL;

  slot = htab_find_slot (equiv, &equiv_hash_elt, NO_INSERT);

  equiv_hash_elt_p = (struct equiv_hash_elt *) *slot;
  VEC_pop (tree, equiv_hash_elt_p->equivalences);
}

/* Record EQUIVALENCE = VALUE into our hash table.  */

static void
record_equiv (tree value, tree equivalence)
{
  struct equiv_hash_elt *equiv_hash_elt;
  void **slot;

  equiv_hash_elt = XNEW (struct equiv_hash_elt);
  equiv_hash_elt->value = value;
  equiv_hash_elt->equivalences = NULL;

  slot = htab_find_slot (equiv, equiv_hash_elt, INSERT);

  if (*slot == NULL)
    *slot = (void *) equiv_hash_elt;
  else
     free (equiv_hash_elt);

  equiv_hash_elt = (struct equiv_hash_elt *) *slot;
  
  VEC_safe_push (tree, heap, equiv_hash_elt->equivalences, equivalence);
}

/* Main driver for un-cprop.  */

static unsigned int
tree_ssa_uncprop (void)
{
  struct dom_walk_data walk_data;
  basic_block bb;

  associate_equivalences_with_edges ();

  /* Create our global data structures.  */
  equiv = htab_create (1024, equiv_hash, equiv_eq, equiv_free);
  equiv_stack = VEC_alloc (tree, heap, 2);

  /* We're going to do a dominator walk, so ensure that we have
     dominance information.  */
  calculate_dominance_info (CDI_DOMINATORS);

  /* Setup callbacks for the generic dominator tree walker.  */
  walk_data.walk_stmts_backward = false;
  walk_data.dom_direction = CDI_DOMINATORS;
  walk_data.initialize_block_local_data = NULL;
  walk_data.before_dom_children_before_stmts = uncprop_initialize_block;
  walk_data.before_dom_children_walk_stmts = NULL;
  walk_data.before_dom_children_after_stmts = uncprop_into_successor_phis;
  walk_data.after_dom_children_before_stmts = NULL;
  walk_data.after_dom_children_walk_stmts = NULL;
  walk_data.after_dom_children_after_stmts = uncprop_finalize_block;
  walk_data.global_data = NULL;
  walk_data.block_local_data_size = 0;
  walk_data.interesting_blocks = NULL;

  /* Now initialize the dominator walker.  */
  init_walk_dominator_tree (&walk_data);

  /* Recursively walk the dominator tree undoing unprofitable
     constant/copy propagations.  */
  walk_dominator_tree (&walk_data, ENTRY_BLOCK_PTR);

  /* Finalize and clean up.  */
  fini_walk_dominator_tree (&walk_data);

  /* EQUIV_STACK should already be empty at this point, so we just
     need to empty elements out of the hash table, free EQUIV_STACK,
     and cleanup the AUX field on the edges.  */
  htab_delete (equiv);
  VEC_free (tree, heap, equiv_stack);
  FOR_EACH_BB (bb)
    {
      edge e;
      edge_iterator ei;

      FOR_EACH_EDGE (e, ei, bb->succs)
	{
	  if (e->aux)
	    {
	      free (e->aux);
	      e->aux = NULL;
	    }
	}
    }
  return 0;
}


/* We have finished processing the dominator children of BB, perform
   any finalization actions in preparation for leaving this node in
   the dominator tree.  */

static void
uncprop_finalize_block (struct dom_walk_data *walk_data ATTRIBUTE_UNUSED,
			basic_block bb ATTRIBUTE_UNUSED)
{
  /* Pop the topmost value off the equiv stack.  */
  tree value = VEC_pop (tree, equiv_stack);

  /* If that value was non-null, then pop the topmost equivalency off
     its equivalency stack.  */
  if (value != NULL)
    remove_equivalence (value);
}

/* Unpropagate values from PHI nodes in successor blocks of BB.  */

static void
uncprop_into_successor_phis (struct dom_walk_data *walk_data ATTRIBUTE_UNUSED,
			     basic_block bb)
{
  edge e;
  edge_iterator ei;

  /* For each successor edge, first temporarily record any equivalence
     on that edge.  Then unpropagate values in any PHI nodes at the
     destination of the edge.  Then remove the temporary equivalence.  */
  FOR_EACH_EDGE (e, ei, bb->succs)
    {
      tree phi = phi_nodes (e->dest);

      /* If there are no PHI nodes in this destination, then there is
	 no sense in recording any equivalences.  */
      if (!phi)
	continue;

      /* Record any equivalency associated with E.  */
      if (e->aux)
	{
	  struct edge_equivalency *equiv = (struct edge_equivalency *) e->aux;
	  record_equiv (equiv->rhs, equiv->lhs);
	}

      /* Walk over the PHI nodes, unpropagating values.  */
      for ( ; phi; phi = PHI_CHAIN (phi))
	{
	  /* Sigh.  We'll have more efficient access to this one day.  */
	  tree arg = PHI_ARG_DEF (phi, e->dest_idx);
	  struct equiv_hash_elt equiv_hash_elt;
	  void **slot;

	  /* If the argument is not an invariant, or refers to the same
	     underlying variable as the PHI result, then there's no
	     point in un-propagating the argument.  */
	  if (!is_gimple_min_invariant (arg)
	      && SSA_NAME_VAR (arg) != SSA_NAME_VAR (PHI_RESULT (phi)))
	    continue;

	  /* Lookup this argument's value in the hash table.  */
	  equiv_hash_elt.value = arg;
	  equiv_hash_elt.equivalences = NULL;
	  slot = htab_find_slot (equiv, &equiv_hash_elt, NO_INSERT);

	  if (slot)
	    {
	      struct equiv_hash_elt *elt = (struct equiv_hash_elt *) *slot;
	      int j;

	      /* Walk every equivalence with the same value.  If we find
		 one with the same underlying variable as the PHI result,
		 then replace the value in the argument with its equivalent
		 SSA_NAME.  Use the most recent equivalence as hopefully
		 that results in shortest lifetimes.  */
	      for (j = VEC_length (tree, elt->equivalences) - 1; j >= 0; j--)
		{
		  tree equiv = VEC_index (tree, elt->equivalences, j);

		  if (SSA_NAME_VAR (equiv) == SSA_NAME_VAR (PHI_RESULT (phi)))
		    {
		      SET_PHI_ARG_DEF (phi, e->dest_idx, equiv);
		      break;
		    }
		}
	    }
	}

      /* If we had an equivalence associated with this edge, remove it.  */
      if (e->aux)
	{
	  struct edge_equivalency *equiv = (struct edge_equivalency *) e->aux;
	  remove_equivalence (equiv->rhs);
	}
    }
}

/* Ignoring loop backedges, if BB has precisely one incoming edge then
   return that edge.  Otherwise return NULL.  */
static edge
single_incoming_edge_ignoring_loop_edges (basic_block bb)
{
  edge retval = NULL;
  edge e;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, bb->preds)
    {
      /* A loop back edge can be identified by the destination of
	 the edge dominating the source of the edge.  */
      if (dominated_by_p (CDI_DOMINATORS, e->src, e->dest))
	continue;

      /* If we have already seen a non-loop edge, then we must have
	 multiple incoming non-loop edges and thus we return NULL.  */
      if (retval)
	return NULL;

      /* This is the first non-loop incoming edge we have found.  Record
	 it.  */
      retval = e;
    }

  return retval;
}

static void
uncprop_initialize_block (struct dom_walk_data *walk_data ATTRIBUTE_UNUSED,
			  basic_block bb)
{
  basic_block parent;
  edge e;
  bool recorded = false;

  /* If this block is dominated by a single incoming edge and that edge
     has an equivalency, then record the equivalency and push the
     VALUE onto EQUIV_STACK.  Else push a NULL entry on EQUIV_STACK.  */
  parent = get_immediate_dominator (CDI_DOMINATORS, bb);
  if (parent)
    {
      e = single_incoming_edge_ignoring_loop_edges (bb);

      if (e && e->src == parent && e->aux)
	{
	  struct edge_equivalency *equiv = (struct edge_equivalency *) e->aux;

	  record_equiv (equiv->rhs, equiv->lhs);
	  VEC_safe_push (tree, heap, equiv_stack, equiv->rhs);
	  recorded = true;
	}
    }

  if (!recorded)
    VEC_safe_push (tree, heap, equiv_stack, NULL_TREE);
}

static bool
gate_uncprop (void)
{
  return flag_tree_dom != 0;
}

struct tree_opt_pass pass_uncprop = 
{
  "uncprop",				/* name */
  gate_uncprop,				/* gate */
  tree_ssa_uncprop,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_SSA_UNCPROP,			/* tv_id */
  PROP_cfg | PROP_ssa,			/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func | TODO_verify_ssa,	/* todo_flags_finish */
  0					/* letter */
};
