;; Predicate definitions for the Blackfin.
;; Copyright (C) 2005, 2006  Free Software Foundation, Inc.
;; Contributed by Analog Devices.
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

;; Return nonzero iff OP is one of the integer constants 1 or 2.
(define_predicate "pos_scale_operand"
  (and (match_code "const_int")
       (match_test "INTVAL (op) == 1 || INTVAL (op) == 2")))

;; Return nonzero iff OP is one of the integer constants 2 or 4.
(define_predicate "scale_by_operand"
  (and (match_code "const_int")
       (match_test "INTVAL (op) == 2 || INTVAL (op) == 4")))

;; Return nonzero if OP is a constant that consists of two parts; lower
;; bits all zero and upper bits all ones.  In this case, we can perform
;; an AND operation with a sequence of two shifts.  Don't return nonzero
;; if the constant would be cheap to load.
(define_predicate "highbits_operand"
  (and (match_code "const_int")
       (match_test "log2constp (-INTVAL (op)) && !CONST_7BIT_IMM_P (INTVAL (op))")))

;; Return nonzero if OP is suitable as a right-hand side operand for an
;; andsi3 operation.
(define_predicate "rhs_andsi3_operand"
  (ior (match_operand 0 "register_operand")
       (and (match_code "const_int")
	    (match_test "log2constp (~INTVAL (op)) || INTVAL (op) == 255 || INTVAL (op) == 65535"))))

;; Return nonzero if OP is a register or a constant with exactly one bit
;; set.
(define_predicate "regorlog2_operand"
  (ior (match_operand 0 "register_operand")
       (and (match_code "const_int")
	    (match_test "log2constp (INTVAL (op))"))))

;; Return nonzero if OP is a register or an integer constant.
(define_predicate "reg_or_const_int_operand"
  (ior (match_operand 0 "register_operand")
       (match_code "const_int")))

(define_predicate "const01_operand"
  (and (match_code "const_int")
       (match_test "op == const0_rtx || op == const1_rtx")))

(define_predicate "vec_shift_operand"
  (ior (and (match_code "const_int")
	    (match_test "INTVAL (op) >= -16 && INTVAL (op) < 15"))
       (match_operand 0 "register_operand")))

;; Like register_operand, but make sure that hard regs have a valid mode.
(define_predicate "valid_reg_operand"
  (match_operand 0 "register_operand")
{
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  if (REGNO (op) < FIRST_PSEUDO_REGISTER)
    return HARD_REGNO_MODE_OK (REGNO (op), mode);
  return 1;
})

;; Return nonzero if OP is a LC register.
(define_predicate "lc_register_operand"
  (and (match_code "reg")
       (match_test "REGNO (op) == REG_LC0 || REGNO (op) == REG_LC1")))

;; Return nonzero if OP is a LT register.
(define_predicate "lt_register_operand"
  (and (match_code "reg")
       (match_test "REGNO (op) == REG_LT0 || REGNO (op) == REG_LT1")))

;; Return nonzero if OP is a LB register.
(define_predicate "lb_register_operand"
  (and (match_code "reg")
       (match_test "REGNO (op) == REG_LB0 || REGNO (op) == REG_LB1")))

;; Return nonzero if OP is a register or a 7 bit signed constant.
(define_predicate "reg_or_7bit_operand"
  (ior (match_operand 0 "register_operand")
       (and (match_code "const_int")
	    (match_test "CONST_7BIT_IMM_P (INTVAL (op))"))))

;; Return nonzero if OP is a register other than DREG and PREG.
(define_predicate "nondp_register_operand"
  (match_operand 0 "register_operand")
{
  unsigned int regno;
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);

  regno = REGNO (op);
  return (regno >= FIRST_PSEUDO_REGISTER || !DP_REGNO_P (regno));
})

;; Return nonzero if OP is a register other than DREG and PREG, or MEM.
(define_predicate "nondp_reg_or_memory_operand"
  (ior (match_operand 0 "nondp_register_operand")
       (match_operand 0 "memory_operand")))

;; Return nonzero if OP is a register or, when negated, a 7 bit signed
;; constant.
(define_predicate "reg_or_neg7bit_operand"
  (ior (match_operand 0 "register_operand")
       (and (match_code "const_int")
	    (match_test "CONST_7BIT_IMM_P (-INTVAL (op))"))))

;; Used for secondary reloads, this function returns 1 if OP is of the
;; form (plus (fp) (const_int)).
(define_predicate "fp_plus_const_operand"
  (match_code "plus")
{
  rtx op1, op2;

  op1 = XEXP (op, 0);
  op2 = XEXP (op, 1);
  return (REG_P (op1)
	  && (REGNO (op1) == FRAME_POINTER_REGNUM
	      || REGNO (op1) == STACK_POINTER_REGNUM)
	  && GET_CODE (op2) == CONST_INT);
})

;; Returns 1 if OP is a symbolic operand, i.e. a symbol_ref or a label_ref,
;; possibly with an offset.
(define_predicate "symbolic_operand"
  (ior (match_code "symbol_ref,label_ref")
       (and (match_code "const")
	    (match_test "GET_CODE (XEXP (op,0)) == PLUS
			 && (GET_CODE (XEXP (XEXP (op, 0), 0)) == SYMBOL_REF
			     || GET_CODE (XEXP (XEXP (op, 0), 0)) == LABEL_REF)
			 && GET_CODE (XEXP (XEXP (op, 0), 1)) == CONST_INT"))))

;; Returns 1 if OP is a plain constant or matched by symbolic_operand.
(define_predicate "symbolic_or_const_operand"
  (ior (match_code "const_int,const_double")
       (match_operand 0 "symbolic_operand")))

;; Returns 1 if OP is a SYMBOL_REF.
(define_predicate "symbol_ref_operand"
  (match_code "symbol_ref"))

;; True for any non-virtual or eliminable register.  Used in places where
;; instantiation of such a register may cause the pattern to not be recognized.
(define_predicate "register_no_elim_operand"
  (match_operand 0 "register_operand")
{
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  return !(op == arg_pointer_rtx
	   || op == frame_pointer_rtx
	   || (REGNO (op) >= FIRST_PSEUDO_REGISTER
	       && REGNO (op) <= LAST_VIRTUAL_REGISTER));
})

;; Test for an operator valid in a conditional branch
(define_predicate "bfin_cbranch_operator"
  (match_code "eq,ne"))
