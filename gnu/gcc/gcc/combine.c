/* Optimize by combining instructions for GNU compiler.
   Copyright (C) 1987, 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.

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

/* This module is essentially the "combiner" phase of the U. of Arizona
   Portable Optimizer, but redone to work on our list-structured
   representation for RTL instead of their string representation.

   The LOG_LINKS of each insn identify the most recent assignment
   to each REG used in the insn.  It is a list of previous insns,
   each of which contains a SET for a REG that is used in this insn
   and not used or set in between.  LOG_LINKs never cross basic blocks.
   They were set up by the preceding pass (lifetime analysis).

   We try to combine each pair of insns joined by a logical link.
   We also try to combine triples of insns A, B and C when
   C has a link back to B and B has a link back to A.

   LOG_LINKS does not have links for use of the CC0.  They don't
   need to, because the insn that sets the CC0 is always immediately
   before the insn that tests it.  So we always regard a branch
   insn as having a logical link to the preceding insn.  The same is true
   for an insn explicitly using CC0.

   We check (with use_crosses_set_p) to avoid combining in such a way
   as to move a computation to a place where its value would be different.

   Combination is done by mathematically substituting the previous
   insn(s) values for the regs they set into the expressions in
   the later insns that refer to these regs.  If the result is a valid insn
   for our target machine, according to the machine description,
   we install it, delete the earlier insns, and update the data flow
   information (LOG_LINKS and REG_NOTES) for what we did.

   There are a few exceptions where the dataflow information created by
   flow.c aren't completely updated:

   - reg_live_length is not updated
   - reg_n_refs is not adjusted in the rare case when a register is
     no longer required in a computation
   - there are extremely rare cases (see distribute_notes) when a
     REG_DEAD note is lost
   - a LOG_LINKS entry that refers to an insn with multiple SETs may be
     removed because there is no way to know which register it was
     linking

   To simplify substitution, we combine only when the earlier insn(s)
   consist of only a single assignment.  To simplify updating afterward,
   we never combine when a subroutine call appears in the middle.

   Since we do not represent assignments to CC0 explicitly except when that
   is all an insn does, there is no LOG_LINKS entry in an insn that uses
   the condition code for the insn that set the condition code.
   Fortunately, these two insns must be consecutive.
   Therefore, every JUMP_INSN is taken to have an implicit logical link
   to the preceding insn.  This is not quite right, since non-jumps can
   also use the condition code; but in practice such insns would not
   combine anyway.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "tm_p.h"
#include "flags.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "insn-config.h"
#include "function.h"
/* Include expr.h after insn-config.h so we get HAVE_conditional_move.  */
#include "expr.h"
#include "insn-attr.h"
#include "recog.h"
#include "real.h"
#include "toplev.h"
#include "target.h"
#include "optabs.h"
#include "insn-codes.h"
#include "rtlhooks-def.h"
/* Include output.h for dump_file.  */
#include "output.h"
#include "params.h"
#include "timevar.h"
#include "tree-pass.h"

/* Number of attempts to combine instructions in this function.  */

static int combine_attempts;

/* Number of attempts that got as far as substitution in this function.  */

static int combine_merges;

/* Number of instructions combined with added SETs in this function.  */

static int combine_extras;

/* Number of instructions combined in this function.  */

static int combine_successes;

/* Totals over entire compilation.  */

static int total_attempts, total_merges, total_extras, total_successes;

/* combine_instructions may try to replace the right hand side of the
   second instruction with the value of an associated REG_EQUAL note
   before throwing it at try_combine.  That is problematic when there
   is a REG_DEAD note for a register used in the old right hand side
   and can cause distribute_notes to do wrong things.  This is the
   second instruction if it has been so modified, null otherwise.  */

static rtx i2mod;

/* When I2MOD is nonnull, this is a copy of the old right hand side.  */

static rtx i2mod_old_rhs;

/* When I2MOD is nonnull, this is a copy of the new right hand side.  */

static rtx i2mod_new_rhs;

/* Vector mapping INSN_UIDs to cuids.
   The cuids are like uids but increase monotonically always.
   Combine always uses cuids so that it can compare them.
   But actually renumbering the uids, which we used to do,
   proves to be a bad idea because it makes it hard to compare
   the dumps produced by earlier passes with those from later passes.  */

static int *uid_cuid;
static int max_uid_cuid;

/* Get the cuid of an insn.  */

#define INSN_CUID(INSN) \
(INSN_UID (INSN) > max_uid_cuid ? insn_cuid (INSN) : uid_cuid[INSN_UID (INSN)])

/* Maximum register number, which is the size of the tables below.  */

static unsigned int combine_max_regno;

struct reg_stat {
  /* Record last point of death of (hard or pseudo) register n.  */
  rtx				last_death;

  /* Record last point of modification of (hard or pseudo) register n.  */
  rtx				last_set;

  /* The next group of fields allows the recording of the last value assigned
     to (hard or pseudo) register n.  We use this information to see if an
     operation being processed is redundant given a prior operation performed
     on the register.  For example, an `and' with a constant is redundant if
     all the zero bits are already known to be turned off.

     We use an approach similar to that used by cse, but change it in the
     following ways:

     (1) We do not want to reinitialize at each label.
     (2) It is useful, but not critical, to know the actual value assigned
	 to a register.  Often just its form is helpful.

     Therefore, we maintain the following fields:

     last_set_value		the last value assigned
     last_set_label		records the value of label_tick when the
				register was assigned
     last_set_table_tick	records the value of label_tick when a
				value using the register is assigned
     last_set_invalid		set to nonzero when it is not valid
				to use the value of this register in some
				register's value

     To understand the usage of these tables, it is important to understand
     the distinction between the value in last_set_value being valid and
     the register being validly contained in some other expression in the
     table.

     (The next two parameters are out of date).

     reg_stat[i].last_set_value is valid if it is nonzero, and either
     reg_n_sets[i] is 1 or reg_stat[i].last_set_label == label_tick.

     Register I may validly appear in any expression returned for the value
     of another register if reg_n_sets[i] is 1.  It may also appear in the
     value for register J if reg_stat[j].last_set_invalid is zero, or
     reg_stat[i].last_set_label < reg_stat[j].last_set_label.

     If an expression is found in the table containing a register which may
     not validly appear in an expression, the register is replaced by
     something that won't match, (clobber (const_int 0)).  */

  /* Record last value assigned to (hard or pseudo) register n.  */

  rtx				last_set_value;

  /* Record the value of label_tick when an expression involving register n
     is placed in last_set_value.  */

  int				last_set_table_tick;

  /* Record the value of label_tick when the value for register n is placed in
     last_set_value.  */

  int				last_set_label;

  /* These fields are maintained in parallel with last_set_value and are
     used to store the mode in which the register was last set, the bits
     that were known to be zero when it was last set, and the number of
     sign bits copies it was known to have when it was last set.  */

  unsigned HOST_WIDE_INT	last_set_nonzero_bits;
  char				last_set_sign_bit_copies;
  ENUM_BITFIELD(machine_mode)	last_set_mode : 8;

  /* Set nonzero if references to register n in expressions should not be
     used.  last_set_invalid is set nonzero when this register is being
     assigned to and last_set_table_tick == label_tick.  */

  char				last_set_invalid;

  /* Some registers that are set more than once and used in more than one
     basic block are nevertheless always set in similar ways.  For example,
     a QImode register may be loaded from memory in two places on a machine
     where byte loads zero extend.

     We record in the following fields if a register has some leading bits
     that are always equal to the sign bit, and what we know about the
     nonzero bits of a register, specifically which bits are known to be
     zero.

     If an entry is zero, it means that we don't know anything special.  */

  unsigned char			sign_bit_copies;

  unsigned HOST_WIDE_INT	nonzero_bits;

  /* Record the value of the label_tick when the last truncation
     happened.  The field truncated_to_mode is only valid if
     truncation_label == label_tick.  */

  int				truncation_label;

  /* Record the last truncation seen for this register.  If truncation
     is not a nop to this mode we might be able to save an explicit
     truncation if we know that value already contains a truncated
     value.  */

  ENUM_BITFIELD(machine_mode)	truncated_to_mode : 8;
};

static struct reg_stat *reg_stat;

/* Record the cuid of the last insn that invalidated memory
   (anything that writes memory, and subroutine calls, but not pushes).  */

static int mem_last_set;

/* Record the cuid of the last CALL_INSN
   so we can tell whether a potential combination crosses any calls.  */

static int last_call_cuid;

/* When `subst' is called, this is the insn that is being modified
   (by combining in a previous insn).  The PATTERN of this insn
   is still the old pattern partially modified and it should not be
   looked at, but this may be used to examine the successors of the insn
   to judge whether a simplification is valid.  */

static rtx subst_insn;

/* This is the lowest CUID that `subst' is currently dealing with.
   get_last_value will not return a value if the register was set at or
   after this CUID.  If not for this mechanism, we could get confused if
   I2 or I1 in try_combine were an insn that used the old value of a register
   to obtain a new value.  In that case, we might erroneously get the
   new value of the register when we wanted the old one.  */

static int subst_low_cuid;

/* This contains any hard registers that are used in newpat; reg_dead_at_p
   must consider all these registers to be always live.  */

static HARD_REG_SET newpat_used_regs;

/* This is an insn to which a LOG_LINKS entry has been added.  If this
   insn is the earlier than I2 or I3, combine should rescan starting at
   that location.  */

static rtx added_links_insn;

/* Basic block in which we are performing combines.  */
static basic_block this_basic_block;

/* A bitmap indicating which blocks had registers go dead at entry.
   After combine, we'll need to re-do global life analysis with
   those blocks as starting points.  */
static sbitmap refresh_blocks;

/* The following array records the insn_rtx_cost for every insn
   in the instruction stream.  */

static int *uid_insn_cost;

/* Length of the currently allocated uid_insn_cost array.  */

static int last_insn_cost;

/* Incremented for each label.  */

static int label_tick;

/* Mode used to compute significance in reg_stat[].nonzero_bits.  It is the
   largest integer mode that can fit in HOST_BITS_PER_WIDE_INT.  */

static enum machine_mode nonzero_bits_mode;

/* Nonzero when reg_stat[].nonzero_bits and reg_stat[].sign_bit_copies can
   be safely used.  It is zero while computing them and after combine has
   completed.  This former test prevents propagating values based on
   previously set values, which can be incorrect if a variable is modified
   in a loop.  */

static int nonzero_sign_valid;


/* Record one modification to rtl structure
   to be undone by storing old_contents into *where.  */

struct undo
{
  struct undo *next;
  enum { UNDO_RTX, UNDO_INT, UNDO_MODE } kind;
  union { rtx r; int i; enum machine_mode m; } old_contents;
  union { rtx *r; int *i; } where;
};

/* Record a bunch of changes to be undone, up to MAX_UNDO of them.
   num_undo says how many are currently recorded.

   other_insn is nonzero if we have modified some other insn in the process
   of working on subst_insn.  It must be verified too.  */

struct undobuf
{
  struct undo *undos;
  struct undo *frees;
  rtx other_insn;
};

static struct undobuf undobuf;

/* Number of times the pseudo being substituted for
   was found and replaced.  */

static int n_occurrences;

static rtx reg_nonzero_bits_for_combine (rtx, enum machine_mode, rtx,
					 enum machine_mode,
					 unsigned HOST_WIDE_INT,
					 unsigned HOST_WIDE_INT *);
static rtx reg_num_sign_bit_copies_for_combine (rtx, enum machine_mode, rtx,
						enum machine_mode,
						unsigned int, unsigned int *);
static void do_SUBST (rtx *, rtx);
static void do_SUBST_INT (int *, int);
static void init_reg_last (void);
static void setup_incoming_promotions (void);
static void set_nonzero_bits_and_sign_copies (rtx, rtx, void *);
static int cant_combine_insn_p (rtx);
static int can_combine_p (rtx, rtx, rtx, rtx, rtx *, rtx *);
static int combinable_i3pat (rtx, rtx *, rtx, rtx, int, rtx *);
static int contains_muldiv (rtx);
static rtx try_combine (rtx, rtx, rtx, int *);
static void undo_all (void);
static void undo_commit (void);
static rtx *find_split_point (rtx *, rtx);
static rtx subst (rtx, rtx, rtx, int, int);
static rtx combine_simplify_rtx (rtx, enum machine_mode, int);
static rtx simplify_if_then_else (rtx);
static rtx simplify_set (rtx);
static rtx simplify_logical (rtx);
static rtx expand_compound_operation (rtx);
static rtx expand_field_assignment (rtx);
static rtx make_extraction (enum machine_mode, rtx, HOST_WIDE_INT,
			    rtx, unsigned HOST_WIDE_INT, int, int, int);
static rtx extract_left_shift (rtx, int);
static rtx make_compound_operation (rtx, enum rtx_code);
static int get_pos_from_mask (unsigned HOST_WIDE_INT,
			      unsigned HOST_WIDE_INT *);
static rtx canon_reg_for_combine (rtx, rtx);
static rtx force_to_mode (rtx, enum machine_mode,
			  unsigned HOST_WIDE_INT, int);
static rtx if_then_else_cond (rtx, rtx *, rtx *);
static rtx known_cond (rtx, enum rtx_code, rtx, rtx);
static int rtx_equal_for_field_assignment_p (rtx, rtx);
static rtx make_field_assignment (rtx);
static rtx apply_distributive_law (rtx);
static rtx distribute_and_simplify_rtx (rtx, int);
static rtx simplify_and_const_int_1 (enum machine_mode, rtx,
				     unsigned HOST_WIDE_INT);
static rtx simplify_and_const_int (rtx, enum machine_mode, rtx,
				   unsigned HOST_WIDE_INT);
static int merge_outer_ops (enum rtx_code *, HOST_WIDE_INT *, enum rtx_code,
			    HOST_WIDE_INT, enum machine_mode, int *);
static rtx simplify_shift_const_1 (enum rtx_code, enum machine_mode, rtx, int);
static rtx simplify_shift_const (rtx, enum rtx_code, enum machine_mode, rtx,
				 int);
static int recog_for_combine (rtx *, rtx, rtx *);
static rtx gen_lowpart_for_combine (enum machine_mode, rtx);
static enum rtx_code simplify_comparison (enum rtx_code, rtx *, rtx *);
static void update_table_tick (rtx);
static void record_value_for_reg (rtx, rtx, rtx);
static void check_conversions (rtx, rtx);
static void record_dead_and_set_regs_1 (rtx, rtx, void *);
static void record_dead_and_set_regs (rtx);
static int get_last_value_validate (rtx *, rtx, int, int);
static rtx get_last_value (rtx);
static int use_crosses_set_p (rtx, int);
static void reg_dead_at_p_1 (rtx, rtx, void *);
static int reg_dead_at_p (rtx, rtx);
static void move_deaths (rtx, rtx, int, rtx, rtx *);
static int reg_bitfield_target_p (rtx, rtx);
static void distribute_notes (rtx, rtx, rtx, rtx, rtx, rtx);
static void distribute_links (rtx);
static void mark_used_regs_combine (rtx);
static int insn_cuid (rtx);
static void record_promoted_value (rtx, rtx);
static int unmentioned_reg_p_1 (rtx *, void *);
static bool unmentioned_reg_p (rtx, rtx);
static void record_truncated_value (rtx);
static bool reg_truncated_to_mode (enum machine_mode, rtx);
static rtx gen_lowpart_or_truncate (enum machine_mode, rtx);


/* It is not safe to use ordinary gen_lowpart in combine.
   See comments in gen_lowpart_for_combine.  */
#undef RTL_HOOKS_GEN_LOWPART
#define RTL_HOOKS_GEN_LOWPART              gen_lowpart_for_combine

/* Our implementation of gen_lowpart never emits a new pseudo.  */
#undef RTL_HOOKS_GEN_LOWPART_NO_EMIT
#define RTL_HOOKS_GEN_LOWPART_NO_EMIT      gen_lowpart_for_combine

#undef RTL_HOOKS_REG_NONZERO_REG_BITS
#define RTL_HOOKS_REG_NONZERO_REG_BITS     reg_nonzero_bits_for_combine

#undef RTL_HOOKS_REG_NUM_SIGN_BIT_COPIES
#define RTL_HOOKS_REG_NUM_SIGN_BIT_COPIES  reg_num_sign_bit_copies_for_combine

#undef RTL_HOOKS_REG_TRUNCATED_TO_MODE
#define RTL_HOOKS_REG_TRUNCATED_TO_MODE    reg_truncated_to_mode

static const struct rtl_hooks combine_rtl_hooks = RTL_HOOKS_INITIALIZER;


/* Substitute NEWVAL, an rtx expression, into INTO, a place in some
   insn.  The substitution can be undone by undo_all.  If INTO is already
   set to NEWVAL, do not record this change.  Because computing NEWVAL might
   also call SUBST, we have to compute it before we put anything into
   the undo table.  */

static void
do_SUBST (rtx *into, rtx newval)
{
  struct undo *buf;
  rtx oldval = *into;

  if (oldval == newval)
    return;

  /* We'd like to catch as many invalid transformations here as
     possible.  Unfortunately, there are way too many mode changes
     that are perfectly valid, so we'd waste too much effort for
     little gain doing the checks here.  Focus on catching invalid
     transformations involving integer constants.  */
  if (GET_MODE_CLASS (GET_MODE (oldval)) == MODE_INT
      && GET_CODE (newval) == CONST_INT)
    {
      /* Sanity check that we're replacing oldval with a CONST_INT
	 that is a valid sign-extension for the original mode.  */
      gcc_assert (INTVAL (newval)
		  == trunc_int_for_mode (INTVAL (newval), GET_MODE (oldval)));

      /* Replacing the operand of a SUBREG or a ZERO_EXTEND with a
	 CONST_INT is not valid, because after the replacement, the
	 original mode would be gone.  Unfortunately, we can't tell
	 when do_SUBST is called to replace the operand thereof, so we
	 perform this test on oldval instead, checking whether an
	 invalid replacement took place before we got here.  */
      gcc_assert (!(GET_CODE (oldval) == SUBREG
		    && GET_CODE (SUBREG_REG (oldval)) == CONST_INT));
      gcc_assert (!(GET_CODE (oldval) == ZERO_EXTEND
		    && GET_CODE (XEXP (oldval, 0)) == CONST_INT));
    }

  if (undobuf.frees)
    buf = undobuf.frees, undobuf.frees = buf->next;
  else
    buf = XNEW (struct undo);

  buf->kind = UNDO_RTX;
  buf->where.r = into;
  buf->old_contents.r = oldval;
  *into = newval;

  buf->next = undobuf.undos, undobuf.undos = buf;
}

#define SUBST(INTO, NEWVAL)	do_SUBST(&(INTO), (NEWVAL))

/* Similar to SUBST, but NEWVAL is an int expression.  Note that substitution
   for the value of a HOST_WIDE_INT value (including CONST_INT) is
   not safe.  */

static void
do_SUBST_INT (int *into, int newval)
{
  struct undo *buf;
  int oldval = *into;

  if (oldval == newval)
    return;

  if (undobuf.frees)
    buf = undobuf.frees, undobuf.frees = buf->next;
  else
    buf = XNEW (struct undo);

  buf->kind = UNDO_INT;
  buf->where.i = into;
  buf->old_contents.i = oldval;
  *into = newval;

  buf->next = undobuf.undos, undobuf.undos = buf;
}

#define SUBST_INT(INTO, NEWVAL)  do_SUBST_INT(&(INTO), (NEWVAL))

/* Similar to SUBST, but just substitute the mode.  This is used when
   changing the mode of a pseudo-register, so that any other
   references to the entry in the regno_reg_rtx array will change as
   well.  */

static void
do_SUBST_MODE (rtx *into, enum machine_mode newval)
{
  struct undo *buf;
  enum machine_mode oldval = GET_MODE (*into);

  if (oldval == newval)
    return;

  if (undobuf.frees)
    buf = undobuf.frees, undobuf.frees = buf->next;
  else
    buf = XNEW (struct undo);

  buf->kind = UNDO_MODE;
  buf->where.r = into;
  buf->old_contents.m = oldval;
  PUT_MODE (*into, newval);

  buf->next = undobuf.undos, undobuf.undos = buf;
}

#define SUBST_MODE(INTO, NEWVAL)  do_SUBST_MODE(&(INTO), (NEWVAL))

/* Subroutine of try_combine.  Determine whether the combine replacement
   patterns NEWPAT and NEWI2PAT are cheaper according to insn_rtx_cost
   that the original instruction sequence I1, I2 and I3.  Note that I1
   and/or NEWI2PAT may be NULL_RTX.  This function returns false, if the
   costs of all instructions can be estimated, and the replacements are
   more expensive than the original sequence.  */

static bool
combine_validate_cost (rtx i1, rtx i2, rtx i3, rtx newpat, rtx newi2pat)
{
  int i1_cost, i2_cost, i3_cost;
  int new_i2_cost, new_i3_cost;
  int old_cost, new_cost;

  /* Lookup the original insn_rtx_costs.  */
  i2_cost = INSN_UID (i2) <= last_insn_cost
	    ? uid_insn_cost[INSN_UID (i2)] : 0;
  i3_cost = INSN_UID (i3) <= last_insn_cost
	    ? uid_insn_cost[INSN_UID (i3)] : 0;

  if (i1)
    {
      i1_cost = INSN_UID (i1) <= last_insn_cost
		? uid_insn_cost[INSN_UID (i1)] : 0;
      old_cost = (i1_cost > 0 && i2_cost > 0 && i3_cost > 0)
		 ? i1_cost + i2_cost + i3_cost : 0;
    }
  else
    {
      old_cost = (i2_cost > 0 && i3_cost > 0) ? i2_cost + i3_cost : 0;
      i1_cost = 0;
    }

  /* Calculate the replacement insn_rtx_costs.  */
  new_i3_cost = insn_rtx_cost (newpat);
  if (newi2pat)
    {
      new_i2_cost = insn_rtx_cost (newi2pat);
      new_cost = (new_i2_cost > 0 && new_i3_cost > 0)
		 ? new_i2_cost + new_i3_cost : 0;
    }
  else
    {
      new_cost = new_i3_cost;
      new_i2_cost = 0;
    }

  if (undobuf.other_insn)
    {
      int old_other_cost, new_other_cost;

      old_other_cost = (INSN_UID (undobuf.other_insn) <= last_insn_cost
			? uid_insn_cost[INSN_UID (undobuf.other_insn)] : 0);
      new_other_cost = insn_rtx_cost (PATTERN (undobuf.other_insn));
      if (old_other_cost > 0 && new_other_cost > 0)
	{
	  old_cost += old_other_cost;
	  new_cost += new_other_cost;
	}
      else
	old_cost = 0;
    }

  /* Disallow this recombination if both new_cost and old_cost are
     greater than zero, and new_cost is greater than old cost.  */
  if (old_cost > 0
      && new_cost > old_cost)
    {
      if (dump_file)
	{
	  if (i1)
	    {
	      fprintf (dump_file,
		       "rejecting combination of insns %d, %d and %d\n",
		       INSN_UID (i1), INSN_UID (i2), INSN_UID (i3));
	      fprintf (dump_file, "original costs %d + %d + %d = %d\n",
		       i1_cost, i2_cost, i3_cost, old_cost);
	    }
	  else
	    {
	      fprintf (dump_file,
		       "rejecting combination of insns %d and %d\n",
		       INSN_UID (i2), INSN_UID (i3));
	      fprintf (dump_file, "original costs %d + %d = %d\n",
		       i2_cost, i3_cost, old_cost);
	    }

	  if (newi2pat)
	    {
	      fprintf (dump_file, "replacement costs %d + %d = %d\n",
		       new_i2_cost, new_i3_cost, new_cost);
	    }
	  else
	    fprintf (dump_file, "replacement cost %d\n", new_cost);
	}

      return false;
    }

  /* Update the uid_insn_cost array with the replacement costs.  */
  uid_insn_cost[INSN_UID (i2)] = new_i2_cost;
  uid_insn_cost[INSN_UID (i3)] = new_i3_cost;
  if (i1)
    uid_insn_cost[INSN_UID (i1)] = 0;

  return true;
}

/* Main entry point for combiner.  F is the first insn of the function.
   NREGS is the first unused pseudo-reg number.

   Return nonzero if the combiner has turned an indirect jump
   instruction into a direct jump.  */
static int
combine_instructions (rtx f, unsigned int nregs)
{
  rtx insn, next;
#ifdef HAVE_cc0
  rtx prev;
#endif
  int i;
  unsigned int j = 0;
  rtx links, nextlinks;
  sbitmap_iterator sbi;

  int new_direct_jump_p = 0;

  combine_attempts = 0;
  combine_merges = 0;
  combine_extras = 0;
  combine_successes = 0;

  combine_max_regno = nregs;

  rtl_hooks = combine_rtl_hooks;

  reg_stat = XCNEWVEC (struct reg_stat, nregs);

  init_recog_no_volatile ();

  /* Compute maximum uid value so uid_cuid can be allocated.  */

  for (insn = f, i = 0; insn; insn = NEXT_INSN (insn))
    if (INSN_UID (insn) > i)
      i = INSN_UID (insn);

  uid_cuid = XNEWVEC (int, i + 1);
  max_uid_cuid = i;

  nonzero_bits_mode = mode_for_size (HOST_BITS_PER_WIDE_INT, MODE_INT, 0);

  /* Don't use reg_stat[].nonzero_bits when computing it.  This can cause
     problems when, for example, we have j <<= 1 in a loop.  */

  nonzero_sign_valid = 0;

  /* Compute the mapping from uids to cuids.
     Cuids are numbers assigned to insns, like uids,
     except that cuids increase monotonically through the code.

     Scan all SETs and see if we can deduce anything about what
     bits are known to be zero for some registers and how many copies
     of the sign bit are known to exist for those registers.

     Also set any known values so that we can use it while searching
     for what bits are known to be set.  */

  label_tick = 1;

  setup_incoming_promotions ();

  refresh_blocks = sbitmap_alloc (last_basic_block);
  sbitmap_zero (refresh_blocks);

  /* Allocate array of current insn_rtx_costs.  */
  uid_insn_cost = XCNEWVEC (int, max_uid_cuid + 1);
  last_insn_cost = max_uid_cuid;

  for (insn = f, i = 0; insn; insn = NEXT_INSN (insn))
    {
      uid_cuid[INSN_UID (insn)] = ++i;
      subst_low_cuid = i;
      subst_insn = insn;

      if (INSN_P (insn))
	{
	  note_stores (PATTERN (insn), set_nonzero_bits_and_sign_copies,
		       NULL);
	  record_dead_and_set_regs (insn);

#ifdef AUTO_INC_DEC
	  for (links = REG_NOTES (insn); links; links = XEXP (links, 1))
	    if (REG_NOTE_KIND (links) == REG_INC)
	      set_nonzero_bits_and_sign_copies (XEXP (links, 0), NULL_RTX,
						NULL);
#endif

	  /* Record the current insn_rtx_cost of this instruction.  */
	  if (NONJUMP_INSN_P (insn))
	    uid_insn_cost[INSN_UID (insn)] = insn_rtx_cost (PATTERN (insn));
	  if (dump_file)
	    fprintf(dump_file, "insn_cost %d: %d\n",
		    INSN_UID (insn), uid_insn_cost[INSN_UID (insn)]);
	}

      if (LABEL_P (insn))
	label_tick++;
    }

  nonzero_sign_valid = 1;

  /* Now scan all the insns in forward order.  */

  label_tick = 1;
  last_call_cuid = 0;
  mem_last_set = 0;
  init_reg_last ();
  setup_incoming_promotions ();

  FOR_EACH_BB (this_basic_block)
    {
      for (insn = BB_HEAD (this_basic_block);
	   insn != NEXT_INSN (BB_END (this_basic_block));
	   insn = next ? next : NEXT_INSN (insn))
	{
	  next = 0;

	  if (LABEL_P (insn))
	    label_tick++;

	  else if (INSN_P (insn))
	    {
	      /* See if we know about function return values before this
		 insn based upon SUBREG flags.  */
	      check_conversions (insn, PATTERN (insn));

	      /* Try this insn with each insn it links back to.  */

	      for (links = LOG_LINKS (insn); links; links = XEXP (links, 1))
		if ((next = try_combine (insn, XEXP (links, 0),
					 NULL_RTX, &new_direct_jump_p)) != 0)
		  goto retry;

	      /* Try each sequence of three linked insns ending with this one.  */

	      for (links = LOG_LINKS (insn); links; links = XEXP (links, 1))
		{
		  rtx link = XEXP (links, 0);

		  /* If the linked insn has been replaced by a note, then there
		     is no point in pursuing this chain any further.  */
		  if (NOTE_P (link))
		    continue;

		  for (nextlinks = LOG_LINKS (link);
		       nextlinks;
		       nextlinks = XEXP (nextlinks, 1))
		    if ((next = try_combine (insn, link,
					     XEXP (nextlinks, 0),
					     &new_direct_jump_p)) != 0)
		      goto retry;
		}

#ifdef HAVE_cc0
	      /* Try to combine a jump insn that uses CC0
		 with a preceding insn that sets CC0, and maybe with its
		 logical predecessor as well.
		 This is how we make decrement-and-branch insns.
		 We need this special code because data flow connections
		 via CC0 do not get entered in LOG_LINKS.  */

	      if (JUMP_P (insn)
		  && (prev = prev_nonnote_insn (insn)) != 0
		  && NONJUMP_INSN_P (prev)
		  && sets_cc0_p (PATTERN (prev)))
		{
		  if ((next = try_combine (insn, prev,
					   NULL_RTX, &new_direct_jump_p)) != 0)
		    goto retry;

		  for (nextlinks = LOG_LINKS (prev); nextlinks;
		       nextlinks = XEXP (nextlinks, 1))
		    if ((next = try_combine (insn, prev,
					     XEXP (nextlinks, 0),
					     &new_direct_jump_p)) != 0)
		      goto retry;
		}

	      /* Do the same for an insn that explicitly references CC0.  */
	      if (NONJUMP_INSN_P (insn)
		  && (prev = prev_nonnote_insn (insn)) != 0
		  && NONJUMP_INSN_P (prev)
		  && sets_cc0_p (PATTERN (prev))
		  && GET_CODE (PATTERN (insn)) == SET
		  && reg_mentioned_p (cc0_rtx, SET_SRC (PATTERN (insn))))
		{
		  if ((next = try_combine (insn, prev,
					   NULL_RTX, &new_direct_jump_p)) != 0)
		    goto retry;

		  for (nextlinks = LOG_LINKS (prev); nextlinks;
		       nextlinks = XEXP (nextlinks, 1))
		    if ((next = try_combine (insn, prev,
					     XEXP (nextlinks, 0),
					     &new_direct_jump_p)) != 0)
		      goto retry;
		}

	      /* Finally, see if any of the insns that this insn links to
		 explicitly references CC0.  If so, try this insn, that insn,
		 and its predecessor if it sets CC0.  */
	      for (links = LOG_LINKS (insn); links; links = XEXP (links, 1))
		if (NONJUMP_INSN_P (XEXP (links, 0))
		    && GET_CODE (PATTERN (XEXP (links, 0))) == SET
		    && reg_mentioned_p (cc0_rtx, SET_SRC (PATTERN (XEXP (links, 0))))
		    && (prev = prev_nonnote_insn (XEXP (links, 0))) != 0
		    && NONJUMP_INSN_P (prev)
		    && sets_cc0_p (PATTERN (prev))
		    && (next = try_combine (insn, XEXP (links, 0),
					    prev, &new_direct_jump_p)) != 0)
		  goto retry;
#endif

	      /* Try combining an insn with two different insns whose results it
		 uses.  */
	      for (links = LOG_LINKS (insn); links; links = XEXP (links, 1))
		for (nextlinks = XEXP (links, 1); nextlinks;
		     nextlinks = XEXP (nextlinks, 1))
		  if ((next = try_combine (insn, XEXP (links, 0),
					   XEXP (nextlinks, 0),
					   &new_direct_jump_p)) != 0)
		    goto retry;

	      /* Try this insn with each REG_EQUAL note it links back to.  */
	      for (links = LOG_LINKS (insn); links; links = XEXP (links, 1))
		{
		  rtx set, note;
		  rtx temp = XEXP (links, 0);
		  if ((set = single_set (temp)) != 0
		      && (note = find_reg_equal_equiv_note (temp)) != 0
		      && (note = XEXP (note, 0), GET_CODE (note)) != EXPR_LIST
		      /* Avoid using a register that may already been marked
			 dead by an earlier instruction.  */
		      && ! unmentioned_reg_p (note, SET_SRC (set))
		      && (GET_MODE (note) == VOIDmode
			  ? SCALAR_INT_MODE_P (GET_MODE (SET_DEST (set)))
			  : GET_MODE (SET_DEST (set)) == GET_MODE (note)))
		    {
		      /* Temporarily replace the set's source with the
			 contents of the REG_EQUAL note.  The insn will
			 be deleted or recognized by try_combine.  */
		      rtx orig = SET_SRC (set);
		      SET_SRC (set) = note;
		      i2mod = temp;
		      i2mod_old_rhs = copy_rtx (orig);
		      i2mod_new_rhs = copy_rtx (note);
		      next = try_combine (insn, i2mod, NULL_RTX,
					  &new_direct_jump_p);
		      i2mod = NULL_RTX;
		      if (next)
			goto retry;
		      SET_SRC (set) = orig;
		    }
		}

	      if (!NOTE_P (insn))
		record_dead_and_set_regs (insn);

	    retry:
	      ;
	    }
	}
    }
  clear_bb_flags ();

  EXECUTE_IF_SET_IN_SBITMAP (refresh_blocks, 0, j, sbi)
    BASIC_BLOCK (j)->flags |= BB_DIRTY;
  new_direct_jump_p |= purge_all_dead_edges ();
  delete_noop_moves ();

  update_life_info_in_dirty_blocks (UPDATE_LIFE_GLOBAL_RM_NOTES,
				    PROP_DEATH_NOTES | PROP_SCAN_DEAD_CODE
				    | PROP_KILL_DEAD_CODE);

  /* Clean up.  */
  sbitmap_free (refresh_blocks);
  free (uid_insn_cost);
  free (reg_stat);
  free (uid_cuid);

  {
    struct undo *undo, *next;
    for (undo = undobuf.frees; undo; undo = next)
      {
	next = undo->next;
	free (undo);
      }
    undobuf.frees = 0;
  }

  total_attempts += combine_attempts;
  total_merges += combine_merges;
  total_extras += combine_extras;
  total_successes += combine_successes;

  nonzero_sign_valid = 0;
  rtl_hooks = general_rtl_hooks;

  /* Make recognizer allow volatile MEMs again.  */
  init_recog ();

  return new_direct_jump_p;
}

/* Wipe the last_xxx fields of reg_stat in preparation for another pass.  */

static void
init_reg_last (void)
{
  unsigned int i;
  for (i = 0; i < combine_max_regno; i++)
    memset (reg_stat + i, 0, offsetof (struct reg_stat, sign_bit_copies));
}

/* Set up any promoted values for incoming argument registers.  */

static void
setup_incoming_promotions (void)
{
  unsigned int regno;
  rtx reg;
  enum machine_mode mode;
  int unsignedp;
  rtx first = get_insns ();

  if (targetm.calls.promote_function_args (TREE_TYPE (cfun->decl)))
    {
      for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
	/* Check whether this register can hold an incoming pointer
	   argument.  FUNCTION_ARG_REGNO_P tests outgoing register
	   numbers, so translate if necessary due to register windows.  */
	if (FUNCTION_ARG_REGNO_P (OUTGOING_REGNO (regno))
	    && (reg = promoted_input_arg (regno, &mode, &unsignedp)) != 0)
	  {
	    record_value_for_reg
	      (reg, first, gen_rtx_fmt_e ((unsignedp ? ZERO_EXTEND
					   : SIGN_EXTEND),
					  GET_MODE (reg),
					  gen_rtx_CLOBBER (mode, const0_rtx)));
	  }
    }
}

/* Called via note_stores.  If X is a pseudo that is narrower than
   HOST_BITS_PER_WIDE_INT and is being set, record what bits are known zero.

   If we are setting only a portion of X and we can't figure out what
   portion, assume all bits will be used since we don't know what will
   be happening.

   Similarly, set how many bits of X are known to be copies of the sign bit
   at all locations in the function.  This is the smallest number implied
   by any set of X.  */

static void
set_nonzero_bits_and_sign_copies (rtx x, rtx set,
				  void *data ATTRIBUTE_UNUSED)
{
  unsigned int num;

  if (REG_P (x)
      && REGNO (x) >= FIRST_PSEUDO_REGISTER
      /* If this register is undefined at the start of the file, we can't
	 say what its contents were.  */
      && ! REGNO_REG_SET_P
	 (ENTRY_BLOCK_PTR->next_bb->il.rtl->global_live_at_start, REGNO (x))
      && GET_MODE_BITSIZE (GET_MODE (x)) <= HOST_BITS_PER_WIDE_INT)
    {
      if (set == 0 || GET_CODE (set) == CLOBBER)
	{
	  reg_stat[REGNO (x)].nonzero_bits = GET_MODE_MASK (GET_MODE (x));
	  reg_stat[REGNO (x)].sign_bit_copies = 1;
	  return;
	}

      /* If this is a complex assignment, see if we can convert it into a
	 simple assignment.  */
      set = expand_field_assignment (set);

      /* If this is a simple assignment, or we have a paradoxical SUBREG,
	 set what we know about X.  */

      if (SET_DEST (set) == x
	  || (GET_CODE (SET_DEST (set)) == SUBREG
	      && (GET_MODE_SIZE (GET_MODE (SET_DEST (set)))
		  > GET_MODE_SIZE (GET_MODE (SUBREG_REG (SET_DEST (set)))))
	      && SUBREG_REG (SET_DEST (set)) == x))
	{
	  rtx src = SET_SRC (set);

#ifdef SHORT_IMMEDIATES_SIGN_EXTEND
	  /* If X is narrower than a word and SRC is a non-negative
	     constant that would appear negative in the mode of X,
	     sign-extend it for use in reg_stat[].nonzero_bits because some
	     machines (maybe most) will actually do the sign-extension
	     and this is the conservative approach.

	     ??? For 2.5, try to tighten up the MD files in this regard
	     instead of this kludge.  */

	  if (GET_MODE_BITSIZE (GET_MODE (x)) < BITS_PER_WORD
	      && GET_CODE (src) == CONST_INT
	      && INTVAL (src) > 0
	      && 0 != (INTVAL (src)
		       & ((HOST_WIDE_INT) 1
			  << (GET_MODE_BITSIZE (GET_MODE (x)) - 1))))
	    src = GEN_INT (INTVAL (src)
			   | ((HOST_WIDE_INT) (-1)
			      << GET_MODE_BITSIZE (GET_MODE (x))));
#endif

	  /* Don't call nonzero_bits if it cannot change anything.  */
	  if (reg_stat[REGNO (x)].nonzero_bits != ~(unsigned HOST_WIDE_INT) 0)
	    reg_stat[REGNO (x)].nonzero_bits
	      |= nonzero_bits (src, nonzero_bits_mode);
	  num = num_sign_bit_copies (SET_SRC (set), GET_MODE (x));
	  if (reg_stat[REGNO (x)].sign_bit_copies == 0
	      || reg_stat[REGNO (x)].sign_bit_copies > num)
	    reg_stat[REGNO (x)].sign_bit_copies = num;
	}
      else
	{
	  reg_stat[REGNO (x)].nonzero_bits = GET_MODE_MASK (GET_MODE (x));
	  reg_stat[REGNO (x)].sign_bit_copies = 1;
	}
    }
}

/* See if INSN can be combined into I3.  PRED and SUCC are optionally
   insns that were previously combined into I3 or that will be combined
   into the merger of INSN and I3.

   Return 0 if the combination is not allowed for any reason.

   If the combination is allowed, *PDEST will be set to the single
   destination of INSN and *PSRC to the single source, and this function
   will return 1.  */

static int
can_combine_p (rtx insn, rtx i3, rtx pred ATTRIBUTE_UNUSED, rtx succ,
	       rtx *pdest, rtx *psrc)
{
  int i;
  rtx set = 0, src, dest;
  rtx p;
#ifdef AUTO_INC_DEC
  rtx link;
#endif
  int all_adjacent = (succ ? (next_active_insn (insn) == succ
			      && next_active_insn (succ) == i3)
		      : next_active_insn (insn) == i3);

  /* Can combine only if previous insn is a SET of a REG, a SUBREG or CC0.
     or a PARALLEL consisting of such a SET and CLOBBERs.

     If INSN has CLOBBER parallel parts, ignore them for our processing.
     By definition, these happen during the execution of the insn.  When it
     is merged with another insn, all bets are off.  If they are, in fact,
     needed and aren't also supplied in I3, they may be added by
     recog_for_combine.  Otherwise, it won't match.

     We can also ignore a SET whose SET_DEST is mentioned in a REG_UNUSED
     note.

     Get the source and destination of INSN.  If more than one, can't
     combine.  */

  if (GET_CODE (PATTERN (insn)) == SET)
    set = PATTERN (insn);
  else if (GET_CODE (PATTERN (insn)) == PARALLEL
	   && GET_CODE (XVECEXP (PATTERN (insn), 0, 0)) == SET)
    {
      for (i = 0; i < XVECLEN (PATTERN (insn), 0); i++)
	{
	  rtx elt = XVECEXP (PATTERN (insn), 0, i);
	  rtx note;

	  switch (GET_CODE (elt))
	    {
	    /* This is important to combine floating point insns
	       for the SH4 port.  */
	    case USE:
	      /* Combining an isolated USE doesn't make sense.
		 We depend here on combinable_i3pat to reject them.  */
	      /* The code below this loop only verifies that the inputs of
		 the SET in INSN do not change.  We call reg_set_between_p
		 to verify that the REG in the USE does not change between
		 I3 and INSN.
		 If the USE in INSN was for a pseudo register, the matching
		 insn pattern will likely match any register; combining this
		 with any other USE would only be safe if we knew that the
		 used registers have identical values, or if there was
		 something to tell them apart, e.g. different modes.  For
		 now, we forgo such complicated tests and simply disallow
		 combining of USES of pseudo registers with any other USE.  */
	      if (REG_P (XEXP (elt, 0))
		  && GET_CODE (PATTERN (i3)) == PARALLEL)
		{
		  rtx i3pat = PATTERN (i3);
		  int i = XVECLEN (i3pat, 0) - 1;
		  unsigned int regno = REGNO (XEXP (elt, 0));

		  do
		    {
		      rtx i3elt = XVECEXP (i3pat, 0, i);

		      if (GET_CODE (i3elt) == USE
			  && REG_P (XEXP (i3elt, 0))
			  && (REGNO (XEXP (i3elt, 0)) == regno
			      ? reg_set_between_p (XEXP (elt, 0),
						   PREV_INSN (insn), i3)
			      : regno >= FIRST_PSEUDO_REGISTER))
			return 0;
		    }
		  while (--i >= 0);
		}
	      break;

	      /* We can ignore CLOBBERs.  */
	    case CLOBBER:
	      break;

	    case SET:
	      /* Ignore SETs whose result isn't used but not those that
		 have side-effects.  */
	      if (find_reg_note (insn, REG_UNUSED, SET_DEST (elt))
		  && (!(note = find_reg_note (insn, REG_EH_REGION, NULL_RTX))
		      || INTVAL (XEXP (note, 0)) <= 0)
		  && ! side_effects_p (elt))
		break;

	      /* If we have already found a SET, this is a second one and
		 so we cannot combine with this insn.  */
	      if (set)
		return 0;

	      set = elt;
	      break;

	    default:
	      /* Anything else means we can't combine.  */
	      return 0;
	    }
	}

      if (set == 0
	  /* If SET_SRC is an ASM_OPERANDS we can't throw away these CLOBBERs,
	     so don't do anything with it.  */
	  || GET_CODE (SET_SRC (set)) == ASM_OPERANDS)
	return 0;
    }
  else
    return 0;

  if (set == 0)
    return 0;

  set = expand_field_assignment (set);
  src = SET_SRC (set), dest = SET_DEST (set);

  /* Don't eliminate a store in the stack pointer.  */
  if (dest == stack_pointer_rtx
      /* Don't combine with an insn that sets a register to itself if it has
	 a REG_EQUAL note.  This may be part of a REG_NO_CONFLICT sequence.  */
      || (rtx_equal_p (src, dest) && find_reg_note (insn, REG_EQUAL, NULL_RTX))
      /* Can't merge an ASM_OPERANDS.  */
      || GET_CODE (src) == ASM_OPERANDS
      /* Can't merge a function call.  */
      || GET_CODE (src) == CALL
      /* Don't eliminate a function call argument.  */
      || (CALL_P (i3)
	  && (find_reg_fusage (i3, USE, dest)
	      || (REG_P (dest)
		  && REGNO (dest) < FIRST_PSEUDO_REGISTER
		  && global_regs[REGNO (dest)])))
      /* Don't substitute into an incremented register.  */
      || FIND_REG_INC_NOTE (i3, dest)
      || (succ && FIND_REG_INC_NOTE (succ, dest))
      /* Don't substitute into a non-local goto, this confuses CFG.  */
      || (JUMP_P (i3) && find_reg_note (i3, REG_NON_LOCAL_GOTO, NULL_RTX))
#if 0
      /* Don't combine the end of a libcall into anything.  */
      /* ??? This gives worse code, and appears to be unnecessary, since no
	 pass after flow uses REG_LIBCALL/REG_RETVAL notes.  Local-alloc does
	 use REG_RETVAL notes for noconflict blocks, but other code here
	 makes sure that those insns don't disappear.  */
      || find_reg_note (insn, REG_RETVAL, NULL_RTX)
#endif
      /* Make sure that DEST is not used after SUCC but before I3.  */
      || (succ && ! all_adjacent
	  && reg_used_between_p (dest, succ, i3))
      /* Make sure that the value that is to be substituted for the register
	 does not use any registers whose values alter in between.  However,
	 If the insns are adjacent, a use can't cross a set even though we
	 think it might (this can happen for a sequence of insns each setting
	 the same destination; last_set of that register might point to
	 a NOTE).  If INSN has a REG_EQUIV note, the register is always
	 equivalent to the memory so the substitution is valid even if there
	 are intervening stores.  Also, don't move a volatile asm or
	 UNSPEC_VOLATILE across any other insns.  */
      || (! all_adjacent
	  && (((!MEM_P (src)
		|| ! find_reg_note (insn, REG_EQUIV, src))
	       && use_crosses_set_p (src, INSN_CUID (insn)))
	      || (GET_CODE (src) == ASM_OPERANDS && MEM_VOLATILE_P (src))
	      || GET_CODE (src) == UNSPEC_VOLATILE))
      /* If there is a REG_NO_CONFLICT note for DEST in I3 or SUCC, we get
	 better register allocation by not doing the combine.  */
      || find_reg_note (i3, REG_NO_CONFLICT, dest)
      || (succ && find_reg_note (succ, REG_NO_CONFLICT, dest))
      /* Don't combine across a CALL_INSN, because that would possibly
	 change whether the life span of some REGs crosses calls or not,
	 and it is a pain to update that information.
	 Exception: if source is a constant, moving it later can't hurt.
	 Accept that special case, because it helps -fforce-addr a lot.  */
      || (INSN_CUID (insn) < last_call_cuid && ! CONSTANT_P (src)))
    return 0;

  /* DEST must either be a REG or CC0.  */
  if (REG_P (dest))
    {
      /* If register alignment is being enforced for multi-word items in all
	 cases except for parameters, it is possible to have a register copy
	 insn referencing a hard register that is not allowed to contain the
	 mode being copied and which would not be valid as an operand of most
	 insns.  Eliminate this problem by not combining with such an insn.

	 Also, on some machines we don't want to extend the life of a hard
	 register.  */

      if (REG_P (src)
	  && ((REGNO (dest) < FIRST_PSEUDO_REGISTER
	       && ! HARD_REGNO_MODE_OK (REGNO (dest), GET_MODE (dest)))
	      /* Don't extend the life of a hard register unless it is
		 user variable (if we have few registers) or it can't
		 fit into the desired register (meaning something special
		 is going on).
		 Also avoid substituting a return register into I3, because
		 reload can't handle a conflict with constraints of other
		 inputs.  */
	      || (REGNO (src) < FIRST_PSEUDO_REGISTER
		  && ! HARD_REGNO_MODE_OK (REGNO (src), GET_MODE (src)))))
	return 0;
    }
  else if (GET_CODE (dest) != CC0)
    return 0;


  if (GET_CODE (PATTERN (i3)) == PARALLEL)
    for (i = XVECLEN (PATTERN (i3), 0) - 1; i >= 0; i--)
      if (GET_CODE (XVECEXP (PATTERN (i3), 0, i)) == CLOBBER)
	{
	  /* Don't substitute for a register intended as a clobberable
	     operand.  */
	  rtx reg = XEXP (XVECEXP (PATTERN (i3), 0, i), 0);
	  if (rtx_equal_p (reg, dest))
	    return 0;

	  /* If the clobber represents an earlyclobber operand, we must not
	     substitute an expression containing the clobbered register.
	     As we do not analyze the constraint strings here, we have to
	     make the conservative assumption.  However, if the register is
	     a fixed hard reg, the clobber cannot represent any operand;
	     we leave it up to the machine description to either accept or
	     reject use-and-clobber patterns.  */
	  if (!REG_P (reg)
	      || REGNO (reg) >= FIRST_PSEUDO_REGISTER
	      || !fixed_regs[REGNO (reg)])
	    if (reg_overlap_mentioned_p (reg, src))
	      return 0;
	}

  /* If INSN contains anything volatile, or is an `asm' (whether volatile
     or not), reject, unless nothing volatile comes between it and I3 */

  if (GET_CODE (src) == ASM_OPERANDS || volatile_refs_p (src))
    {
      /* Make sure succ doesn't contain a volatile reference.  */
      if (succ != 0 && volatile_refs_p (PATTERN (succ)))
	return 0;

      for (p = NEXT_INSN (insn); p != i3; p = NEXT_INSN (p))
	if (INSN_P (p) && p != succ && volatile_refs_p (PATTERN (p)))
	  return 0;
    }

  /* If INSN is an asm, and DEST is a hard register, reject, since it has
     to be an explicit register variable, and was chosen for a reason.  */

  if (GET_CODE (src) == ASM_OPERANDS
      && REG_P (dest) && REGNO (dest) < FIRST_PSEUDO_REGISTER)
    return 0;

  /* If there are any volatile insns between INSN and I3, reject, because
     they might affect machine state.  */

  for (p = NEXT_INSN (insn); p != i3; p = NEXT_INSN (p))
    if (INSN_P (p) && p != succ && volatile_insn_p (PATTERN (p)))
      return 0;

  /* If INSN contains an autoincrement or autodecrement, make sure that
     register is not used between there and I3, and not already used in
     I3 either.  Neither must it be used in PRED or SUCC, if they exist.
     Also insist that I3 not be a jump; if it were one
     and the incremented register were spilled, we would lose.  */

#ifdef AUTO_INC_DEC
  for (link = REG_NOTES (insn); link; link = XEXP (link, 1))
    if (REG_NOTE_KIND (link) == REG_INC
	&& (JUMP_P (i3)
	    || reg_used_between_p (XEXP (link, 0), insn, i3)
	    || (pred != NULL_RTX
		&& reg_overlap_mentioned_p (XEXP (link, 0), PATTERN (pred)))
	    || (succ != NULL_RTX
		&& reg_overlap_mentioned_p (XEXP (link, 0), PATTERN (succ)))
	    || reg_overlap_mentioned_p (XEXP (link, 0), PATTERN (i3))))
      return 0;
#endif

#ifdef HAVE_cc0
  /* Don't combine an insn that follows a CC0-setting insn.
     An insn that uses CC0 must not be separated from the one that sets it.
     We do, however, allow I2 to follow a CC0-setting insn if that insn
     is passed as I1; in that case it will be deleted also.
     We also allow combining in this case if all the insns are adjacent
     because that would leave the two CC0 insns adjacent as well.
     It would be more logical to test whether CC0 occurs inside I1 or I2,
     but that would be much slower, and this ought to be equivalent.  */

  p = prev_nonnote_insn (insn);
  if (p && p != pred && NONJUMP_INSN_P (p) && sets_cc0_p (PATTERN (p))
      && ! all_adjacent)
    return 0;
#endif

  /* If we get here, we have passed all the tests and the combination is
     to be allowed.  */

  *pdest = dest;
  *psrc = src;

  return 1;
}

/* LOC is the location within I3 that contains its pattern or the component
   of a PARALLEL of the pattern.  We validate that it is valid for combining.

   One problem is if I3 modifies its output, as opposed to replacing it
   entirely, we can't allow the output to contain I2DEST or I1DEST as doing
   so would produce an insn that is not equivalent to the original insns.

   Consider:

	 (set (reg:DI 101) (reg:DI 100))
	 (set (subreg:SI (reg:DI 101) 0) <foo>)

   This is NOT equivalent to:

	 (parallel [(set (subreg:SI (reg:DI 100) 0) <foo>)
		    (set (reg:DI 101) (reg:DI 100))])

   Not only does this modify 100 (in which case it might still be valid
   if 100 were dead in I2), it sets 101 to the ORIGINAL value of 100.

   We can also run into a problem if I2 sets a register that I1
   uses and I1 gets directly substituted into I3 (not via I2).  In that
   case, we would be getting the wrong value of I2DEST into I3, so we
   must reject the combination.  This case occurs when I2 and I1 both
   feed into I3, rather than when I1 feeds into I2, which feeds into I3.
   If I1_NOT_IN_SRC is nonzero, it means that finding I1 in the source
   of a SET must prevent combination from occurring.

   Before doing the above check, we first try to expand a field assignment
   into a set of logical operations.

   If PI3_DEST_KILLED is nonzero, it is a pointer to a location in which
   we place a register that is both set and used within I3.  If more than one
   such register is detected, we fail.

   Return 1 if the combination is valid, zero otherwise.  */

static int
combinable_i3pat (rtx i3, rtx *loc, rtx i2dest, rtx i1dest,
		  int i1_not_in_src, rtx *pi3dest_killed)
{
  rtx x = *loc;

  if (GET_CODE (x) == SET)
    {
      rtx set = x ;
      rtx dest = SET_DEST (set);
      rtx src = SET_SRC (set);
      rtx inner_dest = dest;
      rtx subdest;

      while (GET_CODE (inner_dest) == STRICT_LOW_PART
	     || GET_CODE (inner_dest) == SUBREG
	     || GET_CODE (inner_dest) == ZERO_EXTRACT)
	inner_dest = XEXP (inner_dest, 0);

      /* Check for the case where I3 modifies its output, as discussed
	 above.  We don't want to prevent pseudos from being combined
	 into the address of a MEM, so only prevent the combination if
	 i1 or i2 set the same MEM.  */
      if ((inner_dest != dest &&
	   (!MEM_P (inner_dest)
	    || rtx_equal_p (i2dest, inner_dest)
	    || (i1dest && rtx_equal_p (i1dest, inner_dest)))
	   && (reg_overlap_mentioned_p (i2dest, inner_dest)
	       || (i1dest && reg_overlap_mentioned_p (i1dest, inner_dest))))

	  /* This is the same test done in can_combine_p except we can't test
	     all_adjacent; we don't have to, since this instruction will stay
	     in place, thus we are not considering increasing the lifetime of
	     INNER_DEST.

	     Also, if this insn sets a function argument, combining it with
	     something that might need a spill could clobber a previous
	     function argument; the all_adjacent test in can_combine_p also
	     checks this; here, we do a more specific test for this case.  */

	  || (REG_P (inner_dest)
	      && REGNO (inner_dest) < FIRST_PSEUDO_REGISTER
	      && (! HARD_REGNO_MODE_OK (REGNO (inner_dest),
					GET_MODE (inner_dest))))
	  || (i1_not_in_src && reg_overlap_mentioned_p (i1dest, src)))
	return 0;

      /* If DEST is used in I3, it is being killed in this insn, so
	 record that for later.  We have to consider paradoxical
	 subregs here, since they kill the whole register, but we
	 ignore partial subregs, STRICT_LOW_PART, etc.
	 Never add REG_DEAD notes for the FRAME_POINTER_REGNUM or the
	 STACK_POINTER_REGNUM, since these are always considered to be
	 live.  Similarly for ARG_POINTER_REGNUM if it is fixed.  */
      subdest = dest;
      if (GET_CODE (subdest) == SUBREG
	  && (GET_MODE_SIZE (GET_MODE (subdest))
	      >= GET_MODE_SIZE (GET_MODE (SUBREG_REG (subdest)))))
	subdest = SUBREG_REG (subdest);
      if (pi3dest_killed
	  && REG_P (subdest)
	  && reg_referenced_p (subdest, PATTERN (i3))
	  && REGNO (subdest) != FRAME_POINTER_REGNUM
#if HARD_FRAME_POINTER_REGNUM != FRAME_POINTER_REGNUM
	  && REGNO (subdest) != HARD_FRAME_POINTER_REGNUM
#endif
#if ARG_POINTER_REGNUM != FRAME_POINTER_REGNUM
	  && (REGNO (subdest) != ARG_POINTER_REGNUM
	      || ! fixed_regs [REGNO (subdest)])
#endif
	  && REGNO (subdest) != STACK_POINTER_REGNUM)
	{
	  if (*pi3dest_killed)
	    return 0;

	  *pi3dest_killed = subdest;
	}
    }

  else if (GET_CODE (x) == PARALLEL)
    {
      int i;

      for (i = 0; i < XVECLEN (x, 0); i++)
	if (! combinable_i3pat (i3, &XVECEXP (x, 0, i), i2dest, i1dest,
				i1_not_in_src, pi3dest_killed))
	  return 0;
    }

  return 1;
}

/* Return 1 if X is an arithmetic expression that contains a multiplication
   and division.  We don't count multiplications by powers of two here.  */

static int
contains_muldiv (rtx x)
{
  switch (GET_CODE (x))
    {
    case MOD:  case DIV:  case UMOD:  case UDIV:
      return 1;

    case MULT:
      return ! (GET_CODE (XEXP (x, 1)) == CONST_INT
		&& exact_log2 (INTVAL (XEXP (x, 1))) >= 0);
    default:
      if (BINARY_P (x))
	return contains_muldiv (XEXP (x, 0))
	    || contains_muldiv (XEXP (x, 1));

      if (UNARY_P (x))
	return contains_muldiv (XEXP (x, 0));

      return 0;
    }
}

/* Determine whether INSN can be used in a combination.  Return nonzero if
   not.  This is used in try_combine to detect early some cases where we
   can't perform combinations.  */

static int
cant_combine_insn_p (rtx insn)
{
  rtx set;
  rtx src, dest;

  /* If this isn't really an insn, we can't do anything.
     This can occur when flow deletes an insn that it has merged into an
     auto-increment address.  */
  if (! INSN_P (insn))
    return 1;

  /* Never combine loads and stores involving hard regs that are likely
     to be spilled.  The register allocator can usually handle such
     reg-reg moves by tying.  If we allow the combiner to make
     substitutions of likely-spilled regs, reload might die.
     As an exception, we allow combinations involving fixed regs; these are
     not available to the register allocator so there's no risk involved.  */

  set = single_set (insn);
  if (! set)
    return 0;
  src = SET_SRC (set);
  dest = SET_DEST (set);
  if (GET_CODE (src) == SUBREG)
    src = SUBREG_REG (src);
  if (GET_CODE (dest) == SUBREG)
    dest = SUBREG_REG (dest);
  if (REG_P (src) && REG_P (dest)
      && ((REGNO (src) < FIRST_PSEUDO_REGISTER
	   && ! fixed_regs[REGNO (src)]
	   && CLASS_LIKELY_SPILLED_P (REGNO_REG_CLASS (REGNO (src))))
	  || (REGNO (dest) < FIRST_PSEUDO_REGISTER
	      && ! fixed_regs[REGNO (dest)]
	      && CLASS_LIKELY_SPILLED_P (REGNO_REG_CLASS (REGNO (dest))))))
    return 1;

  return 0;
}

struct likely_spilled_retval_info
{
  unsigned regno, nregs;
  unsigned mask;
};

/* Called via note_stores by likely_spilled_retval_p.  Remove from info->mask
   hard registers that are known to be written to / clobbered in full.  */
static void
likely_spilled_retval_1 (rtx x, rtx set, void *data)
{
  struct likely_spilled_retval_info *info = data;
  unsigned regno, nregs;
  unsigned new_mask;

  if (!REG_P (XEXP (set, 0)))
    return;
  regno = REGNO (x);
  if (regno >= info->regno + info->nregs)
    return;
  nregs = hard_regno_nregs[regno][GET_MODE (x)];
  if (regno + nregs <= info->regno)
    return;
  new_mask = (2U << (nregs - 1)) - 1;
  if (regno < info->regno)
    new_mask >>= info->regno - regno;
  else
    new_mask <<= regno - info->regno;
  info->mask &= new_mask;
}

/* Return nonzero iff part of the return value is live during INSN, and
   it is likely spilled.  This can happen when more than one insn is needed
   to copy the return value, e.g. when we consider to combine into the
   second copy insn for a complex value.  */

static int
likely_spilled_retval_p (rtx insn)
{
  rtx use = BB_END (this_basic_block);
  rtx reg, p;
  unsigned regno, nregs;
  /* We assume here that no machine mode needs more than
     32 hard registers when the value overlaps with a register
     for which FUNCTION_VALUE_REGNO_P is true.  */
  unsigned mask;
  struct likely_spilled_retval_info info;

  if (!NONJUMP_INSN_P (use) || GET_CODE (PATTERN (use)) != USE || insn == use)
    return 0;
  reg = XEXP (PATTERN (use), 0);
  if (!REG_P (reg) || !FUNCTION_VALUE_REGNO_P (REGNO (reg)))
    return 0;
  regno = REGNO (reg);
  nregs = hard_regno_nregs[regno][GET_MODE (reg)];
  if (nregs == 1)
    return 0;
  mask = (2U << (nregs - 1)) - 1;

  /* Disregard parts of the return value that are set later.  */
  info.regno = regno;
  info.nregs = nregs;
  info.mask = mask;
  for (p = PREV_INSN (use); info.mask && p != insn; p = PREV_INSN (p))
    note_stores (PATTERN (insn), likely_spilled_retval_1, &info);
  mask = info.mask;

  /* Check if any of the (probably) live return value registers is
     likely spilled.  */
  nregs --;
  do
    {
      if ((mask & 1 << nregs)
	  && CLASS_LIKELY_SPILLED_P (REGNO_REG_CLASS (regno + nregs)))
	return 1;
    } while (nregs--);
  return 0;
}

/* Adjust INSN after we made a change to its destination.

   Changing the destination can invalidate notes that say something about
   the results of the insn and a LOG_LINK pointing to the insn.  */

static void
adjust_for_new_dest (rtx insn)
{
  rtx *loc;

  /* For notes, be conservative and simply remove them.  */
  loc = &REG_NOTES (insn);
  while (*loc)
    {
      enum reg_note kind = REG_NOTE_KIND (*loc);
      if (kind == REG_EQUAL || kind == REG_EQUIV)
	*loc = XEXP (*loc, 1);
      else
	loc = &XEXP (*loc, 1);
    }

  /* The new insn will have a destination that was previously the destination
     of an insn just above it.  Call distribute_links to make a LOG_LINK from
     the next use of that destination.  */
  distribute_links (gen_rtx_INSN_LIST (VOIDmode, insn, NULL_RTX));
}

/* Return TRUE if combine can reuse reg X in mode MODE.
   ADDED_SETS is nonzero if the original set is still required.  */
static bool
can_change_dest_mode (rtx x, int added_sets, enum machine_mode mode)
{
  unsigned int regno;

  if (!REG_P(x))
    return false;

  regno = REGNO (x);
  /* Allow hard registers if the new mode is legal, and occupies no more
     registers than the old mode.  */
  if (regno < FIRST_PSEUDO_REGISTER)
    return (HARD_REGNO_MODE_OK (regno, mode)
	    && (hard_regno_nregs[regno][GET_MODE (x)]
		>= hard_regno_nregs[regno][mode]));

  /* Or a pseudo that is only used once.  */
  return (REG_N_SETS (regno) == 1 && !added_sets
	  && !REG_USERVAR_P (x));
}


/* Check whether X, the destination of a set, refers to part of
   the register specified by REG.  */

static bool
reg_subword_p (rtx x, rtx reg)
{
  /* Check that reg is an integer mode register.  */
  if (!REG_P (reg) || GET_MODE_CLASS (GET_MODE (reg)) != MODE_INT)
    return false;

  if (GET_CODE (x) == STRICT_LOW_PART
      || GET_CODE (x) == ZERO_EXTRACT)
    x = XEXP (x, 0);

  return GET_CODE (x) == SUBREG
	 && SUBREG_REG (x) == reg
	 && GET_MODE_CLASS (GET_MODE (x)) == MODE_INT;
}


/* Try to combine the insns I1 and I2 into I3.
   Here I1 and I2 appear earlier than I3.
   I1 can be zero; then we combine just I2 into I3.

   If we are combining three insns and the resulting insn is not recognized,
   try splitting it into two insns.  If that happens, I2 and I3 are retained
   and I1 is pseudo-deleted by turning it into a NOTE.  Otherwise, I1 and I2
   are pseudo-deleted.

   Return 0 if the combination does not work.  Then nothing is changed.
   If we did the combination, return the insn at which combine should
   resume scanning.

   Set NEW_DIRECT_JUMP_P to a nonzero value if try_combine creates a
   new direct jump instruction.  */

static rtx
try_combine (rtx i3, rtx i2, rtx i1, int *new_direct_jump_p)
{
  /* New patterns for I3 and I2, respectively.  */
  rtx newpat, newi2pat = 0;
  rtvec newpat_vec_with_clobbers = 0;
  int substed_i2 = 0, substed_i1 = 0;
  /* Indicates need to preserve SET in I1 or I2 in I3 if it is not dead.  */
  int added_sets_1, added_sets_2;
  /* Total number of SETs to put into I3.  */
  int total_sets;
  /* Nonzero if I2's body now appears in I3.  */
  int i2_is_used;
  /* INSN_CODEs for new I3, new I2, and user of condition code.  */
  int insn_code_number, i2_code_number = 0, other_code_number = 0;
  /* Contains I3 if the destination of I3 is used in its source, which means
     that the old life of I3 is being killed.  If that usage is placed into
     I2 and not in I3, a REG_DEAD note must be made.  */
  rtx i3dest_killed = 0;
  /* SET_DEST and SET_SRC of I2 and I1.  */
  rtx i2dest, i2src, i1dest = 0, i1src = 0;
  /* PATTERN (I1) and PATTERN (I2), or a copy of it in certain cases.  */
  rtx i1pat = 0, i2pat = 0;
  /* Indicates if I2DEST or I1DEST is in I2SRC or I1_SRC.  */
  int i2dest_in_i2src = 0, i1dest_in_i1src = 0, i2dest_in_i1src = 0;
  int i2dest_killed = 0, i1dest_killed = 0;
  int i1_feeds_i3 = 0;
  /* Notes that must be added to REG_NOTES in I3 and I2.  */
  rtx new_i3_notes, new_i2_notes;
  /* Notes that we substituted I3 into I2 instead of the normal case.  */
  int i3_subst_into_i2 = 0;
  /* Notes that I1, I2 or I3 is a MULT operation.  */
  int have_mult = 0;
  int swap_i2i3 = 0;

  int maxreg;
  rtx temp;
  rtx link;
  int i;

  /* Exit early if one of the insns involved can't be used for
     combinations.  */
  if (cant_combine_insn_p (i3)
      || cant_combine_insn_p (i2)
      || (i1 && cant_combine_insn_p (i1))
      || likely_spilled_retval_p (i3)
      /* We also can't do anything if I3 has a
	 REG_LIBCALL note since we don't want to disrupt the contiguity of a
	 libcall.  */
#if 0
      /* ??? This gives worse code, and appears to be unnecessary, since no
	 pass after flow uses REG_LIBCALL/REG_RETVAL notes.  */
      || find_reg_note (i3, REG_LIBCALL, NULL_RTX)
#endif
      )
    return 0;

  combine_attempts++;
  undobuf.other_insn = 0;

  /* Reset the hard register usage information.  */
  CLEAR_HARD_REG_SET (newpat_used_regs);

  /* If I1 and I2 both feed I3, they can be in any order.  To simplify the
     code below, set I1 to be the earlier of the two insns.  */
  if (i1 && INSN_CUID (i1) > INSN_CUID (i2))
    temp = i1, i1 = i2, i2 = temp;

  added_links_insn = 0;

  /* First check for one important special-case that the code below will
     not handle.  Namely, the case where I1 is zero, I2 is a PARALLEL
     and I3 is a SET whose SET_SRC is a SET_DEST in I2.  In that case,
     we may be able to replace that destination with the destination of I3.
     This occurs in the common code where we compute both a quotient and
     remainder into a structure, in which case we want to do the computation
     directly into the structure to avoid register-register copies.

     Note that this case handles both multiple sets in I2 and also
     cases where I2 has a number of CLOBBER or PARALLELs.

     We make very conservative checks below and only try to handle the
     most common cases of this.  For example, we only handle the case
     where I2 and I3 are adjacent to avoid making difficult register
     usage tests.  */

  if (i1 == 0 && NONJUMP_INSN_P (i3) && GET_CODE (PATTERN (i3)) == SET
      && REG_P (SET_SRC (PATTERN (i3)))
      && REGNO (SET_SRC (PATTERN (i3))) >= FIRST_PSEUDO_REGISTER
      && find_reg_note (i3, REG_DEAD, SET_SRC (PATTERN (i3)))
      && GET_CODE (PATTERN (i2)) == PARALLEL
      && ! side_effects_p (SET_DEST (PATTERN (i3)))
      /* If the dest of I3 is a ZERO_EXTRACT or STRICT_LOW_PART, the code
	 below would need to check what is inside (and reg_overlap_mentioned_p
	 doesn't support those codes anyway).  Don't allow those destinations;
	 the resulting insn isn't likely to be recognized anyway.  */
      && GET_CODE (SET_DEST (PATTERN (i3))) != ZERO_EXTRACT
      && GET_CODE (SET_DEST (PATTERN (i3))) != STRICT_LOW_PART
      && ! reg_overlap_mentioned_p (SET_SRC (PATTERN (i3)),
				    SET_DEST (PATTERN (i3)))
      && next_real_insn (i2) == i3)
    {
      rtx p2 = PATTERN (i2);

      /* Make sure that the destination of I3,
	 which we are going to substitute into one output of I2,
	 is not used within another output of I2.  We must avoid making this:
	 (parallel [(set (mem (reg 69)) ...)
		    (set (reg 69) ...)])
	 which is not well-defined as to order of actions.
	 (Besides, reload can't handle output reloads for this.)

	 The problem can also happen if the dest of I3 is a memory ref,
	 if another dest in I2 is an indirect memory ref.  */
      for (i = 0; i < XVECLEN (p2, 0); i++)
	if ((GET_CODE (XVECEXP (p2, 0, i)) == SET
	     || GET_CODE (XVECEXP (p2, 0, i)) == CLOBBER)
	    && reg_overlap_mentioned_p (SET_DEST (PATTERN (i3)),
					SET_DEST (XVECEXP (p2, 0, i))))
	  break;

      if (i == XVECLEN (p2, 0))
	for (i = 0; i < XVECLEN (p2, 0); i++)
	  if ((GET_CODE (XVECEXP (p2, 0, i)) == SET
	       || GET_CODE (XVECEXP (p2, 0, i)) == CLOBBER)
	      && SET_DEST (XVECEXP (p2, 0, i)) == SET_SRC (PATTERN (i3)))
	    {
	      combine_merges++;

	      subst_insn = i3;
	      subst_low_cuid = INSN_CUID (i2);

	      added_sets_2 = added_sets_1 = 0;
	      i2dest = SET_SRC (PATTERN (i3));
	      i2dest_killed = dead_or_set_p (i2, i2dest);

	      /* Replace the dest in I2 with our dest and make the resulting
		 insn the new pattern for I3.  Then skip to where we
		 validate the pattern.  Everything was set up above.  */
	      SUBST (SET_DEST (XVECEXP (p2, 0, i)),
		     SET_DEST (PATTERN (i3)));

	      newpat = p2;
	      i3_subst_into_i2 = 1;
	      goto validate_replacement;
	    }
    }

  /* If I2 is setting a pseudo to a constant and I3 is setting some
     sub-part of it to another constant, merge them by making a new
     constant.  */
  if (i1 == 0
      && (temp = single_set (i2)) != 0
      && (GET_CODE (SET_SRC (temp)) == CONST_INT
	  || GET_CODE (SET_SRC (temp)) == CONST_DOUBLE)
      && GET_CODE (PATTERN (i3)) == SET
      && (GET_CODE (SET_SRC (PATTERN (i3))) == CONST_INT
	  || GET_CODE (SET_SRC (PATTERN (i3))) == CONST_DOUBLE)
      && reg_subword_p (SET_DEST (PATTERN (i3)), SET_DEST (temp)))
    {
      rtx dest = SET_DEST (PATTERN (i3));
      int offset = -1;
      int width = 0;

      if (GET_CODE (dest) == ZERO_EXTRACT)
	{
	  if (GET_CODE (XEXP (dest, 1)) == CONST_INT
	      && GET_CODE (XEXP (dest, 2)) == CONST_INT)
	    {
	      width = INTVAL (XEXP (dest, 1));
	      offset = INTVAL (XEXP (dest, 2));
	      dest = XEXP (dest, 0);
	      if (BITS_BIG_ENDIAN)
		offset = GET_MODE_BITSIZE (GET_MODE (dest)) - width - offset;
	    }
	}
      else
	{
	  if (GET_CODE (dest) == STRICT_LOW_PART)
	    dest = XEXP (dest, 0);
	  width = GET_MODE_BITSIZE (GET_MODE (dest));
	  offset = 0;
	}

      if (offset >= 0)
	{
	  /* If this is the low part, we're done.  */
	  if (subreg_lowpart_p (dest))
	    ;
	  /* Handle the case where inner is twice the size of outer.  */
	  else if (GET_MODE_BITSIZE (GET_MODE (SET_DEST (temp)))
		   == 2 * GET_MODE_BITSIZE (GET_MODE (dest)))
	    offset += GET_MODE_BITSIZE (GET_MODE (dest));
	  /* Otherwise give up for now.  */
	  else
	    offset = -1;
	}

      if (offset >= 0)
	{
	  HOST_WIDE_INT mhi, ohi, ihi;
	  HOST_WIDE_INT mlo, olo, ilo;
	  rtx inner = SET_SRC (PATTERN (i3));
	  rtx outer = SET_SRC (temp);

	  if (GET_CODE (outer) == CONST_INT)
	    {
	      olo = INTVAL (outer);
	      ohi = olo < 0 ? -1 : 0;
	    }
	  else
	    {
	      olo = CONST_DOUBLE_LOW (outer);
	      ohi = CONST_DOUBLE_HIGH (outer);
	    }

	  if (GET_CODE (inner) == CONST_INT)
	    {
	      ilo = INTVAL (inner);
	      ihi = ilo < 0 ? -1 : 0;
	    }
	  else
	    {
	      ilo = CONST_DOUBLE_LOW (inner);
	      ihi = CONST_DOUBLE_HIGH (inner);
	    }

	  if (width < HOST_BITS_PER_WIDE_INT)
	    {
	      mlo = ((unsigned HOST_WIDE_INT) 1 << width) - 1;
	      mhi = 0;
	    }
	  else if (width < HOST_BITS_PER_WIDE_INT * 2)
	    {
	      mhi = ((unsigned HOST_WIDE_INT) 1
		     << (width - HOST_BITS_PER_WIDE_INT)) - 1;
	      mlo = -1;
	    }
	  else
	    {
	      mlo = -1;
	      mhi = -1;
	    }

	  ilo &= mlo;
	  ihi &= mhi;

	  if (offset >= HOST_BITS_PER_WIDE_INT)
	    {
	      mhi = mlo << (offset - HOST_BITS_PER_WIDE_INT);
	      mlo = 0;
	      ihi = ilo << (offset - HOST_BITS_PER_WIDE_INT);
	      ilo = 0;
	    }
	  else if (offset > 0)
	    {
	      mhi = (mhi << offset) | ((unsigned HOST_WIDE_INT) mlo
		     		       >> (HOST_BITS_PER_WIDE_INT - offset));
	      mlo = mlo << offset;
	      ihi = (ihi << offset) | ((unsigned HOST_WIDE_INT) ilo
		     		       >> (HOST_BITS_PER_WIDE_INT - offset));
	      ilo = ilo << offset;
	    }

	  olo = (olo & ~mlo) | ilo;
	  ohi = (ohi & ~mhi) | ihi;

	  combine_merges++;
	  subst_insn = i3;
	  subst_low_cuid = INSN_CUID (i2);
	  added_sets_2 = added_sets_1 = 0;
	  i2dest = SET_DEST (temp);
	  i2dest_killed = dead_or_set_p (i2, i2dest);

	  SUBST (SET_SRC (temp),
		 immed_double_const (olo, ohi, GET_MODE (SET_DEST (temp))));

	  newpat = PATTERN (i2);
	  goto validate_replacement;
	}
    }

#ifndef HAVE_cc0
  /* If we have no I1 and I2 looks like:
	(parallel [(set (reg:CC X) (compare:CC OP (const_int 0)))
		   (set Y OP)])
     make up a dummy I1 that is
	(set Y OP)
     and change I2 to be
	(set (reg:CC X) (compare:CC Y (const_int 0)))

     (We can ignore any trailing CLOBBERs.)

     This undoes a previous combination and allows us to match a branch-and-
     decrement insn.  */

  if (i1 == 0 && GET_CODE (PATTERN (i2)) == PARALLEL
      && XVECLEN (PATTERN (i2), 0) >= 2
      && GET_CODE (XVECEXP (PATTERN (i2), 0, 0)) == SET
      && (GET_MODE_CLASS (GET_MODE (SET_DEST (XVECEXP (PATTERN (i2), 0, 0))))
	  == MODE_CC)
      && GET_CODE (SET_SRC (XVECEXP (PATTERN (i2), 0, 0))) == COMPARE
      && XEXP (SET_SRC (XVECEXP (PATTERN (i2), 0, 0)), 1) == const0_rtx
      && GET_CODE (XVECEXP (PATTERN (i2), 0, 1)) == SET
      && REG_P (SET_DEST (XVECEXP (PATTERN (i2), 0, 1)))
      && rtx_equal_p (XEXP (SET_SRC (XVECEXP (PATTERN (i2), 0, 0)), 0),
		      SET_SRC (XVECEXP (PATTERN (i2), 0, 1))))
    {
      for (i = XVECLEN (PATTERN (i2), 0) - 1; i >= 2; i--)
	if (GET_CODE (XVECEXP (PATTERN (i2), 0, i)) != CLOBBER)
	  break;

      if (i == 1)
	{
	  /* We make I1 with the same INSN_UID as I2.  This gives it
	     the same INSN_CUID for value tracking.  Our fake I1 will
	     never appear in the insn stream so giving it the same INSN_UID
	     as I2 will not cause a problem.  */

	  i1 = gen_rtx_INSN (VOIDmode, INSN_UID (i2), NULL_RTX, i2,
			     BLOCK_FOR_INSN (i2), INSN_LOCATOR (i2),
			     XVECEXP (PATTERN (i2), 0, 1), -1, NULL_RTX,
			     NULL_RTX);

	  SUBST (PATTERN (i2), XVECEXP (PATTERN (i2), 0, 0));
	  SUBST (XEXP (SET_SRC (PATTERN (i2)), 0),
		 SET_DEST (PATTERN (i1)));
	}
    }
#endif

  /* Verify that I2 and I1 are valid for combining.  */
  if (! can_combine_p (i2, i3, i1, NULL_RTX, &i2dest, &i2src)
      || (i1 && ! can_combine_p (i1, i3, NULL_RTX, i2, &i1dest, &i1src)))
    {
      undo_all ();
      return 0;
    }

  /* Record whether I2DEST is used in I2SRC and similarly for the other
     cases.  Knowing this will help in register status updating below.  */
  i2dest_in_i2src = reg_overlap_mentioned_p (i2dest, i2src);
  i1dest_in_i1src = i1 && reg_overlap_mentioned_p (i1dest, i1src);
  i2dest_in_i1src = i1 && reg_overlap_mentioned_p (i2dest, i1src);
  i2dest_killed = dead_or_set_p (i2, i2dest);
  i1dest_killed = i1 && dead_or_set_p (i1, i1dest);

  /* See if I1 directly feeds into I3.  It does if I1DEST is not used
     in I2SRC.  */
  i1_feeds_i3 = i1 && ! reg_overlap_mentioned_p (i1dest, i2src);

  /* Ensure that I3's pattern can be the destination of combines.  */
  if (! combinable_i3pat (i3, &PATTERN (i3), i2dest, i1dest,
			  i1 && i2dest_in_i1src && i1_feeds_i3,
			  &i3dest_killed))
    {
      undo_all ();
      return 0;
    }

  /* See if any of the insns is a MULT operation.  Unless one is, we will
     reject a combination that is, since it must be slower.  Be conservative
     here.  */
  if (GET_CODE (i2src) == MULT
      || (i1 != 0 && GET_CODE (i1src) == MULT)
      || (GET_CODE (PATTERN (i3)) == SET
	  && GET_CODE (SET_SRC (PATTERN (i3))) == MULT))
    have_mult = 1;

  /* If I3 has an inc, then give up if I1 or I2 uses the reg that is inc'd.
     We used to do this EXCEPT in one case: I3 has a post-inc in an
     output operand.  However, that exception can give rise to insns like
	mov r3,(r3)+
     which is a famous insn on the PDP-11 where the value of r3 used as the
     source was model-dependent.  Avoid this sort of thing.  */

#if 0
  if (!(GET_CODE (PATTERN (i3)) == SET
	&& REG_P (SET_SRC (PATTERN (i3)))
	&& MEM_P (SET_DEST (PATTERN (i3)))
	&& (GET_CODE (XEXP (SET_DEST (PATTERN (i3)), 0)) == POST_INC
	    || GET_CODE (XEXP (SET_DEST (PATTERN (i3)), 0)) == POST_DEC)))
    /* It's not the exception.  */
#endif
#ifdef AUTO_INC_DEC
    for (link = REG_NOTES (i3); link; link = XEXP (link, 1))
      if (REG_NOTE_KIND (link) == REG_INC
	  && (reg_overlap_mentioned_p (XEXP (link, 0), PATTERN (i2))
	      || (i1 != 0
		  && reg_overlap_mentioned_p (XEXP (link, 0), PATTERN (i1)))))
	{
	  undo_all ();
	  return 0;
	}
#endif

  /* See if the SETs in I1 or I2 need to be kept around in the merged
     instruction: whenever the value set there is still needed past I3.
     For the SETs in I2, this is easy: we see if I2DEST dies or is set in I3.

     For the SET in I1, we have two cases:  If I1 and I2 independently
     feed into I3, the set in I1 needs to be kept around if I1DEST dies
     or is set in I3.  Otherwise (if I1 feeds I2 which feeds I3), the set
     in I1 needs to be kept around unless I1DEST dies or is set in either
     I2 or I3.  We can distinguish these cases by seeing if I2SRC mentions
     I1DEST.  If so, we know I1 feeds into I2.  */

  added_sets_2 = ! dead_or_set_p (i3, i2dest);

  added_sets_1
    = i1 && ! (i1_feeds_i3 ? dead_or_set_p (i3, i1dest)
	       : (dead_or_set_p (i3, i1dest) || dead_or_set_p (i2, i1dest)));

  /* If the set in I2 needs to be kept around, we must make a copy of
     PATTERN (I2), so that when we substitute I1SRC for I1DEST in
     PATTERN (I2), we are only substituting for the original I1DEST, not into
     an already-substituted copy.  This also prevents making self-referential
     rtx.  If I2 is a PARALLEL, we just need the piece that assigns I2SRC to
     I2DEST.  */

  if (added_sets_2)
    {
      if (GET_CODE (PATTERN (i2)) == PARALLEL)
	i2pat = gen_rtx_SET (VOIDmode, i2dest, copy_rtx (i2src));
      else
	i2pat = copy_rtx (PATTERN (i2));
    }

  if (added_sets_1)
    {
      if (GET_CODE (PATTERN (i1)) == PARALLEL)
	i1pat = gen_rtx_SET (VOIDmode, i1dest, copy_rtx (i1src));
      else
	i1pat = copy_rtx (PATTERN (i1));
    }

  combine_merges++;

  /* Substitute in the latest insn for the regs set by the earlier ones.  */

  maxreg = max_reg_num ();

  subst_insn = i3;

#ifndef HAVE_cc0
  /* Many machines that don't use CC0 have insns that can both perform an
     arithmetic operation and set the condition code.  These operations will
     be represented as a PARALLEL with the first element of the vector
     being a COMPARE of an arithmetic operation with the constant zero.
     The second element of the vector will set some pseudo to the result
     of the same arithmetic operation.  If we simplify the COMPARE, we won't
     match such a pattern and so will generate an extra insn.   Here we test
     for this case, where both the comparison and the operation result are
     needed, and make the PARALLEL by just replacing I2DEST in I3SRC with
     I2SRC.  Later we will make the PARALLEL that contains I2.  */

  if (i1 == 0 && added_sets_2 && GET_CODE (PATTERN (i3)) == SET
      && GET_CODE (SET_SRC (PATTERN (i3))) == COMPARE
      && XEXP (SET_SRC (PATTERN (i3)), 1) == const0_rtx
      && rtx_equal_p (XEXP (SET_SRC (PATTERN (i3)), 0), i2dest))
    {
#ifdef SELECT_CC_MODE
      rtx *cc_use;
      enum machine_mode compare_mode;
#endif

      newpat = PATTERN (i3);
      SUBST (XEXP (SET_SRC (newpat), 0), i2src);

      i2_is_used = 1;

#ifdef SELECT_CC_MODE
      /* See if a COMPARE with the operand we substituted in should be done
	 with the mode that is currently being used.  If not, do the same
	 processing we do in `subst' for a SET; namely, if the destination
	 is used only once, try to replace it with a register of the proper
	 mode and also replace the COMPARE.  */
      if (undobuf.other_insn == 0
	  && (cc_use = find_single_use (SET_DEST (newpat), i3,
					&undobuf.other_insn))
	  && ((compare_mode = SELECT_CC_MODE (GET_CODE (*cc_use),
					      i2src, const0_rtx))
	      != GET_MODE (SET_DEST (newpat))))
	{
	  if (can_change_dest_mode(SET_DEST (newpat), added_sets_2,
				   compare_mode))
	    {
	      unsigned int regno = REGNO (SET_DEST (newpat));
	      rtx new_dest;

	      if (regno < FIRST_PSEUDO_REGISTER)
		new_dest = gen_rtx_REG (compare_mode, regno);
	      else
		{
		  SUBST_MODE (regno_reg_rtx[regno], compare_mode);
		  new_dest = regno_reg_rtx[regno];
		}

	      SUBST (SET_DEST (newpat), new_dest);
	      SUBST (XEXP (*cc_use, 0), new_dest);
	      SUBST (SET_SRC (newpat),
		     gen_rtx_COMPARE (compare_mode, i2src, const0_rtx));
	    }
	  else
	    undobuf.other_insn = 0;
	}
#endif
    }
  else
#endif
    {
      /* It is possible that the source of I2 or I1 may be performing
	 an unneeded operation, such as a ZERO_EXTEND of something
	 that is known to have the high part zero.  Handle that case
	 by letting subst look at the innermost one of them.

	 Another way to do this would be to have a function that tries
	 to simplify a single insn instead of merging two or more
	 insns.  We don't do this because of the potential of infinite
	 loops and because of the potential extra memory required.
	 However, doing it the way we are is a bit of a kludge and
	 doesn't catch all cases.

	 But only do this if -fexpensive-optimizations since it slows
	 things down and doesn't usually win.

	 This is not done in the COMPARE case above because the
	 unmodified I2PAT is used in the PARALLEL and so a pattern
	 with a modified I2SRC would not match.  */

      if (flag_expensive_optimizations)
	{
	  /* Pass pc_rtx so no substitutions are done, just
	     simplifications.  */
	  if (i1)
	    {
	      subst_low_cuid = INSN_CUID (i1);
	      i1src = subst (i1src, pc_rtx, pc_rtx, 0, 0);
	    }
	  else
	    {
	      subst_low_cuid = INSN_CUID (i2);
	      i2src = subst (i2src, pc_rtx, pc_rtx, 0, 0);
	    }
	}

      n_occurrences = 0;		/* `subst' counts here */

      /* If I1 feeds into I2 (not into I3) and I1DEST is in I1SRC, we
	 need to make a unique copy of I2SRC each time we substitute it
	 to avoid self-referential rtl.  */

      subst_low_cuid = INSN_CUID (i2);
      newpat = subst (PATTERN (i3), i2dest, i2src, 0,
		      ! i1_feeds_i3 && i1dest_in_i1src);
      substed_i2 = 1;

      /* Record whether i2's body now appears within i3's body.  */
      i2_is_used = n_occurrences;
    }

  /* If we already got a failure, don't try to do more.  Otherwise,
     try to substitute in I1 if we have it.  */

  if (i1 && GET_CODE (newpat) != CLOBBER)
    {
      /* Before we can do this substitution, we must redo the test done
	 above (see detailed comments there) that ensures  that I1DEST
	 isn't mentioned in any SETs in NEWPAT that are field assignments.  */

#if !defined(OPENBSD_NATIVE) && !defined(OPENBSD_CROSS) /* GCC PR #34628 */
      if (! combinable_i3pat (NULL_RTX, &newpat, i1dest, NULL_RTX,
			      0, (rtx*) 0))
#endif
	{
	  undo_all ();
	  return 0;
	}

      n_occurrences = 0;
      subst_low_cuid = INSN_CUID (i1);
      newpat = subst (newpat, i1dest, i1src, 0, 0);
      substed_i1 = 1;
    }

  /* Fail if an autoincrement side-effect has been duplicated.  Be careful
     to count all the ways that I2SRC and I1SRC can be used.  */
  if ((FIND_REG_INC_NOTE (i2, NULL_RTX) != 0
       && i2_is_used + added_sets_2 > 1)
      || (i1 != 0 && FIND_REG_INC_NOTE (i1, NULL_RTX) != 0
	  && (n_occurrences + added_sets_1 + (added_sets_2 && ! i1_feeds_i3)
	      > 1))
      /* Fail if we tried to make a new register.  */
      || max_reg_num () != maxreg
      /* Fail if we couldn't do something and have a CLOBBER.  */
      || GET_CODE (newpat) == CLOBBER
      /* Fail if this new pattern is a MULT and we didn't have one before
	 at the outer level.  */
      || (GET_CODE (newpat) == SET && GET_CODE (SET_SRC (newpat)) == MULT
	  && ! have_mult))
    {
      undo_all ();
      return 0;
    }

  /* If the actions of the earlier insns must be kept
     in addition to substituting them into the latest one,
     we must make a new PARALLEL for the latest insn
     to hold additional the SETs.  */

  if (added_sets_1 || added_sets_2)
    {
      combine_extras++;

      if (GET_CODE (newpat) == PARALLEL)
	{
	  rtvec old = XVEC (newpat, 0);
	  total_sets = XVECLEN (newpat, 0) + added_sets_1 + added_sets_2;
	  newpat = gen_rtx_PARALLEL (VOIDmode, rtvec_alloc (total_sets));
	  memcpy (XVEC (newpat, 0)->elem, &old->elem[0],
		  sizeof (old->elem[0]) * old->num_elem);
	}
      else
	{
	  rtx old = newpat;
	  total_sets = 1 + added_sets_1 + added_sets_2;
	  newpat = gen_rtx_PARALLEL (VOIDmode, rtvec_alloc (total_sets));
	  XVECEXP (newpat, 0, 0) = old;
	}

      if (added_sets_1)
	XVECEXP (newpat, 0, --total_sets) = i1pat;

      if (added_sets_2)
	{
	  /* If there is no I1, use I2's body as is.  We used to also not do
	     the subst call below if I2 was substituted into I3,
	     but that could lose a simplification.  */
	  if (i1 == 0)
	    XVECEXP (newpat, 0, --total_sets) = i2pat;
	  else
	    /* See comment where i2pat is assigned.  */
	    XVECEXP (newpat, 0, --total_sets)
	      = subst (i2pat, i1dest, i1src, 0, 0);
	}
    }

  /* We come here when we are replacing a destination in I2 with the
     destination of I3.  */
 validate_replacement:

  /* Note which hard regs this insn has as inputs.  */
  mark_used_regs_combine (newpat);

  /* If recog_for_combine fails, it strips existing clobbers.  If we'll
     consider splitting this pattern, we might need these clobbers.  */
  if (i1 && GET_CODE (newpat) == PARALLEL
      && GET_CODE (XVECEXP (newpat, 0, XVECLEN (newpat, 0) - 1)) == CLOBBER)
    {
      int len = XVECLEN (newpat, 0);

      newpat_vec_with_clobbers = rtvec_alloc (len);
      for (i = 0; i < len; i++)
	RTVEC_ELT (newpat_vec_with_clobbers, i) = XVECEXP (newpat, 0, i);
    }

  /* Is the result of combination a valid instruction?  */
  insn_code_number = recog_for_combine (&newpat, i3, &new_i3_notes);

  /* If the result isn't valid, see if it is a PARALLEL of two SETs where
     the second SET's destination is a register that is unused and isn't
     marked as an instruction that might trap in an EH region.  In that case,
     we just need the first SET.   This can occur when simplifying a divmod
     insn.  We *must* test for this case here because the code below that
     splits two independent SETs doesn't handle this case correctly when it
     updates the register status.

     It's pointless doing this if we originally had two sets, one from
     i3, and one from i2.  Combining then splitting the parallel results
     in the original i2 again plus an invalid insn (which we delete).
     The net effect is only to move instructions around, which makes
     debug info less accurate.

     Also check the case where the first SET's destination is unused.
     That would not cause incorrect code, but does cause an unneeded
     insn to remain.  */

  if (insn_code_number < 0
      && !(added_sets_2 && i1 == 0)
      && GET_CODE (newpat) == PARALLEL
      && XVECLEN (newpat, 0) == 2
      && GET_CODE (XVECEXP (newpat, 0, 0)) == SET
      && GET_CODE (XVECEXP (newpat, 0, 1)) == SET
      && asm_noperands (newpat) < 0)
    {
      rtx set0 = XVECEXP (newpat, 0, 0);
      rtx set1 = XVECEXP (newpat, 0, 1);
      rtx note;

      if (((REG_P (SET_DEST (set1))
	    && find_reg_note (i3, REG_UNUSED, SET_DEST (set1)))
	   || (GET_CODE (SET_DEST (set1)) == SUBREG
	       && find_reg_note (i3, REG_UNUSED, SUBREG_REG (SET_DEST (set1)))))
	  && (!(note = find_reg_note (i3, REG_EH_REGION, NULL_RTX))
	      || INTVAL (XEXP (note, 0)) <= 0)
	  && ! side_effects_p (SET_SRC (set1)))
	{
	  newpat = set0;
	  insn_code_number = recog_for_combine (&newpat, i3, &new_i3_notes);
	}

      else if (((REG_P (SET_DEST (set0))
		 && find_reg_note (i3, REG_UNUSED, SET_DEST (set0)))
		|| (GET_CODE (SET_DEST (set0)) == SUBREG
		    && find_reg_note (i3, REG_UNUSED,
				      SUBREG_REG (SET_DEST (set0)))))
	       && (!(note = find_reg_note (i3, REG_EH_REGION, NULL_RTX))
		   || INTVAL (XEXP (note, 0)) <= 0)
	       && ! side_effects_p (SET_SRC (set0)))
	{
	  newpat = set1;
	  insn_code_number = recog_for_combine (&newpat, i3, &new_i3_notes);

	  if (insn_code_number >= 0)
	    {
	      /* If we will be able to accept this, we have made a
		 change to the destination of I3.  This requires us to
		 do a few adjustments.  */

	      PATTERN (i3) = newpat;
	      adjust_for_new_dest (i3);
	    }
	}
    }

  /* If we were combining three insns and the result is a simple SET
     with no ASM_OPERANDS that wasn't recognized, try to split it into two
     insns.  There are two ways to do this.  It can be split using a
     machine-specific method (like when you have an addition of a large
     constant) or by combine in the function find_split_point.  */

  if (i1 && insn_code_number < 0 && GET_CODE (newpat) == SET
      && asm_noperands (newpat) < 0)
    {
      rtx m_split, *split;

      /* See if the MD file can split NEWPAT.  If it can't, see if letting it
	 use I2DEST as a scratch register will help.  In the latter case,
	 convert I2DEST to the mode of the source of NEWPAT if we can.  */

      m_split = split_insns (newpat, i3);

      /* We can only use I2DEST as a scratch reg if it doesn't overlap any
	 inputs of NEWPAT.  */

      /* ??? If I2DEST is not safe, and I1DEST exists, then it would be
	 possible to try that as a scratch reg.  This would require adding
	 more code to make it work though.  */

      if (m_split == 0 && ! reg_overlap_mentioned_p (i2dest, newpat))
	{
	  enum machine_mode new_mode = GET_MODE (SET_DEST (newpat));

	  /* First try to split using the original register as a
	     scratch register.  */
	  m_split = split_insns (gen_rtx_PARALLEL
				 (VOIDmode,
				  gen_rtvec (2, newpat,
					     gen_rtx_CLOBBER (VOIDmode,
							      i2dest))),
				 i3);

	  /* If that didn't work, try changing the mode of I2DEST if
	     we can.  */
	  if (m_split == 0
	      && new_mode != GET_MODE (i2dest)
	      && new_mode != VOIDmode
	      && can_change_dest_mode (i2dest, added_sets_2, new_mode))
	    {
	      enum machine_mode old_mode = GET_MODE (i2dest);
	      rtx ni2dest;

	      if (REGNO (i2dest) < FIRST_PSEUDO_REGISTER)
		ni2dest = gen_rtx_REG (new_mode, REGNO (i2dest));
	      else
		{
		  SUBST_MODE (regno_reg_rtx[REGNO (i2dest)], new_mode);
		  ni2dest = regno_reg_rtx[REGNO (i2dest)];
		}

	      m_split = split_insns (gen_rtx_PARALLEL
				     (VOIDmode,
				      gen_rtvec (2, newpat,
						 gen_rtx_CLOBBER (VOIDmode,
								  ni2dest))),
				     i3);

	      if (m_split == 0
		  && REGNO (i2dest) >= FIRST_PSEUDO_REGISTER)
		{
		  struct undo *buf;

		  PUT_MODE (regno_reg_rtx[REGNO (i2dest)], old_mode);
		  buf = undobuf.undos;
		  undobuf.undos = buf->next;
		  buf->next = undobuf.frees;
		  undobuf.frees = buf;
		}
	    }
	}

      /* If recog_for_combine has discarded clobbers, try to use them
	 again for the split.  */
      if (m_split == 0 && newpat_vec_with_clobbers)
	m_split
	  = split_insns (gen_rtx_PARALLEL (VOIDmode,
					   newpat_vec_with_clobbers), i3);

      if (m_split && NEXT_INSN (m_split) == NULL_RTX)
	{
	  m_split = PATTERN (m_split);
	  insn_code_number = recog_for_combine (&m_split, i3, &new_i3_notes);
	  if (insn_code_number >= 0)
	    newpat = m_split;
	}
      else if (m_split && NEXT_INSN (NEXT_INSN (m_split)) == NULL_RTX
	       && (next_real_insn (i2) == i3
		   || ! use_crosses_set_p (PATTERN (m_split), INSN_CUID (i2))))
	{
	  rtx i2set, i3set;
	  rtx newi3pat = PATTERN (NEXT_INSN (m_split));
	  newi2pat = PATTERN (m_split);

	  i3set = single_set (NEXT_INSN (m_split));
	  i2set = single_set (m_split);

	  i2_code_number = recog_for_combine (&newi2pat, i2, &new_i2_notes);

	  /* If I2 or I3 has multiple SETs, we won't know how to track
	     register status, so don't use these insns.  If I2's destination
	     is used between I2 and I3, we also can't use these insns.  */

	  if (i2_code_number >= 0 && i2set && i3set
	      && (next_real_insn (i2) == i3
		  || ! reg_used_between_p (SET_DEST (i2set), i2, i3)))
	    insn_code_number = recog_for_combine (&newi3pat, i3,
						  &new_i3_notes);
	  if (insn_code_number >= 0)
	    newpat = newi3pat;

	  /* It is possible that both insns now set the destination of I3.
	     If so, we must show an extra use of it.  */

	  if (insn_code_number >= 0)
	    {
	      rtx new_i3_dest = SET_DEST (i3set);
	      rtx new_i2_dest = SET_DEST (i2set);

	      while (GET_CODE (new_i3_dest) == ZERO_EXTRACT
		     || GET_CODE (new_i3_dest) == STRICT_LOW_PART
		     || GET_CODE (new_i3_dest) == SUBREG)
		new_i3_dest = XEXP (new_i3_dest, 0);

	      while (GET_CODE (new_i2_dest) == ZERO_EXTRACT
		     || GET_CODE (new_i2_dest) == STRICT_LOW_PART
		     || GET_CODE (new_i2_dest) == SUBREG)
		new_i2_dest = XEXP (new_i2_dest, 0);

	      if (REG_P (new_i3_dest)
		  && REG_P (new_i2_dest)
		  && REGNO (new_i3_dest) == REGNO (new_i2_dest))
		REG_N_SETS (REGNO (new_i2_dest))++;
	    }
	}

      /* If we can split it and use I2DEST, go ahead and see if that
	 helps things be recognized.  Verify that none of the registers
	 are set between I2 and I3.  */
      if (insn_code_number < 0 && (split = find_split_point (&newpat, i3)) != 0
#ifdef HAVE_cc0
	  && REG_P (i2dest)
#endif
	  /* We need I2DEST in the proper mode.  If it is a hard register
	     or the only use of a pseudo, we can change its mode.
	     Make sure we don't change a hard register to have a mode that
	     isn't valid for it, or change the number of registers.  */
	  && (GET_MODE (*split) == GET_MODE (i2dest)
	      || GET_MODE (*split) == VOIDmode
	      || can_change_dest_mode (i2dest, added_sets_2,
				       GET_MODE (*split)))
	  && (next_real_insn (i2) == i3
	      || ! use_crosses_set_p (*split, INSN_CUID (i2)))
	  /* We can't overwrite I2DEST if its value is still used by
	     NEWPAT.  */
	  && ! reg_referenced_p (i2dest, newpat))
	{
	  rtx newdest = i2dest;
	  enum rtx_code split_code = GET_CODE (*split);
	  enum machine_mode split_mode = GET_MODE (*split);
	  bool subst_done = false;
	  newi2pat = NULL_RTX;

	  /* Get NEWDEST as a register in the proper mode.  We have already
	     validated that we can do this.  */
	  if (GET_MODE (i2dest) != split_mode && split_mode != VOIDmode)
	    {
	      if (REGNO (i2dest) < FIRST_PSEUDO_REGISTER)
		newdest = gen_rtx_REG (split_mode, REGNO (i2dest));
	      else
		{
		  SUBST_MODE (regno_reg_rtx[REGNO (i2dest)], split_mode);
		  newdest = regno_reg_rtx[REGNO (i2dest)];
		}
	    }

	  /* If *SPLIT is a (mult FOO (const_int pow2)), convert it to
	     an ASHIFT.  This can occur if it was inside a PLUS and hence
	     appeared to be a memory address.  This is a kludge.  */
	  if (split_code == MULT
	      && GET_CODE (XEXP (*split, 1)) == CONST_INT
	      && INTVAL (XEXP (*split, 1)) > 0
	      && (i = exact_log2 (INTVAL (XEXP (*split, 1)))) >= 0)
	    {
	      SUBST (*split, gen_rtx_ASHIFT (split_mode,
					     XEXP (*split, 0), GEN_INT (i)));
	      /* Update split_code because we may not have a multiply
		 anymore.  */
	      split_code = GET_CODE (*split);
	    }

#ifdef INSN_SCHEDULING
	  /* If *SPLIT is a paradoxical SUBREG, when we split it, it should
	     be written as a ZERO_EXTEND.  */
	  if (split_code == SUBREG && MEM_P (SUBREG_REG (*split)))
	    {
#ifdef LOAD_EXTEND_OP
	      /* Or as a SIGN_EXTEND if LOAD_EXTEND_OP says that that's
		 what it really is.  */
	      if (LOAD_EXTEND_OP (GET_MODE (SUBREG_REG (*split)))
		  == SIGN_EXTEND)
		SUBST (*split, gen_rtx_SIGN_EXTEND (split_mode,
						    SUBREG_REG (*split)));
	      else
#endif
		SUBST (*split, gen_rtx_ZERO_EXTEND (split_mode,
						    SUBREG_REG (*split)));
	    }
#endif

	  /* Attempt to split binary operators using arithmetic identities.  */
	  if (BINARY_P (SET_SRC (newpat))
	      && split_mode == GET_MODE (SET_SRC (newpat))
	      && ! side_effects_p (SET_SRC (newpat)))
	    {
	      rtx setsrc = SET_SRC (newpat);
	      enum machine_mode mode = GET_MODE (setsrc);
	      enum rtx_code code = GET_CODE (setsrc);
	      rtx src_op0 = XEXP (setsrc, 0);
	      rtx src_op1 = XEXP (setsrc, 1);

	      /* Split "X = Y op Y" as "Z = Y; X = Z op Z".  */
	      if (rtx_equal_p (src_op0, src_op1))
		{
		  newi2pat = gen_rtx_SET (VOIDmode, newdest, src_op0);
		  SUBST (XEXP (setsrc, 0), newdest);
		  SUBST (XEXP (setsrc, 1), newdest);
		  subst_done = true;
		}
	      /* Split "((P op Q) op R) op S" where op is PLUS or MULT.  */
	      else if ((code == PLUS || code == MULT)
		       && GET_CODE (src_op0) == code
		       && GET_CODE (XEXP (src_op0, 0)) == code
		       && (INTEGRAL_MODE_P (mode)
			   || (FLOAT_MODE_P (mode)
			       && flag_unsafe_math_optimizations)))
		{
		  rtx p = XEXP (XEXP (src_op0, 0), 0);
		  rtx q = XEXP (XEXP (src_op0, 0), 1);
		  rtx r = XEXP (src_op0, 1);
		  rtx s = src_op1;

		  /* Split both "((X op Y) op X) op Y" and
		     "((X op Y) op Y) op X" as "T op T" where T is
		     "X op Y".  */
		  if ((rtx_equal_p (p,r) && rtx_equal_p (q,s))
		       || (rtx_equal_p (p,s) && rtx_equal_p (q,r)))
		    {
		      newi2pat = gen_rtx_SET (VOIDmode, newdest,
					      XEXP (src_op0, 0));
		      SUBST (XEXP (setsrc, 0), newdest);
		      SUBST (XEXP (setsrc, 1), newdest);
		      subst_done = true;
		    }
		  /* Split "((X op X) op Y) op Y)" as "T op T" where
		     T is "X op Y".  */
		  else if (rtx_equal_p (p,q) && rtx_equal_p (r,s))
		    {
		      rtx tmp = simplify_gen_binary (code, mode, p, r);
		      newi2pat = gen_rtx_SET (VOIDmode, newdest, tmp);
		      SUBST (XEXP (setsrc, 0), newdest);
		      SUBST (XEXP (setsrc, 1), newdest);
		      subst_done = true;
		    }
		}
	    }

	  if (!subst_done)
	    {
	      newi2pat = gen_rtx_SET (VOIDmode, newdest, *split);
	      SUBST (*split, newdest);
	    }

	  i2_code_number = recog_for_combine (&newi2pat, i2, &new_i2_notes);

	  /* recog_for_combine might have added CLOBBERs to newi2pat.
	     Make sure NEWPAT does not depend on the clobbered regs.  */
	  if (GET_CODE (newi2pat) == PARALLEL)
	    for (i = XVECLEN (newi2pat, 0) - 1; i >= 0; i--)
	      if (GET_CODE (XVECEXP (newi2pat, 0, i)) == CLOBBER)
		{
		  rtx reg = XEXP (XVECEXP (newi2pat, 0, i), 0);
		  if (reg_overlap_mentioned_p (reg, newpat))
		    {
		      undo_all ();
		      return 0;
		    }
		}

	  /* If the split point was a MULT and we didn't have one before,
	     don't use one now.  */
	  if (i2_code_number >= 0 && ! (split_code == MULT && ! have_mult))
	    insn_code_number = recog_for_combine (&newpat, i3, &new_i3_notes);
	}
    }

  /* Check for a case where we loaded from memory in a narrow mode and
     then sign extended it, but we need both registers.  In that case,
     we have a PARALLEL with both loads from the same memory location.
     We can split this into a load from memory followed by a register-register
     copy.  This saves at least one insn, more if register allocation can
     eliminate the copy.

     We cannot do this if the destination of the first assignment is a
     condition code register or cc0.  We eliminate this case by making sure
     the SET_DEST and SET_SRC have the same mode.

     We cannot do this if the destination of the second assignment is
     a register that we have already assumed is zero-extended.  Similarly
     for a SUBREG of such a register.  */

  else if (i1 && insn_code_number < 0 && asm_noperands (newpat) < 0
	   && GET_CODE (newpat) == PARALLEL
	   && XVECLEN (newpat, 0) == 2
	   && GET_CODE (XVECEXP (newpat, 0, 0)) == SET
	   && GET_CODE (SET_SRC (XVECEXP (newpat, 0, 0))) == SIGN_EXTEND
	   && (GET_MODE (SET_DEST (XVECEXP (newpat, 0, 0)))
	       == GET_MODE (SET_SRC (XVECEXP (newpat, 0, 0))))
	   && GET_CODE (XVECEXP (newpat, 0, 1)) == SET
	   && rtx_equal_p (SET_SRC (XVECEXP (newpat, 0, 1)),
			   XEXP (SET_SRC (XVECEXP (newpat, 0, 0)), 0))
	   && ! use_crosses_set_p (SET_SRC (XVECEXP (newpat, 0, 1)),
				   INSN_CUID (i2))
	   && GET_CODE (SET_DEST (XVECEXP (newpat, 0, 1))) != ZERO_EXTRACT
	   && GET_CODE (SET_DEST (XVECEXP (newpat, 0, 1))) != STRICT_LOW_PART
	   && ! (temp = SET_DEST (XVECEXP (newpat, 0, 1)),
		 (REG_P (temp)
		  && reg_stat[REGNO (temp)].nonzero_bits != 0
		  && GET_MODE_BITSIZE (GET_MODE (temp)) < BITS_PER_WORD
		  && GET_MODE_BITSIZE (GET_MODE (temp)) < HOST_BITS_PER_INT
		  && (reg_stat[REGNO (temp)].nonzero_bits
		      != GET_MODE_MASK (word_mode))))
	   && ! (GET_CODE (SET_DEST (XVECEXP (newpat, 0, 1))) == SUBREG
		 && (temp = SUBREG_REG (SET_DEST (XVECEXP (newpat, 0, 1))),
		     (REG_P (temp)
		      && reg_stat[REGNO (temp)].nonzero_bits != 0
		      && GET_MODE_BITSIZE (GET_MODE (temp)) < BITS_PER_WORD
		      && GET_MODE_BITSIZE (GET_MODE (temp)) < HOST_BITS_PER_INT
		      && (reg_stat[REGNO (temp)].nonzero_bits
			  != GET_MODE_MASK (word_mode)))))
	   && ! reg_overlap_mentioned_p (SET_DEST (XVECEXP (newpat, 0, 1)),
					 SET_SRC (XVECEXP (newpat, 0, 1)))
	   && ! find_reg_note (i3, REG_UNUSED,
			       SET_DEST (XVECEXP (newpat, 0, 0))))
    {
      rtx ni2dest;

      newi2pat = XVECEXP (newpat, 0, 0);
      ni2dest = SET_DEST (XVECEXP (newpat, 0, 0));
      newpat = XVECEXP (newpat, 0, 1);
      SUBST (SET_SRC (newpat),
	     gen_lowpart (GET_MODE (SET_SRC (newpat)), ni2dest));
      i2_code_number = recog_for_combine (&newi2pat, i2, &new_i2_notes);

      if (i2_code_number >= 0)
	insn_code_number = recog_for_combine (&newpat, i3, &new_i3_notes);

      if (insn_code_number >= 0)
	swap_i2i3 = 1;
    }

  /* Similarly, check for a case where we have a PARALLEL of two independent
     SETs but we started with three insns.  In this case, we can do the sets
     as two separate insns.  This case occurs when some SET allows two
     other insns to combine, but the destination of that SET is still live.  */

  else if (i1 && insn_code_number < 0 && asm_noperands (newpat) < 0
	   && GET_CODE (newpat) == PARALLEL
	   && XVECLEN (newpat, 0) == 2
	   && GET_CODE (XVECEXP (newpat, 0, 0)) == SET
	   && GET_CODE (SET_DEST (XVECEXP (newpat, 0, 0))) != ZERO_EXTRACT
	   && GET_CODE (SET_DEST (XVECEXP (newpat, 0, 0))) != STRICT_LOW_PART
	   && GET_CODE (XVECEXP (newpat, 0, 1)) == SET
	   && GET_CODE (SET_DEST (XVECEXP (newpat, 0, 1))) != ZERO_EXTRACT
	   && GET_CODE (SET_DEST (XVECEXP (newpat, 0, 1))) != STRICT_LOW_PART
	   && ! use_crosses_set_p (SET_SRC (XVECEXP (newpat, 0, 1)),
				   INSN_CUID (i2))
	   && ! reg_referenced_p (SET_DEST (XVECEXP (newpat, 0, 1)),
				  XVECEXP (newpat, 0, 0))
	   && ! reg_referenced_p (SET_DEST (XVECEXP (newpat, 0, 0)),
				  XVECEXP (newpat, 0, 1))
	   && ! (contains_muldiv (SET_SRC (XVECEXP (newpat, 0, 0)))
		 && contains_muldiv (SET_SRC (XVECEXP (newpat, 0, 1))))
#ifdef HAVE_cc0
	   /* We cannot split the parallel into two sets if both sets
	      reference cc0.  */
	   && ! (reg_referenced_p (cc0_rtx, XVECEXP (newpat, 0, 0))
		 && reg_referenced_p (cc0_rtx, XVECEXP (newpat, 0, 1)))
#endif
	   )
    {
      /* Normally, it doesn't matter which of the two is done first,
	 but it does if one references cc0.  In that case, it has to
	 be first.  */
#ifdef HAVE_cc0
      if (reg_referenced_p (cc0_rtx, XVECEXP (newpat, 0, 0)))
	{
	  newi2pat = XVECEXP (newpat, 0, 0);
	  newpat = XVECEXP (newpat, 0, 1);
	}
      else
#endif
	{
	  newi2pat = XVECEXP (newpat, 0, 1);
	  newpat = XVECEXP (newpat, 0, 0);
	}

      i2_code_number = recog_for_combine (&newi2pat, i2, &new_i2_notes);

      if (i2_code_number >= 0)
	insn_code_number = recog_for_combine (&newpat, i3, &new_i3_notes);
    }

  /* If it still isn't recognized, fail and change things back the way they
     were.  */
  if ((insn_code_number < 0
       /* Is the result a reasonable ASM_OPERANDS?  */
       && (! check_asm_operands (newpat) || added_sets_1 || added_sets_2)))
    {
      undo_all ();
      return 0;
    }

  /* If we had to change another insn, make sure it is valid also.  */
  if (undobuf.other_insn)
    {
      rtx other_pat = PATTERN (undobuf.other_insn);
      rtx new_other_notes;
      rtx note, next;

      CLEAR_HARD_REG_SET (newpat_used_regs);

      other_code_number = recog_for_combine (&other_pat, undobuf.other_insn,
					     &new_other_notes);

      if (other_code_number < 0 && ! check_asm_operands (other_pat))
	{
	  undo_all ();
	  return 0;
	}

      PATTERN (undobuf.other_insn) = other_pat;

      /* If any of the notes in OTHER_INSN were REG_UNUSED, ensure that they
	 are still valid.  Then add any non-duplicate notes added by
	 recog_for_combine.  */
      for (note = REG_NOTES (undobuf.other_insn); note; note = next)
	{
	  next = XEXP (note, 1);

	  if (REG_NOTE_KIND (note) == REG_UNUSED
	      && ! reg_set_p (XEXP (note, 0), PATTERN (undobuf.other_insn)))
	    {
	      if (REG_P (XEXP (note, 0)))
		REG_N_DEATHS (REGNO (XEXP (note, 0)))--;

	      remove_note (undobuf.other_insn, note);
	    }
	}

      for (note = new_other_notes; note; note = XEXP (note, 1))
	if (REG_P (XEXP (note, 0)))
	  REG_N_DEATHS (REGNO (XEXP (note, 0)))++;

      distribute_notes (new_other_notes, undobuf.other_insn,
			undobuf.other_insn, NULL_RTX, NULL_RTX, NULL_RTX);
    }
#ifdef HAVE_cc0
  /* If I2 is the CC0 setter and I3 is the CC0 user then check whether
     they are adjacent to each other or not.  */
  {
    rtx p = prev_nonnote_insn (i3);
    if (p && p != i2 && NONJUMP_INSN_P (p) && newi2pat
	&& sets_cc0_p (newi2pat))
      {
	undo_all ();
	return 0;
      }
  }
#endif

  /* Only allow this combination if insn_rtx_costs reports that the
     replacement instructions are cheaper than the originals.  */
  if (!combine_validate_cost (i1, i2, i3, newpat, newi2pat))
    {
      undo_all ();
      return 0;
    }

  /* We now know that we can do this combination.  Merge the insns and
     update the status of registers and LOG_LINKS.  */

  if (swap_i2i3)
    {
      rtx insn;
      rtx link;
      rtx ni2dest;

      /* I3 now uses what used to be its destination and which is now
	 I2's destination.  This requires us to do a few adjustments.  */
      PATTERN (i3) = newpat;
      adjust_for_new_dest (i3);

      /* We need a LOG_LINK from I3 to I2.  But we used to have one,
	 so we still will.

	 However, some later insn might be using I2's dest and have
	 a LOG_LINK pointing at I3.  We must remove this link.
	 The simplest way to remove the link is to point it at I1,
	 which we know will be a NOTE.  */

      /* newi2pat is usually a SET here; however, recog_for_combine might
	 have added some clobbers.  */
      if (GET_CODE (newi2pat) == PARALLEL)
	ni2dest = SET_DEST (XVECEXP (newi2pat, 0, 0));
      else
	ni2dest = SET_DEST (newi2pat);

      for (insn = NEXT_INSN (i3);
	   insn && (this_basic_block->next_bb == EXIT_BLOCK_PTR
		    || insn != BB_HEAD (this_basic_block->next_bb));
	   insn = NEXT_INSN (insn))
	{
	  if (INSN_P (insn) && reg_referenced_p (ni2dest, PATTERN (insn)))
	    {
	      for (link = LOG_LINKS (insn); link;
		   link = XEXP (link, 1))
		if (XEXP (link, 0) == i3)
		  XEXP (link, 0) = i1;

	      break;
	    }
	}
    }

  {
    rtx i3notes, i2notes, i1notes = 0;
    rtx i3links, i2links, i1links = 0;
    rtx midnotes = 0;
    unsigned int regno;
    /* Compute which registers we expect to eliminate.  newi2pat may be setting
       either i3dest or i2dest, so we must check it.  Also, i1dest may be the
       same as i3dest, in which case newi2pat may be setting i1dest.  */
    rtx elim_i2 = ((newi2pat && reg_set_p (i2dest, newi2pat))
		   || i2dest_in_i2src || i2dest_in_i1src
		   || !i2dest_killed
		   ? 0 : i2dest);
    rtx elim_i1 = (i1 == 0 || i1dest_in_i1src
		   || (newi2pat && reg_set_p (i1dest, newi2pat))
		   || !i1dest_killed
		   ? 0 : i1dest);

    /* Get the old REG_NOTES and LOG_LINKS from all our insns and
       clear them.  */
    i3notes = REG_NOTES (i3), i3links = LOG_LINKS (i3);
    i2notes = REG_NOTES (i2), i2links = LOG_LINKS (i2);
    if (i1)
      i1notes = REG_NOTES (i1), i1links = LOG_LINKS (i1);

    /* Ensure that we do not have something that should not be shared but
       occurs multiple times in the new insns.  Check this by first
       resetting all the `used' flags and then copying anything is shared.  */

    reset_used_flags (i3notes);
    reset_used_flags (i2notes);
    reset_used_flags (i1notes);
    reset_used_flags (newpat);
    reset_used_flags (newi2pat);
    if (undobuf.other_insn)
      reset_used_flags (PATTERN (undobuf.other_insn));

    i3notes = copy_rtx_if_shared (i3notes);
    i2notes = copy_rtx_if_shared (i2notes);
    i1notes = copy_rtx_if_shared (i1notes);
    newpat = copy_rtx_if_shared (newpat);
    newi2pat = copy_rtx_if_shared (newi2pat);
    if (undobuf.other_insn)
      reset_used_flags (PATTERN (undobuf.other_insn));

    INSN_CODE (i3) = insn_code_number;
    PATTERN (i3) = newpat;

    if (CALL_P (i3) && CALL_INSN_FUNCTION_USAGE (i3))
      {
	rtx call_usage = CALL_INSN_FUNCTION_USAGE (i3);

	reset_used_flags (call_usage);
	call_usage = copy_rtx (call_usage);

	if (substed_i2)
	  replace_rtx (call_usage, i2dest, i2src);

	if (substed_i1)
	  replace_rtx (call_usage, i1dest, i1src);

	CALL_INSN_FUNCTION_USAGE (i3) = call_usage;
      }

    if (undobuf.other_insn)
      INSN_CODE (undobuf.other_insn) = other_code_number;

    /* We had one special case above where I2 had more than one set and
       we replaced a destination of one of those sets with the destination
       of I3.  In that case, we have to update LOG_LINKS of insns later
       in this basic block.  Note that this (expensive) case is rare.

       Also, in this case, we must pretend that all REG_NOTEs for I2
       actually came from I3, so that REG_UNUSED notes from I2 will be
       properly handled.  */

    if (i3_subst_into_i2)
      {
	for (i = 0; i < XVECLEN (PATTERN (i2), 0); i++)
	  if ((GET_CODE (XVECEXP (PATTERN (i2), 0, i)) == SET
	       || GET_CODE (XVECEXP (PATTERN (i2), 0, i)) == CLOBBER)
	      && REG_P (SET_DEST (XVECEXP (PATTERN (i2), 0, i)))
	      && SET_DEST (XVECEXP (PATTERN (i2), 0, i)) != i2dest
	      && ! find_reg_note (i2, REG_UNUSED,
				  SET_DEST (XVECEXP (PATTERN (i2), 0, i))))
	    for (temp = NEXT_INSN (i2);
		 temp && (this_basic_block->next_bb == EXIT_BLOCK_PTR
			  || BB_HEAD (this_basic_block) != temp);
		 temp = NEXT_INSN (temp))
	      if (temp != i3 && INSN_P (temp))
		for (link = LOG_LINKS (temp); link; link = XEXP (link, 1))
		  if (XEXP (link, 0) == i2)
		    XEXP (link, 0) = i3;

	if (i3notes)
	  {
	    rtx link = i3notes;
	    while (XEXP (link, 1))
	      link = XEXP (link, 1);
	    XEXP (link, 1) = i2notes;
	  }
	else
	  i3notes = i2notes;
	i2notes = 0;
      }

    LOG_LINKS (i3) = 0;
    REG_NOTES (i3) = 0;
    LOG_LINKS (i2) = 0;
    REG_NOTES (i2) = 0;

    if (newi2pat)
      {
	INSN_CODE (i2) = i2_code_number;
	PATTERN (i2) = newi2pat;
      }
    else
      SET_INSN_DELETED (i2);

    if (i1)
      {
	LOG_LINKS (i1) = 0;
	REG_NOTES (i1) = 0;
	SET_INSN_DELETED (i1);
      }

    /* Get death notes for everything that is now used in either I3 or
       I2 and used to die in a previous insn.  If we built two new
       patterns, move from I1 to I2 then I2 to I3 so that we get the
       proper movement on registers that I2 modifies.  */

    if (newi2pat)
      {
	move_deaths (newi2pat, NULL_RTX, INSN_CUID (i1), i2, &midnotes);
	move_deaths (newpat, newi2pat, INSN_CUID (i1), i3, &midnotes);
      }
    else
      move_deaths (newpat, NULL_RTX, i1 ? INSN_CUID (i1) : INSN_CUID (i2),
		   i3, &midnotes);

    /* Distribute all the LOG_LINKS and REG_NOTES from I1, I2, and I3.  */
    if (i3notes)
      distribute_notes (i3notes, i3, i3, newi2pat ? i2 : NULL_RTX,
			elim_i2, elim_i1);
    if (i2notes)
      distribute_notes (i2notes, i2, i3, newi2pat ? i2 : NULL_RTX,
			elim_i2, elim_i1);
    if (i1notes)
      distribute_notes (i1notes, i1, i3, newi2pat ? i2 : NULL_RTX,
			elim_i2, elim_i1);
    if (midnotes)
      distribute_notes (midnotes, NULL_RTX, i3, newi2pat ? i2 : NULL_RTX,
			elim_i2, elim_i1);

    /* Distribute any notes added to I2 or I3 by recog_for_combine.  We
       know these are REG_UNUSED and want them to go to the desired insn,
       so we always pass it as i3.  We have not counted the notes in
       reg_n_deaths yet, so we need to do so now.  */

    if (newi2pat && new_i2_notes)
      {
	for (temp = new_i2_notes; temp; temp = XEXP (temp, 1))
	  if (REG_P (XEXP (temp, 0)))
	    REG_N_DEATHS (REGNO (XEXP (temp, 0)))++;

	distribute_notes (new_i2_notes, i2, i2, NULL_RTX, NULL_RTX, NULL_RTX);
      }

    if (new_i3_notes)
      {
	for (temp = new_i3_notes; temp; temp = XEXP (temp, 1))
	  if (REG_P (XEXP (temp, 0)))
	    REG_N_DEATHS (REGNO (XEXP (temp, 0)))++;

	distribute_notes (new_i3_notes, i3, i3, NULL_RTX, NULL_RTX, NULL_RTX);
      }

    /* If I3DEST was used in I3SRC, it really died in I3.  We may need to
       put a REG_DEAD note for it somewhere.  If NEWI2PAT exists and sets
       I3DEST, the death must be somewhere before I2, not I3.  If we passed I3
       in that case, it might delete I2.  Similarly for I2 and I1.
       Show an additional death due to the REG_DEAD note we make here.  If
       we discard it in distribute_notes, we will decrement it again.  */

    if (i3dest_killed)
      {
	if (REG_P (i3dest_killed))
	  REG_N_DEATHS (REGNO (i3dest_killed))++;

	if (newi2pat && reg_set_p (i3dest_killed, newi2pat))
	  distribute_notes (gen_rtx_EXPR_LIST (REG_DEAD, i3dest_killed,
					       NULL_RTX),
			    NULL_RTX, i2, NULL_RTX, elim_i2, elim_i1);
	else
	  distribute_notes (gen_rtx_EXPR_LIST (REG_DEAD, i3dest_killed,
					       NULL_RTX),
			    NULL_RTX, i3, newi2pat ? i2 : NULL_RTX,
			    elim_i2, elim_i1);
      }

    if (i2dest_in_i2src)
      {
	if (REG_P (i2dest))
	  REG_N_DEATHS (REGNO (i2dest))++;

	if (newi2pat && reg_set_p (i2dest, newi2pat))
	  distribute_notes (gen_rtx_EXPR_LIST (REG_DEAD, i2dest, NULL_RTX),
			    NULL_RTX, i2, NULL_RTX, NULL_RTX, NULL_RTX);
	else
	  distribute_notes (gen_rtx_EXPR_LIST (REG_DEAD, i2dest, NULL_RTX),
			    NULL_RTX, i3, newi2pat ? i2 : NULL_RTX,
			    NULL_RTX, NULL_RTX);
      }

    if (i1dest_in_i1src)
      {
	if (REG_P (i1dest))
	  REG_N_DEATHS (REGNO (i1dest))++;

	if (newi2pat && reg_set_p (i1dest, newi2pat))
	  distribute_notes (gen_rtx_EXPR_LIST (REG_DEAD, i1dest, NULL_RTX),
			    NULL_RTX, i2, NULL_RTX, NULL_RTX, NULL_RTX);
	else
	  distribute_notes (gen_rtx_EXPR_LIST (REG_DEAD, i1dest, NULL_RTX),
			    NULL_RTX, i3, newi2pat ? i2 : NULL_RTX,
			    NULL_RTX, NULL_RTX);
      }

    distribute_links (i3links);
    distribute_links (i2links);
    distribute_links (i1links);

    if (REG_P (i2dest))
      {
	rtx link;
	rtx i2_insn = 0, i2_val = 0, set;

	/* The insn that used to set this register doesn't exist, and
	   this life of the register may not exist either.  See if one of
	   I3's links points to an insn that sets I2DEST.  If it does,
	   that is now the last known value for I2DEST. If we don't update
	   this and I2 set the register to a value that depended on its old
	   contents, we will get confused.  If this insn is used, thing
	   will be set correctly in combine_instructions.  */

	for (link = LOG_LINKS (i3); link; link = XEXP (link, 1))
	  if ((set = single_set (XEXP (link, 0))) != 0
	      && rtx_equal_p (i2dest, SET_DEST (set)))
	    i2_insn = XEXP (link, 0), i2_val = SET_SRC (set);

	record_value_for_reg (i2dest, i2_insn, i2_val);

	/* If the reg formerly set in I2 died only once and that was in I3,
	   zero its use count so it won't make `reload' do any work.  */
	if (! added_sets_2
	    && (newi2pat == 0 || ! reg_mentioned_p (i2dest, newi2pat))
	    && ! i2dest_in_i2src)
	  {
	    regno = REGNO (i2dest);
	    REG_N_SETS (regno)--;
	  }
      }

    if (i1 && REG_P (i1dest))
      {
	rtx link;
	rtx i1_insn = 0, i1_val = 0, set;

	for (link = LOG_LINKS (i3); link; link = XEXP (link, 1))
	  if ((set = single_set (XEXP (link, 0))) != 0
	      && rtx_equal_p (i1dest, SET_DEST (set)))
	    i1_insn = XEXP (link, 0), i1_val = SET_SRC (set);

	record_value_for_reg (i1dest, i1_insn, i1_val);

	regno = REGNO (i1dest);
	if (! added_sets_1 && ! i1dest_in_i1src)
	  REG_N_SETS (regno)--;
      }

    /* Update reg_stat[].nonzero_bits et al for any changes that may have
       been made to this insn.  The order of
       set_nonzero_bits_and_sign_copies() is important.  Because newi2pat
       can affect nonzero_bits of newpat */
    if (newi2pat)
      note_stores (newi2pat, set_nonzero_bits_and_sign_copies, NULL);
    note_stores (newpat, set_nonzero_bits_and_sign_copies, NULL);

    /* Set new_direct_jump_p if a new return or simple jump instruction
       has been created.

       If I3 is now an unconditional jump, ensure that it has a
       BARRIER following it since it may have initially been a
       conditional jump.  It may also be the last nonnote insn.  */

    if (returnjump_p (i3) || any_uncondjump_p (i3))
      {
	*new_direct_jump_p = 1;
	mark_jump_label (PATTERN (i3), i3, 0);

	if ((temp = next_nonnote_insn (i3)) == NULL_RTX
	    || !BARRIER_P (temp))
	  emit_barrier_after (i3);
      }

    if (undobuf.other_insn != NULL_RTX
	&& (returnjump_p (undobuf.other_insn)
	    || any_uncondjump_p (undobuf.other_insn)))
      {
	*new_direct_jump_p = 1;

	if ((temp = next_nonnote_insn (undobuf.other_insn)) == NULL_RTX
	    || !BARRIER_P (temp))
	  emit_barrier_after (undobuf.other_insn);
      }

    /* An NOOP jump does not need barrier, but it does need cleaning up
       of CFG.  */
    if (GET_CODE (newpat) == SET
	&& SET_SRC (newpat) == pc_rtx
	&& SET_DEST (newpat) == pc_rtx)
      *new_direct_jump_p = 1;
  }

  combine_successes++;
  undo_commit ();

  if (added_links_insn
      && (newi2pat == 0 || INSN_CUID (added_links_insn) < INSN_CUID (i2))
      && INSN_CUID (added_links_insn) < INSN_CUID (i3))
    return added_links_insn;
  else
    return newi2pat ? i2 : i3;
}

/* Undo all the modifications recorded in undobuf.  */

static void
undo_all (void)
{
  struct undo *undo, *next;

  for (undo = undobuf.undos; undo; undo = next)
    {
      next = undo->next;
      switch (undo->kind)
	{
	case UNDO_RTX:
	  *undo->where.r = undo->old_contents.r;
	  break;
	case UNDO_INT:
	  *undo->where.i = undo->old_contents.i;
	  break;
	case UNDO_MODE:
	  PUT_MODE (*undo->where.r, undo->old_contents.m);
	  break;
	default:
	  gcc_unreachable ();
	}

      undo->next = undobuf.frees;
      undobuf.frees = undo;
    }

  undobuf.undos = 0;
}

/* We've committed to accepting the changes we made.  Move all
   of the undos to the free list.  */

static void
undo_commit (void)
{
  struct undo *undo, *next;

  for (undo = undobuf.undos; undo; undo = next)
    {
      next = undo->next;
      undo->next = undobuf.frees;
      undobuf.frees = undo;
    }
  undobuf.undos = 0;
}

/* Find the innermost point within the rtx at LOC, possibly LOC itself,
   where we have an arithmetic expression and return that point.  LOC will
   be inside INSN.

   try_combine will call this function to see if an insn can be split into
   two insns.  */

static rtx *
find_split_point (rtx *loc, rtx insn)
{
  rtx x = *loc;
  enum rtx_code code = GET_CODE (x);
  rtx *split;
  unsigned HOST_WIDE_INT len = 0;
  HOST_WIDE_INT pos = 0;
  int unsignedp = 0;
  rtx inner = NULL_RTX;

  /* First special-case some codes.  */
  switch (code)
    {
    case SUBREG:
#ifdef INSN_SCHEDULING
      /* If we are making a paradoxical SUBREG invalid, it becomes a split
	 point.  */
      if (MEM_P (SUBREG_REG (x)))
	return loc;
#endif
      return find_split_point (&SUBREG_REG (x), insn);

    case MEM:
#ifdef HAVE_lo_sum
      /* If we have (mem (const ..)) or (mem (symbol_ref ...)), split it
	 using LO_SUM and HIGH.  */
      if (GET_CODE (XEXP (x, 0)) == CONST
	  || GET_CODE (XEXP (x, 0)) == SYMBOL_REF)
	{
	  SUBST (XEXP (x, 0),
		 gen_rtx_LO_SUM (Pmode,
				 gen_rtx_HIGH (Pmode, XEXP (x, 0)),
				 XEXP (x, 0)));
	  return &XEXP (XEXP (x, 0), 0);
	}
#endif

      /* If we have a PLUS whose second operand is a constant and the
	 address is not valid, perhaps will can split it up using
	 the machine-specific way to split large constants.  We use
	 the first pseudo-reg (one of the virtual regs) as a placeholder;
	 it will not remain in the result.  */
      if (GET_CODE (XEXP (x, 0)) == PLUS
	  && GET_CODE (XEXP (XEXP (x, 0), 1)) == CONST_INT
	  && ! memory_address_p (GET_MODE (x), XEXP (x, 0)))
	{
	  rtx reg = regno_reg_rtx[FIRST_PSEUDO_REGISTER];
	  rtx seq = split_insns (gen_rtx_SET (VOIDmode, reg, XEXP (x, 0)),
				 subst_insn);

	  /* This should have produced two insns, each of which sets our
	     placeholder.  If the source of the second is a valid address,
	     we can make put both sources together and make a split point
	     in the middle.  */

	  if (seq
	      && NEXT_INSN (seq) != NULL_RTX
	      && NEXT_INSN (NEXT_INSN (seq)) == NULL_RTX
	      && NONJUMP_INSN_P (seq)
	      && GET_CODE (PATTERN (seq)) == SET
	      && SET_DEST (PATTERN (seq)) == reg
	      && ! reg_mentioned_p (reg,
				    SET_SRC (PATTERN (seq)))
	      && NONJUMP_INSN_P (NEXT_INSN (seq))
	      && GET_CODE (PATTERN (NEXT_INSN (seq))) == SET
	      && SET_DEST (PATTERN (NEXT_INSN (seq))) == reg
	      && memory_address_p (GET_MODE (x),
				   SET_SRC (PATTERN (NEXT_INSN (seq)))))
	    {
	      rtx src1 = SET_SRC (PATTERN (seq));
	      rtx src2 = SET_SRC (PATTERN (NEXT_INSN (seq)));

	      /* Replace the placeholder in SRC2 with SRC1.  If we can
		 find where in SRC2 it was placed, that can become our
		 split point and we can replace this address with SRC2.
		 Just try two obvious places.  */

	      src2 = replace_rtx (src2, reg, src1);
	      split = 0;
	      if (XEXP (src2, 0) == src1)
		split = &XEXP (src2, 0);
	      else if (GET_RTX_FORMAT (GET_CODE (XEXP (src2, 0)))[0] == 'e'
		       && XEXP (XEXP (src2, 0), 0) == src1)
		split = &XEXP (XEXP (src2, 0), 0);

	      if (split)
		{
		  SUBST (XEXP (x, 0), src2);
		  return split;
		}
	    }

	  /* If that didn't work, perhaps the first operand is complex and
	     needs to be computed separately, so make a split point there.
	     This will occur on machines that just support REG + CONST
	     and have a constant moved through some previous computation.  */

	  else if (!OBJECT_P (XEXP (XEXP (x, 0), 0))
		   && ! (GET_CODE (XEXP (XEXP (x, 0), 0)) == SUBREG
			 && OBJECT_P (SUBREG_REG (XEXP (XEXP (x, 0), 0)))))
	    return &XEXP (XEXP (x, 0), 0);
	}
      break;

    case SET:
#ifdef HAVE_cc0
      /* If SET_DEST is CC0 and SET_SRC is not an operand, a COMPARE, or a
	 ZERO_EXTRACT, the most likely reason why this doesn't match is that
	 we need to put the operand into a register.  So split at that
	 point.  */

      if (SET_DEST (x) == cc0_rtx
	  && GET_CODE (SET_SRC (x)) != COMPARE
	  && GET_CODE (SET_SRC (x)) != ZERO_EXTRACT
	  && !OBJECT_P (SET_SRC (x))
	  && ! (GET_CODE (SET_SRC (x)) == SUBREG
		&& OBJECT_P (SUBREG_REG (SET_SRC (x)))))
	return &SET_SRC (x);
#endif

      /* See if we can split SET_SRC as it stands.  */
      split = find_split_point (&SET_SRC (x), insn);
      if (split && split != &SET_SRC (x))
	return split;

      /* See if we can split SET_DEST as it stands.  */
      split = find_split_point (&SET_DEST (x), insn);
      if (split && split != &SET_DEST (x))
	return split;

      /* See if this is a bitfield assignment with everything constant.  If
	 so, this is an IOR of an AND, so split it into that.  */
      if (GET_CODE (SET_DEST (x)) == ZERO_EXTRACT
	  && (GET_MODE_BITSIZE (GET_MODE (XEXP (SET_DEST (x), 0)))
	      <= HOST_BITS_PER_WIDE_INT)
	  && GET_CODE (XEXP (SET_DEST (x), 1)) == CONST_INT
	  && GET_CODE (XEXP (SET_DEST (x), 2)) == CONST_INT
	  && GET_CODE (SET_SRC (x)) == CONST_INT
	  && ((INTVAL (XEXP (SET_DEST (x), 1))
	       + INTVAL (XEXP (SET_DEST (x), 2)))
	      <= GET_MODE_BITSIZE (GET_MODE (XEXP (SET_DEST (x), 0))))
	  && ! side_effects_p (XEXP (SET_DEST (x), 0)))
	{
	  HOST_WIDE_INT pos = INTVAL (XEXP (SET_DEST (x), 2));
	  unsigned HOST_WIDE_INT len = INTVAL (XEXP (SET_DEST (x), 1));
	  unsigned HOST_WIDE_INT src = INTVAL (SET_SRC (x));
	  rtx dest = XEXP (SET_DEST (x), 0);
	  enum machine_mode mode = GET_MODE (dest);
	  unsigned HOST_WIDE_INT mask = ((HOST_WIDE_INT) 1 << len) - 1;
	  rtx or_mask;

	  if (BITS_BIG_ENDIAN)
	    pos = GET_MODE_BITSIZE (mode) - len - pos;

	  or_mask = gen_int_mode (src << pos, mode);
	  if (src == mask)
	    SUBST (SET_SRC (x),
		   simplify_gen_binary (IOR, mode, dest, or_mask));
	  else
	    {
	      rtx negmask = gen_int_mode (~(mask << pos), mode);
	      SUBST (SET_SRC (x),
		     simplify_gen_binary (IOR, mode,
					  simplify_gen_binary (AND, mode,
							       dest, negmask),
					  or_mask));
	    }

	  SUBST (SET_DEST (x), dest);

	  split = find_split_point (&SET_SRC (x), insn);
	  if (split && split != &SET_SRC (x))
	    return split;
	}

      /* Otherwise, see if this is an operation that we can split into two.
	 If so, try to split that.  */
      code = GET_CODE (SET_SRC (x));

      switch (code)
	{
	case AND:
	  /* If we are AND'ing with a large constant that is only a single
	     bit and the result is only being used in a context where we
	     need to know if it is zero or nonzero, replace it with a bit
	     extraction.  This will avoid the large constant, which might
	     have taken more than one insn to make.  If the constant were
	     not a valid argument to the AND but took only one insn to make,
	     this is no worse, but if it took more than one insn, it will
	     be better.  */

	  if (GET_CODE (XEXP (SET_SRC (x), 1)) == CONST_INT
	      && REG_P (XEXP (SET_SRC (x), 0))
	      && (pos = exact_log2 (INTVAL (XEXP (SET_SRC (x), 1)))) >= 7
	      && REG_P (SET_DEST (x))
	      && (split = find_single_use (SET_DEST (x), insn, (rtx*) 0)) != 0
	      && (GET_CODE (*split) == EQ || GET_CODE (*split) == NE)
	      && XEXP (*split, 0) == SET_DEST (x)
	      && XEXP (*split, 1) == const0_rtx)
	    {
	      rtx extraction = make_extraction (GET_MODE (SET_DEST (x)),
						XEXP (SET_SRC (x), 0),
						pos, NULL_RTX, 1, 1, 0, 0);
	      if (extraction != 0)
		{
		  SUBST (SET_SRC (x), extraction);
		  return find_split_point (loc, insn);
		}
	    }
	  break;

	case NE:
	  /* If STORE_FLAG_VALUE is -1, this is (NE X 0) and only one bit of X
	     is known to be on, this can be converted into a NEG of a shift.  */
	  if (STORE_FLAG_VALUE == -1 && XEXP (SET_SRC (x), 1) == const0_rtx
	      && GET_MODE (SET_SRC (x)) == GET_MODE (XEXP (SET_SRC (x), 0))
	      && 1 <= (pos = exact_log2
		       (nonzero_bits (XEXP (SET_SRC (x), 0),
				      GET_MODE (XEXP (SET_SRC (x), 0))))))
	    {
	      enum machine_mode mode = GET_MODE (XEXP (SET_SRC (x), 0));

	      SUBST (SET_SRC (x),
		     gen_rtx_NEG (mode,
				  gen_rtx_LSHIFTRT (mode,
						    XEXP (SET_SRC (x), 0),
						    GEN_INT (pos))));

	      split = find_split_point (&SET_SRC (x), insn);
	      if (split && split != &SET_SRC (x))
		return split;
	    }
	  break;

	case SIGN_EXTEND:
	  inner = XEXP (SET_SRC (x), 0);

	  /* We can't optimize if either mode is a partial integer
	     mode as we don't know how many bits are significant
	     in those modes.  */
	  if (GET_MODE_CLASS (GET_MODE (inner)) == MODE_PARTIAL_INT
	      || GET_MODE_CLASS (GET_MODE (SET_SRC (x))) == MODE_PARTIAL_INT)
	    break;

	  pos = 0;
	  len = GET_MODE_BITSIZE (GET_MODE (inner));
	  unsignedp = 0;
	  break;

	case SIGN_EXTRACT:
	case ZERO_EXTRACT:
	  if (GET_CODE (XEXP (SET_SRC (x), 1)) == CONST_INT
	      && GET_CODE (XEXP (SET_SRC (x), 2)) == CONST_INT)
	    {
	      inner = XEXP (SET_SRC (x), 0);
	      len = INTVAL (XEXP (SET_SRC (x), 1));
	      pos = INTVAL (XEXP (SET_SRC (x), 2));

	      if (BITS_BIG_ENDIAN)
		pos = GET_MODE_BITSIZE (GET_MODE (inner)) - len - pos;
	      unsignedp = (code == ZERO_EXTRACT);
	    }
	  break;

	default:
	  break;
	}

      if (len && pos >= 0 && pos + len <= GET_MODE_BITSIZE (GET_MODE (inner)))
	{
	  enum machine_mode mode = GET_MODE (SET_SRC (x));

	  /* For unsigned, we have a choice of a shift followed by an
	     AND or two shifts.  Use two shifts for field sizes where the
	     constant might be too large.  We assume here that we can
	     always at least get 8-bit constants in an AND insn, which is
	     true for every current RISC.  */

	  if (unsignedp && len <= 8)
	    {
	      SUBST (SET_SRC (x),
		     gen_rtx_AND (mode,
				  gen_rtx_LSHIFTRT
				  (mode, gen_lowpart (mode, inner),
				   GEN_INT (pos)),
				  GEN_INT (((HOST_WIDE_INT) 1 << len) - 1)));

	      split = find_split_point (&SET_SRC (x), insn);
	      if (split && split != &SET_SRC (x))
		return split;
	    }
	  else
	    {
	      SUBST (SET_SRC (x),
		     gen_rtx_fmt_ee
		     (unsignedp ? LSHIFTRT : ASHIFTRT, mode,
		      gen_rtx_ASHIFT (mode,
				      gen_lowpart (mode, inner),
				      GEN_INT (GET_MODE_BITSIZE (mode)
					       - len - pos)),
		      GEN_INT (GET_MODE_BITSIZE (mode) - len)));

	      split = find_split_point (&SET_SRC (x), insn);
	      if (split && split != &SET_SRC (x))
		return split;
	    }
	}

      /* See if this is a simple operation with a constant as the second
	 operand.  It might be that this constant is out of range and hence
	 could be used as a split point.  */
      if (BINARY_P (SET_SRC (x))
	  && CONSTANT_P (XEXP (SET_SRC (x), 1))
	  && (OBJECT_P (XEXP (SET_SRC (x), 0))
	      || (GET_CODE (XEXP (SET_SRC (x), 0)) == SUBREG
		  && OBJECT_P (SUBREG_REG (XEXP (SET_SRC (x), 0))))))
	return &XEXP (SET_SRC (x), 1);

      /* Finally, see if this is a simple operation with its first operand
	 not in a register.  The operation might require this operand in a
	 register, so return it as a split point.  We can always do this
	 because if the first operand were another operation, we would have
	 already found it as a split point.  */
      if ((BINARY_P (SET_SRC (x)) || UNARY_P (SET_SRC (x)))
	  && ! register_operand (XEXP (SET_SRC (x), 0), VOIDmode))
	return &XEXP (SET_SRC (x), 0);

      return 0;

    case AND:
    case IOR:
      /* We write NOR as (and (not A) (not B)), but if we don't have a NOR,
	 it is better to write this as (not (ior A B)) so we can split it.
	 Similarly for IOR.  */
      if (GET_CODE (XEXP (x, 0)) == NOT && GET_CODE (XEXP (x, 1)) == NOT)
	{
	  SUBST (*loc,
		 gen_rtx_NOT (GET_MODE (x),
			      gen_rtx_fmt_ee (code == IOR ? AND : IOR,
					      GET_MODE (x),
					      XEXP (XEXP (x, 0), 0),
					      XEXP (XEXP (x, 1), 0))));
	  return find_split_point (loc, insn);
	}

      /* Many RISC machines have a large set of logical insns.  If the
	 second operand is a NOT, put it first so we will try to split the
	 other operand first.  */
      if (GET_CODE (XEXP (x, 1)) == NOT)
	{
	  rtx tem = XEXP (x, 0);
	  SUBST (XEXP (x, 0), XEXP (x, 1));
	  SUBST (XEXP (x, 1), tem);
	}
      break;

    default:
      break;
    }

  /* Otherwise, select our actions depending on our rtx class.  */
  switch (GET_RTX_CLASS (code))
    {
    case RTX_BITFIELD_OPS:		/* This is ZERO_EXTRACT and SIGN_EXTRACT.  */
    case RTX_TERNARY:
      split = find_split_point (&XEXP (x, 2), insn);
      if (split)
	return split;
      /* ... fall through ...  */
    case RTX_BIN_ARITH:
    case RTX_COMM_ARITH:
    case RTX_COMPARE:
    case RTX_COMM_COMPARE:
      split = find_split_point (&XEXP (x, 1), insn);
      if (split)
	return split;
      /* ... fall through ...  */
    case RTX_UNARY:
      /* Some machines have (and (shift ...) ...) insns.  If X is not
	 an AND, but XEXP (X, 0) is, use it as our split point.  */
      if (GET_CODE (x) != AND && GET_CODE (XEXP (x, 0)) == AND)
	return &XEXP (x, 0);

      split = find_split_point (&XEXP (x, 0), insn);
      if (split)
	return split;
      return loc;

    default:
      /* Otherwise, we don't have a split point.  */
      return 0;
    }
}

/* Throughout X, replace FROM with TO, and return the result.
   The result is TO if X is FROM;
   otherwise the result is X, but its contents may have been modified.
   If they were modified, a record was made in undobuf so that
   undo_all will (among other things) return X to its original state.

   If the number of changes necessary is too much to record to undo,
   the excess changes are not made, so the result is invalid.
   The changes already made can still be undone.
   undobuf.num_undo is incremented for such changes, so by testing that
   the caller can tell whether the result is valid.

   `n_occurrences' is incremented each time FROM is replaced.

   IN_DEST is nonzero if we are processing the SET_DEST of a SET.

   UNIQUE_COPY is nonzero if each substitution must be unique.  We do this
   by copying if `n_occurrences' is nonzero.  */

static rtx
subst (rtx x, rtx from, rtx to, int in_dest, int unique_copy)
{
  enum rtx_code code = GET_CODE (x);
  enum machine_mode op0_mode = VOIDmode;
  const char *fmt;
  int len, i;
  rtx new;

/* Two expressions are equal if they are identical copies of a shared
   RTX or if they are both registers with the same register number
   and mode.  */

#define COMBINE_RTX_EQUAL_P(X,Y)			\
  ((X) == (Y)						\
   || (REG_P (X) && REG_P (Y)	\
       && REGNO (X) == REGNO (Y) && GET_MODE (X) == GET_MODE (Y)))

  if (! in_dest && COMBINE_RTX_EQUAL_P (x, from))
    {
      n_occurrences++;
      return (unique_copy && n_occurrences > 1 ? copy_rtx (to) : to);
    }

  /* If X and FROM are the same register but different modes, they will
     not have been seen as equal above.  However, flow.c will make a
     LOG_LINKS entry for that case.  If we do nothing, we will try to
     rerecognize our original insn and, when it succeeds, we will
     delete the feeding insn, which is incorrect.

     So force this insn not to match in this (rare) case.  */
  if (! in_dest && code == REG && REG_P (from)
      && REGNO (x) == REGNO (from))
    return gen_rtx_CLOBBER (GET_MODE (x), const0_rtx);

  /* If this is an object, we are done unless it is a MEM or LO_SUM, both
     of which may contain things that can be combined.  */
  if (code != MEM && code != LO_SUM && OBJECT_P (x))
    return x;

  /* It is possible to have a subexpression appear twice in the insn.
     Suppose that FROM is a register that appears within TO.
     Then, after that subexpression has been scanned once by `subst',
     the second time it is scanned, TO may be found.  If we were
     to scan TO here, we would find FROM within it and create a
     self-referent rtl structure which is completely wrong.  */
  if (COMBINE_RTX_EQUAL_P (x, to))
    return to;

  /* Parallel asm_operands need special attention because all of the
     inputs are shared across the arms.  Furthermore, unsharing the
     rtl results in recognition failures.  Failure to handle this case
     specially can result in circular rtl.

     Solve this by doing a normal pass across the first entry of the
     parallel, and only processing the SET_DESTs of the subsequent
     entries.  Ug.  */

  if (code == PARALLEL
      && GET_CODE (XVECEXP (x, 0, 0)) == SET
      && GET_CODE (SET_SRC (XVECEXP (x, 0, 0))) == ASM_OPERANDS)
    {
      new = subst (XVECEXP (x, 0, 0), from, to, 0, unique_copy);

      /* If this substitution failed, this whole thing fails.  */
      if (GET_CODE (new) == CLOBBER
	  && XEXP (new, 0) == const0_rtx)
	return new;

      SUBST (XVECEXP (x, 0, 0), new);

      for (i = XVECLEN (x, 0) - 1; i >= 1; i--)
	{
	  rtx dest = SET_DEST (XVECEXP (x, 0, i));

	  if (!REG_P (dest)
	      && GET_CODE (dest) != CC0
	      && GET_CODE (dest) != PC)
	    {
	      new = subst (dest, from, to, 0, unique_copy);

	      /* If this substitution failed, this whole thing fails.  */
	      if (GET_CODE (new) == CLOBBER
		  && XEXP (new, 0) == const0_rtx)
		return new;

	      SUBST (SET_DEST (XVECEXP (x, 0, i)), new);
	    }
	}
    }
  else
    {
      len = GET_RTX_LENGTH (code);
      fmt = GET_RTX_FORMAT (code);

      /* We don't need to process a SET_DEST that is a register, CC0,
	 or PC, so set up to skip this common case.  All other cases
	 where we want to suppress replacing something inside a
	 SET_SRC are handled via the IN_DEST operand.  */
      if (code == SET
	  && (REG_P (SET_DEST (x))
	      || GET_CODE (SET_DEST (x)) == CC0
	      || GET_CODE (SET_DEST (x)) == PC))
	fmt = "ie";

      /* Get the mode of operand 0 in case X is now a SIGN_EXTEND of a
	 constant.  */
      if (fmt[0] == 'e')
	op0_mode = GET_MODE (XEXP (x, 0));

      for (i = 0; i < len; i++)
	{
	  if (fmt[i] == 'E')
	    {
	      int j;
	      for (j = XVECLEN (x, i) - 1; j >= 0; j--)
		{
		  if (COMBINE_RTX_EQUAL_P (XVECEXP (x, i, j), from))
		    {
		      new = (unique_copy && n_occurrences
			     ? copy_rtx (to) : to);
		      n_occurrences++;
		    }
		  else
		    {
		      new = subst (XVECEXP (x, i, j), from, to, 0,
				   unique_copy);

		      /* If this substitution failed, this whole thing
			 fails.  */
		      if (GET_CODE (new) == CLOBBER
			  && XEXP (new, 0) == const0_rtx)
			return new;
		    }

		  SUBST (XVECEXP (x, i, j), new);
		}
	    }
	  else if (fmt[i] == 'e')
	    {
	      /* If this is a register being set, ignore it.  */
	      new = XEXP (x, i);
	      if (in_dest
		  && i == 0
		  && (((code == SUBREG || code == ZERO_EXTRACT)
		       && REG_P (new))
		      || code == STRICT_LOW_PART))
		;

	      else if (COMBINE_RTX_EQUAL_P (XEXP (x, i), from))
		{
		  /* In general, don't install a subreg involving two
		     modes not tieable.  It can worsen register
		     allocation, and can even make invalid reload
		     insns, since the reg inside may need to be copied
		     from in the outside mode, and that may be invalid
		     if it is an fp reg copied in integer mode.

		     We allow two exceptions to this: It is valid if
		     it is inside another SUBREG and the mode of that
		     SUBREG and the mode of the inside of TO is
		     tieable and it is valid if X is a SET that copies
		     FROM to CC0.  */

		  if (GET_CODE (to) == SUBREG
		      && ! MODES_TIEABLE_P (GET_MODE (to),
					    GET_MODE (SUBREG_REG (to)))
		      && ! (code == SUBREG
			    && MODES_TIEABLE_P (GET_MODE (x),
						GET_MODE (SUBREG_REG (to))))
#ifdef HAVE_cc0
		      && ! (code == SET && i == 1 && XEXP (x, 0) == cc0_rtx)
#endif
		      )
		    return gen_rtx_CLOBBER (VOIDmode, const0_rtx);

#ifdef CANNOT_CHANGE_MODE_CLASS
		  if (code == SUBREG
		      && REG_P (to)
		      && REGNO (to) < FIRST_PSEUDO_REGISTER
		      && REG_CANNOT_CHANGE_MODE_P (REGNO (to),
						   GET_MODE (to),
						   GET_MODE (x)))
		    return gen_rtx_CLOBBER (VOIDmode, const0_rtx);
#endif

		  new = (unique_copy && n_occurrences ? copy_rtx (to) : to);
		  n_occurrences++;
		}
	      else
		/* If we are in a SET_DEST, suppress most cases unless we
		   have gone inside a MEM, in which case we want to
		   simplify the address.  We assume here that things that
		   are actually part of the destination have their inner
		   parts in the first expression.  This is true for SUBREG,
		   STRICT_LOW_PART, and ZERO_EXTRACT, which are the only
		   things aside from REG and MEM that should appear in a
		   SET_DEST.  */
		new = subst (XEXP (x, i), from, to,
			     (((in_dest
				&& (code == SUBREG || code == STRICT_LOW_PART
				    || code == ZERO_EXTRACT))
			       || code == SET)
			      && i == 0), unique_copy);

	      /* If we found that we will have to reject this combination,
		 indicate that by returning the CLOBBER ourselves, rather than
		 an expression containing it.  This will speed things up as
		 well as prevent accidents where two CLOBBERs are considered
		 to be equal, thus producing an incorrect simplification.  */

	      if (GET_CODE (new) == CLOBBER && XEXP (new, 0) == const0_rtx)
		return new;

	      if (GET_CODE (x) == SUBREG
		  && (GET_CODE (new) == CONST_INT
		      || GET_CODE (new) == CONST_DOUBLE))
		{
		  enum machine_mode mode = GET_MODE (x);

		  x = simplify_subreg (GET_MODE (x), new,
				       GET_MODE (SUBREG_REG (x)),
				       SUBREG_BYTE (x));
		  if (! x)
		    x = gen_rtx_CLOBBER (mode, const0_rtx);
		}
	      else if (GET_CODE (new) == CONST_INT
		       && GET_CODE (x) == ZERO_EXTEND)
		{
		  x = simplify_unary_operation (ZERO_EXTEND, GET_MODE (x),
						new, GET_MODE (XEXP (x, 0)));
		  gcc_assert (x);
		}
	      else
		SUBST (XEXP (x, i), new);
	    }
	}
    }

  /* Try to simplify X.  If the simplification changed the code, it is likely
     that further simplification will help, so loop, but limit the number
     of repetitions that will be performed.  */

  for (i = 0; i < 4; i++)
    {
      /* If X is sufficiently simple, don't bother trying to do anything
	 with it.  */
      if (code != CONST_INT && code != REG && code != CLOBBER)
	x = combine_simplify_rtx (x, op0_mode, in_dest);

      if (GET_CODE (x) == code)
	break;

      code = GET_CODE (x);

      /* We no longer know the original mode of operand 0 since we
	 have changed the form of X)  */
      op0_mode = VOIDmode;
    }

  return x;
}

/* Simplify X, a piece of RTL.  We just operate on the expression at the
   outer level; call `subst' to simplify recursively.  Return the new
   expression.

   OP0_MODE is the original mode of XEXP (x, 0).  IN_DEST is nonzero
   if we are inside a SET_DEST.  */

static rtx
combine_simplify_rtx (rtx x, enum machine_mode op0_mode, int in_dest)
{
  enum rtx_code code = GET_CODE (x);
  enum machine_mode mode = GET_MODE (x);
  rtx temp;
  int i;

  /* If this is a commutative operation, put a constant last and a complex
     expression first.  We don't need to do this for comparisons here.  */
  if (COMMUTATIVE_ARITH_P (x)
      && swap_commutative_operands_p (XEXP (x, 0), XEXP (x, 1)))
    {
      temp = XEXP (x, 0);
      SUBST (XEXP (x, 0), XEXP (x, 1));
      SUBST (XEXP (x, 1), temp);
    }

  /* If this is a simple operation applied to an IF_THEN_ELSE, try
     applying it to the arms of the IF_THEN_ELSE.  This often simplifies
     things.  Check for cases where both arms are testing the same
     condition.

     Don't do anything if all operands are very simple.  */

  if ((BINARY_P (x)
       && ((!OBJECT_P (XEXP (x, 0))
	    && ! (GET_CODE (XEXP (x, 0)) == SUBREG
		  && OBJECT_P (SUBREG_REG (XEXP (x, 0)))))
	   || (!OBJECT_P (XEXP (x, 1))
	       && ! (GET_CODE (XEXP (x, 1)) == SUBREG
		     && OBJECT_P (SUBREG_REG (XEXP (x, 1)))))))
      || (UNARY_P (x)
	  && (!OBJECT_P (XEXP (x, 0))
	       && ! (GET_CODE (XEXP (x, 0)) == SUBREG
		     && OBJECT_P (SUBREG_REG (XEXP (x, 0)))))))
    {
      rtx cond, true_rtx, false_rtx;

      cond = if_then_else_cond (x, &true_rtx, &false_rtx);
      if (cond != 0
	  /* If everything is a comparison, what we have is highly unlikely
	     to be simpler, so don't use it.  */
	  && ! (COMPARISON_P (x)
		&& (COMPARISON_P (true_rtx) || COMPARISON_P (false_rtx))))
	{
	  rtx cop1 = const0_rtx;
	  enum rtx_code cond_code = simplify_comparison (NE, &cond, &cop1);

	  if (cond_code == NE && COMPARISON_P (cond))
	    return x;

	  /* Simplify the alternative arms; this may collapse the true and
	     false arms to store-flag values.  Be careful to use copy_rtx
	     here since true_rtx or false_rtx might share RTL with x as a
	     result of the if_then_else_cond call above.  */
	  true_rtx = subst (copy_rtx (true_rtx), pc_rtx, pc_rtx, 0, 0);
	  false_rtx = subst (copy_rtx (false_rtx), pc_rtx, pc_rtx, 0, 0);

	  /* If true_rtx and false_rtx are not general_operands, an if_then_else
	     is unlikely to be simpler.  */
	  if (general_operand (true_rtx, VOIDmode)
	      && general_operand (false_rtx, VOIDmode))
	    {
	      enum rtx_code reversed;

	      /* Restarting if we generate a store-flag expression will cause
		 us to loop.  Just drop through in this case.  */

	      /* If the result values are STORE_FLAG_VALUE and zero, we can
		 just make the comparison operation.  */
	      if (true_rtx == const_true_rtx && false_rtx == const0_rtx)
		x = simplify_gen_relational (cond_code, mode, VOIDmode,
					     cond, cop1);
	      else if (true_rtx == const0_rtx && false_rtx == const_true_rtx
		       && ((reversed = reversed_comparison_code_parts
					(cond_code, cond, cop1, NULL))
			   != UNKNOWN))
		x = simplify_gen_relational (reversed, mode, VOIDmode,
					     cond, cop1);

	      /* Likewise, we can make the negate of a comparison operation
		 if the result values are - STORE_FLAG_VALUE and zero.  */
	      else if (GET_CODE (true_rtx) == CONST_INT
		       && INTVAL (true_rtx) == - STORE_FLAG_VALUE
		       && false_rtx == const0_rtx)
		x = simplify_gen_unary (NEG, mode,
					simplify_gen_relational (cond_code,
								 mode, VOIDmode,
								 cond, cop1),
					mode);
	      else if (GET_CODE (false_rtx) == CONST_INT
		       && INTVAL (false_rtx) == - STORE_FLAG_VALUE
		       && true_rtx == const0_rtx
		       && ((reversed = reversed_comparison_code_parts
					(cond_code, cond, cop1, NULL))
			   != UNKNOWN))
		x = simplify_gen_unary (NEG, mode,
					simplify_gen_relational (reversed,
								 mode, VOIDmode,
								 cond, cop1),
					mode);
	      else
		return gen_rtx_IF_THEN_ELSE (mode,
					     simplify_gen_relational (cond_code,
								      mode,
								      VOIDmode,
								      cond,
								      cop1),
					     true_rtx, false_rtx);

	      code = GET_CODE (x);
	      op0_mode = VOIDmode;
	    }
	}
    }

  /* Try to fold this expression in case we have constants that weren't
     present before.  */
  temp = 0;
  switch (GET_RTX_CLASS (code))
    {
    case RTX_UNARY:
      if (op0_mode == VOIDmode)
	op0_mode = GET_MODE (XEXP (x, 0));
      temp = simplify_unary_operation (code, mode, XEXP (x, 0), op0_mode);
      break;
    case RTX_COMPARE:
    case RTX_COMM_COMPARE:
      {
	enum machine_mode cmp_mode = GET_MODE (XEXP (x, 0));
	if (cmp_mode == VOIDmode)
	  {
	    cmp_mode = GET_MODE (XEXP (x, 1));
	    if (cmp_mode == VOIDmode)
	      cmp_mode = op0_mode;
	  }
	temp = simplify_relational_operation (code, mode, cmp_mode,
					      XEXP (x, 0), XEXP (x, 1));
      }
      break;
    case RTX_COMM_ARITH:
    case RTX_BIN_ARITH:
      temp = simplify_binary_operation (code, mode, XEXP (x, 0), XEXP (x, 1));
      break;
    case RTX_BITFIELD_OPS:
    case RTX_TERNARY:
      temp = simplify_ternary_operation (code, mode, op0_mode, XEXP (x, 0),
					 XEXP (x, 1), XEXP (x, 2));
      break;
    default:
      break;
    }

  if (temp)
    {
      x = temp;
      code = GET_CODE (temp);
      op0_mode = VOIDmode;
      mode = GET_MODE (temp);
    }

  /* First see if we can apply the inverse distributive law.  */
  if (code == PLUS || code == MINUS
      || code == AND || code == IOR || code == XOR)
    {
      x = apply_distributive_law (x);
      code = GET_CODE (x);
      op0_mode = VOIDmode;
    }

  /* If CODE is an associative operation not otherwise handled, see if we
     can associate some operands.  This can win if they are constants or
     if they are logically related (i.e. (a & b) & a).  */
  if ((code == PLUS || code == MINUS || code == MULT || code == DIV
       || code == AND || code == IOR || code == XOR
       || code == SMAX || code == SMIN || code == UMAX || code == UMIN)
      && ((INTEGRAL_MODE_P (mode) && code != DIV)
	  || (flag_unsafe_math_optimizations && FLOAT_MODE_P (mode))))
    {
      if (GET_CODE (XEXP (x, 0)) == code)
	{
	  rtx other = XEXP (XEXP (x, 0), 0);
	  rtx inner_op0 = XEXP (XEXP (x, 0), 1);
	  rtx inner_op1 = XEXP (x, 1);
	  rtx inner;

	  /* Make sure we pass the constant operand if any as the second
	     one if this is a commutative operation.  */
	  if (CONSTANT_P (inner_op0) && COMMUTATIVE_ARITH_P (x))
	    {
	      rtx tem = inner_op0;
	      inner_op0 = inner_op1;
	      inner_op1 = tem;
	    }
	  inner = simplify_binary_operation (code == MINUS ? PLUS
					     : code == DIV ? MULT
					     : code,
					     mode, inner_op0, inner_op1);

	  /* For commutative operations, try the other pair if that one
	     didn't simplify.  */
	  if (inner == 0 && COMMUTATIVE_ARITH_P (x))
	    {
	      other = XEXP (XEXP (x, 0), 1);
	      inner = simplify_binary_operation (code, mode,
						 XEXP (XEXP (x, 0), 0),
						 XEXP (x, 1));
	    }

	  if (inner)
	    return simplify_gen_binary (code, mode, other, inner);
	}
    }

  /* A little bit of algebraic simplification here.  */
  switch (code)
    {
    case MEM:
      /* Ensure that our address has any ASHIFTs converted to MULT in case
	 address-recognizing predicates are called later.  */
      temp = make_compound_operation (XEXP (x, 0), MEM);
      SUBST (XEXP (x, 0), temp);
      break;

    case SUBREG:
      if (op0_mode == VOIDmode)
	op0_mode = GET_MODE (SUBREG_REG (x));

      /* See if this can be moved to simplify_subreg.  */
      if (CONSTANT_P (SUBREG_REG (x))
	  && subreg_lowpart_offset (mode, op0_mode) == SUBREG_BYTE (x)
	     /* Don't call gen_lowpart if the inner mode
		is VOIDmode and we cannot simplify it, as SUBREG without
		inner mode is invalid.  */
	  && (GET_MODE (SUBREG_REG (x)) != VOIDmode
	      || gen_lowpart_common (mode, SUBREG_REG (x))))
	return gen_lowpart (mode, SUBREG_REG (x));

      if (GET_MODE_CLASS (GET_MODE (SUBREG_REG (x))) == MODE_CC)
	break;
      {
	rtx temp;
	temp = simplify_subreg (mode, SUBREG_REG (x), op0_mode,
				SUBREG_BYTE (x));
	if (temp)
	  return temp;
      }

      /* Don't change the mode of the MEM if that would change the meaning
	 of the address.  */
      if (MEM_P (SUBREG_REG (x))
	  && (MEM_VOLATILE_P (SUBREG_REG (x))
	      || mode_dependent_address_p (XEXP (SUBREG_REG (x), 0))))
	return gen_rtx_CLOBBER (mode, const0_rtx);

      /* Note that we cannot do any narrowing for non-constants since
	 we might have been counting on using the fact that some bits were
	 zero.  We now do this in the SET.  */

      break;

    case NEG:
      temp = expand_compound_operation (XEXP (x, 0));

      /* For C equal to the width of MODE minus 1, (neg (ashiftrt X C)) can be
	 replaced by (lshiftrt X C).  This will convert
	 (neg (sign_extract X 1 Y)) to (zero_extract X 1 Y).  */

      if (GET_CODE (temp) == ASHIFTRT
	  && GET_CODE (XEXP (temp, 1)) == CONST_INT
	  && INTVAL (XEXP (temp, 1)) == GET_MODE_BITSIZE (mode) - 1)
	return simplify_shift_const (NULL_RTX, LSHIFTRT, mode, XEXP (temp, 0),
				     INTVAL (XEXP (temp, 1)));

      /* If X has only a single bit that might be nonzero, say, bit I, convert
	 (neg X) to (ashiftrt (ashift X C-I) C-I) where C is the bitsize of
	 MODE minus 1.  This will convert (neg (zero_extract X 1 Y)) to
	 (sign_extract X 1 Y).  But only do this if TEMP isn't a register
	 or a SUBREG of one since we'd be making the expression more
	 complex if it was just a register.  */

      if (!REG_P (temp)
	  && ! (GET_CODE (temp) == SUBREG
		&& REG_P (SUBREG_REG (temp)))
	  && (i = exact_log2 (nonzero_bits (temp, mode))) >= 0)
	{
	  rtx temp1 = simplify_shift_const
	    (NULL_RTX, ASHIFTRT, mode,
	     simplify_shift_const (NULL_RTX, ASHIFT, mode, temp,
				   GET_MODE_BITSIZE (mode) - 1 - i),
	     GET_MODE_BITSIZE (mode) - 1 - i);

	  /* If all we did was surround TEMP with the two shifts, we
	     haven't improved anything, so don't use it.  Otherwise,
	     we are better off with TEMP1.  */
	  if (GET_CODE (temp1) != ASHIFTRT
	      || GET_CODE (XEXP (temp1, 0)) != ASHIFT
	      || XEXP (XEXP (temp1, 0), 0) != temp)
	    return temp1;
	}
      break;

    case TRUNCATE:
      /* We can't handle truncation to a partial integer mode here
	 because we don't know the real bitsize of the partial
	 integer mode.  */
      if (GET_MODE_CLASS (mode) == MODE_PARTIAL_INT)
	break;

      if (GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	  && TRULY_NOOP_TRUNCATION (GET_MODE_BITSIZE (mode),
				    GET_MODE_BITSIZE (GET_MODE (XEXP (x, 0)))))
	SUBST (XEXP (x, 0),
	       force_to_mode (XEXP (x, 0), GET_MODE (XEXP (x, 0)),
			      GET_MODE_MASK (mode), 0));

      /* Similarly to what we do in simplify-rtx.c, a truncate of a register
	 whose value is a comparison can be replaced with a subreg if
	 STORE_FLAG_VALUE permits.  */
      if (GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	  && ((HOST_WIDE_INT) STORE_FLAG_VALUE & ~GET_MODE_MASK (mode)) == 0
	  && (temp = get_last_value (XEXP (x, 0)))
	  && COMPARISON_P (temp))
	return gen_lowpart (mode, XEXP (x, 0));
      break;

#ifdef HAVE_cc0
    case COMPARE:
      /* Convert (compare FOO (const_int 0)) to FOO unless we aren't
	 using cc0, in which case we want to leave it as a COMPARE
	 so we can distinguish it from a register-register-copy.  */
      if (XEXP (x, 1) == const0_rtx)
	return XEXP (x, 0);

      /* x - 0 is the same as x unless x's mode has signed zeros and
	 allows rounding towards -infinity.  Under those conditions,
	 0 - 0 is -0.  */
      if (!(HONOR_SIGNED_ZEROS (GET_MODE (XEXP (x, 0)))
	    && HONOR_SIGN_DEPENDENT_ROUNDING (GET_MODE (XEXP (x, 0))))
	  && XEXP (x, 1) == CONST0_RTX (GET_MODE (XEXP (x, 0))))
	return XEXP (x, 0);
      break;
#endif

    case CONST:
      /* (const (const X)) can become (const X).  Do it this way rather than
	 returning the inner CONST since CONST can be shared with a
	 REG_EQUAL note.  */
      if (GET_CODE (XEXP (x, 0)) == CONST)
	SUBST (XEXP (x, 0), XEXP (XEXP (x, 0), 0));
      break;

#ifdef HAVE_lo_sum
    case LO_SUM:
      /* Convert (lo_sum (high FOO) FOO) to FOO.  This is necessary so we
	 can add in an offset.  find_split_point will split this address up
	 again if it doesn't match.  */
      if (GET_CODE (XEXP (x, 0)) == HIGH
	  && rtx_equal_p (XEXP (XEXP (x, 0), 0), XEXP (x, 1)))
	return XEXP (x, 1);
      break;
#endif

    case PLUS:
      /* (plus (xor (and <foo> (const_int pow2 - 1)) <c>) <-c>)
	 when c is (const_int (pow2 + 1) / 2) is a sign extension of a
	 bit-field and can be replaced by either a sign_extend or a
	 sign_extract.  The `and' may be a zero_extend and the two
	 <c>, -<c> constants may be reversed.  */
      if (GET_CODE (XEXP (x, 0)) == XOR
	  && GET_CODE (XEXP (x, 1)) == CONST_INT
	  && GET_CODE (XEXP (XEXP (x, 0), 1)) == CONST_INT
	  && INTVAL (XEXP (x, 1)) == -INTVAL (XEXP (XEXP (x, 0), 1))
	  && ((i = exact_log2 (INTVAL (XEXP (XEXP (x, 0), 1)))) >= 0
	      || (i = exact_log2 (INTVAL (XEXP (x, 1)))) >= 0)
	  && GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	  && ((GET_CODE (XEXP (XEXP (x, 0), 0)) == AND
	       && GET_CODE (XEXP (XEXP (XEXP (x, 0), 0), 1)) == CONST_INT
	       && (INTVAL (XEXP (XEXP (XEXP (x, 0), 0), 1))
		   == ((HOST_WIDE_INT) 1 << (i + 1)) - 1))
	      || (GET_CODE (XEXP (XEXP (x, 0), 0)) == ZERO_EXTEND
		  && (GET_MODE_BITSIZE (GET_MODE (XEXP (XEXP (XEXP (x, 0), 0), 0)))
		      == (unsigned int) i + 1))))
	return simplify_shift_const
	  (NULL_RTX, ASHIFTRT, mode,
	   simplify_shift_const (NULL_RTX, ASHIFT, mode,
				 XEXP (XEXP (XEXP (x, 0), 0), 0),
				 GET_MODE_BITSIZE (mode) - (i + 1)),
	   GET_MODE_BITSIZE (mode) - (i + 1));

      /* If only the low-order bit of X is possibly nonzero, (plus x -1)
	 can become (ashiftrt (ashift (xor x 1) C) C) where C is
	 the bitsize of the mode - 1.  This allows simplification of
	 "a = (b & 8) == 0;"  */
      if (XEXP (x, 1) == constm1_rtx
	  && !REG_P (XEXP (x, 0))
	  && ! (GET_CODE (XEXP (x, 0)) == SUBREG
		&& REG_P (SUBREG_REG (XEXP (x, 0))))
	  && nonzero_bits (XEXP (x, 0), mode) == 1)
	return simplify_shift_const (NULL_RTX, ASHIFTRT, mode,
	   simplify_shift_const (NULL_RTX, ASHIFT, mode,
				 gen_rtx_XOR (mode, XEXP (x, 0), const1_rtx),
				 GET_MODE_BITSIZE (mode) - 1),
	   GET_MODE_BITSIZE (mode) - 1);

      /* If we are adding two things that have no bits in common, convert
	 the addition into an IOR.  This will often be further simplified,
	 for example in cases like ((a & 1) + (a & 2)), which can
	 become a & 3.  */

      if (GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	  && (nonzero_bits (XEXP (x, 0), mode)
	      & nonzero_bits (XEXP (x, 1), mode)) == 0)
	{
	  /* Try to simplify the expression further.  */
	  rtx tor = simplify_gen_binary (IOR, mode, XEXP (x, 0), XEXP (x, 1));
	  temp = combine_simplify_rtx (tor, mode, in_dest);

	  /* If we could, great.  If not, do not go ahead with the IOR
	     replacement, since PLUS appears in many special purpose
	     address arithmetic instructions.  */
	  if (GET_CODE (temp) != CLOBBER && temp != tor)
	    return temp;
	}
      break;

    case MINUS:
      /* (minus <foo> (and <foo> (const_int -pow2))) becomes
	 (and <foo> (const_int pow2-1))  */
      if (GET_CODE (XEXP (x, 1)) == AND
	  && GET_CODE (XEXP (XEXP (x, 1), 1)) == CONST_INT
	  && exact_log2 (-INTVAL (XEXP (XEXP (x, 1), 1))) >= 0
	  && rtx_equal_p (XEXP (XEXP (x, 1), 0), XEXP (x, 0)))
	return simplify_and_const_int (NULL_RTX, mode, XEXP (x, 0),
				       -INTVAL (XEXP (XEXP (x, 1), 1)) - 1);
      break;

    case MULT:
      /* If we have (mult (plus A B) C), apply the distributive law and then
	 the inverse distributive law to see if things simplify.  This
	 occurs mostly in addresses, often when unrolling loops.  */

      if (GET_CODE (XEXP (x, 0)) == PLUS)
	{
	  rtx result = distribute_and_simplify_rtx (x, 0);
	  if (result)
	    return result;
	}

      /* Try simplify a*(b/c) as (a*b)/c.  */
      if (FLOAT_MODE_P (mode) && flag_unsafe_math_optimizations
	  && GET_CODE (XEXP (x, 0)) == DIV)
	{
	  rtx tem = simplify_binary_operation (MULT, mode,
					       XEXP (XEXP (x, 0), 0),
					       XEXP (x, 1));
	  if (tem)
	    return simplify_gen_binary (DIV, mode, tem, XEXP (XEXP (x, 0), 1));
	}
      break;

    case UDIV:
      /* If this is a divide by a power of two, treat it as a shift if
	 its first operand is a shift.  */
      if (GET_CODE (XEXP (x, 1)) == CONST_INT
	  && (i = exact_log2 (INTVAL (XEXP (x, 1)))) >= 0
	  && (GET_CODE (XEXP (x, 0)) == ASHIFT
	      || GET_CODE (XEXP (x, 0)) == LSHIFTRT
	      || GET_CODE (XEXP (x, 0)) == ASHIFTRT
	      || GET_CODE (XEXP (x, 0)) == ROTATE
	      || GET_CODE (XEXP (x, 0)) == ROTATERT))
	return simplify_shift_const (NULL_RTX, LSHIFTRT, mode, XEXP (x, 0), i);
      break;

    case EQ:  case NE:
    case GT:  case GTU:  case GE:  case GEU:
    case LT:  case LTU:  case LE:  case LEU:
    case UNEQ:  case LTGT:
    case UNGT:  case UNGE:
    case UNLT:  case UNLE:
    case UNORDERED: case ORDERED:
      /* If the first operand is a condition code, we can't do anything
	 with it.  */
      if (GET_CODE (XEXP (x, 0)) == COMPARE
	  || (GET_MODE_CLASS (GET_MODE (XEXP (x, 0))) != MODE_CC
	      && ! CC0_P (XEXP (x, 0))))
	{
	  rtx op0 = XEXP (x, 0);
	  rtx op1 = XEXP (x, 1);
	  enum rtx_code new_code;

	  if (GET_CODE (op0) == COMPARE)
	    op1 = XEXP (op0, 1), op0 = XEXP (op0, 0);

	  /* Simplify our comparison, if possible.  */
	  new_code = simplify_comparison (code, &op0, &op1);

	  /* If STORE_FLAG_VALUE is 1, we can convert (ne x 0) to simply X
	     if only the low-order bit is possibly nonzero in X (such as when
	     X is a ZERO_EXTRACT of one bit).  Similarly, we can convert EQ to
	     (xor X 1) or (minus 1 X); we use the former.  Finally, if X is
	     known to be either 0 or -1, NE becomes a NEG and EQ becomes
	     (plus X 1).

	     Remove any ZERO_EXTRACT we made when thinking this was a
	     comparison.  It may now be simpler to use, e.g., an AND.  If a
	     ZERO_EXTRACT is indeed appropriate, it will be placed back by
	     the call to make_compound_operation in the SET case.  */

	  if (STORE_FLAG_VALUE == 1
	      && new_code == NE && GET_MODE_CLASS (mode) == MODE_INT
	      && op1 == const0_rtx
	      && mode == GET_MODE (op0)
	      && nonzero_bits (op0, mode) == 1)
	    return gen_lowpart (mode,
				expand_compound_operation (op0));

	  else if (STORE_FLAG_VALUE == 1
		   && new_code == NE && GET_MODE_CLASS (mode) == MODE_INT
		   && op1 == const0_rtx
		   && mode == GET_MODE (op0)
		   && (num_sign_bit_copies (op0, mode)
		       == GET_MODE_BITSIZE (mode)))
	    {
	      op0 = expand_compound_operation (op0);
	      return simplify_gen_unary (NEG, mode,
					 gen_lowpart (mode, op0),
					 mode);
	    }

	  else if (STORE_FLAG_VALUE == 1
		   && new_code == EQ && GET_MODE_CLASS (mode) == MODE_INT
		   && op1 == const0_rtx
		   && mode == GET_MODE (op0)
		   && nonzero_bits (op0, mode) == 1)
	    {
	      op0 = expand_compound_operation (op0);
	      return simplify_gen_binary (XOR, mode,
					  gen_lowpart (mode, op0),
					  const1_rtx);
	    }

	  else if (STORE_FLAG_VALUE == 1
		   && new_code == EQ && GET_MODE_CLASS (mode) == MODE_INT
		   && op1 == const0_rtx
		   && mode == GET_MODE (op0)
		   && (num_sign_bit_copies (op0, mode)
		       == GET_MODE_BITSIZE (mode)))
	    {
	      op0 = expand_compound_operation (op0);
	      return plus_constant (gen_lowpart (mode, op0), 1);
	    }

	  /* If STORE_FLAG_VALUE is -1, we have cases similar to
	     those above.  */
	  if (STORE_FLAG_VALUE == -1
	      && new_code == NE && GET_MODE_CLASS (mode) == MODE_INT
	      && op1 == const0_rtx
	      && (num_sign_bit_copies (op0, mode)
		  == GET_MODE_BITSIZE (mode)))
	    return gen_lowpart (mode,
				expand_compound_operation (op0));

	  else if (STORE_FLAG_VALUE == -1
		   && new_code == NE && GET_MODE_CLASS (mode) == MODE_INT
		   && op1 == const0_rtx
		   && mode == GET_MODE (op0)
		   && nonzero_bits (op0, mode) == 1)
	    {
	      op0 = expand_compound_operation (op0);
	      return simplify_gen_unary (NEG, mode,
					 gen_lowpart (mode, op0),
					 mode);
	    }

	  else if (STORE_FLAG_VALUE == -1
		   && new_code == EQ && GET_MODE_CLASS (mode) == MODE_INT
		   && op1 == const0_rtx
		   && mode == GET_MODE (op0)
		   && (num_sign_bit_copies (op0, mode)
		       == GET_MODE_BITSIZE (mode)))
	    {
	      op0 = expand_compound_operation (op0);
	      return simplify_gen_unary (NOT, mode,
					 gen_lowpart (mode, op0),
					 mode);
	    }

	  /* If X is 0/1, (eq X 0) is X-1.  */
	  else if (STORE_FLAG_VALUE == -1
		   && new_code == EQ && GET_MODE_CLASS (mode) == MODE_INT
		   && op1 == const0_rtx
		   && mode == GET_MODE (op0)
		   && nonzero_bits (op0, mode) == 1)
	    {
	      op0 = expand_compound_operation (op0);
	      return plus_constant (gen_lowpart (mode, op0), -1);
	    }

	  /* If STORE_FLAG_VALUE says to just test the sign bit and X has just
	     one bit that might be nonzero, we can convert (ne x 0) to
	     (ashift x c) where C puts the bit in the sign bit.  Remove any
	     AND with STORE_FLAG_VALUE when we are done, since we are only
	     going to test the sign bit.  */
	  if (new_code == NE && GET_MODE_CLASS (mode) == MODE_INT
	      && GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	      && ((STORE_FLAG_VALUE & GET_MODE_MASK (mode))
		  == (unsigned HOST_WIDE_INT) 1 << (GET_MODE_BITSIZE (mode) - 1))
	      && op1 == const0_rtx
	      && mode == GET_MODE (op0)
	      && (i = exact_log2 (nonzero_bits (op0, mode))) >= 0)
	    {
	      x = simplify_shift_const (NULL_RTX, ASHIFT, mode,
					expand_compound_operation (op0),
					GET_MODE_BITSIZE (mode) - 1 - i);
	      if (GET_CODE (x) == AND && XEXP (x, 1) == const_true_rtx)
		return XEXP (x, 0);
	      else
		return x;
	    }

	  /* If the code changed, return a whole new comparison.  */
	  if (new_code != code)
	    return gen_rtx_fmt_ee (new_code, mode, op0, op1);

	  /* Otherwise, keep this operation, but maybe change its operands.
	     This also converts (ne (compare FOO BAR) 0) to (ne FOO BAR).  */
	  SUBST (XEXP (x, 0), op0);
	  SUBST (XEXP (x, 1), op1);
	}
      break;

    case IF_THEN_ELSE:
      return simplify_if_then_else (x);

    case ZERO_EXTRACT:
    case SIGN_EXTRACT:
    case ZERO_EXTEND:
    case SIGN_EXTEND:
      /* If we are processing SET_DEST, we are done.  */
      if (in_dest)
	return x;

      return expand_compound_operation (x);

    case SET:
      return simplify_set (x);

    case AND:
    case IOR:
      return simplify_logical (x);

    case ASHIFT:
    case LSHIFTRT:
    case ASHIFTRT:
    case ROTATE:
    case ROTATERT:
      /* If this is a shift by a constant amount, simplify it.  */
      if (GET_CODE (XEXP (x, 1)) == CONST_INT)
	return simplify_shift_const (x, code, mode, XEXP (x, 0),
				     INTVAL (XEXP (x, 1)));

      else if (SHIFT_COUNT_TRUNCATED && !REG_P (XEXP (x, 1)))
	SUBST (XEXP (x, 1),
	       force_to_mode (XEXP (x, 1), GET_MODE (XEXP (x, 1)),
			      ((HOST_WIDE_INT) 1
			       << exact_log2 (GET_MODE_BITSIZE (GET_MODE (x))))
			      - 1,
			      0));
      break;

    default:
      break;
    }

  return x;
}

/* Simplify X, an IF_THEN_ELSE expression.  Return the new expression.  */

static rtx
simplify_if_then_else (rtx x)
{
  enum machine_mode mode = GET_MODE (x);
  rtx cond = XEXP (x, 0);
  rtx true_rtx = XEXP (x, 1);
  rtx false_rtx = XEXP (x, 2);
  enum rtx_code true_code = GET_CODE (cond);
  int comparison_p = COMPARISON_P (cond);
  rtx temp;
  int i;
  enum rtx_code false_code;
  rtx reversed;

  /* Simplify storing of the truth value.  */
  if (comparison_p && true_rtx == const_true_rtx && false_rtx == const0_rtx)
    return simplify_gen_relational (true_code, mode, VOIDmode,
				    XEXP (cond, 0), XEXP (cond, 1));

  /* Also when the truth value has to be reversed.  */
  if (comparison_p
      && true_rtx == const0_rtx && false_rtx == const_true_rtx
      && (reversed = reversed_comparison (cond, mode)))
    return reversed;

  /* Sometimes we can simplify the arm of an IF_THEN_ELSE if a register used
     in it is being compared against certain values.  Get the true and false
     comparisons and see if that says anything about the value of each arm.  */

  if (comparison_p
      && ((false_code = reversed_comparison_code (cond, NULL))
	  != UNKNOWN)
      && REG_P (XEXP (cond, 0)))
    {
      HOST_WIDE_INT nzb;
      rtx from = XEXP (cond, 0);
      rtx true_val = XEXP (cond, 1);
      rtx false_val = true_val;
      int swapped = 0;

      /* If FALSE_CODE is EQ, swap the codes and arms.  */

      if (false_code == EQ)
	{
	  swapped = 1, true_code = EQ, false_code = NE;
	  temp = true_rtx, true_rtx = false_rtx, false_rtx = temp;
	}

      /* If we are comparing against zero and the expression being tested has
	 only a single bit that might be nonzero, that is its value when it is
	 not equal to zero.  Similarly if it is known to be -1 or 0.  */

      if (true_code == EQ && true_val == const0_rtx
	  && exact_log2 (nzb = nonzero_bits (from, GET_MODE (from))) >= 0)
	false_code = EQ, false_val = GEN_INT (nzb);
      else if (true_code == EQ && true_val == const0_rtx
	       && (num_sign_bit_copies (from, GET_MODE (from))
		   == GET_MODE_BITSIZE (GET_MODE (from))))
	false_code = EQ, false_val = constm1_rtx;

      /* Now simplify an arm if we know the value of the register in the
	 branch and it is used in the arm.  Be careful due to the potential
	 of locally-shared RTL.  */

      if (reg_mentioned_p (from, true_rtx))
	true_rtx = subst (known_cond (copy_rtx (true_rtx), true_code,
				      from, true_val),
		      pc_rtx, pc_rtx, 0, 0);
      if (reg_mentioned_p (from, false_rtx))
	false_rtx = subst (known_cond (copy_rtx (false_rtx), false_code,
				   from, false_val),
		       pc_rtx, pc_rtx, 0, 0);

      SUBST (XEXP (x, 1), swapped ? false_rtx : true_rtx);
      SUBST (XEXP (x, 2), swapped ? true_rtx : false_rtx);

      true_rtx = XEXP (x, 1);
      false_rtx = XEXP (x, 2);
      true_code = GET_CODE (cond);
    }

  /* If we have (if_then_else FOO (pc) (label_ref BAR)) and FOO can be
     reversed, do so to avoid needing two sets of patterns for
     subtract-and-branch insns.  Similarly if we have a constant in the true
     arm, the false arm is the same as the first operand of the comparison, or
     the false arm is more complicated than the true arm.  */

  if (comparison_p
      && reversed_comparison_code (cond, NULL) != UNKNOWN
      && (true_rtx == pc_rtx
	  || (CONSTANT_P (true_rtx)
	      && GET_CODE (false_rtx) != CONST_INT && false_rtx != pc_rtx)
	  || true_rtx == const0_rtx
	  || (OBJECT_P (true_rtx) && !OBJECT_P (false_rtx))
	  || (GET_CODE (true_rtx) == SUBREG && OBJECT_P (SUBREG_REG (true_rtx))
	      && !OBJECT_P (false_rtx))
	  || reg_mentioned_p (true_rtx, false_rtx)
	  || rtx_equal_p (false_rtx, XEXP (cond, 0))))
    {
      true_code = reversed_comparison_code (cond, NULL);
      SUBST (XEXP (x, 0), reversed_comparison (cond, GET_MODE (cond)));
      SUBST (XEXP (x, 1), false_rtx);
      SUBST (XEXP (x, 2), true_rtx);

      temp = true_rtx, true_rtx = false_rtx, false_rtx = temp;
      cond = XEXP (x, 0);

      /* It is possible that the conditional has been simplified out.  */
      true_code = GET_CODE (cond);
      comparison_p = COMPARISON_P (cond);
    }

  /* If the two arms are identical, we don't need the comparison.  */

  if (rtx_equal_p (true_rtx, false_rtx) && ! side_effects_p (cond))
    return true_rtx;

  /* Convert a == b ? b : a to "a".  */
  if (true_code == EQ && ! side_effects_p (cond)
      && !HONOR_NANS (mode)
      && rtx_equal_p (XEXP (cond, 0), false_rtx)
      && rtx_equal_p (XEXP (cond, 1), true_rtx))
    return false_rtx;
  else if (true_code == NE && ! side_effects_p (cond)
	   && !HONOR_NANS (mode)
	   && rtx_equal_p (XEXP (cond, 0), true_rtx)
	   && rtx_equal_p (XEXP (cond, 1), false_rtx))
    return true_rtx;

  /* Look for cases where we have (abs x) or (neg (abs X)).  */

  if (GET_MODE_CLASS (mode) == MODE_INT
      && GET_CODE (false_rtx) == NEG
      && rtx_equal_p (true_rtx, XEXP (false_rtx, 0))
      && comparison_p
      && rtx_equal_p (true_rtx, XEXP (cond, 0))
      && ! side_effects_p (true_rtx))
    switch (true_code)
      {
      case GT:
      case GE:
	return simplify_gen_unary (ABS, mode, true_rtx, mode);
      case LT:
      case LE:
	return
	  simplify_gen_unary (NEG, mode,
			      simplify_gen_unary (ABS, mode, true_rtx, mode),
			      mode);
      default:
	break;
      }

  /* Look for MIN or MAX.  */

  if ((! FLOAT_MODE_P (mode) || flag_unsafe_math_optimizations)
      && comparison_p
      && rtx_equal_p (XEXP (cond, 0), true_rtx)
      && rtx_equal_p (XEXP (cond, 1), false_rtx)
      && ! side_effects_p (cond))
    switch (true_code)
      {
      case GE:
      case GT:
	return simplify_gen_binary (SMAX, mode, true_rtx, false_rtx);
      case LE:
      case LT:
	return simplify_gen_binary (SMIN, mode, true_rtx, false_rtx);
      case GEU:
      case GTU:
	return simplify_gen_binary (UMAX, mode, true_rtx, false_rtx);
      case LEU:
      case LTU:
	return simplify_gen_binary (UMIN, mode, true_rtx, false_rtx);
      default:
	break;
      }

  /* If we have (if_then_else COND (OP Z C1) Z) and OP is an identity when its
     second operand is zero, this can be done as (OP Z (mult COND C2)) where
     C2 = C1 * STORE_FLAG_VALUE. Similarly if OP has an outer ZERO_EXTEND or
     SIGN_EXTEND as long as Z is already extended (so we don't destroy it).
     We can do this kind of thing in some cases when STORE_FLAG_VALUE is
     neither 1 or -1, but it isn't worth checking for.  */

  if ((STORE_FLAG_VALUE == 1 || STORE_FLAG_VALUE == -1)
      && comparison_p
      && GET_MODE_CLASS (mode) == MODE_INT
      && ! side_effects_p (x))
    {
      rtx t = make_compound_operation (true_rtx, SET);
      rtx f = make_compound_operation (false_rtx, SET);
      rtx cond_op0 = XEXP (cond, 0);
      rtx cond_op1 = XEXP (cond, 1);
      enum rtx_code op = UNKNOWN, extend_op = UNKNOWN;
      enum machine_mode m = mode;
      rtx z = 0, c1 = NULL_RTX;

      if ((GET_CODE (t) == PLUS || GET_CODE (t) == MINUS
	   || GET_CODE (t) == IOR || GET_CODE (t) == XOR
	   || GET_CODE (t) == ASHIFT
	   || GET_CODE (t) == LSHIFTRT || GET_CODE (t) == ASHIFTRT)
	  && rtx_equal_p (XEXP (t, 0), f))
	c1 = XEXP (t, 1), op = GET_CODE (t), z = f;

      /* If an identity-zero op is commutative, check whether there
	 would be a match if we swapped the operands.  */
      else if ((GET_CODE (t) == PLUS || GET_CODE (t) == IOR
		|| GET_CODE (t) == XOR)
	       && rtx_equal_p (XEXP (t, 1), f))
	c1 = XEXP (t, 0), op = GET_CODE (t), z = f;
      else if (GET_CODE (t) == SIGN_EXTEND
	       && (GET_CODE (XEXP (t, 0)) == PLUS
		   || GET_CODE (XEXP (t, 0)) == MINUS
		   || GET_CODE (XEXP (t, 0)) == IOR
		   || GET_CODE (XEXP (t, 0)) == XOR
		   || GET_CODE (XEXP (t, 0)) == ASHIFT
		   || GET_CODE (XEXP (t, 0)) == LSHIFTRT
		   || GET_CODE (XEXP (t, 0)) == ASHIFTRT)
	       && GET_CODE (XEXP (XEXP (t, 0), 0)) == SUBREG
	       && subreg_lowpart_p (XEXP (XEXP (t, 0), 0))
	       && rtx_equal_p (SUBREG_REG (XEXP (XEXP (t, 0), 0)), f)
	       && (num_sign_bit_copies (f, GET_MODE (f))
		   > (unsigned int)
		     (GET_MODE_BITSIZE (mode)
		      - GET_MODE_BITSIZE (GET_MODE (XEXP (XEXP (t, 0), 0))))))
	{
	  c1 = XEXP (XEXP (t, 0), 1); z = f; op = GET_CODE (XEXP (t, 0));
	  extend_op = SIGN_EXTEND;
	  m = GET_MODE (XEXP (t, 0));
	}
      else if (GET_CODE (t) == SIGN_EXTEND
	       && (GET_CODE (XEXP (t, 0)) == PLUS
		   || GET_CODE (XEXP (t, 0)) == IOR
		   || GET_CODE (XEXP (t, 0)) == XOR)
	       && GET_CODE (XEXP (XEXP (t, 0), 1)) == SUBREG
	       && subreg_lowpart_p (XEXP (XEXP (t, 0), 1))
	       && rtx_equal_p (SUBREG_REG (XEXP (XEXP (t, 0), 1)), f)
	       && (num_sign_bit_copies (f, GET_MODE (f))
		   > (unsigned int)
		     (GET_MODE_BITSIZE (mode)
		      - GET_MODE_BITSIZE (GET_MODE (XEXP (XEXP (t, 0), 1))))))
	{
	  c1 = XEXP (XEXP (t, 0), 0); z = f; op = GET_CODE (XEXP (t, 0));
	  extend_op = SIGN_EXTEND;
	  m = GET_MODE (XEXP (t, 0));
	}
      else if (GET_CODE (t) == ZERO_EXTEND
	       && (GET_CODE (XEXP (t, 0)) == PLUS
		   || GET_CODE (XEXP (t, 0)) == MINUS
		   || GET_CODE (XEXP (t, 0)) == IOR
		   || GET_CODE (XEXP (t, 0)) == XOR
		   || GET_CODE (XEXP (t, 0)) == ASHIFT
		   || GET_CODE (XEXP (t, 0)) == LSHIFTRT
		   || GET_CODE (XEXP (t, 0)) == ASHIFTRT)
	       && GET_CODE (XEXP (XEXP (t, 0), 0)) == SUBREG
	       && GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	       && subreg_lowpart_p (XEXP (XEXP (t, 0), 0))
	       && rtx_equal_p (SUBREG_REG (XEXP (XEXP (t, 0), 0)), f)
	       && ((nonzero_bits (f, GET_MODE (f))
		    & ~GET_MODE_MASK (GET_MODE (XEXP (XEXP (t, 0), 0))))
		   == 0))
	{
	  c1 = XEXP (XEXP (t, 0), 1); z = f; op = GET_CODE (XEXP (t, 0));
	  extend_op = ZERO_EXTEND;
	  m = GET_MODE (XEXP (t, 0));
	}
      else if (GET_CODE (t) == ZERO_EXTEND
	       && (GET_CODE (XEXP (t, 0)) == PLUS
		   || GET_CODE (XEXP (t, 0)) == IOR
		   || GET_CODE (XEXP (t, 0)) == XOR)
	       && GET_CODE (XEXP (XEXP (t, 0), 1)) == SUBREG
	       && GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	       && subreg_lowpart_p (XEXP (XEXP (t, 0), 1))
	       && rtx_equal_p (SUBREG_REG (XEXP (XEXP (t, 0), 1)), f)
	       && ((nonzero_bits (f, GET_MODE (f))
		    & ~GET_MODE_MASK (GET_MODE (XEXP (XEXP (t, 0), 1))))
		   == 0))
	{
	  c1 = XEXP (XEXP (t, 0), 0); z = f; op = GET_CODE (XEXP (t, 0));
	  extend_op = ZERO_EXTEND;
	  m = GET_MODE (XEXP (t, 0));
	}

      if (z)
	{
	  temp = subst (simplify_gen_relational (true_code, m, VOIDmode,
						 cond_op0, cond_op1),
			pc_rtx, pc_rtx, 0, 0);
	  temp = simplify_gen_binary (MULT, m, temp,
				      simplify_gen_binary (MULT, m, c1,
							   const_true_rtx));
	  temp = subst (temp, pc_rtx, pc_rtx, 0, 0);
	  temp = simplify_gen_binary (op, m, gen_lowpart (m, z), temp);

	  if (extend_op != UNKNOWN)
	    temp = simplify_gen_unary (extend_op, mode, temp, m);

	  return temp;
	}
    }

  /* If we have (if_then_else (ne A 0) C1 0) and either A is known to be 0 or
     1 and C1 is a single bit or A is known to be 0 or -1 and C1 is the
     negation of a single bit, we can convert this operation to a shift.  We
     can actually do this more generally, but it doesn't seem worth it.  */

  if (true_code == NE && XEXP (cond, 1) == const0_rtx
      && false_rtx == const0_rtx && GET_CODE (true_rtx) == CONST_INT
      && ((1 == nonzero_bits (XEXP (cond, 0), mode)
	   && (i = exact_log2 (INTVAL (true_rtx))) >= 0)
	  || ((num_sign_bit_copies (XEXP (cond, 0), mode)
	       == GET_MODE_BITSIZE (mode))
	      && (i = exact_log2 (-INTVAL (true_rtx))) >= 0)))
    return
      simplify_shift_const (NULL_RTX, ASHIFT, mode,
			    gen_lowpart (mode, XEXP (cond, 0)), i);

  /* (IF_THEN_ELSE (NE REG 0) (0) (8)) is REG for nonzero_bits (REG) == 8.  */
  if (true_code == NE && XEXP (cond, 1) == const0_rtx
      && false_rtx == const0_rtx && GET_CODE (true_rtx) == CONST_INT
      && GET_MODE (XEXP (cond, 0)) == mode
      && (INTVAL (true_rtx) & GET_MODE_MASK (mode))
	  == nonzero_bits (XEXP (cond, 0), mode)
      && (i = exact_log2 (INTVAL (true_rtx) & GET_MODE_MASK (mode))) >= 0)
    return XEXP (cond, 0);

  return x;
}

/* Simplify X, a SET expression.  Return the new expression.  */

static rtx
simplify_set (rtx x)
{
  rtx src = SET_SRC (x);
  rtx dest = SET_DEST (x);
  enum machine_mode mode
    = GET_MODE (src) != VOIDmode ? GET_MODE (src) : GET_MODE (dest);
  rtx other_insn;
  rtx *cc_use;

  /* (set (pc) (return)) gets written as (return).  */
  if (GET_CODE (dest) == PC && GET_CODE (src) == RETURN)
    return src;

  /* Now that we know for sure which bits of SRC we are using, see if we can
     simplify the expression for the object knowing that we only need the
     low-order bits.  */

  if (GET_MODE_CLASS (mode) == MODE_INT
      && GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT)
    {
      src = force_to_mode (src, mode, ~(HOST_WIDE_INT) 0, 0);
      SUBST (SET_SRC (x), src);
    }

  /* If we are setting CC0 or if the source is a COMPARE, look for the use of
     the comparison result and try to simplify it unless we already have used
     undobuf.other_insn.  */
  if ((GET_MODE_CLASS (mode) == MODE_CC
       || GET_CODE (src) == COMPARE
       || CC0_P (dest))
      && (cc_use = find_single_use (dest, subst_insn, &other_insn)) != 0
      && (undobuf.other_insn == 0 || other_insn == undobuf.other_insn)
      && COMPARISON_P (*cc_use)
      && rtx_equal_p (XEXP (*cc_use, 0), dest))
    {
      enum rtx_code old_code = GET_CODE (*cc_use);
      enum rtx_code new_code;
      rtx op0, op1, tmp;
      int other_changed = 0;
      enum machine_mode compare_mode = GET_MODE (dest);

      if (GET_CODE (src) == COMPARE)
	op0 = XEXP (src, 0), op1 = XEXP (src, 1);
      else
	op0 = src, op1 = CONST0_RTX (GET_MODE (src));

      tmp = simplify_relational_operation (old_code, compare_mode, VOIDmode,
					   op0, op1);
      if (!tmp)
	new_code = old_code;
      else if (!CONSTANT_P (tmp))
	{
	  new_code = GET_CODE (tmp);
	  op0 = XEXP (tmp, 0);
	  op1 = XEXP (tmp, 1);
	}
      else
	{
	  rtx pat = PATTERN (other_insn);
	  undobuf.other_insn = other_insn;
	  SUBST (*cc_use, tmp);

	  /* Attempt to simplify CC user.  */
	  if (GET_CODE (pat) == SET)
	    {
	      rtx new = simplify_rtx (SET_SRC (pat));
	      if (new != NULL_RTX)
		SUBST (SET_SRC (pat), new);
	    }

	  /* Convert X into a no-op move.  */
	  SUBST (SET_DEST (x), pc_rtx);
	  SUBST (SET_SRC (x), pc_rtx);
	  return x;
	}

      /* Simplify our comparison, if possible.  */
      new_code = simplify_comparison (new_code, &op0, &op1);

#ifdef SELECT_CC_MODE
      /* If this machine has CC modes other than CCmode, check to see if we
	 need to use a different CC mode here.  */
      if (GET_MODE_CLASS (GET_MODE (op0)) == MODE_CC)
	compare_mode = GET_MODE (op0);
      else
	compare_mode = SELECT_CC_MODE (new_code, op0, op1);

#ifndef HAVE_cc0
      /* If the mode changed, we have to change SET_DEST, the mode in the
	 compare, and the mode in the place SET_DEST is used.  If SET_DEST is
	 a hard register, just build new versions with the proper mode.  If it
	 is a pseudo, we lose unless it is only time we set the pseudo, in
	 which case we can safely change its mode.  */
      if (compare_mode != GET_MODE (dest))
	{
	  if (can_change_dest_mode (dest, 0, compare_mode))
	    {
	      unsigned int regno = REGNO (dest);
	      rtx new_dest;

	      if (regno < FIRST_PSEUDO_REGISTER)
		new_dest = gen_rtx_REG (compare_mode, regno);
	      else
		{
		  SUBST_MODE (regno_reg_rtx[regno], compare_mode);
		  new_dest = regno_reg_rtx[regno];
		}

	      SUBST (SET_DEST (x), new_dest);
	      SUBST (XEXP (*cc_use, 0), new_dest);
	      other_changed = 1;

	      dest = new_dest;
	    }
	}
#endif  /* cc0 */
#endif  /* SELECT_CC_MODE */

      /* If the code changed, we have to build a new comparison in
	 undobuf.other_insn.  */
      if (new_code != old_code)
	{
	  int other_changed_previously = other_changed;
	  unsigned HOST_WIDE_INT mask;

	  SUBST (*cc_use, gen_rtx_fmt_ee (new_code, GET_MODE (*cc_use),
					  dest, const0_rtx));
	  other_changed = 1;

	  /* If the only change we made was to change an EQ into an NE or
	     vice versa, OP0 has only one bit that might be nonzero, and OP1
	     is zero, check if changing the user of the condition code will
	     produce a valid insn.  If it won't, we can keep the original code
	     in that insn by surrounding our operation with an XOR.  */

	  if (((old_code == NE && new_code == EQ)
	       || (old_code == EQ && new_code == NE))
	      && ! other_changed_previously && op1 == const0_rtx
	      && GET_MODE_BITSIZE (GET_MODE (op0)) <= HOST_BITS_PER_WIDE_INT
	      && exact_log2 (mask = nonzero_bits (op0, GET_MODE (op0))) >= 0)
	    {
	      rtx pat = PATTERN (other_insn), note = 0;

	      if ((recog_for_combine (&pat, other_insn, &note) < 0
		   && ! check_asm_operands (pat)))
		{
		  PUT_CODE (*cc_use, old_code);
		  other_changed = 0;

		  op0 = simplify_gen_binary (XOR, GET_MODE (op0),
					     op0, GEN_INT (mask));
		}
	    }
	}

      if (other_changed)
	undobuf.other_insn = other_insn;

#ifdef HAVE_cc0
      /* If we are now comparing against zero, change our source if
	 needed.  If we do not use cc0, we always have a COMPARE.  */
      if (op1 == const0_rtx && dest == cc0_rtx)
	{
	  SUBST (SET_SRC (x), op0);
	  src = op0;
	}
      else
#endif

      /* Otherwise, if we didn't previously have a COMPARE in the
	 correct mode, we need one.  */
      if (GET_CODE (src) != COMPARE || GET_MODE (src) != compare_mode)
	{
	  SUBST (SET_SRC (x), gen_rtx_COMPARE (compare_mode, op0, op1));
	  src = SET_SRC (x);
	}
      else if (GET_MODE (op0) == compare_mode && op1 == const0_rtx)
	{
	  SUBST (SET_SRC (x), op0);
	  src = SET_SRC (x);
	}
      /* Otherwise, update the COMPARE if needed.  */
      else if (XEXP (src, 0) != op0 || XEXP (src, 1) != op1)
	{
	  SUBST (SET_SRC (x), gen_rtx_COMPARE (compare_mode, op0, op1));
	  src = SET_SRC (x);
	}
    }
  else
    {
      /* Get SET_SRC in a form where we have placed back any
	 compound expressions.  Then do the checks below.  */
      src = make_compound_operation (src, SET);
      SUBST (SET_SRC (x), src);
    }

  /* If we have (set x (subreg:m1 (op:m2 ...) 0)) with OP being some operation,
     and X being a REG or (subreg (reg)), we may be able to convert this to
     (set (subreg:m2 x) (op)).

     We can always do this if M1 is narrower than M2 because that means that
     we only care about the low bits of the result.

     However, on machines without WORD_REGISTER_OPERATIONS defined, we cannot
     perform a narrower operation than requested since the high-order bits will
     be undefined.  On machine where it is defined, this transformation is safe
     as long as M1 and M2 have the same number of words.  */

  if (GET_CODE (src) == SUBREG && subreg_lowpart_p (src)
      && !OBJECT_P (SUBREG_REG (src))
      && (((GET_MODE_SIZE (GET_MODE (src)) + (UNITS_PER_WORD - 1))
	   / UNITS_PER_WORD)
	  == ((GET_MODE_SIZE (GET_MODE (SUBREG_REG (src)))
	       + (UNITS_PER_WORD - 1)) / UNITS_PER_WORD))
#ifndef WORD_REGISTER_OPERATIONS
      && (GET_MODE_SIZE (GET_MODE (src))
	< GET_MODE_SIZE (GET_MODE (SUBREG_REG (src))))
#endif
#ifdef CANNOT_CHANGE_MODE_CLASS
      && ! (REG_P (dest) && REGNO (dest) < FIRST_PSEUDO_REGISTER
	    && REG_CANNOT_CHANGE_MODE_P (REGNO (dest),
					 GET_MODE (SUBREG_REG (src)),
					 GET_MODE (src)))
#endif
      && (REG_P (dest)
	  || (GET_CODE (dest) == SUBREG
	      && REG_P (SUBREG_REG (dest)))))
    {
      SUBST (SET_DEST (x),
	     gen_lowpart (GET_MODE (SUBREG_REG (src)),
				      dest));
      SUBST (SET_SRC (x), SUBREG_REG (src));

      src = SET_SRC (x), dest = SET_DEST (x);
    }

#ifdef HAVE_cc0
  /* If we have (set (cc0) (subreg ...)), we try to remove the subreg
     in SRC.  */
  if (dest == cc0_rtx
      && GET_CODE (src) == SUBREG
      && subreg_lowpart_p (src)
      && (GET_MODE_BITSIZE (GET_MODE (src))
	  < GET_MODE_BITSIZE (GET_MODE (SUBREG_REG (src)))))
    {
      rtx inner = SUBREG_REG (src);
      enum machine_mode inner_mode = GET_MODE (inner);

      /* Here we make sure that we don't have a sign bit on.  */
      if (GET_MODE_BITSIZE (inner_mode) <= HOST_BITS_PER_WIDE_INT
	  && (nonzero_bits (inner, inner_mode)
	      < ((unsigned HOST_WIDE_INT) 1
		 << (GET_MODE_BITSIZE (GET_MODE (src)) - 1))))
	{
	  SUBST (SET_SRC (x), inner);
	  src = SET_SRC (x);
	}
    }
#endif

#ifdef LOAD_EXTEND_OP
  /* If we have (set FOO (subreg:M (mem:N BAR) 0)) with M wider than N, this
     would require a paradoxical subreg.  Replace the subreg with a
     zero_extend to avoid the reload that would otherwise be required.  */

  if (GET_CODE (src) == SUBREG && subreg_lowpart_p (src)
      && LOAD_EXTEND_OP (GET_MODE (SUBREG_REG (src))) != UNKNOWN
      && SUBREG_BYTE (src) == 0
      && (GET_MODE_SIZE (GET_MODE (src))
	  > GET_MODE_SIZE (GET_MODE (SUBREG_REG (src))))
      && MEM_P (SUBREG_REG (src)))
    {
      SUBST (SET_SRC (x),
	     gen_rtx_fmt_e (LOAD_EXTEND_OP (GET_MODE (SUBREG_REG (src))),
			    GET_MODE (src), SUBREG_REG (src)));

      src = SET_SRC (x);
    }
#endif

  /* If we don't have a conditional move, SET_SRC is an IF_THEN_ELSE, and we
     are comparing an item known to be 0 or -1 against 0, use a logical
     operation instead. Check for one of the arms being an IOR of the other
     arm with some value.  We compute three terms to be IOR'ed together.  In
     practice, at most two will be nonzero.  Then we do the IOR's.  */

  if (GET_CODE (dest) != PC
      && GET_CODE (src) == IF_THEN_ELSE
      && GET_MODE_CLASS (GET_MODE (src)) == MODE_INT
      && (GET_CODE (XEXP (src, 0)) == EQ || GET_CODE (XEXP (src, 0)) == NE)
      && XEXP (XEXP (src, 0), 1) == const0_rtx
      && GET_MODE (src) == GET_MODE (XEXP (XEXP (src, 0), 0))
#ifdef HAVE_conditional_move
      && ! can_conditionally_move_p (GET_MODE (src))
#endif
      && (num_sign_bit_copies (XEXP (XEXP (src, 0), 0),
			       GET_MODE (XEXP (XEXP (src, 0), 0)))
	  == GET_MODE_BITSIZE (GET_MODE (XEXP (XEXP (src, 0), 0))))
      && ! side_effects_p (src))
    {
      rtx true_rtx = (GET_CODE (XEXP (src, 0)) == NE
		      ? XEXP (src, 1) : XEXP (src, 2));
      rtx false_rtx = (GET_CODE (XEXP (src, 0)) == NE
		   ? XEXP (src, 2) : XEXP (src, 1));
      rtx term1 = const0_rtx, term2, term3;

      if (GET_CODE (true_rtx) == IOR
	  && rtx_equal_p (XEXP (true_rtx, 0), false_rtx))
	term1 = false_rtx, true_rtx = XEXP (true_rtx, 1), false_rtx = const0_rtx;
      else if (GET_CODE (true_rtx) == IOR
	       && rtx_equal_p (XEXP (true_rtx, 1), false_rtx))
	term1 = false_rtx, true_rtx = XEXP (true_rtx, 0), false_rtx = const0_rtx;
      else if (GET_CODE (false_rtx) == IOR
	       && rtx_equal_p (XEXP (false_rtx, 0), true_rtx))
	term1 = true_rtx, false_rtx = XEXP (false_rtx, 1), true_rtx = const0_rtx;
      else if (GET_CODE (false_rtx) == IOR
	       && rtx_equal_p (XEXP (false_rtx, 1), true_rtx))
	term1 = true_rtx, false_rtx = XEXP (false_rtx, 0), true_rtx = const0_rtx;

      term2 = simplify_gen_binary (AND, GET_MODE (src),
				   XEXP (XEXP (src, 0), 0), true_rtx);
      term3 = simplify_gen_binary (AND, GET_MODE (src),
				   simplify_gen_unary (NOT, GET_MODE (src),
						       XEXP (XEXP (src, 0), 0),
						       GET_MODE (src)),
				   false_rtx);

      SUBST (SET_SRC (x),
	     simplify_gen_binary (IOR, GET_MODE (src),
				  simplify_gen_binary (IOR, GET_MODE (src),
						       term1, term2),
				  term3));

      src = SET_SRC (x);
    }

  /* If either SRC or DEST is a CLOBBER of (const_int 0), make this
     whole thing fail.  */
  if (GET_CODE (src) == CLOBBER && XEXP (src, 0) == const0_rtx)
    return src;
  else if (GET_CODE (dest) == CLOBBER && XEXP (dest, 0) == const0_rtx)
    return dest;
  else
    /* Convert this into a field assignment operation, if possible.  */
    return make_field_assignment (x);
}

/* Simplify, X, and AND, IOR, or XOR operation, and return the simplified
   result.  */

static rtx
simplify_logical (rtx x)
{
  enum machine_mode mode = GET_MODE (x);
  rtx op0 = XEXP (x, 0);
  rtx op1 = XEXP (x, 1);

  switch (GET_CODE (x))
    {
    case AND:
      /* We can call simplify_and_const_int only if we don't lose
	 any (sign) bits when converting INTVAL (op1) to
	 "unsigned HOST_WIDE_INT".  */
      if (GET_CODE (op1) == CONST_INT
	  && (GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	      || INTVAL (op1) > 0))
	{
	  x = simplify_and_const_int (x, mode, op0, INTVAL (op1));
	  if (GET_CODE (x) != AND)
	    return x;

	  op0 = XEXP (x, 0);
	  op1 = XEXP (x, 1);
	}

      /* If we have any of (and (ior A B) C) or (and (xor A B) C),
	 apply the distributive law and then the inverse distributive
	 law to see if things simplify.  */
      if (GET_CODE (op0) == IOR || GET_CODE (op0) == XOR)
	{
	  rtx result = distribute_and_simplify_rtx (x, 0);
	  if (result)
	    return result;
	}
      if (GET_CODE (op1) == IOR || GET_CODE (op1) == XOR)
	{
	  rtx result = distribute_and_simplify_rtx (x, 1);
	  if (result)
	    return result;
	}
      break;

    case IOR:
      /* If we have (ior (and A B) C), apply the distributive law and then
	 the inverse distributive law to see if things simplify.  */

      if (GET_CODE (op0) == AND)
	{
	  rtx result = distribute_and_simplify_rtx (x, 0);
	  if (result)
	    return result;
	}

      if (GET_CODE (op1) == AND)
	{
	  rtx result = distribute_and_simplify_rtx (x, 1);
	  if (result)
	    return result;
	}
      break;

    default:
      gcc_unreachable ();
    }

  return x;
}

/* We consider ZERO_EXTRACT, SIGN_EXTRACT, and SIGN_EXTEND as "compound
   operations" because they can be replaced with two more basic operations.
   ZERO_EXTEND is also considered "compound" because it can be replaced with
   an AND operation, which is simpler, though only one operation.

   The function expand_compound_operation is called with an rtx expression
   and will convert it to the appropriate shifts and AND operations,
   simplifying at each stage.

   The function make_compound_operation is called to convert an expression
   consisting of shifts and ANDs into the equivalent compound expression.
   It is the inverse of this function, loosely speaking.  */

static rtx
expand_compound_operation (rtx x)
{
  unsigned HOST_WIDE_INT pos = 0, len;
  int unsignedp = 0;
  unsigned int modewidth;
  rtx tem;

  switch (GET_CODE (x))
    {
    case ZERO_EXTEND:
      unsignedp = 1;
    case SIGN_EXTEND:
      /* We can't necessarily use a const_int for a multiword mode;
	 it depends on implicitly extending the value.
	 Since we don't know the right way to extend it,
	 we can't tell whether the implicit way is right.

	 Even for a mode that is no wider than a const_int,
	 we can't win, because we need to sign extend one of its bits through
	 the rest of it, and we don't know which bit.  */
      if (GET_CODE (XEXP (x, 0)) == CONST_INT)
	return x;

      /* Return if (subreg:MODE FROM 0) is not a safe replacement for
	 (zero_extend:MODE FROM) or (sign_extend:MODE FROM).  It is for any MEM
	 because (SUBREG (MEM...)) is guaranteed to cause the MEM to be
	 reloaded. If not for that, MEM's would very rarely be safe.

	 Reject MODEs bigger than a word, because we might not be able
	 to reference a two-register group starting with an arbitrary register
	 (and currently gen_lowpart might crash for a SUBREG).  */

      if (GET_MODE_SIZE (GET_MODE (XEXP (x, 0))) > UNITS_PER_WORD)
	return x;

      /* Reject MODEs that aren't scalar integers because turning vector
	 or complex modes into shifts causes problems.  */

      if (! SCALAR_INT_MODE_P (GET_MODE (XEXP (x, 0))))
	return x;

      len = GET_MODE_BITSIZE (GET_MODE (XEXP (x, 0)));
      /* If the inner object has VOIDmode (the only way this can happen
	 is if it is an ASM_OPERANDS), we can't do anything since we don't
	 know how much masking to do.  */
      if (len == 0)
	return x;

      break;

    case ZERO_EXTRACT:
      unsignedp = 1;

      /* ... fall through ...  */

    case SIGN_EXTRACT:
      /* If the operand is a CLOBBER, just return it.  */
      if (GET_CODE (XEXP (x, 0)) == CLOBBER)
	return XEXP (x, 0);

      if (GET_CODE (XEXP (x, 1)) != CONST_INT
	  || GET_CODE (XEXP (x, 2)) != CONST_INT
	  || GET_MODE (XEXP (x, 0)) == VOIDmode)
	return x;

      /* Reject MODEs that aren't scalar integers because turning vector
	 or complex modes into shifts causes problems.  */

      if (! SCALAR_INT_MODE_P (GET_MODE (XEXP (x, 0))))
	return x;

      len = INTVAL (XEXP (x, 1));
      pos = INTVAL (XEXP (x, 2));

      /* This should stay within the object being extracted, fail otherwise.  */
      if (len + pos > GET_MODE_BITSIZE (GET_MODE (XEXP (x, 0))))
	return x;

      if (BITS_BIG_ENDIAN)
	pos = GET_MODE_BITSIZE (GET_MODE (XEXP (x, 0))) - len - pos;

      break;

    default:
      return x;
    }
  /* Convert sign extension to zero extension, if we know that the high
     bit is not set, as this is easier to optimize.  It will be converted
     back to cheaper alternative in make_extraction.  */
  if (GET_CODE (x) == SIGN_EXTEND
      && (GET_MODE_BITSIZE (GET_MODE (x)) <= HOST_BITS_PER_WIDE_INT
	  && ((nonzero_bits (XEXP (x, 0), GET_MODE (XEXP (x, 0)))
		& ~(((unsigned HOST_WIDE_INT)
		      GET_MODE_MASK (GET_MODE (XEXP (x, 0))))
		     >> 1))
	       == 0)))
    {
      rtx temp = gen_rtx_ZERO_EXTEND (GET_MODE (x), XEXP (x, 0));
      rtx temp2 = expand_compound_operation (temp);

      /* Make sure this is a profitable operation.  */
      if (rtx_cost (x, SET) > rtx_cost (temp2, SET))
       return temp2;
      else if (rtx_cost (x, SET) > rtx_cost (temp, SET))
       return temp;
      else
       return x;
    }

  /* We can optimize some special cases of ZERO_EXTEND.  */
  if (GET_CODE (x) == ZERO_EXTEND)
    {
      /* (zero_extend:DI (truncate:SI foo:DI)) is just foo:DI if we
	 know that the last value didn't have any inappropriate bits
	 set.  */
      if (GET_CODE (XEXP (x, 0)) == TRUNCATE
	  && GET_MODE (XEXP (XEXP (x, 0), 0)) == GET_MODE (x)
	  && GET_MODE_BITSIZE (GET_MODE (x)) <= HOST_BITS_PER_WIDE_INT
	  && (nonzero_bits (XEXP (XEXP (x, 0), 0), GET_MODE (x))
	      & ~GET_MODE_MASK (GET_MODE (XEXP (x, 0)))) == 0)
	return XEXP (XEXP (x, 0), 0);

      /* Likewise for (zero_extend:DI (subreg:SI foo:DI 0)).  */
      if (GET_CODE (XEXP (x, 0)) == SUBREG
	  && GET_MODE (SUBREG_REG (XEXP (x, 0))) == GET_MODE (x)
	  && subreg_lowpart_p (XEXP (x, 0))
	  && GET_MODE_BITSIZE (GET_MODE (x)) <= HOST_BITS_PER_WIDE_INT
	  && (nonzero_bits (SUBREG_REG (XEXP (x, 0)), GET_MODE (x))
	      & ~GET_MODE_MASK (GET_MODE (XEXP (x, 0)))) == 0)
	return SUBREG_REG (XEXP (x, 0));

      /* (zero_extend:DI (truncate:SI foo:DI)) is just foo:DI when foo
	 is a comparison and STORE_FLAG_VALUE permits.  This is like
	 the first case, but it works even when GET_MODE (x) is larger
	 than HOST_WIDE_INT.  */
      if (GET_CODE (XEXP (x, 0)) == TRUNCATE
	  && GET_MODE (XEXP (XEXP (x, 0), 0)) == GET_MODE (x)
	  && COMPARISON_P (XEXP (XEXP (x, 0), 0))
	  && (GET_MODE_BITSIZE (GET_MODE (XEXP (x, 0)))
	      <= HOST_BITS_PER_WIDE_INT)
	  && ((HOST_WIDE_INT) STORE_FLAG_VALUE
	      & ~GET_MODE_MASK (GET_MODE (XEXP (x, 0)))) == 0)
	return XEXP (XEXP (x, 0), 0);

      /* Likewise for (zero_extend:DI (subreg:SI foo:DI 0)).  */
      if (GET_CODE (XEXP (x, 0)) == SUBREG
	  && GET_MODE (SUBREG_REG (XEXP (x, 0))) == GET_MODE (x)
	  && subreg_lowpart_p (XEXP (x, 0))
	  && COMPARISON_P (SUBREG_REG (XEXP (x, 0)))
	  && (GET_MODE_BITSIZE (GET_MODE (XEXP (x, 0)))
	      <= HOST_BITS_PER_WIDE_INT)
	  && ((HOST_WIDE_INT) STORE_FLAG_VALUE
	      & ~GET_MODE_MASK (GET_MODE (XEXP (x, 0)))) == 0)
	return SUBREG_REG (XEXP (x, 0));

    }

  /* If we reach here, we want to return a pair of shifts.  The inner
     shift is a left shift of BITSIZE - POS - LEN bits.  The outer
     shift is a right shift of BITSIZE - LEN bits.  It is arithmetic or
     logical depending on the value of UNSIGNEDP.

     If this was a ZERO_EXTEND or ZERO_EXTRACT, this pair of shifts will be
     converted into an AND of a shift.

     We must check for the case where the left shift would have a negative
     count.  This can happen in a case like (x >> 31) & 255 on machines
     that can't shift by a constant.  On those machines, we would first
     combine the shift with the AND to produce a variable-position
     extraction.  Then the constant of 31 would be substituted in to produce
     a such a position.  */

  modewidth = GET_MODE_BITSIZE (GET_MODE (x));
  if (modewidth + len >= pos)
    {
      enum machine_mode mode = GET_MODE (x);
      tem = gen_lowpart (mode, XEXP (x, 0));
      if (!tem || GET_CODE (tem) == CLOBBER)
	return x;
      tem = simplify_shift_const (NULL_RTX, ASHIFT, mode,
				  tem, modewidth - pos - len);
      tem = simplify_shift_const (NULL_RTX, unsignedp ? LSHIFTRT : ASHIFTRT,
				  mode, tem, modewidth - len);
    }
  else if (unsignedp && len < HOST_BITS_PER_WIDE_INT)
    tem = simplify_and_const_int (NULL_RTX, GET_MODE (x),
				  simplify_shift_const (NULL_RTX, LSHIFTRT,
							GET_MODE (x),
							XEXP (x, 0), pos),
				  ((HOST_WIDE_INT) 1 << len) - 1);
  else
    /* Any other cases we can't handle.  */
    return x;

  /* If we couldn't do this for some reason, return the original
     expression.  */
  if (GET_CODE (tem) == CLOBBER)
    return x;

  return tem;
}

/* X is a SET which contains an assignment of one object into
   a part of another (such as a bit-field assignment, STRICT_LOW_PART,
   or certain SUBREGS). If possible, convert it into a series of
   logical operations.

   We half-heartedly support variable positions, but do not at all
   support variable lengths.  */

static rtx
expand_field_assignment (rtx x)
{
  rtx inner;
  rtx pos;			/* Always counts from low bit.  */
  int len;
  rtx mask, cleared, masked;
  enum machine_mode compute_mode;

  /* Loop until we find something we can't simplify.  */
  while (1)
    {
      if (GET_CODE (SET_DEST (x)) == STRICT_LOW_PART
	  && GET_CODE (XEXP (SET_DEST (x), 0)) == SUBREG)
	{
	  inner = SUBREG_REG (XEXP (SET_DEST (x), 0));
	  len = GET_MODE_BITSIZE (GET_MODE (XEXP (SET_DEST (x), 0)));
	  pos = GEN_INT (subreg_lsb (XEXP (SET_DEST (x), 0)));
	}
      else if (GET_CODE (SET_DEST (x)) == ZERO_EXTRACT
	       && GET_CODE (XEXP (SET_DEST (x), 1)) == CONST_INT)
	{
	  inner = XEXP (SET_DEST (x), 0);
	  len = INTVAL (XEXP (SET_DEST (x), 1));
	  pos = XEXP (SET_DEST (x), 2);

	  /* A constant position should stay within the width of INNER.  */
	  if (GET_CODE (pos) == CONST_INT
	      && INTVAL (pos) + len > GET_MODE_BITSIZE (GET_MODE (inner)))
	    break;

	  if (BITS_BIG_ENDIAN)
	    {
	      if (GET_CODE (pos) == CONST_INT)
		pos = GEN_INT (GET_MODE_BITSIZE (GET_MODE (inner)) - len
			       - INTVAL (pos));
	      else if (GET_CODE (pos) == MINUS
		       && GET_CODE (XEXP (pos, 1)) == CONST_INT
		       && (INTVAL (XEXP (pos, 1))
			   == GET_MODE_BITSIZE (GET_MODE (inner)) - len))
		/* If position is ADJUST - X, new position is X.  */
		pos = XEXP (pos, 0);
	      else
		pos = simplify_gen_binary (MINUS, GET_MODE (pos),
					   GEN_INT (GET_MODE_BITSIZE (
						    GET_MODE (inner))
						    - len),
					   pos);
	    }
	}

      /* A SUBREG between two modes that occupy the same numbers of words
	 can be done by moving the SUBREG to the source.  */
      else if (GET_CODE (SET_DEST (x)) == SUBREG
	       /* We need SUBREGs to compute nonzero_bits properly.  */
	       && nonzero_sign_valid
	       && (((GET_MODE_SIZE (GET_MODE (SET_DEST (x)))
		     + (UNITS_PER_WORD - 1)) / UNITS_PER_WORD)
		   == ((GET_MODE_SIZE (GET_MODE (SUBREG_REG (SET_DEST (x))))
			+ (UNITS_PER_WORD - 1)) / UNITS_PER_WORD)))
	{
	  x = gen_rtx_SET (VOIDmode, SUBREG_REG (SET_DEST (x)),
			   gen_lowpart
			   (GET_MODE (SUBREG_REG (SET_DEST (x))),
			    SET_SRC (x)));
	  continue;
	}
      else
	break;

      while (GET_CODE (inner) == SUBREG && subreg_lowpart_p (inner))
	inner = SUBREG_REG (inner);

      compute_mode = GET_MODE (inner);

      /* Don't attempt bitwise arithmetic on non scalar integer modes.  */
      if (! SCALAR_INT_MODE_P (compute_mode))
	{
	  enum machine_mode imode;

	  /* Don't do anything for vector or complex integral types.  */
	  if (! FLOAT_MODE_P (compute_mode))
	    break;

	  /* Try to find an integral mode to pun with.  */
	  imode = mode_for_size (GET_MODE_BITSIZE (compute_mode), MODE_INT, 0);
	  if (imode == BLKmode)
	    break;

	  compute_mode = imode;
	  inner = gen_lowpart (imode, inner);
	}

      /* Compute a mask of LEN bits, if we can do this on the host machine.  */
      if (len >= HOST_BITS_PER_WIDE_INT)
	break;

      /* Now compute the equivalent expression.  Make a copy of INNER
	 for the SET_DEST in case it is a MEM into which we will substitute;
	 we don't want shared RTL in that case.  */
      mask = GEN_INT (((HOST_WIDE_INT) 1 << len) - 1);
      cleared = simplify_gen_binary (AND, compute_mode,
				     simplify_gen_unary (NOT, compute_mode,
				       simplify_gen_binary (ASHIFT,
							    compute_mode,
							    mask, pos),
				       compute_mode),
				     inner);
      masked = simplify_gen_binary (ASHIFT, compute_mode,
				    simplify_gen_binary (
				      AND, compute_mode,
				      gen_lowpart (compute_mode, SET_SRC (x)),
				      mask),
				    pos);

      x = gen_rtx_SET (VOIDmode, copy_rtx (inner),
		       simplify_gen_binary (IOR, compute_mode,
					    cleared, masked));
    }

  return x;
}

/* Return an RTX for a reference to LEN bits of INNER.  If POS_RTX is nonzero,
   it is an RTX that represents a variable starting position; otherwise,
   POS is the (constant) starting bit position (counted from the LSB).

   UNSIGNEDP is nonzero for an unsigned reference and zero for a
   signed reference.

   IN_DEST is nonzero if this is a reference in the destination of a
   SET.  This is used when a ZERO_ or SIGN_EXTRACT isn't needed.  If nonzero,
   a STRICT_LOW_PART will be used, if zero, ZERO_EXTEND or SIGN_EXTEND will
   be used.

   IN_COMPARE is nonzero if we are in a COMPARE.  This means that a
   ZERO_EXTRACT should be built even for bits starting at bit 0.

   MODE is the desired mode of the result (if IN_DEST == 0).

   The result is an RTX for the extraction or NULL_RTX if the target
   can't handle it.  */

static rtx
make_extraction (enum machine_mode mode, rtx inner, HOST_WIDE_INT pos,
		 rtx pos_rtx, unsigned HOST_WIDE_INT len, int unsignedp,
		 int in_dest, int in_compare)
{
  /* This mode describes the size of the storage area
     to fetch the overall value from.  Within that, we
     ignore the POS lowest bits, etc.  */
  enum machine_mode is_mode = GET_MODE (inner);
  enum machine_mode inner_mode;
  enum machine_mode wanted_inner_mode;
  enum machine_mode wanted_inner_reg_mode = word_mode;
  enum machine_mode pos_mode = word_mode;
  enum machine_mode extraction_mode = word_mode;
  enum machine_mode tmode = mode_for_size (len, MODE_INT, 1);
  rtx new = 0;
  rtx orig_pos_rtx = pos_rtx;
  HOST_WIDE_INT orig_pos;

  if (GET_CODE (inner) == SUBREG && subreg_lowpart_p (inner))
    {
      /* If going from (subreg:SI (mem:QI ...)) to (mem:QI ...),
	 consider just the QI as the memory to extract from.
	 The subreg adds or removes high bits; its mode is
	 irrelevant to the meaning of this extraction,
	 since POS and LEN count from the lsb.  */
      if (MEM_P (SUBREG_REG (inner)))
	is_mode = GET_MODE (SUBREG_REG (inner));
      inner = SUBREG_REG (inner);
    }
  else if (GET_CODE (inner) == ASHIFT
	   && GET_CODE (XEXP (inner, 1)) == CONST_INT
	   && pos_rtx == 0 && pos == 0
	   && len > (unsigned HOST_WIDE_INT) INTVAL (XEXP (inner, 1)))
    {
      /* We're extracting the least significant bits of an rtx
	 (ashift X (const_int C)), where LEN > C.  Extract the
	 least significant (LEN - C) bits of X, giving an rtx
	 whose mode is MODE, then shift it left C times.  */
      new = make_extraction (mode, XEXP (inner, 0),
			     0, 0, len - INTVAL (XEXP (inner, 1)),
			     unsignedp, in_dest, in_compare);
      if (new != 0)
	return gen_rtx_ASHIFT (mode, new, XEXP (inner, 1));
    }

  inner_mode = GET_MODE (inner);

  if (pos_rtx && GET_CODE (pos_rtx) == CONST_INT)
    pos = INTVAL (pos_rtx), pos_rtx = 0;

  /* See if this can be done without an extraction.  We never can if the
     width of the field is not the same as that of some integer mode. For
     registers, we can only avoid the extraction if the position is at the
     low-order bit and this is either not in the destination or we have the
     appropriate STRICT_LOW_PART operation available.

     For MEM, we can avoid an extract if the field starts on an appropriate
     boundary and we can change the mode of the memory reference.  */

  if (tmode != BLKmode
      && ((pos_rtx == 0 && (pos % BITS_PER_WORD) == 0
	   && !MEM_P (inner)
	   && (inner_mode == tmode
	       || !REG_P (inner)
	       || TRULY_NOOP_TRUNCATION (GET_MODE_BITSIZE (tmode),
					 GET_MODE_BITSIZE (inner_mode))
	       || reg_truncated_to_mode (tmode, inner))
	   && (! in_dest
	       || (REG_P (inner)
		   && have_insn_for (STRICT_LOW_PART, tmode))))
	  || (MEM_P (inner) && pos_rtx == 0
	      && (pos
		  % (STRICT_ALIGNMENT ? GET_MODE_ALIGNMENT (tmode)
		     : BITS_PER_UNIT)) == 0
	      /* We can't do this if we are widening INNER_MODE (it
		 may not be aligned, for one thing).  */
	      && GET_MODE_BITSIZE (inner_mode) >= GET_MODE_BITSIZE (tmode)
	      && (inner_mode == tmode
		  || (! mode_dependent_address_p (XEXP (inner, 0))
		      && ! MEM_VOLATILE_P (inner))))))
    {
      /* If INNER is a MEM, make a new MEM that encompasses just the desired
	 field.  If the original and current mode are the same, we need not
	 adjust the offset.  Otherwise, we do if bytes big endian.

	 If INNER is not a MEM, get a piece consisting of just the field
	 of interest (in this case POS % BITS_PER_WORD must be 0).  */

      if (MEM_P (inner))
	{
	  HOST_WIDE_INT offset;

	  /* POS counts from lsb, but make OFFSET count in memory order.  */
	  if (BYTES_BIG_ENDIAN)
	    offset = (GET_MODE_BITSIZE (is_mode) - len - pos) / BITS_PER_UNIT;
	  else
	    offset = pos / BITS_PER_UNIT;

	  new = adjust_address_nv (inner, tmode, offset);
	}
      else if (REG_P (inner))
	{
	  if (tmode != inner_mode)
	    {
	      /* We can't call gen_lowpart in a DEST since we
		 always want a SUBREG (see below) and it would sometimes
		 return a new hard register.  */
	      if (pos || in_dest)
		{
		  HOST_WIDE_INT final_word = pos / BITS_PER_WORD;

		  if (WORDS_BIG_ENDIAN
		      && GET_MODE_SIZE (inner_mode) > UNITS_PER_WORD)
		    final_word = ((GET_MODE_SIZE (inner_mode)
				   - GET_MODE_SIZE (tmode))
				  / UNITS_PER_WORD) - final_word;

		  final_word *= UNITS_PER_WORD;
		  if (BYTES_BIG_ENDIAN &&
		      GET_MODE_SIZE (inner_mode) > GET_MODE_SIZE (tmode))
		    final_word += (GET_MODE_SIZE (inner_mode)
				   - GET_MODE_SIZE (tmode)) % UNITS_PER_WORD;

		  /* Avoid creating invalid subregs, for example when
		     simplifying (x>>32)&255.  */
		  if (!validate_subreg (tmode, inner_mode, inner, final_word))
		    return NULL_RTX;

		  new = gen_rtx_SUBREG (tmode, inner, final_word);
		}
	      else
		new = gen_lowpart (tmode, inner);
	    }
	  else
	    new = inner;
	}
      else
	new = force_to_mode (inner, tmode,
			     len >= HOST_BITS_PER_WIDE_INT
			     ? ~(unsigned HOST_WIDE_INT) 0
			     : ((unsigned HOST_WIDE_INT) 1 << len) - 1,
			     0);

      /* If this extraction is going into the destination of a SET,
	 make a STRICT_LOW_PART unless we made a MEM.  */

      if (in_dest)
	return (MEM_P (new) ? new
		: (GET_CODE (new) != SUBREG
		   ? gen_rtx_CLOBBER (tmode, const0_rtx)
		   : gen_rtx_STRICT_LOW_PART (VOIDmode, new)));

      if (mode == tmode)
	return new;

      if (GET_CODE (new) == CONST_INT)
	return gen_int_mode (INTVAL (new), mode);

      /* If we know that no extraneous bits are set, and that the high
	 bit is not set, convert the extraction to the cheaper of
	 sign and zero extension, that are equivalent in these cases.  */
      if (flag_expensive_optimizations
	  && (GET_MODE_BITSIZE (tmode) <= HOST_BITS_PER_WIDE_INT
	      && ((nonzero_bits (new, tmode)
		   & ~(((unsigned HOST_WIDE_INT)
			GET_MODE_MASK (tmode))
		       >> 1))
		  == 0)))
	{
	  rtx temp = gen_rtx_ZERO_EXTEND (mode, new);
	  rtx temp1 = gen_rtx_SIGN_EXTEND (mode, new);

	  /* Prefer ZERO_EXTENSION, since it gives more information to
	     backends.  */
	  if (rtx_cost (temp, SET) <= rtx_cost (temp1, SET))
	    return temp;
	  return temp1;
	}

      /* Otherwise, sign- or zero-extend unless we already are in the
	 proper mode.  */

      return (gen_rtx_fmt_e (unsignedp ? ZERO_EXTEND : SIGN_EXTEND,
			     mode, new));
    }

  /* Unless this is a COMPARE or we have a funny memory reference,
     don't do anything with zero-extending field extracts starting at
     the low-order bit since they are simple AND operations.  */
  if (pos_rtx == 0 && pos == 0 && ! in_dest
      && ! in_compare && unsignedp)
    return 0;

  /* Unless INNER is not MEM, reject this if we would be spanning bytes or
     if the position is not a constant and the length is not 1.  In all
     other cases, we would only be going outside our object in cases when
     an original shift would have been undefined.  */
  if (MEM_P (inner)
      && ((pos_rtx == 0 && pos + len > GET_MODE_BITSIZE (is_mode))
	  || (pos_rtx != 0 && len != 1)))
    return 0;

  /* Get the mode to use should INNER not be a MEM, the mode for the position,
     and the mode for the result.  */
  if (in_dest && mode_for_extraction (EP_insv, -1) != MAX_MACHINE_MODE)
    {
      wanted_inner_reg_mode = mode_for_extraction (EP_insv, 0);
      pos_mode = mode_for_extraction (EP_insv, 2);
      extraction_mode = mode_for_extraction (EP_insv, 3);
    }

  if (! in_dest && unsignedp
      && mode_for_extraction (EP_extzv, -1) != MAX_MACHINE_MODE)
    {
      wanted_inner_reg_mode = mode_for_extraction (EP_extzv, 1);
      pos_mode = mode_for_extraction (EP_extzv, 3);
      extraction_mode = mode_for_extraction (EP_extzv, 0);
    }

  if (! in_dest && ! unsignedp
      && mode_for_extraction (EP_extv, -1) != MAX_MACHINE_MODE)
    {
      wanted_inner_reg_mode = mode_for_extraction (EP_extv, 1);
      pos_mode = mode_for_extraction (EP_extv, 3);
      extraction_mode = mode_for_extraction (EP_extv, 0);
    }

  /* Never narrow an object, since that might not be safe.  */

  if (mode != VOIDmode
      && GET_MODE_SIZE (extraction_mode) < GET_MODE_SIZE (mode))
    extraction_mode = mode;

  if (pos_rtx && GET_MODE (pos_rtx) != VOIDmode
      && GET_MODE_SIZE (pos_mode) < GET_MODE_SIZE (GET_MODE (pos_rtx)))
    pos_mode = GET_MODE (pos_rtx);

  /* If this is not from memory, the desired mode is the preferred mode
     for an extraction pattern's first input operand, or word_mode if there
     is none.  */
  if (!MEM_P (inner))
    wanted_inner_mode = wanted_inner_reg_mode;
  else
    {
      /* Be careful not to go beyond the extracted object and maintain the
	 natural alignment of the memory.  */
      wanted_inner_mode = smallest_mode_for_size (len, MODE_INT);
      while (pos % GET_MODE_BITSIZE (wanted_inner_mode) + len
	     > GET_MODE_BITSIZE (wanted_inner_mode))
	{
	  wanted_inner_mode = GET_MODE_WIDER_MODE (wanted_inner_mode);
	  gcc_assert (wanted_inner_mode != VOIDmode);
	}

      /* If we have to change the mode of memory and cannot, the desired mode
	 is EXTRACTION_MODE.  */
      if (inner_mode != wanted_inner_mode
	  && (mode_dependent_address_p (XEXP (inner, 0))
	      || MEM_VOLATILE_P (inner)
	      || pos_rtx))
	wanted_inner_mode = extraction_mode;
    }

  orig_pos = pos;

  if (BITS_BIG_ENDIAN)
    {
      /* POS is passed as if BITS_BIG_ENDIAN == 0, so we need to convert it to
	 BITS_BIG_ENDIAN style.  If position is constant, compute new
	 position.  Otherwise, build subtraction.
	 Note that POS is relative to the mode of the original argument.
	 If it's a MEM we need to recompute POS relative to that.
	 However, if we're extracting from (or inserting into) a register,
	 we want to recompute POS relative to wanted_inner_mode.  */
      int width = (MEM_P (inner)
		   ? GET_MODE_BITSIZE (is_mode)
		   : GET_MODE_BITSIZE (wanted_inner_mode));

      if (pos_rtx == 0)
	pos = width - len - pos;
      else
	pos_rtx
	  = gen_rtx_MINUS (GET_MODE (pos_rtx), GEN_INT (width - len), pos_rtx);
      /* POS may be less than 0 now, but we check for that below.
	 Note that it can only be less than 0 if !MEM_P (inner).  */
    }

  /* If INNER has a wider mode, and this is a constant extraction, try to
     make it smaller and adjust the byte to point to the byte containing
     the value.  */
  if (wanted_inner_mode != VOIDmode
      && inner_mode != wanted_inner_mode
      && ! pos_rtx
      && GET_MODE_SIZE (wanted_inner_mode) < GET_MODE_SIZE (is_mode)
      && MEM_P (inner)
      && ! mode_dependent_address_p (XEXP (inner, 0))
      && ! MEM_VOLATILE_P (inner))
    {
      int offset = 0;

      /* The computations below will be correct if the machine is big
	 endian in both bits and bytes or little endian in bits and bytes.
	 If it is mixed, we must adjust.  */

      /* If bytes are big endian and we had a paradoxical SUBREG, we must
	 adjust OFFSET to compensate.  */
      if (BYTES_BIG_ENDIAN
	  && GET_MODE_SIZE (inner_mode) < GET_MODE_SIZE (is_mode))
	offset -= GET_MODE_SIZE (is_mode) - GET_MODE_SIZE (inner_mode);

      /* We can now move to the desired byte.  */
      offset += (pos / GET_MODE_BITSIZE (wanted_inner_mode))
		* GET_MODE_SIZE (wanted_inner_mode);
      pos %= GET_MODE_BITSIZE (wanted_inner_mode);

      if (BYTES_BIG_ENDIAN != BITS_BIG_ENDIAN
	  && is_mode != wanted_inner_mode)
	offset = (GET_MODE_SIZE (is_mode)
		  - GET_MODE_SIZE (wanted_inner_mode) - offset);

      inner = adjust_address_nv (inner, wanted_inner_mode, offset);
    }

  /* If INNER is not memory, we can always get it into the proper mode.  If we
     are changing its mode, POS must be a constant and smaller than the size
     of the new mode.  */
  else if (!MEM_P (inner))
    {
      if (GET_MODE (inner) != wanted_inner_mode
	  && (pos_rtx != 0
	      || orig_pos + len > GET_MODE_BITSIZE (wanted_inner_mode)))
	return 0;

      if (orig_pos < 0)
	return 0;

      inner = force_to_mode (inner, wanted_inner_mode,
			     pos_rtx
			     || len + orig_pos >= HOST_BITS_PER_WIDE_INT
			     ? ~(unsigned HOST_WIDE_INT) 0
			     : ((((unsigned HOST_WIDE_INT) 1 << len) - 1)
				<< orig_pos),
			     0);
    }

  /* Adjust mode of POS_RTX, if needed.  If we want a wider mode, we
     have to zero extend.  Otherwise, we can just use a SUBREG.  */
  if (pos_rtx != 0
      && GET_MODE_SIZE (pos_mode) > GET_MODE_SIZE (GET_MODE (pos_rtx)))
    {
      rtx temp = gen_rtx_ZERO_EXTEND (pos_mode, pos_rtx);

      /* If we know that no extraneous bits are set, and that the high
	 bit is not set, convert extraction to cheaper one - either
	 SIGN_EXTENSION or ZERO_EXTENSION, that are equivalent in these
	 cases.  */
      if (flag_expensive_optimizations
	  && (GET_MODE_BITSIZE (GET_MODE (pos_rtx)) <= HOST_BITS_PER_WIDE_INT
	      && ((nonzero_bits (pos_rtx, GET_MODE (pos_rtx))
		   & ~(((unsigned HOST_WIDE_INT)
			GET_MODE_MASK (GET_MODE (pos_rtx)))
		       >> 1))
		  == 0)))
	{
	  rtx temp1 = gen_rtx_SIGN_EXTEND (pos_mode, pos_rtx);

	  /* Prefer ZERO_EXTENSION, since it gives more information to
	     backends.  */
	  if (rtx_cost (temp1, SET) < rtx_cost (temp, SET))
	    temp = temp1;
	}
      pos_rtx = temp;
    }
  else if (pos_rtx != 0
	   && GET_MODE_SIZE (pos_mode) < GET_MODE_SIZE (GET_MODE (pos_rtx)))
    pos_rtx = gen_lowpart (pos_mode, pos_rtx);

  /* Make POS_RTX unless we already have it and it is correct.  If we don't
     have a POS_RTX but we do have an ORIG_POS_RTX, the latter must
     be a CONST_INT.  */
  if (pos_rtx == 0 && orig_pos_rtx != 0 && INTVAL (orig_pos_rtx) == pos)
    pos_rtx = orig_pos_rtx;

  else if (pos_rtx == 0)
    pos_rtx = GEN_INT (pos);

  /* Make the required operation.  See if we can use existing rtx.  */
  new = gen_rtx_fmt_eee (unsignedp ? ZERO_EXTRACT : SIGN_EXTRACT,
			 extraction_mode, inner, GEN_INT (len), pos_rtx);
  if (! in_dest)
    new = gen_lowpart (mode, new);

  return new;
}

/* See if X contains an ASHIFT of COUNT or more bits that can be commuted
   with any other operations in X.  Return X without that shift if so.  */

static rtx
extract_left_shift (rtx x, int count)
{
  enum rtx_code code = GET_CODE (x);
  enum machine_mode mode = GET_MODE (x);
  rtx tem;

  switch (code)
    {
    case ASHIFT:
      /* This is the shift itself.  If it is wide enough, we will return
	 either the value being shifted if the shift count is equal to
	 COUNT or a shift for the difference.  */
      if (GET_CODE (XEXP (x, 1)) == CONST_INT
	  && INTVAL (XEXP (x, 1)) >= count)
	return simplify_shift_const (NULL_RTX, ASHIFT, mode, XEXP (x, 0),
				     INTVAL (XEXP (x, 1)) - count);
      break;

    case NEG:  case NOT:
      if ((tem = extract_left_shift (XEXP (x, 0), count)) != 0)
	return simplify_gen_unary (code, mode, tem, mode);

      break;

    case PLUS:  case IOR:  case XOR:  case AND:
      /* If we can safely shift this constant and we find the inner shift,
	 make a new operation.  */
      if (GET_CODE (XEXP (x, 1)) == CONST_INT
	  && (INTVAL (XEXP (x, 1)) & ((((HOST_WIDE_INT) 1 << count)) - 1)) == 0
	  && (tem = extract_left_shift (XEXP (x, 0), count)) != 0)
	return simplify_gen_binary (code, mode, tem,
				    GEN_INT (INTVAL (XEXP (x, 1)) >> count));

      break;

    default:
      break;
    }

  return 0;
}

/* Look at the expression rooted at X.  Look for expressions
   equivalent to ZERO_EXTRACT, SIGN_EXTRACT, ZERO_EXTEND, SIGN_EXTEND.
   Form these expressions.

   Return the new rtx, usually just X.

   Also, for machines like the VAX that don't have logical shift insns,
   try to convert logical to arithmetic shift operations in cases where
   they are equivalent.  This undoes the canonicalizations to logical
   shifts done elsewhere.

   We try, as much as possible, to re-use rtl expressions to save memory.

   IN_CODE says what kind of expression we are processing.  Normally, it is
   SET.  In a memory address (inside a MEM, PLUS or minus, the latter two
   being kludges), it is MEM.  When processing the arguments of a comparison
   or a COMPARE against zero, it is COMPARE.  */

static rtx
make_compound_operation (rtx x, enum rtx_code in_code)
{
  enum rtx_code code = GET_CODE (x);
  enum machine_mode mode = GET_MODE (x);
  int mode_width = GET_MODE_BITSIZE (mode);
  rtx rhs, lhs;
  enum rtx_code next_code;
  int i;
  rtx new = 0;
  rtx tem;
  const char *fmt;

  /* Select the code to be used in recursive calls.  Once we are inside an
     address, we stay there.  If we have a comparison, set to COMPARE,
     but once inside, go back to our default of SET.  */

  next_code = (code == MEM || code == PLUS || code == MINUS ? MEM
	       : ((code == COMPARE || COMPARISON_P (x))
		  && XEXP (x, 1) == const0_rtx) ? COMPARE
	       : in_code == COMPARE ? SET : in_code);

  /* Process depending on the code of this operation.  If NEW is set
     nonzero, it will be returned.  */

  switch (code)
    {
    case ASHIFT:
      /* Convert shifts by constants into multiplications if inside
	 an address.  */
      if (in_code == MEM && GET_CODE (XEXP (x, 1)) == CONST_INT
	  && INTVAL (XEXP (x, 1)) < HOST_BITS_PER_WIDE_INT
	  && INTVAL (XEXP (x, 1)) >= 0)
	{
	  new = make_compound_operation (XEXP (x, 0), next_code);
	  new = gen_rtx_MULT (mode, new,
			      GEN_INT ((HOST_WIDE_INT) 1
				       << INTVAL (XEXP (x, 1))));
	}
      break;

    case AND:
      /* If the second operand is not a constant, we can't do anything
	 with it.  */
      if (GET_CODE (XEXP (x, 1)) != CONST_INT)
	break;

      /* If the constant is a power of two minus one and the first operand
	 is a logical right shift, make an extraction.  */
      if (GET_CODE (XEXP (x, 0)) == LSHIFTRT
	  && (i = exact_log2 (INTVAL (XEXP (x, 1)) + 1)) >= 0)
	{
	  new = make_compound_operation (XEXP (XEXP (x, 0), 0), next_code);
	  new = make_extraction (mode, new, 0, XEXP (XEXP (x, 0), 1), i, 1,
				 0, in_code == COMPARE);
	}

      /* Same as previous, but for (subreg (lshiftrt ...)) in first op.  */
      else if (GET_CODE (XEXP (x, 0)) == SUBREG
	       && subreg_lowpart_p (XEXP (x, 0))
	       && GET_CODE (SUBREG_REG (XEXP (x, 0))) == LSHIFTRT
	       && (i = exact_log2 (INTVAL (XEXP (x, 1)) + 1)) >= 0)
	{
	  new = make_compound_operation (XEXP (SUBREG_REG (XEXP (x, 0)), 0),
					 next_code);
	  new = make_extraction (GET_MODE (SUBREG_REG (XEXP (x, 0))), new, 0,
				 XEXP (SUBREG_REG (XEXP (x, 0)), 1), i, 1,
				 0, in_code == COMPARE);
	}
      /* Same as previous, but for (xor/ior (lshiftrt...) (lshiftrt...)).  */
      else if ((GET_CODE (XEXP (x, 0)) == XOR
		|| GET_CODE (XEXP (x, 0)) == IOR)
	       && GET_CODE (XEXP (XEXP (x, 0), 0)) == LSHIFTRT
	       && GET_CODE (XEXP (XEXP (x, 0), 1)) == LSHIFTRT
	       && (i = exact_log2 (INTVAL (XEXP (x, 1)) + 1)) >= 0)
	{
	  /* Apply the distributive law, and then try to make extractions.  */
	  new = gen_rtx_fmt_ee (GET_CODE (XEXP (x, 0)), mode,
				gen_rtx_AND (mode, XEXP (XEXP (x, 0), 0),
					     XEXP (x, 1)),
				gen_rtx_AND (mode, XEXP (XEXP (x, 0), 1),
					     XEXP (x, 1)));
	  new = make_compound_operation (new, in_code);
	}

      /* If we are have (and (rotate X C) M) and C is larger than the number
	 of bits in M, this is an extraction.  */

      else if (GET_CODE (XEXP (x, 0)) == ROTATE
	       && GET_CODE (XEXP (XEXP (x, 0), 1)) == CONST_INT
	       && (i = exact_log2 (INTVAL (XEXP (x, 1)) + 1)) >= 0
	       && i <= INTVAL (XEXP (XEXP (x, 0), 1)))
	{
	  new = make_compound_operation (XEXP (XEXP (x, 0), 0), next_code);
	  new = make_extraction (mode, new,
				 (GET_MODE_BITSIZE (mode)
				  - INTVAL (XEXP (XEXP (x, 0), 1))),
				 NULL_RTX, i, 1, 0, in_code == COMPARE);
	}

      /* On machines without logical shifts, if the operand of the AND is
	 a logical shift and our mask turns off all the propagated sign
	 bits, we can replace the logical shift with an arithmetic shift.  */
      else if (GET_CODE (XEXP (x, 0)) == LSHIFTRT
	       && !have_insn_for (LSHIFTRT, mode)
	       && have_insn_for (ASHIFTRT, mode)
	       && GET_CODE (XEXP (XEXP (x, 0), 1)) == CONST_INT
	       && INTVAL (XEXP (XEXP (x, 0), 1)) >= 0
	       && INTVAL (XEXP (XEXP (x, 0), 1)) < HOST_BITS_PER_WIDE_INT
	       && mode_width <= HOST_BITS_PER_WIDE_INT)
	{
	  unsigned HOST_WIDE_INT mask = GET_MODE_MASK (mode);

	  mask >>= INTVAL (XEXP (XEXP (x, 0), 1));
	  if ((INTVAL (XEXP (x, 1)) & ~mask) == 0)
	    SUBST (XEXP (x, 0),
		   gen_rtx_ASHIFTRT (mode,
				     make_compound_operation
				     (XEXP (XEXP (x, 0), 0), next_code),
				     XEXP (XEXP (x, 0), 1)));
	}

      /* If the constant is one less than a power of two, this might be
	 representable by an extraction even if no shift is present.
	 If it doesn't end up being a ZERO_EXTEND, we will ignore it unless
	 we are in a COMPARE.  */
      else if ((i = exact_log2 (INTVAL (XEXP (x, 1)) + 1)) >= 0)
	new = make_extraction (mode,
			       make_compound_operation (XEXP (x, 0),
							next_code),
			       0, NULL_RTX, i, 1, 0, in_code == COMPARE);

      /* If we are in a comparison and this is an AND with a power of two,
	 convert this into the appropriate bit extract.  */
      else if (in_code == COMPARE
	       && (i = exact_log2 (INTVAL (XEXP (x, 1)))) >= 0)
	new = make_extraction (mode,
			       make_compound_operation (XEXP (x, 0),
							next_code),
			       i, NULL_RTX, 1, 1, 0, 1);

      break;

    case LSHIFTRT:
      /* If the sign bit is known to be zero, replace this with an
	 arithmetic shift.  */
      if (have_insn_for (ASHIFTRT, mode)
	  && ! have_insn_for (LSHIFTRT, mode)
	  && mode_width <= HOST_BITS_PER_WIDE_INT
	  && (nonzero_bits (XEXP (x, 0), mode) & (1 << (mode_width - 1))) == 0)
	{
	  new = gen_rtx_ASHIFTRT (mode,
				  make_compound_operation (XEXP (x, 0),
							   next_code),
				  XEXP (x, 1));
	  break;
	}

      /* ... fall through ...  */

    case ASHIFTRT:
      lhs = XEXP (x, 0);
      rhs = XEXP (x, 1);

      /* If we have (ashiftrt (ashift foo C1) C2) with C2 >= C1,
	 this is a SIGN_EXTRACT.  */
      if (GET_CODE (rhs) == CONST_INT
	  && GET_CODE (lhs) == ASHIFT
	  && GET_CODE (XEXP (lhs, 1)) == CONST_INT
	  && INTVAL (rhs) >= INTVAL (XEXP (lhs, 1)))
	{
	  new = make_compound_operation (XEXP (lhs, 0), next_code);
	  new = make_extraction (mode, new,
				 INTVAL (rhs) - INTVAL (XEXP (lhs, 1)),
				 NULL_RTX, mode_width - INTVAL (rhs),
				 code == LSHIFTRT, 0, in_code == COMPARE);
	  break;
	}

      /* See if we have operations between an ASHIFTRT and an ASHIFT.
	 If so, try to merge the shifts into a SIGN_EXTEND.  We could
	 also do this for some cases of SIGN_EXTRACT, but it doesn't
	 seem worth the effort; the case checked for occurs on Alpha.  */

      if (!OBJECT_P (lhs)
	  && ! (GET_CODE (lhs) == SUBREG
		&& (OBJECT_P (SUBREG_REG (lhs))))
	  && GET_CODE (rhs) == CONST_INT
	  && INTVAL (rhs) < HOST_BITS_PER_WIDE_INT
	  && (new = extract_left_shift (lhs, INTVAL (rhs))) != 0)
	new = make_extraction (mode, make_compound_operation (new, next_code),
			       0, NULL_RTX, mode_width - INTVAL (rhs),
			       code == LSHIFTRT, 0, in_code == COMPARE);

      break;

    case SUBREG:
      /* Call ourselves recursively on the inner expression.  If we are
	 narrowing the object and it has a different RTL code from
	 what it originally did, do this SUBREG as a force_to_mode.  */

      tem = make_compound_operation (SUBREG_REG (x), in_code);

      {
	rtx simplified;
	simplified = simplify_subreg (GET_MODE (x), tem, GET_MODE (tem),
				      SUBREG_BYTE (x));

	if (simplified)
	  tem = simplified;

	if (GET_CODE (tem) != GET_CODE (SUBREG_REG (x))
	    && GET_MODE_SIZE (mode) < GET_MODE_SIZE (GET_MODE (tem))
	    && subreg_lowpart_p (x))
	  {
	    rtx newer = force_to_mode (tem, mode, ~(HOST_WIDE_INT) 0,
				       0);

	    /* If we have something other than a SUBREG, we might have
	       done an expansion, so rerun ourselves.  */
	    if (GET_CODE (newer) != SUBREG)
	      newer = make_compound_operation (newer, in_code);

	    return newer;
	  }

	if (simplified)
	  return tem;
      }
      break;

    default:
      break;
    }

  if (new)
    {
      x = gen_lowpart (mode, new);
      code = GET_CODE (x);
    }

  /* Now recursively process each operand of this operation.  */
  fmt = GET_RTX_FORMAT (code);
  for (i = 0; i < GET_RTX_LENGTH (code); i++)
    if (fmt[i] == 'e')
      {
	new = make_compound_operation (XEXP (x, i), next_code);
	SUBST (XEXP (x, i), new);
      }

  /* If this is a commutative operation, the changes to the operands
     may have made it noncanonical.  */
  if (COMMUTATIVE_ARITH_P (x)
      && swap_commutative_operands_p (XEXP (x, 0), XEXP (x, 1)))
    {
      tem = XEXP (x, 0);
      SUBST (XEXP (x, 0), XEXP (x, 1));
      SUBST (XEXP (x, 1), tem);
    }

  return x;
}

/* Given M see if it is a value that would select a field of bits
   within an item, but not the entire word.  Return -1 if not.
   Otherwise, return the starting position of the field, where 0 is the
   low-order bit.

   *PLEN is set to the length of the field.  */

static int
get_pos_from_mask (unsigned HOST_WIDE_INT m, unsigned HOST_WIDE_INT *plen)
{
  /* Get the bit number of the first 1 bit from the right, -1 if none.  */
  int pos = exact_log2 (m & -m);
  int len = 0;

  if (pos >= 0)
    /* Now shift off the low-order zero bits and see if we have a
       power of two minus 1.  */
    len = exact_log2 ((m >> pos) + 1);

  if (len <= 0)
    pos = -1;

  *plen = len;
  return pos;
}

/* If X refers to a register that equals REG in value, replace these
   references with REG.  */
static rtx
canon_reg_for_combine (rtx x, rtx reg)
{
  rtx op0, op1, op2;
  const char *fmt;
  int i;
  bool copied;

  enum rtx_code code = GET_CODE (x);
  switch (GET_RTX_CLASS (code))
    {
    case RTX_UNARY:
      op0 = canon_reg_for_combine (XEXP (x, 0), reg);
      if (op0 != XEXP (x, 0))
	return simplify_gen_unary (GET_CODE (x), GET_MODE (x), op0,
				   GET_MODE (reg));
      break;

    case RTX_BIN_ARITH:
    case RTX_COMM_ARITH:
      op0 = canon_reg_for_combine (XEXP (x, 0), reg);
      op1 = canon_reg_for_combine (XEXP (x, 1), reg);
      if (op0 != XEXP (x, 0) || op1 != XEXP (x, 1))
	return simplify_gen_binary (GET_CODE (x), GET_MODE (x), op0, op1);
      break;

    case RTX_COMPARE:
    case RTX_COMM_COMPARE:
      op0 = canon_reg_for_combine (XEXP (x, 0), reg);
      op1 = canon_reg_for_combine (XEXP (x, 1), reg);
      if (op0 != XEXP (x, 0) || op1 != XEXP (x, 1))
	return simplify_gen_relational (GET_CODE (x), GET_MODE (x),
					GET_MODE (op0), op0, op1);
      break;

    case RTX_TERNARY:
    case RTX_BITFIELD_OPS:
      op0 = canon_reg_for_combine (XEXP (x, 0), reg);
      op1 = canon_reg_for_combine (XEXP (x, 1), reg);
      op2 = canon_reg_for_combine (XEXP (x, 2), reg);
      if (op0 != XEXP (x, 0) || op1 != XEXP (x, 1) || op2 != XEXP (x, 2))
	return simplify_gen_ternary (GET_CODE (x), GET_MODE (x),
				     GET_MODE (op0), op0, op1, op2);

    case RTX_OBJ:
      if (REG_P (x))
	{
	  if (rtx_equal_p (get_last_value (reg), x)
	      || rtx_equal_p (reg, get_last_value (x)))
	    return reg;
	  else
	    break;
	}

      /* fall through */

    default:
      fmt = GET_RTX_FORMAT (code);
      copied = false;
      for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
	if (fmt[i] == 'e')
	  {
	    rtx op = canon_reg_for_combine (XEXP (x, i), reg);
	    if (op != XEXP (x, i))
	      {
		if (!copied)
		  {
		    copied = true;
		    x = copy_rtx (x);
		  }
		XEXP (x, i) = op;
	      }
	  }
	else if (fmt[i] == 'E')
	  {
	    int j;
	    for (j = 0; j < XVECLEN (x, i); j++)
	      {
		rtx op = canon_reg_for_combine (XVECEXP (x, i, j), reg);
		if (op != XVECEXP (x, i, j))
		  {
		    if (!copied)
		      {
			copied = true;
			x = copy_rtx (x);
		      }
		    XVECEXP (x, i, j) = op;
		  }
	      }
	  }

      break;
    }

  return x;
}

/* Return X converted to MODE.  If the value is already truncated to
   MODE we can just return a subreg even though in the general case we
   would need an explicit truncation.  */

static rtx
gen_lowpart_or_truncate (enum machine_mode mode, rtx x)
{
  if (GET_MODE_SIZE (GET_MODE (x)) <= GET_MODE_SIZE (mode)
      || TRULY_NOOP_TRUNCATION (GET_MODE_BITSIZE (mode),
				GET_MODE_BITSIZE (GET_MODE (x)))
      || (REG_P (x) && reg_truncated_to_mode (mode, x)))
    return gen_lowpart (mode, x);
  else
    return simplify_gen_unary (TRUNCATE, mode, x, GET_MODE (x));
}

/* See if X can be simplified knowing that we will only refer to it in
   MODE and will only refer to those bits that are nonzero in MASK.
   If other bits are being computed or if masking operations are done
   that select a superset of the bits in MASK, they can sometimes be
   ignored.

   Return a possibly simplified expression, but always convert X to
   MODE.  If X is a CONST_INT, AND the CONST_INT with MASK.

   If JUST_SELECT is nonzero, don't optimize by noticing that bits in MASK
   are all off in X.  This is used when X will be complemented, by either
   NOT, NEG, or XOR.  */

static rtx
force_to_mode (rtx x, enum machine_mode mode, unsigned HOST_WIDE_INT mask,
	       int just_select)
{
  enum rtx_code code = GET_CODE (x);
  int next_select = just_select || code == XOR || code == NOT || code == NEG;
  enum machine_mode op_mode;
  unsigned HOST_WIDE_INT fuller_mask, nonzero;
  rtx op0, op1, temp;

  /* If this is a CALL or ASM_OPERANDS, don't do anything.  Some of the
     code below will do the wrong thing since the mode of such an
     expression is VOIDmode.

     Also do nothing if X is a CLOBBER; this can happen if X was
     the return value from a call to gen_lowpart.  */
  if (code == CALL || code == ASM_OPERANDS || code == CLOBBER)
    return x;

  /* We want to perform the operation is its present mode unless we know
     that the operation is valid in MODE, in which case we do the operation
     in MODE.  */
  op_mode = ((GET_MODE_CLASS (mode) == GET_MODE_CLASS (GET_MODE (x))
	      && have_insn_for (code, mode))
	     ? mode : GET_MODE (x));

  /* It is not valid to do a right-shift in a narrower mode
     than the one it came in with.  */
  if ((code == LSHIFTRT || code == ASHIFTRT)
      && GET_MODE_BITSIZE (mode) < GET_MODE_BITSIZE (GET_MODE (x)))
    op_mode = GET_MODE (x);

  /* Truncate MASK to fit OP_MODE.  */
  if (op_mode)
    mask &= GET_MODE_MASK (op_mode);

  /* When we have an arithmetic operation, or a shift whose count we
     do not know, we need to assume that all bits up to the highest-order
     bit in MASK will be needed.  This is how we form such a mask.  */
  if (mask & ((unsigned HOST_WIDE_INT) 1 << (HOST_BITS_PER_WIDE_INT - 1)))
    fuller_mask = ~(unsigned HOST_WIDE_INT) 0;
  else
    fuller_mask = (((unsigned HOST_WIDE_INT) 1 << (floor_log2 (mask) + 1))
		   - 1);

  /* Determine what bits of X are guaranteed to be (non)zero.  */
  nonzero = nonzero_bits (x, mode);

  /* If none of the bits in X are needed, return a zero.  */
  if (!just_select && (nonzero & mask) == 0 && !side_effects_p (x))
    x = const0_rtx;

  /* If X is a CONST_INT, return a new one.  Do this here since the
     test below will fail.  */
  if (GET_CODE (x) == CONST_INT)
    {
      if (SCALAR_INT_MODE_P (mode))
	return gen_int_mode (INTVAL (x) & mask, mode);
      else
	{
	  x = GEN_INT (INTVAL (x) & mask);
	  return gen_lowpart_common (mode, x);
	}
    }

  /* If X is narrower than MODE and we want all the bits in X's mode, just
     get X in the proper mode.  */
  if (GET_MODE_SIZE (GET_MODE (x)) < GET_MODE_SIZE (mode)
      && (GET_MODE_MASK (GET_MODE (x)) & ~mask) == 0)
    return gen_lowpart (mode, x);

  switch (code)
    {
    case CLOBBER:
      /* If X is a (clobber (const_int)), return it since we know we are
	 generating something that won't match.  */
      return x;

    case SIGN_EXTEND:
    case ZERO_EXTEND:
    case ZERO_EXTRACT:
    case SIGN_EXTRACT:
      x = expand_compound_operation (x);
      if (GET_CODE (x) != code)
	return force_to_mode (x, mode, mask, next_select);
      break;

    case SUBREG:
      if (subreg_lowpart_p (x)
	  /* We can ignore the effect of this SUBREG if it narrows the mode or
	     if the constant masks to zero all the bits the mode doesn't
	     have.  */
	  && ((GET_MODE_SIZE (GET_MODE (x))
	       < GET_MODE_SIZE (GET_MODE (SUBREG_REG (x))))
	      || (0 == (mask
			& GET_MODE_MASK (GET_MODE (x))
			& ~GET_MODE_MASK (GET_MODE (SUBREG_REG (x)))))))
	return force_to_mode (SUBREG_REG (x), mode, mask, next_select);
      break;

    case AND:
      /* If this is an AND with a constant, convert it into an AND
	 whose constant is the AND of that constant with MASK.  If it
	 remains an AND of MASK, delete it since it is redundant.  */

      if (GET_CODE (XEXP (x, 1)) == CONST_INT)
	{
	  x = simplify_and_const_int (x, op_mode, XEXP (x, 0),
				      mask & INTVAL (XEXP (x, 1)));

	  /* If X is still an AND, see if it is an AND with a mask that
	     is just some low-order bits.  If so, and it is MASK, we don't
	     need it.  */

	  if (GET_CODE (x) == AND && GET_CODE (XEXP (x, 1)) == CONST_INT
	      && ((INTVAL (XEXP (x, 1)) & GET_MODE_MASK (GET_MODE (x)))
		  == mask))
	    x = XEXP (x, 0);

	  /* If it remains an AND, try making another AND with the bits
	     in the mode mask that aren't in MASK turned on.  If the
	     constant in the AND is wide enough, this might make a
	     cheaper constant.  */

	  if (GET_CODE (x) == AND && GET_CODE (XEXP (x, 1)) == CONST_INT
	      && GET_MODE_MASK (GET_MODE (x)) != mask
	      && GET_MODE_BITSIZE (GET_MODE (x)) <= HOST_BITS_PER_WIDE_INT)
	    {
	      HOST_WIDE_INT cval = (INTVAL (XEXP (x, 1))
				    | (GET_MODE_MASK (GET_MODE (x)) & ~mask));
	      int width = GET_MODE_BITSIZE (GET_MODE (x));
	      rtx y;

	      /* If MODE is narrower than HOST_WIDE_INT and CVAL is a negative
		 number, sign extend it.  */
	      if (width > 0 && width < HOST_BITS_PER_WIDE_INT
		  && (cval & ((HOST_WIDE_INT) 1 << (width - 1))) != 0)
		cval |= (HOST_WIDE_INT) -1 << width;

	      y = simplify_gen_binary (AND, GET_MODE (x),
				       XEXP (x, 0), GEN_INT (cval));
	      if (rtx_cost (y, SET) < rtx_cost (x, SET))
		x = y;
	    }

	  break;
	}

      goto binop;

    case PLUS:
      /* In (and (plus FOO C1) M), if M is a mask that just turns off
	 low-order bits (as in an alignment operation) and FOO is already
	 aligned to that boundary, mask C1 to that boundary as well.
	 This may eliminate that PLUS and, later, the AND.  */

      {
	unsigned int width = GET_MODE_BITSIZE (mode);
	unsigned HOST_WIDE_INT smask = mask;

	/* If MODE is narrower than HOST_WIDE_INT and mask is a negative
	   number, sign extend it.  */

	if (width < HOST_BITS_PER_WIDE_INT
	    && (smask & ((HOST_WIDE_INT) 1 << (width - 1))) != 0)
	  smask |= (HOST_WIDE_INT) -1 << width;

	if (GET_CODE (XEXP (x, 1)) == CONST_INT
	    && exact_log2 (- smask) >= 0
	    && (nonzero_bits (XEXP (x, 0), mode) & ~smask) == 0
	    && (INTVAL (XEXP (x, 1)) & ~smask) != 0)
	  return force_to_mode (plus_constant (XEXP (x, 0),
					       (INTVAL (XEXP (x, 1)) & smask)),
				mode, smask, next_select);
      }

      /* ... fall through ...  */

    case MULT:
      /* For PLUS, MINUS and MULT, we need any bits less significant than the
	 most significant bit in MASK since carries from those bits will
	 affect the bits we are interested in.  */
      mask = fuller_mask;
      goto binop;

    case MINUS:
      /* If X is (minus C Y) where C's least set bit is larger than any bit
	 in the mask, then we may replace with (neg Y).  */
      if (GET_CODE (XEXP (x, 0)) == CONST_INT
	  && (((unsigned HOST_WIDE_INT) (INTVAL (XEXP (x, 0))
					& -INTVAL (XEXP (x, 0))))
	      > mask))
	{
	  x = simplify_gen_unary (NEG, GET_MODE (x), XEXP (x, 1),
				  GET_MODE (x));
	  return force_to_mode (x, mode, mask, next_select);
	}

      /* Similarly, if C contains every bit in the fuller_mask, then we may
	 replace with (not Y).  */
      if (GET_CODE (XEXP (x, 0)) == CONST_INT
	  && ((INTVAL (XEXP (x, 0)) | (HOST_WIDE_INT) fuller_mask)
	      == INTVAL (XEXP (x, 0))))
	{
	  x = simplify_gen_unary (NOT, GET_MODE (x),
				  XEXP (x, 1), GET_MODE (x));
	  return force_to_mode (x, mode, mask, next_select);
	}

      mask = fuller_mask;
      goto binop;

    case IOR:
    case XOR:
      /* If X is (ior (lshiftrt FOO C1) C2), try to commute the IOR and
	 LSHIFTRT so we end up with an (and (lshiftrt (ior ...) ...) ...)
	 operation which may be a bitfield extraction.  Ensure that the
	 constant we form is not wider than the mode of X.  */

      if (GET_CODE (XEXP (x, 0)) == LSHIFTRT
	  && GET_CODE (XEXP (XEXP (x, 0), 1)) == CONST_INT
	  && INTVAL (XEXP (XEXP (x, 0), 1)) >= 0
	  && INTVAL (XEXP (XEXP (x, 0), 1)) < HOST_BITS_PER_WIDE_INT
	  && GET_CODE (XEXP (x, 1)) == CONST_INT
	  && ((INTVAL (XEXP (XEXP (x, 0), 1))
	       + floor_log2 (INTVAL (XEXP (x, 1))))
	      < GET_MODE_BITSIZE (GET_MODE (x)))
	  && (INTVAL (XEXP (x, 1))
	      & ~nonzero_bits (XEXP (x, 0), GET_MODE (x))) == 0)
	{
	  temp = GEN_INT ((INTVAL (XEXP (x, 1)) & mask)
			  << INTVAL (XEXP (XEXP (x, 0), 1)));
	  temp = simplify_gen_binary (GET_CODE (x), GET_MODE (x),
				      XEXP (XEXP (x, 0), 0), temp);
	  x = simplify_gen_binary (LSHIFTRT, GET_MODE (x), temp,
				   XEXP (XEXP (x, 0), 1));
	  return force_to_mode (x, mode, mask, next_select);
	}

    binop:
      /* For most binary operations, just propagate into the operation and
	 change the mode if we have an operation of that mode.  */

      op0 = gen_lowpart_or_truncate (op_mode,
				     force_to_mode (XEXP (x, 0), mode, mask,
						    next_select));
      op1 = gen_lowpart_or_truncate (op_mode,
				     force_to_mode (XEXP (x, 1), mode, mask,
					next_select));

      if (op_mode != GET_MODE (x) || op0 != XEXP (x, 0) || op1 != XEXP (x, 1))
	x = simplify_gen_binary (code, op_mode, op0, op1);
      break;

    case ASHIFT:
      /* For left shifts, do the same, but just for the first operand.
	 However, we cannot do anything with shifts where we cannot
	 guarantee that the counts are smaller than the size of the mode
	 because such a count will have a different meaning in a
	 wider mode.  */

      if (! (GET_CODE (XEXP (x, 1)) == CONST_INT
	     && INTVAL (XEXP (x, 1)) >= 0
	     && INTVAL (XEXP (x, 1)) < GET_MODE_BITSIZE (mode))
	  && ! (GET_MODE (XEXP (x, 1)) != VOIDmode
		&& (nonzero_bits (XEXP (x, 1), GET_MODE (XEXP (x, 1)))
		    < (unsigned HOST_WIDE_INT) GET_MODE_BITSIZE (mode))))
	break;

      /* If the shift count is a constant and we can do arithmetic in
	 the mode of the shift, refine which bits we need.  Otherwise, use the
	 conservative form of the mask.  */
      if (GET_CODE (XEXP (x, 1)) == CONST_INT
	  && INTVAL (XEXP (x, 1)) >= 0
	  && INTVAL (XEXP (x, 1)) < GET_MODE_BITSIZE (op_mode)
	  && GET_MODE_BITSIZE (op_mode) <= HOST_BITS_PER_WIDE_INT)
	mask >>= INTVAL (XEXP (x, 1));
      else
	mask = fuller_mask;

      op0 = gen_lowpart_or_truncate (op_mode,
				     force_to_mode (XEXP (x, 0), op_mode,
						    mask, next_select));

      if (op_mode != GET_MODE (x) || op0 != XEXP (x, 0))
	x = simplify_gen_binary (code, op_mode, op0, XEXP (x, 1));
      break;

    case LSHIFTRT:
      /* Here we can only do something if the shift count is a constant,
	 this shift constant is valid for the host, and we can do arithmetic
	 in OP_MODE.  */

      if (GET_CODE (XEXP (x, 1)) == CONST_INT
	  && INTVAL (XEXP (x, 1)) < HOST_BITS_PER_WIDE_INT
	  && GET_MODE_BITSIZE (op_mode) <= HOST_BITS_PER_WIDE_INT)
	{
	  rtx inner = XEXP (x, 0);
	  unsigned HOST_WIDE_INT inner_mask;

	  /* Select the mask of the bits we need for the shift operand.  */
	  inner_mask = mask << INTVAL (XEXP (x, 1));

	  /* We can only change the mode of the shift if we can do arithmetic
	     in the mode of the shift and INNER_MASK is no wider than the
	     width of X's mode.  */
	  if ((inner_mask & ~GET_MODE_MASK (GET_MODE (x))) != 0)
	    op_mode = GET_MODE (x);

	  inner = force_to_mode (inner, op_mode, inner_mask, next_select);

	  if (GET_MODE (x) != op_mode || inner != XEXP (x, 0))
	    x = simplify_gen_binary (LSHIFTRT, op_mode, inner, XEXP (x, 1));
	}

      /* If we have (and (lshiftrt FOO C1) C2) where the combination of the
	 shift and AND produces only copies of the sign bit (C2 is one less
	 than a power of two), we can do this with just a shift.  */

      if (GET_CODE (x) == LSHIFTRT
	  && GET_CODE (XEXP (x, 1)) == CONST_INT
	  /* The shift puts one of the sign bit copies in the least significant
	     bit.  */
	  && ((INTVAL (XEXP (x, 1))
	       + num_sign_bit_copies (XEXP (x, 0), GET_MODE (XEXP (x, 0))))
	      >= GET_MODE_BITSIZE (GET_MODE (x)))
	  && exact_log2 (mask + 1) >= 0
	  /* Number of bits left after the shift must be more than the mask
	     needs.  */
	  && ((INTVAL (XEXP (x, 1)) + exact_log2 (mask + 1))
	      <= GET_MODE_BITSIZE (GET_MODE (x)))
	  /* Must be more sign bit copies than the mask needs.  */
	  && ((int) num_sign_bit_copies (XEXP (x, 0), GET_MODE (XEXP (x, 0)))
	      >= exact_log2 (mask + 1)))
	x = simplify_gen_binary (LSHIFTRT, GET_MODE (x), XEXP (x, 0),
				 GEN_INT (GET_MODE_BITSIZE (GET_MODE (x))
					  - exact_log2 (mask + 1)));

      goto shiftrt;

    case ASHIFTRT:
      /* If we are just looking for the sign bit, we don't need this shift at
	 all, even if it has a variable count.  */
      if (GET_MODE_BITSIZE (GET_MODE (x)) <= HOST_BITS_PER_WIDE_INT
	  && (mask == ((unsigned HOST_WIDE_INT) 1
		       << (GET_MODE_BITSIZE (GET_MODE (x)) - 1))))
	return force_to_mode (XEXP (x, 0), mode, mask, next_select);

      /* If this is a shift by a constant, get a mask that contains those bits
	 that are not copies of the sign bit.  We then have two cases:  If
	 MASK only includes those bits, this can be a logical shift, which may
	 allow simplifications.  If MASK is a single-bit field not within
	 those bits, we are requesting a copy of the sign bit and hence can
	 shift the sign bit to the appropriate location.  */

      if (GET_CODE (XEXP (x, 1)) == CONST_INT && INTVAL (XEXP (x, 1)) >= 0
	  && INTVAL (XEXP (x, 1)) < HOST_BITS_PER_WIDE_INT)
	{
	  int i;

	  /* If the considered data is wider than HOST_WIDE_INT, we can't
	     represent a mask for all its bits in a single scalar.
	     But we only care about the lower bits, so calculate these.  */

	  if (GET_MODE_BITSIZE (GET_MODE (x)) > HOST_BITS_PER_WIDE_INT)
	    {
	      nonzero = ~(HOST_WIDE_INT) 0;

	      /* GET_MODE_BITSIZE (GET_MODE (x)) - INTVAL (XEXP (x, 1))
		 is the number of bits a full-width mask would have set.
		 We need only shift if these are fewer than nonzero can
		 hold.  If not, we must keep all bits set in nonzero.  */

	      if (GET_MODE_BITSIZE (GET_MODE (x)) - INTVAL (XEXP (x, 1))
		  < HOST_BITS_PER_WIDE_INT)
		nonzero >>= INTVAL (XEXP (x, 1))
			    + HOST_BITS_PER_WIDE_INT
			    - GET_MODE_BITSIZE (GET_MODE (x)) ;
	    }
	  else
	    {
	      nonzero = GET_MODE_MASK (GET_MODE (x));
	      nonzero >>= INTVAL (XEXP (x, 1));
	    }

	  if ((mask & ~nonzero) == 0)
	    {
	      x = simplify_shift_const (NULL_RTX, LSHIFTRT, GET_MODE (x),
					XEXP (x, 0), INTVAL (XEXP (x, 1)));
	      if (GET_CODE (x) != ASHIFTRT)
		return force_to_mode (x, mode, mask, next_select);
	    }

	  else if ((i = exact_log2 (mask)) >= 0)
	    {
	      x = simplify_shift_const
		  (NULL_RTX, LSHIFTRT, GET_MODE (x), XEXP (x, 0),
		   GET_MODE_BITSIZE (GET_MODE (x)) - 1 - i);

	      if (GET_CODE (x) != ASHIFTRT)
		return force_to_mode (x, mode, mask, next_select);
	    }
	}

      /* If MASK is 1, convert this to an LSHIFTRT.  This can be done
	 even if the shift count isn't a constant.  */
      if (mask == 1)
	x = simplify_gen_binary (LSHIFTRT, GET_MODE (x),
				 XEXP (x, 0), XEXP (x, 1));

    shiftrt:

      /* If this is a zero- or sign-extension operation that just affects bits
	 we don't care about, remove it.  Be sure the call above returned
	 something that is still a shift.  */

      if ((GET_CODE (x) == LSHIFTRT || GET_CODE (x) == ASHIFTRT)
	  && GET_CODE (XEXP (x, 1)) == CONST_INT
	  && INTVAL (XEXP (x, 1)) >= 0
	  && (INTVAL (XEXP (x, 1))
	      <= GET_MODE_BITSIZE (GET_MODE (x)) - (floor_log2 (mask) + 1))
	  && GET_CODE (XEXP (x, 0)) == ASHIFT
	  && XEXP (XEXP (x, 0), 1) == XEXP (x, 1))
	return force_to_mode (XEXP (XEXP (x, 0), 0), mode, mask,
			      next_select);

      break;

    case ROTATE:
    case ROTATERT:
      /* If the shift count is constant and we can do computations
	 in the mode of X, compute where the bits we care about are.
	 Otherwise, we can't do anything.  Don't change the mode of
	 the shift or propagate MODE into the shift, though.  */
      if (GET_CODE (XEXP (x, 1)) == CONST_INT
	  && INTVAL (XEXP (x, 1)) >= 0)
	{
	  temp = simplify_binary_operation (code == ROTATE ? ROTATERT : ROTATE,
					    GET_MODE (x), GEN_INT (mask),
					    XEXP (x, 1));
	  if (temp && GET_CODE (temp) == CONST_INT)
	    SUBST (XEXP (x, 0),
		   force_to_mode (XEXP (x, 0), GET_MODE (x),
				  INTVAL (temp), next_select));
	}
      break;

    case NEG:
      /* If we just want the low-order bit, the NEG isn't needed since it
	 won't change the low-order bit.  */
      if (mask == 1)
	return force_to_mode (XEXP (x, 0), mode, mask, just_select);

      /* We need any bits less significant than the most significant bit in
	 MASK since carries from those bits will affect the bits we are
	 interested in.  */
      mask = fuller_mask;
      goto unop;

    case NOT:
      /* (not FOO) is (xor FOO CONST), so if FOO is an LSHIFTRT, we can do the
	 same as the XOR case above.  Ensure that the constant we form is not
	 wider than the mode of X.  */

      if (GET_CODE (XEXP (x, 0)) == LSHIFTRT
	  && GET_CODE (XEXP (XEXP (x, 0), 1)) == CONST_INT
	  && INTVAL (XEXP (XEXP (x, 0), 1)) >= 0
	  && (INTVAL (XEXP (XEXP (x, 0), 1)) + floor_log2 (mask)
	      < GET_MODE_BITSIZE (GET_MODE (x)))
	  && INTVAL (XEXP (XEXP (x, 0), 1)) < HOST_BITS_PER_WIDE_INT)
	{
	  temp = gen_int_mode (mask << INTVAL (XEXP (XEXP (x, 0), 1)),
			       GET_MODE (x));
	  temp = simplify_gen_binary (XOR, GET_MODE (x),
				      XEXP (XEXP (x, 0), 0), temp);
	  x = simplify_gen_binary (LSHIFTRT, GET_MODE (x),
				   temp, XEXP (XEXP (x, 0), 1));

	  return force_to_mode (x, mode, mask, next_select);
	}

      /* (and (not FOO) CONST) is (not (or FOO (not CONST))), so we must
	 use the full mask inside the NOT.  */
      mask = fuller_mask;

    unop:
      op0 = gen_lowpart_or_truncate (op_mode,
				     force_to_mode (XEXP (x, 0), mode, mask,
						    next_select));
      if (op_mode != GET_MODE (x) || op0 != XEXP (x, 0))
	x = simplify_gen_unary (code, op_mode, op0, op_mode);
      break;

    case NE:
      /* (and (ne FOO 0) CONST) can be (and FOO CONST) if CONST is included
	 in STORE_FLAG_VALUE and FOO has a single bit that might be nonzero,
	 which is equal to STORE_FLAG_VALUE.  */
      if ((mask & ~STORE_FLAG_VALUE) == 0 && XEXP (x, 1) == const0_rtx
	  && GET_MODE (XEXP (x, 0)) == mode
	  && exact_log2 (nonzero_bits (XEXP (x, 0), mode)) >= 0
	  && (nonzero_bits (XEXP (x, 0), mode)
	      == (unsigned HOST_WIDE_INT) STORE_FLAG_VALUE))
	return force_to_mode (XEXP (x, 0), mode, mask, next_select);

      break;

    case IF_THEN_ELSE:
      /* We have no way of knowing if the IF_THEN_ELSE can itself be
	 written in a narrower mode.  We play it safe and do not do so.  */

      SUBST (XEXP (x, 1),
	     gen_lowpart_or_truncate (GET_MODE (x),
				      force_to_mode (XEXP (x, 1), mode,
						     mask, next_select)));
      SUBST (XEXP (x, 2),
	     gen_lowpart_or_truncate (GET_MODE (x),
				      force_to_mode (XEXP (x, 2), mode,
						     mask, next_select)));
      break;

    default:
      break;
    }

  /* Ensure we return a value of the proper mode.  */
  return gen_lowpart_or_truncate (mode, x);
}

/* Return nonzero if X is an expression that has one of two values depending on
   whether some other value is zero or nonzero.  In that case, we return the
   value that is being tested, *PTRUE is set to the value if the rtx being
   returned has a nonzero value, and *PFALSE is set to the other alternative.

   If we return zero, we set *PTRUE and *PFALSE to X.  */

static rtx
if_then_else_cond (rtx x, rtx *ptrue, rtx *pfalse)
{
  enum machine_mode mode = GET_MODE (x);
  enum rtx_code code = GET_CODE (x);
  rtx cond0, cond1, true0, true1, false0, false1;
  unsigned HOST_WIDE_INT nz;

  /* If we are comparing a value against zero, we are done.  */
  if ((code == NE || code == EQ)
      && XEXP (x, 1) == const0_rtx)
    {
      *ptrue = (code == NE) ? const_true_rtx : const0_rtx;
      *pfalse = (code == NE) ? const0_rtx : const_true_rtx;
      return XEXP (x, 0);
    }

  /* If this is a unary operation whose operand has one of two values, apply
     our opcode to compute those values.  */
  else if (UNARY_P (x)
	   && (cond0 = if_then_else_cond (XEXP (x, 0), &true0, &false0)) != 0)
    {
      *ptrue = simplify_gen_unary (code, mode, true0, GET_MODE (XEXP (x, 0)));
      *pfalse = simplify_gen_unary (code, mode, false0,
				    GET_MODE (XEXP (x, 0)));
      return cond0;
    }

  /* If this is a COMPARE, do nothing, since the IF_THEN_ELSE we would
     make can't possibly match and would suppress other optimizations.  */
  else if (code == COMPARE)
    ;

  /* If this is a binary operation, see if either side has only one of two
     values.  If either one does or if both do and they are conditional on
     the same value, compute the new true and false values.  */
  else if (BINARY_P (x))
    {
      cond0 = if_then_else_cond (XEXP (x, 0), &true0, &false0);
      cond1 = if_then_else_cond (XEXP (x, 1), &true1, &false1);

      if ((cond0 != 0 || cond1 != 0)
	  && ! (cond0 != 0 && cond1 != 0 && ! rtx_equal_p (cond0, cond1)))
	{
	  /* If if_then_else_cond returned zero, then true/false are the
	     same rtl.  We must copy one of them to prevent invalid rtl
	     sharing.  */
	  if (cond0 == 0)
	    true0 = copy_rtx (true0);
	  else if (cond1 == 0)
	    true1 = copy_rtx (true1);

	  if (COMPARISON_P (x))
	    {
	      *ptrue = simplify_gen_relational (code, mode, VOIDmode,
						true0, true1);
	      *pfalse = simplify_gen_relational (code, mode, VOIDmode,
						 false0, false1);
	     }
	  else
	    {
	      *ptrue = simplify_gen_binary (code, mode, true0, true1);
	      *pfalse = simplify_gen_binary (code, mode, false0, false1);
	    }

	  return cond0 ? cond0 : cond1;
	}

      /* See if we have PLUS, IOR, XOR, MINUS or UMAX, where one of the
	 operands is zero when the other is nonzero, and vice-versa,
	 and STORE_FLAG_VALUE is 1 or -1.  */

      if ((STORE_FLAG_VALUE == 1 || STORE_FLAG_VALUE == -1)
	  && (code == PLUS || code == IOR || code == XOR || code == MINUS
	      || code == UMAX)
	  && GET_CODE (XEXP (x, 0)) == MULT && GET_CODE (XEXP (x, 1)) == MULT)
	{
	  rtx op0 = XEXP (XEXP (x, 0), 1);
	  rtx op1 = XEXP (XEXP (x, 1), 1);

	  cond0 = XEXP (XEXP (x, 0), 0);
	  cond1 = XEXP (XEXP (x, 1), 0);

	  if (COMPARISON_P (cond0)
	      && COMPARISON_P (cond1)
	      && ((GET_CODE (cond0) == reversed_comparison_code (cond1, NULL)
		   && rtx_equal_p (XEXP (cond0, 0), XEXP (cond1, 0))
		   && rtx_equal_p (XEXP (cond0, 1), XEXP (cond1, 1)))
		  || ((swap_condition (GET_CODE (cond0))
		       == reversed_comparison_code (cond1, NULL))
		      && rtx_equal_p (XEXP (cond0, 0), XEXP (cond1, 1))
		      && rtx_equal_p (XEXP (cond0, 1), XEXP (cond1, 0))))
	      && ! side_effects_p (x))
	    {
	      *ptrue = simplify_gen_binary (MULT, mode, op0, const_true_rtx);
	      *pfalse = simplify_gen_binary (MULT, mode,
					     (code == MINUS
					      ? simplify_gen_unary (NEG, mode,
								    op1, mode)
					      : op1),
					      const_true_rtx);
	      return cond0;
	    }
	}

      /* Similarly for MULT, AND and UMIN, except that for these the result
	 is always zero.  */
      if ((STORE_FLAG_VALUE == 1 || STORE_FLAG_VALUE == -1)
	  && (code == MULT || code == AND || code == UMIN)
	  && GET_CODE (XEXP (x, 0)) == MULT && GET_CODE (XEXP (x, 1)) == MULT)
	{
	  cond0 = XEXP (XEXP (x, 0), 0);
	  cond1 = XEXP (XEXP (x, 1), 0);

	  if (COMPARISON_P (cond0)
	      && COMPARISON_P (cond1)
	      && ((GET_CODE (cond0) == reversed_comparison_code (cond1, NULL)
		   && rtx_equal_p (XEXP (cond0, 0), XEXP (cond1, 0))
		   && rtx_equal_p (XEXP (cond0, 1), XEXP (cond1, 1)))
		  || ((swap_condition (GET_CODE (cond0))
		       == reversed_comparison_code (cond1, NULL))
		      && rtx_equal_p (XEXP (cond0, 0), XEXP (cond1, 1))
		      && rtx_equal_p (XEXP (cond0, 1), XEXP (cond1, 0))))
	      && ! side_effects_p (x))
	    {
	      *ptrue = *pfalse = const0_rtx;
	      return cond0;
	    }
	}
    }

  else if (code == IF_THEN_ELSE)
    {
      /* If we have IF_THEN_ELSE already, extract the condition and
	 canonicalize it if it is NE or EQ.  */
      cond0 = XEXP (x, 0);
      *ptrue = XEXP (x, 1), *pfalse = XEXP (x, 2);
      if (GET_CODE (cond0) == NE && XEXP (cond0, 1) == const0_rtx)
	return XEXP (cond0, 0);
      else if (GET_CODE (cond0) == EQ && XEXP (cond0, 1) == const0_rtx)
	{
	  *ptrue = XEXP (x, 2), *pfalse = XEXP (x, 1);
	  return XEXP (cond0, 0);
	}
      else
	return cond0;
    }

  /* If X is a SUBREG, we can narrow both the true and false values
     if the inner expression, if there is a condition.  */
  else if (code == SUBREG
	   && 0 != (cond0 = if_then_else_cond (SUBREG_REG (x),
					       &true0, &false0)))
    {
      true0 = simplify_gen_subreg (mode, true0,
				   GET_MODE (SUBREG_REG (x)), SUBREG_BYTE (x));
      false0 = simplify_gen_subreg (mode, false0,
				    GET_MODE (SUBREG_REG (x)), SUBREG_BYTE (x));
      if (true0 && false0)
	{
	  *ptrue = true0;
	  *pfalse = false0;
	  return cond0;
	}
    }

  /* If X is a constant, this isn't special and will cause confusions
     if we treat it as such.  Likewise if it is equivalent to a constant.  */
  else if (CONSTANT_P (x)
	   || ((cond0 = get_last_value (x)) != 0 && CONSTANT_P (cond0)))
    ;

  /* If we're in BImode, canonicalize on 0 and STORE_FLAG_VALUE, as that
     will be least confusing to the rest of the compiler.  */
  else if (mode == BImode)
    {
      *ptrue = GEN_INT (STORE_FLAG_VALUE), *pfalse = const0_rtx;
      return x;
    }

  /* If X is known to be either 0 or -1, those are the true and
     false values when testing X.  */
  else if (x == constm1_rtx || x == const0_rtx
	   || (mode != VOIDmode
	       && num_sign_bit_copies (x, mode) == GET_MODE_BITSIZE (mode)))
    {
      *ptrue = constm1_rtx, *pfalse = const0_rtx;
      return x;
    }

  /* Likewise for 0 or a single bit.  */
  else if (SCALAR_INT_MODE_P (mode)
	   && GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	   && exact_log2 (nz = nonzero_bits (x, mode)) >= 0)
    {
      *ptrue = gen_int_mode (nz, mode), *pfalse = const0_rtx;
      return x;
    }

  /* Otherwise fail; show no condition with true and false values the same.  */
  *ptrue = *pfalse = x;
  return 0;
}

/* Return the value of expression X given the fact that condition COND
   is known to be true when applied to REG as its first operand and VAL
   as its second.  X is known to not be shared and so can be modified in
   place.

   We only handle the simplest cases, and specifically those cases that
   arise with IF_THEN_ELSE expressions.  */

static rtx
known_cond (rtx x, enum rtx_code cond, rtx reg, rtx val)
{
  enum rtx_code code = GET_CODE (x);
  rtx temp;
  const char *fmt;
  int i, j;

  if (side_effects_p (x))
    return x;

  /* If either operand of the condition is a floating point value,
     then we have to avoid collapsing an EQ comparison.  */
  if (cond == EQ
      && rtx_equal_p (x, reg)
      && ! FLOAT_MODE_P (GET_MODE (x))
      && ! FLOAT_MODE_P (GET_MODE (val)))
    return val;

  if (cond == UNEQ && rtx_equal_p (x, reg))
    return val;

  /* If X is (abs REG) and we know something about REG's relationship
     with zero, we may be able to simplify this.  */

  if (code == ABS && rtx_equal_p (XEXP (x, 0), reg) && val == const0_rtx)
    switch (cond)
      {
      case GE:  case GT:  case EQ:
	return XEXP (x, 0);
      case LT:  case LE:
	return simplify_gen_unary (NEG, GET_MODE (XEXP (x, 0)),
				   XEXP (x, 0),
				   GET_MODE (XEXP (x, 0)));
      default:
	break;
      }

  /* The only other cases we handle are MIN, MAX, and comparisons if the
     operands are the same as REG and VAL.  */

  else if (COMPARISON_P (x) || COMMUTATIVE_ARITH_P (x))
    {
      if (rtx_equal_p (XEXP (x, 0), val))
	cond = swap_condition (cond), temp = val, val = reg, reg = temp;

      if (rtx_equal_p (XEXP (x, 0), reg) && rtx_equal_p (XEXP (x, 1), val))
	{
	  if (COMPARISON_P (x))
	    {
	      if (comparison_dominates_p (cond, code))
		return const_true_rtx;

	      code = reversed_comparison_code (x, NULL);
	      if (code != UNKNOWN
		  && comparison_dominates_p (cond, code))
		return const0_rtx;
	      else
		return x;
	    }
	  else if (code == SMAX || code == SMIN
		   || code == UMIN || code == UMAX)
	    {
	      int unsignedp = (code == UMIN || code == UMAX);

	      /* Do not reverse the condition when it is NE or EQ.
		 This is because we cannot conclude anything about
		 the value of 'SMAX (x, y)' when x is not equal to y,
		 but we can when x equals y.  */
	      if ((code == SMAX || code == UMAX)
		  && ! (cond == EQ || cond == NE))
		cond = reverse_condition (cond);

	      switch (cond)
		{
		case GE:   case GT:
		  return unsignedp ? x : XEXP (x, 1);
		case LE:   case LT:
		  return unsignedp ? x : XEXP (x, 0);
		case GEU:  case GTU:
		  return unsignedp ? XEXP (x, 1) : x;
		case LEU:  case LTU:
		  return unsignedp ? XEXP (x, 0) : x;
		default:
		  break;
		}
	    }
	}
    }
  else if (code == SUBREG)
    {
      enum machine_mode inner_mode = GET_MODE (SUBREG_REG (x));
      rtx new, r = known_cond (SUBREG_REG (x), cond, reg, val);

      if (SUBREG_REG (x) != r)
	{
	  /* We must simplify subreg here, before we lose track of the
	     original inner_mode.  */
	  new = simplify_subreg (GET_MODE (x), r,
				 inner_mode, SUBREG_BYTE (x));
	  if (new)
	    return new;
	  else
	    SUBST (SUBREG_REG (x), r);
	}

      return x;
    }
  /* We don't have to handle SIGN_EXTEND here, because even in the
     case of replacing something with a modeless CONST_INT, a
     CONST_INT is already (supposed to be) a valid sign extension for
     its narrower mode, which implies it's already properly
     sign-extended for the wider mode.  Now, for ZERO_EXTEND, the
     story is different.  */
  else if (code == ZERO_EXTEND)
    {
      enum machine_mode inner_mode = GET_MODE (XEXP (x, 0));
      rtx new, r = known_cond (XEXP (x, 0), cond, reg, val);

      if (XEXP (x, 0) != r)
	{
	  /* We must simplify the zero_extend here, before we lose
	     track of the original inner_mode.  */
	  new = simplify_unary_operation (ZERO_EXTEND, GET_MODE (x),
					  r, inner_mode);
	  if (new)
	    return new;
	  else
	    SUBST (XEXP (x, 0), r);
	}

      return x;
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	SUBST (XEXP (x, i), known_cond (XEXP (x, i), cond, reg, val));
      else if (fmt[i] == 'E')
	for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	  SUBST (XVECEXP (x, i, j), known_cond (XVECEXP (x, i, j),
						cond, reg, val));
    }

  return x;
}

/* See if X and Y are equal for the purposes of seeing if we can rewrite an
   assignment as a field assignment.  */

static int
rtx_equal_for_field_assignment_p (rtx x, rtx y)
{
  if (x == y || rtx_equal_p (x, y))
    return 1;

  if (x == 0 || y == 0 || GET_MODE (x) != GET_MODE (y))
    return 0;

  /* Check for a paradoxical SUBREG of a MEM compared with the MEM.
     Note that all SUBREGs of MEM are paradoxical; otherwise they
     would have been rewritten.  */
  if (MEM_P (x) && GET_CODE (y) == SUBREG
      && MEM_P (SUBREG_REG (y))
      && rtx_equal_p (SUBREG_REG (y),
		      gen_lowpart (GET_MODE (SUBREG_REG (y)), x)))
    return 1;

  if (MEM_P (y) && GET_CODE (x) == SUBREG
      && MEM_P (SUBREG_REG (x))
      && rtx_equal_p (SUBREG_REG (x),
		      gen_lowpart (GET_MODE (SUBREG_REG (x)), y)))
    return 1;

  /* We used to see if get_last_value of X and Y were the same but that's
     not correct.  In one direction, we'll cause the assignment to have
     the wrong destination and in the case, we'll import a register into this
     insn that might have already have been dead.   So fail if none of the
     above cases are true.  */
  return 0;
}

/* See if X, a SET operation, can be rewritten as a bit-field assignment.
   Return that assignment if so.

   We only handle the most common cases.  */

static rtx
make_field_assignment (rtx x)
{
  rtx dest = SET_DEST (x);
  rtx src = SET_SRC (x);
  rtx assign;
  rtx rhs, lhs;
  HOST_WIDE_INT c1;
  HOST_WIDE_INT pos;
  unsigned HOST_WIDE_INT len;
  rtx other;
  enum machine_mode mode;

  /* If SRC was (and (not (ashift (const_int 1) POS)) DEST), this is
     a clear of a one-bit field.  We will have changed it to
     (and (rotate (const_int -2) POS) DEST), so check for that.  Also check
     for a SUBREG.  */

  if (GET_CODE (src) == AND && GET_CODE (XEXP (src, 0)) == ROTATE
      && GET_CODE (XEXP (XEXP (src, 0), 0)) == CONST_INT
      && INTVAL (XEXP (XEXP (src, 0), 0)) == -2
      && rtx_equal_for_field_assignment_p (dest, XEXP (src, 1)))
    {
      assign = make_extraction (VOIDmode, dest, 0, XEXP (XEXP (src, 0), 1),
				1, 1, 1, 0);
      if (assign != 0)
	return gen_rtx_SET (VOIDmode, assign, const0_rtx);
      return x;
    }

  if (GET_CODE (src) == AND && GET_CODE (XEXP (src, 0)) == SUBREG
      && subreg_lowpart_p (XEXP (src, 0))
      && (GET_MODE_SIZE (GET_MODE (XEXP (src, 0)))
	  < GET_MODE_SIZE (GET_MODE (SUBREG_REG (XEXP (src, 0)))))
      && GET_CODE (SUBREG_REG (XEXP (src, 0))) == ROTATE
      && GET_CODE (XEXP (SUBREG_REG (XEXP (src, 0)), 0)) == CONST_INT
      && INTVAL (XEXP (SUBREG_REG (XEXP (src, 0)), 0)) == -2
      && rtx_equal_for_field_assignment_p (dest, XEXP (src, 1)))
    {
      assign = make_extraction (VOIDmode, dest, 0,
				XEXP (SUBREG_REG (XEXP (src, 0)), 1),
				1, 1, 1, 0);
      if (assign != 0)
	return gen_rtx_SET (VOIDmode, assign, const0_rtx);
      return x;
    }

  /* If SRC is (ior (ashift (const_int 1) POS) DEST), this is a set of a
     one-bit field.  */
  if (GET_CODE (src) == IOR && GET_CODE (XEXP (src, 0)) == ASHIFT
      && XEXP (XEXP (src, 0), 0) == const1_rtx
      && rtx_equal_for_field_assignment_p (dest, XEXP (src, 1)))
    {
      assign = make_extraction (VOIDmode, dest, 0, XEXP (XEXP (src, 0), 1),
				1, 1, 1, 0);
      if (assign != 0)
	return gen_rtx_SET (VOIDmode, assign, const1_rtx);
      return x;
    }

  /* If DEST is already a field assignment, i.e. ZERO_EXTRACT, and the
     SRC is an AND with all bits of that field set, then we can discard
     the AND.  */
  if (GET_CODE (dest) == ZERO_EXTRACT
      && GET_CODE (XEXP (dest, 1)) == CONST_INT
      && GET_CODE (src) == AND
      && GET_CODE (XEXP (src, 1)) == CONST_INT)
    {
      HOST_WIDE_INT width = INTVAL (XEXP (dest, 1));
      unsigned HOST_WIDE_INT and_mask = INTVAL (XEXP (src, 1));
      unsigned HOST_WIDE_INT ze_mask;

      if (width >= HOST_BITS_PER_WIDE_INT)
	ze_mask = -1;
      else
	ze_mask = ((unsigned HOST_WIDE_INT)1 << width) - 1;

      /* Complete overlap.  We can remove the source AND.  */
      if ((and_mask & ze_mask) == ze_mask)
	return gen_rtx_SET (VOIDmode, dest, XEXP (src, 0));

      /* Partial overlap.  We can reduce the source AND.  */
      if ((and_mask & ze_mask) != and_mask)
	{
	  mode = GET_MODE (src);
	  src = gen_rtx_AND (mode, XEXP (src, 0),
			     gen_int_mode (and_mask & ze_mask, mode));
	  return gen_rtx_SET (VOIDmode, dest, src);
	}
    }

  /* The other case we handle is assignments into a constant-position
     field.  They look like (ior/xor (and DEST C1) OTHER).  If C1 represents
     a mask that has all one bits except for a group of zero bits and
     OTHER is known to have zeros where C1 has ones, this is such an
     assignment.  Compute the position and length from C1.  Shift OTHER
     to the appropriate position, force it to the required mode, and
     make the extraction.  Check for the AND in both operands.  */

  if (GET_CODE (src) != IOR && GET_CODE (src) != XOR)
    return x;

  rhs = expand_compound_operation (XEXP (src, 0));
  lhs = expand_compound_operation (XEXP (src, 1));

  if (GET_CODE (rhs) == AND
      && GET_CODE (XEXP (rhs, 1)) == CONST_INT
      && rtx_equal_for_field_assignment_p (XEXP (rhs, 0), dest))
    c1 = INTVAL (XEXP (rhs, 1)), other = lhs;
  else if (GET_CODE (lhs) == AND
	   && GET_CODE (XEXP (lhs, 1)) == CONST_INT
	   && rtx_equal_for_field_assignment_p (XEXP (lhs, 0), dest))
    c1 = INTVAL (XEXP (lhs, 1)), other = rhs;
  else
    return x;

  pos = get_pos_from_mask ((~c1) & GET_MODE_MASK (GET_MODE (dest)), &len);
  if (pos < 0 || pos + len > GET_MODE_BITSIZE (GET_MODE (dest))
      || GET_MODE_BITSIZE (GET_MODE (dest)) > HOST_BITS_PER_WIDE_INT
      || (c1 & nonzero_bits (other, GET_MODE (dest))) != 0)
    return x;

  assign = make_extraction (VOIDmode, dest, pos, NULL_RTX, len, 1, 1, 0);
  if (assign == 0)
    return x;

  /* The mode to use for the source is the mode of the assignment, or of
     what is inside a possible STRICT_LOW_PART.  */
  mode = (GET_CODE (assign) == STRICT_LOW_PART
	  ? GET_MODE (XEXP (assign, 0)) : GET_MODE (assign));

  /* Shift OTHER right POS places and make it the source, restricting it
     to the proper length and mode.  */

  src = canon_reg_for_combine (simplify_shift_const (NULL_RTX, LSHIFTRT,
						     GET_MODE (src),
						     other, pos),
			       dest);
  src = force_to_mode (src, mode,
		       GET_MODE_BITSIZE (mode) >= HOST_BITS_PER_WIDE_INT
		       ? ~(unsigned HOST_WIDE_INT) 0
		       : ((unsigned HOST_WIDE_INT) 1 << len) - 1,
		       0);

  /* If SRC is masked by an AND that does not make a difference in
     the value being stored, strip it.  */
  if (GET_CODE (assign) == ZERO_EXTRACT
      && GET_CODE (XEXP (assign, 1)) == CONST_INT
      && INTVAL (XEXP (assign, 1)) < HOST_BITS_PER_WIDE_INT
      && GET_CODE (src) == AND
      && GET_CODE (XEXP (src, 1)) == CONST_INT
      && ((unsigned HOST_WIDE_INT) INTVAL (XEXP (src, 1))
	  == ((unsigned HOST_WIDE_INT) 1 << INTVAL (XEXP (assign, 1))) - 1))
    src = XEXP (src, 0);

  return gen_rtx_SET (VOIDmode, assign, src);
}

/* See if X is of the form (+ (* a c) (* b c)) and convert to (* (+ a b) c)
   if so.  */

static rtx
apply_distributive_law (rtx x)
{
  enum rtx_code code = GET_CODE (x);
  enum rtx_code inner_code;
  rtx lhs, rhs, other;
  rtx tem;

  /* Distributivity is not true for floating point as it can change the
     value.  So we don't do it unless -funsafe-math-optimizations.  */
  if (FLOAT_MODE_P (GET_MODE (x))
      && ! flag_unsafe_math_optimizations)
    return x;

  /* The outer operation can only be one of the following:  */
  if (code != IOR && code != AND && code != XOR
      && code != PLUS && code != MINUS)
    return x;

  lhs = XEXP (x, 0);
  rhs = XEXP (x, 1);

  /* If either operand is a primitive we can't do anything, so get out
     fast.  */
  if (OBJECT_P (lhs) || OBJECT_P (rhs))
    return x;

  lhs = expand_compound_operation (lhs);
  rhs = expand_compound_operation (rhs);
  inner_code = GET_CODE (lhs);
  if (inner_code != GET_CODE (rhs))
    return x;

  /* See if the inner and outer operations distribute.  */
  switch (inner_code)
    {
    case LSHIFTRT:
    case ASHIFTRT:
    case AND:
    case IOR:
      /* These all distribute except over PLUS.  */
      if (code == PLUS || code == MINUS)
	return x;
      break;

    case MULT:
      if (code != PLUS && code != MINUS)
	return x;
      break;

    case ASHIFT:
      /* This is also a multiply, so it distributes over everything.  */
      break;

    case SUBREG:
      /* Non-paradoxical SUBREGs distributes over all operations,
	 provided the inner modes and byte offsets are the same, this
	 is an extraction of a low-order part, we don't convert an fp
	 operation to int or vice versa, this is not a vector mode,
	 and we would not be converting a single-word operation into a
	 multi-word operation.  The latter test is not required, but
	 it prevents generating unneeded multi-word operations.  Some
	 of the previous tests are redundant given the latter test,
	 but are retained because they are required for correctness.

	 We produce the result slightly differently in this case.  */

      if (GET_MODE (SUBREG_REG (lhs)) != GET_MODE (SUBREG_REG (rhs))
	  || SUBREG_BYTE (lhs) != SUBREG_BYTE (rhs)
	  || ! subreg_lowpart_p (lhs)
	  || (GET_MODE_CLASS (GET_MODE (lhs))
	      != GET_MODE_CLASS (GET_MODE (SUBREG_REG (lhs))))
	  || (GET_MODE_SIZE (GET_MODE (lhs))
	      > GET_MODE_SIZE (GET_MODE (SUBREG_REG (lhs))))
	  || VECTOR_MODE_P (GET_MODE (lhs))
	  || GET_MODE_SIZE (GET_MODE (SUBREG_REG (lhs))) > UNITS_PER_WORD
	  /* Result might need to be truncated.  Don't change mode if
	     explicit truncation is needed.  */
	  || !TRULY_NOOP_TRUNCATION
	       (GET_MODE_BITSIZE (GET_MODE (x)),
		GET_MODE_BITSIZE (GET_MODE (SUBREG_REG (lhs)))))
	return x;

      tem = simplify_gen_binary (code, GET_MODE (SUBREG_REG (lhs)),
				 SUBREG_REG (lhs), SUBREG_REG (rhs));
      return gen_lowpart (GET_MODE (x), tem);

    default:
      return x;
    }

  /* Set LHS and RHS to the inner operands (A and B in the example
     above) and set OTHER to the common operand (C in the example).
     There is only one way to do this unless the inner operation is
     commutative.  */
  if (COMMUTATIVE_ARITH_P (lhs)
      && rtx_equal_p (XEXP (lhs, 0), XEXP (rhs, 0)))
    other = XEXP (lhs, 0), lhs = XEXP (lhs, 1), rhs = XEXP (rhs, 1);
  else if (COMMUTATIVE_ARITH_P (lhs)
	   && rtx_equal_p (XEXP (lhs, 0), XEXP (rhs, 1)))
    other = XEXP (lhs, 0), lhs = XEXP (lhs, 1), rhs = XEXP (rhs, 0);
  else if (COMMUTATIVE_ARITH_P (lhs)
	   && rtx_equal_p (XEXP (lhs, 1), XEXP (rhs, 0)))
    other = XEXP (lhs, 1), lhs = XEXP (lhs, 0), rhs = XEXP (rhs, 1);
  else if (rtx_equal_p (XEXP (lhs, 1), XEXP (rhs, 1)))
    other = XEXP (lhs, 1), lhs = XEXP (lhs, 0), rhs = XEXP (rhs, 0);
  else
    return x;

  /* Form the new inner operation, seeing if it simplifies first.  */
  tem = simplify_gen_binary (code, GET_MODE (x), lhs, rhs);

  /* There is one exception to the general way of distributing:
     (a | c) ^ (b | c) -> (a ^ b) & ~c  */
  if (code == XOR && inner_code == IOR)
    {
      inner_code = AND;
      other = simplify_gen_unary (NOT, GET_MODE (x), other, GET_MODE (x));
    }

  /* We may be able to continuing distributing the result, so call
     ourselves recursively on the inner operation before forming the
     outer operation, which we return.  */
  return simplify_gen_binary (inner_code, GET_MODE (x),
			      apply_distributive_law (tem), other);
}

/* See if X is of the form (* (+ A B) C), and if so convert to
   (+ (* A C) (* B C)) and try to simplify.

   Most of the time, this results in no change.  However, if some of
   the operands are the same or inverses of each other, simplifications
   will result.

   For example, (and (ior A B) (not B)) can occur as the result of
   expanding a bit field assignment.  When we apply the distributive
   law to this, we get (ior (and (A (not B))) (and (B (not B)))),
   which then simplifies to (and (A (not B))).

   Note that no checks happen on the validity of applying the inverse
   distributive law.  This is pointless since we can do it in the
   few places where this routine is called.

   N is the index of the term that is decomposed (the arithmetic operation,
   i.e. (+ A B) in the first example above).  !N is the index of the term that
   is distributed, i.e. of C in the first example above.  */
static rtx
distribute_and_simplify_rtx (rtx x, int n)
{
  enum machine_mode mode;
  enum rtx_code outer_code, inner_code;
  rtx decomposed, distributed, inner_op0, inner_op1, new_op0, new_op1, tmp;

  decomposed = XEXP (x, n);
  if (!ARITHMETIC_P (decomposed))
    return NULL_RTX;

  mode = GET_MODE (x);
  outer_code = GET_CODE (x);
  distributed = XEXP (x, !n);

  inner_code = GET_CODE (decomposed);
  inner_op0 = XEXP (decomposed, 0);
  inner_op1 = XEXP (decomposed, 1);

  /* Special case (and (xor B C) (not A)), which is equivalent to
     (xor (ior A B) (ior A C))  */
  if (outer_code == AND && inner_code == XOR && GET_CODE (distributed) == NOT)
    {
      distributed = XEXP (distributed, 0);
      outer_code = IOR;
    }

  if (n == 0)
    {
      /* Distribute the second term.  */
      new_op0 = simplify_gen_binary (outer_code, mode, inner_op0, distributed);
      new_op1 = simplify_gen_binary (outer_code, mode, inner_op1, distributed);
    }
  else
    {
      /* Distribute the first term.  */
      new_op0 = simplify_gen_binary (outer_code, mode, distributed, inner_op0);
      new_op1 = simplify_gen_binary (outer_code, mode, distributed, inner_op1);
    }

  tmp = apply_distributive_law (simplify_gen_binary (inner_code, mode,
						     new_op0, new_op1));
  if (GET_CODE (tmp) != outer_code
      && rtx_cost (tmp, SET) < rtx_cost (x, SET))
    return tmp;

  return NULL_RTX;
}

/* Simplify a logical `and' of VAROP with the constant CONSTOP, to be done
   in MODE.  Return an equivalent form, if different from (and VAROP
   (const_int CONSTOP)).  Otherwise, return NULL_RTX.  */

static rtx
simplify_and_const_int_1 (enum machine_mode mode, rtx varop,
			  unsigned HOST_WIDE_INT constop)
{
  unsigned HOST_WIDE_INT nonzero;
  unsigned HOST_WIDE_INT orig_constop;
  rtx orig_varop;
  int i;

  orig_varop = varop;
  orig_constop = constop;
  if (GET_CODE (varop) == CLOBBER)
    return NULL_RTX;

  /* Simplify VAROP knowing that we will be only looking at some of the
     bits in it.

     Note by passing in CONSTOP, we guarantee that the bits not set in
     CONSTOP are not significant and will never be examined.  We must
     ensure that is the case by explicitly masking out those bits
     before returning.  */
  varop = force_to_mode (varop, mode, constop, 0);

  /* If VAROP is a CLOBBER, we will fail so return it.  */
  if (GET_CODE (varop) == CLOBBER)
    return varop;

  /* If VAROP is a CONST_INT, then we need to apply the mask in CONSTOP
     to VAROP and return the new constant.  */
  if (GET_CODE (varop) == CONST_INT)
    return gen_int_mode (INTVAL (varop) & constop, mode);

  /* See what bits may be nonzero in VAROP.  Unlike the general case of
     a call to nonzero_bits, here we don't care about bits outside
     MODE.  */

  nonzero = nonzero_bits (varop, mode) & GET_MODE_MASK (mode);

  /* Turn off all bits in the constant that are known to already be zero.
     Thus, if the AND isn't needed at all, we will have CONSTOP == NONZERO_BITS
     which is tested below.  */

  constop &= nonzero;

  /* If we don't have any bits left, return zero.  */
  if (constop == 0)
    return const0_rtx;

  /* If VAROP is a NEG of something known to be zero or 1 and CONSTOP is
     a power of two, we can replace this with an ASHIFT.  */
  if (GET_CODE (varop) == NEG && nonzero_bits (XEXP (varop, 0), mode) == 1
      && (i = exact_log2 (constop)) >= 0)
    return simplify_shift_const (NULL_RTX, ASHIFT, mode, XEXP (varop, 0), i);

  /* If VAROP is an IOR or XOR, apply the AND to both branches of the IOR
     or XOR, then try to apply the distributive law.  This may eliminate
     operations if either branch can be simplified because of the AND.
     It may also make some cases more complex, but those cases probably
     won't match a pattern either with or without this.  */

  if (GET_CODE (varop) == IOR || GET_CODE (varop) == XOR)
    return
      gen_lowpart
	(mode,
	 apply_distributive_law
	 (simplify_gen_binary (GET_CODE (varop), GET_MODE (varop),
			       simplify_and_const_int (NULL_RTX,
						       GET_MODE (varop),
						       XEXP (varop, 0),
						       constop),
			       simplify_and_const_int (NULL_RTX,
						       GET_MODE (varop),
						       XEXP (varop, 1),
						       constop))));

  /* If VAROP is PLUS, and the constant is a mask of low bits, distribute
     the AND and see if one of the operands simplifies to zero.  If so, we
     may eliminate it.  */

  if (GET_CODE (varop) == PLUS
      && exact_log2 (constop + 1) >= 0)
    {
      rtx o0, o1;

      o0 = simplify_and_const_int (NULL_RTX, mode, XEXP (varop, 0), constop);
      o1 = simplify_and_const_int (NULL_RTX, mode, XEXP (varop, 1), constop);
      if (o0 == const0_rtx)
	return o1;
      if (o1 == const0_rtx)
	return o0;
    }

  /* Make a SUBREG if necessary.  If we can't make it, fail.  */
  varop = gen_lowpart (mode, varop);
  if (varop == NULL_RTX || GET_CODE (varop) == CLOBBER)
    return NULL_RTX;

  /* If we are only masking insignificant bits, return VAROP.  */
  if (constop == nonzero)
    return varop;

  if (varop == orig_varop && constop == orig_constop)
    return NULL_RTX;

  /* Otherwise, return an AND.  */
  return simplify_gen_binary (AND, mode, varop, gen_int_mode (constop, mode));
}


/* We have X, a logical `and' of VAROP with the constant CONSTOP, to be done
   in MODE.

   Return an equivalent form, if different from X.  Otherwise, return X.  If
   X is zero, we are to always construct the equivalent form.  */

static rtx
simplify_and_const_int (rtx x, enum machine_mode mode, rtx varop,
			unsigned HOST_WIDE_INT constop)
{
  rtx tem = simplify_and_const_int_1 (mode, varop, constop);
  if (tem)
    return tem;

  if (!x)
    x = simplify_gen_binary (AND, GET_MODE (varop), varop,
			     gen_int_mode (constop, mode));
  if (GET_MODE (x) != mode)
    x = gen_lowpart (mode, x);
  return x;
}

/* Given a REG, X, compute which bits in X can be nonzero.
   We don't care about bits outside of those defined in MODE.

   For most X this is simply GET_MODE_MASK (GET_MODE (MODE)), but if X is
   a shift, AND, or zero_extract, we can do better.  */

static rtx
reg_nonzero_bits_for_combine (rtx x, enum machine_mode mode,
			      rtx known_x ATTRIBUTE_UNUSED,
			      enum machine_mode known_mode ATTRIBUTE_UNUSED,
			      unsigned HOST_WIDE_INT known_ret ATTRIBUTE_UNUSED,
			      unsigned HOST_WIDE_INT *nonzero)
{
  rtx tem;

  /* If X is a register whose nonzero bits value is current, use it.
     Otherwise, if X is a register whose value we can find, use that
     value.  Otherwise, use the previously-computed global nonzero bits
     for this register.  */

  if (reg_stat[REGNO (x)].last_set_value != 0
      && (reg_stat[REGNO (x)].last_set_mode == mode
	  || (GET_MODE_CLASS (reg_stat[REGNO (x)].last_set_mode) == MODE_INT
	      && GET_MODE_CLASS (mode) == MODE_INT))
      && (reg_stat[REGNO (x)].last_set_label == label_tick
	  || (REGNO (x) >= FIRST_PSEUDO_REGISTER
	      && REG_N_SETS (REGNO (x)) == 1
	      && ! REGNO_REG_SET_P
		 (ENTRY_BLOCK_PTR->next_bb->il.rtl->global_live_at_start,
		  REGNO (x))))
      && INSN_CUID (reg_stat[REGNO (x)].last_set) < subst_low_cuid)
    {
      *nonzero &= reg_stat[REGNO (x)].last_set_nonzero_bits;
      return NULL;
    }

  tem = get_last_value (x);

  if (tem)
    {
#ifdef SHORT_IMMEDIATES_SIGN_EXTEND
      /* If X is narrower than MODE and TEM is a non-negative
	 constant that would appear negative in the mode of X,
	 sign-extend it for use in reg_nonzero_bits because some
	 machines (maybe most) will actually do the sign-extension
	 and this is the conservative approach.

	 ??? For 2.5, try to tighten up the MD files in this regard
	 instead of this kludge.  */

      if (GET_MODE_BITSIZE (GET_MODE (x)) < GET_MODE_BITSIZE (mode)
	  && GET_CODE (tem) == CONST_INT
	  && INTVAL (tem) > 0
	  && 0 != (INTVAL (tem)
		   & ((HOST_WIDE_INT) 1
		      << (GET_MODE_BITSIZE (GET_MODE (x)) - 1))))
	tem = GEN_INT (INTVAL (tem)
		       | ((HOST_WIDE_INT) (-1)
			  << GET_MODE_BITSIZE (GET_MODE (x))));
#endif
      return tem;
    }
  else if (nonzero_sign_valid && reg_stat[REGNO (x)].nonzero_bits)
    {
      unsigned HOST_WIDE_INT mask = reg_stat[REGNO (x)].nonzero_bits;

      if (GET_MODE_BITSIZE (GET_MODE (x)) < GET_MODE_BITSIZE (mode))
	/* We don't know anything about the upper bits.  */
	mask |= GET_MODE_MASK (mode) ^ GET_MODE_MASK (GET_MODE (x));
      *nonzero &= mask;
    }

  return NULL;
}

/* Return the number of bits at the high-order end of X that are known to
   be equal to the sign bit.  X will be used in mode MODE; if MODE is
   VOIDmode, X will be used in its own mode.  The returned value  will always
   be between 1 and the number of bits in MODE.  */

static rtx
reg_num_sign_bit_copies_for_combine (rtx x, enum machine_mode mode,
				     rtx known_x ATTRIBUTE_UNUSED,
				     enum machine_mode known_mode
				     ATTRIBUTE_UNUSED,
				     unsigned int known_ret ATTRIBUTE_UNUSED,
				     unsigned int *result)
{
  rtx tem;

  if (reg_stat[REGNO (x)].last_set_value != 0
      && reg_stat[REGNO (x)].last_set_mode == mode
      && (reg_stat[REGNO (x)].last_set_label == label_tick
	  || (REGNO (x) >= FIRST_PSEUDO_REGISTER
	      && REG_N_SETS (REGNO (x)) == 1
	      && ! REGNO_REG_SET_P
		 (ENTRY_BLOCK_PTR->next_bb->il.rtl->global_live_at_start,
		  REGNO (x))))
      && INSN_CUID (reg_stat[REGNO (x)].last_set) < subst_low_cuid)
    {
      *result = reg_stat[REGNO (x)].last_set_sign_bit_copies;
      return NULL;
    }

  tem = get_last_value (x);
  if (tem != 0)
    return tem;

  if (nonzero_sign_valid && reg_stat[REGNO (x)].sign_bit_copies != 0
      && GET_MODE_BITSIZE (GET_MODE (x)) == GET_MODE_BITSIZE (mode))
    *result = reg_stat[REGNO (x)].sign_bit_copies;

  return NULL;
}

/* Return the number of "extended" bits there are in X, when interpreted
   as a quantity in MODE whose signedness is indicated by UNSIGNEDP.  For
   unsigned quantities, this is the number of high-order zero bits.
   For signed quantities, this is the number of copies of the sign bit
   minus 1.  In both case, this function returns the number of "spare"
   bits.  For example, if two quantities for which this function returns
   at least 1 are added, the addition is known not to overflow.

   This function will always return 0 unless called during combine, which
   implies that it must be called from a define_split.  */

unsigned int
extended_count (rtx x, enum machine_mode mode, int unsignedp)
{
  if (nonzero_sign_valid == 0)
    return 0;

  return (unsignedp
	  ? (GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	     ? (unsigned int) (GET_MODE_BITSIZE (mode) - 1
			       - floor_log2 (nonzero_bits (x, mode)))
	     : 0)
	  : num_sign_bit_copies (x, mode) - 1);
}

/* This function is called from `simplify_shift_const' to merge two
   outer operations.  Specifically, we have already found that we need
   to perform operation *POP0 with constant *PCONST0 at the outermost
   position.  We would now like to also perform OP1 with constant CONST1
   (with *POP0 being done last).

   Return 1 if we can do the operation and update *POP0 and *PCONST0 with
   the resulting operation.  *PCOMP_P is set to 1 if we would need to
   complement the innermost operand, otherwise it is unchanged.

   MODE is the mode in which the operation will be done.  No bits outside
   the width of this mode matter.  It is assumed that the width of this mode
   is smaller than or equal to HOST_BITS_PER_WIDE_INT.

   If *POP0 or OP1 are UNKNOWN, it means no operation is required.  Only NEG, PLUS,
   IOR, XOR, and AND are supported.  We may set *POP0 to SET if the proper
   result is simply *PCONST0.

   If the resulting operation cannot be expressed as one operation, we
   return 0 and do not change *POP0, *PCONST0, and *PCOMP_P.  */

static int
merge_outer_ops (enum rtx_code *pop0, HOST_WIDE_INT *pconst0, enum rtx_code op1, HOST_WIDE_INT const1, enum machine_mode mode, int *pcomp_p)
{
  enum rtx_code op0 = *pop0;
  HOST_WIDE_INT const0 = *pconst0;

  const0 &= GET_MODE_MASK (mode);
  const1 &= GET_MODE_MASK (mode);

  /* If OP0 is an AND, clear unimportant bits in CONST1.  */
  if (op0 == AND)
    const1 &= const0;

  /* If OP0 or OP1 is UNKNOWN, this is easy.  Similarly if they are the same or
     if OP0 is SET.  */

  if (op1 == UNKNOWN || op0 == SET)
    return 1;

  else if (op0 == UNKNOWN)
    op0 = op1, const0 = const1;

  else if (op0 == op1)
    {
      switch (op0)
	{
	case AND:
	  const0 &= const1;
	  break;
	case IOR:
	  const0 |= const1;
	  break;
	case XOR:
	  const0 ^= const1;
	  break;
	case PLUS:
	  const0 += const1;
	  break;
	case NEG:
	  op0 = UNKNOWN;
	  break;
	default:
	  break;
	}
    }

  /* Otherwise, if either is a PLUS or NEG, we can't do anything.  */
  else if (op0 == PLUS || op1 == PLUS || op0 == NEG || op1 == NEG)
    return 0;

  /* If the two constants aren't the same, we can't do anything.  The
     remaining six cases can all be done.  */
  else if (const0 != const1)
    return 0;

  else
    switch (op0)
      {
      case IOR:
	if (op1 == AND)
	  /* (a & b) | b == b */
	  op0 = SET;
	else /* op1 == XOR */
	  /* (a ^ b) | b == a | b */
	  {;}
	break;

      case XOR:
	if (op1 == AND)
	  /* (a & b) ^ b == (~a) & b */
	  op0 = AND, *pcomp_p = 1;
	else /* op1 == IOR */
	  /* (a | b) ^ b == a & ~b */
	  op0 = AND, const0 = ~const0;
	break;

      case AND:
	if (op1 == IOR)
	  /* (a | b) & b == b */
	op0 = SET;
	else /* op1 == XOR */
	  /* (a ^ b) & b) == (~a) & b */
	  *pcomp_p = 1;
	break;
      default:
	break;
      }

  /* Check for NO-OP cases.  */
  const0 &= GET_MODE_MASK (mode);
  if (const0 == 0
      && (op0 == IOR || op0 == XOR || op0 == PLUS))
    op0 = UNKNOWN;
  else if (const0 == 0 && op0 == AND)
    op0 = SET;
  else if ((unsigned HOST_WIDE_INT) const0 == GET_MODE_MASK (mode)
	   && op0 == AND)
    op0 = UNKNOWN;

  /* ??? Slightly redundant with the above mask, but not entirely.
     Moving this above means we'd have to sign-extend the mode mask
     for the final test.  */
  const0 = trunc_int_for_mode (const0, mode);

  *pop0 = op0;
  *pconst0 = const0;

  return 1;
}

/* Simplify a shift of VAROP by COUNT bits.  CODE says what kind of shift.
   The result of the shift is RESULT_MODE.  Return NULL_RTX if we cannot
   simplify it.  Otherwise, return a simplified value.

   The shift is normally computed in the widest mode we find in VAROP, as
   long as it isn't a different number of words than RESULT_MODE.  Exceptions
   are ASHIFTRT and ROTATE, which are always done in their original mode.  */

static rtx
simplify_shift_const_1 (enum rtx_code code, enum machine_mode result_mode,
			rtx varop, int orig_count)
{
  enum rtx_code orig_code = code;
  rtx orig_varop = varop;
  int count;
  enum machine_mode mode = result_mode;
  enum machine_mode shift_mode, tmode;
  unsigned int mode_words
    = (GET_MODE_SIZE (mode) + (UNITS_PER_WORD - 1)) / UNITS_PER_WORD;
  /* We form (outer_op (code varop count) (outer_const)).  */
  enum rtx_code outer_op = UNKNOWN;
  HOST_WIDE_INT outer_const = 0;
  int complement_p = 0;
  rtx new, x;

  /* Make sure and truncate the "natural" shift on the way in.  We don't
     want to do this inside the loop as it makes it more difficult to
     combine shifts.  */
  if (SHIFT_COUNT_TRUNCATED)
    orig_count &= GET_MODE_BITSIZE (mode) - 1;

  /* If we were given an invalid count, don't do anything except exactly
     what was requested.  */

  if (orig_count < 0 || orig_count >= (int) GET_MODE_BITSIZE (mode))
    return NULL_RTX;

  count = orig_count;

  /* Unless one of the branches of the `if' in this loop does a `continue',
     we will `break' the loop after the `if'.  */

  while (count != 0)
    {
      /* If we have an operand of (clobber (const_int 0)), fail.  */
      if (GET_CODE (varop) == CLOBBER)
	return NULL_RTX;

      /* If we discovered we had to complement VAROP, leave.  Making a NOT
	 here would cause an infinite loop.  */
      if (complement_p)
	break;

      /* Convert ROTATERT to ROTATE.  */
      if (code == ROTATERT)
	{
	  unsigned int bitsize = GET_MODE_BITSIZE (result_mode);;
	  code = ROTATE;
	  if (VECTOR_MODE_P (result_mode))
	    count = bitsize / GET_MODE_NUNITS (result_mode) - count;
	  else
	    count = bitsize - count;
	}

      /* We need to determine what mode we will do the shift in.  If the
	 shift is a right shift or a ROTATE, we must always do it in the mode
	 it was originally done in.  Otherwise, we can do it in MODE, the
	 widest mode encountered.  */
      shift_mode
	= (code == ASHIFTRT || code == LSHIFTRT || code == ROTATE
	   ? result_mode : mode);

      /* Handle cases where the count is greater than the size of the mode
	 minus 1.  For ASHIFT, use the size minus one as the count (this can
	 occur when simplifying (lshiftrt (ashiftrt ..))).  For rotates,
	 take the count modulo the size.  For other shifts, the result is
	 zero.

	 Since these shifts are being produced by the compiler by combining
	 multiple operations, each of which are defined, we know what the
	 result is supposed to be.  */

      if (count > (GET_MODE_BITSIZE (shift_mode) - 1))
	{
	  if (code == ASHIFTRT)
	    count = GET_MODE_BITSIZE (shift_mode) - 1;
	  else if (code == ROTATE || code == ROTATERT)
	    count %= GET_MODE_BITSIZE (shift_mode);
	  else
	    {
	      /* We can't simply return zero because there may be an
		 outer op.  */
	      varop = const0_rtx;
	      count = 0;
	      break;
	    }
	}

      /* An arithmetic right shift of a quantity known to be -1 or 0
	 is a no-op.  */
      if (code == ASHIFTRT
	  && (num_sign_bit_copies (varop, shift_mode)
	      == GET_MODE_BITSIZE (shift_mode)))
	{
	  count = 0;
	  break;
	}

      /* If we are doing an arithmetic right shift and discarding all but
	 the sign bit copies, this is equivalent to doing a shift by the
	 bitsize minus one.  Convert it into that shift because it will often
	 allow other simplifications.  */

      if (code == ASHIFTRT
	  && (count + num_sign_bit_copies (varop, shift_mode)
	      >= GET_MODE_BITSIZE (shift_mode)))
	count = GET_MODE_BITSIZE (shift_mode) - 1;

      /* We simplify the tests below and elsewhere by converting
	 ASHIFTRT to LSHIFTRT if we know the sign bit is clear.
	 `make_compound_operation' will convert it to an ASHIFTRT for
	 those machines (such as VAX) that don't have an LSHIFTRT.  */
      if (GET_MODE_BITSIZE (shift_mode) <= HOST_BITS_PER_WIDE_INT
	  && code == ASHIFTRT
	  && ((nonzero_bits (varop, shift_mode)
	       & ((HOST_WIDE_INT) 1 << (GET_MODE_BITSIZE (shift_mode) - 1)))
	      == 0))
	code = LSHIFTRT;

      if (((code == LSHIFTRT
	    && GET_MODE_BITSIZE (shift_mode) <= HOST_BITS_PER_WIDE_INT
	    && !(nonzero_bits (varop, shift_mode) >> count))
	   || (code == ASHIFT
	       && GET_MODE_BITSIZE (shift_mode) <= HOST_BITS_PER_WIDE_INT
	       && !((nonzero_bits (varop, shift_mode) << count)
		    & GET_MODE_MASK (shift_mode))))
	  && !side_effects_p (varop))
	varop = const0_rtx;

      switch (GET_CODE (varop))
	{
	case SIGN_EXTEND:
	case ZERO_EXTEND:
	case SIGN_EXTRACT:
	case ZERO_EXTRACT:
	  new = expand_compound_operation (varop);
	  if (new != varop)
	    {
	      varop = new;
	      continue;
	    }
	  break;

	case MEM:
	  /* If we have (xshiftrt (mem ...) C) and C is MODE_WIDTH
	     minus the width of a smaller mode, we can do this with a
	     SIGN_EXTEND or ZERO_EXTEND from the narrower memory location.  */
	  if ((code == ASHIFTRT || code == LSHIFTRT)
	      && ! mode_dependent_address_p (XEXP (varop, 0))
	      && ! MEM_VOLATILE_P (varop)
	      && (tmode = mode_for_size (GET_MODE_BITSIZE (mode) - count,
					 MODE_INT, 1)) != BLKmode)
	    {
	      new = adjust_address_nv (varop, tmode,
				       BYTES_BIG_ENDIAN ? 0
				       : count / BITS_PER_UNIT);

	      varop = gen_rtx_fmt_e (code == ASHIFTRT ? SIGN_EXTEND
				     : ZERO_EXTEND, mode, new);
	      count = 0;
	      continue;
	    }
	  break;

	case SUBREG:
	  /* If VAROP is a SUBREG, strip it as long as the inner operand has
	     the same number of words as what we've seen so far.  Then store
	     the widest mode in MODE.  */
	  if (subreg_lowpart_p (varop)
	      && (GET_MODE_SIZE (GET_MODE (SUBREG_REG (varop)))
		  > GET_MODE_SIZE (GET_MODE (varop)))
	      && (unsigned int) ((GET_MODE_SIZE (GET_MODE (SUBREG_REG (varop)))
				  + (UNITS_PER_WORD - 1)) / UNITS_PER_WORD)
		 == mode_words)
	    {
	      varop = SUBREG_REG (varop);
	      if (GET_MODE_SIZE (GET_MODE (varop)) > GET_MODE_SIZE (mode))
		mode = GET_MODE (varop);
	      continue;
	    }
	  break;

	case MULT:
	  /* Some machines use MULT instead of ASHIFT because MULT
	     is cheaper.  But it is still better on those machines to
	     merge two shifts into one.  */
	  if (GET_CODE (XEXP (varop, 1)) == CONST_INT
	      && exact_log2 (INTVAL (XEXP (varop, 1))) >= 0)
	    {
	      varop
		= simplify_gen_binary (ASHIFT, GET_MODE (varop),
				       XEXP (varop, 0),
				       GEN_INT (exact_log2 (
						INTVAL (XEXP (varop, 1)))));
	      continue;
	    }
	  break;

	case UDIV:
	  /* Similar, for when divides are cheaper.  */
	  if (GET_CODE (XEXP (varop, 1)) == CONST_INT
	      && exact_log2 (INTVAL (XEXP (varop, 1))) >= 0)
	    {
	      varop
		= simplify_gen_binary (LSHIFTRT, GET_MODE (varop),
				       XEXP (varop, 0),
				       GEN_INT (exact_log2 (
						INTVAL (XEXP (varop, 1)))));
	      continue;
	    }
	  break;

	case ASHIFTRT:
	  /* If we are extracting just the sign bit of an arithmetic
	     right shift, that shift is not needed.  However, the sign
	     bit of a wider mode may be different from what would be
	     interpreted as the sign bit in a narrower mode, so, if
	     the result is narrower, don't discard the shift.  */
	  if (code == LSHIFTRT
	      && count == (GET_MODE_BITSIZE (result_mode) - 1)
	      && (GET_MODE_BITSIZE (result_mode)
		  >= GET_MODE_BITSIZE (GET_MODE (varop))))
	    {
	      varop = XEXP (varop, 0);
	      continue;
	    }

	  /* ... fall through ...  */

	case LSHIFTRT:
	case ASHIFT:
	case ROTATE:
	  /* Here we have two nested shifts.  The result is usually the
	     AND of a new shift with a mask.  We compute the result below.  */
	  if (GET_CODE (XEXP (varop, 1)) == CONST_INT
	      && INTVAL (XEXP (varop, 1)) >= 0
	      && INTVAL (XEXP (varop, 1)) < GET_MODE_BITSIZE (GET_MODE (varop))
	      && GET_MODE_BITSIZE (result_mode) <= HOST_BITS_PER_WIDE_INT
	      && GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	      && !VECTOR_MODE_P (result_mode))
	    {
	      enum rtx_code first_code = GET_CODE (varop);
	      unsigned int first_count = INTVAL (XEXP (varop, 1));
	      unsigned HOST_WIDE_INT mask;
	      rtx mask_rtx;

	      /* We have one common special case.  We can't do any merging if
		 the inner code is an ASHIFTRT of a smaller mode.  However, if
		 we have (ashift:M1 (subreg:M1 (ashiftrt:M2 FOO C1) 0) C2)
		 with C2 == GET_MODE_BITSIZE (M1) - GET_MODE_BITSIZE (M2),
		 we can convert it to
		 (ashiftrt:M1 (ashift:M1 (and:M1 (subreg:M1 FOO 0 C2) C3) C1).
		 This simplifies certain SIGN_EXTEND operations.  */
	      if (code == ASHIFT && first_code == ASHIFTRT
		  && count == (GET_MODE_BITSIZE (result_mode)
			       - GET_MODE_BITSIZE (GET_MODE (varop))))
		{
		  /* C3 has the low-order C1 bits zero.  */

		  mask = (GET_MODE_MASK (mode)
			  & ~(((HOST_WIDE_INT) 1 << first_count) - 1));

		  varop = simplify_and_const_int (NULL_RTX, result_mode,
						  XEXP (varop, 0), mask);
		  varop = simplify_shift_const (NULL_RTX, ASHIFT, result_mode,
						varop, count);
		  count = first_count;
		  code = ASHIFTRT;
		  continue;
		}

	      /* If this was (ashiftrt (ashift foo C1) C2) and FOO has more
		 than C1 high-order bits equal to the sign bit, we can convert
		 this to either an ASHIFT or an ASHIFTRT depending on the
		 two counts.

		 We cannot do this if VAROP's mode is not SHIFT_MODE.  */

	      if (code == ASHIFTRT && first_code == ASHIFT
		  && GET_MODE (varop) == shift_mode
		  && (num_sign_bit_copies (XEXP (varop, 0), shift_mode)
		      > first_count))
		{
		  varop = XEXP (varop, 0);
		  count -= first_count;
		  if (count < 0)
		    {
		      count = -count;
		      code = ASHIFT;
		    }

		  continue;
		}

	      /* There are some cases we can't do.  If CODE is ASHIFTRT,
		 we can only do this if FIRST_CODE is also ASHIFTRT.

		 We can't do the case when CODE is ROTATE and FIRST_CODE is
		 ASHIFTRT.

		 If the mode of this shift is not the mode of the outer shift,
		 we can't do this if either shift is a right shift or ROTATE.

		 Finally, we can't do any of these if the mode is too wide
		 unless the codes are the same.

		 Handle the case where the shift codes are the same
		 first.  */

	      if (code == first_code)
		{
		  if (GET_MODE (varop) != result_mode
		      && (code == ASHIFTRT || code == LSHIFTRT
			  || code == ROTATE))
		    break;

		  count += first_count;
		  varop = XEXP (varop, 0);
		  continue;
		}

	      if (code == ASHIFTRT
		  || (code == ROTATE && first_code == ASHIFTRT)
		  || GET_MODE_BITSIZE (mode) > HOST_BITS_PER_WIDE_INT
		  || (GET_MODE (varop) != result_mode
		      && (first_code == ASHIFTRT || first_code == LSHIFTRT
			  || first_code == ROTATE
			  || code == ROTATE)))
		break;

	      /* To compute the mask to apply after the shift, shift the
		 nonzero bits of the inner shift the same way the
		 outer shift will.  */

	      mask_rtx = GEN_INT (nonzero_bits (varop, GET_MODE (varop)));

	      mask_rtx
		= simplify_const_binary_operation (code, result_mode, mask_rtx,
						   GEN_INT (count));

	      /* Give up if we can't compute an outer operation to use.  */
	      if (mask_rtx == 0
		  || GET_CODE (mask_rtx) != CONST_INT
		  || ! merge_outer_ops (&outer_op, &outer_const, AND,
					INTVAL (mask_rtx),
					result_mode, &complement_p))
		break;

	      /* If the shifts are in the same direction, we add the
		 counts.  Otherwise, we subtract them.  */
	      if ((code == ASHIFTRT || code == LSHIFTRT)
		  == (first_code == ASHIFTRT || first_code == LSHIFTRT))
		count += first_count;
	      else
		count -= first_count;

	      /* If COUNT is positive, the new shift is usually CODE,
		 except for the two exceptions below, in which case it is
		 FIRST_CODE.  If the count is negative, FIRST_CODE should
		 always be used  */
	      if (count > 0
		  && ((first_code == ROTATE && code == ASHIFT)
		      || (first_code == ASHIFTRT && code == LSHIFTRT)))
		code = first_code;
	      else if (count < 0)
		code = first_code, count = -count;

	      varop = XEXP (varop, 0);
	      continue;
	    }

	  /* If we have (A << B << C) for any shift, we can convert this to
	     (A << C << B).  This wins if A is a constant.  Only try this if
	     B is not a constant.  */

	  else if (GET_CODE (varop) == code
		   && GET_CODE (XEXP (varop, 0)) == CONST_INT
		   && GET_CODE (XEXP (varop, 1)) != CONST_INT)
	    {
	      rtx new = simplify_const_binary_operation (code, mode,
							 XEXP (varop, 0),
							 GEN_INT (count));
	      varop = gen_rtx_fmt_ee (code, mode, new, XEXP (varop, 1));
	      count = 0;
	      continue;
	    }
	  break;

	case NOT:
	  /* Make this fit the case below.  */
	  varop = gen_rtx_XOR (mode, XEXP (varop, 0),
			       GEN_INT (GET_MODE_MASK (mode)));
	  continue;

	case IOR:
	case AND:
	case XOR:
	  /* If we have (xshiftrt (ior (plus X (const_int -1)) X) C)
	     with C the size of VAROP - 1 and the shift is logical if
	     STORE_FLAG_VALUE is 1 and arithmetic if STORE_FLAG_VALUE is -1,
	     we have an (le X 0) operation.   If we have an arithmetic shift
	     and STORE_FLAG_VALUE is 1 or we have a logical shift with
	     STORE_FLAG_VALUE of -1, we have a (neg (le X 0)) operation.  */

	  if (GET_CODE (varop) == IOR && GET_CODE (XEXP (varop, 0)) == PLUS
	      && XEXP (XEXP (varop, 0), 1) == constm1_rtx
	      && (STORE_FLAG_VALUE == 1 || STORE_FLAG_VALUE == -1)
	      && (code == LSHIFTRT || code == ASHIFTRT)
	      && count == (GET_MODE_BITSIZE (GET_MODE (varop)) - 1)
	      && rtx_equal_p (XEXP (XEXP (varop, 0), 0), XEXP (varop, 1)))
	    {
	      count = 0;
	      varop = gen_rtx_LE (GET_MODE (varop), XEXP (varop, 1),
				  const0_rtx);

	      if (STORE_FLAG_VALUE == 1 ? code == ASHIFTRT : code == LSHIFTRT)
		varop = gen_rtx_NEG (GET_MODE (varop), varop);

	      continue;
	    }

	  /* If we have (shift (logical)), move the logical to the outside
	     to allow it to possibly combine with another logical and the
	     shift to combine with another shift.  This also canonicalizes to
	     what a ZERO_EXTRACT looks like.  Also, some machines have
	     (and (shift)) insns.  */

	  if (GET_CODE (XEXP (varop, 1)) == CONST_INT
	      /* We can't do this if we have (ashiftrt (xor))  and the
		 constant has its sign bit set in shift_mode.  */
	      && !(code == ASHIFTRT && GET_CODE (varop) == XOR
		   && 0 > trunc_int_for_mode (INTVAL (XEXP (varop, 1)),
					      shift_mode))
	      && (new = simplify_const_binary_operation (code, result_mode,
							 XEXP (varop, 1),
							 GEN_INT (count))) != 0
	      && GET_CODE (new) == CONST_INT
	      && merge_outer_ops (&outer_op, &outer_const, GET_CODE (varop),
				  INTVAL (new), result_mode, &complement_p))
	    {
	      varop = XEXP (varop, 0);
	      continue;
	    }

	  /* If we can't do that, try to simplify the shift in each arm of the
	     logical expression, make a new logical expression, and apply
	     the inverse distributive law.  This also can't be done
	     for some (ashiftrt (xor)).  */
	  if (GET_CODE (XEXP (varop, 1)) == CONST_INT
	     && !(code == ASHIFTRT && GET_CODE (varop) == XOR
		  && 0 > trunc_int_for_mode (INTVAL (XEXP (varop, 1)),
					     shift_mode)))
	    {
	      rtx lhs = simplify_shift_const (NULL_RTX, code, shift_mode,
					      XEXP (varop, 0), count);
	      rtx rhs = simplify_shift_const (NULL_RTX, code, shift_mode,
					      XEXP (varop, 1), count);

	      varop = simplify_gen_binary (GET_CODE (varop), shift_mode,
					   lhs, rhs);
	      varop = apply_distributive_law (varop);

	      count = 0;
	      continue;
	    }
	  break;

	case EQ:
	  /* Convert (lshiftrt (eq FOO 0) C) to (xor FOO 1) if STORE_FLAG_VALUE
	     says that the sign bit can be tested, FOO has mode MODE, C is
	     GET_MODE_BITSIZE (MODE) - 1, and FOO has only its low-order bit
	     that may be nonzero.  */
	  if (code == LSHIFTRT
	      && XEXP (varop, 1) == const0_rtx
	      && GET_MODE (XEXP (varop, 0)) == result_mode
	      && count == (GET_MODE_BITSIZE (result_mode) - 1)
	      && GET_MODE_BITSIZE (result_mode) <= HOST_BITS_PER_WIDE_INT
	      && STORE_FLAG_VALUE == -1
	      && nonzero_bits (XEXP (varop, 0), result_mode) == 1
	      && merge_outer_ops (&outer_op, &outer_const, XOR,
				  (HOST_WIDE_INT) 1, result_mode,
				  &complement_p))
	    {
	      varop = XEXP (varop, 0);
	      count = 0;
	      continue;
	    }
	  break;

	case NEG:
	  /* (lshiftrt (neg A) C) where A is either 0 or 1 and C is one less
	     than the number of bits in the mode is equivalent to A.  */
	  if (code == LSHIFTRT
	      && count == (GET_MODE_BITSIZE (result_mode) - 1)
	      && nonzero_bits (XEXP (varop, 0), result_mode) == 1)
	    {
	      varop = XEXP (varop, 0);
	      count = 0;
	      continue;
	    }

	  /* NEG commutes with ASHIFT since it is multiplication.  Move the
	     NEG outside to allow shifts to combine.  */
	  if (code == ASHIFT
	      && merge_outer_ops (&outer_op, &outer_const, NEG,
				  (HOST_WIDE_INT) 0, result_mode,
				  &complement_p))
	    {
	      varop = XEXP (varop, 0);
	      continue;
	    }
	  break;

	case PLUS:
	  /* (lshiftrt (plus A -1) C) where A is either 0 or 1 and C
	     is one less than the number of bits in the mode is
	     equivalent to (xor A 1).  */
	  if (code == LSHIFTRT
	      && count == (GET_MODE_BITSIZE (result_mode) - 1)
	      && XEXP (varop, 1) == constm1_rtx
	      && nonzero_bits (XEXP (varop, 0), result_mode) == 1
	      && merge_outer_ops (&outer_op, &outer_const, XOR,
				  (HOST_WIDE_INT) 1, result_mode,
				  &complement_p))
	    {
	      count = 0;
	      varop = XEXP (varop, 0);
	      continue;
	    }

	  /* If we have (xshiftrt (plus FOO BAR) C), and the only bits
	     that might be nonzero in BAR are those being shifted out and those
	     bits are known zero in FOO, we can replace the PLUS with FOO.
	     Similarly in the other operand order.  This code occurs when
	     we are computing the size of a variable-size array.  */

	  if ((code == ASHIFTRT || code == LSHIFTRT)
	      && count < HOST_BITS_PER_WIDE_INT
	      && nonzero_bits (XEXP (varop, 1), result_mode) >> count == 0
	      && (nonzero_bits (XEXP (varop, 1), result_mode)
		  & nonzero_bits (XEXP (varop, 0), result_mode)) == 0)
	    {
	      varop = XEXP (varop, 0);
	      continue;
	    }
	  else if ((code == ASHIFTRT || code == LSHIFTRT)
		   && count < HOST_BITS_PER_WIDE_INT
		   && GET_MODE_BITSIZE (result_mode) <= HOST_BITS_PER_WIDE_INT
		   && 0 == (nonzero_bits (XEXP (varop, 0), result_mode)
			    >> count)
		   && 0 == (nonzero_bits (XEXP (varop, 0), result_mode)
			    & nonzero_bits (XEXP (varop, 1),
						 result_mode)))
	    {
	      varop = XEXP (varop, 1);
	      continue;
	    }

	  /* (ashift (plus foo C) N) is (plus (ashift foo N) C').  */
	  if (code == ASHIFT
	      && GET_CODE (XEXP (varop, 1)) == CONST_INT
	      && (new = simplify_const_binary_operation (ASHIFT, result_mode,
							 XEXP (varop, 1),
							 GEN_INT (count))) != 0
	      && GET_CODE (new) == CONST_INT
	      && merge_outer_ops (&outer_op, &outer_const, PLUS,
				  INTVAL (new), result_mode, &complement_p))
	    {
	      varop = XEXP (varop, 0);
	      continue;
	    }

	  /* Check for 'PLUS signbit', which is the canonical form of 'XOR
	     signbit', and attempt to change the PLUS to an XOR and move it to
	     the outer operation as is done above in the AND/IOR/XOR case
	     leg for shift(logical). See details in logical handling above
	     for reasoning in doing so.  */
	  if (code == LSHIFTRT
	      && GET_CODE (XEXP (varop, 1)) == CONST_INT
	      && mode_signbit_p (result_mode, XEXP (varop, 1))
	      && (new = simplify_const_binary_operation (code, result_mode,
							 XEXP (varop, 1),
							 GEN_INT (count))) != 0
	      && GET_CODE (new) == CONST_INT
	      && merge_outer_ops (&outer_op, &outer_const, XOR,
				  INTVAL (new), result_mode, &complement_p))
	    {
	      varop = XEXP (varop, 0);
	      continue;
	    }

	  break;

	case MINUS:
	  /* If we have (xshiftrt (minus (ashiftrt X C)) X) C)
	     with C the size of VAROP - 1 and the shift is logical if
	     STORE_FLAG_VALUE is 1 and arithmetic if STORE_FLAG_VALUE is -1,
	     we have a (gt X 0) operation.  If the shift is arithmetic with
	     STORE_FLAG_VALUE of 1 or logical with STORE_FLAG_VALUE == -1,
	     we have a (neg (gt X 0)) operation.  */

	  if ((STORE_FLAG_VALUE == 1 || STORE_FLAG_VALUE == -1)
	      && GET_CODE (XEXP (varop, 0)) == ASHIFTRT
	      && count == (GET_MODE_BITSIZE (GET_MODE (varop)) - 1)
	      && (code == LSHIFTRT || code == ASHIFTRT)
	      && GET_CODE (XEXP (XEXP (varop, 0), 1)) == CONST_INT
	      && INTVAL (XEXP (XEXP (varop, 0), 1)) == count
	      && rtx_equal_p (XEXP (XEXP (varop, 0), 0), XEXP (varop, 1)))
	    {
	      count = 0;
	      varop = gen_rtx_GT (GET_MODE (varop), XEXP (varop, 1),
				  const0_rtx);

	      if (STORE_FLAG_VALUE == 1 ? code == ASHIFTRT : code == LSHIFTRT)
		varop = gen_rtx_NEG (GET_MODE (varop), varop);

	      continue;
	    }
	  break;

	case TRUNCATE:
	  /* Change (lshiftrt (truncate (lshiftrt))) to (truncate (lshiftrt))
	     if the truncate does not affect the value.  */
	  if (code == LSHIFTRT
	      && GET_CODE (XEXP (varop, 0)) == LSHIFTRT
	      && GET_CODE (XEXP (XEXP (varop, 0), 1)) == CONST_INT
	      && (INTVAL (XEXP (XEXP (varop, 0), 1))
		  >= (GET_MODE_BITSIZE (GET_MODE (XEXP (varop, 0)))
		      - GET_MODE_BITSIZE (GET_MODE (varop)))))
	    {
	      rtx varop_inner = XEXP (varop, 0);

	      varop_inner
		= gen_rtx_LSHIFTRT (GET_MODE (varop_inner),
				    XEXP (varop_inner, 0),
				    GEN_INT
				    (count + INTVAL (XEXP (varop_inner, 1))));
	      varop = gen_rtx_TRUNCATE (GET_MODE (varop), varop_inner);
	      count = 0;
	      continue;
	    }
	  break;

	default:
	  break;
	}

      break;
    }

  /* We need to determine what mode to do the shift in.  If the shift is
     a right shift or ROTATE, we must always do it in the mode it was
     originally done in.  Otherwise, we can do it in MODE, the widest mode
     encountered.  The code we care about is that of the shift that will
     actually be done, not the shift that was originally requested.  */
  shift_mode
    = (code == ASHIFTRT || code == LSHIFTRT || code == ROTATE
       ? result_mode : mode);

  /* We have now finished analyzing the shift.  The result should be
     a shift of type CODE with SHIFT_MODE shifting VAROP COUNT places.  If
     OUTER_OP is non-UNKNOWN, it is an operation that needs to be applied
     to the result of the shift.  OUTER_CONST is the relevant constant,
     but we must turn off all bits turned off in the shift.  */

  if (outer_op == UNKNOWN
      && orig_code == code && orig_count == count
      && varop == orig_varop
      && shift_mode == GET_MODE (varop))
    return NULL_RTX;

  /* Make a SUBREG if necessary.  If we can't make it, fail.  */
  varop = gen_lowpart (shift_mode, varop);
  if (varop == NULL_RTX || GET_CODE (varop) == CLOBBER)
    return NULL_RTX;

  /* If we have an outer operation and we just made a shift, it is
     possible that we could have simplified the shift were it not
     for the outer operation.  So try to do the simplification
     recursively.  */

  if (outer_op != UNKNOWN)
    x = simplify_shift_const_1 (code, shift_mode, varop, count);
  else
    x = NULL_RTX;

  if (x == NULL_RTX)
    x = simplify_gen_binary (code, shift_mode, varop, GEN_INT (count));

  /* If we were doing an LSHIFTRT in a wider mode than it was originally,
     turn off all the bits that the shift would have turned off.  */
  if (orig_code == LSHIFTRT && result_mode != shift_mode)
    x = simplify_and_const_int (NULL_RTX, shift_mode, x,
				GET_MODE_MASK (result_mode) >> orig_count);

  /* Do the remainder of the processing in RESULT_MODE.  */
  x = gen_lowpart_or_truncate (result_mode, x);

  /* If COMPLEMENT_P is set, we have to complement X before doing the outer
     operation.  */
  if (complement_p)
    x = simplify_gen_unary (NOT, result_mode, x, result_mode);

  if (outer_op != UNKNOWN)
    {
      if (GET_MODE_BITSIZE (result_mode) < HOST_BITS_PER_WIDE_INT)
	outer_const = trunc_int_for_mode (outer_const, result_mode);

      if (outer_op == AND)
	x = simplify_and_const_int (NULL_RTX, result_mode, x, outer_const);
      else if (outer_op == SET)
	{
	  /* This means that we have determined that the result is
	     equivalent to a constant.  This should be rare.  */
	  if (!side_effects_p (x))
	    x = GEN_INT (outer_const);
	}
      else if (GET_RTX_CLASS (outer_op) == RTX_UNARY)
	x = simplify_gen_unary (outer_op, result_mode, x, result_mode);
      else
	x = simplify_gen_binary (outer_op, result_mode, x,
				 GEN_INT (outer_const));
    }

  return x;
}

/* Simplify a shift of VAROP by COUNT bits.  CODE says what kind of shift.
   The result of the shift is RESULT_MODE.  If we cannot simplify it,
   return X or, if it is NULL, synthesize the expression with
   simplify_gen_binary.  Otherwise, return a simplified value.

   The shift is normally computed in the widest mode we find in VAROP, as
   long as it isn't a different number of words than RESULT_MODE.  Exceptions
   are ASHIFTRT and ROTATE, which are always done in their original mode.  */

static rtx
simplify_shift_const (rtx x, enum rtx_code code, enum machine_mode result_mode,
		      rtx varop, int count)
{
  rtx tem = simplify_shift_const_1 (code, result_mode, varop, count);
  if (tem)
    return tem;

  if (!x)
    x = simplify_gen_binary (code, GET_MODE (varop), varop, GEN_INT (count));
  if (GET_MODE (x) != result_mode)
    x = gen_lowpart (result_mode, x);
  return x;
}


/* Like recog, but we receive the address of a pointer to a new pattern.
   We try to match the rtx that the pointer points to.
   If that fails, we may try to modify or replace the pattern,
   storing the replacement into the same pointer object.

   Modifications include deletion or addition of CLOBBERs.

   PNOTES is a pointer to a location where any REG_UNUSED notes added for
   the CLOBBERs are placed.

   The value is the final insn code from the pattern ultimately matched,
   or -1.  */

static int
recog_for_combine (rtx *pnewpat, rtx insn, rtx *pnotes)
{
  rtx pat = *pnewpat;
  int insn_code_number;
  int num_clobbers_to_add = 0;
  int i;
  rtx notes = 0;
  rtx old_notes, old_pat;

  /* If PAT is a PARALLEL, check to see if it contains the CLOBBER
     we use to indicate that something didn't match.  If we find such a
     thing, force rejection.  */
  if (GET_CODE (pat) == PARALLEL)
    for (i = XVECLEN (pat, 0) - 1; i >= 0; i--)
      if (GET_CODE (XVECEXP (pat, 0, i)) == CLOBBER
	  && XEXP (XVECEXP (pat, 0, i), 0) == const0_rtx)
	return -1;

  old_pat = PATTERN (insn);
  old_notes = REG_NOTES (insn);
  PATTERN (insn) = pat;
  REG_NOTES (insn) = 0;

  insn_code_number = recog (pat, insn, &num_clobbers_to_add);

  /* If it isn't, there is the possibility that we previously had an insn
     that clobbered some register as a side effect, but the combined
     insn doesn't need to do that.  So try once more without the clobbers
     unless this represents an ASM insn.  */

  if (insn_code_number < 0 && ! check_asm_operands (pat)
      && GET_CODE (pat) == PARALLEL)
    {
      int pos;

      for (pos = 0, i = 0; i < XVECLEN (pat, 0); i++)
	if (GET_CODE (XVECEXP (pat, 0, i)) != CLOBBER)
	  {
	    if (i != pos)
	      SUBST (XVECEXP (pat, 0, pos), XVECEXP (pat, 0, i));
	    pos++;
	  }

      SUBST_INT (XVECLEN (pat, 0), pos);

      if (pos == 1)
	pat = XVECEXP (pat, 0, 0);

      PATTERN (insn) = pat;
      insn_code_number = recog (pat, insn, &num_clobbers_to_add);
    }
  PATTERN (insn) = old_pat;
  REG_NOTES (insn) = old_notes;

  /* Recognize all noop sets, these will be killed by followup pass.  */
  if (insn_code_number < 0 && GET_CODE (pat) == SET && set_noop_p (pat))
    insn_code_number = NOOP_MOVE_INSN_CODE, num_clobbers_to_add = 0;

  /* If we had any clobbers to add, make a new pattern than contains
     them.  Then check to make sure that all of them are dead.  */
  if (num_clobbers_to_add)
    {
      rtx newpat = gen_rtx_PARALLEL (VOIDmode,
				     rtvec_alloc (GET_CODE (pat) == PARALLEL
						  ? (XVECLEN (pat, 0)
						     + num_clobbers_to_add)
						  : num_clobbers_to_add + 1));

      if (GET_CODE (pat) == PARALLEL)
	for (i = 0; i < XVECLEN (pat, 0); i++)
	  XVECEXP (newpat, 0, i) = XVECEXP (pat, 0, i);
      else
	XVECEXP (newpat, 0, 0) = pat;

      add_clobbers (newpat, insn_code_number);

      for (i = XVECLEN (newpat, 0) - num_clobbers_to_add;
	   i < XVECLEN (newpat, 0); i++)
	{
	  if (REG_P (XEXP (XVECEXP (newpat, 0, i), 0))
	      && ! reg_dead_at_p (XEXP (XVECEXP (newpat, 0, i), 0), insn))
	    return -1;
	  notes = gen_rtx_EXPR_LIST (REG_UNUSED,
				     XEXP (XVECEXP (newpat, 0, i), 0), notes);
	}
      pat = newpat;
    }

  *pnewpat = pat;
  *pnotes = notes;

  return insn_code_number;
}

/* Like gen_lowpart_general but for use by combine.  In combine it
   is not possible to create any new pseudoregs.  However, it is
   safe to create invalid memory addresses, because combine will
   try to recognize them and all they will do is make the combine
   attempt fail.

   If for some reason this cannot do its job, an rtx
   (clobber (const_int 0)) is returned.
   An insn containing that will not be recognized.  */

static rtx
gen_lowpart_for_combine (enum machine_mode omode, rtx x)
{
  enum machine_mode imode = GET_MODE (x);
  unsigned int osize = GET_MODE_SIZE (omode);
  unsigned int isize = GET_MODE_SIZE (imode);
  rtx result;

  if (omode == imode)
    return x;

  /* Return identity if this is a CONST or symbolic reference.  */
  if (omode == Pmode
      && (GET_CODE (x) == CONST
	  || GET_CODE (x) == SYMBOL_REF
	  || GET_CODE (x) == LABEL_REF))
    return x;

  /* We can only support MODE being wider than a word if X is a
     constant integer or has a mode the same size.  */
  if (GET_MODE_SIZE (omode) > UNITS_PER_WORD
      && ! ((imode == VOIDmode
	     && (GET_CODE (x) == CONST_INT
		 || GET_CODE (x) == CONST_DOUBLE))
	    || isize == osize))
    goto fail;

  /* X might be a paradoxical (subreg (mem)).  In that case, gen_lowpart
     won't know what to do.  So we will strip off the SUBREG here and
     process normally.  */
  if (GET_CODE (x) == SUBREG && MEM_P (SUBREG_REG (x)))
    {
      x = SUBREG_REG (x);

      /* For use in case we fall down into the address adjustments
	 further below, we need to adjust the known mode and size of
	 x; imode and isize, since we just adjusted x.  */
      imode = GET_MODE (x);

      if (imode == omode)
	return x;

      isize = GET_MODE_SIZE (imode);
    }

  result = gen_lowpart_common (omode, x);

#ifdef CANNOT_CHANGE_MODE_CLASS
  if (result != 0 && GET_CODE (result) == SUBREG)
    record_subregs_of_mode (result);
#endif

  if (result)
    return result;

  if (MEM_P (x))
    {
      int offset = 0;

      /* Refuse to work on a volatile memory ref or one with a mode-dependent
	 address.  */
      if (MEM_VOLATILE_P (x) || mode_dependent_address_p (XEXP (x, 0)))
	goto fail;

      /* If we want to refer to something bigger than the original memref,
	 generate a paradoxical subreg instead.  That will force a reload
	 of the original memref X.  */
      if (isize < osize)
	return gen_rtx_SUBREG (omode, x, 0);

      if (WORDS_BIG_ENDIAN)
	offset = MAX (isize, UNITS_PER_WORD) - MAX (osize, UNITS_PER_WORD);

      /* Adjust the address so that the address-after-the-data is
	 unchanged.  */
      if (BYTES_BIG_ENDIAN)
	offset -= MIN (UNITS_PER_WORD, osize) - MIN (UNITS_PER_WORD, isize);

      return adjust_address_nv (x, omode, offset);
    }

  /* If X is a comparison operator, rewrite it in a new mode.  This
     probably won't match, but may allow further simplifications.  */
  else if (COMPARISON_P (x))
    return gen_rtx_fmt_ee (GET_CODE (x), omode, XEXP (x, 0), XEXP (x, 1));

  /* If we couldn't simplify X any other way, just enclose it in a
     SUBREG.  Normally, this SUBREG won't match, but some patterns may
     include an explicit SUBREG or we may simplify it further in combine.  */
  else
    {
      int offset = 0;
      rtx res;

      offset = subreg_lowpart_offset (omode, imode);
      if (imode == VOIDmode)
	{
	  imode = int_mode_for_mode (omode);
	  x = gen_lowpart_common (imode, x);
	  if (x == NULL)
	    goto fail;
	}
      res = simplify_gen_subreg (omode, x, imode, offset);
      if (res)
	return res;
    }

 fail:
  return gen_rtx_CLOBBER (imode, const0_rtx);
}

/* Simplify a comparison between *POP0 and *POP1 where CODE is the
   comparison code that will be tested.

   The result is a possibly different comparison code to use.  *POP0 and
   *POP1 may be updated.

   It is possible that we might detect that a comparison is either always
   true or always false.  However, we do not perform general constant
   folding in combine, so this knowledge isn't useful.  Such tautologies
   should have been detected earlier.  Hence we ignore all such cases.  */

static enum rtx_code
simplify_comparison (enum rtx_code code, rtx *pop0, rtx *pop1)
{
  rtx op0 = *pop0;
  rtx op1 = *pop1;
  rtx tem, tem1;
  int i;
  enum machine_mode mode, tmode;

  /* Try a few ways of applying the same transformation to both operands.  */
  while (1)
    {
#ifndef WORD_REGISTER_OPERATIONS
      /* The test below this one won't handle SIGN_EXTENDs on these machines,
	 so check specially.  */
      if (code != GTU && code != GEU && code != LTU && code != LEU
	  && GET_CODE (op0) == ASHIFTRT && GET_CODE (op1) == ASHIFTRT
	  && GET_CODE (XEXP (op0, 0)) == ASHIFT
	  && GET_CODE (XEXP (op1, 0)) == ASHIFT
	  && GET_CODE (XEXP (XEXP (op0, 0), 0)) == SUBREG
	  && GET_CODE (XEXP (XEXP (op1, 0), 0)) == SUBREG
	  && (GET_MODE (SUBREG_REG (XEXP (XEXP (op0, 0), 0)))
	      == GET_MODE (SUBREG_REG (XEXP (XEXP (op1, 0), 0))))
	  && GET_CODE (XEXP (op0, 1)) == CONST_INT
	  && XEXP (op0, 1) == XEXP (op1, 1)
	  && XEXP (op0, 1) == XEXP (XEXP (op0, 0), 1)
	  && XEXP (op0, 1) == XEXP (XEXP (op1, 0), 1)
	  && (INTVAL (XEXP (op0, 1))
	      == (GET_MODE_BITSIZE (GET_MODE (op0))
		  - (GET_MODE_BITSIZE
		     (GET_MODE (SUBREG_REG (XEXP (XEXP (op0, 0), 0))))))))
	{
	  op0 = SUBREG_REG (XEXP (XEXP (op0, 0), 0));
	  op1 = SUBREG_REG (XEXP (XEXP (op1, 0), 0));
	}
#endif

      /* If both operands are the same constant shift, see if we can ignore the
	 shift.  We can if the shift is a rotate or if the bits shifted out of
	 this shift are known to be zero for both inputs and if the type of
	 comparison is compatible with the shift.  */
      if (GET_CODE (op0) == GET_CODE (op1)
	  && GET_MODE_BITSIZE (GET_MODE (op0)) <= HOST_BITS_PER_WIDE_INT
	  && ((GET_CODE (op0) == ROTATE && (code == NE || code == EQ))
	      || ((GET_CODE (op0) == LSHIFTRT || GET_CODE (op0) == ASHIFT)
		  && (code != GT && code != LT && code != GE && code != LE))
	      || (GET_CODE (op0) == ASHIFTRT
		  && (code != GTU && code != LTU
		      && code != GEU && code != LEU)))
	  && GET_CODE (XEXP (op0, 1)) == CONST_INT
	  && INTVAL (XEXP (op0, 1)) >= 0
	  && INTVAL (XEXP (op0, 1)) < HOST_BITS_PER_WIDE_INT
	  && XEXP (op0, 1) == XEXP (op1, 1))
	{
	  enum machine_mode mode = GET_MODE (op0);
	  unsigned HOST_WIDE_INT mask = GET_MODE_MASK (mode);
	  int shift_count = INTVAL (XEXP (op0, 1));

	  if (GET_CODE (op0) == LSHIFTRT || GET_CODE (op0) == ASHIFTRT)
	    mask &= (mask >> shift_count) << shift_count;
	  else if (GET_CODE (op0) == ASHIFT)
	    mask = (mask & (mask << shift_count)) >> shift_count;

	  if ((nonzero_bits (XEXP (op0, 0), mode) & ~mask) == 0
	      && (nonzero_bits (XEXP (op1, 0), mode) & ~mask) == 0)
	    op0 = XEXP (op0, 0), op1 = XEXP (op1, 0);
	  else
	    break;
	}

      /* If both operands are AND's of a paradoxical SUBREG by constant, the
	 SUBREGs are of the same mode, and, in both cases, the AND would
	 be redundant if the comparison was done in the narrower mode,
	 do the comparison in the narrower mode (e.g., we are AND'ing with 1
	 and the operand's possibly nonzero bits are 0xffffff01; in that case
	 if we only care about QImode, we don't need the AND).  This case
	 occurs if the output mode of an scc insn is not SImode and
	 STORE_FLAG_VALUE == 1 (e.g., the 386).

	 Similarly, check for a case where the AND's are ZERO_EXTEND
	 operations from some narrower mode even though a SUBREG is not
	 present.  */

      else if (GET_CODE (op0) == AND && GET_CODE (op1) == AND
	       && GET_CODE (XEXP (op0, 1)) == CONST_INT
	       && GET_CODE (XEXP (op1, 1)) == CONST_INT)
	{
	  rtx inner_op0 = XEXP (op0, 0);
	  rtx inner_op1 = XEXP (op1, 0);
	  HOST_WIDE_INT c0 = INTVAL (XEXP (op0, 1));
	  HOST_WIDE_INT c1 = INTVAL (XEXP (op1, 1));
	  int changed = 0;

	  if (GET_CODE (inner_op0) == SUBREG && GET_CODE (inner_op1) == SUBREG
	      && (GET_MODE_SIZE (GET_MODE (inner_op0))
		  > GET_MODE_SIZE (GET_MODE (SUBREG_REG (inner_op0))))
	      && (GET_MODE (SUBREG_REG (inner_op0))
		  == GET_MODE (SUBREG_REG (inner_op1)))
	      && (GET_MODE_BITSIZE (GET_MODE (SUBREG_REG (inner_op0)))
		  <= HOST_BITS_PER_WIDE_INT)
	      && (0 == ((~c0) & nonzero_bits (SUBREG_REG (inner_op0),
					     GET_MODE (SUBREG_REG (inner_op0)))))
	      && (0 == ((~c1) & nonzero_bits (SUBREG_REG (inner_op1),
					     GET_MODE (SUBREG_REG (inner_op1))))))
	    {
	      op0 = SUBREG_REG (inner_op0);
	      op1 = SUBREG_REG (inner_op1);

	      /* The resulting comparison is always unsigned since we masked
		 off the original sign bit.  */
	      code = unsigned_condition (code);

	      changed = 1;
	    }

	  else if (c0 == c1)
	    for (tmode = GET_CLASS_NARROWEST_MODE
		 (GET_MODE_CLASS (GET_MODE (op0)));
		 tmode != GET_MODE (op0); tmode = GET_MODE_WIDER_MODE (tmode))
	      if ((unsigned HOST_WIDE_INT) c0 == GET_MODE_MASK (tmode))
		{
		  op0 = gen_lowpart (tmode, inner_op0);
		  op1 = gen_lowpart (tmode, inner_op1);
		  code = unsigned_condition (code);
		  changed = 1;
		  break;
		}

	  if (! changed)
	    break;
	}

      /* If both operands are NOT, we can strip off the outer operation
	 and adjust the comparison code for swapped operands; similarly for
	 NEG, except that this must be an equality comparison.  */
      else if ((GET_CODE (op0) == NOT && GET_CODE (op1) == NOT)
	       || (GET_CODE (op0) == NEG && GET_CODE (op1) == NEG
		   && (code == EQ || code == NE)))
	op0 = XEXP (op0, 0), op1 = XEXP (op1, 0), code = swap_condition (code);

      else
	break;
    }

  /* If the first operand is a constant, swap the operands and adjust the
     comparison code appropriately, but don't do this if the second operand
     is already a constant integer.  */
  if (swap_commutative_operands_p (op0, op1))
    {
      tem = op0, op0 = op1, op1 = tem;
      code = swap_condition (code);
    }

  /* We now enter a loop during which we will try to simplify the comparison.
     For the most part, we only are concerned with comparisons with zero,
     but some things may really be comparisons with zero but not start
     out looking that way.  */

  while (GET_CODE (op1) == CONST_INT)
    {
      enum machine_mode mode = GET_MODE (op0);
      unsigned int mode_width = GET_MODE_BITSIZE (mode);
      unsigned HOST_WIDE_INT mask = GET_MODE_MASK (mode);
      int equality_comparison_p;
      int sign_bit_comparison_p;
      int unsigned_comparison_p;
      HOST_WIDE_INT const_op;

      /* We only want to handle integral modes.  This catches VOIDmode,
	 CCmode, and the floating-point modes.  An exception is that we
	 can handle VOIDmode if OP0 is a COMPARE or a comparison
	 operation.  */

      if (GET_MODE_CLASS (mode) != MODE_INT
	  && ! (mode == VOIDmode
		&& (GET_CODE (op0) == COMPARE || COMPARISON_P (op0))))
	break;

      /* Get the constant we are comparing against and turn off all bits
	 not on in our mode.  */
      const_op = INTVAL (op1);
      if (mode != VOIDmode)
	const_op = trunc_int_for_mode (const_op, mode);
      op1 = GEN_INT (const_op);

      /* If we are comparing against a constant power of two and the value
	 being compared can only have that single bit nonzero (e.g., it was
	 `and'ed with that bit), we can replace this with a comparison
	 with zero.  */
      if (const_op
	  && (code == EQ || code == NE || code == GE || code == GEU
	      || code == LT || code == LTU)
	  && mode_width <= HOST_BITS_PER_WIDE_INT
	  && exact_log2 (const_op) >= 0
	  && nonzero_bits (op0, mode) == (unsigned HOST_WIDE_INT) const_op)
	{
	  code = (code == EQ || code == GE || code == GEU ? NE : EQ);
	  op1 = const0_rtx, const_op = 0;
	}

      /* Similarly, if we are comparing a value known to be either -1 or
	 0 with -1, change it to the opposite comparison against zero.  */

      if (const_op == -1
	  && (code == EQ || code == NE || code == GT || code == LE
	      || code == GEU || code == LTU)
	  && num_sign_bit_copies (op0, mode) == mode_width)
	{
	  code = (code == EQ || code == LE || code == GEU ? NE : EQ);
	  op1 = const0_rtx, const_op = 0;
	}

      /* Do some canonicalizations based on the comparison code.  We prefer
	 comparisons against zero and then prefer equality comparisons.
	 If we can reduce the size of a constant, we will do that too.  */

      switch (code)
	{
	case LT:
	  /* < C is equivalent to <= (C - 1) */
	  if (const_op > 0)
	    {
	      const_op -= 1;
	      op1 = GEN_INT (const_op);
	      code = LE;
	      /* ... fall through to LE case below.  */
	    }
	  else
	    break;

	case LE:
	  /* <= C is equivalent to < (C + 1); we do this for C < 0  */
	  if (const_op < 0)
	    {
	      const_op += 1;
	      op1 = GEN_INT (const_op);
	      code = LT;
	    }

	  /* If we are doing a <= 0 comparison on a value known to have
	     a zero sign bit, we can replace this with == 0.  */
	  else if (const_op == 0
		   && mode_width <= HOST_BITS_PER_WIDE_INT
		   && (nonzero_bits (op0, mode)
		       & ((HOST_WIDE_INT) 1 << (mode_width - 1))) == 0)
	    code = EQ;
	  break;

	case GE:
	  /* >= C is equivalent to > (C - 1).  */
	  if (const_op > 0)
	    {
	      const_op -= 1;
	      op1 = GEN_INT (const_op);
	      code = GT;
	      /* ... fall through to GT below.  */
	    }
	  else
	    break;

	case GT:
	  /* > C is equivalent to >= (C + 1); we do this for C < 0.  */
	  if (const_op < 0)
	    {
	      const_op += 1;
	      op1 = GEN_INT (const_op);
	      code = GE;
	    }

	  /* If we are doing a > 0 comparison on a value known to have
	     a zero sign bit, we can replace this with != 0.  */
	  else if (const_op == 0
		   && mode_width <= HOST_BITS_PER_WIDE_INT
		   && (nonzero_bits (op0, mode)
		       & ((HOST_WIDE_INT) 1 << (mode_width - 1))) == 0)
	    code = NE;
	  break;

	case LTU:
	  /* < C is equivalent to <= (C - 1).  */
	  if (const_op > 0)
	    {
	      const_op -= 1;
	      op1 = GEN_INT (const_op);
	      code = LEU;
	      /* ... fall through ...  */
	    }

	  /* (unsigned) < 0x80000000 is equivalent to >= 0.  */
	  else if ((mode_width <= HOST_BITS_PER_WIDE_INT)
		   && (const_op == (HOST_WIDE_INT) 1 << (mode_width - 1)))
	    {
	      const_op = 0, op1 = const0_rtx;
	      code = GE;
	      break;
	    }
	  else
	    break;

	case LEU:
	  /* unsigned <= 0 is equivalent to == 0 */
	  if (const_op == 0)
	    code = EQ;

	  /* (unsigned) <= 0x7fffffff is equivalent to >= 0.  */
	  else if ((mode_width <= HOST_BITS_PER_WIDE_INT)
		   && (const_op == ((HOST_WIDE_INT) 1 << (mode_width - 1)) - 1))
	    {
	      const_op = 0, op1 = const0_rtx;
	      code = GE;
	    }
	  break;

	case GEU:
	  /* >= C is equivalent to > (C - 1).  */
	  if (const_op > 1)
	    {
	      const_op -= 1;
	      op1 = GEN_INT (const_op);
	      code = GTU;
	      /* ... fall through ...  */
	    }

	  /* (unsigned) >= 0x80000000 is equivalent to < 0.  */
	  else if ((mode_width <= HOST_BITS_PER_WIDE_INT)
		   && (const_op == (HOST_WIDE_INT) 1 << (mode_width - 1)))
	    {
	      const_op = 0, op1 = const0_rtx;
	      code = LT;
	      break;
	    }
	  else
	    break;

	case GTU:
	  /* unsigned > 0 is equivalent to != 0 */
	  if (const_op == 0)
	    code = NE;

	  /* (unsigned) > 0x7fffffff is equivalent to < 0.  */
	  else if ((mode_width <= HOST_BITS_PER_WIDE_INT)
		   && (const_op == ((HOST_WIDE_INT) 1 << (mode_width - 1)) - 1))
	    {
	      const_op = 0, op1 = const0_rtx;
	      code = LT;
	    }
	  break;

	default:
	  break;
	}

      /* Compute some predicates to simplify code below.  */

      equality_comparison_p = (code == EQ || code == NE);
      sign_bit_comparison_p = ((code == LT || code == GE) && const_op == 0);
      unsigned_comparison_p = (code == LTU || code == LEU || code == GTU
			       || code == GEU);

      /* If this is a sign bit comparison and we can do arithmetic in
	 MODE, say that we will only be needing the sign bit of OP0.  */
      if (sign_bit_comparison_p
	  && GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT)
	op0 = force_to_mode (op0, mode,
			     ((HOST_WIDE_INT) 1
			      << (GET_MODE_BITSIZE (mode) - 1)),
			     0);

      /* Now try cases based on the opcode of OP0.  If none of the cases
	 does a "continue", we exit this loop immediately after the
	 switch.  */

      switch (GET_CODE (op0))
	{
	case ZERO_EXTRACT:
	  /* If we are extracting a single bit from a variable position in
	     a constant that has only a single bit set and are comparing it
	     with zero, we can convert this into an equality comparison
	     between the position and the location of the single bit.  */
	  /* Except we can't if SHIFT_COUNT_TRUNCATED is set, since we might
	     have already reduced the shift count modulo the word size.  */
	  if (!SHIFT_COUNT_TRUNCATED
	      && GET_CODE (XEXP (op0, 0)) == CONST_INT
	      && XEXP (op0, 1) == const1_rtx
	      && equality_comparison_p && const_op == 0
	      && (i = exact_log2 (INTVAL (XEXP (op0, 0)))) >= 0)
	    {
	      if (BITS_BIG_ENDIAN)
		{
		  enum machine_mode new_mode
		    = mode_for_extraction (EP_extzv, 1);
		  if (new_mode == MAX_MACHINE_MODE)
		    i = BITS_PER_WORD - 1 - i;
		  else
		    {
		      mode = new_mode;
		      i = (GET_MODE_BITSIZE (mode) - 1 - i);
		    }
		}

	      op0 = XEXP (op0, 2);
	      op1 = GEN_INT (i);
	      const_op = i;

	      /* Result is nonzero iff shift count is equal to I.  */
	      code = reverse_condition (code);
	      continue;
	    }

	  /* ... fall through ...  */

	case SIGN_EXTRACT:
	  tem = expand_compound_operation (op0);
	  if (tem != op0)
	    {
	      op0 = tem;
	      continue;
	    }
	  break;

	case NOT:
	  /* If testing for equality, we can take the NOT of the constant.  */
	  if (equality_comparison_p
	      && (tem = simplify_unary_operation (NOT, mode, op1, mode)) != 0)
	    {
	      op0 = XEXP (op0, 0);
	      op1 = tem;
	      continue;
	    }

	  /* If just looking at the sign bit, reverse the sense of the
	     comparison.  */
	  if (sign_bit_comparison_p)
	    {
	      op0 = XEXP (op0, 0);
	      code = (code == GE ? LT : GE);
	      continue;
	    }
	  break;

	case NEG:
	  /* If testing for equality, we can take the NEG of the constant.  */
	  if (equality_comparison_p
	      && (tem = simplify_unary_operation (NEG, mode, op1, mode)) != 0)
	    {
	      op0 = XEXP (op0, 0);
	      op1 = tem;
	      continue;
	    }

	  /* The remaining cases only apply to comparisons with zero.  */
	  if (const_op != 0)
	    break;

	  /* When X is ABS or is known positive,
	     (neg X) is < 0 if and only if X != 0.  */

	  if (sign_bit_comparison_p
	      && (GET_CODE (XEXP (op0, 0)) == ABS
		  || (mode_width <= HOST_BITS_PER_WIDE_INT
		      && (nonzero_bits (XEXP (op0, 0), mode)
			  & ((HOST_WIDE_INT) 1 << (mode_width - 1))) == 0)))
	    {
	      op0 = XEXP (op0, 0);
	      code = (code == LT ? NE : EQ);
	      continue;
	    }

	  /* If we have NEG of something whose two high-order bits are the
	     same, we know that "(-a) < 0" is equivalent to "a > 0".  */
	  if (num_sign_bit_copies (op0, mode) >= 2)
	    {
	      op0 = XEXP (op0, 0);
	      code = swap_condition (code);
	      continue;
	    }
	  break;

	case ROTATE:
	  /* If we are testing equality and our count is a constant, we
	     can perform the inverse operation on our RHS.  */
	  if (equality_comparison_p && GET_CODE (XEXP (op0, 1)) == CONST_INT
	      && (tem = simplify_binary_operation (ROTATERT, mode,
						   op1, XEXP (op0, 1))) != 0)
	    {
	      op0 = XEXP (op0, 0);
	      op1 = tem;
	      continue;
	    }

	  /* If we are doing a < 0 or >= 0 comparison, it means we are testing
	     a particular bit.  Convert it to an AND of a constant of that
	     bit.  This will be converted into a ZERO_EXTRACT.  */
	  if (const_op == 0 && sign_bit_comparison_p
	      && GET_CODE (XEXP (op0, 1)) == CONST_INT
	      && mode_width <= HOST_BITS_PER_WIDE_INT)
	    {
	      op0 = simplify_and_const_int (NULL_RTX, mode, XEXP (op0, 0),
					    ((HOST_WIDE_INT) 1
					     << (mode_width - 1
						 - INTVAL (XEXP (op0, 1)))));
	      code = (code == LT ? NE : EQ);
	      continue;
	    }

	  /* Fall through.  */

	case ABS:
	  /* ABS is ignorable inside an equality comparison with zero.  */
	  if (const_op == 0 && equality_comparison_p)
	    {
	      op0 = XEXP (op0, 0);
	      continue;
	    }
	  break;

	case SIGN_EXTEND:
	  /* Can simplify (compare (zero/sign_extend FOO) CONST) to
	     (compare FOO CONST) if CONST fits in FOO's mode and we
	     are either testing inequality or have an unsigned
	     comparison with ZERO_EXTEND or a signed comparison with
	     SIGN_EXTEND.  But don't do it if we don't have a compare
	     insn of the given mode, since we'd have to revert it
	     later on, and then we wouldn't know whether to sign- or
	     zero-extend.  */
	  mode = GET_MODE (XEXP (op0, 0));
	  if (mode != VOIDmode && GET_MODE_CLASS (mode) == MODE_INT
	      && ! unsigned_comparison_p
	      && (GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT)
	      && ((unsigned HOST_WIDE_INT) const_op
		  < (((unsigned HOST_WIDE_INT) 1
		      << (GET_MODE_BITSIZE (mode) - 1))))
	      && cmp_optab->handlers[(int) mode].insn_code != CODE_FOR_nothing)
	    {
	      op0 = XEXP (op0, 0);
	      continue;
	    }
	  break;

	case SUBREG:
	  /* Check for the case where we are comparing A - C1 with C2, that is

	       (subreg:MODE (plus (A) (-C1))) op (C2)

	     with C1 a constant, and try to lift the SUBREG, i.e. to do the
	     comparison in the wider mode.  One of the following two conditions
	     must be true in order for this to be valid:

	       1. The mode extension results in the same bit pattern being added
		  on both sides and the comparison is equality or unsigned.  As
		  C2 has been truncated to fit in MODE, the pattern can only be
		  all 0s or all 1s.

	       2. The mode extension results in the sign bit being copied on
		  each side.

	     The difficulty here is that we have predicates for A but not for
	     (A - C1) so we need to check that C1 is within proper bounds so
	     as to perturbate A as little as possible.  */

	  if (mode_width <= HOST_BITS_PER_WIDE_INT
	      && subreg_lowpart_p (op0)
	      && GET_MODE_BITSIZE (GET_MODE (SUBREG_REG (op0))) > mode_width
	      && GET_CODE (SUBREG_REG (op0)) == PLUS
	      && GET_CODE (XEXP (SUBREG_REG (op0), 1)) == CONST_INT)
	    {
	      enum machine_mode inner_mode = GET_MODE (SUBREG_REG (op0));
	      rtx a = XEXP (SUBREG_REG (op0), 0);
	      HOST_WIDE_INT c1 = -INTVAL (XEXP (SUBREG_REG (op0), 1));

	      if ((c1 > 0
		   && (unsigned HOST_WIDE_INT) c1
		       < (unsigned HOST_WIDE_INT) 1 << (mode_width - 1)
		   && (equality_comparison_p || unsigned_comparison_p)
		   /* (A - C1) zero-extends if it is positive and sign-extends
		      if it is negative, C2 both zero- and sign-extends.  */
		   && ((0 == (nonzero_bits (a, inner_mode)
			      & ~GET_MODE_MASK (mode))
			&& const_op >= 0)
		       /* (A - C1) sign-extends if it is positive and 1-extends
			  if it is negative, C2 both sign- and 1-extends.  */
		       || (num_sign_bit_copies (a, inner_mode)
			   > (unsigned int) (GET_MODE_BITSIZE (inner_mode)
					     - mode_width)
			   && const_op < 0)))
		  || ((unsigned HOST_WIDE_INT) c1
		       < (unsigned HOST_WIDE_INT) 1 << (mode_width - 2)
		      /* (A - C1) always sign-extends, like C2.  */
		      && num_sign_bit_copies (a, inner_mode)
			 > (unsigned int) (GET_MODE_BITSIZE (inner_mode)
					   - (mode_width - 1))))
		{
		  op0 = SUBREG_REG (op0);
		  continue;
		}
	    }

	  /* If the inner mode is narrower and we are extracting the low part,
	     we can treat the SUBREG as if it were a ZERO_EXTEND.  */
	  if (subreg_lowpart_p (op0)
	      && GET_MODE_BITSIZE (GET_MODE (SUBREG_REG (op0))) < mode_width)
	    /* Fall through */ ;
	  else
	    break;

	  /* ... fall through ...  */

	case ZERO_EXTEND:
	  mode = GET_MODE (XEXP (op0, 0));
	  if (mode != VOIDmode && GET_MODE_CLASS (mode) == MODE_INT
	      && (unsigned_comparison_p || equality_comparison_p)
	      && (GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT)
	      && ((unsigned HOST_WIDE_INT) const_op < GET_MODE_MASK (mode))
	      && cmp_optab->handlers[(int) mode].insn_code != CODE_FOR_nothing)
	    {
	      op0 = XEXP (op0, 0);
	      continue;
	    }
	  break;

	case PLUS:
	  /* (eq (plus X A) B) -> (eq X (minus B A)).  We can only do
	     this for equality comparisons due to pathological cases involving
	     overflows.  */
	  if (equality_comparison_p
	      && 0 != (tem = simplify_binary_operation (MINUS, mode,
							op1, XEXP (op0, 1))))
	    {
	      op0 = XEXP (op0, 0);
	      op1 = tem;
	      continue;
	    }

	  /* (plus (abs X) (const_int -1)) is < 0 if and only if X == 0.  */
	  if (const_op == 0 && XEXP (op0, 1) == constm1_rtx
	      && GET_CODE (XEXP (op0, 0)) == ABS && sign_bit_comparison_p)
	    {
	      op0 = XEXP (XEXP (op0, 0), 0);
	      code = (code == LT ? EQ : NE);
	      continue;
	    }
	  break;

	case MINUS:
	  /* We used to optimize signed comparisons against zero, but that
	     was incorrect.  Unsigned comparisons against zero (GTU, LEU)
	     arrive here as equality comparisons, or (GEU, LTU) are
	     optimized away.  No need to special-case them.  */

	  /* (eq (minus A B) C) -> (eq A (plus B C)) or
	     (eq B (minus A C)), whichever simplifies.  We can only do
	     this for equality comparisons due to pathological cases involving
	     overflows.  */
	  if (equality_comparison_p
	      && 0 != (tem = simplify_binary_operation (PLUS, mode,
							XEXP (op0, 1), op1)))
	    {
	      op0 = XEXP (op0, 0);
	      op1 = tem;
	      continue;
	    }

	  if (equality_comparison_p
	      && 0 != (tem = simplify_binary_operation (MINUS, mode,
							XEXP (op0, 0), op1)))
	    {
	      op0 = XEXP (op0, 1);
	      op1 = tem;
	      continue;
	    }

	  /* The sign bit of (minus (ashiftrt X C) X), where C is the number
	     of bits in X minus 1, is one iff X > 0.  */
	  if (sign_bit_comparison_p && GET_CODE (XEXP (op0, 0)) == ASHIFTRT
	      && GET_CODE (XEXP (XEXP (op0, 0), 1)) == CONST_INT
	      && (unsigned HOST_WIDE_INT) INTVAL (XEXP (XEXP (op0, 0), 1))
		 == mode_width - 1
	      && rtx_equal_p (XEXP (XEXP (op0, 0), 0), XEXP (op0, 1)))
	    {
	      op0 = XEXP (op0, 1);
	      code = (code == GE ? LE : GT);
	      continue;
	    }
	  break;

	case XOR:
	  /* (eq (xor A B) C) -> (eq A (xor B C)).  This is a simplification
	     if C is zero or B is a constant.  */
	  if (equality_comparison_p
	      && 0 != (tem = simplify_binary_operation (XOR, mode,
							XEXP (op0, 1), op1)))
	    {
	      op0 = XEXP (op0, 0);
	      op1 = tem;
	      continue;
	    }
	  break;

	case EQ:  case NE:
	case UNEQ:  case LTGT:
	case LT:  case LTU:  case UNLT:  case LE:  case LEU:  case UNLE:
	case GT:  case GTU:  case UNGT:  case GE:  case GEU:  case UNGE:
	case UNORDERED: case ORDERED:
	  /* We can't do anything if OP0 is a condition code value, rather
	     than an actual data value.  */
	  if (const_op != 0
	      || CC0_P (XEXP (op0, 0))
	      || GET_MODE_CLASS (GET_MODE (XEXP (op0, 0))) == MODE_CC)
	    break;

	  /* Get the two operands being compared.  */
	  if (GET_CODE (XEXP (op0, 0)) == COMPARE)
	    tem = XEXP (XEXP (op0, 0), 0), tem1 = XEXP (XEXP (op0, 0), 1);
	  else
	    tem = XEXP (op0, 0), tem1 = XEXP (op0, 1);

	  /* Check for the cases where we simply want the result of the
	     earlier test or the opposite of that result.  */
	  if (code == NE || code == EQ
	      || (GET_MODE_BITSIZE (GET_MODE (op0)) <= HOST_BITS_PER_WIDE_INT
		  && GET_MODE_CLASS (GET_MODE (op0)) == MODE_INT
		  && (STORE_FLAG_VALUE
		      & (((HOST_WIDE_INT) 1
			  << (GET_MODE_BITSIZE (GET_MODE (op0)) - 1))))
		  && (code == LT || code == GE)))
	    {
	      enum rtx_code new_code;
	      if (code == LT || code == NE)
		new_code = GET_CODE (op0);
	      else
		new_code = reversed_comparison_code (op0, NULL);

	      if (new_code != UNKNOWN)
		{
		  code = new_code;
		  op0 = tem;
		  op1 = tem1;
		  continue;
		}
	    }
	  break;

	case IOR:
	  /* The sign bit of (ior (plus X (const_int -1)) X) is nonzero
	     iff X <= 0.  */
	  if (sign_bit_comparison_p && GET_CODE (XEXP (op0, 0)) == PLUS
	      && XEXP (XEXP (op0, 0), 1) == constm1_rtx
	      && rtx_equal_p (XEXP (XEXP (op0, 0), 0), XEXP (op0, 1)))
	    {
	      op0 = XEXP (op0, 1);
	      code = (code == GE ? GT : LE);
	      continue;
	    }
	  break;

	case AND:
	  /* Convert (and (xshift 1 X) Y) to (and (lshiftrt Y X) 1).  This
	     will be converted to a ZERO_EXTRACT later.  */
	  if (const_op == 0 && equality_comparison_p
	      && GET_CODE (XEXP (op0, 0)) == ASHIFT
	      && XEXP (XEXP (op0, 0), 0) == const1_rtx)
	    {
	      op0 = simplify_and_const_int
		(NULL_RTX, mode, gen_rtx_LSHIFTRT (mode,
						   XEXP (op0, 1),
						   XEXP (XEXP (op0, 0), 1)),
		 (HOST_WIDE_INT) 1);
	      continue;
	    }

	  /* If we are comparing (and (lshiftrt X C1) C2) for equality with
	     zero and X is a comparison and C1 and C2 describe only bits set
	     in STORE_FLAG_VALUE, we can compare with X.  */
	  if (const_op == 0 && equality_comparison_p
	      && mode_width <= HOST_BITS_PER_WIDE_INT
	      && GET_CODE (XEXP (op0, 1)) == CONST_INT
	      && GET_CODE (XEXP (op0, 0)) == LSHIFTRT
	      && GET_CODE (XEXP (XEXP (op0, 0), 1)) == CONST_INT
	      && INTVAL (XEXP (XEXP (op0, 0), 1)) >= 0
	      && INTVAL (XEXP (XEXP (op0, 0), 1)) < HOST_BITS_PER_WIDE_INT)
	    {
	      mask = ((INTVAL (XEXP (op0, 1)) & GET_MODE_MASK (mode))
		      << INTVAL (XEXP (XEXP (op0, 0), 1)));
	      if ((~STORE_FLAG_VALUE & mask) == 0
		  && (COMPARISON_P (XEXP (XEXP (op0, 0), 0))
		      || ((tem = get_last_value (XEXP (XEXP (op0, 0), 0))) != 0
			  && COMPARISON_P (tem))))
		{
		  op0 = XEXP (XEXP (op0, 0), 0);
		  continue;
		}
	    }

	  /* If we are doing an equality comparison of an AND of a bit equal
	     to the sign bit, replace this with a LT or GE comparison of
	     the underlying value.  */
	  if (equality_comparison_p
	      && const_op == 0
	      && GET_CODE (XEXP (op0, 1)) == CONST_INT
	      && mode_width <= HOST_BITS_PER_WIDE_INT
	      && ((INTVAL (XEXP (op0, 1)) & GET_MODE_MASK (mode))
		  == (unsigned HOST_WIDE_INT) 1 << (mode_width - 1)))
	    {
	      op0 = XEXP (op0, 0);
	      code = (code == EQ ? GE : LT);
	      continue;
	    }

	  /* If this AND operation is really a ZERO_EXTEND from a narrower
	     mode, the constant fits within that mode, and this is either an
	     equality or unsigned comparison, try to do this comparison in
	     the narrower mode.

	     Note that in:

	     (ne:DI (and:DI (reg:DI 4) (const_int 0xffffffff)) (const_int 0))
	     -> (ne:DI (reg:SI 4) (const_int 0))

	     unless TRULY_NOOP_TRUNCATION allows it or the register is
	     known to hold a value of the required mode the
	     transformation is invalid.  */
	  if ((equality_comparison_p || unsigned_comparison_p)
	      && GET_CODE (XEXP (op0, 1)) == CONST_INT
	      && (i = exact_log2 ((INTVAL (XEXP (op0, 1))
				   & GET_MODE_MASK (mode))
				  + 1)) >= 0
	      && const_op >> i == 0
	      && (tmode = mode_for_size (i, MODE_INT, 1)) != BLKmode
	      && (TRULY_NOOP_TRUNCATION (GET_MODE_BITSIZE (tmode),
					 GET_MODE_BITSIZE (GET_MODE (op0)))
		  || (REG_P (XEXP (op0, 0))
		      && reg_truncated_to_mode (tmode, XEXP (op0, 0)))))
	    {
	      op0 = gen_lowpart (tmode, XEXP (op0, 0));
	      continue;
	    }

	  /* If this is (and:M1 (subreg:M2 X 0) (const_int C1)) where C1
	     fits in both M1 and M2 and the SUBREG is either paradoxical
	     or represents the low part, permute the SUBREG and the AND
	     and try again.  */
	  if (GET_CODE (XEXP (op0, 0)) == SUBREG)
	    {
	      unsigned HOST_WIDE_INT c1;
	      tmode = GET_MODE (SUBREG_REG (XEXP (op0, 0)));
	      /* Require an integral mode, to avoid creating something like
		 (AND:SF ...).  */
	      if (SCALAR_INT_MODE_P (tmode)
		  /* It is unsafe to commute the AND into the SUBREG if the
		     SUBREG is paradoxical and WORD_REGISTER_OPERATIONS is
		     not defined.  As originally written the upper bits
		     have a defined value due to the AND operation.
		     However, if we commute the AND inside the SUBREG then
		     they no longer have defined values and the meaning of
		     the code has been changed.  */
		  && (0
#ifdef WORD_REGISTER_OPERATIONS
		      || (mode_width > GET_MODE_BITSIZE (tmode)
			  && mode_width <= BITS_PER_WORD)
#endif
		      || (mode_width <= GET_MODE_BITSIZE (tmode)
			  && subreg_lowpart_p (XEXP (op0, 0))))
		  && GET_CODE (XEXP (op0, 1)) == CONST_INT
		  && mode_width <= HOST_BITS_PER_WIDE_INT
		  && GET_MODE_BITSIZE (tmode) <= HOST_BITS_PER_WIDE_INT
		  && ((c1 = INTVAL (XEXP (op0, 1))) & ~mask) == 0
		  && (c1 & ~GET_MODE_MASK (tmode)) == 0
		  && c1 != mask
		  && c1 != GET_MODE_MASK (tmode))
		{
		  op0 = simplify_gen_binary (AND, tmode,
					     SUBREG_REG (XEXP (op0, 0)),
					     gen_int_mode (c1, tmode));
		  op0 = gen_lowpart (mode, op0);
		  continue;
		}
	    }

	  /* Convert (ne (and (not X) 1) 0) to (eq (and X 1) 0).  */
	  if (const_op == 0 && equality_comparison_p
	      && XEXP (op0, 1) == const1_rtx
	      && GET_CODE (XEXP (op0, 0)) == NOT)
	    {
	      op0 = simplify_and_const_int
		(NULL_RTX, mode, XEXP (XEXP (op0, 0), 0), (HOST_WIDE_INT) 1);
	      code = (code == NE ? EQ : NE);
	      continue;
	    }

	  /* Convert (ne (and (lshiftrt (not X)) 1) 0) to
	     (eq (and (lshiftrt X) 1) 0).
	     Also handle the case where (not X) is expressed using xor.  */
	  if (const_op == 0 && equality_comparison_p
	      && XEXP (op0, 1) == const1_rtx
	      && GET_CODE (XEXP (op0, 0)) == LSHIFTRT)
	    {
	      rtx shift_op = XEXP (XEXP (op0, 0), 0);
	      rtx shift_count = XEXP (XEXP (op0, 0), 1);

	      if (GET_CODE (shift_op) == NOT
		  || (GET_CODE (shift_op) == XOR
		      && GET_CODE (XEXP (shift_op, 1)) == CONST_INT
		      && GET_CODE (shift_count) == CONST_INT
		      && GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
		      && (INTVAL (XEXP (shift_op, 1))
			  == (HOST_WIDE_INT) 1 << INTVAL (shift_count))))
		{
		  op0 = simplify_and_const_int
		    (NULL_RTX, mode,
		     gen_rtx_LSHIFTRT (mode, XEXP (shift_op, 0), shift_count),
		     (HOST_WIDE_INT) 1);
		  code = (code == NE ? EQ : NE);
		  continue;
		}
	    }
	  break;

	case ASHIFT:
	  /* If we have (compare (ashift FOO N) (const_int C)) and
	     the high order N bits of FOO (N+1 if an inequality comparison)
	     are known to be zero, we can do this by comparing FOO with C
	     shifted right N bits so long as the low-order N bits of C are
	     zero.  */
	  if (GET_CODE (XEXP (op0, 1)) == CONST_INT
	      && INTVAL (XEXP (op0, 1)) >= 0
	      && ((INTVAL (XEXP (op0, 1)) + ! equality_comparison_p)
		  < HOST_BITS_PER_WIDE_INT)
	      && ((const_op
		   & (((HOST_WIDE_INT) 1 << INTVAL (XEXP (op0, 1))) - 1)) == 0)
	      && mode_width <= HOST_BITS_PER_WIDE_INT
	      && (nonzero_bits (XEXP (op0, 0), mode)
		  & ~(mask >> (INTVAL (XEXP (op0, 1))
			       + ! equality_comparison_p))) == 0)
	    {
	      /* We must perform a logical shift, not an arithmetic one,
		 as we want the top N bits of C to be zero.  */
	      unsigned HOST_WIDE_INT temp = const_op & GET_MODE_MASK (mode);

	      temp >>= INTVAL (XEXP (op0, 1));
	      op1 = gen_int_mode (temp, mode);
	      op0 = XEXP (op0, 0);
	      continue;
	    }

	  /* If we are doing a sign bit comparison, it means we are testing
	     a particular bit.  Convert it to the appropriate AND.  */
	  if (sign_bit_comparison_p && GET_CODE (XEXP (op0, 1)) == CONST_INT
	      && mode_width <= HOST_BITS_PER_WIDE_INT)
	    {
	      op0 = simplify_and_const_int (NULL_RTX, mode, XEXP (op0, 0),
					    ((HOST_WIDE_INT) 1
					     << (mode_width - 1
						 - INTVAL (XEXP (op0, 1)))));
	      code = (code == LT ? NE : EQ);
	      continue;
	    }

	  /* If this an equality comparison with zero and we are shifting
	     the low bit to the sign bit, we can convert this to an AND of the
	     low-order bit.  */
	  if (const_op == 0 && equality_comparison_p
	      && GET_CODE (XEXP (op0, 1)) == CONST_INT
	      && (unsigned HOST_WIDE_INT) INTVAL (XEXP (op0, 1))
		 == mode_width - 1)
	    {
	      op0 = simplify_and_const_int (NULL_RTX, mode, XEXP (op0, 0),
					    (HOST_WIDE_INT) 1);
	      continue;
	    }
	  break;

	case ASHIFTRT:
	  /* If this is an equality comparison with zero, we can do this
	     as a logical shift, which might be much simpler.  */
	  if (equality_comparison_p && const_op == 0
	      && GET_CODE (XEXP (op0, 1)) == CONST_INT)
	    {
	      op0 = simplify_shift_const (NULL_RTX, LSHIFTRT, mode,
					  XEXP (op0, 0),
					  INTVAL (XEXP (op0, 1)));
	      continue;
	    }

	  /* If OP0 is a sign extension and CODE is not an unsigned comparison,
	     do the comparison in a narrower mode.  */
	  if (! unsigned_comparison_p
	      && GET_CODE (XEXP (op0, 1)) == CONST_INT
	      && GET_CODE (XEXP (op0, 0)) == ASHIFT
	      && XEXP (op0, 1) == XEXP (XEXP (op0, 0), 1)
	      && (tmode = mode_for_size (mode_width - INTVAL (XEXP (op0, 1)),
					 MODE_INT, 1)) != BLKmode
	      && (((unsigned HOST_WIDE_INT) const_op
		   + (GET_MODE_MASK (tmode) >> 1) + 1)
		  <= GET_MODE_MASK (tmode)))
	    {
	      op0 = gen_lowpart (tmode, XEXP (XEXP (op0, 0), 0));
	      continue;
	    }

	  /* Likewise if OP0 is a PLUS of a sign extension with a
	     constant, which is usually represented with the PLUS
	     between the shifts.  */
	  if (! unsigned_comparison_p
	      && GET_CODE (XEXP (op0, 1)) == CONST_INT
	      && GET_CODE (XEXP (op0, 0)) == PLUS
	      && GET_CODE (XEXP (XEXP (op0, 0), 1)) == CONST_INT
	      && GET_CODE (XEXP (XEXP (op0, 0), 0)) == ASHIFT
	      && XEXP (op0, 1) == XEXP (XEXP (XEXP (op0, 0), 0), 1)
	      && (tmode = mode_for_size (mode_width - INTVAL (XEXP (op0, 1)),
					 MODE_INT, 1)) != BLKmode
	      && (((unsigned HOST_WIDE_INT) const_op
		   + (GET_MODE_MASK (tmode) >> 1) + 1)
		  <= GET_MODE_MASK (tmode)))
	    {
	      rtx inner = XEXP (XEXP (XEXP (op0, 0), 0), 0);
	      rtx add_const = XEXP (XEXP (op0, 0), 1);
	      rtx new_const = simplify_gen_binary (ASHIFTRT, GET_MODE (op0),
						   add_const, XEXP (op0, 1));

	      op0 = simplify_gen_binary (PLUS, tmode,
					 gen_lowpart (tmode, inner),
					 new_const);
	      continue;
	    }

	  /* ... fall through ...  */
	case LSHIFTRT:
	  /* If we have (compare (xshiftrt FOO N) (const_int C)) and
	     the low order N bits of FOO are known to be zero, we can do this
	     by comparing FOO with C shifted left N bits so long as no
	     overflow occurs.  */
	  if (GET_CODE (XEXP (op0, 1)) == CONST_INT
	      && INTVAL (XEXP (op0, 1)) >= 0
	      && INTVAL (XEXP (op0, 1)) < HOST_BITS_PER_WIDE_INT
	      && mode_width <= HOST_BITS_PER_WIDE_INT
	      && (nonzero_bits (XEXP (op0, 0), mode)
		  & (((HOST_WIDE_INT) 1 << INTVAL (XEXP (op0, 1))) - 1)) == 0
	      && (((unsigned HOST_WIDE_INT) const_op
		   + (GET_CODE (op0) != LSHIFTRT
		      ? ((GET_MODE_MASK (mode) >> INTVAL (XEXP (op0, 1)) >> 1)
			 + 1)
		      : 0))
		  <= GET_MODE_MASK (mode) >> INTVAL (XEXP (op0, 1))))
	    {
	      /* If the shift was logical, then we must make the condition
		 unsigned.  */
	      if (GET_CODE (op0) == LSHIFTRT)
		code = unsigned_condition (code);

	      const_op <<= INTVAL (XEXP (op0, 1));
	      op1 = GEN_INT (const_op);
	      op0 = XEXP (op0, 0);
	      continue;
	    }

	  /* If we are using this shift to extract just the sign bit, we
	     can replace this with an LT or GE comparison.  */
	  if (const_op == 0
	      && (equality_comparison_p || sign_bit_comparison_p)
	      && GET_CODE (XEXP (op0, 1)) == CONST_INT
	      && (unsigned HOST_WIDE_INT) INTVAL (XEXP (op0, 1))
		 == mode_width - 1)
	    {
	      op0 = XEXP (op0, 0);
	      code = (code == NE || code == GT ? LT : GE);
	      continue;
	    }
	  break;

	default:
	  break;
	}

      break;
    }

  /* Now make any compound operations involved in this comparison.  Then,
     check for an outmost SUBREG on OP0 that is not doing anything or is
     paradoxical.  The latter transformation must only be performed when
     it is known that the "extra" bits will be the same in op0 and op1 or
     that they don't matter.  There are three cases to consider:

     1. SUBREG_REG (op0) is a register.  In this case the bits are don't
     care bits and we can assume they have any convenient value.  So
     making the transformation is safe.

     2. SUBREG_REG (op0) is a memory and LOAD_EXTEND_OP is not defined.
     In this case the upper bits of op0 are undefined.  We should not make
     the simplification in that case as we do not know the contents of
     those bits.

     3. SUBREG_REG (op0) is a memory and LOAD_EXTEND_OP is defined and not
     UNKNOWN.  In that case we know those bits are zeros or ones.  We must
     also be sure that they are the same as the upper bits of op1.

     We can never remove a SUBREG for a non-equality comparison because
     the sign bit is in a different place in the underlying object.  */

  op0 = make_compound_operation (op0, op1 == const0_rtx ? COMPARE : SET);
  op1 = make_compound_operation (op1, SET);

  if (GET_CODE (op0) == SUBREG && subreg_lowpart_p (op0)
      && GET_MODE_CLASS (GET_MODE (op0)) == MODE_INT
      && GET_MODE_CLASS (GET_MODE (SUBREG_REG (op0))) == MODE_INT
      && (code == NE || code == EQ))
    {
      if (GET_MODE_SIZE (GET_MODE (op0))
	  > GET_MODE_SIZE (GET_MODE (SUBREG_REG (op0))))
	{
	  /* For paradoxical subregs, allow case 1 as above.  Case 3 isn't
	     implemented.  */
	  if (REG_P (SUBREG_REG (op0)))
	    {
	      op0 = SUBREG_REG (op0);
	      op1 = gen_lowpart (GET_MODE (op0), op1);
	    }
	}
      else if ((GET_MODE_BITSIZE (GET_MODE (SUBREG_REG (op0)))
		<= HOST_BITS_PER_WIDE_INT)
	       && (nonzero_bits (SUBREG_REG (op0),
				 GET_MODE (SUBREG_REG (op0)))
		   & ~GET_MODE_MASK (GET_MODE (op0))) == 0)
	{
	  tem = gen_lowpart (GET_MODE (SUBREG_REG (op0)), op1);

	  if ((nonzero_bits (tem, GET_MODE (SUBREG_REG (op0)))
	       & ~GET_MODE_MASK (GET_MODE (op0))) == 0)
	    op0 = SUBREG_REG (op0), op1 = tem;
	}
    }

  /* We now do the opposite procedure: Some machines don't have compare
     insns in all modes.  If OP0's mode is an integer mode smaller than a
     word and we can't do a compare in that mode, see if there is a larger
     mode for which we can do the compare.  There are a number of cases in
     which we can use the wider mode.  */

  mode = GET_MODE (op0);
  if (mode != VOIDmode && GET_MODE_CLASS (mode) == MODE_INT
      && GET_MODE_SIZE (mode) < UNITS_PER_WORD
      && ! have_insn_for (COMPARE, mode))
    for (tmode = GET_MODE_WIDER_MODE (mode);
	 (tmode != VOIDmode
	  && GET_MODE_BITSIZE (tmode) <= HOST_BITS_PER_WIDE_INT);
	 tmode = GET_MODE_WIDER_MODE (tmode))
      if (have_insn_for (COMPARE, tmode))
	{
	  int zero_extended;

	  /* If the only nonzero bits in OP0 and OP1 are those in the
	     narrower mode and this is an equality or unsigned comparison,
	     we can use the wider mode.  Similarly for sign-extended
	     values, in which case it is true for all comparisons.  */
	  zero_extended = ((code == EQ || code == NE
			    || code == GEU || code == GTU
			    || code == LEU || code == LTU)
			   && (nonzero_bits (op0, tmode)
			       & ~GET_MODE_MASK (mode)) == 0
			   && ((GET_CODE (op1) == CONST_INT
				|| (nonzero_bits (op1, tmode)
				    & ~GET_MODE_MASK (mode)) == 0)));

	  if (zero_extended
	      || ((num_sign_bit_copies (op0, tmode)
		   > (unsigned int) (GET_MODE_BITSIZE (tmode)
				     - GET_MODE_BITSIZE (mode)))
		  && (num_sign_bit_copies (op1, tmode)
		      > (unsigned int) (GET_MODE_BITSIZE (tmode)
					- GET_MODE_BITSIZE (mode)))))
	    {
	      /* If OP0 is an AND and we don't have an AND in MODE either,
		 make a new AND in the proper mode.  */
	      if (GET_CODE (op0) == AND
		  && !have_insn_for (AND, mode))
		op0 = simplify_gen_binary (AND, tmode,
					   gen_lowpart (tmode,
							XEXP (op0, 0)),
					   gen_lowpart (tmode,
							XEXP (op0, 1)));

	      op0 = gen_lowpart (tmode, op0);
	      if (zero_extended && GET_CODE (op1) == CONST_INT)
		op1 = GEN_INT (INTVAL (op1) & GET_MODE_MASK (mode));
	      op1 = gen_lowpart (tmode, op1);
	      break;
	    }

	  /* If this is a test for negative, we can make an explicit
	     test of the sign bit.  */

	  if (op1 == const0_rtx && (code == LT || code == GE)
	      && GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT)
	    {
	      op0 = simplify_gen_binary (AND, tmode,
					 gen_lowpart (tmode, op0),
					 GEN_INT ((HOST_WIDE_INT) 1
						  << (GET_MODE_BITSIZE (mode)
						      - 1)));
	      code = (code == LT) ? NE : EQ;
	      break;
	    }
	}

#ifdef CANONICALIZE_COMPARISON
  /* If this machine only supports a subset of valid comparisons, see if we
     can convert an unsupported one into a supported one.  */
  CANONICALIZE_COMPARISON (code, op0, op1);
#endif

  *pop0 = op0;
  *pop1 = op1;

  return code;
}

/* Utility function for record_value_for_reg.  Count number of
   rtxs in X.  */
static int
count_rtxs (rtx x)
{
  enum rtx_code code = GET_CODE (x);
  const char *fmt;
  int i, ret = 1;

  if (GET_RTX_CLASS (code) == '2'
      || GET_RTX_CLASS (code) == 'c')
    {
      rtx x0 = XEXP (x, 0);
      rtx x1 = XEXP (x, 1);

      if (x0 == x1)
	return 1 + 2 * count_rtxs (x0);

      if ((GET_RTX_CLASS (GET_CODE (x1)) == '2'
	   || GET_RTX_CLASS (GET_CODE (x1)) == 'c')
	  && (x0 == XEXP (x1, 0) || x0 == XEXP (x1, 1)))
	return 2 + 2 * count_rtxs (x0)
	       + count_rtxs (x == XEXP (x1, 0)
			     ? XEXP (x1, 1) : XEXP (x1, 0));

      if ((GET_RTX_CLASS (GET_CODE (x0)) == '2'
	   || GET_RTX_CLASS (GET_CODE (x0)) == 'c')
	  && (x1 == XEXP (x0, 0) || x1 == XEXP (x0, 1)))
	return 2 + 2 * count_rtxs (x1)
	       + count_rtxs (x == XEXP (x0, 0)
			     ? XEXP (x0, 1) : XEXP (x0, 0));
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    if (fmt[i] == 'e')
      ret += count_rtxs (XEXP (x, i));

  return ret;
}

/* Utility function for following routine.  Called when X is part of a value
   being stored into last_set_value.  Sets last_set_table_tick
   for each register mentioned.  Similar to mention_regs in cse.c  */

static void
update_table_tick (rtx x)
{
  enum rtx_code code = GET_CODE (x);
  const char *fmt = GET_RTX_FORMAT (code);
  int i;

  if (code == REG)
    {
      unsigned int regno = REGNO (x);
      unsigned int endregno
	= regno + (regno < FIRST_PSEUDO_REGISTER
		   ? hard_regno_nregs[regno][GET_MODE (x)] : 1);
      unsigned int r;

      for (r = regno; r < endregno; r++)
	reg_stat[r].last_set_table_tick = label_tick;

      return;
    }

  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    /* Note that we can't have an "E" in values stored; see
       get_last_value_validate.  */
    if (fmt[i] == 'e')
      {
	/* Check for identical subexpressions.  If x contains
	   identical subexpression we only have to traverse one of
	   them.  */
	if (i == 0 && ARITHMETIC_P (x))
	  {
	    /* Note that at this point x1 has already been
	       processed.  */
	    rtx x0 = XEXP (x, 0);
	    rtx x1 = XEXP (x, 1);

	    /* If x0 and x1 are identical then there is no need to
	       process x0.  */
	    if (x0 == x1)
	      break;

	    /* If x0 is identical to a subexpression of x1 then while
	       processing x1, x0 has already been processed.  Thus we
	       are done with x.  */
	    if (ARITHMETIC_P (x1)
		&& (x0 == XEXP (x1, 0) || x0 == XEXP (x1, 1)))
	      break;

	    /* If x1 is identical to a subexpression of x0 then we
	       still have to process the rest of x0.  */
	    if (ARITHMETIC_P (x0)
		&& (x1 == XEXP (x0, 0) || x1 == XEXP (x0, 1)))
	      {
		update_table_tick (XEXP (x0, x1 == XEXP (x0, 0) ? 1 : 0));
		break;
	      }
	  }

	update_table_tick (XEXP (x, i));
      }
}

/* Record that REG is set to VALUE in insn INSN.  If VALUE is zero, we
   are saying that the register is clobbered and we no longer know its
   value.  If INSN is zero, don't update reg_stat[].last_set; this is
   only permitted with VALUE also zero and is used to invalidate the
   register.  */

static void
record_value_for_reg (rtx reg, rtx insn, rtx value)
{
  unsigned int regno = REGNO (reg);
  unsigned int endregno
    = regno + (regno < FIRST_PSEUDO_REGISTER
	       ? hard_regno_nregs[regno][GET_MODE (reg)] : 1);
  unsigned int i;

  /* If VALUE contains REG and we have a previous value for REG, substitute
     the previous value.  */
  if (value && insn && reg_overlap_mentioned_p (reg, value))
    {
      rtx tem;

      /* Set things up so get_last_value is allowed to see anything set up to
	 our insn.  */
      subst_low_cuid = INSN_CUID (insn);
      tem = get_last_value (reg);

      /* If TEM is simply a binary operation with two CLOBBERs as operands,
	 it isn't going to be useful and will take a lot of time to process,
	 so just use the CLOBBER.  */

      if (tem)
	{
	  if (ARITHMETIC_P (tem)
	      && GET_CODE (XEXP (tem, 0)) == CLOBBER
	      && GET_CODE (XEXP (tem, 1)) == CLOBBER)
	    tem = XEXP (tem, 0);
	  else if (count_occurrences (value, reg, 1) >= 2)
	    {
	      /* If there are two or more occurrences of REG in VALUE,
		 prevent the value from growing too much.  */
	      if (count_rtxs (tem) > MAX_LAST_VALUE_RTL)
		tem = gen_rtx_CLOBBER (GET_MODE (tem), const0_rtx);
	    }

	  value = replace_rtx (copy_rtx (value), reg, tem);
	}
    }

  /* For each register modified, show we don't know its value, that
     we don't know about its bitwise content, that its value has been
     updated, and that we don't know the location of the death of the
     register.  */
  for (i = regno; i < endregno; i++)
    {
      if (insn)
	reg_stat[i].last_set = insn;

      reg_stat[i].last_set_value = 0;
      reg_stat[i].last_set_mode = 0;
      reg_stat[i].last_set_nonzero_bits = 0;
      reg_stat[i].last_set_sign_bit_copies = 0;
      reg_stat[i].last_death = 0;
      reg_stat[i].truncated_to_mode = 0;
    }

  /* Mark registers that are being referenced in this value.  */
  if (value)
    update_table_tick (value);

  /* Now update the status of each register being set.
     If someone is using this register in this block, set this register
     to invalid since we will get confused between the two lives in this
     basic block.  This makes using this register always invalid.  In cse, we
     scan the table to invalidate all entries using this register, but this
     is too much work for us.  */

  for (i = regno; i < endregno; i++)
    {
      reg_stat[i].last_set_label = label_tick;
      if (!insn || (value && reg_stat[i].last_set_table_tick == label_tick))
	reg_stat[i].last_set_invalid = 1;
      else
	reg_stat[i].last_set_invalid = 0;
    }

  /* The value being assigned might refer to X (like in "x++;").  In that
     case, we must replace it with (clobber (const_int 0)) to prevent
     infinite loops.  */
  if (value && ! get_last_value_validate (&value, insn,
					  reg_stat[regno].last_set_label, 0))
    {
      value = copy_rtx (value);
      if (! get_last_value_validate (&value, insn,
				     reg_stat[regno].last_set_label, 1))
	value = 0;
    }

  /* For the main register being modified, update the value, the mode, the
     nonzero bits, and the number of sign bit copies.  */

  reg_stat[regno].last_set_value = value;

  if (value)
    {
      enum machine_mode mode = GET_MODE (reg);
      subst_low_cuid = INSN_CUID (insn);
      reg_stat[regno].last_set_mode = mode;
      if (GET_MODE_CLASS (mode) == MODE_INT
	  && GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT)
	mode = nonzero_bits_mode;
      reg_stat[regno].last_set_nonzero_bits = nonzero_bits (value, mode);
      reg_stat[regno].last_set_sign_bit_copies
	= num_sign_bit_copies (value, GET_MODE (reg));
    }
}

/* Called via note_stores from record_dead_and_set_regs to handle one
   SET or CLOBBER in an insn.  DATA is the instruction in which the
   set is occurring.  */

static void
record_dead_and_set_regs_1 (rtx dest, rtx setter, void *data)
{
  rtx record_dead_insn = (rtx) data;

  if (GET_CODE (dest) == SUBREG)
    dest = SUBREG_REG (dest);

  if (!record_dead_insn)
    {
      if (REG_P (dest))
	record_value_for_reg (dest, NULL_RTX, NULL_RTX);
      return;
    }

  if (REG_P (dest))
    {
      /* If we are setting the whole register, we know its value.  Otherwise
	 show that we don't know the value.  We can handle SUBREG in
	 some cases.  */
      if (GET_CODE (setter) == SET && dest == SET_DEST (setter))
	record_value_for_reg (dest, record_dead_insn, SET_SRC (setter));
      else if (GET_CODE (setter) == SET
	       && GET_CODE (SET_DEST (setter)) == SUBREG
	       && SUBREG_REG (SET_DEST (setter)) == dest
	       && GET_MODE_BITSIZE (GET_MODE (dest)) <= BITS_PER_WORD
	       && subreg_lowpart_p (SET_DEST (setter)))
	record_value_for_reg (dest, record_dead_insn,
			      gen_lowpart (GET_MODE (dest),
						       SET_SRC (setter)));
      else
	record_value_for_reg (dest, record_dead_insn, NULL_RTX);
    }
  else if (MEM_P (dest)
	   /* Ignore pushes, they clobber nothing.  */
	   && ! push_operand (dest, GET_MODE (dest)))
    mem_last_set = INSN_CUID (record_dead_insn);
}

/* Update the records of when each REG was most recently set or killed
   for the things done by INSN.  This is the last thing done in processing
   INSN in the combiner loop.

   We update reg_stat[], in particular fields last_set, last_set_value,
   last_set_mode, last_set_nonzero_bits, last_set_sign_bit_copies,
   last_death, and also the similar information mem_last_set (which insn
   most recently modified memory) and last_call_cuid (which insn was the
   most recent subroutine call).  */

static void
record_dead_and_set_regs (rtx insn)
{
  rtx link;
  unsigned int i;

  for (link = REG_NOTES (insn); link; link = XEXP (link, 1))
    {
      if (REG_NOTE_KIND (link) == REG_DEAD
	  && REG_P (XEXP (link, 0)))
	{
	  unsigned int regno = REGNO (XEXP (link, 0));
	  unsigned int endregno
	    = regno + (regno < FIRST_PSEUDO_REGISTER
		       ? hard_regno_nregs[regno][GET_MODE (XEXP (link, 0))]
		       : 1);

	  for (i = regno; i < endregno; i++)
	    reg_stat[i].last_death = insn;
	}
      else if (REG_NOTE_KIND (link) == REG_INC)
	record_value_for_reg (XEXP (link, 0), insn, NULL_RTX);
    }

  if (CALL_P (insn))
    {
      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
	if (TEST_HARD_REG_BIT (regs_invalidated_by_call, i))
	  {
	    reg_stat[i].last_set_value = 0;
	    reg_stat[i].last_set_mode = 0;
	    reg_stat[i].last_set_nonzero_bits = 0;
	    reg_stat[i].last_set_sign_bit_copies = 0;
	    reg_stat[i].last_death = 0;
	    reg_stat[i].truncated_to_mode = 0;
	  }

      last_call_cuid = mem_last_set = INSN_CUID (insn);

      /* We can't combine into a call pattern.  Remember, though, that
	 the return value register is set at this CUID.  We could
	 still replace a register with the return value from the
	 wrong subroutine call!  */
      note_stores (PATTERN (insn), record_dead_and_set_regs_1, NULL_RTX);
    }
  else
    note_stores (PATTERN (insn), record_dead_and_set_regs_1, insn);
}

/* If a SUBREG has the promoted bit set, it is in fact a property of the
   register present in the SUBREG, so for each such SUBREG go back and
   adjust nonzero and sign bit information of the registers that are
   known to have some zero/sign bits set.

   This is needed because when combine blows the SUBREGs away, the
   information on zero/sign bits is lost and further combines can be
   missed because of that.  */

static void
record_promoted_value (rtx insn, rtx subreg)
{
  rtx links, set;
  unsigned int regno = REGNO (SUBREG_REG (subreg));
  enum machine_mode mode = GET_MODE (subreg);

  if (GET_MODE_BITSIZE (mode) > HOST_BITS_PER_WIDE_INT)
    return;

  for (links = LOG_LINKS (insn); links;)
    {
      insn = XEXP (links, 0);
      set = single_set (insn);

      if (! set || !REG_P (SET_DEST (set))
	  || REGNO (SET_DEST (set)) != regno
	  || GET_MODE (SET_DEST (set)) != GET_MODE (SUBREG_REG (subreg)))
	{
	  links = XEXP (links, 1);
	  continue;
	}

      if (reg_stat[regno].last_set == insn)
	{
	  if (SUBREG_PROMOTED_UNSIGNED_P (subreg) > 0)
	    reg_stat[regno].last_set_nonzero_bits &= GET_MODE_MASK (mode);
	}

      if (REG_P (SET_SRC (set)))
	{
	  regno = REGNO (SET_SRC (set));
	  links = LOG_LINKS (insn);
	}
      else
	break;
    }
}

/* Check if X, a register, is known to contain a value already
   truncated to MODE.  In this case we can use a subreg to refer to
   the truncated value even though in the generic case we would need
   an explicit truncation.  */

static bool
reg_truncated_to_mode (enum machine_mode mode, rtx x)
{
  enum machine_mode truncated = reg_stat[REGNO (x)].truncated_to_mode;

  if (truncated == 0 || reg_stat[REGNO (x)].truncation_label != label_tick)
    return false;
  if (GET_MODE_SIZE (truncated) <= GET_MODE_SIZE (mode))
    return true;
  if (TRULY_NOOP_TRUNCATION (GET_MODE_BITSIZE (mode),
			     GET_MODE_BITSIZE (truncated)))
    return true;
  return false;
}

/* X is a REG or a SUBREG.  If X is some sort of a truncation record
   it.  For non-TRULY_NOOP_TRUNCATION targets we might be able to turn
   a truncate into a subreg using this information.  */

static void
record_truncated_value (rtx x)
{
  enum machine_mode truncated_mode;

  if (GET_CODE (x) == SUBREG && REG_P (SUBREG_REG (x)))
    {
      enum machine_mode original_mode = GET_MODE (SUBREG_REG (x));
      truncated_mode = GET_MODE (x);

      if (GET_MODE_SIZE (original_mode) <= GET_MODE_SIZE (truncated_mode))
	return;

      if (TRULY_NOOP_TRUNCATION (GET_MODE_BITSIZE (truncated_mode),
				 GET_MODE_BITSIZE (original_mode)))
	return;

      x = SUBREG_REG (x);
    }
  /* ??? For hard-regs we now record everything.  We might be able to
     optimize this using last_set_mode.  */
  else if (REG_P (x) && REGNO (x) < FIRST_PSEUDO_REGISTER)
    truncated_mode = GET_MODE (x);
  else
    return;

  if (reg_stat[REGNO (x)].truncated_to_mode == 0
      || reg_stat[REGNO (x)].truncation_label < label_tick
      || (GET_MODE_SIZE (truncated_mode)
	  < GET_MODE_SIZE (reg_stat[REGNO (x)].truncated_to_mode)))
    {
      reg_stat[REGNO (x)].truncated_to_mode = truncated_mode;
      reg_stat[REGNO (x)].truncation_label = label_tick;
    }
}

/* Scan X for promoted SUBREGs and truncated REGs.  For each one
   found, note what it implies to the registers used in it.  */

static void
check_conversions (rtx insn, rtx x)
{
  if (GET_CODE (x) == SUBREG || REG_P (x))
    {
      if (GET_CODE (x) == SUBREG
	  && SUBREG_PROMOTED_VAR_P (x)
	  && REG_P (SUBREG_REG (x)))
	record_promoted_value (insn, x);

      record_truncated_value (x);
    }
  else
    {
      const char *format = GET_RTX_FORMAT (GET_CODE (x));
      int i, j;

      for (i = 0; i < GET_RTX_LENGTH (GET_CODE (x)); i++)
	switch (format[i])
	  {
	  case 'e':
	    check_conversions (insn, XEXP (x, i));
	    break;
	  case 'V':
	  case 'E':
	    if (XVEC (x, i) != 0)
	      for (j = 0; j < XVECLEN (x, i); j++)
		check_conversions (insn, XVECEXP (x, i, j));
	    break;
	  }
    }
}

/* Utility routine for the following function.  Verify that all the registers
   mentioned in *LOC are valid when *LOC was part of a value set when
   label_tick == TICK.  Return 0 if some are not.

   If REPLACE is nonzero, replace the invalid reference with
   (clobber (const_int 0)) and return 1.  This replacement is useful because
   we often can get useful information about the form of a value (e.g., if
   it was produced by a shift that always produces -1 or 0) even though
   we don't know exactly what registers it was produced from.  */

static int
get_last_value_validate (rtx *loc, rtx insn, int tick, int replace)
{
  rtx x = *loc;
  const char *fmt = GET_RTX_FORMAT (GET_CODE (x));
  int len = GET_RTX_LENGTH (GET_CODE (x));
  int i;

  if (REG_P (x))
    {
      unsigned int regno = REGNO (x);
      unsigned int endregno
	= regno + (regno < FIRST_PSEUDO_REGISTER
		   ? hard_regno_nregs[regno][GET_MODE (x)] : 1);
      unsigned int j;

      for (j = regno; j < endregno; j++)
	if (reg_stat[j].last_set_invalid
	    /* If this is a pseudo-register that was only set once and not
	       live at the beginning of the function, it is always valid.  */
	    || (! (regno >= FIRST_PSEUDO_REGISTER
		   && REG_N_SETS (regno) == 1
		   && (! REGNO_REG_SET_P
		       (ENTRY_BLOCK_PTR->next_bb->il.rtl->global_live_at_start,
			regno)))
		&& reg_stat[j].last_set_label > tick))
	  {
	    if (replace)
	      *loc = gen_rtx_CLOBBER (GET_MODE (x), const0_rtx);
	    return replace;
	  }

      return 1;
    }
  /* If this is a memory reference, make sure that there were
     no stores after it that might have clobbered the value.  We don't
     have alias info, so we assume any store invalidates it.  */
  else if (MEM_P (x) && !MEM_READONLY_P (x)
	   && INSN_CUID (insn) <= mem_last_set)
    {
      if (replace)
	*loc = gen_rtx_CLOBBER (GET_MODE (x), const0_rtx);
      return replace;
    }

  for (i = 0; i < len; i++)
    {
      if (fmt[i] == 'e')
	{
	  /* Check for identical subexpressions.  If x contains
	     identical subexpression we only have to traverse one of
	     them.  */
	  if (i == 1 && ARITHMETIC_P (x))
	    {
	      /* Note that at this point x0 has already been checked
		 and found valid.  */
	      rtx x0 = XEXP (x, 0);
	      rtx x1 = XEXP (x, 1);

	      /* If x0 and x1 are identical then x is also valid.  */
	      if (x0 == x1)
		return 1;

	      /* If x1 is identical to a subexpression of x0 then
		 while checking x0, x1 has already been checked.  Thus
		 it is valid and so as x.  */
	      if (ARITHMETIC_P (x0)
		  && (x1 == XEXP (x0, 0) || x1 == XEXP (x0, 1)))
		return 1;

	      /* If x0 is identical to a subexpression of x1 then x is
		 valid iff the rest of x1 is valid.  */
	      if (ARITHMETIC_P (x1)
		  && (x0 == XEXP (x1, 0) || x0 == XEXP (x1, 1)))
		return
		  get_last_value_validate (&XEXP (x1,
						  x0 == XEXP (x1, 0) ? 1 : 0),
					   insn, tick, replace);
	    }

	  if (get_last_value_validate (&XEXP (x, i), insn, tick,
				       replace) == 0)
	    return 0;
	}
      /* Don't bother with these.  They shouldn't occur anyway.  */
      else if (fmt[i] == 'E')
	return 0;
    }

  /* If we haven't found a reason for it to be invalid, it is valid.  */
  return 1;
}

/* Get the last value assigned to X, if known.  Some registers
   in the value may be replaced with (clobber (const_int 0)) if their value
   is known longer known reliably.  */

static rtx
get_last_value (rtx x)
{
  unsigned int regno;
  rtx value;

  /* If this is a non-paradoxical SUBREG, get the value of its operand and
     then convert it to the desired mode.  If this is a paradoxical SUBREG,
     we cannot predict what values the "extra" bits might have.  */
  if (GET_CODE (x) == SUBREG
      && subreg_lowpart_p (x)
      && (GET_MODE_SIZE (GET_MODE (x))
	  <= GET_MODE_SIZE (GET_MODE (SUBREG_REG (x))))
      && (value = get_last_value (SUBREG_REG (x))) != 0)
    return gen_lowpart (GET_MODE (x), value);

  if (!REG_P (x))
    return 0;

  regno = REGNO (x);
  value = reg_stat[regno].last_set_value;

  /* If we don't have a value, or if it isn't for this basic block and
     it's either a hard register, set more than once, or it's a live
     at the beginning of the function, return 0.

     Because if it's not live at the beginning of the function then the reg
     is always set before being used (is never used without being set).
     And, if it's set only once, and it's always set before use, then all
     uses must have the same last value, even if it's not from this basic
     block.  */

  if (value == 0
      || (reg_stat[regno].last_set_label != label_tick
	  && (regno < FIRST_PSEUDO_REGISTER
	      || REG_N_SETS (regno) != 1
	      || (REGNO_REG_SET_P
		  (ENTRY_BLOCK_PTR->next_bb->il.rtl->global_live_at_start,
		   regno)))))
    return 0;

  /* If the value was set in a later insn than the ones we are processing,
     we can't use it even if the register was only set once.  */
  if (INSN_CUID (reg_stat[regno].last_set) >= subst_low_cuid)
    return 0;

  /* If the value has all its registers valid, return it.  */
  if (get_last_value_validate (&value, reg_stat[regno].last_set,
			       reg_stat[regno].last_set_label, 0))
    return value;

  /* Otherwise, make a copy and replace any invalid register with
     (clobber (const_int 0)).  If that fails for some reason, return 0.  */

  value = copy_rtx (value);
  if (get_last_value_validate (&value, reg_stat[regno].last_set,
			       reg_stat[regno].last_set_label, 1))
    return value;

  return 0;
}

/* Return nonzero if expression X refers to a REG or to memory
   that is set in an instruction more recent than FROM_CUID.  */

static int
use_crosses_set_p (rtx x, int from_cuid)
{
  const char *fmt;
  int i;
  enum rtx_code code = GET_CODE (x);

  if (code == REG)
    {
      unsigned int regno = REGNO (x);
      unsigned endreg = regno + (regno < FIRST_PSEUDO_REGISTER
				 ? hard_regno_nregs[regno][GET_MODE (x)] : 1);

#ifdef PUSH_ROUNDING
      /* Don't allow uses of the stack pointer to be moved,
	 because we don't know whether the move crosses a push insn.  */
      if (regno == STACK_POINTER_REGNUM && PUSH_ARGS)
	return 1;
#endif
      for (; regno < endreg; regno++)
	if (reg_stat[regno].last_set
	    && INSN_CUID (reg_stat[regno].last_set) > from_cuid)
	  return 1;
      return 0;
    }

  if (code == MEM && mem_last_set > from_cuid)
    return 1;

  fmt = GET_RTX_FORMAT (code);

  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'E')
	{
	  int j;
	  for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	    if (use_crosses_set_p (XVECEXP (x, i, j), from_cuid))
	      return 1;
	}
      else if (fmt[i] == 'e'
	       && use_crosses_set_p (XEXP (x, i), from_cuid))
	return 1;
    }
  return 0;
}

/* Define three variables used for communication between the following
   routines.  */

static unsigned int reg_dead_regno, reg_dead_endregno;
static int reg_dead_flag;

/* Function called via note_stores from reg_dead_at_p.

   If DEST is within [reg_dead_regno, reg_dead_endregno), set
   reg_dead_flag to 1 if X is a CLOBBER and to -1 it is a SET.  */

static void
reg_dead_at_p_1 (rtx dest, rtx x, void *data ATTRIBUTE_UNUSED)
{
  unsigned int regno, endregno;

  if (!REG_P (dest))
    return;

  regno = REGNO (dest);
  endregno = regno + (regno < FIRST_PSEUDO_REGISTER
		      ? hard_regno_nregs[regno][GET_MODE (dest)] : 1);

  if (reg_dead_endregno > regno && reg_dead_regno < endregno)
    reg_dead_flag = (GET_CODE (x) == CLOBBER) ? 1 : -1;
}

/* Return nonzero if REG is known to be dead at INSN.

   We scan backwards from INSN.  If we hit a REG_DEAD note or a CLOBBER
   referencing REG, it is dead.  If we hit a SET referencing REG, it is
   live.  Otherwise, see if it is live or dead at the start of the basic
   block we are in.  Hard regs marked as being live in NEWPAT_USED_REGS
   must be assumed to be always live.  */

static int
reg_dead_at_p (rtx reg, rtx insn)
{
  basic_block block;
  unsigned int i;

  /* Set variables for reg_dead_at_p_1.  */
  reg_dead_regno = REGNO (reg);
  reg_dead_endregno = reg_dead_regno + (reg_dead_regno < FIRST_PSEUDO_REGISTER
					? hard_regno_nregs[reg_dead_regno]
							  [GET_MODE (reg)]
					: 1);

  reg_dead_flag = 0;

  /* Check that reg isn't mentioned in NEWPAT_USED_REGS.  For fixed registers
     we allow the machine description to decide whether use-and-clobber
     patterns are OK.  */
  if (reg_dead_regno < FIRST_PSEUDO_REGISTER)
    {
      for (i = reg_dead_regno; i < reg_dead_endregno; i++)
	if (!fixed_regs[i] && TEST_HARD_REG_BIT (newpat_used_regs, i))
	  return 0;
    }

  /* Scan backwards until we find a REG_DEAD note, SET, CLOBBER, label, or
     beginning of function.  */
  for (; insn && !LABEL_P (insn) && !BARRIER_P (insn);
       insn = prev_nonnote_insn (insn))
    {
      note_stores (PATTERN (insn), reg_dead_at_p_1, NULL);
      if (reg_dead_flag)
	return reg_dead_flag == 1 ? 1 : 0;

      if (find_regno_note (insn, REG_DEAD, reg_dead_regno))
	return 1;
    }

  /* Get the basic block that we were in.  */
  if (insn == 0)
    block = ENTRY_BLOCK_PTR->next_bb;
  else
    {
      FOR_EACH_BB (block)
	if (insn == BB_HEAD (block))
	  break;

      if (block == EXIT_BLOCK_PTR)
	return 0;
    }

  for (i = reg_dead_regno; i < reg_dead_endregno; i++)
    if (REGNO_REG_SET_P (block->il.rtl->global_live_at_start, i))
      return 0;

  return 1;
}

/* Note hard registers in X that are used.  This code is similar to
   that in flow.c, but much simpler since we don't care about pseudos.  */

static void
mark_used_regs_combine (rtx x)
{
  RTX_CODE code = GET_CODE (x);
  unsigned int regno;
  int i;

  switch (code)
    {
    case LABEL_REF:
    case SYMBOL_REF:
    case CONST_INT:
    case CONST:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case PC:
    case ADDR_VEC:
    case ADDR_DIFF_VEC:
    case ASM_INPUT:
#ifdef HAVE_cc0
    /* CC0 must die in the insn after it is set, so we don't need to take
       special note of it here.  */
    case CC0:
#endif
      return;

    case CLOBBER:
      /* If we are clobbering a MEM, mark any hard registers inside the
	 address as used.  */
      if (MEM_P (XEXP (x, 0)))
	mark_used_regs_combine (XEXP (XEXP (x, 0), 0));
      return;

    case REG:
      regno = REGNO (x);
      /* A hard reg in a wide mode may really be multiple registers.
	 If so, mark all of them just like the first.  */
      if (regno < FIRST_PSEUDO_REGISTER)
	{
	  unsigned int endregno, r;

	  /* None of this applies to the stack, frame or arg pointers.  */
	  if (regno == STACK_POINTER_REGNUM
#if FRAME_POINTER_REGNUM != HARD_FRAME_POINTER_REGNUM
	      || regno == HARD_FRAME_POINTER_REGNUM
#endif
#if FRAME_POINTER_REGNUM != ARG_POINTER_REGNUM
	      || (regno == ARG_POINTER_REGNUM && fixed_regs[regno])
#endif
	      || regno == FRAME_POINTER_REGNUM)
	    return;

	  endregno = regno + hard_regno_nregs[regno][GET_MODE (x)];
	  for (r = regno; r < endregno; r++)
	    SET_HARD_REG_BIT (newpat_used_regs, r);
	}
      return;

    case SET:
      {
	/* If setting a MEM, or a SUBREG of a MEM, then note any hard regs in
	   the address.  */
	rtx testreg = SET_DEST (x);

	while (GET_CODE (testreg) == SUBREG
	       || GET_CODE (testreg) == ZERO_EXTRACT
	       || GET_CODE (testreg) == STRICT_LOW_PART)
	  testreg = XEXP (testreg, 0);

	if (MEM_P (testreg))
	  mark_used_regs_combine (XEXP (testreg, 0));

	mark_used_regs_combine (SET_SRC (x));
      }
      return;

    default:
      break;
    }

  /* Recursively scan the operands of this expression.  */

  {
    const char *fmt = GET_RTX_FORMAT (code);

    for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
      {
	if (fmt[i] == 'e')
	  mark_used_regs_combine (XEXP (x, i));
	else if (fmt[i] == 'E')
	  {
	    int j;

	    for (j = 0; j < XVECLEN (x, i); j++)
	      mark_used_regs_combine (XVECEXP (x, i, j));
	  }
      }
  }
}

/* Remove register number REGNO from the dead registers list of INSN.

   Return the note used to record the death, if there was one.  */

rtx
remove_death (unsigned int regno, rtx insn)
{
  rtx note = find_regno_note (insn, REG_DEAD, regno);

  if (note)
    {
      REG_N_DEATHS (regno)--;
      remove_note (insn, note);
    }

  return note;
}

/* For each register (hardware or pseudo) used within expression X, if its
   death is in an instruction with cuid between FROM_CUID (inclusive) and
   TO_INSN (exclusive), put a REG_DEAD note for that register in the
   list headed by PNOTES.

   That said, don't move registers killed by maybe_kill_insn.

   This is done when X is being merged by combination into TO_INSN.  These
   notes will then be distributed as needed.  */

static void
move_deaths (rtx x, rtx maybe_kill_insn, int from_cuid, rtx to_insn,
	     rtx *pnotes)
{
  const char *fmt;
  int len, i;
  enum rtx_code code = GET_CODE (x);

  if (code == REG)
    {
      unsigned int regno = REGNO (x);
      rtx where_dead = reg_stat[regno].last_death;
      rtx before_dead, after_dead;

      /* Don't move the register if it gets killed in between from and to.  */
      if (maybe_kill_insn && reg_set_p (x, maybe_kill_insn)
	  && ! reg_referenced_p (x, maybe_kill_insn))
	return;

      /* WHERE_DEAD could be a USE insn made by combine, so first we
	 make sure that we have insns with valid INSN_CUID values.  */
      before_dead = where_dead;
      while (before_dead && INSN_UID (before_dead) > max_uid_cuid)
	before_dead = PREV_INSN (before_dead);

      after_dead = where_dead;
      while (after_dead && INSN_UID (after_dead) > max_uid_cuid)
	after_dead = NEXT_INSN (after_dead);

      if (before_dead && after_dead
	  && INSN_CUID (before_dead) >= from_cuid
	  && (INSN_CUID (after_dead) < INSN_CUID (to_insn)
	      || (where_dead != after_dead
		  && INSN_CUID (after_dead) == INSN_CUID (to_insn))))
	{
	  rtx note = remove_death (regno, where_dead);

	  /* It is possible for the call above to return 0.  This can occur
	     when last_death points to I2 or I1 that we combined with.
	     In that case make a new note.

	     We must also check for the case where X is a hard register
	     and NOTE is a death note for a range of hard registers
	     including X.  In that case, we must put REG_DEAD notes for
	     the remaining registers in place of NOTE.  */

	  if (note != 0 && regno < FIRST_PSEUDO_REGISTER
	      && (GET_MODE_SIZE (GET_MODE (XEXP (note, 0)))
		  > GET_MODE_SIZE (GET_MODE (x))))
	    {
	      unsigned int deadregno = REGNO (XEXP (note, 0));
	      unsigned int deadend
		= (deadregno + hard_regno_nregs[deadregno]
					       [GET_MODE (XEXP (note, 0))]);
	      unsigned int ourend
		= regno + hard_regno_nregs[regno][GET_MODE (x)];
	      unsigned int i;

	      for (i = deadregno; i < deadend; i++)
		if (i < regno || i >= ourend)
		  REG_NOTES (where_dead)
		    = gen_rtx_EXPR_LIST (REG_DEAD,
					 regno_reg_rtx[i],
					 REG_NOTES (where_dead));
	    }

	  /* If we didn't find any note, or if we found a REG_DEAD note that
	     covers only part of the given reg, and we have a multi-reg hard
	     register, then to be safe we must check for REG_DEAD notes
	     for each register other than the first.  They could have
	     their own REG_DEAD notes lying around.  */
	  else if ((note == 0
		    || (note != 0
			&& (GET_MODE_SIZE (GET_MODE (XEXP (note, 0)))
			    < GET_MODE_SIZE (GET_MODE (x)))))
		   && regno < FIRST_PSEUDO_REGISTER
		   && hard_regno_nregs[regno][GET_MODE (x)] > 1)
	    {
	      unsigned int ourend
		= regno + hard_regno_nregs[regno][GET_MODE (x)];
	      unsigned int i, offset;
	      rtx oldnotes = 0;

	      if (note)
		offset = hard_regno_nregs[regno][GET_MODE (XEXP (note, 0))];
	      else
		offset = 1;

	      for (i = regno + offset; i < ourend; i++)
		move_deaths (regno_reg_rtx[i],
			     maybe_kill_insn, from_cuid, to_insn, &oldnotes);
	    }

	  if (note != 0 && GET_MODE (XEXP (note, 0)) == GET_MODE (x))
	    {
	      XEXP (note, 1) = *pnotes;
	      *pnotes = note;
	    }
	  else
	    *pnotes = gen_rtx_EXPR_LIST (REG_DEAD, x, *pnotes);

	  REG_N_DEATHS (regno)++;
	}

      return;
    }

  else if (GET_CODE (x) == SET)
    {
      rtx dest = SET_DEST (x);

      move_deaths (SET_SRC (x), maybe_kill_insn, from_cuid, to_insn, pnotes);

      /* In the case of a ZERO_EXTRACT, a STRICT_LOW_PART, or a SUBREG
	 that accesses one word of a multi-word item, some
	 piece of everything register in the expression is used by
	 this insn, so remove any old death.  */
      /* ??? So why do we test for equality of the sizes?  */

      if (GET_CODE (dest) == ZERO_EXTRACT
	  || GET_CODE (dest) == STRICT_LOW_PART
	  || (GET_CODE (dest) == SUBREG
	      && (((GET_MODE_SIZE (GET_MODE (dest))
		    + UNITS_PER_WORD - 1) / UNITS_PER_WORD)
		  == ((GET_MODE_SIZE (GET_MODE (SUBREG_REG (dest)))
		       + UNITS_PER_WORD - 1) / UNITS_PER_WORD))))
	{
	  move_deaths (dest, maybe_kill_insn, from_cuid, to_insn, pnotes);
	  return;
	}

      /* If this is some other SUBREG, we know it replaces the entire
	 value, so use that as the destination.  */
      if (GET_CODE (dest) == SUBREG)
	dest = SUBREG_REG (dest);

      /* If this is a MEM, adjust deaths of anything used in the address.
	 For a REG (the only other possibility), the entire value is
	 being replaced so the old value is not used in this insn.  */

      if (MEM_P (dest))
	move_deaths (XEXP (dest, 0), maybe_kill_insn, from_cuid,
		     to_insn, pnotes);
      return;
    }

  else if (GET_CODE (x) == CLOBBER)
    return;

  len = GET_RTX_LENGTH (code);
  fmt = GET_RTX_FORMAT (code);

  for (i = 0; i < len; i++)
    {
      if (fmt[i] == 'E')
	{
	  int j;
	  for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	    move_deaths (XVECEXP (x, i, j), maybe_kill_insn, from_cuid,
			 to_insn, pnotes);
	}
      else if (fmt[i] == 'e')
	move_deaths (XEXP (x, i), maybe_kill_insn, from_cuid, to_insn, pnotes);
    }
}

/* Return 1 if X is the target of a bit-field assignment in BODY, the
   pattern of an insn.  X must be a REG.  */

static int
reg_bitfield_target_p (rtx x, rtx body)
{
  int i;

  if (GET_CODE (body) == SET)
    {
      rtx dest = SET_DEST (body);
      rtx target;
      unsigned int regno, tregno, endregno, endtregno;

      if (GET_CODE (dest) == ZERO_EXTRACT)
	target = XEXP (dest, 0);
      else if (GET_CODE (dest) == STRICT_LOW_PART)
	target = SUBREG_REG (XEXP (dest, 0));
      else
	return 0;

      if (GET_CODE (target) == SUBREG)
	target = SUBREG_REG (target);

      if (!REG_P (target))
	return 0;

      tregno = REGNO (target), regno = REGNO (x);
      if (tregno >= FIRST_PSEUDO_REGISTER || regno >= FIRST_PSEUDO_REGISTER)
	return target == x;

      endtregno = tregno + hard_regno_nregs[tregno][GET_MODE (target)];
      endregno = regno + hard_regno_nregs[regno][GET_MODE (x)];

      return endregno > tregno && regno < endtregno;
    }

  else if (GET_CODE (body) == PARALLEL)
    for (i = XVECLEN (body, 0) - 1; i >= 0; i--)
      if (reg_bitfield_target_p (x, XVECEXP (body, 0, i)))
	return 1;

  return 0;
}

/* Given a chain of REG_NOTES originally from FROM_INSN, try to place them
   as appropriate.  I3 and I2 are the insns resulting from the combination
   insns including FROM (I2 may be zero).

   ELIM_I2 and ELIM_I1 are either zero or registers that we know will
   not need REG_DEAD notes because they are being substituted for.  This
   saves searching in the most common cases.

   Each note in the list is either ignored or placed on some insns, depending
   on the type of note.  */

static void
distribute_notes (rtx notes, rtx from_insn, rtx i3, rtx i2, rtx elim_i2,
		  rtx elim_i1)
{
  rtx note, next_note;
  rtx tem;

  for (note = notes; note; note = next_note)
    {
      rtx place = 0, place2 = 0;

      next_note = XEXP (note, 1);
      switch (REG_NOTE_KIND (note))
	{
	case REG_BR_PROB:
	case REG_BR_PRED:
	  /* Doesn't matter much where we put this, as long as it's somewhere.
	     It is preferable to keep these notes on branches, which is most
	     likely to be i3.  */
	  place = i3;
	  break;

	case REG_VALUE_PROFILE:
	  /* Just get rid of this note, as it is unused later anyway.  */
	  break;

	case REG_NON_LOCAL_GOTO:
	  if (JUMP_P (i3))
	    place = i3;
	  else
	    {
	      gcc_assert (i2 && JUMP_P (i2));
	      place = i2;
	    }
	  break;

	case REG_EH_REGION:
	  /* These notes must remain with the call or trapping instruction.  */
	  if (CALL_P (i3))
	    place = i3;
	  else if (i2 && CALL_P (i2))
	    place = i2;
	  else
	    {
	      gcc_assert (flag_non_call_exceptions);
	      if (may_trap_p (i3))
		place = i3;
	      else if (i2 && may_trap_p (i2))
		place = i2;
	      /* ??? Otherwise assume we've combined things such that we
		 can now prove that the instructions can't trap.  Drop the
		 note in this case.  */
	    }
	  break;

	case REG_NORETURN:
	case REG_SETJMP:
	  /* These notes must remain with the call.  It should not be
	     possible for both I2 and I3 to be a call.  */
	  if (CALL_P (i3))
	    place = i3;
	  else
	    {
	      gcc_assert (i2 && CALL_P (i2));
	      place = i2;
	    }
	  break;

	case REG_UNUSED:
	  /* Any clobbers for i3 may still exist, and so we must process
	     REG_UNUSED notes from that insn.

	     Any clobbers from i2 or i1 can only exist if they were added by
	     recog_for_combine.  In that case, recog_for_combine created the
	     necessary REG_UNUSED notes.  Trying to keep any original
	     REG_UNUSED notes from these insns can cause incorrect output
	     if it is for the same register as the original i3 dest.
	     In that case, we will notice that the register is set in i3,
	     and then add a REG_UNUSED note for the destination of i3, which
	     is wrong.  However, it is possible to have REG_UNUSED notes from
	     i2 or i1 for register which were both used and clobbered, so
	     we keep notes from i2 or i1 if they will turn into REG_DEAD
	     notes.  */

	  /* If this register is set or clobbered in I3, put the note there
	     unless there is one already.  */
	  if (reg_set_p (XEXP (note, 0), PATTERN (i3)))
	    {
	      if (from_insn != i3)
		break;

	      if (! (REG_P (XEXP (note, 0))
		     ? find_regno_note (i3, REG_UNUSED, REGNO (XEXP (note, 0)))
		     : find_reg_note (i3, REG_UNUSED, XEXP (note, 0))))
		place = i3;
	    }
	  /* Otherwise, if this register is used by I3, then this register
	     now dies here, so we must put a REG_DEAD note here unless there
	     is one already.  */
	  else if (reg_referenced_p (XEXP (note, 0), PATTERN (i3))
		   && ! (REG_P (XEXP (note, 0))
			 ? find_regno_note (i3, REG_DEAD,
					    REGNO (XEXP (note, 0)))
			 : find_reg_note (i3, REG_DEAD, XEXP (note, 0))))
	    {
	      PUT_REG_NOTE_KIND (note, REG_DEAD);
	      place = i3;
	    }
	  break;

	case REG_EQUAL:
	case REG_EQUIV:
	case REG_NOALIAS:
	  /* These notes say something about results of an insn.  We can
	     only support them if they used to be on I3 in which case they
	     remain on I3.  Otherwise they are ignored.

	     If the note refers to an expression that is not a constant, we
	     must also ignore the note since we cannot tell whether the
	     equivalence is still true.  It might be possible to do
	     slightly better than this (we only have a problem if I2DEST
	     or I1DEST is present in the expression), but it doesn't
	     seem worth the trouble.  */

	  if (from_insn == i3
	      && (XEXP (note, 0) == 0 || CONSTANT_P (XEXP (note, 0))))
	    place = i3;
	  break;

	case REG_INC:
	case REG_NO_CONFLICT:
	  /* These notes say something about how a register is used.  They must
	     be present on any use of the register in I2 or I3.  */
	  if (reg_mentioned_p (XEXP (note, 0), PATTERN (i3)))
	    place = i3;

	  if (i2 && reg_mentioned_p (XEXP (note, 0), PATTERN (i2)))
	    {
	      if (place)
		place2 = i2;
	      else
		place = i2;
	    }
	  break;

	case REG_LABEL:
	  /* This can show up in several ways -- either directly in the
	     pattern, or hidden off in the constant pool with (or without?)
	     a REG_EQUAL note.  */
	  /* ??? Ignore the without-reg_equal-note problem for now.  */
	  if (reg_mentioned_p (XEXP (note, 0), PATTERN (i3))
	      || ((tem = find_reg_note (i3, REG_EQUAL, NULL_RTX))
		  && GET_CODE (XEXP (tem, 0)) == LABEL_REF
		  && XEXP (XEXP (tem, 0), 0) == XEXP (note, 0)))
	    place = i3;

	  if (i2
	      && (reg_mentioned_p (XEXP (note, 0), PATTERN (i2))
		  || ((tem = find_reg_note (i2, REG_EQUAL, NULL_RTX))
		      && GET_CODE (XEXP (tem, 0)) == LABEL_REF
		      && XEXP (XEXP (tem, 0), 0) == XEXP (note, 0))))
	    {
	      if (place)
		place2 = i2;
	      else
		place = i2;
	    }

	  /* Don't attach REG_LABEL note to a JUMP_INSN.  Add
	     a JUMP_LABEL instead or decrement LABEL_NUSES.  */
	  if (place && JUMP_P (place))
	    {
	      rtx label = JUMP_LABEL (place);

	      if (!label)
		JUMP_LABEL (place) = XEXP (note, 0);
	      else
		{
		  gcc_assert (label == XEXP (note, 0));
		  if (LABEL_P (label))
		    LABEL_NUSES (label)--;
		}
	      place = 0;
	    }
	  if (place2 && JUMP_P (place2))
	    {
	      rtx label = JUMP_LABEL (place2);

	      if (!label)
		JUMP_LABEL (place2) = XEXP (note, 0);
	      else
		{
		  gcc_assert (label == XEXP (note, 0));
		  if (LABEL_P (label))
		    LABEL_NUSES (label)--;
		}
	      place2 = 0;
	    }
	  break;

	case REG_NONNEG:
	  /* This note says something about the value of a register prior
	     to the execution of an insn.  It is too much trouble to see
	     if the note is still correct in all situations.  It is better
	     to simply delete it.  */
	  break;

	case REG_RETVAL:
	  /* If the insn previously containing this note still exists,
	     put it back where it was.  Otherwise move it to the previous
	     insn.  Adjust the corresponding REG_LIBCALL note.  */
	  if (!NOTE_P (from_insn))
	    place = from_insn;
	  else
	    {
	      tem = find_reg_note (XEXP (note, 0), REG_LIBCALL, NULL_RTX);
	      place = prev_real_insn (from_insn);
	      if (tem && place)
		XEXP (tem, 0) = place;
	      /* If we're deleting the last remaining instruction of a
		 libcall sequence, don't add the notes.  */
	      else if (XEXP (note, 0) == from_insn)
		tem = place = 0;
	      /* Don't add the dangling REG_RETVAL note.  */
	      else if (! tem)
		place = 0;
	    }
	  break;

	case REG_LIBCALL:
	  /* This is handled similarly to REG_RETVAL.  */
	  if (!NOTE_P (from_insn))
	    place = from_insn;
	  else
	    {
	      tem = find_reg_note (XEXP (note, 0), REG_RETVAL, NULL_RTX);
	      place = next_real_insn (from_insn);
	      if (tem && place)
		XEXP (tem, 0) = place;
	      /* If we're deleting the last remaining instruction of a
		 libcall sequence, don't add the notes.  */
	      else if (XEXP (note, 0) == from_insn)
		tem = place = 0;
	      /* Don't add the dangling REG_LIBCALL note.  */
	      else if (! tem)
		place = 0;
	    }
	  break;

	case REG_DEAD:
	  /* If we replaced the right hand side of FROM_INSN with a
	     REG_EQUAL note, the original use of the dying register
	     will not have been combined into I3 and I2.  In such cases,
	     FROM_INSN is guaranteed to be the first of the combined
	     instructions, so we simply need to search back before
	     FROM_INSN for the previous use or set of this register,
	     then alter the notes there appropriately.

	     If the register is used as an input in I3, it dies there.
	     Similarly for I2, if it is nonzero and adjacent to I3.

	     If the register is not used as an input in either I3 or I2
	     and it is not one of the registers we were supposed to eliminate,
	     there are two possibilities.  We might have a non-adjacent I2
	     or we might have somehow eliminated an additional register
	     from a computation.  For example, we might have had A & B where
	     we discover that B will always be zero.  In this case we will
	     eliminate the reference to A.

	     In both cases, we must search to see if we can find a previous
	     use of A and put the death note there.  */

	  if (from_insn
	      && from_insn == i2mod
	      && !reg_overlap_mentioned_p (XEXP (note, 0), i2mod_new_rhs))
	    tem = from_insn;
	  else
	    {
	      if (from_insn
		  && CALL_P (from_insn)
		  && find_reg_fusage (from_insn, USE, XEXP (note, 0)))
		place = from_insn;
	      else if (reg_referenced_p (XEXP (note, 0), PATTERN (i3)))
		place = i3;
	      else if (i2 != 0 && next_nonnote_insn (i2) == i3
		       && reg_referenced_p (XEXP (note, 0), PATTERN (i2)))
		place = i2;
	      else if ((rtx_equal_p (XEXP (note, 0), elim_i2)
			&& !(i2mod
			     && reg_overlap_mentioned_p (XEXP (note, 0),
							 i2mod_old_rhs)))
		       || rtx_equal_p (XEXP (note, 0), elim_i1))
		break;
	      tem = i3;
	    }

	  if (place == 0)
	    {
	      basic_block bb = this_basic_block;

	      for (tem = PREV_INSN (tem); place == 0; tem = PREV_INSN (tem))
		{
		  if (! INSN_P (tem))
		    {
		      if (tem == BB_HEAD (bb))
			break;
		      continue;
		    }

		  /* If the register is being set at TEM, see if that is all
		     TEM is doing.  If so, delete TEM.  Otherwise, make this
		     into a REG_UNUSED note instead. Don't delete sets to
		     global register vars.  */
		  if ((REGNO (XEXP (note, 0)) >= FIRST_PSEUDO_REGISTER
		       || !global_regs[REGNO (XEXP (note, 0))])
		      && reg_set_p (XEXP (note, 0), PATTERN (tem)))
		    {
		      rtx set = single_set (tem);
		      rtx inner_dest = 0;
#ifdef HAVE_cc0
		      rtx cc0_setter = NULL_RTX;
#endif

		      if (set != 0)
			for (inner_dest = SET_DEST (set);
			     (GET_CODE (inner_dest) == STRICT_LOW_PART
			      || GET_CODE (inner_dest) == SUBREG
			      || GET_CODE (inner_dest) == ZERO_EXTRACT);
			     inner_dest = XEXP (inner_dest, 0))
			  ;

		      /* Verify that it was the set, and not a clobber that
			 modified the register.

			 CC0 targets must be careful to maintain setter/user
			 pairs.  If we cannot delete the setter due to side
			 effects, mark the user with an UNUSED note instead
			 of deleting it.  */

		      if (set != 0 && ! side_effects_p (SET_SRC (set))
			  && rtx_equal_p (XEXP (note, 0), inner_dest)
#ifdef HAVE_cc0
			  && (! reg_mentioned_p (cc0_rtx, SET_SRC (set))
			      || ((cc0_setter = prev_cc0_setter (tem)) != NULL
				  && sets_cc0_p (PATTERN (cc0_setter)) > 0))
#endif
			  )
			{
			  /* Move the notes and links of TEM elsewhere.
			     This might delete other dead insns recursively.
			     First set the pattern to something that won't use
			     any register.  */
			  rtx old_notes = REG_NOTES (tem);

			  PATTERN (tem) = pc_rtx;
			  REG_NOTES (tem) = NULL;

			  distribute_notes (old_notes, tem, tem, NULL_RTX,
					    NULL_RTX, NULL_RTX);
			  distribute_links (LOG_LINKS (tem));

			  SET_INSN_DELETED (tem);

#ifdef HAVE_cc0
			  /* Delete the setter too.  */
			  if (cc0_setter)
			    {
			      PATTERN (cc0_setter) = pc_rtx;
			      old_notes = REG_NOTES (cc0_setter);
			      REG_NOTES (cc0_setter) = NULL;

			      distribute_notes (old_notes, cc0_setter,
						cc0_setter, NULL_RTX,
						NULL_RTX, NULL_RTX);
			      distribute_links (LOG_LINKS (cc0_setter));

			      SET_INSN_DELETED (cc0_setter);
			    }
#endif
			}
		      else
			{
			  PUT_REG_NOTE_KIND (note, REG_UNUSED);

			  /*  If there isn't already a REG_UNUSED note, put one
			      here.  Do not place a REG_DEAD note, even if
			      the register is also used here; that would not
			      match the algorithm used in lifetime analysis
			      and can cause the consistency check in the
			      scheduler to fail.  */
			  if (! find_regno_note (tem, REG_UNUSED,
						 REGNO (XEXP (note, 0))))
			    place = tem;
			  break;
			}
		    }
		  else if (reg_referenced_p (XEXP (note, 0), PATTERN (tem))
			   || (CALL_P (tem)
			       && find_reg_fusage (tem, USE, XEXP (note, 0))))
		    {
		      place = tem;

		      /* If we are doing a 3->2 combination, and we have a
			 register which formerly died in i3 and was not used
			 by i2, which now no longer dies in i3 and is used in
			 i2 but does not die in i2, and place is between i2
			 and i3, then we may need to move a link from place to
			 i2.  */
		      if (i2 && INSN_UID (place) <= max_uid_cuid
			  && INSN_CUID (place) > INSN_CUID (i2)
			  && from_insn
			  && INSN_CUID (from_insn) > INSN_CUID (i2)
			  && reg_referenced_p (XEXP (note, 0), PATTERN (i2)))
			{
			  rtx links = LOG_LINKS (place);
			  LOG_LINKS (place) = 0;
			  distribute_links (links);
			}
		      break;
		    }

		  if (tem == BB_HEAD (bb))
		    break;
		}

	      /* We haven't found an insn for the death note and it
		 is still a REG_DEAD note, but we have hit the beginning
		 of the block.  If the existing life info says the reg
		 was dead, there's nothing left to do.  Otherwise, we'll
		 need to do a global life update after combine.  */
	      if (REG_NOTE_KIND (note) == REG_DEAD && place == 0
		  && REGNO_REG_SET_P (bb->il.rtl->global_live_at_start,
				      REGNO (XEXP (note, 0))))
		SET_BIT (refresh_blocks, this_basic_block->index);
	    }

	  /* If the register is set or already dead at PLACE, we needn't do
	     anything with this note if it is still a REG_DEAD note.
	     We check here if it is set at all, not if is it totally replaced,
	     which is what `dead_or_set_p' checks, so also check for it being
	     set partially.  */

	  if (place && REG_NOTE_KIND (note) == REG_DEAD)
	    {
	      unsigned int regno = REGNO (XEXP (note, 0));

	      /* Similarly, if the instruction on which we want to place
		 the note is a noop, we'll need do a global live update
		 after we remove them in delete_noop_moves.  */
	      if (noop_move_p (place))
		SET_BIT (refresh_blocks, this_basic_block->index);

	      if (dead_or_set_p (place, XEXP (note, 0))
		  || reg_bitfield_target_p (XEXP (note, 0), PATTERN (place)))
		{
		  /* Unless the register previously died in PLACE, clear
		     last_death.  [I no longer understand why this is
		     being done.] */
		  if (reg_stat[regno].last_death != place)
		    reg_stat[regno].last_death = 0;
		  place = 0;
		}
	      else
		reg_stat[regno].last_death = place;

	      /* If this is a death note for a hard reg that is occupying
		 multiple registers, ensure that we are still using all
		 parts of the object.  If we find a piece of the object
		 that is unused, we must arrange for an appropriate REG_DEAD
		 note to be added for it.  However, we can't just emit a USE
		 and tag the note to it, since the register might actually
		 be dead; so we recourse, and the recursive call then finds
		 the previous insn that used this register.  */

	      if (place && regno < FIRST_PSEUDO_REGISTER
		  && hard_regno_nregs[regno][GET_MODE (XEXP (note, 0))] > 1)
		{
		  unsigned int endregno
		    = regno + hard_regno_nregs[regno]
					      [GET_MODE (XEXP (note, 0))];
		  int all_used = 1;
		  unsigned int i;

		  for (i = regno; i < endregno; i++)
		    if ((! refers_to_regno_p (i, i + 1, PATTERN (place), 0)
			 && ! find_regno_fusage (place, USE, i))
			|| dead_or_set_regno_p (place, i))
		      all_used = 0;

		  if (! all_used)
		    {
		      /* Put only REG_DEAD notes for pieces that are
			 not already dead or set.  */

		      for (i = regno; i < endregno;
			   i += hard_regno_nregs[i][reg_raw_mode[i]])
			{
			  rtx piece = regno_reg_rtx[i];
			  basic_block bb = this_basic_block;

			  if (! dead_or_set_p (place, piece)
			      && ! reg_bitfield_target_p (piece,
							  PATTERN (place)))
			    {
			      rtx new_note
				= gen_rtx_EXPR_LIST (REG_DEAD, piece, NULL_RTX);

			      distribute_notes (new_note, place, place,
						NULL_RTX, NULL_RTX, NULL_RTX);
			    }
			  else if (! refers_to_regno_p (i, i + 1,
							PATTERN (place), 0)
				   && ! find_regno_fusage (place, USE, i))
			    for (tem = PREV_INSN (place); ;
				 tem = PREV_INSN (tem))
			      {
				if (! INSN_P (tem))
				  {
				    if (tem == BB_HEAD (bb))
				      {
					SET_BIT (refresh_blocks,
						 this_basic_block->index);
					break;
				      }
				    continue;
				  }
				if (dead_or_set_p (tem, piece)
				    || reg_bitfield_target_p (piece,
							      PATTERN (tem)))
				  {
				    REG_NOTES (tem)
				      = gen_rtx_EXPR_LIST (REG_UNUSED, piece,
							   REG_NOTES (tem));
				    break;
				  }
			      }

			}

		      place = 0;
		    }
		}
	    }
	  break;

	default:
	  /* Any other notes should not be present at this point in the
	     compilation.  */
	  gcc_unreachable ();
	}

      if (place)
	{
	  XEXP (note, 1) = REG_NOTES (place);
	  REG_NOTES (place) = note;
	}
      else if ((REG_NOTE_KIND (note) == REG_DEAD
		|| REG_NOTE_KIND (note) == REG_UNUSED)
	       && REG_P (XEXP (note, 0)))
	REG_N_DEATHS (REGNO (XEXP (note, 0)))--;

      if (place2)
	{
	  if ((REG_NOTE_KIND (note) == REG_DEAD
	       || REG_NOTE_KIND (note) == REG_UNUSED)
	      && REG_P (XEXP (note, 0)))
	    REG_N_DEATHS (REGNO (XEXP (note, 0)))++;

	  REG_NOTES (place2) = gen_rtx_fmt_ee (GET_CODE (note),
					       REG_NOTE_KIND (note),
					       XEXP (note, 0),
					       REG_NOTES (place2));
	}
    }
}

/* Similarly to above, distribute the LOG_LINKS that used to be present on
   I3, I2, and I1 to new locations.  This is also called to add a link
   pointing at I3 when I3's destination is changed.  */

static void
distribute_links (rtx links)
{
  rtx link, next_link;

  for (link = links; link; link = next_link)
    {
      rtx place = 0;
      rtx insn;
      rtx set, reg;

      next_link = XEXP (link, 1);

      /* If the insn that this link points to is a NOTE or isn't a single
	 set, ignore it.  In the latter case, it isn't clear what we
	 can do other than ignore the link, since we can't tell which
	 register it was for.  Such links wouldn't be used by combine
	 anyway.

	 It is not possible for the destination of the target of the link to
	 have been changed by combine.  The only potential of this is if we
	 replace I3, I2, and I1 by I3 and I2.  But in that case the
	 destination of I2 also remains unchanged.  */

      if (NOTE_P (XEXP (link, 0))
	  || (set = single_set (XEXP (link, 0))) == 0)
	continue;

      reg = SET_DEST (set);
      while (GET_CODE (reg) == SUBREG || GET_CODE (reg) == ZERO_EXTRACT
	     || GET_CODE (reg) == STRICT_LOW_PART)
	reg = XEXP (reg, 0);

      /* A LOG_LINK is defined as being placed on the first insn that uses
	 a register and points to the insn that sets the register.  Start
	 searching at the next insn after the target of the link and stop
	 when we reach a set of the register or the end of the basic block.

	 Note that this correctly handles the link that used to point from
	 I3 to I2.  Also note that not much searching is typically done here
	 since most links don't point very far away.  */

      for (insn = NEXT_INSN (XEXP (link, 0));
	   (insn && (this_basic_block->next_bb == EXIT_BLOCK_PTR
		     || BB_HEAD (this_basic_block->next_bb) != insn));
	   insn = NEXT_INSN (insn))
	if (INSN_P (insn) && reg_overlap_mentioned_p (reg, PATTERN (insn)))
	  {
	    if (reg_referenced_p (reg, PATTERN (insn)))
	      place = insn;
	    break;
	  }
	else if (CALL_P (insn)
		 && find_reg_fusage (insn, USE, reg))
	  {
	    place = insn;
	    break;
	  }
	else if (INSN_P (insn) && reg_set_p (reg, insn))
	  break;

      /* If we found a place to put the link, place it there unless there
	 is already a link to the same insn as LINK at that point.  */

      if (place)
	{
	  rtx link2;

	  for (link2 = LOG_LINKS (place); link2; link2 = XEXP (link2, 1))
	    if (XEXP (link2, 0) == XEXP (link, 0))
	      break;

	  if (link2 == 0)
	    {
	      XEXP (link, 1) = LOG_LINKS (place);
	      LOG_LINKS (place) = link;

	      /* Set added_links_insn to the earliest insn we added a
		 link to.  */
	      if (added_links_insn == 0
		  || INSN_CUID (added_links_insn) > INSN_CUID (place))
		added_links_insn = place;
	    }
	}
    }
}

/* Subroutine of unmentioned_reg_p and callback from for_each_rtx.
   Check whether the expression pointer to by LOC is a register or
   memory, and if so return 1 if it isn't mentioned in the rtx EXPR.
   Otherwise return zero.  */

static int
unmentioned_reg_p_1 (rtx *loc, void *expr)
{
  rtx x = *loc;

  if (x != NULL_RTX
      && (REG_P (x) || MEM_P (x))
      && ! reg_mentioned_p (x, (rtx) expr))
    return 1;
  return 0;
}

/* Check for any register or memory mentioned in EQUIV that is not
   mentioned in EXPR.  This is used to restrict EQUIV to "specializations"
   of EXPR where some registers may have been replaced by constants.  */

static bool
unmentioned_reg_p (rtx equiv, rtx expr)
{
  return for_each_rtx (&equiv, unmentioned_reg_p_1, expr);
}

/* Compute INSN_CUID for INSN, which is an insn made by combine.  */

static int
insn_cuid (rtx insn)
{
  while (insn != 0 && INSN_UID (insn) > max_uid_cuid
	 && NONJUMP_INSN_P (insn) && GET_CODE (PATTERN (insn)) == USE)
    insn = NEXT_INSN (insn);

  gcc_assert (INSN_UID (insn) <= max_uid_cuid);

  return INSN_CUID (insn);
}

void
dump_combine_stats (FILE *file)
{
  fprintf
    (file,
     ";; Combiner statistics: %d attempts, %d substitutions (%d requiring new space),\n;; %d successes.\n\n",
     combine_attempts, combine_merges, combine_extras, combine_successes);
}

void
dump_combine_total_stats (FILE *file)
{
  fprintf
    (file,
     "\n;; Combiner totals: %d attempts, %d substitutions (%d requiring new space),\n;; %d successes.\n",
     total_attempts, total_merges, total_extras, total_successes);
}


static bool
gate_handle_combine (void)
{
  return (optimize > 0);
}

/* Try combining insns through substitution.  */
static unsigned int
rest_of_handle_combine (void)
{
  int rebuild_jump_labels_after_combine
    = combine_instructions (get_insns (), max_reg_num ());

  /* Combining insns may have turned an indirect jump into a
     direct jump.  Rebuild the JUMP_LABEL fields of jumping
     instructions.  */
  if (rebuild_jump_labels_after_combine)
    {
      timevar_push (TV_JUMP);
      rebuild_jump_labels (get_insns ());
      timevar_pop (TV_JUMP);

      delete_dead_jumptables ();
      cleanup_cfg (CLEANUP_EXPENSIVE | CLEANUP_UPDATE_LIFE);
    }
  return 0;
}

struct tree_opt_pass pass_combine =
{
  "combine",                            /* name */
  gate_handle_combine,                  /* gate */
  rest_of_handle_combine,               /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_COMBINE,                           /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func |
  TODO_ggc_collect,                     /* todo_flags_finish */
  'c'                                   /* letter */
};

