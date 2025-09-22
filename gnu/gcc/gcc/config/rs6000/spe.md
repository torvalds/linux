;; e500 SPE description
;; Copyright (C) 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
;; Contributed by Aldy Hernandez (aldy@quesejoda.com)

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
;; along with GCC; see the file COPYING.  If not, write to the
;; Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
;; MA 02110-1301, USA.

(define_constants
  [(SPE_ACC_REGNO	111)
   (SPEFSCR_REGNO	112)

   (CMPDFEQ_GPR		1006)
   (TSTDFEQ_GPR		1007)
   (CMPDFGT_GPR		1008)
   (TSTDFGT_GPR		1009)
   (CMPDFLT_GPR		1010)
   (TSTDFLT_GPR		1011)
   (E500_CR_IOR_COMPARE 1012)
   ])

;; Modes using a 64-bit register.
(define_mode_macro SPE64 [DF V4HI V2SF V1DI V2SI])

(define_insn "*negsf2_gpr"
  [(set (match_operand:SF 0 "gpc_reg_operand" "=r")
        (neg:SF (match_operand:SF 1 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && !TARGET_FPRS"
  "efsneg %0,%1"
  [(set_attr "type" "fpsimple")])

(define_insn "*abssf2_gpr"
  [(set (match_operand:SF 0 "gpc_reg_operand" "=r")
	(abs:SF (match_operand:SF 1 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && !TARGET_FPRS"
  "efsabs %0,%1"
  [(set_attr "type" "fpsimple")])

(define_insn "*nabssf2_gpr"
  [(set (match_operand:SF 0 "gpc_reg_operand" "=r")
	(neg:SF (abs:SF (match_operand:SF 1 "gpc_reg_operand" "r"))))]
  "TARGET_HARD_FLOAT && !TARGET_FPRS"
  "efsnabs %0,%1"
  [(set_attr "type" "fpsimple")])

(define_insn "*addsf3_gpr"
  [(set (match_operand:SF 0 "gpc_reg_operand" "=r")
	(plus:SF (match_operand:SF 1 "gpc_reg_operand" "%r")
		 (match_operand:SF 2 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && !TARGET_FPRS"
  "efsadd %0,%1,%2"
  [(set_attr "type" "fp")])

(define_insn "*subsf3_gpr"
  [(set (match_operand:SF 0 "gpc_reg_operand" "=r")
	(minus:SF (match_operand:SF 1 "gpc_reg_operand" "r")
		  (match_operand:SF 2 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && !TARGET_FPRS"
  "efssub %0,%1,%2"
  [(set_attr "type" "fp")])

(define_insn "*mulsf3_gpr"
  [(set (match_operand:SF 0 "gpc_reg_operand" "=r")
        (mult:SF (match_operand:SF 1 "gpc_reg_operand" "%r")
                 (match_operand:SF 2 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && !TARGET_FPRS"
  "efsmul %0,%1,%2"
  [(set_attr "type" "fp")])

(define_insn "*divsf3_gpr"
  [(set (match_operand:SF 0 "gpc_reg_operand" "=r")
        (div:SF (match_operand:SF 1 "gpc_reg_operand" "r")
                (match_operand:SF 2 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && !TARGET_FPRS"
  "efsdiv %0,%1,%2"
  [(set_attr "type" "vecfdiv")])

;; Floating point conversion instructions.

(define_insn "fixuns_truncdfsi2"
  [(set (match_operand:SI 0 "gpc_reg_operand" "=r")
	(unsigned_fix:SI (match_operand:DF 1 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE"
  "efdctuiz %0,%1"
  [(set_attr "type" "fp")])

(define_insn "spe_extendsfdf2"
  [(set (match_operand:DF 0 "gpc_reg_operand" "=r")
	(float_extend:DF (match_operand:SF 1 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE"
  "efdcfs %0,%1"
  [(set_attr "type" "fp")])

(define_insn "spe_fixuns_truncsfsi2"
  [(set (match_operand:SI 0 "gpc_reg_operand" "=r")
	(unsigned_fix:SI (match_operand:SF 1 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && !TARGET_FPRS"
  "efsctuiz %0,%1"
  [(set_attr "type" "fp")])

(define_insn "spe_fix_truncsfsi2"
  [(set (match_operand:SI 0 "gpc_reg_operand" "=r")
	(fix:SI (match_operand:SF 1 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && !TARGET_FPRS"
  "efsctsiz %0,%1"
  [(set_attr "type" "fp")])

(define_insn "spe_fix_truncdfsi2"
  [(set (match_operand:SI 0 "gpc_reg_operand" "=r")
	(fix:SI (match_operand:DF 1 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE"
  "efdctsiz %0,%1"
  [(set_attr "type" "fp")])

(define_insn "spe_floatunssisf2"
  [(set (match_operand:SF 0 "gpc_reg_operand" "=r")
        (unsigned_float:SF (match_operand:SI 1 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && !TARGET_FPRS"
  "efscfui %0,%1"
  [(set_attr "type" "fp")])

(define_insn "spe_floatunssidf2"
  [(set (match_operand:DF 0 "gpc_reg_operand" "=r")
        (unsigned_float:DF (match_operand:SI 1 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE"
  "efdcfui %0,%1"
  [(set_attr "type" "fp")])

(define_insn "spe_floatsisf2"
  [(set (match_operand:SF 0 "gpc_reg_operand" "=r")
        (float:SF (match_operand:SI 1 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && !TARGET_FPRS"
  "efscfsi %0,%1"
  [(set_attr "type" "fp")])

(define_insn "spe_floatsidf2"
  [(set (match_operand:DF 0 "gpc_reg_operand" "=r")
	(float:DF (match_operand:SI 1 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE"
  "efdcfsi %0,%1"
  [(set_attr "type" "fp")])

;; SPE SIMD instructions

(define_insn "spe_evabs"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(abs:V2SI (match_operand:V2SI 1 "gpc_reg_operand" "r")))]
  "TARGET_SPE"
  "evabs %0,%1"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evandc"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (and:V2SI (match_operand:V2SI 1 "gpc_reg_operand" "r")
		  (not:V2SI (match_operand:V2SI 2 "gpc_reg_operand" "r"))))]
  "TARGET_SPE"
  "evandc %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evand"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (and:V2SI (match_operand:V2SI 1 "gpc_reg_operand" "r")
		  (match_operand:V2SI 2 "gpc_reg_operand" "r")))]
  "TARGET_SPE"
  "evand %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

;; Vector compare instructions

(define_insn "spe_evcmpeq"
  [(set (match_operand:CC 0 "cc_reg_operand" "=y")
	(unspec:CC [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 500))]
  "TARGET_SPE"
  "evcmpeq %0,%1,%2"
  [(set_attr "type" "veccmp")
   (set_attr  "length" "4")])

(define_insn "spe_evcmpgts"
  [(set (match_operand:CC 0 "cc_reg_operand" "=y")
        (unspec:CC [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 501))]
  "TARGET_SPE"
  "evcmpgts %0,%1,%2"
  [(set_attr "type" "veccmp")
   (set_attr  "length" "4")])

(define_insn "spe_evcmpgtu"
  [(set (match_operand:CC 0 "cc_reg_operand" "=y")
        (unspec:CC [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 502))]
  "TARGET_SPE"
  "evcmpgtu %0,%1,%2"
  [(set_attr "type" "veccmp")
   (set_attr  "length" "4")])

(define_insn "spe_evcmplts"
  [(set (match_operand:CC 0 "cc_reg_operand" "=y")
        (unspec:CC [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 503))]
  "TARGET_SPE"
  "evcmplts %0,%1,%2"
  [(set_attr "type" "veccmp")
   (set_attr  "length" "4")])

(define_insn "spe_evcmpltu"
  [(set (match_operand:CC 0 "cc_reg_operand" "=y")
        (unspec:CC [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 504))]
  "TARGET_SPE"
  "evcmpltu %0,%1,%2"
  [(set_attr "type" "veccmp")
   (set_attr  "length" "4")])

;; Floating point vector compare instructions

(define_insn "spe_evfscmpeq"
  [(set (match_operand:CC 0 "cc_reg_operand" "=y")
        (unspec:CC [(match_operand:V2SF 1 "gpc_reg_operand" "r")
		    (match_operand:V2SF 2 "gpc_reg_operand" "r")] 538))
   (clobber (reg:SI SPEFSCR_REGNO))]
  "TARGET_SPE"
  "evfscmpeq %0,%1,%2"
  [(set_attr "type" "veccmp")
   (set_attr  "length" "4")])

(define_insn "spe_evfscmpgt"
  [(set (match_operand:CC 0 "cc_reg_operand" "=y")
        (unspec:CC [(match_operand:V2SF 1 "gpc_reg_operand" "r")
		    (match_operand:V2SF 2 "gpc_reg_operand" "r")] 539))
   (clobber (reg:SI SPEFSCR_REGNO))]
  "TARGET_SPE"
  "evfscmpgt %0,%1,%2"
  [(set_attr "type" "veccmp")
   (set_attr  "length" "4")])

(define_insn "spe_evfscmplt"
  [(set (match_operand:CC 0 "cc_reg_operand" "=y")
        (unspec:CC [(match_operand:V2SF 1 "gpc_reg_operand" "r")
		    (match_operand:V2SF 2 "gpc_reg_operand" "r")] 540))
   (clobber (reg:SI SPEFSCR_REGNO))]
  "TARGET_SPE"
  "evfscmplt %0,%1,%2"
  [(set_attr "type" "veccmp")
   (set_attr  "length" "4")])

(define_insn "spe_evfststeq"
  [(set (match_operand:CC 0 "cc_reg_operand" "=y")
        (unspec:CC [(match_operand:V2SF 1 "gpc_reg_operand" "r")
		    (match_operand:V2SF 2 "gpc_reg_operand" "r")] 541))]
  "TARGET_SPE"
  "evfststeq %0,%1,%2"
  [(set_attr "type" "veccmp")
   (set_attr  "length" "4")])

(define_insn "spe_evfststgt"
  [(set (match_operand:CC 0 "cc_reg_operand" "=y")
        (unspec:CC [(match_operand:V2SF 1 "gpc_reg_operand" "r")
		    (match_operand:V2SF 2 "gpc_reg_operand" "r")] 542))]
  "TARGET_SPE"
  "evfststgt %0,%1,%2"
  [(set_attr "type" "veccmp")
   (set_attr  "length" "4")])

(define_insn "spe_evfststlt"
  [(set (match_operand:CC 0 "cc_reg_operand" "=y")
        (unspec:CC [(match_operand:V2SF 1 "gpc_reg_operand" "r")
		    (match_operand:V2SF 2 "gpc_reg_operand" "r")] 543))]
  "TARGET_SPE"
  "evfststlt %0,%1,%2"
  [(set_attr "type" "veccmp")
   (set_attr  "length" "4")])

;; End of vector compare instructions

(define_insn "spe_evcntlsw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")] 505))]
  "TARGET_SPE"
  "evcntlsw %0,%1"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evcntlzw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")] 506))]
  "TARGET_SPE"
  "evcntlzw %0,%1"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_eveqv"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (not:V2SI (xor:V2SI (match_operand:V2SI 1 "gpc_reg_operand" "r")
			    (match_operand:V2SI 2 "gpc_reg_operand" "r"))))]
  "TARGET_SPE"
  "eveqv %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evextsb"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")] 507))]
  "TARGET_SPE"
  "evextsb %0,%1"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evextsh"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")] 508))]
  "TARGET_SPE"
  "evextsh %0,%1"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evlhhesplat"
  [(set (match_operand:V2SI 0 "gpc_reg_operand"  "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand"   "b")
			   (match_operand:QI 2 "immediate_operand" "i"))))
   (unspec [(const_int 0)] 509)]
  "TARGET_SPE && INTVAL (operands[2]) >= 0 && INTVAL (operands[2]) <= 31"
  "evlhhesplat %0,%2*2(%1)"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evlhhesplatx"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:SI 2 "gpc_reg_operand" "r"))))
   (unspec [(const_int 0)] 510)]
  "TARGET_SPE"
  "evlhhesplatx %0,%1,%2"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evlhhossplat"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:QI 2 "immediate_operand" "i"))))
   (unspec [(const_int 0)] 511)]
  "TARGET_SPE && INTVAL (operands[2]) >= 0 && INTVAL (operands[2]) <= 31"
  "evlhhossplat %0,%2*2(%1)"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evlhhossplatx"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:SI 2 "gpc_reg_operand" "r"))))
   (unspec [(const_int 0)] 512)]
  "TARGET_SPE"
  "evlhhossplatx %0,%1,%2"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evlhhousplat"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:QI 2 "immediate_operand" "i"))))
   (unspec [(const_int 0)] 513)]
  "TARGET_SPE && INTVAL (operands[2]) >= 0 && INTVAL (operands[2]) <= 31"
  "evlhhousplat %0,%2*2(%1)"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evlhhousplatx"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:SI 2 "gpc_reg_operand" "r"))))
   (unspec [(const_int 0)] 514)]
  "TARGET_SPE"
  "evlhhousplatx %0,%1,%2"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evlwhsplat"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:QI 2 "immediate_operand" "i"))))
   (unspec [(const_int 0)] 515)]
  "TARGET_SPE && INTVAL (operands[2]) >= 0 && INTVAL (operands[2]) <= 31"
  "evlwhsplat %0,%2*4(%1)"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evlwhsplatx"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:SI 2 "gpc_reg_operand" "r"))))
   (unspec [(const_int 0)] 516)]
  "TARGET_SPE"
  "evlwhsplatx %0,%1,%2"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evlwwsplat"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:QI 2 "immediate_operand" "i"))))
   (unspec [(const_int 0)] 517)]
  "TARGET_SPE && INTVAL (operands[2]) >= 0 && INTVAL (operands[2]) <= 31"
  "evlwwsplat %0,%2*4(%1)"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evlwwsplatx"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:SI 2 "gpc_reg_operand" "r"))))
   (unspec [(const_int 0)] 518)]
  "TARGET_SPE"
  "evlwwsplatx %0,%1,%2"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evmergehi"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(vec_merge:V2SI (match_operand:V2SI 1 "gpc_reg_operand" "r")
			(vec_select:V2SI
			 (match_operand:V2SI 2 "gpc_reg_operand" "r")
			 (parallel [(const_int 1)
				    (const_int 0)]))
			(const_int 2)))]
  "TARGET_SPE"
  "evmergehi %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evmergehilo"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(vec_merge:V2SI (match_operand:V2SI 1 "gpc_reg_operand" "r")
			(match_operand:V2SI 2 "gpc_reg_operand" "r")
			(const_int 2)))]
  "TARGET_SPE"
  "evmergehilo %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evmergelo"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(vec_merge:V2SI (vec_select:V2SI
			 (match_operand:V2SI 1 "gpc_reg_operand" "r")
			 (parallel [(const_int 1)
				    (const_int 0)]))
			(match_operand:V2SI 2 "gpc_reg_operand" "r")
			(const_int 2)))]
  "TARGET_SPE"
  "evmergelo %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evmergelohi"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(vec_merge:V2SI (vec_select:V2SI
			 (match_operand:V2SI 1 "gpc_reg_operand" "r")
			 (parallel [(const_int 1)
				    (const_int 0)]))
			(vec_select:V2SI
			 (match_operand:V2SI 2 "gpc_reg_operand" "r")
			 (parallel [(const_int 1)
				    (const_int 0)]))
			(const_int 2)))]
  "TARGET_SPE"
  "evmergelohi %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evnand"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (not:V2SI (and:V2SI (match_operand:V2SI 1 "gpc_reg_operand" "r")
                            (match_operand:V2SI 2 "gpc_reg_operand" "r"))))]
  "TARGET_SPE"
  "evnand %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "negv2si2"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (neg:V2SI (match_operand:V2SI 1 "gpc_reg_operand" "r")))]
  "TARGET_SPE"
  "evneg %0,%1"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evnor"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (not:V2SI  (ior:V2SI (match_operand:V2SI 1 "gpc_reg_operand" "r")
                             (match_operand:V2SI 2 "gpc_reg_operand" "r"))))]
  "TARGET_SPE"
  "evnor %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evorc"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (ior:V2SI (match_operand:V2SI 1 "gpc_reg_operand" "r")
		  (not:V2SI (match_operand:V2SI 2 "gpc_reg_operand" "r"))))]
  "TARGET_SPE"
  "evorc %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evor"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (ior:V2SI (match_operand:V2SI 1 "gpc_reg_operand" "r")
		  (match_operand:V2SI 2 "gpc_reg_operand" "r")))]
  "TARGET_SPE"
  "evor %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evrlwi"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (match_operand:QI 2 "immediate_operand" "i")] 519))]
  "TARGET_SPE"
  "evrlwi %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evrlw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 520))]
  "TARGET_SPE"
  "evrlw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evrndw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")] 521))]
  "TARGET_SPE"
  "evrndw %0,%1"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evsel"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (match_operand:CC 3 "cc_reg_operand" "y")] 522))]
  "TARGET_SPE"
  "evsel %0,%1,%2,%3"
  [(set_attr "type" "veccmp")
   (set_attr "length" "4")])

(define_insn "spe_evsel_fs"
  [(set (match_operand:V2SF 0 "gpc_reg_operand" "=r")
	(unspec:V2SF [(match_operand:V2SF 1 "gpc_reg_operand" "r")
		      (match_operand:V2SF 2 "gpc_reg_operand" "r")
		      (match_operand:CC 3 "cc_reg_operand" "y")] 725))]
  "TARGET_SPE"
  "evsel %0,%1,%2,%3"
  [(set_attr "type" "veccmp")
   (set_attr "length" "4")])

(define_insn "spe_evslwi"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (match_operand:QI 2 "immediate_operand" "i")]
		     523))]
  "TARGET_SPE"
  "evslwi %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evslw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 524))]
  "TARGET_SPE"
  "evslw %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evsrwis"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (match_operand:QI 2 "immediate_operand" "i")]
		     525))]
  "TARGET_SPE"
  "evsrwis %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evsrwiu"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (match_operand:QI 2 "immediate_operand" "i")]
		     526))]
  "TARGET_SPE"
  "evsrwiu %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evsrws"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 527))]
  "TARGET_SPE"
  "evsrws %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evsrwu"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 528))]
  "TARGET_SPE"
  "evsrwu %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

;; vector xors

(define_insn "xorv2si3"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (xor:V2SI (match_operand:V2SI 1 "gpc_reg_operand" "r")
		  (match_operand:V2SI 2 "gpc_reg_operand" "r")))]
  "TARGET_SPE"
  "evxor %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "xorv4hi3"
  [(set (match_operand:V4HI 0 "gpc_reg_operand" "=r")
        (xor:V4HI (match_operand:V4HI 1 "gpc_reg_operand" "r")
		  (match_operand:V4HI 2 "gpc_reg_operand" "r")))]
  "TARGET_SPE"
  "evxor %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "xorv1di3"
  [(set (match_operand:V1DI 0 "gpc_reg_operand" "=r")
        (xor:V1DI (match_operand:V1DI 1 "gpc_reg_operand" "r")
		  (match_operand:V1DI 2 "gpc_reg_operand" "r")))]
  "TARGET_SPE"
  "evxor %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

;; end of vector xors

(define_insn "spe_evfsabs"
  [(set (match_operand:V2SF 0 "gpc_reg_operand" "=r")
        (abs:V2SF (match_operand:V2SF 1 "gpc_reg_operand" "r")))]
  "TARGET_SPE"
  "evfsabs %0,%1"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evfsadd"
  [(set (match_operand:V2SF 0 "gpc_reg_operand" "=r")
        (plus:V2SF (match_operand:V2SF 1 "gpc_reg_operand" "r")
		   (match_operand:V2SF 2 "gpc_reg_operand" "r")))
   (clobber (reg:SI SPEFSCR_REGNO))]
  "TARGET_SPE"
  "evfsadd %0,%1,%2"
  [(set_attr "type" "vecfloat")
   (set_attr  "length" "4")])

(define_insn "spe_evfscfsf"
  [(set (match_operand:V2SF 0 "gpc_reg_operand" "=r")
        (unspec:V2SF [(match_operand:V2SF 1 "gpc_reg_operand" "r")] 529))]
  "TARGET_SPE"
  "evfscfsf %0,%1"
  [(set_attr "type" "vecfloat")
   (set_attr  "length" "4")])

(define_insn "spe_evfscfsi"
  [(set (match_operand:V2SF 0 "gpc_reg_operand" "=r")
        (float:V2SF (match_operand:V2SI 1 "gpc_reg_operand" "r")))]
  "TARGET_SPE"
  "evfscfsi %0,%1"
  [(set_attr "type" "vecfloat")
   (set_attr  "length" "4")])

(define_insn "spe_evfscfuf"
  [(set (match_operand:V2SF 0 "gpc_reg_operand" "=r")
        (unspec:V2SF [(match_operand:V2SF 1 "gpc_reg_operand" "r")] 530))]
  "TARGET_SPE"
  "evfscfuf %0,%1"
  [(set_attr "type" "vecfloat")
   (set_attr  "length" "4")])

(define_insn "spe_evfscfui"
  [(set (match_operand:V2SF 0 "gpc_reg_operand" "=r")
	(unspec:V2SF [(match_operand:V2SI 1 "gpc_reg_operand" "r")] 701))]
  "TARGET_SPE"
  "evfscfui %0,%1"
  [(set_attr "type" "vecfloat")
   (set_attr  "length" "4")])

(define_insn "spe_evfsctsf"
  [(set (match_operand:V2SF 0 "gpc_reg_operand" "=r")
        (unspec:V2SF [(match_operand:V2SF 1 "gpc_reg_operand" "r")] 531))]
  "TARGET_SPE"
  "evfsctsf %0,%1"
  [(set_attr "type" "vecfloat")
   (set_attr  "length" "4")])

(define_insn "spe_evfsctsi"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(unspec:V2SI [(match_operand:V2SF 1 "gpc_reg_operand" "r")] 532))]
  "TARGET_SPE"
  "evfsctsi %0,%1"
  [(set_attr "type" "vecfloat")
   (set_attr  "length" "4")])

(define_insn "spe_evfsctsiz"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(unspec:V2SI [(match_operand:V2SF 1 "gpc_reg_operand" "r")] 533))]
  "TARGET_SPE"
  "evfsctsiz %0,%1"
  [(set_attr "type" "vecfloat")
   (set_attr  "length" "4")])

(define_insn "spe_evfsctuf"
  [(set (match_operand:V2SF 0 "gpc_reg_operand" "=r")
        (unspec:V2SF [(match_operand:V2SF 1 "gpc_reg_operand" "r")] 534))]
  "TARGET_SPE"
  "evfsctuf %0,%1"
  [(set_attr "type" "vecfloat")
   (set_attr  "length" "4")])

(define_insn "spe_evfsctui"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SF 1 "gpc_reg_operand" "r")] 535))]
  "TARGET_SPE"
  "evfsctui %0,%1"
  [(set_attr "type" "vecfloat")
   (set_attr  "length" "4")])

(define_insn "spe_evfsctuiz"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SF 1 "gpc_reg_operand" "r")] 536))]
  "TARGET_SPE"
  "evfsctuiz %0,%1"
  [(set_attr "type" "vecfloat")
   (set_attr  "length" "4")])

(define_insn "spe_evfsdiv"
  [(set (match_operand:V2SF 0 "gpc_reg_operand" "=r")
        (div:V2SF (match_operand:V2SF 1 "gpc_reg_operand" "r")
		  (match_operand:V2SF 2 "gpc_reg_operand" "r")))
   (clobber (reg:SI SPEFSCR_REGNO))]
  "TARGET_SPE"
  "evfsdiv %0,%1,%2"
  [(set_attr "type" "vecfdiv")
   (set_attr  "length" "4")])

(define_insn "spe_evfsmul"
  [(set (match_operand:V2SF 0 "gpc_reg_operand" "=r")
        (mult:V2SF (match_operand:V2SF 1 "gpc_reg_operand" "r")
		   (match_operand:V2SF 2 "gpc_reg_operand" "r")))
   (clobber (reg:SI SPEFSCR_REGNO))]
  "TARGET_SPE"
  "evfsmul %0,%1,%2"
  [(set_attr "type" "vecfloat")
   (set_attr  "length" "4")])

(define_insn "spe_evfsnabs"
  [(set (match_operand:V2SF 0 "gpc_reg_operand" "=r")
	(unspec:V2SF [(match_operand:V2SF 1 "gpc_reg_operand" "r")] 537))]
  "TARGET_SPE"
  "evfsnabs %0,%1"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evfsneg"
  [(set (match_operand:V2SF 0 "gpc_reg_operand" "=r")
        (neg:V2SF (match_operand:V2SF 1 "gpc_reg_operand" "r")))]
  "TARGET_SPE"
  "evfsneg %0,%1"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evfssub"
  [(set (match_operand:V2SF 0 "gpc_reg_operand" "=r")
        (minus:V2SF (match_operand:V2SF 1 "gpc_reg_operand" "r")
		    (match_operand:V2SF 2 "gpc_reg_operand" "r")))
   (clobber (reg:SI SPEFSCR_REGNO))]
  "TARGET_SPE"
  "evfssub %0,%1,%2"
  [(set_attr "type" "vecfloat")
   (set_attr  "length" "4")])

;; SPE SIMD load instructions.

;; Only the hardware engineer who designed the SPE understands the
;; plethora of load and store instructions ;-).  We have no way of
;; differentiating between them with RTL so use an unspec of const_int 0 
;; to avoid identical RTL.

(define_insn "spe_evldd"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:QI 2 "immediate_operand" "i"))))
   (unspec [(const_int 0)] 544)]
  "TARGET_SPE && INTVAL (operands[2]) >= 0 && INTVAL (operands[2]) <= 31"
  "evldd %0,%2*8(%1)"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evlddx"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:SI 2 "gpc_reg_operand" "r"))))
   (unspec [(const_int 0)] 545)]
  "TARGET_SPE"
  "evlddx %0,%1,%2"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evldh"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:QI 2 "immediate_operand" "i"))))
   (unspec [(const_int 0)] 546)]
  "TARGET_SPE && INTVAL (operands[2]) >= 0 && INTVAL (operands[2]) <= 31"
  "evldh %0,%2*8(%1)"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evldhx"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:SI 2 "gpc_reg_operand" "r"))))
   (unspec [(const_int 0)] 547)]
  "TARGET_SPE"
  "evldhx %0,%1,%2"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evldw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:QI 2 "immediate_operand" "i"))))
   (unspec [(const_int 0)] 548)]
  "TARGET_SPE && INTVAL (operands[2]) >= 0 && INTVAL (operands[2]) <= 31"
  "evldw %0,%2*8(%1)"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evldwx"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:SI 2 "gpc_reg_operand" "r"))))
   (unspec [(const_int 0)] 549)]
  "TARGET_SPE"
  "evldwx %0,%1,%2"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evlwhe"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:QI 2 "immediate_operand" "i"))))
   (unspec [(const_int 0)] 550)]
  "TARGET_SPE && INTVAL (operands[2]) >= 0 && INTVAL (operands[2]) <= 31"
  "evlwhe %0,%2*4(%1)"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evlwhex"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:SI 2 "gpc_reg_operand" "r"))))
   (unspec [(const_int 0)] 551)]
  "TARGET_SPE"
  "evlwhex %0,%1,%2"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evlwhos"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:QI 2 "immediate_operand" "i"))))
   (unspec [(const_int 0)] 552)]
  "TARGET_SPE && INTVAL (operands[2]) >= 0 && INTVAL (operands[2]) <= 31"
  "evlwhos %0,%2*4(%1)"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evlwhosx"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:SI 2 "gpc_reg_operand" "r"))))
   (unspec [(const_int 0)] 553)]
  "TARGET_SPE"
  "evlwhosx %0,%1,%2"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evlwhou"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:QI 2 "immediate_operand" "i"))))
   (unspec [(const_int 0)] 554)]
  "TARGET_SPE && INTVAL (operands[2]) >= 0 && INTVAL (operands[2]) <= 31"
  "evlwhou %0,%2*4(%1)"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_evlwhoux"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
	(mem:V2SI (plus:SI (match_operand:SI 1 "gpc_reg_operand" "b")
			   (match_operand:SI 2 "gpc_reg_operand" "r"))))
   (unspec [(const_int 0)] 555)]
  "TARGET_SPE"
  "evlwhoux %0,%1,%2"
  [(set_attr "type" "vecload")
   (set_attr  "length" "4")])

(define_insn "spe_brinc"
  [(set (match_operand:SI 0 "gpc_reg_operand" "=r")
        (unspec:SI [(match_operand:SI 1 "gpc_reg_operand" "r")
		    (match_operand:SI 2 "gpc_reg_operand" "r")] 556))]
  "TARGET_SPE"
  "brinc %0,%1,%2"
  [(set_attr "type" "brinc")
   (set_attr  "length" "4")])

(define_insn "spe_evmhegsmfaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 557))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhegsmfaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhegsmfan"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 558))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhegsmfan %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhegsmiaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 559))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhegsmiaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhegsmian"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 560))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhegsmian %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhegumiaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 561))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhegumiaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhegumian"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 562))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhegumian %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhesmfaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 563))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhesmfaaw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhesmfanw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 564))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhesmfanw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhesmfa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 565))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhesmfa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhesmf"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 566))]
  "TARGET_SPE"
  "evmhesmf %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhesmiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 567))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhesmiaaw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhesmianw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 568))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhesmianw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhesmia"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 569))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhesmia %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhesmi"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 570))]
  "TARGET_SPE"
  "evmhesmi %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhessfaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 571))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhessfaaw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhessfanw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 572))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhessfanw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhessfa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 573))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhessfa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhessf"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 574))
   (clobber (reg:SI SPEFSCR_REGNO))]
  "TARGET_SPE"
  "evmhessf %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhessiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 575))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhessiaaw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhessianw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 576))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhessianw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmheumiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 577))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmheumiaaw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmheumianw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 578))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmheumianw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmheumia"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 579))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmheumia %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmheumi"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 580))]
  "TARGET_SPE"
  "evmheumi %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmheusiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 581))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmheusiaaw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmheusianw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 582))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmheusianw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhogsmfaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 583))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhogsmfaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhogsmfan"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 584))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhogsmfan %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhogsmiaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 585))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhogsmiaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhogsmian"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 586))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhogsmian %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhogumiaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 587))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhogumiaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhogumian"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 588))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhogumian %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhosmfaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 589))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhosmfaaw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhosmfanw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 590))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhosmfanw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhosmfa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 591))]
  "TARGET_SPE"
  "evmhosmfa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhosmf"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 592))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhosmf %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhosmiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 593))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhosmiaaw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhosmianw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 594))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhosmianw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhosmia"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 595))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhosmia %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhosmi"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 596))]
  "TARGET_SPE"
  "evmhosmi %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhossfaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 597))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhossfaaw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhossfanw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 598))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhossfanw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhossfa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 599))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhossfa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhossf"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 600))
   (clobber (reg:SI SPEFSCR_REGNO))]
  "TARGET_SPE"
  "evmhossf %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhossiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 601))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhossiaaw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhossianw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 602))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhossianw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhoumiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 603))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhoumiaaw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhoumianw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 604))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhoumianw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhoumia"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 605))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhoumia %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhoumi"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 606))]
  "TARGET_SPE"
  "evmhoumi %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhousiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 607))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhousiaaw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmhousianw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 608))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmhousianw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmmlssfa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 609))]
  "TARGET_SPE"
  "evmmlssfa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmmlssf"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 610))]
  "TARGET_SPE"
  "evmmlssf %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhsmfa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 611))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhsmfa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhsmf"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 612))]
  "TARGET_SPE"
  "evmwhsmf %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhsmia"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 613))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhsmia %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhsmi"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 614))]
  "TARGET_SPE"
  "evmwhsmi %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhssfa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 615))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhssfa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhusian"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 626))]
  "TARGET_SPE"
  "evmwhusian %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhssf"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 628))
   (clobber (reg:SI SPEFSCR_REGNO))]
  "TARGET_SPE"
  "evmwhssf %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhumia"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 629))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhumia %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhumi"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 630))]
  "TARGET_SPE"
  "evmwhumi %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwlsmiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 635))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwlsmiaaw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwlsmianw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 636))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwlsmianw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwlssiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 641))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwlssiaaw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwlssianw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 642))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwlssianw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwlumiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 643))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwlumiaaw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwlumianw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 644))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwlumianw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwlumia"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 645))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwlumia %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwlumi"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 646))]
  "TARGET_SPE"
  "evmwlumi %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwlusiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 647))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwlusiaaw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwlusianw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 648))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwlusianw %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwsmfaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 649))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwsmfaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwsmfan"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 650))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwsmfan %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwsmfa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 651))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwsmfa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwsmf"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 652))]
  "TARGET_SPE"
  "evmwsmf %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwsmiaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 653))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwsmiaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwsmian"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 654))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwsmian %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwsmia"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 655))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwsmia %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwsmi"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 656))]
  "TARGET_SPE"
  "evmwsmi %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwssfaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 657))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwssfaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwssfan"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 658))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwssfan %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwssfa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 659))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwssfa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwssf"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 660))
   (clobber (reg:SI SPEFSCR_REGNO))]
  "TARGET_SPE"
  "evmwssf %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwumiaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 661))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwumiaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwumian"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 662))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwumian %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwumia"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 663))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwumia %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwumi"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 664))]
  "TARGET_SPE"
  "evmwumi %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evaddw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (plus:V2SI (match_operand:V2SI 1 "gpc_reg_operand" "r")
		   (match_operand:V2SI 2 "gpc_reg_operand" "r")))]
  "TARGET_SPE"
  "evaddw %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evaddusiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 673))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evaddusiaaw %0,%1"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evaddumiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 674))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evaddumiaaw %0,%1"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evaddssiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 675))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evaddssiaaw %0,%1"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evaddsmiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 676))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evaddsmiaaw %0,%1"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evaddiw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (match_operand:QI 2 "immediate_operand" "i")] 677))]
  "TARGET_SPE"
  "evaddiw %0,%1,%2"
  [(set_attr "type" "vecsimple")
   (set_attr  "length" "4")])

(define_insn "spe_evsubifw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (match_operand:QI 2 "immediate_operand" "i")] 678))]
  "TARGET_SPE"
  "evsubifw %0,%2,%1"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evsubfw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (minus:V2SI (match_operand:V2SI 1 "gpc_reg_operand" "r")
		    (match_operand:V2SI 2 "gpc_reg_operand" "r")))]
  "TARGET_SPE"
  "evsubfw %0,%2,%1"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evsubfusiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 679))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evsubfusiaaw %0,%1"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evsubfumiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 680))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evsubfumiaaw %0,%1"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evsubfssiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 681))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evsubfssiaaw %0,%1"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evsubfsmiaaw"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
		      (reg:V2SI SPE_ACC_REGNO)] 682))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evsubfsmiaaw %0,%1"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmra"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (match_operand:V2SI 1 "gpc_reg_operand" "r"))
   (set (reg:V2SI SPE_ACC_REGNO)
	(unspec:V2SI [(match_dup 1)] 726))]
  "TARGET_SPE"
  "evmra %0,%1"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evdivws"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (div:V2SI (match_operand:V2SI 1 "gpc_reg_operand" "r")
		  (match_operand:V2SI 2 "gpc_reg_operand" "r")))
   (clobber (reg:SI SPEFSCR_REGNO))]
  "TARGET_SPE"
  "evdivws %0,%1,%2"
  [(set_attr "type" "vecdiv")
   (set_attr  "length" "4")])

(define_insn "spe_evdivwu"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (udiv:V2SI (match_operand:V2SI 1 "gpc_reg_operand" "r")
		   (match_operand:V2SI 2 "gpc_reg_operand" "r")))
      (clobber (reg:SI SPEFSCR_REGNO))]
  "TARGET_SPE"
  "evdivwu %0,%1,%2"
  [(set_attr "type" "vecdiv")
   (set_attr  "length" "4")])

(define_insn "spe_evsplatfi"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:QI 1 "immediate_operand" "i")] 684))]
  "TARGET_SPE"
  "evsplatfi %0,%1"
  [(set_attr "type" "vecperm")
   (set_attr  "length" "4")])

(define_insn "spe_evsplati"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:QI 1 "immediate_operand" "i")] 685))]
  "TARGET_SPE"
  "evsplati %0,%1"
  [(set_attr "type" "vecperm")
   (set_attr  "length" "4")])

(define_insn "spe_evstdd"
  [(set (mem:V2SI (plus:SI (match_operand:SI 0 "gpc_reg_operand" "b")
			   (match_operand:QI 1 "immediate_operand" "i")))
	(match_operand:V2SI 2 "gpc_reg_operand" "r"))
   (unspec [(const_int 0)] 686)]
  "TARGET_SPE && INTVAL (operands[1]) >= 0 && INTVAL (operands[1]) <= 31"
  "evstdd %2,%1*8(%0)"
  [(set_attr "type" "vecstore")
   (set_attr  "length" "4")])

(define_insn "spe_evstddx"
  [(set (mem:V2SI (plus:SI (match_operand:SI 0 "gpc_reg_operand" "b")
			   (match_operand:SI 1 "gpc_reg_operand" "r")))
	(match_operand:V2SI 2 "gpc_reg_operand" "r"))
   (unspec [(const_int 0)] 687)]
  "TARGET_SPE"
  "evstddx %2,%0,%1"
  [(set_attr "type" "vecstore")
   (set_attr  "length" "4")])

(define_insn "spe_evstdh"
  [(set (mem:V2SI (plus:SI (match_operand:SI 0 "gpc_reg_operand" "b")
			   (match_operand:QI 1 "immediate_operand" "i")))
	(match_operand:V2SI 2 "gpc_reg_operand" "r"))
   (unspec [(const_int 0)] 688)]
  "TARGET_SPE && INTVAL (operands[1]) >= 0 && INTVAL (operands[1]) <= 31"
  "evstdh %2,%1*8(%0)"
  [(set_attr "type" "vecstore")
   (set_attr  "length" "4")])

(define_insn "spe_evstdhx"
  [(set (mem:V2SI (plus:SI (match_operand:SI 0 "gpc_reg_operand" "b")
			   (match_operand:SI 1 "gpc_reg_operand" "r")))
	(match_operand:V2SI 2 "gpc_reg_operand" "r"))
   (unspec [(const_int 0)] 689)]
  "TARGET_SPE"
  "evstdhx %2,%0,%1"
  [(set_attr "type" "vecstore")
   (set_attr  "length" "4")])

(define_insn "spe_evstdw"
  [(set (mem:V2SI (plus:SI (match_operand:SI 0 "gpc_reg_operand" "b")
			   (match_operand:QI 1 "immediate_operand" "i")))
	(match_operand:V2SI 2 "gpc_reg_operand" "r"))
   (unspec [(const_int 0)] 690)]
  "TARGET_SPE && INTVAL (operands[1]) >= 0 && INTVAL (operands[1]) <= 31"
  "evstdw %2,%1*8(%0)"
  [(set_attr "type" "vecstore")
   (set_attr  "length" "4")])

(define_insn "spe_evstdwx"
  [(set (mem:V2SI (plus:SI (match_operand:SI 0 "gpc_reg_operand" "b")
			   (match_operand:SI 1 "gpc_reg_operand" "r")))
	(match_operand:V2SI 2 "gpc_reg_operand" "r"))
   (unspec [(const_int 0)] 691)]
  "TARGET_SPE"
  "evstdwx %2,%0,%1"
  [(set_attr "type" "vecstore")
   (set_attr  "length" "4")])

(define_insn "spe_evstwhe"
  [(set (mem:V2SI (plus:SI (match_operand:SI 0 "gpc_reg_operand" "b")
			   (match_operand:QI 1 "immediate_operand" "i")))
	(match_operand:V2SI 2 "gpc_reg_operand" "r"))
   (unspec [(const_int 0)] 692)]
  "TARGET_SPE && INTVAL (operands[1]) >= 0 && INTVAL (operands[1]) <= 31"
  "evstwhe %2,%1*4(%0)"
  [(set_attr "type" "vecstore")
   (set_attr  "length" "4")])

(define_insn "spe_evstwhex"
  [(set (mem:V2SI (plus:SI (match_operand:SI 0 "gpc_reg_operand" "b")
			   (match_operand:SI 1 "gpc_reg_operand" "r")))
	(match_operand:V2SI 2 "gpc_reg_operand" "r"))
   (unspec [(const_int 0)] 693)]
  "TARGET_SPE"
  "evstwhex %2,%0,%1"
  [(set_attr "type" "vecstore")
   (set_attr  "length" "4")])

(define_insn "spe_evstwho"
  [(set (mem:V2SI (plus:SI (match_operand:SI 0 "gpc_reg_operand" "b")
			   (match_operand:QI 1 "immediate_operand" "i")))
	(match_operand:V2SI 2 "gpc_reg_operand" "r"))
   (unspec [(const_int 0)] 694)]
  "TARGET_SPE && INTVAL (operands[1]) >= 0 && INTVAL (operands[1]) <= 31"
  "evstwho %2,%1*4(%0)"
  [(set_attr "type" "vecstore")
   (set_attr  "length" "4")])

(define_insn "spe_evstwhox"
  [(set (mem:V2SI (plus:SI (match_operand:SI 0 "gpc_reg_operand" "b")
			   (match_operand:SI 1 "gpc_reg_operand" "r")))
	(match_operand:V2SI 2 "gpc_reg_operand" "r"))
   (unspec [(const_int 0)] 695)]
  "TARGET_SPE"
  "evstwhox %2,%0,%1"
  [(set_attr "type" "vecstore")
   (set_attr  "length" "4")])

(define_insn "spe_evstwwe"
  [(set (mem:V2SI (plus:SI (match_operand:SI 0 "gpc_reg_operand" "b")
			   (match_operand:QI 1 "immediate_operand" "i")))
	(match_operand:V2SI 2 "gpc_reg_operand" "r"))
   (unspec [(const_int 0)] 696)]
  "TARGET_SPE && INTVAL (operands[1]) >= 0 && INTVAL (operands[1]) <= 31"
  "evstwwe %2,%1*4(%0)"
  [(set_attr "type" "vecstore")
   (set_attr  "length" "4")])

(define_insn "spe_evstwwex"
  [(set (mem:V2SI (plus:SI (match_operand:SI 0 "gpc_reg_operand" "b")
			   (match_operand:SI 1 "gpc_reg_operand" "r")))
	(match_operand:V2SI 2 "gpc_reg_operand" "r"))
   (unspec [(const_int 0)] 697)]
  "TARGET_SPE"
  "evstwwex %2,%0,%1"
  [(set_attr "type" "vecstore")
   (set_attr  "length" "4")])

(define_insn "spe_evstwwo"
  [(set (mem:V2SI (plus:SI (match_operand:SI 0 "gpc_reg_operand" "b")
			   (match_operand:QI 1 "immediate_operand" "i")))
	(match_operand:V2SI 2 "gpc_reg_operand" "r"))
   (unspec [(const_int 0)] 698)]
  "TARGET_SPE && INTVAL (operands[1]) >= 0 && INTVAL (operands[1]) <= 31"
  "evstwwo %2,%1*4(%0)"
  [(set_attr "type" "vecstore")
   (set_attr  "length" "4")])

(define_insn "spe_evstwwox"
  [(set (mem:V2SI (plus:SI (match_operand:SI 0 "gpc_reg_operand" "b")
			   (match_operand:SI 1 "gpc_reg_operand" "r")))
	(match_operand:V2SI 2 "gpc_reg_operand" "r"))
   (unspec [(const_int 0)] 699)]
  "TARGET_SPE"
  "evstwwox %2,%0,%1"
  [(set_attr "type" "vecstore")
   (set_attr  "length" "4")])

;; Double-precision floating point instructions.

;; FIXME: Add o=r option.
(define_insn "*frob_df_di"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=r,r")
        (subreg:DF (match_operand:DI 1 "input_operand" "r,m") 0))]
  "TARGET_E500_DOUBLE"
  "@
   evmergelo %0,%1,%L1
   evldd%X1 %0,%y1")

(define_insn "*frob_di_df"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=&r")
        (subreg:DI (match_operand:DF 1 "input_operand" "r") 0))]
  "TARGET_E500_DOUBLE"
  "evmergehi %0,%1,%1\;mr %L0,%1"
  [(set_attr "length" "8")])

(define_insn "*frob_di_df_2"
  [(set (subreg:DF (match_operand:DI 0 "register_operand" "=&r,r") 0)
	(match_operand:DF 1 "input_operand" "r,m"))]
  "TARGET_E500_DOUBLE"
  "*
{
  switch (which_alternative)
    {
    default: 
      gcc_unreachable ();
    case 0:
      return \"evmergehi %0,%1,%1\;mr %L0,%1\";
    case 1:
      /* If the address is not offsettable we need to load the whole
	 doubleword into a 64-bit register and then copy the high word
	 to form the correct output layout.  */
      if (!offsettable_nonstrict_memref_p (operands[1]))
	return \"evldd%X1 %L0,%y1\;evmergehi %0,%L0,%L0\";
      /* If the low-address word is used in the address, we must load
	it last.  Otherwise, load it first.  Note that we cannot have
	auto-increment in that case since the address register is
	known to be dead.  */
      if (refers_to_regno_p (REGNO (operands[0]), REGNO (operands[0]) + 1,
			     operands[1], 0))
	return \"{l|lwz} %L0,%L1\;{l|lwz} %0,%1\";
      else
        return \"{l%U1%X1|lwz%U1%X1} %0,%1\;{l|lwz} %L0,%L1\";
    }
}"
  [(set_attr "length" "8,8")])

(define_insn "*mov_si<mode>_e500_subreg0"
  [(set (subreg:SI (match_operand:SPE64 0 "register_operand" "+r,&r") 0)
	(match_operand:SI 1 "input_operand" "r,m"))]
  "(TARGET_E500_DOUBLE && <MODE>mode == DFmode) || (TARGET_SPE && <MODE>mode != DFmode)"
  "@
   evmergelo %0,%1,%0
   evmergelohi %0,%0,%0\;{l%U1%X1|lwz%U1%X1} %0,%1\;evmergelohi %0,%0,%0")

;; ??? Could use evstwwe for memory stores in some cases, depending on
;; the offset.
(define_insn "*mov_si<mode>_e500_subreg0_2"
  [(set (match_operand:SI 0 "rs6000_nonimmediate_operand" "+r,m")
	(subreg:SI (match_operand:SPE64 1 "register_operand" "+r,&r") 0))]
  "(TARGET_E500_DOUBLE && <MODE>mode == DFmode) || (TARGET_SPE && <MODE>mode != DFmode)"
  "@
   evmergehi %0,%0,%1
   evmergelohi %1,%1,%1\;{st%U0%X0|stw%U0%X0} %1,%0")

(define_insn "*mov_si<mode>_e500_subreg4"
  [(set (subreg:SI (match_operand:SPE64 0 "register_operand" "+r,r") 4)
	(match_operand:SI 1 "input_operand" "r,m"))]
  "(TARGET_E500_DOUBLE && <MODE>mode == DFmode) || (TARGET_SPE && <MODE>mode != DFmode)"
  "@
   mr %0,%1
   {l%U1%X1|lwz%U1%X1} %0,%1")

(define_insn "*mov_si<mode>_e500_subreg4_2"
  [(set (match_operand:SI 0 "rs6000_nonimmediate_operand" "+r,m")
	(subreg:SI (match_operand:SPE64 1 "register_operand" "r,r") 4))]
  "(TARGET_E500_DOUBLE && <MODE>mode == DFmode) || (TARGET_SPE && <MODE>mode != DFmode)"
  "@
   mr %0,%1
   {st%U0%X0|stw%U0%X0} %1,%0")

;; FIXME: Allow r=CONST0.
(define_insn "*movdf_e500_double"
  [(set (match_operand:DF 0 "rs6000_nonimmediate_operand" "=r,r,m")
	(match_operand:DF 1 "input_operand" "r,m,r"))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE
    && (gpc_reg_operand (operands[0], DFmode)
        || gpc_reg_operand (operands[1], DFmode))"
  "*
 {
   switch (which_alternative)
     {
     case 0:
       return \"evor %0,%1,%1\";
     case 1:
       return \"evldd%X1 %0,%y1\";
     case 2:
       return \"evstdd%X0 %1,%y0\";
     default:
       gcc_unreachable ();
     }
 }"
  [(set_attr "type" "*,vecload,vecstore")
   (set_attr "length" "*,*,*")])

(define_insn "spe_truncdfsf2"
  [(set (match_operand:SF 0 "gpc_reg_operand" "=r")
	(float_truncate:SF (match_operand:DF 1 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE"
  "efscfd %0,%1")

(define_insn "spe_absdf2"
  [(set (match_operand:DF 0 "gpc_reg_operand" "=r")
	(abs:DF (match_operand:DF 1 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE"
  "efdabs %0,%1")

(define_insn "spe_nabsdf2"
  [(set (match_operand:DF 0 "gpc_reg_operand" "=r")
	(neg:DF (abs:DF (match_operand:DF 1 "gpc_reg_operand" "r"))))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE"
  "efdnabs %0,%1")

(define_insn "spe_negdf2"
  [(set (match_operand:DF 0 "gpc_reg_operand" "=r")
	(neg:DF (match_operand:DF 1 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE"
  "efdneg %0,%1")

(define_insn "spe_adddf3"
  [(set (match_operand:DF 0 "gpc_reg_operand" "=r")
	(plus:DF (match_operand:DF 1 "gpc_reg_operand" "r")
		 (match_operand:DF 2 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE"
  "efdadd %0,%1,%2")

(define_insn "spe_subdf3"
  [(set (match_operand:DF 0 "gpc_reg_operand" "=r")
	(minus:DF (match_operand:DF 1 "gpc_reg_operand" "r")
		  (match_operand:DF 2 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE"
  "efdsub %0,%1,%2")

(define_insn "spe_muldf3"
  [(set (match_operand:DF 0 "gpc_reg_operand" "=r")
	(mult:DF (match_operand:DF 1 "gpc_reg_operand" "r")
		 (match_operand:DF 2 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE"
  "efdmul %0,%1,%2")

(define_insn "spe_divdf3"
  [(set (match_operand:DF 0 "gpc_reg_operand" "=r")
	(div:DF (match_operand:DF 1 "gpc_reg_operand" "r")
		(match_operand:DF 2 "gpc_reg_operand" "r")))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE"
  "efddiv %0,%1,%2")

;; Vector move instructions.

(define_expand "movv2si"
  [(set (match_operand:V2SI 0 "nonimmediate_operand" "")
	(match_operand:V2SI 1 "any_operand" ""))]
  "TARGET_SPE"
  "{ rs6000_emit_move (operands[0], operands[1], V2SImode); DONE; }")

(define_insn "*movv2si_internal"
  [(set (match_operand:V2SI 0 "nonimmediate_operand" "=m,r,r,r")
	(match_operand:V2SI 1 "input_operand" "r,m,r,W"))]
  "TARGET_SPE
   && (gpc_reg_operand (operands[0], V2SImode)
       || gpc_reg_operand (operands[1], V2SImode))"
  "*
{
  switch (which_alternative)
    {
    case 0: return \"evstdd%X0 %1,%y0\";
    case 1: return \"evldd%X1 %0,%y1\";
    case 2: return \"evor %0,%1,%1\";
    case 3: return output_vec_const_move (operands);
    default: gcc_unreachable ();
    }
}"
  [(set_attr "type" "vecload,vecstore,*,*")
   (set_attr "length" "*,*,*,12")])

(define_split
  [(set (match_operand:V2SI 0 "register_operand" "")
	(match_operand:V2SI 1 "zero_constant" ""))]
  "TARGET_SPE && reload_completed"
  [(set (match_dup 0)
	(xor:V2SI (match_dup 0) (match_dup 0)))]
  "")

(define_expand "movv1di"
  [(set (match_operand:V1DI 0 "nonimmediate_operand" "")
	(match_operand:V1DI 1 "any_operand" ""))]
  "TARGET_SPE"
  "{ rs6000_emit_move (operands[0], operands[1], V1DImode); DONE; }")

(define_insn "*movv1di_internal"
  [(set (match_operand:V1DI 0 "nonimmediate_operand" "=m,r,r,r")
	(match_operand:V1DI 1 "input_operand" "r,m,r,W"))]
  "TARGET_SPE
   && (gpc_reg_operand (operands[0], V1DImode)
       || gpc_reg_operand (operands[1], V1DImode))"
  "@
   evstdd%X0 %1,%y0
   evldd%X1 %0,%y1
   evor %0,%1,%1
   evxor %0,%0,%0"
  [(set_attr "type" "vecload,vecstore,*,*")
   (set_attr "length" "*,*,*,*")])

(define_expand "movv4hi"
  [(set (match_operand:V4HI 0 "nonimmediate_operand" "")
	(match_operand:V4HI 1 "any_operand" ""))]
  "TARGET_SPE"
  "{ rs6000_emit_move (operands[0], operands[1], V4HImode); DONE; }")

(define_insn "*movv4hi_internal"
  [(set (match_operand:V4HI 0 "nonimmediate_operand" "=m,r,r,r")
	(match_operand:V4HI 1 "input_operand" "r,m,r,W"))]
  "TARGET_SPE
   && (gpc_reg_operand (operands[0], V4HImode)
       || gpc_reg_operand (operands[1], V4HImode))"
  "@
   evstdd%X0 %1,%y0
   evldd%X1 %0,%y1
   evor %0,%1,%1
   evxor %0,%0,%0"
  [(set_attr "type" "vecload")])

(define_expand "movv2sf"
  [(set (match_operand:V2SF 0 "nonimmediate_operand" "")
	(match_operand:V2SF 1 "any_operand" ""))]
  "TARGET_SPE"
  "{ rs6000_emit_move (operands[0], operands[1], V2SFmode); DONE; }")

(define_insn "*movv2sf_internal"
  [(set (match_operand:V2SF 0 "nonimmediate_operand" "=m,r,r,r")
	(match_operand:V2SF 1 "input_operand" "r,m,r,W"))]
  "TARGET_SPE
   && (gpc_reg_operand (operands[0], V2SFmode)
       || gpc_reg_operand (operands[1], V2SFmode))"
  "@
   evstdd%X0 %1,%y0
   evldd%X1 %0,%y1
   evor %0,%1,%1
   evxor %0,%0,%0"
  [(set_attr "type" "vecload,vecstore,*,*")
   (set_attr "length" "*,*,*,*")])

;; End of vector move instructions.

(define_insn "spe_evmwhssfaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 702))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhssfaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhssmaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 703))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhssmaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhsmfaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 704))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhsmfaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhsmiaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 705))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhsmiaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhusiaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 706))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhusiaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhumiaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 707))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhumiaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhssfan"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 708))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhssfan %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhssian"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 709))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhssian %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhsmfan"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 710))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhsmfan %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhsmian"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 711))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhsmian %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhumian"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 713))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhumian %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhgssfaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 714))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhgssfaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhgsmfaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 715))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhgsmfaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhgsmiaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 716))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhgsmiaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhgumiaa"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 717))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhgumiaa %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhgssfan"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 718))
   (clobber (reg:SI SPEFSCR_REGNO))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhgssfan %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhgsmfan"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 719))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhgsmfan %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhgsmian"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 720))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhgsmian %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_evmwhgumian"
  [(set (match_operand:V2SI 0 "gpc_reg_operand" "=r")
        (unspec:V2SI [(match_operand:V2SI 1 "gpc_reg_operand" "r")
                      (match_operand:V2SI 2 "gpc_reg_operand" "r")] 721))
   (set (reg:V2SI SPE_ACC_REGNO) (unspec:V2SI  [(const_int 0)] 0))]
  "TARGET_SPE"
  "evmwhgumian %0,%1,%2"
  [(set_attr "type" "veccomplex")
   (set_attr  "length" "4")])

(define_insn "spe_mtspefscr"
  [(set (reg:SI SPEFSCR_REGNO)
	(unspec_volatile:SI [(match_operand:SI 0 "register_operand" "r")]
			    722))]
  "TARGET_SPE"
  "mtspefscr %0"
  [(set_attr "type" "vecsimple")])

(define_insn "spe_mfspefscr"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec_volatile:SI [(reg:SI SPEFSCR_REGNO)] 723))]
  "TARGET_SPE"
  "mfspefscr %0"
  [(set_attr "type" "vecsimple")])

;; FP comparison stuff.

;; Flip the GT bit.
(define_insn "e500_flip_gt_bit"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=y")
	(unspec:CCFP
	 [(match_operand:CCFP 1 "cc_reg_operand" "y")] 999))]
  "!TARGET_FPRS && TARGET_HARD_FLOAT"
  "*
{
  return output_e500_flip_gt_bit (operands[0], operands[1]);
}"
  [(set_attr "type" "cr_logical")])

;; MPC8540 single-precision FP instructions on GPRs.
;; We have 2 variants for each.  One for IEEE compliant math and one
;; for non IEEE compliant math.

(define_insn "cmpsfeq_gpr"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=y")
	(unspec:CCFP
	 [(compare:CCFP (match_operand:SF 1 "gpc_reg_operand" "r")
			(match_operand:SF 2 "gpc_reg_operand" "r"))]
	 1000))]
  "TARGET_HARD_FLOAT && !TARGET_FPRS && !flag_unsafe_math_optimizations"
  "efscmpeq %0,%1,%2"
  [(set_attr "type" "veccmp")])

(define_insn "tstsfeq_gpr"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=y")
	(unspec:CCFP
	 [(compare:CCFP (match_operand:SF 1 "gpc_reg_operand" "r")
			(match_operand:SF 2 "gpc_reg_operand" "r"))]
	 1001))]
  "TARGET_HARD_FLOAT && !TARGET_FPRS && flag_unsafe_math_optimizations"
  "efststeq %0,%1,%2"
  [(set_attr "type" "veccmpsimple")])

(define_insn "cmpsfgt_gpr"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=y")
	(unspec:CCFP
	 [(compare:CCFP (match_operand:SF 1 "gpc_reg_operand" "r")
			(match_operand:SF 2 "gpc_reg_operand" "r"))]
	 1002))]
  "TARGET_HARD_FLOAT && !TARGET_FPRS && !flag_unsafe_math_optimizations"
  "efscmpgt %0,%1,%2"
  [(set_attr "type" "veccmp")])

(define_insn "tstsfgt_gpr"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=y")
	(unspec:CCFP
	 [(compare:CCFP (match_operand:SF 1 "gpc_reg_operand" "r")
			(match_operand:SF 2 "gpc_reg_operand" "r"))]
	 1003))]
  "TARGET_HARD_FLOAT && !TARGET_FPRS && flag_unsafe_math_optimizations"
  "efststgt %0,%1,%2"
  [(set_attr "type" "veccmpsimple")])

(define_insn "cmpsflt_gpr"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=y")
	(unspec:CCFP
	 [(compare:CCFP (match_operand:SF 1 "gpc_reg_operand" "r")
			(match_operand:SF 2 "gpc_reg_operand" "r"))]
	 1004))]
  "TARGET_HARD_FLOAT && !TARGET_FPRS && !flag_unsafe_math_optimizations"
  "efscmplt %0,%1,%2"
  [(set_attr "type" "veccmp")])

(define_insn "tstsflt_gpr"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=y")
	(unspec:CCFP
	 [(compare:CCFP (match_operand:SF 1 "gpc_reg_operand" "r")
			(match_operand:SF 2 "gpc_reg_operand" "r"))]
	 1005))]
  "TARGET_HARD_FLOAT && !TARGET_FPRS && flag_unsafe_math_optimizations"
  "efststlt %0,%1,%2"
  [(set_attr "type" "veccmpsimple")])

;; Same thing, but for double-precision.

(define_insn "cmpdfeq_gpr"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=y")
	(unspec:CCFP
	 [(compare:CCFP (match_operand:DF 1 "gpc_reg_operand" "r")
			(match_operand:DF 2 "gpc_reg_operand" "r"))]
	 CMPDFEQ_GPR))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE && !flag_unsafe_math_optimizations"
  "efdcmpeq %0,%1,%2"
  [(set_attr "type" "veccmp")])

(define_insn "tstdfeq_gpr"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=y")
	(unspec:CCFP
	 [(compare:CCFP (match_operand:DF 1 "gpc_reg_operand" "r")
			(match_operand:DF 2 "gpc_reg_operand" "r"))]
	 TSTDFEQ_GPR))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE && flag_unsafe_math_optimizations"
  "efdtsteq %0,%1,%2"
  [(set_attr "type" "veccmpsimple")])

(define_insn "cmpdfgt_gpr"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=y")
	(unspec:CCFP
	 [(compare:CCFP (match_operand:DF 1 "gpc_reg_operand" "r")
			(match_operand:DF 2 "gpc_reg_operand" "r"))]
	 CMPDFGT_GPR))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE && !flag_unsafe_math_optimizations"
  "efdcmpgt %0,%1,%2"
  [(set_attr "type" "veccmp")])

(define_insn "tstdfgt_gpr"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=y")
	(unspec:CCFP
	 [(compare:CCFP (match_operand:DF 1 "gpc_reg_operand" "r")
			(match_operand:DF 2 "gpc_reg_operand" "r"))]
	 TSTDFGT_GPR))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE && flag_unsafe_math_optimizations"
  "efdtstgt %0,%1,%2"
  [(set_attr "type" "veccmpsimple")])

(define_insn "cmpdflt_gpr"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=y")
	(unspec:CCFP
	 [(compare:CCFP (match_operand:DF 1 "gpc_reg_operand" "r")
			(match_operand:DF 2 "gpc_reg_operand" "r"))]
	 CMPDFLT_GPR))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE && !flag_unsafe_math_optimizations"
  "efdcmplt %0,%1,%2"
  [(set_attr "type" "veccmp")])

(define_insn "tstdflt_gpr"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=y")
	(unspec:CCFP
	 [(compare:CCFP (match_operand:DF 1 "gpc_reg_operand" "r")
			(match_operand:DF 2 "gpc_reg_operand" "r"))]
	 TSTDFLT_GPR))]
  "TARGET_HARD_FLOAT && TARGET_E500_DOUBLE && flag_unsafe_math_optimizations"
  "efdtstlt %0,%1,%2"
  [(set_attr "type" "veccmpsimple")])

;; Like cceq_ior_compare, but compare the GT bits.
(define_insn "e500_cr_ior_compare"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=y")
	(unspec:CCFP [(match_operand 1 "cc_reg_operand" "y")
		      (match_operand 2 "cc_reg_operand" "y")]
		     E500_CR_IOR_COMPARE))]
  "TARGET_E500"
  "cror 4*%0+gt,4*%1+gt,4*%2+gt"
  [(set_attr "type" "cr_logical")])
