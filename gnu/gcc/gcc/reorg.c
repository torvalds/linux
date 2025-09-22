/* Perform instruction reorganizations for delay slot filling.
   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Richard Kenner (kenner@vlsi1.ultra.nyu.edu).
   Hacked by Michael Tiemann (tiemann@cygnus.com).

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

/* Instruction reorganization pass.

   This pass runs after register allocation and final jump
   optimization.  It should be the last pass to run before peephole.
   It serves primarily to fill delay slots of insns, typically branch
   and call insns.  Other insns typically involve more complicated
   interactions of data dependencies and resource constraints, and
   are better handled by scheduling before register allocation (by the
   function `schedule_insns').

   The Branch Penalty is the number of extra cycles that are needed to
   execute a branch insn.  On an ideal machine, branches take a single
   cycle, and the Branch Penalty is 0.  Several RISC machines approach
   branch delays differently:

   The MIPS has a single branch delay slot.  Most insns
   (except other branches) can be used to fill this slot.  When the
   slot is filled, two insns execute in two cycles, reducing the
   branch penalty to zero.

   The SPARC always has a branch delay slot, but its effects can be
   annulled when the branch is not taken.  This means that failing to
   find other sources of insns, we can hoist an insn from the branch
   target that would only be safe to execute knowing that the branch
   is taken.

   The HP-PA always has a branch delay slot.  For unconditional branches
   its effects can be annulled when the branch is taken.  The effects
   of the delay slot in a conditional branch can be nullified for forward
   taken branches, or for untaken backward branches.  This means
   we can hoist insns from the fall-through path for forward branches or
   steal insns from the target of backward branches.

   The TMS320C3x and C4x have three branch delay slots.  When the three
   slots are filled, the branch penalty is zero.  Most insns can fill the
   delay slots except jump insns.

   Three techniques for filling delay slots have been implemented so far:

   (1) `fill_simple_delay_slots' is the simplest, most efficient way
   to fill delay slots.  This pass first looks for insns which come
   from before the branch and which are safe to execute after the
   branch.  Then it searches after the insn requiring delay slots or,
   in the case of a branch, for insns that are after the point at
   which the branch merges into the fallthrough code, if such a point
   exists.  When such insns are found, the branch penalty decreases
   and no code expansion takes place.

   (2) `fill_eager_delay_slots' is more complicated: it is used for
   scheduling conditional jumps, or for scheduling jumps which cannot
   be filled using (1).  A machine need not have annulled jumps to use
   this strategy, but it helps (by keeping more options open).
   `fill_eager_delay_slots' tries to guess the direction the branch
   will go; if it guesses right 100% of the time, it can reduce the
   branch penalty as much as `fill_simple_delay_slots' does.  If it
   guesses wrong 100% of the time, it might as well schedule nops.  When
   `fill_eager_delay_slots' takes insns from the fall-through path of
   the jump, usually there is no code expansion; when it takes insns
   from the branch target, there is code expansion if it is not the
   only way to reach that target.

   (3) `relax_delay_slots' uses a set of rules to simplify code that
   has been reorganized by (1) and (2).  It finds cases where
   conditional test can be eliminated, jumps can be threaded, extra
   insns can be eliminated, etc.  It is the job of (1) and (2) to do a
   good job of scheduling locally; `relax_delay_slots' takes care of
   making the various individual schedules work well together.  It is
   especially tuned to handle the control flow interactions of branch
   insns.  It does nothing for insns with delay slots that do not
   branch.

   On machines that use CC0, we are very conservative.  We will not make
   a copy of an insn involving CC0 since we want to maintain a 1-1
   correspondence between the insn that sets and uses CC0.  The insns are
   allowed to be separated by placing an insn that sets CC0 (but not an insn
   that uses CC0; we could do this, but it doesn't seem worthwhile) in a
   delay slot.  In that case, we point each insn at the other with REG_CC_USER
   and REG_CC_SETTER notes.  Note that these restrictions affect very few
   machines because most RISC machines with delay slots will not use CC0
   (the RT is the only known exception at this point).

   Not yet implemented:

   The Acorn Risc Machine can conditionally execute most insns, so
   it is profitable to move single insns into a position to execute
   based on the condition code of the previous insn.

   The HP-PA can conditionally nullify insns, providing a similar
   effect to the ARM, differing mostly in which insn is "in charge".  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "toplev.h"
#include "rtl.h"
#include "tm_p.h"
#include "expr.h"
#include "function.h"
#include "insn-config.h"
#include "conditions.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "regs.h"
#include "recog.h"
#include "flags.h"
#include "output.h"
#include "obstack.h"
#include "insn-attr.h"
#include "resource.h"
#include "except.h"
#include "params.h"
#include "timevar.h"
#include "target.h"
#include "tree-pass.h"

#ifdef DELAY_SLOTS

#ifndef ANNUL_IFTRUE_SLOTS
#define eligible_for_annul_true(INSN, SLOTS, TRIAL, FLAGS) 0
#endif
#ifndef ANNUL_IFFALSE_SLOTS
#define eligible_for_annul_false(INSN, SLOTS, TRIAL, FLAGS) 0
#endif

/* Insns which have delay slots that have not yet been filled.  */

static struct obstack unfilled_slots_obstack;
static rtx *unfilled_firstobj;

/* Define macros to refer to the first and last slot containing unfilled
   insns.  These are used because the list may move and its address
   should be recomputed at each use.  */

#define unfilled_slots_base	\
  ((rtx *) obstack_base (&unfilled_slots_obstack))

#define unfilled_slots_next	\
  ((rtx *) obstack_next_free (&unfilled_slots_obstack))

/* Points to the label before the end of the function.  */
static rtx end_of_function_label;

/* Mapping between INSN_UID's and position in the code since INSN_UID's do
   not always monotonically increase.  */
static int *uid_to_ruid;

/* Highest valid index in `uid_to_ruid'.  */
static int max_uid;

static int stop_search_p (rtx, int);
static int resource_conflicts_p (struct resources *, struct resources *);
static int insn_references_resource_p (rtx, struct resources *, int);
static int insn_sets_resource_p (rtx, struct resources *, int);
static rtx find_end_label (void);
static rtx emit_delay_sequence (rtx, rtx, int);
static rtx add_to_delay_list (rtx, rtx);
static rtx delete_from_delay_slot (rtx);
static void delete_scheduled_jump (rtx);
static void note_delay_statistics (int, int);
#if defined(ANNUL_IFFALSE_SLOTS) || defined(ANNUL_IFTRUE_SLOTS)
static rtx optimize_skip (rtx);
#endif
static int get_jump_flags (rtx, rtx);
static int rare_destination (rtx);
static int mostly_true_jump (rtx, rtx);
static rtx get_branch_condition (rtx, rtx);
static int condition_dominates_p (rtx, rtx);
static int redirect_with_delay_slots_safe_p (rtx, rtx, rtx);
static int redirect_with_delay_list_safe_p (rtx, rtx, rtx);
static int check_annul_list_true_false (int, rtx);
static rtx steal_delay_list_from_target (rtx, rtx, rtx, rtx,
					 struct resources *,
					 struct resources *,
					 struct resources *,
					 int, int *, int *, rtx *);
static rtx steal_delay_list_from_fallthrough (rtx, rtx, rtx, rtx,
					      struct resources *,
					      struct resources *,
					      struct resources *,
					      int, int *, int *);
static void try_merge_delay_insns (rtx, rtx);
static rtx redundant_insn (rtx, rtx, rtx);
static int own_thread_p (rtx, rtx, int);
static void update_block (rtx, rtx);
static int reorg_redirect_jump (rtx, rtx);
static void update_reg_dead_notes (rtx, rtx);
static void fix_reg_dead_note (rtx, rtx);
static void update_reg_unused_notes (rtx, rtx);
static void fill_simple_delay_slots (int);
static rtx fill_slots_from_thread (rtx, rtx, rtx, rtx, int, int, int, int,
				   int *, rtx);
static void fill_eager_delay_slots (void);
static void relax_delay_slots (rtx);
#ifdef HAVE_return
static void make_return_insns (rtx);
#endif

/* Return TRUE if this insn should stop the search for insn to fill delay
   slots.  LABELS_P indicates that labels should terminate the search.
   In all cases, jumps terminate the search.  */

static int
stop_search_p (rtx insn, int labels_p)
{
  if (insn == 0)
    return 1;

  /* If the insn can throw an exception that is caught within the function,
     it may effectively perform a jump from the viewpoint of the function.
     Therefore act like for a jump.  */
  if (can_throw_internal (insn))
    return 1;

  switch (GET_CODE (insn))
    {
    case NOTE:
    case CALL_INSN:
      return 0;

    case CODE_LABEL:
      return labels_p;

    case JUMP_INSN:
    case BARRIER:
      return 1;

    case INSN:
      /* OK unless it contains a delay slot or is an `asm' insn of some type.
	 We don't know anything about these.  */
      return (GET_CODE (PATTERN (insn)) == SEQUENCE
	      || GET_CODE (PATTERN (insn)) == ASM_INPUT
	      || asm_noperands (PATTERN (insn)) >= 0);

    default:
      gcc_unreachable ();
    }
}

/* Return TRUE if any resources are marked in both RES1 and RES2 or if either
   resource set contains a volatile memory reference.  Otherwise, return FALSE.  */

static int
resource_conflicts_p (struct resources *res1, struct resources *res2)
{
  if ((res1->cc && res2->cc) || (res1->memory && res2->memory)
      || (res1->unch_memory && res2->unch_memory)
      || res1->volatil || res2->volatil)
    return 1;

#ifdef HARD_REG_SET
  return (res1->regs & res2->regs) != HARD_CONST (0);
#else
  {
    int i;

    for (i = 0; i < HARD_REG_SET_LONGS; i++)
      if ((res1->regs[i] & res2->regs[i]) != 0)
	return 1;
    return 0;
  }
#endif
}

/* Return TRUE if any resource marked in RES, a `struct resources', is
   referenced by INSN.  If INCLUDE_DELAYED_EFFECTS is set, return if the called
   routine is using those resources.

   We compute this by computing all the resources referenced by INSN and
   seeing if this conflicts with RES.  It might be faster to directly check
   ourselves, and this is the way it used to work, but it means duplicating
   a large block of complex code.  */

static int
insn_references_resource_p (rtx insn, struct resources *res,
			    int include_delayed_effects)
{
  struct resources insn_res;

  CLEAR_RESOURCE (&insn_res);
  mark_referenced_resources (insn, &insn_res, include_delayed_effects);
  return resource_conflicts_p (&insn_res, res);
}

/* Return TRUE if INSN modifies resources that are marked in RES.
   INCLUDE_DELAYED_EFFECTS is set if the actions of that routine should be
   included.   CC0 is only modified if it is explicitly set; see comments
   in front of mark_set_resources for details.  */

static int
insn_sets_resource_p (rtx insn, struct resources *res,
		      int include_delayed_effects)
{
  struct resources insn_sets;

  CLEAR_RESOURCE (&insn_sets);
  mark_set_resources (insn, &insn_sets, 0, include_delayed_effects);
  return resource_conflicts_p (&insn_sets, res);
}

/* Find a label at the end of the function or before a RETURN.  If there
   is none, try to make one.  If that fails, returns 0.

   The property of such a label is that it is placed just before the
   epilogue or a bare RETURN insn, so that another bare RETURN can be
   turned into a jump to the label unconditionally.  In particular, the
   label cannot be placed before a RETURN insn with a filled delay slot.

   ??? There may be a problem with the current implementation.  Suppose
   we start with a bare RETURN insn and call find_end_label.  It may set
   end_of_function_label just before the RETURN.  Suppose the machinery
   is able to fill the delay slot of the RETURN insn afterwards.  Then
   end_of_function_label is no longer valid according to the property
   described above and find_end_label will still return it unmodified.
   Note that this is probably mitigated by the following observation:
   once end_of_function_label is made, it is very likely the target of
   a jump, so filling the delay slot of the RETURN will be much more
   difficult.  */

static rtx
find_end_label (void)
{
  rtx insn;

  /* If we found one previously, return it.  */
  if (end_of_function_label)
    return end_of_function_label;

  /* Otherwise, see if there is a label at the end of the function.  If there
     is, it must be that RETURN insns aren't needed, so that is our return
     label and we don't have to do anything else.  */

  insn = get_last_insn ();
  while (NOTE_P (insn)
	 || (NONJUMP_INSN_P (insn)
	     && (GET_CODE (PATTERN (insn)) == USE
		 || GET_CODE (PATTERN (insn)) == CLOBBER)))
    insn = PREV_INSN (insn);

  /* When a target threads its epilogue we might already have a
     suitable return insn.  If so put a label before it for the
     end_of_function_label.  */
  if (BARRIER_P (insn)
      && JUMP_P (PREV_INSN (insn))
      && GET_CODE (PATTERN (PREV_INSN (insn))) == RETURN)
    {
      rtx temp = PREV_INSN (PREV_INSN (insn));
      end_of_function_label = gen_label_rtx ();
      LABEL_NUSES (end_of_function_label) = 0;

      /* Put the label before an USE insns that may precede the RETURN insn.  */
      while (GET_CODE (temp) == USE)
	temp = PREV_INSN (temp);

      emit_label_after (end_of_function_label, temp);
    }

  else if (LABEL_P (insn))
    end_of_function_label = insn;
  else
    {
      end_of_function_label = gen_label_rtx ();
      LABEL_NUSES (end_of_function_label) = 0;
      /* If the basic block reorder pass moves the return insn to
	 some other place try to locate it again and put our
	 end_of_function_label there.  */
      while (insn && ! (JUMP_P (insn)
		        && (GET_CODE (PATTERN (insn)) == RETURN)))
	insn = PREV_INSN (insn);
      if (insn)
	{
	  insn = PREV_INSN (insn);

	  /* Put the label before an USE insns that may proceed the
	     RETURN insn.  */
	  while (GET_CODE (insn) == USE)
	    insn = PREV_INSN (insn);

	  emit_label_after (end_of_function_label, insn);
	}
      else
	{
#ifdef HAVE_epilogue
	  if (HAVE_epilogue
#ifdef HAVE_return
	      && ! HAVE_return
#endif
	      )
	    {
	      /* The RETURN insn has its delay slot filled so we cannot
		 emit the label just before it.  Since we already have
		 an epilogue and cannot emit a new RETURN, we cannot
		 emit the label at all.  */
	      end_of_function_label = NULL_RTX;
	      return end_of_function_label;
	    }
#endif /* HAVE_epilogue */

	  /* Otherwise, make a new label and emit a RETURN and BARRIER,
	     if needed.  */
	  emit_label (end_of_function_label);
#ifdef HAVE_return
	  /* We don't bother trying to create a return insn if the
	     epilogue has filled delay-slots; we would have to try and
	     move the delay-slot fillers to the delay-slots for the new
	     return insn or in front of the new return insn.  */
	  if (current_function_epilogue_delay_list == NULL
	      && HAVE_return)
	    {
	      /* The return we make may have delay slots too.  */
	      rtx insn = gen_return ();
	      insn = emit_jump_insn (insn);
	      emit_barrier ();
	      if (num_delay_slots (insn) > 0)
		obstack_ptr_grow (&unfilled_slots_obstack, insn);
	    }
#endif
	}
    }

  /* Show one additional use for this label so it won't go away until
     we are done.  */
  ++LABEL_NUSES (end_of_function_label);

  return end_of_function_label;
}

/* Put INSN and LIST together in a SEQUENCE rtx of LENGTH, and replace
   the pattern of INSN with the SEQUENCE.

   Chain the insns so that NEXT_INSN of each insn in the sequence points to
   the next and NEXT_INSN of the last insn in the sequence points to
   the first insn after the sequence.  Similarly for PREV_INSN.  This makes
   it easier to scan all insns.

   Returns the SEQUENCE that replaces INSN.  */

static rtx
emit_delay_sequence (rtx insn, rtx list, int length)
{
  int i = 1;
  rtx li;
  int had_barrier = 0;

  /* Allocate the rtvec to hold the insns and the SEQUENCE.  */
  rtvec seqv = rtvec_alloc (length + 1);
  rtx seq = gen_rtx_SEQUENCE (VOIDmode, seqv);
  rtx seq_insn = make_insn_raw (seq);
  rtx first = get_insns ();
  rtx last = get_last_insn ();

  /* Make a copy of the insn having delay slots.  */
  rtx delay_insn = copy_rtx (insn);

  /* If INSN is followed by a BARRIER, delete the BARRIER since it will only
     confuse further processing.  Update LAST in case it was the last insn.
     We will put the BARRIER back in later.  */
  if (NEXT_INSN (insn) && BARRIER_P (NEXT_INSN (insn)))
    {
      delete_related_insns (NEXT_INSN (insn));
      last = get_last_insn ();
      had_barrier = 1;
    }

  /* Splice our SEQUENCE into the insn stream where INSN used to be.  */
  NEXT_INSN (seq_insn) = NEXT_INSN (insn);
  PREV_INSN (seq_insn) = PREV_INSN (insn);

  if (insn != last)
    PREV_INSN (NEXT_INSN (seq_insn)) = seq_insn;

  if (insn != first)
    NEXT_INSN (PREV_INSN (seq_insn)) = seq_insn;

  /* Note the calls to set_new_first_and_last_insn must occur after
     SEQ_INSN has been completely spliced into the insn stream.

     Otherwise CUR_INSN_UID will get set to an incorrect value because
     set_new_first_and_last_insn will not find SEQ_INSN in the chain.  */
  if (insn == last)
    set_new_first_and_last_insn (first, seq_insn);

  if (insn == first)
    set_new_first_and_last_insn (seq_insn, last);

  /* Build our SEQUENCE and rebuild the insn chain.  */
  XVECEXP (seq, 0, 0) = delay_insn;
  INSN_DELETED_P (delay_insn) = 0;
  PREV_INSN (delay_insn) = PREV_INSN (seq_insn);

  for (li = list; li; li = XEXP (li, 1), i++)
    {
      rtx tem = XEXP (li, 0);
      rtx note, next;

      /* Show that this copy of the insn isn't deleted.  */
      INSN_DELETED_P (tem) = 0;

      XVECEXP (seq, 0, i) = tem;
      PREV_INSN (tem) = XVECEXP (seq, 0, i - 1);
      NEXT_INSN (XVECEXP (seq, 0, i - 1)) = tem;

      /* SPARC assembler, for instance, emit warning when debug info is output
         into the delay slot.  */
      if (INSN_LOCATOR (tem) && !INSN_LOCATOR (seq_insn))
	INSN_LOCATOR (seq_insn) = INSN_LOCATOR (tem);
      INSN_LOCATOR (tem) = 0;

      for (note = REG_NOTES (tem); note; note = next)
	{
	  next = XEXP (note, 1);
	  switch (REG_NOTE_KIND (note))
	    {
	    case REG_DEAD:
	      /* Remove any REG_DEAD notes because we can't rely on them now
		 that the insn has been moved.  */
	      remove_note (tem, note);
	      break;

	    case REG_LABEL:
	      /* Keep the label reference count up to date.  */
	      if (LABEL_P (XEXP (note, 0)))
		LABEL_NUSES (XEXP (note, 0)) ++;
	      break;

	    default:
	      break;
	    }
	}
    }

  NEXT_INSN (XVECEXP (seq, 0, length)) = NEXT_INSN (seq_insn);

  /* If the previous insn is a SEQUENCE, update the NEXT_INSN pointer on the
     last insn in that SEQUENCE to point to us.  Similarly for the first
     insn in the following insn if it is a SEQUENCE.  */

  if (PREV_INSN (seq_insn) && NONJUMP_INSN_P (PREV_INSN (seq_insn))
      && GET_CODE (PATTERN (PREV_INSN (seq_insn))) == SEQUENCE)
    NEXT_INSN (XVECEXP (PATTERN (PREV_INSN (seq_insn)), 0,
			XVECLEN (PATTERN (PREV_INSN (seq_insn)), 0) - 1))
      = seq_insn;

  if (NEXT_INSN (seq_insn) && NONJUMP_INSN_P (NEXT_INSN (seq_insn))
      && GET_CODE (PATTERN (NEXT_INSN (seq_insn))) == SEQUENCE)
    PREV_INSN (XVECEXP (PATTERN (NEXT_INSN (seq_insn)), 0, 0)) = seq_insn;

  /* If there used to be a BARRIER, put it back.  */
  if (had_barrier)
    emit_barrier_after (seq_insn);

  gcc_assert (i == length + 1);

  return seq_insn;
}

/* Add INSN to DELAY_LIST and return the head of the new list.  The list must
   be in the order in which the insns are to be executed.  */

static rtx
add_to_delay_list (rtx insn, rtx delay_list)
{
  /* If we have an empty list, just make a new list element.  If
     INSN has its block number recorded, clear it since we may
     be moving the insn to a new block.  */

  if (delay_list == 0)
    {
      clear_hashed_info_for_insn (insn);
      return gen_rtx_INSN_LIST (VOIDmode, insn, NULL_RTX);
    }

  /* Otherwise this must be an INSN_LIST.  Add INSN to the end of the
     list.  */
  XEXP (delay_list, 1) = add_to_delay_list (insn, XEXP (delay_list, 1));

  return delay_list;
}

/* Delete INSN from the delay slot of the insn that it is in, which may
   produce an insn with no delay slots.  Return the new insn.  */

static rtx
delete_from_delay_slot (rtx insn)
{
  rtx trial, seq_insn, seq, prev;
  rtx delay_list = 0;
  int i;
  int had_barrier = 0;

  /* We first must find the insn containing the SEQUENCE with INSN in its
     delay slot.  Do this by finding an insn, TRIAL, where
     PREV_INSN (NEXT_INSN (TRIAL)) != TRIAL.  */

  for (trial = insn;
       PREV_INSN (NEXT_INSN (trial)) == trial;
       trial = NEXT_INSN (trial))
    ;

  seq_insn = PREV_INSN (NEXT_INSN (trial));
  seq = PATTERN (seq_insn);

  if (NEXT_INSN (seq_insn) && BARRIER_P (NEXT_INSN (seq_insn)))
    had_barrier = 1;

  /* Create a delay list consisting of all the insns other than the one
     we are deleting (unless we were the only one).  */
  if (XVECLEN (seq, 0) > 2)
    for (i = 1; i < XVECLEN (seq, 0); i++)
      if (XVECEXP (seq, 0, i) != insn)
	delay_list = add_to_delay_list (XVECEXP (seq, 0, i), delay_list);

  /* Delete the old SEQUENCE, re-emit the insn that used to have the delay
     list, and rebuild the delay list if non-empty.  */
  prev = PREV_INSN (seq_insn);
  trial = XVECEXP (seq, 0, 0);
  delete_related_insns (seq_insn);
  add_insn_after (trial, prev);

  /* If there was a barrier after the old SEQUENCE, remit it.  */
  if (had_barrier)
    emit_barrier_after (trial);

  /* If there are any delay insns, remit them.  Otherwise clear the
     annul flag.  */
  if (delay_list)
    trial = emit_delay_sequence (trial, delay_list, XVECLEN (seq, 0) - 2);
  else if (INSN_P (trial))
    INSN_ANNULLED_BRANCH_P (trial) = 0;

  INSN_FROM_TARGET_P (insn) = 0;

  /* Show we need to fill this insn again.  */
  obstack_ptr_grow (&unfilled_slots_obstack, trial);

  return trial;
}

/* Delete INSN, a JUMP_INSN.  If it is a conditional jump, we must track down
   the insn that sets CC0 for it and delete it too.  */

static void
delete_scheduled_jump (rtx insn)
{
  /* Delete the insn that sets cc0 for us.  On machines without cc0, we could
     delete the insn that sets the condition code, but it is hard to find it.
     Since this case is rare anyway, don't bother trying; there would likely
     be other insns that became dead anyway, which we wouldn't know to
     delete.  */

#ifdef HAVE_cc0
  if (reg_mentioned_p (cc0_rtx, insn))
    {
      rtx note = find_reg_note (insn, REG_CC_SETTER, NULL_RTX);

      /* If a reg-note was found, it points to an insn to set CC0.  This
	 insn is in the delay list of some other insn.  So delete it from
	 the delay list it was in.  */
      if (note)
	{
	  if (! FIND_REG_INC_NOTE (XEXP (note, 0), NULL_RTX)
	      && sets_cc0_p (PATTERN (XEXP (note, 0))) == 1)
	    delete_from_delay_slot (XEXP (note, 0));
	}
      else
	{
	  /* The insn setting CC0 is our previous insn, but it may be in
	     a delay slot.  It will be the last insn in the delay slot, if
	     it is.  */
	  rtx trial = previous_insn (insn);
	  if (NOTE_P (trial))
	    trial = prev_nonnote_insn (trial);
	  if (sets_cc0_p (PATTERN (trial)) != 1
	      || FIND_REG_INC_NOTE (trial, NULL_RTX))
	    return;
	  if (PREV_INSN (NEXT_INSN (trial)) == trial)
	    delete_related_insns (trial);
	  else
	    delete_from_delay_slot (trial);
	}
    }
#endif

  delete_related_insns (insn);
}

/* Counters for delay-slot filling.  */

#define NUM_REORG_FUNCTIONS 2
#define MAX_DELAY_HISTOGRAM 3
#define MAX_REORG_PASSES 2

static int num_insns_needing_delays[NUM_REORG_FUNCTIONS][MAX_REORG_PASSES];

static int num_filled_delays[NUM_REORG_FUNCTIONS][MAX_DELAY_HISTOGRAM+1][MAX_REORG_PASSES];

static int reorg_pass_number;

static void
note_delay_statistics (int slots_filled, int index)
{
  num_insns_needing_delays[index][reorg_pass_number]++;
  if (slots_filled > MAX_DELAY_HISTOGRAM)
    slots_filled = MAX_DELAY_HISTOGRAM;
  num_filled_delays[index][slots_filled][reorg_pass_number]++;
}

#if defined(ANNUL_IFFALSE_SLOTS) || defined(ANNUL_IFTRUE_SLOTS)

/* Optimize the following cases:

   1.  When a conditional branch skips over only one instruction,
       use an annulling branch and put that insn in the delay slot.
       Use either a branch that annuls when the condition if true or
       invert the test with a branch that annuls when the condition is
       false.  This saves insns, since otherwise we must copy an insn
       from the L1 target.

        (orig)		 (skip)		(otherwise)
	Bcc.n L1	Bcc',a L1	Bcc,a L1'
	insn		insn		insn2
      L1:	      L1:	      L1:
	insn2		insn2		insn2
	insn3		insn3	      L1':
					insn3

   2.  When a conditional branch skips over only one instruction,
       and after that, it unconditionally branches somewhere else,
       perform the similar optimization. This saves executing the
       second branch in the case where the inverted condition is true.

	Bcc.n L1	Bcc',a L2
	insn		insn
      L1:	      L1:
	Bra L2		Bra L2

   INSN is a JUMP_INSN.

   This should be expanded to skip over N insns, where N is the number
   of delay slots required.  */

static rtx
optimize_skip (rtx insn)
{
  rtx trial = next_nonnote_insn (insn);
  rtx next_trial = next_active_insn (trial);
  rtx delay_list = 0;
  int flags;

  flags = get_jump_flags (insn, JUMP_LABEL (insn));

  if (trial == 0
      || !NONJUMP_INSN_P (trial)
      || GET_CODE (PATTERN (trial)) == SEQUENCE
      || recog_memoized (trial) < 0
      || (! eligible_for_annul_false (insn, 0, trial, flags)
	  && ! eligible_for_annul_true (insn, 0, trial, flags))
      || can_throw_internal (trial))
    return 0;

  /* There are two cases where we are just executing one insn (we assume
     here that a branch requires only one insn; this should be generalized
     at some point):  Where the branch goes around a single insn or where
     we have one insn followed by a branch to the same label we branch to.
     In both of these cases, inverting the jump and annulling the delay
     slot give the same effect in fewer insns.  */
  if ((next_trial == next_active_insn (JUMP_LABEL (insn))
       && ! (next_trial == 0 && current_function_epilogue_delay_list != 0))
      || (next_trial != 0
	  && JUMP_P (next_trial)
	  && JUMP_LABEL (insn) == JUMP_LABEL (next_trial)
	  && (simplejump_p (next_trial)
	      || GET_CODE (PATTERN (next_trial)) == RETURN)))
    {
      if (eligible_for_annul_false (insn, 0, trial, flags))
	{
	  if (invert_jump (insn, JUMP_LABEL (insn), 1))
	    INSN_FROM_TARGET_P (trial) = 1;
	  else if (! eligible_for_annul_true (insn, 0, trial, flags))
	    return 0;
	}

      delay_list = add_to_delay_list (trial, NULL_RTX);
      next_trial = next_active_insn (trial);
      update_block (trial, trial);
      delete_related_insns (trial);

      /* Also, if we are targeting an unconditional
	 branch, thread our jump to the target of that branch.  Don't
	 change this into a RETURN here, because it may not accept what
	 we have in the delay slot.  We'll fix this up later.  */
      if (next_trial && JUMP_P (next_trial)
	  && (simplejump_p (next_trial)
	      || GET_CODE (PATTERN (next_trial)) == RETURN))
	{
	  rtx target_label = JUMP_LABEL (next_trial);
	  if (target_label == 0)
	    target_label = find_end_label ();

	  if (target_label)
	    {
	      /* Recompute the flags based on TARGET_LABEL since threading
		 the jump to TARGET_LABEL may change the direction of the
		 jump (which may change the circumstances in which the
		 delay slot is nullified).  */
	      flags = get_jump_flags (insn, target_label);
	      if (eligible_for_annul_true (insn, 0, trial, flags))
		reorg_redirect_jump (insn, target_label);
	    }
	}

      INSN_ANNULLED_BRANCH_P (insn) = 1;
    }

  return delay_list;
}
#endif

/*  Encode and return branch direction and prediction information for
    INSN assuming it will jump to LABEL.

    Non conditional branches return no direction information and
    are predicted as very likely taken.  */

static int
get_jump_flags (rtx insn, rtx label)
{
  int flags;

  /* get_jump_flags can be passed any insn with delay slots, these may
     be INSNs, CALL_INSNs, or JUMP_INSNs.  Only JUMP_INSNs have branch
     direction information, and only if they are conditional jumps.

     If LABEL is zero, then there is no way to determine the branch
     direction.  */
  if (JUMP_P (insn)
      && (condjump_p (insn) || condjump_in_parallel_p (insn))
      && INSN_UID (insn) <= max_uid
      && label != 0
      && INSN_UID (label) <= max_uid)
    flags
      = (uid_to_ruid[INSN_UID (label)] > uid_to_ruid[INSN_UID (insn)])
	 ? ATTR_FLAG_forward : ATTR_FLAG_backward;
  /* No valid direction information.  */
  else
    flags = 0;

  /* If insn is a conditional branch call mostly_true_jump to get
     determine the branch prediction.

     Non conditional branches are predicted as very likely taken.  */
  if (JUMP_P (insn)
      && (condjump_p (insn) || condjump_in_parallel_p (insn)))
    {
      int prediction;

      prediction = mostly_true_jump (insn, get_branch_condition (insn, label));
      switch (prediction)
	{
	case 2:
	  flags |= (ATTR_FLAG_very_likely | ATTR_FLAG_likely);
	  break;
	case 1:
	  flags |= ATTR_FLAG_likely;
	  break;
	case 0:
	  flags |= ATTR_FLAG_unlikely;
	  break;
	case -1:
	  flags |= (ATTR_FLAG_very_unlikely | ATTR_FLAG_unlikely);
	  break;

	default:
	  gcc_unreachable ();
	}
    }
  else
    flags |= (ATTR_FLAG_very_likely | ATTR_FLAG_likely);

  return flags;
}

/* Return 1 if INSN is a destination that will be branched to rarely (the
   return point of a function); return 2 if DEST will be branched to very
   rarely (a call to a function that doesn't return).  Otherwise,
   return 0.  */

static int
rare_destination (rtx insn)
{
  int jump_count = 0;
  rtx next;

  for (; insn; insn = next)
    {
      if (NONJUMP_INSN_P (insn) && GET_CODE (PATTERN (insn)) == SEQUENCE)
	insn = XVECEXP (PATTERN (insn), 0, 0);

      next = NEXT_INSN (insn);

      switch (GET_CODE (insn))
	{
	case CODE_LABEL:
	  return 0;
	case BARRIER:
	  /* A BARRIER can either be after a JUMP_INSN or a CALL_INSN.  We
	     don't scan past JUMP_INSNs, so any barrier we find here must
	     have been after a CALL_INSN and hence mean the call doesn't
	     return.  */
	  return 2;
	case JUMP_INSN:
	  if (GET_CODE (PATTERN (insn)) == RETURN)
	    return 1;
	  else if (simplejump_p (insn)
		   && jump_count++ < 10)
	    next = JUMP_LABEL (insn);
	  else
	    return 0;

	default:
	  break;
	}
    }

  /* If we got here it means we hit the end of the function.  So this
     is an unlikely destination.  */

  return 1;
}

/* Return truth value of the statement that this branch
   is mostly taken.  If we think that the branch is extremely likely
   to be taken, we return 2.  If the branch is slightly more likely to be
   taken, return 1.  If the branch is slightly less likely to be taken,
   return 0 and if the branch is highly unlikely to be taken, return -1.

   CONDITION, if nonzero, is the condition that JUMP_INSN is testing.  */

static int
mostly_true_jump (rtx jump_insn, rtx condition)
{
  rtx target_label = JUMP_LABEL (jump_insn);
  rtx note;
  int rare_dest, rare_fallthrough;

  /* If branch probabilities are available, then use that number since it
     always gives a correct answer.  */
  note = find_reg_note (jump_insn, REG_BR_PROB, 0);
  if (note)
    {
      int prob = INTVAL (XEXP (note, 0));

      if (prob >= REG_BR_PROB_BASE * 9 / 10)
	return 2;
      else if (prob >= REG_BR_PROB_BASE / 2)
	return 1;
      else if (prob >= REG_BR_PROB_BASE / 10)
	return 0;
      else
	return -1;
    }

  /* Look at the relative rarities of the fallthrough and destination.  If
     they differ, we can predict the branch that way.  */
  rare_dest = rare_destination (target_label);
  rare_fallthrough = rare_destination (NEXT_INSN (jump_insn));

  switch (rare_fallthrough - rare_dest)
    {
    case -2:
      return -1;
    case -1:
      return 0;
    case 0:
      break;
    case 1:
      return 1;
    case 2:
      return 2;
    }

  /* If we couldn't figure out what this jump was, assume it won't be
     taken.  This should be rare.  */
  if (condition == 0)
    return 0;

  /* Predict backward branches usually take, forward branches usually not.  If
     we don't know whether this is forward or backward, assume the branch
     will be taken, since most are.  */
  return (target_label == 0 || INSN_UID (jump_insn) > max_uid
	  || INSN_UID (target_label) > max_uid
	  || (uid_to_ruid[INSN_UID (jump_insn)]
	      > uid_to_ruid[INSN_UID (target_label)]));
}

/* Return the condition under which INSN will branch to TARGET.  If TARGET
   is zero, return the condition under which INSN will return.  If INSN is
   an unconditional branch, return const_true_rtx.  If INSN isn't a simple
   type of jump, or it doesn't go to TARGET, return 0.  */

static rtx
get_branch_condition (rtx insn, rtx target)
{
  rtx pat = PATTERN (insn);
  rtx src;

  if (condjump_in_parallel_p (insn))
    pat = XVECEXP (pat, 0, 0);

  if (GET_CODE (pat) == RETURN)
    return target == 0 ? const_true_rtx : 0;

  else if (GET_CODE (pat) != SET || SET_DEST (pat) != pc_rtx)
    return 0;

  src = SET_SRC (pat);
  if (GET_CODE (src) == LABEL_REF && XEXP (src, 0) == target)
    return const_true_rtx;

  else if (GET_CODE (src) == IF_THEN_ELSE
	   && ((target == 0 && GET_CODE (XEXP (src, 1)) == RETURN)
	       || (GET_CODE (XEXP (src, 1)) == LABEL_REF
		   && XEXP (XEXP (src, 1), 0) == target))
	   && XEXP (src, 2) == pc_rtx)
    return XEXP (src, 0);

  else if (GET_CODE (src) == IF_THEN_ELSE
	   && ((target == 0 && GET_CODE (XEXP (src, 2)) == RETURN)
	       || (GET_CODE (XEXP (src, 2)) == LABEL_REF
		   && XEXP (XEXP (src, 2), 0) == target))
	   && XEXP (src, 1) == pc_rtx)
    {
      enum rtx_code rev;
      rev = reversed_comparison_code (XEXP (src, 0), insn);
      if (rev != UNKNOWN)
	return gen_rtx_fmt_ee (rev, GET_MODE (XEXP (src, 0)),
			       XEXP (XEXP (src, 0), 0),
			       XEXP (XEXP (src, 0), 1));
    }

  return 0;
}

/* Return nonzero if CONDITION is more strict than the condition of
   INSN, i.e., if INSN will always branch if CONDITION is true.  */

static int
condition_dominates_p (rtx condition, rtx insn)
{
  rtx other_condition = get_branch_condition (insn, JUMP_LABEL (insn));
  enum rtx_code code = GET_CODE (condition);
  enum rtx_code other_code;

  if (rtx_equal_p (condition, other_condition)
      || other_condition == const_true_rtx)
    return 1;

  else if (condition == const_true_rtx || other_condition == 0)
    return 0;

  other_code = GET_CODE (other_condition);
  if (GET_RTX_LENGTH (code) != 2 || GET_RTX_LENGTH (other_code) != 2
      || ! rtx_equal_p (XEXP (condition, 0), XEXP (other_condition, 0))
      || ! rtx_equal_p (XEXP (condition, 1), XEXP (other_condition, 1)))
    return 0;

  return comparison_dominates_p (code, other_code);
}

/* Return nonzero if redirecting JUMP to NEWLABEL does not invalidate
   any insns already in the delay slot of JUMP.  */

static int
redirect_with_delay_slots_safe_p (rtx jump, rtx newlabel, rtx seq)
{
  int flags, i;
  rtx pat = PATTERN (seq);

  /* Make sure all the delay slots of this jump would still
     be valid after threading the jump.  If they are still
     valid, then return nonzero.  */

  flags = get_jump_flags (jump, newlabel);
  for (i = 1; i < XVECLEN (pat, 0); i++)
    if (! (
#ifdef ANNUL_IFFALSE_SLOTS
	   (INSN_ANNULLED_BRANCH_P (jump)
	    && INSN_FROM_TARGET_P (XVECEXP (pat, 0, i)))
	   ? eligible_for_annul_false (jump, i - 1,
				       XVECEXP (pat, 0, i), flags) :
#endif
#ifdef ANNUL_IFTRUE_SLOTS
	   (INSN_ANNULLED_BRANCH_P (jump)
	    && ! INSN_FROM_TARGET_P (XVECEXP (pat, 0, i)))
	   ? eligible_for_annul_true (jump, i - 1,
				      XVECEXP (pat, 0, i), flags) :
#endif
	   eligible_for_delay (jump, i - 1, XVECEXP (pat, 0, i), flags)))
      break;

  return (i == XVECLEN (pat, 0));
}

/* Return nonzero if redirecting JUMP to NEWLABEL does not invalidate
   any insns we wish to place in the delay slot of JUMP.  */

static int
redirect_with_delay_list_safe_p (rtx jump, rtx newlabel, rtx delay_list)
{
  int flags, i;
  rtx li;

  /* Make sure all the insns in DELAY_LIST would still be
     valid after threading the jump.  If they are still
     valid, then return nonzero.  */

  flags = get_jump_flags (jump, newlabel);
  for (li = delay_list, i = 0; li; li = XEXP (li, 1), i++)
    if (! (
#ifdef ANNUL_IFFALSE_SLOTS
	   (INSN_ANNULLED_BRANCH_P (jump)
	    && INSN_FROM_TARGET_P (XEXP (li, 0)))
	   ? eligible_for_annul_false (jump, i, XEXP (li, 0), flags) :
#endif
#ifdef ANNUL_IFTRUE_SLOTS
	   (INSN_ANNULLED_BRANCH_P (jump)
	    && ! INSN_FROM_TARGET_P (XEXP (li, 0)))
	   ? eligible_for_annul_true (jump, i, XEXP (li, 0), flags) :
#endif
	   eligible_for_delay (jump, i, XEXP (li, 0), flags)))
      break;

  return (li == NULL);
}

/* DELAY_LIST is a list of insns that have already been placed into delay
   slots.  See if all of them have the same annulling status as ANNUL_TRUE_P.
   If not, return 0; otherwise return 1.  */

static int
check_annul_list_true_false (int annul_true_p, rtx delay_list)
{
  rtx temp;

  if (delay_list)
    {
      for (temp = delay_list; temp; temp = XEXP (temp, 1))
	{
	  rtx trial = XEXP (temp, 0);

	  if ((annul_true_p && INSN_FROM_TARGET_P (trial))
	      || (!annul_true_p && !INSN_FROM_TARGET_P (trial)))
	    return 0;
	}
    }

  return 1;
}

/* INSN branches to an insn whose pattern SEQ is a SEQUENCE.  Given that
   the condition tested by INSN is CONDITION and the resources shown in
   OTHER_NEEDED are needed after INSN, see whether INSN can take all the insns
   from SEQ's delay list, in addition to whatever insns it may execute
   (in DELAY_LIST).   SETS and NEEDED are denote resources already set and
   needed while searching for delay slot insns.  Return the concatenated
   delay list if possible, otherwise, return 0.

   SLOTS_TO_FILL is the total number of slots required by INSN, and
   PSLOTS_FILLED points to the number filled so far (also the number of
   insns in DELAY_LIST).  It is updated with the number that have been
   filled from the SEQUENCE, if any.

   PANNUL_P points to a nonzero value if we already know that we need
   to annul INSN.  If this routine determines that annulling is needed,
   it may set that value nonzero.

   PNEW_THREAD points to a location that is to receive the place at which
   execution should continue.  */

static rtx
steal_delay_list_from_target (rtx insn, rtx condition, rtx seq,
			      rtx delay_list, struct resources *sets,
			      struct resources *needed,
			      struct resources *other_needed,
			      int slots_to_fill, int *pslots_filled,
			      int *pannul_p, rtx *pnew_thread)
{
  rtx temp;
  int slots_remaining = slots_to_fill - *pslots_filled;
  int total_slots_filled = *pslots_filled;
  rtx new_delay_list = 0;
  int must_annul = *pannul_p;
  int used_annul = 0;
  int i;
  struct resources cc_set;

  /* We can't do anything if there are more delay slots in SEQ than we
     can handle, or if we don't know that it will be a taken branch.
     We know that it will be a taken branch if it is either an unconditional
     branch or a conditional branch with a stricter branch condition.

     Also, exit if the branch has more than one set, since then it is computing
     other results that can't be ignored, e.g. the HPPA mov&branch instruction.
     ??? It may be possible to move other sets into INSN in addition to
     moving the instructions in the delay slots.

     We can not steal the delay list if one of the instructions in the
     current delay_list modifies the condition codes and the jump in the
     sequence is a conditional jump. We can not do this because we can
     not change the direction of the jump because the condition codes
     will effect the direction of the jump in the sequence.  */

  CLEAR_RESOURCE (&cc_set);
  for (temp = delay_list; temp; temp = XEXP (temp, 1))
    {
      rtx trial = XEXP (temp, 0);

      mark_set_resources (trial, &cc_set, 0, MARK_SRC_DEST_CALL);
      if (insn_references_resource_p (XVECEXP (seq , 0, 0), &cc_set, 0))
	return delay_list;
    }

  if (XVECLEN (seq, 0) - 1 > slots_remaining
      || ! condition_dominates_p (condition, XVECEXP (seq, 0, 0))
      || ! single_set (XVECEXP (seq, 0, 0)))
    return delay_list;

#ifdef MD_CAN_REDIRECT_BRANCH
  /* On some targets, branches with delay slots can have a limited
     displacement.  Give the back end a chance to tell us we can't do
     this.  */
  if (! MD_CAN_REDIRECT_BRANCH (insn, XVECEXP (seq, 0, 0)))
    return delay_list;
#endif

  for (i = 1; i < XVECLEN (seq, 0); i++)
    {
      rtx trial = XVECEXP (seq, 0, i);
      int flags;

      if (insn_references_resource_p (trial, sets, 0)
	  || insn_sets_resource_p (trial, needed, 0)
	  || insn_sets_resource_p (trial, sets, 0)
#ifdef HAVE_cc0
	  /* If TRIAL sets CC0, we can't copy it, so we can't steal this
	     delay list.  */
	  || find_reg_note (trial, REG_CC_USER, NULL_RTX)
#endif
	  /* If TRIAL is from the fallthrough code of an annulled branch insn
	     in SEQ, we cannot use it.  */
	  || (INSN_ANNULLED_BRANCH_P (XVECEXP (seq, 0, 0))
	      && ! INSN_FROM_TARGET_P (trial)))
	return delay_list;

      /* If this insn was already done (usually in a previous delay slot),
	 pretend we put it in our delay slot.  */
      if (redundant_insn (trial, insn, new_delay_list))
	continue;

      /* We will end up re-vectoring this branch, so compute flags
	 based on jumping to the new label.  */
      flags = get_jump_flags (insn, JUMP_LABEL (XVECEXP (seq, 0, 0)));

      if (! must_annul
	  && ((condition == const_true_rtx
	       || (! insn_sets_resource_p (trial, other_needed, 0)
		   && ! may_trap_or_fault_p (PATTERN (trial)))))
	  ? eligible_for_delay (insn, total_slots_filled, trial, flags)
	  : (must_annul || (delay_list == NULL && new_delay_list == NULL))
	     && (must_annul = 1,
	         check_annul_list_true_false (0, delay_list)
	         && check_annul_list_true_false (0, new_delay_list)
	         && eligible_for_annul_false (insn, total_slots_filled,
					      trial, flags)))
	{
	  if (must_annul)
	    used_annul = 1;
	  temp = copy_rtx (trial);
	  INSN_FROM_TARGET_P (temp) = 1;
	  new_delay_list = add_to_delay_list (temp, new_delay_list);
	  total_slots_filled++;

	  if (--slots_remaining == 0)
	    break;
	}
      else
	return delay_list;
    }

  /* Show the place to which we will be branching.  */
  *pnew_thread = next_active_insn (JUMP_LABEL (XVECEXP (seq, 0, 0)));

  /* Add any new insns to the delay list and update the count of the
     number of slots filled.  */
  *pslots_filled = total_slots_filled;
  if (used_annul)
    *pannul_p = 1;

  if (delay_list == 0)
    return new_delay_list;

  for (temp = new_delay_list; temp; temp = XEXP (temp, 1))
    delay_list = add_to_delay_list (XEXP (temp, 0), delay_list);

  return delay_list;
}

/* Similar to steal_delay_list_from_target except that SEQ is on the
   fallthrough path of INSN.  Here we only do something if the delay insn
   of SEQ is an unconditional branch.  In that case we steal its delay slot
   for INSN since unconditional branches are much easier to fill.  */

static rtx
steal_delay_list_from_fallthrough (rtx insn, rtx condition, rtx seq,
				   rtx delay_list, struct resources *sets,
				   struct resources *needed,
				   struct resources *other_needed,
				   int slots_to_fill, int *pslots_filled,
				   int *pannul_p)
{
  int i;
  int flags;
  int must_annul = *pannul_p;
  int used_annul = 0;

  flags = get_jump_flags (insn, JUMP_LABEL (insn));

  /* We can't do anything if SEQ's delay insn isn't an
     unconditional branch.  */

  if (! simplejump_p (XVECEXP (seq, 0, 0))
      && GET_CODE (PATTERN (XVECEXP (seq, 0, 0))) != RETURN)
    return delay_list;

  for (i = 1; i < XVECLEN (seq, 0); i++)
    {
      rtx trial = XVECEXP (seq, 0, i);

      /* If TRIAL sets CC0, stealing it will move it too far from the use
	 of CC0.  */
      if (insn_references_resource_p (trial, sets, 0)
	  || insn_sets_resource_p (trial, needed, 0)
	  || insn_sets_resource_p (trial, sets, 0)
#ifdef HAVE_cc0
	  || sets_cc0_p (PATTERN (trial))
#endif
	  )

	break;

      /* If this insn was already done, we don't need it.  */
      if (redundant_insn (trial, insn, delay_list))
	{
	  delete_from_delay_slot (trial);
	  continue;
	}

      if (! must_annul
	  && ((condition == const_true_rtx
	       || (! insn_sets_resource_p (trial, other_needed, 0)
		   && ! may_trap_or_fault_p (PATTERN (trial)))))
	  ? eligible_for_delay (insn, *pslots_filled, trial, flags)
	  : (must_annul || delay_list == NULL) && (must_annul = 1,
	     check_annul_list_true_false (1, delay_list)
	     && eligible_for_annul_true (insn, *pslots_filled, trial, flags)))
	{
	  if (must_annul)
	    used_annul = 1;
	  delete_from_delay_slot (trial);
	  delay_list = add_to_delay_list (trial, delay_list);

	  if (++(*pslots_filled) == slots_to_fill)
	    break;
	}
      else
	break;
    }

  if (used_annul)
    *pannul_p = 1;
  return delay_list;
}

/* Try merging insns starting at THREAD which match exactly the insns in
   INSN's delay list.

   If all insns were matched and the insn was previously annulling, the
   annul bit will be cleared.

   For each insn that is merged, if the branch is or will be non-annulling,
   we delete the merged insn.  */

static void
try_merge_delay_insns (rtx insn, rtx thread)
{
  rtx trial, next_trial;
  rtx delay_insn = XVECEXP (PATTERN (insn), 0, 0);
  int annul_p = INSN_ANNULLED_BRANCH_P (delay_insn);
  int slot_number = 1;
  int num_slots = XVECLEN (PATTERN (insn), 0);
  rtx next_to_match = XVECEXP (PATTERN (insn), 0, slot_number);
  struct resources set, needed;
  rtx merged_insns = 0;
  int i;
  int flags;

  flags = get_jump_flags (delay_insn, JUMP_LABEL (delay_insn));

  CLEAR_RESOURCE (&needed);
  CLEAR_RESOURCE (&set);

  /* If this is not an annulling branch, take into account anything needed in
     INSN's delay slot.  This prevents two increments from being incorrectly
     folded into one.  If we are annulling, this would be the correct
     thing to do.  (The alternative, looking at things set in NEXT_TO_MATCH
     will essentially disable this optimization.  This method is somewhat of
     a kludge, but I don't see a better way.)  */
  if (! annul_p)
    for (i = 1 ; i < num_slots; i++)
      if (XVECEXP (PATTERN (insn), 0, i))
	mark_referenced_resources (XVECEXP (PATTERN (insn), 0, i), &needed, 1);

  for (trial = thread; !stop_search_p (trial, 1); trial = next_trial)
    {
      rtx pat = PATTERN (trial);
      rtx oldtrial = trial;

      next_trial = next_nonnote_insn (trial);

      /* TRIAL must be a CALL_INSN or INSN.  Skip USE and CLOBBER.  */
      if (NONJUMP_INSN_P (trial)
	  && (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER))
	continue;

      if (GET_CODE (next_to_match) == GET_CODE (trial)
#ifdef HAVE_cc0
	  /* We can't share an insn that sets cc0.  */
	  && ! sets_cc0_p (pat)
#endif
	  && ! insn_references_resource_p (trial, &set, 1)
	  && ! insn_sets_resource_p (trial, &set, 1)
	  && ! insn_sets_resource_p (trial, &needed, 1)
	  && (trial = try_split (pat, trial, 0)) != 0
	  /* Update next_trial, in case try_split succeeded.  */
	  && (next_trial = next_nonnote_insn (trial))
	  /* Likewise THREAD.  */
	  && (thread = oldtrial == thread ? trial : thread)
	  && rtx_equal_p (PATTERN (next_to_match), PATTERN (trial))
	  /* Have to test this condition if annul condition is different
	     from (and less restrictive than) non-annulling one.  */
	  && eligible_for_delay (delay_insn, slot_number - 1, trial, flags))
	{

	  if (! annul_p)
	    {
	      update_block (trial, thread);
	      if (trial == thread)
		thread = next_active_insn (thread);

	      delete_related_insns (trial);
	      INSN_FROM_TARGET_P (next_to_match) = 0;
	    }
	  else
	    merged_insns = gen_rtx_INSN_LIST (VOIDmode, trial, merged_insns);

	  if (++slot_number == num_slots)
	    break;

	  next_to_match = XVECEXP (PATTERN (insn), 0, slot_number);
	}

      mark_set_resources (trial, &set, 0, MARK_SRC_DEST_CALL);
      mark_referenced_resources (trial, &needed, 1);
    }

  /* See if we stopped on a filled insn.  If we did, try to see if its
     delay slots match.  */
  if (slot_number != num_slots
      && trial && NONJUMP_INSN_P (trial)
      && GET_CODE (PATTERN (trial)) == SEQUENCE
      && ! INSN_ANNULLED_BRANCH_P (XVECEXP (PATTERN (trial), 0, 0)))
    {
      rtx pat = PATTERN (trial);
      rtx filled_insn = XVECEXP (pat, 0, 0);

      /* Account for resources set/needed by the filled insn.  */
      mark_set_resources (filled_insn, &set, 0, MARK_SRC_DEST_CALL);
      mark_referenced_resources (filled_insn, &needed, 1);

      for (i = 1; i < XVECLEN (pat, 0); i++)
	{
	  rtx dtrial = XVECEXP (pat, 0, i);

	  if (! insn_references_resource_p (dtrial, &set, 1)
	      && ! insn_sets_resource_p (dtrial, &set, 1)
	      && ! insn_sets_resource_p (dtrial, &needed, 1)
#ifdef HAVE_cc0
	      && ! sets_cc0_p (PATTERN (dtrial))
#endif
	      && rtx_equal_p (PATTERN (next_to_match), PATTERN (dtrial))
	      && eligible_for_delay (delay_insn, slot_number - 1, dtrial, flags))
	    {
	      if (! annul_p)
		{
		  rtx new;

		  update_block (dtrial, thread);
		  new = delete_from_delay_slot (dtrial);
	          if (INSN_DELETED_P (thread))
		    thread = new;
		  INSN_FROM_TARGET_P (next_to_match) = 0;
		}
	      else
		merged_insns = gen_rtx_INSN_LIST (SImode, dtrial,
						  merged_insns);

	      if (++slot_number == num_slots)
		break;

	      next_to_match = XVECEXP (PATTERN (insn), 0, slot_number);
	    }
	  else
	    {
	      /* Keep track of the set/referenced resources for the delay
		 slots of any trial insns we encounter.  */
	      mark_set_resources (dtrial, &set, 0, MARK_SRC_DEST_CALL);
	      mark_referenced_resources (dtrial, &needed, 1);
	    }
	}
    }

  /* If all insns in the delay slot have been matched and we were previously
     annulling the branch, we need not any more.  In that case delete all the
     merged insns.  Also clear the INSN_FROM_TARGET_P bit of each insn in
     the delay list so that we know that it isn't only being used at the
     target.  */
  if (slot_number == num_slots && annul_p)
    {
      for (; merged_insns; merged_insns = XEXP (merged_insns, 1))
	{
	  if (GET_MODE (merged_insns) == SImode)
	    {
	      rtx new;

	      update_block (XEXP (merged_insns, 0), thread);
	      new = delete_from_delay_slot (XEXP (merged_insns, 0));
	      if (INSN_DELETED_P (thread))
		thread = new;
	    }
	  else
	    {
	      update_block (XEXP (merged_insns, 0), thread);
	      delete_related_insns (XEXP (merged_insns, 0));
	    }
	}

      INSN_ANNULLED_BRANCH_P (delay_insn) = 0;

      for (i = 0; i < XVECLEN (PATTERN (insn), 0); i++)
	INSN_FROM_TARGET_P (XVECEXP (PATTERN (insn), 0, i)) = 0;
    }
}

/* See if INSN is redundant with an insn in front of TARGET.  Often this
   is called when INSN is a candidate for a delay slot of TARGET.
   DELAY_LIST are insns that will be placed in delay slots of TARGET in front
   of INSN.  Often INSN will be redundant with an insn in a delay slot of
   some previous insn.  This happens when we have a series of branches to the
   same label; in that case the first insn at the target might want to go
   into each of the delay slots.

   If we are not careful, this routine can take up a significant fraction
   of the total compilation time (4%), but only wins rarely.  Hence we
   speed this routine up by making two passes.  The first pass goes back
   until it hits a label and sees if it finds an insn with an identical
   pattern.  Only in this (relatively rare) event does it check for
   data conflicts.

   We do not split insns we encounter.  This could cause us not to find a
   redundant insn, but the cost of splitting seems greater than the possible
   gain in rare cases.  */

static rtx
redundant_insn (rtx insn, rtx target, rtx delay_list)
{
  rtx target_main = target;
  rtx ipat = PATTERN (insn);
  rtx trial, pat;
  struct resources needed, set;
  int i;
  unsigned insns_to_search;

  /* If INSN has any REG_UNUSED notes, it can't match anything since we
     are allowed to not actually assign to such a register.  */
  if (find_reg_note (insn, REG_UNUSED, NULL_RTX) != 0)
    return 0;

  /* Scan backwards looking for a match.  */
  for (trial = PREV_INSN (target),
	 insns_to_search = MAX_DELAY_SLOT_INSN_SEARCH;
       trial && insns_to_search > 0;
       trial = PREV_INSN (trial), --insns_to_search)
    {
      if (LABEL_P (trial))
	return 0;

      if (! INSN_P (trial))
	continue;

      pat = PATTERN (trial);
      if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
	continue;

      if (GET_CODE (pat) == SEQUENCE)
	{
	  /* Stop for a CALL and its delay slots because it is difficult to
	     track its resource needs correctly.  */
	  if (CALL_P (XVECEXP (pat, 0, 0)))
	    return 0;

	  /* Stop for an INSN or JUMP_INSN with delayed effects and its delay
	     slots because it is difficult to track its resource needs
	     correctly.  */

#ifdef INSN_SETS_ARE_DELAYED
	  if (INSN_SETS_ARE_DELAYED (XVECEXP (pat, 0, 0)))
	    return 0;
#endif

#ifdef INSN_REFERENCES_ARE_DELAYED
	  if (INSN_REFERENCES_ARE_DELAYED (XVECEXP (pat, 0, 0)))
	    return 0;
#endif

	  /* See if any of the insns in the delay slot match, updating
	     resource requirements as we go.  */
	  for (i = XVECLEN (pat, 0) - 1; i > 0; i--)
	    if (GET_CODE (XVECEXP (pat, 0, i)) == GET_CODE (insn)
		&& rtx_equal_p (PATTERN (XVECEXP (pat, 0, i)), ipat)
		&& ! find_reg_note (XVECEXP (pat, 0, i), REG_UNUSED, NULL_RTX))
	      break;

	  /* If found a match, exit this loop early.  */
	  if (i > 0)
	    break;
	}

      else if (GET_CODE (trial) == GET_CODE (insn) && rtx_equal_p (pat, ipat)
	       && ! find_reg_note (trial, REG_UNUSED, NULL_RTX))
	break;
    }

  /* If we didn't find an insn that matches, return 0.  */
  if (trial == 0)
    return 0;

  /* See what resources this insn sets and needs.  If they overlap, or
     if this insn references CC0, it can't be redundant.  */

  CLEAR_RESOURCE (&needed);
  CLEAR_RESOURCE (&set);
  mark_set_resources (insn, &set, 0, MARK_SRC_DEST_CALL);
  mark_referenced_resources (insn, &needed, 1);

  /* If TARGET is a SEQUENCE, get the main insn.  */
  if (NONJUMP_INSN_P (target) && GET_CODE (PATTERN (target)) == SEQUENCE)
    target_main = XVECEXP (PATTERN (target), 0, 0);

  if (resource_conflicts_p (&needed, &set)
#ifdef HAVE_cc0
      || reg_mentioned_p (cc0_rtx, ipat)
#endif
      /* The insn requiring the delay may not set anything needed or set by
	 INSN.  */
      || insn_sets_resource_p (target_main, &needed, 1)
      || insn_sets_resource_p (target_main, &set, 1))
    return 0;

  /* Insns we pass may not set either NEEDED or SET, so merge them for
     simpler tests.  */
  needed.memory |= set.memory;
  needed.unch_memory |= set.unch_memory;
  IOR_HARD_REG_SET (needed.regs, set.regs);

  /* This insn isn't redundant if it conflicts with an insn that either is
     or will be in a delay slot of TARGET.  */

  while (delay_list)
    {
      if (insn_sets_resource_p (XEXP (delay_list, 0), &needed, 1))
	return 0;
      delay_list = XEXP (delay_list, 1);
    }

  if (NONJUMP_INSN_P (target) && GET_CODE (PATTERN (target)) == SEQUENCE)
    for (i = 1; i < XVECLEN (PATTERN (target), 0); i++)
      if (insn_sets_resource_p (XVECEXP (PATTERN (target), 0, i), &needed, 1))
	return 0;

  /* Scan backwards until we reach a label or an insn that uses something
     INSN sets or sets something insn uses or sets.  */

  for (trial = PREV_INSN (target),
	 insns_to_search = MAX_DELAY_SLOT_INSN_SEARCH;
       trial && !LABEL_P (trial) && insns_to_search > 0;
       trial = PREV_INSN (trial), --insns_to_search)
    {
      if (!INSN_P (trial))
	continue;

      pat = PATTERN (trial);
      if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
	continue;

      if (GET_CODE (pat) == SEQUENCE)
	{
	  /* If this is a CALL_INSN and its delay slots, it is hard to track
	     the resource needs properly, so give up.  */
	  if (CALL_P (XVECEXP (pat, 0, 0)))
	    return 0;

	  /* If this is an INSN or JUMP_INSN with delayed effects, it
	     is hard to track the resource needs properly, so give up.  */

#ifdef INSN_SETS_ARE_DELAYED
	  if (INSN_SETS_ARE_DELAYED (XVECEXP (pat, 0, 0)))
	    return 0;
#endif

#ifdef INSN_REFERENCES_ARE_DELAYED
	  if (INSN_REFERENCES_ARE_DELAYED (XVECEXP (pat, 0, 0)))
	    return 0;
#endif

	  /* See if any of the insns in the delay slot match, updating
	     resource requirements as we go.  */
	  for (i = XVECLEN (pat, 0) - 1; i > 0; i--)
	    {
	      rtx candidate = XVECEXP (pat, 0, i);

	      /* If an insn will be annulled if the branch is false, it isn't
		 considered as a possible duplicate insn.  */
	      if (rtx_equal_p (PATTERN (candidate), ipat)
		  && ! (INSN_ANNULLED_BRANCH_P (XVECEXP (pat, 0, 0))
			&& INSN_FROM_TARGET_P (candidate)))
		{
		  /* Show that this insn will be used in the sequel.  */
		  INSN_FROM_TARGET_P (candidate) = 0;
		  return candidate;
		}

	      /* Unless this is an annulled insn from the target of a branch,
		 we must stop if it sets anything needed or set by INSN.  */
	      if ((! INSN_ANNULLED_BRANCH_P (XVECEXP (pat, 0, 0))
		   || ! INSN_FROM_TARGET_P (candidate))
		  && insn_sets_resource_p (candidate, &needed, 1))
		return 0;
	    }

	  /* If the insn requiring the delay slot conflicts with INSN, we
	     must stop.  */
	  if (insn_sets_resource_p (XVECEXP (pat, 0, 0), &needed, 1))
	    return 0;
	}
      else
	{
	  /* See if TRIAL is the same as INSN.  */
	  pat = PATTERN (trial);
	  if (rtx_equal_p (pat, ipat))
	    return trial;

	  /* Can't go any further if TRIAL conflicts with INSN.  */
	  if (insn_sets_resource_p (trial, &needed, 1))
	    return 0;
	}
    }

  return 0;
}

/* Return 1 if THREAD can only be executed in one way.  If LABEL is nonzero,
   it is the target of the branch insn being scanned.  If ALLOW_FALLTHROUGH
   is nonzero, we are allowed to fall into this thread; otherwise, we are
   not.

   If LABEL is used more than one or we pass a label other than LABEL before
   finding an active insn, we do not own this thread.  */

static int
own_thread_p (rtx thread, rtx label, int allow_fallthrough)
{
  rtx active_insn;
  rtx insn;

  /* We don't own the function end.  */
  if (thread == 0)
    return 0;

  /* Get the first active insn, or THREAD, if it is an active insn.  */
  active_insn = next_active_insn (PREV_INSN (thread));

  for (insn = thread; insn != active_insn; insn = NEXT_INSN (insn))
    if (LABEL_P (insn)
	&& (insn != label || LABEL_NUSES (insn) != 1))
      return 0;

  if (allow_fallthrough)
    return 1;

  /* Ensure that we reach a BARRIER before any insn or label.  */
  for (insn = prev_nonnote_insn (thread);
       insn == 0 || !BARRIER_P (insn);
       insn = prev_nonnote_insn (insn))
    if (insn == 0
	|| LABEL_P (insn)
	|| (NONJUMP_INSN_P (insn)
	    && GET_CODE (PATTERN (insn)) != USE
	    && GET_CODE (PATTERN (insn)) != CLOBBER))
      return 0;

  return 1;
}

/* Called when INSN is being moved from a location near the target of a jump.
   We leave a marker of the form (use (INSN)) immediately in front
   of WHERE for mark_target_live_regs.  These markers will be deleted when
   reorg finishes.

   We used to try to update the live status of registers if WHERE is at
   the start of a basic block, but that can't work since we may remove a
   BARRIER in relax_delay_slots.  */

static void
update_block (rtx insn, rtx where)
{
  /* Ignore if this was in a delay slot and it came from the target of
     a branch.  */
  if (INSN_FROM_TARGET_P (insn))
    return;

  emit_insn_before (gen_rtx_USE (VOIDmode, insn), where);

  /* INSN might be making a value live in a block where it didn't use to
     be.  So recompute liveness information for this block.  */

  incr_ticks_for_insn (insn);
}

/* Similar to REDIRECT_JUMP except that we update the BB_TICKS entry for
   the basic block containing the jump.  */

static int
reorg_redirect_jump (rtx jump, rtx nlabel)
{
  incr_ticks_for_insn (jump);
  return redirect_jump (jump, nlabel, 1);
}

/* Called when INSN is being moved forward into a delay slot of DELAYED_INSN.
   We check every instruction between INSN and DELAYED_INSN for REG_DEAD notes
   that reference values used in INSN.  If we find one, then we move the
   REG_DEAD note to INSN.

   This is needed to handle the case where a later insn (after INSN) has a
   REG_DEAD note for a register used by INSN, and this later insn subsequently
   gets moved before a CODE_LABEL because it is a redundant insn.  In this
   case, mark_target_live_regs may be confused into thinking the register
   is dead because it sees a REG_DEAD note immediately before a CODE_LABEL.  */

static void
update_reg_dead_notes (rtx insn, rtx delayed_insn)
{
  rtx p, link, next;

  for (p = next_nonnote_insn (insn); p != delayed_insn;
       p = next_nonnote_insn (p))
    for (link = REG_NOTES (p); link; link = next)
      {
	next = XEXP (link, 1);

	if (REG_NOTE_KIND (link) != REG_DEAD
	    || !REG_P (XEXP (link, 0)))
	  continue;

	if (reg_referenced_p (XEXP (link, 0), PATTERN (insn)))
	  {
	    /* Move the REG_DEAD note from P to INSN.  */
	    remove_note (p, link);
	    XEXP (link, 1) = REG_NOTES (insn);
	    REG_NOTES (insn) = link;
	  }
      }
}

/* Called when an insn redundant with start_insn is deleted.  If there
   is a REG_DEAD note for the target of start_insn between start_insn
   and stop_insn, then the REG_DEAD note needs to be deleted since the
   value no longer dies there.

   If the REG_DEAD note isn't deleted, then mark_target_live_regs may be
   confused into thinking the register is dead.  */

static void
fix_reg_dead_note (rtx start_insn, rtx stop_insn)
{
  rtx p, link, next;

  for (p = next_nonnote_insn (start_insn); p != stop_insn;
       p = next_nonnote_insn (p))
    for (link = REG_NOTES (p); link; link = next)
      {
	next = XEXP (link, 1);

	if (REG_NOTE_KIND (link) != REG_DEAD
	    || !REG_P (XEXP (link, 0)))
	  continue;

	if (reg_set_p (XEXP (link, 0), PATTERN (start_insn)))
	  {
	    remove_note (p, link);
	    return;
	  }
      }
}

/* Delete any REG_UNUSED notes that exist on INSN but not on REDUNDANT_INSN.

   This handles the case of udivmodXi4 instructions which optimize their
   output depending on whether any REG_UNUSED notes are present.
   we must make sure that INSN calculates as many results as REDUNDANT_INSN
   does.  */

static void
update_reg_unused_notes (rtx insn, rtx redundant_insn)
{
  rtx link, next;

  for (link = REG_NOTES (insn); link; link = next)
    {
      next = XEXP (link, 1);

      if (REG_NOTE_KIND (link) != REG_UNUSED
	  || !REG_P (XEXP (link, 0)))
	continue;

      if (! find_regno_note (redundant_insn, REG_UNUSED,
			     REGNO (XEXP (link, 0))))
	remove_note (insn, link);
    }
}

/* Scan a function looking for insns that need a delay slot and find insns to
   put into the delay slot.

   NON_JUMPS_P is nonzero if we are to only try to fill non-jump insns (such
   as calls).  We do these first since we don't want jump insns (that are
   easier to fill) to get the only insns that could be used for non-jump insns.
   When it is zero, only try to fill JUMP_INSNs.

   When slots are filled in this manner, the insns (including the
   delay_insn) are put together in a SEQUENCE rtx.  In this fashion,
   it is possible to tell whether a delay slot has really been filled
   or not.  `final' knows how to deal with this, by communicating
   through FINAL_SEQUENCE.  */

static void
fill_simple_delay_slots (int non_jumps_p)
{
  rtx insn, pat, trial, next_trial;
  int i;
  int num_unfilled_slots = unfilled_slots_next - unfilled_slots_base;
  struct resources needed, set;
  int slots_to_fill, slots_filled;
  rtx delay_list;

  for (i = 0; i < num_unfilled_slots; i++)
    {
      int flags;
      /* Get the next insn to fill.  If it has already had any slots assigned,
	 we can't do anything with it.  Maybe we'll improve this later.  */

      insn = unfilled_slots_base[i];
      if (insn == 0
	  || INSN_DELETED_P (insn)
	  || (NONJUMP_INSN_P (insn)
	      && GET_CODE (PATTERN (insn)) == SEQUENCE)
	  || (JUMP_P (insn) && non_jumps_p)
	  || (!JUMP_P (insn) && ! non_jumps_p))
	continue;

      /* It may have been that this insn used to need delay slots, but
	 now doesn't; ignore in that case.  This can happen, for example,
	 on the HP PA RISC, where the number of delay slots depends on
	 what insns are nearby.  */
      slots_to_fill = num_delay_slots (insn);

      /* Some machine description have defined instructions to have
	 delay slots only in certain circumstances which may depend on
	 nearby insns (which change due to reorg's actions).

	 For example, the PA port normally has delay slots for unconditional
	 jumps.

	 However, the PA port claims such jumps do not have a delay slot
	 if they are immediate successors of certain CALL_INSNs.  This
	 allows the port to favor filling the delay slot of the call with
	 the unconditional jump.  */
      if (slots_to_fill == 0)
	continue;

      /* This insn needs, or can use, some delay slots.  SLOTS_TO_FILL
	 says how many.  After initialization, first try optimizing

	 call _foo		call _foo
	 nop			add %o7,.-L1,%o7
	 b,a L1
	 nop

	 If this case applies, the delay slot of the call is filled with
	 the unconditional jump.  This is done first to avoid having the
	 delay slot of the call filled in the backward scan.  Also, since
	 the unconditional jump is likely to also have a delay slot, that
	 insn must exist when it is subsequently scanned.

	 This is tried on each insn with delay slots as some machines
	 have insns which perform calls, but are not represented as
	 CALL_INSNs.  */

      slots_filled = 0;
      delay_list = 0;

      if (JUMP_P (insn))
	flags = get_jump_flags (insn, JUMP_LABEL (insn));
      else
	flags = get_jump_flags (insn, NULL_RTX);

      if ((trial = next_active_insn (insn))
	  && JUMP_P (trial)
	  && simplejump_p (trial)
	  && eligible_for_delay (insn, slots_filled, trial, flags)
	  && no_labels_between_p (insn, trial)
	  && ! can_throw_internal (trial))
	{
	  rtx *tmp;
	  slots_filled++;
	  delay_list = add_to_delay_list (trial, delay_list);

	  /* TRIAL may have had its delay slot filled, then unfilled.  When
	     the delay slot is unfilled, TRIAL is placed back on the unfilled
	     slots obstack.  Unfortunately, it is placed on the end of the
	     obstack, not in its original location.  Therefore, we must search
	     from entry i + 1 to the end of the unfilled slots obstack to
	     try and find TRIAL.  */
	  tmp = &unfilled_slots_base[i + 1];
	  while (*tmp != trial && tmp != unfilled_slots_next)
	    tmp++;

	  /* Remove the unconditional jump from consideration for delay slot
	     filling and unthread it.  */
	  if (*tmp == trial)
	    *tmp = 0;
	  {
	    rtx next = NEXT_INSN (trial);
	    rtx prev = PREV_INSN (trial);
	    if (prev)
	      NEXT_INSN (prev) = next;
	    if (next)
	      PREV_INSN (next) = prev;
	  }
	}

      /* Now, scan backwards from the insn to search for a potential
	 delay-slot candidate.  Stop searching when a label or jump is hit.

	 For each candidate, if it is to go into the delay slot (moved
	 forward in execution sequence), it must not need or set any resources
	 that were set by later insns and must not set any resources that
	 are needed for those insns.

	 The delay slot insn itself sets resources unless it is a call
	 (in which case the called routine, not the insn itself, is doing
	 the setting).  */

      if (slots_filled < slots_to_fill)
	{
	  CLEAR_RESOURCE (&needed);
	  CLEAR_RESOURCE (&set);
	  mark_set_resources (insn, &set, 0, MARK_SRC_DEST);
	  mark_referenced_resources (insn, &needed, 0);

	  for (trial = prev_nonnote_insn (insn); ! stop_search_p (trial, 1);
	       trial = next_trial)
	    {
	      next_trial = prev_nonnote_insn (trial);

	      /* This must be an INSN or CALL_INSN.  */
	      pat = PATTERN (trial);

	      /* USE and CLOBBER at this level was just for flow; ignore it.  */
	      if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
		continue;

	      /* Check for resource conflict first, to avoid unnecessary
		 splitting.  */
	      if (! insn_references_resource_p (trial, &set, 1)
		  && ! insn_sets_resource_p (trial, &set, 1)
		  && ! insn_sets_resource_p (trial, &needed, 1)
#ifdef HAVE_cc0
		  /* Can't separate set of cc0 from its use.  */
		  && ! (reg_mentioned_p (cc0_rtx, pat) && ! sets_cc0_p (pat))
#endif
		  && ! can_throw_internal (trial))
		{
		  trial = try_split (pat, trial, 1);
		  next_trial = prev_nonnote_insn (trial);
		  if (eligible_for_delay (insn, slots_filled, trial, flags))
		    {
		      /* In this case, we are searching backward, so if we
			 find insns to put on the delay list, we want
			 to put them at the head, rather than the
			 tail, of the list.  */

		      update_reg_dead_notes (trial, insn);
		      delay_list = gen_rtx_INSN_LIST (VOIDmode,
						      trial, delay_list);
		      update_block (trial, trial);
		      delete_related_insns (trial);
		      if (slots_to_fill == ++slots_filled)
			break;
		      continue;
		    }
		}

	      mark_set_resources (trial, &set, 0, MARK_SRC_DEST_CALL);
	      mark_referenced_resources (trial, &needed, 1);
	    }
	}

      /* If all needed slots haven't been filled, we come here.  */

      /* Try to optimize case of jumping around a single insn.  */
#if defined(ANNUL_IFFALSE_SLOTS) || defined(ANNUL_IFTRUE_SLOTS)
      if (slots_filled != slots_to_fill
	  && delay_list == 0
	  && JUMP_P (insn)
	  && (condjump_p (insn) || condjump_in_parallel_p (insn)))
	{
	  delay_list = optimize_skip (insn);
	  if (delay_list)
	    slots_filled += 1;
	}
#endif

      /* Try to get insns from beyond the insn needing the delay slot.
	 These insns can neither set or reference resources set in insns being
	 skipped, cannot set resources in the insn being skipped, and, if this
	 is a CALL_INSN (or a CALL_INSN is passed), cannot trap (because the
	 call might not return).

	 There used to be code which continued past the target label if
	 we saw all uses of the target label.  This code did not work,
	 because it failed to account for some instructions which were
	 both annulled and marked as from the target.  This can happen as a
	 result of optimize_skip.  Since this code was redundant with
	 fill_eager_delay_slots anyways, it was just deleted.  */

      if (slots_filled != slots_to_fill
	  /* If this instruction could throw an exception which is
	     caught in the same function, then it's not safe to fill
	     the delay slot with an instruction from beyond this
	     point.  For example, consider:

               int i = 2;

	       try {
                 f();
	         i = 3;
               } catch (...) {}

               return i;

	     Even though `i' is a local variable, we must be sure not
	     to put `i = 3' in the delay slot if `f' might throw an
	     exception.

	     Presumably, we should also check to see if we could get
	     back to this function via `setjmp'.  */
	  && ! can_throw_internal (insn)
	  && (!JUMP_P (insn)
	      || ((condjump_p (insn) || condjump_in_parallel_p (insn))
		  && ! simplejump_p (insn)
		  && JUMP_LABEL (insn) != 0)))
	{
	  /* Invariant: If insn is a JUMP_INSN, the insn's jump
	     label.  Otherwise, zero.  */
	  rtx target = 0;
	  int maybe_never = 0;
	  rtx pat, trial_delay;

	  CLEAR_RESOURCE (&needed);
	  CLEAR_RESOURCE (&set);

	  if (CALL_P (insn))
	    {
	      mark_set_resources (insn, &set, 0, MARK_SRC_DEST_CALL);
	      mark_referenced_resources (insn, &needed, 1);
	      maybe_never = 1;
	    }
	  else
	    {
	      mark_set_resources (insn, &set, 0, MARK_SRC_DEST_CALL);
	      mark_referenced_resources (insn, &needed, 1);
	      if (JUMP_P (insn))
		target = JUMP_LABEL (insn);
	    }

	  if (target == 0)
	    for (trial = next_nonnote_insn (insn); trial; trial = next_trial)
	      {
		next_trial = next_nonnote_insn (trial);

		if (LABEL_P (trial)
		    || BARRIER_P (trial))
		  break;

		/* We must have an INSN, JUMP_INSN, or CALL_INSN.  */
		pat = PATTERN (trial);

		/* Stand-alone USE and CLOBBER are just for flow.  */
		if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
		  continue;

		/* If this already has filled delay slots, get the insn needing
		   the delay slots.  */
		if (GET_CODE (pat) == SEQUENCE)
		  trial_delay = XVECEXP (pat, 0, 0);
		else
		  trial_delay = trial;

		/* Stop our search when seeing an unconditional jump.  */
		if (JUMP_P (trial_delay))
		  break;

		/* See if we have a resource problem before we try to
		   split.  */
		if (GET_CODE (pat) != SEQUENCE
		    && ! insn_references_resource_p (trial, &set, 1)
		    && ! insn_sets_resource_p (trial, &set, 1)
		    && ! insn_sets_resource_p (trial, &needed, 1)
#ifdef HAVE_cc0
		    && ! (reg_mentioned_p (cc0_rtx, pat) && ! sets_cc0_p (pat))
#endif
		    && ! (maybe_never && may_trap_or_fault_p (pat))
		    && (trial = try_split (pat, trial, 0))
		    && eligible_for_delay (insn, slots_filled, trial, flags)
		    && ! can_throw_internal(trial))
		  {
		    next_trial = next_nonnote_insn (trial);
		    delay_list = add_to_delay_list (trial, delay_list);

#ifdef HAVE_cc0
		    if (reg_mentioned_p (cc0_rtx, pat))
		      link_cc0_insns (trial);
#endif

		    delete_related_insns (trial);
		    if (slots_to_fill == ++slots_filled)
		      break;
		    continue;
		  }

		mark_set_resources (trial, &set, 0, MARK_SRC_DEST_CALL);
		mark_referenced_resources (trial, &needed, 1);

		/* Ensure we don't put insns between the setting of cc and the
		   comparison by moving a setting of cc into an earlier delay
		   slot since these insns could clobber the condition code.  */
		set.cc = 1;

		/* If this is a call or jump, we might not get here.  */
		if (CALL_P (trial_delay)
		    || JUMP_P (trial_delay))
		  maybe_never = 1;
	      }

	  /* If there are slots left to fill and our search was stopped by an
	     unconditional branch, try the insn at the branch target.  We can
	     redirect the branch if it works.

	     Don't do this if the insn at the branch target is a branch.  */
	  if (slots_to_fill != slots_filled
	      && trial
	      && JUMP_P (trial)
	      && simplejump_p (trial)
	      && (target == 0 || JUMP_LABEL (trial) == target)
	      && (next_trial = next_active_insn (JUMP_LABEL (trial))) != 0
	      && ! (NONJUMP_INSN_P (next_trial)
		    && GET_CODE (PATTERN (next_trial)) == SEQUENCE)
	      && !JUMP_P (next_trial)
	      && ! insn_references_resource_p (next_trial, &set, 1)
	      && ! insn_sets_resource_p (next_trial, &set, 1)
	      && ! insn_sets_resource_p (next_trial, &needed, 1)
#ifdef HAVE_cc0
	      && ! reg_mentioned_p (cc0_rtx, PATTERN (next_trial))
#endif
	      && ! (maybe_never && may_trap_or_fault_p (PATTERN (next_trial)))
	      && (next_trial = try_split (PATTERN (next_trial), next_trial, 0))
	      && eligible_for_delay (insn, slots_filled, next_trial, flags)
	      && ! can_throw_internal (trial))
	    {
	      /* See comment in relax_delay_slots about necessity of using
		 next_real_insn here.  */
	      rtx new_label = next_real_insn (next_trial);

	      if (new_label != 0)
		new_label = get_label_before (new_label);
	      else
		new_label = find_end_label ();

	      if (new_label)
	        {
		  delay_list
		    = add_to_delay_list (copy_rtx (next_trial), delay_list);
		  slots_filled++;
		  reorg_redirect_jump (trial, new_label);

		  /* If we merged because we both jumped to the same place,
		     redirect the original insn also.  */
		  if (target)
		    reorg_redirect_jump (insn, new_label);
		}
	    }
	}

      /* If this is an unconditional jump, then try to get insns from the
	 target of the jump.  */
      if (JUMP_P (insn)
	  && simplejump_p (insn)
	  && slots_filled != slots_to_fill)
	delay_list
	  = fill_slots_from_thread (insn, const_true_rtx,
				    next_active_insn (JUMP_LABEL (insn)),
				    NULL, 1, 1,
				    own_thread_p (JUMP_LABEL (insn),
						  JUMP_LABEL (insn), 0),
				    slots_to_fill, &slots_filled,
				    delay_list);

      if (delay_list)
	unfilled_slots_base[i]
	  = emit_delay_sequence (insn, delay_list, slots_filled);

      if (slots_to_fill == slots_filled)
	unfilled_slots_base[i] = 0;

      note_delay_statistics (slots_filled, 0);
    }

#ifdef DELAY_SLOTS_FOR_EPILOGUE
  /* See if the epilogue needs any delay slots.  Try to fill them if so.
     The only thing we can do is scan backwards from the end of the
     function.  If we did this in a previous pass, it is incorrect to do it
     again.  */
  if (current_function_epilogue_delay_list)
    return;

  slots_to_fill = DELAY_SLOTS_FOR_EPILOGUE;
  if (slots_to_fill == 0)
    return;

  slots_filled = 0;
  CLEAR_RESOURCE (&set);

  /* The frame pointer and stack pointer are needed at the beginning of
     the epilogue, so instructions setting them can not be put in the
     epilogue delay slot.  However, everything else needed at function
     end is safe, so we don't want to use end_of_function_needs here.  */
  CLEAR_RESOURCE (&needed);
  if (frame_pointer_needed)
    {
      SET_HARD_REG_BIT (needed.regs, FRAME_POINTER_REGNUM);
#if HARD_FRAME_POINTER_REGNUM != FRAME_POINTER_REGNUM
      SET_HARD_REG_BIT (needed.regs, HARD_FRAME_POINTER_REGNUM);
#endif
      if (! EXIT_IGNORE_STACK
	  || current_function_sp_is_unchanging)
	SET_HARD_REG_BIT (needed.regs, STACK_POINTER_REGNUM);
    }
  else
    SET_HARD_REG_BIT (needed.regs, STACK_POINTER_REGNUM);

#ifdef EPILOGUE_USES
  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    {
      if (EPILOGUE_USES (i))
	SET_HARD_REG_BIT (needed.regs, i);
    }
#endif

  for (trial = get_last_insn (); ! stop_search_p (trial, 1);
       trial = PREV_INSN (trial))
    {
      if (NOTE_P (trial))
	continue;
      pat = PATTERN (trial);
      if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
	continue;

      if (! insn_references_resource_p (trial, &set, 1)
	  && ! insn_sets_resource_p (trial, &needed, 1)
	  && ! insn_sets_resource_p (trial, &set, 1)
#ifdef HAVE_cc0
	  /* Don't want to mess with cc0 here.  */
	  && ! reg_mentioned_p (cc0_rtx, pat)
#endif
	  && ! can_throw_internal (trial))
	{
	  trial = try_split (pat, trial, 1);
	  if (ELIGIBLE_FOR_EPILOGUE_DELAY (trial, slots_filled))
	    {
	      /* Here as well we are searching backward, so put the
		 insns we find on the head of the list.  */

	      current_function_epilogue_delay_list
		= gen_rtx_INSN_LIST (VOIDmode, trial,
				     current_function_epilogue_delay_list);
	      mark_end_of_function_resources (trial, 1);
	      update_block (trial, trial);
	      delete_related_insns (trial);

	      /* Clear deleted bit so final.c will output the insn.  */
	      INSN_DELETED_P (trial) = 0;

	      if (slots_to_fill == ++slots_filled)
		break;
	      continue;
	    }
	}

      mark_set_resources (trial, &set, 0, MARK_SRC_DEST_CALL);
      mark_referenced_resources (trial, &needed, 1);
    }

  note_delay_statistics (slots_filled, 0);
#endif
}

/* Try to find insns to place in delay slots.

   INSN is the jump needing SLOTS_TO_FILL delay slots.  It tests CONDITION
   or is an unconditional branch if CONDITION is const_true_rtx.
   *PSLOTS_FILLED is updated with the number of slots that we have filled.

   THREAD is a flow-of-control, either the insns to be executed if the
   branch is true or if the branch is false, THREAD_IF_TRUE says which.

   OPPOSITE_THREAD is the thread in the opposite direction.  It is used
   to see if any potential delay slot insns set things needed there.

   LIKELY is nonzero if it is extremely likely that the branch will be
   taken and THREAD_IF_TRUE is set.  This is used for the branch at the
   end of a loop back up to the top.

   OWN_THREAD and OWN_OPPOSITE_THREAD are true if we are the only user of the
   thread.  I.e., it is the fallthrough code of our jump or the target of the
   jump when we are the only jump going there.

   If OWN_THREAD is false, it must be the "true" thread of a jump.  In that
   case, we can only take insns from the head of the thread for our delay
   slot.  We then adjust the jump to point after the insns we have taken.  */

static rtx
fill_slots_from_thread (rtx insn, rtx condition, rtx thread,
			rtx opposite_thread, int likely, int thread_if_true,
			int own_thread, int slots_to_fill,
			int *pslots_filled, rtx delay_list)
{
  rtx new_thread;
  struct resources opposite_needed, set, needed;
  rtx trial;
  int lose = 0;
  int must_annul = 0;
  int flags;

  /* Validate our arguments.  */
  gcc_assert(condition != const_true_rtx || thread_if_true);
  gcc_assert(own_thread || thread_if_true);

  flags = get_jump_flags (insn, JUMP_LABEL (insn));

  /* If our thread is the end of subroutine, we can't get any delay
     insns from that.  */
  if (thread == 0)
    return delay_list;

  /* If this is an unconditional branch, nothing is needed at the
     opposite thread.  Otherwise, compute what is needed there.  */
  if (condition == const_true_rtx)
    CLEAR_RESOURCE (&opposite_needed);
  else
    mark_target_live_regs (get_insns (), opposite_thread, &opposite_needed);

  /* If the insn at THREAD can be split, do it here to avoid having to
     update THREAD and NEW_THREAD if it is done in the loop below.  Also
     initialize NEW_THREAD.  */

  new_thread = thread = try_split (PATTERN (thread), thread, 0);

  /* Scan insns at THREAD.  We are looking for an insn that can be removed
     from THREAD (it neither sets nor references resources that were set
     ahead of it and it doesn't set anything needs by the insns ahead of
     it) and that either can be placed in an annulling insn or aren't
     needed at OPPOSITE_THREAD.  */

  CLEAR_RESOURCE (&needed);
  CLEAR_RESOURCE (&set);

  /* If we do not own this thread, we must stop as soon as we find
     something that we can't put in a delay slot, since all we can do
     is branch into THREAD at a later point.  Therefore, labels stop
     the search if this is not the `true' thread.  */

  for (trial = thread;
       ! stop_search_p (trial, ! thread_if_true) && (! lose || own_thread);
       trial = next_nonnote_insn (trial))
    {
      rtx pat, old_trial;

      /* If we have passed a label, we no longer own this thread.  */
      if (LABEL_P (trial))
	{
	  own_thread = 0;
	  continue;
	}

      pat = PATTERN (trial);
      if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
	continue;

      /* If TRIAL conflicts with the insns ahead of it, we lose.  Also,
	 don't separate or copy insns that set and use CC0.  */
      if (! insn_references_resource_p (trial, &set, 1)
	  && ! insn_sets_resource_p (trial, &set, 1)
	  && ! insn_sets_resource_p (trial, &needed, 1)
#ifdef HAVE_cc0
	  && ! (reg_mentioned_p (cc0_rtx, pat)
		&& (! own_thread || ! sets_cc0_p (pat)))
#endif
	  && ! can_throw_internal (trial))
	{
	  rtx prior_insn;

	  /* If TRIAL is redundant with some insn before INSN, we don't
	     actually need to add it to the delay list; we can merely pretend
	     we did.  */
	  if ((prior_insn = redundant_insn (trial, insn, delay_list)))
	    {
	      fix_reg_dead_note (prior_insn, insn);
	      if (own_thread)
		{
		  update_block (trial, thread);
		  if (trial == thread)
		    {
		      thread = next_active_insn (thread);
		      if (new_thread == trial)
			new_thread = thread;
		    }

		  delete_related_insns (trial);
		}
	      else
		{
		  update_reg_unused_notes (prior_insn, trial);
		  new_thread = next_active_insn (trial);
		}

	      continue;
	    }

	  /* There are two ways we can win:  If TRIAL doesn't set anything
	     needed at the opposite thread and can't trap, or if it can
	     go into an annulled delay slot.  */
	  if (!must_annul
	      && (condition == const_true_rtx
	          || (! insn_sets_resource_p (trial, &opposite_needed, 1)
		      && ! may_trap_or_fault_p (pat))))
	    {
	      old_trial = trial;
	      trial = try_split (pat, trial, 0);
	      if (new_thread == old_trial)
		new_thread = trial;
	      if (thread == old_trial)
		thread = trial;
	      pat = PATTERN (trial);
	      if (eligible_for_delay (insn, *pslots_filled, trial, flags))
		goto winner;
	    }
	  else if (0
#ifdef ANNUL_IFTRUE_SLOTS
		   || ! thread_if_true
#endif
#ifdef ANNUL_IFFALSE_SLOTS
		   || thread_if_true
#endif
		   )
	    {
	      old_trial = trial;
	      trial = try_split (pat, trial, 0);
	      if (new_thread == old_trial)
		new_thread = trial;
	      if (thread == old_trial)
		thread = trial;
	      pat = PATTERN (trial);
	      if ((must_annul || delay_list == NULL) && (thread_if_true
		   ? check_annul_list_true_false (0, delay_list)
		     && eligible_for_annul_false (insn, *pslots_filled, trial, flags)
		   : check_annul_list_true_false (1, delay_list)
		     && eligible_for_annul_true (insn, *pslots_filled, trial, flags)))
		{
		  rtx temp;

		  must_annul = 1;
		winner:

#ifdef HAVE_cc0
		  if (reg_mentioned_p (cc0_rtx, pat))
		    link_cc0_insns (trial);
#endif

		  /* If we own this thread, delete the insn.  If this is the
		     destination of a branch, show that a basic block status
		     may have been updated.  In any case, mark the new
		     starting point of this thread.  */
		  if (own_thread)
		    {
		      rtx note;

		      update_block (trial, thread);
		      if (trial == thread)
			{
			  thread = next_active_insn (thread);
			  if (new_thread == trial)
			    new_thread = thread;
			}

		      /* We are moving this insn, not deleting it.  We must
			 temporarily increment the use count on any referenced
			 label lest it be deleted by delete_related_insns.  */
		      note = find_reg_note (trial, REG_LABEL, 0);
		      /* REG_LABEL could be NOTE_INSN_DELETED_LABEL too.  */
		      if (note && LABEL_P (XEXP (note, 0)))
			LABEL_NUSES (XEXP (note, 0))++;

		      delete_related_insns (trial);

		      if (note && LABEL_P (XEXP (note, 0)))
			LABEL_NUSES (XEXP (note, 0))--;
		    }
		  else
		    new_thread = next_active_insn (trial);

		  temp = own_thread ? trial : copy_rtx (trial);
		  if (thread_if_true)
		    INSN_FROM_TARGET_P (temp) = 1;

		  delay_list = add_to_delay_list (temp, delay_list);

		  if (slots_to_fill == ++(*pslots_filled))
		    {
		      /* Even though we have filled all the slots, we
			 may be branching to a location that has a
			 redundant insn.  Skip any if so.  */
		      while (new_thread && ! own_thread
			     && ! insn_sets_resource_p (new_thread, &set, 1)
			     && ! insn_sets_resource_p (new_thread, &needed, 1)
			     && ! insn_references_resource_p (new_thread,
							      &set, 1)
			     && (prior_insn
				 = redundant_insn (new_thread, insn,
						   delay_list)))
			{
			  /* We know we do not own the thread, so no need
			     to call update_block and delete_insn.  */
			  fix_reg_dead_note (prior_insn, insn);
			  update_reg_unused_notes (prior_insn, new_thread);
			  new_thread = next_active_insn (new_thread);
			}
		      break;
		    }

		  continue;
		}
	    }
	}

      /* This insn can't go into a delay slot.  */
      lose = 1;
      mark_set_resources (trial, &set, 0, MARK_SRC_DEST_CALL);
      mark_referenced_resources (trial, &needed, 1);

      /* Ensure we don't put insns between the setting of cc and the comparison
	 by moving a setting of cc into an earlier delay slot since these insns
	 could clobber the condition code.  */
      set.cc = 1;

      /* If this insn is a register-register copy and the next insn has
	 a use of our destination, change it to use our source.  That way,
	 it will become a candidate for our delay slot the next time
	 through this loop.  This case occurs commonly in loops that
	 scan a list.

	 We could check for more complex cases than those tested below,
	 but it doesn't seem worth it.  It might also be a good idea to try
	 to swap the two insns.  That might do better.

	 We can't do this if the next insn modifies our destination, because
	 that would make the replacement into the insn invalid.  We also can't
	 do this if it modifies our source, because it might be an earlyclobber
	 operand.  This latter test also prevents updating the contents of
	 a PRE_INC.  We also can't do this if there's overlap of source and
	 destination.  Overlap may happen for larger-than-register-size modes.  */

      if (NONJUMP_INSN_P (trial) && GET_CODE (pat) == SET
	  && REG_P (SET_SRC (pat))
	  && REG_P (SET_DEST (pat))
	  && !reg_overlap_mentioned_p (SET_DEST (pat), SET_SRC (pat)))
	{
	  rtx next = next_nonnote_insn (trial);

	  if (next && NONJUMP_INSN_P (next)
	      && GET_CODE (PATTERN (next)) != USE
	      && ! reg_set_p (SET_DEST (pat), next)
	      && ! reg_set_p (SET_SRC (pat), next)
	      && reg_referenced_p (SET_DEST (pat), PATTERN (next))
	      && ! modified_in_p (SET_DEST (pat), next))
	    validate_replace_rtx (SET_DEST (pat), SET_SRC (pat), next);
	}
    }

  /* If we stopped on a branch insn that has delay slots, see if we can
     steal some of the insns in those slots.  */
  if (trial && NONJUMP_INSN_P (trial)
      && GET_CODE (PATTERN (trial)) == SEQUENCE
      && JUMP_P (XVECEXP (PATTERN (trial), 0, 0)))
    {
      /* If this is the `true' thread, we will want to follow the jump,
	 so we can only do this if we have taken everything up to here.  */
      if (thread_if_true && trial == new_thread)
	{
	  delay_list
	    = steal_delay_list_from_target (insn, condition, PATTERN (trial),
					    delay_list, &set, &needed,
					    &opposite_needed, slots_to_fill,
					    pslots_filled, &must_annul,
					    &new_thread);
	  /* If we owned the thread and are told that it branched
	     elsewhere, make sure we own the thread at the new location.  */
	  if (own_thread && trial != new_thread)
	    own_thread = own_thread_p (new_thread, new_thread, 0);
	}
      else if (! thread_if_true)
	delay_list
	  = steal_delay_list_from_fallthrough (insn, condition,
					       PATTERN (trial),
					       delay_list, &set, &needed,
					       &opposite_needed, slots_to_fill,
					       pslots_filled, &must_annul);
    }

  /* If we haven't found anything for this delay slot and it is very
     likely that the branch will be taken, see if the insn at our target
     increments or decrements a register with an increment that does not
     depend on the destination register.  If so, try to place the opposite
     arithmetic insn after the jump insn and put the arithmetic insn in the
     delay slot.  If we can't do this, return.  */
  if (delay_list == 0 && likely && new_thread
      && NONJUMP_INSN_P (new_thread)
      && GET_CODE (PATTERN (new_thread)) != ASM_INPUT
      && asm_noperands (PATTERN (new_thread)) < 0)
    {
      rtx pat = PATTERN (new_thread);
      rtx dest;
      rtx src;

      trial = new_thread;
      pat = PATTERN (trial);

      if (!NONJUMP_INSN_P (trial)
	  || GET_CODE (pat) != SET
	  || ! eligible_for_delay (insn, 0, trial, flags)
	  || can_throw_internal (trial))
	return 0;

      dest = SET_DEST (pat), src = SET_SRC (pat);
      if ((GET_CODE (src) == PLUS || GET_CODE (src) == MINUS)
	  && rtx_equal_p (XEXP (src, 0), dest)
	  && (!FLOAT_MODE_P (GET_MODE (src))
	      || flag_unsafe_math_optimizations)
	  && ! reg_overlap_mentioned_p (dest, XEXP (src, 1))
	  && ! side_effects_p (pat))
	{
	  rtx other = XEXP (src, 1);
	  rtx new_arith;
	  rtx ninsn;

	  /* If this is a constant adjustment, use the same code with
	     the negated constant.  Otherwise, reverse the sense of the
	     arithmetic.  */
	  if (GET_CODE (other) == CONST_INT)
	    new_arith = gen_rtx_fmt_ee (GET_CODE (src), GET_MODE (src), dest,
					negate_rtx (GET_MODE (src), other));
	  else
	    new_arith = gen_rtx_fmt_ee (GET_CODE (src) == PLUS ? MINUS : PLUS,
					GET_MODE (src), dest, other);

	  ninsn = emit_insn_after (gen_rtx_SET (VOIDmode, dest, new_arith),
				   insn);

	  if (recog_memoized (ninsn) < 0
	      || (extract_insn (ninsn), ! constrain_operands (1)))
	    {
	      delete_related_insns (ninsn);
	      return 0;
	    }

	  if (own_thread)
	    {
	      update_block (trial, thread);
	      if (trial == thread)
		{
		  thread = next_active_insn (thread);
		  if (new_thread == trial)
		    new_thread = thread;
		}
	      delete_related_insns (trial);
	    }
	  else
	    new_thread = next_active_insn (trial);

	  ninsn = own_thread ? trial : copy_rtx (trial);
	  if (thread_if_true)
	    INSN_FROM_TARGET_P (ninsn) = 1;

	  delay_list = add_to_delay_list (ninsn, NULL_RTX);
	  (*pslots_filled)++;
	}
    }

  if (delay_list && must_annul)
    INSN_ANNULLED_BRANCH_P (insn) = 1;

  /* If we are to branch into the middle of this thread, find an appropriate
     label or make a new one if none, and redirect INSN to it.  If we hit the
     end of the function, use the end-of-function label.  */
  if (new_thread != thread)
    {
      rtx label;

      gcc_assert (thread_if_true);

      if (new_thread && JUMP_P (new_thread)
	  && (simplejump_p (new_thread)
	      || GET_CODE (PATTERN (new_thread)) == RETURN)
	  && redirect_with_delay_list_safe_p (insn,
					      JUMP_LABEL (new_thread),
					      delay_list))
	new_thread = follow_jumps (JUMP_LABEL (new_thread));

      if (new_thread == 0)
	label = find_end_label ();
      else if (LABEL_P (new_thread))
	label = new_thread;
      else
	label = get_label_before (new_thread);

      if (label)
	reorg_redirect_jump (insn, label);
    }

  return delay_list;
}

/* Make another attempt to find insns to place in delay slots.

   We previously looked for insns located in front of the delay insn
   and, for non-jump delay insns, located behind the delay insn.

   Here only try to schedule jump insns and try to move insns from either
   the target or the following insns into the delay slot.  If annulling is
   supported, we will be likely to do this.  Otherwise, we can do this only
   if safe.  */

static void
fill_eager_delay_slots (void)
{
  rtx insn;
  int i;
  int num_unfilled_slots = unfilled_slots_next - unfilled_slots_base;

  for (i = 0; i < num_unfilled_slots; i++)
    {
      rtx condition;
      rtx target_label, insn_at_target, fallthrough_insn;
      rtx delay_list = 0;
      int own_target;
      int own_fallthrough;
      int prediction, slots_to_fill, slots_filled;

      insn = unfilled_slots_base[i];
      if (insn == 0
	  || INSN_DELETED_P (insn)
	  || !JUMP_P (insn)
	  || ! (condjump_p (insn) || condjump_in_parallel_p (insn)))
	continue;

      slots_to_fill = num_delay_slots (insn);
      /* Some machine description have defined instructions to have
	 delay slots only in certain circumstances which may depend on
	 nearby insns (which change due to reorg's actions).

	 For example, the PA port normally has delay slots for unconditional
	 jumps.

	 However, the PA port claims such jumps do not have a delay slot
	 if they are immediate successors of certain CALL_INSNs.  This
	 allows the port to favor filling the delay slot of the call with
	 the unconditional jump.  */
      if (slots_to_fill == 0)
	continue;

      slots_filled = 0;
      target_label = JUMP_LABEL (insn);
      condition = get_branch_condition (insn, target_label);

      if (condition == 0)
	continue;

      /* Get the next active fallthrough and target insns and see if we own
	 them.  Then see whether the branch is likely true.  We don't need
	 to do a lot of this for unconditional branches.  */

      insn_at_target = next_active_insn (target_label);
      own_target = own_thread_p (target_label, target_label, 0);

      if (condition == const_true_rtx)
	{
	  own_fallthrough = 0;
	  fallthrough_insn = 0;
	  prediction = 2;
	}
      else
	{
	  fallthrough_insn = next_active_insn (insn);
	  own_fallthrough = own_thread_p (NEXT_INSN (insn), NULL_RTX, 1);
	  prediction = mostly_true_jump (insn, condition);
	}

      /* If this insn is expected to branch, first try to get insns from our
	 target, then our fallthrough insns.  If it is not expected to branch,
	 try the other order.  */

      if (prediction > 0)
	{
	  delay_list
	    = fill_slots_from_thread (insn, condition, insn_at_target,
				      fallthrough_insn, prediction == 2, 1,
				      own_target,
				      slots_to_fill, &slots_filled, delay_list);

	  if (delay_list == 0 && own_fallthrough)
	    {
	      /* Even though we didn't find anything for delay slots,
		 we might have found a redundant insn which we deleted
		 from the thread that was filled.  So we have to recompute
		 the next insn at the target.  */
	      target_label = JUMP_LABEL (insn);
	      insn_at_target = next_active_insn (target_label);

	      delay_list
		= fill_slots_from_thread (insn, condition, fallthrough_insn,
					  insn_at_target, 0, 0,
					  own_fallthrough,
					  slots_to_fill, &slots_filled,
					  delay_list);
	    }
	}
      else
	{
	  if (own_fallthrough)
	    delay_list
	      = fill_slots_from_thread (insn, condition, fallthrough_insn,
					insn_at_target, 0, 0,
					own_fallthrough,
					slots_to_fill, &slots_filled,
					delay_list);

	  if (delay_list == 0)
	    delay_list
	      = fill_slots_from_thread (insn, condition, insn_at_target,
					next_active_insn (insn), 0, 1,
					own_target,
					slots_to_fill, &slots_filled,
					delay_list);
	}

      if (delay_list)
	unfilled_slots_base[i]
	  = emit_delay_sequence (insn, delay_list, slots_filled);

      if (slots_to_fill == slots_filled)
	unfilled_slots_base[i] = 0;

      note_delay_statistics (slots_filled, 1);
    }
}

/* Once we have tried two ways to fill a delay slot, make a pass over the
   code to try to improve the results and to do such things as more jump
   threading.  */

static void
relax_delay_slots (rtx first)
{
  rtx insn, next, pat;
  rtx trial, delay_insn, target_label;

  /* Look at every JUMP_INSN and see if we can improve it.  */
  for (insn = first; insn; insn = next)
    {
      rtx other;

      next = next_active_insn (insn);

      /* If this is a jump insn, see if it now jumps to a jump, jumps to
	 the next insn, or jumps to a label that is not the last of a
	 group of consecutive labels.  */
      if (JUMP_P (insn)
	  && (condjump_p (insn) || condjump_in_parallel_p (insn))
	  && (target_label = JUMP_LABEL (insn)) != 0)
	{
	  target_label = skip_consecutive_labels (follow_jumps (target_label));
	  if (target_label == 0)
	    target_label = find_end_label ();

	  if (target_label && next_active_insn (target_label) == next
	      && ! condjump_in_parallel_p (insn))
	    {
	      delete_jump (insn);
	      continue;
	    }

	  if (target_label && target_label != JUMP_LABEL (insn))
	    reorg_redirect_jump (insn, target_label);

	  /* See if this jump conditionally branches around an unconditional
	     jump.  If so, invert this jump and point it to the target of the
	     second jump.  */
	  if (next && JUMP_P (next)
	      && any_condjump_p (insn)
	      && (simplejump_p (next) || GET_CODE (PATTERN (next)) == RETURN)
	      && target_label
	      && next_active_insn (target_label) == next_active_insn (next)
	      && no_labels_between_p (insn, next))
	    {
	      rtx label = JUMP_LABEL (next);

	      /* Be careful how we do this to avoid deleting code or
		 labels that are momentarily dead.  See similar optimization
		 in jump.c.

		 We also need to ensure we properly handle the case when
		 invert_jump fails.  */

	      ++LABEL_NUSES (target_label);
	      if (label)
		++LABEL_NUSES (label);

	      if (invert_jump (insn, label, 1))
		{
		  delete_related_insns (next);
		  next = insn;
		}

	      if (label)
		--LABEL_NUSES (label);

	      if (--LABEL_NUSES (target_label) == 0)
		delete_related_insns (target_label);

	      continue;
	    }
	}

      /* If this is an unconditional jump and the previous insn is a
	 conditional jump, try reversing the condition of the previous
	 insn and swapping our targets.  The next pass might be able to
	 fill the slots.

	 Don't do this if we expect the conditional branch to be true, because
	 we would then be making the more common case longer.  */

      if (JUMP_P (insn)
	  && (simplejump_p (insn) || GET_CODE (PATTERN (insn)) == RETURN)
	  && (other = prev_active_insn (insn)) != 0
	  && any_condjump_p (other)
	  && no_labels_between_p (other, insn)
	  && 0 > mostly_true_jump (other,
				   get_branch_condition (other,
							 JUMP_LABEL (other))))
	{
	  rtx other_target = JUMP_LABEL (other);
	  target_label = JUMP_LABEL (insn);

	  if (invert_jump (other, target_label, 0))
	    reorg_redirect_jump (insn, other_target);
	}

      /* Now look only at cases where we have filled a delay slot.  */
      if (!NONJUMP_INSN_P (insn)
	  || GET_CODE (PATTERN (insn)) != SEQUENCE)
	continue;

      pat = PATTERN (insn);
      delay_insn = XVECEXP (pat, 0, 0);

      /* See if the first insn in the delay slot is redundant with some
	 previous insn.  Remove it from the delay slot if so; then set up
	 to reprocess this insn.  */
      if (redundant_insn (XVECEXP (pat, 0, 1), delay_insn, 0))
	{
	  delete_from_delay_slot (XVECEXP (pat, 0, 1));
	  next = prev_active_insn (next);
	  continue;
	}

      /* See if we have a RETURN insn with a filled delay slot followed
	 by a RETURN insn with an unfilled a delay slot.  If so, we can delete
	 the first RETURN (but not its delay insn).  This gives the same
	 effect in fewer instructions.

	 Only do so if optimizing for size since this results in slower, but
	 smaller code.  */
      if (optimize_size
	  && GET_CODE (PATTERN (delay_insn)) == RETURN
	  && next
	  && JUMP_P (next)
	  && GET_CODE (PATTERN (next)) == RETURN)
	{
	  rtx after;
	  int i;

	  /* Delete the RETURN and just execute the delay list insns.

	     We do this by deleting the INSN containing the SEQUENCE, then
	     re-emitting the insns separately, and then deleting the RETURN.
	     This allows the count of the jump target to be properly
	     decremented.  */

	  /* Clear the from target bit, since these insns are no longer
	     in delay slots.  */
	  for (i = 0; i < XVECLEN (pat, 0); i++)
	    INSN_FROM_TARGET_P (XVECEXP (pat, 0, i)) = 0;

	  trial = PREV_INSN (insn);
	  delete_related_insns (insn);
	  gcc_assert (GET_CODE (pat) == SEQUENCE);
	  after = trial;
	  for (i = 0; i < XVECLEN (pat, 0); i++)
	    {
	      rtx this_insn = XVECEXP (pat, 0, i);
	      add_insn_after (this_insn, after);
	      after = this_insn;
	    }
	  delete_scheduled_jump (delay_insn);
	  continue;
	}

      /* Now look only at the cases where we have a filled JUMP_INSN.  */
      if (!JUMP_P (XVECEXP (PATTERN (insn), 0, 0))
	  || ! (condjump_p (XVECEXP (PATTERN (insn), 0, 0))
		|| condjump_in_parallel_p (XVECEXP (PATTERN (insn), 0, 0))))
	continue;

      target_label = JUMP_LABEL (delay_insn);

      if (target_label)
	{
	  /* If this jump goes to another unconditional jump, thread it, but
	     don't convert a jump into a RETURN here.  */
	  trial = skip_consecutive_labels (follow_jumps (target_label));
	  if (trial == 0)
	    trial = find_end_label ();

	  if (trial && trial != target_label
	      && redirect_with_delay_slots_safe_p (delay_insn, trial, insn))
	    {
	      reorg_redirect_jump (delay_insn, trial);
	      target_label = trial;
	    }

	  /* If the first insn at TARGET_LABEL is redundant with a previous
	     insn, redirect the jump to the following insn process again.  */
	  trial = next_active_insn (target_label);
	  if (trial && GET_CODE (PATTERN (trial)) != SEQUENCE
	      && redundant_insn (trial, insn, 0)
	      && ! can_throw_internal (trial))
	    {
	      /* Figure out where to emit the special USE insn so we don't
		 later incorrectly compute register live/death info.  */
	      rtx tmp = next_active_insn (trial);
	      if (tmp == 0)
		tmp = find_end_label ();

	      if (tmp)
	        {
		  /* Insert the special USE insn and update dataflow info.  */
		  update_block (trial, tmp);

		  /* Now emit a label before the special USE insn, and
		     redirect our jump to the new label.  */
		  target_label = get_label_before (PREV_INSN (tmp));
		  reorg_redirect_jump (delay_insn, target_label);
		  next = insn;
		  continue;
		}
	    }

	  /* Similarly, if it is an unconditional jump with one insn in its
	     delay list and that insn is redundant, thread the jump.  */
	  if (trial && GET_CODE (PATTERN (trial)) == SEQUENCE
	      && XVECLEN (PATTERN (trial), 0) == 2
	      && JUMP_P (XVECEXP (PATTERN (trial), 0, 0))
	      && (simplejump_p (XVECEXP (PATTERN (trial), 0, 0))
		  || GET_CODE (PATTERN (XVECEXP (PATTERN (trial), 0, 0))) == RETURN)
	      && redundant_insn (XVECEXP (PATTERN (trial), 0, 1), insn, 0))
	    {
	      target_label = JUMP_LABEL (XVECEXP (PATTERN (trial), 0, 0));
	      if (target_label == 0)
		target_label = find_end_label ();

	      if (target_label
	          && redirect_with_delay_slots_safe_p (delay_insn, target_label,
						       insn))
		{
		  reorg_redirect_jump (delay_insn, target_label);
		  next = insn;
		  continue;
		}
	    }
	}

      if (! INSN_ANNULLED_BRANCH_P (delay_insn)
	  && prev_active_insn (target_label) == insn
	  && ! condjump_in_parallel_p (delay_insn)
#ifdef HAVE_cc0
	  /* If the last insn in the delay slot sets CC0 for some insn,
	     various code assumes that it is in a delay slot.  We could
	     put it back where it belonged and delete the register notes,
	     but it doesn't seem worthwhile in this uncommon case.  */
	  && ! find_reg_note (XVECEXP (pat, 0, XVECLEN (pat, 0) - 1),
			      REG_CC_USER, NULL_RTX)
#endif
	  )
	{
	  rtx after;
	  int i;

	  /* All this insn does is execute its delay list and jump to the
	     following insn.  So delete the jump and just execute the delay
	     list insns.

	     We do this by deleting the INSN containing the SEQUENCE, then
	     re-emitting the insns separately, and then deleting the jump.
	     This allows the count of the jump target to be properly
	     decremented.  */

	  /* Clear the from target bit, since these insns are no longer
	     in delay slots.  */
	  for (i = 0; i < XVECLEN (pat, 0); i++)
	    INSN_FROM_TARGET_P (XVECEXP (pat, 0, i)) = 0;

	  trial = PREV_INSN (insn);
	  delete_related_insns (insn);
	  gcc_assert (GET_CODE (pat) == SEQUENCE);
	  after = trial;
	  for (i = 0; i < XVECLEN (pat, 0); i++)
	    {
	      rtx this_insn = XVECEXP (pat, 0, i);
	      add_insn_after (this_insn, after);
	      after = this_insn;
	    }
	  delete_scheduled_jump (delay_insn);
	  continue;
	}

      /* See if this is an unconditional jump around a single insn which is
	 identical to the one in its delay slot.  In this case, we can just
	 delete the branch and the insn in its delay slot.  */
      if (next && NONJUMP_INSN_P (next)
	  && prev_label (next_active_insn (next)) == target_label
	  && simplejump_p (insn)
	  && XVECLEN (pat, 0) == 2
	  && rtx_equal_p (PATTERN (next), PATTERN (XVECEXP (pat, 0, 1))))
	{
	  delete_related_insns (insn);
	  continue;
	}

      /* See if this jump (with its delay slots) conditionally branches
	 around an unconditional jump (without delay slots).  If so, invert
	 this jump and point it to the target of the second jump.  We cannot
	 do this for annulled jumps, though.  Again, don't convert a jump to
	 a RETURN here.  */
      if (! INSN_ANNULLED_BRANCH_P (delay_insn)
	  && any_condjump_p (delay_insn)
	  && next && JUMP_P (next)
	  && (simplejump_p (next) || GET_CODE (PATTERN (next)) == RETURN)
	  && next_active_insn (target_label) == next_active_insn (next)
	  && no_labels_between_p (insn, next))
	{
	  rtx label = JUMP_LABEL (next);
	  rtx old_label = JUMP_LABEL (delay_insn);

	  if (label == 0)
	    label = find_end_label ();

	  /* find_end_label can generate a new label. Check this first.  */
	  if (label
	      && no_labels_between_p (insn, next)
	      && redirect_with_delay_slots_safe_p (delay_insn, label, insn))
	    {
	      /* Be careful how we do this to avoid deleting code or labels
		 that are momentarily dead.  See similar optimization in
		 jump.c  */
	      if (old_label)
		++LABEL_NUSES (old_label);

	      if (invert_jump (delay_insn, label, 1))
		{
		  int i;

		  /* Must update the INSN_FROM_TARGET_P bits now that
		     the branch is reversed, so that mark_target_live_regs
		     will handle the delay slot insn correctly.  */
		  for (i = 1; i < XVECLEN (PATTERN (insn), 0); i++)
		    {
		      rtx slot = XVECEXP (PATTERN (insn), 0, i);
		      INSN_FROM_TARGET_P (slot) = ! INSN_FROM_TARGET_P (slot);
		    }

		  delete_related_insns (next);
		  next = insn;
		}

	      if (old_label && --LABEL_NUSES (old_label) == 0)
		delete_related_insns (old_label);
	      continue;
	    }
	}

      /* If we own the thread opposite the way this insn branches, see if we
	 can merge its delay slots with following insns.  */
      if (INSN_FROM_TARGET_P (XVECEXP (pat, 0, 1))
	  && own_thread_p (NEXT_INSN (insn), 0, 1))
	try_merge_delay_insns (insn, next);
      else if (! INSN_FROM_TARGET_P (XVECEXP (pat, 0, 1))
	       && own_thread_p (target_label, target_label, 0))
	try_merge_delay_insns (insn, next_active_insn (target_label));

      /* If we get here, we haven't deleted INSN.  But we may have deleted
	 NEXT, so recompute it.  */
      next = next_active_insn (insn);
    }
}

#ifdef HAVE_return

/* Look for filled jumps to the end of function label.  We can try to convert
   them into RETURN insns if the insns in the delay slot are valid for the
   RETURN as well.  */

static void
make_return_insns (rtx first)
{
  rtx insn, jump_insn, pat;
  rtx real_return_label = end_of_function_label;
  int slots, i;

#ifdef DELAY_SLOTS_FOR_EPILOGUE
  /* If a previous pass filled delay slots in the epilogue, things get a
     bit more complicated, as those filler insns would generally (without
     data flow analysis) have to be executed after any existing branch
     delay slot filler insns.  It is also unknown whether such a
     transformation would actually be profitable.  Note that the existing
     code only cares for branches with (some) filled delay slots.  */
  if (current_function_epilogue_delay_list != NULL)
    return;
#endif

  /* See if there is a RETURN insn in the function other than the one we
     made for END_OF_FUNCTION_LABEL.  If so, set up anything we can't change
     into a RETURN to jump to it.  */
  for (insn = first; insn; insn = NEXT_INSN (insn))
    if (JUMP_P (insn) && GET_CODE (PATTERN (insn)) == RETURN)
      {
	real_return_label = get_label_before (insn);
	break;
      }

  /* Show an extra usage of REAL_RETURN_LABEL so it won't go away if it
     was equal to END_OF_FUNCTION_LABEL.  */
  LABEL_NUSES (real_return_label)++;

  /* Clear the list of insns to fill so we can use it.  */
  obstack_free (&unfilled_slots_obstack, unfilled_firstobj);

  for (insn = first; insn; insn = NEXT_INSN (insn))
    {
      int flags;

      /* Only look at filled JUMP_INSNs that go to the end of function
	 label.  */
      if (!NONJUMP_INSN_P (insn)
	  || GET_CODE (PATTERN (insn)) != SEQUENCE
	  || !JUMP_P (XVECEXP (PATTERN (insn), 0, 0))
	  || JUMP_LABEL (XVECEXP (PATTERN (insn), 0, 0)) != end_of_function_label)
	continue;

      pat = PATTERN (insn);
      jump_insn = XVECEXP (pat, 0, 0);

      /* If we can't make the jump into a RETURN, try to redirect it to the best
	 RETURN and go on to the next insn.  */
      if (! reorg_redirect_jump (jump_insn, NULL_RTX))
	{
	  /* Make sure redirecting the jump will not invalidate the delay
	     slot insns.  */
	  if (redirect_with_delay_slots_safe_p (jump_insn,
						real_return_label,
						insn))
	    reorg_redirect_jump (jump_insn, real_return_label);
	  continue;
	}

      /* See if this RETURN can accept the insns current in its delay slot.
	 It can if it has more or an equal number of slots and the contents
	 of each is valid.  */

      flags = get_jump_flags (jump_insn, JUMP_LABEL (jump_insn));
      slots = num_delay_slots (jump_insn);
      if (slots >= XVECLEN (pat, 0) - 1)
	{
	  for (i = 1; i < XVECLEN (pat, 0); i++)
	    if (! (
#ifdef ANNUL_IFFALSE_SLOTS
		   (INSN_ANNULLED_BRANCH_P (jump_insn)
		    && INSN_FROM_TARGET_P (XVECEXP (pat, 0, i)))
		   ? eligible_for_annul_false (jump_insn, i - 1,
					       XVECEXP (pat, 0, i), flags) :
#endif
#ifdef ANNUL_IFTRUE_SLOTS
		   (INSN_ANNULLED_BRANCH_P (jump_insn)
		    && ! INSN_FROM_TARGET_P (XVECEXP (pat, 0, i)))
		   ? eligible_for_annul_true (jump_insn, i - 1,
					      XVECEXP (pat, 0, i), flags) :
#endif
		   eligible_for_delay (jump_insn, i - 1,
				       XVECEXP (pat, 0, i), flags)))
	      break;
	}
      else
	i = 0;

      if (i == XVECLEN (pat, 0))
	continue;

      /* We have to do something with this insn.  If it is an unconditional
	 RETURN, delete the SEQUENCE and output the individual insns,
	 followed by the RETURN.  Then set things up so we try to find
	 insns for its delay slots, if it needs some.  */
      if (GET_CODE (PATTERN (jump_insn)) == RETURN)
	{
	  rtx prev = PREV_INSN (insn);

	  delete_related_insns (insn);
	  for (i = 1; i < XVECLEN (pat, 0); i++)
	    prev = emit_insn_after (PATTERN (XVECEXP (pat, 0, i)), prev);

	  insn = emit_jump_insn_after (PATTERN (jump_insn), prev);
	  emit_barrier_after (insn);

	  if (slots)
	    obstack_ptr_grow (&unfilled_slots_obstack, insn);
	}
      else
	/* It is probably more efficient to keep this with its current
	   delay slot as a branch to a RETURN.  */
	reorg_redirect_jump (jump_insn, real_return_label);
    }

  /* Now delete REAL_RETURN_LABEL if we never used it.  Then try to fill any
     new delay slots we have created.  */
  if (--LABEL_NUSES (real_return_label) == 0)
    delete_related_insns (real_return_label);

  fill_simple_delay_slots (1);
  fill_simple_delay_slots (0);
}
#endif

/* Try to find insns to place in delay slots.  */

void
dbr_schedule (rtx first)
{
  rtx insn, next, epilogue_insn = 0;
  int i;

  /* If the current function has no insns other than the prologue and
     epilogue, then do not try to fill any delay slots.  */
  if (n_basic_blocks == NUM_FIXED_BLOCKS)
    return;

  /* Find the highest INSN_UID and allocate and initialize our map from
     INSN_UID's to position in code.  */
  for (max_uid = 0, insn = first; insn; insn = NEXT_INSN (insn))
    {
      if (INSN_UID (insn) > max_uid)
	max_uid = INSN_UID (insn);
      if (NOTE_P (insn)
	  && NOTE_LINE_NUMBER (insn) == NOTE_INSN_EPILOGUE_BEG)
	epilogue_insn = insn;
    }

  uid_to_ruid = xmalloc ((max_uid + 1) * sizeof (int));
  for (i = 0, insn = first; insn; i++, insn = NEXT_INSN (insn))
    uid_to_ruid[INSN_UID (insn)] = i;

  /* Initialize the list of insns that need filling.  */
  if (unfilled_firstobj == 0)
    {
      gcc_obstack_init (&unfilled_slots_obstack);
      unfilled_firstobj = obstack_alloc (&unfilled_slots_obstack, 0);
    }

  for (insn = next_active_insn (first); insn; insn = next_active_insn (insn))
    {
      rtx target;

      INSN_ANNULLED_BRANCH_P (insn) = 0;
      INSN_FROM_TARGET_P (insn) = 0;

      /* Skip vector tables.  We can't get attributes for them.  */
      if (JUMP_P (insn)
	  && (GET_CODE (PATTERN (insn)) == ADDR_VEC
	      || GET_CODE (PATTERN (insn)) == ADDR_DIFF_VEC))
	continue;

      if (num_delay_slots (insn) > 0)
	obstack_ptr_grow (&unfilled_slots_obstack, insn);

      /* Ensure all jumps go to the last of a set of consecutive labels.  */
      if (JUMP_P (insn)
	  && (condjump_p (insn) || condjump_in_parallel_p (insn))
	  && JUMP_LABEL (insn) != 0
	  && ((target = skip_consecutive_labels (JUMP_LABEL (insn)))
	      != JUMP_LABEL (insn)))
	redirect_jump (insn, target, 1);
    }

  init_resource_info (epilogue_insn);

  /* Show we haven't computed an end-of-function label yet.  */
  end_of_function_label = 0;

  /* Initialize the statistics for this function.  */
  memset (num_insns_needing_delays, 0, sizeof num_insns_needing_delays);
  memset (num_filled_delays, 0, sizeof num_filled_delays);

  /* Now do the delay slot filling.  Try everything twice in case earlier
     changes make more slots fillable.  */

  for (reorg_pass_number = 0;
       reorg_pass_number < MAX_REORG_PASSES;
       reorg_pass_number++)
    {
      fill_simple_delay_slots (1);
      fill_simple_delay_slots (0);
      fill_eager_delay_slots ();
      relax_delay_slots (first);
    }

  /* Delete any USE insns made by update_block; subsequent passes don't need
     them or know how to deal with them.  */
  for (insn = first; insn; insn = next)
    {
      next = NEXT_INSN (insn);

      if (NONJUMP_INSN_P (insn) && GET_CODE (PATTERN (insn)) == USE
	  && INSN_P (XEXP (PATTERN (insn), 0)))
	next = delete_related_insns (insn);
    }

  /* If we made an end of function label, indicate that it is now
     safe to delete it by undoing our prior adjustment to LABEL_NUSES.
     If it is now unused, delete it.  */
  if (end_of_function_label && --LABEL_NUSES (end_of_function_label) == 0)
    delete_related_insns (end_of_function_label);

#ifdef HAVE_return
  if (HAVE_return && end_of_function_label != 0)
    make_return_insns (first);
#endif

  obstack_free (&unfilled_slots_obstack, unfilled_firstobj);

  /* It is not clear why the line below is needed, but it does seem to be.  */
  unfilled_firstobj = obstack_alloc (&unfilled_slots_obstack, 0);

  if (dump_file)
    {
      int i, j, need_comma;
      int total_delay_slots[MAX_DELAY_HISTOGRAM + 1];
      int total_annul_slots[MAX_DELAY_HISTOGRAM + 1];

      for (reorg_pass_number = 0;
	   reorg_pass_number < MAX_REORG_PASSES;
	   reorg_pass_number++)
	{
	  fprintf (dump_file, ";; Reorg pass #%d:\n", reorg_pass_number + 1);
	  for (i = 0; i < NUM_REORG_FUNCTIONS; i++)
	    {
	      need_comma = 0;
	      fprintf (dump_file, ";; Reorg function #%d\n", i);

	      fprintf (dump_file, ";; %d insns needing delay slots\n;; ",
		       num_insns_needing_delays[i][reorg_pass_number]);

	      for (j = 0; j < MAX_DELAY_HISTOGRAM + 1; j++)
		if (num_filled_delays[i][j][reorg_pass_number])
		  {
		    if (need_comma)
		      fprintf (dump_file, ", ");
		    need_comma = 1;
		    fprintf (dump_file, "%d got %d delays",
			     num_filled_delays[i][j][reorg_pass_number], j);
		  }
	      fprintf (dump_file, "\n");
	    }
	}
      memset (total_delay_slots, 0, sizeof total_delay_slots);
      memset (total_annul_slots, 0, sizeof total_annul_slots);
      for (insn = first; insn; insn = NEXT_INSN (insn))
	{
	  if (! INSN_DELETED_P (insn)
	      && NONJUMP_INSN_P (insn)
	      && GET_CODE (PATTERN (insn)) != USE
	      && GET_CODE (PATTERN (insn)) != CLOBBER)
	    {
	      if (GET_CODE (PATTERN (insn)) == SEQUENCE)
		{
		  j = XVECLEN (PATTERN (insn), 0) - 1;
		  if (j > MAX_DELAY_HISTOGRAM)
		    j = MAX_DELAY_HISTOGRAM;
		  if (INSN_ANNULLED_BRANCH_P (XVECEXP (PATTERN (insn), 0, 0)))
		    total_annul_slots[j]++;
		  else
		    total_delay_slots[j]++;
		}
	      else if (num_delay_slots (insn) > 0)
		total_delay_slots[0]++;
	    }
	}
      fprintf (dump_file, ";; Reorg totals: ");
      need_comma = 0;
      for (j = 0; j < MAX_DELAY_HISTOGRAM + 1; j++)
	{
	  if (total_delay_slots[j])
	    {
	      if (need_comma)
		fprintf (dump_file, ", ");
	      need_comma = 1;
	      fprintf (dump_file, "%d got %d delays", total_delay_slots[j], j);
	    }
	}
      fprintf (dump_file, "\n");
#if defined (ANNUL_IFTRUE_SLOTS) || defined (ANNUL_IFFALSE_SLOTS)
      fprintf (dump_file, ";; Reorg annuls: ");
      need_comma = 0;
      for (j = 0; j < MAX_DELAY_HISTOGRAM + 1; j++)
	{
	  if (total_annul_slots[j])
	    {
	      if (need_comma)
		fprintf (dump_file, ", ");
	      need_comma = 1;
	      fprintf (dump_file, "%d got %d delays", total_annul_slots[j], j);
	    }
	}
      fprintf (dump_file, "\n");
#endif
      fprintf (dump_file, "\n");
    }

  /* For all JUMP insns, fill in branch prediction notes, so that during
     assembler output a target can set branch prediction bits in the code.
     We have to do this now, as up until this point the destinations of
     JUMPS can be moved around and changed, but past right here that cannot
     happen.  */
  for (insn = first; insn; insn = NEXT_INSN (insn))
    {
      int pred_flags;

      if (NONJUMP_INSN_P (insn))
	{
	  rtx pat = PATTERN (insn);

	  if (GET_CODE (pat) == SEQUENCE)
	    insn = XVECEXP (pat, 0, 0);
	}
      if (!JUMP_P (insn))
	continue;

      pred_flags = get_jump_flags (insn, JUMP_LABEL (insn));
      REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_BR_PRED,
					    GEN_INT (pred_flags),
					    REG_NOTES (insn));
    }
  free_resource_info ();
  free (uid_to_ruid);
#ifdef DELAY_SLOTS_FOR_EPILOGUE
  /* SPARC assembler, for instance, emit warning when debug info is output
     into the delay slot.  */
  {
    rtx link;

    for (link = current_function_epilogue_delay_list;
         link;
         link = XEXP (link, 1))
      INSN_LOCATOR (XEXP (link, 0)) = 0;
  }
#endif
}
#endif /* DELAY_SLOTS */

static bool
gate_handle_delay_slots (void)
{
#ifdef DELAY_SLOTS
  return flag_delayed_branch;
#else 
  return 0;
#endif
}

/* Run delay slot optimization.  */
static unsigned int
rest_of_handle_delay_slots (void)
{
#ifdef DELAY_SLOTS
  dbr_schedule (get_insns ());
#endif
  return 0;
}   

struct tree_opt_pass pass_delay_slots =
{
  "dbr",                                /* name */
  gate_handle_delay_slots,              /* gate */
  rest_of_handle_delay_slots,           /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_DBR_SCHED,                         /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func |
  TODO_ggc_collect,                     /* todo_flags_finish */
  'd'                                   /* letter */
};

/* Machine dependent reorg pass.  */
static bool
gate_handle_machine_reorg (void)
{
  return targetm.machine_dependent_reorg != 0;
}


static unsigned int
rest_of_handle_machine_reorg (void)
{
  targetm.machine_dependent_reorg ();
  return 0;
}

struct tree_opt_pass pass_machine_reorg =
{
  "mach",                               /* name */
  gate_handle_machine_reorg,            /* gate */
  rest_of_handle_machine_reorg,         /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_MACH_DEP,                          /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func |
  TODO_ggc_collect,                     /* todo_flags_finish */
  'M'                                   /* letter */
};

