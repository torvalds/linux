/* Control flow graph manipulation code for GNU compiler.
   Copyright (C) 1987, 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.

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

/* This file contains low level functions to manipulate the CFG and
   analyze it.  All other modules should not transform the data structure
   directly and use abstraction instead.  The file is supposed to be
   ordered bottom-up and should not contain any code dependent on a
   particular intermediate language (RTL or trees).

   Available functionality:
     - Initialization/deallocation
	 init_flow, clear_edges
     - Low level basic block manipulation
	 alloc_block, expunge_block
     - Edge manipulation
	 make_edge, make_single_succ_edge, cached_make_edge, remove_edge
	 - Low level edge redirection (without updating instruction chain)
	     redirect_edge_succ, redirect_edge_succ_nodup, redirect_edge_pred
     - Dumping and debugging
	 dump_flow_info, debug_flow_info, dump_edge_info
     - Allocation of AUX fields for basic blocks
	 alloc_aux_for_blocks, free_aux_for_blocks, alloc_aux_for_block
     - clear_bb_flags
     - Consistency checking
	 verify_flow_info
     - Dumping and debugging
	 print_rtl_with_bb, dump_bb, debug_bb, debug_bb_n
 */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "hard-reg-set.h"
#include "regs.h"
#include "flags.h"
#include "output.h"
#include "function.h"
#include "except.h"
#include "toplev.h"
#include "tm_p.h"
#include "obstack.h"
#include "timevar.h"
#include "tree-pass.h"
#include "ggc.h"
#include "hashtab.h"
#include "alloc-pool.h"

/* The obstack on which the flow graph components are allocated.  */

struct bitmap_obstack reg_obstack;

void debug_flow_info (void);
static void free_edge (edge);

#define RDIV(X,Y) (((X) + (Y) / 2) / (Y))

/* Called once at initialization time.  */

void
init_flow (void)
{
  if (!cfun->cfg)
    cfun->cfg = ggc_alloc_cleared (sizeof (struct control_flow_graph));
  n_edges = 0;
  ENTRY_BLOCK_PTR = ggc_alloc_cleared (sizeof (struct basic_block_def));
  ENTRY_BLOCK_PTR->index = ENTRY_BLOCK;
  EXIT_BLOCK_PTR = ggc_alloc_cleared (sizeof (struct basic_block_def));
  EXIT_BLOCK_PTR->index = EXIT_BLOCK;
  ENTRY_BLOCK_PTR->next_bb = EXIT_BLOCK_PTR;
  EXIT_BLOCK_PTR->prev_bb = ENTRY_BLOCK_PTR;
}

/* Helper function for remove_edge and clear_edges.  Frees edge structure
   without actually unlinking it from the pred/succ lists.  */

static void
free_edge (edge e ATTRIBUTE_UNUSED)
{
  n_edges--;
  ggc_free (e);
}

/* Free the memory associated with the edge structures.  */

void
clear_edges (void)
{
  basic_block bb;
  edge e;
  edge_iterator ei;

  FOR_EACH_BB (bb)
    {
      FOR_EACH_EDGE (e, ei, bb->succs)
	free_edge (e);
      VEC_truncate (edge, bb->succs, 0);
      VEC_truncate (edge, bb->preds, 0);
    }

  FOR_EACH_EDGE (e, ei, ENTRY_BLOCK_PTR->succs)
    free_edge (e);
  VEC_truncate (edge, EXIT_BLOCK_PTR->preds, 0);
  VEC_truncate (edge, ENTRY_BLOCK_PTR->succs, 0);

  gcc_assert (!n_edges);
}

/* Allocate memory for basic_block.  */

basic_block
alloc_block (void)
{
  basic_block bb;
  bb = ggc_alloc_cleared (sizeof (*bb));
  return bb;
}

/* Link block B to chain after AFTER.  */
void
link_block (basic_block b, basic_block after)
{
  b->next_bb = after->next_bb;
  b->prev_bb = after;
  after->next_bb = b;
  b->next_bb->prev_bb = b;
}

/* Unlink block B from chain.  */
void
unlink_block (basic_block b)
{
  b->next_bb->prev_bb = b->prev_bb;
  b->prev_bb->next_bb = b->next_bb;
  b->prev_bb = NULL;
  b->next_bb = NULL;
}

/* Sequentially order blocks and compact the arrays.  */
void
compact_blocks (void)
{
  int i;
  basic_block bb;

  SET_BASIC_BLOCK (ENTRY_BLOCK, ENTRY_BLOCK_PTR);
  SET_BASIC_BLOCK (EXIT_BLOCK, EXIT_BLOCK_PTR);

  i = NUM_FIXED_BLOCKS;
  FOR_EACH_BB (bb) 
    {
      SET_BASIC_BLOCK (i, bb);
      bb->index = i;
      i++;
    }

  gcc_assert (i == n_basic_blocks);

  for (; i < last_basic_block; i++)
    SET_BASIC_BLOCK (i, NULL);

  last_basic_block = n_basic_blocks;
}

/* Remove block B from the basic block array.  */

void
expunge_block (basic_block b)
{
  unlink_block (b);
  SET_BASIC_BLOCK (b->index, NULL);
  n_basic_blocks--;
  /* We should be able to ggc_free here, but we are not.
     The dead SSA_NAMES are left pointing to dead statements that are pointing
     to dead basic blocks making garbage collector to die.
     We should be able to release all dead SSA_NAMES and at the same time we should
     clear out BB pointer of dead statements consistently.  */
}

/* Connect E to E->src.  */

static inline void
connect_src (edge e)
{
  VEC_safe_push (edge, gc, e->src->succs, e);
}

/* Connect E to E->dest.  */

static inline void
connect_dest (edge e)
{
  basic_block dest = e->dest;
  VEC_safe_push (edge, gc, dest->preds, e);
  e->dest_idx = EDGE_COUNT (dest->preds) - 1;
}

/* Disconnect edge E from E->src.  */

static inline void
disconnect_src (edge e)
{
  basic_block src = e->src;
  edge_iterator ei;
  edge tmp;

  for (ei = ei_start (src->succs); (tmp = ei_safe_edge (ei)); )
    {
      if (tmp == e)
	{
	  VEC_unordered_remove (edge, src->succs, ei.index);
	  return;
	}
      else
	ei_next (&ei);
    }

  gcc_unreachable ();
}

/* Disconnect edge E from E->dest.  */

static inline void
disconnect_dest (edge e)
{
  basic_block dest = e->dest;
  unsigned int dest_idx = e->dest_idx;

  VEC_unordered_remove (edge, dest->preds, dest_idx);

  /* If we removed an edge in the middle of the edge vector, we need
     to update dest_idx of the edge that moved into the "hole".  */
  if (dest_idx < EDGE_COUNT (dest->preds))
    EDGE_PRED (dest, dest_idx)->dest_idx = dest_idx;
}

/* Create an edge connecting SRC and DEST with flags FLAGS.  Return newly
   created edge.  Use this only if you are sure that this edge can't
   possibly already exist.  */

edge
unchecked_make_edge (basic_block src, basic_block dst, int flags)
{
  edge e;
  e = ggc_alloc_cleared (sizeof (*e));
  n_edges++;

  e->src = src;
  e->dest = dst;
  e->flags = flags;

  connect_src (e);
  connect_dest (e);

  execute_on_growing_pred (e);

  return e;
}

/* Create an edge connecting SRC and DST with FLAGS optionally using
   edge cache CACHE.  Return the new edge, NULL if already exist.  */

edge
cached_make_edge (sbitmap edge_cache, basic_block src, basic_block dst, int flags)
{
  if (edge_cache == NULL
      || src == ENTRY_BLOCK_PTR
      || dst == EXIT_BLOCK_PTR)
    return make_edge (src, dst, flags);

  /* Does the requested edge already exist?  */
  if (! TEST_BIT (edge_cache, dst->index))
    {
      /* The edge does not exist.  Create one and update the
	 cache.  */
      SET_BIT (edge_cache, dst->index);
      return unchecked_make_edge (src, dst, flags);
    }

  /* At this point, we know that the requested edge exists.  Adjust
     flags if necessary.  */
  if (flags)
    {
      edge e = find_edge (src, dst);
      e->flags |= flags;
    }

  return NULL;
}

/* Create an edge connecting SRC and DEST with flags FLAGS.  Return newly
   created edge or NULL if already exist.  */

edge
make_edge (basic_block src, basic_block dest, int flags)
{
  edge e = find_edge (src, dest);

  /* Make sure we don't add duplicate edges.  */
  if (e)
    {
      e->flags |= flags;
      return NULL;
    }

  return unchecked_make_edge (src, dest, flags);
}

/* Create an edge connecting SRC to DEST and set probability by knowing
   that it is the single edge leaving SRC.  */

edge
make_single_succ_edge (basic_block src, basic_block dest, int flags)
{
  edge e = make_edge (src, dest, flags);

  e->probability = REG_BR_PROB_BASE;
  e->count = src->count;
  return e;
}

/* This function will remove an edge from the flow graph.  */

void
remove_edge (edge e)
{
  remove_predictions_associated_with_edge (e);
  execute_on_shrinking_pred (e);

  disconnect_src (e);
  disconnect_dest (e);

  free_edge (e);
}

/* Redirect an edge's successor from one block to another.  */

void
redirect_edge_succ (edge e, basic_block new_succ)
{
  execute_on_shrinking_pred (e);

  disconnect_dest (e);

  e->dest = new_succ;

  /* Reconnect the edge to the new successor block.  */
  connect_dest (e);

  execute_on_growing_pred (e);
}

/* Like previous but avoid possible duplicate edge.  */

edge
redirect_edge_succ_nodup (edge e, basic_block new_succ)
{
  edge s;

  s = find_edge (e->src, new_succ);
  if (s && s != e)
    {
      s->flags |= e->flags;
      s->probability += e->probability;
      if (s->probability > REG_BR_PROB_BASE)
	s->probability = REG_BR_PROB_BASE;
      s->count += e->count;
      remove_edge (e);
      e = s;
    }
  else
    redirect_edge_succ (e, new_succ);

  return e;
}

/* Redirect an edge's predecessor from one block to another.  */

void
redirect_edge_pred (edge e, basic_block new_pred)
{
  disconnect_src (e);

  e->src = new_pred;

  /* Reconnect the edge to the new predecessor block.  */
  connect_src (e);
}

/* Clear all basic block flags, with the exception of partitioning.  */
void
clear_bb_flags (void)
{
  basic_block bb;

  FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR, NULL, next_bb)
    bb->flags = (BB_PARTITION (bb)  | (bb->flags & BB_DISABLE_SCHEDULE)
		 | (bb->flags & BB_RTL));
}

/* Check the consistency of profile information.  We can't do that
   in verify_flow_info, as the counts may get invalid for incompletely
   solved graphs, later eliminating of conditionals or roundoff errors.
   It is still practical to have them reported for debugging of simple
   testcases.  */
void
check_bb_profile (basic_block bb, FILE * file)
{
  edge e;
  int sum = 0;
  gcov_type lsum;
  edge_iterator ei;

  if (profile_status == PROFILE_ABSENT)
    return;

  if (bb != EXIT_BLOCK_PTR)
    {
      FOR_EACH_EDGE (e, ei, bb->succs)
	sum += e->probability;
      if (EDGE_COUNT (bb->succs) && abs (sum - REG_BR_PROB_BASE) > 100)
	fprintf (file, "Invalid sum of outgoing probabilities %.1f%%\n",
		 sum * 100.0 / REG_BR_PROB_BASE);
      lsum = 0;
      FOR_EACH_EDGE (e, ei, bb->succs)
	lsum += e->count;
      if (EDGE_COUNT (bb->succs)
	  && (lsum - bb->count > 100 || lsum - bb->count < -100))
	fprintf (file, "Invalid sum of outgoing counts %i, should be %i\n",
		 (int) lsum, (int) bb->count);
    }
  if (bb != ENTRY_BLOCK_PTR)
    {
      sum = 0;
      FOR_EACH_EDGE (e, ei, bb->preds)
	sum += EDGE_FREQUENCY (e);
      if (abs (sum - bb->frequency) > 100)
	fprintf (file,
		 "Invalid sum of incoming frequencies %i, should be %i\n",
		 sum, bb->frequency);
      lsum = 0;
      FOR_EACH_EDGE (e, ei, bb->preds)
	lsum += e->count;
      if (lsum - bb->count > 100 || lsum - bb->count < -100)
	fprintf (file, "Invalid sum of incoming counts %i, should be %i\n",
		 (int) lsum, (int) bb->count);
    }
}

/* Emit basic block information for BB.  HEADER is true if the user wants
   the generic information and the predecessors, FOOTER is true if they want
   the successors.  FLAGS is the dump flags of interest; TDF_DETAILS emit
   global register liveness information.  PREFIX is put in front of every
   line.  The output is emitted to FILE.  */
void
dump_bb_info (basic_block bb, bool header, bool footer, int flags,
	      const char *prefix, FILE *file)
{
  edge e;
  edge_iterator ei;

  if (header)
    {
      fprintf (file, "\n%sBasic block %d ", prefix, bb->index);
      if (bb->prev_bb)
        fprintf (file, ", prev %d", bb->prev_bb->index);
      if (bb->next_bb)
        fprintf (file, ", next %d", bb->next_bb->index);
      fprintf (file, ", loop_depth %d, count ", bb->loop_depth);
      fprintf (file, HOST_WIDEST_INT_PRINT_DEC, bb->count);
      fprintf (file, ", freq %i", bb->frequency);
      if (maybe_hot_bb_p (bb))
	fprintf (file, ", maybe hot");
      if (probably_never_executed_bb_p (bb))
	fprintf (file, ", probably never executed");
      fprintf (file, ".\n");

      fprintf (file, "%sPredecessors: ", prefix);
      FOR_EACH_EDGE (e, ei, bb->preds)
	dump_edge_info (file, e, 0);
   }

  if (footer)
    {
      fprintf (file, "\n%sSuccessors: ", prefix);
      FOR_EACH_EDGE (e, ei, bb->succs)
	dump_edge_info (file, e, 1);
   }

  if ((flags & TDF_DETAILS)
      && (bb->flags & BB_RTL))
    {
      if (bb->il.rtl->global_live_at_start && header)
	{
	  fprintf (file, "\n%sRegisters live at start:", prefix);
	  dump_regset (bb->il.rtl->global_live_at_start, file);
	}

      if (bb->il.rtl->global_live_at_end && footer)
	{
	  fprintf (file, "\n%sRegisters live at end:", prefix);
	  dump_regset (bb->il.rtl->global_live_at_end, file);
	}
   }

  putc ('\n', file);
}

void
dump_flow_info (FILE *file, int flags)
{
  basic_block bb;

  /* There are no pseudo registers after reload.  Don't dump them.  */
  if (reg_n_info && !reload_completed
      && (flags & TDF_DETAILS) != 0)
    {
      unsigned int i, max = max_reg_num ();
      fprintf (file, "%d registers.\n", max);
      for (i = FIRST_PSEUDO_REGISTER; i < max; i++)
	if (REG_N_REFS (i))
	  {
	    enum reg_class class, altclass;

	    fprintf (file, "\nRegister %d used %d times across %d insns",
		     i, REG_N_REFS (i), REG_LIVE_LENGTH (i));
	    if (REG_BASIC_BLOCK (i) >= 0)
	      fprintf (file, " in block %d", REG_BASIC_BLOCK (i));
	    if (REG_N_SETS (i))
	      fprintf (file, "; set %d time%s", REG_N_SETS (i),
		       (REG_N_SETS (i) == 1) ? "" : "s");
	    if (regno_reg_rtx[i] != NULL && REG_USERVAR_P (regno_reg_rtx[i]))
	      fprintf (file, "; user var");
	    if (REG_N_DEATHS (i) != 1)
	      fprintf (file, "; dies in %d places", REG_N_DEATHS (i));
	    if (REG_N_CALLS_CROSSED (i) == 1)
	      fprintf (file, "; crosses 1 call");
	    else if (REG_N_CALLS_CROSSED (i))
	      fprintf (file, "; crosses %d calls", REG_N_CALLS_CROSSED (i));
	    if (regno_reg_rtx[i] != NULL
		&& PSEUDO_REGNO_BYTES (i) != UNITS_PER_WORD)
	      fprintf (file, "; %d bytes", PSEUDO_REGNO_BYTES (i));

	    class = reg_preferred_class (i);
	    altclass = reg_alternate_class (i);
	    if (class != GENERAL_REGS || altclass != ALL_REGS)
	      {
		if (altclass == ALL_REGS || class == ALL_REGS)
		  fprintf (file, "; pref %s", reg_class_names[(int) class]);
		else if (altclass == NO_REGS)
		  fprintf (file, "; %s or none", reg_class_names[(int) class]);
		else
		  fprintf (file, "; pref %s, else %s",
			   reg_class_names[(int) class],
			   reg_class_names[(int) altclass]);
	      }

	    if (regno_reg_rtx[i] != NULL && REG_POINTER (regno_reg_rtx[i]))
	      fprintf (file, "; pointer");
	    fprintf (file, ".\n");
	  }
    }

  fprintf (file, "\n%d basic blocks, %d edges.\n", n_basic_blocks, n_edges);
  FOR_EACH_BB (bb)
    {
      dump_bb_info (bb, true, true, flags, "", file);
      check_bb_profile (bb, file);
    }

  putc ('\n', file);
}

void
debug_flow_info (void)
{
  dump_flow_info (stderr, TDF_DETAILS);
}

void
dump_edge_info (FILE *file, edge e, int do_succ)
{
  basic_block side = (do_succ ? e->dest : e->src);

  if (side == ENTRY_BLOCK_PTR)
    fputs (" ENTRY", file);
  else if (side == EXIT_BLOCK_PTR)
    fputs (" EXIT", file);
  else
    fprintf (file, " %d", side->index);

  if (e->probability)
    fprintf (file, " [%.1f%%] ", e->probability * 100.0 / REG_BR_PROB_BASE);

  if (e->count)
    {
      fprintf (file, " count:");
      fprintf (file, HOST_WIDEST_INT_PRINT_DEC, e->count);
    }

  if (e->flags)
    {
      static const char * const bitnames[] = {
	"fallthru", "ab", "abcall", "eh", "fake", "dfs_back",
	"can_fallthru", "irreducible", "sibcall", "loop_exit",
	"true", "false", "exec"
      };
      int comma = 0;
      int i, flags = e->flags;

      fputs (" (", file);
      for (i = 0; flags; i++)
	if (flags & (1 << i))
	  {
	    flags &= ~(1 << i);

	    if (comma)
	      fputc (',', file);
	    if (i < (int) ARRAY_SIZE (bitnames))
	      fputs (bitnames[i], file);
	    else
	      fprintf (file, "%d", i);
	    comma = 1;
	  }

      fputc (')', file);
    }
}

/* Simple routines to easily allocate AUX fields of basic blocks.  */

static struct obstack block_aux_obstack;
static void *first_block_aux_obj = 0;
static struct obstack edge_aux_obstack;
static void *first_edge_aux_obj = 0;

/* Allocate a memory block of SIZE as BB->aux.  The obstack must
   be first initialized by alloc_aux_for_blocks.  */

inline void
alloc_aux_for_block (basic_block bb, int size)
{
  /* Verify that aux field is clear.  */
  gcc_assert (!bb->aux && first_block_aux_obj);
  bb->aux = obstack_alloc (&block_aux_obstack, size);
  memset (bb->aux, 0, size);
}

/* Initialize the block_aux_obstack and if SIZE is nonzero, call
   alloc_aux_for_block for each basic block.  */

void
alloc_aux_for_blocks (int size)
{
  static int initialized;

  if (!initialized)
    {
      gcc_obstack_init (&block_aux_obstack);
      initialized = 1;
    }
  else
    /* Check whether AUX data are still allocated.  */
    gcc_assert (!first_block_aux_obj);

  first_block_aux_obj = obstack_alloc (&block_aux_obstack, 0);
  if (size)
    {
      basic_block bb;

      FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR, NULL, next_bb)
	alloc_aux_for_block (bb, size);
    }
}

/* Clear AUX pointers of all blocks.  */

void
clear_aux_for_blocks (void)
{
  basic_block bb;

  FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR, NULL, next_bb)
    bb->aux = NULL;
}

/* Free data allocated in block_aux_obstack and clear AUX pointers
   of all blocks.  */

void
free_aux_for_blocks (void)
{
  gcc_assert (first_block_aux_obj);
  obstack_free (&block_aux_obstack, first_block_aux_obj);
  first_block_aux_obj = NULL;

  clear_aux_for_blocks ();
}

/* Allocate a memory edge of SIZE as BB->aux.  The obstack must
   be first initialized by alloc_aux_for_edges.  */

inline void
alloc_aux_for_edge (edge e, int size)
{
  /* Verify that aux field is clear.  */
  gcc_assert (!e->aux && first_edge_aux_obj);
  e->aux = obstack_alloc (&edge_aux_obstack, size);
  memset (e->aux, 0, size);
}

/* Initialize the edge_aux_obstack and if SIZE is nonzero, call
   alloc_aux_for_edge for each basic edge.  */

void
alloc_aux_for_edges (int size)
{
  static int initialized;

  if (!initialized)
    {
      gcc_obstack_init (&edge_aux_obstack);
      initialized = 1;
    }
  else
    /* Check whether AUX data are still allocated.  */
    gcc_assert (!first_edge_aux_obj);

  first_edge_aux_obj = obstack_alloc (&edge_aux_obstack, 0);
  if (size)
    {
      basic_block bb;

      FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR, EXIT_BLOCK_PTR, next_bb)
	{
	  edge e;
	  edge_iterator ei;

	  FOR_EACH_EDGE (e, ei, bb->succs)
	    alloc_aux_for_edge (e, size);
	}
    }
}

/* Clear AUX pointers of all edges.  */

void
clear_aux_for_edges (void)
{
  basic_block bb;
  edge e;

  FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR, EXIT_BLOCK_PTR, next_bb)
    {
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, bb->succs)
	e->aux = NULL;
    }
}

/* Free data allocated in edge_aux_obstack and clear AUX pointers
   of all edges.  */

void
free_aux_for_edges (void)
{
  gcc_assert (first_edge_aux_obj);
  obstack_free (&edge_aux_obstack, first_edge_aux_obj);
  first_edge_aux_obj = NULL;

  clear_aux_for_edges ();
}

void
debug_bb (basic_block bb)
{
  dump_bb (bb, stderr, 0);
}

basic_block
debug_bb_n (int n)
{
  basic_block bb = BASIC_BLOCK (n);
  dump_bb (bb, stderr, 0);
  return bb;
}

/* Dumps cfg related information about basic block BB to FILE.  */

static void
dump_cfg_bb_info (FILE *file, basic_block bb)
{
  unsigned i;
  edge_iterator ei;
  bool first = true;
  static const char * const bb_bitnames[] =
    {
      "dirty", "new", "reachable", "visited", "irreducible_loop", "superblock"
    };
  const unsigned n_bitnames = sizeof (bb_bitnames) / sizeof (char *);
  edge e;

  fprintf (file, "Basic block %d", bb->index);
  for (i = 0; i < n_bitnames; i++)
    if (bb->flags & (1 << i))
      {
	if (first)
	  fprintf (file, " (");
	else
	  fprintf (file, ", ");
	first = false;
	fprintf (file, bb_bitnames[i]);
      }
  if (!first)
    fprintf (file, ")");
  fprintf (file, "\n");

  fprintf (file, "Predecessors: ");
  FOR_EACH_EDGE (e, ei, bb->preds)
    dump_edge_info (file, e, 0);

  fprintf (file, "\nSuccessors: ");
  FOR_EACH_EDGE (e, ei, bb->succs)
    dump_edge_info (file, e, 1);
  fprintf (file, "\n\n");
}

/* Dumps a brief description of cfg to FILE.  */

void
brief_dump_cfg (FILE *file)
{
  basic_block bb;

  FOR_EACH_BB (bb)
    {
      dump_cfg_bb_info (file, bb);
    }
}

/* An edge originally destinating BB of FREQUENCY and COUNT has been proved to
   leave the block by TAKEN_EDGE.  Update profile of BB such that edge E can be
   redirected to destination of TAKEN_EDGE.

   This function may leave the profile inconsistent in the case TAKEN_EDGE
   frequency or count is believed to be lower than FREQUENCY or COUNT
   respectively.  */
void
update_bb_profile_for_threading (basic_block bb, int edge_frequency,
				 gcov_type count, edge taken_edge)
{
  edge c;
  int prob;
  edge_iterator ei;

  bb->count -= count;
  if (bb->count < 0)
    {
      if (dump_file)
	fprintf (dump_file, "bb %i count became negative after threading",
		 bb->index);
      bb->count = 0;
    }

  /* Compute the probability of TAKEN_EDGE being reached via threaded edge.
     Watch for overflows.  */
  if (bb->frequency)
    prob = edge_frequency * REG_BR_PROB_BASE / bb->frequency;
  else
    prob = 0;
  if (prob > taken_edge->probability)
    {
      if (dump_file)
	fprintf (dump_file, "Jump threading proved probability of edge "
		 "%i->%i too small (it is %i, should be %i).\n",
		 taken_edge->src->index, taken_edge->dest->index,
		 taken_edge->probability, prob);
      prob = taken_edge->probability;
    }

  /* Now rescale the probabilities.  */
  taken_edge->probability -= prob;
  prob = REG_BR_PROB_BASE - prob;
  bb->frequency -= edge_frequency;
  if (bb->frequency < 0)
    bb->frequency = 0;
  if (prob <= 0)
    {
      if (dump_file)
	fprintf (dump_file, "Edge frequencies of bb %i has been reset, "
		 "frequency of block should end up being 0, it is %i\n",
		 bb->index, bb->frequency);
      EDGE_SUCC (bb, 0)->probability = REG_BR_PROB_BASE;
      ei = ei_start (bb->succs);
      ei_next (&ei);
      for (; (c = ei_safe_edge (ei)); ei_next (&ei))
	c->probability = 0;
    }
  else if (prob != REG_BR_PROB_BASE)
    {
      int scale = RDIV (65536 * REG_BR_PROB_BASE, prob);

      FOR_EACH_EDGE (c, ei, bb->succs)
	{
	  c->probability = RDIV (c->probability * scale, 65536);
	  if (c->probability > REG_BR_PROB_BASE)
	    c->probability = REG_BR_PROB_BASE;
	}
    }

  gcc_assert (bb == taken_edge->src);
  taken_edge->count -= count;
  if (taken_edge->count < 0)
    {
      if (dump_file)
	fprintf (dump_file, "edge %i->%i count became negative after threading",
		 taken_edge->src->index, taken_edge->dest->index);
      taken_edge->count = 0;
    }
}

/* Multiply all frequencies of basic blocks in array BBS of length NBBS
   by NUM/DEN, in int arithmetic.  May lose some accuracy.  */
void
scale_bbs_frequencies_int (basic_block *bbs, int nbbs, int num, int den)
{
  int i;
  edge e;
  if (num < 0)
    num = 0;
  if (num > den)
    return;
  /* Assume that the users are producing the fraction from frequencies
     that never grow far enough to risk arithmetic overflow.  */
  gcc_assert (num < 65536);
  for (i = 0; i < nbbs; i++)
    {
      edge_iterator ei;
      bbs[i]->frequency = RDIV (bbs[i]->frequency * num, den);
      bbs[i]->count = RDIV (bbs[i]->count * num, den);
      FOR_EACH_EDGE (e, ei, bbs[i]->succs)
	e->count = RDIV (e->count * num, den);
    }
}

/* numbers smaller than this value are safe to multiply without getting
   64bit overflow.  */
#define MAX_SAFE_MULTIPLIER (1 << (sizeof (HOST_WIDEST_INT) * 4 - 1))

/* Multiply all frequencies of basic blocks in array BBS of length NBBS
   by NUM/DEN, in gcov_type arithmetic.  More accurate than previous
   function but considerably slower.  */
void
scale_bbs_frequencies_gcov_type (basic_block *bbs, int nbbs, gcov_type num,
				 gcov_type den)
{
  int i;
  edge e;
  gcov_type fraction = RDIV (num * 65536, den);

  gcc_assert (fraction >= 0);

  if (num < MAX_SAFE_MULTIPLIER)
    for (i = 0; i < nbbs; i++)
      {
	edge_iterator ei;
	bbs[i]->frequency = RDIV (bbs[i]->frequency * num, den);
	if (bbs[i]->count <= MAX_SAFE_MULTIPLIER)
	  bbs[i]->count = RDIV (bbs[i]->count * num, den);
	else
	  bbs[i]->count = RDIV (bbs[i]->count * fraction, 65536);
	FOR_EACH_EDGE (e, ei, bbs[i]->succs)
	  if (bbs[i]->count <= MAX_SAFE_MULTIPLIER)
	    e->count = RDIV (e->count * num, den);
	  else
	    e->count = RDIV (e->count * fraction, 65536);
      }
   else
    for (i = 0; i < nbbs; i++)
      {
	edge_iterator ei;
	if (sizeof (gcov_type) > sizeof (int))
	  bbs[i]->frequency = RDIV (bbs[i]->frequency * num, den);
	else
	  bbs[i]->frequency = RDIV (bbs[i]->frequency * fraction, 65536);
	bbs[i]->count = RDIV (bbs[i]->count * fraction, 65536);
	FOR_EACH_EDGE (e, ei, bbs[i]->succs)
	  e->count = RDIV (e->count * fraction, 65536);
      }
}

/* Data structures used to maintain mapping between basic blocks and
   copies.  */
static htab_t bb_original;
static htab_t bb_copy;
static alloc_pool original_copy_bb_pool;

struct htab_bb_copy_original_entry
{
  /* Block we are attaching info to.  */
  int index1;
  /* Index of original or copy (depending on the hashtable) */
  int index2;
};

static hashval_t
bb_copy_original_hash (const void *p)
{
  struct htab_bb_copy_original_entry *data
    = ((struct htab_bb_copy_original_entry *)p);

  return data->index1;
}
static int
bb_copy_original_eq (const void *p, const void *q)
{
  struct htab_bb_copy_original_entry *data
    = ((struct htab_bb_copy_original_entry *)p);
  struct htab_bb_copy_original_entry *data2
    = ((struct htab_bb_copy_original_entry *)q);

  return data->index1 == data2->index1;
}

/* Initialize the data structures to maintain mapping between blocks
   and its copies.  */
void
initialize_original_copy_tables (void)
{
  gcc_assert (!original_copy_bb_pool);
  original_copy_bb_pool
    = create_alloc_pool ("original_copy",
			 sizeof (struct htab_bb_copy_original_entry), 10);
  bb_original = htab_create (10, bb_copy_original_hash,
			     bb_copy_original_eq, NULL);
  bb_copy = htab_create (10, bb_copy_original_hash, bb_copy_original_eq, NULL);
}

/* Free the data structures to maintain mapping between blocks and
   its copies.  */
void
free_original_copy_tables (void)
{
  gcc_assert (original_copy_bb_pool);
  htab_delete (bb_copy);
  htab_delete (bb_original);
  free_alloc_pool (original_copy_bb_pool);
  bb_copy = NULL;
  bb_original = NULL;
  original_copy_bb_pool = NULL;
}

/* Set original for basic block.  Do nothing when data structures are not
   initialized so passes not needing this don't need to care.  */
void
set_bb_original (basic_block bb, basic_block original)
{
  if (original_copy_bb_pool)
    {
      struct htab_bb_copy_original_entry **slot;
      struct htab_bb_copy_original_entry key;

      key.index1 = bb->index;
      slot =
	(struct htab_bb_copy_original_entry **) htab_find_slot (bb_original,
							       &key, INSERT);
      if (*slot)
	(*slot)->index2 = original->index;
      else
	{
	  *slot = pool_alloc (original_copy_bb_pool);
	  (*slot)->index1 = bb->index;
	  (*slot)->index2 = original->index;
	}
    }
}

/* Get the original basic block.  */
basic_block
get_bb_original (basic_block bb)
{
  struct htab_bb_copy_original_entry *entry;
  struct htab_bb_copy_original_entry key;

  gcc_assert (original_copy_bb_pool);

  key.index1 = bb->index;
  entry = (struct htab_bb_copy_original_entry *) htab_find (bb_original, &key);
  if (entry)
    return BASIC_BLOCK (entry->index2);
  else
    return NULL;
}

/* Set copy for basic block.  Do nothing when data structures are not
   initialized so passes not needing this don't need to care.  */
void
set_bb_copy (basic_block bb, basic_block copy)
{
  if (original_copy_bb_pool)
    {
      struct htab_bb_copy_original_entry **slot;
      struct htab_bb_copy_original_entry key;

      key.index1 = bb->index;
      slot =
	(struct htab_bb_copy_original_entry **) htab_find_slot (bb_copy,
							       &key, INSERT);
      if (*slot)
	(*slot)->index2 = copy->index;
      else
	{
	  *slot = pool_alloc (original_copy_bb_pool);
	  (*slot)->index1 = bb->index;
	  (*slot)->index2 = copy->index;
	}
    }
}

/* Get the copy of basic block.  */
basic_block
get_bb_copy (basic_block bb)
{
  struct htab_bb_copy_original_entry *entry;
  struct htab_bb_copy_original_entry key;

  gcc_assert (original_copy_bb_pool);

  key.index1 = bb->index;
  entry = (struct htab_bb_copy_original_entry *) htab_find (bb_copy, &key);
  if (entry)
    return BASIC_BLOCK (entry->index2);
  else
    return NULL;
}
