;;- Machine description for FPA co-processor for ARM cpus.
;;  Copyright 1991, 1993, 1994, 1995, 1996, 1996, 1997, 1998, 1999, 2000,
;;  2001, 2002, 2003, 2004, 2005  Free Software Foundation, Inc.
;;  Contributed by Pieter `Tiggr' Schoenmakers (rcpieter@win.tue.nl)
;;  and Martin Simmons (@harleqn.co.uk).
;;  More major hacks by Richard Earnshaw (rearnsha@arm.com).

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
;; along with GCC; see the file COPYING.  If not, write to
;; the Free Software Foundation, 51 Franklin Street, Fifth Floor,
;; Boston, MA 02110-1301, USA.

;; FPA automaton.
(define_automaton "armfp")

;; Floating point unit (FPA)
(define_cpu_unit "fpa" "armfp")

; The fpa10 doesn't really have a memory read unit, but it can start
; to speculatively execute the instruction in the pipeline, provided
; the data is already loaded, so pretend reads have a delay of 2 (and
; that the pipeline is infinite).
(define_cpu_unit "fpa_mem" "arm")

(define_insn_reservation "fdivx" 71
  (and (eq_attr "fpu" "fpa")
       (eq_attr "type" "fdivx"))
  "core+fpa*69")

(define_insn_reservation "fdivd" 59
  (and (eq_attr "fpu" "fpa")
       (eq_attr "type" "fdivd"))
  "core+fpa*57")

(define_insn_reservation "fdivs" 31
  (and (eq_attr "fpu" "fpa")
       (eq_attr "type" "fdivs"))
  "core+fpa*29")

(define_insn_reservation "fmul" 9
  (and (eq_attr "fpu" "fpa")
       (eq_attr "type" "fmul"))
  "core+fpa*7")

(define_insn_reservation "ffmul" 6
  (and (eq_attr "fpu" "fpa")
       (eq_attr "type" "ffmul"))
  "core+fpa*4")

(define_insn_reservation "farith" 4
  (and (eq_attr "fpu" "fpa")
       (eq_attr "type" "farith"))
  "core+fpa*2")

(define_insn_reservation "ffarith" 2
  (and (eq_attr "fpu" "fpa")
       (eq_attr "type" "ffarith"))
  "core+fpa*2")

(define_insn_reservation "r_2_f" 5
  (and (eq_attr "fpu" "fpa")
       (eq_attr "type" "r_2_f"))
  "core+fpa*3")

(define_insn_reservation "f_2_r" 1
  (and (eq_attr "fpu" "fpa")
       (eq_attr "type" "f_2_r"))
  "core+fpa*2")

(define_insn_reservation "f_load" 3
  (and (eq_attr "fpu" "fpa") (eq_attr "type" "f_load"))
  "fpa_mem+core*3")

(define_insn_reservation "f_store" 4
  (and (eq_attr "fpu" "fpa") (eq_attr "type" "f_store"))
  "core*4")

(define_insn_reservation "r_mem_f" 6
  (and (eq_attr "model_wbuf" "no")
    (and (eq_attr "fpu" "fpa") (eq_attr "type" "r_mem_f")))
  "core*6")

(define_insn_reservation "f_mem_r" 7
  (and (eq_attr "fpu" "fpa") (eq_attr "type" "f_mem_r"))
  "core*7")


(define_insn "*addsf3_fpa"
  [(set (match_operand:SF          0 "s_register_operand" "=f,f")
	(plus:SF (match_operand:SF 1 "s_register_operand" "%f,f")
		 (match_operand:SF 2 "arm_float_add_operand"    "fG,H")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "@
   adf%?s\\t%0, %1, %2
   suf%?s\\t%0, %1, #%N2"
  [(set_attr "type" "farith")
   (set_attr "predicable" "yes")]
)

(define_insn "*adddf3_fpa"
  [(set (match_operand:DF          0 "s_register_operand" "=f,f")
	(plus:DF (match_operand:DF 1 "s_register_operand" "%f,f")
		 (match_operand:DF 2 "arm_float_add_operand"    "fG,H")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "@
   adf%?d\\t%0, %1, %2
   suf%?d\\t%0, %1, #%N2"
  [(set_attr "type" "farith")
   (set_attr "predicable" "yes")]
)

(define_insn "*adddf_esfdf_df_fpa"
  [(set (match_operand:DF           0 "s_register_operand" "=f,f")
	(plus:DF (float_extend:DF
		  (match_operand:SF 1 "s_register_operand"  "f,f"))
		 (match_operand:DF  2 "arm_float_add_operand"    "fG,H")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "@
   adf%?d\\t%0, %1, %2
   suf%?d\\t%0, %1, #%N2"
  [(set_attr "type" "farith")
   (set_attr "predicable" "yes")]
)

(define_insn "*adddf_df_esfdf_fpa"
  [(set (match_operand:DF           0 "s_register_operand" "=f")
	(plus:DF (match_operand:DF  1 "s_register_operand"  "f")
		 (float_extend:DF
		  (match_operand:SF 2 "s_register_operand"  "f"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "adf%?d\\t%0, %1, %2"
  [(set_attr "type" "farith")
   (set_attr "predicable" "yes")]
)

(define_insn "*adddf_esfdf_esfdf_fpa"
  [(set (match_operand:DF           0 "s_register_operand" "=f")
	(plus:DF (float_extend:DF 
		  (match_operand:SF 1 "s_register_operand" "f"))
		 (float_extend:DF
		  (match_operand:SF 2 "s_register_operand" "f"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "adf%?d\\t%0, %1, %2"
  [(set_attr "type" "farith")
   (set_attr "predicable" "yes")]
)

(define_insn "*subsf3_fpa"
  [(set (match_operand:SF 0 "s_register_operand" "=f,f")
	(minus:SF (match_operand:SF 1 "arm_float_rhs_operand" "f,G")
		  (match_operand:SF 2 "arm_float_rhs_operand" "fG,f")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "@
   suf%?s\\t%0, %1, %2
   rsf%?s\\t%0, %2, %1"
  [(set_attr "type" "farith")]
)

(define_insn "*subdf3_fpa"
  [(set (match_operand:DF           0 "s_register_operand" "=f,f")
	(minus:DF (match_operand:DF 1 "arm_float_rhs_operand"     "f,G")
		  (match_operand:DF 2 "arm_float_rhs_operand"    "fG,f")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "@
   suf%?d\\t%0, %1, %2
   rsf%?d\\t%0, %2, %1"
  [(set_attr "type" "farith")
   (set_attr "predicable" "yes")]
)

(define_insn "*subdf_esfdf_df_fpa"
  [(set (match_operand:DF            0 "s_register_operand" "=f")
	(minus:DF (float_extend:DF
		   (match_operand:SF 1 "s_register_operand"  "f"))
		  (match_operand:DF  2 "arm_float_rhs_operand"    "fG")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "suf%?d\\t%0, %1, %2"
  [(set_attr "type" "farith")
   (set_attr "predicable" "yes")]
)

(define_insn "*subdf_df_esfdf_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f,f")
	(minus:DF (match_operand:DF 1 "arm_float_rhs_operand" "f,G")
		  (float_extend:DF
		   (match_operand:SF 2 "s_register_operand" "f,f"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "@
   suf%?d\\t%0, %1, %2
   rsf%?d\\t%0, %2, %1"
  [(set_attr "type" "farith")
   (set_attr "predicable" "yes")]
)

(define_insn "*subdf_esfdf_esfdf_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f")
	(minus:DF (float_extend:DF
		   (match_operand:SF 1 "s_register_operand" "f"))
		  (float_extend:DF
		   (match_operand:SF 2 "s_register_operand" "f"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "suf%?d\\t%0, %1, %2"
  [(set_attr "type" "farith")
   (set_attr "predicable" "yes")]
)

(define_insn "*mulsf3_fpa"
  [(set (match_operand:SF 0 "s_register_operand" "=f")
	(mult:SF (match_operand:SF 1 "s_register_operand" "f")
		 (match_operand:SF 2 "arm_float_rhs_operand" "fG")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "fml%?s\\t%0, %1, %2"
  [(set_attr "type" "ffmul")
   (set_attr "predicable" "yes")]
)

(define_insn "*muldf3_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f")
	(mult:DF (match_operand:DF 1 "s_register_operand" "f")
		 (match_operand:DF 2 "arm_float_rhs_operand" "fG")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "muf%?d\\t%0, %1, %2"
  [(set_attr "type" "fmul")
   (set_attr "predicable" "yes")]
)

(define_insn "*muldf_esfdf_df_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f")
	(mult:DF (float_extend:DF
		  (match_operand:SF 1 "s_register_operand" "f"))
		 (match_operand:DF 2 "arm_float_rhs_operand" "fG")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "muf%?d\\t%0, %1, %2"
  [(set_attr "type" "fmul")
   (set_attr "predicable" "yes")]
)

(define_insn "*muldf_df_esfdf_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f")
	(mult:DF (match_operand:DF 1 "s_register_operand" "f")
		 (float_extend:DF
		  (match_operand:SF 2 "s_register_operand" "f"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "muf%?d\\t%0, %1, %2"
  [(set_attr "type" "fmul")
   (set_attr "predicable" "yes")]
)

(define_insn "*muldf_esfdf_esfdf_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f")
	(mult:DF
	 (float_extend:DF (match_operand:SF 1 "s_register_operand" "f"))
	 (float_extend:DF (match_operand:SF 2 "s_register_operand" "f"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "muf%?d\\t%0, %1, %2"
  [(set_attr "type" "fmul")
   (set_attr "predicable" "yes")]
)

;; Division insns

(define_insn "*divsf3_fpa"
  [(set (match_operand:SF 0 "s_register_operand" "=f,f")
	(div:SF (match_operand:SF 1 "arm_float_rhs_operand" "f,G")
		(match_operand:SF 2 "arm_float_rhs_operand" "fG,f")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "@
   fdv%?s\\t%0, %1, %2
   frd%?s\\t%0, %2, %1"
  [(set_attr "type" "fdivs")
   (set_attr "predicable" "yes")]
)

(define_insn "*divdf3_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f,f")
	(div:DF (match_operand:DF 1 "arm_float_rhs_operand" "f,G")
		(match_operand:DF 2 "arm_float_rhs_operand" "fG,f")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "@
   dvf%?d\\t%0, %1, %2
   rdf%?d\\t%0, %2, %1"
  [(set_attr "type" "fdivd")
   (set_attr "predicable" "yes")]
)

(define_insn "*divdf_esfdf_df_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f")
	(div:DF (float_extend:DF
		 (match_operand:SF 1 "s_register_operand" "f"))
		(match_operand:DF 2 "arm_float_rhs_operand" "fG")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "dvf%?d\\t%0, %1, %2"
  [(set_attr "type" "fdivd")
   (set_attr "predicable" "yes")]
)

(define_insn "*divdf_df_esfdf_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f")
	(div:DF (match_operand:DF 1 "arm_float_rhs_operand" "fG")
		(float_extend:DF
		 (match_operand:SF 2 "s_register_operand" "f"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "rdf%?d\\t%0, %2, %1"
  [(set_attr "type" "fdivd")
   (set_attr "predicable" "yes")]
)

(define_insn "*divdf_esfdf_esfdf_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f")
	(div:DF (float_extend:DF
		 (match_operand:SF 1 "s_register_operand" "f"))
		(float_extend:DF
		 (match_operand:SF 2 "s_register_operand" "f"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "dvf%?d\\t%0, %1, %2"
  [(set_attr "type" "fdivd")
   (set_attr "predicable" "yes")]
)

(define_insn "*modsf3_fpa"
  [(set (match_operand:SF 0 "s_register_operand" "=f")
	(mod:SF (match_operand:SF 1 "s_register_operand" "f")
		(match_operand:SF 2 "arm_float_rhs_operand" "fG")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "rmf%?s\\t%0, %1, %2"
  [(set_attr "type" "fdivs")
   (set_attr "predicable" "yes")]
)

(define_insn "*moddf3_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f")
	(mod:DF (match_operand:DF 1 "s_register_operand" "f")
		(match_operand:DF 2 "arm_float_rhs_operand" "fG")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "rmf%?d\\t%0, %1, %2"
  [(set_attr "type" "fdivd")
   (set_attr "predicable" "yes")]
)

(define_insn "*moddf_esfdf_df_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f")
	(mod:DF (float_extend:DF
		 (match_operand:SF 1 "s_register_operand" "f"))
		(match_operand:DF 2 "arm_float_rhs_operand" "fG")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "rmf%?d\\t%0, %1, %2"
  [(set_attr "type" "fdivd")
   (set_attr "predicable" "yes")]
)

(define_insn "*moddf_df_esfdf_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f")
	(mod:DF (match_operand:DF 1 "s_register_operand" "f")
		(float_extend:DF
		 (match_operand:SF 2 "s_register_operand" "f"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "rmf%?d\\t%0, %1, %2"
  [(set_attr "type" "fdivd")
   (set_attr "predicable" "yes")]
)

(define_insn "*moddf_esfdf_esfdf_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f")
	(mod:DF (float_extend:DF
		 (match_operand:SF 1 "s_register_operand" "f"))
		(float_extend:DF
		 (match_operand:SF 2 "s_register_operand" "f"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "rmf%?d\\t%0, %1, %2"
  [(set_attr "type" "fdivd")
   (set_attr "predicable" "yes")]
)

(define_insn "*negsf2_fpa"
  [(set (match_operand:SF         0 "s_register_operand" "=f")
	(neg:SF (match_operand:SF 1 "s_register_operand" "f")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "mnf%?s\\t%0, %1"
  [(set_attr "type" "ffarith")
   (set_attr "predicable" "yes")]
)

(define_insn "*negdf2_fpa"
  [(set (match_operand:DF         0 "s_register_operand" "=f")
	(neg:DF (match_operand:DF 1 "s_register_operand" "f")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "mnf%?d\\t%0, %1"
  [(set_attr "type" "ffarith")
   (set_attr "predicable" "yes")]
)

(define_insn "*negdf_esfdf_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f")
	(neg:DF (float_extend:DF
		 (match_operand:SF 1 "s_register_operand" "f"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "mnf%?d\\t%0, %1"
  [(set_attr "type" "ffarith")
   (set_attr "predicable" "yes")]
)

(define_insn "*abssf2_fpa"
  [(set (match_operand:SF          0 "s_register_operand" "=f")
	 (abs:SF (match_operand:SF 1 "s_register_operand" "f")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "abs%?s\\t%0, %1"
  [(set_attr "type" "ffarith")
   (set_attr "predicable" "yes")]
)

(define_insn "*absdf2_fpa"
  [(set (match_operand:DF         0 "s_register_operand" "=f")
	(abs:DF (match_operand:DF 1 "s_register_operand" "f")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "abs%?d\\t%0, %1"
  [(set_attr "type" "ffarith")
   (set_attr "predicable" "yes")]
)

(define_insn "*absdf_esfdf_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f")
	(abs:DF (float_extend:DF
		 (match_operand:SF 1 "s_register_operand" "f"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "abs%?d\\t%0, %1"
  [(set_attr "type" "ffarith")
   (set_attr "predicable" "yes")]
)

(define_insn "*sqrtsf2_fpa"
  [(set (match_operand:SF 0 "s_register_operand" "=f")
	(sqrt:SF (match_operand:SF 1 "s_register_operand" "f")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "sqt%?s\\t%0, %1"
  [(set_attr "type" "float_em")
   (set_attr "predicable" "yes")]
)

(define_insn "*sqrtdf2_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f")
	(sqrt:DF (match_operand:DF 1 "s_register_operand" "f")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "sqt%?d\\t%0, %1"
  [(set_attr "type" "float_em")
   (set_attr "predicable" "yes")]
)

(define_insn "*sqrtdf_esfdf_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f")
	(sqrt:DF (float_extend:DF
		  (match_operand:SF 1 "s_register_operand" "f"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "sqt%?d\\t%0, %1"
  [(set_attr "type" "float_em")
   (set_attr "predicable" "yes")]
)

(define_insn "*floatsisf2_fpa"
  [(set (match_operand:SF           0 "s_register_operand" "=f")
	(float:SF (match_operand:SI 1 "s_register_operand" "r")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "flt%?s\\t%0, %1"
  [(set_attr "type" "r_2_f")
   (set_attr "predicable" "yes")]
)

(define_insn "*floatsidf2_fpa"
  [(set (match_operand:DF           0 "s_register_operand" "=f")
	(float:DF (match_operand:SI 1 "s_register_operand" "r")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "flt%?d\\t%0, %1"
  [(set_attr "type" "r_2_f")
   (set_attr "predicable" "yes")]
)

(define_insn "*fix_truncsfsi2_fpa"
  [(set (match_operand:SI         0 "s_register_operand" "=r")
	(fix:SI (fix:SF (match_operand:SF 1 "s_register_operand" "f"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "fix%?z\\t%0, %1"
  [(set_attr "type" "f_2_r")
   (set_attr "predicable" "yes")]
)

(define_insn "*fix_truncdfsi2_fpa"
  [(set (match_operand:SI         0 "s_register_operand" "=r")
	(fix:SI (fix:DF (match_operand:DF 1 "s_register_operand" "f"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "fix%?z\\t%0, %1"
  [(set_attr "type" "f_2_r")
   (set_attr "predicable" "yes")]
)

(define_insn "*truncdfsf2_fpa"
  [(set (match_operand:SF 0 "s_register_operand" "=f")
	(float_truncate:SF
	 (match_operand:DF 1 "s_register_operand" "f")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "mvf%?s\\t%0, %1"
  [(set_attr "type" "ffarith")
   (set_attr "predicable" "yes")]
)

(define_insn "*extendsfdf2_fpa"
  [(set (match_operand:DF                  0 "s_register_operand" "=f")
	(float_extend:DF (match_operand:SF 1 "s_register_operand"  "f")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "mvf%?d\\t%0, %1"
  [(set_attr "type" "ffarith")
   (set_attr "predicable" "yes")]
)

(define_insn "*movsf_fpa"
  [(set (match_operand:SF 0 "nonimmediate_operand" "=f,f,f, m,f,r,r,r, m")
	(match_operand:SF 1 "general_operand"      "fG,H,mE,f,r,f,r,mE,r"))]
  "TARGET_ARM
   && TARGET_HARD_FLOAT && TARGET_FPA
   && (GET_CODE (operands[0]) != MEM
       || register_operand (operands[1], SFmode))"
  "@
   mvf%?s\\t%0, %1
   mnf%?s\\t%0, #%N1
   ldf%?s\\t%0, %1
   stf%?s\\t%1, %0
   str%?\\t%1, [%|sp, #-4]!\;ldf%?s\\t%0, [%|sp], #4
   stf%?s\\t%1, [%|sp, #-4]!\;ldr%?\\t%0, [%|sp], #4
   mov%?\\t%0, %1
   ldr%?\\t%0, %1\\t%@ float
   str%?\\t%1, %0\\t%@ float"
  [(set_attr "length" "4,4,4,4,8,8,4,4,4")
   (set_attr "predicable" "yes")
   (set_attr "type"
	 "ffarith,ffarith,f_load,f_store,r_mem_f,f_mem_r,*,load1,store1")
   (set_attr "pool_range" "*,*,1024,*,*,*,*,4096,*")
   (set_attr "neg_pool_range" "*,*,1012,*,*,*,*,4084,*")]
)

(define_insn "*movdf_fpa"
  [(set (match_operand:DF 0 "nonimmediate_operand"
						"=r,Q,r,m,r, f, f,f, m,!f,!r")
	(match_operand:DF 1 "general_operand"
						"Q, r,r,r,mF,fG,H,mF,f,r, f"))]
  "TARGET_ARM
   && TARGET_HARD_FLOAT && TARGET_FPA
   && (GET_CODE (operands[0]) != MEM
       || register_operand (operands[1], DFmode))"
  "*
  {
  switch (which_alternative)
    {
    default:
    case 0: return \"ldm%?ia\\t%m1, %M0\\t%@ double\";
    case 1: return \"stm%?ia\\t%m0, %M1\\t%@ double\";
    case 2: return \"#\";
    case 3: case 4: return output_move_double (operands);
    case 5: return \"mvf%?d\\t%0, %1\";
    case 6: return \"mnf%?d\\t%0, #%N1\";
    case 7: return \"ldf%?d\\t%0, %1\";
    case 8: return \"stf%?d\\t%1, %0\";
    case 9: return output_mov_double_fpa_from_arm (operands);
    case 10: return output_mov_double_arm_from_fpa (operands);
    }
  }
  "
  [(set_attr "length" "4,4,8,8,8,4,4,4,4,8,8")
   (set_attr "predicable" "yes")
   (set_attr "type"
    "load1,store2,*,store2,load1,ffarith,ffarith,f_load,f_store,r_mem_f,f_mem_r")
   (set_attr "pool_range" "*,*,*,*,1020,*,*,1024,*,*,*")
   (set_attr "neg_pool_range" "*,*,*,*,1008,*,*,1008,*,*,*")]
)

;; We treat XFmode as meaning 'internal format'.  It's the right size and we
;; don't use it for anything else.  We only support moving between FPA
;; registers and moving an FPA register to/from memory.
(define_insn "*movxf_fpa"
  [(set (match_operand:XF 0 "nonimmediate_operand" "=f,f,m")
	(match_operand:XF 1 "general_operand" "f,m,f"))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA
   && (register_operand (operands[0], XFmode)
       || register_operand (operands[1], XFmode))"
  "*
  switch (which_alternative)
    {
    default:
    case 0: return \"mvf%?e\\t%0, %1\";
    case 1: if (arm_fpu_arch == FPUTYPE_FPA_EMU2)
	      return \"ldf%?e\\t%0, %1\";
	    return \"lfm%?\\t%0, 1, %1\";
    case 2: if (arm_fpu_arch == FPUTYPE_FPA_EMU2)
	      return \"stf%?e\\t%1, %0\";
	    return \"sfm%?\\t%1, 1, %0\";
    }
  "
  [(set_attr "length" "4,4,4")
   (set_attr "predicable" "yes")
   (set_attr "type" "ffarith,f_load,f_store")]
)

(define_insn "*cmpsf_fpa"
  [(set (reg:CCFP CC_REGNUM)
	(compare:CCFP (match_operand:SF 0 "s_register_operand" "f,f")
		      (match_operand:SF 1 "arm_float_add_operand" "fG,H")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "@
   cmf%?\\t%0, %1
   cnf%?\\t%0, #%N1"
  [(set_attr "conds" "set")
   (set_attr "type" "f_2_r")]
)

(define_insn "*cmpdf_fpa"
  [(set (reg:CCFP CC_REGNUM)
	(compare:CCFP (match_operand:DF 0 "s_register_operand" "f,f")
		      (match_operand:DF 1 "arm_float_add_operand" "fG,H")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "@
   cmf%?\\t%0, %1
   cnf%?\\t%0, #%N1"
  [(set_attr "conds" "set")
   (set_attr "type" "f_2_r")]
)

(define_insn "*cmpesfdf_df_fpa"
  [(set (reg:CCFP CC_REGNUM)
	(compare:CCFP (float_extend:DF
		       (match_operand:SF 0 "s_register_operand" "f,f"))
		      (match_operand:DF 1 "arm_float_add_operand" "fG,H")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "@
   cmf%?\\t%0, %1
   cnf%?\\t%0, #%N1"
  [(set_attr "conds" "set")
   (set_attr "type" "f_2_r")]
)

(define_insn "*cmpdf_esfdf_fpa"
  [(set (reg:CCFP CC_REGNUM)
	(compare:CCFP (match_operand:DF 0 "s_register_operand" "f")
		      (float_extend:DF
		       (match_operand:SF 1 "s_register_operand" "f"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "cmf%?\\t%0, %1"
  [(set_attr "conds" "set")
   (set_attr "type" "f_2_r")]
)

(define_insn "*cmpsf_trap_fpa"
  [(set (reg:CCFPE CC_REGNUM)
	(compare:CCFPE (match_operand:SF 0 "s_register_operand" "f,f")
		       (match_operand:SF 1 "arm_float_add_operand" "fG,H")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "@
   cmf%?e\\t%0, %1
   cnf%?e\\t%0, #%N1"
  [(set_attr "conds" "set")
   (set_attr "type" "f_2_r")]
)

(define_insn "*cmpdf_trap_fpa"
  [(set (reg:CCFPE CC_REGNUM)
	(compare:CCFPE (match_operand:DF 0 "s_register_operand" "f,f")
		       (match_operand:DF 1 "arm_float_add_operand" "fG,H")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "@
   cmf%?e\\t%0, %1
   cnf%?e\\t%0, #%N1"
  [(set_attr "conds" "set")
   (set_attr "type" "f_2_r")]
)

(define_insn "*cmp_esfdf_df_trap_fpa"
  [(set (reg:CCFPE CC_REGNUM)
	(compare:CCFPE (float_extend:DF
			(match_operand:SF 0 "s_register_operand" "f,f"))
		       (match_operand:DF 1 "arm_float_add_operand" "fG,H")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "@
   cmf%?e\\t%0, %1
   cnf%?e\\t%0, #%N1"
  [(set_attr "conds" "set")
   (set_attr "type" "f_2_r")]
)

(define_insn "*cmp_df_esfdf_trap_fpa"
  [(set (reg:CCFPE CC_REGNUM)
	(compare:CCFPE (match_operand:DF 0 "s_register_operand" "f")
		       (float_extend:DF
			(match_operand:SF 1 "s_register_operand" "f"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "cmf%?e\\t%0, %1"
  [(set_attr "conds" "set")
   (set_attr "type" "f_2_r")]
)

(define_insn "*movsfcc_fpa"
  [(set (match_operand:SF 0 "s_register_operand" "=f,f,f,f,f,f,f,f")
	(if_then_else:SF
	 (match_operator 3 "arm_comparison_operator" 
	  [(match_operand 4 "cc_register" "") (const_int 0)])
	 (match_operand:SF 1 "arm_float_add_operand" "0,0,fG,H,fG,fG,H,H")
	 (match_operand:SF 2 "arm_float_add_operand" "fG,H,0,0,fG,H,fG,H")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "@
   mvf%D3s\\t%0, %2
   mnf%D3s\\t%0, #%N2
   mvf%d3s\\t%0, %1
   mnf%d3s\\t%0, #%N1
   mvf%d3s\\t%0, %1\;mvf%D3s\\t%0, %2
   mvf%d3s\\t%0, %1\;mnf%D3s\\t%0, #%N2
   mnf%d3s\\t%0, #%N1\;mvf%D3s\\t%0, %2
   mnf%d3s\\t%0, #%N1\;mnf%D3s\\t%0, #%N2"
  [(set_attr "length" "4,4,4,4,8,8,8,8")
   (set_attr "type" "ffarith")
   (set_attr "conds" "use")]
)

(define_insn "*movdfcc_fpa"
  [(set (match_operand:DF 0 "s_register_operand" "=f,f,f,f,f,f,f,f")
	(if_then_else:DF
	 (match_operator 3 "arm_comparison_operator"
	  [(match_operand 4 "cc_register" "") (const_int 0)])
	 (match_operand:DF 1 "arm_float_add_operand" "0,0,fG,H,fG,fG,H,H")
	 (match_operand:DF 2 "arm_float_add_operand" "fG,H,0,0,fG,H,fG,H")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_FPA"
  "@
   mvf%D3d\\t%0, %2
   mnf%D3d\\t%0, #%N2
   mvf%d3d\\t%0, %1
   mnf%d3d\\t%0, #%N1
   mvf%d3d\\t%0, %1\;mvf%D3d\\t%0, %2
   mvf%d3d\\t%0, %1\;mnf%D3d\\t%0, #%N2
   mnf%d3d\\t%0, #%N1\;mvf%D3d\\t%0, %2
   mnf%d3d\\t%0, #%N1\;mnf%D3d\\t%0, #%N2"
  [(set_attr "length" "4,4,4,4,8,8,8,8")
   (set_attr "type" "ffarith")
   (set_attr "conds" "use")]
)
