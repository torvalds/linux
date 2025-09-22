;; Scheduling description for Alpha EV6.
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

; EV6 can issue 4 insns per clock.  It's out-of-order, so this isn't
; expected to help over-much, but a precise description can be important
; for software pipelining.
;
; EV6 has two symmetric pairs ("clusters") of two asymmetric integer
; units ("upper" and "lower"), yielding pipe names U0, U1, L0, L1.
;
; ??? The clusters have independent register files that are re-synced
; every cycle.  Thus there is one additional cycle of latency between
; insns issued on different clusters.  Possibly model that by duplicating
; all EBOX insn_reservations that can issue to either cluster, increasing
; all latencies by one, and adding bypasses within the cluster.
;
; ??? In addition, instruction order affects cluster issue.

(define_automaton "ev6_0,ev6_1")
(define_cpu_unit "ev6_u0,ev6_u1,ev6_l0,ev6_l1" "ev6_0")
(define_reservation "ev6_u" "ev6_u0|ev6_u1")
(define_reservation "ev6_l" "ev6_l0|ev6_l1")
(define_reservation "ev6_ebox" "ev6_u|ev6_l")

(define_cpu_unit "ev6_fa" "ev6_1")
(define_cpu_unit "ev6_fm,ev6_fst0,ev6_fst1" "ev6_0")
(define_reservation "ev6_fst" "ev6_fst0|ev6_fst1")

; Assume type "multi" single issues.
(define_insn_reservation "ev6_multi" 1
  (and (eq_attr "tune" "ev6")
       (eq_attr "type" "multi"))
  "ev6_u0+ev6_u1+ev6_l0+ev6_l1+ev6_fa+ev6_fm+ev6_fst0+ev6_fst1")

; Integer loads take at least 3 clocks, and only issue to lower units.
; adjust_cost still factors in user-specified memory latency, so return 1 here.
(define_insn_reservation "ev6_ild" 1
  (and (eq_attr "tune" "ev6")
       (eq_attr "type" "ild,ldsym,ld_l"))
  "ev6_l")

(define_insn_reservation "ev6_ist" 1
  (and (eq_attr "tune" "ev6")
       (eq_attr "type" "ist,st_c"))
  "ev6_l")

(define_insn_reservation "ev6_mb" 1
  (and (eq_attr "tune" "ev6")
       (eq_attr "type" "mb"))
  "ev6_l1")

; FP loads take at least 4 clocks.  adjust_cost still factors
; in user-specified memory latency, so return 2 here.
(define_insn_reservation "ev6_fld" 2
  (and (eq_attr "tune" "ev6")
       (eq_attr "type" "fld"))
  "ev6_l")

; The FPU communicates with memory and the integer register file
; via two fp store units.  We need a slot in the fst immediately, and
; a slot in LOW after the operand data is ready.  At which point the
; data may be moved either to the store queue or the integer register
; file and the insn retired.

(define_insn_reservation "ev6_fst" 3
  (and (eq_attr "tune" "ev6")
       (eq_attr "type" "fst"))
  "ev6_fst,nothing,ev6_l")

; Arithmetic goes anywhere.
(define_insn_reservation "ev6_arith" 1
  (and (eq_attr "tune" "ev6")
       (eq_attr "type" "iadd,ilog,icmp"))
  "ev6_ebox")

; Motion video insns also issue only to U0, and take three ticks.
(define_insn_reservation "ev6_mvi" 3
  (and (eq_attr "tune" "ev6")
       (eq_attr "type" "mvi"))
  "ev6_u0")

; Shifts issue to upper units.
(define_insn_reservation "ev6_shift" 1
  (and (eq_attr "tune" "ev6")
       (eq_attr "type" "shift"))
  "ev6_u")

; Multiplies issue only to U1, and all take 7 ticks.
(define_insn_reservation "ev6_imul" 7
  (and (eq_attr "tune" "ev6")
       (eq_attr "type" "imul"))
  "ev6_u1")

; Conditional moves decompose into two independent primitives, each taking
; one cycle.  Since ev6 is out-of-order, we can't see anything but two cycles.
(define_insn_reservation "ev6_icmov" 2
  (and (eq_attr "tune" "ev6")
       (eq_attr "type" "icmov"))
  "ev6_ebox,ev6_ebox")

; Integer branches issue to upper units
(define_insn_reservation "ev6_ibr" 1
  (and (eq_attr "tune" "ev6")
       (eq_attr "type" "ibr,callpal"))
  "ev6_u")

; Calls only issue to L0.
(define_insn_reservation "ev6_jsr" 1
  (and (eq_attr "tune" "ev6")
       (eq_attr "type" "jsr"))
  "ev6_l0")

; Ftoi/itof only issue to lower pipes.
(define_insn_reservation "ev6_itof" 3
  (and (eq_attr "tune" "ev6")
       (eq_attr "type" "itof"))
  "ev6_l")

(define_insn_reservation "ev6_ftoi" 3
  (and (eq_attr "tune" "ev6")
       (eq_attr "type" "ftoi"))
  "ev6_fst,nothing,ev6_l")

(define_insn_reservation "ev6_fmul" 4
  (and (eq_attr "tune" "ev6")
       (eq_attr "type" "fmul"))
  "ev6_fm")

(define_insn_reservation "ev6_fadd" 4
  (and (eq_attr "tune" "ev6")
       (eq_attr "type" "fadd,fcpys,fbr"))
  "ev6_fa")

(define_insn_reservation "ev6_fcmov" 8
  (and (eq_attr "tune" "ev6")
       (eq_attr "type" "fcmov"))
  "ev6_fa,nothing*3,ev6_fa")

(define_insn_reservation "ev6_fdivsf" 12
  (and (eq_attr "tune" "ev6")
       (and (eq_attr "type" "fdiv")
	    (eq_attr "opsize" "si")))
  "ev6_fa*9")

(define_insn_reservation "ev6_fdivdf" 15
  (and (eq_attr "tune" "ev6")
       (and (eq_attr "type" "fdiv")
	    (eq_attr "opsize" "di")))
  "ev6_fa*12")

(define_insn_reservation "ev6_sqrtsf" 18
  (and (eq_attr "tune" "ev6")
       (and (eq_attr "type" "fsqrt")
	    (eq_attr "opsize" "si")))
  "ev6_fa*15")

(define_insn_reservation "ev6_sqrtdf" 33
  (and (eq_attr "tune" "ev6")
       (and (eq_attr "type" "fsqrt")
	    (eq_attr "opsize" "di")))
  "ev6_fa*30")
