/* The tracer pass for the GNU compiler.
   Contributed by Jan Hubicka, SuSE Labs.
   Copyright (C) 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* This pass performs the tail duplication needed for superblock formation.
   For more information see:

     Design and Analysis of Profile-Based Optimization in Compaq's
     Compilation Tools for Alpha; Journal of Instruction-Level
     Parallelism 3 (2000) 1-25

   Unlike Compaq's implementation we don't do the loop peeling as most
   probably a better job can be done by a special pass and we don't
   need to worry too much about the code size implications as the tail
   duplicates are crossjumped again if optimizations are not
   performed.  */


#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "cfglayout.h"
#include "fibheap.h"
#include "flags.h"
#include "timevar.h"
#include "params.h"
#include "coverage.h"
#include "tree-pass.h"

static int count_insns (basic_block);
static bool ignore_bb_p (basic_block);
static bool better_p (edge, edge);
static edge find_best_successor (basic_block);
static edge find_best_predecessor (basic_block);
static int find_trace (basic_block, basic_block *);
static void tail_duplicate (void);
static void layout_superblocks (void);

/* Minimal outgoing edge probability considered for superblock formation.  */
static int probability_cutoff;
static int branch_ratio_cutoff;

/* Return true if BB has been seen - it is connected to some trace
   already.  */

#define seen(bb) (bb->il.rtl->visited || bb->aux)

/* Return true if we should ignore the basic block for purposes of tracing.  */
static bool
ignore_bb_p (basic_block bb)
{
  if (bb->index < NUM_FIXED_BLOCKS)
    return true;
  if (!maybe_hot_bb_p (bb))
    return true;
  return false;
}

/* Return number of instructions in the block.  */

static int
count_insns (basic_block bb)
{
  rtx insn;
  int n = 0;

  for (insn = BB_HEAD (bb);
       insn != NEXT_INSN (BB_END (bb));
       insn = NEXT_INSN (insn))
    if (active_insn_p (insn))
      n++;
  return n;
}

/* Return true if E1 is more frequent than E2.  */
static bool
better_p (edge e1, edge e2)
{
  if (e1->count != e2->count)
    return e1->count > e2->count;
  if (e1->src->frequency * e1->probability !=
      e2->src->frequency * e2->probability)
    return (e1->src->frequency * e1->probability
	    > e2->src->frequency * e2->probability);
  /* This is needed to avoid changes in the decision after
     CFG is modified.  */
  if (e1->src != e2->src)
    return e1->src->index > e2->src->index;
  return e1->dest->index > e2->dest->index;
}

/* Return most frequent successor of basic block BB.  */

static edge
find_best_successor (basic_block bb)
{
  edge e;
  edge best = NULL;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, bb->succs)
    if (!best || better_p (e, best))
      best = e;
  if (!best || ignore_bb_p (best->dest))
    return NULL;
  if (best->probability <= probability_cutoff)
    return NULL;
  return best;
}

/* Return most frequent predecessor of basic block BB.  */

static edge
find_best_predecessor (basic_block bb)
{
  edge e;
  edge best = NULL;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, bb->preds)
    if (!best || better_p (e, best))
      best = e;
  if (!best || ignore_bb_p (best->src))
    return NULL;
  if (EDGE_FREQUENCY (best) * REG_BR_PROB_BASE
      < bb->frequency * branch_ratio_cutoff)
    return NULL;
  return best;
}

/* Find the trace using bb and record it in the TRACE array.
   Return number of basic blocks recorded.  */

static int
find_trace (basic_block bb, basic_block *trace)
{
  int i = 0;
  edge e;

  if (dump_file)
    fprintf (dump_file, "Trace seed %i [%i]", bb->index, bb->frequency);

  while ((e = find_best_predecessor (bb)) != NULL)
    {
      basic_block bb2 = e->src;
      if (seen (bb2) || (e->flags & (EDGE_DFS_BACK | EDGE_COMPLEX))
	  || find_best_successor (bb2) != e)
	break;
      if (dump_file)
	fprintf (dump_file, ",%i [%i]", bb->index, bb->frequency);
      bb = bb2;
    }
  if (dump_file)
    fprintf (dump_file, " forward %i [%i]", bb->index, bb->frequency);
  trace[i++] = bb;

  /* Follow the trace in forward direction.  */
  while ((e = find_best_successor (bb)) != NULL)
    {
      bb = e->dest;
      if (seen (bb) || (e->flags & (EDGE_DFS_BACK | EDGE_COMPLEX))
	  || find_best_predecessor (bb) != e)
	break;
      if (dump_file)
	fprintf (dump_file, ",%i [%i]", bb->index, bb->frequency);
      trace[i++] = bb;
    }
  if (dump_file)
    fprintf (dump_file, "\n");
  return i;
}

/* Look for basic blocks in frequency order, construct traces and tail duplicate
   if profitable.  */

static void
tail_duplicate (void)
{
  fibnode_t *blocks = XCNEWVEC (fibnode_t, last_basic_block);
  basic_block *trace = XNEWVEC (basic_block, n_basic_blocks);
  int *counts = XNEWVEC (int, last_basic_block);
  int ninsns = 0, nduplicated = 0;
  gcov_type weighted_insns = 0, traced_insns = 0;
  fibheap_t heap = fibheap_new ();
  gcov_type cover_insns;
  int max_dup_insns;
  basic_block bb;

  if (profile_info && flag_branch_probabilities)
    probability_cutoff = PARAM_VALUE (TRACER_MIN_BRANCH_PROBABILITY_FEEDBACK);
  else
    probability_cutoff = PARAM_VALUE (TRACER_MIN_BRANCH_PROBABILITY);
  probability_cutoff = REG_BR_PROB_BASE / 100 * probability_cutoff;

  branch_ratio_cutoff =
    (REG_BR_PROB_BASE / 100 * PARAM_VALUE (TRACER_MIN_BRANCH_RATIO));

  FOR_EACH_BB (bb)
    {
      int n = count_insns (bb);
      if (!ignore_bb_p (bb))
	blocks[bb->index] = fibheap_insert (heap, -bb->frequency,
					    bb);

      counts [bb->index] = n;
      ninsns += n;
      weighted_insns += n * bb->frequency;
    }

  if (profile_info && flag_branch_probabilities)
    cover_insns = PARAM_VALUE (TRACER_DYNAMIC_COVERAGE_FEEDBACK);
  else
    cover_insns = PARAM_VALUE (TRACER_DYNAMIC_COVERAGE);
  cover_insns = (weighted_insns * cover_insns + 50) / 100;
  max_dup_insns = (ninsns * PARAM_VALUE (TRACER_MAX_CODE_GROWTH) + 50) / 100;

  while (traced_insns < cover_insns && nduplicated < max_dup_insns
         && !fibheap_empty (heap))
    {
      basic_block bb = fibheap_extract_min (heap);
      int n, pos;

      if (!bb)
	break;

      blocks[bb->index] = NULL;

      if (ignore_bb_p (bb))
	continue;
      gcc_assert (!seen (bb));

      n = find_trace (bb, trace);

      bb = trace[0];
      traced_insns += bb->frequency * counts [bb->index];
      if (blocks[bb->index])
	{
	  fibheap_delete_node (heap, blocks[bb->index]);
	  blocks[bb->index] = NULL;
	}

      for (pos = 1; pos < n; pos++)
	{
	  basic_block bb2 = trace[pos];

	  if (blocks[bb2->index])
	    {
	      fibheap_delete_node (heap, blocks[bb2->index]);
	      blocks[bb2->index] = NULL;
	    }
	  traced_insns += bb2->frequency * counts [bb2->index];
	  if (EDGE_COUNT (bb2->preds) > 1
	      && can_duplicate_block_p (bb2))
	    {
	      edge e;
	      basic_block old = bb2;

	      e = find_edge (bb, bb2);

	      nduplicated += counts [bb2->index];
	      bb2 = duplicate_block (bb2, e, bb);

	      /* Reconsider the original copy of block we've duplicated.
	         Removing the most common predecessor may make it to be
	         head.  */
	      blocks[old->index] =
		fibheap_insert (heap, -old->frequency, old);

	      if (dump_file)
		fprintf (dump_file, "Duplicated %i as %i [%i]\n",
			 old->index, bb2->index, bb2->frequency);
	    }
	  bb->aux = bb2;
	  bb2->il.rtl->visited = 1;
	  bb = bb2;
	  /* In case the trace became infrequent, stop duplicating.  */
	  if (ignore_bb_p (bb))
	    break;
	}
      if (dump_file)
	fprintf (dump_file, " covered now %.1f\n\n",
		 traced_insns * 100.0 / weighted_insns);
    }
  if (dump_file)
    fprintf (dump_file, "Duplicated %i insns (%i%%)\n", nduplicated,
	     nduplicated * 100 / ninsns);

  free (blocks);
  free (trace);
  free (counts);
  fibheap_delete (heap);
}

/* Connect the superblocks into linear sequence.  At the moment we attempt to keep
   the original order as much as possible, but the algorithm may be made smarter
   later if needed.  BB reordering pass should void most of the benefits of such
   change though.  */

static void
layout_superblocks (void)
{
  basic_block end = single_succ (ENTRY_BLOCK_PTR);
  basic_block bb = end->next_bb;

  while (bb != EXIT_BLOCK_PTR)
    {
      edge_iterator ei;
      edge e, best = NULL;
      while (end->aux)
	end = end->aux;

      FOR_EACH_EDGE (e, ei, end->succs)
	if (e->dest != EXIT_BLOCK_PTR
	    && e->dest != single_succ (ENTRY_BLOCK_PTR)
	    && !e->dest->il.rtl->visited
	    && (!best || EDGE_FREQUENCY (e) > EDGE_FREQUENCY (best)))
	  best = e;

      if (best)
	{
	  end->aux = best->dest;
	  best->dest->il.rtl->visited = 1;
	}
      else
	for (; bb != EXIT_BLOCK_PTR; bb = bb->next_bb)
	  {
	    if (!bb->il.rtl->visited)
	      {
		end->aux = bb;
		bb->il.rtl->visited = 1;
		break;
	      }
	  }
    }
}

/* Main entry point to this file.  FLAGS is the set of flags to pass
   to cfg_layout_initialize().  */

void
tracer (unsigned int flags)
{
  if (n_basic_blocks <= NUM_FIXED_BLOCKS + 1)
    return;

  cfg_layout_initialize (flags);
  mark_dfs_back_edges ();
  if (dump_file)
    dump_flow_info (dump_file, dump_flags);
  tail_duplicate ();
  layout_superblocks ();
  if (dump_file)
    dump_flow_info (dump_file, dump_flags);
  cfg_layout_finalize ();

  /* Merge basic blocks in duplicated traces.  */
  cleanup_cfg (CLEANUP_EXPENSIVE);
}

static bool
gate_handle_tracer (void)
{
  return (optimize > 0 && flag_tracer);
}

/* Run tracer.  */
static unsigned int
rest_of_handle_tracer (void)
{
  if (dump_file)
    dump_flow_info (dump_file, dump_flags);
  tracer (0);
  cleanup_cfg (CLEANUP_EXPENSIVE);
  reg_scan (get_insns (), max_reg_num ());
  return 0;
}

struct tree_opt_pass pass_tracer =
{
  "tracer",                             /* name */
  gate_handle_tracer,                   /* gate */
  rest_of_handle_tracer,                /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_TRACER,                            /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func,                       /* todo_flags_finish */
  'T'                                   /* letter */
};

