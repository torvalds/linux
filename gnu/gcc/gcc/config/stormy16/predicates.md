;; Predicate definitions for XSTORMY16.
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

;; Return 1 if OP is a shift operator.

(define_predicate "shift_operator"
  (match_code "ashift,ashiftrt,lshiftrt")
{
  enum rtx_code code = GET_CODE (op);

  return (code == ASHIFT
	  || code == ASHIFTRT
	  || code == LSHIFTRT);
})

;; Return 1 if this is an EQ or NE operator.

(define_predicate "equality_operator"
  (match_code "eq,ne")
{
  return ((mode == VOIDmode || GET_MODE (op) == mode)
	  && (GET_CODE (op) == EQ || GET_CODE (op) == NE));
})

;; Return 1 if this is a comparison operator but not an EQ or NE
;; operator.

(define_predicate "inequality_operator"
  (match_code "ge,gt,le,lt,geu,gtu,leu,ltu")
{
  return comparison_operator (op, mode) && ! equality_operator (op, mode);
})

;; Return 1 if this is a LT, GE, LTU, or GEU operator.

(define_predicate "xstormy16_ineqsi_operator"
  (match_code "lt,ge,ltu,geu")
{
  enum rtx_code code = GET_CODE (op);
  
  return ((mode == VOIDmode || GET_MODE (op) == mode)
	  && (code == LT || code == GE || code == LTU || code == GEU));
})

;; Predicate for MEMs that can use special 8-bit addressing.

(define_predicate "xstormy16_below100_operand"
  (match_code "mem")
{
  if (GET_MODE (op) != mode)
    return 0;
  if (GET_CODE (op) == MEM)
    op = XEXP (op, 0);
  else if (GET_CODE (op) == SUBREG
	   && GET_CODE (XEXP (op, 0)) == MEM
	   && !MEM_VOLATILE_P (XEXP (op, 0)))
    op = XEXP (XEXP (op, 0), 0);
  else
    return 0;
  if (GET_CODE (op) == CONST_INT)
    {
      HOST_WIDE_INT i = INTVAL (op);
      return (i >= 0x7f00 && i < 0x7fff);
    }
  return xstormy16_below100_symbol (op, HImode);
})

;; TODO: Add a comment here.

(define_predicate "xstormy16_below100_or_register"
  (match_code "mem,reg,subreg")
{
  return (xstormy16_below100_operand (op, mode)
	  || register_operand (op, mode));
})

;; TODO: Add a comment here.

(define_predicate "xstormy16_splittable_below100_or_register"
  (match_code "mem,reg,subreg")
{
  if (GET_CODE (op) == MEM && MEM_VOLATILE_P (op))
    return 0;
  return (xstormy16_below100_operand (op, mode)
	  || register_operand (op, mode));
})

;; Predicate for constants with exactly one bit not set.

(define_predicate "xstormy16_onebit_clr_operand"
  (match_code "const_int")
{
  HOST_WIDE_INT i;
  if (GET_CODE (op) != CONST_INT)
    return 0;
  i = ~ INTVAL (op);
  if (mode == QImode)
    i &= 0xff;
  if (mode == HImode)
    i &= 0xffff;
  return exact_log2 (i) != -1;
})

;; Predicate for constants with exactly one bit set.

(define_predicate "xstormy16_onebit_set_operand"
  (match_code "const_int")
{
  HOST_WIDE_INT i;
  if (GET_CODE (op) != CONST_INT)
    return 0;
  i = INTVAL (op);
  if (mode == QImode)
    i &= 0xff;
  if (mode == HImode)
    i &= 0xffff;
  return exact_log2 (i) != -1;
})

;; TODO: Add a comment here.

(define_predicate "nonimmediate_nonstack_operand"
  (match_code "reg,mem,subreg")
{
  /* 'Q' is for pushes, 'R' for pops.  */
  return (nonimmediate_operand (op, mode) 
	  && ! xstormy16_extra_constraint_p (op, 'Q')
	  && ! xstormy16_extra_constraint_p (op, 'R'));
})
