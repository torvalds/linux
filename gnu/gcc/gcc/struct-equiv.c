/* Control flow optimization code for GNU compiler.
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

/* Try to match two basic blocks - or their ends - for structural equivalence.
   We scan the blocks from their ends backwards, and expect that insns are
   identical, except for certain cases involving registers.  A mismatch
   We scan the blocks from their ends backwards, hoping to find a match, I.e.
   insns are identical, except for certain cases involving registers.  A
   mismatch between register number RX (used in block X) and RY (used in the
   same way in block Y) can be handled in one of the following cases:
   1. RX and RY are local to their respective blocks; they are set there and
      die there.  If so, they can effectively be ignored.
   2. RX and RY die in their blocks, but live at the start.  If any path
      gets redirected through X instead of Y, the caller must emit
      compensation code to move RY to RX.  If there are overlapping inputs,
      the function resolve_input_conflict ensures that this can be done.
      Information about these registers are tracked in the X_LOCAL, Y_LOCAL,
      LOCAL_COUNT and LOCAL_RVALUE fields.
   3. RX and RY live throughout their blocks, including the start and the end.
      Either RX and RY must be identical, or we have to replace all uses in
      block X with a new pseudo, which is stored in the INPUT_REG field.  The
      caller can then use block X instead of block Y by copying RY to the new
      pseudo.

   The main entry point to this file is struct_equiv_block_eq.  This function
   uses a struct equiv_info to accept some of its inputs, to keep track of its
   internal state, to pass down to its helper functions, and to communicate
   some of the results back to the caller.

   Most scans will result in a failure to match a sufficient number of insns
   to make any optimization worth while, therefore the process is geared more
   to quick scanning rather than the ability to exactly backtrack when we
   find a mismatch.  The information gathered is still meaningful to make a
   preliminary decision if we want to do an optimization, we might only
   slightly overestimate the number of matchable insns, and underestimate
   the number of inputs an miss an input conflict.  Sufficient information
   is gathered so that when we make another pass, we won't have to backtrack
   at the same point.
   Another issue is that information in memory attributes and/or REG_NOTES
   might have to be merged or discarded to make a valid match.  We don't want
   to discard such information when we are not certain that we want to merge
   the two (partial) blocks.
   For these reasons, struct_equiv_block_eq has to be called first with the
   STRUCT_EQUIV_START bit set in the mode parameter.  This will calculate the
   number of matched insns and the number and types of inputs.  If the
   need_rerun field is set, the results are only tentative, and the caller
   has to call again with STRUCT_EQUIV_RERUN till need_rerun is false in
   order to get a reliable match.
   To install the changes necessary for the match, the function has to be
   called again with STRUCT_EQUIV_FINAL.

   While scanning an insn, we process first all the SET_DESTs, then the
   SET_SRCes, then the REG_NOTES, in order to keep the register liveness
   information consistent.
   If we were to mix up the order for sources / destinations in an insn where
   a source is also a destination, we'd end up being mistaken to think that
   the register is not live in the preceding insn.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "regs.h"
#include "output.h"
#include "insn-config.h"
#include "flags.h"
#include "recog.h"
#include "tm_p.h"
#include "target.h"
#include "emit-rtl.h"
#include "reload.h"

static void merge_memattrs (rtx, rtx);
static bool set_dest_equiv_p (rtx x, rtx y, struct equiv_info *info);
static bool set_dest_addr_equiv_p (rtx x, rtx y, struct equiv_info *info);
static void find_dying_inputs (struct equiv_info *info);
static bool resolve_input_conflict (struct equiv_info *info);

/* After reload, some moves, as indicated by SECONDARY_RELOAD_CLASS and
   SECONDARY_MEMORY_NEEDED, cannot be done directly.  For our purposes, we
   consider them impossible to generate after reload (even though some
   might be synthesized when you throw enough code at them).
   Since we don't know while processing a cross-jump if a local register
   that is currently live will eventually be live and thus be an input,
   we keep track of potential inputs that would require an impossible move
   by using a prohibitively high cost for them.
   This number, multiplied with the larger of STRUCT_EQUIV_MAX_LOCAL and
   FIRST_PSEUDO_REGISTER, must fit in the input_cost field of
   struct equiv_info.  */
#define IMPOSSIBLE_MOVE_FACTOR 20000



/* Removes the memory attributes of MEM expression
   if they are not equal.  */

void
merge_memattrs (rtx x, rtx y)
{
  int i;
  int j;
  enum rtx_code code;
  const char *fmt;

  if (x == y)
    return;
  if (x == 0 || y == 0)
    return;

  code = GET_CODE (x);

  if (code != GET_CODE (y))
    return;

  if (GET_MODE (x) != GET_MODE (y))
    return;

  if (code == MEM && MEM_ATTRS (x) != MEM_ATTRS (y))
    {
      if (! MEM_ATTRS (x))
	MEM_ATTRS (y) = 0;
      else if (! MEM_ATTRS (y))
	MEM_ATTRS (x) = 0;
      else
	{
	  rtx mem_size;

	  if (MEM_ALIAS_SET (x) != MEM_ALIAS_SET (y))
	    {
	      set_mem_alias_set (x, 0);
	      set_mem_alias_set (y, 0);
	    }

	  if (! mem_expr_equal_p (MEM_EXPR (x), MEM_EXPR (y)))
	    {
	      set_mem_expr (x, 0);
	      set_mem_expr (y, 0);
	      set_mem_offset (x, 0);
	      set_mem_offset (y, 0);
	    }
	  else if (MEM_OFFSET (x) != MEM_OFFSET (y))
	    {
	      set_mem_offset (x, 0);
	      set_mem_offset (y, 0);
	    }

	  if (!MEM_SIZE (x))
	    mem_size = NULL_RTX;
	  else if (!MEM_SIZE (y))
	    mem_size = NULL_RTX;
	  else
	    mem_size = GEN_INT (MAX (INTVAL (MEM_SIZE (x)),
				     INTVAL (MEM_SIZE (y))));
	  set_mem_size (x, mem_size);
	  set_mem_size (y, mem_size);

	  set_mem_align (x, MIN (MEM_ALIGN (x), MEM_ALIGN (y)));
	  set_mem_align (y, MEM_ALIGN (x));
	}
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      switch (fmt[i])
	{
	case 'E':
	  /* Two vectors must have the same length.  */
	  if (XVECLEN (x, i) != XVECLEN (y, i))
	    return;

	  for (j = 0; j < XVECLEN (x, i); j++)
	    merge_memattrs (XVECEXP (x, i, j), XVECEXP (y, i, j));

	  break;

	case 'e':
	  merge_memattrs (XEXP (x, i), XEXP (y, i));
	}
    }
  return;
}

/* In SET, assign the bit for the register number of REG the value VALUE.
   If REG is a hard register, do so for all its constituent registers.
   Return the number of registers that have become included (as a positive
   number) or excluded (as a negative number).  */
static int
assign_reg_reg_set (regset set, rtx reg, int value)
{
  unsigned regno = REGNO (reg);
  int nregs, i, old;

  if (regno >= FIRST_PSEUDO_REGISTER)
    {
      gcc_assert (!reload_completed);
      nregs = 1;
    }
  else
    nregs = hard_regno_nregs[regno][GET_MODE (reg)];
  for (old = 0, i = nregs; --i >= 0; regno++)
    {
      if ((value != 0) == REGNO_REG_SET_P (set, regno))
	continue;
      if (value)
	old++, SET_REGNO_REG_SET (set, regno);
      else
	old--, CLEAR_REGNO_REG_SET (set, regno);
    }
  return old;
}

/* Record state about current inputs / local registers / liveness
   in *P.  */
static inline void
struct_equiv_make_checkpoint (struct struct_equiv_checkpoint *p,
			      struct equiv_info *info)
{
  *p = info->cur;
}

/* Call struct_equiv_make_checkpoint (P, INFO) if the current partial block
   is suitable to split off - i.e. there is no dangling cc0 user - and
   if the current cost of the common instructions, minus the cost for
   setting up the inputs, is higher than what has been recorded before
   in CHECKPOINT[N].  Also, if we do so, confirm or cancel any pending
   changes.  */
static void
struct_equiv_improve_checkpoint (struct struct_equiv_checkpoint *p,
				 struct equiv_info *info)
{
#ifdef HAVE_cc0
  if (reg_mentioned_p (cc0_rtx, info->cur.x_start)
      && !sets_cc0_p (info->cur.x_start))
    return;
#endif
  if (info->cur.input_count >= IMPOSSIBLE_MOVE_FACTOR)
    return;
  if (info->input_cost >= 0
      ? (COSTS_N_INSNS(info->cur.ninsns - p->ninsns)
	 > info->input_cost * (info->cur.input_count - p->input_count))
      : info->cur.ninsns > p->ninsns && !info->cur.input_count)
    {
      if (info->check_input_conflict && ! resolve_input_conflict (info))
	return;
      /* We have a profitable set of changes.  If this is the final pass,
	 commit them now.  Otherwise, we don't know yet if we can make any
	 change, so put the old code back for now.  */
      if (info->mode & STRUCT_EQUIV_FINAL)
	confirm_change_group ();
      else
	cancel_changes (0);
      struct_equiv_make_checkpoint (p, info);
    }
}

/* Restore state about current inputs / local registers / liveness
   from P.  */
static void
struct_equiv_restore_checkpoint (struct struct_equiv_checkpoint *p,
				 struct equiv_info *info)
{
  info->cur.ninsns = p->ninsns;
  info->cur.x_start = p->x_start;
  info->cur.y_start = p->y_start;
  info->cur.input_count = p->input_count;
  info->cur.input_valid = p->input_valid;
  while (info->cur.local_count > p->local_count)
    {
      info->cur.local_count--;
      info->cur.version--;
      if (REGNO_REG_SET_P (info->x_local_live,
			   REGNO (info->x_local[info->cur.local_count])))
	{
	  assign_reg_reg_set (info->x_local_live,
			      info->x_local[info->cur.local_count], 0);
	  assign_reg_reg_set (info->y_local_live,
			      info->y_local[info->cur.local_count], 0);
	  info->cur.version--;
	}
    }
  if (info->cur.version != p->version)
    info->need_rerun = true;
}


/* Update register liveness to reflect that X is now life (if rvalue is
   nonzero) or dead (if rvalue is zero) in INFO->x_block, and likewise Y
   in INFO->y_block.  Return the number of registers the liveness of which
   changed in each block (as a negative number if registers became dead).  */
static int
note_local_live (struct equiv_info *info, rtx x, rtx y, int rvalue)
{
  unsigned x_regno = REGNO (x);
  unsigned y_regno = REGNO (y);
  int x_nominal_nregs = (x_regno >= FIRST_PSEUDO_REGISTER
			 ? 1 : hard_regno_nregs[x_regno][GET_MODE (x)]);
  int y_nominal_nregs = (y_regno >= FIRST_PSEUDO_REGISTER
			 ? 1 : hard_regno_nregs[y_regno][GET_MODE (y)]);
  int x_change = assign_reg_reg_set (info->x_local_live, x, rvalue);
  int y_change = assign_reg_reg_set (info->y_local_live, y, rvalue);

  gcc_assert (x_nominal_nregs && y_nominal_nregs);
  gcc_assert (x_change * y_nominal_nregs == y_change * x_nominal_nregs);
  if (y_change)
    {
      if (reload_completed)
	{
	  unsigned x_regno ATTRIBUTE_UNUSED = REGNO (x);
	  unsigned y_regno = REGNO (y);
	  enum machine_mode x_mode = GET_MODE (x);

	  if (secondary_reload_class (0, REGNO_REG_CLASS (y_regno), x_mode, x)
	      != NO_REGS
#ifdef SECONDARY_MEMORY_NEEDED
	      || SECONDARY_MEMORY_NEEDED (REGNO_REG_CLASS (y_regno),
					  REGNO_REG_CLASS (x_regno), x_mode)
#endif
	      )
	  y_change *= IMPOSSIBLE_MOVE_FACTOR;
	}
      info->cur.input_count += y_change;
      info->cur.version++;
    }
  return x_change;
}

/* Check if *XP is equivalent to Y.  Until an an unreconcilable difference is
   found, use in-group changes with validate_change on *XP to make register
   assignments agree.  It is the (not necessarily direct) callers
   responsibility to verify / confirm / cancel these changes, as appropriate.
   RVALUE indicates if the processed piece of rtl is used as a destination, in
   which case we can't have different registers being an input.  Returns
   nonzero if the two blocks have been identified as equivalent, zero otherwise.
   RVALUE == 0: destination
   RVALUE == 1: source
   RVALUE == -1: source, ignore SET_DEST of SET / clobber.  */
bool
rtx_equiv_p (rtx *xp, rtx y, int rvalue, struct equiv_info *info)
{
  rtx x = *xp;
  enum rtx_code code;
  int length;
  const char *format;
  int i;

  if (!y || !x)
    return x == y;
  code = GET_CODE (y);
  if (code != REG && x == y)
    return true;
  if (GET_CODE (x) != code
      || GET_MODE (x) != GET_MODE (y))
    return false;

  /* ??? could extend to allow CONST_INT inputs.  */
  switch (code)
    {
    case REG:
      {
	unsigned x_regno = REGNO (x);
	unsigned y_regno = REGNO (y);
	int x_common_live, y_common_live;

	if (reload_completed
	    && (x_regno >= FIRST_PSEUDO_REGISTER
		|| y_regno >= FIRST_PSEUDO_REGISTER))
	  {
	    /* We should only see this in REG_NOTEs.  */
	    gcc_assert (!info->live_update);
	    /* Returning false will cause us to remove the notes.  */
	    return false;
	  }
#ifdef STACK_REGS
	/* After reg-stack, can only accept literal matches of stack regs.  */
	if (info->mode & CLEANUP_POST_REGSTACK
	    && (IN_RANGE (x_regno, FIRST_STACK_REG, LAST_STACK_REG)
		|| IN_RANGE (y_regno, FIRST_STACK_REG, LAST_STACK_REG)))
	  return x_regno == y_regno;
#endif

	/* If the register is a locally live one in one block, the
	   corresponding one must be locally live in the other, too, and
	   match of identical regnos doesn't apply.  */
	if (REGNO_REG_SET_P (info->x_local_live, x_regno))
	  {
	    if (!REGNO_REG_SET_P (info->y_local_live, y_regno))
	      return false;
	  }
	else if (REGNO_REG_SET_P (info->y_local_live, y_regno))
	  return false;
	else if (x_regno == y_regno)
	  {
	    if (!rvalue && info->cur.input_valid
		&& (reg_overlap_mentioned_p (x, info->x_input)
		    || reg_overlap_mentioned_p (x, info->y_input)))
	      return false;

	    /* Update liveness information.  */
	    if (info->live_update
		&& assign_reg_reg_set (info->common_live, x, rvalue))
	      info->cur.version++;

	    return true;
	  }

	x_common_live = REGNO_REG_SET_P (info->common_live, x_regno);
	y_common_live = REGNO_REG_SET_P (info->common_live, y_regno);
	if (x_common_live != y_common_live)
	  return false;
	else if (x_common_live)
	  {
	    if (! rvalue || info->input_cost < 0 || no_new_pseudos)
	      return false;
	    /* If info->live_update is not set, we are processing notes.
	       We then allow a match with x_input / y_input found in a
	       previous pass.  */
	    if (info->live_update && !info->cur.input_valid)
	      {
		info->cur.input_valid = true;
		info->x_input = x;
		info->y_input = y;
		info->cur.input_count += optimize_size ? 2 : 1;
		if (info->input_reg
		    && GET_MODE (info->input_reg) != GET_MODE (info->x_input))
		  info->input_reg = NULL_RTX;
		if (!info->input_reg)
		  info->input_reg = gen_reg_rtx (GET_MODE (info->x_input));
	      }
	    else if ((info->live_update
		      ? ! info->cur.input_valid : ! info->x_input)
		     || ! rtx_equal_p (x, info->x_input)
		     || ! rtx_equal_p (y, info->y_input))
	      return false;
	    validate_change (info->cur.x_start, xp, info->input_reg, 1);
	  }
	else
	  {
	    int x_nregs = (x_regno >= FIRST_PSEUDO_REGISTER
			   ? 1 : hard_regno_nregs[x_regno][GET_MODE (x)]);
	    int y_nregs = (y_regno >= FIRST_PSEUDO_REGISTER
			   ? 1 : hard_regno_nregs[y_regno][GET_MODE (y)]);
	    int size = GET_MODE_SIZE (GET_MODE (x));
	    enum machine_mode x_mode = GET_MODE (x);
	    unsigned x_regno_i, y_regno_i;
	    int x_nregs_i, y_nregs_i, size_i;
	    int local_count = info->cur.local_count;

	    /* This might be a register local to each block.  See if we have
	       it already registered.  */
	    for (i = local_count - 1; i >= 0; i--)
	      {
		x_regno_i = REGNO (info->x_local[i]);
		x_nregs_i = (x_regno_i >= FIRST_PSEUDO_REGISTER
			     ? 1 : hard_regno_nregs[x_regno_i][GET_MODE (x)]);
		y_regno_i = REGNO (info->y_local[i]);
		y_nregs_i = (y_regno_i >= FIRST_PSEUDO_REGISTER
			     ? 1 : hard_regno_nregs[y_regno_i][GET_MODE (y)]);
		size_i = GET_MODE_SIZE (GET_MODE (info->x_local[i]));

		/* If we have a new pair of registers that is wider than an
		   old pair and enclosing it with matching offsets,
		   remove the old pair.  If we find a matching, wider, old
		   pair, use the old one.  If the width is the same, use the
		   old one if the modes match, but the new if they don't.
		   We don't want to get too fancy with subreg_regno_offset
		   here, so we just test two straightforward cases each.  */
		if (info->live_update
		    && (x_mode != GET_MODE (info->x_local[i])
			? size >= size_i : size > size_i))
		  {
		    /* If the new pair is fully enclosing a matching
		       existing pair, remove the old one.  N.B. because
		       we are removing one entry here, the check below
		       if we have space for a new entry will succeed.  */
		    if ((x_regno <= x_regno_i
			 && x_regno + x_nregs >= x_regno_i + x_nregs_i
			 && x_nregs == y_nregs && x_nregs_i == y_nregs_i
			 && x_regno - x_regno_i == y_regno - y_regno_i)
			|| (x_regno == x_regno_i && y_regno == y_regno_i
			    && x_nregs >= x_nregs_i && y_nregs >= y_nregs_i))
		      {
			info->cur.local_count = --local_count;
			info->x_local[i] = info->x_local[local_count];
			info->y_local[i] = info->y_local[local_count];
			continue;
		      }
		  }
		else
		  {

		    /* If the new pair is fully enclosed within a matching
		       existing pair, succeed.  */
		    if (x_regno >= x_regno_i
			&& x_regno + x_nregs <= x_regno_i + x_nregs_i
			&& x_nregs == y_nregs && x_nregs_i == y_nregs_i
			&& x_regno - x_regno_i == y_regno - y_regno_i)
		      break;
		    if (x_regno == x_regno_i && y_regno == y_regno_i
			&& x_nregs <= x_nregs_i && y_nregs <= y_nregs_i)
		      break;
		}

		/* Any other overlap causes a match failure.  */
		if (x_regno + x_nregs > x_regno_i
		    && x_regno_i + x_nregs_i > x_regno)
		  return false;
		if (y_regno + y_nregs > y_regno_i
		    && y_regno_i + y_nregs_i > y_regno)
		  return false;
	      }
	    if (i < 0)
	      {
		/* Not found.  Create a new entry if possible.  */
		if (!info->live_update
		    || info->cur.local_count >= STRUCT_EQUIV_MAX_LOCAL)
		  return false;
		info->x_local[info->cur.local_count] = x;
		info->y_local[info->cur.local_count] = y;
		info->cur.local_count++;
		info->cur.version++;
	      }
	    note_local_live (info, x, y, rvalue);
	  }
	return true;
      }
    case SET:
      gcc_assert (rvalue < 0);
      /* Ignore the destinations role as a destination.  Still, we have
	 to consider input registers embedded in the addresses of a MEM.
	 N.B., we process the rvalue aspect of STRICT_LOW_PART /
	 ZERO_EXTEND / SIGN_EXTEND along with their lvalue aspect.  */
      if(!set_dest_addr_equiv_p (SET_DEST (x), SET_DEST (y), info))
	return false;
      /* Process source.  */
      return rtx_equiv_p (&SET_SRC (x), SET_SRC (y), 1, info);
    case PRE_MODIFY:
      /* Process destination.  */
      if (!rtx_equiv_p (&XEXP (x, 0), XEXP (y, 0), 0, info))
	return false;
      /* Process source.  */
      return rtx_equiv_p (&XEXP (x, 1), XEXP (y, 1), 1, info);
    case POST_MODIFY:
      {
	rtx x_dest0, x_dest1;

	/* Process destination.  */
	x_dest0 = XEXP (x, 0);
	gcc_assert (REG_P (x_dest0));
	if (!rtx_equiv_p (&XEXP (x, 0), XEXP (y, 0), 0, info))
	  return false;
	x_dest1 = XEXP (x, 0);
	/* validate_change might have changed the destination.  Put it back
	   so that we can do a proper match for its role a an input.  */
	XEXP (x, 0) = x_dest0;
	if (!rtx_equiv_p (&XEXP (x, 0), XEXP (y, 0), 1, info))
	  return false;
	gcc_assert (x_dest1 == XEXP (x, 0));
	/* Process source.  */
	return rtx_equiv_p (&XEXP (x, 1), XEXP (y, 1), 1, info);
      }
    case CLOBBER:
      gcc_assert (rvalue < 0);
      return true;
    /* Some special forms are also rvalues when they appear in lvalue
       positions.  However, we must ont try to match a register after we
       have already altered it with validate_change, consider the rvalue
       aspect while we process the lvalue.  */
    case STRICT_LOW_PART:
    case ZERO_EXTEND:
    case SIGN_EXTEND:
      {
	rtx x_inner, y_inner;
	enum rtx_code code;
	int change;

	if (rvalue)
	  break;
	x_inner = XEXP (x, 0);
	y_inner = XEXP (y, 0);
	if (GET_MODE (x_inner) != GET_MODE (y_inner))
	  return false;
	code = GET_CODE (x_inner);
	if (code != GET_CODE (y_inner))
	  return false;
	/* The address of a MEM is an input that will be processed during
	   rvalue == -1 processing.  */
	if (code == SUBREG)
	  {
	    if (SUBREG_BYTE (x_inner) != SUBREG_BYTE (y_inner))
	      return false;
	    x = x_inner;
	    x_inner = SUBREG_REG (x_inner);
	    y_inner = SUBREG_REG (y_inner);
	    if (GET_MODE (x_inner) != GET_MODE (y_inner))
	      return false;
	    code = GET_CODE (x_inner);
	    if (code != GET_CODE (y_inner))
	      return false;
	  }
	if (code == MEM)
	  return true;
	gcc_assert (code == REG);
	if (! rtx_equiv_p (&XEXP (x, 0), y_inner, rvalue, info))
	  return false;
	if (REGNO (x_inner) == REGNO (y_inner))
	  {
	    change = assign_reg_reg_set (info->common_live, x_inner, 1);
	    info->cur.version++;
	  }
	else
	  change = note_local_live (info, x_inner, y_inner, 1);
	gcc_assert (change);
	return true;
      }
    /* The AUTO_INC / POST_MODIFY / PRE_MODIFY sets are modelled to take
       place during input processing, however, that is benign, since they
       are paired with reads.  */
    case MEM:
      return !rvalue || rtx_equiv_p (&XEXP (x, 0), XEXP (y, 0), rvalue, info);
    case POST_INC: case POST_DEC: case PRE_INC: case PRE_DEC:
      return (rtx_equiv_p (&XEXP (x, 0), XEXP (y, 0), 0, info)
	      && rtx_equiv_p (&XEXP (x, 0), XEXP (y, 0), 1, info));
    case PARALLEL:
      /* If this is a top-level PATTERN PARALLEL, we expect the caller to 
	 have handled the SET_DESTs.  A complex or vector PARALLEL can be
	 identified by having a mode.  */
      gcc_assert (rvalue < 0 || GET_MODE (x) != VOIDmode);
      break;
    case LABEL_REF:
      /* Check special tablejump match case.  */
      if (XEXP (y, 0) == info->y_label)
	return (XEXP (x, 0) == info->x_label);
      /* We can't assume nonlocal labels have their following insns yet.  */
      if (LABEL_REF_NONLOCAL_P (x) || LABEL_REF_NONLOCAL_P (y))
	return XEXP (x, 0) == XEXP (y, 0);

      /* Two label-refs are equivalent if they point at labels
	 in the same position in the instruction stream.  */
      return (next_real_insn (XEXP (x, 0))
	      == next_real_insn (XEXP (y, 0)));
    case SYMBOL_REF:
      return XSTR (x, 0) == XSTR (y, 0);
    /* Some rtl is guaranteed to be shared, or unique;  If we didn't match
       EQ equality above, they aren't the same.  */
    case CONST_INT:
    case CODE_LABEL:
      return false;
    default:
      break;
    }

  /* For commutative operations, the RTX match if the operands match in any
     order.  */
  if (targetm.commutative_p (x, UNKNOWN))
    return ((rtx_equiv_p (&XEXP (x, 0), XEXP (y, 0), rvalue, info)
	     && rtx_equiv_p (&XEXP (x, 1), XEXP (y, 1), rvalue, info))
	    || (rtx_equiv_p (&XEXP (x, 0), XEXP (y, 1), rvalue, info)
		&& rtx_equiv_p (&XEXP (x, 1), XEXP (y, 0), rvalue, info)));

  /* Process subexpressions - this is similar to rtx_equal_p.  */
  length = GET_RTX_LENGTH (code);
  format = GET_RTX_FORMAT (code);

  for (i = 0; i < length; ++i)
    {
      switch (format[i])
	{
	case 'w':
	  if (XWINT (x, i) != XWINT (y, i))
	    return false;
	  break;
	case 'n':
	case 'i':
	  if (XINT (x, i) != XINT (y, i))
	    return false;
	  break;
	case 'V':
	case 'E':
	  if (XVECLEN (x, i) != XVECLEN (y, i))
	    return false;
	  if (XVEC (x, i) != 0)
	    {
	      int j;
	      for (j = 0; j < XVECLEN (x, i); ++j)
		{
		  if (! rtx_equiv_p (&XVECEXP (x, i, j), XVECEXP (y, i, j),
				     rvalue, info))
		    return false;
		}
	    }
	  break;
	case 'e':
	  if (! rtx_equiv_p (&XEXP (x, i), XEXP (y, i), rvalue, info))
	    return false;
	  break;
	case 'S':
	case 's':
	  if ((XSTR (x, i) || XSTR (y, i))
	      && (! XSTR (x, i) || ! XSTR (y, i)
		  || strcmp (XSTR (x, i), XSTR (y, i))))
	    return false;
	  break;
	case 'u':
	  /* These are just backpointers, so they don't matter.  */
	  break;
	case '0':
	case 't':
	  break;
	  /* It is believed that rtx's at this level will never
	     contain anything but integers and other rtx's,
	     except for within LABEL_REFs and SYMBOL_REFs.  */
	default:
	  gcc_unreachable ();
	}
    }
  return true;
}

/* Do only the rtx_equiv_p SET_DEST processing for SETs and CLOBBERs.
   Since we are scanning backwards, this the first step in processing each
   insn.  Return true for success.  */
static bool
set_dest_equiv_p (rtx x, rtx y, struct equiv_info *info)
{
  if (!x || !y)
    return x == y;
  if (GET_CODE (x) != GET_CODE (y))
    return false;
  else if (GET_CODE (x) == SET || GET_CODE (x) == CLOBBER)
    return rtx_equiv_p (&XEXP (x, 0), XEXP (y, 0), 0, info);
  else if (GET_CODE (x) == PARALLEL)
    {
      int j;

      if (XVECLEN (x, 0) != XVECLEN (y, 0))
	return false;
      for (j = 0; j < XVECLEN (x, 0); ++j)
	{
	  rtx xe = XVECEXP (x, 0, j);
	  rtx ye = XVECEXP (y, 0, j);

	  if (GET_CODE (xe) != GET_CODE (ye))
	    return false;
	  if ((GET_CODE (xe) == SET || GET_CODE (xe) == CLOBBER)
	      && ! rtx_equiv_p (&XEXP (xe, 0), XEXP (ye, 0), 0, info))
	    return false;
	}
    }
  return true;
}

/* Process MEMs in SET_DEST destinations.  We must not process this together
   with REG SET_DESTs, but must do it separately, lest when we see
   [(set (reg:SI foo) (bar))
    (set (mem:SI (reg:SI foo) (baz)))]
   struct_equiv_block_eq could get confused to assume that (reg:SI foo)
   is not live before this instruction.  */
static bool
set_dest_addr_equiv_p (rtx x, rtx y, struct equiv_info *info)
{
  enum rtx_code code = GET_CODE (x);
  int length;
  const char *format;
  int i;

  if (code != GET_CODE (y))
    return false;
  if (code == MEM)
    return rtx_equiv_p (&XEXP (x, 0), XEXP (y, 0), 1, info);

  /* Process subexpressions.  */
  length = GET_RTX_LENGTH (code);
  format = GET_RTX_FORMAT (code);

  for (i = 0; i < length; ++i)
    {
      switch (format[i])
	{
	case 'V':
	case 'E':
	  if (XVECLEN (x, i) != XVECLEN (y, i))
	    return false;
	  if (XVEC (x, i) != 0)
	    {
	      int j;
	      for (j = 0; j < XVECLEN (x, i); ++j)
		{
		  if (! set_dest_addr_equiv_p (XVECEXP (x, i, j),
					       XVECEXP (y, i, j), info))
		    return false;
		}
	    }
	  break;
	case 'e':
	  if (! set_dest_addr_equiv_p (XEXP (x, i), XEXP (y, i), info))
	    return false;
	  break;
	default:
	  break;
	}
    }
  return true;
}

/* Check if the set of REG_DEAD notes attached to I1 and I2 allows us to
   go ahead with merging I1 and I2, which otherwise look fine.
   Inputs / local registers for the inputs of I1 and I2 have already been
   set up.  */
static bool
death_notes_match_p (rtx i1 ATTRIBUTE_UNUSED, rtx i2 ATTRIBUTE_UNUSED,
		     struct equiv_info *info ATTRIBUTE_UNUSED)
{
#ifdef STACK_REGS
  /* If cross_jump_death_matters is not 0, the insn's mode
     indicates whether or not the insn contains any stack-like regs.  */

  if ((info->mode & CLEANUP_POST_REGSTACK) && stack_regs_mentioned (i1))
    {
      /* If register stack conversion has already been done, then
	 death notes must also be compared before it is certain that
	 the two instruction streams match.  */

      rtx note;
      HARD_REG_SET i1_regset, i2_regset;

      CLEAR_HARD_REG_SET (i1_regset);
      CLEAR_HARD_REG_SET (i2_regset);

      for (note = REG_NOTES (i1); note; note = XEXP (note, 1))
	if (REG_NOTE_KIND (note) == REG_DEAD && STACK_REG_P (XEXP (note, 0)))
	  SET_HARD_REG_BIT (i1_regset, REGNO (XEXP (note, 0)));

      for (note = REG_NOTES (i2); note; note = XEXP (note, 1))
	if (REG_NOTE_KIND (note) == REG_DEAD && STACK_REG_P (XEXP (note, 0)))
	  {
	    unsigned regno = REGNO (XEXP (note, 0));
	    int i;

	    for (i = info->cur.local_count - 1; i >= 0; i--)
	      if (regno == REGNO (info->y_local[i]))
		{
		  regno = REGNO (info->x_local[i]);
		  break;
		}
	    SET_HARD_REG_BIT (i2_regset, regno);
	  }

      GO_IF_HARD_REG_EQUAL (i1_regset, i2_regset, done);

      return false;

    done:
      ;
    }
#endif
  return true;
}

/* Return true if I1 and I2 are equivalent and thus can be crossjumped.  */

bool
insns_match_p (rtx i1, rtx i2, struct equiv_info *info)
{
  int rvalue_change_start;
  struct struct_equiv_checkpoint before_rvalue_change;

  /* Verify that I1 and I2 are equivalent.  */
  if (GET_CODE (i1) != GET_CODE (i2))
    return false;

  info->cur.x_start = i1;
  info->cur.y_start = i2;

  /* If this is a CALL_INSN, compare register usage information.
     If we don't check this on stack register machines, the two
     CALL_INSNs might be merged leaving reg-stack.c with mismatching
     numbers of stack registers in the same basic block.
     If we don't check this on machines with delay slots, a delay slot may
     be filled that clobbers a parameter expected by the subroutine.

     ??? We take the simple route for now and assume that if they're
     equal, they were constructed identically.  */

  if (CALL_P (i1))
    {
      if (SIBLING_CALL_P (i1) != SIBLING_CALL_P (i2)
	  || ! set_dest_equiv_p (PATTERN (i1), PATTERN (i2), info)
	  || ! set_dest_equiv_p (CALL_INSN_FUNCTION_USAGE (i1),
				 CALL_INSN_FUNCTION_USAGE (i2), info)
	  || ! rtx_equiv_p (&CALL_INSN_FUNCTION_USAGE (i1),
			    CALL_INSN_FUNCTION_USAGE (i2), -1, info))
	{
	  cancel_changes (0);
	  return false;
	}
    }
  else if (INSN_P (i1))
    {
      if (! set_dest_equiv_p (PATTERN (i1), PATTERN (i2), info))
	{
	  cancel_changes (0);
	  return false;
	}
    }
  rvalue_change_start = num_validated_changes ();
  struct_equiv_make_checkpoint (&before_rvalue_change, info);
  /* Check death_notes_match_p *after* the inputs have been processed,
     so that local inputs will already have been set up.  */
  if (! INSN_P (i1)
      || (!bitmap_bit_p (info->equiv_used, info->cur.ninsns)
	  && rtx_equiv_p (&PATTERN (i1), PATTERN (i2), -1, info)
	  && death_notes_match_p (i1, i2, info)
	  && verify_changes (0)))
    return true;

  /* Do not do EQUIV substitution after reload.  First, we're undoing the
     work of reload_cse.  Second, we may be undoing the work of the post-
     reload splitting pass.  */
  /* ??? Possibly add a new phase switch variable that can be used by
     targets to disallow the troublesome insns after splitting.  */
  if (!reload_completed)
    {
      rtx equiv1, equiv2;

      cancel_changes (rvalue_change_start);
      struct_equiv_restore_checkpoint (&before_rvalue_change, info);

      /* The following code helps take care of G++ cleanups.  */
      equiv1 = find_reg_equal_equiv_note (i1);
      equiv2 = find_reg_equal_equiv_note (i2);
      if (equiv1 && equiv2
	  /* If the equivalences are not to a constant, they may
	     reference pseudos that no longer exist, so we can't
	     use them.  */
	  && (! reload_completed
	      || (CONSTANT_P (XEXP (equiv1, 0))
		  && rtx_equal_p (XEXP (equiv1, 0), XEXP (equiv2, 0)))))
	{
	  rtx s1 = single_set (i1);
	  rtx s2 = single_set (i2);

	  if (s1 != 0 && s2 != 0)
	    {
	      validate_change (i1, &SET_SRC (s1), XEXP (equiv1, 0), 1);
	      validate_change (i2, &SET_SRC (s2), XEXP (equiv2, 0), 1);
	      /* Only inspecting the new SET_SRC is not good enough,
		 because there may also be bare USEs in a single_set
		 PARALLEL.  */
	      if (rtx_equiv_p (&PATTERN (i1), PATTERN (i2), -1, info)
		  && death_notes_match_p (i1, i2, info)
		  && verify_changes (0))
		{
		  /* Mark this insn so that we'll use the equivalence in
		     all subsequent passes.  */
		  bitmap_set_bit (info->equiv_used, info->cur.ninsns);
		  return true;
		}
	    }
	}
    }

  cancel_changes (0);
  return false;
}

/* Set up mode and register information in INFO.  Return true for success.  */
bool
struct_equiv_init (int mode, struct equiv_info *info)
{
  if ((info->x_block->flags | info->y_block->flags) & BB_DIRTY)
    update_life_info_in_dirty_blocks (UPDATE_LIFE_GLOBAL_RM_NOTES,
				      (PROP_DEATH_NOTES
				       | ((mode & CLEANUP_POST_REGSTACK)
					  ? PROP_POST_REGSTACK : 0)));
  if (!REG_SET_EQUAL_P (info->x_block->il.rtl->global_live_at_end,
			info->y_block->il.rtl->global_live_at_end))
    {
#ifdef STACK_REGS
      unsigned rn;

      if (!(mode & CLEANUP_POST_REGSTACK))
	return false;
      /* After reg-stack.  Remove bogus live info about stack regs.  N.B.
	 these regs are not necessarily all dead - we swap random bogosity
	 against constant bogosity.  However, clearing these bits at
	 least makes the regsets comparable.  */
      for (rn = FIRST_STACK_REG; rn <= LAST_STACK_REG; rn++)
	{
	  CLEAR_REGNO_REG_SET (info->x_block->il.rtl->global_live_at_end, rn);
	  CLEAR_REGNO_REG_SET (info->y_block->il.rtl->global_live_at_end, rn);
	}
      if (!REG_SET_EQUAL_P (info->x_block->il.rtl->global_live_at_end,
			    info->y_block->il.rtl->global_live_at_end))
#endif
	return false;
    }
  info->mode = mode;
  if (mode & STRUCT_EQUIV_START)
    {
      info->x_input = info->y_input = info->input_reg = NULL_RTX;
      info->equiv_used = ALLOC_REG_SET (&reg_obstack);
      info->check_input_conflict = false;
    }
  info->had_input_conflict = false;
  info->cur.ninsns = info->cur.version = 0;
  info->cur.local_count = info->cur.input_count = 0;
  info->cur.x_start = info->cur.y_start = NULL_RTX;
  info->x_label = info->y_label = NULL_RTX;
  info->need_rerun = false;
  info->live_update = true;
  info->cur.input_valid = false;
  info->common_live = ALLOC_REG_SET (&reg_obstack);
  info->x_local_live = ALLOC_REG_SET (&reg_obstack);
  info->y_local_live = ALLOC_REG_SET (&reg_obstack);
  COPY_REG_SET (info->common_live, info->x_block->il.rtl->global_live_at_end);
  struct_equiv_make_checkpoint (&info->best_match, info);
  return true;
}

/* Insns XI and YI have been matched.  Merge memory attributes and reg
   notes.  */
static void
struct_equiv_merge (rtx xi, rtx yi, struct equiv_info *info)
{
  rtx equiv1, equiv2;

  merge_memattrs (xi, yi);

  /* If the merged insns have different REG_EQUAL notes, then
     remove them.  */
  info->live_update = false;
  equiv1 = find_reg_equal_equiv_note (xi);
  equiv2 = find_reg_equal_equiv_note (yi);
  if (equiv1 && !equiv2)
    remove_note (xi, equiv1);
  else if (!equiv1 && equiv2)
    remove_note (yi, equiv2);
  else if (equiv1 && equiv2
  	 && !rtx_equiv_p (&XEXP (equiv1, 0), XEXP (equiv2, 0),
  			   1, info))
    {
      remove_note (xi, equiv1);
      remove_note (yi, equiv2);
    }
  info->live_update = true;
}

/* Return number of matched insns.
   This function must be called up to three times for a successful cross-jump
   match:
   first to find out which instructions do match.  While trying to match
   another instruction that doesn't match, we destroy information in info
   about the actual inputs.  So if there have been any before the last
   match attempt, we need to call this function again to recompute the
   actual inputs up to the actual start of the matching sequence.
   When we are then satisfied that the cross-jump is worthwhile, we
   call this function a third time to make any changes needed to make the
   sequences match: apply equivalences, remove non-matching
   notes and merge memory attributes.  */
int
struct_equiv_block_eq (int mode, struct equiv_info *info)
{
  rtx x_stop, y_stop;
  rtx xi, yi;
  int i;

  if (mode & STRUCT_EQUIV_START)
    {
      x_stop = BB_HEAD (info->x_block);
      y_stop = BB_HEAD (info->y_block);
      if (!x_stop || !y_stop)
	return 0;
    }
  else
    {
      x_stop = info->cur.x_start;
      y_stop = info->cur.y_start;
    }
  if (!struct_equiv_init (mode, info))
    gcc_unreachable ();

  /* Skip simple jumps at the end of the blocks.  Complex jumps still
     need to be compared for equivalence, which we'll do below.  */

  xi = BB_END (info->x_block);
  if (onlyjump_p (xi)
      || (returnjump_p (xi) && !side_effects_p (PATTERN (xi))))
    {
      info->cur.x_start = xi;
      xi = PREV_INSN (xi);
    }

  yi = BB_END (info->y_block);
  if (onlyjump_p (yi)
      || (returnjump_p (yi) && !side_effects_p (PATTERN (yi))))
    {
      info->cur.y_start = yi;
      /* Count everything except for unconditional jump as insn.  */
      /* ??? Is it right to count unconditional jumps with a clobber?
	 Should we count conditional returns?  */
      if (!simplejump_p (yi) && !returnjump_p (yi) && info->cur.x_start)
	info->cur.ninsns++;
      yi = PREV_INSN (yi);
    }

  if (mode & STRUCT_EQUIV_MATCH_JUMPS)
    {
      /* The caller is expected to have compared the jumps already, but we
	 need to match them again to get any local registers and inputs.  */
      gcc_assert (!info->cur.x_start == !info->cur.y_start);
      if (info->cur.x_start)
	{
	  if (any_condjump_p (info->cur.x_start)
	      ? !condjump_equiv_p (info, false)
	      : !insns_match_p (info->cur.x_start, info->cur.y_start, info))
	    gcc_unreachable ();
	}
      else if (any_condjump_p (xi) && any_condjump_p (yi))
	{
	  info->cur.x_start = xi;
	  info->cur.y_start = yi;
	  xi = PREV_INSN (xi);
	  yi = PREV_INSN (yi);
	  info->cur.ninsns++;
	  if (!condjump_equiv_p (info, false))
	    gcc_unreachable ();
	}
      if (info->cur.x_start && info->mode & STRUCT_EQUIV_FINAL)
	struct_equiv_merge (info->cur.x_start, info->cur.y_start, info);
    }

  struct_equiv_improve_checkpoint (&info->best_match, info);
  info->x_end = xi;
  info->y_end = yi;
  if (info->cur.x_start != x_stop)
    for (;;)
      {
	/* Ignore notes.  */
	while (!INSN_P (xi) && xi != x_stop)
	  xi = PREV_INSN (xi);

	while (!INSN_P (yi) && yi != y_stop)
	  yi = PREV_INSN (yi);

	if (!insns_match_p (xi, yi, info))
	  break;
	if (INSN_P (xi))
	  {
	    if (info->mode & STRUCT_EQUIV_FINAL)
	      struct_equiv_merge (xi, yi, info);
	    info->cur.ninsns++;
	    struct_equiv_improve_checkpoint (&info->best_match, info);
	  }
	if (xi == x_stop || yi == y_stop)
	  {
	    /* If we reached the start of at least one of the blocks, but
	       best_match hasn't been advanced back to the first valid insn
	       yet, represent the increased benefit of completing the block
	       as an increased instruction count.  */
	    if (info->best_match.x_start != info->cur.x_start
		&& (xi == BB_HEAD (info->x_block)
		    || yi == BB_HEAD (info->y_block)))
	      {
		info->cur.ninsns++;
		struct_equiv_improve_checkpoint (&info->best_match, info);
		info->cur.ninsns--;
		if (info->best_match.ninsns > info->cur.ninsns)
		  info->best_match.ninsns = info->cur.ninsns;
	      }
	    break;
	  }
	xi = PREV_INSN (xi);
	yi = PREV_INSN (yi);
      }

  /* If we failed to match an insn, but had some changes registered from
     trying to make the insns match, we need to cancel these changes now.  */
  cancel_changes (0);
  /* Restore to best_match to get the sequence with the best known-so-far
     cost-benefit difference.  */
  struct_equiv_restore_checkpoint (&info->best_match, info);

  /* Include preceding notes and labels in the cross-jump / if-conversion.
     One, this may bring us to the head of the blocks.
     Two, it keeps line number notes as matched as may be.  */
  if (info->cur.ninsns)
    {
      xi = info->cur.x_start;
      yi = info->cur.y_start;
      while (xi != x_stop && !INSN_P (PREV_INSN (xi)))
	xi = PREV_INSN (xi);

      while (yi != y_stop && !INSN_P (PREV_INSN (yi)))
	yi = PREV_INSN (yi);

      info->cur.x_start = xi;
      info->cur.y_start = yi;
    }

  if (!info->cur.input_valid)
    info->x_input = info->y_input = info->input_reg = NULL_RTX;
  if (!info->need_rerun)
    {
      find_dying_inputs (info);
      if (info->mode & STRUCT_EQUIV_FINAL)
	{
	  if (info->check_input_conflict && ! resolve_input_conflict (info))
	    gcc_unreachable ();
	}
      else
	{
	  bool input_conflict = info->had_input_conflict;

	  if (!input_conflict
	      && info->dying_inputs > 1
	      && bitmap_intersect_p (info->x_local_live, info->y_local_live))
	    {
	      regset_head clobbered_regs;

	      INIT_REG_SET (&clobbered_regs);
	      for (i = 0; i < info->cur.local_count; i++)
		{
		  if (assign_reg_reg_set (&clobbered_regs, info->y_local[i], 0))
		    {
		      input_conflict = true;
		      break;
		    }
		  assign_reg_reg_set (&clobbered_regs, info->x_local[i], 1);
		}
	      CLEAR_REG_SET (&clobbered_regs);
	    }
	  if (input_conflict && !info->check_input_conflict)
	    info->need_rerun = true;
	  info->check_input_conflict = input_conflict;
	}
    }

  if (info->mode & STRUCT_EQUIV_NEED_FULL_BLOCK
      && (info->cur.x_start != x_stop || info->cur.y_start != y_stop))
    return 0;
  return info->cur.ninsns;
}

/* For each local register, set info->local_rvalue to true iff the register
   is a dying input.  Store the total number of these in info->dying_inputs.  */
static void
find_dying_inputs (struct equiv_info *info)
{
  int i;

  info->dying_inputs = 0;
  for (i = info->cur.local_count-1; i >=0; i--)
    {
      rtx x = info->x_local[i];
      unsigned regno = REGNO (x);
      int nregs = (regno >= FIRST_PSEUDO_REGISTER
		   ? 1 : hard_regno_nregs[regno][GET_MODE (x)]);

      for (info->local_rvalue[i] = false; nregs > 0; regno++, --nregs)
	if (REGNO_REG_SET_P (info->x_local_live, regno))
	  {
	    info->dying_inputs++;
	    info->local_rvalue[i] = true;
	    break;
	  }
    }
}

/* For each local register that is a dying input, y_local[i] will be
   copied to x_local[i].  We'll do this in ascending order.  Try to
   re-order the locals to avoid conflicts like r3 = r2; r4 = r3; .
   Return true iff the re-ordering is successful, or not necessary.  */
static bool
resolve_input_conflict (struct equiv_info *info)
{
  int i, j, end;
  int nswaps = 0;
  rtx save_x_local[STRUCT_EQUIV_MAX_LOCAL];
  rtx save_y_local[STRUCT_EQUIV_MAX_LOCAL];

  find_dying_inputs (info);
  if (info->dying_inputs <= 1)
    return true;
  memcpy (save_x_local, info->x_local, sizeof save_x_local);
  memcpy (save_y_local, info->y_local, sizeof save_y_local);
  end = info->cur.local_count - 1;
  for (i = 0; i <= end; i++)
    {
      /* Cycle detection with regsets is expensive, so we just check that
	 we don't exceed the maximum number of swaps needed in the acyclic
	 case.  */
      int max_swaps = end - i;

      /* Check if x_local[i] will be clobbered.  */
      if (!info->local_rvalue[i])
	continue;
      /* Check if any later value needs to be copied earlier.  */
      for (j = i + 1; j <= end; j++)
	{
	  rtx tmp;

	  if (!info->local_rvalue[j])
	    continue;
	  if (!reg_overlap_mentioned_p (info->x_local[i], info->y_local[j]))
	    continue;
	  if (--max_swaps < 0)
	    {
	      memcpy (info->x_local, save_x_local, sizeof save_x_local);
	      memcpy (info->y_local, save_y_local, sizeof save_y_local);
	      return false;
	    }
	  nswaps++;
	  tmp = info->x_local[i];
	  info->x_local[i] = info->x_local[j];
	  info->x_local[j] = tmp;
	  tmp = info->y_local[i];
	  info->y_local[i] = info->y_local[j];
	  info->y_local[j] = tmp;
	  j = i;
	}
    }
  info->had_input_conflict = true;
  if (dump_file && nswaps)
    fprintf (dump_file, "Resolved input conflict, %d %s.\n",
	     nswaps, nswaps == 1 ? "swap" : "swaps");
  return true;
}
