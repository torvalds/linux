;; DFA scheduling description for SH4.
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

;; The following description models the SH4 pipeline using the DFA based
;; scheduler.  The DFA based description is better way to model a
;; superscalar pipeline as compared to function unit reservation model.
;; 1. The function unit based model is oriented to describe at most one
;;    unit reservation by each insn. It is difficult to model unit reservations
;;    in multiple pipeline units by same insn.  This can be done using DFA
;;    based description.
;; 2. The execution performance of DFA based scheduler does not depend on
;;    processor complexity.
;; 3. Writing all unit reservations for an instruction class is a more natural
;;    description of the pipeline and makes the interface to the hazard
;;    recognizer simpler than the old function unit based model.
;; 4. The DFA model is richer and is a part of greater overall framework
;;    of RCSP.


;; Two automata are defined to reduce number of states
;; which a single large automaton will have. (Factoring)

(define_automaton "inst_pipeline,fpu_pipe")

;; This unit is basically the decode unit of the processor.
;; Since SH4 is a dual issue machine,it is as if there are two
;; units so that any insn can be processed by either one
;; of the decoding unit.

(define_cpu_unit "pipe_01,pipe_02" "inst_pipeline")


;; The fixed point arithmetic calculator(?? EX Unit).

(define_cpu_unit  "int" "inst_pipeline")

;; f1_1 and f1_2 are floating point units.Actually there is
;; a f1 unit which can overlap with other f1 unit but
;; not another F1 unit.It is as though there were two
;; f1 units.

(define_cpu_unit "f1_1,f1_2" "fpu_pipe")

;; The floating point units (except FS - F2 always precedes it.)

(define_cpu_unit "F0,F1,F2,F3" "fpu_pipe")

;; This is basically the MA unit of SH4
;; used in LOAD/STORE pipeline.

(define_cpu_unit "memory" "inst_pipeline")

;; However, there are LS group insns that don't use it, even ones that
;; complete in 0 cycles.  So we use an extra unit for the issue of LS insns.
(define_cpu_unit "load_store" "inst_pipeline")

;; The address calculator used for branch instructions.
;; This will be reserved after "issue" of branch instructions
;; and this is to make sure that no two branch instructions
;; can be issued in parallel.

(define_cpu_unit "pcr_addrcalc" "inst_pipeline")

;; ----------------------------------------------------
;; This reservation is to simplify the dual issue description.

(define_reservation  "issue"  "pipe_01|pipe_02")

;; This is to express the locking of D stage.
;; Note that the issue of a CO group insn also effectively locks the D stage.

(define_reservation  "d_lock" "pipe_01+pipe_02")

;; Every FE instruction but fipr / ftrv starts with issue and this.
(define_reservation "F01" "F0+F1")

;; This is to simplify description where F1,F2,FS
;; are used simultaneously.

(define_reservation "fpu" "F1+F2")

;; This is to highlight the fact that f1
;; cannot overlap with F1.

(exclusion_set  "f1_1,f1_2" "F1")

(define_insn_reservation "nil" 0 (eq_attr "type" "nil") "nothing")

;; Although reg moves have a latency of zero
;; we need to highlight that they use D stage
;; for one cycle.

;; Group:	MT

(define_insn_reservation "reg_mov" 0
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "move"))
  "issue")

;; Group:	LS

(define_insn_reservation "freg_mov" 0
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "fmove"))
  "issue+load_store")

;; We don't model all pipeline stages; we model the issue ('D') stage
;; inasmuch as we allow only two instructions to issue simultaneously,
;; and CO instructions prevent any simultaneous issue of another instruction.
;; (This uses pipe_01 and pipe_02).
;; Double issue of EX insns is prevented by using the int unit in the EX stage.
;; Double issue of EX / BR insns is prevented by using the int unit /
;; pcr_addrcalc unit in the EX stage.
;; Double issue of BR / LS instructions is prevented by using the
;; pcr_addrcalc / load_store unit in the issue cycle.
;; Double issue of FE instructions is prevented by using F0 in the first
;; pipeline stage after the first D stage.
;; There is no need to describe the [ES]X / [MN]A / S stages after a D stage
;; (except in the cases outlined above), nor to describe the FS stage after
;; the F2 stage.

;; Other MT  group instructions(1 step operations)
;; Group:	MT
;; Latency: 	1
;; Issue Rate: 	1

(define_insn_reservation "mt" 1
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "mt_group"))
  "issue")

;; Fixed Point Arithmetic Instructions(1 step operations)
;; Group:	EX
;; Latency: 	1
;; Issue Rate: 	1

(define_insn_reservation "sh4_simple_arith" 1
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "insn_class" "ex_group"))
  "issue,int")

;; Load and store instructions have no alignment peculiarities for the SH4,
;; but they use the load-store unit, which they share with the fmove type
;; insns (fldi[01]; fmov frn,frm; flds; fsts; fabs; fneg) .
;; Loads have a latency of two.
;; However, call insns can only paired with a preceding insn, and have
;; a delay slot, so that we want two more insns to be scheduled between the
;; load of the function address and the call.  This is equivalent to a
;; latency of three.
;; ADJUST_COST can only properly handle reductions of the cost, so we
;; use a latency of three here, which gets multiplied by 10 to yield 30.
;; We only do this for SImode loads of general registers, to make the work
;; for ADJUST_COST easier.

;; Load Store instructions. (MOV.[BWL]@(d,GBR)
;; Group:	LS
;; Latency: 	2
;; Issue Rate: 	1

(define_insn_reservation "sh4_load" 2
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "load,pcload"))
  "issue+load_store,nothing,memory")

;; calls / sfuncs need an extra instruction for their delay slot.
;; Moreover, estimating the latency for SImode loads as 3 will also allow
;; adjust_cost to meaningfully bump it back up to 3 if they load the shift
;; count of a dynamic shift.
(define_insn_reservation "sh4_load_si" 3
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "load_si,pcload_si"))
  "issue+load_store,nothing,memory")

;; (define_bypass 2 "sh4_load_si" "!sh4_call")

;; The load latency is upped to three higher if the dependent insn does
;; double precision computation.  We want the 'default' latency to reflect
;; that increased latency because otherwise the insn priorities won't
;; allow proper scheduling.
(define_insn_reservation "sh4_fload" 3
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "fload,pcfload"))
  "issue+load_store,nothing,memory")

;; (define_bypass 2 "sh4_fload" "!")

(define_insn_reservation "sh4_store" 1
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "store"))
  "issue+load_store,nothing,memory")

;; Load Store instructions.
;; Group:	LS
;; Latency: 	1
;; Issue Rate: 	1

(define_insn_reservation "sh4_gp_fpul" 1
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "gp_fpul"))
  "issue+load_store")

;; Load Store instructions.
;; Group:	LS
;; Latency: 	3
;; Issue Rate: 	1

(define_insn_reservation "sh4_fpul_gp" 3
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "fpul_gp"))
  "issue+load_store")

;; Branch (BF,BF/S,BT,BT/S,BRA)
;; Group:	BR
;; Latency when taken: 	2 (or 1)
;; Issue Rate: 	1
;; The latency is 1 when displacement is 0.
;; We can't really do much with the latency, even if we could express it,
;; but the pairing restrictions are useful to take into account.
;; ??? If the branch is likely, we might want to fill the delay slot;
;; if the branch is likely, but not very likely, should we pretend to use
;; a resource that CO instructions use, to get a pairable delay slot insn?

(define_insn_reservation "sh4_branch"  1
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "cbranch,jump"))
  "issue+pcr_addrcalc")

;; Branch Far (JMP,RTS,BRAF)
;; Group:	CO
;; Latency: 	3
;; Issue Rate: 	2
;; ??? Scheduling happens before branch shortening, and hence jmp and braf
;; can't be distinguished from bra for the "jump" pattern.

(define_insn_reservation "sh4_return" 3
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "return,jump_ind"))
         "d_lock*2")

;; RTE
;; Group:	CO
;; Latency: 	5
;; Issue Rate: 	5
;; this instruction can be executed in any of the pipelines
;; and blocks the pipeline for next 4 stages.

(define_insn_reservation "sh4_return_from_exp" 5
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "rte"))
  "d_lock*5")

;; OCBP, OCBWB
;; Group:	CO
;; Latency: 	1-5
;; Issue Rate: 	1

;; cwb is used for the sequence ocbwb @%0; extu.w %0,%2; or %1,%2; mov.l %0,@%2
;; ocbwb on its own would be "d_lock,nothing,memory*5"
(define_insn_reservation "ocbwb"  6
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "cwb"))
  "d_lock*2,(d_lock+memory)*3,issue+load_store+memory,memory*2")

;; LDS to PR,JSR
;; Group:	CO
;; Latency: 	3
;; Issue Rate: 	2
;; The SX stage is blocked for last 2 cycles.
;; OTOH, the only time that has an effect for insns generated by the compiler
;; is when lds to PR is followed by sts from PR - and that is highly unlikely -
;; or when we are doing a function call - and we don't do inter-function
;; scheduling.  For the function call case, it's really best that we end with
;; something that models an rts.

(define_insn_reservation "sh4_lds_to_pr" 3
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "prset") )
  "d_lock*2")

;; calls introduce a longisch delay that is likely to flush the pipelines
;; of the caller's instructions.  Ordinary functions tend to end with a
;; load to restore a register (in the delay slot of rts), while sfuncs
;; tend to end with an EX or MT insn.  But that is not actually relevant,
;; since there are no instructions that contend for memory access early.
;; We could, of course, provide exact scheduling information for specific
;; sfuncs, if that should prove useful.

(define_insn_reservation "sh4_call" 16
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "call,sfunc"))
  "d_lock*16")

;; LDS.L to PR
;; Group:	CO
;; Latency: 	3
;; Issue Rate: 	2
;; The SX unit is blocked for last 2 cycles.

(define_insn_reservation "ldsmem_to_pr"  3
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "pload"))
  "d_lock*2")

;; STS from PR
;; Group:	CO
;; Latency: 	2
;; Issue Rate: 	2
;; The SX unit in second and third cycles.

(define_insn_reservation "sts_from_pr" 2
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "prget"))
  "d_lock*2")

;; STS.L from PR
;; Group:	CO
;; Latency: 	2
;; Issue Rate: 	2

(define_insn_reservation "sh4_prstore_mem" 2
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "pstore"))
  "d_lock*2,nothing,memory")

;; LDS to FPSCR
;; Group:	CO
;; Latency: 	4
;; Issue Rate: 	1
;; F1 is blocked for last three cycles.

(define_insn_reservation "fpscr_load" 4
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "gp_fpscr"))
  "d_lock,nothing,F1*3")

;; LDS.L to FPSCR
;; Group:	CO
;; Latency: 	1 / 4
;; Latency to update Rn is 1 and latency to update FPSCR is 4
;; Issue Rate: 	1
;; F1 is blocked for last three cycles.

(define_insn_reservation "fpscr_load_mem" 4
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type"  "mem_fpscr"))
  "d_lock,nothing,(F1+memory),F1*2")


;; Fixed point multiplication (DMULS.L DMULU.L MUL.L MULS.W,MULU.W)
;; Group:	CO
;; Latency: 	4 / 4
;; Issue Rate: 	1

(define_insn_reservation "multi" 4
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "smpy,dmpy"))
  "d_lock,(d_lock+f1_1),(f1_1|f1_2)*3,F2")

;; Fixed STS from MACL / MACH
;; Group:	CO
;; Latency: 	3
;; Issue Rate: 	1

(define_insn_reservation "sh4_mac_gp" 3
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "mac_gp"))
  "d_lock")


;; Single precision floating point computation FCMP/EQ,
;; FCMP/GT, FADD, FLOAT, FMAC, FMUL, FSUB, FTRC, FRVHG, FSCHG
;; Group:	FE
;; Latency: 	3/4
;; Issue Rate: 	1

(define_insn_reservation "fp_arith"  3
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "fp"))
  "issue,F01,F2")

(define_insn_reservation "fp_arith_ftrc"  3
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "ftrc_s"))
  "issue,F01,F2")

(define_bypass 1 "fp_arith_ftrc" "sh4_fpul_gp")

;; Single Precision FDIV/SQRT
;; Group:	FE
;; Latency: 	12/13 (FDIV); 11/12 (FSQRT)
;; Issue Rate: 	1
;; We describe fdiv here; fsqrt is actually one cycle faster.

(define_insn_reservation "fp_div" 12
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "fdiv"))
  "issue,F01+F3,F2+F3,F3*7,F1+F3,F2")

;; Double Precision floating point computation
;; (FCNVDS, FCNVSD, FLOAT, FTRC)
;; Group:	FE
;; Latency: 	(3,4)/5
;; Issue Rate: 	1

(define_insn_reservation "dp_float" 4
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "dfp_conv"))
  "issue,F01,F1+F2,F2")

;; Double-precision floating-point (FADD,FMUL,FSUB)
;; Group:	FE
;; Latency: 	(7,8)/9
;; Issue Rate: 	1

(define_insn_reservation "fp_double_arith" 8
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "dfp_arith"))
  "issue,F01,F1+F2,fpu*4,F2")

;; Double-precision FCMP (FCMP/EQ,FCMP/GT)
;; Group:	CO
;; Latency: 	3/5
;; Issue Rate: 	2

(define_insn_reservation "fp_double_cmp" 3
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "dfp_cmp"))
  "d_lock,(d_lock+F01),F1+F2,F2")

;; Double precision FDIV/SQRT
;; Group:	FE
;; Latency: 	(24,25)/26
;; Issue Rate: 	1

(define_insn_reservation "dp_div" 25
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "dfdiv"))
  "issue,F01+F3,F1+F2+F3,F2+F3,F3*16,F1+F3,(fpu+F3)*2,F2")


;; Use the branch-not-taken case to model arith3 insns.  For the branch taken
;; case, we'd get a d_lock instead of issue at the end.
(define_insn_reservation "arith3" 3
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "arith3"))
  "issue,d_lock+pcr_addrcalc,issue")

;; arith3b insns schedule the same no matter if the branch is taken or not.
(define_insn_reservation "arith3b" 2
  (and (eq_attr "pipe_model" "sh4")
       (eq_attr "type" "arith3"))
  "issue,d_lock+pcr_addrcalc")
