;; Scheduling description for SPARClet.
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

;; The SPARClet is a single-issue processor.

(define_automaton "sparclet")

(define_cpu_unit "sl_load0,sl_load1,sl_load2,sl_load3" "sparclet")
(define_cpu_unit "sl_store,sl_imul" "sparclet")

(define_reservation "sl_load_any" "(sl_load0 | sl_load1 | sl_load2 | sl_load3)")
(define_reservation "sl_load_all" "(sl_load0 + sl_load1 + sl_load2 + sl_load3)")

(define_insn_reservation "sl_ld" 3
  (and (eq_attr "cpu" "tsc701")
   (eq_attr "type" "load,sload"))
  "sl_load_any, sl_load_any, sl_load_any")

(define_insn_reservation "sl_st" 3
  (and (eq_attr "cpu" "tsc701")
    (eq_attr "type" "store"))
  "(sl_store+sl_load_all)*3")

(define_insn_reservation "sl_imul" 5
  (and (eq_attr "cpu" "tsc701")
    (eq_attr "type" "imul"))
  "sl_imul*5")
