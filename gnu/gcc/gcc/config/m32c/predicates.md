;; Machine Descriptions for R8C/M16C/M32C
;; Copyright (C) 2005
;; Free Software Foundation, Inc.
;; Contributed by Red Hat.
;;
;; This file is part of GCC.
;;
;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published
;; by the Free Software Foundation; either version 2, or (at your
;; option) any later version.
;;
;; GCC is distributed in the hope that it will be useful, but WITHOUT
;; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;; or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
;; License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to the Free
;; Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
;; 02110-1301, USA.

;; Predicates

; TRUE for any valid operand.  We do this because general_operand
; refuses to match volatile memory refs.

(define_predicate "m32c_any_operand"
  (ior (match_operand 0 "general_operand")
       (match_operand 1 "memory_operand")))

; Likewise for nonimmediate_operand.

(define_predicate "m32c_nonimmediate_operand"
  (ior (match_operand 0 "nonimmediate_operand")
       (match_operand 1 "memory_operand")))

; TRUE if the operand is a pseudo-register.
(define_predicate "m32c_pseudo"
  (ior (and (match_code "reg")
	    (match_test "REGNO(op) >= FIRST_PSEUDO_REGISTER"))
       (and (match_code "subreg")
	    (and (match_test "GET_CODE (XEXP (op, 0)) == REG")
		 (match_test "REGNO(XEXP (op,0)) >= FIRST_PSEUDO_REGISTER")))))
       

; Returning true causes many predicates to NOT match.  We allow
; subregs for type changing, but not for size changing.
(define_predicate "m32c_wide_subreg"
  (and (match_code "subreg")
       (not (match_operand 0 "m32c_pseudo")))
  {
    unsigned int sizeo = GET_MODE_SIZE (GET_MODE (op));
    unsigned int sizei = GET_MODE_SIZE (GET_MODE (XEXP (op, 0)));
    sizeo = (sizeo + UNITS_PER_WORD - 1) / UNITS_PER_WORD;
    sizei = (sizei + UNITS_PER_WORD - 1) / UNITS_PER_WORD;
    return sizeo != sizei;
  })

; TRUE for r0 through r3, or a pseudo that reload could put in r0
; through r3 (likewise for the next couple too)
(define_predicate "r0123_operand"
  (ior (match_operand 0 "m32c_pseudo" "")
       (and (match_code "reg")
	    (match_test "REGNO(op) <= R3_REGNO"))))

; TRUE for r0
(define_predicate "m32c_r0_operand"
  (ior (match_operand 0 "m32c_pseudo" "")
       (and (match_code "reg")
	    (match_test "REGNO(op) == R0_REGNO"))))

; TRUE for r1
(define_predicate "m32c_r1_operand"
  (ior (match_operand 0 "m32c_pseudo" "")
       (and (match_code "reg")
	    (match_test "REGNO(op) == R1_REGNO"))))

; TRUE for HL_CLASS (r0 or r1)
(define_predicate "m32c_hl_operand"
  (ior (match_operand 0 "m32c_pseudo" "")
       (and (match_code "reg")
	    (match_test "REGNO(op) == R0_REGNO || REGNO(op) == R1_REGNO"))))


; TRUE for r2
(define_predicate "m32c_r2_operand"
  (ior (match_operand 0 "m32c_pseudo" "")
       (and (match_code "reg")
	    (match_test "REGNO(op) == R2_REGNO"))))

; TRUE for r3
(define_predicate "m32c_r3_operand"
  (ior (match_operand 0 "m32c_pseudo" "")
       (and (match_code "reg")
	    (match_test "REGNO(op) == R3_REGNO"))))

; TRUE for any general operand except r2.
(define_predicate "m32c_notr2_operand"
  (and (match_operand 0 "general_operand")
       (ior (not (match_code "reg"))
	    (match_test "REGNO(op) != R2_REGNO"))))

; TRUE for the stack pointer.
(define_predicate "m32c_sp_operand"
  (ior (match_operand 0 "m32c_pseudo" "")
       (and (match_code "reg")
	    (match_test "REGNO(op) == SP_REGNO"))))

; TRUE for control registers.
(define_predicate "cr_operand"
  (match_code "reg")
  "return (REGNO (op) >= SB_REGNO
           && REGNO (op) <= FLG_REGNO);")

; TRUE for $a0 or $a1.
(define_predicate "a_operand"
  (and (match_code "reg")
       (match_test "REGNO (op) == A0_REGNO || REGNO (op) == A1_REGNO")))

; TRUE for $a0 or $a1 or a pseudo
(define_predicate "ap_operand"
  (ior (match_operand 0 "m32c_pseudo" "")
       (and (match_code "reg")
	    (match_test "REGNO (op) == A0_REGNO || REGNO (op) == A1_REGNO"))))

; TRUE for r0 through r3, or a0 or a1.
(define_predicate "ra_operand"
  (and (and (match_operand 0 "register_operand" "")
	    (not (match_operand 1 "cr_operand" "")))
       (not (match_operand 2 "m32c_wide_subreg" ""))))

; Likewise, plus TRUE for memory references.
(define_predicate "mra_operand"
  (and (and (match_operand 0 "nonimmediate_operand" "")
	    (not (match_operand 1 "cr_operand" "")))
       (not (match_operand 2 "m32c_wide_subreg" ""))))

; Likewise, plus TRUE for subregs.
(define_predicate "mras_operand"
  (and (match_operand 0 "nonimmediate_operand" "")
       (not (match_operand 1 "cr_operand" ""))))

; As above, but no push/pop operations
(define_predicate "mra_nopp_operand"
  (match_operand 0 "mra_operand" "")
{
  if (GET_CODE (op) == MEM
      && (GET_CODE (XEXP (op, 0)) == PRE_DEC
	  || (GET_CODE (XEXP (op, 0)) == POST_INC)))
    return 0;
  return 1;
})

; TRUE for memory, r0..r3, a0..a1, or immediates.
(define_predicate "mrai_operand"
  (and (and (match_operand 0 "m32c_any_operand" "")
	    (not (match_operand 1 "cr_operand" "")))
       (not (match_operand 2 "m32c_wide_subreg" ""))))

; Likewise, plus true for subregs.
(define_predicate "mrasi_operand"
  (and (match_operand 0 "general_operand" "")
       (not (match_operand 1 "cr_operand" ""))))

; TRUE for r0..r3 or memory.
(define_predicate "mr_operand"
  (and (match_operand 0 "mra_operand" "")
       (not (match_operand 1 "a_operand" ""))))

; TRUE for a0..a1 or memory.
(define_predicate "ma_operand"
  (ior (match_operand 0 "a_operand" "")
       (match_operand 1 "memory_operand" "")))

; TRUE for memory operands that are not indexed
(define_predicate "memsym_operand"
  (and (match_operand 0 "memory_operand" "")
       (match_test "m32c_extra_constraint_p (op, 'S', \"Si\")")))

; TRUE for memory operands with small integer addresses
(define_predicate "memimmed_operand"
  (and (match_operand 0 "memory_operand" "")
       (match_test "m32c_extra_constraint_p (op, 'S', \"Sp\")")))

; TRUE for r1h.  This is complicated since r1h isn't a register GCC
; normally knows about.
(define_predicate "r1h_operand"
  (match_code "zero_extract")
  {
    rtx reg = XEXP (op, 0);
    rtx size = XEXP (op, 1);
    rtx pos = XEXP (op, 2);
    return (GET_CODE (reg) == REG
	    && REGNO (reg) == R1_REGNO
	    && GET_CODE (size) == CONST_INT
	    && INTVAL (size) == 8
	    && GET_CODE (pos) == CONST_INT
	    && INTVAL (pos) == 8);
  })

; TRUE if we can shift by this amount.  Constant shift counts have a
; limited range.
(define_predicate "shiftcount_operand"
  (ior (match_operand 0 "mra_operand" "")
       (and (match_operand 2 "const_int_operand" "")
	    (match_test "-8 <= INTVAL (op) && INTVAL (op) && INTVAL (op) <= 8"))))
(define_predicate "longshiftcount_operand"
  (ior (match_operand 0 "mra_operand" "")
       (and (match_operand 2 "const_int_operand" "")
	    (match_test "-32 <= INTVAL (op) && INTVAL (op) && INTVAL (op) <= 32"))))

; TRUE for r0..r3, a0..a1, or sp.
(define_predicate "mra_or_sp_operand"
  (and (ior (match_operand 0 "mra_operand")
	    (match_operand 1 "m32c_sp_operand"))
       (not (match_operand 2 "m32c_wide_subreg" ""))))


; TRUE for r2 or r3.
(define_predicate "m32c_r2r3_operand"
  (ior (and (match_code "reg")
	    (ior (match_test "REGNO(op) == R2_REGNO")
		 (match_test "REGNO(op) == R3_REGNO")))
       (and (match_code "subreg")
	    (match_test "GET_CODE (XEXP (op, 0)) == REG && (REGNO (XEXP (op, 0)) == R2_REGNO || REGNO (XEXP (op, 0)) == R3_REGNO)"))))

; Likewise, plus TRUE for a0..a1.
(define_predicate "m32c_r2r3a_operand"
  (ior (match_operand 0 "m32c_r2r3_operand" "")
       (match_operand 0 "a_operand" "")))

; These two are only for movqi - no subreg limit
(define_predicate "mra_qi_operand"
  (and (and (match_operand 0 "m32c_nonimmediate_operand" "")
	    (not (match_operand 1 "cr_operand" "")))
       (not (match_operand 1 "m32c_r2r3a_operand" ""))))

(define_predicate "mrai_qi_operand"
  (and (and (match_operand 0 "m32c_any_operand" "")
	    (not (match_operand 1 "cr_operand" "")))
       (not (match_operand 1 "m32c_r2r3a_operand" ""))))

(define_predicate "a_qi_operand"
  (ior (match_operand 0 "m32c_pseudo" "")
       (match_operand 1 "a_operand" "")))

; TRUE for comparisons we support.
(define_predicate "m32c_cmp_operator"
  (match_code "eq,ne,gt,gtu,lt,ltu,ge,geu,le,leu"))

(define_predicate "m32c_eqne_operator"
  (match_code "eq,ne"))

; TRUE for mem0
(define_predicate "m32c_mem0_operand"
  (ior (match_operand 0 "m32c_pseudo" "")
       (and (match_code "reg")
	    (match_test "REGNO(op) == MEM0_REGNO"))))

; TRUE for things the call patterns can return.
(define_predicate "m32c_return_operand"
  (ior (match_operand 0 "m32c_r0_operand")
       (ior (match_operand 0 "m32c_mem0_operand")
	    (match_code "parallel"))))

; TRUE for constants we can multiply pointers by
(define_predicate "m32c_psi_scale"
  (and (match_operand 0 "const_int_operand")
       (match_test "m32c_const_ok_for_constraint_p(INTVAL(op), 'I', \"Ilb\")")))

; TRUE for one bit set (bit) or clear (mask) out of N bits.

(define_predicate "m32c_1bit8_operand"
  (and (match_operand 0 "const_int_operand")
       (match_test "m32c_const_ok_for_constraint_p(INTVAL(op), 'I', \"Ilb\")")))

(define_predicate "m32c_1bit16_operand"
  (and (match_operand 0 "const_int_operand")
       (match_test "m32c_const_ok_for_constraint_p(INTVAL(op), 'I', \"Ilw\")")))

(define_predicate "m32c_1mask8_operand"
  (and (match_operand 0 "const_int_operand")
       (match_test "m32c_const_ok_for_constraint_p(INTVAL(op), 'I', \"Imb\")")))

(define_predicate "m32c_1mask16_operand"
  (and (match_operand 0 "const_int_operand")
       (match_test "m32c_const_ok_for_constraint_p(INTVAL(op), 'I', \"Imw\")")))
