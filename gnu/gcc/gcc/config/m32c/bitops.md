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

;; Bit-wise operations (and, ior, xor, shift)

; On the R8C and M16C, "address" for bit instructions is usually (but
; not always!) the *bit* address, not the *byte* address.  This
; confuses gcc, so we avoid cases where gcc would produce the wrong
; code.  We're left with absolute addresses and registers, and the odd
; case of shifting a bit by a variable.

; On the M32C, "address" for bit instructions is a regular address,
; and the bit number is stored in a separate field.  Thus, we can let
; gcc do more interesting things.  However, the M32C cannot set all
; the bits in a 16 bit register, which the R8C/M16C can do.

; However, it all means that we end up with two sets of patterns, one
; for each chip.

;;----------------------------------------------------------------------

;; First off, all the ways we can set one bit, other than plain IOR.

(define_insn "bset_qi"
  [(set (match_operand:QI 0 "memsym_operand" "+Si")
	(ior:QI (subreg:QI (ashift:HI (const_int 1)
				      (subreg:QI (match_operand:HI 1 "a_qi_operand" "Raa") 0)) 0)
		(match_operand:QI 2 "" "0")))]
  "TARGET_A16"
  "bset\t%0[%1]"
  [(set_attr "flags" "n")]
  )  

(define_insn "bset_hi"
  [(set (zero_extract:HI (match_operand:QI 0 "memsym_operand" "+Si")
			 (const_int 1)
			 (zero_extend:HI (subreg:QI (match_operand:HI 1 "a_qi_operand" "Raa") 0)))
	(const_int 1))]
  "TARGET_A16"
  "bset\t%0[%1]"
  [(set_attr "flags" "n")]
  )  

;;----------------------------------------------------------------------

;; Now all the ways we can clear one bit, other than plain AND.

; This is odd because the shift patterns use QI counts, but we can't
; easily put QI in $aN without causing problems elsewhere.
(define_insn "bclr_qi"
  [(set (zero_extract:HI (match_operand:QI 0 "memsym_operand" "+Si")
			 (const_int 1)
			 (zero_extend:HI (subreg:QI (match_operand:HI 1 "a_qi_operand" "Raa") 0)))
	(const_int 0))]
  "TARGET_A16"
  "bclr\t%0[%1]"
  [(set_attr "flags" "n")]
  )  


;;----------------------------------------------------------------------

;; Now the generic patterns.

(define_insn "andqi3_16"
  [(set (match_operand:QI 0 "mra_operand" "=Sp,Rqi,RhlSd,RhlSd,??Rmm,??Rmm")
	(and:QI (match_operand:QI 1 "mra_operand" "%0,0,0,0,0,0")
		(match_operand 2 "mrai_operand" "Imb,Imb,iRhlSd,?Rmm,iRhlSd,?Rmm")))]
  "TARGET_A16"
  "@
   bclr\t%B2,%0
   bclr\t%B2,%h0
   and.b\t%x2,%0
   and.b\t%x2,%0
   and.b\t%x2,%0
   and.b\t%x2,%0"
  [(set_attr "flags" "n,n,sz,sz,sz,sz")]
  )

(define_insn "andhi3_16"
  [(set (match_operand:HI 0 "mra_operand" "=Sp,Sp,Rhi,RhiSd,??Rmm,RhiSd,??Rmm")
	(and:HI (match_operand:HI 1 "mra_operand" "%0,0,0,0,0,0,0")
		(match_operand:HI 2 "mrai_operand" "Imb,Imw,Imw,iRhiSd,?Rmm,?Rmm,iRhiSd")))]
  "TARGET_A16"
  "@
   
   bclr\t%B2,%0
   bclr\t%B2-8,1+%0
   bclr\t%B2,%0
   and.w\t%X2,%0
   and.w\t%X2,%0
   and.w\t%X2,%0
   and.w\t%X2,%0"
  [(set_attr "flags" "n,n,n,sz,sz,sz,sz")]
  )

(define_insn "andsi3"
  [(set (match_operand:SI 0 "mra_operand" "=RsiSd,RsiSd,??Rmm,??Rmm,??Rmm,RsiSd")
        (and:SI (match_operand:SI 1 "mra_operand" "%0,0,0,0,0,0")
                (match_operand:SI 2 "mrai_operand" "i,?Rmm,i,RsiSd,?Rmm,RsiSd")))]
  ""
  "*
  switch (which_alternative)
    {
    case 0:
      output_asm_insn (\"and.w %X2,%h0\",operands);
      operands[2]= GEN_INT (INTVAL (operands[2]) >> 16);
      return \"and.w %X2,%H0\";
    case 1:
      return \"and.w %h2,%h0\;and.w %H2,%H0\";
    case 2:
      output_asm_insn (\"and.w %X2,%h0\",operands);
      operands[2]= GEN_INT (INTVAL (operands[2]) >> 16);
      return \"and.w %X2,%H0\";
    case 3:
      return \"and.w %h2,%h0\;and.w %H2,%H0\";
    case 4:
      return \"and.w %h2,%h0\;and.w %H2,%H0\";
    case 5:
      return \"and.w %h2,%h0\;and.w %H2,%H0\";
    }"
  [(set_attr "flags" "x,x,x,x,x,x")]
)


(define_insn "iorqi3_16"
  [(set (match_operand:QI 0 "mra_operand" "=Sp,Rqi,RqiSd,??Rmm,RqiSd,??Rmm")
	(ior:QI (match_operand:QI 1 "mra_operand" "%0,0,0,0,0,0")
		(match_operand:QI 2 "mrai_operand" "Ilb,Ilb,iRhlSd,iRhlSd,?Rmm,?Rmm")))]
  "TARGET_A16"
  "@
   bset\t%B2,%0
   bset\t%B2,%h0
   or.b\t%x2,%0
   or.b\t%x2,%0
   or.b\t%x2,%0
   or.b\t%x2,%0"
  [(set_attr "flags" "n,n,sz,sz,sz,sz")]
  )

(define_insn "iorhi3_16"
  [(set (match_operand:HI 0 "mra_operand" "=Sp,Sp,Rhi,RhiSd,RhiSd,??Rmm,??Rmm")
	(ior:HI (match_operand:HI 1 "mra_operand" "%0,0,0,0,0,0,0")
		(match_operand:HI 2 "mrai_operand" "Imb,Imw,Ilw,iRhiSd,?Rmm,iRhiSd,?Rmm")))]
  "TARGET_A16"
  "@
   bset %B2,%0
   bset\t%B2-8,1+%0
   bset\t%B2,%0
   or.w\t%X2,%0
   or.w\t%X2,%0
   or.w\t%X2,%0
   or.w\t%X2,%0"
  [(set_attr "flags" "n,n,n,sz,sz,sz,sz")]
  )

; - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

(define_insn "andqi3_24"
  [(set (match_operand:QI 0 "mra_operand" "=Sd,Rqi,RhlSd,RhlSd,??Rmm,??Rmm")
	(and:QI (match_operand:QI 1 "mra_operand" "%0,0,0,0,0,0")
		(match_operand 2 "mrai_operand" "Imb,Imb,iRhlSd,?Rmm,iRhlSd,?Rmm")))]
  "TARGET_A24"
  "@
   bclr\t%B2,%0
   bclr\t%B2,%0
   and.b\t%x2,%0
   and.b\t%x2,%0
   and.b\t%x2,%0
   and.b\t%x2,%0"
  [(set_attr "flags" "n,n,sz,sz,sz,sz")]
  )

(define_insn "andhi3_24"
  [(set (match_operand:HI 0 "mra_operand" "=Sd,Sd,Rqi,Rqi,RhiSd,??Rmm,RhiSd,??Rmm")
	(and:HI (match_operand:HI 1 "mra_operand" "%0,0,0,0,0,0,0,0")
		(match_operand:HI 2 "mrai_operand" "Imb,Imw,Imb,Imw,iRhiSd,?Rmm,?Rmm,iRhiSd")))]
  "TARGET_A24"
  "@
   bclr\t%B2,%0
   bclr\t%B2-8,1+%0
   bclr\t%B2,%h0
   bclr\t%B2-8,%H0
   and.w\t%X2,%0
   and.w\t%X2,%0
   and.w\t%X2,%0
   and.w\t%X2,%0"
  [(set_attr "flags" "n,n,n,n,sz,sz,sz,sz")]
  )



(define_insn "iorqi3_24"
  [(set (match_operand:QI 0 "mra_operand" "=RqiSd,RqiSd,??Rmm,RqiSd,??Rmm")
	(ior:QI (match_operand:QI 1 "mra_operand" "%0,0,0,0,0")
		(match_operand:QI 2 "mrai_operand" "Ilb,iRhlSd,iRhlSd,?Rmm,?Rmm")))]
  "TARGET_A24"
  "@
   bset\t%B2,%0
   or.b\t%x2,%0
   or.b\t%x2,%0
   or.b\t%x2,%0
   or.b\t%x2,%0"
  [(set_attr "flags" "n,sz,sz,sz,sz")]
  )

(define_insn "iorhi3_24"
  [(set (match_operand:HI 0 "mra_operand" "=Sd,Sd,Rqi,Rqi,RhiSd,RhiSd,??Rmm,??Rmm")
	(ior:HI (match_operand:HI 1 "mra_operand" "%0,0,0,0,0,0,0,0")
		(match_operand:HI 2 "mrai_operand" "Ilb,Ilw,Ilb,Ilw,iRhiSd,?Rmm,iRhiSd,?Rmm")))]
  "TARGET_A24"
  "@
   bset\t%B2,%0
   bset\t%B2-8,1+%0
   bset\t%B2,%h0
   bset\t%B2-8,%H0
   or.w\t%X2,%0
   or.w\t%X2,%0
   or.w\t%X2,%0
   or.w\t%X2,%0"
  [(set_attr "flags" "n,n,n,n,sz,sz,sz,sz")]
  )


; ----------------------------------------------------------------------

(define_expand "andqi3"
  [(set (match_operand:QI 0 "mra_operand" "")
	(and:QI (match_operand:QI 1 "mra_operand" "")
		(match_operand:QI 2 "mrai_operand" "")))]
  ""
  "if (TARGET_A16)
     emit_insn (gen_andqi3_16 (operands[0], operands[1], operands[2]));
   else
     emit_insn (gen_andqi3_24 (operands[0], operands[1], operands[2]));
   DONE;"
  )

(define_expand "andhi3"
  [(set (match_operand:HI 0 "mra_operand" "")
	(and:HI (match_operand:HI 1 "mra_operand" "")
		(match_operand:HI 2 "mrai_operand" "")))]
  ""
  "if (TARGET_A16)
     emit_insn (gen_andhi3_16 (operands[0], operands[1], operands[2]));
   else
     emit_insn (gen_andhi3_24 (operands[0], operands[1], operands[2]));
   DONE;"
  )

(define_expand "iorqi3"
  [(set (match_operand:QI 0 "mra_operand" "")
	(ior:QI (match_operand:QI 1 "mra_operand" "")
		(match_operand:QI 2 "mrai_operand" "")))]
  ""
  "if (TARGET_A16)
     emit_insn (gen_iorqi3_16 (operands[0], operands[1], operands[2]));
   else
     emit_insn (gen_iorqi3_24 (operands[0], operands[1], operands[2]));
   DONE;"
  )

(define_expand "iorhi3"
  [(set (match_operand:HI 0 "mra_operand" "")
	(ior:HI (match_operand:HI 1 "mra_operand" "")
		(match_operand:HI 2 "mrai_operand" "")))]
  ""
  "if (TARGET_A16)
     emit_insn (gen_iorhi3_16 (operands[0], operands[1], operands[2]));
   else
     emit_insn (gen_iorhi3_24 (operands[0], operands[1], operands[2]));
   DONE;"
  )

(define_insn "iorsi3"
  [(set (match_operand:SI 0 "mra_operand" "=RsiSd,RsiSd,??Rmm,??Rmm,??Rmm,RsiSd")
        (ior:SI (match_operand:SI 1 "mra_operand" "%0,0,0,0,0,0")
                (match_operand:SI 2 "mrai_operand" "i,?Rmm,i,RsiSd,?Rmm,RsiSd")))]
  ""
  "*
  switch (which_alternative)
    {
    case 0:
      output_asm_insn (\"or.w %X2,%h0\",operands);
      operands[2]= GEN_INT (INTVAL (operands[2]) >> 16);
      return \"or.w %X2,%H0\";
    case 1:
      return \"or.w %h2,%h0\;or.w %H2,%H0\";
    case 2:
      output_asm_insn (\"or.w %X2,%h0\",operands);
      operands[2]= GEN_INT (INTVAL (operands[2]) >> 16);
      return \"or.w %X2,%H0\";
    case 3:
      return \"or.w %h2,%h0\;or.w %H2,%H0\";
    case 4:
      return \"or.w %h2,%h0\;or.w %H2,%H0\";
    case 5:
      return \"or.w %h2,%h0\;or.w %H2,%H0\";
    }"
  [(set_attr "flags" "x,x,x,x,x,x")]
)

(define_insn "xorqi3"
  [(set (match_operand:QI 0 "mra_operand" "=RhlSd,RhlSd,??Rmm,??Rmm")
	(xor:QI (match_operand:QI 1 "mra_operand" "%0,0,0,0")
		(match_operand:QI 2 "mrai_operand" "iRhlSd,?Rmm,iRhlSd,?Rmm")))]
  ""
  "xor.b\t%x2,%0"
  [(set_attr "flags" "sz,sz,sz,sz")]
  )

(define_insn "xorhi3"
  [(set (match_operand:HI 0 "mra_operand" "=RhiSd,RhiSd,??Rmm,??Rmm")
	(xor:HI (match_operand:HI 1 "mra_operand" "%0,0,0,0")
		(match_operand:HI 2 "mrai_operand" "iRhiSd,?Rmm,iRhiSd,?Rmm")))]
  ""
  "xor.w\t%X2,%0"
  [(set_attr "flags" "sz,sz,sz,sz")]
  )

(define_insn "xorsi3"
  [(set (match_operand:SI 0 "mra_operand" "=RsiSd,RsiSd,??Rmm,??Rmm,??Rmm,RsiSd")
        (xor:SI (match_operand:SI 1 "mra_operand" "%0,0,0,0,0,0")
                (match_operand:SI 2 "mrai_operand" "i,?Rmm,i,RsiSd,?Rmm,RsiSd")))]
  ""
  "*
  switch (which_alternative)
    {
    case 0:
      output_asm_insn (\"xor.w %X2,%h0\",operands);
      operands[2]= GEN_INT (INTVAL (operands[2]) >> 16);
      return \"xor.w %X2,%H0\";
    case 1:
      return \"xor.w %h2,%h0\;xor.w %H2,%H0\";
    case 2:
      output_asm_insn (\"xor.w %X2,%h0\",operands);
      operands[2]= GEN_INT (INTVAL (operands[2]) >> 16);
      return \"xor.w %X2,%H0\";
    case 3:
      return \"xor.w %h2,%h0\;xor.w %H2,%H0\";
    case 4:
      return \"xor.w %h2,%h0\;xor.w %H2,%H0\";
    case 5:
      return \"xor.w %h2,%h0\;xor.w %H2,%H0\";
    }"
  [(set_attr "flags" "x,x,x,x,x,x")]
)

(define_insn "one_cmplqi2"
  [(set (match_operand:QI 0 "mra_operand" "=RhlSd,??Rmm")
	(not:QI (match_operand:QI 1 "mra_operand" "0,0")))]
  ""
  "not.b\t%0"
  [(set_attr "flags" "sz,sz")]
  )

(define_insn "one_cmplhi2"
  [(set (match_operand:HI 0 "mra_operand" "=RhiSd,??Rmm")
	(not:HI (match_operand:HI 1 "mra_operand" "0,0")))]
  ""
  "not.w\t%0"
  [(set_attr "flags" "sz,sz")]
  )

; Optimizations using bit opcodes

; We need this because combine only looks at three insns at a time,
; and the bclr_qi pattern uses four - mov, shift, not, and.  GCC
; should never expand this pattern, because it only shifts a constant
; by a constant, so gcc should do that itself.
(define_insn "shift1_qi"
  [(set (match_operand:QI 0 "mra_operand" "=Rqi")
	(ashift:QI (const_int 1)
		   (match_operand 1 "const_int_operand" "In4")))]
  ""
  "mov.b\t#1,%0\n\tshl.b\t%1,%0"
  )
(define_insn "shift1_hi"
  [(set (match_operand:HI 0 "mra_operand" "=Rhi")
	(ashift:HI (const_int 1)
		   (match_operand 1 "const_int_operand" "In4")))]
  ""
  "mov.w\t#1,%0\n\tshl.w\t%1,%0"
  )

; Generic insert-bit expander, needed so that we can use the bit
; opcodes for volatile bitfields.

(define_expand "insv"
  [(set (zero_extract:HI (match_operand:HI 0 "mra_operand" "")
			 (match_operand 1 "const_int_operand" "")
			 (match_operand 2 "const_int_operand" ""))
	(match_operand:HI 3 "const_int_operand" ""))]
  ""
  "if (m32c_expand_insv (operands))
     FAIL;
   DONE;"
  )
