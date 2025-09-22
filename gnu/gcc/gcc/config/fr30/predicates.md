;; Predicate definitions for FR30.
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

;; Returns true if OP is an integer value suitable for use in an
;; ADDSP instruction.

(define_predicate "stack_add_operand"
  (match_code "const_int")
{
  return
    (GET_CODE (op) == CONST_INT
     && INTVAL (op) >= -512
     && INTVAL (op) <=  508
     && ((INTVAL (op) & 3) == 0));
})

;; Returns true if OP is hard register in the range 8 - 15.

(define_predicate "high_register_operand"
  (match_code "reg")
{
  return
    (GET_CODE (op) == REG
     && REGNO (op) <= 15
     && REGNO (op) >= 8);
})

;; Returns true if OP is hard register in the range 0 - 7.

(define_predicate "low_register_operand"
  (match_code "reg")
{
  return
    (GET_CODE (op) == REG
     && REGNO (op) <= 7);
})

;; Returns true if OP is suitable for use in a CALL insn.

(define_predicate "call_operand"
  (match_code "mem")
{
  return (GET_CODE (op) == MEM
	  && (GET_CODE (XEXP (op, 0)) == SYMBOL_REF
	      || GET_CODE (XEXP (op, 0)) == REG));
})

;; Returns TRUE if OP is a valid operand of a DImode operation.

(define_predicate "di_operand"
  (match_code "const_int,const_double,reg,mem")
{
  if (register_operand (op, mode))
    return TRUE;

  if (mode != VOIDmode && GET_MODE (op) != VOIDmode && GET_MODE (op) != DImode)
    return FALSE;

  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);

  switch (GET_CODE (op))
    {
    case CONST_DOUBLE:
    case CONST_INT:
      return TRUE;

    case MEM:
      return memory_address_p (DImode, XEXP (op, 0));

    default:
      return FALSE;
    }
})

;; Returns TRUE if OP is a DImode register or MEM.

(define_predicate "nonimmediate_di_operand"
  (match_code "reg,mem")
{
  if (register_operand (op, mode))
    return TRUE;

  if (mode != VOIDmode && GET_MODE (op) != VOIDmode && GET_MODE (op) != DImode)
    return FALSE;

  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);

  if (GET_CODE (op) == MEM)
    return memory_address_p (DImode, XEXP (op, 0));

  return FALSE;
})

;; Returns true if OP is an integer value suitable for use in an ADD
;; or ADD2 instruction, or if it is a register.

(define_predicate "add_immediate_operand"
  (match_code "reg,const_int")
{
  return
    (GET_CODE (op) == REG
     || (GET_CODE (op) == CONST_INT
	 && INTVAL (op) >= -16
	 && INTVAL (op) <=  15));
})
