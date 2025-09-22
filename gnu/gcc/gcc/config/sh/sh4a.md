;; Scheduling description for Renesas SH4a
;; Copyright (C) 2003, 2004 Free Software Foundation, Inc.
;;
;; This file is part of GCC.
;;
;; GNU CC is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.
;;
;; GNU CC is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with GNU CC; see the file COPYING.  If not, write to
;; the Free Software Foundation, 51 Franklin Street, Fifth Floor,
;; Boston, MA 02110-1301, USA.

;; The following description models the SH4A pipeline
;; using the DFA based scheduler.

(define_automaton "sh4a")

(define_cpu_unit "sh4a_ex"   "sh4a")
(define_cpu_unit "sh4a_ls"   "sh4a")
(define_cpu_unit "sh4a_fex"  "sh4a")
(define_cpu_unit "sh4a_fls"  "sh4a")
(define_cpu_unit "sh4a_mult" "sh4a")
(define_cpu_unit "sh4a_fdiv" "sh4a")

;; Decoding is done on the integer pipeline like the
;; sh4. Define issue to be the | of the two pipelines
;; to control how often instructions are issued.
(define_reservation "ID_or" "sh4a_ex|sh4a_ls")
(define_reservation "ID_and" "sh4a_ex+sh4a_ls")


;; =======================================================
;; Locking Descriptions

;; Sh4a_Memory access on the LS pipeline.
(define_cpu_unit "sh4a_memory" "sh4a")

;; Other access on the LS pipeline.
(define_cpu_unit "sh4a_load_store" "sh4a")

;;  The address calculator used for branch instructions.
;; This will be reserved after "issue" of branch instructions
;; and this is to make sure that no two branch instructions
;; can be issued in parallel.
(define_reservation "sh4a_addrcalc" "sh4a_ex")

;; =======================================================
;; Reservations

;; Branch (BF,BF/S,BT,BT/S,BRA,BSR)
;; Group: BR
;; Latency when taken: 2
(define_insn_reservation "sh4a_branch" 2
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "cbranch,jump"))
  "ID_or+sh4a_addrcalc")

;; Jump (JSR,JMP,RTS)
;; Group: BR
;; Latency: 3
(define_insn_reservation "sh4a_jump" 3
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "return,jump_ind"))
  "ID_or+sh4a_addrcalc")

;; RTE
;; Group: CO
;; Latency: 3
(define_insn_reservation "sh4a_rte" 3
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "rte"))
  "ID_and*4")

;; EX Group Single
;; Group: EX
;; Latency: 0
(define_insn_reservation "sh4a_ex" 0
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "insn_class" "ex_group"))
  "sh4a_ex")

;; MOVA
;; Group: LS
;; Latency: 1
(define_insn_reservation "sh4a_mova" 1
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "mova"))
  "sh4a_ls+sh4a_load_store")

;; MOV
;; Group: MT
;; Latency: 0
(define_insn_reservation "sh4a_mov" 0
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "move"))
  "ID_or")

;; Load
;; Group: LS
;; Latency: 3
(define_insn_reservation "sh4a_load" 3
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "load,pcload"))
  "sh4a_ls+sh4a_memory")

(define_insn_reservation "sh4a_load_si" 3
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "load_si,pcload_si"))
  "sh4a_ls+sh4a_memory")

;; Store
;; Group: LS
;; Latency: 0
(define_insn_reservation "sh4a_store" 0
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "store"))
  "sh4a_ls+sh4a_memory")

;; CWB TYPE

;; MOVUA
;; Group: LS
;; Latency: 3
(define_insn_reservation "sh4a_movua" 3
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "movua"))
  "sh4a_ls+sh4a_memory*2")

;; Fixed point multiplication (single)
;; Group: CO
;; Latency: 2
(define_insn_reservation "sh4a_smult" 2
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "smpy"))
  "ID_or+sh4a_mult")

;; Fixed point multiplication (double)
;; Group: CO
;; Latency: 3
(define_insn_reservation "sh4a_dmult" 3
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "dmpy"))
  "ID_or+sh4a_mult")

(define_insn_reservation "sh4a_mac_gp" 3
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "mac_gp"))
  "ID_and")

;; Other MT  group instructions(1 step operations)
;; Group:	MT
;; Latency: 	1
(define_insn_reservation "sh4a_mt" 1
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "mt_group"))
  "ID_or")

;; Floating point reg move
;; Group: LS
;; Latency: 2
(define_insn_reservation "sh4a_freg_mov" 2
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "fmove"))
  "sh4a_ls,sh4a_fls")

;; Single precision floating point computation FCMP/EQ,
;; FCMP/GT, FADD, FLOAT, FMAC, FMUL, FSUB, FTRC, FRVHG, FSCHG
;; Group:	FE
;; Latency: 	3
(define_insn_reservation "sh4a_fp_arith"  3
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "fp"))
  "ID_or,sh4a_fex")

(define_insn_reservation "sh4a_fp_arith_ftrc"  3
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "ftrc_s"))
  "ID_or,sh4a_fex")

;; Single-precision FDIV/FSQRT
;; Group: FE
;; Latency: 20
(define_insn_reservation "sh4a_fdiv" 20
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "fdiv"))
  "ID_or,sh4a_fex+sh4a_fdiv,sh4a_fex")

;; Double Precision floating point computation
;; (FCNVDS, FCNVSD, FLOAT, FTRC)
;; Group:	FE
;; Latency: 	3
(define_insn_reservation "sh4a_dp_float" 3
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "dfp_conv"))
  "ID_or,sh4a_fex")

;; Double-precision floating-point (FADD,FMUL,FSUB)
;; Group:	FE
;; Latency: 	5
(define_insn_reservation "sh4a_fp_double_arith" 5
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "dfp_arith"))
  "ID_or,sh4a_fex*3")

;; Double precision FDIV/SQRT
;; Group:	FE
;; Latency: 	36
(define_insn_reservation "sh4a_dp_div" 36
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "dfdiv"))
  "ID_or,sh4a_fex+sh4a_fdiv,sh4a_fex*2")

;; FSRRA
;; Group: FE
;; Latency: 5
(define_insn_reservation "sh4a_fsrra" 5
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "fsrra"))
  "ID_or,sh4a_fex")

;; FSCA
;; Group: FE
;; Latency: 7
(define_insn_reservation "sh4a_fsca" 7
  (and (eq_attr "cpu" "sh4a")
       (eq_attr "type" "fsca"))
  "ID_or,sh4a_fex*3")
