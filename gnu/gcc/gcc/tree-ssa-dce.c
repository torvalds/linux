/* Dead code elimination pass for the GNU compiler.
   Copyright (C) 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Ben Elliston <bje@redhat.com>
   and Andrew MacLeod <amacleod@redhat.com>
   Adapted to use control dependence by Steven Bosscher, SUSE Labs.
 
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

/* Dead code elimination.

   References:

     Building an Optimizing Compiler,
     Robert Morgan, Butterworth-Heinemann, 1998, Section 8.9.

     Advanced Compiler Design and Implementation,
     Steven Muchnick, Morgan Kaufmann, 1997, Section 18.10.

   Dead-code elimination is the removal of statements which have no
   impact on the program's output.  "Dead statements" have no impact
   on the program's output, while "necessary statements" may have
   impact on the output.

   The algorithm consists of three phases:
   1. Marking as necessary all statements known to be necessary,
      e.g. most function calls, writing a value to memory, etc;
   2. Propagating necessary statements, e.g., the statements
      giving values to operands in necessary statements; and
   3. Removing dead statements.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "ggc.h"

/* These RTL headers are needed for basic-block.h.  */
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "obstack.h"
#include "basic-block.h"

#include "tree.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-gimple.h"
#include "tree-dump.h"
#include "tree-pass.h"
#include "timevar.h"
#include "flags.h"
#include "cfgloop.h"
#include "tree-scalar-evolution.h"

static struct stmt_stats
{
  int total;
  int total_phis;
  int removed;
  int removed_phis;
} stats;

static VEC(tree,heap) *worklist;

/* Vector indicating an SSA name has already been processed and marked
   as necessary.  */
static sbitmap processed;

/* Vector indicating that last_stmt if a basic block has already been
   marked as necessary.  */
static sbitmap last_stmt_necessary;

/* Before we can determine whether a control branch is dead, we need to
   compute which blocks are control dependent on which edges.

   We expect each block to be control dependent on very few edges so we
   use a bitmap for each block recording its edges.  An array holds the
   bitmap.  The Ith bit in the bitmap is set if that block is dependent
   on the Ith edge.  */
static bitmap *control_dependence_map;

/* Vector indicating that a basic block has already had all the edges
   processed that it is control dependent on.  */
static sbitmap visited_control_parents;

/* TRUE if this pass alters the CFG (by removing control statements).
   FALSE otherwise.

   If this pass alters the CFG, then it will arrange for the dominators
   to be recomputed.  */
static bool cfg_altered;

/* Execute code that follows the macro for each edge (given number
   EDGE_NUMBER within the CODE) for which the block with index N is
   control dependent.  */
#define EXECUTE_IF_CONTROL_DEPENDENT(BI, N, EDGE_NUMBER)	\
  EXECUTE_IF_SET_IN_BITMAP (control_dependence_map[(N)], 0,	\
			    (EDGE_NUMBER), (BI))

/* Local function prototypes.  */
static inline void set_control_dependence_map_bit (basic_block, int);
static inline void clear_control_dependence_bitmap (basic_block);
static void find_all_control_dependences (struct edge_list *);
static void find_control_dependence (struct edge_list *, int);
static inline basic_block find_pdom (basic_block);

static inline void mark_stmt_necessary (tree, bool);
static inline void mark_operand_necessary (tree, bool);

static void mark_stmt_if_obviously_necessary (tree, bool);
static void find_obviously_necessary_stmts (struct edge_list *);

static void mark_control_dependent_edges_necessary (basic_block, struct edge_list *);
static void propagate_necessity (struct edge_list *);

static void eliminate_unnecessary_stmts (void);
static void remove_dead_phis (basic_block);
static void remove_dead_stmt (block_stmt_iterator *, basic_block);

static void print_stats (void);
static void tree_dce_init (bool);
static void tree_dce_done (bool);

/* Indicate block BB is control dependent on an edge with index EDGE_INDEX.  */
static inline void
set_control_dependence_map_bit (basic_block bb, int edge_index)
{
  if (bb == ENTRY_BLOCK_PTR)
    return;
  gcc_assert (bb != EXIT_BLOCK_PTR);
  bitmap_set_bit (control_dependence_map[bb->index], edge_index);
}

/* Clear all control dependences for block BB.  */
static inline void
clear_control_dependence_bitmap (basic_block bb)
{
  bitmap_clear (control_dependence_map[bb->index]);
}

/* Record all blocks' control dependences on all edges in the edge
   list EL, ala Morgan, Section 3.6.  */

static void
find_all_control_dependences (struct edge_list *el)
{
  int i;

  for (i = 0; i < NUM_EDGES (el); ++i)
    find_control_dependence (el, i);
}

/* Determine all blocks' control dependences on the given edge with edge_list
   EL index EDGE_INDEX, ala Morgan, Section 3.6.  */

static void
find_control_dependence (struct edge_list *el, int edge_index)
{
  basic_block current_block;
  basic_block ending_block;

  gcc_assert (INDEX_EDGE_PRED_BB (el, edge_index) != EXIT_BLOCK_PTR);

  if (INDEX_EDGE_PRED_BB (el, edge_index) == ENTRY_BLOCK_PTR)
    ending_block = single_succ (ENTRY_BLOCK_PTR);
  else
    ending_block = find_pdom (INDEX_EDGE_PRED_BB (el, edge_index));

  for (current_block = INDEX_EDGE_SUCC_BB (el, edge_index);
       current_block != ending_block && current_block != EXIT_BLOCK_PTR;
       current_block = find_pdom (current_block))
    {
      edge e = INDEX_EDGE (el, edge_index);

      /* For abnormal edges, we don't make current_block control
	 dependent because instructions that throw are always necessary
	 anyway.  */
      if (e->flags & EDGE_ABNORMAL)
	continue;

      set_control_dependence_map_bit (current_block, edge_index);
    }
}

/* Find the immediate postdominator PDOM of the specified basic block BLOCK.
   This function is necessary because some blocks have negative numbers.  */

static inline basic_block
find_pdom (basic_block block)
{
  gcc_assert (block != ENTRY_BLOCK_PTR);

  if (block == EXIT_BLOCK_PTR)
    return EXIT_BLOCK_PTR;
  else
    {
      basic_block bb = get_immediate_dominator (CDI_POST_DOMINATORS, block);
      if (! bb)
	return EXIT_BLOCK_PTR;
      return bb;
    }
}

#define NECESSARY(stmt)		stmt->common.asm_written_flag

/* If STMT is not already marked necessary, mark it, and add it to the
   worklist if ADD_TO_WORKLIST is true.  */
static inline void
mark_stmt_necessary (tree stmt, bool add_to_worklist)
{
  gcc_assert (stmt);
  gcc_assert (!DECL_P (stmt));

  if (NECESSARY (stmt))
    return;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Marking useful stmt: ");
      print_generic_stmt (dump_file, stmt, TDF_SLIM);
      fprintf (dump_file, "\n");
    }

  NECESSARY (stmt) = 1;
  if (add_to_worklist)
    VEC_safe_push (tree, heap, worklist, stmt);
}

/* Mark the statement defining operand OP as necessary.  PHIONLY is true
   if we should only mark it necessary if it is a phi node.  */

static inline void
mark_operand_necessary (tree op, bool phionly)
{
  tree stmt;
  int ver;

  gcc_assert (op);

  ver = SSA_NAME_VERSION (op);
  if (TEST_BIT (processed, ver))
    return;
  SET_BIT (processed, ver);

  stmt = SSA_NAME_DEF_STMT (op);
  gcc_assert (stmt);

  if (NECESSARY (stmt)
      || IS_EMPTY_STMT (stmt)
      || (phionly && TREE_CODE (stmt) != PHI_NODE))
    return;

  NECESSARY (stmt) = 1;
  VEC_safe_push (tree, heap, worklist, stmt);
}


/* Mark STMT as necessary if it obviously is.  Add it to the worklist if
   it can make other statements necessary.

   If AGGRESSIVE is false, control statements are conservatively marked as
   necessary.  */

static void
mark_stmt_if_obviously_necessary (tree stmt, bool aggressive)
{
  stmt_ann_t ann;
  tree op;

  /* With non-call exceptions, we have to assume that all statements could
     throw.  If a statement may throw, it is inherently necessary.  */
  if (flag_non_call_exceptions
      && tree_could_throw_p (stmt))
    {
      mark_stmt_necessary (stmt, true);
      return;
    }

  /* Statements that are implicitly live.  Most function calls, asm and return
     statements are required.  Labels and BIND_EXPR nodes are kept because
     they are control flow, and we have no way of knowing whether they can be
     removed.  DCE can eliminate all the other statements in a block, and CFG
     can then remove the block and labels.  */
  switch (TREE_CODE (stmt))
    {
    case BIND_EXPR:
    case LABEL_EXPR:
    case CASE_LABEL_EXPR:
      mark_stmt_necessary (stmt, false);
      return;

    case ASM_EXPR:
    case RESX_EXPR:
    case RETURN_EXPR:
      mark_stmt_necessary (stmt, true);
      return;

    case CALL_EXPR:
      /* Most, but not all function calls are required.  Function calls that
	 produce no result and have no side effects (i.e. const pure
	 functions) are unnecessary.  */
      if (TREE_SIDE_EFFECTS (stmt))
	mark_stmt_necessary (stmt, true);
      return;

    case MODIFY_EXPR:
      op = get_call_expr_in (stmt);
      if (op && TREE_SIDE_EFFECTS (op))
	{
	  mark_stmt_necessary (stmt, true);
	  return;
	}

      /* These values are mildly magic bits of the EH runtime.  We can't
	 see the entire lifetime of these values until landing pads are
	 generated.  */
      if (TREE_CODE (TREE_OPERAND (stmt, 0)) == EXC_PTR_EXPR
	  || TREE_CODE (TREE_OPERAND (stmt, 0)) == FILTER_EXPR)
	{
	  mark_stmt_necessary (stmt, true);
	  return;
	}
      break;

    case GOTO_EXPR:
      gcc_assert (!simple_goto_p (stmt));
      mark_stmt_necessary (stmt, true);
      return;

    case COND_EXPR:
      gcc_assert (EDGE_COUNT (bb_for_stmt (stmt)->succs) == 2);
      /* Fall through.  */

    case SWITCH_EXPR:
      if (! aggressive)
	mark_stmt_necessary (stmt, true);
      break;

    default:
      break;
    }

  ann = stmt_ann (stmt);

  /* If the statement has volatile operands, it needs to be preserved.
     Same for statements that can alter control flow in unpredictable
     ways.  */
  if (ann->has_volatile_ops || is_ctrl_altering_stmt (stmt))
    {
      mark_stmt_necessary (stmt, true);
      return;
    }

  if (is_hidden_global_store (stmt))
    {
      mark_stmt_necessary (stmt, true);
      return;
    }

  return;
}

/* Find obviously necessary statements.  These are things like most function
   calls, and stores to file level variables.

   If EL is NULL, control statements are conservatively marked as
   necessary.  Otherwise it contains the list of edges used by control
   dependence analysis.  */

static void
find_obviously_necessary_stmts (struct edge_list *el)
{
  basic_block bb;
  block_stmt_iterator i;
  edge e;

  FOR_EACH_BB (bb)
    {
      tree phi;

      /* Check any PHI nodes in the block.  */
      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	{
	  NECESSARY (phi) = 0;

	  /* PHIs for virtual variables do not directly affect code
	     generation and need not be considered inherently necessary
	     regardless of the bits set in their decl.

	     Thus, we only need to mark PHIs for real variables which
	     need their result preserved as being inherently necessary.  */
	  if (is_gimple_reg (PHI_RESULT (phi))
	      && is_global_var (SSA_NAME_VAR (PHI_RESULT (phi))))
	    mark_stmt_necessary (phi, true);
        }

      /* Check all statements in the block.  */
      for (i = bsi_start (bb); ! bsi_end_p (i); bsi_next (&i))
	{
	  tree stmt = bsi_stmt (i);
	  NECESSARY (stmt) = 0;
	  mark_stmt_if_obviously_necessary (stmt, el != NULL);
	}
    }

  if (el)
    {
      /* Prevent the loops from being removed.  We must keep the infinite loops,
	 and we currently do not have a means to recognize the finite ones.  */
      FOR_EACH_BB (bb)
	{
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    if (e->flags & EDGE_DFS_BACK)
	      mark_control_dependent_edges_necessary (e->dest, el);
	}
    }
}

/* Make corresponding control dependent edges necessary.  We only
   have to do this once for each basic block, so we clear the bitmap
   after we're done.  */
static void
mark_control_dependent_edges_necessary (basic_block bb, struct edge_list *el)
{
  bitmap_iterator bi;
  unsigned edge_number;

  gcc_assert (bb != EXIT_BLOCK_PTR);

  if (bb == ENTRY_BLOCK_PTR)
    return;

  EXECUTE_IF_CONTROL_DEPENDENT (bi, bb->index, edge_number)
    {
      tree t;
      basic_block cd_bb = INDEX_EDGE_PRED_BB (el, edge_number);

      if (TEST_BIT (last_stmt_necessary, cd_bb->index))
	continue;
      SET_BIT (last_stmt_necessary, cd_bb->index);

      t = last_stmt (cd_bb);
      if (t && is_ctrl_stmt (t))
	mark_stmt_necessary (t, true);
    }
}

/* Propagate necessity using the operands of necessary statements.  Process
   the uses on each statement in the worklist, and add all feeding statements
   which contribute to the calculation of this value to the worklist.

   In conservative mode, EL is NULL.  */

static void
propagate_necessity (struct edge_list *el)
{
  tree i;
  bool aggressive = (el ? true : false); 

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\nProcessing worklist:\n");

  while (VEC_length (tree, worklist) > 0)
    {
      /* Take `i' from worklist.  */
      i = VEC_pop (tree, worklist);

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "processing: ");
	  print_generic_stmt (dump_file, i, TDF_SLIM);
	  fprintf (dump_file, "\n");
	}

      if (aggressive)
	{
	  /* Mark the last statements of the basic blocks that the block
	     containing `i' is control dependent on, but only if we haven't
	     already done so.  */
	  basic_block bb = bb_for_stmt (i);
	  if (bb != ENTRY_BLOCK_PTR
	      && ! TEST_BIT (visited_control_parents, bb->index))
	    {
	      SET_BIT (visited_control_parents, bb->index);
	      mark_control_dependent_edges_necessary (bb, el);
	    }
	}

      if (TREE_CODE (i) == PHI_NODE)
	{
	  /* PHI nodes are somewhat special in that each PHI alternative has
	     data and control dependencies.  All the statements feeding the
	     PHI node's arguments are always necessary.  In aggressive mode,
	     we also consider the control dependent edges leading to the
	     predecessor block associated with each PHI alternative as
	     necessary.  */
	  int k;
	  for (k = 0; k < PHI_NUM_ARGS (i); k++)
            {
	      tree arg = PHI_ARG_DEF (i, k);
	      if (TREE_CODE (arg) == SSA_NAME)
		mark_operand_necessary (arg, false);
	    }

	  if (aggressive)
	    {
	      for (k = 0; k < PHI_NUM_ARGS (i); k++)
		{
		  basic_block arg_bb = PHI_ARG_EDGE (i, k)->src;
		  if (arg_bb != ENTRY_BLOCK_PTR
		      && ! TEST_BIT (visited_control_parents, arg_bb->index))
		    {
		      SET_BIT (visited_control_parents, arg_bb->index);
		      mark_control_dependent_edges_necessary (arg_bb, el);
		    }
		}
	    }
	}
      else
	{
	  /* Propagate through the operands.  Examine all the USE, VUSE and
	     V_MAY_DEF operands in this statement.  Mark all the statements 
	     which feed this statement's uses as necessary.  */
	  ssa_op_iter iter;
	  tree use;

	  /* The operands of V_MAY_DEF expressions are also needed as they
	     represent potential definitions that may reach this
	     statement (V_MAY_DEF operands allow us to follow def-def 
	     links).  */

	  FOR_EACH_SSA_TREE_OPERAND (use, i, iter, SSA_OP_ALL_USES)
	    mark_operand_necessary (use, false);
	}
    }
}


/* Propagate necessity around virtual phi nodes used in kill operands.
   The reason this isn't done during propagate_necessity is because we don't
   want to keep phis around that are just there for must-defs, unless we
   absolutely have to.  After we've rewritten the reaching definitions to be
   correct in the previous part of the fixup routine, we can simply propagate
   around the information about which of these virtual phi nodes are really
   used, and set the NECESSARY flag accordingly.
   Note that we do the minimum here to ensure that we keep alive the phis that
   are actually used in the corrected SSA form.  In particular, some of these
   phis may now have all of the same operand, and will be deleted by some
   other pass.  */

static void
mark_really_necessary_kill_operand_phis (void)
{
  basic_block bb;
  int i;

  /* Seed the worklist with the new virtual phi arguments and virtual
     uses */
  FOR_EACH_BB (bb)
    {
      block_stmt_iterator bsi;
      tree phi;
      
      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	{
	  if (!is_gimple_reg (PHI_RESULT (phi)) && NECESSARY (phi))
	    {
	      for (i = 0; i < PHI_NUM_ARGS (phi); i++)
		mark_operand_necessary (PHI_ARG_DEF (phi, i), true);
	    }
	}
      
      for (bsi = bsi_last (bb); !bsi_end_p (bsi); bsi_prev (&bsi))
	{
	  tree stmt = bsi_stmt (bsi);
	
	  if (NECESSARY (stmt))
	    {
	      use_operand_p use_p;
	      ssa_op_iter iter;
	      FOR_EACH_SSA_USE_OPERAND (use_p, stmt, iter,
					SSA_OP_VIRTUAL_USES | SSA_OP_VIRTUAL_KILLS)
		{
		  tree use = USE_FROM_PTR (use_p);
		  mark_operand_necessary (use, true);
		}
	    }
	}
    }
  
  /* Mark all virtual phis still in use as necessary, and all of their
     arguments that are phis as necessary.  */
  while (VEC_length (tree, worklist) > 0)
    {
      tree use = VEC_pop (tree, worklist);
      
      for (i = 0; i < PHI_NUM_ARGS (use); i++)
	mark_operand_necessary (PHI_ARG_DEF (use, i), true);
    }
}




/* Eliminate unnecessary statements. Any instruction not marked as necessary
   contributes nothing to the program, and can be deleted.  */

static void
eliminate_unnecessary_stmts (void)
{
  basic_block bb;
  block_stmt_iterator i;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "\nEliminating unnecessary statements:\n");
  
  clear_special_calls ();
  FOR_EACH_BB (bb)
    {
      /* Remove dead PHI nodes.  */
      remove_dead_phis (bb);
    }

  FOR_EACH_BB (bb)
    {
      /* Remove dead statements.  */
      for (i = bsi_start (bb); ! bsi_end_p (i) ; )
	{
         tree t = bsi_stmt (i);

         stats.total++;

         /* If `i' is not necessary then remove it.  */
         if (! NECESSARY (t))
           remove_dead_stmt (&i, bb);
         else
           {
             tree call = get_call_expr_in (t);
             if (call)
               notice_special_calls (call);
             bsi_next (&i);
           }
	}
    }
 }

/* Remove dead PHI nodes from block BB.  */

static void
remove_dead_phis (basic_block bb)
{
  tree prev, phi;

  prev = NULL_TREE;
  phi = phi_nodes (bb);
  while (phi)
    {
      stats.total_phis++;

      if (! NECESSARY (phi))
	{
	  tree next = PHI_CHAIN (phi);

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "Deleting : ");
	      print_generic_stmt (dump_file, phi, TDF_SLIM);
	      fprintf (dump_file, "\n");
	    }

	  remove_phi_node (phi, prev);
	  stats.removed_phis++;
	  phi = next;
	}
      else
	{
	  prev = phi;
	  phi = PHI_CHAIN (phi);
	}
    }
}

/* Remove dead statement pointed to by iterator I.  Receives the basic block BB
   containing I so that we don't have to look it up.  */

static void
remove_dead_stmt (block_stmt_iterator *i, basic_block bb)
{
  tree t = bsi_stmt (*i);
  def_operand_p def_p;

  ssa_op_iter iter;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Deleting : ");
      print_generic_stmt (dump_file, t, TDF_SLIM);
      fprintf (dump_file, "\n");
    }

  stats.removed++;

  /* If we have determined that a conditional branch statement contributes
     nothing to the program, then we not only remove it, but we also change
     the flow graph so that the current block will simply fall-thru to its
     immediate post-dominator.  The blocks we are circumventing will be
     removed by cleanup_tree_cfg if this change in the flow graph makes them
     unreachable.  */
  if (is_ctrl_stmt (t))
    {
      basic_block post_dom_bb;

      /* The post dominance info has to be up-to-date.  */
      gcc_assert (dom_computed[CDI_POST_DOMINATORS] == DOM_OK);
      /* Get the immediate post dominator of bb.  */
      post_dom_bb = get_immediate_dominator (CDI_POST_DOMINATORS, bb);

      /* There are three particularly problematical cases.

	 1. Blocks that do not have an immediate post dominator.  This
	    can happen with infinite loops.

	 2. Blocks that are only post dominated by the exit block.  These
	    can also happen for infinite loops as we create fake edges
	    in the dominator tree.

	 3. If the post dominator has PHI nodes we may be able to compute
	    the right PHI args for them.


	 In each of these cases we must remove the control statement
	 as it may reference SSA_NAMEs which are going to be removed and
	 we remove all but one outgoing edge from the block.  */
      if (! post_dom_bb
	  || post_dom_bb == EXIT_BLOCK_PTR
	  || phi_nodes (post_dom_bb))
	;
      else
	{
	  /* Redirect the first edge out of BB to reach POST_DOM_BB.  */
	  redirect_edge_and_branch (EDGE_SUCC (bb, 0), post_dom_bb);
	  PENDING_STMT (EDGE_SUCC (bb, 0)) = NULL;
	}
      EDGE_SUCC (bb, 0)->probability = REG_BR_PROB_BASE;
      EDGE_SUCC (bb, 0)->count = bb->count;

      /* The edge is no longer associated with a conditional, so it does
	 not have TRUE/FALSE flags.  */
      EDGE_SUCC (bb, 0)->flags &= ~(EDGE_TRUE_VALUE | EDGE_FALSE_VALUE);

      /* The lone outgoing edge from BB will be a fallthru edge.  */
      EDGE_SUCC (bb, 0)->flags |= EDGE_FALLTHRU;

      /* Remove the remaining the outgoing edges.  */
      while (!single_succ_p (bb))
	{
	  /* FIXME.  When we remove the edge, we modify the CFG, which
	     in turn modifies the dominator and post-dominator tree.
	     Is it safe to postpone recomputing the dominator and
	     post-dominator tree until the end of this pass given that
	     the post-dominators are used above?  */
	  cfg_altered = true;
          remove_edge (EDGE_SUCC (bb, 1));
	}
    }
  
  FOR_EACH_SSA_DEF_OPERAND (def_p, t, iter, SSA_OP_VIRTUAL_DEFS)
    {
      tree def = DEF_FROM_PTR (def_p);
      mark_sym_for_renaming (SSA_NAME_VAR (def));
    }
  bsi_remove (i, true);  
  release_defs (t); 
}

/* Print out removed statement statistics.  */

static void
print_stats (void)
{
  if (dump_file && (dump_flags & (TDF_STATS|TDF_DETAILS)))
    {
      float percg;

      percg = ((float) stats.removed / (float) stats.total) * 100;
      fprintf (dump_file, "Removed %d of %d statements (%d%%)\n",
	       stats.removed, stats.total, (int) percg);

      if (stats.total_phis == 0)
	percg = 0;
      else
	percg = ((float) stats.removed_phis / (float) stats.total_phis) * 100;

      fprintf (dump_file, "Removed %d of %d PHI nodes (%d%%)\n",
	       stats.removed_phis, stats.total_phis, (int) percg);
    }
}

/* Initialization for this pass.  Set up the used data structures.  */

static void
tree_dce_init (bool aggressive)
{
  memset ((void *) &stats, 0, sizeof (stats));

  if (aggressive)
    {
      int i;

      control_dependence_map = XNEWVEC (bitmap, last_basic_block);
      for (i = 0; i < last_basic_block; ++i)
	control_dependence_map[i] = BITMAP_ALLOC (NULL);

      last_stmt_necessary = sbitmap_alloc (last_basic_block);
      sbitmap_zero (last_stmt_necessary);
    }

  processed = sbitmap_alloc (num_ssa_names + 1);
  sbitmap_zero (processed);

  worklist = VEC_alloc (tree, heap, 64);
  cfg_altered = false;
}

/* Cleanup after this pass.  */

static void
tree_dce_done (bool aggressive)
{
  if (aggressive)
    {
      int i;

      for (i = 0; i < last_basic_block; ++i)
	BITMAP_FREE (control_dependence_map[i]);
      free (control_dependence_map);

      sbitmap_free (visited_control_parents);
      sbitmap_free (last_stmt_necessary);
    }

  sbitmap_free (processed);

  VEC_free (tree, heap, worklist);
}

/* Main routine to eliminate dead code.

   AGGRESSIVE controls the aggressiveness of the algorithm.
   In conservative mode, we ignore control dependence and simply declare
   all but the most trivially dead branches necessary.  This mode is fast.
   In aggressive mode, control dependences are taken into account, which
   results in more dead code elimination, but at the cost of some time.

   FIXME: Aggressive mode before PRE doesn't work currently because
	  the dominance info is not invalidated after DCE1.  This is
	  not an issue right now because we only run aggressive DCE
	  as the last tree SSA pass, but keep this in mind when you
	  start experimenting with pass ordering.  */

static void
perform_tree_ssa_dce (bool aggressive)
{
  struct edge_list *el = NULL;

  tree_dce_init (aggressive);

  if (aggressive)
    {
      /* Compute control dependence.  */
      timevar_push (TV_CONTROL_DEPENDENCES);
      calculate_dominance_info (CDI_POST_DOMINATORS);
      el = create_edge_list ();
      find_all_control_dependences (el);
      timevar_pop (TV_CONTROL_DEPENDENCES);

      visited_control_parents = sbitmap_alloc (last_basic_block);
      sbitmap_zero (visited_control_parents);

      mark_dfs_back_edges ();
    }

  find_obviously_necessary_stmts (el);

  propagate_necessity (el);

  mark_really_necessary_kill_operand_phis ();
  eliminate_unnecessary_stmts ();

  if (aggressive)
    free_dominance_info (CDI_POST_DOMINATORS);

  /* If we removed paths in the CFG, then we need to update
     dominators as well.  I haven't investigated the possibility
     of incrementally updating dominators.  */
  if (cfg_altered)
    free_dominance_info (CDI_DOMINATORS);

  /* Debugging dumps.  */
  if (dump_file)
    print_stats ();

  tree_dce_done (aggressive);

  free_edge_list (el);
}

/* Pass entry points.  */
static unsigned int
tree_ssa_dce (void)
{
  perform_tree_ssa_dce (/*aggressive=*/false);
  return 0;
}

static unsigned int
tree_ssa_dce_loop (void)
{
  perform_tree_ssa_dce (/*aggressive=*/false);
  free_numbers_of_iterations_estimates (current_loops);
  scev_reset ();
  return 0;
}

static unsigned int
tree_ssa_cd_dce (void)
{
  perform_tree_ssa_dce (/*aggressive=*/optimize >= 2);
  return 0;
}

static bool
gate_dce (void)
{
  return flag_tree_dce != 0;
}

struct tree_opt_pass pass_dce =
{
  "dce",				/* name */
  gate_dce,				/* gate */
  tree_ssa_dce,				/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_DCE,				/* tv_id */
  PROP_cfg | PROP_ssa | PROP_alias,	/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func 
    | TODO_update_ssa
    | TODO_cleanup_cfg
    | TODO_ggc_collect
    | TODO_verify_ssa
    | TODO_remove_unused_locals,	/* todo_flags_finish */
  0					/* letter */
};

struct tree_opt_pass pass_dce_loop =
{
  "dceloop",				/* name */
  gate_dce,				/* gate */
  tree_ssa_dce_loop,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_DCE,				/* tv_id */
  PROP_cfg | PROP_ssa | PROP_alias,	/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func 
    | TODO_update_ssa
    | TODO_cleanup_cfg
    | TODO_verify_ssa,			/* todo_flags_finish */
  0					/* letter */
};

struct tree_opt_pass pass_cd_dce =
{
  "cddce",				/* name */
  gate_dce,				/* gate */
  tree_ssa_cd_dce,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_CD_DCE,			/* tv_id */
  PROP_cfg | PROP_ssa | PROP_alias,	/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func
    | TODO_update_ssa
    | TODO_cleanup_cfg
    | TODO_ggc_collect
    | TODO_verify_ssa
    | TODO_verify_flow,			/* todo_flags_finish */
  0					/* letter */
};
