;; Constraint definitions for ARM and Thumb
;; Copyright (C) 2006 Free Software Foundation, Inc.
;; Contributed by ARM Ltd.

;; This file is part of GCC.

;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published
;; by the Free Software Foundation; either version 2, or (at your
;; option) any later version.

;; GCC is distributed in the hope that it will be useful, but WITHOUT
;; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;; or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
;; License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to
;; the Free Software Foundation, 51 Franklin Street, Fifth Floor,
;; Boston, MA 02110-1301, USA.

;; The following register constraints have been used:
;; - in ARM state: f, v, w, y, z
;; - in Thumb state: h, k, b
;; - in both states: l, c
;; In ARM state, 'l' is an alias for 'r'

;; The following normal constraints have been used:
;; in ARM state: G, H, I, J, K, L, M
;; in Thumb state: I, J, K, L, M, N, O

;; The following multi-letter normal constraints have been used:
;; in ARM state: Da, Db, Dc

;; The following memory constraints have been used:
;; in ARM state: Q, Uq, Uv, Uy


(define_register_constraint "f" "TARGET_ARM ? FPA_REGS : NO_REGS"
 "Legacy FPA registers @code{f0}-@code{f7}.")

(define_register_constraint "v" "TARGET_ARM ? CIRRUS_REGS : NO_REGS"
 "The Cirrus Maverick co-processor registers.")

(define_register_constraint "w" "TARGET_ARM ? VFP_REGS : NO_REGS"
 "The VFP registers @code{s0}-@code{s31}.")

(define_register_constraint "y" "TARGET_REALLY_IWMMXT ? IWMMXT_REGS : NO_REGS"
 "The Intel iWMMX co-processor registers.")

(define_register_constraint "z"
 "TARGET_REALLY_IWMMXT ? IWMMXT_GR_REGS : NO_REGS"
 "The Intel iWMMX GR registers.")

(define_register_constraint "l" "TARGET_THUMB ? LO_REGS : GENERAL_REGS"
 "In Thumb state the core registers @code{r0}-@code{r7}.")

(define_register_constraint "h" "TARGET_THUMB ? HI_REGS : NO_REGS"
 "In Thumb state the core registers @code{r8}-@code{r15}.")

(define_register_constraint "k" "TARGET_THUMB ? STACK_REG : NO_REGS"
 "@internal
  Thumb only.  The stack register.")

(define_register_constraint "b" "TARGET_THUMB ? BASE_REGS : NO_REGS"
 "@internal
  Thumb only.  The union of the low registers and the stack register.")

(define_register_constraint "c" "CC_REG"
 "@internal The condition code register.")

(define_constraint "I"
 "In ARM state a constant that can be used as an immediate value in a Data
  Processing instruction.  In Thumb state a constant in the range 0-255."
 (and (match_code "const_int")
      (match_test "TARGET_ARM ? const_ok_for_arm (ival)
		   : ival >= 0 && ival <= 255")))

(define_constraint "J"
 "In ARM state a constant in the range @minus{}4095-4095.  In Thumb state
  a constant in the range @minus{}255-@minus{}1."
 (and (match_code "const_int")
      (match_test "TARGET_ARM ? (ival >= -4095 && ival <= 4095)
		   : (ival >= -255 && ival <= -1)")))

(define_constraint "K"
 "In ARM state a constant that satisfies the @code{I} constraint if inverted.
  In Thumb state a constant that satisfies the @code{I} constraint multiplied 
  by any power of 2."
 (and (match_code "const_int")
      (match_test "TARGET_ARM ? const_ok_for_arm (~ival)
		   : thumb_shiftable_const (ival)")))

(define_constraint "L"
 "In ARM state a constant that satisfies the @code{I} constraint if negated.
  In Thumb state a constant in the range @minus{}7-7."
 (and (match_code "const_int")
      (match_test "TARGET_ARM ? const_ok_for_arm (-ival)
		   : (ival >= -7 && ival <= 7)")))

;; The ARM state version is internal...
;; @internal In ARM state a constant in the range 0-32 or any power of 2.
(define_constraint "M"
 "In Thumb state a constant that is a multiple of 4 in the range 0-1020."
 (and (match_code "const_int")
      (match_test "TARGET_ARM ? ((ival >= 0 && ival <= 32)
				 || ((ival & (ival - 1)) == 0))
		   : ((ival >= 0 && ival <= 1020) && ((ival & 3) == 0))")))

(define_constraint "N"
 "In Thumb state a constant in the range 0-31."
 (and (match_code "const_int")
      (match_test "TARGET_THUMB && ival >= 0 && ival <= 31")))

(define_constraint "O"
 "In Thumb state a constant that is a multiple of 4 in the range
  @minus{}508-508."
 (and (match_code "const_int")
      (match_test "TARGET_THUMB && ival >= -508 && ival <= 508
		   && ((ival & 3) == 0)")))

(define_constraint "G"
 "In ARM state a valid FPA immediate constant."
 (and (match_code "const_double")
      (match_test "TARGET_ARM && arm_const_double_rtx (op)")))

(define_constraint "H"
 "In ARM state a valid FPA immediate constant when negated."
 (and (match_code "const_double")
      (match_test "TARGET_ARM && neg_const_double_rtx_ok_for_fpa (op)")))

(define_constraint "Da"
 "@internal
  In ARM state a const_int, const_double or const_vector that can
  be generated with two Data Processing insns."
 (and (match_code "const_double,const_int,const_vector")
      (match_test "TARGET_ARM && arm_const_double_inline_cost (op) == 2")))

(define_constraint "Db"
 "@internal
  In ARM state a const_int, const_double or const_vector that can
  be generated with three Data Processing insns."
 (and (match_code "const_double,const_int,const_vector")
      (match_test "TARGET_ARM && arm_const_double_inline_cost (op) == 3")))

(define_constraint "Dc"
 "@internal
  In ARM state a const_int, const_double or const_vector that can
  be generated with four Data Processing insns.  This pattern is disabled
  if optimizing for space or when we have load-delay slots to fill."
 (and (match_code "const_double,const_int,const_vector")
      (match_test "TARGET_ARM && arm_const_double_inline_cost (op) == 4
		   && !(optimize_size || arm_ld_sched)")))

(define_memory_constraint "Uv"
 "@internal
  In ARM state a valid VFP load/store address."
 (and (match_code "mem")
      (match_test "TARGET_ARM && arm_coproc_mem_operand (op, FALSE)")))

(define_memory_constraint "Uy"
 "@internal
  In ARM state a valid iWMMX load/store address."
 (and (match_code "mem")
      (match_test "TARGET_ARM && arm_coproc_mem_operand (op, TRUE)")))

(define_memory_constraint "Uq"
 "@internal
  In ARM state an address valid in ldrsb instructions."
 (and (match_code "mem")
      (match_test "TARGET_ARM
		   && arm_legitimate_address_p (GET_MODE (op), XEXP (op, 0),
						SIGN_EXTEND, 0)")))

(define_memory_constraint "Q"
 "@internal
  In ARM state an address that is a single base register."
 (and (match_code "mem")
      (match_test "REG_P (XEXP (op, 0))")))

;; We used to have constraint letters for S and R in ARM state, but
;; all uses of these now appear to have been removed.

;; Additionally, we used to have a Q constraint in Thumb state, but
;; this wasn't really a valid memory constraint.  Again, all uses of
;; this now seem to have been removed.
