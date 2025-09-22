;; Predicate definitions for Motorola 88000.
;; Copyright (C) 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
;; 2001, 2002 Free Software Foundation, Inc.
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

;; Return true if OP is a suitable input for a move insn.

(define_predicate "move_operand"
  (match_code "subreg, reg, const_int, lo_sum, mem")
{
  if (register_operand (op, mode))
    return true;
  if (CONST_INT_P (op))
    return (classify_integer (mode, INTVAL (op)) < m88k_oru_hi16);
  if (GET_MODE (op) != mode)
    return false;
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  if (!MEM_P (op))
    return false;

  op = XEXP (op, 0);
  if (GET_CODE (op) == LO_SUM)
    return (REG_P (XEXP (op, 0)) && symbolic_address_p (XEXP (op, 1)));
  return memory_address_p (mode, op);
})

;; Return true if OP is suitable for a call insn.

(define_predicate "call_address_operand"
  (and (match_code "subreg, reg, symbol_ref, label_ref, const")
       (match_test "REG_P (op) || symbolic_address_p (op)")))

;; Return true if OP is a register or const0_rtx.

(define_predicate "reg_or_0_operand"
  (and (match_code "subreg, reg, const_int")
       (match_test "op == const0_rtx || register_operand (op, mode)")))

;; Return true if OP is a valid second operand for an arithmetic insn.

(define_predicate "arith_operand"
  (and (match_code "subreg, reg, const_int")
       (match_test "register_operand (op, mode)
		    || (CONST_INT_P (op) && SMALL_INT (op))")))

;; Return true if OP is a register or 5 bit integer.

(define_predicate "arith5_operand"
  (and (match_code "subreg, reg, const_int")
       (match_test "register_operand (op, mode)
		    || (CONST_INT_P (op) && (unsigned) INTVAL (op) < 32)")))

(define_predicate "arith32_operand"
  (and (match_code "subreg, reg, const_int")
       (match_test "register_operand (op, mode) || CONST_INT_P (op)")))

(define_predicate "arith64_operand"
  (and (match_code "subreg, reg, const_int")
       (match_test "register_operand (op, mode) || CONST_INT_P (op)")))

(define_predicate "int5_operand"
  (and (match_code "const_int")
       (match_test "(unsigned) INTVAL (op) < 32")))

(define_predicate "int32_operand"
  (match_code "const_int"))

;; Return true if OP is a register or a valid immediate operand for
;; addu or subu.

(define_predicate "add_operand"
  (and (match_code "subreg, reg, const_int")
       (match_test "register_operand (op, mode)
		    || (CONST_INT_P (op) && ADD_INT (op))")))

(define_predicate "reg_or_bbx_mask_operand"
  (match_code "subreg, reg, const_int")
{
  int value;
  if (register_operand (op, mode))
    return true;
  if (!CONST_INT_P (op))
    return false;

  value = INTVAL (op);
  if (POWER_OF_2 (value))
    return true;

  return false;
})

;; Return true if OP is valid to use in the context of a floating
;; point operation.  Special case 0.0, since we can use r0.

(define_predicate "real_or_0_operand"
  (match_code "subreg, reg, const_double")
{
  if (mode != SFmode && mode != DFmode)
    return false;

  return (register_operand (op, mode)
	  || (GET_CODE (op) == CONST_DOUBLE
	      && op == CONST0_RTX (mode)));
})

;; Return true if OP is valid to use in the context of logic arithmetic
;; on condition codes.

(define_special_predicate "partial_ccmode_register_operand"
  (and (match_code "subreg, reg")
       (ior (match_test "register_operand (op, CCmode)")
	    (match_test "register_operand (op, CCEVENmode)"))))

;; Return true if OP is a relational operator.

(define_predicate "relop"
  (match_code "unordered, ordered, eq, ne, lt, le, ge, gt, ltu, leu, geu, gtu"))

(define_predicate "even_relop"
  (match_code "unordered, eq, lt, gt, ltu, gtu"))

(define_predicate "odd_relop"
  (match_code "ordered, ne, le, ge, leu, geu"))

;; Return true if OP is a relational operator, and is not an unsigned
;; relational operator.

(define_predicate "relop_no_unsigned"
  (match_code "eq, ne, lt, le, ge, gt")
{
  /* @@ What is this test doing?  Why not use `mode'?  */
  if (GET_MODE_CLASS (GET_MODE (op)) == MODE_FLOAT
      || GET_MODE (op) == DImode
      || GET_MODE_CLASS (GET_MODE (XEXP (op, 0))) == MODE_FLOAT
      || GET_MODE (XEXP (op, 0)) == DImode
      || GET_MODE_CLASS (GET_MODE (XEXP (op, 1))) == MODE_FLOAT
      || GET_MODE (XEXP (op, 1)) == DImode)
    return false;
  return true;
})

;; Return true if the code of this rtx pattern is EQ or NE.

(define_predicate "equality_op"
  (match_code "eq, ne"))

;; Return true if the code of this rtx pattern is pc or label_ref.

(define_special_predicate "pc_or_label_ref"
  (match_code "pc, label_ref"))

;; Return true if OP is either a symbol reference or a sum of a symbol
;; reference and a constant.

(define_predicate "symbolic_operand"
  (and (match_code "symbol_ref,label_ref,const")
       (match_test "symbolic_address_p (op)")))
