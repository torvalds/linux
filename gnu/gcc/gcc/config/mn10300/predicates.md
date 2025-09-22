;; Predicate definitions for Matsushita MN10300.
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

;; Return true if the operand is the 1.0f constant.

(define_predicate "const_1f_operand"
  (match_code "const_int,const_double")
{
  return (op == CONST1_RTX (SFmode));
})

;; Return 1 if X is a CONST_INT that is only 8 bits wide.  This is
;; used for the btst insn which may examine memory or a register (the
;; memory variant only allows an unsigned 8 bit integer).

(define_predicate "const_8bit_operand"
  (match_code "const_int")
{
  return (GET_CODE (op) == CONST_INT
	  && INTVAL (op) >= 0
	  && INTVAL (op) < 256);
})

;; Return true if OP is a valid call operand.

(define_predicate "call_address_operand"
  (match_code "symbol_ref,reg,unspec")
{
  if (flag_pic)
    return (EXTRA_CONSTRAINT (op, 'S') || GET_CODE (op) == REG);

  return (GET_CODE (op) == SYMBOL_REF || GET_CODE (op) == REG);
})
