/* Search an insn for pseudo regs that must be in hard regs and are not.
   Copyright (C) 1987, 1988, 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006  Free Software Foundation,
   Inc.

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

/* This file contains subroutines used only from the file reload1.c.
   It knows how to scan one insn for operands and values
   that need to be copied into registers to make valid code.
   It also finds other operands and values which are valid
   but for which equivalent values in registers exist and
   ought to be used instead.

   Before processing the first insn of the function, call `init_reload'.
   init_reload actually has to be called earlier anyway.

   To scan an insn, call `find_reloads'.  This does two things:
   1. sets up tables describing which values must be reloaded
   for this insn, and what kind of hard regs they must be reloaded into;
   2. optionally record the locations where those values appear in
   the data, so they can be replaced properly later.
   This is done only if the second arg to `find_reloads' is nonzero.

   The third arg to `find_reloads' specifies the number of levels
   of indirect addressing supported by the machine.  If it is zero,
   indirect addressing is not valid.  If it is one, (MEM (REG n))
   is valid even if (REG n) did not get a hard register; if it is two,
   (MEM (MEM (REG n))) is also valid even if (REG n) did not get a
   hard register, and similarly for higher values.

   Then you must choose the hard regs to reload those pseudo regs into,
   and generate appropriate load insns before this insn and perhaps
   also store insns after this insn.  Set up the array `reload_reg_rtx'
   to contain the REG rtx's for the registers you used.  In some
   cases `find_reloads' will return a nonzero value in `reload_reg_rtx'
   for certain reloads.  Then that tells you which register to use,
   so you do not need to allocate one.  But you still do need to add extra
   instructions to copy the value into and out of that register.

   Finally you must call `subst_reloads' to substitute the reload reg rtx's
   into the locations already recorded.

NOTE SIDE EFFECTS:

   find_reloads can alter the operands of the instruction it is called on.

   1. Two operands of any sort may be interchanged, if they are in a
   commutative instruction.
   This happens only if find_reloads thinks the instruction will compile
   better that way.

   2. Pseudo-registers that are equivalent to constants are replaced
   with those constants if they are not in hard registers.

1 happens every time find_reloads is called.
2 happens only when REPLACE is 1, which is only when
actually doing the reloads, not when just counting them.

Using a reload register for several reloads in one insn:

When an insn has reloads, it is considered as having three parts:
the input reloads, the insn itself after reloading, and the output reloads.
Reloads of values used in memory addresses are often needed for only one part.

When this is so, reload_when_needed records which part needs the reload.
Two reloads for different parts of the insn can share the same reload
register.

When a reload is used for addresses in multiple parts, or when it is
an ordinary operand, it is classified as RELOAD_OTHER, and cannot share
a register with any other reload.  */

#define REG_OK_STRICT

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tm_p.h"
#include "insn-config.h"
#include "expr.h"
#include "optabs.h"
#include "recog.h"
#include "reload.h"
#include "regs.h"
#include "addresses.h"
#include "hard-reg-set.h"
#include "flags.h"
#include "real.h"
#include "output.h"
#include "function.h"
#include "toplev.h"
#include "params.h"
#include "target.h"

/* True if X is a constant that can be forced into the constant pool.  */
#define CONST_POOL_OK_P(X)			\
  (CONSTANT_P (X)				\
   && GET_CODE (X) != HIGH			\
   && !targetm.cannot_force_const_mem (X))

/* True if C is a non-empty register class that has too few registers
   to be safely used as a reload target class.  */
#define SMALL_REGISTER_CLASS_P(C) \
  (reg_class_size [(C)] == 1 \
   || (reg_class_size [(C)] >= 1 && CLASS_LIKELY_SPILLED_P (C)))


/* All reloads of the current insn are recorded here.  See reload.h for
   comments.  */
int n_reloads;
struct reload rld[MAX_RELOADS];

/* All the "earlyclobber" operands of the current insn
   are recorded here.  */
int n_earlyclobbers;
rtx reload_earlyclobbers[MAX_RECOG_OPERANDS];

int reload_n_operands;

/* Replacing reloads.

   If `replace_reloads' is nonzero, then as each reload is recorded
   an entry is made for it in the table `replacements'.
   Then later `subst_reloads' can look through that table and
   perform all the replacements needed.  */

/* Nonzero means record the places to replace.  */
static int replace_reloads;

/* Each replacement is recorded with a structure like this.  */
struct replacement
{
  rtx *where;			/* Location to store in */
  rtx *subreg_loc;		/* Location of SUBREG if WHERE is inside
				   a SUBREG; 0 otherwise.  */
  int what;			/* which reload this is for */
  enum machine_mode mode;	/* mode it must have */
};

static struct replacement replacements[MAX_RECOG_OPERANDS * ((MAX_REGS_PER_ADDRESS * 2) + 1)];

/* Number of replacements currently recorded.  */
static int n_replacements;

/* Used to track what is modified by an operand.  */
struct decomposition
{
  int reg_flag;		/* Nonzero if referencing a register.  */
  int safe;		/* Nonzero if this can't conflict with anything.  */
  rtx base;		/* Base address for MEM.  */
  HOST_WIDE_INT start;	/* Starting offset or register number.  */
  HOST_WIDE_INT end;	/* Ending offset or register number.  */
};

#ifdef SECONDARY_MEMORY_NEEDED

/* Save MEMs needed to copy from one class of registers to another.  One MEM
   is used per mode, but normally only one or two modes are ever used.

   We keep two versions, before and after register elimination.  The one
   after register elimination is record separately for each operand.  This
   is done in case the address is not valid to be sure that we separately
   reload each.  */

static rtx secondary_memlocs[NUM_MACHINE_MODES];
static rtx secondary_memlocs_elim[NUM_MACHINE_MODES][MAX_RECOG_OPERANDS];
static int secondary_memlocs_elim_used = 0;
#endif

/* The instruction we are doing reloads for;
   so we can test whether a register dies in it.  */
static rtx this_insn;

/* Nonzero if this instruction is a user-specified asm with operands.  */
static int this_insn_is_asm;

/* If hard_regs_live_known is nonzero,
   we can tell which hard regs are currently live,
   at least enough to succeed in choosing dummy reloads.  */
static int hard_regs_live_known;

/* Indexed by hard reg number,
   element is nonnegative if hard reg has been spilled.
   This vector is passed to `find_reloads' as an argument
   and is not changed here.  */
static short *static_reload_reg_p;

/* Set to 1 in subst_reg_equivs if it changes anything.  */
static int subst_reg_equivs_changed;

/* On return from push_reload, holds the reload-number for the OUT
   operand, which can be different for that from the input operand.  */
static int output_reloadnum;

  /* Compare two RTX's.  */
#define MATCHES(x, y) \
 (x == y || (x != 0 && (REG_P (x)				\
			? REG_P (y) && REGNO (x) == REGNO (y)	\
			: rtx_equal_p (x, y) && ! side_effects_p (x))))

  /* Indicates if two reloads purposes are for similar enough things that we
     can merge their reloads.  */
#define MERGABLE_RELOADS(when1, when2, op1, op2) \
  ((when1) == RELOAD_OTHER || (when2) == RELOAD_OTHER	\
   || ((when1) == (when2) && (op1) == (op2))		\
   || ((when1) == RELOAD_FOR_INPUT && (when2) == RELOAD_FOR_INPUT) \
   || ((when1) == RELOAD_FOR_OPERAND_ADDRESS		\
       && (when2) == RELOAD_FOR_OPERAND_ADDRESS)	\
   || ((when1) == RELOAD_FOR_OTHER_ADDRESS		\
       && (when2) == RELOAD_FOR_OTHER_ADDRESS))

  /* Nonzero if these two reload purposes produce RELOAD_OTHER when merged.  */
#define MERGE_TO_OTHER(when1, when2, op1, op2) \
  ((when1) != (when2)					\
   || ! ((op1) == (op2)					\
	 || (when1) == RELOAD_FOR_INPUT			\
	 || (when1) == RELOAD_FOR_OPERAND_ADDRESS	\
	 || (when1) == RELOAD_FOR_OTHER_ADDRESS))

  /* If we are going to reload an address, compute the reload type to
     use.  */
#define ADDR_TYPE(type)					\
  ((type) == RELOAD_FOR_INPUT_ADDRESS			\
   ? RELOAD_FOR_INPADDR_ADDRESS				\
   : ((type) == RELOAD_FOR_OUTPUT_ADDRESS		\
      ? RELOAD_FOR_OUTADDR_ADDRESS			\
      : (type)))

static int push_secondary_reload (int, rtx, int, int, enum reg_class,
				  enum machine_mode, enum reload_type,
				  enum insn_code *, secondary_reload_info *);
static enum reg_class find_valid_class (enum machine_mode, enum machine_mode,
					int, unsigned int);
static int reload_inner_reg_of_subreg (rtx, enum machine_mode, int);
static void push_replacement (rtx *, int, enum machine_mode);
static void dup_replacements (rtx *, rtx *);
static void combine_reloads (void);
static int find_reusable_reload (rtx *, rtx, enum reg_class,
				 enum reload_type, int, int);
static rtx find_dummy_reload (rtx, rtx, rtx *, rtx *, enum machine_mode,
			      enum machine_mode, enum reg_class, int, int);
static int hard_reg_set_here_p (unsigned int, unsigned int, rtx);
static struct decomposition decompose (rtx);
static int immune_p (rtx, rtx, struct decomposition);
static int alternative_allows_memconst (const char *, int);
static rtx find_reloads_toplev (rtx, int, enum reload_type, int, int, rtx,
				int *);
static rtx make_memloc (rtx, int);
static int maybe_memory_address_p (enum machine_mode, rtx, rtx *);
static int find_reloads_address (enum machine_mode, rtx *, rtx, rtx *,
				 int, enum reload_type, int, rtx);
static rtx subst_reg_equivs (rtx, rtx);
static rtx subst_indexed_address (rtx);
static void update_auto_inc_notes (rtx, int, int);
static int find_reloads_address_1 (enum machine_mode, rtx, int,
				   enum rtx_code, enum rtx_code, rtx *,
				   int, enum reload_type,int, rtx);
static void find_reloads_address_part (rtx, rtx *, enum reg_class,
				       enum machine_mode, int,
				       enum reload_type, int);
static rtx find_reloads_subreg_address (rtx, int, int, enum reload_type,
					int, rtx);
static void copy_replacements_1 (rtx *, rtx *, int);
static int find_inc_amount (rtx, rtx);
static int refers_to_mem_for_reload_p (rtx);
static int refers_to_regno_for_reload_p (unsigned int, unsigned int,
					 rtx, rtx *);

/* Add NEW to reg_equiv_alt_mem_list[REGNO] if it's not present in the
   list yet.  */

static void
push_reg_equiv_alt_mem (int regno, rtx mem)
{
  rtx it;

  for (it = reg_equiv_alt_mem_list [regno]; it; it = XEXP (it, 1))
    if (rtx_equal_p (XEXP (it, 0), mem))
      return;

  reg_equiv_alt_mem_list [regno]
    = alloc_EXPR_LIST (REG_EQUIV, mem,
		       reg_equiv_alt_mem_list [regno]);
}

/* Determine if any secondary reloads are needed for loading (if IN_P is
   nonzero) or storing (if IN_P is zero) X to or from a reload register of
   register class RELOAD_CLASS in mode RELOAD_MODE.  If secondary reloads
   are needed, push them.

   Return the reload number of the secondary reload we made, or -1 if
   we didn't need one.  *PICODE is set to the insn_code to use if we do
   need a secondary reload.  */

static int
push_secondary_reload (int in_p, rtx x, int opnum, int optional,
		       enum reg_class reload_class,
		       enum machine_mode reload_mode, enum reload_type type,
		       enum insn_code *picode, secondary_reload_info *prev_sri)
{
  enum reg_class class = NO_REGS;
  enum reg_class scratch_class;
  enum machine_mode mode = reload_mode;
  enum insn_code icode = CODE_FOR_nothing;
  enum insn_code t_icode = CODE_FOR_nothing;
  enum reload_type secondary_type;
  int s_reload, t_reload = -1;
  const char *scratch_constraint;
  char letter;
  secondary_reload_info sri;

  if (type == RELOAD_FOR_INPUT_ADDRESS
      || type == RELOAD_FOR_OUTPUT_ADDRESS
      || type == RELOAD_FOR_INPADDR_ADDRESS
      || type == RELOAD_FOR_OUTADDR_ADDRESS)
    secondary_type = type;
  else
    secondary_type = in_p ? RELOAD_FOR_INPUT_ADDRESS : RELOAD_FOR_OUTPUT_ADDRESS;

  *picode = CODE_FOR_nothing;

  /* If X is a paradoxical SUBREG, use the inner value to determine both the
     mode and object being reloaded.  */
  if (GET_CODE (x) == SUBREG
      && (GET_MODE_SIZE (GET_MODE (x))
	  > GET_MODE_SIZE (GET_MODE (SUBREG_REG (x)))))
    {
      x = SUBREG_REG (x);
      reload_mode = GET_MODE (x);
    }

  /* If X is a pseudo-register that has an equivalent MEM (actually, if it
     is still a pseudo-register by now, it *must* have an equivalent MEM
     but we don't want to assume that), use that equivalent when seeing if
     a secondary reload is needed since whether or not a reload is needed
     might be sensitive to the form of the MEM.  */

  if (REG_P (x) && REGNO (x) >= FIRST_PSEUDO_REGISTER
      && reg_equiv_mem[REGNO (x)] != 0)
    x = reg_equiv_mem[REGNO (x)];

  sri.icode = CODE_FOR_nothing;
  sri.prev_sri = prev_sri;
  class = targetm.secondary_reload (in_p, x, reload_class, reload_mode, &sri);
  icode = sri.icode;

  /* If we don't need any secondary registers, done.  */
  if (class == NO_REGS && icode == CODE_FOR_nothing)
    return -1;

  if (class != NO_REGS)
    t_reload = push_secondary_reload (in_p, x, opnum, optional, class,
				      reload_mode, type, &t_icode, &sri);

  /* If we will be using an insn, the secondary reload is for a
     scratch register.  */

  if (icode != CODE_FOR_nothing)
    {
      /* If IN_P is nonzero, the reload register will be the output in
	 operand 0.  If IN_P is zero, the reload register will be the input
	 in operand 1.  Outputs should have an initial "=", which we must
	 skip.  */

      /* ??? It would be useful to be able to handle only two, or more than
	 three, operands, but for now we can only handle the case of having
	 exactly three: output, input and one temp/scratch.  */
      gcc_assert (insn_data[(int) icode].n_operands == 3);

      /* ??? We currently have no way to represent a reload that needs
	 an icode to reload from an intermediate tertiary reload register.
	 We should probably have a new field in struct reload to tag a
	 chain of scratch operand reloads onto.   */
      gcc_assert (class == NO_REGS);

      scratch_constraint = insn_data[(int) icode].operand[2].constraint;
      gcc_assert (*scratch_constraint == '=');
      scratch_constraint++;
      if (*scratch_constraint == '&')
	scratch_constraint++;
      letter = *scratch_constraint;
      scratch_class = (letter == 'r' ? GENERAL_REGS
		       : REG_CLASS_FROM_CONSTRAINT ((unsigned char) letter,
						   scratch_constraint));

      class = scratch_class;
      mode = insn_data[(int) icode].operand[2].mode;
    }

  /* This case isn't valid, so fail.  Reload is allowed to use the same
     register for RELOAD_FOR_INPUT_ADDRESS and RELOAD_FOR_INPUT reloads, but
     in the case of a secondary register, we actually need two different
     registers for correct code.  We fail here to prevent the possibility of
     silently generating incorrect code later.

     The convention is that secondary input reloads are valid only if the
     secondary_class is different from class.  If you have such a case, you
     can not use secondary reloads, you must work around the problem some
     other way.

     Allow this when a reload_in/out pattern is being used.  I.e. assume
     that the generated code handles this case.  */

  gcc_assert (!in_p || class != reload_class || icode != CODE_FOR_nothing
	      || t_icode != CODE_FOR_nothing);

  /* See if we can reuse an existing secondary reload.  */
  for (s_reload = 0; s_reload < n_reloads; s_reload++)
    if (rld[s_reload].secondary_p
	&& (reg_class_subset_p (class, rld[s_reload].class)
	    || reg_class_subset_p (rld[s_reload].class, class))
	&& ((in_p && rld[s_reload].inmode == mode)
	    || (! in_p && rld[s_reload].outmode == mode))
	&& ((in_p && rld[s_reload].secondary_in_reload == t_reload)
	    || (! in_p && rld[s_reload].secondary_out_reload == t_reload))
	&& ((in_p && rld[s_reload].secondary_in_icode == t_icode)
	    || (! in_p && rld[s_reload].secondary_out_icode == t_icode))
	&& (SMALL_REGISTER_CLASS_P (class) || SMALL_REGISTER_CLASSES)
	&& MERGABLE_RELOADS (secondary_type, rld[s_reload].when_needed,
			     opnum, rld[s_reload].opnum))
      {
	if (in_p)
	  rld[s_reload].inmode = mode;
	if (! in_p)
	  rld[s_reload].outmode = mode;

	if (reg_class_subset_p (class, rld[s_reload].class))
	  rld[s_reload].class = class;

	rld[s_reload].opnum = MIN (rld[s_reload].opnum, opnum);
	rld[s_reload].optional &= optional;
	rld[s_reload].secondary_p = 1;
	if (MERGE_TO_OTHER (secondary_type, rld[s_reload].when_needed,
			    opnum, rld[s_reload].opnum))
	  rld[s_reload].when_needed = RELOAD_OTHER;
      }

  if (s_reload == n_reloads)
    {
#ifdef SECONDARY_MEMORY_NEEDED
      /* If we need a memory location to copy between the two reload regs,
	 set it up now.  Note that we do the input case before making
	 the reload and the output case after.  This is due to the
	 way reloads are output.  */

      if (in_p && icode == CODE_FOR_nothing
	  && SECONDARY_MEMORY_NEEDED (class, reload_class, mode))
	{
	  get_secondary_mem (x, reload_mode, opnum, type);

	  /* We may have just added new reloads.  Make sure we add
	     the new reload at the end.  */
	  s_reload = n_reloads;
	}
#endif

      /* We need to make a new secondary reload for this register class.  */
      rld[s_reload].in = rld[s_reload].out = 0;
      rld[s_reload].class = class;

      rld[s_reload].inmode = in_p ? mode : VOIDmode;
      rld[s_reload].outmode = ! in_p ? mode : VOIDmode;
      rld[s_reload].reg_rtx = 0;
      rld[s_reload].optional = optional;
      rld[s_reload].inc = 0;
      /* Maybe we could combine these, but it seems too tricky.  */
      rld[s_reload].nocombine = 1;
      rld[s_reload].in_reg = 0;
      rld[s_reload].out_reg = 0;
      rld[s_reload].opnum = opnum;
      rld[s_reload].when_needed = secondary_type;
      rld[s_reload].secondary_in_reload = in_p ? t_reload : -1;
      rld[s_reload].secondary_out_reload = ! in_p ? t_reload : -1;
      rld[s_reload].secondary_in_icode = in_p ? t_icode : CODE_FOR_nothing;
      rld[s_reload].secondary_out_icode
	= ! in_p ? t_icode : CODE_FOR_nothing;
      rld[s_reload].secondary_p = 1;

      n_reloads++;

#ifdef SECONDARY_MEMORY_NEEDED
      if (! in_p && icode == CODE_FOR_nothing
	  && SECONDARY_MEMORY_NEEDED (reload_class, class, mode))
	get_secondary_mem (x, mode, opnum, type);
#endif
    }

  *picode = icode;
  return s_reload;
}

/* If a secondary reload is needed, return its class.  If both an intermediate
   register and a scratch register is needed, we return the class of the
   intermediate register.  */
enum reg_class
secondary_reload_class (bool in_p, enum reg_class class,
			enum machine_mode mode, rtx x)
{
  enum insn_code icode;
  secondary_reload_info sri;

  sri.icode = CODE_FOR_nothing;
  sri.prev_sri = NULL;
  class = targetm.secondary_reload (in_p, x, class, mode, &sri);
  icode = sri.icode;

  /* If there are no secondary reloads at all, we return NO_REGS.
     If an intermediate register is needed, we return its class.  */
  if (icode == CODE_FOR_nothing || class != NO_REGS)
    return class;

  /* No intermediate register is needed, but we have a special reload
     pattern, which we assume for now needs a scratch register.  */
  return scratch_reload_class (icode);
}

/* ICODE is the insn_code of a reload pattern.  Check that it has exactly
   three operands, verify that operand 2 is an output operand, and return
   its register class.
   ??? We'd like to be able to handle any pattern with at least 2 operands,
   for zero or more scratch registers, but that needs more infrastructure.  */
enum reg_class
scratch_reload_class (enum insn_code icode)
{
  const char *scratch_constraint;
  char scratch_letter;
  enum reg_class class;

  gcc_assert (insn_data[(int) icode].n_operands == 3);
  scratch_constraint = insn_data[(int) icode].operand[2].constraint;
  gcc_assert (*scratch_constraint == '=');
  scratch_constraint++;
  if (*scratch_constraint == '&')
    scratch_constraint++;
  scratch_letter = *scratch_constraint;
  if (scratch_letter == 'r')
    return GENERAL_REGS;
  class = REG_CLASS_FROM_CONSTRAINT ((unsigned char) scratch_letter,
				     scratch_constraint);
  gcc_assert (class != NO_REGS);
  return class;
}

#ifdef SECONDARY_MEMORY_NEEDED

/* Return a memory location that will be used to copy X in mode MODE.
   If we haven't already made a location for this mode in this insn,
   call find_reloads_address on the location being returned.  */

rtx
get_secondary_mem (rtx x ATTRIBUTE_UNUSED, enum machine_mode mode,
		   int opnum, enum reload_type type)
{
  rtx loc;
  int mem_valid;

  /* By default, if MODE is narrower than a word, widen it to a word.
     This is required because most machines that require these memory
     locations do not support short load and stores from all registers
     (e.g., FP registers).  */

#ifdef SECONDARY_MEMORY_NEEDED_MODE
  mode = SECONDARY_MEMORY_NEEDED_MODE (mode);
#else
  if (GET_MODE_BITSIZE (mode) < BITS_PER_WORD && INTEGRAL_MODE_P (mode))
    mode = mode_for_size (BITS_PER_WORD, GET_MODE_CLASS (mode), 0);
#endif

  /* If we already have made a MEM for this operand in MODE, return it.  */
  if (secondary_memlocs_elim[(int) mode][opnum] != 0)
    return secondary_memlocs_elim[(int) mode][opnum];

  /* If this is the first time we've tried to get a MEM for this mode,
     allocate a new one.  `something_changed' in reload will get set
     by noticing that the frame size has changed.  */

  if (secondary_memlocs[(int) mode] == 0)
    {
#ifdef SECONDARY_MEMORY_NEEDED_RTX
      secondary_memlocs[(int) mode] = SECONDARY_MEMORY_NEEDED_RTX (mode);
#else
      secondary_memlocs[(int) mode]
	= assign_stack_local (mode, GET_MODE_SIZE (mode), 0);
#endif
    }

  /* Get a version of the address doing any eliminations needed.  If that
     didn't give us a new MEM, make a new one if it isn't valid.  */

  loc = eliminate_regs (secondary_memlocs[(int) mode], VOIDmode, NULL_RTX);
  mem_valid = strict_memory_address_p (mode, XEXP (loc, 0));

  if (! mem_valid && loc == secondary_memlocs[(int) mode])
    loc = copy_rtx (loc);

  /* The only time the call below will do anything is if the stack
     offset is too large.  In that case IND_LEVELS doesn't matter, so we
     can just pass a zero.  Adjust the type to be the address of the
     corresponding object.  If the address was valid, save the eliminated
     address.  If it wasn't valid, we need to make a reload each time, so
     don't save it.  */

  if (! mem_valid)
    {
      type =  (type == RELOAD_FOR_INPUT ? RELOAD_FOR_INPUT_ADDRESS
	       : type == RELOAD_FOR_OUTPUT ? RELOAD_FOR_OUTPUT_ADDRESS
	       : RELOAD_OTHER);

      find_reloads_address (mode, &loc, XEXP (loc, 0), &XEXP (loc, 0),
			    opnum, type, 0, 0);
    }

  secondary_memlocs_elim[(int) mode][opnum] = loc;
  if (secondary_memlocs_elim_used <= (int)mode)
    secondary_memlocs_elim_used = (int)mode + 1;
  return loc;
}

/* Clear any secondary memory locations we've made.  */

void
clear_secondary_mem (void)
{
  memset (secondary_memlocs, 0, sizeof secondary_memlocs);
}
#endif /* SECONDARY_MEMORY_NEEDED */


/* Find the largest class which has at least one register valid in
   mode INNER, and which for every such register, that register number
   plus N is also valid in OUTER (if in range) and is cheap to move
   into REGNO.  Such a class must exist.  */

static enum reg_class
find_valid_class (enum machine_mode outer ATTRIBUTE_UNUSED,
		  enum machine_mode inner ATTRIBUTE_UNUSED, int n,
		  unsigned int dest_regno ATTRIBUTE_UNUSED)
{
  int best_cost = -1;
  int class;
  int regno;
  enum reg_class best_class = NO_REGS;
  enum reg_class dest_class ATTRIBUTE_UNUSED = REGNO_REG_CLASS (dest_regno);
  unsigned int best_size = 0;
  int cost;

  for (class = 1; class < N_REG_CLASSES; class++)
    {
      int bad = 0;
      int good = 0;
      for (regno = 0; regno < FIRST_PSEUDO_REGISTER - n && ! bad; regno++)
	if (TEST_HARD_REG_BIT (reg_class_contents[class], regno))
	  {
	    if (HARD_REGNO_MODE_OK (regno, inner))
	      {
		good = 1;
		if (! TEST_HARD_REG_BIT (reg_class_contents[class], regno + n)
		    || ! HARD_REGNO_MODE_OK (regno + n, outer))
		  bad = 1;
	      }
	  }

      if (bad || !good)
	continue;
      cost = REGISTER_MOVE_COST (outer, class, dest_class);

      if ((reg_class_size[class] > best_size
	   && (best_cost < 0 || best_cost >= cost))
	  || best_cost > cost)
	{
	  best_class = class;
	  best_size = reg_class_size[class];
	  best_cost = REGISTER_MOVE_COST (outer, class, dest_class);
	}
    }

  gcc_assert (best_size != 0);

  return best_class;
}

/* Return the number of a previously made reload that can be combined with
   a new one, or n_reloads if none of the existing reloads can be used.
   OUT, CLASS, TYPE and OPNUM are the same arguments as passed to
   push_reload, they determine the kind of the new reload that we try to
   combine.  P_IN points to the corresponding value of IN, which can be
   modified by this function.
   DONT_SHARE is nonzero if we can't share any input-only reload for IN.  */

static int
find_reusable_reload (rtx *p_in, rtx out, enum reg_class class,
		      enum reload_type type, int opnum, int dont_share)
{
  rtx in = *p_in;
  int i;
  /* We can't merge two reloads if the output of either one is
     earlyclobbered.  */

  if (earlyclobber_operand_p (out))
    return n_reloads;

  /* We can use an existing reload if the class is right
     and at least one of IN and OUT is a match
     and the other is at worst neutral.
     (A zero compared against anything is neutral.)

     If SMALL_REGISTER_CLASSES, don't use existing reloads unless they are
     for the same thing since that can cause us to need more reload registers
     than we otherwise would.  */

  for (i = 0; i < n_reloads; i++)
    if ((reg_class_subset_p (class, rld[i].class)
	 || reg_class_subset_p (rld[i].class, class))
	/* If the existing reload has a register, it must fit our class.  */
	&& (rld[i].reg_rtx == 0
	    || TEST_HARD_REG_BIT (reg_class_contents[(int) class],
				  true_regnum (rld[i].reg_rtx)))
	&& ((in != 0 && MATCHES (rld[i].in, in) && ! dont_share
	     && (out == 0 || rld[i].out == 0 || MATCHES (rld[i].out, out)))
	    || (out != 0 && MATCHES (rld[i].out, out)
		&& (in == 0 || rld[i].in == 0 || MATCHES (rld[i].in, in))))
	&& (rld[i].out == 0 || ! earlyclobber_operand_p (rld[i].out))
	&& (SMALL_REGISTER_CLASS_P (class) || SMALL_REGISTER_CLASSES)
	&& MERGABLE_RELOADS (type, rld[i].when_needed, opnum, rld[i].opnum))
      return i;

  /* Reloading a plain reg for input can match a reload to postincrement
     that reg, since the postincrement's value is the right value.
     Likewise, it can match a preincrement reload, since we regard
     the preincrementation as happening before any ref in this insn
     to that register.  */
  for (i = 0; i < n_reloads; i++)
    if ((reg_class_subset_p (class, rld[i].class)
	 || reg_class_subset_p (rld[i].class, class))
	/* If the existing reload has a register, it must fit our
	   class.  */
	&& (rld[i].reg_rtx == 0
	    || TEST_HARD_REG_BIT (reg_class_contents[(int) class],
				  true_regnum (rld[i].reg_rtx)))
	&& out == 0 && rld[i].out == 0 && rld[i].in != 0
	&& ((REG_P (in)
	     && GET_RTX_CLASS (GET_CODE (rld[i].in)) == RTX_AUTOINC
	     && MATCHES (XEXP (rld[i].in, 0), in))
	    || (REG_P (rld[i].in)
		&& GET_RTX_CLASS (GET_CODE (in)) == RTX_AUTOINC
		&& MATCHES (XEXP (in, 0), rld[i].in)))
	&& (rld[i].out == 0 || ! earlyclobber_operand_p (rld[i].out))
	&& (SMALL_REGISTER_CLASS_P (class) || SMALL_REGISTER_CLASSES)
	&& MERGABLE_RELOADS (type, rld[i].when_needed,
			     opnum, rld[i].opnum))
      {
	/* Make sure reload_in ultimately has the increment,
	   not the plain register.  */
	if (REG_P (in))
	  *p_in = rld[i].in;
	return i;
      }
  return n_reloads;
}

/* Return nonzero if X is a SUBREG which will require reloading of its
   SUBREG_REG expression.  */

static int
reload_inner_reg_of_subreg (rtx x, enum machine_mode mode, int output)
{
  rtx inner;

  /* Only SUBREGs are problematical.  */
  if (GET_CODE (x) != SUBREG)
    return 0;

  inner = SUBREG_REG (x);

  /* If INNER is a constant or PLUS, then INNER must be reloaded.  */
  if (CONSTANT_P (inner) || GET_CODE (inner) == PLUS)
    return 1;

  /* If INNER is not a hard register, then INNER will not need to
     be reloaded.  */
  if (!REG_P (inner)
      || REGNO (inner) >= FIRST_PSEUDO_REGISTER)
    return 0;

  /* If INNER is not ok for MODE, then INNER will need reloading.  */
  if (! HARD_REGNO_MODE_OK (subreg_regno (x), mode))
    return 1;

  /* If the outer part is a word or smaller, INNER larger than a
     word and the number of regs for INNER is not the same as the
     number of words in INNER, then INNER will need reloading.  */
  return (GET_MODE_SIZE (mode) <= UNITS_PER_WORD
	  && output
	  && GET_MODE_SIZE (GET_MODE (inner)) > UNITS_PER_WORD
	  && ((GET_MODE_SIZE (GET_MODE (inner)) / UNITS_PER_WORD)
	      != (int) hard_regno_nregs[REGNO (inner)][GET_MODE (inner)]));
}

/* Return nonzero if IN can be reloaded into REGNO with mode MODE without
   requiring an extra reload register.  The caller has already found that
   IN contains some reference to REGNO, so check that we can produce the
   new value in a single step.  E.g. if we have
   (set (reg r13) (plus (reg r13) (const int 1))), and there is an
   instruction that adds one to a register, this should succeed.
   However, if we have something like
   (set (reg r13) (plus (reg r13) (const int 999))), and the constant 999
   needs to be loaded into a register first, we need a separate reload
   register.
   Such PLUS reloads are generated by find_reload_address_part.
   The out-of-range PLUS expressions are usually introduced in the instruction
   patterns by register elimination and substituting pseudos without a home
   by their function-invariant equivalences.  */
static int
can_reload_into (rtx in, int regno, enum machine_mode mode)
{
  rtx dst, test_insn;
  int r = 0;
  struct recog_data save_recog_data;

  /* For matching constraints, we often get notional input reloads where
     we want to use the original register as the reload register.  I.e.
     technically this is a non-optional input-output reload, but IN is
     already a valid register, and has been chosen as the reload register.
     Speed this up, since it trivially works.  */
  if (REG_P (in))
    return 1;

  /* To test MEMs properly, we'd have to take into account all the reloads
     that are already scheduled, which can become quite complicated.
     And since we've already handled address reloads for this MEM, it
     should always succeed anyway.  */
  if (MEM_P (in))
    return 1;

  /* If we can make a simple SET insn that does the job, everything should
     be fine.  */
  dst =  gen_rtx_REG (mode, regno);
  test_insn = make_insn_raw (gen_rtx_SET (VOIDmode, dst, in));
  save_recog_data = recog_data;
  if (recog_memoized (test_insn) >= 0)
    {
      extract_insn (test_insn);
      r = constrain_operands (1);
    }
  recog_data = save_recog_data;
  return r;
}

/* Record one reload that needs to be performed.
   IN is an rtx saying where the data are to be found before this instruction.
   OUT says where they must be stored after the instruction.
   (IN is zero for data not read, and OUT is zero for data not written.)
   INLOC and OUTLOC point to the places in the instructions where
   IN and OUT were found.
   If IN and OUT are both nonzero, it means the same register must be used
   to reload both IN and OUT.

   CLASS is a register class required for the reloaded data.
   INMODE is the machine mode that the instruction requires
   for the reg that replaces IN and OUTMODE is likewise for OUT.

   If IN is zero, then OUT's location and mode should be passed as
   INLOC and INMODE.

   STRICT_LOW is the 1 if there is a containing STRICT_LOW_PART rtx.

   OPTIONAL nonzero means this reload does not need to be performed:
   it can be discarded if that is more convenient.

   OPNUM and TYPE say what the purpose of this reload is.

   The return value is the reload-number for this reload.

   If both IN and OUT are nonzero, in some rare cases we might
   want to make two separate reloads.  (Actually we never do this now.)
   Therefore, the reload-number for OUT is stored in
   output_reloadnum when we return; the return value applies to IN.
   Usually (presently always), when IN and OUT are nonzero,
   the two reload-numbers are equal, but the caller should be careful to
   distinguish them.  */

int
push_reload (rtx in, rtx out, rtx *inloc, rtx *outloc,
	     enum reg_class class, enum machine_mode inmode,
	     enum machine_mode outmode, int strict_low, int optional,
	     int opnum, enum reload_type type)
{
  int i;
  int dont_share = 0;
  int dont_remove_subreg = 0;
  rtx *in_subreg_loc = 0, *out_subreg_loc = 0;
  int secondary_in_reload = -1, secondary_out_reload = -1;
  enum insn_code secondary_in_icode = CODE_FOR_nothing;
  enum insn_code secondary_out_icode = CODE_FOR_nothing;

  /* INMODE and/or OUTMODE could be VOIDmode if no mode
     has been specified for the operand.  In that case,
     use the operand's mode as the mode to reload.  */
  if (inmode == VOIDmode && in != 0)
    inmode = GET_MODE (in);
  if (outmode == VOIDmode && out != 0)
    outmode = GET_MODE (out);

  /* If IN is a pseudo register everywhere-equivalent to a constant, and
     it is not in a hard register, reload straight from the constant,
     since we want to get rid of such pseudo registers.
     Often this is done earlier, but not always in find_reloads_address.  */
  if (in != 0 && REG_P (in))
    {
      int regno = REGNO (in);

      if (regno >= FIRST_PSEUDO_REGISTER && reg_renumber[regno] < 0
	  && reg_equiv_constant[regno] != 0)
	in = reg_equiv_constant[regno];
    }

  /* Likewise for OUT.  Of course, OUT will never be equivalent to
     an actual constant, but it might be equivalent to a memory location
     (in the case of a parameter).  */
  if (out != 0 && REG_P (out))
    {
      int regno = REGNO (out);

      if (regno >= FIRST_PSEUDO_REGISTER && reg_renumber[regno] < 0
	  && reg_equiv_constant[regno] != 0)
	out = reg_equiv_constant[regno];
    }

  /* If we have a read-write operand with an address side-effect,
     change either IN or OUT so the side-effect happens only once.  */
  if (in != 0 && out != 0 && MEM_P (in) && rtx_equal_p (in, out))
    switch (GET_CODE (XEXP (in, 0)))
      {
      case POST_INC: case POST_DEC:   case POST_MODIFY:
	in = replace_equiv_address_nv (in, XEXP (XEXP (in, 0), 0));
	break;

      case PRE_INC: case PRE_DEC: case PRE_MODIFY:
	out = replace_equiv_address_nv (out, XEXP (XEXP (out, 0), 0));
	break;

      default:
	break;
      }

  /* If we are reloading a (SUBREG constant ...), really reload just the
     inside expression in its own mode.  Similarly for (SUBREG (PLUS ...)).
     If we have (SUBREG:M1 (MEM:M2 ...) ...) (or an inner REG that is still
     a pseudo and hence will become a MEM) with M1 wider than M2 and the
     register is a pseudo, also reload the inside expression.
     For machines that extend byte loads, do this for any SUBREG of a pseudo
     where both M1 and M2 are a word or smaller, M1 is wider than M2, and
     M2 is an integral mode that gets extended when loaded.
     Similar issue for (SUBREG:M1 (REG:M2 ...) ...) for a hard register R where
     either M1 is not valid for R or M2 is wider than a word but we only
     need one word to store an M2-sized quantity in R.
     (However, if OUT is nonzero, we need to reload the reg *and*
     the subreg, so do nothing here, and let following statement handle it.)

     Note that the case of (SUBREG (CONST_INT...)...) is handled elsewhere;
     we can't handle it here because CONST_INT does not indicate a mode.

     Similarly, we must reload the inside expression if we have a
     STRICT_LOW_PART (presumably, in == out in the cas).

     Also reload the inner expression if it does not require a secondary
     reload but the SUBREG does.

     Finally, reload the inner expression if it is a register that is in
     the class whose registers cannot be referenced in a different size
     and M1 is not the same size as M2.  If subreg_lowpart_p is false, we
     cannot reload just the inside since we might end up with the wrong
     register class.  But if it is inside a STRICT_LOW_PART, we have
     no choice, so we hope we do get the right register class there.  */

  if (in != 0 && GET_CODE (in) == SUBREG
      && (subreg_lowpart_p (in) || strict_low)
#ifdef CANNOT_CHANGE_MODE_CLASS
      && !CANNOT_CHANGE_MODE_CLASS (GET_MODE (SUBREG_REG (in)), inmode, class)
#endif
      && (CONSTANT_P (SUBREG_REG (in))
	  || GET_CODE (SUBREG_REG (in)) == PLUS
	  || strict_low
	  || (((REG_P (SUBREG_REG (in))
		&& REGNO (SUBREG_REG (in)) >= FIRST_PSEUDO_REGISTER)
	       || MEM_P (SUBREG_REG (in)))
	      && ((GET_MODE_SIZE (inmode)
		   > GET_MODE_SIZE (GET_MODE (SUBREG_REG (in))))
#ifdef LOAD_EXTEND_OP
		  || (GET_MODE_SIZE (inmode) <= UNITS_PER_WORD
		      && (GET_MODE_SIZE (GET_MODE (SUBREG_REG (in)))
			  <= UNITS_PER_WORD)
		      && (GET_MODE_SIZE (inmode)
			  > GET_MODE_SIZE (GET_MODE (SUBREG_REG (in))))
		      && INTEGRAL_MODE_P (GET_MODE (SUBREG_REG (in)))
		      && LOAD_EXTEND_OP (GET_MODE (SUBREG_REG (in))) != UNKNOWN)
#endif
#ifdef WORD_REGISTER_OPERATIONS
		  || ((GET_MODE_SIZE (inmode)
		       < GET_MODE_SIZE (GET_MODE (SUBREG_REG (in))))
		      && ((GET_MODE_SIZE (inmode) - 1) / UNITS_PER_WORD ==
			  ((GET_MODE_SIZE (GET_MODE (SUBREG_REG (in))) - 1)
			   / UNITS_PER_WORD)))
#endif
		  ))
	  || (REG_P (SUBREG_REG (in))
	      && REGNO (SUBREG_REG (in)) < FIRST_PSEUDO_REGISTER
	      /* The case where out is nonzero
		 is handled differently in the following statement.  */
	      && (out == 0 || subreg_lowpart_p (in))
	      && ((GET_MODE_SIZE (inmode) <= UNITS_PER_WORD
		   && (GET_MODE_SIZE (GET_MODE (SUBREG_REG (in)))
		       > UNITS_PER_WORD)
		   && ((GET_MODE_SIZE (GET_MODE (SUBREG_REG (in)))
			/ UNITS_PER_WORD)
		       != (int) hard_regno_nregs[REGNO (SUBREG_REG (in))]
						[GET_MODE (SUBREG_REG (in))]))
		  || ! HARD_REGNO_MODE_OK (subreg_regno (in), inmode)))
	  || (secondary_reload_class (1, class, inmode, in) != NO_REGS
	      && (secondary_reload_class (1, class, GET_MODE (SUBREG_REG (in)),
					  SUBREG_REG (in))
		  == NO_REGS))
#ifdef CANNOT_CHANGE_MODE_CLASS
	  || (REG_P (SUBREG_REG (in))
	      && REGNO (SUBREG_REG (in)) < FIRST_PSEUDO_REGISTER
	      && REG_CANNOT_CHANGE_MODE_P
	      (REGNO (SUBREG_REG (in)), GET_MODE (SUBREG_REG (in)), inmode))
#endif
	  ))
    {
      in_subreg_loc = inloc;
      inloc = &SUBREG_REG (in);
      in = *inloc;
#if ! defined (LOAD_EXTEND_OP) && ! defined (WORD_REGISTER_OPERATIONS)
      if (MEM_P (in))
	/* This is supposed to happen only for paradoxical subregs made by
	   combine.c.  (SUBREG (MEM)) isn't supposed to occur other ways.  */
	gcc_assert (GET_MODE_SIZE (GET_MODE (in)) <= GET_MODE_SIZE (inmode));
#endif
      inmode = GET_MODE (in);
    }

  /* Similar issue for (SUBREG:M1 (REG:M2 ...) ...) for a hard register R where
     either M1 is not valid for R or M2 is wider than a word but we only
     need one word to store an M2-sized quantity in R.

     However, we must reload the inner reg *as well as* the subreg in
     that case.  */

  /* Similar issue for (SUBREG constant ...) if it was not handled by the
     code above.  This can happen if SUBREG_BYTE != 0.  */

  if (in != 0 && reload_inner_reg_of_subreg (in, inmode, 0))
    {
      enum reg_class in_class = class;

      if (REG_P (SUBREG_REG (in)))
	in_class
	  = find_valid_class (inmode, GET_MODE (SUBREG_REG (in)),
			      subreg_regno_offset (REGNO (SUBREG_REG (in)),
						   GET_MODE (SUBREG_REG (in)),
						   SUBREG_BYTE (in),
						   GET_MODE (in)),
			      REGNO (SUBREG_REG (in)));

      /* This relies on the fact that emit_reload_insns outputs the
	 instructions for input reloads of type RELOAD_OTHER in the same
	 order as the reloads.  Thus if the outer reload is also of type
	 RELOAD_OTHER, we are guaranteed that this inner reload will be
	 output before the outer reload.  */
      push_reload (SUBREG_REG (in), NULL_RTX, &SUBREG_REG (in), (rtx *) 0,
		   in_class, VOIDmode, VOIDmode, 0, 0, opnum, type);
      dont_remove_subreg = 1;
    }

  /* Similarly for paradoxical and problematical SUBREGs on the output.
     Note that there is no reason we need worry about the previous value
     of SUBREG_REG (out); even if wider than out,
     storing in a subreg is entitled to clobber it all
     (except in the case of STRICT_LOW_PART,
     and in that case the constraint should label it input-output.)  */
  if (out != 0 && GET_CODE (out) == SUBREG
      && (subreg_lowpart_p (out) || strict_low)
#ifdef CANNOT_CHANGE_MODE_CLASS
      && !CANNOT_CHANGE_MODE_CLASS (GET_MODE (SUBREG_REG (out)), outmode, class)
#endif
      && (CONSTANT_P (SUBREG_REG (out))
	  || strict_low
	  || (((REG_P (SUBREG_REG (out))
		&& REGNO (SUBREG_REG (out)) >= FIRST_PSEUDO_REGISTER)
	       || MEM_P (SUBREG_REG (out)))
	      && ((GET_MODE_SIZE (outmode)
		   > GET_MODE_SIZE (GET_MODE (SUBREG_REG (out))))
#ifdef WORD_REGISTER_OPERATIONS
		  || ((GET_MODE_SIZE (outmode)
		       < GET_MODE_SIZE (GET_MODE (SUBREG_REG (out))))
		      && ((GET_MODE_SIZE (outmode) - 1) / UNITS_PER_WORD ==
			  ((GET_MODE_SIZE (GET_MODE (SUBREG_REG (out))) - 1)
			   / UNITS_PER_WORD)))
#endif
		  ))
	  || (REG_P (SUBREG_REG (out))
	      && REGNO (SUBREG_REG (out)) < FIRST_PSEUDO_REGISTER
	      && ((GET_MODE_SIZE (outmode) <= UNITS_PER_WORD
		   && (GET_MODE_SIZE (GET_MODE (SUBREG_REG (out)))
		       > UNITS_PER_WORD)
		   && ((GET_MODE_SIZE (GET_MODE (SUBREG_REG (out)))
			/ UNITS_PER_WORD)
		       != (int) hard_regno_nregs[REGNO (SUBREG_REG (out))]
						[GET_MODE (SUBREG_REG (out))]))
		  || ! HARD_REGNO_MODE_OK (subreg_regno (out), outmode)))
	  || (secondary_reload_class (0, class, outmode, out) != NO_REGS
	      && (secondary_reload_class (0, class, GET_MODE (SUBREG_REG (out)),
					  SUBREG_REG (out))
		  == NO_REGS))
#ifdef CANNOT_CHANGE_MODE_CLASS
	  || (REG_P (SUBREG_REG (out))
	      && REGNO (SUBREG_REG (out)) < FIRST_PSEUDO_REGISTER
	      && REG_CANNOT_CHANGE_MODE_P (REGNO (SUBREG_REG (out)),
					   GET_MODE (SUBREG_REG (out)),
					   outmode))
#endif
	  ))
    {
      out_subreg_loc = outloc;
      outloc = &SUBREG_REG (out);
      out = *outloc;
#if ! defined (LOAD_EXTEND_OP) && ! defined (WORD_REGISTER_OPERATIONS)
      gcc_assert (!MEM_P (out)
		  || GET_MODE_SIZE (GET_MODE (out))
		     <= GET_MODE_SIZE (outmode));
#endif
      outmode = GET_MODE (out);
    }

  /* Similar issue for (SUBREG:M1 (REG:M2 ...) ...) for a hard register R where
     either M1 is not valid for R or M2 is wider than a word but we only
     need one word to store an M2-sized quantity in R.

     However, we must reload the inner reg *as well as* the subreg in
     that case.  In this case, the inner reg is an in-out reload.  */

  if (out != 0 && reload_inner_reg_of_subreg (out, outmode, 1))
    {
      /* This relies on the fact that emit_reload_insns outputs the
	 instructions for output reloads of type RELOAD_OTHER in reverse
	 order of the reloads.  Thus if the outer reload is also of type
	 RELOAD_OTHER, we are guaranteed that this inner reload will be
	 output after the outer reload.  */
      dont_remove_subreg = 1;
      push_reload (SUBREG_REG (out), SUBREG_REG (out), &SUBREG_REG (out),
		   &SUBREG_REG (out),
		   find_valid_class (outmode, GET_MODE (SUBREG_REG (out)),
				     subreg_regno_offset (REGNO (SUBREG_REG (out)),
							  GET_MODE (SUBREG_REG (out)),
							  SUBREG_BYTE (out),
							  GET_MODE (out)),
				     REGNO (SUBREG_REG (out))),
		   VOIDmode, VOIDmode, 0, 0,
		   opnum, RELOAD_OTHER);
    }

  /* If IN appears in OUT, we can't share any input-only reload for IN.  */
  if (in != 0 && out != 0 && MEM_P (out)
      && (REG_P (in) || MEM_P (in) || GET_CODE (in) == PLUS)
      && reg_overlap_mentioned_for_reload_p (in, XEXP (out, 0)))
    dont_share = 1;

  /* If IN is a SUBREG of a hard register, make a new REG.  This
     simplifies some of the cases below.  */

  if (in != 0 && GET_CODE (in) == SUBREG && REG_P (SUBREG_REG (in))
      && REGNO (SUBREG_REG (in)) < FIRST_PSEUDO_REGISTER
      && ! dont_remove_subreg)
    in = gen_rtx_REG (GET_MODE (in), subreg_regno (in));

  /* Similarly for OUT.  */
  if (out != 0 && GET_CODE (out) == SUBREG
      && REG_P (SUBREG_REG (out))
      && REGNO (SUBREG_REG (out)) < FIRST_PSEUDO_REGISTER
      && ! dont_remove_subreg)
    out = gen_rtx_REG (GET_MODE (out), subreg_regno (out));

  /* Narrow down the class of register wanted if that is
     desirable on this machine for efficiency.  */
  {
    enum reg_class preferred_class = class;

    if (in != 0)
      preferred_class = PREFERRED_RELOAD_CLASS (in, class);

  /* Output reloads may need analogous treatment, different in detail.  */
#ifdef PREFERRED_OUTPUT_RELOAD_CLASS
    if (out != 0)
      preferred_class = PREFERRED_OUTPUT_RELOAD_CLASS (out, preferred_class);
#endif

    /* Discard what the target said if we cannot do it.  */
    if (preferred_class != NO_REGS
	|| (optional && type == RELOAD_FOR_OUTPUT))
      class = preferred_class;
  }

  /* Make sure we use a class that can handle the actual pseudo
     inside any subreg.  For example, on the 386, QImode regs
     can appear within SImode subregs.  Although GENERAL_REGS
     can handle SImode, QImode needs a smaller class.  */
#ifdef LIMIT_RELOAD_CLASS
  if (in_subreg_loc)
    class = LIMIT_RELOAD_CLASS (inmode, class);
  else if (in != 0 && GET_CODE (in) == SUBREG)
    class = LIMIT_RELOAD_CLASS (GET_MODE (SUBREG_REG (in)), class);

  if (out_subreg_loc)
    class = LIMIT_RELOAD_CLASS (outmode, class);
  if (out != 0 && GET_CODE (out) == SUBREG)
    class = LIMIT_RELOAD_CLASS (GET_MODE (SUBREG_REG (out)), class);
#endif

  /* Verify that this class is at least possible for the mode that
     is specified.  */
  if (this_insn_is_asm)
    {
      enum machine_mode mode;
      if (GET_MODE_SIZE (inmode) > GET_MODE_SIZE (outmode))
	mode = inmode;
      else
	mode = outmode;
      if (mode == VOIDmode)
	{
	  error_for_asm (this_insn, "cannot reload integer constant "
			 "operand in %<asm%>");
	  mode = word_mode;
	  if (in != 0)
	    inmode = word_mode;
	  if (out != 0)
	    outmode = word_mode;
	}
      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
	if (HARD_REGNO_MODE_OK (i, mode)
	    && TEST_HARD_REG_BIT (reg_class_contents[(int) class], i))
	  {
	    int nregs = hard_regno_nregs[i][mode];

	    int j;
	    for (j = 1; j < nregs; j++)
	      if (! TEST_HARD_REG_BIT (reg_class_contents[(int) class], i + j))
		break;
	    if (j == nregs)
	      break;
	  }
      if (i == FIRST_PSEUDO_REGISTER)
	{
	  error_for_asm (this_insn, "impossible register constraint "
			 "in %<asm%>");
	  /* Avoid further trouble with this insn.  */
	  PATTERN (this_insn) = gen_rtx_USE (VOIDmode, const0_rtx);
	  /* We used to continue here setting class to ALL_REGS, but it triggers
	     sanity check on i386 for:
	     void foo(long double d)
	     {
	       asm("" :: "a" (d));
	     }
	     Returning zero here ought to be safe as we take care in
	     find_reloads to not process the reloads when instruction was
	     replaced by USE.  */
	    
	  return 0;
	}
    }

  /* Optional output reloads are always OK even if we have no register class,
     since the function of these reloads is only to have spill_reg_store etc.
     set, so that the storing insn can be deleted later.  */
  gcc_assert (class != NO_REGS
	      || (optional != 0 && type == RELOAD_FOR_OUTPUT));

  i = find_reusable_reload (&in, out, class, type, opnum, dont_share);

  if (i == n_reloads)
    {
      /* See if we need a secondary reload register to move between CLASS
	 and IN or CLASS and OUT.  Get the icode and push any required reloads
	 needed for each of them if so.  */

      if (in != 0)
	secondary_in_reload
	  = push_secondary_reload (1, in, opnum, optional, class, inmode, type,
				   &secondary_in_icode, NULL);
      if (out != 0 && GET_CODE (out) != SCRATCH)
	secondary_out_reload
	  = push_secondary_reload (0, out, opnum, optional, class, outmode,
				   type, &secondary_out_icode, NULL);

      /* We found no existing reload suitable for re-use.
	 So add an additional reload.  */

#ifdef SECONDARY_MEMORY_NEEDED
      /* If a memory location is needed for the copy, make one.  */
      if (in != 0
	  && (REG_P (in)
	      || (GET_CODE (in) == SUBREG && REG_P (SUBREG_REG (in))))
	  && reg_or_subregno (in) < FIRST_PSEUDO_REGISTER
	  && SECONDARY_MEMORY_NEEDED (REGNO_REG_CLASS (reg_or_subregno (in)),
				      class, inmode))
	get_secondary_mem (in, inmode, opnum, type);
#endif

      i = n_reloads;
      rld[i].in = in;
      rld[i].out = out;
      rld[i].class = class;
      rld[i].inmode = inmode;
      rld[i].outmode = outmode;
      rld[i].reg_rtx = 0;
      rld[i].optional = optional;
      rld[i].inc = 0;
      rld[i].nocombine = 0;
      rld[i].in_reg = inloc ? *inloc : 0;
      rld[i].out_reg = outloc ? *outloc : 0;
      rld[i].opnum = opnum;
      rld[i].when_needed = type;
      rld[i].secondary_in_reload = secondary_in_reload;
      rld[i].secondary_out_reload = secondary_out_reload;
      rld[i].secondary_in_icode = secondary_in_icode;
      rld[i].secondary_out_icode = secondary_out_icode;
      rld[i].secondary_p = 0;

      n_reloads++;

#ifdef SECONDARY_MEMORY_NEEDED
      if (out != 0
          && (REG_P (out)
	      || (GET_CODE (out) == SUBREG && REG_P (SUBREG_REG (out))))
	  && reg_or_subregno (out) < FIRST_PSEUDO_REGISTER
	  && SECONDARY_MEMORY_NEEDED (class,
				      REGNO_REG_CLASS (reg_or_subregno (out)),
				      outmode))
	get_secondary_mem (out, outmode, opnum, type);
#endif
    }
  else
    {
      /* We are reusing an existing reload,
	 but we may have additional information for it.
	 For example, we may now have both IN and OUT
	 while the old one may have just one of them.  */

      /* The modes can be different.  If they are, we want to reload in
	 the larger mode, so that the value is valid for both modes.  */
      if (inmode != VOIDmode
	  && GET_MODE_SIZE (inmode) > GET_MODE_SIZE (rld[i].inmode))
	rld[i].inmode = inmode;
      if (outmode != VOIDmode
	  && GET_MODE_SIZE (outmode) > GET_MODE_SIZE (rld[i].outmode))
	rld[i].outmode = outmode;
      if (in != 0)
	{
	  rtx in_reg = inloc ? *inloc : 0;
	  /* If we merge reloads for two distinct rtl expressions that
	     are identical in content, there might be duplicate address
	     reloads.  Remove the extra set now, so that if we later find
	     that we can inherit this reload, we can get rid of the
	     address reloads altogether.

	     Do not do this if both reloads are optional since the result
	     would be an optional reload which could potentially leave
	     unresolved address replacements.

	     It is not sufficient to call transfer_replacements since
	     choose_reload_regs will remove the replacements for address
	     reloads of inherited reloads which results in the same
	     problem.  */
	  if (rld[i].in != in && rtx_equal_p (in, rld[i].in)
	      && ! (rld[i].optional && optional))
	    {
	      /* We must keep the address reload with the lower operand
		 number alive.  */
	      if (opnum > rld[i].opnum)
		{
		  remove_address_replacements (in);
		  in = rld[i].in;
		  in_reg = rld[i].in_reg;
		}
	      else
		remove_address_replacements (rld[i].in);
	    }
	  rld[i].in = in;
	  rld[i].in_reg = in_reg;
	}
      if (out != 0)
	{
	  rld[i].out = out;
	  rld[i].out_reg = outloc ? *outloc : 0;
	}
      if (reg_class_subset_p (class, rld[i].class))
	rld[i].class = class;
      rld[i].optional &= optional;
      if (MERGE_TO_OTHER (type, rld[i].when_needed,
			  opnum, rld[i].opnum))
	rld[i].when_needed = RELOAD_OTHER;
      rld[i].opnum = MIN (rld[i].opnum, opnum);
    }

  /* If the ostensible rtx being reloaded differs from the rtx found
     in the location to substitute, this reload is not safe to combine
     because we cannot reliably tell whether it appears in the insn.  */

  if (in != 0 && in != *inloc)
    rld[i].nocombine = 1;

#if 0
  /* This was replaced by changes in find_reloads_address_1 and the new
     function inc_for_reload, which go with a new meaning of reload_inc.  */

  /* If this is an IN/OUT reload in an insn that sets the CC,
     it must be for an autoincrement.  It doesn't work to store
     the incremented value after the insn because that would clobber the CC.
     So we must do the increment of the value reloaded from,
     increment it, store it back, then decrement again.  */
  if (out != 0 && sets_cc0_p (PATTERN (this_insn)))
    {
      out = 0;
      rld[i].out = 0;
      rld[i].inc = find_inc_amount (PATTERN (this_insn), in);
      /* If we did not find a nonzero amount-to-increment-by,
	 that contradicts the belief that IN is being incremented
	 in an address in this insn.  */
      gcc_assert (rld[i].inc != 0);
    }
#endif

  /* If we will replace IN and OUT with the reload-reg,
     record where they are located so that substitution need
     not do a tree walk.  */

  if (replace_reloads)
    {
      if (inloc != 0)
	{
	  struct replacement *r = &replacements[n_replacements++];
	  r->what = i;
	  r->subreg_loc = in_subreg_loc;
	  r->where = inloc;
	  r->mode = inmode;
	}
      if (outloc != 0 && outloc != inloc)
	{
	  struct replacement *r = &replacements[n_replacements++];
	  r->what = i;
	  r->where = outloc;
	  r->subreg_loc = out_subreg_loc;
	  r->mode = outmode;
	}
    }

  /* If this reload is just being introduced and it has both
     an incoming quantity and an outgoing quantity that are
     supposed to be made to match, see if either one of the two
     can serve as the place to reload into.

     If one of them is acceptable, set rld[i].reg_rtx
     to that one.  */

  if (in != 0 && out != 0 && in != out && rld[i].reg_rtx == 0)
    {
      rld[i].reg_rtx = find_dummy_reload (in, out, inloc, outloc,
					  inmode, outmode,
					  rld[i].class, i,
					  earlyclobber_operand_p (out));

      /* If the outgoing register already contains the same value
	 as the incoming one, we can dispense with loading it.
	 The easiest way to tell the caller that is to give a phony
	 value for the incoming operand (same as outgoing one).  */
      if (rld[i].reg_rtx == out
	  && (REG_P (in) || CONSTANT_P (in))
	  && 0 != find_equiv_reg (in, this_insn, 0, REGNO (out),
				  static_reload_reg_p, i, inmode))
	rld[i].in = out;
    }

  /* If this is an input reload and the operand contains a register that
     dies in this insn and is used nowhere else, see if it is the right class
     to be used for this reload.  Use it if so.  (This occurs most commonly
     in the case of paradoxical SUBREGs and in-out reloads).  We cannot do
     this if it is also an output reload that mentions the register unless
     the output is a SUBREG that clobbers an entire register.

     Note that the operand might be one of the spill regs, if it is a
     pseudo reg and we are in a block where spilling has not taken place.
     But if there is no spilling in this block, that is OK.
     An explicitly used hard reg cannot be a spill reg.  */

  if (rld[i].reg_rtx == 0 && in != 0 && hard_regs_live_known)
    {
      rtx note;
      int regno;
      enum machine_mode rel_mode = inmode;

      if (out && GET_MODE_SIZE (outmode) > GET_MODE_SIZE (inmode))
	rel_mode = outmode;

      for (note = REG_NOTES (this_insn); note; note = XEXP (note, 1))
	if (REG_NOTE_KIND (note) == REG_DEAD
	    && REG_P (XEXP (note, 0))
	    && (regno = REGNO (XEXP (note, 0))) < FIRST_PSEUDO_REGISTER
	    && reg_mentioned_p (XEXP (note, 0), in)
	    /* Check that we don't use a hardreg for an uninitialized
	       pseudo.  See also find_dummy_reload().  */
	    && (ORIGINAL_REGNO (XEXP (note, 0)) < FIRST_PSEUDO_REGISTER
		|| ! bitmap_bit_p (ENTRY_BLOCK_PTR->il.rtl->global_live_at_end,
				   ORIGINAL_REGNO (XEXP (note, 0))))
	    && ! refers_to_regno_for_reload_p (regno,
					       (regno
						+ hard_regno_nregs[regno]
								  [rel_mode]),
					       PATTERN (this_insn), inloc)
	    /* If this is also an output reload, IN cannot be used as
	       the reload register if it is set in this insn unless IN
	       is also OUT.  */
	    && (out == 0 || in == out
		|| ! hard_reg_set_here_p (regno,
					  (regno
					   + hard_regno_nregs[regno]
							     [rel_mode]),
					  PATTERN (this_insn)))
	    /* ??? Why is this code so different from the previous?
	       Is there any simple coherent way to describe the two together?
	       What's going on here.  */
	    && (in != out
		|| (GET_CODE (in) == SUBREG
		    && (((GET_MODE_SIZE (GET_MODE (in)) + (UNITS_PER_WORD - 1))
			 / UNITS_PER_WORD)
			== ((GET_MODE_SIZE (GET_MODE (SUBREG_REG (in)))
			     + (UNITS_PER_WORD - 1)) / UNITS_PER_WORD))))
	    /* Make sure the operand fits in the reg that dies.  */
	    && (GET_MODE_SIZE (rel_mode)
		<= GET_MODE_SIZE (GET_MODE (XEXP (note, 0))))
	    && HARD_REGNO_MODE_OK (regno, inmode)
	    && HARD_REGNO_MODE_OK (regno, outmode))
	  {
	    unsigned int offs;
	    unsigned int nregs = MAX (hard_regno_nregs[regno][inmode],
				      hard_regno_nregs[regno][outmode]);

	    for (offs = 0; offs < nregs; offs++)
	      if (fixed_regs[regno + offs]
		  || ! TEST_HARD_REG_BIT (reg_class_contents[(int) class],
					  regno + offs))
		break;

	    if (offs == nregs
		&& (! (refers_to_regno_for_reload_p
		       (regno, (regno + hard_regno_nregs[regno][inmode]),
				in, (rtx *)0))
		    || can_reload_into (in, regno, inmode)))
	      {
		rld[i].reg_rtx = gen_rtx_REG (rel_mode, regno);
		break;
	      }
	  }
    }

  if (out)
    output_reloadnum = i;

  return i;
}

/* Record an additional place we must replace a value
   for which we have already recorded a reload.
   RELOADNUM is the value returned by push_reload
   when the reload was recorded.
   This is used in insn patterns that use match_dup.  */

static void
push_replacement (rtx *loc, int reloadnum, enum machine_mode mode)
{
  if (replace_reloads)
    {
      struct replacement *r = &replacements[n_replacements++];
      r->what = reloadnum;
      r->where = loc;
      r->subreg_loc = 0;
      r->mode = mode;
    }
}

/* Duplicate any replacement we have recorded to apply at
   location ORIG_LOC to also be performed at DUP_LOC.
   This is used in insn patterns that use match_dup.  */

static void
dup_replacements (rtx *dup_loc, rtx *orig_loc)
{
  int i, n = n_replacements;

  for (i = 0; i < n; i++)
    {
      struct replacement *r = &replacements[i];
      if (r->where == orig_loc)
	push_replacement (dup_loc, r->what, r->mode);
    }
}

/* Transfer all replacements that used to be in reload FROM to be in
   reload TO.  */

void
transfer_replacements (int to, int from)
{
  int i;

  for (i = 0; i < n_replacements; i++)
    if (replacements[i].what == from)
      replacements[i].what = to;
}

/* IN_RTX is the value loaded by a reload that we now decided to inherit,
   or a subpart of it.  If we have any replacements registered for IN_RTX,
   cancel the reloads that were supposed to load them.
   Return nonzero if we canceled any reloads.  */
int
remove_address_replacements (rtx in_rtx)
{
  int i, j;
  char reload_flags[MAX_RELOADS];
  int something_changed = 0;

  memset (reload_flags, 0, sizeof reload_flags);
  for (i = 0, j = 0; i < n_replacements; i++)
    {
      if (loc_mentioned_in_p (replacements[i].where, in_rtx))
	reload_flags[replacements[i].what] |= 1;
      else
	{
	  replacements[j++] = replacements[i];
	  reload_flags[replacements[i].what] |= 2;
	}
    }
  /* Note that the following store must be done before the recursive calls.  */
  n_replacements = j;

  for (i = n_reloads - 1; i >= 0; i--)
    {
      if (reload_flags[i] == 1)
	{
	  deallocate_reload_reg (i);
	  remove_address_replacements (rld[i].in);
	  rld[i].in = 0;
	  something_changed = 1;
	}
    }
  return something_changed;
}

/* If there is only one output reload, and it is not for an earlyclobber
   operand, try to combine it with a (logically unrelated) input reload
   to reduce the number of reload registers needed.

   This is safe if the input reload does not appear in
   the value being output-reloaded, because this implies
   it is not needed any more once the original insn completes.

   If that doesn't work, see we can use any of the registers that
   die in this insn as a reload register.  We can if it is of the right
   class and does not appear in the value being output-reloaded.  */

static void
combine_reloads (void)
{
  int i;
  int output_reload = -1;
  int secondary_out = -1;
  rtx note;

  /* Find the output reload; return unless there is exactly one
     and that one is mandatory.  */

  for (i = 0; i < n_reloads; i++)
    if (rld[i].out != 0)
      {
	if (output_reload >= 0)
	  return;
	output_reload = i;
      }

  if (output_reload < 0 || rld[output_reload].optional)
    return;

  /* An input-output reload isn't combinable.  */

  if (rld[output_reload].in != 0)
    return;

  /* If this reload is for an earlyclobber operand, we can't do anything.  */
  if (earlyclobber_operand_p (rld[output_reload].out))
    return;

  /* If there is a reload for part of the address of this operand, we would
     need to chnage it to RELOAD_FOR_OTHER_ADDRESS.  But that would extend
     its life to the point where doing this combine would not lower the
     number of spill registers needed.  */
  for (i = 0; i < n_reloads; i++)
    if ((rld[i].when_needed == RELOAD_FOR_OUTPUT_ADDRESS
	 || rld[i].when_needed == RELOAD_FOR_OUTADDR_ADDRESS)
	&& rld[i].opnum == rld[output_reload].opnum)
      return;

  /* Check each input reload; can we combine it?  */

  for (i = 0; i < n_reloads; i++)
    if (rld[i].in && ! rld[i].optional && ! rld[i].nocombine
	/* Life span of this reload must not extend past main insn.  */
	&& rld[i].when_needed != RELOAD_FOR_OUTPUT_ADDRESS
	&& rld[i].when_needed != RELOAD_FOR_OUTADDR_ADDRESS
	&& rld[i].when_needed != RELOAD_OTHER
	&& (CLASS_MAX_NREGS (rld[i].class, rld[i].inmode)
	    == CLASS_MAX_NREGS (rld[output_reload].class,
				rld[output_reload].outmode))
	&& rld[i].inc == 0
	&& rld[i].reg_rtx == 0
#ifdef SECONDARY_MEMORY_NEEDED
	/* Don't combine two reloads with different secondary
	   memory locations.  */
	&& (secondary_memlocs_elim[(int) rld[output_reload].outmode][rld[i].opnum] == 0
	    || secondary_memlocs_elim[(int) rld[output_reload].outmode][rld[output_reload].opnum] == 0
	    || rtx_equal_p (secondary_memlocs_elim[(int) rld[output_reload].outmode][rld[i].opnum],
			    secondary_memlocs_elim[(int) rld[output_reload].outmode][rld[output_reload].opnum]))
#endif
	&& (SMALL_REGISTER_CLASSES
	    ? (rld[i].class == rld[output_reload].class)
	    : (reg_class_subset_p (rld[i].class,
				   rld[output_reload].class)
	       || reg_class_subset_p (rld[output_reload].class,
				      rld[i].class)))
	&& (MATCHES (rld[i].in, rld[output_reload].out)
	    /* Args reversed because the first arg seems to be
	       the one that we imagine being modified
	       while the second is the one that might be affected.  */
	    || (! reg_overlap_mentioned_for_reload_p (rld[output_reload].out,
						      rld[i].in)
		/* However, if the input is a register that appears inside
		   the output, then we also can't share.
		   Imagine (set (mem (reg 69)) (plus (reg 69) ...)).
		   If the same reload reg is used for both reg 69 and the
		   result to be stored in memory, then that result
		   will clobber the address of the memory ref.  */
		&& ! (REG_P (rld[i].in)
		      && reg_overlap_mentioned_for_reload_p (rld[i].in,
							     rld[output_reload].out))))
	&& ! reload_inner_reg_of_subreg (rld[i].in, rld[i].inmode,
					 rld[i].when_needed != RELOAD_FOR_INPUT)
	&& (reg_class_size[(int) rld[i].class]
	    || SMALL_REGISTER_CLASSES)
	/* We will allow making things slightly worse by combining an
	   input and an output, but no worse than that.  */
	&& (rld[i].when_needed == RELOAD_FOR_INPUT
	    || rld[i].when_needed == RELOAD_FOR_OUTPUT))
      {
	int j;

	/* We have found a reload to combine with!  */
	rld[i].out = rld[output_reload].out;
	rld[i].out_reg = rld[output_reload].out_reg;
	rld[i].outmode = rld[output_reload].outmode;
	/* Mark the old output reload as inoperative.  */
	rld[output_reload].out = 0;
	/* The combined reload is needed for the entire insn.  */
	rld[i].when_needed = RELOAD_OTHER;
	/* If the output reload had a secondary reload, copy it.  */
	if (rld[output_reload].secondary_out_reload != -1)
	  {
	    rld[i].secondary_out_reload
	      = rld[output_reload].secondary_out_reload;
	    rld[i].secondary_out_icode
	      = rld[output_reload].secondary_out_icode;
	  }

#ifdef SECONDARY_MEMORY_NEEDED
	/* Copy any secondary MEM.  */
	if (secondary_memlocs_elim[(int) rld[output_reload].outmode][rld[output_reload].opnum] != 0)
	  secondary_memlocs_elim[(int) rld[output_reload].outmode][rld[i].opnum]
	    = secondary_memlocs_elim[(int) rld[output_reload].outmode][rld[output_reload].opnum];
#endif
	/* If required, minimize the register class.  */
	if (reg_class_subset_p (rld[output_reload].class,
				rld[i].class))
	  rld[i].class = rld[output_reload].class;

	/* Transfer all replacements from the old reload to the combined.  */
	for (j = 0; j < n_replacements; j++)
	  if (replacements[j].what == output_reload)
	    replacements[j].what = i;

	return;
      }

  /* If this insn has only one operand that is modified or written (assumed
     to be the first),  it must be the one corresponding to this reload.  It
     is safe to use anything that dies in this insn for that output provided
     that it does not occur in the output (we already know it isn't an
     earlyclobber.  If this is an asm insn, give up.  */

  if (INSN_CODE (this_insn) == -1)
    return;

  for (i = 1; i < insn_data[INSN_CODE (this_insn)].n_operands; i++)
    if (insn_data[INSN_CODE (this_insn)].operand[i].constraint[0] == '='
	|| insn_data[INSN_CODE (this_insn)].operand[i].constraint[0] == '+')
      return;

  /* See if some hard register that dies in this insn and is not used in
     the output is the right class.  Only works if the register we pick
     up can fully hold our output reload.  */
  for (note = REG_NOTES (this_insn); note; note = XEXP (note, 1))
    if (REG_NOTE_KIND (note) == REG_DEAD
	&& REG_P (XEXP (note, 0))
	&& ! reg_overlap_mentioned_for_reload_p (XEXP (note, 0),
						 rld[output_reload].out)
	&& REGNO (XEXP (note, 0)) < FIRST_PSEUDO_REGISTER
	&& HARD_REGNO_MODE_OK (REGNO (XEXP (note, 0)), rld[output_reload].outmode)
	&& TEST_HARD_REG_BIT (reg_class_contents[(int) rld[output_reload].class],
			      REGNO (XEXP (note, 0)))
	&& (hard_regno_nregs[REGNO (XEXP (note, 0))][rld[output_reload].outmode]
	    <= hard_regno_nregs[REGNO (XEXP (note, 0))][GET_MODE (XEXP (note, 0))])
	/* Ensure that a secondary or tertiary reload for this output
	   won't want this register.  */
	&& ((secondary_out = rld[output_reload].secondary_out_reload) == -1
	    || (! (TEST_HARD_REG_BIT
		   (reg_class_contents[(int) rld[secondary_out].class],
		    REGNO (XEXP (note, 0))))
		&& ((secondary_out = rld[secondary_out].secondary_out_reload) == -1
		    ||  ! (TEST_HARD_REG_BIT
			   (reg_class_contents[(int) rld[secondary_out].class],
			    REGNO (XEXP (note, 0)))))))
	&& ! fixed_regs[REGNO (XEXP (note, 0))]
	/* Check that we don't use a hardreg for an uninitialized
	   pseudo.  See also find_dummy_reload().  */
	&& (ORIGINAL_REGNO (XEXP (note, 0)) < FIRST_PSEUDO_REGISTER
	    || ! bitmap_bit_p (ENTRY_BLOCK_PTR->il.rtl->global_live_at_end,
			       ORIGINAL_REGNO (XEXP (note, 0)))))
      {
	rld[output_reload].reg_rtx
	  = gen_rtx_REG (rld[output_reload].outmode,
			 REGNO (XEXP (note, 0)));
	return;
      }
}

/* Try to find a reload register for an in-out reload (expressions IN and OUT).
   See if one of IN and OUT is a register that may be used;
   this is desirable since a spill-register won't be needed.
   If so, return the register rtx that proves acceptable.

   INLOC and OUTLOC are locations where IN and OUT appear in the insn.
   CLASS is the register class required for the reload.

   If FOR_REAL is >= 0, it is the number of the reload,
   and in some cases when it can be discovered that OUT doesn't need
   to be computed, clear out rld[FOR_REAL].out.

   If FOR_REAL is -1, this should not be done, because this call
   is just to see if a register can be found, not to find and install it.

   EARLYCLOBBER is nonzero if OUT is an earlyclobber operand.  This
   puts an additional constraint on being able to use IN for OUT since
   IN must not appear elsewhere in the insn (it is assumed that IN itself
   is safe from the earlyclobber).  */

static rtx
find_dummy_reload (rtx real_in, rtx real_out, rtx *inloc, rtx *outloc,
		   enum machine_mode inmode, enum machine_mode outmode,
		   enum reg_class class, int for_real, int earlyclobber)
{
  rtx in = real_in;
  rtx out = real_out;
  int in_offset = 0;
  int out_offset = 0;
  rtx value = 0;

  /* If operands exceed a word, we can't use either of them
     unless they have the same size.  */
  if (GET_MODE_SIZE (outmode) != GET_MODE_SIZE (inmode)
      && (GET_MODE_SIZE (outmode) > UNITS_PER_WORD
	  || GET_MODE_SIZE (inmode) > UNITS_PER_WORD))
    return 0;

  /* Note that {in,out}_offset are needed only when 'in' or 'out'
     respectively refers to a hard register.  */

  /* Find the inside of any subregs.  */
  while (GET_CODE (out) == SUBREG)
    {
      if (REG_P (SUBREG_REG (out))
	  && REGNO (SUBREG_REG (out)) < FIRST_PSEUDO_REGISTER)
	out_offset += subreg_regno_offset (REGNO (SUBREG_REG (out)),
					   GET_MODE (SUBREG_REG (out)),
					   SUBREG_BYTE (out),
					   GET_MODE (out));
      out = SUBREG_REG (out);
    }
  while (GET_CODE (in) == SUBREG)
    {
      if (REG_P (SUBREG_REG (in))
	  && REGNO (SUBREG_REG (in)) < FIRST_PSEUDO_REGISTER)
	in_offset += subreg_regno_offset (REGNO (SUBREG_REG (in)),
					  GET_MODE (SUBREG_REG (in)),
					  SUBREG_BYTE (in),
					  GET_MODE (in));
      in = SUBREG_REG (in);
    }

  /* Narrow down the reg class, the same way push_reload will;
     otherwise we might find a dummy now, but push_reload won't.  */
  {
    enum reg_class preferred_class = PREFERRED_RELOAD_CLASS (in, class);
    if (preferred_class != NO_REGS)
      class = preferred_class;
  }

  /* See if OUT will do.  */
  if (REG_P (out)
      && REGNO (out) < FIRST_PSEUDO_REGISTER)
    {
      unsigned int regno = REGNO (out) + out_offset;
      unsigned int nwords = hard_regno_nregs[regno][outmode];
      rtx saved_rtx;

      /* When we consider whether the insn uses OUT,
	 ignore references within IN.  They don't prevent us
	 from copying IN into OUT, because those refs would
	 move into the insn that reloads IN.

	 However, we only ignore IN in its role as this reload.
	 If the insn uses IN elsewhere and it contains OUT,
	 that counts.  We can't be sure it's the "same" operand
	 so it might not go through this reload.  */
      saved_rtx = *inloc;
      *inloc = const0_rtx;

      if (regno < FIRST_PSEUDO_REGISTER
	  && HARD_REGNO_MODE_OK (regno, outmode)
	  && ! refers_to_regno_for_reload_p (regno, regno + nwords,
					     PATTERN (this_insn), outloc))
	{
	  unsigned int i;

	  for (i = 0; i < nwords; i++)
	    if (! TEST_HARD_REG_BIT (reg_class_contents[(int) class],
				     regno + i))
	      break;

	  if (i == nwords)
	    {
	      if (REG_P (real_out))
		value = real_out;
	      else
		value = gen_rtx_REG (outmode, regno);
	    }
	}

      *inloc = saved_rtx;
    }

  /* Consider using IN if OUT was not acceptable
     or if OUT dies in this insn (like the quotient in a divmod insn).
     We can't use IN unless it is dies in this insn,
     which means we must know accurately which hard regs are live.
     Also, the result can't go in IN if IN is used within OUT,
     or if OUT is an earlyclobber and IN appears elsewhere in the insn.  */
  if (hard_regs_live_known
      && REG_P (in)
      && REGNO (in) < FIRST_PSEUDO_REGISTER
      && (value == 0
	  || find_reg_note (this_insn, REG_UNUSED, real_out))
      && find_reg_note (this_insn, REG_DEAD, real_in)
      && !fixed_regs[REGNO (in)]
      && HARD_REGNO_MODE_OK (REGNO (in),
			     /* The only case where out and real_out might
				have different modes is where real_out
				is a subreg, and in that case, out
				has a real mode.  */
			     (GET_MODE (out) != VOIDmode
			      ? GET_MODE (out) : outmode))
        /* But only do all this if we can be sure, that this input
           operand doesn't correspond with an uninitialized pseudoreg.
           global can assign some hardreg to it, which is the same as
	   a different pseudo also currently live (as it can ignore the
	   conflict).  So we never must introduce writes to such hardregs,
	   as they would clobber the other live pseudo using the same.
	   See also PR20973.  */
      && (ORIGINAL_REGNO (in) < FIRST_PSEUDO_REGISTER
          || ! bitmap_bit_p (ENTRY_BLOCK_PTR->il.rtl->global_live_at_end,
			     ORIGINAL_REGNO (in))))
    {
      unsigned int regno = REGNO (in) + in_offset;
      unsigned int nwords = hard_regno_nregs[regno][inmode];

      if (! refers_to_regno_for_reload_p (regno, regno + nwords, out, (rtx*) 0)
	  && ! hard_reg_set_here_p (regno, regno + nwords,
				    PATTERN (this_insn))
	  && (! earlyclobber
	      || ! refers_to_regno_for_reload_p (regno, regno + nwords,
						 PATTERN (this_insn), inloc)))
	{
	  unsigned int i;

	  for (i = 0; i < nwords; i++)
	    if (! TEST_HARD_REG_BIT (reg_class_contents[(int) class],
				     regno + i))
	      break;

	  if (i == nwords)
	    {
	      /* If we were going to use OUT as the reload reg
		 and changed our mind, it means OUT is a dummy that
		 dies here.  So don't bother copying value to it.  */
	      if (for_real >= 0 && value == real_out)
		rld[for_real].out = 0;
	      if (REG_P (real_in))
		value = real_in;
	      else
		value = gen_rtx_REG (inmode, regno);
	    }
	}
    }

  return value;
}

/* This page contains subroutines used mainly for determining
   whether the IN or an OUT of a reload can serve as the
   reload register.  */

/* Return 1 if X is an operand of an insn that is being earlyclobbered.  */

int
earlyclobber_operand_p (rtx x)
{
  int i;

  for (i = 0; i < n_earlyclobbers; i++)
    if (reload_earlyclobbers[i] == x)
      return 1;

  return 0;
}

/* Return 1 if expression X alters a hard reg in the range
   from BEG_REGNO (inclusive) to END_REGNO (exclusive),
   either explicitly or in the guise of a pseudo-reg allocated to REGNO.
   X should be the body of an instruction.  */

static int
hard_reg_set_here_p (unsigned int beg_regno, unsigned int end_regno, rtx x)
{
  if (GET_CODE (x) == SET || GET_CODE (x) == CLOBBER)
    {
      rtx op0 = SET_DEST (x);

      while (GET_CODE (op0) == SUBREG)
	op0 = SUBREG_REG (op0);
      if (REG_P (op0))
	{
	  unsigned int r = REGNO (op0);

	  /* See if this reg overlaps range under consideration.  */
	  if (r < end_regno
	      && r + hard_regno_nregs[r][GET_MODE (op0)] > beg_regno)
	    return 1;
	}
    }
  else if (GET_CODE (x) == PARALLEL)
    {
      int i = XVECLEN (x, 0) - 1;

      for (; i >= 0; i--)
	if (hard_reg_set_here_p (beg_regno, end_regno, XVECEXP (x, 0, i)))
	  return 1;
    }

  return 0;
}

/* Return 1 if ADDR is a valid memory address for mode MODE,
   and check that each pseudo reg has the proper kind of
   hard reg.  */

int
strict_memory_address_p (enum machine_mode mode ATTRIBUTE_UNUSED, rtx addr)
{
  GO_IF_LEGITIMATE_ADDRESS (mode, addr, win);
  return 0;

 win:
  return 1;
}

/* Like rtx_equal_p except that it allows a REG and a SUBREG to match
   if they are the same hard reg, and has special hacks for
   autoincrement and autodecrement.
   This is specifically intended for find_reloads to use
   in determining whether two operands match.
   X is the operand whose number is the lower of the two.

   The value is 2 if Y contains a pre-increment that matches
   a non-incrementing address in X.  */

/* ??? To be completely correct, we should arrange to pass
   for X the output operand and for Y the input operand.
   For now, we assume that the output operand has the lower number
   because that is natural in (SET output (... input ...)).  */

int
operands_match_p (rtx x, rtx y)
{
  int i;
  RTX_CODE code = GET_CODE (x);
  const char *fmt;
  int success_2;

  if (x == y)
    return 1;
  if ((code == REG || (code == SUBREG && REG_P (SUBREG_REG (x))))
      && (REG_P (y) || (GET_CODE (y) == SUBREG
				  && REG_P (SUBREG_REG (y)))))
    {
      int j;

      if (code == SUBREG)
	{
	  i = REGNO (SUBREG_REG (x));
	  if (i >= FIRST_PSEUDO_REGISTER)
	    goto slow;
	  i += subreg_regno_offset (REGNO (SUBREG_REG (x)),
				    GET_MODE (SUBREG_REG (x)),
				    SUBREG_BYTE (x),
				    GET_MODE (x));
	}
      else
	i = REGNO (x);

      if (GET_CODE (y) == SUBREG)
	{
	  j = REGNO (SUBREG_REG (y));
	  if (j >= FIRST_PSEUDO_REGISTER)
	    goto slow;
	  j += subreg_regno_offset (REGNO (SUBREG_REG (y)),
				    GET_MODE (SUBREG_REG (y)),
				    SUBREG_BYTE (y),
				    GET_MODE (y));
	}
      else
	j = REGNO (y);

      /* On a WORDS_BIG_ENDIAN machine, point to the last register of a
	 multiple hard register group of scalar integer registers, so that
	 for example (reg:DI 0) and (reg:SI 1) will be considered the same
	 register.  */
      if (WORDS_BIG_ENDIAN && GET_MODE_SIZE (GET_MODE (x)) > UNITS_PER_WORD
	  && SCALAR_INT_MODE_P (GET_MODE (x))
	  && i < FIRST_PSEUDO_REGISTER)
	i += hard_regno_nregs[i][GET_MODE (x)] - 1;
      if (WORDS_BIG_ENDIAN && GET_MODE_SIZE (GET_MODE (y)) > UNITS_PER_WORD
	  && SCALAR_INT_MODE_P (GET_MODE (y))
	  && j < FIRST_PSEUDO_REGISTER)
	j += hard_regno_nregs[j][GET_MODE (y)] - 1;

      return i == j;
    }
  /* If two operands must match, because they are really a single
     operand of an assembler insn, then two postincrements are invalid
     because the assembler insn would increment only once.
     On the other hand, a postincrement matches ordinary indexing
     if the postincrement is the output operand.  */
  if (code == POST_DEC || code == POST_INC || code == POST_MODIFY)
    return operands_match_p (XEXP (x, 0), y);
  /* Two preincrements are invalid
     because the assembler insn would increment only once.
     On the other hand, a preincrement matches ordinary indexing
     if the preincrement is the input operand.
     In this case, return 2, since some callers need to do special
     things when this happens.  */
  if (GET_CODE (y) == PRE_DEC || GET_CODE (y) == PRE_INC
      || GET_CODE (y) == PRE_MODIFY)
    return operands_match_p (x, XEXP (y, 0)) ? 2 : 0;

 slow:

  /* Now we have disposed of all the cases in which different rtx codes
     can match.  */
  if (code != GET_CODE (y))
    return 0;

  /* (MULT:SI x y) and (MULT:HI x y) are NOT equivalent.  */
  if (GET_MODE (x) != GET_MODE (y))
    return 0;

  switch (code)
    {
    case CONST_INT:
    case CONST_DOUBLE:
      return 0;

    case LABEL_REF:
      return XEXP (x, 0) == XEXP (y, 0);
    case SYMBOL_REF:
      return XSTR (x, 0) == XSTR (y, 0);

    default:
      break;
    }

  /* Compare the elements.  If any pair of corresponding elements
     fail to match, return 0 for the whole things.  */

  success_2 = 0;
  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      int val, j;
      switch (fmt[i])
	{
	case 'w':
	  if (XWINT (x, i) != XWINT (y, i))
	    return 0;
	  break;

	case 'i':
	  if (XINT (x, i) != XINT (y, i))
	    return 0;
	  break;

	case 'e':
	  val = operands_match_p (XEXP (x, i), XEXP (y, i));
	  if (val == 0)
	    return 0;
	  /* If any subexpression returns 2,
	     we should return 2 if we are successful.  */
	  if (val == 2)
	    success_2 = 1;
	  break;

	case '0':
	  break;

	case 'E':
	  if (XVECLEN (x, i) != XVECLEN (y, i))
	    return 0;
	  for (j = XVECLEN (x, i) - 1; j >= 0; --j)
	    {
	      val = operands_match_p (XVECEXP (x, i, j), XVECEXP (y, i, j));
	      if (val == 0)
		return 0;
	      if (val == 2)
		success_2 = 1;
	    }
	  break;

	  /* It is believed that rtx's at this level will never
	     contain anything but integers and other rtx's,
	     except for within LABEL_REFs and SYMBOL_REFs.  */
	default:
	  gcc_unreachable ();
	}
    }
  return 1 + success_2;
}

/* Describe the range of registers or memory referenced by X.
   If X is a register, set REG_FLAG and put the first register
   number into START and the last plus one into END.
   If X is a memory reference, put a base address into BASE
   and a range of integer offsets into START and END.
   If X is pushing on the stack, we can assume it causes no trouble,
   so we set the SAFE field.  */

static struct decomposition
decompose (rtx x)
{
  struct decomposition val;
  int all_const = 0;

  memset (&val, 0, sizeof (val));

  switch (GET_CODE (x))
    {
    case MEM:
      {
	rtx base = NULL_RTX, offset = 0;
	rtx addr = XEXP (x, 0);
	
	if (GET_CODE (addr) == PRE_DEC || GET_CODE (addr) == PRE_INC
	    || GET_CODE (addr) == POST_DEC || GET_CODE (addr) == POST_INC)
	  {
	    val.base = XEXP (addr, 0);
	    val.start = -GET_MODE_SIZE (GET_MODE (x));
	    val.end = GET_MODE_SIZE (GET_MODE (x));
	    val.safe = REGNO (val.base) == STACK_POINTER_REGNUM;
	    return val;
	  }
	
	if (GET_CODE (addr) == PRE_MODIFY || GET_CODE (addr) == POST_MODIFY)
	  {
	    if (GET_CODE (XEXP (addr, 1)) == PLUS
		&& XEXP (addr, 0) == XEXP (XEXP (addr, 1), 0)
		&& CONSTANT_P (XEXP (XEXP (addr, 1), 1)))
	      {
		val.base  = XEXP (addr, 0);
		val.start = -INTVAL (XEXP (XEXP (addr, 1), 1));
		val.end   = INTVAL (XEXP (XEXP (addr, 1), 1));
		val.safe  = REGNO (val.base) == STACK_POINTER_REGNUM;
		return val;
	      }
	  }
	
	if (GET_CODE (addr) == CONST)
	  {
	    addr = XEXP (addr, 0);
	    all_const = 1;
	  }
	if (GET_CODE (addr) == PLUS)
	  {
	    if (CONSTANT_P (XEXP (addr, 0)))
	      {
		base = XEXP (addr, 1);
		offset = XEXP (addr, 0);
	      }
	    else if (CONSTANT_P (XEXP (addr, 1)))
	      {
		base = XEXP (addr, 0);
		offset = XEXP (addr, 1);
	      }
	  }
	
	if (offset == 0)
	  {
	    base = addr;
	    offset = const0_rtx;
	  }
	if (GET_CODE (offset) == CONST)
	  offset = XEXP (offset, 0);
	if (GET_CODE (offset) == PLUS)
	  {
	    if (GET_CODE (XEXP (offset, 0)) == CONST_INT)
	      {
		base = gen_rtx_PLUS (GET_MODE (base), base, XEXP (offset, 1));
		offset = XEXP (offset, 0);
	      }
	    else if (GET_CODE (XEXP (offset, 1)) == CONST_INT)
	      {
		base = gen_rtx_PLUS (GET_MODE (base), base, XEXP (offset, 0));
		offset = XEXP (offset, 1);
	      }
	    else
	      {
		base = gen_rtx_PLUS (GET_MODE (base), base, offset);
		offset = const0_rtx;
	      }
	  }
	else if (GET_CODE (offset) != CONST_INT)
	  {
	    base = gen_rtx_PLUS (GET_MODE (base), base, offset);
	    offset = const0_rtx;
	  }
	
	if (all_const && GET_CODE (base) == PLUS)
	  base = gen_rtx_CONST (GET_MODE (base), base);
	
	gcc_assert (GET_CODE (offset) == CONST_INT);
	
	val.start = INTVAL (offset);
	val.end = val.start + GET_MODE_SIZE (GET_MODE (x));
	val.base = base;
      }
      break;
      
    case REG:
      val.reg_flag = 1;
      val.start = true_regnum (x);
      if (val.start < 0 || val.start >= FIRST_PSEUDO_REGISTER)
	{
	  /* A pseudo with no hard reg.  */
	  val.start = REGNO (x);
	  val.end = val.start + 1;
	}
      else
	/* A hard reg.  */
	val.end = val.start + hard_regno_nregs[val.start][GET_MODE (x)];
      break;

    case SUBREG:
      if (!REG_P (SUBREG_REG (x)))
	/* This could be more precise, but it's good enough.  */
	return decompose (SUBREG_REG (x));
      val.reg_flag = 1;
      val.start = true_regnum (x);
      if (val.start < 0 || val.start >= FIRST_PSEUDO_REGISTER)
	return decompose (SUBREG_REG (x));
      else
	/* A hard reg.  */
	val.end = val.start + hard_regno_nregs[val.start][GET_MODE (x)];
      break;

    case SCRATCH:
      /* This hasn't been assigned yet, so it can't conflict yet.  */
      val.safe = 1;
      break;

    default:
      gcc_assert (CONSTANT_P (x));
      val.safe = 1;
      break;
    }
  return val;
}

/* Return 1 if altering Y will not modify the value of X.
   Y is also described by YDATA, which should be decompose (Y).  */

static int
immune_p (rtx x, rtx y, struct decomposition ydata)
{
  struct decomposition xdata;

  if (ydata.reg_flag)
    return !refers_to_regno_for_reload_p (ydata.start, ydata.end, x, (rtx*) 0);
  if (ydata.safe)
    return 1;

  gcc_assert (MEM_P (y));
  /* If Y is memory and X is not, Y can't affect X.  */
  if (!MEM_P (x))
    return 1;

  xdata = decompose (x);

  if (! rtx_equal_p (xdata.base, ydata.base))
    {
      /* If bases are distinct symbolic constants, there is no overlap.  */
      if (CONSTANT_P (xdata.base) && CONSTANT_P (ydata.base))
	return 1;
      /* Constants and stack slots never overlap.  */
      if (CONSTANT_P (xdata.base)
	  && (ydata.base == frame_pointer_rtx
	      || ydata.base == hard_frame_pointer_rtx
	      || ydata.base == stack_pointer_rtx))
	return 1;
      if (CONSTANT_P (ydata.base)
	  && (xdata.base == frame_pointer_rtx
	      || xdata.base == hard_frame_pointer_rtx
	      || xdata.base == stack_pointer_rtx))
	return 1;
      /* If either base is variable, we don't know anything.  */
      return 0;
    }

  return (xdata.start >= ydata.end || ydata.start >= xdata.end);
}

/* Similar, but calls decompose.  */

int
safe_from_earlyclobber (rtx op, rtx clobber)
{
  struct decomposition early_data;

  early_data = decompose (clobber);
  return immune_p (op, clobber, early_data);
}

/* Main entry point of this file: search the body of INSN
   for values that need reloading and record them with push_reload.
   REPLACE nonzero means record also where the values occur
   so that subst_reloads can be used.

   IND_LEVELS says how many levels of indirection are supported by this
   machine; a value of zero means that a memory reference is not a valid
   memory address.

   LIVE_KNOWN says we have valid information about which hard
   regs are live at each point in the program; this is true when
   we are called from global_alloc but false when stupid register
   allocation has been done.

   RELOAD_REG_P if nonzero is a vector indexed by hard reg number
   which is nonnegative if the reg has been commandeered for reloading into.
   It is copied into STATIC_RELOAD_REG_P and referenced from there
   by various subroutines.

   Return TRUE if some operands need to be changed, because of swapping
   commutative operands, reg_equiv_address substitution, or whatever.  */

int
find_reloads (rtx insn, int replace, int ind_levels, int live_known,
	      short *reload_reg_p)
{
  int insn_code_number;
  int i, j;
  int noperands;
  /* These start out as the constraints for the insn
     and they are chewed up as we consider alternatives.  */
  char *constraints[MAX_RECOG_OPERANDS];
  /* These are the preferred classes for an operand, or NO_REGS if it isn't
     a register.  */
  enum reg_class preferred_class[MAX_RECOG_OPERANDS];
  char pref_or_nothing[MAX_RECOG_OPERANDS];
  /* Nonzero for a MEM operand whose entire address needs a reload. 
     May be -1 to indicate the entire address may or may not need a reload.  */
  int address_reloaded[MAX_RECOG_OPERANDS];
  /* Nonzero for an address operand that needs to be completely reloaded.
     May be -1 to indicate the entire operand may or may not need a reload.  */
  int address_operand_reloaded[MAX_RECOG_OPERANDS];
  /* Value of enum reload_type to use for operand.  */
  enum reload_type operand_type[MAX_RECOG_OPERANDS];
  /* Value of enum reload_type to use within address of operand.  */
  enum reload_type address_type[MAX_RECOG_OPERANDS];
  /* Save the usage of each operand.  */
  enum reload_usage { RELOAD_READ, RELOAD_READ_WRITE, RELOAD_WRITE } modified[MAX_RECOG_OPERANDS];
  int no_input_reloads = 0, no_output_reloads = 0;
  int n_alternatives;
  int this_alternative[MAX_RECOG_OPERANDS];
  char this_alternative_match_win[MAX_RECOG_OPERANDS];
  char this_alternative_win[MAX_RECOG_OPERANDS];
  char this_alternative_offmemok[MAX_RECOG_OPERANDS];
  char this_alternative_earlyclobber[MAX_RECOG_OPERANDS];
  int this_alternative_matches[MAX_RECOG_OPERANDS];
  int swapped;
  int goal_alternative[MAX_RECOG_OPERANDS];
  int this_alternative_number;
  int goal_alternative_number = 0;
  int operand_reloadnum[MAX_RECOG_OPERANDS];
  int goal_alternative_matches[MAX_RECOG_OPERANDS];
  int goal_alternative_matched[MAX_RECOG_OPERANDS];
  char goal_alternative_match_win[MAX_RECOG_OPERANDS];
  char goal_alternative_win[MAX_RECOG_OPERANDS];
  char goal_alternative_offmemok[MAX_RECOG_OPERANDS];
  char goal_alternative_earlyclobber[MAX_RECOG_OPERANDS];
  int goal_alternative_swapped;
  int best;
  int commutative;
  char operands_match[MAX_RECOG_OPERANDS][MAX_RECOG_OPERANDS];
  rtx substed_operand[MAX_RECOG_OPERANDS];
  rtx body = PATTERN (insn);
  rtx set = single_set (insn);
  int goal_earlyclobber = 0, this_earlyclobber;
  enum machine_mode operand_mode[MAX_RECOG_OPERANDS];
  int retval = 0;

  this_insn = insn;
  n_reloads = 0;
  n_replacements = 0;
  n_earlyclobbers = 0;
  replace_reloads = replace;
  hard_regs_live_known = live_known;
  static_reload_reg_p = reload_reg_p;

  /* JUMP_INSNs and CALL_INSNs are not allowed to have any output reloads;
     neither are insns that SET cc0.  Insns that use CC0 are not allowed
     to have any input reloads.  */
  if (JUMP_P (insn) || CALL_P (insn))
    no_output_reloads = 1;

#ifdef HAVE_cc0
  if (reg_referenced_p (cc0_rtx, PATTERN (insn)))
    no_input_reloads = 1;
  if (reg_set_p (cc0_rtx, PATTERN (insn)))
    no_output_reloads = 1;
#endif

#ifdef SECONDARY_MEMORY_NEEDED
  /* The eliminated forms of any secondary memory locations are per-insn, so
     clear them out here.  */

  if (secondary_memlocs_elim_used)
    {
      memset (secondary_memlocs_elim, 0,
	      sizeof (secondary_memlocs_elim[0]) * secondary_memlocs_elim_used);
      secondary_memlocs_elim_used = 0;
    }
#endif

  /* Dispose quickly of (set (reg..) (reg..)) if both have hard regs and it
     is cheap to move between them.  If it is not, there may not be an insn
     to do the copy, so we may need a reload.  */
  if (GET_CODE (body) == SET
      && REG_P (SET_DEST (body))
      && REGNO (SET_DEST (body)) < FIRST_PSEUDO_REGISTER
      && REG_P (SET_SRC (body))
      && REGNO (SET_SRC (body)) < FIRST_PSEUDO_REGISTER
      && REGISTER_MOVE_COST (GET_MODE (SET_SRC (body)),
			     REGNO_REG_CLASS (REGNO (SET_SRC (body))),
			     REGNO_REG_CLASS (REGNO (SET_DEST (body)))) == 2)
    return 0;

  extract_insn (insn);

  noperands = reload_n_operands = recog_data.n_operands;
  n_alternatives = recog_data.n_alternatives;

  /* Just return "no reloads" if insn has no operands with constraints.  */
  if (noperands == 0 || n_alternatives == 0)
    return 0;

  insn_code_number = INSN_CODE (insn);
  this_insn_is_asm = insn_code_number < 0;

  memcpy (operand_mode, recog_data.operand_mode,
	  noperands * sizeof (enum machine_mode));
  memcpy (constraints, recog_data.constraints, noperands * sizeof (char *));

  commutative = -1;

  /* If we will need to know, later, whether some pair of operands
     are the same, we must compare them now and save the result.
     Reloading the base and index registers will clobber them
     and afterward they will fail to match.  */

  for (i = 0; i < noperands; i++)
    {
      char *p;
      int c;

      substed_operand[i] = recog_data.operand[i];
      p = constraints[i];

      modified[i] = RELOAD_READ;

      /* Scan this operand's constraint to see if it is an output operand,
	 an in-out operand, is commutative, or should match another.  */

      while ((c = *p))
	{
	  p += CONSTRAINT_LEN (c, p);
	  switch (c)
	    {
	    case '=':
	      modified[i] = RELOAD_WRITE;
	      break;
	    case '+':
	      modified[i] = RELOAD_READ_WRITE;
	      break;
	    case '%':
	      {
		/* The last operand should not be marked commutative.  */
		gcc_assert (i != noperands - 1);

		/* We currently only support one commutative pair of
		   operands.  Some existing asm code currently uses more
		   than one pair.  Previously, that would usually work,
		   but sometimes it would crash the compiler.  We
		   continue supporting that case as well as we can by
		   silently ignoring all but the first pair.  In the
		   future we may handle it correctly.  */
		if (commutative < 0)
		  commutative = i;
		else
		  gcc_assert (this_insn_is_asm);
	      }
	      break;
	    /* Use of ISDIGIT is tempting here, but it may get expensive because
	       of locale support we don't want.  */
	    case '0': case '1': case '2': case '3': case '4':
	    case '5': case '6': case '7': case '8': case '9':
	      {
		c = strtoul (p - 1, &p, 10);

		operands_match[c][i]
		  = operands_match_p (recog_data.operand[c],
				      recog_data.operand[i]);

		/* An operand may not match itself.  */
		gcc_assert (c != i);

		/* If C can be commuted with C+1, and C might need to match I,
		   then C+1 might also need to match I.  */
		if (commutative >= 0)
		  {
		    if (c == commutative || c == commutative + 1)
		      {
			int other = c + (c == commutative ? 1 : -1);
			operands_match[other][i]
			  = operands_match_p (recog_data.operand[other],
					      recog_data.operand[i]);
		      }
		    if (i == commutative || i == commutative + 1)
		      {
			int other = i + (i == commutative ? 1 : -1);
			operands_match[c][other]
			  = operands_match_p (recog_data.operand[c],
					      recog_data.operand[other]);
		      }
		    /* Note that C is supposed to be less than I.
		       No need to consider altering both C and I because in
		       that case we would alter one into the other.  */
		  }
	      }
	    }
	}
    }

  /* Examine each operand that is a memory reference or memory address
     and reload parts of the addresses into index registers.
     Also here any references to pseudo regs that didn't get hard regs
     but are equivalent to constants get replaced in the insn itself
     with those constants.  Nobody will ever see them again.

     Finally, set up the preferred classes of each operand.  */

  for (i = 0; i < noperands; i++)
    {
      RTX_CODE code = GET_CODE (recog_data.operand[i]);

      address_reloaded[i] = 0;
      address_operand_reloaded[i] = 0;
      operand_type[i] = (modified[i] == RELOAD_READ ? RELOAD_FOR_INPUT
			 : modified[i] == RELOAD_WRITE ? RELOAD_FOR_OUTPUT
			 : RELOAD_OTHER);
      address_type[i]
	= (modified[i] == RELOAD_READ ? RELOAD_FOR_INPUT_ADDRESS
	   : modified[i] == RELOAD_WRITE ? RELOAD_FOR_OUTPUT_ADDRESS
	   : RELOAD_OTHER);

      if (*constraints[i] == 0)
	/* Ignore things like match_operator operands.  */
	;
      else if (constraints[i][0] == 'p'
	       || EXTRA_ADDRESS_CONSTRAINT (constraints[i][0], constraints[i]))
	{
	  address_operand_reloaded[i]
	    = find_reloads_address (recog_data.operand_mode[i], (rtx*) 0,
				    recog_data.operand[i],
				    recog_data.operand_loc[i],
				    i, operand_type[i], ind_levels, insn);

	  /* If we now have a simple operand where we used to have a
	     PLUS or MULT, re-recognize and try again.  */
	  if ((OBJECT_P (*recog_data.operand_loc[i])
	       || GET_CODE (*recog_data.operand_loc[i]) == SUBREG)
	      && (GET_CODE (recog_data.operand[i]) == MULT
		  || GET_CODE (recog_data.operand[i]) == PLUS))
	    {
	      INSN_CODE (insn) = -1;
	      retval = find_reloads (insn, replace, ind_levels, live_known,
				     reload_reg_p);
	      return retval;
	    }

	  recog_data.operand[i] = *recog_data.operand_loc[i];
	  substed_operand[i] = recog_data.operand[i];

	  /* Address operands are reloaded in their existing mode,
	     no matter what is specified in the machine description.  */
	  operand_mode[i] = GET_MODE (recog_data.operand[i]);
	}
      else if (code == MEM)
	{
	  address_reloaded[i]
	    = find_reloads_address (GET_MODE (recog_data.operand[i]),
				    recog_data.operand_loc[i],
				    XEXP (recog_data.operand[i], 0),
				    &XEXP (recog_data.operand[i], 0),
				    i, address_type[i], ind_levels, insn);
	  recog_data.operand[i] = *recog_data.operand_loc[i];
	  substed_operand[i] = recog_data.operand[i];
	}
      else if (code == SUBREG)
	{
	  rtx reg = SUBREG_REG (recog_data.operand[i]);
	  rtx op
	    = find_reloads_toplev (recog_data.operand[i], i, address_type[i],
				   ind_levels,
				   set != 0
				   && &SET_DEST (set) == recog_data.operand_loc[i],
				   insn,
				   &address_reloaded[i]);

	  /* If we made a MEM to load (a part of) the stackslot of a pseudo
	     that didn't get a hard register, emit a USE with a REG_EQUAL
	     note in front so that we might inherit a previous, possibly
	     wider reload.  */

	  if (replace
	      && MEM_P (op)
	      && REG_P (reg)
	      && (GET_MODE_SIZE (GET_MODE (reg))
		  >= GET_MODE_SIZE (GET_MODE (op))))
	    set_unique_reg_note (emit_insn_before (gen_rtx_USE (VOIDmode, reg),
						   insn),
				 REG_EQUAL, reg_equiv_memory_loc[REGNO (reg)]);

	  substed_operand[i] = recog_data.operand[i] = op;
	}
      else if (code == PLUS || GET_RTX_CLASS (code) == RTX_UNARY)
	/* We can get a PLUS as an "operand" as a result of register
	   elimination.  See eliminate_regs and gen_reload.  We handle
	   a unary operator by reloading the operand.  */
	substed_operand[i] = recog_data.operand[i]
	  = find_reloads_toplev (recog_data.operand[i], i, address_type[i],
				 ind_levels, 0, insn,
				 &address_reloaded[i]);
      else if (code == REG)
	{
	  /* This is equivalent to calling find_reloads_toplev.
	     The code is duplicated for speed.
	     When we find a pseudo always equivalent to a constant,
	     we replace it by the constant.  We must be sure, however,
	     that we don't try to replace it in the insn in which it
	     is being set.  */
	  int regno = REGNO (recog_data.operand[i]);
	  if (reg_equiv_constant[regno] != 0
	      && (set == 0 || &SET_DEST (set) != recog_data.operand_loc[i]))
	    {
	      /* Record the existing mode so that the check if constants are
		 allowed will work when operand_mode isn't specified.  */

	      if (operand_mode[i] == VOIDmode)
		operand_mode[i] = GET_MODE (recog_data.operand[i]);

	      substed_operand[i] = recog_data.operand[i]
		= reg_equiv_constant[regno];
	    }
	  if (reg_equiv_memory_loc[regno] != 0
	      && (reg_equiv_address[regno] != 0 || num_not_at_initial_offset))
	    /* We need not give a valid is_set_dest argument since the case
	       of a constant equivalence was checked above.  */
	    substed_operand[i] = recog_data.operand[i]
	      = find_reloads_toplev (recog_data.operand[i], i, address_type[i],
				     ind_levels, 0, insn,
				     &address_reloaded[i]);
	}
      /* If the operand is still a register (we didn't replace it with an
	 equivalent), get the preferred class to reload it into.  */
      code = GET_CODE (recog_data.operand[i]);
      preferred_class[i]
	= ((code == REG && REGNO (recog_data.operand[i])
	    >= FIRST_PSEUDO_REGISTER)
	   ? reg_preferred_class (REGNO (recog_data.operand[i]))
	   : NO_REGS);
      pref_or_nothing[i]
	= (code == REG
	   && REGNO (recog_data.operand[i]) >= FIRST_PSEUDO_REGISTER
	   && reg_alternate_class (REGNO (recog_data.operand[i])) == NO_REGS);
    }

  /* If this is simply a copy from operand 1 to operand 0, merge the
     preferred classes for the operands.  */
  if (set != 0 && noperands >= 2 && recog_data.operand[0] == SET_DEST (set)
      && recog_data.operand[1] == SET_SRC (set))
    {
      preferred_class[0] = preferred_class[1]
	= reg_class_subunion[(int) preferred_class[0]][(int) preferred_class[1]];
      pref_or_nothing[0] |= pref_or_nothing[1];
      pref_or_nothing[1] |= pref_or_nothing[0];
    }

  /* Now see what we need for pseudo-regs that didn't get hard regs
     or got the wrong kind of hard reg.  For this, we must consider
     all the operands together against the register constraints.  */

  best = MAX_RECOG_OPERANDS * 2 + 600;

  swapped = 0;
  goal_alternative_swapped = 0;
 try_swapped:

  /* The constraints are made of several alternatives.
     Each operand's constraint looks like foo,bar,... with commas
     separating the alternatives.  The first alternatives for all
     operands go together, the second alternatives go together, etc.

     First loop over alternatives.  */

  for (this_alternative_number = 0;
       this_alternative_number < n_alternatives;
       this_alternative_number++)
    {
      /* Loop over operands for one constraint alternative.  */
      /* LOSERS counts those that don't fit this alternative
	 and would require loading.  */
      int losers = 0;
      /* BAD is set to 1 if it some operand can't fit this alternative
	 even after reloading.  */
      int bad = 0;
      /* REJECT is a count of how undesirable this alternative says it is
	 if any reloading is required.  If the alternative matches exactly
	 then REJECT is ignored, but otherwise it gets this much
	 counted against it in addition to the reloading needed.  Each
	 ? counts three times here since we want the disparaging caused by
	 a bad register class to only count 1/3 as much.  */
      int reject = 0;

      this_earlyclobber = 0;

      for (i = 0; i < noperands; i++)
	{
	  char *p = constraints[i];
	  char *end;
	  int len;
	  int win = 0;
	  int did_match = 0;
	  /* 0 => this operand can be reloaded somehow for this alternative.  */
	  int badop = 1;
	  /* 0 => this operand can be reloaded if the alternative allows regs.  */
	  int winreg = 0;
	  int c;
	  int m;
	  rtx operand = recog_data.operand[i];
	  int offset = 0;
	  /* Nonzero means this is a MEM that must be reloaded into a reg
	     regardless of what the constraint says.  */
	  int force_reload = 0;
	  int offmemok = 0;
	  /* Nonzero if a constant forced into memory would be OK for this
	     operand.  */
	  int constmemok = 0;
	  int earlyclobber = 0;

	  /* If the predicate accepts a unary operator, it means that
	     we need to reload the operand, but do not do this for
	     match_operator and friends.  */
	  if (UNARY_P (operand) && *p != 0)
	    operand = XEXP (operand, 0);

	  /* If the operand is a SUBREG, extract
	     the REG or MEM (or maybe even a constant) within.
	     (Constants can occur as a result of reg_equiv_constant.)  */

	  while (GET_CODE (operand) == SUBREG)
	    {
	      /* Offset only matters when operand is a REG and
		 it is a hard reg.  This is because it is passed
		 to reg_fits_class_p if it is a REG and all pseudos
		 return 0 from that function.  */
	      if (REG_P (SUBREG_REG (operand))
		  && REGNO (SUBREG_REG (operand)) < FIRST_PSEUDO_REGISTER)
		{
		  if (!subreg_offset_representable_p
			(REGNO (SUBREG_REG (operand)),
			 GET_MODE (SUBREG_REG (operand)),
			 SUBREG_BYTE (operand),
			 GET_MODE (operand)))
		     force_reload = 1;
		  offset += subreg_regno_offset (REGNO (SUBREG_REG (operand)),
						 GET_MODE (SUBREG_REG (operand)),
						 SUBREG_BYTE (operand),
						 GET_MODE (operand));
		}
	      operand = SUBREG_REG (operand);
	      /* Force reload if this is a constant or PLUS or if there may
		 be a problem accessing OPERAND in the outer mode.  */
	      if (CONSTANT_P (operand)
		  || GET_CODE (operand) == PLUS
		  /* We must force a reload of paradoxical SUBREGs
		     of a MEM because the alignment of the inner value
		     may not be enough to do the outer reference.  On
		     big-endian machines, it may also reference outside
		     the object.

		     On machines that extend byte operations and we have a
		     SUBREG where both the inner and outer modes are no wider
		     than a word and the inner mode is narrower, is integral,
		     and gets extended when loaded from memory, combine.c has
		     made assumptions about the behavior of the machine in such
		     register access.  If the data is, in fact, in memory we
		     must always load using the size assumed to be in the
		     register and let the insn do the different-sized
		     accesses.

		     This is doubly true if WORD_REGISTER_OPERATIONS.  In
		     this case eliminate_regs has left non-paradoxical
		     subregs for push_reload to see.  Make sure it does
		     by forcing the reload.

		     ??? When is it right at this stage to have a subreg
		     of a mem that is _not_ to be handled specially?  IMO
		     those should have been reduced to just a mem.  */
		  || ((MEM_P (operand)
		       || (REG_P (operand)
			   && REGNO (operand) >= FIRST_PSEUDO_REGISTER))
#ifndef WORD_REGISTER_OPERATIONS
		      && (((GET_MODE_BITSIZE (GET_MODE (operand))
			    < BIGGEST_ALIGNMENT)
			   && (GET_MODE_SIZE (operand_mode[i])
			       > GET_MODE_SIZE (GET_MODE (operand))))
			  || BYTES_BIG_ENDIAN
#ifdef LOAD_EXTEND_OP
			  || (GET_MODE_SIZE (operand_mode[i]) <= UNITS_PER_WORD
			      && (GET_MODE_SIZE (GET_MODE (operand))
				  <= UNITS_PER_WORD)
			      && (GET_MODE_SIZE (operand_mode[i])
				  > GET_MODE_SIZE (GET_MODE (operand)))
			      && INTEGRAL_MODE_P (GET_MODE (operand))
			      && LOAD_EXTEND_OP (GET_MODE (operand)) != UNKNOWN)
#endif
			  )
#endif
		      )
		  )
		force_reload = 1;
	    }

	  this_alternative[i] = (int) NO_REGS;
	  this_alternative_win[i] = 0;
	  this_alternative_match_win[i] = 0;
	  this_alternative_offmemok[i] = 0;
	  this_alternative_earlyclobber[i] = 0;
	  this_alternative_matches[i] = -1;

	  /* An empty constraint or empty alternative
	     allows anything which matched the pattern.  */
	  if (*p == 0 || *p == ',')
	    win = 1, badop = 0;

	  /* Scan this alternative's specs for this operand;
	     set WIN if the operand fits any letter in this alternative.
	     Otherwise, clear BADOP if this operand could
	     fit some letter after reloads,
	     or set WINREG if this operand could fit after reloads
	     provided the constraint allows some registers.  */

	  do
	    switch ((c = *p, len = CONSTRAINT_LEN (c, p)), c)
	      {
	      case '\0':
		len = 0;
		break;
	      case ',':
		c = '\0';
		break;

	      case '=':  case '+':  case '*':
		break;

	      case '%':
		/* We only support one commutative marker, the first
		   one.  We already set commutative above.  */
		break;

	      case '?':
		reject += 6;
		break;

	      case '!':
		reject = 600;
		break;

	      case '#':
		/* Ignore rest of this alternative as far as
		   reloading is concerned.  */
		do
		  p++;
		while (*p && *p != ',');
		len = 0;
		break;

	      case '0':  case '1':  case '2':  case '3':  case '4':
	      case '5':  case '6':  case '7':  case '8':  case '9':
		m = strtoul (p, &end, 10);
		p = end;
		len = 0;

		this_alternative_matches[i] = m;
		/* We are supposed to match a previous operand.
		   If we do, we win if that one did.
		   If we do not, count both of the operands as losers.
		   (This is too conservative, since most of the time
		   only a single reload insn will be needed to make
		   the two operands win.  As a result, this alternative
		   may be rejected when it is actually desirable.)  */
		if ((swapped && (m != commutative || i != commutative + 1))
		    /* If we are matching as if two operands were swapped,
		       also pretend that operands_match had been computed
		       with swapped.
		       But if I is the second of those and C is the first,
		       don't exchange them, because operands_match is valid
		       only on one side of its diagonal.  */
		    ? (operands_match
		       [(m == commutative || m == commutative + 1)
		       ? 2 * commutative + 1 - m : m]
		       [(i == commutative || i == commutative + 1)
		       ? 2 * commutative + 1 - i : i])
		    : operands_match[m][i])
		  {
		    /* If we are matching a non-offsettable address where an
		       offsettable address was expected, then we must reject
		       this combination, because we can't reload it.  */
		    if (this_alternative_offmemok[m]
			&& MEM_P (recog_data.operand[m])
			&& this_alternative[m] == (int) NO_REGS
			&& ! this_alternative_win[m])
		      bad = 1;

		    did_match = this_alternative_win[m];
		  }
		else
		  {
		    /* Operands don't match.  */
		    rtx value;
		    int loc1, loc2;
		    /* Retroactively mark the operand we had to match
		       as a loser, if it wasn't already.  */
		    if (this_alternative_win[m])
		      losers++;
		    this_alternative_win[m] = 0;
		    if (this_alternative[m] == (int) NO_REGS)
		      bad = 1;
		    /* But count the pair only once in the total badness of
		       this alternative, if the pair can be a dummy reload.
		       The pointers in operand_loc are not swapped; swap
		       them by hand if necessary.  */
		    if (swapped && i == commutative)
		      loc1 = commutative + 1;
		    else if (swapped && i == commutative + 1)
		      loc1 = commutative;
		    else
		      loc1 = i;
		    if (swapped && m == commutative)
		      loc2 = commutative + 1;
		    else if (swapped && m == commutative + 1)
		      loc2 = commutative;
		    else
		      loc2 = m;
		    value
		      = find_dummy_reload (recog_data.operand[i],
					   recog_data.operand[m],
					   recog_data.operand_loc[loc1],
					   recog_data.operand_loc[loc2],
					   operand_mode[i], operand_mode[m],
					   this_alternative[m], -1,
					   this_alternative_earlyclobber[m]);

		    if (value != 0)
		      losers--;
		  }
		/* This can be fixed with reloads if the operand
		   we are supposed to match can be fixed with reloads.  */
		badop = 0;
		this_alternative[i] = this_alternative[m];

		/* If we have to reload this operand and some previous
		   operand also had to match the same thing as this
		   operand, we don't know how to do that.  So reject this
		   alternative.  */
		if (! did_match || force_reload)
		  for (j = 0; j < i; j++)
		    if (this_alternative_matches[j]
			== this_alternative_matches[i])
		      badop = 1;
		break;

	      case 'p':
		/* All necessary reloads for an address_operand
		   were handled in find_reloads_address.  */
		this_alternative[i]
		  = (int) base_reg_class (VOIDmode, ADDRESS, SCRATCH);
		win = 1;
		badop = 0;
		break;

	      case 'm':
		if (force_reload)
		  break;
		if (MEM_P (operand)
		    || (REG_P (operand)
			&& REGNO (operand) >= FIRST_PSEUDO_REGISTER
			&& reg_renumber[REGNO (operand)] < 0))
		  win = 1;
		if (CONST_POOL_OK_P (operand))
		  badop = 0;
		constmemok = 1;
		break;

	      case '<':
		if (MEM_P (operand)
		    && ! address_reloaded[i]
		    && (GET_CODE (XEXP (operand, 0)) == PRE_DEC
			|| GET_CODE (XEXP (operand, 0)) == POST_DEC))
		  win = 1;
		break;

	      case '>':
		if (MEM_P (operand)
		    && ! address_reloaded[i]
		    && (GET_CODE (XEXP (operand, 0)) == PRE_INC
			|| GET_CODE (XEXP (operand, 0)) == POST_INC))
		  win = 1;
		break;

		/* Memory operand whose address is not offsettable.  */
	      case 'V':
		if (force_reload)
		  break;
		if (MEM_P (operand)
		    && ! (ind_levels ? offsettable_memref_p (operand)
			  : offsettable_nonstrict_memref_p (operand))
		    /* Certain mem addresses will become offsettable
		       after they themselves are reloaded.  This is important;
		       we don't want our own handling of unoffsettables
		       to override the handling of reg_equiv_address.  */
		    && !(REG_P (XEXP (operand, 0))
			 && (ind_levels == 0
			     || reg_equiv_address[REGNO (XEXP (operand, 0))] != 0)))
		  win = 1;
		break;

		/* Memory operand whose address is offsettable.  */
	      case 'o':
		if (force_reload)
		  break;
		if ((MEM_P (operand)
		     /* If IND_LEVELS, find_reloads_address won't reload a
			pseudo that didn't get a hard reg, so we have to
			reject that case.  */
		     && ((ind_levels ? offsettable_memref_p (operand)
			  : offsettable_nonstrict_memref_p (operand))
			 /* A reloaded address is offsettable because it is now
			    just a simple register indirect.  */
			 || address_reloaded[i] == 1))
		    || (REG_P (operand)
			&& REGNO (operand) >= FIRST_PSEUDO_REGISTER
			&& reg_renumber[REGNO (operand)] < 0
			/* If reg_equiv_address is nonzero, we will be
			   loading it into a register; hence it will be
			   offsettable, but we cannot say that reg_equiv_mem
			   is offsettable without checking.  */
			&& ((reg_equiv_mem[REGNO (operand)] != 0
			     && offsettable_memref_p (reg_equiv_mem[REGNO (operand)]))
			    || (reg_equiv_address[REGNO (operand)] != 0))))
		  win = 1;
		if (CONST_POOL_OK_P (operand)
		    || MEM_P (operand))
		  badop = 0;
		constmemok = 1;
		offmemok = 1;
		break;

	      case '&':
		/* Output operand that is stored before the need for the
		   input operands (and their index registers) is over.  */
		earlyclobber = 1, this_earlyclobber = 1;
		break;

	      case 'E':
	      case 'F':
		if (GET_CODE (operand) == CONST_DOUBLE
		    || (GET_CODE (operand) == CONST_VECTOR
			&& (GET_MODE_CLASS (GET_MODE (operand))
			    == MODE_VECTOR_FLOAT)))
		  win = 1;
		break;

	      case 'G':
	      case 'H':
		if (GET_CODE (operand) == CONST_DOUBLE
		    && CONST_DOUBLE_OK_FOR_CONSTRAINT_P (operand, c, p))
		  win = 1;
		break;

	      case 's':
		if (GET_CODE (operand) == CONST_INT
		    || (GET_CODE (operand) == CONST_DOUBLE
			&& GET_MODE (operand) == VOIDmode))
		  break;
	      case 'i':
		if (CONSTANT_P (operand)
		    && (! flag_pic || LEGITIMATE_PIC_OPERAND_P (operand)))
		  win = 1;
		break;

	      case 'n':
		if (GET_CODE (operand) == CONST_INT
		    || (GET_CODE (operand) == CONST_DOUBLE
			&& GET_MODE (operand) == VOIDmode))
		  win = 1;
		break;

	      case 'I':
	      case 'J':
	      case 'K':
	      case 'L':
	      case 'M':
	      case 'N':
	      case 'O':
	      case 'P':
		if (GET_CODE (operand) == CONST_INT
		    && CONST_OK_FOR_CONSTRAINT_P (INTVAL (operand), c, p))
		  win = 1;
		break;

	      case 'X':
		force_reload = 0;
		win = 1;
		break;

	      case 'g':
		if (! force_reload
		    /* A PLUS is never a valid operand, but reload can make
		       it from a register when eliminating registers.  */
		    && GET_CODE (operand) != PLUS
		    /* A SCRATCH is not a valid operand.  */
		    && GET_CODE (operand) != SCRATCH
		    && (! CONSTANT_P (operand)
			|| ! flag_pic
			|| LEGITIMATE_PIC_OPERAND_P (operand))
		    && (GENERAL_REGS == ALL_REGS
			|| !REG_P (operand)
			|| (REGNO (operand) >= FIRST_PSEUDO_REGISTER
			    && reg_renumber[REGNO (operand)] < 0)))
		  win = 1;
		/* Drop through into 'r' case.  */

	      case 'r':
		this_alternative[i]
		  = (int) reg_class_subunion[this_alternative[i]][(int) GENERAL_REGS];
		goto reg;

	      default:
		if (REG_CLASS_FROM_CONSTRAINT (c, p) == NO_REGS)
		  {
#ifdef EXTRA_CONSTRAINT_STR
		    if (EXTRA_MEMORY_CONSTRAINT (c, p))
		      {
			if (force_reload)
			  break;
		        if (EXTRA_CONSTRAINT_STR (operand, c, p))
		          win = 1;
			/* If the address was already reloaded,
			   we win as well.  */
			else if (MEM_P (operand)
				 && address_reloaded[i] == 1)
			  win = 1;
			/* Likewise if the address will be reloaded because
			   reg_equiv_address is nonzero.  For reg_equiv_mem
			   we have to check.  */
		        else if (REG_P (operand)
				 && REGNO (operand) >= FIRST_PSEUDO_REGISTER
				 && reg_renumber[REGNO (operand)] < 0
				 && ((reg_equiv_mem[REGNO (operand)] != 0
				      && EXTRA_CONSTRAINT_STR (reg_equiv_mem[REGNO (operand)], c, p))
				     || (reg_equiv_address[REGNO (operand)] != 0)))
			  win = 1;

			/* If we didn't already win, we can reload
			   constants via force_const_mem, and other
			   MEMs by reloading the address like for 'o'.  */
			if (CONST_POOL_OK_P (operand)
			    || MEM_P (operand))
			  badop = 0;
			constmemok = 1;
			offmemok = 1;
			break;
		      }
		    if (EXTRA_ADDRESS_CONSTRAINT (c, p))
		      {
		        if (EXTRA_CONSTRAINT_STR (operand, c, p))
		          win = 1;

			/* If we didn't already win, we can reload
			   the address into a base register.  */
			this_alternative[i]
			  = (int) base_reg_class (VOIDmode, ADDRESS, SCRATCH);
			badop = 0;
			break;
		      }

		    if (EXTRA_CONSTRAINT_STR (operand, c, p))
		      win = 1;
#endif
		    break;
		  }

		this_alternative[i]
		  = (int) (reg_class_subunion
			   [this_alternative[i]]
			   [(int) REG_CLASS_FROM_CONSTRAINT (c, p)]);
	      reg:
		if (GET_MODE (operand) == BLKmode)
		  break;
		winreg = 1;
		if (REG_P (operand)
		    && reg_fits_class_p (operand, this_alternative[i],
					 offset, GET_MODE (recog_data.operand[i])))
		  win = 1;
		break;
	      }
	  while ((p += len), c);

	  constraints[i] = p;

	  /* If this operand could be handled with a reg,
	     and some reg is allowed, then this operand can be handled.  */
	  if (winreg && this_alternative[i] != (int) NO_REGS)
	    badop = 0;

	  /* Record which operands fit this alternative.  */
	  this_alternative_earlyclobber[i] = earlyclobber;
	  if (win && ! force_reload)
	    this_alternative_win[i] = 1;
	  else if (did_match && ! force_reload)
	    this_alternative_match_win[i] = 1;
	  else
	    {
	      int const_to_mem = 0;

	      this_alternative_offmemok[i] = offmemok;
	      losers++;
	      if (badop)
		bad = 1;
	      /* Alternative loses if it has no regs for a reg operand.  */
	      if (REG_P (operand)
		  && this_alternative[i] == (int) NO_REGS
		  && this_alternative_matches[i] < 0)
		bad = 1;

	      /* If this is a constant that is reloaded into the desired
		 class by copying it to memory first, count that as another
		 reload.  This is consistent with other code and is
		 required to avoid choosing another alternative when
		 the constant is moved into memory by this function on
		 an early reload pass.  Note that the test here is
		 precisely the same as in the code below that calls
		 force_const_mem.  */
	      if (CONST_POOL_OK_P (operand)
		  && ((PREFERRED_RELOAD_CLASS (operand,
					       (enum reg_class) this_alternative[i])
		       == NO_REGS)
		      || no_input_reloads)
		  && operand_mode[i] != VOIDmode)
		{
		  const_to_mem = 1;
		  if (this_alternative[i] != (int) NO_REGS)
		    losers++;
		}

	      /* Alternative loses if it requires a type of reload not
		 permitted for this insn.  We can always reload SCRATCH
		 and objects with a REG_UNUSED note.  */
	      if (GET_CODE (operand) != SCRATCH
		       && modified[i] != RELOAD_READ && no_output_reloads
		       && ! find_reg_note (insn, REG_UNUSED, operand))
		bad = 1;
	      else if (modified[i] != RELOAD_WRITE && no_input_reloads
		       && ! const_to_mem)
		bad = 1;

	      /* If we can't reload this value at all, reject this
		 alternative.  Note that we could also lose due to
		 LIMIT_RELOAD_CLASS, but we don't check that
		 here.  */

	      if (! CONSTANT_P (operand)
		  && (enum reg_class) this_alternative[i] != NO_REGS)
		{
		  if (PREFERRED_RELOAD_CLASS
			(operand, (enum reg_class) this_alternative[i])
		      == NO_REGS)
		    reject = 600;

#ifdef PREFERRED_OUTPUT_RELOAD_CLASS
		  if (operand_type[i] == RELOAD_FOR_OUTPUT
		      && PREFERRED_OUTPUT_RELOAD_CLASS
			   (operand, (enum reg_class) this_alternative[i])
		         == NO_REGS)
		    reject = 600;
#endif
		}

	      /* We prefer to reload pseudos over reloading other things,
		 since such reloads may be able to be eliminated later.
		 If we are reloading a SCRATCH, we won't be generating any
		 insns, just using a register, so it is also preferred.
		 So bump REJECT in other cases.  Don't do this in the
		 case where we are forcing a constant into memory and
		 it will then win since we don't want to have a different
		 alternative match then.  */
	      if (! (REG_P (operand)
		     && REGNO (operand) >= FIRST_PSEUDO_REGISTER)
		  && GET_CODE (operand) != SCRATCH
		  && ! (const_to_mem && constmemok))
		reject += 2;

	      /* Input reloads can be inherited more often than output
		 reloads can be removed, so penalize output reloads.  */
	      if (operand_type[i] != RELOAD_FOR_INPUT
		  && GET_CODE (operand) != SCRATCH)
		reject++;
	    }

	  /* If this operand is a pseudo register that didn't get a hard
	     reg and this alternative accepts some register, see if the
	     class that we want is a subset of the preferred class for this
	     register.  If not, but it intersects that class, use the
	     preferred class instead.  If it does not intersect the preferred
	     class, show that usage of this alternative should be discouraged;
	     it will be discouraged more still if the register is `preferred
	     or nothing'.  We do this because it increases the chance of
	     reusing our spill register in a later insn and avoiding a pair
	     of memory stores and loads.

	     Don't bother with this if this alternative will accept this
	     operand.

	     Don't do this for a multiword operand, since it is only a
	     small win and has the risk of requiring more spill registers,
	     which could cause a large loss.

	     Don't do this if the preferred class has only one register
	     because we might otherwise exhaust the class.  */

	  if (! win && ! did_match
	      && this_alternative[i] != (int) NO_REGS
	      && GET_MODE_SIZE (operand_mode[i]) <= UNITS_PER_WORD
	      && reg_class_size [(int) preferred_class[i]] > 0
	      && ! SMALL_REGISTER_CLASS_P (preferred_class[i]))
	    {
	      if (! reg_class_subset_p (this_alternative[i],
					preferred_class[i]))
		{
		  /* Since we don't have a way of forming the intersection,
		     we just do something special if the preferred class
		     is a subset of the class we have; that's the most
		     common case anyway.  */
		  if (reg_class_subset_p (preferred_class[i],
					  this_alternative[i]))
		    this_alternative[i] = (int) preferred_class[i];
		  else
		    reject += (2 + 2 * pref_or_nothing[i]);
		}
	    }
	}

      /* Now see if any output operands that are marked "earlyclobber"
	 in this alternative conflict with any input operands
	 or any memory addresses.  */

      for (i = 0; i < noperands; i++)
	if (this_alternative_earlyclobber[i]
	    && (this_alternative_win[i] || this_alternative_match_win[i]))
	  {
	    struct decomposition early_data;

	    early_data = decompose (recog_data.operand[i]);

	    gcc_assert (modified[i] != RELOAD_READ);

	    if (this_alternative[i] == NO_REGS)
	      {
		this_alternative_earlyclobber[i] = 0;
		gcc_assert (this_insn_is_asm);
		error_for_asm (this_insn,
			       "%<&%> constraint used with no register class");
	      }

	    for (j = 0; j < noperands; j++)
	      /* Is this an input operand or a memory ref?  */
	      if ((MEM_P (recog_data.operand[j])
		   || modified[j] != RELOAD_WRITE)
		  && j != i
		  /* Ignore things like match_operator operands.  */
		  && *recog_data.constraints[j] != 0
		  /* Don't count an input operand that is constrained to match
		     the early clobber operand.  */
		  && ! (this_alternative_matches[j] == i
			&& rtx_equal_p (recog_data.operand[i],
					recog_data.operand[j]))
		  /* Is it altered by storing the earlyclobber operand?  */
		  && !immune_p (recog_data.operand[j], recog_data.operand[i],
				early_data))
		{
		  /* If the output is in a non-empty few-regs class,
		     it's costly to reload it, so reload the input instead.  */
		  if (SMALL_REGISTER_CLASS_P (this_alternative[i])
		      && (REG_P (recog_data.operand[j])
			  || GET_CODE (recog_data.operand[j]) == SUBREG))
		    {
		      losers++;
		      this_alternative_win[j] = 0;
		      this_alternative_match_win[j] = 0;
		    }
		  else
		    break;
		}
	    /* If an earlyclobber operand conflicts with something,
	       it must be reloaded, so request this and count the cost.  */
	    if (j != noperands)
	      {
		losers++;
		this_alternative_win[i] = 0;
		this_alternative_match_win[j] = 0;
		for (j = 0; j < noperands; j++)
		  if (this_alternative_matches[j] == i
		      && this_alternative_match_win[j])
		    {
		      this_alternative_win[j] = 0;
		      this_alternative_match_win[j] = 0;
		      losers++;
		    }
	      }
	  }

      /* If one alternative accepts all the operands, no reload required,
	 choose that alternative; don't consider the remaining ones.  */
      if (losers == 0)
	{
	  /* Unswap these so that they are never swapped at `finish'.  */
	  if (commutative >= 0)
	    {
	      recog_data.operand[commutative] = substed_operand[commutative];
	      recog_data.operand[commutative + 1]
		= substed_operand[commutative + 1];
	    }
	  for (i = 0; i < noperands; i++)
	    {
	      goal_alternative_win[i] = this_alternative_win[i];
	      goal_alternative_match_win[i] = this_alternative_match_win[i];
	      goal_alternative[i] = this_alternative[i];
	      goal_alternative_offmemok[i] = this_alternative_offmemok[i];
	      goal_alternative_matches[i] = this_alternative_matches[i];
	      goal_alternative_earlyclobber[i]
		= this_alternative_earlyclobber[i];
	    }
	  goal_alternative_number = this_alternative_number;
	  goal_alternative_swapped = swapped;
	  goal_earlyclobber = this_earlyclobber;
	  goto finish;
	}

      /* REJECT, set by the ! and ? constraint characters and when a register
	 would be reloaded into a non-preferred class, discourages the use of
	 this alternative for a reload goal.  REJECT is incremented by six
	 for each ? and two for each non-preferred class.  */
      losers = losers * 6 + reject;

      /* If this alternative can be made to work by reloading,
	 and it needs less reloading than the others checked so far,
	 record it as the chosen goal for reloading.  */
      if (! bad && best > losers)
	{
	  for (i = 0; i < noperands; i++)
	    {
	      goal_alternative[i] = this_alternative[i];
	      goal_alternative_win[i] = this_alternative_win[i];
	      goal_alternative_match_win[i] = this_alternative_match_win[i];
	      goal_alternative_offmemok[i] = this_alternative_offmemok[i];
	      goal_alternative_matches[i] = this_alternative_matches[i];
	      goal_alternative_earlyclobber[i]
		= this_alternative_earlyclobber[i];
	    }
	  goal_alternative_swapped = swapped;
	  best = losers;
	  goal_alternative_number = this_alternative_number;
	  goal_earlyclobber = this_earlyclobber;
	}
    }

  /* If insn is commutative (it's safe to exchange a certain pair of operands)
     then we need to try each alternative twice,
     the second time matching those two operands
     as if we had exchanged them.
     To do this, really exchange them in operands.

     If we have just tried the alternatives the second time,
     return operands to normal and drop through.  */

  if (commutative >= 0)
    {
      swapped = !swapped;
      if (swapped)
	{
	  enum reg_class tclass;
	  int t;

	  recog_data.operand[commutative] = substed_operand[commutative + 1];
	  recog_data.operand[commutative + 1] = substed_operand[commutative];
	  /* Swap the duplicates too.  */
	  for (i = 0; i < recog_data.n_dups; i++)
	    if (recog_data.dup_num[i] == commutative
		|| recog_data.dup_num[i] == commutative + 1)
	      *recog_data.dup_loc[i]
		 = recog_data.operand[(int) recog_data.dup_num[i]];

	  tclass = preferred_class[commutative];
	  preferred_class[commutative] = preferred_class[commutative + 1];
	  preferred_class[commutative + 1] = tclass;

	  t = pref_or_nothing[commutative];
	  pref_or_nothing[commutative] = pref_or_nothing[commutative + 1];
	  pref_or_nothing[commutative + 1] = t;

	  t = address_reloaded[commutative];
	  address_reloaded[commutative] = address_reloaded[commutative + 1];
	  address_reloaded[commutative + 1] = t;

	  memcpy (constraints, recog_data.constraints,
		  noperands * sizeof (char *));
	  goto try_swapped;
	}
      else
	{
	  recog_data.operand[commutative] = substed_operand[commutative];
	  recog_data.operand[commutative + 1]
	    = substed_operand[commutative + 1];
	  /* Unswap the duplicates too.  */
	  for (i = 0; i < recog_data.n_dups; i++)
	    if (recog_data.dup_num[i] == commutative
		|| recog_data.dup_num[i] == commutative + 1)
	      *recog_data.dup_loc[i]
		 = recog_data.operand[(int) recog_data.dup_num[i]];
	}
    }

  /* The operands don't meet the constraints.
     goal_alternative describes the alternative
     that we could reach by reloading the fewest operands.
     Reload so as to fit it.  */

  if (best == MAX_RECOG_OPERANDS * 2 + 600)
    {
      /* No alternative works with reloads??  */
      if (insn_code_number >= 0)
	fatal_insn ("unable to generate reloads for:", insn);
      error_for_asm (insn, "inconsistent operand constraints in an %<asm%>");
      /* Avoid further trouble with this insn.  */
      PATTERN (insn) = gen_rtx_USE (VOIDmode, const0_rtx);
      n_reloads = 0;
      return 0;
    }

  /* Jump to `finish' from above if all operands are valid already.
     In that case, goal_alternative_win is all 1.  */
 finish:

  /* Right now, for any pair of operands I and J that are required to match,
     with I < J,
     goal_alternative_matches[J] is I.
     Set up goal_alternative_matched as the inverse function:
     goal_alternative_matched[I] = J.  */

  for (i = 0; i < noperands; i++)
    goal_alternative_matched[i] = -1;

  for (i = 0; i < noperands; i++)
    if (! goal_alternative_win[i]
	&& goal_alternative_matches[i] >= 0)
      goal_alternative_matched[goal_alternative_matches[i]] = i;

  for (i = 0; i < noperands; i++)
    goal_alternative_win[i] |= goal_alternative_match_win[i];

  /* If the best alternative is with operands 1 and 2 swapped,
     consider them swapped before reporting the reloads.  Update the
     operand numbers of any reloads already pushed.  */

  if (goal_alternative_swapped)
    {
      rtx tem;

      tem = substed_operand[commutative];
      substed_operand[commutative] = substed_operand[commutative + 1];
      substed_operand[commutative + 1] = tem;
      tem = recog_data.operand[commutative];
      recog_data.operand[commutative] = recog_data.operand[commutative + 1];
      recog_data.operand[commutative + 1] = tem;
      tem = *recog_data.operand_loc[commutative];
      *recog_data.operand_loc[commutative]
	= *recog_data.operand_loc[commutative + 1];
      *recog_data.operand_loc[commutative + 1] = tem;

      for (i = 0; i < n_reloads; i++)
	{
	  if (rld[i].opnum == commutative)
	    rld[i].opnum = commutative + 1;
	  else if (rld[i].opnum == commutative + 1)
	    rld[i].opnum = commutative;
	}
    }

  for (i = 0; i < noperands; i++)
    {
      operand_reloadnum[i] = -1;

      /* If this is an earlyclobber operand, we need to widen the scope.
	 The reload must remain valid from the start of the insn being
	 reloaded until after the operand is stored into its destination.
	 We approximate this with RELOAD_OTHER even though we know that we
	 do not conflict with RELOAD_FOR_INPUT_ADDRESS reloads.

	 One special case that is worth checking is when we have an
	 output that is earlyclobber but isn't used past the insn (typically
	 a SCRATCH).  In this case, we only need have the reload live
	 through the insn itself, but not for any of our input or output
	 reloads.
	 But we must not accidentally narrow the scope of an existing
	 RELOAD_OTHER reload - leave these alone.

	 In any case, anything needed to address this operand can remain
	 however they were previously categorized.  */

      if (goal_alternative_earlyclobber[i] && operand_type[i] != RELOAD_OTHER)
	operand_type[i]
	  = (find_reg_note (insn, REG_UNUSED, recog_data.operand[i])
	     ? RELOAD_FOR_INSN : RELOAD_OTHER);
    }

  /* Any constants that aren't allowed and can't be reloaded
     into registers are here changed into memory references.  */
  for (i = 0; i < noperands; i++)
    if (! goal_alternative_win[i]
	&& CONST_POOL_OK_P (recog_data.operand[i])
	&& ((PREFERRED_RELOAD_CLASS (recog_data.operand[i],
				     (enum reg_class) goal_alternative[i])
	     == NO_REGS)
	    || no_input_reloads)
	&& operand_mode[i] != VOIDmode)
      {
	substed_operand[i] = recog_data.operand[i]
	  = find_reloads_toplev (force_const_mem (operand_mode[i],
						  recog_data.operand[i]),
				 i, address_type[i], ind_levels, 0, insn,
				 NULL);
	if (alternative_allows_memconst (recog_data.constraints[i],
					 goal_alternative_number))
	  goal_alternative_win[i] = 1;
      }

  /* Likewise any invalid constants appearing as operand of a PLUS
     that is to be reloaded.  */
  for (i = 0; i < noperands; i++)
    if (! goal_alternative_win[i]
	&& GET_CODE (recog_data.operand[i]) == PLUS
	&& CONST_POOL_OK_P (XEXP (recog_data.operand[i], 1))
	&& (PREFERRED_RELOAD_CLASS (XEXP (recog_data.operand[i], 1),
				    (enum reg_class) goal_alternative[i])
	     == NO_REGS)
	&& operand_mode[i] != VOIDmode)
      {
	rtx tem = force_const_mem (operand_mode[i],
				   XEXP (recog_data.operand[i], 1));
	tem = gen_rtx_PLUS (operand_mode[i],
			    XEXP (recog_data.operand[i], 0), tem);

	substed_operand[i] = recog_data.operand[i]
	  = find_reloads_toplev (tem, i, address_type[i],
				 ind_levels, 0, insn, NULL);
      }

  /* Record the values of the earlyclobber operands for the caller.  */
  if (goal_earlyclobber)
    for (i = 0; i < noperands; i++)
      if (goal_alternative_earlyclobber[i])
	reload_earlyclobbers[n_earlyclobbers++] = recog_data.operand[i];

  /* Now record reloads for all the operands that need them.  */
  for (i = 0; i < noperands; i++)
    if (! goal_alternative_win[i])
      {
	/* Operands that match previous ones have already been handled.  */
	if (goal_alternative_matches[i] >= 0)
	  ;
	/* Handle an operand with a nonoffsettable address
	   appearing where an offsettable address will do
	   by reloading the address into a base register.

	   ??? We can also do this when the operand is a register and
	   reg_equiv_mem is not offsettable, but this is a bit tricky,
	   so we don't bother with it.  It may not be worth doing.  */
	else if (goal_alternative_matched[i] == -1
		 && goal_alternative_offmemok[i]
		 && MEM_P (recog_data.operand[i]))
	  {
	    /* If the address to be reloaded is a VOIDmode constant,
	       use Pmode as mode of the reload register, as would have
	       been done by find_reloads_address.  */
	    enum machine_mode address_mode;
	    address_mode = GET_MODE (XEXP (recog_data.operand[i], 0));
	    if (address_mode == VOIDmode)
	      address_mode = Pmode;

	    operand_reloadnum[i]
	      = push_reload (XEXP (recog_data.operand[i], 0), NULL_RTX,
			     &XEXP (recog_data.operand[i], 0), (rtx*) 0,
			     base_reg_class (VOIDmode, MEM, SCRATCH),
			     address_mode,
			     VOIDmode, 0, 0, i, RELOAD_FOR_INPUT);
	    rld[operand_reloadnum[i]].inc
	      = GET_MODE_SIZE (GET_MODE (recog_data.operand[i]));

	    /* If this operand is an output, we will have made any
	       reloads for its address as RELOAD_FOR_OUTPUT_ADDRESS, but
	       now we are treating part of the operand as an input, so
	       we must change these to RELOAD_FOR_INPUT_ADDRESS.  */

	    if (modified[i] == RELOAD_WRITE)
	      {
		for (j = 0; j < n_reloads; j++)
		  {
		    if (rld[j].opnum == i)
		      {
			if (rld[j].when_needed == RELOAD_FOR_OUTPUT_ADDRESS)
			  rld[j].when_needed = RELOAD_FOR_INPUT_ADDRESS;
			else if (rld[j].when_needed
				 == RELOAD_FOR_OUTADDR_ADDRESS)
			  rld[j].when_needed = RELOAD_FOR_INPADDR_ADDRESS;
		      }
		  }
	      }
	  }
	else if (goal_alternative_matched[i] == -1)
	  {
	    operand_reloadnum[i]
	      = push_reload ((modified[i] != RELOAD_WRITE
			      ? recog_data.operand[i] : 0),
			     (modified[i] != RELOAD_READ
			      ? recog_data.operand[i] : 0),
			     (modified[i] != RELOAD_WRITE
			      ? recog_data.operand_loc[i] : 0),
			     (modified[i] != RELOAD_READ
			      ? recog_data.operand_loc[i] : 0),
			     (enum reg_class) goal_alternative[i],
			     (modified[i] == RELOAD_WRITE
			      ? VOIDmode : operand_mode[i]),
			     (modified[i] == RELOAD_READ
			      ? VOIDmode : operand_mode[i]),
			     (insn_code_number < 0 ? 0
			      : insn_data[insn_code_number].operand[i].strict_low),
			     0, i, operand_type[i]);
	  }
	/* In a matching pair of operands, one must be input only
	   and the other must be output only.
	   Pass the input operand as IN and the other as OUT.  */
	else if (modified[i] == RELOAD_READ
		 && modified[goal_alternative_matched[i]] == RELOAD_WRITE)
	  {
	    operand_reloadnum[i]
	      = push_reload (recog_data.operand[i],
			     recog_data.operand[goal_alternative_matched[i]],
			     recog_data.operand_loc[i],
			     recog_data.operand_loc[goal_alternative_matched[i]],
			     (enum reg_class) goal_alternative[i],
			     operand_mode[i],
			     operand_mode[goal_alternative_matched[i]],
			     0, 0, i, RELOAD_OTHER);
	    operand_reloadnum[goal_alternative_matched[i]] = output_reloadnum;
	  }
	else if (modified[i] == RELOAD_WRITE
		 && modified[goal_alternative_matched[i]] == RELOAD_READ)
	  {
	    operand_reloadnum[goal_alternative_matched[i]]
	      = push_reload (recog_data.operand[goal_alternative_matched[i]],
			     recog_data.operand[i],
			     recog_data.operand_loc[goal_alternative_matched[i]],
			     recog_data.operand_loc[i],
			     (enum reg_class) goal_alternative[i],
			     operand_mode[goal_alternative_matched[i]],
			     operand_mode[i],
			     0, 0, i, RELOAD_OTHER);
	    operand_reloadnum[i] = output_reloadnum;
	  }
	else
	  {
	    gcc_assert (insn_code_number < 0);
	    error_for_asm (insn, "inconsistent operand constraints "
			   "in an %<asm%>");
	    /* Avoid further trouble with this insn.  */
	    PATTERN (insn) = gen_rtx_USE (VOIDmode, const0_rtx);
	    n_reloads = 0;
	    return 0;
	  }
      }
    else if (goal_alternative_matched[i] < 0
	     && goal_alternative_matches[i] < 0
	     && address_operand_reloaded[i] != 1
	     && optimize)
      {
	/* For each non-matching operand that's a MEM or a pseudo-register
	   that didn't get a hard register, make an optional reload.
	   This may get done even if the insn needs no reloads otherwise.  */

	rtx operand = recog_data.operand[i];

	while (GET_CODE (operand) == SUBREG)
	  operand = SUBREG_REG (operand);
	if ((MEM_P (operand)
	     || (REG_P (operand)
		 && REGNO (operand) >= FIRST_PSEUDO_REGISTER))
	    /* If this is only for an output, the optional reload would not
	       actually cause us to use a register now, just note that
	       something is stored here.  */
	    && ((enum reg_class) goal_alternative[i] != NO_REGS
		|| modified[i] == RELOAD_WRITE)
	    && ! no_input_reloads
	    /* An optional output reload might allow to delete INSN later.
	       We mustn't make in-out reloads on insns that are not permitted
	       output reloads.
	       If this is an asm, we can't delete it; we must not even call
	       push_reload for an optional output reload in this case,
	       because we can't be sure that the constraint allows a register,
	       and push_reload verifies the constraints for asms.  */
	    && (modified[i] == RELOAD_READ
		|| (! no_output_reloads && ! this_insn_is_asm)))
	  operand_reloadnum[i]
	    = push_reload ((modified[i] != RELOAD_WRITE
			    ? recog_data.operand[i] : 0),
			   (modified[i] != RELOAD_READ
			    ? recog_data.operand[i] : 0),
			   (modified[i] != RELOAD_WRITE
			    ? recog_data.operand_loc[i] : 0),
			   (modified[i] != RELOAD_READ
			    ? recog_data.operand_loc[i] : 0),
			   (enum reg_class) goal_alternative[i],
			   (modified[i] == RELOAD_WRITE
			    ? VOIDmode : operand_mode[i]),
			   (modified[i] == RELOAD_READ
			    ? VOIDmode : operand_mode[i]),
			   (insn_code_number < 0 ? 0
			    : insn_data[insn_code_number].operand[i].strict_low),
			   1, i, operand_type[i]);
	/* If a memory reference remains (either as a MEM or a pseudo that
	   did not get a hard register), yet we can't make an optional
	   reload, check if this is actually a pseudo register reference;
	   we then need to emit a USE and/or a CLOBBER so that reload
	   inheritance will do the right thing.  */
	else if (replace
		 && (MEM_P (operand)
		     || (REG_P (operand)
			 && REGNO (operand) >= FIRST_PSEUDO_REGISTER
			 && reg_renumber [REGNO (operand)] < 0)))
	  {
	    operand = *recog_data.operand_loc[i];

	    while (GET_CODE (operand) == SUBREG)
	      operand = SUBREG_REG (operand);
	    if (REG_P (operand))
	      {
		if (modified[i] != RELOAD_WRITE)
		  /* We mark the USE with QImode so that we recognize
		     it as one that can be safely deleted at the end
		     of reload.  */
		  PUT_MODE (emit_insn_before (gen_rtx_USE (VOIDmode, operand),
					      insn), QImode);
		if (modified[i] != RELOAD_READ)
		  emit_insn_after (gen_rtx_CLOBBER (VOIDmode, operand), insn);
	      }
	  }
      }
    else if (goal_alternative_matches[i] >= 0
	     && goal_alternative_win[goal_alternative_matches[i]]
	     && modified[i] == RELOAD_READ
	     && modified[goal_alternative_matches[i]] == RELOAD_WRITE
	     && ! no_input_reloads && ! no_output_reloads
	     && optimize)
      {
	/* Similarly, make an optional reload for a pair of matching
	   objects that are in MEM or a pseudo that didn't get a hard reg.  */

	rtx operand = recog_data.operand[i];

	while (GET_CODE (operand) == SUBREG)
	  operand = SUBREG_REG (operand);
	if ((MEM_P (operand)
	     || (REG_P (operand)
		 && REGNO (operand) >= FIRST_PSEUDO_REGISTER))
	    && ((enum reg_class) goal_alternative[goal_alternative_matches[i]]
		!= NO_REGS))
	  operand_reloadnum[i] = operand_reloadnum[goal_alternative_matches[i]]
	    = push_reload (recog_data.operand[goal_alternative_matches[i]],
			   recog_data.operand[i],
			   recog_data.operand_loc[goal_alternative_matches[i]],
			   recog_data.operand_loc[i],
			   (enum reg_class) goal_alternative[goal_alternative_matches[i]],
			   operand_mode[goal_alternative_matches[i]],
			   operand_mode[i],
			   0, 1, goal_alternative_matches[i], RELOAD_OTHER);
      }

  /* Perform whatever substitutions on the operands we are supposed
     to make due to commutativity or replacement of registers
     with equivalent constants or memory slots.  */

  for (i = 0; i < noperands; i++)
    {
      /* We only do this on the last pass through reload, because it is
	 possible for some data (like reg_equiv_address) to be changed during
	 later passes.  Moreover, we lose the opportunity to get a useful
	 reload_{in,out}_reg when we do these replacements.  */

      if (replace)
	{
	  rtx substitution = substed_operand[i];

	  *recog_data.operand_loc[i] = substitution;

	  /* If we're replacing an operand with a LABEL_REF, we need
	     to make sure that there's a REG_LABEL note attached to
	     this instruction.  */
	  if (!JUMP_P (insn)
	      && GET_CODE (substitution) == LABEL_REF
	      && !find_reg_note (insn, REG_LABEL, XEXP (substitution, 0)))
	    REG_NOTES (insn) = gen_rtx_INSN_LIST (REG_LABEL,
						  XEXP (substitution, 0),
						  REG_NOTES (insn));
	}
      else
	retval |= (substed_operand[i] != *recog_data.operand_loc[i]);
    }

  /* If this insn pattern contains any MATCH_DUP's, make sure that
     they will be substituted if the operands they match are substituted.
     Also do now any substitutions we already did on the operands.

     Don't do this if we aren't making replacements because we might be
     propagating things allocated by frame pointer elimination into places
     it doesn't expect.  */

  if (insn_code_number >= 0 && replace)
    for (i = insn_data[insn_code_number].n_dups - 1; i >= 0; i--)
      {
	int opno = recog_data.dup_num[i];
	*recog_data.dup_loc[i] = *recog_data.operand_loc[opno];
	dup_replacements (recog_data.dup_loc[i], recog_data.operand_loc[opno]);
      }

#if 0
  /* This loses because reloading of prior insns can invalidate the equivalence
     (or at least find_equiv_reg isn't smart enough to find it any more),
     causing this insn to need more reload regs than it needed before.
     It may be too late to make the reload regs available.
     Now this optimization is done safely in choose_reload_regs.  */

  /* For each reload of a reg into some other class of reg,
     search for an existing equivalent reg (same value now) in the right class.
     We can use it as long as we don't need to change its contents.  */
  for (i = 0; i < n_reloads; i++)
    if (rld[i].reg_rtx == 0
	&& rld[i].in != 0
	&& REG_P (rld[i].in)
	&& rld[i].out == 0)
      {
	rld[i].reg_rtx
	  = find_equiv_reg (rld[i].in, insn, rld[i].class, -1,
			    static_reload_reg_p, 0, rld[i].inmode);
	/* Prevent generation of insn to load the value
	   because the one we found already has the value.  */
	if (rld[i].reg_rtx)
	  rld[i].in = rld[i].reg_rtx;
      }
#endif

  /* If we detected error and replaced asm instruction by USE, forget about the
     reloads.  */
  if (GET_CODE (PATTERN (insn)) == USE
      && GET_CODE (XEXP (PATTERN (insn), 0)) == CONST_INT)
    n_reloads = 0;

  /* Perhaps an output reload can be combined with another
     to reduce needs by one.  */
  if (!goal_earlyclobber)
    combine_reloads ();

  /* If we have a pair of reloads for parts of an address, they are reloading
     the same object, the operands themselves were not reloaded, and they
     are for two operands that are supposed to match, merge the reloads and
     change the type of the surviving reload to RELOAD_FOR_OPERAND_ADDRESS.  */

  for (i = 0; i < n_reloads; i++)
    {
      int k;

      for (j = i + 1; j < n_reloads; j++)
	if ((rld[i].when_needed == RELOAD_FOR_INPUT_ADDRESS
	     || rld[i].when_needed == RELOAD_FOR_OUTPUT_ADDRESS
	     || rld[i].when_needed == RELOAD_FOR_INPADDR_ADDRESS
	     || rld[i].when_needed == RELOAD_FOR_OUTADDR_ADDRESS)
	    && (rld[j].when_needed == RELOAD_FOR_INPUT_ADDRESS
		|| rld[j].when_needed == RELOAD_FOR_OUTPUT_ADDRESS
		|| rld[j].when_needed == RELOAD_FOR_INPADDR_ADDRESS
		|| rld[j].when_needed == RELOAD_FOR_OUTADDR_ADDRESS)
	    && rtx_equal_p (rld[i].in, rld[j].in)
	    && (operand_reloadnum[rld[i].opnum] < 0
		|| rld[operand_reloadnum[rld[i].opnum]].optional)
	    && (operand_reloadnum[rld[j].opnum] < 0
		|| rld[operand_reloadnum[rld[j].opnum]].optional)
	    && (goal_alternative_matches[rld[i].opnum] == rld[j].opnum
		|| (goal_alternative_matches[rld[j].opnum]
		    == rld[i].opnum)))
	  {
	    for (k = 0; k < n_replacements; k++)
	      if (replacements[k].what == j)
		replacements[k].what = i;

	    if (rld[i].when_needed == RELOAD_FOR_INPADDR_ADDRESS
		|| rld[i].when_needed == RELOAD_FOR_OUTADDR_ADDRESS)
	      rld[i].when_needed = RELOAD_FOR_OPADDR_ADDR;
	    else
	      rld[i].when_needed = RELOAD_FOR_OPERAND_ADDRESS;
	    rld[j].in = 0;
	  }
    }

  /* Scan all the reloads and update their type.
     If a reload is for the address of an operand and we didn't reload
     that operand, change the type.  Similarly, change the operand number
     of a reload when two operands match.  If a reload is optional, treat it
     as though the operand isn't reloaded.

     ??? This latter case is somewhat odd because if we do the optional
     reload, it means the object is hanging around.  Thus we need only
     do the address reload if the optional reload was NOT done.

     Change secondary reloads to be the address type of their operand, not
     the normal type.

     If an operand's reload is now RELOAD_OTHER, change any
     RELOAD_FOR_INPUT_ADDRESS reloads of that operand to
     RELOAD_FOR_OTHER_ADDRESS.  */

  for (i = 0; i < n_reloads; i++)
    {
      if (rld[i].secondary_p
	  && rld[i].when_needed == operand_type[rld[i].opnum])
	rld[i].when_needed = address_type[rld[i].opnum];

      if ((rld[i].when_needed == RELOAD_FOR_INPUT_ADDRESS
	   || rld[i].when_needed == RELOAD_FOR_OUTPUT_ADDRESS
	   || rld[i].when_needed == RELOAD_FOR_INPADDR_ADDRESS
	   || rld[i].when_needed == RELOAD_FOR_OUTADDR_ADDRESS)
	  && (operand_reloadnum[rld[i].opnum] < 0
	      || rld[operand_reloadnum[rld[i].opnum]].optional))
	{
	  /* If we have a secondary reload to go along with this reload,
	     change its type to RELOAD_FOR_OPADDR_ADDR.  */

	  if ((rld[i].when_needed == RELOAD_FOR_INPUT_ADDRESS
	       || rld[i].when_needed == RELOAD_FOR_INPADDR_ADDRESS)
	      && rld[i].secondary_in_reload != -1)
	    {
	      int secondary_in_reload = rld[i].secondary_in_reload;

	      rld[secondary_in_reload].when_needed = RELOAD_FOR_OPADDR_ADDR;

	      /* If there's a tertiary reload we have to change it also.  */
	      if (secondary_in_reload > 0
		  && rld[secondary_in_reload].secondary_in_reload != -1)
		rld[rld[secondary_in_reload].secondary_in_reload].when_needed
		  = RELOAD_FOR_OPADDR_ADDR;
	    }

	  if ((rld[i].when_needed == RELOAD_FOR_OUTPUT_ADDRESS
	       || rld[i].when_needed == RELOAD_FOR_OUTADDR_ADDRESS)
	      && rld[i].secondary_out_reload != -1)
	    {
	      int secondary_out_reload = rld[i].secondary_out_reload;

	      rld[secondary_out_reload].when_needed = RELOAD_FOR_OPADDR_ADDR;

	      /* If there's a tertiary reload we have to change it also.  */
	      if (secondary_out_reload
		  && rld[secondary_out_reload].secondary_out_reload != -1)
		rld[rld[secondary_out_reload].secondary_out_reload].when_needed
		  = RELOAD_FOR_OPADDR_ADDR;
	    }

	  if (rld[i].when_needed == RELOAD_FOR_INPADDR_ADDRESS
	      || rld[i].when_needed == RELOAD_FOR_OUTADDR_ADDRESS)
	    rld[i].when_needed = RELOAD_FOR_OPADDR_ADDR;
	  else
	    rld[i].when_needed = RELOAD_FOR_OPERAND_ADDRESS;
	}

      if ((rld[i].when_needed == RELOAD_FOR_INPUT_ADDRESS
	   || rld[i].when_needed == RELOAD_FOR_INPADDR_ADDRESS)
	  && operand_reloadnum[rld[i].opnum] >= 0
	  && (rld[operand_reloadnum[rld[i].opnum]].when_needed
	      == RELOAD_OTHER))
	rld[i].when_needed = RELOAD_FOR_OTHER_ADDRESS;

      if (goal_alternative_matches[rld[i].opnum] >= 0)
	rld[i].opnum = goal_alternative_matches[rld[i].opnum];
    }

  /* Scan all the reloads, and check for RELOAD_FOR_OPERAND_ADDRESS reloads.
     If we have more than one, then convert all RELOAD_FOR_OPADDR_ADDR
     reloads to RELOAD_FOR_OPERAND_ADDRESS reloads.

     choose_reload_regs assumes that RELOAD_FOR_OPADDR_ADDR reloads never
     conflict with RELOAD_FOR_OPERAND_ADDRESS reloads.  This is true for a
     single pair of RELOAD_FOR_OPADDR_ADDR/RELOAD_FOR_OPERAND_ADDRESS reloads.
     However, if there is more than one RELOAD_FOR_OPERAND_ADDRESS reload,
     then a RELOAD_FOR_OPADDR_ADDR reload conflicts with all
     RELOAD_FOR_OPERAND_ADDRESS reloads other than the one that uses it.
     This is complicated by the fact that a single operand can have more
     than one RELOAD_FOR_OPERAND_ADDRESS reload.  It is very difficult to fix
     choose_reload_regs without affecting code quality, and cases that
     actually fail are extremely rare, so it turns out to be better to fix
     the problem here by not generating cases that choose_reload_regs will
     fail for.  */
  /* There is a similar problem with RELOAD_FOR_INPUT_ADDRESS /
     RELOAD_FOR_OUTPUT_ADDRESS when there is more than one of a kind for
     a single operand.
     We can reduce the register pressure by exploiting that a
     RELOAD_FOR_X_ADDR_ADDR that precedes all RELOAD_FOR_X_ADDRESS reloads
     does not conflict with any of them, if it is only used for the first of
     the RELOAD_FOR_X_ADDRESS reloads.  */
  {
    int first_op_addr_num = -2;
    int first_inpaddr_num[MAX_RECOG_OPERANDS];
    int first_outpaddr_num[MAX_RECOG_OPERANDS];
    int need_change = 0;
    /* We use last_op_addr_reload and the contents of the above arrays
       first as flags - -2 means no instance encountered, -1 means exactly
       one instance encountered.
       If more than one instance has been encountered, we store the reload
       number of the first reload of the kind in question; reload numbers
       are known to be non-negative.  */
    for (i = 0; i < noperands; i++)
      first_inpaddr_num[i] = first_outpaddr_num[i] = -2;
    for (i = n_reloads - 1; i >= 0; i--)
      {
	switch (rld[i].when_needed)
	  {
	  case RELOAD_FOR_OPERAND_ADDRESS:
	    if (++first_op_addr_num >= 0)
	      {
		first_op_addr_num = i;
		need_change = 1;
	      }
	    break;
	  case RELOAD_FOR_INPUT_ADDRESS:
	    if (++first_inpaddr_num[rld[i].opnum] >= 0)
	      {
		first_inpaddr_num[rld[i].opnum] = i;
		need_change = 1;
	      }
	    break;
	  case RELOAD_FOR_OUTPUT_ADDRESS:
	    if (++first_outpaddr_num[rld[i].opnum] >= 0)
	      {
		first_outpaddr_num[rld[i].opnum] = i;
		need_change = 1;
	      }
	    break;
	  default:
	    break;
	  }
      }

    if (need_change)
      {
	for (i = 0; i < n_reloads; i++)
	  {
	    int first_num;
	    enum reload_type type;

	    switch (rld[i].when_needed)
	      {
	      case RELOAD_FOR_OPADDR_ADDR:
		first_num = first_op_addr_num;
		type = RELOAD_FOR_OPERAND_ADDRESS;
		break;
	      case RELOAD_FOR_INPADDR_ADDRESS:
		first_num = first_inpaddr_num[rld[i].opnum];
		type = RELOAD_FOR_INPUT_ADDRESS;
		break;
	      case RELOAD_FOR_OUTADDR_ADDRESS:
		first_num = first_outpaddr_num[rld[i].opnum];
		type = RELOAD_FOR_OUTPUT_ADDRESS;
		break;
	      default:
		continue;
	      }
	    if (first_num < 0)
	      continue;
	    else if (i > first_num)
	      rld[i].when_needed = type;
	    else
	      {
		/* Check if the only TYPE reload that uses reload I is
		   reload FIRST_NUM.  */
		for (j = n_reloads - 1; j > first_num; j--)
		  {
		    if (rld[j].when_needed == type
			&& (rld[i].secondary_p
			    ? rld[j].secondary_in_reload == i
			    : reg_mentioned_p (rld[i].in, rld[j].in)))
		      {
			rld[i].when_needed = type;
			break;
		      }
		  }
	      }
	  }
      }
  }

  /* See if we have any reloads that are now allowed to be merged
     because we've changed when the reload is needed to
     RELOAD_FOR_OPERAND_ADDRESS or RELOAD_FOR_OTHER_ADDRESS.  Only
     check for the most common cases.  */

  for (i = 0; i < n_reloads; i++)
    if (rld[i].in != 0 && rld[i].out == 0
	&& (rld[i].when_needed == RELOAD_FOR_OPERAND_ADDRESS
	    || rld[i].when_needed == RELOAD_FOR_OPADDR_ADDR
	    || rld[i].when_needed == RELOAD_FOR_OTHER_ADDRESS))
      for (j = 0; j < n_reloads; j++)
	if (i != j && rld[j].in != 0 && rld[j].out == 0
	    && rld[j].when_needed == rld[i].when_needed
	    && MATCHES (rld[i].in, rld[j].in)
	    && rld[i].class == rld[j].class
	    && !rld[i].nocombine && !rld[j].nocombine
	    && rld[i].reg_rtx == rld[j].reg_rtx)
	  {
	    rld[i].opnum = MIN (rld[i].opnum, rld[j].opnum);
	    transfer_replacements (i, j);
	    rld[j].in = 0;
	  }

#ifdef HAVE_cc0
  /* If we made any reloads for addresses, see if they violate a
     "no input reloads" requirement for this insn.  But loads that we
     do after the insn (such as for output addresses) are fine.  */
  if (no_input_reloads)
    for (i = 0; i < n_reloads; i++)
      gcc_assert (rld[i].in == 0
		  || rld[i].when_needed == RELOAD_FOR_OUTADDR_ADDRESS
		  || rld[i].when_needed == RELOAD_FOR_OUTPUT_ADDRESS);
#endif

  /* Compute reload_mode and reload_nregs.  */
  for (i = 0; i < n_reloads; i++)
    {
      rld[i].mode
	= (rld[i].inmode == VOIDmode
	   || (GET_MODE_SIZE (rld[i].outmode)
	       > GET_MODE_SIZE (rld[i].inmode)))
	  ? rld[i].outmode : rld[i].inmode;

      rld[i].nregs = CLASS_MAX_NREGS (rld[i].class, rld[i].mode);
    }

  /* Special case a simple move with an input reload and a
     destination of a hard reg, if the hard reg is ok, use it.  */
  for (i = 0; i < n_reloads; i++)
    if (rld[i].when_needed == RELOAD_FOR_INPUT
	&& GET_CODE (PATTERN (insn)) == SET
	&& REG_P (SET_DEST (PATTERN (insn)))
	&& SET_SRC (PATTERN (insn)) == rld[i].in)
      {
	rtx dest = SET_DEST (PATTERN (insn));
	unsigned int regno = REGNO (dest);

	if (regno < FIRST_PSEUDO_REGISTER
	    && TEST_HARD_REG_BIT (reg_class_contents[rld[i].class], regno)
	    && HARD_REGNO_MODE_OK (regno, rld[i].mode))
	  {
	    int nr = hard_regno_nregs[regno][rld[i].mode];
	    int ok = 1, nri;

	    for (nri = 1; nri < nr; nri ++)
	      if (! TEST_HARD_REG_BIT (reg_class_contents[rld[i].class], regno + nri))
		ok = 0;

	    if (ok)
	      rld[i].reg_rtx = dest;
	  }
      }

  return retval;
}

/* Return 1 if alternative number ALTNUM in constraint-string CONSTRAINT
   accepts a memory operand with constant address.  */

static int
alternative_allows_memconst (const char *constraint, int altnum)
{
  int c;
  /* Skip alternatives before the one requested.  */
  while (altnum > 0)
    {
      while (*constraint++ != ',');
      altnum--;
    }
  /* Scan the requested alternative for 'm' or 'o'.
     If one of them is present, this alternative accepts memory constants.  */
  for (; (c = *constraint) && c != ',' && c != '#';
       constraint += CONSTRAINT_LEN (c, constraint))
    if (c == 'm' || c == 'o' || EXTRA_MEMORY_CONSTRAINT (c, constraint))
      return 1;
  return 0;
}

/* Scan X for memory references and scan the addresses for reloading.
   Also checks for references to "constant" regs that we want to eliminate
   and replaces them with the values they stand for.
   We may alter X destructively if it contains a reference to such.
   If X is just a constant reg, we return the equivalent value
   instead of X.

   IND_LEVELS says how many levels of indirect addressing this machine
   supports.

   OPNUM and TYPE identify the purpose of the reload.

   IS_SET_DEST is true if X is the destination of a SET, which is not
   appropriate to be replaced by a constant.

   INSN, if nonzero, is the insn in which we do the reload.  It is used
   to determine if we may generate output reloads, and where to put USEs
   for pseudos that we have to replace with stack slots.

   ADDRESS_RELOADED.  If nonzero, is a pointer to where we put the
   result of find_reloads_address.  */

static rtx
find_reloads_toplev (rtx x, int opnum, enum reload_type type,
		     int ind_levels, int is_set_dest, rtx insn,
		     int *address_reloaded)
{
  RTX_CODE code = GET_CODE (x);

  const char *fmt = GET_RTX_FORMAT (code);
  int i;
  int copied;

  if (code == REG)
    {
      /* This code is duplicated for speed in find_reloads.  */
      int regno = REGNO (x);
      if (reg_equiv_constant[regno] != 0 && !is_set_dest)
	x = reg_equiv_constant[regno];
#if 0
      /*  This creates (subreg (mem...)) which would cause an unnecessary
	  reload of the mem.  */
      else if (reg_equiv_mem[regno] != 0)
	x = reg_equiv_mem[regno];
#endif
      else if (reg_equiv_memory_loc[regno]
	       && (reg_equiv_address[regno] != 0 || num_not_at_initial_offset))
	{
	  rtx mem = make_memloc (x, regno);
	  if (reg_equiv_address[regno]
	      || ! rtx_equal_p (mem, reg_equiv_mem[regno]))
	    {
	      /* If this is not a toplevel operand, find_reloads doesn't see
		 this substitution.  We have to emit a USE of the pseudo so
		 that delete_output_reload can see it.  */
	      if (replace_reloads && recog_data.operand[opnum] != x)
		/* We mark the USE with QImode so that we recognize it
		   as one that can be safely deleted at the end of
		   reload.  */
		PUT_MODE (emit_insn_before (gen_rtx_USE (VOIDmode, x), insn),
			  QImode);
	      x = mem;
	      i = find_reloads_address (GET_MODE (x), &x, XEXP (x, 0), &XEXP (x, 0),
					opnum, type, ind_levels, insn);
	      if (!rtx_equal_p (mem, x))
		push_reg_equiv_alt_mem (regno, x);
	      if (address_reloaded)
		*address_reloaded = i;
	    }
	}
      return x;
    }
  if (code == MEM)
    {
      rtx tem = x;

      i = find_reloads_address (GET_MODE (x), &tem, XEXP (x, 0), &XEXP (x, 0),
				opnum, type, ind_levels, insn);
      if (address_reloaded)
	*address_reloaded = i;

      return tem;
    }

  if (code == SUBREG && REG_P (SUBREG_REG (x)))
    {
      /* Check for SUBREG containing a REG that's equivalent to a
	 constant.  If the constant has a known value, truncate it
	 right now.  Similarly if we are extracting a single-word of a
	 multi-word constant.  If the constant is symbolic, allow it
	 to be substituted normally.  push_reload will strip the
	 subreg later.  The constant must not be VOIDmode, because we
	 will lose the mode of the register (this should never happen
	 because one of the cases above should handle it).  */

      int regno = REGNO (SUBREG_REG (x));
      rtx tem;

      if (subreg_lowpart_p (x)
	  && regno >= FIRST_PSEUDO_REGISTER
	  && reg_renumber[regno] < 0
	  && reg_equiv_constant[regno] != 0
	  && (tem = gen_lowpart_common (GET_MODE (x),
					reg_equiv_constant[regno])) != 0)
	return tem;

      if (regno >= FIRST_PSEUDO_REGISTER
	  && reg_renumber[regno] < 0
	  && reg_equiv_constant[regno] != 0)
	{
	  tem =
	    simplify_gen_subreg (GET_MODE (x), reg_equiv_constant[regno],
				 GET_MODE (SUBREG_REG (x)), SUBREG_BYTE (x));
	  gcc_assert (tem);
	  return tem;
	}

      /* If the subreg contains a reg that will be converted to a mem,
	 convert the subreg to a narrower memref now.
	 Otherwise, we would get (subreg (mem ...) ...),
	 which would force reload of the mem.

	 We also need to do this if there is an equivalent MEM that is
	 not offsettable.  In that case, alter_subreg would produce an
	 invalid address on big-endian machines.

	 For machines that extend byte loads, we must not reload using
	 a wider mode if we have a paradoxical SUBREG.  find_reloads will
	 force a reload in that case.  So we should not do anything here.  */

      if (regno >= FIRST_PSEUDO_REGISTER
#ifdef LOAD_EXTEND_OP
	       && (GET_MODE_SIZE (GET_MODE (x))
		   <= GET_MODE_SIZE (GET_MODE (SUBREG_REG (x))))
#endif
	       && (reg_equiv_address[regno] != 0
		   || (reg_equiv_mem[regno] != 0
		       && (! strict_memory_address_p (GET_MODE (x),
						      XEXP (reg_equiv_mem[regno], 0))
			   || ! offsettable_memref_p (reg_equiv_mem[regno])
			   || num_not_at_initial_offset))))
	x = find_reloads_subreg_address (x, 1, opnum, type, ind_levels,
					 insn);
    }

  for (copied = 0, i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	{
	  rtx new_part = find_reloads_toplev (XEXP (x, i), opnum, type,
					      ind_levels, is_set_dest, insn,
					      address_reloaded);
	  /* If we have replaced a reg with it's equivalent memory loc -
	     that can still be handled here e.g. if it's in a paradoxical
	     subreg - we must make the change in a copy, rather than using
	     a destructive change.  This way, find_reloads can still elect
	     not to do the change.  */
	  if (new_part != XEXP (x, i) && ! CONSTANT_P (new_part) && ! copied)
	    {
	      x = shallow_copy_rtx (x);
	      copied = 1;
	    }
	  XEXP (x, i) = new_part;
	}
    }
  return x;
}

/* Return a mem ref for the memory equivalent of reg REGNO.
   This mem ref is not shared with anything.  */

static rtx
make_memloc (rtx ad, int regno)
{
  /* We must rerun eliminate_regs, in case the elimination
     offsets have changed.  */
  rtx tem
    = XEXP (eliminate_regs (reg_equiv_memory_loc[regno], 0, NULL_RTX), 0);

  /* If TEM might contain a pseudo, we must copy it to avoid
     modifying it when we do the substitution for the reload.  */
  if (rtx_varies_p (tem, 0))
    tem = copy_rtx (tem);

  tem = replace_equiv_address_nv (reg_equiv_memory_loc[regno], tem);
  tem = adjust_address_nv (tem, GET_MODE (ad), 0);

  /* Copy the result if it's still the same as the equivalence, to avoid
     modifying it when we do the substitution for the reload.  */
  if (tem == reg_equiv_memory_loc[regno])
    tem = copy_rtx (tem);
  return tem;
}

/* Returns true if AD could be turned into a valid memory reference
   to mode MODE by reloading the part pointed to by PART into a
   register.  */

static int
maybe_memory_address_p (enum machine_mode mode, rtx ad, rtx *part)
{
  int retv;
  rtx tem = *part;
  rtx reg = gen_rtx_REG (GET_MODE (tem), max_reg_num ());

  *part = reg;
  retv = memory_address_p (mode, ad);
  *part = tem;

  return retv;
}

/* Record all reloads needed for handling memory address AD
   which appears in *LOC in a memory reference to mode MODE
   which itself is found in location  *MEMREFLOC.
   Note that we take shortcuts assuming that no multi-reg machine mode
   occurs as part of an address.

   OPNUM and TYPE specify the purpose of this reload.

   IND_LEVELS says how many levels of indirect addressing this machine
   supports.

   INSN, if nonzero, is the insn in which we do the reload.  It is used
   to determine if we may generate output reloads, and where to put USEs
   for pseudos that we have to replace with stack slots.

   Value is one if this address is reloaded or replaced as a whole; it is
   zero if the top level of this address was not reloaded or replaced, and
   it is -1 if it may or may not have been reloaded or replaced.

   Note that there is no verification that the address will be valid after
   this routine does its work.  Instead, we rely on the fact that the address
   was valid when reload started.  So we need only undo things that reload
   could have broken.  These are wrong register types, pseudos not allocated
   to a hard register, and frame pointer elimination.  */

static int
find_reloads_address (enum machine_mode mode, rtx *memrefloc, rtx ad,
		      rtx *loc, int opnum, enum reload_type type,
		      int ind_levels, rtx insn)
{
  int regno;
  int removed_and = 0;
  int op_index;
  rtx tem;

  /* If the address is a register, see if it is a legitimate address and
     reload if not.  We first handle the cases where we need not reload
     or where we must reload in a non-standard way.  */

  if (REG_P (ad))
    {
      regno = REGNO (ad);

      /* If the register is equivalent to an invariant expression, substitute
	 the invariant, and eliminate any eliminable register references.  */
      tem = reg_equiv_constant[regno];
      if (tem != 0
	  && (tem = eliminate_regs (tem, mode, insn))
	  && strict_memory_address_p (mode, tem))
	{
	  *loc = ad = tem;
	  return 0;
	}

      tem = reg_equiv_memory_loc[regno];
      if (tem != 0)
	{
	  if (reg_equiv_address[regno] != 0 || num_not_at_initial_offset)
	    {
	      tem = make_memloc (ad, regno);
	      if (! strict_memory_address_p (GET_MODE (tem), XEXP (tem, 0)))
		{
		  rtx orig = tem;

		  find_reloads_address (GET_MODE (tem), &tem, XEXP (tem, 0),
					&XEXP (tem, 0), opnum,
					ADDR_TYPE (type), ind_levels, insn);
		  if (tem != orig)
		    push_reg_equiv_alt_mem (regno, tem);
		}
	      /* We can avoid a reload if the register's equivalent memory
		 expression is valid as an indirect memory address.
		 But not all addresses are valid in a mem used as an indirect
		 address: only reg or reg+constant.  */

	      if (ind_levels > 0
		  && strict_memory_address_p (mode, tem)
		  && (REG_P (XEXP (tem, 0))
		      || (GET_CODE (XEXP (tem, 0)) == PLUS
			  && REG_P (XEXP (XEXP (tem, 0), 0))
			  && CONSTANT_P (XEXP (XEXP (tem, 0), 1)))))
		{
		  /* TEM is not the same as what we'll be replacing the
		     pseudo with after reload, put a USE in front of INSN
		     in the final reload pass.  */
		  if (replace_reloads
		      && num_not_at_initial_offset
		      && ! rtx_equal_p (tem, reg_equiv_mem[regno]))
		    {
		      *loc = tem;
		      /* We mark the USE with QImode so that we
			 recognize it as one that can be safely
			 deleted at the end of reload.  */
		      PUT_MODE (emit_insn_before (gen_rtx_USE (VOIDmode, ad),
						  insn), QImode);

		      /* This doesn't really count as replacing the address
			 as a whole, since it is still a memory access.  */
		    }
		  return 0;
		}
	      ad = tem;
	    }
	}

      /* The only remaining case where we can avoid a reload is if this is a
	 hard register that is valid as a base register and which is not the
	 subject of a CLOBBER in this insn.  */

      else if (regno < FIRST_PSEUDO_REGISTER
	       && regno_ok_for_base_p (regno, mode, MEM, SCRATCH)
	       && ! regno_clobbered_p (regno, this_insn, mode, 0))
	return 0;

      /* If we do not have one of the cases above, we must do the reload.  */
      push_reload (ad, NULL_RTX, loc, (rtx*) 0, base_reg_class (mode, MEM, SCRATCH),
		   GET_MODE (ad), VOIDmode, 0, 0, opnum, type);
      return 1;
    }

  if (strict_memory_address_p (mode, ad))
    {
      /* The address appears valid, so reloads are not needed.
	 But the address may contain an eliminable register.
	 This can happen because a machine with indirect addressing
	 may consider a pseudo register by itself a valid address even when
	 it has failed to get a hard reg.
	 So do a tree-walk to find and eliminate all such regs.  */

      /* But first quickly dispose of a common case.  */
      if (GET_CODE (ad) == PLUS
	  && GET_CODE (XEXP (ad, 1)) == CONST_INT
	  && REG_P (XEXP (ad, 0))
	  && reg_equiv_constant[REGNO (XEXP (ad, 0))] == 0)
	return 0;

      subst_reg_equivs_changed = 0;
      *loc = subst_reg_equivs (ad, insn);

      if (! subst_reg_equivs_changed)
	return 0;

      /* Check result for validity after substitution.  */
      if (strict_memory_address_p (mode, ad))
	return 0;
    }

#ifdef LEGITIMIZE_RELOAD_ADDRESS
  do
    {
      if (memrefloc)
	{
	  LEGITIMIZE_RELOAD_ADDRESS (ad, GET_MODE (*memrefloc), opnum, type,
				     ind_levels, win);
	}
      break;
    win:
      *memrefloc = copy_rtx (*memrefloc);
      XEXP (*memrefloc, 0) = ad;
      move_replacements (&ad, &XEXP (*memrefloc, 0));
      return -1;
    }
  while (0);
#endif

  /* The address is not valid.  We have to figure out why.  First see if
     we have an outer AND and remove it if so.  Then analyze what's inside.  */

  if (GET_CODE (ad) == AND)
    {
      removed_and = 1;
      loc = &XEXP (ad, 0);
      ad = *loc;
    }

  /* One possibility for why the address is invalid is that it is itself
     a MEM.  This can happen when the frame pointer is being eliminated, a
     pseudo is not allocated to a hard register, and the offset between the
     frame and stack pointers is not its initial value.  In that case the
     pseudo will have been replaced by a MEM referring to the
     stack pointer.  */
  if (MEM_P (ad))
    {
      /* First ensure that the address in this MEM is valid.  Then, unless
	 indirect addresses are valid, reload the MEM into a register.  */
      tem = ad;
      find_reloads_address (GET_MODE (ad), &tem, XEXP (ad, 0), &XEXP (ad, 0),
			    opnum, ADDR_TYPE (type),
			    ind_levels == 0 ? 0 : ind_levels - 1, insn);

      /* If tem was changed, then we must create a new memory reference to
	 hold it and store it back into memrefloc.  */
      if (tem != ad && memrefloc)
	{
	  *memrefloc = copy_rtx (*memrefloc);
	  copy_replacements (tem, XEXP (*memrefloc, 0));
	  loc = &XEXP (*memrefloc, 0);
	  if (removed_and)
	    loc = &XEXP (*loc, 0);
	}

      /* Check similar cases as for indirect addresses as above except
	 that we can allow pseudos and a MEM since they should have been
	 taken care of above.  */

      if (ind_levels == 0
	  || (GET_CODE (XEXP (tem, 0)) == SYMBOL_REF && ! indirect_symref_ok)
	  || MEM_P (XEXP (tem, 0))
	  || ! (REG_P (XEXP (tem, 0))
		|| (GET_CODE (XEXP (tem, 0)) == PLUS
		    && REG_P (XEXP (XEXP (tem, 0), 0))
		    && GET_CODE (XEXP (XEXP (tem, 0), 1)) == CONST_INT)))
	{
	  /* Must use TEM here, not AD, since it is the one that will
	     have any subexpressions reloaded, if needed.  */
	  push_reload (tem, NULL_RTX, loc, (rtx*) 0,
		       base_reg_class (mode, MEM, SCRATCH), GET_MODE (tem),
		       VOIDmode, 0,
		       0, opnum, type);
	  return ! removed_and;
	}
      else
	return 0;
    }

  /* If we have address of a stack slot but it's not valid because the
     displacement is too large, compute the sum in a register.
     Handle all base registers here, not just fp/ap/sp, because on some
     targets (namely SH) we can also get too large displacements from
     big-endian corrections.  */
  else if (GET_CODE (ad) == PLUS
	   && REG_P (XEXP (ad, 0))
	   && REGNO (XEXP (ad, 0)) < FIRST_PSEUDO_REGISTER
	   && GET_CODE (XEXP (ad, 1)) == CONST_INT
	   && regno_ok_for_base_p (REGNO (XEXP (ad, 0)), mode, PLUS,
				   CONST_INT))

    {
      /* Unshare the MEM rtx so we can safely alter it.  */
      if (memrefloc)
	{
	  *memrefloc = copy_rtx (*memrefloc);
	  loc = &XEXP (*memrefloc, 0);
	  if (removed_and)
	    loc = &XEXP (*loc, 0);
	}

      if (double_reg_address_ok)
	{
	  /* Unshare the sum as well.  */
	  *loc = ad = copy_rtx (ad);

	  /* Reload the displacement into an index reg.
	     We assume the frame pointer or arg pointer is a base reg.  */
	  find_reloads_address_part (XEXP (ad, 1), &XEXP (ad, 1),
				     INDEX_REG_CLASS, GET_MODE (ad), opnum,
				     type, ind_levels);
	  return 0;
	}
      else
	{
	  /* If the sum of two regs is not necessarily valid,
	     reload the sum into a base reg.
	     That will at least work.  */
	  find_reloads_address_part (ad, loc,
				     base_reg_class (mode, MEM, SCRATCH),
				     Pmode, opnum, type, ind_levels);
	}
      return ! removed_and;
    }

  /* If we have an indexed stack slot, there are three possible reasons why
     it might be invalid: The index might need to be reloaded, the address
     might have been made by frame pointer elimination and hence have a
     constant out of range, or both reasons might apply.

     We can easily check for an index needing reload, but even if that is the
     case, we might also have an invalid constant.  To avoid making the
     conservative assumption and requiring two reloads, we see if this address
     is valid when not interpreted strictly.  If it is, the only problem is
     that the index needs a reload and find_reloads_address_1 will take care
     of it.

     Handle all base registers here, not just fp/ap/sp, because on some
     targets (namely SPARC) we can also get invalid addresses from preventive
     subreg big-endian corrections made by find_reloads_toplev.  We
     can also get expressions involving LO_SUM (rather than PLUS) from
     find_reloads_subreg_address.

     If we decide to do something, it must be that `double_reg_address_ok'
     is true.  We generate a reload of the base register + constant and
     rework the sum so that the reload register will be added to the index.
     This is safe because we know the address isn't shared.

     We check for the base register as both the first and second operand of
     the innermost PLUS and/or LO_SUM.  */

  for (op_index = 0; op_index < 2; ++op_index)
    {
      rtx operand, addend;
      enum rtx_code inner_code;

      if (GET_CODE (ad) != PLUS)
	  continue;

      inner_code = GET_CODE (XEXP (ad, 0));
      if (!(GET_CODE (ad) == PLUS 
	    && GET_CODE (XEXP (ad, 1)) == CONST_INT
	    && (inner_code == PLUS || inner_code == LO_SUM)))
	continue;

      operand = XEXP (XEXP (ad, 0), op_index);
      if (!REG_P (operand) || REGNO (operand) >= FIRST_PSEUDO_REGISTER)
	continue;

      addend = XEXP (XEXP (ad, 0), 1 - op_index);

      if ((regno_ok_for_base_p (REGNO (operand), mode, inner_code,
				GET_CODE (addend))
	   || operand == frame_pointer_rtx
#if FRAME_POINTER_REGNUM != HARD_FRAME_POINTER_REGNUM
	   || operand == hard_frame_pointer_rtx
#endif
#if FRAME_POINTER_REGNUM != ARG_POINTER_REGNUM
	   || operand == arg_pointer_rtx
#endif
	   || operand == stack_pointer_rtx)
	  && ! maybe_memory_address_p (mode, ad, 
				       &XEXP (XEXP (ad, 0), 1 - op_index)))
	{
	  rtx offset_reg;
	  enum reg_class cls;

	  offset_reg = plus_constant (operand, INTVAL (XEXP (ad, 1)));

	  /* Form the adjusted address.  */
	  if (GET_CODE (XEXP (ad, 0)) == PLUS)
	    ad = gen_rtx_PLUS (GET_MODE (ad), 
			       op_index == 0 ? offset_reg : addend, 
			       op_index == 0 ? addend : offset_reg);
	  else
	    ad = gen_rtx_LO_SUM (GET_MODE (ad), 
				 op_index == 0 ? offset_reg : addend, 
				 op_index == 0 ? addend : offset_reg);
	  *loc = ad;

	  cls = base_reg_class (mode, MEM, GET_CODE (addend));
	  find_reloads_address_part (XEXP (ad, op_index), 
				     &XEXP (ad, op_index), cls,
				     GET_MODE (ad), opnum, type, ind_levels);
	  find_reloads_address_1 (mode,
				  XEXP (ad, 1 - op_index), 1, GET_CODE (ad),
				  GET_CODE (XEXP (ad, op_index)),
				  &XEXP (ad, 1 - op_index), opnum,
				  type, 0, insn);

	  return 0;
	}
    }

  /* See if address becomes valid when an eliminable register
     in a sum is replaced.  */

  tem = ad;
  if (GET_CODE (ad) == PLUS)
    tem = subst_indexed_address (ad);
  if (tem != ad && strict_memory_address_p (mode, tem))
    {
      /* Ok, we win that way.  Replace any additional eliminable
	 registers.  */

      subst_reg_equivs_changed = 0;
      tem = subst_reg_equivs (tem, insn);

      /* Make sure that didn't make the address invalid again.  */

      if (! subst_reg_equivs_changed || strict_memory_address_p (mode, tem))
	{
	  *loc = tem;
	  return 0;
	}
    }

  /* If constants aren't valid addresses, reload the constant address
     into a register.  */
  if (CONSTANT_P (ad) && ! strict_memory_address_p (mode, ad))
    {
      /* If AD is an address in the constant pool, the MEM rtx may be shared.
	 Unshare it so we can safely alter it.  */
      if (memrefloc && GET_CODE (ad) == SYMBOL_REF
	  && CONSTANT_POOL_ADDRESS_P (ad))
	{
	  *memrefloc = copy_rtx (*memrefloc);
	  loc = &XEXP (*memrefloc, 0);
	  if (removed_and)
	    loc = &XEXP (*loc, 0);
	}

      find_reloads_address_part (ad, loc, base_reg_class (mode, MEM, SCRATCH),
				 Pmode, opnum, type, ind_levels);
      return ! removed_and;
    }

  return find_reloads_address_1 (mode, ad, 0, MEM, SCRATCH, loc, opnum, type,
				 ind_levels, insn);
}

/* Find all pseudo regs appearing in AD
   that are eliminable in favor of equivalent values
   and do not have hard regs; replace them by their equivalents.
   INSN, if nonzero, is the insn in which we do the reload.  We put USEs in
   front of it for pseudos that we have to replace with stack slots.  */

static rtx
subst_reg_equivs (rtx ad, rtx insn)
{
  RTX_CODE code = GET_CODE (ad);
  int i;
  const char *fmt;

  switch (code)
    {
    case HIGH:
    case CONST_INT:
    case CONST:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case SYMBOL_REF:
    case LABEL_REF:
    case PC:
    case CC0:
      return ad;

    case REG:
      {
	int regno = REGNO (ad);

	if (reg_equiv_constant[regno] != 0)
	  {
	    subst_reg_equivs_changed = 1;
	    return reg_equiv_constant[regno];
	  }
	if (reg_equiv_memory_loc[regno] && num_not_at_initial_offset)
	  {
	    rtx mem = make_memloc (ad, regno);
	    if (! rtx_equal_p (mem, reg_equiv_mem[regno]))
	      {
		subst_reg_equivs_changed = 1;
		/* We mark the USE with QImode so that we recognize it
		   as one that can be safely deleted at the end of
		   reload.  */
		PUT_MODE (emit_insn_before (gen_rtx_USE (VOIDmode, ad), insn),
			  QImode);
		return mem;
	      }
	  }
      }
      return ad;

    case PLUS:
      /* Quickly dispose of a common case.  */
      if (XEXP (ad, 0) == frame_pointer_rtx
	  && GET_CODE (XEXP (ad, 1)) == CONST_INT)
	return ad;
      break;

    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    if (fmt[i] == 'e')
      XEXP (ad, i) = subst_reg_equivs (XEXP (ad, i), insn);
  return ad;
}

/* Compute the sum of X and Y, making canonicalizations assumed in an
   address, namely: sum constant integers, surround the sum of two
   constants with a CONST, put the constant as the second operand, and
   group the constant on the outermost sum.

   This routine assumes both inputs are already in canonical form.  */

rtx
form_sum (rtx x, rtx y)
{
  rtx tem;
  enum machine_mode mode = GET_MODE (x);

  if (mode == VOIDmode)
    mode = GET_MODE (y);

  if (mode == VOIDmode)
    mode = Pmode;

  if (GET_CODE (x) == CONST_INT)
    return plus_constant (y, INTVAL (x));
  else if (GET_CODE (y) == CONST_INT)
    return plus_constant (x, INTVAL (y));
  else if (CONSTANT_P (x))
    tem = x, x = y, y = tem;

  if (GET_CODE (x) == PLUS && CONSTANT_P (XEXP (x, 1)))
    return form_sum (XEXP (x, 0), form_sum (XEXP (x, 1), y));

  /* Note that if the operands of Y are specified in the opposite
     order in the recursive calls below, infinite recursion will occur.  */
  if (GET_CODE (y) == PLUS && CONSTANT_P (XEXP (y, 1)))
    return form_sum (form_sum (x, XEXP (y, 0)), XEXP (y, 1));

  /* If both constant, encapsulate sum.  Otherwise, just form sum.  A
     constant will have been placed second.  */
  if (CONSTANT_P (x) && CONSTANT_P (y))
    {
      if (GET_CODE (x) == CONST)
	x = XEXP (x, 0);
      if (GET_CODE (y) == CONST)
	y = XEXP (y, 0);

      return gen_rtx_CONST (VOIDmode, gen_rtx_PLUS (mode, x, y));
    }

  return gen_rtx_PLUS (mode, x, y);
}

/* If ADDR is a sum containing a pseudo register that should be
   replaced with a constant (from reg_equiv_constant),
   return the result of doing so, and also apply the associative
   law so that the result is more likely to be a valid address.
   (But it is not guaranteed to be one.)

   Note that at most one register is replaced, even if more are
   replaceable.  Also, we try to put the result into a canonical form
   so it is more likely to be a valid address.

   In all other cases, return ADDR.  */

static rtx
subst_indexed_address (rtx addr)
{
  rtx op0 = 0, op1 = 0, op2 = 0;
  rtx tem;
  int regno;

  if (GET_CODE (addr) == PLUS)
    {
      /* Try to find a register to replace.  */
      op0 = XEXP (addr, 0), op1 = XEXP (addr, 1), op2 = 0;
      if (REG_P (op0)
	  && (regno = REGNO (op0)) >= FIRST_PSEUDO_REGISTER
	  && reg_renumber[regno] < 0
	  && reg_equiv_constant[regno] != 0)
	op0 = reg_equiv_constant[regno];
      else if (REG_P (op1)
	       && (regno = REGNO (op1)) >= FIRST_PSEUDO_REGISTER
	       && reg_renumber[regno] < 0
	       && reg_equiv_constant[regno] != 0)
	op1 = reg_equiv_constant[regno];
      else if (GET_CODE (op0) == PLUS
	       && (tem = subst_indexed_address (op0)) != op0)
	op0 = tem;
      else if (GET_CODE (op1) == PLUS
	       && (tem = subst_indexed_address (op1)) != op1)
	op1 = tem;
      else
	return addr;

      /* Pick out up to three things to add.  */
      if (GET_CODE (op1) == PLUS)
	op2 = XEXP (op1, 1), op1 = XEXP (op1, 0);
      else if (GET_CODE (op0) == PLUS)
	op2 = op1, op1 = XEXP (op0, 1), op0 = XEXP (op0, 0);

      /* Compute the sum.  */
      if (op2 != 0)
	op1 = form_sum (op1, op2);
      if (op1 != 0)
	op0 = form_sum (op0, op1);

      return op0;
    }
  return addr;
}

/* Update the REG_INC notes for an insn.  It updates all REG_INC
   notes for the instruction which refer to REGNO the to refer
   to the reload number.

   INSN is the insn for which any REG_INC notes need updating.

   REGNO is the register number which has been reloaded.

   RELOADNUM is the reload number.  */

static void
update_auto_inc_notes (rtx insn ATTRIBUTE_UNUSED, int regno ATTRIBUTE_UNUSED,
		       int reloadnum ATTRIBUTE_UNUSED)
{
#ifdef AUTO_INC_DEC
  rtx link;

  for (link = REG_NOTES (insn); link; link = XEXP (link, 1))
    if (REG_NOTE_KIND (link) == REG_INC
        && (int) REGNO (XEXP (link, 0)) == regno)
      push_replacement (&XEXP (link, 0), reloadnum, VOIDmode);
#endif
}

/* Record the pseudo registers we must reload into hard registers in a
   subexpression of a would-be memory address, X referring to a value
   in mode MODE.  (This function is not called if the address we find
   is strictly valid.)

   CONTEXT = 1 means we are considering regs as index regs,
   = 0 means we are considering them as base regs.
   OUTER_CODE is the code of the enclosing RTX, typically a MEM, a PLUS,
   or an autoinc code.
   If CONTEXT == 0 and OUTER_CODE is a PLUS or LO_SUM, then INDEX_CODE
   is the code of the index part of the address.  Otherwise, pass SCRATCH
   for this argument.
   OPNUM and TYPE specify the purpose of any reloads made.

   IND_LEVELS says how many levels of indirect addressing are
   supported at this point in the address.

   INSN, if nonzero, is the insn in which we do the reload.  It is used
   to determine if we may generate output reloads.

   We return nonzero if X, as a whole, is reloaded or replaced.  */

/* Note that we take shortcuts assuming that no multi-reg machine mode
   occurs as part of an address.
   Also, this is not fully machine-customizable; it works for machines
   such as VAXen and 68000's and 32000's, but other possible machines
   could have addressing modes that this does not handle right.
   If you add push_reload calls here, you need to make sure gen_reload
   handles those cases gracefully.  */

static int
find_reloads_address_1 (enum machine_mode mode, rtx x, int context,
			enum rtx_code outer_code, enum rtx_code index_code,
			rtx *loc, int opnum, enum reload_type type,
			int ind_levels, rtx insn)
{
#define REG_OK_FOR_CONTEXT(CONTEXT, REGNO, MODE, OUTER, INDEX)		\
  ((CONTEXT) == 0							\
   ? regno_ok_for_base_p (REGNO, MODE, OUTER, INDEX)			\
   : REGNO_OK_FOR_INDEX_P (REGNO))					

  enum reg_class context_reg_class;
  RTX_CODE code = GET_CODE (x);

  if (context == 1)
    context_reg_class = INDEX_REG_CLASS;
  else
    context_reg_class = base_reg_class (mode, outer_code, index_code);

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

	if (GET_CODE (op0) == SUBREG)
	  {
	    op0 = SUBREG_REG (op0);
	    code0 = GET_CODE (op0);
	    if (code0 == REG && REGNO (op0) < FIRST_PSEUDO_REGISTER)
	      op0 = gen_rtx_REG (word_mode,
				 (REGNO (op0) +
				  subreg_regno_offset (REGNO (SUBREG_REG (orig_op0)),
						       GET_MODE (SUBREG_REG (orig_op0)),
						       SUBREG_BYTE (orig_op0),
						       GET_MODE (orig_op0))));
	  }

	if (GET_CODE (op1) == SUBREG)
	  {
	    op1 = SUBREG_REG (op1);
	    code1 = GET_CODE (op1);
	    if (code1 == REG && REGNO (op1) < FIRST_PSEUDO_REGISTER)
	      /* ??? Why is this given op1's mode and above for
		 ??? op0 SUBREGs we use word_mode?  */
	      op1 = gen_rtx_REG (GET_MODE (op1),
				 (REGNO (op1) +
				  subreg_regno_offset (REGNO (SUBREG_REG (orig_op1)),
						       GET_MODE (SUBREG_REG (orig_op1)),
						       SUBREG_BYTE (orig_op1),
						       GET_MODE (orig_op1))));
	  }
	/* Plus in the index register may be created only as a result of
	   register rematerialization for expression like &localvar*4.  Reload it.
	   It may be possible to combine the displacement on the outer level,
	   but it is probably not worthwhile to do so.  */
	if (context == 1)
	  {
	    find_reloads_address (GET_MODE (x), loc, XEXP (x, 0), &XEXP (x, 0),
				  opnum, ADDR_TYPE (type), ind_levels, insn);
	    push_reload (*loc, NULL_RTX, loc, (rtx*) 0,
			 context_reg_class,
			 GET_MODE (x), VOIDmode, 0, 0, opnum, type);
	    return 1;
	  }

	if (code0 == MULT || code0 == SIGN_EXTEND || code0 == TRUNCATE
	    || code0 == ZERO_EXTEND || code1 == MEM)
	  {
	    find_reloads_address_1 (mode, orig_op0, 1, PLUS, SCRATCH,
				    &XEXP (x, 0), opnum, type, ind_levels,
				    insn);
	    find_reloads_address_1 (mode, orig_op1, 0, PLUS, code0,
				    &XEXP (x, 1), opnum, type, ind_levels,
				    insn);
	  }

	else if (code1 == MULT || code1 == SIGN_EXTEND || code1 == TRUNCATE
		 || code1 == ZERO_EXTEND || code0 == MEM)
	  {
	    find_reloads_address_1 (mode, orig_op0, 0, PLUS, code1,
				    &XEXP (x, 0), opnum, type, ind_levels,
				    insn);
	    find_reloads_address_1 (mode, orig_op1, 1, PLUS, SCRATCH,
				    &XEXP (x, 1), opnum, type, ind_levels,
				    insn);
	  }

	else if (code0 == CONST_INT || code0 == CONST
		 || code0 == SYMBOL_REF || code0 == LABEL_REF)
	  find_reloads_address_1 (mode, orig_op1, 0, PLUS, code0,
				  &XEXP (x, 1), opnum, type, ind_levels,
				  insn);

	else if (code1 == CONST_INT || code1 == CONST
		 || code1 == SYMBOL_REF || code1 == LABEL_REF)
	  find_reloads_address_1 (mode, orig_op0, 0, PLUS, code1,
				  &XEXP (x, 0), opnum, type, ind_levels,
				  insn);

	else if (code0 == REG && code1 == REG)
	  {
	    if (REGNO_OK_FOR_INDEX_P (REGNO (op0))
		&& regno_ok_for_base_p (REGNO (op1), mode, PLUS, REG))
	      return 0;
	    else if (REGNO_OK_FOR_INDEX_P (REGNO (op1))
		     && regno_ok_for_base_p (REGNO (op0), mode, PLUS, REG))
	      return 0;
	    else if (regno_ok_for_base_p (REGNO (op1), mode, PLUS, REG))
	      find_reloads_address_1 (mode, orig_op0, 1, PLUS, SCRATCH,
				      &XEXP (x, 0), opnum, type, ind_levels,
				      insn);
	    else if (regno_ok_for_base_p (REGNO (op0), mode, PLUS, REG))
	      find_reloads_address_1 (mode, orig_op1, 1, PLUS, SCRATCH,
				      &XEXP (x, 1), opnum, type, ind_levels,
				      insn);
	    else if (REGNO_OK_FOR_INDEX_P (REGNO (op1)))
	      find_reloads_address_1 (mode, orig_op0, 0, PLUS, REG,
				      &XEXP (x, 0), opnum, type, ind_levels,
				      insn);
	    else if (REGNO_OK_FOR_INDEX_P (REGNO (op0)))
	      find_reloads_address_1 (mode, orig_op1, 0, PLUS, REG,
				      &XEXP (x, 1), opnum, type, ind_levels,
				      insn);
	    else
	      {
		find_reloads_address_1 (mode, orig_op0, 1, PLUS, SCRATCH,
					&XEXP (x, 0), opnum, type, ind_levels,
					insn);
		find_reloads_address_1 (mode, orig_op1, 0, PLUS, REG,
					&XEXP (x, 1), opnum, type, ind_levels,
					insn);
	      }
	  }

	else if (code0 == REG)
	  {
	    find_reloads_address_1 (mode, orig_op0, 1, PLUS, SCRATCH,
				    &XEXP (x, 0), opnum, type, ind_levels,
				    insn);
	    find_reloads_address_1 (mode, orig_op1, 0, PLUS, REG,
				    &XEXP (x, 1), opnum, type, ind_levels,
				    insn);
	  }

	else if (code1 == REG)
	  {
	    find_reloads_address_1 (mode, orig_op1, 1, PLUS, SCRATCH,
				    &XEXP (x, 1), opnum, type, ind_levels,
				    insn);
	    find_reloads_address_1 (mode, orig_op0, 0, PLUS, REG,
				    &XEXP (x, 0), opnum, type, ind_levels,
				    insn);
	  }
      }

      return 0;

    case POST_MODIFY:
    case PRE_MODIFY:
      {
	rtx op0 = XEXP (x, 0);
	rtx op1 = XEXP (x, 1);
	enum rtx_code index_code;
	int regno;
	int reloadnum;

	if (GET_CODE (op1) != PLUS && GET_CODE (op1) != MINUS)
	  return 0;

	/* Currently, we only support {PRE,POST}_MODIFY constructs
	   where a base register is {inc,dec}remented by the contents
	   of another register or by a constant value.  Thus, these
	   operands must match.  */
	gcc_assert (op0 == XEXP (op1, 0));

	/* Require index register (or constant).  Let's just handle the
	   register case in the meantime... If the target allows
	   auto-modify by a constant then we could try replacing a pseudo
	   register with its equivalent constant where applicable.

	   If we later decide to reload the whole PRE_MODIFY or
	   POST_MODIFY, inc_for_reload might clobber the reload register
	   before reading the index.  The index register might therefore
	   need to live longer than a TYPE reload normally would, so be
	   conservative and class it as RELOAD_OTHER.  */
	if (REG_P (XEXP (op1, 1)))
	  if (!REGNO_OK_FOR_INDEX_P (REGNO (XEXP (op1, 1))))
	    find_reloads_address_1 (mode, XEXP (op1, 1), 1, code, SCRATCH,
				    &XEXP (op1, 1), opnum, RELOAD_OTHER,
				    ind_levels, insn);

	gcc_assert (REG_P (XEXP (op1, 0)));

	regno = REGNO (XEXP (op1, 0));
	index_code = GET_CODE (XEXP (op1, 1));

	/* A register that is incremented cannot be constant!  */
	gcc_assert (regno < FIRST_PSEUDO_REGISTER
		    || reg_equiv_constant[regno] == 0);

	/* Handle a register that is equivalent to a memory location
	    which cannot be addressed directly.  */
	if (reg_equiv_memory_loc[regno] != 0
	    && (reg_equiv_address[regno] != 0
		|| num_not_at_initial_offset))
	  {
	    rtx tem = make_memloc (XEXP (x, 0), regno);

	    if (reg_equiv_address[regno]
		|| ! rtx_equal_p (tem, reg_equiv_mem[regno]))
	      {
		rtx orig = tem;

		/* First reload the memory location's address.
		    We can't use ADDR_TYPE (type) here, because we need to
		    write back the value after reading it, hence we actually
		    need two registers.  */
		find_reloads_address (GET_MODE (tem), &tem, XEXP (tem, 0),
				      &XEXP (tem, 0), opnum,
				      RELOAD_OTHER,
				      ind_levels, insn);

		if (tem != orig)
		  push_reg_equiv_alt_mem (regno, tem);

		/* Then reload the memory location into a base
		   register.  */
		reloadnum = push_reload (tem, tem, &XEXP (x, 0),
					 &XEXP (op1, 0),
					 base_reg_class (mode, code,
							 index_code),
					 GET_MODE (x), GET_MODE (x), 0,
					 0, opnum, RELOAD_OTHER);

		update_auto_inc_notes (this_insn, regno, reloadnum);
		return 0;
	      }
	  }

	if (reg_renumber[regno] >= 0)
	  regno = reg_renumber[regno];

	/* We require a base register here...  */
	if (!regno_ok_for_base_p (regno, GET_MODE (x), code, index_code))
	  {
	    reloadnum = push_reload (XEXP (op1, 0), XEXP (x, 0),
				     &XEXP (op1, 0), &XEXP (x, 0),
				     base_reg_class (mode, code, index_code),
				     GET_MODE (x), GET_MODE (x), 0, 0,
				     opnum, RELOAD_OTHER);

	    update_auto_inc_notes (this_insn, regno, reloadnum);
	    return 0;
	  }
      }
      return 0;

    case POST_INC:
    case POST_DEC:
    case PRE_INC:
    case PRE_DEC:
      if (REG_P (XEXP (x, 0)))
	{
	  int regno = REGNO (XEXP (x, 0));
	  int value = 0;
	  rtx x_orig = x;

	  /* A register that is incremented cannot be constant!  */
	  gcc_assert (regno < FIRST_PSEUDO_REGISTER
		      || reg_equiv_constant[regno] == 0);

	  /* Handle a register that is equivalent to a memory location
	     which cannot be addressed directly.  */
	  if (reg_equiv_memory_loc[regno] != 0
	      && (reg_equiv_address[regno] != 0 || num_not_at_initial_offset))
	    {
	      rtx tem = make_memloc (XEXP (x, 0), regno);
	      if (reg_equiv_address[regno]
		  || ! rtx_equal_p (tem, reg_equiv_mem[regno]))
		{
		  rtx orig = tem;

		  /* First reload the memory location's address.
		     We can't use ADDR_TYPE (type) here, because we need to
		     write back the value after reading it, hence we actually
		     need two registers.  */
		  find_reloads_address (GET_MODE (tem), &tem, XEXP (tem, 0),
					&XEXP (tem, 0), opnum, type,
					ind_levels, insn);
		  if (tem != orig)
		    push_reg_equiv_alt_mem (regno, tem);
		  /* Put this inside a new increment-expression.  */
		  x = gen_rtx_fmt_e (GET_CODE (x), GET_MODE (x), tem);
		  /* Proceed to reload that, as if it contained a register.  */
		}
	    }

	  /* If we have a hard register that is ok as an index,
	     don't make a reload.  If an autoincrement of a nice register
	     isn't "valid", it must be that no autoincrement is "valid".
	     If that is true and something made an autoincrement anyway,
	     this must be a special context where one is allowed.
	     (For example, a "push" instruction.)
	     We can't improve this address, so leave it alone.  */

	  /* Otherwise, reload the autoincrement into a suitable hard reg
	     and record how much to increment by.  */

	  if (reg_renumber[regno] >= 0)
	    regno = reg_renumber[regno];
	  if (regno >= FIRST_PSEUDO_REGISTER
	      || !REG_OK_FOR_CONTEXT (context, regno, mode, outer_code,
				      index_code))
	    {
	      int reloadnum;

	      /* If we can output the register afterwards, do so, this
		 saves the extra update.
		 We can do so if we have an INSN - i.e. no JUMP_INSN nor
		 CALL_INSN - and it does not set CC0.
		 But don't do this if we cannot directly address the
		 memory location, since this will make it harder to
		 reuse address reloads, and increases register pressure.
		 Also don't do this if we can probably update x directly.  */
	      rtx equiv = (MEM_P (XEXP (x, 0))
			   ? XEXP (x, 0)
			   : reg_equiv_mem[regno]);
	      int icode = (int) add_optab->handlers[(int) Pmode].insn_code;
	      if (insn && NONJUMP_INSN_P (insn) && equiv
		  && memory_operand (equiv, GET_MODE (equiv))
#ifdef HAVE_cc0
		  && ! sets_cc0_p (PATTERN (insn))
#endif
		  && ! (icode != CODE_FOR_nothing
			&& ((*insn_data[icode].operand[0].predicate)
			    (equiv, Pmode))
			&& ((*insn_data[icode].operand[1].predicate)
			    (equiv, Pmode))))
		{
		  /* We use the original pseudo for loc, so that
		     emit_reload_insns() knows which pseudo this
		     reload refers to and updates the pseudo rtx, not
		     its equivalent memory location, as well as the
		     corresponding entry in reg_last_reload_reg.  */
		  loc = &XEXP (x_orig, 0);
		  x = XEXP (x, 0);
		  reloadnum
		    = push_reload (x, x, loc, loc,
				   context_reg_class,
				   GET_MODE (x), GET_MODE (x), 0, 0,
				   opnum, RELOAD_OTHER);
		}
	      else
		{
		  reloadnum
		    = push_reload (x, NULL_RTX, loc, (rtx*) 0,
				   context_reg_class,
				   GET_MODE (x), GET_MODE (x), 0, 0,
				   opnum, type);
		  rld[reloadnum].inc
		    = find_inc_amount (PATTERN (this_insn), XEXP (x_orig, 0));

		  value = 1;
		}

	      update_auto_inc_notes (this_insn, REGNO (XEXP (x_orig, 0)),
				     reloadnum);
	    }
	  return value;
	}

      else if (MEM_P (XEXP (x, 0)))
	{
	  /* This is probably the result of a substitution, by eliminate_regs,
	     of an equivalent address for a pseudo that was not allocated to a
	     hard register.  Verify that the specified address is valid and
	     reload it into a register.  */
	  /* Variable `tem' might or might not be used in FIND_REG_INC_NOTE.  */
	  rtx tem ATTRIBUTE_UNUSED = XEXP (x, 0);
	  rtx link;
	  int reloadnum;

	  /* Since we know we are going to reload this item, don't decrement
	     for the indirection level.

	     Note that this is actually conservative:  it would be slightly
	     more efficient to use the value of SPILL_INDIRECT_LEVELS from
	     reload1.c here.  */
	  /* We can't use ADDR_TYPE (type) here, because we need to
	     write back the value after reading it, hence we actually
	     need two registers.  */
	  find_reloads_address (GET_MODE (x), &XEXP (x, 0),
				XEXP (XEXP (x, 0), 0), &XEXP (XEXP (x, 0), 0),
				opnum, type, ind_levels, insn);

	  reloadnum = push_reload (x, NULL_RTX, loc, (rtx*) 0,
				   context_reg_class,
				   GET_MODE (x), VOIDmode, 0, 0, opnum, type);
	  rld[reloadnum].inc
	    = find_inc_amount (PATTERN (this_insn), XEXP (x, 0));

	  link = FIND_REG_INC_NOTE (this_insn, tem);
	  if (link != 0)
	    push_replacement (&XEXP (link, 0), reloadnum, VOIDmode);

	  return 1;
	}
      return 0;

    case TRUNCATE:
    case SIGN_EXTEND:
    case ZERO_EXTEND:
      /* Look for parts to reload in the inner expression and reload them
	 too, in addition to this operation.  Reloading all inner parts in
	 addition to this one shouldn't be necessary, but at this point,
	 we don't know if we can possibly omit any part that *can* be
	 reloaded.  Targets that are better off reloading just either part
	 (or perhaps even a different part of an outer expression), should
	 define LEGITIMIZE_RELOAD_ADDRESS.  */
      find_reloads_address_1 (GET_MODE (XEXP (x, 0)), XEXP (x, 0),
			      context, code, SCRATCH, &XEXP (x, 0), opnum,
			      type, ind_levels, insn);
      push_reload (x, NULL_RTX, loc, (rtx*) 0,
		   context_reg_class,
		   GET_MODE (x), VOIDmode, 0, 0, opnum, type);
      return 1;

    case MEM:
      /* This is probably the result of a substitution, by eliminate_regs, of
	 an equivalent address for a pseudo that was not allocated to a hard
	 register.  Verify that the specified address is valid and reload it
	 into a register.

	 Since we know we are going to reload this item, don't decrement for
	 the indirection level.

	 Note that this is actually conservative:  it would be slightly more
	 efficient to use the value of SPILL_INDIRECT_LEVELS from
	 reload1.c here.  */

      find_reloads_address (GET_MODE (x), loc, XEXP (x, 0), &XEXP (x, 0),
			    opnum, ADDR_TYPE (type), ind_levels, insn);
      push_reload (*loc, NULL_RTX, loc, (rtx*) 0,
		   context_reg_class,
		   GET_MODE (x), VOIDmode, 0, 0, opnum, type);
      return 1;

    case REG:
      {
	int regno = REGNO (x);

	if (reg_equiv_constant[regno] != 0)
	  {
	    find_reloads_address_part (reg_equiv_constant[regno], loc,
				       context_reg_class,
				       GET_MODE (x), opnum, type, ind_levels);
	    return 1;
	  }

#if 0 /* This might screw code in reload1.c to delete prior output-reload
	 that feeds this insn.  */
	if (reg_equiv_mem[regno] != 0)
	  {
	    push_reload (reg_equiv_mem[regno], NULL_RTX, loc, (rtx*) 0,
			 context_reg_class,
			 GET_MODE (x), VOIDmode, 0, 0, opnum, type);
	    return 1;
	  }
#endif

	if (reg_equiv_memory_loc[regno]
	    && (reg_equiv_address[regno] != 0 || num_not_at_initial_offset))
	  {
	    rtx tem = make_memloc (x, regno);
	    if (reg_equiv_address[regno] != 0
		|| ! rtx_equal_p (tem, reg_equiv_mem[regno]))
	      {
		x = tem;
		find_reloads_address (GET_MODE (x), &x, XEXP (x, 0),
				      &XEXP (x, 0), opnum, ADDR_TYPE (type),
				      ind_levels, insn);
		if (x != tem)
		  push_reg_equiv_alt_mem (regno, x);
	      }
	  }

	if (reg_renumber[regno] >= 0)
	  regno = reg_renumber[regno];

	if (regno >= FIRST_PSEUDO_REGISTER
	    || !REG_OK_FOR_CONTEXT (context, regno, mode, outer_code,
				    index_code))
	  {
	    push_reload (x, NULL_RTX, loc, (rtx*) 0,
			 context_reg_class,
			 GET_MODE (x), VOIDmode, 0, 0, opnum, type);
	    return 1;
	  }

	/* If a register appearing in an address is the subject of a CLOBBER
	   in this insn, reload it into some other register to be safe.
	   The CLOBBER is supposed to make the register unavailable
	   from before this insn to after it.  */
	if (regno_clobbered_p (regno, this_insn, GET_MODE (x), 0))
	  {
	    push_reload (x, NULL_RTX, loc, (rtx*) 0,
			 context_reg_class,
			 GET_MODE (x), VOIDmode, 0, 0, opnum, type);
	    return 1;
	  }
      }
      return 0;

    case SUBREG:
      if (REG_P (SUBREG_REG (x)))
	{
	  /* If this is a SUBREG of a hard register and the resulting register
	     is of the wrong class, reload the whole SUBREG.  This avoids
	     needless copies if SUBREG_REG is multi-word.  */
	  if (REGNO (SUBREG_REG (x)) < FIRST_PSEUDO_REGISTER)
	    {
	      int regno ATTRIBUTE_UNUSED = subreg_regno (x);

	      if (!REG_OK_FOR_CONTEXT (context, regno, mode, outer_code,
				       index_code))
		{
		  push_reload (x, NULL_RTX, loc, (rtx*) 0,
			       context_reg_class,
			       GET_MODE (x), VOIDmode, 0, 0, opnum, type);
		  return 1;
		}
	    }
	  /* If this is a SUBREG of a pseudo-register, and the pseudo-register
	     is larger than the class size, then reload the whole SUBREG.  */
	  else
	    {
	      enum reg_class class = context_reg_class;
	      if ((unsigned) CLASS_MAX_NREGS (class, GET_MODE (SUBREG_REG (x)))
		  > reg_class_size[class])
		{
		  x = find_reloads_subreg_address (x, 0, opnum, 
						   ADDR_TYPE (type),
						   ind_levels, insn);
		  push_reload (x, NULL_RTX, loc, (rtx*) 0, class,
			       GET_MODE (x), VOIDmode, 0, 0, opnum, type);
		  return 1;
		}
	    }
	}
      break;

    default:
      break;
    }

  {
    const char *fmt = GET_RTX_FORMAT (code);
    int i;

    for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
      {
	if (fmt[i] == 'e')
	  /* Pass SCRATCH for INDEX_CODE, since CODE can never be a PLUS once
	     we get here.  */
	  find_reloads_address_1 (mode, XEXP (x, i), context, code, SCRATCH,
				  &XEXP (x, i), opnum, type, ind_levels, insn);
      }
  }

#undef REG_OK_FOR_CONTEXT
  return 0;
}

/* X, which is found at *LOC, is a part of an address that needs to be
   reloaded into a register of class CLASS.  If X is a constant, or if
   X is a PLUS that contains a constant, check that the constant is a
   legitimate operand and that we are supposed to be able to load
   it into the register.

   If not, force the constant into memory and reload the MEM instead.

   MODE is the mode to use, in case X is an integer constant.

   OPNUM and TYPE describe the purpose of any reloads made.

   IND_LEVELS says how many levels of indirect addressing this machine
   supports.  */

static void
find_reloads_address_part (rtx x, rtx *loc, enum reg_class class,
			   enum machine_mode mode, int opnum,
			   enum reload_type type, int ind_levels)
{
  if (CONSTANT_P (x)
      && (! LEGITIMATE_CONSTANT_P (x)
	  || PREFERRED_RELOAD_CLASS (x, class) == NO_REGS))
    {
      rtx tem;

      tem = x = force_const_mem (mode, x);
      find_reloads_address (mode, &tem, XEXP (tem, 0), &XEXP (tem, 0),
			    opnum, type, ind_levels, 0);
    }

  else if (GET_CODE (x) == PLUS
	   && CONSTANT_P (XEXP (x, 1))
	   && (! LEGITIMATE_CONSTANT_P (XEXP (x, 1))
	       || PREFERRED_RELOAD_CLASS (XEXP (x, 1), class) == NO_REGS))
    {
      rtx tem;

      tem = force_const_mem (GET_MODE (x), XEXP (x, 1));
      x = gen_rtx_PLUS (GET_MODE (x), XEXP (x, 0), tem);
      find_reloads_address (mode, &tem, XEXP (tem, 0), &XEXP (tem, 0),
			    opnum, type, ind_levels, 0);
    }

  push_reload (x, NULL_RTX, loc, (rtx*) 0, class,
	       mode, VOIDmode, 0, 0, opnum, type);
}

/* X, a subreg of a pseudo, is a part of an address that needs to be
   reloaded.

   If the pseudo is equivalent to a memory location that cannot be directly
   addressed, make the necessary address reloads.

   If address reloads have been necessary, or if the address is changed
   by register elimination, return the rtx of the memory location;
   otherwise, return X.

   If FORCE_REPLACE is nonzero, unconditionally replace the subreg with the
   memory location.

   OPNUM and TYPE identify the purpose of the reload.

   IND_LEVELS says how many levels of indirect addressing are
   supported at this point in the address.

   INSN, if nonzero, is the insn in which we do the reload.  It is used
   to determine where to put USEs for pseudos that we have to replace with
   stack slots.  */

static rtx
find_reloads_subreg_address (rtx x, int force_replace, int opnum,
			     enum reload_type type, int ind_levels, rtx insn)
{
  int regno = REGNO (SUBREG_REG (x));

  if (reg_equiv_memory_loc[regno])
    {
      /* If the address is not directly addressable, or if the address is not
	 offsettable, then it must be replaced.  */
      if (! force_replace
	  && (reg_equiv_address[regno]
	      || ! offsettable_memref_p (reg_equiv_mem[regno])))
	force_replace = 1;

      if (force_replace || num_not_at_initial_offset)
	{
	  rtx tem = make_memloc (SUBREG_REG (x), regno);

	  /* If the address changes because of register elimination, then
	     it must be replaced.  */
	  if (force_replace
	      || ! rtx_equal_p (tem, reg_equiv_mem[regno]))
	    {
	      unsigned outer_size = GET_MODE_SIZE (GET_MODE (x));
	      unsigned inner_size = GET_MODE_SIZE (GET_MODE (SUBREG_REG (x)));
	      int offset;
	      rtx orig = tem;
	      enum machine_mode orig_mode = GET_MODE (orig);
	      int reloaded;

	      /* For big-endian paradoxical subregs, SUBREG_BYTE does not
		 hold the correct (negative) byte offset.  */
	      if (BYTES_BIG_ENDIAN && outer_size > inner_size)
		offset = inner_size - outer_size;
	      else
		offset = SUBREG_BYTE (x);

	      XEXP (tem, 0) = plus_constant (XEXP (tem, 0), offset);
	      PUT_MODE (tem, GET_MODE (x));

	      /* If this was a paradoxical subreg that we replaced, the
		 resulting memory must be sufficiently aligned to allow
		 us to widen the mode of the memory.  */
	      if (outer_size > inner_size)
		{
		  rtx base;

		  base = XEXP (tem, 0);
		  if (GET_CODE (base) == PLUS)
		    {
		      if (GET_CODE (XEXP (base, 1)) == CONST_INT
			  && INTVAL (XEXP (base, 1)) % outer_size != 0)
			return x;
		      base = XEXP (base, 0);
		    }
		  if (!REG_P (base)
		      || (REGNO_POINTER_ALIGN (REGNO (base))
			  < outer_size * BITS_PER_UNIT))
		    return x;
		}

	      reloaded = find_reloads_address (GET_MODE (tem), &tem,
					       XEXP (tem, 0), &XEXP (tem, 0),
					       opnum, type, ind_levels, insn);
	      /* ??? Do we need to handle nonzero offsets somehow?  */
	      if (!offset && tem != orig)
		push_reg_equiv_alt_mem (regno, tem);

	      /* For some processors an address may be valid in the
		 original mode but not in a smaller mode.  For
		 example, ARM accepts a scaled index register in
		 SImode but not in HImode.  find_reloads_address
		 assumes that we pass it a valid address, and doesn't
		 force a reload.  This will probably be fine if
		 find_reloads_address finds some reloads.  But if it
		 doesn't find any, then we may have just converted a
		 valid address into an invalid one.  Check for that
		 here.  */
	      if (reloaded != 1
		  && strict_memory_address_p (orig_mode, XEXP (tem, 0))
		  && !strict_memory_address_p (GET_MODE (tem),
					       XEXP (tem, 0)))
		push_reload (XEXP (tem, 0), NULL_RTX, &XEXP (tem, 0), (rtx*) 0,
			     base_reg_class (GET_MODE (tem), MEM, SCRATCH),
			     GET_MODE (XEXP (tem, 0)), VOIDmode, 0, 0,
			     opnum, type);

	      /* If this is not a toplevel operand, find_reloads doesn't see
		 this substitution.  We have to emit a USE of the pseudo so
		 that delete_output_reload can see it.  */
	      if (replace_reloads && recog_data.operand[opnum] != x)
		/* We mark the USE with QImode so that we recognize it
		   as one that can be safely deleted at the end of
		   reload.  */
		PUT_MODE (emit_insn_before (gen_rtx_USE (VOIDmode,
							 SUBREG_REG (x)),
					    insn), QImode);
	      x = tem;
	    }
	}
    }
  return x;
}

/* Substitute into the current INSN the registers into which we have reloaded
   the things that need reloading.  The array `replacements'
   contains the locations of all pointers that must be changed
   and says what to replace them with.

   Return the rtx that X translates into; usually X, but modified.  */

void
subst_reloads (rtx insn)
{
  int i;

  for (i = 0; i < n_replacements; i++)
    {
      struct replacement *r = &replacements[i];
      rtx reloadreg = rld[r->what].reg_rtx;
      if (reloadreg)
	{
#ifdef ENABLE_CHECKING
	  /* Internal consistency test.  Check that we don't modify
	     anything in the equivalence arrays.  Whenever something from
	     those arrays needs to be reloaded, it must be unshared before
	     being substituted into; the equivalence must not be modified.
	     Otherwise, if the equivalence is used after that, it will
	     have been modified, and the thing substituted (probably a
	     register) is likely overwritten and not a usable equivalence.  */
	  int check_regno;

	  for (check_regno = 0; check_regno < max_regno; check_regno++)
	    {
#define CHECK_MODF(ARRAY)						\
	      gcc_assert (!ARRAY[check_regno]				\
			  || !loc_mentioned_in_p (r->where,		\
						  ARRAY[check_regno]))

	      CHECK_MODF (reg_equiv_constant);
	      CHECK_MODF (reg_equiv_memory_loc);
	      CHECK_MODF (reg_equiv_address);
	      CHECK_MODF (reg_equiv_mem);
#undef CHECK_MODF
	    }
#endif /* ENABLE_CHECKING */

	  /* If we're replacing a LABEL_REF with a register, add a
	     REG_LABEL note to indicate to flow which label this
	     register refers to.  */
	  if (GET_CODE (*r->where) == LABEL_REF
	      && JUMP_P (insn))
	    {
	      REG_NOTES (insn) = gen_rtx_INSN_LIST (REG_LABEL,
						    XEXP (*r->where, 0),
						    REG_NOTES (insn));
	      JUMP_LABEL (insn) = XEXP (*r->where, 0);
	   }

	  /* Encapsulate RELOADREG so its machine mode matches what
	     used to be there.  Note that gen_lowpart_common will
	     do the wrong thing if RELOADREG is multi-word.  RELOADREG
	     will always be a REG here.  */
	  if (GET_MODE (reloadreg) != r->mode && r->mode != VOIDmode)
	    reloadreg = reload_adjust_reg_for_mode (reloadreg, r->mode);

	  /* If we are putting this into a SUBREG and RELOADREG is a
	     SUBREG, we would be making nested SUBREGs, so we have to fix
	     this up.  Note that r->where == &SUBREG_REG (*r->subreg_loc).  */

	  if (r->subreg_loc != 0 && GET_CODE (reloadreg) == SUBREG)
	    {
	      if (GET_MODE (*r->subreg_loc)
		  == GET_MODE (SUBREG_REG (reloadreg)))
		*r->subreg_loc = SUBREG_REG (reloadreg);
	      else
		{
		  int final_offset =
		    SUBREG_BYTE (*r->subreg_loc) + SUBREG_BYTE (reloadreg);

		  /* When working with SUBREGs the rule is that the byte
		     offset must be a multiple of the SUBREG's mode.  */
		  final_offset = (final_offset /
				  GET_MODE_SIZE (GET_MODE (*r->subreg_loc)));
		  final_offset = (final_offset *
				  GET_MODE_SIZE (GET_MODE (*r->subreg_loc)));

		  *r->where = SUBREG_REG (reloadreg);
		  SUBREG_BYTE (*r->subreg_loc) = final_offset;
		}
	    }
	  else
	    *r->where = reloadreg;
	}
      /* If reload got no reg and isn't optional, something's wrong.  */
      else
	gcc_assert (rld[r->what].optional);
    }
}

/* Make a copy of any replacements being done into X and move those
   copies to locations in Y, a copy of X.  */

void
copy_replacements (rtx x, rtx y)
{
  /* We can't support X being a SUBREG because we might then need to know its
     location if something inside it was replaced.  */
  gcc_assert (GET_CODE (x) != SUBREG);

  copy_replacements_1 (&x, &y, n_replacements);
}

static void
copy_replacements_1 (rtx *px, rtx *py, int orig_replacements)
{
  int i, j;
  rtx x, y;
  struct replacement *r;
  enum rtx_code code;
  const char *fmt;

  for (j = 0; j < orig_replacements; j++)
    {
      if (replacements[j].subreg_loc == px)
	{
	  r = &replacements[n_replacements++];
	  r->where = replacements[j].where;
	  r->subreg_loc = py;
	  r->what = replacements[j].what;
	  r->mode = replacements[j].mode;
	}
      else if (replacements[j].where == px)
	{
	  r = &replacements[n_replacements++];
	  r->where = py;
	  r->subreg_loc = 0;
	  r->what = replacements[j].what;
	  r->mode = replacements[j].mode;
	}
    }

  x = *px;
  y = *py;
  code = GET_CODE (x);
  fmt = GET_RTX_FORMAT (code);

  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	copy_replacements_1 (&XEXP (x, i), &XEXP (y, i), orig_replacements);
      else if (fmt[i] == 'E')
	for (j = XVECLEN (x, i); --j >= 0; )
	  copy_replacements_1 (&XVECEXP (x, i, j), &XVECEXP (y, i, j),
			       orig_replacements);
    }
}

/* Change any replacements being done to *X to be done to *Y.  */

void
move_replacements (rtx *x, rtx *y)
{
  int i;

  for (i = 0; i < n_replacements; i++)
    if (replacements[i].subreg_loc == x)
      replacements[i].subreg_loc = y;
    else if (replacements[i].where == x)
      {
	replacements[i].where = y;
	replacements[i].subreg_loc = 0;
      }
}

/* If LOC was scheduled to be replaced by something, return the replacement.
   Otherwise, return *LOC.  */

rtx
find_replacement (rtx *loc)
{
  struct replacement *r;

  for (r = &replacements[0]; r < &replacements[n_replacements]; r++)
    {
      rtx reloadreg = rld[r->what].reg_rtx;

      if (reloadreg && r->where == loc)
	{
	  if (r->mode != VOIDmode && GET_MODE (reloadreg) != r->mode)
	    reloadreg = gen_rtx_REG (r->mode, REGNO (reloadreg));

	  return reloadreg;
	}
      else if (reloadreg && r->subreg_loc == loc)
	{
	  /* RELOADREG must be either a REG or a SUBREG.

	     ??? Is it actually still ever a SUBREG?  If so, why?  */

	  if (REG_P (reloadreg))
	    return gen_rtx_REG (GET_MODE (*loc),
				(REGNO (reloadreg) +
				 subreg_regno_offset (REGNO (SUBREG_REG (*loc)),
						      GET_MODE (SUBREG_REG (*loc)),
						      SUBREG_BYTE (*loc),
						      GET_MODE (*loc))));
	  else if (GET_MODE (reloadreg) == GET_MODE (*loc))
	    return reloadreg;
	  else
	    {
	      int final_offset = SUBREG_BYTE (reloadreg) + SUBREG_BYTE (*loc);

	      /* When working with SUBREGs the rule is that the byte
		 offset must be a multiple of the SUBREG's mode.  */
	      final_offset = (final_offset / GET_MODE_SIZE (GET_MODE (*loc)));
	      final_offset = (final_offset * GET_MODE_SIZE (GET_MODE (*loc)));
	      return gen_rtx_SUBREG (GET_MODE (*loc), SUBREG_REG (reloadreg),
				     final_offset);
	    }
	}
    }

  /* If *LOC is a PLUS, MINUS, or MULT, see if a replacement is scheduled for
     what's inside and make a new rtl if so.  */
  if (GET_CODE (*loc) == PLUS || GET_CODE (*loc) == MINUS
      || GET_CODE (*loc) == MULT)
    {
      rtx x = find_replacement (&XEXP (*loc, 0));
      rtx y = find_replacement (&XEXP (*loc, 1));

      if (x != XEXP (*loc, 0) || y != XEXP (*loc, 1))
	return gen_rtx_fmt_ee (GET_CODE (*loc), GET_MODE (*loc), x, y);
    }

  return *loc;
}

/* Return nonzero if register in range [REGNO, ENDREGNO)
   appears either explicitly or implicitly in X
   other than being stored into (except for earlyclobber operands).

   References contained within the substructure at LOC do not count.
   LOC may be zero, meaning don't ignore anything.

   This is similar to refers_to_regno_p in rtlanal.c except that we
   look at equivalences for pseudos that didn't get hard registers.  */

static int
refers_to_regno_for_reload_p (unsigned int regno, unsigned int endregno,
			      rtx x, rtx *loc)
{
  int i;
  unsigned int r;
  RTX_CODE code;
  const char *fmt;

  if (x == 0)
    return 0;

 repeat:
  code = GET_CODE (x);

  switch (code)
    {
    case REG:
      r = REGNO (x);

      /* If this is a pseudo, a hard register must not have been allocated.
	 X must therefore either be a constant or be in memory.  */
      if (r >= FIRST_PSEUDO_REGISTER)
	{
	  if (reg_equiv_memory_loc[r])
	    return refers_to_regno_for_reload_p (regno, endregno,
						 reg_equiv_memory_loc[r],
						 (rtx*) 0);

	  gcc_assert (reg_equiv_constant[r] || reg_equiv_invariant[r]);
	  return 0;
	}

      return (endregno > r
	      && regno < r + (r < FIRST_PSEUDO_REGISTER
			      ? hard_regno_nregs[r][GET_MODE (x)]
			      : 1));

    case SUBREG:
      /* If this is a SUBREG of a hard reg, we can see exactly which
	 registers are being modified.  Otherwise, handle normally.  */
      if (REG_P (SUBREG_REG (x))
	  && REGNO (SUBREG_REG (x)) < FIRST_PSEUDO_REGISTER)
	{
	  unsigned int inner_regno = subreg_regno (x);
	  unsigned int inner_endregno
	    = inner_regno + (inner_regno < FIRST_PSEUDO_REGISTER
			     ? hard_regno_nregs[inner_regno][GET_MODE (x)] : 1);

	  return endregno > inner_regno && regno < inner_endregno;
	}
      break;

    case CLOBBER:
    case SET:
      if (&SET_DEST (x) != loc
	  /* Note setting a SUBREG counts as referring to the REG it is in for
	     a pseudo but not for hard registers since we can
	     treat each word individually.  */
	  && ((GET_CODE (SET_DEST (x)) == SUBREG
	       && loc != &SUBREG_REG (SET_DEST (x))
	       && REG_P (SUBREG_REG (SET_DEST (x)))
	       && REGNO (SUBREG_REG (SET_DEST (x))) >= FIRST_PSEUDO_REGISTER
	       && refers_to_regno_for_reload_p (regno, endregno,
						SUBREG_REG (SET_DEST (x)),
						loc))
	      /* If the output is an earlyclobber operand, this is
		 a conflict.  */
	      || ((!REG_P (SET_DEST (x))
		   || earlyclobber_operand_p (SET_DEST (x)))
		  && refers_to_regno_for_reload_p (regno, endregno,
						   SET_DEST (x), loc))))
	return 1;

      if (code == CLOBBER || loc == &SET_SRC (x))
	return 0;
      x = SET_SRC (x);
      goto repeat;

    default:
      break;
    }

  /* X does not match, so try its subexpressions.  */

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e' && loc != &XEXP (x, i))
	{
	  if (i == 0)
	    {
	      x = XEXP (x, 0);
	      goto repeat;
	    }
	  else
	    if (refers_to_regno_for_reload_p (regno, endregno,
					      XEXP (x, i), loc))
	      return 1;
	}
      else if (fmt[i] == 'E')
	{
	  int j;
	  for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	    if (loc != &XVECEXP (x, i, j)
		&& refers_to_regno_for_reload_p (regno, endregno,
						 XVECEXP (x, i, j), loc))
	      return 1;
	}
    }
  return 0;
}

/* Nonzero if modifying X will affect IN.  If X is a register or a SUBREG,
   we check if any register number in X conflicts with the relevant register
   numbers.  If X is a constant, return 0.  If X is a MEM, return 1 iff IN
   contains a MEM (we don't bother checking for memory addresses that can't
   conflict because we expect this to be a rare case.

   This function is similar to reg_overlap_mentioned_p in rtlanal.c except
   that we look at equivalences for pseudos that didn't get hard registers.  */

int
reg_overlap_mentioned_for_reload_p (rtx x, rtx in)
{
  int regno, endregno;

  /* Overly conservative.  */
  if (GET_CODE (x) == STRICT_LOW_PART
      || GET_RTX_CLASS (GET_CODE (x)) == RTX_AUTOINC)
    x = XEXP (x, 0);

  /* If either argument is a constant, then modifying X can not affect IN.  */
  if (CONSTANT_P (x) || CONSTANT_P (in))
    return 0;
  else if (GET_CODE (x) == SUBREG && GET_CODE (SUBREG_REG (x)) == MEM)
    return refers_to_mem_for_reload_p (in);
  else if (GET_CODE (x) == SUBREG)
    {
      regno = REGNO (SUBREG_REG (x));
      if (regno < FIRST_PSEUDO_REGISTER)
	regno += subreg_regno_offset (REGNO (SUBREG_REG (x)),
				      GET_MODE (SUBREG_REG (x)),
				      SUBREG_BYTE (x),
				      GET_MODE (x));
    }
  else if (REG_P (x))
    {
      regno = REGNO (x);

      /* If this is a pseudo, it must not have been assigned a hard register.
	 Therefore, it must either be in memory or be a constant.  */

      if (regno >= FIRST_PSEUDO_REGISTER)
	{
	  if (reg_equiv_memory_loc[regno])
	    return refers_to_mem_for_reload_p (in);
	  gcc_assert (reg_equiv_constant[regno]);
	  return 0;
	}
    }
  else if (MEM_P (x))
    return refers_to_mem_for_reload_p (in);
  else if (GET_CODE (x) == SCRATCH || GET_CODE (x) == PC
	   || GET_CODE (x) == CC0)
    return reg_mentioned_p (x, in);
  else 
    {
      gcc_assert (GET_CODE (x) == PLUS);

      /* We actually want to know if X is mentioned somewhere inside IN.
	 We must not say that (plus (sp) (const_int 124)) is in
	 (plus (sp) (const_int 64)), since that can lead to incorrect reload
	 allocation when spuriously changing a RELOAD_FOR_OUTPUT_ADDRESS
	 into a RELOAD_OTHER on behalf of another RELOAD_OTHER.  */
      while (MEM_P (in))
	in = XEXP (in, 0);
      if (REG_P (in))
	return 0;
      else if (GET_CODE (in) == PLUS)
	return (rtx_equal_p (x, in)
		|| reg_overlap_mentioned_for_reload_p (x, XEXP (in, 0))
		|| reg_overlap_mentioned_for_reload_p (x, XEXP (in, 1)));
      else return (reg_overlap_mentioned_for_reload_p (XEXP (x, 0), in)
		   || reg_overlap_mentioned_for_reload_p (XEXP (x, 1), in));
    }

  endregno = regno + (regno < FIRST_PSEUDO_REGISTER
		      ? hard_regno_nregs[regno][GET_MODE (x)] : 1);

  return refers_to_regno_for_reload_p (regno, endregno, in, (rtx*) 0);
}

/* Return nonzero if anything in X contains a MEM.  Look also for pseudo
   registers.  */

static int
refers_to_mem_for_reload_p (rtx x)
{
  const char *fmt;
  int i;

  if (MEM_P (x))
    return 1;

  if (REG_P (x))
    return (REGNO (x) >= FIRST_PSEUDO_REGISTER
	    && reg_equiv_memory_loc[REGNO (x)]);

  fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0; i--)
    if (fmt[i] == 'e'
	&& (MEM_P (XEXP (x, i))
	    || refers_to_mem_for_reload_p (XEXP (x, i))))
      return 1;

  return 0;
}

/* Check the insns before INSN to see if there is a suitable register
   containing the same value as GOAL.
   If OTHER is -1, look for a register in class CLASS.
   Otherwise, just see if register number OTHER shares GOAL's value.

   Return an rtx for the register found, or zero if none is found.

   If RELOAD_REG_P is (short *)1,
   we reject any hard reg that appears in reload_reg_rtx
   because such a hard reg is also needed coming into this insn.

   If RELOAD_REG_P is any other nonzero value,
   it is a vector indexed by hard reg number
   and we reject any hard reg whose element in the vector is nonnegative
   as well as any that appears in reload_reg_rtx.

   If GOAL is zero, then GOALREG is a register number; we look
   for an equivalent for that register.

   MODE is the machine mode of the value we want an equivalence for.
   If GOAL is nonzero and not VOIDmode, then it must have mode MODE.

   This function is used by jump.c as well as in the reload pass.

   If GOAL is the sum of the stack pointer and a constant, we treat it
   as if it were a constant except that sp is required to be unchanging.  */

rtx
find_equiv_reg (rtx goal, rtx insn, enum reg_class class, int other,
		short *reload_reg_p, int goalreg, enum machine_mode mode)
{
  rtx p = insn;
  rtx goaltry, valtry, value, where;
  rtx pat;
  int regno = -1;
  int valueno;
  int goal_mem = 0;
  int goal_const = 0;
  int goal_mem_addr_varies = 0;
  int need_stable_sp = 0;
  int nregs;
  int valuenregs;
  int num = 0;

  if (goal == 0)
    regno = goalreg;
  else if (REG_P (goal))
    regno = REGNO (goal);
  else if (MEM_P (goal))
    {
      enum rtx_code code = GET_CODE (XEXP (goal, 0));
      if (MEM_VOLATILE_P (goal))
	return 0;
      if (flag_float_store && SCALAR_FLOAT_MODE_P (GET_MODE (goal)))
	return 0;
      /* An address with side effects must be reexecuted.  */
      switch (code)
	{
	case POST_INC:
	case PRE_INC:
	case POST_DEC:
	case PRE_DEC:
	case POST_MODIFY:
	case PRE_MODIFY:
	  return 0;
	default:
	  break;
	}
      goal_mem = 1;
    }
  else if (CONSTANT_P (goal))
    goal_const = 1;
  else if (GET_CODE (goal) == PLUS
	   && XEXP (goal, 0) == stack_pointer_rtx
	   && CONSTANT_P (XEXP (goal, 1)))
    goal_const = need_stable_sp = 1;
  else if (GET_CODE (goal) == PLUS
	   && XEXP (goal, 0) == frame_pointer_rtx
	   && CONSTANT_P (XEXP (goal, 1)))
    goal_const = 1;
  else
    return 0;

  num = 0;
  /* Scan insns back from INSN, looking for one that copies
     a value into or out of GOAL.
     Stop and give up if we reach a label.  */

  while (1)
    {
      p = PREV_INSN (p);
      num++;
      if (p == 0 || LABEL_P (p)
	  || num > PARAM_VALUE (PARAM_MAX_RELOAD_SEARCH_INSNS))
	return 0;

      if (NONJUMP_INSN_P (p)
	  /* If we don't want spill regs ...  */
	  && (! (reload_reg_p != 0
		 && reload_reg_p != (short *) (HOST_WIDE_INT) 1)
	      /* ... then ignore insns introduced by reload; they aren't
		 useful and can cause results in reload_as_needed to be
		 different from what they were when calculating the need for
		 spills.  If we notice an input-reload insn here, we will
		 reject it below, but it might hide a usable equivalent.
		 That makes bad code.  It may even fail: perhaps no reg was
		 spilled for this insn because it was assumed we would find
		 that equivalent.  */
	      || INSN_UID (p) < reload_first_uid))
	{
	  rtx tem;
	  pat = single_set (p);

	  /* First check for something that sets some reg equal to GOAL.  */
	  if (pat != 0
	      && ((regno >= 0
		   && true_regnum (SET_SRC (pat)) == regno
		   && (valueno = true_regnum (valtry = SET_DEST (pat))) >= 0)
		  ||
		  (regno >= 0
		   && true_regnum (SET_DEST (pat)) == regno
		   && (valueno = true_regnum (valtry = SET_SRC (pat))) >= 0)
		  ||
		  (goal_const && rtx_equal_p (SET_SRC (pat), goal)
		   /* When looking for stack pointer + const,
		      make sure we don't use a stack adjust.  */
		   && !reg_overlap_mentioned_for_reload_p (SET_DEST (pat), goal)
		   && (valueno = true_regnum (valtry = SET_DEST (pat))) >= 0)
		  || (goal_mem
		      && (valueno = true_regnum (valtry = SET_DEST (pat))) >= 0
		      && rtx_renumbered_equal_p (goal, SET_SRC (pat)))
		  || (goal_mem
		      && (valueno = true_regnum (valtry = SET_SRC (pat))) >= 0
		      && rtx_renumbered_equal_p (goal, SET_DEST (pat)))
		  /* If we are looking for a constant,
		     and something equivalent to that constant was copied
		     into a reg, we can use that reg.  */
		  || (goal_const && REG_NOTES (p) != 0
		      && (tem = find_reg_note (p, REG_EQUIV, NULL_RTX))
		      && ((rtx_equal_p (XEXP (tem, 0), goal)
			   && (valueno
			       = true_regnum (valtry = SET_DEST (pat))) >= 0)
			  || (REG_P (SET_DEST (pat))
			      && GET_CODE (XEXP (tem, 0)) == CONST_DOUBLE
			      && SCALAR_FLOAT_MODE_P (GET_MODE (XEXP (tem, 0)))
			      && GET_CODE (goal) == CONST_INT
			      && 0 != (goaltry
				       = operand_subword (XEXP (tem, 0), 0, 0,
							  VOIDmode))
			      && rtx_equal_p (goal, goaltry)
			      && (valtry
				  = operand_subword (SET_DEST (pat), 0, 0,
						     VOIDmode))
			      && (valueno = true_regnum (valtry)) >= 0)))
		  || (goal_const && (tem = find_reg_note (p, REG_EQUIV,
							  NULL_RTX))
		      && REG_P (SET_DEST (pat))
		      && GET_CODE (XEXP (tem, 0)) == CONST_DOUBLE
		      && SCALAR_FLOAT_MODE_P (GET_MODE (XEXP (tem, 0)))
		      && GET_CODE (goal) == CONST_INT
		      && 0 != (goaltry = operand_subword (XEXP (tem, 0), 1, 0,
							  VOIDmode))
		      && rtx_equal_p (goal, goaltry)
		      && (valtry
			  = operand_subword (SET_DEST (pat), 1, 0, VOIDmode))
		      && (valueno = true_regnum (valtry)) >= 0)))
	    {
	      if (other >= 0)
		{
		  if (valueno != other)
		    continue;
		}
	      else if ((unsigned) valueno >= FIRST_PSEUDO_REGISTER)
		continue;
	      else
		{
		  int i;

		  for (i = hard_regno_nregs[valueno][mode] - 1; i >= 0; i--)
		    if (! TEST_HARD_REG_BIT (reg_class_contents[(int) class],
					     valueno + i))
		      break;
		  if (i >= 0)
		    continue;
		}
	      value = valtry;
	      where = p;
	      break;
	    }
	}
    }

  /* We found a previous insn copying GOAL into a suitable other reg VALUE
     (or copying VALUE into GOAL, if GOAL is also a register).
     Now verify that VALUE is really valid.  */

  /* VALUENO is the register number of VALUE; a hard register.  */

  /* Don't try to re-use something that is killed in this insn.  We want
     to be able to trust REG_UNUSED notes.  */
  if (REG_NOTES (where) != 0 && find_reg_note (where, REG_UNUSED, value))
    return 0;

  /* If we propose to get the value from the stack pointer or if GOAL is
     a MEM based on the stack pointer, we need a stable SP.  */
  if (valueno == STACK_POINTER_REGNUM || regno == STACK_POINTER_REGNUM
      || (goal_mem && reg_overlap_mentioned_for_reload_p (stack_pointer_rtx,
							  goal)))
    need_stable_sp = 1;

  /* Reject VALUE if the copy-insn moved the wrong sort of datum.  */
  if (GET_MODE (value) != mode)
    return 0;

  /* Reject VALUE if it was loaded from GOAL
     and is also a register that appears in the address of GOAL.  */

  if (goal_mem && value == SET_DEST (single_set (where))
      && refers_to_regno_for_reload_p (valueno,
				       (valueno
					+ hard_regno_nregs[valueno][mode]),
				       goal, (rtx*) 0))
    return 0;

  /* Reject registers that overlap GOAL.  */

  if (regno >= 0 && regno < FIRST_PSEUDO_REGISTER)
    nregs = hard_regno_nregs[regno][mode];
  else
    nregs = 1;
  valuenregs = hard_regno_nregs[valueno][mode];

  if (!goal_mem && !goal_const
      && regno + nregs > valueno && regno < valueno + valuenregs)
    return 0;

  /* Reject VALUE if it is one of the regs reserved for reloads.
     Reload1 knows how to reuse them anyway, and it would get
     confused if we allocated one without its knowledge.
     (Now that insns introduced by reload are ignored above,
     this case shouldn't happen, but I'm not positive.)  */

  if (reload_reg_p != 0 && reload_reg_p != (short *) (HOST_WIDE_INT) 1)
    {
      int i;
      for (i = 0; i < valuenregs; ++i)
	if (reload_reg_p[valueno + i] >= 0)
	  return 0;
    }

  /* Reject VALUE if it is a register being used for an input reload
     even if it is not one of those reserved.  */

  if (reload_reg_p != 0)
    {
      int i;
      for (i = 0; i < n_reloads; i++)
	if (rld[i].reg_rtx != 0 && rld[i].in)
	  {
	    int regno1 = REGNO (rld[i].reg_rtx);
	    int nregs1 = hard_regno_nregs[regno1]
					 [GET_MODE (rld[i].reg_rtx)];
	    if (regno1 < valueno + valuenregs
		&& regno1 + nregs1 > valueno)
	      return 0;
	  }
    }

  if (goal_mem)
    /* We must treat frame pointer as varying here,
       since it can vary--in a nonlocal goto as generated by expand_goto.  */
    goal_mem_addr_varies = !CONSTANT_ADDRESS_P (XEXP (goal, 0));

  /* Now verify that the values of GOAL and VALUE remain unaltered
     until INSN is reached.  */

  p = insn;
  while (1)
    {
      p = PREV_INSN (p);
      if (p == where)
	return value;

      /* Don't trust the conversion past a function call
	 if either of the two is in a call-clobbered register, or memory.  */
      if (CALL_P (p))
	{
	  int i;

	  if (goal_mem || need_stable_sp)
	    return 0;

	  if (regno >= 0 && regno < FIRST_PSEUDO_REGISTER)
	    for (i = 0; i < nregs; ++i)
	      if (call_used_regs[regno + i]
		  || HARD_REGNO_CALL_PART_CLOBBERED (regno + i, mode))
		return 0;

	  if (valueno >= 0 && valueno < FIRST_PSEUDO_REGISTER)
	    for (i = 0; i < valuenregs; ++i)
	      if (call_used_regs[valueno + i]
		  || HARD_REGNO_CALL_PART_CLOBBERED (valueno + i, mode))
		return 0;
	}

      if (INSN_P (p))
	{
	  pat = PATTERN (p);

	  /* Watch out for unspec_volatile, and volatile asms.  */
	  if (volatile_insn_p (pat))
	    return 0;

	  /* If this insn P stores in either GOAL or VALUE, return 0.
	     If GOAL is a memory ref and this insn writes memory, return 0.
	     If GOAL is a memory ref and its address is not constant,
	     and this insn P changes a register used in GOAL, return 0.  */

	  if (GET_CODE (pat) == COND_EXEC)
	    pat = COND_EXEC_CODE (pat);
	  if (GET_CODE (pat) == SET || GET_CODE (pat) == CLOBBER)
	    {
	      rtx dest = SET_DEST (pat);
	      while (GET_CODE (dest) == SUBREG
		     || GET_CODE (dest) == ZERO_EXTRACT
		     || GET_CODE (dest) == STRICT_LOW_PART)
		dest = XEXP (dest, 0);
	      if (REG_P (dest))
		{
		  int xregno = REGNO (dest);
		  int xnregs;
		  if (REGNO (dest) < FIRST_PSEUDO_REGISTER)
		    xnregs = hard_regno_nregs[xregno][GET_MODE (dest)];
		  else
		    xnregs = 1;
		  if (xregno < regno + nregs && xregno + xnregs > regno)
		    return 0;
		  if (xregno < valueno + valuenregs
		      && xregno + xnregs > valueno)
		    return 0;
		  if (goal_mem_addr_varies
		      && reg_overlap_mentioned_for_reload_p (dest, goal))
		    return 0;
		  if (xregno == STACK_POINTER_REGNUM && need_stable_sp)
		    return 0;
		}
	      else if (goal_mem && MEM_P (dest)
		       && ! push_operand (dest, GET_MODE (dest)))
		return 0;
	      else if (MEM_P (dest) && regno >= FIRST_PSEUDO_REGISTER
		       && reg_equiv_memory_loc[regno] != 0)
		return 0;
	      else if (need_stable_sp && push_operand (dest, GET_MODE (dest)))
		return 0;
	    }
	  else if (GET_CODE (pat) == PARALLEL)
	    {
	      int i;
	      for (i = XVECLEN (pat, 0) - 1; i >= 0; i--)
		{
		  rtx v1 = XVECEXP (pat, 0, i);
		  if (GET_CODE (v1) == COND_EXEC)
		    v1 = COND_EXEC_CODE (v1);
		  if (GET_CODE (v1) == SET || GET_CODE (v1) == CLOBBER)
		    {
		      rtx dest = SET_DEST (v1);
		      while (GET_CODE (dest) == SUBREG
			     || GET_CODE (dest) == ZERO_EXTRACT
			     || GET_CODE (dest) == STRICT_LOW_PART)
			dest = XEXP (dest, 0);
		      if (REG_P (dest))
			{
			  int xregno = REGNO (dest);
			  int xnregs;
			  if (REGNO (dest) < FIRST_PSEUDO_REGISTER)
			    xnregs = hard_regno_nregs[xregno][GET_MODE (dest)];
			  else
			    xnregs = 1;
			  if (xregno < regno + nregs
			      && xregno + xnregs > regno)
			    return 0;
			  if (xregno < valueno + valuenregs
			      && xregno + xnregs > valueno)
			    return 0;
			  if (goal_mem_addr_varies
			      && reg_overlap_mentioned_for_reload_p (dest,
								     goal))
			    return 0;
			  if (xregno == STACK_POINTER_REGNUM && need_stable_sp)
			    return 0;
			}
		      else if (goal_mem && MEM_P (dest)
			       && ! push_operand (dest, GET_MODE (dest)))
			return 0;
		      else if (MEM_P (dest) && regno >= FIRST_PSEUDO_REGISTER
			       && reg_equiv_memory_loc[regno] != 0)
			return 0;
		      else if (need_stable_sp
			       && push_operand (dest, GET_MODE (dest)))
			return 0;
		    }
		}
	    }

	  if (CALL_P (p) && CALL_INSN_FUNCTION_USAGE (p))
	    {
	      rtx link;

	      for (link = CALL_INSN_FUNCTION_USAGE (p); XEXP (link, 1) != 0;
		   link = XEXP (link, 1))
		{
		  pat = XEXP (link, 0);
		  if (GET_CODE (pat) == CLOBBER)
		    {
		      rtx dest = SET_DEST (pat);

		      if (REG_P (dest))
			{
			  int xregno = REGNO (dest);
			  int xnregs
			    = hard_regno_nregs[xregno][GET_MODE (dest)];

			  if (xregno < regno + nregs
			      && xregno + xnregs > regno)
			    return 0;
			  else if (xregno < valueno + valuenregs
				   && xregno + xnregs > valueno)
			    return 0;
			  else if (goal_mem_addr_varies
				   && reg_overlap_mentioned_for_reload_p (dest,
								     goal))
			    return 0;
			}

		      else if (goal_mem && MEM_P (dest)
			       && ! push_operand (dest, GET_MODE (dest)))
			return 0;
		      else if (need_stable_sp
			       && push_operand (dest, GET_MODE (dest)))
			return 0;
		    }
		}
	    }

#ifdef AUTO_INC_DEC
	  /* If this insn auto-increments or auto-decrements
	     either regno or valueno, return 0 now.
	     If GOAL is a memory ref and its address is not constant,
	     and this insn P increments a register used in GOAL, return 0.  */
	  {
	    rtx link;

	    for (link = REG_NOTES (p); link; link = XEXP (link, 1))
	      if (REG_NOTE_KIND (link) == REG_INC
		  && REG_P (XEXP (link, 0)))
		{
		  int incno = REGNO (XEXP (link, 0));
		  if (incno < regno + nregs && incno >= regno)
		    return 0;
		  if (incno < valueno + valuenregs && incno >= valueno)
		    return 0;
		  if (goal_mem_addr_varies
		      && reg_overlap_mentioned_for_reload_p (XEXP (link, 0),
							     goal))
		    return 0;
		}
	  }
#endif
	}
    }
}

/* Find a place where INCED appears in an increment or decrement operator
   within X, and return the amount INCED is incremented or decremented by.
   The value is always positive.  */

static int
find_inc_amount (rtx x, rtx inced)
{
  enum rtx_code code = GET_CODE (x);
  const char *fmt;
  int i;

  if (code == MEM)
    {
      rtx addr = XEXP (x, 0);
      if ((GET_CODE (addr) == PRE_DEC
	   || GET_CODE (addr) == POST_DEC
	   || GET_CODE (addr) == PRE_INC
	   || GET_CODE (addr) == POST_INC)
	  && XEXP (addr, 0) == inced)
	return GET_MODE_SIZE (GET_MODE (x));
      else if ((GET_CODE (addr) == PRE_MODIFY
		|| GET_CODE (addr) == POST_MODIFY)
	       && GET_CODE (XEXP (addr, 1)) == PLUS
	       && XEXP (addr, 0) == XEXP (XEXP (addr, 1), 0)
	       && XEXP (addr, 0) == inced
	       && GET_CODE (XEXP (XEXP (addr, 1), 1)) == CONST_INT)
	{
	  i = INTVAL (XEXP (XEXP (addr, 1), 1));
	  return i < 0 ? -i : i;
	}
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	{
	  int tem = find_inc_amount (XEXP (x, i), inced);
	  if (tem != 0)
	    return tem;
	}
      if (fmt[i] == 'E')
	{
	  int j;
	  for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	    {
	      int tem = find_inc_amount (XVECEXP (x, i, j), inced);
	      if (tem != 0)
		return tem;
	    }
	}
    }

  return 0;
}

/* Return 1 if registers from REGNO to ENDREGNO are the subjects of a
   REG_INC note in insn INSN.  REGNO must refer to a hard register.  */

#ifdef AUTO_INC_DEC
static int 
reg_inc_found_and_valid_p (unsigned int regno, unsigned int endregno,
			   rtx insn)
{
  rtx link;

  gcc_assert (insn);

  if (! INSN_P (insn))
    return 0;
    
  for (link = REG_NOTES (insn); link; link = XEXP (link, 1))
    if (REG_NOTE_KIND (link) == REG_INC)
      {
	unsigned int test = (int) REGNO (XEXP (link, 0));
	if (test >= regno && test < endregno)
	  return 1; 
      }
  return 0;
}
#else

#define reg_inc_found_and_valid_p(regno,endregno,insn) 0

#endif 

/* Return 1 if register REGNO is the subject of a clobber in insn INSN.
   If SETS is 1, also consider SETs.  If SETS is 2, enable checking
   REG_INC.  REGNO must refer to a hard register.  */

int
regno_clobbered_p (unsigned int regno, rtx insn, enum machine_mode mode,
		   int sets)
{
  unsigned int nregs, endregno;

  /* regno must be a hard register.  */
  gcc_assert (regno < FIRST_PSEUDO_REGISTER);

  nregs = hard_regno_nregs[regno][mode];
  endregno = regno + nregs;

  if ((GET_CODE (PATTERN (insn)) == CLOBBER
       || (sets == 1 && GET_CODE (PATTERN (insn)) == SET))
      && REG_P (XEXP (PATTERN (insn), 0)))
    {
      unsigned int test = REGNO (XEXP (PATTERN (insn), 0));

      return test >= regno && test < endregno;
    }

  if (sets == 2 && reg_inc_found_and_valid_p (regno, endregno, insn))
    return 1; 
  
  if (GET_CODE (PATTERN (insn)) == PARALLEL)
    {
      int i = XVECLEN (PATTERN (insn), 0) - 1;

      for (; i >= 0; i--)
	{
	  rtx elt = XVECEXP (PATTERN (insn), 0, i);
	  if ((GET_CODE (elt) == CLOBBER
	       || (sets == 1 && GET_CODE (PATTERN (insn)) == SET))
	      && REG_P (XEXP (elt, 0)))
	    {
	      unsigned int test = REGNO (XEXP (elt, 0));

	      if (test >= regno && test < endregno)
		return 1;
	    }
	  if (sets == 2
	      && reg_inc_found_and_valid_p (regno, endregno, elt))
	    return 1; 
	}
    }

  return 0;
}

/* Find the low part, with mode MODE, of a hard regno RELOADREG.  */
rtx
reload_adjust_reg_for_mode (rtx reloadreg, enum machine_mode mode)
{
  int regno;

  if (GET_MODE (reloadreg) == mode)
    return reloadreg;

  regno = REGNO (reloadreg);

  if (WORDS_BIG_ENDIAN)
    regno += (int) hard_regno_nregs[regno][GET_MODE (reloadreg)]
      - (int) hard_regno_nregs[regno][mode];

  return gen_rtx_REG (mode, regno);
}

static const char *const reload_when_needed_name[] =
{
  "RELOAD_FOR_INPUT",
  "RELOAD_FOR_OUTPUT",
  "RELOAD_FOR_INSN",
  "RELOAD_FOR_INPUT_ADDRESS",
  "RELOAD_FOR_INPADDR_ADDRESS",
  "RELOAD_FOR_OUTPUT_ADDRESS",
  "RELOAD_FOR_OUTADDR_ADDRESS",
  "RELOAD_FOR_OPERAND_ADDRESS",
  "RELOAD_FOR_OPADDR_ADDR",
  "RELOAD_OTHER",
  "RELOAD_FOR_OTHER_ADDRESS"
};

/* These functions are used to print the variables set by 'find_reloads' */

void
debug_reload_to_stream (FILE *f)
{
  int r;
  const char *prefix;

  if (! f)
    f = stderr;
  for (r = 0; r < n_reloads; r++)
    {
      fprintf (f, "Reload %d: ", r);

      if (rld[r].in != 0)
	{
	  fprintf (f, "reload_in (%s) = ",
		   GET_MODE_NAME (rld[r].inmode));
	  print_inline_rtx (f, rld[r].in, 24);
	  fprintf (f, "\n\t");
	}

      if (rld[r].out != 0)
	{
	  fprintf (f, "reload_out (%s) = ",
		   GET_MODE_NAME (rld[r].outmode));
	  print_inline_rtx (f, rld[r].out, 24);
	  fprintf (f, "\n\t");
	}

      fprintf (f, "%s, ", reg_class_names[(int) rld[r].class]);

      fprintf (f, "%s (opnum = %d)",
	       reload_when_needed_name[(int) rld[r].when_needed],
	       rld[r].opnum);

      if (rld[r].optional)
	fprintf (f, ", optional");

      if (rld[r].nongroup)
	fprintf (f, ", nongroup");

      if (rld[r].inc != 0)
	fprintf (f, ", inc by %d", rld[r].inc);

      if (rld[r].nocombine)
	fprintf (f, ", can't combine");

      if (rld[r].secondary_p)
	fprintf (f, ", secondary_reload_p");

      if (rld[r].in_reg != 0)
	{
	  fprintf (f, "\n\treload_in_reg: ");
	  print_inline_rtx (f, rld[r].in_reg, 24);
	}

      if (rld[r].out_reg != 0)
	{
	  fprintf (f, "\n\treload_out_reg: ");
	  print_inline_rtx (f, rld[r].out_reg, 24);
	}

      if (rld[r].reg_rtx != 0)
	{
	  fprintf (f, "\n\treload_reg_rtx: ");
	  print_inline_rtx (f, rld[r].reg_rtx, 24);
	}

      prefix = "\n\t";
      if (rld[r].secondary_in_reload != -1)
	{
	  fprintf (f, "%ssecondary_in_reload = %d",
		   prefix, rld[r].secondary_in_reload);
	  prefix = ", ";
	}

      if (rld[r].secondary_out_reload != -1)
	fprintf (f, "%ssecondary_out_reload = %d\n",
		 prefix, rld[r].secondary_out_reload);

      prefix = "\n\t";
      if (rld[r].secondary_in_icode != CODE_FOR_nothing)
	{
	  fprintf (f, "%ssecondary_in_icode = %s", prefix,
		   insn_data[rld[r].secondary_in_icode].name);
	  prefix = ", ";
	}

      if (rld[r].secondary_out_icode != CODE_FOR_nothing)
	fprintf (f, "%ssecondary_out_icode = %s", prefix,
		 insn_data[rld[r].secondary_out_icode].name);

      fprintf (f, "\n");
    }
}

void
debug_reload (void)
{
  debug_reload_to_stream (stderr);
}
