;; ARM 1136J[F]-S Pipeline Description
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
;; ARM1136JF-S Technical Reference Manual, Copyright (c) 2003 ARM
;; Limited.
;;

;; This automaton provides a pipeline description for the ARM
;; 1136J-S and 1136JF-S cores.
;;
;; The model given here assumes that the condition for all conditional
;; instructions is "true", i.e., that all of the instructions are
;; actually executed.

(define_automaton "arm1136jfs")

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Pipelines
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; There are three distinct pipelines (page 1-26 and following):
;;
;; - A 4-stage decode pipeline, shared by all three.  It has fetch (1),
;;   fetch (2), decode, and issue stages.  Since this is always involved,
;;   we do not model it in the scheduler.
;;
;; - A 4-stage ALU pipeline.  It has shifter, ALU (main integer operations),
;;   and saturation stages.  The fourth stage is writeback; see below.
;;
;; - A 4-stage multiply-accumulate pipeline.  It has three stages, called
;;   MAC1 through MAC3, and a fourth writeback stage.
;;
;;   The 4th-stage writeback is shared between the ALU and MAC pipelines,
;;   which operate in lockstep.  Results from either pipeline will be
;;   moved into the writeback stage.  Because the two pipelines operate
;;   in lockstep, we schedule them as a single "execute" pipeline.
;;
;; - A 4-stage LSU pipeline.  It has address generation, data cache (1),
;;   data cache (2), and writeback stages.  (Note that this pipeline,
;;   including the writeback stage, is independent from the ALU & LSU pipes.)  

(define_cpu_unit "e_1,e_2,e_3,e_wb" "arm1136jfs")     ; ALU and MAC
; e_1 = Sh/Mac1, e_2 = ALU/Mac2, e_3 = SAT/Mac3
(define_cpu_unit "l_a,l_dc1,l_dc2,l_wb" "arm1136jfs") ; Load/Store

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; ALU Instructions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; ALU instructions require eight cycles to execute, and use the ALU
;; pipeline in each of the eight stages.  The results are available
;; after the alu stage has finished.
;;
;; If the destination register is the PC, the pipelines are stalled
;; for several cycles.  That case is not modelled here.

;; ALU operations with no shifted operand
(define_insn_reservation "11_alu_op" 2
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "type" "alu"))
 "e_1,e_2,e_3,e_wb")

;; ALU operations with a shift-by-constant operand
(define_insn_reservation "11_alu_shift_op" 2
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "type" "alu_shift"))
 "e_1,e_2,e_3,e_wb")

;; ALU operations with a shift-by-register operand
;; These really stall in the decoder, in order to read
;; the shift value in a second cycle. Pretend we take two cycles in
;; the shift stage.
(define_insn_reservation "11_alu_shift_reg_op" 3
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "type" "alu_shift_reg"))
 "e_1*2,e_2,e_3,e_wb")

;; alu_ops can start sooner, if there is no shifter dependency
(define_bypass 1 "11_alu_op,11_alu_shift_op"
	       "11_alu_op")
(define_bypass 1 "11_alu_op,11_alu_shift_op"
	       "11_alu_shift_op"
	       "arm_no_early_alu_shift_value_dep")
(define_bypass 1 "11_alu_op,11_alu_shift_op"
	       "11_alu_shift_reg_op"
	       "arm_no_early_alu_shift_dep")
(define_bypass 2 "11_alu_shift_reg_op"
	       "11_alu_op")
(define_bypass 2 "11_alu_shift_reg_op"
	       "11_alu_shift_op"
	       "arm_no_early_alu_shift_value_dep")
(define_bypass 2 "11_alu_shift_reg_op"
	       "11_alu_shift_reg_op"
	       "arm_no_early_alu_shift_dep")

(define_bypass 1 "11_alu_op,11_alu_shift_op"
	       "11_mult1,11_mult2,11_mult3,11_mult4,11_mult5,11_mult6,11_mult7"
	       "arm_no_early_mul_dep")
(define_bypass 2 "11_alu_shift_reg_op"
	       "11_mult1,11_mult2,11_mult3,11_mult4,11_mult5,11_mult6,11_mult7"
	       "arm_no_early_mul_dep")

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Multiplication Instructions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Multiplication instructions loop in the first two execute stages until
;; the instruction has been passed through the multiplier array enough
;; times.

;; Multiply and multiply-accumulate results are available after four stages.
(define_insn_reservation "11_mult1" 4
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "insn" "mul,mla"))
 "e_1*2,e_2,e_3,e_wb")

;; The *S variants set the condition flags, which requires three more cycles.
(define_insn_reservation "11_mult2" 4
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "insn" "muls,mlas"))
 "e_1*2,e_2,e_3,e_wb")

(define_bypass 3 "11_mult1,11_mult2"
	       "11_mult1,11_mult2,11_mult3,11_mult4,11_mult5,11_mult6,11_mult7"
	       "arm_no_early_mul_dep")
(define_bypass 3 "11_mult1,11_mult2"
	       "11_alu_op")
(define_bypass 3 "11_mult1,11_mult2"
	       "11_alu_shift_op"
	       "arm_no_early_alu_shift_value_dep")
(define_bypass 3 "11_mult1,11_mult2"
	       "11_alu_shift_reg_op"
	       "arm_no_early_alu_shift_dep")
(define_bypass 3 "11_mult1,11_mult2"
	       "11_store1"
	       "arm_no_early_store_addr_dep")

;; Signed and unsigned multiply long results are available across two cycles;
;; the less significant word is available one cycle before the more significant
;; word.  Here we conservatively wait until both are available, which is
;; after three iterations and the memory cycle.  The same is also true of
;; the two multiply-accumulate instructions.
(define_insn_reservation "11_mult3" 5
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "insn" "smull,umull,smlal,umlal"))
 "e_1*3,e_2,e_3,e_wb*2")

;; The *S variants set the condition flags, which requires three more cycles.
(define_insn_reservation "11_mult4" 5
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "insn" "smulls,umulls,smlals,umlals"))
 "e_1*3,e_2,e_3,e_wb*2")

(define_bypass 4 "11_mult3,11_mult4"
	       "11_mult1,11_mult2,11_mult3,11_mult4,11_mult5,11_mult6,11_mult7"
	       "arm_no_early_mul_dep")
(define_bypass 4 "11_mult3,11_mult4"
	       "11_alu_op")
(define_bypass 4 "11_mult3,11_mult4"
	       "11_alu_shift_op"
	       "arm_no_early_alu_shift_value_dep")
(define_bypass 4 "11_mult3,11_mult4"
	       "11_alu_shift_reg_op"
	       "arm_no_early_alu_shift_dep")
(define_bypass 4 "11_mult3,11_mult4"
	       "11_store1"
	       "arm_no_early_store_addr_dep")

;; Various 16x16->32 multiplies and multiply-accumulates, using combinations
;; of high and low halves of the argument registers.  They take a single
;; pass through the pipeline and make the result available after three
;; cycles.
(define_insn_reservation "11_mult5" 3
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "insn" "smulxy,smlaxy,smulwy,smlawy,smuad,smuadx,smlad,smladx,smusd,smusdx,smlsd,smlsdx"))
 "e_1,e_2,e_3,e_wb")

(define_bypass 2 "11_mult5"
	       "11_mult1,11_mult2,11_mult3,11_mult4,11_mult5,11_mult6,11_mult7"
	       "arm_no_early_mul_dep")
(define_bypass 2 "11_mult5"
	       "11_alu_op")
(define_bypass 2 "11_mult5"
	       "11_alu_shift_op"
	       "arm_no_early_alu_shift_value_dep")
(define_bypass 2 "11_mult5"
	       "11_alu_shift_reg_op"
	       "arm_no_early_alu_shift_dep")
(define_bypass 2 "11_mult5"
	       "11_store1"
	       "arm_no_early_store_addr_dep")

;; The same idea, then the 32-bit result is added to a 64-bit quantity.
(define_insn_reservation "11_mult6" 4
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "insn" "smlalxy"))
 "e_1*2,e_2,e_3,e_wb*2")

;; Signed 32x32 multiply, then the most significant 32 bits are extracted
;; and are available after the memory stage.
(define_insn_reservation "11_mult7" 4
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "insn" "smmul,smmulr"))
 "e_1*2,e_2,e_3,e_wb")

(define_bypass 3 "11_mult6,11_mult7"
	       "11_mult1,11_mult2,11_mult3,11_mult4,11_mult5,11_mult6,11_mult7"
	       "arm_no_early_mul_dep")
(define_bypass 3 "11_mult6,11_mult7"
	       "11_alu_op")
(define_bypass 3 "11_mult6,11_mult7"
	       "11_alu_shift_op"
	       "arm_no_early_alu_shift_value_dep")
(define_bypass 3 "11_mult6,11_mult7"
	       "11_alu_shift_reg_op"
	       "arm_no_early_alu_shift_dep")
(define_bypass 3 "11_mult6,11_mult7"
	       "11_store1"
	       "arm_no_early_store_addr_dep")

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Branch Instructions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; These vary greatly depending on their arguments and the results of
;; stat prediction.  Cycle count ranges from zero (unconditional branch,
;; folded dynamic prediction) to seven (incorrect predictions, etc).  We
;; assume an optimal case for now, because the cost of a cache miss
;; overwhelms the cost of everything else anyhow.

(define_insn_reservation "11_branches" 0
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "type" "branch"))
 "nothing")

;; Call latencies are not predictable.  A semi-arbitrary very large
;; number is used as "positive infinity" so that everything should be
;; finished by the time of return.
(define_insn_reservation "11_call" 32
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "type" "call"))
 "nothing")

;; Branches are predicted. A correctly predicted branch will be no
;; cost, but we're conservative here, and use the timings a
;; late-register would give us.
(define_bypass 1 "11_alu_op,11_alu_shift_op"
	       "11_branches")
(define_bypass 2 "11_alu_shift_reg_op"
	       "11_branches")
(define_bypass 2 "11_load1,11_load2"
	       "11_branches")
(define_bypass 3 "11_load34"
	       "11_branches")

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Load/Store Instructions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; The models for load/store instructions do not accurately describe
;; the difference between operations with a base register writeback.
;; These models assume that all memory references hit in dcache.  Also,
;; if the PC is one of the registers involved, there are additional stalls
;; not modelled here.  Addressing modes are also not modelled.

(define_insn_reservation "11_load1" 3
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "type" "load1"))
 "l_a+e_1,l_dc1,l_dc2,l_wb")

;; Load byte results are not available until the writeback stage, where
;; the correct byte is extracted.

(define_insn_reservation "11_loadb" 4
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "type" "load_byte"))
 "l_a+e_1,l_dc1,l_dc2,l_wb")

(define_insn_reservation "11_store1" 0
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "type" "store1"))
 "l_a+e_1,l_dc1,l_dc2,l_wb")

;; Load/store double words into adjacent registers.  The timing and
;; latencies are different depending on whether the address is 64-bit
;; aligned.  This model assumes that it is.
(define_insn_reservation "11_load2" 3
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "type" "load2"))
 "l_a+e_1,l_dc1,l_dc2,l_wb")

(define_insn_reservation "11_store2" 0
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "type" "store2"))
 "l_a+e_1,l_dc1,l_dc2,l_wb")

;; Load/store multiple registers.  Two registers are stored per cycle.
;; Actual timing depends on how many registers are affected, so we
;; optimistically schedule a low latency.
(define_insn_reservation "11_load34" 4
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "type" "load3,load4"))
 "l_a+e_1,l_dc1*2,l_dc2,l_wb")

(define_insn_reservation "11_store34" 0
 (and (eq_attr "tune" "arm1136js,arm1136jfs")
      (eq_attr "type" "store3,store4"))
 "l_a+e_1,l_dc1*2,l_dc2,l_wb")

;; A store can start immediately after an alu op, if that alu op does
;; not provide part of the address to access.
(define_bypass 1 "11_alu_op,11_alu_shift_op"
	       "11_store1"
	       "arm_no_early_store_addr_dep")
(define_bypass 2 "11_alu_shift_reg_op"
	       "11_store1"
	       "arm_no_early_store_addr_dep")

;; An alu op can start sooner after a load, if that alu op does not
;; have an early register dependency on the load
(define_bypass 2 "11_load1"
	       "11_alu_op")
(define_bypass 2 "11_load1"
	       "11_alu_shift_op"
	       "arm_no_early_alu_shift_value_dep")
(define_bypass 2 "11_load1"
	       "11_alu_shift_reg_op"
	       "arm_no_early_alu_shift_dep")

(define_bypass 3 "11_loadb"
	       "11_alu_op")
(define_bypass 3 "11_loadb"
	       "11_alu_shift_op"
	       "arm_no_early_alu_shift_value_dep")
(define_bypass 3 "11_loadb"
	       "11_alu_shift_reg_op"
	       "arm_no_early_alu_shift_dep")

;; A mul op can start sooner after a load, if that mul op does not
;; have an early multiply dependency
(define_bypass 2 "11_load1"
	       "11_mult1,11_mult2,11_mult3,11_mult4,11_mult5,11_mult6,11_mult7"
	       "arm_no_early_mul_dep")
(define_bypass 3 "11_load34"
	       "11_mult1,11_mult2,11_mult3,11_mult4,11_mult5,11_mult6,11_mult7"
	       "arm_no_early_mul_dep")
(define_bypass 3 "11_loadb"
	       "11_mult1,11_mult2,11_mult3,11_mult4,11_mult5,11_mult6,11_mult7"
	       "arm_no_early_mul_dep")

;; A store can start sooner after a load, if that load does not
;; produce part of the address to access
(define_bypass 2 "11_load1"
	       "11_store1"
	       "arm_no_early_store_addr_dep")
(define_bypass 3 "11_loadb"
	       "11_store1"
	       "arm_no_early_store_addr_dep")
