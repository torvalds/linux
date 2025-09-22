;; Predicate definitions for Renesas M32R.
;; Copyright (C) 2005 Free Software Foundation, Inc.
;;
;; This file is part of GCC.
;;
;; GCC is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.
;;
;; GCC is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to
;; the Free Software Foundation, 51 Franklin Street, Fifth Floor,
;; Boston, MA 02110-1301, USA.

;; Return true if OP is a register or the constant 0.

(define_predicate "reg_or_zero_operand"
  (match_code "reg,subreg,const_int")
{
  if (GET_CODE (op) == REG || GET_CODE (op) == SUBREG)
    return register_operand (op, mode);

  if (GET_CODE (op) != CONST_INT)
    return 0;

  return INTVAL (op) == 0;
})

;; Return nonzero if the operand is suitable for use in a conditional
;; move sequence.

(define_predicate "conditional_move_operand"
  (match_code "reg,subreg,const_int")
{
  /* Only defined for simple integers so far...  */
  if (mode != SImode && mode != HImode && mode != QImode)
    return FALSE;

  /* At the moment we can handle moving registers and loading constants.  */
  /* To be added: Addition/subtraction/bitops/multiplication of registers.  */

  switch (GET_CODE (op))
    {
    case REG:
      return 1;

    case CONST_INT:
      return INT8_P (INTVAL (op));

    default:
#if 0
      fprintf (stderr, "Test for cond move op of type: %s\n",
	       GET_RTX_NAME (GET_CODE (op)));
#endif
      return 0;
    }
})

;; Return true if the code is a test of the carry bit.

(define_predicate "carry_compare_operand"
  (match_code "eq,ne")
{
  rtx x;

  if (GET_MODE (op) != CCmode && GET_MODE (op) != VOIDmode)
    return FALSE;

  if (GET_CODE (op) != NE && GET_CODE (op) != EQ)
    return FALSE;

  x = XEXP (op, 0);
  if (GET_CODE (x) != REG || REGNO (x) != CARRY_REGNUM)
    return FALSE;

  x = XEXP (op, 1);
  if (GET_CODE (x) != CONST_INT || INTVAL (x) != 0)
    return FALSE;

  return TRUE;
})

;; Return 1 if OP is an EQ or NE comparison operator.

(define_predicate "eqne_comparison_operator"
  (match_code "eq,ne")
{
  enum rtx_code code = GET_CODE (op);

  return (code == EQ || code == NE);
})

;; Return 1 if OP is a signed comparison operator.

(define_predicate "signed_comparison_operator"
  (match_code "eq,ne,lt,le,gt,ge")
{
  enum rtx_code code = GET_CODE (op);

  return (COMPARISON_P (op)
  	  && (code == EQ || code == NE
	      || code == LT || code == LE || code == GT || code == GE));
})

;; Return true if OP is an acceptable argument for a move destination.

(define_predicate "move_dest_operand"
  (match_code "reg,subreg,mem")
{
  switch (GET_CODE (op))
    {
    case REG :
      return register_operand (op, mode);
    case SUBREG :
      /* (subreg (mem ...) ...) can occur here if the inner part was once a
	 pseudo-reg and is now a stack slot.  */
      if (GET_CODE (SUBREG_REG (op)) == MEM)
	return address_operand (XEXP (SUBREG_REG (op), 0), mode);
      else
	return register_operand (op, mode);
    case MEM :
      if (GET_CODE (XEXP (op, 0)) == POST_INC)
	return 0;		/* stores can't do post inc */
      return address_operand (XEXP (op, 0), mode);
    default :
      return 0;
    }
})

;; Return true if OP is an acceptable argument for a single word move
;; source.

(define_predicate "move_src_operand"
  (match_code "reg,subreg,mem,const_int,const_double,label_ref,const,symbol_ref")
{
  switch (GET_CODE (op))
    {
    case LABEL_REF :
    case SYMBOL_REF :
    case CONST :
      return addr24_operand (op, mode);
    case CONST_INT :
      /* ??? We allow more cse opportunities if we only allow constants
	 loadable with one insn, and split the rest into two.  The instances
	 where this would help should be rare and the current way is
	 simpler.  */
      if (HOST_BITS_PER_WIDE_INT > 32)
	{
	  HOST_WIDE_INT rest = INTVAL (op) >> 31;
	  return (rest == 0 || rest == -1);
	}
      else
	return 1;
    case CONST_DOUBLE :
      if (mode == SFmode)
	return 1;
      else if (mode == SImode)
	{
	  /* Large unsigned constants are represented as const_double's.  */
	  unsigned HOST_WIDE_INT low, high;

	  low = CONST_DOUBLE_LOW (op);
	  high = CONST_DOUBLE_HIGH (op);
	  return high == 0 && low <= (unsigned) 0xffffffff;
	}
      else
	return 0;
    case REG :
      return register_operand (op, mode);
    case SUBREG :
      /* (subreg (mem ...) ...) can occur here if the inner part was once a
	 pseudo-reg and is now a stack slot.  */
      if (GET_CODE (SUBREG_REG (op)) == MEM)
	return address_operand (XEXP (SUBREG_REG (op), 0), mode);
      else
	return register_operand (op, mode);
    case MEM :
      if (GET_CODE (XEXP (op, 0)) == PRE_INC
	  || GET_CODE (XEXP (op, 0)) == PRE_DEC)
	return 0;		/* loads can't do pre-{inc,dec} */
      return address_operand (XEXP (op, 0), mode);
    default :
      return 0;
    }
})

;; Return true if OP is an acceptable argument for a double word move
;; source.

(define_predicate "move_double_src_operand"
  (match_code "reg,subreg,mem,const_int,const_double")
{
  switch (GET_CODE (op))
    {
    case CONST_INT :
    case CONST_DOUBLE :
      return 1;
    case REG :
      return register_operand (op, mode);
    case SUBREG :
      /* (subreg (mem ...) ...) can occur here if the inner part was once a
	 pseudo-reg and is now a stack slot.  */
      if (GET_CODE (SUBREG_REG (op)) == MEM)
	return move_double_src_operand (SUBREG_REG (op), mode);
      else
	return register_operand (op, mode);
    case MEM :
      /* Disallow auto inc/dec for now.  */
      if (GET_CODE (XEXP (op, 0)) == PRE_DEC
	  || GET_CODE (XEXP (op, 0)) == PRE_INC)
	return 0;
      return address_operand (XEXP (op, 0), mode);
    default :
      return 0;
    }
})

;; Return true if OP is a const_int requiring two instructions to
;; load.

(define_predicate "two_insn_const_operand"
  (match_code "const_int")
{
  if (GET_CODE (op) != CONST_INT)
    return 0;
  if (INT16_P (INTVAL (op))
      || UINT24_P (INTVAL (op))
      || UPPER16_P (INTVAL (op)))
    return 0;
  return 1;
})

;; Returns 1 if OP is a symbol reference.

(define_predicate "symbolic_operand"
  (match_code "symbol_ref,label_ref,const")
{
  switch (GET_CODE (op))
    {
    case SYMBOL_REF:
    case LABEL_REF:
    case CONST :
      return 1;

    default:
      return 0;
    }
})

;; Return true if OP is a signed 8 bit immediate value.

(define_predicate "int8_operand"
  (match_code "const_int")
{
  if (GET_CODE (op) != CONST_INT)
    return 0;
  return INT8_P (INTVAL (op));
})

;; Return true if OP is an unsigned 16 bit immediate value.

(define_predicate "uint16_operand"
  (match_code "const_int")
{
  if (GET_CODE (op) != CONST_INT)
    return 0;
  return UINT16_P (INTVAL (op));
})

;; Return true if OP is a register or signed 16 bit value.

(define_predicate "reg_or_int16_operand"
  (match_code "reg,subreg,const_int")
{
  if (GET_CODE (op) == REG || GET_CODE (op) == SUBREG)
    return register_operand (op, mode);
  if (GET_CODE (op) != CONST_INT)
    return 0;
  return INT16_P (INTVAL (op));
})

;; Return true if OP is a register or an unsigned 16 bit value.

(define_predicate "reg_or_uint16_operand"
  (match_code "reg,subreg,const_int")
{
  if (GET_CODE (op) == REG || GET_CODE (op) == SUBREG)
    return register_operand (op, mode);
  if (GET_CODE (op) != CONST_INT)
    return 0;
  return UINT16_P (INTVAL (op));
})

;; Return true if OP is a register or signed 16 bit value for
;; compares.

(define_predicate "reg_or_cmp_int16_operand"
  (match_code "reg,subreg,const_int")
{
  if (GET_CODE (op) == REG || GET_CODE (op) == SUBREG)
    return register_operand (op, mode);
  if (GET_CODE (op) != CONST_INT)
    return 0;
  return CMP_INT16_P (INTVAL (op));
})

;; Return true if OP is a register or an integer value that can be
;; used is SEQ/SNE.  We can use either XOR of the value or ADD of the
;; negative of the value for the constant.  Don't allow 0, because
;; that is special cased.

(define_predicate "reg_or_eq_int16_operand"
  (match_code "reg,subreg,const_int")
{
  HOST_WIDE_INT value;

  if (GET_CODE (op) == REG || GET_CODE (op) == SUBREG)
    return register_operand (op, mode);

  if (GET_CODE (op) != CONST_INT)
    return 0;

  value = INTVAL (op);
  return (value != 0) && (UINT16_P (value) || CMP_INT16_P (-value));
})

;; Return true if OP is a signed 16 bit immediate value useful in
;; comparisons.

(define_predicate "cmp_int16_operand"
  (match_code "const_int")
{
  if (GET_CODE (op) != CONST_INT)
    return 0;
  return CMP_INT16_P (INTVAL (op));
})

;; Acceptable arguments to the call insn.

(define_predicate "call_address_operand"
  (match_code "symbol_ref,label_ref,const")
{
  return symbolic_operand (op, mode);

/* Constants and values in registers are not OK, because
   the m32r BL instruction can only support PC relative branching.  */
})

;; Return true if OP is an acceptable input argument for a zero/sign
;; extend operation.

(define_predicate "extend_operand"
  (match_code "reg,subreg,mem")
{
  rtx addr;

  switch (GET_CODE (op))
    {
    case REG :
    case SUBREG :
      return register_operand (op, mode);

    case MEM :
      addr = XEXP (op, 0);
      if (GET_CODE (addr) == PRE_INC || GET_CODE (addr) == PRE_DEC)
	return 0;		/* loads can't do pre inc/pre dec */

      return address_operand (addr, mode);

    default :
      return 0;
    }
})

;; Return nonzero if the operand is an insn that is a small
;; insn. Allow const_int 0 as well, which is a placeholder for NOP
;; slots.

(define_predicate "small_insn_p"
  (match_code "insn,call_insn,jump_insn")
{
  if (GET_CODE (op) == CONST_INT && INTVAL (op) == 0)
    return 1;

  if (! INSN_P (op))
    return 0;

  return get_attr_length (op) == 2;
})

;; Return true if op is an integer constant, less than or equal to
;; MAX_MOVE_BYTES.

(define_predicate "m32r_block_immediate_operand"
  (match_code "const_int")
{
  if (GET_CODE (op) != CONST_INT
      || INTVAL (op) > MAX_MOVE_BYTES
      || INTVAL (op) <= 0)
    return 0;

  return 1;
})

;; Return nonzero if the operand is an insn that is a large insn.

(define_predicate "large_insn_p"
  (match_code "insn,call_insn,jump_insn")
{
  if (! INSN_P (op))
    return 0;

  return get_attr_length (op) != 2;
})

;; Returns 1 if OP is an acceptable operand for seth/add3.

(define_predicate "seth_add3_operand"
  (match_code "symbol_ref,label_ref,const")
{
  if (flag_pic)
    return 0;

  if (GET_CODE (op) == SYMBOL_REF
      || GET_CODE (op) == LABEL_REF)
    return 1;

  if (GET_CODE (op) == CONST
      && GET_CODE (XEXP (op, 0)) == PLUS
      && GET_CODE (XEXP (XEXP (op, 0), 0)) == SYMBOL_REF
      && GET_CODE (XEXP (XEXP (op, 0), 1)) == CONST_INT
      && INT16_P (INTVAL (XEXP (XEXP (op, 0), 1))))
    return 1;

  return 0;
})
