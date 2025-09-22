;; Machine description for GNU compiler, VAX Version
;; Copyright (C) 1987, 1988, 1991, 1994, 1995, 1996, 1998, 1999, 2000, 2001,
;; 2002, 2004, 2005 Free Software Foundation, Inc.

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


;;- Instruction patterns.  When multiple patterns apply,
;;- the first one in the file is chosen.
;;-
;;- See file "rtl.def" for documentation on define_insn, match_*, et al.
;;-
;;- cpp macro #define NOTICE_UPDATE_CC in file tm.h handles condition code
;;- updates for most instructions.

;; UNSPEC_VOLATILE usage:

(define_constants
  [(VUNSPEC_BLOCKAGE 0)	    ; `blockage' insn to prevent scheduling across an
			    ; insn in the code.
   (VUNSPEC_SYNC_ISTREAM 1) ; sequence of insns to sync the I-stream
   (VAX_AP_REGNUM 12)	    ; Register 12 contains the argument pointer
   (VAX_FP_REGNUM 13)	    ; Register 13 contains the frame pointer
   (VAX_SP_REGNUM 14)	    ; Register 14 contains the stack pointer
   (VAX_PC_REGNUM 15)	    ; Register 15 contains the program counter
  ]
)

;; Integer modes supported on VAX, with a mapping from machine mode
;; to mnemonic suffix.  DImode is always a special case.
(define_mode_macro VAXint [QI HI SI])
(define_mode_attr  isfx [(QI "b") (HI "w") (SI "l")])

;; Similar for float modes supported on VAX.
(define_mode_macro VAXfp [SF DF])
(define_mode_attr  fsfx [(SF "f") (DF "%#")])

;; Some output patterns want integer immediates with a prefix...
(define_mode_attr  iprefx [(QI "B") (HI "H") (SI "N")])

;; We don't want to allow a constant operand for test insns because
;; (set (cc0) (const_int foo)) has no mode information.  Such insns will
;; be folded while optimizing anyway.

(define_insn "tst<mode>"
  [(set (cc0)
	(match_operand:VAXint 0 "nonimmediate_operand" "g"))]
  ""
  "tst<VAXint:isfx> %0")

(define_insn "tst<mode>"
  [(set (cc0)
	(match_operand:VAXfp 0 "general_operand" "gF"))]
  ""
  "tst<VAXfp:fsfx> %0")

(define_insn "cmp<mode>"
  [(set (cc0)
	(compare (match_operand:VAXint 0 "nonimmediate_operand" "g")
		 (match_operand:VAXint 1 "general_operand" "g")))]
  ""
  "cmp<VAXint:isfx> %0,%1")

(define_insn "cmp<mode>"
  [(set (cc0)
	(compare (match_operand:VAXfp 0 "general_operand" "gF,gF")
		 (match_operand:VAXfp 1 "general_operand" "G,gF")))]
  ""
  "@
   tst<VAXfp:fsfx> %0
   cmp<VAXfp:fsfx> %0,%1")

(define_insn "*bit<mode>"
  [(set (cc0)
	(and:VAXint (match_operand:VAXint 0 "general_operand" "g")
		   (match_operand:VAXint 1 "general_operand" "g")))]
  ""
  "bit<VAXint:isfx> %0,%1")

;; The VAX has no sCOND insns.  It does have add/subtract with carry
;; which could be used to implement the sltu and sgeu patterns.  However,
;; to do this properly requires a complete rewrite of the compare insns
;; to keep them together with the sltu/sgeu insns until after the
;; reload pass is complete.  The previous implementation didn't do this
;; and has been deleted.


(define_insn "mov<mode>"
  [(set (match_operand:VAXfp 0 "nonimmediate_operand" "=g,g")
	(match_operand:VAXfp 1 "general_operand" "G,gF"))]
  ""
  "@
   clr<VAXfp:fsfx> %0
   mov<VAXfp:fsfx> %1,%0")

;; Some VAXen don't support this instruction.
;;(define_insn "movti"
;;  [(set (match_operand:TI 0 "general_operand" "=g")
;;	(match_operand:TI 1 "general_operand" "g"))]
;;  ""
;;  "movh %1,%0")

(define_insn "movdi"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=g,g")
	(match_operand:DI 1 "general_operand" "I,g"))]
  ""
  "@
   clrq %0
   movq %D1,%0")

;; The VAX move instructions have space-time tradeoffs.  On a MicroVAX
;; register-register mov instructions take 3 bytes and 2 CPU cycles.  clrl
;; takes 2 bytes and 3 cycles.  mov from constant to register takes 2 cycles
;; if the constant is smaller than 4 bytes, 3 cycles for a longword
;; constant.  movz, mneg, and mcom are as fast as mov, so movzwl is faster
;; than movl for positive constants that fit in 16 bits but not 6 bits.  cvt
;; instructions take 4 cycles.  inc takes 3 cycles.  The machine description
;; is willing to trade 1 byte for 1 cycle (clrl instead of movl $0; cvtwl
;; instead of movl).

;; Cycle counts for other models may vary (on a VAX 750 they are similar,
;; but on a VAX 9000 most move and add instructions with one constant
;; operand take 1 cycle).

;;  Loads of constants between 64 and 128 used to be done with
;; "addl3 $63,#,dst" but this is slower than movzbl and takes as much space.

(define_insn "mov<mode>"
  [(set (match_operand:VAXint 0 "nonimmediate_operand" "=g")
        (match_operand:VAXint 1 "general_operand" "g"))]
  ""
  "* return vax_output_int_move (insn, operands, <MODE>mode);")

(define_insn "movstricthi"
  [(set (strict_low_part (match_operand:HI 0 "register_operand" "+g"))
	(match_operand:HI 1 "general_operand" "g"))]
  ""
  "*
{
  if (CONST_INT_P (operands[1]))
    {
      int i = INTVAL (operands[1]);
      if (i == 0)
	return \"clrw %0\";
      else if ((unsigned int)i < 64)
	return \"movw %1,%0\";
      else if ((unsigned int)~i < 64)
	return \"mcomw %H1,%0\";
      else if ((unsigned int)i < 256)
	return \"movzbw %1,%0\";
    }
  return \"movw %1,%0\";
}")

(define_insn "movstrictqi"
  [(set (strict_low_part (match_operand:QI 0 "register_operand" "+g"))
	(match_operand:QI 1 "general_operand" "g"))]
  ""
  "*
{
  if (CONST_INT_P (operands[1]))
    {
      int i = INTVAL (operands[1]);
      if (i == 0)
	return \"clrb %0\";
      else if ((unsigned int)~i < 64)
	return \"mcomb %B1,%0\";
    }
  return \"movb %1,%0\";
}")

;; This is here to accept 4 arguments and pass the first 3 along
;; to the movmemhi1 pattern that really does the work.
(define_expand "movmemhi"
  [(set (match_operand:BLK 0 "general_operand" "=g")
	(match_operand:BLK 1 "general_operand" "g"))
   (use (match_operand:HI 2 "general_operand" "g"))
   (match_operand 3 "" "")]
  ""
  "
  emit_insn (gen_movmemhi1 (operands[0], operands[1], operands[2]));
  DONE;
")

;; The definition of this insn does not really explain what it does,
;; but it should suffice
;; that anything generated as this insn will be recognized as one
;; and that it won't successfully combine with anything.
(define_insn "movmemhi1"
  [(set (match_operand:BLK 0 "memory_operand" "=m")
	(match_operand:BLK 1 "memory_operand" "m"))
   (use (match_operand:HI 2 "general_operand" "g"))
   (clobber (reg:SI 0))
   (clobber (reg:SI 1))
   (clobber (reg:SI 2))
   (clobber (reg:SI 3))
   (clobber (reg:SI 4))
   (clobber (reg:SI 5))]
  ""
  "movc3 %2,%1,%0")

;; Extension and truncation insns.

(define_insn "truncsiqi2"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=g")
	(truncate:QI (match_operand:SI 1 "nonimmediate_operand" "g")))]
  ""
  "cvtlb %1,%0")

(define_insn "truncsihi2"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=g")
	(truncate:HI (match_operand:SI 1 "nonimmediate_operand" "g")))]
  ""
  "cvtlw %1,%0")

(define_insn "trunchiqi2"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=g")
	(truncate:QI (match_operand:HI 1 "nonimmediate_operand" "g")))]
  ""
  "cvtwb %1,%0")

(define_insn "extendhisi2"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(sign_extend:SI (match_operand:HI 1 "nonimmediate_operand" "g")))]
  ""
  "cvtwl %1,%0")

(define_insn "extendqihi2"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=g")
	(sign_extend:HI (match_operand:QI 1 "nonimmediate_operand" "g")))]
  ""
  "cvtbw %1,%0")

(define_insn "extendqisi2"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(sign_extend:SI (match_operand:QI 1 "nonimmediate_operand" "g")))]
  ""
  "cvtbl %1,%0")

(define_insn "extendsfdf2"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=g")
	(float_extend:DF (match_operand:SF 1 "general_operand" "gF")))]
  ""
  "cvtf%# %1,%0")

(define_insn "truncdfsf2"
  [(set (match_operand:SF 0 "nonimmediate_operand" "=g")
	(float_truncate:SF (match_operand:DF 1 "general_operand" "gF")))]
  ""
  "cvt%#f %1,%0")

(define_insn "zero_extendhisi2"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(zero_extend:SI (match_operand:HI 1 "nonimmediate_operand" "g")))]
  ""
  "movzwl %1,%0")

(define_insn "zero_extendqihi2"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=g")
	(zero_extend:HI (match_operand:QI 1 "nonimmediate_operand" "g")))]
  ""
  "movzbw %1,%0")

(define_insn "zero_extendqisi2"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(zero_extend:SI (match_operand:QI 1 "nonimmediate_operand" "g")))]
  ""
  "movzbl %1,%0")

;; Fix-to-float conversion insns.

(define_insn "float<VAXint:mode><VAXfp:mode>2"
  [(set (match_operand:VAXfp 0 "nonimmediate_operand" "=g")
	(float:VAXfp (match_operand:VAXint 1 "nonimmediate_operand" "g")))]
  ""
  "cvt<VAXint:isfx><VAXfp:fsfx> %1,%0")

;; Float-to-fix conversion insns.

(define_insn "fix_trunc<VAXfp:mode><VAXint:mode>2"
  [(set (match_operand:VAXint 0 "nonimmediate_operand" "=g")
	(fix:VAXint (fix:VAXfp (match_operand:VAXfp 1 "general_operand" "gF"))))]
  ""
  "cvt<VAXfp:fsfx><VAXint:isfx> %1,%0")

;;- All kinds of add instructions.

(define_insn "add<mode>3"
  [(set (match_operand:VAXfp 0 "nonimmediate_operand" "=g,g,g")
	(plus:VAXfp (match_operand:VAXfp 1 "general_operand" "0,gF,gF")
		    (match_operand:VAXfp 2 "general_operand" "gF,0,gF")))]
  ""
  "@
   add<VAXfp:fsfx>2 %2,%0
   add<VAXfp:fsfx>2 %1,%0
   add<VAXfp:fsfx>3 %1,%2,%0")

(define_insn "add<mode>3"
  [(set (match_operand:VAXint 0 "nonimmediate_operand" "=g")
	(plus:VAXint (match_operand:VAXint 1 "general_operand" "g")
		    (match_operand:VAXint 2 "general_operand" "g")))]
  ""
  "* return vax_output_int_add (insn, operands, <MODE>mode);")

;; The add-with-carry (adwc) instruction only accepts two operands.
(define_insn "adddi3"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=ro>,ro>")
	(plus:DI (match_operand:DI 1 "general_operand" "%0,ro>")
		 (match_operand:DI 2 "general_operand" "Fro,F")))]
  ""
  "*
{
  rtx low[3];
  const char *pattern;
  int carry = 1;

  split_quadword_operands (operands, low, 3);
  /* Add low parts.  */
  if (rtx_equal_p (operands[0], operands[1]))
    {
      if (low[2] == const0_rtx)
	/* Should examine operand, punt if not POST_INC.  */
	pattern = \"tstl %0\", carry = 0;
      else if (low[2] == const1_rtx)
        pattern = \"incl %0\";
      else
        pattern = \"addl2 %2,%0\";
    }
  else
    {
      if (low[2] == const0_rtx)
	pattern = \"movl %1,%0\", carry = 0;
      else
	pattern = \"addl3 %2,%1,%0\";
    }
  if (pattern)
    output_asm_insn (pattern, low);
  if (!carry)
    /* If CARRY is 0, we don't have any carry value to worry about.  */
    return get_insn_template (CODE_FOR_addsi3, insn);
  /* %0 = C + %1 + %2 */
  if (!rtx_equal_p (operands[0], operands[1]))
    output_asm_insn ((operands[1] == const0_rtx
		      ? \"clrl %0\"
		      : \"movl %1,%0\"), operands);
  return \"adwc %2,%0\";
}")

;;- All kinds of subtract instructions.

(define_insn "sub<mode>3"
  [(set (match_operand:VAXfp 0 "nonimmediate_operand" "=g,g")
	(minus:VAXfp (match_operand:VAXfp 1 "general_operand" "0,gF")
		     (match_operand:VAXfp 2 "general_operand" "gF,gF")))]
  ""
  "@
   sub<VAXfp:fsfx>2 %2,%0
   sub<VAXfp:fsfx>3 %2,%1,%0")

(define_insn "sub<mode>3"
  [(set (match_operand:VAXint 0 "nonimmediate_operand" "=g,g")
	(minus:VAXint (match_operand:VAXint 1 "general_operand" "0,g")
		     (match_operand:VAXint 2 "general_operand" "g,g")))]
  ""
  "@
   sub<VAXint:isfx>2 %2,%0
   sub<VAXint:isfx>3 %2,%1,%0")

;; The subtract-with-carry (sbwc) instruction only takes two operands.
(define_insn "subdi3"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=or>,or>")
	(minus:DI (match_operand:DI 1 "general_operand" "0,or>")
		  (match_operand:DI 2 "general_operand" "For,F")))]
  ""
  "*
{
  rtx low[3];
  const char *pattern;
  int carry = 1;

  split_quadword_operands (operands, low, 3);
  /* Subtract low parts.  */
  if (rtx_equal_p (operands[0], operands[1]))
    {
      if (low[2] == const0_rtx)
	pattern = 0, carry = 0;
      else if (low[2] == constm1_rtx)
	pattern = \"decl %0\";
      else
	pattern = \"subl2 %2,%0\";
    }
  else
    {
      if (low[2] == constm1_rtx)
	pattern = \"decl %0\";
      else if (low[2] == const0_rtx)
	pattern = get_insn_template (CODE_FOR_movsi, insn), carry = 0;
      else
	pattern = \"subl3 %2,%1,%0\";
    }
  if (pattern)
    output_asm_insn (pattern, low);
  if (carry)
    {
      if (!rtx_equal_p (operands[0], operands[1]))
	return \"movl %1,%0\;sbwc %2,%0\";
      return \"sbwc %2,%0\";
      /* %0 = %2 - %1 - C */
    }
  return get_insn_template (CODE_FOR_subsi3, insn);
}")

;;- Multiply instructions.

(define_insn "mul<mode>3"
  [(set (match_operand:VAXfp 0 "nonimmediate_operand" "=g,g,g")
	(mult:VAXfp (match_operand:VAXfp 1 "general_operand" "0,gF,gF")
		    (match_operand:VAXfp 2 "general_operand" "gF,0,gF")))]
  ""
  "@
   mul<VAXfp:fsfx>2 %2,%0
   mul<VAXfp:fsfx>2 %1,%0
   mul<VAXfp:fsfx>3 %1,%2,%0")

(define_insn "mul<mode>3"
  [(set (match_operand:VAXint 0 "nonimmediate_operand" "=g,g,g")
	(mult:VAXint (match_operand:VAXint 1 "general_operand" "0,g,g")
		    (match_operand:VAXint 2 "general_operand" "g,0,g")))]
  ""
  "@
   mul<VAXint:isfx>2 %2,%0
   mul<VAXint:isfx>2 %1,%0
   mul<VAXint:isfx>3 %1,%2,%0")

(define_insn "mulsidi3"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=g")
	(mult:DI (sign_extend:DI
		  (match_operand:SI 1 "nonimmediate_operand" "g"))
		 (sign_extend:DI
		  (match_operand:SI 2 "nonimmediate_operand" "g"))))]
  ""
  "emul %1,%2,$0,%0")

(define_insn ""
  [(set (match_operand:DI 0 "nonimmediate_operand" "=g")
	(plus:DI
	 (mult:DI (sign_extend:DI
		   (match_operand:SI 1 "nonimmediate_operand" "g"))
		  (sign_extend:DI
		   (match_operand:SI 2 "nonimmediate_operand" "g")))
	 (sign_extend:DI (match_operand:SI 3 "nonimmediate_operand" "g"))))]
  ""
  "emul %1,%2,%3,%0")

;; 'F' constraint means type CONST_DOUBLE
(define_insn ""
  [(set (match_operand:DI 0 "nonimmediate_operand" "=g")
	(plus:DI
	 (mult:DI (sign_extend:DI
		   (match_operand:SI 1 "nonimmediate_operand" "g"))
		  (sign_extend:DI
		   (match_operand:SI 2 "nonimmediate_operand" "g")))
	 (match_operand:DI 3 "immediate_operand" "F")))]
  "GET_CODE (operands[3]) == CONST_DOUBLE
    && CONST_DOUBLE_HIGH (operands[3]) == (CONST_DOUBLE_LOW (operands[3]) >> 31)"
  "*
{
  if (CONST_DOUBLE_HIGH (operands[3]))
    operands[3] = GEN_INT (CONST_DOUBLE_LOW (operands[3]));
  return \"emul %1,%2,%3,%0\";
}")

;;- Divide instructions.

(define_insn "div<mode>3"
  [(set (match_operand:VAXfp 0 "nonimmediate_operand" "=g,g")
	(div:VAXfp (match_operand:VAXfp 1 "general_operand" "0,gF")
		   (match_operand:VAXfp 2 "general_operand" "gF,gF")))]
  ""
  "@
   div<VAXfp:fsfx>2 %2,%0
   div<VAXfp:fsfx>3 %2,%1,%0")

(define_insn "div<mode>3"
  [(set (match_operand:VAXint 0 "nonimmediate_operand" "=g,g")
	(div:VAXint (match_operand:VAXint 1 "general_operand" "0,g")
		   (match_operand:VAXint 2 "general_operand" "g,g")))]
  ""
  "@
   div<VAXint:isfx>2 %2,%0
   div<VAXint:isfx>3 %2,%1,%0")

;This is left out because it is very slow;
;we are better off programming around the "lack" of this insn.
;(define_insn "divmoddisi4"
;  [(set (match_operand:SI 0 "general_operand" "=g")
;	(div:SI (match_operand:DI 1 "general_operand" "g")
;		(match_operand:SI 2 "general_operand" "g")))
;   (set (match_operand:SI 3 "general_operand" "=g")
;	(mod:SI (match_operand:DI 1 "general_operand" "g")
;		(match_operand:SI 2 "general_operand" "g")))]
;  ""
;  "ediv %2,%1,%0,%3")

;; Bit-and on the VAX is done with a clear-bits insn.
(define_expand "and<mode>3"
  [(set (match_operand:VAXint 0 "nonimmediate_operand" "")
	(and:VAXint (not:VAXint (match_operand:VAXint 1 "general_operand" ""))
		   (match_operand:VAXint 2 "general_operand" "")))]
  ""
  "
{
  rtx op1 = operands[1];

  /* If there is a constant argument, complement that one.  */
  if (CONST_INT_P (operands[2]) && !CONST_INT_P (op1))
    {
      operands[1] = operands[2];
      operands[2] = op1;
      op1 = operands[1];
    }

  if (CONST_INT_P (op1))
    operands[1] = GEN_INT (~INTVAL (op1));
  else
    operands[1] = expand_unop (<MODE>mode, one_cmpl_optab, op1, 0, 1);
}")

(define_insn "*and<mode>"
  [(set (match_operand:VAXint 0 "nonimmediate_operand" "=g,g")
	(and:VAXint (not:VAXint (match_operand:VAXint 1 "general_operand" "g,g"))
		    (match_operand:VAXint 2 "general_operand" "0,g")))]
  ""
  "@
   bic<VAXint:isfx>2 %1,%0
   bic<VAXint:isfx>3 %1,%2,%0")

;; The following used to be needed because constant propagation can
;; create them starting from the bic insn patterns above.  This is no
;; longer a problem.  However, having these patterns allows optimization
;; opportunities in combine.c.

(define_insn "*and<mode>_const_int"
  [(set (match_operand:VAXint 0 "nonimmediate_operand" "=g,g")
	(and:VAXint (match_operand:VAXint 1 "general_operand" "0,g")
		   (match_operand:VAXint 2 "const_int_operand" "n,n")))]
  ""
  "@
   bic<VAXint:isfx>2 %<VAXint:iprefx>2,%0
   bic<VAXint:isfx>3 %<VAXint:iprefx>2,%1,%0")


;;- Bit set instructions.

(define_insn "ior<mode>3"
  [(set (match_operand:VAXint 0 "nonimmediate_operand" "=g,g,g")
	(ior:VAXint (match_operand:VAXint 1 "general_operand" "0,g,g")
		   (match_operand:VAXint 2 "general_operand" "g,0,g")))]
  ""
  "@
   bis<VAXint:isfx>2 %2,%0
   bis<VAXint:isfx>2 %1,%0
   bis<VAXint:isfx>3 %2,%1,%0")

;;- xor instructions.

(define_insn "xor<mode>3"
  [(set (match_operand:VAXint 0 "nonimmediate_operand" "=g,g,g")
	(xor:VAXint (match_operand:VAXint 1 "general_operand" "0,g,g")
		   (match_operand:VAXint 2 "general_operand" "g,0,g")))]
  ""
  "@
   xor<VAXint:isfx>2 %2,%0
   xor<VAXint:isfx>2 %1,%0
   xor<VAXint:isfx>3 %2,%1,%0")


(define_insn "neg<mode>2"
  [(set (match_operand:VAXfp 0 "nonimmediate_operand" "=g")
	(neg:VAXfp (match_operand:VAXfp 1 "general_operand" "gF")))]
  ""
  "mneg<VAXfp:fsfx> %1,%0")

(define_insn "neg<mode>2"
  [(set (match_operand:VAXint 0 "nonimmediate_operand" "=g")
	(neg:VAXint (match_operand:VAXint 1 "general_operand" "g")))]
  ""
  "mneg<VAXint:isfx> %1,%0")

(define_insn "one_cmpl<mode>2"
  [(set (match_operand:VAXint 0 "nonimmediate_operand" "=g")
	(not:VAXint (match_operand:VAXint 1 "general_operand" "g")))]
  ""
  "mcom<VAXint:isfx> %1,%0")


;; Arithmetic right shift on the VAX works by negating the shift count,
;; then emitting a right shift with the shift count negated.  This means
;; that all actual shift counts in the RTL will be positive.  This
;; prevents converting shifts to ZERO_EXTRACTs with negative positions,
;; which isn't valid.
(define_expand "ashrsi3"
  [(set (match_operand:SI 0 "general_operand" "=g")
	(ashiftrt:SI (match_operand:SI 1 "general_operand" "g")
		   (match_operand:QI 2 "general_operand" "g")))]
  ""
  "
{
  if (!CONST_INT_P (operands[2]))
    operands[2] = gen_rtx_NEG (QImode, negate_rtx (QImode, operands[2]));
}")

(define_insn ""
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(ashiftrt:SI (match_operand:SI 1 "general_operand" "g")
		     (match_operand:QI 2 "const_int_operand" "n")))]
  ""
  "ashl $%n2,%1,%0")

(define_insn ""
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(ashiftrt:SI (match_operand:SI 1 "general_operand" "g")
		     (neg:QI (match_operand:QI 2 "general_operand" "g"))))]
  ""
  "ashl %2,%1,%0")

(define_insn "ashlsi3"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(ashift:SI (match_operand:SI 1 "general_operand" "g")
		   (match_operand:QI 2 "general_operand" "g")))]
  ""
  "*
{
  if (operands[2] == const1_rtx && rtx_equal_p (operands[0], operands[1]))
    return \"addl2 %0,%0\";
  if (REG_P (operands[1])
      && CONST_INT_P (operands[2]))
    {
      int i = INTVAL (operands[2]);
      if (i == 1)
	return \"addl3 %1,%1,%0\";
      if (i == 2)
	return \"moval 0[%1],%0\";
      if (i == 3)
	return \"movad 0[%1],%0\";
    }
  return \"ashl %2,%1,%0\";
}")

;; Arithmetic right shift on the VAX works by negating the shift count.
(define_expand "ashrdi3"
  [(set (match_operand:DI 0 "general_operand" "=g")
	(ashiftrt:DI (match_operand:DI 1 "general_operand" "g")
		     (match_operand:QI 2 "general_operand" "g")))]
  ""
  "
{
  operands[2] = gen_rtx_NEG (QImode, negate_rtx (QImode, operands[2]));
}")

(define_insn "ashldi3"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=g")
	(ashift:DI (match_operand:DI 1 "general_operand" "g")
		   (match_operand:QI 2 "general_operand" "g")))]
  ""
  "ashq %2,%1,%0")

(define_insn ""
  [(set (match_operand:DI 0 "nonimmediate_operand" "=g")
	(ashiftrt:DI (match_operand:DI 1 "general_operand" "g")
		     (neg:QI (match_operand:QI 2 "general_operand" "g"))))]
  ""
  "ashq %2,%1,%0")

;; We used to have expand_shift handle logical right shifts by using extzv,
;; but this make it very difficult to do lshrdi3.  Since the VAX is the
;; only machine with this kludge, it's better to just do this with a
;; define_expand and remove that case from expand_shift.

(define_expand "lshrsi3"
  [(set (match_dup 3)
	(minus:QI (const_int 32)
		  (match_dup 4)))
   (set (match_operand:SI 0 "general_operand" "=g")
	(zero_extract:SI (match_operand:SI 1 "register_operand" "r")
			 (match_dup 3)
			 (match_operand:SI 2 "register_operand" "g")))]
  ""
  "
{
  operands[3] = gen_reg_rtx (QImode);
  operands[4] = gen_lowpart (QImode, operands[2]);
}")

;; Rotate right on the VAX works by negating the shift count.
(define_expand "rotrsi3"
  [(set (match_operand:SI 0 "general_operand" "=g")
	(rotatert:SI (match_operand:SI 1 "general_operand" "g")
		     (match_operand:QI 2 "general_operand" "g")))]
  ""
  "
{
  if (!CONST_INT_P (operands[2]))
    operands[2] = gen_rtx_NEG (QImode, negate_rtx (QImode, operands[2]));
}")

(define_insn "rotlsi3"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(rotate:SI (match_operand:SI 1 "general_operand" "g")
		   (match_operand:QI 2 "general_operand" "g")))]
  ""
  "rotl %2,%1,%0")

(define_insn ""
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(rotatert:SI (match_operand:SI 1 "general_operand" "g")
		     (match_operand:QI 2 "const_int_operand" "n")))]
  ""
  "rotl %R2,%1,%0")

(define_insn ""
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(rotatert:SI (match_operand:SI 1 "general_operand" "g")
		     (neg:QI (match_operand:QI 2 "general_operand" "g"))))]
  ""
  "rotl %2,%1,%0")

;This insn is probably slower than a multiply and an add.
;(define_insn ""
;  [(set (match_operand:SI 0 "general_operand" "=g")
;	(mult:SI (plus:SI (match_operand:SI 1 "general_operand" "g")
;			  (match_operand:SI 2 "general_operand" "g"))
;		 (match_operand:SI 3 "general_operand" "g")))]
;  ""
;  "index %1,$0x80000000,$0x7fffffff,%3,%2,%0")

;; Special cases of bit-field insns which we should
;; recognize in preference to the general case.
;; These handle aligned 8-bit and 16-bit fields,
;; which can usually be done with move instructions.

(define_insn ""
  [(set (zero_extract:SI (match_operand:SI 0 "register_operand" "+ro")
			 (match_operand:QI 1 "const_int_operand" "n")
			 (match_operand:SI 2 "const_int_operand" "n"))
	(match_operand:SI 3 "general_operand" "g"))]
   "(INTVAL (operands[1]) == 8 || INTVAL (operands[1]) == 16)
   && INTVAL (operands[2]) % INTVAL (operands[1]) == 0
   && (REG_P (operands[0])
       || !mode_dependent_address_p (XEXP (operands[0], 0)))"
  "*
{
  if (REG_P (operands[0]))
    {
      if (INTVAL (operands[2]) != 0)
	return \"insv %3,%2,%1,%0\";
    }
  else
    operands[0]
      = adjust_address (operands[0],
			INTVAL (operands[1]) == 8 ? QImode : HImode,
			INTVAL (operands[2]) / 8);

  CC_STATUS_INIT;
  if (INTVAL (operands[1]) == 8)
    return \"movb %3,%0\";
  return \"movw %3,%0\";
}")

(define_insn ""
  [(set (match_operand:SI 0 "nonimmediate_operand" "=&g")
	(zero_extract:SI (match_operand:SI 1 "register_operand" "ro")
			 (match_operand:QI 2 "const_int_operand" "n")
			 (match_operand:SI 3 "const_int_operand" "n")))]
  "(INTVAL (operands[2]) == 8 || INTVAL (operands[2]) == 16)
   && INTVAL (operands[3]) % INTVAL (operands[2]) == 0
   && (REG_P (operands[1])
       || !mode_dependent_address_p (XEXP (operands[1], 0)))"
  "*
{
  if (REG_P (operands[1]))
    {
      if (INTVAL (operands[3]) != 0)
	return \"extzv %3,%2,%1,%0\";
    }
  else
    operands[1]
      = adjust_address (operands[1],
			INTVAL (operands[2]) == 8 ? QImode : HImode,
			INTVAL (operands[3]) / 8);

  if (INTVAL (operands[2]) == 8)
    return \"movzbl %1,%0\";
  return \"movzwl %1,%0\";
}")

(define_insn ""
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(sign_extract:SI (match_operand:SI 1 "register_operand" "ro")
			 (match_operand:QI 2 "const_int_operand" "n")
			 (match_operand:SI 3 "const_int_operand" "n")))]
  "(INTVAL (operands[2]) == 8 || INTVAL (operands[2]) == 16)
   && INTVAL (operands[3]) % INTVAL (operands[2]) == 0
   && (REG_P (operands[1])
       || !mode_dependent_address_p (XEXP (operands[1], 0)))"
  "*
{
  if (REG_P (operands[1]))
    {
      if (INTVAL (operands[3]) != 0)
	return \"extv %3,%2,%1,%0\";
    }
  else
    operands[1]
      = adjust_address (operands[1],
			INTVAL (operands[2]) == 8 ? QImode : HImode,
			INTVAL (operands[3]) / 8);

  if (INTVAL (operands[2]) == 8)
    return \"cvtbl %1,%0\";
  return \"cvtwl %1,%0\";
}")

;; Register-only SImode cases of bit-field insns.

(define_insn ""
  [(set (cc0)
	(compare
	 (sign_extract:SI (match_operand:SI 0 "register_operand" "r")
			  (match_operand:QI 1 "general_operand" "g")
			  (match_operand:SI 2 "general_operand" "g"))
	 (match_operand:SI 3 "general_operand" "g")))]
  ""
  "cmpv %2,%1,%0,%3")

(define_insn ""
  [(set (cc0)
	(compare
	 (zero_extract:SI (match_operand:SI 0 "register_operand" "r")
			  (match_operand:QI 1 "general_operand" "g")
			  (match_operand:SI 2 "general_operand" "g"))
	 (match_operand:SI 3 "general_operand" "g")))]
  ""
  "cmpzv %2,%1,%0,%3")

;; When the field position and size are constant and the destination
;; is a register, extv and extzv are much slower than a rotate followed
;; by a bicl or sign extension.  Because we might end up choosing ext[z]v
;; anyway, we can't allow immediate values for the primary source operand.

(define_insn ""
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(sign_extract:SI (match_operand:SI 1 "register_operand" "ro")
			 (match_operand:QI 2 "general_operand" "g")
			 (match_operand:SI 3 "general_operand" "g")))]
  ""
  "*
{
  if (!CONST_INT_P (operands[3])
      || !CONST_INT_P (operands[2])
      || !REG_P (operands[0])
      || (INTVAL (operands[2]) != 8 && INTVAL (operands[2]) != 16))
    return \"extv %3,%2,%1,%0\";
  if (INTVAL (operands[2]) == 8)
    return \"rotl %R3,%1,%0\;cvtbl %0,%0\";
  return \"rotl %R3,%1,%0\;cvtwl %0,%0\";
}")

(define_insn ""
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(zero_extract:SI (match_operand:SI 1 "register_operand" "ro")
			 (match_operand:QI 2 "general_operand" "g")
			 (match_operand:SI 3 "general_operand" "g")))]
  ""
  "*
{
  if (!CONST_INT_P (operands[3])
      || !CONST_INT_P (operands[2])
      || !REG_P (operands[0]))
    return \"extzv %3,%2,%1,%0\";
  if (INTVAL (operands[2]) == 8)
    return \"rotl %R3,%1,%0\;movzbl %0,%0\";
  if (INTVAL (operands[2]) == 16)
    return \"rotl %R3,%1,%0\;movzwl %0,%0\";
  if (INTVAL (operands[3]) & 31)
    return \"rotl %R3,%1,%0\;bicl2 %M2,%0\";
  if (rtx_equal_p (operands[0], operands[1]))
    return \"bicl2 %M2,%0\";
  return \"bicl3 %M2,%1,%0\";
}")

;; Non-register cases.
;; nonimmediate_operand is used to make sure that mode-ambiguous cases
;; don't match these (and therefore match the cases above instead).

(define_insn ""
  [(set (cc0)
	(compare
	 (sign_extract:SI (match_operand:QI 0 "memory_operand" "m")
			  (match_operand:QI 1 "general_operand" "g")
			  (match_operand:SI 2 "general_operand" "g"))
	 (match_operand:SI 3 "general_operand" "g")))]
  ""
  "cmpv %2,%1,%0,%3")

(define_insn ""
  [(set (cc0)
	(compare
	 (zero_extract:SI (match_operand:QI 0 "nonimmediate_operand" "rm")
			  (match_operand:QI 1 "general_operand" "g")
			  (match_operand:SI 2 "general_operand" "g"))
	 (match_operand:SI 3 "general_operand" "g")))]
  ""
  "cmpzv %2,%1,%0,%3")

(define_insn "extv"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(sign_extract:SI (match_operand:QI 1 "memory_operand" "m")
			 (match_operand:QI 2 "general_operand" "g")
			 (match_operand:SI 3 "general_operand" "g")))]
  ""
  "*
{
  if (!REG_P (operands[0])
      || !CONST_INT_P (operands[2])
      || !CONST_INT_P (operands[3])
      || (INTVAL (operands[2]) != 8 && INTVAL (operands[2]) != 16)
      || INTVAL (operands[2]) + INTVAL (operands[3]) > 32
      || side_effects_p (operands[1])
      || (MEM_P (operands[1])
	  && mode_dependent_address_p (XEXP (operands[1], 0))))
    return \"extv %3,%2,%1,%0\";
  if (INTVAL (operands[2]) == 8)
    return \"rotl %R3,%1,%0\;cvtbl %0,%0\";
  return \"rotl %R3,%1,%0\;cvtwl %0,%0\";
}")

(define_expand "extzv"
  [(set (match_operand:SI 0 "general_operand" "")
	(zero_extract:SI (match_operand:SI 1 "general_operand" "")
			 (match_operand:QI 2 "general_operand" "")
			 (match_operand:SI 3 "general_operand" "")))]
  ""
  "")

(define_insn ""
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(zero_extract:SI (match_operand:QI 1 "memory_operand" "m")
			 (match_operand:QI 2 "general_operand" "g")
			 (match_operand:SI 3 "general_operand" "g")))]
  ""
  "*
{
  if (!REG_P (operands[0])
      || !CONST_INT_P (operands[2])
      || !CONST_INT_P (operands[3])
      || INTVAL (operands[2]) + INTVAL (operands[3]) > 32
      || side_effects_p (operands[1])
      || (MEM_P (operands[1])
	  && mode_dependent_address_p (XEXP (operands[1], 0))))
    return \"extzv %3,%2,%1,%0\";
  if (INTVAL (operands[2]) == 8)
    return \"rotl %R3,%1,%0\;movzbl %0,%0\";
  if (INTVAL (operands[2]) == 16)
    return \"rotl %R3,%1,%0\;movzwl %0,%0\";
  return \"rotl %R3,%1,%0\;bicl2 %M2,%0\";
}")

(define_expand "insv"
  [(set (zero_extract:SI (match_operand:SI 0 "general_operand" "")
			 (match_operand:QI 1 "general_operand" "")
			 (match_operand:SI 2 "general_operand" ""))
	(match_operand:SI 3 "general_operand" ""))]
  ""
  "")

(define_insn ""
  [(set (zero_extract:SI (match_operand:QI 0 "memory_operand" "+g")
			 (match_operand:QI 1 "general_operand" "g")
			 (match_operand:SI 2 "general_operand" "g"))
	(match_operand:SI 3 "general_operand" "g"))]
  ""
  "insv %3,%2,%1,%0")

(define_insn ""
  [(set (zero_extract:SI (match_operand:SI 0 "register_operand" "+r")
			 (match_operand:QI 1 "general_operand" "g")
			 (match_operand:SI 2 "general_operand" "g"))
	(match_operand:SI 3 "general_operand" "g"))]
  ""
  "insv %3,%2,%1,%0")

;; Unconditional jump
(define_insn "jump"
  [(set (pc)
	(label_ref (match_operand 0 "" "")))]
  ""
  "jbr %l0")

;; Conditional jumps
(define_code_macro any_cond [eq ne gt lt gtu ltu ge le geu leu])

(define_insn "b<code>"
  [(set (pc)
	(if_then_else (any_cond (cc0)
				(const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "* return vax_output_conditional_branch (<CODE>);")

;; Recognize reversed jumps.
(define_insn ""
  [(set (pc)
	(if_then_else (match_operator 0 "comparison_operator"
				      [(cc0)
				       (const_int 0)])
		      (pc)
		      (label_ref (match_operand 1 "" ""))))]
  ""
  "j%C0 %l1") ; %C0 negates condition

;; Recognize jbs, jlbs, jbc and jlbc instructions.  Note that the operand
;; of jlbs and jlbc insns are SImode in the hardware.  However, if it is
;; memory, we use QImode in the insn.  So we can't use those instructions
;; for mode-dependent addresses.

(define_insn ""
  [(set (pc)
	(if_then_else
	 (ne (zero_extract:SI (match_operand:QI 0 "memory_operand" "Q,g")
			      (const_int 1)
			      (match_operand:SI 1 "general_operand" "I,g"))
	     (const_int 0))
	 (label_ref (match_operand 2 "" ""))
	 (pc)))]
  ""
  "@
   jlbs %0,%l2
   jbs %1,%0,%l2")

(define_insn ""
  [(set (pc)
	(if_then_else
	 (eq (zero_extract:SI (match_operand:QI 0 "memory_operand" "Q,g")
			      (const_int 1)
			      (match_operand:SI 1 "general_operand" "I,g"))
	     (const_int 0))
	 (label_ref (match_operand 2 "" ""))
	 (pc)))]
  ""
  "@
   jlbc %0,%l2
   jbc %1,%0,%l2")

(define_insn ""
  [(set (pc)
	(if_then_else
	 (ne (zero_extract:SI (match_operand:SI 0 "register_operand" "r,r")
			      (const_int 1)
			      (match_operand:SI 1 "general_operand" "I,g"))
	     (const_int 0))
	 (label_ref (match_operand 2 "" ""))
	 (pc)))]
  ""
  "@
   jlbs %0,%l2
   jbs %1,%0,%l2")

(define_insn ""
  [(set (pc)
	(if_then_else
	 (eq (zero_extract:SI (match_operand:SI 0 "register_operand" "r,r")
			      (const_int 1)
			      (match_operand:SI 1 "general_operand" "I,g"))
	     (const_int 0))
	 (label_ref (match_operand 2 "" ""))
	 (pc)))]
  ""
  "@
   jlbc %0,%l2
   jbc %1,%0,%l2")

;; Subtract-and-jump and Add-and-jump insns.
;; These are not used when output is for the Unix assembler
;; because it does not know how to modify them to reach far.

;; Normal sob insns.

(define_insn ""
  [(set (pc)
	(if_then_else
	 (gt (plus:SI (match_operand:SI 0 "nonimmediate_operand" "+g")
		      (const_int -1))
	     (const_int 0))
	 (label_ref (match_operand 1 "" ""))
	 (pc)))
   (set (match_dup 0)
	(plus:SI (match_dup 0)
		 (const_int -1)))]
  "!TARGET_UNIX_ASM"
  "jsobgtr %0,%l1")

(define_insn ""
  [(set (pc)
	(if_then_else
	 (ge (plus:SI (match_operand:SI 0 "nonimmediate_operand" "+g")
		      (const_int -1))
	     (const_int 0))
	 (label_ref (match_operand 1 "" ""))
	 (pc)))
   (set (match_dup 0)
	(plus:SI (match_dup 0)
		 (const_int -1)))]
  "!TARGET_UNIX_ASM"
  "jsobgeq %0,%l1")

;; Normal aob insns.  Define a version for when operands[1] is a constant.
(define_insn ""
  [(set (pc)
	(if_then_else
	 (lt (plus:SI (match_operand:SI 0 "nonimmediate_operand" "+g")
		      (const_int 1))
	     (match_operand:SI 1 "general_operand" "g"))
	 (label_ref (match_operand 2 "" ""))
	 (pc)))
   (set (match_dup 0)
	(plus:SI (match_dup 0)
		 (const_int 1)))]
  "!TARGET_UNIX_ASM"
  "jaoblss %1,%0,%l2")

(define_insn ""
  [(set (pc)
	(if_then_else
	 (lt (match_operand:SI 0 "nonimmediate_operand" "+g")
	     (match_operand:SI 1 "general_operand" "g"))
	 (label_ref (match_operand 2 "" ""))
	 (pc)))
   (set (match_dup 0)
	(plus:SI (match_dup 0)
		 (const_int 1)))]
  "!TARGET_UNIX_ASM && CONST_INT_P (operands[1])"
  "jaoblss %P1,%0,%l2")

(define_insn ""
  [(set (pc)
	(if_then_else
	 (le (plus:SI (match_operand:SI 0 "nonimmediate_operand" "+g")
		      (const_int 1))
	     (match_operand:SI 1 "general_operand" "g"))
	 (label_ref (match_operand 2 "" ""))
	 (pc)))
   (set (match_dup 0)
	(plus:SI (match_dup 0)
		 (const_int 1)))]
  "!TARGET_UNIX_ASM"
  "jaobleq %1,%0,%l2")

(define_insn ""
  [(set (pc)
	(if_then_else
	 (le (match_operand:SI 0 "nonimmediate_operand" "+g")
	     (match_operand:SI 1 "general_operand" "g"))
	 (label_ref (match_operand 2 "" ""))
	 (pc)))
   (set (match_dup 0)
	(plus:SI (match_dup 0)
		 (const_int 1)))]
  "!TARGET_UNIX_ASM && CONST_INT_P (operands[1])"
  "jaobleq %P1,%0,%l2")

;; Something like a sob insn, but compares against -1.
;; This finds `while (foo--)' which was changed to `while (--foo != -1)'.

(define_insn ""
  [(set (pc)
	(if_then_else
	 (ne (match_operand:SI 0 "nonimmediate_operand" "+g")
	     (const_int 0))
	 (label_ref (match_operand 1 "" ""))
	 (pc)))
   (set (match_dup 0)
	(plus:SI (match_dup 0)
		 (const_int -1)))]
  ""
  "decl %0\;jgequ %l1")

(define_expand "call_pop"
  [(parallel [(call (match_operand:QI 0 "memory_operand" "")
		    (match_operand:SI 1 "const_int_operand" ""))
	      (set (reg:SI VAX_SP_REGNUM)
		   (plus:SI (reg:SI VAX_SP_REGNUM)
			    (match_operand:SI 3 "immediate_operand" "")))])]
  ""
{
  gcc_assert (INTVAL (operands[3]) <= 255 * 4 && INTVAL (operands[3]) % 4 == 0);

  /* Operand 1 is the number of bytes to be popped by DW_CFA_GNU_args_size
     during EH unwinding.  We must include the argument count pushed by
     the calls instruction.  */
  operands[1] = GEN_INT (INTVAL (operands[3]) + 4);
})

(define_insn "*call_pop"
  [(call (match_operand:QI 0 "memory_operand" "m")
	 (match_operand:SI 1 "const_int_operand" "n"))
   (set (reg:SI VAX_SP_REGNUM) (plus:SI (reg:SI VAX_SP_REGNUM)
					(match_operand:SI 2 "immediate_operand" "i")))]
  ""
{
  operands[1] = GEN_INT ((INTVAL (operands[1]) - 4) / 4);
  return "calls %1,%0";
})

(define_expand "call_value_pop"
  [(parallel [(set (match_operand 0 "" "")
		   (call (match_operand:QI 1 "memory_operand" "")
			 (match_operand:SI 2 "const_int_operand" "")))
	      (set (reg:SI VAX_SP_REGNUM)
		   (plus:SI (reg:SI VAX_SP_REGNUM)
			    (match_operand:SI 4 "immediate_operand" "")))])]
  ""
{
  gcc_assert (INTVAL (operands[4]) <= 255 * 4 && INTVAL (operands[4]) % 4 == 0);

  /* Operand 2 is the number of bytes to be popped by DW_CFA_GNU_args_size
     during EH unwinding.  We must include the argument count pushed by
     the calls instruction.  */
  operands[2] = GEN_INT (INTVAL (operands[4]) + 4);
})

(define_insn "*call_value_pop"
  [(set (match_operand 0 "" "")
	(call (match_operand:QI 1 "memory_operand" "m")
	      (match_operand:SI 2 "const_int_operand" "n")))
   (set (reg:SI VAX_SP_REGNUM) (plus:SI (reg:SI VAX_SP_REGNUM)
					(match_operand:SI 3 "immediate_operand" "i")))]
  ""
  "*
{
  operands[2] = GEN_INT ((INTVAL (operands[2]) - 4) / 4);
  return \"calls %2,%1\";
}")

(define_expand "call"
  [(call (match_operand:QI 0 "memory_operand" "")
      (match_operand:SI 1 "const_int_operand" ""))]
  ""
  "
{
  /* Operand 1 is the number of bytes to be popped by DW_CFA_GNU_args_size
     during EH unwinding.  We must include the argument count pushed by
     the calls instruction.  */
  operands[1] = GEN_INT (INTVAL (operands[1]) + 4);
}")

(define_insn "*call"
   [(call (match_operand:QI 0 "memory_operand" "m")
       (match_operand:SI 1 "const_int_operand" ""))]
  ""
  "calls $0,%0")

(define_expand "call_value"
  [(set (match_operand 0 "" "")
      (call (match_operand:QI 1 "memory_operand" "")
            (match_operand:SI 2 "const_int_operand" "")))]
  ""
  "
{
  /* Operand 2 is the number of bytes to be popped by DW_CFA_GNU_args_size
     during EH unwinding.  We must include the argument count pushed by
     the calls instruction.  */
  operands[2] = GEN_INT (INTVAL (operands[2]) + 4);
}")

(define_insn "*call_value"
  [(set (match_operand 0 "" "")
	(call (match_operand:QI 1 "memory_operand" "m")
	      (match_operand:SI 2 "const_int_operand" "")))]
  ""
  "calls $0,%1")

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

  emit_call_insn (gen_call_pop (operands[0], const0_rtx, NULL, const0_rtx));

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

;; UNSPEC_VOLATILE is considered to use and clobber all hard registers and
;; all of memory.  This blocks insns from being moved across this point.

(define_insn "blockage"
  [(unspec_volatile [(const_int 0)] VUNSPEC_BLOCKAGE)]
  ""
  "")

(define_insn "return"
  [(return)]
  ""
  "ret")

(define_expand "epilogue"
  [(return)]
  ""
  "
{
  emit_jump_insn (gen_return ());
  DONE;
}")

(define_insn "nop"
  [(const_int 0)]
  ""
  "nop")

;; This had a wider constraint once, and it had trouble.
;; If you are tempted to try `g', please don't--it's not worth
;; the risk we will reopen the same bug.
(define_insn "indirect_jump"
  [(set (pc) (match_operand:SI 0 "register_operand" "r"))]
  ""
  "jmp (%0)")

;; This is here to accept 5 arguments (as passed by expand_end_case)
;; and pass the first 4 along to the casesi1 pattern that really does
;; the actual casesi work.  We emit a jump here to the default label
;; _before_ the casesi so that we can be sure that the casesi never
;; drops through.
;; This is suboptimal perhaps, but so is much of the rest of this
;; machine description.  For what it's worth, HPPA uses the same trick.
;;
;; operand 0 is index
;; operand 1 is the minimum bound (a const_int)
;; operand 2 is the maximum bound - minimum bound + 1 (also a const_int)
;; operand 3 is CODE_LABEL for the table;
;; operand 4 is the CODE_LABEL to go to if index out of range (ie. default).
;;
;; We emit:
;;	i = index - minimum_bound
;;	if (i > (maximum_bound - minimum_bound + 1) goto default;
;;	casesi (i, 0, table);
;;
(define_expand "casesi"
  [(match_operand:SI 0 "general_operand" "")
   (match_operand:SI 1 "general_operand" "")
   (match_operand:SI 2 "general_operand" "")
   (match_operand 3 "" "")
   (match_operand 4 "" "")]
  ""
{
  /* i = index - minimum_bound;
     But only if the lower bound is not already zero.  */
  if (operands[1] != const0_rtx)
    {
      rtx index = gen_reg_rtx (SImode);
      emit_insn (gen_addsi3 (index,
			     operands[0],
			     GEN_INT (-INTVAL (operands[1]))));
      operands[0] = index;
    }

  /* if (i > (maximum_bound - minimum_bound + 1) goto default;  */
  emit_insn (gen_cmpsi (operands[0], operands[2]));
  emit_jump_insn (gen_bgtu (operands[4]));

  /* casesi (i, 0, table);  */
  emit_jump_insn (gen_casesi1 (operands[0], operands[2], operands[3]));
  DONE;
})

;; This insn is a bit of a lier.  It actually falls through if no case
;; matches.  But, we prevent that from ever happening by emitting a jump
;; before this, see the define_expand above.
(define_insn "casesi1"
  [(match_operand:SI 1 "const_int_operand" "n")
   (set (pc)
	(plus:SI (sign_extend:SI
		  (mem:HI (plus:SI (mult:SI (match_operand:SI 0 "general_operand" "g")
					    (const_int 2))
			  (pc))))
		 (label_ref:SI (match_operand 2 "" ""))))]
  ""
  "casel %0,$0,%1")

;;- load or push effective address
;; These come after the move and add/sub patterns
;; because we don't want pushl $1 turned into pushad 1.
;; or addl3 r1,r2,r3 turned into movab 0(r1)[r2],r3.

;; It does not work to use constraints to distinguish pushes from moves,
;; because < matches any autodecrement, not just a push.

(define_insn ""
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(match_operand:QI 1 "address_operand" "p"))]
  ""
  "*
{
  if (push_operand (operands[0], SImode))
    return \"pushab %a1\";
  else
    return \"movab %a1,%0\";
}")

(define_insn ""
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(match_operand:HI 1 "address_operand" "p"))]
  ""
  "*
{
  if (push_operand (operands[0], SImode))
    return \"pushaw %a1\";
  else
    return \"movaw %a1,%0\";
}")

(define_insn ""
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(match_operand:SI 1 "address_operand" "p"))]
  ""
  "*
{
  if (push_operand (operands[0], SImode))
    return \"pushal %a1\";
  else
    return \"moval %a1,%0\";
}")

(define_insn ""
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(match_operand:DI 1 "address_operand" "p"))]
  ""
  "*
{
  if (push_operand (operands[0], SImode))
    return \"pushaq %a1\";
  else
    return \"movaq %a1,%0\";
}")

(define_insn ""
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(match_operand:SF 1 "address_operand" "p"))]
  ""
  "*
{
  if (push_operand (operands[0], SImode))
    return \"pushaf %a1\";
  else
    return \"movaf %a1,%0\";
}")

(define_insn ""
  [(set (match_operand:SI 0 "nonimmediate_operand" "=g")
	(match_operand:DF 1 "address_operand" "p"))]
  ""
  "*
{
  if (push_operand (operands[0], SImode))
    return \"pushad %a1\";
  else
    return \"movad %a1,%0\";
}")

;; These used to be peepholes, but it is more straightforward to do them
;; as single insns.  However, we must force the output to be a register
;; if it is not an offsettable address so that we know that we can assign
;; to it twice.

;; If we had a good way of evaluating the relative costs, these could be
;; machine-independent.

;; Optimize   extzv ...,z;    andl2 ...,z
;; or	      ashl ...,z;     andl2 ...,z
;; with other operands constant.  This is what the combiner converts the
;; above sequences to before attempting to recognize the new insn.

(define_insn ""
  [(set (match_operand:SI 0 "nonimmediate_operand" "=ro")
	(and:SI (ashiftrt:SI (match_operand:SI 1 "general_operand" "g")
			     (match_operand:QI 2 "const_int_operand" "n"))
		(match_operand:SI 3 "const_int_operand" "n")))]
  "(INTVAL (operands[3]) & ~((1 << (32 - INTVAL (operands[2]))) - 1)) == 0"
  "*
{
  unsigned long mask1 = INTVAL (operands[3]);
  unsigned long mask2 = (1 << (32 - INTVAL (operands[2]))) - 1;

  if ((mask1 & mask2) != mask1)
    operands[3] = GEN_INT (mask1 & mask2);

  return \"rotl %R2,%1,%0\;bicl2 %N3,%0\";
}")

;; left-shift and mask
;; The only case where `ashl' is better is if the mask only turns off
;; bits that the ashl would anyways, in which case it should have been
;; optimized away.

(define_insn ""
  [(set (match_operand:SI 0 "nonimmediate_operand" "=ro")
	(and:SI (ashift:SI (match_operand:SI 1 "general_operand" "g")
			   (match_operand:QI 2 "const_int_operand" "n"))
		(match_operand:SI 3 "const_int_operand" "n")))]
  ""
  "*
{
  operands[3]
    = GEN_INT (INTVAL (operands[3]) & ~((1 << INTVAL (operands[2])) - 1));
  return \"rotl %2,%1,%0\;bicl2 %N3,%0\";
}")

;; Instruction sequence to sync the VAX instruction stream.
(define_insn "sync_istream"
  [(unspec_volatile [(const_int 0)] VUNSPEC_SYNC_ISTREAM)]
  ""
  "movpsl -(%|sp)\;pushal 1(%|pc)\;rei")
