;; Pentium Scheduling
;; Copyright (C) 2002 Free Software Foundation, Inc.
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
;; Boston, MA 02110-1301, USA.  */
;;
;; The Pentium is an in-order core with two integer pipelines.

;; True for insns that behave like prefixed insns on the Pentium.
(define_attr "pent_prefix" "false,true"
  (if_then_else (ior (eq_attr "prefix_0f" "1")
  		     (ior (eq_attr "prefix_data16" "1")
			  (eq_attr "prefix_rep" "1")))
    (const_string "true")
    (const_string "false")))

;; Categorize how an instruction slots.

;; The non-MMX Pentium slots an instruction with prefixes on U pipe only,
;; while MMX Pentium can slot it on either U or V.  Model non-MMX Pentium
;; rules, because it results in noticeably better code on non-MMX Pentium
;; and doesn't hurt much on MMX.  (Prefixed instructions are not very
;; common, so the scheduler usually has a non-prefixed insn to pair).

(define_attr "pent_pair" "uv,pu,pv,np"
  (cond [(eq_attr "imm_disp" "true")
	   (const_string "np")
	 (ior (eq_attr "type" "alu1,alu,imov,icmp,test,lea,incdec")
	      (and (eq_attr "type" "pop,push")
		   (eq_attr "memory" "!both")))
	   (if_then_else (eq_attr "pent_prefix" "true")
	     (const_string "pu")
	     (const_string "uv"))
	 (eq_attr "type" "ibr")
	   (const_string "pv")
	 (and (eq_attr "type" "ishift")
	      (match_operand 2 "const_int_operand" ""))
	   (const_string "pu")
	 (and (eq_attr "type" "rotate")
	      (match_operand 2 "const1_operand" ""))
	   (const_string "pu")
	 (and (eq_attr "type" "ishift1")
	      (match_operand 1 "const_int_operand" ""))
	   (const_string "pu")
	 (and (eq_attr "type" "rotate1")
	      (match_operand 1 "const1_operand" ""))
	   (const_string "pu")
	 (and (eq_attr "type" "call")
	      (match_operand 0 "constant_call_address_operand" ""))
	   (const_string "pv")
	 (and (eq_attr "type" "callv")
	      (match_operand 1 "constant_call_address_operand" ""))
	   (const_string "pv")
	]
	(const_string "np")))

(define_automaton "pentium,pentium_fpu")

;; Pentium do have U and V pipes.  Instruction to both pipes
;; are always issued together, much like on VLIW.
;;
;;                    predecode
;;                   /         \
;;               decodeu     decodev
;;             /    |           |
;;           fpu executeu    executev
;;            |     |           |
;;           fpu  retire     retire
;;            |
;;           fpu
;; We add dummy "port" pipes allocated only first cycle of
;; instruction to specify this behavior.

(define_cpu_unit "pentium-portu,pentium-portv" "pentium")
(define_cpu_unit "pentium-u,pentium-v" "pentium")
(absence_set "pentium-portu" "pentium-u,pentium-v")
(presence_set "pentium-portv" "pentium-portu")

;; Floating point instructions can overlap with new issue of integer
;; instructions.  We model only first cycle of FP pipeline, as it is
;; fully pipelined.
(define_cpu_unit "pentium-fp" "pentium_fpu")

;; There is non-pipelined multiplier unit used for complex operations.
(define_cpu_unit "pentium-fmul" "pentium_fpu")

;; Pentium preserves memory ordering, so when load-execute-store
;; instruction is executed together with other instruction loading
;; data, the execution of the other instruction is delayed to very
;; last cycle of first instruction, when data are bypassed.
;; We model this by allocating "memory" unit when store is pending
;; and using conflicting load units together.

(define_cpu_unit "pentium-memory" "pentium")
(define_cpu_unit "pentium-load0" "pentium")
(define_cpu_unit "pentium-load1" "pentium")
(absence_set "pentium-load0,pentium-load1" "pentium-memory")

(define_reservation "pentium-load" "(pentium-load0 | pentium-load1)")
(define_reservation "pentium-np" "(pentium-u + pentium-v)")
(define_reservation "pentium-uv" "(pentium-u | pentium-v)")
(define_reservation "pentium-portuv" "(pentium-portu | pentium-portv)")
(define_reservation "pentium-firstu" "(pentium-u + pentium-portu)")
(define_reservation "pentium-firstv" "(pentium-v + pentium-portuv)")
(define_reservation "pentium-firstuv" "(pentium-uv + pentium-portuv)")
(define_reservation "pentium-firstuload" "(pentium-load + pentium-firstu)")
(define_reservation "pentium-firstvload" "(pentium-load + pentium-firstv)")
(define_reservation "pentium-firstuvload" "(pentium-load + pentium-firstuv)
					   | (pentium-firstv,pentium-v,
					      (pentium-load+pentium-firstv))")
(define_reservation "pentium-firstuboth" "(pentium-load + pentium-firstu
					   + pentium-memory)")
(define_reservation "pentium-firstvboth" "(pentium-load + pentium-firstv
					   + pentium-memory)")
(define_reservation "pentium-firstuvboth" "(pentium-load + pentium-firstuv
					    + pentium-memory)
					   | (pentium-firstv,pentium-v,
					      (pentium-load+pentium-firstv))")

;; Few common long latency instructions
(define_insn_reservation "pent_mul" 11
  (and (eq_attr "cpu" "pentium")
       (eq_attr "type" "imul"))
  "pentium-np*11")

(define_insn_reservation "pent_str" 12
  (and (eq_attr "cpu" "pentium")
       (eq_attr "type" "str"))
  "pentium-np*12")

;; Integer division and some other long latency instruction block all
;; units, including the FP pipe.  There is no value in modeling the
;; latency of these instructions and not modeling the latency
;; decreases the size of the DFA.
(define_insn_reservation "pent_block" 1
  (and (eq_attr "cpu" "pentium")
       (eq_attr "type" "idiv"))
  "pentium-np+pentium-fp")

(define_insn_reservation "pent_cld" 2
  (and (eq_attr "cpu" "pentium")
       (eq_attr "type" "cld"))
  "pentium-np*2")

;;  Moves usually have one cycle penalty, but there are exceptions.
(define_insn_reservation "pent_fmov" 1
  (and (eq_attr "cpu" "pentium")
       (and (eq_attr "type" "fmov")
	    (eq_attr "memory" "none,load")))
  "(pentium-fp+pentium-np)")

(define_insn_reservation "pent_fpmovxf" 3
  (and (eq_attr "cpu" "pentium")
       (and (eq_attr "type" "fmov")
	    (and (eq_attr "memory" "load,store")
		 (eq_attr "mode" "XF"))))
  "(pentium-fp+pentium-np)*3")

(define_insn_reservation "pent_fpstore" 2
  (and (eq_attr "cpu" "pentium")
       (and (eq_attr "type" "fmov")
	    (ior (match_operand 1 "immediate_operand" "")
		 (eq_attr "memory" "store"))))
  "(pentium-fp+pentium-np)*2")

(define_insn_reservation "pent_imov" 1
  (and (eq_attr "cpu" "pentium")
       (eq_attr "type" "imov"))
  "pentium-firstuv")

;; Push and pop instructions have 1 cycle latency and special
;; hardware bypass allows them to be paired with other push,pop
;; and call instructions.
(define_bypass 0 "pent_push,pent_pop" "pent_push,pent_pop,pent_call")
(define_insn_reservation "pent_push" 1
  (and (eq_attr "cpu" "pentium")
       (and (eq_attr "type" "push")
	    (eq_attr "memory" "store")))
  "pentium-firstuv")

(define_insn_reservation "pent_pop" 1
  (and (eq_attr "cpu" "pentium")
       (eq_attr "type" "pop,leave"))
  "pentium-firstuv")

;; Call and branch instruction can execute in either pipe, but
;; they are only pairable when in the v pipe.
(define_insn_reservation "pent_call" 10
  (and (eq_attr "cpu" "pentium")
       (eq_attr "type" "call,callv"))
  "pentium-firstv,pentium-v*9")

(define_insn_reservation "pent_branch" 1
  (and (eq_attr "cpu" "pentium")
       (eq_attr "type" "ibr"))
  "pentium-firstv")

;; Floating point instruction dispatch in U pipe, but continue
;; in FP pipeline allowing other instructions to be executed.
(define_insn_reservation "pent_fp" 3
  (and (eq_attr "cpu" "pentium")
       (eq_attr "type" "fop,fistp"))
  "(pentium-firstu+pentium-fp),nothing,nothing")

;; First two cycles of fmul are not pipelined.
(define_insn_reservation "pent_fmul" 3
  (and (eq_attr "cpu" "pentium")
       (eq_attr "type" "fmul"))
  "(pentium-firstuv+pentium-fp+pentium-fmul),pentium-fmul,nothing")

;; Long latency FP instructions overlap with integer instructions,
;; but only last 2 cycles with FP ones.
(define_insn_reservation "pent_fdiv" 39
  (and (eq_attr "cpu" "pentium")
       (eq_attr "type" "fdiv"))
  "(pentium-np+pentium-fp+pentium-fmul),
   (pentium-fp+pentium-fmul)*36,pentium-fmul*2")

(define_insn_reservation "pent_fpspc" 70
  (and (eq_attr "cpu" "pentium")
       (eq_attr "type" "fpspc"))
  "(pentium-np+pentium-fp+pentium-fmul),
   (pentium-fp+pentium-fmul)*67,pentium-fmul*2")

;; Integer instructions.  Load/execute/store takes 3 cycles,
;; load/execute 2 cycles and execute only one cycle.
(define_insn_reservation "pent_uv_both" 3
  (and (eq_attr "cpu" "pentium")
       (and (eq_attr "pent_pair" "uv")
	    (eq_attr "memory" "both")))
  "pentium-firstuvboth,pentium-uv+pentium-memory,pentium-uv")

(define_insn_reservation "pent_u_both" 3
  (and (eq_attr "cpu" "pentium")
       (and (eq_attr "pent_pair" "pu")
	    (eq_attr "memory" "both")))
  "pentium-firstuboth,pentium-u+pentium-memory,pentium-u")

(define_insn_reservation "pent_v_both" 3
  (and (eq_attr "cpu" "pentium")
       (and (eq_attr "pent_pair" "pv")
	    (eq_attr "memory" "both")))
  "pentium-firstvboth,pentium-v+pentium-memory,pentium-v")

(define_insn_reservation "pent_np_both" 3
  (and (eq_attr "cpu" "pentium")
       (and (eq_attr "pent_pair" "np")
	    (eq_attr "memory" "both")))
  "pentium-np,pentium-np,pentium-np")

(define_insn_reservation "pent_uv_load" 2
  (and (eq_attr "cpu" "pentium")
       (and (eq_attr "pent_pair" "uv")
	    (eq_attr "memory" "load")))
  "pentium-firstuvload,pentium-uv")

(define_insn_reservation "pent_u_load" 2
  (and (eq_attr "cpu" "pentium")
       (and (eq_attr "pent_pair" "pu")
	    (eq_attr "memory" "load")))
  "pentium-firstuload,pentium-u")

(define_insn_reservation "pent_v_load" 2
  (and (eq_attr "cpu" "pentium")
       (and (eq_attr "pent_pair" "pv")
	    (eq_attr "memory" "load")))
  "pentium-firstvload,pentium-v")

(define_insn_reservation "pent_np_load" 2
  (and (eq_attr "cpu" "pentium")
       (and (eq_attr "pent_pair" "np")
	    (eq_attr "memory" "load")))
  "pentium-np,pentium-np")

(define_insn_reservation "pent_uv" 1
  (and (eq_attr "cpu" "pentium")
       (and (eq_attr "pent_pair" "uv")
	    (eq_attr "memory" "none")))
  "pentium-firstuv")

(define_insn_reservation "pent_u" 1
  (and (eq_attr "cpu" "pentium")
       (and (eq_attr "pent_pair" "pu")
	    (eq_attr "memory" "none")))
  "pentium-firstu")

(define_insn_reservation "pent_v" 1
  (and (eq_attr "cpu" "pentium")
       (and (eq_attr "pent_pair" "pv")
	    (eq_attr "memory" "none")))
  "pentium-firstv")

(define_insn_reservation "pent_np" 1
  (and (eq_attr "cpu" "pentium")
       (and (eq_attr "pent_pair" "np")
	    (eq_attr "memory" "none")))
  "pentium-np")

