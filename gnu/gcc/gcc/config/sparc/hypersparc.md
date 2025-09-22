;; Scheduling description for HyperSPARC.
;;   Copyright (C) 2002 Free Software Foundation, Inc.
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

;; The HyperSPARC is a dual-issue processor.  It is not all that fancy.

;; ??? There are some things not modelled.  For example, sethi+or
;; ??? coming right after each other are specifically identified and
;; ??? dual-issued by the processor.  Similarly for sethi+ld[reg+lo].
;; ??? Actually, to be more precise that rule is sort of modelled now.

(define_automaton "hypersparc_0,hypersparc_1")

;; HyperSPARC/sparclite86x scheduling

(define_cpu_unit "hs_memory,hs_branch,hs_shift,hs_fpalu" "hypersparc_0")
(define_cpu_unit "hs_fpmds" "hypersparc_1")

(define_insn_reservation "hs_load" 1
  (and (ior (eq_attr "cpu" "hypersparc") (eq_attr "cpu" "sparclite86x"))
    (eq_attr "type" "load,sload,fpload"))
  "hs_memory")

(define_insn_reservation "hs_store" 2
  (and (ior (eq_attr "cpu" "hypersparc") (eq_attr "cpu" "sparclite86x"))
    (eq_attr "type" "store,fpstore"))
  "hs_memory, nothing")

(define_insn_reservation "hs_slbranch" 1
  (and (eq_attr "cpu" "sparclite86x")
    (eq_attr "type" "branch"))
  "hs_branch")

(define_insn_reservation "hs_slshift" 1
  (and (eq_attr "cpu" "sparclite86x")
    (eq_attr "type" "shift"))
  "hs_shift")

(define_insn_reservation "hs_fp_alu" 1
  (and (ior (eq_attr "cpu" "hypersparc") (eq_attr "cpu" "sparclite86x"))
    (eq_attr "type" "fp,fpmove,fpcmp"))
  "hs_fpalu")

(define_insn_reservation "hs_fp_mult" 1
  (and (ior (eq_attr "cpu" "hypersparc") (eq_attr "cpu" "sparclite86x"))
    (eq_attr "type" "fpmul"))
  "hs_fpmds")

(define_insn_reservation "hs_fp_divs" 8
  (and (ior (eq_attr "cpu" "hypersparc") (eq_attr "cpu" "sparclite86x"))
    (eq_attr "type" "fpdivs"))
  "hs_fpmds*6, nothing*2")

(define_insn_reservation "hs_fp_divd" 12
  (and (ior (eq_attr "cpu" "hypersparc") (eq_attr "cpu" "sparclite86x"))
    (eq_attr "type" "fpdivd"))
  "hs_fpmds*10, nothing*2")

(define_insn_reservation "hs_fp_sqrt" 17
  (and (ior (eq_attr "cpu" "hypersparc") (eq_attr "cpu" "sparclite86x"))
    (eq_attr "type" "fpsqrts,fpsqrtd"))
  "hs_fpmds*15, nothing*2")

(define_insn_reservation "hs_imul" 17
  (and (ior (eq_attr "cpu" "hypersparc") (eq_attr "cpu" "sparclite86x"))
    (eq_attr "type" "imul"))
  "hs_fpmds*15, nothing*2")
