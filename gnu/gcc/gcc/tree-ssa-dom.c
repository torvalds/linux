/* SSA Dominator optimizations for trees
   Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by Diego Novillo <dnovillo@redhat.com>

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

/* This file implements optimizations on the dominator tree.  */


/* Structure for recording edge equivalences as well as any pending
   edge redirections during the dominator optimizer.

   Computing and storing the edge equivalences instead of creating
   them on-demand can save significant amounts of time, particularly
   for pathological cases involving switch statements.  

   These structures live for a single iteration of the dominator
   optimizer in the edge's AUX field.  At the end of an iteration we
   free each of these structures and update the AUX field to point
   to any requested redirection target (the code for updating the
   CFG and SSA graph for edge redirection expects redirection edge
   targets to be in the AUX field for each edge.  */

struct edge_info
{
  /* If this edge creates a simple equivalence, the LHS and RHS of
     the equivalence will be stored here.  */
  tree lhs;
  tree rhs;

  /* Traversing an edge may also indicate one or more particular conditions
     are true or false.  The number of recorded conditions can vary, but
     can be determined by the condition's code.  So we have an array
     and its maximum index rather than use a varray.  */
  tree *cond_equivalences;
  unsigned int max_cond_equivalences;
};


/* Hash table with expressions made available during the renaming process.
   When an assignment of the form X_i = EXPR is found, the statement is
   stored in this table.  If the same expression EXPR is later found on the
   RHS of another statement, it is replaced with X_i (thus performing
   global redundancy elimination).  Similarly as we pass through conditionals
   we record the conditional itself as having either a true or false value
   in this table.  */
static htab_t avail_exprs;

/* Stack of available expressions in AVAIL_EXPRs.  Each block pushes any
   expressions it enters into the hash table along with a marker entry
   (null).  When we finish processing the block, we pop off entries and
   remove the expressions from the global hash table until we hit the
   marker.  */
static VEC(tree,heap) *avail_exprs_stack;

/* Stack of statements we need to rescan during finalization for newly
   exposed variables.

   Statement rescanning must occur after the current block's available
   expressions are removed from AVAIL_EXPRS.  Else we may change the
   hash code for an expression and be unable to find/remove it from
   AVAIL_EXPRS.  */
static VEC(tree,heap) *stmts_to_rescan;

/* Structure for entries in the expression hash table.

   This requires more memory for the hash table entries, but allows us
   to avoid creating silly tree nodes and annotations for conditionals,
   eliminates 2 global hash tables and two block local varrays.
   
   It also allows us to reduce the number of hash table lookups we
   have to perform in lookup_avail_expr and finally it allows us to
   significantly reduce the number of calls into the hashing routine
   itself.  */

struct expr_hash_elt
{
  /* The value (lhs) of this expression.  */
  tree lhs;

  /* The expression (rhs) we want to record.  */
  tree rhs;

  /* The stmt pointer if this element corresponds to a statement.  */
  tree stmt;

  /* The hash value for RHS/ann.  */
  hashval_t hash;
};

/* Stack of dest,src pairs that need to be restored during finalization.

   A NULL entry is used to mark the end of pairs which need to be
   restored during finalization of this block.  */
static VEC(tree,heap) *const_and_copies_stack;

/* Track whether or not we have changed the control flow graph.  */
static bool cfg_altered;

/* Bitmap of blocks that have had EH statements cleaned.  We should
   remove their dead edges eventually.  */
static bitmap need_eh_cleanup;

/* Statistics for dominator optimizations.  */
struct opt_stats_d
{
  long num_stmts;
  long num_exprs_considered;
  long num_re;
  long num_const_prop;
  long num_copy_prop;
};

static struct opt_stats_d opt_stats;

struct eq_expr_value
{
  tree src;
  tree dst;
};

/* Local functions.  */
static void optimize_stmt (struct dom_walk_data *, 
			   basic_block bb,
			   block_stmt_iterator);
static tree lookup_avail_expr (tree, bool);
static hashval_t avail_expr_hash (const void *);
static hashval_t real_avail_expr_hash (const void *);
static int avail_expr_eq (const void *, const void *);
static void htab_statistics (FILE *, htab_t);
static void record_cond (tree, tree);
static void record_const_or_copy (tree, tree);
static void record_equality (tree, tree);
static void record_equivalences_from_phis (basic_block);
static void record_equivalences_from_incoming_edge (basic_block);
static bool eliminate_redundant_computations (tree);
static void record_equivalences_from_stmt (tree, int, stmt_ann_t);
static void dom_thread_across_edge (struct dom_walk_data *, edge);
static void dom_opt_finalize_block (struct dom_walk_data *, basic_block);
static void dom_opt_initialize_block (struct dom_walk_data *, basic_block);
static void propagate_to_outgoing_edges (struct dom_walk_data *, basic_block);
static void remove_local_expressions_from_table (void);
static void restore_vars_to_original_value (void);
static edge single_incoming_edge_ignoring_loop_edges (basic_block);


/* Allocate an EDGE_INFO for edge E and attach it to E.
   Return the new EDGE_INFO structure.  */

static struct edge_info *
allocate_edge_info (edge e)
{
  struct edge_info *edge_info;

  edge_info = XCNEW (struct edge_info);

  e->aux = edge_info;
  return edge_info;
}

/* Free all EDGE_INFO structures associated with edges in the CFG.
   If a particular edge can be threaded, copy the redirection
   target from the EDGE_INFO structure into the edge's AUX field
   as required by code to update the CFG and SSA graph for
   jump threading.  */

static void
free_all_edge_infos (void)
{
  basic_block bb;
  edge_iterator ei;
  edge e;

  FOR_EACH_BB (bb)
    {
      FOR_EACH_EDGE (e, ei, bb->preds)
        {
	 struct edge_info *edge_info = (struct edge_info *) e->aux;

	  if (edge_info)
	    {
	      if (edge_info->cond_equivalences)
		free (edge_info->cond_equivalences);
	      free (edge_info);
	      e->aux = NULL;
	    }
	}
    }
}

/* Jump threading, redundancy elimination and const/copy propagation. 

   This pass may expose new symbols that need to be renamed into SSA.  For
   every new symbol exposed, its corresponding bit will be set in
   VARS_TO_RENAME.  */

static unsigned int
tree_ssa_dominator_optimize (void)
{
  struct dom_walk_data walk_data;
  unsigned int i;
  struct loops loops_info;

  memset (&opt_stats, 0, sizeof (opt_stats));

  /* Create our hash tables.  */
  avail_exprs = htab_create (1024, real_avail_expr_hash, avail_expr_eq, free);
  avail_exprs_stack = VEC_alloc (tree, heap, 20);
  const_and_copies_stack = VEC_alloc (tree, heap, 20);
  stmts_to_rescan = VEC_alloc (tree, heap, 20);
  need_eh_cleanup = BITMAP_ALLOC (NULL);

  /* Setup callbacks for the generic dominator tree walker.  */
  walk_data.walk_stmts_backward = false;
  walk_data.dom_direction = CDI_DOMINATORS;
  walk_data.initialize_block_local_data = NULL;
  walk_data.before_dom_children_before_stmts = dom_opt_initialize_block;
  walk_data.before_dom_children_walk_stmts = optimize_stmt;
  walk_data.before_dom_children_after_stmts = propagate_to_outgoing_edges;
  walk_data.after_dom_children_before_stmts = NULL;
  walk_data.after_dom_children_walk_stmts = NULL;
  walk_data.after_dom_children_after_stmts = dom_opt_finalize_block;
  /* Right now we only attach a dummy COND_EXPR to the global data pointer.
     When we attach more stuff we'll need to fill this out with a real
     structure.  */
  walk_data.global_data = NULL;
  walk_data.block_local_data_size = 0;
  walk_data.interesting_blocks = NULL;

  /* Now initialize the dominator walker.  */
  init_walk_dominator_tree (&walk_data);

  calculate_dominance_info (CDI_DOMINATORS);

  /* We need to know which edges exit loops so that we can
     aggressively thread through loop headers to an exit
     edge.  */
  flow_loops_find (&loops_info);
  mark_loop_exit_edges (&loops_info);
  flow_loops_free (&loops_info);

  /* Clean up the CFG so that any forwarder blocks created by loop
     canonicalization are removed.  */
  cleanup_tree_cfg ();
  calculate_dominance_info (CDI_DOMINATORS);

  /* We need accurate information regarding back edges in the CFG
     for jump threading.  */
  mark_dfs_back_edges ();

  /* Recursively walk the dominator tree optimizing statements.  */
  walk_dominator_tree (&walk_data, ENTRY_BLOCK_PTR);

  {
    block_stmt_iterator bsi;
    basic_block bb;
    FOR_EACH_BB (bb)
      {
	for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	  update_stmt_if_modified (bsi_stmt (bsi));
      }
  }

  /* If we exposed any new variables, go ahead and put them into
     SSA form now, before we handle jump threading.  This simplifies
     interactions between rewriting of _DECL nodes into SSA form
     and rewriting SSA_NAME nodes into SSA form after block
     duplication and CFG manipulation.  */
  update_ssa (TODO_update_ssa);

  free_all_edge_infos ();

  /* Thread jumps, creating duplicate blocks as needed.  */
  cfg_altered |= thread_through_all_blocks ();

  /* Removal of statements may make some EH edges dead.  Purge
     such edges from the CFG as needed.  */
  if (!bitmap_empty_p (need_eh_cleanup))
    {
      cfg_altered |= tree_purge_all_dead_eh_edges (need_eh_cleanup);
      bitmap_zero (need_eh_cleanup);
    }

  if (cfg_altered)
    free_dominance_info (CDI_DOMINATORS);

  /* Finally, remove everything except invariants in SSA_NAME_VALUE.

     Long term we will be able to let everything in SSA_NAME_VALUE
     persist.  However, for now, we know this is the safe thing to do.  */
  for (i = 0; i < num_ssa_names; i++)
   {
      tree name = ssa_name (i);
      tree value;

      if (!name)
        continue;

      value = SSA_NAME_VALUE (name);
      if (value && !is_gimple_min_invariant (value))
	SSA_NAME_VALUE (name) = NULL;
    }

  /* Debugging dumps.  */
  if (dump_file && (dump_flags & TDF_STATS))
    dump_dominator_optimization_stats (dump_file);

  /* Delete our main hashtable.  */
  htab_delete (avail_exprs);

  /* And finalize the dominator walker.  */
  fini_walk_dominator_tree (&walk_data);

  /* Free asserted bitmaps and stacks.  */
  BITMAP_FREE (need_eh_cleanup);
  
  VEC_free (tree, heap, avail_exprs_stack);
  VEC_free (tree, heap, const_and_copies_stack);
  VEC_free (tree, heap, stmts_to_rescan);
  return 0;
}

static bool
gate_dominator (void)
{
  return flag_tree_dom != 0;
}

struct tree_opt_pass pass_dominator = 
{
  "dom",				/* name */
  gate_dominator,			/* gate */
  tree_ssa_dominator_optimize,		/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_SSA_DOMINATOR_OPTS,		/* tv_id */
  PROP_cfg | PROP_ssa | PROP_alias,	/* properties_required */
  0,					/* properties_provided */
  PROP_smt_usage,			/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func
    | TODO_update_ssa
    | TODO_cleanup_cfg
    | TODO_verify_ssa	
    | TODO_update_smt_usage,		/* todo_flags_finish */
  0					/* letter */
};


/* Given a stmt CONDSTMT containing a COND_EXPR, canonicalize the
   COND_EXPR into a canonical form.  */

static void
canonicalize_comparison (tree condstmt)
{
  tree cond = COND_EXPR_COND (condstmt);
  tree op0;
  tree op1;
  enum tree_code code = TREE_CODE (cond);

  if (!COMPARISON_CLASS_P (cond))
    return;

  op0 = TREE_OPERAND (cond, 0);
  op1 = TREE_OPERAND (cond, 1);

  /* If it would be profitable to swap the operands, then do so to
     canonicalize the statement, enabling better optimization.

     By placing canonicalization of such expressions here we
     transparently keep statements in canonical form, even
     when the statement is modified.  */
  if (tree_swap_operands_p (op0, op1, false))
    {
      /* For relationals we need to swap the operands
	 and change the code.  */
      if (code == LT_EXPR
	  || code == GT_EXPR
	  || code == LE_EXPR
	  || code == GE_EXPR)
	{
	  TREE_SET_CODE (cond, swap_tree_comparison (code));
	  swap_tree_operands (condstmt,
			      &TREE_OPERAND (cond, 0),
			      &TREE_OPERAND (cond, 1));
	  /* If one operand was in the operand cache, but the other is
	     not, because it is a constant, this is a case that the
	     internal updating code of swap_tree_operands can't handle
	     properly.  */
	  if (TREE_CODE_CLASS (TREE_CODE (op0)) 
	      != TREE_CODE_CLASS (TREE_CODE (op1)))
	    update_stmt (condstmt);
	}
    }
}

/* Initialize local stacks for this optimizer and record equivalences
   upon entry to BB.  Equivalences can come from the edge traversed to
   reach BB or they may come from PHI nodes at the start of BB.  */

static void
dom_opt_initialize_block (struct dom_walk_data *walk_data ATTRIBUTE_UNUSED,
			  basic_block bb)
{
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\n\nOptimizing block #%d\n\n", bb->index);

  /* Push a marker on the stacks of local information so that we know how
     far to unwind when we finalize this block.  */
  VEC_safe_push (tree, heap, avail_exprs_stack, NULL_TREE);
  VEC_safe_push (tree, heap, const_and_copies_stack, NULL_TREE);

  record_equivalences_from_incoming_edge (bb);

  /* PHI nodes can create equivalences too.  */
  record_equivalences_from_phis (bb);
}

/* Given an expression EXPR (a relational expression or a statement), 
   initialize the hash table element pointed to by ELEMENT.  */

static void
initialize_hash_element (tree expr, tree lhs, struct expr_hash_elt *element)
{
  /* Hash table elements may be based on conditional expressions or statements.

     For the former case, we have no annotation and we want to hash the
     conditional expression.  In the latter case we have an annotation and
     we want to record the expression the statement evaluates.  */
  if (COMPARISON_CLASS_P (expr) || TREE_CODE (expr) == TRUTH_NOT_EXPR)
    {
      element->stmt = NULL;
      element->rhs = expr;
    }
  else if (TREE_CODE (expr) == COND_EXPR)
    {
      element->stmt = expr;
      element->rhs = COND_EXPR_COND (expr);
    }
  else if (TREE_CODE (expr) == SWITCH_EXPR)
    {
      element->stmt = expr;
      element->rhs = SWITCH_COND (expr);
    }
  else if (TREE_CODE (expr) == RETURN_EXPR && TREE_OPERAND (expr, 0))
    {
      element->stmt = expr;
      element->rhs = TREE_OPERAND (TREE_OPERAND (expr, 0), 1);
    }
  else if (TREE_CODE (expr) == GOTO_EXPR)
    {
      element->stmt = expr;
      element->rhs = GOTO_DESTINATION (expr);
    }
  else
    {
      element->stmt = expr;
      element->rhs = TREE_OPERAND (expr, 1);
    }

  element->lhs = lhs;
  element->hash = avail_expr_hash (element);
}

/* Remove all the expressions in LOCALS from TABLE, stopping when there are
   LIMIT entries left in LOCALs.  */

static void
remove_local_expressions_from_table (void)
{
  /* Remove all the expressions made available in this block.  */
  while (VEC_length (tree, avail_exprs_stack) > 0)
    {
      struct expr_hash_elt element;
      tree expr = VEC_pop (tree, avail_exprs_stack);

      if (expr == NULL_TREE)
	break;

      initialize_hash_element (expr, NULL, &element);
      htab_remove_elt_with_hash (avail_exprs, &element, element.hash);
    }
}

/* Use the source/dest pairs in CONST_AND_COPIES_STACK to restore
   CONST_AND_COPIES to its original state, stopping when we hit a
   NULL marker.  */

static void
restore_vars_to_original_value (void)
{
  while (VEC_length (tree, const_and_copies_stack) > 0)
    {
      tree prev_value, dest;

      dest = VEC_pop (tree, const_and_copies_stack);

      if (dest == NULL)
	break;

      prev_value = VEC_pop (tree, const_and_copies_stack);
      SSA_NAME_VALUE (dest) =  prev_value;
    }
}

/* A trivial wrapper so that we can present the generic jump
   threading code with a simple API for simplifying statements.  */
static tree
simplify_stmt_for_jump_threading (tree stmt, tree within_stmt ATTRIBUTE_UNUSED)
{
  return lookup_avail_expr (stmt, false);
}

/* Wrapper for common code to attempt to thread an edge.  For example,
   it handles lazily building the dummy condition and the bookkeeping
   when jump threading is successful.  */

static void
dom_thread_across_edge (struct dom_walk_data *walk_data, edge e)
{
  /* If we don't already have a dummy condition, build it now.  */
  if (! walk_data->global_data)
    {
      tree dummy_cond = build2 (NE_EXPR, boolean_type_node,
			        integer_zero_node, integer_zero_node);
      dummy_cond = build3 (COND_EXPR, void_type_node, dummy_cond, NULL, NULL);
      walk_data->global_data = dummy_cond;
    }

  thread_across_edge (walk_data->global_data, e, false,
		      &const_and_copies_stack,
		      simplify_stmt_for_jump_threading);
}

/* We have finished processing the dominator children of BB, perform
   any finalization actions in preparation for leaving this node in
   the dominator tree.  */

static void
dom_opt_finalize_block (struct dom_walk_data *walk_data, basic_block bb)
{
  tree last;


  /* If we have an outgoing edge to a block with multiple incoming and
     outgoing edges, then we may be able to thread the edge.  ie, we
     may be able to statically determine which of the outgoing edges
     will be traversed when the incoming edge from BB is traversed.  */
  if (single_succ_p (bb)
      && (single_succ_edge (bb)->flags & EDGE_ABNORMAL) == 0
      && potentially_threadable_block (single_succ (bb)))
    {
      dom_thread_across_edge (walk_data, single_succ_edge (bb));
    }
  else if ((last = last_stmt (bb))
	   && TREE_CODE (last) == COND_EXPR
	   && (COMPARISON_CLASS_P (COND_EXPR_COND (last))
	       || TREE_CODE (COND_EXPR_COND (last)) == SSA_NAME)
	   && EDGE_COUNT (bb->succs) == 2
	   && (EDGE_SUCC (bb, 0)->flags & EDGE_ABNORMAL) == 0
	   && (EDGE_SUCC (bb, 1)->flags & EDGE_ABNORMAL) == 0)
    {
      edge true_edge, false_edge;

      extract_true_false_edges_from_block (bb, &true_edge, &false_edge);

      /* Only try to thread the edge if it reaches a target block with
	 more than one predecessor and more than one successor.  */
      if (potentially_threadable_block (true_edge->dest))
	{
	  struct edge_info *edge_info;
	  unsigned int i;

	  /* Push a marker onto the available expression stack so that we
	     unwind any expressions related to the TRUE arm before processing
	     the false arm below.  */
	  VEC_safe_push (tree, heap, avail_exprs_stack, NULL_TREE);
	  VEC_safe_push (tree, heap, const_and_copies_stack, NULL_TREE);

	  edge_info = (struct edge_info *) true_edge->aux;

	  /* If we have info associated with this edge, record it into
	     our equivalency tables.  */
	  if (edge_info)
	    {
	      tree *cond_equivalences = edge_info->cond_equivalences;
	      tree lhs = edge_info->lhs;
	      tree rhs = edge_info->rhs;

	      /* If we have a simple NAME = VALUE equivalency record it.  */
	      if (lhs && TREE_CODE (lhs) == SSA_NAME)
		record_const_or_copy (lhs, rhs);

	      /* If we have 0 = COND or 1 = COND equivalences, record them
		 into our expression hash tables.  */
	      if (cond_equivalences)
		for (i = 0; i < edge_info->max_cond_equivalences; i += 2)
		  {
		    tree expr = cond_equivalences[i];
		    tree value = cond_equivalences[i + 1];

		    record_cond (expr, value);
		  }
	    }

	  dom_thread_across_edge (walk_data, true_edge);

	  /* And restore the various tables to their state before
	     we threaded this edge.  */
	  remove_local_expressions_from_table ();
	}

      /* Similarly for the ELSE arm.  */
      if (potentially_threadable_block (false_edge->dest))
	{
	  struct edge_info *edge_info;
	  unsigned int i;

	  VEC_safe_push (tree, heap, const_and_copies_stack, NULL_TREE);
	  edge_info = (struct edge_info *) false_edge->aux;

	  /* If we have info associated with this edge, record it into
	     our equivalency tables.  */
	  if (edge_info)
	    {
	      tree *cond_equivalences = edge_info->cond_equivalences;
	      tree lhs = edge_info->lhs;
	      tree rhs = edge_info->rhs;

	      /* If we have a simple NAME = VALUE equivalency record it.  */
	      if (lhs && TREE_CODE (lhs) == SSA_NAME)
		record_const_or_copy (lhs, rhs);

	      /* If we have 0 = COND or 1 = COND equivalences, record them
		 into our expression hash tables.  */
	      if (cond_equivalences)
		for (i = 0; i < edge_info->max_cond_equivalences; i += 2)
		  {
		    tree expr = cond_equivalences[i];
		    tree value = cond_equivalences[i + 1];

		    record_cond (expr, value);
		  }
	    }

	  /* Now thread the edge.  */
	  dom_thread_across_edge (walk_data, false_edge);

	  /* No need to remove local expressions from our tables
	     or restore vars to their original value as that will
	     be done immediately below.  */
	}
    }

  remove_local_expressions_from_table ();
  restore_vars_to_original_value ();

  /* If we queued any statements to rescan in this block, then
     go ahead and rescan them now.  */
  while (VEC_length (tree, stmts_to_rescan) > 0)
    {
      tree stmt = VEC_last (tree, stmts_to_rescan);
      basic_block stmt_bb = bb_for_stmt (stmt);

      if (stmt_bb != bb)
	break;

      VEC_pop (tree, stmts_to_rescan);
      mark_new_vars_to_rename (stmt);
    }
}

/* PHI nodes can create equivalences too.

   Ignoring any alternatives which are the same as the result, if
   all the alternatives are equal, then the PHI node creates an
   equivalence.  */

static void
record_equivalences_from_phis (basic_block bb)
{
  tree phi;

  for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
    {
      tree lhs = PHI_RESULT (phi);
      tree rhs = NULL;
      int i;

      for (i = 0; i < PHI_NUM_ARGS (phi); i++)
	{
	  tree t = PHI_ARG_DEF (phi, i);

	  /* Ignore alternatives which are the same as our LHS.  Since
	     LHS is a PHI_RESULT, it is known to be a SSA_NAME, so we
	     can simply compare pointers.  */
	  if (lhs == t)
	    continue;

	  /* If we have not processed an alternative yet, then set
	     RHS to this alternative.  */
	  if (rhs == NULL)
	    rhs = t;
	  /* If we have processed an alternative (stored in RHS), then
	     see if it is equal to this one.  If it isn't, then stop
	     the search.  */
	  else if (! operand_equal_for_phi_arg_p (rhs, t))
	    break;
	}

      /* If we had no interesting alternatives, then all the RHS alternatives
	 must have been the same as LHS.  */
      if (!rhs)
	rhs = lhs;

      /* If we managed to iterate through each PHI alternative without
	 breaking out of the loop, then we have a PHI which may create
	 a useful equivalence.  We do not need to record unwind data for
	 this, since this is a true assignment and not an equivalence
	 inferred from a comparison.  All uses of this ssa name are dominated
	 by this assignment, so unwinding just costs time and space.  */
      if (i == PHI_NUM_ARGS (phi)
	  && may_propagate_copy (lhs, rhs))
	SSA_NAME_VALUE (lhs) = rhs;
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

/* Record any equivalences created by the incoming edge to BB.  If BB
   has more than one incoming edge, then no equivalence is created.  */

static void
record_equivalences_from_incoming_edge (basic_block bb)
{
  edge e;
  basic_block parent;
  struct edge_info *edge_info;

  /* If our parent block ended with a control statement, then we may be
     able to record some equivalences based on which outgoing edge from
     the parent was followed.  */
  parent = get_immediate_dominator (CDI_DOMINATORS, bb);

  e = single_incoming_edge_ignoring_loop_edges (bb);

  /* If we had a single incoming edge from our parent block, then enter
     any data associated with the edge into our tables.  */
  if (e && e->src == parent)
    {
      unsigned int i;

      edge_info = (struct edge_info *) e->aux;

      if (edge_info)
	{
	  tree lhs = edge_info->lhs;
	  tree rhs = edge_info->rhs;
	  tree *cond_equivalences = edge_info->cond_equivalences;

	  if (lhs)
	    record_equality (lhs, rhs);

	  if (cond_equivalences)
	    {
	      for (i = 0; i < edge_info->max_cond_equivalences; i += 2)
		{
		  tree expr = cond_equivalences[i];
		  tree value = cond_equivalences[i + 1];

		  record_cond (expr, value);
		}
	    }
	}
    }
}

/* Dump SSA statistics on FILE.  */

void
dump_dominator_optimization_stats (FILE *file)
{
  long n_exprs;

  fprintf (file, "Total number of statements:                   %6ld\n\n",
	   opt_stats.num_stmts);
  fprintf (file, "Exprs considered for dominator optimizations: %6ld\n",
           opt_stats.num_exprs_considered);

  n_exprs = opt_stats.num_exprs_considered;
  if (n_exprs == 0)
    n_exprs = 1;

  fprintf (file, "    Redundant expressions eliminated:         %6ld (%.0f%%)\n",
	   opt_stats.num_re, PERCENT (opt_stats.num_re,
				      n_exprs));
  fprintf (file, "    Constants propagated:                     %6ld\n",
	   opt_stats.num_const_prop);
  fprintf (file, "    Copies propagated:                        %6ld\n",
	   opt_stats.num_copy_prop);

  fprintf (file, "\nHash table statistics:\n");

  fprintf (file, "    avail_exprs: ");
  htab_statistics (file, avail_exprs);
}


/* Dump SSA statistics on stderr.  */

void
debug_dominator_optimization_stats (void)
{
  dump_dominator_optimization_stats (stderr);
}


/* Dump statistics for the hash table HTAB.  */

static void
htab_statistics (FILE *file, htab_t htab)
{
  fprintf (file, "size %ld, %ld elements, %f collision/search ratio\n",
	   (long) htab_size (htab),
	   (long) htab_elements (htab),
	   htab_collisions (htab));
}

/* Enter a statement into the true/false expression hash table indicating
   that the condition COND has the value VALUE.  */

static void
record_cond (tree cond, tree value)
{
  struct expr_hash_elt *element = XCNEW (struct expr_hash_elt);
  void **slot;

  initialize_hash_element (cond, value, element);

  slot = htab_find_slot_with_hash (avail_exprs, (void *)element,
				   element->hash, INSERT);
  if (*slot == NULL)
    {
      *slot = (void *) element;
      VEC_safe_push (tree, heap, avail_exprs_stack, cond);
    }
  else
    free (element);
}

/* Build a new conditional using NEW_CODE, OP0 and OP1 and store
   the new conditional into *p, then store a boolean_true_node
   into *(p + 1).  */
   
static void
build_and_record_new_cond (enum tree_code new_code, tree op0, tree op1, tree *p)
{
  *p = build2 (new_code, boolean_type_node, op0, op1);
  p++;
  *p = boolean_true_node;
}

/* Record that COND is true and INVERTED is false into the edge information
   structure.  Also record that any conditions dominated by COND are true
   as well.

   For example, if a < b is true, then a <= b must also be true.  */

static void
record_conditions (struct edge_info *edge_info, tree cond, tree inverted)
{
  tree op0, op1;

  if (!COMPARISON_CLASS_P (cond))
    return;

  op0 = TREE_OPERAND (cond, 0);
  op1 = TREE_OPERAND (cond, 1);

  switch (TREE_CODE (cond))
    {
    case LT_EXPR:
    case GT_EXPR:
      if (FLOAT_TYPE_P (TREE_TYPE (op0)))
	{
	  edge_info->max_cond_equivalences = 12;
	  edge_info->cond_equivalences = XNEWVEC (tree, 12);
	  build_and_record_new_cond (ORDERED_EXPR, op0, op1,
				     &edge_info->cond_equivalences[8]);
	  build_and_record_new_cond (LTGT_EXPR, op0, op1,
				     &edge_info->cond_equivalences[10]);
	}
      else
	{
	  edge_info->max_cond_equivalences = 8;
	  edge_info->cond_equivalences = XNEWVEC (tree, 8);
	}

      build_and_record_new_cond ((TREE_CODE (cond) == LT_EXPR
				  ? LE_EXPR : GE_EXPR),
				 op0, op1, &edge_info->cond_equivalences[4]);
      build_and_record_new_cond (NE_EXPR, op0, op1,
				 &edge_info->cond_equivalences[6]);
      break;

    case GE_EXPR:
    case LE_EXPR:
      if (FLOAT_TYPE_P (TREE_TYPE (op0)))
	{
	  edge_info->max_cond_equivalences = 6;
	  edge_info->cond_equivalences = XNEWVEC (tree, 6);
	  build_and_record_new_cond (ORDERED_EXPR, op0, op1,
				     &edge_info->cond_equivalences[4]);
	}
      else
	{
	  edge_info->max_cond_equivalences = 4;
	  edge_info->cond_equivalences = XNEWVEC (tree, 4);
	}
      break;

    case EQ_EXPR:
      if (FLOAT_TYPE_P (TREE_TYPE (op0)))
	{
	  edge_info->max_cond_equivalences = 10;
	  edge_info->cond_equivalences = XNEWVEC (tree, 10);
	  build_and_record_new_cond (ORDERED_EXPR, op0, op1,
				     &edge_info->cond_equivalences[8]);
	}
      else
	{
	  edge_info->max_cond_equivalences = 8;
	  edge_info->cond_equivalences = XNEWVEC (tree, 8);
	}
      build_and_record_new_cond (LE_EXPR, op0, op1,
				 &edge_info->cond_equivalences[4]);
      build_and_record_new_cond (GE_EXPR, op0, op1,
				 &edge_info->cond_equivalences[6]);
      break;

    case UNORDERED_EXPR:
      edge_info->max_cond_equivalences = 16;
      edge_info->cond_equivalences = XNEWVEC (tree, 16);
      build_and_record_new_cond (NE_EXPR, op0, op1,
				 &edge_info->cond_equivalences[4]);
      build_and_record_new_cond (UNLE_EXPR, op0, op1,
				 &edge_info->cond_equivalences[6]);
      build_and_record_new_cond (UNGE_EXPR, op0, op1,
				 &edge_info->cond_equivalences[8]);
      build_and_record_new_cond (UNEQ_EXPR, op0, op1,
				 &edge_info->cond_equivalences[10]);
      build_and_record_new_cond (UNLT_EXPR, op0, op1,
				 &edge_info->cond_equivalences[12]);
      build_and_record_new_cond (UNGT_EXPR, op0, op1,
				 &edge_info->cond_equivalences[14]);
      break;

    case UNLT_EXPR:
    case UNGT_EXPR:
      edge_info->max_cond_equivalences = 8;
      edge_info->cond_equivalences = XNEWVEC (tree, 8);
      build_and_record_new_cond ((TREE_CODE (cond) == UNLT_EXPR
				  ? UNLE_EXPR : UNGE_EXPR),
				 op0, op1, &edge_info->cond_equivalences[4]);
      build_and_record_new_cond (NE_EXPR, op0, op1,
				 &edge_info->cond_equivalences[6]);
      break;

    case UNEQ_EXPR:
      edge_info->max_cond_equivalences = 8;
      edge_info->cond_equivalences = XNEWVEC (tree, 8);
      build_and_record_new_cond (UNLE_EXPR, op0, op1,
				 &edge_info->cond_equivalences[4]);
      build_and_record_new_cond (UNGE_EXPR, op0, op1,
				 &edge_info->cond_equivalences[6]);
      break;

    case LTGT_EXPR:
      edge_info->max_cond_equivalences = 8;
      edge_info->cond_equivalences = XNEWVEC (tree, 8);
      build_and_record_new_cond (NE_EXPR, op0, op1,
				 &edge_info->cond_equivalences[4]);
      build_and_record_new_cond (ORDERED_EXPR, op0, op1,
				 &edge_info->cond_equivalences[6]);
      break;

    default:
      edge_info->max_cond_equivalences = 4;
      edge_info->cond_equivalences = XNEWVEC (tree, 4);
      break;
    }

  /* Now store the original true and false conditions into the first
     two slots.  */
  edge_info->cond_equivalences[0] = cond;
  edge_info->cond_equivalences[1] = boolean_true_node;
  edge_info->cond_equivalences[2] = inverted;
  edge_info->cond_equivalences[3] = boolean_false_node;
}

/* A helper function for record_const_or_copy and record_equality.
   Do the work of recording the value and undo info.  */

static void
record_const_or_copy_1 (tree x, tree y, tree prev_x)
{
  SSA_NAME_VALUE (x) = y;

  VEC_reserve (tree, heap, const_and_copies_stack, 2);
  VEC_quick_push (tree, const_and_copies_stack, prev_x);
  VEC_quick_push (tree, const_and_copies_stack, x);
}


/* Return the loop depth of the basic block of the defining statement of X.
   This number should not be treated as absolutely correct because the loop
   information may not be completely up-to-date when dom runs.  However, it
   will be relatively correct, and as more passes are taught to keep loop info
   up to date, the result will become more and more accurate.  */

int
loop_depth_of_name (tree x)
{
  tree defstmt;
  basic_block defbb;

  /* If it's not an SSA_NAME, we have no clue where the definition is.  */
  if (TREE_CODE (x) != SSA_NAME)
    return 0;

  /* Otherwise return the loop depth of the defining statement's bb.
     Note that there may not actually be a bb for this statement, if the
     ssa_name is live on entry.  */
  defstmt = SSA_NAME_DEF_STMT (x);
  defbb = bb_for_stmt (defstmt);
  if (!defbb)
    return 0;

  return defbb->loop_depth;
}


/* Record that X is equal to Y in const_and_copies.  Record undo
   information in the block-local vector.  */

static void
record_const_or_copy (tree x, tree y)
{
  tree prev_x = SSA_NAME_VALUE (x);

  if (TREE_CODE (y) == SSA_NAME)
    {
      tree tmp = SSA_NAME_VALUE (y);
      if (tmp)
	y = tmp;
    }

  record_const_or_copy_1 (x, y, prev_x);
}

/* Similarly, but assume that X and Y are the two operands of an EQ_EXPR.
   This constrains the cases in which we may treat this as assignment.  */

static void
record_equality (tree x, tree y)
{
  tree prev_x = NULL, prev_y = NULL;

  if (TREE_CODE (x) == SSA_NAME)
    prev_x = SSA_NAME_VALUE (x);
  if (TREE_CODE (y) == SSA_NAME)
    prev_y = SSA_NAME_VALUE (y);

  /* If one of the previous values is invariant, or invariant in more loops
     (by depth), then use that.
     Otherwise it doesn't matter which value we choose, just so
     long as we canonicalize on one value.  */
  if (TREE_INVARIANT (y))
    ;
  else if (TREE_INVARIANT (x) || (loop_depth_of_name (x) <= loop_depth_of_name (y)))
    prev_x = x, x = y, y = prev_x, prev_x = prev_y;
  else if (prev_x && TREE_INVARIANT (prev_x))
    x = y, y = prev_x, prev_x = prev_y;
  else if (prev_y && TREE_CODE (prev_y) != VALUE_HANDLE)
    y = prev_y;

  /* After the swapping, we must have one SSA_NAME.  */
  if (TREE_CODE (x) != SSA_NAME)
    return;

  /* For IEEE, -0.0 == 0.0, so we don't necessarily know the sign of a
     variable compared against zero.  If we're honoring signed zeros,
     then we cannot record this value unless we know that the value is
     nonzero.  */
  if (HONOR_SIGNED_ZEROS (TYPE_MODE (TREE_TYPE (x)))
      && (TREE_CODE (y) != REAL_CST
	  || REAL_VALUES_EQUAL (dconst0, TREE_REAL_CST (y))))
    return;

  record_const_or_copy_1 (x, y, prev_x);
}

/* Returns true when STMT is a simple iv increment.  It detects the
   following situation:
   
   i_1 = phi (..., i_2)
   i_2 = i_1 +/- ...  */

static bool
simple_iv_increment_p (tree stmt)
{
  tree lhs, rhs, preinc, phi;
  unsigned i;

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    return false;

  lhs = TREE_OPERAND (stmt, 0);
  if (TREE_CODE (lhs) != SSA_NAME)
    return false;

  rhs = TREE_OPERAND (stmt, 1);

  if (TREE_CODE (rhs) != PLUS_EXPR
      && TREE_CODE (rhs) != MINUS_EXPR)
    return false;

  preinc = TREE_OPERAND (rhs, 0);
  if (TREE_CODE (preinc) != SSA_NAME)
    return false;

  phi = SSA_NAME_DEF_STMT (preinc);
  if (TREE_CODE (phi) != PHI_NODE)
    return false;

  for (i = 0; i < (unsigned) PHI_NUM_ARGS (phi); i++)
    if (PHI_ARG_DEF (phi, i) == lhs)
      return true;

  return false;
}

/* CONST_AND_COPIES is a table which maps an SSA_NAME to the current
   known value for that SSA_NAME (or NULL if no value is known).  

   Propagate values from CONST_AND_COPIES into the PHI nodes of the
   successors of BB.  */

static void
cprop_into_successor_phis (basic_block bb)
{
  edge e;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, bb->succs)
    {
      tree phi;
      int indx;

      /* If this is an abnormal edge, then we do not want to copy propagate
	 into the PHI alternative associated with this edge.  */
      if (e->flags & EDGE_ABNORMAL)
	continue;

      phi = phi_nodes (e->dest);
      if (! phi)
	continue;

      indx = e->dest_idx;
      for ( ; phi; phi = PHI_CHAIN (phi))
	{
	  tree new;
	  use_operand_p orig_p;
	  tree orig;

	  /* The alternative may be associated with a constant, so verify
	     it is an SSA_NAME before doing anything with it.  */
	  orig_p = PHI_ARG_DEF_PTR (phi, indx);
	  orig = USE_FROM_PTR (orig_p);
	  if (TREE_CODE (orig) != SSA_NAME)
	    continue;

	  /* If we have *ORIG_P in our constant/copy table, then replace
	     ORIG_P with its value in our constant/copy table.  */
	  new = SSA_NAME_VALUE (orig);
	  if (new
	      && new != orig
	      && (TREE_CODE (new) == SSA_NAME
		  || is_gimple_min_invariant (new))
	      && may_propagate_copy (orig, new))
	    propagate_value (orig_p, new);
	}
    }
}

/* We have finished optimizing BB, record any information implied by
   taking a specific outgoing edge from BB.  */

static void
record_edge_info (basic_block bb)
{
  block_stmt_iterator bsi = bsi_last (bb);
  struct edge_info *edge_info;

  if (! bsi_end_p (bsi))
    {
      tree stmt = bsi_stmt (bsi);

      if (stmt && TREE_CODE (stmt) == SWITCH_EXPR)
	{
	  tree cond = SWITCH_COND (stmt);

	  if (TREE_CODE (cond) == SSA_NAME)
	    {
	      tree labels = SWITCH_LABELS (stmt);
	      int i, n_labels = TREE_VEC_LENGTH (labels);
	      tree *info = XCNEWVEC (tree, last_basic_block);
	      edge e;
	      edge_iterator ei;

	      for (i = 0; i < n_labels; i++)
		{
		  tree label = TREE_VEC_ELT (labels, i);
		  basic_block target_bb = label_to_block (CASE_LABEL (label));

		  if (CASE_HIGH (label)
		      || !CASE_LOW (label)
		      || info[target_bb->index])
		    info[target_bb->index] = error_mark_node;
		  else
		    info[target_bb->index] = label;
		}

	      FOR_EACH_EDGE (e, ei, bb->succs)
		{
		  basic_block target_bb = e->dest;
		  tree node = info[target_bb->index];

		  if (node != NULL && node != error_mark_node)
		    {
		      tree x = fold_convert (TREE_TYPE (cond), CASE_LOW (node));
		      edge_info = allocate_edge_info (e);
		      edge_info->lhs = cond;
		      edge_info->rhs = x;
		    }
		}
	      free (info);
	    }
	}

      /* A COND_EXPR may create equivalences too.  */
      if (stmt && TREE_CODE (stmt) == COND_EXPR)
	{
	  tree cond = COND_EXPR_COND (stmt);
	  edge true_edge;
	  edge false_edge;

	  extract_true_false_edges_from_block (bb, &true_edge, &false_edge);

	  /* If the conditional is a single variable 'X', record 'X = 1'
	     for the true edge and 'X = 0' on the false edge.  */
	  if (SSA_VAR_P (cond))
	    {
	      struct edge_info *edge_info;

	      edge_info = allocate_edge_info (true_edge);
	      edge_info->lhs = cond;
	      edge_info->rhs = constant_boolean_node (1, TREE_TYPE (cond));

	      edge_info = allocate_edge_info (false_edge);
	      edge_info->lhs = cond;
	      edge_info->rhs = constant_boolean_node (0, TREE_TYPE (cond));
	    }
	  /* Equality tests may create one or two equivalences.  */
	  else if (COMPARISON_CLASS_P (cond))
	    {
	      tree op0 = TREE_OPERAND (cond, 0);
	      tree op1 = TREE_OPERAND (cond, 1);

	      /* Special case comparing booleans against a constant as we
		 know the value of OP0 on both arms of the branch.  i.e., we
		 can record an equivalence for OP0 rather than COND.  */
	      if ((TREE_CODE (cond) == EQ_EXPR || TREE_CODE (cond) == NE_EXPR)
		  && TREE_CODE (op0) == SSA_NAME
		  && TREE_CODE (TREE_TYPE (op0)) == BOOLEAN_TYPE
		  && is_gimple_min_invariant (op1))
		{
		  if (TREE_CODE (cond) == EQ_EXPR)
		    {
		      edge_info = allocate_edge_info (true_edge);
		      edge_info->lhs = op0;
		      edge_info->rhs = (integer_zerop (op1)
					    ? boolean_false_node
					    : boolean_true_node);

		      edge_info = allocate_edge_info (false_edge);
		      edge_info->lhs = op0;
		      edge_info->rhs = (integer_zerop (op1)
					    ? boolean_true_node
					    : boolean_false_node);
		    }
		  else
		    {
		      edge_info = allocate_edge_info (true_edge);
		      edge_info->lhs = op0;
		      edge_info->rhs = (integer_zerop (op1)
					    ? boolean_true_node
					    : boolean_false_node);

		      edge_info = allocate_edge_info (false_edge);
		      edge_info->lhs = op0;
		      edge_info->rhs = (integer_zerop (op1)
					    ? boolean_false_node
					    : boolean_true_node);
		    }
		}

	      else if (is_gimple_min_invariant (op0)
		       && (TREE_CODE (op1) == SSA_NAME
			   || is_gimple_min_invariant (op1)))
		{
		  tree inverted = invert_truthvalue (cond);
		  struct edge_info *edge_info;

		  edge_info = allocate_edge_info (true_edge);
		  record_conditions (edge_info, cond, inverted);

		  if (TREE_CODE (cond) == EQ_EXPR)
		    {
		      edge_info->lhs = op1;
		      edge_info->rhs = op0;
		    }

		  edge_info = allocate_edge_info (false_edge);
		  record_conditions (edge_info, inverted, cond);

		  if (TREE_CODE (cond) == NE_EXPR)
		    {
		      edge_info->lhs = op1;
		      edge_info->rhs = op0;
		    }
		}

	      else if (TREE_CODE (op0) == SSA_NAME
		       && (is_gimple_min_invariant (op1)
			   || TREE_CODE (op1) == SSA_NAME))
		{
		  tree inverted = invert_truthvalue (cond);
		  struct edge_info *edge_info;

		  edge_info = allocate_edge_info (true_edge);
		  record_conditions (edge_info, cond, inverted);

		  if (TREE_CODE (cond) == EQ_EXPR)
		    {
		      edge_info->lhs = op0;
		      edge_info->rhs = op1;
		    }

		  edge_info = allocate_edge_info (false_edge);
		  record_conditions (edge_info, inverted, cond);

		  if (TREE_CODE (cond) == NE_EXPR)
		    {
		      edge_info->lhs = op0;
		      edge_info->rhs = op1;
		    }
		}
	    }

	  /* ??? TRUTH_NOT_EXPR can create an equivalence too.  */
	}
    }
}

/* Propagate information from BB to its outgoing edges.

   This can include equivalency information implied by control statements
   at the end of BB and const/copy propagation into PHIs in BB's
   successor blocks.  */

static void
propagate_to_outgoing_edges (struct dom_walk_data *walk_data ATTRIBUTE_UNUSED,
			     basic_block bb)
{
  record_edge_info (bb);
  cprop_into_successor_phis (bb);
}

/* Search for redundant computations in STMT.  If any are found, then
   replace them with the variable holding the result of the computation.

   If safe, record this expression into the available expression hash
   table.  */

static bool
eliminate_redundant_computations (tree stmt)
{
  tree *expr_p, def = NULL_TREE;
  bool insert = true;
  tree cached_lhs;
  bool retval = false;
  bool modify_expr_p = false;

  if (TREE_CODE (stmt) == MODIFY_EXPR)
    def = TREE_OPERAND (stmt, 0);

  /* Certain expressions on the RHS can be optimized away, but can not
     themselves be entered into the hash tables.  */
  if (! def
      || TREE_CODE (def) != SSA_NAME
      || SSA_NAME_OCCURS_IN_ABNORMAL_PHI (def)
      || !ZERO_SSA_OPERANDS (stmt, SSA_OP_VMAYDEF)
      /* Do not record equivalences for increments of ivs.  This would create
	 overlapping live ranges for a very questionable gain.  */
      || simple_iv_increment_p (stmt))
    insert = false;

  /* Check if the expression has been computed before.  */
  cached_lhs = lookup_avail_expr (stmt, insert);

  opt_stats.num_exprs_considered++;

  /* Get a pointer to the expression we are trying to optimize.  */
  if (TREE_CODE (stmt) == COND_EXPR)
    expr_p = &COND_EXPR_COND (stmt);
  else if (TREE_CODE (stmt) == SWITCH_EXPR)
    expr_p = &SWITCH_COND (stmt);
  else if (TREE_CODE (stmt) == RETURN_EXPR && TREE_OPERAND (stmt, 0))
    {
      expr_p = &TREE_OPERAND (TREE_OPERAND (stmt, 0), 1);
      modify_expr_p = true;
    }
  else
    {
      expr_p = &TREE_OPERAND (stmt, 1);
      modify_expr_p = true;
    }

  /* It is safe to ignore types here since we have already done
     type checking in the hashing and equality routines.  In fact
     type checking here merely gets in the way of constant
     propagation.  Also, make sure that it is safe to propagate
     CACHED_LHS into *EXPR_P.  */
  if (cached_lhs
      && ((TREE_CODE (cached_lhs) != SSA_NAME
	   && (modify_expr_p
	       || tree_ssa_useless_type_conversion_1 (TREE_TYPE (*expr_p),
						      TREE_TYPE (cached_lhs))))
	  || may_propagate_copy (*expr_p, cached_lhs)))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "  Replaced redundant expr '");
	  print_generic_expr (dump_file, *expr_p, dump_flags);
	  fprintf (dump_file, "' with '");
	  print_generic_expr (dump_file, cached_lhs, dump_flags);
	   fprintf (dump_file, "'\n");
	}

      opt_stats.num_re++;

#if defined ENABLE_CHECKING
      gcc_assert (TREE_CODE (cached_lhs) == SSA_NAME
		  || is_gimple_min_invariant (cached_lhs));
#endif

      if (TREE_CODE (cached_lhs) == ADDR_EXPR
	  || (POINTER_TYPE_P (TREE_TYPE (*expr_p))
	      && is_gimple_min_invariant (cached_lhs)))
	retval = true;
      
      if (modify_expr_p
	  && !tree_ssa_useless_type_conversion_1 (TREE_TYPE (*expr_p),
						  TREE_TYPE (cached_lhs)))
	cached_lhs = fold_convert (TREE_TYPE (*expr_p), cached_lhs);

      propagate_tree_value (expr_p, cached_lhs);
      mark_stmt_modified (stmt);
    }
  return retval;
}

/* STMT, a MODIFY_EXPR, may create certain equivalences, in either
   the available expressions table or the const_and_copies table.
   Detect and record those equivalences.  */

static void
record_equivalences_from_stmt (tree stmt,
			       int may_optimize_p,
			       stmt_ann_t ann)
{
  tree lhs = TREE_OPERAND (stmt, 0);
  enum tree_code lhs_code = TREE_CODE (lhs);

  if (lhs_code == SSA_NAME)
    {
      tree rhs = TREE_OPERAND (stmt, 1);

      /* Strip away any useless type conversions.  */
      STRIP_USELESS_TYPE_CONVERSION (rhs);

      /* If the RHS of the assignment is a constant or another variable that
	 may be propagated, register it in the CONST_AND_COPIES table.  We
	 do not need to record unwind data for this, since this is a true
	 assignment and not an equivalence inferred from a comparison.  All
	 uses of this ssa name are dominated by this assignment, so unwinding
	 just costs time and space.  */
      if (may_optimize_p
	  && (TREE_CODE (rhs) == SSA_NAME
	      || is_gimple_min_invariant (rhs)))
	SSA_NAME_VALUE (lhs) = rhs;
    }

  /* A memory store, even an aliased store, creates a useful
     equivalence.  By exchanging the LHS and RHS, creating suitable
     vops and recording the result in the available expression table,
     we may be able to expose more redundant loads.  */
  if (!ann->has_volatile_ops
      && (TREE_CODE (TREE_OPERAND (stmt, 1)) == SSA_NAME
	  || is_gimple_min_invariant (TREE_OPERAND (stmt, 1)))
      && !is_gimple_reg (lhs))
    {
      tree rhs = TREE_OPERAND (stmt, 1);
      tree new;

      /* FIXME: If the LHS of the assignment is a bitfield and the RHS
         is a constant, we need to adjust the constant to fit into the
         type of the LHS.  If the LHS is a bitfield and the RHS is not
	 a constant, then we can not record any equivalences for this
	 statement since we would need to represent the widening or
	 narrowing of RHS.  This fixes gcc.c-torture/execute/921016-1.c
	 and should not be necessary if GCC represented bitfields
	 properly.  */
      if (lhs_code == COMPONENT_REF
	  && DECL_BIT_FIELD (TREE_OPERAND (lhs, 1)))
	{
	  if (TREE_CONSTANT (rhs))
	    rhs = widen_bitfield (rhs, TREE_OPERAND (lhs, 1), lhs);
	  else
	    rhs = NULL;

	  /* If the value overflowed, then we can not use this equivalence.  */
	  if (rhs && ! is_gimple_min_invariant (rhs))
	    rhs = NULL;
	}

      if (rhs)
	{
	  /* Build a new statement with the RHS and LHS exchanged.  */
	  new = build2 (MODIFY_EXPR, TREE_TYPE (stmt), rhs, lhs);

	  create_ssa_artficial_load_stmt (new, stmt);

	  /* Finally enter the statement into the available expression
	     table.  */
	  lookup_avail_expr (new, true);
	}
    }
}

/* Replace *OP_P in STMT with any known equivalent value for *OP_P from
   CONST_AND_COPIES.  */

static bool
cprop_operand (tree stmt, use_operand_p op_p)
{
  bool may_have_exposed_new_symbols = false;
  tree val;
  tree op = USE_FROM_PTR (op_p);

  /* If the operand has a known constant value or it is known to be a
     copy of some other variable, use the value or copy stored in
     CONST_AND_COPIES.  */
  val = SSA_NAME_VALUE (op);
  if (val && val != op && TREE_CODE (val) != VALUE_HANDLE)
    {
      tree op_type, val_type;

      /* Do not change the base variable in the virtual operand
	 tables.  That would make it impossible to reconstruct
	 the renamed virtual operand if we later modify this
	 statement.  Also only allow the new value to be an SSA_NAME
	 for propagation into virtual operands.  */
      if (!is_gimple_reg (op)
	  && (TREE_CODE (val) != SSA_NAME
	      || is_gimple_reg (val)
	      || get_virtual_var (val) != get_virtual_var (op)))
	return false;

      /* Do not replace hard register operands in asm statements.  */
      if (TREE_CODE (stmt) == ASM_EXPR
	  && !may_propagate_copy_into_asm (op))
	return false;

      /* Get the toplevel type of each operand.  */
      op_type = TREE_TYPE (op);
      val_type = TREE_TYPE (val);

      /* While both types are pointers, get the type of the object
	 pointed to.  */
      while (POINTER_TYPE_P (op_type) && POINTER_TYPE_P (val_type))
	{
	  op_type = TREE_TYPE (op_type);
	  val_type = TREE_TYPE (val_type);
	}

      /* Make sure underlying types match before propagating a constant by
	 converting the constant to the proper type.  Note that convert may
	 return a non-gimple expression, in which case we ignore this
	 propagation opportunity.  */
      if (TREE_CODE (val) != SSA_NAME)
	{
	  if (!lang_hooks.types_compatible_p (op_type, val_type))
	    {
	      val = fold_convert (TREE_TYPE (op), val);
	      if (!is_gimple_min_invariant (val))
		return false;
	    }
	}

      /* Certain operands are not allowed to be copy propagated due
	 to their interaction with exception handling and some GCC
	 extensions.  */
      else if (!may_propagate_copy (op, val))
	return false;
      
      /* Do not propagate copies if the propagated value is at a deeper loop
	 depth than the propagatee.  Otherwise, this may move loop variant
	 variables outside of their loops and prevent coalescing
	 opportunities.  If the value was loop invariant, it will be hoisted
	 by LICM and exposed for copy propagation.  */
      if (loop_depth_of_name (val) > loop_depth_of_name (op))
	return false;

      /* Dump details.  */
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "  Replaced '");
	  print_generic_expr (dump_file, op, dump_flags);
	  fprintf (dump_file, "' with %s '",
		   (TREE_CODE (val) != SSA_NAME ? "constant" : "variable"));
	  print_generic_expr (dump_file, val, dump_flags);
	  fprintf (dump_file, "'\n");
	}

      /* If VAL is an ADDR_EXPR or a constant of pointer type, note
	 that we may have exposed a new symbol for SSA renaming.  */
      if (TREE_CODE (val) == ADDR_EXPR
	  || (POINTER_TYPE_P (TREE_TYPE (op))
	      && is_gimple_min_invariant (val)))
	may_have_exposed_new_symbols = true;

      if (TREE_CODE (val) != SSA_NAME)
	opt_stats.num_const_prop++;
      else
	opt_stats.num_copy_prop++;

      propagate_value (op_p, val);

      /* And note that we modified this statement.  This is now
	 safe, even if we changed virtual operands since we will
	 rescan the statement and rewrite its operands again.  */
      mark_stmt_modified (stmt);
    }
  return may_have_exposed_new_symbols;
}

/* CONST_AND_COPIES is a table which maps an SSA_NAME to the current
   known value for that SSA_NAME (or NULL if no value is known).  

   Propagate values from CONST_AND_COPIES into the uses, vuses and
   v_may_def_ops of STMT.  */

static bool
cprop_into_stmt (tree stmt)
{
  bool may_have_exposed_new_symbols = false;
  use_operand_p op_p;
  ssa_op_iter iter;

  FOR_EACH_SSA_USE_OPERAND (op_p, stmt, iter, SSA_OP_ALL_USES)
    {
      if (TREE_CODE (USE_FROM_PTR (op_p)) == SSA_NAME)
	may_have_exposed_new_symbols |= cprop_operand (stmt, op_p);
    }

  return may_have_exposed_new_symbols;
}


/* Optimize the statement pointed to by iterator SI.
   
   We try to perform some simplistic global redundancy elimination and
   constant propagation:

   1- To detect global redundancy, we keep track of expressions that have
      been computed in this block and its dominators.  If we find that the
      same expression is computed more than once, we eliminate repeated
      computations by using the target of the first one.

   2- Constant values and copy assignments.  This is used to do very
      simplistic constant and copy propagation.  When a constant or copy
      assignment is found, we map the value on the RHS of the assignment to
      the variable in the LHS in the CONST_AND_COPIES table.  */

static void
optimize_stmt (struct dom_walk_data *walk_data ATTRIBUTE_UNUSED,
	       basic_block bb, block_stmt_iterator si)
{
  stmt_ann_t ann;
  tree stmt, old_stmt;
  bool may_optimize_p;
  bool may_have_exposed_new_symbols = false;

  old_stmt = stmt = bsi_stmt (si);
  
  if (TREE_CODE (stmt) == COND_EXPR)
    canonicalize_comparison (stmt);
  
  update_stmt_if_modified (stmt);
  ann = stmt_ann (stmt);
  opt_stats.num_stmts++;
  may_have_exposed_new_symbols = false;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Optimizing statement ");
      print_generic_stmt (dump_file, stmt, TDF_SLIM);
    }

  /* Const/copy propagate into USES, VUSES and the RHS of V_MAY_DEFs.  */
  may_have_exposed_new_symbols = cprop_into_stmt (stmt);

  /* If the statement has been modified with constant replacements,
     fold its RHS before checking for redundant computations.  */
  if (ann->modified)
    {
      tree rhs;

      /* Try to fold the statement making sure that STMT is kept
	 up to date.  */
      if (fold_stmt (bsi_stmt_ptr (si)))
	{
	  stmt = bsi_stmt (si);
	  ann = stmt_ann (stmt);

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "  Folded to: ");
	      print_generic_stmt (dump_file, stmt, TDF_SLIM);
	    }
	}

      rhs = get_rhs (stmt);
      if (rhs && TREE_CODE (rhs) == ADDR_EXPR)
	recompute_tree_invariant_for_addr_expr (rhs);

      /* Constant/copy propagation above may change the set of 
	 virtual operands associated with this statement.  Folding
	 may remove the need for some virtual operands.

	 Indicate we will need to rescan and rewrite the statement.  */
      may_have_exposed_new_symbols = true;
    }

  /* Check for redundant computations.  Do this optimization only
     for assignments that have no volatile ops and conditionals.  */
  may_optimize_p = (!ann->has_volatile_ops
		    && ((TREE_CODE (stmt) == RETURN_EXPR
			 && TREE_OPERAND (stmt, 0)
			 && TREE_CODE (TREE_OPERAND (stmt, 0)) == MODIFY_EXPR
			 && ! (TREE_SIDE_EFFECTS
			       (TREE_OPERAND (TREE_OPERAND (stmt, 0), 1))))
			|| (TREE_CODE (stmt) == MODIFY_EXPR
			    && ! TREE_SIDE_EFFECTS (TREE_OPERAND (stmt, 1)))
			|| TREE_CODE (stmt) == COND_EXPR
			|| TREE_CODE (stmt) == SWITCH_EXPR));

  if (may_optimize_p)
    may_have_exposed_new_symbols |= eliminate_redundant_computations (stmt);

  /* Record any additional equivalences created by this statement.  */
  if (TREE_CODE (stmt) == MODIFY_EXPR)
    record_equivalences_from_stmt (stmt,
				   may_optimize_p,
				   ann);

  /* If STMT is a COND_EXPR and it was modified, then we may know
     where it goes.  If that is the case, then mark the CFG as altered.

     This will cause us to later call remove_unreachable_blocks and
     cleanup_tree_cfg when it is safe to do so.  It is not safe to 
     clean things up here since removal of edges and such can trigger
     the removal of PHI nodes, which in turn can release SSA_NAMEs to
     the manager.

     That's all fine and good, except that once SSA_NAMEs are released
     to the manager, we must not call create_ssa_name until all references
     to released SSA_NAMEs have been eliminated.

     All references to the deleted SSA_NAMEs can not be eliminated until
     we remove unreachable blocks.

     We can not remove unreachable blocks until after we have completed
     any queued jump threading.

     We can not complete any queued jump threads until we have taken
     appropriate variables out of SSA form.  Taking variables out of
     SSA form can call create_ssa_name and thus we lose.

     Ultimately I suspect we're going to need to change the interface
     into the SSA_NAME manager.  */

  if (ann->modified)
    {
      tree val = NULL;

      if (TREE_CODE (stmt) == COND_EXPR)
	val = COND_EXPR_COND (stmt);
      else if (TREE_CODE (stmt) == SWITCH_EXPR)
	val = SWITCH_COND (stmt);

      if (val && TREE_CODE (val) == INTEGER_CST && find_taken_edge (bb, val))
	cfg_altered = true;

      /* If we simplified a statement in such a way as to be shown that it
	 cannot trap, update the eh information and the cfg to match.  */
      if (maybe_clean_or_replace_eh_stmt (old_stmt, stmt))
	{
	  bitmap_set_bit (need_eh_cleanup, bb->index);
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "  Flagged to clear EH edges.\n");
	}
    }

  if (may_have_exposed_new_symbols)
    VEC_safe_push (tree, heap, stmts_to_rescan, bsi_stmt (si));
}

/* Search for an existing instance of STMT in the AVAIL_EXPRS table.  If
   found, return its LHS. Otherwise insert STMT in the table and return
   NULL_TREE.

   Also, when an expression is first inserted in the AVAIL_EXPRS table, it
   is also added to the stack pointed to by BLOCK_AVAIL_EXPRS_P, so that they
   can be removed when we finish processing this block and its children.

   NOTE: This function assumes that STMT is a MODIFY_EXPR node that
   contains no CALL_EXPR on its RHS and makes no volatile nor
   aliased references.  */

static tree
lookup_avail_expr (tree stmt, bool insert)
{
  void **slot;
  tree lhs;
  tree temp;
  struct expr_hash_elt *element = XNEW (struct expr_hash_elt);

  lhs = TREE_CODE (stmt) == MODIFY_EXPR ? TREE_OPERAND (stmt, 0) : NULL;

  initialize_hash_element (stmt, lhs, element);

  /* Don't bother remembering constant assignments and copy operations.
     Constants and copy operations are handled by the constant/copy propagator
     in optimize_stmt.  */
  if (TREE_CODE (element->rhs) == SSA_NAME
      || is_gimple_min_invariant (element->rhs))
    {
      free (element);
      return NULL_TREE;
    }

  /* Finally try to find the expression in the main expression hash table.  */
  slot = htab_find_slot_with_hash (avail_exprs, element, element->hash,
				   (insert ? INSERT : NO_INSERT));
  if (slot == NULL)
    {
      free (element);
      return NULL_TREE;
    }

  if (*slot == NULL)
    {
      *slot = (void *) element;
      VEC_safe_push (tree, heap, avail_exprs_stack,
		     stmt ? stmt : element->rhs);
      return NULL_TREE;
    }

  /* Extract the LHS of the assignment so that it can be used as the current
     definition of another variable.  */
  lhs = ((struct expr_hash_elt *)*slot)->lhs;

  /* See if the LHS appears in the CONST_AND_COPIES table.  If it does, then
     use the value from the const_and_copies table.  */
  if (TREE_CODE (lhs) == SSA_NAME)
    {
      temp = SSA_NAME_VALUE (lhs);
      if (temp && TREE_CODE (temp) != VALUE_HANDLE)
	lhs = temp;
    }

  free (element);
  return lhs;
}

/* Hashing and equality functions for AVAIL_EXPRS.  The table stores
   MODIFY_EXPR statements.  We compute a value number for expressions using
   the code of the expression and the SSA numbers of its operands.  */

static hashval_t
avail_expr_hash (const void *p)
{
  tree stmt = ((struct expr_hash_elt *)p)->stmt;
  tree rhs = ((struct expr_hash_elt *)p)->rhs;
  tree vuse;
  ssa_op_iter iter;
  hashval_t val = 0;

  /* iterative_hash_expr knows how to deal with any expression and
     deals with commutative operators as well, so just use it instead
     of duplicating such complexities here.  */
  val = iterative_hash_expr (rhs, val);

  /* If the hash table entry is not associated with a statement, then we
     can just hash the expression and not worry about virtual operands
     and such.  */
  if (!stmt || !stmt_ann (stmt))
    return val;

  /* Add the SSA version numbers of every vuse operand.  This is important
     because compound variables like arrays are not renamed in the
     operands.  Rather, the rename is done on the virtual variable
     representing all the elements of the array.  */
  FOR_EACH_SSA_TREE_OPERAND (vuse, stmt, iter, SSA_OP_VUSE)
    val = iterative_hash_expr (vuse, val);

  return val;
}

static hashval_t
real_avail_expr_hash (const void *p)
{
  return ((const struct expr_hash_elt *)p)->hash;
}

static int
avail_expr_eq (const void *p1, const void *p2)
{
  tree stmt1 = ((struct expr_hash_elt *)p1)->stmt;
  tree rhs1 = ((struct expr_hash_elt *)p1)->rhs;
  tree stmt2 = ((struct expr_hash_elt *)p2)->stmt;
  tree rhs2 = ((struct expr_hash_elt *)p2)->rhs;

  /* If they are the same physical expression, return true.  */
  if (rhs1 == rhs2 && stmt1 == stmt2)
    return true;

  /* If their codes are not equal, then quit now.  */
  if (TREE_CODE (rhs1) != TREE_CODE (rhs2))
    return false;

  /* In case of a collision, both RHS have to be identical and have the
     same VUSE operands.  */
  if ((TREE_TYPE (rhs1) == TREE_TYPE (rhs2)
       || lang_hooks.types_compatible_p (TREE_TYPE (rhs1), TREE_TYPE (rhs2)))
      && operand_equal_p (rhs1, rhs2, OEP_PURE_SAME))
    {
      bool ret = compare_ssa_operands_equal (stmt1, stmt2, SSA_OP_VUSE);
      gcc_assert (!ret || ((struct expr_hash_elt *)p1)->hash
		  == ((struct expr_hash_elt *)p2)->hash);
      return ret;
    }

  return false;
}

/* PHI-ONLY copy and constant propagation.  This pass is meant to clean
   up degenerate PHIs created by or exposed by jump threading.  */

/* Given PHI, return its RHS if the PHI is a degenerate, otherwise return
   NULL.  */

static tree
degenerate_phi_result (tree phi)
{
  tree lhs = PHI_RESULT (phi);
  tree val = NULL;
  int i;

  /* Ignoring arguments which are the same as LHS, if all the remaining
     arguments are the same, then the PHI is a degenerate and has the
     value of that common argument.  */
  for (i = 0; i < PHI_NUM_ARGS (phi); i++)
    {
      tree arg = PHI_ARG_DEF (phi, i);

      if (arg == lhs)
	continue;
      else if (!val)
	val = arg;
      else if (!operand_equal_p (arg, val, 0))
	break;
    }
  return (i == PHI_NUM_ARGS (phi) ? val : NULL);
}

/* Given a tree node T, which is either a PHI_NODE or MODIFY_EXPR,
   remove it from the IL.  */

static void
remove_stmt_or_phi (tree t)
{
  if (TREE_CODE (t) == PHI_NODE)
    remove_phi_node (t, NULL);
  else
    {
      block_stmt_iterator bsi = bsi_for_stmt (t);
      bsi_remove (&bsi, true);
    }
}

/* Given a tree node T, which is either a PHI_NODE or MODIFY_EXPR,
   return the "rhs" of the node, in the case of a non-degenerate
   PHI, NULL is returned.  */

static tree
get_rhs_or_phi_arg (tree t)
{
  if (TREE_CODE (t) == PHI_NODE)
    return degenerate_phi_result (t);
  else if (TREE_CODE (t) == MODIFY_EXPR)
    return TREE_OPERAND (t, 1);
  gcc_unreachable ();
}


/* Given a tree node T, which is either a PHI_NODE or a MODIFY_EXPR,
   return the "lhs" of the node.  */

static tree
get_lhs_or_phi_result (tree t)
{
  if (TREE_CODE (t) == PHI_NODE)
    return PHI_RESULT (t);
  else if (TREE_CODE (t) == MODIFY_EXPR)
    return TREE_OPERAND (t, 0);
  gcc_unreachable ();
}

/* Propagate RHS into all uses of LHS (when possible).

   RHS and LHS are derived from STMT, which is passed in solely so
   that we can remove it if propagation is successful.

   When propagating into a PHI node or into a statement which turns
   into a trivial copy or constant initialization, set the
   appropriate bit in INTERESTING_NAMEs so that we will visit those
   nodes as well in an effort to pick up secondary optimization
   opportunities.  */

static void 
propagate_rhs_into_lhs (tree stmt, tree lhs, tree rhs, bitmap interesting_names)
{
  /* First verify that propagation is valid and isn't going to move a
     loop variant variable outside its loop.  */
  if (! SSA_NAME_OCCURS_IN_ABNORMAL_PHI (lhs)
      && (TREE_CODE (rhs) != SSA_NAME
	  || ! SSA_NAME_OCCURS_IN_ABNORMAL_PHI (rhs))
      && may_propagate_copy (lhs, rhs)
      && loop_depth_of_name (lhs) >= loop_depth_of_name (rhs))
    {
      use_operand_p use_p;
      imm_use_iterator iter;
      tree use_stmt;
      bool all = true;

      /* Dump details.  */
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "  Replacing '");
	  print_generic_expr (dump_file, lhs, dump_flags);
	  fprintf (dump_file, "' with %s '",
	           (TREE_CODE (rhs) != SSA_NAME ? "constant" : "variable"));
		   print_generic_expr (dump_file, rhs, dump_flags);
	  fprintf (dump_file, "'\n");
	}

      /* Walk over every use of LHS and try to replace the use with RHS. 
	 At this point the only reason why such a propagation would not
	 be successful would be if the use occurs in an ASM_EXPR.  */
      FOR_EACH_IMM_USE_STMT (use_stmt, iter, lhs)
	{
	
	  /* It's not always safe to propagate into an ASM_EXPR.  */
	  if (TREE_CODE (use_stmt) == ASM_EXPR
	      && ! may_propagate_copy_into_asm (lhs))
	    {
	      all = false;
	      continue;
	    }

	  /* Dump details.  */
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "    Original statement:");
	      print_generic_expr (dump_file, use_stmt, dump_flags);
	      fprintf (dump_file, "\n");
	    }

	  /* Propagate the RHS into this use of the LHS.  */
	  FOR_EACH_IMM_USE_ON_STMT (use_p, iter)
	    propagate_value (use_p, rhs);

	  /* Special cases to avoid useless calls into the folding
	     routines, operand scanning, etc.

	     First, propagation into a PHI may cause the PHI to become
	     a degenerate, so mark the PHI as interesting.  No other
	     actions are necessary.

	     Second, if we're propagating a virtual operand and the
	     propagation does not change the underlying _DECL node for
	     the virtual operand, then no further actions are necessary.  */
	  if (TREE_CODE (use_stmt) == PHI_NODE
	      || (! is_gimple_reg (lhs)
		  && TREE_CODE (rhs) == SSA_NAME
		  && SSA_NAME_VAR (lhs) == SSA_NAME_VAR (rhs)))
	    {
	      /* Dump details.  */
	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "    Updated statement:");
		  print_generic_expr (dump_file, use_stmt, dump_flags);
		  fprintf (dump_file, "\n");
		}

	      /* Propagation into a PHI may expose new degenerate PHIs,
		 so mark the result of the PHI as interesting.  */
	      if (TREE_CODE (use_stmt) == PHI_NODE)
		{
		  tree result = get_lhs_or_phi_result (use_stmt);
		  bitmap_set_bit (interesting_names, SSA_NAME_VERSION (result));
		}
	      continue;
	    }

	  /* From this point onward we are propagating into a 
	     real statement.  Folding may (or may not) be possible,
	     we may expose new operands, expose dead EH edges,
	     etc.  */
	  fold_stmt_inplace (use_stmt);

	  /* Sometimes propagation can expose new operands to the
	     renamer.  Note this will call update_stmt at the 
	     appropriate time.  */
	  mark_new_vars_to_rename (use_stmt);

	  /* Dump details.  */
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "    Updated statement:");
	      print_generic_expr (dump_file, use_stmt, dump_flags);
	      fprintf (dump_file, "\n");
	    }

	  /* If we replaced a variable index with a constant, then
	     we would need to update the invariant flag for ADDR_EXPRs.  */
	  if (TREE_CODE (use_stmt) == MODIFY_EXPR
	      && TREE_CODE (TREE_OPERAND (use_stmt, 1)) == ADDR_EXPR)
	    recompute_tree_invariant_for_addr_expr (TREE_OPERAND (use_stmt, 1));

	  /* If we cleaned up EH information from the statement,
	     mark its containing block as needing EH cleanups.  */
	  if (maybe_clean_or_replace_eh_stmt (use_stmt, use_stmt))
	    {
	      bitmap_set_bit (need_eh_cleanup, bb_for_stmt (use_stmt)->index);
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "  Flagged to clear EH edges.\n");
	    }

	  /* Propagation may expose new trivial copy/constant propagation
	     opportunities.  */
	  if (TREE_CODE (use_stmt) == MODIFY_EXPR
	      && TREE_CODE (TREE_OPERAND (use_stmt, 0)) == SSA_NAME
	      && (TREE_CODE (TREE_OPERAND (use_stmt, 1)) == SSA_NAME
		  || is_gimple_min_invariant (TREE_OPERAND (use_stmt, 1))))
	    {
	      tree result = get_lhs_or_phi_result (use_stmt);
	      bitmap_set_bit (interesting_names, SSA_NAME_VERSION (result));
	    }

	  /* Propagation into these nodes may make certain edges in
	     the CFG unexecutable.  We want to identify them as PHI nodes
	     at the destination of those unexecutable edges may become
	     degenerates.  */
	  else if (TREE_CODE (use_stmt) == COND_EXPR
		   || TREE_CODE (use_stmt) == SWITCH_EXPR
		   || TREE_CODE (use_stmt) == GOTO_EXPR)
	    {
	      tree val;

	      if (TREE_CODE (use_stmt) == COND_EXPR)
		val = COND_EXPR_COND (use_stmt);
	      else if (TREE_CODE (use_stmt) == SWITCH_EXPR)
		val = SWITCH_COND (use_stmt);
	      else
		val = GOTO_DESTINATION  (use_stmt);

	      if (is_gimple_min_invariant (val))
		{
		  basic_block bb = bb_for_stmt (use_stmt);
		  edge te = find_taken_edge (bb, val);
		  edge_iterator ei;
		  edge e;
		  block_stmt_iterator bsi;

		  /* Remove all outgoing edges except TE.  */
		  for (ei = ei_start (bb->succs); (e = ei_safe_edge (ei));)
		    {
		      if (e != te)
			{
			  tree phi;

			  /* Mark all the PHI nodes at the destination of
			     the unexecutable edge as interesting.  */
			  for (phi = phi_nodes (e->dest);
			       phi;
			       phi = PHI_CHAIN (phi))
			    {
			      tree result = PHI_RESULT (phi);
			      int version = SSA_NAME_VERSION (result);

			      bitmap_set_bit (interesting_names, version);
			    }

			  te->probability += e->probability;

			  te->count += e->count;
			  remove_edge (e);
			  cfg_altered = 1;
			}
		      else
			ei_next (&ei);
		    }

		  bsi = bsi_last (bb_for_stmt (use_stmt));
		  bsi_remove (&bsi, true);

		  /* And fixup the flags on the single remaining edge.  */
		  te->flags &= ~(EDGE_TRUE_VALUE | EDGE_FALSE_VALUE);
		  te->flags &= ~EDGE_ABNORMAL;
		  te->flags |= EDGE_FALLTHRU;
		  if (te->probability > REG_BR_PROB_BASE)
		    te->probability = REG_BR_PROB_BASE;
	        }
	    }
	}

      /* Ensure there is nothing else to do. */ 
      gcc_assert (!all || has_zero_uses (lhs));

      /* If we were able to propagate away all uses of LHS, then
	 we can remove STMT.  */
      if (all)
	remove_stmt_or_phi (stmt);
    }
}

/* T is either a PHI node (potentially a degenerate PHI node) or
   a statement that is a trivial copy or constant initialization.

   Attempt to eliminate T by propagating its RHS into all uses of
   its LHS.  This may in turn set new bits in INTERESTING_NAMES
   for nodes we want to revisit later.

   All exit paths should clear INTERESTING_NAMES for the result
   of T.  */

static void
eliminate_const_or_copy (tree t, bitmap interesting_names)
{
  tree lhs = get_lhs_or_phi_result (t);
  tree rhs;
  int version = SSA_NAME_VERSION (lhs);

  /* If the LHS of this statement or PHI has no uses, then we can
     just eliminate it.  This can occur if, for example, the PHI
     was created by block duplication due to threading and its only
     use was in the conditional at the end of the block which was
     deleted.  */
  if (has_zero_uses (lhs))
    {
      bitmap_clear_bit (interesting_names, version);
      remove_stmt_or_phi (t);
      return;
    }

  /* Get the RHS of the assignment or PHI node if the PHI is a
     degenerate.  */
  rhs = get_rhs_or_phi_arg (t);
  if (!rhs)
    {
      bitmap_clear_bit (interesting_names, version);
      return;
    }

  propagate_rhs_into_lhs (t, lhs, rhs, interesting_names);

  /* Note that T may well have been deleted by now, so do
     not access it, instead use the saved version # to clear
     T's entry in the worklist.  */
  bitmap_clear_bit (interesting_names, version);
}

/* The first phase in degenerate PHI elimination.

   Eliminate the degenerate PHIs in BB, then recurse on the
   dominator children of BB.  */

static void
eliminate_degenerate_phis_1 (basic_block bb, bitmap interesting_names)
{
  tree phi, next;
  basic_block son;

  for (phi = phi_nodes (bb); phi; phi = next)
    {
      next = PHI_CHAIN (phi);
      eliminate_const_or_copy (phi, interesting_names);
    }

  /* Recurse into the dominator children of BB.  */
  for (son = first_dom_son (CDI_DOMINATORS, bb);
       son;
       son = next_dom_son (CDI_DOMINATORS, son))
    eliminate_degenerate_phis_1 (son, interesting_names);
}


/* A very simple pass to eliminate degenerate PHI nodes from the
   IL.  This is meant to be fast enough to be able to be run several
   times in the optimization pipeline.

   Certain optimizations, particularly those which duplicate blocks
   or remove edges from the CFG can create or expose PHIs which are
   trivial copies or constant initializations.

   While we could pick up these optimizations in DOM or with the
   combination of copy-prop and CCP, those solutions are far too
   heavy-weight for our needs.

   This implementation has two phases so that we can efficiently
   eliminate the first order degenerate PHIs and second order
   degenerate PHIs.

   The first phase performs a dominator walk to identify and eliminate
   the vast majority of the degenerate PHIs.  When a degenerate PHI
   is identified and eliminated any affected statements or PHIs
   are put on a worklist.

   The second phase eliminates degenerate PHIs and trivial copies
   or constant initializations using the worklist.  This is how we
   pick up the secondary optimization opportunities with minimal
   cost.  */

static unsigned int
eliminate_degenerate_phis (void)
{
  bitmap interesting_names;
  bitmap interesting_names1;

  /* Bitmap of blocks which need EH information updated.  We can not
     update it on-the-fly as doing so invalidates the dominator tree.  */
  need_eh_cleanup = BITMAP_ALLOC (NULL);

  /* INTERESTING_NAMES is effectively our worklist, indexed by
     SSA_NAME_VERSION.

     A set bit indicates that the statement or PHI node which
     defines the SSA_NAME should be (re)examined to determine if
     it has become a degenerate PHI or trivial const/copy propagation
     opportunity. 

     Experiments have show we generally get better compilation
     time behavior with bitmaps rather than sbitmaps.  */
  interesting_names = BITMAP_ALLOC (NULL);
  interesting_names1 = BITMAP_ALLOC (NULL);

  /* First phase.  Eliminate degenerate PHIs via a dominator
     walk of the CFG.

     Experiments have indicated that we generally get better
     compile-time behavior by visiting blocks in the first
     phase in dominator order.  Presumably this is because walking
     in dominator order leaves fewer PHIs for later examination
     by the worklist phase.  */
  calculate_dominance_info (CDI_DOMINATORS);
  eliminate_degenerate_phis_1 (ENTRY_BLOCK_PTR, interesting_names);

  /* Second phase.  Eliminate second order degenerate PHIs as well
     as trivial copies or constant initializations identified by
     the first phase or this phase.  Basically we keep iterating
     until our set of INTERESTING_NAMEs is empty.   */
  while (!bitmap_empty_p (interesting_names))
    {
      unsigned int i;
      bitmap_iterator bi;

      /* EXECUTE_IF_SET_IN_BITMAP does not like its bitmap
	 changed during the loop.  Copy it to another bitmap and
	 use that.  */
      bitmap_copy (interesting_names1, interesting_names);

      EXECUTE_IF_SET_IN_BITMAP (interesting_names1, 0, i, bi)
	{
	  tree name = ssa_name (i);

	  /* Ignore SSA_NAMEs that have been released because
	     their defining statement was deleted (unreachable).  */
	  if (name)
	    eliminate_const_or_copy (SSA_NAME_DEF_STMT (ssa_name (i)),
				     interesting_names);
	}
    }

  /* Propagation of const and copies may make some EH edges dead.  Purge
     such edges from the CFG as needed.  */
  if (!bitmap_empty_p (need_eh_cleanup))
    {
      cfg_altered |= tree_purge_all_dead_eh_edges (need_eh_cleanup);
      BITMAP_FREE (need_eh_cleanup);
    }

  BITMAP_FREE (interesting_names);
  BITMAP_FREE (interesting_names1);
  if (cfg_altered)
    free_dominance_info (CDI_DOMINATORS);
  return 0;
}

struct tree_opt_pass pass_phi_only_cprop =
{
  "phicprop",                           /* name */
  gate_dominator,                       /* gate */
  eliminate_degenerate_phis,            /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_TREE_PHI_CPROP,                    /* tv_id */
  PROP_cfg | PROP_ssa | PROP_alias,     /* properties_required */
  0,                                    /* properties_provided */
  PROP_smt_usage,                       /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_cleanup_cfg | TODO_dump_func 
    | TODO_ggc_collect | TODO_verify_ssa
    | TODO_verify_stmts | TODO_update_smt_usage
    | TODO_update_ssa, /* todo_flags_finish */
  0                                     /* letter */
};
