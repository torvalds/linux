;; GCC machine description for Tensilica's Xtensa architecture.
;; Copyright (C) 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
;; Contributed by Bob Wilson (bwilson@tensilica.com) at Tensilica.

;; This file is part of GCC.

;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.

;; GCC is distributed in the hope that it will be useful, but WITHOUT
;; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;; or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
;; License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to the Free
;; Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
;; 02110-1301, USA.


(define_constants [
  (A0_REG		0)
  (A1_REG		1)
  (A7_REG		7)
  (A8_REG		8)

  (UNSPEC_NSAU		1)
  (UNSPEC_NOP		2)
  (UNSPEC_PLT		3)
  (UNSPEC_RET_ADDR	4)
  (UNSPECV_SET_FP	1)
  (UNSPECV_ENTRY	2)
])


;; Attributes.

(define_attr "type"
  "unknown,jump,call,load,store,move,arith,multi,nop,farith,fmadd,fdiv,fsqrt,fconv,fload,fstore,mul16,mul32,div32,mac16,rsr,wsr"
  (const_string "unknown"))

(define_attr "mode"
  "unknown,none,QI,HI,SI,DI,SF,DF,BL"
  (const_string "unknown"))

(define_attr "length" "" (const_int 1))

;; Describe a user's asm statement.
(define_asm_attributes
  [(set_attr "type" "multi")])


;; Pipeline model.

;; The Xtensa basically has simple 5-stage RISC pipeline.
;; Most instructions complete in 1 cycle, and it is OK to assume that
;; everything is fully pipelined.  The exceptions have special insn
;; reservations in the pipeline description below.  The Xtensa can
;; issue one instruction per cycle, so defining CPU units is unnecessary.

(define_insn_reservation "xtensa_any_insn" 1
			 (eq_attr "type" "!load,fload,rsr,mul16,mul32,fmadd,fconv")
			 "nothing")

(define_insn_reservation "xtensa_memory" 2
			 (eq_attr "type" "load,fload")
			 "nothing")

(define_insn_reservation "xtensa_sreg" 2
			 (eq_attr "type" "rsr")
			 "nothing")

(define_insn_reservation "xtensa_mul16" 2
			 (eq_attr "type" "mul16")
			 "nothing")

(define_insn_reservation "xtensa_mul32" 2
			 (eq_attr "type" "mul32")
			 "nothing")

(define_insn_reservation "xtensa_fmadd" 4
			 (eq_attr "type" "fmadd")
			 "nothing")

(define_insn_reservation "xtensa_fconv" 2
			 (eq_attr "type" "fconv")
			 "nothing")

;; Include predicate definitions

(include "predicates.md")


;; Addition.

(define_expand "adddi3"
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (match_operand:DI 1 "register_operand" "")
		 (match_operand:DI 2 "register_operand" "")))]
  ""
{
  rtx srclo;
  rtx dstlo = gen_lowpart (SImode, operands[0]);
  rtx src1lo = gen_lowpart (SImode, operands[1]);
  rtx src2lo = gen_lowpart (SImode, operands[2]);

  rtx dsthi = gen_highpart (SImode, operands[0]);
  rtx src1hi = gen_highpart (SImode, operands[1]);
  rtx src2hi = gen_highpart (SImode, operands[2]);

  /* Either source can be used for overflow checking, as long as it's
     not clobbered by the first addition.  */
  if (!rtx_equal_p (dstlo, src1lo))
    srclo = src1lo;
  else if (!rtx_equal_p (dstlo, src2lo))
    srclo = src2lo;
  else
    {
      srclo = gen_reg_rtx (SImode);
      emit_move_insn (srclo, src1lo);
    }

  emit_insn (gen_addsi3 (dstlo, src1lo, src2lo));
  emit_insn (gen_addsi3 (dsthi, src1hi, src2hi));
  emit_insn (gen_adddi_carry (dsthi, dstlo, srclo));
  DONE;
})

;; Represent the add-carry operation as an atomic operation instead of
;; expanding it to a conditional branch.  Otherwise, the edge
;; profiling code breaks because inserting the count increment code
;; causes a new jump insn to be added.

(define_insn "adddi_carry"
  [(set (match_operand:SI 0 "register_operand" "+a")
	(plus:SI (ltu:SI (match_operand:SI 1 "register_operand" "r")
			 (match_operand:SI 2 "register_operand" "r"))
		 (match_dup 0)))]
  ""
  "bgeu\t%1, %2, 0f\;addi\t%0, %0, 1\;0:"
  [(set_attr "type"	"multi")
   (set_attr "mode"	"SI")
   (set_attr "length"	"6")])

(define_insn "addsi3"
  [(set (match_operand:SI 0 "register_operand" "=D,D,a,a,a")
	(plus:SI (match_operand:SI 1 "register_operand" "%d,d,r,r,r")
		 (match_operand:SI 2 "add_operand" "d,O,r,J,N")))]
  ""
  "@
   add.n\t%0, %1, %2
   addi.n\t%0, %1, %d2
   add\t%0, %1, %2
   addi\t%0, %1, %d2
   addmi\t%0, %1, %x2"
  [(set_attr "type"	"arith,arith,arith,arith,arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"2,2,3,3,3")])

(define_insn "*addx2"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(plus:SI (mult:SI (match_operand:SI 1 "register_operand" "r")
			  (const_int 2))
		 (match_operand:SI 2 "register_operand" "r")))]
  "TARGET_ADDX"
  "addx2\t%0, %1, %2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "*addx4"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(plus:SI (mult:SI (match_operand:SI 1 "register_operand" "r")
			  (const_int 4))
		 (match_operand:SI 2 "register_operand" "r")))]
  "TARGET_ADDX"
  "addx4\t%0, %1, %2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "*addx8"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(plus:SI (mult:SI (match_operand:SI 1 "register_operand" "r")
			  (const_int 8))
		 (match_operand:SI 2 "register_operand" "r")))]
  "TARGET_ADDX"
  "addx8\t%0, %1, %2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "addsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(plus:SF (match_operand:SF 1 "register_operand" "%f")
		 (match_operand:SF 2 "register_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "add.s\t%0, %1, %2"
  [(set_attr "type"	"fmadd")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3")])


;; Subtraction.

(define_expand "subdi3"
  [(set (match_operand:DI 0 "register_operand" "")
	(minus:DI (match_operand:DI 1 "register_operand" "")
		  (match_operand:DI 2 "register_operand" "")))]
  ""
{
  rtx dstlo = gen_lowpart (SImode, operands[0]);
  rtx src1lo = gen_lowpart (SImode, operands[1]);
  rtx src2lo = gen_lowpart (SImode, operands[2]);

  rtx dsthi = gen_highpart (SImode, operands[0]);
  rtx src1hi = gen_highpart (SImode, operands[1]);
  rtx src2hi = gen_highpart (SImode, operands[2]);

  emit_insn (gen_subsi3 (dsthi, src1hi, src2hi));
  emit_insn (gen_subdi_carry (dsthi, src1lo, src2lo));
  emit_insn (gen_subsi3 (dstlo, src1lo, src2lo));
  DONE;
})

(define_insn "subdi_carry"
  [(set (match_operand:SI 0 "register_operand" "+a")
	(minus:SI (match_dup 0)
		  (ltu:SI (match_operand:SI 1 "register_operand" "r")
			  (match_operand:SI 2 "register_operand" "r"))))]
  ""
  "bgeu\t%1, %2, 0f\;addi\t%0, %0, -1\;0:"
  [(set_attr "type"	"multi")
   (set_attr "mode"	"SI")
   (set_attr "length"	"6")])

(define_insn "subsi3"
  [(set (match_operand:SI 0 "register_operand" "=a")
        (minus:SI (match_operand:SI 1 "register_operand" "r")
		  (match_operand:SI 2 "register_operand" "r")))]
  ""
  "sub\t%0, %1, %2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "*subx2"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(minus:SI (mult:SI (match_operand:SI 1 "register_operand" "r")
			   (const_int 2))
		  (match_operand:SI 2 "register_operand" "r")))]
  "TARGET_ADDX"
  "subx2\t%0, %1, %2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "*subx4"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(minus:SI (mult:SI (match_operand:SI 1 "register_operand" "r")
			   (const_int 4))
		  (match_operand:SI 2 "register_operand" "r")))]
  "TARGET_ADDX"
  "subx4\t%0, %1, %2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "*subx8"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(minus:SI (mult:SI (match_operand:SI 1 "register_operand" "r")
			   (const_int 8))
		  (match_operand:SI 2 "register_operand" "r")))]
  "TARGET_ADDX"
  "subx8\t%0, %1, %2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "subsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(minus:SF (match_operand:SF 1 "register_operand" "f")
		  (match_operand:SF 2 "register_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "sub.s\t%0, %1, %2"
  [(set_attr "type"	"fmadd")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3")])


;; Multiplication.

(define_insn "mulsi3"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(mult:SI (match_operand:SI 1 "register_operand" "%r")
		 (match_operand:SI 2 "register_operand" "r")))]
  "TARGET_MUL32"
  "mull\t%0, %1, %2"
  [(set_attr "type"	"mul32")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "mulhisi3"
  [(set (match_operand:SI 0 "register_operand" "=C,A")
	(mult:SI (sign_extend:SI
		  (match_operand:HI 1 "register_operand" "%r,r"))
		 (sign_extend:SI
		  (match_operand:HI 2 "register_operand" "r,r"))))]
  "TARGET_MUL16 || TARGET_MAC16"
  "@
   mul16s\t%0, %1, %2
   mul.aa.ll\t%1, %2"
  [(set_attr "type"	"mul16,mac16")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3,3")])

(define_insn "umulhisi3"
  [(set (match_operand:SI 0 "register_operand" "=C,A")
	(mult:SI (zero_extend:SI
		  (match_operand:HI 1 "register_operand" "%r,r"))
		 (zero_extend:SI
		  (match_operand:HI 2 "register_operand" "r,r"))))]
  "TARGET_MUL16 || TARGET_MAC16"
  "@
   mul16u\t%0, %1, %2
   umul.aa.ll\t%1, %2"
  [(set_attr "type"	"mul16,mac16")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3,3")])

(define_insn "muladdhisi"
  [(set (match_operand:SI 0 "register_operand" "=A")
	(plus:SI (mult:SI (sign_extend:SI
			   (match_operand:HI 1 "register_operand" "%r"))
			  (sign_extend:SI
			   (match_operand:HI 2 "register_operand" "r")))
		 (match_operand:SI 3 "register_operand" "0")))]
  "TARGET_MAC16"
  "mula.aa.ll\t%1, %2"
  [(set_attr "type"	"mac16")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "mulsubhisi"
  [(set (match_operand:SI 0 "register_operand" "=A")
	(minus:SI (match_operand:SI 1 "register_operand" "0")
		  (mult:SI (sign_extend:SI
			    (match_operand:HI 2 "register_operand" "%r"))
			   (sign_extend:SI
			    (match_operand:HI 3 "register_operand" "r")))))]
  "TARGET_MAC16"
  "muls.aa.ll\t%2, %3"
  [(set_attr "type"	"mac16")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "mulsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(mult:SF (match_operand:SF 1 "register_operand" "%f")
		 (match_operand:SF 2 "register_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "mul.s\t%0, %1, %2"
  [(set_attr "type"	"fmadd")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3")])

(define_insn "muladdsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(plus:SF (mult:SF (match_operand:SF 1 "register_operand" "%f")
			  (match_operand:SF 2 "register_operand" "f"))
		 (match_operand:SF 3 "register_operand" "0")))]
  "TARGET_HARD_FLOAT && TARGET_FUSED_MADD"
  "madd.s\t%0, %1, %2"
  [(set_attr "type"	"fmadd")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3")])

(define_insn "mulsubsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(minus:SF (match_operand:SF 1 "register_operand" "0")
		  (mult:SF (match_operand:SF 2 "register_operand" "%f")
			   (match_operand:SF 3 "register_operand" "f"))))]
  "TARGET_HARD_FLOAT && TARGET_FUSED_MADD"
  "msub.s\t%0, %2, %3"
  [(set_attr "type"	"fmadd")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3")])


;; Division.

(define_insn "divsi3"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(div:SI (match_operand:SI 1 "register_operand" "r")
		(match_operand:SI 2 "register_operand" "r")))]
  "TARGET_DIV32"
  "quos\t%0, %1, %2"
  [(set_attr "type"	"div32")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "udivsi3"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(udiv:SI (match_operand:SI 1 "register_operand" "r")
		 (match_operand:SI 2 "register_operand" "r")))]
  "TARGET_DIV32"
  "quou\t%0, %1, %2"
  [(set_attr "type"	"div32")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "divsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(div:SF (match_operand:SF 1 "register_operand" "f")
		(match_operand:SF 2 "register_operand" "f")))]
  "TARGET_HARD_FLOAT_DIV"
  "div.s\t%0, %1, %2"
  [(set_attr "type"	"fdiv")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3")])

(define_insn "*recipsf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(div:SF (match_operand:SF 1 "const_float_1_operand" "")
		(match_operand:SF 2 "register_operand" "f")))]
  "TARGET_HARD_FLOAT_RECIP && flag_unsafe_math_optimizations"
  "recip.s\t%0, %2"
  [(set_attr "type"	"fdiv")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3")])


;; Remainders.

(define_insn "modsi3"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(mod:SI (match_operand:SI 1 "register_operand" "r")
		(match_operand:SI 2 "register_operand" "r")))]
  "TARGET_DIV32"
  "rems\t%0, %1, %2"
  [(set_attr "type"	"div32")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "umodsi3"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(umod:SI (match_operand:SI 1 "register_operand" "r")
		 (match_operand:SI 2 "register_operand" "r")))]
  "TARGET_DIV32"
  "remu\t%0, %1, %2"
  [(set_attr "type"	"div32")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])


;; Square roots.

(define_insn "sqrtsf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(sqrt:SF (match_operand:SF 1 "register_operand" "f")))]
  "TARGET_HARD_FLOAT_SQRT"
  "sqrt.s\t%0, %1"
  [(set_attr "type"	"fsqrt")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3")])

(define_insn "*rsqrtsf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(div:SF (match_operand:SF 1 "const_float_1_operand" "")
		(sqrt:SF (match_operand:SF 2 "register_operand" "f"))))]
  "TARGET_HARD_FLOAT_RSQRT && flag_unsafe_math_optimizations"
  "rsqrt.s\t%0, %2"
  [(set_attr "type"	"fsqrt")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3")])


;; Absolute value.

(define_insn "abssi2"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(abs:SI (match_operand:SI 1 "register_operand" "r")))]
  "TARGET_ABS"
  "abs\t%0, %1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "abssf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(abs:SF (match_operand:SF 1 "register_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "abs.s\t%0, %1"
  [(set_attr "type"	"farith")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3")])


;; Min and max.

(define_insn "sminsi3"
  [(set (match_operand:SI 0 "register_operand" "=a")
        (smin:SI (match_operand:SI 1 "register_operand" "%r")
                 (match_operand:SI 2 "register_operand" "r")))]
  "TARGET_MINMAX"
  "min\t%0, %1, %2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "uminsi3"
  [(set (match_operand:SI 0 "register_operand" "=a")
        (umin:SI (match_operand:SI 1 "register_operand" "%r")
                 (match_operand:SI 2 "register_operand" "r")))]
  "TARGET_MINMAX"
  "minu\t%0, %1, %2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "smaxsi3"
  [(set (match_operand:SI 0 "register_operand" "=a")
        (smax:SI (match_operand:SI 1 "register_operand" "%r")
                 (match_operand:SI 2 "register_operand" "r")))]
  "TARGET_MINMAX"
  "max\t%0, %1, %2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "umaxsi3"
  [(set (match_operand:SI 0 "register_operand" "=a")
        (umax:SI (match_operand:SI 1 "register_operand" "%r")
                 (match_operand:SI 2 "register_operand" "r")))]
  "TARGET_MINMAX"
  "maxu\t%0, %1, %2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])


;; Find first bit.

(define_expand "ffssi2"
  [(set (match_operand:SI 0 "register_operand" "")
	(ffs:SI (match_operand:SI 1 "register_operand" "")))]
  "TARGET_NSA"
{
  rtx temp = gen_reg_rtx (SImode);
  emit_insn (gen_negsi2 (temp, operands[1]));
  emit_insn (gen_andsi3 (temp, temp, operands[1]));
  emit_insn (gen_nsau (temp, temp));
  emit_insn (gen_negsi2 (temp, temp));
  emit_insn (gen_addsi3 (operands[0], temp, GEN_INT (32)));
  DONE;
})

;; There is no RTL operator corresponding to NSAU.
(define_insn "nsau"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(unspec:SI [(match_operand:SI 1 "register_operand" "r")] UNSPEC_NSAU))]
  "TARGET_NSA"
  "nsau\t%0, %1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])


;; Negation and one's complement.

(define_insn "negsi2"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(neg:SI (match_operand:SI 1 "register_operand" "r")))]
  ""
  "neg\t%0, %1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_expand "one_cmplsi2"
  [(set (match_operand:SI 0 "register_operand" "")
	(not:SI (match_operand:SI 1 "register_operand" "")))]
  ""
{
  rtx temp = gen_reg_rtx (SImode);
  emit_insn (gen_movsi (temp, constm1_rtx));
  emit_insn (gen_xorsi3 (operands[0], temp, operands[1]));
  DONE;
})

(define_insn "negsf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(neg:SF (match_operand:SF 1 "register_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "neg.s\t%0, %1"
  [(set_attr "type"	"farith")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3")])


;; Logical instructions.

(define_insn "andsi3"
  [(set (match_operand:SI 0 "register_operand" "=a,a")
	(and:SI (match_operand:SI 1 "register_operand" "%r,r")
		(match_operand:SI 2 "mask_operand" "P,r")))]
  ""
  "@
   extui\t%0, %1, 0, %K2
   and\t%0, %1, %2"
  [(set_attr "type"	"arith,arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3,3")])

(define_insn "iorsi3"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(ior:SI (match_operand:SI 1 "register_operand" "%r")
		(match_operand:SI 2 "register_operand" "r")))]
  ""
  "or\t%0, %1, %2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "xorsi3"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(xor:SI (match_operand:SI 1 "register_operand" "%r")
		(match_operand:SI 2 "register_operand" "r")))]
  ""
  "xor\t%0, %1, %2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])


;; Zero-extend instructions.

(define_insn "zero_extendhisi2"
  [(set (match_operand:SI 0 "register_operand" "=a,a")
	(zero_extend:SI (match_operand:HI 1 "nonimmed_operand" "r,U")))]
  ""
  "@
   extui\t%0, %1, 0, 16
   l16ui\t%0, %1"
  [(set_attr "type"	"arith,load")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3,3")])

(define_insn "zero_extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "=a,a")
	(zero_extend:SI (match_operand:QI 1 "nonimmed_operand" "r,U")))]
  ""
  "@
   extui\t%0, %1, 0, 8
   l8ui\t%0, %1"
  [(set_attr "type"	"arith,load")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3,3")])


;; Sign-extend instructions.

(define_expand "extendhisi2"
  [(set (match_operand:SI 0 "register_operand" "")
	(sign_extend:SI (match_operand:HI 1 "register_operand" "")))]
  ""
{
  if (sext_operand (operands[1], HImode))
    emit_insn (gen_extendhisi2_internal (operands[0], operands[1]));
  else
    xtensa_extend_reg (operands[0], operands[1]);
  DONE;
})

(define_insn "extendhisi2_internal"
  [(set (match_operand:SI 0 "register_operand" "=B,a")
	(sign_extend:SI (match_operand:HI 1 "sext_operand" "r,U")))]
  ""
  "@
   sext\t%0, %1, 15
   l16si\t%0, %1"
  [(set_attr "type"	"arith,load")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3,3")])

(define_expand "extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "")
	(sign_extend:SI (match_operand:QI 1 "register_operand" "")))]
  ""
{
  if (TARGET_SEXT)
    emit_insn (gen_extendqisi2_internal (operands[0], operands[1]));
  else
    xtensa_extend_reg (operands[0], operands[1]);
  DONE;
})

(define_insn "extendqisi2_internal"
  [(set (match_operand:SI 0 "register_operand" "=B")
	(sign_extend:SI (match_operand:QI 1 "register_operand" "r")))]
  "TARGET_SEXT"
  "sext\t%0, %1, 7"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])


;; Field extract instructions.

(define_expand "extv"
  [(set (match_operand:SI 0 "register_operand" "")
	(sign_extract:SI (match_operand:SI 1 "register_operand" "")
			 (match_operand:SI 2 "const_int_operand" "")
			 (match_operand:SI 3 "const_int_operand" "")))]
  "TARGET_SEXT"
{
  if (!sext_fldsz_operand (operands[2], SImode))
    FAIL;

  /* We could expand to a right shift followed by SEXT but that's
     no better than the standard left and right shift sequence.  */
  if (!lsbitnum_operand (operands[3], SImode))
    FAIL;

  emit_insn (gen_extv_internal (operands[0], operands[1],
				operands[2], operands[3]));
  DONE;
})

(define_insn "extv_internal"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(sign_extract:SI (match_operand:SI 1 "register_operand" "r")
			 (match_operand:SI 2 "sext_fldsz_operand" "i")
			 (match_operand:SI 3 "lsbitnum_operand" "i")))]
  "TARGET_SEXT"
{
  int fldsz = INTVAL (operands[2]);
  operands[2] = GEN_INT (fldsz - 1);
  return "sext\t%0, %1, %2";
}
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_expand "extzv"
  [(set (match_operand:SI 0 "register_operand" "")
	(zero_extract:SI (match_operand:SI 1 "register_operand" "")
			 (match_operand:SI 2 "const_int_operand" "")
			 (match_operand:SI 3 "const_int_operand" "")))]
  ""
{
  if (!extui_fldsz_operand (operands[2], SImode))
    FAIL;
  emit_insn (gen_extzv_internal (operands[0], operands[1],
				 operands[2], operands[3]));
  DONE;
})

(define_insn "extzv_internal"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(zero_extract:SI (match_operand:SI 1 "register_operand" "r")
			 (match_operand:SI 2 "extui_fldsz_operand" "i")
			 (match_operand:SI 3 "const_int_operand" "i")))]
  ""
{
  int shift;
  if (BITS_BIG_ENDIAN)
    shift = (32 - (INTVAL (operands[2]) + INTVAL (operands[3]))) & 0x1f;
  else
    shift = INTVAL (operands[3]) & 0x1f;
  operands[3] = GEN_INT (shift);
  return "extui\t%0, %1, %3, %2";
}
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])


;; Conversions.

(define_insn "fix_truncsfsi2"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(fix:SI (match_operand:SF 1 "register_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "trunc.s\t%0, %1, 0"
  [(set_attr "type"	"fconv")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3")])

(define_insn "fixuns_truncsfsi2"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(unsigned_fix:SI (match_operand:SF 1 "register_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "utrunc.s\t%0, %1, 0"
  [(set_attr "type"	"fconv")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3")])

(define_insn "floatsisf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(float:SF (match_operand:SI 1 "register_operand" "a")))]
  "TARGET_HARD_FLOAT"
  "float.s\t%0, %1, 0"
  [(set_attr "type"	"fconv")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3")])

(define_insn "floatunssisf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(unsigned_float:SF (match_operand:SI 1 "register_operand" "a")))]
  "TARGET_HARD_FLOAT"
  "ufloat.s\t%0, %1, 0"
  [(set_attr "type"	"fconv")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3")])


;; Data movement instructions.

;; 64-bit Integer moves

(define_expand "movdi"
  [(set (match_operand:DI 0 "nonimmed_operand" "")
	(match_operand:DI 1 "general_operand" ""))]
  ""
{
  if (CONSTANT_P (operands[1]) && !TARGET_CONST16)
    operands[1] = force_const_mem (DImode, operands[1]);

  if (!register_operand (operands[0], DImode)
      && !register_operand (operands[1], DImode))
    operands[1] = force_reg (DImode, operands[1]);

  operands[1] = xtensa_copy_incoming_a7 (operands[1]);
})

(define_insn_and_split "movdi_internal"
  [(set (match_operand:DI 0 "nonimmed_operand" "=a,W,a,a,U")
	(match_operand:DI 1 "move_operand" "r,i,T,U,r"))]
  "register_operand (operands[0], DImode)
   || register_operand (operands[1], DImode)"
  "#"
  "reload_completed"
  [(set (match_dup 0) (match_dup 2))
   (set (match_dup 1) (match_dup 3))]
{
  xtensa_split_operand_pair (operands, SImode);
  if (reg_overlap_mentioned_p (operands[0], operands[3]))
    {
      rtx tmp;
      tmp = operands[0], operands[0] = operands[1], operands[1] = tmp;
      tmp = operands[2], operands[2] = operands[3], operands[3] = tmp;
    }
})

;; 32-bit Integer moves

(define_expand "movsi"
  [(set (match_operand:SI 0 "nonimmed_operand" "")
	(match_operand:SI 1 "general_operand" ""))]
  ""
{
  if (xtensa_emit_move_sequence (operands, SImode))
    DONE;
})

(define_insn "movsi_internal"
  [(set (match_operand:SI 0 "nonimmed_operand" "=D,D,D,D,R,R,a,q,a,W,a,a,U,*a,*A")
	(match_operand:SI 1 "move_operand" "M,D,d,R,D,d,r,r,I,i,T,U,r,*A,*r"))]
  "xtensa_valid_move (SImode, operands)"
  "@
   movi.n\t%0, %x1
   mov.n\t%0, %1
   mov.n\t%0, %1
   %v1l32i.n\t%0, %1
   %v0s32i.n\t%1, %0
   %v0s32i.n\t%1, %0
   mov\t%0, %1
   movsp\t%0, %1
   movi\t%0, %x1
   const16\t%0, %t1\;const16\t%0, %b1
   %v1l32r\t%0, %1
   %v1l32i\t%0, %1
   %v0s32i\t%1, %0
   rsr\t%0, ACCLO
   wsr\t%1, ACCLO"
  [(set_attr "type" "move,move,move,load,store,store,move,move,move,move,load,load,store,rsr,wsr")
   (set_attr "mode"	"SI")
   (set_attr "length"	"2,2,2,2,2,2,3,3,3,6,3,3,3,3,3")])

;; 16-bit Integer moves

(define_expand "movhi"
  [(set (match_operand:HI 0 "nonimmed_operand" "")
	(match_operand:HI 1 "general_operand" ""))]
  ""
{
  if (xtensa_emit_move_sequence (operands, HImode))
    DONE;
})

(define_insn "movhi_internal"
  [(set (match_operand:HI 0 "nonimmed_operand" "=D,D,a,a,a,U,*a,*A")
	(match_operand:HI 1 "move_operand" "M,d,r,I,U,r,*A,*r"))]
  "xtensa_valid_move (HImode, operands)"
  "@
   movi.n\t%0, %x1
   mov.n\t%0, %1
   mov\t%0, %1
   movi\t%0, %x1
   %v1l16ui\t%0, %1
   %v0s16i\t%1, %0
   rsr\t%0, ACCLO
   wsr\t%1, ACCLO"
  [(set_attr "type"	"move,move,move,move,load,store,rsr,wsr")
   (set_attr "mode"	"HI")
   (set_attr "length"	"2,2,3,3,3,3,3,3")])

;; 8-bit Integer moves

(define_expand "movqi"
  [(set (match_operand:QI 0 "nonimmed_operand" "")
	(match_operand:QI 1 "general_operand" ""))]
  ""
{
  if (xtensa_emit_move_sequence (operands, QImode))
    DONE;
})

(define_insn "movqi_internal"
  [(set (match_operand:QI 0 "nonimmed_operand" "=D,D,a,a,a,U,*a,*A")
	(match_operand:QI 1 "move_operand" "M,d,r,I,U,r,*A,*r"))]
  "xtensa_valid_move (QImode, operands)"
  "@
   movi.n\t%0, %x1
   mov.n\t%0, %1
   mov\t%0, %1
   movi\t%0, %x1
   %v1l8ui\t%0, %1
   %v0s8i\t%1, %0
   rsr\t%0, ACCLO
   wsr\t%1, ACCLO"
  [(set_attr "type"	"move,move,move,move,load,store,rsr,wsr")
   (set_attr "mode"	"QI")
   (set_attr "length"	"2,2,3,3,3,3,3,3")])

;; 32-bit floating point moves

(define_expand "movsf"
  [(set (match_operand:SF 0 "nonimmed_operand" "")
	(match_operand:SF 1 "general_operand" ""))]
  ""
{
  if (!TARGET_CONST16 && CONSTANT_P (operands[1]))
    operands[1] = force_const_mem (SFmode, operands[1]);

  if ((!register_operand (operands[0], SFmode)
       && !register_operand (operands[1], SFmode))
      || (FP_REG_P (xt_true_regnum (operands[0]))
	  && !(reload_in_progress | reload_completed)
	  && (constantpool_mem_p (operands[1])
	      || CONSTANT_P (operands[1]))))
    operands[1] = force_reg (SFmode, operands[1]);

  operands[1] = xtensa_copy_incoming_a7 (operands[1]);
})

(define_insn "movsf_internal"
  [(set (match_operand:SF 0 "nonimmed_operand" "=f,f,U,D,D,R,a,f,a,W,a,a,U")
	(match_operand:SF 1 "move_operand" "f,U,f,d,R,d,r,r,f,iF,T,U,r"))]
  "((register_operand (operands[0], SFmode)
     || register_operand (operands[1], SFmode))
    && !(FP_REG_P (xt_true_regnum (operands[0]))
         && (constantpool_mem_p (operands[1]) || CONSTANT_P (operands[1]))))"
  "@
   mov.s\t%0, %1
   %v1lsi\t%0, %1
   %v0ssi\t%1, %0
   mov.n\t%0, %1
   %v1l32i.n\t%0, %1
   %v0s32i.n\t%1, %0
   mov\t%0, %1
   wfr\t%0, %1
   rfr\t%0, %1
   const16\t%0, %t1\;const16\t%0, %b1
   %v1l32r\t%0, %1
   %v1l32i\t%0, %1
   %v0s32i\t%1, %0"
  [(set_attr "type"	"farith,fload,fstore,move,load,store,move,farith,farith,move,load,load,store")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3,3,3,2,2,2,3,3,3,6,3,3,3")])

(define_insn "*lsiu"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(mem:SF (plus:SI (match_operand:SI 1 "register_operand" "+a")
			 (match_operand:SI 2 "fpmem_offset_operand" "i"))))
   (set (match_dup 1)
	(plus:SI (match_dup 1) (match_dup 2)))]
  "TARGET_HARD_FLOAT"
{
  if (volatile_refs_p (PATTERN (insn)))
    output_asm_insn ("memw", operands);
  return "lsiu\t%0, %1, %2";
}
  [(set_attr "type"	"fload")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3")])

(define_insn "*ssiu"
  [(set (mem:SF (plus:SI (match_operand:SI 0 "register_operand" "+a")
			 (match_operand:SI 1 "fpmem_offset_operand" "i")))
	(match_operand:SF 2 "register_operand" "f"))
   (set (match_dup 0)
	(plus:SI (match_dup 0) (match_dup 1)))]
  "TARGET_HARD_FLOAT"
{
  if (volatile_refs_p (PATTERN (insn)))
    output_asm_insn ("memw", operands);
  return "ssiu\t%2, %0, %1";
}
  [(set_attr "type"	"fstore")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3")])

;; 64-bit floating point moves

(define_expand "movdf"
  [(set (match_operand:DF 0 "nonimmed_operand" "")
	(match_operand:DF 1 "general_operand" ""))]
  ""
{
  if (CONSTANT_P (operands[1]) && !TARGET_CONST16)
    operands[1] = force_const_mem (DFmode, operands[1]);

  if (!register_operand (operands[0], DFmode)
      && !register_operand (operands[1], DFmode))
    operands[1] = force_reg (DFmode, operands[1]);

  operands[1] = xtensa_copy_incoming_a7 (operands[1]);
})

(define_insn_and_split "movdf_internal"
  [(set (match_operand:DF 0 "nonimmed_operand" "=a,W,a,a,U")
	(match_operand:DF 1 "move_operand" "r,iF,T,U,r"))]
  "register_operand (operands[0], DFmode)
   || register_operand (operands[1], DFmode)"
  "#"
  "reload_completed"
  [(set (match_dup 0) (match_dup 2))
   (set (match_dup 1) (match_dup 3))]
{
  xtensa_split_operand_pair (operands, SFmode);
  if (reg_overlap_mentioned_p (operands[0], operands[3]))
    {
      rtx tmp;
      tmp = operands[0], operands[0] = operands[1], operands[1] = tmp;
      tmp = operands[2], operands[2] = operands[3], operands[3] = tmp;
    }
})

;; Block moves

(define_expand "movmemsi"
  [(parallel [(set (match_operand:BLK 0 "" "")
		   (match_operand:BLK 1 "" ""))
	      (use (match_operand:SI 2 "arith_operand" ""))
	      (use (match_operand:SI 3 "const_int_operand" ""))])]
  ""
{
  if (!xtensa_expand_block_move (operands))
    FAIL;
  DONE;
})


;; Shift instructions.

(define_expand "ashlsi3"
  [(set (match_operand:SI 0 "register_operand" "")
	(ashift:SI (match_operand:SI 1 "register_operand" "")
		   (match_operand:SI 2 "arith_operand" "")))]
  ""
{
  operands[1] = xtensa_copy_incoming_a7 (operands[1]);
})

(define_insn "ashlsi3_internal"
  [(set (match_operand:SI 0 "register_operand" "=a,a")
	(ashift:SI (match_operand:SI 1 "register_operand" "r,r")
		   (match_operand:SI 2 "arith_operand" "J,r")))]
  ""      
  "@
   slli\t%0, %1, %R2
   ssl\t%2\;sll\t%0, %1"
  [(set_attr "type"	"arith,arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3,6")])

(define_insn "ashrsi3"
  [(set (match_operand:SI 0 "register_operand" "=a,a")
	(ashiftrt:SI (match_operand:SI 1 "register_operand" "r,r")
		     (match_operand:SI 2 "arith_operand" "J,r")))]
  ""
  "@
   srai\t%0, %1, %R2
   ssr\t%2\;sra\t%0, %1"
  [(set_attr "type"	"arith,arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3,6")])

(define_insn "lshrsi3"
  [(set (match_operand:SI 0 "register_operand" "=a,a")
	(lshiftrt:SI (match_operand:SI 1 "register_operand" "r,r")
		     (match_operand:SI 2 "arith_operand" "J,r")))]
  ""
{
  if (which_alternative == 0)
    {
      if ((INTVAL (operands[2]) & 0x1f) < 16)
        return "srli\t%0, %1, %R2";
      else
      	return "extui\t%0, %1, %R2, %L2";
    }
  return "ssr\t%2\;srl\t%0, %1";
}
  [(set_attr "type"	"arith,arith")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3,6")])

(define_insn "rotlsi3"
  [(set (match_operand:SI 0 "register_operand" "=a,a")
	(rotate:SI (match_operand:SI 1 "register_operand" "r,r")
		     (match_operand:SI 2 "arith_operand" "J,r")))]
  ""
  "@
   ssai\t%L2\;src\t%0, %1, %1
   ssl\t%2\;src\t%0, %1, %1"
  [(set_attr "type"	"multi,multi")
   (set_attr "mode"	"SI")
   (set_attr "length"	"6,6")])

(define_insn "rotrsi3"
  [(set (match_operand:SI 0 "register_operand" "=a,a")
	(rotatert:SI (match_operand:SI 1 "register_operand" "r,r")
		     (match_operand:SI 2 "arith_operand" "J,r")))]
  ""
  "@
   ssai\t%R2\;src\t%0, %1, %1
   ssr\t%2\;src\t%0, %1, %1"
  [(set_attr "type"	"multi,multi")
   (set_attr "mode"	"SI")
   (set_attr "length"	"6,6")])


;; Comparisons.

;; Handle comparisons by stashing away the operands and then using that
;; information in the subsequent conditional branch.

(define_expand "cmpsi"
  [(set (cc0)
	(compare:CC (match_operand:SI 0 "register_operand" "")
		    (match_operand:SI 1 "nonmemory_operand" "")))]
  ""
{
  branch_cmp[0] = operands[0];
  branch_cmp[1] = operands[1];
  branch_type = CMP_SI;
  DONE;
})

(define_expand "tstsi"
  [(set (cc0)
	(match_operand:SI 0 "register_operand" ""))]
  ""
{
  branch_cmp[0] = operands[0];
  branch_cmp[1] = const0_rtx;
  branch_type = CMP_SI;
  DONE;
})

(define_expand "cmpsf"
  [(set (cc0)
	(compare:CC (match_operand:SF 0 "register_operand" "")
		    (match_operand:SF 1 "register_operand" "")))]
  "TARGET_HARD_FLOAT"
{
  branch_cmp[0] = operands[0];
  branch_cmp[1] = operands[1];
  branch_type = CMP_SF;
  DONE;
})


;; Conditional branches.

(define_expand "beq"
  [(set (pc)
	(if_then_else (eq (cc0) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  xtensa_expand_conditional_branch (operands, EQ);
  DONE;
})

(define_expand "bne"
  [(set (pc)
	(if_then_else (ne (cc0) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  xtensa_expand_conditional_branch (operands, NE);
  DONE;
})

(define_expand "bgt"
  [(set (pc)
	(if_then_else (gt (cc0) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  xtensa_expand_conditional_branch (operands, GT);
  DONE;
})

(define_expand "bge"
  [(set (pc)
	(if_then_else (ge (cc0) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  xtensa_expand_conditional_branch (operands, GE);
  DONE;
})

(define_expand "blt"
  [(set (pc)
	(if_then_else (lt (cc0) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  xtensa_expand_conditional_branch (operands, LT);
  DONE;
})

(define_expand "ble"
  [(set (pc)
	(if_then_else (le (cc0) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  xtensa_expand_conditional_branch (operands, LE);
  DONE;
})

(define_expand "bgtu"
  [(set (pc)
	(if_then_else (gtu (cc0) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  xtensa_expand_conditional_branch (operands, GTU);
  DONE;
})

(define_expand "bgeu"
  [(set (pc)
	(if_then_else (geu (cc0) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  xtensa_expand_conditional_branch (operands, GEU);
  DONE;
})

(define_expand "bltu"
  [(set (pc)
	(if_then_else (ltu (cc0) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  xtensa_expand_conditional_branch (operands, LTU);
  DONE;
})

(define_expand "bleu"
  [(set (pc)
	(if_then_else (leu (cc0) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  xtensa_expand_conditional_branch (operands, LEU);
  DONE;
})

;; Branch patterns for standard integer comparisons

(define_insn "*btrue"
  [(set (pc)
	(if_then_else (match_operator 3 "branch_operator"
			 [(match_operand:SI 0 "register_operand" "r,r")
			  (match_operand:SI 1 "branch_operand" "K,r")])
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  ""
{
  if (which_alternative == 1)
    {
      switch (GET_CODE (operands[3]))
	{
	case EQ:	return "beq\t%0, %1, %2";
	case NE:	return "bne\t%0, %1, %2";
	case LT:	return "blt\t%0, %1, %2";
	case GE:	return "bge\t%0, %1, %2";
	default:	gcc_unreachable ();
	}
    }
  else if (INTVAL (operands[1]) == 0)
    {
      switch (GET_CODE (operands[3]))
	{
	case EQ:	return (TARGET_DENSITY
				? "beqz.n\t%0, %2"
				: "beqz\t%0, %2");
	case NE:	return (TARGET_DENSITY
				? "bnez.n\t%0, %2"
				: "bnez\t%0, %2");
	case LT:	return "bltz\t%0, %2";
	case GE:	return "bgez\t%0, %2";
	default:	gcc_unreachable ();
	}
    }
  else
    {
      switch (GET_CODE (operands[3]))
	{
	case EQ:	return "beqi\t%0, %d1, %2";
	case NE:	return "bnei\t%0, %d1, %2";
	case LT:	return "blti\t%0, %d1, %2";
	case GE:	return "bgei\t%0, %d1, %2";
	default:	gcc_unreachable ();
	}
    }
  gcc_unreachable ();
}
  [(set_attr "type"	"jump,jump")
   (set_attr "mode"	"none")
   (set_attr "length"	"3,3")])

(define_insn "*bfalse"
  [(set (pc)
	(if_then_else (match_operator 3 "branch_operator"
			 [(match_operand:SI 0 "register_operand" "r,r")
			  (match_operand:SI 1 "branch_operand" "K,r")])
		      (pc)
		      (label_ref (match_operand 2 "" ""))))]
  ""
{
  if (which_alternative == 1)
    {
      switch (GET_CODE (operands[3]))
	{
	case EQ:	return "bne\t%0, %1, %2";
	case NE:	return "beq\t%0, %1, %2";
	case LT:	return "bge\t%0, %1, %2";
	case GE:	return "blt\t%0, %1, %2";
	default:	gcc_unreachable ();
	}
    }
  else if (INTVAL (operands[1]) == 0)
    {
      switch (GET_CODE (operands[3]))
	{
	case EQ:	return (TARGET_DENSITY
				? "bnez.n\t%0, %2"
				: "bnez\t%0, %2");
	case NE:	return (TARGET_DENSITY
				? "beqz.n\t%0, %2"
				: "beqz\t%0, %2");
	case LT:	return "bgez\t%0, %2";
	case GE:	return "bltz\t%0, %2";
	default:	gcc_unreachable ();
	}
    }
  else
    {
      switch (GET_CODE (operands[3]))
	{
	case EQ:	return "bnei\t%0, %d1, %2";
	case NE:	return "beqi\t%0, %d1, %2";
	case LT:	return "bgei\t%0, %d1, %2";
	case GE:	return "blti\t%0, %d1, %2";
	default:	gcc_unreachable ();
	}
    }
  gcc_unreachable ();
}
  [(set_attr "type"	"jump,jump")
   (set_attr "mode"	"none")
   (set_attr "length"	"3,3")])

(define_insn "*ubtrue"
  [(set (pc)
	(if_then_else (match_operator 3 "ubranch_operator"
			 [(match_operand:SI 0 "register_operand" "r,r")
			  (match_operand:SI 1 "ubranch_operand" "L,r")])
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  ""
{
  if (which_alternative == 1)
    {
      switch (GET_CODE (operands[3]))
	{
	case LTU:	return "bltu\t%0, %1, %2";
	case GEU:	return "bgeu\t%0, %1, %2";
	default:	gcc_unreachable ();
	}
    }
  else
    {
      switch (GET_CODE (operands[3]))
	{
	case LTU:	return "bltui\t%0, %d1, %2";
	case GEU:	return "bgeui\t%0, %d1, %2";
	default:	gcc_unreachable ();
	}
    }
  gcc_unreachable ();
}
  [(set_attr "type"	"jump,jump")
   (set_attr "mode"	"none")
   (set_attr "length"	"3,3")])

(define_insn "*ubfalse"
  [(set (pc)
	(if_then_else (match_operator 3 "ubranch_operator"
			 [(match_operand:SI 0 "register_operand" "r,r")
			  (match_operand:SI 1 "ubranch_operand" "L,r")])
		      (pc)
		      (label_ref (match_operand 2 "" ""))))]
  ""
{
  if (which_alternative == 1)
    {
      switch (GET_CODE (operands[3]))
	{
	case LTU:	return "bgeu\t%0, %1, %2";
	case GEU:	return "bltu\t%0, %1, %2";
	default:	gcc_unreachable ();
	}
    }
  else
    {
      switch (GET_CODE (operands[3]))
	{
	case LTU:	return "bgeui\t%0, %d1, %2";
	case GEU:	return "bltui\t%0, %d1, %2";
	default:	gcc_unreachable ();
	}
    }
  gcc_unreachable ();
}
  [(set_attr "type"	"jump,jump")
   (set_attr "mode"	"none")
   (set_attr "length"	"3,3")])

;; Branch patterns for bit testing

(define_insn "*bittrue"
  [(set (pc)
	(if_then_else (match_operator 3 "boolean_operator"
			[(zero_extract:SI
			    (match_operand:SI 0 "register_operand" "r,r")
			    (const_int 1)
			    (match_operand:SI 1 "arith_operand" "J,r"))
			 (const_int 0)])
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  ""
{
  if (which_alternative == 0)
    {
      unsigned bitnum = INTVAL(operands[1]) & 0x1f;
      operands[1] = GEN_INT(bitnum);
      switch (GET_CODE (operands[3]))
	{
	case EQ:	return "bbci\t%0, %d1, %2";
	case NE:	return "bbsi\t%0, %d1, %2";
	default:	gcc_unreachable ();
	}
    }
  else
    {
      switch (GET_CODE (operands[3]))
	{
	case EQ:	return "bbc\t%0, %1, %2";
	case NE:	return "bbs\t%0, %1, %2";
	default:	gcc_unreachable ();
	}
    }
  gcc_unreachable ();
}
  [(set_attr "type"	"jump")
   (set_attr "mode"	"none")
   (set_attr "length"	"3")])

(define_insn "*bitfalse"
  [(set (pc)
	(if_then_else (match_operator 3 "boolean_operator"
			[(zero_extract:SI
			    (match_operand:SI 0 "register_operand" "r,r")
			    (const_int 1)
			    (match_operand:SI 1 "arith_operand" "J,r"))
			 (const_int 0)])
		      (pc)
		      (label_ref (match_operand 2 "" ""))))]
  ""
{
  if (which_alternative == 0)
    {
      unsigned bitnum = INTVAL (operands[1]) & 0x1f;
      operands[1] = GEN_INT (bitnum);
      switch (GET_CODE (operands[3]))
	{
	case EQ:	return "bbsi\t%0, %d1, %2";
	case NE:	return "bbci\t%0, %d1, %2";
	default:	gcc_unreachable ();
	}
    }
  else
    {
      switch (GET_CODE (operands[3]))
	{
	case EQ:	return "bbs\t%0, %1, %2";
	case NE:	return "bbc\t%0, %1, %2";
	default:	gcc_unreachable ();
	}
    }
  gcc_unreachable ();
}
  [(set_attr "type"	"jump")
   (set_attr "mode"	"none")
   (set_attr "length"	"3")])

(define_insn "*masktrue"
  [(set (pc)
	(if_then_else (match_operator 3 "boolean_operator"
		 [(and:SI (match_operand:SI 0 "register_operand" "r")
			  (match_operand:SI 1 "register_operand" "r"))
		  (const_int 0)])
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  ""
{
  switch (GET_CODE (operands[3]))
    {
    case EQ:		return "bnone\t%0, %1, %2";
    case NE:		return "bany\t%0, %1, %2";
    default:		gcc_unreachable ();
    }
}
  [(set_attr "type"	"jump")
   (set_attr "mode"	"none")
   (set_attr "length"	"3")])

(define_insn "*maskfalse"
  [(set (pc)
	(if_then_else (match_operator 3 "boolean_operator"
		 [(and:SI (match_operand:SI 0 "register_operand" "r")
			  (match_operand:SI 1 "register_operand" "r"))
		  (const_int 0)])
		      (pc)
		      (label_ref (match_operand 2 "" ""))))]
  ""
{
  switch (GET_CODE (operands[3]))
    {
    case EQ:		return "bany\t%0, %1, %2";
    case NE:		return "bnone\t%0, %1, %2";
    default:		gcc_unreachable ();
    }
}
  [(set_attr "type"	"jump")
   (set_attr "mode"	"none")
   (set_attr "length"	"3")])


;; Define the loop insns used by bct optimization to represent the
;; start and end of a zero-overhead loop (in loop.c).  This start
;; template generates the loop insn; the end template doesn't generate
;; any instructions since loop end is handled in hardware.

(define_insn "zero_cost_loop_start"
  [(set (pc)
	(if_then_else (eq (match_operand:SI 0 "register_operand" "a")
			  (const_int 0))
		      (label_ref (match_operand 1 "" ""))
		      (pc)))
   (set (reg:SI 19)
	(plus:SI (match_dup 0) (const_int -1)))]
  ""
  "loopnez\t%0, %l1"
  [(set_attr "type"	"jump")
   (set_attr "mode"	"none")
   (set_attr "length"	"3")])

(define_insn "zero_cost_loop_end"
  [(set (pc)
	(if_then_else (ne (reg:SI 19) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))
   (set (reg:SI 19)
	(plus:SI (reg:SI 19) (const_int -1)))]
  ""
{
    xtensa_emit_loop_end (insn, operands);
    return "";
}
  [(set_attr "type"	"jump")
   (set_attr "mode"	"none")
   (set_attr "length"	"0")])


;; Setting a register from a comparison.

(define_expand "seq"
  [(set (match_operand:SI 0 "register_operand" "")
	(match_dup 1))]
  ""
{
  operands[1] = gen_rtx_EQ (SImode, branch_cmp[0], branch_cmp[1]);
  if (!xtensa_expand_scc (operands))
    FAIL;
  DONE;
})

(define_expand "sne"
  [(set (match_operand:SI 0 "register_operand" "")
	(match_dup 1))]
  ""
{
  operands[1] = gen_rtx_NE (SImode, branch_cmp[0], branch_cmp[1]);
  if (!xtensa_expand_scc (operands))
    FAIL;
  DONE;
})

(define_expand "sgt"
  [(set (match_operand:SI 0 "register_operand" "")
	(match_dup 1))]
  ""
{
  operands[1] = gen_rtx_GT (SImode, branch_cmp[0], branch_cmp[1]);
  if (!xtensa_expand_scc (operands))
    FAIL;
  DONE;
})

(define_expand "sge"
  [(set (match_operand:SI 0 "register_operand" "")
	(match_dup 1))]
  ""
{
  operands[1] = gen_rtx_GE (SImode, branch_cmp[0], branch_cmp[1]);
  if (!xtensa_expand_scc (operands))
    FAIL;
  DONE;
})

(define_expand "slt"
  [(set (match_operand:SI 0 "register_operand" "")
	(match_dup 1))]
  ""
{
  operands[1] = gen_rtx_LT (SImode, branch_cmp[0], branch_cmp[1]);
  if (!xtensa_expand_scc (operands))
    FAIL;
  DONE;
})

(define_expand "sle"
  [(set (match_operand:SI 0 "register_operand" "")
	(match_dup 1))]
  ""
{
  operands[1] = gen_rtx_LE (SImode, branch_cmp[0], branch_cmp[1]);
  if (!xtensa_expand_scc (operands))
    FAIL;
  DONE;
})


;; Conditional moves.

(define_expand "movsicc"
  [(set (match_operand:SI 0 "register_operand" "")
	(if_then_else:SI (match_operand 1 "comparison_operator" "")
			 (match_operand:SI 2 "register_operand" "")
			 (match_operand:SI 3 "register_operand" "")))]
  ""
{
  if (!xtensa_expand_conditional_move (operands, 0))
    FAIL;
  DONE;
})

(define_expand "movsfcc"
  [(set (match_operand:SF 0 "register_operand" "")
	(if_then_else:SF (match_operand 1 "comparison_operator" "")
			 (match_operand:SF 2 "register_operand" "")
			 (match_operand:SF 3 "register_operand" "")))]
  ""
{
  if (!xtensa_expand_conditional_move (operands, 1))
    FAIL;
  DONE;
})

(define_insn "movsicc_internal0"
  [(set (match_operand:SI 0 "register_operand" "=a,a")
	(if_then_else:SI (match_operator 4 "branch_operator"
			   [(match_operand:SI 1 "register_operand" "r,r")
			    (const_int 0)])
			 (match_operand:SI 2 "register_operand" "r,0")
			 (match_operand:SI 3 "register_operand" "0,r")))]
  ""
{
  if (which_alternative == 0)
    {
      switch (GET_CODE (operands[4]))
	{
	case EQ:	return "moveqz\t%0, %2, %1";
	case NE:	return "movnez\t%0, %2, %1";
	case LT:	return "movltz\t%0, %2, %1";
	case GE:	return "movgez\t%0, %2, %1";
	default:	gcc_unreachable ();
	}
    }
  else
    {
      switch (GET_CODE (operands[4]))
	{
	case EQ:	return "movnez\t%0, %3, %1";
	case NE:	return "moveqz\t%0, %3, %1";
	case LT:	return "movgez\t%0, %3, %1";
	case GE:	return "movltz\t%0, %3, %1";
	default:	gcc_unreachable ();
	}
    }
  gcc_unreachable ();
}
  [(set_attr "type"	"move,move")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3,3")])

(define_insn "movsicc_internal1"
  [(set (match_operand:SI 0 "register_operand" "=a,a")
	(if_then_else:SI (match_operator 4 "boolean_operator"
			   [(match_operand:CC 1 "register_operand" "b,b")
			    (const_int 0)])
			 (match_operand:SI 2 "register_operand" "r,0")
			 (match_operand:SI 3 "register_operand" "0,r")))]
  "TARGET_BOOLEANS"
{
  int isEq = (GET_CODE (operands[4]) == EQ);
  switch (which_alternative)
    {
    case 0:
      if (isEq) return "movf\t%0, %2, %1";
      return "movt\t%0, %2, %1";
    case 1:
      if (isEq) return "movt\t%0, %3, %1";
      return "movf\t%0, %3, %1";
    default:
      gcc_unreachable ();
    }
}
  [(set_attr "type"	"move,move")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3,3")])

(define_insn "movsfcc_internal0"
  [(set (match_operand:SF 0 "register_operand" "=a,a,f,f")
	(if_then_else:SF (match_operator 4 "branch_operator"
			   [(match_operand:SI 1 "register_operand" "r,r,r,r")
			    (const_int 0)])
			 (match_operand:SF 2 "register_operand" "r,0,f,0")
			 (match_operand:SF 3 "register_operand" "0,r,0,f")))]
  ""
{
  switch (which_alternative)
    {
    case 0:
      switch (GET_CODE (operands[4]))
	{
	case EQ:	return "moveqz\t%0, %2, %1";
	case NE:	return "movnez\t%0, %2, %1";
	case LT:	return "movltz\t%0, %2, %1";
	case GE:	return "movgez\t%0, %2, %1";
	default:	gcc_unreachable ();
	}
      break;
    case 1:
      switch (GET_CODE (operands[4]))
	{
	case EQ:	return "movnez\t%0, %3, %1";
	case NE:	return "moveqz\t%0, %3, %1";
	case LT:	return "movgez\t%0, %3, %1";
	case GE:	return "movltz\t%0, %3, %1";
	default:	gcc_unreachable ();
	}
      break;
    case 2:
      switch (GET_CODE (operands[4]))
	{
	case EQ:	return "moveqz.s %0, %2, %1";
	case NE:	return "movnez.s %0, %2, %1";
	case LT:	return "movltz.s %0, %2, %1";
	case GE:	return "movgez.s %0, %2, %1";
	default:	gcc_unreachable ();
	}
      break;
    case 3:
      switch (GET_CODE (operands[4]))
	{
	case EQ:	return "movnez.s %0, %3, %1";
	case NE:	return "moveqz.s %0, %3, %1";
	case LT:	return "movgez.s %0, %3, %1";
	case GE:	return "movltz.s %0, %3, %1";
	default:	gcc_unreachable ();
	}
      break;
    default:
      gcc_unreachable ();
    }
  gcc_unreachable ();
}
  [(set_attr "type"	"move,move,move,move")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3,3,3,3")])

(define_insn "movsfcc_internal1"
  [(set (match_operand:SF 0 "register_operand" "=a,a,f,f")
	(if_then_else:SF (match_operator 4 "boolean_operator"
			   [(match_operand:CC 1 "register_operand" "b,b,b,b")
			    (const_int 0)])
			 (match_operand:SF 2 "register_operand" "r,0,f,0")
			 (match_operand:SF 3 "register_operand" "0,r,0,f")))]
  "TARGET_BOOLEANS"
{
  int isEq = (GET_CODE (operands[4]) == EQ);
  switch (which_alternative)
    {
    case 0:
      if (isEq) return "movf\t%0, %2, %1";
      return "movt\t%0, %2, %1";
    case 1:
      if (isEq) return "movt\t%0, %3, %1";
      return "movf\t%0, %3, %1";
    case 2:
      if (isEq) return "movf.s\t%0, %2, %1";
      return "movt.s\t%0, %2, %1";
    case 3:
      if (isEq) return "movt.s\t%0, %3, %1";
      return "movf.s\t%0, %3, %1";
    default:
      gcc_unreachable ();
    }
}
  [(set_attr "type"	"move,move,move,move")
   (set_attr "mode"	"SF")
   (set_attr "length"	"3,3,3,3")])


;; Floating-point comparisons.

(define_insn "seq_sf"
  [(set (match_operand:CC 0 "register_operand" "=b")
	(eq:CC (match_operand:SF 1 "register_operand" "f")
	       (match_operand:SF 2 "register_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "oeq.s\t%0, %1, %2"
  [(set_attr "type"	"farith")
   (set_attr "mode"	"BL")
   (set_attr "length"	"3")])

(define_insn "slt_sf"
  [(set (match_operand:CC 0 "register_operand" "=b")
	(lt:CC (match_operand:SF 1 "register_operand" "f")
	       (match_operand:SF 2 "register_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "olt.s\t%0, %1, %2"
  [(set_attr "type"	"farith")
   (set_attr "mode"	"BL")
   (set_attr "length"	"3")])

(define_insn "sle_sf"
  [(set (match_operand:CC 0 "register_operand" "=b")
	(le:CC (match_operand:SF 1 "register_operand" "f")
	       (match_operand:SF 2 "register_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "ole.s\t%0, %1, %2"
  [(set_attr "type"	"farith")
   (set_attr "mode"	"BL")
   (set_attr "length"	"3")])


;; Unconditional branches.

(define_insn "jump"
  [(set (pc)
	(label_ref (match_operand 0 "" "")))]
  ""
  "j\t%l0"
  [(set_attr "type"	"jump")
   (set_attr "mode"	"none")
   (set_attr "length"	"3")])

(define_expand "indirect_jump"
  [(set (pc)
	(match_operand 0 "register_operand" ""))]
  ""
{
  rtx dest = operands[0];
  if (GET_CODE (dest) != REG || GET_MODE (dest) != Pmode)
    operands[0] = copy_to_mode_reg (Pmode, dest);

  emit_jump_insn (gen_indirect_jump_internal (dest));
  DONE;
})

(define_insn "indirect_jump_internal"
  [(set (pc) (match_operand:SI 0 "register_operand" "r"))]
  ""
  "jx\t%0"
  [(set_attr "type"	"jump")
   (set_attr "mode"	"none")
   (set_attr "length"	"3")])


(define_expand "tablejump"
  [(use (match_operand:SI 0 "register_operand" ""))
   (use (label_ref (match_operand 1 "" "")))]
   ""
{
  rtx target = operands[0];
  if (flag_pic)
    {
      /* For PIC, the table entry is relative to the start of the table.  */
      rtx label = gen_reg_rtx (SImode);
      target = gen_reg_rtx (SImode);
      emit_move_insn (label, gen_rtx_LABEL_REF (SImode, operands[1]));
      emit_insn (gen_addsi3 (target, operands[0], label));
    }
  emit_jump_insn (gen_tablejump_internal (target, operands[1]));
  DONE;
})

(define_insn "tablejump_internal"
  [(set (pc)
	(match_operand:SI 0 "register_operand" "r"))
   (use (label_ref (match_operand 1 "" "")))]
  ""
  "jx\t%0"
  [(set_attr "type"	"jump")
   (set_attr "mode"	"none")
   (set_attr "length"	"3")])


;; Function calls.

(define_expand "sym_PLT"
  [(const (unspec [(match_operand:SI 0 "" "")] UNSPEC_PLT))]
  ""
  "")

(define_expand "call"
  [(call (match_operand 0 "memory_operand" "")
	 (match_operand 1 "" ""))]
  ""
{
  rtx addr = XEXP (operands[0], 0);
  if (flag_pic && GET_CODE (addr) == SYMBOL_REF
      && (!SYMBOL_REF_LOCAL_P (addr) || SYMBOL_REF_EXTERNAL_P (addr)))
    addr = gen_sym_PLT (addr);
  if (!call_insn_operand (addr, VOIDmode))
    XEXP (operands[0], 0) = copy_to_mode_reg (Pmode, addr);
})

(define_insn "call_internal"
  [(call (mem (match_operand:SI 0 "call_insn_operand" "n,i,r"))
	 (match_operand 1 "" "i,i,i"))]
  ""
{
  return xtensa_emit_call (0, operands);
}
  [(set_attr "type"	"call")
   (set_attr "mode"	"none")
   (set_attr "length"	"3")])

(define_expand "call_value"
  [(set (match_operand 0 "register_operand" "")
	(call (match_operand 1 "memory_operand" "")
	      (match_operand 2 "" "")))]
  ""
{
  rtx addr = XEXP (operands[1], 0);
  if (flag_pic && GET_CODE (addr) == SYMBOL_REF
      && (!SYMBOL_REF_LOCAL_P (addr) || SYMBOL_REF_EXTERNAL_P (addr)))
    addr = gen_sym_PLT (addr);
  if (!call_insn_operand (addr, VOIDmode))
    XEXP (operands[1], 0) = copy_to_mode_reg (Pmode, addr);
})

;; Cannot combine constraints for operand 0 into "afvb":
;; reload.c:find_reloads seems to assume that grouped constraints somehow
;; specify related register classes, and when they don't the constraints
;; fail to match.  By not grouping the constraints, we get the correct
;; behavior.
(define_insn "call_value_internal"
   [(set (match_operand 0 "register_operand" "=af,af,af,v,v,v,b,b,b")
         (call (mem (match_operand:SI 1 "call_insn_operand"
					"n,i,r,n,i,r,n,i,r"))
               (match_operand 2 "" "i,i,i,i,i,i,i,i,i")))]
  ""
{
  return xtensa_emit_call (1, operands);
}
  [(set_attr "type"	"call")
   (set_attr "mode"	"none")
   (set_attr "length"	"3")])

(define_insn "entry"
  [(set (reg:SI A1_REG)
	(unspec_volatile:SI [(match_operand:SI 0 "const_int_operand" "i")
			     (match_operand:SI 1 "const_int_operand" "i")]
			    UNSPECV_ENTRY))]
  ""
{
  if (frame_pointer_needed)
    output_asm_insn (".frame\ta7, %0", operands);
  else
    output_asm_insn (".frame\tsp, %0", operands);
  return "entry\tsp, %1";
}
  [(set_attr "type"	"move")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

(define_insn "return"
  [(return)
   (use (reg:SI A0_REG))]
  "reload_completed"
{
  return (TARGET_DENSITY ? "retw.n" : "retw");
}
  [(set_attr "type"	"jump")
   (set_attr "mode"	"none")
   (set_attr "length"	"2")])


;; Miscellaneous instructions.

(define_expand "prologue"
  [(const_int 0)]
  ""
{
  xtensa_expand_prologue ();
  DONE;
})

(define_expand "epilogue"
  [(return)]
  ""
{
  emit_jump_insn (gen_return ());
  DONE;
})

(define_insn "nop"
  [(const_int 0)]
  ""
{
  return (TARGET_DENSITY ? "nop.n" : "nop");
}
  [(set_attr "type"	"nop")
   (set_attr "mode"	"none")
   (set_attr "length"	"3")])

(define_expand "nonlocal_goto"
  [(match_operand:SI 0 "general_operand" "")
   (match_operand:SI 1 "general_operand" "")
   (match_operand:SI 2 "general_operand" "")
   (match_operand:SI 3 "" "")]
  ""
{
  xtensa_expand_nonlocal_goto (operands);
  DONE;
})

;; Setting up a frame pointer is tricky for Xtensa because GCC doesn't
;; know if a frame pointer is required until the reload pass, and
;; because there may be an incoming argument value in the hard frame
;; pointer register (a7).  If there is an incoming argument in that
;; register, the "set_frame_ptr" insn gets inserted immediately after
;; the insn that copies the incoming argument to a pseudo or to the
;; stack.  This serves several purposes here: (1) it keeps the
;; optimizer from copy-propagating or scheduling the use of a7 as an
;; incoming argument away from the beginning of the function; (2) we
;; can use a post-reload splitter to expand away the insn if a frame
;; pointer is not required, so that the post-reload scheduler can do
;; the right thing; and (3) it makes it easy for the prologue expander
;; to search for this insn to determine whether it should add a new insn
;; to set up the frame pointer.

(define_insn "set_frame_ptr"
  [(set (reg:SI A7_REG) (unspec_volatile:SI [(const_int 0)] UNSPECV_SET_FP))]
  ""
{
  if (frame_pointer_needed)
    return "mov\ta7, sp";
  return "";
}
  [(set_attr "type"	"move")
   (set_attr "mode"	"SI")
   (set_attr "length"	"3")])

;; Post-reload splitter to remove fp assignment when it's not needed.
(define_split
  [(set (reg:SI A7_REG) (unspec_volatile:SI [(const_int 0)] UNSPECV_SET_FP))]
  "reload_completed && !frame_pointer_needed"
  [(unspec [(const_int 0)] UNSPEC_NOP)]
  "")

;; The preceding splitter needs something to split the insn into;
;; things start breaking if the result is just a "use" so instead we
;; generate the following insn.
(define_insn "*unspec_nop"
  [(unspec [(const_int 0)] UNSPEC_NOP)]
  ""
  ""
  [(set_attr "type"	"nop")
   (set_attr "mode"	"none")
   (set_attr "length"	"0")])

;; The fix_return_addr pattern sets the high 2 bits of an address in a
;; register to match the high bits of the current PC.
(define_insn "fix_return_addr"
  [(set (match_operand:SI 0 "register_operand" "=a")
	(unspec:SI [(match_operand:SI 1 "register_operand" "r")]
		   UNSPEC_RET_ADDR))
   (clobber (match_scratch:SI 2 "=r"))
   (clobber (match_scratch:SI 3 "=r"))]
  ""
  "mov\t%2, a0\;call0\t0f\;.align\t4\;0:\;mov\t%3, a0\;mov\ta0, %2\;\
srli\t%3, %3, 30\;slli\t%0, %1, 2\;ssai\t2\;src\t%0, %3, %0"
  [(set_attr "type"	"multi")
   (set_attr "mode"	"SI")
   (set_attr "length"	"24")])


;; Instructions for the Xtensa "boolean" option.

(define_insn "*booltrue"
  [(set (pc)
	(if_then_else (match_operator 2 "boolean_operator"
			 [(match_operand:CC 0 "register_operand" "b")
			  (const_int 0)])
		      (label_ref (match_operand 1 "" ""))
		      (pc)))]
  "TARGET_BOOLEANS"
{
  if (GET_CODE (operands[2]) == EQ)
    return "bf\t%0, %1";
  else
    return "bt\t%0, %1";
}
  [(set_attr "type"	"jump")
   (set_attr "mode"	"none")
   (set_attr "length"	"3")])

(define_insn "*boolfalse"
  [(set (pc)
	(if_then_else (match_operator 2 "boolean_operator"
			 [(match_operand:CC 0 "register_operand" "b")
			  (const_int 0)])
		      (pc)
		      (label_ref (match_operand 1 "" ""))))]
  "TARGET_BOOLEANS"
{
  if (GET_CODE (operands[2]) == EQ)
    return "bt\t%0, %1";
  else
    return "bf\t%0, %1";
}
  [(set_attr "type"	"jump")
   (set_attr "mode"	"none")
   (set_attr "length"	"3")])
