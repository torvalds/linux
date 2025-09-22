;; Scheduling description for Alpha EV4.
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

; On EV4 there are two classes of resources to consider: resources needed
; to issue, and resources needed to execute.  IBUS[01] are in the first
; category.  ABOX, BBOX, EBOX, FBOX, IMUL & FDIV make up the second.
; (There are a few other register-like resources, but ...)

(define_automaton "ev4_0,ev4_1,ev4_2")
(define_cpu_unit "ev4_ib0,ev4_ib1,ev4_abox,ev4_bbox" "ev4_0")
(define_cpu_unit "ev4_ebox,ev4_imul" "ev4_1")
(define_cpu_unit "ev4_fbox,ev4_fdiv" "ev4_2")
(define_reservation "ev4_ib01" "ev4_ib0|ev4_ib1")

; Assume type "multi" single issues.
(define_insn_reservation "ev4_multi" 1
  (and (eq_attr "tune" "ev4")
       (eq_attr "type" "multi"))
  "ev4_ib0+ev4_ib1")

; Loads from L0 completes in three cycles.  adjust_cost still factors
; in user-specified memory latency, so return 1 here.
(define_insn_reservation "ev4_ld" 1
  (and (eq_attr "tune" "ev4")
       (eq_attr "type" "ild,fld,ldsym,ld_l"))
  "ev4_ib01+ev4_abox")

; Stores can issue before the data (but not address) is ready.
(define_insn_reservation "ev4_ist" 1
  (and (eq_attr "tune" "ev4")
       (eq_attr "type" "ist"))
  "ev4_ib1+ev4_abox")

; ??? Separate from ev4_ist because store_data_bypass_p can't handle
; the patterns with multiple sets, like store-conditional.
(define_insn_reservation "ev4_ist_c" 1
  (and (eq_attr "tune" "ev4")
       (eq_attr "type" "st_c"))
  "ev4_ib1+ev4_abox")

(define_insn_reservation "ev4_fst" 1
  (and (eq_attr "tune" "ev4")
       (eq_attr "type" "fst"))
  "ev4_ib0+ev4_abox")

; Memory barrier blocks ABOX insns until it's acknowledged by the external
; memory bus.  This may be *quite* slow.  Setting this to 4 cycles gets
; about all the benefit without making the DFA too large.
(define_insn_reservation "ev4_mb" 4
  (and (eq_attr "tune" "ev4")
       (eq_attr "type" "mb"))
  "ev4_ib1+ev4_abox,ev4_abox*3")

; Branches have no delay cost, but do tie up the unit for two cycles.
(define_insn_reservation "ev4_ibr" 2
  (and (eq_attr "tune" "ev4")
       (eq_attr "type" "ibr,jsr"))
  "ev4_ib1+ev4_bbox,ev4_bbox")

(define_insn_reservation "ev4_callpal" 2
  (and (eq_attr "tune" "ev4")
       (eq_attr "type" "callpal"))
  "ev4_ib1+ev4_bbox,ev4_bbox")

(define_insn_reservation "ev4_fbr" 2
  (and (eq_attr "tune" "ev4")
       (eq_attr "type" "fbr"))
  "ev4_ib0+ev4_bbox,ev4_bbox")

; Arithmetic insns are normally have their results available after
; two cycles.  There are a number of exceptions.

(define_insn_reservation "ev4_iaddlog" 2
  (and (eq_attr "tune" "ev4")
       (eq_attr "type" "iadd,ilog"))
  "ev4_ib0+ev4_ebox")

(define_bypass 1
  "ev4_iaddlog"
  "ev4_ibr,ev4_iaddlog,ev4_shiftcm,ev4_icmp,ev4_imulsi,ev4_imuldi")

(define_insn_reservation "ev4_shiftcm" 2
  (and (eq_attr "tune" "ev4")
       (eq_attr "type" "shift,icmov"))
  "ev4_ib0+ev4_ebox")

(define_insn_reservation "ev4_icmp" 2
  (and (eq_attr "tune" "ev4")
       (eq_attr "type" "icmp"))
  "ev4_ib0+ev4_ebox")

(define_bypass 1 "ev4_icmp" "ev4_ibr")

(define_bypass 0
  "ev4_iaddlog,ev4_shiftcm,ev4_icmp"
  "ev4_ist"
  "store_data_bypass_p")

; Multiplies use a non-pipelined imul unit.  Also, "no [ebox] insn can
; be issued exactly three cycles before an integer multiply completes".

(define_insn_reservation "ev4_imulsi" 21
  (and (eq_attr "tune" "ev4")
       (and (eq_attr "type" "imul")
	    (eq_attr "opsize" "si")))
  "ev4_ib0+ev4_imul,ev4_imul*18,ev4_ebox")

(define_bypass 20 "ev4_imulsi" "ev4_ist" "store_data_bypass_p")

(define_insn_reservation "ev4_imuldi" 23
  (and (eq_attr "tune" "ev4")
       (and (eq_attr "type" "imul")
	    (eq_attr "opsize" "!si")))
  "ev4_ib0+ev4_imul,ev4_imul*20,ev4_ebox")

(define_bypass 22 "ev4_imuldi" "ev4_ist" "store_data_bypass_p")

; Most FP insns have a 6 cycle latency, but with a 4 cycle bypass back in.
(define_insn_reservation "ev4_fpop" 6
  (and (eq_attr "tune" "ev4")
       (eq_attr "type" "fadd,fmul,fcpys,fcmov"))
  "ev4_ib1+ev4_fbox")

(define_bypass 4 "ev4_fpop" "ev4_fpop")

; The floating point divider is not pipelined.  Also, "no FPOP insn can be
; issued exactly five or exactly six cycles before an fdiv insn completes".

(define_insn_reservation "ev4_fdivsf" 34
  (and (eq_attr "tune" "ev4")
       (and (eq_attr "type" "fdiv")
	    (eq_attr "opsize" "si")))
  "ev4_ib1+ev4_fdiv,ev4_fdiv*28,ev4_fdiv+ev4_fbox,ev4_fbox")

(define_insn_reservation "ev4_fdivdf" 63
  (and (eq_attr "tune" "ev4")
       (and (eq_attr "type" "fdiv")
	    (eq_attr "opsize" "di")))
  "ev4_ib1+ev4_fdiv,ev4_fdiv*57,ev4_fdiv+ev4_fbox,ev4_fbox")

; Traps don't consume or produce data.
(define_insn_reservation "ev4_misc" 1
  (and (eq_attr "tune" "ev4")
       (eq_attr "type" "misc"))
  "ev4_ib1")
