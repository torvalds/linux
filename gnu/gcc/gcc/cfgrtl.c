/* Control flow graph manipulation code for GNU compiler.
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

/* This file contains low level functions to manipulate the CFG and analyze it
   that are aware of the RTL intermediate language.

   Available functionality:
     - Basic CFG/RTL manipulation API documented in cfghooks.h
     - CFG-aware instruction chain manipulation
	 delete_insn, delete_insn_chain
     - Edge splitting and committing to edges
	 insert_insn_on_edge, commit_edge_insertions
     - CFG updating after insn simplification
	 purge_dead_edges, purge_all_dead_edges

   Functions not supposed for generic use:
     - Infrastructure to determine quickly basic block for insn
	 compute_bb_for_insn, update_bb_for_insn, set_block_for_insn,
     - Edge redirection with updating and optimizing of insn chain
	 block_label, tidy_fallthru_edge, force_nonfallthru  */

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
#include "tm_p.h"
#include "obstack.h"
#include "insn-config.h"
#include "cfglayout.h"
#include "expr.h"
#include "target.h"
#include "cfgloop.h"
#include "ggc.h"
#include "tree-pass.h"

static int can_delete_note_p (rtx);
static int can_delete_label_p (rtx);
static void commit_one_edge_insertion (edge, int);
static basic_block rtl_split_edge (edge);
static bool rtl_move_block_after (basic_block, basic_block);
static int rtl_verify_flow_info (void);
static basic_block cfg_layout_split_block (basic_block, void *);
static edge cfg_layout_redirect_edge_and_branch (edge, basic_block);
static basic_block cfg_layout_redirect_edge_and_branch_force (edge, basic_block);
static void cfg_layout_delete_block (basic_block);
static void rtl_delete_block (basic_block);
static basic_block rtl_redirect_edge_and_branch_force (edge, basic_block);
static edge rtl_redirect_edge_and_branch (edge, basic_block);
static basic_block rtl_split_block (basic_block, void *);
static void rtl_dump_bb (basic_block, FILE *, int);
static int rtl_verify_flow_info_1 (void);
static void rtl_make_forwarder_block (edge);

/* Return true if NOTE is not one of the ones that must be kept paired,
   so that we may simply delete it.  */

static int
can_delete_note_p (rtx note)
{
  return (NOTE_LINE_NUMBER (note) == NOTE_INSN_DELETED
	  || NOTE_LINE_NUMBER (note) == NOTE_INSN_BASIC_BLOCK);
}

/* True if a given label can be deleted.  */

static int
can_delete_label_p (rtx label)
{
  return (!LABEL_PRESERVE_P (label)
	  /* User declared labels must be preserved.  */
	  && LABEL_NAME (label) == 0
	  && !in_expr_list_p (forced_labels, label));
}

/* Delete INSN by patching it out.  Return the next insn.  */

rtx
delete_insn (rtx insn)
{
  rtx next = NEXT_INSN (insn);
  rtx note;
  bool really_delete = true;

  if (LABEL_P (insn))
    {
      /* Some labels can't be directly removed from the INSN chain, as they
	 might be references via variables, constant pool etc.
	 Convert them to the special NOTE_INSN_DELETED_LABEL note.  */
      if (! can_delete_label_p (insn))
	{
	  const char *name = LABEL_NAME (insn);

	  really_delete = false;
	  PUT_CODE (insn, NOTE);
	  NOTE_LINE_NUMBER (insn) = NOTE_INSN_DELETED_LABEL;
	  NOTE_DELETED_LABEL_NAME (insn) = name;
	}

      remove_node_from_expr_list (insn, &nonlocal_goto_handler_labels);
    }

  if (really_delete)
    {
      /* If this insn has already been deleted, something is very wrong.  */
      gcc_assert (!INSN_DELETED_P (insn));
      remove_insn (insn);
      INSN_DELETED_P (insn) = 1;
    }

  /* If deleting a jump, decrement the use count of the label.  Deleting
     the label itself should happen in the normal course of block merging.  */
  if (JUMP_P (insn)
      && JUMP_LABEL (insn)
      && LABEL_P (JUMP_LABEL (insn)))
    LABEL_NUSES (JUMP_LABEL (insn))--;

  /* Also if deleting an insn that references a label.  */
  else
    {
      while ((note = find_reg_note (insn, REG_LABEL, NULL_RTX)) != NULL_RTX
	     && LABEL_P (XEXP (note, 0)))
	{
	  LABEL_NUSES (XEXP (note, 0))--;
	  remove_note (insn, note);
	}
    }

  if (JUMP_P (insn)
      && (GET_CODE (PATTERN (insn)) == ADDR_VEC
	  || GET_CODE (PATTERN (insn)) == ADDR_DIFF_VEC))
    {
      rtx pat = PATTERN (insn);
      int diff_vec_p = GET_CODE (PATTERN (insn)) == ADDR_DIFF_VEC;
      int len = XVECLEN (pat, diff_vec_p);
      int i;

      for (i = 0; i < len; i++)
	{
	  rtx label = XEXP (XVECEXP (pat, diff_vec_p, i), 0);

	  /* When deleting code in bulk (e.g. removing many unreachable
	     blocks) we can delete a label that's a target of the vector
	     before deleting the vector itself.  */
	  if (!NOTE_P (label))
	    LABEL_NUSES (label)--;
	}
    }

  return next;
}

/* Like delete_insn but also purge dead edges from BB.  */
rtx
delete_insn_and_edges (rtx insn)
{
  rtx x;
  bool purge = false;

  if (INSN_P (insn)
      && BLOCK_FOR_INSN (insn)
      && BB_END (BLOCK_FOR_INSN (insn)) == insn)
    purge = true;
  x = delete_insn (insn);
  if (purge)
    purge_dead_edges (BLOCK_FOR_INSN (insn));
  return x;
}

/* Unlink a chain of insns between START and FINISH, leaving notes
   that must be paired.  */

void
delete_insn_chain (rtx start, rtx finish)
{
  rtx next;

  /* Unchain the insns one by one.  It would be quicker to delete all of these
     with a single unchaining, rather than one at a time, but we need to keep
     the NOTE's.  */
  while (1)
    {
      next = NEXT_INSN (start);
      if (NOTE_P (start) && !can_delete_note_p (start))
	;
      else
	next = delete_insn (start);

      if (start == finish)
	break;
      start = next;
    }
}

/* Like delete_insn but also purge dead edges from BB.  */
void
delete_insn_chain_and_edges (rtx first, rtx last)
{
  bool purge = false;

  if (INSN_P (last)
      && BLOCK_FOR_INSN (last)
      && BB_END (BLOCK_FOR_INSN (last)) == last)
    purge = true;
  delete_insn_chain (first, last);
  if (purge)
    purge_dead_edges (BLOCK_FOR_INSN (last));
}

/* Create a new basic block consisting of the instructions between HEAD and END
   inclusive.  This function is designed to allow fast BB construction - reuses
   the note and basic block struct in BB_NOTE, if any and do not grow
   BASIC_BLOCK chain and should be used directly only by CFG construction code.
   END can be NULL in to create new empty basic block before HEAD.  Both END
   and HEAD can be NULL to create basic block at the end of INSN chain.
   AFTER is the basic block we should be put after.  */

basic_block
create_basic_block_structure (rtx head, rtx end, rtx bb_note, basic_block after)
{
  basic_block bb;

  if (bb_note
      && (bb = NOTE_BASIC_BLOCK (bb_note)) != NULL
      && bb->aux == NULL)
    {
      /* If we found an existing note, thread it back onto the chain.  */

      rtx after;

      if (LABEL_P (head))
	after = head;
      else
	{
	  after = PREV_INSN (head);
	  head = bb_note;
	}

      if (after != bb_note && NEXT_INSN (after) != bb_note)
	reorder_insns_nobb (bb_note, bb_note, after);
    }
  else
    {
      /* Otherwise we must create a note and a basic block structure.  */

      bb = alloc_block ();

      init_rtl_bb_info (bb);
      if (!head && !end)
	head = end = bb_note
	  = emit_note_after (NOTE_INSN_BASIC_BLOCK, get_last_insn ());
      else if (LABEL_P (head) && end)
	{
	  bb_note = emit_note_after (NOTE_INSN_BASIC_BLOCK, head);
	  if (head == end)
	    end = bb_note;
	}
      else
	{
	  bb_note = emit_note_before (NOTE_INSN_BASIC_BLOCK, head);
	  head = bb_note;
	  if (!end)
	    end = head;
	}

      NOTE_BASIC_BLOCK (bb_note) = bb;
    }

  /* Always include the bb note in the block.  */
  if (NEXT_INSN (end) == bb_note)
    end = bb_note;

  BB_HEAD (bb) = head;
  BB_END (bb) = end;
  bb->index = last_basic_block++;
  bb->flags = BB_NEW | BB_RTL;
  link_block (bb, after);
  SET_BASIC_BLOCK (bb->index, bb);
  update_bb_for_insn (bb);
  BB_SET_PARTITION (bb, BB_UNPARTITIONED);

  /* Tag the block so that we know it has been used when considering
     other basic block notes.  */
  bb->aux = bb;

  return bb;
}

/* Create new basic block consisting of instructions in between HEAD and END
   and place it to the BB chain after block AFTER.  END can be NULL in to
   create new empty basic block before HEAD.  Both END and HEAD can be NULL to
   create basic block at the end of INSN chain.  */

static basic_block
rtl_create_basic_block (void *headp, void *endp, basic_block after)
{
  rtx head = headp, end = endp;
  basic_block bb;

  /* Grow the basic block array if needed.  */
  if ((size_t) last_basic_block >= VEC_length (basic_block, basic_block_info))
    {
      size_t old_size = VEC_length (basic_block, basic_block_info);
      size_t new_size = last_basic_block + (last_basic_block + 3) / 4;
      basic_block *p;
      VEC_safe_grow (basic_block, gc, basic_block_info, new_size);
      p = VEC_address (basic_block, basic_block_info);
      memset (&p[old_size], 0, sizeof (basic_block) * (new_size - old_size));
    }

  n_basic_blocks++;

  bb = create_basic_block_structure (head, end, NULL, after);
  bb->aux = NULL;
  return bb;
}

static basic_block
cfg_layout_create_basic_block (void *head, void *end, basic_block after)
{
  basic_block newbb = rtl_create_basic_block (head, end, after);

  return newbb;
}

/* Delete the insns in a (non-live) block.  We physically delete every
   non-deleted-note insn, and update the flow graph appropriately.

   Return nonzero if we deleted an exception handler.  */

/* ??? Preserving all such notes strikes me as wrong.  It would be nice
   to post-process the stream to remove empty blocks, loops, ranges, etc.  */

static void
rtl_delete_block (basic_block b)
{
  rtx insn, end;

  /* If the head of this block is a CODE_LABEL, then it might be the
     label for an exception handler which can't be reached.  We need
     to remove the label from the exception_handler_label list.  */
  insn = BB_HEAD (b);
  if (LABEL_P (insn))
    maybe_remove_eh_handler (insn);

  end = get_last_bb_insn (b);

  /* Selectively delete the entire chain.  */
  BB_HEAD (b) = NULL;
  delete_insn_chain (insn, end);
  if (b->il.rtl->global_live_at_start)
    {
      FREE_REG_SET (b->il.rtl->global_live_at_start);
      FREE_REG_SET (b->il.rtl->global_live_at_end);
      b->il.rtl->global_live_at_start = NULL;
      b->il.rtl->global_live_at_end = NULL;
    }
}

/* Records the basic block struct in BLOCK_FOR_INSN for every insn.  */

void
compute_bb_for_insn (void)
{
  basic_block bb;

  FOR_EACH_BB (bb)
    {
      rtx end = BB_END (bb);
      rtx insn;

      for (insn = BB_HEAD (bb); ; insn = NEXT_INSN (insn))
	{
	  BLOCK_FOR_INSN (insn) = bb;
	  if (insn == end)
	    break;
	}
    }
}

/* Release the basic_block_for_insn array.  */

unsigned int
free_bb_for_insn (void)
{
  rtx insn;
  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    if (!BARRIER_P (insn))
      BLOCK_FOR_INSN (insn) = NULL;
  return 0;
}

struct tree_opt_pass pass_free_cfg =
{
  NULL,                                 /* name */
  NULL,                                 /* gate */
  free_bb_for_insn,                     /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  0,                                    /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  PROP_cfg,                             /* properties_destroyed */
  0,                                    /* todo_flags_start */
  0,                                    /* todo_flags_finish */
  0                                     /* letter */
};

/* Return RTX to emit after when we want to emit code on the entry of function.  */
rtx
entry_of_function (void)
{
  return (n_basic_blocks > NUM_FIXED_BLOCKS ?
	  BB_HEAD (ENTRY_BLOCK_PTR->next_bb) : get_insns ());
}

/* Emit INSN at the entry point of the function, ensuring that it is only
   executed once per function.  */
void
emit_insn_at_entry (rtx insn)
{
  edge_iterator ei = ei_start (ENTRY_BLOCK_PTR->succs);
  edge e = ei_safe_edge (ei);
  gcc_assert (e->flags & EDGE_FALLTHRU);

  insert_insn_on_edge (insn, e);
  commit_edge_insertions ();
}

/* Update insns block within BB.  */

void
update_bb_for_insn (basic_block bb)
{
  rtx insn;

  for (insn = BB_HEAD (bb); ; insn = NEXT_INSN (insn))
    {
      if (!BARRIER_P (insn))
	set_block_for_insn (insn, bb);
      if (insn == BB_END (bb))
	break;
    }
}

/* Creates a new basic block just after basic block B by splitting
   everything after specified instruction I.  */

static basic_block
rtl_split_block (basic_block bb, void *insnp)
{
  basic_block new_bb;
  rtx insn = insnp;
  edge e;
  edge_iterator ei;

  if (!insn)
    {
      insn = first_insn_after_basic_block_note (bb);

      if (insn)
	insn = PREV_INSN (insn);
      else
	insn = get_last_insn ();
    }

  /* We probably should check type of the insn so that we do not create
     inconsistent cfg.  It is checked in verify_flow_info anyway, so do not
     bother.  */
  if (insn == BB_END (bb))
    emit_note_after (NOTE_INSN_DELETED, insn);

  /* Create the new basic block.  */
  new_bb = create_basic_block (NEXT_INSN (insn), BB_END (bb), bb);
  BB_COPY_PARTITION (new_bb, bb);
  BB_END (bb) = insn;

  /* Redirect the outgoing edges.  */
  new_bb->succs = bb->succs;
  bb->succs = NULL;
  FOR_EACH_EDGE (e, ei, new_bb->succs)
    e->src = new_bb;

  if (bb->il.rtl->global_live_at_start)
    {
      new_bb->il.rtl->global_live_at_start = ALLOC_REG_SET (&reg_obstack);
      new_bb->il.rtl->global_live_at_end = ALLOC_REG_SET (&reg_obstack);
      COPY_REG_SET (new_bb->il.rtl->global_live_at_end, bb->il.rtl->global_live_at_end);

      /* We now have to calculate which registers are live at the end
	 of the split basic block and at the start of the new basic
	 block.  Start with those registers that are known to be live
	 at the end of the original basic block and get
	 propagate_block to determine which registers are live.  */
      COPY_REG_SET (new_bb->il.rtl->global_live_at_start, bb->il.rtl->global_live_at_end);
      propagate_block (new_bb, new_bb->il.rtl->global_live_at_start, NULL, NULL, 0);
      COPY_REG_SET (bb->il.rtl->global_live_at_end,
		    new_bb->il.rtl->global_live_at_start);
#ifdef HAVE_conditional_execution
      /* In the presence of conditional execution we are not able to update
	 liveness precisely.  */
      if (reload_completed)
	{
	  bb->flags |= BB_DIRTY;
	  new_bb->flags |= BB_DIRTY;
	}
#endif
    }

  return new_bb;
}

/* Blocks A and B are to be merged into a single block A.  The insns
   are already contiguous.  */

static void
rtl_merge_blocks (basic_block a, basic_block b)
{
  rtx b_head = BB_HEAD (b), b_end = BB_END (b), a_end = BB_END (a);
  rtx del_first = NULL_RTX, del_last = NULL_RTX;
  int b_empty = 0;

  /* If there was a CODE_LABEL beginning B, delete it.  */
  if (LABEL_P (b_head))
    {
      /* This might have been an EH label that no longer has incoming
	 EH edges.  Update data structures to match.  */
      maybe_remove_eh_handler (b_head);

      /* Detect basic blocks with nothing but a label.  This can happen
	 in particular at the end of a function.  */
      if (b_head == b_end)
	b_empty = 1;

      del_first = del_last = b_head;
      b_head = NEXT_INSN (b_head);
    }

  /* Delete the basic block note and handle blocks containing just that
     note.  */
  if (NOTE_INSN_BASIC_BLOCK_P (b_head))
    {
      if (b_head == b_end)
	b_empty = 1;
      if (! del_last)
	del_first = b_head;

      del_last = b_head;
      b_head = NEXT_INSN (b_head);
    }

  /* If there was a jump out of A, delete it.  */
  if (JUMP_P (a_end))
    {
      rtx prev;

      for (prev = PREV_INSN (a_end); ; prev = PREV_INSN (prev))
	if (!NOTE_P (prev)
	    || NOTE_LINE_NUMBER (prev) == NOTE_INSN_BASIC_BLOCK
	    || prev == BB_HEAD (a))
	  break;

      del_first = a_end;

#ifdef HAVE_cc0
      /* If this was a conditional jump, we need to also delete
	 the insn that set cc0.  */
      if (only_sets_cc0_p (prev))
	{
	  rtx tmp = prev;

	  prev = prev_nonnote_insn (prev);
	  if (!prev)
	    prev = BB_HEAD (a);
	  del_first = tmp;
	}
#endif

      a_end = PREV_INSN (del_first);
    }
  else if (BARRIER_P (NEXT_INSN (a_end)))
    del_first = NEXT_INSN (a_end);

  /* Delete everything marked above as well as crap that might be
     hanging out between the two blocks.  */
  BB_HEAD (b) = NULL;
  delete_insn_chain (del_first, del_last);

  /* Reassociate the insns of B with A.  */
  if (!b_empty)
    {
      rtx x;

      for (x = a_end; x != b_end; x = NEXT_INSN (x))
	set_block_for_insn (x, a);

      set_block_for_insn (b_end, a);

      a_end = b_end;
    }

  BB_END (a) = a_end;
  a->il.rtl->global_live_at_end = b->il.rtl->global_live_at_end;
}

/* Return true when block A and B can be merged.  */
static bool
rtl_can_merge_blocks (basic_block a,basic_block b)
{
  /* If we are partitioning hot/cold basic blocks, we don't want to
     mess up unconditional or indirect jumps that cross between hot
     and cold sections.

     Basic block partitioning may result in some jumps that appear to
     be optimizable (or blocks that appear to be mergeable), but which really
     must be left untouched (they are required to make it safely across
     partition boundaries).  See  the comments at the top of
     bb-reorder.c:partition_hot_cold_basic_blocks for complete details.  */

  if (BB_PARTITION (a) != BB_PARTITION (b))
    return false;

  /* There must be exactly one edge in between the blocks.  */
  return (single_succ_p (a)
	  && single_succ (a) == b
	  && single_pred_p (b)
	  && a != b
	  /* Must be simple edge.  */
	  && !(single_succ_edge (a)->flags & EDGE_COMPLEX)
	  && a->next_bb == b
	  && a != ENTRY_BLOCK_PTR && b != EXIT_BLOCK_PTR
	  /* If the jump insn has side effects,
	     we can't kill the edge.  */
	  && (!JUMP_P (BB_END (a))
	      || (reload_completed
		  ? simplejump_p (BB_END (a)) : onlyjump_p (BB_END (a)))));
}

/* Return the label in the head of basic block BLOCK.  Create one if it doesn't
   exist.  */

rtx
block_label (basic_block block)
{
  if (block == EXIT_BLOCK_PTR)
    return NULL_RTX;

  if (!LABEL_P (BB_HEAD (block)))
    {
      BB_HEAD (block) = emit_label_before (gen_label_rtx (), BB_HEAD (block));
    }

  return BB_HEAD (block);
}

/* Attempt to perform edge redirection by replacing possibly complex jump
   instruction by unconditional jump or removing jump completely.  This can
   apply only if all edges now point to the same block.  The parameters and
   return values are equivalent to redirect_edge_and_branch.  */

edge
try_redirect_by_replacing_jump (edge e, basic_block target, bool in_cfglayout)
{
  basic_block src = e->src;
  rtx insn = BB_END (src), kill_from;
  rtx set;
  int fallthru = 0;

  /* If we are partitioning hot/cold basic blocks, we don't want to
     mess up unconditional or indirect jumps that cross between hot
     and cold sections.

     Basic block partitioning may result in some jumps that appear to
     be optimizable (or blocks that appear to be mergeable), but which really
     must be left untouched (they are required to make it safely across
     partition boundaries).  See  the comments at the top of
     bb-reorder.c:partition_hot_cold_basic_blocks for complete details.  */

  if (find_reg_note (insn, REG_CROSSING_JUMP, NULL_RTX)
      || BB_PARTITION (src) != BB_PARTITION (target))
    return NULL;

  /* We can replace or remove a complex jump only when we have exactly
     two edges.  Also, if we have exactly one outgoing edge, we can
     redirect that.  */
  if (EDGE_COUNT (src->succs) >= 3
      /* Verify that all targets will be TARGET.  Specifically, the
	 edge that is not E must also go to TARGET.  */
      || (EDGE_COUNT (src->succs) == 2
	  && EDGE_SUCC (src, EDGE_SUCC (src, 0) == e)->dest != target))
    return NULL;

  if (!onlyjump_p (insn))
    return NULL;
  if ((!optimize || reload_completed) && tablejump_p (insn, NULL, NULL))
    return NULL;

  /* Avoid removing branch with side effects.  */
  set = single_set (insn);
  if (!set || side_effects_p (set))
    return NULL;

  /* In case we zap a conditional jump, we'll need to kill
     the cc0 setter too.  */
  kill_from = insn;
#ifdef HAVE_cc0
  if (reg_mentioned_p (cc0_rtx, PATTERN (insn)))
    kill_from = PREV_INSN (insn);
#endif

  /* See if we can create the fallthru edge.  */
  if (in_cfglayout || can_fallthru (src, target))
    {
      if (dump_file)
	fprintf (dump_file, "Removing jump %i.\n", INSN_UID (insn));
      fallthru = 1;

      /* Selectively unlink whole insn chain.  */
      if (in_cfglayout)
	{
	  rtx insn = src->il.rtl->footer;

	  delete_insn_chain (kill_from, BB_END (src));

	  /* Remove barriers but keep jumptables.  */
	  while (insn)
	    {
	      if (BARRIER_P (insn))
		{
		  if (PREV_INSN (insn))
		    NEXT_INSN (PREV_INSN (insn)) = NEXT_INSN (insn);
		  else
		    src->il.rtl->footer = NEXT_INSN (insn);
		  if (NEXT_INSN (insn))
		    PREV_INSN (NEXT_INSN (insn)) = PREV_INSN (insn);
		}
	      if (LABEL_P (insn))
		break;
	      insn = NEXT_INSN (insn);
	    }
	}
      else
	delete_insn_chain (kill_from, PREV_INSN (BB_HEAD (target)));
    }

  /* If this already is simplejump, redirect it.  */
  else if (simplejump_p (insn))
    {
      if (e->dest == target)
	return NULL;
      if (dump_file)
	fprintf (dump_file, "Redirecting jump %i from %i to %i.\n",
		 INSN_UID (insn), e->dest->index, target->index);
      if (!redirect_jump (insn, block_label (target), 0))
	{
	  gcc_assert (target == EXIT_BLOCK_PTR);
	  return NULL;
	}
    }

  /* Cannot do anything for target exit block.  */
  else if (target == EXIT_BLOCK_PTR)
    return NULL;

  /* Or replace possibly complicated jump insn by simple jump insn.  */
  else
    {
      rtx target_label = block_label (target);
      rtx barrier, label, table;

      emit_jump_insn_after_noloc (gen_jump (target_label), insn);
      JUMP_LABEL (BB_END (src)) = target_label;
      LABEL_NUSES (target_label)++;
      if (dump_file)
	fprintf (dump_file, "Replacing insn %i by jump %i\n",
		 INSN_UID (insn), INSN_UID (BB_END (src)));


      delete_insn_chain (kill_from, insn);

      /* Recognize a tablejump that we are converting to a
	 simple jump and remove its associated CODE_LABEL
	 and ADDR_VEC or ADDR_DIFF_VEC.  */
      if (tablejump_p (insn, &label, &table))
	delete_insn_chain (label, table);

      barrier = next_nonnote_insn (BB_END (src));
      if (!barrier || !BARRIER_P (barrier))
	emit_barrier_after (BB_END (src));
      else
	{
	  if (barrier != NEXT_INSN (BB_END (src)))
	    {
	      /* Move the jump before barrier so that the notes
		 which originally were or were created before jump table are
		 inside the basic block.  */
	      rtx new_insn = BB_END (src);
	      rtx tmp;

	      for (tmp = NEXT_INSN (BB_END (src)); tmp != barrier;
		   tmp = NEXT_INSN (tmp))
		set_block_for_insn (tmp, src);

	      NEXT_INSN (PREV_INSN (new_insn)) = NEXT_INSN (new_insn);
	      PREV_INSN (NEXT_INSN (new_insn)) = PREV_INSN (new_insn);

	      NEXT_INSN (new_insn) = barrier;
	      NEXT_INSN (PREV_INSN (barrier)) = new_insn;

	      PREV_INSN (new_insn) = PREV_INSN (barrier);
	      PREV_INSN (barrier) = new_insn;
	    }
	}
    }

  /* Keep only one edge out and set proper flags.  */
  if (!single_succ_p (src))
    remove_edge (e);
  gcc_assert (single_succ_p (src));

  e = single_succ_edge (src);
  if (fallthru)
    e->flags = EDGE_FALLTHRU;
  else
    e->flags = 0;

  e->probability = REG_BR_PROB_BASE;
  e->count = src->count;

  /* We don't want a block to end on a line-number note since that has
     the potential of changing the code between -g and not -g.  */
  while (NOTE_P (BB_END (e->src))
	 && NOTE_LINE_NUMBER (BB_END (e->src)) >= 0)
    delete_insn (BB_END (e->src));

  if (e->dest != target)
    redirect_edge_succ (e, target);

  return e;
}

/* Redirect edge representing branch of (un)conditional jump or tablejump,
   NULL on failure  */
static edge
redirect_branch_edge (edge e, basic_block target)
{
  rtx tmp;
  rtx old_label = BB_HEAD (e->dest);
  basic_block src = e->src;
  rtx insn = BB_END (src);

  /* We can only redirect non-fallthru edges of jump insn.  */
  if (e->flags & EDGE_FALLTHRU)
    return NULL;
  else if (!JUMP_P (insn))
    return NULL;

  /* Recognize a tablejump and adjust all matching cases.  */
  if (tablejump_p (insn, NULL, &tmp))
    {
      rtvec vec;
      int j;
      rtx new_label = block_label (target);

      if (target == EXIT_BLOCK_PTR)
	return NULL;
      if (GET_CODE (PATTERN (tmp)) == ADDR_VEC)
	vec = XVEC (PATTERN (tmp), 0);
      else
	vec = XVEC (PATTERN (tmp), 1);

      for (j = GET_NUM_ELEM (vec) - 1; j >= 0; --j)
	if (XEXP (RTVEC_ELT (vec, j), 0) == old_label)
	  {
	    RTVEC_ELT (vec, j) = gen_rtx_LABEL_REF (Pmode, new_label);
	    --LABEL_NUSES (old_label);
	    ++LABEL_NUSES (new_label);
	  }

      /* Handle casesi dispatch insns.  */
      if ((tmp = single_set (insn)) != NULL
	  && SET_DEST (tmp) == pc_rtx
	  && GET_CODE (SET_SRC (tmp)) == IF_THEN_ELSE
	  && GET_CODE (XEXP (SET_SRC (tmp), 2)) == LABEL_REF
	  && XEXP (XEXP (SET_SRC (tmp), 2), 0) == old_label)
	{
	  XEXP (SET_SRC (tmp), 2) = gen_rtx_LABEL_REF (Pmode,
						       new_label);
	  --LABEL_NUSES (old_label);
	  ++LABEL_NUSES (new_label);
	}
    }
  else
    {
      /* ?? We may play the games with moving the named labels from
	 one basic block to the other in case only one computed_jump is
	 available.  */
      if (computed_jump_p (insn)
	  /* A return instruction can't be redirected.  */
	  || returnjump_p (insn))
	return NULL;

      /* If the insn doesn't go where we think, we're confused.  */
      gcc_assert (JUMP_LABEL (insn) == old_label);

      /* If the substitution doesn't succeed, die.  This can happen
	 if the back end emitted unrecognizable instructions or if
	 target is exit block on some arches.  */
      if (!redirect_jump (insn, block_label (target), 0))
	{
	  gcc_assert (target == EXIT_BLOCK_PTR);
	  return NULL;
	}
    }

  if (dump_file)
    fprintf (dump_file, "Edge %i->%i redirected to %i\n",
	     e->src->index, e->dest->index, target->index);

  if (e->dest != target)
    e = redirect_edge_succ_nodup (e, target);
  return e;
}

/* Attempt to change code to redirect edge E to TARGET.  Don't do that on
   expense of adding new instructions or reordering basic blocks.

   Function can be also called with edge destination equivalent to the TARGET.
   Then it should try the simplifications and do nothing if none is possible.

   Return edge representing the branch if transformation succeeded.  Return NULL
   on failure.
   We still return NULL in case E already destinated TARGET and we didn't
   managed to simplify instruction stream.  */

static edge
rtl_redirect_edge_and_branch (edge e, basic_block target)
{
  edge ret;
  basic_block src = e->src;

  if (e->flags & (EDGE_ABNORMAL_CALL | EDGE_EH))
    return NULL;

  if (e->dest == target)
    return e;

  if ((ret = try_redirect_by_replacing_jump (e, target, false)) != NULL)
    {
      src->flags |= BB_DIRTY;
      return ret;
    }

  ret = redirect_branch_edge (e, target);
  if (!ret)
    return NULL;

  src->flags |= BB_DIRTY;
  return ret;
}

/* Like force_nonfallthru below, but additionally performs redirection
   Used by redirect_edge_and_branch_force.  */

static basic_block
force_nonfallthru_and_redirect (edge e, basic_block target)
{
  basic_block jump_block, new_bb = NULL, src = e->src;
  rtx note;
  edge new_edge;
  int abnormal_edge_flags = 0;

  /* In the case the last instruction is conditional jump to the next
     instruction, first redirect the jump itself and then continue
     by creating a basic block afterwards to redirect fallthru edge.  */
  if (e->src != ENTRY_BLOCK_PTR && e->dest != EXIT_BLOCK_PTR
      && any_condjump_p (BB_END (e->src))
      && JUMP_LABEL (BB_END (e->src)) == BB_HEAD (e->dest))
    {
      rtx note;
      edge b = unchecked_make_edge (e->src, target, 0);
      bool redirected;

      redirected = redirect_jump (BB_END (e->src), block_label (target), 0);
      gcc_assert (redirected);

      note = find_reg_note (BB_END (e->src), REG_BR_PROB, NULL_RTX);
      if (note)
	{
	  int prob = INTVAL (XEXP (note, 0));

	  b->probability = prob;
	  b->count = e->count * prob / REG_BR_PROB_BASE;
	  e->probability -= e->probability;
	  e->count -= b->count;
	  if (e->probability < 0)
	    e->probability = 0;
	  if (e->count < 0)
	    e->count = 0;
	}
    }

  if (e->flags & EDGE_ABNORMAL)
    {
      /* Irritating special case - fallthru edge to the same block as abnormal
	 edge.
	 We can't redirect abnormal edge, but we still can split the fallthru
	 one and create separate abnormal edge to original destination.
	 This allows bb-reorder to make such edge non-fallthru.  */
      gcc_assert (e->dest == target);
      abnormal_edge_flags = e->flags & ~(EDGE_FALLTHRU | EDGE_CAN_FALLTHRU);
      e->flags &= EDGE_FALLTHRU | EDGE_CAN_FALLTHRU;
    }
  else
    {
      gcc_assert (e->flags & EDGE_FALLTHRU);
      if (e->src == ENTRY_BLOCK_PTR)
	{
	  /* We can't redirect the entry block.  Create an empty block
	     at the start of the function which we use to add the new
	     jump.  */
	  edge tmp;
	  edge_iterator ei;
	  bool found = false;

	  basic_block bb = create_basic_block (BB_HEAD (e->dest), NULL, ENTRY_BLOCK_PTR);

	  /* Change the existing edge's source to be the new block, and add
	     a new edge from the entry block to the new block.  */
	  e->src = bb;
	  for (ei = ei_start (ENTRY_BLOCK_PTR->succs); (tmp = ei_safe_edge (ei)); )
	    {
	      if (tmp == e)
		{
		  VEC_unordered_remove (edge, ENTRY_BLOCK_PTR->succs, ei.index);
		  found = true;
		  break;
		}
	      else
		ei_next (&ei);
	    }

	  gcc_assert (found);

	  VEC_safe_push (edge, gc, bb->succs, e);
	  make_single_succ_edge (ENTRY_BLOCK_PTR, bb, EDGE_FALLTHRU);
	}
    }

  if (EDGE_COUNT (e->src->succs) >= 2 || abnormal_edge_flags)
    {
      /* Create the new structures.  */

      /* If the old block ended with a tablejump, skip its table
	 by searching forward from there.  Otherwise start searching
	 forward from the last instruction of the old block.  */
      if (!tablejump_p (BB_END (e->src), NULL, &note))
	note = BB_END (e->src);
      note = NEXT_INSN (note);

      jump_block = create_basic_block (note, NULL, e->src);
      jump_block->count = e->count;
      jump_block->frequency = EDGE_FREQUENCY (e);
      jump_block->loop_depth = target->loop_depth;

      if (target->il.rtl->global_live_at_start)
	{
	  jump_block->il.rtl->global_live_at_start = ALLOC_REG_SET (&reg_obstack);
	  jump_block->il.rtl->global_live_at_end = ALLOC_REG_SET (&reg_obstack);
	  COPY_REG_SET (jump_block->il.rtl->global_live_at_start,
			target->il.rtl->global_live_at_start);
	  COPY_REG_SET (jump_block->il.rtl->global_live_at_end,
			target->il.rtl->global_live_at_start);
	}

      /* Make sure new block ends up in correct hot/cold section.  */

      BB_COPY_PARTITION (jump_block, e->src);
      if (flag_reorder_blocks_and_partition
	  && targetm.have_named_sections
	  && JUMP_P (BB_END (jump_block))
	  && !any_condjump_p (BB_END (jump_block))
	  && (EDGE_SUCC (jump_block, 0)->flags & EDGE_CROSSING))
	REG_NOTES (BB_END (jump_block)) = gen_rtx_EXPR_LIST (REG_CROSSING_JUMP,
							     NULL_RTX,
							     REG_NOTES
							     (BB_END
							      (jump_block)));

      /* Wire edge in.  */
      new_edge = make_edge (e->src, jump_block, EDGE_FALLTHRU);
      new_edge->probability = e->probability;
      new_edge->count = e->count;

      /* Redirect old edge.  */
      redirect_edge_pred (e, jump_block);
      e->probability = REG_BR_PROB_BASE;

      new_bb = jump_block;
    }
  else
    jump_block = e->src;

  e->flags &= ~EDGE_FALLTHRU;
  if (target == EXIT_BLOCK_PTR)
    {
#ifdef HAVE_return
	emit_jump_insn_after_noloc (gen_return (), BB_END (jump_block));
#else
	gcc_unreachable ();
#endif
    }
  else
    {
      rtx label = block_label (target);
      emit_jump_insn_after_noloc (gen_jump (label), BB_END (jump_block));
      JUMP_LABEL (BB_END (jump_block)) = label;
      LABEL_NUSES (label)++;
    }

  emit_barrier_after (BB_END (jump_block));
  redirect_edge_succ_nodup (e, target);

  if (abnormal_edge_flags)
    make_edge (src, target, abnormal_edge_flags);

  return new_bb;
}

/* Edge E is assumed to be fallthru edge.  Emit needed jump instruction
   (and possibly create new basic block) to make edge non-fallthru.
   Return newly created BB or NULL if none.  */

basic_block
force_nonfallthru (edge e)
{
  return force_nonfallthru_and_redirect (e, e->dest);
}

/* Redirect edge even at the expense of creating new jump insn or
   basic block.  Return new basic block if created, NULL otherwise.
   Conversion must be possible.  */

static basic_block
rtl_redirect_edge_and_branch_force (edge e, basic_block target)
{
  if (redirect_edge_and_branch (e, target)
      || e->dest == target)
    return NULL;

  /* In case the edge redirection failed, try to force it to be non-fallthru
     and redirect newly created simplejump.  */
  e->src->flags |= BB_DIRTY;
  return force_nonfallthru_and_redirect (e, target);
}

/* The given edge should potentially be a fallthru edge.  If that is in
   fact true, delete the jump and barriers that are in the way.  */

static void
rtl_tidy_fallthru_edge (edge e)
{
  rtx q;
  basic_block b = e->src, c = b->next_bb;

  /* ??? In a late-running flow pass, other folks may have deleted basic
     blocks by nopping out blocks, leaving multiple BARRIERs between here
     and the target label. They ought to be chastised and fixed.

     We can also wind up with a sequence of undeletable labels between
     one block and the next.

     So search through a sequence of barriers, labels, and notes for
     the head of block C and assert that we really do fall through.  */

  for (q = NEXT_INSN (BB_END (b)); q != BB_HEAD (c); q = NEXT_INSN (q))
    if (INSN_P (q))
      return;

  /* Remove what will soon cease being the jump insn from the source block.
     If block B consisted only of this single jump, turn it into a deleted
     note.  */
  q = BB_END (b);
  if (JUMP_P (q)
      && onlyjump_p (q)
      && (any_uncondjump_p (q)
	  || single_succ_p (b)))
    {
#ifdef HAVE_cc0
      /* If this was a conditional jump, we need to also delete
	 the insn that set cc0.  */
      if (any_condjump_p (q) && only_sets_cc0_p (PREV_INSN (q)))
	q = PREV_INSN (q);
#endif

      q = PREV_INSN (q);

      /* We don't want a block to end on a line-number note since that has
	 the potential of changing the code between -g and not -g.  */
      while (NOTE_P (q) && NOTE_LINE_NUMBER (q) >= 0)
	q = PREV_INSN (q);
    }

  /* Selectively unlink the sequence.  */
  if (q != PREV_INSN (BB_HEAD (c)))
    delete_insn_chain (NEXT_INSN (q), PREV_INSN (BB_HEAD (c)));

  e->flags |= EDGE_FALLTHRU;
}

/* Should move basic block BB after basic block AFTER.  NIY.  */

static bool
rtl_move_block_after (basic_block bb ATTRIBUTE_UNUSED,
		      basic_block after ATTRIBUTE_UNUSED)
{
  return false;
}

/* Split a (typically critical) edge.  Return the new block.
   The edge must not be abnormal.

   ??? The code generally expects to be called on critical edges.
   The case of a block ending in an unconditional jump to a
   block with multiple predecessors is not handled optimally.  */

static basic_block
rtl_split_edge (edge edge_in)
{
  basic_block bb;
  rtx before;

  /* Abnormal edges cannot be split.  */
  gcc_assert (!(edge_in->flags & EDGE_ABNORMAL));

  /* We are going to place the new block in front of edge destination.
     Avoid existence of fallthru predecessors.  */
  if ((edge_in->flags & EDGE_FALLTHRU) == 0)
    {
      edge e;
      edge_iterator ei;

      FOR_EACH_EDGE (e, ei, edge_in->dest->preds)
	if (e->flags & EDGE_FALLTHRU)
	  break;

      if (e)
	force_nonfallthru (e);
    }

  /* Create the basic block note.  */
  if (edge_in->dest != EXIT_BLOCK_PTR)
    before = BB_HEAD (edge_in->dest);
  else
    before = NULL_RTX;

  /* If this is a fall through edge to the exit block, the blocks might be
     not adjacent, and the right place is the after the source.  */
  if (edge_in->flags & EDGE_FALLTHRU && edge_in->dest == EXIT_BLOCK_PTR)
    {
      before = NEXT_INSN (BB_END (edge_in->src));
      bb = create_basic_block (before, NULL, edge_in->src);
      BB_COPY_PARTITION (bb, edge_in->src);
    }
  else
    {
      bb = create_basic_block (before, NULL, edge_in->dest->prev_bb);
      /* ??? Why not edge_in->dest->prev_bb here?  */
      BB_COPY_PARTITION (bb, edge_in->dest);
    }

  /* ??? This info is likely going to be out of date very soon.  */
  if (edge_in->dest->il.rtl->global_live_at_start)
    {
      bb->il.rtl->global_live_at_start = ALLOC_REG_SET (&reg_obstack);
      bb->il.rtl->global_live_at_end = ALLOC_REG_SET (&reg_obstack);
      COPY_REG_SET (bb->il.rtl->global_live_at_start,
		    edge_in->dest->il.rtl->global_live_at_start);
      COPY_REG_SET (bb->il.rtl->global_live_at_end,
		    edge_in->dest->il.rtl->global_live_at_start);
    }

  make_single_succ_edge (bb, edge_in->dest, EDGE_FALLTHRU);

  /* For non-fallthru edges, we must adjust the predecessor's
     jump instruction to target our new block.  */
  if ((edge_in->flags & EDGE_FALLTHRU) == 0)
    {
      edge redirected = redirect_edge_and_branch (edge_in, bb);
      gcc_assert (redirected);
    }
  else
    redirect_edge_succ (edge_in, bb);

  return bb;
}

/* Queue instructions for insertion on an edge between two basic blocks.
   The new instructions and basic blocks (if any) will not appear in the
   CFG until commit_edge_insertions is called.  */

void
insert_insn_on_edge (rtx pattern, edge e)
{
  /* We cannot insert instructions on an abnormal critical edge.
     It will be easier to find the culprit if we die now.  */
  gcc_assert (!((e->flags & EDGE_ABNORMAL) && EDGE_CRITICAL_P (e)));

  if (e->insns.r == NULL_RTX)
    start_sequence ();
  else
    push_to_sequence (e->insns.r);

  emit_insn (pattern);

  e->insns.r = get_insns ();
  end_sequence ();
}

/* Update the CFG for the instructions queued on edge E.  */

static void
commit_one_edge_insertion (edge e, int watch_calls)
{
  rtx before = NULL_RTX, after = NULL_RTX, insns, tmp, last;
  basic_block bb = NULL;

  /* Pull the insns off the edge now since the edge might go away.  */
  insns = e->insns.r;
  e->insns.r = NULL_RTX;

  /* Special case -- avoid inserting code between call and storing
     its return value.  */
  if (watch_calls && (e->flags & EDGE_FALLTHRU)
      && single_pred_p (e->dest)
      && e->src != ENTRY_BLOCK_PTR
      && CALL_P (BB_END (e->src)))
    {
      rtx next = next_nonnote_insn (BB_END (e->src));

      after = BB_HEAD (e->dest);
      /* The first insn after the call may be a stack pop, skip it.  */
      while (next
	     && keep_with_call_p (next))
	{
	  after = next;
	  next = next_nonnote_insn (next);
	}
      bb = e->dest;
    }
  if (!before && !after)
    {
      /* Figure out where to put these things.  If the destination has
	 one predecessor, insert there.  Except for the exit block.  */
      if (single_pred_p (e->dest) && e->dest != EXIT_BLOCK_PTR)
	{
	  bb = e->dest;

	  /* Get the location correct wrt a code label, and "nice" wrt
	     a basic block note, and before everything else.  */
	  tmp = BB_HEAD (bb);
	  if (LABEL_P (tmp))
	    tmp = NEXT_INSN (tmp);
	  if (NOTE_INSN_BASIC_BLOCK_P (tmp))
	    tmp = NEXT_INSN (tmp);
	  if (tmp == BB_HEAD (bb))
	    before = tmp;
	  else if (tmp)
	    after = PREV_INSN (tmp);
	  else
	    after = get_last_insn ();
	}

      /* If the source has one successor and the edge is not abnormal,
	 insert there.  Except for the entry block.  */
      else if ((e->flags & EDGE_ABNORMAL) == 0
	       && single_succ_p (e->src)
	       && e->src != ENTRY_BLOCK_PTR)
	{
	  bb = e->src;

	  /* It is possible to have a non-simple jump here.  Consider a target
	     where some forms of unconditional jumps clobber a register.  This
	     happens on the fr30 for example.

	     We know this block has a single successor, so we can just emit
	     the queued insns before the jump.  */
	  if (JUMP_P (BB_END (bb)))
	    before = BB_END (bb);
	  else
	    {
	      /* We'd better be fallthru, or we've lost track of
		 what's what.  */
	      gcc_assert (e->flags & EDGE_FALLTHRU);

	      after = BB_END (bb);
	    }
	}
      /* Otherwise we must split the edge.  */
      else
	{
	  bb = split_edge (e);
	  after = BB_END (bb);

	  if (flag_reorder_blocks_and_partition
	      && targetm.have_named_sections
	      && e->src != ENTRY_BLOCK_PTR
	      && BB_PARTITION (e->src) == BB_COLD_PARTITION
	      && !(e->flags & EDGE_CROSSING))
	    {
	      rtx bb_note, cur_insn;

	      bb_note = NULL_RTX;
	      for (cur_insn = BB_HEAD (bb); cur_insn != NEXT_INSN (BB_END (bb));
		   cur_insn = NEXT_INSN (cur_insn))
		if (NOTE_P (cur_insn)
		    && NOTE_LINE_NUMBER (cur_insn) == NOTE_INSN_BASIC_BLOCK)
		  {
		    bb_note = cur_insn;
		    break;
		  }

	      if (JUMP_P (BB_END (bb))
		  && !any_condjump_p (BB_END (bb))
		  && (single_succ_edge (bb)->flags & EDGE_CROSSING))
		REG_NOTES (BB_END (bb)) = gen_rtx_EXPR_LIST
		  (REG_CROSSING_JUMP, NULL_RTX, REG_NOTES (BB_END (bb)));
	    }
	}
    }

  /* Now that we've found the spot, do the insertion.  */

  if (before)
    {
      emit_insn_before_noloc (insns, before);
      last = prev_nonnote_insn (before);
    }
  else
    last = emit_insn_after_noloc (insns, after);

  if (returnjump_p (last))
    {
      /* ??? Remove all outgoing edges from BB and add one for EXIT.
	 This is not currently a problem because this only happens
	 for the (single) epilogue, which already has a fallthru edge
	 to EXIT.  */

      e = single_succ_edge (bb);
      gcc_assert (e->dest == EXIT_BLOCK_PTR
		  && single_succ_p (bb) && (e->flags & EDGE_FALLTHRU));

      e->flags &= ~EDGE_FALLTHRU;
      emit_barrier_after (last);

      if (before)
	delete_insn (before);
    }
  else
    gcc_assert (!JUMP_P (last));

  /* Mark the basic block for find_many_sub_basic_blocks.  */
  bb->aux = &bb->aux;
}

/* Update the CFG for all queued instructions.  */

void
commit_edge_insertions (void)
{
  basic_block bb;
  sbitmap blocks;
  bool changed = false;

#ifdef ENABLE_CHECKING
  verify_flow_info ();
#endif

  FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR, EXIT_BLOCK_PTR, next_bb)
    {
      edge e;
      edge_iterator ei;

      FOR_EACH_EDGE (e, ei, bb->succs)
	if (e->insns.r)
	  {
	    changed = true;
	    commit_one_edge_insertion (e, false);
	  }
    }

  if (!changed)
    return;

  blocks = sbitmap_alloc (last_basic_block);
  sbitmap_zero (blocks);
  FOR_EACH_BB (bb)
    if (bb->aux)
      {
	SET_BIT (blocks, bb->index);
	/* Check for forgotten bb->aux values before commit_edge_insertions
	   call.  */
	gcc_assert (bb->aux == &bb->aux);
	bb->aux = NULL;
      }
  find_many_sub_basic_blocks (blocks);
  sbitmap_free (blocks);
}

/* Update the CFG for all queued instructions, taking special care of inserting
   code on edges between call and storing its return value.  */

void
commit_edge_insertions_watch_calls (void)
{
  basic_block bb;
  sbitmap blocks;
  bool changed = false;

#ifdef ENABLE_CHECKING
  verify_flow_info ();
#endif

  FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR, EXIT_BLOCK_PTR, next_bb)
    {
      edge e;
      edge_iterator ei;

      FOR_EACH_EDGE (e, ei, bb->succs)
	if (e->insns.r)
	  {
	    changed = true;
	    commit_one_edge_insertion (e, true);
	  }
    }

  if (!changed)
    return;

  blocks = sbitmap_alloc (last_basic_block);
  sbitmap_zero (blocks);
  FOR_EACH_BB (bb)
    if (bb->aux)
      {
	SET_BIT (blocks, bb->index);
	/* Check for forgotten bb->aux values before commit_edge_insertions
	   call.  */
	gcc_assert (bb->aux == &bb->aux);
	bb->aux = NULL;
      }
  find_many_sub_basic_blocks (blocks);
  sbitmap_free (blocks);
}

/* Print out RTL-specific basic block information (live information
   at start and end).  */

static void
rtl_dump_bb (basic_block bb, FILE *outf, int indent)
{
  rtx insn;
  rtx last;
  char *s_indent;

  s_indent = alloca ((size_t) indent + 1);
  memset (s_indent, ' ', (size_t) indent);
  s_indent[indent] = '\0';

  fprintf (outf, ";;%s Registers live at start: ", s_indent);
  dump_regset (bb->il.rtl->global_live_at_start, outf);
  putc ('\n', outf);

  for (insn = BB_HEAD (bb), last = NEXT_INSN (BB_END (bb)); insn != last;
       insn = NEXT_INSN (insn))
    print_rtl_single (outf, insn);

  fprintf (outf, ";;%s Registers live at end: ", s_indent);
  dump_regset (bb->il.rtl->global_live_at_end, outf);
  putc ('\n', outf);
}

/* Like print_rtl, but also print out live information for the start of each
   basic block.  */

void
print_rtl_with_bb (FILE *outf, rtx rtx_first)
{
  rtx tmp_rtx;

  if (rtx_first == 0)
    fprintf (outf, "(nil)\n");
  else
    {
      enum bb_state { NOT_IN_BB, IN_ONE_BB, IN_MULTIPLE_BB };
      int max_uid = get_max_uid ();
      basic_block *start = XCNEWVEC (basic_block, max_uid);
      basic_block *end = XCNEWVEC (basic_block, max_uid);
      enum bb_state *in_bb_p = XCNEWVEC (enum bb_state, max_uid);

      basic_block bb;

      FOR_EACH_BB_REVERSE (bb)
	{
	  rtx x;

	  start[INSN_UID (BB_HEAD (bb))] = bb;
	  end[INSN_UID (BB_END (bb))] = bb;
	  for (x = BB_HEAD (bb); x != NULL_RTX; x = NEXT_INSN (x))
	    {
	      enum bb_state state = IN_MULTIPLE_BB;

	      if (in_bb_p[INSN_UID (x)] == NOT_IN_BB)
		state = IN_ONE_BB;
	      in_bb_p[INSN_UID (x)] = state;

	      if (x == BB_END (bb))
		break;
	    }
	}

      for (tmp_rtx = rtx_first; NULL != tmp_rtx; tmp_rtx = NEXT_INSN (tmp_rtx))
	{
	  int did_output;

	  if ((bb = start[INSN_UID (tmp_rtx)]) != NULL)
	    {
	      fprintf (outf, ";; Start of basic block %d, registers live:",
		       bb->index);
	      dump_regset (bb->il.rtl->global_live_at_start, outf);
	      putc ('\n', outf);
	    }

	  if (in_bb_p[INSN_UID (tmp_rtx)] == NOT_IN_BB
	      && !NOTE_P (tmp_rtx)
	      && !BARRIER_P (tmp_rtx))
	    fprintf (outf, ";; Insn is not within a basic block\n");
	  else if (in_bb_p[INSN_UID (tmp_rtx)] == IN_MULTIPLE_BB)
	    fprintf (outf, ";; Insn is in multiple basic blocks\n");

	  did_output = print_rtl_single (outf, tmp_rtx);

	  if ((bb = end[INSN_UID (tmp_rtx)]) != NULL)
	    {
	      fprintf (outf, ";; End of basic block %d, registers live:\n",
		       bb->index);
	      dump_regset (bb->il.rtl->global_live_at_end, outf);
	      putc ('\n', outf);
	    }

	  if (did_output)
	    putc ('\n', outf);
	}

      free (start);
      free (end);
      free (in_bb_p);
    }

  if (current_function_epilogue_delay_list != 0)
    {
      fprintf (outf, "\n;; Insns in epilogue delay list:\n\n");
      for (tmp_rtx = current_function_epilogue_delay_list; tmp_rtx != 0;
	   tmp_rtx = XEXP (tmp_rtx, 1))
	print_rtl_single (outf, XEXP (tmp_rtx, 0));
    }
}

void
update_br_prob_note (basic_block bb)
{
  rtx note;
  if (!JUMP_P (BB_END (bb)))
    return;
  note = find_reg_note (BB_END (bb), REG_BR_PROB, NULL_RTX);
  if (!note || INTVAL (XEXP (note, 0)) == BRANCH_EDGE (bb)->probability)
    return;
  XEXP (note, 0) = GEN_INT (BRANCH_EDGE (bb)->probability);
}

/* Get the last insn associated with block BB (that includes barriers and
   tablejumps after BB).  */
rtx
get_last_bb_insn (basic_block bb)
{
  rtx tmp;
  rtx end = BB_END (bb);

  /* Include any jump table following the basic block.  */
  if (tablejump_p (end, NULL, &tmp))
    end = tmp;

  /* Include any barriers that may follow the basic block.  */
  tmp = next_nonnote_insn (end);
  while (tmp && BARRIER_P (tmp))
    {
      end = tmp;
      tmp = next_nonnote_insn (end);
    }

  return end;
}

/* Verify the CFG and RTL consistency common for both underlying RTL and
   cfglayout RTL.

   Currently it does following checks:

   - test head/end pointers
   - overlapping of basic blocks
   - headers of basic blocks (the NOTE_INSN_BASIC_BLOCK note)
   - tails of basic blocks (ensure that boundary is necessary)
   - scans body of the basic block for JUMP_INSN, CODE_LABEL
     and NOTE_INSN_BASIC_BLOCK
   - verify that no fall_thru edge crosses hot/cold partition boundaries

   In future it can be extended check a lot of other stuff as well
   (reachability of basic blocks, life information, etc. etc.).  */

static int
rtl_verify_flow_info_1 (void)
{
  const int max_uid = get_max_uid ();
  rtx last_head = get_last_insn ();
  basic_block *bb_info;
  rtx x;
  int err = 0;
  basic_block bb;

  bb_info = XCNEWVEC (basic_block, max_uid);

  FOR_EACH_BB_REVERSE (bb)
    {
      rtx head = BB_HEAD (bb);
      rtx end = BB_END (bb);

      /* Verify the end of the basic block is in the INSN chain.  */
      for (x = last_head; x != NULL_RTX; x = PREV_INSN (x))
	if (x == end)
	  break;

      if (!(bb->flags & BB_RTL))
	{
	  error ("BB_RTL flag not set for block %d", bb->index);
	  err = 1;
	}

      if (!x)
	{
	  error ("end insn %d for block %d not found in the insn stream",
		 INSN_UID (end), bb->index);
	  err = 1;
	}

      /* Work backwards from the end to the head of the basic block
	 to verify the head is in the RTL chain.  */
      for (; x != NULL_RTX; x = PREV_INSN (x))
	{
	  /* While walking over the insn chain, verify insns appear
	     in only one basic block and initialize the BB_INFO array
	     used by other passes.  */
	  if (bb_info[INSN_UID (x)] != NULL)
	    {
	      error ("insn %d is in multiple basic blocks (%d and %d)",
		     INSN_UID (x), bb->index, bb_info[INSN_UID (x)]->index);
	      err = 1;
	    }

	  bb_info[INSN_UID (x)] = bb;

	  if (x == head)
	    break;
	}
      if (!x)
	{
	  error ("head insn %d for block %d not found in the insn stream",
		 INSN_UID (head), bb->index);
	  err = 1;
	}

      last_head = x;
    }

  /* Now check the basic blocks (boundaries etc.) */
  FOR_EACH_BB_REVERSE (bb)
    {
      int n_fallthru = 0, n_eh = 0, n_call = 0, n_abnormal = 0, n_branch = 0;
      edge e, fallthru = NULL;
      rtx note;
      edge_iterator ei;

      if (JUMP_P (BB_END (bb))
	  && (note = find_reg_note (BB_END (bb), REG_BR_PROB, NULL_RTX))
	  && EDGE_COUNT (bb->succs) >= 2
	  && any_condjump_p (BB_END (bb)))
	{
	  if (INTVAL (XEXP (note, 0)) != BRANCH_EDGE (bb)->probability
	      && profile_status != PROFILE_ABSENT)
	    {
	      error ("verify_flow_info: REG_BR_PROB does not match cfg %wi %i",
		     INTVAL (XEXP (note, 0)), BRANCH_EDGE (bb)->probability);
	      err = 1;
	    }
	}
      FOR_EACH_EDGE (e, ei, bb->succs)
	{
	  if (e->flags & EDGE_FALLTHRU)
	    {
	      n_fallthru++, fallthru = e;
	      if ((e->flags & EDGE_CROSSING)
		  || (BB_PARTITION (e->src) != BB_PARTITION (e->dest)
		      && e->src != ENTRY_BLOCK_PTR
		      && e->dest != EXIT_BLOCK_PTR))
	    {
		  error ("fallthru edge crosses section boundary (bb %i)",
			 e->src->index);
		  err = 1;
		}
	    }

	  if ((e->flags & ~(EDGE_DFS_BACK
			    | EDGE_CAN_FALLTHRU
			    | EDGE_IRREDUCIBLE_LOOP
			    | EDGE_LOOP_EXIT
			    | EDGE_CROSSING)) == 0)
	    n_branch++;

	  if (e->flags & EDGE_ABNORMAL_CALL)
	    n_call++;

	  if (e->flags & EDGE_EH)
	    n_eh++;
	  else if (e->flags & EDGE_ABNORMAL)
	    n_abnormal++;
	}

      if (n_eh && GET_CODE (PATTERN (BB_END (bb))) != RESX
	  && !find_reg_note (BB_END (bb), REG_EH_REGION, NULL_RTX))
	{
	  error ("missing REG_EH_REGION note in the end of bb %i", bb->index);
	  err = 1;
	}
      if (n_branch
	  && (!JUMP_P (BB_END (bb))
	      || (n_branch > 1 && (any_uncondjump_p (BB_END (bb))
				   || any_condjump_p (BB_END (bb))))))
	{
	  error ("too many outgoing branch edges from bb %i", bb->index);
	  err = 1;
	}
      if (n_fallthru && any_uncondjump_p (BB_END (bb)))
	{
	  error ("fallthru edge after unconditional jump %i", bb->index);
	  err = 1;
	}
      if (n_branch != 1 && any_uncondjump_p (BB_END (bb)))
	{
	  error ("wrong amount of branch edges after unconditional jump %i", bb->index);
	  err = 1;
	}
      if (n_branch != 1 && any_condjump_p (BB_END (bb))
	  && JUMP_LABEL (BB_END (bb)) != BB_HEAD (fallthru->dest))
	{
	  error ("wrong amount of branch edges after conditional jump %i",
		 bb->index);
	  err = 1;
	}
      if (n_call && !CALL_P (BB_END (bb)))
	{
	  error ("call edges for non-call insn in bb %i", bb->index);
	  err = 1;
	}
      if (n_abnormal
	  && (!CALL_P (BB_END (bb)) && n_call != n_abnormal)
	  && (!JUMP_P (BB_END (bb))
	      || any_condjump_p (BB_END (bb))
	      || any_uncondjump_p (BB_END (bb))))
	{
	  error ("abnormal edges for no purpose in bb %i", bb->index);
	  err = 1;
	}

      for (x = BB_HEAD (bb); x != NEXT_INSN (BB_END (bb)); x = NEXT_INSN (x))
	/* We may have a barrier inside a basic block before dead code
	   elimination.  There is no BLOCK_FOR_INSN field in a barrier.  */
	if (!BARRIER_P (x) && BLOCK_FOR_INSN (x) != bb)
	  {
	    debug_rtx (x);
	    if (! BLOCK_FOR_INSN (x))
	      error
		("insn %d inside basic block %d but block_for_insn is NULL",
		 INSN_UID (x), bb->index);
	    else
	      error
		("insn %d inside basic block %d but block_for_insn is %i",
		 INSN_UID (x), bb->index, BLOCK_FOR_INSN (x)->index);

	    err = 1;
	  }

      /* OK pointers are correct.  Now check the header of basic
	 block.  It ought to contain optional CODE_LABEL followed
	 by NOTE_BASIC_BLOCK.  */
      x = BB_HEAD (bb);
      if (LABEL_P (x))
	{
	  if (BB_END (bb) == x)
	    {
	      error ("NOTE_INSN_BASIC_BLOCK is missing for block %d",
		     bb->index);
	      err = 1;
	    }

	  x = NEXT_INSN (x);
	}

      if (!NOTE_INSN_BASIC_BLOCK_P (x) || NOTE_BASIC_BLOCK (x) != bb)
	{
	  error ("NOTE_INSN_BASIC_BLOCK is missing for block %d",
		 bb->index);
	  err = 1;
	}

      if (BB_END (bb) == x)
	/* Do checks for empty blocks here.  */
	;
      else
	for (x = NEXT_INSN (x); x; x = NEXT_INSN (x))
	  {
	    if (NOTE_INSN_BASIC_BLOCK_P (x))
	      {
		error ("NOTE_INSN_BASIC_BLOCK %d in middle of basic block %d",
		       INSN_UID (x), bb->index);
		err = 1;
	      }

	    if (x == BB_END (bb))
	      break;

	    if (control_flow_insn_p (x))
	      {
		error ("in basic block %d:", bb->index);
		fatal_insn ("flow control insn inside a basic block", x);
	      }
	  }
    }

  /* Clean up.  */
  free (bb_info);
  return err;
}

/* Verify the CFG and RTL consistency common for both underlying RTL and
   cfglayout RTL.

   Currently it does following checks:
   - all checks of rtl_verify_flow_info_1
   - check that all insns are in the basic blocks
     (except the switch handling code, barriers and notes)
   - check that all returns are followed by barriers
   - check that all fallthru edge points to the adjacent blocks.  */
static int
rtl_verify_flow_info (void)
{
  basic_block bb;
  int err = rtl_verify_flow_info_1 ();
  rtx x;
  int num_bb_notes;
  const rtx rtx_first = get_insns ();
  basic_block last_bb_seen = ENTRY_BLOCK_PTR, curr_bb = NULL;

  FOR_EACH_BB_REVERSE (bb)
    {
      edge e;
      edge_iterator ei;

      if (bb->predictions)
	{
	  error ("bb prediction set for block %i, but it is not used in RTL land", bb->index);
	  err = 1;
	}

      FOR_EACH_EDGE (e, ei, bb->succs)
	if (e->flags & EDGE_FALLTHRU)
	  break;
      if (!e)
	{
	  rtx insn;

	  /* Ensure existence of barrier in BB with no fallthru edges.  */
	  for (insn = BB_END (bb); !insn || !BARRIER_P (insn);
	       insn = NEXT_INSN (insn))
	    if (!insn
		|| (NOTE_P (insn)
		    && NOTE_LINE_NUMBER (insn) == NOTE_INSN_BASIC_BLOCK))
		{
		  error ("missing barrier after block %i", bb->index);
		  err = 1;
		  break;
		}
	}
      else if (e->src != ENTRY_BLOCK_PTR
	       && e->dest != EXIT_BLOCK_PTR)
	{
	  rtx insn;

	  if (e->src->next_bb != e->dest)
	    {
	      error
		("verify_flow_info: Incorrect blocks for fallthru %i->%i",
		 e->src->index, e->dest->index);
	      err = 1;
	    }
	  else
	    for (insn = NEXT_INSN (BB_END (e->src)); insn != BB_HEAD (e->dest);
		 insn = NEXT_INSN (insn))
	      if (BARRIER_P (insn) || INSN_P (insn))
		{
		  error ("verify_flow_info: Incorrect fallthru %i->%i",
			 e->src->index, e->dest->index);
		  fatal_insn ("wrong insn in the fallthru edge", insn);
		  err = 1;
		}
	}
    }

  num_bb_notes = 0;
  last_bb_seen = ENTRY_BLOCK_PTR;

  for (x = rtx_first; x; x = NEXT_INSN (x))
    {
      if (NOTE_INSN_BASIC_BLOCK_P (x))
	{
	  bb = NOTE_BASIC_BLOCK (x);

	  num_bb_notes++;
	  if (bb != last_bb_seen->next_bb)
	    internal_error ("basic blocks not laid down consecutively");

	  curr_bb = last_bb_seen = bb;
	}

      if (!curr_bb)
	{
	  switch (GET_CODE (x))
	    {
	    case BARRIER:
	    case NOTE:
	      break;

	    case CODE_LABEL:
	      /* An addr_vec is placed outside any basic block.  */
	      if (NEXT_INSN (x)
		  && JUMP_P (NEXT_INSN (x))
		  && (GET_CODE (PATTERN (NEXT_INSN (x))) == ADDR_DIFF_VEC
		      || GET_CODE (PATTERN (NEXT_INSN (x))) == ADDR_VEC))
		x = NEXT_INSN (x);

	      /* But in any case, non-deletable labels can appear anywhere.  */
	      break;

	    default:
	      fatal_insn ("insn outside basic block", x);
	    }
	}

      if (JUMP_P (x)
	  && returnjump_p (x) && ! condjump_p (x)
	  && ! (NEXT_INSN (x) && BARRIER_P (NEXT_INSN (x))))
	    fatal_insn ("return not followed by barrier", x);
      if (curr_bb && x == BB_END (curr_bb))
	curr_bb = NULL;
    }

  if (num_bb_notes != n_basic_blocks - NUM_FIXED_BLOCKS)
    internal_error
      ("number of bb notes in insn chain (%d) != n_basic_blocks (%d)",
       num_bb_notes, n_basic_blocks);

   return err;
}

/* Assume that the preceding pass has possibly eliminated jump instructions
   or converted the unconditional jumps.  Eliminate the edges from CFG.
   Return true if any edges are eliminated.  */

bool
purge_dead_edges (basic_block bb)
{
  edge e;
  rtx insn = BB_END (bb), note;
  bool purged = false;
  bool found;
  edge_iterator ei;

  /* If this instruction cannot trap, remove REG_EH_REGION notes.  */
  if (NONJUMP_INSN_P (insn)
      && (note = find_reg_note (insn, REG_EH_REGION, NULL)))
    {
      rtx eqnote;

      if (! may_trap_p (PATTERN (insn))
	  || ((eqnote = find_reg_equal_equiv_note (insn))
	      && ! may_trap_p (XEXP (eqnote, 0))))
	remove_note (insn, note);
    }

  /* Cleanup abnormal edges caused by exceptions or non-local gotos.  */
  for (ei = ei_start (bb->succs); (e = ei_safe_edge (ei)); )
    {
      /* There are three types of edges we need to handle correctly here: EH
	 edges, abnormal call EH edges, and abnormal call non-EH edges.  The
	 latter can appear when nonlocal gotos are used.  */
      if (e->flags & EDGE_EH)
	{
	  if (can_throw_internal (BB_END (bb))
	      /* If this is a call edge, verify that this is a call insn.  */
	      && (! (e->flags & EDGE_ABNORMAL_CALL)
		  || CALL_P (BB_END (bb))))
	    {
	      ei_next (&ei);
	      continue;
	    }
	}
      else if (e->flags & EDGE_ABNORMAL_CALL)
	{
	  if (CALL_P (BB_END (bb))
	      && (! (note = find_reg_note (insn, REG_EH_REGION, NULL))
		  || INTVAL (XEXP (note, 0)) >= 0))
	    {
	      ei_next (&ei);
	      continue;
	    }
	}
      else
	{
	  ei_next (&ei);
	  continue;
	}

      remove_edge (e);
      bb->flags |= BB_DIRTY;
      purged = true;
    }

  if (JUMP_P (insn))
    {
      rtx note;
      edge b,f;
      edge_iterator ei;

      /* We do care only about conditional jumps and simplejumps.  */
      if (!any_condjump_p (insn)
	  && !returnjump_p (insn)
	  && !simplejump_p (insn))
	return purged;

      /* Branch probability/prediction notes are defined only for
	 condjumps.  We've possibly turned condjump into simplejump.  */
      if (simplejump_p (insn))
	{
	  note = find_reg_note (insn, REG_BR_PROB, NULL);
	  if (note)
	    remove_note (insn, note);
	  while ((note = find_reg_note (insn, REG_BR_PRED, NULL)))
	    remove_note (insn, note);
	}

      for (ei = ei_start (bb->succs); (e = ei_safe_edge (ei)); )
	{
	  /* Avoid abnormal flags to leak from computed jumps turned
	     into simplejumps.  */

	  e->flags &= ~EDGE_ABNORMAL;

	  /* See if this edge is one we should keep.  */
	  if ((e->flags & EDGE_FALLTHRU) && any_condjump_p (insn))
	    /* A conditional jump can fall through into the next
	       block, so we should keep the edge.  */
	    {
	      ei_next (&ei);
	      continue;
	    }
	  else if (e->dest != EXIT_BLOCK_PTR
		   && BB_HEAD (e->dest) == JUMP_LABEL (insn))
	    /* If the destination block is the target of the jump,
	       keep the edge.  */
	    {
	      ei_next (&ei);
	      continue;
	    }
	  else if (e->dest == EXIT_BLOCK_PTR && returnjump_p (insn))
	    /* If the destination block is the exit block, and this
	       instruction is a return, then keep the edge.  */
	    {
	      ei_next (&ei);
	      continue;
	    }
	  else if ((e->flags & EDGE_EH) && can_throw_internal (insn))
	    /* Keep the edges that correspond to exceptions thrown by
	       this instruction and rematerialize the EDGE_ABNORMAL
	       flag we just cleared above.  */
	    {
	      e->flags |= EDGE_ABNORMAL;
	      ei_next (&ei);
	      continue;
	    }

	  /* We do not need this edge.  */
	  bb->flags |= BB_DIRTY;
	  purged = true;
	  remove_edge (e);
	}

      if (EDGE_COUNT (bb->succs) == 0 || !purged)
	return purged;

      if (dump_file)
	fprintf (dump_file, "Purged edges from bb %i\n", bb->index);

      if (!optimize)
	return purged;

      /* Redistribute probabilities.  */
      if (single_succ_p (bb))
	{
	  single_succ_edge (bb)->probability = REG_BR_PROB_BASE;
	  single_succ_edge (bb)->count = bb->count;
	}
      else
	{
	  note = find_reg_note (insn, REG_BR_PROB, NULL);
	  if (!note)
	    return purged;

	  b = BRANCH_EDGE (bb);
	  f = FALLTHRU_EDGE (bb);
	  b->probability = INTVAL (XEXP (note, 0));
	  f->probability = REG_BR_PROB_BASE - b->probability;
	  b->count = bb->count * b->probability / REG_BR_PROB_BASE;
	  f->count = bb->count * f->probability / REG_BR_PROB_BASE;
	}

      return purged;
    }
  else if (CALL_P (insn) && SIBLING_CALL_P (insn))
    {
      /* First, there should not be any EH or ABCALL edges resulting
	 from non-local gotos and the like.  If there were, we shouldn't
	 have created the sibcall in the first place.  Second, there
	 should of course never have been a fallthru edge.  */
      gcc_assert (single_succ_p (bb));
      gcc_assert (single_succ_edge (bb)->flags
		  == (EDGE_SIBCALL | EDGE_ABNORMAL));

      return 0;
    }

  /* If we don't see a jump insn, we don't know exactly why the block would
     have been broken at this point.  Look for a simple, non-fallthru edge,
     as these are only created by conditional branches.  If we find such an
     edge we know that there used to be a jump here and can then safely
     remove all non-fallthru edges.  */
  found = false;
  FOR_EACH_EDGE (e, ei, bb->succs)
    if (! (e->flags & (EDGE_COMPLEX | EDGE_FALLTHRU)))
      {
	found = true;
	break;
      }

  if (!found)
    return purged;

  /* Remove all but the fake and fallthru edges.  The fake edge may be
     the only successor for this block in the case of noreturn
     calls.  */
  for (ei = ei_start (bb->succs); (e = ei_safe_edge (ei)); )
    {
      if (!(e->flags & (EDGE_FALLTHRU | EDGE_FAKE)))
	{
	  bb->flags |= BB_DIRTY;
	  remove_edge (e);
	  purged = true;
	}
      else
	ei_next (&ei);
    }

  gcc_assert (single_succ_p (bb));

  single_succ_edge (bb)->probability = REG_BR_PROB_BASE;
  single_succ_edge (bb)->count = bb->count;

  if (dump_file)
    fprintf (dump_file, "Purged non-fallthru edges from bb %i\n",
	     bb->index);
  return purged;
}

/* Search all basic blocks for potentially dead edges and purge them.  Return
   true if some edge has been eliminated.  */

bool
purge_all_dead_edges (void)
{
  int purged = false;
  basic_block bb;

  FOR_EACH_BB (bb)
    {
      bool purged_here = purge_dead_edges (bb);

      purged |= purged_here;
    }

  return purged;
}

/* Same as split_block but update cfg_layout structures.  */

static basic_block
cfg_layout_split_block (basic_block bb, void *insnp)
{
  rtx insn = insnp;
  basic_block new_bb = rtl_split_block (bb, insn);

  new_bb->il.rtl->footer = bb->il.rtl->footer;
  bb->il.rtl->footer = NULL;

  return new_bb;
}


/* Redirect Edge to DEST.  */
static edge
cfg_layout_redirect_edge_and_branch (edge e, basic_block dest)
{
  basic_block src = e->src;
  edge ret;

  if (e->flags & (EDGE_ABNORMAL_CALL | EDGE_EH))
    return NULL;

  if (e->dest == dest)
    return e;

  if (e->src != ENTRY_BLOCK_PTR
      && (ret = try_redirect_by_replacing_jump (e, dest, true)))
    {
      src->flags |= BB_DIRTY;
      return ret;
    }

  if (e->src == ENTRY_BLOCK_PTR
      && (e->flags & EDGE_FALLTHRU) && !(e->flags & EDGE_COMPLEX))
    {
      if (dump_file)
	fprintf (dump_file, "Redirecting entry edge from bb %i to %i\n",
		 e->src->index, dest->index);

      e->src->flags |= BB_DIRTY;
      redirect_edge_succ (e, dest);
      return e;
    }

  /* Redirect_edge_and_branch may decide to turn branch into fallthru edge
     in the case the basic block appears to be in sequence.  Avoid this
     transformation.  */

  if (e->flags & EDGE_FALLTHRU)
    {
      /* Redirect any branch edges unified with the fallthru one.  */
      if (JUMP_P (BB_END (src))
	  && label_is_jump_target_p (BB_HEAD (e->dest),
				     BB_END (src)))
	{
	  edge redirected;

	  if (dump_file)
	    fprintf (dump_file, "Fallthru edge unified with branch "
		     "%i->%i redirected to %i\n",
		     e->src->index, e->dest->index, dest->index);
	  e->flags &= ~EDGE_FALLTHRU;
	  redirected = redirect_branch_edge (e, dest);
	  gcc_assert (redirected);
	  e->flags |= EDGE_FALLTHRU;
	  e->src->flags |= BB_DIRTY;
	  return e;
	}
      /* In case we are redirecting fallthru edge to the branch edge
	 of conditional jump, remove it.  */
      if (EDGE_COUNT (src->succs) == 2)
	{
	  /* Find the edge that is different from E.  */
	  edge s = EDGE_SUCC (src, EDGE_SUCC (src, 0) == e);

	  if (s->dest == dest
	      && any_condjump_p (BB_END (src))
	      && onlyjump_p (BB_END (src)))
	    delete_insn (BB_END (src));
	}
      ret = redirect_edge_succ_nodup (e, dest);
      if (dump_file)
	fprintf (dump_file, "Fallthru edge %i->%i redirected to %i\n",
		 e->src->index, e->dest->index, dest->index);
    }
  else
    ret = redirect_branch_edge (e, dest);

  /* We don't want simplejumps in the insn stream during cfglayout.  */
  gcc_assert (!simplejump_p (BB_END (src)));

  src->flags |= BB_DIRTY;
  return ret;
}

/* Simple wrapper as we always can redirect fallthru edges.  */
static basic_block
cfg_layout_redirect_edge_and_branch_force (edge e, basic_block dest)
{
  edge redirected = cfg_layout_redirect_edge_and_branch (e, dest);

  gcc_assert (redirected);
  return NULL;
}

/* Same as delete_basic_block but update cfg_layout structures.  */

static void
cfg_layout_delete_block (basic_block bb)
{
  rtx insn, next, prev = PREV_INSN (BB_HEAD (bb)), *to, remaints;

  if (bb->il.rtl->header)
    {
      next = BB_HEAD (bb);
      if (prev)
	NEXT_INSN (prev) = bb->il.rtl->header;
      else
	set_first_insn (bb->il.rtl->header);
      PREV_INSN (bb->il.rtl->header) = prev;
      insn = bb->il.rtl->header;
      while (NEXT_INSN (insn))
	insn = NEXT_INSN (insn);
      NEXT_INSN (insn) = next;
      PREV_INSN (next) = insn;
    }
  next = NEXT_INSN (BB_END (bb));
  if (bb->il.rtl->footer)
    {
      insn = bb->il.rtl->footer;
      while (insn)
	{
	  if (BARRIER_P (insn))
	    {
	      if (PREV_INSN (insn))
		NEXT_INSN (PREV_INSN (insn)) = NEXT_INSN (insn);
	      else
		bb->il.rtl->footer = NEXT_INSN (insn);
	      if (NEXT_INSN (insn))
		PREV_INSN (NEXT_INSN (insn)) = PREV_INSN (insn);
	    }
	  if (LABEL_P (insn))
	    break;
	  insn = NEXT_INSN (insn);
	}
      if (bb->il.rtl->footer)
	{
	  insn = BB_END (bb);
	  NEXT_INSN (insn) = bb->il.rtl->footer;
	  PREV_INSN (bb->il.rtl->footer) = insn;
	  while (NEXT_INSN (insn))
	    insn = NEXT_INSN (insn);
	  NEXT_INSN (insn) = next;
	  if (next)
	    PREV_INSN (next) = insn;
	  else
	    set_last_insn (insn);
	}
    }
  if (bb->next_bb != EXIT_BLOCK_PTR)
    to = &bb->next_bb->il.rtl->header;
  else
    to = &cfg_layout_function_footer;

  rtl_delete_block (bb);

  if (prev)
    prev = NEXT_INSN (prev);
  else
    prev = get_insns ();
  if (next)
    next = PREV_INSN (next);
  else
    next = get_last_insn ();

  if (next && NEXT_INSN (next) != prev)
    {
      remaints = unlink_insn_chain (prev, next);
      insn = remaints;
      while (NEXT_INSN (insn))
	insn = NEXT_INSN (insn);
      NEXT_INSN (insn) = *to;
      if (*to)
	PREV_INSN (*to) = insn;
      *to = remaints;
    }
}

/* Return true when blocks A and B can be safely merged.  */
static bool
cfg_layout_can_merge_blocks_p (basic_block a, basic_block b)
{
  /* If we are partitioning hot/cold basic blocks, we don't want to
     mess up unconditional or indirect jumps that cross between hot
     and cold sections.

     Basic block partitioning may result in some jumps that appear to
     be optimizable (or blocks that appear to be mergeable), but which really
     must be left untouched (they are required to make it safely across
     partition boundaries).  See  the comments at the top of
     bb-reorder.c:partition_hot_cold_basic_blocks for complete details.  */

  if (BB_PARTITION (a) != BB_PARTITION (b))
    return false;

  /* There must be exactly one edge in between the blocks.  */
  return (single_succ_p (a)
	  && single_succ (a) == b
	  && single_pred_p (b) == 1
	  && a != b
	  /* Must be simple edge.  */
	  && !(single_succ_edge (a)->flags & EDGE_COMPLEX)
	  && a != ENTRY_BLOCK_PTR && b != EXIT_BLOCK_PTR
	  /* If the jump insn has side effects,
	     we can't kill the edge.  */
	  && (!JUMP_P (BB_END (a))
	      || (reload_completed
		  ? simplejump_p (BB_END (a)) : onlyjump_p (BB_END (a)))));
}

/* Merge block A and B.  The blocks must be mergeable.  */

static void
cfg_layout_merge_blocks (basic_block a, basic_block b)
{
#ifdef ENABLE_CHECKING
  gcc_assert (cfg_layout_can_merge_blocks_p (a, b));
#endif

  /* If there was a CODE_LABEL beginning B, delete it.  */
  if (LABEL_P (BB_HEAD (b)))
    {
      /* This might have been an EH label that no longer has incoming
	 EH edges.  Update data structures to match.  */
      maybe_remove_eh_handler (BB_HEAD (b));

      delete_insn (BB_HEAD (b));
    }

  /* We should have fallthru edge in a, or we can do dummy redirection to get
     it cleaned up.  */
  if (JUMP_P (BB_END (a)))
    try_redirect_by_replacing_jump (EDGE_SUCC (a, 0), b, true);
  gcc_assert (!JUMP_P (BB_END (a)));

  /* Possible line number notes should appear in between.  */
  if (b->il.rtl->header)
    {
      rtx first = BB_END (a), last;

      last = emit_insn_after_noloc (b->il.rtl->header, BB_END (a));
      delete_insn_chain (NEXT_INSN (first), last);
      b->il.rtl->header = NULL;
    }

  /* In the case basic blocks are not adjacent, move them around.  */
  if (NEXT_INSN (BB_END (a)) != BB_HEAD (b))
    {
      rtx first = unlink_insn_chain (BB_HEAD (b), BB_END (b));

      emit_insn_after_noloc (first, BB_END (a));
      /* Skip possible DELETED_LABEL insn.  */
      if (!NOTE_INSN_BASIC_BLOCK_P (first))
	first = NEXT_INSN (first);
      gcc_assert (NOTE_INSN_BASIC_BLOCK_P (first));
      BB_HEAD (b) = NULL;
      delete_insn (first);
    }
  /* Otherwise just re-associate the instructions.  */
  else
    {
      rtx insn;

      for (insn = BB_HEAD (b);
	   insn != NEXT_INSN (BB_END (b));
	   insn = NEXT_INSN (insn))
	set_block_for_insn (insn, a);
      insn = BB_HEAD (b);
      /* Skip possible DELETED_LABEL insn.  */
      if (!NOTE_INSN_BASIC_BLOCK_P (insn))
	insn = NEXT_INSN (insn);
      gcc_assert (NOTE_INSN_BASIC_BLOCK_P (insn));
      BB_HEAD (b) = NULL;
      BB_END (a) = BB_END (b);
      delete_insn (insn);
    }

  /* Possible tablejumps and barriers should appear after the block.  */
  if (b->il.rtl->footer)
    {
      if (!a->il.rtl->footer)
	a->il.rtl->footer = b->il.rtl->footer;
      else
	{
	  rtx last = a->il.rtl->footer;

	  while (NEXT_INSN (last))
	    last = NEXT_INSN (last);
	  NEXT_INSN (last) = b->il.rtl->footer;
	  PREV_INSN (b->il.rtl->footer) = last;
	}
      b->il.rtl->footer = NULL;
    }
  a->il.rtl->global_live_at_end = b->il.rtl->global_live_at_end;

  if (dump_file)
    fprintf (dump_file, "Merged blocks %d and %d.\n",
	     a->index, b->index);
}

/* Split edge E.  */

static basic_block
cfg_layout_split_edge (edge e)
{
  basic_block new_bb =
    create_basic_block (e->src != ENTRY_BLOCK_PTR
			? NEXT_INSN (BB_END (e->src)) : get_insns (),
			NULL_RTX, e->src);

  /* ??? This info is likely going to be out of date very soon, but we must
     create it to avoid getting an ICE later.  */
  if (e->dest->il.rtl->global_live_at_start)
    {
      new_bb->il.rtl->global_live_at_start = ALLOC_REG_SET (&reg_obstack);
      new_bb->il.rtl->global_live_at_end = ALLOC_REG_SET (&reg_obstack);
      COPY_REG_SET (new_bb->il.rtl->global_live_at_start,
		    e->dest->il.rtl->global_live_at_start);
      COPY_REG_SET (new_bb->il.rtl->global_live_at_end,
		    e->dest->il.rtl->global_live_at_start);
    }

  make_edge (new_bb, e->dest, EDGE_FALLTHRU);
  redirect_edge_and_branch_force (e, new_bb);

  return new_bb;
}

/* Do postprocessing after making a forwarder block joined by edge FALLTHRU.  */

static void
rtl_make_forwarder_block (edge fallthru ATTRIBUTE_UNUSED)
{
}

/* Return 1 if BB ends with a call, possibly followed by some
   instructions that must stay with the call, 0 otherwise.  */

static bool
rtl_block_ends_with_call_p (basic_block bb)
{
  rtx insn = BB_END (bb);

  while (!CALL_P (insn)
	 && insn != BB_HEAD (bb)
	 && keep_with_call_p (insn))
    insn = PREV_INSN (insn);
  return (CALL_P (insn));
}

/* Return 1 if BB ends with a conditional branch, 0 otherwise.  */

static bool
rtl_block_ends_with_condjump_p (basic_block bb)
{
  return any_condjump_p (BB_END (bb));
}

/* Return true if we need to add fake edge to exit.
   Helper function for rtl_flow_call_edges_add.  */

static bool
need_fake_edge_p (rtx insn)
{
  if (!INSN_P (insn))
    return false;

  if ((CALL_P (insn)
       && !SIBLING_CALL_P (insn)
       && !find_reg_note (insn, REG_NORETURN, NULL)
       && !CONST_OR_PURE_CALL_P (insn)))
    return true;

  return ((GET_CODE (PATTERN (insn)) == ASM_OPERANDS
	   && MEM_VOLATILE_P (PATTERN (insn)))
	  || (GET_CODE (PATTERN (insn)) == PARALLEL
	      && asm_noperands (insn) != -1
	      && MEM_VOLATILE_P (XVECEXP (PATTERN (insn), 0, 0)))
	  || GET_CODE (PATTERN (insn)) == ASM_INPUT);
}

/* Add fake edges to the function exit for any non constant and non noreturn
   calls, volatile inline assembly in the bitmap of blocks specified by
   BLOCKS or to the whole CFG if BLOCKS is zero.  Return the number of blocks
   that were split.

   The goal is to expose cases in which entering a basic block does not imply
   that all subsequent instructions must be executed.  */

static int
rtl_flow_call_edges_add (sbitmap blocks)
{
  int i;
  int blocks_split = 0;
  int last_bb = last_basic_block;
  bool check_last_block = false;

  if (n_basic_blocks == NUM_FIXED_BLOCKS)
    return 0;

  if (! blocks)
    check_last_block = true;
  else
    check_last_block = TEST_BIT (blocks, EXIT_BLOCK_PTR->prev_bb->index);

  /* In the last basic block, before epilogue generation, there will be
     a fallthru edge to EXIT.  Special care is required if the last insn
     of the last basic block is a call because make_edge folds duplicate
     edges, which would result in the fallthru edge also being marked
     fake, which would result in the fallthru edge being removed by
     remove_fake_edges, which would result in an invalid CFG.

     Moreover, we can't elide the outgoing fake edge, since the block
     profiler needs to take this into account in order to solve the minimal
     spanning tree in the case that the call doesn't return.

     Handle this by adding a dummy instruction in a new last basic block.  */
  if (check_last_block)
    {
      basic_block bb = EXIT_BLOCK_PTR->prev_bb;
      rtx insn = BB_END (bb);

      /* Back up past insns that must be kept in the same block as a call.  */
      while (insn != BB_HEAD (bb)
	     && keep_with_call_p (insn))
	insn = PREV_INSN (insn);

      if (need_fake_edge_p (insn))
	{
	  edge e;

	  e = find_edge (bb, EXIT_BLOCK_PTR);
	  if (e)
	    {
	      insert_insn_on_edge (gen_rtx_USE (VOIDmode, const0_rtx), e);
	      commit_edge_insertions ();
	    }
	}
    }

  /* Now add fake edges to the function exit for any non constant
     calls since there is no way that we can determine if they will
     return or not...  */

  for (i = NUM_FIXED_BLOCKS; i < last_bb; i++)
    {
      basic_block bb = BASIC_BLOCK (i);
      rtx insn;
      rtx prev_insn;

      if (!bb)
	continue;

      if (blocks && !TEST_BIT (blocks, i))
	continue;

      for (insn = BB_END (bb); ; insn = prev_insn)
	{
	  prev_insn = PREV_INSN (insn);
	  if (need_fake_edge_p (insn))
	    {
	      edge e;
	      rtx split_at_insn = insn;

	      /* Don't split the block between a call and an insn that should
		 remain in the same block as the call.  */
	      if (CALL_P (insn))
		while (split_at_insn != BB_END (bb)
		       && keep_with_call_p (NEXT_INSN (split_at_insn)))
		  split_at_insn = NEXT_INSN (split_at_insn);

	      /* The handling above of the final block before the epilogue
		 should be enough to verify that there is no edge to the exit
		 block in CFG already.  Calling make_edge in such case would
		 cause us to mark that edge as fake and remove it later.  */

#ifdef ENABLE_CHECKING
	      if (split_at_insn == BB_END (bb))
		{
		  e = find_edge (bb, EXIT_BLOCK_PTR);
		  gcc_assert (e == NULL);
		}
#endif

	      /* Note that the following may create a new basic block
		 and renumber the existing basic blocks.  */
	      if (split_at_insn != BB_END (bb))
		{
		  e = split_block (bb, split_at_insn);
		  if (e)
		    blocks_split++;
		}

	      make_edge (bb, EXIT_BLOCK_PTR, EDGE_FAKE);
	    }

	  if (insn == BB_HEAD (bb))
	    break;
	}
    }

  if (blocks_split)
    verify_flow_info ();

  return blocks_split;
}

/* Add COMP_RTX as a condition at end of COND_BB.  FIRST_HEAD is
   the conditional branch target, SECOND_HEAD should be the fall-thru
   there is no need to handle this here the loop versioning code handles
   this.  the reason for SECON_HEAD is that it is needed for condition
   in trees, and this should be of the same type since it is a hook.  */
static void
rtl_lv_add_condition_to_bb (basic_block first_head ,
			    basic_block second_head ATTRIBUTE_UNUSED,
			    basic_block cond_bb, void *comp_rtx)
{
  rtx label, seq, jump;
  rtx op0 = XEXP ((rtx)comp_rtx, 0);
  rtx op1 = XEXP ((rtx)comp_rtx, 1);
  enum rtx_code comp = GET_CODE ((rtx)comp_rtx);
  enum machine_mode mode;


  label = block_label (first_head);
  mode = GET_MODE (op0);
  if (mode == VOIDmode)
    mode = GET_MODE (op1);

  start_sequence ();
  op0 = force_operand (op0, NULL_RTX);
  op1 = force_operand (op1, NULL_RTX);
  do_compare_rtx_and_jump (op0, op1, comp, 0,
			   mode, NULL_RTX, NULL_RTX, label);
  jump = get_last_insn ();
  JUMP_LABEL (jump) = label;
  LABEL_NUSES (label)++;
  seq = get_insns ();
  end_sequence ();

  /* Add the new cond , in the new head.  */
  emit_insn_after(seq, BB_END(cond_bb));
}


/* Given a block B with unconditional branch at its end, get the
   store the return the branch edge and the fall-thru edge in
   BRANCH_EDGE and FALLTHRU_EDGE respectively.  */
static void
rtl_extract_cond_bb_edges (basic_block b, edge *branch_edge,
			   edge *fallthru_edge)
{
  edge e = EDGE_SUCC (b, 0);

  if (e->flags & EDGE_FALLTHRU)
    {
      *fallthru_edge = e;
      *branch_edge = EDGE_SUCC (b, 1);
    }
  else
    {
      *branch_edge = e;
      *fallthru_edge = EDGE_SUCC (b, 1);
    }
}

void
init_rtl_bb_info (basic_block bb)
{
  gcc_assert (!bb->il.rtl);
  bb->il.rtl = ggc_alloc_cleared (sizeof (struct rtl_bb_info));
}


/* Add EXPR to the end of basic block BB.  */

rtx
insert_insn_end_bb_new (rtx pat, basic_block bb)
{
  rtx insn = BB_END (bb);
  rtx new_insn;
  rtx pat_end = pat;

  while (NEXT_INSN (pat_end) != NULL_RTX)
    pat_end = NEXT_INSN (pat_end);

  /* If the last insn is a jump, insert EXPR in front [taking care to
     handle cc0, etc. properly].  Similarly we need to care trapping
     instructions in presence of non-call exceptions.  */

  if (JUMP_P (insn)
      || (NONJUMP_INSN_P (insn)
          && (!single_succ_p (bb)
              || single_succ_edge (bb)->flags & EDGE_ABNORMAL)))
    {
#ifdef HAVE_cc0
      rtx note;
#endif
      /* If this is a jump table, then we can't insert stuff here.  Since
         we know the previous real insn must be the tablejump, we insert
         the new instruction just before the tablejump.  */
      if (GET_CODE (PATTERN (insn)) == ADDR_VEC
          || GET_CODE (PATTERN (insn)) == ADDR_DIFF_VEC)
        insn = prev_real_insn (insn);

#ifdef HAVE_cc0
      /* FIXME: 'twould be nice to call prev_cc0_setter here but it aborts
         if cc0 isn't set.  */
      note = find_reg_note (insn, REG_CC_SETTER, NULL_RTX);
      if (note)
        insn = XEXP (note, 0);
      else
        {
          rtx maybe_cc0_setter = prev_nonnote_insn (insn);
          if (maybe_cc0_setter
              && INSN_P (maybe_cc0_setter)
              && sets_cc0_p (PATTERN (maybe_cc0_setter)))
            insn = maybe_cc0_setter;
        }
#endif
      /* FIXME: What if something in cc0/jump uses value set in new
         insn?  */
      new_insn = emit_insn_before_noloc (pat, insn);
    }

  /* Likewise if the last insn is a call, as will happen in the presence
     of exception handling.  */
  else if (CALL_P (insn)
           && (!single_succ_p (bb)
               || single_succ_edge (bb)->flags & EDGE_ABNORMAL))
    {
      /* Keeping in mind SMALL_REGISTER_CLASSES and parameters in registers,
         we search backward and place the instructions before the first
         parameter is loaded.  Do this for everyone for consistency and a
         presumption that we'll get better code elsewhere as well.  */

      /* Since different machines initialize their parameter registers
         in different orders, assume nothing.  Collect the set of all
         parameter registers.  */
      insn = find_first_parameter_load (insn, BB_HEAD (bb));

      /* If we found all the parameter loads, then we want to insert
         before the first parameter load.

         If we did not find all the parameter loads, then we might have
         stopped on the head of the block, which could be a CODE_LABEL.
         If we inserted before the CODE_LABEL, then we would be putting
         the insn in the wrong basic block.  In that case, put the insn
         after the CODE_LABEL.  Also, respect NOTE_INSN_BASIC_BLOCK.  */
      while (LABEL_P (insn)
             || NOTE_INSN_BASIC_BLOCK_P (insn))
        insn = NEXT_INSN (insn);

      new_insn = emit_insn_before_noloc (pat, insn);
    }
  else
    new_insn = emit_insn_after_noloc (pat, insn);

  return new_insn;
}

/* Implementation of CFG manipulation for linearized RTL.  */
struct cfg_hooks rtl_cfg_hooks = {
  "rtl",
  rtl_verify_flow_info,
  rtl_dump_bb,
  rtl_create_basic_block,
  rtl_redirect_edge_and_branch,
  rtl_redirect_edge_and_branch_force,
  rtl_delete_block,
  rtl_split_block,
  rtl_move_block_after,
  rtl_can_merge_blocks,  /* can_merge_blocks_p */
  rtl_merge_blocks,
  rtl_predict_edge,
  rtl_predicted_by_p,
  NULL, /* can_duplicate_block_p */
  NULL, /* duplicate_block */
  rtl_split_edge,
  rtl_make_forwarder_block,
  rtl_tidy_fallthru_edge,
  rtl_block_ends_with_call_p,
  rtl_block_ends_with_condjump_p,
  rtl_flow_call_edges_add,
  NULL, /* execute_on_growing_pred */
  NULL, /* execute_on_shrinking_pred */
  NULL, /* duplicate loop for trees */
  NULL, /* lv_add_condition_to_bb */
  NULL, /* lv_adjust_loop_header_phi*/
  NULL, /* extract_cond_bb_edges */
  NULL		/* flush_pending_stmts */
};

/* Implementation of CFG manipulation for cfg layout RTL, where
   basic block connected via fallthru edges does not have to be adjacent.
   This representation will hopefully become the default one in future
   version of the compiler.  */

/* We do not want to declare these functions in a header file, since they
   should only be used through the cfghooks interface, and we do not want to
   move them here since it would require also moving quite a lot of related
   code.  */
extern bool cfg_layout_can_duplicate_bb_p (basic_block);
extern basic_block cfg_layout_duplicate_bb (basic_block);

struct cfg_hooks cfg_layout_rtl_cfg_hooks = {
  "cfglayout mode",
  rtl_verify_flow_info_1,
  rtl_dump_bb,
  cfg_layout_create_basic_block,
  cfg_layout_redirect_edge_and_branch,
  cfg_layout_redirect_edge_and_branch_force,
  cfg_layout_delete_block,
  cfg_layout_split_block,
  rtl_move_block_after,
  cfg_layout_can_merge_blocks_p,
  cfg_layout_merge_blocks,
  rtl_predict_edge,
  rtl_predicted_by_p,
  cfg_layout_can_duplicate_bb_p,
  cfg_layout_duplicate_bb,
  cfg_layout_split_edge,
  rtl_make_forwarder_block,
  NULL,
  rtl_block_ends_with_call_p,
  rtl_block_ends_with_condjump_p,
  rtl_flow_call_edges_add,
  NULL, /* execute_on_growing_pred */
  NULL, /* execute_on_shrinking_pred */
  duplicate_loop_to_header_edge, /* duplicate loop for trees */
  rtl_lv_add_condition_to_bb, /* lv_add_condition_to_bb */
  NULL, /* lv_adjust_loop_header_phi*/
  rtl_extract_cond_bb_edges, /* extract_cond_bb_edges */
  NULL		/* flush_pending_stmts */
};
