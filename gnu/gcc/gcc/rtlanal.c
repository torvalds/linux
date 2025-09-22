/* Analyze RTL for GNU compiler.
   Copyright (C) 1987, 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006 Free Software
   Foundation, Inc.

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
#include "hard-reg-set.h"
#include "insn-config.h"
#include "recog.h"
#include "target.h"
#include "output.h"
#include "tm_p.h"
#include "flags.h"
#include "real.h"
#include "regs.h"
#include "function.h"

/* Forward declarations */
static void set_of_1 (rtx, rtx, void *);
static bool covers_regno_p (rtx, unsigned int);
static bool covers_regno_no_parallel_p (rtx, unsigned int);
static int rtx_referenced_p_1 (rtx *, void *);
static int computed_jump_p_1 (rtx);
static void parms_set (rtx, rtx, void *);

static unsigned HOST_WIDE_INT cached_nonzero_bits (rtx, enum machine_mode,
                                                   rtx, enum machine_mode,
                                                   unsigned HOST_WIDE_INT);
static unsigned HOST_WIDE_INT nonzero_bits1 (rtx, enum machine_mode, rtx,
                                             enum machine_mode,
                                             unsigned HOST_WIDE_INT);
static unsigned int cached_num_sign_bit_copies (rtx, enum machine_mode, rtx,
                                                enum machine_mode,
                                                unsigned int);
static unsigned int num_sign_bit_copies1 (rtx, enum machine_mode, rtx,
                                          enum machine_mode, unsigned int);

/* Offset of the first 'e', 'E' or 'V' operand for each rtx code, or
   -1 if a code has no such operand.  */
static int non_rtx_starting_operands[NUM_RTX_CODE];

/* Bit flags that specify the machine subtype we are compiling for.
   Bits are tested using macros TARGET_... defined in the tm.h file
   and set by `-m...' switches.  Must be defined in rtlanal.c.  */

int target_flags;

/* Truncation narrows the mode from SOURCE mode to DESTINATION mode.
   If TARGET_MODE_REP_EXTENDED (DESTINATION, DESTINATION_REP) is
   SIGN_EXTEND then while narrowing we also have to enforce the
   representation and sign-extend the value to mode DESTINATION_REP.

   If the value is already sign-extended to DESTINATION_REP mode we
   can just switch to DESTINATION mode on it.  For each pair of
   integral modes SOURCE and DESTINATION, when truncating from SOURCE
   to DESTINATION, NUM_SIGN_BIT_COPIES_IN_REP[SOURCE][DESTINATION]
   contains the number of high-order bits in SOURCE that have to be
   copies of the sign-bit so that we can do this mode-switch to
   DESTINATION.  */

static unsigned int
num_sign_bit_copies_in_rep[MAX_MODE_INT + 1][MAX_MODE_INT + 1];

/* Return 1 if the value of X is unstable
   (would be different at a different point in the program).
   The frame pointer, arg pointer, etc. are considered stable
   (within one function) and so is anything marked `unchanging'.  */

int
rtx_unstable_p (rtx x)
{
  RTX_CODE code = GET_CODE (x);
  int i;
  const char *fmt;

  switch (code)
    {
    case MEM:
      return !MEM_READONLY_P (x) || rtx_unstable_p (XEXP (x, 0));

    case CONST:
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case SYMBOL_REF:
    case LABEL_REF:
      return 0;

    case REG:
      /* As in rtx_varies_p, we have to use the actual rtx, not reg number.  */
      if (x == frame_pointer_rtx || x == hard_frame_pointer_rtx
	  /* The arg pointer varies if it is not a fixed register.  */
	  || (x == arg_pointer_rtx && fixed_regs[ARG_POINTER_REGNUM]))
	return 0;
#ifndef PIC_OFFSET_TABLE_REG_CALL_CLOBBERED
      /* ??? When call-clobbered, the value is stable modulo the restore
	 that must happen after a call.  This currently screws up local-alloc
	 into believing that the restore is not needed.  */
      if (x == pic_offset_table_rtx)
	return 0;
#endif
      return 1;

    case ASM_OPERANDS:
      if (MEM_VOLATILE_P (x))
	return 1;

      /* Fall through.  */

    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    if (fmt[i] == 'e')
      {
	if (rtx_unstable_p (XEXP (x, i)))
	  return 1;
      }
    else if (fmt[i] == 'E')
      {
	int j;
	for (j = 0; j < XVECLEN (x, i); j++)
	  if (rtx_unstable_p (XVECEXP (x, i, j)))
	    return 1;
      }

  return 0;
}

/* Return 1 if X has a value that can vary even between two
   executions of the program.  0 means X can be compared reliably
   against certain constants or near-constants.
   FOR_ALIAS is nonzero if we are called from alias analysis; if it is
   zero, we are slightly more conservative.
   The frame pointer and the arg pointer are considered constant.  */

int
rtx_varies_p (rtx x, int for_alias)
{
  RTX_CODE code;
  int i;
  const char *fmt;

  if (!x)
    return 0;

  code = GET_CODE (x);
  switch (code)
    {
    case MEM:
      return !MEM_READONLY_P (x) || rtx_varies_p (XEXP (x, 0), for_alias);

    case CONST:
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case SYMBOL_REF:
    case LABEL_REF:
      return 0;

    case REG:
      /* Note that we have to test for the actual rtx used for the frame
	 and arg pointers and not just the register number in case we have
	 eliminated the frame and/or arg pointer and are using it
	 for pseudos.  */
      if (x == frame_pointer_rtx || x == hard_frame_pointer_rtx
	  /* The arg pointer varies if it is not a fixed register.  */
	  || (x == arg_pointer_rtx && fixed_regs[ARG_POINTER_REGNUM]))
	return 0;
      if (x == pic_offset_table_rtx
#ifdef PIC_OFFSET_TABLE_REG_CALL_CLOBBERED
	  /* ??? When call-clobbered, the value is stable modulo the restore
	     that must happen after a call.  This currently screws up
	     local-alloc into believing that the restore is not needed, so we
	     must return 0 only if we are called from alias analysis.  */
	  && for_alias
#endif
	  )
	return 0;
      return 1;

    case LO_SUM:
      /* The operand 0 of a LO_SUM is considered constant
	 (in fact it is related specifically to operand 1)
	 during alias analysis.  */
      return (! for_alias && rtx_varies_p (XEXP (x, 0), for_alias))
	     || rtx_varies_p (XEXP (x, 1), for_alias);

    case ASM_OPERANDS:
      if (MEM_VOLATILE_P (x))
	return 1;

      /* Fall through.  */

    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    if (fmt[i] == 'e')
      {
	if (rtx_varies_p (XEXP (x, i), for_alias))
	  return 1;
      }
    else if (fmt[i] == 'E')
      {
	int j;
	for (j = 0; j < XVECLEN (x, i); j++)
	  if (rtx_varies_p (XVECEXP (x, i, j), for_alias))
	    return 1;
      }

  return 0;
}

/* Return nonzero if the use of X as an address in a MEM can cause a trap.
   MODE is the mode of the MEM (not that of X) and UNALIGNED_MEMS controls
   whether nonzero is returned for unaligned memory accesses on strict
   alignment machines.  */

static int
rtx_addr_can_trap_p_1 (rtx x, enum machine_mode mode, bool unaligned_mems)
{
  enum rtx_code code = GET_CODE (x);

  switch (code)
    {
    case SYMBOL_REF:
      return SYMBOL_REF_WEAK (x);

    case LABEL_REF:
      return 0;

    case REG:
      /* As in rtx_varies_p, we have to use the actual rtx, not reg number.  */
      if (x == frame_pointer_rtx || x == hard_frame_pointer_rtx
	  || x == stack_pointer_rtx
	  /* The arg pointer varies if it is not a fixed register.  */
	  || (x == arg_pointer_rtx && fixed_regs[ARG_POINTER_REGNUM]))
	return 0;
      /* All of the virtual frame registers are stack references.  */
      if (REGNO (x) >= FIRST_VIRTUAL_REGISTER
	  && REGNO (x) <= LAST_VIRTUAL_REGISTER)
	return 0;
      return 1;

    case CONST:
      return rtx_addr_can_trap_p_1 (XEXP (x, 0), mode, unaligned_mems);

    case PLUS:
      /* An address is assumed not to trap if:
	 - it is an address that can't trap plus a constant integer,
	   with the proper remainder modulo the mode size if we are
	   considering unaligned memory references.  */
      if (!rtx_addr_can_trap_p_1 (XEXP (x, 0), mode, unaligned_mems)
	  && GET_CODE (XEXP (x, 1)) == CONST_INT)
	{
	  HOST_WIDE_INT offset;

	  if (!STRICT_ALIGNMENT
	      || !unaligned_mems
	      || GET_MODE_SIZE (mode) == 0)
	    return 0;

	  offset = INTVAL (XEXP (x, 1));

#ifdef SPARC_STACK_BOUNDARY_HACK
	  /* ??? The SPARC port may claim a STACK_BOUNDARY higher than
	     the real alignment of %sp.  However, when it does this, the
	     alignment of %sp+STACK_POINTER_OFFSET is STACK_BOUNDARY.  */
	  if (SPARC_STACK_BOUNDARY_HACK
	      && (XEXP (x, 0) == stack_pointer_rtx
		  || XEXP (x, 0) == hard_frame_pointer_rtx))
	    offset -= STACK_POINTER_OFFSET;
#endif

	  return offset % GET_MODE_SIZE (mode) != 0;
	}

      /* - or it is the pic register plus a constant.  */
      if (XEXP (x, 0) == pic_offset_table_rtx && CONSTANT_P (XEXP (x, 1)))
	return 0;

      return 1;

    case LO_SUM:
    case PRE_MODIFY:
      return rtx_addr_can_trap_p_1 (XEXP (x, 1), mode, unaligned_mems);

    case PRE_DEC:
    case PRE_INC:
    case POST_DEC:
    case POST_INC:
    case POST_MODIFY:
      return rtx_addr_can_trap_p_1 (XEXP (x, 0), mode, unaligned_mems);

    default:
      break;
    }

  /* If it isn't one of the case above, it can cause a trap.  */
  return 1;
}

/* Return nonzero if the use of X as an address in a MEM can cause a trap.  */

int
rtx_addr_can_trap_p (rtx x)
{
  return rtx_addr_can_trap_p_1 (x, VOIDmode, false);
}

/* Return true if X is an address that is known to not be zero.  */

bool
nonzero_address_p (rtx x)
{
  enum rtx_code code = GET_CODE (x);

  switch (code)
    {
    case SYMBOL_REF:
      return !SYMBOL_REF_WEAK (x);

    case LABEL_REF:
      return true;

    case REG:
      /* As in rtx_varies_p, we have to use the actual rtx, not reg number.  */
      if (x == frame_pointer_rtx || x == hard_frame_pointer_rtx
	  || x == stack_pointer_rtx
	  || (x == arg_pointer_rtx && fixed_regs[ARG_POINTER_REGNUM]))
	return true;
      /* All of the virtual frame registers are stack references.  */
      if (REGNO (x) >= FIRST_VIRTUAL_REGISTER
	  && REGNO (x) <= LAST_VIRTUAL_REGISTER)
	return true;
      return false;

    case CONST:
      return nonzero_address_p (XEXP (x, 0));

    case PLUS:
      if (GET_CODE (XEXP (x, 1)) == CONST_INT)
        return nonzero_address_p (XEXP (x, 0));
      /* Handle PIC references.  */
      else if (XEXP (x, 0) == pic_offset_table_rtx
	       && CONSTANT_P (XEXP (x, 1)))
	return true;
      return false;

    case PRE_MODIFY:
      /* Similar to the above; allow positive offsets.  Further, since
	 auto-inc is only allowed in memories, the register must be a
	 pointer.  */
      if (GET_CODE (XEXP (x, 1)) == CONST_INT
	  && INTVAL (XEXP (x, 1)) > 0)
	return true;
      return nonzero_address_p (XEXP (x, 0));

    case PRE_INC:
      /* Similarly.  Further, the offset is always positive.  */
      return true;

    case PRE_DEC:
    case POST_DEC:
    case POST_INC:
    case POST_MODIFY:
      return nonzero_address_p (XEXP (x, 0));

    case LO_SUM:
      return nonzero_address_p (XEXP (x, 1));

    default:
      break;
    }

  /* If it isn't one of the case above, might be zero.  */
  return false;
}

/* Return 1 if X refers to a memory location whose address
   cannot be compared reliably with constant addresses,
   or if X refers to a BLKmode memory object.
   FOR_ALIAS is nonzero if we are called from alias analysis; if it is
   zero, we are slightly more conservative.  */

int
rtx_addr_varies_p (rtx x, int for_alias)
{
  enum rtx_code code;
  int i;
  const char *fmt;

  if (x == 0)
    return 0;

  code = GET_CODE (x);
  if (code == MEM)
    return GET_MODE (x) == BLKmode || rtx_varies_p (XEXP (x, 0), for_alias);

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    if (fmt[i] == 'e')
      {
	if (rtx_addr_varies_p (XEXP (x, i), for_alias))
	  return 1;
      }
    else if (fmt[i] == 'E')
      {
	int j;
	for (j = 0; j < XVECLEN (x, i); j++)
	  if (rtx_addr_varies_p (XVECEXP (x, i, j), for_alias))
	    return 1;
      }
  return 0;
}

/* Return the value of the integer term in X, if one is apparent;
   otherwise return 0.
   Only obvious integer terms are detected.
   This is used in cse.c with the `related_value' field.  */

HOST_WIDE_INT
get_integer_term (rtx x)
{
  if (GET_CODE (x) == CONST)
    x = XEXP (x, 0);

  if (GET_CODE (x) == MINUS
      && GET_CODE (XEXP (x, 1)) == CONST_INT)
    return - INTVAL (XEXP (x, 1));
  if (GET_CODE (x) == PLUS
      && GET_CODE (XEXP (x, 1)) == CONST_INT)
    return INTVAL (XEXP (x, 1));
  return 0;
}

/* If X is a constant, return the value sans apparent integer term;
   otherwise return 0.
   Only obvious integer terms are detected.  */

rtx
get_related_value (rtx x)
{
  if (GET_CODE (x) != CONST)
    return 0;
  x = XEXP (x, 0);
  if (GET_CODE (x) == PLUS
      && GET_CODE (XEXP (x, 1)) == CONST_INT)
    return XEXP (x, 0);
  else if (GET_CODE (x) == MINUS
	   && GET_CODE (XEXP (x, 1)) == CONST_INT)
    return XEXP (x, 0);
  return 0;
}

/* Return the number of places FIND appears within X.  If COUNT_DEST is
   zero, we do not count occurrences inside the destination of a SET.  */

int
count_occurrences (rtx x, rtx find, int count_dest)
{
  int i, j;
  enum rtx_code code;
  const char *format_ptr;
  int count;

  if (x == find)
    return 1;

  code = GET_CODE (x);

  switch (code)
    {
    case REG:
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case SYMBOL_REF:
    case CODE_LABEL:
    case PC:
    case CC0:
      return 0;

    case MEM:
      if (MEM_P (find) && rtx_equal_p (x, find))
	return 1;
      break;

    case SET:
      if (SET_DEST (x) == find && ! count_dest)
	return count_occurrences (SET_SRC (x), find, count_dest);
      break;

    default:
      break;
    }

  format_ptr = GET_RTX_FORMAT (code);
  count = 0;

  for (i = 0; i < GET_RTX_LENGTH (code); i++)
    {
      switch (*format_ptr++)
	{
	case 'e':
	  count += count_occurrences (XEXP (x, i), find, count_dest);
	  break;

	case 'E':
	  for (j = 0; j < XVECLEN (x, i); j++)
	    count += count_occurrences (XVECEXP (x, i, j), find, count_dest);
	  break;
	}
    }
  return count;
}

/* Nonzero if register REG appears somewhere within IN.
   Also works if REG is not a register; in this case it checks
   for a subexpression of IN that is Lisp "equal" to REG.  */

int
reg_mentioned_p (rtx reg, rtx in)
{
  const char *fmt;
  int i;
  enum rtx_code code;

  if (in == 0)
    return 0;

  if (reg == in)
    return 1;

  if (GET_CODE (in) == LABEL_REF)
    return reg == XEXP (in, 0);

  code = GET_CODE (in);

  switch (code)
    {
      /* Compare registers by number.  */
    case REG:
      return REG_P (reg) && REGNO (in) == REGNO (reg);

      /* These codes have no constituent expressions
	 and are unique.  */
    case SCRATCH:
    case CC0:
    case PC:
      return 0;

    case CONST_INT:
    case CONST_VECTOR:
    case CONST_DOUBLE:
      /* These are kept unique for a given value.  */
      return 0;

    default:
      break;
    }

  if (GET_CODE (reg) == code && rtx_equal_p (reg, in))
    return 1;

  fmt = GET_RTX_FORMAT (code);

  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'E')
	{
	  int j;
	  for (j = XVECLEN (in, i) - 1; j >= 0; j--)
	    if (reg_mentioned_p (reg, XVECEXP (in, i, j)))
	      return 1;
	}
      else if (fmt[i] == 'e'
	       && reg_mentioned_p (reg, XEXP (in, i)))
	return 1;
    }
  return 0;
}

/* Return 1 if in between BEG and END, exclusive of BEG and END, there is
   no CODE_LABEL insn.  */

int
no_labels_between_p (rtx beg, rtx end)
{
  rtx p;
  if (beg == end)
    return 0;
  for (p = NEXT_INSN (beg); p != end; p = NEXT_INSN (p))
    if (LABEL_P (p))
      return 0;
  return 1;
}

/* Nonzero if register REG is used in an insn between
   FROM_INSN and TO_INSN (exclusive of those two).  */

int
reg_used_between_p (rtx reg, rtx from_insn, rtx to_insn)
{
  rtx insn;

  if (from_insn == to_insn)
    return 0;

  for (insn = NEXT_INSN (from_insn); insn != to_insn; insn = NEXT_INSN (insn))
    if (INSN_P (insn)
	&& (reg_overlap_mentioned_p (reg, PATTERN (insn))
	   || (CALL_P (insn) && find_reg_fusage (insn, USE, reg))))
      return 1;
  return 0;
}

/* Nonzero if the old value of X, a register, is referenced in BODY.  If X
   is entirely replaced by a new value and the only use is as a SET_DEST,
   we do not consider it a reference.  */

int
reg_referenced_p (rtx x, rtx body)
{
  int i;

  switch (GET_CODE (body))
    {
    case SET:
      if (reg_overlap_mentioned_p (x, SET_SRC (body)))
	return 1;

      /* If the destination is anything other than CC0, PC, a REG or a SUBREG
	 of a REG that occupies all of the REG, the insn references X if
	 it is mentioned in the destination.  */
      if (GET_CODE (SET_DEST (body)) != CC0
	  && GET_CODE (SET_DEST (body)) != PC
	  && !REG_P (SET_DEST (body))
	  && ! (GET_CODE (SET_DEST (body)) == SUBREG
		&& REG_P (SUBREG_REG (SET_DEST (body)))
		&& (((GET_MODE_SIZE (GET_MODE (SUBREG_REG (SET_DEST (body))))
		      + (UNITS_PER_WORD - 1)) / UNITS_PER_WORD)
		    == ((GET_MODE_SIZE (GET_MODE (SET_DEST (body)))
			 + (UNITS_PER_WORD - 1)) / UNITS_PER_WORD)))
	  && reg_overlap_mentioned_p (x, SET_DEST (body)))
	return 1;
      return 0;

    case ASM_OPERANDS:
      for (i = ASM_OPERANDS_INPUT_LENGTH (body) - 1; i >= 0; i--)
	if (reg_overlap_mentioned_p (x, ASM_OPERANDS_INPUT (body, i)))
	  return 1;
      return 0;

    case CALL:
    case USE:
    case IF_THEN_ELSE:
      return reg_overlap_mentioned_p (x, body);

    case TRAP_IF:
      return reg_overlap_mentioned_p (x, TRAP_CONDITION (body));

    case PREFETCH:
      return reg_overlap_mentioned_p (x, XEXP (body, 0));

    case UNSPEC:
    case UNSPEC_VOLATILE:
      for (i = XVECLEN (body, 0) - 1; i >= 0; i--)
	if (reg_overlap_mentioned_p (x, XVECEXP (body, 0, i)))
	  return 1;
      return 0;

    case PARALLEL:
      for (i = XVECLEN (body, 0) - 1; i >= 0; i--)
	if (reg_referenced_p (x, XVECEXP (body, 0, i)))
	  return 1;
      return 0;

    case CLOBBER:
      if (MEM_P (XEXP (body, 0)))
	if (reg_overlap_mentioned_p (x, XEXP (XEXP (body, 0), 0)))
	  return 1;
      return 0;

    case COND_EXEC:
      if (reg_overlap_mentioned_p (x, COND_EXEC_TEST (body)))
	return 1;
      return reg_referenced_p (x, COND_EXEC_CODE (body));

    default:
      return 0;
    }
}

/* Nonzero if register REG is set or clobbered in an insn between
   FROM_INSN and TO_INSN (exclusive of those two).  */

int
reg_set_between_p (rtx reg, rtx from_insn, rtx to_insn)
{
  rtx insn;

  if (from_insn == to_insn)
    return 0;

  for (insn = NEXT_INSN (from_insn); insn != to_insn; insn = NEXT_INSN (insn))
    if (INSN_P (insn) && reg_set_p (reg, insn))
      return 1;
  return 0;
}

/* Internals of reg_set_between_p.  */
int
reg_set_p (rtx reg, rtx insn)
{
  /* We can be passed an insn or part of one.  If we are passed an insn,
     check if a side-effect of the insn clobbers REG.  */
  if (INSN_P (insn)
      && (FIND_REG_INC_NOTE (insn, reg)
	  || (CALL_P (insn)
	      && ((REG_P (reg)
		   && REGNO (reg) < FIRST_PSEUDO_REGISTER
		   && TEST_HARD_REG_BIT (regs_invalidated_by_call,
					 REGNO (reg)))
		  || MEM_P (reg)
		  || find_reg_fusage (insn, CLOBBER, reg)))))
    return 1;

  return set_of (reg, insn) != NULL_RTX;
}

/* Similar to reg_set_between_p, but check all registers in X.  Return 0
   only if none of them are modified between START and END.  Return 1 if
   X contains a MEM; this routine does usememory aliasing.  */

int
modified_between_p (rtx x, rtx start, rtx end)
{
  enum rtx_code code = GET_CODE (x);
  const char *fmt;
  int i, j;
  rtx insn;

  if (start == end)
    return 0;

  switch (code)
    {
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case CONST:
    case SYMBOL_REF:
    case LABEL_REF:
      return 0;

    case PC:
    case CC0:
      return 1;

    case MEM:
      if (modified_between_p (XEXP (x, 0), start, end))
	return 1;
      if (MEM_READONLY_P (x))
	return 0;
      for (insn = NEXT_INSN (start); insn != end; insn = NEXT_INSN (insn))
	if (memory_modified_in_insn_p (x, insn))
	  return 1;
      return 0;
      break;

    case REG:
      return reg_set_between_p (x, start, end);

    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e' && modified_between_p (XEXP (x, i), start, end))
	return 1;

      else if (fmt[i] == 'E')
	for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	  if (modified_between_p (XVECEXP (x, i, j), start, end))
	    return 1;
    }

  return 0;
}

/* Similar to reg_set_p, but check all registers in X.  Return 0 only if none
   of them are modified in INSN.  Return 1 if X contains a MEM; this routine
   does use memory aliasing.  */

int
modified_in_p (rtx x, rtx insn)
{
  enum rtx_code code = GET_CODE (x);
  const char *fmt;
  int i, j;

  switch (code)
    {
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case CONST:
    case SYMBOL_REF:
    case LABEL_REF:
      return 0;

    case PC:
    case CC0:
      return 1;

    case MEM:
      if (modified_in_p (XEXP (x, 0), insn))
	return 1;
      if (MEM_READONLY_P (x))
	return 0;
      if (memory_modified_in_insn_p (x, insn))
	return 1;
      return 0;
      break;

    case REG:
      return reg_set_p (x, insn);

    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e' && modified_in_p (XEXP (x, i), insn))
	return 1;

      else if (fmt[i] == 'E')
	for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	  if (modified_in_p (XVECEXP (x, i, j), insn))
	    return 1;
    }

  return 0;
}

/* Helper function for set_of.  */
struct set_of_data
  {
    rtx found;
    rtx pat;
  };

static void
set_of_1 (rtx x, rtx pat, void *data1)
{
   struct set_of_data *data = (struct set_of_data *) (data1);
   if (rtx_equal_p (x, data->pat)
       || (!MEM_P (x) && reg_overlap_mentioned_p (data->pat, x)))
     data->found = pat;
}

/* Give an INSN, return a SET or CLOBBER expression that does modify PAT
   (either directly or via STRICT_LOW_PART and similar modifiers).  */
rtx
set_of (rtx pat, rtx insn)
{
  struct set_of_data data;
  data.found = NULL_RTX;
  data.pat = pat;
  note_stores (INSN_P (insn) ? PATTERN (insn) : insn, set_of_1, &data);
  return data.found;
}

/* Given an INSN, return a SET expression if this insn has only a single SET.
   It may also have CLOBBERs, USEs, or SET whose output
   will not be used, which we ignore.  */

rtx
single_set_2 (rtx insn, rtx pat)
{
  rtx set = NULL;
  int set_verified = 1;
  int i;

  if (GET_CODE (pat) == PARALLEL)
    {
      for (i = 0; i < XVECLEN (pat, 0); i++)
	{
	  rtx sub = XVECEXP (pat, 0, i);
	  switch (GET_CODE (sub))
	    {
	    case USE:
	    case CLOBBER:
	      break;

	    case SET:
	      /* We can consider insns having multiple sets, where all
		 but one are dead as single set insns.  In common case
		 only single set is present in the pattern so we want
		 to avoid checking for REG_UNUSED notes unless necessary.

		 When we reach set first time, we just expect this is
		 the single set we are looking for and only when more
		 sets are found in the insn, we check them.  */
	      if (!set_verified)
		{
		  if (find_reg_note (insn, REG_UNUSED, SET_DEST (set))
		      && !side_effects_p (set))
		    set = NULL;
		  else
		    set_verified = 1;
		}
	      if (!set)
		set = sub, set_verified = 0;
	      else if (!find_reg_note (insn, REG_UNUSED, SET_DEST (sub))
		       || side_effects_p (sub))
		return NULL_RTX;
	      break;

	    default:
	      return NULL_RTX;
	    }
	}
    }
  return set;
}

/* Given an INSN, return nonzero if it has more than one SET, else return
   zero.  */

int
multiple_sets (rtx insn)
{
  int found;
  int i;

  /* INSN must be an insn.  */
  if (! INSN_P (insn))
    return 0;

  /* Only a PARALLEL can have multiple SETs.  */
  if (GET_CODE (PATTERN (insn)) == PARALLEL)
    {
      for (i = 0, found = 0; i < XVECLEN (PATTERN (insn), 0); i++)
	if (GET_CODE (XVECEXP (PATTERN (insn), 0, i)) == SET)
	  {
	    /* If we have already found a SET, then return now.  */
	    if (found)
	      return 1;
	    else
	      found = 1;
	  }
    }

  /* Either zero or one SET.  */
  return 0;
}

/* Return nonzero if the destination of SET equals the source
   and there are no side effects.  */

int
set_noop_p (rtx set)
{
  rtx src = SET_SRC (set);
  rtx dst = SET_DEST (set);

  if (dst == pc_rtx && src == pc_rtx)
    return 1;

  if (MEM_P (dst) && MEM_P (src))
    return rtx_equal_p (dst, src) && !side_effects_p (dst);

  if (GET_CODE (dst) == ZERO_EXTRACT)
    return rtx_equal_p (XEXP (dst, 0), src)
	   && ! BYTES_BIG_ENDIAN && XEXP (dst, 2) == const0_rtx
	   && !side_effects_p (src);

  if (GET_CODE (dst) == STRICT_LOW_PART)
    dst = XEXP (dst, 0);

  if (GET_CODE (src) == SUBREG && GET_CODE (dst) == SUBREG)
    {
      if (SUBREG_BYTE (src) != SUBREG_BYTE (dst))
	return 0;
      src = SUBREG_REG (src);
      dst = SUBREG_REG (dst);
    }

  return (REG_P (src) && REG_P (dst)
	  && REGNO (src) == REGNO (dst));
}

/* Return nonzero if an insn consists only of SETs, each of which only sets a
   value to itself.  */

int
noop_move_p (rtx insn)
{
  rtx pat = PATTERN (insn);

  if (INSN_CODE (insn) == NOOP_MOVE_INSN_CODE)
    return 1;

  /* Insns carrying these notes are useful later on.  */
  if (find_reg_note (insn, REG_EQUAL, NULL_RTX))
    return 0;

  /* For now treat an insn with a REG_RETVAL note as a
     a special insn which should not be considered a no-op.  */
  if (find_reg_note (insn, REG_RETVAL, NULL_RTX))
    return 0;

  if (GET_CODE (pat) == SET && set_noop_p (pat))
    return 1;

  if (GET_CODE (pat) == PARALLEL)
    {
      int i;
      /* If nothing but SETs of registers to themselves,
	 this insn can also be deleted.  */
      for (i = 0; i < XVECLEN (pat, 0); i++)
	{
	  rtx tem = XVECEXP (pat, 0, i);

	  if (GET_CODE (tem) == USE
	      || GET_CODE (tem) == CLOBBER)
	    continue;

	  if (GET_CODE (tem) != SET || ! set_noop_p (tem))
	    return 0;
	}

      return 1;
    }
  return 0;
}


/* Return the last thing that X was assigned from before *PINSN.  If VALID_TO
   is not NULL_RTX then verify that the object is not modified up to VALID_TO.
   If the object was modified, if we hit a partial assignment to X, or hit a
   CODE_LABEL first, return X.  If we found an assignment, update *PINSN to
   point to it.  ALLOW_HWREG is set to 1 if hardware registers are allowed to
   be the src.  */

rtx
find_last_value (rtx x, rtx *pinsn, rtx valid_to, int allow_hwreg)
{
  rtx p;

  for (p = PREV_INSN (*pinsn); p && !LABEL_P (p);
       p = PREV_INSN (p))
    if (INSN_P (p))
      {
	rtx set = single_set (p);
	rtx note = find_reg_note (p, REG_EQUAL, NULL_RTX);

	if (set && rtx_equal_p (x, SET_DEST (set)))
	  {
	    rtx src = SET_SRC (set);

	    if (note && GET_CODE (XEXP (note, 0)) != EXPR_LIST)
	      src = XEXP (note, 0);

	    if ((valid_to == NULL_RTX
		 || ! modified_between_p (src, PREV_INSN (p), valid_to))
		/* Reject hard registers because we don't usually want
		   to use them; we'd rather use a pseudo.  */
		&& (! (REG_P (src)
		      && REGNO (src) < FIRST_PSEUDO_REGISTER) || allow_hwreg))
	      {
		*pinsn = p;
		return src;
	      }
	  }

	/* If set in non-simple way, we don't have a value.  */
	if (reg_set_p (x, p))
	  break;
      }

  return x;
}

/* Return nonzero if register in range [REGNO, ENDREGNO)
   appears either explicitly or implicitly in X
   other than being stored into.

   References contained within the substructure at LOC do not count.
   LOC may be zero, meaning don't ignore anything.  */

int
refers_to_regno_p (unsigned int regno, unsigned int endregno, rtx x,
		   rtx *loc)
{
  int i;
  unsigned int x_regno;
  RTX_CODE code;
  const char *fmt;

 repeat:
  /* The contents of a REG_NONNEG note is always zero, so we must come here
     upon repeat in case the last REG_NOTE is a REG_NONNEG note.  */
  if (x == 0)
    return 0;

  code = GET_CODE (x);

  switch (code)
    {
    case REG:
      x_regno = REGNO (x);

      /* If we modifying the stack, frame, or argument pointer, it will
	 clobber a virtual register.  In fact, we could be more precise,
	 but it isn't worth it.  */
      if ((x_regno == STACK_POINTER_REGNUM
#if FRAME_POINTER_REGNUM != ARG_POINTER_REGNUM
	   || x_regno == ARG_POINTER_REGNUM
#endif
	   || x_regno == FRAME_POINTER_REGNUM)
	  && regno >= FIRST_VIRTUAL_REGISTER && regno <= LAST_VIRTUAL_REGISTER)
	return 1;

      return (endregno > x_regno
	      && regno < x_regno + (x_regno < FIRST_PSEUDO_REGISTER
				    ? hard_regno_nregs[x_regno][GET_MODE (x)]
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
	       && refers_to_regno_p (regno, endregno,
				     SUBREG_REG (SET_DEST (x)), loc))
	      || (!REG_P (SET_DEST (x))
		  && refers_to_regno_p (regno, endregno, SET_DEST (x), loc))))
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
	    if (refers_to_regno_p (regno, endregno, XEXP (x, i), loc))
	      return 1;
	}
      else if (fmt[i] == 'E')
	{
	  int j;
	  for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	    if (loc != &XVECEXP (x, i, j)
		&& refers_to_regno_p (regno, endregno, XVECEXP (x, i, j), loc))
	      return 1;
	}
    }
  return 0;
}

/* Nonzero if modifying X will affect IN.  If X is a register or a SUBREG,
   we check if any register number in X conflicts with the relevant register
   numbers.  If X is a constant, return 0.  If X is a MEM, return 1 iff IN
   contains a MEM (we don't bother checking for memory addresses that can't
   conflict because we expect this to be a rare case.  */

int
reg_overlap_mentioned_p (rtx x, rtx in)
{
  unsigned int regno, endregno;

  /* If either argument is a constant, then modifying X can not
     affect IN.  Here we look at IN, we can profitably combine
     CONSTANT_P (x) with the switch statement below.  */
  if (CONSTANT_P (in))
    return 0;

 recurse:
  switch (GET_CODE (x))
    {
    case STRICT_LOW_PART:
    case ZERO_EXTRACT:
    case SIGN_EXTRACT:
      /* Overly conservative.  */
      x = XEXP (x, 0);
      goto recurse;

    case SUBREG:
      regno = REGNO (SUBREG_REG (x));
      if (regno < FIRST_PSEUDO_REGISTER)
	regno = subreg_regno (x);
      goto do_reg;

    case REG:
      regno = REGNO (x);
    do_reg:
      endregno = regno + (regno < FIRST_PSEUDO_REGISTER
			  ? hard_regno_nregs[regno][GET_MODE (x)] : 1);
      return refers_to_regno_p (regno, endregno, in, (rtx*) 0);

    case MEM:
      {
	const char *fmt;
	int i;

	if (MEM_P (in))
	  return 1;

	fmt = GET_RTX_FORMAT (GET_CODE (in));
	for (i = GET_RTX_LENGTH (GET_CODE (in)) - 1; i >= 0; i--)
	  if (fmt[i] == 'e')
	    {
	      if (reg_overlap_mentioned_p (x, XEXP (in, i)))
		return 1;
	    }
	  else if (fmt[i] == 'E')
	    {
	      int j;
	      for (j = XVECLEN (in, i) - 1; j >= 0; --j)
		if (reg_overlap_mentioned_p (x, XVECEXP (in, i, j)))
		  return 1;
	    }

	return 0;
      }

    case SCRATCH:
    case PC:
    case CC0:
      return reg_mentioned_p (x, in);

    case PARALLEL:
      {
	int i;

	/* If any register in here refers to it we return true.  */
	for (i = XVECLEN (x, 0) - 1; i >= 0; i--)
	  if (XEXP (XVECEXP (x, 0, i), 0) != 0
	      && reg_overlap_mentioned_p (XEXP (XVECEXP (x, 0, i), 0), in))
	    return 1;
	return 0;
      }

    default:
      gcc_assert (CONSTANT_P (x));
      return 0;
    }
}

/* Call FUN on each register or MEM that is stored into or clobbered by X.
   (X would be the pattern of an insn).
   FUN receives two arguments:
     the REG, MEM, CC0 or PC being stored in or clobbered,
     the SET or CLOBBER rtx that does the store.

  If the item being stored in or clobbered is a SUBREG of a hard register,
  the SUBREG will be passed.  */

void
note_stores (rtx x, void (*fun) (rtx, rtx, void *), void *data)
{
  int i;

  if (GET_CODE (x) == COND_EXEC)
    x = COND_EXEC_CODE (x);

  if (GET_CODE (x) == SET || GET_CODE (x) == CLOBBER)
    {
      rtx dest = SET_DEST (x);

      while ((GET_CODE (dest) == SUBREG
	      && (!REG_P (SUBREG_REG (dest))
		  || REGNO (SUBREG_REG (dest)) >= FIRST_PSEUDO_REGISTER))
	     || GET_CODE (dest) == ZERO_EXTRACT
	     || GET_CODE (dest) == STRICT_LOW_PART)
	dest = XEXP (dest, 0);

      /* If we have a PARALLEL, SET_DEST is a list of EXPR_LIST expressions,
	 each of whose first operand is a register.  */
      if (GET_CODE (dest) == PARALLEL)
	{
	  for (i = XVECLEN (dest, 0) - 1; i >= 0; i--)
	    if (XEXP (XVECEXP (dest, 0, i), 0) != 0)
	      (*fun) (XEXP (XVECEXP (dest, 0, i), 0), x, data);
	}
      else
	(*fun) (dest, x, data);
    }

  else if (GET_CODE (x) == PARALLEL)
    for (i = XVECLEN (x, 0) - 1; i >= 0; i--)
      note_stores (XVECEXP (x, 0, i), fun, data);
}

/* Like notes_stores, but call FUN for each expression that is being
   referenced in PBODY, a pointer to the PATTERN of an insn.  We only call
   FUN for each expression, not any interior subexpressions.  FUN receives a
   pointer to the expression and the DATA passed to this function.

   Note that this is not quite the same test as that done in reg_referenced_p
   since that considers something as being referenced if it is being
   partially set, while we do not.  */

void
note_uses (rtx *pbody, void (*fun) (rtx *, void *), void *data)
{
  rtx body = *pbody;
  int i;

  switch (GET_CODE (body))
    {
    case COND_EXEC:
      (*fun) (&COND_EXEC_TEST (body), data);
      note_uses (&COND_EXEC_CODE (body), fun, data);
      return;

    case PARALLEL:
      for (i = XVECLEN (body, 0) - 1; i >= 0; i--)
	note_uses (&XVECEXP (body, 0, i), fun, data);
      return;

    case USE:
      (*fun) (&XEXP (body, 0), data);
      return;

    case ASM_OPERANDS:
      for (i = ASM_OPERANDS_INPUT_LENGTH (body) - 1; i >= 0; i--)
	(*fun) (&ASM_OPERANDS_INPUT (body, i), data);
      return;

    case TRAP_IF:
      (*fun) (&TRAP_CONDITION (body), data);
      return;

    case PREFETCH:
      (*fun) (&XEXP (body, 0), data);
      return;

    case UNSPEC:
    case UNSPEC_VOLATILE:
      for (i = XVECLEN (body, 0) - 1; i >= 0; i--)
	(*fun) (&XVECEXP (body, 0, i), data);
      return;

    case CLOBBER:
      if (MEM_P (XEXP (body, 0)))
	(*fun) (&XEXP (XEXP (body, 0), 0), data);
      return;

    case SET:
      {
	rtx dest = SET_DEST (body);

	/* For sets we replace everything in source plus registers in memory
	   expression in store and operands of a ZERO_EXTRACT.  */
	(*fun) (&SET_SRC (body), data);

	if (GET_CODE (dest) == ZERO_EXTRACT)
	  {
	    (*fun) (&XEXP (dest, 1), data);
	    (*fun) (&XEXP (dest, 2), data);
	  }

	while (GET_CODE (dest) == SUBREG || GET_CODE (dest) == STRICT_LOW_PART)
	  dest = XEXP (dest, 0);

	if (MEM_P (dest))
	  (*fun) (&XEXP (dest, 0), data);
      }
      return;

    default:
      /* All the other possibilities never store.  */
      (*fun) (pbody, data);
      return;
    }
}

/* Return nonzero if X's old contents don't survive after INSN.
   This will be true if X is (cc0) or if X is a register and
   X dies in INSN or because INSN entirely sets X.

   "Entirely set" means set directly and not through a SUBREG, or
   ZERO_EXTRACT, so no trace of the old contents remains.
   Likewise, REG_INC does not count.

   REG may be a hard or pseudo reg.  Renumbering is not taken into account,
   but for this use that makes no difference, since regs don't overlap
   during their lifetimes.  Therefore, this function may be used
   at any time after deaths have been computed (in flow.c).

   If REG is a hard reg that occupies multiple machine registers, this
   function will only return 1 if each of those registers will be replaced
   by INSN.  */

int
dead_or_set_p (rtx insn, rtx x)
{
  unsigned int regno, last_regno;
  unsigned int i;

  /* Can't use cc0_rtx below since this file is used by genattrtab.c.  */
  if (GET_CODE (x) == CC0)
    return 1;

  gcc_assert (REG_P (x));

  regno = REGNO (x);
  last_regno = (regno >= FIRST_PSEUDO_REGISTER ? regno
		: regno + hard_regno_nregs[regno][GET_MODE (x)] - 1);

  for (i = regno; i <= last_regno; i++)
    if (! dead_or_set_regno_p (insn, i))
      return 0;

  return 1;
}

/* Return TRUE iff DEST is a register or subreg of a register and
   doesn't change the number of words of the inner register, and any
   part of the register is TEST_REGNO.  */

static bool
covers_regno_no_parallel_p (rtx dest, unsigned int test_regno)
{
  unsigned int regno, endregno;

  if (GET_CODE (dest) == SUBREG
      && (((GET_MODE_SIZE (GET_MODE (dest))
	    + UNITS_PER_WORD - 1) / UNITS_PER_WORD)
	  == ((GET_MODE_SIZE (GET_MODE (SUBREG_REG (dest)))
	       + UNITS_PER_WORD - 1) / UNITS_PER_WORD)))
    dest = SUBREG_REG (dest);

  if (!REG_P (dest))
    return false;

  regno = REGNO (dest);
  endregno = (regno >= FIRST_PSEUDO_REGISTER ? regno + 1
	      : regno + hard_regno_nregs[regno][GET_MODE (dest)]);
  return (test_regno >= regno && test_regno < endregno);
}

/* Like covers_regno_no_parallel_p, but also handles PARALLELs where
   any member matches the covers_regno_no_parallel_p criteria.  */

static bool
covers_regno_p (rtx dest, unsigned int test_regno)
{
  if (GET_CODE (dest) == PARALLEL)
    {
      /* Some targets place small structures in registers for return
	 values of functions, and those registers are wrapped in
	 PARALLELs that we may see as the destination of a SET.  */
      int i;

      for (i = XVECLEN (dest, 0) - 1; i >= 0; i--)
	{
	  rtx inner = XEXP (XVECEXP (dest, 0, i), 0);
	  if (inner != NULL_RTX
	      && covers_regno_no_parallel_p (inner, test_regno))
	    return true;
	}

      return false;
    }
  else
    return covers_regno_no_parallel_p (dest, test_regno);
}

/* Utility function for dead_or_set_p to check an individual register.  Also
   called from flow.c.  */

int
dead_or_set_regno_p (rtx insn, unsigned int test_regno)
{
  rtx pattern;

  /* See if there is a death note for something that includes TEST_REGNO.  */
  if (find_regno_note (insn, REG_DEAD, test_regno))
    return 1;

  if (CALL_P (insn)
      && find_regno_fusage (insn, CLOBBER, test_regno))
    return 1;

  pattern = PATTERN (insn);

  if (GET_CODE (pattern) == COND_EXEC)
    pattern = COND_EXEC_CODE (pattern);

  if (GET_CODE (pattern) == SET)
    return covers_regno_p (SET_DEST (pattern), test_regno);
  else if (GET_CODE (pattern) == PARALLEL)
    {
      int i;

      for (i = XVECLEN (pattern, 0) - 1; i >= 0; i--)
	{
	  rtx body = XVECEXP (pattern, 0, i);

	  if (GET_CODE (body) == COND_EXEC)
	    body = COND_EXEC_CODE (body);

	  if ((GET_CODE (body) == SET || GET_CODE (body) == CLOBBER)
	      && covers_regno_p (SET_DEST (body), test_regno))
	    return 1;
	}
    }

  return 0;
}

/* Return the reg-note of kind KIND in insn INSN, if there is one.
   If DATUM is nonzero, look for one whose datum is DATUM.  */

rtx
find_reg_note (rtx insn, enum reg_note kind, rtx datum)
{
  rtx link;

  gcc_assert (insn);

  /* Ignore anything that is not an INSN, JUMP_INSN or CALL_INSN.  */
  if (! INSN_P (insn))
    return 0;
  if (datum == 0)
    {
      for (link = REG_NOTES (insn); link; link = XEXP (link, 1))
	if (REG_NOTE_KIND (link) == kind)
	  return link;
      return 0;
    }

  for (link = REG_NOTES (insn); link; link = XEXP (link, 1))
    if (REG_NOTE_KIND (link) == kind && datum == XEXP (link, 0))
      return link;
  return 0;
}

/* Return the reg-note of kind KIND in insn INSN which applies to register
   number REGNO, if any.  Return 0 if there is no such reg-note.  Note that
   the REGNO of this NOTE need not be REGNO if REGNO is a hard register;
   it might be the case that the note overlaps REGNO.  */

rtx
find_regno_note (rtx insn, enum reg_note kind, unsigned int regno)
{
  rtx link;

  /* Ignore anything that is not an INSN, JUMP_INSN or CALL_INSN.  */
  if (! INSN_P (insn))
    return 0;

  for (link = REG_NOTES (insn); link; link = XEXP (link, 1))
    if (REG_NOTE_KIND (link) == kind
	/* Verify that it is a register, so that scratch and MEM won't cause a
	   problem here.  */
	&& REG_P (XEXP (link, 0))
	&& REGNO (XEXP (link, 0)) <= regno
	&& ((REGNO (XEXP (link, 0))
	     + (REGNO (XEXP (link, 0)) >= FIRST_PSEUDO_REGISTER ? 1
		: hard_regno_nregs[REGNO (XEXP (link, 0))]
				  [GET_MODE (XEXP (link, 0))]))
	    > regno))
      return link;
  return 0;
}

/* Return a REG_EQUIV or REG_EQUAL note if insn has only a single set and
   has such a note.  */

rtx
find_reg_equal_equiv_note (rtx insn)
{
  rtx link;

  if (!INSN_P (insn))
    return 0;
  for (link = REG_NOTES (insn); link; link = XEXP (link, 1))
    if (REG_NOTE_KIND (link) == REG_EQUAL
	|| REG_NOTE_KIND (link) == REG_EQUIV)
      {
	if (single_set (insn) == 0)
	  return 0;
	return link;
      }
  return NULL;
}

/* Return true if DATUM, or any overlap of DATUM, of kind CODE is found
   in the CALL_INSN_FUNCTION_USAGE information of INSN.  */

int
find_reg_fusage (rtx insn, enum rtx_code code, rtx datum)
{
  /* If it's not a CALL_INSN, it can't possibly have a
     CALL_INSN_FUNCTION_USAGE field, so don't bother checking.  */
  if (!CALL_P (insn))
    return 0;

  gcc_assert (datum);

  if (!REG_P (datum))
    {
      rtx link;

      for (link = CALL_INSN_FUNCTION_USAGE (insn);
	   link;
	   link = XEXP (link, 1))
	if (GET_CODE (XEXP (link, 0)) == code
	    && rtx_equal_p (datum, XEXP (XEXP (link, 0), 0)))
	  return 1;
    }
  else
    {
      unsigned int regno = REGNO (datum);

      /* CALL_INSN_FUNCTION_USAGE information cannot contain references
	 to pseudo registers, so don't bother checking.  */

      if (regno < FIRST_PSEUDO_REGISTER)
	{
	  unsigned int end_regno
	    = regno + hard_regno_nregs[regno][GET_MODE (datum)];
	  unsigned int i;

	  for (i = regno; i < end_regno; i++)
	    if (find_regno_fusage (insn, code, i))
	      return 1;
	}
    }

  return 0;
}

/* Return true if REGNO, or any overlap of REGNO, of kind CODE is found
   in the CALL_INSN_FUNCTION_USAGE information of INSN.  */

int
find_regno_fusage (rtx insn, enum rtx_code code, unsigned int regno)
{
  rtx link;

  /* CALL_INSN_FUNCTION_USAGE information cannot contain references
     to pseudo registers, so don't bother checking.  */

  if (regno >= FIRST_PSEUDO_REGISTER
      || !CALL_P (insn) )
    return 0;

  for (link = CALL_INSN_FUNCTION_USAGE (insn); link; link = XEXP (link, 1))
    {
      unsigned int regnote;
      rtx op, reg;

      if (GET_CODE (op = XEXP (link, 0)) == code
	  && REG_P (reg = XEXP (op, 0))
	  && (regnote = REGNO (reg)) <= regno
	  && regnote + hard_regno_nregs[regnote][GET_MODE (reg)] > regno)
	return 1;
    }

  return 0;
}

/* Return true if INSN is a call to a pure function.  */

int
pure_call_p (rtx insn)
{
  rtx link;

  if (!CALL_P (insn) || ! CONST_OR_PURE_CALL_P (insn))
    return 0;

  /* Look for the note that differentiates const and pure functions.  */
  for (link = CALL_INSN_FUNCTION_USAGE (insn); link; link = XEXP (link, 1))
    {
      rtx u, m;

      if (GET_CODE (u = XEXP (link, 0)) == USE
	  && MEM_P (m = XEXP (u, 0)) && GET_MODE (m) == BLKmode
	  && GET_CODE (XEXP (m, 0)) == SCRATCH)
	return 1;
    }

  return 0;
}

/* Remove register note NOTE from the REG_NOTES of INSN.  */

void
remove_note (rtx insn, rtx note)
{
  rtx link;

  if (note == NULL_RTX)
    return;

  if (REG_NOTES (insn) == note)
    {
      REG_NOTES (insn) = XEXP (note, 1);
      return;
    }

  for (link = REG_NOTES (insn); link; link = XEXP (link, 1))
    if (XEXP (link, 1) == note)
      {
	XEXP (link, 1) = XEXP (note, 1);
	return;
      }

  gcc_unreachable ();
}

/* Search LISTP (an EXPR_LIST) for an entry whose first operand is NODE and
   return 1 if it is found.  A simple equality test is used to determine if
   NODE matches.  */

int
in_expr_list_p (rtx listp, rtx node)
{
  rtx x;

  for (x = listp; x; x = XEXP (x, 1))
    if (node == XEXP (x, 0))
      return 1;

  return 0;
}

/* Search LISTP (an EXPR_LIST) for an entry whose first operand is NODE and
   remove that entry from the list if it is found.

   A simple equality test is used to determine if NODE matches.  */

void
remove_node_from_expr_list (rtx node, rtx *listp)
{
  rtx temp = *listp;
  rtx prev = NULL_RTX;

  while (temp)
    {
      if (node == XEXP (temp, 0))
	{
	  /* Splice the node out of the list.  */
	  if (prev)
	    XEXP (prev, 1) = XEXP (temp, 1);
	  else
	    *listp = XEXP (temp, 1);

	  return;
	}

      prev = temp;
      temp = XEXP (temp, 1);
    }
}

/* Nonzero if X contains any volatile instructions.  These are instructions
   which may cause unpredictable machine state instructions, and thus no
   instructions should be moved or combined across them.  This includes
   only volatile asms and UNSPEC_VOLATILE instructions.  */

int
volatile_insn_p (rtx x)
{
  RTX_CODE code;

  code = GET_CODE (x);
  switch (code)
    {
    case LABEL_REF:
    case SYMBOL_REF:
    case CONST_INT:
    case CONST:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case CC0:
    case PC:
    case REG:
    case SCRATCH:
    case CLOBBER:
    case ADDR_VEC:
    case ADDR_DIFF_VEC:
    case CALL:
    case MEM:
      return 0;

    case UNSPEC_VOLATILE:
 /* case TRAP_IF: This isn't clear yet.  */
      return 1;

    case ASM_INPUT:
    case ASM_OPERANDS:
      if (MEM_VOLATILE_P (x))
	return 1;

    default:
      break;
    }

  /* Recursively scan the operands of this expression.  */

  {
    const char *fmt = GET_RTX_FORMAT (code);
    int i;

    for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
      {
	if (fmt[i] == 'e')
	  {
	    if (volatile_insn_p (XEXP (x, i)))
	      return 1;
	  }
	else if (fmt[i] == 'E')
	  {
	    int j;
	    for (j = 0; j < XVECLEN (x, i); j++)
	      if (volatile_insn_p (XVECEXP (x, i, j)))
		return 1;
	  }
      }
  }
  return 0;
}

/* Nonzero if X contains any volatile memory references
   UNSPEC_VOLATILE operations or volatile ASM_OPERANDS expressions.  */

int
volatile_refs_p (rtx x)
{
  RTX_CODE code;

  code = GET_CODE (x);
  switch (code)
    {
    case LABEL_REF:
    case SYMBOL_REF:
    case CONST_INT:
    case CONST:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case CC0:
    case PC:
    case REG:
    case SCRATCH:
    case CLOBBER:
    case ADDR_VEC:
    case ADDR_DIFF_VEC:
      return 0;

    case UNSPEC_VOLATILE:
      return 1;

    case MEM:
    case ASM_INPUT:
    case ASM_OPERANDS:
      if (MEM_VOLATILE_P (x))
	return 1;

    default:
      break;
    }

  /* Recursively scan the operands of this expression.  */

  {
    const char *fmt = GET_RTX_FORMAT (code);
    int i;

    for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
      {
	if (fmt[i] == 'e')
	  {
	    if (volatile_refs_p (XEXP (x, i)))
	      return 1;
	  }
	else if (fmt[i] == 'E')
	  {
	    int j;
	    for (j = 0; j < XVECLEN (x, i); j++)
	      if (volatile_refs_p (XVECEXP (x, i, j)))
		return 1;
	  }
      }
  }
  return 0;
}

/* Similar to above, except that it also rejects register pre- and post-
   incrementing.  */

int
side_effects_p (rtx x)
{
  RTX_CODE code;

  code = GET_CODE (x);
  switch (code)
    {
    case LABEL_REF:
    case SYMBOL_REF:
    case CONST_INT:
    case CONST:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case CC0:
    case PC:
    case REG:
    case SCRATCH:
    case ADDR_VEC:
    case ADDR_DIFF_VEC:
      return 0;

    case CLOBBER:
      /* Reject CLOBBER with a non-VOID mode.  These are made by combine.c
	 when some combination can't be done.  If we see one, don't think
	 that we can simplify the expression.  */
      return (GET_MODE (x) != VOIDmode);

    case PRE_INC:
    case PRE_DEC:
    case POST_INC:
    case POST_DEC:
    case PRE_MODIFY:
    case POST_MODIFY:
    case CALL:
    case UNSPEC_VOLATILE:
 /* case TRAP_IF: This isn't clear yet.  */
      return 1;

    case MEM:
    case ASM_INPUT:
    case ASM_OPERANDS:
      if (MEM_VOLATILE_P (x))
	return 1;

    default:
      break;
    }

  /* Recursively scan the operands of this expression.  */

  {
    const char *fmt = GET_RTX_FORMAT (code);
    int i;

    for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
      {
	if (fmt[i] == 'e')
	  {
	    if (side_effects_p (XEXP (x, i)))
	      return 1;
	  }
	else if (fmt[i] == 'E')
	  {
	    int j;
	    for (j = 0; j < XVECLEN (x, i); j++)
	      if (side_effects_p (XVECEXP (x, i, j)))
		return 1;
	  }
      }
  }
  return 0;
}

enum may_trap_p_flags
{
  MTP_UNALIGNED_MEMS = 1,
  MTP_AFTER_MOVE = 2
};
/* Return nonzero if evaluating rtx X might cause a trap.
   (FLAGS & MTP_UNALIGNED_MEMS) controls whether nonzero is returned for
   unaligned memory accesses on strict alignment machines.  If
   (FLAGS & AFTER_MOVE) is true, returns nonzero even in case the expression
   cannot trap at its current location, but it might become trapping if moved
   elsewhere.  */

static int
may_trap_p_1 (rtx x, unsigned flags)
{
  int i;
  enum rtx_code code;
  const char *fmt;
  bool unaligned_mems = (flags & MTP_UNALIGNED_MEMS) != 0;

  if (x == 0)
    return 0;
  code = GET_CODE (x);
  switch (code)
    {
      /* Handle these cases quickly.  */
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case SYMBOL_REF:
    case LABEL_REF:
    case CONST:
    case PC:
    case CC0:
    case REG:
    case SCRATCH:
      return 0;

    case ASM_INPUT:
    case UNSPEC_VOLATILE:
    case TRAP_IF:
      return 1;

    case ASM_OPERANDS:
      return MEM_VOLATILE_P (x);

      /* Memory ref can trap unless it's a static var or a stack slot.  */
    case MEM:
      if (/* MEM_NOTRAP_P only relates to the actual position of the memory
	     reference; moving it out of condition might cause its address
	     become invalid.  */
	  !(flags & MTP_AFTER_MOVE)
	  && MEM_NOTRAP_P (x)
	  && (!STRICT_ALIGNMENT || !unaligned_mems))
	return 0;
      return
	rtx_addr_can_trap_p_1 (XEXP (x, 0), GET_MODE (x), unaligned_mems);

      /* Division by a non-constant might trap.  */
    case DIV:
    case MOD:
    case UDIV:
    case UMOD:
      if (HONOR_SNANS (GET_MODE (x)))
	return 1;
      if (SCALAR_FLOAT_MODE_P (GET_MODE (x)))
	return flag_trapping_math;
      if (!CONSTANT_P (XEXP (x, 1)) || (XEXP (x, 1) == const0_rtx))
	return 1;
      break;

    case EXPR_LIST:
      /* An EXPR_LIST is used to represent a function call.  This
	 certainly may trap.  */
      return 1;

    case GE:
    case GT:
    case LE:
    case LT:
    case LTGT:
    case COMPARE:
      /* Some floating point comparisons may trap.  */
      if (!flag_trapping_math)
	break;
      /* ??? There is no machine independent way to check for tests that trap
	 when COMPARE is used, though many targets do make this distinction.
	 For instance, sparc uses CCFPE for compares which generate exceptions
	 and CCFP for compares which do not generate exceptions.  */
      if (HONOR_NANS (GET_MODE (x)))
	return 1;
      /* But often the compare has some CC mode, so check operand
	 modes as well.  */
      if (HONOR_NANS (GET_MODE (XEXP (x, 0)))
	  || HONOR_NANS (GET_MODE (XEXP (x, 1))))
	return 1;
      break;

    case EQ:
    case NE:
      if (HONOR_SNANS (GET_MODE (x)))
	return 1;
      /* Often comparison is CC mode, so check operand modes.  */
      if (HONOR_SNANS (GET_MODE (XEXP (x, 0)))
	  || HONOR_SNANS (GET_MODE (XEXP (x, 1))))
	return 1;
      break;

    case FIX:
      /* Conversion of floating point might trap.  */
      if (flag_trapping_math && HONOR_NANS (GET_MODE (XEXP (x, 0))))
	return 1;
      break;

    case NEG:
    case ABS:
    case SUBREG:
      /* These operations don't trap even with floating point.  */
      break;

    default:
      /* Any floating arithmetic may trap.  */
      if (SCALAR_FLOAT_MODE_P (GET_MODE (x))
	  && flag_trapping_math)
	return 1;
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	{
	  if (may_trap_p_1 (XEXP (x, i), flags))
	    return 1;
	}
      else if (fmt[i] == 'E')
	{
	  int j;
	  for (j = 0; j < XVECLEN (x, i); j++)
	    if (may_trap_p_1 (XVECEXP (x, i, j), flags))
	      return 1;
	}
    }
  return 0;
}

/* Return nonzero if evaluating rtx X might cause a trap.  */

int
may_trap_p (rtx x)
{
  return may_trap_p_1 (x, 0);
}

/* Return nonzero if evaluating rtx X might cause a trap, when the expression
   is moved from its current location by some optimization.  */

int
may_trap_after_code_motion_p (rtx x)
{
  return may_trap_p_1 (x, MTP_AFTER_MOVE);
}

/* Same as above, but additionally return nonzero if evaluating rtx X might
   cause a fault.  We define a fault for the purpose of this function as a
   erroneous execution condition that cannot be encountered during the normal
   execution of a valid program; the typical example is an unaligned memory
   access on a strict alignment machine.  The compiler guarantees that it
   doesn't generate code that will fault from a valid program, but this
   guarantee doesn't mean anything for individual instructions.  Consider
   the following example:

      struct S { int d; union { char *cp; int *ip; }; };

      int foo(struct S *s)
      {
	if (s->d == 1)
	  return *s->ip;
	else
	  return *s->cp;
      }

   on a strict alignment machine.  In a valid program, foo will never be
   invoked on a structure for which d is equal to 1 and the underlying
   unique field of the union not aligned on a 4-byte boundary, but the
   expression *s->ip might cause a fault if considered individually.

   At the RTL level, potentially problematic expressions will almost always
   verify may_trap_p; for example, the above dereference can be emitted as
   (mem:SI (reg:P)) and this expression is may_trap_p for a generic register.
   However, suppose that foo is inlined in a caller that causes s->cp to
   point to a local character variable and guarantees that s->d is not set
   to 1; foo may have been effectively translated into pseudo-RTL as:

      if ((reg:SI) == 1)
	(set (reg:SI) (mem:SI (%fp - 7)))
      else
	(set (reg:QI) (mem:QI (%fp - 7)))

   Now (mem:SI (%fp - 7)) is considered as not may_trap_p since it is a
   memory reference to a stack slot, but it will certainly cause a fault
   on a strict alignment machine.  */

int
may_trap_or_fault_p (rtx x)
{
  return may_trap_p_1 (x, MTP_UNALIGNED_MEMS);
}

/* Return nonzero if X contains a comparison that is not either EQ or NE,
   i.e., an inequality.  */

int
inequality_comparisons_p (rtx x)
{
  const char *fmt;
  int len, i;
  enum rtx_code code = GET_CODE (x);

  switch (code)
    {
    case REG:
    case SCRATCH:
    case PC:
    case CC0:
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
      return 0;

    case LT:
    case LTU:
    case GT:
    case GTU:
    case LE:
    case LEU:
    case GE:
    case GEU:
      return 1;

    default:
      break;
    }

  len = GET_RTX_LENGTH (code);
  fmt = GET_RTX_FORMAT (code);

  for (i = 0; i < len; i++)
    {
      if (fmt[i] == 'e')
	{
	  if (inequality_comparisons_p (XEXP (x, i)))
	    return 1;
	}
      else if (fmt[i] == 'E')
	{
	  int j;
	  for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	    if (inequality_comparisons_p (XVECEXP (x, i, j)))
	      return 1;
	}
    }

  return 0;
}

/* Replace any occurrence of FROM in X with TO.  The function does
   not enter into CONST_DOUBLE for the replace.

   Note that copying is not done so X must not be shared unless all copies
   are to be modified.  */

rtx
replace_rtx (rtx x, rtx from, rtx to)
{
  int i, j;
  const char *fmt;

  /* The following prevents loops occurrence when we change MEM in
     CONST_DOUBLE onto the same CONST_DOUBLE.  */
  if (x != 0 && GET_CODE (x) == CONST_DOUBLE)
    return x;

  if (x == from)
    return to;

  /* Allow this function to make replacements in EXPR_LISTs.  */
  if (x == 0)
    return 0;

  if (GET_CODE (x) == SUBREG)
    {
      rtx new = replace_rtx (SUBREG_REG (x), from, to);

      if (GET_CODE (new) == CONST_INT)
	{
	  x = simplify_subreg (GET_MODE (x), new,
			       GET_MODE (SUBREG_REG (x)),
			       SUBREG_BYTE (x));
	  gcc_assert (x);
	}
      else
	SUBREG_REG (x) = new;

      return x;
    }
  else if (GET_CODE (x) == ZERO_EXTEND)
    {
      rtx new = replace_rtx (XEXP (x, 0), from, to);

      if (GET_CODE (new) == CONST_INT)
	{
	  x = simplify_unary_operation (ZERO_EXTEND, GET_MODE (x),
					new, GET_MODE (XEXP (x, 0)));
	  gcc_assert (x);
	}
      else
	XEXP (x, 0) = new;

      return x;
    }

  fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	XEXP (x, i) = replace_rtx (XEXP (x, i), from, to);
      else if (fmt[i] == 'E')
	for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	  XVECEXP (x, i, j) = replace_rtx (XVECEXP (x, i, j), from, to);
    }

  return x;
}

/* Replace occurrences of the old label in *X with the new one.
   DATA is a REPLACE_LABEL_DATA containing the old and new labels.  */

int
replace_label (rtx *x, void *data)
{
  rtx l = *x;
  rtx old_label = ((replace_label_data *) data)->r1;
  rtx new_label = ((replace_label_data *) data)->r2;
  bool update_label_nuses = ((replace_label_data *) data)->update_label_nuses;

  if (l == NULL_RTX)
    return 0;

  if (GET_CODE (l) == SYMBOL_REF
      && CONSTANT_POOL_ADDRESS_P (l))
    {
      rtx c = get_pool_constant (l);
      if (rtx_referenced_p (old_label, c))
	{
	  rtx new_c, new_l;
	  replace_label_data *d = (replace_label_data *) data;

	  /* Create a copy of constant C; replace the label inside
	     but do not update LABEL_NUSES because uses in constant pool
	     are not counted.  */
	  new_c = copy_rtx (c);
	  d->update_label_nuses = false;
	  for_each_rtx (&new_c, replace_label, data);
	  d->update_label_nuses = update_label_nuses;

	  /* Add the new constant NEW_C to constant pool and replace
	     the old reference to constant by new reference.  */
	  new_l = XEXP (force_const_mem (get_pool_mode (l), new_c), 0);
	  *x = replace_rtx (l, l, new_l);
	}
      return 0;
    }

  /* If this is a JUMP_INSN, then we also need to fix the JUMP_LABEL
     field.  This is not handled by for_each_rtx because it doesn't
     handle unprinted ('0') fields.  */
  if (JUMP_P (l) && JUMP_LABEL (l) == old_label)
    JUMP_LABEL (l) = new_label;

  if ((GET_CODE (l) == LABEL_REF
       || GET_CODE (l) == INSN_LIST)
      && XEXP (l, 0) == old_label)
    {
      XEXP (l, 0) = new_label;
      if (update_label_nuses)
	{
	  ++LABEL_NUSES (new_label);
	  --LABEL_NUSES (old_label);
	}
      return 0;
    }

  return 0;
}

/* When *BODY is equal to X or X is directly referenced by *BODY
   return nonzero, thus FOR_EACH_RTX stops traversing and returns nonzero
   too, otherwise FOR_EACH_RTX continues traversing *BODY.  */

static int
rtx_referenced_p_1 (rtx *body, void *x)
{
  rtx y = (rtx) x;

  if (*body == NULL_RTX)
    return y == NULL_RTX;

  /* Return true if a label_ref *BODY refers to label Y.  */
  if (GET_CODE (*body) == LABEL_REF && LABEL_P (y))
    return XEXP (*body, 0) == y;

  /* If *BODY is a reference to pool constant traverse the constant.  */
  if (GET_CODE (*body) == SYMBOL_REF
      && CONSTANT_POOL_ADDRESS_P (*body))
    return rtx_referenced_p (y, get_pool_constant (*body));

  /* By default, compare the RTL expressions.  */
  return rtx_equal_p (*body, y);
}

/* Return true if X is referenced in BODY.  */

int
rtx_referenced_p (rtx x, rtx body)
{
  return for_each_rtx (&body, rtx_referenced_p_1, x);
}

/* If INSN is a tablejump return true and store the label (before jump table) to
   *LABELP and the jump table to *TABLEP.  LABELP and TABLEP may be NULL.  */

bool
tablejump_p (rtx insn, rtx *labelp, rtx *tablep)
{
  rtx label, table;

  if (JUMP_P (insn)
      && (label = JUMP_LABEL (insn)) != NULL_RTX
      && (table = next_active_insn (label)) != NULL_RTX
      && JUMP_P (table)
      && (GET_CODE (PATTERN (table)) == ADDR_VEC
	  || GET_CODE (PATTERN (table)) == ADDR_DIFF_VEC))
    {
      if (labelp)
	*labelp = label;
      if (tablep)
	*tablep = table;
      return true;
    }
  return false;
}

/* A subroutine of computed_jump_p, return 1 if X contains a REG or MEM or
   constant that is not in the constant pool and not in the condition
   of an IF_THEN_ELSE.  */

static int
computed_jump_p_1 (rtx x)
{
  enum rtx_code code = GET_CODE (x);
  int i, j;
  const char *fmt;

  switch (code)
    {
    case LABEL_REF:
    case PC:
      return 0;

    case CONST:
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case SYMBOL_REF:
    case REG:
      return 1;

    case MEM:
      return ! (GET_CODE (XEXP (x, 0)) == SYMBOL_REF
		&& CONSTANT_POOL_ADDRESS_P (XEXP (x, 0)));

    case IF_THEN_ELSE:
      return (computed_jump_p_1 (XEXP (x, 1))
	      || computed_jump_p_1 (XEXP (x, 2)));

    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e'
	  && computed_jump_p_1 (XEXP (x, i)))
	return 1;

      else if (fmt[i] == 'E')
	for (j = 0; j < XVECLEN (x, i); j++)
	  if (computed_jump_p_1 (XVECEXP (x, i, j)))
	    return 1;
    }

  return 0;
}

/* Return nonzero if INSN is an indirect jump (aka computed jump).

   Tablejumps and casesi insns are not considered indirect jumps;
   we can recognize them by a (use (label_ref)).  */

int
computed_jump_p (rtx insn)
{
  int i;
  if (JUMP_P (insn))
    {
      rtx pat = PATTERN (insn);

      if (find_reg_note (insn, REG_LABEL, NULL_RTX))
	return 0;
      else if (GET_CODE (pat) == PARALLEL)
	{
	  int len = XVECLEN (pat, 0);
	  int has_use_labelref = 0;

	  for (i = len - 1; i >= 0; i--)
	    if (GET_CODE (XVECEXP (pat, 0, i)) == USE
		&& (GET_CODE (XEXP (XVECEXP (pat, 0, i), 0))
		    == LABEL_REF))
	      has_use_labelref = 1;

	  if (! has_use_labelref)
	    for (i = len - 1; i >= 0; i--)
	      if (GET_CODE (XVECEXP (pat, 0, i)) == SET
		  && SET_DEST (XVECEXP (pat, 0, i)) == pc_rtx
		  && computed_jump_p_1 (SET_SRC (XVECEXP (pat, 0, i))))
		return 1;
	}
      else if (GET_CODE (pat) == SET
	       && SET_DEST (pat) == pc_rtx
	       && computed_jump_p_1 (SET_SRC (pat)))
	return 1;
    }
  return 0;
}

/* Optimized loop of for_each_rtx, trying to avoid useless recursive
   calls.  Processes the subexpressions of EXP and passes them to F.  */
static int
for_each_rtx_1 (rtx exp, int n, rtx_function f, void *data)
{
  int result, i, j;
  const char *format = GET_RTX_FORMAT (GET_CODE (exp));
  rtx *x;

  for (; format[n] != '\0'; n++)
    {
      switch (format[n])
	{
	case 'e':
	  /* Call F on X.  */
	  x = &XEXP (exp, n);
	  result = (*f) (x, data);
	  if (result == -1)
	    /* Do not traverse sub-expressions.  */
	    continue;
	  else if (result != 0)
	    /* Stop the traversal.  */
	    return result;
	
	  if (*x == NULL_RTX)
	    /* There are no sub-expressions.  */
	    continue;
	
	  i = non_rtx_starting_operands[GET_CODE (*x)];
	  if (i >= 0)
	    {
	      result = for_each_rtx_1 (*x, i, f, data);
	      if (result != 0)
		return result;
	    }
	  break;

	case 'V':
	case 'E':
	  if (XVEC (exp, n) == 0)
	    continue;
	  for (j = 0; j < XVECLEN (exp, n); ++j)
	    {
	      /* Call F on X.  */
	      x = &XVECEXP (exp, n, j);
	      result = (*f) (x, data);
	      if (result == -1)
		/* Do not traverse sub-expressions.  */
		continue;
	      else if (result != 0)
		/* Stop the traversal.  */
		return result;
	
	      if (*x == NULL_RTX)
		/* There are no sub-expressions.  */
		continue;
	
	      i = non_rtx_starting_operands[GET_CODE (*x)];
	      if (i >= 0)
		{
		  result = for_each_rtx_1 (*x, i, f, data);
		  if (result != 0)
		    return result;
	        }
	    }
	  break;

	default:
	  /* Nothing to do.  */
	  break;
	}
    }

  return 0;
}

/* Traverse X via depth-first search, calling F for each
   sub-expression (including X itself).  F is also passed the DATA.
   If F returns -1, do not traverse sub-expressions, but continue
   traversing the rest of the tree.  If F ever returns any other
   nonzero value, stop the traversal, and return the value returned
   by F.  Otherwise, return 0.  This function does not traverse inside
   tree structure that contains RTX_EXPRs, or into sub-expressions
   whose format code is `0' since it is not known whether or not those
   codes are actually RTL.

   This routine is very general, and could (should?) be used to
   implement many of the other routines in this file.  */

int
for_each_rtx (rtx *x, rtx_function f, void *data)
{
  int result;
  int i;

  /* Call F on X.  */
  result = (*f) (x, data);
  if (result == -1)
    /* Do not traverse sub-expressions.  */
    return 0;
  else if (result != 0)
    /* Stop the traversal.  */
    return result;

  if (*x == NULL_RTX)
    /* There are no sub-expressions.  */
    return 0;

  i = non_rtx_starting_operands[GET_CODE (*x)];
  if (i < 0)
    return 0;

  return for_each_rtx_1 (*x, i, f, data);
}


/* Searches X for any reference to REGNO, returning the rtx of the
   reference found if any.  Otherwise, returns NULL_RTX.  */

rtx
regno_use_in (unsigned int regno, rtx x)
{
  const char *fmt;
  int i, j;
  rtx tem;

  if (REG_P (x) && REGNO (x) == regno)
    return x;

  fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	{
	  if ((tem = regno_use_in (regno, XEXP (x, i))))
	    return tem;
	}
      else if (fmt[i] == 'E')
	for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	  if ((tem = regno_use_in (regno , XVECEXP (x, i, j))))
	    return tem;
    }

  return NULL_RTX;
}

/* Return a value indicating whether OP, an operand of a commutative
   operation, is preferred as the first or second operand.  The higher
   the value, the stronger the preference for being the first operand.
   We use negative values to indicate a preference for the first operand
   and positive values for the second operand.  */

int
commutative_operand_precedence (rtx op)
{
  enum rtx_code code = GET_CODE (op);
  
  /* Constants always come the second operand.  Prefer "nice" constants.  */
  if (code == CONST_INT)
    return -7;
  if (code == CONST_DOUBLE)
    return -6;
  op = avoid_constant_pool_reference (op);
  code = GET_CODE (op);

  switch (GET_RTX_CLASS (code))
    {
    case RTX_CONST_OBJ:
      if (code == CONST_INT)
        return -5;
      if (code == CONST_DOUBLE)
        return -4;
      return -3;

    case RTX_EXTRA:
      /* SUBREGs of objects should come second.  */
      if (code == SUBREG && OBJECT_P (SUBREG_REG (op)))
        return -2;

      if (!CONSTANT_P (op))
        return 0;
      else
	/* As for RTX_CONST_OBJ.  */
	return -3;

    case RTX_OBJ:
      /* Complex expressions should be the first, so decrease priority
         of objects.  */
      return -1;

    case RTX_COMM_ARITH:
      /* Prefer operands that are themselves commutative to be first.
         This helps to make things linear.  In particular,
         (and (and (reg) (reg)) (not (reg))) is canonical.  */
      return 4;

    case RTX_BIN_ARITH:
      /* If only one operand is a binary expression, it will be the first
         operand.  In particular,  (plus (minus (reg) (reg)) (neg (reg)))
         is canonical, although it will usually be further simplified.  */
      return 2;
  
    case RTX_UNARY:
      /* Then prefer NEG and NOT.  */
      if (code == NEG || code == NOT)
        return 1;

    default:
      return 0;
    }
}

/* Return 1 iff it is necessary to swap operands of commutative operation
   in order to canonicalize expression.  */

int
swap_commutative_operands_p (rtx x, rtx y)
{
  return (commutative_operand_precedence (x)
	  < commutative_operand_precedence (y));
}

/* Return 1 if X is an autoincrement side effect and the register is
   not the stack pointer.  */
int
auto_inc_p (rtx x)
{
  switch (GET_CODE (x))
    {
    case PRE_INC:
    case POST_INC:
    case PRE_DEC:
    case POST_DEC:
    case PRE_MODIFY:
    case POST_MODIFY:
      /* There are no REG_INC notes for SP.  */
      if (XEXP (x, 0) != stack_pointer_rtx)
	return 1;
    default:
      break;
    }
  return 0;
}

/* Return nonzero if IN contains a piece of rtl that has the address LOC.  */
int
loc_mentioned_in_p (rtx *loc, rtx in)
{
  enum rtx_code code = GET_CODE (in);
  const char *fmt = GET_RTX_FORMAT (code);
  int i, j;

  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (loc == &in->u.fld[i].rt_rtx)
	return 1;
      if (fmt[i] == 'e')
	{
	  if (loc_mentioned_in_p (loc, XEXP (in, i)))
	    return 1;
	}
      else if (fmt[i] == 'E')
	for (j = XVECLEN (in, i) - 1; j >= 0; j--)
	  if (loc_mentioned_in_p (loc, XVECEXP (in, i, j)))
	    return 1;
    }
  return 0;
}

/* Helper function for subreg_lsb.  Given a subreg's OUTER_MODE, INNER_MODE,
   and SUBREG_BYTE, return the bit offset where the subreg begins
   (counting from the least significant bit of the operand).  */

unsigned int
subreg_lsb_1 (enum machine_mode outer_mode,
	      enum machine_mode inner_mode,
	      unsigned int subreg_byte)
{
  unsigned int bitpos;
  unsigned int byte;
  unsigned int word;

  /* A paradoxical subreg begins at bit position 0.  */
  if (GET_MODE_BITSIZE (outer_mode) > GET_MODE_BITSIZE (inner_mode))
    return 0;

  if (WORDS_BIG_ENDIAN != BYTES_BIG_ENDIAN)
    /* If the subreg crosses a word boundary ensure that
       it also begins and ends on a word boundary.  */
    gcc_assert (!((subreg_byte % UNITS_PER_WORD
		  + GET_MODE_SIZE (outer_mode)) > UNITS_PER_WORD
		  && (subreg_byte % UNITS_PER_WORD
		      || GET_MODE_SIZE (outer_mode) % UNITS_PER_WORD)));

  if (WORDS_BIG_ENDIAN)
    word = (GET_MODE_SIZE (inner_mode)
	    - (subreg_byte + GET_MODE_SIZE (outer_mode))) / UNITS_PER_WORD;
  else
    word = subreg_byte / UNITS_PER_WORD;
  bitpos = word * BITS_PER_WORD;

  if (BYTES_BIG_ENDIAN)
    byte = (GET_MODE_SIZE (inner_mode)
	    - (subreg_byte + GET_MODE_SIZE (outer_mode))) % UNITS_PER_WORD;
  else
    byte = subreg_byte % UNITS_PER_WORD;
  bitpos += byte * BITS_PER_UNIT;

  return bitpos;
}

/* Given a subreg X, return the bit offset where the subreg begins
   (counting from the least significant bit of the reg).  */

unsigned int
subreg_lsb (rtx x)
{
  return subreg_lsb_1 (GET_MODE (x), GET_MODE (SUBREG_REG (x)),
		       SUBREG_BYTE (x));
}

/* This function returns the regno offset of a subreg expression.
   xregno - A regno of an inner hard subreg_reg (or what will become one).
   xmode  - The mode of xregno.
   offset - The byte offset.
   ymode  - The mode of a top level SUBREG (or what may become one).
   RETURN - The regno offset which would be used.  */
unsigned int
subreg_regno_offset (unsigned int xregno, enum machine_mode xmode,
		     unsigned int offset, enum machine_mode ymode)
{
  int nregs_xmode, nregs_ymode;
  int mode_multiple, nregs_multiple;
  int y_offset;

  gcc_assert (xregno < FIRST_PSEUDO_REGISTER);

  /* Adjust nregs_xmode to allow for 'holes'.  */
  if (HARD_REGNO_NREGS_HAS_PADDING (xregno, xmode))
    nregs_xmode = HARD_REGNO_NREGS_WITH_PADDING (xregno, xmode);
  else
    nregs_xmode = hard_regno_nregs[xregno][xmode];
    
  nregs_ymode = hard_regno_nregs[xregno][ymode];

  /* If this is a big endian paradoxical subreg, which uses more actual
     hard registers than the original register, we must return a negative
     offset so that we find the proper highpart of the register.  */
  if (offset == 0
      && nregs_ymode > nregs_xmode
      && (GET_MODE_SIZE (ymode) > UNITS_PER_WORD
	  ? WORDS_BIG_ENDIAN : BYTES_BIG_ENDIAN))
    return nregs_xmode - nregs_ymode;

  if (offset == 0 || nregs_xmode == nregs_ymode)
    return 0;

  /* Size of ymode must not be greater than the size of xmode.  */
  mode_multiple = GET_MODE_SIZE (xmode) / GET_MODE_SIZE (ymode);
  gcc_assert (mode_multiple != 0);

  y_offset = offset / GET_MODE_SIZE (ymode);
  nregs_multiple =  nregs_xmode / nregs_ymode;
  return (y_offset / (mode_multiple / nregs_multiple)) * nregs_ymode;
}

/* This function returns true when the offset is representable via
   subreg_offset in the given regno.
   xregno - A regno of an inner hard subreg_reg (or what will become one).
   xmode  - The mode of xregno.
   offset - The byte offset.
   ymode  - The mode of a top level SUBREG (or what may become one).
   RETURN - Whether the offset is representable.  */
bool
subreg_offset_representable_p (unsigned int xregno, enum machine_mode xmode,
			       unsigned int offset, enum machine_mode ymode)
{
  int nregs_xmode, nregs_ymode;
  int mode_multiple, nregs_multiple;
  int y_offset;
  int regsize_xmode, regsize_ymode;

  gcc_assert (xregno < FIRST_PSEUDO_REGISTER);

  /* If there are holes in a non-scalar mode in registers, we expect
     that it is made up of its units concatenated together.  */
  if (HARD_REGNO_NREGS_HAS_PADDING (xregno, xmode))
    {
      enum machine_mode xmode_unit;

      nregs_xmode = HARD_REGNO_NREGS_WITH_PADDING (xregno, xmode);
      if (GET_MODE_INNER (xmode) == VOIDmode)
	xmode_unit = xmode;
      else
	xmode_unit = GET_MODE_INNER (xmode);
      gcc_assert (HARD_REGNO_NREGS_HAS_PADDING (xregno, xmode_unit));
      gcc_assert (nregs_xmode
		  == (GET_MODE_NUNITS (xmode)
		      * HARD_REGNO_NREGS_WITH_PADDING (xregno, xmode_unit)));
      gcc_assert (hard_regno_nregs[xregno][xmode]
		  == (hard_regno_nregs[xregno][xmode_unit]
		      * GET_MODE_NUNITS (xmode)));

      /* You can only ask for a SUBREG of a value with holes in the middle
	 if you don't cross the holes.  (Such a SUBREG should be done by
	 picking a different register class, or doing it in memory if
	 necessary.)  An example of a value with holes is XCmode on 32-bit
	 x86 with -m128bit-long-double; it's represented in 6 32-bit registers,
	 3 for each part, but in memory it's two 128-bit parts.  
	 Padding is assumed to be at the end (not necessarily the 'high part')
	 of each unit.  */
      if ((offset / GET_MODE_SIZE (xmode_unit) + 1 
	   < GET_MODE_NUNITS (xmode))
	  && (offset / GET_MODE_SIZE (xmode_unit)
	      != ((offset + GET_MODE_SIZE (ymode) - 1)
		  / GET_MODE_SIZE (xmode_unit))))
	return false;
    }
  else
    nregs_xmode = hard_regno_nregs[xregno][xmode];
  
  nregs_ymode = hard_regno_nregs[xregno][ymode];

  /* Paradoxical subregs are otherwise valid.  */
  if (offset == 0
      && nregs_ymode > nregs_xmode
      && (GET_MODE_SIZE (ymode) > UNITS_PER_WORD
	  ? WORDS_BIG_ENDIAN : BYTES_BIG_ENDIAN))
    return true;

  /* If registers store different numbers of bits in the different
     modes, we cannot generally form this subreg.  */
  regsize_xmode = GET_MODE_SIZE (xmode) / nregs_xmode;
  regsize_ymode = GET_MODE_SIZE (ymode) / nregs_ymode;
  if (regsize_xmode > regsize_ymode && nregs_ymode > 1)
    return false;
  if (regsize_ymode > regsize_xmode && nregs_xmode > 1)
    return false;

  /* Lowpart subregs are otherwise valid.  */
  if (offset == subreg_lowpart_offset (ymode, xmode))
    return true;

  /* This should always pass, otherwise we don't know how to verify
     the constraint.  These conditions may be relaxed but
     subreg_regno_offset would need to be redesigned.  */
  gcc_assert ((GET_MODE_SIZE (xmode) % GET_MODE_SIZE (ymode)) == 0);
  gcc_assert ((nregs_xmode % nregs_ymode) == 0);

  /* The XMODE value can be seen as a vector of NREGS_XMODE
     values.  The subreg must represent a lowpart of given field.
     Compute what field it is.  */
  offset -= subreg_lowpart_offset (ymode,
				   mode_for_size (GET_MODE_BITSIZE (xmode)
						  / nregs_xmode,
						  MODE_INT, 0));

  /* Size of ymode must not be greater than the size of xmode.  */
  mode_multiple = GET_MODE_SIZE (xmode) / GET_MODE_SIZE (ymode);
  gcc_assert (mode_multiple != 0);

  y_offset = offset / GET_MODE_SIZE (ymode);
  nregs_multiple =  nregs_xmode / nregs_ymode;

  gcc_assert ((offset % GET_MODE_SIZE (ymode)) == 0);
  gcc_assert ((mode_multiple % nregs_multiple) == 0);

  return (!(y_offset % (mode_multiple / nregs_multiple)));
}

/* Return the final regno that a subreg expression refers to.  */
unsigned int
subreg_regno (rtx x)
{
  unsigned int ret;
  rtx subreg = SUBREG_REG (x);
  int regno = REGNO (subreg);

  ret = regno + subreg_regno_offset (regno,
				     GET_MODE (subreg),
				     SUBREG_BYTE (x),
				     GET_MODE (x));
  return ret;

}
struct parms_set_data
{
  int nregs;
  HARD_REG_SET regs;
};

/* Helper function for noticing stores to parameter registers.  */
static void
parms_set (rtx x, rtx pat ATTRIBUTE_UNUSED, void *data)
{
  struct parms_set_data *d = data;
  if (REG_P (x) && REGNO (x) < FIRST_PSEUDO_REGISTER
      && TEST_HARD_REG_BIT (d->regs, REGNO (x)))
    {
      CLEAR_HARD_REG_BIT (d->regs, REGNO (x));
      d->nregs--;
    }
}

/* Look backward for first parameter to be loaded.
   Note that loads of all parameters will not necessarily be
   found if CSE has eliminated some of them (e.g., an argument
   to the outer function is passed down as a parameter).
   Do not skip BOUNDARY.  */
rtx
find_first_parameter_load (rtx call_insn, rtx boundary)
{
  struct parms_set_data parm;
  rtx p, before, first_set;

  /* Since different machines initialize their parameter registers
     in different orders, assume nothing.  Collect the set of all
     parameter registers.  */
  CLEAR_HARD_REG_SET (parm.regs);
  parm.nregs = 0;
  for (p = CALL_INSN_FUNCTION_USAGE (call_insn); p; p = XEXP (p, 1))
    if (GET_CODE (XEXP (p, 0)) == USE
	&& REG_P (XEXP (XEXP (p, 0), 0)))
      {
	gcc_assert (REGNO (XEXP (XEXP (p, 0), 0)) < FIRST_PSEUDO_REGISTER);

	/* We only care about registers which can hold function
	   arguments.  */
	if (!FUNCTION_ARG_REGNO_P (REGNO (XEXP (XEXP (p, 0), 0))))
	  continue;

	SET_HARD_REG_BIT (parm.regs, REGNO (XEXP (XEXP (p, 0), 0)));
	parm.nregs++;
      }
  before = call_insn;
  first_set = call_insn;

  /* Search backward for the first set of a register in this set.  */
  while (parm.nregs && before != boundary)
    {
      before = PREV_INSN (before);

      /* It is possible that some loads got CSEed from one call to
         another.  Stop in that case.  */
      if (CALL_P (before))
	break;

      /* Our caller needs either ensure that we will find all sets
         (in case code has not been optimized yet), or take care
         for possible labels in a way by setting boundary to preceding
         CODE_LABEL.  */
      if (LABEL_P (before))
	{
	  gcc_assert (before == boundary);
	  break;
	}

      if (INSN_P (before))
	{
	  int nregs_old = parm.nregs;
	  note_stores (PATTERN (before), parms_set, &parm);
	  /* If we found something that did not set a parameter reg,
	     we're done.  Do not keep going, as that might result
	     in hoisting an insn before the setting of a pseudo
	     that is used by the hoisted insn. */
	  if (nregs_old != parm.nregs)
	    first_set = before;
	  else
	    break;
	}
    }
  return first_set;
}

/* Return true if we should avoid inserting code between INSN and preceding
   call instruction.  */

bool
keep_with_call_p (rtx insn)
{
  rtx set;

  if (INSN_P (insn) && (set = single_set (insn)) != NULL)
    {
      if (REG_P (SET_DEST (set))
	  && REGNO (SET_DEST (set)) < FIRST_PSEUDO_REGISTER
	  && fixed_regs[REGNO (SET_DEST (set))]
	  && general_operand (SET_SRC (set), VOIDmode))
	return true;
      if (REG_P (SET_SRC (set))
	  && FUNCTION_VALUE_REGNO_P (REGNO (SET_SRC (set)))
	  && REG_P (SET_DEST (set))
	  && REGNO (SET_DEST (set)) >= FIRST_PSEUDO_REGISTER)
	return true;
      /* There may be a stack pop just after the call and before the store
	 of the return register.  Search for the actual store when deciding
	 if we can break or not.  */
      if (SET_DEST (set) == stack_pointer_rtx)
	{
	  rtx i2 = next_nonnote_insn (insn);
	  if (i2 && keep_with_call_p (i2))
	    return true;
	}
    }
  return false;
}

/* Return true if LABEL is a target of JUMP_INSN.  This applies only
   to non-complex jumps.  That is, direct unconditional, conditional,
   and tablejumps, but not computed jumps or returns.  It also does
   not apply to the fallthru case of a conditional jump.  */

bool
label_is_jump_target_p (rtx label, rtx jump_insn)
{
  rtx tmp = JUMP_LABEL (jump_insn);

  if (label == tmp)
    return true;

  if (tablejump_p (jump_insn, NULL, &tmp))
    {
      rtvec vec = XVEC (PATTERN (tmp),
			GET_CODE (PATTERN (tmp)) == ADDR_DIFF_VEC);
      int i, veclen = GET_NUM_ELEM (vec);

      for (i = 0; i < veclen; ++i)
	if (XEXP (RTVEC_ELT (vec, i), 0) == label)
	  return true;
    }

  return false;
}


/* Return an estimate of the cost of computing rtx X.
   One use is in cse, to decide which expression to keep in the hash table.
   Another is in rtl generation, to pick the cheapest way to multiply.
   Other uses like the latter are expected in the future.  */

int
rtx_cost (rtx x, enum rtx_code outer_code ATTRIBUTE_UNUSED)
{
  int i, j;
  enum rtx_code code;
  const char *fmt;
  int total;

  if (x == 0)
    return 0;

  /* Compute the default costs of certain things.
     Note that targetm.rtx_costs can override the defaults.  */

  code = GET_CODE (x);
  switch (code)
    {
    case MULT:
      total = COSTS_N_INSNS (5);
      break;
    case DIV:
    case UDIV:
    case MOD:
    case UMOD:
      total = COSTS_N_INSNS (7);
      break;
    case USE:
      /* Used in combine.c as a marker.  */
      total = 0;
      break;
    default:
      total = COSTS_N_INSNS (1);
    }

  switch (code)
    {
    case REG:
      return 0;

    case SUBREG:
      total = 0;
      /* If we can't tie these modes, make this expensive.  The larger
	 the mode, the more expensive it is.  */
      if (! MODES_TIEABLE_P (GET_MODE (x), GET_MODE (SUBREG_REG (x))))
	return COSTS_N_INSNS (2
			      + GET_MODE_SIZE (GET_MODE (x)) / UNITS_PER_WORD);
      break;

    default:
      if (targetm.rtx_costs (x, code, outer_code, &total))
	return total;
      break;
    }

  /* Sum the costs of the sub-rtx's, plus cost of this operation,
     which is already in total.  */

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    if (fmt[i] == 'e')
      total += rtx_cost (XEXP (x, i), code);
    else if (fmt[i] == 'E')
      for (j = 0; j < XVECLEN (x, i); j++)
	total += rtx_cost (XVECEXP (x, i, j), code);

  return total;
}

/* Return cost of address expression X.
   Expect that X is properly formed address reference.  */

int
address_cost (rtx x, enum machine_mode mode)
{
  /* We may be asked for cost of various unusual addresses, such as operands
     of push instruction.  It is not worthwhile to complicate writing
     of the target hook by such cases.  */

  if (!memory_address_p (mode, x))
    return 1000;

  return targetm.address_cost (x);
}

/* If the target doesn't override, compute the cost as with arithmetic.  */

int
default_address_cost (rtx x)
{
  return rtx_cost (x, MEM);
}


unsigned HOST_WIDE_INT
nonzero_bits (rtx x, enum machine_mode mode)
{
  return cached_nonzero_bits (x, mode, NULL_RTX, VOIDmode, 0);
}

unsigned int
num_sign_bit_copies (rtx x, enum machine_mode mode)
{
  return cached_num_sign_bit_copies (x, mode, NULL_RTX, VOIDmode, 0);
}

/* The function cached_nonzero_bits is a wrapper around nonzero_bits1.
   It avoids exponential behavior in nonzero_bits1 when X has
   identical subexpressions on the first or the second level.  */

static unsigned HOST_WIDE_INT
cached_nonzero_bits (rtx x, enum machine_mode mode, rtx known_x,
		     enum machine_mode known_mode,
		     unsigned HOST_WIDE_INT known_ret)
{
  if (x == known_x && mode == known_mode)
    return known_ret;

  /* Try to find identical subexpressions.  If found call
     nonzero_bits1 on X with the subexpressions as KNOWN_X and the
     precomputed value for the subexpression as KNOWN_RET.  */

  if (ARITHMETIC_P (x))
    {
      rtx x0 = XEXP (x, 0);
      rtx x1 = XEXP (x, 1);

      /* Check the first level.  */
      if (x0 == x1)
	return nonzero_bits1 (x, mode, x0, mode,
			      cached_nonzero_bits (x0, mode, known_x,
						   known_mode, known_ret));

      /* Check the second level.  */
      if (ARITHMETIC_P (x0)
	  && (x1 == XEXP (x0, 0) || x1 == XEXP (x0, 1)))
	return nonzero_bits1 (x, mode, x1, mode,
			      cached_nonzero_bits (x1, mode, known_x,
						   known_mode, known_ret));

      if (ARITHMETIC_P (x1)
	  && (x0 == XEXP (x1, 0) || x0 == XEXP (x1, 1)))
	return nonzero_bits1 (x, mode, x0, mode,
			      cached_nonzero_bits (x0, mode, known_x,
						   known_mode, known_ret));
    }

  return nonzero_bits1 (x, mode, known_x, known_mode, known_ret);
}

/* We let num_sign_bit_copies recur into nonzero_bits as that is useful.
   We don't let nonzero_bits recur into num_sign_bit_copies, because that
   is less useful.  We can't allow both, because that results in exponential
   run time recursion.  There is a nullstone testcase that triggered
   this.  This macro avoids accidental uses of num_sign_bit_copies.  */
#define cached_num_sign_bit_copies sorry_i_am_preventing_exponential_behavior

/* Given an expression, X, compute which bits in X can be nonzero.
   We don't care about bits outside of those defined in MODE.

   For most X this is simply GET_MODE_MASK (GET_MODE (MODE)), but if X is
   an arithmetic operation, we can do better.  */

static unsigned HOST_WIDE_INT
nonzero_bits1 (rtx x, enum machine_mode mode, rtx known_x,
	       enum machine_mode known_mode,
	       unsigned HOST_WIDE_INT known_ret)
{
  unsigned HOST_WIDE_INT nonzero = GET_MODE_MASK (mode);
  unsigned HOST_WIDE_INT inner_nz;
  enum rtx_code code;
  unsigned int mode_width = GET_MODE_BITSIZE (mode);

  /* For floating-point values, assume all bits are needed.  */
  if (FLOAT_MODE_P (GET_MODE (x)) || FLOAT_MODE_P (mode))
    return nonzero;

  /* If X is wider than MODE, use its mode instead.  */
  if (GET_MODE_BITSIZE (GET_MODE (x)) > mode_width)
    {
      mode = GET_MODE (x);
      nonzero = GET_MODE_MASK (mode);
      mode_width = GET_MODE_BITSIZE (mode);
    }

  if (mode_width > HOST_BITS_PER_WIDE_INT)
    /* Our only callers in this case look for single bit values.  So
       just return the mode mask.  Those tests will then be false.  */
    return nonzero;

#ifndef WORD_REGISTER_OPERATIONS
  /* If MODE is wider than X, but both are a single word for both the host
     and target machines, we can compute this from which bits of the
     object might be nonzero in its own mode, taking into account the fact
     that on many CISC machines, accessing an object in a wider mode
     causes the high-order bits to become undefined.  So they are
     not known to be zero.  */

  if (GET_MODE (x) != VOIDmode && GET_MODE (x) != mode
      && GET_MODE_BITSIZE (GET_MODE (x)) <= BITS_PER_WORD
      && GET_MODE_BITSIZE (GET_MODE (x)) <= HOST_BITS_PER_WIDE_INT
      && GET_MODE_BITSIZE (mode) > GET_MODE_BITSIZE (GET_MODE (x)))
    {
      nonzero &= cached_nonzero_bits (x, GET_MODE (x),
				      known_x, known_mode, known_ret);
      nonzero |= GET_MODE_MASK (mode) & ~GET_MODE_MASK (GET_MODE (x));
      return nonzero;
    }
#endif

  code = GET_CODE (x);
  switch (code)
    {
    case REG:
#if defined(POINTERS_EXTEND_UNSIGNED) && !defined(HAVE_ptr_extend)
      /* If pointers extend unsigned and this is a pointer in Pmode, say that
	 all the bits above ptr_mode are known to be zero.  */
      if (POINTERS_EXTEND_UNSIGNED && GET_MODE (x) == Pmode
	  && REG_POINTER (x))
	nonzero &= GET_MODE_MASK (ptr_mode);
#endif

      /* Include declared information about alignment of pointers.  */
      /* ??? We don't properly preserve REG_POINTER changes across
	 pointer-to-integer casts, so we can't trust it except for
	 things that we know must be pointers.  See execute/960116-1.c.  */
      if ((x == stack_pointer_rtx
	   || x == frame_pointer_rtx
	   || x == arg_pointer_rtx)
	  && REGNO_POINTER_ALIGN (REGNO (x)))
	{
	  unsigned HOST_WIDE_INT alignment
	    = REGNO_POINTER_ALIGN (REGNO (x)) / BITS_PER_UNIT;

#ifdef PUSH_ROUNDING
	  /* If PUSH_ROUNDING is defined, it is possible for the
	     stack to be momentarily aligned only to that amount,
	     so we pick the least alignment.  */
	  if (x == stack_pointer_rtx && PUSH_ARGS)
	    alignment = MIN ((unsigned HOST_WIDE_INT) PUSH_ROUNDING (1),
			     alignment);
#endif

	  nonzero &= ~(alignment - 1);
	}

      {
	unsigned HOST_WIDE_INT nonzero_for_hook = nonzero;
	rtx new = rtl_hooks.reg_nonzero_bits (x, mode, known_x,
					      known_mode, known_ret,
					      &nonzero_for_hook);

	if (new)
	  nonzero_for_hook &= cached_nonzero_bits (new, mode, known_x,
						   known_mode, known_ret);

	return nonzero_for_hook;
      }

    case CONST_INT:
#ifdef SHORT_IMMEDIATES_SIGN_EXTEND
      /* If X is negative in MODE, sign-extend the value.  */
      if (INTVAL (x) > 0 && mode_width < BITS_PER_WORD
	  && 0 != (INTVAL (x) & ((HOST_WIDE_INT) 1 << (mode_width - 1))))
	return (INTVAL (x) | ((HOST_WIDE_INT) (-1) << mode_width));
#endif

      return INTVAL (x);

    case MEM:
#ifdef LOAD_EXTEND_OP
      /* In many, if not most, RISC machines, reading a byte from memory
	 zeros the rest of the register.  Noticing that fact saves a lot
	 of extra zero-extends.  */
      if (LOAD_EXTEND_OP (GET_MODE (x)) == ZERO_EXTEND)
	nonzero &= GET_MODE_MASK (GET_MODE (x));
#endif
      break;

    case EQ:  case NE:
    case UNEQ:  case LTGT:
    case GT:  case GTU:  case UNGT:
    case LT:  case LTU:  case UNLT:
    case GE:  case GEU:  case UNGE:
    case LE:  case LEU:  case UNLE:
    case UNORDERED: case ORDERED:
      /* If this produces an integer result, we know which bits are set.
	 Code here used to clear bits outside the mode of X, but that is
	 now done above.  */
      /* Mind that MODE is the mode the caller wants to look at this 
	 operation in, and not the actual operation mode.  We can wind 
	 up with (subreg:DI (gt:V4HI x y)), and we don't have anything
	 that describes the results of a vector compare.  */
      if (GET_MODE_CLASS (GET_MODE (x)) == MODE_INT
	  && mode_width <= HOST_BITS_PER_WIDE_INT)
	nonzero = STORE_FLAG_VALUE;
      break;

    case NEG:
#if 0
      /* Disabled to avoid exponential mutual recursion between nonzero_bits
	 and num_sign_bit_copies.  */
      if (num_sign_bit_copies (XEXP (x, 0), GET_MODE (x))
	  == GET_MODE_BITSIZE (GET_MODE (x)))
	nonzero = 1;
#endif

      if (GET_MODE_SIZE (GET_MODE (x)) < mode_width)
	nonzero |= (GET_MODE_MASK (mode) & ~GET_MODE_MASK (GET_MODE (x)));
      break;

    case ABS:
#if 0
      /* Disabled to avoid exponential mutual recursion between nonzero_bits
	 and num_sign_bit_copies.  */
      if (num_sign_bit_copies (XEXP (x, 0), GET_MODE (x))
	  == GET_MODE_BITSIZE (GET_MODE (x)))
	nonzero = 1;
#endif
      break;

    case TRUNCATE:
      nonzero &= (cached_nonzero_bits (XEXP (x, 0), mode,
				       known_x, known_mode, known_ret)
		  & GET_MODE_MASK (mode));
      break;

    case ZERO_EXTEND:
      nonzero &= cached_nonzero_bits (XEXP (x, 0), mode,
				      known_x, known_mode, known_ret);
      if (GET_MODE (XEXP (x, 0)) != VOIDmode)
	nonzero &= GET_MODE_MASK (GET_MODE (XEXP (x, 0)));
      break;

    case SIGN_EXTEND:
      /* If the sign bit is known clear, this is the same as ZERO_EXTEND.
	 Otherwise, show all the bits in the outer mode but not the inner
	 may be nonzero.  */
      inner_nz = cached_nonzero_bits (XEXP (x, 0), mode,
				      known_x, known_mode, known_ret);
      if (GET_MODE (XEXP (x, 0)) != VOIDmode)
	{
	  inner_nz &= GET_MODE_MASK (GET_MODE (XEXP (x, 0)));
	  if (inner_nz
	      & (((HOST_WIDE_INT) 1
		  << (GET_MODE_BITSIZE (GET_MODE (XEXP (x, 0))) - 1))))
	    inner_nz |= (GET_MODE_MASK (mode)
			 & ~GET_MODE_MASK (GET_MODE (XEXP (x, 0))));
	}

      nonzero &= inner_nz;
      break;

    case AND:
      nonzero &= cached_nonzero_bits (XEXP (x, 0), mode,
				       known_x, known_mode, known_ret)
      		 & cached_nonzero_bits (XEXP (x, 1), mode,
					known_x, known_mode, known_ret);
      break;

    case XOR:   case IOR:
    case UMIN:  case UMAX:  case SMIN:  case SMAX:
      {
	unsigned HOST_WIDE_INT nonzero0 =
	  cached_nonzero_bits (XEXP (x, 0), mode,
			       known_x, known_mode, known_ret);

	/* Don't call nonzero_bits for the second time if it cannot change
	   anything.  */
	if ((nonzero & nonzero0) != nonzero)
	  nonzero &= nonzero0
      		     | cached_nonzero_bits (XEXP (x, 1), mode,
					    known_x, known_mode, known_ret);
      }
      break;

    case PLUS:  case MINUS:
    case MULT:
    case DIV:   case UDIV:
    case MOD:   case UMOD:
      /* We can apply the rules of arithmetic to compute the number of
	 high- and low-order zero bits of these operations.  We start by
	 computing the width (position of the highest-order nonzero bit)
	 and the number of low-order zero bits for each value.  */
      {
	unsigned HOST_WIDE_INT nz0 =
	  cached_nonzero_bits (XEXP (x, 0), mode,
			       known_x, known_mode, known_ret);
	unsigned HOST_WIDE_INT nz1 =
	  cached_nonzero_bits (XEXP (x, 1), mode,
			       known_x, known_mode, known_ret);
	int sign_index = GET_MODE_BITSIZE (GET_MODE (x)) - 1;
	int width0 = floor_log2 (nz0) + 1;
	int width1 = floor_log2 (nz1) + 1;
	int low0 = floor_log2 (nz0 & -nz0);
	int low1 = floor_log2 (nz1 & -nz1);
	HOST_WIDE_INT op0_maybe_minusp
	  = (nz0 & ((HOST_WIDE_INT) 1 << sign_index));
	HOST_WIDE_INT op1_maybe_minusp
	  = (nz1 & ((HOST_WIDE_INT) 1 << sign_index));
	unsigned int result_width = mode_width;
	int result_low = 0;

	switch (code)
	  {
	  case PLUS:
	    result_width = MAX (width0, width1) + 1;
	    result_low = MIN (low0, low1);
	    break;
	  case MINUS:
	    result_low = MIN (low0, low1);
	    break;
	  case MULT:
	    result_width = width0 + width1;
	    result_low = low0 + low1;
	    break;
	  case DIV:
	    if (width1 == 0)
	      break;
	    if (! op0_maybe_minusp && ! op1_maybe_minusp)
	      result_width = width0;
	    break;
	  case UDIV:
	    if (width1 == 0)
	      break;
	    result_width = width0;
	    break;
	  case MOD:
	    if (width1 == 0)
	      break;
	    if (! op0_maybe_minusp && ! op1_maybe_minusp)
	      result_width = MIN (width0, width1);
	    result_low = MIN (low0, low1);
	    break;
	  case UMOD:
	    if (width1 == 0)
	      break;
	    result_width = MIN (width0, width1);
	    result_low = MIN (low0, low1);
	    break;
	  default:
	    gcc_unreachable ();
	  }

	if (result_width < mode_width)
	  nonzero &= ((HOST_WIDE_INT) 1 << result_width) - 1;

	if (result_low > 0)
	  nonzero &= ~(((HOST_WIDE_INT) 1 << result_low) - 1);

#ifdef POINTERS_EXTEND_UNSIGNED
	/* If pointers extend unsigned and this is an addition or subtraction
	   to a pointer in Pmode, all the bits above ptr_mode are known to be
	   zero.  */
	if (POINTERS_EXTEND_UNSIGNED > 0 && GET_MODE (x) == Pmode
	    && (code == PLUS || code == MINUS)
	    && REG_P (XEXP (x, 0)) && REG_POINTER (XEXP (x, 0)))
	  nonzero &= GET_MODE_MASK (ptr_mode);
#endif
      }
      break;

    case ZERO_EXTRACT:
      if (GET_CODE (XEXP (x, 1)) == CONST_INT
	  && INTVAL (XEXP (x, 1)) < HOST_BITS_PER_WIDE_INT)
	nonzero &= ((HOST_WIDE_INT) 1 << INTVAL (XEXP (x, 1))) - 1;
      break;

    case SUBREG:
      /* If this is a SUBREG formed for a promoted variable that has
	 been zero-extended, we know that at least the high-order bits
	 are zero, though others might be too.  */

      if (SUBREG_PROMOTED_VAR_P (x) && SUBREG_PROMOTED_UNSIGNED_P (x) > 0)
	nonzero = GET_MODE_MASK (GET_MODE (x))
		  & cached_nonzero_bits (SUBREG_REG (x), GET_MODE (x),
					 known_x, known_mode, known_ret);

      /* If the inner mode is a single word for both the host and target
	 machines, we can compute this from which bits of the inner
	 object might be nonzero.  */
      if (GET_MODE_BITSIZE (GET_MODE (SUBREG_REG (x))) <= BITS_PER_WORD
	  && (GET_MODE_BITSIZE (GET_MODE (SUBREG_REG (x)))
	      <= HOST_BITS_PER_WIDE_INT))
	{
	  nonzero &= cached_nonzero_bits (SUBREG_REG (x), mode,
					  known_x, known_mode, known_ret);

#if defined (WORD_REGISTER_OPERATIONS) && defined (LOAD_EXTEND_OP)
	  /* If this is a typical RISC machine, we only have to worry
	     about the way loads are extended.  */
	  if ((LOAD_EXTEND_OP (GET_MODE (SUBREG_REG (x))) == SIGN_EXTEND
	       ? (((nonzero
		    & (((unsigned HOST_WIDE_INT) 1
			<< (GET_MODE_BITSIZE (GET_MODE (SUBREG_REG (x))) - 1))))
		   != 0))
	       : LOAD_EXTEND_OP (GET_MODE (SUBREG_REG (x))) != ZERO_EXTEND)
	      || !MEM_P (SUBREG_REG (x)))
#endif
	    {
	      /* On many CISC machines, accessing an object in a wider mode
		 causes the high-order bits to become undefined.  So they are
		 not known to be zero.  */
	      if (GET_MODE_SIZE (GET_MODE (x))
		  > GET_MODE_SIZE (GET_MODE (SUBREG_REG (x))))
		nonzero |= (GET_MODE_MASK (GET_MODE (x))
			    & ~GET_MODE_MASK (GET_MODE (SUBREG_REG (x))));
	    }
	}
      break;

    case ASHIFTRT:
    case LSHIFTRT:
    case ASHIFT:
    case ROTATE:
      /* The nonzero bits are in two classes: any bits within MODE
	 that aren't in GET_MODE (x) are always significant.  The rest of the
	 nonzero bits are those that are significant in the operand of
	 the shift when shifted the appropriate number of bits.  This
	 shows that high-order bits are cleared by the right shift and
	 low-order bits by left shifts.  */
      if (GET_CODE (XEXP (x, 1)) == CONST_INT
	  && INTVAL (XEXP (x, 1)) >= 0
	  && INTVAL (XEXP (x, 1)) < HOST_BITS_PER_WIDE_INT)
	{
	  enum machine_mode inner_mode = GET_MODE (x);
	  unsigned int width = GET_MODE_BITSIZE (inner_mode);
	  int count = INTVAL (XEXP (x, 1));
	  unsigned HOST_WIDE_INT mode_mask = GET_MODE_MASK (inner_mode);
	  unsigned HOST_WIDE_INT op_nonzero =
	    cached_nonzero_bits (XEXP (x, 0), mode,
				 known_x, known_mode, known_ret);
	  unsigned HOST_WIDE_INT inner = op_nonzero & mode_mask;
	  unsigned HOST_WIDE_INT outer = 0;

	  if (mode_width > width)
	    outer = (op_nonzero & nonzero & ~mode_mask);

	  if (code == LSHIFTRT)
	    inner >>= count;
	  else if (code == ASHIFTRT)
	    {
	      inner >>= count;

	      /* If the sign bit may have been nonzero before the shift, we
		 need to mark all the places it could have been copied to
		 by the shift as possibly nonzero.  */
	      if (inner & ((HOST_WIDE_INT) 1 << (width - 1 - count)))
		inner |= (((HOST_WIDE_INT) 1 << count) - 1) << (width - count);
	    }
	  else if (code == ASHIFT)
	    inner <<= count;
	  else
	    inner = ((inner << (count % width)
		      | (inner >> (width - (count % width)))) & mode_mask);

	  nonzero &= (outer | inner);
	}
      break;

    case FFS:
    case POPCOUNT:
      /* This is at most the number of bits in the mode.  */
      nonzero = ((HOST_WIDE_INT) 2 << (floor_log2 (mode_width))) - 1;
      break;

    case CLZ:
      /* If CLZ has a known value at zero, then the nonzero bits are
	 that value, plus the number of bits in the mode minus one.  */
      if (CLZ_DEFINED_VALUE_AT_ZERO (mode, nonzero))
	nonzero |= ((HOST_WIDE_INT) 1 << (floor_log2 (mode_width))) - 1;
      else
	nonzero = -1;
      break;

    case CTZ:
      /* If CTZ has a known value at zero, then the nonzero bits are
	 that value, plus the number of bits in the mode minus one.  */
      if (CTZ_DEFINED_VALUE_AT_ZERO (mode, nonzero))
	nonzero |= ((HOST_WIDE_INT) 1 << (floor_log2 (mode_width))) - 1;
      else
	nonzero = -1;
      break;

    case PARITY:
      nonzero = 1;
      break;

    case IF_THEN_ELSE:
      {
	unsigned HOST_WIDE_INT nonzero_true =
	  cached_nonzero_bits (XEXP (x, 1), mode,
			       known_x, known_mode, known_ret);

	/* Don't call nonzero_bits for the second time if it cannot change
	   anything.  */
	if ((nonzero & nonzero_true) != nonzero)
	  nonzero &= nonzero_true
      		     | cached_nonzero_bits (XEXP (x, 2), mode,
					    known_x, known_mode, known_ret);
      }
      break;

    default:
      break;
    }

  return nonzero;
}

/* See the macro definition above.  */
#undef cached_num_sign_bit_copies


/* The function cached_num_sign_bit_copies is a wrapper around
   num_sign_bit_copies1.  It avoids exponential behavior in
   num_sign_bit_copies1 when X has identical subexpressions on the
   first or the second level.  */

static unsigned int
cached_num_sign_bit_copies (rtx x, enum machine_mode mode, rtx known_x,
			    enum machine_mode known_mode,
			    unsigned int known_ret)
{
  if (x == known_x && mode == known_mode)
    return known_ret;

  /* Try to find identical subexpressions.  If found call
     num_sign_bit_copies1 on X with the subexpressions as KNOWN_X and
     the precomputed value for the subexpression as KNOWN_RET.  */

  if (ARITHMETIC_P (x))
    {
      rtx x0 = XEXP (x, 0);
      rtx x1 = XEXP (x, 1);

      /* Check the first level.  */
      if (x0 == x1)
	return
	  num_sign_bit_copies1 (x, mode, x0, mode,
				cached_num_sign_bit_copies (x0, mode, known_x,
							    known_mode,
							    known_ret));

      /* Check the second level.  */
      if (ARITHMETIC_P (x0)
	  && (x1 == XEXP (x0, 0) || x1 == XEXP (x0, 1)))
	return
	  num_sign_bit_copies1 (x, mode, x1, mode,
				cached_num_sign_bit_copies (x1, mode, known_x,
							    known_mode,
							    known_ret));

      if (ARITHMETIC_P (x1)
	  && (x0 == XEXP (x1, 0) || x0 == XEXP (x1, 1)))
	return
	  num_sign_bit_copies1 (x, mode, x0, mode,
				cached_num_sign_bit_copies (x0, mode, known_x,
							    known_mode,
							    known_ret));
    }

  return num_sign_bit_copies1 (x, mode, known_x, known_mode, known_ret);
}

/* Return the number of bits at the high-order end of X that are known to
   be equal to the sign bit.  X will be used in mode MODE; if MODE is
   VOIDmode, X will be used in its own mode.  The returned value  will always
   be between 1 and the number of bits in MODE.  */

static unsigned int
num_sign_bit_copies1 (rtx x, enum machine_mode mode, rtx known_x,
		      enum machine_mode known_mode,
		      unsigned int known_ret)
{
  enum rtx_code code = GET_CODE (x);
  unsigned int bitwidth = GET_MODE_BITSIZE (mode);
  int num0, num1, result;
  unsigned HOST_WIDE_INT nonzero;

  /* If we weren't given a mode, use the mode of X.  If the mode is still
     VOIDmode, we don't know anything.  Likewise if one of the modes is
     floating-point.  */

  if (mode == VOIDmode)
    mode = GET_MODE (x);

  if (mode == VOIDmode || FLOAT_MODE_P (mode) || FLOAT_MODE_P (GET_MODE (x)))
    return 1;

  /* For a smaller object, just ignore the high bits.  */
  if (bitwidth < GET_MODE_BITSIZE (GET_MODE (x)))
    {
      num0 = cached_num_sign_bit_copies (x, GET_MODE (x),
					 known_x, known_mode, known_ret);
      return MAX (1,
		  num0 - (int) (GET_MODE_BITSIZE (GET_MODE (x)) - bitwidth));
    }

  if (GET_MODE (x) != VOIDmode && bitwidth > GET_MODE_BITSIZE (GET_MODE (x)))
    {
#ifndef WORD_REGISTER_OPERATIONS
  /* If this machine does not do all register operations on the entire
     register and MODE is wider than the mode of X, we can say nothing
     at all about the high-order bits.  */
      return 1;
#else
      /* Likewise on machines that do, if the mode of the object is smaller
	 than a word and loads of that size don't sign extend, we can say
	 nothing about the high order bits.  */
      if (GET_MODE_BITSIZE (GET_MODE (x)) < BITS_PER_WORD
#ifdef LOAD_EXTEND_OP
	  && LOAD_EXTEND_OP (GET_MODE (x)) != SIGN_EXTEND
#endif
	  )
	return 1;
#endif
    }

  switch (code)
    {
    case REG:

#if defined(POINTERS_EXTEND_UNSIGNED) && !defined(HAVE_ptr_extend)
      /* If pointers extend signed and this is a pointer in Pmode, say that
	 all the bits above ptr_mode are known to be sign bit copies.  */
      if (! POINTERS_EXTEND_UNSIGNED && GET_MODE (x) == Pmode && mode == Pmode
	  && REG_POINTER (x))
	return GET_MODE_BITSIZE (Pmode) - GET_MODE_BITSIZE (ptr_mode) + 1;
#endif

      {
	unsigned int copies_for_hook = 1, copies = 1;
	rtx new = rtl_hooks.reg_num_sign_bit_copies (x, mode, known_x,
						     known_mode, known_ret,
						     &copies_for_hook);

	if (new)
	  copies = cached_num_sign_bit_copies (new, mode, known_x,
					       known_mode, known_ret);

	if (copies > 1 || copies_for_hook > 1)
	  return MAX (copies, copies_for_hook);

	/* Else, use nonzero_bits to guess num_sign_bit_copies (see below).  */
      }
      break;

    case MEM:
#ifdef LOAD_EXTEND_OP
      /* Some RISC machines sign-extend all loads of smaller than a word.  */
      if (LOAD_EXTEND_OP (GET_MODE (x)) == SIGN_EXTEND)
	return MAX (1, ((int) bitwidth
			- (int) GET_MODE_BITSIZE (GET_MODE (x)) + 1));
#endif
      break;

    case CONST_INT:
      /* If the constant is negative, take its 1's complement and remask.
	 Then see how many zero bits we have.  */
      nonzero = INTVAL (x) & GET_MODE_MASK (mode);
      if (bitwidth <= HOST_BITS_PER_WIDE_INT
	  && (nonzero & ((HOST_WIDE_INT) 1 << (bitwidth - 1))) != 0)
	nonzero = (~nonzero) & GET_MODE_MASK (mode);

      return (nonzero == 0 ? bitwidth : bitwidth - floor_log2 (nonzero) - 1);

    case SUBREG:
      /* If this is a SUBREG for a promoted object that is sign-extended
	 and we are looking at it in a wider mode, we know that at least the
	 high-order bits are known to be sign bit copies.  */

      if (SUBREG_PROMOTED_VAR_P (x) && ! SUBREG_PROMOTED_UNSIGNED_P (x))
	{
	  num0 = cached_num_sign_bit_copies (SUBREG_REG (x), mode,
					     known_x, known_mode, known_ret);
	  return MAX ((int) bitwidth
		      - (int) GET_MODE_BITSIZE (GET_MODE (x)) + 1,
		      num0);
	}

      /* For a smaller object, just ignore the high bits.  */
      if (bitwidth <= GET_MODE_BITSIZE (GET_MODE (SUBREG_REG (x))))
	{
	  num0 = cached_num_sign_bit_copies (SUBREG_REG (x), VOIDmode,
					     known_x, known_mode, known_ret);
	  return MAX (1, (num0
			  - (int) (GET_MODE_BITSIZE (GET_MODE (SUBREG_REG (x)))
				   - bitwidth)));
	}

#ifdef WORD_REGISTER_OPERATIONS
#ifdef LOAD_EXTEND_OP
      /* For paradoxical SUBREGs on machines where all register operations
	 affect the entire register, just look inside.  Note that we are
	 passing MODE to the recursive call, so the number of sign bit copies
	 will remain relative to that mode, not the inner mode.  */

      /* This works only if loads sign extend.  Otherwise, if we get a
	 reload for the inner part, it may be loaded from the stack, and
	 then we lose all sign bit copies that existed before the store
	 to the stack.  */

      if ((GET_MODE_SIZE (GET_MODE (x))
	   > GET_MODE_SIZE (GET_MODE (SUBREG_REG (x))))
	  && LOAD_EXTEND_OP (GET_MODE (SUBREG_REG (x))) == SIGN_EXTEND
	  && MEM_P (SUBREG_REG (x)))
	return cached_num_sign_bit_copies (SUBREG_REG (x), mode,
					   known_x, known_mode, known_ret);
#endif
#endif
      break;

    case SIGN_EXTRACT:
      if (GET_CODE (XEXP (x, 1)) == CONST_INT)
	return MAX (1, (int) bitwidth - INTVAL (XEXP (x, 1)));
      break;

    case SIGN_EXTEND:
      return (bitwidth - GET_MODE_BITSIZE (GET_MODE (XEXP (x, 0)))
	      + cached_num_sign_bit_copies (XEXP (x, 0), VOIDmode,
					    known_x, known_mode, known_ret));

    case TRUNCATE:
      /* For a smaller object, just ignore the high bits.  */
      num0 = cached_num_sign_bit_copies (XEXP (x, 0), VOIDmode,
					 known_x, known_mode, known_ret);
      return MAX (1, (num0 - (int) (GET_MODE_BITSIZE (GET_MODE (XEXP (x, 0)))
				    - bitwidth)));

    case NOT:
      return cached_num_sign_bit_copies (XEXP (x, 0), mode,
					 known_x, known_mode, known_ret);

    case ROTATE:       case ROTATERT:
      /* If we are rotating left by a number of bits less than the number
	 of sign bit copies, we can just subtract that amount from the
	 number.  */
      if (GET_CODE (XEXP (x, 1)) == CONST_INT
	  && INTVAL (XEXP (x, 1)) >= 0
	  && INTVAL (XEXP (x, 1)) < (int) bitwidth)
	{
	  num0 = cached_num_sign_bit_copies (XEXP (x, 0), mode,
					     known_x, known_mode, known_ret);
	  return MAX (1, num0 - (code == ROTATE ? INTVAL (XEXP (x, 1))
				 : (int) bitwidth - INTVAL (XEXP (x, 1))));
	}
      break;

    case NEG:
      /* In general, this subtracts one sign bit copy.  But if the value
	 is known to be positive, the number of sign bit copies is the
	 same as that of the input.  Finally, if the input has just one bit
	 that might be nonzero, all the bits are copies of the sign bit.  */
      num0 = cached_num_sign_bit_copies (XEXP (x, 0), mode,
					 known_x, known_mode, known_ret);
      if (bitwidth > HOST_BITS_PER_WIDE_INT)
	return num0 > 1 ? num0 - 1 : 1;

      nonzero = nonzero_bits (XEXP (x, 0), mode);
      if (nonzero == 1)
	return bitwidth;

      if (num0 > 1
	  && (((HOST_WIDE_INT) 1 << (bitwidth - 1)) & nonzero))
	num0--;

      return num0;

    case IOR:   case AND:   case XOR:
    case SMIN:  case SMAX:  case UMIN:  case UMAX:
      /* Logical operations will preserve the number of sign-bit copies.
	 MIN and MAX operations always return one of the operands.  */
      num0 = cached_num_sign_bit_copies (XEXP (x, 0), mode,
					 known_x, known_mode, known_ret);
      num1 = cached_num_sign_bit_copies (XEXP (x, 1), mode,
					 known_x, known_mode, known_ret);
      return MIN (num0, num1);

    case PLUS:  case MINUS:
      /* For addition and subtraction, we can have a 1-bit carry.  However,
	 if we are subtracting 1 from a positive number, there will not
	 be such a carry.  Furthermore, if the positive number is known to
	 be 0 or 1, we know the result is either -1 or 0.  */

      if (code == PLUS && XEXP (x, 1) == constm1_rtx
	  && bitwidth <= HOST_BITS_PER_WIDE_INT)
	{
	  nonzero = nonzero_bits (XEXP (x, 0), mode);
	  if ((((HOST_WIDE_INT) 1 << (bitwidth - 1)) & nonzero) == 0)
	    return (nonzero == 1 || nonzero == 0 ? bitwidth
		    : bitwidth - floor_log2 (nonzero) - 1);
	}

      num0 = cached_num_sign_bit_copies (XEXP (x, 0), mode,
					 known_x, known_mode, known_ret);
      num1 = cached_num_sign_bit_copies (XEXP (x, 1), mode,
					 known_x, known_mode, known_ret);
      result = MAX (1, MIN (num0, num1) - 1);

#ifdef POINTERS_EXTEND_UNSIGNED
      /* If pointers extend signed and this is an addition or subtraction
	 to a pointer in Pmode, all the bits above ptr_mode are known to be
	 sign bit copies.  */
      if (! POINTERS_EXTEND_UNSIGNED && GET_MODE (x) == Pmode
	  && (code == PLUS || code == MINUS)
	  && REG_P (XEXP (x, 0)) && REG_POINTER (XEXP (x, 0)))
	result = MAX ((int) (GET_MODE_BITSIZE (Pmode)
			     - GET_MODE_BITSIZE (ptr_mode) + 1),
		      result);
#endif
      return result;

    case MULT:
      /* The number of bits of the product is the sum of the number of
	 bits of both terms.  However, unless one of the terms if known
	 to be positive, we must allow for an additional bit since negating
	 a negative number can remove one sign bit copy.  */

      num0 = cached_num_sign_bit_copies (XEXP (x, 0), mode,
					 known_x, known_mode, known_ret);
      num1 = cached_num_sign_bit_copies (XEXP (x, 1), mode,
					 known_x, known_mode, known_ret);

      result = bitwidth - (bitwidth - num0) - (bitwidth - num1);
      if (result > 0
	  && (bitwidth > HOST_BITS_PER_WIDE_INT
	      || (((nonzero_bits (XEXP (x, 0), mode)
		    & ((HOST_WIDE_INT) 1 << (bitwidth - 1))) != 0)
		  && ((nonzero_bits (XEXP (x, 1), mode)
		       & ((HOST_WIDE_INT) 1 << (bitwidth - 1))) != 0))))
	result--;

      return MAX (1, result);

    case UDIV:
      /* The result must be <= the first operand.  If the first operand
	 has the high bit set, we know nothing about the number of sign
	 bit copies.  */
      if (bitwidth > HOST_BITS_PER_WIDE_INT)
	return 1;
      else if ((nonzero_bits (XEXP (x, 0), mode)
		& ((HOST_WIDE_INT) 1 << (bitwidth - 1))) != 0)
	return 1;
      else
	return cached_num_sign_bit_copies (XEXP (x, 0), mode,
					   known_x, known_mode, known_ret);

    case UMOD:
      /* The result must be <= the second operand.  */
      return cached_num_sign_bit_copies (XEXP (x, 1), mode,
					   known_x, known_mode, known_ret);

    case DIV:
      /* Similar to unsigned division, except that we have to worry about
	 the case where the divisor is negative, in which case we have
	 to add 1.  */
      result = cached_num_sign_bit_copies (XEXP (x, 0), mode,
					   known_x, known_mode, known_ret);
      if (result > 1
	  && (bitwidth > HOST_BITS_PER_WIDE_INT
	      || (nonzero_bits (XEXP (x, 1), mode)
		  & ((HOST_WIDE_INT) 1 << (bitwidth - 1))) != 0))
	result--;

      return result;

    case MOD:
      result = cached_num_sign_bit_copies (XEXP (x, 1), mode,
					   known_x, known_mode, known_ret);
      if (result > 1
	  && (bitwidth > HOST_BITS_PER_WIDE_INT
	      || (nonzero_bits (XEXP (x, 1), mode)
		  & ((HOST_WIDE_INT) 1 << (bitwidth - 1))) != 0))
	result--;

      return result;

    case ASHIFTRT:
      /* Shifts by a constant add to the number of bits equal to the
	 sign bit.  */
      num0 = cached_num_sign_bit_copies (XEXP (x, 0), mode,
					 known_x, known_mode, known_ret);
      if (GET_CODE (XEXP (x, 1)) == CONST_INT
	  && INTVAL (XEXP (x, 1)) > 0)
	num0 = MIN ((int) bitwidth, num0 + INTVAL (XEXP (x, 1)));

      return num0;

    case ASHIFT:
      /* Left shifts destroy copies.  */
      if (GET_CODE (XEXP (x, 1)) != CONST_INT
	  || INTVAL (XEXP (x, 1)) < 0
	  || INTVAL (XEXP (x, 1)) >= (int) bitwidth)
	return 1;

      num0 = cached_num_sign_bit_copies (XEXP (x, 0), mode,
					 known_x, known_mode, known_ret);
      return MAX (1, num0 - INTVAL (XEXP (x, 1)));

    case IF_THEN_ELSE:
      num0 = cached_num_sign_bit_copies (XEXP (x, 1), mode,
					 known_x, known_mode, known_ret);
      num1 = cached_num_sign_bit_copies (XEXP (x, 2), mode,
					 known_x, known_mode, known_ret);
      return MIN (num0, num1);

    case EQ:  case NE:  case GE:  case GT:  case LE:  case LT:
    case UNEQ:  case LTGT:  case UNGE:  case UNGT:  case UNLE:  case UNLT:
    case GEU: case GTU: case LEU: case LTU:
    case UNORDERED: case ORDERED:
      /* If the constant is negative, take its 1's complement and remask.
	 Then see how many zero bits we have.  */
      nonzero = STORE_FLAG_VALUE;
      if (bitwidth <= HOST_BITS_PER_WIDE_INT
	  && (nonzero & ((HOST_WIDE_INT) 1 << (bitwidth - 1))) != 0)
	nonzero = (~nonzero) & GET_MODE_MASK (mode);

      return (nonzero == 0 ? bitwidth : bitwidth - floor_log2 (nonzero) - 1);

    default:
      break;
    }

  /* If we haven't been able to figure it out by one of the above rules,
     see if some of the high-order bits are known to be zero.  If so,
     count those bits and return one less than that amount.  If we can't
     safely compute the mask for this mode, always return BITWIDTH.  */

  bitwidth = GET_MODE_BITSIZE (mode);
  if (bitwidth > HOST_BITS_PER_WIDE_INT)
    return 1;

  nonzero = nonzero_bits (x, mode);
  return nonzero & ((HOST_WIDE_INT) 1 << (bitwidth - 1))
	 ? 1 : bitwidth - floor_log2 (nonzero) - 1;
}

/* Calculate the rtx_cost of a single instruction.  A return value of
   zero indicates an instruction pattern without a known cost.  */

int
insn_rtx_cost (rtx pat)
{
  int i, cost;
  rtx set;

  /* Extract the single set rtx from the instruction pattern.
     We can't use single_set since we only have the pattern.  */
  if (GET_CODE (pat) == SET)
    set = pat;
  else if (GET_CODE (pat) == PARALLEL)
    {
      set = NULL_RTX;
      for (i = 0; i < XVECLEN (pat, 0); i++)
	{
	  rtx x = XVECEXP (pat, 0, i);
	  if (GET_CODE (x) == SET)
	    {
	      if (set)
		return 0;
	      set = x;
	    }
	}
      if (!set)
	return 0;
    }
  else
    return 0;

  cost = rtx_cost (SET_SRC (set), SET);
  return cost > 0 ? cost : COSTS_N_INSNS (1);
}

/* Given an insn INSN and condition COND, return the condition in a
   canonical form to simplify testing by callers.  Specifically:

   (1) The code will always be a comparison operation (EQ, NE, GT, etc.).
   (2) Both operands will be machine operands; (cc0) will have been replaced.
   (3) If an operand is a constant, it will be the second operand.
   (4) (LE x const) will be replaced with (LT x <const+1>) and similarly
       for GE, GEU, and LEU.

   If the condition cannot be understood, or is an inequality floating-point
   comparison which needs to be reversed, 0 will be returned.

   If REVERSE is nonzero, then reverse the condition prior to canonizing it.

   If EARLIEST is nonzero, it is a pointer to a place where the earliest
   insn used in locating the condition was found.  If a replacement test
   of the condition is desired, it should be placed in front of that
   insn and we will be sure that the inputs are still valid.

   If WANT_REG is nonzero, we wish the condition to be relative to that
   register, if possible.  Therefore, do not canonicalize the condition
   further.  If ALLOW_CC_MODE is nonzero, allow the condition returned 
   to be a compare to a CC mode register.

   If VALID_AT_INSN_P, the condition must be valid at both *EARLIEST
   and at INSN.  */

rtx
canonicalize_condition (rtx insn, rtx cond, int reverse, rtx *earliest,
			rtx want_reg, int allow_cc_mode, int valid_at_insn_p)
{
  enum rtx_code code;
  rtx prev = insn;
  rtx set;
  rtx tem;
  rtx op0, op1;
  int reverse_code = 0;
  enum machine_mode mode;
  basic_block bb = BLOCK_FOR_INSN (insn);

  code = GET_CODE (cond);
  mode = GET_MODE (cond);
  op0 = XEXP (cond, 0);
  op1 = XEXP (cond, 1);

  if (reverse)
    code = reversed_comparison_code (cond, insn);
  if (code == UNKNOWN)
    return 0;

  if (earliest)
    *earliest = insn;

  /* If we are comparing a register with zero, see if the register is set
     in the previous insn to a COMPARE or a comparison operation.  Perform
     the same tests as a function of STORE_FLAG_VALUE as find_comparison_args
     in cse.c  */

  while ((GET_RTX_CLASS (code) == RTX_COMPARE
	  || GET_RTX_CLASS (code) == RTX_COMM_COMPARE)
	 && op1 == CONST0_RTX (GET_MODE (op0))
	 && op0 != want_reg)
    {
      /* Set nonzero when we find something of interest.  */
      rtx x = 0;

#ifdef HAVE_cc0
      /* If comparison with cc0, import actual comparison from compare
	 insn.  */
      if (op0 == cc0_rtx)
	{
	  if ((prev = prev_nonnote_insn (prev)) == 0
	      || !NONJUMP_INSN_P (prev)
	      || (set = single_set (prev)) == 0
	      || SET_DEST (set) != cc0_rtx)
	    return 0;

	  op0 = SET_SRC (set);
	  op1 = CONST0_RTX (GET_MODE (op0));
	  if (earliest)
	    *earliest = prev;
	}
#endif

      /* If this is a COMPARE, pick up the two things being compared.  */
      if (GET_CODE (op0) == COMPARE)
	{
	  op1 = XEXP (op0, 1);
	  op0 = XEXP (op0, 0);
	  continue;
	}
      else if (!REG_P (op0))
	break;

      /* Go back to the previous insn.  Stop if it is not an INSN.  We also
	 stop if it isn't a single set or if it has a REG_INC note because
	 we don't want to bother dealing with it.  */

      if ((prev = prev_nonnote_insn (prev)) == 0
	  || !NONJUMP_INSN_P (prev)
	  || FIND_REG_INC_NOTE (prev, NULL_RTX)
	  /* In cfglayout mode, there do not have to be labels at the
	     beginning of a block, or jumps at the end, so the previous
	     conditions would not stop us when we reach bb boundary.  */
	  || BLOCK_FOR_INSN (prev) != bb)
	break;

      set = set_of (op0, prev);

      if (set
	  && (GET_CODE (set) != SET
	      || !rtx_equal_p (SET_DEST (set), op0)))
	break;

      /* If this is setting OP0, get what it sets it to if it looks
	 relevant.  */
      if (set)
	{
	  enum machine_mode inner_mode = GET_MODE (SET_DEST (set));
#ifdef FLOAT_STORE_FLAG_VALUE
	  REAL_VALUE_TYPE fsfv;
#endif

	  /* ??? We may not combine comparisons done in a CCmode with
	     comparisons not done in a CCmode.  This is to aid targets
	     like Alpha that have an IEEE compliant EQ instruction, and
	     a non-IEEE compliant BEQ instruction.  The use of CCmode is
	     actually artificial, simply to prevent the combination, but
	     should not affect other platforms.

	     However, we must allow VOIDmode comparisons to match either
	     CCmode or non-CCmode comparison, because some ports have
	     modeless comparisons inside branch patterns.

	     ??? This mode check should perhaps look more like the mode check
	     in simplify_comparison in combine.  */

	  if ((GET_CODE (SET_SRC (set)) == COMPARE
	       || (((code == NE
		     || (code == LT
			 && GET_MODE_CLASS (inner_mode) == MODE_INT
			 && (GET_MODE_BITSIZE (inner_mode)
			     <= HOST_BITS_PER_WIDE_INT)
			 && (STORE_FLAG_VALUE
			     & ((HOST_WIDE_INT) 1
				<< (GET_MODE_BITSIZE (inner_mode) - 1))))
#ifdef FLOAT_STORE_FLAG_VALUE
		     || (code == LT
			 && SCALAR_FLOAT_MODE_P (inner_mode)
			 && (fsfv = FLOAT_STORE_FLAG_VALUE (inner_mode),
			     REAL_VALUE_NEGATIVE (fsfv)))
#endif
		     ))
		   && COMPARISON_P (SET_SRC (set))))
	      && (((GET_MODE_CLASS (mode) == MODE_CC)
		   == (GET_MODE_CLASS (inner_mode) == MODE_CC))
		  || mode == VOIDmode || inner_mode == VOIDmode))
	    x = SET_SRC (set);
	  else if (((code == EQ
		     || (code == GE
			 && (GET_MODE_BITSIZE (inner_mode)
			     <= HOST_BITS_PER_WIDE_INT)
			 && GET_MODE_CLASS (inner_mode) == MODE_INT
			 && (STORE_FLAG_VALUE
			     & ((HOST_WIDE_INT) 1
				<< (GET_MODE_BITSIZE (inner_mode) - 1))))
#ifdef FLOAT_STORE_FLAG_VALUE
		     || (code == GE
			 && SCALAR_FLOAT_MODE_P (inner_mode)
			 && (fsfv = FLOAT_STORE_FLAG_VALUE (inner_mode),
			     REAL_VALUE_NEGATIVE (fsfv)))
#endif
		     ))
		   && COMPARISON_P (SET_SRC (set))
		   && (((GET_MODE_CLASS (mode) == MODE_CC)
			== (GET_MODE_CLASS (inner_mode) == MODE_CC))
		       || mode == VOIDmode || inner_mode == VOIDmode))

	    {
	      reverse_code = 1;
	      x = SET_SRC (set);
	    }
	  else
	    break;
	}

      else if (reg_set_p (op0, prev))
	/* If this sets OP0, but not directly, we have to give up.  */
	break;

      if (x)
	{
	  /* If the caller is expecting the condition to be valid at INSN,
	     make sure X doesn't change before INSN.  */
	  if (valid_at_insn_p)
	    if (modified_in_p (x, prev) || modified_between_p (x, prev, insn))
	      break;
	  if (COMPARISON_P (x))
	    code = GET_CODE (x);
	  if (reverse_code)
	    {
	      code = reversed_comparison_code (x, prev);
	      if (code == UNKNOWN)
		return 0;
	      reverse_code = 0;
	    }

	  op0 = XEXP (x, 0), op1 = XEXP (x, 1);
	  if (earliest)
	    *earliest = prev;
	}
    }

  /* If constant is first, put it last.  */
  if (CONSTANT_P (op0))
    code = swap_condition (code), tem = op0, op0 = op1, op1 = tem;

  /* If OP0 is the result of a comparison, we weren't able to find what
     was really being compared, so fail.  */
  if (!allow_cc_mode
      && GET_MODE_CLASS (GET_MODE (op0)) == MODE_CC)
    return 0;

  /* Canonicalize any ordered comparison with integers involving equality
     if we can do computations in the relevant mode and we do not
     overflow.  */

  if (GET_MODE_CLASS (GET_MODE (op0)) != MODE_CC
      && GET_CODE (op1) == CONST_INT
      && GET_MODE (op0) != VOIDmode
      && GET_MODE_BITSIZE (GET_MODE (op0)) <= HOST_BITS_PER_WIDE_INT)
    {
      HOST_WIDE_INT const_val = INTVAL (op1);
      unsigned HOST_WIDE_INT uconst_val = const_val;
      unsigned HOST_WIDE_INT max_val
	= (unsigned HOST_WIDE_INT) GET_MODE_MASK (GET_MODE (op0));

      switch (code)
	{
	case LE:
	  if ((unsigned HOST_WIDE_INT) const_val != max_val >> 1)
	    code = LT, op1 = gen_int_mode (const_val + 1, GET_MODE (op0));
	  break;

	/* When cross-compiling, const_val might be sign-extended from
	   BITS_PER_WORD to HOST_BITS_PER_WIDE_INT */
	case GE:
	  if ((HOST_WIDE_INT) (const_val & max_val)
	      != (((HOST_WIDE_INT) 1
		   << (GET_MODE_BITSIZE (GET_MODE (op0)) - 1))))
	    code = GT, op1 = gen_int_mode (const_val - 1, GET_MODE (op0));
	  break;

	case LEU:
	  if (uconst_val < max_val)
	    code = LTU, op1 = gen_int_mode (uconst_val + 1, GET_MODE (op0));
	  break;

	case GEU:
	  if (uconst_val != 0)
	    code = GTU, op1 = gen_int_mode (uconst_val - 1, GET_MODE (op0));
	  break;

	default:
	  break;
	}
    }

  /* Never return CC0; return zero instead.  */
  if (CC0_P (op0))
    return 0;

  return gen_rtx_fmt_ee (code, VOIDmode, op0, op1);
}

/* Given a jump insn JUMP, return the condition that will cause it to branch
   to its JUMP_LABEL.  If the condition cannot be understood, or is an
   inequality floating-point comparison which needs to be reversed, 0 will
   be returned.

   If EARLIEST is nonzero, it is a pointer to a place where the earliest
   insn used in locating the condition was found.  If a replacement test
   of the condition is desired, it should be placed in front of that
   insn and we will be sure that the inputs are still valid.  If EARLIEST
   is null, the returned condition will be valid at INSN.

   If ALLOW_CC_MODE is nonzero, allow the condition returned to be a
   compare CC mode register.

   VALID_AT_INSN_P is the same as for canonicalize_condition.  */

rtx
get_condition (rtx jump, rtx *earliest, int allow_cc_mode, int valid_at_insn_p)
{
  rtx cond;
  int reverse;
  rtx set;

  /* If this is not a standard conditional jump, we can't parse it.  */
  if (!JUMP_P (jump)
      || ! any_condjump_p (jump))
    return 0;
  set = pc_set (jump);

  cond = XEXP (SET_SRC (set), 0);

  /* If this branches to JUMP_LABEL when the condition is false, reverse
     the condition.  */
  reverse
    = GET_CODE (XEXP (SET_SRC (set), 2)) == LABEL_REF
      && XEXP (XEXP (SET_SRC (set), 2), 0) == JUMP_LABEL (jump);

  return canonicalize_condition (jump, cond, reverse, earliest, NULL_RTX,
				 allow_cc_mode, valid_at_insn_p);
}

/* Initialize the table NUM_SIGN_BIT_COPIES_IN_REP based on
   TARGET_MODE_REP_EXTENDED.

   Note that we assume that the property of
   TARGET_MODE_REP_EXTENDED(B, C) is sticky to the integral modes
   narrower than mode B.  I.e., if A is a mode narrower than B then in
   order to be able to operate on it in mode B, mode A needs to
   satisfy the requirements set by the representation of mode B.  */

static void
init_num_sign_bit_copies_in_rep (void)
{
  enum machine_mode mode, in_mode;

  for (in_mode = GET_CLASS_NARROWEST_MODE (MODE_INT); in_mode != VOIDmode;
       in_mode = GET_MODE_WIDER_MODE (mode))
    for (mode = GET_CLASS_NARROWEST_MODE (MODE_INT); mode != in_mode;
	 mode = GET_MODE_WIDER_MODE (mode))
      {
	enum machine_mode i;

	/* Currently, it is assumed that TARGET_MODE_REP_EXTENDED
	   extends to the next widest mode.  */
	gcc_assert (targetm.mode_rep_extended (mode, in_mode) == UNKNOWN
		    || GET_MODE_WIDER_MODE (mode) == in_mode);

	/* We are in in_mode.  Count how many bits outside of mode
	   have to be copies of the sign-bit.  */
	for (i = mode; i != in_mode; i = GET_MODE_WIDER_MODE (i))
	  {
	    enum machine_mode wider = GET_MODE_WIDER_MODE (i);

	    if (targetm.mode_rep_extended (i, wider) == SIGN_EXTEND
		/* We can only check sign-bit copies starting from the
		   top-bit.  In order to be able to check the bits we
		   have already seen we pretend that subsequent bits
		   have to be sign-bit copies too.  */
		|| num_sign_bit_copies_in_rep [in_mode][mode])
	      num_sign_bit_copies_in_rep [in_mode][mode]
		+= GET_MODE_BITSIZE (wider) - GET_MODE_BITSIZE (i);
	  }
      }
}

/* Suppose that truncation from the machine mode of X to MODE is not a
   no-op.  See if there is anything special about X so that we can
   assume it already contains a truncated value of MODE.  */

bool
truncated_to_mode (enum machine_mode mode, rtx x)
{
  /* This register has already been used in MODE without explicit
     truncation.  */
  if (REG_P (x) && rtl_hooks.reg_truncated_to_mode (mode, x))
    return true;

  /* See if we already satisfy the requirements of MODE.  If yes we
     can just switch to MODE.  */
  if (num_sign_bit_copies_in_rep[GET_MODE (x)][mode]
      && (num_sign_bit_copies (x, GET_MODE (x))
	  >= num_sign_bit_copies_in_rep[GET_MODE (x)][mode] + 1))
    return true;

  return false;
}

/* Initialize non_rtx_starting_operands, which is used to speed up
   for_each_rtx.  */
void
init_rtlanal (void)
{
  int i;
  for (i = 0; i < NUM_RTX_CODE; i++)
    {
      const char *format = GET_RTX_FORMAT (i);
      const char *first = strpbrk (format, "eEV");
      non_rtx_starting_operands[i] = first ? first - format : -1;
    }

  init_num_sign_bit_copies_in_rep ();
}

/* Check whether this is a constant pool constant.  */
bool
constant_pool_constant_p (rtx x)
{
  x = avoid_constant_pool_reference (x);
  return GET_CODE (x) == CONST_DOUBLE;
}

