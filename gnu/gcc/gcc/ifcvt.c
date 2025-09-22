/* If-conversion support.
   Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005, 2006
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
#include "regs.h"
#include "function.h"
#include "flags.h"
#include "insn-config.h"
#include "recog.h"
#include "except.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "expr.h"
#include "real.h"
#include "output.h"
#include "optabs.h"
#include "toplev.h"
#include "tm_p.h"
#include "cfgloop.h"
#include "target.h"
#include "timevar.h"
#include "tree-pass.h"


#ifndef HAVE_conditional_execution
#define HAVE_conditional_execution 0
#endif
#ifndef HAVE_conditional_move
#define HAVE_conditional_move 0
#endif
#ifndef HAVE_incscc
#define HAVE_incscc 0
#endif
#ifndef HAVE_decscc
#define HAVE_decscc 0
#endif
#ifndef HAVE_trap
#define HAVE_trap 0
#endif
#ifndef HAVE_conditional_trap
#define HAVE_conditional_trap 0
#endif

#ifndef MAX_CONDITIONAL_EXECUTE
#define MAX_CONDITIONAL_EXECUTE   (BRANCH_COST + 1)
#endif

#define NULL_BLOCK	((basic_block) NULL)

/* # of IF-THEN or IF-THEN-ELSE blocks we looked at  */
static int num_possible_if_blocks;

/* # of IF-THEN or IF-THEN-ELSE blocks were converted to conditional
   execution.  */
static int num_updated_if_blocks;

/* # of changes made which require life information to be updated.  */
static int num_true_changes;

/* Whether conditional execution changes were made.  */
static int cond_exec_changed_p;

/* True if life data ok at present.  */
static bool life_data_ok;

/* Forward references.  */
static int count_bb_insns (basic_block);
static bool cheap_bb_rtx_cost_p (basic_block, int);
static rtx first_active_insn (basic_block);
static rtx last_active_insn (basic_block, int);
static basic_block block_fallthru (basic_block);
static int cond_exec_process_insns (ce_if_block_t *, rtx, rtx, rtx, rtx, int);
static rtx cond_exec_get_condition (rtx);
static int cond_exec_process_if_block (ce_if_block_t *, int);
static rtx noce_get_condition (rtx, rtx *);
static int noce_operand_ok (rtx);
static int noce_process_if_block (ce_if_block_t *);
static int process_if_block (ce_if_block_t *);
static void merge_if_block (ce_if_block_t *);
static int find_cond_trap (basic_block, edge, edge);
static basic_block find_if_header (basic_block, int);
static int block_jumps_and_fallthru_p (basic_block, basic_block);
static int find_if_block (ce_if_block_t *);
static int find_if_case_1 (basic_block, edge, edge);
static int find_if_case_2 (basic_block, edge, edge);
static int find_memory (rtx *, void *);
static int dead_or_predicable (basic_block, basic_block, basic_block,
			       basic_block, int);
static void noce_emit_move_insn (rtx, rtx);
static rtx block_has_only_trap (basic_block);

/* Count the number of non-jump active insns in BB.  */

static int
count_bb_insns (basic_block bb)
{
  int count = 0;
  rtx insn = BB_HEAD (bb);

  while (1)
    {
      if (CALL_P (insn) || NONJUMP_INSN_P (insn))
	count++;

      if (insn == BB_END (bb))
	break;
      insn = NEXT_INSN (insn);
    }

  return count;
}

/* Determine whether the total insn_rtx_cost on non-jump insns in
   basic block BB is less than MAX_COST.  This function returns
   false if the cost of any instruction could not be estimated.  */

static bool
cheap_bb_rtx_cost_p (basic_block bb, int max_cost)
{
  int count = 0;
  rtx insn = BB_HEAD (bb);

  while (1)
    {
      if (NONJUMP_INSN_P (insn))
	{
	  int cost = insn_rtx_cost (PATTERN (insn));
	  if (cost == 0)
	    return false;

	  /* If this instruction is the load or set of a "stack" register,
	     such as a floating point register on x87, then the cost of
	     speculatively executing this insn may need to include
	     the additional cost of popping its result off of the
	     register stack.  Unfortunately, correctly recognizing and
	     accounting for this additional overhead is tricky, so for
	     now we simply prohibit such speculative execution.  */
#ifdef STACK_REGS
	  {
	    rtx set = single_set (insn);
	    if (set && STACK_REG_P (SET_DEST (set)))
	      return false;
	  }
#endif

	  count += cost;
	  if (count >= max_cost)
	    return false;
	}
      else if (CALL_P (insn))
	return false;
 
      if (insn == BB_END (bb))
	break;
      insn = NEXT_INSN (insn);
    }

  return true;
}

/* Return the first non-jump active insn in the basic block.  */

static rtx
first_active_insn (basic_block bb)
{
  rtx insn = BB_HEAD (bb);

  if (LABEL_P (insn))
    {
      if (insn == BB_END (bb))
	return NULL_RTX;
      insn = NEXT_INSN (insn);
    }

  while (NOTE_P (insn))
    {
      if (insn == BB_END (bb))
	return NULL_RTX;
      insn = NEXT_INSN (insn);
    }

  if (JUMP_P (insn))
    return NULL_RTX;

  return insn;
}

/* Return the last non-jump active (non-jump) insn in the basic block.  */

static rtx
last_active_insn (basic_block bb, int skip_use_p)
{
  rtx insn = BB_END (bb);
  rtx head = BB_HEAD (bb);

  while (NOTE_P (insn)
	 || JUMP_P (insn)
	 || (skip_use_p
	     && NONJUMP_INSN_P (insn)
	     && GET_CODE (PATTERN (insn)) == USE))
    {
      if (insn == head)
	return NULL_RTX;
      insn = PREV_INSN (insn);
    }

  if (LABEL_P (insn))
    return NULL_RTX;

  return insn;
}

/* Return the basic block reached by falling though the basic block BB.  */

static basic_block
block_fallthru (basic_block bb)
{
  edge e;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, bb->succs)
    if (e->flags & EDGE_FALLTHRU)
      break;

  return (e) ? e->dest : NULL_BLOCK;
}

/* Go through a bunch of insns, converting them to conditional
   execution format if possible.  Return TRUE if all of the non-note
   insns were processed.  */

static int
cond_exec_process_insns (ce_if_block_t *ce_info ATTRIBUTE_UNUSED,
			 /* if block information */rtx start,
			 /* first insn to look at */rtx end,
			 /* last insn to look at */rtx test,
			 /* conditional execution test */rtx prob_val,
			 /* probability of branch taken. */int mod_ok)
{
  int must_be_last = FALSE;
  rtx insn;
  rtx xtest;
  rtx pattern;

  if (!start || !end)
    return FALSE;

  for (insn = start; ; insn = NEXT_INSN (insn))
    {
      if (NOTE_P (insn))
	goto insn_done;

      gcc_assert(NONJUMP_INSN_P (insn) || CALL_P (insn));

      /* Remove USE insns that get in the way.  */
      if (reload_completed && GET_CODE (PATTERN (insn)) == USE)
	{
	  /* ??? Ug.  Actually unlinking the thing is problematic,
	     given what we'd have to coordinate with our callers.  */
	  SET_INSN_DELETED (insn);
	  goto insn_done;
	}

      /* Last insn wasn't last?  */
      if (must_be_last)
	return FALSE;

      if (modified_in_p (test, insn))
	{
	  if (!mod_ok)
	    return FALSE;
	  must_be_last = TRUE;
	}

      /* Now build the conditional form of the instruction.  */
      pattern = PATTERN (insn);
      xtest = copy_rtx (test);

      /* If this is already a COND_EXEC, rewrite the test to be an AND of the
         two conditions.  */
      if (GET_CODE (pattern) == COND_EXEC)
	{
	  if (GET_MODE (xtest) != GET_MODE (COND_EXEC_TEST (pattern)))
	    return FALSE;

	  xtest = gen_rtx_AND (GET_MODE (xtest), xtest,
			       COND_EXEC_TEST (pattern));
	  pattern = COND_EXEC_CODE (pattern);
	}

      pattern = gen_rtx_COND_EXEC (VOIDmode, xtest, pattern);

      /* If the machine needs to modify the insn being conditionally executed,
         say for example to force a constant integer operand into a temp
         register, do so here.  */
#ifdef IFCVT_MODIFY_INSN
      IFCVT_MODIFY_INSN (ce_info, pattern, insn);
      if (! pattern)
	return FALSE;
#endif

      validate_change (insn, &PATTERN (insn), pattern, 1);

      if (CALL_P (insn) && prob_val)
	validate_change (insn, &REG_NOTES (insn),
			 alloc_EXPR_LIST (REG_BR_PROB, prob_val,
					  REG_NOTES (insn)), 1);

    insn_done:
      if (insn == end)
	break;
    }

  return TRUE;
}

/* Return the condition for a jump.  Do not do any special processing.  */

static rtx
cond_exec_get_condition (rtx jump)
{
  rtx test_if, cond;

  if (any_condjump_p (jump))
    test_if = SET_SRC (pc_set (jump));
  else
    return NULL_RTX;
  cond = XEXP (test_if, 0);

  /* If this branches to JUMP_LABEL when the condition is false,
     reverse the condition.  */
  if (GET_CODE (XEXP (test_if, 2)) == LABEL_REF
      && XEXP (XEXP (test_if, 2), 0) == JUMP_LABEL (jump))
    {
      enum rtx_code rev = reversed_comparison_code (cond, jump);
      if (rev == UNKNOWN)
	return NULL_RTX;

      cond = gen_rtx_fmt_ee (rev, GET_MODE (cond), XEXP (cond, 0),
			     XEXP (cond, 1));
    }

  return cond;
}

/* Given a simple IF-THEN or IF-THEN-ELSE block, attempt to convert it
   to conditional execution.  Return TRUE if we were successful at
   converting the block.  */

static int
cond_exec_process_if_block (ce_if_block_t * ce_info,
			    /* if block information */int do_multiple_p)
{
  basic_block test_bb = ce_info->test_bb;	/* last test block */
  basic_block then_bb = ce_info->then_bb;	/* THEN */
  basic_block else_bb = ce_info->else_bb;	/* ELSE or NULL */
  rtx test_expr;		/* expression in IF_THEN_ELSE that is tested */
  rtx then_start;		/* first insn in THEN block */
  rtx then_end;			/* last insn + 1 in THEN block */
  rtx else_start = NULL_RTX;	/* first insn in ELSE block or NULL */
  rtx else_end = NULL_RTX;	/* last insn + 1 in ELSE block */
  int max;			/* max # of insns to convert.  */
  int then_mod_ok;		/* whether conditional mods are ok in THEN */
  rtx true_expr;		/* test for else block insns */
  rtx false_expr;		/* test for then block insns */
  rtx true_prob_val;		/* probability of else block */
  rtx false_prob_val;		/* probability of then block */
  int n_insns;
  enum rtx_code false_code;

  /* If test is comprised of && or || elements, and we've failed at handling
     all of them together, just use the last test if it is the special case of
     && elements without an ELSE block.  */
  if (!do_multiple_p && ce_info->num_multiple_test_blocks)
    {
      if (else_bb || ! ce_info->and_and_p)
	return FALSE;

      ce_info->test_bb = test_bb = ce_info->last_test_bb;
      ce_info->num_multiple_test_blocks = 0;
      ce_info->num_and_and_blocks = 0;
      ce_info->num_or_or_blocks = 0;
    }

  /* Find the conditional jump to the ELSE or JOIN part, and isolate
     the test.  */
  test_expr = cond_exec_get_condition (BB_END (test_bb));
  if (! test_expr)
    return FALSE;

  /* If the conditional jump is more than just a conditional jump,
     then we can not do conditional execution conversion on this block.  */
  if (! onlyjump_p (BB_END (test_bb)))
    return FALSE;

  /* Collect the bounds of where we're to search, skipping any labels, jumps
     and notes at the beginning and end of the block.  Then count the total
     number of insns and see if it is small enough to convert.  */
  then_start = first_active_insn (then_bb);
  then_end = last_active_insn (then_bb, TRUE);
  n_insns = ce_info->num_then_insns = count_bb_insns (then_bb);
  max = MAX_CONDITIONAL_EXECUTE;

  if (else_bb)
    {
      max *= 2;
      else_start = first_active_insn (else_bb);
      else_end = last_active_insn (else_bb, TRUE);
      n_insns += ce_info->num_else_insns = count_bb_insns (else_bb);
    }

  if (n_insns > max)
    return FALSE;

  /* Map test_expr/test_jump into the appropriate MD tests to use on
     the conditionally executed code.  */

  true_expr = test_expr;

  false_code = reversed_comparison_code (true_expr, BB_END (test_bb));
  if (false_code != UNKNOWN)
    false_expr = gen_rtx_fmt_ee (false_code, GET_MODE (true_expr),
				 XEXP (true_expr, 0), XEXP (true_expr, 1));
  else
    false_expr = NULL_RTX;

#ifdef IFCVT_MODIFY_TESTS
  /* If the machine description needs to modify the tests, such as setting a
     conditional execution register from a comparison, it can do so here.  */
  IFCVT_MODIFY_TESTS (ce_info, true_expr, false_expr);

  /* See if the conversion failed.  */
  if (!true_expr || !false_expr)
    goto fail;
#endif

  true_prob_val = find_reg_note (BB_END (test_bb), REG_BR_PROB, NULL_RTX);
  if (true_prob_val)
    {
      true_prob_val = XEXP (true_prob_val, 0);
      false_prob_val = GEN_INT (REG_BR_PROB_BASE - INTVAL (true_prob_val));
    }
  else
    false_prob_val = NULL_RTX;

  /* If we have && or || tests, do them here.  These tests are in the adjacent
     blocks after the first block containing the test.  */
  if (ce_info->num_multiple_test_blocks > 0)
    {
      basic_block bb = test_bb;
      basic_block last_test_bb = ce_info->last_test_bb;

      if (! false_expr)
	goto fail;

      do
	{
	  rtx start, end;
	  rtx t, f;
	  enum rtx_code f_code;

	  bb = block_fallthru (bb);
	  start = first_active_insn (bb);
	  end = last_active_insn (bb, TRUE);
	  if (start
	      && ! cond_exec_process_insns (ce_info, start, end, false_expr,
					    false_prob_val, FALSE))
	    goto fail;

	  /* If the conditional jump is more than just a conditional jump, then
	     we can not do conditional execution conversion on this block.  */
	  if (! onlyjump_p (BB_END (bb)))
	    goto fail;

	  /* Find the conditional jump and isolate the test.  */
	  t = cond_exec_get_condition (BB_END (bb));
	  if (! t)
	    goto fail;

	  f_code = reversed_comparison_code (t, BB_END (bb));
	  if (f_code == UNKNOWN)
	    goto fail;

	  f = gen_rtx_fmt_ee (f_code, GET_MODE (t), XEXP (t, 0), XEXP (t, 1));
	  if (ce_info->and_and_p)
	    {
	      t = gen_rtx_AND (GET_MODE (t), true_expr, t);
	      f = gen_rtx_IOR (GET_MODE (t), false_expr, f);
	    }
	  else
	    {
	      t = gen_rtx_IOR (GET_MODE (t), true_expr, t);
	      f = gen_rtx_AND (GET_MODE (t), false_expr, f);
	    }

	  /* If the machine description needs to modify the tests, such as
	     setting a conditional execution register from a comparison, it can
	     do so here.  */
#ifdef IFCVT_MODIFY_MULTIPLE_TESTS
	  IFCVT_MODIFY_MULTIPLE_TESTS (ce_info, bb, t, f);

	  /* See if the conversion failed.  */
	  if (!t || !f)
	    goto fail;
#endif

	  true_expr = t;
	  false_expr = f;
	}
      while (bb != last_test_bb);
    }

  /* For IF-THEN-ELSE blocks, we don't allow modifications of the test
     on then THEN block.  */
  then_mod_ok = (else_bb == NULL_BLOCK);

  /* Go through the THEN and ELSE blocks converting the insns if possible
     to conditional execution.  */

  if (then_end
      && (! false_expr
	  || ! cond_exec_process_insns (ce_info, then_start, then_end,
					false_expr, false_prob_val,
					then_mod_ok)))
    goto fail;

  if (else_bb && else_end
      && ! cond_exec_process_insns (ce_info, else_start, else_end,
				    true_expr, true_prob_val, TRUE))
    goto fail;

  /* If we cannot apply the changes, fail.  Do not go through the normal fail
     processing, since apply_change_group will call cancel_changes.  */
  if (! apply_change_group ())
    {
#ifdef IFCVT_MODIFY_CANCEL
      /* Cancel any machine dependent changes.  */
      IFCVT_MODIFY_CANCEL (ce_info);
#endif
      return FALSE;
    }

#ifdef IFCVT_MODIFY_FINAL
  /* Do any machine dependent final modifications.  */
  IFCVT_MODIFY_FINAL (ce_info);
#endif

  /* Conversion succeeded.  */
  if (dump_file)
    fprintf (dump_file, "%d insn%s converted to conditional execution.\n",
	     n_insns, (n_insns == 1) ? " was" : "s were");

  /* Merge the blocks!  */
  merge_if_block (ce_info);
  cond_exec_changed_p = TRUE;
  return TRUE;

 fail:
#ifdef IFCVT_MODIFY_CANCEL
  /* Cancel any machine dependent changes.  */
  IFCVT_MODIFY_CANCEL (ce_info);
#endif

  cancel_changes (0);
  return FALSE;
}

/* Used by noce_process_if_block to communicate with its subroutines.

   The subroutines know that A and B may be evaluated freely.  They
   know that X is a register.  They should insert new instructions
   before cond_earliest.  */

struct noce_if_info
{
  basic_block test_bb;
  rtx insn_a, insn_b;
  rtx x, a, b;
  rtx jump, cond, cond_earliest;
  /* True if "b" was originally evaluated unconditionally.  */
  bool b_unconditional;
};

static rtx noce_emit_store_flag (struct noce_if_info *, rtx, int, int);
static int noce_try_move (struct noce_if_info *);
static int noce_try_store_flag (struct noce_if_info *);
static int noce_try_addcc (struct noce_if_info *);
static int noce_try_store_flag_constants (struct noce_if_info *);
static int noce_try_store_flag_mask (struct noce_if_info *);
static rtx noce_emit_cmove (struct noce_if_info *, rtx, enum rtx_code, rtx,
			    rtx, rtx, rtx);
static int noce_try_cmove (struct noce_if_info *);
static int noce_try_cmove_arith (struct noce_if_info *);
static rtx noce_get_alt_condition (struct noce_if_info *, rtx, rtx *);
static int noce_try_minmax (struct noce_if_info *);
static int noce_try_abs (struct noce_if_info *);
static int noce_try_sign_mask (struct noce_if_info *);

/* Helper function for noce_try_store_flag*.  */

static rtx
noce_emit_store_flag (struct noce_if_info *if_info, rtx x, int reversep,
		      int normalize)
{
  rtx cond = if_info->cond;
  int cond_complex;
  enum rtx_code code;

  cond_complex = (! general_operand (XEXP (cond, 0), VOIDmode)
		  || ! general_operand (XEXP (cond, 1), VOIDmode));

  /* If earliest == jump, or when the condition is complex, try to
     build the store_flag insn directly.  */

  if (cond_complex)
    cond = XEXP (SET_SRC (pc_set (if_info->jump)), 0);

  if (reversep)
    code = reversed_comparison_code (cond, if_info->jump);
  else
    code = GET_CODE (cond);

  if ((if_info->cond_earliest == if_info->jump || cond_complex)
      && (normalize == 0 || STORE_FLAG_VALUE == normalize))
    {
      rtx tmp;

      tmp = gen_rtx_fmt_ee (code, GET_MODE (x), XEXP (cond, 0),
			    XEXP (cond, 1));
      tmp = gen_rtx_SET (VOIDmode, x, tmp);

      start_sequence ();
      tmp = emit_insn (tmp);

      if (recog_memoized (tmp) >= 0)
	{
	  tmp = get_insns ();
	  end_sequence ();
	  emit_insn (tmp);

	  if_info->cond_earliest = if_info->jump;

	  return x;
	}

      end_sequence ();
    }

  /* Don't even try if the comparison operands or the mode of X are weird.  */
  if (cond_complex || !SCALAR_INT_MODE_P (GET_MODE (x)))
    return NULL_RTX;

  return emit_store_flag (x, code, XEXP (cond, 0),
			  XEXP (cond, 1), VOIDmode,
			  (code == LTU || code == LEU
			   || code == GEU || code == GTU), normalize);
}

/* Emit instruction to move an rtx, possibly into STRICT_LOW_PART.
   X is the destination/target and Y is the value to copy.  */

static void
noce_emit_move_insn (rtx x, rtx y)
{
  enum machine_mode outmode;
  rtx outer, inner;
  int bitpos;

  if (GET_CODE (x) != STRICT_LOW_PART)
    {
      rtx seq, insn, target;
      optab ot;

      start_sequence ();
      /* Check that the SET_SRC is reasonable before calling emit_move_insn,
	 otherwise construct a suitable SET pattern ourselves.  */
      insn = (OBJECT_P (y) || CONSTANT_P (y) || GET_CODE (y) == SUBREG)
	     ? emit_move_insn (x, y)
	     : emit_insn (gen_rtx_SET (VOIDmode, x, y));
      seq = get_insns ();
      end_sequence();

      if (recog_memoized (insn) <= 0)
	{
	  if (GET_CODE (x) == ZERO_EXTRACT)
	    {
	      rtx op = XEXP (x, 0);
	      unsigned HOST_WIDE_INT size = INTVAL (XEXP (x, 1));
	      unsigned HOST_WIDE_INT start = INTVAL (XEXP (x, 2));

	      /* store_bit_field expects START to be relative to 
		 BYTES_BIG_ENDIAN and adjusts this value for machines with 
		 BITS_BIG_ENDIAN != BYTES_BIG_ENDIAN.  In order to be able to 
		 invoke store_bit_field again it is necessary to have the START
		 value from the first call.  */
	      if (BITS_BIG_ENDIAN != BYTES_BIG_ENDIAN)
		{
		  if (MEM_P (op))
		    start = BITS_PER_UNIT - start - size;
		  else
		    {
		      gcc_assert (REG_P (op));
		      start = BITS_PER_WORD - start - size;
		    }
		}

	      gcc_assert (start < (MEM_P (op) ? BITS_PER_UNIT : BITS_PER_WORD));
	      store_bit_field (op, size, start, GET_MODE (x), y);
	      return;
	    }

	  switch (GET_RTX_CLASS (GET_CODE (y)))
	    {
	    case RTX_UNARY:
	      ot = code_to_optab[GET_CODE (y)];
	      if (ot)
		{
		  start_sequence ();
		  target = expand_unop (GET_MODE (y), ot, XEXP (y, 0), x, 0);
		  if (target != NULL_RTX)
		    {
		      if (target != x)
			emit_move_insn (x, target);
		      seq = get_insns ();
		    }
		  end_sequence ();
		}
	      break;
	      
	    case RTX_BIN_ARITH:
	    case RTX_COMM_ARITH:
	      ot = code_to_optab[GET_CODE (y)];
	      if (ot)
		{
		  start_sequence ();
		  target = expand_binop (GET_MODE (y), ot,
					 XEXP (y, 0), XEXP (y, 1),
					 x, 0, OPTAB_DIRECT);
		  if (target != NULL_RTX)
		    {
		      if (target != x)
			  emit_move_insn (x, target);
		      seq = get_insns ();
		    }
		  end_sequence ();
		}
	      break;
	      
	    default:
	      break;
	    }
	}
      
      emit_insn (seq);
      return;
    }

  outer = XEXP (x, 0);
  inner = XEXP (outer, 0);
  outmode = GET_MODE (outer);
  bitpos = SUBREG_BYTE (outer) * BITS_PER_UNIT;
  store_bit_field (inner, GET_MODE_BITSIZE (outmode), bitpos, outmode, y);
}

/* Return sequence of instructions generated by if conversion.  This
   function calls end_sequence() to end the current stream, ensures
   that are instructions are unshared, recognizable non-jump insns.
   On failure, this function returns a NULL_RTX.  */

static rtx
end_ifcvt_sequence (struct noce_if_info *if_info)
{
  rtx insn;
  rtx seq = get_insns ();

  set_used_flags (if_info->x);
  set_used_flags (if_info->cond);
  unshare_all_rtl_in_chain (seq);
  end_sequence ();

  /* Make sure that all of the instructions emitted are recognizable,
     and that we haven't introduced a new jump instruction.
     As an exercise for the reader, build a general mechanism that
     allows proper placement of required clobbers.  */
  for (insn = seq; insn; insn = NEXT_INSN (insn))
    if (JUMP_P (insn)
	|| recog_memoized (insn) == -1)
      return NULL_RTX;

  return seq;
}

/* Convert "if (a != b) x = a; else x = b" into "x = a" and
   "if (a == b) x = a; else x = b" into "x = b".  */

static int
noce_try_move (struct noce_if_info *if_info)
{
  rtx cond = if_info->cond;
  enum rtx_code code = GET_CODE (cond);
  rtx y, seq;

  if (code != NE && code != EQ)
    return FALSE;

  /* This optimization isn't valid if either A or B could be a NaN
     or a signed zero.  */
  if (HONOR_NANS (GET_MODE (if_info->x))
      || HONOR_SIGNED_ZEROS (GET_MODE (if_info->x)))
    return FALSE;

  /* Check whether the operands of the comparison are A and in
     either order.  */
  if ((rtx_equal_p (if_info->a, XEXP (cond, 0))
       && rtx_equal_p (if_info->b, XEXP (cond, 1)))
      || (rtx_equal_p (if_info->a, XEXP (cond, 1))
	  && rtx_equal_p (if_info->b, XEXP (cond, 0))))
    {
      y = (code == EQ) ? if_info->a : if_info->b;

      /* Avoid generating the move if the source is the destination.  */
      if (! rtx_equal_p (if_info->x, y))
	{
	  start_sequence ();
	  noce_emit_move_insn (if_info->x, y);
	  seq = end_ifcvt_sequence (if_info);
	  if (!seq)
	    return FALSE;

	  emit_insn_before_setloc (seq, if_info->jump,
				   INSN_LOCATOR (if_info->insn_a));
	}
      return TRUE;
    }
  return FALSE;
}

/* Convert "if (test) x = 1; else x = 0".

   Only try 0 and STORE_FLAG_VALUE here.  Other combinations will be
   tried in noce_try_store_flag_constants after noce_try_cmove has had
   a go at the conversion.  */

static int
noce_try_store_flag (struct noce_if_info *if_info)
{
  int reversep;
  rtx target, seq;

  if (GET_CODE (if_info->b) == CONST_INT
      && INTVAL (if_info->b) == STORE_FLAG_VALUE
      && if_info->a == const0_rtx)
    reversep = 0;
  else if (if_info->b == const0_rtx
	   && GET_CODE (if_info->a) == CONST_INT
	   && INTVAL (if_info->a) == STORE_FLAG_VALUE
	   && (reversed_comparison_code (if_info->cond, if_info->jump)
	       != UNKNOWN))
    reversep = 1;
  else
    return FALSE;

  start_sequence ();

  target = noce_emit_store_flag (if_info, if_info->x, reversep, 0);
  if (target)
    {
      if (target != if_info->x)
	noce_emit_move_insn (if_info->x, target);

      seq = end_ifcvt_sequence (if_info);
      if (! seq)
	return FALSE;

      emit_insn_before_setloc (seq, if_info->jump,
			       INSN_LOCATOR (if_info->insn_a));
      return TRUE;
    }
  else
    {
      end_sequence ();
      return FALSE;
    }
}

/* Convert "if (test) x = a; else x = b", for A and B constant.  */

static int
noce_try_store_flag_constants (struct noce_if_info *if_info)
{
  rtx target, seq;
  int reversep;
  HOST_WIDE_INT itrue, ifalse, diff, tmp;
  int normalize, can_reverse;
  enum machine_mode mode;

  if (! no_new_pseudos
      && GET_CODE (if_info->a) == CONST_INT
      && GET_CODE (if_info->b) == CONST_INT)
    {
      mode = GET_MODE (if_info->x);
      ifalse = INTVAL (if_info->a);
      itrue = INTVAL (if_info->b);

      /* Make sure we can represent the difference between the two values.  */
      if ((itrue - ifalse > 0)
	  != ((ifalse < 0) != (itrue < 0) ? ifalse < 0 : ifalse < itrue))
	return FALSE;

      diff = trunc_int_for_mode (itrue - ifalse, mode);

      can_reverse = (reversed_comparison_code (if_info->cond, if_info->jump)
		     != UNKNOWN);

      reversep = 0;
      if (diff == STORE_FLAG_VALUE || diff == -STORE_FLAG_VALUE)
	normalize = 0;
      else if (ifalse == 0 && exact_log2 (itrue) >= 0
	       && (STORE_FLAG_VALUE == 1
		   || BRANCH_COST >= 2))
	normalize = 1;
      else if (itrue == 0 && exact_log2 (ifalse) >= 0 && can_reverse
	       && (STORE_FLAG_VALUE == 1 || BRANCH_COST >= 2))
	normalize = 1, reversep = 1;
      else if (itrue == -1
	       && (STORE_FLAG_VALUE == -1
		   || BRANCH_COST >= 2))
	normalize = -1;
      else if (ifalse == -1 && can_reverse
	       && (STORE_FLAG_VALUE == -1 || BRANCH_COST >= 2))
	normalize = -1, reversep = 1;
      else if ((BRANCH_COST >= 2 && STORE_FLAG_VALUE == -1)
	       || BRANCH_COST >= 3)
	normalize = -1;
      else
	return FALSE;

      if (reversep)
	{
	  tmp = itrue; itrue = ifalse; ifalse = tmp;
	  diff = trunc_int_for_mode (-diff, mode);
	}

      start_sequence ();
      target = noce_emit_store_flag (if_info, if_info->x, reversep, normalize);
      if (! target)
	{
	  end_sequence ();
	  return FALSE;
	}

      /* if (test) x = 3; else x = 4;
	 =>   x = 3 + (test == 0);  */
      if (diff == STORE_FLAG_VALUE || diff == -STORE_FLAG_VALUE)
	{
	  target = expand_simple_binop (mode,
					(diff == STORE_FLAG_VALUE
					 ? PLUS : MINUS),
					GEN_INT (ifalse), target, if_info->x, 0,
					OPTAB_WIDEN);
	}

      /* if (test) x = 8; else x = 0;
	 =>   x = (test != 0) << 3;  */
      else if (ifalse == 0 && (tmp = exact_log2 (itrue)) >= 0)
	{
	  target = expand_simple_binop (mode, ASHIFT,
					target, GEN_INT (tmp), if_info->x, 0,
					OPTAB_WIDEN);
	}

      /* if (test) x = -1; else x = b;
	 =>   x = -(test != 0) | b;  */
      else if (itrue == -1)
	{
	  target = expand_simple_binop (mode, IOR,
					target, GEN_INT (ifalse), if_info->x, 0,
					OPTAB_WIDEN);
	}

      /* if (test) x = a; else x = b;
	 =>   x = (-(test != 0) & (b - a)) + a;  */
      else
	{
	  target = expand_simple_binop (mode, AND,
					target, GEN_INT (diff), if_info->x, 0,
					OPTAB_WIDEN);
	  if (target)
	    target = expand_simple_binop (mode, PLUS,
					  target, GEN_INT (ifalse),
					  if_info->x, 0, OPTAB_WIDEN);
	}

      if (! target)
	{
	  end_sequence ();
	  return FALSE;
	}

      if (target != if_info->x)
	noce_emit_move_insn (if_info->x, target);

      seq = end_ifcvt_sequence (if_info);
      if (!seq)
	return FALSE;

      emit_insn_before_setloc (seq, if_info->jump,
			       INSN_LOCATOR (if_info->insn_a));
      return TRUE;
    }

  return FALSE;
}

/* Convert "if (test) foo++" into "foo += (test != 0)", and
   similarly for "foo--".  */

static int
noce_try_addcc (struct noce_if_info *if_info)
{
  rtx target, seq;
  int subtract, normalize;

  if (! no_new_pseudos
      && GET_CODE (if_info->a) == PLUS
      && rtx_equal_p (XEXP (if_info->a, 0), if_info->b)
      && (reversed_comparison_code (if_info->cond, if_info->jump)
	  != UNKNOWN))
    {
      rtx cond = if_info->cond;
      enum rtx_code code = reversed_comparison_code (cond, if_info->jump);

      /* First try to use addcc pattern.  */
      if (general_operand (XEXP (cond, 0), VOIDmode)
	  && general_operand (XEXP (cond, 1), VOIDmode))
	{
	  start_sequence ();
	  target = emit_conditional_add (if_info->x, code,
					 XEXP (cond, 0),
					 XEXP (cond, 1),
					 VOIDmode,
					 if_info->b,
					 XEXP (if_info->a, 1),
					 GET_MODE (if_info->x),
					 (code == LTU || code == GEU
					  || code == LEU || code == GTU));
	  if (target)
	    {
	      if (target != if_info->x)
		noce_emit_move_insn (if_info->x, target);

	      seq = end_ifcvt_sequence (if_info);
	      if (!seq)
		return FALSE;

	      emit_insn_before_setloc (seq, if_info->jump,
				       INSN_LOCATOR (if_info->insn_a));
	      return TRUE;
	    }
	  end_sequence ();
	}

      /* If that fails, construct conditional increment or decrement using
	 setcc.  */
      if (BRANCH_COST >= 2
	  && (XEXP (if_info->a, 1) == const1_rtx
	      || XEXP (if_info->a, 1) == constm1_rtx))
        {
	  start_sequence ();
	  if (STORE_FLAG_VALUE == INTVAL (XEXP (if_info->a, 1)))
	    subtract = 0, normalize = 0;
	  else if (-STORE_FLAG_VALUE == INTVAL (XEXP (if_info->a, 1)))
	    subtract = 1, normalize = 0;
	  else
	    subtract = 0, normalize = INTVAL (XEXP (if_info->a, 1));


	  target = noce_emit_store_flag (if_info,
					 gen_reg_rtx (GET_MODE (if_info->x)),
					 1, normalize);

	  if (target)
	    target = expand_simple_binop (GET_MODE (if_info->x),
					  subtract ? MINUS : PLUS,
					  if_info->b, target, if_info->x,
					  0, OPTAB_WIDEN);
	  if (target)
	    {
	      if (target != if_info->x)
		noce_emit_move_insn (if_info->x, target);

	      seq = end_ifcvt_sequence (if_info);
	      if (!seq)
		return FALSE;

	      emit_insn_before_setloc (seq, if_info->jump,
				       INSN_LOCATOR (if_info->insn_a));
	      return TRUE;
	    }
	  end_sequence ();
	}
    }

  return FALSE;
}

/* Convert "if (test) x = 0;" to "x &= -(test == 0);"  */

static int
noce_try_store_flag_mask (struct noce_if_info *if_info)
{
  rtx target, seq;
  int reversep;

  reversep = 0;
  if (! no_new_pseudos
      && (BRANCH_COST >= 2
	  || STORE_FLAG_VALUE == -1)
      && ((if_info->a == const0_rtx
	   && rtx_equal_p (if_info->b, if_info->x))
	  || ((reversep = (reversed_comparison_code (if_info->cond,
						     if_info->jump)
			   != UNKNOWN))
	      && if_info->b == const0_rtx
	      && rtx_equal_p (if_info->a, if_info->x))))
    {
      start_sequence ();
      target = noce_emit_store_flag (if_info,
				     gen_reg_rtx (GET_MODE (if_info->x)),
				     reversep, -1);
      if (target)
        target = expand_simple_binop (GET_MODE (if_info->x), AND,
				      if_info->x,
				      target, if_info->x, 0,
				      OPTAB_WIDEN);

      if (target)
	{
	  if (target != if_info->x)
	    noce_emit_move_insn (if_info->x, target);

	  seq = end_ifcvt_sequence (if_info);
	  if (!seq)
	    return FALSE;

	  emit_insn_before_setloc (seq, if_info->jump,
				   INSN_LOCATOR (if_info->insn_a));
	  return TRUE;
	}

      end_sequence ();
    }

  return FALSE;
}

/* Helper function for noce_try_cmove and noce_try_cmove_arith.  */

static rtx
noce_emit_cmove (struct noce_if_info *if_info, rtx x, enum rtx_code code,
		 rtx cmp_a, rtx cmp_b, rtx vfalse, rtx vtrue)
{
  /* If earliest == jump, try to build the cmove insn directly.
     This is helpful when combine has created some complex condition
     (like for alpha's cmovlbs) that we can't hope to regenerate
     through the normal interface.  */

  if (if_info->cond_earliest == if_info->jump)
    {
      rtx tmp;

      tmp = gen_rtx_fmt_ee (code, GET_MODE (if_info->cond), cmp_a, cmp_b);
      tmp = gen_rtx_IF_THEN_ELSE (GET_MODE (x), tmp, vtrue, vfalse);
      tmp = gen_rtx_SET (VOIDmode, x, tmp);

      start_sequence ();
      tmp = emit_insn (tmp);

      if (recog_memoized (tmp) >= 0)
	{
	  tmp = get_insns ();
	  end_sequence ();
	  emit_insn (tmp);

	  return x;
	}

      end_sequence ();
    }

  /* Don't even try if the comparison operands are weird.  */
  if (! general_operand (cmp_a, GET_MODE (cmp_a))
      || ! general_operand (cmp_b, GET_MODE (cmp_b)))
    return NULL_RTX;

#if HAVE_conditional_move
  return emit_conditional_move (x, code, cmp_a, cmp_b, VOIDmode,
				vtrue, vfalse, GET_MODE (x),
			        (code == LTU || code == GEU
				 || code == LEU || code == GTU));
#else
  /* We'll never get here, as noce_process_if_block doesn't call the
     functions involved.  Ifdef code, however, should be discouraged
     because it leads to typos in the code not selected.  However,
     emit_conditional_move won't exist either.  */
  return NULL_RTX;
#endif
}

/* Try only simple constants and registers here.  More complex cases
   are handled in noce_try_cmove_arith after noce_try_store_flag_arith
   has had a go at it.  */

static int
noce_try_cmove (struct noce_if_info *if_info)
{
  enum rtx_code code;
  rtx target, seq;

  if ((CONSTANT_P (if_info->a) || register_operand (if_info->a, VOIDmode))
      && (CONSTANT_P (if_info->b) || register_operand (if_info->b, VOIDmode)))
    {
      start_sequence ();

      code = GET_CODE (if_info->cond);
      target = noce_emit_cmove (if_info, if_info->x, code,
				XEXP (if_info->cond, 0),
				XEXP (if_info->cond, 1),
				if_info->a, if_info->b);

      if (target)
	{
	  if (target != if_info->x)
	    noce_emit_move_insn (if_info->x, target);

	  seq = end_ifcvt_sequence (if_info);
	  if (!seq)
	    return FALSE;

	  emit_insn_before_setloc (seq, if_info->jump,
				   INSN_LOCATOR (if_info->insn_a));
	  return TRUE;
	}
      else
	{
	  end_sequence ();
	  return FALSE;
	}
    }

  return FALSE;
}

/* Try more complex cases involving conditional_move.  */

static int
noce_try_cmove_arith (struct noce_if_info *if_info)
{
  rtx a = if_info->a;
  rtx b = if_info->b;
  rtx x = if_info->x;
  rtx orig_a, orig_b;
  rtx insn_a, insn_b;
  rtx tmp, target;
  int is_mem = 0;
  int insn_cost;
  enum rtx_code code;

  /* A conditional move from two memory sources is equivalent to a
     conditional on their addresses followed by a load.  Don't do this
     early because it'll screw alias analysis.  Note that we've
     already checked for no side effects.  */
  if (! no_new_pseudos && cse_not_expected
      && MEM_P (a) && MEM_P (b)
      && BRANCH_COST >= 5)
    {
      a = XEXP (a, 0);
      b = XEXP (b, 0);
      x = gen_reg_rtx (Pmode);
      is_mem = 1;
    }

  /* ??? We could handle this if we knew that a load from A or B could
     not fault.  This is also true if we've already loaded
     from the address along the path from ENTRY.  */
  else if (may_trap_p (a) || may_trap_p (b))
    return FALSE;

  /* if (test) x = a + b; else x = c - d;
     => y = a + b;
        x = c - d;
	if (test)
	  x = y;
  */

  code = GET_CODE (if_info->cond);
  insn_a = if_info->insn_a;
  insn_b = if_info->insn_b;

  /* Total insn_rtx_cost should be smaller than branch cost.  Exit
     if insn_rtx_cost can't be estimated.  */
  if (insn_a)
    {
      insn_cost = insn_rtx_cost (PATTERN (insn_a));
      if (insn_cost == 0 || insn_cost > COSTS_N_INSNS (BRANCH_COST))
	return FALSE;
    }
  else
    {
      insn_cost = 0;
    }

  if (insn_b) {
    insn_cost += insn_rtx_cost (PATTERN (insn_b));
    if (insn_cost == 0 || insn_cost > COSTS_N_INSNS (BRANCH_COST))
      return FALSE;
  }

  /* Possibly rearrange operands to make things come out more natural.  */
  if (reversed_comparison_code (if_info->cond, if_info->jump) != UNKNOWN)
    {
      int reversep = 0;
      if (rtx_equal_p (b, x))
	reversep = 1;
      else if (general_operand (b, GET_MODE (b)))
	reversep = 1;

      if (reversep)
	{
	  code = reversed_comparison_code (if_info->cond, if_info->jump);
	  tmp = a, a = b, b = tmp;
	  tmp = insn_a, insn_a = insn_b, insn_b = tmp;
	}
    }

  start_sequence ();

  orig_a = a;
  orig_b = b;

  /* If either operand is complex, load it into a register first.
     The best way to do this is to copy the original insn.  In this
     way we preserve any clobbers etc that the insn may have had.
     This is of course not possible in the IS_MEM case.  */
  if (! general_operand (a, GET_MODE (a)))
    {
      rtx set;

      if (no_new_pseudos)
	goto end_seq_and_fail;

      if (is_mem)
	{
	  tmp = gen_reg_rtx (GET_MODE (a));
	  tmp = emit_insn (gen_rtx_SET (VOIDmode, tmp, a));
	}
      else if (! insn_a)
	goto end_seq_and_fail;
      else
	{
	  a = gen_reg_rtx (GET_MODE (a));
	  tmp = copy_rtx (insn_a);
	  set = single_set (tmp);
	  SET_DEST (set) = a;
	  tmp = emit_insn (PATTERN (tmp));
	}
      if (recog_memoized (tmp) < 0)
	goto end_seq_and_fail;
    }
  if (! general_operand (b, GET_MODE (b)))
    {
      rtx set, last;

      if (no_new_pseudos)
	goto end_seq_and_fail;

      if (is_mem)
	{
          tmp = gen_reg_rtx (GET_MODE (b));
	  tmp = gen_rtx_SET (VOIDmode, tmp, b);
	}
      else if (! insn_b)
	goto end_seq_and_fail;
      else
	{
          b = gen_reg_rtx (GET_MODE (b));
	  tmp = copy_rtx (insn_b);
	  set = single_set (tmp);
	  SET_DEST (set) = b;
	  tmp = PATTERN (tmp);
	}

      /* If insn to set up A clobbers any registers B depends on, try to
	 swap insn that sets up A with the one that sets up B.  If even
	 that doesn't help, punt.  */
      last = get_last_insn ();
      if (last && modified_in_p (orig_b, last))
	{
	  tmp = emit_insn_before (tmp, get_insns ());
	  if (modified_in_p (orig_a, tmp))
	    goto end_seq_and_fail;
	}
      else
	tmp = emit_insn (tmp);

      if (recog_memoized (tmp) < 0)
	goto end_seq_and_fail;
    }

  target = noce_emit_cmove (if_info, x, code, XEXP (if_info->cond, 0),
			    XEXP (if_info->cond, 1), a, b);

  if (! target)
    goto end_seq_and_fail;

  /* If we're handling a memory for above, emit the load now.  */
  if (is_mem)
    {
      tmp = gen_rtx_MEM (GET_MODE (if_info->x), target);

      /* Copy over flags as appropriate.  */
      if (MEM_VOLATILE_P (if_info->a) || MEM_VOLATILE_P (if_info->b))
	MEM_VOLATILE_P (tmp) = 1;
      if (MEM_IN_STRUCT_P (if_info->a) && MEM_IN_STRUCT_P (if_info->b))
	MEM_IN_STRUCT_P (tmp) = 1;
      if (MEM_SCALAR_P (if_info->a) && MEM_SCALAR_P (if_info->b))
	MEM_SCALAR_P (tmp) = 1;
      if (MEM_ALIAS_SET (if_info->a) == MEM_ALIAS_SET (if_info->b))
	set_mem_alias_set (tmp, MEM_ALIAS_SET (if_info->a));
      set_mem_align (tmp,
		     MIN (MEM_ALIGN (if_info->a), MEM_ALIGN (if_info->b)));

      noce_emit_move_insn (if_info->x, tmp);
    }
  else if (target != x)
    noce_emit_move_insn (x, target);

  tmp = end_ifcvt_sequence (if_info);
  if (!tmp)
    return FALSE;

  emit_insn_before_setloc (tmp, if_info->jump, INSN_LOCATOR (if_info->insn_a));
  return TRUE;

 end_seq_and_fail:
  end_sequence ();
  return FALSE;
}

/* For most cases, the simplified condition we found is the best
   choice, but this is not the case for the min/max/abs transforms.
   For these we wish to know that it is A or B in the condition.  */

static rtx
noce_get_alt_condition (struct noce_if_info *if_info, rtx target,
			rtx *earliest)
{
  rtx cond, set, insn;
  int reverse;

  /* If target is already mentioned in the known condition, return it.  */
  if (reg_mentioned_p (target, if_info->cond))
    {
      *earliest = if_info->cond_earliest;
      return if_info->cond;
    }

  set = pc_set (if_info->jump);
  cond = XEXP (SET_SRC (set), 0);
  reverse
    = GET_CODE (XEXP (SET_SRC (set), 2)) == LABEL_REF
      && XEXP (XEXP (SET_SRC (set), 2), 0) == JUMP_LABEL (if_info->jump);

  /* If we're looking for a constant, try to make the conditional
     have that constant in it.  There are two reasons why it may
     not have the constant we want:

     1. GCC may have needed to put the constant in a register, because
        the target can't compare directly against that constant.  For
        this case, we look for a SET immediately before the comparison
        that puts a constant in that register.

     2. GCC may have canonicalized the conditional, for example
	replacing "if x < 4" with "if x <= 3".  We can undo that (or
	make equivalent types of changes) to get the constants we need
	if they're off by one in the right direction.  */

  if (GET_CODE (target) == CONST_INT)
    {
      enum rtx_code code = GET_CODE (if_info->cond);
      rtx op_a = XEXP (if_info->cond, 0);
      rtx op_b = XEXP (if_info->cond, 1);
      rtx prev_insn;

      /* First, look to see if we put a constant in a register.  */
      prev_insn = prev_nonnote_insn (if_info->cond_earliest);
      if (prev_insn
	  && INSN_P (prev_insn)
	  && GET_CODE (PATTERN (prev_insn)) == SET)
	{
	  rtx src = find_reg_equal_equiv_note (prev_insn);
	  if (!src)
	    src = SET_SRC (PATTERN (prev_insn));
	  if (GET_CODE (src) == CONST_INT)
	    {
	      if (rtx_equal_p (op_a, SET_DEST (PATTERN (prev_insn))))
		op_a = src;
	      else if (rtx_equal_p (op_b, SET_DEST (PATTERN (prev_insn))))
		op_b = src;

	      if (GET_CODE (op_a) == CONST_INT)
		{
		  rtx tmp = op_a;
		  op_a = op_b;
		  op_b = tmp;
		  code = swap_condition (code);
		}
	    }
	}

      /* Now, look to see if we can get the right constant by
	 adjusting the conditional.  */
      if (GET_CODE (op_b) == CONST_INT)
	{
	  HOST_WIDE_INT desired_val = INTVAL (target);
	  HOST_WIDE_INT actual_val = INTVAL (op_b);

	  switch (code)
	    {
	    case LT:
	      if (actual_val == desired_val + 1)
		{
		  code = LE;
		  op_b = GEN_INT (desired_val);
		}
	      break;
	    case LE:
	      if (actual_val == desired_val - 1)
		{
		  code = LT;
		  op_b = GEN_INT (desired_val);
		}
	      break;
	    case GT:
	      if (actual_val == desired_val - 1)
		{
		  code = GE;
		  op_b = GEN_INT (desired_val);
		}
	      break;
	    case GE:
	      if (actual_val == desired_val + 1)
		{
		  code = GT;
		  op_b = GEN_INT (desired_val);
		}
	      break;
	    default:
	      break;
	    }
	}

      /* If we made any changes, generate a new conditional that is
	 equivalent to what we started with, but has the right
	 constants in it.  */
      if (code != GET_CODE (if_info->cond)
	  || op_a != XEXP (if_info->cond, 0)
	  || op_b != XEXP (if_info->cond, 1))
	{
	  cond = gen_rtx_fmt_ee (code, GET_MODE (cond), op_a, op_b);
	  *earliest = if_info->cond_earliest;
	  return cond;
	}
    }

  cond = canonicalize_condition (if_info->jump, cond, reverse,
				 earliest, target, false, true);
  if (! cond || ! reg_mentioned_p (target, cond))
    return NULL;

  /* We almost certainly searched back to a different place.
     Need to re-verify correct lifetimes.  */

  /* X may not be mentioned in the range (cond_earliest, jump].  */
  for (insn = if_info->jump; insn != *earliest; insn = PREV_INSN (insn))
    if (INSN_P (insn) && reg_overlap_mentioned_p (if_info->x, PATTERN (insn)))
      return NULL;

  /* A and B may not be modified in the range [cond_earliest, jump).  */
  for (insn = *earliest; insn != if_info->jump; insn = NEXT_INSN (insn))
    if (INSN_P (insn)
	&& (modified_in_p (if_info->a, insn)
	    || modified_in_p (if_info->b, insn)))
      return NULL;

  return cond;
}

/* Convert "if (a < b) x = a; else x = b;" to "x = min(a, b);", etc.  */

static int
noce_try_minmax (struct noce_if_info *if_info)
{
  rtx cond, earliest, target, seq;
  enum rtx_code code, op;
  int unsignedp;

  /* ??? Can't guarantee that expand_binop won't create pseudos.  */
  if (no_new_pseudos)
    return FALSE;

  /* ??? Reject modes with NaNs or signed zeros since we don't know how
     they will be resolved with an SMIN/SMAX.  It wouldn't be too hard
     to get the target to tell us...  */
  if (HONOR_SIGNED_ZEROS (GET_MODE (if_info->x))
      || HONOR_NANS (GET_MODE (if_info->x)))
    return FALSE;

  cond = noce_get_alt_condition (if_info, if_info->a, &earliest);
  if (!cond)
    return FALSE;

  /* Verify the condition is of the form we expect, and canonicalize
     the comparison code.  */
  code = GET_CODE (cond);
  if (rtx_equal_p (XEXP (cond, 0), if_info->a))
    {
      if (! rtx_equal_p (XEXP (cond, 1), if_info->b))
	return FALSE;
    }
  else if (rtx_equal_p (XEXP (cond, 1), if_info->a))
    {
      if (! rtx_equal_p (XEXP (cond, 0), if_info->b))
	return FALSE;
      code = swap_condition (code);
    }
  else
    return FALSE;

  /* Determine what sort of operation this is.  Note that the code is for
     a taken branch, so the code->operation mapping appears backwards.  */
  switch (code)
    {
    case LT:
    case LE:
    case UNLT:
    case UNLE:
      op = SMAX;
      unsignedp = 0;
      break;
    case GT:
    case GE:
    case UNGT:
    case UNGE:
      op = SMIN;
      unsignedp = 0;
      break;
    case LTU:
    case LEU:
      op = UMAX;
      unsignedp = 1;
      break;
    case GTU:
    case GEU:
      op = UMIN;
      unsignedp = 1;
      break;
    default:
      return FALSE;
    }

  start_sequence ();

  target = expand_simple_binop (GET_MODE (if_info->x), op,
				if_info->a, if_info->b,
				if_info->x, unsignedp, OPTAB_WIDEN);
  if (! target)
    {
      end_sequence ();
      return FALSE;
    }
  if (target != if_info->x)
    noce_emit_move_insn (if_info->x, target);

  seq = end_ifcvt_sequence (if_info);
  if (!seq)
    return FALSE;

  emit_insn_before_setloc (seq, if_info->jump, INSN_LOCATOR (if_info->insn_a));
  if_info->cond = cond;
  if_info->cond_earliest = earliest;

  return TRUE;
}

/* Convert "if (a < 0) x = -a; else x = a;" to "x = abs(a);", etc.  */

static int
noce_try_abs (struct noce_if_info *if_info)
{
  rtx cond, earliest, target, seq, a, b, c;
  int negate;

  /* ??? Can't guarantee that expand_binop won't create pseudos.  */
  if (no_new_pseudos)
    return FALSE;

  /* Recognize A and B as constituting an ABS or NABS.  The canonical
     form is a branch around the negation, taken when the object is the
     first operand of a comparison against 0 that evaluates to true.  */
  a = if_info->a;
  b = if_info->b;
  if (GET_CODE (a) == NEG && rtx_equal_p (XEXP (a, 0), b))
    negate = 0;
  else if (GET_CODE (b) == NEG && rtx_equal_p (XEXP (b, 0), a))
    {
      c = a; a = b; b = c;
      negate = 1;
    }
  else
    return FALSE;

  cond = noce_get_alt_condition (if_info, b, &earliest);
  if (!cond)
    return FALSE;

  /* Verify the condition is of the form we expect.  */
  if (rtx_equal_p (XEXP (cond, 0), b))
    c = XEXP (cond, 1);
  else if (rtx_equal_p (XEXP (cond, 1), b))
    {
      c = XEXP (cond, 0);
      negate = !negate;
    }
  else
    return FALSE;

  /* Verify that C is zero.  Search one step backward for a
     REG_EQUAL note or a simple source if necessary.  */
  if (REG_P (c))
    {
      rtx set, insn = prev_nonnote_insn (earliest);
      if (insn
	  && (set = single_set (insn))
	  && rtx_equal_p (SET_DEST (set), c))
	{
	  rtx note = find_reg_equal_equiv_note (insn);
	  if (note)
	    c = XEXP (note, 0);
	  else
	    c = SET_SRC (set);
	}
      else
	return FALSE;
    }
  if (MEM_P (c)
      && GET_CODE (XEXP (c, 0)) == SYMBOL_REF
      && CONSTANT_POOL_ADDRESS_P (XEXP (c, 0)))
    c = get_pool_constant (XEXP (c, 0));

  /* Work around funny ideas get_condition has wrt canonicalization.
     Note that these rtx constants are known to be CONST_INT, and
     therefore imply integer comparisons.  */
  if (c == constm1_rtx && GET_CODE (cond) == GT)
    ;
  else if (c == const1_rtx && GET_CODE (cond) == LT)
    ;
  else if (c != CONST0_RTX (GET_MODE (b)))
    return FALSE;

  /* Determine what sort of operation this is.  */
  switch (GET_CODE (cond))
    {
    case LT:
    case LE:
    case UNLT:
    case UNLE:
      negate = !negate;
      break;
    case GT:
    case GE:
    case UNGT:
    case UNGE:
      break;
    default:
      return FALSE;
    }

  start_sequence ();

  target = expand_abs_nojump (GET_MODE (if_info->x), b, if_info->x, 1);

  /* ??? It's a quandary whether cmove would be better here, especially
     for integers.  Perhaps combine will clean things up.  */
  if (target && negate)
    target = expand_simple_unop (GET_MODE (target), NEG, target, if_info->x, 0);

  if (! target)
    {
      end_sequence ();
      return FALSE;
    }

  if (target != if_info->x)
    noce_emit_move_insn (if_info->x, target);

  seq = end_ifcvt_sequence (if_info);
  if (!seq)
    return FALSE;

  emit_insn_before_setloc (seq, if_info->jump, INSN_LOCATOR (if_info->insn_a));
  if_info->cond = cond;
  if_info->cond_earliest = earliest;

  return TRUE;
}

/* Convert "if (m < 0) x = b; else x = 0;" to "x = (m >> C) & b;".  */

static int
noce_try_sign_mask (struct noce_if_info *if_info)
{
  rtx cond, t, m, c, seq;
  enum machine_mode mode;
  enum rtx_code code;

  if (no_new_pseudos)
    return FALSE;

  cond = if_info->cond;
  code = GET_CODE (cond);
  m = XEXP (cond, 0);
  c = XEXP (cond, 1);

  t = NULL_RTX;
  if (if_info->a == const0_rtx)
    {
      if ((code == LT && c == const0_rtx)
	  || (code == LE && c == constm1_rtx))
	t = if_info->b;
    }
  else if (if_info->b == const0_rtx)
    {
      if ((code == GE && c == const0_rtx)
	  || (code == GT && c == constm1_rtx))
	t = if_info->a;
    }

  if (! t || side_effects_p (t))
    return FALSE;

  /* We currently don't handle different modes.  */
  mode = GET_MODE (t);
  if (GET_MODE (m) != mode)
    return FALSE;

  /* This is only profitable if T is cheap, or T is unconditionally
     executed/evaluated in the original insn sequence.  */
  if (rtx_cost (t, SET) >= COSTS_N_INSNS (2)
      && (!if_info->b_unconditional
          || t != if_info->b))
    return FALSE;

  start_sequence ();
  /* Use emit_store_flag to generate "m < 0 ? -1 : 0" instead of expanding
     "(signed) m >> 31" directly.  This benefits targets with specialized
     insns to obtain the signmask, but still uses ashr_optab otherwise.  */
  m = emit_store_flag (gen_reg_rtx (mode), LT, m, const0_rtx, mode, 0, -1);
  t = m ? expand_binop (mode, and_optab, m, t, NULL_RTX, 0, OPTAB_DIRECT)
	: NULL_RTX;

  if (!t)
    {
      end_sequence ();
      return FALSE;
    }

  noce_emit_move_insn (if_info->x, t);

  seq = end_ifcvt_sequence (if_info);
  if (!seq)
    return FALSE;

  emit_insn_before_setloc (seq, if_info->jump, INSN_LOCATOR (if_info->insn_a));
  return TRUE;
}


/* Optimize away "if (x & C) x |= C" and similar bit manipulation
   transformations.  */

static int
noce_try_bitop (struct noce_if_info *if_info)
{
  rtx cond, x, a, result, seq;
  enum machine_mode mode;
  enum rtx_code code;
  int bitnum;

  x = if_info->x;
  cond = if_info->cond;
  code = GET_CODE (cond);

  /* Check for no else condition.  */
  if (! rtx_equal_p (x, if_info->b))
    return FALSE;

  /* Check for a suitable condition.  */
  if (code != NE && code != EQ)
    return FALSE;
  if (XEXP (cond, 1) != const0_rtx)
    return FALSE;
  cond = XEXP (cond, 0);

  /* ??? We could also handle AND here.  */
  if (GET_CODE (cond) == ZERO_EXTRACT)
    {
      if (XEXP (cond, 1) != const1_rtx
	  || GET_CODE (XEXP (cond, 2)) != CONST_INT
	  || ! rtx_equal_p (x, XEXP (cond, 0)))
	return FALSE;
      bitnum = INTVAL (XEXP (cond, 2));
      mode = GET_MODE (x);
      if (BITS_BIG_ENDIAN)
	bitnum = GET_MODE_BITSIZE (mode) - 1 - bitnum;
      if (bitnum < 0 || bitnum >= HOST_BITS_PER_WIDE_INT)
	return FALSE;
    }
  else
    return FALSE;

  a = if_info->a;
  if (GET_CODE (a) == IOR || GET_CODE (a) == XOR)
    {
      /* Check for "if (X & C) x = x op C".  */
      if (! rtx_equal_p (x, XEXP (a, 0))
          || GET_CODE (XEXP (a, 1)) != CONST_INT
	  || (INTVAL (XEXP (a, 1)) & GET_MODE_MASK (mode))
	     != (unsigned HOST_WIDE_INT) 1 << bitnum)
        return FALSE;

      /* if ((x & C) == 0) x |= C; is transformed to x |= C.   */
      /* if ((x & C) != 0) x |= C; is transformed to nothing.  */
      if (GET_CODE (a) == IOR)
	result = (code == NE) ? a : NULL_RTX;
      else if (code == NE)
	{
	  /* if ((x & C) == 0) x ^= C; is transformed to x |= C.   */
	  result = gen_int_mode ((HOST_WIDE_INT) 1 << bitnum, mode);
	  result = simplify_gen_binary (IOR, mode, x, result);
	}
      else
	{
	  /* if ((x & C) != 0) x ^= C; is transformed to x &= ~C.  */
	  result = gen_int_mode (~((HOST_WIDE_INT) 1 << bitnum), mode);
	  result = simplify_gen_binary (AND, mode, x, result);
	}
    }
  else if (GET_CODE (a) == AND)
    {
      /* Check for "if (X & C) x &= ~C".  */
      if (! rtx_equal_p (x, XEXP (a, 0))
	  || GET_CODE (XEXP (a, 1)) != CONST_INT
	  || (INTVAL (XEXP (a, 1)) & GET_MODE_MASK (mode))
	     != (~((HOST_WIDE_INT) 1 << bitnum) & GET_MODE_MASK (mode)))
        return FALSE;

      /* if ((x & C) == 0) x &= ~C; is transformed to nothing.  */
      /* if ((x & C) != 0) x &= ~C; is transformed to x &= ~C.  */
      result = (code == EQ) ? a : NULL_RTX;
    }
  else
    return FALSE;

  if (result)
    {
      start_sequence ();
      noce_emit_move_insn (x, result);
      seq = end_ifcvt_sequence (if_info);
      if (!seq)
	return FALSE;

      emit_insn_before_setloc (seq, if_info->jump,
			       INSN_LOCATOR (if_info->insn_a));
    }
  return TRUE;
}


/* Similar to get_condition, only the resulting condition must be
   valid at JUMP, instead of at EARLIEST.  */

static rtx
noce_get_condition (rtx jump, rtx *earliest)
{
  rtx cond, set, tmp;
  bool reverse;

  if (! any_condjump_p (jump))
    return NULL_RTX;

  set = pc_set (jump);

  /* If this branches to JUMP_LABEL when the condition is false,
     reverse the condition.  */
  reverse = (GET_CODE (XEXP (SET_SRC (set), 2)) == LABEL_REF
	     && XEXP (XEXP (SET_SRC (set), 2), 0) == JUMP_LABEL (jump));

  /* If the condition variable is a register and is MODE_INT, accept it.  */

  cond = XEXP (SET_SRC (set), 0);
  tmp = XEXP (cond, 0);
  if (REG_P (tmp) && GET_MODE_CLASS (GET_MODE (tmp)) == MODE_INT)
    {
      *earliest = jump;

      if (reverse)
	cond = gen_rtx_fmt_ee (reverse_condition (GET_CODE (cond)),
			       GET_MODE (cond), tmp, XEXP (cond, 1));
      return cond;
    }

  /* Otherwise, fall back on canonicalize_condition to do the dirty
     work of manipulating MODE_CC values and COMPARE rtx codes.  */
  return canonicalize_condition (jump, cond, reverse, earliest,
				 NULL_RTX, false, true);
}

/* Initialize for a simple IF-THEN or IF-THEN-ELSE block.  We will not
   be using conditional execution.  Set some fields of IF_INFO based
   on CE_INFO: test_bb, cond, jump, cond_earliest.  Return TRUE if
   things look OK.  */

static int
noce_init_if_info (struct ce_if_block *ce_info, struct noce_if_info *if_info)
{
  basic_block test_bb = ce_info->test_bb;
  rtx cond, jump;

  /* If test is comprised of && or || elements, don't handle it unless
     it is the special case of && elements without an ELSE block.  */
  if (ce_info->num_multiple_test_blocks)
    {
      if (ce_info->else_bb || !ce_info->and_and_p)
	return FALSE;

      ce_info->test_bb = test_bb = ce_info->last_test_bb;
      ce_info->num_multiple_test_blocks = 0;
      ce_info->num_and_and_blocks = 0;
      ce_info->num_or_or_blocks = 0;
    }

  /* If this is not a standard conditional jump, we can't parse it.  */
  jump = BB_END (test_bb);
  cond = noce_get_condition (jump, &if_info->cond_earliest);
  if (!cond)
    return FALSE;

  /* If the conditional jump is more than just a conditional
     jump, then we can not do if-conversion on this block.  */
  if (! onlyjump_p (jump))
    return FALSE;

  /* We must be comparing objects whose modes imply the size.  */
  if (GET_MODE (XEXP (cond, 0)) == BLKmode)
    return FALSE;

  if_info->test_bb = test_bb;
  if_info->cond = cond;
  if_info->jump = jump;

  return TRUE;
}

/* Return true if OP is ok for if-then-else processing.  */

static int
noce_operand_ok (rtx op)
{
  /* We special-case memories, so handle any of them with
     no address side effects.  */
  if (MEM_P (op))
    return ! side_effects_p (XEXP (op, 0));

  if (side_effects_p (op))
    return FALSE;

  return ! may_trap_p (op);
}

/* Return true if a write into MEM may trap or fault.  */

static bool
noce_mem_write_may_trap_or_fault_p (rtx mem)
{
  rtx addr;

  if (MEM_READONLY_P (mem))
    return true;

  if (may_trap_or_fault_p (mem))
    return true;

  addr = XEXP (mem, 0);

  /* Call target hook to avoid the effects of -fpic etc....  */
  addr = targetm.delegitimize_address (addr);

  while (addr)
    switch (GET_CODE (addr))
      {
      case CONST:
      case PRE_DEC:
      case PRE_INC:
      case POST_DEC:
      case POST_INC:
      case POST_MODIFY:
	addr = XEXP (addr, 0);
	break;
      case LO_SUM:
      case PRE_MODIFY:
	addr = XEXP (addr, 1);
	break;
      case PLUS:
	if (GET_CODE (XEXP (addr, 1)) == CONST_INT)
	  addr = XEXP (addr, 0);
	else
	  return false;
	break;
      case LABEL_REF:
	return true;
      case SYMBOL_REF:
	if (SYMBOL_REF_DECL (addr)
	    && decl_readonly_section (SYMBOL_REF_DECL (addr), 0))
	  return true;
	return false;
      default:
	return false;
      }

  return false;
}

/* Given a simple IF-THEN or IF-THEN-ELSE block, attempt to convert it
   without using conditional execution.  Return TRUE if we were
   successful at converting the block.  */

static int
noce_process_if_block (struct ce_if_block * ce_info)
{
  basic_block test_bb = ce_info->test_bb;	/* test block */
  basic_block then_bb = ce_info->then_bb;	/* THEN */
  basic_block else_bb = ce_info->else_bb;	/* ELSE or NULL */
  struct noce_if_info if_info;
  rtx insn_a, insn_b;
  rtx set_a, set_b;
  rtx orig_x, x, a, b;
  rtx jump, cond;

  /* We're looking for patterns of the form

     (1) if (...) x = a; else x = b;
     (2) x = b; if (...) x = a;
     (3) if (...) x = a;   // as if with an initial x = x.

     The later patterns require jumps to be more expensive.

     ??? For future expansion, look for multiple X in such patterns.  */

  if (!noce_init_if_info (ce_info, &if_info))
    return FALSE;

  cond = if_info.cond;
  jump = if_info.jump;

  /* Look for one of the potential sets.  */
  insn_a = first_active_insn (then_bb);
  if (! insn_a
      || insn_a != last_active_insn (then_bb, FALSE)
      || (set_a = single_set (insn_a)) == NULL_RTX)
    return FALSE;

  x = SET_DEST (set_a);
  a = SET_SRC (set_a);

  /* Look for the other potential set.  Make sure we've got equivalent
     destinations.  */
  /* ??? This is overconservative.  Storing to two different mems is
     as easy as conditionally computing the address.  Storing to a
     single mem merely requires a scratch memory to use as one of the
     destination addresses; often the memory immediately below the
     stack pointer is available for this.  */
  set_b = NULL_RTX;
  if (else_bb)
    {
      insn_b = first_active_insn (else_bb);
      if (! insn_b
	  || insn_b != last_active_insn (else_bb, FALSE)
	  || (set_b = single_set (insn_b)) == NULL_RTX
	  || ! rtx_equal_p (x, SET_DEST (set_b)))
	return FALSE;
    }
  else
    {
      insn_b = prev_nonnote_insn (if_info.cond_earliest);
      /* We're going to be moving the evaluation of B down from above
	 COND_EARLIEST to JUMP.  Make sure the relevant data is still
	 intact.  */
      if (! insn_b
	  || !NONJUMP_INSN_P (insn_b)
	  || (set_b = single_set (insn_b)) == NULL_RTX
	  || ! rtx_equal_p (x, SET_DEST (set_b))
	  || reg_overlap_mentioned_p (x, SET_SRC (set_b))
	  || modified_between_p (SET_SRC (set_b),
				 PREV_INSN (if_info.cond_earliest), jump)
	  /* Likewise with X.  In particular this can happen when
	     noce_get_condition looks farther back in the instruction
	     stream than one might expect.  */
	  || reg_overlap_mentioned_p (x, cond)
	  || reg_overlap_mentioned_p (x, a)
	  || modified_between_p (x, PREV_INSN (if_info.cond_earliest), jump))
	insn_b = set_b = NULL_RTX;
    }

  /* If x has side effects then only the if-then-else form is safe to
     convert.  But even in that case we would need to restore any notes
     (such as REG_INC) at then end.  That can be tricky if
     noce_emit_move_insn expands to more than one insn, so disable the
     optimization entirely for now if there are side effects.  */
  if (side_effects_p (x))
    return FALSE;

  b = (set_b ? SET_SRC (set_b) : x);

  /* Only operate on register destinations, and even then avoid extending
     the lifetime of hard registers on small register class machines.  */
  orig_x = x;
  if (!REG_P (x)
      || (SMALL_REGISTER_CLASSES
	  && REGNO (x) < FIRST_PSEUDO_REGISTER))
    {
      if (no_new_pseudos || GET_MODE (x) == BLKmode)
	return FALSE;

      if (GET_MODE (x) == ZERO_EXTRACT 
	  && (GET_CODE (XEXP (x, 1)) != CONST_INT 
	      || GET_CODE (XEXP (x, 2)) != CONST_INT))
	return FALSE;
	  
      x = gen_reg_rtx (GET_MODE (GET_CODE (x) == STRICT_LOW_PART
				 ? XEXP (x, 0) : x));
    }

  /* Don't operate on sources that may trap or are volatile.  */
  if (! noce_operand_ok (a) || ! noce_operand_ok (b))
    return FALSE;

  /* Set up the info block for our subroutines.  */
  if_info.insn_a = insn_a;
  if_info.insn_b = insn_b;
  if_info.x = x;
  if_info.a = a;
  if_info.b = b;
  if_info.b_unconditional = else_bb == 0;

  /* Try optimizations in some approximation of a useful order.  */
  /* ??? Should first look to see if X is live incoming at all.  If it
     isn't, we don't need anything but an unconditional set.  */

  /* Look and see if A and B are really the same.  Avoid creating silly
     cmove constructs that no one will fix up later.  */
  if (rtx_equal_p (a, b))
    {
      /* If we have an INSN_B, we don't have to create any new rtl.  Just
	 move the instruction that we already have.  If we don't have an
	 INSN_B, that means that A == X, and we've got a noop move.  In
	 that case don't do anything and let the code below delete INSN_A.  */
      if (insn_b && else_bb)
	{
	  rtx note;

	  if (else_bb && insn_b == BB_END (else_bb))
	    BB_END (else_bb) = PREV_INSN (insn_b);
	  reorder_insns (insn_b, insn_b, PREV_INSN (jump));

	  /* If there was a REG_EQUAL note, delete it since it may have been
	     true due to this insn being after a jump.  */
	  if ((note = find_reg_note (insn_b, REG_EQUAL, NULL_RTX)) != 0)
	    remove_note (insn_b, note);

	  insn_b = NULL_RTX;
	}
      /* If we have "x = b; if (...) x = a;", and x has side-effects, then
	 x must be executed twice.  */
      else if (insn_b && side_effects_p (orig_x))
	return FALSE;

      x = orig_x;
      goto success;
    }

  /* Disallow the "if (...) x = a;" form (with an implicit "else x = x;")
     for optimizations if writing to x may trap or fault, i.e. it's a memory
     other than a static var or a stack slot, is misaligned on strict
     aligned machines or is read-only.
     If x is a read-only memory, then the program is valid only if we
     avoid the store into it.  If there are stores on both the THEN and
     ELSE arms, then we can go ahead with the conversion; either the
     program is broken, or the condition is always false such that the
     other memory is selected.  */
  if (!set_b && MEM_P (orig_x) && noce_mem_write_may_trap_or_fault_p (orig_x))
    return FALSE;

  if (noce_try_move (&if_info))
    goto success;
  if (noce_try_store_flag (&if_info))
    goto success;
  if (noce_try_bitop (&if_info))
    goto success;
  if (noce_try_minmax (&if_info))
    goto success;
  if (noce_try_abs (&if_info))
    goto success;
  if (HAVE_conditional_move
      && noce_try_cmove (&if_info))
    goto success;
  if (! HAVE_conditional_execution)
    {
      if (noce_try_store_flag_constants (&if_info))
	goto success;
      if (noce_try_addcc (&if_info))
	goto success;
      if (noce_try_store_flag_mask (&if_info))
	goto success;
      if (HAVE_conditional_move
	  && noce_try_cmove_arith (&if_info))
	goto success;
      if (noce_try_sign_mask (&if_info))
	goto success;
    }

  return FALSE;

 success:
  /* The original sets may now be killed.  */
  delete_insn (insn_a);

  /* Several special cases here: First, we may have reused insn_b above,
     in which case insn_b is now NULL.  Second, we want to delete insn_b
     if it came from the ELSE block, because follows the now correct
     write that appears in the TEST block.  However, if we got insn_b from
     the TEST block, it may in fact be loading data needed for the comparison.
     We'll let life_analysis remove the insn if it's really dead.  */
  if (insn_b && else_bb)
    delete_insn (insn_b);

  /* The new insns will have been inserted immediately before the jump.  We
     should be able to remove the jump with impunity, but the condition itself
     may have been modified by gcse to be shared across basic blocks.  */
  delete_insn (jump);

  /* If we used a temporary, fix it up now.  */
  if (orig_x != x)
    {
      start_sequence ();
      noce_emit_move_insn (orig_x, x);
      insn_b = get_insns ();
      set_used_flags (orig_x);
      unshare_all_rtl_in_chain (insn_b);
      end_sequence ();

      emit_insn_after_setloc (insn_b, BB_END (test_bb), INSN_LOCATOR (insn_a));
    }

  /* Merge the blocks!  */
  merge_if_block (ce_info);

  return TRUE;
}

/* Check whether a block is suitable for conditional move conversion.
   Every insn must be a simple set of a register to a constant or a
   register.  For each assignment, store the value in the array VALS,
   indexed by register number.  COND is the condition we will
   test.  */

static int
check_cond_move_block (basic_block bb, rtx *vals, rtx cond)
{
  rtx insn;

  FOR_BB_INSNS (bb, insn)
    {
      rtx set, dest, src;

      if (!INSN_P (insn) || JUMP_P (insn))
	continue;
      set = single_set (insn);
      if (!set)
	return FALSE;

      dest = SET_DEST (set);
      src = SET_SRC (set);
      if (!REG_P (dest)
	  || (SMALL_REGISTER_CLASSES && HARD_REGISTER_P (dest)))
	return FALSE;

      if (!CONSTANT_P (src) && !register_operand (src, VOIDmode))
	return FALSE;

      if (side_effects_p (src) || side_effects_p (dest))
	return FALSE;

      if (may_trap_p (src) || may_trap_p (dest))
	return FALSE;

      /* Don't try to handle this if the source register was
	 modified earlier in the block.  */
      if ((REG_P (src)
	   && vals[REGNO (src)] != NULL)
	  || (GET_CODE (src) == SUBREG && REG_P (SUBREG_REG (src))
	      && vals[REGNO (SUBREG_REG (src))] != NULL))
	return FALSE;

      /* Don't try to handle this if the destination register was
	 modified earlier in the block.  */
      if (vals[REGNO (dest)] != NULL)
	return FALSE;

      /* Don't try to handle this if the condition uses the
	 destination register.  */
      if (reg_overlap_mentioned_p (dest, cond))
	return FALSE;

      vals[REGNO (dest)] = src;

      /* Don't try to handle this if the source register is modified
	 later in the block.  */
      if (!CONSTANT_P (src)
	  && modified_between_p (src, insn, NEXT_INSN (BB_END (bb))))
	return FALSE;
    }

  return TRUE;
}

/* Given a simple IF-THEN or IF-THEN-ELSE block, attempt to convert it
   using only conditional moves.  Return TRUE if we were successful at
   converting the block.  */

static int
cond_move_process_if_block (struct ce_if_block *ce_info)
{
  basic_block then_bb = ce_info->then_bb;
  basic_block else_bb = ce_info->else_bb;
  struct noce_if_info if_info;
  rtx jump, cond, insn, seq, cond_arg0, cond_arg1, loc_insn;
  int max_reg, size, c, i;
  rtx *then_vals;
  rtx *else_vals;
  enum rtx_code code;

  if (!HAVE_conditional_move || no_new_pseudos)
    return FALSE;

  memset (&if_info, 0, sizeof if_info);

  if (!noce_init_if_info (ce_info, &if_info))
    return FALSE;

  cond = if_info.cond;
  jump = if_info.jump;

  /* Build a mapping for each block to the value used for each
     register.  */
  max_reg = max_reg_num ();
  size = (max_reg + 1) * sizeof (rtx);
  then_vals = (rtx *) alloca (size);
  else_vals = (rtx *) alloca (size);
  memset (then_vals, 0, size);
  memset (else_vals, 0, size);

  /* Make sure the blocks are suitable.  */
  if (!check_cond_move_block (then_bb, then_vals, cond)
      || (else_bb && !check_cond_move_block (else_bb, else_vals, cond)))
    return FALSE;

  /* Make sure the blocks can be used together.  If the same register
     is set in both blocks, and is not set to a constant in both
     cases, then both blocks must set it to the same register.  We
     have already verified that if it is set to a register, that the
     source register does not change after the assignment.  Also count
     the number of registers set in only one of the blocks.  */
  c = 0;
  for (i = 0; i <= max_reg; ++i)
    {
      if (!then_vals[i] && !else_vals[i])
	continue;

      if (!then_vals[i] || !else_vals[i])
	++c;
      else
	{
	  if (!CONSTANT_P (then_vals[i])
	      && !CONSTANT_P (else_vals[i])
	      && !rtx_equal_p (then_vals[i], else_vals[i]))
	    return FALSE;
	}
    }

  /* Make sure it is reasonable to convert this block.  What matters
     is the number of assignments currently made in only one of the
     branches, since if we convert we are going to always execute
     them.  */
  if (c > MAX_CONDITIONAL_EXECUTE)
    return FALSE;

  /* Emit the conditional moves.  First do the then block, then do
     anything left in the else blocks.  */

  code = GET_CODE (cond);
  cond_arg0 = XEXP (cond, 0);
  cond_arg1 = XEXP (cond, 1);

  start_sequence ();

  FOR_BB_INSNS (then_bb, insn)
    {
      rtx set, target, dest, t, e;
      unsigned int regno;

      if (!INSN_P (insn) || JUMP_P (insn))
	continue;
      set = single_set (insn);
      gcc_assert (set && REG_P (SET_DEST (set)));

      dest = SET_DEST (set);
      regno = REGNO (dest);
      t = then_vals[regno];
      e = else_vals[regno];
      gcc_assert (t);
      if (!e)
	e = dest;
      target = noce_emit_cmove (&if_info, dest, code, cond_arg0, cond_arg1,
				t, e);
      if (!target)
	{
	  end_sequence ();
	  return FALSE;
	}

      if (target != dest)
	noce_emit_move_insn (dest, target);
    }

  if (else_bb)
    {
      FOR_BB_INSNS (else_bb, insn)
	{
	  rtx set, target, dest;
	  unsigned int regno;

	  if (!INSN_P (insn) || JUMP_P (insn))
	    continue;
	  set = single_set (insn);
	  gcc_assert (set && REG_P (SET_DEST (set)));

	  dest = SET_DEST (set);
	  regno = REGNO (dest);

	  /* If this register was set in the then block, we already
	     handled this case above.  */
	  if (then_vals[regno])
	    continue;
	  gcc_assert (else_vals[regno]);

	  target = noce_emit_cmove (&if_info, dest, code, cond_arg0, cond_arg1,
				    dest, else_vals[regno]);
	  if (!target)
	    {
	      end_sequence ();
	      return FALSE;
	    }

	  if (target != dest)
	    noce_emit_move_insn (dest, target);
	}
    }

  seq = end_ifcvt_sequence (&if_info);
  if (!seq)
    return FALSE;

  loc_insn = first_active_insn (then_bb);
  if (!loc_insn)
    {
      loc_insn = first_active_insn (else_bb);
      gcc_assert (loc_insn);
    }
  emit_insn_before_setloc (seq, jump, INSN_LOCATOR (loc_insn));

  FOR_BB_INSNS (then_bb, insn)
    if (INSN_P (insn) && !JUMP_P (insn))
      delete_insn (insn);
  if (else_bb)
    {
      FOR_BB_INSNS (else_bb, insn)
	if (INSN_P (insn) && !JUMP_P (insn))
	  delete_insn (insn);
    }
  delete_insn (jump);

  merge_if_block (ce_info);

  return TRUE;
}

/* Attempt to convert an IF-THEN or IF-THEN-ELSE block into
   straight line code.  Return true if successful.  */

static int
process_if_block (struct ce_if_block * ce_info)
{
  if (! reload_completed
      && noce_process_if_block (ce_info))
    return TRUE;

  if (HAVE_conditional_move
      && cond_move_process_if_block (ce_info))
    return TRUE;

  if (HAVE_conditional_execution && reload_completed)
    {
      /* If we have && and || tests, try to first handle combining the && and
         || tests into the conditional code, and if that fails, go back and
         handle it without the && and ||, which at present handles the && case
         if there was no ELSE block.  */
      if (cond_exec_process_if_block (ce_info, TRUE))
	return TRUE;

      if (ce_info->num_multiple_test_blocks)
	{
	  cancel_changes (0);

	  if (cond_exec_process_if_block (ce_info, FALSE))
	    return TRUE;
	}
    }

  return FALSE;
}

/* Merge the blocks and mark for local life update.  */

static void
merge_if_block (struct ce_if_block * ce_info)
{
  basic_block test_bb = ce_info->test_bb;	/* last test block */
  basic_block then_bb = ce_info->then_bb;	/* THEN */
  basic_block else_bb = ce_info->else_bb;	/* ELSE or NULL */
  basic_block join_bb = ce_info->join_bb;	/* join block */
  basic_block combo_bb;

  /* All block merging is done into the lower block numbers.  */

  combo_bb = test_bb;

  /* Merge any basic blocks to handle && and || subtests.  Each of
     the blocks are on the fallthru path from the predecessor block.  */
  if (ce_info->num_multiple_test_blocks > 0)
    {
      basic_block bb = test_bb;
      basic_block last_test_bb = ce_info->last_test_bb;
      basic_block fallthru = block_fallthru (bb);

      do
	{
	  bb = fallthru;
	  fallthru = block_fallthru (bb);
	  merge_blocks (combo_bb, bb);
	  num_true_changes++;
	}
      while (bb != last_test_bb);
    }

  /* Merge TEST block into THEN block.  Normally the THEN block won't have a
     label, but it might if there were || tests.  That label's count should be
     zero, and it normally should be removed.  */

  if (then_bb)
    {
      if (combo_bb->il.rtl->global_live_at_end)
	COPY_REG_SET (combo_bb->il.rtl->global_live_at_end,
		      then_bb->il.rtl->global_live_at_end);
      merge_blocks (combo_bb, then_bb);
      num_true_changes++;
    }

  /* The ELSE block, if it existed, had a label.  That label count
     will almost always be zero, but odd things can happen when labels
     get their addresses taken.  */
  if (else_bb)
    {
      merge_blocks (combo_bb, else_bb);
      num_true_changes++;
    }

  /* If there was no join block reported, that means it was not adjacent
     to the others, and so we cannot merge them.  */

  if (! join_bb)
    {
      rtx last = BB_END (combo_bb);

      /* The outgoing edge for the current COMBO block should already
	 be correct.  Verify this.  */
      if (EDGE_COUNT (combo_bb->succs) == 0)
	gcc_assert (find_reg_note (last, REG_NORETURN, NULL)
		    || (NONJUMP_INSN_P (last)
			&& GET_CODE (PATTERN (last)) == TRAP_IF
			&& (TRAP_CONDITION (PATTERN (last))
			    == const_true_rtx)));

      else
      /* There should still be something at the end of the THEN or ELSE
         blocks taking us to our final destination.  */
	gcc_assert (JUMP_P (last)
		    || (EDGE_SUCC (combo_bb, 0)->dest == EXIT_BLOCK_PTR
			&& CALL_P (last)
			&& SIBLING_CALL_P (last))
		    || ((EDGE_SUCC (combo_bb, 0)->flags & EDGE_EH)
			&& can_throw_internal (last)));
    }

  /* The JOIN block may have had quite a number of other predecessors too.
     Since we've already merged the TEST, THEN and ELSE blocks, we should
     have only one remaining edge from our if-then-else diamond.  If there
     is more than one remaining edge, it must come from elsewhere.  There
     may be zero incoming edges if the THEN block didn't actually join
     back up (as with a call to a non-return function).  */
  else if (EDGE_COUNT (join_bb->preds) < 2
	   && join_bb != EXIT_BLOCK_PTR)
    {
      /* We can merge the JOIN.  */
      if (combo_bb->il.rtl->global_live_at_end)
	COPY_REG_SET (combo_bb->il.rtl->global_live_at_end,
		      join_bb->il.rtl->global_live_at_end);

      merge_blocks (combo_bb, join_bb);
      num_true_changes++;
    }
  else
    {
      /* We cannot merge the JOIN.  */

      /* The outgoing edge for the current COMBO block should already
	 be correct.  Verify this.  */
      gcc_assert (single_succ_p (combo_bb)
		  && single_succ (combo_bb) == join_bb);

      /* Remove the jump and cruft from the end of the COMBO block.  */
      if (join_bb != EXIT_BLOCK_PTR)
	tidy_fallthru_edge (single_succ_edge (combo_bb));
    }

  num_updated_if_blocks++;
}

/* Find a block ending in a simple IF condition and try to transform it
   in some way.  When converting a multi-block condition, put the new code
   in the first such block and delete the rest.  Return a pointer to this
   first block if some transformation was done.  Return NULL otherwise.  */

static basic_block
find_if_header (basic_block test_bb, int pass)
{
  ce_if_block_t ce_info;
  edge then_edge;
  edge else_edge;

  /* The kind of block we're looking for has exactly two successors.  */
  if (EDGE_COUNT (test_bb->succs) != 2)
    return NULL;

  then_edge = EDGE_SUCC (test_bb, 0);
  else_edge = EDGE_SUCC (test_bb, 1);

  /* Neither edge should be abnormal.  */
  if ((then_edge->flags & EDGE_COMPLEX)
      || (else_edge->flags & EDGE_COMPLEX))
    return NULL;

  /* Nor exit the loop.  */
  if ((then_edge->flags & EDGE_LOOP_EXIT)
      || (else_edge->flags & EDGE_LOOP_EXIT))
    return NULL;

  /* The THEN edge is canonically the one that falls through.  */
  if (then_edge->flags & EDGE_FALLTHRU)
    ;
  else if (else_edge->flags & EDGE_FALLTHRU)
    {
      edge e = else_edge;
      else_edge = then_edge;
      then_edge = e;
    }
  else
    /* Otherwise this must be a multiway branch of some sort.  */
    return NULL;

  memset (&ce_info, '\0', sizeof (ce_info));
  ce_info.test_bb = test_bb;
  ce_info.then_bb = then_edge->dest;
  ce_info.else_bb = else_edge->dest;
  ce_info.pass = pass;

#ifdef IFCVT_INIT_EXTRA_FIELDS
  IFCVT_INIT_EXTRA_FIELDS (&ce_info);
#endif

  if (find_if_block (&ce_info))
    goto success;

  if (HAVE_trap && HAVE_conditional_trap
      && find_cond_trap (test_bb, then_edge, else_edge))
    goto success;

  if (dom_computed[CDI_POST_DOMINATORS] >= DOM_NO_FAST_QUERY
      && (! HAVE_conditional_execution || reload_completed))
    {
      if (find_if_case_1 (test_bb, then_edge, else_edge))
	goto success;
      if (find_if_case_2 (test_bb, then_edge, else_edge))
	goto success;
    }

  return NULL;

 success:
  if (dump_file)
    fprintf (dump_file, "Conversion succeeded on pass %d.\n", pass);
  return ce_info.test_bb;
}

/* Return true if a block has two edges, one of which falls through to the next
   block, and the other jumps to a specific block, so that we can tell if the
   block is part of an && test or an || test.  Returns either -1 or the number
   of non-note, non-jump, non-USE/CLOBBER insns in the block.  */

static int
block_jumps_and_fallthru_p (basic_block cur_bb, basic_block target_bb)
{
  edge cur_edge;
  int fallthru_p = FALSE;
  int jump_p = FALSE;
  rtx insn;
  rtx end;
  int n_insns = 0;
  edge_iterator ei;

  if (!cur_bb || !target_bb)
    return -1;

  /* If no edges, obviously it doesn't jump or fallthru.  */
  if (EDGE_COUNT (cur_bb->succs) == 0)
    return FALSE;

  FOR_EACH_EDGE (cur_edge, ei, cur_bb->succs)
    {
      if (cur_edge->flags & EDGE_COMPLEX)
	/* Anything complex isn't what we want.  */
	return -1;

      else if (cur_edge->flags & EDGE_FALLTHRU)
	fallthru_p = TRUE;

      else if (cur_edge->dest == target_bb)
	jump_p = TRUE;

      else
	return -1;
    }

  if ((jump_p & fallthru_p) == 0)
    return -1;

  /* Don't allow calls in the block, since this is used to group && and ||
     together for conditional execution support.  ??? we should support
     conditional execution support across calls for IA-64 some day, but
     for now it makes the code simpler.  */
  end = BB_END (cur_bb);
  insn = BB_HEAD (cur_bb);

  while (insn != NULL_RTX)
    {
      if (CALL_P (insn))
	return -1;

      if (INSN_P (insn)
	  && !JUMP_P (insn)
	  && GET_CODE (PATTERN (insn)) != USE
	  && GET_CODE (PATTERN (insn)) != CLOBBER)
	n_insns++;

      if (insn == end)
	break;

      insn = NEXT_INSN (insn);
    }

  return n_insns;
}

/* Determine if a given basic block heads a simple IF-THEN or IF-THEN-ELSE
   block.  If so, we'll try to convert the insns to not require the branch.
   Return TRUE if we were successful at converting the block.  */

static int
find_if_block (struct ce_if_block * ce_info)
{
  basic_block test_bb = ce_info->test_bb;
  basic_block then_bb = ce_info->then_bb;
  basic_block else_bb = ce_info->else_bb;
  basic_block join_bb = NULL_BLOCK;
  edge cur_edge;
  basic_block next;
  edge_iterator ei;

  ce_info->last_test_bb = test_bb;

  /* Discover if any fall through predecessors of the current test basic block
     were && tests (which jump to the else block) or || tests (which jump to
     the then block).  */
  if (HAVE_conditional_execution && reload_completed
      && single_pred_p (test_bb)
      && single_pred_edge (test_bb)->flags == EDGE_FALLTHRU)
    {
      basic_block bb = single_pred (test_bb);
      basic_block target_bb;
      int max_insns = MAX_CONDITIONAL_EXECUTE;
      int n_insns;

      /* Determine if the preceding block is an && or || block.  */
      if ((n_insns = block_jumps_and_fallthru_p (bb, else_bb)) >= 0)
	{
	  ce_info->and_and_p = TRUE;
	  target_bb = else_bb;
	}
      else if ((n_insns = block_jumps_and_fallthru_p (bb, then_bb)) >= 0)
	{
	  ce_info->and_and_p = FALSE;
	  target_bb = then_bb;
	}
      else
	target_bb = NULL_BLOCK;

      if (target_bb && n_insns <= max_insns)
	{
	  int total_insns = 0;
	  int blocks = 0;

	  ce_info->last_test_bb = test_bb;

	  /* Found at least one && or || block, look for more.  */
	  do
	    {
	      ce_info->test_bb = test_bb = bb;
	      total_insns += n_insns;
	      blocks++;

	      if (!single_pred_p (bb))
		break;

	      bb = single_pred (bb);
	      n_insns = block_jumps_and_fallthru_p (bb, target_bb);
	    }
	  while (n_insns >= 0 && (total_insns + n_insns) <= max_insns);

	  ce_info->num_multiple_test_blocks = blocks;
	  ce_info->num_multiple_test_insns = total_insns;

	  if (ce_info->and_and_p)
	    ce_info->num_and_and_blocks = blocks;
	  else
	    ce_info->num_or_or_blocks = blocks;
	}
    }

  /* The THEN block of an IF-THEN combo must have exactly one predecessor,
     other than any || blocks which jump to the THEN block.  */
  if ((EDGE_COUNT (then_bb->preds) - ce_info->num_or_or_blocks) != 1)
    return FALSE;
    
  /* The edges of the THEN and ELSE blocks cannot have complex edges.  */
  FOR_EACH_EDGE (cur_edge, ei, then_bb->preds)
    {
      if (cur_edge->flags & EDGE_COMPLEX)
	return FALSE;
    }

  FOR_EACH_EDGE (cur_edge, ei, else_bb->preds)
    {
      if (cur_edge->flags & EDGE_COMPLEX)
	return FALSE;
    }

  /* The THEN block of an IF-THEN combo must have zero or one successors.  */
  if (EDGE_COUNT (then_bb->succs) > 0
      && (!single_succ_p (then_bb)
          || (single_succ_edge (then_bb)->flags & EDGE_COMPLEX)
	  || (flow2_completed && tablejump_p (BB_END (then_bb), NULL, NULL))))
    return FALSE;

  /* If the THEN block has no successors, conditional execution can still
     make a conditional call.  Don't do this unless the ELSE block has
     only one incoming edge -- the CFG manipulation is too ugly otherwise.
     Check for the last insn of the THEN block being an indirect jump, which
     is listed as not having any successors, but confuses the rest of the CE
     code processing.  ??? we should fix this in the future.  */
  if (EDGE_COUNT (then_bb->succs) == 0)
    {
      if (single_pred_p (else_bb))
	{
	  rtx last_insn = BB_END (then_bb);

	  while (last_insn
		 && NOTE_P (last_insn)
		 && last_insn != BB_HEAD (then_bb))
	    last_insn = PREV_INSN (last_insn);

	  if (last_insn
	      && JUMP_P (last_insn)
	      && ! simplejump_p (last_insn))
	    return FALSE;

	  join_bb = else_bb;
	  else_bb = NULL_BLOCK;
	}
      else
	return FALSE;
    }

  /* If the THEN block's successor is the other edge out of the TEST block,
     then we have an IF-THEN combo without an ELSE.  */
  else if (single_succ (then_bb) == else_bb)
    {
      join_bb = else_bb;
      else_bb = NULL_BLOCK;
    }

  /* If the THEN and ELSE block meet in a subsequent block, and the ELSE
     has exactly one predecessor and one successor, and the outgoing edge
     is not complex, then we have an IF-THEN-ELSE combo.  */
  else if (single_succ_p (else_bb)
	   && single_succ (then_bb) == single_succ (else_bb)
	   && single_pred_p (else_bb)
	   && ! (single_succ_edge (else_bb)->flags & EDGE_COMPLEX)
	   && ! (flow2_completed && tablejump_p (BB_END (else_bb), NULL, NULL)))
    join_bb = single_succ (else_bb);

  /* Otherwise it is not an IF-THEN or IF-THEN-ELSE combination.  */
  else
    return FALSE;

  num_possible_if_blocks++;

  if (dump_file)
    {
      fprintf (dump_file,
	       "\nIF-THEN%s block found, pass %d, start block %d "
	       "[insn %d], then %d [%d]",
	       (else_bb) ? "-ELSE" : "",
	       ce_info->pass,
	       test_bb->index,
	       BB_HEAD (test_bb) ? (int)INSN_UID (BB_HEAD (test_bb)) : -1,
	       then_bb->index,
	       BB_HEAD (then_bb) ? (int)INSN_UID (BB_HEAD (then_bb)) : -1);

      if (else_bb)
	fprintf (dump_file, ", else %d [%d]",
		 else_bb->index,
		 BB_HEAD (else_bb) ? (int)INSN_UID (BB_HEAD (else_bb)) : -1);

      fprintf (dump_file, ", join %d [%d]",
	       join_bb->index,
	       BB_HEAD (join_bb) ? (int)INSN_UID (BB_HEAD (join_bb)) : -1);

      if (ce_info->num_multiple_test_blocks > 0)
	fprintf (dump_file, ", %d %s block%s last test %d [%d]",
		 ce_info->num_multiple_test_blocks,
		 (ce_info->and_and_p) ? "&&" : "||",
		 (ce_info->num_multiple_test_blocks == 1) ? "" : "s",
		 ce_info->last_test_bb->index,
		 ((BB_HEAD (ce_info->last_test_bb))
		  ? (int)INSN_UID (BB_HEAD (ce_info->last_test_bb))
		  : -1));

      fputc ('\n', dump_file);
    }

  /* Make sure IF, THEN, and ELSE, blocks are adjacent.  Actually, we get the
     first condition for free, since we've already asserted that there's a
     fallthru edge from IF to THEN.  Likewise for the && and || blocks, since
     we checked the FALLTHRU flag, those are already adjacent to the last IF
     block.  */
  /* ??? As an enhancement, move the ELSE block.  Have to deal with
     BLOCK notes, if by no other means than backing out the merge if they
     exist.  Sticky enough I don't want to think about it now.  */
  next = then_bb;
  if (else_bb && (next = next->next_bb) != else_bb)
    return FALSE;
  if ((next = next->next_bb) != join_bb && join_bb != EXIT_BLOCK_PTR)
    {
      if (else_bb)
	join_bb = NULL;
      else
	return FALSE;
    }

  /* Do the real work.  */
  ce_info->else_bb = else_bb;
  ce_info->join_bb = join_bb;

  return process_if_block (ce_info);
}

/* Convert a branch over a trap, or a branch
   to a trap, into a conditional trap.  */

static int
find_cond_trap (basic_block test_bb, edge then_edge, edge else_edge)
{
  basic_block then_bb = then_edge->dest;
  basic_block else_bb = else_edge->dest;
  basic_block other_bb, trap_bb;
  rtx trap, jump, cond, cond_earliest, seq;
  enum rtx_code code;

  /* Locate the block with the trap instruction.  */
  /* ??? While we look for no successors, we really ought to allow
     EH successors.  Need to fix merge_if_block for that to work.  */
  if ((trap = block_has_only_trap (then_bb)) != NULL)
    trap_bb = then_bb, other_bb = else_bb;
  else if ((trap = block_has_only_trap (else_bb)) != NULL)
    trap_bb = else_bb, other_bb = then_bb;
  else
    return FALSE;

  if (dump_file)
    {
      fprintf (dump_file, "\nTRAP-IF block found, start %d, trap %d\n",
	       test_bb->index, trap_bb->index);
    }

  /* If this is not a standard conditional jump, we can't parse it.  */
  jump = BB_END (test_bb);
  cond = noce_get_condition (jump, &cond_earliest);
  if (! cond)
    return FALSE;

  /* If the conditional jump is more than just a conditional jump, then
     we can not do if-conversion on this block.  */
  if (! onlyjump_p (jump))
    return FALSE;

  /* We must be comparing objects whose modes imply the size.  */
  if (GET_MODE (XEXP (cond, 0)) == BLKmode)
    return FALSE;

  /* Reverse the comparison code, if necessary.  */
  code = GET_CODE (cond);
  if (then_bb == trap_bb)
    {
      code = reversed_comparison_code (cond, jump);
      if (code == UNKNOWN)
	return FALSE;
    }

  /* Attempt to generate the conditional trap.  */
  seq = gen_cond_trap (code, XEXP (cond, 0),
		       XEXP (cond, 1),
		       TRAP_CODE (PATTERN (trap)));
  if (seq == NULL)
    return FALSE;

  num_true_changes++;

  /* Emit the new insns before cond_earliest.  */
  emit_insn_before_setloc (seq, cond_earliest, INSN_LOCATOR (trap));

  /* Delete the trap block if possible.  */
  remove_edge (trap_bb == then_bb ? then_edge : else_edge);
  if (EDGE_COUNT (trap_bb->preds) == 0)
    delete_basic_block (trap_bb);

  /* If the non-trap block and the test are now adjacent, merge them.
     Otherwise we must insert a direct branch.  */
  if (test_bb->next_bb == other_bb)
    {
      struct ce_if_block new_ce_info;
      delete_insn (jump);
      memset (&new_ce_info, '\0', sizeof (new_ce_info));
      new_ce_info.test_bb = test_bb;
      new_ce_info.then_bb = NULL;
      new_ce_info.else_bb = NULL;
      new_ce_info.join_bb = other_bb;
      merge_if_block (&new_ce_info);
    }
  else
    {
      rtx lab, newjump;

      lab = JUMP_LABEL (jump);
      newjump = emit_jump_insn_after (gen_jump (lab), jump);
      LABEL_NUSES (lab) += 1;
      JUMP_LABEL (newjump) = lab;
      emit_barrier_after (newjump);

      delete_insn (jump);
    }

  return TRUE;
}

/* Subroutine of find_cond_trap: if BB contains only a trap insn,
   return it.  */

static rtx
block_has_only_trap (basic_block bb)
{
  rtx trap;

  /* We're not the exit block.  */
  if (bb == EXIT_BLOCK_PTR)
    return NULL_RTX;

  /* The block must have no successors.  */
  if (EDGE_COUNT (bb->succs) > 0)
    return NULL_RTX;

  /* The only instruction in the THEN block must be the trap.  */
  trap = first_active_insn (bb);
  if (! (trap == BB_END (bb)
	 && GET_CODE (PATTERN (trap)) == TRAP_IF
         && TRAP_CONDITION (PATTERN (trap)) == const_true_rtx))
    return NULL_RTX;

  return trap;
}

/* Look for IF-THEN-ELSE cases in which one of THEN or ELSE is
   transformable, but not necessarily the other.  There need be no
   JOIN block.

   Return TRUE if we were successful at converting the block.

   Cases we'd like to look at:

   (1)
	if (test) goto over; // x not live
	x = a;
	goto label;
	over:

   becomes

	x = a;
	if (! test) goto label;

   (2)
	if (test) goto E; // x not live
	x = big();
	goto L;
	E:
	x = b;
	goto M;

   becomes

	x = b;
	if (test) goto M;
	x = big();
	goto L;

   (3) // This one's really only interesting for targets that can do
       // multiway branching, e.g. IA-64 BBB bundles.  For other targets
       // it results in multiple branches on a cache line, which often
       // does not sit well with predictors.

	if (test1) goto E; // predicted not taken
	x = a;
	if (test2) goto F;
	...
	E:
	x = b;
	J:

   becomes

	x = a;
	if (test1) goto E;
	if (test2) goto F;

   Notes:

   (A) Don't do (2) if the branch is predicted against the block we're
   eliminating.  Do it anyway if we can eliminate a branch; this requires
   that the sole successor of the eliminated block postdominate the other
   side of the if.

   (B) With CE, on (3) we can steal from both sides of the if, creating

	if (test1) x = a;
	if (!test1) x = b;
	if (test1) goto J;
	if (test2) goto F;
	...
	J:

   Again, this is most useful if J postdominates.

   (C) CE substitutes for helpful life information.

   (D) These heuristics need a lot of work.  */

/* Tests for case 1 above.  */

static int
find_if_case_1 (basic_block test_bb, edge then_edge, edge else_edge)
{
  basic_block then_bb = then_edge->dest;
  basic_block else_bb = else_edge->dest, new_bb;
  int then_bb_index;

  /* If we are partitioning hot/cold basic blocks, we don't want to
     mess up unconditional or indirect jumps that cross between hot
     and cold sections.
  
     Basic block partitioning may result in some jumps that appear to
     be optimizable (or blocks that appear to be mergeable), but which really 
     must be left untouched (they are required to make it safely across 
     partition boundaries).  See  the comments at the top of 
     bb-reorder.c:partition_hot_cold_basic_blocks for complete details.  */

  if ((BB_END (then_bb) 
       && find_reg_note (BB_END (then_bb), REG_CROSSING_JUMP, NULL_RTX))
      || (BB_END (test_bb)
	  && find_reg_note (BB_END (test_bb), REG_CROSSING_JUMP, NULL_RTX))
      || (BB_END (else_bb)
	  && find_reg_note (BB_END (else_bb), REG_CROSSING_JUMP, 
			    NULL_RTX)))
    return FALSE;

  /* THEN has one successor.  */
  if (!single_succ_p (then_bb))
    return FALSE;

  /* THEN does not fall through, but is not strange either.  */
  if (single_succ_edge (then_bb)->flags & (EDGE_COMPLEX | EDGE_FALLTHRU))
    return FALSE;

  /* THEN has one predecessor.  */
  if (!single_pred_p (then_bb))
    return FALSE;

  /* THEN must do something.  */
  if (forwarder_block_p (then_bb))
    return FALSE;

  num_possible_if_blocks++;
  if (dump_file)
    fprintf (dump_file,
	     "\nIF-CASE-1 found, start %d, then %d\n",
	     test_bb->index, then_bb->index);

  /* THEN is small.  */
  if (! cheap_bb_rtx_cost_p (then_bb, COSTS_N_INSNS (BRANCH_COST)))
    return FALSE;

  /* Registers set are dead, or are predicable.  */
  if (! dead_or_predicable (test_bb, then_bb, else_bb,
			    single_succ (then_bb), 1))
    return FALSE;

  /* Conversion went ok, including moving the insns and fixing up the
     jump.  Adjust the CFG to match.  */

  bitmap_ior (test_bb->il.rtl->global_live_at_end,
	      else_bb->il.rtl->global_live_at_start,
	      then_bb->il.rtl->global_live_at_end);


  /* We can avoid creating a new basic block if then_bb is immediately
     followed by else_bb, i.e. deleting then_bb allows test_bb to fall
     thru to else_bb.  */

  if (then_bb->next_bb == else_bb
      && then_bb->prev_bb == test_bb
      && else_bb != EXIT_BLOCK_PTR)
    {
      redirect_edge_succ (FALLTHRU_EDGE (test_bb), else_bb);
      new_bb = 0;
    }
  else
    new_bb = redirect_edge_and_branch_force (FALLTHRU_EDGE (test_bb),
                                             else_bb);

  then_bb_index = then_bb->index;
  delete_basic_block (then_bb);

  /* Make rest of code believe that the newly created block is the THEN_BB
     block we removed.  */
  if (new_bb)
    {
      new_bb->index = then_bb_index;
      SET_BASIC_BLOCK (then_bb_index, new_bb);
      /* Since the fallthru edge was redirected from test_bb to new_bb,
         we need to ensure that new_bb is in the same partition as
         test bb (you can not fall through across section boundaries).  */
      BB_COPY_PARTITION (new_bb, test_bb);
    }
  /* We've possibly created jump to next insn, cleanup_cfg will solve that
     later.  */

  num_true_changes++;
  num_updated_if_blocks++;

  return TRUE;
}

/* Test for case 2 above.  */

static int
find_if_case_2 (basic_block test_bb, edge then_edge, edge else_edge)
{
  basic_block then_bb = then_edge->dest;
  basic_block else_bb = else_edge->dest;
  edge else_succ;
  rtx note;

  /* If we are partitioning hot/cold basic blocks, we don't want to
     mess up unconditional or indirect jumps that cross between hot
     and cold sections.
  
     Basic block partitioning may result in some jumps that appear to
     be optimizable (or blocks that appear to be mergeable), but which really 
     must be left untouched (they are required to make it safely across 
     partition boundaries).  See  the comments at the top of 
     bb-reorder.c:partition_hot_cold_basic_blocks for complete details.  */

  if ((BB_END (then_bb)
       && find_reg_note (BB_END (then_bb), REG_CROSSING_JUMP, NULL_RTX))
      || (BB_END (test_bb)
	  && find_reg_note (BB_END (test_bb), REG_CROSSING_JUMP, NULL_RTX))
      || (BB_END (else_bb) 
	  && find_reg_note (BB_END (else_bb), REG_CROSSING_JUMP, 
			    NULL_RTX)))
    return FALSE;

  /* ELSE has one successor.  */
  if (!single_succ_p (else_bb))
    return FALSE;
  else
    else_succ = single_succ_edge (else_bb);

  /* ELSE outgoing edge is not complex.  */
  if (else_succ->flags & EDGE_COMPLEX)
    return FALSE;

  /* ELSE has one predecessor.  */
  if (!single_pred_p (else_bb))
    return FALSE;

  /* THEN is not EXIT.  */
  if (then_bb->index < NUM_FIXED_BLOCKS)
    return FALSE;

  /* ELSE is predicted or SUCC(ELSE) postdominates THEN.  */
  note = find_reg_note (BB_END (test_bb), REG_BR_PROB, NULL_RTX);
  if (note && INTVAL (XEXP (note, 0)) >= REG_BR_PROB_BASE / 2)
    ;
  else if (else_succ->dest->index < NUM_FIXED_BLOCKS
	   || dominated_by_p (CDI_POST_DOMINATORS, then_bb,
			      else_succ->dest))
    ;
  else
    return FALSE;

  num_possible_if_blocks++;
  if (dump_file)
    fprintf (dump_file,
	     "\nIF-CASE-2 found, start %d, else %d\n",
	     test_bb->index, else_bb->index);

  /* ELSE is small.  */
  if (! cheap_bb_rtx_cost_p (else_bb, COSTS_N_INSNS (BRANCH_COST)))
    return FALSE;

  /* Registers set are dead, or are predicable.  */
  if (! dead_or_predicable (test_bb, else_bb, then_bb, else_succ->dest, 0))
    return FALSE;

  /* Conversion went ok, including moving the insns and fixing up the
     jump.  Adjust the CFG to match.  */

  bitmap_ior (test_bb->il.rtl->global_live_at_end,
	      then_bb->il.rtl->global_live_at_start,
	      else_bb->il.rtl->global_live_at_end);

  delete_basic_block (else_bb);

  num_true_changes++;
  num_updated_if_blocks++;

  /* ??? We may now fallthru from one of THEN's successors into a join
     block.  Rerun cleanup_cfg?  Examine things manually?  Wait?  */

  return TRUE;
}

/* A subroutine of dead_or_predicable called through for_each_rtx.
   Return 1 if a memory is found.  */

static int
find_memory (rtx *px, void *data ATTRIBUTE_UNUSED)
{
  return MEM_P (*px);
}

/* Used by the code above to perform the actual rtl transformations.
   Return TRUE if successful.

   TEST_BB is the block containing the conditional branch.  MERGE_BB
   is the block containing the code to manipulate.  NEW_DEST is the
   label TEST_BB should be branching to after the conversion.
   REVERSEP is true if the sense of the branch should be reversed.  */

static int
dead_or_predicable (basic_block test_bb, basic_block merge_bb,
		    basic_block other_bb, basic_block new_dest, int reversep)
{
  rtx head, end, jump, earliest = NULL_RTX, old_dest, new_label = NULL_RTX;

  jump = BB_END (test_bb);

  /* Find the extent of the real code in the merge block.  */
  head = BB_HEAD (merge_bb);
  end = BB_END (merge_bb);

  /* If merge_bb ends with a tablejump, predicating/moving insn's
     into test_bb and then deleting merge_bb will result in the jumptable
     that follows merge_bb being removed along with merge_bb and then we
     get an unresolved reference to the jumptable.  */
  if (tablejump_p (end, NULL, NULL))
    return FALSE;

  if (LABEL_P (head))
    head = NEXT_INSN (head);
  if (NOTE_P (head))
    {
      if (head == end)
	{
	  head = end = NULL_RTX;
	  goto no_body;
	}
      head = NEXT_INSN (head);
    }

  if (JUMP_P (end))
    {
      if (head == end)
	{
	  head = end = NULL_RTX;
	  goto no_body;
	}
      end = PREV_INSN (end);
    }

  /* Disable handling dead code by conditional execution if the machine needs
     to do anything funny with the tests, etc.  */
#ifndef IFCVT_MODIFY_TESTS
  if (HAVE_conditional_execution)
    {
      /* In the conditional execution case, we have things easy.  We know
	 the condition is reversible.  We don't have to check life info
	 because we're going to conditionally execute the code anyway.
	 All that's left is making sure the insns involved can actually
	 be predicated.  */

      rtx cond, prob_val;

      cond = cond_exec_get_condition (jump);
      if (! cond)
	return FALSE;

      prob_val = find_reg_note (jump, REG_BR_PROB, NULL_RTX);
      if (prob_val)
	prob_val = XEXP (prob_val, 0);

      if (reversep)
	{
	  enum rtx_code rev = reversed_comparison_code (cond, jump);
	  if (rev == UNKNOWN)
	    return FALSE;
	  cond = gen_rtx_fmt_ee (rev, GET_MODE (cond), XEXP (cond, 0),
			         XEXP (cond, 1));
	  if (prob_val)
	    prob_val = GEN_INT (REG_BR_PROB_BASE - INTVAL (prob_val));
	}

      if (! cond_exec_process_insns ((ce_if_block_t *)0, head, end, cond,
				     prob_val, 0))
	goto cancel;

      earliest = jump;
    }
  else
#endif
    {
      /* In the non-conditional execution case, we have to verify that there
	 are no trapping operations, no calls, no references to memory, and
	 that any registers modified are dead at the branch site.  */

      rtx insn, cond, prev;
      regset merge_set, tmp, test_live, test_set;
      struct propagate_block_info *pbi;
      unsigned i, fail = 0;
      bitmap_iterator bi;

      /* Check for no calls or trapping operations.  */
      for (insn = head; ; insn = NEXT_INSN (insn))
	{
	  if (CALL_P (insn))
	    return FALSE;
	  if (INSN_P (insn))
	    {
	      if (may_trap_p (PATTERN (insn)))
		return FALSE;

	      /* ??? Even non-trapping memories such as stack frame
		 references must be avoided.  For stores, we collect
		 no lifetime info; for reads, we'd have to assert
		 true_dependence false against every store in the
		 TEST range.  */
	      if (for_each_rtx (&PATTERN (insn), find_memory, NULL))
		return FALSE;
	    }
	  if (insn == end)
	    break;
	}

      if (! any_condjump_p (jump))
	return FALSE;

      /* Find the extent of the conditional.  */
      cond = noce_get_condition (jump, &earliest);
      if (! cond)
	return FALSE;

      /* Collect:
	   MERGE_SET = set of registers set in MERGE_BB
	   TEST_LIVE = set of registers live at EARLIEST
	   TEST_SET  = set of registers set between EARLIEST and the
		       end of the block.  */

      tmp = ALLOC_REG_SET (&reg_obstack);
      merge_set = ALLOC_REG_SET (&reg_obstack);
      test_live = ALLOC_REG_SET (&reg_obstack);
      test_set = ALLOC_REG_SET (&reg_obstack);

      /* ??? bb->local_set is only valid during calculate_global_regs_live,
	 so we must recompute usage for MERGE_BB.  Not so bad, I suppose,
         since we've already asserted that MERGE_BB is small.  */
      /* If we allocated new pseudos (e.g. in the conditional move
	 expander called from noce_emit_cmove), we must resize the
	 array first.  */
      if (max_regno < max_reg_num ())
	{
	  max_regno = max_reg_num ();
	  allocate_reg_info (max_regno, FALSE, FALSE);
	}
      propagate_block (merge_bb, tmp, merge_set, merge_set, 0);

      /* For small register class machines, don't lengthen lifetimes of
	 hard registers before reload.  */
      if (SMALL_REGISTER_CLASSES && ! reload_completed)
	{
          EXECUTE_IF_SET_IN_BITMAP (merge_set, 0, i, bi)
	    {
	      if (i < FIRST_PSEUDO_REGISTER
		  && ! fixed_regs[i]
		  && ! global_regs[i])
		fail = 1;
	    }
	}

      /* For TEST, we're interested in a range of insns, not a whole block.
	 Moreover, we're interested in the insns live from OTHER_BB.  */

      COPY_REG_SET (test_live, other_bb->il.rtl->global_live_at_start);
      pbi = init_propagate_block_info (test_bb, test_live, test_set, test_set,
				       0);

      for (insn = jump; ; insn = prev)
	{
	  prev = propagate_one_insn (pbi, insn);
	  if (insn == earliest)
	    break;
	}

      free_propagate_block_info (pbi);

      /* We can perform the transformation if
	   MERGE_SET & (TEST_SET | TEST_LIVE)
	 and
	   TEST_SET & merge_bb->il.rtl->global_live_at_start
	 are empty.  */

      if (bitmap_intersect_p (test_set, merge_set)
	  || bitmap_intersect_p (test_live, merge_set)
	  || bitmap_intersect_p (test_set,
	    			 merge_bb->il.rtl->global_live_at_start))
	fail = 1;

      FREE_REG_SET (tmp);
      FREE_REG_SET (merge_set);
      FREE_REG_SET (test_live);
      FREE_REG_SET (test_set);

      if (fail)
	return FALSE;
    }

 no_body:
  /* We don't want to use normal invert_jump or redirect_jump because
     we don't want to delete_insn called.  Also, we want to do our own
     change group management.  */

  old_dest = JUMP_LABEL (jump);
  if (other_bb != new_dest)
    {
      new_label = block_label (new_dest);
      if (reversep
	  ? ! invert_jump_1 (jump, new_label)
	  : ! redirect_jump_1 (jump, new_label))
	goto cancel;
    }

  if (! apply_change_group ())
    return FALSE;

  if (other_bb != new_dest)
    {
      redirect_jump_2 (jump, old_dest, new_label, -1, reversep);

      redirect_edge_succ (BRANCH_EDGE (test_bb), new_dest);
      if (reversep)
	{
	  gcov_type count, probability;
	  count = BRANCH_EDGE (test_bb)->count;
	  BRANCH_EDGE (test_bb)->count = FALLTHRU_EDGE (test_bb)->count;
	  FALLTHRU_EDGE (test_bb)->count = count;
	  probability = BRANCH_EDGE (test_bb)->probability;
	  BRANCH_EDGE (test_bb)->probability
	    = FALLTHRU_EDGE (test_bb)->probability;
	  FALLTHRU_EDGE (test_bb)->probability = probability;
	  update_br_prob_note (test_bb);
	}
    }

  /* Move the insns out of MERGE_BB to before the branch.  */
  if (head != NULL)
    {
      rtx insn;

      if (end == BB_END (merge_bb))
	BB_END (merge_bb) = PREV_INSN (head);

      if (squeeze_notes (&head, &end))
	return TRUE;

      /* PR 21767: When moving insns above a conditional branch, REG_EQUAL
	 notes might become invalid.  */
      insn = head;
      do
	{
	  rtx note, set;

	  if (! INSN_P (insn))
	    continue;
	  note = find_reg_note (insn, REG_EQUAL, NULL_RTX);
	  if (! note)
	    continue;
	  set = single_set (insn);
	  if (!set || !function_invariant_p (SET_SRC (set)))
	    remove_note (insn, note);
	} while (insn != end && (insn = NEXT_INSN (insn)));

      reorder_insns (head, end, PREV_INSN (earliest));
    }

  /* Remove the jump and edge if we can.  */
  if (other_bb == new_dest)
    {
      delete_insn (jump);
      remove_edge (BRANCH_EDGE (test_bb));
      /* ??? Can't merge blocks here, as then_bb is still in use.
	 At minimum, the merge will get done just before bb-reorder.  */
    }

  return TRUE;

 cancel:
  cancel_changes (0);
  return FALSE;
}

/* Main entry point for all if-conversion.  */

static void
if_convert (int x_life_data_ok)
{
  basic_block bb;
  int pass;

  num_possible_if_blocks = 0;
  num_updated_if_blocks = 0;
  num_true_changes = 0;
  life_data_ok = (x_life_data_ok != 0);

  if ((! targetm.cannot_modify_jumps_p ())
      && (!flag_reorder_blocks_and_partition || !no_new_pseudos
	  || !targetm.have_named_sections))
    {
      struct loops loops;

      flow_loops_find (&loops);
      mark_loop_exit_edges (&loops);
      flow_loops_free (&loops);
      free_dominance_info (CDI_DOMINATORS);
    }

  /* Compute postdominators if we think we'll use them.  */
  if (HAVE_conditional_execution || life_data_ok)
    calculate_dominance_info (CDI_POST_DOMINATORS);

  if (life_data_ok)
    clear_bb_flags ();

  /* Go through each of the basic blocks looking for things to convert.  If we
     have conditional execution, we make multiple passes to allow us to handle
     IF-THEN{-ELSE} blocks within other IF-THEN{-ELSE} blocks.  */
  pass = 0;
  do
    {
      cond_exec_changed_p = FALSE;
      pass++;

#ifdef IFCVT_MULTIPLE_DUMPS
      if (dump_file && pass > 1)
	fprintf (dump_file, "\n\n========== Pass %d ==========\n", pass);
#endif

      FOR_EACH_BB (bb)
	{
	  basic_block new_bb;
	  while ((new_bb = find_if_header (bb, pass)))
	    bb = new_bb;
	}

#ifdef IFCVT_MULTIPLE_DUMPS
      if (dump_file && cond_exec_changed_p)
	print_rtl_with_bb (dump_file, get_insns ());
#endif
    }
  while (cond_exec_changed_p);

#ifdef IFCVT_MULTIPLE_DUMPS
  if (dump_file)
    fprintf (dump_file, "\n\n========== no more changes\n");
#endif

  free_dominance_info (CDI_POST_DOMINATORS);

  if (dump_file)
    fflush (dump_file);

  clear_aux_for_blocks ();

  /* Rebuild life info for basic blocks that require it.  */
  if (num_true_changes && life_data_ok)
    {
      /* If we allocated new pseudos, we must resize the array for sched1.  */
      if (max_regno < max_reg_num ())
	{
	  max_regno = max_reg_num ();
	  allocate_reg_info (max_regno, FALSE, FALSE);
	}
      update_life_info_in_dirty_blocks (UPDATE_LIFE_GLOBAL_RM_NOTES,
					PROP_DEATH_NOTES | PROP_SCAN_DEAD_CODE
					| PROP_KILL_DEAD_CODE);
    }

  /* Write the final stats.  */
  if (dump_file && num_possible_if_blocks > 0)
    {
      fprintf (dump_file,
	       "\n%d possible IF blocks searched.\n",
	       num_possible_if_blocks);
      fprintf (dump_file,
	       "%d IF blocks converted.\n",
	       num_updated_if_blocks);
      fprintf (dump_file,
	       "%d true changes made.\n\n\n",
	       num_true_changes);
    }

#ifdef ENABLE_CHECKING
  verify_flow_info ();
#endif
}

static bool
gate_handle_if_conversion (void)
{
  return (optimize > 0);
}

/* If-conversion and CFG cleanup.  */
static unsigned int
rest_of_handle_if_conversion (void)
{
  if (flag_if_conversion)
    {
      if (dump_file)
        dump_flow_info (dump_file, dump_flags);
      cleanup_cfg (CLEANUP_EXPENSIVE);
      reg_scan (get_insns (), max_reg_num ());
      if_convert (0);
    }

  timevar_push (TV_JUMP);
  cleanup_cfg (CLEANUP_EXPENSIVE);
  reg_scan (get_insns (), max_reg_num ());
  timevar_pop (TV_JUMP);
  return 0;
}

struct tree_opt_pass pass_rtl_ifcvt =
{
  "ce1",                                /* name */
  gate_handle_if_conversion,            /* gate */
  rest_of_handle_if_conversion,         /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_IFCVT,                             /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func,                       /* todo_flags_finish */
  'C'                                   /* letter */
};

static bool
gate_handle_if_after_combine (void)
{
  return (optimize > 0 && flag_if_conversion);
}


/* Rerun if-conversion, as combine may have simplified things enough
   to now meet sequence length restrictions.  */
static unsigned int
rest_of_handle_if_after_combine (void)
{
  no_new_pseudos = 0;
  if_convert (1);
  no_new_pseudos = 1;
  return 0;
}

struct tree_opt_pass pass_if_after_combine =
{
  "ce2",                                /* name */
  gate_handle_if_after_combine,         /* gate */
  rest_of_handle_if_after_combine,      /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_IFCVT,                             /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func |
  TODO_ggc_collect,                     /* todo_flags_finish */
  'C'                                   /* letter */
};


static bool
gate_handle_if_after_reload (void)
{
  return (optimize > 0);
}

static unsigned int
rest_of_handle_if_after_reload (void)
{
  /* Last attempt to optimize CFG, as scheduling, peepholing and insn
     splitting possibly introduced more crossjumping opportunities.  */
  cleanup_cfg (CLEANUP_EXPENSIVE
               | CLEANUP_UPDATE_LIFE
               | (flag_crossjumping ? CLEANUP_CROSSJUMP : 0));
  if (flag_if_conversion2)
    if_convert (1);
  return 0;
}


struct tree_opt_pass pass_if_after_reload =
{
  "ce3",                                /* name */
  gate_handle_if_after_reload,          /* gate */
  rest_of_handle_if_after_reload,       /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_IFCVT2,                            /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func |
  TODO_ggc_collect,                     /* todo_flags_finish */
  'E'                                   /* letter */
};


