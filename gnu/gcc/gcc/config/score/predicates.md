;; Predicate definitions for Sunplus S+CORE.
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

(define_predicate "arith_operand"
  (ior (match_code "const_int")
       (match_operand 0 "register_operand")))

(define_predicate "const_call_insn_operand"
  (match_code "const,symbol_ref,label_ref")
{
  enum score_symbol_type symbol_type;

  return (mda_symbolic_constant_p (op, &symbol_type)
          && (symbol_type == SYMBOL_GENERAL));
})

(define_predicate "call_insn_operand"
  (ior (match_operand 0 "const_call_insn_operand")
       (match_operand 0 "register_operand")))

(define_predicate "const_uimm5"
  (match_code "const_int")
{
  return IMM_IN_RANGE (INTVAL (op), 5, 0);
})

(define_predicate "hireg_operand"
  (and (match_code "reg")
       (match_test "REGNO (op) == HI_REGNUM")))

(define_predicate "loreg_operand"
  (and (match_code "reg")
       (match_test "REGNO (op) == LO_REGNUM")))

(define_predicate "sr0_operand"
  (and (match_code "reg")
       (match_test "REGNO (op) == CN_REGNUM")))

(define_predicate "g32reg_operand"
  (and (match_code "reg")
       (match_test "GP_REG_P (REGNO (op))")))

(define_predicate "branch_n_operator"
  (match_code "lt,ge"))

(define_predicate "branch_nz_operator"
  (match_code "eq,ne,lt,ge"))

(define_predicate "const_simm12"
  (match_code "const_int")
{
  return IMM_IN_RANGE (INTVAL (op), 12, 1);
})

(define_predicate "const_simm15"
  (match_code "const_int")
{
  return IMM_IN_RANGE (INTVAL (op), 15, 1);
})

