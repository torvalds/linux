;; Predicate definitions for Renesas H8/300.
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

;; Return true if OP is a valid source operand for an integer move
;; instruction.

(define_predicate "general_operand_src"
  (match_code "const_int,const_double,const,symbol_ref,label_ref,subreg,reg,mem")
{
  if (GET_MODE (op) == mode
      && GET_CODE (op) == MEM
      && GET_CODE (XEXP (op, 0)) == POST_INC)
    return 1;
  return general_operand (op, mode);
})

;; Return true if OP is a valid destination operand for an integer
;; move instruction.

(define_predicate "general_operand_dst"
  (match_code "subreg,reg,mem")
{
  if (GET_MODE (op) == mode
      && GET_CODE (op) == MEM
      && GET_CODE (XEXP (op, 0)) == PRE_DEC)
    return 1;
  return general_operand (op, mode);
})

;; Likewise the second operand.

(define_predicate "h8300_src_operand"
  (match_code "const_int,const_double,const,symbol_ref,label_ref,subreg,reg,mem")
{
  if (TARGET_H8300SX)
    return general_operand (op, mode);
  return nonmemory_operand (op, mode);
})

;; Return true if OP is a suitable first operand for a general
;; arithmetic insn such as "add".

(define_predicate "h8300_dst_operand"
  (match_code "subreg,reg,mem")
{
  if (TARGET_H8300SX)
    return nonimmediate_operand (op, mode);
  return register_operand (op, mode);
})

;; Check that an operand is either a register or an unsigned 4-bit
;; constant.

(define_predicate "nibble_operand"
  (match_code "const_int")
{
  return (GET_CODE (op) == CONST_INT && TARGET_H8300SX
	  && INTVAL (op) >= 0 && INTVAL (op) <= 15);
})

;; Check that an operand is either a register or an unsigned 4-bit
;; constant.

(define_predicate "reg_or_nibble_operand"
  (match_code "const_int,subreg,reg")
{
  return (nibble_operand (op, mode) || register_operand (op, mode));
})

;; Return true if X is a shift operation of type H8SX_SHIFT_UNARY.

(define_predicate "h8sx_unary_shift_operator"
  (match_code "ashiftrt,lshiftrt,ashift,rotate")
{
  return (BINARY_P (op) && NON_COMMUTATIVE_P (op)
	  && (h8sx_classify_shift (GET_MODE (op), GET_CODE (op), XEXP (op, 1))
	      == H8SX_SHIFT_UNARY));
})

;; Likewise H8SX_SHIFT_BINARY.

(define_predicate "h8sx_binary_shift_operator"
  (match_code "ashiftrt,lshiftrt,ashift")
{
  return (BINARY_P (op) && NON_COMMUTATIVE_P (op)
	  && (h8sx_classify_shift (GET_MODE (op), GET_CODE (op), XEXP (op, 1))
	      == H8SX_SHIFT_BINARY));
})

;; Return true if OP is a binary operator in which it would be safe to
;; replace register operands with memory operands.

(define_predicate "h8sx_binary_memory_operator"
  (match_code "plus,minus,and,ior,xor,ashift,ashiftrt,lshiftrt,rotate")
{
  if (!TARGET_H8300SX)
    return false;

  if (GET_MODE (op) != QImode
      && GET_MODE (op) != HImode
      && GET_MODE (op) != SImode)
    return false;

  switch (GET_CODE (op))
    {
    case PLUS:
    case MINUS:
    case AND:
    case IOR:
    case XOR:
      return true;

    default:
      return h8sx_unary_shift_operator (op, mode);
    }
})

;; Like h8sx_binary_memory_operator, but applies to unary operators.

(define_predicate "h8sx_unary_memory_operator"
  (match_code "neg,not")
{
  if (!TARGET_H8300SX)
    return false;

  if (GET_MODE (op) != QImode
      && GET_MODE (op) != HImode
      && GET_MODE (op) != SImode)
    return false;

  switch (GET_CODE (op))
    {
    case NEG:
    case NOT:
      return true;

    default:
      return false;
    }
})

;; Return true if X is an ldm.l pattern.  X is known to be parallel.

(define_predicate "h8300_ldm_parallel"
  (match_code "parallel")
{
  return h8300_ldm_stm_parallel (XVEC (op, 0), 1, 0);
})

;; Likewise stm.l.

(define_predicate "h8300_stm_parallel"
  (match_code "parallel")
{
  return h8300_ldm_stm_parallel (XVEC (op, 0), 0, 0);
})

;; Likewise rts/l and rte/l.  Note that the .md pattern will check for
;; the return so there's no need to do that here.

(define_predicate "h8300_return_parallel"
  (match_code "parallel")
{
  return h8300_ldm_stm_parallel (XVEC (op, 0), 1, 1);
})

;; Return true if OP is a constant that contains only one 1 in its
;; binary representation.

(define_predicate "single_one_operand"
  (match_code "const_int")
{
  if (GET_CODE (op) == CONST_INT)
    {
      /* We really need to do this masking because 0x80 in QImode is
	 represented as -128 for example.  */
      if (exact_log2 (INTVAL (op) & GET_MODE_MASK (mode)) >= 0)
	return 1;
    }

  return 0;
})

;; Return true if OP is a constant that contains only one 0 in its
;; binary representation.

(define_predicate "single_zero_operand"
  (match_code "const_int")
{
  if (GET_CODE (op) == CONST_INT)
    {
      /* We really need to do this masking because 0x80 in QImode is
	 represented as -128 for example.  */
      if (exact_log2 (~INTVAL (op) & GET_MODE_MASK (mode)) >= 0)
	return 1;
    }

  return 0;
})

;; Return true if OP is a valid call operand.

(define_predicate "call_insn_operand"
  (match_code "mem")
{
  if (GET_CODE (op) == MEM)
    {
      rtx inside = XEXP (op, 0);
      if (register_operand (inside, Pmode))
	return 1;
      if (CONSTANT_ADDRESS_P (inside))
	return 1;
    }
  return 0;
})

;; Return true if OP is a valid call operand, and OP represents an
;; operand for a small call (4 bytes instead of 6 bytes).

(define_predicate "small_call_insn_operand"
  (match_code "mem")
{
  if (GET_CODE (op) == MEM)
    {
      rtx inside = XEXP (op, 0);

      /* Register indirect is a small call.  */
      if (register_operand (inside, Pmode))
	return 1;

      /* A call through the function vector is a small call too.  */
      if (GET_CODE (inside) == SYMBOL_REF
	  && (SYMBOL_REF_FLAGS (inside) & SYMBOL_FLAG_FUNCVEC_FUNCTION))
	return 1;
    }
  /* Otherwise it's a large call.  */
  return 0;
})

;; Return true if OP is a valid jump operand.

(define_predicate "jump_address_operand"
  (match_code "reg,mem")
{
  if (GET_CODE (op) == REG)
    return mode == Pmode;

  if (GET_CODE (op) == MEM)
    {
      rtx inside = XEXP (op, 0);
      if (register_operand (inside, Pmode))
	return 1;
      if (CONSTANT_ADDRESS_P (inside))
	return 1;
    }
  return 0;
})

;; Return 1 if an addition/subtraction of a constant integer can be
;; transformed into two consecutive adds/subs that are faster than the
;; straightforward way.  Otherwise, return 0.

(define_predicate "two_insn_adds_subs_operand"
  (match_code "const_int")
{
  if (TARGET_H8300SX)
    return 0;

  if (GET_CODE (op) == CONST_INT)
    {
      HOST_WIDE_INT value = INTVAL (op);

      /* Force VALUE to be positive so that we do not have to consider
         the negative case.  */
      if (value < 0)
	value = -value;
      if (TARGET_H8300H || TARGET_H8300S)
	{
	  /* A constant addition/subtraction takes 2 states in QImode,
	     4 states in HImode, and 6 states in SImode.  Thus, the
	     only case we can win is when SImode is used, in which
	     case, two adds/subs are used, taking 4 states.  */
	  if (mode == SImode
	      && (value == 2 + 1
		  || value == 4 + 1
		  || value == 4 + 2
		  || value == 4 + 4))
	    return 1;
	}
      else
	{
	  /* We do not profit directly by splitting addition or
	     subtraction of 3 and 4.  However, since these are
	     implemented as a sequence of adds or subs, they do not
	     clobber (cc0) unlike a sequence of add.b and add.x.  */
	  if (mode == HImode
	      && (value == 2 + 1
		  || value == 2 + 2))
	    return 1;
	}
    }

  return 0;
})

;; Recognize valid operands for bit-field instructions.

(define_predicate "bit_operand"
  (match_code "reg,subreg,mem")
{
  /* We can accept any nonimmediate operand, except that MEM operands must
     be limited to those that use addresses valid for the 'U' constraint.  */
  if (!nonimmediate_operand (op, mode))
    return 0;

  /* H8SX accepts pretty much anything here.  */
  if (TARGET_H8300SX)
    return 1;

  /* Accept any mem during RTL generation.  Otherwise, the code that does
     insv and extzv will think that we cannot handle memory.  However,
     to avoid reload problems, we only accept 'U' MEM operands after RTL
     generation.  This means that any named pattern which uses this predicate
     must force its operands to match 'U' before emitting RTL.  */

  if (GET_CODE (op) == REG)
    return 1;
  if (GET_CODE (op) == SUBREG)
    return 1;
  return (GET_CODE (op) == MEM
	  && OK_FOR_U (op));
})

;; Return nonzero if OP is a MEM suitable for bit manipulation insns.

(define_predicate "bit_memory_operand"
  (match_code "mem")
{
  return (GET_CODE (op) == MEM
	  && OK_FOR_U (op));
})

;; Return nonzero if X is a stack pointer.

(define_predicate "stack_pointer_operand"
  (match_code "reg")
{
  return op == stack_pointer_rtx;
})

;; Return nonzero if X is a constant whose absolute value is greater
;; than 2.

(define_predicate "const_int_gt_2_operand"
  (match_code "const_int")
{
  return (GET_CODE (op) == CONST_INT
	  && abs (INTVAL (op)) > 2);
})

;; Return nonzero if X is a constant whose absolute value is no
;; smaller than 8.

(define_predicate "const_int_ge_8_operand"
  (match_code "const_int")
{
  return (GET_CODE (op) == CONST_INT
	  && abs (INTVAL (op)) >= 8);
})

;; Return nonzero if X is a constant expressible in QImode.

(define_predicate "const_int_qi_operand"
  (match_code "const_int")
{
  return (GET_CODE (op) == CONST_INT
	  && (INTVAL (op) & 0xff) == INTVAL (op));
})

;; Return nonzero if X is a constant expressible in HImode.

(define_predicate "const_int_hi_operand"
  (match_code "const_int")
{
  return (GET_CODE (op) == CONST_INT
	  && (INTVAL (op) & 0xffff) == INTVAL (op));
})

;; Return nonzero if X is a constant suitable for inc/dec.

(define_predicate "incdec_operand"
  (match_code "const_int")
{
  return (GET_CODE (op) == CONST_INT
	  && (CONST_OK_FOR_M (INTVAL (op))
	      || CONST_OK_FOR_O (INTVAL (op))));
})

;; Recognize valid operators for bit instructions.

(define_predicate "bit_operator"
  (match_code "xor,and,ior")
{
  enum rtx_code code = GET_CODE (op);

  return (code == XOR
	  || code == AND
	  || code == IOR);
})

;; Return nonzero if OP is a shift operator.

(define_predicate "nshift_operator"
  (match_code "ashiftrt,lshiftrt,ashift")
{
  switch (GET_CODE (op))
    {
    case ASHIFTRT:
    case LSHIFTRT:
    case ASHIFT:
      return 1;

    default:
      return 0;
    }
})

;; Return nonzero if X is either EQ or NE.

(define_predicate "eqne_operator"
  (match_code "eq,ne")
{
  enum rtx_code code = GET_CODE (op);

  return (code == EQ || code == NE);
})

;; Return nonzero if X is either GT or LE.

(define_predicate "gtle_operator"
  (match_code "gt,le,gtu,leu")
{
  enum rtx_code code = GET_CODE (op);

  return (code == GT || code == LE);
})

;; Return nonzero if X is either GTU or LEU.

(define_predicate "gtuleu_operator"
  (match_code "gtu,leu")
{
  enum rtx_code code = GET_CODE (op);

  return (code == GTU || code == LEU);
})

;; Return nonzero if X is either IOR or XOR.

(define_predicate "iorxor_operator"
  (match_code "ior,xor")
{
  enum rtx_code code = GET_CODE (op);

  return (code == IOR || code == XOR);
})
