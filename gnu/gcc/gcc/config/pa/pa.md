;;- Machine description for HP PA-RISC architecture for GCC compiler
;;   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001,
;;   2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
;;   Contributed by the Center for Software Science at the University
;;   of Utah.

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

;; This gcc Version 2 machine description is inspired by sparc.md and
;; mips.md.

;;- See file "rtl.def" for documentation on define_insn, match_*, et. al.

;; Uses of UNSPEC in this file:

(define_constants
  [(UNSPEC_CFFC		0)	; canonicalize_funcptr_for_compare
   (UNSPEC_GOTO		1)	; indirect_goto
   (UNSPEC_DLTIND14R	2)	; 
   (UNSPEC_TP		3)
   (UNSPEC_TLSGD	4)
   (UNSPEC_TLSLDM	5)
   (UNSPEC_TLSLDO	6)
   (UNSPEC_TLSLDBASE	7)
   (UNSPEC_TLSIE	8)
   (UNSPEC_TLSLE 	9)
   (UNSPEC_TLSGD_PIC   10)
   (UNSPEC_TLSLDM_PIC  11)
   (UNSPEC_TLSIE_PIC   12)
  ])

;; UNSPEC_VOLATILE:

(define_constants
  [(UNSPECV_BLOCKAGE	0)	; blockage
   (UNSPECV_DCACHE	1)	; dcacheflush
   (UNSPECV_ICACHE	2)	; icacheflush
   (UNSPECV_OPC		3)	; outline_prologue_call
   (UNSPECV_OEC		4)	; outline_epilogue_call
   (UNSPECV_LONGJMP	5)	; builtin_longjmp
  ])

;; Maximum pc-relative branch offsets.

;; These numbers are a bit smaller than the maximum allowable offsets
;; so that a few instructions may be inserted before the actual branch.

(define_constants
  [(MAX_12BIT_OFFSET     8184)	; 12-bit branch
   (MAX_17BIT_OFFSET   262100)	; 17-bit branch
  ])

;; Insn type.  Used to default other attribute values.

;; type "unary" insns have one input operand (1) and one output operand (0)
;; type "binary" insns have two input operands (1,2) and one output (0)

(define_attr "type"
  "move,unary,binary,shift,nullshift,compare,load,store,uncond_branch,btable_branch,branch,cbranch,fbranch,call,dyncall,fpload,fpstore,fpalu,fpcc,fpmulsgl,fpmuldbl,fpdivsgl,fpdivdbl,fpsqrtsgl,fpsqrtdbl,multi,milli,parallel_branch"
  (const_string "binary"))

(define_attr "pa_combine_type"
  "fmpy,faddsub,uncond_branch,addmove,none"
  (const_string "none"))

;; Processor type (for scheduling, not code generation) -- this attribute
;; must exactly match the processor_type enumeration in pa.h.
;;
;; FIXME: Add 800 scheduling for completeness?

(define_attr "cpu" "700,7100,7100LC,7200,7300,8000" (const (symbol_ref "pa_cpu_attr")))

;; Length (in # of bytes).
(define_attr "length" ""
  (cond [(eq_attr "type" "load,fpload")
	 (if_then_else (match_operand 1 "symbolic_memory_operand" "")
		       (const_int 8) (const_int 4))

	 (eq_attr "type" "store,fpstore")
	 (if_then_else (match_operand 0 "symbolic_memory_operand" "")
		       (const_int 8) (const_int 4))

	 (eq_attr "type" "binary,shift,nullshift")
	 (if_then_else (match_operand 2 "arith_operand" "")
		       (const_int 4) (const_int 12))

	 (eq_attr "type" "move,unary,shift,nullshift")
	 (if_then_else (match_operand 1 "arith_operand" "")
		       (const_int 4) (const_int 8))]

	(const_int 4)))

(define_asm_attributes
  [(set_attr "length" "4")
   (set_attr "type" "multi")])

;; Attributes for instruction and branch scheduling

;; For conditional branches.
(define_attr "in_branch_delay" "false,true"
  (if_then_else (and (eq_attr "type" "!uncond_branch,btable_branch,branch,cbranch,fbranch,call,dyncall,multi,milli,parallel_branch")
		     (eq_attr "length" "4"))
		(const_string "true")
		(const_string "false")))

;; Disallow instructions which use the FPU since they will tie up the FPU
;; even if the instruction is nullified.
(define_attr "in_nullified_branch_delay" "false,true"
  (if_then_else (and (eq_attr "type" "!uncond_branch,btable_branch,branch,cbranch,fbranch,call,dyncall,multi,milli,fpcc,fpalu,fpmulsgl,fpmuldbl,fpdivsgl,fpdivdbl,fpsqrtsgl,fpsqrtdbl,parallel_branch")
		     (eq_attr "length" "4"))
		(const_string "true")
		(const_string "false")))

;; For calls and millicode calls.  Allow unconditional branches in the
;; delay slot.
(define_attr "in_call_delay" "false,true"
  (cond [(and (eq_attr "type" "!uncond_branch,btable_branch,branch,cbranch,fbranch,call,dyncall,multi,milli,parallel_branch")
	      (eq_attr "length" "4"))
	   (const_string "true")
	 (eq_attr "type" "uncond_branch")
	   (if_then_else (ne (symbol_ref "TARGET_JUMP_IN_DELAY")
			     (const_int 0))
			 (const_string "true")
			 (const_string "false"))]
	(const_string "false")))


;; Call delay slot description.
(define_delay (eq_attr "type" "call")
  [(eq_attr "in_call_delay" "true") (nil) (nil)])

;; Millicode call delay slot description.
(define_delay (eq_attr "type" "milli")
  [(eq_attr "in_call_delay" "true") (nil) (nil)])

;; Return and other similar instructions.
(define_delay (eq_attr "type" "btable_branch,branch,parallel_branch")
  [(eq_attr "in_branch_delay" "true") (nil) (nil)])

;; Floating point conditional branch delay slot description.
(define_delay (eq_attr "type" "fbranch")
  [(eq_attr "in_branch_delay" "true")
   (eq_attr "in_nullified_branch_delay" "true")
   (nil)])

;; Integer conditional branch delay slot description.
;; Nullification of conditional branches on the PA is dependent on the
;; direction of the branch.  Forward branches nullify true and
;; backward branches nullify false.  If the direction is unknown
;; then nullification is not allowed.
(define_delay (eq_attr "type" "cbranch")
  [(eq_attr "in_branch_delay" "true")
   (and (eq_attr "in_nullified_branch_delay" "true")
	(attr_flag "forward"))
   (and (eq_attr "in_nullified_branch_delay" "true")
	(attr_flag "backward"))])

(define_delay (and (eq_attr "type" "uncond_branch")
		   (eq (symbol_ref "following_call (insn)")
		       (const_int 0)))
  [(eq_attr "in_branch_delay" "true") (nil) (nil)])

;; Memory. Disregarding Cache misses, the Mustang memory times are:
;; load: 2, fpload: 3
;; store, fpstore: 3, no D-cache operations should be scheduled.

;; The Timex (aka 700) has two floating-point units: ALU, and MUL/DIV/SQRT.
;; Timings:
;; Instruction	Time	Unit	Minimum Distance (unit contention)
;; fcpy		3	ALU	2
;; fabs		3	ALU	2
;; fadd		3	ALU	2
;; fsub		3	ALU	2
;; fcmp		3	ALU	2
;; fcnv		3	ALU	2
;; fmpyadd	3	ALU,MPY	2
;; fmpysub	3	ALU,MPY 2
;; fmpycfxt	3	ALU,MPY 2
;; fmpy		3	MPY	2
;; fmpyi	3	MPY	2
;; fdiv,sgl	10	MPY	10
;; fdiv,dbl	12	MPY	12
;; fsqrt,sgl	14	MPY	14
;; fsqrt,dbl	18	MPY	18
;;
;; We don't model fmpyadd/fmpysub properly as those instructions
;; keep both the FP ALU and MPY units busy.  Given that these
;; processors are obsolete, I'm not going to spend the time to
;; model those instructions correctly.

(define_automaton "pa700")
(define_cpu_unit "dummy_700,mem_700,fpalu_700,fpmpy_700" "pa700")

(define_insn_reservation "W0" 4
  (and (eq_attr "type" "fpcc")
       (eq_attr "cpu" "700"))
  "fpalu_700*2")

(define_insn_reservation "W1" 3
  (and (eq_attr "type" "fpalu")
       (eq_attr "cpu" "700"))
  "fpalu_700*2")

(define_insn_reservation "W2" 3
  (and (eq_attr "type" "fpmulsgl,fpmuldbl")
       (eq_attr "cpu" "700"))
  "fpmpy_700*2")

(define_insn_reservation "W3" 10
  (and (eq_attr "type" "fpdivsgl")
       (eq_attr "cpu" "700"))
  "fpmpy_700*10")

(define_insn_reservation "W4" 12
  (and (eq_attr "type" "fpdivdbl")
       (eq_attr "cpu" "700"))
  "fpmpy_700*12")

(define_insn_reservation "W5" 14
  (and (eq_attr "type" "fpsqrtsgl")
       (eq_attr "cpu" "700"))
  "fpmpy_700*14")

(define_insn_reservation "W6" 18
  (and (eq_attr "type" "fpsqrtdbl")
       (eq_attr "cpu" "700"))
  "fpmpy_700*18")

(define_insn_reservation "W7" 2
  (and (eq_attr "type" "load")
       (eq_attr "cpu" "700"))
  "mem_700")

(define_insn_reservation "W8" 2
  (and (eq_attr "type" "fpload")
       (eq_attr "cpu" "700"))
  "mem_700")

(define_insn_reservation "W9" 3
  (and (eq_attr "type" "store")
       (eq_attr "cpu" "700"))
  "mem_700*3")

(define_insn_reservation "W10" 3
  (and (eq_attr "type" "fpstore")
       (eq_attr "cpu" "700"))
  "mem_700*3")

(define_insn_reservation "W11" 1
  (and (eq_attr "type" "!fpcc,fpalu,fpmulsgl,fpmuldbl,fpdivsgl,fpdivdbl,fpsqrtsgl,fpsqrtdbl,load,fpload,store,fpstore")
       (eq_attr "cpu" "700"))
  "dummy_700")

;; We have a bypass for all computations in the FP unit which feed an
;; FP store as long as the sizes are the same.
(define_bypass 2 "W1,W2" "W10" "hppa_fpstore_bypass_p")
(define_bypass 9 "W3" "W10" "hppa_fpstore_bypass_p")
(define_bypass 11 "W4" "W10" "hppa_fpstore_bypass_p")
(define_bypass 13 "W5" "W10" "hppa_fpstore_bypass_p")
(define_bypass 17 "W6" "W10" "hppa_fpstore_bypass_p")

;; We have an "anti-bypass" for FP loads which feed an FP store.
(define_bypass 4 "W8" "W10" "hppa_fpstore_bypass_p")

;; Function units for the 7100 and 7150.  The 7100/7150 can dual-issue
;; floating point computations with non-floating point computations (fp loads
;; and stores are not fp computations).
;;
;; Memory. Disregarding Cache misses, memory loads take two cycles; stores also
;; take two cycles, during which no Dcache operations should be scheduled.
;; Any special cases are handled in pa_adjust_cost.  The 7100, 7150 and 7100LC
;; all have the same memory characteristics if one disregards cache misses.
;;
;; The 7100/7150 has three floating-point units: ALU, MUL, and DIV.
;; There's no value in modeling the ALU and MUL separately though
;; since there can never be a functional unit conflict given the
;; latency and issue rates for those units.
;;
;; Timings:
;; Instruction	Time	Unit	Minimum Distance (unit contention)
;; fcpy		2	ALU	1
;; fabs		2	ALU	1
;; fadd		2	ALU	1
;; fsub		2	ALU	1
;; fcmp		2	ALU	1
;; fcnv		2	ALU	1
;; fmpyadd	2	ALU,MPY	1
;; fmpysub	2	ALU,MPY 1
;; fmpycfxt	2	ALU,MPY 1
;; fmpy		2	MPY	1
;; fmpyi	2	MPY	1
;; fdiv,sgl	8	DIV	8
;; fdiv,dbl	15	DIV	15
;; fsqrt,sgl	8	DIV	8
;; fsqrt,dbl	15	DIV	15

(define_automaton "pa7100")
(define_cpu_unit "i_7100, f_7100,fpmac_7100,fpdivsqrt_7100,mem_7100" "pa7100")

(define_insn_reservation "X0" 2
  (and (eq_attr "type" "fpcc,fpalu,fpmulsgl,fpmuldbl")
       (eq_attr "cpu" "7100"))
  "f_7100,fpmac_7100")

(define_insn_reservation "X1" 8
  (and (eq_attr "type" "fpdivsgl,fpsqrtsgl")
       (eq_attr "cpu" "7100"))
  "f_7100+fpdivsqrt_7100,fpdivsqrt_7100*7")

(define_insn_reservation "X2" 15
  (and (eq_attr "type" "fpdivdbl,fpsqrtdbl")
       (eq_attr "cpu" "7100"))
  "f_7100+fpdivsqrt_7100,fpdivsqrt_7100*14")

(define_insn_reservation "X3" 2
  (and (eq_attr "type" "load")
       (eq_attr "cpu" "7100"))
  "i_7100+mem_7100")

(define_insn_reservation "X4" 2
  (and (eq_attr "type" "fpload")
       (eq_attr "cpu" "7100"))
  "i_7100+mem_7100")

(define_insn_reservation "X5" 2
  (and (eq_attr "type" "store")
       (eq_attr "cpu" "7100"))
  "i_7100+mem_7100,mem_7100")

(define_insn_reservation "X6" 2
  (and (eq_attr "type" "fpstore")
       (eq_attr "cpu" "7100"))
  "i_7100+mem_7100,mem_7100")

(define_insn_reservation "X7" 1
  (and (eq_attr "type" "!fpcc,fpalu,fpmulsgl,fpmuldbl,fpdivsgl,fpsqrtsgl,fpdivdbl,fpsqrtdbl,load,fpload,store,fpstore")
       (eq_attr "cpu" "7100"))
  "i_7100")

;; We have a bypass for all computations in the FP unit which feed an
;; FP store as long as the sizes are the same.
(define_bypass 1 "X0" "X6" "hppa_fpstore_bypass_p")
(define_bypass 7 "X1" "X6" "hppa_fpstore_bypass_p")
(define_bypass 14 "X2" "X6" "hppa_fpstore_bypass_p")

;; We have an "anti-bypass" for FP loads which feed an FP store.
(define_bypass 3 "X4" "X6" "hppa_fpstore_bypass_p")

;; The 7100LC has three floating-point units: ALU, MUL, and DIV.
;; There's no value in modeling the ALU and MUL separately though
;; since there can never be a functional unit conflict that
;; can be avoided given the latency, issue rates and mandatory
;; one cycle cpu-wide lock for a double precision fp multiply.
;;
;; Timings:
;; Instruction	Time	Unit	Minimum Distance (unit contention)
;; fcpy		2	ALU	1
;; fabs		2	ALU	1
;; fadd		2	ALU	1
;; fsub		2	ALU	1
;; fcmp		2	ALU	1
;; fcnv		2	ALU	1
;; fmpyadd,sgl	2	ALU,MPY	1
;; fmpyadd,dbl	3	ALU,MPY	2
;; fmpysub,sgl	2	ALU,MPY 1
;; fmpysub,dbl	3	ALU,MPY 2
;; fmpycfxt,sgl	2	ALU,MPY 1
;; fmpycfxt,dbl	3	ALU,MPY 2
;; fmpy,sgl	2	MPY	1
;; fmpy,dbl	3	MPY	2
;; fmpyi	3	MPY	2
;; fdiv,sgl	8	DIV	8
;; fdiv,dbl	15	DIV	15
;; fsqrt,sgl	8	DIV	8
;; fsqrt,dbl	15	DIV	15
;;
;; The PA7200 is just like the PA7100LC except that there is
;; no store-store penalty.
;;
;; The PA7300 is just like the PA7200 except that there is
;; no store-load penalty.
;;
;; Note there are some aspects of the 7100LC we are not modeling
;; at the moment.  I'll be reviewing the 7100LC scheduling info
;; shortly and updating this description.
;;
;;   load-load pairs
;;   store-store pairs
;;   other issue modeling

(define_automaton "pa7100lc")
(define_cpu_unit "i0_7100lc, i1_7100lc, f_7100lc" "pa7100lc")
(define_cpu_unit "fpmac_7100lc" "pa7100lc")
(define_cpu_unit "mem_7100lc" "pa7100lc")

;; Double precision multiplies lock the entire CPU for one
;; cycle.  There is no way to avoid this lock and trying to
;; schedule around the lock is pointless and thus there is no
;; value in trying to model this lock.
;;
;; Not modeling the lock allows us to treat fp multiplies just
;; like any other FP alu instruction.  It allows for a smaller
;; DFA and may reduce register pressure.
(define_insn_reservation "Y0" 2
  (and (eq_attr "type" "fpcc,fpalu,fpmulsgl,fpmuldbl")
       (eq_attr "cpu" "7100LC,7200,7300"))
  "f_7100lc,fpmac_7100lc")

;; fp division and sqrt instructions lock the entire CPU for
;; 7 cycles (single precision) or 14 cycles (double precision).
;; There is no way to avoid this lock and trying to schedule
;; around the lock is pointless and thus there is no value in
;; trying to model this lock.  Not modeling the lock allows
;; for a smaller DFA and may reduce register pressure.
(define_insn_reservation "Y1" 1
  (and (eq_attr "type" "fpdivsgl,fpsqrtsgl,fpdivdbl,fpsqrtdbl")
       (eq_attr "cpu" "7100LC,7200,7300"))
  "f_7100lc")

(define_insn_reservation "Y2" 2
  (and (eq_attr "type" "load")
       (eq_attr "cpu" "7100LC,7200,7300"))
  "i1_7100lc+mem_7100lc")

(define_insn_reservation "Y3" 2
  (and (eq_attr "type" "fpload")
       (eq_attr "cpu" "7100LC,7200,7300"))
  "i1_7100lc+mem_7100lc")

(define_insn_reservation "Y4" 2
  (and (eq_attr "type" "store")
       (eq_attr "cpu" "7100LC"))
  "i1_7100lc+mem_7100lc,mem_7100lc")

(define_insn_reservation "Y5" 2
  (and (eq_attr "type" "fpstore")
       (eq_attr "cpu" "7100LC"))
  "i1_7100lc+mem_7100lc,mem_7100lc")

(define_insn_reservation "Y6" 1
  (and (eq_attr "type" "shift,nullshift")
       (eq_attr "cpu" "7100LC,7200,7300"))
  "i1_7100lc")

(define_insn_reservation "Y7" 1
  (and (eq_attr "type" "!fpcc,fpalu,fpmulsgl,fpmuldbl,fpdivsgl,fpsqrtsgl,fpdivdbl,fpsqrtdbl,load,fpload,store,fpstore,shift,nullshift")
       (eq_attr "cpu" "7100LC,7200,7300"))
  "(i0_7100lc|i1_7100lc)")

;; The 7200 has a store-load penalty
(define_insn_reservation "Y8" 2
  (and (eq_attr "type" "store")
       (eq_attr "cpu" "7200"))
  "i1_7100lc,mem_7100lc")

(define_insn_reservation "Y9" 2
  (and (eq_attr "type" "fpstore")
       (eq_attr "cpu" "7200"))
  "i1_7100lc,mem_7100lc")

;; The 7300 has no penalty for store-store or store-load
(define_insn_reservation "Y10" 2
  (and (eq_attr "type" "store")
       (eq_attr "cpu" "7300"))
  "i1_7100lc")

(define_insn_reservation "Y11" 2
  (and (eq_attr "type" "fpstore")
       (eq_attr "cpu" "7300"))
  "i1_7100lc")

;; We have an "anti-bypass" for FP loads which feed an FP store.
(define_bypass 3 "Y3" "Y5,Y9,Y11" "hppa_fpstore_bypass_p")

;; Scheduling for the PA8000 is somewhat different than scheduling for a
;; traditional architecture.
;;
;; The PA8000 has a large (56) entry reorder buffer that is split between
;; memory and non-memory operations.
;;
;; The PA8000 can issue two memory and two non-memory operations per cycle to
;; the function units, with the exception of branches and multi-output
;; instructions.  The PA8000 can retire two non-memory operations per cycle
;; and two memory operations per cycle, only one of which may be a store.
;;
;; Given the large reorder buffer, the processor can hide most latencies.
;; According to HP, they've got the best results by scheduling for retirement
;; bandwidth with limited latency scheduling for floating point operations.
;; Latency for integer operations and memory references is ignored.
;;
;;
;; We claim floating point operations have a 2 cycle latency and are
;; fully pipelined, except for div and sqrt which are not pipelined and
;; take from 17 to 31 cycles to complete.
;;
;; It's worth noting that there is no way to saturate all the functional
;; units on the PA8000 as there is not enough issue bandwidth.

(define_automaton "pa8000")
(define_cpu_unit "inm0_8000, inm1_8000, im0_8000, im1_8000" "pa8000")
(define_cpu_unit "rnm0_8000, rnm1_8000, rm0_8000, rm1_8000" "pa8000")
(define_cpu_unit "store_8000" "pa8000")
(define_cpu_unit "f0_8000, f1_8000" "pa8000")
(define_cpu_unit "fdivsqrt0_8000, fdivsqrt1_8000" "pa8000")
(define_reservation "inm_8000" "inm0_8000 | inm1_8000")
(define_reservation "im_8000" "im0_8000 | im1_8000")
(define_reservation "rnm_8000" "rnm0_8000 | rnm1_8000")
(define_reservation "rm_8000" "rm0_8000 | rm1_8000")
(define_reservation "f_8000" "f0_8000 | f1_8000")
(define_reservation "fdivsqrt_8000" "fdivsqrt0_8000 | fdivsqrt1_8000")

;; We can issue any two memops per cycle, but we can only retire
;; one memory store per cycle.  We assume that the reorder buffer
;; will hide any memory latencies per HP's recommendation.
(define_insn_reservation "Z0" 0
  (and
    (eq_attr "type" "load,fpload")
    (eq_attr "cpu" "8000"))
  "im_8000,rm_8000")

(define_insn_reservation "Z1" 0
  (and
    (eq_attr "type" "store,fpstore")
    (eq_attr "cpu" "8000"))
  "im_8000,rm_8000+store_8000")

;; We can issue and retire two non-memory operations per cycle with
;; a few exceptions (branches).  This group catches those we want
;; to assume have zero latency.
(define_insn_reservation "Z2" 0
  (and
    (eq_attr "type" "!load,fpload,store,fpstore,uncond_branch,btable_branch,branch,cbranch,fbranch,call,dyncall,multi,milli,parallel_branch,fpcc,fpalu,fpmulsgl,fpmuldbl,fpsqrtsgl,fpsqrtdbl,fpdivsgl,fpdivdbl")
    (eq_attr "cpu" "8000"))
  "inm_8000,rnm_8000")

;; Branches use both slots in the non-memory issue and
;; retirement unit.
(define_insn_reservation "Z3" 0
  (and
    (eq_attr "type" "uncond_branch,btable_branch,branch,cbranch,fbranch,call,dyncall,multi,milli,parallel_branch")
    (eq_attr "cpu" "8000"))
  "inm0_8000+inm1_8000,rnm0_8000+rnm1_8000")

;; We partial latency schedule the floating point units.
;; They can issue/retire two at a time in the non-memory
;; units.  We fix their latency at 2 cycles and they
;; are fully pipelined.
(define_insn_reservation "Z4" 1
 (and
   (eq_attr "type" "fpcc,fpalu,fpmulsgl,fpmuldbl")
   (eq_attr "cpu" "8000"))
 "inm_8000,f_8000,rnm_8000")

;; The fdivsqrt units are not pipelined and have a very long latency.  
;; To keep the DFA from exploding, we do not show all the
;; reservations for the divsqrt unit.
(define_insn_reservation "Z5" 17
 (and
   (eq_attr "type" "fpdivsgl,fpsqrtsgl")
   (eq_attr "cpu" "8000"))
 "inm_8000,fdivsqrt_8000*6,rnm_8000")

(define_insn_reservation "Z6" 31
 (and
   (eq_attr "type" "fpdivdbl,fpsqrtdbl")
   (eq_attr "cpu" "8000"))
 "inm_8000,fdivsqrt_8000*6,rnm_8000")

(include "predicates.md")

;; Compare instructions.
;; This controls RTL generation and register allocation.

;; We generate RTL for comparisons and branches by having the cmpxx
;; patterns store away the operands.  Then, the scc and bcc patterns
;; emit RTL for both the compare and the branch.
;;

(define_expand "cmpdi"
  [(set (reg:CC 0)
	(compare:CC (match_operand:DI 0 "reg_or_0_operand" "")
		    (match_operand:DI 1 "register_operand" "")))]
  "TARGET_64BIT"

  "
{
 hppa_compare_op0 = operands[0];
 hppa_compare_op1 = operands[1];
 hppa_branch_type = CMP_SI;
 DONE;
}")

(define_expand "cmpsi"
  [(set (reg:CC 0)
	(compare:CC (match_operand:SI 0 "reg_or_0_operand" "")
		    (match_operand:SI 1 "arith5_operand" "")))]
  ""
  "
{
 hppa_compare_op0 = operands[0];
 hppa_compare_op1 = operands[1];
 hppa_branch_type = CMP_SI;
 DONE;
}")

(define_expand "cmpsf"
  [(set (reg:CCFP 0)
	(compare:CCFP (match_operand:SF 0 "reg_or_0_operand" "")
		      (match_operand:SF 1 "reg_or_0_operand" "")))]
  "! TARGET_SOFT_FLOAT"
  "
{
  hppa_compare_op0 = operands[0];
  hppa_compare_op1 = operands[1];
  hppa_branch_type = CMP_SF;
  DONE;
}")

(define_expand "cmpdf"
  [(set (reg:CCFP 0)
      (compare:CCFP (match_operand:DF 0 "reg_or_0_operand" "")
                    (match_operand:DF 1 "reg_or_0_operand" "")))]
  "! TARGET_SOFT_FLOAT"
  "
{
  hppa_compare_op0 = operands[0];
  hppa_compare_op1 = operands[1];
  hppa_branch_type = CMP_DF;
  DONE;
}")

(define_insn ""
  [(set (reg:CCFP 0)
	(match_operator:CCFP 2 "comparison_operator"
			     [(match_operand:SF 0 "reg_or_0_operand" "fG")
			      (match_operand:SF 1 "reg_or_0_operand" "fG")]))]
  "! TARGET_SOFT_FLOAT"
  "fcmp,sgl,%Y2 %f0,%f1"
  [(set_attr "length" "4")
   (set_attr "type" "fpcc")])

(define_insn ""
  [(set (reg:CCFP 0)
	(match_operator:CCFP 2 "comparison_operator"
			     [(match_operand:DF 0 "reg_or_0_operand" "fG")
			      (match_operand:DF 1 "reg_or_0_operand" "fG")]))]
  "! TARGET_SOFT_FLOAT"
  "fcmp,dbl,%Y2 %f0,%f1"
  [(set_attr "length" "4")
   (set_attr "type" "fpcc")])

;; Provide a means to emit the movccfp0 and movccfp1 optimization
;; placeholders.  This is necessary in rare situations when a
;; placeholder is re-emitted (see PR 8705).

(define_expand "movccfp"
  [(set (reg:CCFP 0)
	(match_operand 0 "const_int_operand" ""))]
  "! TARGET_SOFT_FLOAT"
  "
{
  if ((unsigned HOST_WIDE_INT) INTVAL (operands[0]) > 1)
    FAIL;
}")

;; The following patterns are optimization placeholders.  In almost
;; all cases, the user of the condition code will be simplified and the
;; original condition code setting insn should be eliminated.

(define_insn "*movccfp0"
  [(set (reg:CCFP 0)
	(const_int 0))]
  "! TARGET_SOFT_FLOAT"
  "fcmp,dbl,= %%fr0,%%fr0"
  [(set_attr "length" "4")
   (set_attr "type" "fpcc")])

(define_insn "*movccfp1"
  [(set (reg:CCFP 0)
	(const_int 1))]
  "! TARGET_SOFT_FLOAT"
  "fcmp,dbl,!= %%fr0,%%fr0"
  [(set_attr "length" "4")
   (set_attr "type" "fpcc")])

;; scc insns.

(define_expand "seq"
  [(set (match_operand:SI 0 "register_operand" "")
	(eq:SI (match_dup 1)
	       (match_dup 2)))]
  "!TARGET_64BIT"
  "
{
  /* fp scc patterns rarely match, and are not a win on the PA.  */
  if (hppa_branch_type != CMP_SI)
    FAIL;
  /* set up operands from compare.  */
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
  /* fall through and generate default code */
}")

(define_expand "sne"
  [(set (match_operand:SI 0 "register_operand" "")
	(ne:SI (match_dup 1)
	       (match_dup 2)))]
  "!TARGET_64BIT"
  "
{
  /* fp scc patterns rarely match, and are not a win on the PA.  */
  if (hppa_branch_type != CMP_SI)
    FAIL;
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
}")

(define_expand "slt"
  [(set (match_operand:SI 0 "register_operand" "")
	(lt:SI (match_dup 1)
	       (match_dup 2)))]
  "!TARGET_64BIT"
  "
{
  /* fp scc patterns rarely match, and are not a win on the PA.  */
  if (hppa_branch_type != CMP_SI)
    FAIL;
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
}")

(define_expand "sgt"
  [(set (match_operand:SI 0 "register_operand" "")
	(gt:SI (match_dup 1)
	       (match_dup 2)))]
  "!TARGET_64BIT"
  "
{
  /* fp scc patterns rarely match, and are not a win on the PA.  */
  if (hppa_branch_type != CMP_SI)
    FAIL;
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
}")

(define_expand "sle"
  [(set (match_operand:SI 0 "register_operand" "")
	(le:SI (match_dup 1)
	       (match_dup 2)))]
  "!TARGET_64BIT"
  "
{
  /* fp scc patterns rarely match, and are not a win on the PA.  */
  if (hppa_branch_type != CMP_SI)
    FAIL;
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
}")

(define_expand "sge"
  [(set (match_operand:SI 0 "register_operand" "")
	(ge:SI (match_dup 1)
	       (match_dup 2)))]
  "!TARGET_64BIT"
  "
{
  /* fp scc patterns rarely match, and are not a win on the PA.  */
  if (hppa_branch_type != CMP_SI)
    FAIL;
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
}")

(define_expand "sltu"
  [(set (match_operand:SI 0 "register_operand" "")
	(ltu:SI (match_dup 1)
	        (match_dup 2)))]
  "!TARGET_64BIT"
  "
{
  if (hppa_branch_type != CMP_SI)
    FAIL;
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
}")

(define_expand "sgtu"
  [(set (match_operand:SI 0 "register_operand" "")
	(gtu:SI (match_dup 1)
	        (match_dup 2)))]
  "!TARGET_64BIT"
  "
{
  if (hppa_branch_type != CMP_SI)
    FAIL;
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
}")

(define_expand "sleu"
  [(set (match_operand:SI 0 "register_operand" "")
	(leu:SI (match_dup 1)
	        (match_dup 2)))]
  "!TARGET_64BIT"
  "
{
  if (hppa_branch_type != CMP_SI)
    FAIL;
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
}")

(define_expand "sgeu"
  [(set (match_operand:SI 0 "register_operand" "")
	(geu:SI (match_dup 1)
	        (match_dup 2)))]
  "!TARGET_64BIT"
  "
{
  if (hppa_branch_type != CMP_SI)
    FAIL;
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
}")

;; Instruction canonicalization puts immediate operands second, which
;; is the reverse of what we want.

(define_insn "scc"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(match_operator:SI 3 "comparison_operator"
			   [(match_operand:SI 1 "register_operand" "r")
			    (match_operand:SI 2 "arith11_operand" "rI")]))]
  ""
  "{com%I2clr|cmp%I2clr},%B3 %2,%1,%0\;ldi 1,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(match_operator:DI 3 "comparison_operator"
			   [(match_operand:DI 1 "register_operand" "r")
			    (match_operand:DI 2 "arith11_operand" "rI")]))]
  "TARGET_64BIT"
  "cmp%I2clr,*%B3 %2,%1,%0\;ldi 1,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn "iorscc"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ior:SI (match_operator:SI 3 "comparison_operator"
				   [(match_operand:SI 1 "register_operand" "r")
				    (match_operand:SI 2 "arith11_operand" "rI")])
		(match_operator:SI 6 "comparison_operator"
				   [(match_operand:SI 4 "register_operand" "r")
				    (match_operand:SI 5 "arith11_operand" "rI")])))]
  ""
  "{com%I2clr|cmp%I2clr},%S3 %2,%1,%%r0\;{com%I5clr|cmp%I5clr},%B6 %5,%4,%0\;ldi 1,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "12")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ior:DI (match_operator:DI 3 "comparison_operator"
				   [(match_operand:DI 1 "register_operand" "r")
				    (match_operand:DI 2 "arith11_operand" "rI")])
		(match_operator:DI 6 "comparison_operator"
				   [(match_operand:DI 4 "register_operand" "r")
				    (match_operand:DI 5 "arith11_operand" "rI")])))]
  "TARGET_64BIT"
  "cmp%I2clr,*%S3 %2,%1,%%r0\;cmp%I5clr,*%B6 %5,%4,%0\;ldi 1,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "12")])

;; Combiner patterns for common operations performed with the output
;; from an scc insn (negscc and incscc).
(define_insn "negscc"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(neg:SI (match_operator:SI 3 "comparison_operator"
	       [(match_operand:SI 1 "register_operand" "r")
		(match_operand:SI 2 "arith11_operand" "rI")])))]
  ""
  "{com%I2clr|cmp%I2clr},%B3 %2,%1,%0\;ldi -1,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(neg:DI (match_operator:DI 3 "comparison_operator"
	       [(match_operand:DI 1 "register_operand" "r")
		(match_operand:DI 2 "arith11_operand" "rI")])))]
  "TARGET_64BIT"
  "cmp%I2clr,*%B3 %2,%1,%0\;ldi -1,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

;; Patterns for adding/subtracting the result of a boolean expression from
;; a register.  First we have special patterns that make use of the carry
;; bit, and output only two instructions.  For the cases we can't in
;; general do in two instructions, the incscc pattern at the end outputs
;; two or three instructions.

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (leu:SI (match_operand:SI 2 "register_operand" "r")
			 (match_operand:SI 3 "arith11_operand" "rI"))
		 (match_operand:SI 1 "register_operand" "r")))]
  ""
  "sub%I3 %3,%2,%%r0\;{addc|add,c} %%r0,%1,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (leu:DI (match_operand:DI 2 "register_operand" "r")
			 (match_operand:DI 3 "arith11_operand" "rI"))
		 (match_operand:DI 1 "register_operand" "r")))]
  "TARGET_64BIT"
  "sub%I3 %3,%2,%%r0\;add,dc %%r0,%1,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

; This need only accept registers for op3, since canonicalization
; replaces geu with gtu when op3 is an integer.
(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (geu:SI (match_operand:SI 2 "register_operand" "r")
			 (match_operand:SI 3 "register_operand" "r"))
		 (match_operand:SI 1 "register_operand" "r")))]
  ""
  "sub %2,%3,%%r0\;{addc|add,c} %%r0,%1,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (geu:DI (match_operand:DI 2 "register_operand" "r")
			 (match_operand:DI 3 "register_operand" "r"))
		 (match_operand:DI 1 "register_operand" "r")))]
  "TARGET_64BIT"
  "sub %2,%3,%%r0\;add,dc %%r0,%1,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

; Match only integers for op3 here.  This is used as canonical form of the
; geu pattern when op3 is an integer.  Don't match registers since we can't
; make better code than the general incscc pattern.
(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (gtu:SI (match_operand:SI 2 "register_operand" "r")
			 (match_operand:SI 3 "int11_operand" "I"))
		 (match_operand:SI 1 "register_operand" "r")))]
  ""
  "addi %k3,%2,%%r0\;{addc|add,c} %%r0,%1,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (gtu:DI (match_operand:DI 2 "register_operand" "r")
			 (match_operand:DI 3 "int11_operand" "I"))
		 (match_operand:DI 1 "register_operand" "r")))]
  "TARGET_64BIT"
  "addi %k3,%2,%%r0\;add,dc %%r0,%1,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn "incscc"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
 	(plus:SI (match_operator:SI 4 "comparison_operator"
		    [(match_operand:SI 2 "register_operand" "r,r")
		     (match_operand:SI 3 "arith11_operand" "rI,rI")])
		 (match_operand:SI 1 "register_operand" "0,?r")))]
  ""
  "@
   {com%I3clr|cmp%I3clr},%B4 %3,%2,%%r0\;addi 1,%0,%0
   {com%I3clr|cmp%I3clr},%B4 %3,%2,%%r0\;addi,tr 1,%1,%0\;copy %1,%0"
  [(set_attr "type" "binary,binary")
   (set_attr "length" "8,12")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r,r")
 	(plus:DI (match_operator:DI 4 "comparison_operator"
		    [(match_operand:DI 2 "register_operand" "r,r")
		     (match_operand:DI 3 "arith11_operand" "rI,rI")])
		 (match_operand:DI 1 "register_operand" "0,?r")))]
  "TARGET_64BIT"
  "@
   cmp%I3clr,*%B4 %3,%2,%%r0\;addi 1,%0,%0
   cmp%I3clr,*%B4 %3,%2,%%r0\;addi,tr 1,%1,%0\;copy %1,%0"
  [(set_attr "type" "binary,binary")
   (set_attr "length" "8,12")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (match_operand:SI 1 "register_operand" "r")
		  (gtu:SI (match_operand:SI 2 "register_operand" "r")
			  (match_operand:SI 3 "arith11_operand" "rI"))))]
  ""
  "sub%I3 %3,%2,%%r0\;{subb|sub,b} %1,%%r0,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(minus:DI (match_operand:DI 1 "register_operand" "r")
		  (gtu:DI (match_operand:DI 2 "register_operand" "r")
			  (match_operand:DI 3 "arith11_operand" "rI"))))]
  "TARGET_64BIT"
  "sub%I3 %3,%2,%%r0\;sub,db %1,%%r0,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (minus:SI (match_operand:SI 1 "register_operand" "r")
			    (gtu:SI (match_operand:SI 2 "register_operand" "r")
				    (match_operand:SI 3 "arith11_operand" "rI")))
		  (match_operand:SI 4 "register_operand" "r")))]
  ""
  "sub%I3 %3,%2,%%r0\;{subb|sub,b} %1,%4,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(minus:DI (minus:DI (match_operand:DI 1 "register_operand" "r")
			    (gtu:DI (match_operand:DI 2 "register_operand" "r")
				    (match_operand:DI 3 "arith11_operand" "rI")))
		  (match_operand:DI 4 "register_operand" "r")))]
  "TARGET_64BIT"
  "sub%I3 %3,%2,%%r0\;sub,db %1,%4,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

; This need only accept registers for op3, since canonicalization
; replaces ltu with leu when op3 is an integer.
(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (match_operand:SI 1 "register_operand" "r")
		  (ltu:SI (match_operand:SI 2 "register_operand" "r")
			  (match_operand:SI 3 "register_operand" "r"))))]
  ""
  "sub %2,%3,%%r0\;{subb|sub,b} %1,%%r0,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(minus:DI (match_operand:DI 1 "register_operand" "r")
		  (ltu:DI (match_operand:DI 2 "register_operand" "r")
			  (match_operand:DI 3 "register_operand" "r"))))]
  "TARGET_64BIT"
  "sub %2,%3,%%r0\;sub,db %1,%%r0,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (minus:SI (match_operand:SI 1 "register_operand" "r")
			    (ltu:SI (match_operand:SI 2 "register_operand" "r")
				    (match_operand:SI 3 "register_operand" "r")))
		  (match_operand:SI 4 "register_operand" "r")))]
  ""
  "sub %2,%3,%%r0\;{subb|sub,b} %1,%4,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(minus:DI (minus:DI (match_operand:DI 1 "register_operand" "r")
			    (ltu:DI (match_operand:DI 2 "register_operand" "r")
				    (match_operand:DI 3 "register_operand" "r")))
		  (match_operand:DI 4 "register_operand" "r")))]
  "TARGET_64BIT"
  "sub %2,%3,%%r0\;sub,db %1,%4,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

; Match only integers for op3 here.  This is used as canonical form of the
; ltu pattern when op3 is an integer.  Don't match registers since we can't
; make better code than the general incscc pattern.
(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (match_operand:SI 1 "register_operand" "r")
		  (leu:SI (match_operand:SI 2 "register_operand" "r")
			  (match_operand:SI 3 "int11_operand" "I"))))]
  ""
  "addi %k3,%2,%%r0\;{subb|sub,b} %1,%%r0,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(minus:DI (match_operand:DI 1 "register_operand" "r")
		  (leu:DI (match_operand:DI 2 "register_operand" "r")
			  (match_operand:DI 3 "int11_operand" "I"))))]
  "TARGET_64BIT"
  "addi %k3,%2,%%r0\;sub,db %1,%%r0,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (minus:SI (match_operand:SI 1 "register_operand" "r")
			    (leu:SI (match_operand:SI 2 "register_operand" "r")
				    (match_operand:SI 3 "int11_operand" "I")))
		  (match_operand:SI 4 "register_operand" "r")))]
  ""
  "addi %k3,%2,%%r0\;{subb|sub,b} %1,%4,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(minus:DI (minus:DI (match_operand:DI 1 "register_operand" "r")
			    (leu:DI (match_operand:DI 2 "register_operand" "r")
				    (match_operand:DI 3 "int11_operand" "I")))
		  (match_operand:DI 4 "register_operand" "r")))]
  "TARGET_64BIT"
  "addi %k3,%2,%%r0\;sub,db %1,%4,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn "decscc"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(minus:SI (match_operand:SI 1 "register_operand" "0,?r")
		  (match_operator:SI 4 "comparison_operator"
		     [(match_operand:SI 2 "register_operand" "r,r")
		      (match_operand:SI 3 "arith11_operand" "rI,rI")])))]
  ""
  "@
   {com%I3clr|cmp%I3clr},%B4 %3,%2,%%r0\;addi -1,%0,%0
   {com%I3clr|cmp%I3clr},%B4 %3,%2,%%r0\;addi,tr -1,%1,%0\;copy %1,%0"
  [(set_attr "type" "binary,binary")
   (set_attr "length" "8,12")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(minus:DI (match_operand:DI 1 "register_operand" "0,?r")
		  (match_operator:DI 4 "comparison_operator"
		     [(match_operand:DI 2 "register_operand" "r,r")
		      (match_operand:DI 3 "arith11_operand" "rI,rI")])))]
  "TARGET_64BIT"
  "@
   cmp%I3clr,*%B4 %3,%2,%%r0\;addi -1,%0,%0
   cmp%I3clr,*%B4 %3,%2,%%r0\;addi,tr -1,%1,%0\;copy %1,%0"
  [(set_attr "type" "binary,binary")
   (set_attr "length" "8,12")])

; Patterns for max and min.  (There is no need for an earlyclobber in the
; last alternative since the middle alternative will match if op0 == op1.)

(define_insn "sminsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r")
	(smin:SI (match_operand:SI 1 "register_operand" "%0,0,r")
		 (match_operand:SI 2 "arith11_operand" "r,I,M")))]
  ""
  "@
  {comclr|cmpclr},> %2,%0,%%r0\;copy %2,%0
  {comiclr|cmpiclr},> %2,%0,%%r0\;ldi %2,%0
  {comclr|cmpclr},> %1,%r2,%0\;copy %1,%0"
[(set_attr "type" "multi,multi,multi")
 (set_attr "length" "8,8,8")])

(define_insn "smindi3"
  [(set (match_operand:DI 0 "register_operand" "=r,r,r")
	(smin:DI (match_operand:DI 1 "register_operand" "%0,0,r")
		 (match_operand:DI 2 "arith11_operand" "r,I,M")))]
  "TARGET_64BIT"
  "@
  cmpclr,*> %2,%0,%%r0\;copy %2,%0
  cmpiclr,*> %2,%0,%%r0\;ldi %2,%0
  cmpclr,*> %1,%r2,%0\;copy %1,%0"
[(set_attr "type" "multi,multi,multi")
 (set_attr "length" "8,8,8")])

(define_insn "uminsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(umin:SI (match_operand:SI 1 "register_operand" "%0,0")
		 (match_operand:SI 2 "arith11_operand" "r,I")))]
  ""
  "@
  {comclr|cmpclr},>> %2,%0,%%r0\;copy %2,%0
  {comiclr|cmpiclr},>> %2,%0,%%r0\;ldi %2,%0"
[(set_attr "type" "multi,multi")
 (set_attr "length" "8,8")])

(define_insn "umindi3"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(umin:DI (match_operand:DI 1 "register_operand" "%0,0")
		 (match_operand:DI 2 "arith11_operand" "r,I")))]
  "TARGET_64BIT"
  "@
  cmpclr,*>> %2,%0,%%r0\;copy %2,%0
  cmpiclr,*>> %2,%0,%%r0\;ldi %2,%0"
[(set_attr "type" "multi,multi")
 (set_attr "length" "8,8")])

(define_insn "smaxsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r")
	(smax:SI (match_operand:SI 1 "register_operand" "%0,0,r")
		 (match_operand:SI 2 "arith11_operand" "r,I,M")))]
  ""
  "@
  {comclr|cmpclr},< %2,%0,%%r0\;copy %2,%0
  {comiclr|cmpiclr},< %2,%0,%%r0\;ldi %2,%0
  {comclr|cmpclr},< %1,%r2,%0\;copy %1,%0"
[(set_attr "type" "multi,multi,multi")
 (set_attr "length" "8,8,8")])

(define_insn "smaxdi3"
  [(set (match_operand:DI 0 "register_operand" "=r,r,r")
	(smax:DI (match_operand:DI 1 "register_operand" "%0,0,r")
		 (match_operand:DI 2 "arith11_operand" "r,I,M")))]
  "TARGET_64BIT"
  "@
  cmpclr,*< %2,%0,%%r0\;copy %2,%0
  cmpiclr,*< %2,%0,%%r0\;ldi %2,%0
  cmpclr,*< %1,%r2,%0\;copy %1,%0"
[(set_attr "type" "multi,multi,multi")
 (set_attr "length" "8,8,8")])

(define_insn "umaxsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(umax:SI (match_operand:SI 1 "register_operand" "%0,0")
		 (match_operand:SI 2 "arith11_operand" "r,I")))]
  ""
  "@
  {comclr|cmpclr},<< %2,%0,%%r0\;copy %2,%0
  {comiclr|cmpiclr},<< %2,%0,%%r0\;ldi %2,%0"
[(set_attr "type" "multi,multi")
 (set_attr "length" "8,8")])

(define_insn "umaxdi3"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(umax:DI (match_operand:DI 1 "register_operand" "%0,0")
		 (match_operand:DI 2 "arith11_operand" "r,I")))]
  "TARGET_64BIT"
  "@
  cmpclr,*<< %2,%0,%%r0\;copy %2,%0
  cmpiclr,*<< %2,%0,%%r0\;ldi %2,%0"
[(set_attr "type" "multi,multi")
 (set_attr "length" "8,8")])

(define_insn "abssi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(abs:SI (match_operand:SI 1 "register_operand" "r")))]
  ""
  "or,>= %%r0,%1,%0\;subi 0,%0,%0"
  [(set_attr "type" "multi")
   (set_attr "length" "8")])

(define_insn "absdi2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(abs:DI (match_operand:DI 1 "register_operand" "r")))]
  "TARGET_64BIT"
  "or,*>= %%r0,%1,%0\;subi 0,%0,%0"
  [(set_attr "type" "multi")
   (set_attr "length" "8")])

;;; Experimental conditional move patterns

(define_expand "movsicc"
  [(set (match_operand:SI 0 "register_operand" "")
	(if_then_else:SI
	 (match_operator 1 "comparison_operator"
	    [(match_dup 4)
	     (match_dup 5)])
	 (match_operand:SI 2 "reg_or_cint_move_operand" "")
	 (match_operand:SI 3 "reg_or_cint_move_operand" "")))]
  ""
  "
{
  enum rtx_code code = GET_CODE (operands[1]);

  if (hppa_branch_type != CMP_SI)
    FAIL;

  if (GET_MODE (hppa_compare_op0) != GET_MODE (hppa_compare_op1)
      || GET_MODE (hppa_compare_op0) != GET_MODE (operands[0]))
    FAIL;

  /* operands[1] is currently the result of compare_from_rtx.  We want to
     emit a compare of the original operands.  */
  operands[1] = gen_rtx_fmt_ee (code, SImode, hppa_compare_op0, hppa_compare_op1);
  operands[4] = hppa_compare_op0;
  operands[5] = hppa_compare_op1;
}")

;; We used to accept any register for op1.
;;
;; However, it loses sometimes because the compiler will end up using
;; different registers for op0 and op1 in some critical cases.  local-alloc
;; will  not tie op0 and op1 because op0 is used in multiple basic blocks.
;;
;; If/when global register allocation supports tying we should allow any
;; register for op1 again.
(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r,r,r,r")
	(if_then_else:SI
	 (match_operator 2 "comparison_operator"
	    [(match_operand:SI 3 "register_operand" "r,r,r,r")
	     (match_operand:SI 4 "arith11_operand" "rI,rI,rI,rI")])
	 (match_operand:SI 1 "reg_or_cint_move_operand" "0,J,N,K")
	 (const_int 0)))]
  ""
  "@
   {com%I4clr|cmp%I4clr},%S2 %4,%3,%%r0\;ldi 0,%0
   {com%I4clr|cmp%I4clr},%B2 %4,%3,%0\;ldi %1,%0
   {com%I4clr|cmp%I4clr},%B2 %4,%3,%0\;ldil L'%1,%0
   {com%I4clr|cmp%I4clr},%B2 %4,%3,%0\;{zdepi|depwi,z} %Z1,%0"
  [(set_attr "type" "multi,multi,multi,nullshift")
   (set_attr "length" "8,8,8,8")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r,r,r,r,r,r,r,r")
	(if_then_else:SI
	 (match_operator 5 "comparison_operator"
	    [(match_operand:SI 3 "register_operand" "r,r,r,r,r,r,r,r")
	     (match_operand:SI 4 "arith11_operand" "rI,rI,rI,rI,rI,rI,rI,rI")])
	 (match_operand:SI 1 "reg_or_cint_move_operand" "0,0,0,0,r,J,N,K")
	 (match_operand:SI 2 "reg_or_cint_move_operand" "r,J,N,K,0,0,0,0")))]
  ""
  "@
   {com%I4clr|cmp%I4clr},%S5 %4,%3,%%r0\;copy %2,%0
   {com%I4clr|cmp%I4clr},%S5 %4,%3,%%r0\;ldi %2,%0
   {com%I4clr|cmp%I4clr},%S5 %4,%3,%%r0\;ldil L'%2,%0
   {com%I4clr|cmp%I4clr},%S5 %4,%3,%%r0\;{zdepi|depwi,z} %Z2,%0
   {com%I4clr|cmp%I4clr},%B5 %4,%3,%%r0\;copy %1,%0
   {com%I4clr|cmp%I4clr},%B5 %4,%3,%%r0\;ldi %1,%0
   {com%I4clr|cmp%I4clr},%B5 %4,%3,%%r0\;ldil L'%1,%0
   {com%I4clr|cmp%I4clr},%B5 %4,%3,%%r0\;{zdepi|depwi,z} %Z1,%0"
  [(set_attr "type" "multi,multi,multi,nullshift,multi,multi,multi,nullshift")
   (set_attr "length" "8,8,8,8,8,8,8,8")])

(define_expand "movdicc"
  [(set (match_operand:DI 0 "register_operand" "")
	(if_then_else:DI
	 (match_operator 1 "comparison_operator"
	    [(match_dup 4)
	     (match_dup 5)])
	 (match_operand:DI 2 "reg_or_cint_move_operand" "")
	 (match_operand:DI 3 "reg_or_cint_move_operand" "")))]
  "TARGET_64BIT"
  "
{
  enum rtx_code code = GET_CODE (operands[1]);

  if (hppa_branch_type != CMP_SI)
    FAIL;

  if (GET_MODE (hppa_compare_op0) != GET_MODE (hppa_compare_op1)
      || GET_MODE (hppa_compare_op0) != GET_MODE (operands[0]))
    FAIL;

  /* operands[1] is currently the result of compare_from_rtx.  We want to
     emit a compare of the original operands.  */
  operands[1] = gen_rtx_fmt_ee (code, DImode, hppa_compare_op0, hppa_compare_op1);
  operands[4] = hppa_compare_op0;
  operands[5] = hppa_compare_op1;
}")

; We need the first constraint alternative in order to avoid
; earlyclobbers on all other alternatives.
(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r,r,r,r,r")
	(if_then_else:DI
	 (match_operator 2 "comparison_operator"
	    [(match_operand:DI 3 "register_operand" "r,r,r,r,r")
	     (match_operand:DI 4 "arith11_operand" "rI,rI,rI,rI,rI")])
	 (match_operand:DI 1 "reg_or_cint_move_operand" "0,r,J,N,K")
	 (const_int 0)))]
  "TARGET_64BIT"
  "@
   cmp%I4clr,*%S2 %4,%3,%%r0\;ldi 0,%0
   cmp%I4clr,*%B2 %4,%3,%0\;copy %1,%0
   cmp%I4clr,*%B2 %4,%3,%0\;ldi %1,%0
   cmp%I4clr,*%B2 %4,%3,%0\;ldil L'%1,%0
   cmp%I4clr,*%B2 %4,%3,%0\;depdi,z %z1,%0"
  [(set_attr "type" "multi,multi,multi,multi,nullshift")
   (set_attr "length" "8,8,8,8,8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r,r,r,r,r,r,r,r")
	(if_then_else:DI
	 (match_operator 5 "comparison_operator"
	    [(match_operand:DI 3 "register_operand" "r,r,r,r,r,r,r,r")
	     (match_operand:DI 4 "arith11_operand" "rI,rI,rI,rI,rI,rI,rI,rI")])
	 (match_operand:DI 1 "reg_or_cint_move_operand" "0,0,0,0,r,J,N,K")
	 (match_operand:DI 2 "reg_or_cint_move_operand" "r,J,N,K,0,0,0,0")))]
  "TARGET_64BIT"
  "@
   cmp%I4clr,*%S5 %4,%3,%%r0\;copy %2,%0
   cmp%I4clr,*%S5 %4,%3,%%r0\;ldi %2,%0
   cmp%I4clr,*%S5 %4,%3,%%r0\;ldil L'%2,%0
   cmp%I4clr,*%S5 %4,%3,%%r0\;depdi,z %z2,%0
   cmp%I4clr,*%B5 %4,%3,%%r0\;copy %1,%0
   cmp%I4clr,*%B5 %4,%3,%%r0\;ldi %1,%0
   cmp%I4clr,*%B5 %4,%3,%%r0\;ldil L'%1,%0
   cmp%I4clr,*%B5 %4,%3,%%r0\;depdi,z %z1,%0"
  [(set_attr "type" "multi,multi,multi,nullshift,multi,multi,multi,nullshift")
   (set_attr "length" "8,8,8,8,8,8,8,8")])

;; Conditional Branches

(define_expand "beq"
  [(set (pc)
	(if_then_else (eq (match_dup 1) (match_dup 2))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  if (hppa_branch_type != CMP_SI)
    {
      emit_insn (gen_cmp_fp (EQ, hppa_compare_op0, hppa_compare_op1));
      emit_bcond_fp (NE, operands[0]);
      DONE;
    }
  /* set up operands from compare.  */
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
  /* fall through and generate default code */
}")

(define_expand "bne"
  [(set (pc)
	(if_then_else (ne (match_dup 1) (match_dup 2))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  if (hppa_branch_type != CMP_SI)
    {
      emit_insn (gen_cmp_fp (NE, hppa_compare_op0, hppa_compare_op1));
      emit_bcond_fp (NE, operands[0]);
      DONE;
    }
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
}")

(define_expand "bgt"
  [(set (pc)
	(if_then_else (gt (match_dup 1) (match_dup 2))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  if (hppa_branch_type != CMP_SI)
    {
      emit_insn (gen_cmp_fp (GT, hppa_compare_op0, hppa_compare_op1));
      emit_bcond_fp (NE, operands[0]);
      DONE;
    }
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
}")

(define_expand "blt"
  [(set (pc)
	(if_then_else (lt (match_dup 1) (match_dup 2))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  if (hppa_branch_type != CMP_SI)
    {
      emit_insn (gen_cmp_fp (LT, hppa_compare_op0, hppa_compare_op1));
      emit_bcond_fp (NE, operands[0]);
      DONE;
    }
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
}")

(define_expand "bge"
  [(set (pc)
	(if_then_else (ge (match_dup 1) (match_dup 2))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  if (hppa_branch_type != CMP_SI)
    {
      emit_insn (gen_cmp_fp (GE, hppa_compare_op0, hppa_compare_op1));
      emit_bcond_fp (NE, operands[0]);
      DONE;
    }
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
}")

(define_expand "ble"
  [(set (pc)
	(if_then_else (le (match_dup 1) (match_dup 2))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  if (hppa_branch_type != CMP_SI)
    {
      emit_insn (gen_cmp_fp (LE, hppa_compare_op0, hppa_compare_op1));
      emit_bcond_fp (NE, operands[0]);
      DONE;
    }
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
}")

(define_expand "bgtu"
  [(set (pc)
	(if_then_else (gtu (match_dup 1) (match_dup 2))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  if (hppa_branch_type != CMP_SI)
    FAIL;
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
}")

(define_expand "bltu"
  [(set (pc)
	(if_then_else (ltu (match_dup 1) (match_dup 2))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  if (hppa_branch_type != CMP_SI)
    FAIL;
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
}")

(define_expand "bgeu"
  [(set (pc)
	(if_then_else (geu (match_dup 1) (match_dup 2))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  if (hppa_branch_type != CMP_SI)
    FAIL;
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
}")

(define_expand "bleu"
  [(set (pc)
	(if_then_else (leu (match_dup 1) (match_dup 2))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  if (hppa_branch_type != CMP_SI)
    FAIL;
  operands[1] = hppa_compare_op0;
  operands[2] = hppa_compare_op1;
}")

(define_expand "bltgt"
  [(set (pc)
	(if_then_else (ltgt (match_dup 1) (match_dup 2))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  if (hppa_branch_type == CMP_SI)
    FAIL;
  emit_insn (gen_cmp_fp (LTGT, hppa_compare_op0, hppa_compare_op1));
  emit_bcond_fp (NE, operands[0]);
  DONE;
}")

(define_expand "bunle"
  [(set (pc)
	(if_then_else (unle (match_dup 1) (match_dup 2))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  if (hppa_branch_type == CMP_SI)
    FAIL;
  emit_insn (gen_cmp_fp (UNLE, hppa_compare_op0, hppa_compare_op1));
  emit_bcond_fp (NE, operands[0]);
  DONE;
}")

(define_expand "bunlt"
  [(set (pc)
	(if_then_else (unlt (match_dup 1) (match_dup 2))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  if (hppa_branch_type == CMP_SI)
    FAIL;
  emit_insn (gen_cmp_fp (UNLT, hppa_compare_op0, hppa_compare_op1));
  emit_bcond_fp (NE, operands[0]);
  DONE;
}")

(define_expand "bunge"
  [(set (pc)
	(if_then_else (unge (match_dup 1) (match_dup 2))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  if (hppa_branch_type == CMP_SI)
    FAIL;
  emit_insn (gen_cmp_fp (UNGE, hppa_compare_op0, hppa_compare_op1));
  emit_bcond_fp (NE, operands[0]);
  DONE;
}")

(define_expand "bungt"
  [(set (pc)
	(if_then_else (ungt (match_dup 1) (match_dup 2))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  if (hppa_branch_type == CMP_SI)
    FAIL;
  emit_insn (gen_cmp_fp (UNGT, hppa_compare_op0, hppa_compare_op1));
  emit_bcond_fp (NE, operands[0]);
  DONE;
}")

(define_expand "buneq"
  [(set (pc)
	(if_then_else (uneq (match_dup 1) (match_dup 2))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  if (hppa_branch_type == CMP_SI)
    FAIL;
  emit_insn (gen_cmp_fp (UNEQ, hppa_compare_op0, hppa_compare_op1));
  emit_bcond_fp (NE, operands[0]);
  DONE;
}")

(define_expand "bunordered"
  [(set (pc)
	(if_then_else (unordered (match_dup 1) (match_dup 2))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  if (hppa_branch_type == CMP_SI)
    FAIL;
  emit_insn (gen_cmp_fp (UNORDERED, hppa_compare_op0, hppa_compare_op1));
  emit_bcond_fp (NE, operands[0]);
  DONE;
}")

(define_expand "bordered"
  [(set (pc)
	(if_then_else (ordered (match_dup 1) (match_dup 2))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  if (hppa_branch_type == CMP_SI)
    FAIL;
  emit_insn (gen_cmp_fp (ORDERED, hppa_compare_op0, hppa_compare_op1));
  emit_bcond_fp (NE, operands[0]);
  DONE;
}")

;; Match the branch patterns.


;; Note a long backward conditional branch with an annulled delay slot
;; has a length of 12.
(define_insn ""
  [(set (pc)
	(if_then_else
	 (match_operator 3 "comparison_operator"
			 [(match_operand:SI 1 "reg_or_0_operand" "rM")
			  (match_operand:SI 2 "arith5_operand" "rL")])
	 (label_ref (match_operand 0 "" ""))
	 (pc)))]
  ""
  "*
{
  return output_cbranch (operands, 0, insn);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 0) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 0) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

;; Match the negated branch.

(define_insn ""
  [(set (pc)
	(if_then_else
	 (match_operator 3 "comparison_operator"
			 [(match_operand:SI 1 "reg_or_0_operand" "rM")
			  (match_operand:SI 2 "arith5_operand" "rL")])
	 (pc)
	 (label_ref (match_operand 0 "" ""))))]
  ""
  "*
{
  return output_cbranch (operands, 1, insn);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 0) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 0) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc)
	(if_then_else
	 (match_operator 3 "comparison_operator"
			 [(match_operand:DI 1 "reg_or_0_operand" "rM")
			  (match_operand:DI 2 "reg_or_0_operand" "rM")])
	 (label_ref (match_operand 0 "" ""))
	 (pc)))]
  "TARGET_64BIT"
  "*
{
  return output_cbranch (operands, 0, insn);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 0) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 0) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

;; Match the negated branch.

(define_insn ""
  [(set (pc)
	(if_then_else
	 (match_operator 3 "comparison_operator"
			 [(match_operand:DI 1 "reg_or_0_operand" "rM")
			  (match_operand:DI 2 "reg_or_0_operand" "rM")])
	 (pc)
	 (label_ref (match_operand 0 "" ""))))]
  "TARGET_64BIT"
  "*
{
  return output_cbranch (operands, 1, insn);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 0) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 0) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])
(define_insn ""
  [(set (pc)
	(if_then_else
	 (match_operator 3 "cmpib_comparison_operator"
			 [(match_operand:DI 1 "reg_or_0_operand" "rM")
			  (match_operand:DI 2 "arith5_operand" "rL")])
	 (label_ref (match_operand 0 "" ""))
	 (pc)))]
  "TARGET_64BIT"
  "*
{
  return output_cbranch (operands, 0, insn);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 0) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 0) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

;; Match the negated branch.

(define_insn ""
  [(set (pc)
	(if_then_else
	 (match_operator 3 "cmpib_comparison_operator"
			 [(match_operand:DI 1 "reg_or_0_operand" "rM")
			  (match_operand:DI 2 "arith5_operand" "rL")])
	 (pc)
	 (label_ref (match_operand 0 "" ""))))]
  "TARGET_64BIT"
  "*
{
  return output_cbranch (operands, 1, insn);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 0) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 0) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

;; Branch on Bit patterns.
(define_insn ""
  [(set (pc)
	(if_then_else
	 (ne (zero_extract:SI (match_operand:SI 0 "register_operand" "r")
			      (const_int 1)
			      (match_operand:SI 1 "uint5_operand" ""))
	     (const_int 0))
	 (label_ref (match_operand 2 "" ""))
	 (pc)))]
  ""
  "*
{
  return output_bb (operands, 0, insn, 0);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc)
	(if_then_else
	 (ne (zero_extract:DI (match_operand:DI 0 "register_operand" "r")
			      (const_int 1)
			      (match_operand:DI 1 "uint32_operand" ""))
	     (const_int 0))
	 (label_ref (match_operand 2 "" ""))
	 (pc)))]
  "TARGET_64BIT"
  "*
{
  return output_bb (operands, 0, insn, 0);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc)
	(if_then_else
	 (ne (zero_extract:SI (match_operand:SI 0 "register_operand" "r")
			      (const_int 1)
			      (match_operand:SI 1 "uint5_operand" ""))
	     (const_int 0))
	 (pc)
	 (label_ref (match_operand 2 "" ""))))]
  ""
  "*
{
  return output_bb (operands, 1, insn, 0);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc)
	(if_then_else
	 (ne (zero_extract:DI (match_operand:DI 0 "register_operand" "r")
			      (const_int 1)
			      (match_operand:DI 1 "uint32_operand" ""))
	     (const_int 0))
	 (pc)
	 (label_ref (match_operand 2 "" ""))))]
  "TARGET_64BIT"
  "*
{
  return output_bb (operands, 1, insn, 0);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc)
	(if_then_else
	 (eq (zero_extract:SI (match_operand:SI 0 "register_operand" "r")
			      (const_int 1)
			      (match_operand:SI 1 "uint5_operand" ""))
	     (const_int 0))
	 (label_ref (match_operand 2 "" ""))
	 (pc)))]
  ""
  "*
{
  return output_bb (operands, 0, insn, 1);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc)
	(if_then_else
	 (eq (zero_extract:DI (match_operand:DI 0 "register_operand" "r")
			      (const_int 1)
			      (match_operand:DI 1 "uint32_operand" ""))
	     (const_int 0))
	 (label_ref (match_operand 2 "" ""))
	 (pc)))]
  "TARGET_64BIT"
  "*
{
  return output_bb (operands, 0, insn, 1);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc)
	(if_then_else
	 (eq (zero_extract:SI (match_operand:SI 0 "register_operand" "r")
			      (const_int 1)
			      (match_operand:SI 1 "uint5_operand" ""))
	     (const_int 0))
	 (pc)
	 (label_ref (match_operand 2 "" ""))))]
  ""
  "*
{
  return output_bb (operands, 1, insn, 1);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc)
	(if_then_else
	 (eq (zero_extract:DI (match_operand:DI 0 "register_operand" "r")
			      (const_int 1)
			      (match_operand:DI 1 "uint32_operand" ""))
	     (const_int 0))
	 (pc)
	 (label_ref (match_operand 2 "" ""))))]
  "TARGET_64BIT"
  "*
{
  return output_bb (operands, 1, insn, 1);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

;; Branch on Variable Bit patterns.
(define_insn ""
  [(set (pc)
	(if_then_else
	 (ne (zero_extract:SI (match_operand:SI 0 "register_operand" "r")
			      (const_int 1)
			      (match_operand:SI 1 "register_operand" "q"))
	     (const_int 0))
	 (label_ref (match_operand 2 "" ""))
	 (pc)))]
  ""
  "*
{
  return output_bvb (operands, 0, insn, 0);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc)
	(if_then_else
	 (ne (zero_extract:DI (match_operand:DI 0 "register_operand" "r")
			      (const_int 1)
			      (match_operand:DI 1 "register_operand" "q"))
	     (const_int 0))
	 (label_ref (match_operand 2 "" ""))
	 (pc)))]
  "TARGET_64BIT"
  "*
{
  return output_bvb (operands, 0, insn, 0);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc)
	(if_then_else
	 (ne (zero_extract:SI (match_operand:SI 0 "register_operand" "r")
			      (const_int 1)
			      (match_operand:SI 1 "register_operand" "q"))
	     (const_int 0))
	 (pc)
	 (label_ref (match_operand 2 "" ""))))]
  ""
  "*
{
  return output_bvb (operands, 1, insn, 0);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc)
	(if_then_else
	 (ne (zero_extract:DI (match_operand:DI 0 "register_operand" "r")
			      (const_int 1)
			      (match_operand:DI 1 "register_operand" "q"))
	     (const_int 0))
	 (pc)
	 (label_ref (match_operand 2 "" ""))))]
  "TARGET_64BIT"
  "*
{
  return output_bvb (operands, 1, insn, 0);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc)
	(if_then_else
	 (eq (zero_extract:SI (match_operand:SI 0 "register_operand" "r")
			      (const_int 1)
			      (match_operand:SI 1 "register_operand" "q"))
	     (const_int 0))
	 (label_ref (match_operand 2 "" ""))
	 (pc)))]
  ""
  "*
{
  return output_bvb (operands, 0, insn, 1);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc)
	(if_then_else
	 (eq (zero_extract:DI (match_operand:DI 0 "register_operand" "r")
			      (const_int 1)
			      (match_operand:DI 1 "register_operand" "q"))
	     (const_int 0))
	 (label_ref (match_operand 2 "" ""))
	 (pc)))]
  "TARGET_64BIT"
  "*
{
  return output_bvb (operands, 0, insn, 1);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc)
	(if_then_else
	 (eq (zero_extract:SI (match_operand:SI 0 "register_operand" "r")
			      (const_int 1)
			      (match_operand:SI 1 "register_operand" "q"))
	     (const_int 0))
	 (pc)
	 (label_ref (match_operand 2 "" ""))))]
  ""
  "*
{
  return output_bvb (operands, 1, insn, 1);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc)
	(if_then_else
	 (eq (zero_extract:DI (match_operand:DI 0 "register_operand" "r")
			      (const_int 1)
			      (match_operand:DI 1 "register_operand" "q"))
	     (const_int 0))
	 (pc)
	 (label_ref (match_operand 2 "" ""))))]
  "TARGET_64BIT"
  "*
{
  return output_bvb (operands, 1, insn, 1);
}"
[(set_attr "type" "cbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

;; Floating point branches

;; ??? Nullification is handled differently from other branches.
;; If nullification is specified, the delay slot is nullified on any
;; taken branch regardless of branch direction.
(define_insn ""
  [(set (pc) (if_then_else (ne (reg:CCFP 0) (const_int 0))
			   (label_ref (match_operand 0 "" ""))
			   (pc)))]
  "!TARGET_SOFT_FLOAT"
  "*
{
  int length = get_attr_length (insn);
  rtx xoperands[1];
  int nullify, xdelay;

  if (length < 16)
    return \"ftest\;b%* %l0\";

  if (dbr_sequence_length () == 0 || INSN_ANNULLED_BRANCH_P (insn))
    {
      nullify = 1;
      xdelay = 0;
      xoperands[0] = GEN_INT (length - 8);
    }
  else
    {
      nullify = 0;
      xdelay = 1;
      xoperands[0] = GEN_INT (length - 4);
    }

  if (nullify)
    output_asm_insn (\"ftest\;add,tr %%r0,%%r0,%%r0\;b,n .+%0\", xoperands);
  else
    output_asm_insn (\"ftest\;add,tr %%r0,%%r0,%%r0\;b .+%0\", xoperands);
  return output_lbranch (operands[0], insn, xdelay);
}"
[(set_attr "type" "fbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 0) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 32)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 28)]
	  (const_int 36)))])

(define_insn ""
  [(set (pc) (if_then_else (ne (reg:CCFP 0) (const_int 0))
			   (pc)
			   (label_ref (match_operand 0 "" ""))))]
  "!TARGET_SOFT_FLOAT"
  "*
{
  int length = get_attr_length (insn);
  rtx xoperands[1];
  int nullify, xdelay;

  if (length < 16)
    return \"ftest\;add,tr %%r0,%%r0,%%r0\;b%* %0\";

  if (dbr_sequence_length () == 0 || INSN_ANNULLED_BRANCH_P (insn))
    {
      nullify = 1;
      xdelay = 0;
      xoperands[0] = GEN_INT (length - 4);
    }
  else
    {
      nullify = 0;
      xdelay = 1;
      xoperands[0] = GEN_INT (length);
    }

  if (nullify)
    output_asm_insn (\"ftest\;b,n .+%0\", xoperands);
  else
    output_asm_insn (\"ftest\;b .+%0\", xoperands);
  return output_lbranch (operands[0], insn, xdelay);
}"
[(set_attr "type" "fbranch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 0) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 12)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 28)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 24)]
	  (const_int 32)))])

;; Move instructions

(define_expand "movsi"
  [(set (match_operand:SI 0 "general_operand" "")
	(match_operand:SI 1 "general_operand" ""))]
  ""
  "
{
  if (emit_move_sequence (operands, SImode, 0))
    DONE;
}")

;; Handle SImode input reloads requiring %r1 as a scratch register.
(define_expand "reload_insi_r1"
  [(set (match_operand:SI 0 "register_operand" "=Z")
	(match_operand:SI 1 "non_hard_reg_operand" ""))
   (clobber (match_operand:SI 2 "register_operand" "=&a"))]
  ""
  "
{
  if (emit_move_sequence (operands, SImode, operands[2]))
    DONE;

  /* We don't want the clobber emitted, so handle this ourselves.  */
  emit_insn (gen_rtx_SET (VOIDmode, operands[0], operands[1]));
  DONE;
}")

;; Handle SImode input reloads requiring a general register as a
;; scratch register.
(define_expand "reload_insi"
  [(set (match_operand:SI 0 "register_operand" "=Z")
	(match_operand:SI 1 "non_hard_reg_operand" ""))
   (clobber (match_operand:SI 2 "register_operand" "=&r"))]
  ""
  "
{
  if (emit_move_sequence (operands, SImode, operands[2]))
    DONE;

  /* We don't want the clobber emitted, so handle this ourselves.  */
  emit_insn (gen_rtx_SET (VOIDmode, operands[0], operands[1]));
  DONE;
}")

;; Handle SImode output reloads requiring a general register as a
;; scratch register.
(define_expand "reload_outsi"
  [(set (match_operand:SI 0 "non_hard_reg_operand" "")
	(match_operand:SI 1  "register_operand" "Z"))
   (clobber (match_operand:SI 2 "register_operand" "=&r"))]
  ""
  "
{
  if (emit_move_sequence (operands, SImode, operands[2]))
    DONE;

  /* We don't want the clobber emitted, so handle this ourselves.  */
  emit_insn (gen_rtx_SET (VOIDmode, operands[0], operands[1]));
  DONE;
}")

(define_insn ""
  [(set (match_operand:SI 0 "move_dest_operand"
			  "=r,r,r,r,r,r,Q,!*q,!r,!*f,*f,T,?r,?*f")
	(match_operand:SI 1 "move_src_operand"
			  "A,r,J,N,K,RQ,rM,!rM,!*q,!*fM,RT,*f,*f,r"))]
  "(register_operand (operands[0], SImode)
    || reg_or_0_operand (operands[1], SImode))
   && !TARGET_SOFT_FLOAT
   && !TARGET_64BIT"
  "@
   ldw RT'%A1,%0
   copy %1,%0
   ldi %1,%0
   ldil L'%1,%0
   {zdepi|depwi,z} %Z1,%0
   ldw%M1 %1,%0
   stw%M0 %r1,%0
   mtsar %r1
   {mfctl|mfctl,w} %%sar,%0
   fcpy,sgl %f1,%0
   fldw%F1 %1,%0
   fstw%F0 %1,%0
   {fstws|fstw} %1,-16(%%sp)\n\t{ldws|ldw} -16(%%sp),%0
   {stws|stw} %1,-16(%%sp)\n\t{fldws|fldw} -16(%%sp),%0"
  [(set_attr "type" "load,move,move,move,shift,load,store,move,move,fpalu,fpload,fpstore,move,move")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4,4,4,4,4,4,4,4,4,4,4,4,8,8")])

(define_insn ""
  [(set (match_operand:SI 0 "move_dest_operand"
			  "=r,r,r,r,r,r,Q,!*q,!r,!*f,*f,T")
	(match_operand:SI 1 "move_src_operand"
			  "A,r,J,N,K,RQ,rM,!rM,!*q,!*fM,RT,*f"))]
  "(register_operand (operands[0], SImode)
    || reg_or_0_operand (operands[1], SImode))
   && !TARGET_SOFT_FLOAT
   && TARGET_64BIT"
  "@
   ldw RT'%A1,%0
   copy %1,%0
   ldi %1,%0
   ldil L'%1,%0
   {zdepi|depwi,z} %Z1,%0
   ldw%M1 %1,%0
   stw%M0 %r1,%0
   mtsar %r1
   {mfctl|mfctl,w} %%sar,%0
   fcpy,sgl %f1,%0
   fldw%F1 %1,%0
   fstw%F0 %1,%0"
  [(set_attr "type" "load,move,move,move,shift,load,store,move,move,fpalu,fpload,fpstore")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4,4,4,4,4,4,4,4,4,4,4,4")])

(define_insn ""
  [(set (match_operand:SI 0 "indexed_memory_operand" "=R")
	(match_operand:SI 1 "register_operand" "f"))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && reload_completed"
  "fstw%F0 %1,%0"
  [(set_attr "type" "fpstore")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4")])

; Rewrite RTL using an indexed store.  This will allow the insn that
; computes the address to be deleted if the register it sets is dead.
(define_peephole2
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (mult:SI (match_operand:SI 1 "register_operand" "")
			  (const_int 4))
		 (match_operand:SI 2 "register_operand" "")))
   (set (mem:SI (match_dup 0))
        (match_operand:SI 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:SI (plus:SI (mult:SI (match_dup 1) (const_int 4)) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:SI (mult:SI (match_dup 1) (const_int 4))
			       (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (match_operand:SI 2 "register_operand" "")
		 (mult:SI (match_operand:SI 1 "register_operand" "")
			  (const_int 4))))
   (set (mem:SI (match_dup 0))
        (match_operand:SI 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:SI (plus:SI (mult:SI (match_dup 1) (const_int 4)) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:SI (mult:SI (match_dup 1) (const_int 4))
			       (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (mult:DI (match_operand:DI 1 "register_operand" "")
			  (const_int 4))
		 (match_operand:DI 2 "register_operand" "")))
   (set (mem:SI (match_dup 0))
        (match_operand:SI 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_64BIT
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:SI (plus:DI (mult:DI (match_dup 1) (const_int 4)) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:DI (mult:DI (match_dup 1) (const_int 4))
			       (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (match_operand:DI 2 "register_operand" "")
		 (mult:DI (match_operand:DI 1 "register_operand" "")
			  (const_int 4))))
   (set (mem:SI (match_dup 0))
        (match_operand:SI 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_64BIT
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:SI (plus:DI (mult:DI (match_dup 1) (const_int 4)) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:DI (mult:DI (match_dup 1) (const_int 4))
			       (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (match_operand:SI 1 "register_operand" "")
		 (match_operand:SI 2 "register_operand" "")))
   (set (mem:SI (match_dup 0))
        (match_operand:SI 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_NO_SPACE_REGS
   && REG_OK_FOR_INDEX_P (operands[1])
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:SI (plus:SI (match_dup 1) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:SI (match_dup 1) (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (match_operand:SI 1 "register_operand" "")
		 (match_operand:SI 2 "register_operand" "")))
   (set (mem:SI (match_dup 0))
        (match_operand:SI 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_NO_SPACE_REGS
   && REG_OK_FOR_BASE_P (operands[1])
   && REG_OK_FOR_INDEX_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:SI (plus:SI (match_dup 2) (match_dup 1)))
	(match_dup 3))
   (set (match_dup 0) (plus:SI (match_dup 2) (match_dup 1)))]
  "")

(define_peephole2
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (match_operand:DI 1 "register_operand" "")
		 (match_operand:DI 2 "register_operand" "")))
   (set (mem:SI (match_dup 0))
        (match_operand:SI 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_64BIT
   && TARGET_NO_SPACE_REGS
   && REG_OK_FOR_INDEX_P (operands[1])
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:SI (plus:DI (match_dup 1) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:DI (match_dup 1) (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (match_operand:DI 1 "register_operand" "")
		 (match_operand:DI 2 "register_operand" "")))
   (set (mem:SI (match_dup 0))
        (match_operand:SI 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_64BIT
   && TARGET_NO_SPACE_REGS
   && REG_OK_FOR_BASE_P (operands[1])
   && REG_OK_FOR_INDEX_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:SI (plus:DI (match_dup 2) (match_dup 1)))
	(match_dup 3))
   (set (match_dup 0) (plus:DI (match_dup 2) (match_dup 1)))]
  "")

(define_insn ""
  [(set (match_operand:SI 0 "move_dest_operand"
			  "=r,r,r,r,r,r,Q,!*q,!r")
	(match_operand:SI 1 "move_src_operand"
			  "A,r,J,N,K,RQ,rM,!rM,!*q"))]
  "(register_operand (operands[0], SImode)
    || reg_or_0_operand (operands[1], SImode))
   && TARGET_SOFT_FLOAT"
  "@
   ldw RT'%A1,%0
   copy %1,%0
   ldi %1,%0
   ldil L'%1,%0
   {zdepi|depwi,z} %Z1,%0
   ldw%M1 %1,%0
   stw%M0 %r1,%0
   mtsar %r1
   {mfctl|mfctl,w} %%sar,%0"
  [(set_attr "type" "load,move,move,move,move,load,store,move,move")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4,4,4,4,4,4,4,4,4")])

;; Load or store with base-register modification.
(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(mem:SI (plus:DI (match_operand:DI 1 "register_operand" "+r")
			 (match_operand:DI 2 "int5_operand" "L"))))
   (set (match_dup 1)
	(plus:DI (match_dup 1) (match_dup 2)))]
  "TARGET_64BIT"
  "ldw,mb %2(%1),%0"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

; And a zero extended variant.
(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extend:DI (mem:SI
			  (plus:DI
			    (match_operand:DI 1 "register_operand" "+r")
			    (match_operand:DI 2 "int5_operand" "L")))))
   (set (match_dup 1)
	(plus:DI (match_dup 1) (match_dup 2)))]
  "TARGET_64BIT"
  "ldw,mb %2(%1),%0"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_expand "pre_load"
  [(parallel [(set (match_operand:SI 0 "register_operand" "")
	      (mem (plus (match_operand 1 "register_operand" "")
			       (match_operand 2 "pre_cint_operand" ""))))
	      (set (match_dup 1)
		   (plus (match_dup 1) (match_dup 2)))])]
  ""
  "
{
  if (TARGET_64BIT)
    {
      emit_insn (gen_pre_ldd (operands[0], operands[1], operands[2]));
      DONE;
    }
  emit_insn (gen_pre_ldw (operands[0], operands[1], operands[2]));
  DONE;
}")

(define_insn "pre_ldw"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(mem:SI (plus:SI (match_operand:SI 1 "register_operand" "+r")
			 (match_operand:SI 2 "pre_cint_operand" ""))))
   (set (match_dup 1)
	(plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "*
{
  if (INTVAL (operands[2]) < 0)
    return \"{ldwm|ldw,mb} %2(%1),%0\";
  return \"{ldws|ldw},mb %2(%1),%0\";
}"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_insn "pre_ldd"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(mem:DI (plus:DI (match_operand:DI 1 "register_operand" "+r")
			 (match_operand:DI 2 "pre_cint_operand" ""))))
   (set (match_dup 1)
	(plus:DI (match_dup 1) (match_dup 2)))]
  "TARGET_64BIT"
  "ldd,mb %2(%1),%0"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_insn ""
  [(set (mem:SI (plus:SI (match_operand:SI 0 "register_operand" "+r")
			 (match_operand:SI 1 "pre_cint_operand" "")))
	(match_operand:SI 2 "reg_or_0_operand" "rM"))
   (set (match_dup 0)
	(plus:SI (match_dup 0) (match_dup 1)))]
  ""
  "*
{
  if (INTVAL (operands[1]) < 0)
    return \"{stwm|stw,mb} %r2,%1(%0)\";
  return \"{stws|stw},mb %r2,%1(%0)\";
}"
  [(set_attr "type" "store")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(mem:SI (match_operand:SI 1 "register_operand" "+r")))
   (set (match_dup 1)
	(plus:SI (match_dup 1)
		 (match_operand:SI 2 "post_cint_operand" "")))]
  ""
  "*
{
  if (INTVAL (operands[2]) > 0)
    return \"{ldwm|ldw,ma} %2(%1),%0\";
  return \"{ldws|ldw},ma %2(%1),%0\";
}"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_expand "post_store"
  [(parallel [(set (mem (match_operand 0 "register_operand" ""))
		   (match_operand 1 "reg_or_0_operand" ""))
	      (set (match_dup 0)
		   (plus (match_dup 0)
			 (match_operand 2 "post_cint_operand" "")))])]
  ""
  "
{
  if (TARGET_64BIT)
    {
      emit_insn (gen_post_std (operands[0], operands[1], operands[2]));
      DONE;
    }
  emit_insn (gen_post_stw (operands[0], operands[1], operands[2]));
  DONE;
}")

(define_insn "post_stw"
  [(set (mem:SI (match_operand:SI 0 "register_operand" "+r"))
	(match_operand:SI 1 "reg_or_0_operand" "rM"))
   (set (match_dup 0)
	(plus:SI (match_dup 0)
		 (match_operand:SI 2 "post_cint_operand" "")))]
  ""
  "*
{
  if (INTVAL (operands[2]) > 0)
    return \"{stwm|stw,ma} %r1,%2(%0)\";
  return \"{stws|stw},ma %r1,%2(%0)\";
}"
  [(set_attr "type" "store")
   (set_attr "length" "4")])

(define_insn "post_std"
  [(set (mem:DI (match_operand:DI 0 "register_operand" "+r"))
	(match_operand:DI 1 "reg_or_0_operand" "rM"))
   (set (match_dup 0)
	(plus:DI (match_dup 0)
		 (match_operand:DI 2 "post_cint_operand" "")))]
  "TARGET_64BIT"
  "std,ma %r1,%2(%0)"
  [(set_attr "type" "store")
   (set_attr "length" "4")])

;; For loading the address of a label while generating PIC code.
;; Note since this pattern can be created at reload time (via movsi), all
;; the same rules for movsi apply here.  (no new pseudos, no temporaries).
(define_insn ""
  [(set (match_operand 0 "pmode_register_operand" "=a")
	(match_operand 1 "pic_label_operand" ""))]
  "TARGET_PA_20"
  "*
{
  rtx xoperands[3];

  xoperands[0] = operands[0];
  xoperands[1] = operands[1];
  xoperands[2] = gen_label_rtx ();

  (*targetm.asm_out.internal_label) (asm_out_file, \"L\",
				     CODE_LABEL_NUMBER (xoperands[2]));
  output_asm_insn (\"mfia %0\", xoperands);

  /* If we're trying to load the address of a label that happens to be
     close, then we can use a shorter sequence.  */
  if (GET_CODE (operands[1]) == LABEL_REF
      && !LABEL_REF_NONLOCAL_P (operands[1])
      && INSN_ADDRESSES_SET_P ()
      && abs (INSN_ADDRESSES (INSN_UID (XEXP (operands[1], 0)))
	        - INSN_ADDRESSES (INSN_UID (insn))) < 8100)
    output_asm_insn (\"ldo %1-%2(%0),%0\", xoperands);
  else
    {
      output_asm_insn (\"addil L%%%1-%2,%0\", xoperands);
      output_asm_insn (\"ldo R%%%1-%2(%0),%0\", xoperands);
    }
  return \"\";
}"
  [(set_attr "type" "multi")
   (set_attr "length" "12")])		; 8 or 12

(define_insn ""
  [(set (match_operand 0 "pmode_register_operand" "=a")
	(match_operand 1 "pic_label_operand" ""))]
  "!TARGET_PA_20"
  "*
{
  rtx xoperands[3];

  xoperands[0] = operands[0];
  xoperands[1] = operands[1];
  xoperands[2] = gen_label_rtx ();

  output_asm_insn (\"bl .+8,%0\", xoperands);
  output_asm_insn (\"depi 0,31,2,%0\", xoperands);
  (*targetm.asm_out.internal_label) (asm_out_file, \"L\",
				     CODE_LABEL_NUMBER (xoperands[2]));

  /* If we're trying to load the address of a label that happens to be
     close, then we can use a shorter sequence.  */
  if (GET_CODE (operands[1]) == LABEL_REF
      && !LABEL_REF_NONLOCAL_P (operands[1])
      && INSN_ADDRESSES_SET_P ()
      && abs (INSN_ADDRESSES (INSN_UID (XEXP (operands[1], 0)))
	        - INSN_ADDRESSES (INSN_UID (insn))) < 8100)
    output_asm_insn (\"ldo %1-%2(%0),%0\", xoperands);
  else
    {
      output_asm_insn (\"addil L%%%1-%2,%0\", xoperands);
      output_asm_insn (\"ldo R%%%1-%2(%0),%0\", xoperands);
    }
  return \"\";
}"
  [(set_attr "type" "multi")
   (set_attr "length" "16")])		; 12 or 16

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=a")
	(plus:SI (match_operand:SI 1 "register_operand" "r")
		 (high:SI (match_operand 2 "" ""))))]
  "symbolic_operand (operands[2], Pmode)
   && ! function_label_operand (operands[2], Pmode)
   && flag_pic"
  "addil LT'%G2,%1"
  [(set_attr "type" "binary")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=a")
	(plus:DI (match_operand:DI 1 "register_operand" "r")
	         (high:DI (match_operand 2 "" ""))))]
  "symbolic_operand (operands[2], Pmode)
   && ! function_label_operand (operands[2], Pmode)
   && TARGET_64BIT
   && flag_pic"
  "addil LT'%G2,%1"
  [(set_attr "type" "binary")
   (set_attr "length" "4")])

;; Always use addil rather than ldil;add sequences.  This allows the
;; HP linker to eliminate the dp relocation if the symbolic operand
;; lives in the TEXT space.
(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=a")
	(high:SI (match_operand 1 "" "")))]
  "symbolic_operand (operands[1], Pmode)
   && ! function_label_operand (operands[1], Pmode)
   && ! read_only_operand (operands[1], Pmode)
   && ! flag_pic"
  "*
{
  if (TARGET_LONG_LOAD_STORE)
    return \"addil NLR'%H1,%%r27\;ldo N'%H1(%%r1),%%r1\";
  else
    return \"addil LR'%H1,%%r27\";
}"
  [(set_attr "type" "binary")
   (set (attr "length")
      (if_then_else (eq (symbol_ref "TARGET_LONG_LOAD_STORE") (const_int 0))
		    (const_int 4)
		    (const_int 8)))])


;; This is for use in the prologue/epilogue code.  We need it
;; to add large constants to a stack pointer or frame pointer.
;; Because of the additional %r1 pressure, we probably do not
;; want to use this in general code, so make it available
;; only after reload.
(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=!a,*r")
	(plus:SI (match_operand:SI 1 "register_operand" "r,r")
		 (high:SI (match_operand 2 "const_int_operand" ""))))]
  "reload_completed"
  "@
   addil L'%G2,%1
   ldil L'%G2,%0\;{addl|add,l} %0,%1,%0"
  [(set_attr "type" "binary,binary")
   (set_attr "length" "4,8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=!a,*r")
	(plus:DI (match_operand:DI 1 "register_operand" "r,r")
		 (high:DI (match_operand 2 "const_int_operand" ""))))]
  "reload_completed && TARGET_64BIT"
  "@
   addil L'%G2,%1
   ldil L'%G2,%0\;{addl|add,l} %0,%1,%0"
  [(set_attr "type" "binary,binary")
   (set_attr "length" "4,8")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(high:SI (match_operand 1 "" "")))]
  "(!flag_pic || !symbolic_operand (operands[1], Pmode))
    && !is_function_label_plus_const (operands[1])"
  "*
{
  if (symbolic_operand (operands[1], Pmode))
    return \"ldil LR'%H1,%0\";
  else
    return \"ldil L'%G1,%0\";
}"
  [(set_attr "type" "move")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(high:DI (match_operand 1 "const_int_operand" "")))]
  "TARGET_64BIT"
  "ldil L'%G1,%0";
  [(set_attr "type" "move")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(lo_sum:DI (match_operand:DI 1 "register_operand" "r")
		   (match_operand:DI 2 "const_int_operand" "i")))]
  "TARGET_64BIT"
  "ldo R'%G2(%1),%0";
  [(set_attr "type" "move")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(lo_sum:SI (match_operand:SI 1 "register_operand" "r")
		   (match_operand:SI 2 "immediate_operand" "i")))]
  "!is_function_label_plus_const (operands[2])"
  "*
{
  gcc_assert (!flag_pic || !symbolic_operand (operands[2], Pmode));
  
  if (symbolic_operand (operands[2], Pmode))
    return \"ldo RR'%G2(%1),%0\";
  else
    return \"ldo R'%G2(%1),%0\";
}"
  [(set_attr "type" "move")
   (set_attr "length" "4")])

;; Now that a symbolic_address plus a constant is broken up early
;; in the compilation phase (for better CSE) we need a special
;; combiner pattern to load the symbolic address plus the constant
;; in only 2 instructions. (For cases where the symbolic address
;; was not a common subexpression.)
(define_split
  [(set (match_operand:SI 0 "register_operand" "")
	(match_operand:SI 1 "symbolic_operand" ""))
   (clobber (match_operand:SI 2 "register_operand" ""))]
  "! (flag_pic && pic_label_operand (operands[1], SImode))"
  [(set (match_dup 2) (high:SI (match_dup 1)))
   (set (match_dup 0) (lo_sum:SI (match_dup 2) (match_dup 1)))]
  "")

;; hppa_legitimize_address goes to a great deal of trouble to
;; create addresses which use indexing.  In some cases, this
;; is a lose because there isn't any store instructions which
;; allow indexed addresses (with integer register source).
;;
;; These define_splits try to turn a 3 insn store into
;; a 2 insn store with some creative RTL rewriting.
(define_split
  [(set (mem:SI (plus:SI (mult:SI (match_operand:SI 0 "register_operand" "")
			       (match_operand:SI 1 "shadd_operand" ""))
		   (plus:SI (match_operand:SI 2 "register_operand" "")
			    (match_operand:SI 3 "const_int_operand" ""))))
	(match_operand:SI 4 "register_operand" ""))
   (clobber (match_operand:SI 5 "register_operand" ""))]
  ""
  [(set (match_dup 5) (plus:SI (mult:SI (match_dup 0) (match_dup 1))
			       (match_dup 2)))
   (set (mem:SI (plus:SI (match_dup 5) (match_dup 3))) (match_dup 4))]
  "")

(define_split
  [(set (mem:HI (plus:SI (mult:SI (match_operand:SI 0 "register_operand" "")
			       (match_operand:SI 1 "shadd_operand" ""))
		   (plus:SI (match_operand:SI 2 "register_operand" "")
			    (match_operand:SI 3 "const_int_operand" ""))))
	(match_operand:HI 4 "register_operand" ""))
   (clobber (match_operand:SI 5 "register_operand" ""))]
  ""
  [(set (match_dup 5) (plus:SI (mult:SI (match_dup 0) (match_dup 1))
			       (match_dup 2)))
   (set (mem:HI (plus:SI (match_dup 5) (match_dup 3))) (match_dup 4))]
  "")

(define_split
  [(set (mem:QI (plus:SI (mult:SI (match_operand:SI 0 "register_operand" "")
			       (match_operand:SI 1 "shadd_operand" ""))
		   (plus:SI (match_operand:SI 2 "register_operand" "")
			    (match_operand:SI 3 "const_int_operand" ""))))
	(match_operand:QI 4 "register_operand" ""))
   (clobber (match_operand:SI 5 "register_operand" ""))]
  ""
  [(set (match_dup 5) (plus:SI (mult:SI (match_dup 0) (match_dup 1))
			       (match_dup 2)))
   (set (mem:QI (plus:SI (match_dup 5) (match_dup 3))) (match_dup 4))]
  "")

(define_expand "movhi"
  [(set (match_operand:HI 0 "general_operand" "")
	(match_operand:HI 1 "general_operand" ""))]
  ""
  "
{
  if (emit_move_sequence (operands, HImode, 0))
    DONE;
}")

(define_insn ""
  [(set (match_operand:HI 0 "move_dest_operand"
	 		  "=r,r,r,r,r,Q,!*q,!r,!*f,?r,?*f")
	(match_operand:HI 1 "move_src_operand"
			  "r,J,N,K,RQ,rM,!rM,!*q,!*fM,*f,r"))]
  "(register_operand (operands[0], HImode)
    || reg_or_0_operand (operands[1], HImode))
   && !TARGET_SOFT_FLOAT
   && !TARGET_64BIT"
  "@
   copy %1,%0
   ldi %1,%0
   ldil L'%1,%0
   {zdepi|depwi,z} %Z1,%0
   ldh%M1 %1,%0
   sth%M0 %r1,%0
   mtsar %r1
   {mfctl|mfctl,w} %sar,%0
   fcpy,sgl %f1,%0
   {fstws|fstw} %1,-16(%%sp)\n\t{ldws|ldw} -16(%%sp),%0
   {stws|stw} %1,-16(%%sp)\n\t{fldws|fldw} -16(%%sp),%0"
  [(set_attr "type" "move,move,move,shift,load,store,move,move,move,move,move")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4,4,4,4,4,4,4,4,4,8,8")])

(define_insn ""
  [(set (match_operand:HI 0 "move_dest_operand"
	 		  "=r,r,r,r,r,Q,!*q,!r,!*f")
	(match_operand:HI 1 "move_src_operand"
			  "r,J,N,K,RQ,rM,!rM,!*q,!*fM"))]
  "(register_operand (operands[0], HImode)
    || reg_or_0_operand (operands[1], HImode))
   && !TARGET_SOFT_FLOAT
   && TARGET_64BIT"
  "@
   copy %1,%0
   ldi %1,%0
   ldil L'%1,%0
   {zdepi|depwi,z} %Z1,%0
   ldh%M1 %1,%0
   sth%M0 %r1,%0
   mtsar %r1
   {mfctl|mfctl,w} %sar,%0
   fcpy,sgl %f1,%0"
  [(set_attr "type" "move,move,move,shift,load,store,move,move,move")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4,4,4,4,4,4,4,4,4")])

(define_insn ""
  [(set (match_operand:HI 0 "move_dest_operand"
	 		  "=r,r,r,r,r,Q,!*q,!r")
	(match_operand:HI 1 "move_src_operand"
			  "r,J,N,K,RQ,rM,!rM,!*q"))]
  "(register_operand (operands[0], HImode)
    || reg_or_0_operand (operands[1], HImode))
   && TARGET_SOFT_FLOAT"
  "@
   copy %1,%0
   ldi %1,%0
   ldil L'%1,%0
   {zdepi|depwi,z} %Z1,%0
   ldh%M1 %1,%0
   sth%M0 %r1,%0
   mtsar %r1
   {mfctl|mfctl,w} %sar,%0"
  [(set_attr "type" "move,move,move,shift,load,store,move,move")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4,4,4,4,4,4,4,4")])

(define_insn ""
  [(set (match_operand:HI 0 "register_operand" "=r")
	(mem:HI (plus:SI (match_operand:SI 1 "register_operand" "+r")
			 (match_operand:SI 2 "int5_operand" "L"))))
   (set (match_dup 1)
	(plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "{ldhs|ldh},mb %2(%1),%0"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:HI 0 "register_operand" "=r")
	(mem:HI (plus:DI (match_operand:DI 1 "register_operand" "+r")
			 (match_operand:DI 2 "int5_operand" "L"))))
   (set (match_dup 1)
	(plus:DI (match_dup 1) (match_dup 2)))]
  "TARGET_64BIT"
  "ldh,mb %2(%1),%0"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

; And a zero extended variant.
(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extend:DI (mem:HI
			  (plus:DI
			    (match_operand:DI 1 "register_operand" "+r")
			    (match_operand:DI 2 "int5_operand" "L")))))
   (set (match_dup 1)
	(plus:DI (match_dup 1) (match_dup 2)))]
  "TARGET_64BIT"
  "ldh,mb %2(%1),%0"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(zero_extend:SI (mem:HI
			  (plus:SI
			    (match_operand:SI 1 "register_operand" "+r")
			    (match_operand:SI 2 "int5_operand" "L")))))
   (set (match_dup 1)
	(plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "{ldhs|ldh},mb %2(%1),%0"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(zero_extend:SI (mem:HI
			  (plus:DI
			    (match_operand:DI 1 "register_operand" "+r")
			    (match_operand:DI 2 "int5_operand" "L")))))
   (set (match_dup 1)
	(plus:DI (match_dup 1) (match_dup 2)))]
  "TARGET_64BIT"
  "ldh,mb %2(%1),%0"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_insn ""
  [(set (mem:HI (plus:SI (match_operand:SI 0 "register_operand" "+r")
			 (match_operand:SI 1 "int5_operand" "L")))
	(match_operand:HI 2 "reg_or_0_operand" "rM"))
   (set (match_dup 0)
	(plus:SI (match_dup 0) (match_dup 1)))]
  ""
  "{sths|sth},mb %r2,%1(%0)"
  [(set_attr "type" "store")
   (set_attr "length" "4")])

(define_insn ""
  [(set (mem:HI (plus:DI (match_operand:DI 0 "register_operand" "+r")
			 (match_operand:DI 1 "int5_operand" "L")))
	(match_operand:HI 2 "reg_or_0_operand" "rM"))
   (set (match_dup 0)
	(plus:DI (match_dup 0) (match_dup 1)))]
  "TARGET_64BIT"
  "sth,mb %r2,%1(%0)"
  [(set_attr "type" "store")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:HI 0 "register_operand" "=r")
	(plus:HI (match_operand:HI 1 "register_operand" "r")
		 (match_operand 2 "const_int_operand" "J")))]
  ""
  "ldo %2(%1),%0"
  [(set_attr "type" "binary")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4")])

(define_expand "movqi"
  [(set (match_operand:QI 0 "general_operand" "")
	(match_operand:QI 1 "general_operand" ""))]
  ""
  "
{
  if (emit_move_sequence (operands, QImode, 0))
    DONE;
}")

(define_insn ""
  [(set (match_operand:QI 0 "move_dest_operand"
			  "=r,r,r,r,r,Q,!*q,!r,!*f,?r,?*f")
	(match_operand:QI 1 "move_src_operand"
			  "r,J,N,K,RQ,rM,!rM,!*q,!*fM,*f,r"))]
  "(register_operand (operands[0], QImode)
    || reg_or_0_operand (operands[1], QImode))
   && !TARGET_SOFT_FLOAT
   && !TARGET_64BIT"
  "@
   copy %1,%0
   ldi %1,%0
   ldil L'%1,%0
   {zdepi|depwi,z} %Z1,%0
   ldb%M1 %1,%0
   stb%M0 %r1,%0
   mtsar %r1
   {mfctl|mfctl,w} %%sar,%0
   fcpy,sgl %f1,%0
   {fstws|fstw} %1,-16(%%sp)\n\t{ldws|ldw} -16(%%sp),%0
   {stws|stw} %1,-16(%%sp)\n\t{fldws|fldw} -16(%%sp),%0"
  [(set_attr "type" "move,move,move,shift,load,store,move,move,move,move,move")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4,4,4,4,4,4,4,4,4,8,8")])

(define_insn ""
  [(set (match_operand:QI 0 "move_dest_operand"
			  "=r,r,r,r,r,Q,!*q,!r,!*f")
	(match_operand:QI 1 "move_src_operand"
			  "r,J,N,K,RQ,rM,!rM,!*q,!*fM"))]
  "(register_operand (operands[0], QImode)
    || reg_or_0_operand (operands[1], QImode))
   && !TARGET_SOFT_FLOAT
   && TARGET_64BIT"
  "@
   copy %1,%0
   ldi %1,%0
   ldil L'%1,%0
   {zdepi|depwi,z} %Z1,%0
   ldb%M1 %1,%0
   stb%M0 %r1,%0
   mtsar %r1
   {mfctl|mfctl,w} %%sar,%0
   fcpy,sgl %f1,%0"
  [(set_attr "type" "move,move,move,shift,load,store,move,move,move")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4,4,4,4,4,4,4,4,4")])

(define_insn ""
  [(set (match_operand:QI 0 "move_dest_operand"
			  "=r,r,r,r,r,Q,!*q,!r")
	(match_operand:QI 1 "move_src_operand"
			  "r,J,N,K,RQ,rM,!rM,!*q"))]
  "(register_operand (operands[0], QImode)
    || reg_or_0_operand (operands[1], QImode))
   && TARGET_SOFT_FLOAT"
  "@
   copy %1,%0
   ldi %1,%0
   ldil L'%1,%0
   {zdepi|depwi,z} %Z1,%0
   ldb%M1 %1,%0
   stb%M0 %r1,%0
   mtsar %r1
   {mfctl|mfctl,w} %%sar,%0"
  [(set_attr "type" "move,move,move,shift,load,store,move,move")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4,4,4,4,4,4,4,4")])

(define_insn ""
  [(set (match_operand:QI 0 "register_operand" "=r")
	(mem:QI (plus:SI (match_operand:SI 1 "register_operand" "+r")
			 (match_operand:SI 2 "int5_operand" "L"))))
   (set (match_dup 1) (plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "{ldbs|ldb},mb %2(%1),%0"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:QI 0 "register_operand" "=r")
	(mem:QI (plus:DI (match_operand:DI 1 "register_operand" "+r")
			 (match_operand:DI 2 "int5_operand" "L"))))
   (set (match_dup 1) (plus:DI (match_dup 1) (match_dup 2)))]
  "TARGET_64BIT"
  "ldb,mb %2(%1),%0"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

; Now the same thing with zero extensions.
(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extend:DI (mem:QI (plus:DI
				  (match_operand:DI 1 "register_operand" "+r")
				  (match_operand:DI 2 "int5_operand" "L")))))
   (set (match_dup 1) (plus:DI (match_dup 1) (match_dup 2)))]
  "TARGET_64BIT"
  "ldb,mb %2(%1),%0"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(zero_extend:SI (mem:QI (plus:SI
				  (match_operand:SI 1 "register_operand" "+r")
				  (match_operand:SI 2 "int5_operand" "L")))))
   (set (match_dup 1) (plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "{ldbs|ldb},mb %2(%1),%0"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(zero_extend:SI (mem:QI (plus:DI
				  (match_operand:DI 1 "register_operand" "+r")
				  (match_operand:DI 2 "int5_operand" "L")))))
   (set (match_dup 1) (plus:DI (match_dup 1) (match_dup 2)))]
  "TARGET_64BIT"
  "ldb,mb %2(%1),%0"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:HI 0 "register_operand" "=r")
	(zero_extend:HI (mem:QI (plus:SI
				  (match_operand:SI 1 "register_operand" "+r")
				  (match_operand:SI 2 "int5_operand" "L")))))
   (set (match_dup 1) (plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "{ldbs|ldb},mb %2(%1),%0"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:HI 0 "register_operand" "=r")
	(zero_extend:HI (mem:QI (plus:DI
				  (match_operand:DI 1 "register_operand" "+r")
				  (match_operand:DI 2 "int5_operand" "L")))))
   (set (match_dup 1) (plus:DI (match_dup 1) (match_dup 2)))]
  "TARGET_64BIT"
  "ldb,mb %2(%1),%0"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_insn ""
  [(set (mem:QI (plus:SI (match_operand:SI 0 "register_operand" "+r")
			 (match_operand:SI 1 "int5_operand" "L")))
	(match_operand:QI 2 "reg_or_0_operand" "rM"))
   (set (match_dup 0)
	(plus:SI (match_dup 0) (match_dup 1)))]
  ""
  "{stbs|stb},mb %r2,%1(%0)"
  [(set_attr "type" "store")
   (set_attr "length" "4")])

(define_insn ""
  [(set (mem:QI (plus:DI (match_operand:DI 0 "register_operand" "+r")
			 (match_operand:DI 1 "int5_operand" "L")))
	(match_operand:QI 2 "reg_or_0_operand" "rM"))
   (set (match_dup 0)
	(plus:DI (match_dup 0) (match_dup 1)))]
  "TARGET_64BIT"
  "stb,mb %r2,%1(%0)"
  [(set_attr "type" "store")
   (set_attr "length" "4")])

;; The definition of this insn does not really explain what it does,
;; but it should suffice that anything generated as this insn will be
;; recognized as a movmemsi operation, and that it will not successfully
;; combine with anything.
(define_expand "movmemsi"
  [(parallel [(set (match_operand:BLK 0 "" "")
		   (match_operand:BLK 1 "" ""))
	      (clobber (match_dup 4))
	      (clobber (match_dup 5))
	      (clobber (match_dup 6))
	      (clobber (match_dup 7))
	      (clobber (match_dup 8))
	      (use (match_operand:SI 2 "arith_operand" ""))
	      (use (match_operand:SI 3 "const_int_operand" ""))])]
  "!TARGET_64BIT && optimize > 0"
  "
{
  int size, align;

  /* HP provides very fast block move library routine for the PA;
     this routine includes:

	4x4 byte at a time block moves,
	1x4 byte at a time with alignment checked at runtime with
	    attempts to align the source and destination as needed
	1x1 byte loop

     With that in mind, here's the heuristics to try and guess when
     the inlined block move will be better than the library block
     move:

	If the size isn't constant, then always use the library routines.

	If the size is large in respect to the known alignment, then use
	the library routines.

	If the size is small in respect to the known alignment, then open
	code the copy (since that will lead to better scheduling).

        Else use the block move pattern.   */

  /* Undetermined size, use the library routine.  */
  if (GET_CODE (operands[2]) != CONST_INT)
    FAIL;

  size = INTVAL (operands[2]);
  align = INTVAL (operands[3]);
  align = align > 4 ? 4 : align;

  /* If size/alignment is large, then use the library routines.  */
  if (size / align > 16)
    FAIL;

  /* This does happen, but not often enough to worry much about.  */
  if (size / align < MOVE_RATIO)
    FAIL;
  
  /* Fall through means we're going to use our block move pattern.  */
  operands[0]
    = replace_equiv_address (operands[0],
			     copy_to_mode_reg (SImode, XEXP (operands[0], 0)));
  operands[1]
    = replace_equiv_address (operands[1],
			     copy_to_mode_reg (SImode, XEXP (operands[1], 0)));
  operands[4] = gen_reg_rtx (SImode);
  operands[5] = gen_reg_rtx (SImode);
  operands[6] = gen_reg_rtx (SImode);
  operands[7] = gen_reg_rtx (SImode);
  operands[8] = gen_reg_rtx (SImode);
}")

;; The operand constraints are written like this to support both compile-time
;; and run-time determined byte counts.  The expander and output_block_move
;; only support compile-time determined counts at this time.
;;
;; If the count is run-time determined, the register with the byte count
;; is clobbered by the copying code, and therefore it is forced to operand 2.
;;
;; We used to clobber operands 0 and 1.  However, a change to regrename.c
;; broke this semantic for pseudo registers.  We can't use match_scratch
;; as this requires two registers in the class R1_REGS when the MEMs for
;; operands 0 and 1 are both equivalent to symbolic MEMs.  Thus, we are
;; forced to internally copy operands 0 and 1 to operands 7 and 8,
;; respectively.  We then split or peephole optimize after reload.
(define_insn "movmemsi_prereload"
  [(set (mem:BLK (match_operand:SI 0 "register_operand" "r,r"))
	(mem:BLK (match_operand:SI 1 "register_operand" "r,r")))
   (clobber (match_operand:SI 2 "register_operand" "=&r,&r"))	;loop cnt/tmp
   (clobber (match_operand:SI 3 "register_operand" "=&r,&r"))	;item tmp1
   (clobber (match_operand:SI 6 "register_operand" "=&r,&r"))	;item tmp2
   (clobber (match_operand:SI 7 "register_operand" "=&r,&r"))	;item tmp3
   (clobber (match_operand:SI 8 "register_operand" "=&r,&r"))	;item tmp4
   (use (match_operand:SI 4 "arith_operand" "J,2"))	 ;byte count
   (use (match_operand:SI 5 "const_int_operand" "n,n"))] ;alignment
  "!TARGET_64BIT"
  "#"
  [(set_attr "type" "multi,multi")])

(define_split
  [(parallel [(set (match_operand:BLK 0 "memory_operand" "")
		   (match_operand:BLK 1 "memory_operand" ""))
	      (clobber (match_operand:SI 2 "register_operand" ""))
	      (clobber (match_operand:SI 3 "register_operand" ""))
	      (clobber (match_operand:SI 6 "register_operand" ""))
	      (clobber (match_operand:SI 7 "register_operand" ""))
	      (clobber (match_operand:SI 8 "register_operand" ""))
	      (use (match_operand:SI 4 "arith_operand" ""))
	      (use (match_operand:SI 5 "const_int_operand" ""))])]
  "!TARGET_64BIT && reload_completed && !flag_peephole2
   && GET_CODE (operands[0]) == MEM
   && register_operand (XEXP (operands[0], 0), SImode)
   && GET_CODE (operands[1]) == MEM
   && register_operand (XEXP (operands[1], 0), SImode)"
  [(set (match_dup 7) (match_dup 9))
   (set (match_dup 8) (match_dup 10))
   (parallel [(set (match_dup 0) (match_dup 1))
   	      (clobber (match_dup 2))
   	      (clobber (match_dup 3))
   	      (clobber (match_dup 6))
   	      (clobber (match_dup 7))
   	      (clobber (match_dup 8))
   	      (use (match_dup 4))
   	      (use (match_dup 5))
	      (const_int 0)])]
  "
{
  operands[9] = XEXP (operands[0], 0);
  operands[10] = XEXP (operands[1], 0);
  operands[0] = replace_equiv_address (operands[0], operands[7]);
  operands[1] = replace_equiv_address (operands[1], operands[8]);
}")

(define_peephole2
  [(parallel [(set (match_operand:BLK 0 "memory_operand" "")
		   (match_operand:BLK 1 "memory_operand" ""))
	      (clobber (match_operand:SI 2 "register_operand" ""))
	      (clobber (match_operand:SI 3 "register_operand" ""))
	      (clobber (match_operand:SI 6 "register_operand" ""))
	      (clobber (match_operand:SI 7 "register_operand" ""))
	      (clobber (match_operand:SI 8 "register_operand" ""))
	      (use (match_operand:SI 4 "arith_operand" ""))
	      (use (match_operand:SI 5 "const_int_operand" ""))])]
  "!TARGET_64BIT
   && GET_CODE (operands[0]) == MEM
   && register_operand (XEXP (operands[0], 0), SImode)
   && GET_CODE (operands[1]) == MEM
   && register_operand (XEXP (operands[1], 0), SImode)"
  [(parallel [(set (match_dup 0) (match_dup 1))
   	      (clobber (match_dup 2))
   	      (clobber (match_dup 3))
   	      (clobber (match_dup 6))
   	      (clobber (match_dup 7))
   	      (clobber (match_dup 8))
   	      (use (match_dup 4))
   	      (use (match_dup 5))
	      (const_int 0)])]
  "
{
  rtx addr = XEXP (operands[0], 0);
  if (dead_or_set_p (curr_insn, addr))
    operands[7] = addr;
  else
    {
      emit_insn (gen_rtx_SET (VOIDmode, operands[7], addr));
      operands[0] = replace_equiv_address (operands[0], operands[7]);
    }

  addr = XEXP (operands[1], 0);
  if (dead_or_set_p (curr_insn, addr))
    operands[8] = addr;
  else
    {
      emit_insn (gen_rtx_SET (VOIDmode, operands[8], addr));
      operands[1] = replace_equiv_address (operands[1], operands[8]);
    }
}")

(define_insn "movmemsi_postreload"
  [(set (mem:BLK (match_operand:SI 0 "register_operand" "+r,r"))
	(mem:BLK (match_operand:SI 1 "register_operand" "+r,r")))
   (clobber (match_operand:SI 2 "register_operand" "=&r,&r"))	;loop cnt/tmp
   (clobber (match_operand:SI 3 "register_operand" "=&r,&r"))	;item tmp1
   (clobber (match_operand:SI 6 "register_operand" "=&r,&r"))	;item tmp2
   (clobber (match_dup 0))
   (clobber (match_dup 1))
   (use (match_operand:SI 4 "arith_operand" "J,2"))	 ;byte count
   (use (match_operand:SI 5 "const_int_operand" "n,n"))  ;alignment
   (const_int 0)]
  "!TARGET_64BIT && reload_completed"
  "* return output_block_move (operands, !which_alternative);"
  [(set_attr "type" "multi,multi")])

(define_expand "movmemdi"
  [(parallel [(set (match_operand:BLK 0 "" "")
		   (match_operand:BLK 1 "" ""))
	      (clobber (match_dup 4))
	      (clobber (match_dup 5))
	      (clobber (match_dup 6))
	      (clobber (match_dup 7))
	      (clobber (match_dup 8))
	      (use (match_operand:DI 2 "arith_operand" ""))
	      (use (match_operand:DI 3 "const_int_operand" ""))])]
  "TARGET_64BIT && optimize > 0"
  "
{
  int size, align;

  /* HP provides very fast block move library routine for the PA;
     this routine includes:

	4x4 byte at a time block moves,
	1x4 byte at a time with alignment checked at runtime with
	    attempts to align the source and destination as needed
	1x1 byte loop

     With that in mind, here's the heuristics to try and guess when
     the inlined block move will be better than the library block
     move:

	If the size isn't constant, then always use the library routines.

	If the size is large in respect to the known alignment, then use
	the library routines.

	If the size is small in respect to the known alignment, then open
	code the copy (since that will lead to better scheduling).

        Else use the block move pattern.   */

  /* Undetermined size, use the library routine.  */
  if (GET_CODE (operands[2]) != CONST_INT)
    FAIL;

  size = INTVAL (operands[2]);
  align = INTVAL (operands[3]);
  align = align > 8 ? 8 : align;

  /* If size/alignment is large, then use the library routines.  */
  if (size / align > 16)
    FAIL;

  /* This does happen, but not often enough to worry much about.  */
  if (size / align < MOVE_RATIO)
    FAIL;
  
  /* Fall through means we're going to use our block move pattern.  */
  operands[0]
    = replace_equiv_address (operands[0],
			     copy_to_mode_reg (DImode, XEXP (operands[0], 0)));
  operands[1]
    = replace_equiv_address (operands[1],
			     copy_to_mode_reg (DImode, XEXP (operands[1], 0)));
  operands[4] = gen_reg_rtx (DImode);
  operands[5] = gen_reg_rtx (DImode);
  operands[6] = gen_reg_rtx (DImode);
  operands[7] = gen_reg_rtx (DImode);
  operands[8] = gen_reg_rtx (DImode);
}")

;; The operand constraints are written like this to support both compile-time
;; and run-time determined byte counts.  The expander and output_block_move
;; only support compile-time determined counts at this time.
;;
;; If the count is run-time determined, the register with the byte count
;; is clobbered by the copying code, and therefore it is forced to operand 2.
;;
;; We used to clobber operands 0 and 1.  However, a change to regrename.c
;; broke this semantic for pseudo registers.  We can't use match_scratch
;; as this requires two registers in the class R1_REGS when the MEMs for
;; operands 0 and 1 are both equivalent to symbolic MEMs.  Thus, we are
;; forced to internally copy operands 0 and 1 to operands 7 and 8,
;; respectively.  We then split or peephole optimize after reload.
(define_insn "movmemdi_prereload"
  [(set (mem:BLK (match_operand:DI 0 "register_operand" "r,r"))
	(mem:BLK (match_operand:DI 1 "register_operand" "r,r")))
   (clobber (match_operand:DI 2 "register_operand" "=&r,&r"))	;loop cnt/tmp
   (clobber (match_operand:DI 3 "register_operand" "=&r,&r"))	;item tmp1
   (clobber (match_operand:DI 6 "register_operand" "=&r,&r"))	;item tmp2
   (clobber (match_operand:DI 7 "register_operand" "=&r,&r"))	;item tmp3
   (clobber (match_operand:DI 8 "register_operand" "=&r,&r"))	;item tmp4
   (use (match_operand:DI 4 "arith_operand" "J,2"))	 ;byte count
   (use (match_operand:DI 5 "const_int_operand" "n,n"))] ;alignment
  "TARGET_64BIT"
  "#"
  [(set_attr "type" "multi,multi")])

(define_split
  [(parallel [(set (match_operand:BLK 0 "memory_operand" "")
		   (match_operand:BLK 1 "memory_operand" ""))
	      (clobber (match_operand:DI 2 "register_operand" ""))
	      (clobber (match_operand:DI 3 "register_operand" ""))
	      (clobber (match_operand:DI 6 "register_operand" ""))
	      (clobber (match_operand:DI 7 "register_operand" ""))
	      (clobber (match_operand:DI 8 "register_operand" ""))
	      (use (match_operand:DI 4 "arith_operand" ""))
	      (use (match_operand:DI 5 "const_int_operand" ""))])]
  "TARGET_64BIT && reload_completed && !flag_peephole2
   && GET_CODE (operands[0]) == MEM
   && register_operand (XEXP (operands[0], 0), DImode)
   && GET_CODE (operands[1]) == MEM
   && register_operand (XEXP (operands[1], 0), DImode)"
  [(set (match_dup 7) (match_dup 9))
   (set (match_dup 8) (match_dup 10))
   (parallel [(set (match_dup 0) (match_dup 1))
   	      (clobber (match_dup 2))
   	      (clobber (match_dup 3))
   	      (clobber (match_dup 6))
   	      (clobber (match_dup 7))
   	      (clobber (match_dup 8))
   	      (use (match_dup 4))
   	      (use (match_dup 5))
	      (const_int 0)])]
  "
{
  operands[9] = XEXP (operands[0], 0);
  operands[10] = XEXP (operands[1], 0);
  operands[0] = replace_equiv_address (operands[0], operands[7]);
  operands[1] = replace_equiv_address (operands[1], operands[8]);
}")

(define_peephole2
  [(parallel [(set (match_operand:BLK 0 "memory_operand" "")
		   (match_operand:BLK 1 "memory_operand" ""))
	      (clobber (match_operand:DI 2 "register_operand" ""))
	      (clobber (match_operand:DI 3 "register_operand" ""))
	      (clobber (match_operand:DI 6 "register_operand" ""))
	      (clobber (match_operand:DI 7 "register_operand" ""))
	      (clobber (match_operand:DI 8 "register_operand" ""))
	      (use (match_operand:DI 4 "arith_operand" ""))
	      (use (match_operand:DI 5 "const_int_operand" ""))])]
  "TARGET_64BIT
   && GET_CODE (operands[0]) == MEM
   && register_operand (XEXP (operands[0], 0), DImode)
   && GET_CODE (operands[1]) == MEM
   && register_operand (XEXP (operands[1], 0), DImode)"
  [(parallel [(set (match_dup 0) (match_dup 1))
   	      (clobber (match_dup 2))
   	      (clobber (match_dup 3))
   	      (clobber (match_dup 6))
   	      (clobber (match_dup 7))
   	      (clobber (match_dup 8))
   	      (use (match_dup 4))
   	      (use (match_dup 5))
	      (const_int 0)])]
  "
{
  rtx addr = XEXP (operands[0], 0);
  if (dead_or_set_p (curr_insn, addr))
    operands[7] = addr;
  else
    {
      emit_insn (gen_rtx_SET (VOIDmode, operands[7], addr));
      operands[0] = replace_equiv_address (operands[0], operands[7]);
    }

  addr = XEXP (operands[1], 0);
  if (dead_or_set_p (curr_insn, addr))
    operands[8] = addr;
  else
    {
      emit_insn (gen_rtx_SET (VOIDmode, operands[8], addr));
      operands[1] = replace_equiv_address (operands[1], operands[8]);
    }
}")

(define_insn "movmemdi_postreload"
  [(set (mem:BLK (match_operand:DI 0 "register_operand" "+r,r"))
	(mem:BLK (match_operand:DI 1 "register_operand" "+r,r")))
   (clobber (match_operand:DI 2 "register_operand" "=&r,&r"))	;loop cnt/tmp
   (clobber (match_operand:DI 3 "register_operand" "=&r,&r"))	;item tmp1
   (clobber (match_operand:DI 6 "register_operand" "=&r,&r"))	;item tmp2
   (clobber (match_dup 0))
   (clobber (match_dup 1))
   (use (match_operand:DI 4 "arith_operand" "J,2"))	 ;byte count
   (use (match_operand:DI 5 "const_int_operand" "n,n"))  ;alignment
   (const_int 0)]
  "TARGET_64BIT && reload_completed"
  "* return output_block_move (operands, !which_alternative);"
  [(set_attr "type" "multi,multi")])

(define_expand "setmemsi"
  [(parallel [(set (match_operand:BLK 0 "" "")
		   (match_operand 2 "const_int_operand" ""))
	      (clobber (match_dup 4))
	      (clobber (match_dup 5))
	      (use (match_operand:SI 1 "arith_operand" ""))
	      (use (match_operand:SI 3 "const_int_operand" ""))])]
  "!TARGET_64BIT && optimize > 0"
  "
{
  int size, align;

  /* If value to set is not zero, use the library routine.  */
  if (operands[2] != const0_rtx)
    FAIL;

  /* Undetermined size, use the library routine.  */
  if (GET_CODE (operands[1]) != CONST_INT)
    FAIL;

  size = INTVAL (operands[1]);
  align = INTVAL (operands[3]);
  align = align > 4 ? 4 : align;

  /* If size/alignment is large, then use the library routines.  */
  if (size / align > 16)
    FAIL;

  /* This does happen, but not often enough to worry much about.  */
  if (size / align < MOVE_RATIO)
    FAIL;
  
  /* Fall through means we're going to use our block clear pattern.  */
  operands[0]
    = replace_equiv_address (operands[0],
			     copy_to_mode_reg (SImode, XEXP (operands[0], 0)));
  operands[4] = gen_reg_rtx (SImode);
  operands[5] = gen_reg_rtx (SImode);
}")

(define_insn "clrmemsi_prereload"
  [(set (mem:BLK (match_operand:SI 0 "register_operand" "r,r"))
	(const_int 0))
   (clobber (match_operand:SI 1 "register_operand" "=&r,&r"))	;loop cnt/tmp
   (clobber (match_operand:SI 4 "register_operand" "=&r,&r"))	;tmp1
   (use (match_operand:SI 2 "arith_operand" "J,1"))	 ;byte count
   (use (match_operand:SI 3 "const_int_operand" "n,n"))] ;alignment
  "!TARGET_64BIT"
  "#"
  [(set_attr "type" "multi,multi")])

(define_split
  [(parallel [(set (match_operand:BLK 0 "memory_operand" "")
		   (const_int 0))
	      (clobber (match_operand:SI 1 "register_operand" ""))
	      (clobber (match_operand:SI 4 "register_operand" ""))
	      (use (match_operand:SI 2 "arith_operand" ""))
	      (use (match_operand:SI 3 "const_int_operand" ""))])]
  "!TARGET_64BIT && reload_completed && !flag_peephole2
   && GET_CODE (operands[0]) == MEM
   && register_operand (XEXP (operands[0], 0), SImode)"
  [(set (match_dup 4) (match_dup 5))
   (parallel [(set (match_dup 0) (const_int 0))
   	      (clobber (match_dup 1))
   	      (clobber (match_dup 4))
   	      (use (match_dup 2))
   	      (use (match_dup 3))
	      (const_int 0)])]
  "
{
  operands[5] = XEXP (operands[0], 0);
  operands[0] = replace_equiv_address (operands[0], operands[4]);
}")

(define_peephole2
  [(parallel [(set (match_operand:BLK 0 "memory_operand" "")
		   (const_int 0))
	      (clobber (match_operand:SI 1 "register_operand" ""))
	      (clobber (match_operand:SI 4 "register_operand" ""))
	      (use (match_operand:SI 2 "arith_operand" ""))
	      (use (match_operand:SI 3 "const_int_operand" ""))])]
  "!TARGET_64BIT
   && GET_CODE (operands[0]) == MEM
   && register_operand (XEXP (operands[0], 0), SImode)"
  [(parallel [(set (match_dup 0) (const_int 0))
   	      (clobber (match_dup 1))
   	      (clobber (match_dup 4))
   	      (use (match_dup 2))
   	      (use (match_dup 3))
	      (const_int 0)])]
  "
{
  rtx addr = XEXP (operands[0], 0);
  if (dead_or_set_p (curr_insn, addr))
    operands[4] = addr;
  else
    {
      emit_insn (gen_rtx_SET (VOIDmode, operands[4], addr));
      operands[0] = replace_equiv_address (operands[0], operands[4]);
    }
}")

(define_insn "clrmemsi_postreload"
  [(set (mem:BLK (match_operand:SI 0 "register_operand" "+r,r"))
	(const_int 0))
   (clobber (match_operand:SI 1 "register_operand" "=&r,&r"))	;loop cnt/tmp
   (clobber (match_dup 0))
   (use (match_operand:SI 2 "arith_operand" "J,1"))	 ;byte count
   (use (match_operand:SI 3 "const_int_operand" "n,n"))  ;alignment
   (const_int 0)]
  "!TARGET_64BIT && reload_completed"
  "* return output_block_clear (operands, !which_alternative);"
  [(set_attr "type" "multi,multi")])

(define_expand "setmemdi"
  [(parallel [(set (match_operand:BLK 0 "" "")
		   (match_operand 2 "const_int_operand" ""))
	      (clobber (match_dup 4))
	      (clobber (match_dup 5))
	      (use (match_operand:DI 1 "arith_operand" ""))
	      (use (match_operand:DI 3 "const_int_operand" ""))])]
  "TARGET_64BIT && optimize > 0"
  "
{
  int size, align;

  /* If value to set is not zero, use the library routine.  */
  if (operands[2] != const0_rtx)
    FAIL;

  /* Undetermined size, use the library routine.  */
  if (GET_CODE (operands[1]) != CONST_INT)
    FAIL;

  size = INTVAL (operands[1]);
  align = INTVAL (operands[3]);
  align = align > 8 ? 8 : align;

  /* If size/alignment is large, then use the library routines.  */
  if (size / align > 16)
    FAIL;

  /* This does happen, but not often enough to worry much about.  */
  if (size / align < MOVE_RATIO)
    FAIL;
  
  /* Fall through means we're going to use our block clear pattern.  */
  operands[0]
    = replace_equiv_address (operands[0],
			     copy_to_mode_reg (DImode, XEXP (operands[0], 0)));
  operands[4] = gen_reg_rtx (DImode);
  operands[5] = gen_reg_rtx (DImode);
}")

(define_insn "clrmemdi_prereload"
  [(set (mem:BLK (match_operand:DI 0 "register_operand" "r,r"))
	(const_int 0))
   (clobber (match_operand:DI 1 "register_operand" "=&r,&r"))	;loop cnt/tmp
   (clobber (match_operand:DI 4 "register_operand" "=&r,&r"))	;item tmp1
   (use (match_operand:DI 2 "arith_operand" "J,1"))	 ;byte count
   (use (match_operand:DI 3 "const_int_operand" "n,n"))] ;alignment
  "TARGET_64BIT"
  "#"
  [(set_attr "type" "multi,multi")])

(define_split
  [(parallel [(set (match_operand:BLK 0 "memory_operand" "")
		   (const_int 0))
	      (clobber (match_operand:DI 1 "register_operand" ""))
	      (clobber (match_operand:DI 4 "register_operand" ""))
	      (use (match_operand:DI 2 "arith_operand" ""))
	      (use (match_operand:DI 3 "const_int_operand" ""))])]
  "TARGET_64BIT && reload_completed && !flag_peephole2
   && GET_CODE (operands[0]) == MEM
   && register_operand (XEXP (operands[0], 0), DImode)"
  [(set (match_dup 4) (match_dup 5))
   (parallel [(set (match_dup 0) (const_int 0))
   	      (clobber (match_dup 1))
   	      (clobber (match_dup 4))
   	      (use (match_dup 2))
   	      (use (match_dup 3))
	      (const_int 0)])]
  "
{
  operands[5] = XEXP (operands[0], 0);
  operands[0] = replace_equiv_address (operands[0], operands[4]);
}")

(define_peephole2
  [(parallel [(set (match_operand:BLK 0 "memory_operand" "")
		   (const_int 0))
	      (clobber (match_operand:DI 1 "register_operand" ""))
	      (clobber (match_operand:DI 4 "register_operand" ""))
	      (use (match_operand:DI 2 "arith_operand" ""))
	      (use (match_operand:DI 3 "const_int_operand" ""))])]
  "TARGET_64BIT
   && GET_CODE (operands[0]) == MEM
   && register_operand (XEXP (operands[0], 0), DImode)"
  [(parallel [(set (match_dup 0) (const_int 0))
   	      (clobber (match_dup 1))
   	      (clobber (match_dup 4))
   	      (use (match_dup 2))
   	      (use (match_dup 3))
	      (const_int 0)])]
  "
{  
  rtx addr = XEXP (operands[0], 0);
  if (dead_or_set_p (curr_insn, addr))
    operands[4] = addr;
  else
    {
      emit_insn (gen_rtx_SET (VOIDmode, operands[4], addr));
      operands[0] = replace_equiv_address (operands[0], operands[4]);
    }
}")

(define_insn "clrmemdi_postreload"
  [(set (mem:BLK (match_operand:DI 0 "register_operand" "+r,r"))
	(const_int 0))
   (clobber (match_operand:DI 1 "register_operand" "=&r,&r"))	;loop cnt/tmp
   (clobber (match_dup 0))
   (use (match_operand:DI 2 "arith_operand" "J,1"))	 ;byte count
   (use (match_operand:DI 3 "const_int_operand" "n,n"))  ;alignment
   (const_int 0)]
  "TARGET_64BIT && reload_completed"
  "* return output_block_clear (operands, !which_alternative);"
  [(set_attr "type" "multi,multi")])

;; Floating point move insns

;; This pattern forces (set (reg:DF ...) (const_double ...))
;; to be reloaded by putting the constant into memory when
;; reg is a floating point register.
;;
;; For integer registers we use ldil;ldo to set the appropriate
;; value.
;;
;; This must come before the movdf pattern, and it must be present
;; to handle obscure reloading cases.
(define_insn ""
  [(set (match_operand:DF 0 "register_operand" "=?r,f")
	(match_operand:DF 1 "" "?F,m"))]
  "GET_CODE (operands[1]) == CONST_DOUBLE
   && operands[1] != CONST0_RTX (DFmode)
   && !TARGET_64BIT
   && !TARGET_SOFT_FLOAT"
  "* return (which_alternative == 0 ? output_move_double (operands)
				    : \"fldd%F1 %1,%0\");"
  [(set_attr "type" "move,fpload")
   (set_attr "length" "16,4")])

(define_expand "movdf"
  [(set (match_operand:DF 0 "general_operand" "")
	(match_operand:DF 1 "general_operand" ""))]
  ""
  "
{
  if (GET_CODE (operands[1]) == CONST_DOUBLE
      && operands[1] != CONST0_RTX (DFmode))
    {
      /* Reject CONST_DOUBLE loads to all hard registers when
	 generating 64-bit code and to floating point registers
	 when generating 32-bit code.  */
      if (REG_P (operands[0])
	  && HARD_REGISTER_P (operands[0])
	  && (TARGET_64BIT || REGNO (operands[0]) >= 32))
	FAIL;

      if (TARGET_64BIT)
	operands[1] = force_const_mem (DFmode, operands[1]);
    }

  if (emit_move_sequence (operands, DFmode, 0))
    DONE;
}")

;; Handle DFmode input reloads requiring a general register as a
;; scratch register.
(define_expand "reload_indf"
  [(set (match_operand:DF 0 "register_operand" "=Z")
	(match_operand:DF 1 "non_hard_reg_operand" ""))
   (clobber (match_operand:DF 2 "register_operand" "=&r"))]
  ""
  "
{
  if (emit_move_sequence (operands, DFmode, operands[2]))
    DONE;

  /* We don't want the clobber emitted, so handle this ourselves.  */
  emit_insn (gen_rtx_SET (VOIDmode, operands[0], operands[1]));
  DONE;
}")

;; Handle DFmode output reloads requiring a general register as a
;; scratch register.
(define_expand "reload_outdf" 
 [(set (match_operand:DF 0 "non_hard_reg_operand" "")
	(match_operand:DF 1  "register_operand" "Z"))
   (clobber (match_operand:DF 2 "register_operand" "=&r"))]
  ""
  "
{
  if (emit_move_sequence (operands, DFmode, operands[2]))
    DONE;

  /* We don't want the clobber emitted, so handle this ourselves.  */
  emit_insn (gen_rtx_SET (VOIDmode, operands[0], operands[1]));
  DONE;
}")

(define_insn ""
  [(set (match_operand:DF 0 "move_dest_operand"
			  "=f,*r,Q,?o,?Q,f,*r,*r,?*r,?f")
	(match_operand:DF 1 "reg_or_0_or_nonsymb_mem_operand"
			  "fG,*rG,f,*r,*r,RQ,o,RQ,f,*r"))]
  "(register_operand (operands[0], DFmode)
    || reg_or_0_operand (operands[1], DFmode))
   && !(GET_CODE (operands[1]) == CONST_DOUBLE
	&& GET_CODE (operands[0]) == MEM)
   && !TARGET_64BIT
   && !TARGET_SOFT_FLOAT"
  "*
{
  if ((FP_REG_P (operands[0]) || FP_REG_P (operands[1])
       || operands[1] == CONST0_RTX (DFmode))
      && !(REG_P (operands[0]) && REG_P (operands[1])
	   && FP_REG_P (operands[0]) ^ FP_REG_P (operands[1])))
    return output_fp_move_double (operands);
  return output_move_double (operands);
}"
  [(set_attr "type" "fpalu,move,fpstore,store,store,fpload,load,load,move,move")
   (set_attr "length" "4,8,4,8,16,4,8,16,12,12")])

(define_insn ""
  [(set (match_operand:DF 0 "indexed_memory_operand" "=R")
	(match_operand:DF 1 "reg_or_0_operand" "f"))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && reload_completed"
  "fstd%F0 %1,%0"
  [(set_attr "type" "fpstore")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4")])

(define_peephole2
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (mult:SI (match_operand:SI 1 "register_operand" "")
			  (const_int 8))
		 (match_operand:SI 2 "register_operand" "")))
   (set (mem:DF (match_dup 0))
        (match_operand:DF 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:DF (plus:SI (mult:SI (match_dup 1) (const_int 8)) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:SI (mult:SI (match_dup 1) (const_int 8))
			       (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (match_operand:SI 2 "register_operand" "")
		 (mult:SI (match_operand:SI 1 "register_operand" "")
			  (const_int 8))))
   (set (mem:DF (match_dup 0))
        (match_operand:DF 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:DF (plus:SI (mult:SI (match_dup 1) (const_int 8)) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:SI (mult:SI (match_dup 1) (const_int 8))
			       (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (mult:DI (match_operand:DI 1 "register_operand" "")
			  (const_int 8))
		 (match_operand:DI 2 "register_operand" "")))
   (set (mem:DF (match_dup 0))
        (match_operand:DF 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_64BIT
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:DF (plus:DI (mult:DI (match_dup 1) (const_int 8)) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:DI (mult:DI (match_dup 1) (const_int 8))
			       (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (match_operand:DI 2 "register_operand" "")
		 (mult:DI (match_operand:DI 1 "register_operand" "")
			  (const_int 8))))
   (set (mem:DF (match_dup 0))
        (match_operand:DF 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_64BIT
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:DF (plus:DI (mult:DI (match_dup 1) (const_int 8)) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:DI (mult:DI (match_dup 1) (const_int 8))
			       (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (match_operand:SI 1 "register_operand" "")
		 (match_operand:SI 2 "register_operand" "")))
   (set (mem:DF (match_dup 0))
        (match_operand:DF 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_NO_SPACE_REGS
   && REG_OK_FOR_INDEX_P (operands[1])
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:DF (plus:SI (match_dup 1) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:SI (match_dup 1) (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (match_operand:SI 1 "register_operand" "")
		 (match_operand:SI 2 "register_operand" "")))
   (set (mem:DF (match_dup 0))
        (match_operand:DF 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_NO_SPACE_REGS
   && REG_OK_FOR_BASE_P (operands[1])
   && REG_OK_FOR_INDEX_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:DF (plus:SI (match_dup 2) (match_dup 1)))
	(match_dup 3))
   (set (match_dup 0) (plus:SI (match_dup 2) (match_dup 1)))]
  "")

(define_peephole2
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (match_operand:DI 1 "register_operand" "")
		 (match_operand:DI 2 "register_operand" "")))
   (set (mem:DF (match_dup 0))
        (match_operand:DF 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_64BIT
   && TARGET_NO_SPACE_REGS
   && REG_OK_FOR_INDEX_P (operands[1])
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:DF (plus:DI (match_dup 1) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:DI (match_dup 1) (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (match_operand:DI 1 "register_operand" "")
		 (match_operand:DI 2 "register_operand" "")))
   (set (mem:DF (match_dup 0))
        (match_operand:DF 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_64BIT
   && TARGET_NO_SPACE_REGS
   && REG_OK_FOR_BASE_P (operands[1])
   && REG_OK_FOR_INDEX_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:DF (plus:DI (match_dup 2) (match_dup 1)))
	(match_dup 3))
   (set (match_dup 0) (plus:DI (match_dup 2) (match_dup 1)))]
  "")

(define_insn ""
  [(set (match_operand:DF 0 "move_dest_operand"
			  "=r,?o,?Q,r,r")
	(match_operand:DF 1 "reg_or_0_or_nonsymb_mem_operand"
			  "rG,r,r,o,RQ"))]
  "(register_operand (operands[0], DFmode)
    || reg_or_0_operand (operands[1], DFmode))
   && !TARGET_64BIT
   && TARGET_SOFT_FLOAT"
  "*
{
  return output_move_double (operands);
}"
  [(set_attr "type" "move,store,store,load,load")
   (set_attr "length" "8,8,16,8,16")])

(define_insn ""
  [(set (match_operand:DF 0 "move_dest_operand"
			  "=!*r,*r,*r,*r,*r,Q,f,f,T")
	(match_operand:DF 1 "move_src_operand"
			  "!*r,J,N,K,RQ,*rG,fG,RT,f"))]
  "(register_operand (operands[0], DFmode)
    || reg_or_0_operand (operands[1], DFmode))
   && !TARGET_SOFT_FLOAT && TARGET_64BIT"
  "@
   copy %1,%0
   ldi %1,%0
   ldil L'%1,%0
   depdi,z %z1,%0
   ldd%M1 %1,%0
   std%M0 %r1,%0
   fcpy,dbl %f1,%0
   fldd%F1 %1,%0
   fstd%F0 %1,%0"
  [(set_attr "type" "move,move,move,shift,load,store,fpalu,fpload,fpstore")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4,4,4,4,4,4,4,4,4")])


(define_expand "movdi"
  [(set (match_operand:DI 0 "general_operand" "")
	(match_operand:DI 1 "general_operand" ""))]
  ""
  "
{
  /* Except for zero, we don't support loading a CONST_INT directly
     to a hard floating-point register since a scratch register is
     needed for the operation.  While the operation could be handled
     before no_new_pseudos is true, the simplest solution is to fail.  */
  if (TARGET_64BIT
      && GET_CODE (operands[1]) == CONST_INT
      && operands[1] != CONST0_RTX (DImode)
      && REG_P (operands[0])
      && HARD_REGISTER_P (operands[0])
      && REGNO (operands[0]) >= 32)
    FAIL;

  if (emit_move_sequence (operands, DImode, 0))
    DONE;
}")

;; Handle DImode input reloads requiring %r1 as a scratch register.
(define_expand "reload_indi_r1"
  [(set (match_operand:DI 0 "register_operand" "=Z")
	(match_operand:DI 1 "non_hard_reg_operand" ""))
   (clobber (match_operand:SI 2 "register_operand" "=&a"))]
  ""
  "
{
  if (emit_move_sequence (operands, DImode, operands[2]))
    DONE;

  /* We don't want the clobber emitted, so handle this ourselves.  */
  emit_insn (gen_rtx_SET (VOIDmode, operands[0], operands[1]));
  DONE;
}")

;; Handle DImode input reloads requiring a general register as a
;; scratch register.
(define_expand "reload_indi"
  [(set (match_operand:DI 0 "register_operand" "=Z")
	(match_operand:DI 1 "non_hard_reg_operand" ""))
   (clobber (match_operand:SI 2 "register_operand" "=&r"))]
  ""
  "
{
  if (emit_move_sequence (operands, DImode, operands[2]))
    DONE;

  /* We don't want the clobber emitted, so handle this ourselves.  */
  emit_insn (gen_rtx_SET (VOIDmode, operands[0], operands[1]));
  DONE;
}")

;; Handle DImode output reloads requiring a general register as a
;; scratch register.
(define_expand "reload_outdi"
  [(set (match_operand:DI 0 "non_hard_reg_operand" "")
	(match_operand:DI 1 "register_operand" "Z"))
   (clobber (match_operand:SI 2 "register_operand" "=&r"))]
  ""
  "
{
  if (emit_move_sequence (operands, DImode, operands[2]))
    DONE;

  /* We don't want the clobber emitted, so handle this ourselves.  */
  emit_insn (gen_rtx_SET (VOIDmode, operands[0], operands[1]));
  DONE;
}")

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(high:DI (match_operand 1 "" "")))]
  "!TARGET_64BIT"
  "*
{
  rtx op0 = operands[0];
  rtx op1 = operands[1];

  switch (GET_CODE (op1))
    {
    case CONST_INT:
#if HOST_BITS_PER_WIDE_INT <= 32
      operands[0] = operand_subword (op0, 1, 0, DImode);
      output_asm_insn (\"ldil L'%1,%0\", operands);

      operands[0] = operand_subword (op0, 0, 0, DImode);
      if (INTVAL (op1) < 0)
	output_asm_insn (\"ldi -1,%0\", operands);
      else
	output_asm_insn (\"ldi 0,%0\", operands);
#else
      operands[0] = operand_subword (op0, 1, 0, DImode);
      operands[1] = GEN_INT (INTVAL (op1) & 0xffffffff);
      output_asm_insn (\"ldil L'%1,%0\", operands);

      operands[0] = operand_subword (op0, 0, 0, DImode);
      operands[1] = GEN_INT (INTVAL (op1) >> 32);
      output_asm_insn (singlemove_string (operands), operands);
#endif
      break;

    case CONST_DOUBLE:
      operands[0] = operand_subword (op0, 1, 0, DImode);
      operands[1] = GEN_INT (CONST_DOUBLE_LOW (op1));
      output_asm_insn (\"ldil L'%1,%0\", operands);

      operands[0] = operand_subword (op0, 0, 0, DImode);
      operands[1] = GEN_INT (CONST_DOUBLE_HIGH (op1));
      output_asm_insn (singlemove_string (operands), operands);
      break;

    default:
      gcc_unreachable ();
    }
  return \"\";
}"
  [(set_attr "type" "move")
   (set_attr "length" "12")])

(define_insn ""
  [(set (match_operand:DI 0 "move_dest_operand"
			  "=r,o,Q,r,r,r,*f,*f,T,?r,?*f")
	(match_operand:DI 1 "general_operand"
			  "rM,r,r,o*R,Q,i,*fM,RT,*f,*f,r"))]
  "(register_operand (operands[0], DImode)
    || reg_or_0_operand (operands[1], DImode))
   && !TARGET_64BIT
   && !TARGET_SOFT_FLOAT"
  "*
{
  if ((FP_REG_P (operands[0]) || FP_REG_P (operands[1])
       || operands[1] == CONST0_RTX (DFmode))
      && !(REG_P (operands[0]) && REG_P (operands[1])
	   && FP_REG_P (operands[0]) ^ FP_REG_P (operands[1])))
    return output_fp_move_double (operands);
  return output_move_double (operands);
}"
  [(set_attr "type"
    "move,store,store,load,load,multi,fpalu,fpload,fpstore,move,move")
   (set_attr "length" "8,8,16,8,16,16,4,4,4,12,12")])

(define_insn ""
  [(set (match_operand:DI 0 "move_dest_operand"
			  "=r,r,r,r,r,r,Q,!*q,!r,!*f,*f,T")
	(match_operand:DI 1 "move_src_operand"
			  "A,r,J,N,K,RQ,rM,!rM,!*q,!*fM,RT,*f"))]
  "(register_operand (operands[0], DImode)
    || reg_or_0_operand (operands[1], DImode))
   && !TARGET_SOFT_FLOAT && TARGET_64BIT"
  "@
   ldd RT'%A1,%0
   copy %1,%0
   ldi %1,%0
   ldil L'%1,%0
   depdi,z %z1,%0
   ldd%M1 %1,%0
   std%M0 %r1,%0
   mtsar %r1
   {mfctl|mfctl,w} %%sar,%0
   fcpy,dbl %f1,%0
   fldd%F1 %1,%0
   fstd%F0 %1,%0"
  [(set_attr "type" "load,move,move,move,shift,load,store,move,move,fpalu,fpload,fpstore")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4,4,4,4,4,4,4,4,4,4,4,4")])

(define_insn ""
  [(set (match_operand:DI 0 "indexed_memory_operand" "=R")
	(match_operand:DI 1 "register_operand" "f"))]
  "!TARGET_SOFT_FLOAT
   && TARGET_64BIT
   && !TARGET_DISABLE_INDEXING
   && reload_completed"
  "fstd%F0 %1,%0"
  [(set_attr "type" "fpstore")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4")])

(define_peephole2
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (mult:DI (match_operand:DI 1 "register_operand" "")
			  (const_int 8))
		 (match_operand:DI 2 "register_operand" "")))
   (set (mem:DI (match_dup 0))
        (match_operand:DI 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_64BIT
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:DI (plus:DI (mult:DI (match_dup 1) (const_int 8)) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:DI (mult:DI (match_dup 1) (const_int 8))
			       (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (match_operand:DI 2 "register_operand" "")
		 (mult:DI (match_operand:DI 1 "register_operand" "")
			  (const_int 8))))
   (set (mem:DI (match_dup 0))
        (match_operand:DI 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_64BIT
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:DI (plus:DI (mult:DI (match_dup 1) (const_int 8)) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:DI (mult:DI (match_dup 1) (const_int 8))
			       (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (match_operand:DI 1 "register_operand" "")
		 (match_operand:DI 2 "register_operand" "")))
   (set (mem:DI (match_dup 0))
        (match_operand:DI 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_64BIT
   && TARGET_NO_SPACE_REGS
   && REG_OK_FOR_INDEX_P (operands[1])
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:DI (plus:DI (match_dup 1) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:DI (match_dup 1) (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (match_operand:DI 1 "register_operand" "")
		 (match_operand:DI 2 "register_operand" "")))
   (set (mem:DI (match_dup 0))
        (match_operand:DI 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_64BIT
   && TARGET_NO_SPACE_REGS
   && REG_OK_FOR_BASE_P (operands[1])
   && REG_OK_FOR_INDEX_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:DI (plus:DI (match_dup 2) (match_dup 1)))
	(match_dup 3))
   (set (match_dup 0) (plus:DI (match_dup 2) (match_dup 1)))]
  "")

(define_insn ""
  [(set (match_operand:DI 0 "move_dest_operand"
			  "=r,o,Q,r,r,r")
	(match_operand:DI 1 "general_operand"
			  "rM,r,r,o,Q,i"))]
  "(register_operand (operands[0], DImode)
    || reg_or_0_operand (operands[1], DImode))
   && !TARGET_64BIT
   && TARGET_SOFT_FLOAT"
  "*
{
  return output_move_double (operands);
}"
  [(set_attr "type" "move,store,store,load,load,multi")
   (set_attr "length" "8,8,16,8,16,16")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r,&r")
	(lo_sum:DI (match_operand:DI 1 "register_operand" "0,r")
		   (match_operand:DI 2 "immediate_operand" "i,i")))]
  "!TARGET_64BIT"
  "*
{
  /* Don't output a 64 bit constant, since we can't trust the assembler to
     handle it correctly.  */
  if (GET_CODE (operands[2]) == CONST_DOUBLE)
    operands[2] = GEN_INT (CONST_DOUBLE_LOW (operands[2]));
  else if (HOST_BITS_PER_WIDE_INT > 32
	   && GET_CODE (operands[2]) == CONST_INT)
    operands[2] = GEN_INT (INTVAL (operands[2]) & 0xffffffff);
  if (which_alternative == 1)
    output_asm_insn (\"copy %1,%0\", operands);
  return \"ldo R'%G2(%R1),%R0\";
}"
  [(set_attr "type" "move,move")
   (set_attr "length" "4,8")])

;; This pattern forces (set (reg:SF ...) (const_double ...))
;; to be reloaded by putting the constant into memory when
;; reg is a floating point register.
;;
;; For integer registers we use ldil;ldo to set the appropriate
;; value.
;;
;; This must come before the movsf pattern, and it must be present
;; to handle obscure reloading cases.
(define_insn ""
  [(set (match_operand:SF 0 "register_operand" "=?r,f")
	(match_operand:SF 1 "" "?F,m"))]
  "GET_CODE (operands[1]) == CONST_DOUBLE
   && operands[1] != CONST0_RTX (SFmode)
   && ! TARGET_SOFT_FLOAT"
  "* return (which_alternative == 0 ? singlemove_string (operands)
				    : \" fldw%F1 %1,%0\");"
  [(set_attr "type" "move,fpload")
   (set_attr "length" "8,4")])

(define_expand "movsf"
  [(set (match_operand:SF 0 "general_operand" "")
	(match_operand:SF 1 "general_operand" ""))]
  ""
  "
{
  /* Reject CONST_DOUBLE loads to floating point registers.  */
  if (GET_CODE (operands[1]) == CONST_DOUBLE
      && operands[1] != CONST0_RTX (SFmode)
      && REG_P (operands[0])
      && HARD_REGISTER_P (operands[0])
      && REGNO (operands[0]) >= 32)
    FAIL;

  if (emit_move_sequence (operands, SFmode, 0))
    DONE;
}")

;; Handle SFmode input reloads requiring a general register as a
;; scratch register.
(define_expand "reload_insf"
  [(set (match_operand:SF 0 "register_operand" "=Z")
	(match_operand:SF 1 "non_hard_reg_operand" ""))
   (clobber (match_operand:SF 2 "register_operand" "=&r"))]
  ""
  "
{
  if (emit_move_sequence (operands, SFmode, operands[2]))
    DONE;

  /* We don't want the clobber emitted, so handle this ourselves.  */
  emit_insn (gen_rtx_SET (VOIDmode, operands[0], operands[1]));
  DONE;
}")

;; Handle SFmode output reloads requiring a general register as a
;; scratch register.
(define_expand "reload_outsf"
  [(set (match_operand:SF 0 "non_hard_reg_operand" "")
	(match_operand:SF 1  "register_operand" "Z"))
   (clobber (match_operand:SF 2 "register_operand" "=&r"))]
  ""
  "
{
  if (emit_move_sequence (operands, SFmode, operands[2]))
    DONE;

  /* We don't want the clobber emitted, so handle this ourselves.  */
  emit_insn (gen_rtx_SET (VOIDmode, operands[0], operands[1]));
  DONE;
}")

(define_insn ""
  [(set (match_operand:SF 0 "move_dest_operand"
			  "=f,!*r,f,*r,Q,Q,?*r,?f")
	(match_operand:SF 1 "reg_or_0_or_nonsymb_mem_operand"
			  "fG,!*rG,RQ,RQ,f,*rG,f,*r"))]
  "(register_operand (operands[0], SFmode)
    || reg_or_0_operand (operands[1], SFmode))
   && !TARGET_SOFT_FLOAT
   && !TARGET_64BIT"
  "@
   fcpy,sgl %f1,%0
   copy %r1,%0
   fldw%F1 %1,%0
   ldw%M1 %1,%0
   fstw%F0 %1,%0
   stw%M0 %r1,%0
   {fstws|fstw} %1,-16(%%sp)\n\t{ldws|ldw} -16(%%sp),%0
   {stws|stw} %1,-16(%%sp)\n\t{fldws|fldw} -16(%%sp),%0"
  [(set_attr "type" "fpalu,move,fpload,load,fpstore,store,move,move")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4,4,4,4,4,4,8,8")])

(define_insn ""
  [(set (match_operand:SF 0 "move_dest_operand"
			  "=f,!*r,f,*r,Q,Q")
	(match_operand:SF 1 "reg_or_0_or_nonsymb_mem_operand"
			  "fG,!*rG,RQ,RQ,f,*rG"))]
  "(register_operand (operands[0], SFmode)
    || reg_or_0_operand (operands[1], SFmode))
   && !TARGET_SOFT_FLOAT
   && TARGET_64BIT"
  "@
   fcpy,sgl %f1,%0
   copy %r1,%0
   fldw%F1 %1,%0
   ldw%M1 %1,%0
   fstw%F0 %1,%0
   stw%M0 %r1,%0"
  [(set_attr "type" "fpalu,move,fpload,load,fpstore,store")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4,4,4,4,4,4")])

(define_insn ""
  [(set (match_operand:SF 0 "indexed_memory_operand" "=R")
	(match_operand:SF 1 "register_operand" "f"))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && reload_completed"
  "fstw%F0 %1,%0"
  [(set_attr "type" "fpstore")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4")])

(define_peephole2
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (mult:SI (match_operand:SI 1 "register_operand" "")
			  (const_int 4))
		 (match_operand:SI 2 "register_operand" "")))
   (set (mem:SF (match_dup 0))
        (match_operand:SF 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:SF (plus:SI (mult:SI (match_dup 1) (const_int 4)) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:SI (mult:SI (match_dup 1) (const_int 4))
			       (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (match_operand:SI 2 "register_operand" "")
		 (mult:SI (match_operand:SI 1 "register_operand" "")
			  (const_int 4))))
   (set (mem:SF (match_dup 0))
        (match_operand:SF 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:SF (plus:SI (mult:SI (match_dup 1) (const_int 4)) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:SI (mult:SI (match_dup 1) (const_int 4))
			       (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (mult:DI (match_operand:DI 1 "register_operand" "")
			  (const_int 4))
		 (match_operand:DI 2 "register_operand" "")))
   (set (mem:SF (match_dup 0))
        (match_operand:SF 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_64BIT
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:SF (plus:DI (mult:DI (match_dup 1) (const_int 4)) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:DI (mult:DI (match_dup 1) (const_int 4))
			       (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (match_operand:DI 2 "register_operand" "")
		 (mult:DI (match_operand:DI 1 "register_operand" "")
			  (const_int 4))))
   (set (mem:SF (match_dup 0))
        (match_operand:SF 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_64BIT
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:SF (plus:DI (mult:DI (match_dup 1) (const_int 4)) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:DI (mult:DI (match_dup 1) (const_int 4))
			       (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (match_operand:SI 1 "register_operand" "")
		 (match_operand:SI 2 "register_operand" "")))
   (set (mem:SF (match_dup 0))
        (match_operand:SF 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_NO_SPACE_REGS
   && REG_OK_FOR_INDEX_P (operands[1])
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:SF (plus:SI (match_dup 1) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:SI (match_dup 1) (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (match_operand:SI 1 "register_operand" "")
		 (match_operand:SI 2 "register_operand" "")))
   (set (mem:SF (match_dup 0))
        (match_operand:SF 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_NO_SPACE_REGS
   && REG_OK_FOR_BASE_P (operands[1])
   && REG_OK_FOR_INDEX_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:SF (plus:SI (match_dup 2) (match_dup 1)))
	(match_dup 3))
   (set (match_dup 0) (plus:SI (match_dup 2) (match_dup 1)))]
  "")

(define_peephole2
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (match_operand:DI 1 "register_operand" "")
		 (match_operand:DI 2 "register_operand" "")))
   (set (mem:SF (match_dup 0))
        (match_operand:SF 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_64BIT
   && TARGET_NO_SPACE_REGS
   && REG_OK_FOR_INDEX_P (operands[1])
   && REG_OK_FOR_BASE_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:SF (plus:DI (match_dup 1) (match_dup 2)))
	(match_dup 3))
   (set (match_dup 0) (plus:DI (match_dup 1) (match_dup 2)))]
  "")

(define_peephole2
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (match_operand:DI 1 "register_operand" "")
		 (match_operand:DI 2 "register_operand" "")))
   (set (mem:SF (match_dup 0))
        (match_operand:SF 3 "register_operand" ""))]
  "!TARGET_SOFT_FLOAT
   && !TARGET_DISABLE_INDEXING
   && TARGET_64BIT
   && TARGET_NO_SPACE_REGS
   && REG_OK_FOR_BASE_P (operands[1])
   && REG_OK_FOR_INDEX_P (operands[2])
   && FP_REGNO_P (REGNO (operands[3]))"
  [(set (mem:SF (plus:DI (match_dup 2) (match_dup 1)))
	(match_dup 3))
   (set (match_dup 0) (plus:DI (match_dup 2) (match_dup 1)))]
  "")

(define_insn ""
  [(set (match_operand:SF 0 "move_dest_operand"
			  "=r,r,Q")
	(match_operand:SF 1 "reg_or_0_or_nonsymb_mem_operand"
			  "rG,RQ,rG"))]
  "(register_operand (operands[0], SFmode)
    || reg_or_0_operand (operands[1], SFmode))
   && TARGET_SOFT_FLOAT"
  "@
   copy %r1,%0
   ldw%M1 %1,%0
   stw%M0 %r1,%0"
  [(set_attr "type" "move,load,store")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4,4,4")])



;;- zero extension instructions
;; We have define_expand for zero extension patterns to make sure the
;; operands get loaded into registers.  The define_insns accept
;; memory operands.  This gives us better overall code than just
;; having a pattern that does or does not accept memory operands.

(define_expand "zero_extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "")
	(zero_extend:HI
	 (match_operand:QI 1 "register_operand" "")))]
  ""
  "")

(define_insn ""
  [(set (match_operand:HI 0 "register_operand" "=r,r")
	(zero_extend:HI
	 (match_operand:QI 1 "move_src_operand" "r,RQ")))]
  "GET_CODE (operands[1]) != CONST_INT"
  "@
   {extru|extrw,u} %1,31,8,%0
   ldb%M1 %1,%0"
  [(set_attr "type" "shift,load")
   (set_attr "length" "4,4")])

(define_expand "zero_extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "")
	(zero_extend:SI
	 (match_operand:QI 1 "register_operand" "")))]
  ""
  "")

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(zero_extend:SI
	 (match_operand:QI 1 "move_src_operand" "r,RQ")))]
  "GET_CODE (operands[1]) != CONST_INT"
  "@
   {extru|extrw,u} %1,31,8,%0
   ldb%M1 %1,%0"
  [(set_attr "type" "shift,load")
   (set_attr "length" "4,4")])

(define_expand "zero_extendhisi2"
  [(set (match_operand:SI 0 "register_operand" "")
	(zero_extend:SI
	 (match_operand:HI 1 "register_operand" "")))]
  ""
  "")

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(zero_extend:SI
	 (match_operand:HI 1 "move_src_operand" "r,RQ")))]
  "GET_CODE (operands[1]) != CONST_INT"
  "@
   {extru|extrw,u} %1,31,16,%0
   ldh%M1 %1,%0"
  [(set_attr "type" "shift,load")
   (set_attr "length" "4,4")])

(define_expand "zero_extendqidi2"
  [(set (match_operand:DI 0 "register_operand" "")
	(zero_extend:DI
	 (match_operand:QI 1 "register_operand" "")))]
  "TARGET_64BIT"
  "")

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(zero_extend:DI
	 (match_operand:QI 1 "move_src_operand" "r,RQ")))]
  "TARGET_64BIT && GET_CODE (operands[1]) != CONST_INT"
  "@
   extrd,u %1,63,8,%0
   ldb%M1 %1,%0"
  [(set_attr "type" "shift,load")
   (set_attr "length" "4,4")])

(define_expand "zero_extendhidi2"
  [(set (match_operand:DI 0 "register_operand" "")
	(zero_extend:DI
	 (match_operand:HI 1 "register_operand" "")))]
  "TARGET_64BIT"
  "")

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(zero_extend:DI
	 (match_operand:HI 1 "move_src_operand" "r,RQ")))]
  "TARGET_64BIT && GET_CODE (operands[1]) != CONST_INT"
  "@
   extrd,u %1,63,16,%0
   ldh%M1 %1,%0"
  [(set_attr "type" "shift,load")
   (set_attr "length" "4,4")])

(define_expand "zero_extendsidi2"
  [(set (match_operand:DI 0 "register_operand" "")
	(zero_extend:DI
	 (match_operand:SI 1 "register_operand" "")))]
  "TARGET_64BIT"
  "")

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(zero_extend:DI
	 (match_operand:SI 1 "move_src_operand" "r,RQ")))]
  "TARGET_64BIT && GET_CODE (operands[1]) != CONST_INT"
  "@
   extrd,u %1,63,32,%0
   ldw%M1 %1,%0"
  [(set_attr "type" "shift,load")
   (set_attr "length" "4,4")])

;;- sign extension instructions

(define_insn "extendhisi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(sign_extend:SI (match_operand:HI 1 "register_operand" "r")))]
  ""
  "{extrs|extrw,s} %1,31,16,%0"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn "extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(sign_extend:HI (match_operand:QI 1 "register_operand" "r")))]
  ""
  "{extrs|extrw,s} %1,31,8,%0"
  [(set_attr "type" "shift") 
  (set_attr "length" "4")])

(define_insn "extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(sign_extend:SI (match_operand:QI 1 "register_operand" "r")))]
  ""
  "{extrs|extrw,s} %1,31,8,%0"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn "extendqidi2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI (match_operand:QI 1 "register_operand" "r")))]
  "TARGET_64BIT"
  "extrd,s %1,63,8,%0"
  [(set_attr "type" "shift") 
  (set_attr "length" "4")])

(define_insn "extendhidi2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI (match_operand:HI 1 "register_operand" "r")))]
  "TARGET_64BIT"
  "extrd,s %1,63,16,%0"
  [(set_attr "type" "shift") 
  (set_attr "length" "4")])

(define_insn "extendsidi2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI (match_operand:SI 1 "register_operand" "r")))]
  "TARGET_64BIT"
  "extrd,s %1,63,32,%0"
  [(set_attr "type" "shift") 
  (set_attr "length" "4")])


;; Conversions between float and double.

(define_insn "extendsfdf2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(float_extend:DF
	 (match_operand:SF 1 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT"
  "{fcnvff|fcnv},sgl,dbl %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn "truncdfsf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(float_truncate:SF
	 (match_operand:DF 1 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT"
  "{fcnvff|fcnv},dbl,sgl %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

;; Conversion between fixed point and floating point.
;; Note that among the fix-to-float insns
;; the ones that start with SImode come first.
;; That is so that an operand that is a CONST_INT
;; (and therefore lacks a specific machine mode).
;; will be recognized as SImode (which is always valid)
;; rather than as QImode or HImode.

;; This pattern forces (set (reg:SF ...) (float:SF (const_int ...)))
;; to be reloaded by putting the constant into memory.
;; It must come before the more general floatsisf2 pattern.
(define_insn ""
  [(set (match_operand:SF 0 "register_operand" "=f")
	(float:SF (match_operand:SI 1 "const_int_operand" "m")))]
  "! TARGET_SOFT_FLOAT"
  "fldw%F1 %1,%0\;{fcnvxf,sgl,sgl|fcnv,w,sgl} %0,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "8")])

(define_insn "floatsisf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(float:SF (match_operand:SI 1 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT"
  "{fcnvxf,sgl,sgl|fcnv,w,sgl} %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

;; This pattern forces (set (reg:DF ...) (float:DF (const_int ...)))
;; to be reloaded by putting the constant into memory.
;; It must come before the more general floatsidf2 pattern.
(define_insn ""
  [(set (match_operand:DF 0 "register_operand" "=f")
	(float:DF (match_operand:SI 1 "const_int_operand" "m")))]
  "! TARGET_SOFT_FLOAT"
  "fldw%F1 %1,%0\;{fcnvxf,sgl,dbl|fcnv,w,dbl} %0,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "8")])

(define_insn "floatsidf2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(float:DF (match_operand:SI 1 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT"
  "{fcnvxf,sgl,dbl|fcnv,w,dbl} %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_expand "floatunssisf2"
  [(set (subreg:SI (match_dup 2) 4)
	(match_operand:SI 1 "register_operand" ""))
   (set (subreg:SI (match_dup 2) 0)
	(const_int 0))
   (set (match_operand:SF 0 "register_operand" "")
	(float:SF (match_dup 2)))]
  "TARGET_PA_11 && ! TARGET_SOFT_FLOAT"
  "
{
  if (TARGET_PA_20)
    {
      emit_insn (gen_floatunssisf2_pa20 (operands[0], operands[1]));
      DONE;
    }
  operands[2] = gen_reg_rtx (DImode);
}")

(define_expand "floatunssidf2"
  [(set (subreg:SI (match_dup 2) 4)
	(match_operand:SI 1 "register_operand" ""))
   (set (subreg:SI (match_dup 2) 0)
	(const_int 0))
   (set (match_operand:DF 0 "register_operand" "")
	(float:DF (match_dup 2)))]
  "TARGET_PA_11 && ! TARGET_SOFT_FLOAT"
  "
{
  if (TARGET_PA_20)
    {
      emit_insn (gen_floatunssidf2_pa20 (operands[0], operands[1]));
      DONE;
    }
  operands[2] = gen_reg_rtx (DImode);
}")

(define_insn "floatdisf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(float:SF (match_operand:DI 1 "register_operand" "f")))]
  "TARGET_PA_11 && ! TARGET_SOFT_FLOAT"
  "{fcnvxf,dbl,sgl|fcnv,dw,sgl} %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn "floatdidf2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(float:DF (match_operand:DI 1 "register_operand" "f")))]
  "TARGET_PA_11 && ! TARGET_SOFT_FLOAT"
  "{fcnvxf,dbl,dbl|fcnv,dw,dbl} %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

;; Convert a float to an actual integer.
;; Truncation is performed as part of the conversion.

(define_insn "fix_truncsfsi2"
  [(set (match_operand:SI 0 "register_operand" "=f")
	(fix:SI (fix:SF (match_operand:SF 1 "register_operand" "f"))))]
  "! TARGET_SOFT_FLOAT"
  "{fcnvfxt,sgl,sgl|fcnv,t,sgl,w} %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn "fix_truncdfsi2"
  [(set (match_operand:SI 0 "register_operand" "=f")
	(fix:SI (fix:DF (match_operand:DF 1 "register_operand" "f"))))]
  "! TARGET_SOFT_FLOAT"
  "{fcnvfxt,dbl,sgl|fcnv,t,dbl,w} %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn "fix_truncsfdi2"
  [(set (match_operand:DI 0 "register_operand" "=f")
	(fix:DI (fix:SF (match_operand:SF 1 "register_operand" "f"))))]
  "TARGET_PA_11 && ! TARGET_SOFT_FLOAT"
  "{fcnvfxt,sgl,dbl|fcnv,t,sgl,dw} %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn "fix_truncdfdi2"
  [(set (match_operand:DI 0 "register_operand" "=f")
	(fix:DI (fix:DF (match_operand:DF 1 "register_operand" "f"))))]
  "TARGET_PA_11 && ! TARGET_SOFT_FLOAT"
  "{fcnvfxt,dbl,dbl|fcnv,t,dbl,dw} %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn "floatunssidf2_pa20"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(unsigned_float:DF (match_operand:SI 1 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  "fcnv,uw,dbl %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn "floatunssisf2_pa20"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(unsigned_float:SF (match_operand:SI 1 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  "fcnv,uw,sgl %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn "floatunsdisf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(unsigned_float:SF (match_operand:DI 1 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  "fcnv,udw,sgl %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn "floatunsdidf2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(unsigned_float:DF (match_operand:DI 1 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  "fcnv,udw,dbl %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn "fixuns_truncsfsi2"
  [(set (match_operand:SI 0 "register_operand" "=f")
	(unsigned_fix:SI (fix:SF (match_operand:SF 1 "register_operand" "f"))))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  "fcnv,t,sgl,uw %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn "fixuns_truncdfsi2"
  [(set (match_operand:SI 0 "register_operand" "=f")
	(unsigned_fix:SI (fix:DF (match_operand:DF 1 "register_operand" "f"))))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  "fcnv,t,dbl,uw %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn "fixuns_truncsfdi2"
  [(set (match_operand:DI 0 "register_operand" "=f")
	(unsigned_fix:DI (fix:SF (match_operand:SF 1 "register_operand" "f"))))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  "fcnv,t,sgl,udw %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn "fixuns_truncdfdi2"
  [(set (match_operand:DI 0 "register_operand" "=f")
	(unsigned_fix:DI (fix:DF (match_operand:DF 1 "register_operand" "f"))))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  "fcnv,t,dbl,udw %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

;;- arithmetic instructions

(define_expand "adddi3"
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (match_operand:DI 1 "register_operand" "")
		 (match_operand:DI 2 "adddi3_operand" "")))]
  ""
  "")

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (match_operand:DI 1 "register_operand" "%r")
		 (match_operand:DI 2 "arith11_operand" "rI")))]
  "!TARGET_64BIT"
  "*
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      if (INTVAL (operands[2]) >= 0)
	return \"addi %2,%R1,%R0\;{addc|add,c} %1,%%r0,%0\";
      else
	return \"addi %2,%R1,%R0\;{subb|sub,b} %1,%%r0,%0\";
    }
  else
    return \"add %R2,%R1,%R0\;{addc|add,c} %2,%1,%0\";
}"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(plus:DI (match_operand:DI 1 "register_operand" "%r,r")
		 (match_operand:DI 2 "arith_operand" "r,J")))]
  "TARGET_64BIT"
  "@
   add,l %1,%2,%0
   ldo %2(%1),%0"
  [(set_attr "type" "binary,binary")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4,4")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (not:DI (match_operand:DI 1 "register_operand" "r"))
		 (match_operand:DI 2 "register_operand" "r")))]
  "TARGET_64BIT"
  "uaddcm %2,%1,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (not:SI (match_operand:SI 1 "register_operand" "r"))
		 (match_operand:SI 2 "register_operand" "r")))]
  ""
  "uaddcm %2,%1,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "4")])

;; define_splits to optimize cases of adding a constant integer
;; to a register when the constant does not fit in 14 bits.  */
(define_split
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (match_operand:SI 1 "register_operand" "")
		 (match_operand:SI 2 "const_int_operand" "")))
   (clobber (match_operand:SI 4 "register_operand" ""))]
  "! cint_ok_for_move (INTVAL (operands[2]))
   && VAL_14_BITS_P (INTVAL (operands[2]) >> 1)"
  [(set (match_dup 4) (plus:SI (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (plus:SI (match_dup 4) (match_dup 3)))]
  "
{
  int val = INTVAL (operands[2]);
  int low = (val < 0) ? -0x2000 : 0x1fff;
  int rest = val - low;

  operands[2] = GEN_INT (rest);
  operands[3] = GEN_INT (low);
}")

(define_split
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (match_operand:SI 1 "register_operand" "")
		 (match_operand:SI 2 "const_int_operand" "")))
   (clobber (match_operand:SI 4 "register_operand" ""))]
  "! cint_ok_for_move (INTVAL (operands[2]))"
  [(set (match_dup 4) (match_dup 2))
   (set (match_dup 0) (plus:SI (mult:SI (match_dup 4) (match_dup 3))
			       (match_dup 1)))]
  "
{
  HOST_WIDE_INT intval = INTVAL (operands[2]);

  /* Try dividing the constant by 2, then 4, and finally 8 to see
     if we can get a constant which can be loaded into a register
     in a single instruction (cint_ok_for_move). 

     If that fails, try to negate the constant and subtract it
     from our input operand.  */
  if (intval % 2 == 0 && cint_ok_for_move (intval / 2))
    {
      operands[2] = GEN_INT (intval / 2);
      operands[3] = const2_rtx;
    }
  else if (intval % 4 == 0 && cint_ok_for_move (intval / 4))
    {
      operands[2] = GEN_INT (intval / 4);
      operands[3] = GEN_INT (4);
    }
  else if (intval % 8 == 0 && cint_ok_for_move (intval / 8))
    {
      operands[2] = GEN_INT (intval / 8);
      operands[3] = GEN_INT (8);
    }
  else if (cint_ok_for_move (-intval))
    {
      emit_insn (gen_rtx_SET (VOIDmode, operands[4], GEN_INT (-intval)));
      emit_insn (gen_subsi3 (operands[0], operands[1], operands[4]));
      DONE;
    }
  else
    FAIL;
}")

(define_insn "addsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(plus:SI (match_operand:SI 1 "register_operand" "%r,r")
		 (match_operand:SI 2 "arith_operand" "r,J")))]
  ""
  "@
   {addl|add,l} %1,%2,%0
   ldo %2(%1),%0"
  [(set_attr "type" "binary,binary")
   (set_attr "pa_combine_type" "addmove")
   (set_attr "length" "4,4")])

(define_expand "subdi3"
  [(set (match_operand:DI 0 "register_operand" "")
	(minus:DI (match_operand:DI 1 "register_operand" "")
		  (match_operand:DI 2 "register_operand" "")))]
  ""
  "")

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(minus:DI (match_operand:DI 1 "register_operand" "r")
		  (match_operand:DI 2 "register_operand" "r")))]
  "!TARGET_64BIT"
  "sub %R1,%R2,%R0\;{subb|sub,b} %1,%2,%0"
  [(set_attr "type" "binary")
  (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r,r,!q")
	(minus:DI (match_operand:DI 1 "arith11_operand" "r,I,!U")
		  (match_operand:DI 2 "register_operand" "r,r,!r")))]
  "TARGET_64BIT"
  "@
   sub %1,%2,%0
   subi %1,%2,%0
   mtsarcm %2"
  [(set_attr "type" "binary,binary,move")
  (set_attr "length" "4,4,4")])

(define_expand "subsi3"
  [(set (match_operand:SI 0 "register_operand" "")
	(minus:SI (match_operand:SI 1 "arith11_operand" "")
		  (match_operand:SI 2 "register_operand" "")))]
  ""
  "")

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(minus:SI (match_operand:SI 1 "arith11_operand" "r,I")
		  (match_operand:SI 2 "register_operand" "r,r")))]
  "!TARGET_PA_20"
  "@
   sub %1,%2,%0
   subi %1,%2,%0"
  [(set_attr "type" "binary,binary")
   (set_attr "length" "4,4")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r,r,!q")
	(minus:SI (match_operand:SI 1 "arith11_operand" "r,I,!S")
		  (match_operand:SI 2 "register_operand" "r,r,!r")))]
  "TARGET_PA_20"
  "@
   sub %1,%2,%0
   subi %1,%2,%0
   mtsarcm %2"
  [(set_attr "type" "binary,binary,move")
   (set_attr "length" "4,4,4")])

;; Clobbering a "register_operand" instead of a match_scratch
;; in operand3 of millicode calls avoids spilling %r1 and
;; produces better code.

;; The mulsi3 insns set up registers for the millicode call.
(define_expand "mulsi3"
  [(set (reg:SI 26) (match_operand:SI 1 "move_src_operand" ""))
   (set (reg:SI 25) (match_operand:SI 2 "move_src_operand" ""))
   (parallel [(set (reg:SI 29) (mult:SI (reg:SI 26) (reg:SI 25)))
	      (clobber (match_dup 3))
	      (clobber (reg:SI 26))
	      (clobber (reg:SI 25))
	      (clobber (match_dup 4))])
   (set (match_operand:SI 0 "move_dest_operand" "") (reg:SI 29))]
  ""
  "
{
  operands[4] = gen_rtx_REG (SImode, TARGET_64BIT ? 2 : 31);
  if (TARGET_PA_11 && !TARGET_DISABLE_FPREGS && !TARGET_SOFT_FLOAT)
    {
      rtx scratch = gen_reg_rtx (DImode);
      operands[1] = force_reg (SImode, operands[1]);
      operands[2] = force_reg (SImode, operands[2]);
      emit_insn (gen_umulsidi3 (scratch, operands[1], operands[2]));
      emit_insn (gen_movsi (operands[0],
			    gen_rtx_SUBREG (SImode, scratch,
					    GET_MODE_SIZE (SImode))));
      DONE;
    }
  operands[3] = gen_reg_rtx (SImode);
}")

(define_insn "umulsidi3"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=f")
	(mult:DI (zero_extend:DI (match_operand:SI 1 "nonimmediate_operand" "f"))
		 (zero_extend:DI (match_operand:SI 2 "nonimmediate_operand" "f"))))]
  "TARGET_PA_11 && ! TARGET_DISABLE_FPREGS && ! TARGET_SOFT_FLOAT"
  "xmpyu %1,%2,%0"
  [(set_attr "type" "fpmuldbl")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:DI 0 "nonimmediate_operand" "=f")
	(mult:DI (zero_extend:DI (match_operand:SI 1 "nonimmediate_operand" "f"))
		 (match_operand:DI 2 "uint32_operand" "f")))]
  "TARGET_PA_11 && ! TARGET_DISABLE_FPREGS && ! TARGET_SOFT_FLOAT && !TARGET_64BIT"
  "xmpyu %1,%R2,%0"
  [(set_attr "type" "fpmuldbl")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:DI 0 "nonimmediate_operand" "=f")
	(mult:DI (zero_extend:DI (match_operand:SI 1 "nonimmediate_operand" "f"))
		 (match_operand:DI 2 "uint32_operand" "f")))]
  "TARGET_PA_11 && ! TARGET_DISABLE_FPREGS && ! TARGET_SOFT_FLOAT && TARGET_64BIT"
  "xmpyu %1,%2R,%0"
  [(set_attr "type" "fpmuldbl")
   (set_attr "length" "4")])

(define_insn ""
  [(set (reg:SI 29) (mult:SI (reg:SI 26) (reg:SI 25)))
   (clobber (match_operand:SI 0 "register_operand" "=a"))
   (clobber (reg:SI 26))
   (clobber (reg:SI 25))
   (clobber (reg:SI 31))]
  "!TARGET_64BIT"
  "* return output_mul_insn (0, insn);"
  [(set_attr "type" "milli")
   (set (attr "length") (symbol_ref "attr_length_millicode_call (insn)"))])

(define_insn ""
  [(set (reg:SI 29) (mult:SI (reg:SI 26) (reg:SI 25)))
   (clobber (match_operand:SI 0 "register_operand" "=a"))
   (clobber (reg:SI 26))
   (clobber (reg:SI 25))
   (clobber (reg:SI 2))]
  "TARGET_64BIT"
  "* return output_mul_insn (0, insn);"
  [(set_attr "type" "milli")
   (set (attr "length") (symbol_ref "attr_length_millicode_call (insn)"))])

(define_expand "muldi3"
  [(set (match_operand:DI 0 "register_operand" "")
        (mult:DI (match_operand:DI 1 "register_operand" "")
		 (match_operand:DI 2 "register_operand" "")))]
  "TARGET_64BIT && ! TARGET_DISABLE_FPREGS && ! TARGET_SOFT_FLOAT"
  "
{
  rtx low_product = gen_reg_rtx (DImode);
  rtx cross_product1 = gen_reg_rtx (DImode);
  rtx cross_product2 = gen_reg_rtx (DImode);
  rtx cross_scratch = gen_reg_rtx (DImode);
  rtx cross_product = gen_reg_rtx (DImode);
  rtx op1l, op1r, op2l, op2r;
  rtx op1shifted, op2shifted;

  op1shifted = gen_reg_rtx (DImode);
  op2shifted = gen_reg_rtx (DImode);
  op1l = gen_reg_rtx (SImode);
  op1r = gen_reg_rtx (SImode);
  op2l = gen_reg_rtx (SImode);
  op2r = gen_reg_rtx (SImode);

  emit_move_insn (op1shifted, gen_rtx_LSHIFTRT (DImode, operands[1],
						GEN_INT (32)));
  emit_move_insn (op2shifted, gen_rtx_LSHIFTRT (DImode, operands[2],
						GEN_INT (32)));
  op1r = force_reg (SImode, gen_rtx_SUBREG (SImode, operands[1], 4));
  op2r = force_reg (SImode, gen_rtx_SUBREG (SImode, operands[2], 4));
  op1l = force_reg (SImode, gen_rtx_SUBREG (SImode, op1shifted, 4));
  op2l = force_reg (SImode, gen_rtx_SUBREG (SImode, op2shifted, 4));

  /* Emit multiplies for the cross products.  */
  emit_insn (gen_umulsidi3 (cross_product1, op2r, op1l));
  emit_insn (gen_umulsidi3 (cross_product2, op2l, op1r));

  /* Emit a multiply for the low sub-word.  */
  emit_insn (gen_umulsidi3 (low_product, copy_rtx (op2r), copy_rtx (op1r)));

  /* Sum the cross products and shift them into proper position.  */
  emit_insn (gen_adddi3 (cross_scratch, cross_product1, cross_product2));
  emit_insn (gen_ashldi3 (cross_product, cross_scratch, GEN_INT (32)));

  /* Add the cross product to the low product and store the result
     into the output operand .  */
  emit_insn (gen_adddi3 (operands[0], cross_product, low_product));
  DONE;
}")

;;; Division and mod.
(define_expand "divsi3"
  [(set (reg:SI 26) (match_operand:SI 1 "move_src_operand" ""))
   (set (reg:SI 25) (match_operand:SI 2 "move_src_operand" ""))
   (parallel [(set (reg:SI 29) (div:SI (reg:SI 26) (reg:SI 25)))
	      (clobber (match_dup 3))
	      (clobber (match_dup 4))
	      (clobber (reg:SI 26))
	      (clobber (reg:SI 25))
	      (clobber (match_dup 5))])
   (set (match_operand:SI 0 "move_dest_operand" "") (reg:SI 29))]
  ""
  "
{
  operands[3] = gen_reg_rtx (SImode);
  if (TARGET_64BIT)
    {
      operands[5] = gen_rtx_REG (SImode, 2);
      operands[4] = operands[5];
    }
  else
    {
      operands[5] = gen_rtx_REG (SImode, 31);
      operands[4] = gen_reg_rtx (SImode);
    }
  if (GET_CODE (operands[2]) == CONST_INT && emit_hpdiv_const (operands, 0))
    DONE;
}")

(define_insn ""
  [(set (reg:SI 29)
	(div:SI (reg:SI 26) (match_operand:SI 0 "div_operand" "")))
   (clobber (match_operand:SI 1 "register_operand" "=a"))
   (clobber (match_operand:SI 2 "register_operand" "=&r"))
   (clobber (reg:SI 26))
   (clobber (reg:SI 25))
   (clobber (reg:SI 31))]
  "!TARGET_64BIT"
  "*
   return output_div_insn (operands, 0, insn);"
  [(set_attr "type" "milli")
   (set (attr "length") (symbol_ref "attr_length_millicode_call (insn)"))])

(define_insn ""
  [(set (reg:SI 29)
	(div:SI (reg:SI 26) (match_operand:SI 0 "div_operand" "")))
   (clobber (match_operand:SI 1 "register_operand" "=a"))
   (clobber (match_operand:SI 2 "register_operand" "=&r"))
   (clobber (reg:SI 26))
   (clobber (reg:SI 25))
   (clobber (reg:SI 2))]
  "TARGET_64BIT"
  "*
   return output_div_insn (operands, 0, insn);"
  [(set_attr "type" "milli")
   (set (attr "length") (symbol_ref "attr_length_millicode_call (insn)"))])

(define_expand "udivsi3"
  [(set (reg:SI 26) (match_operand:SI 1 "move_src_operand" ""))
   (set (reg:SI 25) (match_operand:SI 2 "move_src_operand" ""))
   (parallel [(set (reg:SI 29) (udiv:SI (reg:SI 26) (reg:SI 25)))
	      (clobber (match_dup 3))
	      (clobber (match_dup 4))
	      (clobber (reg:SI 26))
	      (clobber (reg:SI 25))
	      (clobber (match_dup 5))])
   (set (match_operand:SI 0 "move_dest_operand" "") (reg:SI 29))]
  ""
  "
{
  operands[3] = gen_reg_rtx (SImode);

  if (TARGET_64BIT)
    {
      operands[5] = gen_rtx_REG (SImode, 2);
      operands[4] = operands[5];
    }
  else
    {
      operands[5] = gen_rtx_REG (SImode, 31);
      operands[4] = gen_reg_rtx (SImode);
    }
  if (GET_CODE (operands[2]) == CONST_INT && emit_hpdiv_const (operands, 1))
    DONE;
}")

(define_insn ""
  [(set (reg:SI 29)
	(udiv:SI (reg:SI 26) (match_operand:SI 0 "div_operand" "")))
   (clobber (match_operand:SI 1 "register_operand" "=a"))
   (clobber (match_operand:SI 2 "register_operand" "=&r"))
   (clobber (reg:SI 26))
   (clobber (reg:SI 25))
   (clobber (reg:SI 31))]
  "!TARGET_64BIT"
  "*
   return output_div_insn (operands, 1, insn);"
  [(set_attr "type" "milli")
   (set (attr "length") (symbol_ref "attr_length_millicode_call (insn)"))])

(define_insn ""
  [(set (reg:SI 29)
	(udiv:SI (reg:SI 26) (match_operand:SI 0 "div_operand" "")))
   (clobber (match_operand:SI 1 "register_operand" "=a"))
   (clobber (match_operand:SI 2 "register_operand" "=&r"))
   (clobber (reg:SI 26))
   (clobber (reg:SI 25))
   (clobber (reg:SI 2))]
  "TARGET_64BIT"
  "*
   return output_div_insn (operands, 1, insn);"
  [(set_attr "type" "milli")
   (set (attr "length") (symbol_ref "attr_length_millicode_call (insn)"))])

(define_expand "modsi3"
  [(set (reg:SI 26) (match_operand:SI 1 "move_src_operand" ""))
   (set (reg:SI 25) (match_operand:SI 2 "move_src_operand" ""))
   (parallel [(set (reg:SI 29) (mod:SI (reg:SI 26) (reg:SI 25)))
	      (clobber (match_dup 3))
	      (clobber (match_dup 4))
	      (clobber (reg:SI 26))
	      (clobber (reg:SI 25))
	      (clobber (match_dup 5))])
   (set (match_operand:SI 0 "move_dest_operand" "") (reg:SI 29))]
  ""
  "
{
  if (TARGET_64BIT)
    {
      operands[5] = gen_rtx_REG (SImode, 2);
      operands[4] = operands[5];
    }
  else
    {
      operands[5] = gen_rtx_REG (SImode, 31);
      operands[4] = gen_reg_rtx (SImode);
    }
  operands[3] = gen_reg_rtx (SImode);
}")

(define_insn ""
  [(set (reg:SI 29) (mod:SI (reg:SI 26) (reg:SI 25)))
   (clobber (match_operand:SI 0 "register_operand" "=a"))
   (clobber (match_operand:SI 1 "register_operand" "=&r"))
   (clobber (reg:SI 26))
   (clobber (reg:SI 25))
   (clobber (reg:SI 31))]
  "!TARGET_64BIT"
  "*
  return output_mod_insn (0, insn);"
  [(set_attr "type" "milli")
   (set (attr "length") (symbol_ref "attr_length_millicode_call (insn)"))])

(define_insn ""
  [(set (reg:SI 29) (mod:SI (reg:SI 26) (reg:SI 25)))
   (clobber (match_operand:SI 0 "register_operand" "=a"))
   (clobber (match_operand:SI 1 "register_operand" "=&r"))
   (clobber (reg:SI 26))
   (clobber (reg:SI 25))
   (clobber (reg:SI 2))]
  "TARGET_64BIT"
  "*
  return output_mod_insn (0, insn);"
  [(set_attr "type" "milli")
   (set (attr "length") (symbol_ref "attr_length_millicode_call (insn)"))])

(define_expand "umodsi3"
  [(set (reg:SI 26) (match_operand:SI 1 "move_src_operand" ""))
   (set (reg:SI 25) (match_operand:SI 2 "move_src_operand" ""))
   (parallel [(set (reg:SI 29) (umod:SI (reg:SI 26) (reg:SI 25)))
	      (clobber (match_dup 3))
	      (clobber (match_dup 4))
	      (clobber (reg:SI 26))
	      (clobber (reg:SI 25))
	      (clobber (match_dup 5))])
   (set (match_operand:SI 0 "move_dest_operand" "") (reg:SI 29))]
  ""
  "
{
  if (TARGET_64BIT)
    {
      operands[5] = gen_rtx_REG (SImode, 2);
      operands[4] = operands[5];
    }
  else
    {
      operands[5] = gen_rtx_REG (SImode, 31);
      operands[4] = gen_reg_rtx (SImode);
    }
  operands[3] = gen_reg_rtx (SImode);
}")

(define_insn ""
  [(set (reg:SI 29) (umod:SI (reg:SI 26) (reg:SI 25)))
   (clobber (match_operand:SI 0 "register_operand" "=a"))
   (clobber (match_operand:SI 1 "register_operand" "=&r"))
   (clobber (reg:SI 26))
   (clobber (reg:SI 25))
   (clobber (reg:SI 31))]
  "!TARGET_64BIT"
  "*
  return output_mod_insn (1, insn);"
  [(set_attr "type" "milli")
   (set (attr "length") (symbol_ref "attr_length_millicode_call (insn)"))])

(define_insn ""
  [(set (reg:SI 29) (umod:SI (reg:SI 26) (reg:SI 25)))
   (clobber (match_operand:SI 0 "register_operand" "=a"))
   (clobber (match_operand:SI 1 "register_operand" "=&r"))
   (clobber (reg:SI 26))
   (clobber (reg:SI 25))
   (clobber (reg:SI 2))]
  "TARGET_64BIT"
  "*
  return output_mod_insn (1, insn);"
  [(set_attr "type" "milli")
   (set (attr "length") (symbol_ref "attr_length_millicode_call (insn)"))])

;;- and instructions
;; We define DImode `and` so with DImode `not` we can get
;; DImode `andn`.  Other combinations are possible.

(define_expand "anddi3"
  [(set (match_operand:DI 0 "register_operand" "")
	(and:DI (match_operand:DI 1 "register_operand" "")
		(match_operand:DI 2 "and_operand" "")))]
  ""
  "
{
  /* Both operands must be register operands.  */
  if (!TARGET_64BIT && !register_operand (operands[2], DImode))
    FAIL;
}")

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(and:DI (match_operand:DI 1 "register_operand" "%r")
		(match_operand:DI 2 "register_operand" "r")))]
  "!TARGET_64BIT"
  "and %1,%2,%0\;and %R1,%R2,%R0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(and:DI (match_operand:DI 1 "register_operand" "%?r,0")
		(match_operand:DI 2 "and_operand" "rO,P")))]
  "TARGET_64BIT"
  "* return output_64bit_and (operands); "
  [(set_attr "type" "binary")
   (set_attr "length" "4")])

; The ? for op1 makes reload prefer zdepi instead of loading a huge
; constant with ldil;ldo.
(define_insn "andsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(and:SI (match_operand:SI 1 "register_operand" "%?r,0")
		(match_operand:SI 2 "and_operand" "rO,P")))]
  ""
  "* return output_and (operands); "
  [(set_attr "type" "binary,shift")
   (set_attr "length" "4,4")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(and:DI (not:DI (match_operand:DI 1 "register_operand" "r"))
		(match_operand:DI 2 "register_operand" "r")))]
  "!TARGET_64BIT"
  "andcm %2,%1,%0\;andcm %R2,%R1,%R0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(and:DI (not:DI (match_operand:DI 1 "register_operand" "r"))
		(match_operand:DI 2 "register_operand" "r")))]
  "TARGET_64BIT"
  "andcm %2,%1,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(and:SI (not:SI (match_operand:SI 1 "register_operand" "r"))
		(match_operand:SI 2 "register_operand" "r")))]
  ""
  "andcm %2,%1,%0"
  [(set_attr "type" "binary")
  (set_attr "length" "4")])

(define_expand "iordi3"
  [(set (match_operand:DI 0 "register_operand" "")
	(ior:DI (match_operand:DI 1 "register_operand" "")
		(match_operand:DI 2 "ior_operand" "")))]
  ""
  "
{
  /* Both operands must be register operands.  */
  if (!TARGET_64BIT && !register_operand (operands[2], DImode))
    FAIL;
}")

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ior:DI (match_operand:DI 1 "register_operand" "%r")
		(match_operand:DI 2 "register_operand" "r")))]
  "!TARGET_64BIT"
  "or %1,%2,%0\;or %R1,%R2,%R0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(ior:DI (match_operand:DI 1 "register_operand" "0,0")
		(match_operand:DI 2 "ior_operand" "M,i")))]
  "TARGET_64BIT"
  "* return output_64bit_ior (operands); "
  [(set_attr "type" "binary,shift")
   (set_attr "length" "4,4")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ior:DI (match_operand:DI 1 "register_operand" "%r")
		(match_operand:DI 2 "register_operand" "r")))]
  "TARGET_64BIT"
  "or %1,%2,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "4")])

;; Need a define_expand because we've run out of CONST_OK... characters.
(define_expand "iorsi3"
  [(set (match_operand:SI 0 "register_operand" "")
	(ior:SI (match_operand:SI 1 "register_operand" "")
		(match_operand:SI 2 "arith32_operand" "")))]
  ""
  "
{
  if (! (ior_operand (operands[2], SImode)
         || register_operand (operands[2], SImode)))
    operands[2] = force_reg (SImode, operands[2]);
}")

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(ior:SI (match_operand:SI 1 "register_operand" "0,0")
		(match_operand:SI 2 "ior_operand" "M,i")))]
  ""
  "* return output_ior (operands); "
  [(set_attr "type" "binary,shift")
   (set_attr "length" "4,4")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ior:SI (match_operand:SI 1 "register_operand" "%r")
		(match_operand:SI 2 "register_operand" "r")))]
  ""
  "or %1,%2,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "4")])

(define_expand "xordi3"
  [(set (match_operand:DI 0 "register_operand" "")
	(xor:DI (match_operand:DI 1 "register_operand" "")
		(match_operand:DI 2 "register_operand" "")))]
  ""
  "
{
}")

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(xor:DI (match_operand:DI 1 "register_operand" "%r")
		(match_operand:DI 2 "register_operand" "r")))]
  "!TARGET_64BIT"
  "xor %1,%2,%0\;xor %R1,%R2,%R0"
  [(set_attr "type" "binary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(xor:DI (match_operand:DI 1 "register_operand" "%r")
		(match_operand:DI 2 "register_operand" "r")))]
  "TARGET_64BIT"
  "xor %1,%2,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "4")])

(define_insn "xorsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(xor:SI (match_operand:SI 1 "register_operand" "%r")
		(match_operand:SI 2 "register_operand" "r")))]
  ""
  "xor %1,%2,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "4")])

(define_expand "negdi2"
  [(set (match_operand:DI 0 "register_operand" "")
	(neg:DI (match_operand:DI 1 "register_operand" "")))]
  ""
  "")

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(neg:DI (match_operand:DI 1 "register_operand" "r")))]
  "!TARGET_64BIT"
  "sub %%r0,%R1,%R0\;{subb|sub,b} %%r0,%1,%0"
  [(set_attr "type" "unary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(neg:DI (match_operand:DI 1 "register_operand" "r")))]
  "TARGET_64BIT"
  "sub %%r0,%1,%0"
  [(set_attr "type" "unary")
   (set_attr "length" "4")])

(define_insn "negsi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(neg:SI (match_operand:SI 1 "register_operand" "r")))]
  ""
  "sub %%r0,%1,%0"
  [(set_attr "type" "unary")
   (set_attr "length" "4")])

(define_expand "one_cmpldi2"
  [(set (match_operand:DI 0 "register_operand" "")
	(not:DI (match_operand:DI 1 "register_operand" "")))]
  ""
  "
{
}")

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(not:DI (match_operand:DI 1 "register_operand" "r")))]
  "!TARGET_64BIT"
  "uaddcm %%r0,%1,%0\;uaddcm %%r0,%R1,%R0"
  [(set_attr "type" "unary")
   (set_attr "length" "8")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(not:DI (match_operand:DI 1 "register_operand" "r")))]
  "TARGET_64BIT"
  "uaddcm %%r0,%1,%0"
  [(set_attr "type" "unary")
   (set_attr "length" "4")])

(define_insn "one_cmplsi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(not:SI (match_operand:SI 1 "register_operand" "r")))]
  ""
  "uaddcm %%r0,%1,%0"
  [(set_attr "type" "unary")
   (set_attr "length" "4")])

;; Floating point arithmetic instructions.

(define_insn "adddf3"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(plus:DF (match_operand:DF 1 "register_operand" "f")
		 (match_operand:DF 2 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT"
  "fadd,dbl %1,%2,%0"
  [(set_attr "type" "fpalu")
   (set_attr "pa_combine_type" "faddsub")
   (set_attr "length" "4")])

(define_insn "addsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(plus:SF (match_operand:SF 1 "register_operand" "f")
		 (match_operand:SF 2 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT"
  "fadd,sgl %1,%2,%0"
  [(set_attr "type" "fpalu")
   (set_attr "pa_combine_type" "faddsub")
   (set_attr "length" "4")])

(define_insn "subdf3"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(minus:DF (match_operand:DF 1 "register_operand" "f")
		  (match_operand:DF 2 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT"
  "fsub,dbl %1,%2,%0"
  [(set_attr "type" "fpalu")
   (set_attr "pa_combine_type" "faddsub")
   (set_attr "length" "4")])

(define_insn "subsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(minus:SF (match_operand:SF 1 "register_operand" "f")
		  (match_operand:SF 2 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT"
  "fsub,sgl %1,%2,%0"
  [(set_attr "type" "fpalu")
   (set_attr "pa_combine_type" "faddsub")
   (set_attr "length" "4")])

(define_insn "muldf3"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(mult:DF (match_operand:DF 1 "register_operand" "f")
		 (match_operand:DF 2 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT"
  "fmpy,dbl %1,%2,%0"
  [(set_attr "type" "fpmuldbl")
   (set_attr "pa_combine_type" "fmpy")
   (set_attr "length" "4")])

(define_insn "mulsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(mult:SF (match_operand:SF 1 "register_operand" "f")
		 (match_operand:SF 2 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT"
  "fmpy,sgl %1,%2,%0"
  [(set_attr "type" "fpmulsgl")
   (set_attr "pa_combine_type" "fmpy")
   (set_attr "length" "4")])

(define_insn "divdf3"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(div:DF (match_operand:DF 1 "register_operand" "f")
		(match_operand:DF 2 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT"
  "fdiv,dbl %1,%2,%0"
  [(set_attr "type" "fpdivdbl")
   (set_attr "length" "4")])

(define_insn "divsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(div:SF (match_operand:SF 1 "register_operand" "f")
		(match_operand:SF 2 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT"
  "fdiv,sgl %1,%2,%0"
  [(set_attr "type" "fpdivsgl")
   (set_attr "length" "4")])

;; Processors prior to PA 2.0 don't have a fneg instruction.  Fast
;; negation can be done by subtracting from plus zero.  However, this
;; violates the IEEE standard when negating plus and minus zero.
(define_expand "negdf2"
  [(parallel [(set (match_operand:DF 0 "register_operand" "")
		   (neg:DF (match_operand:DF 1 "register_operand" "")))
	      (use (match_dup 2))])]
  "! TARGET_SOFT_FLOAT"
{
  if (TARGET_PA_20 || flag_unsafe_math_optimizations)
    emit_insn (gen_negdf2_fast (operands[0], operands[1]));
  else
    {
      operands[2] = force_reg (DFmode,
	CONST_DOUBLE_FROM_REAL_VALUE (dconstm1, DFmode));
      emit_insn (gen_muldf3 (operands[0], operands[1], operands[2]));
    }
  DONE;
})

(define_insn "negdf2_fast"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(neg:DF (match_operand:DF 1 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT && (TARGET_PA_20 || flag_unsafe_math_optimizations)"
  "*
{
  if (TARGET_PA_20)
    return \"fneg,dbl %1,%0\";
  else
    return \"fsub,dbl %%fr0,%1,%0\";
}"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_expand "negsf2"
  [(parallel [(set (match_operand:SF 0 "register_operand" "")
		   (neg:SF (match_operand:SF 1 "register_operand" "")))
	      (use (match_dup 2))])]
  "! TARGET_SOFT_FLOAT"
{
  if (TARGET_PA_20 || flag_unsafe_math_optimizations)
    emit_insn (gen_negsf2_fast (operands[0], operands[1]));
  else
    {
      operands[2] = force_reg (SFmode,
	CONST_DOUBLE_FROM_REAL_VALUE (dconstm1, SFmode));
      emit_insn (gen_mulsf3 (operands[0], operands[1], operands[2]));
    }
  DONE;
})

(define_insn "negsf2_fast"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(neg:SF (match_operand:SF 1 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT && (TARGET_PA_20 || flag_unsafe_math_optimizations)"
  "*
{
  if (TARGET_PA_20)
    return \"fneg,sgl %1,%0\";
  else
    return \"fsub,sgl %%fr0,%1,%0\";
}"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn "absdf2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(abs:DF (match_operand:DF 1 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT"
  "fabs,dbl %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn "abssf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(abs:SF (match_operand:SF 1 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT"
  "fabs,sgl %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn "sqrtdf2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(sqrt:DF (match_operand:DF 1 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT"
  "fsqrt,dbl %1,%0"
  [(set_attr "type" "fpsqrtdbl")
   (set_attr "length" "4")])

(define_insn "sqrtsf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(sqrt:SF (match_operand:SF 1 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT"
  "fsqrt,sgl %1,%0"
  [(set_attr "type" "fpsqrtsgl")
   (set_attr "length" "4")])

;; PA 2.0 floating point instructions

; fmpyfadd patterns
(define_insn ""
  [(set (match_operand:DF 0 "register_operand" "=f")
	(plus:DF (mult:DF (match_operand:DF 1 "register_operand" "f")
			  (match_operand:DF 2 "register_operand" "f"))
		 (match_operand:DF 3 "register_operand" "f")))]
  "TARGET_PA_20 && ! TARGET_SOFT_FLOAT"
  "fmpyfadd,dbl %1,%2,%3,%0"
  [(set_attr "type" "fpmuldbl")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:DF 0 "register_operand" "=f")
	(plus:DF (match_operand:DF 1 "register_operand" "f")
		 (mult:DF (match_operand:DF 2 "register_operand" "f")
			  (match_operand:DF 3 "register_operand" "f"))))]
  "TARGET_PA_20 && ! TARGET_SOFT_FLOAT"
  "fmpyfadd,dbl %2,%3,%1,%0"
  [(set_attr "type" "fpmuldbl")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SF 0 "register_operand" "=f")
	(plus:SF (mult:SF (match_operand:SF 1 "register_operand" "f")
			  (match_operand:SF 2 "register_operand" "f"))
		 (match_operand:SF 3 "register_operand" "f")))]
  "TARGET_PA_20 && ! TARGET_SOFT_FLOAT"
  "fmpyfadd,sgl %1,%2,%3,%0"
  [(set_attr "type" "fpmulsgl")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SF 0 "register_operand" "=f")
	(plus:SF (match_operand:SF 1 "register_operand" "f")
		 (mult:SF (match_operand:SF 2 "register_operand" "f")
			  (match_operand:SF 3 "register_operand" "f"))))]
  "TARGET_PA_20 && ! TARGET_SOFT_FLOAT"
  "fmpyfadd,sgl %2,%3,%1,%0"
  [(set_attr "type" "fpmulsgl")
   (set_attr "length" "4")])

; fmpynfadd patterns
(define_insn ""
  [(set (match_operand:DF 0 "register_operand" "=f")
	(minus:DF (match_operand:DF 1 "register_operand" "f")
		  (mult:DF (match_operand:DF 2 "register_operand" "f")
			   (match_operand:DF 3 "register_operand" "f"))))]
  "TARGET_PA_20 && ! TARGET_SOFT_FLOAT"
  "fmpynfadd,dbl %2,%3,%1,%0"
  [(set_attr "type" "fpmuldbl")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SF 0 "register_operand" "=f")
	(minus:SF (match_operand:SF 1 "register_operand" "f")
		  (mult:SF (match_operand:SF 2 "register_operand" "f")
			   (match_operand:SF 3 "register_operand" "f"))))]
  "TARGET_PA_20 && ! TARGET_SOFT_FLOAT"
  "fmpynfadd,sgl %2,%3,%1,%0"
  [(set_attr "type" "fpmulsgl")
   (set_attr "length" "4")])

; fnegabs patterns
(define_insn ""
  [(set (match_operand:DF 0 "register_operand" "=f")
	(neg:DF (abs:DF (match_operand:DF 1 "register_operand" "f"))))]
  "TARGET_PA_20 && ! TARGET_SOFT_FLOAT"
  "fnegabs,dbl %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SF 0 "register_operand" "=f")
	(neg:SF (abs:SF (match_operand:SF 1 "register_operand" "f"))))]
  "TARGET_PA_20 && ! TARGET_SOFT_FLOAT"
  "fnegabs,sgl %1,%0"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

;; Generating a fused multiply sequence is a win for this case as it will
;; reduce the latency for the fused case without impacting the plain
;; multiply case.
;;
;; Similar possibilities exist for fnegabs, shadd and other insns which
;; perform two operations with the result of the first feeding the second.
(define_insn ""
  [(set (match_operand:DF 0 "register_operand" "=f")
	(plus:DF (mult:DF (match_operand:DF 1 "register_operand" "f")
			  (match_operand:DF 2 "register_operand" "f"))
		 (match_operand:DF 3 "register_operand" "f")))
   (set (match_operand:DF 4 "register_operand" "=&f")
	(mult:DF (match_dup 1) (match_dup 2)))]
  "(! TARGET_SOFT_FLOAT && TARGET_PA_20
    && ! (reg_overlap_mentioned_p (operands[4], operands[1])
          || reg_overlap_mentioned_p (operands[4], operands[2])))"
  "#"
  [(set_attr "type" "fpmuldbl")
   (set_attr "length" "8")])

;; We want to split this up during scheduling since we want both insns
;; to schedule independently.
(define_split
  [(set (match_operand:DF 0 "register_operand" "")
	(plus:DF (mult:DF (match_operand:DF 1 "register_operand" "")
			  (match_operand:DF 2 "register_operand" ""))
		 (match_operand:DF 3 "register_operand" "")))
   (set (match_operand:DF 4 "register_operand" "")
	(mult:DF (match_dup 1) (match_dup 2)))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  [(set (match_dup 4) (mult:DF (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (plus:DF (mult:DF (match_dup 1) (match_dup 2))
			       (match_dup 3)))]
  "")

(define_insn ""
  [(set (match_operand:SF 0 "register_operand" "=f")
	(plus:SF (mult:SF (match_operand:SF 1 "register_operand" "f")
			  (match_operand:SF 2 "register_operand" "f"))
		 (match_operand:SF 3 "register_operand" "f")))
   (set (match_operand:SF 4 "register_operand" "=&f")
	(mult:SF (match_dup 1) (match_dup 2)))]
  "(! TARGET_SOFT_FLOAT && TARGET_PA_20
    && ! (reg_overlap_mentioned_p (operands[4], operands[1])
          || reg_overlap_mentioned_p (operands[4], operands[2])))"
  "#"
  [(set_attr "type" "fpmuldbl")
   (set_attr "length" "8")])

;; We want to split this up during scheduling since we want both insns
;; to schedule independently.
(define_split
  [(set (match_operand:SF 0 "register_operand" "")
	(plus:SF (mult:SF (match_operand:SF 1 "register_operand" "")
			  (match_operand:SF 2 "register_operand" ""))
		 (match_operand:SF 3 "register_operand" "")))
   (set (match_operand:SF 4 "register_operand" "")
	(mult:SF (match_dup 1) (match_dup 2)))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  [(set (match_dup 4) (mult:SF (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (plus:SF (mult:SF (match_dup 1) (match_dup 2))
			       (match_dup 3)))]
  "")

;; Negating a multiply can be faked by adding zero in a fused multiply-add
;; instruction.
(define_insn ""
  [(set (match_operand:DF 0 "register_operand" "=f")
	(neg:DF (mult:DF (match_operand:DF 1 "register_operand" "f")
			 (match_operand:DF 2 "register_operand" "f"))))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  "fmpynfadd,dbl %1,%2,%%fr0,%0"
  [(set_attr "type" "fpmuldbl")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SF 0 "register_operand" "=f")
	(neg:SF (mult:SF (match_operand:SF 1 "register_operand" "f")
			 (match_operand:SF 2 "register_operand" "f"))))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  "fmpynfadd,sgl %1,%2,%%fr0,%0"
  [(set_attr "type" "fpmuldbl")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:DF 0 "register_operand" "=f")
	(neg:DF (mult:DF (match_operand:DF 1 "register_operand" "f")
			 (match_operand:DF 2 "register_operand" "f"))))
   (set (match_operand:DF 3 "register_operand" "=&f")
	(mult:DF (match_dup 1) (match_dup 2)))]
  "(! TARGET_SOFT_FLOAT && TARGET_PA_20
    && ! (reg_overlap_mentioned_p (operands[3], operands[1])
          || reg_overlap_mentioned_p (operands[3], operands[2])))"
  "#"
  [(set_attr "type" "fpmuldbl")
   (set_attr "length" "8")])

(define_split
  [(set (match_operand:DF 0 "register_operand" "")
	(neg:DF (mult:DF (match_operand:DF 1 "register_operand" "")
			 (match_operand:DF 2 "register_operand" ""))))
   (set (match_operand:DF 3 "register_operand" "")
	(mult:DF (match_dup 1) (match_dup 2)))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  [(set (match_dup 3) (mult:DF (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (neg:DF (mult:DF (match_dup 1) (match_dup 2))))]
  "")

(define_insn ""
  [(set (match_operand:SF 0 "register_operand" "=f")
	(neg:SF (mult:SF (match_operand:SF 1 "register_operand" "f")
			 (match_operand:SF 2 "register_operand" "f"))))
   (set (match_operand:SF 3 "register_operand" "=&f")
	(mult:SF (match_dup 1) (match_dup 2)))]
  "(! TARGET_SOFT_FLOAT && TARGET_PA_20
    && ! (reg_overlap_mentioned_p (operands[3], operands[1])
          || reg_overlap_mentioned_p (operands[3], operands[2])))"
  "#"
  [(set_attr "type" "fpmuldbl")
   (set_attr "length" "8")])

(define_split
  [(set (match_operand:SF 0 "register_operand" "")
	(neg:SF (mult:SF (match_operand:SF 1 "register_operand" "")
			 (match_operand:SF 2 "register_operand" ""))))
   (set (match_operand:SF 3 "register_operand" "")
	(mult:SF (match_dup 1) (match_dup 2)))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  [(set (match_dup 3) (mult:SF (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (neg:SF (mult:SF (match_dup 1) (match_dup 2))))]
  "")

;; Now fused multiplies with the result of the multiply negated.
(define_insn ""
  [(set (match_operand:DF 0 "register_operand" "=f")
	(plus:DF (neg:DF (mult:DF (match_operand:DF 1 "register_operand" "f")
				  (match_operand:DF 2 "register_operand" "f")))
		 (match_operand:DF 3 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  "fmpynfadd,dbl %1,%2,%3,%0"
  [(set_attr "type" "fpmuldbl")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SF 0 "register_operand" "=f")
	(plus:SF (neg:SF (mult:SF (match_operand:SF 1 "register_operand" "f")
			 (match_operand:SF 2 "register_operand" "f")))
		 (match_operand:SF 3 "register_operand" "f")))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  "fmpynfadd,sgl %1,%2,%3,%0"
  [(set_attr "type" "fpmuldbl")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:DF 0 "register_operand" "=f")
	(plus:DF (neg:DF (mult:DF (match_operand:DF 1 "register_operand" "f")
				  (match_operand:DF 2 "register_operand" "f")))
		 (match_operand:DF 3 "register_operand" "f")))
   (set (match_operand:DF 4 "register_operand" "=&f")
	(mult:DF (match_dup 1) (match_dup 2)))]
  "(! TARGET_SOFT_FLOAT && TARGET_PA_20
    && ! (reg_overlap_mentioned_p (operands[4], operands[1])
          || reg_overlap_mentioned_p (operands[4], operands[2])))"
  "#"
  [(set_attr "type" "fpmuldbl")
   (set_attr "length" "8")])

(define_split
  [(set (match_operand:DF 0 "register_operand" "")
	(plus:DF (neg:DF (mult:DF (match_operand:DF 1 "register_operand" "")
				  (match_operand:DF 2 "register_operand" "")))
		 (match_operand:DF 3 "register_operand" "")))
   (set (match_operand:DF 4 "register_operand" "")
	(mult:DF (match_dup 1) (match_dup 2)))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  [(set (match_dup 4) (mult:DF (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (plus:DF (neg:DF (mult:DF (match_dup 1) (match_dup 2)))
			       (match_dup 3)))]
  "")

(define_insn ""
  [(set (match_operand:SF 0 "register_operand" "=f")
	(plus:SF (neg:SF (mult:SF (match_operand:SF 1 "register_operand" "f")
				  (match_operand:SF 2 "register_operand" "f")))
		 (match_operand:SF 3 "register_operand" "f")))
   (set (match_operand:SF 4 "register_operand" "=&f")
	(mult:SF (match_dup 1) (match_dup 2)))]
  "(! TARGET_SOFT_FLOAT && TARGET_PA_20
    && ! (reg_overlap_mentioned_p (operands[4], operands[1])
          || reg_overlap_mentioned_p (operands[4], operands[2])))"
  "#"
  [(set_attr "type" "fpmuldbl")
   (set_attr "length" "8")])

(define_split
  [(set (match_operand:SF 0 "register_operand" "")
	(plus:SF (neg:SF (mult:SF (match_operand:SF 1 "register_operand" "")
				  (match_operand:SF 2 "register_operand" "")))
		 (match_operand:SF 3 "register_operand" "")))
   (set (match_operand:SF 4 "register_operand" "")
	(mult:SF (match_dup 1) (match_dup 2)))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  [(set (match_dup 4) (mult:SF (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (plus:SF (neg:SF (mult:SF (match_dup 1) (match_dup 2)))
			       (match_dup 3)))]
  "")

(define_insn ""
  [(set (match_operand:DF 0 "register_operand" "=f")
	(minus:DF (match_operand:DF 3 "register_operand" "f")
		  (mult:DF (match_operand:DF 1 "register_operand" "f")
			   (match_operand:DF 2 "register_operand" "f"))))
   (set (match_operand:DF 4 "register_operand" "=&f")
	(mult:DF (match_dup 1) (match_dup 2)))]
  "(! TARGET_SOFT_FLOAT && TARGET_PA_20
    && ! (reg_overlap_mentioned_p (operands[4], operands[1])
          || reg_overlap_mentioned_p (operands[4], operands[2])))"
  "#"
  [(set_attr "type" "fpmuldbl")
   (set_attr "length" "8")])

(define_split
  [(set (match_operand:DF 0 "register_operand" "")
	(minus:DF (match_operand:DF 3 "register_operand" "")
		  (mult:DF (match_operand:DF 1 "register_operand" "")
			   (match_operand:DF 2 "register_operand" ""))))
   (set (match_operand:DF 4 "register_operand" "")
	(mult:DF (match_dup 1) (match_dup 2)))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  [(set (match_dup 4) (mult:DF (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (minus:DF (match_dup 3)
				(mult:DF (match_dup 1) (match_dup 2))))]
  "")

(define_insn ""
  [(set (match_operand:SF 0 "register_operand" "=f")
	(minus:SF (match_operand:SF 3 "register_operand" "f")
		  (mult:SF (match_operand:SF 1 "register_operand" "f")
			   (match_operand:SF 2 "register_operand" "f"))))
   (set (match_operand:SF 4 "register_operand" "=&f")
	(mult:SF (match_dup 1) (match_dup 2)))]
  "(! TARGET_SOFT_FLOAT && TARGET_PA_20
    && ! (reg_overlap_mentioned_p (operands[4], operands[1])
          || reg_overlap_mentioned_p (operands[4], operands[2])))"
  "#"
  [(set_attr "type" "fpmuldbl")
   (set_attr "length" "8")])

(define_split
  [(set (match_operand:SF 0 "register_operand" "")
	(minus:SF (match_operand:SF 3 "register_operand" "")
		  (mult:SF (match_operand:SF 1 "register_operand" "")
			   (match_operand:SF 2 "register_operand" ""))))
   (set (match_operand:SF 4 "register_operand" "")
	(mult:SF (match_dup 1) (match_dup 2)))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  [(set (match_dup 4) (mult:SF (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (minus:SF (match_dup 3)
				(mult:SF (match_dup 1) (match_dup 2))))]
  "")

(define_insn ""
  [(set (match_operand:DF 0 "register_operand" "=f")
	(neg:DF (abs:DF (match_operand:DF 1 "register_operand" "f"))))
   (set (match_operand:DF 2 "register_operand" "=&f") (abs:DF (match_dup 1)))]
  "(! TARGET_SOFT_FLOAT && TARGET_PA_20
    && ! reg_overlap_mentioned_p (operands[2], operands[1]))"
  "#"
  [(set_attr "type" "fpalu")
   (set_attr "length" "8")])

(define_split
  [(set (match_operand:DF 0 "register_operand" "")
	(neg:DF (abs:DF (match_operand:DF 1 "register_operand" ""))))
   (set (match_operand:DF 2 "register_operand" "") (abs:DF (match_dup 1)))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  [(set (match_dup 2) (abs:DF (match_dup 1)))
   (set (match_dup 0) (neg:DF (abs:DF (match_dup 1))))]
  "")

(define_insn ""
  [(set (match_operand:SF 0 "register_operand" "=f")
	(neg:SF (abs:SF (match_operand:SF 1 "register_operand" "f"))))
   (set (match_operand:SF 2 "register_operand" "=&f") (abs:SF (match_dup 1)))]
  "(! TARGET_SOFT_FLOAT && TARGET_PA_20
    && ! reg_overlap_mentioned_p (operands[2], operands[1]))"
  "#"
  [(set_attr "type" "fpalu")
   (set_attr "length" "8")])

(define_split
  [(set (match_operand:SF 0 "register_operand" "")
	(neg:SF (abs:SF (match_operand:SF 1 "register_operand" ""))))
   (set (match_operand:SF 2 "register_operand" "") (abs:SF (match_dup 1)))]
  "! TARGET_SOFT_FLOAT && TARGET_PA_20"
  [(set (match_dup 2) (abs:SF (match_dup 1)))
   (set (match_dup 0) (neg:SF (abs:SF (match_dup 1))))]
  "")

;;- Shift instructions

;; Optimized special case of shifting.

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(lshiftrt:SI (match_operand:SI 1 "memory_operand" "m")
		     (const_int 24)))]
  ""
  "ldb%M1 %1,%0"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(lshiftrt:SI (match_operand:SI 1 "memory_operand" "m")
		     (const_int 16)))]
  ""
  "ldh%M1 %1,%0"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (mult:SI (match_operand:SI 2 "register_operand" "r")
			  (match_operand:SI 3 "shadd_operand" ""))
		 (match_operand:SI 1 "register_operand" "r")))]
  ""
  "{sh%O3addl %2,%1,%0|shladd,l %2,%O3,%1,%0} "
  [(set_attr "type" "binary")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (mult:DI (match_operand:DI 2 "register_operand" "r")
			  (match_operand:DI 3 "shadd_operand" ""))
		 (match_operand:DI 1 "register_operand" "r")))]
  "TARGET_64BIT"
  "shladd,l %2,%O3,%1,%0"
  [(set_attr "type" "binary")
   (set_attr "length" "4")])

(define_expand "ashlsi3"
  [(set (match_operand:SI 0 "register_operand" "")
	(ashift:SI (match_operand:SI 1 "lhs_lshift_operand" "")
		   (match_operand:SI 2 "arith32_operand" "")))]
  ""
  "
{
  if (GET_CODE (operands[2]) != CONST_INT)
    {
      rtx temp = gen_reg_rtx (SImode);
      emit_insn (gen_subsi3 (temp, GEN_INT (31), operands[2]));
      if (GET_CODE (operands[1]) == CONST_INT)
	emit_insn (gen_zvdep_imm32 (operands[0], operands[1], temp));
      else
	emit_insn (gen_zvdep32 (operands[0], operands[1], temp));
      DONE;
    }
  /* Make sure both inputs are not constants,
     there are no patterns for that.  */
  operands[1] = force_reg (SImode, operands[1]);
}")

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ashift:SI (match_operand:SI 1 "register_operand" "r")
		   (match_operand:SI 2 "const_int_operand" "n")))]
  ""
  "{zdep|depw,z} %1,%P2,%L2,%0"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

; Match cases of op1 a CONST_INT here that zvdep_imm32 doesn't handle.
; Doing it like this makes slightly better code since reload can
; replace a register with a known value in range -16..15 with a
; constant.  Ideally, we would like to merge zvdep32 and zvdep_imm32,
; but since we have no more CONST_OK... characters, that is not
; possible.
(define_insn "zvdep32"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(ashift:SI (match_operand:SI 1 "arith5_operand" "r,L")
		   (minus:SI (const_int 31)
			     (match_operand:SI 2 "register_operand" "q,q"))))]
  ""
  "@
   {zvdep %1,32,%0|depw,z %1,%%sar,32,%0}
   {zvdepi %1,32,%0|depwi,z %1,%%sar,32,%0}"
  [(set_attr "type" "shift,shift")
   (set_attr "length" "4,4")])

(define_insn "zvdep_imm32"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ashift:SI (match_operand:SI 1 "lhs_lshift_cint_operand" "")
		   (minus:SI (const_int 31)
			     (match_operand:SI 2 "register_operand" "q"))))]
  ""
  "*
{
  int x = INTVAL (operands[1]);
  operands[2] = GEN_INT (4 + exact_log2 ((x >> 4) + 1));
  operands[1] = GEN_INT ((x & 0xf) - 0x10);
  return \"{zvdepi %1,%2,%0|depwi,z %1,%%sar,%2,%0}\";
}"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn "vdepi_ior"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ior:SI (ashift:SI (match_operand:SI 1 "const_int_operand" "")
			   (minus:SI (const_int 31)
				     (match_operand:SI 2 "register_operand" "q")))
		(match_operand:SI 3 "register_operand" "0")))]
  ; accept ...0001...1, can this be generalized?
  "exact_log2 (INTVAL (operands[1]) + 1) > 0"
  "*
{
  int x = INTVAL (operands[1]);
  operands[2] = GEN_INT (exact_log2 (x + 1));
  return \"{vdepi -1,%2,%0|depwi -1,%%sar,%2,%0}\";
}"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn "vdepi_and"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(and:SI (rotate:SI (match_operand:SI 1 "const_int_operand" "")
			   (minus:SI (const_int 31)
				     (match_operand:SI 2 "register_operand" "q")))
		(match_operand:SI 3 "register_operand" "0")))]
  ; this can be generalized...!
  "INTVAL (operands[1]) == -2"
  "*
{
  int x = INTVAL (operands[1]);
  operands[2] = GEN_INT (exact_log2 ((~x) + 1));
  return \"{vdepi 0,%2,%0|depwi 0,%%sar,%2,%0}\";
}"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_expand "ashldi3"
  [(set (match_operand:DI 0 "register_operand" "")
	(ashift:DI (match_operand:DI 1 "lhs_lshift_operand" "")
		   (match_operand:DI 2 "arith32_operand" "")))]
  "TARGET_64BIT"
  "
{
  if (GET_CODE (operands[2]) != CONST_INT)
    {
      rtx temp = gen_reg_rtx (DImode);
      emit_insn (gen_subdi3 (temp, GEN_INT (63), operands[2]));
      if (GET_CODE (operands[1]) == CONST_INT)
	emit_insn (gen_zvdep_imm64 (operands[0], operands[1], temp));
      else
	emit_insn (gen_zvdep64 (operands[0], operands[1], temp));
      DONE;
    }
  /* Make sure both inputs are not constants,
     there are no patterns for that.  */
  operands[1] = force_reg (DImode, operands[1]);
}")

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI (match_operand:DI 1 "register_operand" "r")
		   (match_operand:DI 2 "const_int_operand" "n")))]
  "TARGET_64BIT"
  "depd,z %1,%p2,%Q2,%0"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

; Match cases of op1 a CONST_INT here that zvdep_imm64 doesn't handle.
; Doing it like this makes slightly better code since reload can
; replace a register with a known value in range -16..15 with a
; constant.  Ideally, we would like to merge zvdep64 and zvdep_imm64,
; but since we have no more CONST_OK... characters, that is not
; possible.
(define_insn "zvdep64"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(ashift:DI (match_operand:DI 1 "arith5_operand" "r,L")
		   (minus:DI (const_int 63)
			     (match_operand:DI 2 "register_operand" "q,q"))))]
  "TARGET_64BIT"
  "@
   depd,z %1,%%sar,64,%0
   depdi,z %1,%%sar,64,%0"
  [(set_attr "type" "shift,shift")
   (set_attr "length" "4,4")])

(define_insn "zvdep_imm64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI (match_operand:DI 1 "lhs_lshift_cint_operand" "")
		   (minus:DI (const_int 63)
			     (match_operand:DI 2 "register_operand" "q"))))]
  "TARGET_64BIT"
  "*
{
  int x = INTVAL (operands[1]);
  operands[2] = GEN_INT (4 + exact_log2 ((x >> 4) + 1));
  operands[1] = GEN_INT ((x & 0x1f) - 0x20);
  return \"depdi,z %1,%%sar,%2,%0\";
}"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ior:DI (ashift:DI (match_operand:DI 1 "const_int_operand" "")
			   (minus:DI (const_int 63)
				     (match_operand:DI 2 "register_operand" "q")))
		(match_operand:DI 3 "register_operand" "0")))]
  ; accept ...0001...1, can this be generalized?
  "TARGET_64BIT && exact_log2 (INTVAL (operands[1]) + 1) > 0"
  "*
{
  int x = INTVAL (operands[1]);
  operands[2] = GEN_INT (exact_log2 (x + 1));
  return \"depdi -1,%%sar,%2,%0\";
}"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(and:DI (rotate:DI (match_operand:DI 1 "const_int_operand" "")
			   (minus:DI (const_int 63)
				     (match_operand:DI 2 "register_operand" "q")))
		(match_operand:DI 3 "register_operand" "0")))]
  ; this can be generalized...!
  "TARGET_64BIT && INTVAL (operands[1]) == -2"
  "*
{
  int x = INTVAL (operands[1]);
  operands[2] = GEN_INT (exact_log2 ((~x) + 1));
  return \"depdi 0,%%sar,%2,%0\";
}"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_expand "ashrsi3"
  [(set (match_operand:SI 0 "register_operand" "")
	(ashiftrt:SI (match_operand:SI 1 "register_operand" "")
		     (match_operand:SI 2 "arith32_operand" "")))]
  ""
  "
{
  if (GET_CODE (operands[2]) != CONST_INT)
    {
      rtx temp = gen_reg_rtx (SImode);
      emit_insn (gen_subsi3 (temp, GEN_INT (31), operands[2]));
      emit_insn (gen_vextrs32 (operands[0], operands[1], temp));
      DONE;
    }
}")

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ashiftrt:SI (match_operand:SI 1 "register_operand" "r")
		     (match_operand:SI 2 "const_int_operand" "n")))]
  ""
  "{extrs|extrw,s} %1,%P2,%L2,%0"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn "vextrs32"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ashiftrt:SI (match_operand:SI 1 "register_operand" "r")
		     (minus:SI (const_int 31)
			       (match_operand:SI 2 "register_operand" "q"))))]
  ""
  "{vextrs %1,32,%0|extrw,s %1,%%sar,32,%0}"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_expand "ashrdi3"
  [(set (match_operand:DI 0 "register_operand" "")
	(ashiftrt:DI (match_operand:DI 1 "register_operand" "")
		     (match_operand:DI 2 "arith32_operand" "")))]
  "TARGET_64BIT"
  "
{
  if (GET_CODE (operands[2]) != CONST_INT)
    {
      rtx temp = gen_reg_rtx (DImode);
      emit_insn (gen_subdi3 (temp, GEN_INT (63), operands[2]));
      emit_insn (gen_vextrs64 (operands[0], operands[1], temp));
      DONE;
    }
}")

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashiftrt:DI (match_operand:DI 1 "register_operand" "r")
		     (match_operand:DI 2 "const_int_operand" "n")))]
  "TARGET_64BIT"
  "extrd,s %1,%p2,%Q2,%0"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn "vextrs64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashiftrt:DI (match_operand:DI 1 "register_operand" "r")
		     (minus:DI (const_int 63)
			       (match_operand:DI 2 "register_operand" "q"))))]
  "TARGET_64BIT"
  "extrd,s %1,%%sar,64,%0"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn "lshrsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(lshiftrt:SI (match_operand:SI 1 "register_operand" "r,r")
		     (match_operand:SI 2 "arith32_operand" "q,n")))]
  ""
  "@
   {vshd %%r0,%1,%0|shrpw %%r0,%1,%%sar,%0}
   {extru|extrw,u} %1,%P2,%L2,%0"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn "lshrdi3"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(lshiftrt:DI (match_operand:DI 1 "register_operand" "r,r")
		     (match_operand:DI 2 "arith32_operand" "q,n")))]
  "TARGET_64BIT"
  "@
   shrpd %%r0,%1,%%sar,%0
   extrd,u %1,%p2,%Q2,%0"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn "rotrsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(rotatert:SI (match_operand:SI 1 "register_operand" "r,r")
		     (match_operand:SI 2 "arith32_operand" "q,n")))]
  ""
  "*
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      operands[2] = GEN_INT (INTVAL (operands[2]) & 31);
      return \"{shd|shrpw} %1,%1,%2,%0\";
    }
  else
    return \"{vshd %1,%1,%0|shrpw %1,%1,%%sar,%0}\";
}"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_expand "rotlsi3"
  [(set (match_operand:SI 0 "register_operand" "")
        (rotate:SI (match_operand:SI 1 "register_operand" "")
                   (match_operand:SI 2 "arith32_operand" "")))]
  ""
  "
{
  if (GET_CODE (operands[2]) != CONST_INT)
    {
      rtx temp = gen_reg_rtx (SImode);
      emit_insn (gen_subsi3 (temp, GEN_INT (32), operands[2]));
      emit_insn (gen_rotrsi3 (operands[0], operands[1], temp));
      DONE;
    }
  /* Else expand normally.  */
}")

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
        (rotate:SI (match_operand:SI 1 "register_operand" "r")
                   (match_operand:SI 2 "const_int_operand" "n")))]
  ""
  "*
{
  operands[2] = GEN_INT ((32 - INTVAL (operands[2])) & 31);
  return \"{shd|shrpw} %1,%1,%2,%0\";
}"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(match_operator:SI 5 "plus_xor_ior_operator"
	  [(ashift:SI (match_operand:SI 1 "register_operand" "r")
		      (match_operand:SI 3 "const_int_operand" "n"))
	   (lshiftrt:SI (match_operand:SI 2 "register_operand" "r")
			(match_operand:SI 4 "const_int_operand" "n"))]))]
  "INTVAL (operands[3]) + INTVAL (operands[4]) == 32"
  "{shd|shrpw} %1,%2,%4,%0"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(match_operator:SI 5 "plus_xor_ior_operator"
	  [(lshiftrt:SI (match_operand:SI 2 "register_operand" "r")
			(match_operand:SI 4 "const_int_operand" "n"))
	   (ashift:SI (match_operand:SI 1 "register_operand" "r")
		      (match_operand:SI 3 "const_int_operand" "n"))]))]
  "INTVAL (operands[3]) + INTVAL (operands[4]) == 32"
  "{shd|shrpw} %1,%2,%4,%0"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(and:SI (ashift:SI (match_operand:SI 1 "register_operand" "r")
			   (match_operand:SI 2 "const_int_operand" ""))
		(match_operand:SI 3 "const_int_operand" "")))]
  "exact_log2 (1 + (INTVAL (operands[3]) >> (INTVAL (operands[2]) & 31))) > 0"
  "*
{
  int cnt = INTVAL (operands[2]) & 31;
  operands[3] = GEN_INT (exact_log2 (1 + (INTVAL (operands[3]) >> cnt)));
  operands[2] = GEN_INT (31 - cnt);
  return \"{zdep|depw,z} %1,%2,%3,%0\";
}"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

;; Unconditional and other jump instructions.

;; This can only be used in a leaf function, so we do
;; not need to use the PIC register when generating PIC code.
(define_insn "return"
  [(return)
   (use (reg:SI 2))
   (const_int 0)]
  "hppa_can_use_return_insn_p ()"
  "*
{
  if (TARGET_PA_20)
    return \"bve%* (%%r2)\";
  return \"bv%* %%r0(%%r2)\";
}"
  [(set_attr "type" "branch")
   (set_attr "length" "4")])

;; Emit a different pattern for functions which have non-trivial
;; epilogues so as not to confuse jump and reorg.
(define_insn "return_internal"
  [(return)
   (use (reg:SI 2))
   (const_int 1)]
  ""
  "*
{
  if (TARGET_PA_20)
    return \"bve%* (%%r2)\";
  return \"bv%* %%r0(%%r2)\";
}"
  [(set_attr "type" "branch")
   (set_attr "length" "4")])

;; This is used for eh returns which bypass the return stub.
(define_insn "return_external_pic"
  [(return)
   (clobber (reg:SI 1))
   (use (reg:SI 2))]
  "!TARGET_NO_SPACE_REGS
   && !TARGET_PA_20
   && flag_pic && current_function_calls_eh_return"
  "ldsid (%%sr0,%%r2),%%r1\;mtsp %%r1,%%sr0\;be%* 0(%%sr0,%%r2)"
  [(set_attr "type" "branch")
   (set_attr "length" "12")])

(define_expand "prologue"
  [(const_int 0)]
  ""
  "hppa_expand_prologue ();DONE;")

(define_expand "sibcall_epilogue"
  [(return)]
  ""
  "
{
  hppa_expand_epilogue ();
  DONE;
}")

(define_expand "epilogue"
  [(return)]
  ""
  "
{
  /* Try to use the trivial return first.  Else use the full
     epilogue.  */
  if (hppa_can_use_return_insn_p ())
    emit_jump_insn (gen_return ());
  else
    {
      rtx x;

      hppa_expand_epilogue ();

      /* EH returns bypass the normal return stub.  Thus, we must do an
	 interspace branch to return from functions that call eh_return.
	 This is only a problem for returns from shared code on ports
	 using space registers.  */
      if (!TARGET_NO_SPACE_REGS
	  && !TARGET_PA_20
	  && flag_pic && current_function_calls_eh_return)
	x = gen_return_external_pic ();
      else
	x = gen_return_internal ();

      emit_jump_insn (x);
    }
  DONE;
}")

; Used by hppa_profile_hook to load the starting address of the current
; function; operand 1 contains the address of the label in operand 3
(define_insn "load_offset_label_address"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (plus:SI (match_operand:SI 1 "register_operand" "r")
		 (minus:SI (match_operand:SI 2 "" "")
			   (label_ref:SI (match_operand 3 "" "")))))]
  ""
  "ldo %2-%l3(%1),%0"
  [(set_attr "type" "multi")
   (set_attr "length" "4")])

; Output a code label and load its address.
(define_insn "lcla1"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (label_ref:SI (match_operand 1 "" "")))
   (const_int 0)]
  "!TARGET_PA_20"
  "*
{
  output_asm_insn (\"bl .+8,%0\;depi 0,31,2,%0\", operands);
  (*targetm.asm_out.internal_label) (asm_out_file, \"L\",
                                     CODE_LABEL_NUMBER (operands[1]));
  return \"\";
}"
  [(set_attr "type" "multi")
   (set_attr "length" "8")])

(define_insn "lcla2"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (label_ref:SI (match_operand 1 "" "")))
   (const_int 0)]
  "TARGET_PA_20"
  "*
{
  (*targetm.asm_out.internal_label) (asm_out_file, \"L\",
                                     CODE_LABEL_NUMBER (operands[1]));
  return \"mfia %0\";
}"
  [(set_attr "type" "move")
   (set_attr "length" "4")])

(define_insn "blockage"
  [(unspec_volatile [(const_int 2)] UNSPECV_BLOCKAGE)]
  ""
  ""
  [(set_attr "length" "0")])

(define_insn "jump"
  [(set (pc) (label_ref (match_operand 0 "" "")))]
  ""
  "*
{
  /* An unconditional branch which can reach its target.  */
  if (get_attr_length (insn) < 16)
    return \"b%* %l0\";

  return output_lbranch (operands[0], insn, 1);
}"
  [(set_attr "type" "uncond_branch")
   (set_attr "pa_combine_type" "uncond_branch")
   (set (attr "length")
    (cond [(eq (symbol_ref "jump_in_call_delay (insn)") (const_int 1))
	   (if_then_else (lt (abs (minus (match_dup 0)
					 (plus (pc) (const_int 8))))
			     (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (const_int 8))
	   (lt (abs (minus (match_dup 0) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 4)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 20)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 16)]
	  (const_int 24)))])

;;; Hope this is only within a function...
(define_insn "indirect_jump"
  [(set (pc) (match_operand 0 "register_operand" "r"))]
  "GET_MODE (operands[0]) == word_mode"
  "bv%* %%r0(%0)"
  [(set_attr "type" "branch")
   (set_attr "length" "4")])

;;; An indirect jump can be optimized to a direct jump.  GAS for the
;;; SOM target doesn't allow branching to a label inside a function.
;;; We also don't correctly compute branch distances for labels
;;; outside the current function.  Thus, we use an indirect jump can't
;;; be optimized to a direct jump for all targets.  We assume that
;;; the branch target is in the same space (i.e., nested function
;;; jumping to a label in an outer function in the same translation
;;; unit).
(define_expand "nonlocal_goto"
  [(use (match_operand 0 "general_operand" ""))
   (use (match_operand 1 "general_operand" ""))
   (use (match_operand 2 "general_operand" ""))
   (use (match_operand 3 "general_operand" ""))]
  ""
{
  rtx lab = operands[1];
  rtx stack = operands[2];
  rtx fp = operands[3];

  lab = copy_to_reg (lab);

  emit_insn (gen_rtx_CLOBBER (VOIDmode,
			      gen_rtx_MEM (BLKmode,
					   gen_rtx_SCRATCH (VOIDmode))));
  emit_insn (gen_rtx_CLOBBER (VOIDmode,
			      gen_rtx_MEM (BLKmode,
					   hard_frame_pointer_rtx)));

  /* Restore the frame pointer.  The virtual_stack_vars_rtx is saved
     instead of the hard_frame_pointer_rtx in the save area.  As a
     result, an extra instruction is needed to adjust for the offset
     of the virtual stack variables and the frame pointer.  */
  if (GET_CODE (fp) != REG)
    fp = force_reg (Pmode, fp);
  emit_move_insn (virtual_stack_vars_rtx, fp);

  emit_stack_restore (SAVE_NONLOCAL, stack, NULL_RTX);

  emit_insn (gen_rtx_USE (VOIDmode, hard_frame_pointer_rtx));
  emit_insn (gen_rtx_USE (VOIDmode, stack_pointer_rtx));

  /* Nonlocal goto jumps are only used between functions in the same
     translation unit.  Thus, we can avoid the extra overhead of an
     interspace jump.  */
  emit_jump_insn (gen_indirect_goto (lab));
  emit_barrier ();
  DONE;
})

(define_insn "indirect_goto"
  [(unspec [(match_operand 0 "register_operand" "=r")] UNSPEC_GOTO)]
  "GET_MODE (operands[0]) == word_mode"
  "bv%* %%r0(%0)"
  [(set_attr "type" "branch")
   (set_attr "length" "4")])

;;; This jump is used in branch tables where the insn length is fixed.
;;; The length of this insn is adjusted if the delay slot is not filled.
(define_insn "short_jump"
  [(set (pc) (label_ref (match_operand 0 "" "")))
   (const_int 0)]
  ""
  "b%* %l0%#"
  [(set_attr "type" "btable_branch")
   (set_attr "length" "4")])

;; Subroutines of "casesi".
;; operand 0 is index
;; operand 1 is the minimum bound
;; operand 2 is the maximum bound - minimum bound + 1
;; operand 3 is CODE_LABEL for the table;
;; operand 4 is the CODE_LABEL to go to if index out of range.

(define_expand "casesi"
  [(match_operand:SI 0 "general_operand" "")
   (match_operand:SI 1 "const_int_operand" "")
   (match_operand:SI 2 "const_int_operand" "")
   (match_operand 3 "" "")
   (match_operand 4 "" "")]
  ""
  "
{
  if (GET_CODE (operands[0]) != REG)
    operands[0] = force_reg (SImode, operands[0]);

  if (operands[1] != const0_rtx)
    {
      rtx index = gen_reg_rtx (SImode);

      operands[1] = GEN_INT (-INTVAL (operands[1]));
      if (!INT_14_BITS (operands[1]))
	operands[1] = force_reg (SImode, operands[1]);
      emit_insn (gen_addsi3 (index, operands[0], operands[1]));
      operands[0] = index;
    }

  /* In 64bit mode we must make sure to wipe the upper bits of the register
     just in case the addition overflowed or we had random bits in the
     high part of the register.  */
  if (TARGET_64BIT)
    {
      rtx index = gen_reg_rtx (DImode);

      emit_insn (gen_extendsidi2 (index, operands[0]));
      operands[0] = gen_rtx_SUBREG (SImode, index, 4);
    }

  if (!INT_5_BITS (operands[2]))
    operands[2] = force_reg (SImode, operands[2]);

  /* This branch prevents us finding an insn for the delay slot of the
     following vectored branch.  It might be possible to use the delay
     slot if an index value of -1 was used to transfer to the out-of-range
     label.  In order to do this, we would have to output the -1 vector
     element after the delay insn.  The casesi output code would have to
     check if the casesi insn is in a delay branch sequence and output
     the delay insn if one is found.  If this was done, then it might
     then be worthwhile to split the casesi patterns to improve scheduling.
     However, it's not clear that all this extra complexity is worth
     the effort.  */
  emit_insn (gen_cmpsi (operands[0], operands[2]));
  emit_jump_insn (gen_bgtu (operands[4]));

  if (TARGET_BIG_SWITCH)
    {
      if (TARGET_64BIT)
	{
          rtx tmp1 = gen_reg_rtx (DImode);
          rtx tmp2 = gen_reg_rtx (DImode);

          emit_jump_insn (gen_casesi64p (operands[0], operands[3],
                                         tmp1, tmp2));
	}
      else
	{
	  rtx tmp1 = gen_reg_rtx (SImode);

	  if (flag_pic)
	    {
	      rtx tmp2 = gen_reg_rtx (SImode);

	      emit_jump_insn (gen_casesi32p (operands[0], operands[3],
					     tmp1, tmp2));
	    }
	  else
	    emit_jump_insn (gen_casesi32 (operands[0], operands[3], tmp1));
	}
    }
  else
    emit_jump_insn (gen_casesi0 (operands[0], operands[3]));
  DONE;
}")

;;; The rtl for this pattern doesn't accurately describe what the insn
;;; actually does, particularly when case-vector elements are exploded
;;; in pa_reorg.  However, the initial SET in these patterns must show
;;; the connection of the insn to the following jump table.
(define_insn "casesi0"
  [(set (pc) (mem:SI (plus:SI
		       (mult:SI (match_operand:SI 0 "register_operand" "r")
				(const_int 4))
		       (label_ref (match_operand 1 "" "")))))]
  ""
  "blr,n %0,%%r0\;nop"
  [(set_attr "type" "multi")
   (set_attr "length" "8")])

;;; 32-bit code, absolute branch table.
(define_insn "casesi32"
  [(set (pc) (mem:SI (plus:SI
		       (mult:SI (match_operand:SI 0 "register_operand" "r")
				(const_int 4))
		       (label_ref (match_operand 1 "" "")))))
   (clobber (match_operand:SI 2 "register_operand" "=&r"))]
  "!TARGET_64BIT && TARGET_BIG_SWITCH"
  "ldil L'%l1,%2\;ldo R'%l1(%2),%2\;{ldwx|ldw},s %0(%2),%2\;bv,n %%r0(%2)"
  [(set_attr "type" "multi")
   (set_attr "length" "16")])

;;; 32-bit code, relative branch table.
(define_insn "casesi32p"
  [(set (pc) (mem:SI (plus:SI
		       (mult:SI (match_operand:SI 0 "register_operand" "r")
				(const_int 4))
		       (label_ref (match_operand 1 "" "")))))
   (clobber (match_operand:SI 2 "register_operand" "=&a"))
   (clobber (match_operand:SI 3 "register_operand" "=&r"))]
  "!TARGET_64BIT && TARGET_BIG_SWITCH"
  "{bl .+8,%2\;depi 0,31,2,%2|mfia %2}\;ldo {16|20}(%2),%2\;\
{ldwx|ldw},s %0(%2),%3\;{addl|add,l} %2,%3,%3\;bv,n %%r0(%3)"
  [(set_attr "type" "multi")
   (set (attr "length")
     (if_then_else (ne (symbol_ref "TARGET_PA_20") (const_int 0))
	(const_int 20)
	(const_int 24)))])

;;; 64-bit code, 32-bit relative branch table.
(define_insn "casesi64p"
  [(set (pc) (mem:DI (plus:DI
		       (mult:DI (sign_extend:DI
				  (match_operand:SI 0 "register_operand" "r"))
				(const_int 8))
		       (label_ref (match_operand 1 "" "")))))
   (clobber (match_operand:DI 2 "register_operand" "=&r"))
   (clobber (match_operand:DI 3 "register_operand" "=&r"))]
  "TARGET_64BIT && TARGET_BIG_SWITCH"
  "mfia %2\;ldo 24(%2),%2\;ldw,s %0(%2),%3\;extrd,s %3,63,32,%3\;\
add,l %2,%3,%3\;bv,n %%r0(%3)"
  [(set_attr "type" "multi")
   (set_attr "length" "24")])


;; Call patterns.
;;- jump to subroutine

(define_expand "call"
  [(parallel [(call (match_operand:SI 0 "" "")
		    (match_operand 1 "" ""))
	      (clobber (reg:SI 2))])]
  ""
  "
{
  rtx op, call_insn;
  rtx nb = operands[1];

  if (TARGET_PORTABLE_RUNTIME)
    op = force_reg (SImode, XEXP (operands[0], 0));
  else
    op = XEXP (operands[0], 0);

  if (TARGET_64BIT)
    {
      if (!virtuals_instantiated)
	emit_move_insn (arg_pointer_rtx,
			gen_rtx_PLUS (word_mode, virtual_outgoing_args_rtx,
				      GEN_INT (64)));
      else
	{
	  /* The loop pass can generate new libcalls after the virtual
	     registers are instantiated when fpregs are disabled because
	     the only method that we have for doing DImode multiplication
	     is with a libcall.  This could be trouble if we haven't
	     allocated enough space for the outgoing arguments.  */
	  gcc_assert (INTVAL (nb) <= current_function_outgoing_args_size);

	  emit_move_insn (arg_pointer_rtx,
			  gen_rtx_PLUS (word_mode, stack_pointer_rtx,
					GEN_INT (STACK_POINTER_OFFSET + 64)));
	}
    }

  /* Use two different patterns for calls to explicitly named functions
     and calls through function pointers.  This is necessary as these two
     types of calls use different calling conventions, and CSE might try
     to change the named call into an indirect call in some cases (using
     two patterns keeps CSE from performing this optimization).
     
     We now use even more call patterns as there was a subtle bug in
     attempting to restore the pic register after a call using a simple
     move insn.  During reload, a instruction involving a pseudo register
     with no explicit dependence on the PIC register can be converted
     to an equivalent load from memory using the PIC register.  If we
     emit a simple move to restore the PIC register in the initial rtl
     generation, then it can potentially be repositioned during scheduling.
     and an instruction that eventually uses the PIC register may end up
     between the call and the PIC register restore.
     
     This only worked because there is a post call group of instructions
     that are scheduled with the call.  These instructions are included
     in the same basic block as the call.  However, calls can throw in
     C++ code and a basic block has to terminate at the call if the call
     can throw.  This results in the PIC register restore being scheduled
     independently from the call.  So, we now hide the save and restore
     of the PIC register in the call pattern until after reload.  Then,
     we split the moves out.  A small side benefit is that we now don't
     need to have a use of the PIC register in the return pattern and
     the final save/restore operation is not needed.
     
     I elected to just clobber %r4 in the PIC patterns and use it instead
     of trying to force hppa_pic_save_rtx () to a callee saved register.
     This might have required a new register class and constraint.  It
     was also simpler to just handle the restore from a register than a
     generic pseudo.  */
  if (TARGET_64BIT)
    {
      if (GET_CODE (op) == SYMBOL_REF)
	call_insn = emit_call_insn (gen_call_symref_64bit (op, nb));
      else
	{
	  op = force_reg (word_mode, op);
	  call_insn = emit_call_insn (gen_call_reg_64bit (op, nb));
	}
    }
  else
    {
      if (GET_CODE (op) == SYMBOL_REF)
	{
	  if (flag_pic)
	    call_insn = emit_call_insn (gen_call_symref_pic (op, nb));
	  else
	    call_insn = emit_call_insn (gen_call_symref (op, nb));
	}
      else
	{
	  rtx tmpreg = gen_rtx_REG (word_mode, 22);

	  emit_move_insn (tmpreg, force_reg (word_mode, op));
	  if (flag_pic)
	    call_insn = emit_call_insn (gen_call_reg_pic (nb));
	  else
	    call_insn = emit_call_insn (gen_call_reg (nb));
	}
    }

  DONE;
}")

;; We use function calls to set the attribute length of calls and millicode
;; calls.  This is necessary because of the large variety of call sequences.
;; Implementing the calculation in rtl is difficult as well as ugly.  As
;; we need the same calculation in several places, maintenance becomes a
;; nightmare.
;;
;; However, this has a subtle impact on branch shortening.  When the
;; expression used to set the length attribute of an instruction depends
;; on a relative address (e.g., pc or a branch address), genattrtab
;; notes that the insn's length is variable, and attempts to determine a
;; worst-case default length and code to compute an insn's current length.

;; The use of a function call hides the variable dependence of our calls
;; and millicode calls.  The result is genattrtab doesn't treat the operation
;; as variable and it only generates code for the default case using our
;; function call.  Because of this, calls and millicode calls have a fixed
;; length in the branch shortening pass, and some branches will use a longer
;; code sequence than necessary.  However, the length of any given call
;; will still reflect its final code location and it may be shorter than
;; the initial length estimate.

;; It's possible to trick genattrtab by adding an expression involving `pc'
;; in the set.  However, when genattrtab hits a function call in its attempt
;; to compute the default length, it marks the result as unknown and sets
;; the default result to MAX_INT ;-(  One possible fix that would allow
;; calls to participate in branch shortening would be to make the call to
;; insn_default_length a target option.  Then, we could massage unknown
;; results.  Another fix might be to change genattrtab so that it just does
;; the call in the variable case as it already does for the fixed case.

(define_insn "call_symref"
  [(call (mem:SI (match_operand 0 "call_operand_address" ""))
	 (match_operand 1 "" "i"))
   (clobber (reg:SI 1))
   (clobber (reg:SI 2))
   (use (const_int 0))]
  "!TARGET_PORTABLE_RUNTIME && !TARGET_64BIT"
  "*
{
  output_arg_descriptor (insn);
  return output_call (insn, operands[0], 0);
}"
  [(set_attr "type" "call")
   (set (attr "length") (symbol_ref "attr_length_call (insn, 0)"))])

(define_insn "call_symref_pic"
  [(call (mem:SI (match_operand 0 "call_operand_address" ""))
	 (match_operand 1 "" "i"))
   (clobber (reg:SI 1))
   (clobber (reg:SI 2))
   (clobber (reg:SI 4))
   (use (reg:SI 19))
   (use (const_int 0))]
  "!TARGET_PORTABLE_RUNTIME && !TARGET_64BIT"
  "*
{
  output_arg_descriptor (insn);
  return output_call (insn, operands[0], 0);
}"
  [(set_attr "type" "call")
   (set (attr "length")
	(plus (symbol_ref "attr_length_call (insn, 0)")
	      (symbol_ref "attr_length_save_restore_dltp (insn)")))])

;; Split out the PIC register save and restore after reload.  This is
;; done only if the function returns.  As the split is done after reload,
;; there are some situations in which we unnecessarily save and restore
;; %r4.  This happens when there is a single call and the PIC register
;; is "dead" after the call.  This isn't easy to fix as the usage of
;; the PIC register isn't completely determined until the reload pass.
(define_split
  [(parallel [(call (mem:SI (match_operand 0 "call_operand_address" ""))
		    (match_operand 1 "" ""))
	      (clobber (reg:SI 1))
	      (clobber (reg:SI 2))
	      (clobber (reg:SI 4))
	      (use (reg:SI 19))
	      (use (const_int 0))])]
  "!TARGET_PORTABLE_RUNTIME && !TARGET_64BIT
   && reload_completed
   && !find_reg_note (insn, REG_NORETURN, NULL_RTX)"
  [(set (reg:SI 4) (reg:SI 19))
   (parallel [(call (mem:SI (match_dup 0))
		    (match_dup 1))
	      (clobber (reg:SI 1))
	      (clobber (reg:SI 2))
	      (use (reg:SI 19))
	      (use (const_int 0))])
   (set (reg:SI 19) (reg:SI 4))]
  "")

;; Remove the clobber of register 4 when optimizing.  This has to be
;; done with a peephole optimization rather than a split because the
;; split sequence for a call must be longer than one instruction.
(define_peephole2
  [(parallel [(call (mem:SI (match_operand 0 "call_operand_address" ""))
		    (match_operand 1 "" ""))
	      (clobber (reg:SI 1))
	      (clobber (reg:SI 2))
	      (clobber (reg:SI 4))
	      (use (reg:SI 19))
	      (use (const_int 0))])]
  "!TARGET_PORTABLE_RUNTIME && !TARGET_64BIT && reload_completed"
  [(parallel [(call (mem:SI (match_dup 0))
		    (match_dup 1))
	      (clobber (reg:SI 1))
	      (clobber (reg:SI 2))
	      (use (reg:SI 19))
	      (use (const_int 0))])]
  "")

(define_insn "*call_symref_pic_post_reload"
  [(call (mem:SI (match_operand 0 "call_operand_address" ""))
	 (match_operand 1 "" "i"))
   (clobber (reg:SI 1))
   (clobber (reg:SI 2))
   (use (reg:SI 19))
   (use (const_int 0))]
  "!TARGET_PORTABLE_RUNTIME && !TARGET_64BIT"
  "*
{
  output_arg_descriptor (insn);
  return output_call (insn, operands[0], 0);
}"
  [(set_attr "type" "call")
   (set (attr "length") (symbol_ref "attr_length_call (insn, 0)"))])

;; This pattern is split if it is necessary to save and restore the
;; PIC register.
(define_insn "call_symref_64bit"
  [(call (mem:SI (match_operand 0 "call_operand_address" ""))
	 (match_operand 1 "" "i"))
   (clobber (reg:DI 1))
   (clobber (reg:DI 2))
   (clobber (reg:DI 4))
   (use (reg:DI 27))
   (use (reg:DI 29))
   (use (const_int 0))]
  "TARGET_64BIT"
  "*
{
  output_arg_descriptor (insn);
  return output_call (insn, operands[0], 0);
}"
  [(set_attr "type" "call")
   (set (attr "length")
	(plus (symbol_ref "attr_length_call (insn, 0)")
	      (symbol_ref "attr_length_save_restore_dltp (insn)")))])

;; Split out the PIC register save and restore after reload.  This is
;; done only if the function returns.  As the split is done after reload,
;; there are some situations in which we unnecessarily save and restore
;; %r4.  This happens when there is a single call and the PIC register
;; is "dead" after the call.  This isn't easy to fix as the usage of
;; the PIC register isn't completely determined until the reload pass.
(define_split
  [(parallel [(call (mem:SI (match_operand 0 "call_operand_address" ""))
		    (match_operand 1 "" ""))
	      (clobber (reg:DI 1))
	      (clobber (reg:DI 2))
	      (clobber (reg:DI 4))
	      (use (reg:DI 27))
	      (use (reg:DI 29))
	      (use (const_int 0))])]
  "TARGET_64BIT
   && reload_completed
   && !find_reg_note (insn, REG_NORETURN, NULL_RTX)"
  [(set (reg:DI 4) (reg:DI 27))
   (parallel [(call (mem:SI (match_dup 0))
		    (match_dup 1))
	      (clobber (reg:DI 1))
	      (clobber (reg:DI 2))
	      (use (reg:DI 27))
	      (use (reg:DI 29))
	      (use (const_int 0))])
   (set (reg:DI 27) (reg:DI 4))]
  "")

;; Remove the clobber of register 4 when optimizing.  This has to be
;; done with a peephole optimization rather than a split because the
;; split sequence for a call must be longer than one instruction.
(define_peephole2
  [(parallel [(call (mem:SI (match_operand 0 "call_operand_address" ""))
		    (match_operand 1 "" ""))
	      (clobber (reg:DI 1))
	      (clobber (reg:DI 2))
	      (clobber (reg:DI 4))
	      (use (reg:DI 27))
	      (use (reg:DI 29))
	      (use (const_int 0))])]
  "TARGET_64BIT && reload_completed"
  [(parallel [(call (mem:SI (match_dup 0))
		    (match_dup 1))
	      (clobber (reg:DI 1))
	      (clobber (reg:DI 2))
	      (use (reg:DI 27))
	      (use (reg:DI 29))
	      (use (const_int 0))])]
  "")

(define_insn "*call_symref_64bit_post_reload"
  [(call (mem:SI (match_operand 0 "call_operand_address" ""))
	 (match_operand 1 "" "i"))
   (clobber (reg:DI 1))
   (clobber (reg:DI 2))
   (use (reg:DI 27))
   (use (reg:DI 29))
   (use (const_int 0))]
  "TARGET_64BIT"
  "*
{
  output_arg_descriptor (insn);
  return output_call (insn, operands[0], 0);
}"
  [(set_attr "type" "call")
   (set (attr "length") (symbol_ref "attr_length_call (insn, 0)"))])

(define_insn "call_reg"
  [(call (mem:SI (reg:SI 22))
	 (match_operand 0 "" "i"))
   (clobber (reg:SI 1))
   (clobber (reg:SI 2))
   (use (const_int 1))]
  "!TARGET_64BIT"
  "*
{
  return output_indirect_call (insn, gen_rtx_REG (word_mode, 22));
}"
  [(set_attr "type" "dyncall")
   (set (attr "length") (symbol_ref "attr_length_indirect_call (insn)"))])

;; This pattern is split if it is necessary to save and restore the
;; PIC register.
(define_insn "call_reg_pic"
  [(call (mem:SI (reg:SI 22))
	 (match_operand 0 "" "i"))
   (clobber (reg:SI 1))
   (clobber (reg:SI 2))
   (clobber (reg:SI 4))
   (use (reg:SI 19))
   (use (const_int 1))]
  "!TARGET_64BIT"
  "*
{
  return output_indirect_call (insn, gen_rtx_REG (word_mode, 22));
}"
  [(set_attr "type" "dyncall")
   (set (attr "length")
	(plus (symbol_ref "attr_length_indirect_call (insn)")
	      (symbol_ref "attr_length_save_restore_dltp (insn)")))])

;; Split out the PIC register save and restore after reload.  This is
;; done only if the function returns.  As the split is done after reload,
;; there are some situations in which we unnecessarily save and restore
;; %r4.  This happens when there is a single call and the PIC register
;; is "dead" after the call.  This isn't easy to fix as the usage of
;; the PIC register isn't completely determined until the reload pass.
(define_split
  [(parallel [(call (mem:SI (reg:SI 22))
		    (match_operand 0 "" ""))
	      (clobber (reg:SI 1))
	      (clobber (reg:SI 2))
	      (clobber (reg:SI 4))
	      (use (reg:SI 19))
	      (use (const_int 1))])]
  "!TARGET_64BIT
   && reload_completed
   && !find_reg_note (insn, REG_NORETURN, NULL_RTX)"
  [(set (reg:SI 4) (reg:SI 19))
   (parallel [(call (mem:SI (reg:SI 22))
		    (match_dup 0))
	      (clobber (reg:SI 1))
	      (clobber (reg:SI 2))
	      (use (reg:SI 19))
	      (use (const_int 1))])
   (set (reg:SI 19) (reg:SI 4))]
  "")

;; Remove the clobber of register 4 when optimizing.  This has to be
;; done with a peephole optimization rather than a split because the
;; split sequence for a call must be longer than one instruction.
(define_peephole2
  [(parallel [(call (mem:SI (reg:SI 22))
		    (match_operand 0 "" ""))
	      (clobber (reg:SI 1))
	      (clobber (reg:SI 2))
	      (clobber (reg:SI 4))
	      (use (reg:SI 19))
	      (use (const_int 1))])]
  "!TARGET_64BIT && reload_completed"
  [(parallel [(call (mem:SI (reg:SI 22))
		    (match_dup 0))
	      (clobber (reg:SI 1))
	      (clobber (reg:SI 2))
	      (use (reg:SI 19))
	      (use (const_int 1))])]
  "")

(define_insn "*call_reg_pic_post_reload"
  [(call (mem:SI (reg:SI 22))
	 (match_operand 0 "" "i"))
   (clobber (reg:SI 1))
   (clobber (reg:SI 2))
   (use (reg:SI 19))
   (use (const_int 1))]
  "!TARGET_64BIT"
  "*
{
  return output_indirect_call (insn, gen_rtx_REG (word_mode, 22));
}"
  [(set_attr "type" "dyncall")
   (set (attr "length") (symbol_ref "attr_length_indirect_call (insn)"))])

;; This pattern is split if it is necessary to save and restore the
;; PIC register.
(define_insn "call_reg_64bit"
  [(call (mem:SI (match_operand:DI 0 "register_operand" "r"))
	 (match_operand 1 "" "i"))
   (clobber (reg:DI 2))
   (clobber (reg:DI 4))
   (use (reg:DI 27))
   (use (reg:DI 29))
   (use (const_int 1))]
  "TARGET_64BIT"
  "*
{
  return output_indirect_call (insn, operands[0]);
}"
  [(set_attr "type" "dyncall")
   (set (attr "length")
	(plus (symbol_ref "attr_length_indirect_call (insn)")
	      (symbol_ref "attr_length_save_restore_dltp (insn)")))])

;; Split out the PIC register save and restore after reload.  This is
;; done only if the function returns.  As the split is done after reload,
;; there are some situations in which we unnecessarily save and restore
;; %r4.  This happens when there is a single call and the PIC register
;; is "dead" after the call.  This isn't easy to fix as the usage of
;; the PIC register isn't completely determined until the reload pass.
(define_split
  [(parallel [(call (mem:SI (match_operand 0 "register_operand" ""))
		    (match_operand 1 "" ""))
	      (clobber (reg:DI 2))
	      (clobber (reg:DI 4))
	      (use (reg:DI 27))
	      (use (reg:DI 29))
	      (use (const_int 1))])]
  "TARGET_64BIT
   && reload_completed
   && !find_reg_note (insn, REG_NORETURN, NULL_RTX)"
  [(set (reg:DI 4) (reg:DI 27))
   (parallel [(call (mem:SI (match_dup 0))
		    (match_dup 1))
	      (clobber (reg:DI 2))
	      (use (reg:DI 27))
	      (use (reg:DI 29))
	      (use (const_int 1))])
   (set (reg:DI 27) (reg:DI 4))]
  "")

;; Remove the clobber of register 4 when optimizing.  This has to be
;; done with a peephole optimization rather than a split because the
;; split sequence for a call must be longer than one instruction.
(define_peephole2
  [(parallel [(call (mem:SI (match_operand 0 "register_operand" ""))
		    (match_operand 1 "" ""))
	      (clobber (reg:DI 2))
	      (clobber (reg:DI 4))
	      (use (reg:DI 27))
	      (use (reg:DI 29))
	      (use (const_int 1))])]
  "TARGET_64BIT && reload_completed"
  [(parallel [(call (mem:SI (match_dup 0))
		    (match_dup 1))
	      (clobber (reg:DI 2))
	      (use (reg:DI 27))
	      (use (reg:DI 29))
	      (use (const_int 1))])]
  "")

(define_insn "*call_reg_64bit_post_reload"
  [(call (mem:SI (match_operand:DI 0 "register_operand" "r"))
	 (match_operand 1 "" "i"))
   (clobber (reg:DI 2))
   (use (reg:DI 27))
   (use (reg:DI 29))
   (use (const_int 1))]
  "TARGET_64BIT"
  "*
{
  return output_indirect_call (insn, operands[0]);
}"
  [(set_attr "type" "dyncall")
   (set (attr "length") (symbol_ref "attr_length_indirect_call (insn)"))])

(define_expand "call_value"
  [(parallel [(set (match_operand 0 "" "")
		   (call (match_operand:SI 1 "" "")
			 (match_operand 2 "" "")))
	      (clobber (reg:SI 2))])]
  ""
  "
{
  rtx op, call_insn;
  rtx dst = operands[0];
  rtx nb = operands[2];

  if (TARGET_PORTABLE_RUNTIME)
    op = force_reg (SImode, XEXP (operands[1], 0));
  else
    op = XEXP (operands[1], 0);

  if (TARGET_64BIT)
    {
      if (!virtuals_instantiated)
	emit_move_insn (arg_pointer_rtx,
			gen_rtx_PLUS (word_mode, virtual_outgoing_args_rtx,
				      GEN_INT (64)));
      else
	{
	  /* The loop pass can generate new libcalls after the virtual
	     registers are instantiated when fpregs are disabled because
	     the only method that we have for doing DImode multiplication
	     is with a libcall.  This could be trouble if we haven't
	     allocated enough space for the outgoing arguments.  */
	  gcc_assert (INTVAL (nb) <= current_function_outgoing_args_size);

	  emit_move_insn (arg_pointer_rtx,
			  gen_rtx_PLUS (word_mode, stack_pointer_rtx,
					GEN_INT (STACK_POINTER_OFFSET + 64)));
	}
    }

  /* Use two different patterns for calls to explicitly named functions
     and calls through function pointers.  This is necessary as these two
     types of calls use different calling conventions, and CSE might try
     to change the named call into an indirect call in some cases (using
     two patterns keeps CSE from performing this optimization).

     We now use even more call patterns as there was a subtle bug in
     attempting to restore the pic register after a call using a simple
     move insn.  During reload, a instruction involving a pseudo register
     with no explicit dependence on the PIC register can be converted
     to an equivalent load from memory using the PIC register.  If we
     emit a simple move to restore the PIC register in the initial rtl
     generation, then it can potentially be repositioned during scheduling.
     and an instruction that eventually uses the PIC register may end up
     between the call and the PIC register restore.
     
     This only worked because there is a post call group of instructions
     that are scheduled with the call.  These instructions are included
     in the same basic block as the call.  However, calls can throw in
     C++ code and a basic block has to terminate at the call if the call
     can throw.  This results in the PIC register restore being scheduled
     independently from the call.  So, we now hide the save and restore
     of the PIC register in the call pattern until after reload.  Then,
     we split the moves out.  A small side benefit is that we now don't
     need to have a use of the PIC register in the return pattern and
     the final save/restore operation is not needed.
     
     I elected to just clobber %r4 in the PIC patterns and use it instead
     of trying to force hppa_pic_save_rtx () to a callee saved register.
     This might have required a new register class and constraint.  It
     was also simpler to just handle the restore from a register than a
     generic pseudo.  */
  if (TARGET_64BIT)
    {
      if (GET_CODE (op) == SYMBOL_REF)
	call_insn = emit_call_insn (gen_call_val_symref_64bit (dst, op, nb));
      else
	{
	  op = force_reg (word_mode, op);
	  call_insn = emit_call_insn (gen_call_val_reg_64bit (dst, op, nb));
	}
    }
  else
    {
      if (GET_CODE (op) == SYMBOL_REF)
	{
	  if (flag_pic)
	    call_insn = emit_call_insn (gen_call_val_symref_pic (dst, op, nb));
	  else
	    call_insn = emit_call_insn (gen_call_val_symref (dst, op, nb));
	}
      else
	{
	  rtx tmpreg = gen_rtx_REG (word_mode, 22);

	  emit_move_insn (tmpreg, force_reg (word_mode, op));
	  if (flag_pic)
	    call_insn = emit_call_insn (gen_call_val_reg_pic (dst, nb));
	  else
	    call_insn = emit_call_insn (gen_call_val_reg (dst, nb));
	}
    }

  DONE;
}")

(define_insn "call_val_symref"
  [(set (match_operand 0 "" "")
	(call (mem:SI (match_operand 1 "call_operand_address" ""))
	      (match_operand 2 "" "i")))
   (clobber (reg:SI 1))
   (clobber (reg:SI 2))
   (use (const_int 0))]
  "!TARGET_PORTABLE_RUNTIME && !TARGET_64BIT"
  "*
{
  output_arg_descriptor (insn);
  return output_call (insn, operands[1], 0);
}"
  [(set_attr "type" "call")
   (set (attr "length") (symbol_ref "attr_length_call (insn, 0)"))])

(define_insn "call_val_symref_pic"
  [(set (match_operand 0 "" "")
	(call (mem:SI (match_operand 1 "call_operand_address" ""))
	      (match_operand 2 "" "i")))
   (clobber (reg:SI 1))
   (clobber (reg:SI 2))
   (clobber (reg:SI 4))
   (use (reg:SI 19))
   (use (const_int 0))]
  "!TARGET_PORTABLE_RUNTIME && !TARGET_64BIT"
  "*
{
  output_arg_descriptor (insn);
  return output_call (insn, operands[1], 0);
}"
  [(set_attr "type" "call")
   (set (attr "length")
	(plus (symbol_ref "attr_length_call (insn, 0)")
	      (symbol_ref "attr_length_save_restore_dltp (insn)")))])

;; Split out the PIC register save and restore after reload.  This is
;; done only if the function returns.  As the split is done after reload,
;; there are some situations in which we unnecessarily save and restore
;; %r4.  This happens when there is a single call and the PIC register
;; is "dead" after the call.  This isn't easy to fix as the usage of
;; the PIC register isn't completely determined until the reload pass.
(define_split
  [(parallel [(set (match_operand 0 "" "")
	      (call (mem:SI (match_operand 1 "call_operand_address" ""))
		    (match_operand 2 "" "")))
	      (clobber (reg:SI 1))
	      (clobber (reg:SI 2))
	      (clobber (reg:SI 4))
	      (use (reg:SI 19))
	      (use (const_int 0))])]
  "!TARGET_PORTABLE_RUNTIME && !TARGET_64BIT
   && reload_completed
   && !find_reg_note (insn, REG_NORETURN, NULL_RTX)"
  [(set (reg:SI 4) (reg:SI 19))
   (parallel [(set (match_dup 0)
	      (call (mem:SI (match_dup 1))
		    (match_dup 2)))
	      (clobber (reg:SI 1))
	      (clobber (reg:SI 2))
	      (use (reg:SI 19))
	      (use (const_int 0))])
   (set (reg:SI 19) (reg:SI 4))]
  "")

;; Remove the clobber of register 4 when optimizing.  This has to be
;; done with a peephole optimization rather than a split because the
;; split sequence for a call must be longer than one instruction.
(define_peephole2
  [(parallel [(set (match_operand 0 "" "")
	      (call (mem:SI (match_operand 1 "call_operand_address" ""))
		    (match_operand 2 "" "")))
	      (clobber (reg:SI 1))
	      (clobber (reg:SI 2))
	      (clobber (reg:SI 4))
	      (use (reg:SI 19))
	      (use (const_int 0))])]
  "!TARGET_PORTABLE_RUNTIME && !TARGET_64BIT && reload_completed"
  [(parallel [(set (match_dup 0)
	      (call (mem:SI (match_dup 1))
		    (match_dup 2)))
	      (clobber (reg:SI 1))
	      (clobber (reg:SI 2))
	      (use (reg:SI 19))
	      (use (const_int 0))])]
  "")

(define_insn "*call_val_symref_pic_post_reload"
  [(set (match_operand 0 "" "")
	(call (mem:SI (match_operand 1 "call_operand_address" ""))
	      (match_operand 2 "" "i")))
   (clobber (reg:SI 1))
   (clobber (reg:SI 2))
   (use (reg:SI 19))
   (use (const_int 0))]
  "!TARGET_PORTABLE_RUNTIME && !TARGET_64BIT"
  "*
{
  output_arg_descriptor (insn);
  return output_call (insn, operands[1], 0);
}"
  [(set_attr "type" "call")
   (set (attr "length") (symbol_ref "attr_length_call (insn, 0)"))])

;; This pattern is split if it is necessary to save and restore the
;; PIC register.
(define_insn "call_val_symref_64bit"
  [(set (match_operand 0 "" "")
	(call (mem:SI (match_operand 1 "call_operand_address" ""))
	      (match_operand 2 "" "i")))
   (clobber (reg:DI 1))
   (clobber (reg:DI 2))
   (clobber (reg:DI 4))
   (use (reg:DI 27))
   (use (reg:DI 29))
   (use (const_int 0))]
  "TARGET_64BIT"
  "*
{
  output_arg_descriptor (insn);
  return output_call (insn, operands[1], 0);
}"
  [(set_attr "type" "call")
   (set (attr "length")
	(plus (symbol_ref "attr_length_call (insn, 0)")
	      (symbol_ref "attr_length_save_restore_dltp (insn)")))])

;; Split out the PIC register save and restore after reload.  This is
;; done only if the function returns.  As the split is done after reload,
;; there are some situations in which we unnecessarily save and restore
;; %r4.  This happens when there is a single call and the PIC register
;; is "dead" after the call.  This isn't easy to fix as the usage of
;; the PIC register isn't completely determined until the reload pass.
(define_split
  [(parallel [(set (match_operand 0 "" "")
	      (call (mem:SI (match_operand 1 "call_operand_address" ""))
		    (match_operand 2 "" "")))
	      (clobber (reg:DI 1))
	      (clobber (reg:DI 2))
	      (clobber (reg:DI 4))
	      (use (reg:DI 27))
	      (use (reg:DI 29))
	      (use (const_int 0))])]
  "TARGET_64BIT
   && reload_completed
   && !find_reg_note (insn, REG_NORETURN, NULL_RTX)"
  [(set (reg:DI 4) (reg:DI 27))
   (parallel [(set (match_dup 0)
	      (call (mem:SI (match_dup 1))
		    (match_dup 2)))
	      (clobber (reg:DI 1))
	      (clobber (reg:DI 2))
	      (use (reg:DI 27))
	      (use (reg:DI 29))
	      (use (const_int 0))])
   (set (reg:DI 27) (reg:DI 4))]
  "")

;; Remove the clobber of register 4 when optimizing.  This has to be
;; done with a peephole optimization rather than a split because the
;; split sequence for a call must be longer than one instruction.
(define_peephole2
  [(parallel [(set (match_operand 0 "" "")
	      (call (mem:SI (match_operand 1 "call_operand_address" ""))
		    (match_operand 2 "" "")))
	      (clobber (reg:DI 1))
	      (clobber (reg:DI 2))
	      (clobber (reg:DI 4))
	      (use (reg:DI 27))
	      (use (reg:DI 29))
	      (use (const_int 0))])]
  "TARGET_64BIT && reload_completed"
  [(parallel [(set (match_dup 0)
	      (call (mem:SI (match_dup 1))
		    (match_dup 2)))
	      (clobber (reg:DI 1))
	      (clobber (reg:DI 2))
	      (use (reg:DI 27))
	      (use (reg:DI 29))
	      (use (const_int 0))])]
  "")

(define_insn "*call_val_symref_64bit_post_reload"
  [(set (match_operand 0 "" "")
	(call (mem:SI (match_operand 1 "call_operand_address" ""))
	      (match_operand 2 "" "i")))
   (clobber (reg:DI 1))
   (clobber (reg:DI 2))
   (use (reg:DI 27))
   (use (reg:DI 29))
   (use (const_int 0))]
  "TARGET_64BIT"
  "*
{
  output_arg_descriptor (insn);
  return output_call (insn, operands[1], 0);
}"
  [(set_attr "type" "call")
   (set (attr "length") (symbol_ref "attr_length_call (insn, 0)"))])

(define_insn "call_val_reg"
  [(set (match_operand 0 "" "")
	(call (mem:SI (reg:SI 22))
	      (match_operand 1 "" "i")))
   (clobber (reg:SI 1))
   (clobber (reg:SI 2))
   (use (const_int 1))]
  "!TARGET_64BIT"
  "*
{
  return output_indirect_call (insn, gen_rtx_REG (word_mode, 22));
}"
  [(set_attr "type" "dyncall")
   (set (attr "length") (symbol_ref "attr_length_indirect_call (insn)"))])

;; This pattern is split if it is necessary to save and restore the
;; PIC register.
(define_insn "call_val_reg_pic"
  [(set (match_operand 0 "" "")
	(call (mem:SI (reg:SI 22))
	      (match_operand 1 "" "i")))
   (clobber (reg:SI 1))
   (clobber (reg:SI 2))
   (clobber (reg:SI 4))
   (use (reg:SI 19))
   (use (const_int 1))]
  "!TARGET_64BIT"
  "*
{
  return output_indirect_call (insn, gen_rtx_REG (word_mode, 22));
}"
  [(set_attr "type" "dyncall")
   (set (attr "length")
	(plus (symbol_ref "attr_length_indirect_call (insn)")
	      (symbol_ref "attr_length_save_restore_dltp (insn)")))])

;; Split out the PIC register save and restore after reload.  This is
;; done only if the function returns.  As the split is done after reload,
;; there are some situations in which we unnecessarily save and restore
;; %r4.  This happens when there is a single call and the PIC register
;; is "dead" after the call.  This isn't easy to fix as the usage of
;; the PIC register isn't completely determined until the reload pass.
(define_split
  [(parallel [(set (match_operand 0 "" "")
		   (call (mem:SI (reg:SI 22))
			 (match_operand 1 "" "")))
	      (clobber (reg:SI 1))
	      (clobber (reg:SI 2))
	      (clobber (reg:SI 4))
	      (use (reg:SI 19))
	      (use (const_int 1))])]
  "!TARGET_64BIT
   && reload_completed
   && !find_reg_note (insn, REG_NORETURN, NULL_RTX)"
  [(set (reg:SI 4) (reg:SI 19))
   (parallel [(set (match_dup 0)
		   (call (mem:SI (reg:SI 22))
			 (match_dup 1)))
	      (clobber (reg:SI 1))
	      (clobber (reg:SI 2))
	      (use (reg:SI 19))
	      (use (const_int 1))])
   (set (reg:SI 19) (reg:SI 4))]
  "")

;; Remove the clobber of register 4 when optimizing.  This has to be
;; done with a peephole optimization rather than a split because the
;; split sequence for a call must be longer than one instruction.
(define_peephole2
  [(parallel [(set (match_operand 0 "" "")
		   (call (mem:SI (reg:SI 22))
			 (match_operand 1 "" "")))
	      (clobber (reg:SI 1))
	      (clobber (reg:SI 2))
	      (clobber (reg:SI 4))
	      (use (reg:SI 19))
	      (use (const_int 1))])]
  "!TARGET_64BIT && reload_completed"
  [(parallel [(set (match_dup 0)
		   (call (mem:SI (reg:SI 22))
			 (match_dup 1)))
	      (clobber (reg:SI 1))
	      (clobber (reg:SI 2))
	      (use (reg:SI 19))
	      (use (const_int 1))])]
  "")

(define_insn "*call_val_reg_pic_post_reload"
  [(set (match_operand 0 "" "")
	(call (mem:SI (reg:SI 22))
	      (match_operand 1 "" "i")))
   (clobber (reg:SI 1))
   (clobber (reg:SI 2))
   (use (reg:SI 19))
   (use (const_int 1))]
  "!TARGET_64BIT"
  "*
{
  return output_indirect_call (insn, gen_rtx_REG (word_mode, 22));
}"
  [(set_attr "type" "dyncall")
   (set (attr "length") (symbol_ref "attr_length_indirect_call (insn)"))])

;; This pattern is split if it is necessary to save and restore the
;; PIC register.
(define_insn "call_val_reg_64bit"
  [(set (match_operand 0 "" "")
	(call (mem:SI (match_operand:DI 1 "register_operand" "r"))
	      (match_operand 2 "" "i")))
   (clobber (reg:DI 2))
   (clobber (reg:DI 4))
   (use (reg:DI 27))
   (use (reg:DI 29))
   (use (const_int 1))]
  "TARGET_64BIT"
  "*
{
  return output_indirect_call (insn, operands[1]);
}"
  [(set_attr "type" "dyncall")
   (set (attr "length")
	(plus (symbol_ref "attr_length_indirect_call (insn)")
	      (symbol_ref "attr_length_save_restore_dltp (insn)")))])

;; Split out the PIC register save and restore after reload.  This is
;; done only if the function returns.  As the split is done after reload,
;; there are some situations in which we unnecessarily save and restore
;; %r4.  This happens when there is a single call and the PIC register
;; is "dead" after the call.  This isn't easy to fix as the usage of
;; the PIC register isn't completely determined until the reload pass.
(define_split
  [(parallel [(set (match_operand 0 "" "")
		   (call (mem:SI (match_operand:DI 1 "register_operand" ""))
			 (match_operand 2 "" "")))
	      (clobber (reg:DI 2))
	      (clobber (reg:DI 4))
	      (use (reg:DI 27))
	      (use (reg:DI 29))
	      (use (const_int 1))])]
  "TARGET_64BIT
   && reload_completed
   && !find_reg_note (insn, REG_NORETURN, NULL_RTX)"
  [(set (reg:DI 4) (reg:DI 27))
   (parallel [(set (match_dup 0)
		   (call (mem:SI (match_dup 1))
			 (match_dup 2)))
	      (clobber (reg:DI 2))
	      (use (reg:DI 27))
	      (use (reg:DI 29))
	      (use (const_int 1))])
   (set (reg:DI 27) (reg:DI 4))]
  "")

;; Remove the clobber of register 4 when optimizing.  This has to be
;; done with a peephole optimization rather than a split because the
;; split sequence for a call must be longer than one instruction.
(define_peephole2
  [(parallel [(set (match_operand 0 "" "")
		   (call (mem:SI (match_operand:DI 1 "register_operand" ""))
			 (match_operand 2 "" "")))
	      (clobber (reg:DI 2))
	      (clobber (reg:DI 4))
	      (use (reg:DI 27))
	      (use (reg:DI 29))
	      (use (const_int 1))])]
  "TARGET_64BIT && reload_completed"
  [(parallel [(set (match_dup 0)
		   (call (mem:SI (match_dup 1))
			 (match_dup 2)))
	      (clobber (reg:DI 2))
	      (use (reg:DI 27))
	      (use (reg:DI 29))
	      (use (const_int 1))])]
  "")

(define_insn "*call_val_reg_64bit_post_reload"
  [(set (match_operand 0 "" "")
	(call (mem:SI (match_operand:DI 1 "register_operand" "r"))
	      (match_operand 2 "" "i")))
   (clobber (reg:DI 2))
   (use (reg:DI 27))
   (use (reg:DI 29))
   (use (const_int 1))]
  "TARGET_64BIT"
  "*
{
  return output_indirect_call (insn, operands[1]);
}"
  [(set_attr "type" "dyncall")
   (set (attr "length") (symbol_ref "attr_length_indirect_call (insn)"))])

;; Call subroutine returning any type.

(define_expand "untyped_call"
  [(parallel [(call (match_operand 0 "" "")
		    (const_int 0))
	      (match_operand 1 "" "")
	      (match_operand 2 "" "")])]
  ""
  "
{
  int i;

  emit_call_insn (GEN_CALL (operands[0], const0_rtx, NULL, const0_rtx));

  for (i = 0; i < XVECLEN (operands[2], 0); i++)
    {
      rtx set = XVECEXP (operands[2], 0, i);
      emit_move_insn (SET_DEST (set), SET_SRC (set));
    }

  /* The optimizer does not know that the call sets the function value
     registers we stored in the result block.  We avoid problems by
     claiming that all hard registers are used and clobbered at this
     point.  */
  emit_insn (gen_blockage ());

  DONE;
}")

(define_expand "sibcall"
  [(call (match_operand:SI 0 "" "")
	 (match_operand 1 "" ""))]
  "!TARGET_PORTABLE_RUNTIME"
  "
{
  rtx op, call_insn;
  rtx nb = operands[1];

  op = XEXP (operands[0], 0);

  if (TARGET_64BIT)
    {
      if (!virtuals_instantiated)
	emit_move_insn (arg_pointer_rtx,
			gen_rtx_PLUS (word_mode, virtual_outgoing_args_rtx,
				      GEN_INT (64)));
      else
	{
	  /* The loop pass can generate new libcalls after the virtual
	     registers are instantiated when fpregs are disabled because
	     the only method that we have for doing DImode multiplication
	     is with a libcall.  This could be trouble if we haven't
	     allocated enough space for the outgoing arguments.  */
	  gcc_assert (INTVAL (nb) <= current_function_outgoing_args_size);

	  emit_move_insn (arg_pointer_rtx,
			  gen_rtx_PLUS (word_mode, stack_pointer_rtx,
					GEN_INT (STACK_POINTER_OFFSET + 64)));
	}
    }

  /* Indirect sibling calls are not allowed.  */
  if (TARGET_64BIT)
    call_insn = gen_sibcall_internal_symref_64bit (op, operands[1]);
  else
    call_insn = gen_sibcall_internal_symref (op, operands[1]);

  call_insn = emit_call_insn (call_insn);

  if (TARGET_64BIT)
    use_reg (&CALL_INSN_FUNCTION_USAGE (call_insn), arg_pointer_rtx);

  /* We don't have to restore the PIC register.  */
  if (flag_pic)
    use_reg (&CALL_INSN_FUNCTION_USAGE (call_insn), pic_offset_table_rtx);

  DONE;
}")

(define_insn "sibcall_internal_symref"
  [(call (mem:SI (match_operand 0 "call_operand_address" ""))
	 (match_operand 1 "" "i"))
   (clobber (reg:SI 1))
   (use (reg:SI 2))
   (use (const_int 0))]
  "!TARGET_PORTABLE_RUNTIME && !TARGET_64BIT"
  "*
{
  output_arg_descriptor (insn);
  return output_call (insn, operands[0], 1);
}"
  [(set_attr "type" "call")
   (set (attr "length") (symbol_ref "attr_length_call (insn, 1)"))])

(define_insn "sibcall_internal_symref_64bit"
  [(call (mem:SI (match_operand 0 "call_operand_address" ""))
	 (match_operand 1 "" "i"))
   (clobber (reg:DI 1))
   (use (reg:DI 2))
   (use (const_int 0))]
  "TARGET_64BIT"
  "*
{
  output_arg_descriptor (insn);
  return output_call (insn, operands[0], 1);
}"
  [(set_attr "type" "call")
   (set (attr "length") (symbol_ref "attr_length_call (insn, 1)"))])

(define_expand "sibcall_value"
  [(set (match_operand 0 "" "")
		   (call (match_operand:SI 1 "" "")
			 (match_operand 2 "" "")))]
  "!TARGET_PORTABLE_RUNTIME"
  "
{
  rtx op, call_insn;
  rtx nb = operands[1];

  op = XEXP (operands[1], 0);

  if (TARGET_64BIT)
    {
      if (!virtuals_instantiated)
	emit_move_insn (arg_pointer_rtx,
			gen_rtx_PLUS (word_mode, virtual_outgoing_args_rtx,
				      GEN_INT (64)));
      else
	{
	  /* The loop pass can generate new libcalls after the virtual
	     registers are instantiated when fpregs are disabled because
	     the only method that we have for doing DImode multiplication
	     is with a libcall.  This could be trouble if we haven't
	     allocated enough space for the outgoing arguments.  */
	  gcc_assert (INTVAL (nb) <= current_function_outgoing_args_size);

	  emit_move_insn (arg_pointer_rtx,
			  gen_rtx_PLUS (word_mode, stack_pointer_rtx,
					GEN_INT (STACK_POINTER_OFFSET + 64)));
	}
    }

  /* Indirect sibling calls are not allowed.  */
  if (TARGET_64BIT)
    call_insn
      = gen_sibcall_value_internal_symref_64bit (operands[0], op, operands[2]);
  else
    call_insn
      = gen_sibcall_value_internal_symref (operands[0], op, operands[2]);

  call_insn = emit_call_insn (call_insn);

  if (TARGET_64BIT)
    use_reg (&CALL_INSN_FUNCTION_USAGE (call_insn), arg_pointer_rtx);

  /* We don't have to restore the PIC register.  */
  if (flag_pic)
    use_reg (&CALL_INSN_FUNCTION_USAGE (call_insn), pic_offset_table_rtx);

  DONE;
}")

(define_insn "sibcall_value_internal_symref"
  [(set (match_operand 0 "" "")
	(call (mem:SI (match_operand 1 "call_operand_address" ""))
	      (match_operand 2 "" "i")))
   (clobber (reg:SI 1))
   (use (reg:SI 2))
   (use (const_int 0))]
  "!TARGET_PORTABLE_RUNTIME && !TARGET_64BIT"
  "*
{
  output_arg_descriptor (insn);
  return output_call (insn, operands[1], 1);
}"
  [(set_attr "type" "call")
   (set (attr "length") (symbol_ref "attr_length_call (insn, 1)"))])

(define_insn "sibcall_value_internal_symref_64bit"
  [(set (match_operand 0 "" "")
	(call (mem:SI (match_operand 1 "call_operand_address" ""))
	      (match_operand 2 "" "i")))
   (clobber (reg:DI 1))
   (use (reg:DI 2))
   (use (const_int 0))]
  "TARGET_64BIT"
  "*
{
  output_arg_descriptor (insn);
  return output_call (insn, operands[1], 1);
}"
  [(set_attr "type" "call")
   (set (attr "length") (symbol_ref "attr_length_call (insn, 1)"))])

(define_insn "nop"
  [(const_int 0)]
  ""
  "nop"
  [(set_attr "type" "move")
   (set_attr "length" "4")])

;; These are just placeholders so we know where branch tables
;; begin and end.
(define_insn "begin_brtab"
  [(const_int 1)]
  ""
  "*
{
  /* Only GAS actually supports this pseudo-op.  */
  if (TARGET_GAS)
    return \".begin_brtab\";
  else
    return \"\";
}"
  [(set_attr "type" "move")
   (set_attr "length" "0")])

(define_insn "end_brtab"
  [(const_int 2)]
  ""
  "*
{
  /* Only GAS actually supports this pseudo-op.  */
  if (TARGET_GAS)
    return \".end_brtab\";
  else
    return \"\";
}"
  [(set_attr "type" "move")
   (set_attr "length" "0")])

;;; EH does longjmp's from and within the data section.  Thus,
;;; an interspace branch is required for the longjmp implementation.
;;; Registers r1 and r2 are used as scratch registers for the jump
;;; when necessary.
(define_expand "interspace_jump"
  [(parallel
     [(set (pc) (match_operand 0 "pmode_register_operand" "a"))
      (clobber (match_dup 1))])]
  ""
  "
{
  operands[1] = gen_rtx_REG (word_mode, 2);
}")

(define_insn ""
  [(set (pc) (match_operand 0 "pmode_register_operand" "a"))
  (clobber (reg:SI 2))]
  "TARGET_PA_20 && !TARGET_64BIT"
  "bve%* (%0)"
   [(set_attr "type" "branch")
    (set_attr "length" "4")])

(define_insn ""
  [(set (pc) (match_operand 0 "pmode_register_operand" "a"))
  (clobber (reg:SI 2))]
  "TARGET_NO_SPACE_REGS && !TARGET_64BIT"
  "be%* 0(%%sr4,%0)"
   [(set_attr "type" "branch")
    (set_attr "length" "4")])

(define_insn ""
  [(set (pc) (match_operand 0 "pmode_register_operand" "a"))
  (clobber (reg:SI 2))]
  "!TARGET_64BIT"
  "ldsid (%%sr0,%0),%%r2\;mtsp %%r2,%%sr0\;be%* 0(%%sr0,%0)"
   [(set_attr "type" "branch")
    (set_attr "length" "12")])

(define_insn ""
  [(set (pc) (match_operand 0 "pmode_register_operand" "a"))
  (clobber (reg:DI 2))]
  "TARGET_64BIT"
  "bve%* (%0)"
   [(set_attr "type" "branch")
    (set_attr "length" "4")])

(define_expand "builtin_longjmp"
  [(unspec_volatile [(match_operand 0 "register_operand" "r")] UNSPECV_LONGJMP)]
  ""
  "
{
  /* The elements of the buffer are, in order:  */
  rtx fp = gen_rtx_MEM (Pmode, operands[0]);
  rtx lab = gen_rtx_MEM (Pmode, plus_constant (operands[0],
			 POINTER_SIZE / BITS_PER_UNIT));
  rtx stack = gen_rtx_MEM (Pmode, plus_constant (operands[0],
			   (POINTER_SIZE * 2) / BITS_PER_UNIT));
  rtx pv = gen_rtx_REG (Pmode, 1);

  emit_insn (gen_rtx_CLOBBER (VOIDmode,
			      gen_rtx_MEM (BLKmode,
					   gen_rtx_SCRATCH (VOIDmode))));
  emit_insn (gen_rtx_CLOBBER (VOIDmode,
			      gen_rtx_MEM (BLKmode,
					   hard_frame_pointer_rtx)));

  /* Restore the frame pointer.  The virtual_stack_vars_rtx is saved
     instead of the hard_frame_pointer_rtx in the save area.  We need
     to adjust for the offset between these two values when we have
     a nonlocal_goto pattern.  When we don't have a nonlocal_goto
     pattern, the receiver performs the adjustment.  */
#ifdef HAVE_nonlocal_goto
  if (HAVE_nonlocal_goto)
    emit_move_insn (virtual_stack_vars_rtx, force_reg (Pmode, fp));
  else
#endif
    emit_move_insn (hard_frame_pointer_rtx, fp);

  /* This bit is the same as expand_builtin_longjmp.  */
  emit_stack_restore (SAVE_NONLOCAL, stack, NULL_RTX);
  emit_insn (gen_rtx_USE (VOIDmode, hard_frame_pointer_rtx));
  emit_insn (gen_rtx_USE (VOIDmode, stack_pointer_rtx));

  /* Load the label we are jumping through into r1 so that we know
     where to look for it when we get back to setjmp's function for
     restoring the gp.  */
  emit_move_insn (pv, lab);

  /* Prevent the insns above from being scheduled into the delay slot
     of the interspace jump because the space register could change.  */
  emit_insn (gen_blockage ());

  emit_jump_insn (gen_interspace_jump (pv));
  emit_barrier ();
  DONE;
}")

;;; Operands 2 and 3 are assumed to be CONST_INTs.
(define_expand "extzv"
  [(set (match_operand 0 "register_operand" "")
	(zero_extract (match_operand 1 "register_operand" "")
		      (match_operand 2 "uint32_operand" "")
		      (match_operand 3 "uint32_operand" "")))]
  ""
  "
{
  HOST_WIDE_INT len = INTVAL (operands[2]);
  HOST_WIDE_INT pos = INTVAL (operands[3]);

  /* PA extraction insns don't support zero length bitfields or fields
     extending beyond the left or right-most bits.  Also, we reject lengths
     equal to a word as they are better handled by the move patterns.  */
  if (len <= 0 || len >= BITS_PER_WORD || pos < 0 || pos + len > BITS_PER_WORD)
    FAIL;

  /* From mips.md: extract_bit_field doesn't verify that our source
     matches the predicate, so check it again here.  */
  if (!register_operand (operands[1], VOIDmode))
    FAIL;

  if (TARGET_64BIT)
    emit_insn (gen_extzv_64 (operands[0], operands[1],
			     operands[2], operands[3]));
  else
    emit_insn (gen_extzv_32 (operands[0], operands[1],
			     operands[2], operands[3]));
  DONE;
}")

(define_insn "extzv_32"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(zero_extract:SI (match_operand:SI 1 "register_operand" "r")
			 (match_operand:SI 2 "uint5_operand" "")
			 (match_operand:SI 3 "uint5_operand" "")))]
  ""
  "{extru|extrw,u} %1,%3+%2-1,%2,%0"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(zero_extract:SI (match_operand:SI 1 "register_operand" "r")
			 (const_int 1)
			 (match_operand:SI 2 "register_operand" "q")))]
  ""
  "{vextru %1,1,%0|extrw,u %1,%%sar,1,%0}"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn "extzv_64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extract:DI (match_operand:DI 1 "register_operand" "r")
			 (match_operand:DI 2 "uint32_operand" "")
			 (match_operand:DI 3 "uint32_operand" "")))]
  "TARGET_64BIT"
  "extrd,u %1,%3+%2-1,%2,%0"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extract:DI (match_operand:DI 1 "register_operand" "r")
			 (const_int 1)
			 (match_operand:DI 2 "register_operand" "q")))]
  "TARGET_64BIT"
  "extrd,u %1,%%sar,1,%0"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

;;; Operands 2 and 3 are assumed to be CONST_INTs.
(define_expand "extv"
  [(set (match_operand 0 "register_operand" "")
	(sign_extract (match_operand 1 "register_operand" "")
		      (match_operand 2 "uint32_operand" "")
		      (match_operand 3 "uint32_operand" "")))]
  ""
  "
{
  HOST_WIDE_INT len = INTVAL (operands[2]);
  HOST_WIDE_INT pos = INTVAL (operands[3]);

  /* PA extraction insns don't support zero length bitfields or fields
     extending beyond the left or right-most bits.  Also, we reject lengths
     equal to a word as they are better handled by the move patterns.  */
  if (len <= 0 || len >= BITS_PER_WORD || pos < 0 || pos + len > BITS_PER_WORD)
    FAIL;

  /* From mips.md: extract_bit_field doesn't verify that our source
     matches the predicate, so check it again here.  */
  if (!register_operand (operands[1], VOIDmode))
    FAIL;

  if (TARGET_64BIT)
    emit_insn (gen_extv_64 (operands[0], operands[1],
			    operands[2], operands[3]));
  else
    emit_insn (gen_extv_32 (operands[0], operands[1],
			    operands[2], operands[3]));
  DONE;
}")

(define_insn "extv_32"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(sign_extract:SI (match_operand:SI 1 "register_operand" "r")
			 (match_operand:SI 2 "uint5_operand" "")
			 (match_operand:SI 3 "uint5_operand" "")))]
  ""
  "{extrs|extrw,s} %1,%3+%2-1,%2,%0"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(sign_extract:SI (match_operand:SI 1 "register_operand" "r")
			 (const_int 1)
			 (match_operand:SI 2 "register_operand" "q")))]
  "!TARGET_64BIT"
  "{vextrs %1,1,%0|extrw,s %1,%%sar,1,%0}"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn "extv_64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extract:DI (match_operand:DI 1 "register_operand" "r")
			 (match_operand:DI 2 "uint32_operand" "")
			 (match_operand:DI 3 "uint32_operand" "")))]
  "TARGET_64BIT"
  "extrd,s %1,%3+%2-1,%2,%0"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extract:DI (match_operand:DI 1 "register_operand" "r")
			 (const_int 1)
			 (match_operand:DI 2 "register_operand" "q")))]
  "TARGET_64BIT"
  "extrd,s %1,%%sar,1,%0"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

;;; Operands 1 and 2 are assumed to be CONST_INTs.
(define_expand "insv"
  [(set (zero_extract (match_operand 0 "register_operand" "")
                      (match_operand 1 "uint32_operand" "")
                      (match_operand 2 "uint32_operand" ""))
        (match_operand 3 "arith5_operand" ""))]
  ""
  "
{
  HOST_WIDE_INT len = INTVAL (operands[1]);
  HOST_WIDE_INT pos = INTVAL (operands[2]);

  /* PA insertion insns don't support zero length bitfields or fields
     extending beyond the left or right-most bits.  Also, we reject lengths
     equal to a word as they are better handled by the move patterns.  */
  if (len <= 0 || len >= BITS_PER_WORD || pos < 0 || pos + len > BITS_PER_WORD)
    FAIL;

  /* From mips.md: insert_bit_field doesn't verify that our destination
     matches the predicate, so check it again here.  */
  if (!register_operand (operands[0], VOIDmode))
    FAIL;

  if (TARGET_64BIT)
    emit_insn (gen_insv_64 (operands[0], operands[1],
			    operands[2], operands[3]));
  else
    emit_insn (gen_insv_32 (operands[0], operands[1],
			    operands[2], operands[3]));
  DONE;
}")

(define_insn "insv_32"
  [(set (zero_extract:SI (match_operand:SI 0 "register_operand" "+r,r")
			 (match_operand:SI 1 "uint5_operand" "")
			 (match_operand:SI 2 "uint5_operand" ""))
	(match_operand:SI 3 "arith5_operand" "r,L"))]
  ""
  "@
   {dep|depw} %3,%2+%1-1,%1,%0
   {depi|depwi} %3,%2+%1-1,%1,%0"
  [(set_attr "type" "shift,shift")
   (set_attr "length" "4,4")])

;; Optimize insertion of const_int values of type 1...1xxxx.
(define_insn ""
  [(set (zero_extract:SI (match_operand:SI 0 "register_operand" "+r")
			 (match_operand:SI 1 "uint5_operand" "")
			 (match_operand:SI 2 "uint5_operand" ""))
	(match_operand:SI 3 "const_int_operand" ""))]
  "(INTVAL (operands[3]) & 0x10) != 0 &&
   (~INTVAL (operands[3]) & ((1L << INTVAL (operands[1])) - 1) & ~0xf) == 0"
  "*
{
  operands[3] = GEN_INT ((INTVAL (operands[3]) & 0xf) - 0x10);
  return \"{depi|depwi} %3,%2+%1-1,%1,%0\";
}"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn "insv_64"
  [(set (zero_extract:DI (match_operand:DI 0 "register_operand" "+r,r")
			 (match_operand:DI 1 "uint32_operand" "")
			 (match_operand:DI 2 "uint32_operand" ""))
	(match_operand:DI 3 "arith32_operand" "r,L"))]
  "TARGET_64BIT"
  "@
   depd %3,%2+%1-1,%1,%0
   depdi %3,%2+%1-1,%1,%0"
  [(set_attr "type" "shift,shift")
   (set_attr "length" "4,4")])

;; Optimize insertion of const_int values of type 1...1xxxx.
(define_insn ""
  [(set (zero_extract:DI (match_operand:DI 0 "register_operand" "+r")
			 (match_operand:DI 1 "uint32_operand" "")
			 (match_operand:DI 2 "uint32_operand" ""))
	(match_operand:DI 3 "const_int_operand" ""))]
  "(INTVAL (operands[3]) & 0x10) != 0
   && TARGET_64BIT
   && (~INTVAL (operands[3]) & ((1L << INTVAL (operands[1])) - 1) & ~0xf) == 0"
  "*
{
  operands[3] = GEN_INT ((INTVAL (operands[3]) & 0xf) - 0x10);
  return \"depdi %3,%2+%1-1,%1,%0\";
}"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI (zero_extend:DI (match_operand:SI 1 "register_operand" "r"))
		   (const_int 32)))]
  "TARGET_64BIT"
  "depd,z %1,31,32,%0"
  [(set_attr "type" "shift")
   (set_attr "length" "4")])

;; This insn is used for some loop tests, typically loops reversed when
;; strength reduction is used.  It is actually created when the instruction
;; combination phase combines the special loop test.  Since this insn
;; is both a jump insn and has an output, it must deal with its own
;; reloads, hence the `m' constraints.  The `!' constraints direct reload
;; to not choose the register alternatives in the event a reload is needed.
(define_insn "decrement_and_branch_until_zero"
  [(set (pc)
	(if_then_else
	  (match_operator 2 "comparison_operator"
	   [(plus:SI
	      (match_operand:SI 0 "reg_before_reload_operand" "+!r,!*f,*m")
	      (match_operand:SI 1 "int5_operand" "L,L,L"))
	    (const_int 0)])
	  (label_ref (match_operand 3 "" ""))
	  (pc)))
   (set (match_dup 0)
	(plus:SI (match_dup 0) (match_dup 1)))
   (clobber (match_scratch:SI 4 "=X,r,r"))]
  ""
  "* return output_dbra (operands, insn, which_alternative); "
;; Do not expect to understand this the first time through.
[(set_attr "type" "cbranch,multi,multi")
 (set (attr "length")
      (if_then_else (eq_attr "alternative" "0")
;; Loop counter in register case
;; Short branch has length of 4
;; Long branch has length of 8, 20, 24 or 28
	(cond [(lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28))

;; Loop counter in FP reg case.
;; Extra goo to deal with additional reload insns.
	(if_then_else (eq_attr "alternative" "1")
	  (if_then_else (lt (match_dup 3) (pc))
	     (cond [(lt (abs (minus (match_dup 3) (plus (pc) (const_int 24))))
		      (const_int MAX_12BIT_OFFSET))
		    (const_int 24)
		    (lt (abs (minus (match_dup 3) (plus (pc) (const_int 24))))
		      (const_int MAX_17BIT_OFFSET))
		    (const_int 28)
		    (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
		    (const_int 44)
		    (eq (symbol_ref "flag_pic") (const_int 0))
		    (const_int 40)]
		  (const_int 48))
	     (cond [(lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
		      (const_int MAX_12BIT_OFFSET))
		    (const_int 24)
		    (lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
		      (const_int MAX_17BIT_OFFSET))
		    (const_int 28)
		    (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
		    (const_int 44)
		    (eq (symbol_ref "flag_pic") (const_int 0))
		    (const_int 40)]
		  (const_int 48)))

;; Loop counter in memory case.
;; Extra goo to deal with additional reload insns.
	(if_then_else (lt (match_dup 3) (pc))
	     (cond [(lt (abs (minus (match_dup 3) (plus (pc) (const_int 12))))
		      (const_int MAX_12BIT_OFFSET))
		    (const_int 12)
		    (lt (abs (minus (match_dup 3) (plus (pc) (const_int 12))))
		      (const_int MAX_17BIT_OFFSET))
		    (const_int 16)
		    (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
		    (const_int 32)
		    (eq (symbol_ref "flag_pic") (const_int 0))
		    (const_int 28)]
		  (const_int 36))
	     (cond [(lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
		      (const_int MAX_12BIT_OFFSET))
		    (const_int 12)
		    (lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
		      (const_int MAX_17BIT_OFFSET))
		    (const_int 16)
		    (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
		    (const_int 32)
		    (eq (symbol_ref "flag_pic") (const_int 0))
		    (const_int 28)]
		  (const_int 36))))))])

(define_insn ""
  [(set (pc)
	(if_then_else
	  (match_operator 2 "movb_comparison_operator"
	   [(match_operand:SI 1 "register_operand" "r,r,r,r") (const_int 0)])
	  (label_ref (match_operand 3 "" ""))
	  (pc)))
   (set (match_operand:SI 0 "reg_before_reload_operand" "=!r,!*f,*m,!*q")
	(match_dup 1))]
  ""
"* return output_movb (operands, insn, which_alternative, 0); "
;; Do not expect to understand this the first time through.
[(set_attr "type" "cbranch,multi,multi,multi")
 (set (attr "length")
      (if_then_else (eq_attr "alternative" "0")
;; Loop counter in register case
;; Short branch has length of 4
;; Long branch has length of 8, 20, 24 or 28
        (cond [(lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28))

;; Loop counter in FP reg case.
;; Extra goo to deal with additional reload insns.
	(if_then_else (eq_attr "alternative" "1")
	  (if_then_else (lt (match_dup 3) (pc))
	     (cond [(lt (abs (minus (match_dup 3) (plus (pc) (const_int 12))))
		      (const_int MAX_12BIT_OFFSET))
		    (const_int 12)
		    (lt (abs (minus (match_dup 3) (plus (pc) (const_int 12))))
		      (const_int MAX_17BIT_OFFSET))
		    (const_int 16)
		    (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
		    (const_int 32)
		    (eq (symbol_ref "flag_pic") (const_int 0))
		    (const_int 28)]
		  (const_int 36))
	     (cond [(lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
		      (const_int MAX_12BIT_OFFSET))
		    (const_int 12)
		    (lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
		      (const_int MAX_17BIT_OFFSET))
		    (const_int 16)
		    (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
		    (const_int 32)
		    (eq (symbol_ref "flag_pic") (const_int 0))
		    (const_int 28)]
		  (const_int 36)))

;; Loop counter in memory or sar case.
;; Extra goo to deal with additional reload insns.
	(cond [(lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
		   (const_int MAX_12BIT_OFFSET))
		(const_int 8)
		(lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
		  (const_int MAX_17BIT_OFFSET))
		(const_int 12)
		(ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
		(const_int 28)
		(eq (symbol_ref "flag_pic") (const_int 0))
		(const_int 24)]
	      (const_int 32)))))])

;; Handle negated branch.
(define_insn ""
  [(set (pc)
	(if_then_else
	  (match_operator 2 "movb_comparison_operator"
	   [(match_operand:SI 1 "register_operand" "r,r,r,r") (const_int 0)])
	  (pc)
	  (label_ref (match_operand 3 "" ""))))
   (set (match_operand:SI 0 "reg_before_reload_operand" "=!r,!*f,*m,!*q")
	(match_dup 1))]
  ""
"* return output_movb (operands, insn, which_alternative, 1); "
;; Do not expect to understand this the first time through.
[(set_attr "type" "cbranch,multi,multi,multi")
 (set (attr "length")
      (if_then_else (eq_attr "alternative" "0")
;; Loop counter in register case
;; Short branch has length of 4
;; Long branch has length of 8
        (cond [(lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28))

;; Loop counter in FP reg case.
;; Extra goo to deal with additional reload insns.
	(if_then_else (eq_attr "alternative" "1")
	  (if_then_else (lt (match_dup 3) (pc))
	     (cond [(lt (abs (minus (match_dup 3) (plus (pc) (const_int 12))))
		      (const_int MAX_12BIT_OFFSET))
		    (const_int 12)
		    (lt (abs (minus (match_dup 3) (plus (pc) (const_int 12))))
		      (const_int MAX_17BIT_OFFSET))
		    (const_int 16)
		    (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
		    (const_int 32)
		    (eq (symbol_ref "flag_pic") (const_int 0))
		    (const_int 28)]
		  (const_int 36))
	     (cond [(lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
		      (const_int MAX_12BIT_OFFSET))
		    (const_int 12)
		    (lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
		      (const_int MAX_17BIT_OFFSET))
		    (const_int 16)
		    (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
		    (const_int 32)
		    (eq (symbol_ref "flag_pic") (const_int 0))
		    (const_int 28)]
		  (const_int 36)))

;; Loop counter in memory or SAR case.
;; Extra goo to deal with additional reload insns.
	(cond [(lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
		   (const_int MAX_12BIT_OFFSET))
		(const_int 8)
		(lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
		  (const_int MAX_17BIT_OFFSET))
		(const_int 12)
		(ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
		(const_int 28)
		(eq (symbol_ref "flag_pic") (const_int 0))
		(const_int 24)]
	      (const_int 32)))))])

(define_insn ""
  [(set (pc) (label_ref (match_operand 3 "" "" )))
   (set (match_operand:SI 0 "ireg_operand" "=r")
	(plus:SI (match_operand:SI 1 "ireg_operand" "r")
		 (match_operand:SI 2 "ireg_or_int5_operand" "rL")))]
  "(reload_completed && operands[0] == operands[1]) || operands[0] == operands[2]"
  "*
{
  return output_parallel_addb (operands, insn);
}"
[(set_attr "type" "parallel_branch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 3) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc) (label_ref (match_operand 2 "" "" )))
   (set (match_operand:SF 0 "ireg_operand" "=r")
	(match_operand:SF 1 "ireg_or_int5_operand" "rL"))]
  "reload_completed"
  "*
{
  return output_parallel_movb (operands, insn);
}"
[(set_attr "type" "parallel_branch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc) (label_ref (match_operand 2 "" "" )))
   (set (match_operand:SI 0 "ireg_operand" "=r")
	(match_operand:SI 1 "ireg_or_int5_operand" "rL"))]
  "reload_completed"
  "*
{
  return output_parallel_movb (operands, insn);
}"
[(set_attr "type" "parallel_branch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc) (label_ref (match_operand 2 "" "" )))
   (set (match_operand:HI 0 "ireg_operand" "=r")
	(match_operand:HI 1 "ireg_or_int5_operand" "rL"))]
  "reload_completed"
  "*
{
  return output_parallel_movb (operands, insn);
}"
[(set_attr "type" "parallel_branch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (pc) (label_ref (match_operand 2 "" "" )))
   (set (match_operand:QI 0 "ireg_operand" "=r")
	(match_operand:QI 1 "ireg_or_int5_operand" "rL"))]
  "reload_completed"
  "*
{
  return output_parallel_movb (operands, insn);
}"
[(set_attr "type" "parallel_branch")
 (set (attr "length")
    (cond [(lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_12BIT_OFFSET))
	   (const_int 4)
	   (lt (abs (minus (match_dup 2) (plus (pc) (const_int 8))))
	       (const_int MAX_17BIT_OFFSET))
	   (const_int 8)
	   (ne (symbol_ref "TARGET_PORTABLE_RUNTIME") (const_int 0))
	   (const_int 24)
	   (eq (symbol_ref "flag_pic") (const_int 0))
	   (const_int 20)]
	  (const_int 28)))])

(define_insn ""
  [(set (match_operand 0 "register_operand" "=f")
	(mult (match_operand 1 "register_operand" "f")
	      (match_operand 2 "register_operand" "f")))
   (set (match_operand 3 "register_operand" "+f")
	(plus (match_operand 4 "register_operand" "f")
	      (match_operand 5 "register_operand" "f")))]
  "TARGET_PA_11 && ! TARGET_SOFT_FLOAT
   && reload_completed && fmpyaddoperands (operands)"
  "*
{
  if (GET_MODE (operands[0]) == DFmode)
    {
      if (rtx_equal_p (operands[3], operands[5]))
	return \"fmpyadd,dbl %1,%2,%0,%4,%3\";
      else
	return \"fmpyadd,dbl %1,%2,%0,%5,%3\";
    }
  else
    {
      if (rtx_equal_p (operands[3], operands[5]))
	return \"fmpyadd,sgl %1,%2,%0,%4,%3\";
      else
	return \"fmpyadd,sgl %1,%2,%0,%5,%3\";
    }
}"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand 3 "register_operand" "+f")
	(plus (match_operand 4 "register_operand" "f")
	      (match_operand 5 "register_operand" "f")))
   (set (match_operand 0 "register_operand" "=f")
	(mult (match_operand 1 "register_operand" "f")
	      (match_operand 2 "register_operand" "f")))]
  "TARGET_PA_11 && ! TARGET_SOFT_FLOAT
   && reload_completed && fmpyaddoperands (operands)"
  "*
{
  if (GET_MODE (operands[0]) == DFmode)
    {
      if (rtx_equal_p (operands[3], operands[5]))
	return \"fmpyadd,dbl %1,%2,%0,%4,%3\";
      else
	return \"fmpyadd,dbl %1,%2,%0,%5,%3\";
    }
  else
    {
      if (rtx_equal_p (operands[3], operands[5]))
	return \"fmpyadd,sgl %1,%2,%0,%4,%3\";
      else
	return \"fmpyadd,sgl %1,%2,%0,%5,%3\";
    }
}"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand 0 "register_operand" "=f")
	(mult (match_operand 1 "register_operand" "f")
	      (match_operand 2 "register_operand" "f")))
   (set (match_operand 3 "register_operand" "+f")
	(minus (match_operand 4 "register_operand" "f")
	       (match_operand 5 "register_operand" "f")))]
  "TARGET_PA_11 && ! TARGET_SOFT_FLOAT
   && reload_completed && fmpysuboperands (operands)"
  "*
{
  if (GET_MODE (operands[0]) == DFmode)
    return \"fmpysub,dbl %1,%2,%0,%5,%3\";
  else
    return \"fmpysub,sgl %1,%2,%0,%5,%3\";
}"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

(define_insn ""
  [(set (match_operand 3 "register_operand" "+f")
	(minus (match_operand 4 "register_operand" "f")
	       (match_operand 5 "register_operand" "f")))
   (set (match_operand 0 "register_operand" "=f")
	(mult (match_operand 1 "register_operand" "f")
	      (match_operand 2 "register_operand" "f")))]
  "TARGET_PA_11 && ! TARGET_SOFT_FLOAT
   && reload_completed && fmpysuboperands (operands)"
  "*
{
  if (GET_MODE (operands[0]) == DFmode)
    return \"fmpysub,dbl %1,%2,%0,%5,%3\";
  else
    return \"fmpysub,sgl %1,%2,%0,%5,%3\";
}"
  [(set_attr "type" "fpalu")
   (set_attr "length" "4")])

;; Flush the I and D cache lines from the start address (operand0)
;; to the end address (operand1).  No lines are flushed if the end
;; address is less than the start address (unsigned).
;;
;; Because the range of memory flushed is variable and the size of
;; a MEM can only be a CONST_INT, the patterns specify that they
;; perform an unspecified volatile operation on all memory.
;;
;; The address range for an icache flush must lie within a single
;; space on targets with non-equivalent space registers.
;;
;; This is used by the trampoline code for nested functions.
;;
;; Operand 0 contains the start address.
;; Operand 1 contains the end address.
;; Operand 2 contains the line length to use.
;; Operands 3 and 4 (icacheflush) are clobbered scratch registers.
(define_insn "dcacheflush"
  [(const_int 1)
   (unspec_volatile [(mem:BLK (scratch))] UNSPECV_DCACHE)
   (use (match_operand 0 "pmode_register_operand" "r"))
   (use (match_operand 1 "pmode_register_operand" "r"))
   (use (match_operand 2 "pmode_register_operand" "r"))
   (clobber (match_scratch 3 "=&0"))]
  ""
  "*
{
  if (TARGET_64BIT)
    return \"cmpb,*<<=,n %3,%1,.\;fdc,m %2(%3)\;sync\";
  else
    return \"cmpb,<<=,n %3,%1,.\;fdc,m %2(%3)\;sync\";
}"
  [(set_attr "type" "multi")
   (set_attr "length" "12")])

(define_insn "icacheflush"
  [(const_int 2)
   (unspec_volatile [(mem:BLK (scratch))] UNSPECV_ICACHE)
   (use (match_operand 0 "pmode_register_operand" "r"))
   (use (match_operand 1 "pmode_register_operand" "r"))
   (use (match_operand 2 "pmode_register_operand" "r"))
   (clobber (match_operand 3 "pmode_register_operand" "=&r"))
   (clobber (match_operand 4 "pmode_register_operand" "=&r"))
   (clobber (match_scratch 5 "=&0"))]
  ""
  "*
{
  if (TARGET_64BIT)
    return \"mfsp %%sr0,%4\;ldsid (%5),%3\;mtsp %3,%%sr0\;cmpb,*<<=,n %5,%1,.\;fic,m %2(%%sr0,%5)\;sync\;mtsp %4,%%sr0\;nop\;nop\;nop\;nop\;nop\;nop\";
  else
    return \"mfsp %%sr0,%4\;ldsid (%5),%3\;mtsp %3,%%sr0\;cmpb,<<=,n %5,%1,.\;fic,m %2(%%sr0,%5)\;sync\;mtsp %4,%%sr0\;nop\;nop\;nop\;nop\;nop\;nop\";
}"
  [(set_attr "type" "multi")
   (set_attr "length" "52")])

;; An out-of-line prologue.
(define_insn "outline_prologue_call"
  [(unspec_volatile [(const_int 0)] UNSPECV_OPC)
   (clobber (reg:SI 31))
   (clobber (reg:SI 22))
   (clobber (reg:SI 21))
   (clobber (reg:SI 20))
   (clobber (reg:SI 19))
   (clobber (reg:SI 1))]
  ""
  "*
{
  extern int frame_pointer_needed;

  /* We need two different versions depending on whether or not we
     need a frame pointer.   Also note that we return to the instruction
     immediately after the branch rather than two instructions after the
     break as normally is the case.  */
  if (frame_pointer_needed)
    {
      /* Must import the magic millicode routine(s).  */
      output_asm_insn (\".IMPORT __outline_prologue_fp,MILLICODE\", NULL);

      if (TARGET_PORTABLE_RUNTIME)
	{
	  output_asm_insn (\"ldil L'__outline_prologue_fp,%%r31\", NULL);
	  output_asm_insn (\"ble,n R'__outline_prologue_fp(%%sr0,%%r31)\",
			   NULL);
	}
      else
	output_asm_insn (\"{bl|b,l},n __outline_prologue_fp,%%r31\", NULL);
    }
  else
    {
      /* Must import the magic millicode routine(s).  */
      output_asm_insn (\".IMPORT __outline_prologue,MILLICODE\", NULL);

      if (TARGET_PORTABLE_RUNTIME)
	{
	  output_asm_insn (\"ldil L'__outline_prologue,%%r31\", NULL);
	  output_asm_insn (\"ble,n R'__outline_prologue(%%sr0,%%r31)\", NULL);
	}
      else
	output_asm_insn (\"{bl|b,l},n __outline_prologue,%%r31\", NULL);
    }
  return \"\";
}"
  [(set_attr "type" "multi")
   (set_attr "length" "8")])

;; An out-of-line epilogue.
(define_insn "outline_epilogue_call"
  [(unspec_volatile [(const_int 1)] UNSPECV_OEC)
   (use (reg:SI 29))
   (use (reg:SI 28))
   (clobber (reg:SI 31))
   (clobber (reg:SI 22))
   (clobber (reg:SI 21))
   (clobber (reg:SI 20))
   (clobber (reg:SI 19))
   (clobber (reg:SI 2))
   (clobber (reg:SI 1))]
  ""
  "*
{
  extern int frame_pointer_needed;

  /* We need two different versions depending on whether or not we
     need a frame pointer.   Also note that we return to the instruction
     immediately after the branch rather than two instructions after the
     break as normally is the case.  */
  if (frame_pointer_needed)
    {
      /* Must import the magic millicode routine.  */
      output_asm_insn (\".IMPORT __outline_epilogue_fp,MILLICODE\", NULL);

      /* The out-of-line prologue will make sure we return to the right
	 instruction.  */
      if (TARGET_PORTABLE_RUNTIME)
	{
	  output_asm_insn (\"ldil L'__outline_epilogue_fp,%%r31\", NULL);
	  output_asm_insn (\"ble,n R'__outline_epilogue_fp(%%sr0,%%r31)\",
			   NULL);
	}
      else
	output_asm_insn (\"{bl|b,l},n __outline_epilogue_fp,%%r31\", NULL);
    }
  else
    {
      /* Must import the magic millicode routine.  */
      output_asm_insn (\".IMPORT __outline_epilogue,MILLICODE\", NULL);

      /* The out-of-line prologue will make sure we return to the right
	 instruction.  */
      if (TARGET_PORTABLE_RUNTIME)
	{
	  output_asm_insn (\"ldil L'__outline_epilogue,%%r31\", NULL);
	  output_asm_insn (\"ble,n R'__outline_epilogue(%%sr0,%%r31)\", NULL);
	}
      else
	output_asm_insn (\"{bl|b,l},n __outline_epilogue,%%r31\", NULL);
    }
  return \"\";
}"
  [(set_attr "type" "multi")
   (set_attr "length" "8")])

;; Given a function pointer, canonicalize it so it can be 
;; reliably compared to another function pointer.  */
(define_expand "canonicalize_funcptr_for_compare"
  [(set (reg:SI 26) (match_operand:SI 1 "register_operand" ""))
   (parallel [(set (reg:SI 29) (unspec:SI [(reg:SI 26)] UNSPEC_CFFC))
	      (clobber (match_dup 2))
	      (clobber (reg:SI 26))
	      (clobber (reg:SI 22))
	      (clobber (reg:SI 31))])
   (set (match_operand:SI 0 "register_operand" "")
	(reg:SI 29))]
  "!TARGET_PORTABLE_RUNTIME && !TARGET_64BIT"
  "
{
  if (TARGET_ELF32)
    {
      rtx canonicalize_funcptr_for_compare_libfunc
        = init_one_libfunc (CANONICALIZE_FUNCPTR_FOR_COMPARE_LIBCALL);

      emit_library_call_value (canonicalize_funcptr_for_compare_libfunc,
      			       operands[0], LCT_NORMAL, Pmode,
			       1, operands[1], Pmode);
      DONE;
    }

  operands[2] = gen_reg_rtx (SImode);
  if (GET_CODE (operands[1]) != REG)
    {
      rtx tmp = gen_reg_rtx (Pmode);
      emit_move_insn (tmp, operands[1]);
      operands[1] = tmp;
    }
}")

(define_insn "*$$sh_func_adrs"
  [(set (reg:SI 29) (unspec:SI [(reg:SI 26)] UNSPEC_CFFC))
   (clobber (match_operand:SI 0 "register_operand" "=a"))
   (clobber (reg:SI 26))
   (clobber (reg:SI 22))
   (clobber (reg:SI 31))]
  "!TARGET_64BIT"
  "*
{
  int length = get_attr_length (insn);
  rtx xoperands[2];

  xoperands[0] = GEN_INT (length - 8);
  xoperands[1] = GEN_INT (length - 16);

  /* Must import the magic millicode routine.  */
  output_asm_insn (\".IMPORT $$sh_func_adrs,MILLICODE\", NULL);

  /* This is absolutely amazing.

     First, copy our input parameter into %r29 just in case we don't
     need to call $$sh_func_adrs.  */
  output_asm_insn (\"copy %%r26,%%r29\", NULL);
  output_asm_insn (\"{extru|extrw,u} %%r26,31,2,%%r31\", NULL);

  /* Next, examine the low two bits in %r26, if they aren't 0x2, then
     we use %r26 unchanged.  */
  output_asm_insn (\"{comib|cmpib},<>,n 2,%%r31,.+%0\", xoperands);
  output_asm_insn (\"ldi 4096,%%r31\", NULL);

  /* Next, compare %r26 with 4096, if %r26 is less than or equal to
     4096, then again we use %r26 unchanged.  */
  output_asm_insn (\"{comb|cmpb},<<,n %%r26,%%r31,.+%1\", xoperands);

  /* Finally, call $$sh_func_adrs to extract the function's real add24.  */
  return output_millicode_call (insn,
				gen_rtx_SYMBOL_REF (SImode,
						    \"$$sh_func_adrs\"));
}"
  [(set_attr "type" "multi")
   (set (attr "length")
	(plus (symbol_ref "attr_length_millicode_call (insn)")
	      (const_int 20)))])

;; On the PA, the PIC register is call clobbered, so it must
;; be saved & restored around calls by the caller.  If the call
;; doesn't return normally (nonlocal goto, or an exception is
;; thrown), then the code at the exception handler label must
;; restore the PIC register.
(define_expand "exception_receiver"
  [(const_int 4)]
  "flag_pic"
  "
{
  /* On the 64-bit port, we need a blockage because there is
     confusion regarding the dependence of the restore on the
     frame pointer.  As a result, the frame pointer and pic
     register restores sometimes are interchanged erroneously.  */
  if (TARGET_64BIT)
    emit_insn (gen_blockage ());
  /* Restore the PIC register using hppa_pic_save_rtx ().  The
     PIC register is not saved in the frame in 64-bit ABI.  */
  emit_move_insn (pic_offset_table_rtx, hppa_pic_save_rtx ());
  emit_insn (gen_blockage ());
  DONE;
}")

(define_expand "builtin_setjmp_receiver"
  [(label_ref (match_operand 0 "" ""))]
  "flag_pic"
  "
{
  if (TARGET_64BIT)
    emit_insn (gen_blockage ());
  /* Restore the PIC register.  Hopefully, this will always be from
     a stack slot.  The only registers that are valid after a
     builtin_longjmp are the stack and frame pointers.  */
  emit_move_insn (pic_offset_table_rtx, hppa_pic_save_rtx ());
  emit_insn (gen_blockage ());
  DONE;
}")

;; Allocate new stack space and update the saved stack pointer in the
;; frame marker.  The HP C compilers also copy additional words in the
;; frame marker.  The 64-bit compiler copies words at -48, -32 and -24.
;; The 32-bit compiler copies the word at -16 (Static Link).  We
;; currently don't copy these values.
;;
;; Since the copy of the frame marker can't be done atomically, I
;; suspect that using it for unwind purposes may be somewhat unreliable.
;; The HP compilers appear to raise the stack and copy the frame
;; marker in a strict instruction sequence.  This suggests that the
;; unwind library may check for an alloca sequence when ALLOCA_FRAME
;; is set in the callinfo data.  We currently don't set ALLOCA_FRAME
;; as GAS doesn't support it, or try to keep the instructions emitted
;; here in strict sequence.
(define_expand "allocate_stack"
  [(match_operand 0 "" "")
   (match_operand 1 "" "")]
  ""
  "
{
  rtx addr;

  /* Since the stack grows upward, we need to store virtual_stack_dynamic_rtx
     in operand 0 before adjusting the stack.  */
  emit_move_insn (operands[0], virtual_stack_dynamic_rtx);
  anti_adjust_stack (operands[1]);
  if (TARGET_HPUX_UNWIND_LIBRARY)
    {
      addr = gen_rtx_PLUS (word_mode, stack_pointer_rtx,
			   GEN_INT (TARGET_64BIT ? -8 : -4));
      emit_move_insn (gen_rtx_MEM (word_mode, addr), frame_pointer_rtx);
    }
  if (!TARGET_64BIT && flag_pic)
    {
      rtx addr = gen_rtx_PLUS (word_mode, stack_pointer_rtx, GEN_INT (-32));
      emit_move_insn (gen_rtx_MEM (word_mode, addr), pic_offset_table_rtx);
    }
  DONE;
}")

(define_expand "prefetch"
  [(match_operand 0 "address_operand" "")
   (match_operand 1 "const_int_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_PA_20"
{
  int locality = INTVAL (operands[2]);

  gcc_assert (locality >= 0 && locality <= 3);

  /* Change operand[0] to a MEM as we don't have the infrastructure
     to output all the supported address modes for ldw/ldd when we use
     the address directly.  However, we do have it for MEMs.  */
  operands[0] = gen_rtx_MEM (QImode, operands[0]);

  /* If the address isn't valid for the prefetch, replace it.  */
  if (locality)
    {
      if (!prefetch_nocc_operand (operands[0], QImode))
	operands[0]
	  = replace_equiv_address (operands[0],
				   copy_to_mode_reg (Pmode,
						     XEXP (operands[0], 0)));
      emit_insn (gen_prefetch_nocc (operands[0], operands[1], operands[2]));
    }
  else
    {
      if (!prefetch_cc_operand (operands[0], QImode))
	operands[0]
	  = replace_equiv_address (operands[0],
				   copy_to_mode_reg (Pmode,
						     XEXP (operands[0], 0)));
      emit_insn (gen_prefetch_cc (operands[0], operands[1], operands[2]));
    }
  DONE;
})

(define_insn "prefetch_cc"
  [(prefetch (match_operand:QI 0 "prefetch_cc_operand" "RW")
	     (match_operand:SI 1 "const_int_operand" "n")
	     (match_operand:SI 2 "const_int_operand" "n"))]
  "TARGET_PA_20 && operands[2] == const0_rtx"
{
  /* The SL cache-control completor indicates good spatial locality but
     poor temporal locality.  The ldw instruction with a target of general
     register 0 prefetches a cache line for a read.  The ldd instruction
     prefetches a cache line for a write.  */
  static const char * const instr[2] = {
    "ldw%M0,sl %0,%%r0",
    "ldd%M0,sl %0,%%r0"
  };
  int read_or_write = INTVAL (operands[1]);

  gcc_assert (read_or_write >= 0 && read_or_write <= 1);

  return instr [read_or_write];
}
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_insn "prefetch_nocc"
  [(prefetch (match_operand:QI 0 "prefetch_nocc_operand" "A,RQ")
	     (match_operand:SI 1 "const_int_operand" "n,n")
	     (match_operand:SI 2 "const_int_operand" "n,n"))]
  "TARGET_PA_20 && operands[2] != const0_rtx"
{
  /* The ldw instruction with a target of general register 0 prefetches
     a cache line for a read.  The ldd instruction prefetches a cache line
     for a write.  */
  static const char * const instr[2][2] = {
    {
      "ldw RT'%A0,%%r0",
      "ldd RT'%A0,%%r0",
    },
    {
      "ldw%M0 %0,%%r0",
      "ldd%M0 %0,%%r0",
    }
  };
  int read_or_write = INTVAL (operands[1]);

  gcc_assert (which_alternative == 0 || which_alternative == 1);
  gcc_assert (read_or_write >= 0 && read_or_write <= 1);

  return instr [which_alternative][read_or_write];
}
  [(set_attr "type" "load")
   (set_attr "length" "4")])


;; TLS Support
(define_insn "tgd_load"
 [(set (match_operand:SI 0 "register_operand" "=r")
       (unspec:SI [(match_operand 1 "tgd_symbolic_operand" "")] UNSPEC_TLSGD))
  (clobber (reg:SI 1))
  (use (reg:SI 27))]
  ""
  "*
{
  return \"addil LR'%1-$tls_gdidx$,%%r27\;ldo RR'%1-$tls_gdidx$(%%r1),%0\";
}"
  [(set_attr "type" "multi")
   (set_attr "length" "8")])

(define_insn "tgd_load_pic"
 [(set (match_operand:SI 0 "register_operand" "=r")
       (unspec:SI [(match_operand 1 "tgd_symbolic_operand" "")] UNSPEC_TLSGD_PIC))
  (clobber (reg:SI 1))
  (use (reg:SI 19))]
  ""
  "*
{
  return \"addil LT'%1-$tls_gdidx$,%%r19\;ldo RT'%1-$tls_gdidx$(%%r1),%0\";
}"
  [(set_attr "type" "multi")
   (set_attr "length" "8")])

(define_insn "tld_load"
 [(set (match_operand:SI 0 "register_operand" "=r")
       (unspec:SI [(match_operand 1 "tld_symbolic_operand" "")] UNSPEC_TLSLDM))
  (clobber (reg:SI 1))
  (use (reg:SI 27))]
  ""
  "*
{
  return \"addil LR'%1-$tls_ldidx$,%%r27\;ldo RR'%1-$tls_ldidx$(%%r1),%0\";
}"
  [(set_attr "type" "multi")
   (set_attr "length" "8")])

(define_insn "tld_load_pic"
 [(set (match_operand:SI 0 "register_operand" "=r")
       (unspec:SI [(match_operand 1 "tld_symbolic_operand" "")] UNSPEC_TLSLDM_PIC))
  (clobber (reg:SI 1))
  (use (reg:SI 19))]
  ""
  "*
{
  return \"addil LT'%1-$tls_ldidx$,%%r19\;ldo RT'%1-$tls_ldidx$(%%r1),%0\";
}"
  [(set_attr "type" "multi")
   (set_attr "length" "8")])

(define_insn "tld_offset_load"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (plus:SI (unspec:SI [(match_operand 1 "tld_symbolic_operand" "")] 
		 	    UNSPEC_TLSLDO)
		 (match_operand:SI 2 "register_operand" "r")))
   (clobber (reg:SI 1))]
  ""
  "*
{
  return \"addil LR'%1-$tls_dtpoff$,%2\;ldo RR'%1-$tls_dtpoff$(%%r1),%0\"; 
}"
  [(set_attr "type" "multi")
   (set_attr "length" "8")])

(define_insn "tp_load"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI [(const_int 0)] UNSPEC_TP))]
  ""
  "mfctl %%cr27,%0"
  [(set_attr "type" "multi")
   (set_attr "length" "4")])

(define_insn "tie_load"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (unspec:SI [(match_operand 1 "tie_symbolic_operand" "")] UNSPEC_TLSIE))
   (clobber (reg:SI 1))
   (use (reg:SI 27))]
  ""
  "*
{
  return \"addil LR'%1-$tls_ieoff$,%%r27\;ldw RR'%1-$tls_ieoff$(%%r1),%0\";
}"
  [(set_attr "type" "multi")
   (set_attr "length" "8")])

(define_insn "tie_load_pic"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (unspec:SI [(match_operand 1 "tie_symbolic_operand" "")] UNSPEC_TLSIE_PIC))
   (clobber (reg:SI 1))
   (use (reg:SI 19))]
  ""
  "*
{
  return \"addil LT'%1-$tls_ieoff$,%%r19\;ldw RT'%1-$tls_ieoff$(%%r1),%0\";
}"
  [(set_attr "type" "multi")
   (set_attr "length" "8")])

(define_insn "tle_load"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (plus:SI (unspec:SI [(match_operand 1 "tle_symbolic_operand" "")] 
		 	    UNSPEC_TLSLE)
		 (match_operand:SI 2 "register_operand" "r")))
   (clobber (reg:SI 1))]
  ""
  "addil LR'%1-$tls_leoff$,%2\;ldo RR'%1-$tls_leoff$(%%r1),%0"
  [(set_attr "type" "multi")
   (set_attr "length" "8")])
