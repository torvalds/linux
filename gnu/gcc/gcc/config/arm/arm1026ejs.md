;; ARM 1026EJ-S Pipeline Description
;; Copyright (C) 2003 Free Software Foundation, Inc.
;; Written by CodeSourcery, LLC.
;;
;; This file is part of GCC.
;;
;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.
;;
;; GCC is distributed in the hope that it will be useful, but
;; WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;; General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to the Free
;; Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
;; 02110-1301, USA.  */

;; These descriptions are based on the information contained in the
;; ARM1026EJ-S Technical Reference Manual, Copyright (c) 2003 ARM
;; Limited.
;;

;; This automaton provides a pipeline description for the ARM
;; 1026EJ-S core.
;;
;; The model given here assumes that the condition for all conditional
;; instructions is "true", i.e., that all of the instructions are
;; actually executed.

(define_automaton "arm1026ejs")

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Pipelines
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; There are two pipelines:
;; 
;; - An Arithmetic Logic Unit (ALU) pipeline.
;;
;;   The ALU pipeline has fetch, issue, decode, execute, memory, and
;;   write stages. We only need to model the execute, memory and write
;;   stages.
;;
;; - A Load-Store Unit (LSU) pipeline.
;;
;;   The LSU pipeline has decode, execute, memory, and write stages.
;;   We only model the execute, memory and write stages.

(define_cpu_unit "a_e,a_m,a_w" "arm1026ejs")
(define_cpu_unit "l_e,l_m,l_w" "arm1026ejs")

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; ALU Instructions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; ALU instructions require three cycles to execute, and use the ALU
;; pipeline in each of the three stages.  The results are available
;; after the execute stage stage has finished.
;;
;; If the destination register is the PC, the pipelines are stalled
;; for several cycles.  That case is not modeled here.

;; ALU operations with no shifted operand
(define_insn_reservation "alu_op" 1 
 (and (eq_attr "tune" "arm1026ejs")
      (eq_attr "type" "alu"))
 "a_e,a_m,a_w")

;; ALU operations with a shift-by-constant operand
(define_insn_reservation "alu_shift_op" 1 
 (and (eq_attr "tune" "arm1026ejs")
      (eq_attr "type" "alu_shift"))
 "a_e,a_m,a_w")

;; ALU operations with a shift-by-register operand
;; These really stall in the decoder, in order to read
;; the shift value in a second cycle. Pretend we take two cycles in
;; the execute stage.
(define_insn_reservation "alu_shift_reg_op" 2 
 (and (eq_attr "tune" "arm1026ejs")
      (eq_attr "type" "alu_shift_reg"))
 "a_e*2,a_m,a_w")

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Multiplication Instructions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Multiplication instructions loop in the execute stage until the
;; instruction has been passed through the multiplier array enough
;; times.

;; The result of the "smul" and "smulw" instructions is not available
;; until after the memory stage.
(define_insn_reservation "mult1" 2
 (and (eq_attr "tune" "arm1026ejs")
      (eq_attr "insn" "smulxy,smulwy"))
 "a_e,a_m,a_w")

;; The "smlaxy" and "smlawx" instructions require two iterations through
;; the execute stage; the result is available immediately following
;; the execute stage.
(define_insn_reservation "mult2" 2
 (and (eq_attr "tune" "arm1026ejs")
      (eq_attr "insn" "smlaxy,smlalxy,smlawx"))
 "a_e*2,a_m,a_w")

;; The "smlalxy", "mul", and "mla" instructions require two iterations
;; through the execute stage; the result is not available until after
;; the memory stage.
(define_insn_reservation "mult3" 3
 (and (eq_attr "tune" "arm1026ejs")
      (eq_attr "insn" "smlalxy,mul,mla"))
 "a_e*2,a_m,a_w")

;; The "muls" and "mlas" instructions loop in the execute stage for
;; four iterations in order to set the flags.  The value result is
;; available after three iterations.
(define_insn_reservation "mult4" 3
 (and (eq_attr "tune" "arm1026ejs")
      (eq_attr "insn" "muls,mlas"))
 "a_e*4,a_m,a_w")

;; Long multiply instructions that produce two registers of
;; output (such as umull) make their results available in two cycles;
;; the least significant word is available before the most significant
;; word.  That fact is not modeled; instead, the instructions are
;; described.as if the entire result was available at the end of the
;; cycle in which both words are available.

;; The "umull", "umlal", "smull", and "smlal" instructions all take
;; three iterations through the execute cycle, and make their results
;; available after the memory cycle.
(define_insn_reservation "mult5" 4
 (and (eq_attr "tune" "arm1026ejs")
      (eq_attr "insn" "umull,umlal,smull,smlal"))
 "a_e*3,a_m,a_w")

;; The "umulls", "umlals", "smulls", and "smlals" instructions loop in
;; the execute stage for five iterations in order to set the flags.
;; The value result is available after four iterations.
(define_insn_reservation "mult6" 4
 (and (eq_attr "tune" "arm1026ejs")
      (eq_attr "insn" "umulls,umlals,smulls,smlals"))
 "a_e*5,a_m,a_w")

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Load/Store Instructions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; The models for load/store instructions do not accurately describe
;; the difference between operations with a base register writeback
;; (such as "ldm!").  These models assume that all memory references
;; hit in dcache.

;; LSU instructions require six cycles to execute.  They use the ALU
;; pipeline in all but the 5th cycle, and the LSU pipeline in cycles
;; three through six.
;; Loads and stores which use a scaled register offset or scaled
;; register pre-indexed addressing mode take three cycles EXCEPT for
;; those that are base + offset with LSL of 0 or 2, or base - offset
;; with LSL of zero.  The remainder take 1 cycle to execute.
;; For 4byte loads there is a bypass from the load stage

(define_insn_reservation "load1_op" 2
 (and (eq_attr "tune" "arm1026ejs")
      (eq_attr "type" "load_byte,load1"))
 "a_e+l_e,l_m,a_w+l_w")

(define_insn_reservation "store1_op" 0
 (and (eq_attr "tune" "arm1026ejs")
      (eq_attr "type" "store1"))
 "a_e+l_e,l_m,a_w+l_w")

;; A load's result can be stored by an immediately following store
(define_bypass 1 "load1_op" "store1_op" "arm_no_early_store_addr_dep")

;; On a LDM/STM operation, the LSU pipeline iterates until all of the
;; registers have been processed.
;;
;; The time it takes to load the data depends on whether or not the
;; base address is 64-bit aligned; if it is not, an additional cycle
;; is required.  This model assumes that the address is always 64-bit
;; aligned.  Because the processor can load two registers per cycle,
;; that assumption means that we use the same instruction reservations
;; for loading 2k and 2k - 1 registers.
;;
;; The ALU pipeline is stalled until the completion of the last memory
;; stage in the LSU pipeline.  That is modeled by keeping the ALU
;; execute stage busy until that point.
;;
;; As with ALU operations, if one of the destination registers is the
;; PC, there are additional stalls; that is not modeled.

(define_insn_reservation "load2_op" 2
 (and (eq_attr "tune" "arm1026ejs")
      (eq_attr "type" "load2"))
 "a_e+l_e,l_m,a_w+l_w")

(define_insn_reservation "store2_op" 0
 (and (eq_attr "tune" "arm1026ejs")
      (eq_attr "type" "store2"))
 "a_e+l_e,l_m,a_w+l_w")

(define_insn_reservation "load34_op" 3
 (and (eq_attr "tune" "arm1026ejs")
      (eq_attr "type" "load3,load4"))
 "a_e+l_e,a_e+l_e+l_m,a_e+l_m,a_w+l_w")

(define_insn_reservation "store34_op" 0
 (and (eq_attr "tune" "arm1026ejs")
      (eq_attr "type" "store3,store4"))
 "a_e+l_e,a_e+l_e+l_m,a_e+l_m,a_w+l_w")

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Branch and Call Instructions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Branch instructions are difficult to model accurately.  The ARM
;; core can predict most branches.  If the branch is predicted
;; correctly, and predicted early enough, the branch can be completely
;; eliminated from the instruction stream.  Some branches can
;; therefore appear to require zero cycles to execute.  We assume that
;; all branches are predicted correctly, and that the latency is
;; therefore the minimum value.

(define_insn_reservation "branch_op" 0
 (and (eq_attr "tune" "arm1026ejs")
      (eq_attr "type" "branch"))
 "nothing")

;; The latency for a call is not predictable.  Therefore, we use 32 as
;; roughly equivalent to positive infinity.

(define_insn_reservation "call_op" 32
 (and (eq_attr "tune" "arm1026ejs")
      (eq_attr "type" "call"))
 "nothing")
