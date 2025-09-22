;; Predicate definitions for Vitesse IQ2000.
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

;; Return 1 if OP can be used as an operand where a register or 16 bit
;; unsigned integer is needed.

(define_predicate "uns_arith_operand"
  (match_code "reg,const_int,subreg")
{
  if (GET_CODE (op) == CONST_INT && SMALL_INT_UNSIGNED (op))
    return 1;

  return register_operand (op, mode);
})

;; Return 1 if OP can be used as an operand where a 16 bit integer is
;; needed.

(define_predicate "arith_operand"
  (match_code "reg,const_int,subreg")
{
  if (GET_CODE (op) == CONST_INT && SMALL_INT (op))
    return 1;

  return register_operand (op, mode);
})

;; Return 1 if OP is a integer which fits in 16 bits.

(define_predicate "small_int"
  (match_code "const_int")
{
  return (GET_CODE (op) == CONST_INT && SMALL_INT (op));
})

;; Return 1 if OP is a 32 bit integer which is too big to be loaded
;; with one instruction.

(define_predicate "large_int"
  (match_code "const_int")
{
  HOST_WIDE_INT value;

  if (GET_CODE (op) != CONST_INT)
    return 0;

  value = INTVAL (op);

  /* IOR reg,$r0,value.  */
  if ((value & ~ ((HOST_WIDE_INT) 0x0000ffff)) == 0)
    return 0;

  /* SUBU reg,$r0,value.  */
  if (((unsigned HOST_WIDE_INT) (value + 32768)) <= 32767)
    return 0;

  /* LUI reg,value >> 16.  */
  if ((value & 0x0000ffff) == 0)
    return 0;

  return 1;
})

;; Return 1 if OP is a register or the constant 0.

(define_predicate "reg_or_0_operand"
  (match_code "reg,const_int,const_double,subreg")
{
  switch (GET_CODE (op))
    {
    case CONST_INT:
      return INTVAL (op) == 0;

    case CONST_DOUBLE:
      return op == CONST0_RTX (mode);

    case REG:
    case SUBREG:
      return register_operand (op, mode);

    default:
      break;
    }

  return 0;
})

;; Return 1 if OP is a memory operand that fits in a single
;; instruction (i.e., register + small offset).

(define_predicate "simple_memory_operand"
  (match_code "mem,subreg")
{
  rtx addr, plus0, plus1;

  /* Eliminate non-memory operations.  */
  if (GET_CODE (op) != MEM)
    return 0;

  /* Dword operations really put out 2 instructions, so eliminate them.  */
  if (GET_MODE_SIZE (GET_MODE (op)) > (unsigned) UNITS_PER_WORD)
    return 0;

  /* Decode the address now.  */
  addr = XEXP (op, 0);
  switch (GET_CODE (addr))
    {
    case REG:
    case LO_SUM:
      return 1;

    case CONST_INT:
      return SMALL_INT (addr);

    case PLUS:
      plus0 = XEXP (addr, 0);
      plus1 = XEXP (addr, 1);
      if (GET_CODE (plus0) == REG
	  && GET_CODE (plus1) == CONST_INT && SMALL_INT (plus1)
	  && SMALL_INT_UNSIGNED (plus1) /* No negative offsets.  */)
	return 1;

      else if (GET_CODE (plus1) == REG
	       && GET_CODE (plus0) == CONST_INT && SMALL_INT (plus0)
	       && SMALL_INT_UNSIGNED (plus1) /* No negative offsets.  */)
	return 1;

      else
	return 0;

    case SYMBOL_REF:
      return 0;

    default:
      break;
    }

  return 0;
})

;; Return nonzero if the code of this rtx pattern is EQ or NE.

(define_predicate "equality_op"
  (match_code "eq,ne")
{
  if (mode != GET_MODE (op))
    return 0;

  return GET_CODE (op) == EQ || GET_CODE (op) == NE;
})

;; Return nonzero if the code is a relational operations (EQ, LE,
;; etc).

(define_predicate "cmp_op"
  (match_code "eq,ne,gt,ge,gtu,geu,lt,le,ltu,leu")
{
  if (mode != GET_MODE (op))
    return 0;

  return COMPARISON_P (op);
})

;; Return nonzero if the operand is either the PC or a label_ref.

(define_special_predicate "pc_or_label_operand"
  (match_code "pc,label_ref")
{
  if (op == pc_rtx)
    return 1;

  if (GET_CODE (op) == LABEL_REF)
    return 1;

  return 0;
})

;; Return nonzero if OP is a valid operand for a call instruction.

(define_predicate "call_insn_operand"
  (match_code "const_int,const,symbol_ref,reg")
{
  return (CONSTANT_ADDRESS_P (op)
	  || (GET_CODE (op) == REG && op != arg_pointer_rtx
	      && ! (REGNO (op) >= FIRST_PSEUDO_REGISTER
		    && REGNO (op) <= LAST_VIRTUAL_REGISTER)));
})

;; Return nonzero if OP is valid as a source operand for a move
;; instruction.

(define_predicate "move_operand"
  (match_code "const_int,const_double,const,symbol_ref,label_ref,subreg,reg,mem")
{
  /* Accept any general operand after reload has started; doing so
     avoids losing if reload does an in-place replacement of a register
     with a SYMBOL_REF or CONST.  */
  return (general_operand (op, mode)
	  && (! (iq2000_check_split (op, mode))
	      || reload_in_progress || reload_completed));
})

;; Return nonzero if OP is a constant power of 2.

(define_predicate "power_of_2_operand"
  (match_code "const_int")
{
  int intval;

  if (GET_CODE (op) != CONST_INT)
    return 0;
  else
    intval = INTVAL (op);

  return ((intval & ((unsigned)(intval) - 1)) == 0);
})
