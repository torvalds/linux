/* Register renaming for the GNU compiler.
   Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tm_p.h"
#include "insn-config.h"
#include "regs.h"
#include "addresses.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "reload.h"
#include "output.h"
#include "function.h"
#include "recog.h"
#include "flags.h"
#include "toplev.h"
#include "obstack.h"
#include "timevar.h"
#include "tree-pass.h"

struct du_chain
{
  struct du_chain *next_chain;
  struct du_chain *next_use;

  rtx insn;
  rtx *loc;
  ENUM_BITFIELD(reg_class) cl : 16;
  unsigned int need_caller_save_reg:1;
  unsigned int earlyclobber:1;
};

enum scan_actions
{
  terminate_all_read,
  terminate_overlapping_read,
  terminate_write,
  terminate_dead,
  mark_read,
  mark_write,
  /* mark_access is for marking the destination regs in
     REG_FRAME_RELATED_EXPR notes (as if they were read) so that the
     note is updated properly.  */
  mark_access
};

static const char * const scan_actions_name[] =
{
  "terminate_all_read",
  "terminate_overlapping_read",
  "terminate_write",
  "terminate_dead",
  "mark_read",
  "mark_write",
  "mark_access"
};

static struct obstack rename_obstack;

static void do_replace (struct du_chain *, int);
static void scan_rtx_reg (rtx, rtx *, enum reg_class,
			  enum scan_actions, enum op_type, int);
static void scan_rtx_address (rtx, rtx *, enum reg_class,
			      enum scan_actions, enum machine_mode);
static void scan_rtx (rtx, rtx *, enum reg_class, enum scan_actions,
		      enum op_type, int);
static struct du_chain *build_def_use (basic_block);
static void dump_def_use_chain (struct du_chain *);
static void note_sets (rtx, rtx, void *);
static void clear_dead_regs (HARD_REG_SET *, enum machine_mode, rtx);
static void merge_overlapping_regs (basic_block, HARD_REG_SET *,
				    struct du_chain *);

/* Called through note_stores from update_life.  Find sets of registers, and
   record them in *DATA (which is actually a HARD_REG_SET *).  */

static void
note_sets (rtx x, rtx set ATTRIBUTE_UNUSED, void *data)
{
  HARD_REG_SET *pset = (HARD_REG_SET *) data;
  unsigned int regno;
  int nregs;

  if (GET_CODE (x) == SUBREG)
    x = SUBREG_REG (x);
  if (!REG_P (x))
    return;
  regno = REGNO (x);
  nregs = hard_regno_nregs[regno][GET_MODE (x)];

  /* There must not be pseudos at this point.  */
  gcc_assert (regno + nregs <= FIRST_PSEUDO_REGISTER);

  while (nregs-- > 0)
    SET_HARD_REG_BIT (*pset, regno + nregs);
}

/* Clear all registers from *PSET for which a note of kind KIND can be found
   in the list NOTES.  */

static void
clear_dead_regs (HARD_REG_SET *pset, enum machine_mode kind, rtx notes)
{
  rtx note;
  for (note = notes; note; note = XEXP (note, 1))
    if (REG_NOTE_KIND (note) == kind && REG_P (XEXP (note, 0)))
      {
	rtx reg = XEXP (note, 0);
	unsigned int regno = REGNO (reg);
	int nregs = hard_regno_nregs[regno][GET_MODE (reg)];

	/* There must not be pseudos at this point.  */
	gcc_assert (regno + nregs <= FIRST_PSEUDO_REGISTER);

	while (nregs-- > 0)
	  CLEAR_HARD_REG_BIT (*pset, regno + nregs);
      }
}

/* For a def-use chain CHAIN in basic block B, find which registers overlap
   its lifetime and set the corresponding bits in *PSET.  */

static void
merge_overlapping_regs (basic_block b, HARD_REG_SET *pset,
			struct du_chain *chain)
{
  struct du_chain *t = chain;
  rtx insn;
  HARD_REG_SET live;

  REG_SET_TO_HARD_REG_SET (live, b->il.rtl->global_live_at_start);
  insn = BB_HEAD (b);
  while (t)
    {
      /* Search forward until the next reference to the register to be
	 renamed.  */
      while (insn != t->insn)
	{
	  if (INSN_P (insn))
	    {
	      clear_dead_regs (&live, REG_DEAD, REG_NOTES (insn));
	      note_stores (PATTERN (insn), note_sets, (void *) &live);
	      /* Only record currently live regs if we are inside the
		 reg's live range.  */
	      if (t != chain)
		IOR_HARD_REG_SET (*pset, live);
	      clear_dead_regs (&live, REG_UNUSED, REG_NOTES (insn));
	    }
	  insn = NEXT_INSN (insn);
	}

      IOR_HARD_REG_SET (*pset, live);

      /* For the last reference, also merge in all registers set in the
	 same insn.
	 @@@ We only have take earlyclobbered sets into account.  */
      if (! t->next_use)
	note_stores (PATTERN (insn), note_sets, (void *) pset);

      t = t->next_use;
    }
}

/* Perform register renaming on the current function.  */

static void
regrename_optimize (void)
{
  int tick[FIRST_PSEUDO_REGISTER];
  int this_tick = 0;
  basic_block bb;
  char *first_obj;

  memset (tick, 0, sizeof tick);

  gcc_obstack_init (&rename_obstack);
  first_obj = obstack_alloc (&rename_obstack, 0);

  FOR_EACH_BB (bb)
    {
      struct du_chain *all_chains = 0;
      HARD_REG_SET unavailable;
      HARD_REG_SET regs_seen;

      CLEAR_HARD_REG_SET (unavailable);

      if (dump_file)
	fprintf (dump_file, "\nBasic block %d:\n", bb->index);

      all_chains = build_def_use (bb);

      if (dump_file)
	dump_def_use_chain (all_chains);

      CLEAR_HARD_REG_SET (unavailable);
      /* Don't clobber traceback for noreturn functions.  */
      if (frame_pointer_needed)
	{
	  int i;

	  for (i = hard_regno_nregs[FRAME_POINTER_REGNUM][Pmode]; i--;)
	    SET_HARD_REG_BIT (unavailable, FRAME_POINTER_REGNUM + i);

#if FRAME_POINTER_REGNUM != HARD_FRAME_POINTER_REGNUM
	  for (i = hard_regno_nregs[HARD_FRAME_POINTER_REGNUM][Pmode]; i--;)
	    SET_HARD_REG_BIT (unavailable, HARD_FRAME_POINTER_REGNUM + i);
#endif
	}

      CLEAR_HARD_REG_SET (regs_seen);
      while (all_chains)
	{
	  int new_reg, best_new_reg;
	  int n_uses;
	  struct du_chain *this = all_chains;
	  struct du_chain *tmp, *last;
	  HARD_REG_SET this_unavailable;
	  int reg = REGNO (*this->loc);
	  int i;

	  all_chains = this->next_chain;

	  best_new_reg = reg;

#if 0 /* This just disables optimization opportunities.  */
	  /* Only rename once we've seen the reg more than once.  */
	  if (! TEST_HARD_REG_BIT (regs_seen, reg))
	    {
	      SET_HARD_REG_BIT (regs_seen, reg);
	      continue;
	    }
#endif

	  if (fixed_regs[reg] || global_regs[reg]
#if FRAME_POINTER_REGNUM != HARD_FRAME_POINTER_REGNUM
	      || (frame_pointer_needed && reg == HARD_FRAME_POINTER_REGNUM)
#else
	      || (frame_pointer_needed && reg == FRAME_POINTER_REGNUM)
#endif
	      )
	    continue;

	  COPY_HARD_REG_SET (this_unavailable, unavailable);

	  /* Find last entry on chain (which has the need_caller_save bit),
	     count number of uses, and narrow the set of registers we can
	     use for renaming.  */
	  n_uses = 0;
	  for (last = this; last->next_use; last = last->next_use)
	    {
	      n_uses++;
	      IOR_COMPL_HARD_REG_SET (this_unavailable,
				      reg_class_contents[last->cl]);
	    }
	  if (n_uses < 1)
	    continue;

	  IOR_COMPL_HARD_REG_SET (this_unavailable,
				  reg_class_contents[last->cl]);

	  if (this->need_caller_save_reg)
	    IOR_HARD_REG_SET (this_unavailable, call_used_reg_set);

	  merge_overlapping_regs (bb, &this_unavailable, this);

	  /* Now potential_regs is a reasonable approximation, let's
	     have a closer look at each register still in there.  */
	  for (new_reg = 0; new_reg < FIRST_PSEUDO_REGISTER; new_reg++)
	    {
	      int nregs = hard_regno_nregs[new_reg][GET_MODE (*this->loc)];

	      for (i = nregs - 1; i >= 0; --i)
	        if (TEST_HARD_REG_BIT (this_unavailable, new_reg + i)
		    || fixed_regs[new_reg + i]
		    || global_regs[new_reg + i]
		    /* Can't use regs which aren't saved by the prologue.  */
		    || (! regs_ever_live[new_reg + i]
			&& ! call_used_regs[new_reg + i])
#ifdef LEAF_REGISTERS
		    /* We can't use a non-leaf register if we're in a
		       leaf function.  */
		    || (current_function_is_leaf
			&& !LEAF_REGISTERS[new_reg + i])
#endif
#ifdef HARD_REGNO_RENAME_OK
		    || ! HARD_REGNO_RENAME_OK (reg + i, new_reg + i)
#endif
		    )
		  break;
	      if (i >= 0)
		continue;

	      /* See whether it accepts all modes that occur in
		 definition and uses.  */
	      for (tmp = this; tmp; tmp = tmp->next_use)
		if (! HARD_REGNO_MODE_OK (new_reg, GET_MODE (*tmp->loc))
		    || (tmp->need_caller_save_reg
			&& ! (HARD_REGNO_CALL_PART_CLOBBERED
			      (reg, GET_MODE (*tmp->loc)))
			&& (HARD_REGNO_CALL_PART_CLOBBERED
			    (new_reg, GET_MODE (*tmp->loc)))))
		  break;
	      if (! tmp)
		{
		  if (tick[best_new_reg] > tick[new_reg])
		    best_new_reg = new_reg;
		}
	    }

	  if (dump_file)
	    {
	      fprintf (dump_file, "Register %s in insn %d",
		       reg_names[reg], INSN_UID (last->insn));
	      if (last->need_caller_save_reg)
		fprintf (dump_file, " crosses a call");
	    }

	  if (best_new_reg == reg)
	    {
	      tick[reg] = ++this_tick;
	      if (dump_file)
		fprintf (dump_file, "; no available better choice\n");
	      continue;
	    }

	  do_replace (this, best_new_reg);
	  tick[best_new_reg] = ++this_tick;
	  regs_ever_live[best_new_reg] = 1;

	  if (dump_file)
	    fprintf (dump_file, ", renamed as %s\n", reg_names[best_new_reg]);
	}

      obstack_free (&rename_obstack, first_obj);
    }

  obstack_free (&rename_obstack, NULL);

  if (dump_file)
    fputc ('\n', dump_file);

  count_or_remove_death_notes (NULL, 1);
  update_life_info (NULL, UPDATE_LIFE_LOCAL,
		    PROP_DEATH_NOTES);
}

static void
do_replace (struct du_chain *chain, int reg)
{
  while (chain)
    {
      unsigned int regno = ORIGINAL_REGNO (*chain->loc);
      struct reg_attrs * attr = REG_ATTRS (*chain->loc);

      *chain->loc = gen_raw_REG (GET_MODE (*chain->loc), reg);
      if (regno >= FIRST_PSEUDO_REGISTER)
	ORIGINAL_REGNO (*chain->loc) = regno;
      REG_ATTRS (*chain->loc) = attr;
      chain = chain->next_use;
    }
}


static struct du_chain *open_chains;
static struct du_chain *closed_chains;

static void
scan_rtx_reg (rtx insn, rtx *loc, enum reg_class cl,
	      enum scan_actions action, enum op_type type, int earlyclobber)
{
  struct du_chain **p;
  rtx x = *loc;
  enum machine_mode mode = GET_MODE (x);
  int this_regno = REGNO (x);
  int this_nregs = hard_regno_nregs[this_regno][mode];

  if (action == mark_write)
    {
      if (type == OP_OUT)
	{
	  struct du_chain *this
	    = obstack_alloc (&rename_obstack, sizeof (struct du_chain));
	  this->next_use = 0;
	  this->next_chain = open_chains;
	  this->loc = loc;
	  this->insn = insn;
	  this->cl = cl;
	  this->need_caller_save_reg = 0;
	  this->earlyclobber = earlyclobber;
	  open_chains = this;
	}
      return;
    }

  if ((type == OP_OUT) != (action == terminate_write || action == mark_access))
    return;

  for (p = &open_chains; *p;)
    {
      struct du_chain *this = *p;

      /* Check if the chain has been terminated if it has then skip to
	 the next chain.

	 This can happen when we've already appended the location to
	 the chain in Step 3, but are trying to hide in-out operands
	 from terminate_write in Step 5.  */

      if (*this->loc == cc0_rtx)
	p = &this->next_chain;
      else
	{
	  int regno = REGNO (*this->loc);
	  int nregs = hard_regno_nregs[regno][GET_MODE (*this->loc)];
	  int exact_match = (regno == this_regno && nregs == this_nregs);

	  if (regno + nregs <= this_regno
	      || this_regno + this_nregs <= regno)
	    {
	      p = &this->next_chain;
	      continue;
	    }

	  if (action == mark_read || action == mark_access)
	    {
	      gcc_assert (exact_match);

	      /* ??? Class NO_REGS can happen if the md file makes use of
		 EXTRA_CONSTRAINTS to match registers.  Which is arguably
		 wrong, but there we are.  Since we know not what this may
		 be replaced with, terminate the chain.  */
	      if (cl != NO_REGS)
		{
		  this = obstack_alloc (&rename_obstack, sizeof (struct du_chain));
		  this->next_use = 0;
		  this->next_chain = (*p)->next_chain;
		  this->loc = loc;
		  this->insn = insn;
		  this->cl = cl;
		  this->need_caller_save_reg = 0;
		  while (*p)
		    p = &(*p)->next_use;
		  *p = this;
		  return;
		}
	    }

	  if (action != terminate_overlapping_read || ! exact_match)
	    {
	      struct du_chain *next = this->next_chain;

	      /* Whether the terminated chain can be used for renaming
	         depends on the action and this being an exact match.
	         In either case, we remove this element from open_chains.  */

	      if ((action == terminate_dead || action == terminate_write)
		  && exact_match)
		{
		  this->next_chain = closed_chains;
		  closed_chains = this;
		  if (dump_file)
		    fprintf (dump_file,
			     "Closing chain %s at insn %d (%s)\n",
			     reg_names[REGNO (*this->loc)], INSN_UID (insn),
			     scan_actions_name[(int) action]);
		}
	      else
		{
		  if (dump_file)
		    fprintf (dump_file,
			     "Discarding chain %s at insn %d (%s)\n",
			     reg_names[REGNO (*this->loc)], INSN_UID (insn),
			     scan_actions_name[(int) action]);
		}
	      *p = next;
	    }
	  else
	    p = &this->next_chain;
	}
    }
}

/* Adapted from find_reloads_address_1.  CL is INDEX_REG_CLASS or
   BASE_REG_CLASS depending on how the register is being considered.  */

static void
scan_rtx_address (rtx insn, rtx *loc, enum reg_class cl,
		  enum scan_actions action, enum machine_mode mode)
{
  rtx x = *loc;
  RTX_CODE code = GET_CODE (x);
  const char *fmt;
  int i, j;

  if (action == mark_write || action == mark_access)
    return;

  switch (code)
    {
    case PLUS:
      {
	rtx orig_op0 = XEXP (x, 0);
	rtx orig_op1 = XEXP (x, 1);
	RTX_CODE code0 = GET_CODE (orig_op0);
	RTX_CODE code1 = GET_CODE (orig_op1);
	rtx op0 = orig_op0;
	rtx op1 = orig_op1;
	rtx *locI = NULL;
	rtx *locB = NULL;
	enum rtx_code index_code = SCRATCH;

	if (GET_CODE (op0) == SUBREG)
	  {
	    op0 = SUBREG_REG (op0);
	    code0 = GET_CODE (op0);
	  }

	if (GET_CODE (op1) == SUBREG)
	  {
	    op1 = SUBREG_REG (op1);
	    code1 = GET_CODE (op1);
	  }

	if (code0 == MULT || code0 == SIGN_EXTEND || code0 == TRUNCATE
	    || code0 == ZERO_EXTEND || code1 == MEM)
	  {
	    locI = &XEXP (x, 0);
	    locB = &XEXP (x, 1);
	    index_code = GET_CODE (*locI);
	  }
	else if (code1 == MULT || code1 == SIGN_EXTEND || code1 == TRUNCATE
		 || code1 == ZERO_EXTEND || code0 == MEM)
	  {
	    locI = &XEXP (x, 1);
	    locB = &XEXP (x, 0);
	    index_code = GET_CODE (*locI);
	  }
	else if (code0 == CONST_INT || code0 == CONST
		 || code0 == SYMBOL_REF || code0 == LABEL_REF)
	  {
	    locB = &XEXP (x, 1);
	    index_code = GET_CODE (XEXP (x, 0));
	  }
	else if (code1 == CONST_INT || code1 == CONST
		 || code1 == SYMBOL_REF || code1 == LABEL_REF)
	  {
	    locB = &XEXP (x, 0);
	    index_code = GET_CODE (XEXP (x, 1));
	  }
	else if (code0 == REG && code1 == REG)
	  {
	    int index_op;
	    unsigned regno0 = REGNO (op0), regno1 = REGNO (op1);

	    if (REGNO_OK_FOR_INDEX_P (regno0)
		&& regno_ok_for_base_p (regno1, mode, PLUS, REG))
	      index_op = 0;
	    else if (REGNO_OK_FOR_INDEX_P (regno1)
		     && regno_ok_for_base_p (regno0, mode, PLUS, REG))
	      index_op = 1;
	    else if (regno_ok_for_base_p (regno1, mode, PLUS, REG))
	      index_op = 0;
	    else if (regno_ok_for_base_p (regno0, mode, PLUS, REG))
	      index_op = 1;
	    else if (REGNO_OK_FOR_INDEX_P (regno1))
	      index_op = 1;
	    else
	      index_op = 0;

	    locI = &XEXP (x, index_op);
	    locB = &XEXP (x, !index_op);
	    index_code = GET_CODE (*locI);
	  }
	else if (code0 == REG)
	  {
	    locI = &XEXP (x, 0);
	    locB = &XEXP (x, 1);
	    index_code = GET_CODE (*locI);
	  }
	else if (code1 == REG)
	  {
	    locI = &XEXP (x, 1);
	    locB = &XEXP (x, 0);
	    index_code = GET_CODE (*locI);
	  }

	if (locI)
	  scan_rtx_address (insn, locI, INDEX_REG_CLASS, action, mode);
	if (locB)
	  scan_rtx_address (insn, locB, base_reg_class (mode, PLUS, index_code),
			    action, mode);

	return;
      }

    case POST_INC:
    case POST_DEC:
    case POST_MODIFY:
    case PRE_INC:
    case PRE_DEC:
    case PRE_MODIFY:
#ifndef AUTO_INC_DEC
      /* If the target doesn't claim to handle autoinc, this must be
	 something special, like a stack push.  Kill this chain.  */
      action = terminate_all_read;
#endif
      break;

    case MEM:
      scan_rtx_address (insn, &XEXP (x, 0),
			base_reg_class (GET_MODE (x), MEM, SCRATCH), action,
			GET_MODE (x));
      return;

    case REG:
      scan_rtx_reg (insn, loc, cl, action, OP_IN, 0);
      return;

    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	scan_rtx_address (insn, &XEXP (x, i), cl, action, mode);
      else if (fmt[i] == 'E')
	for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	  scan_rtx_address (insn, &XVECEXP (x, i, j), cl, action, mode);
    }
}

static void
scan_rtx (rtx insn, rtx *loc, enum reg_class cl,
	  enum scan_actions action, enum op_type type, int earlyclobber)
{
  const char *fmt;
  rtx x = *loc;
  enum rtx_code code = GET_CODE (x);
  int i, j;

  code = GET_CODE (x);
  switch (code)
    {
    case CONST:
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case SYMBOL_REF:
    case LABEL_REF:
    case CC0:
    case PC:
      return;

    case REG:
      scan_rtx_reg (insn, loc, cl, action, type, earlyclobber);
      return;

    case MEM:
      scan_rtx_address (insn, &XEXP (x, 0),
			base_reg_class (GET_MODE (x), MEM, SCRATCH), action,
			GET_MODE (x));
      return;

    case SET:
      scan_rtx (insn, &SET_SRC (x), cl, action, OP_IN, 0);
      scan_rtx (insn, &SET_DEST (x), cl, action,
		GET_CODE (PATTERN (insn)) == COND_EXEC ? OP_INOUT : OP_OUT, 0);
      return;

    case STRICT_LOW_PART:
      scan_rtx (insn, &XEXP (x, 0), cl, action, OP_INOUT, earlyclobber);
      return;

    case ZERO_EXTRACT:
    case SIGN_EXTRACT:
      scan_rtx (insn, &XEXP (x, 0), cl, action,
		type == OP_IN ? OP_IN : OP_INOUT, earlyclobber);
      scan_rtx (insn, &XEXP (x, 1), cl, action, OP_IN, 0);
      scan_rtx (insn, &XEXP (x, 2), cl, action, OP_IN, 0);
      return;

    case POST_INC:
    case PRE_INC:
    case POST_DEC:
    case PRE_DEC:
    case POST_MODIFY:
    case PRE_MODIFY:
      /* Should only happen inside MEM.  */
      gcc_unreachable ();

    case CLOBBER:
      scan_rtx (insn, &SET_DEST (x), cl, action,
		GET_CODE (PATTERN (insn)) == COND_EXEC ? OP_INOUT : OP_OUT, 0);
      return;

    case EXPR_LIST:
      scan_rtx (insn, &XEXP (x, 0), cl, action, type, 0);
      if (XEXP (x, 1))
	scan_rtx (insn, &XEXP (x, 1), cl, action, type, 0);
      return;

    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	scan_rtx (insn, &XEXP (x, i), cl, action, type, 0);
      else if (fmt[i] == 'E')
	for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	  scan_rtx (insn, &XVECEXP (x, i, j), cl, action, type, 0);
    }
}

/* Build def/use chain.  */

static struct du_chain *
build_def_use (basic_block bb)
{
  rtx insn;

  open_chains = closed_chains = NULL;

  for (insn = BB_HEAD (bb); ; insn = NEXT_INSN (insn))
    {
      if (INSN_P (insn))
	{
	  int n_ops;
	  rtx note;
	  rtx old_operands[MAX_RECOG_OPERANDS];
	  rtx old_dups[MAX_DUP_OPERANDS];
	  int i, icode;
	  int alt;
	  int predicated;

	  /* Process the insn, determining its effect on the def-use
	     chains.  We perform the following steps with the register
	     references in the insn:
	     (1) Any read that overlaps an open chain, but doesn't exactly
	         match, causes that chain to be closed.  We can't deal
	         with overlaps yet.
	     (2) Any read outside an operand causes any chain it overlaps
	         with to be closed, since we can't replace it.
	     (3) Any read inside an operand is added if there's already
	         an open chain for it.
	     (4) For any REG_DEAD note we find, close open chains that
	         overlap it.
	     (5) For any write we find, close open chains that overlap it.
	     (6) For any write we find in an operand, make a new chain.
	     (7) For any REG_UNUSED, close any chains we just opened.  */

	  icode = recog_memoized (insn);
	  extract_insn (insn);
	  if (! constrain_operands (1))
	    fatal_insn_not_found (insn);
	  preprocess_constraints ();
	  alt = which_alternative;
	  n_ops = recog_data.n_operands;

	  /* Simplify the code below by rewriting things to reflect
	     matching constraints.  Also promote OP_OUT to OP_INOUT
	     in predicated instructions.  */

	  predicated = GET_CODE (PATTERN (insn)) == COND_EXEC;
	  for (i = 0; i < n_ops; ++i)
	    {
	      int matches = recog_op_alt[i][alt].matches;
	      if (matches >= 0)
		recog_op_alt[i][alt].cl = recog_op_alt[matches][alt].cl;
	      if (matches >= 0 || recog_op_alt[i][alt].matched >= 0
	          || (predicated && recog_data.operand_type[i] == OP_OUT))
		recog_data.operand_type[i] = OP_INOUT;
	    }

	  /* Step 1: Close chains for which we have overlapping reads.  */
	  for (i = 0; i < n_ops; i++)
	    scan_rtx (insn, recog_data.operand_loc[i],
		      NO_REGS, terminate_overlapping_read,
		      recog_data.operand_type[i], 0);

	  /* Step 2: Close chains for which we have reads outside operands.
	     We do this by munging all operands into CC0, and closing
	     everything remaining.  */

	  for (i = 0; i < n_ops; i++)
	    {
	      old_operands[i] = recog_data.operand[i];
	      /* Don't squash match_operator or match_parallel here, since
		 we don't know that all of the contained registers are
		 reachable by proper operands.  */
	      if (recog_data.constraints[i][0] == '\0')
		continue;
	      *recog_data.operand_loc[i] = cc0_rtx;
	    }
	  for (i = 0; i < recog_data.n_dups; i++)
	    {
	      int dup_num = recog_data.dup_num[i];

	      old_dups[i] = *recog_data.dup_loc[i];
	      *recog_data.dup_loc[i] = cc0_rtx;

	      /* For match_dup of match_operator or match_parallel, share
		 them, so that we don't miss changes in the dup.  */
	      if (icode >= 0
		  && insn_data[icode].operand[dup_num].eliminable == 0)
		old_dups[i] = recog_data.operand[dup_num];
	    }

	  scan_rtx (insn, &PATTERN (insn), NO_REGS, terminate_all_read,
		    OP_IN, 0);

	  for (i = 0; i < recog_data.n_dups; i++)
	    *recog_data.dup_loc[i] = old_dups[i];
	  for (i = 0; i < n_ops; i++)
	    *recog_data.operand_loc[i] = old_operands[i];

	  /* Step 2B: Can't rename function call argument registers.  */
	  if (CALL_P (insn) && CALL_INSN_FUNCTION_USAGE (insn))
	    scan_rtx (insn, &CALL_INSN_FUNCTION_USAGE (insn),
		      NO_REGS, terminate_all_read, OP_IN, 0);

	  /* Step 2C: Can't rename asm operands that were originally
	     hard registers.  */
	  if (asm_noperands (PATTERN (insn)) > 0)
	    for (i = 0; i < n_ops; i++)
	      {
		rtx *loc = recog_data.operand_loc[i];
		rtx op = *loc;

		if (REG_P (op)
		    && REGNO (op) == ORIGINAL_REGNO (op)
		    && (recog_data.operand_type[i] == OP_IN
			|| recog_data.operand_type[i] == OP_INOUT))
		  scan_rtx (insn, loc, NO_REGS, terminate_all_read, OP_IN, 0);
	      }

	  /* Step 3: Append to chains for reads inside operands.  */
	  for (i = 0; i < n_ops + recog_data.n_dups; i++)
	    {
	      int opn = i < n_ops ? i : recog_data.dup_num[i - n_ops];
	      rtx *loc = (i < n_ops
			  ? recog_data.operand_loc[opn]
			  : recog_data.dup_loc[i - n_ops]);
	      enum reg_class cl = recog_op_alt[opn][alt].cl;
	      enum op_type type = recog_data.operand_type[opn];

	      /* Don't scan match_operand here, since we've no reg class
		 information to pass down.  Any operands that we could
		 substitute in will be represented elsewhere.  */
	      if (recog_data.constraints[opn][0] == '\0')
		continue;

	      if (recog_op_alt[opn][alt].is_address)
		scan_rtx_address (insn, loc, cl, mark_read, VOIDmode);
	      else
		scan_rtx (insn, loc, cl, mark_read, type, 0);
	    }

	  /* Step 3B: Record updates for regs in REG_INC notes, and
	     source regs in REG_FRAME_RELATED_EXPR notes.  */
	  for (note = REG_NOTES (insn); note; note = XEXP (note, 1))
	    if (REG_NOTE_KIND (note) == REG_INC
		|| REG_NOTE_KIND (note) == REG_FRAME_RELATED_EXPR)
	      scan_rtx (insn, &XEXP (note, 0), ALL_REGS, mark_read,
			OP_INOUT, 0);

	  /* Step 4: Close chains for registers that die here.  */
	  for (note = REG_NOTES (insn); note; note = XEXP (note, 1))
	    if (REG_NOTE_KIND (note) == REG_DEAD)
	      scan_rtx (insn, &XEXP (note, 0), NO_REGS, terminate_dead,
			OP_IN, 0);

	  /* Step 4B: If this is a call, any chain live at this point
	     requires a caller-saved reg.  */
	  if (CALL_P (insn))
	    {
	      struct du_chain *p;
	      for (p = open_chains; p; p = p->next_chain)
		p->need_caller_save_reg = 1;
	    }

	  /* Step 5: Close open chains that overlap writes.  Similar to
	     step 2, we hide in-out operands, since we do not want to
	     close these chains.  */

	  for (i = 0; i < n_ops; i++)
	    {
	      old_operands[i] = recog_data.operand[i];
	      if (recog_data.operand_type[i] == OP_INOUT)
		*recog_data.operand_loc[i] = cc0_rtx;
	    }
	  for (i = 0; i < recog_data.n_dups; i++)
	    {
	      int opn = recog_data.dup_num[i];
	      old_dups[i] = *recog_data.dup_loc[i];
	      if (recog_data.operand_type[opn] == OP_INOUT)
		*recog_data.dup_loc[i] = cc0_rtx;
	    }

	  scan_rtx (insn, &PATTERN (insn), NO_REGS, terminate_write, OP_IN, 0);

	  for (i = 0; i < recog_data.n_dups; i++)
	    *recog_data.dup_loc[i] = old_dups[i];
	  for (i = 0; i < n_ops; i++)
	    *recog_data.operand_loc[i] = old_operands[i];

	  /* Step 6: Begin new chains for writes inside operands.  */
	  /* ??? Many targets have output constraints on the SET_DEST
	     of a call insn, which is stupid, since these are certainly
	     ABI defined hard registers.  Don't change calls at all.
	     Similarly take special care for asm statement that originally
	     referenced hard registers.  */
	  if (asm_noperands (PATTERN (insn)) > 0)
	    {
	      for (i = 0; i < n_ops; i++)
		if (recog_data.operand_type[i] == OP_OUT)
		  {
		    rtx *loc = recog_data.operand_loc[i];
		    rtx op = *loc;
		    enum reg_class cl = recog_op_alt[i][alt].cl;

		    if (REG_P (op)
			&& REGNO (op) == ORIGINAL_REGNO (op))
		      continue;

		    scan_rtx (insn, loc, cl, mark_write, OP_OUT,
			      recog_op_alt[i][alt].earlyclobber);
		  }
	    }
	  else if (!CALL_P (insn))
	    for (i = 0; i < n_ops + recog_data.n_dups; i++)
	      {
		int opn = i < n_ops ? i : recog_data.dup_num[i - n_ops];
		rtx *loc = (i < n_ops
			    ? recog_data.operand_loc[opn]
			    : recog_data.dup_loc[i - n_ops]);
		enum reg_class cl = recog_op_alt[opn][alt].cl;

		if (recog_data.operand_type[opn] == OP_OUT)
		  scan_rtx (insn, loc, cl, mark_write, OP_OUT,
			    recog_op_alt[opn][alt].earlyclobber);
	      }

	  /* Step 6B: Record destination regs in REG_FRAME_RELATED_EXPR
	     notes for update.  */
	  for (note = REG_NOTES (insn); note; note = XEXP (note, 1))
	    if (REG_NOTE_KIND (note) == REG_FRAME_RELATED_EXPR)
	      scan_rtx (insn, &XEXP (note, 0), ALL_REGS, mark_access,
			OP_INOUT, 0);

	  /* Step 7: Close chains for registers that were never
	     really used here.  */
	  for (note = REG_NOTES (insn); note; note = XEXP (note, 1))
	    if (REG_NOTE_KIND (note) == REG_UNUSED)
	      scan_rtx (insn, &XEXP (note, 0), NO_REGS, terminate_dead,
			OP_IN, 0);
	}
      if (insn == BB_END (bb))
	break;
    }

  /* Since we close every chain when we find a REG_DEAD note, anything that
     is still open lives past the basic block, so it can't be renamed.  */
  return closed_chains;
}

/* Dump all def/use chains in CHAINS to DUMP_FILE.  They are
   printed in reverse order as that's how we build them.  */

static void
dump_def_use_chain (struct du_chain *chains)
{
  while (chains)
    {
      struct du_chain *this = chains;
      int r = REGNO (*this->loc);
      int nregs = hard_regno_nregs[r][GET_MODE (*this->loc)];
      fprintf (dump_file, "Register %s (%d):", reg_names[r], nregs);
      while (this)
	{
	  fprintf (dump_file, " %d [%s]", INSN_UID (this->insn),
		   reg_class_names[this->cl]);
	  this = this->next_use;
	}
      fprintf (dump_file, "\n");
      chains = chains->next_chain;
    }
}

/* The following code does forward propagation of hard register copies.
   The object is to eliminate as many dependencies as possible, so that
   we have the most scheduling freedom.  As a side effect, we also clean
   up some silly register allocation decisions made by reload.  This
   code may be obsoleted by a new register allocator.  */

/* For each register, we have a list of registers that contain the same
   value.  The OLDEST_REGNO field points to the head of the list, and
   the NEXT_REGNO field runs through the list.  The MODE field indicates
   what mode the data is known to be in; this field is VOIDmode when the
   register is not known to contain valid data.  */

struct value_data_entry
{
  enum machine_mode mode;
  unsigned int oldest_regno;
  unsigned int next_regno;
};

struct value_data
{
  struct value_data_entry e[FIRST_PSEUDO_REGISTER];
  unsigned int max_value_regs;
};

static void kill_value_one_regno (unsigned, struct value_data *);
static void kill_value_regno (unsigned, unsigned, struct value_data *);
static void kill_value (rtx, struct value_data *);
static void set_value_regno (unsigned, enum machine_mode, struct value_data *);
static void init_value_data (struct value_data *);
static void kill_clobbered_value (rtx, rtx, void *);
static void kill_set_value (rtx, rtx, void *);
static int kill_autoinc_value (rtx *, void *);
static void copy_value (rtx, rtx, struct value_data *);
static bool mode_change_ok (enum machine_mode, enum machine_mode,
			    unsigned int);
static rtx maybe_mode_change (enum machine_mode, enum machine_mode,
			      enum machine_mode, unsigned int, unsigned int);
static rtx find_oldest_value_reg (enum reg_class, rtx, struct value_data *);
static bool replace_oldest_value_reg (rtx *, enum reg_class, rtx,
				      struct value_data *);
static bool replace_oldest_value_addr (rtx *, enum reg_class,
				       enum machine_mode, rtx,
				       struct value_data *);
static bool replace_oldest_value_mem (rtx, rtx, struct value_data *);
static bool copyprop_hardreg_forward_1 (basic_block, struct value_data *);
extern void debug_value_data (struct value_data *);
#ifdef ENABLE_CHECKING
static void validate_value_data (struct value_data *);
#endif

/* Kill register REGNO.  This involves removing it from any value
   lists, and resetting the value mode to VOIDmode.  This is only a
   helper function; it does not handle any hard registers overlapping
   with REGNO.  */

static void
kill_value_one_regno (unsigned int regno, struct value_data *vd)
{
  unsigned int i, next;

  if (vd->e[regno].oldest_regno != regno)
    {
      for (i = vd->e[regno].oldest_regno;
	   vd->e[i].next_regno != regno;
	   i = vd->e[i].next_regno)
	continue;
      vd->e[i].next_regno = vd->e[regno].next_regno;
    }
  else if ((next = vd->e[regno].next_regno) != INVALID_REGNUM)
    {
      for (i = next; i != INVALID_REGNUM; i = vd->e[i].next_regno)
	vd->e[i].oldest_regno = next;
    }

  vd->e[regno].mode = VOIDmode;
  vd->e[regno].oldest_regno = regno;
  vd->e[regno].next_regno = INVALID_REGNUM;

#ifdef ENABLE_CHECKING
  validate_value_data (vd);
#endif
}

/* Kill the value in register REGNO for NREGS, and any other registers
   whose values overlap.  */

static void
kill_value_regno (unsigned int regno, unsigned int nregs,
		  struct value_data *vd)
{
  unsigned int j;

  /* Kill the value we're told to kill.  */
  for (j = 0; j < nregs; ++j)
    kill_value_one_regno (regno + j, vd);

  /* Kill everything that overlapped what we're told to kill.  */
  if (regno < vd->max_value_regs)
    j = 0;
  else
    j = regno - vd->max_value_regs;
  for (; j < regno; ++j)
    {
      unsigned int i, n;
      if (vd->e[j].mode == VOIDmode)
	continue;
      n = hard_regno_nregs[j][vd->e[j].mode];
      if (j + n > regno)
	for (i = 0; i < n; ++i)
	  kill_value_one_regno (j + i, vd);
    }
}

/* Kill X.  This is a convenience function wrapping kill_value_regno
   so that we mind the mode the register is in.  */

static void
kill_value (rtx x, struct value_data *vd)
{
  rtx orig_rtx = x;

  if (GET_CODE (x) == SUBREG)
    {
      x = simplify_subreg (GET_MODE (x), SUBREG_REG (x),
			   GET_MODE (SUBREG_REG (x)), SUBREG_BYTE (x));
      if (x == NULL_RTX)
	x = SUBREG_REG (orig_rtx);
    }
  if (REG_P (x))
    {
      unsigned int regno = REGNO (x);
      unsigned int n = hard_regno_nregs[regno][GET_MODE (x)];

      kill_value_regno (regno, n, vd);
    }
}

/* Remember that REGNO is valid in MODE.  */

static void
set_value_regno (unsigned int regno, enum machine_mode mode,
		 struct value_data *vd)
{
  unsigned int nregs;

  vd->e[regno].mode = mode;

  nregs = hard_regno_nregs[regno][mode];
  if (nregs > vd->max_value_regs)
    vd->max_value_regs = nregs;
}

/* Initialize VD such that there are no known relationships between regs.  */

static void
init_value_data (struct value_data *vd)
{
  int i;
  for (i = 0; i < FIRST_PSEUDO_REGISTER; ++i)
    {
      vd->e[i].mode = VOIDmode;
      vd->e[i].oldest_regno = i;
      vd->e[i].next_regno = INVALID_REGNUM;
    }
  vd->max_value_regs = 0;
}

/* Called through note_stores.  If X is clobbered, kill its value.  */

static void
kill_clobbered_value (rtx x, rtx set, void *data)
{
  struct value_data *vd = data;
  if (GET_CODE (set) == CLOBBER)
    kill_value (x, vd);
}

/* Called through note_stores.  If X is set, not clobbered, kill its
   current value and install it as the root of its own value list.  */

static void
kill_set_value (rtx x, rtx set, void *data)
{
  struct value_data *vd = data;
  if (GET_CODE (set) != CLOBBER)
    {
      kill_value (x, vd);
      if (REG_P (x))
	set_value_regno (REGNO (x), GET_MODE (x), vd);
    }
}

/* Called through for_each_rtx.  Kill any register used as the base of an
   auto-increment expression, and install that register as the root of its
   own value list.  */

static int
kill_autoinc_value (rtx *px, void *data)
{
  rtx x = *px;
  struct value_data *vd = data;

  if (GET_RTX_CLASS (GET_CODE (x)) == RTX_AUTOINC)
    {
      x = XEXP (x, 0);
      kill_value (x, vd);
      set_value_regno (REGNO (x), Pmode, vd);
      return -1;
    }

  return 0;
}

/* Assert that SRC has been copied to DEST.  Adjust the data structures
   to reflect that SRC contains an older copy of the shared value.  */

static void
copy_value (rtx dest, rtx src, struct value_data *vd)
{
  unsigned int dr = REGNO (dest);
  unsigned int sr = REGNO (src);
  unsigned int dn, sn;
  unsigned int i;

  /* ??? At present, it's possible to see noop sets.  It'd be nice if
     this were cleaned up beforehand...  */
  if (sr == dr)
    return;

  /* Do not propagate copies to the stack pointer, as that can leave
     memory accesses with no scheduling dependency on the stack update.  */
  if (dr == STACK_POINTER_REGNUM)
    return;

  /* Likewise with the frame pointer, if we're using one.  */
  if (frame_pointer_needed && dr == HARD_FRAME_POINTER_REGNUM)
    return;

  /* Do not propagate copies to fixed or global registers, patterns
     can be relying to see particular fixed register or users can
     expect the chosen global register in asm.  */
  if (fixed_regs[dr] || global_regs[dr])
    return;

  /* If SRC and DEST overlap, don't record anything.  */
  dn = hard_regno_nregs[dr][GET_MODE (dest)];
  sn = hard_regno_nregs[sr][GET_MODE (dest)];
  if ((dr > sr && dr < sr + sn)
      || (sr > dr && sr < dr + dn))
    return;

  /* If SRC had no assigned mode (i.e. we didn't know it was live)
     assign it now and assume the value came from an input argument
     or somesuch.  */
  if (vd->e[sr].mode == VOIDmode)
    set_value_regno (sr, vd->e[dr].mode, vd);

  /* If we are narrowing the input to a smaller number of hard regs,
     and it is in big endian, we are really extracting a high part.
     Since we generally associate a low part of a value with the value itself,
     we must not do the same for the high part.
     Note we can still get low parts for the same mode combination through
     a two-step copy involving differently sized hard regs.
     Assume hard regs fr* are 32 bits bits each, while r* are 64 bits each:
     (set (reg:DI r0) (reg:DI fr0))
     (set (reg:SI fr2) (reg:SI r0))
     loads the low part of (reg:DI fr0) - i.e. fr1 - into fr2, while:
     (set (reg:SI fr2) (reg:SI fr0))
     loads the high part of (reg:DI fr0) into fr2.

     We can't properly represent the latter case in our tables, so don't
     record anything then.  */
  else if (sn < (unsigned int) hard_regno_nregs[sr][vd->e[sr].mode]
	   && (GET_MODE_SIZE (vd->e[sr].mode) > UNITS_PER_WORD
	       ? WORDS_BIG_ENDIAN : BYTES_BIG_ENDIAN))
    return;

  /* If SRC had been assigned a mode narrower than the copy, we can't
     link DEST into the chain, because not all of the pieces of the
     copy came from oldest_regno.  */
  else if (sn > (unsigned int) hard_regno_nregs[sr][vd->e[sr].mode])
    return;

  /* Link DR at the end of the value chain used by SR.  */

  vd->e[dr].oldest_regno = vd->e[sr].oldest_regno;

  for (i = sr; vd->e[i].next_regno != INVALID_REGNUM; i = vd->e[i].next_regno)
    continue;
  vd->e[i].next_regno = dr;

#ifdef ENABLE_CHECKING
  validate_value_data (vd);
#endif
}

/* Return true if a mode change from ORIG to NEW is allowed for REGNO.  */

static bool
mode_change_ok (enum machine_mode orig_mode, enum machine_mode new_mode,
		unsigned int regno ATTRIBUTE_UNUSED)
{
  if (GET_MODE_SIZE (orig_mode) < GET_MODE_SIZE (new_mode))
    return false;

#ifdef CANNOT_CHANGE_MODE_CLASS
  return !REG_CANNOT_CHANGE_MODE_P (regno, orig_mode, new_mode);
#endif

  return true;
}

/* Register REGNO was originally set in ORIG_MODE.  It - or a copy of it -
   was copied in COPY_MODE to COPY_REGNO, and then COPY_REGNO was accessed
   in NEW_MODE.
   Return a NEW_MODE rtx for REGNO if that's OK, otherwise return NULL_RTX.  */

static rtx
maybe_mode_change (enum machine_mode orig_mode, enum machine_mode copy_mode,
		   enum machine_mode new_mode, unsigned int regno,
		   unsigned int copy_regno ATTRIBUTE_UNUSED)
{
  if (orig_mode == new_mode)
    return gen_rtx_raw_REG (new_mode, regno);
  else if (mode_change_ok (orig_mode, new_mode, regno))
    {
      int copy_nregs = hard_regno_nregs[copy_regno][copy_mode];
      int use_nregs = hard_regno_nregs[copy_regno][new_mode];
      int copy_offset
	= GET_MODE_SIZE (copy_mode) / copy_nregs * (copy_nregs - use_nregs);
      int offset
	= GET_MODE_SIZE (orig_mode) - GET_MODE_SIZE (new_mode) - copy_offset;
      int byteoffset = offset % UNITS_PER_WORD;
      int wordoffset = offset - byteoffset;

      offset = ((WORDS_BIG_ENDIAN ? wordoffset : 0)
		+ (BYTES_BIG_ENDIAN ? byteoffset : 0));
      return gen_rtx_raw_REG (new_mode,
			      regno + subreg_regno_offset (regno, orig_mode,
							   offset,
							   new_mode));
    }
  return NULL_RTX;
}

/* Find the oldest copy of the value contained in REGNO that is in
   register class CL and has mode MODE.  If found, return an rtx
   of that oldest register, otherwise return NULL.  */

static rtx
find_oldest_value_reg (enum reg_class cl, rtx reg, struct value_data *vd)
{
  unsigned int regno = REGNO (reg);
  enum machine_mode mode = GET_MODE (reg);
  unsigned int i;

  /* If we are accessing REG in some mode other that what we set it in,
     make sure that the replacement is valid.  In particular, consider
	(set (reg:DI r11) (...))
	(set (reg:SI r9) (reg:SI r11))
	(set (reg:SI r10) (...))
	(set (...) (reg:DI r9))
     Replacing r9 with r11 is invalid.  */
  if (mode != vd->e[regno].mode)
    {
      if (hard_regno_nregs[regno][mode]
	  > hard_regno_nregs[regno][vd->e[regno].mode])
	return NULL_RTX;
    }

  for (i = vd->e[regno].oldest_regno; i != regno; i = vd->e[i].next_regno)
    {
      enum machine_mode oldmode = vd->e[i].mode;
      rtx new;
      unsigned int last;

      for (last = i; last < i + hard_regno_nregs[i][mode]; last++)
	if (!TEST_HARD_REG_BIT (reg_class_contents[cl], last))
	  return NULL_RTX;

      new = maybe_mode_change (oldmode, vd->e[regno].mode, mode, i, regno);
      if (new)
	{
	  ORIGINAL_REGNO (new) = ORIGINAL_REGNO (reg);
	  REG_ATTRS (new) = REG_ATTRS (reg);
	  return new;
	}
    }

  return NULL_RTX;
}

/* If possible, replace the register at *LOC with the oldest register
   in register class CL.  Return true if successfully replaced.  */

static bool
replace_oldest_value_reg (rtx *loc, enum reg_class cl, rtx insn,
			  struct value_data *vd)
{
  rtx new = find_oldest_value_reg (cl, *loc, vd);
  if (new)
    {
      if (dump_file)
	fprintf (dump_file, "insn %u: replaced reg %u with %u\n",
		 INSN_UID (insn), REGNO (*loc), REGNO (new));

      validate_change (insn, loc, new, 1);
      return true;
    }
  return false;
}

/* Similar to replace_oldest_value_reg, but *LOC contains an address.
   Adapted from find_reloads_address_1.  CL is INDEX_REG_CLASS or
   BASE_REG_CLASS depending on how the register is being considered.  */

static bool
replace_oldest_value_addr (rtx *loc, enum reg_class cl,
			   enum machine_mode mode, rtx insn,
			   struct value_data *vd)
{
  rtx x = *loc;
  RTX_CODE code = GET_CODE (x);
  const char *fmt;
  int i, j;
  bool changed = false;

  switch (code)
    {
    case PLUS:
      {
	rtx orig_op0 = XEXP (x, 0);
	rtx orig_op1 = XEXP (x, 1);
	RTX_CODE code0 = GET_CODE (orig_op0);
	RTX_CODE code1 = GET_CODE (orig_op1);
	rtx op0 = orig_op0;
	rtx op1 = orig_op1;
	rtx *locI = NULL;
	rtx *locB = NULL;
	enum rtx_code index_code = SCRATCH;

	if (GET_CODE (op0) == SUBREG)
	  {
	    op0 = SUBREG_REG (op0);
	    code0 = GET_CODE (op0);
	  }

	if (GET_CODE (op1) == SUBREG)
	  {
	    op1 = SUBREG_REG (op1);
	    code1 = GET_CODE (op1);
	  }

	if (code0 == MULT || code0 == SIGN_EXTEND || code0 == TRUNCATE
	    || code0 == ZERO_EXTEND || code1 == MEM)
	  {
	    locI = &XEXP (x, 0);
	    locB = &XEXP (x, 1);
	    index_code = GET_CODE (*locI);
	  }
	else if (code1 == MULT || code1 == SIGN_EXTEND || code1 == TRUNCATE
		 || code1 == ZERO_EXTEND || code0 == MEM)
	  {
	    locI = &XEXP (x, 1);
	    locB = &XEXP (x, 0);
	    index_code = GET_CODE (*locI);
	  }
	else if (code0 == CONST_INT || code0 == CONST
		 || code0 == SYMBOL_REF || code0 == LABEL_REF)
	  {
	    locB = &XEXP (x, 1);
	    index_code = GET_CODE (XEXP (x, 0));
	  }
	else if (code1 == CONST_INT || code1 == CONST
		 || code1 == SYMBOL_REF || code1 == LABEL_REF)
	  {
	    locB = &XEXP (x, 0);
	    index_code = GET_CODE (XEXP (x, 1));
	  }
	else if (code0 == REG && code1 == REG)
	  {
	    int index_op;
	    unsigned regno0 = REGNO (op0), regno1 = REGNO (op1);

	    if (REGNO_OK_FOR_INDEX_P (regno0)
		&& regno_ok_for_base_p (regno1, mode, PLUS, REG))
	      index_op = 0;
	    else if (REGNO_OK_FOR_INDEX_P (regno1)
		     && regno_ok_for_base_p (regno0, mode, PLUS, REG))
	      index_op = 1;
	    else if (regno_ok_for_base_p (regno1, mode, PLUS, REG))
	      index_op = 0;
	    else if (regno_ok_for_base_p (regno0, mode, PLUS, REG))
	      index_op = 1;
	    else if (REGNO_OK_FOR_INDEX_P (regno1))
	      index_op = 1;
	    else
	      index_op = 0;

	    locI = &XEXP (x, index_op);
	    locB = &XEXP (x, !index_op);
	    index_code = GET_CODE (*locI);
	  }
	else if (code0 == REG)
	  {
	    locI = &XEXP (x, 0);
	    locB = &XEXP (x, 1);
	    index_code = GET_CODE (*locI);
	  }
	else if (code1 == REG)
	  {
	    locI = &XEXP (x, 1);
	    locB = &XEXP (x, 0);
	    index_code = GET_CODE (*locI);
	  }

	if (locI)
	  changed |= replace_oldest_value_addr (locI, INDEX_REG_CLASS, mode,
						insn, vd);
	if (locB)
	  changed |= replace_oldest_value_addr (locB,
						base_reg_class (mode, PLUS,
								index_code),
						mode, insn, vd);
	return changed;
      }

    case POST_INC:
    case POST_DEC:
    case POST_MODIFY:
    case PRE_INC:
    case PRE_DEC:
    case PRE_MODIFY:
      return false;

    case MEM:
      return replace_oldest_value_mem (x, insn, vd);

    case REG:
      return replace_oldest_value_reg (loc, cl, insn, vd);

    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	changed |= replace_oldest_value_addr (&XEXP (x, i), cl, mode,
					      insn, vd);
      else if (fmt[i] == 'E')
	for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	  changed |= replace_oldest_value_addr (&XVECEXP (x, i, j), cl,
						mode, insn, vd);
    }

  return changed;
}

/* Similar to replace_oldest_value_reg, but X contains a memory.  */

static bool
replace_oldest_value_mem (rtx x, rtx insn, struct value_data *vd)
{
  return replace_oldest_value_addr (&XEXP (x, 0),
				    base_reg_class (GET_MODE (x), MEM,
						    SCRATCH),
				    GET_MODE (x), insn, vd);
}

/* Perform the forward copy propagation on basic block BB.  */

static bool
copyprop_hardreg_forward_1 (basic_block bb, struct value_data *vd)
{
  bool changed = false;
  rtx insn;

  for (insn = BB_HEAD (bb); ; insn = NEXT_INSN (insn))
    {
      int n_ops, i, alt, predicated;
      bool is_asm, any_replacements;
      rtx set;
      bool replaced[MAX_RECOG_OPERANDS];

      if (! INSN_P (insn))
	{
	  if (insn == BB_END (bb))
	    break;
	  else
	    continue;
	}

      set = single_set (insn);
      extract_insn (insn);
      if (! constrain_operands (1))
	fatal_insn_not_found (insn);
      preprocess_constraints ();
      alt = which_alternative;
      n_ops = recog_data.n_operands;
      is_asm = asm_noperands (PATTERN (insn)) >= 0;

      /* Simplify the code below by rewriting things to reflect
	 matching constraints.  Also promote OP_OUT to OP_INOUT
	 in predicated instructions.  */

      predicated = GET_CODE (PATTERN (insn)) == COND_EXEC;
      for (i = 0; i < n_ops; ++i)
	{
	  int matches = recog_op_alt[i][alt].matches;
	  if (matches >= 0)
	    recog_op_alt[i][alt].cl = recog_op_alt[matches][alt].cl;
	  if (matches >= 0 || recog_op_alt[i][alt].matched >= 0
	      || (predicated && recog_data.operand_type[i] == OP_OUT))
	    recog_data.operand_type[i] = OP_INOUT;
	}

      /* For each earlyclobber operand, zap the value data.  */
      for (i = 0; i < n_ops; i++)
	if (recog_op_alt[i][alt].earlyclobber)
	  kill_value (recog_data.operand[i], vd);

      /* Within asms, a clobber cannot overlap inputs or outputs.
	 I wouldn't think this were true for regular insns, but
	 scan_rtx treats them like that...  */
      note_stores (PATTERN (insn), kill_clobbered_value, vd);

      /* Kill all auto-incremented values.  */
      /* ??? REG_INC is useless, since stack pushes aren't done that way.  */
      for_each_rtx (&PATTERN (insn), kill_autoinc_value, vd);

      /* Kill all early-clobbered operands.  */
      for (i = 0; i < n_ops; i++)
	if (recog_op_alt[i][alt].earlyclobber)
	  kill_value (recog_data.operand[i], vd);

      /* Special-case plain move instructions, since we may well
	 be able to do the move from a different register class.  */
      if (set && REG_P (SET_SRC (set)))
	{
	  rtx src = SET_SRC (set);
	  unsigned int regno = REGNO (src);
	  enum machine_mode mode = GET_MODE (src);
	  unsigned int i;
	  rtx new;

	  /* If we are accessing SRC in some mode other that what we
	     set it in, make sure that the replacement is valid.  */
	  if (mode != vd->e[regno].mode)
	    {
	      if (hard_regno_nregs[regno][mode]
		  > hard_regno_nregs[regno][vd->e[regno].mode])
		goto no_move_special_case;
	    }

	  /* If the destination is also a register, try to find a source
	     register in the same class.  */
	  if (REG_P (SET_DEST (set)))
	    {
	      new = find_oldest_value_reg (REGNO_REG_CLASS (regno), src, vd);
	      if (new && validate_change (insn, &SET_SRC (set), new, 0))
		{
		  if (dump_file)
		    fprintf (dump_file,
			     "insn %u: replaced reg %u with %u\n",
			     INSN_UID (insn), regno, REGNO (new));
		  changed = true;
		  goto did_replacement;
		}
	    }

	  /* Otherwise, try all valid registers and see if its valid.  */
	  for (i = vd->e[regno].oldest_regno; i != regno;
	       i = vd->e[i].next_regno)
	    {
	      new = maybe_mode_change (vd->e[i].mode, vd->e[regno].mode,
				       mode, i, regno);
	      if (new != NULL_RTX)
		{
		  if (validate_change (insn, &SET_SRC (set), new, 0))
		    {
		      ORIGINAL_REGNO (new) = ORIGINAL_REGNO (src);
		      REG_ATTRS (new) = REG_ATTRS (src);
		      if (dump_file)
			fprintf (dump_file,
				 "insn %u: replaced reg %u with %u\n",
				 INSN_UID (insn), regno, REGNO (new));
		      changed = true;
		      goto did_replacement;
		    }
		}
	    }
	}
      no_move_special_case:

      any_replacements = false;

      /* For each input operand, replace a hard register with the
	 eldest live copy that's in an appropriate register class.  */
      for (i = 0; i < n_ops; i++)
	{
	  replaced[i] = false;

	  /* Don't scan match_operand here, since we've no reg class
	     information to pass down.  Any operands that we could
	     substitute in will be represented elsewhere.  */
	  if (recog_data.constraints[i][0] == '\0')
	    continue;

	  /* Don't replace in asms intentionally referencing hard regs.  */
	  if (is_asm && REG_P (recog_data.operand[i])
	      && (REGNO (recog_data.operand[i])
		  == ORIGINAL_REGNO (recog_data.operand[i])))
	    continue;

	  if (recog_data.operand_type[i] == OP_IN)
	    {
	      if (recog_op_alt[i][alt].is_address)
		replaced[i]
		  = replace_oldest_value_addr (recog_data.operand_loc[i],
					       recog_op_alt[i][alt].cl,
					       VOIDmode, insn, vd);
	      else if (REG_P (recog_data.operand[i]))
		replaced[i]
		  = replace_oldest_value_reg (recog_data.operand_loc[i],
					      recog_op_alt[i][alt].cl,
					      insn, vd);
	      else if (MEM_P (recog_data.operand[i]))
		replaced[i] = replace_oldest_value_mem (recog_data.operand[i],
							insn, vd);
	    }
	  else if (MEM_P (recog_data.operand[i]))
	    replaced[i] = replace_oldest_value_mem (recog_data.operand[i],
						    insn, vd);

	  /* If we performed any replacement, update match_dups.  */
	  if (replaced[i])
	    {
	      int j;
	      rtx new;

	      new = *recog_data.operand_loc[i];
	      recog_data.operand[i] = new;
	      for (j = 0; j < recog_data.n_dups; j++)
		if (recog_data.dup_num[j] == i)
		  validate_change (insn, recog_data.dup_loc[j], new, 1);

	      any_replacements = true;
	    }
	}

      if (any_replacements)
	{
	  if (! apply_change_group ())
	    {
	      for (i = 0; i < n_ops; i++)
		if (replaced[i])
		  {
		    rtx old = *recog_data.operand_loc[i];
		    recog_data.operand[i] = old;
		  }

	      if (dump_file)
		fprintf (dump_file,
			 "insn %u: reg replacements not verified\n",
			 INSN_UID (insn));
	    }
	  else
	    changed = true;
	}

    did_replacement:
      /* Clobber call-clobbered registers.  */
      if (CALL_P (insn))
	for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
	  if (TEST_HARD_REG_BIT (regs_invalidated_by_call, i))
	    kill_value_regno (i, 1, vd);

      /* Notice stores.  */
      note_stores (PATTERN (insn), kill_set_value, vd);

      /* Notice copies.  */
      if (set && REG_P (SET_DEST (set)) && REG_P (SET_SRC (set)))
	copy_value (SET_DEST (set), SET_SRC (set), vd);

      if (insn == BB_END (bb))
	break;
    }

  return changed;
}

/* Main entry point for the forward copy propagation optimization.  */

static void
copyprop_hardreg_forward (void)
{
  struct value_data *all_vd;
  bool need_refresh;
  basic_block bb;
  sbitmap visited;

  need_refresh = false;

  all_vd = XNEWVEC (struct value_data, last_basic_block);

  visited = sbitmap_alloc (last_basic_block);
  sbitmap_zero (visited);

  FOR_EACH_BB (bb)
    {
      SET_BIT (visited, bb->index);

      /* If a block has a single predecessor, that we've already
	 processed, begin with the value data that was live at
	 the end of the predecessor block.  */
      /* ??? Ought to use more intelligent queuing of blocks.  */
      if (single_pred_p (bb) 
	  && TEST_BIT (visited, single_pred (bb)->index)
	  && ! (single_pred_edge (bb)->flags & (EDGE_ABNORMAL_CALL | EDGE_EH)))
	all_vd[bb->index] = all_vd[single_pred (bb)->index];
      else
	init_value_data (all_vd + bb->index);

      if (copyprop_hardreg_forward_1 (bb, all_vd + bb->index))
	need_refresh = true;
    }

  sbitmap_free (visited);  

  if (need_refresh)
    {
      if (dump_file)
	fputs ("\n\n", dump_file);

      /* ??? Irritatingly, delete_noop_moves does not take a set of blocks
	 to scan, so we have to do a life update with no initial set of
	 blocks Just In Case.  */
      delete_noop_moves ();
      update_life_info (NULL, UPDATE_LIFE_GLOBAL_RM_NOTES,
			PROP_DEATH_NOTES
			| PROP_SCAN_DEAD_CODE
			| PROP_KILL_DEAD_CODE);
    }

  free (all_vd);
}

/* Dump the value chain data to stderr.  */

void
debug_value_data (struct value_data *vd)
{
  HARD_REG_SET set;
  unsigned int i, j;

  CLEAR_HARD_REG_SET (set);

  for (i = 0; i < FIRST_PSEUDO_REGISTER; ++i)
    if (vd->e[i].oldest_regno == i)
      {
	if (vd->e[i].mode == VOIDmode)
	  {
	    if (vd->e[i].next_regno != INVALID_REGNUM)
	      fprintf (stderr, "[%u] Bad next_regno for empty chain (%u)\n",
		       i, vd->e[i].next_regno);
	    continue;
	  }

	SET_HARD_REG_BIT (set, i);
	fprintf (stderr, "[%u %s] ", i, GET_MODE_NAME (vd->e[i].mode));

	for (j = vd->e[i].next_regno;
	     j != INVALID_REGNUM;
	     j = vd->e[j].next_regno)
	  {
	    if (TEST_HARD_REG_BIT (set, j))
	      {
		fprintf (stderr, "[%u] Loop in regno chain\n", j);
		return;
	      }

	    if (vd->e[j].oldest_regno != i)
	      {
		fprintf (stderr, "[%u] Bad oldest_regno (%u)\n",
			 j, vd->e[j].oldest_regno);
		return;
	      }
	    SET_HARD_REG_BIT (set, j);
	    fprintf (stderr, "[%u %s] ", j, GET_MODE_NAME (vd->e[j].mode));
	  }
	fputc ('\n', stderr);
      }

  for (i = 0; i < FIRST_PSEUDO_REGISTER; ++i)
    if (! TEST_HARD_REG_BIT (set, i)
	&& (vd->e[i].mode != VOIDmode
	    || vd->e[i].oldest_regno != i
	    || vd->e[i].next_regno != INVALID_REGNUM))
      fprintf (stderr, "[%u] Non-empty reg in chain (%s %u %i)\n",
	       i, GET_MODE_NAME (vd->e[i].mode), vd->e[i].oldest_regno,
	       vd->e[i].next_regno);
}

#ifdef ENABLE_CHECKING
static void
validate_value_data (struct value_data *vd)
{
  HARD_REG_SET set;
  unsigned int i, j;

  CLEAR_HARD_REG_SET (set);

  for (i = 0; i < FIRST_PSEUDO_REGISTER; ++i)
    if (vd->e[i].oldest_regno == i)
      {
	if (vd->e[i].mode == VOIDmode)
	  {
	    if (vd->e[i].next_regno != INVALID_REGNUM)
	      internal_error ("validate_value_data: [%u] Bad next_regno for empty chain (%u)",
			      i, vd->e[i].next_regno);
	    continue;
	  }

	SET_HARD_REG_BIT (set, i);

	for (j = vd->e[i].next_regno;
	     j != INVALID_REGNUM;
	     j = vd->e[j].next_regno)
	  {
	    if (TEST_HARD_REG_BIT (set, j))
	      internal_error ("validate_value_data: Loop in regno chain (%u)",
			      j);
	    if (vd->e[j].oldest_regno != i)
	      internal_error ("validate_value_data: [%u] Bad oldest_regno (%u)",
			      j, vd->e[j].oldest_regno);

	    SET_HARD_REG_BIT (set, j);
	  }
      }

  for (i = 0; i < FIRST_PSEUDO_REGISTER; ++i)
    if (! TEST_HARD_REG_BIT (set, i)
	&& (vd->e[i].mode != VOIDmode
	    || vd->e[i].oldest_regno != i
	    || vd->e[i].next_regno != INVALID_REGNUM))
      internal_error ("validate_value_data: [%u] Non-empty reg in chain (%s %u %i)",
		      i, GET_MODE_NAME (vd->e[i].mode), vd->e[i].oldest_regno,
		      vd->e[i].next_regno);
}
#endif

static bool
gate_handle_regrename (void)
{
  return (optimize > 0 && (flag_rename_registers || flag_cprop_registers));
}


/* Run the regrename and cprop passes.  */
static unsigned int
rest_of_handle_regrename (void)
{
  if (flag_rename_registers)
    regrename_optimize ();
  if (flag_cprop_registers)
    copyprop_hardreg_forward ();
  return 0;
}

struct tree_opt_pass pass_regrename =
{
  "rnreg",                              /* name */
  gate_handle_regrename,                /* gate */
  rest_of_handle_regrename,             /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_RENAME_REGISTERS,                  /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func,                       /* todo_flags_finish */
  'n'                                   /* letter */
};

