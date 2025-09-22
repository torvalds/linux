;; Machine Descriptions for R8C/M16C/M32C
;; Copyright (C) 2005
;; Free Software Foundation, Inc.
;; Contributed by Red Hat.
;;
;; This file is part of GCC.
;;
;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published
;; by the Free Software Foundation; either version 2, or (at your
;; option) any later version.
;;
;; GCC is distributed in the hope that it will be useful, but WITHOUT
;; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;; or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
;; License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to the Free
;; Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
;; 02110-1301, USA.

;; multiply and divide

; Here is the pattern for the const_int.
(define_insn "mulqihi3_c"
  [(set (match_operand:HI 0 "mra_operand" "=RhiSd,??Rmm")
        (mult:HI (sign_extend:HI (match_operand:QI 1 "mra_operand" "%0,0"))
                 (match_operand 2 "immediate_operand" "i,i")))]
  ""
  "mul.b\t%2,%1"
  [(set_attr "flags" "o")]
)

; Here is the pattern for registers and such.
(define_insn "mulqihi3_r"
  [(set (match_operand:HI 0 "mra_operand" "=RhiSd,RhiSd,??Rmm,??Rmm,Raa,Raa")
        (mult:HI (sign_extend:HI (match_operand:QI 1 "mra_operand" "%0,0,0,0,0,0"))
                 (sign_extend:HI (match_operand:QI 2 "mra_operand" "RqiSd,?Rmm,RqiSd,?Rmm,RhlSd,?Rmm"))))]
  ""
  "mul.b\t%2,%1"
  [(set_attr "flags" "o")]
)

; Don't try to sign_extend a const_int.  Same for all other multiplies.
(define_expand "mulqihi3"
  [(set (match_operand:HI 0 "mra_operand" "=RhiSd,RhiSd,??Rmm,??Rmm,Raa,Raa")
        (mult:HI (sign_extend:HI (match_operand:QI 1 "mra_operand" "%0,0,0,0,0,0"))
                 (match_operand:QI 2 "mra_operand" "RqiSd,?Rmm,RqiSd,?Rmm,RhlSd,?Rmm")))]
  ""
  "{ if (GET_MODE (operands[2]) != VOIDmode)
      operands[2] = gen_rtx_SIGN_EXTEND (HImode, operands[2]); }"
)

(define_insn "umulqihi3_c"
  [(set (match_operand:HI 0 "mra_operand" "=RhiSd,??Rmm")
        (mult:HI (zero_extend:HI (match_operand:QI 1 "mra_operand" "%0,0"))
                 (match_operand 2 "immediate_operand" "i,i")))]
  ""
  "mulu.b\t%U2,%1"
  [(set_attr "flags" "o")]
)

(define_insn "umulqihi3_r"
  [(set (match_operand:HI 0 "mra_operand" "=RhiSd,RhiSd,??Rmm,??Rmm,Raa,Raa")
        (mult:HI (zero_extend:HI (match_operand:QI 1 "mra_operand" "%0,0,0,0,0,0"))
                 (zero_extend:HI (match_operand:QI 2 "mra_operand" "RqiSd,?Rmm,RqiSd,?Rmm,RhlSd,?Rmm"))))]
  ""
  "mulu.b\t%U2,%1"
  [(set_attr "flags" "o")]
)

(define_expand "umulqihi3"
  [(set (match_operand:HI 0 "mra_operand" "=RhiSd,RhiSd,??Rmm,??Rmm,Raa,Raa")
        (mult:HI (zero_extend:HI (match_operand:QI 1 "mra_operand" "%0,0,0,0,0,0"))
                 (match_operand:QI 2 "mra_operand" "RqiSd,?Rmm,RqiSd,?Rmm,RhlSd,?Rmm")))]
  ""
  "{ if (GET_MODE (operands[2]) != VOIDmode)
      operands[2] = gen_rtx_ZERO_EXTEND (HImode, operands[2]); }"
)

(define_insn "mulhisi3_c"
  [(set (match_operand:SI 0 "ra_operand" "=Rsi")
        (mult:SI (sign_extend:SI (match_operand:HI 1 "mra_operand" "%0"))
                 (match_operand 2 "immediate_operand" "i")))]
  ""
  "mul.w\t%2,%1"
  [(set_attr "flags" "o")]
)

(define_insn "mulhisi3_r"
  [(set (match_operand:SI 0 "mra_operand" "=Rsi,Rsi")
        (mult:SI (sign_extend:SI (match_operand:HI 1 "mra_operand" "%0,0"))
                 (sign_extend:SI (match_operand:HI 2 "mra_operand" "RhiSd,?Rmm"))))]
  ""
  "mul.w\t%2,%1"
  [(set_attr "flags" "o")]
)

(define_expand "mulhisi3"
  [(set (match_operand:SI 0 "mra_operand" "=RsiSd,RsiSd,??Rmm,??Rmm")
        (mult:SI (sign_extend:SI (match_operand:HI 1 "mra_operand" "%0,0,0,0"))
                 (match_operand:HI 2 "mra_operand" "RhiSd,?Rmm,RhiSd,?Rmm")))]
  ""
  "{ if (GET_MODE (operands[2]) != VOIDmode)
      operands[2] = gen_rtx_SIGN_EXTEND (SImode, operands[2]); }"
)

(define_insn "umulhisi3_c"
  [(set (match_operand:SI 0 "ra_operand" "=Rsi")
        (mult:SI (zero_extend:SI (match_operand:HI 1 "mra_operand" "%0"))
                 (match_operand 2 "immediate_operand" "i")))]
  ""
  "mulu.w\t%u2,%1"
  [(set_attr "flags" "o")]
)

(define_insn "umulhisi3_r"
  [(set (match_operand:SI 0 "mra_operand" "=Rsi,Rsi")
        (mult:SI (zero_extend:SI (match_operand:HI 1 "mra_operand" "%0,0"))
                 (zero_extend:SI (match_operand:HI 2 "mra_operand" "RhiSd,?Rmm"))))]
  ""
  "mulu.w\t%u2,%1"
  [(set_attr "flags" "o")]
)

(define_expand "umulhisi3"
  [(set (match_operand:SI 0 "mra_operand" "=RsiSd,RsiSd,??Rmm,??Rmm")
        (mult:SI (zero_extend:SI (match_operand:HI 1 "mra_operand" "%0,0,0,0"))
                 (match_operand:HI 2 "mra_operand" "RhiSd,?Rmm,RhiSd,?Rmm")))]
  ""
  "{ if (GET_MODE (operands[2]) != VOIDmode)
      operands[2] = gen_rtx_ZERO_EXTEND (SImode, operands[2]); }"
)


; GCC expects to be able to multiply pointer-sized integers too, but
; fortunately it only multiplies by powers of two, although sometimes
; they're negative.
(define_insn "mulpsi3_op"
  [(set (match_operand:PSI 0 "mra_operand" "=RsiSd")
	(mult:PSI (match_operand:PSI 1 "mra_operand" "%0")
		  (match_operand 2 "m32c_psi_scale" "Ilb")))]
  "TARGET_A24"
  "shl.l\t%b2,%0"
  [(set_attr "flags" "szc")]
  )

(define_expand "mulpsi3"
  [(set (match_operand:PSI 0 "mra_operand" "=RsiSd")
	(mult:PSI (match_operand:PSI 1 "mra_operand" "%0")
		  (match_operand 2 "m32c_psi_scale" "Ilb")))]
  "TARGET_A24"
  "if (GET_CODE (operands[2]) != CONST_INT
       || ! m32c_psi_scale (operands[2], PSImode))
     {
       m32c_expand_neg_mulpsi3 (operands);
       DONE;
     }"
  )



(define_expand "divmodqi4"
  [(set (match_dup 4)
	(sign_extend:HI (match_operand:QI 1 "register_operand" "0,0")))
   (parallel [(set (match_operand:QI 0 "register_operand" "=R0w,R0w")
		   (div:QI (match_dup 4)
			   (match_operand:QI 2 "general_operand" "iRqiSd,?Rmm")))
	      (set (match_operand:QI 3 "register_operand" "=&R0h,&R0h")
		   (mod:QI (match_dup 4) (match_dup 2)))
	      ])]
  "0"
  "operands[4] = gen_reg_rtx (HImode);"
  )

(define_insn "divmodqi4_n"
  [(set (match_operand:QI 0 "register_operand" "=R0l,R0l")
	(div:QI (match_operand:HI 1 "register_operand" "R0w,R0w")
		(match_operand:QI 2 "general_operand" "iRqiSd,?Rmm")))
   (set (match_operand:QI 3 "register_operand" "=R0h,R0h")
	(mod:QI (match_dup 1) (match_dup 2)))
   ]
  "0"
  "div.b\t%2"
  [(set_attr "flags" "o")]
  )

(define_expand "udivmodqi4"
  [(set (match_dup 4)
	(zero_extend:HI (match_operand:QI 1 "register_operand" "0,0")))
   (parallel [(set (match_operand:QI 0 "register_operand" "=R0l,R0l")
		   (udiv:QI (match_dup 4)
			   (match_operand:QI 2 "general_operand" "iRqiSd,?Rmm")))
	      (set (match_operand:QI 3 "register_operand" "=&R0h,&R0h")
		   (umod:QI (match_dup 4) (match_dup 2)))
	      ])]
  "0"
  "operands[4] = gen_reg_rtx (HImode);"
  )

(define_insn "udivmodqi4_n"
  [(set (match_operand:QI 0 "register_operand" "=R0l,R0l")
	(udiv:QI (match_operand:HI 1 "register_operand" "R0w,R0w")
		(match_operand:QI 2 "general_operand" "iRqiSd,?Rmm")))
   (set (match_operand:QI 3 "register_operand" "=R0h,R0h")
	(umod:QI (match_dup 1) (match_dup 2)))
   ]
  "0"
  "divu.b\t%2"
  [(set_attr "flags" "o")]
  )

(define_expand "divmodhi4"
  [(set (match_dup 4)
	(sign_extend:SI (match_operand:HI 1 "register_operand" "0,0")))
   (parallel [(set (match_operand:HI 0 "register_operand" "=R0w,R0w")
		   (div:HI (match_dup 4)
			   (match_operand:HI 2 "general_operand" "iRhiSd,?Rmm")))
	      (set (match_operand:HI 3 "register_operand" "=R2w,R2w")
		   (mod:HI (match_dup 4) (match_dup 2)))
	      ])]
  ""
  "operands[4] = gen_reg_rtx (SImode);"
  )

(define_insn "divmodhi4_n"
  [(set (match_operand:HI 0 "m32c_r0_operand" "=R0w,R0w")
	(div:HI (match_operand:SI 1 "m32c_r0_operand" "R02,R02")
		(match_operand:HI 2 "m32c_notr2_operand" "iR1wR3wRaaSd,?Rmm")))
   (set (match_operand:HI 3 "m32c_r2_operand" "=R2w,R2w")
	(mod:HI (match_dup 1) (match_dup 2)))
   ]
  ""
  "div.w\t%2"
  [(set_attr "flags" "o")]
  )

(define_expand "udivmodhi4"
  [(set (match_dup 4)
	(zero_extend:SI (match_operand:HI 1 "register_operand" "0,0")))
   (parallel [(set (match_operand:HI 0 "register_operand" "=R0w,R0w")
		   (udiv:HI (match_dup 4)
			   (match_operand:HI 2 "general_operand" "iRhiSd,?Rmm")))
	      (set (match_operand:HI 3 "register_operand" "=R2w,R2w")
		   (umod:HI (match_dup 4) (match_dup 2)))
	      ])]
  ""
  "operands[4] = gen_reg_rtx (SImode);"
  )

(define_insn "udivmodhi4_n"
  [(set (match_operand:HI 0 "m32c_r0_operand" "=R0w,R0w")
	(udiv:HI (match_operand:SI 1 "m32c_r0_operand" "R02,R02")
		(match_operand:HI 2 "m32c_notr2_operand" "iR1wR3wRaaSd,?Rmm")))
   (set (match_operand:HI 3 "m32c_r2_operand" "=R2w,R2w")
	(umod:HI (match_dup 1) (match_dup 2)))
   ]
  ""
  "divu.w\t%2"
  [(set_attr "flags" "o")]
  )
