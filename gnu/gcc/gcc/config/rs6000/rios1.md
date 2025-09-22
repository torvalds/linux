;; Scheduling description for IBM POWER processor.
;;   Copyright (C) 2003, 2004 Free Software Foundation, Inc.
;;
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
;; along with GCC; see the file COPYING.  If not, write to the
;; Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
;; MA 02110-1301, USA.

(define_automaton "rios1,rios1fp")
(define_cpu_unit "iu_rios1" "rios1")
(define_cpu_unit "fpu_rios1" "rios1fp")
(define_cpu_unit "bpu_rios1" "rios1")

;; RIOS1  32-bit IU, FPU, BPU

(define_insn_reservation "rios1-load" 2
  (and (eq_attr "type" "load,load_ext,load_ext_u,load_ext_ux,load_ux,load_u,\
		        load_l,store_c,sync")
       (eq_attr "cpu" "rios1,ppc601"))
  "iu_rios1")

(define_insn_reservation "rios1-store" 2
  (and (eq_attr "type" "store,store_ux,store_u")
       (eq_attr "cpu" "rios1,ppc601"))
  "iu_rios1")

(define_insn_reservation "rios1-fpload" 2
  (and (eq_attr "type" "fpload,fpload_ux,fpload_u")
       (eq_attr "cpu" "rios1"))
  "iu_rios1")

(define_insn_reservation "ppc601-fpload" 3
  (and (eq_attr "type" "fpload,fpload_ux,fpload_u")
       (eq_attr "cpu" "ppc601"))
  "iu_rios1")

(define_insn_reservation "rios1-fpstore" 3
  (and (eq_attr "type" "fpstore,fpstore_ux,fpstore_u")
       (eq_attr "cpu" "rios1,ppc601"))
  "iu_rios1+fpu_rios1")

(define_insn_reservation "rios1-integer" 1
  (and (eq_attr "type" "integer,insert_word")
       (eq_attr "cpu" "rios1,ppc601"))
  "iu_rios1")

(define_insn_reservation "rios1-two" 1
  (and (eq_attr "type" "two")
       (eq_attr "cpu" "rios1,ppc601"))
  "iu_rios1,iu_rios1")

(define_insn_reservation "rios1-three" 1
  (and (eq_attr "type" "three")
       (eq_attr "cpu" "rios1,ppc601"))
  "iu_rios1,iu_rios1,iu_rios1")

(define_insn_reservation "rios1-imul" 5
  (and (eq_attr "type" "imul,imul_compare")
       (eq_attr "cpu" "rios1"))
  "iu_rios1*5")

(define_insn_reservation "rios1-imul2" 4
  (and (eq_attr "type" "imul2")
       (eq_attr "cpu" "rios1"))
  "iu_rios1*4")

(define_insn_reservation "rios1-imul3" 3
  (and (eq_attr "type" "imul")
       (eq_attr "cpu" "rios1"))
  "iu_rios1*3")

(define_insn_reservation "ppc601-imul" 5
  (and (eq_attr "type" "imul,imul2,imul3,imul_compare")
       (eq_attr "cpu" "ppc601"))
  "iu_rios1*5")

(define_insn_reservation "rios1-idiv" 19
  (and (eq_attr "type" "idiv")
       (eq_attr "cpu" "rios1"))
  "iu_rios1*19")

(define_insn_reservation "ppc601-idiv" 36
  (and (eq_attr "type" "idiv")
       (eq_attr "cpu" "ppc601"))
  "iu_rios1*36")

; compare executes on integer unit, but feeds insns which
; execute on the branch unit.
(define_insn_reservation "rios1-compare" 4
  (and (eq_attr "type" "cmp,fast_compare,compare")
       (eq_attr "cpu" "rios1"))
  "iu_rios1,nothing*2,bpu_rios1")

(define_insn_reservation "rios1-delayed_compare" 5
  (and (eq_attr "type" "delayed_compare")
       (eq_attr "cpu" "rios1"))
  "iu_rios1,nothing*3,bpu_rios1")

(define_insn_reservation "ppc601-compare" 3
  (and (eq_attr "type" "cmp,compare,delayed_compare")
       (eq_attr "cpu" "ppc601"))
  "iu_rios1,nothing,bpu_rios1")

(define_insn_reservation "rios1-fpcompare" 9
  (and (eq_attr "type" "fpcompare")
       (eq_attr "cpu" "rios1"))
  "fpu_rios1,nothing*3,bpu_rios1")

(define_insn_reservation "ppc601-fpcompare" 5
  (and (eq_attr "type" "fpcompare")
       (eq_attr "cpu" "ppc601"))
  "(fpu_rios1+iu_rios1*2),nothing*2,bpu_rios1")

(define_insn_reservation "rios1-fp" 2
  (and (eq_attr "type" "fp,dmul")
       (eq_attr "cpu" "rios1"))
  "fpu_rios1")

(define_insn_reservation "ppc601-fp" 4
  (and (eq_attr "type" "fp")
       (eq_attr "cpu" "ppc601"))
  "fpu_rios1")

(define_insn_reservation "rios1-dmul" 5
  (and (eq_attr "type" "dmul")
       (eq_attr "cpu" "ppc601"))
  "fpu_rios1*2")

(define_insn_reservation "rios1-sdiv" 19
  (and (eq_attr "type" "sdiv,ddiv")
       (eq_attr "cpu" "rios1"))
  "fpu_rios1*19")

(define_insn_reservation "ppc601-sdiv" 17
  (and (eq_attr "type" "sdiv")
       (eq_attr "cpu" "ppc601"))
  "fpu_rios1*17")

(define_insn_reservation "ppc601-ddiv" 31
  (and (eq_attr "type" "ddiv")
       (eq_attr "cpu" "ppc601"))
  "fpu_rios1*31")

(define_insn_reservation "rios1-mfcr" 2
  (and (eq_attr "type" "mfcr")
       (eq_attr "cpu" "rios1,ppc601"))
  "iu_rios1,bpu_rios1")

(define_insn_reservation "rios1-mtcr" 4
  (and (eq_attr "type" "mtcr")
       (eq_attr "cpu" "rios1,ppc601"))
  "iu_rios1,bpu_rios1")

(define_insn_reservation "rios1-crlogical" 4
  (and (eq_attr "type" "cr_logical,delayed_cr")
       (eq_attr "cpu" "rios1,ppc601"))
  "bpu_rios1")

(define_insn_reservation "rios1-mtjmpr" 5
  (and (eq_attr "type" "mtjmpr")
       (eq_attr "cpu" "rios1"))
  "iu_rios1,bpu_rios1")

(define_insn_reservation "ppc601-mtjmpr" 4
  (and (eq_attr "type" "mtjmpr")
       (eq_attr "cpu" "ppc601"))
  "iu_rios1,bpu_rios1")

(define_insn_reservation "rios1-mfjmpr" 2
  (and (eq_attr "type" "mfjmpr")
       (eq_attr "cpu" "rios1,ppc601"))
  "iu_rios1,bpu_rios1")

(define_insn_reservation "rios1-branch" 1
  (and (eq_attr "type" "jmpreg,branch,isync")
       (eq_attr "cpu" "rios1,ppc601"))
  "bpu_rios1")

