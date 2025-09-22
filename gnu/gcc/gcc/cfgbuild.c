/* Control flow graph building code for GNU compiler.
   Copyright (C) 1987, 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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

/* find_basic_blocks divides the current function's rtl into basic
   blocks and constructs the CFG.  The blocks are recorded in the
   basic_block_info array; the CFG exists in the edge structures
   referenced by the blocks.

   find_basic_blocks also finds any unreachable loops and deletes them.

   Available functionality:
     - CFG construction
	 find_basic_blocks  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "regs.h"
#include "flags.h"
#include "output.h"
#include "function.h"
#include "except.h"
#include "toplev.h"
#include "timevar.h"

static int count_basic_blocks (rtx);
static void find_basic_blocks_1 (rtx);
static void make_edges (basic_block, basic_block, int);
static void make_label_edge (sbitmap, basic_block, rtx, int);
static void find_bb_boundaries (basic_block);
static void compute_outgoing_frequencies (basic_block);

/* Return true if insn is something that should be contained inside basic
   block.  */

bool
inside_basic_block_p (rtx insn)
{
  switch (GET_CODE (insn))
    {
    case CODE_LABEL:
      /* Avoid creating of basic block for jumptables.  */
      return (NEXT_INSN (insn) == 0
	      || !JUMP_P (NEXT_INSN (insn))
	      || (GET_CODE (PATTERN (NEXT_INSN (insn))) != ADDR_VEC
		  && GET_CODE (PATTERN (NEXT_INSN (insn))) != ADDR_DIFF_VEC));

    case JUMP_INSN:
      return (GET_CODE (PATTERN (insn)) != ADDR_VEC
	      && GET_CODE (PATTERN (insn)) != ADDR_DIFF_VEC);

    case CALL_INSN:
    case INSN:
      return true;

    case BARRIER:
    case NOTE:
      return false;

    default:
      gcc_unreachable ();
    }
}

/* Return true if INSN may cause control flow transfer, so it should be last in
   the basic block.  */

bool
control_flow_insn_p (rtx insn)
{
  rtx note;

  switch (GET_CODE (insn))
    {
    case NOTE:
    case CODE_LABEL:
      return false;

    case JUMP_INSN:
      /* Jump insn always causes control transfer except for tablejumps.  */
      return (GET_CODE (PATTERN (insn)) != ADDR_VEC
	      && GET_CODE (PATTERN (insn)) != ADDR_DIFF_VEC);

    case CALL_INSN:
      /* Noreturn and sibling call instructions terminate the basic blocks
	 (but only if they happen unconditionally).  */
      if ((SIBLING_CALL_P (insn)
	   || find_reg_note (insn, REG_NORETURN, 0))
	  && GET_CODE (PATTERN (insn)) != COND_EXEC)
	return true;
      /* Call insn may return to the nonlocal goto handler.  */
      return ((nonlocal_goto_handler_labels
	       && (0 == (note = find_reg_note (insn, REG_EH_REGION,
					       NULL_RTX))
		   || INTVAL (XEXP (note, 0)) >= 0))
	      /* Or may trap.  */
	      || can_throw_internal (insn));

    case INSN:
      /* Treat trap instructions like noreturn calls (same provision).  */
      if (GET_CODE (PATTERN (insn)) == TRAP_IF
	  && XEXP (PATTERN (insn), 0) == const1_rtx)
	return true;

      return (flag_non_call_exceptions && can_throw_internal (insn));

    case BARRIER:
      /* It is nonsense to reach barrier when looking for the
	 end of basic block, but before dead code is eliminated
	 this may happen.  */
      return false;

    default:
      gcc_unreachable ();
    }
}

/* Count the basic blocks of the function.  */

static int
count_basic_blocks (rtx f)
{
  int count = NUM_FIXED_BLOCKS;
  bool saw_insn = false;
  rtx insn;

  for (insn = f; insn; insn = NEXT_INSN (insn))
    {
      /* Code labels and barriers causes current basic block to be
	 terminated at previous real insn.  */
      if ((LABEL_P (insn) || BARRIER_P (insn))
	  && saw_insn)
	count++, saw_insn = false;

      /* Start basic block if needed.  */
      if (!saw_insn && inside_basic_block_p (insn))
	saw_insn = true;

      /* Control flow insn causes current basic block to be terminated.  */
      if (saw_insn && control_flow_insn_p (insn))
	count++, saw_insn = false;
    }

  if (saw_insn)
    count++;

  /* The rest of the compiler works a bit smoother when we don't have to
     check for the edge case of do-nothing functions with no basic blocks.  */
  if (count == NUM_FIXED_BLOCKS)
    {
      emit_insn (gen_rtx_USE (VOIDmode, const0_rtx));
      count = NUM_FIXED_BLOCKS + 1;
    }

  return count;
}

/* Create an edge between two basic blocks.  FLAGS are auxiliary information
   about the edge that is accumulated between calls.  */

/* Create an edge from a basic block to a label.  */

static void
make_label_edge (sbitmap edge_cache, basic_block src, rtx label, int flags)
{
  gcc_assert (LABEL_P (label));

  /* If the label was never emitted, this insn is junk, but avoid a
     crash trying to refer to BLOCK_FOR_INSN (label).  This can happen
     as a result of a syntax error and a diagnostic has already been
     printed.  */

  if (INSN_UID (label) == 0)
    return;

  cached_make_edge (edge_cache, src, BLOCK_FOR_INSN (label), flags);
}

/* Create the edges generated by INSN in REGION.  */

void
rtl_make_eh_edge (sbitmap edge_cache, basic_block src, rtx insn)
{
  int is_call = CALL_P (insn) ? EDGE_ABNORMAL_CALL : 0;
  rtx handlers, i;

  handlers = reachable_handlers (insn);

  for (i = handlers; i; i = XEXP (i, 1))
    make_label_edge (edge_cache, src, XEXP (i, 0),
		     EDGE_ABNORMAL | EDGE_EH | is_call);

  free_INSN_LIST_list (&handlers);
}

/* States of basic block as seen by find_many_sub_basic_blocks.  */
enum state {
  /* Basic blocks created via split_block belong to this state.
     make_edges will examine these basic blocks to see if we need to
     create edges going out of them.  */
  BLOCK_NEW = 0,

  /* Basic blocks that do not need examining belong to this state.
     These blocks will be left intact.  In particular, make_edges will
     not create edges going out of these basic blocks.  */
  BLOCK_ORIGINAL,

  /* Basic blocks that may need splitting (due to a label appearing in
     the middle, etc) belong to this state.  After splitting them,
     make_edges will create edges going out of them as needed.  */
  BLOCK_TO_SPLIT
};

#define STATE(BB) (enum state) ((size_t) (BB)->aux)
#define SET_STATE(BB, STATE) ((BB)->aux = (void *) (size_t) (STATE))

/* Used internally by purge_dead_tablejump_edges, ORed into state.  */
#define BLOCK_USED_BY_TABLEJUMP		32
#define FULL_STATE(BB) ((size_t) (BB)->aux)

/* Identify the edges going out of basic blocks between MIN and MAX,
   inclusive, that have their states set to BLOCK_NEW or
   BLOCK_TO_SPLIT.

   UPDATE_P should be nonzero if we are updating CFG and zero if we
   are building CFG from scratch.  */

static void
make_edges (basic_block min, basic_block max, int update_p)
{
  basic_block bb;
  sbitmap edge_cache = NULL;

  /* Heavy use of computed goto in machine-generated code can lead to
     nearly fully-connected CFGs.  In that case we spend a significant
     amount of time searching the edge lists for duplicates.  */
  if (forced_labels || cfun->max_jumptable_ents > 100)
    edge_cache = sbitmap_alloc (last_basic_block);

  /* By nature of the way these get numbered, ENTRY_BLOCK_PTR->next_bb block
     is always the entry.  */
  if (min == ENTRY_BLOCK_PTR->next_bb)
    make_edge (ENTRY_BLOCK_PTR, min, EDGE_FALLTHRU);

  FOR_BB_BETWEEN (bb, min, max->next_bb, next_bb)
    {
      rtx insn, x;
      enum rtx_code code;
      edge e;
      edge_iterator ei;

      if (STATE (bb) == BLOCK_ORIGINAL)
	continue;

      /* If we have an edge cache, cache edges going out of BB.  */
      if (edge_cache)
	{
	  sbitmap_zero (edge_cache);
	  if (update_p)
	    {
	      FOR_EACH_EDGE (e, ei, bb->succs)
		if (e->dest != EXIT_BLOCK_PTR)
		  SET_BIT (edge_cache, e->dest->index);
	    }
	}

      if (LABEL_P (BB_HEAD (bb))
	  && LABEL_ALT_ENTRY_P (BB_HEAD (bb)))
	cached_make_edge (NULL, ENTRY_BLOCK_PTR, bb, 0);

      /* Examine the last instruction of the block, and discover the
	 ways we can leave the block.  */

      insn = BB_END (bb);
      code = GET_CODE (insn);

      /* A branch.  */
      if (code == JUMP_INSN)
	{
	  rtx tmp;

	  /* Recognize exception handling placeholders.  */
	  if (GET_CODE (PATTERN (insn)) == RESX)
	    rtl_make_eh_edge (edge_cache, bb, insn);

	  /* Recognize a non-local goto as a branch outside the
	     current function.  */
	  else if (find_reg_note (insn, REG_NON_LOCAL_GOTO, NULL_RTX))
	    ;

	  /* Recognize a tablejump and do the right thing.  */
	  else if (tablejump_p (insn, NULL, &tmp))
	    {
	      rtvec vec;
	      int j;

	      if (GET_CODE (PATTERN (tmp)) == ADDR_VEC)
		vec = XVEC (PATTERN (tmp), 0);
	      else
		vec = XVEC (PATTERN (tmp), 1);

	      for (j = GET_NUM_ELEM (vec) - 1; j >= 0; --j)
		make_label_edge (edge_cache, bb,
				 XEXP (RTVEC_ELT (vec, j), 0), 0);

	      /* Some targets (eg, ARM) emit a conditional jump that also
		 contains the out-of-range target.  Scan for these and
		 add an edge if necessary.  */
	      if ((tmp = single_set (insn)) != NULL
		  && SET_DEST (tmp) == pc_rtx
		  && GET_CODE (SET_SRC (tmp)) == IF_THEN_ELSE
		  && GET_CODE (XEXP (SET_SRC (tmp), 2)) == LABEL_REF)
		make_label_edge (edge_cache, bb,
				 XEXP (XEXP (SET_SRC (tmp), 2), 0), 0);
	    }

	  /* If this is a computed jump, then mark it as reaching
	     everything on the forced_labels list.  */
	  else if (computed_jump_p (insn))
	    {
	      for (x = forced_labels; x; x = XEXP (x, 1))
		make_label_edge (edge_cache, bb, XEXP (x, 0), EDGE_ABNORMAL);
	    }

	  /* Returns create an exit out.  */
	  else if (returnjump_p (insn))
	    cached_make_edge (edge_cache, bb, EXIT_BLOCK_PTR, 0);

	  /* Otherwise, we have a plain conditional or unconditional jump.  */
	  else
	    {
	      gcc_assert (JUMP_LABEL (insn));
	      make_label_edge (edge_cache, bb, JUMP_LABEL (insn), 0);
	    }
	}

      /* If this is a sibling call insn, then this is in effect a combined call
	 and return, and so we need an edge to the exit block.  No need to
	 worry about EH edges, since we wouldn't have created the sibling call
	 in the first place.  */
      if (code == CALL_INSN && SIBLING_CALL_P (insn))
	cached_make_edge (edge_cache, bb, EXIT_BLOCK_PTR,
			  EDGE_SIBCALL | EDGE_ABNORMAL);

      /* If this is a CALL_INSN, then mark it as reaching the active EH
	 handler for this CALL_INSN.  If we're handling non-call
	 exceptions then any insn can reach any of the active handlers.
	 Also mark the CALL_INSN as reaching any nonlocal goto handler.  */
      else if (code == CALL_INSN || flag_non_call_exceptions)
	{
	  /* Add any appropriate EH edges.  */
	  rtl_make_eh_edge (edge_cache, bb, insn);

	  if (code == CALL_INSN && nonlocal_goto_handler_labels)
	    {
	      /* ??? This could be made smarter: in some cases it's possible
		 to tell that certain calls will not do a nonlocal goto.
		 For example, if the nested functions that do the nonlocal
		 gotos do not have their addresses taken, then only calls to
		 those functions or to other nested functions that use them
		 could possibly do nonlocal gotos.  */

	      /* We do know that a REG_EH_REGION note with a value less
		 than 0 is guaranteed not to perform a non-local goto.  */
	      rtx note = find_reg_note (insn, REG_EH_REGION, NULL_RTX);

	      if (!note || INTVAL (XEXP (note, 0)) >=  0)
		for (x = nonlocal_goto_handler_labels; x; x = XEXP (x, 1))
		  make_label_edge (edge_cache, bb, XEXP (x, 0),
				   EDGE_ABNORMAL | EDGE_ABNORMAL_CALL);
	    }
	}

      /* Find out if we can drop through to the next block.  */
      insn = NEXT_INSN (insn);
      e = find_edge (bb, EXIT_BLOCK_PTR);
      if (e && e->flags & EDGE_FALLTHRU)
	insn = NULL;

      while (insn
	     && NOTE_P (insn)
	     && NOTE_LINE_NUMBER (insn) != NOTE_INSN_BASIC_BLOCK)
	insn = NEXT_INSN (insn);

      if (!insn)
	cached_make_edge (edge_cache, bb, EXIT_BLOCK_PTR, EDGE_FALLTHRU);
      else if (bb->next_bb != EXIT_BLOCK_PTR)
	{
	  if (insn == BB_HEAD (bb->next_bb))
	    cached_make_edge (edge_cache, bb, bb->next_bb, EDGE_FALLTHRU);
	}
    }

  if (edge_cache)
    sbitmap_vector_free (edge_cache);
}

/* Find all basic blocks of the function whose first insn is F.

   Collect and return a list of labels whose addresses are taken.  This
   will be used in make_edges for use with computed gotos.  */

static void
find_basic_blocks_1 (rtx f)
{
  rtx insn, next;
  rtx bb_note = NULL_RTX;
  rtx head = NULL_RTX;
  rtx end = NULL_RTX;
  basic_block prev = ENTRY_BLOCK_PTR;

  /* We process the instructions in a slightly different way than we did
     previously.  This is so that we see a NOTE_BASIC_BLOCK after we have
     closed out the previous block, so that it gets attached at the proper
     place.  Since this form should be equivalent to the previous,
     count_basic_blocks continues to use the old form as a check.  */

  for (insn = f; insn; insn = next)
    {
      enum rtx_code code = GET_CODE (insn);

      next = NEXT_INSN (insn);

      if ((LABEL_P (insn) || BARRIER_P (insn))
	  && head)
	{
	  prev = create_basic_block_structure (head, end, bb_note, prev);
	  head = end = NULL_RTX;
	  bb_note = NULL_RTX;
	}

      if (inside_basic_block_p (insn))
	{
	  if (head == NULL_RTX)
	    head = insn;
	  end = insn;
	}

      if (head && control_flow_insn_p (insn))
	{
	  prev = create_basic_block_structure (head, end, bb_note, prev);
	  head = end = NULL_RTX;
	  bb_note = NULL_RTX;
	}

      switch (code)
	{
	case NOTE:
	  {
	    int kind = NOTE_LINE_NUMBER (insn);

	    /* Look for basic block notes with which to keep the
	       basic_block_info pointers stable.  Unthread the note now;
	       we'll put it back at the right place in create_basic_block.
	       Or not at all if we've already found a note in this block.  */
	    if (kind == NOTE_INSN_BASIC_BLOCK)
	      {
		if (bb_note == NULL_RTX)
		  bb_note = insn;
		else
		  next = delete_insn (insn);
	      }
	    break;
	  }

	case CODE_LABEL:
	case JUMP_INSN:
	case CALL_INSN:
	case INSN:
	case BARRIER:
	  break;

	default:
	  gcc_unreachable ();
	}
    }

  if (head != NULL_RTX)
    create_basic_block_structure (head, end, bb_note, prev);
  else if (bb_note)
    delete_insn (bb_note);

  gcc_assert (last_basic_block == n_basic_blocks);

  clear_aux_for_blocks ();
}


/* Find basic blocks of the current function.
   F is the first insn of the function.  */

void
find_basic_blocks (rtx f)
{
  basic_block bb;

  timevar_push (TV_CFG);

  /* Flush out existing data.  */
  if (basic_block_info != NULL)
    {
      clear_edges ();

      /* Clear bb->aux on all extant basic blocks.  We'll use this as a
	 tag for reuse during create_basic_block, just in case some pass
	 copies around basic block notes improperly.  */
      FOR_EACH_BB (bb)
	bb->aux = NULL;

      basic_block_info = NULL;
    }

  n_basic_blocks = count_basic_blocks (f);
  last_basic_block = NUM_FIXED_BLOCKS;
  ENTRY_BLOCK_PTR->next_bb = EXIT_BLOCK_PTR;
  EXIT_BLOCK_PTR->prev_bb = ENTRY_BLOCK_PTR;


  /* Size the basic block table.  The actual structures will be allocated
     by find_basic_blocks_1, since we want to keep the structure pointers
     stable across calls to find_basic_blocks.  */
  /* ??? This whole issue would be much simpler if we called find_basic_blocks
     exactly once, and thereafter we don't have a single long chain of
     instructions at all until close to the end of compilation when we
     actually lay them out.  */

  basic_block_info = VEC_alloc (basic_block, gc, n_basic_blocks);
  VEC_safe_grow (basic_block, gc, basic_block_info, n_basic_blocks);
  memset (VEC_address (basic_block, basic_block_info), 0,
	  sizeof (basic_block) * n_basic_blocks);
  SET_BASIC_BLOCK (ENTRY_BLOCK, ENTRY_BLOCK_PTR);
  SET_BASIC_BLOCK (EXIT_BLOCK, EXIT_BLOCK_PTR);

  find_basic_blocks_1 (f);

  profile_status = PROFILE_ABSENT;

  /* Tell make_edges to examine every block for out-going edges.  */
  FOR_EACH_BB (bb)
    SET_STATE (bb, BLOCK_NEW);

  /* Discover the edges of our cfg.  */
  make_edges (ENTRY_BLOCK_PTR->next_bb, EXIT_BLOCK_PTR->prev_bb, 0);

  /* Do very simple cleanup now, for the benefit of code that runs between
     here and cleanup_cfg, e.g. thread_prologue_and_epilogue_insns.  */
  tidy_fallthru_edges ();

#ifdef ENABLE_CHECKING
  verify_flow_info ();
#endif
  timevar_pop (TV_CFG);
}

static void
mark_tablejump_edge (rtx label)
{
  basic_block bb;

  gcc_assert (LABEL_P (label));
  /* See comment in make_label_edge.  */
  if (INSN_UID (label) == 0)
    return;
  bb = BLOCK_FOR_INSN (label);
  SET_STATE (bb, FULL_STATE (bb) | BLOCK_USED_BY_TABLEJUMP);
}

static void
purge_dead_tablejump_edges (basic_block bb, rtx table)
{
  rtx insn = BB_END (bb), tmp;
  rtvec vec;
  int j;
  edge_iterator ei;
  edge e;

  if (GET_CODE (PATTERN (table)) == ADDR_VEC)
    vec = XVEC (PATTERN (table), 0);
  else
    vec = XVEC (PATTERN (table), 1);

  for (j = GET_NUM_ELEM (vec) - 1; j >= 0; --j)
    mark_tablejump_edge (XEXP (RTVEC_ELT (vec, j), 0));

  /* Some targets (eg, ARM) emit a conditional jump that also
     contains the out-of-range target.  Scan for these and
     add an edge if necessary.  */
  if ((tmp = single_set (insn)) != NULL
       && SET_DEST (tmp) == pc_rtx
       && GET_CODE (SET_SRC (tmp)) == IF_THEN_ELSE
       && GET_CODE (XEXP (SET_SRC (tmp), 2)) == LABEL_REF)
    mark_tablejump_edge (XEXP (XEXP (SET_SRC (tmp), 2), 0));

  for (ei = ei_start (bb->succs); (e = ei_safe_edge (ei)); )
    {
      if (FULL_STATE (e->dest) & BLOCK_USED_BY_TABLEJUMP)
	SET_STATE (e->dest, FULL_STATE (e->dest)
			    & ~(size_t) BLOCK_USED_BY_TABLEJUMP);
      else if (!(e->flags & (EDGE_ABNORMAL | EDGE_EH)))
	{
	  remove_edge (e);
	  continue;
	}
      ei_next (&ei);
    }
}

/* Scan basic block BB for possible BB boundaries inside the block
   and create new basic blocks in the progress.  */

static void
find_bb_boundaries (basic_block bb)
{
  basic_block orig_bb = bb;
  rtx insn = BB_HEAD (bb);
  rtx end = BB_END (bb);
  rtx table;
  rtx flow_transfer_insn = NULL_RTX;
  edge fallthru = NULL;

  if (insn == BB_END (bb))
    return;

  if (LABEL_P (insn))
    insn = NEXT_INSN (insn);

  /* Scan insn chain and try to find new basic block boundaries.  */
  while (1)
    {
      enum rtx_code code = GET_CODE (insn);

      /* On code label, split current basic block.  */
      if (code == CODE_LABEL)
	{
	  fallthru = split_block (bb, PREV_INSN (insn));
	  if (flow_transfer_insn)
	    BB_END (bb) = flow_transfer_insn;

	  bb = fallthru->dest;
	  remove_edge (fallthru);
	  flow_transfer_insn = NULL_RTX;
	  if (LABEL_ALT_ENTRY_P (insn))
	    make_edge (ENTRY_BLOCK_PTR, bb, 0);
	}

      /* In case we've previously seen an insn that effects a control
	 flow transfer, split the block.  */
      if (flow_transfer_insn && inside_basic_block_p (insn))
	{
	  fallthru = split_block (bb, PREV_INSN (insn));
	  BB_END (bb) = flow_transfer_insn;
	  bb = fallthru->dest;
	  remove_edge (fallthru);
	  flow_transfer_insn = NULL_RTX;
	}

      if (control_flow_insn_p (insn))
	flow_transfer_insn = insn;
      if (insn == end)
	break;
      insn = NEXT_INSN (insn);
    }

  /* In case expander replaced normal insn by sequence terminating by
     return and barrier, or possibly other sequence not behaving like
     ordinary jump, we need to take care and move basic block boundary.  */
  if (flow_transfer_insn)
    BB_END (bb) = flow_transfer_insn;

  /* We've possibly replaced the conditional jump by conditional jump
     followed by cleanup at fallthru edge, so the outgoing edges may
     be dead.  */
  purge_dead_edges (bb);

  /* purge_dead_edges doesn't handle tablejump's, but if we have split the
     basic block, we might need to kill some edges.  */
  if (bb != orig_bb && tablejump_p (BB_END (bb), NULL, &table))
    purge_dead_tablejump_edges (bb, table);
}

/*  Assume that frequency of basic block B is known.  Compute frequencies
    and probabilities of outgoing edges.  */

static void
compute_outgoing_frequencies (basic_block b)
{
  edge e, f;
  edge_iterator ei;

  if (EDGE_COUNT (b->succs) == 2)
    {
      rtx note = find_reg_note (BB_END (b), REG_BR_PROB, NULL);
      int probability;

      if (note)
	{
	  probability = INTVAL (XEXP (note, 0));
	  e = BRANCH_EDGE (b);
	  e->probability = probability;
	  e->count = ((b->count * probability + REG_BR_PROB_BASE / 2)
		      / REG_BR_PROB_BASE);
	  f = FALLTHRU_EDGE (b);
	  f->probability = REG_BR_PROB_BASE - probability;
	  f->count = b->count - e->count;
	  return;
	}
    }

  if (single_succ_p (b))
    {
      e = single_succ_edge (b);
      e->probability = REG_BR_PROB_BASE;
      e->count = b->count;
      return;
    }
  guess_outgoing_edge_probabilities (b);
  if (b->count)
    FOR_EACH_EDGE (e, ei, b->succs)
      e->count = ((b->count * e->probability + REG_BR_PROB_BASE / 2)
		  / REG_BR_PROB_BASE);
}

/* Assume that some pass has inserted labels or control flow
   instructions within a basic block.  Split basic blocks as needed
   and create edges.  */

void
find_many_sub_basic_blocks (sbitmap blocks)
{
  basic_block bb, min, max;

  FOR_EACH_BB (bb)
    SET_STATE (bb,
	       TEST_BIT (blocks, bb->index) ? BLOCK_TO_SPLIT : BLOCK_ORIGINAL);

  FOR_EACH_BB (bb)
    if (STATE (bb) == BLOCK_TO_SPLIT)
      find_bb_boundaries (bb);

  FOR_EACH_BB (bb)
    if (STATE (bb) != BLOCK_ORIGINAL)
      break;

  min = max = bb;
  for (; bb != EXIT_BLOCK_PTR; bb = bb->next_bb)
    if (STATE (bb) != BLOCK_ORIGINAL)
      max = bb;

  /* Now re-scan and wire in all edges.  This expect simple (conditional)
     jumps at the end of each new basic blocks.  */
  make_edges (min, max, 1);

  /* Update branch probabilities.  Expect only (un)conditional jumps
     to be created with only the forward edges.  */
  if (profile_status != PROFILE_ABSENT)
    FOR_BB_BETWEEN (bb, min, max->next_bb, next_bb)
      {
	edge e;
	edge_iterator ei;

	if (STATE (bb) == BLOCK_ORIGINAL)
	  continue;
	if (STATE (bb) == BLOCK_NEW)
	  {
	    bb->count = 0;
	    bb->frequency = 0;
	    FOR_EACH_EDGE (e, ei, bb->preds)
	      {
		bb->count += e->count;
		bb->frequency += EDGE_FREQUENCY (e);
	      }
	  }

	compute_outgoing_frequencies (bb);
      }

  FOR_EACH_BB (bb)
    SET_STATE (bb, 0);
}
