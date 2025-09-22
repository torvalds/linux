;; Scheduling description for Alpha EV5.
;;   Copyright (C) 2002, 2004, 2005 Free Software Foundation, Inc.
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

;; EV5 has two asymmetric integer units, E0 and E1, plus separate
;; FP add and multiply units.

(define_automaton "ev5_0,ev5_1")
(define_cpu_unit "ev5_e0,ev5_e1,ev5_fa,ev5_fm" "ev5_0")
(define_reservation "ev5_e01" "ev5_e0|ev5_e1")
(define_reservation "ev5_fam" "ev5_fa|ev5_fm")
(define_cpu_unit "ev5_imul" "ev5_0")
(define_cpu_unit "ev5_fdiv" "ev5_1")

; Assume type "multi" single issues.
(define_insn_reservation "ev5_multi" 1
  (and (eq_attr "tune" "ev5")
       (eq_attr "type" "multi"))
  "ev5_e0+ev5_e1+ev5_fa+ev5_fm")

; Stores can only issue to E0, and may not issue with loads.
; Model this with some fake units.

(define_cpu_unit "ev5_l0,ev5_l1,ev5_st" "ev5_0")
(define_reservation "ev5_ld" "ev5_l0|ev5_l1")
(exclusion_set "ev5_l0,ev5_l1" "ev5_st")

(define_insn_reservation "ev5_st" 1
  (and (eq_attr "tune" "ev5")
       (eq_attr "type" "ist,fst,st_c,mb"))
  "ev5_e0+ev5_st")

; Loads from L0 complete in two cycles.  adjust_cost still factors
; in user-specified memory latency, so return 1 here.
(define_insn_reservation "ev5_ld" 1
  (and (eq_attr "tune" "ev5")
       (eq_attr "type" "ild,fld,ldsym"))
  "ev5_e01+ev5_ld")

(define_insn_reservation "ev5_ld_l" 1
  (and (eq_attr "tune" "ev5")
       (eq_attr "type" "ld_l"))
  "ev5_e0+ev5_ld")

; Integer branches slot only to E1.
(define_insn_reservation "ev5_ibr" 1
  (and (eq_attr "tune" "ev5")
       (eq_attr "type" "ibr"))
  "ev5_e1")

(define_insn_reservation "ev5_callpal" 1
  (and (eq_attr "tune" "ev5")
       (eq_attr "type" "callpal"))
  "ev5_e1")

(define_insn_reservation "ev5_jsr" 1
  (and (eq_attr "tune" "ev5")
       (eq_attr "type" "jsr"))
  "ev5_e1")

(define_insn_reservation "ev5_shift" 1
  (and (eq_attr "tune" "ev5")
       (eq_attr "type" "shift"))
  "ev5_e0")

(define_insn_reservation "ev5_mvi" 2
  (and (eq_attr "tune" "ev5")
       (eq_attr "type" "mvi"))
  "ev5_e0")

(define_insn_reservation "ev5_cmov" 2
  (and (eq_attr "tune" "ev5")
       (eq_attr "type" "icmov"))
  "ev5_e01")

(define_insn_reservation "ev5_iadd" 1
  (and (eq_attr "tune" "ev5")
       (eq_attr "type" "iadd"))
  "ev5_e01")

(define_insn_reservation "ev5_ilogcmp" 1
  (and (eq_attr "tune" "ev5")
       (eq_attr "type" "ilog,icmp"))
  "ev5_e01")

; Conditional move and branch can issue the same cycle as the test.
(define_bypass 0 "ev5_ilogcmp" "ev5_ibr,ev5_cmov" "if_test_bypass_p")

; Multiplies use a non-pipelined imul unit.  Also, "no insn can be issued
; to E0 exactly two cycles before an integer multiply completes".

(define_insn_reservation "ev5_imull" 8
  (and (eq_attr "tune" "ev5")
       (and (eq_attr "type" "imul")
	    (eq_attr "opsize" "si")))
  "ev5_e0+ev5_imul,ev5_imul*3,nothing,ev5_e0")

(define_insn_reservation "ev5_imulq" 12
  (and (eq_attr "tune" "ev5")
       (and (eq_attr "type" "imul")
	    (eq_attr "opsize" "di")))
  "ev5_e0+ev5_imul,ev5_imul*7,nothing,ev5_e0")

(define_insn_reservation "ev5_imulh" 14
  (and (eq_attr "tune" "ev5")
       (and (eq_attr "type" "imul")
	    (eq_attr "opsize" "udi")))
  "ev5_e0+ev5_imul,ev5_imul*7,nothing*3,ev5_e0")

; The multiplier is unable to receive data from Ebox bypass paths.  The
; instruction issues at the expected time, but its latency is increased
; by the time it takes for the input data to become available to the
; multiplier.  For example, an IMULL instruction issued one cycle later
; than an ADDL instruction, which produced one of its operands, has a
; latency of 10 (8 + 2).  If the IMULL instruction is issued two cycles
; later than the ADDL instruction, the latency is 9 (8 + 1).
;
; Model this instead with increased latency on the input instruction.

(define_bypass 3
  "ev5_ld,ev5_ld_l,ev5_shift,ev5_mvi,ev5_cmov,ev5_iadd,ev5_ilogcmp"
  "ev5_imull,ev5_imulq,ev5_imulh")

(define_bypass  9 "ev5_imull" "ev5_imull,ev5_imulq,ev5_imulh")
(define_bypass 13 "ev5_imulq" "ev5_imull,ev5_imulq,ev5_imulh")
(define_bypass 15 "ev5_imulh" "ev5_imull,ev5_imulq,ev5_imulh")

; Similarly for the FPU we have two asymmetric units.

(define_insn_reservation "ev5_fadd" 4
  (and (eq_attr "tune" "ev5")
       (eq_attr "type" "fadd,fcmov"))
  "ev5_fa")

(define_insn_reservation "ev5_fbr" 1
  (and (eq_attr "tune" "ev5")
       (eq_attr "type" "fbr"))
  "ev5_fa")

(define_insn_reservation "ev5_fcpys" 4
  (and (eq_attr "tune" "ev5")
       (eq_attr "type" "fcpys"))
  "ev5_fam")

(define_insn_reservation "ev5_fmul" 4
  (and (eq_attr "tune" "ev5")
       (eq_attr "type" "fmul"))
  "ev5_fm")

; The floating point divider is not pipelined.  Also, "no insn can be issued
; to FA exactly five before an fdiv insn completes".
;
; ??? Do not model this late reservation due to the enormously increased
; size of the resulting DFA.
;
; ??? Putting ev5_fa and ev5_fdiv alone into the same automata produces
; a DFA of acceptable size, but putting ev5_fm and ev5_fa into separate
; automata produces incorrect results for insns that can choose one or
; the other, i.e. ev5_fcpys.

(define_insn_reservation "ev5_fdivsf" 15
  (and (eq_attr "tune" "ev5")
       (and (eq_attr "type" "fdiv")
	    (eq_attr "opsize" "si")))
  ; "ev5_fa+ev5_fdiv,ev5_fdiv*9,ev5_fa+ev5_fdiv,ev5_fdiv*4"
  "ev5_fa+ev5_fdiv,ev5_fdiv*14")

(define_insn_reservation "ev5_fdivdf" 22
  (and (eq_attr "tune" "ev5")
       (and (eq_attr "type" "fdiv")
	    (eq_attr "opsize" "di")))
  ; "ev5_fa+ev5_fdiv,ev5_fdiv*17,ev5_fa+ev5_fdiv,ev5_fdiv*4"
  "ev5_fa+ev5_fdiv,ev5_fdiv*21")

; Traps don't consume or produce data; rpcc is latency 2 if we ever add it.
(define_insn_reservation "ev5_misc" 2
  (and (eq_attr "tune" "ev5")
       (eq_attr "type" "misc"))
  "ev5_e0")
