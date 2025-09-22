;; DFA scheduling description for Renesas / SuperH SH.
;; Copyright (C) 2004 Free Software Foundation, Inc.

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

;; Load and store instructions save a cycle if they are aligned on a
;; four byte boundary.  Using a function unit for stores encourages
;; gcc to separate load and store instructions by one instruction,
;; which makes it more likely that the linker will be able to word
;; align them when relaxing.

;; SH-1 scheduling.  This is just a conversion of the old scheduling
;; model, using define_function_unit.

(define_automaton "sh1")
(define_cpu_unit "sh1memory,sh1int,sh1mpy,sh1fp" "sh1")

;; Loads have a latency of two.
;; However, call insns can have a delay slot, so that we want one more
;; insn to be scheduled between the load of the function address and the call.
;; This is equivalent to a latency of three.
;; ADJUST_COST can only properly handle reductions of the cost, so we
;; use a latency of three here.
;; We only do this for SImode loads of general registers, to make the work
;; for ADJUST_COST easier.
(define_insn_reservation "sh1_load_si" 3
  (and (eq_attr "pipe_model" "sh1")
       (eq_attr "type" "load_si,pcload_si"))
  "sh1memory*2")

(define_insn_reservation "sh1_load_store" 2
  (and (eq_attr "pipe_model" "sh1")
       (eq_attr "type" "load,pcload,pload,store,pstore"))
  "sh1memory*2")

(define_insn_reservation "sh1_arith3" 3
  (and (eq_attr "pipe_model" "sh1")
       (eq_attr "type" "arith3,arith3b"))
  "sh1int*3")

(define_insn_reservation "sh1_dyn_shift" 2
  (and (eq_attr "pipe_model" "sh1")
       (eq_attr "type" "dyn_shift"))
  "sh1int*2")

(define_insn_reservation "sh1_int" 1
  (and (eq_attr "pipe_model" "sh1")
       (eq_attr "type" "!arith3,arith3b,dyn_shift"))
  "sh1int")

;; ??? These are approximations.
(define_insn_reservation "sh1_smpy" 2
  (and (eq_attr "pipe_model" "sh1")
       (eq_attr "type" "smpy"))
  "sh1mpy*2")

(define_insn_reservation "sh1_dmpy" 3
  (and (eq_attr "pipe_model" "sh1")
       (eq_attr "type" "dmpy"))
  "sh1mpy*3")

(define_insn_reservation "sh1_fp" 2
  (and (eq_attr "pipe_model" "sh1")
       (eq_attr "type" "fp,fmove"))
  "sh1fp")

(define_insn_reservation "sh1_fdiv" 13
  (and (eq_attr "pipe_model" "sh1")
       (eq_attr "type" "fdiv"))
  "sh1fp*12")

