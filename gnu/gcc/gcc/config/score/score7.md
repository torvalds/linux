;;  Machine description for Sunplus S+CORE
;;  Sunplus S+CORE 7 Pipeline Description
;;  Copyright (C) 2005
;;  Free Software Foundation, Inc.
;;  Contributed by Sunnorth.

;; This file is part of GCC.

;; GCC is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.

;; GCC is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to
;; the Free Software Foundation, 51 Franklin Street, Fifth Floor,
;; Boston, MA 02110-1301, USA.

(define_automaton "score")

(define_cpu_unit "core" "score")

(define_insn_reservation "memory" 3
                         (eq_attr "type" "load")
                         "core")

(define_insn_reservation "mul" 3
                         (eq_attr "type" "mul,div")
                         "core")

(define_insn_reservation "fce" 1
                         (eq_attr "type" "fce")
                         "core")

(define_insn_reservation "tsr" 1
                         (eq_attr "type" "tsr,fsr")
                         "core")

(define_insn_reservation "up_c" 1
                         (eq_attr "up_c" "yes")
                         "core")
