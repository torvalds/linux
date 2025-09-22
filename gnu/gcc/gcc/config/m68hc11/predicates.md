;; Predicate definitions for Motorola 68HC11 and 68HC12.
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

;; TODO: Add a comment here.

(define_predicate "stack_register_operand"
  (match_code "subreg,reg")
{
  return SP_REG_P (op);
})

;; TODO: Add a comment here.

(define_predicate "d_register_operand"
  (match_code "subreg,reg")
{
  if (GET_MODE (op) != mode && mode != VOIDmode)
    return 0;

  if (GET_CODE (op) == SUBREG)
    op = XEXP (op, 0);

  return GET_CODE (op) == REG
    && (REGNO (op) >= FIRST_PSEUDO_REGISTER
	|| REGNO (op) == HARD_D_REGNUM
        || (mode == QImode && REGNO (op) == HARD_B_REGNUM));
})

;; TODO: Add a comment here.

(define_predicate "hard_addr_reg_operand"
  (match_code "subreg,reg")
{
  if (GET_MODE (op) != mode && mode != VOIDmode)
    return 0;

  if (GET_CODE (op) == SUBREG)
    op = XEXP (op, 0);

  return GET_CODE (op) == REG
    && (REGNO (op) == HARD_X_REGNUM
	|| REGNO (op) == HARD_Y_REGNUM
	|| REGNO (op) == HARD_Z_REGNUM);
})

;; TODO: Add a comment here.

(define_predicate "hard_reg_operand"
  (match_code "subreg,reg")
{
  if (GET_MODE (op) != mode && mode != VOIDmode)
    return 0;

  if (GET_CODE (op) == SUBREG)
    op = XEXP (op, 0);

  return GET_CODE (op) == REG
    && (REGNO (op) >= FIRST_PSEUDO_REGISTER
	|| H_REGNO_P (REGNO (op)));
})

;; TODO: Add a comment here.

(define_predicate "m68hc11_logical_operator"
  (match_code "and,ior,xor")
{
  return GET_CODE (op) == AND || GET_CODE (op) == IOR || GET_CODE (op) == XOR;
})

;; TODO: Add a comment here.

(define_predicate "m68hc11_arith_operator"
  (match_code "and,ior,xor,plus,minus,ashift,ashiftrt,lshiftrt,rotate,rotatert")
{
  return GET_CODE (op) == AND || GET_CODE (op) == IOR || GET_CODE (op) == XOR
    || GET_CODE (op) == PLUS || GET_CODE (op) == MINUS
    || GET_CODE (op) == ASHIFT || GET_CODE (op) == ASHIFTRT
    || GET_CODE (op) == LSHIFTRT || GET_CODE (op) == ROTATE
    || GET_CODE (op) == ROTATERT;
})

;; TODO: Add a comment here.

(define_predicate "m68hc11_non_shift_operator"
  (match_code "and,ior,xor,plus,minus")
{
  return GET_CODE (op) == AND || GET_CODE (op) == IOR || GET_CODE (op) == XOR
    || GET_CODE (op) == PLUS || GET_CODE (op) == MINUS;
})

;; TODO: Add a comment here.

(define_predicate "m68hc11_unary_operator"
  (match_code "neg,not,sign_extend,zero_extend")
{
  return GET_CODE (op) == NEG || GET_CODE (op) == NOT
    || GET_CODE (op) == SIGN_EXTEND || GET_CODE (op) == ZERO_EXTEND;
})

;; Return true if op is a shift operator.

(define_predicate "m68hc11_shift_operator"
  (match_code "ashift,ashiftrt,lshiftrt,rotate,rotatert")
{
  return GET_CODE (op) == ROTATE || GET_CODE (op) == ROTATERT
    || GET_CODE (op) == LSHIFTRT || GET_CODE (op) == ASHIFT
    || GET_CODE (op) == ASHIFTRT;
})

;; TODO: Add a comment here.

(define_predicate "m68hc11_eq_compare_operator"
  (match_code "eq,ne")
{
  return GET_CODE (op) == EQ || GET_CODE (op) == NE;
})

;; TODO: Add a comment here.

(define_predicate "non_push_operand"
  (match_code "subreg,reg,mem")
{
  if (general_operand (op, mode) == 0)
    return 0;

  if (push_operand (op, mode) == 1)
    return 0;
  return 1;
})

;; TODO: Add a comment here.

(define_predicate "splitable_operand"
  (match_code "subreg,reg,mem,symbol_ref,label_ref,const_int,const_double")
{
  if (general_operand (op, mode) == 0)
    return 0;

  if (push_operand (op, mode) == 1)
    return 0;

  /* Reject a (MEM (MEM X)) because the patterns that use non_push_operand
     need to split such addresses to access the low and high part but it
     is not possible to express a valid address for the low part.  */
  if (mode != QImode && GET_CODE (op) == MEM
      && GET_CODE (XEXP (op, 0)) == MEM)
    return 0;
  return 1;
})

;; TODO: Add a comment here.

(define_predicate "reg_or_some_mem_operand"
  (match_code "subreg,reg,mem")
{
  if (GET_CODE (op) == MEM)
    {
      rtx op0 = XEXP (op, 0);
      int addr_mode;

      if (symbolic_memory_operand (op0, mode))
	return 1;

      if (IS_STACK_PUSH (op))
	return 1;

      if (GET_CODE (op) == REG && reload_in_progress
          && REGNO (op) >= FIRST_PSEUDO_REGISTER
          && reg_equiv_memory_loc[REGNO (op)])
         {
            op = reg_equiv_memory_loc[REGNO (op)];
            op = eliminate_regs (op, 0, NULL_RTX);
         }
      if (GET_CODE (op) != MEM)
         return 0;

      op0 = XEXP (op, 0);
      addr_mode = m68hc11_addr_mode | (reload_completed ? ADDR_STRICT : 0);
      addr_mode &= ~ADDR_INDIRECT;
      return m68hc11_valid_addressing_p (op0, mode, addr_mode);
    }

  return register_operand (op, mode);
})

;; TODO: Add a comment here.

(define_predicate "tst_operand"
  (match_code "subreg,reg,mem")
{
  if (GET_CODE (op) == MEM && reload_completed == 0)
    {
      rtx addr = XEXP (op, 0);
      if (m68hc11_auto_inc_p (addr))
	return 0;
    }
  return nonimmediate_operand (op, mode);
})

;; TODO: Add a comment here.

(define_predicate "cmp_operand"
  (match_code "subreg,reg,mem,symbol_ref,label_ref,const_int,const_double")
{
  if (GET_CODE (op) == MEM)
    {
      rtx addr = XEXP (op, 0);
      if (m68hc11_auto_inc_p (addr))
	return 0;
    }
  return general_operand (op, mode);
})
