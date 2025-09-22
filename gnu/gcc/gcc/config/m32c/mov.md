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

;; move, push, extend, etc.

;; Be careful to never create an alternative that has memory as both
;; src and dest, as that makes gcc think that mem-mem moves in general
;; are supported.  While the chip does support this, it only has two
;; address registers and sometimes gcc requires more than that.  One
;; example is code like this: a = *b where both a and b are spilled to
;; the stack.

;; Match push/pop before mov.b for passing char as arg,
;; e.g. stdlib/efgcvt.c.
(define_insn "movqi_op"
  [(set (match_operand:QI 0 "m32c_nonimmediate_operand"
			  "=Rqi*Rmm, <,          RqiSd*Rmm, SdSs,    Rqi*Rmm, Sd")
	(match_operand:QI 1 "m32c_any_operand"
			  "iRqi*Rmm, iRqiSd*Rmm, >,         Rqi*Rmm, SdSs,    i"))]
  "m32c_mov_ok (operands, QImode)"
  "@
    mov.b\t%1,%0
    push.b\t%1
    pop.b\t%0
    mov.b\t%1,%0
    mov.b\t%1,%0
    mov.b\t%1,%0"
  [(set_attr "flags" "sz,*,*,sz,sz,sz")]
  )

(define_expand "movqi"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=RqiSd*Rmm")
	(match_operand:QI 1 "general_operand" "iRqiSd*Rmm"))]
  ""
  "if (m32c_prepare_move (operands, QImode)) DONE;"
  )


(define_insn "movhi_op"
  [(set (match_operand:HI 0 "m32c_nonimmediate_operand"
			  "=Rhi*Rmm,     Sd, SdSs,   *Rcr, RhiSd*Rmm, <, RhiSd*Rmm, <, *Rcr")
	(match_operand:HI 1 "m32c_any_operand"
			  "iRhi*RmmSdSs, i, Rhi*Rmm, RhiSd*Rmm, *Rcr, iRhiSd*Rmm, >, *Rcr, >"))]
  "m32c_mov_ok (operands, HImode)"
  "@
   mov.w\t%1,%0
   mov.w\t%1,%0
   mov.w\t%1,%0
   ldc\t%1,%0
   stc\t%1,%0
   push.w\t%1
   pop.w\t%0
   pushc\t%1
   popc\t%0"
  [(set_attr "flags" "sz,sz,sz,n,n,n,n,n,n")]
  )

(define_expand "movhi"
  [(set (match_operand:HI 0 "m32c_nonimmediate_operand" "=RhiSd*Rmm")
	(match_operand:HI 1 "m32c_any_operand" "iRhiSd*Rmm"))]
  ""
  "if (m32c_prepare_move (operands, HImode)) DONE;"
  )


(define_insn "movpsi_op"
  [(set (match_operand:PSI 0 "m32c_nonimmediate_operand"
			   "=Raa, SdRmmRpi,  Rcl,  RpiSd*Rmm, <,       <, Rcl, RpiRaa*Rmm")
	(match_operand:PSI 1 "m32c_any_operand"
			   "sIU3, iSdRmmRpi, iRpiSd*Rmm, Rcl, Rpi*Rmm, Rcl, >, >"))]
  "TARGET_A24 && m32c_mov_ok (operands, PSImode)"
  "@
   mov.l:s\t%1,%0
   mov.l\t%1,%0
   ldc\t%1,%0
   stc\t%1,%0
   push.l\t%1
   pushc\t%1
   popc\t%0
   #"
  [(set_attr "flags" "sz,sz,n,n,n,n,n,*")]
  )


;; The intention here is to combine the add with the move to create an
;; indexed move.  GCC doesn't always figure this out itself.

(define_peephole2
  [(set (match_operand:HPSI 0 "register_operand" "")
	(plus:HPSI (match_operand:HPSI 1 "register_operand" "")
		   (match_operand:HPSI 2 "immediate_operand" "")))
   (set (match_operand:QHSI 3 "nonimmediate_operand" "")
	(mem:QHSI (match_operand:HPSI 4 "register_operand" "")))]
  "REGNO (operands[0]) == REGNO (operands[1])
   && REGNO (operands[0]) == REGNO (operands[4])
   && (rtx_equal_p (operands[0], operands[3])
       || (dead_or_set_p (peep2_next_insn (1), operands[4])
          && ! reg_mentioned_p (operands[0], operands[3])))"
  [(set (match_dup 3)
	(mem:QHSI (plus:HPSI (match_dup 1)
			     (match_dup 2))))]
  "")

(define_peephole2
  [(set (match_operand:HPSI 0 "register_operand" "")
	(plus:HPSI (match_operand:HPSI 1 "register_operand" "")
		   (match_operand:HPSI 2 "immediate_operand" "")))
   (set (mem:QHSI (match_operand:HPSI 4 "register_operand" ""))
	(match_operand:QHSI 3 "m32c_any_operand" ""))]
  "REGNO (operands[0]) == REGNO (operands[1])
   && REGNO (operands[0]) == REGNO (operands[4])
   && dead_or_set_p (peep2_next_insn (1), operands[4])
   && ! reg_mentioned_p (operands[0], operands[3])"
  [(set (mem:QHSI (plus:HPSI (match_dup 1)
			     (match_dup 2)))
	(match_dup 3))]
  "")

; Peephole to generate SImode mov instructions for storing an
; immediate double data to a memory location.
(define_peephole2
  [(set (match_operand:HI 0 "memory_operand" "")
        (match_operand 1 "const_int_operand" ""))
   (set (match_operand:HI 2 "memory_operand" "")
        (match_operand 3 "const_int_operand" ""))]
   "TARGET_A24 && m32c_immd_dbl_mov (operands, HImode)"
   [(set (match_dup 4) (match_dup 5))]
   ""
)

; Some PSI moves must be split.
(define_split
  [(set (match_operand:PSI 0 "m32c_nonimmediate_operand" "")
	(match_operand:PSI 1 "m32c_any_operand" ""))]
  "reload_completed && m32c_split_psi_p (operands)"
  [(set (match_dup 2)
	(match_dup 3))
   (set (match_dup 4)
	(match_dup 5))]
  "m32c_split_move (operands, PSImode, 3);"
  )

(define_expand "movpsi"
  [(set (match_operand:PSI 0 "m32c_nonimmediate_operand" "")
	(match_operand:PSI 1 "m32c_any_operand" ""))]
  ""
  "if (m32c_prepare_move (operands, PSImode)) DONE;"
  )



(define_expand "movsi"
  [(set (match_operand:SI 0 "m32c_nonimmediate_operand" "=RsiSd*Rmm")
	(match_operand:SI 1 "m32c_any_operand" "iRsiSd*Rmm"))]
  ""
  "if (m32c_split_move (operands, SImode, 0)) DONE;"
  )

; All SI moves are split if TARGET_A16
(define_insn_and_split "movsi_splittable"
  [(set (match_operand:SI 0 "m32c_nonimmediate_operand" "=Rsi<*Rmm,RsiSd*Rmm,Ss")
	(match_operand:SI 1 "m32c_any_operand" "iRsiSd*Rmm,iRsi>*Rmm,Rsi*Rmm"))]
  "TARGET_A16"
  "#"
  "TARGET_A16 && reload_completed"
  [(pc)]
  "m32c_split_move (operands, SImode, 1); DONE;"
  )

; The movsi pattern doesn't always match because sometimes the modes
; don't match.
(define_insn "push_a01_l"
  [(set (mem:SI (pre_dec:PSI (reg:PSI SP_REGNO)))
	(match_operand 0 "a_operand" "Raa"))]
  ""
  "push.l\t%0"
  [(set_attr "flags" "n")]
  )

(define_insn "movsi_24"
  [(set (match_operand:SI 0 "m32c_nonimmediate_operand"  "=Rsi*Rmm,   Sd,       RsiSd*Rmm,     <")
	(match_operand:SI 1 "m32c_any_operand" "iRsiSd*Rmm, iRsi*Rmm, >, iRsiRaaSd*Rmm"))]
  "TARGET_A24"
  "@
   mov.l\t%1,%0
   mov.l\t%1,%0
   #
   push.l\t%1"
  [(set_attr "flags" "sz,sz,*,n")]
  )

(define_expand "movdi"
  [(set (match_operand:DI 0 "m32c_nonimmediate_operand" "=RdiSd*Rmm")
	(match_operand:DI 1 "m32c_any_operand" "iRdiSd*Rmm"))]
  ""
  "if (m32c_split_move (operands, DImode, 0)) DONE;"
  )

(define_insn_and_split "movdi_splittable"
  [(set (match_operand:DI 0 "m32c_nonimmediate_operand" "=Rdi<*Rmm,RdiSd*Rmm")
	(match_operand:DI 1 "m32c_any_operand" "iRdiSd*Rmm,iRdi>*Rmm"))]
  ""
  "#"
  "reload_completed"
  [(pc)]
  "m32c_split_move (operands, DImode, 1); DONE;"
  )




(define_insn "pushqi"
  [(set (mem:QI (pre_dec:PSI (reg:PSI SP_REGNO)))
        (match_operand:QI 0 "mrai_operand" "iRqiSd*Rmm"))]
  ""
  "push.b\t%0"
  [(set_attr "flags" "n")]
  )

(define_expand "pushhi"
  [(set (mem:HI (pre_dec:PSI (reg:PSI SP_REGNO)))
        (match_operand:HI 0 "" ""))]
  ""
  "if (TARGET_A16)
     gen_pushhi_16 (operands[0]);
   else
     gen_pushhi_24 (operands[0]);
   DONE;"
  )

(define_insn "pushhi_16"
  [(set (mem:HI (pre_dec:HI (reg:HI SP_REGNO)))
        (match_operand:HI 0 "mrai_operand" "iRhiSd*Rmm,Rcr"))]
  "TARGET_A16"
  "@
   push.w\t%0
   pushc\t%0"
  [(set_attr "flags" "n,n")]
  )

(define_insn "pushhi_24"
  [(set (mem:HI (pre_dec:PSI (reg:PSI SP_REGNO)))
        (match_operand:HI 0 "mrai_operand" "iRhiSd*Rmm"))]
  "TARGET_A24"
  "push.w\t%0"
  [(set_attr "flags" "n")]
  )

;(define_insn "pushpi"
;  [(set (mem:PSI (pre_dec:PSI (reg:PSI SP_REGNO)))
;        (match_operand:PI 0 "mrai_operand" "iRaa,Rcr"))]
;  "TARGET_A24"
;  "@
;   push.l\t%0
;   pushc\t%0"
;  )

(define_insn "pushsi"
  [(set (mem:SI (pre_dec:PSI (reg:PSI SP_REGNO)))
        (match_operand:SI 0 "mrai_operand" "iRsiSd*Rmm"))]
  "TARGET_A24"
  "push.l\t%0"
  [(set_attr "flags" "n")]
  )

(define_expand "pophi"
  [(set (match_operand:HI 0 "mra_operand" "=RhiSd*Rmm,Rcr")
        (mem:HI (post_inc:HI (reg:HI SP_REGNO))))]
  ""
  "if (TARGET_A16)
     gen_pophi_16 (operands[0]);
   else
     gen_pophi_24 (operands[0]);
   DONE;"
  )

(define_insn "pophi_16"
  [(set (match_operand:HI 0 "mra_operand" "=RhiSd*Rmm,Rcr")
        (mem:HI (post_inc:HI (reg:HI SP_REGNO))))]
  "TARGET_A16"
  "@
   pop.w\t%0
   popc\t%0"
  [(set_attr "flags" "n,n")]
  )

(define_insn "pophi_24"
  [(set (match_operand:HI 0 "mra_operand" "=RhiSd*Rmm")
        (mem:HI (post_inc:PSI (reg:PSI SP_REGNO))))]
  "TARGET_A24"
  "pop.w\t%0"
  [(set_attr "flags" "n")]
  )

(define_insn "poppsi"
  [(set (match_operand:PSI 0 "cr_operand" "=Rcl")
        (mem:PSI (post_inc:PSI (reg:PSI SP_REGNO))))]
  "TARGET_A24"
  "popc\t%0"
  [(set_attr "flags" "n")]
  )


;; Rhl used here as an HI-mode Rxl
(define_insn "extendqihi2"
[(set (match_operand:HI 0 "m32c_nonimmediate_operand" "=RhlSd*Rmm")
	(sign_extend:HI (match_operand:QI 1 "mra_operand" "0")))]
  ""
  "exts.b\t%1"
  [(set_attr "flags" "sz")]
  )

(define_insn "extendhisi2"
  [(set (match_operand:SI 0 "register_operand" "=R03")
	(sign_extend:SI (match_operand:HI 1 "r0123_operand" "0")))]
  ""
  "*
   if (REGNO(operands[0]) == 0) return \"exts.w\t%1\";
   else return \"mov.w r1,r3 | sha.w #-8,r3 | sha.w #-7,r3\";"
  [(set_attr "flags" "x")]
  )

(define_insn "extendpsisi2"
  [(set (match_operand:SI 0 "mr_operand" "=R03Sd*Rmm")
	(sign_extend:SI (match_operand:PSI 1 "mr_operand" "0")))]
  ""
  "; expand psi %1 to si %0"
  [(set_attr "flags" "n")]
  )

(define_insn "zero_extendpsisi2"
  [(set (match_operand:SI 0 "mr_operand" "=R03Sd*Rmm")
	(zero_extend:SI (match_operand:PSI 1 "mr_operand" "0")))]
  ""
  "; expand psi %1 to si %0"
  [(set_attr "flags" "n")]
  )

(define_insn "zero_extendhipsi2"
  [(set (match_operand:PSI 0 "register_operand" "=Raa")
	(truncate:PSI (zero_extend:SI (match_operand:HI 1 "register_operand" "R03"))))]
  ""
  "mov.w\t%1,%0"
  [(set_attr "flags" "sz")]
  )

(define_insn "zero_extendhisi2"
  [(set (match_operand:SI 0 "m32c_nonimmediate_operand" "=RsiSd")
	(zero_extend:SI (match_operand:HI 1 "nonimmediate_operand" "0")))]
  ""
  "mov.w\t#0,%H0"
  [(set_attr "flags" "x")]
  )

(define_insn "zero_extendqihi2"
  [(set (match_operand:HI 0 "m32c_nonimmediate_operand" "=Rhl,RhiSd*Rmm")
	(zero_extend:HI (match_operand:QI 1 "nonimmediate_operand" "0,0")))]
  ""
  "@
   mov.b\t#0,%H0
   and.w\t#255,%0"
  [(set_attr "flags" "x,x")]
  )

(define_insn "truncsipsi2_16"
  [(set (match_operand:PSI 0 "m32c_nonimmediate_operand" "=RsiRadSd*Rmm,Raa,Rcr,RsiSd*Rmm")
	(truncate:PSI (match_operand:SI 1 "nonimmediate_operand" "0,RsiSd*Rmm,RsiSd*Rmm,Rcr")))]
  "TARGET_A16"
  "@
   ; no-op trunc si %1 to psi %0
   #
   ldc\t%1,%0
   stc\t%1,%0"
  [(set_attr "flags" "n,*,n,n")]
  )

(define_insn "trunchiqi2"
  [(set (match_operand:QI 0 "m32c_nonimmediate_operand" "=RqiRmmSd")
	(truncate:QI (match_operand:HI 1 "mra_qi_operand" "0")))]
  ""
  "; no-op trunc hi %1 to qi %0"
  [(set_attr "flags" "n")]
  )

(define_insn "truncsipsi2_24"
  [(set (match_operand:PSI 0              "m32c_nonimmediate_operand" "=RsiSd*Rmm,Raa,!Rcl,RsiSd*Rmm")
	(truncate:PSI (match_operand:SI 1 "m32c_nonimmediate_operand" "0,RsiSd*Rmm,RsiSd*Rmm,!Rcl")))]
  "TARGET_A24"
  "@
   ; no-op trunc si %1 to psi %0
   mov.l\t%1,%0
   ldc\t%1,%0
   stc\t%1,%0"
  [(set_attr "flags" "n,sz,n,n")]
  )

(define_expand "truncsipsi2"
  [(set (match_operand:PSI 0 "m32c_nonimmediate_operand" "=RsiRadSd*Rmm,Raa,Rcr,RsiSd*Rmm")
	(truncate:PSI (match_operand:SI 1 "m32c_nonimmediate_operand" "0,RsiSd*Rmm,RsiSd*Rmm,Rcr")))]
  ""
  ""
  )

(define_expand "reload_inqi"
  [(set (match_operand:QI 2 "" "=&Rqi")
	(match_operand:QI 1 "" ""))
   (set (match_operand:QI 0 "" "")
	(match_dup 2))
   ]
  ""
  "")

(define_expand "reload_outqi"
  [(set (match_operand:QI 2 "" "=&Rqi")
	(match_operand:QI 1 "" ""))
   (set (match_operand:QI 0 "" "")
	(match_dup 2))
   ]
  ""
  "")

(define_expand "reload_inhi"
  [(set (match_operand:HI 2 "" "=&Rhi")
	(match_operand:HI 1 "" ""))
   (set (match_operand:HI 0 "" "")
	(match_dup 2))
   ]
  ""
  "")

(define_expand "reload_outhi"
  [(set (match_operand:HI 2 "" "=&Rhi")
	(match_operand:HI 1 "" ""))
   (set (match_operand:HI 0 "" "")
	(match_dup 2))
   ]
  ""
  "")
