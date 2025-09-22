;; GCC machine description for MMIX
;; Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005
;; Free Software Foundation, Inc.
;; Contributed by Hans-Peter Nilsson (hp@bitrange.com)

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

;; The original PO technology requires these to be ordered by speed,
;; so that assigner will pick the fastest.

;; See file "rtl.def" for documentation on define_insn, match_*, et al.

;; Uses of UNSPEC in this file:
;; UNSPEC_VOLATILE:
;;
;;	0	sync_icache (sync icache before trampoline jump)
;;	1	nonlocal_goto_receiver
;;

;; The order of insns is as in Node: Standard Names, with smaller modes
;; before bigger modes.

(define_constants
  [(MMIX_rJ_REGNUM 259)
   (MMIX_rR_REGNUM 260)
   (MMIX_fp_rO_OFFSET -24)]
)

;; Operand and operator predicates.

(include "predicates.md")

;; FIXME: Can we remove the reg-to-reg for smaller modes?  Shouldn't they
;; be synthesized ok?
(define_insn "movqi"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=r,r ,r,x ,r,r,m,??r")
	(match_operand:QI 1 "general_operand"	    "r,LS,K,rI,x,m,r,n"))]
  ""
  "@
   SET %0,%1
   %s1 %0,%v1
   NEGU %0,0,%n1
   PUT %0,%1
   GET %0,%1
   LDB%U0 %0,%1
   STBU %1,%0
   %r0%I1")

(define_insn "movhi"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=r,r ,r ,x,r,r,m,??r")
	(match_operand:HI 1 "general_operand"	    "r,LS,K,r,x,m,r,n"))]
  ""
  "@
   SET %0,%1
   %s1 %0,%v1
   NEGU %0,0,%n1
   PUT %0,%1
   GET %0,%1
   LDW%U0 %0,%1
   STWU %1,%0
   %r0%I1")

;; gcc.c-torture/compile/920428-2.c fails if there's no "n".
(define_insn "movsi"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=r,r ,r,x,r,r,m,??r")
	(match_operand:SI 1 "general_operand"	    "r,LS,K,r,x,m,r,n"))]
  ""
  "@
   SET %0,%1
   %s1 %0,%v1
   NEGU %0,0,%n1
   PUT %0,%1
   GET %0,%1
   LDT%U0 %0,%1
   STTU %1,%0
   %r0%I1")

;; We assume all "s" are addresses.  Does that hold?
(define_insn "movdi"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=r,r ,r,x,r,m,r,m,r,r,??r")
	(match_operand:DI 1 "general_operand"	    "r,LS,K,r,x,I,m,r,R,s,n"))]
  ""
  "@
   SET %0,%1
   %s1 %0,%v1
   NEGU %0,0,%n1
   PUT %0,%1
   GET %0,%1
   STCO %1,%0
   LDO %0,%1
   STOU %1,%0
   GETA %0,%1
   LDA %0,%1
   %r0%I1")

;; Note that we move around the float as a collection of bits; no
;; conversion to double.
(define_insn "movsf"
 [(set (match_operand:SF 0 "nonimmediate_operand" "=r,r,x,r,r,m,??r")
       (match_operand:SF 1 "general_operand"	   "r,G,r,x,m,r,F"))]
  ""
  "@
   SET %0,%1
   SETL %0,0
   PUT %0,%1
   GET %0,%1
   LDT %0,%1
   STTU %1,%0
   %r0%I1")

(define_insn "movdf"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=r,r,x,r,r,m,??r")
	(match_operand:DF 1 "general_operand"	    "r,G,r,x,m,r,F"))]
  ""
  "@
   SET %0,%1
   SETL %0,0
   PUT %0,%1
   GET %0,%1
   LDO %0,%1
   STOU %1,%0
   %r0%I1")

;; We need to be able to move around the values used as condition codes.
;; First spotted as reported in
;; <URL:http://gcc.gnu.org/ml/gcc-bugs/2003-03/msg00008.html> due to
;; changes in loop optimization.  The file machmode.def says they're of
;; size 4 QI.  Valid bit-patterns correspond to integers -1, 0 and 1, so
;; we treat them as signed entities; see mmix-modes.def.  The following
;; expanders should cover all MODE_CC modes, and expand for this pattern.
(define_insn "*movcc_expanded"
  [(set (match_operand 0 "nonimmediate_operand" "=r,x,r,r,m")
	(match_operand 1 "nonimmediate_operand"  "r,r,x,m,r"))]
  "GET_MODE_CLASS (GET_MODE (operands[0])) == MODE_CC
   && GET_MODE_CLASS (GET_MODE (operands[1])) == MODE_CC"
  "@
   SET %0,%1
   PUT %0,%1
   GET %0,%1
   LDT %0,%1
   STT %1,%0")

(define_expand "movcc"
  [(set (match_operand:CC 0 "nonimmediate_operand" "")
	(match_operand:CC 1 "nonimmediate_operand" ""))]
  ""
  "")

(define_expand "movcc_uns"
  [(set (match_operand:CC_UNS 0 "nonimmediate_operand" "")
	(match_operand:CC_UNS 1 "nonimmediate_operand" ""))]
  ""
  "")

(define_expand "movcc_fp"
  [(set (match_operand:CC_FP 0 "nonimmediate_operand" "")
	(match_operand:CC_FP 1 "nonimmediate_operand" ""))]
  ""
  "")

(define_expand "movcc_fpeq"
  [(set (match_operand:CC_FPEQ 0 "nonimmediate_operand" "")
	(match_operand:CC_FPEQ 1 "nonimmediate_operand" ""))]
  ""
  "")

(define_expand "movcc_fun"
  [(set (match_operand:CC_FUN 0 "nonimmediate_operand" "")
	(match_operand:CC_FUN 1 "nonimmediate_operand" ""))]
  ""
  "")

(define_insn "adddi3"
  [(set (match_operand:DI 0 "register_operand"	"=r,r,r")
	(plus:DI
	 (match_operand:DI 1 "register_operand" "%r,r,0")
	 (match_operand:DI 2 "mmix_reg_or_constant_operand" "rI,K,LS")))]
  ""
  "@
   ADDU %0,%1,%2
   SUBU %0,%1,%n2
   %i2 %0,%v2")

(define_insn "adddf3"
  [(set (match_operand:DF 0 "register_operand" "=r")
	(plus:DF (match_operand:DF 1 "register_operand" "%r")
		 (match_operand:DF 2 "register_operand" "r")))]
  ""
  "FADD %0,%1,%2")

;; Insn canonicalization *should* have removed the need for an integer
;; in operand 2.
(define_insn "subdi3"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(minus:DI (match_operand:DI 1 "mmix_reg_or_8bit_operand" "r,I")
		  (match_operand:DI 2 "register_operand" "r,r")))]
  ""
  "@
   SUBU %0,%1,%2
   NEGU %0,%1,%2")

(define_insn "subdf3"
  [(set (match_operand:DF 0 "register_operand" "=r")
	(minus:DF (match_operand:DF 1 "register_operand" "r")
		  (match_operand:DF 2 "register_operand" "r")))]
  ""
  "FSUB %0,%1,%2")

;; FIXME: Should we define_expand and match 2, 4, 8 (etc) with shift (or
;; %{something}2ADDU %0,%1,0)?  Hopefully GCC should still handle it, so
;; we don't have to taint the machine description.  If results are bad
;; enough, we may have to do it anyway.
(define_insn "muldi3"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(mult:DI (match_operand:DI 1 "register_operand" "%r,r")
		 (match_operand:DI 2 "mmix_reg_or_8bit_operand" "O,rI")))
   (clobber (match_scratch:DI 3 "=X,z"))]
  ""
  "@
   %m2ADDU %0,%1,%1
   MULU %0,%1,%2")

(define_insn "muldf3"
  [(set (match_operand:DF 0 "register_operand" "=r")
	(mult:DF (match_operand:DF 1 "register_operand" "r")
		 (match_operand:DF 2 "register_operand" "r")))]
  ""
  "FMUL %0,%1,%2")

(define_insn "divdf3"
  [(set (match_operand:DF 0 "register_operand" "=r")
	(div:DF (match_operand:DF 1 "register_operand" "r")
		(match_operand:DF 2 "register_operand" "r")))]
  ""
  "FDIV %0,%1,%2")

;; FIXME: Is "frem" doing the right operation for moddf3?
(define_insn "moddf3"
  [(set (match_operand:DF 0 "register_operand" "=r")
	(mod:DF (match_operand:DF 1 "register_operand" "r")
		(match_operand:DF 2 "register_operand" "r")))]
  ""
  "FREM %0,%1,%2")

;; FIXME: Should we define_expand for smin, smax, umin, umax using a
;; nifty conditional sequence?

;; FIXME: The cuter andn combinations don't get here, presumably because
;; they ended up in the constant pool.  Check: still?
(define_insn "anddi3"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(and:DI
	 (match_operand:DI 1 "register_operand" "%r,0")
	 (match_operand:DI 2 "mmix_reg_or_constant_operand" "rI,NT")))]
  ""
  "@
   AND %0,%1,%2
   %A2 %0,%V2")

(define_insn "iordi3"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(ior:DI (match_operand:DI 1 "register_operand" "%r,0")
		(match_operand:DI 2 "mmix_reg_or_constant_operand" "rH,LS")))]
  ""
  "@
   OR %0,%1,%2
   %o2 %0,%v2")

(define_insn "xordi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(xor:DI (match_operand:DI 1 "register_operand" "%r")
		(match_operand:DI 2 "mmix_reg_or_8bit_operand" "rI")))]
  ""
  "XOR %0,%1,%2")

;; FIXME:  When TImode works for other reasons (like cross-compiling from
;; a 32-bit host), add back umulditi3 and umuldi3_highpart here.

;; FIXME: Check what's really reasonable for the mod part.

;; One day we might persuade GCC to expand divisions with constants the
;; way MMIX does; giving the remainder the sign of the divisor.  But even
;; then, it might be good to have an option to divide the way "everybody
;; else" does.  Perhaps then, this option can be on by default.  However,
;; it's not likely to happen because major (C, C++, Fortran) language
;; standards in effect at 2002-04-29 reportedly demand that the sign of
;; the remainder must follow the sign of the dividend.

(define_insn "divmoddi4"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(div:DI (match_operand:DI 1 "register_operand" "r")
		(match_operand:DI 2 "mmix_reg_or_8bit_operand" "rI")))
   (set (match_operand:DI 3 "register_operand" "=y")
	(mod:DI (match_dup 1) (match_dup 2)))]
  ;; Do the library stuff later.
  "TARGET_KNUTH_DIVISION"
  "DIV %0,%1,%2")

(define_insn "udivmoddi4"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(udiv:DI (match_operand:DI 1 "register_operand" "r")
		 (match_operand:DI 2 "mmix_reg_or_8bit_operand" "rI")))
   (set (match_operand:DI 3 "register_operand" "=y")
	(umod:DI (match_dup 1) (match_dup 2)))]
  ""
  "DIVU %0,%1,%2")

(define_expand "divdi3"
  [(parallel
    [(set (match_operand:DI 0 "register_operand" "=&r")
	  (div:DI (match_operand:DI 1 "register_operand" "r")
		  (match_operand:DI 2 "register_operand" "r")))
     (clobber (scratch:DI))
     (clobber (scratch:DI))
     (clobber (reg:DI MMIX_rR_REGNUM))])]
  "! TARGET_KNUTH_DIVISION"
  "")

;; The %2-is-%1-case is there just to make sure things don't fail.  Could
;; presumably happen with optimizations off; no evidence.
(define_insn "*divdi3_nonknuth"
  [(set (match_operand:DI 0 "register_operand" "=&r,r")
	(div:DI (match_operand:DI 1 "register_operand" "r,r")
		(match_operand:DI 2 "register_operand" "1,r")))
   (clobber (match_scratch:DI 3 "=1,1"))
   (clobber (match_scratch:DI 4 "=2,2"))
   (clobber (reg:DI MMIX_rR_REGNUM))]
  "! TARGET_KNUTH_DIVISION"
  "@
   SETL %0,1
   XOR $255,%1,%2\;NEGU %0,0,%2\;CSN %2,%2,%0\;NEGU %0,0,%1\;CSN %1,%1,%0\;\
DIVU %0,%1,%2\;NEGU %1,0,%0\;CSN %0,$255,%1")

(define_expand "moddi3"
  [(parallel
    [(set (match_operand:DI 0 "register_operand" "=&r")
	  (mod:DI (match_operand:DI 1 "register_operand" "r")
		  (match_operand:DI 2 "register_operand" "r")))
     (clobber (scratch:DI))
     (clobber (scratch:DI))
     (clobber (reg:DI MMIX_rR_REGNUM))])]
  "! TARGET_KNUTH_DIVISION"
  "")

;; The %2-is-%1-case is there just to make sure things don't fail.  Could
;; presumably happen with optimizations off; no evidence.
(define_insn "*moddi3_nonknuth"
  [(set (match_operand:DI 0 "register_operand" "=&r,r")
	(mod:DI (match_operand:DI 1 "register_operand" "r,r")
		(match_operand:DI 2 "register_operand" "1,r")))
   (clobber (match_scratch:DI 3 "=1,1"))
   (clobber (match_scratch:DI 4 "=2,2"))
   (clobber (reg:DI MMIX_rR_REGNUM))]
  "! TARGET_KNUTH_DIVISION"
  "@
   SETL %0,0
   NEGU %0,0,%2\;CSN %2,%2,%0\;NEGU $255,0,%1\;CSN %1,%1,$255\;\
DIVU %1,%1,%2\;GET %0,:rR\;NEGU %2,0,%0\;CSNN %0,$255,%2")

(define_insn "ashldi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI
	 (match_operand:DI 1 "register_operand" "r")
	 (match_operand:DI 2 "mmix_reg_or_8bit_operand" "rI")))]
  ""
  "SLU %0,%1,%2")

(define_insn "ashrdi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashiftrt:DI
	 (match_operand:DI 1 "register_operand" "r")
	 (match_operand:DI 2 "mmix_reg_or_8bit_operand" "rI")))]
  ""
  "SR %0,%1,%2")

(define_insn "lshrdi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(lshiftrt:DI
	 (match_operand:DI 1 "register_operand" "r")
	 (match_operand:DI 2 "mmix_reg_or_8bit_operand" "rI")))]
  ""
  "SRU %0,%1,%2")

(define_insn "negdi2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(neg:DI (match_operand:DI 1 "register_operand" "r")))]
  ""
  "NEGU %0,0,%1")

(define_expand "negdf2"
  [(parallel [(set (match_operand:DF 0 "register_operand" "=r")
                   (neg:DF (match_operand:DF 1 "register_operand" "r")))
              (use (match_dup 2))])]
  ""
{
  /* Emit bit-flipping sequence to be IEEE-safe wrt. -+0.  */
  operands[2] = force_reg (DImode, GEN_INT ((HOST_WIDE_INT) 1 << 63));
})

(define_insn "*expanded_negdf2"
  [(set (match_operand:DF 0 "register_operand" "=r")
        (neg:DF (match_operand:DF 1 "register_operand" "r")))
   (use (match_operand:DI 2 "register_operand" "r"))]
  ""
  "XOR %0,%1,%2")

;; FIXME: define_expand for absdi2?

(define_insn "absdf2"
  [(set (match_operand:DF 0 "register_operand" "=r")
	(abs:DF (match_operand:DF 1 "register_operand" "0")))]
  ""
  "ANDNH %0,#8000")

(define_insn "sqrtdf2"
  [(set (match_operand:DF 0 "register_operand" "=r")
	(sqrt:DF (match_operand:DF 1 "register_operand" "r")))]
  ""
  "FSQRT %0,%1")

;; FIXME: define_expand for ffssi2? (not ffsdi2 since int is SImode).

(define_insn "one_cmpldi2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(not:DI (match_operand:DI 1 "register_operand" "r")))]
  ""
  "NOR %0,%1,0")

;; Since we don't have cc0, we do what is recommended in the manual;
;; store away the operands for use in the branch, scc or movcc insn.
(define_expand "cmpdi"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "mmix_reg_or_8bit_operand" "")]
  ""
  "
{
  mmix_compare_op0 = operands[0];
  mmix_compare_op1 = operands[1];
  DONE;
}")

(define_expand "cmpdf"
  [(match_operand:DF 0 "register_operand" "")
   (match_operand:DF 1 "register_operand" "")]
  ""
  "
{
  mmix_compare_op0 = operands[0];
  mmix_compare_op1 = operands[1];
  DONE;
}")

;; When the user-patterns expand, the resulting insns will match the
;; patterns below.

;; We can fold the signed-compare where the register value is
;; already equal to (compare:CCTYPE (reg) (const_int 0)).
;;  We can't do that at all for floating-point, due to NaN, +0.0
;; and -0.0, and we can only do it for the non/zero test of
;; unsigned, so that has to be done another way.
;;  FIXME: Perhaps a peep2 changing CCcode to a new code, that
;; gets folded here.
(define_insn "*cmpcc_folded"
  [(set (match_operand:CC 0 "register_operand" "=r")
	(compare:CC
	 (match_operand:DI 1 "register_operand" "r")
	 (const_int 0)))]
  ;; FIXME: Can we test equivalence any other way?
  ;; FIXME: Can we fold any other way?
  "REGNO (operands[1]) == REGNO (operands[0])"
  "%% folded: cmp %0,%1,0")

(define_insn "*cmpcc"
  [(set (match_operand:CC 0 "register_operand" "=r")
	(compare:CC
	 (match_operand:DI 1 "register_operand" "r")
	 (match_operand:DI 2 "mmix_reg_or_8bit_operand" "rI")))]
  ""
  "CMP %0,%1,%2")

(define_insn "*cmpu"
  [(set (match_operand:CC_UNS 0 "register_operand" "=r")
	(compare:CC_UNS
	 (match_operand:DI 1 "register_operand" "r")
	 (match_operand:DI 2 "mmix_reg_or_8bit_operand" "rI")))]
  ""
  "CMPU %0,%1,%2")

(define_insn "*fcmp"
  [(set (match_operand:CC_FP 0 "register_operand" "=r")
	(compare:CC_FP
	 (match_operand:DF 1 "register_operand" "r")
	 (match_operand:DF 2 "register_operand" "r")))]
  ""
  "FCMP%e0 %0,%1,%2")

;; FIXME: for -mieee, add fsub %0,%1,%1\;fsub %0,%2,%2 before to
;; make signalling compliant.
(define_insn "*feql"
  [(set (match_operand:CC_FPEQ 0 "register_operand" "=r")
	(compare:CC_FPEQ
	 (match_operand:DF 1 "register_operand" "r")
	 (match_operand:DF 2 "register_operand" "r")))]
  ""
  "FEQL%e0 %0,%1,%2")

(define_insn "*fun"
  [(set (match_operand:CC_FUN 0 "register_operand" "=r")
	(compare:CC_FUN
	 (match_operand:DF 1 "register_operand" "r")
	 (match_operand:DF 2 "register_operand" "r")))]
  ""
  "FUN%e0 %0,%1,%2")

;; In order to get correct rounding, we have to use SFLOT and SFLOTU for
;; conversion.  They do not convert to SFmode; they convert to DFmode,
;; with rounding as of SFmode.  They are not usable as is, but we pretend
;; we have a single instruction but emit two.

;; Note that this will (somewhat unexpectedly) create an inexact
;; exception if rounding is necessary - has to be masked off in crt0?
(define_expand "floatdisf2"
  [(parallel [(set (match_operand:SF 0 "nonimmediate_operand" "=rm")
		   (float:SF
		    (match_operand:DI 1 "mmix_reg_or_8bit_operand" "rI")))
	      ;; Let's use a DI scratch, since SF don't generally get into
	      ;; registers.  Dunno what's best; it's really a DF, but that
	      ;; doesn't logically follow from operands in the pattern.
	      (clobber (match_scratch:DI 2 "=&r"))])]
  ""
  "
{
  if (GET_CODE (operands[0]) != MEM)
    {
      rtx stack_slot;

      /* FIXME: This stack-slot remains even at -O3.  There must be a
	 better way.  */
      stack_slot
	= validize_mem (assign_stack_temp (SFmode,
					   GET_MODE_SIZE (SFmode), 0));
      emit_insn (gen_floatdisf2 (stack_slot, operands[1]));
      emit_move_insn (operands[0], stack_slot);
      DONE;
    }
}")

(define_insn "*floatdisf2_real"
  [(set (match_operand:SF 0 "memory_operand" "=m")
	(float:SF
	 (match_operand:DI 1 "mmix_reg_or_8bit_operand" "rI")))
   (clobber (match_scratch:DI 2 "=&r"))]
  ""
  "SFLOT %2,%1\;STSF %2,%0")

(define_expand "floatunsdisf2"
  [(parallel [(set (match_operand:SF 0 "nonimmediate_operand" "=rm")
		   (unsigned_float:SF
		    (match_operand:DI 1 "mmix_reg_or_8bit_operand" "rI")))
	      ;; Let's use a DI scratch, since SF don't generally get into
	      ;; registers.  Dunno what's best; it's really a DF, but that
	      ;; doesn't logically follow from operands in the pattern.
	      (clobber (scratch:DI))])]
  ""
  "
{
  if (GET_CODE (operands[0]) != MEM)
    {
      rtx stack_slot;

      /* FIXME: This stack-slot remains even at -O3.  Must be a better
	 way.  */
      stack_slot
	= validize_mem (assign_stack_temp (SFmode,
					   GET_MODE_SIZE (SFmode), 0));
      emit_insn (gen_floatunsdisf2 (stack_slot, operands[1]));
      emit_move_insn (operands[0], stack_slot);
      DONE;
    }
}")

(define_insn "*floatunsdisf2_real"
  [(set (match_operand:SF 0 "memory_operand" "=m")
	(unsigned_float:SF
	 (match_operand:DI 1 "mmix_reg_or_8bit_operand" "rI")))
   (clobber (match_scratch:DI 2 "=&r"))]
  ""
  "SFLOTU %2,%1\;STSF %2,%0")

;; Note that this will (somewhat unexpectedly) create an inexact
;; exception if rounding is necessary - has to be masked off in crt0?
(define_insn "floatdidf2"
  [(set (match_operand:DF 0 "register_operand" "=r")
	(float:DF
	 (match_operand:DI 1 "mmix_reg_or_8bit_operand" "rI")))]
  ""
  "FLOT %0,%1")

(define_insn "floatunsdidf2"
  [(set (match_operand:DF 0 "register_operand" "=r")
	(unsigned_float:DF
	 (match_operand:DI 1 "mmix_reg_or_8bit_operand" "rI")))]
  ""
  "FLOTU %0,%1")

(define_insn "ftruncdf2"
  [(set (match_operand:DF 0 "register_operand" "=r")
	(fix:DF (match_operand:DF 1 "register_operand" "r")))]
  ""
  ;; ROUND_OFF
  "FINT %0,1,%1")

;; Note that this will (somewhat unexpectedly) create an inexact
;; exception if rounding is necessary - has to be masked off in crt0?
(define_insn "fix_truncdfdi2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(fix:DI (fix:DF (match_operand:DF 1 "register_operand" "r"))))]
  ""
  ;; ROUND_OFF
  "FIX %0,1,%1")

(define_insn "fixuns_truncdfdi2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unsigned_fix:DI
	 (fix:DF (match_operand:DF 1 "register_operand" "r"))))]
  ""
  ;; ROUND_OFF
  "FIXU %0,1,%1")

;; It doesn't seem like it's possible to have memory_operand as a
;; predicate here (testcase: libgcc2 floathisf).  FIXME:  Shouldn't it be
;; possible to do that?  Bug in GCC?  Anyway, this used to be a simple
;; pattern with a memory_operand predicate, but was split up with a
;; define_expand with the old pattern as "anonymous".
;; FIXME: Perhaps with SECONDARY_MEMORY_NEEDED?
(define_expand "truncdfsf2"
  [(set (match_operand:SF 0 "memory_operand" "")
	(float_truncate:SF (match_operand:DF 1 "register_operand" "")))]
  ""
  "
{
  if (GET_CODE (operands[0]) != MEM)
    {
      /* FIXME: There should be a way to say: 'put this in operands[0]
	 but *after* the expanded insn'.  */
      rtx stack_slot;

      /* There is no sane destination but a register here, if it wasn't
	 already MEM.  (It's too hard to get fatal_insn to work here.)  */
      if (! REG_P (operands[0]))
	internal_error (\"MMIX Internal: Bad truncdfsf2 expansion\");

      /* FIXME: This stack-slot remains even at -O3.  Must be a better
	 way.  */
      stack_slot
	= validize_mem (assign_stack_temp (SFmode,
					   GET_MODE_SIZE (SFmode), 0));
      emit_insn (gen_truncdfsf2 (stack_slot, operands[1]));
      emit_move_insn (operands[0], stack_slot);
      DONE;
    }
}")

(define_insn "*truncdfsf2_real"
  [(set (match_operand:SF 0 "memory_operand" "=m")
	(float_truncate:SF (match_operand:DF 1 "register_operand" "r")))]
  ""
  "STSF %1,%0")

;; Same comment as for truncdfsf2.
(define_expand "extendsfdf2"
  [(set (match_operand:DF 0 "register_operand" "=r")
	(float_extend:DF (match_operand:SF 1 "memory_operand" "m")))]
  ""
  "
{
  if (GET_CODE (operands[1]) != MEM)
    {
      rtx stack_slot;

      /* There is no sane destination but a register here, if it wasn't
	 already MEM.  (It's too hard to get fatal_insn to work here.)  */
      if (! REG_P (operands[0]))
	internal_error (\"MMIX Internal: Bad extendsfdf2 expansion\");

      /* FIXME: This stack-slot remains even at -O3.  There must be a
	 better way.  */
      stack_slot
	= validize_mem (assign_stack_temp (SFmode,
					   GET_MODE_SIZE (SFmode), 0));
      emit_move_insn (stack_slot, operands[1]);
      emit_insn (gen_extendsfdf2 (operands[0], stack_slot));
      DONE;
    }
}")

(define_insn "*extendsfdf2_real"
  [(set (match_operand:DF 0 "register_operand" "=r")
	(float_extend:DF (match_operand:SF 1 "memory_operand" "m")))]
  ""
  "LDSF %0,%1")

;; Neither sign-extend nor zero-extend are necessary; gcc knows how to
;; synthesize using shifts or and, except with a memory source and not
;; completely optimal.  FIXME: Actually, other bugs surface when those
;; patterns are defined; fix later.

;; There are no sane values with the bit-patterns of (int) 0..255 except
;; 0 to use in movdfcc.

(define_expand "movdfcc"
  [(set (match_operand:DF 0 "register_operand" "")
	(if_then_else:DF
	 (match_operand 1 "comparison_operator" "")
	 (match_operand:DF 2 "mmix_reg_or_0_operand" "")
	 (match_operand:DF 3 "mmix_reg_or_0_operand" "")))]
  ""
  "
{
  enum rtx_code code = GET_CODE (operands[1]);
  rtx cc_reg = mmix_gen_compare_reg (code, mmix_compare_op0,
				     mmix_compare_op1);
  if (cc_reg == NULL_RTX)
    FAIL;
  operands[1] = gen_rtx_fmt_ee (code, VOIDmode, cc_reg, const0_rtx);
}")

(define_expand "movdicc"
  [(set (match_operand:DI 0 "register_operand" "")
	(if_then_else:DI
	 (match_operand 1 "comparison_operator" "")
	 (match_operand:DI 2 "mmix_reg_or_8bit_operand" "")
	 (match_operand:DI 3 "mmix_reg_or_8bit_operand" "")))]
  ""
  "
{
  enum rtx_code code = GET_CODE (operands[1]);
  rtx cc_reg = mmix_gen_compare_reg (code, mmix_compare_op0,
				     mmix_compare_op1);
  if (cc_reg == NULL_RTX)
    FAIL;
  operands[1] = gen_rtx_fmt_ee (code, VOIDmode, cc_reg, const0_rtx);
}")

;; FIXME: Is this the right way to do "folding" of CCmode -> DImode?
(define_insn "*movdicc_real_foldable"
  [(set (match_operand:DI 0 "register_operand" "=r,r,r,r")
	(if_then_else:DI
	 (match_operator 2 "mmix_foldable_comparison_operator"
			 [(match_operand:DI 3 "register_operand" "r,r,r,r")
			  (const_int 0)])
	 (match_operand:DI 1 "mmix_reg_or_8bit_operand" "rI,0 ,rI,GM")
	 (match_operand:DI 4 "mmix_reg_or_8bit_operand" "0 ,rI,GM,rI")))]
  ""
  "@
   CS%d2 %0,%3,%1
   CS%D2 %0,%3,%4
   ZS%d2 %0,%3,%1
   ZS%D2 %0,%3,%4")

(define_insn "*movdicc_real_reversible"
  [(set
    (match_operand:DI 0 "register_operand"	   "=r ,r ,r ,r")
    (if_then_else:DI
     (match_operator
      2 "mmix_comparison_operator"
      [(match_operand 3 "mmix_reg_cc_operand"	    "r ,r ,r ,r")
      (const_int 0)])
     (match_operand:DI 1 "mmix_reg_or_8bit_operand" "rI,0 ,rI,GM")
     (match_operand:DI 4 "mmix_reg_or_8bit_operand" "0 ,rI,GM,rI")))]
  "REVERSIBLE_CC_MODE (GET_MODE (operands[3]))"
  "@
   CS%d2 %0,%3,%1
   CS%D2 %0,%3,%4
   ZS%d2 %0,%3,%1
   ZS%D2 %0,%3,%4")

(define_insn "*movdicc_real_nonreversible"
  [(set
    (match_operand:DI 0 "register_operand"	   "=r ,r")
    (if_then_else:DI
     (match_operator
      2 "mmix_comparison_operator"
      [(match_operand 3 "mmix_reg_cc_operand"	    "r ,r")
      (const_int 0)])
     (match_operand:DI 1 "mmix_reg_or_8bit_operand" "rI,rI")
     (match_operand:DI 4 "mmix_reg_or_0_operand" "0 ,GM")))]
  "!REVERSIBLE_CC_MODE (GET_MODE (operands[3]))"
  "@
   CS%d2 %0,%3,%1
   ZS%d2 %0,%3,%1")

(define_insn "*movdfcc_real_foldable"
  [(set
    (match_operand:DF 0 "register_operand"	"=r  ,r  ,r  ,r")
    (if_then_else:DF
     (match_operator
      2 "mmix_foldable_comparison_operator"
      [(match_operand:DI 3 "register_operand"	 "r  ,r  ,r  ,r")
      (const_int 0)])
     (match_operand:DF 1 "mmix_reg_or_0_operand" "rGM,0  ,rGM,GM")
     (match_operand:DF 4 "mmix_reg_or_0_operand" "0  ,rGM,GM ,rGM")))]
  ""
  "@
   CS%d2 %0,%3,%1
   CS%D2 %0,%3,%4
   ZS%d2 %0,%3,%1
   ZS%D2 %0,%3,%4")

(define_insn "*movdfcc_real_reversible"
  [(set
    (match_operand:DF 0 "register_operand"	"=r  ,r  ,r  ,r")
    (if_then_else:DF
     (match_operator
      2 "mmix_comparison_operator"
      [(match_operand 3 "mmix_reg_cc_operand"	 "r  ,r  ,r  ,r")
      (const_int 0)])
     (match_operand:DF 1 "mmix_reg_or_0_operand" "rGM,0  ,rGM,GM")
     (match_operand:DF 4 "mmix_reg_or_0_operand" "0  ,rGM,GM ,rGM")))]
  "REVERSIBLE_CC_MODE (GET_MODE (operands[3]))"
  "@
   CS%d2 %0,%3,%1
   CS%D2 %0,%3,%4
   ZS%d2 %0,%3,%1
   ZS%D2 %0,%3,%4")

(define_insn "*movdfcc_real_nonreversible"
  [(set
    (match_operand:DF 0 "register_operand"	"=r  ,r")
    (if_then_else:DF
     (match_operator
      2 "mmix_comparison_operator"
      [(match_operand 3 "mmix_reg_cc_operand"	 "r  ,r")
      (const_int 0)])
     (match_operand:DF 1 "mmix_reg_or_0_operand" "rGM,rGM")
     (match_operand:DF 4 "mmix_reg_or_0_operand" "0  ,GM")))]
  "!REVERSIBLE_CC_MODE (GET_MODE (operands[3]))"
  "@
   CS%d2 %0,%3,%1
   ZS%d2 %0,%3,%1")

;; FIXME: scc patterns will probably help, I just skip them
;; right now.  Revisit.

(define_expand "beq"
  [(set (pc)
	(if_then_else (eq (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1]
    = mmix_gen_compare_reg (EQ, mmix_compare_op0, mmix_compare_op1);
}")

(define_expand "bne"
  [(set (pc)
	(if_then_else (ne (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1]
    = mmix_gen_compare_reg (NE, mmix_compare_op0, mmix_compare_op1);
}")

(define_expand "bgt"
  [(set (pc)
	(if_then_else (gt (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1]
    = mmix_gen_compare_reg (GT, mmix_compare_op0, mmix_compare_op1);
}")

(define_expand "ble"
  [(set (pc)
	(if_then_else (le (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1]
    = mmix_gen_compare_reg (LE, mmix_compare_op0, mmix_compare_op1);

  /* The head comment of optabs.c:can_compare_p says we're required to
     implement this, so we have to clean up the mess here.  */
  if (operands[1] == NULL_RTX)
    {
      /* FIXME: Watch out for sharing/unsharing of rtx:es.  */
      emit_jump_insn ((*bcc_gen_fctn[(int) LT]) (operands[0]));
      emit_jump_insn ((*bcc_gen_fctn[(int) EQ]) (operands[0]));
      DONE;
    }
}")

(define_expand "bge"
  [(set (pc)
	(if_then_else (ge (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1]
    = mmix_gen_compare_reg (GE, mmix_compare_op0, mmix_compare_op1);

  /* The head comment of optabs.c:can_compare_p says we're required to
     implement this, so we have to clean up the mess here.  */
  if (operands[1] == NULL_RTX)
    {
      /* FIXME: Watch out for sharing/unsharing of rtx:es.  */
      emit_jump_insn ((*bcc_gen_fctn[(int) GT]) (operands[0]));
      emit_jump_insn ((*bcc_gen_fctn[(int) EQ]) (operands[0]));
      DONE;
    }
}")

(define_expand "blt"
  [(set (pc)
	(if_then_else (lt (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1]
    = mmix_gen_compare_reg (LT, mmix_compare_op0, mmix_compare_op1);
}")

(define_expand "bgtu"
  [(set (pc)
	(if_then_else (gtu (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1]
    = mmix_gen_compare_reg (GTU, mmix_compare_op0, mmix_compare_op1);
}")

(define_expand "bleu"
  [(set (pc)
	(if_then_else (leu (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1]
    = mmix_gen_compare_reg (LEU, mmix_compare_op0, mmix_compare_op1);
}")

(define_expand "bgeu"
  [(set (pc)
	(if_then_else (geu (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1]
    = mmix_gen_compare_reg (GEU, mmix_compare_op0, mmix_compare_op1);
}")

(define_expand "bltu"
  [(set (pc)
	(if_then_else (ltu (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1]
    = mmix_gen_compare_reg (LTU, mmix_compare_op0, mmix_compare_op1);
}")

(define_expand "bunordered"
  [(set (pc)
	(if_then_else (unordered (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1]
    = mmix_gen_compare_reg (UNORDERED, mmix_compare_op0, mmix_compare_op1);

  if (operands[1] == NULL_RTX)
    FAIL;
}")

(define_expand "bordered"
  [(set (pc)
	(if_then_else (ordered (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "
{
  operands[1]
    = mmix_gen_compare_reg (ORDERED, mmix_compare_op0, mmix_compare_op1);
}")

;; FIXME: we can emit an unordered-or-*not*-equal compare in one insn, but
;; there's no RTL code for it.  Maybe revisit in future.

;; FIXME: Odd/Even matchers?
(define_insn "*bCC_foldable"
  [(set (pc)
	(if_then_else
	 (match_operator 1 "mmix_foldable_comparison_operator"
			 [(match_operand:DI 2 "register_operand" "r")
			  (const_int 0)])
	 (label_ref (match_operand 0 "" ""))
	 (pc)))]
  ""
  "%+B%d1 %2,%0")

(define_insn "*bCC"
  [(set (pc)
	(if_then_else
	 (match_operator 1 "mmix_comparison_operator"
			 [(match_operand 2 "mmix_reg_cc_operand" "r")
			  (const_int 0)])
	 (label_ref (match_operand 0 "" ""))
	 (pc)))]
  ""
  "%+B%d1 %2,%0")

(define_insn "*bCC_inverted_foldable"
  [(set (pc)
	(if_then_else
	 (match_operator 1 "mmix_foldable_comparison_operator"
			 [(match_operand:DI 2 "register_operand" "r")
			  (const_int 0)])
		      (pc)
		      (label_ref (match_operand 0 "" ""))))]
;; REVERSIBLE_CC_MODE is checked by mmix_foldable_comparison_operator.
  ""
  "%+B%D1 %2,%0")

(define_insn "*bCC_inverted"
  [(set (pc)
	(if_then_else
	 (match_operator 1 "mmix_comparison_operator"
			 [(match_operand 2 "mmix_reg_cc_operand" "r")
			  (const_int 0)])
	 (pc)
	 (label_ref (match_operand 0 "" ""))))]
  "REVERSIBLE_CC_MODE (GET_MODE (operands[2]))"
  "%+B%D1 %2,%0")

(define_expand "call"
  [(parallel [(call (match_operand:QI 0 "memory_operand" "")
		    (match_operand 1 "general_operand" ""))
	      (use (match_operand 2 "general_operand" ""))
	      (clobber (match_dup 4))])
   (set (match_dup 4) (match_dup 3))]
  ""
  "
{
  /* The caller checks that the operand is generally valid as an
     address, but at -O0 nothing makes sure that it's also a valid
     call address for a *call*; a mmix_symbolic_or_address_operand.
     Force into a register if it isn't.  */
  if (!mmix_symbolic_or_address_operand (XEXP (operands[0], 0),
					 GET_MODE (XEXP (operands[0], 0))))
    operands[0]
      = replace_equiv_address (operands[0],
			       force_reg (Pmode, XEXP (operands[0], 0)));

  /* Since the epilogue 'uses' the return address, and it is clobbered
     in the call, and we set it back after every call (all but one setting
     will be optimized away), integrity is maintained.  */
  operands[3]
    = mmix_get_hard_reg_initial_val (Pmode,
				     MMIX_INCOMING_RETURN_ADDRESS_REGNUM);

  /* FIXME: There's a bug in gcc which causes NULL to be passed as
     operand[2] when we get out of registers, which later confuses gcc.
     Work around it by replacing it with const_int 0.  Possibly documentation
     error too.  */
  if (operands[2] == NULL_RTX)
    operands[2] = const0_rtx;

  operands[4] = gen_rtx_REG (DImode, MMIX_INCOMING_RETURN_ADDRESS_REGNUM);
}")

(define_expand "call_value"
  [(parallel [(set (match_operand 0 "" "")
		   (call (match_operand:QI 1 "memory_operand" "")
			 (match_operand 2 "general_operand" "")))
	      (use (match_operand 3 "general_operand" ""))
	      (clobber (match_dup 5))])
   (set (match_dup 5) (match_dup 4))]
  ""
  "
{
  /* The caller checks that the operand is generally valid as an
     address, but at -O0 nothing makes sure that it's also a valid
     call address for a *call*; a mmix_symbolic_or_address_operand.
     Force into a register if it isn't.  */
  if (!mmix_symbolic_or_address_operand (XEXP (operands[1], 0),
					 GET_MODE (XEXP (operands[1], 0))))
    operands[1]
      = replace_equiv_address (operands[1],
			       force_reg (Pmode, XEXP (operands[1], 0)));

  /* Since the epilogue 'uses' the return address, and it is clobbered
     in the call, and we set it back after every call (all but one setting
     will be optimized away), integrity is maintained.  */
  operands[4]
    = mmix_get_hard_reg_initial_val (Pmode,
				     MMIX_INCOMING_RETURN_ADDRESS_REGNUM);

  /* FIXME: See 'call'.  */
  if (operands[3] == NULL_RTX)
    operands[3] = const0_rtx;

  /* FIXME: Documentation bug: operands[3] (operands[2] for 'call') is the
     *next* argument register, not the number of arguments in registers.
     (There used to be code here where that mattered.)  */

  operands[5] = gen_rtx_REG (DImode, MMIX_INCOMING_RETURN_ADDRESS_REGNUM);
}")

;; Don't use 'p' here.  A 'p' must stand first in constraints, or reload
;; messes up, not registering the address for reload.  Several C++
;; testcases, including g++.brendan/crash40.C.  FIXME: This is arguably a
;; bug in gcc.  Note line ~2612 in reload.c, that does things on the
;; condition <<else if (constraints[i][0] == 'p')>> and the comment on
;; ~3017 that says:
;; <<   case 'p':
;;	     /* All necessary reloads for an address_operand
;;	        were handled in find_reloads_address.  */>>
;; Sorry, I have not dug deeper.  If symbolic addresses are used
;; rarely compared to addresses in registers, disparaging the
;; first ("p") alternative by adding ? in the first operand
;; might do the trick.  We define 'U' as a synonym to 'p', but without the
;; caveats (and very small advantages) of 'p'.
(define_insn "*call_real"
  [(call (mem:QI
	  (match_operand:DI 0 "mmix_symbolic_or_address_operand" "s,rU"))
	 (match_operand 1 "" ""))
   (use (match_operand 2 "" ""))
   (clobber (reg:DI MMIX_rJ_REGNUM))]
  ""
  "@
   PUSHJ $%p2,%0
   PUSHGO $%p2,%a0")

(define_insn "*call_value_real"
  [(set (match_operand 0 "register_operand" "=r,r")
	(call (mem:QI
	       (match_operand:DI 1 "mmix_symbolic_or_address_operand" "s,rU"))
	      (match_operand 2 "" "")))
  (use (match_operand 3 "" ""))
  (clobber (reg:DI MMIX_rJ_REGNUM))]
  ""
  "@
   PUSHJ $%p3,%1
   PUSHGO $%p3,%a1")

;; I hope untyped_call and untyped_return are not needed for MMIX.
;; Users of Objective-C will notice.

; Generated by GCC.
(define_expand "return"
  [(return)]
  "mmix_use_simple_return ()"
  "")

; Generated by the epilogue expander.
(define_insn "*expanded_return"
  [(return)]
  ""
  "POP %.,0")

(define_expand "prologue"
  [(const_int 0)]
  ""
  "mmix_expand_prologue (); DONE;")

; Note that the (return) from the expander itself is always the last insn
; in the epilogue.
(define_expand "epilogue"
  [(return)]
  ""
  "mmix_expand_epilogue ();")

(define_insn "nop"
  [(const_int 0)]
  ""
  "SWYM 0,0,0")

(define_insn "jump"
  [(set (pc) (label_ref (match_operand 0 "" "")))]
  ""
  "JMP %0")

(define_insn "indirect_jump"
  [(set (pc) (match_operand 0 "address_operand" "p"))]
  ""
  "GO $255,%a0")

;; FIXME: This is just a jump, and should be expanded to one.
(define_insn "tablejump"
  [(set (pc) (match_operand:DI 0 "address_operand" "p"))
   (use (label_ref (match_operand 1 "" "")))]
  ""
  "GO $255,%a0")

;; The only peculiar thing is that the register stack has to be unwound at
;; nonlocal_goto_receiver.  At each function that has a nonlocal label, we
;; save at function entry the location of the "alpha" register stack
;; pointer, rO, in a stack slot known to that function (right below where
;; the frame-pointer would be located).
;; In the nonlocal goto receiver, we unwind the register stack by a series
;; of "pop 0,0" until rO equals the saved value.  (If it goes lower, we
;; should die with a trap.)
(define_expand "nonlocal_goto_receiver"
  [(parallel [(unspec_volatile [(const_int 0)] 1)
	      (clobber (scratch:DI))
	      (clobber (reg:DI MMIX_rJ_REGNUM))])
   (set (reg:DI MMIX_rJ_REGNUM) (match_dup 0))]
  ""
  "
{
  operands[0]
    = mmix_get_hard_reg_initial_val (Pmode,
				     MMIX_INCOMING_RETURN_ADDRESS_REGNUM);

  /* Mark this function as containing a landing-pad.  */
  cfun->machine->has_landing_pad = 1;
}")

;; GCC can insist on using saved registers to keep the slot address in
;; "across" the exception, or (perhaps) to use saved registers in the
;; address and re-use them after the register stack unwind, so it's best
;; to form the address ourselves.
(define_insn "*nonlocal_goto_receiver_expanded"
  [(unspec_volatile [(const_int 0)] 1)
   (clobber (match_scratch:DI 0 "=&r"))
   (clobber (reg:DI MMIX_rJ_REGNUM))]
  ""
{
  rtx temp_reg = operands[0];
  rtx my_operands[2];
  HOST_WIDEST_INT offs;
  const char *my_template
    = "GETA $255,0f\;PUT rJ,$255\;LDOU $255,%a0\n\
0:\;GET %1,rO\;CMPU %1,%1,$255\;BNP %1,1f\;POP 0,0\n1:";

  my_operands[1] = temp_reg;

  /* If we have a frame-pointer (hence unknown stack-pointer offset),
     just use the frame-pointer and the known offset.  */
  if (frame_pointer_needed)
    {
      my_operands[0] = GEN_INT (-MMIX_fp_rO_OFFSET);

      output_asm_insn ("NEGU %1,0,%0", my_operands);
      my_operands[0] = gen_rtx_PLUS (Pmode, frame_pointer_rtx, temp_reg);
    }
  else
    {
      /* We know the fp-based offset, so "eliminate" it to be sp-based.  */
      offs
	= (mmix_initial_elimination_offset (MMIX_FRAME_POINTER_REGNUM,
					    MMIX_STACK_POINTER_REGNUM)
	   + MMIX_fp_rO_OFFSET);

      if (offs >= 0 && offs <= 255)
	my_operands[0]
	  = gen_rtx_PLUS (Pmode, stack_pointer_rtx, GEN_INT (offs));
      else
	{
	  mmix_output_register_setting (asm_out_file, REGNO (temp_reg),
					offs, 1);
	  my_operands[0] = gen_rtx_PLUS (Pmode, stack_pointer_rtx, temp_reg);
	}
    }

  output_asm_insn (my_template, my_operands);
  return "";
})

(define_insn "*Naddu"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (mult:DI (match_operand:DI 1 "register_operand" "r")
			  (match_operand:DI 2 "const_int_operand" "n"))
		 (match_operand:DI 3 "mmix_reg_or_8bit_operand" "rI")))]
  "GET_CODE (operands[2]) == CONST_INT
   && (INTVAL (operands[2]) == 2
       || INTVAL (operands[2]) == 4
       || INTVAL (operands[2]) == 8
       || INTVAL (operands[2]) == 16)"
  "%2ADDU %0,%1,%3")

(define_insn "*andn"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(and:DI
	 (not:DI (match_operand:DI 1 "mmix_reg_or_8bit_operand" "rI"))
	 (match_operand:DI 2 "register_operand" "r")))]
  ""
  "ANDN %0,%2,%1")

(define_insn "*nand"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ior:DI
	 (not:DI (match_operand:DI 1 "register_operand" "%r"))
	 (not:DI (match_operand:DI 2 "mmix_reg_or_8bit_operand" "rI"))))]
  ""
  "NAND %0,%1,%2")

(define_insn "*nor"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(and:DI
	 (not:DI (match_operand:DI 1 "register_operand" "%r"))
	 (not:DI (match_operand:DI 2 "mmix_reg_or_8bit_operand" "rI"))))]
  ""
  "NOR %0,%1,%2")

(define_insn "*nxor"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(not:DI
	 (xor:DI (match_operand:DI 1 "register_operand" "%r")
		 (match_operand:DI 2 "mmix_reg_or_8bit_operand" "rI"))))]
  ""
  "NXOR %0,%1,%2")

(define_insn "sync_icache"
  [(unspec_volatile [(match_operand:DI 0 "memory_operand" "m")
		     (match_operand:DI 1 "const_int_operand" "I")] 0)]
  ""
  "SYNCID %1,%0")

;; Local Variables:
;; mode: lisp
;; indent-tabs-mode: t
;; End:
