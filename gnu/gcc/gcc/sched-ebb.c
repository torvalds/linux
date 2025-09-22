/* Instruction scheduling pass.
   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004 Free Software Foundation, Inc.
   Contributed by Michael Tiemann (tiemann@cygnus.com) Enhanced by,
   and currently maintained by, Jim Wilson (wilson@cygnus.com)

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
#include "toplev.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "regs.h"
#include "function.h"
#include "flags.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "except.h"
#include "toplev.h"
#include "recog.h"
#include "cfglayout.h"
#include "params.h"
#include "sched-int.h"
#include "target.h"
#include "output.h"

/* The number of insns scheduled so far.  */
static int sched_n_insns;

/* The number of insns to be scheduled in total.  */
static int n_insns;

/* Set of blocks, that already have their dependencies calculated.  */
static bitmap_head dont_calc_deps;
/* Set of basic blocks, that are ebb heads of tails respectively.  */
static bitmap_head ebb_head, ebb_tail;

/* Last basic block in current ebb.  */
static basic_block last_bb;

/* Implementations of the sched_info functions for region scheduling.  */
static void init_ready_list (void);
static void begin_schedule_ready (rtx, rtx);
static int schedule_more_p (void);
static const char *ebb_print_insn (rtx, int);
static int rank (rtx, rtx);
static int contributes_to_priority (rtx, rtx);
static void compute_jump_reg_dependencies (rtx, regset, regset, regset);
static basic_block earliest_block_with_similiar_load (basic_block, rtx);
static void add_deps_for_risky_insns (rtx, rtx);
static basic_block schedule_ebb (rtx, rtx);

static void add_remove_insn (rtx, int);
static void add_block1 (basic_block, basic_block);
static basic_block advance_target_bb (basic_block, rtx);
static void fix_recovery_cfg (int, int, int);

#ifdef ENABLE_CHECKING
static int ebb_head_or_leaf_p (basic_block, int);
#endif

/* Return nonzero if there are more insns that should be scheduled.  */

static int
schedule_more_p (void)
{
  return sched_n_insns < n_insns;
}

/* Add all insns that are initially ready to the ready list READY.  Called
   once before scheduling a set of insns.  */

static void
init_ready_list (void)
{
  int n = 0;
  rtx prev_head = current_sched_info->prev_head;
  rtx next_tail = current_sched_info->next_tail;
  rtx insn;

  sched_n_insns = 0;

#if 0
  /* Print debugging information.  */
  if (sched_verbose >= 5)
    debug_dependencies ();
#endif

  /* Initialize ready list with all 'ready' insns in target block.
     Count number of insns in the target block being scheduled.  */
  for (insn = NEXT_INSN (prev_head); insn != next_tail; insn = NEXT_INSN (insn))
    {
      try_ready (insn);
      n++;
    }

  gcc_assert (n == n_insns);
}

/* INSN is being scheduled after LAST.  Update counters.  */
static void
begin_schedule_ready (rtx insn, rtx last)
{
  sched_n_insns++;

  if (BLOCK_FOR_INSN (insn) == last_bb
      /* INSN is a jump in the last block, ...  */
      && control_flow_insn_p (insn)
      /* that is going to be moved over some instructions.  */
      && last != PREV_INSN (insn))
    {
      edge e;
      edge_iterator ei;
      basic_block bb;

      /* An obscure special case, where we do have partially dead
	 instruction scheduled after last control flow instruction.
	 In this case we can create new basic block.  It is
	 always exactly one basic block last in the sequence.  */
      
      FOR_EACH_EDGE (e, ei, last_bb->succs)
	if (e->flags & EDGE_FALLTHRU)
	  break;

#ifdef ENABLE_CHECKING
      gcc_assert (!e || !(e->flags & EDGE_COMPLEX));	    

      gcc_assert (BLOCK_FOR_INSN (insn) == last_bb
		  && !IS_SPECULATION_CHECK_P (insn)
		  && BB_HEAD (last_bb) != insn
		  && BB_END (last_bb) == insn);

      {
	rtx x;

	x = NEXT_INSN (insn);
	if (e)
	  gcc_assert (NOTE_P (x) || LABEL_P (x));
	else
	  gcc_assert (BARRIER_P (x));
      }
#endif

      if (e)
	{
	  bb = split_edge (e);
	  gcc_assert (NOTE_INSN_BASIC_BLOCK_P (BB_END (bb)));
	}
      else
	/* Create an empty unreachable block after the INSN.  */
	bb = create_basic_block (NEXT_INSN (insn), NULL_RTX, last_bb);
      
      /* split_edge () creates BB before E->DEST.  Keep in mind, that
	 this operation extends scheduling region till the end of BB.
	 Hence, we need to shift NEXT_TAIL, so haifa-sched.c won't go out
	 of the scheduling region.  */
      current_sched_info->next_tail = NEXT_INSN (BB_END (bb));
      gcc_assert (current_sched_info->next_tail);

      add_block (bb, last_bb);
      gcc_assert (last_bb == bb);
    }
}

/* Return a string that contains the insn uid and optionally anything else
   necessary to identify this insn in an output.  It's valid to use a
   static buffer for this.  The ALIGNED parameter should cause the string
   to be formatted so that multiple output lines will line up nicely.  */

static const char *
ebb_print_insn (rtx insn, int aligned ATTRIBUTE_UNUSED)
{
  static char tmp[80];

  sprintf (tmp, "%4d", INSN_UID (insn));
  return tmp;
}

/* Compare priority of two insns.  Return a positive number if the second
   insn is to be preferred for scheduling, and a negative one if the first
   is to be preferred.  Zero if they are equally good.  */

static int
rank (rtx insn1, rtx insn2)
{
  basic_block bb1 = BLOCK_FOR_INSN (insn1);
  basic_block bb2 = BLOCK_FOR_INSN (insn2);

  if (bb1->count > bb2->count
      || bb1->frequency > bb2->frequency)
    return -1;
  if (bb1->count < bb2->count
      || bb1->frequency < bb2->frequency)
    return 1;
  return 0;
}

/* NEXT is an instruction that depends on INSN (a backward dependence);
   return nonzero if we should include this dependence in priority
   calculations.  */

static int
contributes_to_priority (rtx next ATTRIBUTE_UNUSED,
			 rtx insn ATTRIBUTE_UNUSED)
{
  return 1;
}

 /* INSN is a JUMP_INSN, COND_SET is the set of registers that are
    conditionally set before INSN.  Store the set of registers that
    must be considered as used by this jump in USED and that of
    registers that must be considered as set in SET.  */

static void
compute_jump_reg_dependencies (rtx insn, regset cond_set, regset used,
			       regset set)
{
  basic_block b = BLOCK_FOR_INSN (insn);
  edge e;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, b->succs)
    if (e->flags & EDGE_FALLTHRU)
      /* The jump may be a by-product of a branch that has been merged
	 in the main codepath after being conditionalized.  Therefore
	 it may guard the fallthrough block from using a value that has
	 conditionally overwritten that of the main codepath.  So we
	 consider that it restores the value of the main codepath.  */
      bitmap_and (set, glat_start [e->dest->index], cond_set);
    else
      bitmap_ior_into (used, glat_start [e->dest->index]);
}

/* Used in schedule_insns to initialize current_sched_info for scheduling
   regions (or single basic blocks).  */

static struct sched_info ebb_sched_info =
{
  init_ready_list,
  NULL,
  schedule_more_p,
  NULL,
  rank,
  ebb_print_insn,
  contributes_to_priority,
  compute_jump_reg_dependencies,

  NULL, NULL,
  NULL, NULL,
  0, 1, 0,

  add_remove_insn,
  begin_schedule_ready,
  add_block1,
  advance_target_bb,
  fix_recovery_cfg,
#ifdef ENABLE_CHECKING
  ebb_head_or_leaf_p,
#endif
  /* We need to DETACH_LIVE_INFO to be able to create new basic blocks.
     See begin_schedule_ready ().  */
  SCHED_EBB | USE_GLAT | DETACH_LIFE_INFO
};

/* Returns the earliest block in EBB currently being processed where a
   "similar load" 'insn2' is found, and hence LOAD_INSN can move
   speculatively into the found block.  All the following must hold:

   (1) both loads have 1 base register (PFREE_CANDIDATEs).
   (2) load_insn and load2 have a def-use dependence upon
   the same insn 'insn1'.

   From all these we can conclude that the two loads access memory
   addresses that differ at most by a constant, and hence if moving
   load_insn would cause an exception, it would have been caused by
   load2 anyhow.

   The function uses list (given by LAST_BLOCK) of already processed
   blocks in EBB.  The list is formed in `add_deps_for_risky_insns'.  */

static basic_block
earliest_block_with_similiar_load (basic_block last_block, rtx load_insn)
{
  rtx back_link;
  basic_block bb, earliest_block = NULL;

  for (back_link = LOG_LINKS (load_insn);
       back_link;
       back_link = XEXP (back_link, 1))
    {
      rtx insn1 = XEXP (back_link, 0);

      if (GET_MODE (back_link) == VOIDmode)
	{
	  /* Found a DEF-USE dependence (insn1, load_insn).  */
	  rtx fore_link;

	  for (fore_link = INSN_DEPEND (insn1);
	       fore_link;
	       fore_link = XEXP (fore_link, 1))
	    {
	      rtx insn2 = XEXP (fore_link, 0);
	      basic_block insn2_block = BLOCK_FOR_INSN (insn2);

	      if (GET_MODE (fore_link) == VOIDmode)
		{
		  if (earliest_block != NULL
		      && earliest_block->index < insn2_block->index)
		    continue;

		  /* Found a DEF-USE dependence (insn1, insn2).  */
		  if (haifa_classify_insn (insn2) != PFREE_CANDIDATE)
		    /* insn2 not guaranteed to be a 1 base reg load.  */
		    continue;

		  for (bb = last_block; bb; bb = bb->aux)
		    if (insn2_block == bb)
		      break;

		  if (!bb)
		    /* insn2 is the similar load.  */
		    earliest_block = insn2_block;
		}
	    }
	}
    }

  return earliest_block;
}

/* The following function adds dependencies between jumps and risky
   insns in given ebb.  */

static void
add_deps_for_risky_insns (rtx head, rtx tail)
{
  rtx insn, prev;
  int class;
  rtx last_jump = NULL_RTX;
  rtx next_tail = NEXT_INSN (tail);
  basic_block last_block = NULL, bb;

  for (insn = head; insn != next_tail; insn = NEXT_INSN (insn))
    if (control_flow_insn_p (insn))
      {
	bb = BLOCK_FOR_INSN (insn);
	bb->aux = last_block;
	last_block = bb;
	last_jump = insn;
      }
    else if (INSN_P (insn) && last_jump != NULL_RTX)
      {
	class = haifa_classify_insn (insn);
	prev = last_jump;
	switch (class)
	  {
	  case PFREE_CANDIDATE:
	    if (flag_schedule_speculative_load)
	      {
		bb = earliest_block_with_similiar_load (last_block, insn);
		if (bb)
		  {
		    bb = bb->aux;
		    if (!bb)
		      break;
		    prev = BB_END (bb);
		  }
	      }
	    /* Fall through.  */
	  case TRAP_RISKY:
	  case IRISKY:
	  case PRISKY_CANDIDATE:
	    /* ??? We could implement better checking PRISKY_CANDIDATEs
	       analogous to sched-rgn.c.  */
	    /* We can not change the mode of the backward
	       dependency because REG_DEP_ANTI has the lowest
	       rank.  */
	    if (! sched_insns_conditions_mutex_p (insn, prev))
	      {
		if (!(current_sched_info->flags & DO_SPECULATION))
		  {
		    enum DEPS_ADJUST_RESULT res;
		    
		    res = add_or_update_back_dep (insn, prev,
						  REG_DEP_ANTI, DEP_ANTI);

		    if (res == DEP_CREATED)
		      add_forw_dep (insn, LOG_LINKS (insn));
		    else
		      gcc_assert (res != DEP_CHANGED);
		  }
		else
		  add_or_update_back_forw_dep (insn, prev, REG_DEP_ANTI,
					       set_dep_weak (DEP_ANTI,
							     BEGIN_CONTROL,
							     MAX_DEP_WEAK));
	      }

            break;

          default:
            break;
	  }
      }
  /* Maintain the invariant that bb->aux is clear after use.  */
  while (last_block)
    {
      bb = last_block->aux;
      last_block->aux = NULL;
      last_block = bb;
    }
}

/* Schedule a single extended basic block, defined by the boundaries HEAD
   and TAIL.  */

static basic_block
schedule_ebb (rtx head, rtx tail)
{
  basic_block first_bb, target_bb;
  struct deps tmp_deps;
  
  first_bb = BLOCK_FOR_INSN (head);
  last_bb = BLOCK_FOR_INSN (tail);

  if (no_real_insns_p (head, tail))
    return BLOCK_FOR_INSN (tail);

  gcc_assert (INSN_P (head) && INSN_P (tail));

  if (!bitmap_bit_p (&dont_calc_deps, first_bb->index))
    {
      init_deps_global ();

      /* Compute LOG_LINKS.  */
      init_deps (&tmp_deps);
      sched_analyze (&tmp_deps, head, tail);
      free_deps (&tmp_deps);

      /* Compute INSN_DEPEND.  */
      compute_forward_dependences (head, tail);

      add_deps_for_risky_insns (head, tail);

      if (targetm.sched.dependencies_evaluation_hook)
        targetm.sched.dependencies_evaluation_hook (head, tail);

      finish_deps_global ();
    }
  else
    /* Only recovery blocks can have their dependencies already calculated,
       and they always are single block ebbs.  */       
    gcc_assert (first_bb == last_bb);

  /* Set priorities.  */
  current_sched_info->sched_max_insns_priority = 0;
  n_insns = set_priorities (head, tail);
  current_sched_info->sched_max_insns_priority++;

  current_sched_info->prev_head = PREV_INSN (head);
  current_sched_info->next_tail = NEXT_INSN (tail);

  if (write_symbols != NO_DEBUG)
    {
      save_line_notes (first_bb->index, head, tail);
      rm_line_notes (head, tail);
    }

  /* rm_other_notes only removes notes which are _inside_ the
     block---that is, it won't remove notes before the first real insn
     or after the last real insn of the block.  So if the first insn
     has a REG_SAVE_NOTE which would otherwise be emitted before the
     insn, it is redundant with the note before the start of the
     block, and so we have to take it out.  */
  if (INSN_P (head))
    {
      rtx note;

      for (note = REG_NOTES (head); note; note = XEXP (note, 1))
	if (REG_NOTE_KIND (note) == REG_SAVE_NOTE)
	  remove_note (head, note);
    }

  /* Remove remaining note insns from the block, save them in
     note_list.  These notes are restored at the end of
     schedule_block ().  */
  rm_other_notes (head, tail);

  unlink_bb_notes (first_bb, last_bb);

  current_sched_info->queue_must_finish_empty = 1;

  target_bb = first_bb;
  schedule_block (&target_bb, n_insns);

  /* We might pack all instructions into fewer blocks,
     so we may made some of them empty.  Can't assert (b == last_bb).  */
  
  /* Sanity check: verify that all region insns were scheduled.  */
  gcc_assert (sched_n_insns == n_insns);
  head = current_sched_info->head;
  tail = current_sched_info->tail;

  if (write_symbols != NO_DEBUG)
    restore_line_notes (head, tail);

  if (EDGE_COUNT (last_bb->preds) == 0)
    /* LAST_BB is unreachable.  */
    {
      gcc_assert (first_bb != last_bb
		  && EDGE_COUNT (last_bb->succs) == 0);
      last_bb = last_bb->prev_bb;
      delete_basic_block (last_bb->next_bb);
    }

  return last_bb;
}

/* The one entry point in this file.  */

void
schedule_ebbs (void)
{
  basic_block bb;
  int probability_cutoff;
  rtx tail;
  sbitmap large_region_blocks, blocks;
  int any_large_regions;

  if (profile_info && flag_branch_probabilities)
    probability_cutoff = PARAM_VALUE (TRACER_MIN_BRANCH_PROBABILITY_FEEDBACK);
  else
    probability_cutoff = PARAM_VALUE (TRACER_MIN_BRANCH_PROBABILITY);
  probability_cutoff = REG_BR_PROB_BASE / 100 * probability_cutoff;

  /* Taking care of this degenerate case makes the rest of
     this code simpler.  */
  if (n_basic_blocks == NUM_FIXED_BLOCKS)
    return;

  /* We need current_sched_info in init_dependency_caches, which is
     invoked via sched_init.  */
  current_sched_info = &ebb_sched_info;

  sched_init ();

  compute_bb_for_insn ();

  /* Initialize DONT_CALC_DEPS and ebb-{start, end} markers.  */
  bitmap_initialize (&dont_calc_deps, 0);
  bitmap_clear (&dont_calc_deps);
  bitmap_initialize (&ebb_head, 0);
  bitmap_clear (&ebb_head);
  bitmap_initialize (&ebb_tail, 0);
  bitmap_clear (&ebb_tail);

  /* Schedule every region in the subroutine.  */
  FOR_EACH_BB (bb)
    {
      rtx head = BB_HEAD (bb);

      for (;;)
	{
	  edge e;
	  edge_iterator ei;
	  tail = BB_END (bb);
	  if (bb->next_bb == EXIT_BLOCK_PTR
	      || LABEL_P (BB_HEAD (bb->next_bb)))
	    break;
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    if ((e->flags & EDGE_FALLTHRU) != 0)
	      break;
	  if (! e)
	    break;
	  if (e->probability <= probability_cutoff)
	    break;
	  bb = bb->next_bb;
	}

      /* Blah.  We should fix the rest of the code not to get confused by
	 a note or two.  */
      while (head != tail)
	{
	  if (NOTE_P (head))
	    head = NEXT_INSN (head);
	  else if (NOTE_P (tail))
	    tail = PREV_INSN (tail);
	  else if (LABEL_P (head))
	    head = NEXT_INSN (head);
	  else
	    break;
	}

      bitmap_set_bit (&ebb_head, BLOCK_NUM (head));
      bb = schedule_ebb (head, tail);
      bitmap_set_bit (&ebb_tail, bb->index);
    }
  bitmap_clear (&dont_calc_deps);

  gcc_assert (current_sched_info->flags & DETACH_LIFE_INFO);
  /* We can create new basic blocks during scheduling, and
     attach_life_info () will create regsets for them
     (along with attaching existing info back).  */
  attach_life_info ();

  /* Updating register live information.  */
  allocate_reg_life_data ();

  any_large_regions = 0;
  large_region_blocks = sbitmap_alloc (last_basic_block);
  sbitmap_zero (large_region_blocks);
  FOR_EACH_BB (bb)
    SET_BIT (large_region_blocks, bb->index);

  blocks = sbitmap_alloc (last_basic_block);
  sbitmap_zero (blocks);

  /* Update life information.  For regions consisting of multiple blocks
     we've possibly done interblock scheduling that affects global liveness.
     For regions consisting of single blocks we need to do only local
     liveness.  */
  FOR_EACH_BB (bb)
    {
      int bbi;
      
      bbi = bb->index;

      if (!bitmap_bit_p (&ebb_head, bbi)
	  || !bitmap_bit_p (&ebb_tail, bbi)
	  /* New blocks (e.g. recovery blocks) should be processed
	     as parts of large regions.  */
	  || !glat_start[bbi])
	any_large_regions = 1;
      else
	{
	  SET_BIT (blocks, bbi);
	  RESET_BIT (large_region_blocks, bbi);
	}
    }

  update_life_info (blocks, UPDATE_LIFE_LOCAL, 0);
  sbitmap_free (blocks);
  
  if (any_large_regions)
    {
      update_life_info (large_region_blocks, UPDATE_LIFE_GLOBAL, 0);

#ifdef ENABLE_CHECKING
      /* !!! We can't check reg_live_info here because of the fact,
	 that destination registers of COND_EXEC's may be dead
	 before scheduling (while they should be alive).  Don't know why.  */
      /*check_reg_live (true);*/
#endif
    }
  sbitmap_free (large_region_blocks);

  bitmap_clear (&ebb_head);
  bitmap_clear (&ebb_tail);

  /* Reposition the prologue and epilogue notes in case we moved the
     prologue/epilogue insns.  */
  if (reload_completed)
    reposition_prologue_and_epilogue_notes (get_insns ());

  if (write_symbols != NO_DEBUG)
    rm_redundant_line_notes ();

  sched_finish ();
}

/* INSN has been added to/removed from current ebb.  */
static void
add_remove_insn (rtx insn ATTRIBUTE_UNUSED, int remove_p)
{
  if (!remove_p)
    n_insns++;
  else
    n_insns--;
}

/* BB was added to ebb after AFTER.  */
static void
add_block1 (basic_block bb, basic_block after)
{
  /* Recovery blocks are always bounded by BARRIERS, 
     therefore, they always form single block EBB,
     therefore, we can use rec->index to identify such EBBs.  */
  if (after == EXIT_BLOCK_PTR)
    bitmap_set_bit (&dont_calc_deps, bb->index);
  else if (after == last_bb)
    last_bb = bb;
}

/* Return next block in ebb chain.  For parameter meaning please refer to
   sched-int.h: struct sched_info: advance_target_bb.  */
static basic_block
advance_target_bb (basic_block bb, rtx insn)
{
  if (insn)
    {
      if (BLOCK_FOR_INSN (insn) != bb
	  && control_flow_insn_p (insn)
	  /* We handle interblock movement of the speculation check
	     or over a speculation check in
	     haifa-sched.c: move_block_after_check ().  */
	  && !IS_SPECULATION_BRANCHY_CHECK_P (insn)
	  && !IS_SPECULATION_BRANCHY_CHECK_P (BB_END (bb)))
	{
	  /* Assert that we don't move jumps across blocks.  */
	  gcc_assert (!control_flow_insn_p (BB_END (bb))
		      && NOTE_INSN_BASIC_BLOCK_P (BB_HEAD (bb->next_bb)));
	  return bb;
	}
      else
	return 0;
    }
  else
    /* Return next non empty block.  */
    {
      do
	{
	  gcc_assert (bb != last_bb);

	  bb = bb->next_bb;
	}
      while (bb_note (bb) == BB_END (bb));

      return bb;
    }
}

/* Fix internal data after interblock movement of jump instruction.
   For parameter meaning please refer to
   sched-int.h: struct sched_info: fix_recovery_cfg.  */
static void
fix_recovery_cfg (int bbi ATTRIBUTE_UNUSED, int jump_bbi, int jump_bb_nexti)
{
  gcc_assert (last_bb->index != bbi);

  if (jump_bb_nexti == last_bb->index)
    last_bb = BASIC_BLOCK (jump_bbi);
}

#ifdef ENABLE_CHECKING
/* Return non zero, if BB is first or last (depending of LEAF_P) block in
   current ebb.  For more information please refer to
   sched-int.h: struct sched_info: region_head_or_leaf_p.  */
static int
ebb_head_or_leaf_p (basic_block bb, int leaf_p)
{
  if (!leaf_p)    
    return bitmap_bit_p (&ebb_head, bb->index);
  else
    return bitmap_bit_p (&ebb_tail, bb->index);
}
#endif /* ENABLE_CHECKING  */
